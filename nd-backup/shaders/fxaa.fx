//--------------------------------------------------------------------------------------
// File: fxaa.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_QUALITY__PRESET 39

#include "global-funcs.fxi"
#include "fxaa3_11.fxi"
#include "post-globals.fxi"

struct FxaaConstants
{
	float m_fxaaQualityRcpFrameX;
	float m_fxaaQualityRcpFrameY;
	float m_fxaaQualitySubpix;
	float m_fxaaQualityEdgeThreshold;
	float m_fxaaQualityEdgeThresholdMin;
	float m_padding[3];
};

struct FxaaData
{
	Texture2D texture;
	SamplerState samplerState; 
};

struct FxaaSrt
{
	FxaaConstants *pConsts;	
	FxaaData *pData;
};

float4 PS_Fxaa(FxaaSrt srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	FxaaTex tex = { srt.pData->samplerState, srt.pData->texture };

	return FxaaPixelShader(
		input.Tex,
		//float4(0, 0, 0, 0),
		tex,
		//tex,
		//tex,
		float2(srt.pConsts->m_fxaaQualityRcpFrameX, srt.pConsts->m_fxaaQualityRcpFrameY), 
		//float4(0, 0, 0, 0),
		//float4(0, 0, 0, 0),
		//float4(0, 0, 0, 0),
		srt.pConsts->m_fxaaQualitySubpix,
		srt.pConsts->m_fxaaQualityEdgeThreshold,
		srt.pConsts->m_fxaaQualityEdgeThresholdMin
		//0,
		//0,
		//0,
		//float4(0, 0, 0, 0)
		);
}
