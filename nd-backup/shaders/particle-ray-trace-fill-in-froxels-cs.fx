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

#ifndef RUNTIME_LIGHT_IN_FILL_IN_FROXELS
	#define RUNTIME_LIGHT_IN_FILL_IN_FROXELS 0
#endif

#ifndef FILL_IN_FROXELS_LIGHT_POINT_LIGHT_ONLY
#define FILL_IN_FROXELS_LIGHT_POINT_LIGHT_ONLY 0
#endif

#if FILL_IN_FROXELS_LIGHT_POINT_LIGHT_ONLY
#define ALLOW_POINT_LIGHTS 1
#define ALLOW_SPOT_LIGHTS 0
#define VOLUMETRICS_SUPPORT_ORTHO_LIGHTS 0
#elif FILL_IN_FROXELS_SPOT_ONLY
#define ALLOW_POINT_LIGHTS 0
#define ALLOW_SPOT_LIGHTS 1
#define VOLUMETRICS_SUPPORT_ORTHO_LIGHTS 0
#elif FILL_IN_FROXELS_POINT_SPOT_ONLY
#define ALLOW_POINT_LIGHTS 1
#define ALLOW_SPOT_LIGHTS 1
#define VOLUMETRICS_SUPPORT_ORTHO_LIGHTS 0
#else
#define ALLOW_POINT_LIGHTS 1
#define ALLOW_SPOT_LIGHTS 1
// in case of dispatch indirect this is cathc all and by default we include ortho lights. otherwise we have special shader for non disaptch indirect version
#if FROXEL_DISPATCH_INDIRECT
#define VOLUMETRICS_SUPPORT_ORTHO_LIGHTS 1
#endif


#endif

//#define ALLOW_DETAILED_SPOT_LIGHTS 1
//#define SPOT_LIGHT_CAN_USE_DEPTH_VARIANCE_DARKENING 1



#ifndef ALLOW_SHADOWS
#define ALLOW_SHADOWS (!FROXEL_EARLY_PASS)
#else

#endif

#ifndef FILL_IN_FROXELS_LIGHTS_ONLY
#define FILL_IN_FROXELS_LIGHTS_ONLY 0
#endif

#ifndef FILL_IN_FROXELS_USE_DETAIL_NOISE
#define FILL_IN_FROXELS_USE_DETAIL_NOISE 1
#endif

#define DEBUG_OCCUPANCY 0

#define DEBUG_IN_FILL_IN 0

// this function assumes plane0 is inside of the rothographic light
float PlaneEvalSimple(float plane0, float rangeInv)
{
	plane0 = -plane0; // flip it so we start at 0 and grow into inside. at 2.0 we are at the different edge

	float awayFromCenter = abs(plane0 - 1.0f);
	float awayFromEdge = 1.0 - awayFromCenter;

	float blendVal = saturate(awayFromEdge  * rangeInv);

	return blendVal;
}

float PlaneEvalSimpleOneSide(float plane0, float start, float rangeInv, float fadeRangeInv)
{
	plane0 = -plane0; // flip it so we start at 0 and grow into inside. at 2.0 we are at the different edge

	
	float awayFromEdge = plane0;

	float awayFromStartEdge = 1.0 - awayFromEdge;

	float blendValStartDist = saturate(awayFromStartEdge * rangeInv);
	float belndValAway = saturate(awayFromEdge  * fadeRangeInv); // saturate(awayFromStartEdge / kStartPlaneDistFade);

	float blendVal = min(blendValStartDist, belndValAway);

	return blendVal;
}

float PlaneIntegration(float plane0Start, float plane0End, float range)
{
	plane0Start = -plane0Start;
	plane0End = -plane0End; // flip it so we start at 0 and grow into inside. at 2.0 we are at the different edge


	float kStartPlaneDistFade = range;

	// froxel start and end points sorted min to max
	float plane0Min = min(plane0Start, plane0End);
	float plane0Max = max(plane0Start, plane0End);

	float planeStartDistance = 0; // starting at 0 we fade

	float planeFullBlendInDist = planeStartDistance + kStartPlaneDistFade;
	float planeFullBlendInEnd = planeStartDistance + 2.0 - kStartPlaneDistFade;
	float planeFullBlendInRange = planeFullBlendInEnd - planeFullBlendInDist;

	float fullPlaneBlendInRangeStart = min(max(plane0Min - planeFullBlendInDist, 0), planeFullBlendInRange);
	float fullPlaneBlendInRangeEnd = min(max(plane0Max - planeFullBlendInDist, 0), planeFullBlendInRange);
	float fullPlaneBlendInAmount = (fullPlaneBlendInRangeEnd - fullPlaneBlendInRangeStart);

	float plane0RangeStart = min(max(plane0Min - planeStartDistance, 0), kStartPlaneDistFade);
	float plane0RangeEnd = min(max(plane0Max - planeStartDistance, 0), kStartPlaneDistFade);

	float plane1RangeStart = min(max(plane0Min - planeFullBlendInEnd, 0), kStartPlaneDistFade);
	float plane1RangeEnd = min(max(plane0Max - planeFullBlendInEnd, 0), kStartPlaneDistFade);

	float plane0AvgBlendInVal = 0;
	{
		float plane0RangeStartScaled = plane0RangeStart / kStartPlaneDistFade; //  converting range of overlap to 0..1 scale where full overlap is 0..1
		float plane0RangeEndScaled = plane0RangeEnd / kStartPlaneDistFade;

		float plane0IntegratedBlendIn = plane0RangeEndScaled * plane0RangeEndScaled * 0.5f - plane0RangeStartScaled * plane0RangeStartScaled * 0.5f;

		plane0AvgBlendInVal = plane0IntegratedBlendIn / max(0.00001, plane0RangeEndScaled - plane0RangeStartScaled);
	}

	float plane1AvgBlendInVal = 0;
	{
		float plane1RangeStartScaled = plane1RangeStart / kStartPlaneDistFade; //  converting range of overlap to 0..1 scale where full overlap is 0..1
		float plane1RangeEndScaled = plane1RangeEnd / kStartPlaneDistFade;

		// but these need to be fliiped around 1.0 since we are approaching the curve from the other side
		plane1RangeStartScaled = 1.0 - plane1RangeStartScaled;
		plane1RangeEndScaled = 1.0 - plane1RangeEndScaled;

		// swap
		float t = plane1RangeStartScaled;
		plane1RangeStartScaled = plane1RangeEndScaled;
		plane1RangeEndScaled = t;

		float plane1IntegratedBlendIn = plane1RangeEndScaled * plane1RangeEndScaled * 0.5f - plane1RangeStartScaled * plane1RangeStartScaled * 0.5f;

		plane1AvgBlendInVal = plane1IntegratedBlendIn / max(0.00001, plane1RangeEndScaled - plane1RangeStartScaled);
	}

	float plane0StartDistFade;

	plane0StartDistFade = (plane0AvgBlendInVal * (plane0RangeEnd - plane0RangeStart) + plane1AvgBlendInVal * (plane1RangeEnd - plane1RangeStart) + fullPlaneBlendInAmount) / abs(plane0Max - plane0Min);

	//plane0StartDistFade *= plane0StartDistFade;

	//float centerIntensity = saturate((lerp(startAlongDir, endAlongDir, 0.5f) - startDistance) / kStartPlaneDistFade);

	//centerIntensity *= centerIntensity;
	//float alognLightIntensityFactor = saturate(abs(alongDirMax - alongDirMin) / 0.01);

	//lightStartDistFade = lerp(centerIntensity, lightStartDistFade, alognLightIntensityFactor);

	//lightStartDistFade *= lightStartDistFade;

	return plane0StartDistFade;
}

// this shader supposrts just fill in and fill in with runtime lights
[NUM_THREADS(64, 1, 1)] //
void CS_VolumetricsFillInFroxels(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 _groupId : SV_GroupID,

#if FROXEL_SPLIT_DISPATCH
	VolumetricsFogSrtWrap srtWrap : S_SRT_DATA
#else
	VolumetricsFogSrt *pSrt : S_SRT_DATA
#endif
)
{
	//__s_setprio(3);

	uint3 groupId = _groupId;

#if FROXEL_SPLIT_DISPATCH
	VolumetricsFogSrt *pSrt = srtWrap.pSrt;
	groupId.x += srtWrap.offset;

	//if (groupId.x >= NdAtomicGetValue(pSrt->m_gdsOffset_5))
	{
		//	return;
	}
#endif
#if FROXEL_DISPATCH_INDIRECT

#if FROXEL_EARLY_PASS
#if FROXELS_NO_SUN_SHADOW_CHECK
	uint dispatchMask = pSrt->m_srcDispatchListEarly1[groupId.x].m_pos;
#elif RUNTIME_LIGHT_IN_FILL_IN_FROXELS
	uint dispatchMask = pSrt->m_srcDispatchListEarly0[groupId.x].m_pos;
#else
	uint dispatchMask = pSrt->m_srcDispatchListEarly2_NoLights[groupId.x].m_pos;
#endif
#else
#if FILL_IN_FROXELS_SPOT_ONLY
	#if ALLOW_SHADOWS
		uint dispatchMask = pSrt->m_srcDispatchList3[groupId.x].m_pos;
	#else
		uint dispatchMask = pSrt->m_srcDispatchList6[groupId.x].m_pos;
	#endif
#elif FILL_IN_FROXELS_LIGHT_POINT_LIGHT_ONLY
	#if ALLOW_SHADOWS
		uint dispatchMask = pSrt->m_srcDispatchList5[groupId.x].m_pos;
	#else
		uint dispatchMask = pSrt->m_srcDispatchList7_PointOnlyNoShadow[groupId.x].m_pos;
	#endif
#elif FILL_IN_FROXELS_POINT_SPOT_ONLY
	#if ALLOW_SHADOWS
		//uint dispatchMask = pSrt->m_srcDispatchList9_PointSpotWithShadow[groupId.x].m_pos;
	#else
		uint dispatchMask = pSrt->m_srcDispatchList8_PointSpotNoShadow[groupId.x].m_pos;
	#endif
#elif FROXELS_NO_SUN_SHADOW_CHECK
	uint dispatchMask = pSrt->m_srcDispatchList1[groupId.x].m_pos;
#elif RUNTIME_LIGHT_IN_FILL_IN_FROXELS
	uint dispatchMask = pSrt->m_srcDispatchList4[groupId.x].m_pos; // filtered leftover froxels (basicall uber shader for everything)
#elif FILL_IN_FROXELS_USE_LIGHT_LIST
	uint dispatchMask = pSrt->m_srcDispatchList0[groupId.x].m_pos; // unfiltered all rt-lit forxel list
#else
	uint dispatchMask = pSrt->m_srcDispatchList2[groupId.x].m_pos;
#endif
#endif

	uint groupZ = dispatchMask & 0x000003FF;
	uint groupY = (dispatchMask >> 10) & 0x000003FF;
	uint groupX = (dispatchMask >> 20) & 0x000003FF;
	uint needSliceCheck = (dispatchMask >> 30) & 0x00000001;

	uint2 groupPos = uint2(8, 8) * uint2(groupX, groupY);

	uint2 pos = groupPos + uint2(groupThreadId.x % 8, groupThreadId.x / 8);

	int iSlice = groupZ;

	groupId = uint3(groupX, groupY, groupZ);

#else
	uint needSliceCheck = 0;

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 pos = groupPos + uint2(groupThreadId.x % 8, groupThreadId.x / 8);

	int iSlice = groupId.z;
#endif

	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);

#if FROXEL_DISPATCH_INDIRECT
	//needSliceCheck = 1;
	int lastSliceToEverBeSampled = needSliceCheck ? min(63, int(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x) + 1) : 63;
	
#else
	int lastSliceToEverBeSampled = min(63, int(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x) + 1);
#endif

	// optimized
	//for (int iSlice = 0; iSlice <= lastSliceToEverBeSampled; ++iSlice)

	// all
	//for (int iSlice = 0; iSlice < numSlices; ++iSlice)

	// single

	// in early pass we don't have depth buffer info, and we just dispatch indirect predicted groups
#if !FROXEL_EARLY_PASS
	if (iSlice > lastSliceToEverBeSampled)
			return;
#endif

	if (pos.y >= pSrt->m_numFroxelsXY.y)
		return;
	if (pos.x >= pSrt->m_numFroxelsXY.x)
		return;

#if !FROXELS_DYNAMIC_RES
	uint tileFlatIndex = mul24((NumFroxelsXRounded + 7) / 8, groupId.y) + groupId.x;
	uint froxelFlatIndex = pSrt->m_numFroxelsXY.x * pos.y + pos.x;
#else
	uint numTilesInWidth = (uint(pSrt->m_numFroxelsXY.x) + 7) / 8;
	uint tileFlatIndex = mul24(numTilesInWidth, groupId.y) + groupId.x;
	uint froxelFlatIndex = mul24(pSrt->m_numFroxelsXY.x, pos.y) + pos.x;
#endif



	uint intersectionMask = pSrt->m_volumetricSSLightsTileBufferRO[tileFlatIndex].m_intersectionMask;

	int numFroxelGroupsX = (pSrt->m_numFroxelsXY.x + 7) / 8;
	int numFroxelGroupsY = (pSrt->m_numFroxelsXY.y + 7) / 8;

	#if FROXEL_DISPATCH_INDIRECT
	// this data could have been available in non indirect method, but right now is not
	intersectionMask = pSrt->m_volumetricSSLightsTileBufferRO[tileFlatIndex + mul24(pSrt->m_numFroxelsGroupsInSlice /* numFroxelGroupsX * numFroxelGroupsY */, uint(iSlice + 1))].m_intersectionMask;

	//#ifndef FROXEL_START_DISTANCE_SHADOW
	//#define FROXEL_START_DISTANCE_SHADOW 1
	//#endif

	#endif


	#if !FOG_TEXTURE_DENSITY_ONLY
	VolumetrciSSLightFroxelData lightFroxelData = pSrt->m_volumetricSSLightsFroxelBufferRO[froxelFlatIndex];
	#endif
	float4 debugResult = float4(0, 0, 0, 0);

	#if FILL_IN_FROXELS_LIGHTS_ONLY
	if (pos.x == 100 && pos.y == 100 && iSlice == 20)
	{
		//_SCE_BREAK();
	}
#endif

	bool debugOccupancy = false;
	#if DEBUG_OCCUPANCY
		debugOccupancy = (pSrt->m_frameNumber % 64) == iSlice;
	#endif

	{
		froxelCoord.z = iSlice;

		//float2 pixelPos = float2(FogFroxelGridSizeNativeRes, FogFroxelGridSizeNativeRes) * (float2(froxelCoord.x, froxelCoord.y) + float2(0.5, 0.5) + pSrt->m_froxelFogJitterXY.xy * posNegBasedOnSlice) / float2(SCREEN_NATIVE_RES_W_F, SCREEN_NATIVE_RES_H_F); // (float2(kTotalFroxelsX - froxelCoord.x, 0));

#if !FROXELS_DYNAMIC_RES
		uint2 pixelPosNotJit = uint2(FogFroxelGridSizeNativeRes, FogFroxelGridSizeNativeRes) * froxelCoord.xy;
#else
		uint2 pixelPosNotJit = uint2(pSrt->m_froxelSizeU, pSrt->m_froxelSizeU) * froxelCoord.xy;
#endif

#if !FROXELS_DYNAMIC_RES
		float2 pixelPosNormNoJit = float2(pos.x + 0.5, pos.y + 0.5) / float2(NumFroxelsXRounded_F, NumFroxelsYRounded_F);
#else
		float2 pixelPosNormNoJit = float2(pos.x + 0.5, pos.y + 0.5) * float2(pSrt->m_numFroxelsXInv, pSrt->m_numFroxelsXInv * 16.0 / 9.0);
#endif
		float posNegBasedOnSlice = (iSlice % 2 ? -1.0 : 1.0);
		float2 pixelPosNormJit = pixelPosNormNoJit + (pSrt->m_fogJitterXYUnorm.xy * posNegBasedOnSlice);
		float2 pixelPosNormJitOtherWay = pixelPosNormNoJit + (-pSrt->m_fogJitterXYUnorm.xy * posNegBasedOnSlice);

		float ndcXJit = pixelPosNormJit.x * 2.0f - 1.0f;
		float ndcYJit = pixelPosNormJit.y * 2.0f - 1.0f;
		ndcYJit = -ndcYJit;

		float ndcXNoJit = pixelPosNormNoJit.x * 2.0f - 1.0f;
		float ndcYNoJit = pixelPosNormNoJit.y * 2.0f - 1.0f;
		ndcYNoJit = -ndcYNoJit;


		int numSampleSteps = 1;
		float4 lightAccumValue = float4(0, 0, 0, 0);
		float lighIntensityAccum = 0;
		float4 noiseAndProbeColor = float4(0, 0, 0, 0);

		float destConv = 0.0;
		float destConvPureDist = 0.0;

		float sliceCoord0 = iSlice;
		float sliceCoord1 = iSlice + 1.0;

		// todo: we probably don't need two different constants since we keep the grid offset the same for runtime lights vs sun fog grid
		#if FILL_IN_FROXELS_LIGHTS_ONLY
			float viewZ0 = FroxelZSliceToCameraDistExpWithOffset(sliceCoord0, pSrt->m_runtimeGridOffset);
			float viewZ1 = FroxelZSliceToCameraDistExpWithOffset(sliceCoord1, pSrt->m_runtimeGridOffset);
			
		#else
			float viewZ0 = FroxelZSliceToCameraDistExp(sliceCoord0, pSrt->m_fogGridOffset);
			float viewZ1 = FroxelZSliceToCameraDistExp(sliceCoord1, pSrt->m_fogGridOffset);
		#endif

		//JitterVer1
		float zdist = viewZ1 - viewZ0;
		zdist *= pSrt->m_frameNumber % 2 ? 1.0 : -1.0;
		zdist /= 10.0f;

		//viewZ0 += zdist;
		//viewZ1 += zdist;

		float3 posVs0 = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * viewZ0;
		float3 posVs1 = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * viewZ1;

		// if we have temporal enabled on lights

		//JitterVer0
		//posVs0 = float3(((pixelPosNormJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * viewZ0;
		//posVs1 = float3(((pixelPosNormJitOtherWay)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * viewZ1;


		float viewDist0 = length(posVs0);
		float viewDist1 = length(posVs1);


		float fogHeightStart = pSrt->m_fogHeightStart;

		float totalColorIntensity = 0;

		for (int iSampleStep = 0; iSampleStep < numSampleSteps; ++iSampleStep)
		{
			float sliceCoord = iSlice + 0.5 / numSampleSteps + 1.0 / numSampleSteps * iSampleStep;
			float sliceCoordNoJit = sliceCoord;

			// jitter only makes sense if we do one sample step
			// jitter is supposed to result in same effect as multiple sample steps
			if (numSampleSteps == 1)
			{
//#if RUNTIME_LIGHT_IN_FILL_IN_FROXELS
				sliceCoord = sliceCoord + pSrt->m_ndcFogJitterZ;// *(iSlice % 2 ? -1.0 : 1.0);
//#endif
			}

			#if FILL_IN_FROXELS_LIGHTS_ONLY
				float viewZJit = FroxelZSliceToCameraDistExpWithOffset(sliceCoord, pSrt->m_runtimeGridOffset);
				float viewZNoJit = FroxelZSliceToCameraDistExpWithOffset(sliceCoordNoJit, pSrt->m_runtimeGridOffset);
			#else
				float viewZJit = FroxelZSliceToCameraDistExp(sliceCoord, pSrt->m_fogGridOffset);
				float viewZNoJit = FroxelZSliceToCameraDistExp(sliceCoordNoJit, pSrt->m_fogGridOffset);
			#endif

			float ndcZJit = GetNdcDepth(viewZJit, pSrt->m_depthParams);
			float ndcZNoJit = GetNdcDepth(viewZNoJit, pSrt->m_depthParams);

			float4 posWsH = mul(float4(ndcXJit, ndcYJit, ndcZJit, 1), pSrt->m_mVPInv);
			float3 posWsJit = posWsH.xyz / posWsH.w;


			float4 prevPosHs_Jit0 = mul(float4(posWsJit, 1), pSrt->m_mLastFrameVP);
			float3 prevPosNdc_Jit0 = prevPosHs_Jit0.xyz / prevPosHs_Jit0.w;

			float3 prevPosUvw_Jit0;
			prevPosUvw_Jit0.xy = prevPosNdc_Jit0.xy * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);
			prevPosUvw_Jit0.y = 1.0f - prevPosUvw_Jit0.y;

			float  prevLinDepth_Jit0 = GetLinearDepth(prevPosNdc_Jit0.z, pSrt->m_lastFrameDepthParams);
			prevPosUvw_Jit0.z = CameraLinearDepthToFroxelZCoordExp(prevLinDepth_Jit0, 0);


			float3 posWsNoJit;
			//posWsH = mul(float4(ndcXNoJit, ndcYNoJit, ndcZNoJit, 1), pSrt->m_mVPInv);
			//posWsNoJit = posWsH.xyz / posWsH.w;

			float4 posNoJitWsH = mul(float4(ndcXNoJit * viewZNoJit, ndcYNoJit * viewZNoJit, ndcZNoJit * viewZNoJit, viewZNoJit), pSrt->m_mAltVPInv);
			float distToFroxel = length(posNoJitWsH.xyz); // we rely on the fact that altWorldPos is exactly camera position
			posWsNoJit = posNoJitWsH.xyz + pSrt->m_altWorldPos; // / posWsH.w;



			float4 prevPosHs_NoJit = mul(float4(posWsNoJit, 1), pSrt->m_mLastFrameVP);
			float3 prevPosNdc_NoJit = prevPosHs_NoJit.xyz / prevPosHs_NoJit.w;

			float3 prevPosUvw_NoJit;
			prevPosUvw_NoJit.xy = prevPosNdc_NoJit.xy * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);
			prevPosUvw_NoJit.y = 1.0f - prevPosUvw_NoJit.y;

			float  prevLinDepth_NoJit = GetLinearDepth(prevPosNdc_NoJit.z, pSrt->m_lastFrameDepthParams);
			prevPosUvw_NoJit.z = CameraLinearDepthToFroxelZCoordExp(prevLinDepth_NoJit, 0);



			float fogRTLightStartDistFactor = clamp((distToFroxel - pSrt->m_rtLightFogStartDistance + 4.0) / 4.0, 0.0f, 1.0f);
			float fogIntensityStartDistFactor = clamp((distToFroxel - pSrt->m_intensityFogStartDistance + 4.0) / 4.0, 0.0f, 1.0f);

			if (pos.x == (192 / 2) && pos.y == (108 / 2) && iSlice == 6)
			{
				//_SCE_BREAK();
			}

#if RUNTIME_LIGHT_IN_FILL_IN_FROXELS && !FROXELS_NO_SUN_SHADOW_CHECK
			//if (intersectionMask)

			//todo: add this check if we combine fill in and light
			#if !FILL_IN_FROXELS_LIGHTS_ONLY
				// when we only do lights, but not only lights, meaning we also do fill in the same shader
				//if (INDEX_OUTSIDE_PROPRTIES_SLICES(pos.z))
			#endif

			float2 depthVarInfo = pSrt->m_srcVolumetricsDepthInfo.SampleLevel(pSrt->m_linearSampler, float3(pixelPosNormNoJit, 3.0), 0).xy; // 1.0 not blurred, 3.0 blurred

			float depthMean = depthVarInfo.x;
			float depthVariance = depthVarInfo.y - depthMean * depthMean;

			{





#if !FROXELS_DYNAMIC_RES
				uint divergingTileIndex = uint(pixelPosNotJit.x) / TILE_SIZE + mul24(uint(pixelPosNotJit.y) / TILE_SIZE, uint(SCREEN_NATIVE_RES_W_F) / TILE_SIZE);
#else
				uint divergingTileIndex = uint(pixelPosNotJit.x) / TILE_SIZE + mul24(uint(pixelPosNotJit.y) / TILE_SIZE, uint(pSrt->m_screenWU) / TILE_SIZE);
#endif

#if !FROXEL_RTL_V_1
				uint tileIndex = divergingTileIndex;

				uint numLights = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_numLightsPerTileBufferRO[tileIndex];

				//if (numLights)
				//	lightAccumValue.rgb += float3(1, 1, 1);

				for (uint i = 0; i < numLights; ++i)
				{
					uint lightIndex = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lightsPerTileBufferRO[tileIndex*MAX_LIGHTS_PER_TILE + i];
#else
				uint numLights = __s_bcnt1_i32_b32(intersectionMask);
				uint froxelLightIndex = 0;
				while (intersectionMask)
				{
					int lightIndex = __s_ff1_i32_b32(intersectionMask);

					intersectionMask = __s_andn2_b32(intersectionMask, __s_lshl_b32(1, lightIndex));
					froxelLightIndex = lightIndex;
					
					#if FOG_TEXTURE_DENSITY_ONLY
					VolumetrciSSLightFroxelData tileLightFroxelData = pSrt->m_volumetricSSLightsFroxelBufferRO[mul24(tileFlatIndex, pSrt->m_numVolLights) + froxelLightIndex];

					float4 lightData_m_data0 = tileLightFroxelData.m_data0[groupThreadId.x];
					float4 lightData_m_data1 = tileLightFroxelData.m_data1[groupThreadId.x];
					#else
					VolumetrciSSLight singleLightData = lightFroxelData.m_lights[froxelLightIndex];
					float4 lightData_m_data0 = singleLightData.m_data0;
					float4 lightData_m_data1 = singleLightData.m_data1;
					#endif
					//VolumetrciSSLight singleLightData = lightFroxelData.m_lights[froxelLightIndex];
					froxelLightIndex += 1;


#if VOLUMETRICS_COMPRESS_SS_LIGHTS
					float4 m_data0 = float4(f16tof32(asuint(lightData_m_data0.x)), f16tof32(asuint(lightData_m_data0.x) >> 16), f16tof32(asuint(lightData_m_data0.y)), f16tof32(asuint(lightData_m_data0.y) >> 16));
					float4 m_data1 = float4(f16tof32(asuint(lightData_m_data0.z)), f16tof32(asuint(lightData_m_data0.z) >> 16), f16tof32(asuint(lightData_m_data0.w)), f16tof32(asuint(lightData_m_data0.w) >> 16));
					float4 m_data2 = float4(f16tof32(asuint(lightData_m_data1.x)), f16tof32(asuint(lightData_m_data1.x) >> 16), f16tof32(asuint(lightData_m_data1.y)), f16tof32(asuint(lightData_m_data1.y) >> 16));
					float4 m_data3 = float4(f16tof32(asuint(lightData_m_data1.z)), f16tof32(asuint(lightData_m_data1.z) >> 16), f16tof32(asuint(lightData_m_data1.w)), f16tof32(asuint(lightData_m_data1.w) >> 16));
#else
					float4 m_data0 = singleLightData.m_data0;
					float4 m_data1 = singleLightData.m_data1;
					float4 m_data2 = singleLightData.m_data2;
					float4 m_data3 = singleLightData.m_data3;
#endif

					float2 intersectionTimes = m_data0.xy;
					float maxIntensityOnSegment = m_data0.z;
					float finalAngleCoord = m_data0.w;// +posNegBasedOnSlice * 1 / 256.0 / 4.0;

					float2 startUv = m_data1.xy;
					float2 uvChange = m_data1.zw;
					//float2 finalUv = m_data2.xy; // used in debug only
					//float2 finalUv = m_data2.xy;
					//float finalDistance = m_data2.z;
					//float finalDistanceInTime = m_data2.w;

					//float finalRoundedAngleCoord = m_data2.y;
					//float2 finalSlerpFactors = m_data2.zw;
					//float2 finalSlerpFactors = float2(0, 0);
					//float minIntensityPossible = m_data2.z;
					//float goodDirFactor = m_data2.x;
					float finalSliceCoord = m_data2.x; // used
					float avgValueFromPreprocessing = m_data2.y;


					//float4 debugVector = m_data2.xyzw;

					float2 finalMinUv = m_data3.xy;
					float2 finalMaxUv = m_data3.zw;
#endif

					const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[lightIndex];

					NewVolumetricsLightDesc newLightDesc = pSrt->m_newVolLightDescRO[lightIndex];


					uint lightType = lightDesc.m_typeData & LIGHT_TYPE_BITS;
					uint isOrtho = lightDesc.m_typeData & LIGHT_IS_ORTHO_BITS;
					uint isOrthoProjector = (lightType == kProjectorLight) && isOrtho;

					const float3 lightPosWs = lightDesc.m_worldPos;
					const float3 lightPosVs = lightDesc.m_viewPos;

					//GetRuntimeLightAccumulation(dummyDebug, lightDesc, setup, scatteredLight, specularLight, refractedLight, false, passId == kPassCheapForward);


					float converganceRate = 0.0; // properly converging
					float converganceRatePureDist = 0.0;

					int numShadowSteps = 1;
					int iShadowStep = 0;
					float inShadow = 0.0;
					float accumProbabilityInFrontOfShadowGeo = 0;
					float varAccum = 0;
					float shadowToDarknessConversion = 0;
					float3 lightColor = lightDesc.m_color.xyz / 1;
					//lightColor = float3(lightIndex == 0, lightIndex == 1, lightIndex == 2);

					float radius = lightDesc.m_radius;

					float len = max(length(lightPosWs - posWsJit), 0.001);
					float brightness = length(lightPosWs - posWsJit) < radius;


					brightness = saturate(1.0 - len / radius);

#define TEXTURE_FACTOR 4.0
#define CIRCLE_TEXTURE_FACTOR (1.0/2.0)
					float lightIntensityChange = abs(lightDesc.m_shadowBlurScale);

					brightness = brightness * brightness * brightness;
#if FROXEL_RTL_V_1
					float startInVolume = max(viewDist0, intersectionTimes.x);
					float endInVolume = min(viewDist1, intersectionTimes.y);
					float lengthInVolume = clamp(endInVolume - startInVolume, 0, viewDist1 - viewDist0);
					float integratedDensity = lengthInVolume;
					float avgDensity = integratedDensity / (viewDist1 - viewDist0);

					if ((lengthInVolume > 0.0) || debugOccupancy)
					{
						// % of froxel for start of light volume and end of light volume. if froxel is fully in light volume this is 0, 1
						float kStartFactor = (startInVolume - viewDist0) / (viewDist1 - viewDist0);
						float kEndFactor = (endInVolume - viewDist0) / (viewDist1 - viewDist0);

						#if VOLUMETRICS_DEPTH_VARIANCE_CHECKS
						// let's check how much of the light volume in this froxel is behind geometry
						// to do that we will look at depth variance

						// controls of when we consider something behind geometry
						const float kLightVarianceBehindGeoStart = pSrt->m_rtBehingGeoVarianceStart;// 0.1; // at 1.0 variance behind geo we are behind 84 % of depth samples
						const float kLightVarianceBehindGeoEnd = pSrt->m_rtBehingGeoVarianceRange;// 0.01;
						const float kLightVarianceMinRange = 0.05; // at least this much of meters for the blend range
						float depthCheckOffset = 0;// meanFroxelLen  * (0.0 + 1.0 * (int(froxelCoord.x + froxelCoord.y) % 2));

						float lightBehindGeoDepthStart = depthCheckOffset + depthMean + kLightVarianceBehindGeoStart * depthVariance - kLightVarianceMinRange;
						float lightBehindGeoDepthRange = max(0.01, (kLightVarianceBehindGeoEnd - kLightVarianceBehindGeoStart) * depthVariance + kLightVarianceMinRange); // 

						// viewZ start and end within froxel that is inside fo light volume. this is Z, not view distance.
						float viewZStart = lerp(viewZ0, viewZ1, kStartFactor);
						float viewZEnd = lerp(viewZ0, viewZ1, kEndFactor);

						// amount of light volume in behind geo blend range
						float behindGeoStart = min(max(viewZStart, lightBehindGeoDepthStart), viewZEnd);
						float behindGeoEnd = max(min(viewZEnd, lightBehindGeoDepthStart + lightBehindGeoDepthRange), lightBehindGeoDepthStart);

						float inFrontOfGeo = behindGeoStart - viewZStart; // this chunk is completely in front
						float behindGeo = viewZEnd - behindGeoEnd; // this chunk is completely behind geo

						float behindGeo0 = saturate((behindGeoStart - lightBehindGeoDepthStart) / lightBehindGeoDepthRange);
						float behindGeo1 = saturate((behindGeoEnd - lightBehindGeoDepthStart) / lightBehindGeoDepthRange);

						float inFrontWeight = (inFrontOfGeo) / (viewZEnd - viewZStart);
						float behindWeight = (behindGeo) / (viewZEnd - viewZStart);
						float rangeWeight = 1 - inFrontWeight - behindWeight;

						float behindGeoAcum = 0;
						if (viewZEnd <= lightBehindGeoDepthStart)
						{
							// the light voilume ends before we enter geo blend range
							behindGeoAcum = 0;
							//startInFrontOfGeoZ, endInFrontOfGeoZ stay the same the whole froxel is in front of geometry
						}
						else if (viewZStart >= lightBehindGeoDepthStart + lightBehindGeoDepthRange)
						{
							//startInFrontOfGeoZ, endInFrontOfGeoZ stay the same the whole froxel is behind of geometry. TODO: do we need to somehow cancel the shadow checks alltogether
							// the light part of this froxel is behind geomtry belnd range
							behindGeoAcum = 1;
						}
						else
						{
							behindGeoAcum = lerp(behindGeo0, behindGeo1, 0.5) * rangeWeight + 1 * behindWeight;
							////startInFrontOfGeoZ = behindGeoStart;// max(behindGeoStart, startInFrontOfGeoZ);
							
							//endInFrontOfGeoZ = behindGeoEnd; // min(behindGeoEnd, endInFrontOfGeoZ);
						}

						float depthVarianceFactor = (1 - behindGeoAcum);
						#else
						float depthVarianceFactor = 1;
						#endif
#else
					{
#endif
						#if ALLOW_POINT_LIGHTS
						{
						#if !FILL_IN_FROXELS_LIGHT_POINT_LIGHT_ONLY
						// no need to check type if point lights only
						if (lightType == kPointLight)
						#endif
						{
							float cameraMotionScale = saturate(pSrt->m_cameraMotion / 3.0);
							float minConverganceRate = lerp(0.6, 0.2, cameraMotionScale); // as camera moves, we don't allow to go too fast

							converganceRatePureDist = 0.2f * saturate(saturate(1.0 - len / (radius / 2.0))) * cameraMotionScale * saturate(lightIntensityChange / 0.3);

							converganceRate = minConverganceRate * saturate(saturate(1.0 - len / (radius / 3))) * saturate(lightIntensityChange / 0.3); // areas closer to light source will store convergance value higher = less temporal. we scale this value and get to max convergance rate at 0.3 intensity change
#if FROXEL_RTL_V_1

							float2 uvTotal0 = startUv + (viewDist0 - intersectionTimes.x) * uvChange;
							float2 uvTotal1 = startUv + (viewDist1 - intersectionTimes.x) * uvChange;

							//uvTotal0.y = 1.0 - uvTotal0.y;
							//uvTotal1.y = 1.0 - uvTotal1.y;


							float2 uv0 = uvTotal0;
							float2 uv1 = uvTotal1;

							uv0 = saturate(uv0);
							uv1 = saturate(uv1);



							float accum1 = pSrt->m_circleTextureRO.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), (uv1), 0).x;
							float accum0 = pSrt->m_circleTextureRO.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), (uv0), 0).x;

							float integral = max(abs(accum1 * accum1 - accum0 * accum0) / CIRCLE_TEXTURE_FACTOR, 0.0) * length(uv1 - uv0) / length(uvTotal1 - uvTotal0);

							float intensityFromIntegral = integral / length(uvTotal1 - uvTotal0);

							//intensityFromIntegral = 1;

							float intensityMultiplier = 1.0 + maxIntensityOnSegment;


							brightness = avgDensity * intensityMultiplier;
							
							//brightness = intersectionTimes.y > intersectionTimes.x;
							
							brightness = intensityFromIntegral * intensityMultiplier;

							// we have a special multiplier for when we are looking away from point light
							// first, check how much away are we looking
							// we isolate to not blow up the number of registers needed
							[isolate]
							{
								float3 lightToFroxel = normalize(posVs1 - lightDesc.m_viewPos);
								float3 camToFroxelNorm = posVs1 / viewDist1;
								float sameDirDot = dot(lightToFroxel, camToFroxelNorm);

								float backIntensityMultiplier = lerp(1, newLightDesc.m_backIntensityMultiplier, saturate(sameDirDot));

								brightness *= backIntensityMultiplier;
							}

#if DEBUG_IN_FILL_IN
							if (pSrt->m_debugControls.z > 0)
							{
								lightColor += float3(10, 1, 1) * 1;
							}
#endif

							//lightColor = float3(uv0, 0) * 1;
#endif
							if (debugOccupancy)
							{
								lightColor = float3(1, 0, 0);
								brightness = 1.0f;
							}



						} // if light type is point light
						}
						#endif // #if ALLOW_POINT_LIGHTS

						float fadeout = 1.0;


						// todo: there a reother settings like start and end range
						// as well as gobo projector light logic
						// right now we just ahve spot lights

						float3 lightToPosWs = -posWsJit + lightPosWs;
						float3 lightRayWS = lightToPosWs / len;

						const float3 lightDirWs = lightDesc.m_worldDir;
						#if FROXEL_START_DISTANCE_SHADOW 
						float lightStartDistFade = 1.0f;
						#endif
						// spot lights add fadeout
						#if ALLOW_SPOT_LIGHTS
						{
						#if !FILL_IN_FROXELS_SPOT_ONLY
						// no need to check if only type allowed
						if (lightType == kSpotLight || lightType == kProjectorLight)
						#endif
						{

							#if !FROXEL_RTL_V_1
							fadeout = saturate(dot(lightRayWS, lightDirWs)*lightDesc.m_fadeoutParams.y + lightDesc.m_fadeoutParams.x) / 16.0;

							float lightDist = dot(lightToPosWs, -lightDirWs);
							if (lightDesc.m_startRange > 0.0f)
							{
								fadeout *= saturate((lightDist - lightDesc.m_startDistance) / lightDesc.m_startRange);
							}
							else
							{
								fadeout *= lightDist > lightDesc.m_startDistance ? 1.0f : 0.0f;
							}
							#endif


							#if FROXEL_RTL_V_1

							#if FROXEL_START_DISTANCE_SHADOW || FROXEL_DISPATCH_INDIRECT
							float kStartDistFade = max(pSrt->m_minStartDistanceRange, newLightDesc.m_startDistanceRange);
							#else
							float kStartDistFade = newLightDesc.m_startDistanceRange;
							#endif
							float startAlongDir = dot(posVs0 - lightPosVs, lightDesc.m_viewDir);
							float endAlongDir = dot(posVs1 - lightPosVs, lightDesc.m_viewDir);
							
							float alongDirMin = min(startAlongDir, endAlongDir);
							float alongDirMax = max(startAlongDir, endAlongDir);
							float alongDirAvg = alongDirMin * 0.5 + alongDirMax * 0.5;
							float linearFalloff = 1.0 - saturate(alongDirAvg / 5);

							#if FROXEL_START_DISTANCE_SHADOW || FROXEL_DISPATCH_INDIRECT
							float startDistance = max(0, newLightDesc.m_startDistance - max(0, (pSrt->m_minStartDistanceRange - newLightDesc.m_startDistanceRange) / 2));
							#else
							float startDistance = newLightDesc.m_startDistance;
							#endif

							float lightFullBlendInDist = startDistance + kStartDistFade;

							float fullBlendInRangeStart = max(alongDirMin - lightFullBlendInDist, 0);
							float fullBlendInRangeEnd = max(alongDirMax - lightFullBlendInDist, 0);



							float rangeStart = min(max(alongDirMin - startDistance, 0), kStartDistFade);
							float rangeEnd = min(max(alongDirMax - startDistance, 0), kStartDistFade);
							
							float rangeStartScaled = rangeStart / kStartDistFade;
							float rangeEndScaled = rangeEnd / kStartDistFade;

							float integratedBlendIn = rangeEndScaled * rangeEndScaled * 0.5f - rangeStartScaled * rangeStartScaled * 0.5f;

							float avgBlendInVal = integratedBlendIn / max(0.00001, rangeEndScaled - rangeStartScaled);
												


							#if !FROXEL_START_DISTANCE_SHADOW
							float lightStartDistFade;
							#endif
							lightStartDistFade = saturate((avgBlendInVal * (rangeEnd - rangeStart) + (fullBlendInRangeEnd - fullBlendInRangeStart)) / max(0.00001, abs(alongDirMax - alongDirMin)));// (integratedBlendIn + (fullBlendInRangeEnd - fullBlendInRangeStart)) / abs(startAlongDir - endAlongDir);

							float centerIntensity = saturate((lerp(startAlongDir, endAlongDir, 0.5f) - startDistance) / kStartDistFade);

							//centerIntensity *= centerIntensity;
							float alognLightIntensityFactor = saturate( abs(alongDirMax - alongDirMin) / 0.01 );

							lightStartDistFade = lerp(centerIntensity, lightStartDistFade, alognLightIntensityFactor);

							lightStartDistFade *= lightStartDistFade;

							//lightStartDistFade = 1;

							float2 uvFroxelLineStart = startUv + (0 - intersectionTimes.x) * uvChange;

							float2 uvTotal0 = startUv + (viewDist0 - intersectionTimes.x) * uvChange;
							float2 uvTotal1 = startUv + (viewDist1 - intersectionTimes.x) * uvChange;

							float2 uv0 = startUv + (startInVolume - intersectionTimes.x) * uvChange;
							float2 uv1 = startUv + (endInVolume - intersectionTimes.x) * uvChange;

							float uvOrig0 = uv0;
							float uvOrig1 = uv1;


							uv0 = uvTotal0;
							uv1 = uvTotal1;
							uv0 = clamp(uv0, min(finalMinUv, finalMaxUv), max(finalMinUv, finalMaxUv));
							uv1 = clamp(uv1, min(finalMinUv, finalMaxUv), max(finalMinUv, finalMaxUv));


							float noise = 0; // posNegBasedOnSlice / 2.0 * 1 / 257.0 / 2.0;

							// helps with bad patterning
							//finalAngleCoord = fmod(finalAngleCoord + 0.5f, 1.0);

							int iFinalSliceCoord = int(floor(finalSliceCoord));
							//finalSliceCoord
							Texture3D<float> integralTexture = pSrt->m_triangleTextureRO;
							float accum1 = 0;
							float accum0 = 0;
							//accum1 = integralTexture.SampleLevel(SetSampleModeClampToBorder(pSrt->m_linearSampler), float3((uv1), finalAngleCoord + noise /*0.28*/), 0).x;
							//accum0 = integralTexture.SampleLevel(SetSampleModeClampToBorder(pSrt->m_linearSampler), float3((uv0), finalAngleCoord - noise /*0.28*/), 0).x;


							//float accum10 = 0;
							//float accum11 = 0;
							//float accum00 = 0;
							//float accum01 = 0;

							ulong exec = __s_read_exec();
							do {
								// We first pick the first active lane index
								uint first_bit = __s_ff1_i32_b64(exec);
								// We know take the value inside this lane
								int uniform_SliceCoord = __v_readlane_b32(iFinalSliceCoord, first_bit);

								// We create a mask which is 1 for all lanes that contain this value
								ulong lane_mask = __v_cmp_eq_u32(iFinalSliceCoord, uniform_SliceCoord);
								// We use the predicate trick to override the exec mask and create a
								// execution block such as exec == lane_mask
								if (__v_cndmask_b32(0, 1, lane_mask)) {

									integralTexture = pSrt->m_triangleTexturesRO[uniform_SliceCoord];
									Texture3D<float> integralTexture1 = pSrt->m_triangleTexturesRO[min(uniform_SliceCoord + 1, VOLUMETRICS_MAX_CONE_SLICES-1)];

									float alternateAngleCoord = fmod(finalAngleCoord + 0.5, 1.0);

									// swap location to use better precision
									// todo: need to figure out if this test is always correct.
									//finalAngleCoord = uv1.x > 0.5 ? alternateAngleCoord : finalAngleCoord;

									//finalAngleCoord = uv1.x > 0.5 ? alternateAngleCoord : finalAngleCoord;

									//finalAngleCoord = (uv1.x > 0.5 && finalAngleCoord <= 0.25) ? alternateAngleCoord : finalAngleCoord;
									//finalAngleCoord = (uv1.x < 0.5 && finalAngleCoord >= 0.75) ? alternateAngleCoord : finalAngleCoord;


									finalAngleCoord = (uv1.x > 0.5 && finalAngleCoord <= 0.25) ? (0.5 + finalAngleCoord) : finalAngleCoord;
									finalAngleCoord = (uv1.x < 0.5 && finalAngleCoord >= 0.75) ? (finalAngleCoord - 0.5) : finalAngleCoord;


									//finalAngleCoord = alternateAngleCoord;

									float accum1Slice0 = integralTexture.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), float3((uv1), finalAngleCoord + noise /*0.28*/), 0).x;
									float accum0Slice0 = integralTexture.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), float3((uv0), finalAngleCoord - noise /*0.28*/), 0).x;

									float accum1Slice1 = integralTexture1.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), float3((uv1), finalAngleCoord + noise /*0.28*/), 0).x;
									float accum0Slice1 = integralTexture1.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), float3((uv0), finalAngleCoord - noise /*0.28*/), 0).x;

									//accum1Slice1 = accum1Slice0;
									//accum0Slice1 = accum0Slice0;

									// technically we should square here, but htis is how interpolation will work if we switch to mips
									accum1 = lerp(accum1Slice0, accum1Slice1, frac(finalSliceCoord));
									accum0 = lerp(accum0Slice0, accum0Slice1, frac(finalSliceCoord));


									//accum10 = integralTexture.SampleLevel((pSrt->m_linearSampler), float3((uv1), finalRoundedAngleCoord), 0).x;
									//accum11 = integralTexture.SampleLevel((pSrt->m_linearSampler), float3((uv1), finalRoundedAngleCoord + 1.0 / 257.0), 0).x;
									//accum00 = integralTexture.SampleLevel((pSrt->m_linearSampler), float3((uv0), finalRoundedAngleCoord /*0.28*/), 0).x;
									//accum01 = integralTexture.SampleLevel((pSrt->m_linearSampler), float3((uv0), finalRoundedAngleCoord + 1.0 / 257.0/*0.28*/), 0).x;

								}
								// Since we are finished with this lane, we update the execution mask
								exec &= ~lane_mask;
								// When all lanes are processed, exec will be zero and we can exit
							} while (exec != 0);




							//accum0 = finalSlerpFactors.x * accum00 + finalSlerpFactors.y * accum01;
							//accum1 = finalSlerpFactors.x * accum10 + finalSlerpFactors.y * accum11;

							//float accumDif = (accum10 - accum00) * finalSlerpFactors.x + (accum11 - accum01) * finalSlerpFactors.y;

							//accum0 = saturate(accum0 - pSrt->m_debugControls2.x * 0.01);
							//accum1 = saturate(accum1 - pSrt->m_debugControls2.x * 0.01);

							float integral = max(abs(accum1 * accum1 - accum0 * accum0) / TEXTURE_FACTOR, length(uvOrig1 - uvOrig0) * 0.0) * length(uv1 - uv0) / length(uvTotal1 - uvTotal0);

							//float integral = max(accumDif, lengthInVolume * 0.0);

							// if value stores distance

							float avgDist = integral / length(uvTotal1 - uvTotal0);
							float avgFalloff = 1.0 * length(uv1 - uv0) / length(uvTotal1 - uvTotal0) - avgDist;
							//float avgIntensity = lengthInVolume > 0.0 ? avgFalloff : 0.0f;

							float coneEdgeStart = pSrt->m_coreIntensityThreshold;

							float coneEdge = saturate(maxIntensityOnSegment / coneEdgeStart);

							//coneEdge *= coneEdge;
							#if DEBUG_IN_FILL_IN
							if (pSrt->m_debugControls.w > 0)
								coneEdge = 1;
							#endif
							//coneEdge = fadeout;
							// this is equation when we store falloff integral on its own: has more quantization issues
							//float avgIntensity =  lengthInVolume > 0.0 ? integral / length(uvTotal1 - uvTotal0) : 0.0f;
							//avgIntensity = viewDist1 > intersectionTimes.x && uvTotal1.x < 1.0 && uvTotal1.y < 1.0 ? integral / length(uvTotal1 - uvTotal0) : 0.0f;


							// this is equation when we store falloff as 1-faloffintegral
							//float avgIntensity =  /* maxIntensityOnSegment * maxIntensityOnSegment * */ (1.0 * length(uv1 - uv0) - integral) / length(uvTotal1 - uvTotal0);
							// when doing uv total
							// works a little better - not sure why. 
							float intensityFromIntegral = integral / length(uvTotal1 - uvTotal0);

							//intensityFromIntegral = avgValueFromPreprocessing * length(uv1 - uv0) / length(uvTotal1 - uvTotal0);

							//intensityFromIntegral = avgValueFromPreprocessing * lengthInVolume / (viewDist1 - viewDist0);



							float intensityFromSlice = avgDensity * coneEdge;
							float avg = max(intensityFromIntegral, intensityFromSlice);

							//coneEdge = 1.0; // disable modificatiions based on hackt plane deviation

							if (intensityFromIntegral > pSrt->m_coreIntensityThreshold)
							{
								intensityFromIntegral = pSrt->m_coreIntensityThreshold + pow(intensityFromIntegral - pSrt->m_coreIntensityThreshold, pSrt->m_coreIntensityPower > 0 ? pSrt->m_coreIntensityPower : 1.0);
							}

							coneEdge = 1.0 + maxIntensityOnSegment;

							float avgIntensity =  /* maxIntensityOnSegment * maxIntensityOnSegment * */
												  //lengthInVolume > 0 && viewDist0 < intersectionTimes.y ? coneEdge * lerp(avgDensity, integral / length(uvTotal1 - uvTotal0), goodDirFactor) : 0.0	;
								((lengthInVolume > 0) /* && ||  (viewDist0 < intersectionTimes.y)*/) ? (coneEdge * intensityFromIntegral) : 0.0;
							//(coneEdge * intensityFromIntegral);
							//lengthInVolume > 0  && viewDist0 < intersectionTimes.y ? avg : 0.0;

							#if DEBUG_IN_FILL_IN
							if (pSrt->m_debugControls.w > 0)
								avgIntensity = coneEdge * intensityFromIntegral;

							// fallback to density when looking straight along light source
							if (pSrt->m_debugControls.z > 0)
							{
								avgIntensity = avgDensity * coneEdge;

								lightColor += float3(1, 1, 1) * 10;
							}
							#endif

							//
							//float avgIntensity =  /* maxIntensityOnSegment * maxIntensityOnSegment * */ coneEdge * integral / length(uvTotal1 - uvTotal0);

							// distance based approach
							//float avgIntensity = 1; //  lengthInVolume > 0.0 ? avgFalloff : 0.0;
							brightness = avgIntensity;

							#if VOLUMETRICS_SUPPORT_ORTHO_LIGHTS
							if (isOrthoProjector)
							{
								lightStartDistFade = 1;
								float kRangeInv = 1.0 / 0.5f;

								// using same memory to store rnage since we dont care about start distance
								kRangeInv = newLightDesc.m_startDistance; //  lightDesc.m_fadeoutRange;

								#if 1 // multisample
								int kNumShadowSteps = 4; // ReadFirstLane(kNumShadowSteps);
								float kHalfStep = 1.0 / (2 * kNumShadowSteps);
								float kStep = 1.0 / kNumShadowSteps;
								float blendSum = 0;

								//newLightDesc.m_startDistance
								//newLightDesc.m_startDistanceRange

								//if (pSrt->m_debugControls2.w > 0 && kNumShadowSteps > pSrt->m_debugControls2.w)
								//{
								//	lightColor.r = 1.0;
								//}

								for (int iStep = 0; iStep < kNumShadowSteps; ++iStep)
								{
									float kFactor = kHalfStep + kStep * iStep; // +kHalfStep * (((pos.x + pos.y + iStep + pSrt->m_frameNumber) % 2) ? 1.0 : -1.0);

									// we do stepping only within the volume inside of light source
									// this is more efficient because we don't do steps that are outside of light source but inside of froxel
									//float kStartFactor = (startInVolume - viewDist0) / (viewDist1 - viewDist0);
									//float kEndFactor = (endInVolume - viewDist0) / (viewDist1 - viewDist0);

									float kLerpFactor = lerp(kStartFactor, kEndFactor, kFactor);

									float viewDistLerped = lerp(viewDist0, viewDist1, kLerpFactor);

									float3 posVsLerped = lerp(posVs0, posVs1, kLerpFactor);


									// plane 0
									float plane0 = m_data3.x + m_data3.y * viewDistLerped;

									// evaluate
									float planeBlendVal = PlaneEvalSimple(plane0, kRangeInv);
									
									float plane1 = m_data3.z + m_data3.w * viewDistLerped;
									
									planeBlendVal = min(planeBlendVal, PlaneEvalSimple(plane1, kRangeInv));

									// start distance
									float plane2 = m_data2.z + m_data2.w * viewDistLerped;

									planeBlendVal = min(planeBlendVal, PlaneEvalSimpleOneSide(plane2, 0, newLightDesc.m_startDistanceRange, 1.0));

									planeBlendVal *= planeBlendVal;

									blendSum += planeBlendVal;
								}

								float planeStartDistFade = avgDensity * blendSum / kNumShadowSteps;

								#else

								
								float planeStartDistFade = 1;
								{
									float plane0Start = m_data3.x + m_data3.y * viewDist0;
									float plane0End = m_data3.x + m_data3.y * viewDist1;
									planeStartDistFade *= PlaneIntegration(plane0Start, plane0End, kRange);
								}

								{
									float plane0Start = m_data3.z + m_data3.w * viewDist0;
									float plane0End = m_data3.z + m_data3.w * viewDist1;
									planeStartDistFade *= PlaneIntegration(plane0Start, plane0End, kRange);
								}


								//{
								//	float plane0Start = m_data2.z + m_data2.w * viewDist0;
								//	float plane0End = m_data2.z + m_data2.w * viewDist1;
								//	planeStartDistFade *= PlaneIntegration(plane0Start, plane0End, kRange);
								//
								//	plane0Start = plane0Start - -1;
								//	plane0Start = -plane0Start;
								//	plane0End = plane0End - -1;
								//	plane0End = -plane0End;
								//	planeStartDistFade *= PlaneIntegration(plane0Start, plane0End, kRange);
								//}
								
								planeStartDistFade *= planeStartDistFade;
								#endif


								//float centerIntensity = saturate((lerp(startAlongDir, endAlongDir, 0.5f) - startDistance) / kStartPlaneDistFade);

								//centerIntensity *= centerIntensity;
								//float alognLightIntensityFactor = saturate(abs(alongDirMax - alongDirMin) / 0.01);

								//lightStartDistFade = lerp(centerIntensity, lightStartDistFade, alognLightIntensityFactor);

								//lightStartDistFade *= lightStartDistFade;

								brightness = planeStartDistFade;

								brightness = brightness * depthVarianceFactor;


								//brightness = avgDensity * (plane0Start <= 0.0 && plane0Start > -2.0f);
							}
							#endif

							//float avgIntensity = integral;
							// correct:
							//brightness = lengthInVolume > 0 ? 1.0 * length(uv1-uv0) / length(uvTotal1 - uvTotal0) : 0.0;// *maxIntensityOnSegment; // avgDensity * 0.001 * 

							// disable integration 
							//brightness = avgDensity;

							float finalAngle = finalAngleCoord * 2 * 3.1415;

							float edgeAngle = 26.565 * 3.1415 / 180.0f;

							fadeout = 1.0;
							float finalAngleDeg = finalAngleCoord * 360;
							
							if (uv1.x < 0.5)
							{
								//lightColor = float3(1, 1, 0);
							}

							if (uv1.x >= 0.5)
							{
								//lightColor = float3(0, 1, 0);
							}
							//if (finalAngleCoord <= 0.25)
							//	lightColor = float3(0, 1, 0);
							//else if (finalAngleCoord <= 0.5)
							//	lightColor = float3(1, 1, 0);
							//else if (finalAngleCoord <= 0.75)
							//	lightColor = float3(0, 1, 1);
							//else if (finalAngleCoord <= 1.0)
							//	lightColor = float3(0, 0, 1);



//								lightColor = float3(abs(cos(finalAngle)), abs(sin(finalAngle)), 0);
							#if DEBUG_IN_FILL_IN && 0
							if (pSrt->m_debugControls2.y > 0 && (pSrt->m_debugControls.z > 0 || pSrt->m_debugControls.w > 0))
							{
								float finalAngleDeg = finalAngleCoord * 360;

								bool on = abs(finalAngleDeg - pSrt->m_debugControls2.x) < pSrt->m_debugControls2.y;
								//bool onUv = abs(uv0.x - pSrt->m_debugControls2.x) < pSrt->m_debugControls2.y;
								//bool onUv = abs(uv0.y - pSrt->m_debugControls2.x) < pSrt->m_debugControls2.y;
								//bool onUv = abs(startUv.x - pSrt->m_debugControls2.x) < pSrt->m_debugControls2.y;
								bool onUv = abs(finalUv.x - pSrt->m_debugControls2.x) < pSrt->m_debugControls2.y;


								lightColor = float3(on, 0, 0);

								lightColor = float3(on * uv0, on * (uv0.x > 0.5)) * 10.0;

								lightColor = float3(onUv * uv0, 0) * 10.0;

								lightColor = float3(onUv * finalUv, 0) * 10.0;
							}
							#endif

							#if !FROXEL_START_DISTANCE_SHADOW
								brightness *= lightStartDistFade;
							#endif
							//	brightness *= linearFalloff;

							if (debugOccupancy)
							{
								lightColor = float3(0, 0, 1);
								brightness = 1.0f;
							}

							//lightColor = float3(finalAngleCoord < 0.25 || finalAngleCoord > 0.75, finalAngleCoord > 0.25 && finalAngleCoord < 0.5, finalAngleCoord > 0.5) * 10;
							//lightColor = float3(debugVector) * 10;
							//lightColor = float3(debugVector.y > 0, debugVector.z > 0, 0) * 2;
							//lightColor = float3(debugVector.z > debugVector.x, debugVector.w > debugVector.y, 0) * 2;
							//lightColor = float3(abs(finalAngle - 3.1415) < edgeAngle, 0, 0) * 2;
							//lightColor = float3(debugVector.z, -debugVector.w, 0) / 3.0;
							//lightColor = float3(accum1 > accum0, accum0 > 0.0 /* startUv.y > 1.0*/, accum1 > 0.0) * 5;
							//lightColor = float3(uvFroxelLineStart.x, uvFroxelLineStart.y * 0 /* startUv.y > 1.0*/, 0.0) * 1;
							//lightColor = float3(lightDirToPlaneAngle, 0 /* startUv.y > 1.0*/, 0.0) * 1;
							//lightColor = float3(intersectionTimes.x / 64, intersectionTimes.y / 64, 0.0) * 10;
							//lightColor = float3(integral, 0 /* startUv.y > 1.0*/, 0.0) * 10;
							//lightColor = float3(integral, 0.0, 0.0) * 1000;
							//lightColor = float3(finalDistanceInTime, 0, 0.0) * 100;
							//lightColor = float3(maxIntensityOnSegment, 0.0, 0.0) * 10;
							//lightColor = float3(finalAngleCoord, 0.0, 0.0) * 3;

							//lightAccumValue.rgb += float3(1, 1, 1);
							#endif
						} // if spotlight or projector
						}
						#endif // #if ALLOW_SPOT_LIGHTS
						brightness *= fadeout;

						// shadows
						#if ALLOW_SHADOWS
						#if ALLOW_POINT_LIGHTS
						if (lightDesc.m_localShadowTextureIdx != kInvalidLocalShadowTextureIndex
							#if !FILL_IN_FROXELS_LIGHT_POINT_LIGHT_ONLY
							// no need to check if point lights only
							&& lightType == kPointLight
							#endif
						)
							//if (false)
						{
							//lightColor = float3(0, 1, 0);


							//float len = length(lightToPosWs);
							// current VS len should match WS len

							int kNumShadowSteps = 4;


							float kHalfStep = 1.0 / (2 * kNumShadowSteps);
							float kStep = 1.0 / kNumShadowSteps;
							float shadowSample = 0;

							for (int iStep = 0; iStep < kNumShadowSteps; ++iStep)
							{
								float kFactor = kHalfStep + kStep * iStep;

								float3 lightToPosVs1 = (posVs0 - lightDesc.m_viewPos);
								float lightToPosVs1Len = length(lightToPosVs1);
								float3 lightToPosVs1Norm = lightToPosVs1Len > 0.0001 ? lightToPosVs1 / lightToPosVs1Len : float3(0,0,0);

								float froxelLineAlongLightDir = min(0, dot(posVs1 - lightDesc.m_viewPos, lightToPosVs1Norm) - dot(posVs0 - lightDesc.m_viewPos, lightToPosVs1Norm));
								// because froxels are deep, we could have shadow/light bleeding throug walls if part of froxel is behind the wall and is lit even though we shouldn't see any lit fog
								// so we offset shadow value by how far the froxel depth moves along light dir


#if FROXEL_RTL_V_1 & 1
								// we do stepping only within the volume inside of light source
								// this is more efficient because we don't do steps that are outside of light source but inside of froxel
								//float kStartFactor = (startInVolume - viewDist0) / (viewDist1 - viewDist0);
								//float kEndFactor = (endInVolume - viewDist0) / (viewDist1 - viewDist0);

								float kLerpFactor = lerp(kStartFactor, kEndFactor, kFactor);
#else
								float kLerpFactor = kFactor;
#endif

								float3 posVsLerped = lerp(posVs0, posVs1, kLerpFactor);
								
							
								float3 posWsLerped =  mul(float4(posVsLerped, 1), pSrt->m_mVInv).xyz;
							
								float3 toLight = lightPosWs - posWsLerped;


								//lightRayWS.x = dot(view2WorldMat[0].xyz, lightRayVS);
								//lightRayWS.y = dot(view2WorldMat[1].xyz, lightRayVS);
								//lightRayWS.z = dot(view2WorldMat[2].xyz, lightRayVS);

								//lightRayWS = normalize(lightRayWS);
							
								//float3 lightSpacePos = CalcTetraLightSpacePos(-lightToPosWs);
								float3 lightSpacePos;
								//[isolate]
								#if FOG_TEXTURE_DENSITY_ONLY
								{lightSpacePos = CalcTetraLightSpacePosStatic(-toLight); }
								#else
								{lightSpacePos = CalcTetraLightSpacePos(-toLight); }
								#endif
								uint bufferId = lightDesc.m_localShadowTextureIdx;
								float2 shadowTexCoord = float2(lightSpacePos.x*0.5f + 0.5f, lightSpacePos.y*-0.5f + 0.5f);
								//float depthZ = pSrt->m_shadowArray.SampleLevel(pSrt->m_pointSampler, shadowTexCoord, 0).x;

								//float   shadowSpaceZ = (float)(pSrt->m_pConsts->m_shadowScreenViewParams.y / (depthZ - pSrt->m_pConsts->m_shadowScreenViewParams.x));
								//float	shadowRayLen = shadowSpaceZ * length(lightRayWS / max3(absLightRayWS.x, absLightRayWS.y, absLightRayWS.z));
								//float3	rayDir = normalize(lightPosVS.xyz + shadowRayLen * lightRayVS);

								// making it bigger means depth check will succeed more easily
								lightSpacePos.z -= froxelLineAlongLightDir * 1.5f;

								float refDepth = (lightDesc.m_depthScale + lightDesc.m_depthOffset / lightSpacePos.z) * lightDesc.m_depthOffsetSlope + lightDesc.m_depthOffsetConst;

								
								float storedValue = pSrt->m_pLocalShadowTextures->SampleLevel(pSrt->m_pointSampler, shadowTexCoord, 0.0f, bufferId);

								shadowSample += pSrt->m_pLocalShadowTextures->SampleCmpLevelZero(pSrt->m_shadowSampler, shadowTexCoord, refDepth, bufferId) / kNumShadowSteps;

								//float shadowMapViewZ = lightDesc.m_depthOffset / (depthZ - lightDesc.m_depthScale);
								//maxCasterReceiverDistance = max(maxCasterReceiverDistance, lightSpacePos.z - shadowMapViewZ);
								//lightColor = float3(refDepth > 0.5, 0, 1) * 1;
								//if (shadowTest == 0.0)
								//if (refDepth > storedValue)
								//{
								//	lightColor = float3(0, 0, 1);
								//	#if FROXEL_RTL_V_1
								//		inShadow = 1.0;
								//	#else
								//		brightness = converganceRate;
								//	#endif
								//}
							}

							{
								#if FROXEL_RTL_V_1
									inShadow = 1.0 - shadowSample;
								#else
									brightness = converganceRate;
								#endif
							}
						} // if shadowed point light
						#endif // #if ALLOW_POINT_LIGHTS
						//TODO: shadow
						#if ALLOW_SPOT_LIGHTS
						#if ALLOW_POINT_LIGHTS
						else 
						#endif
						if (lightDesc.m_localShadowTextureIdx != kInvalidLocalShadowTextureIndex
							#if !FILL_IN_FROXELS_SPOT_ONLY
							// no need to check if same light type
							&& (lightType == kSpotLight || lightType == kProjectorLight)
							#endif
						)
						{

							//float3 posVs0 = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * viewZ0;
							//float3 posVs1 = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * viewZ1;

							//float viewDist0 = length(posVs0);
							//float viewDist1 = length(posVs1);


							float4 posAlt = float4(posWsJit - pSrt->m_altWorldPos, 1.f);

							float3 lightSpacePos;
							lightSpacePos.x = dot(lightDesc.m_altWorldToLightMatrix[0], posAlt);
							lightSpacePos.y = dot(lightDesc.m_altWorldToLightMatrix[1], posAlt);
							lightSpacePos.z = dot(lightDesc.m_altWorldToLightMatrix[2], posAlt); // actually w

							// this actually converts from view space into shadow space

							
							
							float3 lightSpacePos0;
							lightSpacePos0.x = dot(newLightDesc.m_altWorldToLightMatrix[0], float4(posVs0, 1.f));
							lightSpacePos0.y = dot(newLightDesc.m_altWorldToLightMatrix[1], float4(posVs0, 1.f));
							lightSpacePos0.z = dot(newLightDesc.m_altWorldToLightMatrix[2], float4(posVs0, 1.f));
							
							float3 lightSpacePos1;
							lightSpacePos1.x = dot(newLightDesc.m_altWorldToLightMatrix[0], float4(posVs1, 1.f));
							lightSpacePos1.y = dot(newLightDesc.m_altWorldToLightMatrix[1], float4(posVs1, 1.f));
							lightSpacePos1.z = dot(newLightDesc.m_altWorldToLightMatrix[2], float4(posVs1, 1.f));

							float2 shadowUv0 = lightSpacePos0.xy /= lightSpacePos0.z;
							float2 shadowUv1 = lightSpacePos1.xy /= lightSpacePos1.z;

							float uvTravelLength = length(shadowUv1 - shadowUv0);
							float2 shadowUvDir = uvTravelLength > 0.0 ? (shadowUv1 - shadowUv0) / uvTravelLength : float2(0, 0);


							uint spotShadowDimX, spotShadowDimY, spotShadowDimZ;
							pSrt->m_shadowSpotExpArray.GetDimensionsFast(spotShadowDimX, spotShadowDimY, spotShadowDimZ);

							float2 perpendicularShadowDir = float2(-shadowUvDir.y, shadowUvDir.x) / spotShadowDimX;

							int2 closestShadowTexel = shadowUv0 * spotShadowDimX;
							float2 uvOffset = float2(closestShadowTexel) / spotShadowDimX - shadowUv0;
							shadowUv0 += uvOffset;
							shadowUv1 += uvOffset;

							float2 shadowUvDirJit = shadowUvDir / spotShadowDimX;

							shadowUvDirJit *= (pos.x + pos.y + pSrt->m_frameNumber) % 2 ? 0.5 : -0.5;

							float kFroxelsToCover = 1.0;
							#if ALLOW_DETAILED_SPOT_LIGHTS
							if (newLightDesc.m_flags & NEW_VOL_LIGHT_FLAG_USE_EXPANDED_FROXEL_SAMPLING)
							{
								kFroxelsToCover = newLightDesc.m_expandedFroxelSize;
							}
							#endif

							float numShadowPixels = uvTravelLength * spotShadowDimX;
							numShadowPixels *= kFroxelsToCover;
							int iNumShadowPixels = numShadowPixels;

							if (kFroxelsToCover != 1.0)
							{
								iNumShadowPixels = iNumShadowPixels & 0xFFFFFFFE;
							}

							int kNumShadowSteps = pSrt->m_spotLightShadowMaxSteps;

							kNumShadowSteps = min(max(4, iNumShadowPixels), pSrt->m_spotLightShadowMaxSteps);

							// test
							//kNumShadowSteps = pSrt->m_spotLightShadowMaxSteps;

							// we need to keep the body loop non-divergent so that jitter logic always has a neighbor
							// we could do parallel regression and average too.
							kNumShadowSteps = ReadFirstLane(kNumShadowSteps);

							float kHalfStep = kFroxelsToCover / (2 * kNumShadowSteps);
							float kStep = kFroxelsToCover / kNumShadowSteps;
							float shadowSample = 0;

							//if (pSrt->m_debugControls2.w > 0 && kNumShadowSteps > pSrt->m_debugControls2.w)
							//{
							//	lightColor.r = 1.0;
							//}
							//float accumProbabilityInFrontOfShadowGeo = 0;
							for (int iStep = 0; iStep < kNumShadowSteps; ++iStep)	
							{

								float srtJit = pSrt->m_ndcFogJitterZ05;
								float srtJit1 = pSrt->m_ndcFogJitterZ05 - sign(pSrt->m_ndcFogJitterZ05) * 0.5;
								float negFactorBasedOnPos = ((iSlice + pos.x + pos.y) % 2) ? -1.0 : 1.0;

								float vairableJit = negFactorBasedOnPos > 0 ? srtJit : srtJit1;

								float kStartOIffset = (kFroxelsToCover - 1.0) / 2.0f;

								float kFactor = -kStartOIffset + kHalfStep + kStep * iStep;
								
								// this jitter works well for expanded sampling
								if (kFroxelsToCover != 1.0)
									kFactor = kFactor + kStep * /*vairableJit*/ (((iSlice + pos.x + pos.y) + iStep) % 2 ? srtJit : srtJit1); // +kHalfStep * (((pos.x + pos.y + iStep + pSrt->m_frameNumber) % 2) ? 1.0 : -1.0);

								float froxelLineAlongLightDir = min(0, dot(posVs1 - lightDesc.m_viewPos, lightDesc.m_viewDir) - dot(posVs0 - lightDesc.m_viewPos, lightDesc.m_viewDir));
								// because froxels are deep, we could have shadow/light bleeding throug walls if part of froxel is behind the wall and is lit even though we shouldn't see any lit fog
								// so we offset shadow value by how far the froxel depth moves along light dir
								float factor = (iStep < kNumShadowSteps / 2) ? (iStep + 1) : (kNumShadowSteps - iStep);

								
#if FROXEL_RTL_V_1 && 1
								// we do stepping only within the volume inside of light source
								// this is more efficient because we don't do steps that are outside of light source but inside of froxel
								//float kStartFactor = (startInVolume - viewDist0) / (viewDist1 - viewDist0);
								//float kEndFactor = (endInVolume - viewDist0) / (viewDist1 - viewDist0);

								float kLerpFactor = kFroxelsToCover != 1.0 ? kFactor : lerp(kStartFactor, kEndFactor, kFactor);
#else
								float kLerpFactor = kFactor;
#endif

								float outsideOfVolumeFactor = 1; //  kFroxelsToCover != 2.0 || (kLerpFactor >= kStartFactor && kLerpFactor <= kEndFactor);

								float3 posVsLerped = lerp(posVs0, posVs1, kLerpFactor);

								// swap
								float posVsHorSwapX = LaneSwizzle(posVsLerped.x, 0x1F, 0x00, 0x01);
								float posVsHorSwapY = LaneSwizzle(posVsLerped.y, 0x1F, 0x00, 0x01);
								float posVsHorSwapZ = LaneSwizzle(posVsLerped.z, 0x1F, 0x00, 0x01);
								
								

								float posVsLerpedOtherH = LaneSwizzle(posVsLerped.x, 0x1F, 0x00, 0x01);
								float posVsLerpedOtherV = LaneSwizzle(posVsLerped.y, 0x1F, 0x00, 0x08);
								//float posVsLerpedOtherZ = LaneSwizzle(posVsLerped.z, 0x1F, 0x00, 0x08);
								float xDif = posVsLerpedOtherH - posVsLerped.x;
								float yDif = posVsLerpedOtherV - posVsLerped.y;

								//if (iStep % 2 == 0)
								int counter = iStep + (/*pos.x + pos.y + */pSrt->m_frameNumber);
								
								#if VOLUMETRICS_SUPPORT_ORTHO_LIGHTS
								if (!isOrthoProjector)
								#endif
								{
									float xs = counter % 2 ? 1.0f : -1.0f;
									float ys = counter % 4 < 2 ? 1.0f : -1.0f;
									//posVsLerped.x += xs * 1.0* pSrt->m_debugControls2.x * xDif; // -1 +1 -1 +1
									//posVsLerped.y += ys * 1.0* pSrt->m_debugControls2.x * yDif; // +1 +1 -1 -1

									//if (xs > 0)
									{
										posVsLerped = lerp(posVsLerped, float3(posVsHorSwapX, posVsHorSwapY, posVsHorSwapZ), xs * pSrt->m_runtimeNeighborJitterFactor);
										//posVsLerped.y = posVsHorSwapY;
										//posVsLerped.z = posVsHorSwapZ;

										
									}
									
									posVsHorSwapX = LaneSwizzle(posVsLerped.x, 0x1F, 0x00, 0x08);
									posVsHorSwapY = LaneSwizzle(posVsLerped.y, 0x1F, 0x00, 0x08);
									posVsHorSwapZ = LaneSwizzle(posVsLerped.z, 0x1F, 0x00, 0x08);

									///if (ys > 0)
									{
										posVsLerped = lerp(posVsLerped, float3(posVsHorSwapX, posVsHorSwapY, posVsHorSwapZ), ys * pSrt->m_runtimeNeighborJitterFactor);

										//posVsLerped.x = posVsHorSwapX;
										//posVsLerped.y = posVsHorSwapY;
										//posVsLerped.z = posVsHorSwapZ;
									}
								}
								

								lightSpacePos.x = dot(newLightDesc.m_altWorldToLightMatrix[0], float4(posVsLerped, 1.f));
								lightSpacePos.y = dot(newLightDesc.m_altWorldToLightMatrix[1], float4(posVsLerped, 1.f));
								lightSpacePos.z = dot(newLightDesc.m_altWorldToLightMatrix[2], float4(posVsLerped, 1.f));

								//lightSpacePos.x = dot(lightDesc.m_altWorldToLightMatrix[0], posAlt);
								//lightSpacePos.y = dot(lightDesc.m_altWorldToLightMatrix[1], posAlt);
								//lightSpacePos.z = dot(lightDesc.m_altWorldToLightMatrix[2], posAlt); // actually w


								//lightSpacePos = lerp(lightSpacePos0, lightSpacePos1, kLerpFactor);

								float2 shadowUvLerp = lerp(shadowUv0, shadowUv1, kLerpFactor);


								float depth;
								#if VOLUMETRICS_SUPPORT_ORTHO_LIGHTS
								if (isOrthoProjector)
								{
									lightSpacePos.z += -froxelLineAlongLightDir * pSrt->m_spotShadowOffsetAlongDir / lightDesc.m_radius;

									depth = lightSpacePos.z * lightDesc.m_depthOffsetSlope + lightDesc.m_depthOffsetConst;

									//depth += -froxelLineAlongLightDir * pSrt->m_spotShadowOffsetAlongDir;

								}
								else
								#endif
								{
									lightSpacePos.xy /= lightSpacePos.z;

									//lightSpacePos.xy = shadowUvLerp;
									
									lightSpacePos.z += -froxelLineAlongLightDir * pSrt->m_spotShadowOffsetAlongDir;

									depth = (lightDesc.m_depthScale + lightDesc.m_depthOffset / lightSpacePos.z) * lightDesc.m_depthOffsetSlope + lightDesc.m_depthOffsetConst;
								}

								float3 shadowTexCoord = float3(lightSpacePos.x*0.5f + 0.5f, lightSpacePos.y*-0.5f + 0.5f, lightDesc.m_localShadowTextureIdx);

								float2 uvBLurOffset = perpendicularShadowDir * (iStep % 2) * ((iStep+1) % 4 ? 1 : -1); // 0 1 0 -1 0 1 0 -1 ...

								//shadowTexCoord.xy += uvBLurOffset * pSrt->m_debugControls2.x;

								//shadowTexCoord.xy += shadowUvDirJit;

								//if (lightDesc.m_shadowBlurScale == 0.f)
								{
									//shadowSample = NdFetchSpotShadowArray(float4(shadowTexCoord, depth));

									// for depth format
									//shadowSample += pSrt->m_shadowSpotArray.SampleCmpLOD0(pSrt->m_shadowSampler, shadowTexCoord, depth).x / kNumShadowSteps;

									// check ndc depth
									//shadowSample += (pSrt->m_shadowSpotExpArray.SampleLevel(pSrt->m_pointSampler, shadowTexCoord, 0).x > depth ? 1.0 : 0.0) / kNumShadowSteps;
									// check lin depth

									#if !FROXELS_USE_EXP_SHADOWS_FOR_SPOT_LIGHTS
									//float expResult = (pSrt->m_shadowSpotExpArray.SampleLevel(pSrt->m_pointSampler, shadowTexCoord, 0).x < lightSpacePos.z ? 1.0 : 0.0) / kNumShadowSteps;
									float expResult = (pSrt->m_shadowSpotExpArray.SampleLevel(pSrt->m_pointSampler, shadowTexCoord, 0).x > depth ? 1.0 : 0.0);

									#else
									//check exp depth
									float linearepthNorm = saturate(lightSpacePos.z / (FROXELS_SPOT_HALF_RANGE * 2)); // max 32 meters 0..1 value

									if (newLightDesc.m_flags & NEW_VOL_LIGHT_FLAG_USE_DEPTH_VARIANCE_DARKENING)
									{
										float2 depthVarSample = pSrt->m_shadowSpotDepthVarArray.SampleLevel(pSrt->m_linearSampler, shadowTexCoord, 0).xy;


										float spotLightVariance = depthVarSample.y - depthVarSample.x * depthVarSample.x;

										float probabilityInFrontOfGeo = 1;
										if (linearepthNorm > depthVarSample.x)
										{
											probabilityInFrontOfGeo = saturate(spotLightVariance / (spotLightVariance + pow(depthVarSample.x - linearepthNorm, 2)));

											//float acceptableProbability = 0.9f;
											//sampleDepthVarFactor = min(sampleDepthVarFactor, saturate((probabilityInFrontOfGeo - acceptableProbability) / (1.0 - acceptableProbability)));
											//sampleDepthVarFactor = 0;
										}

										accumProbabilityInFrontOfShadowGeo += probabilityInFrontOfGeo / kNumShadowSteps;
										varAccum += spotLightVariance / kNumShadowSteps;
									}

									


									//linearepthNorm = linearepthNorm * 2.0 - 1.0; // -1..1

									#if FROXELS_ALWAYS_REVERSE_SHADOWS_FOR_SPOT_LIGHTS
										linearepthNorm *= -1;
									#else
										linearepthNorm *= (pSrt->m_reverseShadow > 0.0f ? -1 : 1);
									#endif

									float expMap = exp2(-linearepthNorm * pSrt->m_spotLightExpShadowConstant);
									float storedExpMap = pSrt->m_shadowSpotExpArray.SampleLevel(pSrt->m_linearSampler, shadowTexCoord, 0).x;
									float expResult = saturate(expMap * storedExpMap); // 0 = in shadow, 1.0 fully lit, 1..0 range is eating into real shadow where we are in gradual sahdow falloff
									
									#if FROXELS_ALWAYS_REVERSE_SHADOWS_FOR_SPOT_LIGHTS
										expResult = 1.0 - expResult;
									#else
										if (pSrt->m_reverseShadow)
										{
											expResult = 1.0 - expResult;
										}
									#endif
									#endif

									//expResult = expResult != 0.0;

									//expResult = expResult > 0.176;
									
									//bool inExpShadowP0 = expResult < 0.9;
									//ulong lane_mask = __v_cmp_eq_u32(inExpShadowP0, true);
									//uint myIndex = groupThreadId.y * 8 + groupThreadId.x;
									//uint topIndex = myIndex - 8;
									//uint bottomIndex = myIndex + 8;
									//uint leftIndex = myIndex - 1;
									//uint rightIndex = myIndex + 1;
									//	
									//if (groupThreadId.x > 0 && (groupThreadId.x % 8) < 7 && groupThreadId.y > 0 && (groupThreadId.y % 8) < 7)
									//{
									//	float rightShadow = (lane_mask & __v_lshl_b64(1, rightIndex)) ? 0.0 : 0.125;
									//	float leftShadow = (lane_mask & __v_lshl_b64(1, leftIndex)) ? 0.0 : 0.125;
									//	float topShadow = (lane_mask & __v_lshl_b64(1, topIndex)) ? 0.0 : 0.125;
									//	float bottomShadow = (lane_mask & __v_lshl_b64(1, bottomIndex)) ? 0.0 : 0.125;
									//
									//	expResult = expResult * 0.5 + rightShadow + leftShadow + topShadow + bottomShadow;
									//}

									//uncomment to disable shadow
									//expResult = 1;
							
									//large blend
									if (kFroxelsToCover != 1.0)
										shadowSample += (expResult)* factor;// *sampleDepthVarFactor;
									else
										shadowSample += (expResult) / kNumShadowSteps;

								}
								//else
								{
									// removed a bunch of code that had to do with blurred shadow
								}

								//if (!hasHair)
								{
									//	float powerValue = float(lightDesc.m_shadowCurve & 0x0000ffff) / 256.0f;
									//	float scaleValue = float(lightDesc.m_shadowCurve >> 16) / 256.0f;
									//	float invShadowInfo = 1.001f - shadowSample;
									//	shadowSample = saturate((1.001f - pow(invShadowInfo, powerValue)) * scaleValue);
								}
							}

							//large blend
							if (kFroxelsToCover != 1.0)
							{
								shadowSample /= (kNumShadowSteps / 2);
								shadowSample /= (kNumShadowSteps / 2 + 1);

								float kAdjStart = 0.5f;
								float kAdjPower = 0.7;

								kAdjStart = newLightDesc.m_lightShadowGammaCenter;
								kAdjPower = newLightDesc.m_lightShadowGammaPower;

								if (shadowSample > 0 && shadowSample < kAdjStart)
								{
									shadowSample = pow(shadowSample / kAdjStart, kAdjPower) * kAdjStart;
								}
							}
							
							#if SPOT_LIGHT_CAN_USE_DEPTH_VARIANCE_DARKENING
								// variance range check first

								float maxDepthVarAlowed = newLightDesc.m_lightDarkeningDepthMaxVar;
								float depthVarAlowedRange = newLightDesc.m_lightDarkeningDepthVarBlendRange;
								float acceptableVarianceFactor = saturate((maxDepthVarAlowed - varAccum) * depthVarAlowedRange);// / pSrt->m_spotDepthVarBlendRange;

								//pSrt->m_spotDepthVarProbStart
								//pSrt->m_spotDepthVarProbBlendRange

								//shadowSample = acceptableVarianceFactor;

								//shadowSample = accumProbabilityInFrontOfShadowGeo < 0.1f;
								float acceptableProbabilityFactor = saturate((newLightDesc.m_lightDarkeningDepthVarMaxProb - accumProbabilityInFrontOfShadowGeo) * newLightDesc.m_lightDarkeningDepthVarProbBlendRange);

								if (newLightDesc.m_flags & NEW_VOL_LIGHT_FLAG_USE_DEPTH_VARIANCE_DARKENING)
								{
									shadowToDarknessConversion = acceptableVarianceFactor * acceptableProbabilityFactor;// *(1.0 - shadowSample);
								}
							#endif

							//shadowSample = shadowToDarknessConversion;
							//shadowSample = 1;
							// don't actually darken
							#if FROXEL_RTL_V_1
								//shadowSample = sqrt(shadowSample);
							#if FROXEL_START_DISTANCE_SHADOW
								shadowSample *= lightStartDistFade; // use start distance fade as shadow so we can use temporal for better smoothing
							#endif
							inShadow = 1.0 - shadowSample;
								
							#else
								brightness *= shadowSample;
							#endif

							//float shadowResult = max(shadowSample, lightDesc.m_intensityInShadow);
							//float shadowFade = saturate(depthVS * lightDesc.m_shadowFadeScale + lightDesc.m_shadowFadeOffset);
							//return lerp(shadowResult, 1.f, shadowFade);
						} // if shadowed spot light
						#endif // ALLOW_SPOT_LIGHTS
						#endif // #if ALLOW_SHADOWS


						//if (uint(storedFroxelFlatIndex) != froxelFlatIndex)
						//	lightColor = float3(1, 0, 0);

						//if (uint(storedLightIndex) != lightIndex)
						//	lightColor = float3(0, 1, 0);

						// do lighting right away avoiding temporal for shadows. might need to change this if want temporal
						// brightness *= 1.0 - inShadow;

						float3 colorAdd = lightColor * brightness;

#if !SPLIT_FROXEL_TEXTURES
						colorAdd *= fogRTLightStartDistFactor;
#endif

						float probabilityBehindGeo = (1.0 - accumProbabilityInFrontOfShadowGeo);

						#if SPOT_LIGHT_CAN_USE_DEPTH_VARIANCE_DARKENING
							float shadowLerp = 1.0 - pow(1.0 - shadowToDarknessConversion, 2); //  saturate((probabilityBehindGeo - 0.5) / 0.5);// pow(inShadow, 10);
							colorAdd = lerp(colorAdd, float3(0), shadowLerp);
						#endif
						
						{
							lightAccumValue.a += length(colorAdd) * (1.0 - inShadow); // store how much of intensity is in light
							
							
							lightAccumValue.rgb += colorAdd;
							lighIntensityAccum += length(colorAdd);

						}

						float lightAOtherH = LaneSwizzle(lightAccumValue.a, 0x1F, 0x00, 0x01);
						float lightAOtherV = LaneSwizzle(lightAccumValue.a, 0x1F, 0x00, 0x08);
						
						//if (iStep % 2 == 0)
						//int counter = iStep + (pos.x + pos.y);
						if (false)
						{
							//float xs = iStep % 2 ? 1.0f : -1.0f;
							//float ys = iStep % 4 < 2 ? 1.0f : -1.0f;
							//posVsLerped.x += xs * 0.25 * xDif;
							//posVsLerped.y += ys * 0.25 * yDif;
						}


#if USE_FOG_PROPERTIES
						destConv = saturate(destConv + converganceRate);
						destConvPureDist = saturate(destConvPureDist + converganceRatePureDist);
#endif
					} // if intersecting times

					//lightAccumValue.rgb += float3(1,0,0);
				} // for numLights / while (intersectionMask)

				  //if (numLights == 1)
				  //	lightAccumValue.rgb += float3(1, 0, 0);
				  //if (numLights == 2)
				  //	lightAccumValue.rgb += float3(0, 1, 0);
				  //if (numLights == 3)
				  //	lightAccumValue.rgb += float3(0, 0, 1);
				  //if (numLights == 4)
				  //	lightAccumValue.rgb += float3(1, 1, 0);
				  //if (numLights == 5)
				  //	lightAccumValue.rgb += float3(1, 0, 1);
				  //if (numLights == 6)
				  //	lightAccumValue.rgb += float3(0, 1, 1);
				  //if (numLights == 7)
				  //	lightAccumValue.rgb += float3(1, 1, 1);




			} // if intersection mask
			//else
			//{
			//	lightAccumValue = float4(0, 1, 0, 0);
			//}
#endif // #if do lights









#if !FILL_IN_FROXELS_LIGHTS_ONLY

			float3 fogPosWs = posWsNoJit;



			float screenWWS = 2 * pSrt->m_zoomX * viewZJit;
			float screenHWS = screenWWS / 16.0 * 9.0f;

			//float zoomX2 = 2 * pSrt->m_zoomX;
			//float zoomX2Sqr = zoomX2 * zoomX2;
			//float viewZSqr = viewZ * viewZ;
			//float volume = (zoomX2Sqr * viewZSqr / 16.0 * 9.0f); // assume tile is 1m deep

			float volume = screenWWS * screenHWS * 1.0f; // assume tile is 1m deep

#if UseExponentialDepth
			float oneSliceDepth = DepthDerivativeAtSlice(sliceCoord);
			volume = volume * oneSliceDepth;
#endif

			//float numFroxels = NumFroxelsXY;
			//float tileVolume = volume / numFroxels;

			float density = pSrt->m_fogInitialDensity;

			float denistyLimitBlend = saturate((pSrt->m_fogInitialDensityLimitHeight - fogPosWs.y) / 16.0);

			density *= denistyLimitBlend;

			// height fog


			//float rayDensity = (exp2(pixelY * heightRange) - cameraHeightExp) / (heightRange * heightDelta);



#if USE_HEIGHT_FOG


			float heightFog = exp2((pSrt->m_fogHeightStart - fogPosWs.y) * pSrt->m_fogHeightRange) * 0.01;
			density += heightFog * pSrt->m_heightfogContribution;

			float layerFog0 = 0.0f;
			{
				float distFromCenter = max(0, abs(pSrt->m_layerFog0.m_fogStart - fogPosWs.y) - pSrt->m_layerFog0.m_fogAddlThickness);
				layerFog0 = (1.0 - saturate(distFromCenter * pSrt->m_layerFog0.m_fogRange));
				layerFog0 = layerFog0 * layerFog0;

				layerFog0 *= pSrt->m_layerFog0.m_fogContribution;
			}
			// horizontal distance fade
			float distToFroxelXZ = length((fogPosWs - pSrt->m_camPos).xz);
			float distToFroxelY = (fogPosWs - pSrt->m_camPos).y;

			// horizontal distance fade
			
			float layer0FogStartDistFactor = saturate((distToFroxelXZ - pSrt->m_layerFog0.m_fogHorizontalStartDist + 16.0) / 16.0);
			layerFog0 *= layer0FogStartDistFactor;

			float layer0HorizontalBlend = saturate((pSrt->m_layerFog0.m_fogHorizontalEndDist - length((fogPosWs - pSrt->m_camPos).xz)) / 8.0);
			layerFog0 *= layer0HorizontalBlend;


			density += layerFog0;

#endif

			float noise = 1.0;

#if DO_NOISE_IN_FOG_CREATION
			float noiseBlend = saturate((pSrt->m_noiseEndDist - distToFroxel) / 4.0);
			noiseBlend *= saturate((distToFroxel - pSrt->m_noiseStartDist + 4.0) / 4.0);

			SamplerState linSampleWrap = SetWrapSampleMode(pSrt->m_linearSampler);
			
			if (noiseBlend > 0.0)
			{
				// apply noise
				//fmod(posWs * pSrt->m_noiseScale /* + pSrt->m_noiseOffset*/, 1.0).xy

				// * (1.0 / (abs(posWs.y) / 24.0 + 1.0))

				float3 noiseuvw = float3(fogPosWs.x + pSrt->m_noiseOffset.x, fogPosWs.y + pSrt->m_noiseOffset.y, fogPosWs.z + pSrt->m_noiseOffset.z) * pSrt->m_noiseScale;
				noise *= pSrt->m_noiseTexture.SampleLevel(linSampleWrap, noiseuvw, 0).x;
				float noiseDetail = pSrt->m_noiseTexture.SampleLevel(linSampleWrap, noiseuvw * pSrt->m_noiseDetailRelativeScale, 0).x;

#if FILL_IN_FROXELS_USE_DETAIL_NOISE
				//if (noise < pSrt->m_noiseDetailOpacityStart)
				//	noise = noise * noiseDetail * pSrt->m_noiseDetailOpacityMult;
#endif

#if RUNTIME_LIGHT_IN_FILL_IN_FROXELS && !FROXELS_NO_SUN_SHADOW_CHECK
			// we need to blur out noise and fill in some constant value
			// in areas where there is fast convergance rate and when camera is moving

			//noise = lerp(noise, 0.5, saturate(destConvPureDist));	
				noise = lerp(noise, 0.5, saturate(destConv * 2.0));

#endif

				float finalNoise0 = pSrt->m_noiseBlendScaleRangeMin; // something like 0.5, meaning noise can't go lower than that
				float maxValue0 = pSrt->m_noiseBlendScaleRangeMax; // noise can highlight some places up
				finalNoise0 = max(pSrt->m_noiseBlendScaleMin, finalNoise0 + noise * (maxValue0 - pSrt->m_noiseBlendScaleRangeMin));
				finalNoise0 = lerp(1.0, finalNoise0, noiseBlend);
			
				density *= finalNoise0;
			}


			
#endif

			float skyDensity = 0;

			// sky fog
#if USE_SKY_FOG
			//float skyFog = exp2((posWs.y - pSrt->m_skyFogStart) * pSrt->m_skyFogRange) * 0.01;
			//float skyFog = exp2(-max(0, abs(pSrt->m_skyFogStart - posWs.y) - pSrt->m_skyFogAddlThickness) * pSrt->m_skyFogRange) * pSrt->m_skyFogContribution;

			float distFromCenter = max(0, abs(pSrt->m_skyFogStart - fogPosWs.y) - pSrt->m_skyFogAddlThickness);
			float skyFog = (1.0 - saturate(distFromCenter * pSrt->m_skyFogRange));
			skyFog = skyFog * skyFog;

			skyFog *= pSrt->m_skyFogContribution;

			
			float horizontalBlend = saturate((pSrt->m_skyFogHorizontalEndDist - distToFroxelXZ) / 128.0);
			skyFog *= horizontalBlend;

			float skyFogStartDistFactor = clamp((distToFroxelXZ - pSrt->m_skyFogHorizontalStartDist + 16.0) / 16.0, 0.0f, 1.0f);
			#if FROXELS_PATCH
			float verticalStartDist = pSrt->m_skyFogVerticalStartDist; // at 50 m, we want the fog to be 1.0 even if we are close horizontally
			float skyFogVertStartDistFactor = clamp((distToFroxelY - verticalStartDist + 16.0) / 16.0, 0.0f, 1.0f);
			skyFogStartDistFactor = max(skyFogStartDistFactor, skyFogVertStartDistFactor);
			#endif

			skyFog *= skyFogStartDistFactor;


			if (skyFog > 0.001f)
			{
				noise = pSrt->m_skyNoiseTexture.SampleLevel(linSampleWrap, float3(fogPosWs.x + pSrt->m_skyNoiseOffset.x, fogPosWs.y + pSrt->m_skyNoiseOffset.y, fogPosWs.z + pSrt->m_skyNoiseOffset.z) * pSrt->m_skyNoiseScale, 0).x;

				float finalNoise1 = pSrt->m_skyNoiseBlendScaleRangeMin; // something like 0.5, meaning noise can't go lower than that
				float maxValue1 = pSrt->m_skyNoiseBlendScaleRangeMax; // noise can highlight some places up
				finalNoise1 = max(pSrt->m_skyNoiseBlendScaleMin, finalNoise1 + noise * (maxValue1 - finalNoise1));
				skyDensity += skyFog * lerp(finalNoise1, 1.0, clamp((skyFog - pSrt->m_skyFogNoiseInfluenceThreshold) / 0.1, 0, 1));
			}
#endif



			//if (iSampleStep == 0)
			{
				//	debugResult.xy = float2(posWs.y, density);
			}

			//if (iSampleStep == numSampleSteps - 1)
			{
				//	debugResult.zw = float2(posWs.y, density);
			}

			float kCubeSide = 1.0;
			float kCubeFalloff = 0.5;

			float kX = 0.0f;
			float kY = 2.0f;
			float kZ = 0.0f;

			//float minDensityMult = min( clamp(1.0 - (abs(posWs.x - kX) - kCubeSide) / kCubeFalloff, 0, 1), min(clamp(1.0 - (abs(posWs.y - kY) - kCubeSide) / kCubeFalloff, 0, 1), clamp(1.0 - (abs(posWs.z - kZ) - kCubeSide) / kCubeFalloff, 0, 1)));
			//float minDensityMult = clamp(1.0 - (length(posWs.xz - float2(kX, kZ)) - kCubeSide) / kCubeFalloff, 0, 1);
			//minDensityMult = minDensityMult * clamp(1.0 - (abs(posWs.y - kY) - kCubeSide) / kCubeFalloff, 0, 1);
			//density *= pow(minDensityMult, 2.2);

			float fogStartDistFactor = clamp((distToFroxel - pSrt->m_ambientFogStartDistance + 4.0) / 4.0, 0.0f, 1.0f);
			#if SPLIT_FROXEL_TEXTURES
			fogStartDistFactor = 1.0; // applied in accum pass
			#endif
			
			//density = density * fogStartDistFactor;
			skyDensity = clamp(skyDensity, 0.0, 1.0);


#if PREMULTIPLY_VOLUME
			float4 destVal = float4(0, 0, 0, tileVolume * (density + skyDensity));
#else
			float4 destVal = float4(0, 0, 0, density + skyDensity);
#endif


			//

			//float4 posNoJitWsH = mul(float4(ndcXNoJit * viewZNoJit, ndcYNoJit * viewZNoJit, ndcZNoJit * viewZNoJit, viewZNoJit), pSrt->m_mAltVPInv);
			//float distToFroxel = length(posNoJitWsH.xyz); // we rely on the fact that altWorldPos is exactly camera position
			//posWsNoJit = posNoJitWsH.xyz + pSrt->m_altWorldPos; // / posWsH.w;


			float3 uv3d;
			uv3d.xy = float2(ndcXJit, ndcYJit) * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);
			uv3d.y = 1.0f - uv3d.y;
			uv3d.z = saturate(CameraLinearDepthToFroxelZCoordExp(viewZJit, 0));
			
			float3 uv3d_NoJit;
			uv3d_NoJit.xy = float2(ndcXJit, ndcYJit) * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);
			uv3d_NoJit.y = 1.0f - uv3d_NoJit.y;
			uv3d_NoJit.z = saturate(CameraLinearDepthToFroxelZCoordExp(viewZNoJit, 0));


			float4 probeData;

			// if using previous frame data
#if FROXEL_EARLY_PASS
			// if we ever decide to not use last frame's data in early pass, we need to modify this code and set render setting use-prev-frame-probe-cache to false
			probeData = pSrt->m_srcProbeCacheFroxelsPrev.SampleLevel(pSrt->m_linearSampler, /*pSrt->m_jitterAmbientSampling ? */prevPosUvw_Jit0/* : prevPosHs_NoJit*/, 0).rgba;
#else
			probeData = pSrt->m_srcProbeCacheFroxels.SampleLevel(pSrt->m_linearSampler, /*pSrt->m_jitterAmbientSampling ? */uv3d/* : uv3d_NoJit*/, 0).rgba;
#endif
			#if USE_MANUAL_POW_FROXELS
			probeData.xyz *= probeData.xyz;
			#endif

			float3 band0RGB = probeData.xyz * 8.0;
			float densityMultFromProbes = (probeData.a - FROXELS_REGION_DENSITY_MID_POINT) * 2.0; // it was stored as [-1, 1] -> [0, 1]

#if FROXELS_ACCUMULATE_WITH_PROBES
			band0RGB = float3(0, 0, 0);
			//densityMultFromProbes = 0;
#endif

			destVal.a = saturate(destVal.a + densityMultFromProbes) * fogIntensityStartDistFactor;
			//destVal.a = 0.1f;
			//band0RGB = float3(0, 0, 0);
			//pSrt->m_destFogFroxels[froxelCoord] = destVal;

#if PREMULTIPLY_DENSITY
			noiseAndProbeColor = float4(band0RGB * destVal.a * 1.0, destVal.a) / numSampleSteps;
#else
			float skyFactor = saturate(skyDensity* 50.0);
			float3 tint = lerp(pSrt->m_ambientLightTint, pSrt->m_skyFogAmbientLightTint/* /(max(pSrt->m_ambientProbeContribution, 0.001))*/, skyFactor);

			#if FROXELS_AMBIENT_TINT_IN_ACCUMULATE
			tint = float3(1.0f, 1.0f, 1.0f);
			#endif

			noiseAndProbeColor = float4(band0RGB * fogStartDistFactor * tint, destVal.a) / numSampleSteps;
#endif

#endif // if !FILL_IN_FROXELS_LIGHTS_ONLY
		} // for number of samples = 1. won't work with > 1



			  // analitical for computation for testing
			  //float minY = min(posWs0.y, posWs1.y);
			  //float maxY = max(posWs0.y, posWs1.y);

			  //float inFogRange = max(0, min(fogHeightStart, maxY) - minY);

			  //float density = inFogRange / pSrt->m_heightfogContribution;

			  //float3 uv3d;
			  //uv3d.xy = float2(ndcX, ndcY) * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);
			  //uv3d.y = 1.0f - uv3d.y;
			  //float linearDepth = viewZ0;
			  //uv3d.z = clamp(linearDepth / kGridDepth, 0.0f, 1.0f);
#if UseExponentialDepth
			  //uv3d.z = clamp(LinearDepthToFroxelZCoordExp(linearDepth), 0.0f, 1.0f);
#endif
			  //float3 band0RGB = pSrt->m_srcProbeCacheFroxels.SampleLevel(pSrt->m_linearSampler, uv3d, 0).rgb * 8.0;
			  //noiseAndProbeColor = float4(band0RGB * 1.0, density);

			  //debugResult = float4(inFogRange, minY, maxY, density);

			  //pSrt->m_destFogFroxelsDebug[froxelCoord] = debugResult;
			  //pSrt->m_destFogFroxelsDebug1[froxelCoord] = noiseAndProbeColor;
			  //noiseAndProbeColor.x = 0.0;
			  //noiseAndProbeColor.y = 0.0;
			  //noiseAndProbeColor.z = 0.0;
			  //noiseAndProbeColor = float4(0, 0, 0, 0.01);
			  //destConv = 0;

		#if !FILL_IN_FROXELS_LIGHTS_ONLY
			StorePackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord, float4(PackFroxelColor(noiseAndProbeColor.xyz, pSrt->m_fogExposure), PackFroxelDensity(noiseAndProbeColor.a, pSrt->m_densityPackFactor)));
		#endif

#if RUNTIME_LIGHT_IN_FILL_IN_FROXELS && !FROXELS_NO_SUN_SHADOW_CHECK
#if USE_FOG_PROPERTIES
		
		#if FROXEL_RTL_V_1
			// now calculate the percentage of all intensity added that is in shadow
			float totalAddedIntensity = lighIntensityAccum;

			lightAccumValue.a = lightAccumValue.a / max(totalAddedIntensity, 0.0001); // store how much of intensity is in shadow

			//lightAccumValue.r = 10;
			//lightAccumValue.g = 20;
			//lightAccumValue.b = 30;
			//lightAccumValue.a = 0;

#if FILL_IN_FROXELS_LIGHTS_ONLY
			if (pos.x == 100 && pos.y == 100 && iSlice == 20)
			{
				//_SCE_BREAK();
			}
#endif
																				  // not packing alpha channel, 0..1
			StorePackedValuePropertiesTexture(pSrt->m_destPropertiesFroxels, froxelCoord, float4(PackFroxelColor(lightAccumValue.xyz, pSrt->m_runTimeFogExposure), lightAccumValue.a));
		#else
			StorePackedValuePropertiesTexture(pSrt->m_destPropertiesFroxels, froxelCoord, float4(PackFroxelColor(lightAccumValue.xyz, pSrt->m_fogExposure), PackFroxelDensity(lightAccumValue.a, pSrt->m_densityPackFactor)));
		#endif
#endif
#else
#if USE_FOG_PROPERTIES
		pSrt->m_destPropertiesFroxels[froxelCoord] = float4(0, 0, 0, 0);
#endif
#endif
		}
	}


//TODO: consolidate with shader above. Made a copy because of high risk before demo
#if !FROXELS_DYNAMIC_RES
[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsFillInFroxelsExpensive(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
}
#endif

[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsFillInFroxelsHeightMap(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 pos = groupPos + groupThreadId.xy;


	// march through all slices of froxel grid and accumulate results

	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);

	int lastSliceToEverBeSampled = min(63, int(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x) + 1);

	// optimized
	//for (int iSlice = 0; iSlice <= lastSliceToEverBeSampled; ++iSlice)

	// all
	//for (int iSlice = 0; iSlice < numSlices; ++iSlice)

	// single
	int iSlice = groupId.z+1;

	if (iSlice > lastSliceToEverBeSampled)
		return;

	{
		froxelCoord.z = iSlice;

		float ndcX = float(pos.x + 0.5) / float(pSrt->m_fogGridSize.x) * 2.0f - 1.0f;
		float ndcY = float(pos.y + 0.5) / float(pSrt->m_fogGridSize.y) * 2.0f - 1.0f;
		ndcY = -ndcY;

		float2 pixelPosNormNoJit = float2(pos.x + 0.5, pos.y + 0.5) * float2(pSrt->m_numFroxelsXInv, pSrt->m_numFroxelsXInv * 16.0 / 9.0);
		float posNegBasedOnSlice = (iSlice % 2 ? -1.0 : 1.0);
		float2 pixelPosNorm = pixelPosNormNoJit + (pSrt->m_fogJitterXYUnorm.xy * posNegBasedOnSlice);

		float viewZ = FroxelZSliceToCameraDistExp(iSlice + 0.5, pSrt->m_fogGridOffset);

		float sliceCoord0 = iSlice;
		float sliceCoord1 = iSlice + 1.0;

		float viewZ0 = FroxelZSliceToCameraDistExp(sliceCoord0, pSrt->m_fogGridOffset);
		float viewZ1 = FroxelZSliceToCameraDistExp(sliceCoord1, pSrt->m_fogGridOffset);

		float ndcZ0 = GetNdcDepth(viewZ0, pSrt->m_depthParams);
		float4 posWsH0 = mul(float4(ndcX, ndcY, ndcZ0, 1), pSrt->m_mVPInv);
		float3 posWs0 = posWsH0.xyz / posWsH0.w;

		float ndcZ1 = GetNdcDepth(viewZ1, pSrt->m_depthParams);
		float4 posWsH1 = mul(float4(ndcX, ndcY, ndcZ1, 1), pSrt->m_mVPInv);
		float3 posWs1 = posWsH1.xyz / posWsH1.w;


		float ndcZ = GetNdcDepth(viewZ, pSrt->m_depthParams);

		float4 posWsH = mul(float4(ndcX, ndcY, ndcZ, 1), pSrt->m_mVPInv);
		float3 posWs = posWsH.xyz / posWsH.w;


		float screenWWS = 2 * pSrt->m_zoomX * viewZ;
		float screenHWS = screenWWS / 16.0 * 9.0f;

		//float zoomX2 = 2 * pSrt->m_zoomX;
		//float zoomX2Sqr = zoomX2 * zoomX2;
		//float viewZSqr = viewZ * viewZ;
		//float volume = (zoomX2Sqr * viewZSqr / 16.0 * 9.0f); // assume tile is 1m deep

		float volume = screenWWS * screenHWS * 1.0f; // assume tile is 1m deep

#if UseExponentialDepth
		float oneSliceDepth = DepthDerivativeAtSlice(float(iSlice + 0.5));
		volume = volume * oneSliceDepth;
#endif

		//float numFroxels = NumFroxelsXY;

		//float tileVolume = volume / numFroxels;

		float density = 0;

		// height fog

		// check distance to the height map
		float2 heightMapUv;

		float3 t_center = pSrt->m_heightMapCenter;
		float3 t_size = pSrt->m_heightMapSize;
		//xform to bbox space
		float3 uvw;
		uvw.xz = posWs0.xz - t_center.xz;
		uvw.xz = (uvw.xz + t_size.xz * 0.5f) / t_size.xz;
		//uvw.z = -uvw.z; // need this for seatte map since it has inverted camera..
		


		
		for (int ih = 0; ih < pSrt->m_numHeightMaps; ++ih)
		{
			float height16 = 0;

			HeightMapSetup hSetup = pSrt->m_heightMapSetups[ih];

			uvw.x = dot(posWs0.xz - hSetup.m_corner, hSetup.m_x);
			uvw.z = dot(posWs0.xz - hSetup.m_corner, hSetup.m_z);

			if (uvw.x > 0 && uvw.x < 1 && uvw.z > 0 && uvw.z < 1)
			{
				//float2 heightPacked = pSrt->m_heightMaps[ih].SampleLevel(pSrt->m_pointSampler, uvw.xz, 0).rg;
				//height16 = pow(heightPacked.x, 1) + pow(heightPacked.y, 1) / 255.0;
				//height16 = hSetup.m_bottom + hSetup.m_height * height16 + pSrt->m_particleHeightMapFogOffset;
			
				//height16 = t_center.y - (t_size.y * 0.5) + (height16 * t_size.y) + pSrt->m_particleHeightMapFogOffset;

				height16 = pSrt->m_heightMaps[ih].SampleLevel(pSrt->m_pointSampler, uvw.xz, 0).r;
				height16 = hSetup.m_bottom + hSetup.m_height * height16 + pSrt->m_particleHeightMapFogOffset;


				float minY = min(posWs0.y, posWs1.y);
				float maxY = max(posWs0.y, posWs1.y);


				float inFogRangePercentage;
			
				if (maxY <= height16)
				{
					// fully in fog
					inFogRangePercentage = 1;
				}
				else if (minY >= height16)
				{
					// not in fog at all
					inFogRangePercentage = 0;
				}
				else if (abs(maxY - minY) > 0.01)
				{
					inFogRangePercentage = max(0, min(height16, maxY) - minY) / (maxY - minY);
				}
				else
				{
					inFogRangePercentage = 1;
				}
			

				if (maxY < height16 - pSrt->m_particleHeightMapThickness)
					inFogRangePercentage = 0;


				// contribution based on fresnel
				float depthVS = viewZ0;
				float3 positionVS = float3(((pixelPosNorm)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVS;
				float3 tofroxelView = normalize(positionVS);

				float3 camToFroxelNorm = normalize(posWs0 - pSrt->m_camPos);

				//float fresnelFactor = (1.0 - tofroxelView.z) * pSrt->m_particleHeightMapFogContribution;
				// pure vertical = 0
				// pure horizontal = 1

				float fresnelContrib = pSrt->m_particleHeightMapFogFresnel * (1.0 - clamp((viewZ0 - pSrt->m_particleHeightMapFogFresnelEndDist) / 3.0, 0, 1));
				float fresnelFactor = clamp(1.0 - abs(camToFroxelNorm.y) * fresnelContrib * 4.0, 0, 1);
			

				density = fresnelFactor * pSrt->m_particleHeightMapFogContribution * (inFogRangePercentage);

			
				//if (posWs.y > height16)
				{
				//	density = (clamp((posWs.y - height16) / 1.0, 0, 1) < 0.99) * pSrt->m_heightfogContribution;
				}
			}
		}

		float noise = 1.0;

#if DO_NOISE_IN_FOG_CREATION && 1
		// apply noise
		//fmod(posWs * pSrt->m_noiseScale /* + pSrt->m_noiseOffset*/, 1.0).xy
		SamplerState linSampleWrap = SetWrapSampleMode(pSrt->m_linearSampler);

		// * (1.0 / (abs(posWs.y) / 24.0 + 1.0))

		float3 noiseuvw = float3(posWs.x + pSrt->m_noiseOffset.x, posWs.y + pSrt->m_noiseOffset.y, posWs.z + pSrt->m_noiseOffset.z) * pSrt->m_noiseScale;
		noise = pSrt->m_noiseTexture.SampleLevel(linSampleWrap, noiseuvw, 0).x;
		float noiseDetail = pSrt->m_noiseTexture.SampleLevel(linSampleWrap, noiseuvw * pSrt->m_noiseDetailRelativeScale, 0).x;

#if FILL_IN_FROXELS_USE_DETAIL_NOISE
		if (noise < pSrt->m_noiseDetailOpacityStart)
			noise = noise * noiseDetail * pSrt->m_noiseDetailOpacityMult;
#endif


		float finalNoise0 = pSrt->m_noiseBlendScaleRangeMin; // something like 0.5, meaning noise can't go lower than that
		float maxValue0 = pSrt->m_noiseBlendScaleRangeMax; // noise can highlight some places up
		finalNoise0 = max(pSrt->m_noiseBlendScaleMin, finalNoise0 + noise * (maxValue0 - pSrt->m_noiseBlendScaleRangeMin));

		density *= finalNoise0;
#endif

		
#if PREMULTIPLY_VOLUME
		float4 destVal = float4(0, 0, 0, tileVolume * density);
#else
		float4 destVal = float4(0, 0, 0, clamp(density, 0, 1));
#endif

		//band0RGB = float3(0, 0, 0);
		//pSrt->m_destFogFroxels[froxelCoord] = destVal;
#if PREMULTIPLY_DENSITY
		StoreAccumAlphaPackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord, PackFroxelDensity(destVal.a, pSrt->m_densityPackFactor));
#else
		StoreAccumAlphaPackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord, PackFroxelDensity(destVal.a, pSrt->m_densityPackFactor));
#endif
		//pSrt->m_destFogFroxels[froxelCoord] = float4(0, 0, 0, 0);
	}
}


[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsFillInFroxelsHeightMapExp(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 pos = groupPos + groupThreadId.xy;


	// march through all slices of froxel grid and accumulate results

	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);

	int lastSliceToEverBeSampled = min(63, int(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x) + 1);

	// optimized
	//for (int iSlice = 0; iSlice <= lastSliceToEverBeSampled; ++iSlice)

	// all
	//for (int iSlice = 0; iSlice < numSlices; ++iSlice)

	// single
	int iSlice = groupId.z + 1;

	if (iSlice > lastSliceToEverBeSampled)
		return;

	{
		froxelCoord.z = iSlice;


		float ndcX = float(pos.x + 0.5) / float(pSrt->m_fogGridSize.x) * 2.0f - 1.0f;
		float ndcY = float(pos.y + 0.5) / float(pSrt->m_fogGridSize.y) * 2.0f - 1.0f;
		ndcY = -ndcY;

		float2 pixelPosNormNoJit = float2(pos.x + 0.5, pos.y + 0.5) * float2(pSrt->m_numFroxelsXInv, pSrt->m_numFroxelsXInv * 16.0 / 9.0);
		float posNegBasedOnSlice = (iSlice % 2 ? -1.0 : 1.0);
		float2 pixelPosNorm = pixelPosNormNoJit + (pSrt->m_fogJitterXYUnorm.xy * posNegBasedOnSlice);

		bool useExpMap = true;

		float viewZ = FroxelZSliceToCameraDistExp(iSlice + 0.5, pSrt->m_fogGridOffset);

		int kNumShadowSteps = 4;

		// pSrt->m_ndcFogJitterZ jitters relative to center of froxel, and adds or removes 0.5 of 1.0 unit
		// since we do multisapmling, we need to jitter much less (within one multisample step)
		float jitter = pSrt->m_ndcFogJitterZ / kNumShadowSteps / 2.0;

		if (useExpMap)
		{
			viewZ = FroxelZSliceToCameraDistExp(iSlice + 0.5 + pSrt->m_ndcFogJitterZ, pSrt->m_fogGridOffset);
		}

		float sliceOffset = -1.0;
		float sliceCoord0 = iSlice + sliceOffset;
		float sliceCoord1 = iSlice + 1.0 + sliceOffset;



		float viewZ0 = FroxelZSliceToCameraDistExp(sliceCoord0 + 1.0 / kNumShadowSteps / 2.0 + jitter, pSrt->m_fogGridOffset);
		float viewZ1 = FroxelZSliceToCameraDistExp(sliceCoord1 - 1.0 / kNumShadowSteps / 2.0 + jitter, pSrt->m_fogGridOffset);

		float ndcZ0 = GetNdcDepth(viewZ0, pSrt->m_depthParams);
		float4 posWsH0 = mul(float4(ndcX, ndcY, ndcZ0, 1), pSrt->m_mVPInv);
		float3 posWs0 = posWsH0.xyz / posWsH0.w;

		float ndcZ1 = GetNdcDepth(viewZ1, pSrt->m_depthParams);
		float4 posWsH1 = mul(float4(ndcX, ndcY, ndcZ1, 1), pSrt->m_mVPInv);
		float3 posWs1 = posWsH1.xyz / posWsH1.w;


		float ndcZ = GetNdcDepth(viewZ, pSrt->m_depthParams);

		float4 posWsH = mul(float4(ndcX, ndcY, ndcZ, 1), pSrt->m_mVPInv);
		float3 posWs = posWsH.xyz / posWsH.w;


		float screenWWS = 2 * pSrt->m_zoomX * viewZ;
		float screenHWS = screenWWS / 16.0 * 9.0f;

		//float zoomX2 = 2 * pSrt->m_zoomX;
		//float zoomX2Sqr = zoomX2 * zoomX2;
		//float viewZSqr = viewZ * viewZ;
		//float volume = (zoomX2Sqr * viewZSqr / 16.0 * 9.0f); // assume tile is 1m deep

		float volume = screenWWS * screenHWS * 1.0f; // assume tile is 1m deep

#if UseExponentialDepth
		float oneSliceDepth = DepthDerivativeAtSlice(float(iSlice + 0.5));
		volume = volume * oneSliceDepth;
#endif

		//float numFroxels = NumFroxelsXY;

		//float tileVolume = volume / numFroxels;

		float density = 0;

		// height fog

		// check distance to the height map
		float2 heightMapUv;

		float3 t_center = pSrt->m_heightMapCenter;
		float3 t_size = pSrt->m_heightMapSize;
		//xform to bbox space
		float3 uvw;
		uvw.xz = posWs0.xz - t_center.xz;
		uvw.xz = (uvw.xz + t_size.xz * 0.5f) / t_size.xz;
		//uvw.z = -uvw.z; // need this for seatte map since it has inverted camera..

		float2 xzOffset = posWs0.xz - pSrt->m_camPos.xz;
		float dist = length(xzOffset);

		float distFade = 1.0 - saturate((dist - pSrt->m_particleHeightMapEndFadeDist) * pSrt->m_particleHeightMapEndFadeRange);


		if (distFade > 0.0f)
		{
			for (int ih = 0; ih < pSrt->m_numHeightMaps; ++ih)
			{
				float height16 = 0;

				HeightMapSetup hSetup = pSrt->m_heightMapSetups[ih];

				uvw.x = dot(posWs0.xz - hSetup.m_corner, hSetup.m_x);
				uvw.z = dot(posWs0.xz - hSetup.m_corner, hSetup.m_z);

				float3 uvw0;
				float3 uvw1;

				uvw0.x = dot(posWs0.xz - hSetup.m_corner, hSetup.m_x);
				uvw0.z = dot(posWs0.xz - hSetup.m_corner, hSetup.m_z);
				uvw1.x = dot(posWs1.xz - hSetup.m_corner, hSetup.m_x);
				uvw1.z = dot(posWs1.xz - hSetup.m_corner, hSetup.m_z);

				if (uvw.x > 0 && uvw.x < 1 && uvw.z > 0 && uvw.z < 1)
				{
					//float2 heightPacked = pSrt->m_heightMaps[ih].SampleLevel(pSrt->m_pointSampler, uvw.xz, 0).rg;
					//height16 = pow(heightPacked.x, 1) + pow(heightPacked.y, 1) / 255.0;
					//height16 = hSetup.m_bottom + hSetup.m_height * height16 + pSrt->m_particleHeightMapFogOffset;

					//height16 = t_center.y - (t_size.y * 0.5) + (height16 * t_size.y) + pSrt->m_particleHeightMapFogOffset;

					height16 = pSrt->m_heightMaps[ih].SampleLevel(pSrt->m_pointSampler, uvw.xz, 0).r;
					height16 = hSetup.m_bottom + hSetup.m_height * height16 + pSrt->m_particleHeightMapFogOffset;


					float minY = min(posWs0.y, posWs1.y);
					float maxY = max(posWs0.y, posWs1.y);


					float inFogRangePercentage;

					if (maxY <= height16)
					{
						// fully in fog
						inFogRangePercentage = 1;
					}
					else if (minY >= height16)
					{
						// not in fog at all
						inFogRangePercentage = 0;
					}
					else if (abs(maxY - minY) > 0.01)
					{
						inFogRangePercentage = max(0, min(height16, maxY) - minY) / (maxY - minY);
					}
					else
					{
						inFogRangePercentage = 1;
					}


					if (maxY < height16 - pSrt->m_particleHeightMapThickness)
						inFogRangePercentage = 0;

					if (true)
					{
						inFogRangePercentage = 0;
						for (int i = 0; i < kNumShadowSteps; ++i)
						{
							float lerpFactor = 0.5 / kNumShadowSteps + 1.0 / kNumShadowSteps * i;

							// use exponential shadow maps

							float froxelY = lerp(posWs0.y, posWs1.y, lerpFactor) + +pSrt->m_particleHeightMapFogOffset;
							float froxelYRelative = froxelY - hSetup.m_bottom;

							uvw = lerp(uvw0, uvw1, lerpFactor);

							float linearepthNorm = saturate(froxelYRelative / (pSrt->m_heightMapExpRange)); // max 32 meters 0..1 value
							linearepthNorm = linearepthNorm * 2.0 - 1.0; // -1..1
							float expMap = exp2(-linearepthNorm * pSrt->m_heightMapExpConstant);
							float storedExpMap = pSrt->m_heightMaps[ih].SampleLevel(pSrt->m_linearSampler, uvw.xz, 0).g;
							float expResult = saturate(expMap * storedExpMap); // 0 = in shadow, 1.0 fully lit, 1..0 range is eating into real shadow where we are in gradual sahdow falloff
							expResult = 1.0 - abs(expResult - 0.5) * 2.0;
							expResult *= expResult;
							expResult *= expResult;
							expResult *= expResult;
							expResult = min(expResult, 0.9);

							inFogRangePercentage += expResult / kNumShadowSteps; // expResult > 0.1 && expResult < 1.0;

						}

					}


					// contribution based on fresnel
					float depthVS = viewZ0;
					float3 positionVS = float3(((pixelPosNorm)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * depthVS;
					float3 tofroxelView = normalize(positionVS);

					float3 camToFroxelNorm = normalize(posWs0 - pSrt->m_camPos);

					//float fresnelFactor = (1.0 - tofroxelView.z) * pSrt->m_particleHeightMapFogContribution;
					// pure vertical = 0
					// pure horizontal = 1

					float fresnelContrib = pSrt->m_particleHeightMapFogFresnel * (1.0 - clamp((viewZ0 - pSrt->m_particleHeightMapFogFresnelEndDist) / 3.0, 0, 1));
					float fresnelFactor = clamp(1.0 - abs(camToFroxelNorm.y) * fresnelContrib * 4.0, 0, 1);


					density = fresnelFactor * pSrt->m_particleHeightMapFogContribution * (inFogRangePercentage);


					//if (posWs.y > height16)
					{
						//	density = (clamp((posWs.y - height16) / 1.0, 0, 1) < 0.99) * pSrt->m_heightfogContribution;
					}
				}
			}

			density *= distFade;
		}

		float noise = 1.0;

#if DO_NOISE_IN_FOG_CREATION && 1
		// apply noise
		//fmod(posWs * pSrt->m_noiseScale /* + pSrt->m_noiseOffset*/, 1.0).xy
		SamplerState linSampleWrap = SetWrapSampleMode(pSrt->m_linearSampler);

		// * (1.0 / (abs(posWs.y) / 24.0 + 1.0))

		float3 noiseuvw = float3(posWs.x + pSrt->m_noiseOffset.x, posWs.y + pSrt->m_noiseOffset.y, posWs.z + pSrt->m_noiseOffset.z) * pSrt->m_noiseScale;
		noise = pSrt->m_noiseTexture.SampleLevel(linSampleWrap, noiseuvw, 0).x;
		float noiseDetail = pSrt->m_noiseTexture.SampleLevel(linSampleWrap, noiseuvw * pSrt->m_noiseDetailRelativeScale, 0).x;

#if FILL_IN_FROXELS_USE_DETAIL_NOISE
		if (noise < pSrt->m_noiseDetailOpacityStart)
			noise = noise * noiseDetail * pSrt->m_noiseDetailOpacityMult;
#endif


		float finalNoise0 = pSrt->m_noiseBlendScaleRangeMin; // something like 0.5, meaning noise can't go lower than that
		float maxValue0 = pSrt->m_noiseBlendScaleRangeMax; // noise can highlight some places up
		finalNoise0 = max(pSrt->m_noiseBlendScaleMin, finalNoise0 + noise * (maxValue0 - pSrt->m_noiseBlendScaleRangeMin));

		density *= finalNoise0;
#endif


#if PREMULTIPLY_VOLUME
		float4 destVal = float4(0, 0, 0, tileVolume * density);
#else
		float4 destVal = float4(0, 0, 0, clamp(density, 0, 1));
#endif

		//band0RGB = float3(0, 0, 0);
		//pSrt->m_destFogFroxels[froxelCoord] = destVal;
#if PREMULTIPLY_DENSITY
		StoreAccumAlphaPackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord, PackFroxelDensity(destVal.a, pSrt->m_densityPackFactor));
#else
		StoreAccumAlphaPackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord, PackFroxelDensity(destVal.a, pSrt->m_densityPackFactor));
#endif
		//pSrt->m_destFogFroxels[froxelCoord] = float4(0, 0, 0, 0);
	}
}


struct DispatchReference
{
	uint m_offset;
	uint m_dispatchOffset;
	uint m_size;
};

struct CommandBufferPatchSrt
{
	RWStructuredBuffer<uint> m_commandBuffer;
	StructuredBuffer<DispatchReference> m_dispatches;
	uint m_stepOffset;
	uint m_maxSteps;
	uint m_numGroupsToDispatch;
	uint m_cmdBufferIterationSize;
};


[NUM_THREADS(1, 1, 1)] //
void CS_PatchLoopCommandBuffer(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	CommandBufferPatchSrt *pSrt : S_SRT_DATA)
{
#if 1
	//if (pSrt->m_maxSteps < 1000)
	//	return;

	//if (pSrt->m_maxSteps > 1000)
	//	return;

	//if (pSrt->m_maxSteps * 2 > 1000)
	//	return;


	int numSteps = (pSrt->m_numGroupsToDispatch + pSrt->m_stepOffset - 1) / pSrt->m_stepOffset;

	if (numSteps > pSrt->m_maxSteps)
		return;

	// keep numSteps - 1 dispatches not touched.
	// last one needs to be patched with correct size
	// the rest need to be no-oped

	int kOffsetToDispatchParams = 50;
	DispatchReference dispatchRef = pSrt->m_dispatches[numSteps - 1];
	pSrt->m_commandBuffer[dispatchRef.m_dispatchOffset + 1] = pSrt->m_numGroupsToDispatch % pSrt->m_stepOffset;


	if (numSteps + 1 >= pSrt->m_maxSteps)
		return;

	for (int i = numSteps; i < pSrt->m_maxSteps; ++i)
	{
		dispatchRef = pSrt->m_dispatches[i];
		// write noop code
		// skip 7: c0051000 00000000 
		// skip 7864: deb61000 00000000  7864-2: 1eb6  , c | 1  = 1100 | 0001

		uint opcode = 0xc0001000 | ((dispatchRef.m_size-2) << 16);	

		pSrt->m_commandBuffer[dispatchRef.m_offset] = opcode;
	}
#endif
}
