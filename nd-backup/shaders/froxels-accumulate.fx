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


[NUM_THREADS(64, 1, 1)] //
void CS_VolumetricsBlurPass0(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	uint maxIndex = NdAtomicGetValue(pSrt->m_gdsOffset_blur);
	uint listIndex = groupId.x * 64 + groupThreadId.x;

	if (listIndex >= maxIndex)
		return;


	uint dispatchMask = pSrt->m_srcDispatchListBlur[listIndex].m_pos;
	uint coordZ = dispatchMask & 0x0000003F; // 6 bits
	uint coordY = (dispatchMask >> 6) & 0x000001FF; // 9 bits
	uint coordX = (dispatchMask >> (6 + 9)) & 0x000001FF; // 9 bits
	uint extraVal = (dispatchMask >> (6 + 9 + 9)) & 0x000000FF; // 8 bits
	float extraValUnorm = extraVal / 255.0f;
	float extraValSnorm = (extraValUnorm - 0.5f) * 2;

	uint2 pos = uint2(coordX, coordY);
	uint iSlice = coordZ;
	uint3 froxelCoord = uint3(pos, iSlice);


	float2 shadowsPackedCompressed = float2(0, 0);
	float storedWaterValue = 0;

	bool safePos = pos.x > 0 && pos.y > 0 && pos.x < uint(pSrt->m_fogGridSize.x) && pos.y < uint(pSrt->m_fogGridSize.y);

	int lastSliceToEverBeSampled = min(63, (uint(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x) + 1));
	int lastSliceSafeToHaveNeighbors = min(63, pSrt->m_srcVolumetricsScreenSpaceInfoOrig[froxelCoord.xy].x + 1);

	shadowsPackedCompressed = pSrt->m_srcMiscFroxels[froxelCoord + uint3(0, 0, 0)].xy;

	//test
	bool canBlur = false;
	if (iSlice <= lastSliceSafeToHaveNeighbors /* && safePos*/)
	{
		canBlur = true;
		// BLUR
		float centerWeight = 0.4f;

		// we just blur with preceding and next slices
		centerWeight = 0.2; // nothing from neighbors
		
		float k = (1.0 - centerWeight) / 4.0f;

		float sliceFactor = 0; // (iSlice + pos.x + pos.y) % 2;


		float2 blurredShadowValue = pSrt->m_srcMiscFroxels[froxelCoord + int3(0, 0, 0)] * centerWeight
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(-1 * sliceFactor, -1, 0)] * k
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, -1 * sliceFactor, 0)] * k
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, +1 * sliceFactor, 0)] * k
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(+1 * sliceFactor, +1, 0)] * k;

		float2 blurredShadowValuePrevSlice = pSrt->m_srcMiscFroxels[froxelCoord + int3(0, 0, -1)] * centerWeight
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(-1 * sliceFactor, -1, -1)] * k
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, -1 * sliceFactor, -1)] * k
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, +1 * sliceFactor, -1)] * k
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(+1 * sliceFactor, +1, -1)] * k;


		float2 blurredShadowValueNextSlice = pSrt->m_srcMiscFroxels[froxelCoord + int3(0, 0, +1)] * centerWeight
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(-1 * sliceFactor, -1, +1)] * k
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, -1 * sliceFactor, +1)] * k
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, +1 * sliceFactor, +1)] * k
			+ pSrt->m_srcMiscFroxels[froxelCoord + int3(+1 * sliceFactor, +1, +1)] * k;

		// we just blur with preceding and next slices

		if (iSlice < lastSliceSafeToHaveNeighbors) // froxel has JUST been revealed
		{
			// can we sample a whole row above or just one sample above

			float offCenter = abs(extraValSnorm); //  how much we are off center

			float ownWeight = 1.0 - 0.25 * offCenter;// *(1.0 - convColor);

			float otherWeight = (1.0 - ownWeight) / 2;

			//blurredShadowValue.x = 1.0;
			#if FROXELS_NUM_MAX_DEPTH_NEIGHBORS == 2
			float kCenter = 5;
			float kNeighbor1 = 4;
			float kNeighbor1Diag = 3.5;
			float kNeighbor2 = 3;
			float kNeighbor2Diag1 = 2.5;
			float kNeighbor2Diag2 = 2.0;

			float sum = kCenter + kNeighbor1 * 4 + kNeighbor1Diag * 4 + kNeighbor2 * 4 + kNeighbor2Diag1 * 8 + kNeighbor2Diag2 * 4;


			blurredShadowValue =
				pSrt->m_srcMiscFroxels[froxelCoord + int3(-2, -2, 0)] * (kNeighbor2Diag2 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, -2, 0)] * (kNeighbor2Diag1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, -2, 0)] * (kNeighbor2 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, -2, 0)] * (kNeighbor2Diag1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+2, -2, 0)] * (kNeighbor2Diag2 / sum) +
				pSrt->m_srcMiscFroxels[froxelCoord + int3(-2, -1, 0)] * (kNeighbor2Diag1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, -1, 0)] * (kNeighbor1Diag / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, -1, 0)] * (kNeighbor1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, -1, 0)] * (kNeighbor1Diag / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+2, -1, 0)] * (kNeighbor2Diag1 / sum) +
				pSrt->m_srcMiscFroxels[froxelCoord + int3(-2, 0, 0)] * (kNeighbor2 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, 0, 0)] * (kNeighbor1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, 0, 0)] * (kCenter / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, 0, 0)] * (kNeighbor1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+2, 0, 0)] * (kNeighbor2 / sum) +
				pSrt->m_srcMiscFroxels[froxelCoord + int3(-2, +1, 0)] * (kNeighbor2Diag1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, +1, 0)] * (kNeighbor1Diag / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, +1, 0)] * (kNeighbor1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, +1, 0)] * (kNeighbor1Diag / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+2, +1, 0)] * (kNeighbor2Diag1 / sum) +
				pSrt->m_srcMiscFroxels[froxelCoord + int3(-2, +2, 0)] * (kNeighbor2Diag2 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, +2, 0)] * (kNeighbor2Diag1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, +2, 0)] * (kNeighbor2 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, +2, 0)] * (kNeighbor2Diag1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+2, +2, 0)] * (kNeighbor2Diag2 / sum);
			#else

			float kCenter = 5;
			float kNeighbor1 = 4;
			float kNeighbor1Diag = 3.5;

			float sum = kCenter + kNeighbor1 * 4 + kNeighbor1Diag * 4;


			blurredShadowValue =
				pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, -1, 0)] * (kNeighbor1Diag / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, -1, 0)] * (kNeighbor1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, -1, 0)] * (kNeighbor1Diag / sum) +
				pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, 0, 0)] * (kNeighbor1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, 0, 0)] * (kCenter / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, 0, 0)] * (kNeighbor1 / sum) +
				pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, +1, 0)] * (kNeighbor1Diag / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, +1, 0)] * (kNeighbor1 / sum) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, +1, 0)] * (kNeighbor1Diag / sum);

			//blurredShadowValue.x = 1;
			#endif

			//blurredShadowValue =
			//	pSrt->m_srcMiscFroxels[froxelCoord + int3(-2, -2, 0)] * (1.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, -2, 0)] * ( 4.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, -2, 0)] * ( 7.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, -2, 0)] * ( 4.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+2, -2, 0)] * (1.0 / 273)+
			//	pSrt->m_srcMiscFroxels[froxelCoord + int3(-2, -1, 0)] * (4.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, -1, 0)] * (16.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, -1, 0)] * (26.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, -1, 0)] * (16.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+2, -1, 0)] * (4.0 / 273) +
			//	pSrt->m_srcMiscFroxels[froxelCoord + int3(-2,  0, 0)] * (7.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(-1,  0, 0)] * (26.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0,  0, 0)] * (41.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1,  0, 0)] * (26.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+2,  0, 0)] * (7.0 / 273) +
			//	pSrt->m_srcMiscFroxels[froxelCoord + int3(-2, +1, 0)] * (4.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, +1, 0)] * (16.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, +1, 0)] * (26.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, +1, 0)] * (16.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+2, +1, 0)] * (4.0 / 273) +
			//	pSrt->m_srcMiscFroxels[froxelCoord + int3(-2, +2, 0)] * (1.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(-1, +2, 0)] * ( 4.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(0, +2, 0)] * ( 7.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+1, +2, 0)] * ( 4.0 / 273) + pSrt->m_srcMiscFroxels[froxelCoord + int3(+2, +2, 0)] * (1.0 / 273);

			blurredShadowValue = ownWeight * blurredShadowValue + otherWeight * blurredShadowValuePrevSlice + otherWeight * blurredShadowValueNextSlice;

			//blurredShadowValue.x = 1;
			//blurredShadowValue.x = offCenter;
		}
		//blurredShadowValue = shadowsPackedCompressed;

		//if (pos.x < pSrt->m_numFroxelsXY.x / 2)
		{
			shadowsPackedCompressed = blurredShadowValue;
		}
	}
	
	// use prev as temp buffer
	pSrt->m_destMiscFroxelsPrev[froxelCoord].xy = shadowsPackedCompressed;
}



[NUM_THREADS(64, 1, 1)] //
void CS_VolumetricsBlurPass1(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{

	uint maxIndex = NdAtomicGetValue(pSrt->m_gdsOffset_blur);
	uint listIndex = groupId.x * 64 + groupThreadId.x;

	if (listIndex >= maxIndex)
		return;

	uint dispatchMask = pSrt->m_srcDispatchListBlur[listIndex].m_pos;
	uint coordZ = dispatchMask & 0x0000003F; // 6 bits
	uint coordY = (dispatchMask >> 6) & 0x000001FF; // 9 bits
	uint coordX = (dispatchMask >> (6 + 9)) & 0x000001FF; // 9 bits
	uint extraVal = (dispatchMask >> (6 + 9 + 9)) & 0x000000FF; // 8 bits
	float extraValUnorm = extraVal / 255.0f;
	float extraValSnorm = extraValUnorm - 0.5f;

	uint2 pos = uint2(coordX, coordY);
	uint iSlice = coordZ;
	uint3 froxelCoord = uint3(pos, iSlice);


	float2 shadowsPackedCompressed = float2(0, 0);
	float storedWaterValue = 0;

	shadowsPackedCompressed = pSrt->m_srcMiscFroxelsPrev[froxelCoord + uint3(0, 0, 0)].xy;

	// write data back
	pSrt->m_destMiscFroxels[froxelCoord].xy = shadowsPackedCompressed;
}



[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsAccumulateFogFroxels(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	// and now do accumulation
	uint2 groupPos = uint2(8, 8) * groupId;

	uint2 pos = groupPos + groupThreadId.xy;

	// march through all slices of froxel grid and accumulate results

	const int numSlices = 64;

	uint3 froxelCoord = uint3(pos, 0);
	uint flatGroupThreadId = groupThreadId.y * 8 + groupThreadId.x;

#if USE_TRANSMITANCE
	float T = 1.; // transmitance
	float3 C = float3(0, 0, 0); // color
	float alpha = 0.;
#endif

	float3 prevVal = float3(0, 0, 0);

	float4 prevRTVal = float4(0, 0, 0, 0);

	float4 accumVal = float4(0, 0, 0, 1); // pSrt->m_destFogFroxels[froxelCoord];

#if 0

	for (int iSlice = 0; iSlice < 64; ++iSlice)
	{
		//froxelCoord.z = iSlice;
		//pSrt->m_destFogFroxels[froxelCoord].xyzw = float4(100.0, 100.0, 0, 1.0); // fill in garbage
		pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw = float4(100.0, 0, 0, 1.0);
	}

	return;
#else

	//#if !FROXELS_DYNAMIC_RES
	//	float2 pixelPosNormNoJit = float2(pos.x + 0.5, pos.y + 0.5) / float2(NumFroxelsXRounded_F, NumFroxelsYRounded_F);
	//#else
		float2 pixelPosNormNoJit = float2(pos.x + 0.5, pos.y + 0.5) * float2(pSrt->m_numFroxelsXInv, pSrt->m_numFroxelsXInv * 16.0 / 9.0);
	//#endif

	
	//#if !FROXELS_DYNAMIC_RES
	//	uint tileFlatIndex = mul24((NumFroxelsXRounded + 7) / 8, groupId.y) + groupId.x;
	//	uint froxelFlatIndex = pSrt->m_numFroxelsXY.x * pos.y + pos.x;
	//#else
		uint numTilesInWidth = (uint(pSrt->m_numFroxelsXY.x) + 7) / 8;
		uint tileFlatIndex = mul24(numTilesInWidth, groupId.y) + groupId.x;
		uint froxelFlatIndex = mul24(pSrt->m_numFroxelsXY.x, pos.y) + pos.x;
	//#endif
	
	#if FROXELS_CHECK_VOLUMES
		uint intersectionMaskConst = pSrt->m_volumetricSSPartsTileBufferRO[tileFlatIndex].m_intersectionMask;

		#if !FOG_TEXTURE_DENSITY_ONLY
		VolumetrciSSPartFroxelData lightFroxelData = pSrt->m_volumetricSSPartsFroxelBufferRO[froxelFlatIndex];
		#endif
	#endif

	bool safePos = pos.x > 0 && pos.y > 0 && pos.x < uint(pSrt->m_fogGridSize.x) && pos.y < uint(pSrt->m_fogGridSize.y);

	
	//int lastSliceToEverBeSampled = min(63, max(pSrt->m_forcedMinSliceForAccum, uint(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x) + 1));
	int lastSliceToEverBeSampled = min(63, (uint(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x) + 1));

	int lastSliceSafeToHaveNeighbors = min(63, pSrt->m_srcVolumetricsScreenSpaceInfoOrig[froxelCoord.xy].x + 1);

	// the first one is always 0. we set it in normal pass or in early pass
	#if !FROXEL_LATE_PASS
		//pSrt->m_destFogFroxels[uint3(pos, 0)] = float4(0, 0, 0, 0);
		//StorePackedFinalFogAccumValue(pSrt->m_destFogFroxelsTemp, uint3(pos, 0), PackFinalFogAccumValue(float4(0.0, 0, 0, 1.0), pSrt->m_accumFactor));
	#endif

	int iSlice = 0;

	#if FROXEL_EARLY_PASS
	{
		// we want to go up to the predicted slice. predictions are done on per group basis

		int lastSliceInEarlyPassPlusOne = pSrt->m_srcVolumetricsReprojectedScreenSpaceInfo[groupId].x;

		lastSliceToEverBeSampled = lastSliceInEarlyPassPlusOne - 1;
	}
	#endif
	#if FROXEL_LATE_PASS
	{
		int lastSliceInEarlyPassPlusOne = pSrt->m_srcVolumetricsReprojectedScreenSpaceInfo[groupId].x;

		iSlice = lastSliceInEarlyPassPlusOne; // we start at first not processed early slice

		froxelCoord.z = lastSliceInEarlyPassPlusOne - 1; // could be -1
		// pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw = float4(C, T); // T is how much of bg will be applied.

		float4 unpacked = lastSliceInEarlyPassPlusOne > 0 ? UnpackFinalFogAccumValue(ReadPackedFinalFogAccumValue(pSrt->m_destFogFroxelsTemp, froxelCoord), 1.0f / pSrt->m_accumFactor) : float4(0, 0, 0, 1);
		C = unpacked.xyz; // pSrt->m_destFogFroxelsTemp[froxelCoord].xyz;
		T = unpacked.w; // pSrt->m_destFogFroxelsTemp[froxelCoord].w;
		//iSlice = 1;
	}
	#endif

	
	float3 viewSpaceUp = mul(float4(0, 1, 0, 0), pSrt->m_mV).xyz; // we assume this stays normalized

	// precaclulate hange of distance based on depth
	float3 posVsAt1m = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * 1.0;
	float4 posWsHAt1m = mul(float4(posVsAt1m, 1), pSrt->m_mVInv);
	float3 posWsAt1m = posWsHAt1m.xyz;// / posWsHAt1m.w;

	float2 worldXZChangePer1m = (posWsAt1m.xz - pSrt->m_camPos.xz);
	float2 worldYChangePer1m = (posWsAt1m.y - pSrt->m_camPos.y);

	float causticsXAt1m = dot(pSrt->m_causticsSamplingXWs, posWsAt1m - pSrt->m_camPos);
	float causticsZAt1m = dot(pSrt->m_causticsSamplingZWs, posWsAt1m - pSrt->m_camPos);

	float2 causticsXZChange = float2(causticsXAt1m, causticsZAt1m); // -pSrt->m_causticsCamPosXZ;


	float distChangePer1m = length(posVsAt1m);
	float distXZChangePer1m = length(posVsAt1m - dot(viewSpaceUp, posVsAt1m) * viewSpaceUp);
	float heightChangePer1m = dot(viewSpaceUp, posVsAt1m);
	float heightBlendChangePer1m = dot(viewSpaceUp, posVsAt1m) * pSrt->m_sunHeightBlendRangeInv;

	float3 froxelLineVs = posVsAt1m / distChangePer1m;

	float inScatterFactor = 1;
	#if FROXELS_SUN_SCATTERING_IN_ACCUMULATE
		inScatterFactor = SingleScattering(froxelLineVs, pSrt->m_lightDirectionVsForAccumulate, pSrt->m_scatterParams.x, pSrt->m_scatterParams.y);
	#endif

//	float sunAvg = 0;
//	float sunSqrAvg = 0;
//
//	int prevISlice = iSlice;
//	for (; iSlice <= min(lastSliceToEverBeSampled, 30); ++iSlice)
//		//for (int iSlice = 0; iSlice < numSlices; ++iSlice)
//	{
//		froxelCoord.z = iSlice;
//#if SPLIT_FROXEL_TEXTURES
//
//		float2 blurredShadowValue = pSrt->m_srcMiscFroxels[froxelCoord + uint3(0, 0, 0)];
//		float sun = blurredShadowValue.x * blurredShadowValue.x;
//
//		sunAvg += sun;
//		sunSqrAvg += sun * sun;
//#endif
//	}
//
//	sunSqrAvg /= (min(lastSliceToEverBeSampled+1, 30));
//	sunAvg /= (min(lastSliceToEverBeSampled+1, 30));
//
//
//	iSlice = prevISlice;




	for (; iSlice <= lastSliceToEverBeSampled; ++iSlice)
		//for (int iSlice = 0; iSlice < numSlices; ++iSlice)
	{
		froxelCoord.z = iSlice;
		
		float4 runtimeVal;

		runtimeVal = pSrt->m_destPropertiesFroxels[froxelCoord + uint3(0, 0, 0)];

		float viewZ0 = FroxelZSliceToCameraDistExp(iSlice + 0.5, pSrt->m_fogGridOffset);
		float viewZBegin = FroxelZSliceToCameraDistExp(iSlice, pSrt->m_fogGridOffset);
		float viewZEnd = FroxelZSliceToCameraDistExp(iSlice + 1.0, pSrt->m_fogGridOffset);

		float3 posVs = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * viewZ0;



		float ndcX = float(pos.x + 0.5) / float(pSrt->m_fogGridSize.x) * 2.0f - 1.0f;
		float ndcY = float(pos.y + 0.5) / float(pSrt->m_fogGridSize.y) * 2.0f - 1.0f;
		ndcY = -ndcY;
		float ndcZ = GetNdcDepth(viewZ0, pSrt->m_depthParams);



		//#if FROXEL_EARLY_PASS
		// if we ever decide to not use last frame's data in early pass, we need to modify this code and set render setting use-prev-frame-probe-cache to false
		//probeData = pSrt->m_srcProbeCacheFroxelsPrev.SampleLevel(pSrt->m_linearSampler, pSrt->m_jitterAmbientSampling ? prevPosUvw_Jit0 : prevPosHs_NoJit, 0).rgba;
		//#else

		float3 uv3d;
		uv3d.xy = float2(ndcX, ndcY) * float2(0.5f, 0.5f) + float2(0.5f, 0.5f);
		uv3d.y = 1.0f - uv3d.y;
		uv3d.z = saturate(CameraLinearDepthToFroxelZCoordExp(viewZ0, 0));

		float3 uv3d_NoJit = uv3d;
		//uv3d_NoJit = float3(0.0, 0.0, 0.0);

		float4 probeData = pSrt->m_srcProbeCacheFroxels.SampleLevel(pSrt->m_linearSampler, uv3d_NoJit, 0).rgba;

		#if USE_MANUAL_POW_FROXELS
			probeData.xyz *= probeData.xyz;
		#endif

		float3 band0RGB = probeData.xyz * 8.0;
		float densityMultFromProbes = (probeData.a - FROXELS_REGION_DENSITY_MID_POINT) * 2.0; // it was stored as [-1, 1] -> [0, 1]
		densityMultFromProbes *= pSrt->m_fogOverallDensityMultiplier;

		//#endif



		#if FROXELS_CHECK_VOLUMES
		
		// non tint version
		float4 particleFog = float4(0, 0, 0, 0);

		// tint version
		particleFog = float4(1, 1, 1, 0);

		float particleFogBlend = 0;
		float particleDensityBlend = 0;
		uint froxelLightIndex = 0;
		uint intersectionMask = intersectionMaskConst;
		
		while (intersectionMask)
		{
			int lightIndex = __s_ff1_i32_b32(intersectionMask);

			froxelLightIndex = lightIndex;
			intersectionMask = __s_andn2_b32(intersectionMask, __s_lshl_b32(1, lightIndex));
			
			#if FOG_TEXTURE_DENSITY_ONLY
			VolumetrciSSPartFroxelData lightFroxelData = pSrt->m_volumetricSSPartsFroxelBufferRO[tileFlatIndex * VOLUMETRICS_MAX_SS_PARTS + froxelLightIndex];

			float4 lightData_m_data0 = lightFroxelData.m_data0[flatGroupThreadId];
			float4 lightData_m_data1 = lightFroxelData.m_data1[flatGroupThreadId];
			#else
				VolumetrciSSLight singleLightData = lightFroxelData.m_lights[froxelLightIndex];
				float4 lightData_m_data0 = singleLightData.m_data0;
				float4 lightData_m_data1 = singleLightData.m_data1;

			#endif


			froxelLightIndex += 1;

			VolumetricFogVolume lightDesc = pSrt->m_volPartsRO[lightIndex];


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

			float viewDist0 = viewZBegin;
			float viewDist1 = viewZEnd;

			float2 intersectionTimes = m_data0.xy;
			float startInVolume = max(viewDist0, intersectionTimes.x);
			float endInVolume = min(viewDist1, intersectionTimes.y);
			float lengthInVolume = clamp(endInVolume - startInVolume, 0, viewDist1 - viewDist0);
			float integratedDensity = lengthInVolume;
			float avgDensity = integratedDensity / (viewDist1 - viewDist0);
			float2 startUv = m_data1.xy;
			float2 uvChange = m_data1.zw;
			float2 uvTotal0 = startUv + (viewDist0 - intersectionTimes.x) * uvChange;
			float2 uvTotal1 = startUv + (viewDist1 - intersectionTimes.x) * uvChange;
			float2 uv0 = uvTotal0;
			float2 uv1 = uvTotal1;

			uv0 = saturate(uv0);
			uv1 = saturate(uv1);
			if (lengthInVolume > 0.0)
			{
					// color pass
				
					float accum1 = 0;
					float accum0 = 0;

					accum1 = pSrt->m_circlePart1TextureRO.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), (uv1), 0).x;
					accum0 = pSrt->m_circlePart1TextureRO.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), (uv0), 0).x;

					float integral = max(abs(accum1 * accum1 - accum0 * accum0) /* / CIRCLE_TEXTURE_FACTOR*/, 0.0) * length(uv1 - uv0) / length(uvTotal1 - uvTotal0);

					//integral = length(uv1 - uv0);

					float intensityFromIntegral = integral / length(uvTotal1 - uvTotal0);

					avgDensity = intensityFromIntegral * lightDesc.m_opacity;

				
					// this particle will add color
					float3 color = lightDesc.m_color * pSrt->m_volumeColor;
					//	avgDensity = 1;

					if (lightDesc.m_color.x > 0)
					{
						particleFog.xyz = particleFog.xyz * (1 - avgDensity) + color * avgDensity;
						particleFogBlend = particleFogBlend * (1 - avgDensity) + avgDensity;


						// also add density based on current integral
						// pSrt->m_volumeDensityOpacityBlendinFull ~ 0.8
						// pSrt->m_volumeDensityOpacityBlendinStart ~ 0.3
						float densityBlend = saturate((intensityFromIntegral - pSrt->m_volumeDensityOpacityBlendinStart) / (pSrt->m_volumeDensityOpacityBlendinFull - pSrt->m_volumeDensityOpacityBlendinStart));

						float partDens = lightDesc.m_density * lightDesc.m_opacity * pSrt->m_volumeDensityMultiplier;

						particleFog.a = particleFog.a * (1 - densityBlend) + partDens * densityBlend;
						particleDensityBlend = particleFogBlend;

						
							
					}
					
				{
					//float accum1 = 0;
					//float accum0 = 0;
					//
					//// density pass
					//accum1 = pSrt->m_circlePart0TextureRO.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), (uv1), 0).x;
					//accum0 = pSrt->m_circlePart0TextureRO.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), (uv0), 0).x;
					//
					//float integral = max(abs(accum1 * accum1 - accum0 * accum0) / CIRCLE_TEXTURE_FACTOR, 0.0) * length(uv1 - uv0) / length(uvTotal1 - uvTotal0);
					//
					////integral = length(uv1 - uv0);
					//
					//float intensityFromIntegral = integral / length(uvTotal1 - uvTotal0);
					//
					//avgDensity = intensityFromIntegral;// *10;

					// this particle will add density
					//float partDens = lightDesc.m_density * pSrt->m_volumeDensityMultiplier;
					//

					//if (lightDesc.m_color.x < 0)
					//{
					//	particleFog.a = particleFog.a * (1 - avgDensity) + partDens * avgDensity;
					//	particleDensityBlend = particleFogBlend;
					//}

					//particleDensityBlend = particleDensityBlend * (1 - avgDensity) + avgDensity;
				}


				
				
				
			}
		}
		#endif


		//float distToFroxel = length(posVs);
		//
		//float froxelHeightVs = dot(viewSpaceUp, posVs);
		//
		//float3 viewSpaceXZ = posVs - dot(viewSpaceUp, posVs) * viewSpaceUp;
		//
		//float viewSpaceXZDistSqr = dot(viewSpaceXZ, viewSpaceXZ);
		//
		
		// optimize
		float viewSpaceXZ = distXZChangePer1m * viewZ0;
		float viewSpaceXZDistSqr = viewSpaceXZ;
		viewSpaceXZDistSqr *= viewSpaceXZDistSqr;

		// optimize
		float distToFroxel = distChangePer1m * viewZ0;
		// optimize
		float froxelHeightVs = heightChangePer1m * viewZ0;


		float froxelBeginHeightVs = heightChangePer1m * viewZBegin;
		float froxelEndHeightVs = heightChangePer1m * viewZEnd;

		float waterHeightVs = pSrt->m_startHeight - pSrt->m_camPos.y;

		float froxelMinHeight = min(froxelBeginHeightVs, froxelEndHeightVs);
		float froxelMaxHeight = max(froxelBeginHeightVs, froxelEndHeightVs);

		float froxelBelowWaterBegin = min(froxelMinHeight, waterHeightVs);
		float froxelBelowWaterEnd = min(froxelMaxHeight, waterHeightVs);


		float froxelAboveWaterLen = min(froxelMaxHeight - froxelMinHeight, max(0, froxelMaxHeight - waterHeightVs));

		float froxelVerticalLen = froxelMaxHeight - froxelMinHeight;
		float forxelAboveWaterPercent = froxelVerticalLen > 0.0001 ? saturate(froxelAboveWaterLen / froxelVerticalLen) : (froxelMaxHeight > waterHeightVs ? 1.0 : 0.0);
		float forxelBelowWaterPercent = 1.0 - forxelAboveWaterPercent;

		float avgHeightUnderWater = lerp(froxelBelowWaterBegin, froxelBelowWaterEnd, 0.5f); // this is only valid when forxelBelowWaterPercent > 0

		// make relative to water start
		avgHeightUnderWater = waterHeightVs - avgHeightUnderWater;

		float sunLightNearBoostVerticalBlend = clamp((pSrt->m_nearBrightnessHeightVs - froxelHeightVs) / 4.0, 0.0f, 1.0f);
		

		float causticsDistFactor = clamp((pSrt->m_causticsRange - distToFroxel) / 4.0, 0.0f, 1.0f);

		float sunFogStartDistFactor = 1.0f;
		float sunFogEndDistFactor = clamp((pSrt->m_sunEndDist - distToFroxel) / 4.0, 0.0f, 1.0f);


		float fogRTLightStartDistFactor = clamp((distToFroxel - pSrt->m_rtLightFogStartDistance + 4.0) / 4.0, 0.0f, 1.0f);

		sunFogStartDistFactor *= sunFogEndDistFactor;

		float sunHeightBlendOffsetVs = pSrt->m_sunHeightBlendOffset - pSrt->m_camPos.y;
		float sunFogHeightFactor = clamp((froxelHeightVs - sunHeightBlendOffsetVs) * pSrt->m_sunHeightBlendRangeInv, 0.0f, 1.0f);
		
		// optimize
		sunFogHeightFactor = saturate(heightBlendChangePer1m * viewZ0 - sunHeightBlendOffsetVs * pSrt->m_sunHeightBlendRangeInv);

		sunFogStartDistFactor *= sunFogHeightFactor;


		// cone

		float3 sunPos = pSrt->m_lightDirectionVsForAccumulate * 1024.0f;
		float3 sunToFroxelVs = normalize(posVs - sunPos);
		float3 sunToCamera = -pSrt->m_adjustedLightDirectionVs;

		float cosFroxelToCamFromSun = dot(sunToFroxelVs, sunToCamera);
		
		float coneAngleCos = pSrt->m_sunConeCos;

		float coneFactor = saturate((cosFroxelToCamFromSun - coneAngleCos) * 1024.0f);  // positive if inside
		
		//float angleClampFactor = pow(saturate(-dot(normalize(posVs), pSrt->m_lightDirectionVsForAccumulate) - pSrt->m_sunAngleClampStart), pSrt->m_sunAngleClampPower);
		float angleRange = 2.0 - (pSrt->m_sunAngleClampStart + 1.0);
		float angleClampFactor = pow(max(0.0001, saturate((-dot(normalize(posVs), -sunToFroxelVs) - pSrt->m_sunAngleClampStart) / angleRange)), pSrt->m_sunAngleClampPower);

		//coneFactor = 1;

		sunFogStartDistFactor *= coneFactor;
		sunFogStartDistFactor *= angleClampFactor;
		sunFogStartDistFactor *= inScatterFactor;


		// choose between ambient vs water start distance

		#if FROXELS_USE_WATER

			#if FROXELS_UNDER_WATER && FROXELS_POST_WATER
				#define RENDERING_UNDER_WATER_FROXELS 1
			#elif !FROXELS_UNDER_WATER && FROXELS_PRE_WATER
				#define RENDERING_UNDER_WATER_FROXELS 1
			#else
				#define RENDERING_UNDER_WATER_FROXELS 0
			#endif
		#else
			#define RENDERING_UNDER_WATER_FROXELS 0
		#endif
		#if RENDERING_UNDER_WATER_FROXELS
			sunFogStartDistFactor *= clamp((distToFroxel - pSrt->m_sunFogUnderWaterStartDistance + 4.0) / 4.0, 0.0f, 1.0f);
		#else
			sunFogStartDistFactor *= clamp((distToFroxel - pSrt->m_sunFogStartDistance + 4.0) / 4.0, 0.0f, 1.0f);
		#endif

		
		#if RENDERING_UNDER_WATER_FROXELS
		float ambientFogStartDistance = pSrt->m_underWaterAmbientFogStartDistance;
		float sunNearBrightness = pSrt->m_underWaterNearBrightness;
		float rtPostMutliplier = pSrt->m_underWaterRtPostMultiplier;
		//todo: do we want to cacnel thick fog checks for under water froxels?
		//thicknessCheck = 0;
		#else
		float ambientFogStartDistance = pSrt->m_ambientFogStartDistance;
		float sunNearBrightness = pSrt->m_nearBrightness;
		float rtPostMutliplier = pSrt->m_rtPostMultiplier;
		#endif

		float sunLightNearBoost = lerp(1.0, sunNearBrightness, saturate(sunLightNearBoostVerticalBlend * (pSrt->m_nearBrightnessDistSqr - viewSpaceXZDistSqr) / 32.0));

		#if FOG_BETTER_NEAR_SUN
		sunLightNearBoost = lerp(1.0, sunNearBrightness, saturate(sunLightNearBoostVerticalBlend * (pSrt->m_nearBrightnessDist - sqrt(viewSpaceXZDistSqr)) / 8.0));
		#endif

		
		float2 worldXZ = pSrt->m_camPos.xz + worldXZChangePer1m * viewZ0;
		float worldY = pSrt->m_camPos.y + worldYChangePer1m * viewZ0;

		float2 worldXZ0 = pSrt->m_camPos.xz + worldXZChangePer1m * viewZBegin;
		float worldY0 = pSrt->m_camPos.y + worldYChangePer1m * viewZBegin;

		float2 worldXZ1 = pSrt->m_camPos.xz + worldXZChangePer1m * viewZEnd;
		float worldY1 = pSrt->m_camPos.y + worldYChangePer1m * viewZEnd;

		float3 posWs = float3(worldXZ.x, worldY, worldXZ.y);

		float3 posWs0 = float3(worldXZ0.x, worldY0, worldXZ0.y);
		float3 posWs1 = float3(worldXZ1.x, worldY1, worldXZ1.y);


		// store sky fog 0..1 value to know which parameter to blend in for ambient tinting


		// this data might need to be used if we read accumulate probes
		#if FOG_TEXTURE_DENSITY_ONLY
		float skyFog = 0;

		if (pSrt->m_skyFogAmbientLightTint.x >= 0.0f)
		{
			float skyFogStartVs = pSrt->m_skyFogStart - pSrt->m_camPos.y;

			float distFromCenter = max(0, abs(skyFogStartVs - froxelHeightVs) - pSrt->m_skyFogAddlThickness);
			skyFog = (1.0 - saturate(distFromCenter * pSrt->m_skyFogRange));
			skyFog = skyFog * skyFog;

			skyFog *= pSrt->m_skyFogContribution;

			// horizontal distance fade
			float horizontalBlend = saturate((pSrt->m_skyFogHorizontalEndDist - viewSpaceXZ) / 128.0);
			skyFog *= horizontalBlend;

			float skyFogStartDistFactor = clamp((viewSpaceXZ - pSrt->m_skyFogHorizontalStartDist + 16.0) / 16.0, 0.0f, 1.0f);
			skyFog *= skyFogStartDistFactor;
		}

		float4 fogValCompressed = pSrt->m_srcFogFroxels[froxelCoord + uint3(0, 0, 0)];
		// TODO: would it be faster to use m_destFogFroxels ? do we just read full cache line anyway?
		FogTextureData fogValDecompressed = ReadPackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord);

		float3 fogColorDecompressed = FogColorFromData(fogValDecompressed);

		float densityUnpacked = UnpackFroxelDensity(FogDensityFromData(fogValDecompressed), pSrt->m_densityUnpackFactor);

		densityUnpacked *= pSrt->m_fogOverallDensityMultiplier;

		float thicknessCheck = 0;
		
		if (pSrt->m_thickFogCheckStart != 1.0)
		{
			float thicknessCheckEndDistFactor = saturate((pSrt->m_thickFogCheckEndDist - distToFroxel) / 4.0);
			thicknessCheck = saturate((densityUnpacked - pSrt->m_thickFogCheckStart) * pSrt->m_thickFogCheckRange) * thicknessCheckEndDistFactor;
		}
		#else
		float skyFog = 0;

		//if (pSrt->m_skyFogAmbientLightTint.x >= 0.0f)
		{
			float skyFogStartVs = pSrt->m_skyFogStart - pSrt->m_camPos.y;

			float distFromCenter = max(0, abs(skyFogStartVs - froxelHeightVs) - pSrt->m_skyFogAddlThickness);
			skyFog = (1.0 - saturate(distFromCenter * pSrt->m_skyFogRange));
			skyFog = skyFog * skyFog;

			skyFog *= pSrt->m_skyFogContribution;

			// horizontal distance fade
			float horizontalBlend = saturate((pSrt->m_skyFogHorizontalEndDist - viewSpaceXZ) / 128.0);
			skyFog *= horizontalBlend;

			float skyFogStartDistFactor = clamp((viewSpaceXZ - pSrt->m_skyFogHorizontalStartDist + 16.0) / 16.0, 0.0f, 1.0f);
			skyFog *= skyFogStartDistFactor;
		}

		float4 fogValCompressed = pSrt->m_srcFogFroxels[froxelCoord + uint3(0, 0, 0)];
		// TODO: would it be faster to use m_destFogFroxels ? do we just read full cache line anyway?
		FogTextureData fogValDecompressed = ReadPackedValueFogTexture(pSrt->m_destFogFroxels, froxelCoord);

		float3 fogColorDecompressed = FogColorFromData(fogValDecompressed);

		float densityUnpacked = UnpackFroxelDensity(FogDensityFromData(fogValDecompressed), pSrt->m_densityUnpackFactor);

		densityUnpacked *= pSrt->m_fogOverallDensityMultiplier;

		float thicknessCheck = 0;

		//if (pSrt->m_thickFogCheckStart != 1.0)
		{
			float thicknessCheckEndDistFactor = saturate((pSrt->m_thickFogCheckEndDist - distToFroxel) / 4.0);
			thicknessCheck = saturate((densityUnpacked - pSrt->m_thickFogCheckStart) * pSrt->m_thickFogCheckRange) * thicknessCheckEndDistFactor;
		}
		#endif

		float nearFogDensity = 0.0f;

		ambientFogStartDistance = lerp(ambientFogStartDistance, pSrt->m_thickFogAmbientStartDistance, thicknessCheck);
		float ambientFogStartDistFactor = clamp((distToFroxel - ambientFogStartDistance + 4.0) / 4.0, 0.0f, 1.0f);

		#if FROXELS_CHECK_VOLUMES
			// disable start distances in particle volume
			ambientFogStartDistFactor = lerp(ambientFogStartDistFactor, 1.0, particleFogBlend);
		#endif

#define USE_NEAR_FOG (FROXELS_USE_WATER && FROXELS_POST_WATER && FROXELS_UNDER_WATER)
		// special case support for near fog
		#if USE_NEAR_FOG

		float	nearDensPow = pSrt->m_nearFogCurve; // -0.3f;
		nearFogDensity = iSlice > 0 ? pSrt->m_nearFogScale * pow(1.2, abs(2 - iSlice ) * nearDensPow) : 0;
		
		nearFogDensity *= 0.1f;
		
		//nearFogDensity = iSlice >= 0 ? pSrt->m_nearFogScale * pow(2.0, abs(2 - iSlice) * nearDensPow) : 0;

		//nearFogDensity = 0;
		ambientFogStartDistFactor = max(ambientFogStartDistFactor, nearFogDensity); // we need to enable ambient lighting where we have near fog so that we can see it
		#endif
		
		
		float2 shadowsPackedCompressed = float2(0, 0);
		float storedWaterValue = 0;
		#if SPLIT_FROXEL_TEXTURES
			shadowsPackedCompressed = pSrt->m_srcMiscFroxels[froxelCoord + uint3(0, 0, 0)].xy;
			#if FROXELS_USE_WATER
				storedWaterValue = pSrt->m_srcMiscFroxels[froxelCoord + uint3(0, 0, 0)].z;
			#endif
		#endif

		
		float xAdd = iSlice % 2 > 0 ? 0.0 : 1.0;
		float4 nextPrevUnpackedColor = runtimeVal.rgba;
		float zCenterWeight = 0.5;// 0.75f;
		float zSizeWeight = (1.0 - zCenterWeight) / 2;


		
		
		//test

		if (iSlice <= lastSliceSafeToHaveNeighbors && pSrt->m_runTimeBlur > 0.0f && safePos)
		{
			// BLUR
			float centerWeight = 0.4f;
			float k = (1.0 - centerWeight) / 4.0f;

			float sliceFactor = 0; // (iSlice + pos.x + pos.y) % 2;

			float4 blurredRuntimeVal = pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(0, 0, 0)] * centerWeight
				+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(-1 * sliceFactor, -1, 0)] * k
				+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(+1, -1 * sliceFactor, 0)] * k
				+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(-1, +1 * sliceFactor, 0)] * k
				+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(+1 * sliceFactor, +1, 0)] * k;

			#if SPLIT_FROXEL_TEXTURES
			
			float2 blurredShadowValue = pSrt->m_srcMiscFroxels[froxelCoord + uint3(0, 0, 0)] * centerWeight
				+ pSrt->m_srcMiscFroxels[froxelCoord + uint3(-1 * sliceFactor, -1, 0)] * k
				+ pSrt->m_srcMiscFroxels[froxelCoord + uint3(+1, -1 * sliceFactor, 0)] * k
				+ pSrt->m_srcMiscFroxels[froxelCoord + uint3(-1, +1 * sliceFactor, 0)] * k
				+ pSrt->m_srcMiscFroxels[froxelCoord + uint3(+1 * sliceFactor, +1, 0)] * k;

			shadowsPackedCompressed = blurredShadowValue;

			#endif
			//centerWeight = 0.2;
			float sideWeight = 0.1f;
			float cornerWeight = 0.1f;

		//float4 blurredRuntimeVal = pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(0, 0, 0)] * centerWeight
		//	+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(-1, -1, 0)] * cornerWeight
		//	+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3( 0, -1, 0)] * sideWeight
		//	+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(+1, -1, 0)] * cornerWeight
		//	+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(-1, 0, 0)] * sideWeight
		//	+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(+1, 0, 0)] * sideWeight
		//	+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(-1, +1, 0)] * cornerWeight
		//	+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3( 0, +1, 0)] * sideWeight
		//	+ pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(+1, +1, 0)] * cornerWeight;
	
			//nextPrevUnpackedColor = blurredRuntimeVal;

			

			//blurredRuntimeVal = prevRTVal * zSizeWeight + blurredRuntimeVal * zCenterWeight + pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(0, 0, +1)] * zSizeWeight;
			
			//blurredRuntimeVal = prevRTVal * 0.25 + blurredRuntimeVal * 0.75;

			

			float4 blurredSunVal = pSrt->m_srcFogFroxels[froxelCoord + uint3(0, 0, 0)] * centerWeight
				+ pSrt->m_srcFogFroxels[froxelCoord + uint3(-1 * sliceFactor, -1, 0)] * k
				+ pSrt->m_srcFogFroxels[froxelCoord + uint3(+1, -1 * sliceFactor, 0)] * k
				+ pSrt->m_srcFogFroxels[froxelCoord + uint3(-1, +1 * sliceFactor, 0)] * k
				+ pSrt->m_srcFogFroxels[froxelCoord + uint3(+1 * sliceFactor, +1, 0)] * k;
			
			
			// we can just blur shadows when we split textures
			#if !SPLIT_FROXEL_TEXTURES
				runtimeVal = blurredRuntimeVal;
				fogValCompressed = blurredSunVal;
			#endif

		}
		else
		{
			float4 blurredRuntimeVal = runtimeVal;

			//blurredRuntimeVal = prevRTVal * zSizeWeight + runtimeVal * zCenterWeight + pSrt->m_srcPropertiesFroxels[froxelCoord + uint3(0, 0, +1)] * zSizeWeight;

			runtimeVal = blurredRuntimeVal;
		}

#if SPLIT_FROXEL_TEXTURES
		shadowsPackedCompressed.xy *= shadowsPackedCompressed.xy;
#endif
		
#if USE_MANUAL_POW_FROXELS
		runtimeVal.xyz = runtimeVal.xyz * runtimeVal.xyz;
		runtimeVal.w = runtimeVal.w * runtimeVal.w;
		
		#if SPLIT_FROXEL_TEXTURES
			runtimeVal.w = shadowsPackedCompressed.y;
		#endif
#endif
		runtimeVal.xyz *= runtimeVal.w;

		

		

		// use of blurred result
		
		#if FOG_TEXTURE_DENSITY_ONLY
			rtPostMutliplier = thicknessCheck != 0 ? lerp(rtPostMutliplier, pSrt->m_thickFogRTContribution, thicknessCheck) : rtPostMutliplier;
		#else
			rtPostMutliplier = lerp(rtPostMutliplier, pSrt->m_thickFogRTContribution, thicknessCheck);
		#endif
		
		

		runtimeVal.xyz *= rtPostMutliplier;

#if SPLIT_FROXEL_TEXTURES
		runtimeVal.xyz *= fogRTLightStartDistFactor;
#endif

		

		#if FOG_TEXTURE_DENSITY_ONLY
			float3 fogColorUnpacked = float3(0, 0, 0);
		#else
			float3 fogColorUnpacked = UnpackFroxelColor(fogColorDecompressed.rgb, pSrt->m_fogExposure);
		#endif
		
		
#if SPLIT_FROXEL_TEXTURES
		// at this point this stores only ambient
		
		fogColorUnpacked *= ambientFogStartDistFactor;
		
		//fogColorUnpacked *= pSrt->m_ambientProbeContribution;

		float3 sunColorAdd = pSrt->m_lightColorIntensity.rgb * UnpackFroxelColor(shadowsPackedCompressed.x, 0) * pSrt->m_sunLightContribution
			* (sunLightNearBoost * sunLightNearBoost) // we square because of legacy reasons, a lot of params are tuned for code that did squaring
			* sunFogStartDistFactor;

		
#endif

		//runtimeVal = SamplePackedValuePropertiesTexture(pSrt->m_srcPropertiesFroxels, pSrt->m_linearSampler, (froxelCoord + float3(xAdd, xAdd, 0.5)) * float3(pSrt->m_numFroxelsXInv, pSrt->m_numFroxelsXInv * 16.0 / 9.0, 1.0 / 64.0));


		
		float2 miscVal = float2(0, 0);

		float underWaterStoredValue = 0;

		#if FROXELS_USE_WATER && SPLIT_FROXEL_TEXTURES
			underWaterStoredValue = storedWaterValue;
		#elif USE_FOG_CONVERGE_BUFFER
			miscVal = pSrt->m_srcMiscFroxels[froxelCoord + uint3(0, 0, 0)];
		#elif FROXELS_USE_WATER
			miscVal.y = pSrt->m_srcMiscFroxels[froxelCoord + uint3(0, 0, 0)];
		#endif


		// possible combinations for water

		// rendering before water surface rendered

			// camera under water. so we are rendering air froxels

			////  "FROXELS_USE_WATER", "1","FROXELS_POST_WATER", "1" <<--- same

			// camera above water. so we are rendering water froxels
			// "FROXELS_USE_WATER", "1", "FROXELS_PRE_WATER", "1"

		// rendering after water surface


		// camera under water, rendering after water has rendered = rendering water froxels
		//// "FROXELS_USE_WATER", "1","FROXELS_POST_WATER", "1", "FROXELS_UNDER_WATER", "1"


		// camera above water. so this is rendering air froxels
		////  "FROXELS_USE_WATER", "1","FROXELS_POST_WATER", "1" <<--- same


		float causticsSunMult = 1.0;
		float causticsRtMult = 1.0;

#if FROXELS_UNDER_WATER_CAUSTICS
		if (causticsDistFactor > 0.0)
		{
			int kNumSamples = floor(pSrt->m_causticsStepsPer1m * (viewZEnd - viewZBegin)) + 1.0;
			float causticsMult = 0;
			for (int iStep = 0; iStep < kNumSamples; ++iStep)
			{
				float2 causticsXZ = pSrt->m_causticsCamPosXZ + causticsXZChange * (viewZBegin + (viewZEnd - viewZBegin) * (0.5 / kNumSamples + 1.0 / kNumSamples * iStep));

				causticsMult += (pSrt->m_causticsTexture.SampleLevel(SetWrapSampleMode(pSrt->m_linearSampler), float3(causticsXZ / 2.0f, pSrt->m_causticsZ), 0.0f).x);
			}

			causticsMult /= kNumSamples;

			//causticsMult = sqrt(causticsMult);

			causticsMult = lerp(1.0, causticsMult * pSrt->m_causticsSunMult, causticsDistFactor);

			causticsSunMult = causticsMult;

			causticsRtMult = (pSrt->m_causticsRtMult > 0.0f ? causticsMult : 1.0f);
		}
#endif

	// precompute some under water stuff
	float densityFactor = 1.0;

#if FROXELS_USE_WATER
		densityFactor = 0.0;
#if SPLIT_FROXEL_TEXTURES
		// density factor is blend in into under water settings
		float kBlendInMeters = pSrt->m_waterDensityBlendInRange;
		float heightUnderWater = underWaterStoredValue * underWaterStoredValue * 16 - kBlendInMeters;
		
		forxelBelowWaterPercent *= (underWaterStoredValue * underWaterStoredValue);

		#if FROXELS_PRE_WATER
			#if RENDERING_UNDER_WATER_FROXELS
				densityFactor = saturate((heightUnderWater) / kBlendInMeters);
				densityUnpacked = lerp(densityUnpacked, pSrt->m_underWaterDensity * 0.1, forxelBelowWaterPercent);
				
				densityUnpacked = lerp(0, pSrt->m_underWaterDensity * 0.1, forxelBelowWaterPercent);

				densityFactor = forxelBelowWaterPercent;
			#else
				// heightUnderWater can be negative, up to -kBlendInMeters
				densityFactor = saturate(-heightUnderWater / kBlendInMeters);

				densityFactor = 1.0 - saturate((heightUnderWater) / kBlendInMeters);

				densityUnpacked = densityUnpacked;
				//densityUnpacked = lerp(densityUnpacked, pSrt->m_underWaterDensity * 0.1, forxelBelowWaterPercent);

				densityFactor = 0;
			#endif
		#else
			#if RENDERING_UNDER_WATER_FROXELS
				densityFactor = saturate((heightUnderWater) / kBlendInMeters);

				densityUnpacked = lerp(densityUnpacked, pSrt->m_underWaterDensity * 0.1, forxelBelowWaterPercent);
				densityUnpacked = lerp(0, pSrt->m_underWaterDensity * 0.1, forxelBelowWaterPercent);

				densityFactor = forxelBelowWaterPercent;
			#else
				// heightUnderWater can be negative, up to -kBlendInMeters
				densityFactor = saturate(-heightUnderWater / kBlendInMeters);

				densityFactor = 1.0 - saturate((heightUnderWater) / kBlendInMeters);

				densityUnpacked = densityUnpacked;

				//densityUnpacked = lerp(densityUnpacked, pSrt->m_underWaterDensity * 0.1, forxelBelowWaterPercent);
				
				densityFactor = 0;
			#endif

		#endif
			
		float waterHeightBlend = saturate(avgHeightUnderWater / kBlendInMeters);
		densityFactor *= waterHeightBlend;

		nearFogDensity *= densityFactor;

		heightUnderWater = avgHeightUnderWater;


		//if (heightUnderWater < 0.1)
		//densityFactor = 1.0;
#else

	#if FROXELS_PRE_WATER
			densityFactor = (miscVal.y > 0);
	#else
			densityFactor = ((1.0 - miscVal.y) > 0);
	#endif

#endif
#endif

	// fogColorUnpacked at this point stores either (ambient color), or (0,0,0) if we sample ambient in accumulate

	float densityAdd = 0;
	// add probe data
	#if FROXELS_ACCUMULATE_WITH_PROBES
		if (true)
	#else
		// if it is not hardcoded, we have it on a switch
		if (pSrt->m_probeSampleInAccumulate)
	#endif
		{
			fogColorUnpacked = float3(0, 0, 0); // explicitly set it to 0 just to make sure it is visible that those values should be 0 since we sample ambient in accumulate
			//densityAdd += densityMultFromProbes; // density from regions

			float skyDensity = skyFog;
			float skyFactor = saturate(skyDensity* 50.0);

			float3 tint = pSrt->m_ambientLightTint;

			#if FOG_TEXTURE_DENSITY_ONLY
			
			float3 skyFogTint = pSrt->m_skyFogAmbientLightTint;
			
			
			if (skyFactor > 0.001f)
			{
				#if FOG_SKY_NOISE_IN_ACCUMULATE

				SamplerState linSampleWrap = SetWrapSampleMode(pSrt->m_linearSampler);

				int kNoiseSamplesPerM = 4;

				int kNoiseSamples = floor(kNoiseSamplesPerM * (viewZEnd - viewZBegin)) + 1.0;


				float noise = 0;
				for (int i = 0; i < kNoiseSamples; ++i)
				{
					float factor = 0.5f / kNoiseSamples + 1.0 / kNoiseSamples * i;
					float3 iPosWs = lerp(posWs0, posWs1, factor);
					noise += 1.0 / kNoiseSamples * pSrt->m_skyColorNoiseTexture.SampleLevel(linSampleWrap, float3(iPosWs.x  + pSrt->m_skyColorNoiseOffset.x, iPosWs.y + pSrt->m_skyColorNoiseOffset.y, iPosWs.z + pSrt->m_skyColorNoiseOffset.z) * pSrt->m_skyColorNoiseScale, 0).x;
				}


				float finalNoise1 = pSrt->m_skyColorNoiseBlendScaleRangeMin; // something like 0.5, meaning noise can't go lower than that
				float maxValue1 = pSrt->m_skyColorNoiseBlendScaleRangeMax; // noise can highlight some places up
				finalNoise1 = max(pSrt->m_skyColorNoiseBlendScaleMin, finalNoise1 + noise * (maxValue1 - finalNoise1));
				skyFogTint *= finalNoise1;

				#endif

			}

			tint = pSrt->m_skyFogAmbientLightTint.x >= 0.0f ? lerp(tint, skyFogTint/* /(max(pSrt->m_ambientProbeContribution, 0.001))*/, skyFactor) : tint;

			#if FOG_SKY_NOISE_IN_ACCUMULATE_ON_ALL_AMBIENT
				// noise applies to all fog, not just sky fog
				SamplerState linSampleWrap = SetWrapSampleMode(pSrt->m_linearSampler);

				int kNoiseSamplesPerM = 4;

				int kNoiseSamples = floor(kNoiseSamplesPerM * (viewZEnd - viewZBegin)) + 1.0;


				float noise = 0;
				for (int i = 0; i < kNoiseSamples; ++i)
				{
					float factor = 0.5f / kNoiseSamples + 1.0 / kNoiseSamples * i;
					float3 iPosWs = lerp(posWs0, posWs1, factor);
					noise += 1.0 / kNoiseSamples * pSrt->m_skyColorNoiseTexture.SampleLevel(linSampleWrap, float3(iPosWs.x + pSrt->m_skyColorNoiseOffset.x, iPosWs.y + pSrt->m_skyColorNoiseOffset.y, iPosWs.z + pSrt->m_skyColorNoiseOffset.z) * pSrt->m_skyColorNoiseScale, 0).x;
				}


				float finalNoise1 = pSrt->m_skyColorNoiseBlendScaleRangeMin; // something like 0.5, meaning noise can't go lower than that
				float maxValue1 = pSrt->m_skyColorNoiseBlendScaleRangeMax; // noise can highlight some places up
				finalNoise1 = max(pSrt->m_skyColorNoiseBlendScaleMin, finalNoise1 + noise * (maxValue1 - finalNoise1));
				tint *= finalNoise1;

			#endif


			tint = thicknessCheck != 0 ? lerp(tint, pSrt->m_thickFogAmbientLightTint, thicknessCheck) : tint;
			#else
			tint = lerp(tint, pSrt->m_skyFogAmbientLightTint/* /(max(pSrt->m_ambientProbeContribution, 0.001))*/, skyFactor);
			tint = lerp(tint, pSrt->m_thickFogAmbientLightTint, thicknessCheck);
			#endif


			

			float3 underWaterTint = float3(1.0f, 1.0f, 1.0f);
			#if FROXELS_USE_WATER && RENDERING_UNDER_WATER_FROXELS
				//underWaterTint = lerp(float3(1, 1, 1), pSrt->m_underWaterAmbientTint, densityFactor);
				underWaterTint = pSrt->m_underWaterAmbientTint;
			#endif
			fogColorUnpacked = band0RGB * tint * ambientFogStartDistFactor * underWaterTint;
		}
		else
		{
			// in case we already have ambient color in fogColorUnpacked

			#if FROXELS_AMBIENT_TINT_IN_ACCUMULATE
				// color is pure probe color without tint. apply tint.
				float skyDensity = skyFog;
				float skyFactor = saturate(skyDensity* 50.0);
				float3 tint = pSrt->m_ambientLightTint;
				
				tint = lerp(tint, pSrt->m_thickFogAmbientLightTint, thicknessCheck);

				tint = lerp(tint, pSrt->m_skyFogAmbientLightTint/* /(max(pSrt->m_ambientProbeContribution, 0.001))*/, skyFactor);

				fogColorUnpacked.rgb *= tint;
			#endif

			#if FROXELS_USE_WATER && RENDERING_UNDER_WATER_FROXELS
				//float3 underWaterTint = lerp(float3(1, 1, 1), pSrt->m_underWaterAmbientTint, densityFactor);
				//fogColorUnpacked.rgb *= underWaterTint;

				fogColorUnpacked.rgb *= pSrt->m_underWaterAmbientTint;
			#endif
		}

#if FROXELS_USE_WATER
#else
	// no absorbtion, can just add color right away
	fogColorUnpacked += sunColorAdd * causticsSunMult;
#endif
	

	runtimeVal *= causticsRtMult;

	// apply under water absorbtion and caustics multipliers
	#if FROXELS_USE_WATER
	#if SPLIT_FROXEL_TEXTURES

		#if RENDERING_UNDER_WATER_FROXELS
		
			// in shader where we enable under water froxels. only then we actually need to apply absorbtion and sun multiplier
			//sunColorAdd *= lerp(1.0, pSrt->m_underWaterSunIntensity, densityFactor);
			sunColorAdd *= pSrt->m_underWaterSunIntensity;

			sunColorAdd *= causticsSunMult;

			fogColorUnpacked += sunColorAdd;

			#if FROXELS_UNDER_WATER
				ApplyColorAbsorbtion(pSrt, pSrt->m_fogColor, pSrt->m_absorbCoefficients, min(heightUnderWater, pSrt->m_absorbMaxHeight) * pSrt->m_absorbHeightIntensity + min(distToFroxel, pSrt->m_absorbMaxDist) * pSrt->m_absorbDistanceIntensity, heightUnderWater > 0, fogColorUnpacked);
			#else
				ApplyColorAbsorbtion(pSrt, pSrt->m_fogColor, pSrt->m_absorbCoefficients, min(heightUnderWater, pSrt->m_absorbMaxHeight) * pSrt->m_absorbHeightIntensity, heightUnderWater > 0, fogColorUnpacked);
			#endif
		#else
			fogColorUnpacked += sunColorAdd * causticsSunMult;
		#endif
	#else
		// already absorbed when we dont split textures
		fogColorUnpacked += sunColorAdd * causticsSunMult;
	#endif
	#endif

#define ABSORPTION (1.30725)

#if USE_TRANSMITANCE
#if PREMULTIPLY_VOLUME
		float dens = FogDensityFromData(destVal) + nearFogDensity;
		float T_i = exp(-ABSORPTION * dens);

		float3 cellColor = destVal.xyz;
		// color in cell was multiplied by density when added, so we use transmitance right before this cell to add color and then we modify trnasmitance
		C += T * UnpackFroxelColor(destVal.xyz);

		T *= T_i;

		pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw = float4(C, T); // T is how much of bg will be applied.
#else
		float kSub = 0.0 / 255.0;
		float dens = saturate(densityUnpacked - kSub + densityAdd); // densityAdd will be a value in case we sample ambient probes in acumulate and add in desnity from regions
		//dens *= densityFactor;

		if (lastSliceToEverBeSampled != 63)
		{
			//destVal.b = 1.0;
		}

		if (uint(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x) > 128)
		{
			//destVal = float4(0.1, 0, 0, 1.0);
		}


		float3 S = fogColorUnpacked - float3(kSub, kSub, kSub); // this is purely color without multiplying by volume nor density of froxel

		float3 sunAndAmbColor = S;

		if (pSrt->m_debugControls2.y > 0)
		{
			//S += float3(1, 0, 0) *  miscVal.x;//;
		}

		


		//S += float3(densityFactor, 0, 0);


		// todo: get rid of this
		//S = min(S, pow(2.0, pSrt->m_fogExposure));
		
		float3 _runtimeColor = UnpackFroxelColor(runtimeVal.rgb, pSrt->m_runTimeFogExposure) - float3(kSub, kSub, kSub); // this is purely color without multiplying by volume nor density of froxel
		
		float kBlendWeight = 1.0f;

		//float3 runtimeColor = prevRTVal * (1.0 - kBlendWeight) + _runtimeColor * kBlendWeight;

		float3 runtimeColor = _runtimeColor;

		prevRTVal = nextPrevUnpackedColor;

		float maxColor = max(max(runtimeColor.x, runtimeColor.y), runtimeColor.z);
		float addlRunTimeDens = pSrt->m_fakeLightDensity * maxColor;

		//addlRunTimeDens = pSrt->m_fakeLightDensity > 0 && maxColor > 0 ? 0.001 : 0.0f;
		addlRunTimeDens = maxColor > 0 ? pSrt->m_fakeLightDensity: 0.0f;

		//dens += addlRunTimeDens;

		// todo: get rid of this
		//runtimeColor = min(S, pow(2.0, pSrt->m_runTimeFogExposure));
		S += runtimeColor.xyz;


		#if FOG_SKY_NOISE_IN_ACCUMULATE_ON_ALL

		float colorNoiseBlend = saturate((pSrt->m_skyColorNoiseEndDist - distToFroxel) / 4.0);
		colorNoiseBlend *= saturate((distToFroxel - pSrt->m_skyColorNoiseStartDist + 4.0) / 4.0);

		if (colorNoiseBlend > 0.0f)
		{
			// noise applies to all fog, not just sky fog
			SamplerState linSampleWrap = SetWrapSampleMode(pSrt->m_linearSampler);

			float kNoiseSamplesPerM = pSrt->m_skyColorNoiseSamplesPerM;

			uint kNoiseSamples = min(pSrt->m_skyColorNoiseMaxSamples, uint(floor(kNoiseSamplesPerM * (viewZEnd - viewZBegin)) + 1.0));

			float noise = 0;
			for (int i = 0; i < kNoiseSamples; ++i)
			{
				float factor = 0.5f / kNoiseSamples + 1.0 / kNoiseSamples * i;
				float3 iPosWs = lerp(posWs0, posWs1, factor);
				noise += 1.0 / kNoiseSamples * pSrt->m_skyColorNoiseTexture.SampleLevel(linSampleWrap, float3(iPosWs.x + pSrt->m_skyColorNoiseOffset.x, iPosWs.y + pSrt->m_skyColorNoiseOffset.y, iPosWs.z + pSrt->m_skyColorNoiseOffset.z) * pSrt->m_skyColorNoiseScale, 0).x;
			}


			float finalNoise1 = pSrt->m_skyColorNoiseBlendScaleRangeMin; // something like 0.5, meaning noise can't go lower than that
			float maxValue1 = pSrt->m_skyColorNoiseBlendScaleRangeMax; // noise can highlight some places up
			finalNoise1 = max(pSrt->m_skyColorNoiseBlendScaleMin, finalNoise1 + noise * (maxValue1 - finalNoise1));
			S *= finalNoise1;
		}

		#endif



		//S += (runtimeColor.xyz * (1.0 + pSrt->m_fakeLightDensity * 100));

		//S = float3(0, 0, 0);

		if (iSlice < pSrt->m_debugControls.x || (pSrt->m_debugControls.y > 0 && iSlice > pSrt->m_debugControls.x + pSrt->m_debugControls.y + 1.0))
		{
			S = float3(0, 0, 0);	
		}

		if (iSlice > 20)
		{
			//if (froxelCoord.x < pSrt->m_numFroxelsXY.x / 4)
			//{
			//	S += float3(1, 0, 0);
			//}
			//else if (froxelCoord.x < pSrt->m_numFroxelsXY.x / 2)
			//{
			//	S += float3(0, 1, 0);
			//}

			if (froxelCoord.x == 0)
			{
				//S += float3(1, 0, 0);
			}
			if (froxelCoord.y == 0)
			{
				//S += float3(0, 1, 0);
			}

		}

		float3 nextPrevVal = S;

		//S = lerp(S, max(S, prevVal), 0.5);
		
		prevVal = nextPrevVal;

		// apply tint
		S = S * pSrt->m_overallFogTint;

		float dd = 1.0f; // assume tile is 1m deep

#if UseExponentialDepth
		dd = DepthDerivativeAtSlice(float(iSlice + 0.5));
#endif
		// transmittance is probability of how many photons travel unobstructed

		// it depends on distance of the chunk we are looking at and density inside of the chunk

		// bigger the extinction coefficient, less light comes though
		// we apply gamme to the coefficient for artistic control of how fast the fog builds up

		float accumDist = FroxelZSliceToCameraDistExp(float(iSlice + 0.5), pSrt->m_fogGridOffset);

		float extinctionFactor = 1.0;


		#if FOG_TEXTURE_DENSITY_ONLY
		if (pSrt->m_accumulationGamma != 0)
		#endif
		{
			if (pSrt->m_accumulationGamma > 0)
			{
				//float realCoeff = pow(10.0, 4.0 - pSrt->m_accumulationGamma);
				//extinctionFactor = (1 + pow(accumDist / realCoeff, 2));
				extinctionFactor = (1 + pow(accumDist * pSrt->m_accumulationGamma, pSrt->m_accumulationGammaCurve));

			}
			else
			{
				//float realCoeff = pow(10.0, 4.0 + pSrt->m_accumulationGamma); // -0.0001 = 4.0, -4.0 = 0
				//extinctionFactor = 1.0 / (1.0 + accumDist / realCoeff);
				extinctionFactor = 1.0 / (1.0 + accumDist * (-pSrt->m_accumulationGamma));

			}
		}

		#if FROXELS_CHECK_VOLUMES
		
		// non tint version
		//S = S * (1.0f - particleFogBlend) + particleFog.xyz;

		// tint version
		S = S * particleFog.xyz;

		
		dens += particleFog.a; // density is additive

		#endif

		//float realCoeff = pow(10.0, pSrt->m_accumulationGamma);
		//float T_i = exp(-dens * dd * (pSrt->m_extinctionCoeffcient * exp2(accumDist / realCoeff)));
		//float T_i = exp(-dens * dd * (pSrt->m_extinctionCoeffcient * (1+pow(accumDist / realCoeff, 2))));
		//float T_i = exp(-dens * dd * (pSrt->m_extinctionCoeffcient * (1.0 + accumDist * pSrt->m_accumulationGamma)));
		float T_i = exp(-dens * dd * (pSrt->m_extinctionCoeffcient * extinctionFactor));
		 
		
		// lets say we want to decrease transmittance when pixel is in shadow ( to exaggerate shadows )
		//T_i *= clamp(S.r, 0, 1);

		//float3 Sint = (S - S * exp(-dens * dd * pSrt->m_extinctionCoeffcient));

		
		// we want T to never become less than pSrt->m_guaranteedBgVisible
		// T_i is less than 1.0 so it will try to make it smaller
		// we can calculate the smallest T_i that is allowed
		float minT_i = saturate(pSrt->m_guaranteedBgVisible / max(T, 0.0001));

		T_i = max(minT_i, T_i);

		// T_i is how much of bg bg color will come through the fog
		// so we see (1 - T_i ) of the fog
		float3 Sint = S * (1.0 - T_i);

		// at this point T is transmittance of layer up to but not including this one
		// so we apply that multiplier to this fog color since that is how much of bg we can see behind all the parsed layers

		
		C += T * (Sint);

		#if USE_NEAR_FOG
			C += nearFogDensity * sunAndAmbColor * 1;
		#endif

		T *= T_i;

		///T = max(T, pSrt->m_guaranteedBgVisible);
		
		//C = float3(1, 1, 1);
		//pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw = float4(C, T); // T is how much of bg will be applied.
		float Tstore = T;
		#if USE_NEAR_FOG
			Tstore *= (1.0 - nearFogDensity);
		#endif

		StorePackedFinalFogAccumValue(pSrt->m_destFogFroxelsTemp, froxelCoord, PackFinalFogAccumValue(float4(C, Tstore), pSrt->m_accumFactor));

#endif

#else
		accumVal.a = accumVal.a;
		
		float densityContribution = FogDensityFromData(destVal);

		// accumulation alpha stores how much of the background is visible

		//accumVal.a = accumVal.a * (1.0 - densityContribution);
		accumVal.a = max(0, accumVal.a - densityContribution);

		accumVal.rgb = accumVal.rgb + destVal.rgb * densityContribution;

		//pSrt->m_destFogFroxels[froxelCoord] = accumVal;

		// in case of forward accumulation, we will actually accumulate colors and density and use it for fog

		pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw = accumVal.rgba;
#endif
	}

	
	iSlice = lastSliceToEverBeSampled + 1;
	for (; iSlice < 64; ++iSlice)
	{
		//froxelCoord.z = iSlice;
		//pSrt->m_destFogFroxels[froxelCoord].xyzw = float4(100.0, 100.0, 0, 1.0); // fill in garbage
		//pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw = float4(100.0, 0, 0, 1.0);
	}

#if CLEAR_UNSEEN_FROXELS
	iSlice = lastSliceToEverBeSampled + 1;
	for (; iSlice < 64 ; ++iSlice)
	{
		froxelCoord.z = iSlice;
		pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw = float4(0, 0, 0, 1.0);
	}
#endif

	//if (pSrt->m_fixedDepth > 0)
	//{
	//	int iEndSLice = min(lastSliceToEverBeSampled, int(CameraLinearDepthToFroxelZSliceExp(pSrt->m_fixedDepth, pSrt->m_fogGridOffset)));
	//	int iStartSlice = 9;
	//	froxelCoord.z = iEndSLice;
	//
	//	float4 endValue = pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw;
	//
	//
	//	//float xH = LaneSwizzle(endValue.x, 0x1F, 0x00, 0x01);
	//	//float yH = LaneSwizzle(endValue.y, 0x1F, 0x00, 0x01);
	//	//float zH = LaneSwizzle(endValue.z, 0x1F, 0x00, 0x01);
	//	//float wH = LaneSwizzle(endValue.w, 0x1F, 0x00, 0x01);
	//	//
	//	//float xV = LaneSwizzle(endValue.x, 0x1F, 0x00, 0x08);
	//	//float yV = LaneSwizzle(endValue.y, 0x1F, 0x00, 0x08);
	//	//float zV = LaneSwizzle(endValue.z, 0x1F, 0x00, 0x08);
	//	//float wV = LaneSwizzle(endValue.w, 0x1F, 0x00, 0x08);
	//	//
	//	//float myWeight = 0.5f;
	//	//float otherWeight = (1.0 - myWeight) / 2;
	//	//
	//	//endValue = endValue * myWeight + float4(xH, yH, zH, wH) * otherWeight + float4(xV, yV, zV, wV) * otherWeight;
	//
	//	// blur then apply everywhere
	//
	//	//float centerWeight = 0.2;
	//	//float sideWeight = 0.1f;
	//	//float cornerWeight = 0.1f;
	//	//
	//	//endValue = endValue * centerWeight 
	//	//+ pSrt->m_destFogFroxelsTemp[froxelCoord + uint3(-1, -1, 0)] * cornerWeight
	//	//+ pSrt->m_destFogFroxelsTemp[froxelCoord + uint3( 0, -1, 0)] * sideWeight
	//	//+ pSrt->m_destFogFroxelsTemp[froxelCoord + uint3(+1, -1, 0)] * cornerWeight
	//	//+ pSrt->m_destFogFroxelsTemp[froxelCoord + uint3(-1, 0, 0)] * sideWeight
	//	//+ pSrt->m_destFogFroxelsTemp[froxelCoord + uint3(+1, 0, 0)] * sideWeight
	//	//+ pSrt->m_destFogFroxelsTemp[froxelCoord + uint3(-1, +1, 0)] * cornerWeight
	//	//+ pSrt->m_destFogFroxelsTemp[froxelCoord + uint3( 0, +1, 0)] * sideWeight
	//	//+ pSrt->m_destFogFroxelsTemp[froxelCoord + uint3(+1, +1, 0)] * cornerWeight;
	//
	//
	//
	//
	//	iSlice = iEndSLice;
	//	for (; iSlice >= iStartSlice; --iSlice)
	//	{
	//		froxelCoord.z = iSlice;
	//		pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw = endValue;// lerp(pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw, endValue, saturate((iSlice - iStartSlice) / 3.0)); // -pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw;
	//	}
	//}

#endif
}


[NUM_THREADS(8, 8, 1)] //
void CS_ClearUnseenFroxels(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	// and now do accumulation
	uint2 groupPos = uint2(8, 8) * groupId;

	uint2 pos = groupPos + groupThreadId.xy;

	uint3 froxelCoord = uint3(pos, 0);

	int lastSliceToEverBeSampled = min(63, (uint(pSrt->m_srcVolumetricsScreenSpaceInfo[froxelCoord.xy].x) + 1));

	int iSlice = 0;

	iSlice = lastSliceToEverBeSampled + 1;
	for (; iSlice < 64; ++iSlice)
	{
		froxelCoord.z = iSlice;
		pSrt->m_destFogFroxelsTemp[froxelCoord].xyzw = float4(0, 0, 0, 1.0);
	}
}
