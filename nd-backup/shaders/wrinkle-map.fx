//--------------------------------------------------------------------------------------
// File: wrinkle-map.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

struct PS_PosTex
{
    float4 Pos		: SV_POSITION;
    float2 Tex		: TEXCOORD0;
};

struct SamplerTable
{
	SamplerState				g_linearSampler;
	SamplerState				g_pointSampler;
	SamplerComparisonState		g_shadowSampler;
	SamplerState				g_linearClampSampler;
	SamplerState				g_linearClampUSampler;
	SamplerState				g_linearClampVSampler;
};

struct WrinkleRegions 
{
	float4             m_blendValue[128];
	float4             m_regions[128];
	float4	           m_blendExponents[128];
	Texture2D<float4>  m_textures[128];
};

struct WrinkleSrt
{
	int                m_numWrinkles;
	int                m_mode;         // 0 - blend, 1 - add   per channel
	int                m_index;
	float              m_debugBlendValue;

	float              m_heightFieldStrength;
	float              m_pow1;
	float              m_pow2;
	float              m_pow3;

	RWTexture2D<float4>	m_destBuffer;
	Buffer<ulong>		m_regionTiles; // bitmask per tile of regions affecting it

	float2				m_tileDims;
	float2				m_bufferSizeInv;

	float4             m_regionOffset; // inverse transform per region to other regions
	WrinkleRegions   * m_regions;
	SamplerTable      *m_pSamplers;

	float4             m_blendValue;  // blend per channel (1-blendValue) * prev  + blendValue * this
	float4             m_addValue;    // add per channel prev + blendValue * this
};

float4 Ps_Wrinkles(PS_PosTex input, WrinkleSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float2 uvQuad = float2(input.Tex);
	WrinkleRegions *regions = pSrt->m_regions;

	// Convert the uv of the quad into fullScreen uvs
	float2 uv = (1.0 - uvQuad) * pSrt->m_regionOffset.xy + uvQuad * pSrt->m_regionOffset.zw;

	SamplerState textureSampler = pSrt->m_pSamplers->g_linearClampSampler;

	float4 outColor     = float4(0, 0, 0, 1);
	float4 accumulator  = float4(0,0,0,0);
	float  accAoBlend   = 0;

	// Get the base
	// Note, that the base IS NOT swizzled
	// We are expecting the texture to be a normalmap texture. ie. The red and green channels contain the normal map values
	// This is on contrast with the rest of the wrinkle textures, which ARE swizzled. See below for the 
	float3 base = regions->m_textures[0].SampleLevel(textureSampler, uv, 0).rgb;

	for (int i=1; i < pSrt->m_numWrinkles; i++) {
		float  blendValue    = regions->m_blendValue[i].x;
		float  blendValueSat = saturate(blendValue);
		float4 region     = regions->m_regions[i];
		float4 bexp       = regions->m_blendExponents[i];

		float2 uvRegion = (uv - region.xy) / (region.zw - region.xy);
		
		if (all(uv >= regions->m_regions[i].xy && uv <= regions->m_regions[i].zw) && blendValue > 0)  { // are we inside the region and we are blending

			// Remap the fullscreen uv into the region specific one
			float4 srcColor = regions->m_textures[i].SampleLevel(textureSampler, uvRegion, 0);

			// Procedural blend using quad's vertex coordinates as colors (there is info in uvRegion) to say how to create a gradient
			// The gradient is multiplied by a combination of the normal map to create a 'fade in-out' map that directs the blend value
			// This is all to avoid the common "alpha-blending" look of normal blending
			float uvBilinear = dot(float4((1.0 - uvRegion.x) * (1.0 - uvRegion.y), (uvRegion.x) * (1.0 - uvRegion.y), (1.0 - uvRegion.x) * (uvRegion.y), (uvRegion.x) * (uvRegion.y)), bexp);
			float blend = srcColor.r * pow(blendValue, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));
			float blendSat = srcColor.r * pow(blendValueSat, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));

			{
				// Swizzled color using BC3 compression
				// BC3 compression
				// The channels of the BC3 compressed texture are as follows:
				// RGBA <- (alpha mask, normal map G channel, AO, normal map R channel
				// RGBA = (alpha, normalMap.g, AO.r, normalMap.r)
				{
					accumulator.rg += blend * pSrt->m_heightFieldStrength * (2.0 * ( srcColor.ag - .5));
					accumulator.b  += blendSat * srcColor.b;
					accumulator.a  += blend;
					accAoBlend     += blendSat;
				}
			}
		} 
	}
	accumulator.a = saturate(accumulator.a);
	outColor.rg = lerp(base.xy, saturate(accumulator.rg * float2(.5) + float2(.5)), accumulator.a);
	accAoBlend *= pSrt->m_pow1;
	outColor.b = (1.0 - accAoBlend) * base.z + accumulator.b;
	outColor.a = 1.0;

	return outColor;
}

float4 Ps_WrinklesDiffuse(PS_PosTex input, WrinkleSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float2 uvQuad = float2(input.Tex);
	WrinkleRegions *regions = pSrt->m_regions;

	// Convert the uv of the quad into fullScreen uvs
	float2 uv = (1.0 - uvQuad) * pSrt->m_regionOffset.xy + uvQuad * pSrt->m_regionOffset.zw;

	SamplerState textureSampler = pSrt->m_pSamplers->g_linearClampSampler;

	float4 outColor     = float4(0, 0, 0, 1);
	float4 accumulator  = float4(0,0,0,0);
	float  accAoBlend   = 0;

	// Get the base
	// Note, that the base IS NOT swizzled
	// We are expecting the texture to be a normalmap texture. ie. The red and green channels contain the normal map values
	// This is on contrast with the rest of the wrinkle textures, which ARE swizzled. See below for the 
	float3 base = regions->m_textures[0].SampleLevel(textureSampler, uv, 0).rgb;

	for (int i=1; i < pSrt->m_numWrinkles; i++) {
		float  blendValue    = regions->m_blendValue[i].x;
		float  blendValueSat = saturate(blendValue);
		float4 region     = regions->m_regions[i];
		float4 bexp       = regions->m_blendExponents[i];

		float2 uvRegion = (uv - region.xy) / (region.zw - region.xy);
		
		if (all(uv >= regions->m_regions[i].xy && uv <= regions->m_regions[i].zw) && blendValue > 0)  { // are we inside the region and we are blending

			// Remap the fullscreen uv into the region specific one
			float4 srcColor = regions->m_textures[i].SampleLevel(textureSampler, uvRegion, 0);

			// Procedural blend using quad's vertex coordinates as colors (there is info in uvRegion) to say how to create a gradient
			// The gradient is multiplied by a combination of the normal map to create a 'fade in-out' map that directs the blend value
			// This is all to avoid the common "alpha-blending" look of normal blending
			float uvBilinear = dot(float4((1.0 - uvRegion.x) * (1.0 - uvRegion.y), (uvRegion.x) * (1.0 - uvRegion.y), (1.0 - uvRegion.x) * (uvRegion.y), (uvRegion.x) * (uvRegion.y)), bexp);
			float blend = srcColor.r * pow(blendValue, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));
			float blendSat = srcColor.r * pow(blendValueSat, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));

			{
				// Swizzled color using BC3 compression
				// BC3 compression
				// The channels of the BC3 compressed texture are as follows:
				// RGBA <- (alpha mask, normal map G channel, AO, normal map R channel
				// RGBA = (alpha, normalMap.g, AO.r, normalMap.r)
				{
					accumulator.rg += blend * pSrt->m_heightFieldStrength * (2.0 * ( srcColor.ag - .5));
					accumulator.b  += blendSat * (2.0 * srcColor.b - 1.0);
					accumulator.a  += blend;
					accAoBlend     += blendSat;
				}
			}
		} 
	}
	accumulator.a = saturate(accumulator.a);
	outColor.rg = lerp(base.xy, saturate(accumulator.rg * float2(.5) + float2(.5)), accumulator.a);
	accAoBlend *= pSrt->m_pow1;
	outColor.b = (1.0 - accAoBlend) * ((base.z*2.0)-1.0) + accumulator.b;
	outColor.b = outColor.b * .5 + .5;
	outColor.a = 1.0;

	return outColor;
}

																	 
float4 Ps_WrinklesDebug(PS_PosTex input, WrinkleSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float2 uvQuad = float2(input.Tex);
	uint mode     = pSrt->m_mode.x;
	WrinkleRegions *regions = pSrt->m_regions;

	// Convert the uv of the quad into fullScreen uvs
	float2 uv = (1.0 - uvQuad) * pSrt->m_regionOffset.xy + uvQuad * pSrt->m_regionOffset.zw;

	SamplerState textureSampler = pSrt->m_pSamplers->g_linearClampSampler;

	float4 outColor    = float4(0, 0, 0, 1);
	float4 accumulator = float4(0,0,0,0);
	float4 accumulator2 = float4(0,0,1,0);
	float  weight = 0;

	float outline = 1.0;

	// Get the base
	// Note, that the base IS NOT swizzled
	// We are expecting the texture to be a normalmap texture. ie. The red and green channels contain the normal map values
	// This is on contrast with the rest of the wrinkle textures, which ARE swizzled. See below for the 
	float4 base = regions->m_textures[0].SampleLevel(textureSampler, uv, 0);

	for (int i=1; i < pSrt->m_numWrinkles; i++) {
		float  blendValue = regions->m_blendValue[i].x;
		float  blendValueSat = regions->m_blendValue[i].x;
		float4 region = regions->m_regions[i];
		float4 bexp = regions->m_blendExponents[i];

		float2 uvRegion = (uv - region.xy) / (region.zw - region.xy);
		
		if (all(uv >= regions->m_regions[i].xy && uv <= regions->m_regions[i].zw) && blendValue > 0)  { // are we inside the region and we are blending

			// Remap the fullscreen uv into the region specific one
			float4 srcColor = regions->m_textures[i].SampleLevel(textureSampler, uvRegion, 0);

			// Procedural blend using quad's vertex coordinates as colors (there is info in uvRegion) to say how to create a gradient
			// The gradient is multiplied by a combination of the normal map to create a 'fade in-out' map that directs the blend value
			// This is all to avoid the common "alpha-blending" look of normal blending
			float uvBilinear = dot(float4((1.0 - uvRegion.x) * (1.0 - uvRegion.y), (uvRegion.x) * (1.0 - uvRegion.y), (1.0 - uvRegion.x) * (uvRegion.y), (uvRegion.x) * (uvRegion.y)), bexp);
			float blend = srcColor.r * pow(blendValue, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));
			
			float blendSat = srcColor.r * pow(blendValueSat, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));
			// Will draw an outline
			float outWidth = .02;
			outline = (uvRegion.x < outWidth || uvRegion.x > (1.0 - outWidth) || uvRegion.y < outWidth || uvRegion.y > (1.0 - outWidth)) ? 0 : 1;

			switch (mode) {
			case 0:
			case 1:
			case 2:
				// case 0 and 1 shoud be identical!!!
				{
					// Swizzled color using BC3 compression
					// BC3 compression
					// The channels of the BC3 compressed texture are as follows:
					// RGBA <- (alpha mask, normal map G channel, AO, normal map R channel
					// RGBA = (alpha, normalMap.g, AO.r, normalMap.r)
					if (blend  <= 0) 
					{ 
						break; 
					}
					else 
					{
						// float2 srcC = 2.0 * ( srcColor.ag - .5);
						// srcC = float2(pSrt->m_redBias * (pSrt->m_redOffset + srcC.r), pSrt->m_greenBias * (pSrt->m_greenOffset + srcC.g));
						accumulator.rg += blend * pSrt->m_heightFieldStrength * (2.0 * ( srcColor.ag - .5)); // srcC; 
						//accumulator.b  += blend * pSrt->m_pow1 * srcColor.b; // (2.0 * ( srcColor.b - .5));
						//accumulator.b  += blend * pSrt->m_pow1 * (2.0 * ( srcColor.b - .5));
						//accumulator.b = lerp( accumulator.b, 1.0 - saturate(pow(1.0 - accumulator.b * srcColor.b, pSrt->m_pow2)), pSrt->m_pow1 * blend); // AO
						//accumulator.b = max(accumulator.b, srcColor.b); // AO
						accumulator.b += blendSat * (1.0-srcColor.b);
						accumulator.a += blend; 
					}
				}
				break;
			case 3:
				accumulator.b += blendSat * srcColor.b;
				accumulator.a += blendSat; 
				break;
			case 4:
				accumulator.b += blendSat * srcColor.b;
				accumulator.a += blendSat; 
				break;
			case 5:
				accumulator.b += lerp(base.z, srcColor.b, blendSat); // favorite!!
				accumulator.a += blendSat; 
				break;
			case 6:
				accumulator.b += blendSat * srcColor.b;
				accumulator.a += blendSat; 
				break;
			case 7:
				accumulator2.b *= lerp(base.z, srcColor.b, blendSat);
				accumulator2.a += blendSat; 
				break;
			case 9:
				accumulator.b *= uvRegion.x * uvRegion.y;
				accumulator.a += blend; 
				break;
			}
		}

		{
			switch (mode) {
			case 10: 
				{
					float4 srcColor = regions->m_textures[i].SampleLevel(textureSampler, uv, 0);
					accumulator.rgba = srcColor.agbr;

				}
			case 11: 
				{
					float4 srcColor = regions->m_textures[i].SampleLevel(textureSampler, uvQuad, 0);
					accumulator.rgba = srcColor.agbr;

				}
			} 
		}
	}

	switch (mode) {
	case 0:
	case 1:
	case 2:
		outColor.rg = lerp(base.xy, saturate(accumulator.rg * float2(.5) + float2(.5)), saturate(accumulator.a));
		//		outColor.b = saturate(accumulator.b); // AO
		//		outColor.b  = lerp(base.z,  saturate(accumulator.b), pSrt->m_pow1 * accumulator.a);
		outColor.b  = lerp(base.z,  1.0-saturate(accumulator.b), saturate(accumulator.a));
		outColor.a = 1.0;
		outColor.rgb *= outline;
		break;
	case 3:
		outColor.b   =  saturate(accumulator.a);
		outColor.rgb *= outline;
		break;
	case 4:
		outColor.b    = accumulator.b;
		outColor.rgb *= outline;
		break;
	case 5:
		outColor.rg  = lerp(base.xy, float2(0), accumulator.y);
		outColor.b   = lerp(base.z,  saturate(accumulator.b), saturate(accumulator.a)); // favorite!!
		outColor.rgb *= outline;
		break;
	case 6:
		outColor.rg  = lerp(base.xy, float2(0), accumulator.y);
		outColor.b   = (1.0 - accumulator.a) * base.z + accumulator.b; // BEST
		outColor.rgb *= outline;
		break;
	case 7:
		outColor.b   = lerp(base.z,  saturate(accumulator2.b), saturate(accumulator2.a)); 
		outColor.rgb *= outline;
		break;
	case 9:
		{
			outColor.b = accumulator.b;
			outColor.rgb *= outline;
		}
		break;
	case 8:
		{
			outColor.rgba = accumulator.rgba;
			outColor.rgb *= outline;
		}
		break;
	case 13:
		{
			outColor.rgb = pSrt->m_debugBlendValue;
			outColor.a = 1.0;
		}
	}

	outColor.b *= pSrt->m_pow1;


	return outColor;

}

static const int kRegionTileSize = 5;

// CS Versions
[numthreads(8,8,1)]
void Cs_Wrinkles(uint3 dispatchThreadID : SV_DispatchThreadID, WrinkleSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float2 uvQuad = (dispatchThreadID.xy + 0.5f) * pSrt->m_bufferSizeInv;
	WrinkleRegions *regions = pSrt->m_regions;

	// get the entry in the region tile table
	int2	tileId = dispatchThreadID.xy >> kRegionTileSize;
	int		tileIdx = __v_readlane_b32(tileId.y * pSrt->m_tileDims.x + tileId.x, 0);// these are all from the same tile

	ulong	regionTile = pSrt->m_regionTiles[tileIdx];	

	// Convert the uv of the quad into fullScreen uvs
	float2 uv = (1.0 - uvQuad) * pSrt->m_regionOffset.xy + uvQuad * pSrt->m_regionOffset.zw;

	SamplerState textureSampler = pSrt->m_pSamplers->g_linearClampSampler;

	float4 outColor     = float4(0, 0, 0, 1);
	float4 accumulator  = float4(0,0,0,0);
	float  accAoBlend   = 0;

	// Get the base
	// Note, that the base IS NOT swizzled
	// We are expecting the texture to be a normalmap texture. ie. The red and green channels contain the normal map values
	// This is on contrast with the rest of the wrinkle textures, which ARE swizzled. See below for the 
	float3 base = regions->m_textures[0].SampleLevel(textureSampler, uv, 0).rgb;

	//for (int i=1; i < pSrt->m_numWrinkles; i++) {
	while (regionTile) {
		int i = __s_ff1_i32_b64(regionTile);
		regionTile &= ~1L << i;
		i++;

		float  blendValue    = regions->m_blendValue[i].x;
		float  blendValueSat = saturate(blendValue);
		float4 region     = regions->m_regions[i];
		float4 bexp       = regions->m_blendExponents[i];

		float2 uvRegion = (uv - region.xy) / (region.zw - region.xy);
		
		if (all(uv >= regions->m_regions[i].xy && uv <= regions->m_regions[i].zw) && blendValue > 0)  { // are we inside the region and we are blending

			// Remap the fullscreen uv into the region specific one
			float4 srcColor = regions->m_textures[i].SampleLevel(textureSampler, uvRegion, 0);

			// Procedural blend using quad's vertex coordinates as colors (there is info in uvRegion) to say how to create a gradient
			// The gradient is multiplied by a combination of the normal map to create a 'fade in-out' map that directs the blend value
			// This is all to avoid the common "alpha-blending" look of normal blending
			float uvBilinear = dot(float4((1.0 - uvRegion.x) * (1.0 - uvRegion.y), (uvRegion.x) * (1.0 - uvRegion.y), (1.0 - uvRegion.x) * (uvRegion.y), (uvRegion.x) * (uvRegion.y)), bexp);
			float blend = srcColor.r * pow(blendValue, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));
			float blendSat = srcColor.r * pow(blendValueSat, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));

			{
				// Swizzled color using BC3 compression
				// BC3 compression
				// The channels of the BC3 compressed texture are as follows:
				// RGBA <- (alpha mask, normal map G channel, AO, normal map R channel
				// RGBA = (alpha, normalMap.g, AO.r, normalMap.r)
				{
					accumulator.rg += blend * pSrt->m_heightFieldStrength * (2.0 * ( srcColor.ag - .5));
					accumulator.b  += blendSat * srcColor.b;
					accumulator.a  += blend;
					accAoBlend     += blendSat;
				}
			}
		} 
	}
	accumulator.a = saturate(accumulator.a);
	outColor.rg = lerp(base.xy, saturate(accumulator.rg * float2(.5) + float2(.5)), accumulator.a);
	accAoBlend *= pSrt->m_pow1;
	outColor.b = (1.0 - accAoBlend) * base.z + accumulator.b;
	outColor.a = 1.0;

	pSrt->m_destBuffer[dispatchThreadID.xy] = outColor;
}


[numthreads(8,8,1)]
void Cs_WrinklesDiffuse(uint3 dispatchThreadID : SV_DispatchThreadID, WrinkleSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float2 uvQuad = (dispatchThreadID.xy + 0.5f) * pSrt->m_bufferSizeInv;
	WrinkleRegions *regions = pSrt->m_regions;

	// get the entry in the region tile table
	int2	tileId = dispatchThreadID.xy >> kRegionTileSize;
	int		tileIdx = __v_readlane_b32(tileId.y * pSrt->m_tileDims.x + tileId.x, 0);// these are all from the same tile

	ulong	regionTile = pSrt->m_regionTiles[tileIdx];	

	// Convert the uv of the quad into fullScreen uvs
	float2 uv = (1.0 - uvQuad) * pSrt->m_regionOffset.xy + uvQuad * pSrt->m_regionOffset.zw;

	SamplerState textureSampler = pSrt->m_pSamplers->g_linearClampSampler;

	float4 outColor     = float4(0, 0, 0, 1);
	float4 accumulator  = float4(0,0,0,0);
	float  accAoBlend   = 0;

	// Get the base
	// Note, that the base IS NOT swizzled
	// We are expecting the texture to be a normalmap texture. ie. The red and green channels contain the normal map values
	// This is on contrast with the rest of the wrinkle textures, which ARE swizzled. See below for the 
	float3 base = regions->m_textures[0].SampleLevel(textureSampler, uv, 0).rgb;

	//for (int i=1; i < pSrt->m_numWrinkles; i++) {
	while (regionTile) {
		// find the first bit in the mask and also unset it in the mask
		int i = __s_ff1_i32_b64(regionTile);
		regionTile &= ~1L << i;
		i++; // the zero'th region is actually the base region, so we need to increment by one to get the real region we're processing

		float  blendValue    = regions->m_blendValue[i].x;
		float  blendValueSat = saturate(blendValue);
		float4 region     = regions->m_regions[i];
		float4 bexp       = regions->m_blendExponents[i];

		float2 uvRegion = (uv - region.xy) / (region.zw - region.xy);
		
		if (all(uv >= regions->m_regions[i].xy && uv <= regions->m_regions[i].zw) && blendValue > 0)  { // are we inside the region and we are blending
			// Remap the fullscreen uv into the region specific one
			float4 srcColor = regions->m_textures[i].SampleLevel(textureSampler, uvRegion, 0);

			// Procedural blend using quad's vertex coordinates as colors (there is info in uvRegion) to say how to create a gradient
			// The gradient is multiplied by a combination of the normal map to create a 'fade in-out' map that directs the blend value
			// This is all to avoid the common "alpha-blending" look of normal blending
			float uvBilinear = dot(float4((1.0 - uvRegion.x) * (1.0 - uvRegion.y), (uvRegion.x) * (1.0 - uvRegion.y), (1.0 - uvRegion.x) * (uvRegion.y), (uvRegion.x) * (uvRegion.y)), bexp);
			float blend = srcColor.r * pow(blendValue, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));
			float blendSat = srcColor.r * pow(blendValueSat, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));

			{
				// Swizzled color using BC3 compression
				// BC3 compression
				// The channels of the BC3 compressed texture are as follows:
				// RGBA <- (alpha mask, normal map G channel, AO, normal map R channel
				// RGBA = (alpha, normalMap.g, AO.r, normalMap.r)
				{
					accumulator.rg += blend * pSrt->m_heightFieldStrength * (2.0 * ( srcColor.ag - .5));
					accumulator.b  += blendSat * (2.0 * srcColor.b - 1.0);
					accumulator.a  += blend;
					accAoBlend     += blendSat;
				}
			}
		} 

	}
	accumulator.a = saturate(accumulator.a);
	outColor.rg = lerp(base.xy, saturate(accumulator.rg * float2(.5) + float2(.5)), accumulator.a);
	accAoBlend *= pSrt->m_pow1;
	outColor.b = (1.0 - accAoBlend) * ((base.z*2.0)-1.0) + accumulator.b;
	outColor.b = outColor.b * .5 + .5;
	outColor.a = 1.0;

	pSrt->m_destBuffer[dispatchThreadID.xy] = outColor;
}

[numthreads(8,8,1)]
void Cs_WrinklesDebug(uint3 dispatchThreadID : SV_DispatchThreadID, WrinkleSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float2 uvQuad = (dispatchThreadID.xy + 0.5f) * pSrt->m_bufferSizeInv;
	WrinkleRegions *regions = pSrt->m_regions;

	uint mode     = pSrt->m_mode.x;

	// Convert the uv of the quad into fullScreen uvs
	float2 uv = (1.0 - uvQuad) * pSrt->m_regionOffset.xy + uvQuad * pSrt->m_regionOffset.zw;

	SamplerState textureSampler = pSrt->m_pSamplers->g_linearClampSampler;

	float4 outColor    = float4(0, 0, 0, 1);
	float4 accumulator = float4(0,0,0,0);
	float4 accumulator2 = float4(0,0,1,0);
	float  weight = 0;

	float outline = 1.0;

	// Get the base
	// Note, that the base IS NOT swizzled
	// We are expecting the texture to be a normalmap texture. ie. The red and green channels contain the normal map values
	// This is on contrast with the rest of the wrinkle textures, which ARE swizzled. See below for the 
	float4 base = regions->m_textures[0].SampleLevel(textureSampler, uv, 0);

	for (int i=1; i < pSrt->m_numWrinkles; i++) {
		float  blendValue = regions->m_blendValue[i].x;
		float  blendValueSat = regions->m_blendValue[i].x;
		float4 region = regions->m_regions[i];
		float4 bexp = regions->m_blendExponents[i];

		float2 uvRegion = (uv - region.xy) / (region.zw - region.xy);
		
		if (all(uv >= regions->m_regions[i].xy && uv <= regions->m_regions[i].zw) && blendValue > 0)  { // are we inside the region and we are blending

			// Remap the fullscreen uv into the region specific one
			float4 srcColor = regions->m_textures[i].SampleLevel(textureSampler, uvRegion, 0);

			// Procedural blend using quad's vertex coordinates as colors (there is info in uvRegion) to say how to create a gradient
			// The gradient is multiplied by a combination of the normal map to create a 'fade in-out' map that directs the blend value
			// This is all to avoid the common "alpha-blending" look of normal blending
			float uvBilinear = dot(float4((1.0 - uvRegion.x) * (1.0 - uvRegion.y), (uvRegion.x) * (1.0 - uvRegion.y), (1.0 - uvRegion.x) * (uvRegion.y), (uvRegion.x) * (uvRegion.y)), bexp);
			float blend = srcColor.r * pow(blendValue, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));
			
			float blendSat = srcColor.r * pow(blendValueSat, lerp(1.0, pSrt->m_pow2, saturate(pow(uvBilinear, pSrt->m_pow3) * 50 * (1.0 - (srcColor.a - .5) * (srcColor.g - .5)  ))));
			// Will draw an outline
			float outWidth = .02;
			outline = (uvRegion.x < outWidth || uvRegion.x > (1.0 - outWidth) || uvRegion.y < outWidth || uvRegion.y > (1.0 - outWidth)) ? 0 : 1;

			switch (mode) {
			case 0:
			case 1:
			case 2:
				// case 0 and 1 shoud be identical!!!
				{
					// Swizzled color using BC3 compression
					// BC3 compression
					// The channels of the BC3 compressed texture are as follows:
					// RGBA <- (alpha mask, normal map G channel, AO, normal map R channel
					// RGBA = (alpha, normalMap.g, AO.r, normalMap.r)
					if (blend  <= 0) 
					{ 
						break; 
					}
					else 
					{
						// float2 srcC = 2.0 * ( srcColor.ag - .5);
						// srcC = float2(pSrt->m_redBias * (pSrt->m_redOffset + srcC.r), pSrt->m_greenBias * (pSrt->m_greenOffset + srcC.g));
						accumulator.rg += blend * pSrt->m_heightFieldStrength * (2.0 * ( srcColor.ag - .5)); // srcC; 
						//accumulator.b  += blend * pSrt->m_pow1 * srcColor.b; // (2.0 * ( srcColor.b - .5));
						//accumulator.b  += blend * pSrt->m_pow1 * (2.0 * ( srcColor.b - .5));
						//accumulator.b = lerp( accumulator.b, 1.0 - saturate(pow(1.0 - accumulator.b * srcColor.b, pSrt->m_pow2)), pSrt->m_pow1 * blend); // AO
						//accumulator.b = max(accumulator.b, srcColor.b); // AO
						accumulator.b += blendSat * (1.0-srcColor.b);
						accumulator.a += blend; 
					}
				}
				break;
			case 3:
				accumulator.b += blendSat * srcColor.b;
				accumulator.a += blendSat; 
				break;
			case 4:
				accumulator.b += blendSat * srcColor.b;
				accumulator.a += blendSat; 
				break;
			case 5:
				accumulator.b += lerp(base.z, srcColor.b, blendSat); // favorite!!
				accumulator.a += blendSat; 
				break;
			case 6:
				accumulator.b += blendSat * srcColor.b;
				accumulator.a += blendSat; 
				break;
			case 7:
				accumulator2.b *= lerp(base.z, srcColor.b, blendSat);
				accumulator2.a += blendSat; 
				break;
			case 9:
				accumulator.b *= uvRegion.x * uvRegion.y;
				accumulator.a += blend; 
				break;
			}
		}

		{
			switch (mode) {
			case 10: 
				{
					float4 srcColor = regions->m_textures[i].SampleLevel(textureSampler, uv, 0);
					accumulator.rgba = srcColor.agbr;

				}
			case 11: 
				{
					float4 srcColor = regions->m_textures[i].SampleLevel(textureSampler, uvQuad, 0);
					accumulator.rgba = srcColor.agbr;

				}
			} 
		}
	}

	switch (mode) {
	case 0:
	case 1:
	case 2:
		outColor.rg = lerp(base.xy, saturate(accumulator.rg * float2(.5) + float2(.5)), saturate(accumulator.a));
		//		outColor.b = saturate(accumulator.b); // AO
		//		outColor.b  = lerp(base.z,  saturate(accumulator.b), pSrt->m_pow1 * accumulator.a);
		outColor.b  = lerp(base.z,  1.0-saturate(accumulator.b), saturate(accumulator.a));
		outColor.a = 1.0;
		outColor.rgb *= outline;
		break;
	case 3:
		outColor.b   =  saturate(accumulator.a);
		outColor.rgb *= outline;
		break;
	case 4:
		outColor.b    = accumulator.b;
		outColor.rgb *= outline;
		break;
	case 5:
		outColor.rg  = lerp(base.xy, float2(0), accumulator.y);
		outColor.b   = lerp(base.z,  saturate(accumulator.b), saturate(accumulator.a)); // favorite!!
		outColor.rgb *= outline;
		break;
	case 6:
		outColor.rg  = lerp(base.xy, float2(0), accumulator.y);
		outColor.b   = (1.0 - accumulator.a) * base.z + accumulator.b; // BEST
		outColor.rgb *= outline;
		break;
	case 7:
		outColor.b   = lerp(base.z,  saturate(accumulator2.b), saturate(accumulator2.a)); 
		outColor.rgb *= outline;
		break;
	case 9:
		{
			outColor.b = accumulator.b;
			outColor.rgb *= outline;
		}
		break;
	case 8:
		{
			outColor.rgba = accumulator.rgba;
			outColor.rgb *= outline;
		}
		break;
	case 13:
		{
			outColor.rgb = pSrt->m_debugBlendValue;
			outColor.a = 1.0;
		}
	case 15:
	{
		outColor.rgb = base.z;
		break;
	}
	}

	outColor.b *= pSrt->m_pow1;

	pSrt->m_destBuffer[dispatchThreadID.xy] = outColor;
}
