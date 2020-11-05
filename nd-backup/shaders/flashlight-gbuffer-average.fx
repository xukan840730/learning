//--------------------------------------------------------------------------------------
// File: flashlight-gbuffer-average.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "packing.fxi"

// use this to avoid the player character being included
static const float NEAR_DIST_CLIP = 0.75f;

struct SamplerTable
{
	SamplerState				g_linearSampler;
	SamplerState				g_pointSampler;
	SamplerComparisonState		g_shadowSampler;
	SamplerState				g_linearClampSampler;
	SamplerState				g_linearClampUSampler;
	SamplerState				g_linearClampVSampler;
};

uint ReduceSum(uint value)
{
	uint sum = value;

	sum += LaneSwizzle(sum, 0x1f, 0x00, 0x10);
	sum += LaneSwizzle(sum, 0x1f, 0x00, 0x08);
	sum += LaneSwizzle(sum, 0x1f, 0x00, 0x04);
	sum += LaneSwizzle(sum, 0x1f, 0x00, 0x02);
	sum += LaneSwizzle(sum, 0x1f, 0x00, 0x01);

	uint reduced = ReadLane(sum, 0) + ReadLane(sum, 32);
	return reduced;
}

float ComputeLightFalloff(float4 falloffParams, float cubicFalloff, float lightDistSq, float lightDist)
{
	// Taken from runtime-lights.fxi
	float lightFalloffValue = 1.f / (1.f + falloffParams.x*lightDist + (falloffParams.y + cubicFalloff*lightDist) * lightDistSq);
	lightFalloffValue = saturate(lightFalloffValue * falloffParams.z + falloffParams.w);
	return lightFalloffValue;
}

//--------------------------------------------------------------------------------------

struct FlashlightGBufferTintSrt
{
	RW_RegularBuffer<uint4> m_outAccumQuantizedColor;
	Texture2D<uint4> m_gbuffer0;
	Texture2D<uint4> m_gbuffer1;
	Texture2D<float4> m_primaryDepth;

	float4x4 m_screenToWorld;
	float4 m_flashlightFalloffParams;
	float3 m_flashlightConePos;
	float3 m_flashlightDir;
	float3 m_flashlightColor;
	float2 m_invGBufferDims;
	float m_flashlightRadius;
	float m_flashlightAngleRadians;

	SamplerTable *m_pSamplers;
};

[numthreads(8,8,1)]
void Cs_FlashlightGBufferTint(uint2 dispatchThreadID: SV_DispatchThreadID, FlashlightGBufferTintSrt* pSrt : S_SRT_DATA) : SV_Target
{
	uint2 xy = dispatchThreadID;
	float2 uv = ((float2)xy + float2(0.5f, 0.5f)) * pSrt->m_invGBufferDims;
	float depth = pSrt->m_primaryDepth.Sample(pSrt->m_pSamplers->g_pointSampler, uv, 0).x;

	uv.y = 1.0f - uv.y;
	float4 screenSpaceCoord = float4(uv * float2(2.0f, 2.0f) - float2(1.0f, 1.0f), depth, 1.0f);
	float4 worldSpaceCoord = mul(screenSpaceCoord, pSrt->m_screenToWorld);
	float3 worldSpacePos = worldSpaceCoord.xyz / worldSpaceCoord.w;

	float3 flashlightToTexel = worldSpacePos - pSrt->m_flashlightConePos;
	float projectedDist = dot(flashlightToTexel, pSrt->m_flashlightDir);
	float texelAngle = dot(normalize(flashlightToTexel), pSrt->m_flashlightDir);

	// quantize color
	uint r = 0;
	uint g = 0;
	uint b = 0;
	uint a = 0;

	// only accumulate if within the cone of the flashlight
	if (projectedDist >= NEAR_DIST_CLIP && projectedDist <= pSrt->m_flashlightRadius && texelAngle >= pSrt->m_flashlightAngleRadians)
	{
		// get samples from gbuffer
		uint4 packedColor0 = pSrt->m_gbuffer0.Load(uint3(xy, 0));
		uint4 packedColor1 = pSrt->m_gbuffer1.Load(uint3((uint2)xy, 0));

		// unpacked metallic
		float metallic = 0.0f;
		const uint metallicOrTranslucencyBits = (packedColor1.y >> 8) & 0x1F;
		float metallicOrTranslucency = (float)metallicOrTranslucencyBits / 31.f;
		
		const uint metallicSelector = packedColor1.y & MASK_BIT_SPECIAL_METALLIC_SELECTOR;
		if(metallicSelector)
		{
			metallic = metallicOrTranslucency;
		}

		// unpack base color
		float3 unpackedColor = UnpackUInt2(packedColor0.xy).xyz;
		float3 linearColor = pow(unpackedColor, 2.2);

		// compute falloff (disabled for now, adds like 0.5ms for not much difference)
		// float distSq = dot(flashlightToTexel, flashlightToTexel);
		// float dist = sqrt(distSq);
		// float falloff = ComputeLightFalloff(pSrt->m_flashlightFalloffParams, 0.0f, distSq, dist);
		// linearColor *= falloff;

		// make sure to multiply the base color by the flashlight color to properly simulate the color of the bounced lighting
		linearColor *= pSrt->m_flashlightColor;

		// lerp base color based on metallic
		linearColor = lerp(linearColor, float3(0.0f), metallic);

		uint3 quantizedColor = (uint3)(linearColor * 512.0f);

		r = quantizedColor.x;
		g = quantizedColor.y;
		b = quantizedColor.z;
		a = 512;
	}

	// sum up within the wavefront using lane swizzling
	r = ReduceSum(r);
	g = ReduceSum(g);
	b = ReduceSum(b);
	a = ReduceSum(a);

	// only the first lane will atomically add the waverfront's accumulated color to the buffer
	if (__v_cndmask_b32(0, 1, 1))
	{
		__buffer_atomic_add(r, uint2(0, 0), __get_vsharp(pSrt->m_outAccumQuantizedColor), 0, 0); // r
		__buffer_atomic_add(g, uint2(0, 0), __get_vsharp(pSrt->m_outAccumQuantizedColor), 4, 0); // g
		__buffer_atomic_add(b, uint2(0, 0), __get_vsharp(pSrt->m_outAccumQuantizedColor), 8, 0); // b
		__buffer_atomic_add(a, uint2(0, 0), __get_vsharp(pSrt->m_outAccumQuantizedColor), 12, 0); // a
	}
}

struct ExtralightNormalizeAndSetSrt
{
	RW_RegularBuffer<uint4> m_inAccumQuantizedColor;
	RW_RegularBuffer<float3> m_outColor;
	float m_extralightIntensity;
};

[numthreads(1,1,1)]
void Cs_ExtraLightNormalizeAndSet(uint2 dispatchThreadID: SV_DispatchThreadID, ExtralightNormalizeAndSetSrt* pSrt : S_SRT_DATA) : SV_Target
{
	uint2 xy = dispatchThreadID;

	uint r = pSrt->m_inAccumQuantizedColor[0].x;
	uint g = pSrt->m_inAccumQuantizedColor[0].y;
	uint b = pSrt->m_inAccumQuantizedColor[0].z;
	uint norm = pSrt->m_inAccumQuantizedColor[0].w;

	if (r > 0 && g > 0 && b > 0 && norm > 0)
	{
		float normInv = 1.0f / (float)norm;
		float rOut = (float)r * normInv;
		float gOut = (float)g * normInv;
		float bOut = (float)b * normInv;

		float3 rawColor = float3(rOut, gOut, bOut);
		pSrt->m_outColor[xy] = normalize(rawColor) * pSrt->m_extralightIntensity;
	}
	else
	{
		pSrt->m_outColor[xy] = float3(0.0f, 0.0f, 0.0f);
	}
}

//--------------------------------------------------------------------------------------

struct GBufferInFlashlightConeSrt 
{
	RWTexture2D<float4> m_outDebugColor;
	Texture2D<uint4> m_gbuffer0;
	Texture2D<uint4> m_gbuffer1;
	Texture2D<float4> m_primaryDepth;

	float4x4 m_screenToWorld;
	float3 m_flashlightConePos;
	float3 m_flashlightDir;
	float2 m_invOutBufferDims;
	float m_flashlightRadius;
	float m_flashlightAngleRadians;

	SamplerTable *m_pSamplers;
};

[numthreads(8,8,1)]
void Cs_FlashlightGBufferInFlashlightConeDebug(uint2 dispatchThreadID: SV_DispatchThreadID, GBufferInFlashlightConeSrt* pSrt : S_SRT_DATA) : SV_Target
{
	uint2 xy = dispatchThreadID;
	float2 uv = ((float2)xy + float2(0.5f, 0.5f)) * pSrt->m_invOutBufferDims;

	uint4 packedColor0 = pSrt->m_gbuffer0.Sample(pSrt->m_pSamplers->g_linearSampler, uv, 0);
	uint4 packedColor1 = pSrt->m_gbuffer1.Sample(pSrt->m_pSamplers->g_linearSampler, uv, 0);
	float depth = pSrt->m_primaryDepth.Sample(pSrt->m_pSamplers->g_linearSampler, uv, 0);

	float3 unpackedColor = UnpackUInt2(packedColor0.xy).xyz;
	float3 linearColor = pow(unpackedColor, 2.2);

	float metallic = 0.0f;
	const uint metallicOrTranslucencyBits = (packedColor1.y >> 8) & 0x1F;
	float metallicOrTranslucency = (float)metallicOrTranslucencyBits / 31.f;
	
	const uint metallicSelector = packedColor1.y & MASK_BIT_SPECIAL_METALLIC_SELECTOR;
	if(metallicSelector)
	{
		metallic = metallicOrTranslucency;
	}

	//float2 uv = ((float2)xy + float2(0.5f, 0.5f)) * pSrt->m_invOutBufferDims;
	uv.y = 1.0f - uv.y;

	float4 screenSpaceNDC = float4(uv * float2(2.0f, 2.0f) - float2(1.0f, 1.0f), depth, 1.0f);
	float4 worldSpaceCoord = mul(screenSpaceNDC, pSrt->m_screenToWorld);
	float3 worldSpacePos = worldSpaceCoord.xyz / worldSpaceCoord.w;

	float3 flashlightToTexel = worldSpacePos - pSrt->m_flashlightConePos;
	float projectedDist = dot(flashlightToTexel, pSrt->m_flashlightDir);
	float texelAngle = dot(normalize(flashlightToTexel), pSrt->m_flashlightDir);

	if (projectedDist >= NEAR_DIST_CLIP && projectedDist <= pSrt->m_flashlightRadius && texelAngle >= pSrt->m_flashlightAngleRadians)
	{
		pSrt->m_outDebugColor[xy] = float4(linearColor, 1.0f);
	}
	else
	{
		pSrt->m_outDebugColor[xy] = float4(linearColor * 0.01f, 1.0f);
	}
}