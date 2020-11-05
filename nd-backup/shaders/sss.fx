//--------------------------------------------------------------------------------------
// File: sss.fx
//
// Copyright (c) Naughty Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "global-funcs.fxi"
#include "runtime-lights-common.fxi"
#include "packing.fxi"
#include "ShaderFastMathLib.h"
#include "ssct.fxi"

#pragma argument(scheduler=minpressure)

static const int TILE_SIZE = 4;
static const uint THREAD_GROUP_SIZE_XY = 8;
static const float EPSILON = 0.0001f;

// Right now CalculateBlurredDitherShadow() clamps the shadow mask to a minimum of 1.0f / 255.0f.
// Therefore we use this value as well to determine if we need to calculate SSS and to clamp the
// final shadow value. As soon as the clamp will be removed from CalculateBlurredDitherShadow(),
// the value below must be adjusted accordingly.
static const float FULLY_INSIDE_UMBRA = EPSILON;

struct SssConstants
{
	float4		m_screenToViewParams[2];
	float4		m_worldToViewMat[3];
	float4x4	m_viewToScreenMat;
	float4		m_targetParams;
	float4		m_coneTraceParams;
	float4		m_lightDirVSAndCosHalfAngle;
	float4		m_lightPosVSAndRadius;
	float4		m_fadeParams;
	uint4		m_sampleParams;
	float4		m_blurParams[TILE_SIZE];
	float		m_invBlurDepthThreshold;
	float		m_maxValidDist;
	float		m_invZoom;
	float		m_depthBias;
	float		m_slopeScaledDepthBias;
	uint		m_coneTraceSampleCount;
	uint		m_hasWater;
	uint		m_lightType;
};

struct SssRwBuf
{
	RWTexture2D<float> rwt_result;
};

struct SssBufs 
{
	Texture2D<float>	tex_depthWithHeightmap;
	Texture2D<uint>		tex_stencil;
	Texture2D<uint4>	tex_normal;
	Texture2D<float2>	tex_jitterOffsets;
	Texture2D<uint4>	tex_gBuffer1;
	Texture2D<float>	tex_sunShadowMask;
};

struct SssSamplers
{
	SamplerState		smp_point;
	SamplerState		smp_linear;
};

struct SssBlurBufs
{
	Texture2D<float>	tex_depth;
	Texture2D<float>	tex_noisySss;
};

struct SssPsSrt
{
	SssBufs *          pBufs;
	SssConstants *     pConsts;
	SssSamplers *      pSamplers;
};

struct SssPsBlurSrt
{
	SssBlurBufs *      pBufs;
	SssConstants *     pConsts;
	SssSamplers *      pSamplers;
};

struct SssCsSrt
{
	SssBufs *          pBufs;
	SssConstants *     pConsts;
	SssSamplers *      pSamplers;
	SssRwBuf *         pOutput;
};

struct SssCsBlurSrt
{
	SssBlurBufs *      pBufs;
	SssConstants *     pConsts;
	SssSamplers *      pSamplers;
	SssRwBuf *         pOutput;
};

float ComputeSss(in SssConstants *pConsts, in SssBufs *pBufs, in SssSamplers *pSamplers, in int2 screenCoords,
				 in float2 centerTC, in bool useRuntimeLight, in bool useHeightMap)
{
	float centerDepth = LoadLinearDepth(screenCoords, pBufs->tex_depthWithHeightmap,
										pConsts->m_screenToViewParams[0], useHeightMap);

	// Some early exits
	if (centerDepth > pConsts->m_maxValidDist)
		return 1;

	// calculate fading value
	float fade = saturate(1.0f - (centerDepth - pConsts->m_fadeParams.x) * pConsts->m_fadeParams.y);

	// sample shadow mask texture generated from shadow map
	float shadowMask = 1.0f;
	if (!useRuntimeLight)
	{
		shadowMask = pBufs->tex_sunShadowMask.SampleLevel(pSamplers->smp_point, centerTC, 0).x;
	}

	if (pConsts->m_hasWater)
	{
		// Read the stencil buffer to see if this is a water pixel
		// IMPORTANT: CHANGE THIS VALUE IF THE WATER STENCIL BIT CHANGES: StencilBits kStencilBitIsWater
		uint stencil = pBufs->tex_stencil[screenCoords];
		if (stencil & 0x2)
		{
			return shadowMask;
		}
	}

	float visibility = shadowMask;
	
	// skip fragments that are in shadow umbra or faded out 
	if ((shadowMask > FULLY_INSIDE_UMBRA) && (fade > EPSILON) && (centerDepth >= -pConsts->m_maxValidDist))
	{
		// position/ normal of center fragment
		float3 centerPosVS = GetPositionVS(centerTC, centerDepth, pConsts->m_screenToViewParams[0], pConsts->m_screenToViewParams[1]);
		float3 centerNormalWS = UnpackGBufferNormal(pBufs->tex_normal.SampleLevel(pSamplers->smp_point, centerTC, 0.0f));
		float3x3 worldToViewMat;
		worldToViewMat[0] = pConsts->m_worldToViewMat[0].xyz;
		worldToViewMat[1] = pConsts->m_worldToViewMat[1].xyz;
		worldToViewMat[2] = pConsts->m_worldToViewMat[2].xyz;
		float3 centerNormalVS = TransformWorldToView(centerNormalWS, worldToViewMat);

		// light direction and culling
		float3 lightDirVS = pConsts->m_lightDirVSAndCosHalfAngle.xyz;
		bool insideLight = true;
		if (useRuntimeLight)
		{
			float3 lightVec = pConsts->m_lightPosVSAndRadius.xyz - centerPosVS;
			float lightVecLen = length(lightVec);
			insideLight = (lightVecLen <= pConsts->m_lightPosVSAndRadius.w);
			lightDirVS = lightVec / max(lightVecLen, 0.000001f);

			if (pConsts->m_lightType == kSpotLight)
			{
				insideLight = insideLight && (dot(lightDirVS, -pConsts->m_lightDirVSAndCosHalfAngle.xyz) >= pConsts->m_lightDirVSAndCosHalfAngle.w);
			}
		}

		if (insideLight)
		{
			// texture coordinates for fetching jitter offsets
			uint2 noiseTC = uint2(screenCoords + pConsts->m_sampleParams.xy) & (TILE_SIZE - 1);

			// cone trace depth buffer for visibility in specified trace direction
			ConeTraceSetup setup;
			setup.tex_depthWithHeightmap = pBufs->tex_depthWithHeightmap;
			setup.pointSampler = pSamplers->smp_point;
			setup.startPosSS = centerTC;
			setup.startPosVS = centerPosVS;
			setup.normalVS = centerNormalVS;
			setup.coneTraceDirVS = lightDirVS;
			setup.jitterOffset = (pConsts->m_sampleParams.z > 0) ? pBufs->tex_jitterOffsets.Load(int3(noiseTC, 0)).xy : float2(0.0f, 0.0f);
			setup.coneTraceParams = pConsts->m_coneTraceParams;
			setup.screenToViewParams = pConsts->m_screenToViewParams[0];
			setup.viewToScreenMat = pConsts->m_viewToScreenMat;
			setup.invZoom = pConsts->m_invZoom;
			setup.fade = fade;
			setup.depthBias = pConsts->m_depthBias;
			setup.slopeScaledDepthBias = pConsts->m_slopeScaledDepthBias;
			setup.sampleCount = pConsts->m_coneTraceSampleCount;
			setup.useHeightMap = useHeightMap;
			visibility = min(visibility, ConeTraceVisibility(setup));
		}
	}
	
	return max(visibility, FULLY_INSIDE_UMBRA);
}

float BilateralBlur(in SssConstants *pConsts, in SssBlurBufs *pBufs, in SssSamplers *pSamplers, in float2 centerTC, bool vertBlur)
{
	float centerDepth = GetLinearDepth(pBufs->tex_depth.SampleLevel(pSamplers->smp_point, centerTC, 0).r, pConsts->m_screenToViewParams[0]);
	float centerSss = pBufs->tex_noisySss.SampleLevel(pSamplers->smp_point, centerTC, 0.0f).r;
	
	float totalValue = centerSss;
	float totalWeight = 1.0f;
	for (int i = 0; i < TILE_SIZE; i++)
	{
		float2 sampleTC = centerTC;

		if (vertBlur)
			sampleTC.y += pConsts->m_blurParams[i].y;
		else
			sampleTC.x += pConsts->m_blurParams[i].x;

		float sampleDepth = GetLinearDepth(pBufs->tex_depth.SampleLevel(pSamplers->smp_point, sampleTC, 0.0f).r, pConsts->m_screenToViewParams[0]);
		float depthDiff = abs(centerDepth - sampleDepth);
		float weight = (1.0f - saturate(depthDiff * pConsts->m_invBlurDepthThreshold)) * pConsts->m_blurParams[i].z;
		totalValue += pBufs->tex_noisySss.SampleLevel(pSamplers->smp_point, sampleTC, 0.0f).r * weight;
		totalWeight += weight;
	}

	float result = totalValue / totalWeight;
	return result;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

float PS_SssSun(PS_PosTex input, SssPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool useRuntimeLight = false;
	const bool useHeightMap = false;

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float result = ComputeSss(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, useRuntimeLight, useHeightMap);
	return result;
}

float PS_SssSunHeight(PS_PosTex input, SssPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool useRuntimeLight = false;
	const bool useHeightMap = true;

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float result = ComputeSss(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, useRuntimeLight, useHeightMap);
	return result;
}

float PS_SssRuntimeLight(PS_PosTex input, SssPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool useRuntimeLight = true;
	const bool useHeightMap = false;

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float result = ComputeSss(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, useRuntimeLight, useHeightMap);
	return result;
}

float PS_SssRuntimeLightHeight(PS_PosTex input, SssPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool useRuntimeLight = true;
	const bool useHeightMap = true;

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float result = ComputeSss(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, useRuntimeLight, useHeightMap);
	return result;
}

float PS_SssSmartBlurX(PS_PosTex input, SssPsBlurSrt srt : S_SRT_DATA) : SV_Target
{
	const bool vertBlur = false;

	float result = BilateralBlur(srt.pConsts, srt.pBufs, srt.pSamplers, input.Tex, vertBlur);
	return result;
}

float PS_SssSmartBlurY(PS_PosTex input, SssPsBlurSrt srt : S_SRT_DATA) : SV_Target
{
	const bool vertBlur = true;

	float result = BilateralBlur(srt.pConsts, srt.pBufs, srt.pSamplers, input.Tex, vertBlur);
	return result;
}

//--------------------------------------------------------------------------------------
// Compute Shader
//--------------------------------------------------------------------------------------

[numthreads(THREAD_GROUP_SIZE_XY, THREAD_GROUP_SIZE_XY, 1)]
void CS_SssSun(uint3 dispatchThreadId : SV_DispatchThreadID, SssCsSrt srt : S_SRT_DATA)
{
	const bool useRuntimeLight = false;
	const bool useHeightMap = false;

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float result = ComputeSss(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, useRuntimeLight, useHeightMap);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = result;
}

[numthreads(THREAD_GROUP_SIZE_XY, THREAD_GROUP_SIZE_XY, 1)]
void CS_SssSunHeight(uint3 dispatchThreadId : SV_DispatchThreadID, SssCsSrt srt : S_SRT_DATA)
{
	const bool useRuntimeLight = false;
	const bool useHeightMap = true;

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float result = ComputeSss(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, useRuntimeLight, useHeightMap);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = result;
}

[numthreads(THREAD_GROUP_SIZE_XY, THREAD_GROUP_SIZE_XY, 1)]
void CS_SssRuntimeLight(uint3 dispatchThreadId : SV_DispatchThreadID, SssCsSrt srt : S_SRT_DATA)
{
	const bool useRuntimeLight = true;
	const bool useHeightMap = false;

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
		float result = ComputeSss(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, useRuntimeLight, useHeightMap);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = result;
}

[numthreads(THREAD_GROUP_SIZE_XY, THREAD_GROUP_SIZE_XY, 1)]
void CS_SssRuntimeLightHeight(uint3 dispatchThreadId : SV_DispatchThreadID, SssCsSrt srt : S_SRT_DATA)
{
	const bool useRuntimeLight = true;
	const bool useHeightMap = true;

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
		float result = ComputeSss(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, useRuntimeLight, useHeightMap);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = result;
}

[numthreads(THREAD_GROUP_SIZE_XY, THREAD_GROUP_SIZE_XY, 1)]
void CS_SssSmartBlurX(uint3 dispatchThreadId : SV_DispatchThreadID, SssCsBlurSrt srt : S_SRT_DATA)
{
	const bool vertBlur = false;

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float result = BilateralBlur(srt.pConsts, srt.pBufs, srt.pSamplers, uv, vertBlur);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = result;
}

[numthreads(THREAD_GROUP_SIZE_XY, THREAD_GROUP_SIZE_XY, 1)]
void CS_SssSmartBlurY(uint3 dispatchThreadId : SV_DispatchThreadID, SssCsBlurSrt srt : S_SRT_DATA)
{
	const bool vertBlur = true;

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float result = BilateralBlur(srt.pConsts, srt.pBufs, srt.pSamplers, uv, vertBlur);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = result;
}
