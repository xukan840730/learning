//--------------------------------------------------------------------------------------
// File: post-processing.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "global-srt.fxi"

struct PostFogConst
{
	float4		m_screenToViewParams;
	float4		m_viewToWorldMat[3];
	float4		m_cameraPosition;
	float4		m_fogRangeInfo;
	float4		m_fogHeightRangeInfo;
	float4		m_fogColorInfo;
	float4		m_fogSecondColorInfo;
	float4		m_fogColorsVector;
	float4		m_fogDensityInfo;
	float4		m_fogAmbientColorInfo;
	float4		m_skyFogRangeInfo;
	float4		m_skyFogDensityInfo;
	float4		m_skyFogColorInfo;
	float4		m_skyFogSecondColorInfo;
	float4		m_skyFogColorsVector;
};

struct PostFogTextures 
{
	Texture2D<float4> tx_depth;
	Texture2D<float4> tx_nofog;
	Texture2D<float4> tx_noise;
};

struct PostFogSamplers
{
	SamplerState sSamplerPoint;
	SamplerState sSamplerLinear;
};

struct PostFogSrt
{
	PostFogConst *pConsts;
	PostFogTextures *pTexs;	
	PostFogSamplers *pSamplers;
};

float4 PS_ApplyFogBlend(PS_PosTex input, PostFogSrt srt : S_SRT_DATA) : SV_Target
{
	float4 outColor;

	float depthBufferZ = srt.pTexs->tx_depth.Sample(srt.pSamplers->sSamplerPoint, input.Tex).x;
	float viewSpaceZ = srt.pConsts->m_screenToViewParams.w / (depthBufferZ - srt.pConsts->m_screenToViewParams.z);
	float2 screenSpaceXy = input.Tex * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float4 viewSpacePosition =
	    float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0);
	float worldPositionY = dot(srt.pConsts->m_viewToWorldMat[1], viewSpacePosition);

	float3 fogColor = srt.pConsts->m_fogColorInfo.rgb;

	if (depthBufferZ < 1.f) // not sky
	{
		float fFogDistanceScalar =
		    max(0.0, viewSpaceZ * srt.pConsts->m_fogRangeInfo.x + srt.pConsts->m_fogRangeInfo.y) *
		    max(0.0, worldPositionY * srt.pConsts->m_fogRangeInfo.z + srt.pConsts->m_fogRangeInfo.w);
		float fFogHeightScalar =
		    max(0.0, worldPositionY * srt.pConsts->m_fogHeightRangeInfo.z + srt.pConsts->m_fogHeightRangeInfo.w);

		float2 fFogFactorPair =
		    float2(exp2(-fFogDistanceScalar * fFogDistanceScalar), exp(-fFogHeightScalar * fFogHeightScalar));
		fFogFactorPair = fFogFactorPair * srt.pConsts->m_fogDensityInfo.xz + srt.pConsts->m_fogDensityInfo.yw;
		fFogFactorPair = 1 - fFogFactorPair;

		float fFogFactor = max(fFogFactorPair.x, max(fFogFactorPair.y, srt.pConsts->m_fogAmbientColorInfo.r));
		outColor.rgba = float4(fogColor * fFogFactor, fFogFactor);
	}
	else // sky
	{
		// assume distance is 1000 to avoid artifacts
		viewSpaceZ = 1000.0f;
		worldPositionY =
		    dot(srt.pConsts->m_viewToWorldMat[1],
		        float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0));
		float3 skyFogColor = srt.pConsts->m_skyFogColorInfo.rgb;

		float fFogHeightScalar =
		    saturate(worldPositionY * srt.pConsts->m_skyFogRangeInfo.z + srt.pConsts->m_skyFogRangeInfo.w);

		float fHeightAlpha = 1.618 - 1.0 / (fFogHeightScalar + 0.618);

		float finalAlpha = 1 - (srt.pConsts->m_skyFogDensityInfo.y * fHeightAlpha + (1 - fHeightAlpha));
		float3 finalFogAndAlpha = (skyFogColor * srt.pConsts->m_skyFogDensityInfo.x * fHeightAlpha);

		outColor.rgba = float4(finalFogAndAlpha, finalAlpha);

		outColor.rgb += fogColor * srt.pConsts->m_fogAmbientColorInfo.r;
		outColor.a += srt.pConsts->m_fogAmbientColorInfo.r;
	}

	outColor.rgb = max(outColor.rgb, 0.f);
	outColor.a = saturate(outColor.a);
	return outColor;
}

float4 PS_ApplyFogNoiseBlend(PS_PosTex input, PostFogSrt srt : S_SRT_DATA) : SV_Target
{
	float4 outColor;

	float depthBufferZ = srt.pTexs->tx_depth.Sample(srt.pSamplers->sSamplerPoint, input.Tex).x;
	float viewSpaceZ = srt.pConsts->m_screenToViewParams.w / (depthBufferZ - srt.pConsts->m_screenToViewParams.z);
	float2 screenSpaceXy = input.Tex * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float4 viewSpacePosition =
	    float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0);
	float worldPositionY = dot(srt.pConsts->m_viewToWorldMat[1], viewSpacePosition);

	float3 fogColor = srt.pConsts->m_fogColorInfo.rgb;
	float fogNoiseIntensity = srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerLinear, input.Tex).x;

	if (depthBufferZ < 1.f) // not sky
	{
		float fFogDistanceScalar =
		    max(0.0, viewSpaceZ * srt.pConsts->m_fogRangeInfo.x + srt.pConsts->m_fogRangeInfo.y) *
		    max(0.0, worldPositionY * srt.pConsts->m_fogRangeInfo.z + srt.pConsts->m_fogRangeInfo.w);
		float fFogHeightScalar =
		    max(0.0, worldPositionY * srt.pConsts->m_fogHeightRangeInfo.z + srt.pConsts->m_fogHeightRangeInfo.w);

		float2 fFogFactorPair =
		    float2(exp2(-fFogDistanceScalar * fFogDistanceScalar), exp(-fFogHeightScalar * fFogHeightScalar));
		fFogFactorPair = fFogFactorPair * srt.pConsts->m_fogDensityInfo.xz + srt.pConsts->m_fogDensityInfo.yw;
		fFogFactorPair = 1 - fFogFactorPair;

		float fFogFactor = max(fFogFactorPair.x, max(fFogFactorPair.y, srt.pConsts->m_fogAmbientColorInfo.r));
		outColor.rgba = float4(fogColor * fogNoiseIntensity * fFogFactor, fFogFactor);
	}
	else // sky
	{
		// assume distance is 1000 to avoid artifacts
		viewSpaceZ = 1000.0f;
		worldPositionY =
		    dot(srt.pConsts->m_viewToWorldMat[1],
		        float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0));
		float3 skyFogColor = srt.pConsts->m_skyFogColorInfo.rgb;

		float fFogHeightScalar =
		    saturate(worldPositionY * srt.pConsts->m_skyFogRangeInfo.z + srt.pConsts->m_skyFogRangeInfo.w);

		float fHeightAlpha = 1.618 - 1.0 / (fFogHeightScalar + 0.618);

		float finalAlpha = 1 - (srt.pConsts->m_skyFogDensityInfo.y * fHeightAlpha + (1 - fHeightAlpha));
		float3 finalFogAndAlpha = (skyFogColor * srt.pConsts->m_skyFogDensityInfo.x * fHeightAlpha);

		outColor.rgba = float4(finalFogAndAlpha, finalAlpha);

		outColor.rgb += fogColor * fogNoiseIntensity * srt.pConsts->m_fogAmbientColorInfo.r;
		outColor.a += srt.pConsts->m_fogAmbientColorInfo.r;
	}

	outColor.rgb = max(outColor.rgb, 0.f);
	outColor.a = saturate(outColor.a);
	return outColor;
}

float4 PS_ApplyFogBlendWithNoFogZone(PS_PosTex input, PostFogSrt srt : S_SRT_DATA) : SV_Target
{
	float4 outColor;

	float depthBufferZ = srt.pTexs->tx_depth.Sample(srt.pSamplers->sSamplerPoint, input.Tex).x;
	float viewSpaceZ = srt.pConsts->m_screenToViewParams.w / (depthBufferZ - srt.pConsts->m_screenToViewParams.z);
	float2 screenSpaceXy = input.Tex * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float4 viewSpacePosition =
	    float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0);
	float worldPositionY = dot(srt.pConsts->m_viewToWorldMat[1], viewSpacePosition);

	float3 fogColor = srt.pConsts->m_fogColorInfo.rgb;

	if (depthBufferZ < 1.f) // not sky
	{
		float noFogZoneThickness = srt.pTexs->tx_nofog.Sample(srt.pSamplers->sSamplerPoint, input.Tex).a;
		float effectiveViewZ = viewSpaceZ - noFogZoneThickness;
		float groundFogScaler = -noFogZoneThickness / viewSpaceZ + 1.0;
		groundFogScaler *= groundFogScaler;

		float fFogDistanceScalar =
		    max(0.0, effectiveViewZ * srt.pConsts->m_fogRangeInfo.x + srt.pConsts->m_fogRangeInfo.y) *
		    max(0.0, worldPositionY * srt.pConsts->m_fogRangeInfo.z + srt.pConsts->m_fogRangeInfo.w);
		float fFogHeightScalar =
		    max(0.0, worldPositionY * srt.pConsts->m_fogHeightRangeInfo.z + srt.pConsts->m_fogHeightRangeInfo.w);

		float2 fFogFactorPair =
		    float2(exp2(-fFogDistanceScalar * fFogDistanceScalar), exp(-fFogHeightScalar * fFogHeightScalar));
		fFogFactorPair = fFogFactorPair * srt.pConsts->m_fogDensityInfo.xz + srt.pConsts->m_fogDensityInfo.yw;
		fFogFactorPair = 1 - fFogFactorPair;

		float fFogFactor =
		    max(fFogFactorPair.x, max(fFogFactorPair.y, srt.pConsts->m_fogAmbientColorInfo.r) * groundFogScaler);
		outColor.rgba = float4(fogColor * fFogFactor, fFogFactor);
	}
	else // sky
	{
		// assume distance is 1000 to avoid artifacts
		viewSpaceZ = 1000.0f;
		worldPositionY =
		    dot(srt.pConsts->m_viewToWorldMat[1],
		        float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0));
		float3 skyFogColor = srt.pConsts->m_skyFogColorInfo.rgb;

		float fFogHeightScalar =
		    saturate(worldPositionY * srt.pConsts->m_skyFogRangeInfo.z + srt.pConsts->m_skyFogRangeInfo.w);

		float fHeightAlpha = 1.618 - 1.0 / (fFogHeightScalar + 0.618);

		float finalAlpha = 1 - (srt.pConsts->m_skyFogDensityInfo.y * fHeightAlpha + (1 - fHeightAlpha));
		float3 finalFogAndAlpha = (skyFogColor * srt.pConsts->m_skyFogDensityInfo.x * fHeightAlpha);

		outColor.rgba = float4(finalFogAndAlpha, finalAlpha);

		outColor.rgb += fogColor * srt.pConsts->m_fogAmbientColorInfo.r;
		outColor.a += srt.pConsts->m_fogAmbientColorInfo.r;
	}

	outColor.rgb = max(outColor.rgb, 0.f);
	outColor.a = saturate(outColor.a);
	return outColor;
}

float4 PS_ApplyFogNoiseBlendWithNoFogZone(PS_PosTex input, PostFogSrt srt : S_SRT_DATA) : SV_Target
{
	float4 outColor;

	float depthBufferZ = srt.pTexs->tx_depth.Sample(srt.pSamplers->sSamplerPoint, input.Tex).x;
	float viewSpaceZ = srt.pConsts->m_screenToViewParams.w / (depthBufferZ - srt.pConsts->m_screenToViewParams.z);
	float2 screenSpaceXy = input.Tex * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float4 viewSpacePosition =
	    float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0);
	float worldPositionY = dot(srt.pConsts->m_viewToWorldMat[1], viewSpacePosition);

	float3 fogColor = srt.pConsts->m_fogColorInfo.rgb;
	float fogNoiseIntensity = srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerLinear, input.Tex).x;

	if (depthBufferZ < 1.f) // not sky
	{
		float noFogZoneThickness = srt.pTexs->tx_nofog.Sample(srt.pSamplers->sSamplerPoint, input.Tex).a;
		float effectiveViewZ = viewSpaceZ - noFogZoneThickness;
		float groundFogScaler = -noFogZoneThickness / viewSpaceZ + 1.0;
		groundFogScaler *= groundFogScaler;

		float fFogDistanceScalar =
		    max(0.0, effectiveViewZ * srt.pConsts->m_fogRangeInfo.x + srt.pConsts->m_fogRangeInfo.y) *
		    max(0.0, worldPositionY * srt.pConsts->m_fogRangeInfo.z + srt.pConsts->m_fogRangeInfo.w);
		float fFogHeightScalar =
		    max(0.0, worldPositionY * srt.pConsts->m_fogHeightRangeInfo.z + srt.pConsts->m_fogHeightRangeInfo.w);

		float2 fFogFactorPair =
		    float2(exp2(-fFogDistanceScalar * fFogDistanceScalar), exp(-fFogHeightScalar * fFogHeightScalar));
		fFogFactorPair = fFogFactorPair * srt.pConsts->m_fogDensityInfo.xz + srt.pConsts->m_fogDensityInfo.yw;
		fFogFactorPair = 1 - fFogFactorPair;

		float fFogFactor =
		    max(fFogFactorPair.x, max(fFogFactorPair.y, srt.pConsts->m_fogAmbientColorInfo.r) * groundFogScaler);
		outColor.rgba = float4(fogColor * fogNoiseIntensity * fFogFactor, fFogFactor);
	}
	else // sky
	{
		// assume distance is 1000 to avoid artifacts
		viewSpaceZ = 1000.0f;
		worldPositionY =
		    dot(srt.pConsts->m_viewToWorldMat[1],
		        float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0));
		float3 skyFogColor = srt.pConsts->m_skyFogColorInfo.rgb;

		float fFogHeightScalar =
		    saturate(worldPositionY * srt.pConsts->m_skyFogRangeInfo.z + srt.pConsts->m_skyFogRangeInfo.w);

		float fHeightAlpha = 1.618 - 1.0 / (fFogHeightScalar + 0.618);

		float finalAlpha = 1 - (srt.pConsts->m_skyFogDensityInfo.y * fHeightAlpha + (1 - fHeightAlpha));
		float3 finalFogAndAlpha = (skyFogColor * srt.pConsts->m_skyFogDensityInfo.x * fHeightAlpha);

		outColor.rgba = float4(finalFogAndAlpha, finalAlpha);

		outColor.rgb += fogColor * fogNoiseIntensity * srt.pConsts->m_fogAmbientColorInfo.r;
		outColor.a += srt.pConsts->m_fogAmbientColorInfo.r;
	}

	outColor.rgb = max(outColor.rgb, 0.f);
	outColor.a = saturate(outColor.a);
	return outColor;
}

float4 PS_ApplyTwoColoredFogBlend(PS_PosTex input, PostFogSrt srt : S_SRT_DATA) : SV_Target
{
	float4 outColor;

	float depthBufferZ = srt.pTexs->tx_depth.Sample(srt.pSamplers->sSamplerPoint, input.Tex).x;
	float viewSpaceZ = srt.pConsts->m_screenToViewParams.w / (depthBufferZ - srt.pConsts->m_screenToViewParams.z);
	float2 screenSpaceXy = input.Tex * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float4 viewSpacePosition =
	    float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0);
	float worldPositionX = dot(srt.pConsts->m_viewToWorldMat[0], viewSpacePosition);
	float worldPositionY = dot(srt.pConsts->m_viewToWorldMat[1], viewSpacePosition);
	float worldPositionZ = dot(srt.pConsts->m_viewToWorldMat[2], viewSpacePosition);
	float3 worldPosition = float3(worldPositionX, worldPositionY, worldPositionZ);

	worldPosition -= srt.pConsts->m_cameraPosition.xyz;
	worldPosition = normalize(worldPosition);

	float dotAngle = acos(dot(worldPosition, srt.pConsts->m_fogColorsVector.xyz));
	float fColorBlendFactor =
	    saturate((dotAngle - srt.pConsts->m_fogHeightRangeInfo.y) * srt.pConsts->m_fogSecondColorInfo.w);
	fColorBlendFactor = smoothstep(0.0, 1.0, fColorBlendFactor);
	float3 fogColor = lerp(srt.pConsts->m_fogColorInfo.rgb, srt.pConsts->m_fogSecondColorInfo.rgb, fColorBlendFactor);

	if (depthBufferZ < 1.f) // not sky
	{
		float fFogDistanceScalar =
		    max(0.0, viewSpaceZ * srt.pConsts->m_fogRangeInfo.x + srt.pConsts->m_fogRangeInfo.y) *
		    max(0.0, worldPositionY * srt.pConsts->m_fogRangeInfo.z + srt.pConsts->m_fogRangeInfo.w);
		float fFogHeightScalar =
		    max(0.0, worldPositionY * srt.pConsts->m_fogHeightRangeInfo.z + srt.pConsts->m_fogHeightRangeInfo.w);

		float2 fFogFactorPair =
		    float2(exp2(-fFogDistanceScalar * fFogDistanceScalar), exp(-fFogHeightScalar * fFogHeightScalar));
		fFogFactorPair = fFogFactorPair * srt.pConsts->m_fogDensityInfo.xz + srt.pConsts->m_fogDensityInfo.yw;
		fFogFactorPair = 1 - fFogFactorPair;

		float fFogFactor = max(fFogFactorPair.x, max(fFogFactorPair.y, srt.pConsts->m_fogAmbientColorInfo.r));
		outColor.rgba = float4(fogColor * fFogFactor, fFogFactor);
	}
	else // sky
	{
		dotAngle = acos(dot(worldPosition, srt.pConsts->m_skyFogColorsVector.xyz));
		fColorBlendFactor =
		    saturate((dotAngle - srt.pConsts->m_skyFogDensityInfo.w) * srt.pConsts->m_skyFogSecondColorInfo.w);
		fColorBlendFactor = smoothstep(0.0, 1.0, fColorBlendFactor);
		float3 skyFogColor =
		    lerp(srt.pConsts->m_skyFogColorInfo.rgb, srt.pConsts->m_skyFogSecondColorInfo.rgb, fColorBlendFactor);

		float fFogHeightScalar =
		    saturate(worldPositionY * srt.pConsts->m_skyFogRangeInfo.z + srt.pConsts->m_skyFogRangeInfo.w);

		float fHeightAlpha = fFogHeightScalar;

		float finalAlpha = 1 - (srt.pConsts->m_skyFogDensityInfo.y * fHeightAlpha + (1 - fHeightAlpha));
		float3 finalFogAndAlpha = (skyFogColor * srt.pConsts->m_skyFogDensityInfo.x * fHeightAlpha);

		outColor.rgba = float4(finalFogAndAlpha, finalAlpha);

		outColor.rgb += fogColor * srt.pConsts->m_fogAmbientColorInfo.r;
		outColor.a += srt.pConsts->m_fogAmbientColorInfo.r;
	}

	outColor.rgb = max(outColor.rgb, 0.f);
	outColor.a = saturate(outColor.a);
	return outColor;
}

float4 PS_ApplyTwoColoredFogNoiseBlend(PS_PosTex input, PostFogSrt srt : S_SRT_DATA) : SV_Target
{
	float4 outColor;

	float depthBufferZ = srt.pTexs->tx_depth.Sample(srt.pSamplers->sSamplerPoint, input.Tex).x;
	float viewSpaceZ = srt.pConsts->m_screenToViewParams.w / (depthBufferZ - srt.pConsts->m_screenToViewParams.z);
	float2 screenSpaceXy = input.Tex * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float4 viewSpacePosition =
	    float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0);
	float worldPositionX = dot(srt.pConsts->m_viewToWorldMat[0], viewSpacePosition);
	float worldPositionY = dot(srt.pConsts->m_viewToWorldMat[1], viewSpacePosition);
	float worldPositionZ = dot(srt.pConsts->m_viewToWorldMat[2], viewSpacePosition);
	float3 worldPosition = float3(worldPositionX, worldPositionY, worldPositionZ);

	worldPosition -= srt.pConsts->m_cameraPosition.xyz;
	worldPosition = normalize(worldPosition);

	float dotAngle = acos(dot(worldPosition, srt.pConsts->m_fogColorsVector.xyz));
	float fColorBlendFactor =
	    saturate((dotAngle - srt.pConsts->m_fogHeightRangeInfo.y) * srt.pConsts->m_fogSecondColorInfo.w);
	fColorBlendFactor = smoothstep(0.0, 1.0, fColorBlendFactor);
	float3 fogColor = lerp(srt.pConsts->m_fogColorInfo.rgb, srt.pConsts->m_fogSecondColorInfo.rgb, fColorBlendFactor);
	float fogNoiseIntensity = srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerLinear, input.Tex).x;

	if (depthBufferZ < 1.f) // not sky
	{
		float fFogDistanceScalar =
		    max(0.0, viewSpaceZ * srt.pConsts->m_fogRangeInfo.x + srt.pConsts->m_fogRangeInfo.y) *
		    max(0.0, worldPositionY * srt.pConsts->m_fogRangeInfo.z + srt.pConsts->m_fogRangeInfo.w);
		float fFogHeightScalar =
		    max(0.0, worldPositionY * srt.pConsts->m_fogHeightRangeInfo.z + srt.pConsts->m_fogHeightRangeInfo.w);

		float2 fFogFactorPair =
		    float2(exp2(-fFogDistanceScalar * fFogDistanceScalar), exp(-fFogHeightScalar * fFogHeightScalar));
		fFogFactorPair = fFogFactorPair * srt.pConsts->m_fogDensityInfo.xz + srt.pConsts->m_fogDensityInfo.yw;
		fFogFactorPair = 1 - fFogFactorPair;

		float fFogFactor = max(fFogFactorPair.x, max(fFogFactorPair.y, srt.pConsts->m_fogAmbientColorInfo.r));
		outColor.rgba = float4(fogColor * fogNoiseIntensity * fFogFactor, fFogFactor);
	}
	else // sky
	{
		dotAngle = acos(dot(worldPosition, srt.pConsts->m_skyFogColorsVector.xyz));
		fColorBlendFactor =
		    saturate((dotAngle - srt.pConsts->m_skyFogDensityInfo.w) * srt.pConsts->m_skyFogSecondColorInfo.w);
		fColorBlendFactor = smoothstep(0.0, 1.0, fColorBlendFactor);
		float3 skyFogColor =
		    lerp(srt.pConsts->m_skyFogColorInfo.rgb, srt.pConsts->m_skyFogSecondColorInfo.rgb, fColorBlendFactor);

		float fFogHeightScalar =
		    saturate(worldPositionY * srt.pConsts->m_skyFogRangeInfo.z + srt.pConsts->m_skyFogRangeInfo.w);

		float fHeightAlpha = fFogHeightScalar;

		float finalAlpha = 1 - (srt.pConsts->m_skyFogDensityInfo.y * fHeightAlpha + (1 - fHeightAlpha));
		float3 finalFogAndAlpha = (skyFogColor * srt.pConsts->m_skyFogDensityInfo.x * fHeightAlpha);

		outColor.rgba = float4(finalFogAndAlpha, finalAlpha);

		outColor.rgb += fogColor * fogNoiseIntensity * srt.pConsts->m_fogAmbientColorInfo.r;
		outColor.a += srt.pConsts->m_fogAmbientColorInfo.r;
	}

	outColor.rgb = max(outColor.rgb, 0.f);
	outColor.a = saturate(outColor.a);
	return outColor;
}

float4 PS_ApplyTwoColoredFogBlendWithNoFogZone(PS_PosTex input, PostFogSrt srt : S_SRT_DATA) : SV_Target
{
	float4 outColor;

	float depthBufferZ = srt.pTexs->tx_depth.Sample(srt.pSamplers->sSamplerPoint, input.Tex).x;
	float viewSpaceZ = srt.pConsts->m_screenToViewParams.w / (depthBufferZ - srt.pConsts->m_screenToViewParams.z);
	float2 screenSpaceXy = input.Tex * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float4 viewSpacePosition =
	    float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0);
	float worldPositionX = dot(srt.pConsts->m_viewToWorldMat[0], viewSpacePosition);
	float worldPositionY = dot(srt.pConsts->m_viewToWorldMat[1], viewSpacePosition);
	float worldPositionZ = dot(srt.pConsts->m_viewToWorldMat[2], viewSpacePosition);
	float3 worldPosition = float3(worldPositionX, worldPositionY, worldPositionZ);

	worldPosition -= srt.pConsts->m_cameraPosition.xyz;
	worldPosition = normalize(worldPosition);

	float dotAngle = acos(dot(worldPosition, srt.pConsts->m_fogColorsVector.xyz));
	float fColorBlendFactor =
	    saturate((dotAngle - srt.pConsts->m_fogHeightRangeInfo.y) * srt.pConsts->m_fogSecondColorInfo.w);
	fColorBlendFactor = smoothstep(0.0, 1.0, fColorBlendFactor);
	float3 fogColor = lerp(srt.pConsts->m_fogColorInfo.rgb, srt.pConsts->m_fogSecondColorInfo.rgb, fColorBlendFactor);

	if (depthBufferZ < 1.f) // not sky
	{
		float noFogZoneThickness = srt.pTexs->tx_nofog.Sample(srt.pSamplers->sSamplerPoint, input.Tex).a;
		float effectiveViewZ = viewSpaceZ - noFogZoneThickness;
		float groundFogScaler = -noFogZoneThickness / viewSpaceZ + 1.0;
		groundFogScaler *= groundFogScaler;

		float fFogDistanceScalar =
		    max(0.0, effectiveViewZ * srt.pConsts->m_fogRangeInfo.x + srt.pConsts->m_fogRangeInfo.y) *
		    max(0.0, worldPositionY * srt.pConsts->m_fogRangeInfo.z + srt.pConsts->m_fogRangeInfo.w);
		float fFogHeightScalar =
		    max(0.0, worldPositionY * srt.pConsts->m_fogHeightRangeInfo.z + srt.pConsts->m_fogHeightRangeInfo.w);

		float2 fFogFactorPair =
		    float2(exp2(-fFogDistanceScalar * fFogDistanceScalar), exp(-fFogHeightScalar * fFogHeightScalar));
		fFogFactorPair = fFogFactorPair * srt.pConsts->m_fogDensityInfo.xz + srt.pConsts->m_fogDensityInfo.yw;
		fFogFactorPair = 1 - fFogFactorPair;

		float fFogFactor =
		    max(fFogFactorPair.x, max(fFogFactorPair.y, srt.pConsts->m_fogAmbientColorInfo.r) * groundFogScaler);
		outColor.rgba = float4(fogColor * fFogFactor, fFogFactor);
	}
	else // sky
	{
		dotAngle = acos(dot(worldPosition, srt.pConsts->m_skyFogColorsVector.xyz));
		fColorBlendFactor =
		    saturate((dotAngle - srt.pConsts->m_skyFogDensityInfo.w) * srt.pConsts->m_skyFogSecondColorInfo.w);
		fColorBlendFactor = smoothstep(0.0, 1.0, fColorBlendFactor);
		float3 skyFogColor =
		    lerp(srt.pConsts->m_skyFogColorInfo.rgb, srt.pConsts->m_skyFogSecondColorInfo.rgb, fColorBlendFactor);

		float fFogHeightScalar =
		    saturate(worldPositionY * srt.pConsts->m_skyFogRangeInfo.z + srt.pConsts->m_skyFogRangeInfo.w);

		float fHeightAlpha = fFogHeightScalar;

		float finalAlpha = 1 - (srt.pConsts->m_skyFogDensityInfo.y * fHeightAlpha + (1 - fHeightAlpha));
		float3 finalFogAndAlpha = (skyFogColor * srt.pConsts->m_skyFogDensityInfo.x * fHeightAlpha);

		outColor.rgba = float4(finalFogAndAlpha, finalAlpha);

		outColor.rgb += fogColor * srt.pConsts->m_fogAmbientColorInfo.r;
		outColor.a += srt.pConsts->m_fogAmbientColorInfo.r;
	}

	outColor.rgb = max(outColor.rgb, 0.f);
	outColor.a = saturate(outColor.a);
	return outColor;
}

float4 PS_ApplyTwoColoredFogNoiseBlendWithNoFogZone(PS_PosTex input, PostFogSrt srt : S_SRT_DATA) : SV_Target
{
	float4 outColor;

	float depthBufferZ = srt.pTexs->tx_depth.Sample(srt.pSamplers->sSamplerPoint, input.Tex).x;
	float viewSpaceZ = srt.pConsts->m_screenToViewParams.w / (depthBufferZ - srt.pConsts->m_screenToViewParams.z);
	float2 screenSpaceXy = input.Tex * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float4 viewSpacePosition =
	    float4(screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ, viewSpaceZ, 1.0);
	float worldPositionX = dot(srt.pConsts->m_viewToWorldMat[0], viewSpacePosition);
	float worldPositionY = dot(srt.pConsts->m_viewToWorldMat[1], viewSpacePosition);
	float worldPositionZ = dot(srt.pConsts->m_viewToWorldMat[2], viewSpacePosition);
	float3 worldPosition = float3(worldPositionX, worldPositionY, worldPositionZ);

	worldPosition -= srt.pConsts->m_cameraPosition.xyz;
	worldPosition = normalize(worldPosition);

	float dotAngle = acos(dot(worldPosition, srt.pConsts->m_fogColorsVector.xyz));
	float fColorBlendFactor =
	    saturate((dotAngle - srt.pConsts->m_fogHeightRangeInfo.y) * srt.pConsts->m_fogSecondColorInfo.w);
	fColorBlendFactor = smoothstep(0.0, 1.0, fColorBlendFactor);
	float3 fogColor = lerp(srt.pConsts->m_fogColorInfo.rgb, srt.pConsts->m_fogSecondColorInfo.rgb, fColorBlendFactor);
	float fogNoiseIntensity = srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerLinear, input.Tex).x;

	if (depthBufferZ < 1.f) // not sky
	{
		float noFogZoneThickness = srt.pTexs->tx_nofog.Sample(srt.pSamplers->sSamplerPoint, input.Tex).a;
		float effectiveViewZ = viewSpaceZ - noFogZoneThickness;
		float groundFogScaler = -noFogZoneThickness / viewSpaceZ + 1.0;
		groundFogScaler *= groundFogScaler;

		float fFogDistanceScalar =
		    max(0.0, effectiveViewZ * srt.pConsts->m_fogRangeInfo.x + srt.pConsts->m_fogRangeInfo.y) *
		    max(0.0, worldPositionY * srt.pConsts->m_fogRangeInfo.z + srt.pConsts->m_fogRangeInfo.w);
		float fFogHeightScalar =
		    max(0.0, worldPositionY * srt.pConsts->m_fogHeightRangeInfo.z + srt.pConsts->m_fogHeightRangeInfo.w);

		float2 fFogFactorPair =
		    float2(exp2(-fFogDistanceScalar * fFogDistanceScalar), exp(-fFogHeightScalar * fFogHeightScalar));
		fFogFactorPair = fFogFactorPair * srt.pConsts->m_fogDensityInfo.xz + srt.pConsts->m_fogDensityInfo.yw;
		fFogFactorPair = 1 - fFogFactorPair;

		float fFogFactor =
		    max(fFogFactorPair.x, max(fFogFactorPair.y, srt.pConsts->m_fogAmbientColorInfo.r) * groundFogScaler);
		outColor.rgba = float4(fogColor * fogNoiseIntensity * fFogFactor, fFogFactor);
	}
	else // sky
	{
		dotAngle = acos(dot(worldPosition, srt.pConsts->m_skyFogColorsVector.xyz));
		fColorBlendFactor =
		    saturate((dotAngle - srt.pConsts->m_skyFogDensityInfo.w) * srt.pConsts->m_skyFogSecondColorInfo.w);
		fColorBlendFactor = smoothstep(0.0, 1.0, fColorBlendFactor);
		float3 skyFogColor =
		    lerp(srt.pConsts->m_skyFogColorInfo.rgb, srt.pConsts->m_skyFogSecondColorInfo.rgb, fColorBlendFactor);

		float fFogHeightScalar =
		    saturate(worldPositionY * srt.pConsts->m_skyFogRangeInfo.z + srt.pConsts->m_skyFogRangeInfo.w);

		float fHeightAlpha = fFogHeightScalar;

		float finalAlpha = 1 - (srt.pConsts->m_skyFogDensityInfo.y * fHeightAlpha + (1 - fHeightAlpha));
		float3 finalFogAndAlpha = (skyFogColor * srt.pConsts->m_skyFogDensityInfo.x * fHeightAlpha);

		outColor.rgba = float4(finalFogAndAlpha, finalAlpha);

		outColor.rgb += fogColor * fogNoiseIntensity * srt.pConsts->m_fogAmbientColorInfo.r;
		outColor.a += srt.pConsts->m_fogAmbientColorInfo.r;
	}

	outColor.rgb = max(outColor.rgb, 0.f);
	outColor.a = saturate(outColor.a);
	return outColor;
}

// =====================================================================================================================

struct FogNoiseConst
{
	float4 m_screenToViewParams;
	float4 m_viewToWorldMat[3];
	float4 m_viewTo3dFogNoiseTex[3];
	float4 m_fogNoiseScaleInfo;
	float4 m_fogNoiseOffsetInfo;
	float4 m_fogHeightRangeInfo;
	float4 m_fogColorInfo;
};

struct FogNoiseTextures 
{
	Texture2D<float4> tx_depth;
	Texture2D<float4> tx_noise;
};

struct FogNoiseSamplers
{
	SamplerState sSamplerPoint;
	SamplerState sSamplerMipLinear;
};

struct FogNoiseSrt
{
	FogNoiseConst *pConsts;
	FogNoiseTextures *pTexs;	
	FogNoiseSamplers *pSamplers;
};

float4 PS_ScreenSpaceFogNoiseGenerater(PS_PosTex input, FogNoiseSrt srt : S_SRT_DATA) : SV_Target
{
	float depthBufferZ = srt.pTexs->tx_depth.Sample(srt.pSamplers->sSamplerPoint, input.Tex).x;
	float viewSpaceZ =
	    srt.pConsts->m_screenToViewParams.w / (depthBufferZ - srt.pConsts->m_screenToViewParams.z);
	float2 screenSpaceXy = input.Tex * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float2 viewSpaceXY = screenSpaceXy * srt.pConsts->m_screenToViewParams.xy * viewSpaceZ;

	float3 texCoord3d;
	texCoord3d.x = dot(srt.pConsts->m_viewTo3dFogNoiseTex[0], float4(viewSpaceXY, viewSpaceZ, 1.0));
	texCoord3d.y = dot(srt.pConsts->m_viewTo3dFogNoiseTex[1], float4(viewSpaceXY, viewSpaceZ, 1.0));
	texCoord3d.z = dot(srt.pConsts->m_viewTo3dFogNoiseTex[2], float4(viewSpaceXY, viewSpaceZ, 1.0));

	float3 cameraCoordNoise;
	cameraCoordNoise.x = dot(srt.pConsts->m_viewTo3dFogNoiseTex[0], float4(0.0f, 0.0f, 0.0f, 1.0));
	cameraCoordNoise.y = dot(srt.pConsts->m_viewTo3dFogNoiseTex[1], float4(0.0f, 0.0f, 0.0f, 1.0));
	cameraCoordNoise.z = dot(srt.pConsts->m_viewTo3dFogNoiseTex[2], float4(0.0f, 0.0f, 0.0f, 1.0));

	float3 rayVec = texCoord3d - cameraCoordNoise;
	float rayLength = length(rayVec);

	float clampedRayLength = min(2.0f, rayLength);
	rayVec = normalize(rayVec);

	float stepLength = clampedRayLength / 10.0f;

	float3 stepTexCoord3d = rayVec * stepLength;

	texCoord3d = cameraCoordNoise;

	float noiseIntensity = srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.zx).x *
	                       srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.xy).x *
	                       srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.yz).x;

	float3 worldPosition;
	worldPosition.x = dot(srt.pConsts->m_viewToWorldMat[0], float4(viewSpaceXY, viewSpaceZ, 1.f));
	worldPosition.y = dot(srt.pConsts->m_viewToWorldMat[1], float4(viewSpaceXY, viewSpaceZ, 1.f));
	worldPosition.z = dot(srt.pConsts->m_viewToWorldMat[2], float4(viewSpaceXY, viewSpaceZ, 1.f));

	float3 hiNoiseTexCoord3d =
	    worldPosition * srt.pConsts->m_fogNoiseScaleInfo.xyz + srt.pConsts->m_fogNoiseOffsetInfo.xyz;
	float nearNoiseIntensity = srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, hiNoiseTexCoord3d.zx).x *
	                           srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, hiNoiseTexCoord3d.xy).x *
	                           srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, hiNoiseTexCoord3d.yz).x;

	texCoord3d += stepTexCoord3d;
	noiseIntensity += srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.zx).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.xy).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.yz).x;

	texCoord3d += stepTexCoord3d;
	noiseIntensity += srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.zx).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.xy).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.yz).x;

	texCoord3d += stepTexCoord3d;
	noiseIntensity += srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.zx).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.xy).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.yz).x;

	texCoord3d += stepTexCoord3d;
	noiseIntensity += srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.zx).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.xy).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.yz).x;

	texCoord3d += stepTexCoord3d;
	noiseIntensity += srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.zx).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.xy).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.yz).x;

	texCoord3d += stepTexCoord3d;
	noiseIntensity += srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.zx).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.xy).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.yz).x;

	texCoord3d += stepTexCoord3d;
	noiseIntensity += srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.zx).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.xy).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.yz).x;

	texCoord3d += stepTexCoord3d;
	noiseIntensity += srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.zx).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.xy).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.yz).x;

	texCoord3d += stepTexCoord3d;
	noiseIntensity += srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.zx).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.xy).x *
	                  srt.pTexs->tx_noise.Sample(srt.pSamplers->sSamplerMipLinear, texCoord3d.yz).x;

	noiseIntensity = lerp(nearNoiseIntensity, noiseIntensity / 10.0,
	                      saturate(viewSpaceZ / srt.pConsts->m_fogHeightRangeInfo.x) * 0.5 + 0.3);

	return 1.0 + noiseIntensity * srt.pConsts->m_fogColorInfo.w;
}
