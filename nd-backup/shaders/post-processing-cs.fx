#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "post-processing-common.fxi"
#include "packing.fxi"
#include "stencil-util.fxi"
#include "high-contrast-mode-post-utils.fxi"
#include "sobel-filter.fxi"

// =====================================================================================================================

struct RandomnizeRsmTextures
{
	Texture2D<float4>   tOriginalRsm0;
	Texture2D<float4>   tOriginalRsm1;
	Texture3D<float2>	tLDUvSample;
	SamplerState		samplerPoint;
};

struct VirtualPointLight
{
	float3 m_color;
	float3 m_wsNormal;
	float3 m_wsPosition;
};

struct RandomnizeRsmBuffers
{
	RWStructuredBuffer<VirtualPointLight> rwRandomnizedVpls;
	RWStructuredBuffer<uint> rwNumVplsInGroups;
};

struct RandomnizeRsmConsts
{
	float4 m_viewToWorldMatVpl[4][3];
	uint m_vplParams[4];	// num vpls per pixel, ld uv sample texture size, rsm size, viewport size
	float m_uvScale;
};

struct RandomnizeRsmSrt
{
	RandomnizeRsmConsts* pConsts;
	RandomnizeRsmTextures* pTextures;
	RandomnizeRsmBuffers* pBuffers;
};

groupshared uint g_nextSlot;
groupshared uint g_groupOffset;
groupshared uint g_groupIdFlattened;
static const float2 offsetFactors[4] = {float2(0.f, 0.f), float2(-0.5f, 0.f), float2(0.f, -0.5f), float2(-0.5f, -0.5f)};
[numthreads(8, 8, 1)]
void CS_RandomnizeRsm(uint3 dispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, RandomnizeRsmSrt srt : S_SRT_DATA)
{
	g_nextSlot = 0;
	g_groupIdFlattened = groupId.y * srt.pConsts->m_vplParams[1] / 8 + groupId.x;
	g_groupOffset = g_groupIdFlattened * srt.pConsts->m_vplParams[0] * 8 * 8;

	GroupMemoryBarrierWithGroupSync();

	for (uint i = 0; i < srt.pConsts->m_vplParams[0]; i++)
	{
		float2 ldUv = srt.pTextures->tLDUvSample.Load(int4(dispatchThreadId.xy, i, 0));
		uint2 vplScreenLocation = uint2(ldUv * srt.pConsts->m_vplParams[2]);
		uint2 viewportIndex = vplScreenLocation / srt.pConsts->m_vplParams[3];
		uint matrixIndex = viewportIndex.y * 2 + viewportIndex.x;

		float4 vplColor = srt.pTextures->tOriginalRsm0.SampleLevel(srt.pTextures->samplerPoint, ldUv, 0);
		if (dot(vplColor.rgb, vplColor.rgb) > 0.f)
		{
			float4 vplWsNormalVsz = srt.pTextures->tOriginalRsm1.SampleLevel(srt.pTextures->samplerPoint, ldUv, 0);

			float2 vplNdc = (ldUv + offsetFactors[matrixIndex]) * srt.pConsts->m_uvScale * float2(2.f, -2.f) - float2(1.f, -1.f);
			float4 vplVsPos = float4(vplNdc * vplWsNormalVsz.w, vplWsNormalVsz.w, 1.f);
			float3 vplWsPos;
			vplWsPos.x = dot(srt.pConsts->m_viewToWorldMatVpl[matrixIndex][0], vplVsPos);
			vplWsPos.y = dot(srt.pConsts->m_viewToWorldMatVpl[matrixIndex][1], vplVsPos);
			vplWsPos.z = dot(srt.pConsts->m_viewToWorldMatVpl[matrixIndex][2], vplVsPos);

			VirtualPointLight vpl;
			vpl.m_color = vplColor.rgb * vplColor.a;
			vpl.m_wsNormal = vplWsNormalVsz.xyz;
			vpl.m_wsPosition = vplWsPos;

			uint oldSlot;
			InterlockedAdd(g_nextSlot, 1, oldSlot);
			uint bufferIndex = oldSlot + g_groupOffset;
			srt.pBuffers->rwRandomnizedVpls[bufferIndex] = vpl;
		}
	}

	GroupMemoryBarrierWithGroupSync();

	srt.pBuffers->rwNumVplsInGroups[g_groupIdFlattened] = g_nextSlot;
}

struct DefragmentVplsBuffers
{
	StructuredBuffer<uint> numVplsInGroups;
	StructuredBuffer<VirtualPointLight> inputBuffer;
	RWStructuredBuffer<VirtualPointLight> rwOutputBuffer;
	RWStructuredBuffer<uint> rwTotalNumValidVpls;
};

struct DefragmentVplsSrt
{
	DefragmentVplsBuffers* pBuffers;
	uint numGroupsX;
	uint numVplsPerGroup;
	uint numVplsPerPixel;
};

[numthreads(8, 8, 1)]
void CS_DefragmentVpls(int2 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex, DefragmentVplsSrt srt : S_SRT_DATA)
{
	uint groupIdFlattened = groupId.y * srt.numGroupsX + groupId.x;
	uint groupInputOffset = groupIdFlattened * srt.numVplsPerGroup;
	uint numVplsInGroup = srt.pBuffers->numVplsInGroups[groupIdFlattened];
	
	uint groupOutputOffset = 0;
	g_nextSlot = 0;

	for (uint i = 0; i < groupIdFlattened; i++)
		groupOutputOffset += srt.pBuffers->numVplsInGroups[i];

	GroupMemoryBarrierWithGroupSync();

	uint threadInputIndex = groupIndex * srt.numVplsPerPixel;
	for (uint i = 0; i < srt.numVplsPerPixel; i++, threadInputIndex++)
	{
		if (threadInputIndex < numVplsInGroup)
		{
			uint oldSlot;
			InterlockedAdd(g_nextSlot, 1, oldSlot);
			srt.pBuffers->rwOutputBuffer[groupOutputOffset + oldSlot] = srt.pBuffers->inputBuffer[groupInputOffset + threadInputIndex];
		}
	}

	if (groupIdFlattened == 63)
	{
		srt.pBuffers->rwTotalNumValidVpls[0] = groupOutputOffset + numVplsInGroup;
	}
}

// =====================================================================================================================
struct SelectVplTextures
{
	Texture2D<float4> txFlux;
	Texture2D<float4> txWsNormalViewZ;
};

struct VplCount
{
	uint m_count;
	uint m_padding[3];
};

struct SelectVplBuffers
{
	RWStructuredBuffer<VirtualPointLight> rwOutputVpls;
	RWStructuredBuffer<VplCount> rwOutputVplCount;
};

struct SelectVplsConst
{
	float4 m_viewToWorldMatVpl[4][3];
	uint4 m_vplParams;	// x uv scale, y location scale, z rsm size, w viewport size
};

struct SelectVplSrt
{
	SelectVplsConst *pConsts;
	SelectVplTextures *pTextures;
	SelectVplBuffers *pBuffers;
};

groupshared uint gs_outputBufferIndex;

[numthreads(16, 16, 1)]
void CS_SelectVpls(uint3 dispatchThreadId : SV_DispatchThreadId, 
                   uint groupIndex : SV_GroupIndex,
                   SelectVplSrt srt : S_SRT_DATA)
{
	if (groupIndex == 0)
	{
		gs_outputBufferIndex = 0;	//initialize groupshared variable
	}

	GroupMemoryBarrierWithGroupSync();

	uint2 viewportIndex = dispatchThreadId.xy  * srt.pConsts->m_vplParams[1] / srt.pConsts->m_vplParams[3];
	uint matrixIndex = viewportIndex.y * 2 + viewportIndex.x;
	float4 color = srt.pTextures->txFlux.Load(int3(dispatchThreadId));

	if (dot(color.rgb, color.rgb) > 0.f)
	{
		VirtualPointLight vpl;
		vpl.m_color = color.rgb;
		float4 wsNormalViewZ = srt.pTextures->txWsNormalViewZ.Load(int3(dispatchThreadId));
		vpl.m_wsNormal = normalize(wsNormalViewZ.xyz);
		
		float2 uv = (dispatchThreadId.xy + 0.5f) / srt.pConsts->m_vplParams[2];
		float2 adjustedUv = (uv + offsetFactors[matrixIndex]) * srt.pConsts->m_vplParams[0];
		float2 vplNdc = adjustedUv * float2(2.f, -2.f) - float2(1.f, -1.f);
		float4 vplVsPos = float4(vplNdc * wsNormalViewZ.w, wsNormalViewZ.w, 1.f);
		float3 vplWsPos;
		vplWsPos.x = dot(srt.pConsts->m_viewToWorldMatVpl[matrixIndex][0], vplVsPos);
		vplWsPos.y = dot(srt.pConsts->m_viewToWorldMatVpl[matrixIndex][1], vplVsPos);
		vplWsPos.z = dot(srt.pConsts->m_viewToWorldMatVpl[matrixIndex][2], vplVsPos);
		vpl.m_wsPosition = vplWsPos;

		uint currOutputBufferIndex;
		InterlockedAdd(gs_outputBufferIndex, 1, currOutputBufferIndex);
		srt.pBuffers->rwOutputVpls[currOutputBufferIndex] = vpl;
	}

	GroupMemoryBarrierWithGroupSync();

	if (groupIndex == 255)
	{
		srt.pBuffers->rwOutputVplCount[0].m_count = gs_outputBufferIndex;
	}
}

// =====================================================================================================================

struct ComputeBouncedLightingTextures
{
	Texture2D<float4>   tDepthBuffer;
	Texture2D<uint4>	tGBuffer0;
	Texture2D<uint4>	tGBuffer1;
	RWTexture2D<float4>	rwtBouncedLighting;
	SamplerState		samplerPoint;
};

struct ComputeBouncedLightingBuffers
{
	StructuredBuffer<VirtualPointLight> bVpls;
	StructuredBuffer<uint> totalNumValidVpls;
};

struct ComputeBouncedLightingConst
{
	float4 m_camPosIntensity;	//x, y, z are camera position, w intensity scaler
	float4 m_screenToViewParams;
	float4 m_screenSizeParams;	//unused, unused, 1 / width, 1 / height
	float4 m_viewToWorldMat[3];
	uint4 m_vplParams;	//LD uv sample texture size, num vpls per pixel, screen block offset x, screen block offset y
	uint2 m_numThreadGroups;
	float m_specMultiplier;
	float m_minRoughness;
	float m_maxMetallic;
};

struct ComputeBouncedLightingSrt
{
	ComputeBouncedLightingConst *pConsts;
	ComputeBouncedLightingTextures *pTextures;
	ComputeBouncedLightingBuffers *pBuffers;
};

//groupshared VirtualPointLight g_vpls[16];

void ComputeBouncedLighting(uint3 groupId, uint3 groupThreadId, ComputeBouncedLightingSrt srt)
{
	// load vpls into lds
	uint vplCount = srt.pBuffers->totalNumValidVpls[0] / (srt.pConsts->m_vplParams.x * srt.pConsts->m_vplParams.x);
	uint vplBufferOffset = (groupId.y * srt.pConsts->m_vplParams.x + groupId.x + srt.pConsts->m_vplParams.y) * vplCount;
	vplBufferOffset %= srt.pBuffers->totalNumValidVpls[0];
	//for (uint i = 0; i < vplCount; i++)
	//	g_vpls[i] = srt.pBuffers->bVpls[vplBufferOffset + i];

	//GroupMemoryBarrierWithGroupSync();

	uint2 screenLocation = groupThreadId.xy * srt.pConsts->m_numThreadGroups + groupId.xy + srt.pConsts->m_vplParams.zw;
	float invPdfNormFactor = srt.pBuffers->totalNumValidVpls[0];

	// reconstruct pixel's world position from depth buffer z
	float depthBufferZ = srt.pTextures->tDepthBuffer.Load(int3(screenLocation, 0)).x;
	float viewSpaceZ = srt.pConsts->m_screenToViewParams.w / (depthBufferZ - srt.pConsts->m_screenToViewParams.z);
	float2 ndc = (screenLocation + float2(0.5f, 0.5f)) * srt.pConsts->m_screenSizeParams.zw;
	ndc = ndc * float2(2.f, -2.f) - float2(1.f, -1.f);
	float4 viewSpacePos = float4(ndc * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.f);
	float3 worldSpacePos;
	worldSpacePos.x = dot(srt.pConsts->m_viewToWorldMat[0], viewSpacePos);
	worldSpacePos.y = dot(srt.pConsts->m_viewToWorldMat[1], viewSpacePos);
	worldSpacePos.z = dot(srt.pConsts->m_viewToWorldMat[2], viewSpacePos);

	// extract pixel's GBuffer info
	uint4 gBuffer0 = srt.pTextures->tGBuffer0.Load(int3(screenLocation, 0));
	uint4 gBuffer1 = srt.pTextures->tGBuffer1.Load(int3(screenLocation, 0));
	BrdfParameters brdfParameters;
	InitBrdfParameters(brdfParameters);
	Setup setup;
	UnpackGBuffer(gBuffer0, gBuffer1, brdfParameters, setup);
	bool bIsCharacter = gBuffer1.w & 1;
	brdfParameters.roughness = max(brdfParameters.roughness, srt.pConsts->m_minRoughness);
	brdfParameters.metallic = min(brdfParameters.metallic, srt.pConsts->m_maxMetallic);

	float alphaDX = brdfParameters.roughness * brdfParameters.roughness;
	float alphaDXSq = alphaDX * alphaDX;

	float3 F0 = brdfParameters.specular * 0.08;
	F0 = lerp(F0, brdfParameters.baseColor, brdfParameters.metallic);

	float3 worldViewVector = normalize(srt.pConsts->m_camPosIntensity.xyz - worldSpacePos);

	// accumulate lighting from vpls
	float3 diffuseLighting = float3(0.f, 0.f, 0.f);
	float3 specularLighting = float3(0.f, 0.f, 0.f);

	for (uint i = 0; i < vplCount; i++)
	{
		VirtualPointLight vpl = srt.pBuffers->bVpls[vplBufferOffset + i];
		//VirtualPointLight vpl = g_vpls[i];

		// extract vpl's properties
		float3 lightColor = vpl.m_color.rgb;
		float3 lightNormal = vpl.m_wsNormal.xyz;
		float3 lightPosition = vpl.m_wsPosition.xyz;

		// now calculate lighting from this vpl
		float3 lightDir = lightPosition - worldSpacePos;
		float falloffFactor = 1.f / (1.f + dot(lightDir, lightDir));

		lightDir = normalize(lightDir);
		float3 halfVector = normalize(lightDir + worldViewVector);
		float fNdotH = saturate(dot(halfVector, setup.normalWS));
		float fLightSolidAngelFallOff = saturate(dot(lightDir, lightNormal));
		float fNdotL = saturate(dot(lightDir, setup.normalWS));

		float D = alphaDX / max(1.0 + (alphaDXSq - 1.0) * fNdotH * fNdotH, kEpsilon);
		D *= D;

		float3 currDiffuse = lightColor * falloffFactor * fNdotL * fLightSolidAngelFallOff;

		diffuseLighting += currDiffuse;
		specularLighting += D * currDiffuse;
	}
	
	float invCount = vplCount > 0 ? 1.f / vplCount : 0.f;
	if(bIsCharacter)
	{
		// Specular model is too different for characters
		specularLighting = 0.f;
		// We apply the base color in the subsurface/deferred pass.  (Possible because we don't do specular.)
		brdfParameters.baseColor = float3(1.f, 1.f, 1.f);
	}

	float4 finalLighting = float4((diffuseLighting * (1.f - F0) * lerp(brdfParameters.baseColor, float3(0.f, 0.f, 0.f), brdfParameters.metallic) + specularLighting * F0 * srt.pConsts->m_specMultiplier) * invCount * invPdfNormFactor * srt.pConsts->m_camPosIntensity.w, 1.f);
	srt.pTextures->rwtBouncedLighting[screenLocation] = finalLighting;
}

[numthreads(8, 8, 1)]
void CS_ComputeBouncedLighting(uint3 groupId : SV_GroupID, 
							   uint3 groupThreadId : SV_GroupThreadId,
                               ComputeBouncedLightingSrt srt : S_SRT_DATA)
{
	ComputeBouncedLighting(groupId, groupThreadId, srt);
}

[numthreads(6, 8, 1)]
void CS_ComputeBouncedLighting6x8(uint3 groupId : SV_GroupID, 
							   uint3 groupThreadId : SV_GroupThreadId,
                               ComputeBouncedLightingSrt srt : S_SRT_DATA)
{
	ComputeBouncedLighting(groupId, groupThreadId, srt);
}

[numthreads(30, 1, 1)]
void CS_ComputeBouncedLighting30x1(uint3 groupId : SV_GroupID, 
							   uint3 groupThreadId : SV_GroupThreadId,
                               ComputeBouncedLightingSrt srt : S_SRT_DATA)
{
	ComputeBouncedLighting(groupId, groupThreadId, srt);
}

// =====================================================================================================================

struct ComputeBouncedLightingDownsampledTextures
{
	Texture2D<float4>   tDepthBuffer;
	Texture2D<uint4>	tGBuffer0;
	Texture2D<uint4>	tGBuffer1;
	RWTexture2D<float4>	rwtBouncedLighting;
	SamplerState		samplerPoint;
};

struct ComputeBouncedLightingDownsampledBuffers
{
	StructuredBuffer<VirtualPointLight> bVpls;
	StructuredBuffer<VplCount> bVplCount;
};

struct ComputeBouncedLightingDownsampledConst
{
	float4 m_camPosIntensity;	//x, y, z are camera position, w intensity scaler
	float4 m_screenToViewParams;
	float4 m_bufferSizeParams;	// 1.f / width, 1.f / height, 1.f / (kDownsampleScale * kDownsampleScale), unused
	float4 m_viewToWorldMat[3];
	float4 m_uvOffsets[16];
};

struct ComputeBouncedLightingDownsampledSrt
{
	ComputeBouncedLightingDownsampledConst *pConsts;
	ComputeBouncedLightingDownsampledTextures *pTextures;
	ComputeBouncedLightingDownsampledBuffers *pBuffers;
};

void ComputeBouncedLightingDownsampled(uint3 dispatchThreadId, ComputeBouncedLightingDownsampledSrt srt)
{
	float viewSpaceZ = 0.f;
	float roughness = 0.f;
	float specular = 0.f;
	float3 baseColor = float3(0.f, 0.f, 0.f);
	float3 worldSpaceNormal = float3(0.f, 0.f, 0.f);
	float metallic = 0.f;

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_bufferSizeParams.xy;

	for (uint i = 0; i < 16; i++)
	{
		float2 fullSizeUv = uv + srt.pConsts->m_uvOffsets[i].xy;
		float depthBufferZ = srt.pTextures->tDepthBuffer.SampleLevel(srt.pTextures->samplerPoint, fullSizeUv, 0).x;
		viewSpaceZ += srt.pConsts->m_screenToViewParams.w / (depthBufferZ - srt.pConsts->m_screenToViewParams.z);

		uint4 gBuffer0 = srt.pTextures->tGBuffer0.SampleLevel(srt.pTextures->samplerPoint, fullSizeUv, 0);
		uint4 gBuffer1 = srt.pTextures->tGBuffer1.SampleLevel(srt.pTextures->samplerPoint, fullSizeUv, 0);
		BrdfParameters brdfParameters;
		InitBrdfParameters(brdfParameters);
		Setup setup;
		UnpackGBuffer(gBuffer0, gBuffer1, brdfParameters, setup);

		roughness += brdfParameters.roughness;
		specular += brdfParameters.specular;
		baseColor += brdfParameters.baseColor;
		metallic += brdfParameters.metallic;
		worldSpaceNormal += setup.normalWS;
	}

	viewSpaceZ *= srt.pConsts->m_bufferSizeParams.z;	// averge
	float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float4 viewSpacePos = float4(ndc * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.f);
	float3 worldSpacePos;
	worldSpacePos.x = dot(srt.pConsts->m_viewToWorldMat[0], viewSpacePos);
	worldSpacePos.y = dot(srt.pConsts->m_viewToWorldMat[1], viewSpacePos);
	worldSpacePos.z = dot(srt.pConsts->m_viewToWorldMat[2], viewSpacePos);

	// averge gbuffer values
	roughness *= srt.pConsts->m_bufferSizeParams.z;
	specular *= srt.pConsts->m_bufferSizeParams.z;
	baseColor *= srt.pConsts->m_bufferSizeParams.z;
	metallic *= srt.pConsts->m_bufferSizeParams.z;
	float averageNormalLength = length(worldSpaceNormal) * srt.pConsts->m_bufferSizeParams.z;
	// convert toksvig factor for roughness
	float oldSpec = exp2((1.f - roughness) * 10.f);
	float toksvig = averageNormalLength / lerp(oldSpec, 1.f, averageNormalLength);
	float newSpec = toksvig * oldSpec;
	roughness = 1.f - log2(newSpec) * 0.1f;
	worldSpaceNormal = normalize(worldSpaceNormal);

	float alphaDX = roughness * roughness;
	float alphaDXSq = alphaDX * alphaDX;

	float3 F0 = specular * 0.08;
	F0 = lerp(F0, baseColor, metallic);

	float3 worldViewVector = normalize(srt.pConsts->m_camPosIntensity.xyz - worldSpacePos);

	// accumulate lighting from vpls
	float3 diffuseLighting = float3(0.f, 0.f, 0.f);
	float3 specularLighting = float3(0.f, 0.f, 0.f);

	for (uint i = 0; i < srt.pBuffers->bVplCount[0].m_count; i++)
	{
		// extract vpl's properties
		float3 lightColor = srt.pBuffers->bVpls[i].m_color.rgb;
		float3 lightNormal = srt.pBuffers->bVpls[i].m_wsNormal.xyz;
		float3 lightPosition = srt.pBuffers->bVpls[i].m_wsPosition.xyz;

		// now calculate lighting from this vpl
		float3 lightDir = lightPosition - worldSpacePos;
		float falloffFactor = min(1.0, 1.0 / dot(lightDir, lightDir));

		lightDir = normalize(lightDir);
		float3 halfVector = normalize(lightDir + worldViewVector);
		float fNdotH = saturate(dot(halfVector, worldSpaceNormal));
		float fLightSolidAngelFallOff = saturate(dot(lightDir, lightNormal));
		float fNdotL = saturate(dot(lightDir, worldSpaceNormal));

		float D = alphaDX / max(1.0 + (alphaDXSq - 1.0) * fNdotH * fNdotH, kEpsilon);
		D *= D;

		diffuseLighting += lightColor * falloffFactor * fNdotL * fLightSolidAngelFallOff;
		specularLighting += D * lightColor * falloffFactor * fNdotL * fLightSolidAngelFallOff;
	}

	float4 finalLighting = float4((diffuseLighting * (1.f - F0) * lerp(baseColor, float3(0.f, 0.f, 0.f), metallic) + specularLighting * F0) * srt.pConsts->m_camPosIntensity.w, 1.f);
	srt.pTextures->rwtBouncedLighting[dispatchThreadId.xy] = finalLighting;
}

[numthreads(8, 8, 1)]
void CS_ComputeBouncedLightingDownsampled(uint3 dispatchThreadId : SV_DispatchThreadId, ComputeBouncedLightingDownsampledSrt srt : S_SRT_DATA)
{
	ComputeBouncedLightingDownsampled(dispatchThreadId, srt);
}

[numthreads(32, 2, 1)]
void CS_ComputeBouncedLightingDownsampledLeftOver(uint3 dispatchThreadId : SV_DispatchThreadId, ComputeBouncedLightingDownsampledSrt srt : S_SRT_DATA)
{
	ComputeBouncedLightingDownsampled(dispatchThreadId + uint3(0, 264, 0), srt);
}

struct MovieRenderParams
{
	int m_left;
	int m_top;
	int m_movieWidth;
	int m_movieHeight;
	int m_displayWidth;
	int m_displayHeight;
};

struct MovieRenderSrtData
{
	MovieRenderParams *pConsts;
	Texture2D<float4> tSourceColorBuffer0; //			: register(t0);
	Texture2D<float4> tSourceColorBuffer1; //			: register(t1);	
	RWTexture2D<float4> rwOutputColorBuffer; //		: register(u0);
	SamplerState sSamplerLinear;
};

[numthreads(32, 2, 1)]
void CS_MovieRender(uint3 dispatchThreadId : SV_DispatchThreadId,
                    MovieRenderSrtData *srt : S_SRT_DATA)
{
	float2 coord = (dispatchThreadId.xy * float2(srt->pConsts->m_movieWidth, srt->pConsts->m_movieHeight) / float2(srt->pConsts->m_displayWidth, srt->pConsts->m_displayHeight));
	float2 tex = coord * float2(1.0f / srt->pConsts->m_movieWidth, 1.0f / srt->pConsts->m_movieHeight) + float2(0.5f / srt->pConsts->m_movieWidth, 0.5f / srt->pConsts->m_movieHeight);
	float4 result;

	float3 ycbcr = float3(srt->tSourceColorBuffer0.SampleLevel(srt->sSamplerLinear, tex, 0).x - 0.0625f,
						  srt->tSourceColorBuffer1.SampleLevel(srt->sSamplerLinear, tex, 0).x - 0.5f,
						  srt->tSourceColorBuffer1.SampleLevel(srt->sSamplerLinear, tex, 0).y - 0.5f);

	result = float4(dot(float3(1.1644f, 0.0f, 1.7927f), ycbcr), // R
					dot(float3(1.1644f, -0.2133f, -0.5329f), ycbcr), // G
					dot(float3(1.1644f, 2.1124f, 0.0f), ycbcr), // B
					1.0f);

	srt->rwOutputColorBuffer[int2(dispatchThreadId.xy) + int2(srt->pConsts->m_left, srt->pConsts->m_top)] = saturate(result);
}

const static int2 sharpenOffsets[4] = {int2(0, -1), int2(-1, 0), int2(1, 0), int2(0, 1)};

bool ApplyBackgroundBlur(out float3 diffuseColor, float2 uv, Texture2D<float4> srcColor, float depthVal,
						   float2 linearDepthParams, float blurStartDistance,
						   float blurPower, SamplerState pointClampSampler)
{
	float linearDepth = 1.0f / ((depthVal * linearDepthParams.x) + linearDepthParams.y);	// Gives us a smoother gradient than the existing GetDepthVs() or GetLinearDepth()
	float maxLinearDepth = 1.0f / (linearDepthParams.x + linearDepthParams.y);
	
	bool shouldApplyBlur = linearDepth >= blurStartDistance;
	if (shouldApplyBlur)
	{	
		// http://dev.theomader.com/gaussian-kernel-calculator/
		float3x3 gaussianKernel33 = {
			0.077847f,	0.123317f,	0.077847f,
			0.123317f,	0.195346f,	0.123317f,
			0.077847f,	0.123317f,	0.077847f
		};

		float3 blurredColor = float3(0.0f, 0.0f, 0.0f);

		float2 texDimensions;
		srcColor.GetDimensions(texDimensions.x, texDimensions.y);
		float2 pixelDimensions = 1.0f / texDimensions;

		[loop]
		for (int i = -1; i <= 1; i++)
		{
			for (int j = -1; j <= 1; j++)
			{
				float2 sampleUv = uv + (float2(i, j) * pixelDimensions);
				float4 pixelColor = srcColor.SampleLevel(pointClampSampler, sampleUv, 0);
				blurredColor.xyz += gaussianKernel33[i][j] * pixelColor.xyz;
			}
		}

		float fractionIntoBg = saturate((linearDepth - blurStartDistance) / (maxLinearDepth - blurStartDistance));
		diffuseColor = lerp(diffuseColor, blurredColor, fractionIntoBg);
	}

	return shouldApplyBlur;
}

float3 ApplySharpen(float3 inputColor, float2 uv, bool usePresortDof,
	float sharpenWeightScale, float sharpenThreshold,
	float tonemapOffset, float tonemapScale,
	Texture2D<float4> srcColor,
	Texture2D<float3> dofPresort,
	SamplerState linearClampSampler,
	SamplerState pointClampSampler)
{
	float3 outputColor = inputColor;
	bool applySharpen = abs(sharpenWeightScale) > 0.001f;
	if (usePresortDof)
	{
		float presortInfo = dofPresort.SampleLevel(pointClampSampler, uv, 0).x;
		applySharpen = presortInfo < 0.8;
	}

	if (applySharpen)
	{
		float3 neighborContribution = inputColor * 4.f;

		float threshold = sharpenThreshold;
		if (tonemapScale > 0.0f)
			threshold = max((threshold - tonemapOffset) / tonemapScale, 0.0f);

		for (uint i = 0; i < 4; ++i)
			neighborContribution -= srcColor.SampleLevel(linearClampSampler, uv, 0, sharpenOffsets[i]).xyz;

		neighborContribution.rgb = min(abs(neighborContribution), threshold) * sign(neighborContribution);

		outputColor = max(inputColor + neighborContribution * sharpenWeightScale, 0.0f);
	}

	return outputColor;
}

float3 ApplySharpenAndBackgroundBlur(float3 inputColor, Texture2D<float4> srcColor, float2 uv,
									 bool backgroundBlurEnabled, float depthVal, float2 linearDepthParams,
									 float blurStartDistance, float blurFStop, float pixelFilmRatio,
									 bool usePresortDof, Texture2D<float3> dofPresort,
									 float sharpenWeightScale, float sharpenThreshold,
									 float tonemapOffset, float tonemapScale, 
									 SamplerState linearClampSampler, SamplerState pointClampSampler)
{
	const float maxCocRadiusPixels = 2.5f;
	const float focalLengthM = 0.07f;
	const float2 referenceResolution = float2(2560.0f, 1440.0f);

	float3 outputColor = inputColor;

	float2 texDimensions;
	srcColor.GetDimensions(texDimensions.x, texDimensions.y);
	const float resolutionFactor = referenceResolution.x / texDimensions.x;

	float linearDepth = 1.0f / ((depthVal * linearDepthParams.x) + linearDepthParams.y);	// Gives us a more linear gradient than the existing GetDepthVs() or GetLinearDepth()
	float farZ = 1.0f / ((1.0f * linearDepthParams.x) + linearDepthParams.y);

	float coc = GetCocRadius(blurFStop, linearDepth, blurStartDistance, focalLengthM);
	float cocScale = GetCocRadius(blurFStop, farZ, blurStartDistance, focalLengthM);
	coc *= coc / cocScale;	// Extend range linearly
	coc *= pixelFilmRatio * resolutionFactor;

	float cocWeight = saturate(coc * coc);	// Blur should be proportional to the area of the CoC
	cocWeight *= max(sign(linearDepth - blurStartDistance), 0.0f);	// Don't blur foreground
	cocWeight *= max(sign(1.0f - depthVal), 0.0f);					// Don't blur at max depth

	bool shouldApplySharpen = abs(sharpenWeightScale) > 0.001f;
	if (usePresortDof)
	{
		float presortInfo = dofPresort.SampleLevel(pointClampSampler, uv, 0).x;
		shouldApplySharpen = presortInfo < 0.8;
	}

	float3 sharpenNeighborContribution = inputColor * 4.f;
	float3 blurNeighborContribution = float3(0.0f, 0.0f, 0.0f);

	// For Sharpen
	float threshold = sharpenThreshold;
	if (tonemapScale > 0.0f)
		threshold = max((threshold - tonemapOffset) / tonemapScale, 0.0f);

	// For blur
	float2 pixelUvSize = (1.0f / texDimensions);

	float blurCenterWeight = 1.0f - cocWeight;
	float blurNeighborWeight = cocWeight * 0.125f;	// 1/8 for each neighbor

	for (int i = -1; i <= 1; i++)
	{
		for (int j = -1; j <= 1; j++)
		{
			if (i != 0 || j != 0)	// Only compute neighbor contributions
			{
				bool bIsBlurSample = backgroundBlurEnabled;
				bool bIsSharpenSample = shouldApplySharpen && (abs(i + j) == 1);

				float2 offsetNumPixels = normalize(float2(i, j));	// To simulate a circle
				offsetNumPixels += offsetNumPixels * maxCocRadiusPixels * cocWeight * 0.3f;	// Push sample point out a little if coc weight is high

				if (bIsBlurSample || bIsSharpenSample)
				{
					float2 sampleUv = uv + (pixelUvSize * offsetNumPixels / resolutionFactor);
					float3 sampledColor = srcColor.SampleLevel(linearClampSampler, sampleUv, 0).xyz;
					blurNeighborContribution += blurNeighborWeight * sampledColor;	// Blur uses all 8 neighbors
					if (bIsSharpenSample)	// Sharpen doesn't use diagonals
					{
						sharpenNeighborContribution -= sampledColor;
					}
				}
			}
		}
	}

	sharpenNeighborContribution.rgb = min(abs(sharpenNeighborContribution), threshold) * sign(sharpenNeighborContribution);

	float3 sharpenOutputColor = max(inputColor + sharpenNeighborContribution * sharpenWeightScale, 0.0f);
	if (shouldApplySharpen)
	{
		outputColor = sharpenOutputColor;
	}
	if (backgroundBlurEnabled)
	{
		float3 blurOutputColor = (outputColor * blurCenterWeight) + blurNeighborContribution;
		outputColor = blurOutputColor;
	}

	return outputColor;
}

float3 ApplyMotionBlur(float3 inputColor, float2 uv, uint2 xy, uint flags, 
					   float2 bufferSize, float2 depthParams, 
					   Texture2D<float> depthBuffer, 
					   Texture2D<float> dsDepthVSBuffer, 
					   Texture2D<float3> motionBlurColorInfo, 
					   Texture2D<float>	motionBlurAlphaInfo,
					   SamplerState linearClampSampler, 
					   SamplerState pointClampSampler)
{
	float3 outputColor = inputColor;
	bool bUseNewMotionBlur = (flags & 0x4000) != 0;

	if (bUseNewMotionBlur)
	{
		float4 mblurValue = float4(0.0f);
		mblurValue.w = motionBlurAlphaInfo.SampleLevel(linearClampSampler, uv, 0).r;

		if (mblurValue.w > 0.0f)
		{
			mblurValue.xyz = motionBlurColorInfo.SampleLevel(linearClampSampler, uv, 0);
		}

		outputColor = (mblurValue.xyz * mblurValue.w) + (inputColor.xyz * (1.0f - mblurValue.w));
	}
	else
	{
		if ((flags & 0x04) != 0)
		{
			float2 dsBufSize = bufferSize / 2;
			float2 halfSceenPos = (uv * dsBufSize - 0.5f);
			int2 baseCoord = int2(halfSceenPos);
			float2 lerpRatio = frac(halfSceenPos);

			float depthZ = depthBuffer.SampleLevel(pointClampSampler, uv, 0);
			float depthVS = GetDepthVS(depthZ, depthParams);

			float4 dsDepthVS;
			dsDepthVS.x = dsDepthVSBuffer[baseCoord];
			dsDepthVS.y = dsDepthVSBuffer[baseCoord + int2(1, 0)];
			dsDepthVS.z = dsDepthVSBuffer[baseCoord + int2(0, 1)];
			dsDepthVS.w = dsDepthVSBuffer[baseCoord + int2(1, 1)];
			float4 deltaDepthVS = abs(dsDepthVS - depthVS);
			float4 blendWeight = max(exp2(-deltaDepthVS * 10.0f), 0.00001f) * float4((1.0f - lerpRatio.x) * (1.0f - lerpRatio.y), lerpRatio.x * (1.0f - lerpRatio.y), (1.0f - lerpRatio.x) * lerpRatio.y, lerpRatio.x * lerpRatio.y);

			float maxDepthVSRatio = max(max3(deltaDepthVS.x, deltaDepthVS.y, deltaDepthVS.z), deltaDepthVS.w) / depthVS;

			float4 mblurValue;
			if (maxDepthVSRatio < 0.008f)
			{
				mblurValue.xyz = motionBlurColorInfo.SampleLevel(linearClampSampler, uv, 0);
				mblurValue.w = motionBlurAlphaInfo.SampleLevel(linearClampSampler, uv, 0);
				outputColor = mblurValue.xyz + inputColor.xyz * mblurValue.w;
			}
			else
			{
				float2 baseUv = (baseCoord + 0.5f) / dsBufSize;
				float4 srcMblurColor0, srcMblurColor1, srcMblurColor2, srcMblurColor3;
				srcMblurColor0.xyz = motionBlurColorInfo.SampleLevel(pointClampSampler, baseUv, 0.f);
				srcMblurColor0.w = motionBlurAlphaInfo.SampleLevel(pointClampSampler, baseUv, 0.f);
				srcMblurColor1.xyz = motionBlurColorInfo.SampleLevel(pointClampSampler, baseUv, 0.f, int2(1, 0));
				srcMblurColor1.w = motionBlurAlphaInfo.SampleLevel(pointClampSampler, baseUv, 0.f, int2(1, 0));
				srcMblurColor2.xyz = motionBlurColorInfo.SampleLevel(pointClampSampler, baseUv, 0.f, int2(0, 1));
				srcMblurColor2.w = motionBlurAlphaInfo.SampleLevel(pointClampSampler, baseUv, 0.f, int2(0, 1));
				srcMblurColor3.xyz = motionBlurColorInfo.SampleLevel(pointClampSampler, baseUv, 0.f, int2(1, 1));
				srcMblurColor3.w = motionBlurAlphaInfo.SampleLevel(pointClampSampler, baseUv, 0.f, int2(1, 1));

				mblurValue = (srcMblurColor0 * blendWeight.x + srcMblurColor1 * blendWeight.y + srcMblurColor2 * blendWeight.z + srcMblurColor3 * blendWeight.w) / dot(blendWeight, 1.0f);
				outputColor = mblurValue.xyz + inputColor.xyz * mblurValue.w;
			}
		}
		else
		{
			float4 mblurValue;
			mblurValue.xyz = motionBlurColorInfo.SampleLevel(linearClampSampler, uv, 0);
			mblurValue.w = motionBlurAlphaInfo.SampleLevel(linearClampSampler, uv, 0);
			outputColor = mblurValue.xyz + inputColor.xyz * mblurValue.w;
		}
	}

	return outputColor;
}

float3 ApplySaturation(float3 diffuseColor, float saturation, float desaturateDarksThresholdInv, float desaturateDarksAmount)
{
		float luminance = Luminance(diffuseColor);
		desaturateDarksAmount = max(desaturateDarksAmount, saturation);
		float saturationFactor = (desaturateDarksThresholdInv > 0) ? lerp(saturation, desaturateDarksAmount, Pow2(saturate(1.0 - luminance * desaturateDarksThresholdInv))) : saturation;
		diffuseColor = lerp(diffuseColor, luminance.xxx, saturationFactor);
		return diffuseColor;
}

float3 ApplyBonusLut(float3 iptColor, float bonusLutBlend, float4 lutParams,
					 Texture3D<float4> bonusLutTexture, SamplerState linearClampSampler)
{
	float3 outputColor = iptColor;
	if (bonusLutBlend > 0.0)
	{
		float3 lutIndex = saturate(outputColor) * lutParams.y + lutParams.z;
		float3 bonusLutColor = bonusLutTexture.SampleLevel(linearClampSampler, lutIndex, 0).xyz;
		outputColor = lerp(outputColor, bonusLutColor, bonusLutBlend);
	}
	return outputColor;
}

float3 ApplyLutAndHdr(uint hdrMode, bool bEnableLut,
					  float3 iptColor, float3 hdrColor, 
					  int2 dispatchThreadId, float4 lutParams, 
					  float3 hdrTint, 
					  float hdrDesaturate, float hdrLdrBlendPower, 
					  float hdrBlendAmount,
					  float hdrSplitCoord, float bonusLutBlend,
					  float lutPreTonemap,
					  float listenModePriority,
					  Texture3D<float4> lutTexture, 
					  Texture3D<float4> bonusLutTexture,
					  SamplerState linearClampSampler)
{
	float3 outputColor = iptColor;
	if (hdrMode != kHdrOnHdr)
	{
		// LDR Lut input is sRGB range [0,1]
		outputColor = iptColor < 0.0031308f ? 
					 iptColor * 12.92 : 
					 pow(iptColor, 1.0/2.4) * 1.055 - 0.055f;

		if (bEnableLut)
		{
			// Apply pre-tonemapping
			outputColor *= lutPreTonemap;
			outputColor = saturate(outputColor) + max(pow(outputColor, 0.75) - 1.0, 0.0);
			outputColor /= lutPreTonemap;
			
			// LUT is implicitly HDR.  Input is sRGB range [0,2]
			outputColor = saturate(outputColor/2.0);
			
			// LUT output is sRGB range [0,1]
			float3 lutIndex = outputColor * lutParams.y + lutParams.z;
			outputColor = lutTexture.SampleLevel(linearClampSampler, lutIndex, 0).xyz;
		}
	}

	if (bonusLutBlend > 0.0)
	{
		float3 lutIndex = saturate(outputColor) * lutParams.y + lutParams.z;
		float3 bonusLutColor = bonusLutTexture.SampleLevel(linearClampSampler, lutIndex, 0).xyz;
		outputColor = lerp(outputColor, bonusLutColor, bonusLutBlend);
	}

	bool ldrHdrBlendMode = hdrMode == kLdrHdrBlendOnHdr || hdrMode == kHdrSplitTestMode;

	if (hdrMode == kHdrOnHdr || ldrHdrBlendMode)
	{
		hdrColor = iptColor < 0.0031308f ? 
				 iptColor * 12.92 : 
				 pow(iptColor, 1.0/2.4) * 1.055 - 0.055f;

		float desatColor = dot(hdrColor, s_luminanceVector);
		hdrColor = lerp(hdrColor, desatColor, hdrDesaturate);
		hdrColor *= hdrTint;
	}

	if (hdrMode == kHdrOnHdr)
	{
		outputColor = hdrColor;
	}
	else if (ldrHdrBlendMode)
	{
		float blend = max(outputColor.r, max(outputColor.g, outputColor.b));
		blend = pow(blend, hdrLdrBlendPower) * hdrBlendAmount;
		blend = blend * (1.0f - (1.0f / 0.4f * listenModePriority)); // In listenMode, HDR Blend amount should be 0
		if(hdrMode == kHdrSplitTestMode)	// Fade from LDR to HDR
		{
			float wipe = saturate((dispatchThreadId.x + dispatchThreadId.y * 0.25 - hdrSplitCoord)/25.0);
			blend *= wipe;
		}
		outputColor = lerp(outputColor, hdrColor, saturate(blend));
	}

	return outputColor;
}

struct PrePostProcessingSharedData
{
	RWTexture2D<float4>		m_dstColor;
	Texture2D<float4>		m_srcColor;
	Texture2D<float3>		m_motionBlurColorInfo;
	Texture2D<float>		m_motionBlurAlphaInfo;
	Texture2D<float>		m_depthBuffer;
	Texture2D<float>		m_temporalDepth;
	Texture2D<float>		m_dsDepthVS;
	Texture2D<float4>		m_bloomBuffer;
	Texture3D<float4>		m_lutTexture;
	Texture3D<float4>		m_bonusLutTexture;
	Texture2D<float3>		m_dofPresort;
	ByteAddressBuffer		m_exposureControlBuffer;

	SamplerState			m_pointClampSampler;
	SamplerState			m_linearClampSampler;
	SamplerState			m_linearSampler;

	PostProcessingConst		m_consts;
};

struct PrePostProcessingSrt
{
	PrePostProcessingSharedData*		m_pData;
	int2								m_screenOffset;
};

float3 ApplyLutTonemapHdr(float3 inputColor, float2 uv0, int2 dispatchThreadId, PrePostProcessingSrt srt)
{
	// HDR modes:
	// 0: LDR image with TV in LDR mode
	// 1: LDR image with TV in HDR mode (should look same as 0)
	// 2: HDR image with TV in HDR mode
	// 4: LDR/HDR blend with TV in HDR mode. LDR values are tonemapped, HDR values are left alone
	uint hdrMode = srt.m_pData->m_consts.m_hdrMode;
	
	float tonemapScale = asfloat(srt.m_pData->m_exposureControlBuffer.Load(0));
	float tonemapOffset = srt.m_pData->m_consts.m_tonemapControlParameter.z;

	bool bEnableTonemap = (srt.m_pData->m_consts.m_postMode & 0x04) != 0;
	bool bEnableLut = (srt.m_pData->m_consts.m_lutParams.x) > 0.5f;

	float3 exposedColor = inputColor;
	float3 hdrColor = inputColor;

	if (bEnableTonemap)
	{
		exposedColor = max(exposedColor * tonemapScale + tonemapOffset, 0);

		hdrColor = exposedColor;

		// apply filmic tone-mapping.
		if (!bEnableLut && hdrMode != kHdrOnHdr)
		{
			exposedColor = ApplyTonemapping(srt.m_pData->m_consts.m_filmicTonemapParameterCmp0,
											srt.m_pData->m_consts.m_filmicTonemapParameterCmp1, 
											srt.m_pData->m_consts.m_filmicTonemapParameter0.x, 
											uv0, exposedColor);
		}
	}
	
	float3 finalColor = ApplyLutAndHdr(hdrMode, bEnableLut,
									   exposedColor, hdrColor, dispatchThreadId, 
									   srt.m_pData->m_consts.m_lutParams, 
									   srt.m_pData->m_consts.m_hdrColorControls.rgb, 
									   srt.m_pData->m_consts.m_hdrColorControls.a, 
									   srt.m_pData->m_consts.m_hdrLdrBlendPower, 
									   srt.m_pData->m_consts.m_hdrBlendAmount, 
									   srt.m_pData->m_consts.m_hdrSplitCoord,
									   srt.m_pData->m_consts.m_bonusLutBlend,
									   srt.m_pData->m_consts.m_lutPreTonemap,
									   srt.m_pData->m_consts.m_listenModePriority,
									   srt.m_pData->m_lutTexture,
									   srt.m_pData->m_bonusLutTexture, 
									   srt.m_pData->m_linearClampSampler);

	return finalColor;
}

// Sonar mode
float3	ApplySonarMode(float3 color, float3 positionWS, float4 sonar, float maxSonarRadius, float3 colorTint = float3( 1.0f , 1.0f , 1.0f ) ) {
	// distance between the world position and the implicit sphere
	float3 finalColor = color;

	const float rcp_distance_threshold = 2.0f;
	const float thickness1 = 5.f;
	const float thickness2 = 10.f;
	float delta = saturate((sonar.w - length(sonar.xyz - positionWS.xyz)) * rcp_distance_threshold) ;
	float delta2 = saturate((sonar.w - length(sonar.xyz - positionWS.xyz)) * rcp_distance_threshold * thickness1);
	float outline = saturate((delta2 - delta)) * 10.0f;

	delta = saturate((sonar.w - length(sonar.xyz - positionWS.xyz)) * rcp_distance_threshold);
	delta2 = saturate(( sonar.w - 20.f- length(sonar.xyz - positionWS.xyz)) * rcp_distance_threshold * thickness2);
	float fill = saturate(delta - delta2) * 1.5f;

	float maskIn = saturate(max(0, sonar.w * 2.f - 2.f) / maxSonarRadius);
	float maskOut = saturate(pow(1.0 - (sonar.w / maxSonarRadius), 6) * 2.f);
	finalColor = color + max(outline, fill) * maskIn * maskOut * colorTint;

	return finalColor;
}

float3 GetPositionWS(uint2 screenCoord, float depthZ, float2 rcpScreenSize , float4x4 mat , float3 cameraPos)
{
	// Go from screen coordinates to NDC
	float2 ndc = ((float2)screenCoord + float2(0.5f, 0.5f)) * rcpScreenSize;
	ndc.x = ndc.x * 2.f - 1.f;
	ndc.y = (1 - ndc.y) * 2.f - 1.f;

	float4 positionAS = mul(mat, float4(ndc, depthZ, 1.f));
	positionAS.xyz /= positionAS.w;
	return positionAS.xyz + cameraPos;
}

groupshared float g_dsDepthVs[6][6];
[numthreads(8, 8, 1)]
void CS_PrePostProcessing(int3 localDispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, PrePostProcessingSrt srt : S_SRT_DATA) : SV_Target
{
	int2 dispatchThreadId = localDispatchThreadId.xy + srt.m_screenOffset;

	bool bRegularMode			= (srt.m_pData->m_consts.m_postMode & 0x07)		== 0x07;
	bool bEnableBloom			= ((srt.m_pData->m_consts.m_postMode & 0x01) != 0) && (srt.m_pData->m_consts.m_postMinScaleBiasExp.x > 0.0 || srt.m_pData->m_consts.m_preMinScaleBiasExp.x > 0.0);
	bool bEnableVignette		= (srt.m_pData->m_consts.m_vignetteParams0.x)	>  0.5f;
	bool bIsMp					= (srt.m_pData->m_consts.m_flags & 0x08)		!= 0;
	bool bEnableMotionBlur		= (srt.m_pData->m_consts.m_postMode & 0x08)		!= 0;
	bool bEnableGrad			= srt.m_pData->m_consts.m_graduatedFilterDensity > 0;
	bool bEnableBackgroundBlur	= srt.m_pData->m_consts.m_backgroundBlurEnabled > 0.0f;

	float2 uv0 = (dispatchThreadId.xy + float2(0.5f, 0.5f)) * srt.m_pData->m_consts.m_pixelSize.xy;

	float3 diffuseColor = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0, 0).xyz;
	float temporalDepth = srt.m_pData->m_temporalDepth.SampleLevel(srt.m_pData->m_pointClampSampler, uv0, 0);

	float tonemapScale = asfloat(srt.m_pData->m_exposureControlBuffer.Load(0));
	float tonemapOffset = srt.m_pData->m_consts.m_tonemapControlParameter.z;
	
	diffuseColor = ApplySharpenAndBackgroundBlur(diffuseColor, srt.m_pData->m_srcColor, uv0,
												 bEnableBackgroundBlur, temporalDepth, srt.m_pData->m_consts.m_linearDepthParams,
												 srt.m_pData->m_consts.m_backgroundBlurStartDistance, srt.m_pData->m_consts.m_backgroundBlurFStop, srt.m_pData->m_consts.m_pixelFilmRatio,
												 srt.m_pData->m_consts.m_usePresortDof, srt.m_pData->m_dofPresort,
												 srt.m_pData->m_consts.m_weightScale, srt.m_pData->m_consts.m_threshold,
												 tonemapOffset, tonemapScale,
												 srt.m_pData->m_linearClampSampler, srt.m_pData->m_pointClampSampler);

	float testDepthVS = 0.0f;
	
	if (bEnableMotionBlur)
	{
		diffuseColor = ApplyMotionBlur(diffuseColor, uv0, dispatchThreadId.xy,
									   srt.m_pData->m_consts.m_flags, 
									   srt.m_pData->m_consts.m_bufferSize.xy, 
									   srt.m_pData->m_consts.m_depthParams, 
									   srt.m_pData->m_depthBuffer, 
									   srt.m_pData->m_dsDepthVS, 
									   srt.m_pData->m_motionBlurColorInfo, 
									   srt.m_pData->m_motionBlurAlphaInfo, 
									   srt.m_pData->m_linearClampSampler, 
									   srt.m_pData->m_pointClampSampler);
	}
	
	if (bEnableBloom)
	{
		// bloom. screen blur for bloom.
		float3 bloomContribution = 1.0f - saturate(diffuseColor / max(srt.m_pData->m_consts.m_bloomLendParams.x, 1.0f));
		float4 bloomLayer = srt.m_pData->m_bloomBuffer.SampleLevel(srt.m_pData->m_linearClampSampler, uv0, 0);
		diffuseColor += bloomContribution * bloomLayer.xyz;
	}
	
	if (bRegularMode)
	{
		if (bEnableVignette)
		{
			diffuseColor = ApplyVignette(srt.m_pData->m_consts, uv0, diffuseColor).rgb;
		}
	
		diffuseColor = ApplySaturation(diffuseColor, srt.m_pData->m_consts.m_tonemapControlParameter.w, srt.m_pData->m_consts.m_tonemapControlParameter.y, srt.m_pData->m_consts.m_tonemapControlParameter.x);
	}
	
	if (bEnableGrad)
	{
		float4 basePositionVS = float4((dispatchThreadId.xy * srt.m_pData->m_consts.m_screenScaleOffset.xy + srt.m_pData->m_consts.m_screenScaleOffset.zw), 1.0f, 0.0f);
		float3 viewWS;
		viewWS.x = dot(srt.m_pData->m_consts.m_viewToWorld[0], basePositionVS);
		viewWS.y = dot(srt.m_pData->m_consts.m_viewToWorld[1], basePositionVS);
		viewWS.z = dot(srt.m_pData->m_consts.m_viewToWorld[2], basePositionVS);
		viewWS = normalize(viewWS);
	
		float viewY = viewWS.y * srt.m_pData->m_consts.m_graduatedFilterFlip;
		viewY = (viewY - srt.m_pData->m_consts.m_graduatedFilterOffset) * srt.m_pData->m_consts.m_graduatedFilterScale;
	
		float grad = saturate(1.0 - viewY);
	
		grad = pow(grad, srt.m_pData->m_consts.m_graduatedFilterFalloff);
		grad = lerp(1.0, grad, srt.m_pData->m_consts.m_graduatedFilterDensity);
	
		diffuseColor *= grad;
	}
	
	float3 finalColor = ApplyLutTonemapHdr(diffuseColor, uv0, dispatchThreadId, srt);
	
	// Sonar detection
	const float sonarRadius = srt.m_pData->m_consts.m_sonarParam.w;
	const float sonarMaxRaius = srt.m_pData->m_consts.m_sonarMaxRadius;
	if (sonarRadius > 0.0f) {
		const float depthZ = srt.m_pData->m_depthBuffer.SampleLevel(srt.m_pData->m_pointClampSampler, uv0, 0);
		float3 positionWS = GetPositionWS(dispatchThreadId.xy, depthZ, srt.m_pData->m_consts.m_pixelSize.xy, srt.m_pData->m_consts.m_screenToAltWorldMat, srt.m_pData->m_consts.m_cameraPositionWS.xyz );
		finalColor = ApplySonarMode(finalColor, positionWS, srt.m_pData->m_consts.m_sonarParam, sonarMaxRaius);
	}
	
	// Give prioity to the listen mode highlight by reducing high intensity values.
	if (srt.m_pData->m_consts.m_listenModePriority > 0.0f)
	{
		// Determine how much the intensity should decrease. The listen mode priority is in the range [0,1] and
		// controls how much of the final [0,1] output range should be dedicated to listen mode. For example, a
		// prioity of 0.2 will reserve the range [0.8, 1.0] for listen mode.
		float p = srt.m_pData->m_consts.m_listenModePriority;
		float i = saturate(dot(finalColor, s_luminanceVector));
		float d = saturate(p * pow(i, 1.0f / p));
	
		// Determine how much we should reduce the final color by to produce a decrease in luminance equal to d
		// (i.e. find scale k such that luminance(finalColor * k) is eqaul to luminance(finalColor) - d).
		float k = 1.0f - (d / i);
		finalColor = max(float3(0.0f, 0.0f, 0.0f), finalColor * k);
	}
	
	finalColor = min(finalColor, 100.0);
	
	srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)] = float4(finalColor, 0.0f);
}

[numthreads(8, 8, 1)]
void CS_FakePrePostProcessing(int3 localDispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, PrePostProcessingSrt srt : S_SRT_DATA) : SV_Target
{
	int2 dispatchThreadId = localDispatchThreadId.xy + srt.m_screenOffset;
	srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)] = srt.m_pData->m_srcColor[int2(dispatchThreadId.xy)];
}

[numthreads(8, 8, 1)]
void CS_PrePostProcessingDebug(int3 localDispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, PrePostProcessingSrt srt : S_SRT_DATA) : SV_Target
{
	int2 dispatchThreadId = localDispatchThreadId.xy + srt.m_screenOffset;

	float2 uv0 = (dispatchThreadId.xy + float2(0.5f, 0.5f)) * srt.m_pData->m_consts.m_pixelSize.xy;

	float3 diffuseColor = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0, 0).xyz;

	bool bHeatMapMode = srt.m_pData->m_consts.m_flags & 0x1000;
	bool bColorBarsMode	= srt.m_pData->m_consts.m_flags & 0x2000;

	float3 finalColor = diffuseColor;

	// Used for debugging alpha overdraw
	if(bHeatMapMode)
	{
		float numLayers = diffuseColor.r;
		float shading = diffuseColor.g * 0.1 + 0.9;
		if (numLayers > 0.1)
			finalColor = GetHeatmapColor(numLayers*0.5) * shading;
		else
			finalColor = float3(0,0,0.5) * shading;
	}

	// Generate color bars 
	if(bColorBarsMode)
	{
		float x = saturate(uv0.x / 0.9);
		float hue = frac(x * 4.0);
		float saturation = floor(x * 4.0) / 3.0;

		// Some specific hardcoded colors
		if(uv0.x >= 0.9)
		{
			if(uv0.x > 0.95)
			{
				hue = 0.54;
				saturation = 0.33;
			}
			else
			{
				hue = 0.088;
				saturation = 0.26;
			}
		}

		finalColor = HSLtoRGB(float3(hue, saturation, 0.5));

		float value = (1.0 - uv0.y);
		// Quantize gradient on the left
		if(x < 0.125)
			value = ceil(value * 20.0) / 20.0;

		finalColor *= value * 4.0;

		// Draw black line in the middle
		if(abs(uv0.y - 0.5) < 0.001)
			finalColor = float3(0,0,0);

		if(x < 1.0 && frac(x * 4.0) > 0.999)
			finalColor = float3(0,0,0);

		finalColor = finalColor <= 0.04045 ? 
					 finalColor / 12.92 : 
					 pow((finalColor + 0.055)/(1.055), 2.4);

		finalColor = ApplySaturation(finalColor, srt.m_pData->m_consts.m_tonemapControlParameter.w, srt.m_pData->m_consts.m_tonemapControlParameter.y, srt.m_pData->m_consts.m_tonemapControlParameter.x);
		finalColor = ApplyLutTonemapHdr(finalColor, uv0, dispatchThreadId, srt);
		finalColor = min(finalColor, 100.0);
	}

	srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)] = float4(finalColor, 0.0f);
}

struct PostPostProcessingSharedData
{
	RWTexture2D<float4>		m_dstColor;
	Texture2D<float4>		m_srcColor;
	Texture2D<float>		m_depthBuffer;
	Texture2D<float4>		m_distortionVector;
	Texture2D<float3>		m_filmGrainTexture;
	Texture2D<uint>			m_stencilTexture;
	Texture2D<uint4>		m_gBuffer0;
	Texture2D<uint4>		m_gBuffer1;
	Texture2D<float3>		m_movieTexture;
	Texture2D<float3>		m_highlightsColor;
	Texture2D<float>		m_highlightsDepth;
	Texture2D<float4>		m_listenModeHidden;
	Texture2D<float4>		m_listenModeVisible;
	Texture2D<float>		m_listenModeDepth;
	Texture2D<float3>		m_colorChecker;
	Texture2D<float4>		m_ssao;
	Texture2D<float4>		m_bonusAsciiTexture;
	Texture2D<float4>		m_bonusPerlinNoiseTexture;
	Texture3D<float4>		m_bonusLutTexture;

	SamplerState			m_pointClampSampler;
	SamplerState			m_pointSampler;
	SamplerState			m_linearClampSampler;	
	SamplerState			m_linearSampler;

	PostProcessingConst		m_consts;
};

struct PostPostProcessingSrt
{
	PostPostProcessingSharedData*		m_pData;
	int2								m_screenOffset;
};

// Keep this in sync with the bonus-distort-mode enum in render-settings-defines.dcx
enum BonusDistortMode
{
	kBonusDistortModeNone,

	kBonusDistortModeThreeColor,
	kBonusDistortModeAscii,
	kBonusDistortMode8Bit,
	kBonusDistortModeWatercolor,
	kBonusDistortModeVoid,
	kBonusDistortModePopPoster,
	kBonusDistortModeAnimatedNoir,
	kBonusDistortModePrint,

	kBonusDistortModeCount,
};

float2 BonusDistortCoord(float2 texCoord, float2 pixelSize, float2 bufferSize, BonusShaderConst bonus, float3 baseColor, PostPostProcessingSrt srt, out bool trimBorder)
{
	const float2  kCenter = float2(0.5f, 0.5f);
	float2 retval = texCoord;

	// index > 10 == work in progress
	trimBorder = true;

	switch (bonus.m_mode)
	{
	case kBonusDistortMode8Bit:
		{
			float scale = 1.0f;

			if (bonus.m_mode == kBonusDistortModeAscii)
			{
				scale = pixelSize.x * 8.0f;
			}
			else if (bonus.m_mode == kBonusDistortMode8Bit)
			{
				scale = 0.006f;
			}

			retval = float2(-1.0f, -1.0f);

			float2	cellSize = float2(scale, scale * pixelSize.y / pixelSize.x);
			float2	cell = floor(texCoord / cellSize);

			retval = (cell + kCenter) * cellSize;
		}
		break;
	case kBonusDistortModeWatercolor:
		{
			float time = srt.m_pData->m_consts.m_bonus.m_time;
			time = floor(time * 10.0f + 0.5f) / 10.0f;
			retval += (srt.m_pData->m_bonusPerlinNoiseTexture.SampleLevel(srt.m_pData->m_linearSampler, retval * 2.0f * float2(srt.m_pData->m_consts.m_aspect, 1.0f) + float2(time), 0).rg - float2(0.3f, 0.15f)) * 0.0025f;
		}
		break;
	}


	return retval;
}

void PostPostProcessing(int3 localDispatchThreadId, PostPostProcessingSrt srt, bool enableBonus)
{
	int2 dispatchThreadId = localDispatchThreadId.xy + srt.m_screenOffset;

	const float2 pixelSize = srt.m_pData->m_consts.m_pixelSize;
	const float2 bufferSize = srt.m_pData->m_consts.m_bufferSize;

	float2 texCoord = (dispatchThreadId.xy + 0.5f) * pixelSize;
	texCoord.x = texCoord.x * srt.m_pData->m_consts.m_posScale + srt.m_pData->m_consts.m_posOffset;
	float2 btexCoord = texCoord;
	bool bRegularMode = (srt.m_pData->m_consts.m_postMode & 0x07) == 0x07;

	bool bEnableHighlight = (srt.m_pData->m_consts.m_flags & 0x01) != 0;
	bool bEnableListenMode = (srt.m_pData->m_consts.m_flags & 0x02) != 0;
	bool bEnableScopeEffect = srt.m_pData->m_consts.m_scopePostProcessBlend > 0.0f;
	bool bEnableHealthEffect = srt.m_pData->m_consts.m_healthParam > 0;
	bool bEnableVignette = (srt.m_pData->m_consts.m_vignetteParams0.x) > 0.5f;
	bool bEnableVignetteOffset = bEnableVignette && srt.m_pData->m_consts.m_vignetteTintOffset.w != 0.0f;
	bool bEnableChromaticAberration = srt.m_pData->m_consts.m_chromaticAberrationAmount != 0 || bEnableScopeEffect;
	bool bMovieCrossFadeBlend = srt.m_pData->m_consts.m_movieDissolveBlend > 0.0f;
	bool bMovieWipeBlend = srt.m_pData->m_consts.m_movieWipeBlend > 0.0f;
	bool bChapterTitleBlend = srt.m_pData->m_consts.m_chapterTitleMovieWidth > 0;
	bool bEnablePainEffect = srt.m_pData->m_consts.m_painEffectControl > 0.0f;

	// Disable non-photo bonus filters when movies are running.
	enableBonus = enableBonus && !((bMovieCrossFadeBlend || bMovieWipeBlend) && !srt.m_pData->m_consts.m_bonus.m_isPhotoFilter);

	texCoord += srt.m_pData->m_distortionVector.SampleLevel(srt.m_pData->m_linearClampSampler, texCoord, 0).xy * pixelSize * float2(1.5f, 1.5f); //need to scale by 1.5 to account for resolution difference moving from ps3 to ps4

	bool blackPixel = false;
	if (enableBonus)
	{
		float3 baseColor = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, texCoord, 0).xyz;

		bool trimBorder = false;
		texCoord = BonusDistortCoord(texCoord, pixelSize, bufferSize, srt.m_pData->m_consts.m_bonus, baseColor, srt, trimBorder);
		if (srt.m_pData->m_consts.m_bonus.m_mode == kBonusDistortModeWatercolor)
		{
			float2 margin = 0.002f * float2(1.0f, srt.m_pData->m_consts.m_aspect);
			blackPixel = trimBorder && (texCoord.x < margin.x || texCoord.x > 1.0f - margin.x || texCoord.y < margin.y || texCoord.y > 1.0f - margin.y);
		}
		else
			blackPixel = trimBorder && (texCoord.x < 0.0f || texCoord.x > 1.0f || texCoord.y < 0.0f || texCoord.y > 1.0f);
	}

	float fishEyeDistortionStrength = srt.m_pData->m_consts.m_fishEyeDistortionStrength;
	float fishEyeDistortionZoom = srt.m_pData->m_consts.m_fishEyeDistortionZoom;
	float chromaticAberrationAmount = srt.m_pData->m_consts.m_chromaticAberrationAmount;
	bool bIsInScope = false;

	// scope zoom in effect
	if (bEnableScopeEffect)
	{
		float2 ssDisp = srt.m_pData->m_consts.m_scopeSSCenter - texCoord;
		ssDisp *= float2(srt.m_pData->m_consts.m_aspect, 1.0f);
		float ssDist = length(ssDisp);
		if (ssDist < srt.m_pData->m_consts.m_scopeSSRadius)
		{
			bIsInScope = true;
			float distortionFactor = ssDist / srt.m_pData->m_consts.m_scopeSSRadius;
			float centerDist = length((texCoord - float2(0.5)) * float2(srt.m_pData->m_consts.m_aspect, 1.0f)) / srt.m_pData->m_consts.m_scopeSSRadius;
			fishEyeDistortionStrength = lerp(fishEyeDistortionStrength, 1.5f * Pow5(saturate(centerDist)), srt.m_pData->m_consts.m_scopePostProcessBlend);
			fishEyeDistortionZoom = lerp(fishEyeDistortionZoom, 0.92f, srt.m_pData->m_consts.m_scopePostProcessBlend);
			chromaticAberrationAmount = lerp(chromaticAberrationAmount, 0.0225f * distortionFactor, srt.m_pData->m_consts.m_scopePostProcessBlend);
		}
		else
		{
			fishEyeDistortionStrength = -0.035f;
			fishEyeDistortionZoom = 1.0f;
		}
	}

	float2 uv0 = GetDistortionOffset(texCoord, fishEyeDistortionStrength, fishEyeDistortionZoom);
	
	float3 finalColor = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0, 0).xyz;
	
	if (enableBonus && srt.m_pData->m_consts.m_bonus.m_mode == kBonusDistortMode8Bit)
	{
		float2 uvOffset = 2.0f / bufferSize;

		finalColor += srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0 + uvOffset * float2( 1.0f,  0.0f), 0).xyz;
		finalColor += srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0 + uvOffset * float2( 1.0f,  1.0f), 0).xyz;
		finalColor += srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0 + uvOffset * float2( 1.0f, -1.0f), 0).xyz;
		finalColor += srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0 + uvOffset * float2( 0.0f,  1.0f), 0).xyz;
		finalColor += srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0 + uvOffset * float2( 0.0f, -1.0f), 0).xyz;
		finalColor += srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0 + uvOffset * float2(-1.0f,  0.0f), 0).xyz;
		finalColor += srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0 + uvOffset * float2(-1.0f,  1.0f), 0).xyz;
		finalColor += srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0 + uvOffset * float2(-1.0f, -1.0f), 0).xyz;

		finalColor /= 9.0f;

		float depth = srt.m_pData->m_depthBuffer.SampleLevel(srt.m_pData->m_pointClampSampler, uv0, 0);

		finalColor = sqrt(finalColor);
		finalColor = RGBtoHSL(finalColor);
		if (depth < 1.0f)
		{
			finalColor.g = saturate(1.3f * floor(finalColor.g * 12.0f + 0.5f) / 12.0f);
			finalColor.b = floor(finalColor.b * 16.0f + 0.5f) / 16.0f;
		}
		else // Sky
		{
			finalColor.g = saturate(1.3f * floor(finalColor.g * 24.0f + 0.5f) / 24.0f);
			finalColor.b = floor(finalColor.b * 100.0f + 0.5f) / 100.0f;	// hi Carlos
		}
		finalColor = HSLtoRGB(finalColor);
		finalColor *= finalColor;

		finalColor = saturate(finalColor);

		bEnableChromaticAberration = false;
	}

	float finalColorLuminance = Luminance(finalColor);
	if (bEnableScopeEffect)
	{
		chromaticAberrationAmount *= saturate(finalColorLuminance * 100.0);
	}

	uint stencil = srt.m_pData->m_stencilTexture.SampleLevel(srt.m_pData->m_pointClampSampler, uv0, 0);
	bool isHero = stencil & 0x40;

	if (bEnableChromaticAberration)
	{
		float2 uv1 = GetDistortionOffset(texCoord, fishEyeDistortionStrength,
										 fishEyeDistortionZoom - chromaticAberrationAmount * 0.60);
		float2 uv2 = GetDistortionOffset(texCoord, fishEyeDistortionStrength,
										 fishEyeDistortionZoom - chromaticAberrationAmount * 2);

		if (!bEnableScopeEffect)
		{
			finalColor.g = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv1, 0).g;
			finalColor.b = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv2, 0).b;
		}
		else
		{
			finalColor.g = lerp(srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv1, 0).g, finalColor.g, 0.25 * srt.m_pData->m_consts.m_scopePostProcessBlend);
			finalColor.b = lerp(srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv2, 0).b, finalColor.b, 0.25 * srt.m_pData->m_consts.m_scopePostProcessBlend);
		}
	}

	if (bRegularMode)
	{
		if (bEnableHealthEffect)
		{
			finalColor = ApplyHealthEffect(srt.m_pData->m_consts.m_healthParam, srt.m_pData->m_consts.m_healthParamDesat, finalColor);
		}

		if (bEnablePainEffect)
		{
			finalColor = ApplyPainEffect(srt.m_pData->m_consts.m_painEffectControl, finalColor);
		}

		if (bEnableListenMode)
		{
			finalColor = ApplyListenModeHighlight(uv0, localDispatchThreadId.xy, srt.m_pData->m_listenModeVisible, srt.m_pData->m_listenModeHidden,
												  srt.m_pData->m_listenModeDepth, srt.m_pData->m_linearClampSampler, srt.m_pData->m_consts, finalColor);
		}

		if (bEnableHighlight)
		{
			finalColor = ApplyHighlights(uv0, srt.m_pData->m_linearClampSampler, srt.m_pData->m_highlightsColor,
										 srt.m_pData->m_highlightsDepth, srt.m_pData->m_consts.m_depthParams,
										 srt.m_pData->m_consts.m_highlightsAmount, finalColor);
		}

		if (bEnableVignetteOffset)
		{
			finalColor = ApplyVignetteOffset(srt.m_pData->m_consts, uv0, finalColor);
		}

		if (bEnableScopeEffect && !bIsInScope)
		{
			finalColor *= lerp(1.0, 0.85, saturate(texCoord.x) * srt.m_pData->m_consts.m_scopePostProcessBlend);
		}
	}

	{
		if (bMovieCrossFadeBlend)
		{
			//finalColor = BlendMovie(srt.m_pData->m_movieTexture, srt.m_pData->m_consts.m_movieDissolveBlend, int2(dispatchThreadId * srt.m_pData->m_consts.m_movieSizeScale), finalColor);
			finalColor = BlendMovieScale(srt.m_pData->m_movieTexture,
										 srt.m_pData->m_linearClampSampler,
										 srt.m_pData->m_consts.m_movieDissolveBlend,
										 btexCoord * srt.m_pData->m_consts.m_movieSizeScale,
										 finalColor);
		}

		if (bMovieWipeBlend)
		{
			//finalColor = WipeMovie(srt.m_pData->m_movieTexture, srt.m_pData->m_consts.m_movieWipeBlend, int2(dispatchThreadId * srt.m_pData->m_consts.m_movieSizeScale), bufferSize, finalColor, srt.m_pData->m_consts.m_movieWipeDirection);
			finalColor = WipeMovieScale(srt.m_pData->m_movieTexture,
										srt.m_pData->m_linearClampSampler,
										srt.m_pData->m_consts.m_movieWipeBlend,
										btexCoord,
										srt.m_pData->m_consts.m_movieSizeScale,
										finalColor,
										srt.m_pData->m_consts.m_movieWipeDirection);
		}

		bool bApplyMovieBonusLut = srt.m_pData->m_consts.m_bonus.m_isPhotoFilter && (bMovieCrossFadeBlend || bMovieWipeBlend);
		if (bApplyMovieBonusLut)
		{
			finalColor = ApplyBonusLut(finalColor, srt.m_pData->m_consts.m_bonusLutBlend, srt.m_pData->m_consts.m_lutParams,
									   srt.m_pData->m_bonusLutTexture, srt.m_pData->m_linearClampSampler);
		}

		if (bChapterTitleBlend)
		{
			//finalColor = BlendChapterTitle(srt.m_pData->m_movieTexture, finalColor, dispatchThreadId/* * srt.m_pData->m_consts.m_movieSizeScale*/, 
				//int2(srt.m_pData->m_consts.m_chapterTitleMovieWidth, srt.m_pData->m_consts.m_chapterTitleMovieHeight),
				//srt.m_pData->m_consts.m_chapterTitleMovieOffsetX, srt.m_pData->m_consts.m_chapterTitleMovieOffsetY);
			finalColor = BlendChapterTitleScale(srt.m_pData->m_movieTexture,
												srt.m_pData->m_linearClampSampler,
												srt.m_pData->m_consts.m_movieSizeScale,
												finalColor,
												dispatchThreadId,
												int2(srt.m_pData->m_consts.m_chapterTitleMovieWidth, srt.m_pData->m_consts.m_chapterTitleMovieHeight),
												srt.m_pData->m_consts.m_chapterTitleMovieOffsetX,
												srt.m_pData->m_consts.m_chapterTitleMovieOffsetY);
		}
	}

	finalColor = ApplyFilmGrain(srt.m_pData->m_filmGrainTexture, srt.m_pData->m_pointSampler, srt.m_pData->m_consts.m_filmGrainIntensity,
								srt.m_pData->m_consts.m_filmGrainIntensity2, srt.m_pData->m_consts.m_filmGrainOffsetScale, btexCoord, finalColor, true);

	if (enableBonus)
	{
		float3 borderColor = float3(0.0f);
		if (srt.m_pData->m_consts.m_bonus.m_mode == kBonusDistortModeWatercolor)
		{
			borderColor = float3(1.0f);
		}
		finalColor = blackPixel ? borderColor : finalColor;

		BonusShaderConst bonus = srt.m_pData->m_consts.m_bonus;

		switch (bonus.m_mode)
		{
		case kBonusDistortModeWatercolor:
			{
				finalColor = pow(finalColor, 1.0f / 2.2f);
				
				auto sobel_color_intensity = [&](in const float2 uv, in Texture2D<float4> tex, in SamplerState customSampler, in float2 depthParams) -> float {
					float3 color = tex.SampleLevel(customSampler, uv, 0).rgb;
					return color;
				};
				
				auto sobel_depth_intensity = [&](in const float2 uv, in Texture2D<float> tex, in SamplerState customSampler, in float2 depthParams) -> float {
					float depth = tex.SampleLevel(customSampler, uv, 0);
					depth = GetLinearDepth(depth, depthParams);
					return depth;
				};
				
				float lineWidth = 1.0f / srt.m_pData->m_consts.m_bufferSize;
				
				float thickEdgeDarken = ApplySobelFilter<float4, decltype(sobel_color_intensity)>(uv0, 4.0f * lineWidth, srt.m_pData->m_srcColor, srt.m_pData->m_linearSampler, sobel_color_intensity, float2(0.0f));
				thickEdgeDarken *= 0.5f; 
				
				float thinEdgeDarken = ApplySobelFilter<float4, decltype(sobel_color_intensity)>(uv0, lineWidth, srt.m_pData->m_srcColor, srt.m_pData->m_linearSampler, sobel_color_intensity, float2(0.0f));
				
				float depthThickEdgeDarken = ApplySobelFilter<float, decltype(sobel_depth_intensity)>(uv0, 4.0f * lineWidth, srt.m_pData->m_depthBuffer, srt.m_pData->m_linearSampler, sobel_depth_intensity, srt.m_pData->m_consts.m_depthParams);
				depthThickEdgeDarken = saturate(depthThickEdgeDarken * 5.0f) * 0.5f;
				
				float depthThinEdgeDarken = ApplySobelFilter<float, decltype(sobel_depth_intensity)>(uv0, lineWidth, srt.m_pData->m_depthBuffer, srt.m_pData->m_linearSampler, sobel_depth_intensity, srt.m_pData->m_consts.m_depthParams);
				
				thickEdgeDarken = max(thickEdgeDarken, depthThickEdgeDarken);
				
				float edgeDarken = saturate(thickEdgeDarken + thinEdgeDarken);
				
				float luminance = Luminance(finalColor);
				finalColor = lerp(finalColor, luminance, -0.5f);
				
				float threshold = 0.4f;
				float softness = 0.1f;
				float thresholdedLuminance = 1.0f - saturate((threshold - luminance * (1.0f + 2.0f * softness) + 2.0f * softness) / (2.0f * softness + kEpsilon));

				float colorThickness = lerp(0.6f, 0.5f, thresholdedLuminance) + lerp(0.15f, 0.3f, thresholdedLuminance) * edgeDarken;
				
				// black screen
				//colorThickness = luminance < 0.01f ? 1.0f : colorThickness;
				
				finalColor = float3(1.0) - colorThickness * (1.0f - saturate(finalColor));
				
				finalColor = pow(finalColor, 2.2f);
			}
			break;
		case kBonusDistortModeVoid:
			{
				auto sobel_depth_intensity = [&](in const float2 uv, in Texture2D<float> tex, in SamplerState customSampler, in float2 depthParams) -> float {
					float depth = tex.SampleLevel(customSampler, uv, 0);
					depth = GetLinearDepth(depth, depthParams);
					return depth;
				};
				float lineWidth = 1.0f / srt.m_pData->m_consts.m_bufferSize;
				float depthEdge = ApplySobelFilter<float, decltype(sobel_depth_intensity)>(uv0, lineWidth, srt.m_pData->m_depthBuffer, srt.m_pData->m_linearSampler, sobel_depth_intensity, srt.m_pData->m_consts.m_depthParams);
				depthEdge = 4.0f * (depthEdge - 0.75f);
				
				float depthZ = srt.m_pData->m_depthBuffer.SampleLevel(srt.m_pData->m_pointClampSampler, uv0, 0);
				float linearDepth = GetLinearDepth(depthZ, srt.m_pData->m_consts.m_depthParams);
				
				const float2  kCenter = float2(0.5f, 0.5f);
				float2 cellCoord = uv0;
				float scale = srt.m_pData->m_consts.m_pixelSize.x * 16.0f;
				cellCoord = float2(-1.0f, -1.0f);
				float2	cellSize = float2(scale, scale * srt.m_pData->m_consts.m_pixelSize.y / srt.m_pData->m_consts.m_pixelSize.x);
				float2	cell = floor(uv0 / cellSize);
				cellCoord = (cell + kCenter) * cellSize;
				float cellDepth = srt.m_pData->m_depthBuffer.SampleLevel(srt.m_pData->m_pointClampSampler, cellCoord, 0);
				float linearCellDepth = GetLinearDepth(cellDepth, srt.m_pData->m_consts.m_depthParams);
				
				float depthLineFade = saturate((linearDepth - 30.0f) / 10.0f);
				float depthDistanceFade = saturate((linearDepth - 30.0f) / 10.0f);
				
				float transition = saturate(abs(linearCellDepth - 36.0f) / 2.5f);
				transition = transition == 1.0f ? 0.0f : transition;
				
				float lineColorFade = saturate((linearDepth - 50.0f) / 40.0f);
				float3 lineColor = lerp(float3(0.25f, 0.6f, 0.0f), float3(0.03f, 0.2f, 0.0f), lineColorFade);
				lineColor = lerp(lineColor, float3(0.25f, 1.0f, 0.0f), transition > 0.0f ? Pow3(1.0 - transition) : 0.0f);
				
				int idx = int(sqrt(1.0f - transition) * 25.999f);
				transition = transition > 0.0f ? 1.0f : 0.0f;
				depthEdge = transition > 0.6f ? 0.0f : depthEdge;
				int2 pixelIdx = dispatchThreadId.xy % 16 + int2(16 * idx, 0);
				float2 asciiUv = float2(pixelIdx) / float2(416.0f, 16.0f);
				float asciiColor = srt.m_pData->m_bonusAsciiTexture.SampleLevel(srt.m_pData->m_pointClampSampler, asciiUv, 0).r;
				transition *= asciiColor;
				
				finalColor = lerp(finalColor, float3(0.0f), depthDistanceFade);

				finalColor = lerp(finalColor, lineColor, max(saturate(depthEdge * depthLineFade), transition));
			}
			break;
		case kBonusDistortModePopPoster:
			{
				float depthZ = srt.m_pData->m_depthBuffer.SampleLevel(srt.m_pData->m_pointClampSampler, uv0, 0);
				if (depthZ >= 1.0f)
				{
					finalColor = float3(1.0f, 0.6f, 0.6f);
				}

				const bool is_water = IsStencilWater(stencil);
				if (is_water)
				{
					finalColor = float3(0.145f, 0.165f, 0.220f);
				}
			}
			break;
		case kBonusDistortModeAnimatedNoir:
			{
				float warmness = saturate(max(finalColor.r - finalColor.b, finalColor.r - finalColor.g) / finalColor.r);
				finalColor = lerp(finalColor, float3(Luminance(finalColor)), Pow2(1.0f - warmness));
				finalColor = finalColor * finalColor * 2.0f;
			}
			break;
		case kBonusDistortModePrint:
			{
				if (!all(finalColor < 0.0001f))
				{
					float2 dotSize = float2(0.004f);
					float2 dotSize2 = dotSize * float2(1.0f, srt.m_pData->m_consts.m_aspect);
					float2 cellColorUv = floor(uv0 / dotSize2) * dotSize2 + 0.5f * dotSize2;
					
					float2 aspect = float2(srt.m_pData->m_consts.m_aspect, 1.0f);
					float3 printColor = float3(1.0f);
					
					// black
					{
						float3 elementColor = 1.0f - float3(0.0f, 0.0f, 0.0f);
						float rotation = 0.25f * kPi;
						
						float2 cellUv = uv0;
						cellUv *= aspect;
						cellUv = Rotate2D(rotation, cellUv);
						float2 centerPosition = floor(cellUv / dotSize) * dotSize + 0.5f * dotSize;
						centerPosition = Rotate2D(-rotation, centerPosition);
						centerPosition /= aspect;
						float dotDist = length((uv0 - centerPosition) * aspect);
						
						float3 cellColor = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, centerPosition, 0);
						float cellBlack = min(min(1.0f - cellColor.r, 1.0f - cellColor.g), 1.0f - cellColor.b);
						cellBlack = Pow3(cellBlack);
						
						float value = cellBlack;
						value = saturate((sqrt(value) * dotSize * 0.5f * 4.0f / kPi - dotDist) * 2500.0f);
						
						printColor -= value * elementColor;
					}
					
					// cyan
					{
						float3 elementColor = 1.0f - float3(0.0f, 1.0f, 1.0f);
						float rotation = 0.083f * kPi;
						
						float2 cellUv = uv0;
						cellUv *= aspect;
						cellUv = Rotate2D(rotation, cellUv);
						float2 centerPosition = floor(cellUv / dotSize) * dotSize + 0.5f * dotSize;
						centerPosition = Rotate2D(-rotation, centerPosition);
						centerPosition /= aspect;
						centerPosition += 0.1f * dotSize;
						float dotDist = length((uv0 - centerPosition) * aspect);
	
						float3 cellColor = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, centerPosition, 0);
						float cellBlack = min(min(1.0f - cellColor.r, 1.0f - cellColor.g), 1.0f - cellColor.b);
						cellBlack = Pow3(cellBlack);
	
						float value = (1.0f - cellColor.r - cellBlack) / (1.0f - cellBlack);
						value = saturate((sqrt(value) * dotSize * 0.5f * 4.0f / kPi - dotDist) * 2500.0f);
						
						printColor -= value * elementColor;
					}
					
					// magenta
					{
						float3 elementColor = 1.0f - float3(1.0f, 0.0f, 1.0f);
						float rotation = -0.416f * kPi;
						
						float2 cellUv = uv0;
						cellUv *= aspect;
						cellUv = Rotate2D(rotation, cellUv);
						float2 centerPosition = floor(cellUv / dotSize) * dotSize + 0.5f * dotSize;
						centerPosition = Rotate2D(-rotation, centerPosition);
						centerPosition /= aspect;
						centerPosition -= 0.1f * dotSize;
						float dotDist = length((uv0 - centerPosition) * aspect);
						
						float3 cellColor = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, centerPosition, 0);
						float cellBlack = min(min(1.0f - cellColor.r, 1.0f - cellColor.g), 1.0f - cellColor.b);
						cellBlack = Pow3(cellBlack);
	
						float value = (1.0f - cellColor.g - cellBlack) / (1.0f - cellBlack);
						value = saturate((sqrt(value) * dotSize * 0.5f * 4.0f / kPi - dotDist) * 2500.0f);
						
						printColor -= value * elementColor;
					}
					
					// yellow
					{
						float3 elementColor = 1.0f - float3(1.0f, 1.0f, 0.0f);
						float rotation = 0.0f;
						
						float2 cellUv = uv0;
						cellUv *= aspect;
						cellUv = Rotate2D(rotation, cellUv);
						float2 centerPosition = floor(cellUv / dotSize) * dotSize + 0.5f * dotSize;
						centerPosition = Rotate2D(-rotation, centerPosition);
						centerPosition /= aspect;
						float dotDist = length((uv0 - centerPosition) * aspect);
						
						float3 cellColor = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, centerPosition, 0);
						float cellBlack = min(min(1.0f - cellColor.r, 1.0f - cellColor.g), 1.0f - cellColor.b);
						cellBlack = Pow3(cellBlack);
	
						float value = (1.0f - cellColor.b - cellBlack) / (1.0f - cellBlack);
						value = saturate((sqrt(value) * dotSize * 0.5f * 4.0f / kPi - dotDist) * 2500.0f);
						
						printColor -= value * elementColor;
					}
					
					finalColor = printColor;
				}
			}
			break;
		}
	}

	// tint color.
	finalColor = finalColor * srt.m_pData->m_consts.m_tintColorScaleVector.xyz + srt.m_pData->m_consts.m_tintColorOffsetVector.xyz;

	srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)] = float4(max(finalColor, 0), 0.0f);
}

[numthreads(8, 8, 1)]
void CS_PostPostProcessing(int3 localDispatchThreadId : SV_DispatchThreadId, PostPostProcessingSrt srt : S_SRT_DATA)
{
	PostPostProcessing(localDispatchThreadId, srt, false);
}

[numthreads(8, 8, 1)]
void CS_PostPostProcessingBonus(int3 localDispatchThreadId : SV_DispatchThreadId, PostPostProcessingSrt srt : S_SRT_DATA) : SV_Target
{
	PostPostProcessing(localDispatchThreadId, srt, true);
}

struct PostProcessingHCMData
{
	RWTexture2D<float4>		m_dstColor;
	Texture2D<float4>		m_srcColor;
	Texture2D<float>		m_depthBuffer;
	Texture2D<uint>			m_stencilTexture;
	Texture2D<float3>		m_movieTexture;
	Texture2D<float4>		m_listenModeHidden;
	Texture2D<float4>		m_listenModeVisible;
	Texture2D<float>		m_listenModeDepth;
	Texture2D<float2>		m_hcmCoverage;

	SamplerState			m_pointClampSampler;
	SamplerState			m_pointSampler;
	SamplerState			m_linearClampSampler;
	SamplerState			m_linearSampler;

	float4					m_bufSize;

	int						m_debugOutlineMask;
	float					m_waterMinInstensity;
	int						m_isUnderWater;
	int						m_hcmMode;

	float					m_greyLevel;
	int						m_padding1[3];

	PostProcessingConst		m_consts;
};

struct PostProcessingHCMSrt
{
	PostProcessingHCMData*				m_pData;
	HCMSharedConst*						m_pHCMSharedConsts;
};

[numthreads(8, 8, 1)]
void CS_PostProcessingHighContrastMode(int3 localDispatchThreadId : SV_DispatchThreadId, PostProcessingHCMSrt srt : S_SRT_DATA) : SV_Target
{
	const int2 dispatchThreadId = localDispatchThreadId.xy;

	const float2 pixelSize = srt.m_pData->m_consts.m_pixelSize;
	float2 texCoord = (dispatchThreadId.xy + 0.5f) * pixelSize;
	texCoord.x = texCoord.x * srt.m_pData->m_consts.m_posScale + srt.m_pData->m_consts.m_posOffset;
	float depthZ = srt.m_pData->m_depthBuffer.SampleLevel(srt.m_pData->m_pointClampSampler, texCoord, 0);
	const float3 positionWS = GetPositionWS(dispatchThreadId, depthZ, srt.m_pData->m_consts.m_pixelSize.xy, srt.m_pData->m_consts.m_screenToAltWorldMat, srt.m_pData->m_consts.m_cameraPositionWS.xyz);	
	float2 uv0 = texCoord; // GetDistortionOffset(texCoord, fishEyeDistortionStrength, fishEyeDistortionZoom);

	float3 finalColor = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0, 0).xyz;

	// set color by colorblind mode
	const float3 enemyColor = GetHighContrastModeEnemyColor(srt.m_pData->m_consts.m_highContrastMode);
	const float3 pickupColor = GetHighContrastModePickupColor(srt.m_pData->m_consts.m_highContrastMode);
	const float3 heroColor = GetHighContrastModeHeroColor(srt.m_pData->m_consts.m_highContrastMode);
	const float3 interactiveColor = GetHighContrastModeInteractiveColor(srt.m_pData->m_consts.m_highContrastMode);

	bool bRegularMode = (srt.m_pData->m_consts.m_postMode & 0x07) == 0x07;
	bool bEnableHighlight = (srt.m_pData->m_consts.m_flags & 0x01) != 0;
	bool bEnableListenMode = (srt.m_pData->m_consts.m_flags & 0x02) != 0;

	if (bRegularMode)
	{
		if (bEnableListenMode)
		{
			auto hiddenTexEnemyExtract = [&](in const float4 value) -> float {
				return value.r;
			};
			auto hiddenTexBuddyHeroExtract = [&](in const float4 value) -> float {
				return value.g;
			};
			auto hiddenTexBuddyInteractiveExtract = [&](in const float4 value) -> float {
				return value.b;
			};

			const float3 listenModePixelEnemy = GetListenModeHighlight<decltype(hiddenTexEnemyExtract)>(uv0, localDispatchThreadId.xy, srt.m_pData->m_listenModeVisible, srt.m_pData->m_listenModeHidden,
				srt.m_pData->m_listenModeDepth, srt.m_pData->m_linearClampSampler, srt.m_pData->m_consts, hiddenTexEnemyExtract) * enemyColor;
			const float3 listenModePixelHero = GetListenModeHighlight<decltype(hiddenTexBuddyHeroExtract)>(uv0, localDispatchThreadId.xy, srt.m_pData->m_listenModeVisible, srt.m_pData->m_listenModeHidden,
				srt.m_pData->m_listenModeDepth, srt.m_pData->m_linearClampSampler, srt.m_pData->m_consts, hiddenTexBuddyHeroExtract) * heroColor;
			const float3 listenModePixelInteractive = GetListenModeHighlight<decltype(hiddenTexBuddyInteractiveExtract)>(uv0, localDispatchThreadId.xy, srt.m_pData->m_listenModeVisible, srt.m_pData->m_listenModeHidden,
				srt.m_pData->m_listenModeDepth, srt.m_pData->m_linearClampSampler, srt.m_pData->m_consts, hiddenTexBuddyInteractiveExtract) * interactiveColor;
			finalColor += listenModePixelEnemy + listenModePixelHero + listenModePixelInteractive;
		}
	}

	const float3 waterColor = GetHighContrastModeWaterColor(srt.m_pData->m_consts.m_highContrastMode);

	// [HCM Hack]
	// This will be removed after forward rendered objects are properly supported in high contrast mode.
	const uint stencil = srt.m_pData->m_stencilTexture.SampleLevel(srt.m_pData->m_pointClampSampler, uv0, 0);
	const float partCoverage = srt.m_pData->m_hcmCoverage.SampleLevel(srt.m_pData->m_pointClampSampler, uv0, 0).y;
	const bool is_water = ( IsStencilWater(stencil) && partCoverage == 0.0f ) || srt.m_pData->m_isUnderWater;
	if(is_water)
		finalColor = max( finalColor * waterColor * 1.2 , waterColor * srt.m_pData->m_waterMinInstensity );

	// Sonar detection
	const float sonarRadius = srt.m_pData->m_consts.m_sonarParam.w;
	const float maxSonarRadius = srt.m_pData->m_consts.m_sonarMaxRadius;
	const uint	sonarType = srt.m_pData->m_consts.m_sonarType;
	if (sonarRadius > 0.0f ){
		float3 colorTint = float3( 1.0f , 1.0f , 1.0f );
		if(sonarType == kSonarTypeEnemy)
			colorTint = enemyColor;
		else if( sonarType == kSonarTypePickup )
			colorTint = pickupColor;

		colorTint *= 0.4f;
		finalColor = ApplySonarMode(finalColor, positionWS, srt.m_pData->m_consts.m_sonarParam, maxSonarRadius, colorTint);
	}

	// outline for forward rendered objects.
	const float2 invSrcSize = srt.m_pData->m_bufSize.zw;
	const uint hcmType = GetStencilHCMType( stencil );
	const float outlineMask = is_water ? 1.0f : HighContrastModeForwardMask(hcmType, uv0, srt.m_pHCMSharedConsts, srt.m_pData->m_hcmCoverage );
	const float depthLinear = GetLinearDepth(depthZ, srt.m_pData->m_consts.m_depthParams);
	float edgeFactor = outlineMask * HighContrastModeOutline(hcmType, depthLinear , uv0 , float3( 0.0f , 0.0f , 0.0f ) , srt.m_pHCMSharedConsts);

	auto sobel_intensity = [&](in const float2 uv, in Texture2D<float2> tex, in SamplerState customSampler, float2 depthParams) -> float {
		float hc = tex.SampleLevel(customSampler, uv, 0).g;
		return hc > srt.m_pHCMSharedConsts->m_highContrastModeParticleOutlineThreshold;
	};

	// this is the particle outline
	float particleEdge = ApplySobelFilter<float2, decltype(sobel_intensity)>(uv0, invSrcSize, srt.m_pData->m_hcmCoverage, srt.m_pData->m_linearSampler, sobel_intensity, float2(0.0f)) * 0.5f;

	const float hcmCov = srt.m_pData->m_hcmCoverage.SampleLevel(srt.m_pHCMSharedConsts->pointClampSampler, uv0, 0).x;
	if(hcmCov > 0.0f)
	{
		particleEdge += edgeFactor;
	}
	else
	{
		float3 edgeColor = float3(edgeFactor,edgeFactor,edgeFactor);
		edgeColor = ApplyColorTint( hcmType, srt.m_pData->m_hcmMode , float3(edgeFactor,edgeFactor,edgeFactor) , 
												 edgeColor /* Base color is not available unless we pass it in. */
											   );
		finalColor = max( finalColor , edgeColor );
	}

	// accumulate edge color
	finalColor = (srt.m_pData->m_debugOutlineMask) ? outlineMask : (finalColor + particleEdge);

	// this is a last minute feature for model viewer in high contrast mode
	if( depthZ < 1.0f )
		finalColor = lerp( finalColor , float3( 0.3f , 0.3f , 0.3f ) , srt.m_pData->m_greyLevel );

	// to apply fade to black.  
	float2 btexCoord = texCoord;
	bool bMovieCrossFadeBlend = srt.m_pData->m_consts.m_movieDissolveBlend > 0.0f;
	bool bMovieWipeBlend = srt.m_pData->m_consts.m_movieWipeBlend > 0.0f;
	bool bChapterTitleBlend = srt.m_pData->m_consts.m_chapterTitleMovieWidth > 0;

	if (bMovieCrossFadeBlend)
		finalColor = BlendMovieScale(srt.m_pData->m_movieTexture,
			srt.m_pData->m_linearClampSampler,
			srt.m_pData->m_consts.m_movieDissolveBlend,
			btexCoord * srt.m_pData->m_consts.m_movieSizeScale,
			finalColor);

	if (bMovieWipeBlend)
		finalColor = WipeMovieScale(srt.m_pData->m_movieTexture,
			srt.m_pData->m_linearClampSampler,
			srt.m_pData->m_consts.m_movieWipeBlend,
			btexCoord,
			srt.m_pData->m_consts.m_movieSizeScale,
			finalColor,
			srt.m_pData->m_consts.m_movieWipeDirection);

	if (bChapterTitleBlend)
		finalColor = BlendChapterTitleScale(srt.m_pData->m_movieTexture,
			srt.m_pData->m_linearClampSampler,
			srt.m_pData->m_consts.m_movieSizeScale,
			finalColor,
			dispatchThreadId,
			int2(srt.m_pData->m_consts.m_chapterTitleMovieWidth, srt.m_pData->m_consts.m_chapterTitleMovieHeight),
			srt.m_pData->m_consts.m_chapterTitleMovieOffsetX,
			srt.m_pData->m_consts.m_chapterTitleMovieOffsetY);

	finalColor = finalColor * srt.m_pData->m_consts.m_tintColorScaleVector.xyz + srt.m_pData->m_consts.m_tintColorOffsetVector.xyz;

	srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)] = float4( saturate( finalColor ), 1.0f);
}

[numthreads(8, 8, 1)]
void CS_PostPostProcessingDebug(int3 localDispatchThreadId : SV_DispatchThreadId, PostPostProcessingSrt srt : S_SRT_DATA) : SV_Target
{
	PostPostProcessing(localDispatchThreadId, srt, false);

	int2 dispatchThreadId = localDispatchThreadId.xy + srt.m_screenOffset;
	
	const float2 pixelSize = srt.m_pData->m_consts.m_pixelSize;
	float2 texCoord = (dispatchThreadId.xy + 0.5f) * pixelSize;
		
	float3 finalColor = srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)].rgb;

	bool bSafeColorsMode	= srt.m_pData->m_consts.m_flags & 0x10;
	bool bClipColorsMode	= srt.m_pData->m_consts.m_flags & 0x20;
	bool bColorCheckerMode	= srt.m_pData->m_consts.m_flags & 0x40;
	bool bWaveformMonitor	= srt.m_pData->m_consts.m_flags & 0x80;
	bool bWhiteBorder		= srt.m_pData->m_consts.m_flags & 0x100;
	bool bBlackBorder		= srt.m_pData->m_consts.m_flags & 0x8000;

	float clipColorValue = 16.0/255.0;
	float combatColorValue = 32.0/255.0;

	if (bColorCheckerMode)
	{
		finalColor = srt.m_pData->m_colorChecker.SampleLevel(srt.m_pData->m_linearClampSampler, texCoord, 0);

		finalColor = finalColor < 0.0031308f ? 
					 finalColor * 12.92 : 
					 pow(finalColor, 1.0/2.4) * 1.055 - 0.055f;
	}

	if (bClipColorsMode)
	{
		finalColor = saturate((finalColor - clipColorValue) / (1.0 - clipColorValue));
	}

	if(bSafeColorsMode)
	{
		float maxColor = max(finalColor.r, max(finalColor.g, finalColor.b));
		if(maxColor == 0)
			finalColor = float3(0.5,0,0);
		else if(maxColor < 4.0/255.0)
			finalColor = float3(1,0,0);
		else if(maxColor < 8.0/255.0)
			finalColor = float3(1,0.5,0);
		else if(maxColor < clipColorValue)
			finalColor = float3(1,1,0);
	}

	if(bWaveformMonitor)
	{
		int2 bufferSize				= int2(srt.m_pData->m_consts.m_bufferSize);
		float displayScale			= 0.25;
		float displayGutter			= 0.015;
		int2 displayPos				= int2(bufferSize * (1.0 - displayScale - displayGutter));
		int2 displayMaxPos			= int2(bufferSize * (1.0 - displayGutter));
		int range					= int(bufferSize.y * displayScale);
		int topBorder				= 3;
		float sampleSpacing			= 2.0;
		float sampleContribution	= 0.05;
		
		if(dispatchThreadId.x > displayPos.x && dispatchThreadId.y > displayPos.y && dispatchThreadId.x < displayMaxPos.x && dispatchThreadId.y < displayMaxPos.y)
		{
			int2 uv0 = int2((dispatchThreadId - displayPos) / displayScale);
			int plotPosition = range - 1 - (dispatchThreadId.y - displayPos.y);
			
			if(plotPosition == range / 2)
				finalColor = 0.375;
			else if((plotPosition / (range / 10)) * (range / 10) == plotPosition)
				finalColor = 0.125;
			else
				finalColor = 0;
			
			for(int y = 0; y < bufferSize.y / sampleSpacing; y++)
			{
				int2 uv = int2(uv0.x, y * sampleSpacing);
				float3 sampleColor = srt.m_pData->m_srcColor.Load(int3(uv, 0)).xyz;
				
				if(bClipColorsMode)
					sampleColor = saturate((sampleColor - clipColorValue) / (1.0 - clipColorValue));				
					
				int3 sampleIndex = min(int3(sampleColor.rgb * range), range - topBorder);
	
				finalColor += (sampleIndex == plotPosition) * sampleContribution;
			}
			
			finalColor = saturate(finalColor);
			
			if(plotPosition == int(clipColorValue * range))
				finalColor = float3(1, 0, 0);
			else if(plotPosition == int(combatColorValue * range))
				finalColor = float3(1, 0.5, 0);
		}
	}

	if(bWhiteBorder || bBlackBorder)
	{
		int2 bufferSize = int2(srt.m_pData->m_consts.m_bufferSize);
		float2 border = float2(0.05, 0.05 * bufferSize.x / bufferSize.y);
		if(texCoord.x < border.x || texCoord.x > (1.0 - border.x) || texCoord.y < border.y || texCoord.y > (1.0 - border.y))
		{
			finalColor = bWhiteBorder ? float3(1,1,1) : float3(0,0,0);
		}
	}

	srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)] = float4(finalColor, 0.0f);
}

struct ColorBufferDuplicateWithHealthFxSrt
{
	RWTexture2D<float4>		dst_color;
	Texture2D<float4>		src_color;
	float					m_healthParamDesat;
};

[numthreads(8, 8, 1)]
void Cs_ColorBufferDuplicateWithHealthFx(uint2 dispatchThreadId : SV_DispatchThreadID, ColorBufferDuplicateWithHealthFxSrt* srt : S_SRT_DATA)
{
	bool bEnableHealthEffect		= srt->m_healthParamDesat > 0;

	float4 srcColor = srt->src_color.Load(int3(dispatchThreadId.xy, 0));

	float4 finalColor = srcColor;
	if(bEnableHealthEffect)
	{
		finalColor.rgb = ApplyHealthEffectDesat(srt->m_healthParamDesat, finalColor.rgb);
	}

	srt->dst_color[int2(dispatchThreadId.xy)] = finalColor;
}

// All distortion stuff must happen after checkerboard resolve
[numthreads(8, 8, 1)]
void CS_PostCheckerboardProcessing(int3 localDispatchThreadId : SV_DispatchThreadId, PostPostProcessingSrt srt : S_SRT_DATA) : SV_Target
{
	int2 dispatchThreadId = localDispatchThreadId.xy + srt.m_screenOffset;

	const float2 pixelSize = srt.m_pData->m_consts.m_displayPixelSize;
	const float2 bufferSize = srt.m_pData->m_consts.m_displayBufferSize;

	float2 texCoord0 = (dispatchThreadId.xy + 0.5f) * pixelSize;

	bool bEnableChromaticAberration = srt.m_pData->m_consts.m_chromaticAberrationAmount != 0;
	bool bMovieCrossFadeBlend		= srt.m_pData->m_consts.m_movieDissolveBlend > 0.0f;
	bool bMovieWipeBlend			= srt.m_pData->m_consts.m_movieWipeBlend > 0.0f;
	bool bChapterTitleBlend			= srt.m_pData->m_consts.m_chapterTitleMovieWidth > 0;

	float2 texCoord = texCoord0 + srt.m_pData->m_distortionVector.SampleLevel(srt.m_pData->m_linearClampSampler, texCoord0, 0).xy * pixelSize * float2(1.5f, 1.5f);	//need to scale by 1.5 to account for resolution difference moving from ps3 to ps4

	float2 uv0 = GetDistortionOffset(texCoord, srt.m_pData->m_consts.m_fishEyeDistortionStrength,
									srt.m_pData->m_consts.m_fishEyeDistortionZoom);

	float3 finalColor = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv0, 0).xyz;

	if (bEnableChromaticAberration)
	{
		float2 uv1 = GetDistortionOffset(texCoord, srt.m_pData->m_consts.m_fishEyeDistortionStrength,
										 srt.m_pData->m_consts.m_fishEyeDistortionZoom - srt.m_pData->m_consts.m_chromaticAberrationAmount * 0.60);
		float2 uv2 = GetDistortionOffset(texCoord, srt.m_pData->m_consts.m_fishEyeDistortionStrength,
										 srt.m_pData->m_consts.m_fishEyeDistortionZoom - srt.m_pData->m_consts.m_chromaticAberrationAmount * 2);

		finalColor.g = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv1, 0).g;
		finalColor.b = srt.m_pData->m_srcColor.SampleLevel(srt.m_pData->m_linearClampSampler, uv2, 0).b;
	}

	{
		if (bMovieCrossFadeBlend)
			//finalColor = BlendMovie(srt.m_pData->m_movieTexture, srt.m_pData->m_consts.m_movieDissolveBlend, int2(dispatchThreadId * srt.m_pData->m_consts.m_movieSizeScale), finalColor);
			finalColor = BlendMovieScale(srt.m_pData->m_movieTexture, 
										 srt.m_pData->m_linearClampSampler, 
										 srt.m_pData->m_consts.m_movieDissolveBlend, 
										 texCoord0 * srt.m_pData->m_consts.m_movieSizeScale, 
										 finalColor);

		if (bMovieWipeBlend)
			//finalColor = WipeMovie(srt.m_pData->m_movieTexture, srt.m_pData->m_consts.m_movieWipeBlend, int2(dispatchThreadId * srt.m_pData->m_consts.m_movieSizeScale), bufferSize, finalColor, srt.m_pData->m_consts.m_movieWipeDirection);
			finalColor = WipeMovieScale(srt.m_pData->m_movieTexture, 
										srt.m_pData->m_linearClampSampler, 
										srt.m_pData->m_consts.m_movieWipeBlend, 
										texCoord0, 
										srt.m_pData->m_consts.m_movieSizeScale, 
										finalColor, 
										srt.m_pData->m_consts.m_movieWipeDirection);

		if (bChapterTitleBlend)
			//finalColor = BlendChapterTitle(srt.m_pData->m_movieTexture, finalColor, dispatchThreadId/* * srt.m_pData->m_consts.m_movieSizeScale*/, 
				//int2(srt.m_pData->m_consts.m_chapterTitleMovieWidth, srt.m_pData->m_consts.m_chapterTitleMovieHeight),
				//srt.m_pData->m_consts.m_chapterTitleMovieOffsetX, srt.m_pData->m_consts.m_chapterTitleMovieOffsetY);
			finalColor = BlendChapterTitleScale(srt.m_pData->m_movieTexture, 
												srt.m_pData->m_linearClampSampler, 
												srt.m_pData->m_consts.m_movieSizeScale,
												finalColor, 
												dispatchThreadId, 
												int2(srt.m_pData->m_consts.m_chapterTitleMovieWidth, srt.m_pData->m_consts.m_chapterTitleMovieHeight),
												srt.m_pData->m_consts.m_chapterTitleMovieOffsetX, 
												srt.m_pData->m_consts.m_chapterTitleMovieOffsetY);
	}

	srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)] = float4(finalColor, 0.0f);
}

// =====================================================================================================================

struct ApplyAccessibilityZoomSrt
{
	float2 m_invBufferSize;
	float4 m_uvRemap; // [minU, maxU, minV, maxV]
	float m_zoomLerp;

	Texture2D<float4> m_tSrcColor;
	RW_Texture2D<float4> m_tDstColor;

	SamplerState m_linearClampSampler;
};

[numthreads(8, 8, 1)]
void CS_ApplyAccessibilityZoom(uint2 dispatchThreadID: SV_DispatchThreadID, ApplyAccessibilityZoomSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float2 pixelXY = (float2)dispatchThreadID;
	float2 inputUV = pixelXY * pSrt->m_invBufferSize;

	float zoomedU = pSrt->m_uvRemap.x + ((pSrt->m_uvRemap.y - pSrt->m_uvRemap.x) * inputUV.x);
	float zoomedV = pSrt->m_uvRemap.z + ((pSrt->m_uvRemap.w - pSrt->m_uvRemap.z) * inputUV.y);

	zoomedU = lerp(inputUV.x, zoomedU, pSrt->m_zoomLerp);
	zoomedV = lerp(inputUV.y, zoomedV, pSrt->m_zoomLerp);

	float3 srcColor = pSrt->m_tSrcColor.SampleLevel(pSrt->m_linearClampSampler, float2(zoomedU, zoomedV), 0.0f).xyz;
	pSrt->m_tDstColor[dispatchThreadID] = float4(srcColor, 1.0f);
}

struct AccessibilityZoomDebugViewSrt
{
	int4 m_zoomRegionPixels;
	int2 m_touchPixelPosition;
	float m_initiateZoomPercent;
	RW_Texture2D<float4> m_tPrimaryFloat;
};

[numthreads(8, 8, 1)]
void CS_AccessibilityZoomDebugView(uint2 dispatchThreadID: SV_DispatchThreadID, AccessibilityZoomDebugViewSrt* pSrt : S_SRT_DATA) : SV_Target
{
	int2 pixelXY = (int2)dispatchThreadID;

	float3 srcColor = pSrt->m_tPrimaryFloat[pixelXY].xyz;
	float3 outputColor = srcColor;

	int minX = pSrt->m_zoomRegionPixels.x;
	int maxX = pSrt->m_zoomRegionPixels.y;
	int minY = pSrt->m_zoomRegionPixels.z;
	int maxY = pSrt->m_zoomRegionPixels.w;

	int minXDisp = abs(pixelXY.x - minX);
	int maxXDisp = abs(pixelXY.x - maxX);
	int minYDisp = abs(pixelXY.y - minY);
	int maxYDisp = abs(pixelXY.y - maxY);

	int borderThickness = 0;

	// Uncomment for pink border
	/*
	if ((minXDisp <= borderThickness || maxXDisp <= borderThickness) && pixelXY.y >= minY && pixelXY.y <= maxY)
	{
		outputColor = float3(1.0f, 0.0f, 1.0f);
	}

	if ((minYDisp <= borderThickness || maxYDisp <= borderThickness) && pixelXY.x >= minX && pixelXY.x <= maxX)
	{
		outputColor = float3(1.0f, 0.0f, 1.0f);
	}
	*/

	int touchCircleRadius = 30;
	int lenFromTouch = length(pixelXY - pSrt->m_touchPixelPosition);
	int innerFillRadius = (int)((float)touchCircleRadius * pSrt->m_initiateZoomPercent);
	if (lenFromTouch == touchCircleRadius || lenFromTouch <= innerFillRadius)
	{
		outputColor = float3(1.0f, 0.0f, 1.0f);
	}

	pSrt->m_tPrimaryFloat[pixelXY] = float4(outputColor, 1.0f);
}
