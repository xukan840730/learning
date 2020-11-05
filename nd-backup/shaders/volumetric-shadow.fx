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

#include "particle-ray-trace-cs-defines.fxi"
#include "particle-ray-trace-cs-ps.fxi"


uint GetCascadedShadowLevelSimple(float depthVS, float4 cascadedLevelDist[2])
{
	float4 cmpResult0 = depthVS > cascadedLevelDist[0].xyzw;
	float4 cmpResult1 = depthVS > cascadedLevelDist[1].xyzw;

	return (uint)dot(cmpResult0, 1.0) + (uint)dot(cmpResult1, 1.0);
}

int GetCascadedLevel(float cascadedLevelDistFloats[8], int numCascadedLevel, float viewZ)
{
	bool bFound = false;
	int i = 0;
	while( i < numCascadedLevel-1 && !bFound)
	{
		if (cascadedLevelDistFloats[i] >= viewZ)
			bFound = true;
		else
			i++;
	}

	return i;
}


void FindFrustumClipEdge(out float2 refRayMinY, out float2 refRayMaxY, float3 clipPlane, float4 viewFrustumPlanes[4])
{
	float3 clipRay0 = cross(clipPlane, viewFrustumPlanes[0]);
	float3 clipRay1 = cross(clipPlane, viewFrustumPlanes[1]);
	float3 clipRay2 = cross(clipPlane, viewFrustumPlanes[2]);
	float3 clipRay3 = cross(clipPlane, viewFrustumPlanes[3]);

	float2 refRayXP = clipRay0.xy / clipRay0.z;
	float2 refRayXN = clipRay1.xy / clipRay1.z;
	float2 refRayYP = clipRay2.xy / clipRay2.z;
	float2 refRayYN = clipRay3.xy / clipRay3.z;

	refRayMinY = refRayXN;
	refRayMaxY = refRayXP;

	if (refRayMinY.y > refRayMaxY.y)
	{
		refRayMinY = refRayXP;
		refRayMaxY = refRayXN;
	}

	if (refRayYP.y > refRayMinY.y && refRayYP.y < refRayMaxY.y)
	{
		refRayMaxY = refRayYP;
	}

	if (refRayYN.y > refRayMinY.y && refRayYN.y < refRayMaxY.y)
	{
		refRayMinY = refRayYN;
	}
}

float3 CalculateSunlightShadowRayDir(int numCascadedLevels,
	float cascadedLevelDistFloats[8],
	float4 shadowMatAndTranspose[kMaxNumCascaded * 6],
	Texture2DArray<float> txSunShadow,
	SamplerState samplePoint,
	float3 positionVS,
	float viewZ0, float viewZ1, float3 maxValue, float shadowOffset)
{
	int iCascadedLevel0 = GetCascadedLevel(cascadedLevelDistFloats, numCascadedLevels, viewZ0);
	int iCascadedLevel1 = GetCascadedLevel(cascadedLevelDistFloats, numCascadedLevels, viewZ1);
	int iSampleCascadeLevel = GetCascadedLevel(cascadedLevelDistFloats, numCascadedLevels, positionVS.z);

	//int minCascadedlevel = min(iCascadedLevel0, min(iCascadedLevel1, iSampleCascadeLevel));

	// we can sample up to cascade that has the sample point. we know that cascade has a valid result for the sampling point
	// however, we might get a valid result in earlier cascade too, thats why we check which cascaded
	//int maxCascadedLevel = iSampleCascadeLevel; //  max(iCascadedLevel0, iCascadedLevel1);

	int minCascadedlevel = min(iCascadedLevel0, iCascadedLevel1);
	int maxCascadedLevel = max(iCascadedLevel0, iCascadedLevel1);
	float4 posVS = float4(positionVS, 1.0);

	int rightCascadedLevel = minCascadedlevel;
	int i = minCascadedlevel;

	float3 shadowCoords;
	float cascadedDist = cascadedLevelDistFloats[i];
	shadowCoords.x = dot(posVS, shadowMatAndTranspose[i * 6 + 0]);
	shadowCoords.y = dot(posVS, shadowMatAndTranspose[i * 6 + 1]);
	shadowCoords.z = i;

	float rightDepthZ = txSunShadow.SampleLevel(samplePoint, shadowCoords, 0).r;
	float shadowPosVSZ = dot(float4(shadowCoords.xy, rightDepthZ, 1.0f), shadowMatAndTranspose[i * 6 + 5]);

	// we have to check all cascades that this line intersects with becuase position we are checking can be outside of frustum
	// meaning that just because it depth matches a particular cascade doesn't mean that cascade will actually have a value for it
	// if we check cascade and the shadow position for this ray is inside of that cascade, we are guaranteed that we can stop and we got the best result
	// if we get < 1.0 shadow and it is last cascade that is also the best result


	bool bFound = (shadowPosVSZ <= cascadedDist) || (rightDepthZ < 1.0 && i == maxCascadedLevel);

	i++;
	float refShadowPosVSZ = shadowPosVSZ;

	while (i <= maxCascadedLevel && !bFound)
	{
		cascadedDist = cascadedLevelDistFloats[i];
		shadowCoords.x = dot(posVS, shadowMatAndTranspose[i * 6 + 0]);
		shadowCoords.y = dot(posVS, shadowMatAndTranspose[i * 6 + 1]);
		shadowCoords.z = i;

		float depthZ = txSunShadow.SampleLevel(samplePoint, shadowCoords, 0).r;

		float4 posSS = float4(shadowCoords.xy, depthZ, 1.0f);
		shadowPosVSZ = dot(posSS, shadowMatAndTranspose[i * 6 + 5]);

		// akovalovs: I'm not sure about this logic here. This is a copy of exisitng code
		// specifically I don't understand yet why we need  && shadowPosVSZ > refShadowPosVSZ

		if ((i == maxCascadedLevel || shadowPosVSZ <= cascadedDist)  && shadowPosVSZ > refShadowPosVSZ)
		{
			rightCascadedLevel = i;
			rightDepthZ = depthZ;
			refShadowPosVSZ = shadowPosVSZ;
			bFound = true;
		}
		else if (shadowPosVSZ < refShadowPosVSZ)
		{
			bFound = true;
		}

		i++;
	}

	if (!bFound)
	{
		rightDepthZ = 1.0;
	}

	if (rightDepthZ == 1.0)
	{
		return maxValue;
	}
	else
	{
		float4 posSS = float4(shadowCoords.xy, rightDepthZ + shadowOffset, 1.0f);

		float3 shadowPosVS;
		shadowPosVS.x = dot(posSS, shadowMatAndTranspose[rightCascadedLevel * 6 + 3]);
		shadowPosVS.y = dot(posSS, shadowMatAndTranspose[rightCascadedLevel * 6 + 4]);
		shadowPosVS.z = dot(posSS, shadowMatAndTranspose[rightCascadedLevel * 6 + 5]);

		return shadowPosVS;
	}

}

#define USE_FORCED_CASCADE 0


float3 CalculateSunlightShadowRayDirNew(int numCascadedLevels, float sunIntensity,
	float cascadedLevelDistFloats[8],
#if USE_FORCED_CASCADE
	int forcedCascadeIndex,
#else
	int unused,
#endif
	float4 shadowMatAndTranspose[kMaxNumCascaded * 6],
	Texture2DArray<float> txSunShadow,
	SamplerState samplePoint,
	float3 lightDirectionVs,
	float3 positionVS,
	float viewZ0, float viewZ1, float3 maxValue, float shadowOffset, out bool outFound, out bool couldBeEarlier, float sunBeyondEvaluation, float uvSqueeze)
{
	#if OPTIMIZE_SUN_LIGHT_CACHE
	outFound = false;
	couldBeEarlier = false;
	#endif
	if (numCascadedLevels == 0)
	{
		return sunIntensity == 0 ? -maxValue : maxValue; // fully shadowed when intsntiy is 0
	}


	int iCascadedLevel0 = GetCascadedLevel(cascadedLevelDistFloats, numCascadedLevels, viewZ0);
	int iCascadedLevel1 = GetCascadedLevel(cascadedLevelDistFloats, numCascadedLevels, viewZ1);
	//int iSampleCascadeLevel = GetCascadedLevel(cascadedLevelDistFloats, numCascadedLevels, positionVS.z);

	//int minCascadedlevel = min(iCascadedLevel0, min(iCascadedLevel1, iSampleCascadeLevel));

	// we can sample up to cascade that has the sample point. we know that cascade has a valid result for the sampling point
	// however, we might get a valid result in earlier cascade too, thats why we check which cascaded
	//int maxCascadedLevel = iSampleCascadeLevel; //  max(iCascadedLevel0, iCascadedLevel1);


	// we want to find a sample in shadow cascade that corresponds to our sampling point
	// there could be samples for this point in multiple cascades
	// in fact the data could be different in multiple cascades

	// technically, the most correct result is the one fursthest away from camera along light direction
	// because one object could be in one cascade but not the other
	// but the sample is closer cascade is more detailed


	bool sunAwayFromCamera = lightDirectionVs.z > 0;



	int minCascadedlevel = min(iCascadedLevel0, iCascadedLevel1);
	int maxCascadedLevel = max(iCascadedLevel0, iCascadedLevel1);

	// synchronize min and max cacde levels so we can use scalar iteration

	ulong lane_mask_min_0 = __v_cmp_eq_u32(minCascadedlevel, 0);
	ulong lane_mask_min_1 = __v_cmp_eq_u32(minCascadedlevel, 1);
	ulong lane_mask_min_2 = __v_cmp_eq_u32(minCascadedlevel, 2);

	ulong lane_mask_max_0 = __v_cmp_eq_u32(maxCascadedLevel, 0);
	ulong lane_mask_max_1 = __v_cmp_eq_u32(maxCascadedLevel, 1);
	ulong lane_mask_max_2 = __v_cmp_eq_u32(maxCascadedLevel, 2);

	int uniformMax = 0;
	if (lane_mask_max_2)
	{
		uniformMax = 2;
	}
	else if (lane_mask_max_1)
	{
		uniformMax = 1;
	}

	int uniformMin = 2;

	if (lane_mask_min_0)
	{
		uniformMin = 0;
	}
	else if (lane_mask_min_1)
	{
		uniformMin = 1;
	}
#if OPTIMIZE_SUN_LIGHT_CACHE
	minCascadedlevel = uniformMin;
	maxCascadedLevel = uniformMax;
#endif

	float4 posVS = float4(positionVS, 1.0);

#if USE_FORCED_CASCADE
	int rightCascadedLevel = forcedCascadeIndex >= 0 ? forcedCascadeIndex : minCascadedlevel;
#else
	int rightCascadedLevel = minCascadedlevel;
#endif

	//if (minCascadedlevel == 1 && maxCascadedLevel == 1)
	//{
	//	return -maxValue;
	//}
	//
	//if (minCascadedlevel == 0 && maxCascadedLevel == 0)
	//{
	//	return -maxValue;
	//}

	//if (minCascadedlevel == 0 && maxCascadedLevel == 1)
	//{
	//	return -maxValue;
	//}

	//if (minCascadedlevel == 1 && maxCascadedLevel == 1)
	//{
	//	return maxValue;
	//}
	//else
	//{
	//	return -maxValue;
	//}

	float kUvScale = uvSqueeze;
	int i = rightCascadedLevel;

	float3 shadowCoords;
	float cascadedDist = cascadedLevelDistFloats[rightCascadedLevel];
	shadowCoords.x = dot(posVS, shadowMatAndTranspose[rightCascadedLevel * 6 + 0]);
	shadowCoords.y = dot(posVS, shadowMatAndTranspose[rightCascadedLevel * 6 + 1]);
	shadowCoords.z = rightCascadedLevel;

#if FROXEL_SUN_FILTER_PREV
	shadowCoords.x = 0.5 + (shadowCoords.x - 0.5) * kUvScale;
	shadowCoords.y = 0.5 + (shadowCoords.y - 0.5) * kUvScale;
#endif

	float rightDepthZ = txSunShadow.SampleLevel(samplePoint, shadowCoords, 0).r;

	float4 posSS0 = float4(shadowCoords.xy, rightDepthZ, 1.0f);

	float3 shadowPosVs0;

	bool inCascadeView = (shadowCoords.x >= 0 && shadowCoords.x <= 1.0 && shadowCoords.y >= 0 && shadowCoords.y <= 1.0);

	shadowPosVs0.x = dot(posSS0, shadowMatAndTranspose[rightCascadedLevel * 6 + 3]);
	shadowPosVs0.y = dot(posSS0, shadowMatAndTranspose[rightCascadedLevel * 6 + 4]);
	shadowPosVs0.z = dot(posSS0, shadowMatAndTranspose[rightCascadedLevel * 6 + 5]);

	float rightPosAlongLightDir = dot(lightDirectionVs, shadowPosVs0);

	float shadowPosVSZ = dot(float4(shadowCoords.xy, rightDepthZ, 1.0f), shadowMatAndTranspose[rightCascadedLevel * 6 + 5]);

	// we have to check all cascades that this line intersects with becuase position we are checking can be outside of frustum
	// meaning that just because it depth matches a particular cascade doesn't mean that cascade will actually have a value for it
	// if we check cascade and the shadow position for this ray is inside of that cascade, we are guaranteed that we can stop and we got the best result
	// if we get < 1.0 shadow and it is last cascade that is also the best result

	bool mustBeInNextCascade = false;
	bool bFound = (rightDepthZ < 1.0) && inCascadeView && (rightDepthZ > 0.0f || i == maxCascadedLevel);
	if (rightDepthZ == 0.0f)
	{
		mustBeInNextCascade = true;
	}

	//if (!bFound)
	//{
	//	return -maxValue;
	//}

	//if (rightCascadedLevel == 0 && 1 == maxCascadedLevel && bFound)
	//{
	//	bFound = true;
	//	return -maxValue;
	//
	//}

	//if (mustBeInNextCascade)
	//{
	//	bFound = true;
	//	return -maxValue;
	//
	//}

	//if (minCascadedlevel == 0 && maxCascadedLevel == 2)
	//{
	//	if (mustBeInNextCascade)
	//	{
	//		return maxValue;
	//	}
	//	else
	//	{
	//		return -maxValue;
	//	}
	//}
	//else
	//{
	//	return -maxValue;
	//}


	i++;
	float refShadowPosVSZ = shadowPosVSZ;

	while (i <= maxCascadedLevel && !bFound
#if USE_FORCED_CASCADE
		&& forcedCascadeIndex == -1
#endif
	)
	{
		cascadedDist = cascadedLevelDistFloats[i];
		shadowCoords.x = dot(posVS, shadowMatAndTranspose[i * 6 + 0]);
		shadowCoords.y = dot(posVS, shadowMatAndTranspose[i * 6 + 1]);
		shadowCoords.z = i;

#if FROXEL_SUN_FILTER_PREV
		shadowCoords.x = 0.5 + (shadowCoords.x - 0.5) * kUvScale;
		shadowCoords.y = 0.5 + (shadowCoords.y - 0.5) * kUvScale;
#endif

		float depthZ = txSunShadow.SampleLevel(samplePoint, shadowCoords, 0).r;

		float4 posSS = float4(shadowCoords.xy, depthZ, 1.0f);
		shadowPosVSZ = dot(posSS, shadowMatAndTranspose[i * 6 + 5]);

		inCascadeView = inCascadeView || (shadowCoords.x >= 0 && shadowCoords.x <= 1.0 && shadowCoords.y >= 0 && shadowCoords.y <= 1.0);

		float3 shadowPosVs;
		shadowPosVs.x = dot(posSS, shadowMatAndTranspose[i * 6 + 3]);
		shadowPosVs.y = dot(posSS, shadowMatAndTranspose[i * 6 + 4]);
		shadowPosVs.z = dot(posSS, shadowMatAndTranspose[i * 6 + 5]);

		float posAlongLightDir = dot(lightDirectionVs, shadowPosVs);

		// akovalovs: I'm not sure about this logic here. This is a copy of exisitng code
		// specifically I don't understand yet why we need  && shadowPosVSZ > refShadowPosVSZ

		//if (posAlongLightDir0)
		//{
		//	if (shadowPosVSZ < rightDepthZ)
		//	{
		//		rightDepthZ = shadowPosVSZ;
		//		rightCascadedLevel = i;
		//	}
		//}
		//else
		//{
		//	if (shadowPosVSZ > rightDepthZ)
		//	{
		//		rightDepthZ = shadowPosVSZ;
		//		rightCascadedLevel = i;
		//	}
		//}
		
		//if ((i == maxCascadedLevel || shadowPosVSZ <= cascadedDist))//  && shadowPosVSZ > refShadowPosVSZ)
		//{
		//	rightCascadedLevel = i;
		//	rightDepthZ = depthZ;
		//	refShadowPosVSZ = shadowPosVSZ;
		//	bFound = true;
		//}
		//else if (shadowPosVSZ < refShadowPosVSZ)
		//{
		//	bFound = true;
		//}
		if (mustBeInNextCascade && posAlongLightDir >= rightPosAlongLightDir)
		{
			//mustBeInNextCascade means we hit 0.0 depth at previous cascade.
			// if this cascade has a depth along light ray that is further, that means that we are missing an object in this cascade, that was there in other cascade
			// the best option we have is just use previous results.

			// keep results from before:
			// rightDepthZ
			// rightCascadedLevel
			bFound = true;

			if (posAlongLightDir == rightPosAlongLightDir)
			{
				//return -maxValue;
			}
		}
		else if (depthZ < 1.0 && (depthZ > 0.0f || i == maxCascadedLevel))
		{
			rightCascadedLevel = i;
			rightDepthZ = depthZ;
			bFound = true;
			break;
		}
		else
		{
			if (depthZ == 0.0f)
			{
				rightDepthZ = depthZ;
				rightCascadedLevel = i;
				rightPosAlongLightDir = posAlongLightDir;
				mustBeInNextCascade = true;
			}
		}

		i++;
	}

	//if (sunAwayFromCamera)
	//{
	//	return maxValue;
	//}
	//
	//return -maxValue;

	//if (!bFound && minCascadedlevel == 0 && maxCascadedLevel == 1)
	//{
	//	rightDepthZ = 1.0;
	//
	//	return maxValue;
	//}
	//
	//return -maxValue;

	if (bFound || mustBeInNextCascade)
	{
		float4 posSS = float4(shadowCoords.xy, rightDepthZ + shadowOffset, 1.0f);

		float3 shadowPosVS;
		shadowPosVS.x = dot(posSS, shadowMatAndTranspose[rightCascadedLevel * 6 + 3]);
		shadowPosVS.y = dot(posSS, shadowMatAndTranspose[rightCascadedLevel * 6 + 4]);
		shadowPosVS.z = dot(posSS, shadowMatAndTranspose[rightCascadedLevel * 6 + 5]);

		outFound = true;
		couldBeEarlier = mustBeInNextCascade || !inCascadeView;

		//if (!inCascadeView)
		//if (rightDepthZ == 1.0)
		//	return -maxValue;

		return shadowPosVS;

		//return -maxValue;
	}
	else
	{
		return sunBeyondEvaluation * maxValue;

		//if (inCascadeView)
		//	return -maxValue;
		//else
		//	return maxValue;
	}

	/*
	if (rightDepthZ == 1.0)
	{
	return maxValue;
	}
	else
	{
	float4 posSS = float4(shadowCoords.xy, rightDepthZ + shadowOffset, 1.0f);

	float3 shadowPosVS;
	shadowPosVS.x = dot(posSS, shadowMatAndTranspose[rightCascadedLevel * 6 + 3]);
	shadowPosVS.y = dot(posSS, shadowMatAndTranspose[rightCascadedLevel * 6 + 4]);
	shadowPosVS.z = dot(posSS, shadowMatAndTranspose[rightCascadedLevel * 6 + 5]);

	return shadowPosVS;
	}
	*/

}



float3 CalculateSunlightShadowRayDirCircle(
	int numCascadedLevels, float sunIntensity,
	float cascadedLevelDistFloats[8],
#if USE_FORCED_CASCADE
	int forcedCascadeIndex,
#else
	int unused,
#endif
	float4 shadowMatAndTranspose[kMaxNumCascaded * 6],
	Texture2DArray<float> txSunShadow,
	SamplerState samplePoint,
	float3 positionVS,
	float3 edgePositionVS,
	float bFromSun,
	float3 maxValue, float shadowOffset, out bool outFound, out bool couldBeEarlier, float sunBeyondEvaluation, float uvSqueeze)
{
	outFound = false;
	couldBeEarlier = false;
	if (numCascadedLevels == 0)
		return sunIntensity == 0 ? -maxValue : maxValue; // fully shadowed when intsntiy is 0

	int maxCascadedLevelIdx = numCascadedLevels - 1;

	int iEdgeCascadedLevel = GetCascadedLevel(cascadedLevelDistFloats, numCascadedLevels, edgePositionVS.z);


	ulong lane_mask_min_0 = __v_cmp_eq_u32(iEdgeCascadedLevel, 0);
	ulong lane_mask_min_1 = __v_cmp_eq_u32(iEdgeCascadedLevel, 1);
	ulong lane_mask_min_2 = __v_cmp_eq_u32(iEdgeCascadedLevel, 2);

	int uniformMin = 2;

	if (lane_mask_min_0)
	{
		uniformMin = 0;
	}
	else if (lane_mask_min_1)
	{
		uniformMin = 1;
	}

#if OPTIMIZE_SUN_LIGHT_CACHE
	int i = uniformMin;
#else
	int i = iEdgeCascadedLevel;
#endif


	float4 posVS = float4(positionVS, 1.0);

	bool bFound = false;
	bool mustBeInNextCascade = false;
	float3 shadowCoords = float3(0.0f, 0.0f, 0.0f);
	float depthZ = 0;
	//maxCascadedLevelIdx = 1;
	//i = 1;
	bool inCascadeView = false;

	while (i <= maxCascadedLevelIdx && !bFound)
	{
		#if OPTIMIZE_SUN_LIGHT_CACHE
		if (i >= iEdgeCascadedLevel)
		{
		#endif

		shadowCoords.x = dot(posVS, shadowMatAndTranspose[i * 6 + 0]);
		shadowCoords.y = dot(posVS, shadowMatAndTranspose[i * 6 + 1]);
		shadowCoords.z = i;

		float kUvScale = uvSqueeze;
#if FROXEL_SUN_FILTER_PREV
		shadowCoords.x = 0.5 + (shadowCoords.x - 0.5) * kUvScale;
		shadowCoords.y = 0.5 + (shadowCoords.y - 0.5) * kUvScale;
#endif
		inCascadeView = inCascadeView || (shadowCoords.x >= 0 && shadowCoords.x <= 1.0 && shadowCoords.y >= 0 && shadowCoords.y <= 1.0);

		depthZ = txSunShadow.SampleLevel(samplePoint, shadowCoords, 0).r;

		if (bFromSun)
		{
			if (depthZ < 1.0f)
				bFound = true;
			else
				i++;
		}
		else
		{
			// if we are looking at the sun there are a couple of possibilities.
			// first of all we traverse cascades closest to futhest
			// if we hit z > 0 that means we hit something inside of the cascade volume and there is no way we will hit something in next cascade
			// if we hit z == 0 it is possible we will hit something in next cascade. but if it is last cascade we are checking then it is the best result we will get
			if ((depthZ > 0.0f) || (depthZ == 0.0f && i == maxCascadedLevelIdx))
			{
				bFound = true;
				if (depthZ == 0.0f)
				{
					mustBeInNextCascade = true;
				}
			}
			else
			{
				if (depthZ == 0.0f)
				{
					mustBeInNextCascade = true;
				}
				i++;
			}
		}
		#if OPTIMIZE_SUN_LIGHT_CACHE
		}
		else
		{
			i++;
		}
		#endif

		
	}


	// return maxValue; - returns maximum z value = everything is lit

	//return maxValue;
	
	//bFound = false;
	/*
	if (bFound && i == 1)
	{
		//return maxValue;

		float4 posSS = float4(shadowCoords.xy, depthZ + shadowOffset, 1.0f);

		float3 shadowPosVS;
		shadowPosVS.x = dot(posSS, shadowMatAndTranspose[i * 6 + 3]);
		shadowPosVS.y = dot(posSS, shadowMatAndTranspose[i * 6 + 4]);
		shadowPosVS.z = dot(posSS, shadowMatAndTranspose[i * 6 + 5]);

		return shadowPosVS;
	}
	else
	{
		return -maxValue;
	}
	*/

	
	if (bFound == false)
	{
		i = maxCascadedLevelIdx;
		depthZ = 1.0;
	}

	if (depthZ == 1.0 && !mustBeInNextCascade)
	{
		outFound = false;
		return sunBeyondEvaluation * maxValue;
		//return maxValue;
	}
	else
	{
		float4 posSS = float4(shadowCoords.xy, depthZ + shadowOffset, 1.0f);

		float3 shadowPosVS;
		shadowPosVS.x = dot(posSS, shadowMatAndTranspose[i * 6 + 3]);
		shadowPosVS.y = dot(posSS, shadowMatAndTranspose[i * 6 + 4]);
		shadowPosVS.z = dot(posSS, shadowMatAndTranspose[i * 6 + 5]);
		outFound = true;
		couldBeEarlier =	mustBeInNextCascade || !inCascadeView;
		return shadowPosVS;
	}
}




[NUM_THREADS(64, 1, 1)] //
void CS_CreateShadowCacheSlices(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsCreateShadowCacheSrt *pSrt : S_SRT_DATA)
{
	int iAngleSlice = dispatchId.x;

	float angle = pSrt->m_startAngle + pSrt->m_angleStep * (iAngleSlice); // samwe as pixels, we are always in the middle of the slice
	//float angle = pSrt->m_startAngle + pSrt->m_angleStep * (iAngleSlice); // todo: do +0.5 offset. this is for debugging

	float sinAngle;
	float cosAngle;

	sincos(angle, sinAngle, cosAngle);

	float3 inSamplePlaneDirVs = normalize(cosAngle * pSrt->m_samplingPlaneZVs + sinAngle * pSrt->m_samplingPlaneXVs); // direction in sampling plane for this angle slice

	float3 lightDirectionVs = pSrt->m_lightDirectionVs;

	float3 anglePlane = normalize(cross(lightDirectionVs, inSamplePlaneDirVs));

	float2 refRayMinY, refRayMaxY;
	FindFrustumClipEdge(refRayMinY, refRayMaxY, anglePlane, pSrt->m_viewFrustumPlanes);

	bool lookingAlongTheSun = pSrt->m_lookingAtLight;

	if (lookingAlongTheSun)
	{
		if (dot(float3(refRayMinY, 1.0f), inSamplePlaneDirVs) > 0.0f)
		{
			float2 tempRefRay = refRayMinY;
			refRayMinY = refRayMaxY;
			refRayMaxY = tempRefRay;
		}
	}

	float3 normalizedZRefRay0 = float3(refRayMinY, 1.0f);
	float3 normalizedZRefRay1 = float3(refRayMaxY, 1.0f);

	float3 normalRefRay0 = normalize(normalizedZRefRay0);
	float3 normalRefRay1 = normalize(normalizedZRefRay1);

	// we will use this vector to sample shadow map
	float3 sampleDirVs = normalRefRay0;



	//TODO: I dont know what this code is for but somehow it would choose a different sample vector
	//float4 farClipPlane;
	//farClipPlane.xyz = normalize(cross(clipPlane, lightDirection));
	//farClipPlane.w = -dot(farClipPlane.xyz, normalRefRay0);
	//
	//if (dot(farClipPlane, float4(normalRefRay1, 1.0f)) * farClipPlane.w < 0.0f)
	//	newSampleDir = normalRefRay1;

	if (!lookingAlongTheSun && abs(dot(normalRefRay1, lightDirectionVs)) < abs(dot(normalRefRay0, lightDirectionVs)))
	{
		sampleDirVs = normalRefRay1;
	}

	// this vector is perpendicular to sunlight and move along the sample direction
	// we use this vector to find out from a voxel, where we are on sample vector
	// by projecting on reference vector and multiplying it by ratio of movement on reference vector and sample vector
	float3 referenceVectorVs = normalize(cross(lightDirectionVs, cross(sampleDirVs, lightDirectionVs)));
	float kMaxDistanceZVs = max(kShadowMapCacheDepth, pSrt->m_cascadedLevelDistFloats[kMaxNumCascaded - 1]); // todo: this could just be pSrt->m_cascadedLevelDistFloats[kMaxNumCascaded - 1]. It should yield better resolution, but also change performance a bit since we will calulcate all slices

	uint2 texDim;
	pSrt->m_volumetricsShadowCacheRW.GetDimensions(texDim.x, texDim.y);

	int kMaxDepthSteps = texDim.y;




	if (!lookingAlongTheSun)
	{
		float maxDistAlongSampleVector = kMaxDistanceZVs / sampleDirVs.z; // how much we need to move along sample direction to get to max distance along view z

		float3 sampleEndPositionVs = sampleDirVs * maxDistAlongSampleVector;

		float referenceEndDistance = dot(sampleEndPositionVs, referenceVectorVs);
		float sampleDirToReferenceDirRatio = maxDistAlongSampleVector / referenceEndDistance; // how much we move on sample vector when we move along reference vector

		pSrt->m_shadowSlicesRW[iAngleSlice].m_sampleDirectionVs = sampleDirVs;
		pSrt->m_shadowSlicesRW[iAngleSlice].m_logStep = maxDistAlongSampleVector / kMaxDepthSteps;

		pSrt->m_shadowSlicesRW[iAngleSlice].m_shadowMapReferenceVectorVs = referenceVectorVs;
		pSrt->m_shadowSlicesRW[iAngleSlice].m_sampleDirToReferenceDirRatio = sampleDirToReferenceDirRatio;

		pSrt->m_shadowSlicesRW[iAngleSlice].m_sliceVector0Vs = refRayMinY;
		pSrt->m_shadowSlicesRW[iAngleSlice].m_sliceVector1Vs = refRayMaxY;


		// now march through shadow map and collect all samples


		float3 sampleDirStep = sampleDirVs * maxDistAlongSampleVector / kMaxDepthSteps;

		for (int iDepthStep = 0; iDepthStep < kMaxDepthSteps * 0; ++iDepthStep)
		{
			float3 posVs = sampleDirStep * iDepthStep;

			//posVs.x += ((iDepthStep % 3) == 0) * 0.1f;
			//posVs.x += -((iDepthStep % 1) == 0) * 0.1f;

			float distToClipPlane = dot(referenceVectorVs, posVs);
			float viewZ0 = distToClipPlane / dot(referenceVectorVs.xyz, float3(refRayMinY.xy, 1.0f));
			float viewZ1 = distToClipPlane / dot(referenceVectorVs.xyz, float3(refRayMaxY.xy, 1.0f));

			// viewZ0 and viewZ1 are distances along the edge rays that are start and end point of a segment where this froxel lives
			float3 maxValue = lightDirectionVs.xyz  * (+1.0 / pSrt->m_expShadowMapMultiplier / 2);

			float3 shadowRayDir = CalculateSunlightShadowRayDir(pSrt->m_numCascadedLevels,
				pSrt->m_cascadedLevelDistFloats,
				pSrt->m_shadowMat,
				pSrt->m_shadowBuffer,
				pSrt->m_pointSampler,
				posVs,
				viewZ0, viewZ1, maxValue, 0);
			// shadowRayDir is position of shadow in view space
			// dotting it with light direction gives us distance along light direction to the shadow location. anything smaller than that is not in shadow

			// note dispatchThreadId.x == groupId.x, it is the slice index
			float shadowDistAlongRay = clamp(dot(shadowRayDir, lightDirectionVs.xyz), -1.0 / pSrt->m_expShadowMapMultiplier / 2, 1.0 / pSrt->m_expShadowMapMultiplier / 2);

			float shadowExpValue = shadowDistAlongRay * pSrt->m_expShadowMapMultiplier * 2.0;
			shadowExpValue = min(pSrt->m_shadowDistOffset, shadowExpValue);

			float expMap = exp(shadowExpValue * pSrt->m_expShadowMapConstant);
			#if CACHED_SHADOW_EXP_BASE_2
				expMap = exp2(shadowExpValue * pSrt->m_expShadowMapConstant);
			#endif
			#if FROXELS_DEBUG_IN_SHADOW_CACHE_TEXTURE
				pSrt->m_volumetricsShadowCacheRW[int2(iAngleSlice, iDepthStep)] = float4(shadowDistAlongRay, expMap, shadowExpValue * pSrt->m_expShadowMapConstant, 1);
			#else
				pSrt->m_volumetricsShadowCacheRW[int2(iAngleSlice, iDepthStep)] = float2(shadowDistAlongRay, expMap);
			#endif
		}
	}
	else
	{
		// these points are start and end of the slice intersection with shadow map
		float3 startPos = normalizedZRefRay0 * pSrt->m_cascadedLevelDistFloats[kMaxNumCascaded - 1];
		float3 endPos = normalizedZRefRay1 * pSrt->m_cascadedLevelDistFloats[kMaxNumCascaded - 1];

		float3 sampleRay = endPos - startPos;
		float sampleRayLen = length(sampleRay);
		float3 sampleRayNrm = sampleRay / sampleRayLen;
		float3 clipVector = normalize(cross(lightDirectionVs, cross(sampleRayNrm, lightDirectionVs)));

		float startLogDist = dot(startPos, clipVector);
		float endLogDist = dot(endPos, clipVector);
		float stepLogDist = (endLogDist - startLogDist) / kMaxDepthSteps;

		pSrt->m_shadowSlicesRW[iAngleSlice].m_sampleDirectionVs = startPos;
		pSrt->m_shadowSlicesRW[iAngleSlice].m_logStep = startLogDist;

		pSrt->m_shadowSlicesRW[iAngleSlice].m_shadowMapReferenceVectorVs = clipVector;
		pSrt->m_shadowSlicesRW[iAngleSlice].m_sampleDirToReferenceDirRatio = stepLogDist;

		pSrt->m_shadowSlicesRW[iAngleSlice].m_sliceVector0Vs = sampleRayNrm.xy;
		pSrt->m_shadowSlicesRW[iAngleSlice].m_sliceVector1Vs = float2(sampleRayNrm.z, sampleRayLen);


		for (int iDepthStep = 0; iDepthStep < kMaxDepthSteps * 0; ++iDepthStep)
		{
			float distToStart = (stepLogDist * (iDepthStep/* + 0.5f*/) + startLogDist);
			// how much along the sampling vector we are
			float t0 = (distToStart - dot(startPos, clipVector)) / dot(sampleRayNrm.xyz, clipVector);
			float3 positionVS = sampleRayNrm.xyz * t0 + startPos;

			float distToClip = dot(positionVS, clipVector);

			float3 edgePositionVS = startPos;
			if (distToClip * sampleRayLen > 0)
				edgePositionVS += sampleRayNrm.xyz * sampleRayLen;

			float t = distToClip / dot(edgePositionVS, clipVector);
			
			float3 maxValue = lightDirectionVs.xyz  * (+1.0 / pSrt->m_expShadowMapMultiplier / 2);
			bool found = true;
			bool couldBeEarlier = false;
			float3 shadowRayDir = CalculateSunlightShadowRayDirCircle(pSrt->m_numCascadedLevels, pSrt->m_lightColorIntensity.a,
				pSrt->m_cascadedLevelDistFloats,
				pSrt->m_forcedCascadeIndex,
				pSrt->m_shadowMat,
				pSrt->m_shadowBuffer,
				pSrt->m_pointSampler,

				positionVS,
				edgePositionVS * t,
				lightDirectionVs.z > 0.0f,
				maxValue, 0, found, couldBeEarlier, pSrt->m_sunBeyondEvaluation, pSrt->m_uvSqueeze);

			float shadowDistAlongRay = clamp(dot(shadowRayDir, lightDirectionVs.xyz), -1.0 / pSrt->m_expShadowMapMultiplier / 2, 1.0 / pSrt->m_expShadowMapMultiplier / 2);

			float shadowExpValue = shadowDistAlongRay * pSrt->m_expShadowMapMultiplier * 2.0;

			shadowExpValue = min(pSrt->m_shadowDistOffset, shadowExpValue);

			float expMap = exp(shadowExpValue * pSrt->m_expShadowMapConstant);
			#if CACHED_SHADOW_EXP_BASE_2
				expMap = exp2(shadowExpValue * pSrt->m_expShadowMapConstant);
			#endif
			#if FROXELS_DEBUG_IN_SHADOW_CACHE_TEXTURE
				pSrt->m_volumetricsShadowCacheRW[int2(iAngleSlice, iDepthStep)] = float4(shadowDistAlongRay, expMap, shadowExpValue * pSrt->m_expShadowMapConstant, 1);
			#else
				pSrt->m_volumetricsShadowCacheRW[int2(iAngleSlice, iDepthStep)] = float2(shadowDistAlongRay, expMap);
			#endif

			//pSrt->m_volumetricsShadowCacheRW[int2(iAngleSlice, iDepthStep)] = float4(shadowDistAlongRay, positionVS.xyz);
		}
	}
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
	float startAngle,
	float angleStepInv,
	float numAngleSlicesMinus1,
	uint lookingAtLight,
	float3 camPos,

	// same for different frames
	float shadowMaxDistAlongLightDir,
	float nearBrightness,
	float nearBrightnessDistSqr,
	float expShadowMapConstant,
	float expShadowMapMultiplier,

	float3 positionVS,
	float3 posWs,
	float inScatterFactor,
	bool haveFallback,
	float fallBackValue,
	// these are not used, but were useful before

	inout float densityAdd,

	// results
	inout float brightness, inout float expValue, inout float resShadowPosAlongLightDir
)
{
	{
		//densityAdd = densityAdd / (1.0 + iSlice / 4.0) ;
		densityAdd = 0;// max(densityAdd - 1, 0);


		float3 N = normalize(positionVS);

		float2 cosSinRefAngle = normalize(float2(dot(N, samplingPlaneZVs), dot(N, samplingPlaneXVs)));

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


		float yCoord = (shadowDepthStep /* + 0.5*/) / float(texDim.y);
		float xCoord = (iAngleSlice + 0.5f) / float(texDim.x);

		float shadowPosAlongLightDir = volumetricsShadowCacheRO.SampleLevel(pointSampler, float2(xCoord, yCoord), 0).r;
		uint2 shadowCoords = uint2(iAngleSlice, shadowDepthStep);
		//float shadowPosAlongLightDir = pSrt->m_volumetricsShadowCacheRO[shadowCoords].r;


		resShadowPosAlongLightDir = shadowPosAlongLightDir;

		//float expMap = pSrt->m_volumetricsShadowCacheRO.SampleLevel(pSrt->m_pointSampler, float2(xCoord, yCoord), 0).g;
		//float shadowPosAlongLightDir = pSrt->m_volumetricsShadowCacheRO.SampleLevel(pSrt->m_linearSampler, float2(xCoord, yCoord), 0);
		float expMap = volumetricsShadowCacheRO.SampleLevel(linearSampler, float2(xCoord, yCoord), 0).g;

		expValue = expMap;

		if (haveFallback)
		{
			if (false || shadowDepthStep < (0.5 /* + 0.5*/) || iAngleSlice < (0 /* + 1*/) || iAngleSlice > (texDim.x - 1 /* - 1*/))
			{
				resShadowPosAlongLightDir = fallBackValue;
			}
		}

		float pointPosAlongLightDir = dot(positionVS, lightDirectionVs);

		float alwaysLit = saturate((posWs.y - shadowMaxDistAlongLightDir) / 8.0);
		float xzDistSqr = dot((posWs - camPos).xz, (posWs - camPos).xz);
		float nearDistSqr = nearBrightnessDistSqr;
		float sunLightNearBoost = lerp(1.0, nearBrightness, saturate((nearDistSqr - xzDistSqr) / 32.0));

		// normal test:
		float dif = shadowPosAlongLightDir - pointPosAlongLightDir;

		pointPosAlongLightDir = clamp(pointPosAlongLightDir, -1.0 / (expShadowMapMultiplier * 2.0), 1.0 / (expShadowMapMultiplier * 2.0));

		float pointPosAlongLightDirForExp = pointPosAlongLightDir * (expShadowMapMultiplier * 2.0); // -1.0 .. 1.0 range

#if FROXELS_ALWAYS_REVERSE_SHADOWS_FOR_SUN_LIGHT
		pointPosAlongLightDirForExp = -pointPosAlongLightDirForExp;
#endif

																									// exp map test
		float expVisibility = clamp(expMap * exp(-pointPosAlongLightDirForExp * expShadowMapConstant), 0.0f, 1.0f) * sunLightNearBoost;
#if CACHED_SHADOW_EXP_BASE_2
		expVisibility = clamp(expMap * exp2(-pointPosAlongLightDirForExp * expShadowMapConstant), 0.0f, 1.0f) * sunLightNearBoost;
#endif

		#if FROXELS_ALWAYS_REVERSE_SHADOWS_FOR_SUN_LIGHT
			expVisibility = 1.0 - expVisibility;
		#endif

		expVisibility *= expVisibility; // get a tighter curve

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



[NUM_THREADS(64, 1, 1)] //
void CS_CreateShadowCacheSliceData(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsCreateShadowCacheSrt *pSrt : S_SRT_DATA)
{
	int iAngleSlice = dispatchId.x;
	int iDepthStep = groupId.y;

	//iDepthStep = groupId.x;
	//iAngleSlice = groupThreadId.x + groupId.y * 64;

	float3 lightDirectionVs = pSrt->m_lightDirectionVs;

	bool lookingAlongTheSun = pSrt->m_lookingAtLight;

	uint2 texDim;
	pSrt->m_volumetricsShadowCacheRW.GetDimensions(texDim.x, texDim.y);

	int kMaxDepthSteps = texDim.y;


	float2 refRayMinY = pSrt->m_shadowSlicesRO[iAngleSlice].m_sliceVector0Vs;
	float2 refRayMaxY = pSrt->m_shadowSlicesRO[iAngleSlice].m_sliceVector1Vs;

	//iDepthStep
	int kNumSubSteps = 4;
		
#define SHADOW_START_VALUE -100000000.0
#define SHADOW_OP(a, b) max(a, b)


	float prevExpValue = 0;
	float prevBrightness = 0;
	float densityAdd = 0;
	float inScatterFactor = 1;
	#if FROXLES_AWAY_FROM_SUN
	lookingAlongTheSun = false;
	#endif
	#if FROXELS_TOWARDS_SUN
	lookingAlongTheSun = true;
	#endif
	float kMaxDistAlongRay = 1.0 / pSrt->m_expShadowMapMultiplier / 2;
	float kMaxDistAlongRayPrevAllowed = kMaxDistAlongRay * 0.9;

	if (!lookingAlongTheSun)
	{
		
		float3 referenceVectorVs = pSrt->m_shadowSlicesRO[iAngleSlice].m_shadowMapReferenceVectorVs;
	
		// now march through shadow map and collect all samples

		float3 sampleDirVs = pSrt->m_shadowSlicesRO[iAngleSlice].m_sampleDirectionVs;

		float3 sampleDirStep = sampleDirVs * pSrt->m_shadowSlicesRO[iAngleSlice].m_logStep;

		// previous value
		float stepCenter = iDepthStep + 0.5f;
		float3 posVsCenter = sampleDirStep * stepCenter;
		float3 prevPosVs_Center = mul(float4(posVsCenter, 1), pSrt->m_mVInvxLastFrameV).xyz;

		float4 prevPosH = mul(float4(prevPosVs_Center, 1), pSrt->m_mPLastFrame);
		float3 prevPosNdc = prevPosH.xyz / prevPosH.w;

		bool prevPosInView = abs(prevPosNdc.x) < 1.0 && abs(prevPosNdc.y) < 1.0 && prevPosNdc.z > 0.0;

		float4 posWsCenterH = mul(float4(posVsCenter, 1), pSrt->m_mVInv);
		float3 posWsCenter = posWsCenterH.xyz;// / posWsH.w;

		
		// results
		float resExpMap;
		float resShadowDistAlongRay = SHADOW_START_VALUE;
		float jitOffset = (0.5 / kNumSubSteps) * (pSrt->m_jitterCounter % 2);

		

		bool found = true;

		if (iDepthStep == 8 && iAngleSlice == 7)
		{
			//_SCE_BREAK();
		}
		
		
		for (int iSubStep = 0; (iSubStep < kNumSubSteps) && found	; ++iSubStep)
		{
			float step = iDepthStep + 0.5f;
			step = iDepthStep + float(iSubStep) / kNumSubSteps - 0.5 + (1.0 - saturate(iDepthStep / 8.0)) * 2.0;
			step += jitOffset;
			float3 posVs = sampleDirStep * step;

			//posVs.x += ((iDepthStep % 3) == 0) * 0.1f;
			//posVs.x += -((iDepthStep % 1) == 0) * 0.1f;

			float distToClipPlane = dot(referenceVectorVs, posVs);
			float viewZ0 = distToClipPlane / dot(referenceVectorVs.xyz, float3(refRayMinY.xy, 1.0f));
			float viewZ1 = distToClipPlane / dot(referenceVectorVs.xyz, float3(refRayMaxY.xy, 1.0f));

			// viewZ0 and viewZ1 are distances along the edge rays that are start and end point of a segment where this froxel lives
			float3 maxValue = lightDirectionVs.xyz  * (+1.0 / pSrt->m_expShadowMapMultiplier / 2);
			bool couldBeEarlier = false;
			float3 shadowRayDir = CalculateSunlightShadowRayDirNew(pSrt->m_numCascadedLevels, pSrt->m_lightColorIntensity.a,
				pSrt->m_cascadedLevelDistFloats,
				pSrt->m_forcedCascadeIndex,
				pSrt->m_shadowMat,
				pSrt->m_shadowBuffer,
				pSrt->m_pointSampler,
				lightDirectionVs,
				posVs,
				viewZ0, viewZ1, maxValue, 0, found, couldBeEarlier, pSrt->m_sunBeyondEvaluation, pSrt->m_uvSqueeze);

			#if ENABLE_SUN_SNAPSHOT
			// if not found, we can check the static shadow too
			ShadowMapSetup hSetupVs = pSrt->m_shadowSetupVs;


			if ( (!found || couldBeEarlier) && hSetupVs.m_corner.x > -1000000.0f)
			{
				//shadowRayDir = maxValue;
				//z = 1.0 - z;

				float u = dot(posVs - hSetupVs.m_corner, hSetupVs.m_x);
				float v = dot(posVs - hSetupVs.m_corner, hSetupVs.m_z);
				float z = dot(posVs - hSetupVs.m_corner, hSetupVs.m_y);


				if (u < 0 || u > 1 || v < 0 || v > 1)// || z < 0 || z > 1)
				{
				}
				else
				{

					float shadowValue = pSrt->m_shadowMap.SampleLevel(pSrt->m_pointSampler, float2(u, v), 0).r;

					shadowValue = 1.0 - shadowValue;

					// now convert back to WS
					ShadowMapSetup hSetupInvVs = pSrt->m_shadowSetupInvVs;

					
					float3 shadowPosVs = hSetupVs.m_corner + hSetupInvVs.m_x * u + hSetupInvVs.m_z * v + hSetupInvVs.m_y * shadowValue;

					shadowRayDir = shadowPosVs;

					//brightness = z > shadowValue;
					
				}

			}
			#endif

			// shadowRayDir is position of shadow in view space
			// dotting it with light direction gives us distance along light direction to the shadow location. anything smaller than that is not in shadow


			// make sure clamp sampler grabs max value at the edge of sampling this texture
			if (iDepthStep == kMaxDepthSteps - 1)
			{
				shadowRayDir = maxValue;
			}

			if (iDepthStep <= 1)
			{
				shadowRayDir = -maxValue;
			}
			//if (iAngleSlice <= 1)
			//{
			//	shadowRayDir = -maxValue;
			//}

			// note dispatchThreadId.x == groupId.x, it is the slice index
			float shadowDistAlongRay = clamp(dot(shadowRayDir, lightDirectionVs.xyz), -1.0 / pSrt->m_expShadowMapMultiplier / 2, 1.0 / pSrt->m_expShadowMapMultiplier / 2);

			resShadowDistAlongRay = SHADOW_OP(resShadowDistAlongRay, shadowDistAlongRay);
		}

		float currentFrameShadowDistAlongRay = resShadowDistAlongRay;


		float prevShadowPosAlongLightDir = currentFrameShadowDistAlongRay;

		if (found && prevPosInView)
		{
			DoShadowCacheLighting(
				// data that changes for prev frame
				pSrt->m_shadowSlicesPrevRO, pSrt->m_volumetricsShadowCachePrevRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
				pSrt->m_samplingPlaneZVsPrev, pSrt->m_samplingPlaneXVsPrev, pSrt->m_lightDirectionVsPrev, pSrt->m_startAnglePrev, pSrt->m_angleStepInvPrev, pSrt->m_numAngleSlicesMinus1Prev, pSrt->m_lookingAtLightPrev, pSrt->m_camPosPrev,
				// same for different frames
				/*pSrt->m_shadowMaxDistAlongLightDir*/10000000.0, /*pSrt->m_nearBrightness*/ 0, /*pSrt->m_nearBrightnessDistSqr*/ 0, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier,

				//positionVS, 
				prevPosVs_Center,
				posWsCenter, inScatterFactor, true, currentFrameShadowDistAlongRay, densityAdd, prevBrightness, prevExpValue, prevShadowPosAlongLightDir);

			//prevShadowPosAlongLightDir = prevPosInView ? prevShadowPosAlongLightDir : currentFrameShadowDistAlongRay;
		}

		#if FROXEL_SUN_FILTER_PREV
		if (prevShadowPosAlongLightDir < kMaxDistAlongRayPrevAllowed)
		#endif
		{
			resShadowDistAlongRay = pSrt->m_shadowConvergeFactor > 0.0 ? SHADOW_OP(resShadowDistAlongRay, prevShadowPosAlongLightDir) : resShadowDistAlongRay;
		}


		float3 shadowPointPosVsCenter = posVsCenter - dot(posVsCenter, lightDirectionVs.xyz) * lightDirectionVs.xyz + lightDirectionVs.xyz * resShadowDistAlongRay;
		float viewDistZ = shadowPointPosVsCenter.z;
		float froxelSliceFloat0 = CameraLinearDepthToFroxelZSliceExpWithOffset(viewDistZ, 0 /*pSrt->m_runtimeGridOffset*/);
		float oneSliceDepth = DepthDerivativeAtSlice(froxelSliceFloat0);
		float3 toFroxelVs = normalize(shadowPointPosVsCenter);
		float kRange = pSrt->m_smartOffsetAngleStart;
		float alongTheSun = saturate((-dot(toFroxelVs, lightDirectionVs.xyz) - kRange) * pSrt->m_smartOffsetAngleRangeInv);

		alongTheSun = pow(alongTheSun, 0.5);


		resShadowDistAlongRay -= alongTheSun * (oneSliceDepth * pSrt->m_smartShadowOffsetFroxelDepth + pSrt->m_smartShadowOffsetStatic);

		float shadowExpValue = resShadowDistAlongRay * pSrt->m_expShadowMapMultiplier * 2.0; // -1.0 .. 1.0
		//shadowExpValue = min(pSrt->m_shadowDistOffset, shadowExpValue);
#if FROXELS_ALWAYS_REVERSE_SHADOWS_FOR_SUN_LIGHT
		shadowExpValue = -shadowExpValue;
		shadowExpValue = shadowExpValue + pSrt->m_shadowDistOffset;
#else
		shadowExpValue = shadowExpValue - pSrt->m_shadowDistOffset;
#endif

		float expMap = exp(shadowExpValue * pSrt->m_expShadowMapConstant);
#if CACHED_SHADOW_EXP_BASE_2
		expMap = exp2(shadowExpValue * pSrt->m_expShadowMapConstant);
#endif

		resExpMap = expMap;

		//resExpMap = lerp(prevExpValue, resExpMap, pSrt->m_shadowConvergeFactor);

		#if FROXELS_DEBUG_IN_SHADOW_CACHE_TEXTURE
			pSrt->m_volumetricsShadowCacheRW[int2(iAngleSlice, iDepthStep)] = float4(shadowDistAlongRay, expMap, shadowExpValue * pSrt->m_expShadowMapConstant, 1);
		#else
			pSrt->m_volumetricsShadowCacheRW[int2(iAngleSlice, iDepthStep)] = float2(currentFrameShadowDistAlongRay, resExpMap);
		#endif
	}
	else
	{
		float3 normalizedZRefRay0 = float3(refRayMinY, 1.0f);
		float3 normalizedZRefRay1 = float3(refRayMaxY, 1.0f);


		float stepLogDist = pSrt->m_shadowSlicesRO[iAngleSlice].m_sampleDirToReferenceDirRatio;
		float startLogDist = pSrt->m_shadowSlicesRO[iAngleSlice].m_logStep;

		float3 clipVector = pSrt->m_shadowSlicesRO[iAngleSlice].m_shadowMapReferenceVectorVs;
		float3 startPos = pSrt->m_shadowSlicesRO[iAngleSlice].m_sampleDirectionVs;

		float3 sampleRayNrm = float3(pSrt->m_shadowSlicesRO[iAngleSlice].m_sliceVector0Vs.xy, pSrt->m_shadowSlicesRO[iAngleSlice].m_sliceVector1Vs.x);

		float sampleRayLen = pSrt->m_shadowSlicesRO[iAngleSlice].m_sliceVector1Vs.y;

		// previous value

		float stepCenter = iDepthStep + 0.5f;

		float distToStartCenter = (stepLogDist * (stepCenter/* + 0.5f*/)+startLogDist);
		// how much along the sampling vector we are
		float tC = (distToStartCenter - dot(startPos, clipVector)) / dot(sampleRayNrm.xyz, clipVector);
		float3 posVsCenter = sampleRayNrm.xyz * tC + startPos;
		float3 prevPosVs_Center = mul(float4(posVsCenter, 1), pSrt->m_mVInvxLastFrameV).xyz;

		float4 posWsCenterH = mul(float4(posVsCenter, 1), pSrt->m_mVInv);
		float3 posWsCenter = posWsCenterH.xyz;// / posWsH.w;
		
		float4 prevPosH = mul(float4(prevPosVs_Center, 1), pSrt->m_mPLastFrame);
		float3 prevPosNdc = prevPosH.xyz / prevPosH.w;

		bool prevPosInView = abs(prevPosNdc.x) < 1.0 && abs(prevPosNdc.y) < 1.0 && prevPosNdc.z > 0.0;


		// results
		float resExpMap;
		float resShadowDistAlongRay = SHADOW_START_VALUE;
		float jitOffset = (0.5 / kNumSubSteps) * (pSrt->m_jitterCounter % 2);

		for (int iSubStep = 0; iSubStep < kNumSubSteps; ++iSubStep)
		{
			float step = iDepthStep + 0.5f;
			step = iDepthStep + float(iSubStep) / kNumSubSteps - 0.5;
			step += jitOffset;

			float distToStart = (stepLogDist * (step/* + 0.5f*/)+startLogDist);
			// how much along the sampling vector we are
			float t0 = (distToStart - dot(startPos, clipVector)) / dot(sampleRayNrm.xyz, clipVector);
			float3 positionVS = sampleRayNrm.xyz * t0 + startPos;

			float distToClip = dot(positionVS, clipVector);

			float3 edgePositionVS = startPos;
			if (distToClip * sampleRayLen > 0)
				edgePositionVS += sampleRayNrm.xyz * sampleRayLen;

			float t = distToClip / dot(edgePositionVS, clipVector);
			
			float3 maxValue = lightDirectionVs.xyz  * (+1.0 / pSrt->m_expShadowMapMultiplier / 2);

			bool found = true;
			bool couldBeEarlier = false;
			float3 shadowRayDir = CalculateSunlightShadowRayDirCircle(pSrt->m_numCascadedLevels, pSrt->m_lightColorIntensity.a,
				pSrt->m_cascadedLevelDistFloats,
				pSrt->m_forcedCascadeIndex,
				pSrt->m_shadowMat,
				pSrt->m_shadowBuffer,
				pSrt->m_pointSampler,

				positionVS,
				edgePositionVS * t,
				lightDirectionVs.z > 0.0f,
				maxValue, 0, found, couldBeEarlier, pSrt->m_sunBeyondEvaluation, pSrt->m_uvSqueeze);


			// if not found, we can check the static shadow too
			#if ENABLE_SUN_SNAPSHOT
			ShadowMapSetup hSetupVs = pSrt->m_shadowSetupVs;


			if ((!found || couldBeEarlier	) &&	hSetupVs.m_corner.x > -1000000.0f)
			{
				shadowRayDir = maxValue;
				//z = 1.0 - z;

				float u = dot(positionVS - hSetupVs.m_corner, hSetupVs.m_x);
				float v = dot(positionVS - hSetupVs.m_corner, hSetupVs.m_z);
				float z = dot(positionVS - hSetupVs.m_corner, hSetupVs.m_y);


				if (u < 0 || u > 1 || v < 0 || v > 1)// || z < 0 || z > 1)
				{
				}
				else
				{

					float shadowValue = pSrt->m_shadowMap.SampleLevel(pSrt->m_pointSampler, float2(u, v), 0).r;

					shadowValue = 1.0 - shadowValue;

					// now convert back to WS
					ShadowMapSetup hSetupInvVs = pSrt->m_shadowSetupInvVs;


					float3 shadowPosVs = hSetupVs.m_corner + hSetupInvVs.m_x * u + hSetupInvVs.m_z * v + hSetupInvVs.m_y * shadowValue;

					shadowRayDir = shadowPosVs;

					//brightness = z > shadowValue;

				}

			}
			#endif

			float shadowDistAlongRay = clamp(dot(shadowRayDir, lightDirectionVs.xyz), -1.0 / pSrt->m_expShadowMapMultiplier / 2, 1.0 / pSrt->m_expShadowMapMultiplier / 2); // clamp to -100 .. 100

			//shadowDistAlongRay = maxValue;

			
			resShadowDistAlongRay = SHADOW_OP(resShadowDistAlongRay, shadowDistAlongRay);
		}


		float currentFrameShadowDistAlongRay = resShadowDistAlongRay;
	
		float prevShadowPosAlongLightDir;

		DoShadowCacheLighting(
			// data that changes for prev frame
			pSrt->m_shadowSlicesPrevRO, pSrt->m_volumetricsShadowCachePrevRO, pSrt->m_linearSampler, pSrt->m_pointSampler,
			pSrt->m_samplingPlaneZVsPrev, pSrt->m_samplingPlaneXVsPrev, pSrt->m_lightDirectionVsPrev, pSrt->m_startAnglePrev, pSrt->m_angleStepInvPrev, pSrt->m_numAngleSlicesMinus1Prev, pSrt->m_lookingAtLightPrev, pSrt->m_camPosPrev,
			// same for different frames
			/*pSrt->m_shadowMaxDistAlongLightDir*/10000000.0, /*pSrt->m_nearBrightness*/ 0, /*pSrt->m_nearBrightnessDistSqr*/ 0, pSrt->m_expShadowMapConstant, pSrt->m_expShadowMapMultiplier,

			//positionVS, 
			prevPosVs_Center,
			posWsCenter, inScatterFactor, true, currentFrameShadowDistAlongRay, densityAdd, prevBrightness, prevExpValue, prevShadowPosAlongLightDir);
		
		prevShadowPosAlongLightDir = prevPosInView ? prevShadowPosAlongLightDir : currentFrameShadowDistAlongRay;

		#if FROXEL_SUN_FILTER_PREV
		if (prevShadowPosAlongLightDir < kMaxDistAlongRayPrevAllowed)
		#endif
		{
			resShadowDistAlongRay = pSrt->m_shadowConvergeFactor > 0.0 ? SHADOW_OP(resShadowDistAlongRay, prevShadowPosAlongLightDir) : resShadowDistAlongRay;
		}

		float3 shadowPointPosVsCenter = posVsCenter - dot(posVsCenter, lightDirectionVs.xyz) * lightDirectionVs.xyz + lightDirectionVs.xyz * resShadowDistAlongRay;
		float viewDistZ = shadowPointPosVsCenter.z;
		float froxelSliceFloat0 = CameraLinearDepthToFroxelZSliceExpWithOffset(viewDistZ, 0 /*pSrt->m_runtimeGridOffset*/);
		float oneSliceDepth = DepthDerivativeAtSlice(froxelSliceFloat0);
		float3 toFroxelVs = normalize(shadowPointPosVsCenter);
		float kRange = pSrt->m_smartOffsetAngleStart;
		float alongTheSun = saturate((-dot(toFroxelVs, lightDirectionVs.xyz) - kRange) * pSrt->m_smartOffsetAngleRangeInv);

		alongTheSun = pow(alongTheSun, 0.5);

		resShadowDistAlongRay -= alongTheSun * oneSliceDepth * pSrt->m_smartShadowOffsetFroxelDepth + pSrt->m_smartShadowOffsetStatic;

		float shadowExpValue = resShadowDistAlongRay * pSrt->m_expShadowMapMultiplier * 2.0; // to -1.0 .. 1.0

		//shadowExpValue = min(pSrt->m_shadowDistOffset, shadowExpValue);
#if FROXELS_ALWAYS_REVERSE_SHADOWS_FOR_SUN_LIGHT
		shadowExpValue = -shadowExpValue;
		shadowExpValue = shadowExpValue + pSrt->m_shadowDistOffset;
#else
	//shadowExpValue = min(pSrt->m_shadowDistOffset, shadowExpValue);
		shadowExpValue = shadowExpValue - pSrt->m_shadowDistOffset;
#endif
		//shadowExpValue = shadowExpValue - pSrt->m_shadowDistOffset;

		float expMap = exp(shadowExpValue * pSrt->m_expShadowMapConstant);
#if CACHED_SHADOW_EXP_BASE_2
		expMap = exp2(shadowExpValue * pSrt->m_expShadowMapConstant);
#endif

		resExpMap = expMap;

		//float resExpMapLerped = pSrt->m_shadowConvergeFactor == 0 ? prevExpValue : (pSrt->m_shadowConvergeFactor < 1.0 ? lerp(prevExpValue, resExpMap, pSrt->m_shadowConvergeFactor) : resExpMap);

#if FROXELS_DEBUG_IN_SHADOW_CACHE_TEXTURE
		pSrt->m_volumetricsShadowCacheRW[int2(iAngleSlice, iDepthStep)] = float4(shadowDistAlongRay, expMap, shadowExpValue * pSrt->m_expShadowMapConstant, 1);
#else
		pSrt->m_volumetricsShadowCacheRW[int2(iAngleSlice, iDepthStep)] = float2(currentFrameShadowDistAlongRay, resExpMap);
		//pSrt->m_volumetricsShadowCacheRW[int2(iAngleSlice, iDepthStep)] = float2(resExpMap, resExpMapLerped);
#endif

		//pSrt->m_volumetricsShadowCacheRW[int2(iAngleSlice, iDepthStep)] = float4(shadowDistAlongRay, positionVS.xyz);

	} // if looking at the sun
}



[NUM_THREADS(8, 8, 1)] //
void CS_ShadowCacheSlicesBlurX(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsCreateShadowCacheSrt *pSrt : S_SRT_DATA)
{

	uint2 pos = dispatchId.xy;

	float2 val0  = pSrt->m_volumetricsShadowCacheRW[pos].xy;
	float2 valM1 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x - 1, pos.y)].xy;
	float2 valP1 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x + 1, pos.y)].xy;
	float2 valM2 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x - 2, pos.y)].xy;
	float2 valP2 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x + 2, pos.y)].xy;
	float2 valM3 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x - 3, pos.y)].xy;
	float2 valP3 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x + 3, pos.y)].xy;

	float2 res = val0 * 0.4 + valM1 * 0.2 + valP1 * 0.2 + valM2 * 0.1 + valP2 * 0.1 ;

	//float4 res = val0 * 0.2 + valM1 * 0.2 + valP1 * 0.2 + valM2 * 0.1 + valP2 * 0.1 + valM2 * 0.1 + valP2 * 0.1;

	pSrt->m_volumetricsShadowCacheTempRW[pos].g = res.g;

}


[NUM_THREADS(8, 8, 1)] //
void CS_ShadowCacheSlicesBlurY(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsCreateShadowCacheSrt *pSrt : S_SRT_DATA)
{

	uint2 pos = dispatchId.xy;

	float2 val0  = pSrt->m_volumetricsShadowCacheTempRW[pos].xy;
	float2 valM1 = pSrt->m_volumetricsShadowCacheTempRW[uint2(pos.x, pos.y - 1)].xy;
	float2 valP1 = pSrt->m_volumetricsShadowCacheTempRW[uint2(pos.x, pos.y + 1)].xy;
	float2 valM2 = pSrt->m_volumetricsShadowCacheTempRW[uint2(pos.x, pos.y - 2)].xy;
	float2 valP2 = pSrt->m_volumetricsShadowCacheTempRW[uint2(pos.x, pos.y + 2)].xy;

	float2 res = val0 * 0.4 + valM1 * 0.2 + valP1 * 0.2 + valM2 * 0.1 + valP2 * 0.1;

	pSrt->m_volumetricsShadowCacheRW[pos].g = res.g;

}

[NUM_THREADS(8, 8, 1)] //
void CS_ShadowCacheSlicesExaggerateX(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsCreateShadowCacheSrt *pSrt : S_SRT_DATA)
{

	uint2 pos = dispatchId.xy;

	float2 val0 = pSrt->m_volumetricsShadowCacheRW[pos].xy;
	float2 valM1 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x - 1, pos.y)].xy;
	float2 valP1 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x + 1, pos.y)].xy;
	float2 valM2 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x - 2, pos.y)].xy;
	float2 valP2 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x + 2, pos.y)].xy;
	float2 valM3 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x - 3, pos.y)].xy;
	float2 valP3 = pSrt->m_volumetricsShadowCacheRW[uint2(pos.x + 3, pos.y)].xy;

	//float4 res = val0 * 0.4 + valM1 * 0.2 + valP1 * 0.2 + valM2 * 0.1 + valP2 * 0.1;
	//float4 res = min(val0, min(valM1, valP1));
	float2 res = min(val0, valM1);

	//float4 res = val0 * 0.2 + valM1 * 0.2 + valP1 * 0.2 + valM2 * 0.1 + valP2 * 0.1 + valM2 * 0.1 + valP2 * 0.1;

	pSrt->m_volumetricsShadowCacheTempRW[pos].g = res.g;

}


[NUM_THREADS(8, 8, 1)] //
void CS_ShadowCacheSlicesExaggerateY(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsCreateShadowCacheSrt *pSrt : S_SRT_DATA)
{

	uint2 pos = dispatchId.xy;

	float2 val0 = pSrt->m_volumetricsShadowCacheTempRW[pos];
	float2 valM1 = pSrt->m_volumetricsShadowCacheTempRW[uint2(pos.x, pos.y - 1)];
	float2 valP1 = pSrt->m_volumetricsShadowCacheTempRW[uint2(pos.x, pos.y + 1)];
	float2 valM2 = pSrt->m_volumetricsShadowCacheTempRW[uint2(pos.x, pos.y - 2)];
	float2 valP2 = pSrt->m_volumetricsShadowCacheTempRW[uint2(pos.x, pos.y + 2)];

	//float4 res = min(val0, min(valM1, valP1));
	float2 res = val0;

	pSrt->m_volumetricsShadowCacheRW[pos].g = res.g;

}



[NUM_THREADS(8, 8, 1)] // x : particle index, y is
void CS_UpscaleAndCompositeHalfResBufferToPrimaryFloat(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsCompositeSrt *pSrt : S_SRT_DATA)
{
	//uint2 groupPos = groupId * GROUP_SAMPLE_WIDTH_HEIGHT;

	// make our reads use L1, and we will write to L2
	//uint4 tsharplo = __get_tsharplo(pSrt->m_destTexture);
	//uint4 tsharphi = __get_tsharphi(pSrt->m_destTexture);
	//Texture2D<float4> destTextureRO = __create_texture< Texture2D<float4> >(tsharplo, tsharphi);

	uint2 posFullRes = dispatchId.xy;

	float2 uvHalfRes = posFullRes / float2(SCREEN_NATIVE_RES_W_F, SCREEN_NATIVE_RES_H_F);

	float4 lowerResC00 = pSrt->m_srcTexture.SampleLevel(pSrt->m_sampler, uvHalfRes, 0).rgba;
	//float4 lowerResC00 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, uvQuarterResAdj, 0).rgba;

	float4 destColor = pSrt->m_destTextureRO[posFullRes];
	
	// srcColor is already pre-blended with its alpha
	destColor.rgb = destColor.rgb * lowerResC00.a + lowerResC00.rgb;

	//pSrt->m_destTextureHighRes[posHalfRes] = lowerResC00;
	pSrt->m_destTexture[posFullRes] = destColor;
}


float HeightMapDepthToStoredValue(float linDepth, float expRange, float expShadowConstant)
{
#if FROXELS_USE_EXP_SHADOWS_FOR_HEIGHT_MAP

	// need to convert stored depth to linear depth

	// this is how we go from linear to stored depth:
	// depth = (lightDesc.m_depthScale + lightDesc.m_depthOffset / linearDepth) * lightDesc.m_depthOffsetSlope + lightDesc.m_depthOffsetConst;

	// depth - lightDesc.m_depthOffsetConst = (lightDesc.m_depthScale + lightDesc.m_depthOffset / linearDepth) * lightDesc.m_depthOffsetSlope
	// (depth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope = (lightDesc.m_depthScale + lightDesc.m_depthOffset / linearDepth)
	// (depth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope = lightDesc.m_depthScale + lightDesc.m_depthOffset / linearDepth
	// [(depth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope] - lightDesc.m_depthScale = lightDesc.m_depthOffset / linearDepth
	// linearDepth = lightDesc.m_depthOffset / ([(depth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope] - lightDesc.m_depthScale)

	float linearDepth = linDepth;

	float linearepthNorm = saturate(linearDepth / (expRange)); // max 32 meters 0..1 value

	linearepthNorm = linearepthNorm * 2.0 - 1.0; // -1..1

												 //shadowExpValue = min(pSrt->m_shadowDistOffset, shadowExpValue);

	float expMap = exp2(linearepthNorm * expShadowConstant);

	return expMap;

#else
	return linDepth;
#endif
}


[NUM_THREADS(8, 8, 1)] // x : particle index, y is
void CS_ConvertToColorHeight(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsColorHeightMapSrt *pSrt : S_SRT_DATA)
{
	//uint2 groupPos = groupId * GROUP_SAMPLE_WIDTH_HEIGHT;

	// make our reads use L1, and we will write to L2
	//uint4 tsharplo = __get_tsharplo(pSrt->m_destTexture);
	//uint4 tsharphi = __get_tsharphi(pSrt->m_destTexture);
	//Texture2D<float4> destTextureRO = __create_texture< Texture2D<float4> >(tsharplo, tsharphi);

	uint2 pos = dispatchId.xy;

	float maxValue = 0;

	int kSteps = 1;
	for (int y = -kSteps; y <= kSteps; ++y)
	{
		for (int x = -kSteps; x <= kSteps; ++x)
		{
			maxValue = max(maxValue, pSrt->m_srcTexture[int2(pos) + int2(x, y)]);
		}
	}

	//float depthVal = (pSrt->m_params[1] + GetLinearDepth(pSrt->m_srcTexture[pos], pSrt->m_depthParams.xy)) / pSrt->m_params[0]; // should give us 0 to 1 over teh whole rnage of particle cube
	float depthVal = (pSrt->m_params[1] + maxValue * (pSrt->m_params[0] - pSrt->m_params[1])) / pSrt->m_params[0]; // should give us 0 to 1 over teh whole rnage of particle cube

	depthVal = 1.0 - saturate(depthVal);
	float r = floor(depthVal * 255.0) / 255.0;
	float g = (depthVal - r) * 255.0;
	float b = 0;
	float a = 1;
	
	//r = depthVal;
	//g = depthVal;
	//b = depthVal;

	//pSrt->m_destTexture[pos] = float4(r, g, b, a);
	pSrt->m_destTexture[pos].x = depthVal;

	float linDepth = depthVal * pSrt->m_params[0];

	pSrt->m_destTexture[pos].y = HeightMapDepthToStoredValue(linDepth, pSrt->m_expRange, pSrt->m_expConstant);
}

