//--------------------------------------------------------------------------------------
// File: post-processing.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "under-water-fog.fxi"

struct PS_PosTex
{
    float4 Pos		: SV_POSITION;
    float2 Tex		: TEXCOORD0;
};

struct SsUnderWaterFogConst
{
	float4		m_params[5];
	uint		m_msaaSampleCount;
	float		m_invMsaaSampleCount;
	float		m_pad[2];
};

struct UnderWaterFogPsTextures
{
	Texture2D<float>		src_depth;
	Texture2D<float4>		src_color;
};

struct UnderWaterFogPsSamplers
{
	SamplerState			samplerPoint;
};

struct UnderWaterFogPsSrt
{
	SsUnderWaterFogConst	*consts;
	UnderWaterFogPsTextures	*texs;
	UnderWaterFogPsSamplers	*smpls;
};

float4 PS_ApplyUnderWaterFog(UnderWaterFogPsSrt srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float4 finalFogColor = srt.texs->src_color.Sample(srt.smpls->samplerPoint, input.Tex);
	float3 absorptionFactor = 1.0f;
	float3 fogColor = 0;
	if (srt.consts->m_params[0].x > 0.0f)
	{
		float depthBufferZ			= srt.texs->src_depth.Sample(srt.smpls->samplerPoint, input.Tex).x;
		float viewSpaceZ			= 1.0f / (depthBufferZ * srt.consts->m_params[2].x + srt.consts->m_params[2].y);
		float2 viewSpaceXY			= input.Tex.xy * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

		float worldPositionY		= dot(srt.consts->m_params[3], float4(viewSpaceXY * viewSpaceZ, viewSpaceZ, 1.0));

		float lightDistance;
		float ScatteringAttenuation;
		fogColor.xyz = UnderWaterFogCalculation(absorptionFactor,
												lightDistance,
												ScatteringAttenuation,
											    srt.consts->m_params[0].y,	//float WaterPlaneHeight, 
												worldPositionY,					//float PixelHeight, 
												srt.consts->m_params[0].z,	//float CameraHeight, 
												viewSpaceZ,						//float SceneDepth,
												srt.consts->m_params[2].z,	//float DrakeDepth,
												srt.consts->m_params[0].w,	//float ScatteringAmount,
												srt.consts->m_params[4].w,	//float NearColorPreservation,
												srt.consts->m_params[1].w,	//float3 CameraVector,
												srt.consts->m_params[2].w,  //float fogBrightnessMultiplier,
												finalFogColor.xyz,
												srt.consts->m_params[1].xyz,
												srt.consts->m_params[4].xyz);	//float3 AbsorptionCoefficient);
	}

	return float4(finalFogColor.xyz * absorptionFactor + fogColor, finalFogColor.w);
}

