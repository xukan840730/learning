//--------------------------------------------------------------------------------------
// File: post-processing.fx
//
// Copyright (c) Naughty Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "global-funcs.fxi"
#include "packing.fxi"
#include "ShaderFastMathLib.h"
#include "quat.fxi"
#include "ssct.fxi"

#pragma argument(scheduler=minpressure)

static const int NUM_SAMPLES_PER_FRAME = 4;
static const int TILE_SIZE = 4;
static const uint SSDO_NUM_SAMPLES = 4;

// regular tetrahedron corners aligned to z-axis
static const float3 sampleKernelSsdo[SSDO_NUM_SAMPLES] =
{
	float3(0.0f, 0.0f, 1.0f),
	float3(0.94280904f, 0.0f, -0.33333333f),
	float3(-0.47140452f, 0.81649658f, -0.33333333f),
	float3(-0.47140452f, -0.81649658f, -0.33333333f)
};

struct SsaoConstants
{
	float4		m_screenToViewParams[2];
	float4		m_worldToViewMat[3];
	float4x4	m_viewToScreenMat;
	float4		m_targetParams;
	float4		m_distanceParams;
	float4		m_intensityParams;
	float4		m_screenSizeParams;
	float4		m_coneTraceParams;
	uint4		m_sampleParams;
	float4		m_blurParams[TILE_SIZE];
	float		m_maxValidDist;
	float		m_invZoom;
	float		m_depthBias;
	float		m_slopeScaledDepthBias;
	uint		m_coneTraceSampleCount;
	uint		m_hasWater;
	float		m_cornerTolerance;
};

struct SsaoRwBuf
{
	RWTexture2D<float4> rwt_result;
};

struct SsaoBufs 
{
	Texture2D<float>	tex_depthWithHeightmap;
	Texture2D<uint4>    tex_normal;
	Texture3D<float2>   tex_sampleOffset;
	Texture2D<float4>   tex_randomNormals;
	Texture2D<float2>   tex_jitterOffsets;
	Texture2D<uint>		tex_stencil;
	Texture2D<uint4>	tex_gBuffer1;
};

struct SsaoSamplers
{
	SamplerState	    smp_point;
};

struct SsaoBlurBufs
{
	Texture2D<float>	tex_depth;
	Texture2D<float4>   tex_noisySsao;
};

struct SsaoPsSrt
{
	SsaoBufs *          pBufs;
	SsaoConstants *     pConsts;
	SsaoSamplers *      pSamplers;
};

struct SsaoPsBlurSrt
{
	SsaoBlurBufs *      pBufs;
	SsaoConstants *     pConsts;
	SsaoSamplers *      pSamplers;
};

struct SsaoCsSrt
{
	SsaoBufs *          pBufs;
	SsaoConstants *     pConsts;
	SsaoSamplers *      pSamplers;
	SsaoRwBuf *         pOutput;
};

struct SsaoCsBlurSrt
{
	SsaoBlurBufs *      pBufs;
	SsaoConstants *     pConsts;
	SsaoSamplers *      pSamplers;
	SsaoRwBuf *         pOutput;
};

// 2 full-rate ALU
float FourthRoot(in float value)
{
	return fastSqrtNR0(fastSqrtNR0(value));
}

float2 ComputeSsdo(in SsaoConstants *pConsts, in SsaoBufs *pBufs, in SsaoSamplers *pSamplers,
				   in int2 screenCoords, in float2 centerTC, in bool selfShadows, in bool useHeightMap, in bool isForeground)
{
	if (!useHeightMap)
	{
		// not using heightmap, need to check for disable ssao separately
		uint gBufferSample = pBufs->tex_gBuffer1.SampleLevel(pSamplers->smp_point, centerTC, 0.0f).y;
		bool enableSSAO = gBufferSample & (1 << 14);
		if (!enableSSAO)
			return float2(1.0, 1.0);
	}

	float centerDepth = abs(LoadLinearDepth(screenCoords, pBufs->tex_depthWithHeightmap,
											pConsts->m_screenToViewParams[0], useHeightMap));

	// Some early exits
	if (centerDepth > pConsts->m_maxValidDist)
		return 1;

	if (pConsts->m_hasWater)
	{
		// Read the stencil buffer to see if this is a water pixel
		// IMPORTANT: CHANGE THIS VALUE IF THE WATER STENCIL BIT CHANGES: StencilBits kStencilBitIsWater
		uint stencil = pBufs->tex_stencil[screenCoords];
		if (stencil & 0x2)
		{
			return 1;
		}
	}

	// position/ normal/ dominant direction of center fragment
	float3 centerPosVS = GetPositionVS(centerTC, centerDepth, pConsts->m_screenToViewParams[0], pConsts->m_screenToViewParams[1]);
	float3 centerNormalWS, dominantDirWS;
	UnpackGBufferNormalAndDominantDir(pBufs->tex_normal.SampleLevel(pSamplers->smp_point, centerTC, 0.0f),
									  pBufs->tex_gBuffer1.SampleLevel(pSamplers->smp_point, centerTC, 0.0f),
									  centerNormalWS,
									  dominantDirWS);
	float3x3 worldToViewMat;
	worldToViewMat[0] = pConsts->m_worldToViewMat[0].xyz;
	worldToViewMat[1] = pConsts->m_worldToViewMat[1].xyz;
	worldToViewMat[2] = pConsts->m_worldToViewMat[2].xyz;
	float3 centerNormalVS = TransformWorldToView(centerNormalWS, worldToViewMat);
	float3 dominantDirVS = TransformWorldToView(dominantDirWS, worldToViewMat);

	// calculate random rotation vector for fetching neighbor samples
	uint2 noiseTC = uint2(screenCoords + pConsts->m_sampleParams.zw) & (TILE_SIZE - 1);
	float3 randomNormal = pBufs->tex_randomNormals.Load(int3(noiseTC, 0)).xyz;

	// keep search radius stable in screen space
	float sampleRadius = pConsts->m_distanceParams.z * centerDepth;

	// create quaternion that rotates from z-axis to centerNormalVS
	Quat rotQuat = CreateRotQuat(sampleKernelSsdo[0], centerNormalVS);

	float occlusion = 0.0f;
	float numValidSamples = 0.0f;

	[unroll]
	for (uint i = 0; i < SSDO_NUM_SAMPLES; i++)
	{
		// rotate kernel samples with random rotation vector
		float3 samplingDir = reflect(sampleKernelSsdo[i], randomNormal);

		// ensure that sample is located in the upper hemisphere
		samplingDir.z = abs(samplingDir.z);

		// rotate sample hemisphere to align to centerNormalVS
		samplingDir = MultiplyVecQuat(samplingDir, rotQuat);

		// importance sampling towards dominant direction
		float lerpFactor = FourthRoot(saturate(dot(samplingDir, dominantDirVS))) * pConsts->m_intensityParams.z;
		samplingDir = normalize(lerp(samplingDir, dominantDirVS, lerpFactor));

		// position of neighbor samples
		float3 samplingPosVS = samplingDir * sampleRadius + centerPosVS;
		float2 sampleTC = GetScreenTC(samplingPosVS, pConsts->m_viewToScreenMat);
		float sampleDepth = SampleLinearDepth(sampleTC, pBufs->tex_depthWithHeightmap, pSamplers->smp_point,
											  pConsts->m_screenToViewParams[0], useHeightMap);
		float3 samplePosVS = GetPositionVS(sampleTC, sampleDepth, pConsts->m_screenToViewParams[0], pConsts->m_screenToViewParams[1]);

		// accumulate occlusion from neighbor samples
		float3 vecToSample = samplePosVS - centerPosVS;
		float vecToSampleLength = length(vecToSample);
		vecToSample /= vecToSampleLength;
		float attenuation = (pConsts->m_distanceParams.x - vecToSampleLength) * pConsts->m_distanceParams.y; 
		occlusion += saturate(dot(centerNormalVS, vecToSample)) * saturate(attenuation);
		numValidSamples += (attenuation > 0.0f) ? 1.0f : 0.0f;
	}

	occlusion *= pConsts->m_intensityParams.y / max(1.0f, numValidSamples);

	// calculate visibility
	float visibility = pow(saturate(1.0f - occlusion), pConsts->m_intensityParams.x);

	// init directional visibility
	float dirVisibility = visibility;

	// add self-shadows for foreground objects
	if (selfShadows)
	{
		if (isForeground)
		{
			// cone trace depth buffer for visibility in dominant direction of ambient lighting
			ConeTraceSetup setup;
			setup.tex_depthWithHeightmap = pBufs->tex_depthWithHeightmap;
			setup.pointSampler = pSamplers->smp_point;
			setup.startPosSS = centerTC;
			setup.startPosVS = centerPosVS;
			setup.normalVS = centerNormalVS;
			setup.coneTraceDirVS = dominantDirVS;
			setup.jitterOffset = pBufs->tex_jitterOffsets.Load(int3(noiseTC, 0)).xy;
			setup.coneTraceParams = pConsts->m_coneTraceParams;
			setup.screenToViewParams = pConsts->m_screenToViewParams[0];
			setup.viewToScreenMat = pConsts->m_viewToScreenMat;
			setup.invZoom = pConsts->m_invZoom;
			setup.fade = 1.0f;
			setup.depthBias = pConsts->m_depthBias;
			setup.slopeScaledDepthBias = pConsts->m_slopeScaledDepthBias;
			setup.sampleCount = pConsts->m_coneTraceSampleCount;
			setup.useHeightMap = useHeightMap;
			dirVisibility = min(dirVisibility, ConeTraceVisibility(setup));
		}
	}

	visibility = lerp(1.0, visibility, pConsts->m_intensityParams.w);
	dirVisibility = lerp(1.0, dirVisibility, pConsts->m_intensityParams.w);

	return float2(dirVisibility, visibility);
}

struct AoResult
{
	float value;
	bool valid;
};

AoResult ComputeAoForSample(SsaoConstants *pConstants, SsaoBufs *pBufs, SsaoSamplers *pSamplers, uint sampleIndex,
                            uint2 samplesCoord, float2 pixelTexCoord, float3 pixelPositionVS,
                            float3 pixelNormalVS, bool useHeightMap)
{
	AoResult result;	
	float2 sampleTexCoords = pBufs->tex_sampleOffset.Load(int4(samplesCoord, sampleIndex, 0)) * pConstants->m_distanceParams.zw + pixelTexCoord;
	float sampleViewSpaceZ = SampleLinearDepth(sampleTexCoords, pBufs->tex_depthWithHeightmap, pSamplers->smp_point,
											   pConstants->m_screenToViewParams[0], useHeightMap);
	float3 samplePositionVS = GetPositionVS(sampleTexCoords, sampleViewSpaceZ, pConstants->m_screenToViewParams[0], pConstants->m_screenToViewParams[1]);

	float3 vecToSample = samplePositionVS - pixelPositionVS;
	float attenuation = (pConstants->m_distanceParams.x - length(vecToSample)) * pConstants->m_distanceParams.y;
	float NdotV = dot(pixelNormalVS, normalize(vecToSample));
	result.valid = attenuation > 0.0;
	result.value = saturate(NdotV) * attenuation * (NdotV > pConstants->m_cornerTolerance ? 1.0 : 0.0);

	return result;
}

float2 ComputeSsao(SsaoConstants *pConsts, SsaoBufs *pBufs, SsaoSamplers *pSamplers,
				   int2 screenCoords, float2 uv, bool selfShadows, bool useHeightMap, bool isForeground)
{
	if (!useHeightMap)
	{
		// not using heightmap, need to check for disable ssao separately
		uint gBufferSample = pBufs->tex_gBuffer1.SampleLevel(pSamplers->smp_point, uv, 0.0f).y;
		bool enableSSAO = gBufferSample & (1 << 14);
		if (!enableSSAO)
			return float2(1.0, 1.0);
	}

	float viewSpaceZ = abs(LoadLinearDepth(screenCoords, pBufs->tex_depthWithHeightmap,
										   pConsts->m_screenToViewParams[0], useHeightMap));

	// Some early exits
	if (viewSpaceZ > pConsts->m_maxValidDist)
		return 1;
	
	if (pConsts->m_hasWater)
	{
		// Read the stencil buffer to see if this is a water pixel
		// IMPORTANT: CHANGE THIS VALUE IF THE WATER STENCIL BIT CHANGES: StencilBits kStencilBitIsWater
		uint stencil = pBufs->tex_stencil[screenCoords];
		if (stencil & 0x2)
		{
			return 1;
		}
	}

	float3 positionVS = GetPositionVS(uv, viewSpaceZ, pConsts->m_screenToViewParams[0], pConsts->m_screenToViewParams[1]);

	float3 normalWS = UnpackGBufferNormal(pBufs->tex_normal.SampleLevel(pSamplers->smp_point, uv, 0.0f));
	float3 normalVS;
	normalVS.x = dot(pConsts->m_worldToViewMat[0].xyz, normalWS);
	normalVS.y = dot(pConsts->m_worldToViewMat[1].xyz, normalWS);
	normalVS.z = dot(pConsts->m_worldToViewMat[2].xyz, normalWS);

	uint2 samplesCoord = uint2(screenCoords + pConsts->m_sampleParams.zw) & (TILE_SIZE - 1);

	float occlusion = 0.0f;

	for (uint i = 0; i < pConsts->m_sampleParams.x / 2; i++)
	{
		AoResult result = 
			ComputeAoForSample(pConsts, pBufs, pSamplers,
			                   2 * i + 0 + pConsts->m_sampleParams.y, samplesCoord, uv, positionVS, normalVS, useHeightMap);
		AoResult resultSymmetry =
			ComputeAoForSample(pConsts, pBufs, pSamplers,
		                       2 * i + 1 + pConsts->m_sampleParams.y, samplesCoord, uv, positionVS, normalVS, useHeightMap);

		if (result.valid)
		{
			occlusion += result.value;
			if(resultSymmetry.valid)
				occlusion += resultSymmetry.value;
			else
				occlusion += result.value;
		}
		else
		{
			if (resultSymmetry.valid) 
				occlusion += 2.0 * resultSymmetry.value;
		}
	}

	occlusion *= pConsts->m_intensityParams.y;

	float visibility = pow(saturate(1.f - occlusion), pConsts->m_intensityParams.x);

	// init directional visibility
	float dirVisibility = visibility;

	// add self-shadows for foreground objects
	if (selfShadows)
	{
		if (isForeground)
		{
			// calculate dominant direction of ambient lighting in view-space
			float3 dominantDirWS = UnpackGBufferDominantDir(pBufs->tex_gBuffer1.SampleLevel(pSamplers->smp_point, uv, 0.0f));
			float3x3 worldToViewMat;
			worldToViewMat[0] = pConsts->m_worldToViewMat[0].xyz;
			worldToViewMat[1] = pConsts->m_worldToViewMat[1].xyz;
			worldToViewMat[2] = pConsts->m_worldToViewMat[2].xyz;
			float3 dominantDirVS = TransformWorldToView(dominantDirWS, worldToViewMat);

			// cone trace depth buffer for visibility in dominant direction of ambient lighting
			ConeTraceSetup setup;
			setup.tex_depthWithHeightmap = pBufs->tex_depthWithHeightmap;
			setup.pointSampler = pSamplers->smp_point;
			setup.startPosSS = uv;
			setup.startPosVS = positionVS;
			setup.normalVS = normalVS;
			setup.coneTraceDirVS = dominantDirVS;
			setup.jitterOffset = pBufs->tex_jitterOffsets.Load(int3(samplesCoord, 0)).xy;
			setup.coneTraceParams = pConsts->m_coneTraceParams;
			setup.screenToViewParams = pConsts->m_screenToViewParams[0];
			setup.viewToScreenMat = pConsts->m_viewToScreenMat;
			setup.invZoom = pConsts->m_invZoom;
			setup.fade = 1.0f;
			setup.depthBias = pConsts->m_depthBias;
			setup.slopeScaledDepthBias = pConsts->m_slopeScaledDepthBias;
			setup.sampleCount = pConsts->m_coneTraceSampleCount;
			setup.useHeightMap = useHeightMap;
			dirVisibility = min(dirVisibility, ConeTraceVisibility(setup));
		}
	}

	visibility = lerp(1.0, visibility, pConsts->m_intensityParams.w);
	dirVisibility = lerp(1.0, dirVisibility, pConsts->m_intensityParams.w);

	return float2(dirVisibility, visibility);
}

float ComputeSsaoNoNormal(SsaoConstants *pConsts, SsaoBufs *pBufs, SsaoSamplers *pSamplers,
						  int2 screenCoords, float2 uv)
{
	float viewSpaceZ = abs(LoadLinearDepth(screenCoords, pBufs->tex_depthWithHeightmap,
										   pConsts->m_screenToViewParams[0], false));

	// Some early exits
	if (viewSpaceZ > pConsts->m_maxValidDist)
		return 1;
	
	if (pConsts->m_hasWater)
	{
		// Read the stencil buffer to see if this is a water pixel
		// IMPORTANT: CHANGE THIS VALUE IF THE WATER STENCIL BIT CHANGES: StencilBits kStencilBitIsWater
		uint stencil = pBufs->tex_stencil[screenCoords];
		if (stencil & 0x2)
		{
			return 1;
		}
	}

	float3 positionVS = GetPositionVS(uv, viewSpaceZ, pConsts->m_screenToViewParams[0], pConsts->m_screenToViewParams[1]);

	float3 positionVS_x, positionVS_y;
	positionVS_x.x = LaneSwizzle(positionVS.x, 0x1f, 0x00, 0x01);
	positionVS_x.y = LaneSwizzle(positionVS.y, 0x1f, 0x00, 0x01);
	positionVS_x.z = LaneSwizzle(positionVS.z, 0x1f, 0x00, 0x01);

	positionVS_y.x = LaneSwizzle(positionVS.x, 0x1f, 0x00, 0x08);
	positionVS_y.y = LaneSwizzle(positionVS.y, 0x1f, 0x00, 0x08);
	positionVS_y.z = LaneSwizzle(positionVS.z, 0x1f, 0x00, 0x08);

	float3 normalVS = normalize(cross(positionVS_x - positionVS.xyz, positionVS_y - positionVS.xyz));
	uint2 oddIndex = screenCoords & 0x01;

	normalVS *= ((oddIndex.x ^ oddIndex.y) > 0 ? 1.0f : -1.0f);

	uint2 samplesCoord = uint2(screenCoords + pConsts->m_sampleParams.zw) & (TILE_SIZE - 1);

	float occlusion = 0.0f;

	for (uint i = 0; i < pConsts->m_sampleParams.x / 2; i++)
	{
		AoResult result = 
			ComputeAoForSample(pConsts, pBufs, pSamplers,
			                   2 * i + 0 + pConsts->m_sampleParams.y, samplesCoord, uv, positionVS, normalVS, false);
		AoResult resultSymmetry =
			ComputeAoForSample(pConsts, pBufs, pSamplers,
		                       2 * i + 1 + pConsts->m_sampleParams.y, samplesCoord, uv, positionVS, normalVS, false);

		if (result.valid)
		{
			occlusion += result.value;
			if(resultSymmetry.valid)
				occlusion += resultSymmetry.value;
			else
				occlusion += result.value;
		}
		else
		{
			if (resultSymmetry.valid) 
				occlusion += 2.0 * resultSymmetry.value;
		}
	}

	occlusion *= pConsts->m_intensityParams.y;

	float visibility = pow(saturate(1.f - occlusion), pConsts->m_intensityParams.x);

	visibility = lerp(1.0, visibility, pConsts->m_intensityParams.w);

	return visibility;
}

float2 BilateralBlur(in SsaoConstants *pConsts, in SsaoBlurBufs *pBufs, in SsaoSamplers *pSamplers, in float2 centerTC, bool vertBlur)
{
	float centerDepth = GetLinearDepth(pBufs->tex_depth.SampleLevel(pSamplers->smp_point, centerTC, 0).r, pConsts->m_screenToViewParams[0]);
	float2 centerSsao = pBufs->tex_noisySsao.SampleLevel(pSamplers->smp_point, centerTC, 0.0f).ra;

	float2 totalValue = centerSsao;
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
		float weight = (1.0f - saturate(depthDiff * pConsts->m_screenSizeParams.z)) * pConsts->m_blurParams[i].z;
		totalValue += pBufs->tex_noisySsao.SampleLevel(pSamplers->smp_point, sampleTC, 0.0f).ra * weight;
		totalWeight += weight;
	}

	float2 result = totalValue / totalWeight;
	return result;
}


//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

float4 PS_SsaoNull(PS_PosTex input) : SV_Target
{
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
}

float4 PS_Ssao(PS_PosTex input, SsaoPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool selfShadows = false;
	const bool useHeightMap = false;

	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[(int2)input.Pos.xy];
	const bool isForeground = ((stencil & 0x20) != 0);

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float2 result = ComputeSsao(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, selfShadows, useHeightMap, isForeground);
	return float4(result.xxx, result.y);
}

float4 PS_Ssao_Ssct(PS_PosTex input, SsaoPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool selfShadows = true;
	const bool useHeightMap = false;

	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[(int2)input.Pos.xy];
	const bool isForeground = ((stencil & 0x20) != 0);

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float2 result = ComputeSsao(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, selfShadows, useHeightMap, isForeground);
	return float4(result.xxx, result.y);
}

float4 PS_Ssao_Height(PS_PosTex input, SsaoPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool selfShadows = false;
	const bool useHeightMap = true;

	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[(int2)input.Pos.xy];
	const bool isForeground = ((stencil & 0x20) != 0);

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float2 result = ComputeSsao(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, selfShadows, useHeightMap, isForeground);
	return float4(result.xxx, result.y);
}

float4 PS_Ssao_Ssct_Height(PS_PosTex input, SsaoPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool selfShadows = true;
	const bool useHeightMap = true;

	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[(int2)input.Pos.xy];
	const bool isForeground = ((stencil & 0x20) != 0);

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float2 result = ComputeSsao(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, selfShadows, useHeightMap, isForeground);
	return float4(result.xxx, result.y);
}

float4 PS_Ssdo(PS_PosTex input, SsaoPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool selfShadows = false;
	const bool useHeightMap = false;

	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[(int2)input.Pos.xy];
	const bool isForeground = ((stencil & 0x20) != 0);

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float2 result = ComputeSsdo(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, selfShadows, useHeightMap, isForeground);
	return float4(result.xxx, result.y);
}

float4 PS_Ssdo_Ssct(PS_PosTex input, SsaoPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool selfShadows = true;
	const bool useHeightMap = false;

	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[(int2)input.Pos.xy];
	const bool isForeground = ((stencil & 0x20) != 0);

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float2 result = ComputeSsdo(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, selfShadows, useHeightMap, isForeground);
	return float4(result.xxx, result.y);
}

float4 PS_Ssdo_Height(PS_PosTex input, SsaoPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool selfShadows = false;
	const bool useHeightMap = true;

	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[(int2)input.Pos.xy];
	const bool isForeground = ((stencil & 0x20) != 0);

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float2 result = ComputeSsdo(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, selfShadows, useHeightMap, isForeground);
	return float4(result.xxx, result.y);
}

float4 PS_Ssdo_Ssct_Height(PS_PosTex input, SsaoPsSrt srt : S_SRT_DATA) : SV_Target
{
	const bool selfShadows = true;
	const bool useHeightMap = true;

	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[(int2)input.Pos.xy];
	const bool isForeground = ((stencil & 0x20) != 0);

	float2 uv = input.Pos.xy * srt.pConsts->m_targetParams.xy; // 2 ALUs opposed to using Tex interpolator at 4 ALUs
	float2 result = ComputeSsdo(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)input.Pos.xy, uv, selfShadows, useHeightMap, isForeground);
	return float4(result.xxx, result.y);
}

float4 PS_SsaoSmartBlurX(PS_PosTex input, SsaoPsBlurSrt srt : S_SRT_DATA) : SV_Target
{
	const bool vertBlur = false;

	float2 result = BilateralBlur(srt.pConsts, srt.pBufs, srt.pSamplers, input.Tex, vertBlur);
	return float4(result.xxx, result.y);
}

float4 PS_SsaoSmartBlurY(PS_PosTex input, SsaoPsBlurSrt srt : S_SRT_DATA) : SV_Target
{
	const bool vertBlur = true;

	float2 result = BilateralBlur(srt.pConsts, srt.pBufs, srt.pSamplers, input.Tex, vertBlur);
	return float4(result.xxx, result.y);
}

float4 PS_SsaoBlurX(PS_PosTex input, SsaoPsBlurSrt srt : S_SRT_DATA) : SV_Target
{
	float2 totalValue = float2(0.0f, 0.0f);
	for (int i = 0; i <= TILE_SIZE; i++)
	{
		totalValue += srt.pBufs->tex_noisySsao.SampleLevel(srt.pSamplers->smp_point, input.Tex, 0.f, int2(-TILE_SIZE / 2 + i, 0)).ra;
	}

	float2 result = totalValue / (TILE_SIZE + 1);
	return float4(result.xxx, result.y);
}

float4 PS_SsaoBlurY(PS_PosTex input, SsaoPsBlurSrt srt : S_SRT_DATA) : SV_Target
{
	float2 totalValue = float2(0.0f, 0.0f);
	for (int i = 0; i <= TILE_SIZE; i++)
	{
		totalValue += srt.pBufs->tex_noisySsao.SampleLevel(srt.pSamplers->smp_point, input.Tex, 0, int2(0, -TILE_SIZE / 2 + i)).ra;
	}

	float2 result = totalValue / (TILE_SIZE + 1);
	return float4(result.xxx, result.y);
}


//--------------------------------------------------------------------------------------
// Compute Shader
//--------------------------------------------------------------------------------------

[numthreads(8, 8, 1)] 
void CS_Ssao(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsSrt srt : S_SRT_DATA)
{
	const bool selfShadows = false;
	const bool useHeightMap = false;

	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[dispatchThreadId.xy];
	const bool isForeground = ((stencil & 0x20) != 0);

#if SSAO_ON_FG_ONLY
	if (!isForeground)
	{
		srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
#endif

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float2 result = ComputeSsao(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, selfShadows, useHeightMap, isForeground);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}

[numthreads(8, 8, 1)]
void CS_Ssao_Ssct(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsSrt srt : S_SRT_DATA)
{
	const bool selfShadows = true;
	const bool useHeightMap = false;
	
	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[dispatchThreadId.xy];
	const bool isForeground = ((stencil & 0x20) != 0);
	
#if SSAO_ON_FG_ONLY
	if (!isForeground)
	{
		srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
#endif

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float2 result = ComputeSsao(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, selfShadows, useHeightMap, isForeground);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}

[numthreads(8, 8, 1)]
void CS_Ssao_Height(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsSrt srt : S_SRT_DATA)
{
	const bool selfShadows = false;
	const bool useHeightMap = true;
	
	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[dispatchThreadId.xy];
	const bool isForeground = ((stencil & 0x20) != 0);
	
#if SSAO_ON_FG_ONLY
	if (!isForeground)
	{
		srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
#endif

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float2 result = ComputeSsao(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, selfShadows, useHeightMap, isForeground);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}

[numthreads(8, 8, 1)]
void CS_Ssao_Ssct_Height(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsSrt srt : S_SRT_DATA)
{
	const bool selfShadows = true;
	const bool useHeightMap = true;
	
	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[dispatchThreadId.xy];
	const bool isForeground = ((stencil & 0x20) != 0);
	
#if SSAO_ON_FG_ONLY
	if (!isForeground)
	{
		srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
#endif

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float2 result = ComputeSsao(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, selfShadows, useHeightMap, isForeground);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}

[numthreads(8, 8, 1)]
void CS_Ssdo(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsSrt srt : S_SRT_DATA)
{
	const bool selfShadows = false;
	const bool useHeightMap = false;
	
	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[dispatchThreadId.xy];
	const bool isForeground = ((stencil & 0x20) != 0);
	
#if SSAO_ON_FG_ONLY
	if (!isForeground)
	{
		srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
#endif

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float2 result = ComputeSsdo(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, selfShadows, useHeightMap, isForeground);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}

[numthreads(8, 8, 1)]
void CS_Ssdo_Ssct(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsSrt srt : S_SRT_DATA)
{
	const bool selfShadows = true;
	const bool useHeightMap = false;
	
	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[dispatchThreadId.xy];
	const bool isForeground = ((stencil & 0x20) != 0);
	
#if SSAO_ON_FG_ONLY
	if (!isForeground)
	{
		srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
#endif

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float2 result = ComputeSsdo(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, selfShadows, useHeightMap, isForeground);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}

[numthreads(8, 8, 1)]
void CS_Ssdo_Height(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsSrt srt : S_SRT_DATA)
{
	const bool selfShadows = false;
	const bool useHeightMap = true;
	
	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[dispatchThreadId.xy];
	const bool isForeground = ((stencil & 0x20) != 0);
	
#if SSAO_ON_FG_ONLY
	if (!isForeground)
	{
		srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
#endif

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float2 result = ComputeSsdo(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, selfShadows, useHeightMap, isForeground);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}

[numthreads(8, 8, 1)]
void CS_Ssdo_Ssct_Height(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsSrt srt : S_SRT_DATA)
{
	const bool selfShadows = true;
	const bool useHeightMap = true;
	
	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[dispatchThreadId.xy];
	const bool isForeground = ((stencil & 0x20) != 0);
	
#if SSAO_ON_FG_ONLY
	if (!isForeground)
	{
		srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
#endif

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float2 result = ComputeSsdo(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv, selfShadows, useHeightMap, isForeground);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}

[numthreads(8, 8, 1)] 
void CS_SsaoNoNormal(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsSrt srt : S_SRT_DATA)
{
	// Read the stencil buffer to see if this is a foreground object pixel
	// IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!
	uint stencil = srt.pBufs->tex_stencil[dispatchThreadId.xy];
	const bool isForeground = ((stencil & 0x20) != 0);
	
#if SSAO_ON_FG_ONLY
	if (!isForeground)
	{
		srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
#endif

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float result = ComputeSsaoNoNormal(srt.pConsts, srt.pBufs, srt.pSamplers, (int2)dispatchThreadId.xy, uv);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = result;
}

[numthreads(64, 1, 1)]
void CS_SsaoSmartBlurX(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsBlurSrt srt : S_SRT_DATA)
{
	const bool vertBlur = false;

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float2 result = BilateralBlur(srt.pConsts, srt.pBufs, srt.pSamplers, uv, vertBlur);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}

[numthreads(8, 8, 1)]
void CS_SsaoSmartBlurY(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsBlurSrt srt : S_SRT_DATA)
{
	const bool vertBlur = true;

	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;
	float2 result = BilateralBlur(srt.pConsts, srt.pBufs, srt.pSamplers, uv, vertBlur);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}


groupshared float2 g_blurVals[64 + TILE_SIZE];

[numthreads(64, 1, 1)] 
void CS_SsaoBlurX(int3 dispatchThreadId : SV_DispatchThreadID, uint2 groupThreadId : SV_GroupThreadID, SsaoCsBlurSrt srt : S_SRT_DATA)
{
	{
		int2 coord = dispatchThreadId.xy - int2(TILE_SIZE/2, 0);
		g_blurVals[groupThreadId.x] = srt.pBufs->tex_noisySsao[coord].ra;
	}

	if (groupThreadId.x >= 64 - TILE_SIZE)
	{
		int2 coord = dispatchThreadId.xy + int2(TILE_SIZE, 0);
		g_blurVals[groupThreadId.x + TILE_SIZE] = srt.pBufs->tex_noisySsao[coord].ra;
	}

	float2 totalValue = float2(0.0f, 0.0f);

	for (uint i = 0; i <= TILE_SIZE; i++)
	{
		totalValue += g_blurVals[groupThreadId.x + i];
	}

	float2 result = totalValue / (TILE_SIZE + 1);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}

[numthreads(8, 8, 1)] 
void CS_SsaoBlurY(uint3 dispatchThreadId : SV_DispatchThreadID, SsaoCsBlurSrt srt : S_SRT_DATA)
{
	float2 totalValue = float2(0.0f, 0.0f);
	float2 screenUv = (dispatchThreadId.xy + 0.5f) * srt.pConsts->m_targetParams.xy;

	for (uint i = 0; i <= TILE_SIZE; i++)
	{
		totalValue += srt.pBufs->tex_noisySsao.SampleLevel(srt.pSamplers->smp_point, screenUv, 0.f, int2(0, -TILE_SIZE / 2 + i)).ra;
	}

	float2 result = totalValue / (TILE_SIZE + 1);
	srt.pOutput->rwt_result[dispatchThreadId.xy] = float4(result.xxx, result.y);
}
