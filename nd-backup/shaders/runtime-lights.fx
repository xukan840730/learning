//--------------------------------------------------------------------------------------
// File: runtime-lights.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "global-funcs.fxi"

float4 PS_ShadowFalloffTextureGenerator(PS_PosTex input) : SV_Target
{
	float4 result;
	float2 pixelCoord = input.Tex * float2(256.0f, 8.0f);

	if (pixelCoord.y < 2.0)
	{
		result = float4(1.f, 1.f, 1.f, 1.f);
	}
	else
	{
		float pow_index = pixelCoord.y < 4.0 ? 1.0 : (pixelCoord.y < 6.0 ? 2.0 : 3.0);
		float stepRange = 204.0f;
		float maxRange = pow(1024.0f / stepRange, pow_index);
		float rangeValue = pow(pixelCoord.x * 4.0 / stepRange, pow_index);
		result = pixelCoord.x > 255.0 ? 0.f : (1.0f / (1.0f + rangeValue) - 1.0f / (maxRange + 1)) * (1.0f + 1.0f / maxRange);
	}
	
	return result;
}
