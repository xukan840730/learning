/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "global-constants.fxi"
#include "global-funcs.fxi"
#include "packing.fxi"
#include "post-globals.fxi"
#include "post-processing-common.fxi"
#include "color-util.fxi"

#define kMotionBlurEpsilon 0.01f
#define kMinMotionBlurLength 0.5f

// Mirror of MotionBlurNew::MotionBlurNewDebugMode enum
#define kMotionBlurNewDebugModeNone 0
#define kMotionBlurNewDebugTileMax 1
#define kMotionBlurNewDebugNeighborMax 2
#define kMotionBlurNewDebugSampleNoise 3
#define kMotionBlurNewDebugMotionBlurAlpha 4
#define kMotionBlurNewDebugMotionBlurColor 5
#define kMotionBlurNewDebugMotionVectors 6
#define kMotionBlurNewDebugHalfSpreadVelocity 7
#define kMotionBlurNewDebugUserCustom 8

struct SamplerTable
{
	SamplerState				g_linearSampler;
	SamplerState				g_pointSampler;
	SamplerComparisonState		g_shadowSampler;
	SamplerState				g_linearClampSampler;
	SamplerState				g_linearClampUSampler;
	SamplerState				g_linearClampVSampler;
};

float SquaredLength(float2 vec)
{
	return dot(vec, vec);
}

float SquaredLength(float x, float y)
{
	return SquaredLength(float2(x, y));
}

float FastSqrt(float f)
{
	return fastSqrtNR0(f);
}

float FastSqrtPrecise(float f)
{
	return fastSqrtNR2(f);
}

float FastRcpSqrt(float f)
{
	return fastRcpSqrtNR0(f);
}

float FastRcpSqrtPrecise(float f)
{
	return fastRcpSqrtNR2(f);
}

float FastRcp(float f)
{
	return fastRcpNR0(f);
}

float FastRcpPrecise(float f)
{
	return fastRcpNR2(f);
}

float2 FastRcp(float2 f)
{
	return float2(fastRcpNR0(f.x), fastRcpNR0(f.y));
}

float2 FastRcpPrecise(float2 f)
{
	return float2(fastRcpNR2(f.x), fastRcpNR2(f.y));
}

float FastLength(float2 v)
{
	return length(v);
	//return FastSqrt(SquaredLength(v, v));
}

float FastLengthPrecise(float2 v)
{
	return length(v);
	//return FastSqrtPrecise(SquaredLength(v, v));
}

float2 FastNormalize(float2 v)
{
	return normalize(v);
	//return v * FastRcp(FastLength(v));
}

float2 FastNormalizePrecise(float2 v)
{
	return normalize(v);
	//return v * FastRcpPrecise(FastLengthPrecise(v));
}

// Find max length vec2 within the wavefront
void FullMaxReduce(float2 value, inout float2 maxValue)
{
	maxValue = value;

	// Do a simple reduction using lane swizzling
	float maxValueX = value.x;
	float maxValueY = value.y;

	// Step 1: Get 16 (flip the 5th bit)
	float reduceX = LaneSwizzle(maxValueX, 0x1F, 0x00, 0x10);
	float reduceY = LaneSwizzle(maxValueY, 0x1F, 0x00, 0x10);
	
	if (SquaredLength(maxValueX, maxValueY) < SquaredLength(reduceX, reduceY))
	{
		maxValueX = reduceX;
		maxValueY = reduceY;
	}

	// Step 2: Get the max of the 8th lane
	reduceX = LaneSwizzle(maxValueX, 0x1F, 0x00, 0x8);
	reduceY = LaneSwizzle(maxValueY, 0x1F, 0x00, 0x8);
	
	if (SquaredLength(maxValueX, maxValueY) < SquaredLength(reduceX, reduceY))
	{
		maxValueX = reduceX;
		maxValueY = reduceY;
	}

	// Step 3: Get the max of the 4th lane
	reduceX = LaneSwizzle(maxValueX, 0x1F, 0x00, 0x4);
	reduceY = LaneSwizzle(maxValueY, 0x1F, 0x00, 0x4);
	
	if (SquaredLength(maxValueX, maxValueY) < SquaredLength(reduceX, reduceY))
	{
		maxValueX = reduceX;
		maxValueY = reduceY;
	}

	// Step 4: Get the max of the 2nd lane
	reduceX = LaneSwizzle(maxValueX, 0x1F, 0x00, 0x2);
	reduceY = LaneSwizzle(maxValueY, 0x1F, 0x00, 0x2);
	
	if (SquaredLength(maxValueX, maxValueY) < SquaredLength(reduceX, reduceY))
	{
		maxValueX = reduceX;
		maxValueY = reduceY;
	}

	// Step 5: Get the max of the 1st lane
	reduceX = LaneSwizzle(maxValueX, 0x1F, 0x00, 0x1);
	reduceY = LaneSwizzle(maxValueY, 0x1F, 0x00, 0x1);
	
	if (SquaredLength(maxValueX, maxValueY) < SquaredLength(reduceX, reduceY))
	{
		maxValueX = reduceX;
		maxValueY = reduceY;
	}

	// Finally, get the max of the other group of 32 registers
	float maxValueAx = ReadLane(maxValueX, 0);
	float maxValueAy = ReadLane(maxValueY, 0);
	float maxValueBx = ReadLane(maxValueX, 32);
	float maxValueBy = ReadLane(maxValueY, 32);

	if (SquaredLength(maxValueAx, maxValueAy) > SquaredLength(maxValueBx, maxValueBx))
	{
		maxValue = float2(maxValueAx, maxValueAy);
	}
	else
	{
		maxValue = float2(maxValueBx, maxValueBy);
	}
}

void FullMaxReduce16(float2 value, inout float2 maxValue)
{
	maxValue = value;

	// Do a simple reduction using lane swizzling
	float maxValueX = value.x;
	float maxValueY = value.y;

	// Step 1: Get the max of the 8th lane
	float reduceX = LaneSwizzle(maxValueX, 0x1F, 0x00, 0x8);
	float reduceY = LaneSwizzle(maxValueY, 0x1F, 0x00, 0x8);

	if (SquaredLength(maxValueX, maxValueY) < SquaredLength(reduceX, reduceY))
	{
		maxValueX = reduceX;
		maxValueY = reduceY;
	}

	// Step 2: Get the max of the 4th lane
	reduceX = LaneSwizzle(maxValueX, 0x1F, 0x00, 0x4);
	reduceY = LaneSwizzle(maxValueY, 0x1F, 0x00, 0x4);

	if (SquaredLength(maxValueX, maxValueY) < SquaredLength(reduceX, reduceY))
	{
		maxValueX = reduceX;
		maxValueY = reduceY;
	}

	// Step 3: Get the max of the 2nd lane
	reduceX = LaneSwizzle(maxValueX, 0x1F, 0x00, 0x2);
	reduceY = LaneSwizzle(maxValueY, 0x1F, 0x00, 0x2);

	if (SquaredLength(maxValueX, maxValueY) < SquaredLength(reduceX, reduceY))
	{
		maxValueX = reduceX;
		maxValueY = reduceY;
	}

	// Step 4: Get the max of the 1st lane
	reduceX = LaneSwizzle(maxValueX, 0x1F, 0x00, 0x1);
	reduceY = LaneSwizzle(maxValueY, 0x1F, 0x00, 0x1);

	if (SquaredLength(maxValueX, maxValueY) < SquaredLength(reduceX, reduceY))
	{
		maxValueX = reduceX;
		maxValueY = reduceY;
	}

	// Lane 0 will have the max
	maxValue = float2(ReadLane(maxValueX, 0), ReadLane(maxValueY, 0));
}



// NOTES(Stephen Merendino): I lifted this from Random.usf of Unreal Engine
//
// high frequency dither pattern appearing almost random without banding steps
// note: from "NEXT GENERATION POST PROCESSING IN CALL OF DUTY: ADVANCED WARFARE"
//      http://advances.realtimerendering.com/s2014/index.html
// Epic extended by FrameId
// ~7 ALU operations (2 frac, 3 mad, 2 *)
// @return 0..1
float InterleavedGradientNoise(float2 uv, float FrameId)
{
	// magic values are found by experimentation
	uv += FrameId * (float2(47, 17) * 0.695f);

	const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
	return frac(magic.z * frac(dot(uv, magic.xy)));
}

// [0, 1[
// ~10 ALU operations (2 frac, 5 *, 3 mad)
float RandFast(uint2 PixelPos, float Magic = 3571.0)
{
	float2 Random2 = (1.0 / 4320.0) * PixelPos + float2(0.25, 0.0);
	float Random = frac(dot(Random2 * Random2, Magic));
	Random = frac(Random * Random * (2 * Magic));
	return Random;
}

// This one is from the Call of Duty Post Processing slides
float RandDitherSpatial(uint2 pixelPos, float scale = 1.0f)
{
	float2 positionMod = float2(pixelPos & 1);
	return (-scale + 2.0f * scale * positionMod.x) * (-1.0f + 2.0f * positionMod.y);
}

float RandDitherTemporal(uint2 pixelPos, uint frameCountMod, float scale = 1.0f)
{
	float2 positionMod = float2(pixelPos & 1);
	float temporal = 0.5f * scale * (-1.0f + 2.0f * frameCountMod);
	return temporal;
}

float RandDither(uint2 pixelPos, uint frameCountMod)
{
	return RandDitherSpatial(pixelPos, 0.5f);
}

float VanDerCorput(int i)
{
	/*const float kVanDerCorput[] = {	0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 
									0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
									0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
									0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
									0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
									0.0f, 0.0f, 0.0f, 0.0f, 0.0f };*/

	const float kVanDerCorput[] = { 0.37500000, -0.37500000, 0.12500000, -0.2500000, 0.25000000,
									-0.5000000, 0.00000000, -0.0625000, 0.43750000, -0.3125000,
									0.18750000, -0.1875000, 0.31250000, -0.4375000, 0.06250000,
									-0.0937500, 0.40625000, -0.3437500, 0.15625000, -0.2187500,
									0.28125000,	-0.4687500, 0.03125000, -0.0312500, 0.46875000,
									-0.2812500, 0.21875000, -0.1562500, 0.34375000, -0.40625000 };

	return kVanDerCorput[i];
}

/*  Half spread velocity based on McGuire 2012 Section 2.2

	NOTES (Stephen Merendino):
	frameTimeExposurePercent is a user adjustable param in the range [0,1] that represents (exposureTime) * (frameRate) from the literature.
	
	Framerate for us is 30 frames per second (for T2) which means that a single frame is produced every 0.03333~ ms.
	Exposure can never be MORE than that because you can't expose an image after you have stopped capturing the image.
	
	If you expose for the full frame interval (0.033333ms) and multiply by framerate (30 frames/second) you get 1.0.
	
	This means that you are exposing for the full frame and therefore you don't want to scale down the velocity vector at all.
	If you expose for LESS than the full frame (say half frame = 1/30 * 1/2 = 1/60 = 0.0166666~ ms) then you get a scale of 0.5f.
	
	In the end, it made sense to me to let this be a tweakable param to let artist have control over exposure as a way of tweaking the strength
	of the motion blur. Exposing for less than full frame interval time means the blur won't be as strong.

	Right now this exposure setting is only used here in this motion blur algorithm, but maybe we should have a game wide exposure setting?
 */
float2 CalculateHalfSpreadVelocity(float2 sourceMotionVector, uint2 motionVectorBufferDims, uint tileSize, float frameTimeExposurePercent)
{
	// need to convert UV space sourceMotionVector to pixel space vector (multiply motion vector by motion vector buffer size)
	float2 motionVecPixels = sourceMotionVector * ((float2)motionVectorBufferDims);

	// qx matches the naming convention used in McGuire 2012
	// multiply by 0.5f because we sample forwards and backwards from starting position in reconstruction
	float2 qx = 0.5f * motionVecPixels/* * frameTimeExposurePercent*/;
	float qxMagnitude = length(qx);

	// So this looks complicated, but what its doing is simple. We are just making sure that the magnitude of our velocity can not extend past our tile size.
	float2 halfSpreadVelocity = qx * max(kMinMotionBlurLength, min(qxMagnitude, (float)tileSize));
	//halfSpreadVelocity /= (qxMagnitude + kMotionBlurEpsilon);
	halfSpreadVelocity *= FastRcp(qxMagnitude + kMotionBlurEpsilon);

	/*float halfSpreadVelocityMagnitude = max(kMinMotionBlurLength, min(qxMagnitude, (float)tileSize));
	float2 halfSpreadVelocity = normalize(qx) * halfSpreadVelocityMagnitude;*/

	return halfSpreadVelocity;
}

#define USE_MAGNITUDE_WEIGHTING 1

float Cone(float2 pixelPosX, float2 pixelPosY, float halfSpreadVelocityMagnitude)
{
	float pixelDisp = FastLength(pixelPosX - pixelPosY);
	float velLength = halfSpreadVelocityMagnitude;

	/**/
	if (pixelDisp <= velLength)
	{
		return 1.0f;
	}
	else
	{
		return 0.0f;
	}
	/*/
	return clamp(1.0f - pixelDisp / velLength, 0.0f, 1.0f);
	/**/
}

float Cone(float2 pixelPosX, float2 pixelPosY, float2 halfSpreadVelocity)
{
	return Cone(pixelPosX, pixelPosY, FastLength(halfSpreadVelocity));
}

float Cylinder(float2 pixelPosX, float2 pixelPosY, float2 halfSpreadVelocity)
{
#if USE_MAGNITUDE_WEIGHTING
	float pixelDisp = length(pixelPosX - pixelPosY);
	float velLength = length(halfSpreadVelocity);

	return 1.0f - smoothstep(0.95f * velLength, 1.05 * velLength, pixelDisp);
#else
	return 1.0f;
#endif
}

float Cylinder(float2 pixelPosX, float2 pixelPosY, float halfSpreadVelocityMagnitude)
{
#if USE_MAGNITUDE_WEIGHTING
	float pixelDisp = FastLength(pixelPosX - pixelPosY);
	float velLength = halfSpreadVelocityMagnitude;

	return 1.0f - smoothstep(0.95f * velLength, 1.05 * velLength, pixelDisp);
#else
	return 1.0f;
#endif
}

float SoftDepthCompare(float linearDepthA, float linearDepthB, float zExtent)
{
	// We never want things behind other things to blur on top
	if (linearDepthA > linearDepthB)
		return 0.0f;

	/**/
	// Mcguire 2014 continuous depth compare
	return saturate(1.0f - ((linearDepthA - linearDepthB) * FastRcp(min(linearDepthA, linearDepthB))));
	/*/
	// McGuire 2012 user controlled depth compare
	linearDepthA *= linearDepthA;
	linearDepthB *= linearDepthB;
	return clamp(1.0f - (linearDepthA - linearDepthB) / zExtent, 0.0f, 1.0f);
	/**/
}

// Takes view space depth and normalizes it based on camera near and far distances
float NormalizeLinearDepth(float linearDepth, float2 cameraClipDistances)
{
	float normalizedLinearDepth = (linearDepth - cameraClipDistances.x) / (cameraClipDistances.y - cameraClipDistances.x);
	return saturate(normalizedLinearDepth);
}

//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------


struct ExtractCharacterMaskSrt
{
	Texture2D<uint4> m_gbuffer1;
	RWTexture2D<uint> m_characterMask;
	uint m_isHalfRes;
};

[numthreads(8, 8, 1)]
void CS_MotionBlurExtractCharacterMask(uint3 dispatchThreadId : SV_DispatchThreadId, ExtractCharacterMaskSrt* pSrt : S_SRT_DATA)
{
	pSrt->m_characterMask[dispatchThreadId.xy] = pSrt->m_gbuffer1[dispatchThreadId.xy].w & MASK_BIT_EXTRA_CHARACTER;
}

//--------------------------------------------------------------------------------------
struct ApplyVignetteBlurSrt
{
	Texture2D<float3> m_primaryFloat;
	RWTexture2D<float3> m_vignetteResult;
	RWTexture2D<float3> m_vignetteAlpha;

	uint m_maxBlurRadius;
	uint m_numSamples;
	float2 m_invOutputBufferDims;
	float2 m_invColorBufferDims;
	float m_vignetteBlurScale;
	float m_vignetteBlurStartRadius;
	float m_vignetteBlurStartRange;
	float m_vignetteBlurStrength;
	float m_verticalScale;
	float2 m_scopeSSCenter;
	float m_scopeSSRadius;
	float m_scopePostProcessBlend;

	SamplerTable *m_pSamplers;
};

[numthreads(8, 8, 1)]
void CS_MotionBlurApplyVignetteBlur(uint3 dispatchThreadId : SV_DispatchThreadId, ApplyVignetteBlurSrt* pSrt : S_SRT_DATA)
{
	uint2 pixelXY = dispatchThreadId.xy;

	float2 uv = ((float2)pixelXY + float2(0.5f)) * pSrt->m_invOutputBufferDims;
	float2 radialUv = uv - 0.5f;

	radialUv.y *= pSrt->m_verticalScale;

	float radialUvLen = length(radialUv);

	radialUvLen *= pSrt->m_vignetteBlurStrength;

	float radialBlurStrength = saturate((radialUvLen - pSrt->m_vignetteBlurStartRadius) / pSrt->m_vignetteBlurStartRange);

	if (radialUvLen >= pSrt->m_vignetteBlurStartRadius && radialBlurStrength > 0.0f)
	{
		float2 singlePixelStep = pSrt->m_invColorBufferDims;
		float2 radialSinglePixelStep = normalize(radialUv * (16.0f / 9.0f)) * singlePixelStep;

		float blurRadius = (float)pSrt->m_maxBlurRadius * radialBlurStrength;
		uint numSamplesClamped = clamp(pSrt->m_numSamples, 1, (uint)blurRadius);

		float samplePixelSpacing = blurRadius / (float)numSamplesClamped;

		float2 sampleUv = uv;

		float2 iterStep = -radialSinglePixelStep * samplePixelSpacing;

		float3 accum = float3(0.0f);
		float weight = 0.0f;
		for (uint curSample = 0; curSample < numSamplesClamped; curSample++)
		{
			accum += pSrt->m_primaryFloat.SampleLevel(pSrt->m_pSamplers->g_linearClampSampler, sampleUv, 0);

			weight += 1.0f;

			sampleUv += iterStep;
		}

		accum /= weight;

		pSrt->m_vignetteResult[pixelXY] = accum;
		pSrt->m_vignetteAlpha[pixelXY] = radialBlurStrength;
	}
	else
	{
		pSrt->m_vignetteResult[pixelXY] = pSrt->m_primaryFloat.SampleLevel(pSrt->m_pSamplers->g_linearClampSampler, uv, 0);
		pSrt->m_vignetteAlpha[pixelXY] = 0.0f;
	}
}

float2 CalculatePhotoModeMotionVector(uint2 pixelXY, Texture2D<float> primaryDepth, Texture2D<float4> motionVectorFullRes, float2 linearDepthParams, float4 fullSizeInfo, float4x4 currentToLastFrameMat, float4x4 currentToFakeLastFrameMat, float2 projectionJitterOffsets)
{
	float srcDepth = primaryDepth[pixelXY];
	float currentDepthVS = GetDepthVS(srcDepth, linearDepthParams);

	float2 uvSS = float2((pixelXY + 0.5f) * fullSizeInfo.zw);

	float ssX = (uvSS.x - 0.5f) * 2.0f;
	float ssY = (uvSS.y - 0.5f) * -2.0f;

	float4 inputPositionSS = float4(ssX, ssY, srcDepth, 1.0f) * currentDepthVS;
	float4 lastPositionSS = mul(inputPositionSS, currentToLastFrameMat);
	float4 fakeLastPositionSS = mul(inputPositionSS, currentToFakeLastFrameMat);

	if (fakeLastPositionSS.w < 0.02f)
		fakeLastPositionSS = inputPositionSS + (fakeLastPositionSS - inputPositionSS) * (0.01f - inputPositionSS.w) / (fakeLastPositionSS.w - inputPositionSS.w);

	lastPositionSS.xy = lastPositionSS.xy / lastPositionSS.w * float2(0.5f, -0.5f) + 0.5f;
	fakeLastPositionSS.xy = fakeLastPositionSS.xy / fakeLastPositionSS.w * float2(0.5f, -0.5f) + 0.5f;

	float2 motionVectorCalculate = (lastPositionSS.xy - uvSS + projectionJitterOffsets) * fullSizeInfo.xy;

	return motionVectorFullRes[pixelXY] + (fakeLastPositionSS.w > 0.01f ? (fakeLastPositionSS.xy - uvSS) : 0);
}

//--------------------------------------------------------------------------------------
struct PrepareInputDataFullResSrt
{
	Texture2D<float4> m_motionVectorFullRes;
	Texture2D<float3> m_primaryFloatFullRes;
	Texture2D<float> m_primaryDepth;
	Texture2D<uint> m_temporalAADepthInfo;
	RWTexture2D<float4> m_velocityAndLinearDepthResult;
	RWTexture2D<float2> m_tileMaxVelocity;

	uint m_tileSize;
	uint2 m_motionVectorBufferDims;
	float2 m_linearDepthParams;
	float2 m_cameraClipDistances;
	float m_frameTimeExposurePercent;
	uint m_rejectTemporalEdgeSamples;
	uint m_isHalfRes;
	float2 m_projectionJitterOffsets;
	float4x4 m_currentToLastFrameMat;
	float4x4 m_currentToFakeLastFrameMat;
	float4 m_fullSizeInfo;
};

groupshared float2 s_wavefrontMaxVelocitiesTileSize32[16];
groupshared float s_wavefrontMaxMagnitudesTileSize32[16];

[numthreads(32, 32, 1)]
void CS_MotionBlurPrepareInputDataFullResTileSize32(uint3 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, PrepareInputDataFullResSrt* pSrt : S_SRT_DATA)
{
	uint2 pixelXY = dispatchThreadId.xy;
	uint tileSize = pSrt->m_tileSize;

#if PHOTO_MODE_ENABLED
	float2 motionVec = CalculatePhotoModeMotionVector(pixelXY, pSrt->m_primaryDepth, pSrt->m_motionVectorFullRes, pSrt->m_linearDepthParams, pSrt->m_fullSizeInfo, pSrt->m_currentToLastFrameMat, pSrt->m_currentToFakeLastFrameMat, pSrt->m_projectionJitterOffsets);
#else
	float2 motionVec = pSrt->m_motionVectorFullRes[pixelXY];
#endif

	// edge detection from temporal aa depth info
	bool bEdge = false;
	if (pSrt->m_rejectTemporalEdgeSamples)
	{
		uint depthInfo = pSrt->m_temporalAADepthInfo[pixelXY];
		bEdge = depthInfo & 0x80;
	}

	// velocity
	float2 halfSpreadVelocity = CalculateHalfSpreadVelocity(motionVec, pSrt->m_motionVectorBufferDims, tileSize, pSrt->m_frameTimeExposurePercent);

	// depth
	float perspectiveDepth = pSrt->m_primaryDepth[pixelXY];
	float srcLinearDepth = GetLinearDepth(perspectiveDepth, pSrt->m_linearDepthParams);
	srcLinearDepth = NormalizeLinearDepth(srcLinearDepth, pSrt->m_cameraClipDistances);

	pSrt->m_velocityAndLinearDepthResult[pixelXY] = float4(halfSpreadVelocity, length(halfSpreadVelocity), srcLinearDepth * (bEdge ? -1.0f : 1.0f));

	// Tile Max Velocity
	{
		// 1. Setup different params we'll need
		uint2 tileXY = groupId;
		uint threadIndex = groupThreadId.y * 32 + groupThreadId.x;

		// 2. Load single motion vector for the thread
		float2 srcVelocity = motionVec;

		// 3. Reduce within wavefront and store max value into groupshared memory after memory barrier to make sure every thread is loaded and ready
		GroupMemoryBarrierWithGroupSync();

		float2 wavefrontMaxVelocity = float2(0.0f);
		FullMaxReduce(srcVelocity, wavefrontMaxVelocity);

		// only one lane per wavefront writes out the wavefront max value
		if (threadIndex % 64 == 0)
		{
			float wavefrontMaxVelocityLengthSq = SquaredLength(wavefrontMaxVelocity);
			uint wavefrontIndex = (threadIndex >> 6);

			s_wavefrontMaxVelocitiesTileSize32[wavefrontIndex] = wavefrontMaxVelocity;
			s_wavefrontMaxMagnitudesTileSize32[wavefrontIndex] = wavefrontMaxVelocityLengthSq;
		}

		// sync point again to make sure lds to fully prepared
		GroupMemoryBarrierWithGroupSync();

		// 4. First thread of threadgroup will loop through the values that the wavefronts computed and write out the final max
		if (all(groupThreadId == uint2(0, 0)))
		{
			float maxMagnitude = 0.0f;
			uint maxIndex = 0;

			for (uint i = 0; i < 16; i++)
			{
				if (s_wavefrontMaxMagnitudesTileSize32[i] > maxMagnitude)
				{
					maxMagnitude = s_wavefrontMaxMagnitudesTileSize32[i];
					maxIndex = i;
				}
			}

			pSrt->m_tileMaxVelocity[tileXY] = s_wavefrontMaxVelocitiesTileSize32[maxIndex];
		}
	}
}


groupshared float2 s_wavefrontMaxVelocitiesTileSize24[9];
groupshared float s_wavefrontMaxMagnitudesTileSize24[9];

[numthreads(24, 24, 1)]
void CS_MotionBlurPrepareInputDataFullResTileSize24(uint3 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, PrepareInputDataFullResSrt* pSrt : S_SRT_DATA)
{
	uint2 pixelXY = dispatchThreadId.xy;
	uint tileSize = pSrt->m_tileSize;

#if PHOTO_MODE_ENABLED
	float2 motionVec = CalculatePhotoModeMotionVector(pixelXY, pSrt->m_primaryDepth, pSrt->m_motionVectorFullRes, pSrt->m_linearDepthParams, pSrt->m_fullSizeInfo, pSrt->m_currentToLastFrameMat, pSrt->m_currentToFakeLastFrameMat, pSrt->m_projectionJitterOffsets);
#else
	float2 motionVec = pSrt->m_motionVectorFullRes[pixelXY];
#endif

	// edge detection from temporal aa depth info
	bool bEdge = false;
	if (pSrt->m_rejectTemporalEdgeSamples)
	{
		uint depthInfo = pSrt->m_temporalAADepthInfo[pixelXY];
		bEdge = depthInfo & 0x80;
	}

	// velocity
	float2 halfSpreadVelocity = CalculateHalfSpreadVelocity(motionVec, pSrt->m_motionVectorBufferDims, tileSize, pSrt->m_frameTimeExposurePercent);

	// depth
	float perspectiveDepth = pSrt->m_primaryDepth[pixelXY];
	float srcLinearDepth = GetLinearDepth(perspectiveDepth, pSrt->m_linearDepthParams);
	srcLinearDepth = NormalizeLinearDepth(srcLinearDepth, pSrt->m_cameraClipDistances);

	pSrt->m_velocityAndLinearDepthResult[pixelXY] = float4(halfSpreadVelocity, length(halfSpreadVelocity), srcLinearDepth * (bEdge ? -1.0f : 1.0f));

	// Tile Max Velocity
	{
		// 1. Setup different params we'll need
		uint2 tileXY = groupId;
		uint threadIndex = groupThreadId.y * 24 + groupThreadId.x;
		uint wavefrontIndex = (threadIndex >> 6);

		// 2. Load single motion vector for the thread
		float2 srcVelocity = motionVec;

		// 3. Reduce within wavefront and store max value into groupshared memory after memory barrier to make sure every thread is loaded and ready
		GroupMemoryBarrierWithGroupSync();

		float2 wavefrontMaxVelocity = float2(0.0f);
		FullMaxReduce(srcVelocity, wavefrontMaxVelocity);

		// only one lane per wavefront writes out the wavefront max value
		if (threadIndex % 64 == 0)
		{
			float wavefrontMaxVelocityLengthSq = SquaredLength(wavefrontMaxVelocity);

			s_wavefrontMaxVelocitiesTileSize24[wavefrontIndex] = wavefrontMaxVelocity;
			s_wavefrontMaxMagnitudesTileSize24[wavefrontIndex] = wavefrontMaxVelocityLengthSq;
		}

		// sync point again to make sure lds to fully prepared
		GroupMemoryBarrierWithGroupSync();

		// 4. First thread of threadgroup will loop through the values that the wavefronts computed and write out the final max
		if (all(groupThreadId == uint2(0, 0)))
		{
			float maxMagnitude = 0.0f;
			uint maxIndex = 0;

			for (uint i = 0; i < 9; i++)
			{
				if (s_wavefrontMaxMagnitudesTileSize24[i] > maxMagnitude)
				{
					maxMagnitude = s_wavefrontMaxMagnitudesTileSize24[i];
					maxIndex = i;
				}
			}

			pSrt->m_tileMaxVelocity[tileXY] = s_wavefrontMaxVelocitiesTileSize24[maxIndex];
		}
	}
}


//--------------------------------------------------------------------------------------
struct PrepareInputDataHalfResSrt
{
	Texture2D<float4> m_motionVectorFullRes;
	Texture2D<float3> m_primaryFloatFullRes;
	Texture2D<float> m_primaryDepth;
	Texture2D<uint> m_temporalAADepthInfo;
	RWTexture2D<float3> m_halfResPrimaryFloat;
	RWTexture2D<float4> m_velocityAndLinearDepthResult;
	RWTexture2D<float2> m_tileMaxVelocity;

	SamplerTable *m_pSamplers;

	uint m_tileSize;
	uint2 m_motionVectorBufferDims;
	uint2 m_motionVectorHalfResBufferDims;
	float2 m_linearDepthParams;
	float2 m_cameraClipDistances;
	float m_frameTimeExposurePercent;
	uint m_rejectTemporalEdgeSamples;
	uint m_isHalfRes;
	float2 m_projectionJitterOffsets;
	float4x4 m_currentToLastFrameMat;
	float4x4 m_currentToFakeLastFrameMat;
	float4 m_fullSizeInfo;
};

groupshared float2 s_wavefrontMaxVelocitiesTileSize16[4];
groupshared float s_wavefrontMaxMagnitudesTileSize16[4];

#define CHARACTER_PRIORITY_DOWNSAMPLE 0

[numthreads(16, 16, 1)]
void CS_MotionBlurPrepareInputDataHalfResTileSize16(uint3 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, PrepareInputDataHalfResSrt* pSrt : S_SRT_DATA)
{
	uint2 pixelXY = dispatchThreadId.xy;
	uint tileSize = pSrt->m_tileSize;
	uint2 tileXY = groupId;

	float2 maxMotionVec = float2(0.0f);

	uint2 fullResSearchPos = pixelXY * 2;

#if PHOTO_MODE_ENABLED
	float2 motionVec00 = CalculatePhotoModeMotionVector(fullResSearchPos, pSrt->m_primaryDepth, pSrt->m_motionVectorFullRes, pSrt->m_linearDepthParams, pSrt->m_fullSizeInfo, pSrt->m_currentToLastFrameMat, pSrt->m_currentToFakeLastFrameMat, pSrt->m_projectionJitterOffsets);
#else
	float2 motionVec00 = pSrt->m_motionVectorFullRes[fullResSearchPos].xy;
#endif	
	uint depthInfo00 = pSrt->m_temporalAADepthInfo[fullResSearchPos];
	float motionMagnitude00 = SquaredLength(motionVec00);

#if PHOTO_MODE_ENABLED
	float2 motionVec01 = CalculatePhotoModeMotionVector(fullResSearchPos + uint2(0, 1), pSrt->m_primaryDepth, pSrt->m_motionVectorFullRes, pSrt->m_linearDepthParams, pSrt->m_fullSizeInfo, pSrt->m_currentToLastFrameMat, pSrt->m_currentToFakeLastFrameMat, pSrt->m_projectionJitterOffsets);
#else
	float2 motionVec01 = pSrt->m_motionVectorFullRes[fullResSearchPos + uint2(0, 1)].xy;
#endif
	uint depthInfo01 = pSrt->m_temporalAADepthInfo[fullResSearchPos + uint2(0, 1)];
	float motionMagnitude01 = SquaredLength(motionVec01);

#if PHOTO_MODE_ENABLED
	float2 motionVec10 = CalculatePhotoModeMotionVector(fullResSearchPos + uint2(1, 0), pSrt->m_primaryDepth, pSrt->m_motionVectorFullRes, pSrt->m_linearDepthParams, pSrt->m_fullSizeInfo, pSrt->m_currentToLastFrameMat, pSrt->m_currentToFakeLastFrameMat, pSrt->m_projectionJitterOffsets);
#else
	float2 motionVec10 = pSrt->m_motionVectorFullRes[fullResSearchPos + uint2(1, 0)].xy;
#endif
	uint depthInfo10 = pSrt->m_temporalAADepthInfo[fullResSearchPos + uint2(1, 0)];
	float motionMagnitude10 = SquaredLength(motionVec10);

#if PHOTO_MODE_ENABLED
	float2 motionVec11 = CalculatePhotoModeMotionVector(fullResSearchPos + uint2(1, 1), pSrt->m_primaryDepth, pSrt->m_motionVectorFullRes, pSrt->m_linearDepthParams, pSrt->m_fullSizeInfo, pSrt->m_currentToLastFrameMat, pSrt->m_currentToFakeLastFrameMat, pSrt->m_projectionJitterOffsets);
#else
	float2 motionVec11 = pSrt->m_motionVectorFullRes[fullResSearchPos + uint2(1, 1)].xy;
#endif
	uint depthInfo11 = pSrt->m_temporalAADepthInfo[fullResSearchPos + uint2(1, 1)];
	float motionMagnitude11 = SquaredLength(motionVec11);

	// start assuming the first sample is the max depth
	float maxMotion = motionMagnitude00;
	maxMotionVec = motionVec00;
	uint2 maxMotionRelativeXY = uint2(0, 0);

	if (maxMotion < motionMagnitude01)
	{
		maxMotion = motionMagnitude01;
		maxMotionVec = motionVec01;
		maxMotionRelativeXY = uint2(0, 1);
	}

	if (maxMotion < motionMagnitude10)
	{
		maxMotion = motionMagnitude10;
		maxMotionVec = motionVec10;
		maxMotionRelativeXY = uint2(1, 0);
	}

	if (maxMotion < motionMagnitude11)
	{
		maxMotionVec = motionVec11;
		maxMotionRelativeXY = uint2(1, 1);
	}

	uint2 fullResXY = fullResSearchPos + maxMotionRelativeXY;

	// edge detection from temporal aa depth info
	bool bEdge = false;
	if (pSrt->m_rejectTemporalEdgeSamples)
	{
		bEdge = (depthInfo00 | depthInfo01 | depthInfo10 | depthInfo11) & 0x80;
	}

	// velocity
	float2 halfSpreadVelocity = CalculateHalfSpreadVelocity(maxMotionVec, pSrt->m_motionVectorHalfResBufferDims, pSrt->m_tileSize, pSrt->m_frameTimeExposurePercent);

	// depth
	float perspectiveDepth = pSrt->m_primaryDepth[fullResXY];
	float srcLinearDepth = GetLinearDepth(perspectiveDepth, pSrt->m_linearDepthParams);
	srcLinearDepth = NormalizeLinearDepth(srcLinearDepth, pSrt->m_cameraClipDistances);

	pSrt->m_velocityAndLinearDepthResult[pixelXY] = float4(halfSpreadVelocity, length(halfSpreadVelocity), srcLinearDepth * (bEdge ? -1.0f : 1.0f));

	{
		// 1. Setup different params we'll need
		uint threadIndex = groupThreadId.y * 16 + groupThreadId.x;

		// 2. Load single motion vector for the thread
		float2 srcVelocity = maxMotionVec;

		// 3. Reduce within wavefront and store max value into groupshared memory after memory barrier to make sure every thread is loaded and ready
		GroupMemoryBarrierWithGroupSync();

		float2 wavefrontMaxVelocity = float2(0.0f);
		FullMaxReduce(srcVelocity, wavefrontMaxVelocity);

		// only one lane per wavefront writes out the wavefront max value
		if (threadIndex % 64 == 0)
		{
			float wavefrontMaxVelocityLengthSq = SquaredLength(wavefrontMaxVelocity);
			uint wavefrontIndex = (threadIndex >> 6);

			s_wavefrontMaxVelocitiesTileSize16[wavefrontIndex] = wavefrontMaxVelocity;
			s_wavefrontMaxMagnitudesTileSize16[wavefrontIndex] = wavefrontMaxVelocityLengthSq;
		}

		// sync point again to make sure lds to fully prepared
		GroupMemoryBarrierWithGroupSync();

		// 4. First thread of threadgroup will loop through the values that the wavefronts computed and write out the final max
		if (all(groupThreadId == uint2(0, 0)))
		{
			float maxMagnitude = 0.0f;
			uint maxIndex = 0;

			for (uint i = 0; i < 4; i++)
			{
				if (s_wavefrontMaxMagnitudesTileSize16[i] > maxMagnitude)
				{
					maxMagnitude = s_wavefrontMaxMagnitudesTileSize16[i];
					maxIndex = i;
				}
			}

			pSrt->m_tileMaxVelocity[tileXY] = s_wavefrontMaxVelocitiesTileSize16[maxIndex];
		}
	}
}

groupshared float2 s_wavefrontMaxVelocitiesTileSize12[3];
groupshared float s_wavefrontMaxMagnitudesTileSize12[3];

[numthreads(12, 12, 1)]
void CS_MotionBlurPrepareInputDataHalfResTileSize12(uint3 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, PrepareInputDataHalfResSrt* pSrt : S_SRT_DATA)
{
	if (all(dispatchThreadId.xy < pSrt->m_motionVectorHalfResBufferDims))
	{
		uint2 pixelXY = dispatchThreadId.xy;
		uint tileSize = pSrt->m_tileSize;
		uint2 tileXY = groupId;

		float2 maxMotionVec = float2(0.0f);

		uint2 fullResSearchPos = pixelXY * 2;

#if PHOTO_MODE_ENABLED
		float2 motionVec00 = CalculatePhotoModeMotionVector(fullResSearchPos, pSrt->m_primaryDepth, pSrt->m_motionVectorFullRes, pSrt->m_linearDepthParams, pSrt->m_fullSizeInfo, pSrt->m_currentToLastFrameMat, pSrt->m_currentToFakeLastFrameMat, pSrt->m_projectionJitterOffsets);
#else
		float2 motionVec00 = pSrt->m_motionVectorFullRes[fullResSearchPos].xy;
#endif
		float motionMagnitude00 = SquaredLength(motionVec00);
		uint depthInfo00 = pSrt->m_temporalAADepthInfo[fullResSearchPos];

#if PHOTO_MODE_ENABLED
		float2 motionVec01 = CalculatePhotoModeMotionVector(fullResSearchPos + uint2(0, 1), pSrt->m_primaryDepth, pSrt->m_motionVectorFullRes, pSrt->m_linearDepthParams, pSrt->m_fullSizeInfo, pSrt->m_currentToLastFrameMat, pSrt->m_currentToFakeLastFrameMat, pSrt->m_projectionJitterOffsets);
#else
		float2 motionVec01 = pSrt->m_motionVectorFullRes[fullResSearchPos + uint2(0, 1)].xy;
#endif
		float motionMagnitude01 = SquaredLength(motionVec01);
		uint depthInfo01 = pSrt->m_temporalAADepthInfo[fullResSearchPos + uint2(0, 1)];

#if PHOTO_MODE_ENABLED
		float2 motionVec10 = CalculatePhotoModeMotionVector(fullResSearchPos + uint2(1, 0), pSrt->m_primaryDepth, pSrt->m_motionVectorFullRes, pSrt->m_linearDepthParams, pSrt->m_fullSizeInfo, pSrt->m_currentToLastFrameMat, pSrt->m_currentToFakeLastFrameMat, pSrt->m_projectionJitterOffsets);
#else
		float2 motionVec10 = pSrt->m_motionVectorFullRes[fullResSearchPos + uint2(1, 0)].xy;
#endif
		float motionMagnitude10 = SquaredLength(motionVec10);
		uint depthInfo10 = pSrt->m_temporalAADepthInfo[fullResSearchPos + uint2(1, 0)];

#if PHOTO_MODE_ENABLED
		float2 motionVec11 = CalculatePhotoModeMotionVector(fullResSearchPos + uint2(1, 1), pSrt->m_primaryDepth, pSrt->m_motionVectorFullRes, pSrt->m_linearDepthParams, pSrt->m_fullSizeInfo, pSrt->m_currentToLastFrameMat, pSrt->m_currentToFakeLastFrameMat, pSrt->m_projectionJitterOffsets);
#else
		float2 motionVec11 = pSrt->m_motionVectorFullRes[fullResSearchPos + uint2(1, 1)].xy;
#endif
		float motionMagnitude11 = SquaredLength(motionVec11);
		uint depthInfo11 = pSrt->m_temporalAADepthInfo[fullResSearchPos + uint2(1, 1)];

		// start assuming the first sample is the max depth
		float maxMotion = motionMagnitude00;
		maxMotionVec = motionVec00;
		uint2 maxMotionRelativeXY = uint2(0, 0);

		if (maxMotion < motionMagnitude01)
		{
			maxMotion = motionMagnitude01;
			maxMotionVec = motionVec01;
			maxMotionRelativeXY = uint2(0, 1);
		}

		if (maxMotion < motionMagnitude10)
		{
			maxMotion = motionMagnitude10;
			maxMotionVec = motionVec10;
			maxMotionRelativeXY = uint2(1, 0);
		}

		if (maxMotion < motionMagnitude11)
		{
			maxMotionVec = motionVec11;
			maxMotionRelativeXY = uint2(1, 1);
		}

		uint2 fullResXY = fullResSearchPos + maxMotionRelativeXY;

		// edge detection from temporal aa depth info
		bool bEdge = false;
		if (pSrt->m_rejectTemporalEdgeSamples)
		{
			bEdge = (depthInfo00 | depthInfo01 | depthInfo10 | depthInfo11) & 0x80;
		}

		// velocity
		float2 halfSpreadVelocity = CalculateHalfSpreadVelocity(maxMotionVec, pSrt->m_motionVectorHalfResBufferDims, pSrt->m_tileSize, pSrt->m_frameTimeExposurePercent);

		// depth
		float perspectiveDepth = pSrt->m_primaryDepth[fullResXY];
		float srcLinearDepth = GetLinearDepth(perspectiveDepth, pSrt->m_linearDepthParams);
		srcLinearDepth = NormalizeLinearDepth(srcLinearDepth, pSrt->m_cameraClipDistances);

		pSrt->m_velocityAndLinearDepthResult[pixelXY] = float4(halfSpreadVelocity, length(halfSpreadVelocity), srcLinearDepth * (bEdge ? -1.0f : 1.0f));

		{
			// 1. Setup different params we'll need		
			uint threadIndex = groupThreadId.y * 12 + groupThreadId.x;
			uint wavefrontIndex = (threadIndex >> 6);

			// 2. Load single motion vector for the thread
			float2 srcVelocity = maxMotionVec;

			// 3. Reduce within wavefront and store max value into groupshared memory after memory barrier to make sure every thread is loaded and ready
			GroupMemoryBarrierWithGroupSync();

			float2 wavefrontMaxVelocity = float2(0.0f);
			if (threadIndex < 128)
			{
				FullMaxReduce(srcVelocity, wavefrontMaxVelocity);

				if (threadIndex == 0 || threadIndex == 64)
				{
					float wavefrontMaxVelocityLengthSq = SquaredLength(wavefrontMaxVelocity);

					s_wavefrontMaxVelocitiesTileSize12[wavefrontIndex] = wavefrontMaxVelocity;
					s_wavefrontMaxMagnitudesTileSize12[wavefrontIndex] = wavefrontMaxVelocityLengthSq;
				}
			}
			else if (threadIndex < 144)
			{
				FullMaxReduce16(srcVelocity, wavefrontMaxVelocity);

				if (threadIndex == 128)
				{
					float wavefrontMaxVelocityLengthSq = SquaredLength(wavefrontMaxVelocity);

					s_wavefrontMaxVelocitiesTileSize12[wavefrontIndex] = wavefrontMaxVelocity;
					s_wavefrontMaxMagnitudesTileSize12[wavefrontIndex] = wavefrontMaxVelocityLengthSq;
				}
			}

			// sync point again to make sure lds to fully prepared
			GroupMemoryBarrierWithGroupSync();

			// 4. First thread of threadgroup will loop through the values that the wavefronts computed and write out the final max
			if (all(groupThreadId == uint2(0, 0)))
			{
				float maxMagnitude = 0.0f;
				uint maxIndex = 0;

				for (uint i = 0; i < 3; i++)
				{
					if (s_wavefrontMaxMagnitudesTileSize12[i] > maxMagnitude)
					{
						maxMagnitude = s_wavefrontMaxMagnitudesTileSize12[i];
						maxIndex = i;
					}
				}

				pSrt->m_tileMaxVelocity[tileXY] = s_wavefrontMaxVelocitiesTileSize12[maxIndex];
			}
		}
	}
}


//--------------------------------------------------------------------------------------
struct NeighborMaxVelocitySrt
{
	Texture2D<float2> m_tileMaxVelocity;
	RWTexture2D<float2> m_neighborMaxVelocity;
	uint2 m_bufferDims;
	uint2 m_motionVectorBufferDims;
	uint m_tileSize;
	float m_frameTimeExposurePercent;

#if IS_DEBUG
	uint m_debugMode;
#endif
};

[numthreads(8, 8, 1)]
void CS_MotionBlurNeighborMaxVelocity(uint3 dispatchThreadId : SV_DispatchThreadId, NeighborMaxVelocitySrt* pSrt : S_SRT_DATA)
{
	int2 pixelXY = (int2)dispatchThreadId.xy;

	// Start with middle
	float2 maxTileVelocity = pSrt->m_tileMaxVelocity[uint2(pixelXY.x, pixelXY.y)];
	float maxTileMagnitude = SquaredLength(maxTileVelocity * pSrt->m_motionVectorBufferDims);
	
	//--------------------------------------------------------------
	// Direct adjacent neighbors

	// Top-Middle neighbor
	{
		float2 srcTileVelocity = pSrt->m_tileMaxVelocity[uint2(pixelXY.x, pixelXY.y - 1)];
		float srcTileMagnitude = SquaredLength(srcTileVelocity * pSrt->m_motionVectorBufferDims);

		if (srcTileMagnitude > maxTileMagnitude)
		{
			maxTileVelocity = srcTileVelocity;
			maxTileMagnitude = srcTileMagnitude;
		}
	}

	// Right-Middle neighbor
	{
		float2 srcTileVelocity = pSrt->m_tileMaxVelocity[uint2(pixelXY.x + 1, pixelXY.y)];
		float srcTileMagnitude = SquaredLength(srcTileVelocity * pSrt->m_motionVectorBufferDims);

		if (srcTileMagnitude > maxTileMagnitude)
		{
			maxTileVelocity = srcTileVelocity;
			maxTileMagnitude = srcTileMagnitude;
		}
	}

	// Bottom-Middle neighbor
	{
		float2 srcTileVelocity = pSrt->m_tileMaxVelocity[uint2(pixelXY.x, pixelXY.y + 1)];
		float srcTileMagnitude = SquaredLength(srcTileVelocity * pSrt->m_motionVectorBufferDims);

		if (srcTileMagnitude > maxTileMagnitude)
		{
			maxTileVelocity = srcTileVelocity;
			maxTileMagnitude = srcTileMagnitude;
		}
	}

	// Left-Middle neighbor
	{
		float2 srcTileVelocity = pSrt->m_tileMaxVelocity[uint2(pixelXY.x - 1, pixelXY.y)];
		float srcTileMagnitude = SquaredLength(srcTileVelocity * pSrt->m_motionVectorBufferDims);

		if (srcTileMagnitude > maxTileMagnitude)
		{
			maxTileVelocity = srcTileVelocity;
			maxTileMagnitude = srcTileMagnitude;
		}
	}

	//--------------------------------------------------------------
	// Corner neighbors

	// Top-Left neighbor
	{
		float2 srcTileVelocity = pSrt->m_tileMaxVelocity[uint2(pixelXY.x - 1, pixelXY.y - 1)];
		float srcTileMagnitude = SquaredLength(srcTileVelocity * pSrt->m_motionVectorBufferDims);
		
		float2 srcTileDirection = normalize(srcTileVelocity);
		float2 toCenterTileDirection = float2(0.7071067f, 0.7071067f);
		float dotToCenter = dot(srcTileDirection, toCenterTileDirection);
		bool isFacingCenter = dotToCenter >= 0.7071067f; //within 90 degree cone torwards center cos(45 degrees) = 0.7071067f

		if (all(srcTileMagnitude > maxTileMagnitude && isFacingCenter))
		{
			maxTileVelocity = srcTileVelocity;
			maxTileMagnitude = srcTileMagnitude;
		}
	}

	// Top-Right neighbor
	{
		float2 srcTileVelocity = pSrt->m_tileMaxVelocity[uint2(pixelXY.x + 1, pixelXY.y - 1)];
		float srcTileMagnitude = SquaredLength(srcTileVelocity * pSrt->m_motionVectorBufferDims);

		float2 srcTileDirection = normalize(srcTileVelocity);
		float2 toCenterTileDirection = float2(-0.7071067f, 0.7071067f);
		float dotToCenter = dot(srcTileDirection, toCenterTileDirection);
		bool isFacingCenter = dotToCenter >= 0.7071067f; //within 90 degree cone torwards center cos(45 degrees) = 0.7071067f

		if (all(srcTileMagnitude > maxTileMagnitude && isFacingCenter))
		{
			maxTileVelocity = srcTileVelocity;
			maxTileMagnitude = srcTileMagnitude;
		}
	}

	// Bottom-Left neighbor
	{
		float2 srcTileVelocity = pSrt->m_tileMaxVelocity[uint2(pixelXY.x - 1, pixelXY.y + 1)];
		float srcTileMagnitude = SquaredLength(srcTileVelocity * pSrt->m_motionVectorBufferDims);
		
		float2 srcTileDirection = normalize(srcTileVelocity);
		float2 toCenterTileDirection = float2(0.7071067f, -0.7071067f);
		float dotToCenter = dot(srcTileDirection, toCenterTileDirection);
		bool isFacingCenter = dotToCenter >= 0.7071067f; //within 90 degree cone torwards center cos(45 degrees) = 0.7071067f

		if (all(srcTileMagnitude > maxTileMagnitude && isFacingCenter))
		{
			maxTileVelocity = srcTileVelocity;
			maxTileMagnitude = srcTileMagnitude;
		}
	}

	// Bottom-Right neighbor
	{
		float2 srcTileVelocity = pSrt->m_tileMaxVelocity[uint2(pixelXY.x + 1, pixelXY.y + 1)];
		float srcTileMagnitude = SquaredLength(srcTileVelocity * pSrt->m_motionVectorBufferDims);

		float2 srcTileDirection = normalize(srcTileVelocity);
		float2 toCenterTileDirection = float2(-0.7071067f, -0.7071067f);
		float dotToCenter = dot(srcTileDirection, toCenterTileDirection);
		bool isFacingCenter = dotToCenter >= 0.7071067f; //within 90 degree cone torwards center cos(45 degrees) = 0.7071067f

		if (all(srcTileMagnitude > maxTileMagnitude && isFacingCenter))
		{
			maxTileVelocity = srcTileVelocity;
			maxTileMagnitude = srcTileMagnitude;
		}
	}

#if IS_DEBUG
	if (pSrt->m_debugMode != kMotionBlurNewDebugTileMax)
	{
		pSrt->m_neighborMaxVelocity[pixelXY] = CalculateHalfSpreadVelocity(maxTileVelocity, pSrt->m_motionVectorBufferDims, pSrt->m_tileSize, pSrt->m_frameTimeExposurePercent);
	}
	else
	{
		// pass through tile max if we are wanting to output that for debug
		pSrt->m_neighborMaxVelocity[pixelXY] = CalculateHalfSpreadVelocity(pSrt->m_tileMaxVelocity[pixelXY], pSrt->m_motionVectorBufferDims, pSrt->m_tileSize, pSrt->m_frameTimeExposurePercent);
	}
#else
	pSrt->m_neighborMaxVelocity[pixelXY] = CalculateHalfSpreadVelocity(maxTileVelocity, pSrt->m_motionVectorBufferDims, pSrt->m_tileSize, pSrt->m_frameTimeExposurePercent);
#endif
	
}


//--------------------------------------------------------------------------------------
struct ReconstructionFilterSrt
{
	Texture2D<float3> m_primaryFloat;
	Texture2D<float4> m_motionVector;
	Texture2D<float> m_primaryDepth;
	Texture2D<float2> m_neighborMaxVelocity;
	Texture2D<float4> m_velocityAndLinearDepth;
	Texture2D<float3> m_vignetteBlurResult;
	Texture2D<float> m_vignetteBlurAlpha;
	Texture2D<uint> m_characterMask;
	RWTexture2D<float3> m_motionBlurResult;
	RWTexture2D<uint> m_gatheredSamplesCount;
#if IS_DEBUG
	RWTexture2D<float4> m_debugData;
#endif

	SamplerTable *m_pSamplers;

	uint m_tileSize;
	uint2 m_motionVectorBufferDims;
	float2 m_invPrimaryFloatBufferDims;
	float m_frameTimeExposurePercent;
	uint m_numReconstructionSamples;
	float2 m_linearDepthParams;
	float2 m_cameraClipDistances;
	float m_velocityTriggerThreshold;
	float m_invVelocityStartRange;
	float m_vignetteBlurScale;
	float m_zExtent;
	uint m_isHalfRes;
	uint m_frameCountMod;
	uint m_useBilinearReconstruction;
	uint m_isFirstSeperablePass;
	uint m_usePerPixelSampleCounting;
	float m_countingSampleWeightThreshold;
	float m_centerTapWeightModifier;
	uint m_enableExtraCharacterBlur;
	uint m_enableDualDirectionSampling;
	uint m_enableDirectionalWeighting;
	float m_lumaDiscardThreshold;

#if IS_DEBUG
	uint m_debugMode;
	uint m_forceFullscreenOutput;
	uint m_useOverrideMotionVector;
	float2 m_overrideMotionVector;
	int m_overrideSampleIndex;
#endif
};


//--------------------------------------------------------------------------------------

float GetBlurBlendWeight(float velocityMagnitude, float startDist, float invStartRange)
{
	return saturate((velocityMagnitude - startDist) * invStartRange);
}

float2 GetDominantNeighborhoodVelocity(uint2 pixelXY, ReconstructionFilterSrt* pSrt)
{
	// Get correct neighbor max velocity based on resolution
	float2 neighborMaxVelocity = float2(0.0f);
#if IS_1440P
	neighborMaxVelocity = pSrt->m_neighborMaxVelocity[pixelXY / (pSrt->m_isHalfRes ? 16 : 32)]; // 4x4 wavefronts per tile for orbis (tile size of 32)
#elif IS_1080P
	neighborMaxVelocity = pSrt->m_neighborMaxVelocity[pixelXY / (pSrt->m_isHalfRes ? 12 : 24)]; // 3x3 wavefronts per tile for orbis (tile size of 24)
#endif

	neighborMaxVelocity *= pSrt->m_frameTimeExposurePercent;
	return neighborMaxVelocity;
}

float4 GetVelocityAndLinearDepth(uint2 pixelXY, ReconstructionFilterSrt* pSrt)
{
	float4 srcVelocityAndLinearDepth = pSrt->m_velocityAndLinearDepth[pixelXY];
	float2 srcVelocity = srcVelocityAndLinearDepth.xy;
	float srcVelocityMagnitude = srcVelocityAndLinearDepth.z;
	float srcLinearDepth = srcVelocityAndLinearDepth.w;

	srcVelocity *= pSrt->m_frameTimeExposurePercent;
	srcVelocityMagnitude *= pSrt->m_frameTimeExposurePercent;

	return float4(srcVelocity, srcVelocityMagnitude, srcLinearDepth);
}

float2 CalculateSecondarySampleDirection(float2 srcVelocityNormalized, float srcVelocityMagnitude, float2 neighborMaxVelocityNormalized, ReconstructionFilterSrt* pSrt)
{
	// Lerp from pixel velocity to perpendicular neighbor velocity as the pixel velocity decreases
	float2 neighborPerpendicular = float2(-neighborMaxVelocityNormalized.y, neighborMaxVelocityNormalized.x);

	// Only if we have a source motion vector that is large enough will we start to use a real secondary sampling direction
	// This helps prevent "cross" artifacts where small bright objects would be sampled in two directions creating a cross looking visual articact
	if (srcVelocityMagnitude >= pSrt->m_velocityTriggerThreshold * 0.5f)
	{
		if (dot(neighborPerpendicular, srcVelocityNormalized) < 0.0f)
		{
			neighborPerpendicular *= -1.0f;
		}

		return normalize(lerp(neighborPerpendicular, srcVelocityNormalized, saturate(srcVelocityMagnitude * FastRcp(pSrt->m_velocityTriggerThreshold))));
	}
	else
	{
		return neighborMaxVelocityNormalized;
	}
	
	//return normalize(lerp(neighborPerpendicular, srcVelocityNormalized, saturate(srcVelocityMagnitude * FastRcp(pSrt->m_velocityTriggerThreshold))));
}

void GetCenterTap(uint2 pixelXY, float2 srcVelocityMagnitude, ReconstructionFilterSrt* pSrt, inout float3 accumColor, inout float accumWeight, inout float luma)
{
	// Sample the source pixel's color value
	float3 srcColor = float3(0.0f);
#if WITH_VIGNETTE
	srcColor = pSrt->m_vignetteBlurResult[pixelXY];
#else
	srcColor = pSrt->m_primaryFloat[pixelXY];
#endif

	luma = Luminance(srcColor);

	// calculate how much weight this center tap has
	float srcWeight = clamp((float)pSrt->m_numReconstructionSamples / (pSrt->m_centerTapWeightModifier * srcVelocityMagnitude), 0.0f, 2.0f);

	// accumulate into out variables
	accumColor += srcColor * srcWeight;
	accumWeight += srcWeight;
}

void GatherBlurTaps(uint2 pixelXY, float2 srcVelocity, float srcVelocityMagnitude, float srcLinearDepth, float srcLuma,
								   float2 neighborMaxVelocity, float neighborMaxVelocityMagnitude, 
								   bool isSrcTaaEdge, ReconstructionFilterSrt* pSrt, inout float3 accumColor, inout float accumWeight, inout uint numMeaningfulSamplesGathered)
{	
	float2 srcVelocityNormalized = srcVelocity * FastRcp(srcVelocityMagnitude);
	float2 neighborMaxVelocityNormalized = neighborMaxVelocity * FastRcp(neighborMaxVelocityMagnitude);

	float2 secondarySampleDirection = neighborMaxVelocityNormalized;
	float2 secondarySampleVelocity = neighborMaxVelocity;

	if (pSrt->m_enableDualDirectionSampling)
	{
		secondarySampleDirection = CalculateSecondarySampleDirection(srcVelocityNormalized, srcVelocityMagnitude, neighborMaxVelocityNormalized, pSrt);
		secondarySampleVelocity = secondarySampleDirection * neighborMaxVelocityMagnitude;
	}

	float jitterDivisor = FastRcp((float)pSrt->m_numReconstructionSamples + 1.0f);

	float2 uv = (float2(pixelXY)+float2(0.5f)) * pSrt->m_invPrimaryFloatBufferDims;
	float jitterFactor = InterleavedGradientNoise(uv, 0 /*pSrt->m_frameCountMod*/) - 0.5f;
	
	//jitterFactor = VanDerCorput(((pixelXY.x + 1) * (pixelXY.y + 1)) % pSrt->m_numReconstructionSamples);

	bool srcIsChar = false;
	if (pSrt->m_enableExtraCharacterBlur)
	{
		if (pSrt->m_characterMask[pixelXY * (pSrt->m_isHalfRes ? 2 : 1)])
		{
			srcIsChar = true;
		}
	}

	// Main reconstruction loop
	for (uint i = 0; i < pSrt->m_numReconstructionSamples; i++)
	{
#if IS_DEBUG
		if (pSrt->m_overrideSampleIndex != -1 && pSrt->m_overrideSampleIndex != i)
		{
			continue;
		}
#endif

		// Calculate sample placement along direction
		float t = lerp(-1.0f, 1.0f, ((float)i + jitterFactor + 1.0f) * jitterDivisor);

		// Calculate sample pixel location
		//float2 sampleDir = float(i & 1) * srcVelocity + (1.0f - float(i & 1)) * neighborMaxVelocity; // alternate between dominant neighbor velocity and pixel velocity based on loop counter
		float2 sampleDir = ((i & 1) == 0) ? neighborMaxVelocity : secondarySampleVelocity; // alternate between dominant neighbor velocity and pixel velocity
		float2 sampleDirNorm = ((i & 1) == 0) ? neighborMaxVelocityNormalized : secondarySampleDirection;
		uint2 sampleXY = (uint2)round((float2)pixelXY + t * sampleDir);

		// Sample the half spread velocity and linear camera space depth for the sample pixel
		float4 sampleVelocityAndLinearDepth = GetVelocityAndLinearDepth(sampleXY, pSrt); // [velocityX, velocityY, velocityMagnitude, linearDepth]
		float2 sampleVelocity = sampleVelocityAndLinearDepth.xy;
		float sampleVelocityMagnitude = sampleVelocityAndLinearDepth.z;
		float sampleLinearDepth = sampleVelocityAndLinearDepth.w;

		// Sample color (use vignette blur as source if that is active)
		float3 sampleColor = float3(0.0f);
#if WITH_VIGNETTE
		sampleColor = pSrt->m_vignetteBlurResult[sampleXY];
#else
		sampleColor = pSrt->m_primaryFloat[sampleXY];
#endif

		// Negative linear depth means its a TAA edge
		if (sampleLinearDepth < 0.0f)
		{
			// Don't accept TAA edge samples if we are not a character pixel because it will create nasty artifacts
			// This is because TAA can cause some of the player's edge to bleed over onto pixels that might have high motion
			if (!srcIsChar)
			{
				continue;
			}
			
			sampleLinearDepth *= -1.0f;
		}

		// Use the directionality as a factor into the weight
		float sampleDotDir = 1.0f;
		float sourceDotDir = 1.0f;
		float maxDotDir = 1.0f;

		if (pSrt->m_enableDirectionalWeighting)
		{
			sampleDotDir = saturate(dot(normalize(sampleVelocity), sampleDirNorm));
			sourceDotDir = saturate(dot(srcVelocityNormalized, neighborMaxVelocityNormalized));
			maxDotDir = max(sourceDotDir, sampleDotDir);
		}

		// depth classification weights
		float foreground = SoftDepthCompare(sampleLinearDepth, srcLinearDepth, pSrt->m_zExtent);
		float background = SoftDepthCompare(srcLinearDepth, sampleLinearDepth, pSrt->m_zExtent);

		// compute final sample weight

		// Case 1: The sample is moving and blurring over the source pixel
		float foregroundWeight = foreground * sampleDotDir * Cone(sampleXY, pixelXY, sampleVelocityMagnitude);

		// Case 2: The source pixel is moving and should be blurry
		float backgroundWeight = background * sourceDotDir * Cone(pixelXY, sampleXY, srcVelocityMagnitude);

		// Case 3: Both the source and sample pixels are moving and blur over each other
		float dualWeight = 2.0f * maxDotDir * Cylinder(sampleXY, pixelXY, sampleVelocityMagnitude) * Cylinder(pixelXY, sampleXY, srcVelocityMagnitude);

		float sampleWeight = foregroundWeight + backgroundWeight + dualWeight;

#if IS_DEBUG
		if (pSrt->m_overrideSampleIndex != -1)
		{
			pSrt->m_debugData[pixelXY] = float4(sampleDirNorm.x, sampleDirNorm.y, sourceDotDir, 1.0f);
		}
#endif

		// Boost sample weight for samples around the edges of characters to help get rid of hard blur outlines on characters
		{
			bool isCharSample = false;
			if (pSrt->m_characterMask[sampleXY * (pSrt->m_isHalfRes ? 2 : 1)])
			{
				isCharSample = true;
			}

			if (pSrt->m_enableExtraCharacterBlur && !srcIsChar && isCharSample)
			{
				sampleWeight = clamp(sampleWeight * 2.0f, 0.0f, 3.0f);
			}

			if (pSrt->m_enableExtraCharacterBlur && srcIsChar && !isCharSample)
			{
				sampleWeight = clamp(sampleWeight * 2.0f, 0.0f, 3.0f);
			}
		}

		float sampleLuma = Luminance(sampleColor);
		if (sampleLuma - srcLuma > pSrt->m_lumaDiscardThreshold)
		{
			sampleWeight = 0.0f;
		}

		// accumulate weight and color
		accumWeight += sampleWeight;
		accumColor += sampleWeight * sampleColor;

		// if this is an "important" enough sample that was gathered away from the center then we count it
		if (all(pSrt->m_usePerPixelSampleCounting && sampleWeight > pSrt->m_countingSampleWeightThreshold && any(sampleXY != pixelXY)))
		{
			numMeaningfulSamplesGathered++;
		}
	}
}

void ComputeMotionBlurForPixel(uint2 pixelXY, float2 neighborMaxVelocity, float neighborMaxVelocityMagnitude, float neighborBlurBlendWeight, ReconstructionFilterSrt* pSrt, inout float3 finalColor, inout uint numSamplesGathered)
{
	float2 uv = (float2(pixelXY)+float2(0.5f)) * pSrt->m_invPrimaryFloatBufferDims;

	// Get the precomputed half spread velocity and camera space linear depth info for this pixel
	float4 srcVelocityAndLinearDepth = GetVelocityAndLinearDepth(pixelXY, pSrt);
	float2 srcVelocity = srcVelocityAndLinearDepth.xy;
	float srcVelocityMagnitude = srcVelocityAndLinearDepth.z;
	float srcLinearDepth = srcVelocityAndLinearDepth.w;

	// If we have no blend weight for motion blur then just early out
	if (neighborBlurBlendWeight <= 0.0f)
	{
		finalColor = float3(0.0f);
		numSamplesGathered = 0;
	}
	else
	{
		// If vignette blur is active, then the result of the vignette blur is fed as source color for motion blur
		// In the scenario where we are vignette blurring then we will scale down how much we motion blur to compensate for there being two blurs applied
		// We scale it down here because we don't want to accidentally lower our velocity into the "early out" branch above
#if WITH_VIGNETTE
		neighborMaxVelocity *= 1.0f - (pSrt->m_vignetteBlurScale * 0.375f);
#endif

		// We check for temporal AA edges. This is encoded by setting the pre computed camera space depth to negative in the earlier pass (this can only be a positive value)
		// We need to do this to prevent nasty artifacts with pixels being gathered that shouldn't be
		bool srcIsTaaEdge = false;
		if (srcLinearDepth < 0.0f)
		{
			srcLinearDepth *= -1.0f;
			srcIsTaaEdge = true;
		}

		// Sample center tap
		float3 accumColor = float3(0.0f);
		float accumWeight = 0.0f;
		float srcLuma = 0.0f;
		GetCenterTap(pixelXY, srcVelocityMagnitude, pSrt, accumColor, accumWeight, srcLuma);

		// Sample along blur directions
		GatherBlurTaps(pixelXY, srcVelocity, srcVelocityMagnitude, srcLinearDepth, srcLuma, neighborMaxVelocity, neighborMaxVelocityMagnitude, srcIsTaaEdge, pSrt, accumColor, accumWeight, numSamplesGathered);

		// Normalize accumulated color
		finalColor = accumColor / accumWeight;

		// Blend with vignette color if it is active
#if WITH_VIGNETTE
		float3 vignetteColor = pSrt->m_vignetteBlurResult[pixelXY];
		finalColor = lerp(finalColor, vignetteColor, 1.0f - neighborBlurBlendWeight);
#endif
	}
}

[numthreads(8, 8, 1)]
void CS_MotionBlurReconstructionFilter(uint3 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, ReconstructionFilterSrt* pSrt : S_SRT_DATA)
{
	uint2 pixelXY = dispatchThreadId.xy;

	// Get neighbor max velocity for this pixel
	float2 neighborMaxVelocity = float2(0.0f);

#if IS_1440P && HALF_RES
	neighborMaxVelocity = ReadFirstLane(pSrt->m_neighborMaxVelocity[groupId / 2] * pSrt->m_frameTimeExposurePercent);
#elif IS_1440P && !HALF_RES
	neighborMaxVelocity = ReadFirstLane(pSrt->m_neighborMaxVelocity[groupId / 4] * pSrt->m_frameTimeExposurePercent);
#elif IS_1080P && !HALF_RES
	neighborMaxVelocity = ReadFirstLane(pSrt->m_neighborMaxVelocity[groupId / 3] * pSrt->m_frameTimeExposurePercent);
#endif

	float neighborMaxVelocityMagnitude = ReadFirstLane(length(neighborMaxVelocity));
	float neighborBlurBlendWeight = ReadFirstLane(GetBlurBlendWeight(neighborMaxVelocityMagnitude, pSrt->m_velocityTriggerThreshold, pSrt->m_invVelocityStartRange));

	if (neighborBlurBlendWeight <= 0.0f)
	{
#if WITH_VIGNETTE
		pSrt->m_motionBlurResult[pixelXY] = pSrt->m_vignetteBlurResult[pixelXY];
#else
		pSrt->m_motionBlurResult[pixelXY] = pSrt->m_primaryFloat[pixelXY];
#endif

#if IS_DEBUG
		if (pSrt->m_debugMode == kMotionBlurNewDebugUserCustom) // Hack the shader to output whatever you want for debugging purposes here
		{
			pSrt->m_debugData[pixelXY] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		}
#endif
	}
	else
	{
		float3 finalColor = float3(0.0f);
		uint numSamplesGathered = 0;
		ComputeMotionBlurForPixel(pixelXY, neighborMaxVelocity, neighborMaxVelocityMagnitude, neighborBlurBlendWeight, pSrt, finalColor, numSamplesGathered);

		pSrt->m_motionBlurResult[pixelXY] = finalColor;
		if (pSrt->m_usePerPixelSampleCounting)
		{
			pSrt->m_gatheredSamplesCount[pixelXY] = numSamplesGathered;
		}

#if IS_DEBUG
		if (pSrt->m_debugMode == kMotionBlurNewDebugTileMax || pSrt->m_debugMode == kMotionBlurNewDebugNeighborMax)
		{
			// neighborMaxVelocity will contain tileMax if we are using tileMax debug mode
			pSrt->m_motionBlurResult[pixelXY] = float3(neighborMaxVelocity.x, neighborMaxVelocity.y, 0.0f);
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugSampleNoise)
		{
			float jitterDivisor = FastRcp((float)pSrt->m_numReconstructionSamples + 1.0f);
			float jitterFactor = RandDither(pixelXY, pSrt->m_frameCountMod);

			float2 uv = (float2(pixelXY)+float2(0.5f)) * pSrt->m_invPrimaryFloatBufferDims;
			float foo = InterleavedGradientNoise(uv, 1) - 0.5f;
			//float jitterFactor = VanDerCorput(((pixelXY.x + 1) * (pixelXY.y + 1)) % pSrt->m_numReconstructionSamples);

			pSrt->m_motionBlurResult[pixelXY] = float3(foo);
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugMotionBlurAlpha)
		{
			pSrt->m_motionBlurResult[pixelXY] = float3(1.0f, 0.0f, 1.0f);
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugMotionBlurColor)
		{
			pSrt->m_motionBlurResult[pixelXY] = finalColor;
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugMotionVectors)
		{
			float2 srcMotionVec = pSrt->m_motionVector[pixelXY * (pSrt->m_isHalfRes ? 2.0f : 1.0f)];
			pSrt->m_motionBlurResult[pixelXY] = float3(srcMotionVec.x, srcMotionVec.y, 0.0f);
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugHalfSpreadVelocity)
		{
			float2 srcMotionVec = pSrt->m_motionVector[pixelXY * (pSrt->m_isHalfRes ? 2.0f : 1.0f)];
			float2 halfSpreadVelocity = CalculateHalfSpreadVelocity(srcMotionVec, pSrt->m_motionVectorBufferDims, pSrt->m_tileSize, pSrt->m_frameTimeExposurePercent);
			pSrt->m_motionBlurResult[pixelXY] = float3(halfSpreadVelocity.x, halfSpreadVelocity.y, 0.0f);
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugUserCustom) // Hack the shader to output whatever you want for debugging purposes here
		{
			pSrt->m_motionBlurResult[pixelXY] = finalColor;
			pSrt->m_debugData[pixelXY] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			pSrt->m_motionBlurResult[pixelXY] = finalColor;
		}
#endif
	}
}

[numthreads(12, 12, 1)]
void CS_MotionBlurReconstructionFilter1080pHalfRes(uint3 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, ReconstructionFilterSrt* pSrt : S_SRT_DATA)
{
	uint2 pixelXY = dispatchThreadId.xy;
	float2 uv = (float2(pixelXY)+float2(0.5f)) * pSrt->m_invPrimaryFloatBufferDims;

	// Get neighbor max velocity for this pixel
	float2 neighborMaxVelocity = ReadFirstLane(pSrt->m_neighborMaxVelocity[groupId] * pSrt->m_frameTimeExposurePercent);
	float neighborMaxVelocityMagnitude = ReadFirstLane(length(neighborMaxVelocity));
	float neighborBlurBlendWeight = ReadFirstLane(GetBlurBlendWeight(neighborMaxVelocityMagnitude, pSrt->m_velocityTriggerThreshold, pSrt->m_invVelocityStartRange));

#if IS_DEBUG
	if (pSrt->m_useOverrideMotionVector)
	{
		neighborMaxVelocity = CalculateHalfSpreadVelocity(pSrt->m_overrideMotionVector, pSrt->m_motionVectorBufferDims, pSrt->m_tileSize, pSrt->m_frameTimeExposurePercent);
		neighborMaxVelocity *= pSrt->m_frameTimeExposurePercent;
	}
#endif

	if (neighborBlurBlendWeight <= 0.0f)
	{
#if WITH_VIGNETTE
		pSrt->m_motionBlurResult[pixelXY] = pSrt->m_vignetteBlurResult[pixelXY];
#else
		pSrt->m_motionBlurResult[pixelXY] = pSrt->m_primaryFloat[pixelXY];
#endif

#if IS_DEBUG
		if (pSrt->m_debugMode == kMotionBlurNewDebugUserCustom) // Hack the shader to output whatever you want for debugging purposes here
		{
			pSrt->m_debugData[pixelXY] = float4(1.0f, 0.0f, 1.0f, 1.0f);
		}
#endif
	}
	else
	{
		float3 finalColor = float3(0.0f);
		uint numSamplesGathered = 0;
		ComputeMotionBlurForPixel(pixelXY, neighborMaxVelocity, neighborMaxVelocityMagnitude, neighborBlurBlendWeight, pSrt, finalColor, numSamplesGathered);

		pSrt->m_motionBlurResult[pixelXY] = finalColor;
		if (pSrt->m_usePerPixelSampleCounting)
		{
			pSrt->m_gatheredSamplesCount[pixelXY] = numSamplesGathered;
		}

#if IS_DEBUG
		if (pSrt->m_debugMode == kMotionBlurNewDebugTileMax || pSrt->m_debugMode == kMotionBlurNewDebugNeighborMax)
		{
			// neighborMaxVelocity will contain tileMax if we are using tileMax debug mode
			pSrt->m_motionBlurResult[pixelXY] = float3(neighborMaxVelocity.x, neighborMaxVelocity.y, 0.0f);
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugSampleNoise)
		{
			float sampleNoiseSpatial = RandDitherSpatial(pixelXY, 0.75f);
			float sampleNoiseTemporal = RandDitherTemporal(pixelXY, 0, 0.5f);
			pSrt->m_motionBlurResult[pixelXY] = float3(sampleNoiseSpatial + sampleNoiseTemporal);
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugMotionBlurAlpha)
		{
			pSrt->m_motionBlurResult[pixelXY] = float3(1.0f, 0.0f, 1.0f);
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugMotionBlurColor)
		{
			pSrt->m_motionBlurResult[pixelXY] = finalColor;
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugMotionVectors)
		{
			float2 srcMotionVec = pSrt->m_motionVector[pixelXY * (pSrt->m_isHalfRes ? 2.0f : 1.0f)];
			pSrt->m_motionBlurResult[pixelXY] = float3(srcMotionVec.x, srcMotionVec.y, 0.0f);
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugHalfSpreadVelocity)
		{
			float2 srcMotionVec = pSrt->m_motionVector[pixelXY * (pSrt->m_isHalfRes ? 2.0f : 1.0f)];
			float2 halfSpreadVelocity = CalculateHalfSpreadVelocity(srcMotionVec, pSrt->m_motionVectorBufferDims, pSrt->m_tileSize, pSrt->m_frameTimeExposurePercent);
			pSrt->m_motionBlurResult[pixelXY] = float3(halfSpreadVelocity.x, halfSpreadVelocity.y, 0.0f);
		}
		else if (pSrt->m_debugMode == kMotionBlurNewDebugUserCustom) // Hack the shader to output whatever you want for debugging purposes here
		{
			pSrt->m_motionBlurResult[pixelXY] = finalColor;
			pSrt->m_debugData[pixelXY] = float4(1.0f, 0.0f, 1.0f, 1.0f);
		}
		else
		{
			pSrt->m_motionBlurResult[pixelXY] = finalColor;
		}
#endif
	}
}


//--------------------------------------------------------------------------------------
struct ComputeBlendWeightSrt
{
	Texture2D<float4> m_motionVector;
	Texture2D<uint> m_characterMask;
	Texture2D<float2> m_neighborMaxVelocity;
	Texture2D<float> m_vignetteBlurAlpha;
	Texture2D<uint> m_gatheredSamplesCount;
	RWTexture2D<float> m_motionBlurAlpha;

	uint2 m_motionVectorBufferDims;
	uint m_tileSize;
	float m_frameTimeExposurePercent;
	float m_velocityTriggerThreshold;
	float m_invVelocityStartRange;
	float m_vignetteBlurScale;
	uint m_isHalfRes;
	uint m_usePerPixelSampleCounting;
	uint m_perPixelNumSamplesThreshold;

	uint m_debugMode;

	SamplerTable *m_pSamplers;
};

[numthreads(8, 8, 1)]
void CS_MotionBlurComputeBlendWeight(uint3 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, ComputeBlendWeightSrt* pSrt : S_SRT_DATA)
{
	uint2 pixelXY = dispatchThreadId.xy;
	uint2 motionVectorDims = pSrt->m_motionVectorBufferDims;
	uint2 halfResMotionVectorDims = pSrt->m_motionVectorBufferDims / 2;
	float2 uv = (float2(pixelXY)+float2(0.5f)) * FastRcp(float2(motionVectorDims));

	// check extra mask to see if pixel is a character pixel
	bool isChar = false;
	uint extraMask = pSrt->m_characterMask[pixelXY];
	if (extraMask & MASK_BIT_EXTRA_CHARACTER)
	{
		isChar = true;
	}

	float2 pixelMotionVec = pSrt->m_motionVector[pixelXY];
	float2 pixelHalfSpreadVelocity = CalculateHalfSpreadVelocity(pixelMotionVec, (pSrt->m_isHalfRes ? halfResMotionVectorDims : motionVectorDims), pSrt->m_tileSize, pSrt->m_frameTimeExposurePercent);

	// if character pixel, we use the per-pixel velocity to calculate the blend weight
	// if no characeter pixel, we use the neighbor max velocity
	// this prevents weird character motion blur issues around the edges of the character where you would see slight motion in the tiles or half res artifacts
	float2 blurVelocity = float2(0.0f);
	if (isChar)
	{
		blurVelocity = pixelHalfSpreadVelocity;		
	}
	else
	{
		uint2 tileXY = (pSrt->m_isHalfRes ? pixelXY / 2 : pixelXY) / pSrt->m_tileSize;
		blurVelocity = pSrt->m_neighborMaxVelocity[tileXY];
	}

	blurVelocity *= pSrt->m_frameTimeExposurePercent;

	// Pixel has not enough motion but the tile does, lets see if the pixel *actually* needs to take motion blur data instead of sharpened pixel
	float motionBlurBlendWeight = GetBlurBlendWeight(length(blurVelocity), pSrt->m_velocityTriggerThreshold, pSrt->m_invVelocityStartRange);

	if (all(pSrt->m_usePerPixelSampleCounting /* && perPixelBlendWeight <= 0.75f && motionBlurBlendWeight > 0.0f) */))
	{
		//uint numGatheredSamples = pSrt->m_gatheredSamplesCount.SampleLevel(pSrt->m_pSamplers->g_linearClampSampler, uv, 0);
		uint numGatheredSamples = pSrt->m_gatheredSamplesCount[pSrt->m_isHalfRes ? pixelXY / 2 : pixelXY];
		if (numGatheredSamples == 0)
		{
			motionBlurBlendWeight = 0.0f;
		}
		else
		{
			float2 scaledPixelVelocity = pixelHalfSpreadVelocity * pSrt->m_frameTimeExposurePercent;
			float perPixelBlendWeight = GetBlurBlendWeight(FastLength(scaledPixelVelocity), pSrt->m_velocityTriggerThreshold, pSrt->m_invVelocityStartRange);

			float t = saturate((float)numGatheredSamples * 0.3333333f); // divide by 3.0f, we lerp from per pixel to the tile blend over a sample count of 3 samples
			motionBlurBlendWeight = lerp(perPixelBlendWeight, motionBlurBlendWeight, t);
		}		
	}

#if WITH_VIGNETTE
	float vignetteBlurAlpha = pSrt->m_vignetteBlurAlpha.SampleLevel(pSrt->m_pSamplers->g_linearClampSampler, uv, 0);

	if (motionBlurBlendWeight <= 0.0f)
	{
		pSrt->m_motionBlurAlpha[pixelXY] = vignetteBlurAlpha;
	}
	else
	{
		// if we are not in the "vignette blur zone" and alpha is 1.0 then we should continue to use 1.0 to make sure player stays sharp
		if (vignetteBlurAlpha <= 0.0f)
		{
			pSrt->m_motionBlurAlpha[pixelXY] = motionBlurBlendWeight;
		}
		else
		{
			pSrt->m_motionBlurAlpha[pixelXY] = max(vignetteBlurAlpha, motionBlurBlendWeight);
		}
	}
#else
	pSrt->m_motionBlurAlpha[pixelXY] = motionBlurBlendWeight;
#endif

#if IS_DEBUG
	// force output 0.0 if we are in a debug mode, this ensures we'll write our output to the final frame buffer
	if (pSrt->m_debugMode != kMotionBlurNewDebugModeNone)
	{
		pSrt->m_motionBlurAlpha[pixelXY] = 1.0f;
	}
#endif
}