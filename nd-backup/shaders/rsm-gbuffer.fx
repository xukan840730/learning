//--------------------------------------------------------------------------------------
// File: rsm-gbuffer.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#define ND_PSSL

#include "packing.fxi"
#include "texture-table.fxi"

// use this to avoid the player character being included
static const float NEAR_DIST_CLIP = 0.5f;

// We clamp pixel area to prevent issues that arise from using gbuffer as source data
static const float MAX_PIXEL_AREA = 0.005f;

struct SamplerTable
{
	SamplerState				g_linearSampler;
	SamplerState				g_pointSampler;
	SamplerComparisonState		g_shadowSampler;
	SamplerState				g_linearClampSampler;
	SamplerState				g_linearClampUSampler;
	SamplerState				g_linearClampVSampler;
};

float ComputeLightFalloff(float4 falloffParams, float cubicFalloff, float lightDistSq, float lightDist)
{
	// Taken from runtime-lights.fxi
	float lightFalloffValue = 1.f / (1.f + falloffParams.x*lightDist + (falloffParams.y + cubicFalloff*lightDist) * lightDistSq);
	lightFalloffValue = saturate(lightFalloffValue * falloffParams.z + falloffParams.w);
	return lightFalloffValue;
}

//--------------------------------------------------------------------------------------

struct RsmGBufferSrt
{
	RW_Texture2D<float4> m_outColor;
	RW_Texture2D<float4> m_outNormal;
	RW_Texture2D<float> m_outDepth;
	Texture2D<float4> m_rsmGobo;

	Texture2D<uint4> m_gbuffer0;
	Texture2D<uint4> m_gbuffer1;
	LocalShadowTable* m_pLocalShadowTextures;
	uint2 m_pad;

	float4x4 m_worldToGBuffer;
	float4x4 m_flashlightScreenToWorld;
	float4 m_worldSpaceViewZ;
	float3 m_flashlightColor;
	float3 m_flashlightConePos;
	float3 m_flashlightDir;
	float3 m_altWorldOrigin;
	float2 m_invOutDims;
	float2 m_gbufferDims;
	float m_flashlightRadius;
	float m_flashlightAngleRadians;
	uint m_localShadowTextureIdx;
	uint2 m_outputOffsets;

	SamplerTable *m_pSamplers;
};

[numthreads(64,1,1)]
void Cs_GenerateRsmFromGBuffer(uint2 dispatchThreadID: SV_DispatchThreadID, RsmGBufferSrt* pSrt : S_SRT_DATA) : SV_Target
{
	uint2 xy = dispatchThreadID;

	// We need to modify the dispatch XY to make it so that ddx/ddy reads the correct values from the adjacent lanes
	xy.x = (xy.x / 2) + (xy.x % 2);
	xy.y = xy.y * 2;

	uint2 sampleXY;
	sampleXY.x = QuadSwizzle(xy.x, 0, 1, 0, 1);
	sampleXY.y = xy.y  + ((dispatchThreadID.x / 2) % 2);

	float2 uv = (sampleXY) * pSrt->m_invOutDims;
	float spotShadowDepth = pSrt->m_pLocalShadowTextures->SampleLevel(pSrt->m_pSamplers->g_pointSampler, uv, 0.0f, pSrt->m_localShadowTextureIdx);
	float4 shadowSpaceCoord = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), spotShadowDepth, 1.0f);

	float4 worldSpaceCoord = mul(pSrt->m_flashlightScreenToWorld, shadowSpaceCoord);
	float3 worldSpacePosHS = worldSpaceCoord.xyz / worldSpaceCoord.w;
	float3 worldSpacePosWS = worldSpacePosHS + pSrt->m_altWorldOrigin;

	float4 gbufferSpaceCoord = mul(pSrt->m_worldToGBuffer, float4(worldSpacePosWS, 1.0f));
	float3 gbufferNDC = gbufferSpaceCoord.xyz / gbufferSpaceCoord.w;

	float2 gbufferUV = (gbufferNDC.xy * float2(0.5f)) + float2(0.5f);
	gbufferUV.y = 1.0f - gbufferUV.y;

	// get samples from gbuffer
	uint2 gbufferXY = (uint2)(gbufferUV * pSrt->m_gbufferDims);
	uint4 packedColor0 = pSrt->m_gbuffer0.Load(uint3(gbufferXY, 0));
	uint4 packedColor1 = pSrt->m_gbuffer1.Load(uint3(gbufferXY, 0));

	float3 flashlightToTexel = worldSpacePosWS - pSrt->m_flashlightConePos;
	float projectedDist = dot(flashlightToTexel, pSrt->m_flashlightDir);
	float texelAngle = dot(normalize(flashlightToTexel), pSrt->m_flashlightDir);

	float3 deltaWorldX = ddx_fine(worldSpacePosWS);
	float3 deltaWorldY = ddy_fine(worldSpacePosWS);
	float pixelArea = length(cross(deltaWorldX, deltaWorldY));

	if(projectedDist <= pSrt->m_flashlightRadius && texelAngle >= pSrt->m_flashlightAngleRadians && pixelArea <= MAX_PIXEL_AREA)
	{
		float2 encodedNormal = UnpackNormal(packedColor0.z, packedColor0.w);
		float3 normalWS = DecodeNormal(encodedNormal);

		// unpack base color
		float3 unpackedColor = UnpackUInt2(packedColor0.xy).xyz;
		float3 linearColor = saturate(pow(unpackedColor, 2.2));

		// unpacked metallic
		float metallic = 0.0f;
		const uint metallicOrTranslucencyBits = (packedColor1.y >> 8) & 0x1F;
		float metallicOrTranslucency = (float)metallicOrTranslucencyBits / 31.f;
		const uint metallicSelector = packedColor1.y & MASK_BIT_SPECIAL_METALLIC_SELECTOR;
		if(metallicSelector)
		{
			metallic = metallicOrTranslucency;
		}
		linearColor = lerp(linearColor, float3(0.0f), metallic);

		// compute falloff
		float3 vLightDir = pSrt->m_flashlightConePos - worldSpacePosWS;
		float falloff = 1.f / (kEpsilon + dot(vLightDir, vLightDir));
		float fadeoutFactor = dot(-pSrt->m_worldSpaceViewZ.xyz, normalize(vLightDir));
		float3 gobo = pSrt->m_rsmGobo.SampleLevel(pSrt->m_pSamplers->g_linearSampler, float2(0.5, 0.5), 15.f).rgb;

		float3 fadeoutMask = gobo * falloff * saturate(dot(normalWS, normalize(vLightDir)));
		linearColor *= fadeoutMask;
		linearColor *= pSrt->m_flashlightColor;

		pSrt->m_outColor[sampleXY + pSrt->m_outputOffsets] = float4(linearColor, pixelArea);
		pSrt->m_outNormal[sampleXY + pSrt->m_outputOffsets] = float4(-normalWS, projectedDist);
		pSrt->m_outDepth[sampleXY + pSrt->m_outputOffsets] = spotShadowDepth;
	}
	else
	{
		pSrt->m_outColor[xy + pSrt->m_outputOffsets] = float4(0.0f);
		pSrt->m_outNormal[xy + pSrt->m_outputOffsets] = float4(0.0f);
		pSrt->m_outDepth[xy + pSrt->m_outputOffsets] = 0.0f;
	}
}