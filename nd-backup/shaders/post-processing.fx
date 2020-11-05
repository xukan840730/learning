//--------------------------------------------------------------------------------------
// File: post-processing.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "packing.fxi"
#include "global-funcs.fxi"

struct MotionBlurConstants
{
	float4		m_viewSpaceXyParams;
	float4		m_invViewSpaceXyParams;
	float4		m_screenViewParameter;
	float4		m_viewToLastFrameScreen[3];
	float4		m_motionBlurRangeInfo;
	float4		m_motionBlurInfo;
	float4		m_bufferSize;
	float4		m_explosionBlurInfo;		
	float4		m_explosionCenter[5];
	float4		m_explosionFade[5];
};

#define M_PI (3.14159265f)

struct SsMblurSrt
{
	Texture2D<float>		depth_buf;
	SamplerState			samplerPoint;
	MotionBlurConstants		consts;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
PixelShaderOutputXy2 PS_ScreenSpaceMblurVector(SsMblurSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	PixelShaderOutputXy2 OUT;

	float depthBufferZ = srt->depth_buf.SampleLevel(srt->samplerPoint, input.Tex, 0).x;
	float viewSpaceZ = (float)(srt->consts.m_screenViewParameter.y /
	                           (depthBufferZ - srt->consts.m_screenViewParameter.x));
	float2 viewSpaceXY =
	    input.Tex.xy * srt->consts.m_viewSpaceXyParams.xy + srt->consts.m_viewSpaceXyParams.zw;

	float4 vsPosition = float4(viewSpaceXY * viewSpaceZ, viewSpaceZ, 1.0);

	float3 lastFrameViewSpacePos;
	lastFrameViewSpacePos.x = dot(vsPosition, srt->consts.m_viewToLastFrameScreen[0]);
	lastFrameViewSpacePos.y = dot(vsPosition, srt->consts.m_viewToLastFrameScreen[1]);
	lastFrameViewSpacePos.z = dot(vsPosition, srt->consts.m_viewToLastFrameScreen[2]);

	float2 screenSpaceXy = lastFrameViewSpacePos.xy / lastFrameViewSpacePos.z;

	float2 lastFrameUv = screenSpaceXy * float2(0.5, -0.5f) + 0.5f;

	float2 montionBlurVector = lastFrameUv - input.Tex.xy;

	float2 motionBlurVecPixels = srt->consts.m_bufferSize.xy;

	float2 motionBlurScaleFactorXy = saturate(viewSpaceZ * srt->consts.m_motionBlurRangeInfo.xy +
	                                          srt->consts.m_motionBlurRangeInfo.zw);
	float motionBlurScaleFactor = saturate(motionBlurScaleFactorXy.x + motionBlurScaleFactorXy.y);
	motionBlurScaleFactor *=
	    saturate(sqrt(dot(motionBlurVecPixels, motionBlurVecPixels)) * srt->consts.m_motionBlurInfo.y +
	             srt->consts.m_motionBlurInfo.z) *
	    srt->consts.m_motionBlurInfo.x;
	montionBlurVector *= motionBlurScaleFactor;

	float2 explosionVec = 0;
	for (int i = 0; i < asuint(srt->consts.m_explosionBlurInfo.x); i++)
	{
		float3 direction = vsPosition.xyz - srt->consts.m_explosionCenter[i].xyz;
		float distance2 = dot(direction, direction);

		float lerpFactor = distance2 * srt->consts.m_explosionFade[i].x;
		float theta = M_PI * 3 / 2 - srt->consts.m_explosionCenter[i].w * M_PI * 4 + sqrt(lerpFactor) * M_PI * 4;
		float scale = saturate((theta + 3.14169265 * 9.0 / 2.0) / (4.0 * M_PI));
		scale = theta > M_PI * 3 / 2 ? 0.0 : scale;
		float blurStrength =
		    scale * srt->consts.m_explosionFade[i].z * (0.4f * sin(theta) + 0.6f) * exp2(-lerpFactor);
		float3 explosionNewPosVS = vsPosition.xyz + blurStrength * normalize(direction);

		float2 explosionPosSS =
		    explosionNewPosVS.xy * srt->consts.m_invViewSpaceXyParams.xy / explosionNewPosVS.z;
		explosionVec += ((explosionPosSS * float2(0.5f, -0.5f) + 0.5f) - input.Tex.xy);
	}

	OUT.color0 = explosionVec;
	OUT.color1 = montionBlurVector;

	return OUT;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
const static int2 depthOffsets[5] = {int2(-1, -1), int2(1, -1), int2(-1, 1), int2(1, 1), int2(0, 0)};

struct DilateStencilTextures
{
	Texture2D<float> depthBuffer;
	Texture2D<uint> stencilBuffer;
	SamplerState samplerPoint;
};

struct DilateStencilSrt
{
	DilateStencilTextures* pTextures;
};

uint PS_DilateStencil(DilateStencilSrt srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float centerDepth = srt.pTextures->depthBuffer.SampleLevel(srt.pTextures->samplerPoint, input.Tex, 0.f);

	float minDepth = centerDepth;
	uint minDepthIndex = 4;
	for (uint i = 0; i < 4; i++)
	{
		float neighborDepth = srt.pTextures->depthBuffer.SampleLevel(srt.pTextures->samplerPoint, input.Tex, 0.f, depthOffsets[i]);

		if (neighborDepth < minDepth)
		{
			minDepth = neighborDepth;
			minDepthIndex = i;
		}
	}

	return srt.pTextures->stencilBuffer.SampleLevel(srt.pTextures->samplerPoint, input.Tex, 0.f, depthOffsets[minDepthIndex]);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
struct DilateStencilPrepassTextures
{
	Texture2D<float> depthBuffer;
	Texture2D<float> opaqueAlphaDepthBuffer;
	Texture2D<uint> stencilBuffer;
	Texture2D<uint> opaqueAlphaStencilBuffer;
	SamplerState samplerPoint;
};

struct DilateStencilPrepassSrt
{
	DilateStencilPrepassTextures* pTextures;
};

float PS_DilateStencilPrepass(DilateStencilPrepassSrt srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float depth = srt.pTextures->depthBuffer.SampleLevel(srt.pTextures->samplerPoint, input.Tex, 0.f);
	float opaqueAlphaDepth = srt.pTextures->opaqueAlphaDepthBuffer.SampleLevel(srt.pTextures->samplerPoint, input.Tex, 0.f);
	uint stencil = srt.pTextures->stencilBuffer.SampleLevel(srt.pTextures->samplerPoint, input.Tex, 0.f) & 0x18;
	uint opaqueAlphaStencil = srt.pTextures->opaqueAlphaStencilBuffer.SampleLevel(srt.pTextures->samplerPoint, input.Tex, 0.f) & 0x18;

	return stencil == opaqueAlphaStencil ? depth : opaqueAlphaDepth;
}