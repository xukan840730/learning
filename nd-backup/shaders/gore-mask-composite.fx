//--------------------------------------------------------------------------------------
// File: gore-mask-composite.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

struct SamplerTable
{
	SamplerState				g_linearSampler;
	SamplerState				g_pointSampler;
	SamplerComparisonState		g_shadowSampler;
	SamplerState				g_linearClampSampler;
	SamplerState				g_linearClampUSampler;
	SamplerState				g_linearClampVSampler;
	SamplerState				g_linearMirrorSampler;
	SamplerState				g_linearMirrorUSampler;
	SamplerState				g_linearMirrorVSampler;
	SamplerState				g_pointSampler_NoHCMBias;
	SamplerState				g_linearClampSampler_NoHCMBias;
};

struct GoreRegions
{
	float4             m_regions[32];
	float4	           m_blendExponents[32];
	Texture2D<float4>  m_textures[32];
};

struct GoreSrt
{
	RWTexture2D<float4>	m_destBuffer;
	float2 m_invBufferSize;

	int m_numRegions;
	GoreRegions* m_regions;

	float2 m_tileDims;
	Buffer<ulong> m_regionTiles; //bitmask per tile of regions affecting it

	SamplerTable *m_pSamplers;

	int m_mode;
};

float2 CalculateCompositedNormal(float2 xy, int numCompositedRegions)
{
	xy /= (float)(numCompositedRegions); // scale down by the number of regions that were composited
	float z = sqrt(1.0f - (xy.x * xy.x) - (xy.y * xy.y)); // calculate what the z component would be
	xy = normalize(float3(xy, z)).xy; // create the full normal and normalize it
	return xy;
}

static const int kRegionTileSize = 4;

// CS Versions
[numthreads(8,8,1)]
void Cs_Gore(uint3 dispatchThreadID : SV_DispatchThreadID, GoreSrt* pSrt : S_SRT_DATA) : SV_Target
{
	SamplerState textureSampler = pSrt->m_pSamplers->g_linearClampSampler_NoHCMBias;
	float2 uv = (dispatchThreadID.xy + 0.5f) * pSrt->m_invBufferSize;

	int2 tileId = dispatchThreadID.xy >> kRegionTileSize;
	int tileIdx = __v_readlane_b32(tileId.y * pSrt->m_tileDims.x + tileId.x, 0);

	ulong regionTile = pSrt->m_regionTiles[tileIdx];

	float4 curMask = pSrt->m_destBuffer[dispatchThreadID.xy];
	float2 rg = (curMask.rg * 2.0f) - float2(1.0f);
	float b = curMask.b;
	float a = curMask.a;

	int numCompositedRegions = 1;

	while(regionTile)
	{
		int i = __s_ff1_i32_b64(regionTile); 
		regionTile &= ~1L << i;

		float4 region = pSrt->m_regions->m_regions[i];

		if (all(uv >= region.xy && uv <= region.zw))
		{
			float regionU = (uv.x - region.x) / (region.z - region.x);
			float regionV = (uv.y - region.y) / (region.w - region.y);

			float4 srcColor = pSrt->m_regions->m_textures[i].SampleLevel(textureSampler, float2(regionU, regionV), 0);
			rg += ((srcColor.rg * 2.0f) - float2(1.0f));
			b = max(b, srcColor.b);
			a = min(a, srcColor.a);

			numCompositedRegions += 1;
		}
	}

	float4 outColor = curMask;

	// if we actually composited any regions, take care to try and preserve the normals
	if (numCompositedRegions > 1)
	{
		rg = CalculateCompositedNormal(rg, numCompositedRegions);
		outColor.rg = saturate((rg + float2(1.0f)) * 0.5f);
	}

	outColor.b = b;
	outColor.a = round(a);

	pSrt->m_destBuffer[dispatchThreadID.xy] = outColor;
}

enum GoreDebugDrawModes
{
	kDrawBloodNormals = 0x01,
	kDrawBloodMask = 0x02,
	kDrawGoreMask = 0x04,
	kDrawAll = 0x08
};

[numthreads(8,8,1)]
void Cs_GoreDebug(uint3 dispatchThreadID : SV_DispatchThreadID, GoreSrt* pSrt : S_SRT_DATA) : SV_Target
{
	SamplerState textureSampler = pSrt->m_pSamplers->g_linearClampSampler_NoHCMBias;
	float2 uv = (dispatchThreadID.xy + 0.5f) * pSrt->m_invBufferSize;

	int2 tileId = dispatchThreadID.xy >> kRegionTileSize;
	int tileIdx = __v_readlane_b32(tileId.y * pSrt->m_tileDims.x + tileId.x, 0);

	ulong regionTile = pSrt->m_regionTiles[tileIdx];

	float4 curMask = pSrt->m_destBuffer[dispatchThreadID.xy];
	float2 rg = (curMask.rg * 2.0f) - float2(1.0f);
	float b = curMask.b;
	float a = curMask.a;

	int numCompositedRegions = 1;

	while(regionTile)
	{
		int i = __s_ff1_i32_b64(regionTile);
		regionTile &= ~1L << i;

		float4 region = pSrt->m_regions->m_regions[i];

		if (all(uv >= region.xy && uv <= region.zw))
		{
			float regionU = (uv.x - region.x) / (region.z - region.x);
			float regionV = (uv.y - region.y) / (region.w - region.y);

			float4 srcColor = pSrt->m_regions->m_textures[i].SampleLevel(textureSampler, float2(regionU, regionV), 0);
			rg += ((srcColor.rg * 2.0f) - float2(1.0f));
			b = max(b, srcColor.b);
			a = min(a, srcColor.a);

			numCompositedRegions += 1;
		}
	}

	float4 outColor = curMask;

	// if we actually composited any regions, take care to try and preserve the normals
	if (numCompositedRegions > 1)
	{
		rg = CalculateCompositedNormal(rg, numCompositedRegions);
		outColor.rg = saturate((rg + float2(1.0f)) * 0.5f);
	}

	if (pSrt->m_mode == kDrawAll)
	{
		outColor.b = b;
		outColor.a = round(a);
	}
	else
	{
		outColor.rg = ((pSrt->m_mode & kDrawBloodNormals) != 0) ? outColor.rg : float2(0.5f);
		outColor.b = ((pSrt->m_mode & kDrawBloodMask) != 0) ? b : 0.0f;
		outColor.a = ((pSrt->m_mode & kDrawGoreMask) != 0) ? round(a) : 1.0f;
	}


	pSrt->m_destBuffer[dispatchThreadID.xy] = outColor;
}