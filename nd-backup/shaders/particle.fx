/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "packing.fxi"
#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "global-const-buffers.fxi"
#include "color-util.fxi"

struct CompositeDownsampledParticlesSrt
{
	float4					m_bufSize;
	float4					m_invBufSize;
	float					m_upscaleRatio;
	float3					m_pad;
	RegularBuffer<float4>	m_boundsBuffer;
	Texture2D<float4>		m_colorBuffer;
	Texture2D<uint>			m_cmaskBuffer;
	Texture2D<uint>			m_mergeBuffer;
	SamplerState			m_linearSampler;
};

float4 VS_CompositeDownsampledParticles(CompositeDownsampledParticlesSrt *pSrt : S_SRT_DATA,
										uint vertexId : SV_VertexID) : SV_Position
{
	uint boundsIdx = vertexId / 6;
	vertexId -= boundsIdx * 6;

	// Calculate the vertex position using the vertex id and the input bounds
	float4 bounds = pSrt->m_boundsBuffer[boundsIdx];

	// #DBTD: Figure out how to do this without the if statements maybe?
	float2 pos = float2(0.0f, 0.0f);
	     if (vertexId == 0) pos = float2(bounds.x, bounds.y);
	else if (vertexId == 1) pos = float2(bounds.x, bounds.w);
	else if (vertexId == 2) pos = float2(bounds.z, bounds.y);
	else if (vertexId == 3) pos = float2(bounds.z, bounds.y);
	else if (vertexId == 4) pos = float2(bounds.x, bounds.w);
	else if (vertexId == 5) pos = float2(bounds.z, bounds.w);

	return float4(pos, 0.5f, 1.0f);
}

float4 CompositeDownsampledParticles(CompositeDownsampledParticlesSrt *pSrt, float4 position)
{
	int2 upos = (int2)position.xy;

	// See Cs_CreateHalfResCompositeInfo
	int mergeInfo = pSrt->m_mergeBuffer[upos];
	int2 orgOffset = int2(mergeInfo, mergeInfo >> 2) & 0x03;
	int2 ioffset = orgOffset - 1;
	float scale = (mergeInfo >> 4) / 15.0f;

	float2 invSrcSize = pSrt->m_invBufSize.xy;
	float2 invDstSize = pSrt->m_invBufSize.zw;
	float2 srcSize = pSrt->m_bufSize.xy;

	float2 uvDst = position.xy * invDstSize;
	float2 uvSrc = (upos / 2 + ioffset + 0.5f) * invSrcSize;

	int2 sampleId = upos / 2 + int2(-1, -1);

	int2 blockIdx = sampleId >> 3;
	uint cmaskBits = pSrt->m_cmaskBuffer[blockIdx];
	int2 blockInnerIdx = sampleId & 0x07;

	if (blockInnerIdx.x >= 4)
	{
		cmaskBits >>= 1;
	}

	if (blockInnerIdx.x >= 6)
	{
		uint cmaskBits01 = pSrt->m_cmaskBuffer[blockIdx + int2(1, 0)];
		cmaskBits = (cmaskBits & 0x05) | ((cmaskBits01 << 1) & 0x0A);
	}

	if (blockInnerIdx.y >= 4)
	{
		cmaskBits >>= 2;
	}

	if (blockInnerIdx.y >= 6)
	{
		uint cmaskBits10 = pSrt->m_cmaskBuffer[blockIdx + int2(0, 1)];
		cmaskBits = (cmaskBits & 0x03) | ((cmaskBits10 << (blockInnerIdx.x < 4 ? 2 : 1)) & 0x0C);

		if (blockInnerIdx.x >= 6)
		{
			uint cmaskBits11 = pSrt->m_cmaskBuffer[blockIdx + int2(1, 1)];
			cmaskBits = (cmaskBits & 0x07) | ((cmaskBits11 << 3) & 0x08);
		}
	}

	int2 tileInnerIdx = sampleId & 0x03;

	uint expandBits0 = (((cmaskBits & 0x01) ? 0x0F : 0x00) | ((cmaskBits & 0x02) ? 0xF0 : 0x00)) >> tileInnerIdx.x;
	uint expandBits1 = (((cmaskBits & 0x04) ? 0x0F : 0x00) | ((cmaskBits & 0x08) ? 0xF0 : 0x00)) >> tileInnerIdx.x;

	uint rowBits0 = expandBits0;
	uint rowBits1 = tileInnerIdx.y <= 2 ? expandBits0 : expandBits1;
	uint rowBits2 = tileInnerIdx.y <= 1 ? expandBits0 : expandBits1;

	uint sumBits = (rowBits0 & 0x07) | ((rowBits1 & 0x07) << 4) | ((rowBits2 & 0x07) << 8);

	int testBit = 1 << ((orgOffset.y << 2) + orgOffset.x);

	if (sumBits == 0 || (sumBits != 0x777 && (sumBits & testBit) == 0))
		discard;

	float2 uv = lerp(uvDst, uvSrc, sumBits == 0x777 ? lerp(0.0f, scale, pSrt->m_upscaleRatio) : 1.0f);

	return pSrt->m_colorBuffer.SampleLevel(pSrt->m_linearSampler, uv, 0);
}

float4 PS_CompositeDownsampledParticles(CompositeDownsampledParticlesSrt *pSrt : S_SRT_DATA,
										float4 position : SV_Position) : SV_Target
{
	return CompositeDownsampledParticles(pSrt , position);
}

struct CompsitionOutput
{
	float4	color			: SV_Target;
	float2	pixelCoverage	: SV_Target1;
};

// This pass will be removed once particles output hcmcoverage value later.
CompsitionOutput PS_CompositeDownsampledParticlesHCM(CompositeDownsampledParticlesSrt *pSrt : S_SRT_DATA,
	float4 position : SV_Position)
{
	float4 srcColor = CompositeDownsampledParticles(pSrt , position);
	
	CompsitionOutput output;
	output.color = srcColor;
	output.pixelCoverage.x = 0.0f;			// This channel is reserved for opaque objects and it is masked out in this pass
	output.pixelCoverage.y = Luminance(srcColor.rgb) * srcColor.a;

	return output;
}