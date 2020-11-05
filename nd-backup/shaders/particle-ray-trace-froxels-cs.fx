/*
* Copyright (c) 2014 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "packing.fxi"
#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "global-const-buffers.fxi"
#include "tile-util.fxi"
#include "atomics.fxi"
#include "particle-cs.fxi"
#include "tetra-shadows.fxi"

#define ND_PSSL 1
#define USE_EXPANDED_COMPRESSED_PROBE 1
#include "vol-probe-lighting-cs.fx"
#define PROBE_DATA_ALREADY_DEFINED
#include "particle-ray-trace-cs-defines.fxi"
#include "particle-ray-trace-cs-ps.fxi"



// 0.0625 vertical half screen

// up to down
/*
#define DEST_VAL_POSY float4(0, 0, 0, 0.2)

// down to up
#define DEST_VAL_NEGY float4(0, 0, 0, 0.0)

// accumulating left to right
#define DEST_VAL_POSX float4(0, 0, 0, 0.0)

// accumulating right to left
#define DEST_VAL_NEGX float4(0, 0, 0, 0.0)

// 1.0 / 32

// accumulating back to front
#define DEST_VAL_POSZ float4(0, 0, 0, 0)

// accumulating front to back
#define DEST_VAL_NEGZ float4(0, 0, 0, 0)
*/

#define DEST_VAL_NEGY destVal
#define DEST_VAL_POSY destVal
#define DEST_VAL_POSX destVal
#define DEST_VAL_NEGX destVal
#define DEST_VAL_POSZ destVal
#define DEST_VAL_NEGZ destVal


// the more already accumulated, the less we add

#define CONTRIB_FACTOR (1.0f - clamp(accumVal.a * 1.0, 0, 1))
#define FALLOFF_FACTOR 0.95
//#define CONTRIB_FACTOR 1.0
//#define FALLOFF_FACTOR 1.0

//#define MAX_CONTRIB (1.0 - accumVal.a) * 0.5
#define MAX_CONTRIB 100.0

// if enable thse, need to also enable kCsRayTraceParticlesVerticalBlurFroxels on code side
#define DO_BLUR 0
#define DO_BLUR_Z_ONLY 0

//#define BLUR_CENTER 0.8
#define BLUR_CENTER 0.5

#define BLUR_SIDE (0.5 * (1.0 - BLUR_CENTER))



uint GetCascadedShadowLevelSimple(float depthVS, float4 cascadedLevelDist[2])
{
	float4 cmpResult0 = depthVS > cascadedLevelDist[0].xyzw;
	float4 cmpResult1 = depthVS > cascadedLevelDist[1].xyzw;

	return (uint)dot(cmpResult0, 1.0) + (uint)dot(cmpResult1, 1.0);
}

#if !FROXELS_DYNAMIC_RES

[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsLightFroxelsAndPotentialHBlur (const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
}


[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsLastFroxelFixup(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 pos = groupPos + groupThreadId.xy;

	

	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);

	float4 accumVal = 0; // pSrt->m_destFogFroxels[froxelCoord];
	float densityAdd = 0;
	float densityAddTotal = 3.0;
	float prevBrightness = 0;
	float brightnessRunningAverage = 0;
	float brightnessAccum = 0;

	// we alway copy the value of the last slice from preceding slice

	// note we will copy last slice lighting into lastSlice + 1, because when we will sample for pixels within the last slice
	// it will interpolate beween last slice and last slice + 1

	int lastSlice = min(62, uint(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x));
	// to make the loghting happen always:
	//lastSlice = 62;

	// need to copy the value from last froxel instersecting geometry to next one
	//pSrt->m_destFogFroxels[uint3(pos, lastSlice + 1)] = pSrt->m_destFogFroxels[uint3(pos, lastSlice)];


	// debug
	int lastSliceInEarlyPassPlusOneDebug = pSrt->m_srcVolumetricsReprojectedScreenSpaceInfo[groupId].x+1;
	//pSrt->m_destFogFroxels[uint3(pos, lastSliceInEarlyPassDebug)].xyzw = float4(1.0, 0, 0, 1.0);

}
#endif

#define GROUP_SIZE_X 8
#define GROUP_SIZE_Y 8

#define NUM_GROUPS_IN_BIG_TILE_X 8
#define NUM_GROUPS_IN_BIG_TILE_Y 8
#define NUM_GROUPS_IN_BIG_TILE (NUM_GROUPS_IN_BIG_TILE_X * NUM_GROUPS_IN_BIG_TILE_Y)

// This always runs, no matter early pass enabled or not

[NUM_THREADS(GROUP_SIZE_X, GROUP_SIZE_Y, 1)] //
void CS_VolumetricsClassifyFroxels(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	int StoredMaxZ = 0;
	int StoredMinZ = 64;
	float StoredMinLightZ = 0;
	float StoredMaxLightZ = 0;

	uint2 startGroupId = uint2(NUM_GROUPS_IN_BIG_TILE_X, NUM_GROUPS_IN_BIG_TILE_Y) * (groupId.xy);

	for (uint iGroup = 0; iGroup < NUM_GROUPS_IN_BIG_TILE; ++iGroup)
	{
		uint xOffset = iGroup % NUM_GROUPS_IN_BIG_TILE_X;
		uint yOffset = iGroup / NUM_GROUPS_IN_BIG_TILE_X;
		
		uint2 iGroupId = (startGroupId + uint2(xOffset, yOffset));
		uint2 groupPos = (startGroupId + uint2(xOffset, yOffset)) * uint2(GROUP_SIZE_X, GROUP_SIZE_Y);

		#if FROXELS_NO_EARLY_PASS
			int lastSliceInEarlyPassPlusOne = 0;
		#else
			int lastSliceInEarlyPassPlusOne = pSrt->m_srcVolumetricsReprojectedScreenSpaceInfo[startGroupId + uint2(xOffset, yOffset)].x;
		#endif
		uint2 pos = groupPos + groupThreadId.xy;


		uint numTilesInWidth = (pSrt->m_numFroxelsXY.x + 7) / 8;
		uint tileFlatIndex = mul24(numTilesInWidth, iGroupId.y) + iGroupId.x;
		uint froxelFlatIndex = pSrt->m_numFroxelsXY.x * pos.y + pos.x;

		uint intersectionMask = pSrt->m_volumetricSSLightsTileBufferRO[tileFlatIndex].m_intersectionMask;
		VolumetrciSSLightFroxelData lightFroxelData = pSrt->m_volumetricSSLightsFroxelBufferRO[froxelFlatIndex];

		uint numLights = __s_bcnt1_i32_b32(intersectionMask);
		uint froxelLightIndex = 0;

		float minLightZ =  10000000.0f;
		float maxLightZ = -10000000.0f;

		//while (intersectionMask)
		//{
		//	int lightIndex = __s_ff1_i32_b32(intersectionMask);
		//
		//	intersectionMask = __s_andn2_b32(intersectionMask, __s_lshl_b32(1, lightIndex));
		//	VolumetrciSSLight singleLightData = lightFroxelData.m_lights[froxelLightIndex];
		//	froxelLightIndex += 1;
		//
		//
		//	#if VOLUMETRICS_COMPRESS_SS_LIGHTS
		//		float4 m_data0 = float4(f16tof32(asuint(singleLightData.m_data0.x)), f16tof32(asuint(singleLightData.m_data0.x) >> 16), f16tof32(asuint(singleLightData.m_data0.y)), f16tof32(asuint(singleLightData.m_data0.y) >> 16));
		//		float4 m_data1 = float4(f16tof32(asuint(singleLightData.m_data0.z)), f16tof32(asuint(singleLightData.m_data0.z) >> 16), f16tof32(asuint(singleLightData.m_data0.w)), f16tof32(asuint(singleLightData.m_data0.w) >> 16));
		//		float4 m_data2 = float4(f16tof32(asuint(singleLightData.m_data1.x)), f16tof32(asuint(singleLightData.m_data1.x) >> 16), f16tof32(asuint(singleLightData.m_data1.y)), f16tof32(asuint(singleLightData.m_data1.y) >> 16));
		//		float4 m_data3 = float4(f16tof32(asuint(singleLightData.m_data1.z)), f16tof32(asuint(singleLightData.m_data1.z) >> 16), f16tof32(asuint(singleLightData.m_data1.w)), f16tof32(asuint(singleLightData.m_data1.w) >> 16));
		//	#else
		//		float4 m_data0 = singleLightData.m_data0;
		//		float4 m_data1 = singleLightData.m_data1;
		//		float4 m_data2 = singleLightData.m_data2;
		//		float4 m_data3 = singleLightData.m_data3;
		//	#endif
		//
		//	float2 intersectionTimes = m_data0.xy;
		//
		//	//if (intersectionTimes.y > 0.00001)
		//	{
		//		minLightZ = min(minLightZ, intersectionTimes.x);
		//		maxLightZ = max(maxLightZ, intersectionTimes.y);
		//	}
		//}

		// now reduce the min and max for the whole group
		//{
		//	float maxZ_01 = max(maxLightZ, LaneSwizzle(maxLightZ, 0x1f, 0x00, 0x01));
		//	float maxZ_02 = max(maxZ_01, LaneSwizzle(maxZ_01, 0x1f, 0x00, 0x02));
		//	float maxZ_04 = max(maxZ_02, LaneSwizzle(maxZ_02, 0x1f, 0x00, 0x04));
		//	float maxZ_08 = max(maxZ_04, LaneSwizzle(maxZ_04, 0x1f, 0x00, 0x08));
		//	float maxZ_10 = max(maxZ_08, LaneSwizzle(maxZ_08, 0x1f, 0x00, 0x10));
		//	float maxZ = max(ReadLane(maxZ_10, 0x00), ReadLane(maxZ_10, 0x20));
		//
		//	WriteLane(StoredMaxLightZ, maxZ, iGroup);
		//}
		//
		//{
		//	float minZ_01 = min(minLightZ, LaneSwizzle(minLightZ, 0x1f, 0x00, 0x01));
		//	float minZ_02 = min(minZ_01, LaneSwizzle(minZ_01, 0x1f, 0x00, 0x02));
		//	float minZ_04 = min(minZ_02, LaneSwizzle(minZ_02, 0x1f, 0x00, 0x04));
		//	float minZ_08 = min(minZ_04, LaneSwizzle(minZ_04, 0x1f, 0x00, 0x08));
		//	float minZ_10 = min(minZ_08, LaneSwizzle(minZ_08, 0x1f, 0x00, 0x10));
		//	float minZ = min(ReadLane(minZ_10, 0x00), ReadLane(minZ_10, 0x20));
		//
		//	WriteLane(StoredMinLightZ, minZ, iGroup);
		//}

		// simple version just add all froxels to list

		uint3 froxelCoord = uint3(pos, 0);


		uint lastFroxelTouchedByGeo = uint(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x);
		int lastSliceToEverBeSampled = min(63, lastFroxelTouchedByGeo + 1);

		//lastSliceToEverBeSampled = 63;

		// now find min slice to ever be sampled accross all threads

		{
			int maxZ_01 = max(lastSliceToEverBeSampled, LaneSwizzle(lastSliceToEverBeSampled, 0x1f, 0x00, 0x01));
			int maxZ_02 = max(maxZ_01, LaneSwizzle(maxZ_01, 0x1f, 0x00, 0x02));
			int maxZ_04 = max(maxZ_02, LaneSwizzle(maxZ_02, 0x1f, 0x00, 0x04));
			int maxZ_08 = max(maxZ_04, LaneSwizzle(maxZ_04, 0x1f, 0x00, 0x08));
			int maxZ_10 = max(maxZ_08, LaneSwizzle(maxZ_08, 0x1f, 0x00, 0x10));
			int maxZ = max(ReadLane(maxZ_10, 0x00), ReadLane(maxZ_10, 0x20));

			// store maxZ into each thread

			WriteLane(StoredMaxZ, maxZ, iGroup);
		}

		{
			int maxZ_01 = min(lastSliceToEverBeSampled, LaneSwizzle(lastSliceToEverBeSampled, 0x1f, 0x00, 0x01));
			int maxZ_02 = min(maxZ_01, LaneSwizzle(maxZ_01, 0x1f, 0x00, 0x02));
			int maxZ_04 = min(maxZ_02, LaneSwizzle(maxZ_02, 0x1f, 0x00, 0x04));
			int maxZ_08 = min(maxZ_04, LaneSwizzle(maxZ_04, 0x1f, 0x00, 0x08));
			int maxZ_10 = min(maxZ_08, LaneSwizzle(maxZ_08, 0x1f, 0x00, 0x10));
			int minZ = min(ReadLane(maxZ_10, 0x00), ReadLane(maxZ_10, 0x20));

			// store maxZ into each thread

			WriteLane(StoredMinZ, minZ, iGroup);
		}


		// lastSliceInEarlyPass stores how much we processed in early pass
		// if the real value is bigger than that, then we will process in late pass
		// if the real value is smaller than predicted early pass then we don't need to do anything
		// but what we can do is modify what "real" value form this frame is
		// that would allow us to use the excessive value next frame if it is revelaed

		//if (lastSliceInEarlyPass - 1 > StoredMaxZ)
		//{
		//	pSrt->m_destVolumetricsScreenSpaceInfo[froxelCoord.xy].x = lastSliceInEarlyPass - 1; // stores last froxel slice touched by geo, which is 1 less than last slice processed
		//	pSrt->m_destVolumetricsScreenSpaceInfoOrig[froxelCoord.xy].x = lastSliceInEarlyPass - 1;
		//}
	}


	// we want to write group in order of x-y then z. Slice by slice.
	// each thread stores the max froxel per each group

	uint myIndex = groupThreadId.y * GROUP_SIZE_X + groupThreadId.x;

	// at this point, each thread stores whether at this slice, the corresponding group is before the max depth for that group
	// basically each thread is responsile for its 8x8 group
	// we can now do logic in here that can per group 

	uint xOffset = myIndex % NUM_GROUPS_IN_BIG_TILE_X;
	uint yOffset = myIndex / NUM_GROUPS_IN_BIG_TILE_X;

	uint2 groupPos = (startGroupId + uint2(xOffset, yOffset)) * uint2(GROUP_SIZE_X, GROUP_SIZE_Y);

	uint2 pos = groupPos + uint2(4,4); // center of the group

	// convert to world space for shader LODing


	float2 pixelPosNormNoJit = float2(pos.x + 0.5, pos.y + 0.5) * float2(pSrt->m_numFroxelsXInv, pSrt->m_numFroxelsXInv * 16.0 / 9.0);

	float ndcX = pixelPosNormNoJit.x * 2.0f - 1.0f;
	float ndcY = pixelPosNormNoJit.y * 2.0f - 1.0f;
	ndcY = -ndcY; // so we flip Y

#if FROXELS_NO_EARLY_PASS
	int lastSliceInEarlyPassPlusOne = 0;
#else
	int lastSliceInEarlyPassPlusOne = pSrt->m_srcVolumetricsReprojectedScreenSpaceInfo[startGroupId + uint2(xOffset, yOffset)].x;
#endif

	uint2 iGroupId = (startGroupId + uint2(xOffset, yOffset));
	
	uint numTilesInWidth = (pSrt->m_numFroxelsXY.x + 7) / 8;
	uint numTilesInHeight = (pSrt->m_numFroxelsXY.y + 7) / 8;

	if (iGroupId.x >= numTilesInWidth || iGroupId.y >= numTilesInHeight)
		return;

	uint tileFlatIndex = mul24(numTilesInWidth, iGroupId.y) + iGroupId.x;

	ExtraVolumetrciSSLightTileData extraTileInfo = pSrt->m_volumetricSSLightsTileBufferExtraRO[tileFlatIndex];

	StoredMinLightZ = extraTileInfo.m_minZ;
	StoredMaxLightZ = extraTileInfo.m_maxZ;

	int groupLightBits = pSrt->m_srcVolumetricsReprojectedScreenSpaceInfo[startGroupId + uint2(xOffset, yOffset)].y; // this always has groupLightBits from early pass. if early pass is enabled this also has last predicted froxel depth that was put into early list

	int groupHasLights = groupLightBits & 1;
	int groupHasShadowedLights = groupLightBits & 2;

	ulong lightOccupancyBits = pSrt->m_volumetricSSLightsTileBufferExtraRO[tileFlatIndex].m_lightOccupancyBits;

	for (uint iSlice = 0; iSlice < 64; ++iSlice)
	{
		float sliceCoord = iSlice;

		float viewZ = FroxelZSliceToCameraDistExp(sliceCoord, pSrt->m_fogGridOffset);

		float ndcZ = GetNdcDepthSafe(viewZ, pSrt->m_depthParams);

		float depthVS = viewZ;
	
		float3 positionVS = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVS;

		float4 posWsH = mul(float4(positionVS, 1), pSrt->m_mVInv);
		float3 posWs = posWsH.xyz;// / posWsH.w;

		float sliceCoord0 = iSlice;
		float sliceCoord1 = iSlice + 1.0;

		float viewZ0 = FroxelZSliceToCameraDistExp(sliceCoord0, pSrt->m_fogGridOffset);
		float viewZ1 = FroxelZSliceToCameraDistExp(sliceCoord1, pSrt->m_fogGridOffset);

		float3 posVs0 = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * viewZ0;
		float3 posVs1 = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * viewZ1;

		float viewDist0 = length(posVs0);
		float viewDist1 = length(posVs1);

		float startInVolume = max(viewDist0, StoredMinLightZ);
		float endInVolume = min(viewDist1, StoredMaxLightZ);

		float lengthInVolume = clamp(endInVolume - startInVolume, 0, viewDist1 - viewDist0);

		groupHasLights = lengthInVolume > 0.0f;
		
		groupHasLights = (lightOccupancyBits & __v_lshl_b64(1, iSlice)) > 0;

		int numFroxelGroupsX = (pSrt->m_numFroxelsXY.x + 7) / 8;
		int numFroxelGroupsY = (pSrt->m_numFroxelsXY.y + 7) / 8;

		//pSrt->m_cascadedLevelDistFloats[kMaxNumCascaded - 1]

		int sliceAtCascadeEnd = int(CameraLinearDepthToFroxelZSliceExp(pSrt->m_lastCascadeDist, 0)) + 1.0f;

		

		uint tileSliceIntersectionMask = pSrt->m_volumetricSSLightsTileBufferRO[tileFlatIndex + mul24(pSrt->m_numFroxelsGroupsInSlice /* numFroxelGroupsX * numFroxelGroupsY */, uint(iSlice + 1))].m_intersectionMask;
		uint tileSliceLightTypeMask = pSrt->m_volumetricSSLightsTileBufferRO[tileFlatIndex + mul24(pSrt->m_numFroxelsGroupsInSlice /* numFroxelGroupsX * numFroxelGroupsY */, uint(iSlice + 1))].m_lightTypeMask;

		groupHasLights = tileSliceIntersectionMask != 0;

		int groupHasPointLightsOnly = (tileSliceLightTypeMask & FROXEL_LIGHT_MASK_ALL_SHAPES) == FROXEL_LIGHT_MASK_POINT_LIGHT;
		int groupHasSpotLightsOnly = (tileSliceLightTypeMask & FROXEL_LIGHT_MASK_ALL_SHAPES) == FROXEL_LIGHT_MASK_SPOT_LIGHT;

		int groupHasPointSpotLightsOnly = (tileSliceLightTypeMask & FROXEL_LIGHT_MASK_ALL_SHAPES) == (FROXEL_LIGHT_MASK_SPOT_LIGHT | FROXEL_LIGHT_MASK_POINT_LIGHT);

		int groupHasShadows = (tileSliceLightTypeMask & FROXEL_SHADOW_MASK_ALL_TYPES) != 0;

		int groupHasDepthVarianceDarkening = (tileSliceLightTypeMask & FROXEL_LIGHT_MASK_USE_DEPTH_VARIANCE_DARKENING);
		int groupHasExpandedFroxelSampling = (tileSliceLightTypeMask & FROXEL_LIGHT_MASK_USE_EXPANDED_FROXEL_SAMPLING);
		int groupHasDepthBufferVarianceshadowing = (tileSliceLightTypeMask & FROXEL_LIGHT_MASK_USE_DEPTH_BUFFER_VARIANCE_SHADOWING);

		int needsSliceCheck = iSlice > StoredMinZ ? 1 : 0;
		//groupHasLights = groupHasPointLightsOnly;
		//groupHasLights = groupHasSpotLightsOnly;

		// if there was no early pass, lastSliceInEarlyPassPlusOne is just 0
		bool before = iSlice <= StoredMaxZ && iSlice >= lastSliceInEarlyPassPlusOne; // && iSlice > 0; 

		bool noSky = posWs.y < 10.0f;

		bool noSun = iSlice > sliceAtCascadeEnd;

		if (before)
		{
			if (noSun)
			{
				uint index = NdAtomicIncrement(pSrt->m_gdsOffset_1);
				uint mask = (needsSliceCheck << 30) | ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
				pSrt->m_destDispatchList1[index].m_pos = mask;
			}
			else
			{
				// list of froxels with sun
				{
					uint index = NdAtomicIncrement(pSrt->m_gdsOffset_0);
					uint mask = (needsSliceCheck << 30) | ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
					pSrt->m_destDispatchList0[index].m_pos = mask;
				}
			}

			bool needCatchAll = groupHasDepthVarianceDarkening || groupHasExpandedFroxelSampling || groupHasDepthBufferVarianceshadowing;
			// have lists for areas with and without lights
			if (groupHasLights)
			{
				// if subdivide  more..
				if (groupHasSpotLightsOnly && !needCatchAll)
				{
					if (groupHasShadows)
					{
						uint index = NdAtomicIncrement(pSrt->m_gdsOffset_6);
						uint mask = (needsSliceCheck << 30) | ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
						pSrt->m_destDispatchList3[index].m_pos = mask;
					}
					else
					{
						uint index = NdAtomicIncrement(pSrt->m_gdsOffset_9);
						uint mask = (needsSliceCheck << 30) | ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
						pSrt->m_destDispatchList6[index].m_pos = mask;
					}
				}
				else if (groupHasPointLightsOnly && !needCatchAll)
				{
					if (groupHasShadows)
					{
						uint index = NdAtomicIncrement(pSrt->m_gdsOffset_8);
						uint mask = (needsSliceCheck << 30) | ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
						pSrt->m_destDispatchList5[index].m_pos = mask;
					}
					else
					{
						uint index = NdAtomicIncrement(pSrt->m_gdsOffset_10);
						uint mask = (needsSliceCheck << 30) | ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
						pSrt->m_destDispatchList7_PointOnlyNoShadow[index].m_pos = mask;
					}
				}
				else if (groupHasPointSpotLightsOnly && !groupHasShadows && !needCatchAll)
				{
					uint index = NdAtomicIncrement(pSrt->m_gdsOffset_11);
					uint mask = (needsSliceCheck << 30) | ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
					pSrt->m_destDispatchList8_PointSpotNoShadow[index].m_pos = mask;
				}
				else
				{
					// catch all

					uint index = NdAtomicIncrement(pSrt->m_gdsOffset_7);
					uint mask = (needsSliceCheck << 30) | ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
					pSrt->m_destDispatchList4[index].m_pos = mask;
				}

				
			}
			else
			{
				// list with no lights
				uint index = NdAtomicIncrement(pSrt->m_gdsOffset_4);
				uint mask = (needsSliceCheck << 30) | ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
				pSrt->m_destDispatchList2[index].m_pos = mask;
			}
			
		}
	}

	/*
	// now activate only the threads that are before the lastSlice
	uint mySlice = groupThreadId.y * GROUP_SIZE_X + groupThreadId.x;

	
	for (uint iGroup = 0; iGroup < NUM_GROUPS_IN_BIG_TILE; ++iGroup)
	{
		uint xOffset = iGroup % NUM_GROUPS_IN_BIG_TILE_X;
		uint yOffset = iGroup / NUM_GROUPS_IN_BIG_TILE_X;

		int maxZ = ReadLane(StoredMaxZ, iGroup);

		// if all froxels are behind the last sampled froxel we don't need to do anything
		bool before = mySlice <= maxZ && mySlice > 0;


		if (before)
		{
			uint index = NdAtomicIncrement(pSrt->m_gdsOffset_0);
			uint mask = ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (mySlice);
			pSrt->m_destDispatchList0[index].m_pos = mask;
		}
	}
	*/

	
#if 0


	ulong beforeMask = __v_cmp_eq_u32(before, true);

	if (beforeMask == 0)
	{
		return;
	}


	uint groupType = 0;

	bool checkEndCondition = true;

	ulong sentinel_mask = __v_cmp_eq_u32(checkEndCondition, false);

	groupType = sentinel_mask == 0 ? 1 : 0;

	groupType = 0;

	uint index;
	
	if (__v_cndmask_b32(0, 1, 0x0000000000000001))
	{
		// increment by 1 from 0th thread
		index = NdAtomicIncrement(pSrt->m_gdsOffset_0);
	}

	index = ReadLane(index, 0);

	// encode position
	uint mask = (groupId.x << 20) | (groupId.y << 10) | (z);

	pSrt->m_destDispatchList0[index].m_pos = mask;
#endif
	/*
	pSrt->m_destFogFroxels[uint3(pos, 0)] = float4(0, 0, 0, 0);


	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);

	float4 accumVal = 0; // pSrt->m_destFogFroxels[froxelCoord];
	float densityAdd = 0;
	float densityAddTotal = 3.0;
	float prevBrightness = 0;
	float brightnessRunningAverage = 0;
	float brightnessAccum = 0;

	// we alway copy the value of the last slice from preceding slice

	// note we will copy last slice lighting into lastSlice + 1, because when we will sample for pixels within the last slice
	// it will interpolate beween last slice and last slice + 1

	int lastSlice = min(62, uint(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x));
	// to make the loghting happen always:
	//lastSlice = 62;

	// need to copy the value from last froxel instersecting geometry to next one
	pSrt->m_destFogFroxels[uint3(pos, lastSlice + 1)] = pSrt->m_destFogFroxels[uint3(pos, lastSlice)];
	*/
}


[NUM_THREADS(GROUP_SIZE_X, GROUP_SIZE_Y, 1)] //
void CS_VolumetricsClassifyEarlyFroxels(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	int StoredMinZ = -1;

	uint2 startGroupId = uint2(NUM_GROUPS_IN_BIG_TILE_X, NUM_GROUPS_IN_BIG_TILE_Y) * (groupId.xy);

	for (uint iGroup = 0; iGroup < NUM_GROUPS_IN_BIG_TILE; ++iGroup)
	{
		uint xOffset = iGroup % NUM_GROUPS_IN_BIG_TILE_X;
		uint yOffset = iGroup / NUM_GROUPS_IN_BIG_TILE_X;


		uint2 groupPos = (startGroupId + uint2(xOffset, yOffset)) * uint2(GROUP_SIZE_X, GROUP_SIZE_Y);

		uint2 pos = groupPos + groupThreadId.xy;
		uint z = groupId.z + 1;
		// simple version just add all froxels to list

		uint3 froxelCoord = uint3(pos, 0);


		
		
		// calculate the last froxel position in world space

		float2 pixelPosNorm = pos * float2(pSrt->m_numFroxelsXInv, pSrt->m_numFroxelsXInv * 16.0 / 9.0);

		float ndcX = pixelPosNorm.x * 2.0f - 1.0f;
		float ndcY = pixelPosNorm.y * 2.0f - 1.0f;
		ndcY = -ndcY; // so we flip Y


		float viewZ = FroxelZSliceToCameraDistExp(63.5, pSrt->m_fogGridOffset);

		float ndcZ = GetNdcDepthSafe(viewZ, pSrt->m_depthParams);

		float depthVS = viewZ;

		float3 positionVS = float3(((pixelPosNorm)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVS;

		float4 posWsH = mul(float4(positionVS, 1), pSrt->m_mVInv);
		float3 posWs = posWsH.xyz;// / posWsH.w;


		float4 prevPosHs = mul(float4(posWs, 1), pSrt->m_mLastFrameVP);
		float3 prevPosNdc = prevPosHs.xyz / prevPosHs.w;

		float3 prevPosUvw;
		prevPosUvw.xy = prevPosNdc.xy * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);
		prevPosUvw.y = 1.0f - prevPosUvw.y;
		
		int2 froxelScreenCoord = floor(pSrt->m_numFroxelsXY * float2(prevPosUvw.xy));
		int prevLastFroxelIntersectingGeo = pSrt->m_srcVolumetricsScreenSpaceInfoOrigPrev[froxelScreenCoord].r; // sampling out of bounds gives us 0

		
		  //lastSliceToEverBeSampled = 63;

		  // now find min slice to ever be sampled accross all threads

		int minZ_01 = min(prevLastFroxelIntersectingGeo, LaneSwizzle(prevLastFroxelIntersectingGeo, 0x1f, 0x00, 0x01));
		int minZ_02 = min(minZ_01, LaneSwizzle(minZ_01, 0x1f, 0x00, 0x02));
		int minZ_04 = min(minZ_02, LaneSwizzle(minZ_02, 0x1f, 0x00, 0x04));
		int minZ_08 = min(minZ_04, LaneSwizzle(minZ_04, 0x1f, 0x00, 0x08));
		int minZ_10 = min(minZ_08, LaneSwizzle(minZ_08, 0x1f, 0x00, 0x10));
		int minZ = min(ReadLane(minZ_10, 0x00), ReadLane(minZ_10, 0x20));

		// store maxZ into each thread




		//minZ = 32; // hard code to 32, basically first 32 layers for each group are visible. Todo: use reprojected depth

		WriteLane(StoredMinZ, minZ, iGroup);

	}


	// we want to write group in order of x-y then z. Slice by slice.
	// each thread stores the max froxel per each group in StoredMaxZ

	uint myIndex = groupThreadId.y * GROUP_SIZE_X + groupThreadId.x;

	// at this point, each thread stores whether at this slice, the corresponding group is before the max depth for that group
	// basically each thread is responsile for its 8x8 group
	// we can now do logic in here that is per group 

	// we will dispatch groups up to and including <= StoredMaxZ

	uint xOffset = myIndex % NUM_GROUPS_IN_BIG_TILE_X;
	uint yOffset = myIndex / NUM_GROUPS_IN_BIG_TILE_X;

	// actually we will just store one value per whole group
	
#if FROXELS_JUST_REPROJ_INFO
	// in this case we don't care about this value since we don't do early pass
	// actually we don't do this anymore and instead force read 0 when we read this texture
	//StoredMinZ = -1;
#endif
	
	uint2 groupPos = (startGroupId + uint2(xOffset, yOffset)) * uint2(GROUP_SIZE_X, GROUP_SIZE_Y);

	uint2 pos00 = groupPos; // top left of the group. this position is in froxels
	uint2 pos11 = groupPos + uint2(GROUP_SIZE_X, GROUP_SIZE_Y); // top left of the group. this position is in froxels


	uint2 posCenter = groupPos + uint2(GROUP_SIZE_X/2, GROUP_SIZE_Y/2); // center of the group. this position is in froxels

										// convert to world space for shader LODing


	float2 pixelPosNorm = float2(posCenter.x, posCenter.y) * float2(pSrt->m_numFroxelsXInv, pSrt->m_numFroxelsXInv * 16.0 / 9.0);

	float ndcX = pixelPosNorm.x * 2.0f - 1.0f;
	float ndcY = pixelPosNorm.y * 2.0f - 1.0f;
	ndcY = -ndcY; // so we flip Y

	uint2 pixelPos00 = pos00 * uint2(pSrt->m_froxelSizeU, pSrt->m_froxelSizeU);
	uint2 pixelPos11 = pos11 * uint2(pSrt->m_froxelSizeU, pSrt->m_froxelSizeU) - uint2(1, 1);


	// now check if this group intersects with any lights
	bool haveAnyLights = false;
	bool haveShadowLights = false;

	int groupFlags = 0;

	if (pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_numLights)
	{
		uint tileX0 = pixelPos00.x / TILE_SIZE;
		uint tileX1 = pixelPos11.x / TILE_SIZE;
		uint tileY0 = pixelPos00.y / TILE_SIZE;
		uint tileY1 = pixelPos11.y / TILE_SIZE;


		uint combinedList = 0;
		
		for (uint tileY = tileY0; tileY <= tileY1; ++tileY)
		{
			for (uint tileX = tileX0; tileX <= tileX1; ++tileX)
			{
				uint tileIndex = mul24(tileY, uint(pSrt->m_screenWU) / TILE_SIZE) + tileX;

				uint numLights = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_numLightsPerTileBufferRO[tileIndex];

				if (numLights)
				{
					haveAnyLights = true;
				}

				for (uint i = 0; i < numLights; ++i)
				{
					uint lightIndex = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lightsPerTileBufferRO[tileIndex*MAX_LIGHTS_PER_TILE + i];
					const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[lightIndex];

					if (lightDesc.m_localShadowTextureIdx != kInvalidLocalShadowTextureIndex)
					{
						// have a shadow
						haveShadowLights = true;
					}
				}
			}
		}
	}
	
	uint groupHasLights = haveAnyLights;

	if (haveAnyLights)
	{
		groupFlags = groupFlags | 1;
	}

	if (haveShadowLights)
	{
		groupFlags = groupFlags | 2;
	}

// note we have a case where we can't always have the texture store reprojected depth AND have early pass ith run time lights
// the issue is that with run time lights the code uses this value to know how far froxels were processed in early pass
// so it has to be 0 when early pass is running and we have shadowed lights
// to fix this, we just need to check that the tile has shadowed lights in later classify pass and from that deduce that it should be 0

#if !FROXELS_JUST_REPROJ_INFO
	if (haveShadowLights)
	{
		// we make early pass not porcess any of these group. late pass classify will add them to appropriate list.
		// we could add them to late list ourselves, but this way we can keep the reprojected texture store the depth of only preprocessed groups
		StoredMinZ = -1;
	}
#endif

	pSrt->m_destVolumetricsReprojectedScreenSpaceInfo[(startGroupId + uint2(xOffset, yOffset))] = uint2(StoredMinZ + 1, groupFlags); // we use this later in classifications to generate leftover groups as well for knowing whether screenspace area intersects light sources

#if !FROXELS_JUST_REPROJ_INFO
	for (uint iSlice = 0; iSlice < 64; ++iSlice)
	{
		float sliceCoord = iSlice;

		float viewZ = FroxelZSliceToCameraDistExp(sliceCoord, pSrt->m_fogGridOffset);

		float ndcZ = GetNdcDepthSafe(viewZ, pSrt->m_depthParams);

		float depthVS = viewZ;

		float3 positionVS = float3(((pixelPosNorm)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVS;

		float4 posWsH = mul(float4(positionVS, 1), pSrt->m_mVInv);
		float3 posWs = posWsH.xyz;// / posWsH.w;


		bool before = iSlice <= StoredMinZ && iSlice >= 0;

		bool noSky = posWs.y < 10.0f;

		bool noSun = iSlice > 48;

		if (before)
		{
			if (noSun)
			{
				uint index = NdAtomicIncrement(pSrt->m_gdsOffset_3);
				uint mask = ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
				pSrt->m_destDispatchListEarly1[index].m_pos = mask;
			}
			else
			{
				// so these groups were guessed to be visible this frame and we could try process them early. however some might not be possible to process early
				// because we don't have enough data just yet. specifically if we have runtime lights here and don't have runtime shadows yet

				// TODO: we need to do much more processing with lights. we can do early processing on groups that don't touch lights or groups that have lights with no shadows
				bool canProcessEarly = !haveShadowLights; // pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_numLights == 0;

				if (canProcessEarly)
				{
					if (groupHasLights)
					{
						uint index = NdAtomicIncrement(pSrt->m_gdsOffset_2);
						uint mask = ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
						pSrt->m_destDispatchListEarly0[index].m_pos = mask;
					}
					else
					{
						uint index = NdAtomicIncrement(pSrt->m_gdsOffset_5);
						uint mask = ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
						pSrt->m_destDispatchListEarly2_NoLights[index].m_pos = mask;
					}

				}
				else
				{
					// we add it to the late pass processing. late pass classification will add even more based on visible but not guessed
					uint index = NdAtomicIncrement(pSrt->m_gdsOffset_0);
					uint mask = ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (iSlice);
					pSrt->m_destDispatchList0[index].m_pos = mask;
				}
			}
		}
	}
#endif

	/*
	// now activate only the threads that are before the lastSlice
	uint mySlice = groupThreadId.y * GROUP_SIZE_X + groupThreadId.x;


	for (uint iGroup = 0; iGroup < NUM_GROUPS_IN_BIG_TILE; ++iGroup)
	{
	uint xOffset = iGroup % NUM_GROUPS_IN_BIG_TILE_X;
	uint yOffset = iGroup / NUM_GROUPS_IN_BIG_TILE_X;

	int maxZ = ReadLane(StoredMaxZ, iGroup);

	// if all froxels are behind the last sampled froxel we don't need to do anything
	bool before = mySlice <= maxZ && mySlice > 0;


	if (before)
	{
	uint index = NdAtomicIncrement(pSrt->m_gdsOffset_0);
	uint mask = ((startGroupId.x + xOffset) << 20) | ((startGroupId.y + yOffset) << 10) | (mySlice);
	pSrt->m_destDispatchList0[index].m_pos = mask;
	}
	}
	*/


#if 0


	ulong beforeMask = __v_cmp_eq_u32(before, true);

	if (beforeMask == 0)
	{
		return;
	}


	uint groupType = 0;

	bool checkEndCondition = true;

	ulong sentinel_mask = __v_cmp_eq_u32(checkEndCondition, false);

	groupType = sentinel_mask == 0 ? 1 : 0;

	groupType = 0;

	uint index;

	if (__v_cndmask_b32(0, 1, 0x0000000000000001))
	{
		// increment by 1 from 0th thread
		index = NdAtomicIncrement(pSrt->m_gdsOffset_0);
	}

	index = ReadLane(index, 0);

	// encode position
	uint mask = (groupId.x << 20) | (groupId.y << 10) | (z);

	pSrt->m_destDispatchList0[index].m_pos = mask;
#endif
	/*
	pSrt->m_destFogFroxels[uint3(pos, 0)] = float4(0, 0, 0, 0);


	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);

	float4 accumVal = 0; // pSrt->m_destFogFroxels[froxelCoord];
	float densityAdd = 0;
	float densityAddTotal = 3.0;
	float prevBrightness = 0;
	float brightnessRunningAverage = 0;
	float brightnessAccum = 0;

	// we alway copy the value of the last slice from preceding slice

	// note we will copy last slice lighting into lastSlice + 1, because when we will sample for pixels within the last slice
	// it will interpolate beween last slice and last slice + 1

	int lastSlice = min(62, uint(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x));
	// to make the loghting happen always:
	//lastSlice = 62;

	// need to copy the value from last froxel instersecting geometry to next one
	pSrt->m_destFogFroxels[uint3(pos, lastSlice + 1)] = pSrt->m_destFogFroxels[uint3(pos, lastSlice)];
	*/
}

void DoShadowCacheLighting(StructuredBuffer<ShadowCacheAngleSlice> shadowSlicesRO,

	#if FROXELS_DEBUG_IN_SHADOW_CACHE_TEXTURE
		Texture2D<float4>		volumetricsShadowCacheRO,
	#else
		Texture2D<float2>		volumetricsShadowCacheRO,
	#endif
	SamplerState linearSampler,
	SamplerState pointSampler,

	float3 samplingPlaneZVs,
	float3 samplingPlaneXVs,
	float3 lightDirectionVs,
	float3 froxelLineVs,
	float2 _cosSinRefAngle,
	float startAngle,
	float angleStepInv,
	float numAngleSlicesMinus1,
	uint lookingAtLight,
	float3 camPos,

	// same for different frames
	float shadowMaxDistAlongLightDir,
	float nearBrightness,
	float nearBrightnessAdd,
	float nearBrightnessDistSqr,
	float expShadowMapConstant,
	float expShadowMapMultiplier,
	float2 scatterParams,

	float3 positionVS,
	float3 posWs,
	float inScatterFactor,

// these are not used, but were useful before

	inout float densityAdd,

	// results
	inout float brightness
)
{
	{
		//densityAdd = densityAdd / (1.0 + iSlice / 4.0) ;
		densityAdd = 0;// max(densityAdd - 1, 0);


		float3 N = normalize(positionVS);

		#if OPTIMIZE_SUN_LIGHT
		N = froxelLineVs;
		#endif

		float2 cosSinRefAngle = normalize(float2(dot(N, samplingPlaneZVs), dot(N, samplingPlaneXVs)));
		#if OPTIMIZE_SUN_LIGHT
		cosSinRefAngle =_cosSinRefAngle;
		#endif

		//float angle = Acos(Clamp(cosSinRefAngle.Y() > 0.0f ? float(cosSinRefAngle.X()) : -float(cosSinRefAngle.X()), -0.9999999f, 0.9999999f));
		float angle = (cosSinRefAngle.y > 0.0f ? 1.0 : -1.0) * acos(clamp(float(cosSinRefAngle.x), -0.9999999f, 0.9999999f));

		if (lookingAtLight)
		{
			if (cosSinRefAngle.x > 0)
			{

			}
			else
			{
				angle = angle - (cosSinRefAngle.y > 0.0f ? 1.0 : -1.0) * 3.14159265358979323846;
			}
			//angle = (cosSinRefAngle.Y() > 0.0f ? 1.0 : -1.0) * Asin(Clamp(float(cosSinRefAngle.Y()), -0.9999999f, 0.9999999f));
		}

		float sampleArrayIndex = clamp((angle - startAngle) * angleStepInv, 0.0f, float(numAngleSlicesMinus1));
		int iAngleSlice = sampleArrayIndex + 0.5;



		float3 sampleDirVs = shadowSlicesRO[iAngleSlice].m_sampleDirectionVs;

		float3 shadowMapReferenceVectorVs = shadowSlicesRO[iAngleSlice].m_shadowMapReferenceVectorVs;

		// find top and bottom of the intersection of this point and plane
		float distToClipPlane = dot(shadowMapReferenceVectorVs, positionVS);
		float3 refray0 = float3(shadowSlicesRO[iAngleSlice].m_sliceVector0Vs.xy, 1.0f);
		float3 refray1 = float3(shadowSlicesRO[iAngleSlice].m_sliceVector1Vs.xy, 1.0f);

		// this math calculates how much we need to travel along top and bottom slice vectors to get to the same distance as the sample point
		float viewZ0 = distToClipPlane / dot(shadowMapReferenceVectorVs, refray0);
		float viewZ1 = distToClipPlane / dot(shadowMapReferenceVectorVs, refray1);


		// now find how far along it is along reference vector
		float distAlongreference = dot(positionVS, shadowMapReferenceVectorVs);

		float distAlongSamplingDir = distAlongreference * shadowSlicesRO[iAngleSlice].m_sampleDirToReferenceDirRatio;
		float shadowDepthStep = distAlongSamplingDir / shadowSlicesRO[iAngleSlice].m_logStep;


		if (lookingAtLight)
		{
			// the logic is a little different for when we look at the light

			// first subtract the distance along reference of start point of the slice
			distAlongreference -= shadowSlicesRO[iAngleSlice].m_logStep;

			// in this case the step is stored in m_sampleDirToReferenceDirRatio
			shadowDepthStep = distAlongreference / shadowSlicesRO[iAngleSlice].m_sampleDirToReferenceDirRatio;
		}


		uint2 texDim = uint2(128, 512);
		volumetricsShadowCacheRO.GetDimensionsFast(texDim.x, texDim.y);


		float yCoord = (shadowDepthStep + 0.5) / float(texDim.y);
		float xCoord = (iAngleSlice + 0.5f) / float(texDim.x);

		float shadowPosAlongLightDir = volumetricsShadowCacheRO.SampleLevel(pointSampler, float2(xCoord, yCoord), 0).r;
		uint2 shadowCoords = uint2(iAngleSlice, shadowDepthStep);
		//float shadowPosAlongLightDir = pSrt->m_volumetricsShadowCacheRO[shadowCoords].r;



		//float expMap = pSrt->m_volumetricsShadowCacheRO.SampleLevel(pSrt->m_pointSampler, float2(xCoord, yCoord), 0).g;
		//float shadowPosAlongLightDir = pSrt->m_volumetricsShadowCacheRO.SampleLevel(pSrt->m_linearSampler, float2(xCoord, yCoord), 0);

		float  maxExpMap = exp2(/*resShadowDistAlongRay*/ 1.0 * expShadowMapConstant);
		//float expMap = shadowDepthStep >= texDim.y || shadowDepthStep < 0 ? maxExpMap : volumetricsShadowCacheRO.SampleLevel(linearSampler, float2(xCoord, yCoord), 0).g;
		//float expMap = shadowDepthStep >= texDim.y ? maxExpMap : volumetricsShadowCacheRO.SampleLevel(linearSampler, float2(xCoord, yCoord), 0).g;
		float expMap = volumetricsShadowCacheRO.SampleLevel(linearSampler, float2(xCoord, yCoord), 0).g;


		float pointPosAlongLightDir = dot(positionVS, lightDirectionVs);

		float alwaysLit = saturate((posWs.y - shadowMaxDistAlongLightDir) / 8.0);
		float xzDistSqr = dot((posWs - camPos).xz, (posWs - camPos).xz);
		float nearDistSqr = nearBrightnessDistSqr;

#if SPLIT_FROXEL_TEXTURES
		float sunLightNearBoost = 1.0;
#else
		float sunLightNearBoost = lerp(1.0, nearBrightness, saturate((nearDistSqr - xzDistSqr) / 32.0));
#endif
		float sunLightNearBoostAdd = lerp(1.0, nearBrightnessAdd, saturate((nearDistSqr - xzDistSqr) / 32.0));

		// normal test:
		float dif = shadowPosAlongLightDir - pointPosAlongLightDir;

		pointPosAlongLightDir = clamp(pointPosAlongLightDir, -1.0 / (expShadowMapMultiplier * 2.0), 1.0 / (expShadowMapMultiplier * 2.0));

		float pointPosAlongLightDirForExp = pointPosAlongLightDir * (expShadowMapMultiplier * 2.0); // -1.0 .. 1.0 range

																									// exp map test
		float expVisibility = clamp(expMap * exp(-pointPosAlongLightDirForExp * expShadowMapConstant) - 1.0, 0.0f, 1.0f);

		//expVisibility = expVisibility >= pow(2, -6) ? 1 : 0;
#if CACHED_SHADOW_EXP_BASE_2
		expVisibility = clamp(expMap * exp2(-pointPosAlongLightDirForExp * expShadowMapConstant), 0.0f, 1.0f);
#endif
		expVisibility *= sunLightNearBoost; // this will end up being squared.. wasn't meant like this but noticed too late after a bunch of levels were tuned

		expVisibility *= expVisibility; // get a tighter curve
		//sunLightNearBoost = sunLightNearBoost > 1.0 ? 1 : 0;
										//expVisibility = max(alwaysLit, expVisibility);

#if FROXELS_ALLOW_DEBUG
		pSrt->m_destFogFroxelsDebug[froxelCoord].xyzw = float4(/*xCoord, yCoord*/shadowCoords, /*sampleArrayIndex*/expVisibility, pointPosAlongLightDirForExp);
		pSrt->m_destFogFroxelsDebug1[froxelCoord].xyzw = float4(/*positionVS.xyz*/pointPosAlongLightDir, shadowPosAlongLightDir, expMap, /*angle*//*pointPosAlongLightDirForExp*/
#if CACHED_SHADOW_EXP_BASE_2
			exp2(-pointPosAlongLightDirForExp * expShadowMapConstant)
#else
			exp(-pointPosAlongLightDirForExp * expShadowMapConstant)
#endif
			);
#endif
		float depthTest = (dif > 0.0);

		float depthTestExp = expVisibility;

		
		float densityBoost = abs(0.5f - expVisibility) * 2.0; // varies from 0 to 1. 0 in between shadow or no shadow, 1 when purely in shadow or out of shadow

		densityBoost = densityBoost;

		densityBoost = 1.0 - densityBoost;

		//float oneSliceDepth = 1;
		//#if UseExponentialDepth
		//	oneSliceDepth = DepthDerivativeAtSlice(sliceCoord);
		//#endif

		//densityBoost = densityBoost > 0.01;
		//densityAdd = max(densityAdd, densityBoost * pSrt->m_varianceFogDensity * 1.0 / oneSliceDepth); // fill in 1m of space
		//densityAdd = min(densityAddTotal, densityAdd);
		//densityAddTotal -= densityAdd;

		//densityBoost = densityBoost > 0.1;
		//depthTestExp = max(densityBoost, depthTestExp);
		densityBoost = 1.0;
		//depthTestExp = pow(depthTestExp, 1/5.0);
		//brightness += depthTest * 1.0f / numShadowSteps;

		//inScatterFactor = inScatterFactor > 0.05;

#if USE_EXPONENT_MAP_IN_CACHED_SHADOW
		brightness += inScatterFactor * depthTestExp;
#else
		brightness += inScatterFactor * depthTest;
#endif



	}


}


#define TRACK_PROBE_BEST_REGION 0
void FindBestRegion(VolumetricsFogSrt *pSrt, float3 posWs, inout int iBestRegion, inout float regionBlend)
{
	int iGlobalPlane = 0;
	iBestRegion = -1;
	int iBestRegionForProbeCull = -1;
	int probeRegionPlaneOffset = 0;

	float kFarFromRegion = 1.0; // 6.0f;

	posWs.y += pSrt->m_waterOffset;

	[isolate]
	{
		float minRegionAwayDist = 10000000.0f;
		int numSubRegions = pSrt->m_numSubRegions;
		for (int iSubRegion = 0; (iSubRegion < numSubRegions) /* && (!fullyInside)*/; ++iSubRegion)
		{
			int numPlanes = pSrt->m_fogSubRegionData[iSubRegion].m_numPlanes;
			uint internalPlaneBits = pSrt->m_fogSubRegionData[iSubRegion].m_internalPlaneBits;
			float blendOut = 5.0; // pSrt->m_fogSubRegionData[iSubRegion].m_fadeOutRadius; // this could be made a constant specifically for coarse culling
			int doProbeCull = (internalPlaneBits & 0x80000000) ? 0 : -1;
			bool inside = true;
			//fullyInside = true;
			//float regionAwayDist = 0.0f;
			float regionAwayDist = -blendOut;

			for (int iPlane = 0; iPlane < numPlanes; ++iPlane)
			{
				float4 planeEq = pSrt->m_fogPlanes[iGlobalPlane + iPlane];

				#if !FROXELS_UNDER_WATER_REGION_ADD_DENSITY

				if (planeEq.y > 0.99)
				{
					continue;
				}
				#endif

				float d = dot(planeEq.xyz, posWs) + planeEq.w;

				bool isInternal = internalPlaneBits & (1 << iPlane);

				if (d > 0.0)
				{
					inside = false; // fully outside, no need to iterate more
									//fullyInside = false;
	#if !TRACK_PROBE_BEST_REGION
					regionAwayDist = 10000000.0f; // will not change closest distance
					break; // can exit early in case we don't care about finding probe region
	#endif
				}



	#if TRACK_PROBE_BEST_REGION
				if (d <= 0)
	#endif
				{
					// if this is internal plane, it shouldn't contribute to closest to edge calculations
					// because it is completely inside combined region
					if (isInternal)
					{
						d = -blendOut;
					}
				}

	#if TRACK_PROBE_BEST_REGION
				// we want to keep checking planes even if we know we are outside of plane. we do it because we want to be more generous for
				// choosing a region to cull probes for. the idea here is that for froxels that are close but outside of region
				// we still want to exclude probes that are not in the region to avoid any bleeding
				if (d > kFarFromRegion)
				{
					// ok we can break out of the loop now. we are definitely far from region
					regionAwayDist = 10000000.0f; // will not change closest distance
					break;
				}
	#endif

				//if (d > -blendOut)
				//{
				//	fullyInside = false;
				//}

				regionAwayDist = max(regionAwayDist, d);

			}

			if (inside)
			{
				// we are inside of the region, just break out right away, we don't care if we are in any other one.
				minRegionAwayDist = regionAwayDist;
				iBestRegion = iSubRegion;
				iBestRegionForProbeCull = iSubRegion | doProbeCull; // will stay -1 if the region doesn't want the probe culling
				probeRegionPlaneOffset = iGlobalPlane;
				break;
			}


			if (regionAwayDist < minRegionAwayDist)
			{
				minRegionAwayDist = regionAwayDist;
				iBestRegionForProbeCull = iSubRegion | doProbeCull; // will stay -1 if the region doesn't want the probe culling
				probeRegionPlaneOffset = iGlobalPlane;
			}

			iGlobalPlane += numPlanes;

		}



		if (iBestRegion >= 0)
		{
			//float blendOut = pSrt->m_fogSubRegionData[iBestRegion].m_fadeOutRadius;
			float blendOut = pSrt->m_waterDensityBlendInRange;

			//float regionBlend = saturate(1.0 - (minRegionAwayDist) / blendOut);

			regionBlend = saturate(-(minRegionAwayDist) / blendOut);

			//FogRegionSettings regionSettings = pSrt->m_fogSubRegionData[iBestRegion].m_settings;
			//
			//regionAddlDensity = regionBlend * regionSettings.m_fogRegionAddlDensity;
			//regionTint.x = regionBlend * regionSettings.m_fogRegionAmbientTintR;
			//regionTint.y = regionBlend * regionSettings.m_fogRegionAmbientTintG;
			//regionTint.z = regionBlend * regionSettings.m_fogRegionAmbientTintB;
		}
	}
}


void SamplePreviousFrame(VolumetricsFogSrt *pSrt, inout int successCounter, inout float2 prevMiscAccum, inout FogTextureData prevValAccum, inout float prevValDensOrigAccum, inout float weightBiasAccum, inout float3 uv3d, float3 posNoJitWs, float2 screenDimF, inout uint classify, uint3 coord, inout float sampleOffCenter)
{
	
	


	// we have to use a non jittered position for sampling previous frame. otherwise verything moves around
	// can't do additional smoothing by just using jittered position. maybe need to come up with
	// a different jitter order
	//TODO: can we convert from view to view for faster conversion?
	float4 posLastFrameH = mul(float4(posNoJitWs, 1), pSrt->m_mLastFrameVP);
	float3 posLastFrameNdc = posLastFrameH.xyz / posLastFrameH.w;

	// now go from ndc to uv coords

	uv3d.xy = posLastFrameNdc.xy * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);

	uv3d.y = 1.0f - uv3d.y;

	float ndcDepth = posLastFrameNdc.z;

	float linearDepth = GetLinearDepth(ndcDepth, pSrt->m_lastFrameDepthParams);

	//uv3d.z = 5.5 / kGridDepth; //  clamp(linearDepth / kGridDepth, 0.0f, 1.0f);

	uv3d.z = CameraLinearDepthToFroxelZCoordExp(linearDepth, pSrt->m_fogGridOffsetPrev);
	uv3d.z = clamp(uv3d.z, 0.0f, 1.0f);




	
	//posNoJitWs = posWs;
	
	


	// we need to sample previous value of this world space location
	
	// we check last frame's froxel screen space data with a point sampler of original screen space data (not expanded to neighbors)

	//uint prevLastFroxelIntersectingGeo = pSrt->m_srcVolumetricsScreenSpaceInfoOrigPrev.SampleLevel(pSrt->m_volumetricsSrt->m_pointSampler, float2(uv3d.xy), 0).r;




#if FROXELS_NO_UPDATE_BEHIND_GEO && !FROXELS_NO_CHECKS
	FogTextureData prevVal; // = SamplePackedValueFogTexture(pSrt->m_srcFogFroxelsPrev, pSrt->m_linearSampler, uv3d);
	float prevValDensOrig;
	#if USE_FOG_PROPERTIES && !SPLIT_FROXEL_TEXTURES
		float4 prevProperties = float4(0, 0, 0, 0);
	#endif

	
	int2 screenCoord = floor(screenDimF * float2(uv3d.xy));
	int prevLastFroxelIntersectingGeo = pSrt->m_srcVolumetricsScreenSpaceInfoOrigPrev[screenCoord].r; // sampling out of bounds gives us 0

	//int prevLastFroxelIntersectingGeo = lastFroxelsCombined.y;

	//if (screenCoord.x < 0 || screenCoord.x >= screenDim.x || screenCoord.y < 0 || screenCoord.y >= screenDim.y)
	//{
		//prevLastFroxelIntersectingGeo = -1;
	//}

	//TODO: should we jusr read with [] here?
	int prevLastFroxelEvaluated = pSrt->m_srcVolumetricsScreenSpaceInfoPrevFrame[screenCoord].r; // sampling out of bounds gives us 0. if this one fits, we are not guaranteed to have neighbors, but we do have some value

	uint prevSlice = asuint(int(floor(uv3d.z * 64))); // will become huge as negative and will fail tests below
	
	sampleOffCenter = (uv3d.z * 64 - prevSlice) - 0.5f;

	


	if (prevSlice <= 0)
	{
	//	prevSlice = 100000; // will fail tests below
	}


	// same logic simplified:

	if (prevSlice <= prevLastFroxelIntersectingGeo  + 1)
	{
		// we are guaranteed that sampling within this froxel at current depth will give us a good result, because we know that on previous frame
		// this froxel was visible, so its neighbors would have been forced visible

		classify |= 1;

		if (prevSlice == prevLastFroxelIntersectingGeo + 1)
		{
			//float4 prevValLin = SamplePackedValueFogTexture(pSrt->m_srcFogFroxelsPrev, pSrt->m_pointSampler, uv3d);

			classify |= 2;

			uv3d.z = (prevSlice + 0.5f) / 64.0f;

			// test
			//uv3d.xy = (screenCoord + float2(0.5, 0.5)) / float2(screenDimF);

			//uv3d.xy = (screenCoord + float2(0.5, 0.5)) / float2(screenDim);
			
		
			//if ((abs(prevValLin.x - prevVal.x) > 0.001))
			//{
				//_SCE_BREAK();
				//prevVal = float4(prevValLin.x + prevVal.x, prevValLin.y +prevVal.y, prevValLin.z + prevVal.z, 1.0);

				//StorePackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord, prevVal);

				//return;
			//}
			//prevVal = float4(0, 0, 0, 0);
		}

		prevVal = SamplePackedValueFogTexture(pSrt->m_srcFogFroxelsPrev, pSrt->m_linearSampler, uv3d, prevValDensOrig);
		prevValAccum += prevVal;
		prevValDensOrigAccum += prevValDensOrig;
		#if USE_FOG_PROPERTIES && !SPLIT_FROXEL_TEXTURES
			prevProperties = SamplePackedValuePropertiesTexture(pSrt->m_srcPropertiesFroxelsPrev, pSrt->m_linearSampler, uv3d).rgba;
		#endif
		
		#if SPLIT_FROXEL_TEXTURES
			float2 prevMisc;
			prevMisc.xy = pSrt->m_srcMiscFroxelsPrev.SampleLevel(pSrt->m_linearSampler, uv3d, 0).rg;
			#if USE_MANUAL_POW_FROXELS
				prevMisc.xy *= prevMisc.xy;
				prevMisc.x = UnpackFroxelColor(prevMisc.x, 0);
			#endif
			prevMiscAccum += prevMisc;
		#endif
		successCounter += 1;
		
		if (prevMisc.x > 0.1)// && prevSlice < 30)
		{
		//	_SCE_BREAK();
		}
	}
#if FROXELS_USE_DETAILED_SUN
	else if (prevSlice <= prevLastFroxelEvaluated /* + 1*/)
	{
		// we can only sample within the xy boundaries of this froxel. still can be blended slice
		uv3d.xy = (screenCoord + float2(0.5, 0.5)) / float2(screenDimF);

		classify |= 4;

		if (prevSlice == prevLastFroxelEvaluated + 1)
		{
			//float4 prevValLin = SamplePackedValueFogTexture(pSrt->m_srcFogFroxelsPrev, pSrt->m_pointSampler, uv3d);

			uv3d.z = (prevSlice + 0.5f) / 64.0f;

			//uv3d.xy = (screenCoord + float2(0.5, 0.5)) / float2(screenDim);

			classify |= 8;

		
			//if ((abs(prevValLin.x - prevVal.x) > 0.001))
			//{
				//_SCE_BREAK();
				//prevVal = float4(prevValLin.x + prevVal.x, prevValLin.y +prevVal.y, prevValLin.z + prevVal.z, 1.0);

				//StorePackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord, prevVal);

				//return;
			//}
			//prevVal = float4(0, 0, 0, 0);
		}

		prevVal = SamplePackedValueFogTexture(pSrt->m_srcFogFroxelsPrev, pSrt->m_linearSampler, uv3d, prevValDensOrig);
		prevValAccum += prevVal;
		prevValDensOrigAccum += prevValDensOrig;

		#if USE_FOG_PROPERTIES && !SPLIT_FROXEL_TEXTURES
			prevProperties = SamplePackedValuePropertiesTexture(pSrt->m_srcPropertiesFroxelsPrev, pSrt->m_linearSampler, uv3d).rgba;
		#endif
		
		#if SPLIT_FROXEL_TEXTURES
			float2 prevMisc;
			prevMisc.xy = pSrt->m_srcMiscFroxelsPrev.SampleLevel(pSrt->m_linearSampler, uv3d, 0).rg;
			#if USE_MANUAL_POW_FROXELS
				prevMisc.xy *= prevMisc.xy;
				prevMisc.x = UnpackFroxelColor(prevMisc.x, 0);
			#endif
			prevMiscAccum += prevMisc;
		#endif
		successCounter += 1;
		weightBiasAccum += 0.5f;
	}
#endif
	else
	{
		prevVal = 0;
		prevValDensOrig = 0;
		weightBiasAccum += 1.0;

		classify |= 16;
	}

	//if (pSrt->m_frameNumber % 64 == pos.x % 64)
	//	weightBias = 1;

	//weightBias = 1.0;
	//weightBias = 0.0f;
	//prevVal = SamplePackedValue(pSrt->m_srcFogFroxelsPrev, pSrt->m_linearSampler, uv3d);
#else
	// in this case we always sample previous value because we will update all froxels even behind geo (in which case we just use older value)
	float prevValDensOrig;
	FogTextureData prevVal = SamplePackedValueFogTexture(pSrt->m_srcFogFroxelsPrev, pSrt->m_linearSampler, uv3d, prevValDensOrig);
	prevValAccum += prevVal;
	prevValDensOrigAccum += prevValDensOrig;

	#if USE_FOG_PROPERTIES && !SPLIT_FROXEL_TEXTURES
		float4 prevProperties = SamplePackedValuePropertiesTexture(pSrt->m_srcPropertiesFroxelsPrev, pSrt->m_linearSampler, uv3d).rgba;
	#endif

	#if SPLIT_FROXEL_TEXTURES
		float2 prevMisc;
		prevMisc.xy = pSrt->m_srcMiscFroxelsPrev.SampleLevel(pSrt->m_linearSampler, uv3d, 0).rg;
		//prevMisc.xy = pSrt->m_srcMiscFroxelsPrev[coord].rg;
		

		#if USE_MANUAL_POW_FROXELS
			prevMisc.xy *= prevMisc.xy;
			prevMisc.x = UnpackFroxelColor(prevMisc.x, 0);
			
		#endif
		//if (coord.z != 15)
		//prevMisc.y = 0;
		prevMiscAccum += prevMisc;
	#endif
	successCounter += 1;
#endif
}


#undef TRACK_PROBE_BEST_REGION

[NUM_THREADS(GROUP_SIZE_X, GROUP_SIZE_Y, 1)] //
void CS_LightFroxelsAndTemporal(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 _groupId : SV_GroupID,

	#if FROXEL_SPLIT_DISPATCH || FROXEL_DISPATCH_INDIRECT_DYN_LIST
	VolumetricsFogSrtWrap srtWrap : S_SRT_DATA
	#else
	VolumetricsFogSrt *pSrt : S_SRT_DATA
	#endif
)
{
	//__s_setprio(3);

	uint3 groupId = _groupId;
	
	#if FROXEL_DISPATCH_INDIRECT_DYN_LIST
		VolumetricsFogSrt *pSrt = srtWrap.pSrt;
	#elif FROXEL_SPLIT_DISPATCH
		VolumetricsFogSrt *pSrt = srtWrap.pSrt;
		groupId.x += srtWrap.offset;
		//if (groupId.x >= NdAtomicGetValue(pSrt->m_gdsOffset_5))
		{
		//	return;
		}
	#endif

#if FROXEL_DISPATCH_INDIRECT || FROXEL_DISPATCH_INDIRECT_DYN_LIST
	#if FROXEL_DISPATCH_INDIRECT_DYN_LIST
		uint dispatchMask = pSrt->m_srcDispatchLists[srtWrap.offset][groupId.x].m_pos;
	#elif FROXEL_EARLY_PASS
		#if FROXELS_NO_SUN_SHADOW_CHECK
			uint dispatchMask = pSrt->m_srcDispatchListEarly1[groupId.x].m_pos;
		#elif FROXEL_NO_LIGHTS_LIST

			uint dispatchMask = pSrt->m_srcDispatchListEarly2_NoLights[groupId.x].m_pos;
		#else
			uint dispatchMask = pSrt->m_srcDispatchListEarly0[groupId.x].m_pos;
		#endif
	#else
		#if FROXELS_NO_SUN_SHADOW_CHECK
			uint dispatchMask = pSrt->m_srcDispatchList1[groupId.x].m_pos;
			/*
			uint dispatchMask = 0;
			while (true)
			{ 
				uint counter = NdAtomicGetValue(pSrt->m_gdsOffset_1+dispatchMask);
				if (counter > groupId.x)
				{
					dispatchMask = pSrt->m_srcDispatchList1[min(groupId.x, counter)].m_pos;
					break;
				}
				else
				{
					dispatchMask += 0.00000009;
				}

		
			} // spin
			*/
		#elif FROXEL_NO_LIGHTS_LIST
			uint dispatchMask = pSrt->m_srcDispatchList2[groupId.x].m_pos;
		#else
			uint dispatchMask = pSrt->m_srcDispatchList0[groupId.x].m_pos;
			/*
			uint dispatchMask = 0;
			while (true)
			{ 
				uint counter = NdAtomicGetValue(pSrt->m_gdsOffset_0+dispatchMask);
				if (counter > groupId.x)
				{
					dispatchMask = pSrt->m_srcDispatchList0[min(groupId.x, counter)].m_pos;
					break;
				}
				else
				{
					dispatchMask += 0.00000009;
				}

		
			} // spin
			*/
		#endif
	#endif

	uint groupZ = dispatchMask & 0x000003FF;
	uint groupY = (dispatchMask >> 10) & 0x000003FF;
	uint groupX = (dispatchMask >> 20) & 0x000003FF;
	uint needSliceCheck = (dispatchMask >> 30) & 0x00000001;

	uint2 groupPos = uint2(GROUP_SIZE_X, GROUP_SIZE_Y) * uint2(groupX, groupY);

	uint2 pos = groupPos + groupThreadId.xy;

	int iSlice = groupZ;
#else
	uint2 groupPos = uint2(GROUP_SIZE_X, GROUP_SIZE_Y) * groupId.xy;

	uint2 pos = groupPos + groupThreadId.xy;

	int iSlice = groupId.z;
#endif

	// march through all slices of froxel grid and accumulate results

	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);

	float4 accumVal = 0; // pSrt->m_destFogFroxels[froxelCoord];
	float densityAdd = 0;
	float densityAddTotal = 3.0;
	float prevBrightness = 0;
	float brightnessRunningAverage = 0;
	float brightnessAccum = 0;

	// we alway copy the value of the last slice from preceding slice

	// note we will copy last slice lighting into lastSlice + 1, because when we will sample for pixels within the last slice
	// it will interpolate beween last slice and last slice + 1

	//int lastSlice = min(62, int(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x));
	//uint2 lastFroxelsCombined = uint2(pSrt->m_srcVolumetricsScreenSpaceInfoCombined[froxelCoord.xy].xy);

#if !FROXEL_EARLY_PASS
	uint lastFroxelTouchedByGeo = uint(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x);
	//uint lastFroxelTouchedByGeo = lastFroxelsCombined.x;
	int lastSliceToEverBeSampled = min(63, lastFroxelTouchedByGeo + 1);
#endif

	// to make the loghting happen always:
	//lastSlice = 62;

	//for (int iSlice = min(lastSlice, 1); iSlice <= lastSlice; ++iSlice)

	
	froxelCoord.z = iSlice;

	FogTextureData destVal;
	float destValDensOrig;

	//StorePackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord, destVal);

#if 1

	// 0,0,0 coord corrresponds to top left corner of the screen at the screen depth, so +Y is down
	// in ndc Y+ is up
#if !FROXELS_DYNAMIC_RES
	float2 pixelPosNormNoJit = float2(pos.x + 0.5, pos.y + 0.5) / float2(NumFroxelsXRounded_F, NumFroxelsYRounded_F);
#else
	float2 pixelPosNormNoJit = float2(pos.x + 0.5, pos.y + 0.5) * float2(pSrt->m_numFroxelsXInv, pSrt->m_numFroxelsXInv * 16.0 / 9.0);
#endif

	float posNegBasedOnSlice = (iSlice % 2 ? -1.0 : 1.0);

	float negFactorBasedOnPos = ((iSlice + pos.x + pos.y) % 2) ? -1.0 : 1.0;

	float2 pixelPosNormJit = pixelPosNormNoJit +(pSrt->m_fogJitterXYUnorm.xy * posNegBasedOnSlice);
	
	//float2 pixelPosJit = float2(FogFroxelGridSizeNativeRes, FogFroxelGridSizeNativeRes) * (float2(froxelCoord.x, froxelCoord.y) + float2(0.5, 0.5) + pSrt->m_froxelFogJitterXY.xy * posNegBasedOnSlice) / float2(SCREEN_NATIVE_RES_W_F, SCREEN_NATIVE_RES_H_F);
	
	float ndcX = pixelPosNormNoJit.x * 2.0f - 1.0f;
	float ndcY = pixelPosNormNoJit.y * 2.0f - 1.0f;
	ndcY = -ndcY; // so we flip Y

	int numShadowSteps = 1;

	float weightBias = 0.0;
	uint classify = 0;
	FogTextureData resultValue;
	float resultValueFinal;
	float4 newProperties = float4(0,0,0,0);
#if FROXELS_USE_WATER || SPLIT_FROXEL_TEXTURES
	float4 newMisc = float4(0, 0, 0, 0);
#endif

	float sliceCoordNoJit = iSlice + 0.5;
	float sliceCoordNoJit0 = iSlice + 0.0;
	float sliceCoordNoJit1 = iSlice + 1.0;

	float viewZNoJit = FroxelZSliceToCameraDistExp(sliceCoordNoJit, pSrt->m_fogGridOffset);
	float viewZNoJit0 = FroxelZSliceToCameraDistExp(sliceCoordNoJit0, pSrt->m_fogGridOffset);
	float viewZNoJit1 = FroxelZSliceToCameraDistExp(sliceCoordNoJit1, pSrt->m_fogGridOffset);

		//float4 posNoJitWsH = mul(float4(ndcX, ndcY, ndcZNoJit, 1), pSrt->m_mVPInv);
	//float3 posNoJitWs = posNoJitWsH.xyz / posNoJitWsH.w;

#if !FROXELS_DYNAMIC_RES
	uint2 screenDim = uint2(NumFroxelsXRounded, NumFroxelsYRounded);
	float2 screenDimF = float2(screenDim);
#else
	//uint2 screenDim;
	//pSrt->m_srcVolumetricsScreenSpaceInfoOrigPrev.GetDimensionsFast(screenDim.x, screenDim.y);
	//float2 screenDimF = float2(1.0 / pSrt->m_numFroxelsXInv, 1.0 / pSrt->m_numFroxelsXInv / 16.0 * 9.0);

	float2 screenDimF = pSrt->m_numFroxelsXY;
#endif


	float3 posNoJitWs;
	
	// works ok
	//float4 posNoJitWsH = mul(float4(ndcX * viewZNoJit, ndcY * viewZNoJit, ndcZNoJit * viewZNoJit, viewZNoJit), pSrt->m_mVPInv);
	//float3 posNoJitWs = posNoJitWsH.xyz; // / posNoJitWsH.w;

	float2 prevMisc = float2(0, 0);
	FogTextureData prevVal = 0;
	float prevValDensOrig = 0;


	
	

	float3 uv3d;
	float sampleOffCenter = 0;
#define FROXELS_MULTI_SAMPLE_PREVIOUS 0

#if !FROXELS_MULTI_SAMPLE_PREVIOUS
	//[isolate]
	{
		float4 posNoJitWsH;
		float ndcZNoJit = GetNdcDepthSafe(viewZNoJit, pSrt->m_depthParams);

		posNoJitWsH = mul(float4(ndcX * viewZNoJit, ndcY * viewZNoJit, ndcZNoJit * viewZNoJit, viewZNoJit), pSrt->m_mAltVPInv);
		posNoJitWs = posNoJitWsH.xyz + pSrt->m_altWorldPos; // / posWsH.w;
	}
	int successCounter = 0;
	SamplePreviousFrame(pSrt, successCounter, prevMisc, prevVal, prevValDensOrig, weightBias, uv3d, posNoJitWs, float2(pSrt->m_numFroxelsXYPrevF), classify, uint3(pos, iSlice), sampleOffCenter);
#else
	int successCounter = 0;
	float3 posNoJitWs0;
	float3 posNoJitWs1;

	{
		float4 posNoJitWsH;
		float ndcZNoJit0 = GetNdcDepthSafe(viewZNoJit0, pSrt->m_depthParams);

		posNoJitWsH = mul(float4(ndcX * viewZNoJit0, ndcY * viewZNoJit0, ndcZNoJit0 * viewZNoJit0, viewZNoJit0), pSrt->m_mAltVPInv);
		posNoJitWs0 = posNoJitWsH.xyz + pSrt->m_altWorldPos; // / posWsH.w;
	}
	
	posNoJitWs = posNoJitWs0;

	{
		float4 posNoJitWsH;
		float ndcZNoJit1 = GetNdcDepthSafe(viewZNoJit1, pSrt->m_depthParams);

		posNoJitWsH = mul(float4(ndcX * viewZNoJit1, ndcY * viewZNoJit1, ndcZNoJit1 * viewZNoJit1, viewZNoJit1), pSrt->m_mAltVPInv);
		posNoJitWs1 = posNoJitWsH.xyz + pSrt->m_altWorldPos; // / posWsH.w;
	}


	// we dont need to loop it seems. 2 samples instead of one does the job well
	int kNumPrevSamples = 2;
	//for (int i = 0; i < kNumPrevSamples; ++i)
	//{
	//	float blendVal = 0.5 / kNumPrevSamples + 1.0 / kNumPrevSamples * i;
	//
	//	float3 prevPosWsBlended = lerp(posNoJitWs0, posNoJitWs1, blendVal);
	//	SamplePreviousFrame(pSrt, successCounter, prevMisc, prevVal, prevValDensOrig, weightBias, prevPosWsBlended, screenDimF, uint3(pos, iSlice));
	//}
	
	float3 uv3d0;
	{
		float blendVal = 0.5 / kNumPrevSamples + 1.0 / kNumPrevSamples * 0;
		blendVal = 0.25;

		//blendVal = 0.25 - 0.1;
		float3 posWsBlended = lerp(posNoJitWs0, posNoJitWs1, blendVal);
		SamplePreviousFrame(pSrt, successCounter, prevMisc, prevVal, prevValDensOrig, weightBias, uv3d0, posWsBlended, screenDimF, classify, uint3(pos, iSlice));
	}

	float3 uv3d1;
	{
		float blendVal = 0.5 / kNumPrevSamples + 1.0 / kNumPrevSamples * 1;
		blendVal = 0.75;
		//blendVal = 0.75 + 0.1;
		float3 prevPosWsBlended = lerp(posNoJitWs0, posNoJitWs1, blendVal);
		SamplePreviousFrame(pSrt, successCounter, prevMisc, prevVal, prevValDensOrig, weightBias, uv3d1, prevPosWsBlended, screenDimF, classify, uint3(pos, iSlice));
	}
	
	uv3d = uv3d0;

	if (successCounter > 0)
	{
		prevVal /= successCounter;
		prevValDensOrig /= successCounter;
		prevMisc /= successCounter;
	}

	weightBias /= kNumPrevSamples; // will be 1.0 if all failed. 0.5 if 1/2 failed, etc.
	//weightBias *= weightBias;
	//weightBias = weightBias < 1.0 ? 0 : 1.0f;
	#if USE_FOG_CONVERGE_BUFFER
	float framesInHistory = 255 * prevVal.y + 1.0;
	#else
	float framesInHistory = 255 * 1.0 + 1.0;

	#endif
	// 1 frame - weight 0.5
	// 2 frames : new weight = 0.3
	// 3 frames : new weight = 0.25
	// 20 frames : new weight 0.05

	//newWeight = max(newWeight, 1.0 / (1.0 + framesInHistory));

	// how much of mid sample to take vs multisample
	//float mutisampleWeight = framesInHistory < 100 ? 0 : 1;//	 saturate(framesInHistory / 20);
	//
	////if (weightBias == 1.0)
	//{
	//	float4 posNoJitWsH;
	//	float ndcZNoJit = GetNdcDepthSafe(viewZNoJit, pSrt->m_depthParams);
	//
	//	posNoJitWsH = mul(float4(ndcX * viewZNoJit, ndcY * viewZNoJit, ndcZNoJit * viewZNoJit, viewZNoJit), pSrt->m_mAltVPInv);
	//	posNoJitWs = posNoJitWsH.xyz + pSrt->m_altWorldPos; // / posWsH.w;
	//
	//	successCounter = 0;
	//
	//	float2 midPrevMisc = float2(0, 0);
	//	FogTextureData midPrevVal = 0;
	//
	//
	//
	//	float midWeightBias = 0;
	//	SamplePreviousFrame(pSrt, successCounter, midPrevMisc, midPrevVal, midWeightBias, uv3d, posNoJitWs, screenDimF, classify);
	//
	//	if (midWeightBias != 1.0)
	//	{
	//		prevMisc = lerp(midPrevMisc, prevMisc, mutisampleWeight);
	//	}
	//}


#endif


#define USE_METHOD1 1

	float sliceCoordJit_Start = iSlice + pSrt->m_ndcFogJitterZ05;
	float sliceCoordJit_End = sliceCoordJit_Start + 1;

	#if FROXELS_USE_DETAILED_SUN
	// we expand froxel size
	float sliceCoordNoJit_Start = iSlice - 0.5;
	float sliceCoordNoJit_End = sliceCoordNoJit_Start + 1.5;
	#else
	float sliceCoordNoJit_Start = iSlice;
	float sliceCoordNoJit_End = sliceCoordNoJit_Start + 1.0;

	#endif

	if (weightBias == 1.0)
	{
		//sliceCoordNoJit_Start -= 1.0;
		//sliceCoordNoJit_End += 1.0;
	}

	// METHOD 1 - works but is temporal
	#if USE_METHOD1

	#else
	//float sliceCoordNoJit_Start = iSlice + pSrt->m_ndcFogJitterZ;
	//float sliceCoordNoJit_End = sliceCoordNoJit_Start + 1;
	
	// METHOD 2
	float sliceCoordNoJit_Start = iSlice - 0.5;
	float sliceCoordNoJit_End = sliceCoordNoJit_Start + 2.0;
	#endif

	float sliceCoord_Jit0 = sliceCoordNoJit;
	float sliceCoord_Jit1 = sliceCoordNoJit;
	float sliceCoord_Jit2 = sliceCoordNoJit;
	float sliceCoord_Jit3 = sliceCoordNoJit;

#if FROXELS_USE_DETAILED_SUN
	float srtJit = pSrt->m_ndcFogJitterZ05;
	float srtJit1 = pSrt->m_ndcFogJitterZ05 - sign(pSrt->m_ndcFogJitterZ05) * 0.5;

	float vairableJit = negFactorBasedOnPos > 0 ? srtJit : srtJit1;
#else
	float srtJit = pSrt->m_ndcFogJitterZ;
#endif

	float ndcFogJitterZ_0 = srtJit;

	float ndcFogJitterZ_1 = srtJit + 0.25f;
	if (ndcFogJitterZ_1 > 0.5)
		ndcFogJitterZ_1 = ndcFogJitterZ_1 - 1.0;

	float ndcFogJitterZ_2 = srtJit + 0.5f;
	if (ndcFogJitterZ_2 > 0.5)
		ndcFogJitterZ_2 = ndcFogJitterZ_2 - 1.0;
	float ndcFogJitterZ_3 = srtJit + 0.75f;
	if (ndcFogJitterZ_3 > 0.5)
		ndcFogJitterZ_3 = ndcFogJitterZ_3 - 1.0;

#define FROXELS_NUM_SUN_SAMPLES_DYNAMIC 1
#define FROXELS_NUM_SUN_SAMPLES 4

	// jitter only makes sense if we do one shadow step
	// jitter is supposed to result in same effect as multiple shadow steps
	if (numShadowSteps == 1)
	{
		sliceCoord_Jit0 = sliceCoord_Jit0 + ndcFogJitterZ_0; // *posNegBasedOnSlice;
		sliceCoord_Jit1 = sliceCoord_Jit1 + ndcFogJitterZ_1;
		sliceCoord_Jit2 = sliceCoord_Jit2 + ndcFogJitterZ_2;
		sliceCoord_Jit3 = sliceCoord_Jit3 + ndcFogJitterZ_3;
	}

	//float sliceCoordNext = sliceCoord + (pSrt->m_ndcFogJitterZ > 0.0 ? pSrt->m_ndcFogJitterZ - 0.5f : pSrt->m_ndcFogJitterZ - 0.5f);
	float sliceCoordNext = sliceCoord_Jit0 + 1 / 8.0; // this is inbetween jitter steps (since they are 1/4)

	float viewZ_Jit0 = FroxelZSliceToCameraDistExp(sliceCoord_Jit0, pSrt->m_fogGridOffset);
	float viewZ_Jit1 = FroxelZSliceToCameraDistExp(sliceCoord_Jit1, pSrt->m_fogGridOffset);
	float viewZ_Jit2 = FroxelZSliceToCameraDistExp(sliceCoord_Jit2, pSrt->m_fogGridOffset);
	float viewZ_Jit3 = FroxelZSliceToCameraDistExp(sliceCoord_Jit3, pSrt->m_fogGridOffset);
	float viewZNext = FroxelZSliceToCameraDistExp(sliceCoordNext, pSrt->m_fogGridOffset);

	float viewZNoJit_Start = FroxelZSliceToCameraDistExp(sliceCoordNoJit_Start, pSrt->m_fogGridOffset);
	float viewZNoJit_End = FroxelZSliceToCameraDistExp(sliceCoordNoJit_End, pSrt->m_fogGridOffset);

	float viewZJit_Start = FroxelZSliceToCameraDistExp(sliceCoordJit_Start, pSrt->m_fogGridOffset);
	float viewZJit_End = FroxelZSliceToCameraDistExp(sliceCoordJit_End, pSrt->m_fogGridOffset);



	float ndcZ_Jit0 = GetNdcDepthSafe(viewZ_Jit0, pSrt->m_depthParams);
	float ndcZ_Jit1 = GetNdcDepthSafe(viewZ_Jit1, pSrt->m_depthParams);
	float ndcZ_Jit2 = GetNdcDepthSafe(viewZ_Jit2, pSrt->m_depthParams);
	float ndcZ_Jit3 = GetNdcDepthSafe(viewZ_Jit3, pSrt->m_depthParams);
	float ndcZNext = GetNdcDepthSafe(viewZNext, pSrt->m_depthParams);

	float ndcZNoJit_Start = GetNdcDepthSafe(viewZNoJit_Start, pSrt->m_depthParams);
	float ndcZNoJit_End = GetNdcDepthSafe(viewZNoJit_End, pSrt->m_depthParams);
	float ndcZJit_Start = GetNdcDepthSafe(viewZJit_Start, pSrt->m_depthParams);
	float ndcZJit_End = GetNdcDepthSafe(viewZJit_End, pSrt->m_depthParams);


	//pixelPosJit.x += (iShadowStep - 1) * FogFroxelGridSizeNativeRes / 1920.0;

	float depthVs_Jit0 = viewZ_Jit0;
	float depthVs_Jit1 = viewZ_Jit1;
	float depthVs_Jit2 = viewZ_Jit2;
	float depthVs_Jit3 = viewZ_Jit3;
	float depthVsNext = viewZNext;
	float depthVSNoJit_Start = viewZNoJit_Start;
	float depthVSNoJit_End = viewZNoJit_End;
	float depthVSJit_Start = viewZJit_Start;
	float depthVSJit_End = viewZJit_End;

	float3 positionVs_Jit0 = float3(((pixelPosNormJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVs_Jit0;
	float3 positionVs_Jit1 = float3(((pixelPosNormJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVs_Jit1;
	float3 positionVs_Jit2 = float3(((pixelPosNormJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVs_Jit2;
	float3 positionVs_Jit3 = float3(((pixelPosNormJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVs_Jit3;
	float3 positionVsNext = float3(((pixelPosNormJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVsNext;
	float3 positionVSNoZJit = float3(((pixelPosNormJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * viewZNoJit;
	float3 positionVSNoZJit_Start = float3(((pixelPosNormJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVSNoJit_Start; // used in method 2
	float3 positionVSNoZJit_End = float3(((pixelPosNormJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVSNoJit_End; // used in method 2
	float3 positionVSNoZJitNoXYJit_Start = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVSNoJit_Start; // used in method 2
	float3 positionVSNoZJitNoXYJit_End = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVSNoJit_End; // used in method 2

	float3 positionVSZJit_Start = float3(((pixelPosNormJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVSJit_Start;
	float3 positionVSZJit_End = float3(((pixelPosNormJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVSJit_End;

	float4 posWsNoZJitH = mul(float4(positionVSNoZJit, 1), pSrt->m_mVInv);
	float3 posWsNoZJitNotUsed = posWsNoZJitH.xyz;// / posWsH.w;


	//float3 posWs_Jit0 = mul(float4(positionVs_Jit0, 1), pSrt->m_mVInv).xyz;
	//float3 prevPosVs_Jit0 = mul(float4(posWs_Jit0, 1), pSrt->m_mLastFrameV).xyz;
	float3 prevPosVs_Jit0 = mul(float4(positionVs_Jit0, 1), pSrt->m_mVInvxLastFrameV).xyz;

	
	float3 prevPositionVSNoZJitNoXYJit_Start = mul(float4(positionVSNoZJitNoXYJit_Start, 1), pSrt->m_mVInvxLastFrameV).xyz;
	float3 prevPositionVSNoZJitNoXYJit_End = mul(float4(positionVSNoZJitNoXYJit_End, 1), pSrt->m_mVInvxLastFrameV).xyz;



	//float3 posWs_Jit1 = mul(float4(positionVs_Jit1, 1), pSrt->m_mVInv).xyz;
	//float3 prevPosVs_Jit1 = mul(float4(posWs_Jit1, 1), pSrt->m_mLastFrameV).xyz;
	float3 prevPosVs_Jit1 = mul(float4(positionVs_Jit1, 1), pSrt->m_mVInvxLastFrameV).xyz;

	//float3 posWs_Jit2 = mul(float4(positionVs_Jit2, 1), pSrt->m_mVInv).xyz;
	//float3 prevPosVs_Jit2 = mul(float4(posWs_Jit2, 1), pSrt->m_mLastFrameV).xyz;
	float3 prevPosVs_Jit2 = mul(float4(positionVs_Jit2, 1), pSrt->m_mVInvxLastFrameV).xyz;
	
	//float3 posWs_Jit3 = mul(float4(positionVs_Jit3, 1), pSrt->m_mVInv).xyz;
	//float3 prevPosVs_Jit3 = mul(float4(posWs_Jit3, 1), pSrt->m_mLastFrameV).xyz;
	float3 prevPosVs_Jit3 = mul(float4(positionVs_Jit3, 1), pSrt->m_mVInvxLastFrameV).xyz;

	//float3 posWsNextJit = mul(float4(positionVsNext, 1), pSrt->m_mVInv).xyz;
	//float3 prevPosVsNextJit = mul(float4(posWsNextJit, 1), pSrt->m_mLastFrameV).xyz;
	float3 prevPosVsNextJit = mul(float4(positionVsNext, 1), pSrt->m_mLastFrameV).xyz;

	

	// check if we are in water
#if FROXELS_USE_WATER
	int iBestRegion;
	float regionBlend = 0;
	float valueToStore = 0;
	FindBestRegion(pSrt, posNoJitWs, iBestRegion, regionBlend);

	//regionBlend = posNoJitWs.y < -0.0 ? 1 : 0;
	float3 waterColorAdd = float3(0, 0, 0);
	if (iBestRegion >= 0)
	{
		//waterColorAdd = float3(1, 0, 0) * regionBlend;
	}
#endif

	


	// this helps with sgpr count. also makes sure we don't read current result unless we need it
	//[isolate] // not with the new compiler though..
	{
#if !FROXELS_NO_CHECKS && !FROXEL_EARLY_PASS
	if ( /*iSlice >=  min(lastSliceToEverBeSampled, 1) && */iSlice <= lastSliceToEverBeSampled /* || true*/)
#endif
	{

		// do lighting for froxels in view
		destVal = ReadPackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord, destValDensOrig);

	


		
	float coneAngleCos = pSrt->m_sunConeCos; // cos(1.0 * 3.1415 / 180.0f);
		
	#if SPLIT_FROXEL_TEXTURES
	
		float fogStartDistFactor = 1.0;
	#else
		float fogStartDistFactor = clamp((distToFroxel - pSrt->m_sunFogStartDistance + 4.0) / 4.0, 0.0f, 1.0f);
		float fogEndDistFactor = clamp((pSrt->m_sunEndDist - distToFroxel) / 4.0, 0.0f, 1.0f);
	
		fogStartDistFactor *= fogEndDistFactor;


		float fogHeightFactor = clamp((froxelHeight - pSrt->m_sunHeightBlendOffset) * pSrt->m_sunHeightBlendRangeInv, 0.0f, 1.0f);

		fogStartDistFactor *= fogHeightFactor;
	#endif

		// to find previous value in the grid, we need to first find the world position of current sample and the project it into previous frame
#if !FROXELS_NO_SUN_SHADOW_CHECK

		float brightness = 0;
		float densityFactor = 1;

#if USE_CACHED_SHADOW



		#if FROXEL_USE_PREV_FRAME_DATA

			float3 camToFroxelVs = normalize(prevPosVs_Jit0); // calculate it once

			float3 sunPos = pSrt->m_lightDirectionVsPrev * 512.0f;
			float3 sunToFroxelVs = normalize(prevPosVs_Jit0 - sunPos);
			float3 sunToCamera = -pSrt->m_lightDirectionVsPrev;

			
			float cosFroxelToCamFromSun = dot(sunToFroxelVs, sunToCamera);
			float inScatterFactor = 1;
			#if !FROXELS_SUN_SCATTERING_IN_ACCUMULATE
				inScatterFactor = SingleScattering(camToFroxelVs, pSrt->m_lightDirectionVsPrev, pSrt->m_scatterParams.x, pSrt->m_scatterParams.y);
			#endif

			float coneFactor = saturate((cosFroxelToCamFromSun - coneAngleCos) * 1024.0f);  // positive if inside
			#if !SPLIT_FROXEL_TEXTURES // done in accumulation
				inScatterFactor *= coneFactor;
			#endif

			float3 froxelLineVsPrev = normalize(prevPosVs_Jit0);
			float2 cosSinRefAnglePrev = normalize(float2(dot(froxelLineVsPrev, pSrt->m_samplingPlaneZVsPrev), dot(froxelLineVsPrev, pSrt->m_samplingPlaneXVsPrev)));


			DoShadowCacheLighting(
				// data that changes for prev frame
				pSrt->m_shadowSlicesPrevRO, pSrt->m_volumetricsShadowCachePrevRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
				pSrt->m_samplingPlaneZVsPrev, pSrt->m_samplingPlaneXVsPrev, pSrt->m_lightDirectionVsPrev, froxelLineVsPrev, cosSinRefAnglePrev, pSrt->m_startAnglePrev, pSrt->m_angleStepInvPrev, pSrt->m_numAngleSlicesMinus1Prev, pSrt->m_lookingAtLightPrev, pSrt->m_camPosPrev,
				// same for different frames
				pSrt->m_shadowMaxDistAlongLightDir, pSrt->m_nearBrightness, pSrt->m_nearBrightnessAdd, pSrt->m_nearBrightnessDistSqr, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier, pSrt->m_scatterParams,

				//positionVS, 
				prevPosVs_Jit0,
				posNoJitWs, inScatterFactor, densityAdd, brightness);

#if FROXELS_NUM_SUN_SAMPLES > 1
			DoShadowCacheLighting(
				// data that changes for prev frame
				pSrt->m_shadowSlicesPrevRO, pSrt->m_volumetricsShadowCachePrevRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
				pSrt->m_samplingPlaneZVsPrev, pSrt->m_samplingPlaneXVsPrev, pSrt->m_lightDirectionVsPrev, froxelLineVsPrev, cosSinRefAnglePrev, pSrt->m_startAnglePrev, pSrt->m_angleStepInvPrev, pSrt->m_numAngleSlicesMinus1Prev, pSrt->m_lookingAtLightPrev, pSrt->m_camPosPrev,
				// same for different frames
				pSrt->m_shadowMaxDistAlongLightDir, pSrt->m_nearBrightness, pSrt->m_nearBrightnessAdd, pSrt->m_nearBrightnessDistSqr, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier, pSrt->m_scatterParams,

				//positionVS, 
				prevPosVs_Jit2,
				posNoJitWs, inScatterFactor, densityAdd, brightness);
#endif

#if FROXELS_NUM_SUN_SAMPLES > 2
			DoShadowCacheLighting(
				// data that changes for prev frame
				pSrt->m_shadowSlicesPrevRO, pSrt->m_volumetricsShadowCachePrevRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
				pSrt->m_samplingPlaneZVsPrev, pSrt->m_samplingPlaneXVsPrev, pSrt->m_lightDirectionVsPrev, froxelLineVsPrev, cosSinRefAnglePrev, pSrt->m_startAnglePrev, pSrt->m_angleStepInvPrev, pSrt->m_numAngleSlicesMinus1Prev, pSrt->m_lookingAtLightPrev, pSrt->m_camPosPrev,
				// same for different frames
				pSrt->m_shadowMaxDistAlongLightDir, pSrt->m_nearBrightness, pSrt->m_nearBrightnessAdd, pSrt->m_nearBrightnessDistSqr, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier, pSrt->m_scatterParams,

				//positionVS, 
				prevPosVs_Jit1,
				posNoJitWs, inScatterFactor, densityAdd, brightness);
#endif
#if FROXELS_NUM_SUN_SAMPLES > 3
			DoShadowCacheLighting(
				// data that changes for prev frame
				pSrt->m_shadowSlicesPrevRO, pSrt->m_volumetricsShadowCachePrevRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
				pSrt->m_samplingPlaneZVsPrev, pSrt->m_samplingPlaneXVsPrev, pSrt->m_lightDirectionVsPrev, froxelLineVsPrev, cosSinRefAnglePrev, pSrt->m_startAnglePrev, pSrt->m_angleStepInvPrev, pSrt->m_numAngleSlicesMinus1Prev, pSrt->m_lookingAtLightPrev, pSrt->m_camPosPrev,
				// same for different frames
				pSrt->m_shadowMaxDistAlongLightDir, pSrt->m_nearBrightness, pSrt->m_nearBrightnessAdd, pSrt->m_nearBrightnessDistSqr, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier, pSrt->m_scatterParams,

				//positionVS, 
				prevPosVs_Jit3,
				posNoJitWs, densityAdd, inScatterFactor, brightness);
#endif

			brightness = fogStartDistFactor * brightness / float(FROXELS_NUM_SUN_SAMPLES);
			/*
			DoShadowCacheLighting(
				// data that changes for prev frame
				pSrt->m_shadowSlicesPrevRO, pSrt->m_volumetricsShadowCachePrevRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
				pSrt->m_samplingPlaneZVsPrev, pSrt->m_samplingPlaneXVsPrev, pSrt->m_lightDirectionVsPrev, froxelLineVsPrev, cosSinRefAnglePrev, pSrt->m_startAnglePrev, pSrt->m_angleStepInvPrev, pSrt->m_numAngleSlicesMinus1Prev, pSrt->m_lookingAtLightPrev, pSrt->m_camPosPrev,
				// same for different frames
				pSrt->m_shadowMaxDistAlongLightDir, pSrt->m_nearBrightness, pSrt->m_nearBrightnessAdd, pSrt->m_nearBrightnessDistSqr, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier, pSrt->m_scatterParams,
			
				//positionVS, 
				prevPosVsNextJit,
				posNoJitWs,
				densityAdd,
				brightness);
			
			brightness = brightness / 2.0;
			*/

		#else // if use prev frame data
			// use current frame
			
			#if !FROXELS_NUM_SUN_SAMPLES_DYNAMIC
				float3 camToFroxelVs = normalize(positionVs_Jit0); // calculate it once

				float3 sunPos = pSrt->m_lightDirectionVs * 1024.0f;
				float3 sunToFroxelVs = normalize(positionVs_Jit0 - sunPos);
				float3 sunToCamera = -pSrt->m_adjustedLightDirectionVs;

				float cosFroxelToCamFromSun = dot(sunToFroxelVs, sunToCamera);
				float inScatterFactor = 1;
				#if !FROXELS_SUN_SCATTERING_IN_ACCUMULATE
					inScatterFactor = SingleScattering(camToFroxelVs, pSrt->m_lightDirectionVs, pSrt->m_scatterParams.x, pSrt->m_scatterParams.y);
				#endif
			
				float coneFactor = saturate((cosFroxelToCamFromSun - coneAngleCos) * 1024.0f);  // positive if inside
				#if !SPLIT_FROXEL_TEXTURES // done in accumulation
					inScatterFactor *= coneFactor;
				#endif

				float3 froxelLineVs = normalize(positionVs_Jit0);
				float2 cosSinRefAngle = normalize(float2(dot(froxelLineVs, pSrt->m_samplingPlaneZVs), dot(froxelLineVs, pSrt->m_samplingPlaneXVs)));


				DoShadowCacheLighting(
					// data that changes for prev frame
					pSrt->m_shadowSlicesRO, pSrt->m_volumetricsShadowCacheRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
					pSrt->m_samplingPlaneZVs, pSrt->m_samplingPlaneXVs, pSrt->m_lightDirectionVs, froxelLineVs, cosSinRefAngle, pSrt->m_startAngle, pSrt->m_angleStepInv, pSrt->m_numAngleSlicesMinus1, pSrt->m_lookingAtLight, pSrt->m_camPos,
					// same for different frames
					pSrt->m_shadowMaxDistAlongLightDir, pSrt->m_nearBrightness, pSrt->m_nearBrightnessAdd, pSrt->m_nearBrightnessDistSqr, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier, pSrt->m_scatterParams,

					positionVs_Jit0, posNoJitWs, inScatterFactor, densityAdd, brightness);

				#if FROXELS_NUM_SUN_SAMPLES > 1

				DoShadowCacheLighting(
					// data that changes for prev frame
					pSrt->m_shadowSlicesRO, pSrt->m_volumetricsShadowCacheRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
					pSrt->m_samplingPlaneZVs, pSrt->m_samplingPlaneXVs, pSrt->m_lightDirectionVs, froxelLineVs, cosSinRefAngle, pSrt->m_startAngle, pSrt->m_angleStepInv, pSrt->m_numAngleSlicesMinus1, pSrt->m_lookingAtLight, pSrt->m_camPos,
					// same for different frames
					pSrt->m_shadowMaxDistAlongLightDir, pSrt->m_nearBrightness, pSrt->m_nearBrightnessAdd, pSrt->m_nearBrightnessDistSqr, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier, pSrt->m_scatterParams,

					positionVs_Jit2, posNoJitWs, inScatterFactor, densityAdd, brightness);

			
				#endif
				brightness = fogStartDistFactor * brightness / float(FROXELS_NUM_SUN_SAMPLES);
			#else

				// if dynamic number of samples
				float3 camToFroxelVs = normalize(positionVSNoZJit_Start); // calculate it once

				float3 sunPos = pSrt->m_lightDirectionVs * 1024.0f;
				float3 sunToFroxelVs = normalize(positionVSNoZJit_Start - sunPos);
				float3 sunToCamera = -pSrt->m_adjustedLightDirectionVs;

				float cosFroxelToCamFromSun = dot(sunToFroxelVs, sunToCamera);

				float inScatterFactor = 1;
				#if !FROXELS_SUN_SCATTERING_IN_ACCUMULATE
					inScatterFactor = SingleScattering(camToFroxelVs, pSrt->m_lightDirectionVs, pSrt->m_scatterParams.x, pSrt->m_scatterParams.y);
				#endif

				float coneFactor = saturate((cosFroxelToCamFromSun - coneAngleCos) * 1024.0f);  // positive if inside

				#if !SPLIT_FROXEL_TEXTURES // done in accumulation
					
					inScatterFactor *= coneFactor;
				#endif


				if (pos.x > pSrt->m_numFroxelsXY.x / 3 && pos.x < pSrt->m_numFroxelsXY.x - pSrt->m_numFroxelsXY.x / 3)
				{
					if (pos.y > pSrt->m_numFroxelsXY.y / 3 && pos.y < pSrt->m_numFroxelsXY.y - pSrt->m_numFroxelsXY.y / 3)
					{
						if (iSlice < 30)
						{
							//weightBias = 1.0;
						}
					}
				}
				//weightBias = 1.0;

				#if FROXELS_USE_DETAILED_SUN

				bool useMethod2 = true;

				useMethod2 = useMethod2 || (weightBias >= 0.5f);

				int kNumDynamicSteps = pSrt->m_numSunLightSteps * (1 + ((weightBias >= 0.5f) ? 1.0 : 0));

				float viewSpaceStep = (viewZNoJit_End - viewZNoJit_Start) / kNumDynamicSteps;

				float kHalfStep = 1.0 / (2 * kNumDynamicSteps);
				float kStep = 1.0 / kNumDynamicSteps;

				#if FROXELS_NEWLY_REVEALED_LIST
				// no xy jitter for new list
				float3 posVsStart = positionVSNoZJitNoXYJit_Start;
				float3 posVsEnd = positionVSNoZJitNoXYJit_End;
				vairableJit = 0;
				#else
				float3 posVsStart = (weightBias >= 0.5f) ? positionVSNoZJitNoXYJit_Start : positionVSNoZJit_Start;
				float3 posVsEnd = (weightBias >= 0.5f) ? positionVSNoZJitNoXYJit_End : positionVSNoZJit_End;
				vairableJit = (weightBias >= 0.5f) ? 0 : vairableJit; // no jitter when generating newly revealed data to avoid uneven history
				#endif

				float3 froxelLineVs = normalize(posVsStart);

				float2 cosSinRefAngle = normalize(float2(dot(froxelLineVs, pSrt->m_samplingPlaneZVs), dot(froxelLineVs, pSrt->m_samplingPlaneXVs)));

				float3 froxelLineVsPrev = normalize(prevPositionVSNoZJitNoXYJit_Start);
				float2 cosSinRefAnglePrev = normalize(float2(dot(froxelLineVsPrev, pSrt->m_samplingPlaneZVsPrev), dot(froxelLineVsPrev, pSrt->m_samplingPlaneXVsPrev)));


				if ((weightBias == 1.0 || useMethod2) && true)
				{
					// this is expensive froxel and we will also blur it
					#if FROXELS_LOG_NEWLY_REVEALED
					if (weightBias >= 0.5)// && iSlice < 32)
					{
						uint index = NdAtomicIncrement(pSrt->m_gdsOffset_blur);
						// slice is 6 bits (0..63)
						// posY is 9 bits
						// posX is 9 bits
						// 8 bits left
						uint extraVal = saturate(sampleOffCenter + 0.5) * 255;
						uint mask = (extraVal << 6 + 9 + 9) | ((pos.x) << 6 + 9) | ((pos.y) << 6) | (iSlice);
						pSrt->m_destDispatchListBlur[index].m_pos = mask;

						//return;
					}
					#endif


					for (int iStep = 0; iStep < kNumDynamicSteps; ++iStep)
					{
						// need interpolated view space position

						// METHOD 0
						// float kFactor = kHalfStep / 2 + srtJit * kHalfStep + kStep * iStep;

						// METHOD 1 - works but is temporal
						// METHOD 2
						float kFactor = kHalfStep + vairableJit * kStep * 1 + kStep * iStep;

						float3 positionVsNoZJit_Lerp = lerp(posVsStart, posVsEnd, kFactor);
						float3 prevPositionVsNoZJit_Lerp = lerp(prevPositionVSNoZJitNoXYJit_Start, prevPositionVSNoZJitNoXYJit_End, kFactor);
						int counter = iStep + (pos.x +/*  pos.y + */pSrt->m_frameNumber);

						float xs = counter % 2 < 1 ? 1.0f : -1.0f;
						//xs = 1;
						xs = pSrt->m_runtimeNeighborJitterFactor > 0 ? xs : 0;

						//float posVsHorSwapX = xs > 0 ? LaneSwizzle(positionVsNoZJit_Lerp.x, 0x1F, 0x00, 0x01);
						float posVsHorSwapY = positionVsNoZJit_Lerp.y;//LaneSwizzle(positionVsNoZJit_Lerp.y, 0x1F, 0x00, 0x08);
						float posVsHorSwapZ = positionVsNoZJit_Lerp.z; // LaneSwizzle(positionVsNoZJit_Lerp.z, 0x1F, 0x00, 0x08);

						///if (ys > 0)
						
						//positionVsNoZJit_Lerp = lerp(positionVsNoZJit_Lerp, float3(posVsHorSwapX, posVsHorSwapY, posVsHorSwapZ), ys * pSrt->m_runtimeNeighborJitterFactor);
						//positionVsNoZJit_Lerp.x = xs > 0 ? LaneSwizzle(positionVsNoZJit_Lerp.x, 0x1F, 0x00, 0x01) : positionVsNoZJit_Lerp.x;

						//froxelLineVs = normalize(positionVsNoZJit_Lerp);

						//cosSinRefAngle = normalize(float2(dot(positionVsNoZJit_Lerp, pSrt->m_samplingPlaneZVs), dot(positionVsNoZJit_Lerp, pSrt->m_samplingPlaneXVs)));




						float brightnessTmp = 0;
						DoShadowCacheLighting(
							// data that changes for prev frame
							pSrt->m_shadowSlicesRO, pSrt->m_volumetricsShadowCacheRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
							pSrt->m_samplingPlaneZVs, pSrt->m_samplingPlaneXVs, pSrt->m_lightDirectionVs, froxelLineVs, cosSinRefAngle, pSrt->m_startAngle, pSrt->m_angleStepInv, pSrt->m_numAngleSlicesMinus1, pSrt->m_lookingAtLight, pSrt->m_camPos,
							// same for different frames
							pSrt->m_shadowMaxDistAlongLightDir, pSrt->m_nearBrightness, pSrt->m_nearBrightnessAdd, pSrt->m_nearBrightnessDistSqr, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier, pSrt->m_scatterParams,

							positionVsNoZJit_Lerp, posNoJitWs, inScatterFactor, densityAdd, brightnessTmp);


						#if FROXELS_NEWLY_REVEALED_LIST || 1

						if (weightBias >= 0.5)
						{
							float brightnessTmpPrev = 0;

							DoShadowCacheLighting(
								// data that changes for prev frame
								pSrt->m_shadowSlicesPrevRO, pSrt->m_volumetricsShadowCachePrevRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
								pSrt->m_samplingPlaneZVsPrev, pSrt->m_samplingPlaneXVsPrev, pSrt->m_lightDirectionVsPrev, froxelLineVsPrev, cosSinRefAnglePrev, pSrt->m_startAnglePrev, pSrt->m_angleStepInvPrev, pSrt->m_numAngleSlicesMinus1Prev, pSrt->m_lookingAtLightPrev, pSrt->m_camPosPrev,
								// same for different frames
								pSrt->m_shadowMaxDistAlongLightDir, pSrt->m_nearBrightness, pSrt->m_nearBrightnessAdd, pSrt->m_nearBrightnessDistSqr, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier, pSrt->m_scatterParams,

								//positionVS, 
								prevPositionVsNoZJit_Lerp,
								posNoJitWs, inScatterFactor, densityAdd, brightnessTmpPrev);


							brightnessTmp = (brightnessTmp + brightnessTmpPrev) / 2;
						}
						#endif

						// METHOD 2

						float factor = (iStep < kNumDynamicSteps / 2) ? (iStep + 1) : (kNumDynamicSteps - iStep);
						
						float otherBrightnessX = LaneSwizzle(brightnessTmp.x, 0x1F, 0x00, 0x01);
						float brightnessDif = otherBrightnessX - brightnessTmp;

						// extrapolated blur

						float k = pSrt->m_runtimeNeighborJitterFactor;
						float blurredBrightness = brightnessTmp * k + (1.0 - k) / 2 * otherBrightnessX + (1.0 - k) / 2 * saturate(brightnessTmp - brightnessDif);

						//brightnessTmp = blurredBrightness; // xs > 0 ? brightnessTmp * 0.75 + 0.25 * LaneSwizzle(brightnessTmp.x, 0x1F, 0x00, 0x01) : brightnessTmp;


						brightness += brightnessTmp * factor;
						//brightnessSqr += brightnessTmp * brightnessTmp;

					}
					// METHOD 2
					brightness /= (kNumDynamicSteps / 2);
					brightness /= (kNumDynamicSteps / 2 + 1);
					//brightness = 1;

					//brightness = 0.0f;
				}
				else
				{
					for (int iStep = 0; iStep < kNumDynamicSteps; ++iStep)
					{
						// need interpolated view space position

						// METHOD 0
						// float kFactor = kHalfStep / 2 + srtJit * kHalfStep + kStep * iStep;

						// METHOD 1 - works but is temporal
						#if USE_METHOD1
						float kFactor = kHalfStep + /*srtJit * kHalfStep + */ kStep * iStep;
					
						#else
						// METHOD 2
						float kFactor = kHalfStep / 2 + srtJit * kHalfStep + kStep * iStep;
						#endif

						float3 positionVsNoZJit_Lerp = lerp(positionVSZJit_Start, positionVSZJit_End, kFactor);

						float brightnessTmp = 0;
						DoShadowCacheLighting(
							// data that changes for prev frame
							pSrt->m_shadowSlicesRO, pSrt->m_volumetricsShadowCacheRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
							pSrt->m_samplingPlaneZVs, pSrt->m_samplingPlaneXVs, pSrt->m_lightDirectionVs, froxelLineVs, cosSinRefAngle, pSrt->m_startAngle, pSrt->m_angleStepInv, pSrt->m_numAngleSlicesMinus1, pSrt->m_lookingAtLight, pSrt->m_camPos,
							// same for different frames
							pSrt->m_shadowMaxDistAlongLightDir, pSrt->m_nearBrightness, pSrt->m_nearBrightnessAdd, pSrt->m_nearBrightnessDistSqr, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier, pSrt->m_scatterParams,

							positionVsNoZJit_Lerp, posNoJitWs, inScatterFactor, densityAdd, brightnessTmp);

						// METHOD 1
						#if USE_METHOD1
						float factor = 1; // (iStep < kNumDynamicSteps / 2) ? (iStep + 1) : (kNumDynamicSteps - iStep);
						#else

						// METHOD 2
						float factor = (iStep < kNumDynamicSteps / 2) ? (iStep + 1) : (kNumDynamicSteps - iStep);
						#endif
						brightness += brightnessTmp * factor;
						//brightnessSqr += brightnessTmp * brightnessTmp;

					}

					// METHOD 1
					#if USE_METHOD1
					brightness /= kNumDynamicSteps;
					if (weightBias == 1)
					{
						//brightness = 1.0;
					}
					#else
					// METHOD 2
					brightness /= (kNumDynamicSteps / 2);
					brightness /= (kNumDynamicSteps / 2 + 1);
					#endif
				}
				#else

				int kNumDynamicSteps = pSrt->m_numSunLightSteps;
				float viewSpaceStep = (viewZNoJit_End - viewZNoJit_Start) / kNumDynamicSteps;

				float kHalfStep = 1.0 / (2 * kNumDynamicSteps);
				float kStep = 1.0 / kNumDynamicSteps;

				float3 froxelLineVs = normalize(positionVSNoZJit_Start);

				float2 cosSinRefAngle = normalize(float2(dot(froxelLineVs, pSrt->m_samplingPlaneZVs), dot(froxelLineVs, pSrt->m_samplingPlaneXVs)));

				for (int iStep = 0; iStep < kNumDynamicSteps; ++iStep)
				{
					// need interpolated view space position
					float kFactor = kHalfStep / 2 + srtJit * kHalfStep + kStep * iStep;

					float3 positionVsNoZJit_Lerp = lerp(positionVSNoZJit_Start, positionVSNoZJit_End, kFactor);


					DoShadowCacheLighting(
						// data that changes for prev frame
						pSrt->m_shadowSlicesRO, pSrt->m_volumetricsShadowCacheRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
						pSrt->m_samplingPlaneZVs, pSrt->m_samplingPlaneXVs, pSrt->m_lightDirectionVs, froxelLineVs, cosSinRefAngle, pSrt->m_startAngle, pSrt->m_angleStepInv, pSrt->m_numAngleSlicesMinus1, pSrt->m_lookingAtLight, pSrt->m_camPos,
						// same for different frames
						pSrt->m_shadowMaxDistAlongLightDir, pSrt->m_nearBrightness, pSrt->m_nearBrightnessAdd, pSrt->m_nearBrightnessDistSqr, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier, pSrt->m_scatterParams,

						positionVsNoZJit_Lerp, posNoJitWs, inScatterFactor, densityAdd, brightness);

				}
				brightness /= kNumDynamicSteps;
				brightness *= fogStartDistFactor;

				#endif
			#endif // end if use dynamic number of steps

#if 0
			// use shadow texture
			ShadowMapSetup hSetup = pSrt->m_shadowSetup;
			float u = dot(posNoJitWs - hSetup.m_corner, hSetup.m_x);
			float v = dot(posNoJitWs - hSetup.m_corner, hSetup.m_z);
			float z = dot(posNoJitWs - hSetup.m_corner, hSetup.m_y);
			z = 1.0 - z;

			float shadowValue = pSrt->m_shadowMap.SampleLevel(pSrt->m_pointSampler, float2(u, v), 0).r;

			brightness = z > shadowValue;

			if (u < 0 || u > 1 || v < 0 || v > 1 || z < 0 || z > 1)
				brightness = 0;
			//else
			//	brightness = pow(z, 2);
#endif
		#endif

		#if 0
		//for (int iShadowStep = 0; iShadowStep < numShadowSteps; ++iShadowStep)
		{
			//densityAdd = densityAdd / (1.0 + iSlice / 4.0) ;
			densityAdd = 0;// max(densityAdd - 1, 0);


			float3 samplingPlaneZVs = pSrt->m_samplingPlaneZVs;
			float3 samplingPlaneXVs = pSrt->m_samplingPlaneXVs;

			float3 N = normalize(positionVS);

			float2 cosSinRefAngle = normalize(float2(dot(N, samplingPlaneZVs), dot(N, samplingPlaneXVs)));

			//float angle = Acos(Clamp(cosSinRefAngle.Y() > 0.0f ? float(cosSinRefAngle.X()) : -float(cosSinRefAngle.X()), -0.9999999f, 0.9999999f));
			float angle = (cosSinRefAngle.y > 0.0f ? 1.0 : -1.0) * acos(clamp(float(cosSinRefAngle.x), -0.9999999f, 0.9999999f));

			if (pSrt->m_lookingAtLight)
			{
				if (cosSinRefAngle.x > 0)
				{

				}
				else
				{
					angle = angle - (cosSinRefAngle.y > 0.0f ? 1.0 : -1.0) * 3.14159265358979323846;
				}
				//angle = (cosSinRefAngle.Y() > 0.0f ? 1.0 : -1.0) * Asin(Clamp(float(cosSinRefAngle.Y()), -0.9999999f, 0.9999999f));
			}

			float sampleArrayIndex = clamp((angle - pSrt->m_startAngle) * pSrt->m_angleStepInv, 0.0f, float(pSrt->m_numAngleSlicesMinus1));
			int iAngleSlice = sampleArrayIndex + 0.5;



			float3 sampleDirVs = pSrt->m_shadowSlicesRO[iAngleSlice].m_sampleDirectionVs;

			float3 shadowMapReferenceVectorVs = pSrt->m_shadowSlicesRO[iAngleSlice].m_shadowMapReferenceVectorVs;

			// find top and bottom of the intersection of this point and plane
			float distToClipPlane = dot(shadowMapReferenceVectorVs, positionVS);
			float3 refray0 = float3(pSrt->m_shadowSlicesRO[iAngleSlice].m_sliceVector0Vs.xy, 1.0f);
			float3 refray1 = float3(pSrt->m_shadowSlicesRO[iAngleSlice].m_sliceVector1Vs.xy, 1.0f);

			// this math calculates how much we need to travel along top and bottom slice vectors to get to the same distance as the sample point
			float viewZ0 = distToClipPlane / dot(shadowMapReferenceVectorVs, refray0);
			float viewZ1 = distToClipPlane / dot(shadowMapReferenceVectorVs, refray1);


			// now find how far along it is along reference vector
			float distAlongreference = dot(positionVS, shadowMapReferenceVectorVs);

			float distAlongSamplingDir = distAlongreference * pSrt->m_shadowSlicesRO[iAngleSlice].m_sampleDirToReferenceDirRatio;
			float shadowDepthStep = distAlongSamplingDir / pSrt->m_shadowSlicesRO[iAngleSlice].m_logStep;


			if (pSrt->m_lookingAtLight)
			{
				// the logic is a little different for when we look at the light

				// first subtract the distance along reference of start point of the slice
				distAlongreference -= pSrt->m_shadowSlicesRO[iAngleSlice].m_logStep;

				// in this case the step is stored in m_sampleDirToReferenceDirRatio
				shadowDepthStep = distAlongreference / pSrt->m_shadowSlicesRO[iAngleSlice].m_sampleDirToReferenceDirRatio;
			}


			uint2 texDim = uint2(128, 512);
			pSrt->m_volumetricsShadowCacheRO.GetDimensionsFast(texDim.x, texDim.y);


			float yCoord = (shadowDepthStep + 0.5) / float(texDim.y);
			float xCoord = (iAngleSlice + 0.5f) / float(texDim.x);

			float shadowPosAlongLightDir = pSrt->m_volumetricsShadowCacheRO.SampleLevel(pSrt->m_pointSampler, float2(xCoord, yCoord), 0).r;
			uint2 shadowCoords = uint2(iAngleSlice, shadowDepthStep);
			//float shadowPosAlongLightDir = pSrt->m_volumetricsShadowCacheRO[shadowCoords].r;



			//float expMap = pSrt->m_volumetricsShadowCacheRO.SampleLevel(pSrt->m_pointSampler, float2(xCoord, yCoord), 0).g;
			//float shadowPosAlongLightDir = pSrt->m_volumetricsShadowCacheRO.SampleLevel(pSrt->m_linearSampler, float2(xCoord, yCoord), 0);
			float expMap = pSrt->m_volumetricsShadowCacheRO.SampleLevel(pSrt->m_linearSampler, float2(xCoord, yCoord), 0).g;

			float3 lightDirectionVs = pSrt->m_lightDirectionVs;

			float pointPosAlongLightDir = dot(positionVS, lightDirectionVs);

			float alwaysLit = saturate((posWs.y - pSrt->m_shadowMaxDistAlongLightDir) / 8.0);
			float xzDistSqr = dot((posWs - pSrt->m_camPos).xz, (posWs - pSrt->m_camPos).xz);
			float nearDistSqr = pSrt->m_nearBrightnessDistSqr;
			float sunLightNearBoost = lerp(1.0, pSrt->m_nearBrightness, saturate((nearDistSqr - xzDistSqr) / 32.0));

			// normal test:
			float dif = shadowPosAlongLightDir - pointPosAlongLightDir;

			pointPosAlongLightDir = clamp(pointPosAlongLightDir, -1.0 / (pSrt->m_expShadowMapMultiplier * 2.0), 1.0 / (pSrt->m_expShadowMapMultiplier * 2.0));

			float pointPosAlongLightDirForExp = pointPosAlongLightDir * (pSrt->m_expShadowMapMultiplier * 2.0); // -1.0 .. 1.0 range

																											  // exp map test
			float expVisibility = clamp(expMap * exp(-pointPosAlongLightDirForExp * pSrt->m_expShadowMapConstant), 0.0f, 1.0f) * sunLightNearBoost;
			#if CACHED_SHADOW_EXP_BASE_2
				expVisibility = clamp(expMap * exp2(-pointPosAlongLightDirForExp * pSrt->m_expShadowMapConstant), 0.0f, 1.0f) * sunLightNearBoost;
			#endif

			expVisibility *= expVisibility; // get a tighter curve

			//expVisibility = max(alwaysLit, expVisibility);

#if FROXELS_ALLOW_DEBUG
			pSrt->m_destFogFroxelsDebug[froxelCoord].xyzw = float4(/*xCoord, yCoord*/shadowCoords, /*sampleArrayIndex*/expVisibility, pointPosAlongLightDirForExp);
			pSrt->m_destFogFroxelsDebug1[froxelCoord].xyzw = float4(/*positionVS.xyz*/pointPosAlongLightDir, shadowPosAlongLightDir, expMap, /*angle*//*pointPosAlongLightDirForExp*/
				#if CACHED_SHADOW_EXP_BASE_2
					exp2(-pointPosAlongLightDirForExp * pSrt->m_expShadowMapConstant)
				#else
					exp(-pointPosAlongLightDirForExp * pSrt->m_expShadowMapConstant)
				#endif
				);
#endif
			float depthTest = (dif > 0.0);

			float depthTestExp = expVisibility;

			float3 camToFroxelVs = normalize(positionVS);

			float inScatterFactor = 1;
			#if !FROXELS_SUN_SCATTERING_IN_ACCUMULATE
				inScatterFactor = SingleScattering(camToFroxelVs, pSrt->m_lightDirectionVs, pSrt->m_scatterParams.x, pSrt->m_scatterParams.y);
			#endif

			float densityBoost = abs(0.5f - expVisibility) * 2.0; // varies from 0 to 1. 0 in between shadow or no shadow, 1 when purely in shadow or out of shadow

			densityBoost = densityBoost;

			densityBoost = 1.0 - densityBoost;

			float oneSliceDepth = 1;
#if UseExponentialDepth
			oneSliceDepth = DepthDerivativeAtSlice(sliceCoord);
#endif

			densityBoost = densityBoost > 0.01;

			densityAdd = max(densityAdd, densityBoost * pSrt->m_varianceFogDensity * 1.0 / oneSliceDepth); // fill in 1m of space

			densityAdd = min(densityAddTotal, densityAdd);

			densityAddTotal -= densityAdd;

			//densityBoost = densityBoost > 0.1;
			//depthTestExp = max(densityBoost, depthTestExp);
			densityBoost = 1.0;
			//depthTestExp = pow(depthTestExp, 1/5.0);
			//brightness += depthTest * 1.0f / numShadowSteps;

			//inScatterFactor = inScatterFactor > 0.05;

#if USE_EXPONENT_MAP_IN_CACHED_SHADOW
			brightness += inScatterFactor * depthTestExp * 1.0f / numShadowSteps;
#else
			brightness += inScatterFactor * depthTest * 1.0f / numShadowSteps;
#endif

		}
		#endif
#else

		int numShadowSteps = 1;
		for (int iShadowStep = 0; iShadowStep < numShadowSteps; ++iShadowStep)
		{
			float sliceCoord = iSlice + 0.5 / numShadowSteps + 1.0 / numShadowSteps * iShadowStep;

			if (numShadowSteps == 1)
			{
				sliceCoord = sliceCoord + pSrt->m_ndcFogJitter.z;// *(iSlice % 2 ? -1.0 : 1.0);
			}

			float viewZ = FroxelZSliceToCameraDistExp(sliceCoord, pSrt->m_fogGridOffset);

			float depthVS = viewZ;

			float2 pixelPosJit = pixelPos;
			//pixelPosJit.x += (iShadowStep - 1) * FogFroxelGridSizeNativeRes / 1920.0;

			float3 positionVS = float3(((pixelPosJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVS;
			//float ndcZ = GetNdcDepth(viewZ, pSrt->m_depthParams);
			//float4 positionVSH = mul(float4(ndcJitX, ndcJitY, ndcZ, 1), pSrt->m_mPInv);
			//float3 positionVS = positionVSH.xyz / positionVSH.w;


			//uint iCascadedLevel = GetCascadedShadowLevel(positionVS, pSrt->m_pConsts->m_cascadedLevelDist, pSrt->m_pConsts->m_clipPlaneVS);
			uint iCascadedLevel = GetCascadedShadowLevelSimple(depthVS, pSrt->m_volumetricsSrt->m_cascadedLevelDist);

			int baseMatrixIdx = iCascadedLevel * 6;

			float4 shadowTexCoord;
			shadowTexCoord.x = dot(float4(positionVS.xyz, 1.0), pSrt->m_volumetricsSrt->m_shadowMat[baseMatrixIdx + 0]);
			shadowTexCoord.y = dot(float4(positionVS.xyz, 1.0), pSrt->m_volumetricsSrt->m_shadowMat[baseMatrixIdx + 1]);
			shadowTexCoord.z = (float)iCascadedLevel;
			shadowTexCoord.w = dot(float4(positionVS.xyz, 1.0), pSrt->m_volumetricsSrt->m_shadowMat[baseMatrixIdx + 2]);
			shadowTexCoord.xy = saturate(shadowTexCoord.xy);

			float depthZ = pSrt->m_volumetricsSrt->m_shadowBuffer.SampleLevel(pSrt->m_linearSampler, shadowTexCoord.xyz, 0);
			float depthTest = iCascadedLevel < 4 ? shadowTexCoord.w < depthZ : 1;

			brightness += depthTest * 1.0f / numShadowSteps;
		}
#endif

		//if (pSrt->m_froxelPropertiesUsage > 0)
		{
			//float2 properties = pSrt->m_srcPropertiesFroxels[froxelCoord];

			//if (properties.x > 0.1)
			//	brightness = prevBrightness;
		}

#if USE_FOG_PROPERTIES

		// we disable sun brightness when convergance weight is very fast
		//brightness *= saturate(1.0 - pSrt->m_srcPropertiesFroxels[froxelCoord].x);

		// closer it is to shadowed wall, less sun it gets
		//brightness *= saturate(1.0 - pSrt->m_srcPropertiesFroxels[froxelCoord].y * 16.0);

#endif

		float3 lightColor = pSrt->m_lightColorIntensity.rgb;
#if PREMULTIPLY_DENSITY
		float3 colorAdd = lightColor * brightness * destVal.a * pSrt->m_sunLightContribution;
#else
		float3 colorAdd = lightColor * brightness * pSrt->m_sunLightContribution;
#endif

#if FROXELS_USE_WATER
		colorAdd = lerp(colorAdd, colorAdd * pSrt->m_underWaterSunIntensity, regionBlend);
#endif

		float causticsMult = pSrt->m_causticsTexture.SampleLevel(SetWrapSampleMode(pSrt->m_linearSampler), float3(posNoJitWs.xz / 10.0f, pSrt->m_causticsZ), 0.0f).x;
		//colorAdd *= causticsMult;

		// for water we unpack and pack it back because we need access to base ambient
#if FROXELS_USE_WATER
		colorAdd += waterColorAdd;

		float destinationDensity = UnpackFroxelDensity(FogDensityFromData(destVal), pSrt->m_densityUnpackFactor);
		float kBlendInMeters = pSrt->m_waterDensityBlendInRange;
		float heightUnderWater = regionBlend > 0 ? max(0, pSrt->m_startHeight - posNoJitWs.y) : 0.0f;

		// version purely with constant

		// we artificially shift the water blend in start by blend in amount so that by the time we reach water surface we will be fully blended in
		heightUnderWater = max(-kBlendInMeters, pSrt->m_startHeight - posNoJitWs.y + kBlendInMeters); //  we also shift by blend in range so we can store a negative value

		valueToStore = regionBlend > 0 ? saturate((heightUnderWater + kBlendInMeters) / 16.0) : 0.0f; // we store how deep we are if we are under water. offset by kBlendInMeters, so we have some info about how much above water we are too
		valueToStore = regionBlend;

		float waterDensityFactor = saturate(max(0, heightUnderWater) / kBlendInMeters) * regionBlend;
		#if SPLIT_FROXEL_TEXTURES
			// we don't add sunlight since we store sun intensity in separate texture
			// so we can't subtract a particular channel of sun color
		#else

		#endif

		
		// we blend in into water density by the time we are kBlendInMeters under water
		////
		
		#if FROXELS_UNDER_WATER_REGION_ADD_DENSITY
		destinationDensity = lerp(destinationDensity, pSrt->m_underWaterDensity * 0.1, waterDensityFactor);
		#endif

		FogDensityFromData(destVal) = PackFroxelDensity(destinationDensity, pSrt->m_densityPackFactor);
		
#else
		//colorAdd = float3(0, 0, 0);

		//destVal.rgb = destVal.rgb * brightness;
		// note because this pass combines light and temporal, technically we would need to do 
		// saturate of the value since it would have been stored and loaded in separate passes
		// but this allows us to have value > than 1 that will be part of temporal combine
		// and actually will allow us bigger range

		#if SPLIT_FROXEL_TEXTURES
			// we don't add sunlight since we store sun intensity in separate texture
		#else
			
		#endif


		//destVal.a += densityAdd / FROXEL_GRID_DENSITY_FACTOR;

		//destVal.a *= densityFactor;
#endif

		#if SPLIT_FROXEL_TEXTURES
			newMisc.x = brightness; // this value could be > 1.0 because of additional adjustments like near brightness
		#endif

		FogDensityFromData(destVal) = FroxelDensityAddUnpackedValueToPacked(FogDensityFromData(destVal), pSrt->m_sunAddlDensity * brightness, pSrt->m_densityPackFactor);

		
#else
	
	// no shadow checks at all. just check if inside of the sun cone

	float3 camToFroxelVs = normalize(positionVs_Jit0); // calculate it once

	float3 sunPos = pSrt->m_lightDirectionVs * 512.0f;
	float3 sunToFroxelVs = normalize(positionVs_Jit0 - sunPos);
	float3 sunToCamera = -pSrt->m_lightDirectionVs;

	float cosFroxelToCamFromSun = dot(sunToFroxelVs, sunToCamera);
	float inScatterFactor = 1;
	#if !FROXELS_SUN_SCATTERING_IN_ACCUMULATE
		inScatterFactor = SingleScattering(camToFroxelVs, pSrt->m_lightDirectionVs, pSrt->m_scatterParams.x, pSrt->m_scatterParams.y);
	#endif
	float coneFactor = saturate((cosFroxelToCamFromSun - coneAngleCos) * 1024.0f);  // positive if inside

#if !SPLIT_FROXEL_TEXTURES // done in accumulation
	inScatterFactor *= coneFactor;
#endif

	//inScatterFactor = 1;
	#if !FOG_TEXTURE_DENSITY_ONLY
	float3 colorAdd = inScatterFactor * 0.2f * pSrt->m_lightColorIntensity.rgb * pSrt->m_sunLightContribution;
	destVal.rgb = saturate(FroxelColorAddUpackedValueToPacked(destVal.rgb, colorAdd, pSrt->m_fogExposure));
	#endif

	newMisc.x = pSrt->m_sunBeyondEvaluation; // brightness

	#if FROXELS_USE_WATER
		
		float destinationDensity = UnpackFroxelDensity(FogDensityFromData(destVal), pSrt->m_densityUnpackFactor);
		float kBlendInMeters = pSrt->m_waterDensityBlendInRange;
		float heightUnderWater = regionBlend > 0 ? max(0, pSrt->m_startHeight - posNoJitWs.y) : 0.0f;

		// version purely with constant

		// we artificially shift the water blend in start by blend in amount so that by the time we reach water surface we will be fully blended in
		heightUnderWater = max(-kBlendInMeters, pSrt->m_startHeight - posNoJitWs.y + kBlendInMeters); //  we also shift by blend in range so we can store a negative value

		valueToStore = regionBlend > 0 ? saturate((heightUnderWater + kBlendInMeters) / 16.0) : 0.0f; // we store how deep we are if we are under water. offset by kBlendInMeters, so we have some info about how much above water we are too
		valueToStore = regionBlend;

		float waterDensityFactor = saturate(max(0, heightUnderWater) / kBlendInMeters) * regionBlend;
		#if SPLIT_FROXEL_TEXTURES
			// we don't add sunlight since we store sun intensity in separate texture
			// so we can't subtract a particular channel of sun color
		#else

		#endif

		
		// we blend in into water density by the time we are kBlendInMeters under water
		////
		
		#if FROXELS_UNDER_WATER_REGION_ADD_DENSITY
		destinationDensity = lerp(destinationDensity, pSrt->m_underWaterDensity * 0.1, waterDensityFactor);
		#endif

		FogDensityFromData(destVal) = PackFroxelDensity(destinationDensity, pSrt->m_densityPackFactor);
		
	#endif

#endif
		
		//TODO: I don't know yet how in this shader to make it light the same way as previous
		//if (iSlice == lastSlice)
		{
			// copy this value to the last slice too
			//pSrt->m_destFogFroxels[uint3(froxelCoord.xy, froxelCoord.z + 1)] = destVal;
		}

		//prevBrightness = brightness;
		//brightnessRunningAverage = brightnessRunningAverage * 0.7 + brightness * 0.3;

		//brightnessAccum = clamp(brightnessAccum + brightness / 5, 0, 1);
		
		//FogDensityFromData(destVal) *= pSrt->m_fogOverallDensityMultiplier;
		
		#if !DO_NOISE_IN_FOG_CREATION
			SamplerState linSampleWrap = SetWrapSampleMode(pSrt->m_linearSampler);
			float noise = pSrt->m_noiseTexture.SampleLevel(linSampleWrap, float3(posNoJitWs.x + pSrt->m_noiseOffset.x, posNoJitWs.y + pSrt->m_noiseOffset.y, posNoJitWs.z + pSrt->m_noiseOffset.z) * pSrt->m_noiseScale, 0).z;
			float finalNoise = pSrt->m_noiseBlendScaleRangeMin; // something like 0.5, meaning noise can't go lower than that
			float maxValue = pSrt->m_noiseBlendScaleRangeMax; // noise can highlight some places up
			finalNoise = max(pSrt->m_noiseBlendScaleMin,  finalNoise + noise * (maxValue - pSrt->m_noiseBlendScaleRangeMin));

			destVal.a *= finalNoise;
		#endif

		// with 2 samples:
		//	0.5 we see some jitter. still might be ok for sharp movements
		//	0.25 seems to not have jitter, see some artifacts on fast movements. seems pretty goood with rotation artifacts
		//	0.4 also seems good with removing artifacts
		//	0.33 seems to have no jitter close by. some artifacts on movement.


		float startWeight = 0.05 + pSrt->m_addlConvergence;
		float newWeight = saturate(startWeight + weightBias);


		float densityDif = abs(FogDensityFromData(prevVal) - FogDensityFromData(destVal));
		float differenceScale = saturate(densityDif / 0.01);


		//newWeight = newWeight + (1.0 - newWeight) * differenceScale;
		

		const float kFroxelHalfUvZ = 0.5f / 64.0f;
		const float kFroxelHalfUvZInv = 64.0 * 2;

#if !FROXELS_DYNAMIC_RES
		const float kFroxelHalfUvX = 0.5f / float(NumFroxelsXRounded_F);
		const float kFroxelHalfUvY = 0.5f / float(NumFroxelsYRounded_F);

		const float kFroxelHalfUvXInv = float(NumFroxelsXRounded_F) * 2;
		const float kFroxelHalfUvYInv = float(NumFroxelsYRounded_F) * 2;
		

#else
		const float kFroxelHalfUvX = 0.5f * pSrt->m_numFroxelsXInv;
		const float kFroxelHalfUvY = 0.5f * (pSrt->m_numFroxelsXInv * 16.0 / 9.0);

		const float kFroxelHalfUvXInv = pSrt->m_numFroxelsXY.x * 2;
		const float kFroxelHalfUvYInv = pSrt->m_numFroxelsXY.y * 2;
#endif
		float awayDistX0 = saturate((kFroxelHalfUvX - uv3d.x) * kFroxelHalfUvXInv);
		float awayDistY0 = saturate((kFroxelHalfUvY - uv3d.y) * kFroxelHalfUvYInv);
		float awayDistZ0 = saturate((kFroxelHalfUvZ - uv3d.z) * kFroxelHalfUvZInv);

		float awayDistX1 = saturate((uv3d.x - (1.0 - kFroxelHalfUvX)) * kFroxelHalfUvXInv);
		float awayDistY1 = saturate((uv3d.y - (1.0 - kFroxelHalfUvY)) * kFroxelHalfUvYInv);

		float awayDist =  saturate((awayDistX0 + awayDistY0 + awayDistZ0 + awayDistX1 + awayDistY1) * (0.75 + 0.5 * ((pos.x + pos.y + iSlice) % 2)));

		newWeight = awayDist * (1.0 - newWeight) + newWeight;

		#if USE_FOG_PROPERTIES

		// we store convergance rate to speed up convergance in certain areas. for example flickering runtime lights

		
		
		//destVal.r = prevProperties;

		//newWeight = saturate(newWeight + newProperties);

		
		#endif
		// y component no longer exists, is in different texture
		//if (int(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x) != int(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].y))
		//{
		//newWeight = 1.0;
		//destVal.xy = float2(1, 1);
		//}

		// this could be behind a feature because we only do this when we are allowing refresh of froxels behind geo
		#if !FROXEL_EARLY_PASS
		if (lastFroxelTouchedByGeo > 128)
		{
			// special encoding for froxels that are forced to update really fast behind geo

			// in that encoding the value is last slice to be sampled + 128
			// so we test if our slice is > than stored value - 128

			if (iSlice > lastFroxelTouchedByGeo - 128)
			{
				newWeight = 1.0;
			}
		}
		#endif

		#if DISABLE_TEMPORAL_IN_FOG
		newWeight = 1.0;
		#endif


		//newWeight = 1.0;

		float newWeightBeforeMisc = newWeight;

#if USE_FOG_CONVERGE_BUFFER
		float _framesInHistory = 255 * prevVal.y + 1.0;
		// 1 frame - weight 0.5
		// 2 frames : new weight = 0.3
		// 3 frames : new weight = 0.25
		// 20 frames : new weight 0.05
		
		//newWeight = max(newWeight, 1.0 / (1.0 + _framesInHistory));

		//newWeight = saturate(newWeight + (pSrt->m_debugControls2.z > 0 ? 0 : prevVal.y * pSrt->m_revealedDataConvergeFactor));
#endif
/*
		if (prevFrameNotVisible)
		{
			newWeight = 1.0;

			destVal = float4(1.0, 0, 0, 0.1);
		}

		if (prevFrameLastVisible)
		{
			newWeight = 1.0;

			destVal = float4(0.0, 1.0, 0, 0.1);
		}
		
		if (prevPointVisible)
		{
			newWeight = 1.0;

			destVal = float4(0.0, 0.0, 1.0, 0.1);
		}

		newWeight = 1.0;
*/
		//newWeight = 0.1;
		FogDensityFromData(resultValue) = PackFroxelDensity(
			UnpackFroxelDensity(FogDensityFromData(destVal), pSrt->m_densityUnpackFactor) * newWeight + UnpackFroxelDensity(FogDensityFromData(prevVal), pSrt->m_densityUnpackFactorPrev) * (1.0 - newWeight)
			, pSrt->m_densityPackFactor);

		// density converge guarantee


		
		#if !FOG_TEXTURE_DENSITY_ONLY
		float maxChannel = max(max(prevVal.r, prevVal.g), prevVal.b);
		float maxChannelCompressed = sqrt(maxChannel);
		float maxNewChannel = max(max(destVal.r, destVal.g), destVal.b);
		float newWeightRgb = newWeight;
		if (maxNewChannel < 0.02)
		{
			// slower, easier to just get sqrt again instead of storing register
			//float maxChannelCompressed = max(max(prevValCompressed.r, prevValCompressed.g), prevValCompressed.b);

			// calulcate what is the min value we need to move by to actually make a difference
			float bitDif = (maxChannelCompressed * 255 + 1) / 255.0f;
			bitDif = bitDif * bitDif; // uncompressed

			bitDif = bitDif - maxChannel; // if we don't change by this much, we really are not going to change anything with temporal lerp

			
			float neededMaxNewChannel = maxNewChannel > maxChannel ? max(maxNewChannel, maxChannel + bitDif) : max(min(maxNewChannel, maxChannel - bitDif), 0);


			float minWeightToMakeDif = maxChannel > 0 ? bitDif / maxChannel : 0.05; // this works if we are blending to 0, basically what is the percentage of current value we need to take away

			float wantedPackedDif = (maxNewChannel - maxChannel);

			// the smaller this value, the better. as long as we end up making difference
			//bitDif *= 2.0f;
			int randomizer = froxelCoord.x + froxelCoord.y + froxelCoord.z + pSrt->m_frameNumber;
			bool allowWeightAdjust = (randomizer % 2) == 0;
			allowWeightAdjust = true;; // (froxelCoord.x % 2) == ((pSrt->m_frameNumber % 2)); // (froxelCoord.x % 2) == ((pSrt->m_frameNumber// ((froxelCoord.z + pSrt->m_frameNumber) % 4) == 0; // true; //  (froxelCoord.x % 2) == ((pSrt->m_frameNumber / 2) % 2);
			minWeightToMakeDif = (wantedPackedDif < 0 && allowWeightAdjust) ? saturate(bitDif / -wantedPackedDif) : 0.05; // how much of the current change we need to apply to make difference

			newWeightRgb = max(newWeight, minWeightToMakeDif);
		}


		//if (wantedPackedDif > 0 && wantedPackedDif < bitDif)
		//	newWeight = 0;

		resultValue.rgb = (FroxelColorAddPackedValueToPackedScaled(destVal.rgb, newWeightRgb, prevVal.rgb, (1.0 - newWeightRgb), pSrt->m_fogExposure, pSrt->m_fogExposurePrev));
		#endif

		#if SPLIT_FROXEL_TEXTURES
			float brightnessDiff = abs(prevMisc.x - newMisc.x);
			float sunShadowConvergeRate = newWeight;

			#if FROXELS_USE_DETAILED_SUN
			sunShadowConvergeRate = saturate((brightnessDiff - pSrt->m_sunConvergeDif) * pSrt->m_sunConvergeScale);
			sunShadowConvergeRate = /*pSrt->m_runtimeConvergeVal1 > 0 ? 1.0 : */ lerp(0.05, pSrt->m_sunMaxConvRate, sunShadowConvergeRate);
			//sunShadowConvergeRate = /*pSrt->m_runtimeTemporalOff > 0 ? 1.0 : */ min(pSrt->m_sunMaxConvRate, saturate(sunShadowConvergeRate /* + newWeight - 0.05*/));

			//shadowConvergeRate = 0.05;
			sunShadowConvergeRate = saturate(sunShadowConvergeRate + (newWeight - 0.05));
			#endif

			newMisc.x = lerp(prevMisc.x, newMisc.x, sunShadowConvergeRate); // this value could be > 1.0 because of additional adjustments like near brightness

			newMisc.x = PackFroxelColor(newMisc.x, 0);
		#endif

#if USE_FOG_PROPERTIES
		newProperties = ReadPackedValuePropertiesTexture(pSrt->m_destPropertiesFroxels, froxelCoord);

		// since the runtime grid could be offset differently we can't just read it, we need to sample it

		//float3 runtimeCoord;
		//runtimeCoord.xy = float2(froxelCoord.x + 0.5, froxelCoord.y  + 0.5) * float2(pSrt->m_numFroxelsXInv, pSrt->m_numFroxelsXInv * 16.0 / 9.0);
		//
		//float currLinearDepth = FroxelZSliceToCameraDistExp(iSlice + 0.5, pSrt->m_fogGridOffset); // going from current regular fog texture into linear space. note using regular offset, not runtime offset
		//
		//runtimeCoord.z = CameraLinearDepthToFroxelZSliceExpWithOffset(currLinearDepth, pSrt->m_runtimeGridOffset) / 64.0;
		//runtimeCoord.z = clamp(runtimeCoord.z, 0.0f, 1.0f);
		//
		//newProperties = SamplePackedValue(pSrt->m_srcPropertiesFroxels, pSrt->m_linearSampler, runtimeCoord);

#if FROXEL_RTL_V_1
		float unpackedNewPropertiesAlpha = newProperties.a;

#if SPLIT_FROXEL_TEXTURES
		float unpackedPrevPropertiesAlpha = prevMisc.y; // y component of misc stores temporal shadow of runtime lights
#else
		float unpackedPrevPropertiesAlpha = prevProperties.a;
#endif


		// this code is for temporal of shadow value
	
		// we temporaly blend the in shadow value
		// scenarios:
		// 1) something was not in shaodw and becomes in shadow : temporal will slowly make it darker
		// 2) something was in shadow and not anymore, temporal will smoothly make it lighter

		// 1) can happen if there was no light at all, it shows up and we need to immediately use shadow information from current frame
		// that's why we check if previously there was any light intensity or not

		//float shadowWeight = newWeight;

		//float prevLightMax = max(max(prevProperties.r, prevProperties.g), prevProperties.b);

		//shadowWeight = prevLightMax < 0.001 ? 1.0 : shadowWeight;
		//shadowWeight = 1;
		//shadowWeight = shadowWeight + (1.0 - shadowWeight) * abs(unpackedPrevPropertiesAlpha - newProperties.a) / 10.0;



		float3 newPropertiesUnpackedShadowed = lerp(UnpackFroxelColor(newProperties.rgb, pSrt->m_runTimeFogExposure), float3(0, 0, 0), newProperties.a);
		// try do temporal based on prev properties vs current value
		
		//float3 unpackedPrevRTLightShadowed = UnpackFroxelColor(prevProperties.rgb, pSrt->m_runTimeFogExposurePrev);

		// now we check how close that value is to ours. the current value is a good approximation of final value because we do a lot of samples
		// so threshold for difference should be low

		//float colorDiff = length(newPropertiesUnpackedShadowed - unpackedPrevRTLightShadowed);

		if (iSlice == 16 && pos.x == pSrt->m_numFroxelsXY.x / 2 && pos.y == pSrt->m_numFroxelsXY.y / 2)
		{
			//_SCE_BREAK();
		}

		float currentShadow = 1.0 - newProperties.a;
		float previousShadow = 1.0 - unpackedPrevPropertiesAlpha;
		float shadowDiff = abs(previousShadow - currentShadow);


		//float runtImeLightConvergeRate = saturate((colorDiff - pSrt->m_runtimeConvergeDif0) / max(pSrt->m_runtimeConvergeScale0, 0.000001));
		//runtImeLightConvergeRate = lerp(0.05, 1.0, runtImeLightConvergeRate);

		
		float shadowConvergeRate = saturate((shadowDiff - pSrt->m_runtimeConvergeDif1) / max(pSrt->m_runtimeConvergeScale1, 0.000001));
		shadowConvergeRate = /*pSrt->m_runtimeConvergeVal1 > 0 ? 1.0 : */ lerp(0.05, pSrt->m_runtimeTemporalMaxConvRate, shadowConvergeRate);
		shadowConvergeRate = /*pSrt->m_runtimeTemporalOff > 0 ? 1.0 : */ min(pSrt->m_runtimeTemporalMaxConvRate, saturate(shadowConvergeRate /* + newWeight - 0.05*/));

		//shadowConvergeRate = 0.05;
		shadowConvergeRate = saturate(shadowConvergeRate + (weightBias));

		if (weightBias == 0 && previousShadow == 1.0 && currentShadow < 0.5)
		{
			//_SCE_BREAK();
		}

#if DISABLE_TEMPORAL_IN_FOG
		shadowConvergeRate = 1.0;
#endif
		if (pSrt->m_frameNumber % 2)
		{
			//shadowConvergeRate = 1.0;
		}

		float temporalShadow = lerp(previousShadow, currentShadow, shadowConvergeRate);
		//float temporalShadow = previousShadow * (1 - shadowConvergeRate) + currentShadow * shadowConvergeRate;
		//temporalShadow = weightBias >= 1.0 ? 0 : temporalShadow;

		//newPropertiesUnpackedShadowed.rgb = lerp(unpackedPrevRTLightShadowed.rgb, newPropertiesUnpackedShadowed.rgb, runtImeLightConvergeRate);
		newPropertiesUnpackedShadowed.rgb = lerp(UnpackFroxelColor(newProperties.rgb, pSrt->m_runTimeFogExposure), float3(0, 0, 0), temporalShadow);

		// keep color the same, we will do shadowing in accumulate apss
		newPropertiesUnpackedShadowed.rgb = UnpackFroxelColor(newProperties.rgb, pSrt->m_runTimeFogExposure);
		
		//newMisc.x = 0.0;

		if (/*weightBias == 0 && */previousShadow == 1.0 && currentShadow < 0.5)
		{
			//_SCE_BREAK();
			//newPropertiesUnpackedShadowed.rgb = float3(1, 0, 0);

			//newMisc.x = 1.0;
		}
		//if (weightBias == 1)

		ulong exec = __s_read_exec();

		if (exec == 0xFFFFFFFFFFFFFFFF)
		{
			if (currentShadow == 1.0)
			{
				ulong shadowExec = __s_read_exec();

				if (shadowExec != exec)
				{
					// all threads active but some read bad shadow
					//newMisc.x = 1.0;
				}
			}

		}
#if !FROXEL_EARLY_PASS
		if (/*iSlice > 12 && weightBias == 1 && previousShadow > 0.5 lastSliceToEverBeSampled == 63*/ currentShadow == 1.0)
		{
			//_SCE_BREAK();
			//newPropertiesUnpackedShadowed.rgb = float3(1, 0, 0);

			ulong shadowExec = __s_read_exec();

			if (shadowExec != 0xFFFFFFFFFFFFFFFF)
			{
				//newMisc.x = 1.0;
			}
		}
#endif

		//newPropertiesUnpackedShadowed.rgb = FroxelColorAddPackedValueToPackedScaled(newProperties.rgb, newWeight, prevProperties.rgb, (1.0 - newWeight), pSrt->m_runTimeFogExposure, pSrt->m_fogExposurePrev);

#if DISABLE_TEMPORAL_IN_FOG
		//shadowWeight = 1;
#endif
		//shadowWeight = 1; // for now there is no temporal on runtime lights v1

	
		// store properties.a as temporal convergance rate

		// newWeight is >= 1.0 when we reveal new pixel



		float propertiesWeight = 1.0;
		//newProperties.rgb = FroxelColorAddPackedValueToPackedScaled(newProperties.rgb, propertiesWeight, prevProperties.rgb, (1.0 - propertiesWeight), pSrt->m_runTimeFogExposure, pSrt->m_runTimeFogExposurePrev);
		newProperties.rgb = newProperties.rgb;

		#if FROXELS_USE_WATER
			ApplyColorAbsorbtion(pSrt, pSrt->m_fogColor, pSrt->m_rtAbsorbCoefficients, min(viewZNoJit, pSrt->m_rtAbsorbMaxDist), regionBlend > 0, newPropertiesUnpackedShadowed.rgb);
		#endif

		newProperties.rgb = PackFroxelColor(newPropertiesUnpackedShadowed.rgb, pSrt->m_runTimeFogExposure); // this enables temporal
		newProperties.a = 1.0 - temporalShadow; // -1.0 / 255.0;
		#if SPLIT_FROXEL_TEXTURES
			newMisc.y = newProperties.a; // stores runtime shadow
			#if FROXELS_USE_WATER
				newMisc.z = sqrt(valueToStore);
				//newProperties.a = valueToStore; // value of under water stored with runtime lights
			#endif
		#endif

#else
	// this is deprecated case and will likely never be used
	#if SPLIT_FROXEL_TEXTURES
		// we don't have previous properites
		newMisc.y = newProperties.a; // stores runtime shadow
	#else
		newProperties.a = newProperties.a * newWeight + prevProperties.a * (1.0 - newWeight);
		newProperties.rgb = FroxelColorAddPackedValueToPackedScaled(newProperties.rgb, newWeight, prevProperties.rgb, (1.0 - newWeight), pSrt->m_fogExposure, pSrt->m_fogExposurePrev);
	#endif

#endif
	


		//resultValue.rgb += newProperties.rgb * 10;
#endif

		// this is good code we want to re-enable. this has to do with temporally blending newly revealed areas
#if USE_FOG_CONVERGE_BUFFER
		float newConverganceProperty = newWeight * 1.0 + (1.0 - newWeight) * prevVal.y;

		newConverganceProperty = ((newWeightBeforeMisc - startWeight) + prevVal.y) * pSrt->m_revealedDataConvergeFactor;

		int convergeRandomnessCounter = pos.x + pos.y;

		float randomnessFactor = 1; // 0.75 + (convergeRandomnessCounter % 2) * 0.5;

		//newConverganceProperty = newWeight;
		prevVal.y += (1.0 / 255) * randomnessFactor; // added frame of history

		//prevVal.y = prevVal.y - weightBias * 255;

		prevVal.y = weightBias >= 0.5 ? 0.0f : prevVal.y;

		//prevVal.y = (weightBias > 0.0  && weightBias < 0.9) ? 0.0f : 1;

		

		resultValue.y = saturate(prevVal.y);

		// only completely unused results
		//resultValue.y = ((classify == 16) ) ? 0.0 : 1.0;
		// partial results
		//resultValue.y = ((classify & 16) && (classify != 16)) ? 0.0 : 1.0;
		//resultValue.y = ((classify & 4)) ? 0.0 : 1.0;

		//resultValue.y = saturate(prevVal.y * pSrt->m_revealedDataConvergeFactor + weightBias); // prevVal.y / 2;// newConverganceProperty;
#endif
		// at this point we are done with all computations for resultValue

		//resultValue.a = destVal.a; // disables temporal on density
		#if USE_MANUAL_POW_FROXELS
		resultValueFinal = sqrt(FogDensityFromData(resultValue)); // this value will be written staright into texture without compression so we need to compress ourselves
		#else
		resultValueFinal = FogDensityFromData(resultValue);
		#endif

#if !DISABLE_TEMPORAL_IN_FOG
		uint prevValOrigUint = (prevValDensOrig + 0.5 / 255) * 255;
		uint destValOrigUint = (destValDensOrig + 0.5 / 255) * 255;
		uint newValOrigUint = (resultValueFinal + 0.5 / 255) * 255;

		//if ((/*pos.x + pos.y + */iSlice + pSrt->m_frameNumber) % 2)
		#if FROXELS_UNDER_WATER_REGION_ADD_DENSITY
		if (false)
		#else
		if (pSrt->m_densityConvergeGuarantee)
		#endif
		{
			if (newValOrigUint == prevValOrigUint  && /* prevValOrigUint > destValOrigUint*/ prevValDensOrig > destValDensOrig)
			{
				//if (prevValDensOrig > destValDensOrig)
				{
					resultValueFinal = (newValOrigUint - 1.1) / 255.0; // force to go down by 1 bit
				}
				//else
				{
					//resultValueFinal = (newValOrigUint + 1.9) / 255.0;
				}


				//resultValue.a = resultValueFinal * resultValueFinal;
				//resultValue.a -= 1.0 / 255.0f;
				//newMisc.x = 10000;
				//newMisc.y = 10000;
				//newProperties = float4(100, 0, 0, 1);
			}
		}
#endif


		#if FROXELS_USE_WATER && !SPLIT_FROXEL_TEXTURES
			newMisc.y = regionBlend;
		#endif
		/*
		if (iSlice > lastSliceToEverBeSampled)
		{
			// means we are beyond last touched slice + 1, so this slice should never be sampled at all
			// there are a couple of options we can do here.

			// if we have 1/16th of froxels updating behind geo we can just sample rev value and hopefully that value is ok
			// so we could just use prev value
			// we could use prev value avraged when revealing new froxels from camera turn 
			// so we fill in new froxels with some grey color (better than nothing and will not propagate a bright spot light color)
			// we could also just blend to ambient probe color

			// if we know we are updating 1/16th of sreen, we could sample closest updated value. that way we know we are getting newest data, might just be a little bit spacially off
			// that would require additional sample and we don't want it for now



			// this actually doesn't work out well, we end up with always wrong color

			#if CLEAR_REVEALED_FROXELS_BEHIND_GEO
				// we blend to 0 when are looking at newly revealed froxels behind geo
				float avg = (prevVal.x + prevVal.y + prevVal.z) / 3.0;
				resultValue = lerp(prevVal, float4(avg, avg, avg, prevVal.a), awayDist);
				//resultValue = lerp(prevVal, float4(0, 0, 0, 0), awayDist);
			#else
				// for the demo we keep this version. probably should enable it in general to avoid stale lighting
				resultValue = prevVal;
			#endif

		}
		*/
#if FROXELS_NO_UPDATE_BEHIND_GEO
		// we only store value in this if/else clause when we don't update anything behind geo
		StorePackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord, resultValue, resultValueFinal);
		//newProperties = float4(100, 0, 0, 1);
		
		#if OPTIMIZE_FROXEL_PROPERTIES
			StorePackedValuePropertiesTextureW(pSrt->m_destPropertiesFroxels, froxelCoord, newProperties);
		#elif USE_FOG_PROPERTIES
			StorePackedValuePropertiesTexture(pSrt->m_destPropertiesFroxels, froxelCoord, newProperties);
		#endif
		#if SPLIT_FROXEL_TEXTURES
			#if USE_MANUAL_POW_FROXELS
				//newMisc.x = 1.0;
				newMisc.xy = sqrt(newMisc.xy);
			#endif
			#if FROXELS_USE_WATER
				pSrt->m_destMiscFroxels[froxelCoord] = newMisc.xyz;
			#else
				pSrt->m_destMiscFroxels[froxelCoord] = newMisc.xy;
			#endif
		#elif FROXELS_USE_WATER
			pSrt->m_destMiscFroxels[froxelCoord] = newMisc.y;
		#endif
#endif
	}
	#if !FROXELS_NO_CHECKS && !FROXEL_EARLY_PASS
	else
	{
		// froxel out of range so we just keep the same value
		#if !FROXELS_NO_UPDATE_BEHIND_GEO
			FogDensityFromData(resultValue) = FogDensityFromData(prevVal);
			resultValueFinal = sqrt(FogDensityFromData(resultValue));

			#if USE_FOG_CONVERGE_BUFFER
			resultValue.y = prevVal.y;
			#endif

			#if !FOG_TEXTURE_DENSITY_ONLY
			// we still need to convert from old exposure to new in case it has changed
			resultValue.rgb = (FroxelColorAddPackedValueToPackedScaled(float3(0,0,0), 0.0, prevVal.rgb, (1.0), pSrt->m_fogExposure, pSrt->m_fogExposurePrev));
			#endif
			// actually we need to do something like this since exposure could be changing we need to update the value
			//resultValue.rgb = PackFroxelColor(UnpackFroxelColor(prevVal.rgb, pSrt->m_fogExposurePrev), pSrt->m_fogExposure);

			#if USE_FOG_PROPERTIES 
				#if !SPLIT_FROXEL_TEXTURES
					newProperties = prevProperties;
				#else
					// there is no such thing as previous properties, because rt light grif exists only on one frame
					newProperties = float4(0, 0, 0, 0);
				#endif
			#endif
			
			#if SPLIT_FROXEL_TEXTURES
				// prevMisc.x is unpacked already
					newMisc.xy = prevMisc.xy;;
					newMisc.x = PackFroxelColor(newMisc.x, 0);

					//newMisc.xy = 0;
			#endif

		#endif
	}
	#endif
	}
	
#if !FROXELS_NO_UPDATE_BEHIND_GEO
	// we only store value in this #if/#else clause when we don't update anything behind geo
	StorePackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord, resultValue, resultValueFinal);
	//newProperties = float4(100, 0, 0, 1);

	#if OPTIMIZE_FROXEL_PROPERTIES
		StorePackedValuePropertiesTextureW(pSrt->m_destPropertiesFroxels, froxelCoord, newProperties);
	#elif USE_FOG_PROPERTIES
		StorePackedValuePropertiesTexture(pSrt->m_destPropertiesFroxels, froxelCoord, newProperties);
	#endif
	#if SPLIT_FROXEL_TEXTURES
		if (iSlice > lastSliceToEverBeSampled)
		{
			if (froxelCoord.x == 122 && froxelCoord.y == 64 && froxelCoord.z == 24)
			{
				//_SCE_BREAK();
			}
		}

		#if USE_MANUAL_POW_FROXELS
			newMisc.xy = sqrt(newMisc.xy);
		#endif
		#if FROXELS_USE_WATER
			pSrt->m_destMiscFroxels[froxelCoord] = newMisc.xyz;
		#else
			pSrt->m_destMiscFroxels[froxelCoord] = newMisc.xy;
		#endif
	#elif FROXELS_USE_WATER
		pSrt->m_destMiscFroxels[froxelCoord] = newMisc.y;
	#endif
#endif

#endif
}


#if !FROXELS_DYNAMIC_RES

[NUM_THREADS(8, 8, 1)] //
void CS_RayTraceParticlesVerticalBlurFroxels(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
#if DO_BLUR
	uint2 groupPos = uint2(8, 8) * groupId;

	uint2 pos = groupPos + groupThreadId;

	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);

	// vertical blur

	for (int iSlice = 0; iSlice < numSlices; ++iSlice)
	{
		froxelCoord.z = iSlice;

		//float4 srcVal0 = pSrt->m_destFogFroxelsPrev[froxelCoord];
		//float4 srcValm1 = pSrt->m_destFogFroxelsPrev[uint3(froxelCoord.x, froxelCoord.y - 1, froxelCoord.z)];
		//float4 srcValp1 = pSrt->m_destFogFroxelsPrev[uint3(froxelCoord.x, froxelCoord.y + 1, froxelCoord.z)];

		float4 srcVal0 = pSrt->m_destFogFroxelsTemp[froxelCoord];
		float4 srcValm1 = pSrt->m_destFogFroxelsTemp[uint3(froxelCoord.x, froxelCoord.y - 1, froxelCoord.z)];
		float4 srcValp1 = pSrt->m_destFogFroxelsTemp[uint3(froxelCoord.x, froxelCoord.y + 1, froxelCoord.z)];

		float4 destVal = srcVal0 * BLUR_CENTER + srcValm1 * BLUR_SIDE + srcValp1 * BLUR_SIDE;

		pSrt->m_destFogFroxels[froxelCoord] = destVal;

	}
#endif
}


[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsTemporalCombineAndAccumulateFogFroxels(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
}



[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsClearUnusedFroxels(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 pos = groupPos + groupThreadId.xy;


	// march through all slices of froxel grid and accumulate results

	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);

	//#define ABSORPTION 1.030725


	int lastSliceToEverBeSampled = min(63, uint(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x) + 1); // last sample to be sampled is last froxel touched by geo + 1


	for (int iSlice = lastSliceToEverBeSampled+1; iSlice < 64; ++iSlice)
	{
		froxelCoord.z = iSlice;

		float4 resultValue = float4(1, 0, 0, 1);

		
		//resultValue = float4(.5, .5, .5, .5);
		pSrt->m_destFogFroxels[froxelCoord] = resultValue;
	}
}

[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsTemporalCombineFogFroxels(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{

}

[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsLightFroxelsWithRuntimeLights(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
}


#define FROXEL_PROBES_USE_OCCLUSION 0
#define FROXEL_PROBES_USE_DEPTH_VARIANCE_OCCLUSION 0

#define FROXEL_PROBES_USE_OCCLUSION_OLD 0 // the non scalar version
#define FROXEL_PROBES_USE_DEPTH_VARIANCE_OCCLUSION_OLD 0 // the non scalar version
#define FROXEL_PROBES_USE_MULTISAMPLING_OLD 0 // the non scalar version

#define FROXEL_CHECK_REGIONS 1
#define FROXEL_PROBES_USE_REGION_CULL (FROXEL_CHECK_REGIONS && 1)

#define USE_SMALL_PROBE 1

void FindProbeNearestNeighbors(int nodeTreeIndex, inout uint probeInfoList[kMaxNumProbeResults],

#if ENABLE_FEEDBACK_DEBUG
	inout uint probeDebugList[kMaxNumProbeResults],
#endif

	int probeTreeIndex, inout uint probeCount,
	in int rootIndex, in float3 centerPosLS, in float3 edgeLenLS,
	in float maxDistance, in float3 positionLS, in float3 position0LS, in float3 position1LS, in float3 occlusionTargetPtLS, in float3 positionWS, in float3 camWs, in float3 wsOffset,
	StructuredBuffer<uint2> probeNodeBuffer,
	StructuredBuffer<float4> probePosBuffer,
	VolumetricsFogSrt *pSrt, int iGlobalPlane, int iBestRegion, uint debugFroxel)
{
	int stackTop = 0;
	uint stackBuffer[kStackBufferSize];
	float maxDistSqr = maxDistance * maxDistance;
	

	if (all(abs(positionLS - centerPosLS) < edgeLenLS))
	{
		PushStack(stackTop, stackBuffer, rootIndex);

		while (stackTop > 0)
		{
			uint nodeIndex = PopStack(stackTop, stackBuffer);
			uint2 node = probeNodeBuffer[nodeIndex];

			float splitPos = asfloat(node.x);
			uint splitAxis = node.y & 0x03;
			uint hasLeftChild = node.y & 0x04;
			uint rightChild = node.y >> 3u;

			float planeDist = 0.0f;
			if (splitAxis != 3)
			{
				bool hasChild0 = hasLeftChild != 0;
				bool hasChild1 = rightChild < (1u << 29u) - 1;
				uint child0 = nodeIndex + 1;
				uint child1 = rightChild;

				planeDist = positionLS[splitAxis] - splitPos;
				if (planeDist > 0.0f)
				{
					// swap values
					child0 ^= child1;
					child1 ^= child0;
					child0 ^= child1;
					hasChild0 ^= hasChild1;
					hasChild1 ^= hasChild0;
					hasChild0 ^= hasChild1;
				}

				if (hasChild0)
				{
					PushStack(stackTop, stackBuffer, child0);
				}
				if (abs(planeDist) < maxDistance && hasChild1)
				{
					PushStack(stackTop, stackBuffer, child1);
				}
			}
			
			if (abs(planeDist) >= maxDistance)
			{
				// discarded
				if (debugFroxel)
				{
					float3 debugProbePos = probePosBuffer[nodeIndex].xyz + wsOffset;

					//DebugProbeData(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float4(1, 0, 0, 1), planeDist, splitAxis, splitPos, positionLS[splitAxis]);
				}
			}

			if (abs(planeDist) < maxDistance)
			{
				float3 probeDiffVec = probePosBuffer[nodeIndex].xyz - positionLS;
				float distSqr = dot(probeDiffVec, probeDiffVec);

				float3 probeDiffVec0 = probePosBuffer[nodeIndex].xyz - position0LS;
				float distSqr0 = dot(probeDiffVec0, probeDiffVec0);

				float3 probeDiffVec1 = probePosBuffer[nodeIndex].xyz - position1LS;
				float distSqr1 = dot(probeDiffVec1, probeDiffVec1);

				if (debugFroxel)
				{
					float3 debugProbePos = probePosBuffer[nodeIndex].xyz + wsOffset;
					
					//DebugProbeData(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float4(0, 0, 1, 1), planeDist, splitAxis, splitPos, positionLS[splitAxis]);

				}

				if (distSqr < maxDistSqr)
				{
					// now we need to check whether this probe is in target region

					bool ok = true;
#if FROXEL_PROBES_USE_REGION_CULL
					if (iBestRegion != -1)
					{
						int numPlanes = pSrt->m_fogSubRegionData[iBestRegion].m_numPlanes;
						uint internalPlaneBits = pSrt->m_fogSubRegionData[iBestRegion].m_internalPlaneBits;

						bool inside = true;
						//float regionAwayDist = 0.0f;

						for (int iPlane = 0; iPlane < numPlanes; ++iPlane)
						{
							float4 planeEq = pSrt->m_fogPlanes[iGlobalPlane + iPlane];
							float d = dot(planeEq.xyz, probePosBuffer[nodeIndex].xyz + wsOffset) + planeEq.w;
							bool isInternal = internalPlaneBits & (1 << iPlane);

							if (!isInternal && d > 0)
							{
								inside = false; // fully outside, no need to iterate more
								break;
							}
						}

						//iGlobalPlane += numPlanes;

						ok = inside;

						if (debugFroxel && !inside)
						{
							float3 debugProbePos = probePosBuffer[nodeIndex].xyz + wsOffset;

							DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(0, 0, 1), -1); // blue : culled by region
						}

					}
#endif

					// check depth variance
					#if FROXEL_PROBES_USE_DEPTH_VARIANCE_OCCLUSION_OLD
					{
						float3 probePosWs = probePosBuffer[nodeIndex].xyz + wsOffset;

						float4 prevPosHs = mul(float4(probePosWs, 1), pSrt->m_mLastFrameVP);
						float3 prevPosNdc = prevPosHs.xyz / prevPosHs.w;
						//if (abs(prevPosNdc.x) > 1 || abs(prevPosNdc.y) > 1)
						//	ok = false;


						prevPosNdc.x = clamp(prevPosNdc.x, -1.0f, 1.0f);
						prevPosNdc.y = clamp(prevPosNdc.y, -1.0f, 1.0f);


						float3 prevPosView = mul(float4(probePosWs, 1), pSrt->m_mLastFrameV);
						float3 prevPosViewNorm = normalize(prevPosView);

						float3 prevPosUvw;
						prevPosUvw.xy = prevPosNdc.xy * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);
						prevPosUvw.y = 1.0f - prevPosUvw.y;

						uint2 realDim = uint2(24, 14);
						uint2 dim = uint2(192, 108);
						float2 dvUv = prevPosUvw.xy * float2(realDim) / float2(dim);

						float2 prevDepthVariance = pSrt->m_srcVolumetricsDepthInfoPrev.SampleLevel(pSrt->m_linearSampler, float3(dvUv, 6.0), 0).xy;

						float3 toProbe = normalize(probePosWs - pSrt->m_camPosPrev);

						float mean = prevDepthVariance.x;

						float3 meanPos = pSrt->m_camPosPrev + toProbe * (prevDepthVariance.x / prevPosViewNorm.z);
						float variance = prevDepthVariance.y - prevDepthVariance.x * prevDepthVariance.x;
						float3 varianceVec = toProbe * (variance / prevPosViewNorm.z);

						float probabilityOfGeoBehindProbe = prevPosView.z > (mean + 0.01) ? (variance / (variance + (prevPosView.z - mean))) : 1;


						if (debugFroxel)
						{
							DebugProbeDepthVariance(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, probePosWs, prevDepthVariance, meanPos, varianceVec, probabilityOfGeoBehindProbe);
						}

						// different way of checking it. this one is per froxel

						// first porject the probe onto froxel line

						
						float3 prevFroxelPositionVs = mul(float4(positionWS, 1), pSrt->m_mLastFrameV).xyz;
						float3 toFroxelVsNorm = normalize(prevFroxelPositionVs);

						float3 projectedPrevPosVs = dot(toFroxelVsNorm, prevPosView) * toFroxelVsNorm; // in view of previous frame. we can now compare z of this to depth variance of froxel location at previous frame
						{
							float3 prevFroxelPosUvw;

							float4 prevFroxelPositionH = mul(float4(prevFroxelPositionVs, 1), pSrt->m_mLastFrameP);

							float3 prevFroxelPositionNdc = prevFroxelPositionH.xyz / prevFroxelPositionH.w;

							// this is for debug, should match our original ws
							float3 prevFroxelWs = mul(float4(prevFroxelPositionVs, 1.0), pSrt->m_mLastFrameVInv).xyz;
							
							float3 projProbePrevWs = mul(float4(projectedPrevPosVs, 1.0), pSrt->m_mLastFrameVInv).xyz;


							prevFroxelPosUvw.xy = prevFroxelPositionNdc.xy * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);
							prevFroxelPosUvw.y = 1.0f - prevFroxelPosUvw.y;

							float2 froxelDvUv = prevFroxelPosUvw.xy * float2(realDim) / float2(dim);

							float2 prevFroxelDepthVariance = pSrt->m_srcVolumetricsDepthInfoPrev.SampleLevel(pSrt->m_linearSampler, float3(froxelDvUv, 6.0), 0).xy;
							float froxelVariance = max(0, prevFroxelDepthVariance.y - prevFroxelDepthVariance.x * prevFroxelDepthVariance.x);
							float froxelMean = prevFroxelDepthVariance.x;

							float froxelProbabilityOfGeoBehindProbe = projectedPrevPosVs.z > (froxelMean + 0.01) ? (froxelVariance / (froxelVariance + (projectedPrevPosVs.z - froxelMean))) : 1;


							if (debugFroxel)
							{
								DebugProbeDepthVarianceNew(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, probePosWs, projProbePrevWs, prevFroxelDepthVariance, froxelProbabilityOfGeoBehindProbe);//, meanPos, varianceVec, probabilityOfGeoBehindProbe);
							}
						}

					}
					#endif

					//if (pSrt->m_useOcclusion > 0)
#if FROXEL_PROBES_USE_OCCLUSION_OLD
					{
						uint probeIndex = asuint(probePosBuffer[nodeIndex].w);
						
						#if USE_SMALL_PROBE
						SmallCompressedProbe probe = pSrt->m_probesSmallRO[probeIndex];
						#else
						CompressedProbe probe = pSrt->m_probesRO[probeIndex];
						#endif

						// Shrink bbox for occlusion evaluation to avoid unstable results resulting from bboxes that
						// are too large in comparison to parent mesh.
						float3 probeVec = occlusionTargetPtLS - probePosBuffer[nodeIndex].xyz;
						float probeVecLen = length(probeVec);
						probeVec /= max(probeVecLen, 0.00001f);


						float3x4 localToWorldMat;
						localToWorldMat[0] = pSrt->m_lsToWorldMatrices0[nodeTreeIndex];
						localToWorldMat[1] = pSrt->m_lsToWorldMatrices1[nodeTreeIndex];
						localToWorldMat[2] = pSrt->m_lsToWorldMatrices2[nodeTreeIndex];
						float3 dir = mul(localToWorldMat, float4(probeVec, 0.0f)).xyz;

						float occluderDepth = CalcProbeOccluderDepth(probe.m_occlusion, dir);
						float currentDepth = saturate(probeVecLen / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);

						//float occlusion = 1.0f - smoothstep(occluderDepth * pSrt->m_occlusionScale, occluderDepth, currentDepth + pSrt->m_occlusionBias);
						//weight *= (occluderDepth < 1.0f) ? occlusion : 1.0f;

						ok = (occluderDepth >= (saturate(pSrt->m_probeOcclusionDiscardDistScalar / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH))) || (occluderDepth >= saturate(probeVecLen / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH - (PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH / 127.0)));
					}
#endif

					if (ok)
					{
						#if ENABLE_FEEDBACK_DEBUG
						if (debugFroxel)
						{
							float3 debugProbePos = probePosBuffer[nodeIndex].xyz + wsOffset;
							//pSrt->m_debugProbeData[nodeIndex].m_data0 = float4(debugProbePos, 1.0f);

							//DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float4(1, 1, 1, 1));
						}
						#endif

						// calculate weight based off the otehr positions


						float distWeight = exp2(-distSqr / maxDistSqr * 8.0f);

						float distWeight0 = exp2(-distSqr0 / maxDistSqr * 8.0f);
						float distWeight1 = exp2(-distSqr1 / maxDistSqr * 8.0f);

						int kNumSteps = pSrt->m_numProbeSampleSteps;
						float wSum = 0;
						for (int iStep = 0; iStep < kNumSteps; ++iStep)
						{
							float blend = 0.5f / kNumSteps + (1.0 / kNumSteps) * iStep;

							float3 posLsLerped = lerp(position0LS, position1LS, blend);
							float3 probeDiffLerped = probePosBuffer[nodeIndex].xyz - posLsLerped;
							float distSqrLerped = dot(probeDiffLerped, probeDiffLerped);
							float distWeightLerped = exp2(-distSqrLerped / maxDistSqr * 8.0f);

							wSum += distWeightLerped;
						}

						float weight = distWeight;
						#if FROXEL_PROBES_USE_MULTISAMPLING_OLD
						//weight = lerp(distWeight0, distWeight1, 0.5);
						if (kNumSteps > 0)
						{
							weight = wSum / kNumSteps;
						}
						#endif
						uint iWeight;
						uint probeInfo = EncodeProbeInfo(weight, asuint(probePosBuffer[nodeIndex].w), iWeight);
						if (probeCount < kMaxNumProbeResults)
						{
							probeInfoList[probeCount] = probeInfo | probeCount;
							#if ENABLE_FEEDBACK_DEBUG
							probeDebugList[probeCount] = (probeTreeIndex << 16) |  nodeIndex;
							#endif
							probeCount++;
						}
						else
						{
							// find slot with smallest weight
							uint minSlot = min(min3(min3(probeInfoList[0], probeInfoList[1], probeInfoList[2]),
								min3(probeInfoList[3], probeInfoList[4], probeInfoList[5]),
								min3(probeInfoList[6], probeInfoList[7], probeInfoList[8])),
								probeInfoList[9]);

							if (iWeight > (minSlot & 0xfff00000))
							{
								uint probeInfoIndex = minSlot & 0x0f;
								probeInfoList[probeInfoIndex] = probeInfo | probeInfoIndex;

								#if ENABLE_FEEDBACK_DEBUG
								if (debugFroxel)
								{
									uint removedNodeIndex = probeDebugList[probeInfoIndex] & 0x0000FFFF;
									float3 debugProbePos = probePosBuffer[removedNodeIndex].xyz + wsOffset;

									DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 0, 0), -1); // red: removed from list after being added because too many
								}

								probeDebugList[probeInfoIndex] = (probeTreeIndex << 16) | nodeIndex;
								#endif
							}
							#if ENABLE_FEEDBACK_DEBUG
							else
							{
								if (debugFroxel)
								{
									// discarding because not strong enough
									float3 debugProbePos = probePosBuffer[nodeIndex].xyz + wsOffset;

									DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 1, 0), -1); // yellow: not added to the list because too many already
								}
							}
							#endif
						}
					}
				}
			}
		}
	}
}



[NUM_THREADS(4, 4, 4)] //
void CS_VolumetricsTemporalCombineProbeCacheFroxels(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{

	uint2 groupPos = uint2(4, 4) * groupId.xy;

	uint2 pos = groupPos + groupThreadId.xy;


	uint threadId = groupId.z * 4 * 4 + groupId.y * 4 + groupId.x;

	int iSlice = groupId.z * 4 + groupThreadId.z;

	// march through all slices of froxel grid and accumulate results

	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);


	if (pos.x >= pSrt->m_probeCacheGridSize.x || pos.y >= pSrt->m_probeCacheGridSize.y)
		return;  // this wont exit whole wavefront but it will help with divergence issues

	if (pos.x == 5 && pos.y == 5 && iSlice == 5)
	{
		//_SCE_BREAK();
	}

	
	{
		froxelCoord.z = iSlice;

		
		float ndcX = float(pos.x + 0.5) / float(pSrt->m_probeCacheGridSize.x) * 2.0f - 1.0f;
		float ndcY = pSrt->m_probeCacheCellRealToNdcScale * float(pos.y + 0.5) / float(pSrt->m_probeCacheGridSize.y) * 2.0f - 1.0f;
		ndcY = -ndcY;

		// todo: add jitter and temporal

		float viewZJit = FroxelZSliceToCameraDistExp((iSlice + 0.5 + pSrt->m_ndcFogJitterZ) * pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);
		float viewZJitWithOffset = FroxelZSliceToCameraDistExp((iSlice + 0.5 + pSrt->m_ndcFogJitterZ + pSrt->m_probeDepthOffset) * pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);
		float viewZUnjit = FroxelZSliceToCameraDistExp((iSlice + 0.5) * pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);
		float viewZ0Unjit = FroxelZSliceToCameraDistExp((iSlice) * pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);
		float viewZ1Unjit = FroxelZSliceToCameraDistExp((iSlice + 1.0)* pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);
		float viewZ0WithOffset = FroxelZSliceToCameraDistExp((iSlice + pSrt->m_probeDepthOffset) * pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);
		float viewZ1WithOffset = FroxelZSliceToCameraDistExp((iSlice + 1.0 + pSrt->m_probeDepthOffset) * pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);


		float sliceDepth = DepthDerivativeAtSlice((iSlice + 0.5) * pSrt->m_probeCacheZScale) * pSrt->m_probeCacheZScale;
		//float depthOffset = sliceDepth * pSrt->m_probeDepthOffset;

		float ndcZJit = GetNdcDepth(viewZJit, pSrt->m_depthParams);
		float ndcZJitWithOffset = GetNdcDepth(viewZJitWithOffset, pSrt->m_depthParams);
		float ndcZUnjit = GetNdcDepth(viewZUnjit, pSrt->m_depthParams);
		float ndcZ0Unjit = GetNdcDepth(viewZ0Unjit, pSrt->m_depthParams);
		float ndcZ1Unjit = GetNdcDepth(viewZ1Unjit, pSrt->m_depthParams);
		float ndcZ0WithOffset = GetNdcDepth(viewZ0WithOffset, pSrt->m_depthParams);
		float ndcZ1WithOffset = GetNdcDepth(viewZ1WithOffset, pSrt->m_depthParams);

		float4 posWsH = mul(float4(ndcX, ndcY, ndcZJit, 1), pSrt->m_mVPInv);
		float3 posWs = posWsH.xyz / posWsH.w;

		float4 posWsJitWithOffsetH = mul(float4(ndcX, ndcY, ndcZJitWithOffset, 1), pSrt->m_mVPInv);
		float3 posWsJitWithOffset = posWsJitWithOffsetH.xyz / posWsJitWithOffsetH.w;

		float4 posWs0WithOffsetH = mul(float4(ndcX, ndcY, ndcZ0WithOffset, 1), pSrt->m_mVPInv);
		float3 posWs0WithOffset = posWs0WithOffsetH.xyz / posWs0WithOffsetH.w;

		float4 posWs1WithOffsetH = mul(float4(ndcX, ndcY, ndcZ1WithOffset, 1), pSrt->m_mVPInv);
		float3 posWs1WithOffset = posWs1WithOffsetH.xyz / posWs1WithOffsetH.w;

		float4 posWsUnjitH = mul(float4(ndcX, ndcY, ndcZUnjit, 1), pSrt->m_mVPInv);
		float3 posWsUnjit = posWsUnjitH.xyz / posWsUnjitH.w;

		bool debugFroxel = (pos.x == pSrt->m_debugProbes0.x && pos.y == pSrt->m_debugProbes0.y && iSlice == pSrt->m_debugProbes0.z);
#if !ENABLE_FEEDBACK_DEBUG
		debugFroxel = false;
#endif

		if (debugFroxel)
		{
			DebugProbeFroxel(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, posWs, /*samplingPos*/posWsJitWithOffset, pSrt->m_maxBlurRadius, -1.0f, float4(1, 0, 0, 1));
			//pSrt->m_debugProbeMasterData[0].m_posWs = posWs;
			//pSrt->m_debugProbeMasterData[0].m_posValid = 1.0;

			//pSrt->m_debugProbeMasterData[0].m_r = pSrt->m_maxBlurRadius;

			{
				float ndcX0 = float(pos.x) / float(pSrt->m_probeCacheGridSize.x) * 2.0f - 1.0f;
				float ndcY0 = pSrt->m_probeCacheCellRealToNdcScale * float(pos.y) / float(pSrt->m_probeCacheGridSize.y) * 2.0f - 1.0f;
				ndcY0 = -ndcY0;

				float ndcX1 = float(pos.x + 1) / float(pSrt->m_probeCacheGridSize.x) * 2.0f - 1.0f;
				float ndcY1 = pSrt->m_probeCacheCellRealToNdcScale * float(pos.y) / float(pSrt->m_probeCacheGridSize.y) * 2.0f - 1.0f;
				ndcY1 = -ndcY1;

				float ndcX2 = float(pos.x + 1) / float(pSrt->m_probeCacheGridSize.x) * 2.0f - 1.0f;
				float ndcY2 = pSrt->m_probeCacheCellRealToNdcScale * float(pos.y + 1) / float(pSrt->m_probeCacheGridSize.y) * 2.0f - 1.0f;
				ndcY2 = -ndcY2;

				float ndcX3 = float(pos.x) / float(pSrt->m_probeCacheGridSize.x) * 2.0f - 1.0f;
				float ndcY3 = pSrt->m_probeCacheCellRealToNdcScale * float(pos.y + 1) / float(pSrt->m_probeCacheGridSize.y) * 2.0f - 1.0f;
				ndcY3 = -ndcY3;

				{
					float4 posWs0H = mul(float4(ndcX0, ndcY0, ndcZ0Unjit, 1), pSrt->m_mVPInv);
					float3 posWs0 = posWs0H.xyz / posWs0H.w;

					float4 posWs1H = mul(float4(ndcX1, ndcY1, ndcZ0Unjit, 1), pSrt->m_mVPInv);
					float3 posWs1 = posWs1H.xyz / posWs1H.w;

					float4 posWs2H = mul(float4(ndcX2, ndcY2, ndcZ0Unjit, 1), pSrt->m_mVPInv);
					float3 posWs2 = posWs2H.xyz / posWs2H.w;

					float4 posWs3H = mul(float4(ndcX3, ndcY3, ndcZ0Unjit, 1), pSrt->m_mVPInv);
					float3 posWs3 = posWs3H.xyz / posWs3H.w;

					DebugProbeFroxelBox(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, posWs0, posWs1, posWs2, posWs3);
				}

				{
					float4 posWs0H = mul(float4(ndcX0, ndcY0, ndcZ1Unjit, 1), pSrt->m_mVPInv);
					float3 posWs0 = posWs0H.xyz / posWs0H.w;

					float4 posWs1H = mul(float4(ndcX1, ndcY1, ndcZ1Unjit, 1), pSrt->m_mVPInv);
					float3 posWs1 = posWs1H.xyz / posWs1H.w;

					float4 posWs2H = mul(float4(ndcX2, ndcY2, ndcZ1Unjit, 1), pSrt->m_mVPInv);
					float3 posWs2 = posWs2H.xyz / posWs2H.w;

					float4 posWs3H = mul(float4(ndcX3, ndcY3, ndcZ1Unjit, 1), pSrt->m_mVPInv);
					float3 posWs3 = posWs3H.xyz / posWs3H.w;

					DebugProbeFroxelBox(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, posWs0, posWs1, posWs2, posWs3);
				}
			}
		}

		
		[isolate]
		{
		}


		// go through regions
		int inside = false;
		int fullyInside = false;
		uint probeCullMask = 0;
		float regionAddlDensity = 0;
		float3 regionTint = float3(1.0f, 1.0f, 1.0f);

#define TRACK_PROBE_BEST_REGION 1
		float kFarFromRegion = sliceDepth * pSrt->m_probeCloseToRegionFactor; // 6.0f;

#if FROXEL_CHECK_REGIONS
		int iGlobalPlane = 0;
		int iBestRegion = -1;
		int iBestRegionForProbeCull = -1;
		int probeRegionPlaneOffset = 0;
		[isolate]
		{
			float minRegionAwayDist = 10000000.0f;
			int numSubRegions = pSrt->m_numSubRegions;
			for (int iSubRegion = 0; (iSubRegion < numSubRegions) /* && (!fullyInside)*/; ++iSubRegion)
			{
				int numPlanes = pSrt->m_fogSubRegionData[iSubRegion].m_numPlanes;
				uint internalPlaneBits = pSrt->m_fogSubRegionData[iSubRegion].m_internalPlaneBits;
				float blendOut = pSrt->m_fogSubRegionData[iSubRegion].m_fadeOutRadius; // this could be made a constant specifically for coarse culling
				int doProbeCull = (internalPlaneBits & 0x80000000) ? 0 : -1;
				inside = true;
				//fullyInside = true;
				//float regionAwayDist = 0.0f;
				float regionAwayDist = -blendOut;

				for (int iPlane = 0; iPlane < numPlanes; ++iPlane)
				{
					float4 planeEq = pSrt->m_fogPlanes[iGlobalPlane + iPlane];
					float d = dot(planeEq.xyz, posWs) + planeEq.w;

					bool isInternal = internalPlaneBits & (1 << iPlane);

					if (d > 0)
					{
						inside = false; // fully outside, no need to iterate more
						//fullyInside = false;
						#if !TRACK_PROBE_BEST_REGION
							regionAwayDist = 10000000.0f; // will not change closest distance
							break; // can exit early in case we don't care about finding probe region
						#endif
					}

					

					#if TRACK_PROBE_BEST_REGION
					if (d <= 0)
					#endif
					{
						// if this is internal plane, it shouldn't contribute to closest to edge calculations
						// because it is completely inside combined region
						if (isInternal)
						{
							d = -blendOut;
						}
					}

					#if TRACK_PROBE_BEST_REGION
					// we want to keep checking planes even if we know we are outside of plane. we do it because we want to be more generous for
					// choosing a region to cull probes for. the idea here is that for froxels that are close but outside of region
					// we still want to exclude probes that are not in the region to avoid any bleeding
					if (d > kFarFromRegion)
					{
						// ok we can break out of the loop now. we are definitely far from region
						regionAwayDist = 10000000.0f; // will not change closest distance
						break;
					}
					#endif

					//if (d > -blendOut)
					//{
					//	fullyInside = false;
					//}

					regionAwayDist = max(regionAwayDist, d);
					
				}

				if (inside)
				{
					// we are inside of the region, just break out right away, we don't care if we are in any other one.
					minRegionAwayDist = regionAwayDist;
					iBestRegion = iSubRegion;
					iBestRegionForProbeCull = iSubRegion | doProbeCull; // will stay -1 if the region doesn't want the probe culling
					probeRegionPlaneOffset = iGlobalPlane;
					break;
				}

				
				if (regionAwayDist < minRegionAwayDist)
				{
					minRegionAwayDist = regionAwayDist;
					iBestRegionForProbeCull = iSubRegion | doProbeCull; // will stay -1 if the region doesn't want the probe culling
					probeRegionPlaneOffset = iGlobalPlane;
				}

				iGlobalPlane += numPlanes;

			}

			

			if (iBestRegion >= 0)
			{
				float blendOut = pSrt->m_fogSubRegionData[iBestRegion].m_fadeOutRadius;
				
				//float regionBlend = saturate(1.0 - (minRegionAwayDist) / blendOut);
				
				float regionBlend = saturate(-(minRegionAwayDist) / blendOut);


				//regionBlend *= regionBlend;
				//regionBlend *= regionBlend;

				//regionBlend = regionBlend > 0.0 ? 1.0 : 0.0;

				FogRegionSettings regionSettings = pSrt->m_fogSubRegionData[iBestRegion].m_settings;

				regionAddlDensity = regionBlend * regionSettings.m_fogRegionAddlDensity;
				regionTint.x = regionBlend * regionSettings.m_fogRegionAmbientTintR;
				regionTint.y = regionBlend * regionSettings.m_fogRegionAmbientTintG;
				regionTint.z = regionBlend * regionSettings.m_fogRegionAmbientTintB;
			}
		}
#else
	int iBestRegion = -1;
	int iBestRegionForProbeCull = -1;
	int probeRegionPlaneOffset = 0;
#endif
		// force all probes culled by first region
		//iBestRegionForProbeCull = 0;
		//probeRegionPlaneOffset = 0;
	//regionAddlDensity = 0;
	//regionTint.xyz = float3(1.0, 1.0, 1.0);

		// forse no leak protection
		//iBestRegionForProbeCull = -1;
		//probeRegionPlaneOffset = 0;

		//inside = false;

		float3 center = float3(5, 2, 3);

		float3 dif = posWs - center;
		float d2 = dot(dif, dif);

		inside = inside || (d2 < (5 * 5));


		// get sky data
#if		USE_MIP_SKY_FOG
		float3 skyColor;
		float skyColorContribution;
		{
			float3 dirToFroxel = posWs - pSrt->m_camPos;
			
			dirToFroxel = normalize(dirToFroxel);

			float2 texUvCoord = GetFogSphericalCoords(dirToFroxel, pSrt->m_skyAngleOffset);
			//float numMipLevels = 8;
			//float mipBias = (1.0 - sqrt(saturate((pixelDepth - pSrt->m_mipfadeDistance) / (numMipLevels*pSrt->m_mipfadeDistance))))*numMipLevels;
			float mipBias = 0;

			float4 mipFogColor = pSrt->m_skyTexture.SampleLevel(SetWrapSampleMode(pSrt->m_linearSampler), texUvCoord, mipBias);
			skyColor = mipFogColor.xyz * pSrt->m_skyMipNormalizationFactor;

			skyColorContribution = saturate(1.0 + (viewZUnjit - pSrt->m_skyMipTextureBlendInDist) / 16.0);
		}

		// get probe data

		float4 resultData = float4(skyColor, 0.5);

		if (skyColorContribution < 1.0f)
#else
		float4 resultData = float4(0, 0, 0, 0);

#endif
		{
			float3 probeSamplePos = posWsJitWithOffset; // posWs

			float3 probeSamplePos0 = posWs0WithOffset; // posWs
			float3 probeSamplePos1 = posWs1WithOffset; // posWs


			uint probeInfoList[kMaxNumProbeResults];
#if ENABLE_FEEDBACK_DEBUG
			uint probeDebugList[kMaxNumProbeResults];
#endif
			uint probeCount = 0;
			for (uint i = 0; i < pSrt->m_numNodeTrees; i++)
			{
				float radius = i < pSrt->m_numLod0Trees ? pSrt->m_maxBlurRadius : 15.0f;

				float3x4 worldToLocalMat;
				worldToLocalMat[0] = pSrt->m_wsToLsMatrices0[i];
				worldToLocalMat[1] = pSrt->m_wsToLsMatrices1[i];
				worldToLocalMat[2] = pSrt->m_wsToLsMatrices2[i];
				float3 positionLS = mul(worldToLocalMat, float4(probeSamplePos, 1.0f)).xyz;
				positionLS = /*ReadFirstLane*/(positionLS);
				float3 position0LS = mul(worldToLocalMat, float4(probeSamplePos0, 1.0f)).xyz;
				position0LS = /*ReadFirstLane*/(position0LS);
				float3 position1LS = mul(worldToLocalMat, float4(probeSamplePos1, 1.0f)).xyz;
				position1LS = /*ReadFirstLane*/(position1LS);

				float3 camPosLs = mul(worldToLocalMat, float4(pSrt->m_camPos, 1.0f)).xyz;

				// we are making assumpion that there is no rotation involved for the probe level
				float3 wsOffset = float3(-worldToLocalMat[0].w, -worldToLocalMat[1].w, -worldToLocalMat[2].w);
				FindProbeNearestNeighbors(i, probeInfoList,
					
					#if ENABLE_FEEDBACK_DEBUG
					probeDebugList,
					#endif
					i, probeCount, pSrt->m_rootIndices[i],
					pSrt->m_centerPosLs[i].xyz, pSrt->m_edgeLengthLs[i].xyz,
					radius, positionLS, position0LS, position1LS, camPosLs, probeSamplePos, pSrt->m_camPos, wsOffset,
					pSrt->m_probeNodesRO, pSrt->m_probePosRO, pSrt, probeRegionPlaneOffset, iBestRegionForProbeCull, debugFroxel);
			}

			const float kCoeffScale = 1.0f / 127.0f;

			// init probe sum
			float weightSum = 1.0f;
			#if USE_SMALL_PROBE
			SmallCompressedProbe firstProbeSmall = pSrt->m_defaultProbeSmall;
			#else
			CompressedProbe firstProbe = pSrt->m_defaultProbe;
			#endif
			int maxWeightProbe = -1;

			if (probeCount > 0)
			{
				uint probeIndex;
				DecodeProbeInfo(probeInfoList[0], weightSum, probeIndex);

				#if USE_SMALL_PROBE
				firstProbeSmall = pSrt->m_probesSmallRO[probeIndex];
				#else
				firstProbe = pSrt->m_probesRO[probeIndex];
				#endif

				maxWeightProbe = 0;

				#if ENABLE_FEEDBACK_DEBUG
				if (debugFroxel)
				{
					int nodeIndex = probeDebugList[0] & 0x0000FFFF;
					int treeIndex = probeDebugList[0] >> 16;
					float3x4 worldToLocalMat;
					worldToLocalMat[0] = pSrt->m_wsToLsMatrices0[treeIndex];
					worldToLocalMat[1] = pSrt->m_wsToLsMatrices1[treeIndex];
					worldToLocalMat[2] = pSrt->m_wsToLsMatrices2[treeIndex];
					float3 wsOffset = float3(-worldToLocalMat[0].w, -worldToLocalMat[1].w, -worldToLocalMat[2].w);

					float3 debugProbePos = pSrt->m_probePosRO[nodeIndex].xyz + wsOffset;
					DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 1, 1), weightSum); // white: part of result
				}
				#endif
			}

			
			#if USE_SMALL_PROBE
			float probeScale = firstProbeSmall.m_colorScale * weightSum * kCoeffScale;
			#else
			float probeScale = firstProbe.m_colorScale * weightSum * kCoeffScale;
			#endif
			DecompressedProbe probeSum;

			float shadowSum;

			#if USE_SMALL_PROBE
			float densitySum = (firstProbeSmall.m_addlData & 0x0000FFFF) / 256.0f;
			DecompressSmallProbeWithShadow(firstProbeSmall, probeScale, probeSum, shadowSum);
			#else
			float densitySum = (firstProbe.m_addlData & 0x0000FFFF) / 256.0f;
			DecompressProbeWithShadow(firstProbe, probeScale, probeSum, shadowSum);
			#endif

			


			if (probeCount == 0)
			{
				probeSum.s0.x *= pSrt->m_defaultProbeTint.x;
				probeSum.s1.x *= pSrt->m_defaultProbeTint.y;
				probeSum.s2.x *= pSrt->m_defaultProbeTint.z;
			}

			if (probeCount == 0 && iBestRegion >= 0)
			{
				float4 probe = pSrt->m_fogSubRegionData[iBestRegion].m_probe;
				if (probe.w > 0.0f)
				{
					weightSum = 0.01f; // a little bit of default probe from region
					probeSum.s0.x = probe.x * weightSum;
					probeSum.s1.x = probe.y * weightSum;
					probeSum.s2.x = probe.z * weightSum;
				}
			}

			shadowSum *= weightSum;
			densitySum *= weightSum;

			float probeMaxW = weightSum;


#pragma warning(disable : 8201)

			// accumulate probe samples
			if (probeCount > 1)
			{
				for (uint i = 1; i < probeCount; i++)
				{

					float weight;
					uint probeIndex;
					DecodeProbeInfo(probeInfoList[i], weight, probeIndex);

					#if ENABLE_FEEDBACK_DEBUG
					if (debugFroxel)
					{
						int nodeIndex = probeDebugList[i] & 0x0000FFFF;
						int treeIndex = probeDebugList[i] >> 16;
						float3x4 worldToLocalMat;
						worldToLocalMat[0] = pSrt->m_wsToLsMatrices0[treeIndex];
						worldToLocalMat[1] = pSrt->m_wsToLsMatrices1[treeIndex];
						worldToLocalMat[2] = pSrt->m_wsToLsMatrices2[treeIndex];
						float3 wsOffset = float3(-worldToLocalMat[0].w, -worldToLocalMat[1].w, -worldToLocalMat[2].w);

						float3 debugProbePos = pSrt->m_probePosRO[nodeIndex].xyz + wsOffset;
						DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 1, 1), weight); // white: part of result
					}
					#endif


					

					#if USE_SMALL_PROBE
					SmallCompressedProbe probeSmall = pSrt->m_probesSmallRO[probeIndex];
					probeScale = probeSmall.m_colorScale * weight * kCoeffScale;
					#else
					CompressedProbe probe = pSrt->m_probesRO[probeIndex];
					probeScale = probe.m_colorScale * weight * kCoeffScale;
					#endif


					DecompressedProbe probeSample;
					float shadowSample;
					
					#if USE_SMALL_PROBE
					float densitySample = (probeSmall.m_addlData & 0x0000FFFF) / 256.0f;
					DecompressSmallProbeWithShadow(probeSmall, probeScale, probeSample, shadowSample);
					#else
					float densitySample = (probe.m_addlData & 0x0000FFFF) / 256.0f;
					DecompressProbeWithShadow(probe, probeScale, probeSample, shadowSample);
					#endif




					shadowSample *= weight;
					densitySample *= weight;

					probeSum.s0 += probeSample.s0;
					probeSum.s1 += probeSample.s1;
					probeSum.s2 += probeSample.s2;
					probeSum.s3 += probeSample.s3;
					probeSum.s4 += probeSample.s4;
					probeSum.s5 += probeSample.s5;
					probeSum.s6 += probeSample.s6;
					probeSum.s7 += probeSample.s7;
					shadowSum += shadowSample;
					weightSum += weight;
					densitySum += densitySample;

					if (weight > probeMaxW)
					{
						probeMaxW = weight;
						maxWeightProbe = i;
					}
				}
			}

			float invWeightSum = 1.0f / max(weightSum, 0.0001f);

			// normalize accumulated probes
			probeSum.s0 *= invWeightSum;
			probeSum.s1 *= invWeightSum;
			probeSum.s2 *= invWeightSum;
			probeSum.s3 *= invWeightSum;
			probeSum.s4 *= invWeightSum;
			probeSum.s5 *= invWeightSum;
			probeSum.s6 *= invWeightSum;
			probeSum.s7 *= invWeightSum;
			shadowSum *= invWeightSum;
			//densitySum *= invWeightSum;
			

			float3 band0RGB = float3(probeSum.s0.x, probeSum.s1.x, probeSum.s2.x);

			if (probeCount == 0)
			{
			//	band0RGB = float3(1, 0, 0);
			}

			if (debugFroxel)
			{
				DebugProbeResult(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, probeSamplePos, band0RGB, weightSum, pSrt->m_frameNumber);
			}


			#if 0
			if (maxWeightProbe >= 0)
			{
				CompressedProbe probe = pSrt->m_probesRO[maxWeightProbe];

				// Shrink bbox for occlusion evaluation to avoid unstable results resulting from bboxes that
				// are too large in comparison to parent mesh.
				float3 probeVec = occlusionTargetPtLS - probePosBuffer[nodeIndex].xyz;
				float probeVecLen = length(probeVec);
				probeVec /= max(probeVecLen, 0.00001f);


				float3x4 localToWorldMat;
				localToWorldMat[0] = pSrt->m_lsToWorldMatrices0[nodeTreeIndex];
				localToWorldMat[1] = pSrt->m_lsToWorldMatrices1[nodeTreeIndex];
				localToWorldMat[2] = pSrt->m_lsToWorldMatrices2[nodeTreeIndex];
				float3 dir = mul(localToWorldMat, float4(probeVec, 0.0f)).xyz;

				float occluderDepth = CalcProbeOccluderDepth(probe.m_occlusion, dir);
				float currentDepth = saturate(probeVecLen / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);

				//float occlusion = 1.0f - smoothstep(occluderDepth * pSrt->m_occlusionScale, occluderDepth, currentDepth + pSrt->m_occlusionBias);
				//weight *= (occluderDepth < 1.0f) ? occlusion : 1.0f;

				ok = occluderDepth > saturate(pSrt->m_probeDiscardDist / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);
			}
			#endif
			
			densitySum = regionAddlDensity * 0.5f + FROXELS_REGION_DENSITY_MID_POINT; // convert -1 .. 1 to 0 to 1
#if		USE_MIP_SKY_FOG
			resultData = float4(regionTint * lerp(band0RGB, skyColor, skyColorContribution), densitySum /*shadowSum*/);
#else
			resultData = float4(regionTint * band0RGB, densitySum /*shadowSum*/);
#endif
		}
	else
	{
		// we are fully lighting with sky, but we still want to process regions

		float densitySum = regionAddlDensity * 0.5f + FROXELS_REGION_DENSITY_MID_POINT;
		resultData.rgb *= regionTint;
		resultData.a = densitySum;

	}


	// and now combine with previous frame

	// to find previous value in the grid, we need to first find the world position of current sample and the project it into previous frame

	float4 posLastFrameH = mul(float4(posWsUnjit, 1), pSrt->m_mLastFrameVP);
	float3 posLastFrameNdc = posLastFrameH.xyz / posLastFrameH.w;

	// now go from ndc to uv coords

	float3 uv3d;
	uv3d.xy = posLastFrameNdc.xy * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);

	uv3d.y = 1.0f - uv3d.y;

	float ndcDepth = posLastFrameNdc.z;

	float linearDepth = GetLinearDepth(ndcDepth, pSrt->m_lastFrameDepthParams);

	uv3d.z = CameraLinearDepthToFroxelZCoordExp(linearDepth, pSrt->m_fogGridOffset);
	uv3d.z = clamp(uv3d.z, 0.0f, 1.0f);

	float4 resultValue = resultData;
#if DISABLE_TEMPORAL_IN_FOG

#else

	//uv3d.xy = float2(0.5, 0.5);
	//float4 prevVal = pSrt->m_srcProbeCacheFroxelsPrev[froxelCoord];
	float4 prevVal = pSrt->m_srcProbeCacheFroxelsPrev.SampleLevel(pSrt->m_linearSampler, uv3d, 0).rgba;
#if USE_MANUAL_POW_FROXELS
	prevVal.xyz *= prevVal.xyz;
#endif
	prevVal.xyz *= 8.0f;

	resultValue = resultData * 0.05 + prevVal * 0.95;
#endif

	resultValue.xyz = resultValue.xyz / 8.0;

#if USE_MANUAL_POW_FROXELS
	resultValue.xyz = sqrt(resultValue.xyz);
#endif

	pSrt->m_destProbeCacheFroxels[froxelCoord] = resultValue;
	}
}


void PushStackScalar(inout int index, inout uint stackBuffer, in uint value)
{
	WriteLane(stackBuffer, value, (uint)index);
	index++;
}

uint PopStackScalar(inout int index, inout uint stackBuffer)
{
	index--;

	return ReadLane(stackBuffer, (uint)index);
}

uint PosFToUint(float posIn)
{
	return (posIn - (-3000)) / 0.25;
}

uint DistFToUint(float posIn)
{
	return (posIn) / 0.25;
}

uint3 PosF3ToUint3(float3 posIn)
{
	return uint3(PosFToUint(posIn.x), PosFToUint(posIn.y), PosFToUint(posIn.z));
}

float PosUToFloat(uint posIn)
{
	return (posIn * 0.25) + (-3000);
}
float3 PosU3ToFloat3(float3 encodedF3)
{
	uint3 u3 = uint3(asuint(encodedF3.x), asuint(encodedF3.y), asuint(encodedF3.z));

	return float3(PosUToFloat(u3.x), PosUToFloat(u3.y), PosUToFloat(u3.z));
}

#define UINT_SPLIT_POS 1
#define UINT_POS 1

#define UINT_CENTER_EDGE 1

#define SCALAR_USE_TWO_VECTORS 1

#define SCALAR_USE_THREE_VECTORS 1

#ifdef kMaxNumProbeResults
#undef kMaxNumProbeResults
#endif

#if SCALAR_USE_THREE_VECTORS
#define kMaxNumProbeResults 192
#elif SCALAR_USE_TWO_VECTORS
#define kMaxNumProbeResults 128
#else
#define kMaxNumProbeResults 64
#endif


uint EncodeProbeInfoUint(inout uint weight, in uint probeIndex)
{
	//iWeight = uint(weight * 4095.0f) << 20u;
	//return iWeight | (probeIndex << 4u);
	weight = (weight & 0x0000ffff) << 16;

	return weight | probeIndex;
}

void DecodeProbeInfoUint(in uint probeInfo, out uint weight, out uint probeIndex)
{
	//weight = (probeInfo >> 20u) / 4095.0f;
	//probeIndex = (probeInfo >> 4u) & 0xffff;

	weight = (probeInfo >> 16);
	probeIndex = (probeInfo) & 0xffff;
}

void FindProbeNearestNeighborsScalar(int nodeTreeIndex, 
	
	inout uint probeInfoListScalar0,

#if SCALAR_USE_TWO_VECTORS
	inout uint probeInfoListScalar1,
#endif

#if SCALAR_USE_THREE_VECTORS
	inout uint probeInfoListScalar2,
#endif
	
	inout uint probeCount,
	in int rootIndex, in float3 centerPosLS, in float3 edgeLenLS,
	in float maxDistance, in float3 positionLS, in float3 positionWS, in float3 occlusionTargetPtLS, in float3 wsOffset,
	StructuredBuffer<uint2> probeNodeBuffer,
	StructuredBuffer<float4> probePosBuffer,
	VolumetricsFogSrt *pSrt, int iGlobalPlane_, int iBestRegion_, uint debugFroxel)
{
	int stackTop = 0;
	uint stackBufferScalar = 0; // we use vgpr for it, so we have [64] of these
	//uint stackBuffer[kStackBufferSize];

	float maxDistSqr = maxDistance * maxDistance;
	uint maxDistanceUint = DistFToUint(maxDistance);
	uint maxDistSqrUint = maxDistanceUint * maxDistanceUint;

	int3 positionLSUint = int3(PosF3ToUint3(positionLS));
	int3 positionWSUint = int3(PosF3ToUint3(positionWS));
	positionLSUint = ReadFirstLane(positionLSUint);
	positionWSUint = ReadFirstLane(positionWSUint);

	int3 centerPosLSUint = int3(asuint(centerPosLS.x), asuint(centerPosLS.y), asuint(centerPosLS.z));
	int3 edgeLenLSUint = int3(asuint(edgeLenLS.x), asuint(edgeLenLS.y), asuint(edgeLenLS.z)) + int3(maxDistanceUint, maxDistanceUint, maxDistanceUint);
	#if UINT_CENTER_EDGE
	if (all(abs(positionLSUint - centerPosLSUint) < edgeLenLSUint))
	#else
	if (all(abs(positionLS - centerPosLS) < (edgeLenLS + float3(maxDistance, maxDistance, maxDistance))))
	#endif
	{
		PushStackScalar(stackTop, stackBufferScalar, rootIndex);
		//PushStack(stackTop, stackBuffer, rootIndex);

		while (stackTop > 0)
		{
			uint nodeIndex = PopStackScalar(stackTop, stackBufferScalar);
			//uint nodeIndex = PopStack(stackTop, stackBuffer);

			uint2 node = probeNodeBuffer[nodeIndex];

			float splitPos = asfloat(node.x);
			uint splitAxis = node.y & 0x03;
			uint hasLeftChild = node.y & 0x04;
			uint rightChild = node.y >> 3u;
			
			#if UINT_SPLIT_POS
			int planeDist = 0.0f;
			#else
			float planeDist = 0.0f;
			#endif
			if (splitAxis != 3)
			{
				uint hasChild0 = hasLeftChild != 0;
				uint hasChild1 = rightChild < (1u << 29u) - 1;
				uint child0 = nodeIndex + 1;
				uint child1 = rightChild;

				#if UINT_SPLIT_POS
					planeDist = int(positionLSUint[splitAxis]) - int(asuint(splitPos));
				#else
					planeDist = positionLS[splitAxis] - splitPos;
				#endif
				planeDist = ReadFirstLane(planeDist);
				if (planeDist > 0)
				{
					// swap values
					child0 ^= child1;
					child1 ^= child0;
					child0 ^= child1;
					hasChild0 ^= hasChild1;
					hasChild1 ^= hasChild0;
					hasChild0 ^= hasChild1;
				}
				hasChild0 = ReadFirstLane(hasChild0);
				hasChild1 = ReadFirstLane(hasChild1);
				child0 = ReadFirstLane(child0);
				child1 = ReadFirstLane(child1);

				if (hasChild0)
				{
					PushStackScalar(stackTop, stackBufferScalar, child0);
					//PushStack(stackTop, stackBuffer, child0);
				}
				#if UINT_SPLIT_POS
				if (abs(planeDist) < maxDistanceUint && hasChild1)
				#else
				if (abs(planeDist) < maxDistance && hasChild1)
				#endif
				{
					PushStackScalar(stackTop, stackBufferScalar, child1);
					//PushStack(stackTop, stackBuffer, child1);
				}
			}
			planeDist = ReadFirstLane(planeDist);
			#if UINT_SPLIT_POS
			if (abs(planeDist) < maxDistanceUint)
			#else
			if (abs(planeDist) < maxDistance)
			#endif
			{
				uint3 probePosUint = uint3(asuint(probePosBuffer[nodeIndex].x), asuint(probePosBuffer[nodeIndex].y), asuint(probePosBuffer[nodeIndex].z));

				int3 probeDiffVecUint = int3(probePosUint) - int3(positionWSUint);
				float3 probeDiffVec = probePosBuffer[nodeIndex].xyz - positionWS;


				float distSqr = dot(probeDiffVec, probeDiffVec);
				int distSqrUint = probeDiffVecUint.x * probeDiffVecUint.x + probeDiffVecUint.y * probeDiffVecUint.y + probeDiffVecUint.z * probeDiffVecUint.z;

				#if UINT_POS
				if (distSqrUint < maxDistSqrUint)
					//positionWS = PosU3ToFloat3(float3(asfloat(positionWSUint.x), asfloat(positionWSUint.y), asfloat(positionWSUint.z)));
					//probeDiffVec = PosU3ToFloat3(probePosBuffer[nodeIndex].xyz) - positionWS;
					//distSqr = dot(probeDiffVec, probeDiffVec);
					//if (distSqr < maxDistSqr)
				#else
				if (distSqr < maxDistSqr)
				#endif
				{
					// now we need to check whether this probe is in target region

					bool ok = true;
#if 0
					if (iBestRegion != -1)
					{
						int numPlanes = pSrt->m_fogSubRegionData[iBestRegion].m_numPlanes;

						bool inside = true;
						//float regionAwayDist = 0.0f;

						for (int iPlane = 0; iPlane < numPlanes; ++iPlane)
						{
							float4 planeEq = pSrt->m_fogPlanes[iGlobalPlane + iPlane];
							float d = dot(planeEq.xyz, probePosBuffer[nodeIndex].xyz /* + wsOffset*/) + planeEq.w;

							if (d > 0)
							{
								inside = false; // fully outside, no need to iterate more
								break;
							}
						}

						//iGlobalPlane += numPlanes;

						ok = inside;

					}
#endif

					//if (pSrt->m_useOcclusion > 0)
#if 0
					{
						uint probeIndex = asuint(probePosBuffer[nodeIndex].w);
						CompressedProbe probe = pSrt->m_probesRO[probeIndex];

						// Shrink bbox for occlusion evaluation to avoid unstable results resulting from bboxes that
						// are too large in comparison to parent mesh.
						float3 probeVec = occlusionTargetPtLS - probePosBuffer[nodeIndex].xyz;
						float probeVecLen = length(probeVec);
						probeVec /= max(probeVecLen, 0.00001f);


						float3x4 localToWorldMat;
						localToWorldMat[0] = pSrt->m_lsToWorldMatrices0[nodeTreeIndex];
						localToWorldMat[1] = pSrt->m_lsToWorldMatrices1[nodeTreeIndex];
						localToWorldMat[2] = pSrt->m_lsToWorldMatrices2[nodeTreeIndex];
						float3 dir = mul(localToWorldMat, float4(probeVec, 0.0f)).xyz;

						float occluderDepth = CalcProbeOccluderDepth(probe.m_occlusion, dir);
						float currentDepth = saturate(probeVecLen / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);

						//float occlusion = 1.0f - smoothstep(occluderDepth * pSrt->m_occlusionScale, occluderDepth, currentDepth + pSrt->m_occlusionBias);
						//weight *= (occluderDepth < 1.0f) ? occlusion : 1.0f;

						ok = occluderDepth > saturate(pSrt->m_probeOcclusionDiscardDistScalar / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);
					}
#endif

					if (ok)
					{
#if UINT_POS
						float distWeight = exp2(-1.0 * distSqrUint / maxDistSqrUint * 8.0f)  ;// exp2(-distSqr / maxDistSqr * 3.0f);
						float weight = distWeight;

						uint distUWeight = distSqrUint;

						uint iWeight;
						//uint probeInfo = EncodeProbeInfo(weight, nodeIndex /*asuint(probePosBuffer[nodeIndex].w)*/, iWeight); // WE ENCODE NODE INDEX FOR POSITION
						uint probeInfo = EncodeProbeInfoUint(distUWeight, nodeIndex /*asuint(probePosBuffer[nodeIndex].w)*/); // WE ENCODE NODE INDEX FOR POSITION
						iWeight = distUWeight;

#else
						float distWeight = exp2(-distSqr / maxDistSqr * 3.0f);
						float weight = distWeight;

						uint iWeight;
						uint probeInfo = EncodeProbeInfo(weight, nodeIndex /*asuint(probePosBuffer[nodeIndex].w)*/, iWeight); // WE ENCODE NODE INDEX FOR POSITION

#endif
						

						if (probeCount < kMaxNumProbeResults)
						{
							//probeInfoList[probeCount] = probeInfo | (probeCount & 0x07f); // 7 bits

							//WriteLane(probeInfoListScalar0, probeInfo | (probeCount & 0x07f), probeCount);
							#if SCALAR_USE_THREE_VECTORS
							if (probeCount >= 128)
								WriteLane(probeInfoListScalar2, probeInfo, probeCount - 128);
							else
							#endif
							#if SCALAR_USE_TWO_VECTORS
							if (probeCount >= 64)
								WriteLane(probeInfoListScalar1, probeInfo, probeCount - 64);
							else
							#endif
								WriteLane(probeInfoListScalar0, probeInfo, probeCount);

							probeCount++;
						}
						else
						{
							#if SCALAR_USE_THREE_VECTORS
							uint probeInfoListScalar = max(probeInfoListScalar0, max(probeInfoListScalar1, probeInfoListScalar2));
							#elif SCALAR_USE_TWO_VECTORS
							uint probeInfoListScalar = max(probeInfoListScalar0, probeInfoListScalar1);
							#else
							uint probeInfoListScalar = probeInfoListScalar0;
							#endif

							uint minZ_01 = /*min*/max(probeInfoListScalar, LaneSwizzle(probeInfoListScalar, 0x1f, 0x00, 0x01));
							uint minZ_02 = /*min*/max(minZ_01, LaneSwizzle(minZ_01, 0x1f, 0x00, 0x02));
							uint minZ_04 = /*min*/max(minZ_02, LaneSwizzle(minZ_02, 0x1f, 0x00, 0x04));
							uint minZ_08 = /*min*/max(minZ_04, LaneSwizzle(minZ_04, 0x1f, 0x00, 0x08));
							uint minZ_10 = /*min*/max(minZ_08, LaneSwizzle(minZ_08, 0x1f, 0x00, 0x10));
							uint minZ =    /*min*/max(ReadLane(minZ_10, 0x00), ReadLane(minZ_10, 0x20));

								
							uint minSlot = minZ;

							if (iWeight < (minSlot & 0xffff0000))
							{
								#if SCALAR_USE_THREE_VECTORS
									ulong lane_mask = __v_cmp_eq_u32(probeInfoListScalar0, minSlot);
									if (lane_mask)
									{
										uint probeInfoIndex = __s_ff1_i32_b64(lane_mask); // figure out which thread is the max thread
										WriteLane(probeInfoListScalar0, probeInfo, probeInfoIndex);

										#if ENABLE_FEEDBACK_DEBUG
										if (debugFroxel)
										{
											uint removedProbeInfo = ReadLane(probeInfoListScalar0, probeInfoIndex);

											uint removedWeight;
											uint removedNodeIndex;
											DecodeProbeInfoUint(removedProbeInfo, removedWeight, removedNodeIndex);
											float3 removedProbePosUint = (probePosBuffer[removedNodeIndex].xyz);
											float3 debugProbePos = PosU3ToFloat3(removedProbePosUint);

											DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 0, 0), -1); // red: removed from list after being added because too many
										}
										#endif
									}
									else
									{
										lane_mask = __v_cmp_eq_u32(probeInfoListScalar1, minSlot);
										if (lane_mask)
										{
											uint probeInfoIndex = __s_ff1_i32_b64(lane_mask); // figure out which thread is the max thread
											
											#if ENABLE_FEEDBACK_DEBUG
											if (debugFroxel)
											{
												uint removedProbeInfo = ReadLane(probeInfoListScalar1, probeInfoIndex);

												uint removedWeight;
												uint removedNodeIndex;
												DecodeProbeInfoUint(removedProbeInfo, removedWeight, removedNodeIndex);
												float3 removedProbePosUint = (probePosBuffer[removedNodeIndex].xyz);
												float3 debugProbePos = PosU3ToFloat3(removedProbePosUint);

												DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 0, 0), -1); // red: removed from list after being added because too many
											}
											#endif

											WriteLane(probeInfoListScalar1, probeInfo, probeInfoIndex);

										}
										else
										{
											lane_mask = __v_cmp_eq_u32(probeInfoListScalar2, minSlot);
											uint probeInfoIndex = __s_ff1_i32_b64(lane_mask); // figure out which thread is the max thread

											#if ENABLE_FEEDBACK_DEBUG
											if (debugFroxel)
											{
												uint removedProbeInfo = ReadLane(probeInfoListScalar2, probeInfoIndex);

												uint removedWeight;
												uint removedNodeIndex;
												DecodeProbeInfoUint(removedProbeInfo, removedWeight, removedNodeIndex);
												float3 removedProbePosUint = (probePosBuffer[removedNodeIndex].xyz);
												float3 debugProbePos = PosU3ToFloat3(removedProbePosUint);

												DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 0, 0), -1); // red: removed from list after being added because too many
											}
											#endif
											WriteLane(probeInfoListScalar2, probeInfo, probeInfoIndex);
										}
									}
								#elif SCALAR_USE_TWO_VECTORS
									ulong lane_mask = __v_cmp_eq_u32(probeInfoListScalar0, minSlot);
									if (lane_mask)
									{
										uint probeInfoIndex = __s_ff1_i32_b64(lane_mask); // figure out which thread is the max thread
										#if ENABLE_FEEDBACK_DEBUG
										if (debugFroxel)
										{
											uint removedProbeInfo = ReadLane(probeInfoListScalar0, probeInfoIndex);

											uint removedWeight;
											uint removedNodeIndex;
											DecodeProbeInfoUint(removedProbeInfo, removedWeight, removedNodeIndex);
											float3 removedProbePosUint = (probePosBuffer[removedNodeIndex].xyz);
											float3 debugProbePos = PosU3ToFloat3(removedProbePosUint);

											DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 0, 0), -1); // red: removed from list after being added because too many
										}
										#endif
										WriteLane(probeInfoListScalar0, probeInfo, probeInfoIndex);
									}
									else
									{
										lane_mask = __v_cmp_eq_u32(probeInfoListScalar1, minSlot);
										uint probeInfoIndex = __s_ff1_i32_b64(lane_mask); // figure out which thread is the max thread
										#if ENABLE_FEEDBACK_DEBUG
										if (debugFroxel)
										{
											uint removedProbeInfo = ReadLane(probeInfoListScalar1, probeInfoIndex);

											uint removedWeight;
											uint removedNodeIndex;
											DecodeProbeInfoUint(removedProbeInfo, removedWeight, removedNodeIndex);
											float3 removedProbePosUint = (probePosBuffer[removedNodeIndex].xyz);
											float3 debugProbePos = PosU3ToFloat3(removedProbePosUint);

											DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 0, 0), -1); // red: removed from list after being added because too many
										}
										#endif
										WriteLane(probeInfoListScalar1, probeInfo, probeInfoIndex);
									}
								#else
									ulong lane_mask = __v_cmp_eq_u32(probeInfoListScalar0, minSlot);
									uint probeInfoIndex = __s_ff1_i32_b64(lane_mask); // figure out which thread is the max thread
									#if ENABLE_FEEDBACK_DEBUG
									if (debugFroxel)
									{
										uint removedProbeInfo = ReadLane(probeInfoListScalar0, probeInfoIndex);

										uint removedWeight;
										uint removedNodeIndex;
										DecodeProbeInfoUint(removedProbeInfo, removedWeight, removedNodeIndex);
										float3 removedProbePosUint = (probePosBuffer[removedNodeIndex].xyz);
										float3 debugProbePos = PosU3ToFloat3(removedProbePosUint);

										DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 0, 0), -1); // red: removed from list after being added because too many
									}
									#endif
									WriteLane(probeInfoListScalar0, probeInfo, probeInfoIndex);
								#endif
							}
							else
							{
								#if ENABLE_FEEDBACK_DEBUG
								if (debugFroxel)
								{
									// discarding because not strong enough
									float3 debugProbePos = PosU3ToFloat3(float3(asfloat(probePosUint.x), asfloat(probePosUint.y), asfloat(probePosUint.z)));
									DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 1, 0), -1); // yellow: not added to the list because too many already
								}
								#endif
							}

							//if (iWeight > (minSlot & 0xfff00000))
							{
								//uint probeInfoIndex = minSlot & 0x07f; // 7 bits
								//WriteLane(probeInfoListScalar0, probeInfo | probeInfoIndex, probeInfoIndex);
							}
						}
						
#if 0

						else
						{
							// find slot with smallest weight
							//uint minSlot = probeInfoList[0];
							uint minSlot = ReadLane(probeInfoListScalar, 0);

							for (uint iSearch = 1; iSearch < kMaxNumProbeResults; ++iSearch)
							{
								//minSlot = min(minSlot, probeInfoList[iSearch]);
								minSlot = min(minSlot, ReadLane(probeInfoListScalar, iSearch));
							}
							/*
							uint minSlot = min(min3(min3(probeInfoList[0], probeInfoList[1], probeInfoList[2]),
								min3(probeInfoList[3], probeInfoList[4], probeInfoList[5]),
								min3(probeInfoList[6], probeInfoList[7], probeInfoList[8])),
								probeInfoList[9]);
							*/
							if (iWeight > (minSlot & 0xfff00000))
							{
								uint probeInfoIndex = minSlot & 0x07f; // 7 bits
								//probeInfoList[probeInfoIndex] = probeInfo | probeInfoIndex;
								WriteLane(probeInfoListScalar, probeInfo | probeInfoIndex, probeInfoIndex);

							}
						}
#endif
					}
				}
			}
		}
	}
}


void ScalarEvaluateProbeForWavefront(inout uint probeInfoListScalar, float3 posWsJitWithOffsetScalar, float3 corner0, float3 corner1, VolumetricsFogSrt *pSrt, uint debugGroup)
{
	bool ok = true;

	float finalFactor = 1.0;

	float3 occlusionTargetPtWS = pSrt->m_camPos + pSrt->m_camDir * 1.5f;

	//float weight;
	uint nodeIndex;
	//DecodeProbeInfo(/*ReadFirstLane*/probeInfoListScalar0, weight, nodeIndex);
	uint weightUint;
	DecodeProbeInfoUint(/*ReadFirstLane*/probeInfoListScalar, weightUint, nodeIndex);

	uint probeIndex = asuint(pSrt->m_probePosRO[nodeIndex].w);
#if USE_SMALL_PROBE
	SmallCompressedProbe probe = pSrt->m_probesSmallRO[probeIndex];
#else
	CompressedProbe probe = pSrt->m_probesRO[probeIndex];
#endif


	// Shrink bbox for occlusion evaluation to avoid unstable results resulting from bboxes that
	// are too large in comparison to parent mesh.

#if UINT_POS
	float3 probePosWs = pSrt->m_probePosROFloats[nodeIndex].xyz; // PosU3ToFloat3(pSrt->m_probePosRO[nodeIndex].xyz);
	float3 probeVec = occlusionTargetPtWS - probePosWs;
#else
	float3 probePosWs = (pSrt->m_probePosRO[nodeIndex].xyz);
	float3 probeVec = occlusionTargetPtWS - probePosWs;
#endif

	float3 probeLeft = cross(float3(0, 1, 0), probeVec);
	float leftVecLen = length(probeLeft);

	probeLeft /= max(leftVecLen, 0.00001f);

	float3 toProbeDistXzSqr = dot(probeVec.xz, probeVec.xz);

	float probeVecLen = length(probeVec);

	probeVec /= max(probeVecLen, 0.00001f);

	float3 probeUp = cross(probeVec, probeLeft); //  no need to normalize

	float3 dir0 = probeVec;
	float kAngle = 3.1415 / 4; // 45 degrees

	float kDiagDistance = saturate(pSrt->m_probeOcclusionDiscardDistScalar * (1 / cos(kAngle)) / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);

	float3 dir1 = cos(kAngle) * probeVec + sin(kAngle) * probeLeft;
	float3 dir2 = cos(kAngle) * probeVec - sin(kAngle) * probeLeft;

	float3 dir3 = cos(kAngle) * probeVec + sin(kAngle) * probeUp;
	float3 dir4 = cos(kAngle) * probeVec - sin(kAngle) * probeUp;



	float occluderDepth0 = CalcProbeOccluderDepth(probe.m_occlusion, dir0);
	float occluderDepth1 = CalcProbeOccluderDepth(probe.m_occlusion, dir1);
	float occluderDepth2 = CalcProbeOccluderDepth(probe.m_occlusion, dir2);
	float occluderDepth3 = CalcProbeOccluderDepth(probe.m_occlusion, dir3);
	float occluderDepth4 = CalcProbeOccluderDepth(probe.m_occlusion, dir4);

	float currentDepth = saturate(probeVecLen / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);

	//float occlusion = 1.0f - smoothstep(occluderDepth * pSrt->m_occlusionScale, occluderDepth, currentDepth + pSrt->m_occlusionBias);
	//weight *= (occluderDepth < 1.0f) ? occlusion : 1.0f;

	bool closeTest = true;

	#if FROXEL_PROBES_USE_OCCLUSION

	float kMaxOccluderDepth = (PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH - PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH / 127.0);
	float testedDepth = min(probeVecLen + 0.5f, min(pSrt->m_probeOcclusionDiscardDistScalar, kMaxOccluderDepth));

	//closeTest = (occluderDepth0 >= saturate(probeVecLen / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH - (PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH / 127.0)));
	closeTest = (occluderDepth0 >= testedDepth / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);

	finalFactor = pSrt->m_probeOcclusionDiscardDistScalar > 0 ? smoothstep(saturate((testedDepth - 0.5) / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH), saturate(testedDepth / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH), occluderDepth0) : 1;

	closeTest = finalFactor > 0;

	#if ENABLE_FEEDBACK_DEBUG
	if (!closeTest && debugGroup)
	{
		float3 debugProbePos = probePosWs;
		DebugProbeStateScalar(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1.0f, 0.41f, 0.70f), 0.0f); // pink: close test failed

		DebugProbeStateFailedOcclusionScalar(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, occluderDepth0 * testedDepth, testedDepth, occlusionTargetPtWS, float3(1.0f, 0.41f, 0.70f));

	}
	#endif

	ok = closeTest;
	#endif

	#if FROXEL_PROBES_USE_OCCLUSION

	if (ok && false)
	{
		float3 vecToCroner0 = corner0 - probePosWs;
		float vecToCroner0Len = length(vecToCroner0);

		vecToCroner0 /= max(vecToCroner0Len, 0.00001f);

		float currentDepthCorner0 = saturate(vecToCroner0Len / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);
		float testedDepthCorner0 = min(vecToCroner0Len + 0.5f, min(pSrt->m_probeOcclusionDiscardDistScalar, kMaxOccluderDepth));

		float occluderDepthCorner0 = CalcProbeOccluderDepth(probe.m_occlusion, vecToCroner0);

		bool corner0Test = (occluderDepthCorner0 >= saturate(testedDepthCorner0 / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH));


		// now check towards two closest corners of the scalar box

		#if ENABLE_FEEDBACK_DEBUG
		if (!corner0Test && debugGroup)
		{
			float3 debugProbePos = probePosWs;
			DebugProbeStateScalar(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1.0f, 0.41f, 0.70f), 0.0f); // pink: close test failed

			DebugProbeStateFailedOcclusionScalar(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, occluderDepthCorner0 * testedDepthCorner0, testedDepthCorner0, corner0, float3(1.0f, 0.41f, 0.70f));

		}
		#endif

		ok = corner0Test;
	}
	

	if (ok && false)
	{
		float3 vecToCroner0 = posWsJitWithOffsetScalar - probePosWs;
		float vecToCroner0Len = length(vecToCroner0);

		vecToCroner0 /= max(vecToCroner0Len, 0.00001f);

		float currentDepthCorner0 = saturate(vecToCroner0Len / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);
		float testedDepthCorner0 = min(vecToCroner0Len + 0.5f, min(pSrt->m_probeOcclusionDiscardDistScalar, kMaxOccluderDepth));

		float occluderDepthCorner0 = CalcProbeOccluderDepth(probe.m_occlusion, vecToCroner0);

		bool corner0Test = (occluderDepthCorner0 >= saturate(testedDepthCorner0 / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH));


		// now check towards two closest corners of the scalar box

		#if ENABLE_FEEDBACK_DEBUG
		if (!corner0Test && debugGroup)
		{
			float3 debugProbePos = probePosWs;
			DebugProbeStateScalar(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1.0f, 0.41f, 0.70f), 0.0f); // pink: close test failed

			DebugProbeStateFailedOcclusionScalar(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, occluderDepthCorner0 * testedDepthCorner0, testedDepthCorner0, corner0, float3(1.0f, 0.41f, 0.70f));

		}
		#endif

		ok = corner0Test;
	}

	bool wasOkBeforeOcclusiontest = ok;


	//testedDepth = pSrt->m_probeOcclusionDiscardDistScalar;
	//
	//ok = ok || (occluderDepth0 >= (saturate(pSrt->m_probeOcclusionDiscardDistScalar / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH)));
	//
	//
	//if (wasOkBeforeOcclusiontest && !ok && debugGroup)
	//{
	//	// failed first occlusion test
	//	float3 debugProbePos = probePosWs;
	//	//DebugProbeStateScalar(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1.0, 0.5, 0.0), 0.0f); // orange: discarded based
	//
	//	DebugProbeStateFailedOcclusionScalar(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, occluderDepth0 * testedDepth, testedDepth, occlusionTargetPtWS, float3(1.0, 0.5, 0.0));
	//}

	wasOkBeforeOcclusiontest = ok;

	bool ok0 = (occluderDepth0 >= (saturate(pSrt->m_probeOcclusionDiscardDistScalar / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH)));

	bool ok1 = (occluderDepth1 >= (kDiagDistance));

	
	// if this probe is above us and hitting occlusion down, can also discard it

	bool doCeilingTest = (toProbeDistXzSqr < pSrt->m_ceilingDiscardTestXZRange) && (probePosWs.y > pSrt->m_ceilingDiscardTestHeightOffset);

	if (doCeilingTest)
	{
		float occluderDepthDown = CalcProbeOccluderDepth(probe.m_occlusion, float3(0, -1, 0));
		bool okCeiling = (occluderDepthDown >= (saturate(pSrt->m_probeOcclusionDiscardDistScalar / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH)));

		//ok = ok && okCeiling;
	}

	bool doFloorTest = (toProbeDistXzSqr < pSrt->m_floorDiscardTestXZRange) && (probePosWs.y < pSrt->m_floorDiscardTestHeightOffset);
	if (doFloorTest)
	{
		float occluderDepthUp = CalcProbeOccluderDepth(probe.m_occlusion, float3(0, 1, 0));
		bool okFloor = (occluderDepthUp >= (saturate(pSrt->m_probeOcclusionDiscardDistScalar / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH)));
		//ok = ok && okFloor;
	}
	
	#if ENABLE_FEEDBACK_DEBUG
	if (wasOkBeforeOcclusiontest && !ok && debugGroup)
	{
		// failed ceiling or floor test
		float3 debugProbePos = probePosWs;
		DebugProbeStateScalar(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1.0, 0.5, 0.0), 0.0f); // orange: discarded based
	}
	#endif

#endif // end of occlusion checks

	// test depth variance

	#if FROXEL_PROBES_USE_DEPTH_VARIANCE_OCCLUSION
	if (ok)
	{
		float4 prevPosHs = mul(float4(probePosWs, 1), pSrt->m_mLastFrameVP);
		float3 prevPosNdc = prevPosHs.xyz / prevPosHs.w;
		//if (abs(prevPosNdc.x) > 1 || abs(prevPosNdc.y) > 1)
		//	ok = false;


		prevPosNdc.x = clamp(prevPosNdc.x, -1.0f, 1.0f);
		prevPosNdc.y = clamp(prevPosNdc.y, -1.0f, 1.0f);


		float3 prevPosView = mul(float4(probePosWs, 1), pSrt->m_mLastFrameV);
		float3 prevPosViewNorm = normalize(prevPosView);

		float3 prevPosUvw;
		prevPosUvw.xy = prevPosNdc.xy * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);
		prevPosUvw.y = 1.0f - prevPosUvw.y;

		uint2 realDim = uint2(24, 14);
		uint2 dim = uint2(192, 108);
		float2 dvUv = prevPosUvw.xy * float2(realDim) / float2(dim);

		float2 prevDepthVariance = pSrt->m_srcVolumetricsDepthInfoPrev.SampleLevel(pSrt->m_linearSampler, float3(dvUv, 6.0), 0).xy;

		if ((abs(prevPosNdc.x) >= 1.0 || abs(prevPosNdc.y) >= 1.0) && pSrt->m_probeOcclusionDepthVarianceThreshold >= 0.0)
		{
			// outside of view
			prevDepthVariance = float2(0, 0);
			finalFactor *= 0;
		}

		if (pSrt->m_probeOcclusionDepthVarianceThreshold >= 0.0)
		{
			finalFactor *= (1.0 - smoothstep(0.9f, 1.0f, max(abs(prevPosNdc.x), abs(prevPosNdc.y))));
		}
		//finalFactor *= (1.0 - abs(prevPosNdc.y));

		float3 toProbe = normalize(probePosWs - pSrt->m_camPosPrev);

		float mean = prevDepthVariance.x;

		float3 meanPos = pSrt->m_camPosPrev + toProbe * (prevDepthVariance.x / prevPosViewNorm.z);
		float variance = abs(prevDepthVariance.y - prevDepthVariance.x * prevDepthVariance.x);
		float3 varianceVec = toProbe * (variance / prevPosViewNorm.z);

		float probabilityOfGeoBehindProbe = prevPosView.z > (mean + 0.01) ? (variance / (variance + (prevPosView.z - mean))) : 1;

		//ok = probabilityOfGeoBehindProbe > pSrt->m_probeOcclusionDepthVarianceThreshold;
		float probabilityFactor = pSrt->m_probeOcclusionDepthVarianceThreshold >= 0 ? smoothstep(pSrt->m_probeOcclusionDepthVarianceThreshold, pSrt->m_probeOcclusionDepthVarianceThreshold + 0.1f, probabilityOfGeoBehindProbe) : 1.0f;
		ok = probabilityFactor > 0;

		finalFactor *= probabilityFactor;

#if ENABLE_FEEDBACK_DEBUG
		if (debugGroup)
		{
			DebugProbeDepthVariance(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, probePosWs, prevDepthVariance, meanPos, varianceVec, probabilityOfGeoBehindProbe);
		}
#endif
	}
	#endif

	
	//if (debugGroup)
	//{
	//	float3 debugProbePos = probePosWs;
	//	DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 1, 1), weightSum); // white: part of result
	//}
	//

	
	//probeInfoListScalar = finalFactor > 0.0001 ? EncodeProbeInfoUint(weightUint * finalFactor, nodeIndex) : 0; // discarding the probe based on occlusion towards camera
	
#if FROXEL_PROBES_USE_OCCLUSION || FROXEL_PROBES_USE_DEPTH_VARIANCE_OCCLUSION
	weightUint = finalFactor * 0xffff; // note we will completely override wight in this register because we will recalculate real weight anyway, and this weight will just be a multiplier
	probeInfoListScalar = (finalFactor > 0.0f) ? EncodeProbeInfoUint(weightUint, nodeIndex) : 0; // discarding the probe based on occlusion towards camera
	//probeInfoListScalar = ok ? probeInfoListScalar : 0; // discarding the probe based on occlusion towards camera
#endif
}

void ScalarEvaluate64ProbesForThreads(inout uint probeInfoListScalar, VolumetricsFogSrt *pSrt,
	inout float weightSum, inout float shadowSum, inout float densitySum, inout DecompressedProbe probeSum,
	int iBestRegionForProbeCull, int probeRegionPlaneOffset, float maxDistSqr, float3 posWsJitWithOffset, uint debugFroxel,
	inout float intesnitySum,
	inout float intesnitySqrSum,
	inout float numSucceededProbes
)
{
	const float kCoeffScale = 1.0f / 127.0f;

	ulong lane_mask = __v_cmp_ne_u32(probeInfoListScalar, 0);
	while (lane_mask)
	{
		uint first_bit = __s_ff1_i32_b64(lane_mask);
		uint i = first_bit;
		lane_mask = __s_andn2_b64(lane_mask, __s_lshl_b64(1, first_bit));

		//				for (uint i = 0; i < probeCount; i++)
		//				{
		float weight;
		uint nodeIndex;

		//DecodeProbeInfo(/*ReadFirstLane*/(probeInfoList[i]), weight, nodeIndex);
		//DecodeProbeInfo(/*ReadFirstLane*/(ReadLane(probeInfoListScalar0, i)), weight, nodeIndex);
		uint weightUint;
		DecodeProbeInfoUint(/*ReadFirstLane*/(ReadLane(probeInfoListScalar, i)), weightUint, nodeIndex);
		
#if FROXEL_PROBES_USE_OCCLUSION || FROXEL_PROBES_USE_DEPTH_VARIANCE_OCCLUSION
		float finalFactor = weightUint / float(0x0000ffff); // 0..1
#else
		float finalFactor = 1.0f;
#endif
		uint probeIndex = asuint(pSrt->m_probePosROFloats[nodeIndex].w); // asuint(pSrt->m_probePosRO[nodeIndex].w);


																		 // recalculate weight for each froxel now
#if UINT_POS
		float3 probePosWs = pSrt->m_probePosROFloats[nodeIndex].xyz;// PosU3ToFloat3(pSrt->m_probePosRO[nodeIndex].xyz);
#else
		float3 probePosWs = (pSrt->m_probePosRO[nodeIndex].xyz);
#endif

		float3 probeDif = probePosWs - posWsJitWithOffset;

		float distSqr = dot(probeDif, probeDif);

		if (distSqr > maxDistSqr)
			continue;

		bool ok = true;

#if FROXEL_PROBES_USE_REGION_CULL && 1
		if (iBestRegionForProbeCull != -1)
		{
			int numPlanes = pSrt->m_fogSubRegionData[iBestRegionForProbeCull].m_numPlanes;
			uint internalPlaneBits = pSrt->m_fogSubRegionData[iBestRegionForProbeCull].m_internalPlaneBits;

			bool probeIsInside = true;
			//float regionAwayDist = 0.0f;

			for (int iPlane = 0; iPlane < numPlanes; ++iPlane)
			{
				float4 planeEq = pSrt->m_fogPlanes[probeRegionPlaneOffset + iPlane];
				float d = dot(planeEq.xyz, probePosWs /* + wsOffset*/) + planeEq.w;
				bool isInternal = internalPlaneBits & (1 << iPlane);

				if (!isInternal && d > 0)
				{
					probeIsInside = false; // fully outside, no need to iterate more
					break;
				}
			}

			//iGlobalPlane += numPlanes;

			ok = probeIsInside;

		}
#endif
		if (!ok)
			continue;

		// also check occlusion
		float distWeight = exp2(-distSqr / maxDistSqr * 8.0f);
		weight = distWeight;

		weight *= finalFactor;

		if (weight < 0.00001)
			continue;

#if USE_SMALL_PROBE
		SmallCompressedProbe probeSmall = pSrt->m_probesSmallRO[probeIndex];
		float probeScale = probeSmall.m_colorScale * weight * kCoeffScale;
		SmallCompressedProbe probe = probeSmall;
#else
		CompressedProbe probe = pSrt->m_probesRO[probeIndex];
		float probeScale = probe.m_colorScale * weight * kCoeffScale;
#endif

		#if FROXEL_PROBES_USE_OCCLUSION
		if (true)
		{
			float3 vecToCroner0 = -probeDif;
			float vecToCroner0Len = length(vecToCroner0);

			vecToCroner0 /= max(vecToCroner0Len, 0.00001f);

			float kMaxOccluderDepth = (PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH - PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH / 127.0);

			float currentDepthCorner0 = saturate(vecToCroner0Len / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);
			float testedDepthCorner0 = min(vecToCroner0Len + 0.5f, min(pSrt->m_probeOcclusionDiscardDistVector, kMaxOccluderDepth));

			float occluderDepthCorner0 = CalcProbeOccluderDepth(probe.m_occlusion, vecToCroner0);

			bool corner0Test = pSrt->m_probeOcclusionDiscardDistVector > 0.0 ? (occluderDepthCorner0 >= saturate(testedDepthCorner0 / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH)) : 1;


			// now check towards two closest corners of the scalar box

#if ENABLE_FEEDBACK_DEBUG
			if (!corner0Test && debugFroxel)
			{
				float3 debugProbePos = probePosWs;
				DebugProbeStateScalar(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1.0f, 0.41f, 0.70f), 0.0f); // pink: close test failed

				DebugProbeStateFailedOcclusionScalar(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, occluderDepthCorner0 * testedDepthCorner0, testedDepthCorner0, posWsJitWithOffset, float3(1.0f, 0.41f, 0.70f));

			}
#endif

			ok = corner0Test;
		}

		if (!ok)
			continue;
		#endif



		#if ENABLE_FEEDBACK_DEBUG
		if (debugFroxel)
		{
			float3 debugProbePos = probePosWs;
			DebugProbeState(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, debugProbePos, 0.5f, float3(1, 1, 1), weight); // white: part of result
		}
		#endif



		DecompressedProbe probeSample;
		float shadowSample;

#if USE_SMALL_PROBE
		float densitySample = (probeSmall.m_addlData & 0x0000FFFF) / 256.0f;
		DecompressSmallProbeWithShadow(probeSmall, probeScale, probeSample, shadowSample);
#else
		float densitySample = (probe.m_addlData & 0x0000FFFF) / 256.0f;
		DecompressProbeWithShadow(probe, probeScale, probeSample, shadowSample);
#endif


		float3 color = float3(probeSample.s0.x / weight, probeSample.s1.x / weight, probeSample.s2.x / weight);

		float intensity2 = dot(color, color);
		float intensity = sqrt(intensity2);

		// adjust weight towards darker colors
		float maxColorDistSqr = 6 * 6;
		float colorWeight = exp2(-intensity2 / maxColorDistSqr * pSrt->m_darkBias);

		weight *= colorWeight;
		probeSample.s0.x *= colorWeight;
		probeSample.s1.x *= colorWeight;
		probeSample.s2.x *= colorWeight;

		float adjustedNumber = weight * 128; // + 1 
		intesnitySum += intensity * adjustedNumber;
		intesnitySqrSum += intensity2 * adjustedNumber;
		numSucceededProbes += adjustedNumber;


		shadowSample *= weight;
		densitySample *= weight;

		probeSum.s0 += probeSample.s0;
		probeSum.s1 += probeSample.s1;
		probeSum.s2 += probeSample.s2;
		probeSum.s3 += probeSample.s3;
		probeSum.s4 += probeSample.s4;
		probeSum.s5 += probeSample.s5;
		probeSum.s6 += probeSample.s6;
		probeSum.s7 += probeSample.s7;
		shadowSum += shadowSample;
		weightSum += weight;
		densitySum += densitySample;

		//if (weight > probeMaxW)
		//{
		//	probeMaxW = weight;
		//	//maxWeightProbe = i;
		//}
	} // while iterating trhough all probes

}

[NUM_THREADS(4, 4, 4)] //
void CS_VolumetricsTemporalCombineProbeCacheFroxelsScalar(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{

	uint2 groupPos = uint2(4, 4) * groupId.xy;

	uint2 pos = groupPos + groupThreadId.xy;

	uint2 posScalar = groupPos + uint2(2, 2);

	uint threadId = groupId.z * 4 * 4 + groupId.y * 4 + groupId.x;

	// march through all slices of froxel grid and accumulate results

	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);


	if (pos.x >= pSrt->m_probeCacheGridSize.x || pos.y >= pSrt->m_probeCacheGridSize.y)
	{
		pos.x = pSrt->m_probeCacheGridSize.x - 1;
		pos.y = pSrt->m_probeCacheGridSize.y - 1;
		//pos = groupPos;
		//	return;  // this wont exit whole wavefront but it will help with divergence issues
	}
	int iSlice = groupId.z * 4 + groupThreadId.z;

	uint debugFroxel = (pos.x == pSrt->m_debugProbes0.x && pos.y == pSrt->m_debugProbes0.y && iSlice == pSrt->m_debugProbes0.z);
	uint debugGroup = __v_cmp_eq_u32(debugFroxel, 1) != 0;

	#if !ENABLE_FEEDBACK_DEBUG
	debugFroxel = 0;
	debugGroup = 0;
	#endif
	int iSliceScalar = groupId.z * 4 + 2;

	if (pos.x == 5 && pos.y == 5 && iSlice == 5)
	{
		//_SCE_BREAK();
	}

	{
		froxelCoord.z = iSlice;


		float ndcX = float(pos.x + 0.5) / float(pSrt->m_probeCacheGridSize.x) * 2.0f - 1.0f;
		float ndcY = /* pSrt->m_probeCacheCellRealToNdcScale * */ float(pos.y + 0.5) / float(pSrt->m_probeCacheGridSize.y) * 2.0f - 1.0f;
		ndcY = -ndcY;


		float ndcXScalar = float(posScalar.x + 0.5) / float(pSrt->m_probeCacheGridSize.x) * 2.0f - 1.0f;
		float ndcYScalar = /* pSrt->m_probeCacheCellRealToNdcScale * */ float(posScalar.y + 0.5) / float(pSrt->m_probeCacheGridSize.y) * 2.0f - 1.0f;
		ndcYScalar = -ndcYScalar;

		// todo: add jitter and temporal

		float viewZJit = FroxelZSliceToCameraDistExp((iSlice + 0.5 + pSrt->m_ndcFogJitterZ) * pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);
		float viewZJitWithOffset = FroxelZSliceToCameraDistExp((iSlice + 0.5 + pSrt->m_ndcFogJitterZ + pSrt->m_probeDepthOffset) * pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);
		float viewZJitWithOffsetScalar = FroxelZSliceToCameraDistExp((iSliceScalar /* + 0.5*/  /* + pSrt->m_ndcFogJitterZ*/ + pSrt->m_probeDepthOffset) * pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);
		float viewZUnjit = FroxelZSliceToCameraDistExp((iSlice + 0.5) * pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);
		float viewZ0Unjit = FroxelZSliceToCameraDistExp((iSlice) * pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);
		float viewZ1Unjit = FroxelZSliceToCameraDistExp((iSlice + 1.0)* pSrt->m_probeCacheZScale, pSrt->m_fogGridOffset);


		float sliceDepth = DepthDerivativeAtSlice((iSlice + 0.5) * pSrt->m_probeCacheZScale) * pSrt->m_probeCacheZScale;
		//float depthOffset = sliceDepth * pSrt->m_probeDepthOffset;

		float ndcZJit = GetNdcDepth(viewZJit, pSrt->m_depthParams);
		float ndcZJitWithOffset = GetNdcDepth(viewZJitWithOffset, pSrt->m_depthParams);
		float ndcZJitWithOffsetScalar = GetNdcDepth(viewZJitWithOffsetScalar, pSrt->m_depthParams);

		float ndcZUnjit = GetNdcDepth(viewZUnjit, pSrt->m_depthParams);
		float ndcZ0Unjit = GetNdcDepth(viewZ0Unjit, pSrt->m_depthParams);
		float ndcZ1Unjit = GetNdcDepth(viewZ1Unjit, pSrt->m_depthParams);

		//float4 posWsH = mul(float4(ndcX, ndcY, ndcZJit, 1), pSrt->m_mVPInv);
		//float3 posWs = posWsH.xyz / posWsH.w;
		
		float4 posWsH = mul(float4(ndcX * viewZJit, ndcY * viewZJit, ndcZJit * viewZJit, viewZJit), pSrt->m_mAltVPInv);
		float3 posWs = posWsH.xyz + pSrt->m_altWorldPos; // / posWsH.w;

		//float4 posWsJitWithOffsetH = mul(float4(ndcX, ndcY, ndcZJitWithOffset, 1), pSrt->m_mVPInv);
		//float3 posWsJitWithOffset = posWsJitWithOffsetH.xyz / posWsJitWithOffsetH.w;

		float4 posWsJitWithOffsetH = mul(float4(ndcX * viewZJitWithOffset, ndcY * viewZJitWithOffset, ndcZJit * viewZJitWithOffset, viewZJitWithOffset), pSrt->m_mAltVPInv);
		float3 posWsJitWithOffset = posWsJitWithOffsetH.xyz + pSrt->m_altWorldPos; // / posWsH.w;

		//float4 posWsJitWithOffsetScalarH = mul(float4(ndcXScalar, ndcYScalar, ndcZJitWithOffsetScalar, 1), pSrt->m_mVPInv);
		//float3 posWsJitWithOffsetScalar = posWsJitWithOffsetScalarH.xyz / posWsJitWithOffsetScalarH.w;
		
		float4 posWsJitWithOffsetScalarH = mul(float4(ndcXScalar * viewZJitWithOffsetScalar, ndcYScalar * viewZJitWithOffsetScalar, ndcZJitWithOffsetScalar * viewZJitWithOffsetScalar, viewZJitWithOffsetScalar), pSrt->m_mAltVPInv);
		float3 posWsJitWithOffsetScalar = posWsJitWithOffsetScalarH.xyz + pSrt->m_altWorldPos; // / posWsH.w;

		//float4 posWsUnjitH = mul(float4(ndcX, ndcY, ndcZUnjit, 1), pSrt->m_mVPInv);
		//float3 posWsUnjit = posWsUnjitH.xyz / posWsUnjitH.w;

		float4 posWsUnjitH = mul(float4(ndcX * viewZUnjit, ndcY * viewZUnjit, ndcZUnjit * viewZUnjit, viewZUnjit), pSrt->m_mAltVPInv);
		float3 posWsUnjit = posWsUnjitH.xyz + pSrt->m_altWorldPos; // / posWsH.w;


		float3 posWs00 = float3(ReadLane(posWsJitWithOffset.x, 0), ReadLane(posWsJitWithOffset.y, 0), ReadLane(posWsJitWithOffset.z, 0));
		float3 posWs10 = float3(ReadLane(posWsJitWithOffset.x, 4), ReadLane(posWsJitWithOffset.y, 4), ReadLane(posWsJitWithOffset.z, 4));
		float3 posWs11 = float3(ReadLane(posWsJitWithOffset.x, 63), ReadLane(posWsJitWithOffset.y, 63), ReadLane(posWsJitWithOffset.z, 63));

		posWsJitWithOffsetScalar = lerp(posWs00, posWs11, 0.5f);

		float3 diag = posWs11 - posWs00; // ReadLane(posWsJitWithOffset.xyz, 0) - ReadLane(posWsJitWithOffset.xyz, 63);

		float groupRadius = length(diag) / 2;

		#if ENABLE_FEEDBACK_DEBUG
		if (debugFroxel)
		{
			DebugProbeFroxel(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, posWs, /*samplingPos*/posWsJitWithOffsetScalar, groupRadius, groupRadius + pSrt->m_maxBlurRadius, float4(1, 0, 0, 1));

			{
				float ndcX0 = float(pos.x) / float(pSrt->m_probeCacheGridSize.x) * 2.0f - 1.0f;
				float ndcY0 = pSrt->m_probeCacheCellRealToNdcScale * float(pos.y) / float(pSrt->m_probeCacheGridSize.y) * 2.0f - 1.0f;
				ndcY0 = -ndcY0;

				float ndcX1 = float(pos.x + 1) / float(pSrt->m_probeCacheGridSize.x) * 2.0f - 1.0f;
				float ndcY1 = pSrt->m_probeCacheCellRealToNdcScale * float(pos.y) / float(pSrt->m_probeCacheGridSize.y) * 2.0f - 1.0f;
				ndcY1 = -ndcY1;

				float ndcX2 = float(pos.x + 1) / float(pSrt->m_probeCacheGridSize.x) * 2.0f - 1.0f;
				float ndcY2 = pSrt->m_probeCacheCellRealToNdcScale * float(pos.y + 1) / float(pSrt->m_probeCacheGridSize.y) * 2.0f - 1.0f;
				ndcY2 = -ndcY2;

				float ndcX3 = float(pos.x) / float(pSrt->m_probeCacheGridSize.x) * 2.0f - 1.0f;
				float ndcY3 = pSrt->m_probeCacheCellRealToNdcScale * float(pos.y + 1) / float(pSrt->m_probeCacheGridSize.y) * 2.0f - 1.0f;
				ndcY3 = -ndcY3;

				{
					float4 posWs0H = mul(float4(ndcX0, ndcY0, ndcZ0Unjit, 1), pSrt->m_mVPInv);
					float3 posWs0 = posWs0H.xyz / posWs0H.w;

					float4 posWs1H = mul(float4(ndcX1, ndcY1, ndcZ0Unjit, 1), pSrt->m_mVPInv);
					float3 posWs1 = posWs1H.xyz / posWs1H.w;

					float4 posWs2H = mul(float4(ndcX2, ndcY2, ndcZ0Unjit, 1), pSrt->m_mVPInv);
					float3 posWs2 = posWs2H.xyz / posWs2H.w;

					float4 posWs3H = mul(float4(ndcX3, ndcY3, ndcZ0Unjit, 1), pSrt->m_mVPInv);
					float3 posWs3 = posWs3H.xyz / posWs3H.w;

					DebugProbeFroxelBox(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, posWs0, posWs1, posWs2, posWs3);
				}

				{
					float4 posWs0H = mul(float4(ndcX0, ndcY0, ndcZ1Unjit, 1), pSrt->m_mVPInv);
					float3 posWs0 = posWs0H.xyz / posWs0H.w;

					float4 posWs1H = mul(float4(ndcX1, ndcY1, ndcZ1Unjit, 1), pSrt->m_mVPInv);
					float3 posWs1 = posWs1H.xyz / posWs1H.w;

					float4 posWs2H = mul(float4(ndcX2, ndcY2, ndcZ1Unjit, 1), pSrt->m_mVPInv);
					float3 posWs2 = posWs2H.xyz / posWs2H.w;

					float4 posWs3H = mul(float4(ndcX3, ndcY3, ndcZ1Unjit, 1), pSrt->m_mVPInv);
					float3 posWs3 = posWs3H.xyz / posWs3H.w;

					DebugProbeFroxelBox(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, posWs0, posWs1, posWs2, posWs3);
				}
			}
		}
		#endif

		
		// go through regions
		int inside = false;
		int fullyInside = false;
		uint probeCullMask = 0;
		float regionAddlDensity = 0;
		float3 regionTint = float3(1.0f, 1.0f, 1.0f);

#define TRACK_PROBE_BEST_REGION 1
		float kFarFromRegion = sliceDepth * pSrt->m_probeCloseToRegionFactor; // 6.0f;

#if FROXEL_CHECK_REGIONS
		int iGlobalPlane = 0;
		int iBestRegion = -1;
		int iBestRegionForProbeCull = -1;
		int probeRegionPlaneOffset = 0;

		[isolate]
		{
			float minRegionAwayDist = 10000000.0f;
			int numSubRegions = pSrt->m_numSubRegions;
			for (int iSubRegion = 0; (iSubRegion < numSubRegions) /* && (!fullyInside)*/; ++iSubRegion)
			{
				int numPlanes = pSrt->m_fogSubRegionData[iSubRegion].m_numPlanes;
				uint internalPlaneBits = pSrt->m_fogSubRegionData[iSubRegion].m_internalPlaneBits;
				float blendOut = pSrt->m_fogSubRegionData[iSubRegion].m_fadeOutRadius; // this could be made a constant specifically for coarse culling
				int doProbeCull = (internalPlaneBits & 0x80000000) ? 0 : -1;
				inside = true;
				//fullyInside = true;
				//float regionAwayDist = 0.0f;
				float regionAwayDist = -blendOut;

				for (int iPlane = 0; iPlane < numPlanes; ++iPlane)
				{
					float4 planeEq = pSrt->m_fogPlanes[iGlobalPlane + iPlane];
					float d = dot(planeEq.xyz, posWs) + planeEq.w;

					bool isInternal = internalPlaneBits & (1 << iPlane);

					if (d > 0)
					{
						inside = false; // fully outside, no need to iterate more
										//fullyInside = false;
#if !TRACK_PROBE_BEST_REGION
						regionAwayDist = 10000000.0f; // will not change closest distance
						break; // can exit early in case we don't care about finding probe region
#endif
					}



#if TRACK_PROBE_BEST_REGION
					if (d <= 0)
#endif
					{
						// if this is internal plane, it shouldn't contribute to closest to edge calculations
						// because it is completely inside combined region
						if (isInternal)
						{
							d = -blendOut;
						}
					}

#if TRACK_PROBE_BEST_REGION
					// we want to keep checking planes even if we know we are outside of plane. we do it because we want to be more generous for
					// choosing a region to cull probes for. the idea here is that for froxels that are close but outside of region
					// we still want to exclude probes that are not in the region to avoid any bleeding
					if (d > kFarFromRegion)
					{
						// ok we can break out of the loop now. we are definitely far from region
						regionAwayDist = 10000000.0f; // will not change closest distance
						break;
					}
#endif

					//if (d > -blendOut)
					//{
					//	fullyInside = false;
					//}

					regionAwayDist = max(regionAwayDist, d);

				}

				if (inside)
				{
					// we are inside of the region, just break out right away, we don't care if we are in any other one.
					minRegionAwayDist = regionAwayDist;
					iBestRegion = iSubRegion;
					iBestRegionForProbeCull = iSubRegion | doProbeCull; // will stay -1 if the region doesn't want the probe culling
					probeRegionPlaneOffset = iGlobalPlane;
					break;
				}


				if (regionAwayDist < minRegionAwayDist)
				{
					minRegionAwayDist = regionAwayDist;
					iBestRegionForProbeCull = iSubRegion | doProbeCull; // will stay -1 if the region doesn't want the probe culling
					probeRegionPlaneOffset = iGlobalPlane;
				}

				iGlobalPlane += numPlanes;

			}



			if (iBestRegion >= 0)
			{
				float blendOut = pSrt->m_fogSubRegionData[iBestRegion].m_fadeOutRadius;

				//float regionBlend = saturate(1.0 - (minRegionAwayDist) / blendOut);

				float regionBlend = saturate(-(minRegionAwayDist) / blendOut);


				//regionBlend *= regionBlend;
				//regionBlend *= regionBlend;

				//regionBlend = regionBlend > 0.0 ? 1.0 : 0.0;

				FogRegionSettings regionSettings = pSrt->m_fogSubRegionData[iBestRegion].m_settings;

				regionAddlDensity = regionBlend * regionSettings.m_fogRegionAddlDensity;
				regionTint.x = regionBlend * regionSettings.m_fogRegionAmbientTintR;
				regionTint.y = regionBlend * regionSettings.m_fogRegionAmbientTintG;
				regionTint.z = regionBlend * regionSettings.m_fogRegionAmbientTintB;
			}
		}
#else
		int iBestRegion = -1;
		int iBestRegionForProbeCull = -1;
		int probeRegionPlaneOffset = 0;
#endif
		// force all probes culled by first region
		//iBestRegionForProbeCull = 0;
		//probeRegionPlaneOffset = 0;
		//regionAddlDensity = 0;
		//regionTint.xyz = float3(1.0, 1.0, 1.0);

		// forse no leak protection
		//iBestRegionForProbeCull = -1;
		//probeRegionPlaneOffset = 0;

		//inside = false;
		float maxIntensity = 100;

		// get sky data
#if		USE_MIP_SKY_FOG
		float3 skyColor;
		float skyColorContribution;
		{
			float3 dirToFroxel = posWs - pSrt->m_camPos;

			dirToFroxel = normalize(dirToFroxel);

			float2 texUvCoord = GetFogSphericalCoords(dirToFroxel, pSrt->m_skyAngleOffset);
			//float numMipLevels = 8;
			//float mipBias = (1.0 - sqrt(saturate((pixelDepth - pSrt->m_mipfadeDistance) / (numMipLevels*pSrt->m_mipfadeDistance))))*numMipLevels;
			float mipBias = 0;

			float4 mipFogColor = pSrt->m_skyTexture.SampleLevel(SetWrapSampleMode(pSrt->m_linearSampler), texUvCoord, mipBias);
			skyColor = mipFogColor.xyz * pSrt->m_skyMipNormalizationFactor;

			skyColorContribution = saturate(1.0 + (viewZUnjit - pSrt->m_skyMipTextureBlendInDist) / 16.0);
		}

		// get probe data

		float4 resultData = float4(skyColor, 0.5);

		uint needProbes = skyColorContribution < 1.0f;

		if (__v_cmp_eq_u32(needProbes, 1)) // if any thread needs probes, we go into if, since we need all 64 threads
#else
		float4 resultData = float4(0, 0, 0, 0);

#endif
		{
			float3 probeSamplePos = ReadFirstLane(posWsJitWithOffsetScalar); // posWs
			uint probeInfoListScalar0 = 0;
			#if SCALAR_USE_TWO_VECTORS
			uint probeInfoListScalar1 = 0;
			#endif

			#if SCALAR_USE_THREE_VECTORS
			uint probeInfoListScalar2 = 0;
			#endif
			uint probeCount = 0;
			for (uint i = 0; i < pSrt->m_numNodeTrees; i++)
			{
				float radius = i < pSrt->m_numLod0Trees ? pSrt->m_maxBlurRadius : 15.0f;
				
				radius += groupRadius;

				float3x4 worldToLocalMat;
				worldToLocalMat[0] = pSrt->m_wsToLsMatrices0[i];
				worldToLocalMat[1] = pSrt->m_wsToLsMatrices1[i];
				worldToLocalMat[2] = pSrt->m_wsToLsMatrices2[i];
				float3 positionLS = mul(worldToLocalMat, float4(probeSamplePos, 1.0f)).xyz;

				float3 camPosLs = mul(worldToLocalMat, float4(pSrt->m_camPos, 1.0f)).xyz;

				// we are making assumpion that there is no rotation involved for the probe level
				float3 wsOffset = float3(-worldToLocalMat[0].w, -worldToLocalMat[1].w, -worldToLocalMat[2].w);
				FindProbeNearestNeighborsScalar(i, probeInfoListScalar0,
					#if SCALAR_USE_TWO_VECTORS
					probeInfoListScalar1,
					#endif
					#if SCALAR_USE_THREE_VECTORS
					probeInfoListScalar2,
					#endif
					probeCount, pSrt->m_rootIndices[i],
					pSrt->m_centerPosLs[i].xyz, pSrt->m_edgeLengthLs[i].xyz,
					radius, positionLS, probeSamplePos, camPosLs, wsOffset,
					pSrt->m_probeNodesRO, pSrt->m_probePosRO, pSrt, probeRegionPlaneOffset, iBestRegionForProbeCull, debugFroxel);
			}

			const float kCoeffScale = 1.0f / 127.0f;
			float radius = pSrt->m_maxBlurRadius;

			const float maxDistSqr = radius * radius; // this would break with lod 1..

			//if (probeCount == 0)
			//{
			//	probeSum.s0.x *= pSrt->m_defaultProbeTint.x;
			//	probeSum.s1.x *= pSrt->m_defaultProbeTint.y;
			//	probeSum.s2.x *= pSrt->m_defaultProbeTint.z;
			//}
			//
			//if (probeCount == 0 && iBestRegion >= 0)
			//{
			//	float4 probe = pSrt->m_fogSubRegionData[iBestRegion].m_probe;
			//	if (probe.w > 0.0f)
			//	{
			//		weightSum = 0.01f; // a little bit of default probe from region
			//		probeSum.s0.x = probe.x * weightSum;
			//		probeSum.s1.x = probe.y * weightSum;
			//		probeSum.s2.x = probe.z * weightSum;
			//	}
			//}


			//float probeMaxW = weightSum;


#pragma warning(disable : 8201)
			float weightSum = 0;
			float shadowSum = 0;
			float densitySum = 0;
			DecompressedProbe probeSum = DecompressedProbe(0);
			
			probeCount = ReadFirstLane(probeCount);
			//if (probeCount == 64)

			float3 defaultProbeTint = pSrt->m_defaultProbeTint.xyz;
			
			//defaultProbeTint = float3(0, 0, 0);

			if (false && groupPos.x == 4 && groupPos.y == 4 && iSliceScalar == 30)
			{
				probeInfoListScalar0 = 0;
				probeInfoListScalar1 = 0;
				
				#if SCALAR_USE_THREE_VECTORS
				probeInfoListScalar2 = 0;
				#endif

				weightSum = 0;

				if (probeCount == 0)
				{
					defaultProbeTint = float3(0, 1, 1);
				}
				else if (probeCount < 32)
				{
					defaultProbeTint = float3(0, 1, 0);
				}
				else if (probeCount < 64)
				{
					defaultProbeTint = float3(1, 1, 0);
				}
				else if (probeCount < 96)
				{
					defaultProbeTint = float3(0, 0, 1);
				}
				else if (probeCount < 128)
				{
					defaultProbeTint = float3(1, 0, 1);
				}
				else if (probeCount == 192)
				{
					defaultProbeTint = float3(1, 0, 0);
				}
				probeCount = 0;
			}

			// accumulate probe samples
			//if (probeCount > 0)
			[isolate]
			{
				// now each thread will grab its probe and evaluate occlusion

				if (probeInfoListScalar0 != 0)
				{
					[isolate] 
					{
						ScalarEvaluateProbeForWavefront(probeInfoListScalar0, posWsJitWithOffsetScalar, posWs00, posWs10, pSrt, debugGroup);
					}
				}

				
				#if SCALAR_USE_TWO_VECTORS
				if (probeInfoListScalar1 != 0)
				{
					[isolate]
					{
						ScalarEvaluateProbeForWavefront(probeInfoListScalar1, posWsJitWithOffsetScalar, posWs00, posWs10, pSrt, debugGroup);
					}
				}
				#endif
				#if SCALAR_USE_THREE_VECTORS
				if (probeInfoListScalar2 != 0)
				{
					[isolate]
					{
						ScalarEvaluateProbeForWavefront(probeInfoListScalar2, posWsJitWithOffsetScalar, posWs00, posWs10, pSrt, debugGroup);
					}
				}
				#endif
			}

			float intesnitySum = 0;
			float intesnitySqrSum = 0;
			float numSucceededProbes = 0;
			ScalarEvaluate64ProbesForThreads(probeInfoListScalar0, pSrt, weightSum, shadowSum, densitySum, probeSum, iBestRegionForProbeCull, probeRegionPlaneOffset, maxDistSqr, posWsJitWithOffset, debugFroxel, intesnitySum, intesnitySqrSum, numSucceededProbes);

			#if SCALAR_USE_TWO_VECTORS
			ScalarEvaluate64ProbesForThreads(probeInfoListScalar1, pSrt, weightSum, shadowSum, densitySum, probeSum, iBestRegionForProbeCull, probeRegionPlaneOffset, maxDistSqr, posWsJitWithOffset, debugFroxel, intesnitySum, intesnitySqrSum, numSucceededProbes);
			#endif
			#if SCALAR_USE_THREE_VECTORS
			ScalarEvaluate64ProbesForThreads(probeInfoListScalar2, pSrt, weightSum, shadowSum, densitySum, probeSum, iBestRegionForProbeCull, probeRegionPlaneOffset, maxDistSqr, posWsJitWithOffset, debugFroxel, intesnitySum, intesnitySqrSum, numSucceededProbes);
			#endif
#if 0
			ulong lane_mask = __v_cmp_ne_u32(probeInfoListScalar0, 0);
			while (lane_mask)
			{
				uint first_bit = __s_ff1_i32_b64(lane_mask);
				uint i = first_bit;
				lane_mask = __s_andn2_b64(lane_mask, __s_lshl_b64(1, first_bit));

//				for (uint i = 0; i < probeCount; i++)
//				{
				float weight;
				uint nodeIndex;

				//DecodeProbeInfo(/*ReadFirstLane*/(probeInfoList[i]), weight, nodeIndex);
				//DecodeProbeInfo(/*ReadFirstLane*/(ReadLane(probeInfoListScalar0, i)), weight, nodeIndex);
				uint weightUint;
				DecodeProbeInfoUint(/*ReadFirstLane*/(ReadLane(probeInfoListScalar0, i)), weightUint, nodeIndex);
				uint probeIndex = asuint(pSrt->m_probePosROFloats[nodeIndex].w); // asuint(pSrt->m_probePosRO[nodeIndex].w);


				// recalculate weight for each froxel now
				#if UINT_POS
				float3 probePosWs = pSrt->m_probePosROFloats[nodeIndex].xyz;// PosU3ToFloat3(pSrt->m_probePosRO[nodeIndex].xyz);
				#else
				float3 probePosWs = (pSrt->m_probePosRO[nodeIndex].xyz);
				#endif

				float3 probeDif = probePosWs - posWsJitWithOffset;

				float distSqr = dot(probeDif, probeDif);

				if (distSqr > maxDistSqr)
					continue;

				bool ok = true;

#if FROXEL_PROBES_USE_REGION_CULL && 1
				if (iBestRegionForProbeCull != -1)
				{
					int numPlanes = pSrt->m_fogSubRegionData[iBestRegionForProbeCull].m_numPlanes;

					bool probeIsInside = true;
					//float regionAwayDist = 0.0f;

					for (int iPlane = 0; iPlane < numPlanes; ++iPlane)
					{
						float4 planeEq = pSrt->m_fogPlanes[probeRegionPlaneOffset + iPlane];
						float d = dot(planeEq.xyz, probePosWs /* + wsOffset*/) + planeEq.w;

						if (d > 0)
						{
							probeIsInside = false; // fully outside, no need to iterate more
							break;
						}
					}

					//iGlobalPlane += numPlanes;

					ok = probeIsInside;

				}
#endif
				if (!ok)
					continue;


				float distWeight = exp2(-distSqr / maxDistSqr * 3.0f);
				weight = distWeight;

#if USE_SMALL_PROBE
				SmallCompressedProbe probeSmall = pSrt->m_probesSmallRO[probeIndex];
				float probeScale = probeSmall.m_colorScale * weight * kCoeffScale;
#else
				CompressedProbe probe = pSrt->m_probesRO[probeIndex];
				float probeScale = probe.m_colorScale * weight * kCoeffScale;
#endif


				DecompressedProbe probeSample;
				float shadowSample;

#if USE_SMALL_PROBE
				float densitySample = (probeSmall.m_addlData & 0x0000FFFF) / 256.0f;
				DecompressSmallProbeWithShadow(probeSmall, probeScale, probeSample, shadowSample);
#else
				float densitySample = (probe.m_addlData & 0x0000FFFF) / 256.0f;
				DecompressProbeWithShadow(probe, probeScale, probeSample, shadowSample);
#endif




				shadowSample *= weight;
				densitySample *= weight;

				probeSum.s0 += probeSample.s0;
				probeSum.s1 += probeSample.s1;
				probeSum.s2 += probeSample.s2;
				probeSum.s3 += probeSample.s3;
				probeSum.s4 += probeSample.s4;
				probeSum.s5 += probeSample.s5;
				probeSum.s6 += probeSample.s6;
				probeSum.s7 += probeSample.s7;
				shadowSum += shadowSample;
				weightSum += weight;
				densitySum += densitySample;

				//if (weight > probeMaxW)
				//{
				//	probeMaxW = weight;
				//	//maxWeightProbe = i;
				//}
			} // while iterating trhough all probes
#endif
			float variance = -1;
			float mean = -1;
			if (weightSum == 0.0f)
			{
				// did not find any probes for this froxel
				weightSum = 1.0f;

				#if USE_SMALL_PROBE
				SmallCompressedProbe firstProbeSmall = pSrt->m_defaultProbeSmall;
				float probeScale = firstProbeSmall.m_colorScale * kCoeffScale;
				#else
				CompressedProbe firstProbe = pSrt->m_defaultProbe;
				float probeScale = firstProbe.m_colorScale * kCoeffScale;
				#endif


				#if USE_SMALL_PROBE
				densitySum = (firstProbeSmall.m_addlData & 0x0000FFFF) / 256.0f;
				DecompressSmallProbeWithShadow(firstProbeSmall, probeScale, probeSum, shadowSum);
				#else
				densitySum = (firstProbe.m_addlData & 0x0000FFFF) / 256.0f;
				DecompressProbeWithShadow(firstProbe, probeScale, probeSum, shadowSum);
				#endif

				probeSum.s0.x *= defaultProbeTint.x;
				probeSum.s1.x *= defaultProbeTint.y;
				probeSum.s2.x *= defaultProbeTint.z;

				//if (iBestRegion >= 0)
				//{
				//	float4 probe = pSrt->m_fogSubRegionData[iBestRegion].m_probe;
				//	if (probe.w > 0.0f)
				//	{
				//		//weightSum = 0.01f; // a little bit of default probe from region
				//		probeSum.s0.x = probe.x * weightSum;
				//		probeSum.s1.x = probe.y * weightSum;
				//		probeSum.s2.x = probe.z * weightSum;
				//	}
				//}
			}
			else
			{
				// perfrom variance clamping
				// variance was computed just on color intensity of contributing probes, not taking probe weights into account
				mean = intesnitySum / numSucceededProbes;
				variance = intesnitySqrSum / numSucceededProbes - mean * mean;
				float kMaxAllowedVariance = pSrt->m_probeColorVarianceThreshold;
				maxIntensity = max(0, mean + kMaxAllowedVariance * variance);
			}




			float invWeightSum = 1.0f / max(weightSum, 0.0001f);

			// normalize accumulated probes
			probeSum.s0 *= invWeightSum;
			probeSum.s1 *= invWeightSum;
			probeSum.s2 *= invWeightSum;
			probeSum.s3 *= invWeightSum;
			probeSum.s4 *= invWeightSum;
			probeSum.s5 *= invWeightSum;
			probeSum.s6 *= invWeightSum;
			probeSum.s7 *= invWeightSum;
			shadowSum *= invWeightSum;
			//densitySum *= invWeightSum;

			float intensity = length(float3(probeSum.s0.x, probeSum.s1.x, probeSum.s2.x));


			float3 band0RGB = float3(probeSum.s0.x, probeSum.s1.x, probeSum.s2.x) / max(0.0001, intensity) * min(intensity, maxIntensity);
			//float3 band0RGB = float3(probeSum.s0.x, probeSum.s1.x, probeSum.s2.x);

			#if ENABLE_FEEDBACK_DEBUG
			if (debugFroxel)
			{
				DebugProbeColorVariance(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, posWs,
					variance, mean, maxIntensity, intensity, band0RGB);
			}
			#endif


			if (probeCount == 0)
			{
				//	band0RGB = float3(1, 0, 0);
			}


			densitySum = regionAddlDensity * 0.5f + FROXELS_REGION_DENSITY_MID_POINT; // convert -1 .. 1 to 0 to 1
#if		USE_MIP_SKY_FOG
			resultData = float4(regionTint * lerp(band0RGB, skyColor, skyColorContribution), densitySum /*shadowSum*/);
#else
			resultData = float4(regionTint * band0RGB, densitySum /*shadowSum*/);
#endif
		}
	else
	{
		// we are fully lighting with sky, but we still want to process regions

		float densitySum = regionAddlDensity * 0.5f + FROXELS_REGION_DENSITY_MID_POINT;
		resultData.rgb *= regionTint;
		resultData.a = densitySum;

	}


	// and now combine with previous frame

	// to find previous value in the grid, we need to first find the world position of current sample and the project it into previous frame

	float4 posLastFrameH = mul(float4(posWsUnjit, 1), pSrt->m_mLastFrameVP);
	float3 posLastFrameNdc = posLastFrameH.xyz / posLastFrameH.w;

	// now go from ndc to uv coords

	float3 uv3d;
	uv3d.xy = posLastFrameNdc.xy * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);

	uv3d.y = 1.0f - uv3d.y;

	float ndcDepth = posLastFrameNdc.z;

	float linearDepth = GetLinearDepth(ndcDepth, pSrt->m_lastFrameDepthParams);

	uv3d.z = CameraLinearDepthToFroxelZCoordExp(linearDepth, pSrt->m_fogGridOffset);
	uv3d.z = clamp(uv3d.z, 0.0f, 1.0f);

	float4 resultValue = resultData;
#if DISABLE_TEMPORAL_IN_FOG

#else

	//uv3d.xy = float2(0.5, 0.5);
	//float4 prevVal = pSrt->m_srcProbeCacheFroxelsPrev[froxelCoord];
	float4 prevVal = pSrt->m_srcProbeCacheFroxelsPrev.SampleLevel(pSrt->m_linearSampler, uv3d, 0).rgba;

#if USE_MANUAL_POW_FROXELS
	prevVal.xyz *= prevVal.xyz;
#endif

	prevVal.xyz *= 8.0f;

	float intensity = length(prevVal.xyz);

	//prevVal.xyz = prevVal.xyz / max(0.0001, intensity) * min(intensity, maxIntensity);

	float newWeight = 0.05;

	const float kFroxelHalfUvZ = 0.5f / 64.0f;
	const float kFroxelHalfUvZInv = 64.0 * 2;

	uint3 texDim = uint3(1, 1, 1);
	pSrt->m_srcProbeCacheFroxelsPrev.GetDimensionsFast(texDim.x, texDim.y, texDim.z);



	const float kFroxelHalfUvX = 0.5f / float(texDim.x);
	const float kFroxelHalfUvY = 0.5f / float(texDim.y);

	const float kFroxelHalfUvXInv = float(texDim.x) * 2;
	const float kFroxelHalfUvYInv = float(texDim.y) * 2;


	float awayDistX0 = saturate((kFroxelHalfUvX - uv3d.x) * kFroxelHalfUvXInv);
	float awayDistY0 = saturate((kFroxelHalfUvY - uv3d.y) * kFroxelHalfUvYInv);
	float awayDistZ0 = saturate((kFroxelHalfUvZ - uv3d.z) * kFroxelHalfUvZInv);

	float awayDistX1 = saturate((uv3d.x - (1.0 - kFroxelHalfUvX)) * kFroxelHalfUvXInv);
	float awayDistY1 = saturate((uv3d.y - (1.0 - kFroxelHalfUvY)) * kFroxelHalfUvYInv);

	float awayDist = saturate((awayDistX0 + awayDistY0 + awayDistZ0 + awayDistX1 + awayDistY1) * (0.75 + 0.5 * ((pos.x + pos.y + iSlice) % 2)));

	newWeight = awayDist * (1.0 - newWeight) + newWeight;



	resultValue = resultData * newWeight + prevVal * (1 - newWeight);

	// we don't have temporal on density channel
	resultValue.a = resultData.a;

#endif

	resultValue.xyz = resultValue.xyz / 8.0;

#if USE_MANUAL_POW_FROXELS
	resultValue.xyz = sqrt(resultValue.xyz);
#endif

	if (any(isnan(resultValue.xyz)))
	{
		resultValue.xyz = float3(0, 0, 0);
	}
	
	pSrt->m_destProbeCacheFroxels[froxelCoord] = resultValue;
	}
}




static const float EPSILON = 0.0001f;

// Right now CalculateBlurredDitherShadow() clamps the shadow mask to a minimum of 1.0f / 255.0f.
// Therefore we use this value as well to determine if we need to calculate SSS and to clamp the
// final shadow value. As soon as the clamp will be removed from CalculateBlurredDitherShadow(),
// the value below must be adjusted accordingly.
static const float FULLY_INSIDE_UMBRA = (1.0f / 255.0f) + EPSILON;

[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeScreenSpaceShadowIntersections(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 pos = groupPos + groupThreadId.xy;


	// first we analyze 2d screen and see which froxels intersect with depth buffer. if they are in shadow, we will mark them to not receive light
	uint shadowX = pos.x * FogFroxelGridSizeNativeRes;// +FogFroxelGridSizeNativeRes / 2;
	uint shadowY = pos.y * FogFroxelGridSizeNativeRes;// +FogFroxelGridSizeNativeRes / 2;


	shadowX += 3 * (pSrt->m_frameNumber % 4);
	shadowY += 3 * ((FogFroxelGridSizeNativeRes / 4) % 4);

	float shadowMask = pSrt->m_srcShadowInfoSurface[uint2(shadowX, shadowY)].x;
	float depthVal = pSrt->m_depthTexture[uint2(shadowX, shadowY)];

	const int numSlices = 64;
	uint3 froxelCoord = uint3(pos, 0);

	


	for (int iSlice = 0; iSlice < numSlices; ++iSlice)
	{
		froxelCoord.z = iSlice;


		float ndcX = float(pos.x + 0.5) / float(pSrt->m_fogGridSize.x) * 2.0f - 1.0f;
		float ndcY = float(pos.y + 0.5) / float(pSrt->m_fogGridSize.y) * 2.0f - 1.0f;
		ndcY = -ndcY;

		// we also add jitter to lookup to force a blur, this is not proper temporal AA
		//ndcX += pSrt->m_ndcFogJitter.x;

		float viewZ = FroxelZSliceToCameraDistExp(iSlice + 0.5, pSrt->m_fogGridOffset);

		float ndcZ = GetNdcDepth(viewZ, pSrt->m_depthParams);

		float4 posWsH = mul(float4(ndcX, ndcY, ndcZ, 1), pSrt->m_mVPInv);
		float3 posWs = posWsH.xyz / posWsH.w;

		//TODO: change to src once is in new compute job
		float2 resultData = pSrt->m_destPropertiesFroxels[froxelCoord];


		// and now combine with previous frame

		// to find previous value in the grid, we need to first find the world position of current sample and the project it into previous frame

		float4 posLastFrameH = mul(float4(posWs, 1), pSrt->m_mLastFrameVP);
		float3 posLastFrameNdc = posLastFrameH.xyz / posLastFrameH.w;

		// now go from ndc to uv coords

		float3 uv3d;
		uv3d.xy = posLastFrameNdc.xy * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);

		uv3d.y = 1.0f - uv3d.y;

		float ndcDepth = posLastFrameNdc.z;

		float linearDepth = GetLinearDepth(ndcDepth, pSrt->m_lastFrameDepthParams);
		uv3d.z = CameraLinearDepthToFroxelZCoordExp(linearDepth, pSrt->m_fogGridOffset);
		uv3d.z = clamp(uv3d.z, 0.0f, 1.0f);



		//uv3d.xy = float2(0.5, 0.5);
		//float4 prevVal = pSrt->m_srcFogFroxelsPrev[froxelCoord];
		//float4 prevVal = pSrt->m_srcFogFroxelsPrev.SampleLevel(pSrt->m_linearSampler, uv3d, 0).rgba;

		//float4 prevVal = pSrt->m_srcPropertiesFroxelsPrev.SampleLevel(pSrt->m_linearSampler, uv3d, 0).rgba;

		//uv3d = float3(float(pos.x + 0.5) / float(pSrt->m_fogGridSize.x), float(pos.y + 0.5) / float(pSrt->m_fogGridSize.y), (froxelCoord.z + 0.5) / 64.0);

		float2 prevVal = float2(0, 0); // pSrt->m_srcPropertiesFroxelsPrev.SampleLevel(pSrt->m_linearSampler, uv3d, 0).rg;
		//float4 prevVal = pSrt->m_srcPropertiesFroxelsPrev[froxelCoord];

		float2 resultValue = resultData.xy * 0.1 + prevVal.xy * 0.9;
		
		// for now don't use temporal
		//resultValue.xy = resultData.xy;

		pSrt->m_destPropertiesFroxels[froxelCoord].y = resultValue.y;

		pSrt->m_destPropertiesFroxels[froxelCoord].y = 0;
	}

	/*
	if (shadowMask < 1.0f)
	{
		// find the froxel that is intersecting with depth
		float linearDepth = GetLinearDepth(depthVal, pSrt->m_depthParams);

		float froxelSliceFloat = CameraLinearDepthToFroxelZSliceExp(linearDepth, pSrt->m_fogGridOffset);
		float froxelZ = froxelSliceFloat - 1.0;

		for (int iSlice = froxelZ; iSlice <= froxelZ + 2; ++iSlice)
		{
			froxelCoord.z = iSlice;

			pSrt->m_destPropertiesFroxels[froxelCoord].y = 1.0 - shadowMask;

		}
	}
	*/
	

	// brute force..
	for (int iY = 0; iY < FogFroxelGridSizeNativeRes; ++iY)
	{
		for (int iX = 0; iX < FogFroxelGridSizeNativeRes; ++iX)
		{
			//shadowX += 3 * (pSrt->m_frameNumber % 4);
			//shadowY += 3 * ((FogFroxelGridSizeNativeRes / 4) % 4);

			shadowMask = pSrt->m_srcShadowInfoSurface[uint2(shadowX + iX, shadowY + iY)].x;
			depthVal = pSrt->m_depthTexture[uint2(shadowX + iX, shadowY + iY)];

			float linearDepth = GetLinearDepth(depthVal, pSrt->m_depthParams);

			float froxelSliceFloat = CameraLinearDepthToFroxelZSliceExp(linearDepth, pSrt->m_fogGridOffset);
			float froxelZ = froxelSliceFloat - 1.0;

			for (int iSlice = froxelZ; iSlice <= froxelZ + 5; ++iSlice)
			{
				froxelCoord.z = iSlice;

				pSrt->m_destPropertiesFroxels[froxelCoord].y = max(pSrt->m_destPropertiesFroxels[froxelCoord].y, 1.0 - shadowMask);
			}

			//pSrt->m_destVolumetricsScreenSpaceInfo[blockId].x = maxSlice + 1 + 128;
		}
	}

}

[NUM_THREADS(8, 8, 1)] //
void CS_ExpandFroxelDepths0(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	// 2x2 htiles correspond to single destination tile

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint xMod4 = groupId.x % 4;
	uint yMod4 = groupId.y % 4;

	uint groupIdMod16 = yMod4 * 4 + xMod4;

	uint2 blockId = groupPos + groupThreadId.xy;

	// check myself and all neighbors
	float maxSlice = 0;
	int counter = blockId.x + blockId.y + pSrt->m_frameNumber;

	int numSteps = 1;


	int lastSliceInEarlyPassPlusOne = pSrt->m_srcVolumetricsReprojectedScreenSpaceInfo[groupId].x;

	for (int y = -numSteps; y <= numSteps; ++y)
	{
		for (int x = -numSteps; x <= numSteps; ++x)
		{
			maxSlice = max(maxSlice, pSrt->m_srcVolumetricsScreenSpaceInfoOrig[uint2(blockId.x + x, blockId.y + y)].x);
		}
	}
	
	//maxSlice = max(maxSlice, lastSliceInEarlyPassPlusOne);

	{
		pSrt->m_destVolumetricsScreenSpaceInfo[blockId].x = maxSlice;
	}
}


[NUM_THREADS(8, 8, 1)] //
void CS_ExpandFroxelDepths1(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	// 2x2 htiles correspond to single destination tile

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint xMod4 = groupId.x % 4;
	uint yMod4 = groupId.y % 4;

	uint groupIdMod16 = yMod4 * 4 + xMod4;

	
	uint2 blockId = groupPos + groupThreadId.xy;

	// check myself and all neighbors
	float maxSlice = 0;
	int counter = blockId.x + blockId.y + pSrt->m_frameNumber;

	int numSteps = 1;

	for (int y = -numSteps; y <= numSteps; ++y)
	{
		for (int x = -numSteps; x <= numSteps; ++x)
		{
			maxSlice = max(maxSlice, pSrt->m_srcVolumetricsScreenSpaceInfo[uint2(blockId.x + x, blockId.y + y)].x);
		}
	}

	{
		pSrt->m_destVolumetricsScreenSpaceInfoOrig[blockId].x = maxSlice;
	}
}

[NUM_THREADS(8, 8, 1)] //
void CS_FindFroxelMaxNeighborDepths(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	// 2x2 htiles correspond to single destination tile

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint xMod4 = groupId.x % 4;
	uint yMod4 = groupId.y % 4;

	uint groupIdMod16 = yMod4 * 4 + xMod4;

	uint2 blockId = groupPos + groupThreadId.xy;

	// check myself and all neighbors
	float maxSlice = 0;
	int counter = blockId.x + blockId.y + pSrt->m_frameNumber;
	int numSteps = pSrt->m_numNeighborSteps;

	for (int y = -numSteps; y <= numSteps; ++y)
	{
		for (int x = -numSteps; x <= numSteps; ++x)
		{
			maxSlice = max(maxSlice, pSrt->m_srcVolumetricsScreenSpaceInfoOrig[uint2(blockId.x + x, blockId.y + y)].x);
		}
	}

#if FROXEL_MAX_DEPTH_MIN_DEPTH_SPECIFIED
	maxSlice = max(maxSlice, CameraLinearDepthToFroxelZSliceExp(pSrt->m_minDepth, pSrt->m_fogGridOffset) + 1);
#endif

#if FROXEL_MAX_DEPTH_REFRESH_BEHIND_GEO_16TH
	if (groupIdMod16 == (pSrt->m_frameNumber % 16))
	{
		// for 1/16th of the screen, we always update all froxels
		pSrt->m_destVolumetricsScreenSpaceInfo[blockId].x = maxSlice + 1 + 128; // each slice after maxSlice + 2 will be updated at 1.0 convergance rate. we do + 2 just to make sure we don't touch anything that could potentially be sampled
	}
	else
#endif

	{
		pSrt->m_destVolumetricsScreenSpaceInfo[blockId].x = maxSlice;
	}

#if FROXEL_MAX_DEPTH_REFRESH_ALL_ALWAYS
	//pSrt->m_destVolumetricsScreenSpaceInfoOrig[blockId].x = 64;
	pSrt->m_destVolumetricsScreenSpaceInfo[blockId].x = 64;
#endif


#if FROXEL_MAX_DEPTH_REFRESH_BEHIND_GEO_NEWLY_REVEALED

	float ndcX = float(blockId.x + 0.5) * pSrt->m_numFroxelsXInv * 2.0f - 1.0f;
	float ndcY = float(blockId.y + 0.5) * (pSrt->m_numFroxelsXInv * 16.0 / 9.0) * 2.0f - 1.0f;
	ndcY = -ndcY;
	// now check against all planes

	const float kFroxelHalfUvX = 0.5f / pSrt->m_numFroxelsXY.x;
	const float kFroxelHalfUvY = 0.5f / pSrt->m_numFroxelsXY.y;

	float awayDistX0 = saturate(-(dot(pSrt->m_leftPlaneNdc.xy, float2(ndcX, ndcY)) - pSrt->m_leftPlaneNdc.z - kFroxelHalfUvX) / kFroxelHalfUvX);
	float awayDistY0 = saturate(-(dot(pSrt->m_topPlaneNdc.xy, float2(ndcX, ndcY)) - pSrt->m_topPlaneNdc.z   - kFroxelHalfUvY) / kFroxelHalfUvY);
	float awayDistX1 = saturate(-(dot(pSrt->m_rightPlaneNdc.xy, float2(ndcX, ndcY)) - pSrt->m_rightPlaneNdc.z - kFroxelHalfUvX) / kFroxelHalfUvX);
	float awayDistY1 = saturate(-(dot(pSrt->m_btmPlaneNdc.xy, float2(ndcX, ndcY)) - pSrt->m_btmPlaneNdc.z   - kFroxelHalfUvY) / kFroxelHalfUvY);

	float awayDist = saturate((awayDistX0 + awayDistY0 + awayDistX1 + awayDistY1) /*   * (0.75 + 0.5 * ((pos.x + pos.y + iSlice) % 2))  */);

	if (awayDist > 0.5f)
	{
		pSrt->m_destVolumetricsScreenSpaceInfo[blockId].x = maxSlice + 1 + 128;
	}


#endif



}

#endif
