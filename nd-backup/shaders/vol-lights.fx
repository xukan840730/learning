//--------------------------------------------------------------------------------------
// File: post-processing.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "global-const-buffers.fxi"


#define kClampCap 0.1f

struct VolumetricLightConst
{
	float4		m_screenSize;
	float4		m_viewSpaceXyParams;
	float4		m_linearDepthParams;
	float4		m_ditherParams;
	float4		m_lightPosAndRadius;
	float4		m_lightDirAndConAngle;
	float4		m_sampleRightVector;
	float4		m_sampleDirVector;
	float4		m_sampleAngleInfo;
	float4		m_sampleHeightDensityInfo;
	float4		m_viewToWorldMatY;
	float4		m_unpackScreenPosParam;
	float4		m_volColor;
};

cbuffer VolumetricLightsParams : register(b2)
{
	VolumetricLightConst	g_volLightParams;
};

#define kMaxNumCascaded		8
struct  VolDirectLightParams
{
	float4					m_screenSize;
	float4					m_viewSpaceXyParams;
	float4					m_linearDepthParams;
	float4					m_directionVS;
	float4					m_sampleRightVS;
	float4					m_sampleDirVS;
	float4					m_sampleAngleInfo;
	float4					m_color;
	float4					m_params;
	float4					m_shadowScreenViewParams;
	float4					m_invViewSpaceParams;
	float4					m_boxVertex[8];
	float4					m_viewFrustumPlanes[4];
	float4					m_clipPlanes[3];
	float4					m_clipDistance;
	float4					m_cascadedLevelDist[kMaxNumCascaded / 4];
//	float					m_shadowZScale[kMaxNumCascaded];
	float4					m_shadowMat[kMaxNumCascaded * 5];
};


cbuffer VolumetricDirectLightsParams : register(b2)
{
	VolDirectLightParams	g_volDirectLightParams;
};

struct VolSunlightMergeConst
{
	float4		m_volColorInfo;
	float4		m_volParams;
};

cbuffer DownsampleParams : register(b2)
{
	VolSunlightMergeConst	g_volLightMergeParams;
};


float GetSpotVolIntegrate(float t0, float t1, float cosHalfConeAngle, float radius, float fNdotD, float fNdotO, float fOdotD, float fOdotO)
{
	float sqr_a = max(fOdotO - fNdotO * fNdotO, 0.0f);
	float x0 = t0 - fNdotO;
	float x1 = t1 - fNdotO;
	float sqr_x0 = x0 * x0;
	float sqr_x1 = x1 * x1;

	float G0 = sqrt(sqr_x0 + sqr_a);
	float G1 = sqrt(sqr_x1 + sqr_a);

	float A = 1.0 / (radius * (1.0 - cosHalfConeAngle));
	float E = radius * (fNdotD * fNdotO - fOdotD) + cosHalfConeAngle * 0.5 * sqr_a;
	float F = log(max(x1 + G1, 0.000001f) / max(x0 + G0, 0.000001f));
	float H = fOdotD - radius * cosHalfConeAngle;

	float vol = radius * fNdotD * (G1 - G0) +
				E * F +
				cosHalfConeAngle * 0.5 * (x1 * G1 - x0 * G0) - 
				fNdotD * 0.5 * (t1 * t1 - t0 * t0) + 
				H * (t1 - t0);
	return A * max(vol, 0.0f);
}
/*
; ======================================================
; ======================================================
; calculating point light volume value, 
; volume = integral(26.0 / 25.0 / (25.0 * d ^ 2 / r ^ 2 + 1.0) - 1.0 / 25.0 , t0, t1);
;	d ^ 2 = dot(Nt - O, Nt - O) <=> d ^ 2 = t ^ 2 - 2 * dot(N, O) * t + dot(O, O);
; let A = 26.0 / 625.0 * r ^ 2;
; let B = 1.0 / 25.0 * r ^ 2 + dot(O, O);
; <=> volume = A * integral( 1.0 / (t ^ 2 - 2 * dot(N, O) * t + B), t0, t1) - 1.0 / 25.0 * integral(t, t0, t1);
; <=> volume = A * integral( 1.0 / ((t - dot(N, O)) ^ 2 + B - dot(N, O) ^ 2), t0, t1) - (t1 - t0) / 25.0;
; <=> volume0 = A * integral( 1.0 / ((t - dot(N, O)) ^ 2 + B - dot(N, O) ^ 2), t0, t1);
; <=> volume1 = - (t1 - t0) / 25.0;
; B - dot(N, O) ^ 2 <=> 1.0 / 25.0 * r ^ 2 + dot(O, O) - dot(N, O) ^ 2;
; let sqrC = B - dot(N, O) ^ 2;
; since dot(O, O) - dot(N, O) ^ 2 always bigger than zero, so sqrC is always bigger than 0; 
; let x = t - dot(N, O);
; let C = sqrt(sqrC);
; <=> volume0 = A * integral ( 1.0 / (x ^ 2 + C ^ 2), t0, t1);
; <=> volume0 = A * (1 / C ^ 2) * integral( 1.0 / ( (x / C) ^ 2 + 1.0f), t0, t1);
; let x1 = t1 - dot(N, O); x0 = t0 - dot(N, O);
; <=> volume0 = A * (1.0 / C ^ 2) * (arctan( x1 / C) - arctan( x0 / C));
*/

float GetSpotVolIntegrateOld(float t0, float t1, float cosHalfConeAngle, float radius, float fNdotD, float fNdotO, float fOdotD, float fOdotO)
{
	float sqrRadius = radius * radius;
	float A = 26.0f / 625.0f * sqrRadius;
	float B = 1.0f / 25.0f * sqrRadius + fOdotO;
	float sqrC = B - fNdotO * fNdotO;
	float C = sqrt(sqrC);
	float x1 = t1 - fNdotO;
	float x0 = t0 - fNdotO;

	float volScale = cosHalfConeAngle < 1.0f ? 1.0f / (radius * (1.0 - cosHalfConeAngle)) : 0.0f;

#if 0
	float volume0 = A / sqrC * (atan(x1 / C) - atan(x0 / C));
	float volume1 = - (t1 - t0) / 25.0f;
		
	return (volume0 + volume1) * volScale;
#else
	float sqrT0Dist = x0 * x0 + sqrC;
	float sqrT1Dist = x1 * x1 + sqrC;
	float sqrT01Dist = sqrT0Dist * sqrT1Dist;
	float angleValue = 0.0f;
	if (sqrT01Dist > 0.0f)
	{
		float acosValue = (x0 * x1 + sqrC) / sqrt(sqrT01Dist);
		angleValue = acos(saturate(acosValue));
	}
	float volumeVal = C > 0.0f ? (angleValue / C * A - (t1 - t0) / 25.0f) : 0.0f;

	return max(volumeVal * volScale, 0.0f);
#endif
}

float4 PS_VolumetricSpotLight (PS_PosTex input) : SV_Target
{
	float4 finalColor = float4(0, 0, 0, 0);
	float3 viewVector = float3(input.Tex.xy * g_volLightParams.m_viewSpaceXyParams.xy + g_volLightParams.m_viewSpaceXyParams.zw, 1.0);
	float viewDist = length(viewVector);
	float radius = g_volLightParams.m_lightPosAndRadius.w;
	float cosHalfConeAngle = g_volLightParams.m_lightDirAndConAngle.w;


	float3 N = normalize(viewVector);
	float3 D = g_volLightParams.m_lightDirAndConAngle.xyz;
	float3 O = g_volLightParams.m_lightPosAndRadius.xyz;
	float sqrCosHalfConeAngle = cosHalfConeAngle * cosHalfConeAngle;

	float depthBufferZ = txDepthBuffer.Sample(g_sSamplerPoint, input.Tex).x;
	float viewSpaceDist = viewDist / (depthBufferZ * g_volLightParams.m_linearDepthParams.x + g_volLightParams.m_linearDepthParams.y);

	float fNdotD = dot(N, D);
	float fOdotD = dot(O, D);
	float fNdotO = dot(N, O);
	float fOdotO = dot(O, O);

	// ray intersect with cone shape.
	float A = fNdotD * fNdotD - sqrCosHalfConeAngle;
	float B = fNdotD * fOdotD - sqrCosHalfConeAngle * fNdotO;
	float C = fOdotD * fOdotD - sqrCosHalfConeAngle * fOdotO;

	float fBdivA = B / A;
	float fCdivA = C / A;

	float fFinalT0 = 0;
	float fFinalT1 = 0;

	float delta = fBdivA * fBdivA - fCdivA;
	if (delta > 0)
	{
		float sqrtDelta = sqrt(delta);
		float t0 = fBdivA - sqrtDelta;
		float t1 = fBdivA + sqrtDelta;

		float tt0 = t0 * fNdotD - fOdotD;
		float tt1 = t1 * fNdotD - fOdotD;

		if (fNdotD > 0)
		{
			if (tt0 < 0)
				t0 = 10000.0f;
			if (tt1 < 0)
				t1 = 10000.0f;
		}
		else
		{
			if (tt0 < 0)
				t0 = 0.0f;
			if (tt1 < 0)
				t1 = 0.0f;
		}

		fFinalT0 = min(t0, t1);
		fFinalT1 = max(t0, t1);

		B = fNdotO;
		C = fOdotO - radius * radius;

		delta = B * B - C;
		if (delta > 0)
		{
			sqrtDelta = sqrt(delta);
			float t2 = B - sqrtDelta;
			float t3 = B + sqrtDelta;

			fFinalT0 = max(fFinalT0, t2);
			fFinalT1 = min(fFinalT1, t3);
		}

		fFinalT0 = clamp(fFinalT0, 0, viewSpaceDist);
		fFinalT1 = clamp(fFinalT1, 0, viewSpaceDist);
	}

	if (fFinalT1 > fFinalT0)
	{
		float3 dirToLightSrc0 = fFinalT0 * N - O;
		float3 dirToLightSrc1 = fFinalT1 * N - O;
		float2 cosSinRefAngle = normalize(float2(dot(dirToLightSrc1, g_volLightParams.m_sampleDirVector.xyz),  dot(dirToLightSrc1, g_volLightParams.m_sampleRightVector.xyz)));
		float angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));
		if (cosSinRefAngle.y < 0)
			angle = 2.0f * kPi - angle;

		if (angle < g_volLightParams.m_sampleAngleInfo.z)
			angle += 2.0f * kPi;

		int sampleArrayIndex = int(saturate(angle * g_volLightParams.m_sampleAngleInfo.x + g_volLightParams.m_sampleAngleInfo.y) * g_volLightParams.m_sampleAngleInfo.w);

		float3 toLight = normalize(O);

		float refCosAngle = dot(toLight, N);

		float nearSampleAngle = acos(dot(toLight, normalize(dirToLightSrc0)));
		float farSampleAngle = acos(dot(toLight, normalize(dirToLightSrc1)));

		uint2 angleScaleOffset = tSampleInfoBuffer.Load2(sampleArrayIndex * 16);
		float2 fAngleScaleOffset = float2(asfloat(angleScaleOffset.x), asfloat(angleScaleOffset.y));

		float startIdx = (nearSampleAngle * fAngleScaleOffset.y - fAngleScaleOffset.x) * 256.0f;
		float endIdx = (farSampleAngle * fAngleScaleOffset.y - fAngleScaleOffset.x) * 256.0f;


		float sumVisible = 0;
		for (int i = int(min(startIdx, endIdx) / 4); i < int(max(startIdx, endIdx) / 4); i++)
		{
			float4 sampleVal = txSampleListBuffer.Load(int3(i, sampleArrayIndex, 0));
			bool4 cmpResult = refCosAngle > sampleVal;
			sumVisible += dot((int4)cmpResult, 1);
		}

		finalColor = max(GetSpotVolIntegrate(fFinalT0, fFinalT1, cosHalfConeAngle, radius, fNdotD, fNdotO, fOdotD, fOdotO), 0) * 0.1f * (sumVisible / 256.0f);
	}

	return finalColor;
}

float4 PS_VolumetricSpotLightByShape(PS_Pos input) : SV_Target
{
	float2 tex = input.Pos.xy * g_volLightParams.m_screenSize.zw;

	float4 finalColor = float4(0, 0, 0, 0);
	float3 viewVector = float3(tex * g_volLightParams.m_viewSpaceXyParams.xy + g_volLightParams.m_viewSpaceXyParams.zw, 1.0);
	float viewDist = length(viewVector);
	float radius = g_volLightParams.m_lightPosAndRadius.w;
	float cosHalfConeAngle = g_volLightParams.m_lightDirAndConAngle.w;


	float3 N = normalize(viewVector);
	float3 D = g_volLightParams.m_lightDirAndConAngle.xyz;
	float3 O = g_volLightParams.m_lightPosAndRadius.xyz;
	float sqrCosHalfConeAngle = cosHalfConeAngle * cosHalfConeAngle;

	float depthBufferZ = txDepthBuffer.Sample(g_sSamplerPoint, tex).x;
	float viewSpaceDist = viewDist / (depthBufferZ * g_volLightParams.m_linearDepthParams.x + g_volLightParams.m_linearDepthParams.y);

	float fNdotD = dot(N, D);
	float fOdotD = dot(O, D);
	float fNdotO = dot(N, O);
	float fOdotO = dot(O, O);

	// ray intersect with cone shape.
	float A = fNdotD * fNdotD - sqrCosHalfConeAngle;
	float B = fNdotD * fOdotD - sqrCosHalfConeAngle * fNdotO;
	float C = fOdotD * fOdotD - sqrCosHalfConeAngle * fOdotO;

	float fBdivA = B / A;
	float fCdivA = C / A;

	float fFinalT0 = 0;
	float fFinalT1 = 0;

	float delta = fBdivA * fBdivA - fCdivA;
	if (delta > 0)
	{
		float sqrtDelta = sqrt(delta);
		float t0 = fBdivA - sqrtDelta;
		float t1 = fBdivA + sqrtDelta;

		float tt0 = t0 * fNdotD - fOdotD;
		float tt1 = t1 * fNdotD - fOdotD;

		if (fNdotD > 0)
		{
			if (tt0 < 0)
				t0 = 10000.0f;
			if (tt1 < 0)
				t1 = 10000.0f;
		}
		else
		{
			if (tt0 < 0)
				t0 = 0.0f;
			if (tt1 < 0)
				t1 = 0.0f;
		}

		fFinalT0 = min(t0, t1);
		fFinalT1 = max(t0, t1);

		B = fNdotO;
		C = fOdotO - radius * radius;

		delta = B * B - C;
		if (delta > 0)
		{
			sqrtDelta = sqrt(delta);
			float t2 = B - sqrtDelta;
			float t3 = B + sqrtDelta;

			fFinalT0 = max(fFinalT0, t2);
			fFinalT1 = min(fFinalT1, t3);
		}

		fFinalT0 = clamp(fFinalT0, 0, viewSpaceDist);
		fFinalT1 = clamp(fFinalT1, 0, viewSpaceDist);
	}

	if (fFinalT1 > fFinalT0)
	{
		float3 dirToLightSrc0 = fFinalT0 * N - O;
		float3 dirToLightSrc1 = fFinalT1 * N - O;
		float2 cosSinRefAngle = normalize(float2(dot(dirToLightSrc1, g_volLightParams.m_sampleDirVector.xyz),  dot(dirToLightSrc1, g_volLightParams.m_sampleRightVector.xyz)));
		float angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));
		if (cosSinRefAngle.y < 0)
			angle = 2.0f * kPi - angle;

		if (angle < g_volLightParams.m_sampleAngleInfo.z)
			angle += 2.0f * kPi;

		int sampleArrayIndex = int(saturate(angle * g_volLightParams.m_sampleAngleInfo.x + g_volLightParams.m_sampleAngleInfo.y) * g_volLightParams.m_sampleAngleInfo.w);

		float3 toLight = normalize(O);

		float refCosAngle = dot(toLight, N);

		float nearSampleAngle = acos(dot(toLight, normalize(dirToLightSrc0)));
		float farSampleAngle = acos(dot(toLight, normalize(dirToLightSrc1)));

		uint2 angleScaleOffset = tSampleInfoBuffer.Load2(sampleArrayIndex * 16);
		float2 fAngleScaleOffset = float2(asfloat(angleScaleOffset.x), asfloat(angleScaleOffset.y));

		float startIdx = (nearSampleAngle * fAngleScaleOffset.y - fAngleScaleOffset.x) * 256.0f;
		float endIdx = (farSampleAngle * fAngleScaleOffset.y - fAngleScaleOffset.x) * 256.0f;


		float sumVisible = 0;
		for (int i = int(min(startIdx, endIdx) / 4); i < int(max(startIdx, endIdx) / 4); i++)
		{
			float4 sampleVal = txSampleListBuffer.Load(int3(i, sampleArrayIndex, 0));
			bool4 cmpResult = refCosAngle > sampleVal;
			sumVisible += dot((int4)cmpResult, 1);
		}

		finalColor = max(GetSpotVolIntegrate(fFinalT0, fFinalT1, cosHalfConeAngle, radius, fNdotD, fNdotO, fOdotD, fOdotO), 0) * 0.1f * (sumVisible / 256.0f);
	}

	return finalColor;
}

struct VolSpotNoShadowPsTextures
{
	Texture2D<float>		src_depth;
};

struct VolSpotNoShadowPsSamplers
{
	SamplerState			samplerPoint;
};

struct VolSpotNoShadowPsSrt
{
	VolumetricLightConst		*consts;
	VolSpotNoShadowPsTextures	*texs;
	VolSpotNoShadowPsSamplers	*smpls;
};

float4 PS_VolumetricSpotLightNoShadowDirectByShapeOldStyle (PS_Pos input, VolSpotNoShadowPsSrt srt : S_SRT_DATA) : SV_Target
{
	float2 tex = input.Pos.xy * srt.consts->m_screenSize.zw;

	float4 finalColor = float4(0, 0, 0, 0);
	float3 viewVector = float3(tex * srt.consts->m_viewSpaceXyParams.xy + srt.consts->m_viewSpaceXyParams.zw, 1.0);
	float viewDist = length(viewVector);
	float radius = srt.consts->m_lightPosAndRadius.w;
	float cosHalfConeAngle = srt.consts->m_lightDirAndConAngle.w;

	float3 N = normalize(viewVector);
	float3 D = srt.consts->m_lightDirAndConAngle.xyz;
	float3 O = srt.consts->m_lightPosAndRadius.xyz;
	float sqrCosHalfConeAngle = cosHalfConeAngle * cosHalfConeAngle;

	float depthBufferZ = srt.texs->src_depth.Sample(srt.smpls->samplerPoint, tex).x;
	float viewSpaceZ = 1.0f / (depthBufferZ * srt.consts->m_linearDepthParams.x + srt.consts->m_linearDepthParams.y);
	float viewSpaceDist = viewDist * viewSpaceZ;

	float fNdotD = dot(N, D);
	float fOdotD = dot(O, D);
	float fNdotO = dot(N, O);
	float fOdotO = dot(O, O);

	// ray intersect with cone shape.
	float A = fNdotD * fNdotD - sqrCosHalfConeAngle;
	float B = fNdotD * fOdotD - sqrCosHalfConeAngle * fNdotO;
	float C = fOdotD * fOdotD - sqrCosHalfConeAngle * fOdotO;

	float fBdivA = B / A;
	float fCdivA = C / A;

	float fFinalT0 = 0;
	float fFinalT1 = 0;

	float delta = fBdivA * fBdivA - fCdivA;
	if (delta > 0)
	{
		float sqrtDelta = sqrt(delta);
		float t0 = fBdivA - sqrtDelta;
		float t1 = fBdivA + sqrtDelta;

		float tt0 = t0 * fNdotD - fOdotD;
		float tt1 = t1 * fNdotD - fOdotD;

		if (fNdotD > 0)
		{
			if (tt0 < 0)
				t0 = 10000.0f;
			if (tt1 < 0)
				t1 = 10000.0f;
		}
		else
		{
			if (tt0 < 0)
				t0 = 0.0f;
			if (tt1 < 0)
				t1 = 0.0f;
		}

		fFinalT0 = min(t0, t1);
		fFinalT1 = max(t0, t1);

		B = fNdotO;
		C = fOdotO - radius * radius;

		delta = B * B - C;
		if (delta > 0)
		{
			sqrtDelta = sqrt(delta);
			float t2 = B - sqrtDelta;
			float t3 = B + sqrtDelta;

			fFinalT0 = max(fFinalT0, t2);
			fFinalT1 = min(fFinalT1, t3);
		}

		fFinalT0 = clamp(fFinalT0, 0, viewSpaceDist);
		fFinalT1 = clamp(fFinalT1, 0, viewSpaceDist);
	}

	if (fFinalT1 > fFinalT0)
	{
		float volValue = max(GetSpotVolIntegrateOld(fFinalT0, fFinalT1, cosHalfConeAngle, radius, fNdotD, fNdotO, fOdotD, fOdotO), 0);

		finalColor = float4(volValue * srt.consts->m_volColor.xyz, 0.0f);
	}

	return finalColor;
}

struct VolSpotLightPsTextures
{
	Texture2D<float>		src_depth;
	Texture2D<float4>		sample_info;
	Texture2D<float4>		visibility_buf;
};

struct VolSpotLightPsSamplers
{
	SamplerState			samplerPoint;
};

struct VolSpotLightPsSrt
{
	VolumetricLightConst	*consts;
	VolSpotLightPsTextures	*texs;
	VolSpotLightPsSamplers	*smpls;
};

float4 PS_VolumetricSpotLightDirectByShapeOldStyle (PS_Pos input, VolSpotLightPsSrt srt : S_SRT_DATA) : SV_Target
{
	float2 tex = input.Pos.xy * srt.consts->m_screenSize.zw;

	float4 finalColor = float4(0, 0, 0, 0);
	float3 viewVector = float3(tex * srt.consts->m_viewSpaceXyParams.xy + srt.consts->m_viewSpaceXyParams.zw, 1.0);
	float viewDist = length(viewVector);
	float radius = srt.consts->m_lightPosAndRadius.w;
	float cosHalfConeAngle = srt.consts->m_lightDirAndConAngle.w;

	float3 N = normalize(viewVector);
	float3 D = srt.consts->m_lightDirAndConAngle.xyz;
	float3 O = srt.consts->m_lightPosAndRadius.xyz;
	float sqrCosHalfConeAngle = cosHalfConeAngle * cosHalfConeAngle;

	float depthBufferZ = srt.texs->src_depth.Sample(srt.smpls->samplerPoint, tex).x;
	float viewSpaceZ = 1.0f / (depthBufferZ * srt.consts->m_linearDepthParams.x + srt.consts->m_linearDepthParams.y);
	float viewSpaceDist = viewDist * viewSpaceZ;

	float fNdotD = dot(N, D);
	float fOdotD = dot(O, D);
	float fNdotO = dot(N, O);
	float fOdotO = dot(O, O);

	// ray intersect with cone shape.
	float A = fNdotD * fNdotD - sqrCosHalfConeAngle;
	float B = fNdotD * fOdotD - sqrCosHalfConeAngle * fNdotO;
	float C = fOdotD * fOdotD - sqrCosHalfConeAngle * fOdotO;

	float fBdivA = B / A;
	float fCdivA = C / A;

	float fFinalT0 = 0;
	float fFinalT1 = 0;

	float delta = fBdivA * fBdivA - fCdivA;
	if (delta > 0)
	{
		float sqrtDelta = sqrt(delta);
		float t0 = fBdivA - sqrtDelta;
		float t1 = fBdivA + sqrtDelta;

		float tt0 = t0 * fNdotD - fOdotD;
		float tt1 = t1 * fNdotD - fOdotD;

		if (fNdotD > 0)
		{
			if (tt0 < 0)
				t0 = 10000.0f;
			if (tt1 < 0)
				t1 = 10000.0f;
		}
		else
		{
			if (tt0 < 0)
				t0 = 0.0f;
			if (tt1 < 0)
				t1 = 0.0f;
		}

		fFinalT0 = min(t0, t1);
		fFinalT1 = max(t0, t1);

		B = fNdotO;
		C = fOdotO - radius * radius;

		delta = B * B - C;
		if (delta > 0)
		{
			sqrtDelta = sqrt(delta);
			float t2 = B - sqrtDelta;
			float t3 = B + sqrtDelta;

			fFinalT0 = max(fFinalT0, t2);
			fFinalT1 = min(fFinalT1, t3);
		}

		fFinalT0 = clamp(fFinalT0, 0, viewSpaceDist);
		fFinalT1 = clamp(fFinalT1, 0, viewSpaceDist);
	}

	if (fFinalT1 > fFinalT0)
	{
		float2 cosSinRefAngle = normalize(float2(dot(N, srt.consts->m_sampleDirVector.xyz),  dot(N, srt.consts->m_sampleRightVector.xyz)));
		float angle = acos(clamp(cosSinRefAngle.y > 0 ? cosSinRefAngle.x : -cosSinRefAngle.x, -1.0f, 1.0f));

		float sampleArrayIndex = saturate(angle * srt.consts->m_sampleAngleInfo.x + srt.consts->m_sampleAngleInfo.y) * srt.consts->m_sampleAngleInfo.w;

		int sampleArrayIndexLow = int(min(sampleArrayIndex, srt.consts->m_sampleAngleInfo.w-1));
		int sampleArrayIndexHi = int(min(sampleArrayIndex + 1.0f, srt.consts->m_sampleAngleInfo.w-1));

		float4 viewRayDir = srt.texs->sample_info.Load(int3(sampleArrayIndexLow, 2, 0));
		float4 viewRayRight = srt.texs->sample_info.Load(int3(sampleArrayIndexLow, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexLow = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		viewRayDir = srt.texs->sample_info.Load(int3(sampleArrayIndexHi, 2, 0));
		viewRayRight = srt.texs->sample_info.Load(int3(sampleArrayIndexHi, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexHi = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		float2 visibility00 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexLow, 511.0), sampleArrayIndexLow, 0)).xy;
		float2 visibility01 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexLow+1.0f, 511.0), sampleArrayIndexLow, 0)).xy;
		float2 visibility10 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexHi, 511.0), sampleArrayIndexHi, 0)).xy;
		float2 visibility11 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexHi+1.0f, 511.0), sampleArrayIndexHi, 0)).xy;

		float diffZ00 = (viewSpaceZ - visibility00.y);
		float diffZ01 = (viewSpaceZ - visibility01.y);
		float diffZ10 = (viewSpaceZ - visibility10.y);
		float diffZ11 = (viewSpaceZ - visibility11.y);

		float y = frac(sampleArrayIndex);
		float x0 = frac(lookUpIndexLow);
		float x1 = frac(lookUpIndexHi);

		float2 offset00 = float2(x0, y);
		float2 offset01 = float2(1.0f - x0, y);
		float2 offset10 = float2(x1, 1.0f - y);
		float2 offset11 = float2(1.0f - x1, 1.0f - y);

		float weight00 = 1.0f / ((1.0f + diffZ00 * diffZ00) * (1.0f + dot(offset00, offset00)));
		float weight01 = 1.0f / ((1.0f + diffZ01 * diffZ01) * (1.0f + dot(offset01, offset01)));
		float weight10 = 1.0f / ((1.0f + diffZ10 * diffZ10) * (1.0f + dot(offset10, offset10)));
		float weight11 = 1.0f / ((1.0f + diffZ11 * diffZ11) * (1.0f + dot(offset11, offset11)));

		float visibility = (visibility00.x * weight00 + visibility01.x * weight01 + visibility10.x * weight10 + visibility11.x * weight11) / (weight00 + weight01 + weight10 + weight11);
		visibility *= visibility;
		float volValue = max(GetSpotVolIntegrateOld(fFinalT0, fFinalT1, cosHalfConeAngle, radius, fNdotD, fNdotO, fOdotD, fOdotO), 0);

		finalColor = float4(volValue * visibility * srt.consts->m_volColor.xyz, 0.0f);
	}

	return finalColor;
}

float4 PS_VolumetricSpotLightWithMaskDirectByShapeOldStyle (PS_Pos input, VolSpotLightPsSrt srt : S_SRT_DATA) : SV_Target
{
	float2 tex = input.Pos.xy * srt.consts->m_screenSize.zw;

	float4 finalColor = float4(0, 0, 0, 0);
	float3 viewVector = float3(tex * srt.consts->m_viewSpaceXyParams.xy + srt.consts->m_viewSpaceXyParams.zw, 1.0);
	float viewDist = length(viewVector);
	float radius = srt.consts->m_lightPosAndRadius.w;
	float cosHalfConeAngle = srt.consts->m_lightDirAndConAngle.w;

	float3 N = normalize(viewVector);
	float3 D = srt.consts->m_lightDirAndConAngle.xyz;
	float3 O = srt.consts->m_lightPosAndRadius.xyz;
	float sqrCosHalfConeAngle = cosHalfConeAngle * cosHalfConeAngle;

	float depthBufferZ = srt.texs->src_depth.Sample(srt.smpls->samplerPoint, tex).x;
	float viewSpaceZ = 1.0f / (depthBufferZ * srt.consts->m_linearDepthParams.x + srt.consts->m_linearDepthParams.y);
	float viewSpaceDist = viewDist * viewSpaceZ;

	float fNdotD = dot(N, D);
	float fOdotD = dot(O, D);
	float fNdotO = dot(N, O);
	float fOdotO = dot(O, O);

	// ray intersect with cone shape.
	float A = fNdotD * fNdotD - sqrCosHalfConeAngle;
	float B = fNdotD * fOdotD - sqrCosHalfConeAngle * fNdotO;
	float C = fOdotD * fOdotD - sqrCosHalfConeAngle * fOdotO;

	float fBdivA = B / A;
	float fCdivA = C / A;

	float fFinalT0 = 0;
	float fFinalT1 = 0;

	float delta = fBdivA * fBdivA - fCdivA;
	if (delta > 0)
	{
		float sqrtDelta = sqrt(delta);
		float t0 = fBdivA - sqrtDelta;
		float t1 = fBdivA + sqrtDelta;

		float tt0 = t0 * fNdotD - fOdotD;
		float tt1 = t1 * fNdotD - fOdotD;

		if (fNdotD > 0)
		{
			if (tt0 < 0)
				t0 = 10000.0f;
			if (tt1 < 0)
				t1 = 10000.0f;
		}
		else
		{
			if (tt0 < 0)
				t0 = 0.0f;
			if (tt1 < 0)
				t1 = 0.0f;
		}

		fFinalT0 = min(t0, t1);
		fFinalT1 = max(t0, t1);

		B = fNdotO;
		C = fOdotO - radius * radius;

		delta = B * B - C;
		if (delta > 0)
		{
			sqrtDelta = sqrt(delta);
			float t2 = B - sqrtDelta;
			float t3 = B + sqrtDelta;

			fFinalT0 = max(fFinalT0, t2);
			fFinalT1 = min(fFinalT1, t3);
		}

		fFinalT0 = clamp(fFinalT0, 0, viewSpaceDist);
		fFinalT1 = clamp(fFinalT1, 0, viewSpaceDist);
	}

	if (fFinalT1 > fFinalT0)
	{
		float2 cosSinRefAngle = normalize(float2(dot(N, srt.consts->m_sampleDirVector.xyz),  dot(N, srt.consts->m_sampleRightVector.xyz)));
		float angle = acos(clamp(cosSinRefAngle.y > 0.0f ? cosSinRefAngle.x : -cosSinRefAngle.x, -1.0f, 1.0f));

		float sampleArrayIndex = saturate(angle * srt.consts->m_sampleAngleInfo.x + srt.consts->m_sampleAngleInfo.y) * srt.consts->m_sampleAngleInfo.w;

		int sampleArrayIndexLow = int(min(sampleArrayIndex, srt.consts->m_sampleAngleInfo.w-1));
		int sampleArrayIndexHi = int(min(sampleArrayIndex + 1.0f, srt.consts->m_sampleAngleInfo.w-1));

		float4 viewRayDir = srt.texs->sample_info.Load(int3(sampleArrayIndexLow, 2, 0));
		float4 viewRayRight = srt.texs->sample_info.Load(int3(sampleArrayIndexLow, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexLow = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		viewRayDir = srt.texs->sample_info.Load(int3(sampleArrayIndexHi, 2, 0));
		viewRayRight = srt.texs->sample_info.Load(int3(sampleArrayIndexHi, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexHi = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		float4 visibility00 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexLow, 511.0), sampleArrayIndexLow, 0));
		float4 visibility01 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexLow+1.0f, 511.0), sampleArrayIndexLow, 0));
		float4 visibility10 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexHi, 511.0), sampleArrayIndexHi, 0));
		float4 visibility11 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexHi+1.0f, 511.0), sampleArrayIndexHi, 0));

		float diffZ00 = (viewSpaceZ - visibility00.w);
		float diffZ01 = (viewSpaceZ - visibility01.w);
		float diffZ10 = (viewSpaceZ - visibility10.w);
		float diffZ11 = (viewSpaceZ - visibility11.w);

		float y = frac(sampleArrayIndex);
		float x0 = frac(lookUpIndexLow);
		float x1 = frac(lookUpIndexHi);

		float2 offset00 = float2(x0, y);
		float2 offset01 = float2(1.0f - x0, y);
		float2 offset10 = float2(x1, 1.0f - y);
		float2 offset11 = float2(1.0f - x1, 1.0f - y);

		float weight00 = 1.0f / ((1.0f + diffZ00 * diffZ00) * (1.0f + dot(offset00, offset00)));
		float weight01 = 1.0f / ((1.0f + diffZ01 * diffZ01) * (1.0f + dot(offset01, offset01)));
		float weight10 = 1.0f / ((1.0f + diffZ10 * diffZ10) * (1.0f + dot(offset10, offset10)));
		float weight11 = 1.0f / ((1.0f + diffZ11 * diffZ11) * (1.0f + dot(offset11, offset11)));

		float3 visibility = (visibility00.xyz * weight00 + visibility01.xyz * weight01 + visibility10.xyz * weight10 + visibility11.xyz * weight11) / (weight00 + weight01 + weight10 + weight11);

		float intensity = dot(visibility, s_luminanceVector.xyz);
		float intensitySqr = intensity * intensity;
		visibility *= (intensitySqr * intensity);

		float volValue = max(GetSpotVolIntegrateOld(fFinalT0, fFinalT1, cosHalfConeAngle, radius, fNdotD, fNdotO, fOdotD, fOdotO), 0);

		finalColor = float4(volValue * visibility * srt.consts->m_volColor.xyz, 0.0f) ;
	}

	return finalColor;
}

float4 PS_VolumetricSpotLightDirectByShape (PS_Pos input, VolSpotLightPsSrt srt : S_SRT_DATA) : SV_Target
{
	float2 tex = input.Pos.xy * srt.consts->m_screenSize.zw;

	float4 finalColor = float4(0, 0, 0, 0);
	float3 viewVector = float3(tex * srt.consts->m_viewSpaceXyParams.xy + srt.consts->m_viewSpaceXyParams.zw, 1.0);
	float viewDist = length(viewVector);
	float radius = srt.consts->m_lightPosAndRadius.w;
	float cosHalfConeAngle = srt.consts->m_lightDirAndConAngle.w;

	float3 N = normalize(viewVector);
	float3 D = srt.consts->m_lightDirAndConAngle.xyz;
	float3 O = srt.consts->m_lightPosAndRadius.xyz;
	float sqrCosHalfConeAngle = cosHalfConeAngle * cosHalfConeAngle;

	float depthBufferZ = srt.texs->src_depth.Sample(srt.smpls->samplerPoint, tex).x;
	float viewSpaceZ = 1.0f / (depthBufferZ * srt.consts->m_linearDepthParams.x + srt.consts->m_linearDepthParams.y);
	float viewSpaceDist = viewDist * viewSpaceZ;

	float fNdotD = dot(N, D);
	float fOdotD = dot(O, D);
	float fNdotO = dot(N, O);
	float fOdotO = dot(O, O);

	// ray intersect with cone shape.
	float A = fNdotD * fNdotD - sqrCosHalfConeAngle;
	float B = fNdotD * fOdotD - sqrCosHalfConeAngle * fNdotO;
	float C = fOdotD * fOdotD - sqrCosHalfConeAngle * fOdotO;

	float fBdivA = B / A;
	float fCdivA = C / A;

	float fFinalT0 = 0;
	float fFinalT1 = 0;

	float delta = fBdivA * fBdivA - fCdivA;
	if (delta > 0)
	{
		float sqrtDelta = sqrt(delta);
		float t0 = fBdivA - sqrtDelta;
		float t1 = fBdivA + sqrtDelta;

		float tt0 = t0 * fNdotD - fOdotD;
		float tt1 = t1 * fNdotD - fOdotD;

		if (fNdotD > 0)
		{
			if (tt0 < 0)
				t0 = 10000.0f;
			if (tt1 < 0)
				t1 = 10000.0f;
		}
		else
		{
			if (tt0 < 0)
				t0 = 0.0f;
			if (tt1 < 0)
				t1 = 0.0f;
		}

		fFinalT0 = min(t0, t1);
		fFinalT1 = max(t0, t1);

		B = fNdotO;
		C = fOdotO - radius * radius;

		delta = B * B - C;
		if (delta > 0)
		{
			sqrtDelta = sqrt(delta);
			float t2 = B - sqrtDelta;
			float t3 = B + sqrtDelta;

			fFinalT0 = max(fFinalT0, t2);
			fFinalT1 = min(fFinalT1, t3);
		}

		fFinalT0 = clamp(fFinalT0, 0, viewSpaceDist);
		fFinalT1 = clamp(fFinalT1, 0, viewSpaceDist);
	}

	if (fFinalT1 > fFinalT0)
	{
		float2 cosSinRefAngle = normalize(float2(dot(N, srt.consts->m_sampleDirVector.xyz),  dot(N, srt.consts->m_sampleRightVector.xyz)));
		float angle = acos(clamp(cosSinRefAngle.y > 0 ? cosSinRefAngle.x : -cosSinRefAngle.x, -1.0f, 1.0f));

		float sampleArrayIndex = saturate(angle * srt.consts->m_sampleAngleInfo.x + srt.consts->m_sampleAngleInfo.y) * srt.consts->m_sampleAngleInfo.w;

		int sampleArrayIndexLow = int(min(sampleArrayIndex, srt.consts->m_sampleAngleInfo.w-1));
		int sampleArrayIndexHi = int(min(sampleArrayIndex + 1.0f, srt.consts->m_sampleAngleInfo.w-1));

		float4 viewRayDir = srt.texs->sample_info.Load(int3(sampleArrayIndexLow, 2, 0));
		float4 viewRayRight = srt.texs->sample_info.Load(int3(sampleArrayIndexLow, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexLow = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		viewRayDir = srt.texs->sample_info.Load(int3(sampleArrayIndexHi, 2, 0));
		viewRayRight = srt.texs->sample_info.Load(int3(sampleArrayIndexHi, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexHi = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		float2 visibility00 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexLow, 511.0), sampleArrayIndexLow, 0)).xy;
		float2 visibility01 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexLow+1.0f, 511.0), sampleArrayIndexLow, 0)).xy;
		float2 visibility10 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexHi, 511.0), sampleArrayIndexHi, 0)).xy;
		float2 visibility11 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexHi+1.0f, 511.0), sampleArrayIndexHi, 0)).xy;

		float diffZ00 = (viewSpaceZ - visibility00.y);
		float diffZ01 = (viewSpaceZ - visibility01.y);
		float diffZ10 = (viewSpaceZ - visibility10.y);
		float diffZ11 = (viewSpaceZ - visibility11.y);

		float y = frac(sampleArrayIndex);
		float x0 = frac(lookUpIndexLow);
		float x1 = frac(lookUpIndexHi);

		float2 offset00 = float2(x0, y);
		float2 offset01 = float2(1.0f - x0, y);
		float2 offset10 = float2(x1, 1.0f - y);
		float2 offset11 = float2(1.0f - x1, 1.0f - y);

		float weight00 = 1.0f / ((1.0f + diffZ00 * diffZ00) * (1.0f + dot(offset00, offset00)));
		float weight01 = 1.0f / ((1.0f + diffZ01 * diffZ01) * (1.0f + dot(offset01, offset01)));
		float weight10 = 1.0f / ((1.0f + diffZ10 * diffZ10) * (1.0f + dot(offset10, offset10)));
		float weight11 = 1.0f / ((1.0f + diffZ11 * diffZ11) * (1.0f + dot(offset11, offset11)));

		float visibility = (visibility00.x * weight00 + visibility01.x * weight01 + visibility10.x * weight10 + visibility11.x * weight11) / (weight00 + weight01 + weight10 + weight11);
		float volValue = max(GetSpotVolIntegrate(fFinalT0, fFinalT1, cosHalfConeAngle, radius, fNdotD, fNdotO, fOdotD, fOdotO), 0);

		finalColor = float4(volValue * visibility * srt.consts->m_volColor.xyz, 0.0f);
	}

	return finalColor;
}

float4 PS_VolumetricSpotLightWithMaskDirectByShape (PS_Pos input, VolSpotLightPsSrt srt : S_SRT_DATA) : SV_Target
{
	float2 tex = input.Pos.xy * srt.consts->m_screenSize.zw;

	float4 finalColor = float4(0, 0, 0, 0);
	float3 viewVector = float3(tex * srt.consts->m_viewSpaceXyParams.xy + srt.consts->m_viewSpaceXyParams.zw, 1.0);
	float viewDist = length(viewVector);
	float radius = srt.consts->m_lightPosAndRadius.w;
	float cosHalfConeAngle = srt.consts->m_lightDirAndConAngle.w;

	float3 N = normalize(viewVector);
	float3 D = srt.consts->m_lightDirAndConAngle.xyz;
	float3 O = srt.consts->m_lightPosAndRadius.xyz;
	float sqrCosHalfConeAngle = cosHalfConeAngle * cosHalfConeAngle;

	float depthBufferZ = srt.texs->src_depth.Sample(srt.smpls->samplerPoint, tex).x;
	float viewSpaceZ = 1.0f / (depthBufferZ * srt.consts->m_linearDepthParams.x + srt.consts->m_linearDepthParams.y);
	float viewSpaceDist = viewDist * viewSpaceZ;

	float fNdotD = dot(N, D);
	float fOdotD = dot(O, D);
	float fNdotO = dot(N, O);
	float fOdotO = dot(O, O);

	// ray intersect with cone shape.
	float A = fNdotD * fNdotD - sqrCosHalfConeAngle;
	float B = fNdotD * fOdotD - sqrCosHalfConeAngle * fNdotO;
	float C = fOdotD * fOdotD - sqrCosHalfConeAngle * fOdotO;

	float fBdivA = B / A;
	float fCdivA = C / A;

	float fFinalT0 = 0;
	float fFinalT1 = 0;

	float delta = fBdivA * fBdivA - fCdivA;
	if (delta > 0)
	{
		float sqrtDelta = sqrt(delta);
		float t0 = fBdivA - sqrtDelta;
		float t1 = fBdivA + sqrtDelta;

		float tt0 = t0 * fNdotD - fOdotD;
		float tt1 = t1 * fNdotD - fOdotD;

		if (fNdotD > 0)
		{
			if (tt0 < 0)
				t0 = 10000.0f;
			if (tt1 < 0)
				t1 = 10000.0f;
		}
		else
		{
			if (tt0 < 0)
				t0 = 0.0f;
			if (tt1 < 0)
				t1 = 0.0f;
		}

		fFinalT0 = min(t0, t1);
		fFinalT1 = max(t0, t1);

		B = fNdotO;
		C = fOdotO - radius * radius;

		delta = B * B - C;
		if (delta > 0)
		{
			sqrtDelta = sqrt(delta);
			float t2 = B - sqrtDelta;
			float t3 = B + sqrtDelta;

			fFinalT0 = max(fFinalT0, t2);
			fFinalT1 = min(fFinalT1, t3);
		}

		fFinalT0 = clamp(fFinalT0, 0, viewSpaceDist);
		fFinalT1 = clamp(fFinalT1, 0, viewSpaceDist);
	}

	if (fFinalT1 > fFinalT0)
	{
		float2 cosSinRefAngle = normalize(float2(dot(N, srt.consts->m_sampleDirVector.xyz),  dot(N, srt.consts->m_sampleRightVector.xyz)));
		float angle = acos(clamp(cosSinRefAngle.y > 0.0f ? cosSinRefAngle.x : -cosSinRefAngle.x, -1.0f, 1.0f));

		float sampleArrayIndex = saturate(angle * srt.consts->m_sampleAngleInfo.x + srt.consts->m_sampleAngleInfo.y) * srt.consts->m_sampleAngleInfo.w;

		int sampleArrayIndexLow = int(min(sampleArrayIndex, srt.consts->m_sampleAngleInfo.w-1));
		int sampleArrayIndexHi = int(min(sampleArrayIndex + 1.0f, srt.consts->m_sampleAngleInfo.w-1));

		float4 viewRayDir = srt.texs->sample_info.Load(int3(sampleArrayIndexLow, 2, 0));
		float4 viewRayRight = srt.texs->sample_info.Load(int3(sampleArrayIndexLow, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexLow = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		viewRayDir = srt.texs->sample_info.Load(int3(sampleArrayIndexHi, 2, 0));
		viewRayRight = srt.texs->sample_info.Load(int3(sampleArrayIndexHi, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexHi = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		float4 visibility00 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexLow, 511.0), sampleArrayIndexLow, 0));
		float4 visibility01 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexLow+1.0f, 511.0), sampleArrayIndexLow, 0));
		float4 visibility10 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexHi, 511.0), sampleArrayIndexHi, 0));
		float4 visibility11 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexHi+1.0f, 511.0), sampleArrayIndexHi, 0));

		float diffZ00 = (viewSpaceZ - visibility00.w);
		float diffZ01 = (viewSpaceZ - visibility01.w);
		float diffZ10 = (viewSpaceZ - visibility10.w);
		float diffZ11 = (viewSpaceZ - visibility11.w);

		float y = frac(sampleArrayIndex);
		float x0 = frac(lookUpIndexLow);
		float x1 = frac(lookUpIndexHi);

		float2 offset00 = float2(x0, y);
		float2 offset01 = float2(1.0f - x0, y);
		float2 offset10 = float2(x1, 1.0f - y);
		float2 offset11 = float2(1.0f - x1, 1.0f - y);

		float weight00 = 1.0f / ((1.0f + diffZ00 * diffZ00) * (1.0f + dot(offset00, offset00)));
		float weight01 = 1.0f / ((1.0f + diffZ01 * diffZ01) * (1.0f + dot(offset01, offset01)));
		float weight10 = 1.0f / ((1.0f + diffZ10 * diffZ10) * (1.0f + dot(offset10, offset10)));
		float weight11 = 1.0f / ((1.0f + diffZ11 * diffZ11) * (1.0f + dot(offset11, offset11)));

		float3 visibility = (visibility00.xyz * weight00 + visibility01.xyz * weight01 + visibility10.xyz * weight10 + visibility11.xyz * weight11) / (weight00 + weight01 + weight10 + weight11);

		float volValue = max(GetSpotVolIntegrate(fFinalT0, fFinalT1, cosHalfConeAngle, radius, fNdotD, fNdotO, fOdotD, fOdotO), 0);

		finalColor = float4(volValue * visibility * srt.consts->m_volColor.xyz, 0.0f) ;
	}

	return finalColor;
}

struct VolSunBlockPsTextures
{
	Texture2D<float>		src_depth;
	Texture2D<float4>		sample_info;
	Texture2D<float4>		visibility_buf;
};

struct VolSunBlockPsSamplers
{
	SamplerState			samplerPoint;
};

struct VolSunBlockPsSrt
{
	VolDirectLightParams	*consts;
	VolSunBlockPsTextures	*texs;
	VolSunBlockPsSamplers	*smpls;
};

float4 PS_VolumetricDirectLightDirectByShape(PS_Pos input, VolSunBlockPsSrt srt : S_SRT_DATA) : SV_Target
{
	float2 tex = input.Pos.xy * srt.consts->m_screenSize.zw;

	float4 finalColor = float4(0, 0, 0, 0);
	float3 viewVector = float3(tex * srt.consts->m_viewSpaceXyParams.xy + srt.consts->m_viewSpaceXyParams.zw, 1.0);

	float3 N = normalize(viewVector);

	float depthBufferZ = srt.texs->src_depth.Sample(srt.smpls->samplerPoint, tex).x;
	float viewSpaceZ = 1.0f / (depthBufferZ * srt.consts->m_linearDepthParams.x + srt.consts->m_linearDepthParams.y);
	float distVS = length(viewVector) * viewSpaceZ;

	float invVDotP0			= 1.0f / dot(N, srt.consts->m_clipPlanes[0].xyz);
	float invVDotP1			= 1.0f / dot(N, srt.consts->m_clipPlanes[1].xyz);
	float invVDotP2			= 1.0f / dot(N, srt.consts->m_clipPlanes[2].xyz);

	float dist0				= -srt.consts->m_clipPlanes[0].w * invVDotP0;
	float dist1				= -srt.consts->m_clipPlanes[1].w * invVDotP1;
	float dist2				= -srt.consts->m_clipPlanes[2].w * invVDotP2;
	float dist3				= srt.consts->m_clipDistance.x * invVDotP1;
	float dist4				= srt.consts->m_clipDistance.y * invVDotP2;
//	float dist5				= srt.consts->m_clipDistance.z * invVDotP0;

	float minDist			= -100000.0f;
	float maxDist			= 100000.0f;

	if (invVDotP0 > 0)
	{
		maxDist = min(maxDist, dist0);
//		minDist = max(minDist, dist5);
	}
	else
	{
		minDist = max(minDist, dist0);
//		maxDist = min(maxDist, dist5);
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

	float fFinalT0			= max(minDist, 0);
	float fFinalT1			= min(max(maxDist, 0), distVS);

	if (fFinalT1 > fFinalT0)
	{
		float2 cosSinRefAngle = normalize(float2(dot(N, srt.consts->m_sampleDirVS.xyz),  dot(N, srt.consts->m_sampleRightVS.xyz)));
		float angle = acos(clamp(cosSinRefAngle.y > 0 ? cosSinRefAngle.x : -cosSinRefAngle.x, -1.0f, 1.0f));

		float sampleArrayIndex = saturate(angle * srt.consts->m_sampleAngleInfo.y + srt.consts->m_sampleAngleInfo.z) * srt.consts->m_sampleAngleInfo.w;

		int sampleArrayIndexLow = int(min(sampleArrayIndex, srt.consts->m_sampleAngleInfo.w-1));
		int sampleArrayIndexHi = int(min(sampleArrayIndex + 1.0f, srt.consts->m_sampleAngleInfo.w-1));

		float4 viewRayDir = srt.texs->sample_info.Load(int3(sampleArrayIndexLow, 2, 0));
		float4 viewRayRight = srt.texs->sample_info.Load(int3(sampleArrayIndexLow, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexLow = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		viewRayDir = srt.texs->sample_info.Load(int3(sampleArrayIndexHi, 2, 0));
		viewRayRight = srt.texs->sample_info.Load(int3(sampleArrayIndexHi, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexHi = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		float2 visibility00 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexLow, 511.0), sampleArrayIndexLow, 0)).xy;
		float2 visibility01 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexLow+1.0f, 511.0), sampleArrayIndexLow, 0)).xy;
		float2 visibility10 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexHi, 511.0), sampleArrayIndexHi, 0)).xy;
		float2 visibility11 = srt.texs->visibility_buf.Load(int3(min(lookUpIndexHi+1.0f, 511.0), sampleArrayIndexHi, 0)).xy;

		float diffZ00 = (viewSpaceZ - visibility00.y);
		float diffZ01 = (viewSpaceZ - visibility01.y);
		float diffZ10 = (viewSpaceZ - visibility10.y);
		float diffZ11 = (viewSpaceZ - visibility11.y);

		float y = frac(sampleArrayIndex);
		float x0 = frac(lookUpIndexLow);
		float x1 = frac(lookUpIndexHi);

		float2 offset00 = float2(x0, y);
		float2 offset01 = float2(1.0f - x0, y);
		float2 offset10 = float2(x1, 1.0f - y);
		float2 offset11 = float2(1.0f - x1, 1.0f - y);

		float weight00 = 1.0f / ((1.0f + diffZ00 * diffZ00) * (1.0f + dot(offset00, offset00)));
		float weight01 = 1.0f / ((1.0f + diffZ01 * diffZ01) * (1.0f + dot(offset01, offset01)));
		float weight10 = 1.0f / ((1.0f + diffZ10 * diffZ10) * (1.0f + dot(offset10, offset10)));
		float weight11 = 1.0f / ((1.0f + diffZ11 * diffZ11) * (1.0f + dot(offset11, offset11)));

		float visibility = (visibility00.x * weight00 + visibility01.x * weight01 + visibility10.x * weight10 + visibility11.x * weight11) / (weight00 + weight01 + weight10 + weight11);
		visibility *= visibility;
		visibility *= visibility;

//		float integral = (fFinalT1 - fFinalT0) * (1.0f + srt.consts->m_clipPlanes[0].w * srt.consts->m_clipDistance.w) +
//					     0.5f * (fFinalT1 * fFinalT1 - fFinalT0 * fFinalT0) * dot(N, srt.consts->m_clipPlanes[0].xyz) * srt.consts->m_clipDistance.w;

		float integral = (fFinalT1 - fFinalT0);

		finalColor = float4(integral * visibility * srt.consts->m_color.xyz, 0.0f);
	}

	return finalColor;
}

float4 PS_VolumetricSunLightDirect (PS_PosTex input) : SV_Target
{
	float2 tex = input.Tex.xy;

	float4 finalColor = float4(0, 0, 0, 0);
	float3 viewVector = float3(tex * g_volDirectLightParams.m_viewSpaceXyParams.xy + g_volDirectLightParams.m_viewSpaceXyParams.zw, 1.0);

	float3 N = normalize(viewVector);

	float depthBufferZ = txDepthBuffer.Sample(g_sSamplerPoint, tex).x;
	float viewSpaceZ = 1.0f / (depthBufferZ * g_volDirectLightParams.m_linearDepthParams.x + g_volDirectLightParams.m_linearDepthParams.y);
	float distVS = length(viewVector) * viewSpaceZ;

	float fFinalT0			= 0.0f;
	float fFinalT1			= min(g_volDirectLightParams.m_params.z, distVS);

	if (fFinalT1 > fFinalT0)
	{
		float2 cosSinRefAngle = normalize(float2(dot(N, g_volDirectLightParams.m_sampleDirVS.xyz),  dot(N, g_volDirectLightParams.m_sampleRightVS.xyz)));
		float angle = acos(clamp(cosSinRefAngle.y > 0 ? cosSinRefAngle.x : -cosSinRefAngle.x, -0.9999999f, 0.9999999f));

		float sampleArrayIndex = saturate(angle * g_volDirectLightParams.m_sampleAngleInfo.y + g_volDirectLightParams.m_sampleAngleInfo.z) * g_volDirectLightParams.m_sampleAngleInfo.w;

		int sampleArrayIndexLow = int(min(sampleArrayIndex, g_volDirectLightParams.m_sampleAngleInfo.w-1));
		int sampleArrayIndexHi = int(min(sampleArrayIndex + 1.0f, g_volDirectLightParams.m_sampleAngleInfo.w-1));

		float4 viewRayDir = txDiffuse1.Load(int3(sampleArrayIndexLow, 2, 0));
		float4 viewRayRight = txDiffuse1.Load(int3(sampleArrayIndexLow, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexLow = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		viewRayDir = txDiffuse1.Load(int3(sampleArrayIndexHi, 2, 0));
		viewRayRight = txDiffuse1.Load(int3(sampleArrayIndexHi, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndexHi = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		float3 visibility00 = txDiffuse2.Load(int3(min(lookUpIndexLow, 511.0), sampleArrayIndexLow, 0)).xyw;
		float3 visibility01 = txDiffuse2.Load(int3(min(lookUpIndexLow+1.0f, 511.0), sampleArrayIndexLow, 0)).xyw;
		float3 visibility10 = txDiffuse2.Load(int3(min(lookUpIndexHi, 511.0), sampleArrayIndexHi, 0)).xyw;
		float3 visibility11 = txDiffuse2.Load(int3(min(lookUpIndexHi+1.0f, 511.0), sampleArrayIndexHi, 0)).xyw;

		float diffZ00 = (viewSpaceZ - visibility00.z);
		float diffZ01 = (viewSpaceZ - visibility01.z);
		float diffZ10 = (viewSpaceZ - visibility10.z);
		float diffZ11 = (viewSpaceZ - visibility11.z);

		float y = frac(sampleArrayIndex);
		float x0 = frac(lookUpIndexLow);
		float x1 = frac(lookUpIndexHi);

		float2 offset00 = float2(x0, y);
		float2 offset01 = float2(1.0f - x0, y);
		float2 offset10 = float2(x1, 1.0f - y);
		float2 offset11 = float2(1.0f - x1, 1.0f - y);

		float weight00 = 1.0f / ((1.0f + diffZ00 * diffZ00) * (1.0f + dot(offset00, offset00)));
		float weight01 = 1.0f / ((1.0f + diffZ01 * diffZ01) * (1.0f + dot(offset01, offset01)));
		float weight10 = 1.0f / ((1.0f + diffZ10 * diffZ10) * (1.0f + dot(offset10, offset10)));
		float weight11 = 1.0f / ((1.0f + diffZ11 * diffZ11) * (1.0f + dot(offset11, offset11)));

		float2 visibility = (visibility00.xy * weight00 + visibility01.xy * weight01 + visibility10.xy * weight10 + visibility11.xy * weight11) / (weight00 + weight01 + weight10 + weight11);

/*		float integral = (fFinalT1 - fFinalT0) * (1.0f + g_volDirectLightParams.m_clipPlanes[0].w * g_volDirectLightParams.m_clipDistance.w) +
					     0.5f * (fFinalT1 * fFinalT1 - fFinalT0 * fFinalT0) * dot(N, g_volDirectLightParams.m_clipPlanes[0].xyz) * g_volDirectLightParams.m_clipDistance.w;*/
//		float integral = (fFinalT1 - fFinalT0);

		float adjustFactor = 1.0f / (1.0f + g_volDirectLightParams.m_params.y * txDiffuse3.Load(int3(0, 0, 0)).x);

		finalColor = float4(visibility.xxx * g_volDirectLightParams.m_color.xyz * adjustFactor, 1.0f - (1.0f - visibility.y) * adjustFactor);
	}

	return finalColor;
}

float4 PS_VolumetricSunFogIntensity (PS_PosTex input) : SV_Target
{
	float2 tex = input.Tex.xy;

	float4 finalColor = float4(0, 0, 0, 0);
	float3 viewVector = float3(tex * g_volDirectLightParams.m_viewSpaceXyParams.xy + g_volDirectLightParams.m_viewSpaceXyParams.zw, 1.0);

	float3 N = normalize(viewVector);

	float depthBufferZ = txDepthBuffer.Sample(g_sSamplerPoint, tex).x;
	float viewSpaceZ = 1.0f / (depthBufferZ * g_volDirectLightParams.m_linearDepthParams.x + g_volDirectLightParams.m_linearDepthParams.y);
	float distVS = length(viewVector) * viewSpaceZ;

	float fFinalT0			= 0.0f;
	float fFinalT1			= min(g_volDirectLightParams.m_params.z, distVS);

	if (fFinalT1 > fFinalT0)
	{
		float2 cosSinRefAngle = normalize(float2(dot(N, g_volDirectLightParams.m_sampleDirVS.xyz),  dot(N, g_volDirectLightParams.m_sampleRightVS.xyz)));
		float angle = acos(clamp(cosSinRefAngle.y > 0 ? cosSinRefAngle.x : -cosSinRefAngle.x, -0.9999999f, 0.9999999f));

		float fSampleArrayIndex = saturate(angle * g_volDirectLightParams.m_sampleAngleInfo.y + g_volDirectLightParams.m_sampleAngleInfo.z) * g_volDirectLightParams.m_sampleAngleInfo.w;

		int sampleArrayIndex = int(min(fSampleArrayIndex, g_volDirectLightParams.m_sampleAngleInfo.w-1));

		float4 viewRayDir = txDiffuse1.Load(int3(sampleArrayIndex, 2, 0));
		float4 viewRayRight = txDiffuse1.Load(int3(sampleArrayIndex, 3, 0));

		cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight.xyz)));
		angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

		float lookUpIndex = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

		float visibility = txDiffuse2.Sample(g_sSamplerLinear, float2((lookUpIndex + 0.5f) * g_volDirectLightParams.m_shadowScreenViewParams.y, (sampleArrayIndex + 0.5f) * g_volDirectLightParams.m_shadowScreenViewParams.x)).x;

		finalColor = visibility;
	}

	return finalColor;
}

float4 PS_VolumetricSunLightMerge(PS_PosTex input) : SV_Target
{
	float4 volLightColor = float4(g_volLightMergeParams.m_volColorInfo.xyz * txDiffuse.Sample(g_sSamplerLinear, input.Tex).x * 1.0f / (1.0f + g_volLightMergeParams.m_volParams.x * txDiffuse1.Sample(g_sSamplerPoint, float2(0.5f, 0.5f)).x), 0.0f);
	return volLightColor * g_volLightMergeParams.m_volColorInfo.w/* * float4(txDiffuse2.Sample(g_sSamplerLinear, input.Tex).xyz, 1.0f)*/;
}

float4 LoadSamplesByIndex(uint sampleArrayIndex, uint sampleIdxBy4)
{
	return txSampleListBuffer.Load(int3((int)sampleIdxBy4, (int)sampleArrayIndex, 0));
/*
	uint4 iSamplesInfo = tSamplesBuffer.Load4(sampleArrayIndex * 4096 + sampleIdxBy4 * 16);
	return float4(asfloat(iSamplesInfo.x), asfloat(iSamplesInfo.y), asfloat(iSamplesInfo.z), asfloat(iSamplesInfo.w)); */
}

float CalculateSunlightVol(/*float t, */float sunlightVolIntensity, float3 vsPosition)
{
	float tFact = dot(g_volLightParams.m_viewToWorldMatY.xyz, vsPosition);
	float worldSpacePositionY = tFact + g_volLightParams.m_viewToWorldMatY.w;

	if (worldSpacePositionY > g_volLightParams.m_sampleHeightDensityInfo.x)
	{
		float x = (g_volLightParams.m_sampleHeightDensityInfo.x - g_volLightParams.m_viewToWorldMatY.w) / tFact;
		vsPosition *= x;
		tFact *= x;
	}

	return saturate(((g_volLightParams.m_sampleHeightDensityInfo.x - g_volLightParams.m_viewToWorldMatY.w) - tFact * 0.5f) * sunlightVolIntensity);
	//return saturate(t * sunlightVolIntensity);
}

float4 PS_VolumetricSunLight (PS_PosTex input) : SV_Target
{
	float4 finalColor = float4(1, 0, 0, 0);
	float3 viewVector = float3(input.Tex.xy * g_volLightParams.m_viewSpaceXyParams.xy + g_volLightParams.m_viewSpaceXyParams.zw, 1.0);

	float3 sunlightDir = g_volLightParams.m_lightDirAndConAngle.xyz;
	//float3 sunlightColor = g_volLightParams.m_lightPosAndRadius.xyz;
	float sunlightVolIntensity = 1.0f;//g_volLightParams.m_lightPosAndRadius.w;

	float depthBufferZ = txDepthBuffer.Sample(g_sSamplerPoint, input.Tex).x;
	float viewSpaceZ = 1.0f / (depthBufferZ * g_volLightParams.m_linearDepthParams.x + g_volLightParams.m_linearDepthParams.y);
	float3 vsPosition = viewSpaceZ * viewVector;
	//float vsDist = length(vsPosition);

	viewVector = normalize(vsPosition);

	float2 cosSinRefAngle = normalize(float2(dot(viewVector, g_volLightParams.m_sampleDirVector.xyz), dot(viewVector, g_volLightParams.m_sampleRightVector.xyz)));
	float refAngle = acos(min(cosSinRefAngle.x, 1.0f));
	refAngle = cosSinRefAngle.y > 0 ? refAngle : -refAngle;

	float2 dither = frac(input.Pos.yx * 0.4 * 0.434 + input.Pos.xy * 0.434) * 2.0f - 1.0f;

	float fSampleArrayIndex = saturate(refAngle * g_volLightParams.m_sampleAngleInfo.x + g_volLightParams.m_sampleAngleInfo.y) * g_volLightParams.m_sampleAngleInfo.w + dither.x * g_volLightParams.m_ditherParams.x;
	fSampleArrayIndex = fSampleArrayIndex < 0 ? -fSampleArrayIndex : fSampleArrayIndex;
	fSampleArrayIndex = fSampleArrayIndex >= g_volLightParams.m_sampleAngleInfo.w ? (g_volLightParams.m_sampleAngleInfo.w - 1.0f) * 2.0f - fSampleArrayIndex : fSampleArrayIndex;

	int sampleArrayIndex = (int)fSampleArrayIndex;

	uint4 iSampleDirInfo = tSampleInfoBuffer.Load4(sampleArrayIndex * 32);
	uint4 iSampleInfo1 = tSampleInfoBuffer.Load4(sampleArrayIndex * 32 + 16);
	float4 sampleDirInfo = float4(asfloat(iSampleDirInfo.x), asfloat(iSampleDirInfo.y), asfloat(iSampleDirInfo.z), asfloat(iSampleDirInfo.w));
	float3 sampleDir = sampleDirInfo.xyz;
	float maxSampleDist = sampleDirInfo.w;

	float2 sampleScaleOffset =  float2(asfloat(iSampleInfo1.x), asfloat(iSampleInfo1.y));

	float refCosAngle = clamp(dot(sunlightDir, viewVector) * sampleScaleOffset.x + sampleScaleOffset.y + g_volLightParams.m_linearDepthParams.z, kClampCap * 0.5f, 1.0f - kClampCap * 0.5f);

	float3 rayEndClipPlane = normalize(cross(sunlightDir, cross(sunlightDir, viewVector)));

	float distOnSampleDir = dot(rayEndClipPlane, vsPosition) / dot(rayEndClipPlane, sampleDir);

	//float t1 = vsDist;

	float fVolIntensity = CalculateSunlightVol(/*t1, */sunlightVolIntensity, vsPosition);

	float fNumSamples = clamp(ReverseSampleDistribute(saturate(distOnSampleDir / maxSampleDist)) * 
							g_volLightParams.m_sampleAngleInfo.z * (1.0f + dither.y * g_volLightParams.m_ditherParams.y * (1.0f / g_volLightParams.m_sampleAngleInfo.z)), 
							0.0f, g_volLightParams.m_sampleAngleInfo.z);
	uint numSamples = (uint)fNumSamples;
	uint numSamplesBy4 = numSamples / 4;
	//float ratioLastSample = fNumSamples - numSamples;

	float4 sumVisible4 = 0;
	float4 fCmpResult = 0;
	for (uint i = 0; i < numSamplesBy4; i++)
	{
		float4 sampleVal = LoadSamplesByIndex(sampleArrayIndex, i);
		bool4 cmpResult = refCosAngle < sampleVal;
		fCmpResult = cmpResult;
		sumVisible4 += fCmpResult;
	}

	float sumVisible = dot(sumVisible4, 1.0);
	{
		uint numSamplesLeft = numSamples & 0x03;
		float4 sampleVal = LoadSamplesByIndex(sampleArrayIndex, numSamplesBy4);
		bool4 cmpResult = refCosAngle < sampleVal;
		float4 fCmpResultLast = cmpResult;

		if (numSamplesLeft > 0)
		{
			for (uint i = 0; i < numSamplesLeft; i++)
				sumVisible += fCmpResultLast[i];
		}

		sumVisible += fCmpResultLast[numSamplesLeft] * (fNumSamples - numSamples);
	}

	float visibleRatio = sumVisible / fNumSamples;

	//finalColor = visibleRatio * 0.5f;//float4(fVolIntensity * float3(1,1,1)/*sunlightColor*/ * pow(visibleRatio, 1), 0.0f);
	finalColor = fVolIntensity * pow(max(visibleRatio, 0.0f), g_volLightParams.m_ditherParams.z);

	return finalColor;
}

float2 UnpackPackScreenPos(uint uPos)
{
	uint2 screenPos = uint2(uPos & 0xffff, uPos >> 16);

	float2 fPos = screenPos / 65535.0f;
	
	return fPos.xy * g_volLightParams.m_unpackScreenPosParam.xy + g_volLightParams.m_unpackScreenPosParam.zw;
}

float GetT(float v0DotClip, float v1DotClip, float cutPosDotClip)
{
	float d0 = v1DotClip - v0DotClip;
	float d1 = cutPosDotClip - v0DotClip;

	return d1 / d0;
}

float4 PS_VolumetricSunLightCircle (PS_PosTex input) : SV_Target
{
	float4 finalColor = float4(1, 0, 0, 0);
	float3 viewVector = float3(input.Tex.xy * g_volLightParams.m_viewSpaceXyParams.xy + g_volLightParams.m_viewSpaceXyParams.zw, 1.0);

	float3 sunlightDir = g_volLightParams.m_lightDirAndConAngle.xyz;
	//float3 sunlightColor = g_volLightParams.m_lightPosAndRadius.xyz;
	float sunlightVolIntensity = 1.0f;//g_volLightParams.m_lightPosAndRadius.w;
	float maxViewDist = g_volLightParams.m_linearDepthParams.w;

	float depthBufferZ = txDepthBuffer.Sample(g_sSamplerPoint, input.Tex).x;
	float viewSpaceZ = 1.0f / (depthBufferZ * g_volLightParams.m_linearDepthParams.x + g_volLightParams.m_linearDepthParams.y);
	float3 vsPosition = viewSpaceZ * viewVector;
	float vsDist = length(vsPosition);

	viewVector = normalize(vsPosition);

	float2 cosSinRefAngle = normalize(float2(dot(viewVector, g_volLightParams.m_sampleDirVector.xyz), dot(viewVector, g_volLightParams.m_sampleRightVector.xyz)));

	float refAngle = acos(min(cosSinRefAngle.x, 1.0f));
	refAngle = cosSinRefAngle.y > 0 ? refAngle : kPi-refAngle;

	float2 screenPos = input.Tex.xy * float2(1920, 1080);

	float2 dither = frac(screenPos.yx * 0.4 * 0.434 + screenPos.xy * 0.434) * 2.0f - 1.0f;

	float fSampleArrayIndex = saturate(refAngle * g_volLightParams.m_sampleAngleInfo.x + g_volLightParams.m_sampleAngleInfo.y) * g_volLightParams.m_sampleAngleInfo.w + dither.x * g_volLightParams.m_ditherParams.x;
	fSampleArrayIndex = fSampleArrayIndex < 0 ? -fSampleArrayIndex : fSampleArrayIndex;
	fSampleArrayIndex = fSampleArrayIndex >= g_volLightParams.m_sampleAngleInfo.w ? (g_volLightParams.m_sampleAngleInfo.w - 1.0f) * 2.0f - fSampleArrayIndex : fSampleArrayIndex;

	int sampleArrayIndex = (int)fSampleArrayIndex;

	uint4 iSampleInfo0 = tSampleInfoBuffer.Load4(sampleArrayIndex * 32);
	uint4 iSampleInfo1 = tSampleInfoBuffer.Load4(sampleArrayIndex * 32 + 16);
	float3 sampleRayEnd[2];
	sampleRayEnd[0] = float3(UnpackPackScreenPos(iSampleInfo0.x), 1.0f) * maxViewDist;
	sampleRayEnd[1] = float3(UnpackPackScreenPos(iSampleInfo0.y), 1.0f) * maxViewDist;
	float3 sampleCenter = float3(UnpackPackScreenPos(iSampleInfo0.z), 1.0f) * maxViewDist;
	uint bCenterVisible = iSampleInfo0.w >> 15;
	uint numSplitSamples = (iSampleInfo0.w & 0x00007fff) << 2;

	float2 sampleScaleOffset =  float2(asfloat(iSampleInfo1.x), asfloat(iSampleInfo1.y));

	float refAngleValue = dot(sunlightDir, viewVector);

	float sinAngle = 1.0f - sqrt(max(1.0f - refAngleValue * refAngleValue, 0.0f));
	refAngleValue = refAngleValue > 0 ? sinAngle : -sinAngle;

	if (sunlightDir.z > 0)
		refAngleValue -= (1.0f - saturate(vsDist / maxViewDist)) * 0.05f;
	else
		refAngleValue += (1.0f - saturate(vsDist / maxViewDist)) * 0.05f;

	refAngleValue = clamp(refAngleValue * sampleScaleOffset.x + sampleScaleOffset.y, kClampCap * 0.5f, 1.0f - kClampCap * 0.5f);

	float3 rayEndClipPlane = normalize(cross(sunlightDir, cross(sunlightDir, viewVector)));

	float3 clampedVsPos = vsPosition.z > maxViewDist ? (vsPosition / vsPosition.z) * maxViewDist : vsPosition;

	float v0DotN = dot(sampleRayEnd[0], rayEndClipPlane);
	float v1DotN = dot(sampleRayEnd[1], rayEndClipPlane);
	float cDotN = dot(sampleCenter, rayEndClipPlane);
	float cutPosDotClip = dot(clampedVsPos, rayEndClipPlane);

	float t0 = GetT(cDotN, v0DotN, cutPosDotClip);
	float t1 = GetT(cDotN, v1DotN, cutPosDotClip);

	uint baseIdx = 0;
	uint numSamples = 0;
	float3 curSampleRayEnd;
	float t;
	if (t0 > 0.0f)
	{
		t = saturate(t0);
		numSamples = numSplitSamples;
		curSampleRayEnd = sampleRayEnd[0];
	}
	else
	{
		t = saturate(t1);
		baseIdx = numSplitSamples;
		numSamples = g_volLightParams.m_sampleAngleInfo.z - baseIdx;
		curSampleRayEnd = sampleRayEnd[1];
	}

	float fVolIntensity = CalculateSunlightVol(/*t1, */sunlightVolIntensity, vsPosition);

	float tEnd = t * numSamples;
	uint iEnd = (uint)tEnd;

	float sumVisible = 0;
	if (tEnd < 1.0f)
	{
		sumVisible = bCenterVisible * tEnd;
	}
	else
	{
		uint startIdxBy4 = (baseIdx >> 2);
		uint numSampleSubOne = max(iEnd - 1, 0);
		uint numIdxBy4 = numSampleSubOne >> 2;

		float4 sumVisible4 = 0;
		uint i;
		for (i = startIdxBy4; i < startIdxBy4 + numIdxBy4; i++)
		{
			float4 sampleVal = LoadSamplesByIndex(sampleArrayIndex, i);
			bool4 cmpResult = refAngleValue < sampleVal;
			float4 fCmpResult = cmpResult;
			sumVisible4 += fCmpResult;
		}

		sumVisible = dot(sumVisible4, 1.0) + bCenterVisible;

		uint numSamplesLeft = numSampleSubOne & 0x03;
		float4 sampleVal = LoadSamplesByIndex(sampleArrayIndex, startIdxBy4 + numIdxBy4);
		bool4 cmpResult = refAngleValue < sampleVal;
		float4 fCmpResultLast = cmpResult;

		for (i = 0; i < numSamplesLeft; i++)
			sumVisible += fCmpResultLast[i];

		sumVisible += fCmpResultLast[numSamplesLeft] * frac(tEnd);
	}

	float visibleRatio = sumVisible / tEnd;

	//finalColor = visibleRatio * 0.5f;//float4(fVolIntensity * float3(1,1,1)/*sunlightColor*/ * pow(visibleRatio, 1), 0.0f);
	finalColor = fVolIntensity * pow(max(visibleRatio, 0.0f), g_volLightParams.m_ditherParams.z);

	return finalColor;
}

