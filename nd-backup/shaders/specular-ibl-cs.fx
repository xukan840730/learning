//------------------------------------------------------------------------------------------------------------
#include "global-funcs.fxi"
#include "packing.fxi"
#include "tile-util.fxi"
#include "culling.fxi"

// Copy-pasted this here so as not to include reflection-util - which brings in a whole boatload of stuff
// Using the reflection direction for specular cubemaps is not entirely
// accurate since the dominant direction of the GGX lobe is aligned slightly
// off the reflection direction in some cases
// Reference: Frostbite slides
// NOTE: WE DON'T NORMALIZE
float3 GetGGXDominantDirection(float3 normalWS, float3 reflectWS, float roughness)
{
	float smoothness = saturate(1.f - roughness);
	float factor = smoothness * (sqrt(smoothness) + roughness);
	return lerp(normalWS, reflectWS, factor);
}

#define kMaxNumCubes       250
#define kNumCubemapDwords  8   // Must be enough to hold kMaxNumCubes bits

struct CubemapInfo
{
	float3					m_blendFadeStart;
	uint					m_priority;
	float3					m_blendFadeScale;
	float					m_unboundedProbesAlpha;
	float4					m_influenceTransformFromVS[3];
	float4					m_proxyTransformFromVS[3];
	float4					m_proxyTransformFromWS[3];
	float3					m_proxyTransformToWS[3];
	float3					m_proxyLocalPosition;
	uint					m_unbounded;

	float3					m_xSide;
	float3					m_ySide;
	float3					m_zSide;
	float3					m_center;
	float3					m_extents;
};

struct CubemapBits
{
	uint m_bits[kNumCubemapDwords];
};

struct SpecularIblCullSrt
{
	Texture2D<float2>				m_16thMinMaxDepth;

	RW_RegularBuffer<CubemapBits>	m_cubemapsPerTile;
	SamplerState					m_linearClampSampler;

	CubemapInfo						m_cubemaps[kMaxNumCubes];

	float3							m_topLeftCornerVS;
	float2							m_tileSizeVS;
	float2							m_invHtileSize;
	float2							m_depthParams;
	float4x4						screenToView;
	uint2							screenSize;

	uint							m_numCubemaps;
	uint							m_numTilesX;
};


// There are 4 tiles per threadgroup. And we allocate 8 uints for the light indices (for a max of 256)
groupshared uint g_cubemapsInTile[4][kNumCubemapDwords];

// Each 4x4 thread quad does one tile. So a threadgroup does 2x2 tiles
[NUM_THREADS(8, 8, 1)]
void CS_SpecularIblCull(
	uint2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	uint2 dispatchId : S_GROUP_ID,
	uint2 threadId : S_GROUP_THREAD_ID,
	SpecularIblCullSrt *pSrt : S_SRT_DATA)
{
	const uint2 quadId = threadId / 4;
	const uint quadIndex = quadId.x + quadId.y*2;
	const uint2 tileCoord = dispatchId * 2 + quadId;
	const uint tileIndex = tileCoord.x + mul24(tileCoord.y, pSrt->m_numTilesX);
	const uint threadQuadIndex = (threadId.x % 4) + (threadId.y % 4) * 4;
	
	if (threadQuadIndex == 0)
	{
		for (uint i = 0; i < kNumCubemapDwords; i++)
		{
			g_cubemapsInTile[quadIndex][i] = 0;
		}
	}

	// Get min/max depth 
	float2 minMaxDepth = pSrt->m_16thMinMaxDepth.Load(int3(tileCoord, 0));
	float2 minMaxDepthVS = pSrt->m_depthParams.yy / (minMaxDepth - pSrt->m_depthParams.xx); 

	// Generate tile frustum (froxel) vertices
	const float tileSize = 16.0f;
	float2 tileSizeNDC = (tileSize * 2.0f).xx / float2(pSrt->screenSize);
	tileSizeNDC.y *= -1.0f;
	float2 topLeftNDC = float2(-1.0f, 1.0f) + tileSizeNDC * tileCoord;
	float2 bottomRightNDC = topLeftNDC + tileSizeNDC;

	// Start with clockwise calculation of the near plane points
	float3 froxelVertices[8];
	froxelVertices[0] = float3(topLeftNDC.xy, 0);
	froxelVertices[1] = float3(bottomRightNDC.x, topLeftNDC.y, 0);
	froxelVertices[2] = float3(bottomRightNDC.xy, 0);
	froxelVertices[3] = float3(topLeftNDC.x, bottomRightNDC.y, 0);
	froxelVertices[4] = float3(topLeftNDC.xy, 0);
	froxelVertices[5] = float3(bottomRightNDC.x, topLeftNDC.y, 0);
	froxelVertices[6] = float3(bottomRightNDC.xy, 0);
	froxelVertices[7] = float3(topLeftNDC.x, bottomRightNDC.y, 0);

	// Generate AABB of tile frustum
	// Mostly taken from deferred lights cull
	float3 froxelAabbMin = froxelVertices[0];
	float3 froxelAabbMax = froxelVertices[0];
	for (uint i = 0; i < 8; ++i)
	{
		float z = i < 4 ? minMaxDepth.x : minMaxDepth.y;
		float4 viewVert = mul(pSrt->screenToView, float4(froxelVertices[i].xy, z, 1.f)); // this shader has column-major matrix packing
		float viewZ = i < 4 ? minMaxDepthVS.x : minMaxDepthVS.y;
		froxelVertices[i] = (float3(viewVert.xy / viewVert.w, viewZ));
		froxelAabbMin.xy = float2(min(froxelAabbMin.x, froxelVertices[i].x), min(froxelAabbMin.y, froxelVertices[i].y));
		froxelAabbMax.xy = float2(max(froxelAabbMax.x, froxelVertices[i].x), max(froxelAabbMax.y, froxelVertices[i].y));
	}
	froxelAabbMin.z = froxelVertices[0].z;
	froxelAabbMax.z = froxelVertices[4].z;
	
	// Generate tile frustum side planes
	// Mostly taken from deferred lights cull
	float4 froxelSidePlanes[4];
	{
		float3 tileStartVS = pSrt->m_topLeftCornerVS + float3(tileCoord*pSrt->m_tileSizeVS, 0);
		float3 tileEndVS = tileStartVS + float3(pSrt->m_tileSizeVS, 0);

		// The compiler is smart enough to optimize all the unnecessary math by having 0 components below.
		froxelSidePlanes[0] = (float4(normalize(cross(tileStartVS, float3(0,1,0))), 0));
		froxelSidePlanes[1] = (float4(normalize(cross(tileEndVS, float3(0,-1,0))), 0));
		froxelSidePlanes[2] = (float4(normalize(cross(tileStartVS, float3(-1,0,0))), 0));
		froxelSidePlanes[3] = (float4(normalize(cross(tileEndVS, float3(1,0,0))), 0));
	}

	// Make sure zero'ing out the LDS is complete
	ThreadGroupMemoryBarrier();

	// Loop over the cubemaps and cull their OBB 
	for (uint i = 0; i < pSrt->m_numCubemaps; i += 16)
	{
		uint lightIndex = i + threadQuadIndex;
		if (lightIndex < pSrt->m_numCubemaps)
		{
			const CubemapInfo cubemap = pSrt->m_cubemaps[lightIndex];
			bool intersects = true;
			
			if (cubemap.m_unbounded == 0)
			{
				intersects = FroxelObbOverlap(froxelAabbMin, froxelAabbMax, froxelSidePlanes, cubemap.m_center, 
											  cubemap.m_xSide, cubemap.m_ySide, cubemap.m_zSide);
			}

			if (intersects)
			{
				// We use a bit per light - find out which dword the bit belongs to
				uint priorityIndex = pSrt->m_numCubemaps - lightIndex;
				uint dwordIndex = priorityIndex / 32;
				uint lightbit = 1 << (priorityIndex % 32);

				uint newBitField;
				AtomicOr(g_cubemapsInTile[quadIndex][dwordIndex], lightbit, newBitField);
			}
		}
	}

	ThreadGroupMemoryBarrier();

	// Save off the results into the output buffer
	if (threadQuadIndex == 0)
	{
		CubemapBits bits;

		for (uint i = 0; i < kNumCubemapDwords; i++)
		{
			bits.m_bits[i] = g_cubemapsInTile[quadIndex][i];
		}

		pSrt->m_cubemapsPerTile[tileIndex] = bits;
	}
}

struct SpecularIblSrt
{
	uint						m_bufWidth;
	uint						m_bufHeight;
	uint						m_numCubes;
	float						m_iblScale;
	float2						m_depthParams;
	uint						m_debugDrawNumCubesPerBlock;
	uint						m_numTilesX;
	float4						m_cameraPos;
	float4						m_screenScaleOffset;
	float4						m_viewToWorldMat[3];
	CubemapInfo					m_cubemapInfoList[kMaxNumCubes];
	RegularBuffer<CubemapBits>	m_cubemapsInTile;
	RWTexture2D<float4>			m_rbSpecularIblBuffer;
	RWTexture2D<float3>			m_rbHalfResBuffer;
	Texture2D<float3>			m_srcHalfResBuffer;
	Texture2D<float>			m_srcDepth;
	Texture2D<uint4>			m_normalBuffer;
	TextureCube<float4>			m_cubemaps[kMaxNumCubes];
	SamplerState				m_LinearSampler;
	Texture2D<float>			m_opaqueAlphaDepth;
	Texture2D<uint>				m_opaqueAlphaStencil;
	Texture2D<float4>			m_gbufferTransparent;
	float						m_roughnessThreshold;
	uint						m_enableHalfRes : 1;
	uint						m_iblOnWater : 1;
};

static const int kNumRoughnessMips		= 7;

float3 DoSpecularIbl(float3 worldReflDir, float lod, float3 positionVS, CubemapBits cubemapsInTile, SpecularIblSrt srt)
{
	float4 sumColors = float4(0, 0, 0, 1.0f);
	float3 cubeColor = float3(0, 0, 0);
	bool bFinish = false;

	// Read in the culling data
	uint numCubemaps = 0;
	for (uint i = 0; i < kNumCubemapDwords; i++)
	{
		numCubemaps += cubemapsInTile.m_bits[i];
	}

	for (int dwordIndex = kNumCubemapDwords - 1; dwordIndex >= 0 && !bFinish; --dwordIndex)
	{
		while (cubemapsInTile.m_bits[dwordIndex] > 0 && !bFinish)
		{
			uint setBit = FirstSetBit_Hi(cubemapsInTile.m_bits[dwordIndex]);

			cubemapsInTile.m_bits[dwordIndex] &= ~(1 << setBit);

			uint cubeIndex = ReadFirstLane(srt.m_numCubes - (32*dwordIndex + setBit));

			CubemapInfo   cubeInfo = srt.m_cubemapInfoList[cubeIndex];
			float3 proxyPosInRegionSpace;
			proxyPosInRegionSpace.x = dot(cubeInfo.m_proxyTransformFromVS[0], float4(positionVS, 1.0f));
			proxyPosInRegionSpace.y = dot(cubeInfo.m_proxyTransformFromVS[1], float4(positionVS, 1.0f));
			proxyPosInRegionSpace.z = dot(cubeInfo.m_proxyTransformFromVS[2], float4(positionVS, 1.0f));

			float3 influencePosInRegionSpace;
			influencePosInRegionSpace.x = dot(cubeInfo.m_influenceTransformFromVS[0], float4(positionVS, 1.0f));
			influencePosInRegionSpace.y = dot(cubeInfo.m_influenceTransformFromVS[1], float4(positionVS, 1.0f));
			influencePosInRegionSpace.z = dot(cubeInfo.m_influenceTransformFromVS[2], float4(positionVS, 1.0f));
			float3 absInfluencePosInRegionSpace = abs(influencePosInRegionSpace);

			if (all(absInfluencePosInRegionSpace < 1.0f))
			{
				bool bNeedBlend = any(absInfluencePosInRegionSpace > cubeInfo.m_blendFadeStart);
				bool bBounded = cubeInfo.m_unbounded == 0;

				float3 cubeDir = worldReflDir;
				if (bBounded)
				{
					float3 dirInRegionSpace;
					dirInRegionSpace.x = dot(cubeInfo.m_proxyTransformFromWS[0].xyz, worldReflDir);
					dirInRegionSpace.y = dot(cubeInfo.m_proxyTransformFromWS[1].xyz, worldReflDir);
					dirInRegionSpace.z = dot(cubeInfo.m_proxyTransformFromWS[2].xyz, worldReflDir);

					float tx = (sign(dirInRegionSpace.x) * 1.0f - proxyPosInRegionSpace.x) / (dirInRegionSpace.x == 0.0f ? 0.00001f : dirInRegionSpace.x);
					float ty = (sign(dirInRegionSpace.y) * 1.0f - proxyPosInRegionSpace.y) / (dirInRegionSpace.y == 0.0f ? 0.00001f : dirInRegionSpace.y);
					float tz = (sign(dirInRegionSpace.z) * 1.0f - proxyPosInRegionSpace.z) / (dirInRegionSpace.z == 0.0f ? 0.00001f : dirInRegionSpace.z);

					float t = min3(tx, ty, tz);
					float3 hitPosition = proxyPosInRegionSpace + dirInRegionSpace * t - cubeInfo.m_proxyLocalPosition;
					cubeDir.x = dot(cubeInfo.m_proxyTransformToWS[0].xyz, hitPosition);
					cubeDir.y = dot(cubeInfo.m_proxyTransformToWS[1].xyz, hitPosition);
					cubeDir.z = dot(cubeInfo.m_proxyTransformToWS[2].xyz, hitPosition);

					// No need to normalize cubemap lookup vectors
				}

				float fadeScale = cubeInfo.m_unboundedProbesAlpha;
				if (bBounded && bNeedBlend)
				{
					float3 fadeScaleXYZ = 1.0f - max(absInfluencePosInRegionSpace - cubeInfo.m_blendFadeStart, 0.0f) * cubeInfo.m_blendFadeScale;
						fadeScale = fadeScaleXYZ.x * fadeScaleXYZ.y * fadeScaleXYZ.z;
				}

				cubeColor = srt.m_cubemaps[cubeIndex].SampleLevel(srt.m_LinearSampler, cubeDir, lod).xyz;

				if (bBounded && !bNeedBlend)
				{
					sumColors.xyz += cubeColor * sumColors.w;
					bFinish = true;
				}
				else
				{
					sumColors.xyz += cubeColor * fadeScale * sumColors.w;
					sumColors.w *= (1.0f - fadeScale);
				}
			}
		}
	}

	float3 finalIblColor = bFinish ? sumColors.xyz : (sumColors.xyz + cubeColor.xyz * sumColors.w);
	return finalIblColor;
}

[numthreads(8, 8, 1)]
void Cs_SpecularIblHalfRes(uint2 dispatchThreadId : SV_DispatchThreadID, SpecularIblSrt *pSrt : S_SRT_DATA)
{
	const uint2 fullResCoord = dispatchThreadId * 2;
	const uint2 tileCoord = dispatchThreadId / 8;
	const uint tileOffset = tileCoord.x + mul24(tileCoord.y, pSrt->m_numTilesX);

	// As a first step, just use the roughness of the top-left pixel
	const uint4 sample0 = pSrt->m_normalBuffer.Load(int3(fullResCoord, 0)).zzzw;
	float4 normalRoughness = UnpackGBufferNormalRoughness(sample0);
	float3 worldNormal = normalRoughness.xyz;
	float roughness = normalRoughness.w;

	if (roughness >= pSrt->m_roughnessThreshold)
	{
		float rawDepth = pSrt->m_srcDepth[fullResCoord];
		float depthVS = pSrt->m_depthParams.y / (rawDepth - pSrt->m_depthParams.x);
		float3 basePositionVS = float3(((fullResCoord.xy + 0.5f) * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f);
		float3 positionVS = basePositionVS * depthVS;

		float3 positionWS;
		positionWS.x = dot(pSrt->m_viewToWorldMat[0], float4(positionVS, 1.0f));
		positionWS.y = dot(pSrt->m_viewToWorldMat[1], float4(positionVS, 1.0f));
		positionWS.z = dot(pSrt->m_viewToWorldMat[2], float4(positionVS, 1.0f));

		// find which level of the cubemap we should sample
		float lod			= roughness * kNumRoughnessMips;

		float3 worldViewDir		= pSrt->m_cameraPos.xyz - positionWS;
		float3 reflectDir		= normalize( reflect( -worldViewDir, worldNormal ) );
		float3 worldReflDir		= GetGGXDominantDirection(worldNormal, reflectDir, roughness);	

		CubemapBits cubemapsInTile = pSrt->m_cubemapsInTile[tileOffset];

		float3 finalIblColor = DoSpecularIbl(worldReflDir, lod, positionVS, cubemapsInTile, *pSrt);
		finalIblColor *= pSrt->m_iblScale;

		pSrt->m_rbHalfResBuffer[dispatchThreadId.xy] = finalIblColor;
	}
	else
	{
		pSrt->m_rbHalfResBuffer[dispatchThreadId.xy] = 0;
	}
}

[numthreads(8, 8, 1)]
void Cs_SpecularIblAllHalfRes(uint2 dispatchThreadId : SV_DispatchThreadID, SpecularIblSrt *pSrt : S_SRT_DATA)
{
	const uint2 fullResCoord = dispatchThreadId * 2;
	const uint2 tileCoord = dispatchThreadId / 8;
	const uint tileOffset = tileCoord.x + mul24(tileCoord.y, pSrt->m_numTilesX);

	// As a first step, just use the roughness of the top-left pixel
	const uint4 sample0 = pSrt->m_normalBuffer.Load(int3(fullResCoord, 0)).zzzw;
	float4 normalRoughness = UnpackGBufferNormalRoughness(sample0);
	float3 worldNormal = normalRoughness.xyz;
	float roughness = normalRoughness.w;

	const bool isWater = (pSrt->m_opaqueAlphaStencil[fullResCoord] & 2) && pSrt->m_iblOnWater;
	if (isWater)
	{
		worldNormal = pSrt->m_gbufferTransparent[fullResCoord].xyz;
		roughness = pSrt->m_gbufferTransparent[fullResCoord].w;
	}

	float rawDepth = (isWater) ? pSrt->m_opaqueAlphaDepth[fullResCoord] : pSrt->m_srcDepth[fullResCoord];
	float depthVS = pSrt->m_depthParams.y / (rawDepth - pSrt->m_depthParams.x);
	float3 basePositionVS = float3(((fullResCoord.xy + 0.5f) * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f);
	float3 positionVS = basePositionVS * depthVS;
	
	float3 positionWS;
	positionWS.x = dot(pSrt->m_viewToWorldMat[0], float4(positionVS, 1.0f));
	positionWS.y = dot(pSrt->m_viewToWorldMat[1], float4(positionVS, 1.0f));
	positionWS.z = dot(pSrt->m_viewToWorldMat[2], float4(positionVS, 1.0f));

	// find which level of the cubemap we should sample
	float lod			= roughness * kNumRoughnessMips;

	float3 worldViewDir		= pSrt->m_cameraPos.xyz - positionWS;
	float3 reflectDir		= normalize( reflect( -worldViewDir, worldNormal ) );
	float3 worldReflDir		= GetGGXDominantDirection(worldNormal, reflectDir, roughness);	

	CubemapBits cubemapsInTile = pSrt->m_cubemapsInTile[tileOffset];

	float3 finalIblColor = DoSpecularIbl(worldReflDir, lod, positionVS, cubemapsInTile, *pSrt);
	finalIblColor *= pSrt->m_iblScale;

	pSrt->m_rbHalfResBuffer[dispatchThreadId.xy] = finalIblColor;
}

[numthreads(8, 8, 1)]
void Cs_SpecularIbl(uint3 dispatchThreadId : SV_DispatchThreadID, SpecularIblSrt* pSrt : S_SRT_DATA)
{
	// Get the htile of the current pixel
	const uint2 screenCoord = uint2(dispatchThreadId.xy);
	const uint2 tileCoord = screenCoord / 16;
	const uint tileOffset = tileCoord.x + mul24(tileCoord.y, pSrt->m_numTilesX);

	bool isWater = (pSrt->m_opaqueAlphaStencil[screenCoord] & 2) && pSrt->m_iblOnWater;

	float rawDepth = (isWater) ? pSrt->m_opaqueAlphaDepth[screenCoord] : pSrt->m_srcDepth[screenCoord];
	float depthVS = pSrt->m_depthParams.y / (rawDepth - pSrt->m_depthParams.x);

	float3 basePositionVS = float3(((dispatchThreadId.xy + 0.5f) * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f);
    float3 positionVS = basePositionVS * depthVS;

    float3 positionWS;
	positionWS.x = dot(pSrt->m_viewToWorldMat[0], float4(positionVS, 1.0f));
	positionWS.y = dot(pSrt->m_viewToWorldMat[1], float4(positionVS, 1.0f));
    positionWS.z = dot(pSrt->m_viewToWorldMat[2], float4(positionVS, 1.0f));

	const uint4 sample0 = pSrt->m_normalBuffer.Load(int3(screenCoord, 0)).zzzw;
	float4 normalRoughness = UnpackGBufferNormalRoughness(sample0);
	float3 worldNormal = normalRoughness.xyz;
	float roughness = normalRoughness.w;

	if (isWater)
	{
		worldNormal = pSrt->m_gbufferTransparent[screenCoord].xyz;
		roughness = pSrt->m_gbufferTransparent[screenCoord].w;
	}

	//bool needHalfRes = pSrt->m_enableHalfRes && roughness > pSrt->m_roughnessThreshold;
	bool needHalfRes = roughness >= pSrt->m_roughnessThreshold && !isWater;

	float3 finalIblColor;

	if (pSrt->m_enableHalfRes && needHalfRes)
	{
		finalIblColor = pSrt->m_srcHalfResBuffer[dispatchThreadId.xy / 2];
	}
	else
	{
		// find which level of the cubemap we should sample
		float lod			= roughness * kNumRoughnessMips;

		float3 worldViewDir		= pSrt->m_cameraPos.xyz - positionWS;
		float3 reflectDir		= normalize( reflect( -worldViewDir, worldNormal ) );
		float3 worldReflDir		= GetGGXDominantDirection(worldNormal, reflectDir, roughness);	

		// Read in the culling data
		CubemapBits cubemapsInTile = pSrt->m_cubemapsInTile[tileOffset];

		finalIblColor = DoSpecularIbl(worldReflDir, lod, positionVS, cubemapsInTile, *pSrt);
		finalIblColor *= pSrt->m_iblScale;
	}

	if (pSrt->m_debugDrawNumCubesPerBlock)
	{
		CubemapBits cubemapsInTile = pSrt->m_cubemapsInTile[tileOffset];

		uint finalNumCubes = 0;
		for (uint i = 0; i < kNumCubemapDwords; i++)
		{
			finalNumCubes += CountSetBits(cubemapsInTile.m_bits[i]);
		}

		finalIblColor = finalNumCubes;
	}

    pSrt->m_rbSpecularIblBuffer[int2(dispatchThreadId.xy)] = float4(finalIblColor, 0.0f);
}
