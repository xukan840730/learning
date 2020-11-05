//--------------------------------------------------------------------------------------
// File: high-contrast-mode-lighting.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "packing.fxi"
#include "high-contrast-mode-post-utils.fxi"
#include "stencil-util.fxi"

struct HCMLightingConst
{
	float4x4	m_ScreenToAltWorldMat;
	float4		m_cameraPositionWS;		// Current camera position in world space
	int			m_highContrastMode;
	float		m_highContrastModeBaseDepthRange;
	float		m_highContrastModeGroundBrightness;
	float		m_highContrastModeGroundRange;
	float		m_highContrastModeBasecolorLevel;
	float		m_highContrastModeBasecolorGrey;
	float		m_highContrastModeEnemyBoost;
	uint		m_HighContrastModeDebuggingTypes : 1;

	RWTexture2D<float4>		primaryFloat;
	Texture2D<uint4>		gBuffer0;
	Texture2D<uint4>		gBuffer1;
	Texture2D<uint>			m_materialMaskBuffer;
	Texture2D<uint>			stencilBuffer;
	Texture2D<float4>		ssaoBuffer;
};

struct HCMLightingSrt
{
	HCMLightingConst	*pLightingConsts;
	HCMSharedConst		*pSharedConsts;
};

// High contrast mode is specifically for people experiencing difficulties in vision.
// In a nutshell, there will be way less detail in the game and the contrast is way higher so that those players can identify objects in the game.
[numthreads(8, 8, 1)]
void CS_HighContrastModeLighting(int3 localDispatchThreadId : SV_DispatchThreadId, HCMLightingSrt srt : S_SRT_DATA) : SV_Target
{
	const int2 dispatchThreadId = localDispatchThreadId.xy;
	const float2 texCoord = (dispatchThreadId.xy + 0.5f) * srt.pSharedConsts->m_viewSize.zw;

	// get the resolution of the target buffer
	const int2 viewSize = int2((int)srt.pSharedConsts->m_viewSize.x , (int)srt.pSharedConsts->m_viewSize.y);

	// skip earlier to make sure that we don't pollute memory outside what is allocated.
	if (any(dispatchThreadId >= viewSize))
		return;

	// unpack gbuffer0, 1
	const uint4 gBuffer0 = srt.pLightingConsts->gBuffer0.SampleLevel(srt.pSharedConsts->pointClampSampler, texCoord, 0);
	const uint4 gBuffer1 = srt.pLightingConsts->gBuffer1.SampleLevel(srt.pSharedConsts->pointClampSampler, texCoord, 0);
	BrdfParameters brdfParameters;
	InitBrdfParameters(brdfParameters);
	Setup setup;
	UnpackGBuffer(gBuffer0, gBuffer1, brdfParameters, setup);
	const float3 baseColor = pow(brdfParameters.baseColor, 1 / 2.2);

	const uint materialMaskSample = srt.pLightingConsts->m_materialMaskBuffer.Load(int3(texCoord, 0));
	UnpackMaterialMask(materialMaskSample, gBuffer1, setup.materialMask);

	// get stencil
	const uint stencil = srt.pLightingConsts->stencilBuffer.SampleLevel(srt.pSharedConsts->pointClampSampler, texCoord, 0);
	const bool isWater = IsStencilWater(stencil);
	const uint hcmType = GetStencilHCMType(stencil);

	float3 finalColor = baseColor;

	// object color tag debug mode
	const bool bHighContrastModeDebuggingType = srt.pLightingConsts->m_HighContrastModeDebuggingTypes;
	if (bHighContrastModeDebuggingType)
	{
		float3 debugColor = finalColor.g * 0.2;
		if (isWater)
			debugColor = float3(0.4f, 0.65f, 0.82f) * 0.6;
		if (STENCIL_HCM_ENEMY == hcmType)
			debugColor = float3(0.2, 0.2, 2.0);
		if (STENCIL_HCM_HERO == hcmType)
			debugColor = float3(2.0, 0.2, 0.2);
		if (STENCIL_HCM_BACKGROUND == hcmType)
			debugColor = float3(0.2f, 0.5f, 0.2f);
		else if (STENCIL_HCM_INTERACTIVE == hcmType)
			debugColor = float3(0.8, 0.8, 0.8);
		else if (STENCIL_HCM_PICKUP == hcmType)
			debugColor = float3(1.0, 0.84, 0.0);
		else if (STENCIL_HCM_BACKGROUNDNOOUTLINE == hcmType)
			debugColor = float3(0.0, 0.8, 0.8);
		else if (STENCIL_HCM_BASECOLOR == hcmType)
			debugColor = float3(1.0f, 0.0f, 0.0f);

		finalColor = debugColor;
	}
	else if (isWater)
	{
		finalColor = finalColor * float3(0.4f, 0.65f, 0.72f) * 1.2;
	}
	else
	{
		// get depth
		const float2 depthParams = srt.pSharedConsts->m_depthParams;
		const float depthZ = srt.pSharedConsts->depthBuffer.SampleLevel(srt.pSharedConsts->pointClampSampler, texCoord, 0);
		const float depthLinear = viewz_from_devicez(depthZ, depthParams);

		// get ao
		float ssao = srt.pLightingConsts->ssaoBuffer.SampleLevel(srt.pSharedConsts->pointClampSampler, texCoord, 0).r;
		ssao = lerp(ssao, 1.0, Luminance(brdfParameters.baseColor));
		float ao = min(ssao, brdfParameters.ao);

		// First stage - AO
		const float skyMask = saturate(depthLinear - 1000);
		ao = min((ao + skyMask), ssao);
		ao = ao * ao;

		// create outline
		const float3 edgeHighlight = HighContrastModeOutline( hcmType , depthLinear , texCoord , baseColor , srt.pSharedConsts );

		// shade high contrast mode pixels
		{
			const float baseDepthRange		= srt.pLightingConsts->m_highContrastModeBaseDepthRange;
			const float aoDepthRange		= srt.pSharedConsts->m_highContrastModeAODepthRange;
			const float groundBrightness	= srt.pLightingConsts->m_highContrastModeGroundBrightness;
			const float groundRange			= srt.pLightingConsts->m_highContrastModeGroundRange;
			const float basecolorLevel		= srt.pLightingConsts->m_highContrastModeBasecolorLevel;
			finalColor = HighContrastModeLighting(	hcmType , srt.pLightingConsts->m_highContrastMode , setup , brdfParameters , depthLinear , edgeHighlight , ao ,
													aoDepthRange , groundBrightness, groundRange, basecolorLevel, baseDepthRange );
		}
	}

	if(hcmType == STENCIL_HCM_BASECOLOR)
	{
		const float baseColorGreyWeight = srt.pLightingConsts->m_highContrastModeBasecolorGrey;
		const float baseColorGrey = 0.2f;
		finalColor = lerp(finalColor, float3(baseColorGrey, baseColorGrey, baseColorGrey), baseColorGreyWeight);
	}

	if(hcmType == STENCIL_HCM_ENEMY)
	{
		finalColor *= srt.pLightingConsts->m_highContrastModeEnemyBoost;
	}

	srt.pLightingConsts->primaryFloat[int2(dispatchThreadId.xy)] = float4(finalColor, 0.0f);
}