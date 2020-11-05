//--------------------------------------------------------------------------------------
// File: post-processing.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "runtime-lights-const-buffers.fxi"

Texture2DArray<float>		txSunShadow1			: register( t1 );

#define kMaxNumCascaded		8
struct VolumetricSunlightShaderConst
{
	float4			m_sunDir;
	float4			m_dirVector;
	float4			m_rightVector;
	float4			m_clipPlanes[3];
	float4			m_clipDistance;
	float4			m_volumetricColor;
	float4			m_sampleAngleInfo;
	float4			m_cascadedLevelDist[kMaxNumCascaded / 4];
	float4			m_shadowMat[kMaxNumCascaded * 3];
};

cbuffer VolumetricSunlightShaderParams : register(b2)
{
	VolumetricSunlightShaderConst	g_volumetricSunlightParams;
};

float4 PS_VolSunlightDirect (PS_PosTex input) : SV_Target
{
	float2 tex					= input.Pos.xy * g_runtimeLightParams.m_screenSize.zw;

	float depthBufferZ			= txDepthBuffer.SampleLevel(g_sSamplerPoint, tex, 0).x;
	float viewSpaceZ			= (float)(g_runtimeLightParams.m_screenViewParameter.y / (depthBufferZ - g_runtimeLightParams.m_screenViewParameter.x));
	float2 viewSpaceXY			= tex.xy * g_runtimeLightParams.m_viewSpaceXyParams.xy + g_runtimeLightParams.m_viewSpaceXyParams.zw;

	float4 vsPosition			= float4(viewSpaceXY * viewSpaceZ, viewSpaceZ, 1.0);

	float viewDist				= length(vsPosition.xyz);
	float3 viewDir				= vsPosition.xyz / viewDist;

	float invVDotP0				= 1.0f / dot(viewDir, g_volumetricSunlightParams.m_clipPlanes[0].xyz);
	float invVDotP1				= 1.0f / dot(viewDir, g_volumetricSunlightParams.m_clipPlanes[1].xyz);
	float invVDotP2				= 1.0f / dot(viewDir, g_volumetricSunlightParams.m_clipPlanes[2].xyz);

	float dist0					= -g_volumetricSunlightParams.m_clipPlanes[0].w * invVDotP0;
	float dist1					= -g_volumetricSunlightParams.m_clipPlanes[1].w * invVDotP1;
	float dist2					= -g_volumetricSunlightParams.m_clipPlanes[2].w * invVDotP2;
	float dist3					= g_volumetricSunlightParams.m_clipDistance.x * invVDotP1;
	float dist4					= g_volumetricSunlightParams.m_clipDistance.y * invVDotP2;
	float dist5					= g_volumetricSunlightParams.m_clipDistance.z * invVDotP0;

	float minDist				= -100000.0f;
	float maxDist				= 100000.0f;

	if (invVDotP0 > 0)
	{
		maxDist = min(maxDist, dist0);
		minDist = max(minDist, dist5);
	}
	else
	{
		minDist = max(minDist, dist0);
		maxDist = min(maxDist, dist5);
	}

	if (invVDotP1 > 0)
	{
		maxDist = min(maxDist, dist1);
		minDist = max(minDist, dist3);
	}
	else
	{
		maxDist = min(maxDist, dist3);
		minDist = max(minDist, dist1);
	}

	if (invVDotP2 > 0)
	{
		maxDist = min(maxDist, dist2);
		minDist = max(minDist, dist4);
	}
	else
	{
		maxDist = min(maxDist, dist4);
		minDist = max(minDist, dist2);
	}

	float nearDist = max(minDist, 0);
	float farDist = min(max(maxDist, 0), viewDist);

	int numSamples = 64;

	float stepDist = (farDist - nearDist) / (float)numSamples;

	float sumVisi = 0;
	for (int i = 0; i < numSamples; i++)
	{
		float sampleDist = nearDist + (i + 0.5f) * stepDist;
		float3 positionVS = viewDir * sampleDist;

		int4 cmpResult0 = g_volumetricSunlightParams.m_cascadedLevelDist[0] < positionVS.z;
		int4 cmpResult1 = g_volumetricSunlightParams.m_cascadedLevelDist[1] < positionVS.z;

		int iCascadedLevel = cmpResult0.x + cmpResult0.y + cmpResult0.z + cmpResult0.w + 
							   cmpResult1.x + cmpResult1.y + cmpResult1.z + cmpResult1.w;

		float4 posVS = float4(positionVS, 1.0);
		float3 shadowProjTex;
		shadowProjTex.x = dot(g_volumetricSunlightParams.m_shadowMat[iCascadedLevel * 3 + 0], posVS);
		shadowProjTex.y = dot(g_volumetricSunlightParams.m_shadowMat[iCascadedLevel * 3 + 1], posVS);
		shadowProjTex.z = dot(g_volumetricSunlightParams.m_shadowMat[iCascadedLevel * 3 + 2], posVS);

		float refDepth = shadowProjTex.z;
		float4 shadowTexCoord = float4(shadowProjTex.x, shadowProjTex.y, (float)iCascadedLevel, refDepth);

		sumVisi += txSunShadow1.SampleCmpLevelZero(g_sSamplerShadow, shadowTexCoord.xyz, shadowTexCoord.w).r;
	}

	float integral =
	    (farDist - minDist) *
	        (1.0f + g_volumetricSunlightParams.m_clipPlanes[0].w * g_volumetricSunlightParams.m_clipDistance.w) +
	    0.5f * (farDist * farDist - minDist * minDist) * dot(viewDir, g_volumetricSunlightParams.m_clipPlanes[0].xyz) *
	        g_volumetricSunlightParams.m_clipDistance.w;

	return float4(float3(integral * g_volumetricSunlightParams.m_volumetricColor.xyz * sumVisi / (float)numSamples), 0.0f);
}

