/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#define REDUCTION_TILE_SIZE (16)
#include "reduction-util.fxi"
#include "global-funcs.fxi"
#include "post-processing-common.fxi"

struct BokehDofTileMinMaxSrt
{
	Texture2D<float>		m_depth;
	Texture2D<float>		m_fogCocTex;
	RW_Texture2D<float4>	m_outTex;
	RW_Texture2D<float>		m_outFogCocTex;
	DofBlurConstants		*consts;
	DofWeaponScopeParams	*scopeParams;

	float2					m_invBufferSize;
	float					m_pad0[2];
	float4					m_screenScaleOffset;
	float4					m_viewToWorldMatY;

	float					m_cocOverride;
	float					m_aspect;
	float					m_volFogScale;
	float					m_maxVolFogCoc;
	float					m_fogHeightStart;
	float					m_fogHeightRange;
	float					m_fogCameraHeightExp;
	float					m_fogDistanceStart;
	float					m_fogDistanceEnd;
	float					m_cameraPosY;
	float					m_fogDensity;
	float					m_distanceGamma;
	float					m_fogMaxDensity;
	uint				    m_useFogBufferAsBlurSource;
	float					m_fogAffectSky;
};

struct BokehDofTileMinMaxSrt_Y
{
	Texture2D<float4>		m_inTex;
	RW_Texture2D<float4>	m_outTex;
	uint					m_heightM1;
};


struct BokehDofNeighborhoodMinMaxSrt
{
	Texture2D<float4>		m_minMaxTex;
	RW_Texture2D<float4>	m_neighMinMaxTex;
	int2					m_texSizeM1;
	int						m_blurSteps;
};

float2 GetTileMinMax(uint threadGroupIndex, float coc)
{
	return MinMaxReduce(threadGroupIndex, coc);
}

float2 UniformSample(float i, uint numRingSamples)
{
	float phi = i / float(numRingSamples) * kPi * 2.f;
	return float2(cos(phi), sin(phi));
}

float GetCoc(float linearDepth, DofBlurConstants *consts)
{
	float coc = 0.0f;

	float fNumber = consts->m_fNumber;
	if (fNumber >= consts->m_minFNumber)
	{
		coc = GetCocRadius(consts->m_fNumber, linearDepth, consts->m_focusPlaneDist, consts->m_lensfocalLength) * consts->m_pixelFilmRatio;

		float cocSign = sign(coc);
		float absCoc = abs(coc);
		float2 scaleOffsetParams = coc > 0.0f ? consts->m_dofBlurParams.xz : consts->m_dofBlurParams.yw;
		float finalCoc = max(absCoc - scaleOffsetParams.y, 0.0f) * scaleOffsetParams.x;
		coc = finalCoc * cocSign;
	}
	else
	{
		coc = dot(saturate(linearDepth.xxxx * consts->m_dofRangeScaleInfo.xyzw + consts->m_dofRangeOffsetInfo.xyzw),
			float4(consts->m_dofIntensityInfo.x, consts->m_dofIntensityInfo.y, -consts->m_dofIntensityInfo.y, consts->m_dofIntensityInfo.w)) + consts->m_dofIntensityInfo.z; //
		coc = abs(coc);
	}

	return clamp(coc / 2, -consts->m_maxBlurRadius, consts->m_maxBlurRadius);
}

// For the Coc, we want to get the ABSOLUTE minimum/maximum, but still 
// preserve the sign of the Coc. Sign is no longer mathematical, 
// but a simple is-foreground/background signifier
float UnsignedMin(float a, float b)
{
	return abs(a) < abs(b) ? a : b;
}

float UnsignedMax(float a, float b)
{
	return abs(a) > abs(b) ? a : b;
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofTileMinMax_X_CreateFogCocBuffer(int2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	uint2 groupThreadId : S_GROUP_THREAD_ID, uint2 groupId : S_GROUP_ID, BokehDofTileMinMaxSrt *pSrt : S_SRT_DATA)
{
	int2 dispatchId0 = dispatchThreadId.xy * int2(2, 1);
	int2 dispatchId1 = dispatchId0 + int2(1, 0);

	// Load up 2 values in order to cut the number of inactive threads during reduction
	float2 viewSpaceWithAlphaZ = { pSrt->m_depth[dispatchId0], pSrt->m_depth[dispatchId1] };

	float2 linearDepths = 1.0f / (viewSpaceWithAlphaZ * pSrt->consts->m_linearDepthParams.xx + pSrt->consts->m_linearDepthParams.yy);

	float2 coc_fog = 0;

	float3 positionVS0 = float3(((dispatchId0.xy + 0.5f) * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f) * linearDepths.x;
	float3 positionVS1 = float3(((dispatchId1.xy + 0.5f) * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f) * linearDepths.y;

	float postionWSY0 = dot(float4(positionVS0, 1.0f), pSrt->m_viewToWorldMatY);
	float postionWSY1 = dot(float4(positionVS1, 1.0f), pSrt->m_viewToWorldMatY);

	float2 pixelY;
	pixelY.x = pSrt->m_fogHeightStart - postionWSY0;
	pixelY.y = pSrt->m_fogHeightStart - postionWSY1;

	float cameraY = pSrt->m_fogHeightStart - pSrt->m_cameraPosY;

	if (pSrt->m_volFogScale > 0)
	{
		coc_fog.x = GetFogDensityIntegralS(pixelY.x, cameraY, linearDepths.x, pSrt->m_distanceGamma,
			pSrt->m_fogDistanceStart, pSrt->m_fogDensity, pSrt->m_fogMaxDensity,
			pSrt->m_fogHeightStart, pSrt->m_fogHeightRange, pSrt->m_fogCameraHeightExp, pSrt->m_fogDistanceEnd).x *
			(viewSpaceWithAlphaZ.x == 1.0f ? pSrt->m_fogAffectSky : 1.0f);

		coc_fog.y = GetFogDensityIntegralS(pixelY.y, cameraY, linearDepths.y, pSrt->m_distanceGamma,
			pSrt->m_fogDistanceStart, pSrt->m_fogDensity, pSrt->m_fogMaxDensity,
			pSrt->m_fogHeightStart, pSrt->m_fogHeightRange, pSrt->m_fogCameraHeightExp, pSrt->m_fogDistanceEnd).x *
			(viewSpaceWithAlphaZ.y == 1.0f ? pSrt->m_fogAffectSky : 1.0f);
	}

	coc_fog = min(coc_fog * pSrt->m_volFogScale, pSrt->m_maxVolFogCoc);

	float2 cocs = {
		abs(GetCoc(linearDepths.x, pSrt->consts)) + coc_fog.x,
		abs(GetCoc(linearDepths.y, pSrt->consts)) + coc_fog.y
	};

	if (pSrt->m_cocOverride > 0)
		cocs = pSrt->m_cocOverride.xx;

	float2 minMaxCoc = float2(min(cocs.x, cocs.y), max(cocs.x, cocs.y));
	minMaxCoc = MinMaxReduce8(minMaxCoc.x, minMaxCoc.y);
	float2 minMaxDepth = float2(min(linearDepths.x, linearDepths.y), max(linearDepths.x, linearDepths.y));
	minMaxDepth = MinMaxReduce8(minMaxDepth.x, minMaxDepth.y);

	if (groupThreadId.x == 0)
	{
		pSrt->m_outTex[uint2(groupId.x, dispatchThreadId.y)] = float4(minMaxCoc, minMaxDepth);
	}

	pSrt->m_outFogCocTex[dispatchId0] = coc_fog.x;
	pSrt->m_outFogCocTex[dispatchId1] = coc_fog.y;
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofTileMinMax_X(int2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	uint2 groupThreadId : S_GROUP_THREAD_ID, uint2 groupId : S_GROUP_ID, BokehDofTileMinMaxSrt *pSrt : S_SRT_DATA)
{
	int2 dispatchId0 = dispatchThreadId.xy * int2(2, 1);
	int2 dispatchId1 = dispatchId0 + int2(1, 0);

	// Load up 2 values in order to cut the number of inactive threads during reduction
	float2 viewSpaceWithAlphaZ = { pSrt->m_depth[dispatchId0], pSrt->m_depth[dispatchId1] };

	float2 linearDepths = 1.0f / (viewSpaceWithAlphaZ * pSrt->consts->m_linearDepthParams.xx + pSrt->consts->m_linearDepthParams.yy);

	float2 coc_fog = float2(pSrt->m_fogCocTex[dispatchId0], pSrt->m_fogCocTex[dispatchId1]);

	float2 cocs = {
		abs(GetCoc(linearDepths.x, pSrt->consts)) + coc_fog.x,
		abs(GetCoc(linearDepths.y, pSrt->consts)) + coc_fog.y
	};

	if (pSrt->m_cocOverride > 0)
		cocs = pSrt->m_cocOverride.xx;

	// override dof coc on outside of scope when zoomed in with rifle
	DofWeaponScopeParams* pScopeParams = pSrt->scopeParams;
	if (pScopeParams->m_scopePostProcessBlend > 0.0f)
	{
		float2 uvSS = float2(dispatchId0)* pSrt->m_invBufferSize;

		float2 aspectCorrectCenter = pScopeParams->m_scopeSSCenter * float2(pSrt->m_aspect, 1.0f);
		float2 aspectCorrectUV = uvSS * float2(pSrt->m_aspect, 1.0f);
		float2 ssDisp = aspectCorrectUV - aspectCorrectCenter;

		if (length(ssDisp) >= pScopeParams->m_scopeSSRadius)
		{
			cocs = lerp(cocs, float2(pScopeParams->m_scopeDofCoc), pScopeParams->m_scopePostProcessBlend);
		}
	}

	float2 minMaxCoc = float2(min(cocs.x, cocs.y), max(cocs.x, cocs.y));
	minMaxCoc = MinMaxReduce8(minMaxCoc.x, minMaxCoc.y);
	float2 minMaxDepth = float2(min(linearDepths.x, linearDepths.y), max(linearDepths.x, linearDepths.y));
	minMaxDepth = MinMaxReduce8(minMaxDepth.x, minMaxDepth.y);

	if (groupThreadId.x == 0)
	{
		pSrt->m_outTex[uint2(groupId.x, dispatchThreadId.y)] = float4(minMaxCoc, minMaxDepth);
	}
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofTileMinMax_Y(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	uint2 groupThreadId : S_GROUP_THREAD_ID, uint2 groupId : S_GROUP_ID, BokehDofTileMinMaxSrt_Y *pSrt : S_SRT_DATA)
{
	// Same strategy as X filter
	uint2 scaledCoord = dispatchThreadId.yx * uint2(1, 2);

	float4 values[2] = {
		pSrt->m_inTex[uint2(scaledCoord.x, min(scaledCoord.y, pSrt->m_heightM1))],
		pSrt->m_inTex[uint2(scaledCoord.x, min(scaledCoord.y + 1, pSrt->m_heightM1))],
	};

	float2 minMaxCoc = float2(min(values[0].x, values[1].x),
		max(values[0].y, values[1].y));
	float2 minMaxDepth = float2(min(values[0].z, values[1].z), max(values[0].w, values[1].w));

	minMaxCoc = MinMaxReduce8(minMaxCoc.x, minMaxCoc.y);
	minMaxDepth = MinMaxReduce8(minMaxDepth.x, minMaxDepth.y);

	if (groupThreadId.x == 0)
	{
		pSrt->m_outTex[uint2(dispatchThreadId.y, groupId.x)] = float4(minMaxCoc, minMaxDepth);
	}
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofNeighborhoodMinMax(int2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	BokehDofNeighborhoodMinMaxSrt *pSrt : S_SRT_DATA)
{
	float4 minMax = pSrt->m_minMaxTex[dispatchThreadId];

	// TODO: Might be faster to use point sampling with offsets - no need to do min/max of texel coordinates

	for (int j = -1; j <= 1; ++j)
	{
		for (int i = -1; i <= 1; ++i)
		{
			if (i == 0 && j == 0)
				continue;

			int2 location = dispatchThreadId + int2(i, j);
			location = clamp(location, int2(0, 0), pSrt->m_texSizeM1);

			float4 neighbor = pSrt->m_minMaxTex[location];
			minMax.x = min(minMax.x, neighbor.x);
			minMax.y = max(minMax.y, neighbor.y);
			minMax.z = min(minMax.z, neighbor.z); // Do we need to do this? I'm not sure
			minMax.w = max(minMax.w, neighbor.w);
		}
	}

	// Finally, get the unsigned min/max while still preserving the sign bit
	float2 unsignedMinMax = { UnsignedMin(minMax.x, minMax.y),
		UnsignedMax(minMax.x, minMax.y) };

	pSrt->m_neighMinMaxTex[dispatchThreadId] = minMax;
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofNeighborhoodMinMax2(int2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	BokehDofNeighborhoodMinMaxSrt *pSrt : S_SRT_DATA)
{
	float4 minMax = pSrt->m_minMaxTex[dispatchThreadId];

	// TODO: Might be faster to use point sampling with offsets - no need to do min/max of texel coordinates

	for (int j = -2; j <= 2; ++j)
	{
		for (int i = -2; i <= 2; ++i)
		{
			if (i == 0 && j == 0)
				continue;

			int2 location = dispatchThreadId + int2(i, j);
			location = clamp(location, int2(0, 0), pSrt->m_texSizeM1);

			float4 neighbor = pSrt->m_minMaxTex[location];
			minMax.x = min(minMax.x, neighbor.x);
			minMax.y = max(minMax.y, neighbor.y);
			minMax.z = min(minMax.z, neighbor.z); // Do we need to do this? I'm not sure
			minMax.w = max(minMax.w, neighbor.w);
		}
	}

	// Finally, get the unsigned min/max while still preserving the sign bit
	float2 unsignedMinMax = { UnsignedMin(minMax.x, minMax.y),
		UnsignedMax(minMax.x, minMax.y) };

	pSrt->m_neighMinMaxTex[dispatchThreadId] = minMax;
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofNeighborhoodMinMaxN(int2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	BokehDofNeighborhoodMinMaxSrt *pSrt : S_SRT_DATA)
{
	float4 minMax = pSrt->m_minMaxTex[dispatchThreadId];

	// TODO: Might be faster to use point sampling with offsets - no need to do min/max of texel coordinates

	for (int j = -pSrt->m_blurSteps; j <= pSrt->m_blurSteps; ++j)
	{
		for (int i = -pSrt->m_blurSteps; i <= pSrt->m_blurSteps; ++i)
		{
			if (i == 0 && j == 0)
				continue;

			int2 location = dispatchThreadId + int2(i, j);
			location = clamp(location, int2(0, 0), pSrt->m_texSizeM1);

			float4 neighbor = pSrt->m_minMaxTex[location];
			minMax.x = min(minMax.x, neighbor.x);
			minMax.y = max(minMax.y, neighbor.y);
			minMax.z = min(minMax.z, neighbor.z); // Do we need to do this? I'm not sure
			minMax.w = max(minMax.w, neighbor.w);
		}
	}

	// Finally, get the unsigned min/max while still preserving the sign bit
	float2 unsignedMinMax = { UnsignedMin(minMax.x, minMax.y),
		UnsignedMax(minMax.x, minMax.y) };

	pSrt->m_neighMinMaxTex[dispatchThreadId] = minMax;
}


////////////////////////////////////////////////////////////////////////////////////////////
// Presort pass
////////////////////////////////////////////////////////////////////////////////////////////

static const float DOF_DEPTH_SCALE_FOREGROUND = 1.f / 2.5f;
static const float DOF_SINGLE_PIXEL_RADIUS = 0.5f;
static const float DOF_SPREAD_TOE_POWER = 4.f;

struct PresortSrt
{
	Texture2D<float2>		m_minHalfResDepth;
	Texture2D<float4>		m_tiles;
	Texture2D<float>		m_srcCoc;
	RW_Texture2D<float3>	m_outPresort;
	DofBlurConstants		*consts;
	float					m_cocOverride;
	float					m_depthScaleForeground;
	uint					m_enableDepthWarp;
	uint					m_useBetterFgBgClassification;
	uint					m_useMixedClassification;
	uint					m_useMinMaxDepth;
};

// Gives a weight value to the background/foreground categories for each pixel
float2 DepthCmp2(float2 depth, float closestDepth, float farthestDepth, float depthScaleForeground, bool enableDepthWarp, bool useBetterFgBgClassification, bool useMinMaxDepth)
{
	if (useBetterFgBgClassification)
	{
		// School of thought: prioritize FG since the whole point of depthCmp is to handle FG diffuse over background, and make it CoC dependent.
		float d1Min = enableDepthWarp ? Pow2(depth.x) : depth.x;
		
		float d1Max = enableDepthWarp ? Pow2(depth.x) : depth.x;
		if (useMinMaxDepth)
			d1Max = enableDepthWarp ? Pow2(depth.y) : depth.y;
		
		float d2 = enableDepthWarp ? Pow2(closestDepth) : closestDepth;
		float d3 = enableDepthWarp ? Pow2(farthestDepth) : farthestDepth;

		float relativeScale = 1.f / (d3 - d2);

		float fg = saturate(relativeScale * (d3 - d1Max));
		float bg = saturate(relativeScale * (d1Min - d2));

		float2 depthCmp = float2(bg, fg);
		return depthCmp;
	}
	else
	{
		float d1 = enableDepthWarp ? Pow2(depth.x) : depth.x;
		float d2 = enableDepthWarp ? Pow2(closestDepth) : closestDepth;
		float d = depthScaleForeground * (d1 - d2);	// aarora: depthScaleForeground = [0.4 - 20.0]. d can be huge, and the "scaling" doesn't make sense here if depthScaleFg < 1
		float2 depthCmp;
		// BG factor
		depthCmp.x = smoothstep(0.f, 1.f, d);		// aarora: If d is > 1, how does this even make sense?
		// FG factor
		depthCmp.y = 1.f - depthCmp.x;				// aarora: Same here
		return depthCmp;
	}
}

// The blend value or weight of a pixel is inversely proportional to the area of the CoC at that pixel
float SampleAlpha(float coc)
{
	float clampedCoc = max(coc, 1.0f);	// At least one pixel of Coc (coc of zero means it doesn't spread beyond the given pixel).
	return rcp(kPi * clampedCoc * clampedCoc * DOF_SINGLE_PIXEL_RADIUS * DOF_SINGLE_PIXEL_RADIUS);
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofPresort(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID, PresortSrt *pSrt : S_SRT_DATA)
{
	float2 depth = pSrt->m_minHalfResDepth[dispatchThreadId];
	float2 linearDepth = 1.f / (depth * pSrt->consts->m_linearDepthParams.x + pSrt->consts->m_linearDepthParams.y);
	const float4 tileData = pSrt->m_tiles[dispatchThreadId / 8];

	float coc = abs(GetCoc(linearDepth.x, pSrt->consts)) + pSrt->m_srcCoc[dispatchThreadId * 2];

	if (pSrt->m_cocOverride > 0)
		coc = pSrt->m_cocOverride;

	bool bUseBetterFgBgClassification = pSrt->m_useBetterFgBgClassification;
	if (pSrt->m_useMixedClassification)
	{
		bUseBetterFgBgClassification = linearDepth.x > (pSrt->consts->m_focusPlaneDist + 0.25f);	// Slight bias to avoid affecting other FG objects
	}

	float2 depthCmp = DepthCmp2(linearDepth, tileData.z, tileData.w, pSrt->m_depthScaleForeground, pSrt->m_enableDepthWarp, bUseBetterFgBgClassification, pSrt->m_useMinMaxDepth);
	float alpha = SampleAlpha(coc);

	// Take the abs of the coc or else R11G11B10 buffers break
	coc = abs(coc);
	float3 value = float3(coc,						// Coc
		(depthCmp.x),				// BG factor
		(depthCmp.y));			// FG factor

	if (!pSrt->m_useBetterFgBgClassification)
		value.yz *= alpha;

	value.y = max(value.y, 0.00001f);
	
	pSrt->m_outPresort[dispatchThreadId] = value;
}

////////////////////////////////////////////////////////////////////////////////////////////
// Downsample pass
////////////////////////////////////////////////////////////////////////////////////////////

struct DownsampleSrt
{
	Texture2D<float4>		m_tiles;
	Texture2D<float>		m_depth;
	Texture2D<float>		m_minDepthInfo;
	Texture2D<float3>		m_color;
	Texture2D<float2>		m_mvector;
	RW_Texture2D<float3>	m_outColor;
	RW_Texture2D<float2>	m_outMvector;
	SamplerState			m_sampler;
	DofBlurConstants		*consts;
	float2					m_invDstSize;
	float2					m_invSrcSize;
	float2					m_samples[4];
	int						m_blurColor;
	int						m_useDepthInfo;
	float					m_horizontalScale;
};

float2 GetDepthOffsetPixels(float2 uv, DownsampleSrt srt)
{
	// Follow the same ordering as the Gather() function
	float minDepthInfo = srt.m_minDepthInfo.SampleLevel(srt.m_sampler, uv, 0);
	if (minDepthInfo == 0.0f)
	{
		// Bottom left
		return float2(0.0f, 1.0f);
	}
	else if (minDepthInfo == 0.25f)
	{
		// Bottom right
		return float2(1.0f, 1.0f);
	}
	else if (minDepthInfo == 0.5f)
	{
		// Top right
		return float2(1.0f, 0.0f);
	}
	else
	{
		// Top left
		return float2(0.0f, 0.0f);
	}
}

void ProcessDownsampleTap(float2 uv, float depth, float linearDepth, DownsampleSrt srt, inout float4 samples, inout float2 mvectorSamples)
{
	const float3 kLuma = float3(0.2125f, 0.7154f, 0.0721f);

	float3 colorSample = srt.m_color.SampleLevel(srt.m_sampler, uv, 0);
	float2 mvectorSample = srt.m_mvector.SampleLevel(srt.m_sampler, uv, 0);

#if 0
	float4 depthSamples = srt.m_depth.Gather(srt.m_sampler, uv);
	float4 depthDiff = abs(depth - depthSamples);

	float maxDepthDiff = max3(depthDiff.x, depthDiff.y, max(depthDiff.z, depthDiff.w));

	// Find the depth bilateral weights
	float minWeight = exp(-maxDepthDiff * maxDepthDiff);
	minWeight = max(minWeight, 0.00001);
#else
	// Turn off bilateral filtering. Doesn't seem to affect quality that much with the temporal
	// bluring. And it's a huge cost
	float minWeight = 1;
#endif

	float colorWeight = rcp(1 + max3(colorSample.r, colorSample.g, colorSample.b));

	float weight = minWeight * colorWeight;

	samples += weight * float4(colorSample, 1.f);
	mvectorSamples += weight * mvectorSample;
}

void BokehDofDownsample(uint2 dispatchThreadId, DownsampleSrt *pSrt, bool bBlurMvector)
{
	float2 uv = (dispatchThreadId + 0.5f) * pSrt->m_invDstSize;

	const float4 tileData = pSrt->m_tiles[dispatchThreadId / 8];

	// Blur color using a 9-tap filter
	const float diskSize = tileData.y;

	const float2 pixelsPerSample = diskSize / 3.f * float2(pSrt->m_horizontalScale, 1.f);
	const float2 filterRadius = diskSize / 3.f / 2.f;

	// Parameters needed for bilateral filter which isn't used any more
	// Keeping support for code in case we ever need it again
	float depth = 0;
	float linearDepth = 0;

	float4 samples = 0;
	float2 mvectorSamples = 0;
	if (pSrt->m_blurColor)
	{
		// Do the center tap
		ProcessDownsampleTap(uv, depth, linearDepth, *pSrt, samples, mvectorSamples);

		// Do the first ring
		for (uint i = 0; i < 4; ++i)
		{
			float2 sampleUv = uv + pSrt->m_samples[i] * filterRadius * pSrt->m_invSrcSize;
			ProcessDownsampleTap(sampleUv, depth, linearDepth, *pSrt, samples, mvectorSamples);
		}

		if (samples.w > 0)
		{
			samples.xyz /= samples.w;
			mvectorSamples /= samples.w;
		}
	}
	else
	{
		if (pSrt->m_useDepthInfo)
		{
			// Use full-res UV + depth info to pick the exact pixel chosen by min depth
			float2 baseUv = (2.f * dispatchThreadId + 0.5f) * pSrt->m_invSrcSize;	// Top-left of 2x2 set of pixels
			float2 depthOffset = GetDepthOffsetPixels(baseUv, *pSrt);
			uv = baseUv + (depthOffset * pSrt->m_invSrcSize);
		}
		samples = float4(pSrt->m_color.SampleLevel(pSrt->m_sampler, uv, 0).xyz, 1.f);
		mvectorSamples = pSrt->m_mvector.SampleLevel(pSrt->m_sampler, uv, 0);
	}

	pSrt->m_outColor[dispatchThreadId] = samples.xyz;
	if (bBlurMvector)
		pSrt->m_outMvector[dispatchThreadId] = mvectorSamples;
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofDownsample(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	DownsampleSrt *pSrt : S_SRT_DATA)
{
	BokehDofDownsample(dispatchThreadId, pSrt, false);
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofDownsampleWithMvector(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	DownsampleSrt *pSrt : S_SRT_DATA)
{
	BokehDofDownsample(dispatchThreadId, pSrt, true);
}

bool DoesTileUseFullRes(float tileMaxCoc)
{
	return (tileMaxCoc < 0.5);
}

////////////////////////////////////////////////////////////////////////////////////////////
// Blur pass
////////////////////////////////////////////////////////////////////////////////////////////

struct BlurSrt
{
	Texture2D<float4>		m_tileBuffer;
	Texture2D<float3>		m_presortBuffer;
	Texture2D<float3>		m_colorBuffer;
	Texture2D<float2>		m_mvectorBuffer;
	RW_Texture2D<float3>	m_outBuffer;
	RW_Texture2D<float2>	m_outMvectorBuffer;
	RW_Texture2D<float>		m_outAlpha;
	SamplerState			m_pointSampler;
	SamplerState			m_linearSampler;

	DofWeaponScopeParams	*scopeParams;

	float2					m_invFullSize;
	float2					m_invHalfSize;
	float					m_aspect;
	float					m_jitterScale;
	float					m_simpleBlurThreshold;

	float					m_firstRingThreshold;
	float					m_secondRingThreshold;
	float					m_thirdRingThreshold;
	float					m_fourRingThreshold;
	float					m_fiveRingThreshold;
	float					m_sixRingThreshold;

	uint					m_ringOpt;
	uint					m_useForegroundAlpha;
	uint					m_useBetterFgBgClassification;
	uint					m_disableSimpleBlur;
	uint					m_usePointSampler;
	uint					m_separateMiddleLayer;
	int						m_debugShowLayer;
	int						m_debugShowRing;
	float					m_debugShowArc;
	float					m_debugShowCoc;
	uint					m_debugShowLastSample;
	uint					m_debugShowLastPresort;
	int						m_debugShowSimpleBlurredPixels;

	float					m_horizontalScale;
};

// http://psgraphics.blogspot.com/2011/01/improved-code-for-concentric-map.html
float2 UniformCircleSample(float a, float b)
{
	float r, phi;
	if (a*a > b*b)
	{
		r = a;
		phi = (kPi / 4.f)*(b / a);
	}
	else
	{
		r = b;
		phi = (kPi / 2.f) - (kPi / 4.f)*(a / b);
	}

	return float2(r*cos(phi), r*sin(phi));
}

float SpreadToe(float offsetCoc, float spreadCmp)
{
	// HACK! Prevents sharp foreground from bleeding onto blurry background, though.
	return offsetCoc <= 1 ? pow(spreadCmp, DOF_SPREAD_TOE_POWER) : spreadCmp;
}

float DoesSampleCircleOverlapPixel(float offsetCoc, float sampleCoc, float spreadScale, bool bUseBetterFgBgClassification)
{
	return SpreadToe(offsetCoc, saturate(spreadScale * sampleCoc - offsetCoc + 1));
}

float RandFast(uint2 PixelPos, float Magic = 3571.0)
{
	float2 Random = (1.0 / 4320.0) * PixelPos + float2(0.25, 0.0);
	Random = frac(dot(Random * Random, Magic));
	Random = frac(dot(Random * Random, Magic));
	return Random.x;
}

float2 UniformDiskSample(float2 u)
{
	float r = sqrt(u.x);
	float theta = 2 * kPi * u.y;
	return r * float2(cos(theta), sin(theta));
}

float MinComp(float4 v) { return min3(v.x, v.y, min(v.z, v.w)); }
float AvgComp(float4 v) { return (v.x + v.y + v.z + v.w) / 4; }

float2 ProcessTap(float2 uv, float offsetCoc, float spreadScale, float3 centerPresortInfo,
	inout float4 background, inout float2 bgMvector, inout float4 foreground, inout float2 fgMvector,
	BlurSrt srt)
{
	float3 presortInfo = srt.m_presortBuffer.SampleLevel(srt.m_pointSampler, uv, 0);
	float sampleCoc = presortInfo.x;

	float2 bgFgSamples = float2(0.f, 0.f);

	// If the circle doesn't overlap this pixel, don't pay the cost of sampling two times - optimization
	float doesCircleOverlap = DoesSampleCircleOverlapPixel(offsetCoc, sampleCoc, spreadScale, srt.m_useBetterFgBgClassification);
	if (doesCircleOverlap > 0.f || !srt.m_useBetterFgBgClassification)	// ...but also, don't allow this optimization on the old code (which didn't have it anyway). Allows for better before/after performance comparisons.
	{
		bgFgSamples = float2(1.f, 1.f);

		float3 sampleColor = (srt.m_usePointSampler) ? srt.m_colorBuffer.SampleLevel(srt.m_pointSampler, uv, 0) : srt.m_colorBuffer.SampleLevel(srt.m_linearSampler, uv, 0);
		float2 sampleMvector = srt.m_mvectorBuffer.SampleLevel(srt.m_linearSampler, uv, 0);

		if (srt.m_useBetterFgBgClassification)
			presortInfo.yz *= saturate(SampleAlpha(sampleCoc));		// Conserve energy by spreading the pixel over its CoC's area

		presortInfo.yz *= saturate(doesCircleOverlap);			// The old code used to do this instead of disallowing sampling entirely

		background += presortInfo.y * float4(sampleColor, 1.f);
		foreground += presortInfo.z * float4(sampleColor, 1.f);

		bgMvector += presortInfo.y * sampleMvector;
		fgMvector += presortInfo.z * sampleMvector;

		if (srt.m_debugShowLastSample)
		{
			background = presortInfo.y * float4(sampleColor, 1.f);
			foreground = presortInfo.z * float4(sampleColor, 1.f);

			bgMvector = presortInfo.y * sampleMvector;
			fgMvector = presortInfo.z * sampleMvector;

			if (srt.m_debugShowLastPresort)
			{
				background = float4(sampleCoc / 16.f, presortInfo.y, 0.f, 1.f);
				foreground = float4(sampleCoc / 16.f, 0.f, presortInfo.z, 1.f);
			}
		}
	}

	return bgFgSamples;
}

float2 ProcessRing(float2 uv, float spreadScale,
				   float2 pixelsPerSampleUnit, float3 mainPresortInfo,
				   inout float4 background, inout float2 bgMvector,
				   inout float4 foreground, inout float2 fgMvector,
				   const uint ringNumber, const uint debugMaxSamples, BlurSrt *pSrt)
{
	float2 numSamplesBgFg = float2(0.f, 0.f);

	// Micro-optimization: calculate cos-sin only for first quadrant and use trig identities to compute for other three quadrants (as opposed to calculating cos and sin for every sample)
	float4x4 quadCosSinMultipliers = {
			float4(float2(1.f, 0.f), float2(0.f, 1.f)),	// 1st quadrant - cos(phi), sin(phi)
			float4(float2(0.f, -1.f), float2(1.f, 0.f)),	// 2nd quadrant - cos(90 + phi) = -sin(phi), sin(90 + phi) = cos(phi)
			float4(float2(-1.f, 0.f), float2(0.f, -1.f)),	// 3rd quadrant - cos(180 + phi) = -cos(phi), sin(180 + phi) = -sin(phi)
			float4(float2(0.f, 1.f), float2(-1.f, 0.f)),	// 4th quadrant - cos(270 + phi) = sin(phi), sin(270 + phi) = -cos(phi)
	};

	const uint numSamples = ringNumber * 8;
	const uint numSamplesInQuadrant = numSamples / 4;

	const uint maxNumRings = 6;
	const uint maxNumSamplesInQuadrant = (maxNumRings * 8) / 4;		// 12

	float2 angleCosSins[maxNumSamplesInQuadrant];
	for (uint i = 0; i < numSamplesInQuadrant; i++)
	{
		angleCosSins[i] = UniformSample(i, numSamples);
	}

	for (uint i = 0; i < 4; i++)
	{
		float4 quadrantMultiplier = quadCosSinMultipliers[i];

		bool bDebugEarlyExit = false;
		for (uint j = 0; j < numSamplesInQuadrant; j++)
		{
			float2 angleCosSin = angleCosSins[j];

			if (pSrt->m_debugShowArc != 0.f)
			{
				int numSamplesTaken = (i * 4) + (j + 1);
				bDebugEarlyExit = numSamplesTaken > debugMaxSamples;
				if (bDebugEarlyExit)
					break;
			}

			float2 sampleCosSin = float2(dot(angleCosSin, quadrantMultiplier.xy), dot(angleCosSin, quadrantMultiplier.zw));
			float2 sampleUv = uv + sampleCosSin * (ringNumber * pixelsPerSampleUnit) * pSrt->m_invFullSize;
			numSamplesBgFg += ProcessTap(sampleUv, ringNumber, spreadScale, mainPresortInfo, background, bgMvector, foreground, fgMvector, *pSrt);
		}

		if (bDebugEarlyExit)
			break;
	}

	return numSamplesBgFg;
}

void ProcessSimpleTap(float2 uv, inout float3 background, inout float2 bgMvector, BlurSrt srt)
{
	float3 sampleColor = srt.m_colorBuffer.SampleLevel(srt.m_pointSampler, uv, 0);
	float2 sampleMvector = srt.m_mvectorBuffer.SampleLevel(srt.m_linearSampler, uv, 0);

	background += sampleColor;
	bgMvector += sampleMvector;
}

void BokehDofBlur(uint2 dispatchThreadId, BlurSrt *pSrt, bool bBlurMvector)
{
	float2 uv = (dispatchThreadId + 0.5) * pSrt->m_invHalfSize;
	float4 tile = pSrt->m_tileBuffer[dispatchThreadId / 8];

	bool simpleBlur = !pSrt->m_disableSimpleBlur && (abs(tile.x - tile.y) < pSrt->m_simpleBlurThreshold);

	// override dof coc on outside of scope when zoomed in with rifle
	DofWeaponScopeParams* pScopeParams = pSrt->scopeParams;
	if (pScopeParams->m_scopePostProcessBlend > 0.0f)
	{
		float2 aspectCorrectCenter = pScopeParams->m_scopeSSCenter * float2(pSrt->m_aspect, 1.0f);
		float2 aspectCorrectUV = uv * float2(pSrt->m_aspect, 1.0f);
		float2 ssDisp = aspectCorrectUV - aspectCorrectCenter;
		if (length(ssDisp) >= pScopeParams->m_scopeSSRadius)
		{
			tile.xy = lerp(tile.yy, float2(pScopeParams->m_scopeDofCoc), pScopeParams->m_scopePostProcessBlend);
			simpleBlur = true;
		}
	}

	float3 finalColor = 0;
	float2 finalMvector = 0;

	const float maxCoc = tile.y;
	float diskScale = 1;

	bool doFirstRing = false,
		doSecondRing = false,
		doThirdRing = false;

	// Base the number of discs on maxCoc as it's a good indicator of how far we have to gather from
	if (pSrt->m_debugShowRing > -1)
	{
		int opt = pSrt->m_debugShowRing;
		doFirstRing = opt == 1;
		doSecondRing = opt == 2;
		doThirdRing = opt == 3;
		diskScale = (float)max(opt, 1);
	}
	else if (maxCoc > pSrt->m_thirdRingThreshold)
	{
		doFirstRing = true;
		doSecondRing = true;
		doThirdRing = true;
		diskScale = 3.f;
	}
	else if (maxCoc > pSrt->m_secondRingThreshold)
	{
		doFirstRing = true;
		doSecondRing = true;
		doThirdRing = false;
		diskScale = 2.f;
	}
	else if (maxCoc > pSrt->m_firstRingThreshold)
	{
		doFirstRing = true;
		doSecondRing = false;
		doThirdRing = false;
		diskScale = 1.f;
	}

	if (pSrt->m_ringOpt == 0)
	{
		doFirstRing = doSecondRing = doThirdRing = true;
		diskScale = 3.f;
	}

	// There are 3 rings, so this is the size of the entire disk per ring
	const float diskSize = maxCoc;
	float2 pixelsPerSampleUnit = diskSize / diskScale;
	float sampleUnitsPerPixel = 1.f / pixelsPerSampleUnit.x;
	pixelsPerSampleUnit.x *= pSrt->m_horizontalScale;
	float spreadScale = sampleUnitsPerPixel;	// Used later to determine if gathered CoCs are large enough to overlap this pixel

	float alpha = 0;

	if (DoesTileUseFullRes(maxCoc))	// Same threshold that, in the Upres pass, will fully use the full-res buffer. Absolutely no point in running blur, then. Early out!
	{
		pSrt->m_outBuffer[dispatchThreadId] = pSrt->m_colorBuffer.SampleLevel(pSrt->m_linearSampler, uv, 0);
		pSrt->m_outMvectorBuffer[dispatchThreadId] = pSrt->m_mvectorBuffer.SampleLevel(pSrt->m_linearSampler, uv, 0);
		if (pSrt->m_useForegroundAlpha && !pSrt->m_debugShowSimpleBlurredPixels)
		{
			pSrt->m_outAlpha[dispatchThreadId] = 0.0f;
		}
		return;
	}

	// Get the center tap
	if (simpleBlur)
	{
		float3 background = 0;
		float2 bgMvector = 0;
		float numSamples = 1;

		ProcessSimpleTap(uv, background, bgMvector, *pSrt);

		// Get the first ring
		if (doFirstRing)
		{
			for (uint i = 0; i < 8; ++i)
			{
				float2 sampleUv = uv + UniformSample(i, 8) * pixelsPerSampleUnit * pSrt->m_invFullSize;
				ProcessSimpleTap(sampleUv, background, bgMvector, *pSrt);
			}
			numSamples += 8;

			if (doSecondRing)
			{
				// Get the second ring
				for (uint i = 0; i < 16; ++i)
				{
					float2 sampleUv = uv + UniformSample(i, 16) * (2 * pixelsPerSampleUnit) * pSrt->m_invFullSize;
					ProcessSimpleTap(sampleUv, background, bgMvector, *pSrt);
				}
				numSamples += 16;

				if (doThirdRing)
				{
					// Get the third ring
					for (uint i = 0; i < 24; ++i)
					{
						float2 sampleUv = uv + UniformSample(i, 24) * (3 * pixelsPerSampleUnit) * pSrt->m_invFullSize;
						ProcessSimpleTap(sampleUv, background, bgMvector, *pSrt);
					}
					numSamples += 24;
				}
			}
		}

		background /= numSamples;
		bgMvector /= numSamples;

		finalColor = background;
		finalMvector = bgMvector;

		if (pSrt->m_debugShowSimpleBlurredPixels)
		{
			pSrt->m_outAlpha[dispatchThreadId] = 1.0f;
		}
	}
	else
	{
		float4 mainColor = 0;
		float4 background = 0;
		float4 foreground = 0;

		float2 mainMvector = 0;
		float2 bgMvector = 0;
		float2 fgMvector = 0;

		float numSamples = 0;
		float2 numSamplesBgFg = 0;

		float3 mainPresortInfo = 0;

		if (pSrt->m_debugShowRing <= 0)
		{
			if (pSrt->m_separateMiddleLayer)
			{
				mainPresortInfo = pSrt->m_presortBuffer.SampleLevel(pSrt->m_pointSampler, uv, 0);
				float mainAlpha = saturate(SampleAlpha(mainPresortInfo.x));

				mainColor.xyz = (pSrt->m_usePointSampler) ? pSrt->m_colorBuffer.SampleLevel(pSrt->m_pointSampler, uv, 0) : pSrt->m_colorBuffer.SampleLevel(pSrt->m_linearSampler, uv, 0);
				mainColor.a = mainAlpha;
				mainMvector = pSrt->m_mvectorBuffer.SampleLevel(pSrt->m_linearSampler, uv, 0);
			}
			else
			{
				numSamplesBgFg += ProcessTap(uv, 0, spreadScale, mainPresortInfo, background, bgMvector, foreground, fgMvector, *pSrt);
			}
			numSamples += 1;
		}

		int debugMaxSamplesPerRing[3] = { 8, 16, 24 };
		if (pSrt->m_debugShowArc != 0.f)
		{
			for (int i = 0; i < 3; i++)
			{
				float sampleArc = 360.f / (float)debugMaxSamplesPerRing[i];
				debugMaxSamplesPerRing[i] = (int)(pSrt->m_debugShowArc / sampleArc) + 1;
			}
		}

		const uint numQuadrants = 4;
		if (doFirstRing)
		{
			numSamplesBgFg += ProcessRing(uv, spreadScale, pixelsPerSampleUnit, mainPresortInfo,
				background, bgMvector, foreground, fgMvector,
				1, debugMaxSamplesPerRing[0], pSrt);
			numSamples += 8;
		}

		if (doSecondRing)
		{
			numSamplesBgFg += ProcessRing(uv, spreadScale, pixelsPerSampleUnit, mainPresortInfo,
				background, bgMvector, foreground, fgMvector,
				2, debugMaxSamplesPerRing[1], pSrt);
			numSamples += 16;
		}

		if (doThirdRing)
		{
			numSamplesBgFg += ProcessRing(uv, spreadScale, pixelsPerSampleUnit, mainPresortInfo,
				background, bgMvector, foreground, fgMvector,
				3, debugMaxSamplesPerRing[2], pSrt);
			numSamples += 24;
		}

		if (pSrt->m_separateMiddleLayer)
		{
			finalColor = mainColor.xyz;
			finalMvector = mainMvector;

			if (background.a > 0)
			{
				background.xyz /= background.a;
				bgMvector /= background.a;

				finalColor = lerp(background.xyz, mainColor.xyz, mainColor.a);
				finalMvector = lerp(bgMvector, mainMvector, mainColor.a);
			}

			if (foreground.a > 0)
			{
				foreground.xyz /= foreground.a;
				fgMvector /= foreground.a;

				if (pSrt->m_useBetterFgBgClassification)
				{
					foreground.a = saturate(foreground.a * rcp(numSamplesBgFg.y));
					background.a = background.a * rcp(numSamplesBgFg.x);
					alpha = foreground.a / (foreground.a + background.a);
				}
				else
				{
					alpha = saturate(2.0f * rcp(numSamples) * rcp(SampleAlpha(diskSize)) * foreground.a);
				}

				finalColor = lerp(finalColor, foreground.xyz, alpha);
				finalMvector = lerp(finalMvector, fgMvector, alpha);
			}

			if (pSrt->m_debugShowLayer > -1)
			{
				int opt = pSrt->m_debugShowLayer;
				if (opt == 0)
				{
					finalColor = background.xyz;
				}
				else if (opt == 1)
				{
					finalColor = mainColor.xyz;
				}
				else if (opt == 2)
				{
					finalColor = foreground.xyz;
				}
				else if (opt == 3)
				{
					// Shows downsampled color after all blending
				}
				else if (opt == 4)	// Start showing layers with blending, successively
				{
					finalColor = background.xyz;
				}
				else if (opt == 5)
				{
					finalColor = mainColor.xyz;
				}
				else if (opt == 6)	// BG-main blended
				{
					finalColor = lerp(background.xyz, mainColor.xyz, mainColor.a);
				}
				else if (opt == 7)	// BG-main blended
				{
					finalColor = foreground.xyz;
				}
				else
				{
					// Shows downsampled color after all blending
				}
			}
		}
		else if (pSrt->m_useBetterFgBgClassification)
		{
			if (background.a > 0)
			{
				background.xyz /= background.a;
				bgMvector /= background.a;
			}

			if (foreground.a > 0)
			{
				foreground.xyz /= foreground.a;
				fgMvector /= foreground.a;
			}

			foreground.a = saturate(2.0f * foreground.a * rcp(numSamplesBgFg.y));
			background.a = background.a * rcp(numSamplesBgFg.x);
			alpha = foreground.a / (foreground.a + background.a);

			finalColor = lerp(background.xyz, foreground.xyz, alpha);
			finalMvector = lerp(bgMvector, fgMvector, alpha);

			if (pSrt->m_debugShowLayer > -1)
			{
				int opt = pSrt->m_debugShowLayer;
				if (opt == 0)
				{
					finalColor = background.xyz;
				}
				else if (opt == 1)
				{
					finalColor = foreground.xyz;
				}
				else
				{
					// Shows downsampled color after all blending
				}
			}
		}
		else
		{
			if (background.a > 0)
			{
				background.xyz /= background.a;
				bgMvector /= background.a;
			}

			if (foreground.a > 0)
			{
				foreground.xyz /= foreground.a;
				fgMvector /= foreground.a;
			}

			// Bias alpha toward foreground to enable fg to diffuse over bg better (for blurry fg on sharp bg)
			alpha = saturate(2.0f * rcp(numSamples) * rcp(SampleAlpha(diskSize)) * foreground.a);
			finalColor = lerp(background.xyz, foreground.xyz, alpha);
			finalMvector = lerp(bgMvector, fgMvector, alpha);

			if (pSrt->m_debugShowLayer > -1)
			{
				int opt = pSrt->m_debugShowLayer;
				if (opt == 0)
				{
					finalColor = background.xyz;
				}
				else if (opt == 1)
				{
					finalColor = foreground.xyz;
				}
				else
				{
					// Shows downsampled color after all blending
				}
			}
		}

		if (pSrt->m_debugShowSimpleBlurredPixels)
		{
			pSrt->m_outAlpha[dispatchThreadId] = 0.0f;
		}
	}

	if (pSrt->m_ringOpt == 2)
	{
		float3 debugColor;
		if (doThirdRing)
			debugColor = float3(1, 0.1f, 0.1f);
		else if (doSecondRing)
			debugColor = float3(1, 1, 0.1);
		else if (doFirstRing)
			debugColor = float3(0.1, 1, 0.1);
		else
			debugColor = float3(0.1, 0.1, 1);

		finalColor *= debugColor;
	}

	if (pSrt->m_debugShowCoc > 0.0f)
	{
		float coc = pSrt->m_presortBuffer.SampleLevel(pSrt->m_pointSampler, uv, 0).x;
		if (coc > pSrt->m_debugShowCoc)
		{
			finalColor *= float3(1.0f, 0.0f, 0.0f);
		}
		else
		{
			finalColor *= float3(0.0f, 1.0f, 0.0f);
		}
	}

	pSrt->m_outBuffer[dispatchThreadId] = finalColor;
	if (bBlurMvector)
		pSrt->m_outMvectorBuffer[dispatchThreadId] = finalMvector;

	if (pSrt->m_useForegroundAlpha && !pSrt->m_debugShowSimpleBlurredPixels)
	{
		pSrt->m_outAlpha[dispatchThreadId] = alpha;
	}
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofBlur(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	BlurSrt *pSrt : S_SRT_DATA)
{
	BokehDofBlur(dispatchThreadId, pSrt, false);
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofBlurWithMvector(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	BlurSrt *pSrt : S_SRT_DATA)
{
	BokehDofBlur(dispatchThreadId, pSrt, true);
}

void BokehDofBlurHirez(uint2 dispatchThreadId, BlurSrt *pSrt, bool bBlurMvector)
{
	float2 uv = (dispatchThreadId + 0.5) * pSrt->m_invHalfSize;
	float4 tile = pSrt->m_tileBuffer[dispatchThreadId / 8];

	bool simpleBlur = !pSrt->m_disableSimpleBlur && (abs(tile.x - tile.y) < pSrt->m_simpleBlurThreshold);

	// override dof coc on outside of scope when zoomed in with rifle
	DofWeaponScopeParams* pScopeParams = pSrt->scopeParams;
	if (pScopeParams->m_scopePostProcessBlend > 0.0f)
	{
		float2 aspectCorrectCenter = pScopeParams->m_scopeSSCenter * float2(pSrt->m_aspect, 1.0f);
		float2 aspectCorrectUV = uv * float2(pSrt->m_aspect, 1.0f);
		float2 ssDisp = aspectCorrectUV - aspectCorrectCenter;
		if (length(ssDisp) >= pScopeParams->m_scopeSSRadius)
		{
			tile.xy = lerp(tile.yy, float2(pScopeParams->m_scopeDofCoc), pScopeParams->m_scopePostProcessBlend);
			simpleBlur = true;
		}
	}

	float3 finalColor = 0;
	float2 finalMvector = 0;

	const float maxCoc = tile.y;
	float diskScale = 1;

	bool doFirstRing = false,
		doSecondRing = false,
		doThirdRing = false,
		doFourRing = false,
		doFiveRing = false,
		doSixRing = false;

	// Base the number of discs on maxCoc as it's a good indicator of how far we have to gather from
	if (pSrt->m_debugShowRing > -1)
	{
		int opt = pSrt->m_debugShowRing;
		doFirstRing = opt == 1;
		doSecondRing = opt == 2;
		doThirdRing = opt == 3;
		doFourRing = opt == 4;
		doFiveRing = opt == 5;
		doSixRing = opt == 6;
		diskScale = (float)max(opt, 1);
	}
	else if (maxCoc > pSrt->m_sixRingThreshold)
	{
		doFirstRing = true;
		doSecondRing = true;
		doThirdRing = true;
		doFourRing = true;
		doFiveRing = true;
		doSixRing = true;
		diskScale = 6.f;
	}
	else if (maxCoc > pSrt->m_fiveRingThreshold)
	{
		doFirstRing = true;
		doSecondRing = true;
		doThirdRing = true;
		doFourRing = true;
		doFiveRing = true;
		diskScale = 5.f;
	}
	else if (maxCoc > pSrt->m_fourRingThreshold)
	{
		doFirstRing = true;
		doSecondRing = true;
		doThirdRing = true;
		doFourRing = true;
		diskScale = 4.f;
	}
	else if (maxCoc > pSrt->m_thirdRingThreshold)
	{
		doFirstRing = true;
		doSecondRing = true;
		doThirdRing = true;
		diskScale = 3.f;
	}
	else if (maxCoc > pSrt->m_secondRingThreshold)
	{
		doFirstRing = true;
		doSecondRing = true;
		diskScale = 2.f;
	}
	else if (maxCoc > pSrt->m_firstRingThreshold)
	{
		doFirstRing = true;
		diskScale = 1.f;
	}

	if (pSrt->m_ringOpt == 0)
	{
		doFirstRing = doSecondRing = doThirdRing = doFourRing = doFiveRing = doSixRing = true;
		diskScale = 6.f;
	}

	// There are upto 6 rings, so this is the size of the entire disk per ring
	const float diskSize = tile.y;
	float2 pixelsPerSampleUnit = diskSize / diskScale;
	float sampleUnitsPerPixel = 1.f / pixelsPerSampleUnit.x;
	pixelsPerSampleUnit.x *= pSrt->m_horizontalScale;
	float spreadScale = sampleUnitsPerPixel;

	float alpha = 0;

	// Get the center tap
	if (simpleBlur)
	{
		float3 background = 0;
		float2 bgMvector = 0;
		float numSamples = 1;

		ProcessSimpleTap(uv, background, bgMvector, *pSrt);

		// Get the first ring
		if (doFirstRing)
		{
			for (uint i = 0; i < 8; ++i)
			{
				float2 sampleUv = uv + UniformSample(i, 8) * pixelsPerSampleUnit * pSrt->m_invFullSize;
				ProcessSimpleTap(sampleUv, background, bgMvector, *pSrt);
			}
			numSamples += 8;

			if (doSecondRing)
			{
				// Get the second ring
				for (uint i = 0; i < 16; ++i)
				{
					float2 sampleUv = uv + UniformSample(i, 16) * (2 * pixelsPerSampleUnit) * pSrt->m_invFullSize;
					ProcessSimpleTap(sampleUv, background, bgMvector, *pSrt);
				}
				numSamples += 16;

				if (doThirdRing)
				{
					// Get the third ring
					for (uint i = 0; i < 24; ++i)
					{
						float2 sampleUv = uv + UniformSample(i, 24) * (3 * pixelsPerSampleUnit) * pSrt->m_invFullSize;
						ProcessSimpleTap(sampleUv, background, bgMvector, *pSrt);
					}
					numSamples += 24;

					if (doFourRing)
					{
						// Get the third ring
						for (uint i = 0; i < 32; ++i)
						{
							float2 sampleUv = uv + UniformSample(i, 32) * (4 * pixelsPerSampleUnit) * pSrt->m_invFullSize;
							ProcessSimpleTap(sampleUv, background, bgMvector, *pSrt);
						}
						numSamples += 32;

						if (doFiveRing)
						{
							// Get the third ring
							for (uint i = 0; i < 40; ++i)
							{
								float2 sampleUv = uv + UniformSample(i, 40) * (5 * pixelsPerSampleUnit) * pSrt->m_invFullSize;
								ProcessSimpleTap(sampleUv, background, bgMvector, *pSrt);
							}
							numSamples += 40;

							if (doSixRing)
							{
								// Get the third ring
								for (uint i = 0; i < 48; ++i)
								{
									float2 sampleUv = uv + UniformSample(i, 48) * (6 * pixelsPerSampleUnit) * pSrt->m_invFullSize;
									ProcessSimpleTap(sampleUv, background, bgMvector, *pSrt);
								}
								numSamples += 48;
							}
						}
					}
				}
			}
		}

		background /= numSamples;
		bgMvector /= numSamples;

		finalColor = background;
		finalMvector = bgMvector;
	}
	else
	{
		float4 mainColor = 0;
		float4 background = 0;
		float4 foreground = 0;

		float2 mainMvector = 0;
		float2 bgMvector = 0;
		float2 fgMvector = 0;

		float numSamples = 0;
		float2 numSamplesBgFg = 0;

		float3 mainPresortInfo = 0;

		if (pSrt->m_separateMiddleLayer)
		{
			mainPresortInfo = pSrt->m_presortBuffer.SampleLevel(pSrt->m_pointSampler, uv, 0);
			float mainAlpha = saturate(SampleAlpha(mainPresortInfo.x));

			mainColor.xyz = (pSrt->m_usePointSampler) ? pSrt->m_colorBuffer.SampleLevel(pSrt->m_pointSampler, uv, 0) : pSrt->m_colorBuffer.SampleLevel(pSrt->m_linearSampler, uv, 0);
			mainColor.a = mainAlpha;
			mainMvector = pSrt->m_mvectorBuffer.SampleLevel(pSrt->m_linearSampler, uv, 0);
		}
		else
		{
			numSamplesBgFg += ProcessTap(uv, 0, spreadScale, mainPresortInfo, background, bgMvector, foreground, fgMvector, *pSrt);
		}
		numSamples += 1;

		int debugMaxSamplesPerRing[6] = { 8, 16, 24, 32, 40, 48 };
		if (pSrt->m_debugShowArc != 0.f)
		{
			for (int i = 0; i < 6; i++)
			{
				float sampleArc = 360.f / (float)debugMaxSamplesPerRing[i];
				debugMaxSamplesPerRing[i] = (int)(pSrt->m_debugShowArc[i] / sampleArc);
			}
		}

		if (doFirstRing)
		{
			numSamplesBgFg += ProcessRing(uv, spreadScale, pixelsPerSampleUnit, mainPresortInfo,
				background, bgMvector, foreground, fgMvector,
				1, debugMaxSamplesPerRing[0], pSrt);
			numSamples += 8;
		}

		if (doSecondRing)
		{
			numSamplesBgFg += ProcessRing(uv, spreadScale, pixelsPerSampleUnit, mainPresortInfo,
				background, bgMvector, foreground, fgMvector,
				2, debugMaxSamplesPerRing[1], pSrt);
			numSamples += 16;
		}

		if (doThirdRing)
		{
			numSamplesBgFg += ProcessRing(uv, spreadScale, pixelsPerSampleUnit, mainPresortInfo,
				background, bgMvector, foreground, fgMvector,
				3, debugMaxSamplesPerRing[2], pSrt);
			numSamples += 24;
		}

		if (doFourRing)
		{
			numSamplesBgFg += ProcessRing(uv, spreadScale, pixelsPerSampleUnit, mainPresortInfo,
				background, bgMvector, foreground, fgMvector,
				4, debugMaxSamplesPerRing[3], pSrt);
			numSamples += 32;
		}

		if (doFiveRing)
		{
			numSamplesBgFg += ProcessRing(uv, spreadScale, pixelsPerSampleUnit, mainPresortInfo,
				background, bgMvector, foreground, fgMvector,
				5, debugMaxSamplesPerRing[4], pSrt);
			numSamples += 40;
		}

		if (doSixRing)
		{
			numSamplesBgFg += ProcessRing(uv, spreadScale, pixelsPerSampleUnit, mainPresortInfo,
				background, bgMvector, foreground, fgMvector,
				6, debugMaxSamplesPerRing[5], pSrt);
			numSamples += 48;
		}

		if (pSrt->m_separateMiddleLayer)
		{
			finalColor = mainColor.xyz;
			finalMvector = mainMvector;

			if (background.a > 0)
			{
				background.xyz /= background.a;
				bgMvector /= background.a;

				finalColor = lerp(background.xyz, mainColor.xyz, mainColor.a);
				finalMvector = lerp(bgMvector, mainMvector, mainColor.a);
			}

			if (foreground.a > 0)
			{
				foreground.xyz /= foreground.a;
				fgMvector /= foreground.a;

				if (pSrt->m_useBetterFgBgClassification)
				{
					foreground.a = saturate(foreground.a * rcp(numSamplesBgFg.y));
					background.a = background.a * rcp(numSamplesBgFg.x);
					alpha = foreground.a / (foreground.a + background.a);
				}
				else
				{
					alpha = saturate(2.0f * rcp(numSamples) * rcp(SampleAlpha(diskSize)) * foreground.a);
				}

				finalColor = lerp(finalColor, foreground.xyz, alpha);
				finalMvector = lerp(finalMvector, fgMvector, alpha);
			}

			if (pSrt->m_debugShowLayer > -1)
			{
				int opt = pSrt->m_debugShowLayer;
				if (opt == 0)
				{
					finalColor = background.xyz;
				}
				else if (opt == 1)
				{
					finalColor = mainColor.xyz;
				}
				else if (opt == 2)	// BG-main blended
				{
					finalColor = lerp(background.xyz, mainColor.xyz, mainColor.a);
				}
				else
				{
					finalColor = foreground.xyz;
				}
			}
		}
		else if (pSrt->m_useBetterFgBgClassification)
		{
			if (background.a > 0)
			{
				background.xyz /= background.a;
				bgMvector /= background.a;
			}

			if (foreground.a > 0)
			{
				foreground.xyz /= foreground.a;
				fgMvector /= foreground.a;
			}

			foreground.a = saturate(2.0f * foreground.a * rcp(numSamplesBgFg.y));
			background.a = background.a * rcp(numSamplesBgFg.x);
			alpha = foreground.a / (foreground.a + background.a);

			finalColor = lerp(background.xyz, foreground.xyz, alpha);
			finalMvector = lerp(bgMvector, fgMvector, alpha);
		}
		else
		{
			if (background.a > 0)
			{
				background.xyz /= background.a;
				bgMvector /= background.a;
			}

			if (foreground.a > 0)
			{
				foreground.xyz /= foreground.a;
				fgMvector /= foreground.a;
			}

			// Bias alpha toward foreground to enable fg to diffuse over bg better (for blurry fg on sharp bg)
			alpha = saturate(2.0f * rcp(numSamples) * rcp(SampleAlpha(diskSize)) * foreground.a);
			finalColor = lerp(background.xyz, foreground.xyz, alpha);
			finalMvector = lerp(bgMvector, fgMvector, alpha);
		}

		if (pSrt->m_debugShowSimpleBlurredPixels)
		{
			pSrt->m_outAlpha[dispatchThreadId] = 0.0f;
		}
	}

	if (pSrt->m_ringOpt == 2)
	{
		float3 debugColor;
		if (doThirdRing)
			debugColor = float3(1, 0.1f, 0.1f);
		else if (doSecondRing)
			debugColor = float3(1, 1, 0.1);
		else if (doFirstRing)
			debugColor = float3(0.1, 1, 0.1);
		else
			debugColor = float3(0.1, 0.1, 1);

		finalColor *= debugColor;
	}

	pSrt->m_outBuffer[dispatchThreadId] = finalColor;
	if (bBlurMvector)
		pSrt->m_outMvectorBuffer[dispatchThreadId] = finalMvector;

	if (pSrt->m_useForegroundAlpha)
	{
		pSrt->m_outAlpha[dispatchThreadId] = alpha;
	}
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofBlurHirez(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	BlurSrt *pSrt : S_SRT_DATA)
{
	BokehDofBlurHirez(dispatchThreadId, pSrt, false);
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofBlurHirezWithMvector(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	BlurSrt *pSrt : S_SRT_DATA)
{
	BokehDofBlurHirez(dispatchThreadId, pSrt, true);
}

////////////////////////////////////////////////////////////////////////////////////////////
// 3x3 Median filter
// http://users.utcluj.ro/~baruch/resources/Image/xl23_16.pdf
////////////////////////////////////////////////////////////////////////////////////////////

struct MedianSrt
{
	Texture2D<float3> m_srcTex;
	Texture2D<float2> m_srcMvectorTex;
	RW_Texture2D<float3> m_dstTex;
	RW_Texture2D<float2> m_dstMvectorTex;
};

void BokehDofMedianFilter(int2 dispatchThreadId, MedianSrt *pSrt, bool bBlurMvector)
{
	// TODO: Perhaps preload the data into LDS?

	// Helpers to make this code shorter
	Texture2D<float3> src = pSrt->m_srcTex;
	int2 id = dispatchThreadId;

	float3 p[9] = {
		src[id - int2(1, 1)],	src[id - int2(0, 1)],	src[id + int2(1, -1)],
		src[id - int2(1, 0)],	src[id],				src[id + int2(1, 0)],
		src[id + int2(-1, 1)],	src[id + int2(0, 1)],	src[id + int2(1, 1)],
	};

	// Build up the tree node min/maxes from the top down

	float3 n20max = max3(p[0], p[1], p[2]);
	float3 n20min = med3(p[0], p[1], p[2]);
	float3 n10min = min3(p[0], p[1], p[2]);

	float3 n21max = max3(p[3], p[4], p[5]);
	float3 n21min = med3(p[3], p[4], p[5]);
	float3 n11min = min3(p[3], p[4], p[5]);

	float3 n22max = max3(p[6], p[7], p[8]);
	float3 n22min = med3(p[6], p[7], p[8]);
	float3 n12min = min3(p[6], p[7], p[8]);

	float3 n30max = max(n10min, n11min);
	float3 n31min = min(n21max, n22max);

	float3 n50max = max(n30max, n12min);
	float3 n52min = min(n20max, n31min);

	float3 n60min = med3(n20min, n21min, n22min);
	float3 answer = med3(n50max, n60min, n52min);

	pSrt->m_dstTex[id] = answer;

	if (bBlurMvector)
	{
		Texture2D<float2> srcMvec = pSrt->m_srcMvectorTex;

		float2 pMvec[9] = {
			srcMvec[id - int2(1, 1)],	srcMvec[id - int2(0, 1)],	srcMvec[id + int2(1, -1)],
			srcMvec[id - int2(1, 0)],	srcMvec[id],				srcMvec[id + int2(1, 0)],
			srcMvec[id + int2(-1, 1)],	srcMvec[id + int2(0, 1)],	srcMvec[id + int2(1, 1)],
		};

		// Build up the tree node min/maxes from the top down

		float2 n20maxM = max3(pMvec[0], pMvec[1], pMvec[2]);
		float2 n20minM = med3(pMvec[0], pMvec[1], pMvec[2]);
		float2 n10minM = min3(pMvec[0], pMvec[1], pMvec[2]);

		float2 n21maxM = max3(pMvec[3], pMvec[4], pMvec[5]);
		float2 n21minM = med3(pMvec[3], pMvec[4], pMvec[5]);
		float2 n11minM = min3(pMvec[3], pMvec[4], pMvec[5]);

		float2 n22maxM = max3(pMvec[6], pMvec[7], pMvec[8]);
		float2 n22minM = med3(pMvec[6], pMvec[7], pMvec[8]);
		float2 n12minM = min3(pMvec[6], pMvec[7], pMvec[8]);

		float2 n30maxM = max(n10minM, n11minM);
		float2 n31minM = min(n21maxM, n22maxM);

		float2 n50maxM = max(n30maxM, n12minM);
		float2 n52minM = min(n20maxM, n31minM);

		float2 n60minM = med3(n20minM, n21minM, n22minM);
		float2 answerM = med3(n50maxM, n60minM, n52minM);

		pSrt->m_dstMvectorTex[id] = answerM;
	}
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofMedianFilter(int2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	MedianSrt *pSrt : S_SRT_DATA)
{
	BokehDofMedianFilter(dispatchThreadId, pSrt, false);
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofMedianFilterWithMvector(int2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	MedianSrt *pSrt : S_SRT_DATA)
{
	BokehDofMedianFilter(dispatchThreadId, pSrt, true);
}

////////////////////////////////////////////////////////////////////////////////////////////
// Upres filter
////////////////////////////////////////////////////////////////////////////////////////////

struct UpresSrt
{
	Texture2D<float3>	m_srcHalfColor;
	Texture2D<float3>	m_srcHalfMvector;
	Texture2D<float>	m_srcHalfAlpha;
	Texture2D<float3>	m_presortBuffer;
	Texture2D<float3>	m_srcFullColor;
	Texture2D<float2>	m_srcFullMvector;
	Texture2D<float>	m_srcFullDepth;
	Texture2D<float>	m_srcCoc;
	Texture2D<float4>	m_tileDataNeigh;
	Texture2D<float4>	m_tileData;
	RW_Texture2D<float3>	m_outColor;
	RW_Texture2D<float2>	m_outMVector;

	SamplerState		m_pointSampler;
	SamplerState		m_linearSampler;

	DofBlurConstants	 *consts;
	DofWeaponScopeParams *scopeParams;

	float				m_cocOverride;
	float				m_depthScaleForeground;
	uint				m_enableDepthWarp;
	float				m_scaleForNonResolutionRelated;

	float2				m_invFullSize;
	float2				m_samples[4];

	int					m_useForegroundAlpha;
	uint				m_useBetterFgBgClassification;
	uint				m_useMixedClassification;
	uint				m_useRawFgAlphaAsBlendFactor;
	int					m_debugBlendFactor;
	int					m_debugCoc;

	float				m_aspect;
};

void BokehDofUpres(uint2 dispatchThreadId, UpresSrt *pSrt, bool bBlurMvector)
{
	const float2 uv = (dispatchThreadId + 0.5f) * pSrt->m_invFullSize;

	// Calculate the full-res coc
	float linearDepth = 1.0f / (pSrt->m_srcFullDepth[dispatchThreadId] * pSrt->consts->m_linearDepthParams.x + pSrt->consts->m_linearDepthParams.y);
	float coc = (abs(GetCoc(linearDepth, pSrt->consts)) + pSrt->m_srcCoc[dispatchThreadId]) * pSrt->m_scaleForNonResolutionRelated;
	float unclampedCoc = coc;

	// override dof coc on outside of scope when zoomed in with rifle
	DofWeaponScopeParams* pScopeParams = pSrt->scopeParams;
	if (pScopeParams->m_scopePostProcessBlend > 0.0f)
	{
		float2 aspectCorrectCenter = pScopeParams->m_scopeSSCenter * float2(pSrt->m_aspect, 1.0f);
		float2 aspectCorrectUV = uv * float2(pSrt->m_aspect, 1.0f);
		float2 ssDisp = aspectCorrectUV - aspectCorrectCenter;
		if (length(ssDisp) >= pScopeParams->m_scopeSSRadius)
		{
			coc = lerp(coc, float2(pScopeParams->m_scopeDofCoc), pScopeParams->m_scopePostProcessBlend);
			unclampedCoc = coc;
		}
	}

	coc = max(coc, 0.5f);

	if (pSrt->m_cocOverride)
	{
		coc = pSrt->m_cocOverride;
		unclampedCoc = pSrt->m_cocOverride;
	}

	const float4 tileDataNeigh = pSrt->m_tileDataNeigh[dispatchThreadId / 16] * pSrt->m_scaleForNonResolutionRelated;
	const float4 tileData = pSrt->m_tileData[dispatchThreadId / 16] * pSrt->m_scaleForNonResolutionRelated;
	const float maxCoc = tileDataNeigh.y;
	const float minCoc = tileDataNeigh.x;

	// This is the case where the implementation gets very fuzzy....

	float3 color;
	float2 mvector;
	float combinedFactor;

	// Branch out into a super simple version that just returns the full-res color if 
	// the max coc is small enough
	if (DoesTileUseFullRes(maxCoc))
	{
		color = pSrt->m_srcFullColor[dispatchThreadId];
		pSrt->m_outColor[dispatchThreadId] = color;
		if (pSrt->m_debugCoc)
			pSrt->m_outColor[dispatchThreadId] = float3(unclampedCoc, maxCoc, minCoc);

		mvector = pSrt->m_srcFullMvector[dispatchThreadId];
		if (bBlurMvector)
			pSrt->m_outMVector[dispatchThreadId] = mvector;
		return;
	}
	else
	{
		combinedFactor = 1.0f - saturate(coc - 0.5f);

		if (pSrt->m_useForegroundAlpha)
		{
			// Recompute background factor at full res instead of sampling it from half res presort buffer.
			// This is not only cheaper than performing a bilateral upscale but also significantly improves
			// out-of-focus foreground edges.
			bool bUseBetterFgBgClassification = pSrt->m_useBetterFgBgClassification;
			if (pSrt->m_useMixedClassification)
			{
				bUseBetterFgBgClassification = linearDepth > (pSrt->consts->m_focusPlaneDist + 0.25f);
			}

			float2 depthCmp = DepthCmp2(float2(linearDepth, linearDepth), tileDataNeigh.z, tileDataNeigh.w, pSrt->m_depthScaleForeground, pSrt->m_enableDepthWarp, bUseBetterFgBgClassification, false);
			float bgFactor = depthCmp.x;

			float fgAlpha = pSrt->m_srcHalfAlpha.SampleLevel(pSrt->m_linearSampler, uv, 0);

			// Bias the fg alpha if above zero. This helps on the edges of blurry FG objects where the alpha is lower than 1, especially since we're going to blend AGAIN with the fullres color.
			// This bias helps offset the effect of blending away the fg color twice and maintains the FG diffuse over BG even after the Upres step (compare to the Downsampled Color buffer).
			fgAlpha = saturate(fgAlpha + ceil(fgAlpha) * 0.5f);

			// For certain cases (eg. where blurry FG diffuses onto other FG), the BG factor may not be strong enough as the "BG" is also FG relative to other pixels in the tile. So it makes sense to disregard it here for certain shots
			if (pSrt->m_useRawFgAlphaAsBlendFactor)
			{
				bgFactor = 1.0f;
			}

			// Bias the factor toward the half-res texture if:
			// 1. The given pixel is a background pixel with respect to its neighborhood
			// 2. The foreground alpha is high enough
			// (Based on how fg over bg blur is accomplished in the blur step)
			combinedFactor *= saturate(1.0f - bgFactor * fgAlpha);
		}

		bool hasHalfRes = combinedFactor < 0.95;
		bool hasFullRes = combinedFactor > 0.05;

		float3 halfRes = 0;
		float2 halfResMvector = 0;

		// Only sample the half-res buffer if we actually need it
		if (hasHalfRes)
		{
			halfRes = pSrt->m_srcHalfColor.SampleLevel(pSrt->m_linearSampler, uv, 0);
			halfResMvector = pSrt->m_srcHalfMvector.SampleLevel(pSrt->m_linearSampler, uv, 0);
		}

		float3 fullRes;
		float2 fullMvector;

		if (hasFullRes)
		{
			fullRes = pSrt->m_srcFullColor.SampleLevel(pSrt->m_pointSampler, uv, 0);
			fullMvector = pSrt->m_srcFullMvector.SampleLevel(pSrt->m_pointSampler, uv, 0);

			if (!hasHalfRes)
			{
				halfRes = fullRes;
				halfResMvector = fullMvector;
			}
		}
		else
		{
			fullRes = halfRes;
			fullMvector = halfResMvector;
		}

		color = lerp(halfRes, fullRes, combinedFactor);
		mvector = lerp(halfResMvector, fullMvector, combinedFactor);
	}

	if (pSrt->m_debugBlendFactor)
	{
		color = lerp(float3(1, 0, 0), float3(0, 0, 1), combinedFactor);
	}

	pSrt->m_outColor[dispatchThreadId] = color;

	if (pSrt->m_debugCoc)
		pSrt->m_outColor[dispatchThreadId] = float3(unclampedCoc, maxCoc, minCoc);

	if (bBlurMvector)
		pSrt->m_outMVector[dispatchThreadId] = mvector;
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofUpres(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	UpresSrt *pSrt : S_SRT_DATA)
{
	BokehDofUpres(dispatchThreadId, pSrt, false);
}

[NUM_THREADS(8, 8, 1)]
void Cs_BokehDofUpresWithMvector(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID,
	UpresSrt *pSrt : S_SRT_DATA)
{
	BokehDofUpres(dispatchThreadId, pSrt, true);
}