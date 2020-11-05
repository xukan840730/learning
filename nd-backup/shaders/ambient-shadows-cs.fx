#include "global-funcs.fxi"
#include "packing.fxi"
#include "culling.fxi"

#ifndef AMBIENT_SHADOWS_THREAD_DEF
	#define kAmbientShadowsTileWidth	8
	#define kAmbientShadowsTileHeight	8
	#define kAmbientShadowsNumTileThreads ( (kAmbientShadowsTileWidth) * (kAmbientShadowsTileHeight) )
	#define AMBIENT_SHADOWS_THREAD_DEF	numthreads(kAmbientShadowsTileWidth, kAmbientShadowsTileHeight, 1)
#endif

#define kMaxOccluders				1024
#define kOccludersBatchSize			64

struct Occluder
{
	float4					m_positionInfluenceRadius;
	float4					m_upAxisRadius;
	float4					m_leftAxisFadeoutRadius;
	float4					m_forwardAxisLengthRcp;
	uint					m_objectId;
	uint3					m_pad;
};

struct OccluderVol
{
	row_major matrix		m_screenToAmbShadowVolume;
	row_major matrix		m_ambShadowVolumeToWorld;
	float4					m_positionInfluenceRadius;
	uint					m_volumeTextureIdx;
	uint					m_objectId;
	uint2					m_pad;
};

struct AmbientShadowConstants
{
	uint		m_numCapsuleOccluders;
	uint		m_numVolumeOccluders;
	uint		m_ambientShadowResMode;
	uint		m_stencilTestAgainstBoatEnabled;
	int2		m_fullBufferSize;
	float		m_fgIntensity;
	float		m_bgIntensity;
	uint4		m_params;
	float4		m_projectionParamsXY;
	float4		m_projectionParamsZ;
	float4		m_scaleParams;
	float4x4	m_worldToView;
};

struct AmbientShadowBuffers
{
	StructuredBuffer<Occluder> m_occluders;	
	StructuredBuffer<OccluderVol> m_occluderVols;
	StructuredBuffer<uint> m_mappingTable;
};

struct AmbientShadowSamplers
{
	SamplerState				m_sLinearSampler;
};

struct AmbientShadowTextures
{
	Texture2D<uint4>			m_gbuffer0;
	Texture2D<uint4>			m_lDirBuffer;
	Texture2D<uint>				m_dsBufOffset;
	Texture2D<float>			m_depthBuffer;
	Texture2D<uint>				m_stencilBuffer;
	Texture2D<uint2>			m_objectIdBuffer;
	Texture2D<float>			m_occlusionLookup;
    RWTexture2D<float3>			m_resultsBuffer;
	Texture3D<float4>			m_volumeTextures[kMaxOccluders];
};

struct AmbientShadowSrt
{
	AmbientShadowConstants		*pConsts;
	AmbientShadowBuffers		*pBufs;
	AmbientShadowTextures		*pTexs;
	AmbientShadowSamplers		*pSamplers;
};

groupshared uint			g_numOccludersInBatch;
groupshared uint			g_occluderIdx[kOccludersBatchSize];

int2 GetFullPixelOffset(int2 dispatchThreadId, AmbientShadowSrt srt)
{
	int2 srcBufferOffset = int2(dispatchThreadId.xy);
	if (srt.pConsts->m_ambientShadowResMode > 0)
	{
		if (srt.pConsts->m_ambientShadowResMode == 1)
		{
			uint dsOffset = srt.pTexs->m_dsBufOffset[int2(dispatchThreadId.x / 4, dispatchThreadId.y)] >> ((dispatchThreadId.x & 0x03) * 2);
			srcBufferOffset = clamp(srcBufferOffset * 2 + (int2(dsOffset, dsOffset >> 1) & 0x01), 0, srt.pConsts->m_fullBufferSize);
		}
		else
		{
			uint dsOffset = srt.pTexs->m_dsBufOffset[dispatchThreadId.xy];
			srcBufferOffset = clamp(srcBufferOffset * 4 + (int2(dsOffset, dsOffset >> 4) & 0x03), 0, srt.pConsts->m_fullBufferSize);
		}
	}

	return srcBufferOffset;
}

groupshared uint			g_numVolOccludersInBatch;
groupshared uint			g_numCapOccludersInBatch;
groupshared uint			g_occluderVolIdx[4 * kOccludersBatchSize];
groupshared uint			g_occluderCapIdx[4 * kOccludersBatchSize];


[AMBIENT_SHADOWS_THREAD_DEF]
void Cs_AmbientShadows(uint3 dispatchThreadId : SV_DispatchThreadID,
	uint groupIndex : SV_GroupIndex,
	AmbientShadowSrt srt : S_SRT_DATA)

{
	uint2 screenPos = dispatchThreadId.xy;
	uint4 params = srt.pConsts->m_params;
	uint numCapOccluders = srt.pConsts->m_numCapsuleOccluders;
	uint numVolOccluders = srt.pConsts->m_numVolumeOccluders;

	if (screenPos.x > params.x - 1) screenPos.x = params.x - 1;
	if (screenPos.y > params.y - 1) screenPos.y = params.y - 1;

	int2 srcBufferOffset = GetFullPixelOffset(int2(dispatchThreadId.xy), srt);

	uint4 sample1 = srt.pTexs->m_lDirBuffer[srcBufferOffset];
	uint stencil = srt.pTexs->m_stencilBuffer[srcBufferOffset];

	// compute the view-space bounding box of the given tile
	float3 dominantLightDir = UnpackGBufferDomDir(sample1);
	float3 dominantLightDirView = mul((float3x3)srt.pConsts->m_worldToView, dominantLightDir);

	float4 normalRoughness = UnpackGBufferNormalRoughness(srt.pTexs->m_gbuffer0[srcBufferOffset]);
	float roughness = max(normalRoughness.w, 0.25)*0.5;
	float coneAngleCosSq = roughness*roughness;

	float3 normalWS = normalRoughness.xyz;
	float3 normalVS = mul((float3x3)srt.pConsts->m_worldToView, normalWS);

	float uvU = ((float)screenPos.x + 0.5f) / params.x;
	float uvV = 1.0f - ((float)screenPos.y + 0.5f) / params.y;

	float screenX = (uvU * 2.0f - 1.0f);
	float screenY = (uvV * 2.0f - 1.0f);
	float screenZ = srt.pTexs->m_depthBuffer.Load(int3(screenPos.xy, 0));

	float viewZ = srt.pConsts->m_projectionParamsZ.y / (screenZ - srt.pConsts->m_projectionParamsZ.x);
	float viewXN = (screenX * srt.pConsts->m_projectionParamsXY.y - srt.pConsts->m_projectionParamsZ.z);
	float viewYN = (screenY * srt.pConsts->m_projectionParamsXY.w - srt.pConsts->m_projectionParamsZ.w);

	float viewX = viewXN * viewZ;
	float viewY = viewYN * viewZ;

	float3 positionVS = float3(viewX, viewY, viewZ);
	float3 reflectVS = reflect(normalize(positionVS), normalVS);

	// Fadeout parameters
	float occluderFadeoutVal = 0.9f;

	float minZ_01 = min(viewZ, LaneSwizzle(viewZ, 0x1f, 0x00, 0x01));
	float maxZ_01 = max(viewZ, LaneSwizzle(viewZ, 0x1f, 0x00, 0x01));

	float minZ_02 = min(minZ_01, LaneSwizzle(minZ_01, 0x1f, 0x00, 0x02));
	float maxZ_02 = max(maxZ_01, LaneSwizzle(maxZ_01, 0x1f, 0x00, 0x02));

	float minZ_04 = min(minZ_02, LaneSwizzle(minZ_02, 0x1f, 0x00, 0x04));
	float maxZ_04 = max(maxZ_02, LaneSwizzle(maxZ_02, 0x1f, 0x00, 0x04));

	float minZ_08 = min(minZ_04, LaneSwizzle(minZ_04, 0x1f, 0x00, 0x08));
	float maxZ_08 = max(maxZ_04, LaneSwizzle(maxZ_04, 0x1f, 0x00, 0x08));

	float minZ_10 = min(minZ_08, LaneSwizzle(minZ_08, 0x1f, 0x00, 0x10));
	float maxZ_10 = max(maxZ_08, LaneSwizzle(maxZ_08, 0x1f, 0x00, 0x10));

	float bboxMinZ = min(ReadLane(minZ_10, 0x00), ReadLane(minZ_10, 0x20));
	float bboxMaxZ = max(ReadLane(maxZ_10, 0x00), ReadLane(maxZ_10, 0x20));

	float minViewXN = ReadLane(viewXN, 0x3f);
	float minViewYN = ReadLane(viewYN, 0x3f);
	float minViewZN = ReadLane(viewZ, 0x3f);
	float maxViewXN = ReadLane(viewXN, 0x00);
	float maxViewYN = ReadLane(viewYN, 0x00);
	float maxViewZN = ReadLane(viewZ, 0x00);

	float minViewX = min(minViewXN * minViewZN, minViewXN * maxViewZN);
	float maxViewX = max(maxViewXN * minViewZN, maxViewXN * maxViewZN);
	float minViewY = min(minViewYN * minViewZN, minViewYN * maxViewZN);
	float maxViewY = max(maxViewYN * minViewZN, maxViewYN * maxViewZN);

	float3 froxelAabbMin = float3(minViewX, minViewY, bboxMinZ);
	float3 froxelAabbMax = float3(maxViewX, maxViewY, bboxMaxZ);

	float4 froxelSidePlanes[4];
	froxelSidePlanes[0] = float4(normalize(float3(1.0f, 0, -minViewXN)), 0.0f);
	froxelSidePlanes[1] = float4(normalize(float3(-1.0f, 0, maxViewXN)), 0.0f);
	froxelSidePlanes[2] = float4(normalize(float3(0, 1.0f, -minViewYN)), 0.0f);
	froxelSidePlanes[3] = float4(normalize(float3(0, -1.0f, maxViewYN)), 0.0f);

	if (groupIndex == 0)
	{
		g_numVolOccludersInBatch = 0;
		g_numCapOccludersInBatch = 0;
	}

	// only FGs have valid object ids on base
	uint objectId = 0xFFFFFFFF;
	bool isFg = stencil & 0x20;
	float intensity = srt.pConsts->m_bgIntensity;
	if (isFg)
	{
		static const uint kNumPrimIdBits = 17;
		objectId = srt.pTexs->m_objectIdBuffer[srcBufferOffset].x >> kNumPrimIdBits;

		// convert visibility buffer object ID to ambient occluders object ID
		objectId = srt.pBufs->m_mappingTable[objectId];

		intensity = srt.pConsts->m_fgIntensity;
	}

	// Multipurpose stencil bit used by interior of boat to mask out water, foliage, ambient shadows etc
	if (srt.pConsts->m_stencilTestAgainstBoatEnabled && (stencil & 0x04))
	{
		intensity = 0.0f;
	}

	// gather capsules
	uint batchStartOccluder = 0;
	while (batchStartOccluder < numCapOccluders)
	{
		if (groupIndex < kOccludersBatchSize)
		{
			// each thread processes a single occluder
			uint occluderToProcess = batchStartOccluder + groupIndex;
			if (occluderToProcess < numCapOccluders)
			{
				float4 occluderInfluenceSphereVS = srt.pBufs->m_occluders[occluderToProcess].m_positionInfluenceRadius;

				// if occluder intersects current tile add it to the list
				if (FroxelSphereOverlap(froxelAabbMin, froxelAabbMax, froxelSidePlanes, occluderInfluenceSphereVS.xyz, occluderInfluenceSphereVS.w))
				{
					uint occluderIndex = 0;
					InterlockedAdd(g_numCapOccludersInBatch, 1u, occluderIndex);

					g_occluderCapIdx[occluderIndex] = occluderToProcess;
				}
			}
		}
		batchStartOccluder += kOccludersBatchSize;
	}

	// gather volumes
	batchStartOccluder = 0;
	while (batchStartOccluder < numVolOccluders)
	{
		if (groupIndex < kOccludersBatchSize)
		{
			uint occluderToProcess = batchStartOccluder + groupIndex;
			if (occluderToProcess < numVolOccluders)
			{
				float4 occluderInfluenceSphereVS = srt.pBufs->m_occluderVols[occluderToProcess].m_positionInfluenceRadius;

				// if occluder intersects current tile add it to the list

				// Change to box-box eventually [MR]
				if (FroxelSphereOverlap(froxelAabbMin, froxelAabbMax, froxelSidePlanes, occluderInfluenceSphereVS.xyz, occluderInfluenceSphereVS.w))
				{
					uint occluderIndex = 0;
					InterlockedAdd(g_numVolOccludersInBatch, 1u, occluderIndex);

					// add box info to the occluder list for this tile
					g_occluderVolIdx[occluderIndex] = occluderToProcess;
				}
			}
		}
		batchStartOccluder += kOccludersBatchSize;
	}

	float totalCapAmbientOcclusion = 1.0f;
	float totalCapDirectionalOcclusion = 1.0f;
	float totalCapReflectionOcclusion = 1.0f;

	if (intensity > 0)
	{
		// switch to thread-per-pixel processing
		for (uint i = 0; i < g_numCapOccludersInBatch; ++i)
		{
			uint			vidx = g_occluderCapIdx[i];
			Occluder occ = srt.pBufs->m_occluders[vidx];

			// don't apply occluders to parent object
			if (objectId == occ.m_objectId)
				continue;

			float4 positionInfluenceRadius = occ.m_positionInfluenceRadius;
			float4 upAxisRadius = occ.m_upAxisRadius;
			float4 leftAxisFadeoutRadius = occ.m_leftAxisFadeoutRadius;
			float4 forwardAxisLengthRcp = occ.m_forwardAxisLengthRcp;

			// convert view space pos to occluder pos
			float3 vecToOccluderVS = positionVS - positionInfluenceRadius.xyz;

			float3 occluderSpacePos = float3(dot(leftAxisFadeoutRadius.xyz, vecToOccluderVS),
				dot(forwardAxisLengthRcp.xyz, vecToOccluderVS),
				dot(upAxisRadius.xyz, vecToOccluderVS));

			float distToOccluderUnscaledSq = dot(occluderSpacePos, occluderSpacePos);
			occluderSpacePos.z *= forwardAxisLengthRcp.w;

			float distToOccluderRcp = rsqrt(dot(occluderSpacePos, occluderSpacePos));
			float3 vecToOccluder = -occluderSpacePos * distToOccluderRcp;

			float halfBlockerAngleSin = saturate(upAxisRadius.w * distToOccluderRcp);
			halfBlockerAngleSin = (halfBlockerAngleSin * 127.0f / 128.0f) + (0.5f / 128.0f);

			// Ambient Occlusion //////////////////////////
			// not quite right, but cheaper than the real thing, and looks good enough
			float ambientOcclusion = 1.0f - saturate(upAxisRadius.w * upAxisRadius.w * distToOccluderRcp * distToOccluderRcp);

			// Aesthetic tuning for falloff
			ambientOcclusion *= ambientOcclusion;
			ambientOcclusion *= ambientOcclusion;

			// shadowing term to eliminate occlusion for pixels facing away from occluders
			float normalFixedAmbientOcclusion = lerp(1.0f, ambientOcclusion, saturate(dot(normalVS, -normalize(vecToOccluderVS))));
			ambientOcclusion = lerp(ambientOcclusion, normalFixedAmbientOcclusion, srt.pConsts->m_scaleParams.y);

			// Directional Occlusion //////////////////////
			float directionalOcclusion = 1.0f;

			{
				float3 occluderSpaceDir = float3(dot(leftAxisFadeoutRadius.xyz, dominantLightDirView),
					dot(forwardAxisLengthRcp.xyz, dominantLightDirView),
					dot(upAxisRadius.xyz, dominantLightDirView));

				occluderSpaceDir.z *= forwardAxisLengthRcp.w;
				occluderSpaceDir = normalize(occluderSpaceDir);

				float blockerToConeAxisCos = saturate(dot(vecToOccluder, occluderSpaceDir));

				blockerToConeAxisCos = (blockerToConeAxisCos * 511.0f / 512.0f) + (0.5f / 512.0f);

				directionalOcclusion = srt.pTexs->m_occlusionLookup.SampleLevel(srt.pSamplers->m_sLinearSampler, float2(blockerToConeAxisCos, halfBlockerAngleSin), 0.0f);
			}

			// Reflection Occlusion //////////////////////
			float reflectionOcclusion = 1.0f;

			{
				float3 occluderSpaceReflection = float3(dot(leftAxisFadeoutRadius.xyz, reflectVS),
					dot(forwardAxisLengthRcp.xyz, reflectVS),
					dot(upAxisRadius.xyz, reflectVS));

				occluderSpaceReflection.z *= forwardAxisLengthRcp.w;
				occluderSpaceReflection = normalize(occluderSpaceReflection);

				float reflectionBlockerToConeAxisCos = saturate(dot(vecToOccluder, occluderSpaceReflection));

				// Note: the following code is a hack, we should do a proper intersection of the reflection cone (based on roughness), vs the occluder
				float reflectionBlockerToConeAxisSinSq = (1.0f - reflectionBlockerToConeAxisCos * reflectionBlockerToConeAxisCos);
				float arcRadius = upAxisRadius.w * distToOccluderRcp;
				float arcRadiusSq = arcRadius * arcRadius;
				reflectionOcclusion = saturate((reflectionBlockerToConeAxisSinSq + coneAngleCosSq - arcRadiusSq) / (sqrt(2.0)*coneAngleCosSq));
			}

			// Fadeout ////////////////////////////////////
			float fadeOutStartDistSq = (positionInfluenceRadius.w * occluderFadeoutVal) * (positionInfluenceRadius.w * occluderFadeoutVal);
			float fadeOutEndDistSq = positionInfluenceRadius.w * positionInfluenceRadius.w;
			float fadeoutFactor = saturate((distToOccluderUnscaledSq - fadeOutStartDistSq) / (fadeOutEndDistSq - fadeOutStartDistSq));

			ambientOcclusion = lerp(ambientOcclusion, 1.0f, fadeoutFactor);
			directionalOcclusion = lerp(directionalOcclusion, 1.0f, fadeoutFactor);
			reflectionOcclusion = lerp(reflectionOcclusion, 1.0f, lerp(fadeoutFactor, 1.0, roughness));

			totalCapAmbientOcclusion *= ambientOcclusion;
			totalCapDirectionalOcclusion *= directionalOcclusion;
			totalCapReflectionOcclusion *= reflectionOcclusion;
		}

		{
			float minAmbientVal = 0.05f;
			float ambientA = (minAmbientVal * minAmbientVal + 1.0f) / (minAmbientVal * minAmbientVal + 2.0f * minAmbientVal + 1.0f);
			float ambientB = 2.0f * ambientA * minAmbientVal - minAmbientVal;
			float ambientC = ambientA * minAmbientVal * minAmbientVal + minAmbientVal - minAmbientVal * minAmbientVal;

			float minDirectionalVal = 0.25f;
			float directionalA = (minDirectionalVal * minDirectionalVal + 1.0f) / (minDirectionalVal * minDirectionalVal + 2.0f * minDirectionalVal + 1.0f);
			float directionalB = 2.0f * directionalA * minDirectionalVal - minDirectionalVal;
			float directionalC = directionalA * minDirectionalVal * minDirectionalVal + minDirectionalVal - minDirectionalVal * minDirectionalVal;

			float minReflectionVal = 0.1f;
			float reflectionA = (minReflectionVal * minReflectionVal + 1.0f) / (minReflectionVal * minReflectionVal + 2.0f * minReflectionVal + 1.0f);
			float reflectionB = 2.0f * reflectionA * minDirectionalVal - minDirectionalVal;
			float reflectionC = reflectionA * minReflectionVal * minReflectionVal + minReflectionVal - minReflectionVal * minReflectionVal;

			totalCapAmbientOcclusion = (ambientA * totalCapAmbientOcclusion + ambientB) * totalCapAmbientOcclusion + ambientC;
			totalCapDirectionalOcclusion = (directionalA * totalCapDirectionalOcclusion + directionalB) * totalCapDirectionalOcclusion + directionalC;
			totalCapReflectionOcclusion = (reflectionA * totalCapReflectionOcclusion + reflectionB) * totalCapReflectionOcclusion + reflectionC;
		}
	}

	float totalVolAmbientOcclusion = 1.0f;
	float totalVolDirectionalOcclusion = 1.0f;
	float totalVolReflectionOcclusion = 1.0f;

	if (intensity > 0)
	{
		for (uint i = 0; i < g_numVolOccludersInBatch; ++i)
		{
			uint			vidx = g_occluderVolIdx[i];
			OccluderVol		vol = srt.pBufs->m_occluderVols[vidx];

			// don't apply occluders to parent object
			if (objectId == vol.m_objectId)
				continue;

			// Map volume position to 3d uv
			float4 hpos = float4(screenX, screenY, screenZ, 1.0);
			float4 volumePos = mul(hpos, vol.m_screenToAmbShadowVolume);
			uint   vtexIdx = vol.m_volumeTextureIdx;

			// Sample occlusion data from 3d texture
			float3 volumeUvs = saturate((volumePos.xyz / volumePos.w) * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5));
			float4 occlusionData = srt.pTexs->m_volumeTextures[vtexIdx].SampleLevel(srt.pSamplers->m_sLinearSampler, volumeUvs, 0);
			float3 occlusionDir = occlusionData.rgb * 2.0 - 1.0;
			float occlusionDirLen = length(occlusionDir);
			occlusionDir /= occlusionDirLen;
			float occlusionAngleSin = occlusionData.a * occlusionData.a;

			// convert occlusion dir to world space 
			float3 occlusionDirWS = mul(float4(occlusionDir, 0), vol.m_ambShadowVolumeToWorld).xyz;
			occlusionDirWS = normalize(occlusionDirWS);
			float3 occlusionDirVS = mul(srt.pConsts->m_worldToView, float4(occlusionDirWS, 0.0)).xyz;

			float halfBlockerAngleSin = (occlusionAngleSin  * 127.0f / 128.0f) + (0.5f / 128.0f);

			float ambientOcclusion = 1.0f;

			{
				float coneToAxisAngleCos = dot(occlusionDirVS, normalVS) * 0.5 + 0.5;
				float blockerToConeAxisCos = (coneToAxisAngleCos * 511.0f / 512.0f) + (0.5f / 512.0f);
				ambientOcclusion = srt.pTexs->m_occlusionLookup.SampleLevel(srt.pSamplers->m_sLinearSampler, float2(blockerToConeAxisCos, halfBlockerAngleSin), 0);
			}

			if (occlusionDirLen <= 0.1)
			{
				occlusionDirWS = dominantLightDir;
			}

			float directionalOcclusion = 1.0;

			{
				float coneToAxisAngleCos = dot(occlusionDirWS, dominantLightDir) * 0.5 + 0.5;
				float blockerToConeAxisCos = (coneToAxisAngleCos * 511.0f / 512.0f) + (0.5f / 512.0f);
				directionalOcclusion = srt.pTexs->m_occlusionLookup.SampleLevel(srt.pSamplers->m_sLinearSampler, float2(blockerToConeAxisCos, halfBlockerAngleSin), 0.0f);
			}

			float reflectionOcclusion = 1.0f;

			{
				float coneToAxisAngleCos = dot(occlusionDirVS, reflectVS) * 0.5 + 0.5;
				float blockerToConeAxisCos = (coneToAxisAngleCos * 511.0f / 512.0f) + (0.5f / 512.0f);
				reflectionOcclusion = srt.pTexs->m_occlusionLookup.SampleLevel(srt.pSamplers->m_sLinearSampler, float2(blockerToConeAxisCos, halfBlockerAngleSin), 0.0f);
			}

			totalVolAmbientOcclusion *= ambientOcclusion;
			totalVolDirectionalOcclusion *= directionalOcclusion;
			totalVolReflectionOcclusion *= reflectionOcclusion;
		}

		{
			float minAmbientVal = 0.20f;
			float ambientA = (minAmbientVal * minAmbientVal + 1.0f) / (minAmbientVal * minAmbientVal + 2.0f * minAmbientVal + 1.0f);
			float ambientB = 2.0f * ambientA * minAmbientVal - minAmbientVal;
			float ambientC = ambientA * minAmbientVal * minAmbientVal + minAmbientVal - minAmbientVal * minAmbientVal;

			float minDirectionalVal = 0.35f;
			float directionalA = (minDirectionalVal * minDirectionalVal + 1.0f) / (minDirectionalVal * minDirectionalVal + 2.0f * minDirectionalVal + 1.0f);
			float directionalB = 2.0f * directionalA * minDirectionalVal - minDirectionalVal;
			float directionalC = directionalA * minDirectionalVal * minDirectionalVal + minDirectionalVal - minDirectionalVal * minDirectionalVal;

			float minReflectionVal = 0.1f;
			float reflectionA = (minReflectionVal * minReflectionVal + 1.0f) / (minReflectionVal * minReflectionVal + 2.0f * minReflectionVal + 1.0f);
			float reflectionB = 2.0f * reflectionA * minDirectionalVal - minDirectionalVal;
			float reflectionC = reflectionA * minReflectionVal * minReflectionVal + minReflectionVal - minReflectionVal * minReflectionVal;

			totalVolAmbientOcclusion = (ambientA * totalVolAmbientOcclusion + ambientB) * totalVolAmbientOcclusion + ambientC;
			totalVolDirectionalOcclusion = (directionalA * totalVolDirectionalOcclusion + directionalB) * totalVolDirectionalOcclusion + directionalC;
			totalVolReflectionOcclusion = (reflectionA * totalVolReflectionOcclusion + reflectionB) * totalVolReflectionOcclusion + reflectionC;
		}
	}

	// output as appropriate here
	float3 finalAmbientShadow = float3(min(totalCapAmbientOcclusion, totalVolAmbientOcclusion), min(totalCapDirectionalOcclusion, totalVolDirectionalOcclusion), min(totalCapReflectionOcclusion, totalVolReflectionOcclusion));
	finalAmbientShadow = saturate(lerp(1.0f, finalAmbientShadow, srt.pConsts->m_scaleParams.xzw * intensity));

	srt.pTexs->m_resultsBuffer[screenPos.xy] = finalAmbientShadow;
}

struct AmbientShadowBlurTextures
{
	Texture2D<float> tex_depth;
	Texture2D<uint> tex_stencil;
	Texture2D<float4> tex_input;
	RWTexture2D<float3> tex_output;
};

struct AmbientShadowBlurSrt
{
	AmbientShadowBlurTextures* pTextures;
	float4 params;
};

static const int BLUR_SCALE = 4;

[AMBIENT_SHADOWS_THREAD_DEF]
void CS_AmbientShadowSmartBlurX(uint3 dispatchThreadId : SV_DispatchThreadID, AmbientShadowBlurSrt srt : S_SRT_DATA)
{
	float depthBufferZ = srt.pTextures->tex_depth.Load(int3(dispatchThreadId));
	uint stencilValue = srt.pTextures->tex_stencil.Load(int3(dispatchThreadId)) & 0xe0;
	float viewSpaceZ = srt.params.y / (depthBufferZ - srt.params.x);
	float3 totalValue = srt.pTextures->tex_input.Load(int3(dispatchThreadId)).rgb;
	float totalWeight = 1.0f;

	for (int i = 0; i < BLUR_SCALE; i++)
	{
		uint sampleStencilValue = srt.pTextures->tex_stencil.Load(int3(dispatchThreadId), int2(-BLUR_SCALE / 2 + i, 0)) & 0xe0;
		if (sampleStencilValue == stencilValue)
		{
			float sampleDepthBufferZ = srt.pTextures->tex_depth.Load(int3(dispatchThreadId), int2(-BLUR_SCALE / 2 + i, 0));
			float sampleViewSpaceZ = srt.params.y / (sampleDepthBufferZ - srt.params.x);
			float viewSpaceZDiff = abs(viewSpaceZ - sampleViewSpaceZ);
			float weight = 1.f - saturate(viewSpaceZDiff / srt.params.z);
			totalValue += srt.pTextures->tex_input.Load(int3(dispatchThreadId), int2(-BLUR_SCALE / 2 + i, 0)).rgb * weight;
			totalWeight += weight;
		}
	}

	float3 result = totalValue / totalWeight;
	srt.pTextures->tex_output[dispatchThreadId.xy] = result;
}

[AMBIENT_SHADOWS_THREAD_DEF]
void CS_AmbientShadowSmartBlurY(uint3 dispatchThreadId : SV_DispatchThreadID, AmbientShadowBlurSrt srt : S_SRT_DATA)
{
	float depthBufferZ = srt.pTextures->tex_depth.Load(int3(dispatchThreadId));
	uint stencilValue = srt.pTextures->tex_stencil.Load(int3(dispatchThreadId)) & 0xe0;
	float viewSpaceZ = srt.params.y / (depthBufferZ - srt.params.x);
	float3 totalValue = srt.pTextures->tex_input.Load(int3(dispatchThreadId)).rgb;
	float totalWeight = 1.f;

	for (int i = 0; i < BLUR_SCALE; i++)
	{
		uint sampleStencilValue = srt.pTextures->tex_stencil.Load(int3(dispatchThreadId), int2(0, -BLUR_SCALE / 2 + i)) & 0xe0;
		if (sampleStencilValue == stencilValue)
		{
			float sampleDepthBufferZ = srt.pTextures->tex_depth.Load(int3(dispatchThreadId), int2(0, -BLUR_SCALE / 2 + i));
			float sampleViewSpaceZ = srt.params.y / (sampleDepthBufferZ - srt.params.x);
			float viewSpaceZDiff = abs(viewSpaceZ - sampleViewSpaceZ);
			float weight = 1.f - saturate(viewSpaceZDiff / srt.params.z);
			totalValue += srt.pTextures->tex_input.Load(int3(dispatchThreadId), int2(0, -BLUR_SCALE / 2 + i)).rgb * weight;
			totalWeight += weight;
		}
	}

	float3 result = totalValue / totalWeight;
	srt.pTextures->tex_output[dispatchThreadId.xy] = result;
}