//
// Rain occluder compute shader
//
#include "global-funcs.fxi"

#define kMaxVertexBlocks	320	// 5 * 64, (rounding up 300 to nearest wavefront)

// Should be the same as wetfx.h
#define kMaxRainOccluderSamples 30 * 10

struct RainOccluderSamplesInfo 
{
	int m_numSamples;
	int m_pad1;
	int m_pad2;
	int m_pad3;

	float4 m_occlusionParameters[3];

	float4 m_position[kMaxRainOccluderSamples];
};

struct SamplerTable
{
	SamplerState g_linearSampler;
	SamplerState g_pointSampler;
	SamplerComparisonState g_shadowSampler;
	SamplerState g_linearClampSampler;
	SamplerState g_linearClampUSampler;
	SamplerState g_linearClampVSampler;
};

struct TexturesAndSamplers 
{
	Texture2D<float4> m_rainOccluderShadow;	
	SamplerTable* m_pSamplers;
};

struct SrtData
{
	RainOccluderSamplesInfo  * m_consts;
	RWStructuredBuffer<float>  m_result;
	TexturesAndSamplers*	   m_texturesAndSamplers;
};

[numthreads(kMaxVertexBlocks, 1, 1)]
void Cs_RainOccluder(  uint2 dispatchThreadId : SV_DispatchThreadID, SrtData srt : S_SRT_DATA)
{
	// Each thread deals with one individual sample
	int index = dispatchThreadId.x;
	if (index >= kMaxRainOccluderSamples)
		return;

	RainOccluderSamplesInfo *info = srt.m_consts;

	float4 pos = info->m_position[index];  // asume .w =1
	float3 texcoords;

	texcoords.x = dot(info->m_occlusionParameters[0], pos);
	texcoords.y = dot(info->m_occlusionParameters[1], pos);
	texcoords.z = dot(info->m_occlusionParameters[2], pos);

	TexturesAndSamplers* pTxSamp = srt.m_texturesAndSamplers;

	float value = pTxSamp->m_rainOccluderShadow.SampleCmpLevelZero(pTxSamp->m_pSamplers->g_shadowSampler, texcoords.xy, texcoords.z).r;

	srt.m_result[index] = value;
}
