#include "global-funcs.fxi"

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
										
static const uint	kSamplesX = 4;
static const uint	kSamplesY = 4;
static const uint	kSamples = kSamplesX * kSamplesY;
								  
struct PathTracerCompositePsSrt
{
	Texture2D<float4>	m_texture;
	SamplerState		m_samplerState;

	float4				m_screenSize;
	uint4				m_pixelScaleOffset;
	uint				m_numSamples[kSamples];
};

float4 PS_PathTracerComposite(PathTracerCompositePsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	uint2	sampleIdx = (uint2)(input.Tex * srt->m_screenSize.xy) % srt->m_pixelScaleOffset.xy;
	uint	idx = sampleIdx.x  + sampleIdx.y * srt->m_pixelScaleOffset.x;

	return (float)1.f / srt->m_numSamples[idx] * srt->m_texture.Sample(srt->m_samplerState, input.Tex);
}

