#define THREADS_PER_WAVEFRONT 64

#include "global-srt.fxi"
#include "post-globals.fxi"
#include "tile-util.fxi"
#include "post-processing-common.fxi"
#include "packing.fxi"

#ifndef CS_MEMORY_BOUNDS_CHECK
#define CS_MEMORY_BOUNDS_CHECK 1
#endif

/************************************************************************/
/* MemSet, set 8 bytes per thread                                       */
/************************************************************************/
struct MemSetSrt
{
	RWBuffer<uint2> m_dest;
	uint2           m_value;
	uint            m_numElements;
};

[numthreads(THREADS_PER_WAVEFRONT, 1, 1)]
void Cs_MemSet(uint3 dispatchThreadId : SV_DispatchThreadID, MemSetSrt srt : S_SRT_DATA)
{
#if CS_MEMORY_BOUNDS_CHECK
	if (dispatchThreadId.x < srt.m_numElements)
#endif
		srt.m_dest[dispatchThreadId.x] = srt.m_value;
}


/************************************************************************/
/* MemSet, set 16 bytes per thread                                      */
/************************************************************************/
struct MemSet4Srt
{
	RWBuffer<uint4> m_dest;
	uint4           m_value;
	uint            m_numElements;
};

[numthreads(THREADS_PER_WAVEFRONT, 1, 1)]
void Cs_MemSet4(uint3 dispatchThreadId : SV_DispatchThreadID, MemSet4Srt srt : S_SRT_DATA)
{
#if CS_MEMORY_BOUNDS_CHECK
	if (dispatchThreadId.x < srt.m_numElements)
#endif
		srt.m_dest[dispatchThreadId.x] = srt.m_value;
}


/************************************************************************/
/* MemCopy, copy 4 bytes per thread                                     */
/************************************************************************/
struct MemCpySrt
{
	StructuredBuffer<uint2>   m_src;
	RWStructuredBuffer<uint2> m_dest;
	uint           m_numElements;
};

[numthreads(THREADS_PER_WAVEFRONT, 1, 1)]
void Cs_MemCpy(uint3 dispatchThreadId : SV_DispatchThreadID, MemCpySrt srt : S_SRT_DATA)
{
	__s_setprio(3);

	uint i = dispatchThreadId.x;

	
	const uint2 e0 = srt.m_src[i];

#if CS_MEMORY_BOUNDS_CHECK
	if (i < srt.m_numElements)
#endif
		srt.m_dest[i] = e0;

}

/************************************************************************/
/* Batched Memcpy												         */
/************************************************************************/
static const uint kMaxMemCpyBatchSize = 8;

struct BatchedMemCpySrt
{
	Buffer<uint>	m_srcs[kMaxMemCpyBatchSize];
	RWBuffer<uint>	m_dests[kMaxMemCpyBatchSize];
	uint			m_numElements[kMaxMemCpyBatchSize];
	uint			m_numBatches;
};

[numthreads(64, 1, 1)]
void Cs_BatchedMemCpy(uint2 dispatchThreadId : SV_DispatchThreadID, BatchedMemCpySrt *pSrt : S_SRT_DATA)
{
	for (uint iBatch = 0; iBatch < pSrt->m_numBatches; ++iBatch)
	{
		// Skip the wavefront's entire wasted work if possible
		if (dispatchThreadId.x < pSrt->m_numElements[iBatch])
		{
			pSrt->m_dests[iBatch][dispatchThreadId.x] = pSrt->m_srcs[iBatch][dispatchThreadId.x];
		}
	}
}

/************************************************************************/
/* MemCopyToTexture, copy 4 bytes per thread                            */
/************************************************************************/
struct MemCpyTextureSrt
{
	Buffer<float4>      m_src;
	RWTexture2D<float4> m_dest;
	uint                m_width;
	uint                m_height;
};

[numthreads(8, 8, 1)]
void Cs_MemCpyTexture(uint3 dispatchThreadId : SV_DispatchThreadID, MemCpyTextureSrt srt : S_SRT_DATA)
{
#if CS_MEMORY_BOUNDS_CHECK
	if (dispatchThreadId.x < srt.m_width && dispatchThreadId.y < srt.m_height)
#endif
		srt.m_dest[int2(dispatchThreadId.xy)] = srt.m_src[dispatchThreadId.y * srt.m_width + dispatchThreadId.x];
}


/************************************************************************/
/* blend two 3d lut textures                                            */
/************************************************************************/

struct BlendLutTextures
{
	Texture3D<float4> txLut0; //: register(t0);
	Texture3D<float4> txLut1; //: register(t1);
	RWTexture3D<float4> txLutBlend; //: register(u0);
};

struct BlendLutSrt
{
	BlendLutTextures *pTextures;
	float lutBlendAlpha;
};

[numthreads(8, 8, 8)]
void Cs_BlendLut(uint3 dispatchThreadId : SV_DispatchThreadID,
                 BlendLutSrt srt : S_SRT_DATA)
{
	srt.pTextures->txLutBlend[dispatchThreadId] =
	    lerp(srt.pTextures->txLut0[dispatchThreadId], srt.pTextures->txLut1[dispatchThreadId], srt.lutBlendAlpha);
}


/************************************************************************/
/* Hires capture merging                                                */
/************************************************************************/

struct HiresCaptureSrt
{
	Texture2D<float4>	m_src;
	RWTexture2D<float4> m_dst;
	uint4				m_srcOffset;
	uint4				m_dstOffset;
};

[numthreads(8, 8, 1)]
void Cs_MergeToHiresCapture(uint3 dispatchThreadId : SV_DispatchThreadID, HiresCaptureSrt *pSrt : S_SRT_DATA)
{
	if (dispatchThreadId.x >= pSrt->m_srcOffset.x && dispatchThreadId.x < pSrt->m_srcOffset.z &&
		dispatchThreadId.y >= pSrt->m_srcOffset.y && dispatchThreadId.y < pSrt->m_srcOffset.w)
	{
		pSrt->m_dst[dispatchThreadId.xy + pSrt->m_dstOffset.xy] =	pSrt->m_src[dispatchThreadId.xy];
	}
}

const static int2 sharpenOffsets[4] = {int2(0, -1), int2(-1, 0), int2(1, 0), int2(0, 1)};

struct SharpenTextures
{
	Texture2D<float4>	m_txSrc;
	RWTexture2D<float4>	m_txDst;
	Texture2D<float3>	m_txDofPresort;
	Texture2D<float3>	m_txFilmGrain;
};

struct SharpenConsts
{
	float4	m_filmGrainOffsetScale;
	float4	m_filmGrainIntensity;
	float4	m_filmGrainIntensity2;
	float4	m_tintColorScaleVector;
	float4	m_tintColorOffsetVector;
	float2	m_invBufferDimension;
	float	m_weightScale;
	float	m_threshold;
	int		m_isHdrMode;
	int3	m_pad;
};

struct SharpenSrt
{
	SharpenConsts*		m_pConsts;
	SharpenTextures*	m_pTextures;
	SamplerState		m_samplerPoint;	
};

static const float3x3 BT709_TO_BT2020 = {
	0.627403896, 0.329283038, 0.043313066,
	0.069097289, 0.919540395, 0.011362316,
	0.016391439, 0.088013308, 0.895595253
};


float3 SRGB_OETF(float3 L)
{
	float3 dark = L.xyz * 12.92;
    float3 light = 1.055 * (float3)pow(L.xyz, 1.0 / 2.4) - 0.055;

    float3 r;
	bool3 cri = L.xyz <= 0.0031308;
	r.x = CndMask(cri.x, dark.x, light.x);
	r.y = CndMask(cri.y, dark.y, light.y);
	r.z = CndMask(cri.z, dark.z, light.z);
    return r;
}

float3 SRGB_EOTF(float3 E)
{
	float3 dark = E.xyz/12.92;
	float3 light = pow((E.xyz+0.055)/(1+0.055), 2.4);
	float3 r;
	bool3 cri = E.xyz <= 0.04045;
	r.x = CndMask(cri.x, dark.x, light.x);
	r.y = CndMask(cri.y, dark.y, light.y);
	r.z = CndMask(cri.z, dark.z, light.z);
	return r;
}

//apply gamma adjustment to (minL, maxL).
float3 GammaAdjOOTF(float3 L, float n, float minLNits, float maxLNits, float gamma, bool inverse)
{
	float3 nits = L.xyz * n;
	bool3 cri = nits >= minLNits && nits < maxLNits;
	float3 i = float3((nits - minLNits) / (maxLNits - minLNits));
	float3 j; 
	if(inverse){
		j = SRGB_EOTF(pow(i, 1/gamma)).xyz;
	}else{
		j = pow( SRGB_OETF(i).xyz, gamma);
	}
	float3 adj =  (minLNits + (maxLNits - minLNits) * j) / n;
	float3 ret;
	ret.x = CndMask(cri.x, adj.x, L.x);
	ret.y = CndMask(cri.y, adj.y, L.y);
	ret.z = CndMask(cri.z, adj.z, L.z);
	return ret;
}

//input: normalized L in units of RefWhite (1.0=100nits), output: normalized E
float3 applyPQ_OETF(float3 L, float n, uint gamma_adj = 0, float gamma = 2.2)
{
	if(gamma_adj)
		L = GammaAdjOOTF(L, n, 0.0, 300.0, gamma, false);
	const float c1 = 0.8359375;//3424.f/4096.f;
	const float c2 = 18.8515625;//2413.f/4096.f*32.f;
	const float c3 = 18.6875; //2392.f/4096.f*32.f;
	const float m1 = 0.159301758125; //2610.f / 4096.f / 4;
	const float m2 = 78.84375;// 2523.f / 4096.f * 128.f;
	L = L * n / 10000.0; 
	float3 Lm1 = pow(L.xyz, m1);
	float3 X = (c1 + c2 * Lm1) / (1 + c3 * Lm1);
	return pow(X, m2);
}

float4 GetSharpenColor(uint3 dispatchThreadId, float4 currentColor, bool applySharpen, SharpenSrt srt)
{
	float2 uv = (dispatchThreadId.xy + 0.5f) * srt.m_pConsts->m_invBufferDimension;

	float3 outColor = currentColor.rgb;

	if (applySharpen)
	{
		float4 neighborContribution = currentColor * 4.f;

		float weightScale = srt.m_pConsts->m_weightScale;
		float threshold = srt.m_pConsts->m_threshold;

		for (uint i = 0; i < 4; i++)
		{
			neighborContribution -= srt.m_pTextures->m_txSrc.SampleLevel(srt.m_samplerPoint, uv, 0.f, sharpenOffsets[i]);
		}

		neighborContribution.rgb = min(abs(neighborContribution.rgb), threshold) * sign(neighborContribution.rgb);

		if (srt.m_pConsts->m_isHdrMode)
			outColor = max(currentColor.rgb + neighborContribution.rgb * weightScale, 0.0f);
		else
			outColor = saturate(currentColor.rgb + neighborContribution.rgb * weightScale);
	}

	outColor = ApplyFilmGrain( srt.m_pTextures->m_txFilmGrain, srt.m_samplerPoint, srt.m_pConsts->m_filmGrainIntensity, srt.m_pConsts->m_filmGrainIntensity2, srt.m_pConsts->m_filmGrainOffsetScale, uv, outColor, true );

	// tint color.
	outColor = outColor * srt.m_pConsts->m_tintColorScaleVector.xyz + srt.m_pConsts->m_tintColorOffsetVector.xyz;

	if (!srt.m_pConsts->m_isHdrMode)
	{
		outColor = outColor < 0.03928f / 12.92f ? outColor * 12.92 : pow(outColor, 1.0/2.4) * 1.055 - 0.055f;
	}

	return float4(outColor, currentColor.a);
}

[numthreads(8, 8, 1)]
void Cs_Sharpen(uint3 dispatchThreadId : SV_DispatchThreadID, SharpenSrt srt : S_SRT_DATA)
{
	float4 currentColor = srt.m_pTextures->m_txSrc[dispatchThreadId.xy];
	srt.m_pTextures->m_txDst[dispatchThreadId.xy] = GetSharpenColor(dispatchThreadId, currentColor, true, srt);
}

[numthreads(8, 8, 1)]
void Cs_SharpenAfterDof(uint3 dispatchThreadId : SV_DispatchThreadID, SharpenSrt srt : S_SRT_DATA)
{
	// Only sharpen pixels that are in focus
	// presortInfo = (coc, background, foreground)
	float3 presortInfo = srt.m_pTextures->m_txDofPresort[dispatchThreadId / 2]; // presort is half-res
	// Only sharpen pixels that are in focus (or a tiny bit out of focus)
	float4 finalColor = srt.m_pTextures->m_txSrc[dispatchThreadId.xy];
	bool applySharpen = presortInfo.x < 0.8;
	finalColor = GetSharpenColor(dispatchThreadId, finalColor, applySharpen, srt);		

	srt.m_pTextures->m_txDst[dispatchThreadId.xy] = finalColor;
}

struct ApplyHdrCodingSrt
{
	float4					m_params;
	RWTexture2D<float4>		m_dstDisplay;
	Texture2D<float4>		m_srcTmpBuffer;
};

float3 GetSrgbColor(float3 iptColor)
{
	return iptColor < 0.0031308f ? iptColor * 12.92 : pow(iptColor, 1.0/2.4) * 1.055 - 0.055f;
}

[numthreads(8, 8, 1)]
void Cs_ApplyHdrCode(uint3 dispatchThreadId : SV_DispatchThreadID, ApplyHdrCodingSrt* pSrt : S_SRT_DATA)
{
	// input is in sRGB space
	float4 currentColor = pSrt->m_srcTmpBuffer[dispatchThreadId.xy];

	// apply the TV SDR EOTF, this is the curve the TV would apply if we were in SDR mode
	// TVs usually do a gamma 2.4 (BT 1886) but we give control to the user
	currentColor = pow(currentColor, pSrt->m_params.y);

	float3 tmpColor = mul(BT709_TO_BT2020, currentColor.xyz);

	//convert to nits. 1.0 = 300 nits is a good guess for modern TVs
	//maybe some calibration/tonemapping/display mapping here
	//OETF expects normalized L

	float4 outColor;
	outColor.xyz = applyPQ_OETF(tmpColor, pSrt->m_params.x);
	outColor.w = 1.0f;

	pSrt->m_dstDisplay[dispatchThreadId.xy] = outColor;
}


struct HistogramOnRegionSrt
{
	uint					m_tileX;
	uint					m_tileY;
	uint2					m_pad;
	uint4					m_checkBoxInfo;

	RWByteAddressBuffer		m_rwbAccBuffer;
	Texture2D<float4>		m_srcColorInfo; 
};

groupshared uint			g_brightnessSlots[64];
groupshared float			g_frequencyMulLuminance[64];

[numthreads(8, 8, 1)]
void Cs_HistogramPixelsOnScreenRegion(int2 dispatchThreadId : SV_DispatchThreadID, int2 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex, HistogramOnRegionSrt* pSrt : S_SRT_DATA)
{
	uint2 srcUv = dispatchThreadId.xy + pSrt->m_checkBoxInfo.xy;

	g_brightnessSlots[groupIndex] = 0;

	if (all(srcUv < pSrt->m_checkBoxInfo.zw))
	{
		float3 sRgbColor = pSrt->m_srcColorInfo[srcUv].xyz;
		float3 linearColor = pow(sRgbColor.xyz, 2.2f);
		float linearLuminance = dot(linearColor, s_luminanceVector);
		float sRgbLuminance = pow(linearLuminance, 1.f / 2.2f);

		uint slotIndex = uint(sRgbLuminance * 63.f);
		
		InterlockedAdd(g_brightnessSlots[slotIndex], 1);
	}

	pSrt->m_rwbAccBuffer.InterlockedAdd(groupIndex * 4, g_brightnessSlots[groupIndex]);
}

struct CalcuateAvgColorByPercentSrt
{
	uint					m_refNumSamples;
	uint					m_slotIdx;
	uint2					m_pad;
	
	RWBuffer<float>			m_rwbAvgPixelInfo;
	RWByteAddressBuffer		m_srcHistogramSlotInfo; 
};

[numthreads(64, 1, 1)]
void Cs_CalcuateAvgColorByPercent(int2 dispatchThreadId : SV_DispatchThreadID, int2 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex, CalcuateAvgColorByPercentSrt* pSrt : S_SRT_DATA)
{
	g_brightnessSlots[groupIndex] = pSrt->m_srcHistogramSlotInfo.Load(groupIndex * 4);
	
	float sRgbLuminance = (float)groupIndex / 63.f;
	float weightedLuma = pow(sRgbLuminance, (1.0f/3.0f));
	g_frequencyMulLuminance[groupIndex] = (float)g_brightnessSlots[groupIndex] * weightedLuma;
	
	GroupMemoryBarrierWithGroupSync();

	UNROLL
	for (uint numberOfOperands = 32; numberOfOperands > 0; numberOfOperands >>= 1)
	{
		if (groupIndex < numberOfOperands)
		{
			g_frequencyMulLuminance[groupIndex] += g_frequencyMulLuminance[groupIndex + numberOfOperands];
			g_brightnessSlots[groupIndex] += g_brightnessSlots[groupIndex + numberOfOperands];
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if (groupIndex == 0)
		pSrt->m_rwbAvgPixelInfo[pSrt->m_slotIdx] = g_frequencyMulLuminance[0] / (float)g_brightnessSlots[0];
}

struct HistogramOnScreenSrt
{
	RWByteAddressBuffer		m_rwbAccBufferR;
	RWByteAddressBuffer		m_rwbAccBufferG;
	RWByteAddressBuffer		m_rwbAccBufferB;
	Texture2D<float4>		m_srcColorInfo; 
};

groupshared uint			g_rChannelSlots[64];
groupshared uint			g_gChannelSlots[64];
groupshared uint			g_bChannelSlots[64];
groupshared float			g_frequencyMulIntensityR[64];
groupshared float			g_frequencyMulIntensityG[64];
groupshared float			g_frequencyMulIntensityB[64];

[numthreads(8, 8, 1)]
void Cs_HistogramPixelsOnScreen(int2 dispatchThreadId : SV_DispatchThreadID, int2 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex, HistogramOnScreenSrt* pSrt : S_SRT_DATA)
{
	g_rChannelSlots[groupIndex] = 0;
	g_gChannelSlots[groupIndex] = 0;
	g_bChannelSlots[groupIndex] = 0;

	float3 sRgbColor = pSrt->m_srcColorInfo[dispatchThreadId.xy].xyz;
	
	uint rSlotIndex = uint(sRgbColor.r * 63.f);
	uint gSlotIndex = uint(sRgbColor.g * 63.f);
	uint bSlotIndex = uint(sRgbColor.b * 63.f);
		
	InterlockedAdd(g_rChannelSlots[rSlotIndex], 1);
	InterlockedAdd(g_gChannelSlots[gSlotIndex], 1);
	InterlockedAdd(g_bChannelSlots[bSlotIndex], 1);

	pSrt->m_rwbAccBufferR.InterlockedAdd(groupIndex * 4, g_rChannelSlots[groupIndex]);
	pSrt->m_rwbAccBufferG.InterlockedAdd(groupIndex * 4, g_gChannelSlots[groupIndex]);
	pSrt->m_rwbAccBufferB.InterlockedAdd(groupIndex * 4, g_bChannelSlots[groupIndex]);
}

struct CalcuateAvgScreenColorByPercentSrt
{
	uint					m_numSamples;
	uint3					m_pad;

	RWBuffer<float3>		m_rwbAvgRgbPixelInfo;
	RWByteAddressBuffer		m_srcHistogramSlotInfoR;
	RWByteAddressBuffer		m_srcHistogramSlotInfoG; 
	RWByteAddressBuffer		m_srcHistogramSlotInfoB; 
};

[numthreads(64, 1, 1)]
void Cs_CalcuateAvgScreenColorByPercent(int2 dispatchThreadId : SV_DispatchThreadID, int2 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex, CalcuateAvgScreenColorByPercentSrt* pSrt : S_SRT_DATA)
{
	g_rChannelSlots[groupIndex] = pSrt->m_srcHistogramSlotInfoR.Load(groupIndex * 4);
	g_gChannelSlots[groupIndex] = pSrt->m_srcHistogramSlotInfoG.Load(groupIndex * 4);
	g_bChannelSlots[groupIndex] = pSrt->m_srcHistogramSlotInfoB.Load(groupIndex * 4);
	
	float rIntensity = (float)groupIndex / 63.f;
	float gIntensity = (float)groupIndex / 63.f;
	float bIntensity = (float)groupIndex / 63.f;

	g_frequencyMulIntensityR[groupIndex] = (float)g_rChannelSlots[groupIndex] * rIntensity;
	g_frequencyMulIntensityG[groupIndex] = (float)g_gChannelSlots[groupIndex] * gIntensity;
	g_frequencyMulIntensityB[groupIndex] = (float)g_bChannelSlots[groupIndex] * bIntensity;
	
	GroupMemoryBarrierWithGroupSync();

	UNROLL
	for (uint numberOfOperands = 32; numberOfOperands > 0; numberOfOperands >>= 1)
	{
		if (groupIndex < numberOfOperands)
		{
			g_frequencyMulIntensityR[groupIndex] += g_frequencyMulIntensityR[groupIndex + numberOfOperands];
			g_frequencyMulIntensityG[groupIndex] += g_frequencyMulIntensityG[groupIndex + numberOfOperands];
			g_frequencyMulIntensityB[groupIndex] += g_frequencyMulIntensityB[groupIndex + numberOfOperands];
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if (groupIndex == 0)
	{
		pSrt->m_rwbAvgRgbPixelInfo[0] = g_frequencyMulIntensityR[0] / (float)pSrt->m_numSamples;
		pSrt->m_rwbAvgRgbPixelInfo[1] = g_frequencyMulIntensityG[0] / (float)pSrt->m_numSamples;
		pSrt->m_rwbAvgRgbPixelInfo[2] = g_frequencyMulIntensityB[0] / (float)pSrt->m_numSamples;
	}
}

struct DsBuffer16thConsts
{
	float4					m_info; 
};

struct DsBuffer16thTextures
{
	RWTexture2D<float4>		avg_color;
	Texture2D<float4>		src_color;
};

struct Dsbuffer16thSamplers
{
	SamplerState			samplerLinear;	
};

struct DsBuffer16thSrt
{
	DsBuffer16thConsts		*consts;
	DsBuffer16thTextures	*texs;
	Dsbuffer16thSamplers	*smpls;
};

groupshared float4			g_srcColorInfo[64];

[numthreads(8, 8, 1)]
void Cs_DownSampleBufferby16th(uint2 dispatchThreadId : SV_DispatchThreadID, uint2 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex, DsBuffer16thSrt srt : S_SRT_DATA)
{
	float2 srcUv = (dispatchThreadId.xy + float2(0.5, 0.5)) * srt.consts->m_info.xy;

	g_srcColorInfo[groupIndex] = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, srcUv, 0);

	if (groupIndex < 32)
		g_srcColorInfo[groupIndex] += g_srcColorInfo[groupIndex + 32];

	if (groupIndex < 16)
		g_srcColorInfo[groupIndex] += g_srcColorInfo[groupIndex + 16];

	if (groupIndex < 8)
		g_srcColorInfo[groupIndex] += g_srcColorInfo[groupIndex + 8];

	if (groupIndex < 4)
		g_srcColorInfo[groupIndex] += g_srcColorInfo[groupIndex + 4];

	if (groupIndex < 2)
		g_srcColorInfo[groupIndex] += g_srcColorInfo[groupIndex + 2];

	if (groupIndex < 1)
	{
		float4 sumColorInfo = g_srcColorInfo[0] + g_srcColorInfo[1];
		sumColorInfo /= sumColorInfo.w;
		srt.texs->avg_color[groupId.xy] = sumColorInfo;
	}
}

struct ColorCopySrt
{
	RWTexture2D<float4>		dst_color;
	Texture2D<float4>		src_color;
	SamplerState			ssampler;	
	float4					params;
};

[numthreads(8, 8, 1)]
void Cs_ColorCopy(uint2 dispatchThreadId : SV_DispatchThreadID, ColorCopySrt* srt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId.xy + float2(0.5, 0.5)) * srt->params.zw;

	srt->dst_color[int2(dispatchThreadId.xy)] = srt->src_color.SampleLevel(srt->ssampler, uv, 0);
}

[numthreads(8, 8, 1)]
void Cs_ColorCopyGammaEncode(uint2 dispatchThreadId : SV_DispatchThreadID, ColorCopySrt* srt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId.xy + float2(0.5, 0.5)) * srt->params.zw;
	float4 finalColor = srt->src_color.SampleLevel(srt->ssampler, uv, 0);
	finalColor.rgb = finalColor.rgb < 0.03928f / 12.92f ? finalColor.rgb * 12.92f : pow(finalColor.rgb, 1.f / 2.4f) * 1.055f - 0.055f;

	srt->dst_color[int2(dispatchThreadId.xy)] = finalColor;
}

struct ColorBufferDuplicateSrt
{
	RWTexture2D<float4>		dst_color;
	Texture2D<float4>		src_color;
};

[numthreads(8, 8, 1)]
void Cs_ColorBufferDuplicate(uint2 dispatchThreadId : SV_DispatchThreadID, ColorBufferDuplicateSrt* srt : S_SRT_DATA)
{
	float4 srcColor = srt->src_color.Load(int3(dispatchThreadId.xy, 0));
	srt->dst_color[int2(dispatchThreadId.xy)] = srcColor;
}

[numthreads(8, 8, 1)]
void Cs_ColorCopyAlphaOne(uint2 dispatchThreadId : SV_DispatchThreadID, ColorBufferDuplicateSrt* srt : S_SRT_DATA)
{
	float3 srcColor = srt->src_color.Load(int3(dispatchThreadId.xy, 0)).rgb;
	srt->dst_color[int2(dispatchThreadId.xy)] = float4(srcColor, 1.f);
}

struct ColorCopy2Srt
{
	RWTexture2D<float4>		dst_color;
	RWTexture2D<float4>		dst_color1;
	Texture2D<float4>		src_color;
};

[numthreads(8, 8, 1)]
void Cs_ColorCopy2(uint2 dispatchThreadId : SV_DispatchThreadID, ColorCopy2Srt* srt : S_SRT_DATA)
{
	float4 srcColor = srt->src_color.Load(int3(dispatchThreadId.xy, 0));
	srt->dst_color[int2(dispatchThreadId.xy)] = srcColor;
	srt->dst_color1[int2(dispatchThreadId.xy)] = srcColor;
}

struct ColorCopy2Data
{
	RWTexture2D<float4>		dst_color;
	RWTexture2D<float4>		dst_color1;
	Texture2D<float4>		src_color;
};

struct ColorCopy2Srt_1
{
	ColorCopy2Data*			pData;
	int2					screenOffset;
};

[numthreads(8, 8, 1)]
void Cs_ColorCopy2_1(int2 localDispatchThreadId : SV_DispatchThreadID, ColorCopy2Srt_1 srt : S_SRT_DATA)
{
	int2 dispatchThreadId = localDispatchThreadId + srt.screenOffset;

	float4 srcColor = srt.pData->src_color.Load(int3(dispatchThreadId.xy, 0));
	srt.pData->dst_color[int2(dispatchThreadId.xy)] = srcColor;
	srt.pData->dst_color1[int2(dispatchThreadId.xy)] = srcColor;
}

struct CreateRefractionSrt
{
	uint					m_pixelScale;
	RWTexture2D<float3>		m_rwDstColor;
	Texture2D<float4>		m_srcColor;
};

[numthreads(8, 8, 1)]
void Cs_CreateRefractionBuffer(int2 dispatchThreadId : SV_DispatchThreadID, CreateRefractionSrt* pSrt : S_SRT_DATA)
{
	pSrt->m_rwDstColor[dispatchThreadId.xy] = pSrt->m_srcColor[dispatchThreadId.xy * pSrt->m_pixelScale].xyz;
}

struct UpscaleDowansampleConstCsSrt
{
	RWTexture2D<float4>		dst_color;
	Texture2D<float4>		cur_half_buffer;
	Texture2D<float4>		last_half_buffer;
	Texture2D<float2>		motion_blur_buffer;
	SamplerState			ssampler;	
	float4					params[2];
};

[numthreads(8, 8, 1)]
void Cs_MergeHalfScreenBuffer(uint2 dispatchThreadId : SV_DispatchThreadID, UpscaleDowansampleConstCsSrt* srt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId.xy + float2(0.5, 0.5)) * srt->params[0].zw;
	bool bIsCurrentPixel = (srt->params[1].x <= 0 && dispatchThreadId.x % 2 == 0) || (srt->params[1].x > 0 && dispatchThreadId.x % 2 == 1);

	float4 finalColor = srt->cur_half_buffer.Load(int3(dispatchThreadId.x / 2, dispatchThreadId.y, 0));
	if (bIsCurrentPixel == false)
	{
		float2 motionBlurUv = uv + srt->motion_blur_buffer.Load(int3(dispatchThreadId.x / 2, dispatchThreadId.y, 0));
		bool bValidUv = all(motionBlurUv >= 0.0f && motionBlurUv <= 1.0f);
		if (bValidUv)
		{
			int2 lastFrameScreenCoord = int2(motionBlurUv * srt->params[0].xy);
			float4 lastFrameColor = srt->last_half_buffer.Load(int3(lastFrameScreenCoord, 0));

			float4 colorDiff = abs(lastFrameColor - finalColor);
//			if (dot(colorDiff.xyz, colorDiff.xyz) < srt->params[1].y)
				finalColor = lastFrameColor;
		}
	}

	srt->dst_color[int2(dispatchThreadId.xy)] = finalColor;
}

struct MergeHalfDepthConstCsSrt
{
	RWTexture2D<float>		dst_depth;
	RWTexture2D<uint>		dst_nearby_depth_info;
	Texture2D<float>		first_half_depth;
	Texture2D<float>		second_half_depth;
	float4					params;
};

groupshared float g_depths[10][10];
[numthreads(8, 8, 1)]
void Cs_MergeHalfDepthBuffer(uint2 dispatchThreadId : SV_DispatchThreadID, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, MergeHalfDepthConstCsSrt* pSrt : S_SRT_DATA)
{
	bool bIsCurrentPixel = (pSrt->params.x <= 0 && dispatchThreadId.x % 2 == 0) || (pSrt->params.x > 0 && dispatchThreadId.x % 2 == 1);

	{
		int indexY = groupIndex / 10;
		int indexX = groupIndex % 10;
		int2 uvCoord = int2(groupId.xy) * 8 - 1 + int2(indexX, indexY);
		int3 loadaddr = int3(uvCoord.x / 2, uvCoord.y, 0);
		g_depths[indexY][indexX] = (indexX % 2 == 0) ? pSrt->second_half_depth.Load(loadaddr) : pSrt->first_half_depth.Load(loadaddr);
	}

	if (groupIndex < 36)
	{
		int indexY1 = (groupIndex + 64) / 10;
		int indexX1 = (groupIndex + 64) % 10;
		int2 uvCoord = int2(groupId.xy) * 8 - 1 + int2(indexX1, indexY1);
		int3 loadaddr = int3(uvCoord.x / 2, uvCoord.y, 0);
		g_depths[indexY1][indexX1] = (indexX1 % 2 == 0) ? pSrt->second_half_depth.Load(loadaddr) : pSrt->first_half_depth.Load(loadaddr);
	}

	float currentDepth = g_depths[groupThreadId.y + 1][groupThreadId.x + 1];

	uint depthInfo = 0x11;
	if (bIsCurrentPixel == false)
	{
		float depthDiff[6];
		depthDiff[0] = abs(g_depths[groupThreadId.y][groupThreadId.x] - currentDepth);
		depthDiff[1] = abs(g_depths[groupThreadId.y+1][groupThreadId.x] - currentDepth);
		depthDiff[2] = abs(g_depths[groupThreadId.y+2][groupThreadId.x] - currentDepth);
		depthDiff[3] = abs(g_depths[groupThreadId.y][groupThreadId.x+2] - currentDepth);
		depthDiff[4] = abs(g_depths[groupThreadId.y+1][groupThreadId.x+2] - currentDepth);
		depthDiff[5] = abs(g_depths[groupThreadId.y+2][groupThreadId.x+2] - currentDepth);

		float minDepthDiff0 = min3(depthDiff[0], depthDiff[1], depthDiff[2]);
		float minDepthDiff1 = min3(depthDiff[3], depthDiff[4], depthDiff[5]);
		float minDepthDiff = min(minDepthDiff0, minDepthDiff1);

		uint baseIndex = minDepthDiff == minDepthDiff1 ? 3 : 0;

		if (minDepthDiff == depthDiff[baseIndex+1])
			baseIndex += 1;
		else if (minDepthDiff == depthDiff[baseIndex+2])
			baseIndex += 2;

		depthInfo = ((baseIndex % 3) << 4) | ((baseIndex / 3) == 0 ? 0 : 2);
	}

	pSrt->dst_nearby_depth_info[int2(dispatchThreadId)] = depthInfo;
	pSrt->dst_depth[int2(dispatchThreadId)] = currentDepth;
}

// =============================================================================

const static int2 depthOffsets[5] = { int2(0, 0), int2(2, 0), int2(0,  2), int2(2, 2), int2(1, 1) };

// Faster but less accurate luma computation. 
// Luma includes a scaling by 4.
float Luma4(float3 Color)
{
	return (Color.g * 2.0f) + (Color.r + Color.b);
}

struct UpscaleDowansamplePostConstCsSrt
{
	RWTexture2D<float4>		dst_color;
	Texture2D<float4>		cur_half_buffer;
	Texture2D<float4>		last_half_buffer;
	Texture2D<float>		cur_depth;
	Texture2D<uint>			nearby_depth_info;
	Texture2D<float4>		accumulate_buffer;
	Texture2D<float2>		motion_blur_buffer;
	SamplerState			samplerPoint;
	SamplerState			samplerLinear;
	float4					params[2];
};

groupshared float3 g_colors[10][10];
groupshared uint g_stencils[10][10];
[numthreads(8, 8, 1)]
void Cs_MergeHalfScreenBufferPost(int2 dispatchThreadId : SV_DispatchThreadID, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, UpscaleDowansamplePostConstCsSrt* pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId.xy + float2(0.5, 0.5)) * pSrt->params[0].zw;
	bool bIsCurrentPixel = (pSrt->params[1].x <= 0 && dispatchThreadId.x % 2 == 0) || (pSrt->params[1].x > 0 && dispatchThreadId.x % 2 == 1);
	uint nearbyDepthInfo = pSrt->nearby_depth_info.Load(int3(dispatchThreadId.xy, 0));

	int offsetX = int(nearbyDepthInfo & 0x0f) - 1;
	int offsetY = int(nearbyDepthInfo >> 4) - 1;

	int2 halfDispatchThreadId = dispatchThreadId.xy + int2(offsetX, offsetY);
	float2 motionVector = pSrt->motion_blur_buffer.Load(int3(halfDispatchThreadId.x / 2, halfDispatchThreadId.y, 0));

	int2 baseThreadId = int2(groupId.xy) * int2(8, 8);
	{
		int arrayIdxY = groupIndex / 10;
		int arrayIdxX = groupIndex % 10;
		int srcIndexY = arrayIdxY + baseThreadId.y - 1;
		int srcIndexX = arrayIdxX + baseThreadId.x - 1;

		uint depthInfo = pSrt->nearby_depth_info.Load(int3(srcIndexX, srcIndexY, 0));
		int2 offset = int2(depthInfo & 0x0f, depthInfo >> 4) - 1;
		g_colors[arrayIdxY][arrayIdxX] = pSrt->cur_half_buffer.Load(int3((srcIndexX + offset.x) / 2, srcIndexY + offset.y, 0)).xyz;
	}

	if (groupIndex < 36)
	{
		int arrayIdxY = (groupIndex + 64) / 10;
		int arrayIdxX = (groupIndex + 64) % 10;
		int srcIndexY = arrayIdxY + baseThreadId.y - 1;
		int srcIndexX = arrayIdxX + baseThreadId.x - 1;

		uint depthInfo = pSrt->nearby_depth_info.Load(int3(srcIndexX, srcIndexY, 0));
		int2 offset = int2(depthInfo & 0x0f, depthInfo >> 4) - 1;
		g_colors[arrayIdxY][arrayIdxX] = pSrt->cur_half_buffer.Load(int3((srcIndexX + offset.x) / 2, srcIndexY + offset.y, 0)).xyz;
	}

	float3 finalColor = g_colors[groupThreadId.y+1][groupThreadId.x+1];
	{
		float currentDepth = pSrt->cur_depth.Load(int3(dispatchThreadId.xy, 0));
		float3 neighborMin = finalColor;
		float3 neighborMax = finalColor;

		neighborMin = min(neighborMin, g_colors[groupThreadId.y][groupThreadId.x]);
		neighborMax = max(neighborMax, g_colors[groupThreadId.y][groupThreadId.x]);

		neighborMin = min(neighborMin, g_colors[groupThreadId.y][groupThreadId.x+1]);
		neighborMax = max(neighborMax, g_colors[groupThreadId.y][groupThreadId.x+1]);

		neighborMin = min(neighborMin, g_colors[groupThreadId.y][groupThreadId.x+2]);
		neighborMax = max(neighborMax, g_colors[groupThreadId.y][groupThreadId.x+2]);

		neighborMin = min(neighborMin, g_colors[groupThreadId.y+1][groupThreadId.x]);
		neighborMax = max(neighborMax, g_colors[groupThreadId.y+1][groupThreadId.x]);

		neighborMin = min(neighborMin, g_colors[groupThreadId.y+1][groupThreadId.x+2]);
		neighborMax = max(neighborMax, g_colors[groupThreadId.y+1][groupThreadId.x+2]);

		neighborMin = min(neighborMin, g_colors[groupThreadId.y+2][groupThreadId.x]);
		neighborMax = max(neighborMax, g_colors[groupThreadId.y+2][groupThreadId.x]);

		neighborMin = min(neighborMin, g_colors[groupThreadId.y+2][groupThreadId.x+1]);
		neighborMax = max(neighborMax, g_colors[groupThreadId.y+2][groupThreadId.x+1]);

		neighborMin = min(neighborMin, g_colors[groupThreadId.y+2][groupThreadId.x+2]);
		neighborMax = max(neighborMax, g_colors[groupThreadId.y+2][groupThreadId.x+2]);

		float lumaMin = Luma4(neighborMin.rgb);
		float lumaMax = Luma4(neighborMax.rgb);
		float lumaContrast = lumaMax - lumaMin;

/*		float4 neighborDepths = pSrt->cur_depth.GatherRed(pSrt->samplerPoint, uv, depthOffsets[0], depthOffsets[1], depthOffsets[2], depthOffsets[3]);
		float minDepth = currentDepth;
		uint minDepthIndex = 4;
		for (uint i = 0; i < 4; i++)
		{
			if (neighborDepths[i] < minDepth)
			{
				minDepth = neighborDepths[i];
				minDepthIndex = i;
			}
		}

		float2 motionVector = pSrt->motion_blur_buffer.SampleLevel(pSrt->samplerLinear, uv + depthOffsets[minDepthIndex] * (1.0f / 960.0f, 1.0f / 1080.0f), 0);*/
		float2 backTemp = motionVector * float2(2.f, -2.f) * pSrt->params[0].xy;
		float historyBlurAmp = 2.f;
		float historyBlur = saturate(abs(backTemp.x) * historyBlurAmp + abs(backTemp.y) * historyBlurAmp);

		float2 lastFrameUv = uv + motionVector;
		float3 outColor = pSrt->accumulate_buffer.SampleLevel(pSrt->samplerLinear, lastFrameUv, 0).rgb;

		float lumaHistory = Luma4(outColor.rgb);
		outColor = clamp(outColor, neighborMin, neighborMax);

		lumaHistory = min(abs(lumaMin - lumaHistory), abs(lumaMax - lumaHistory));
		float historyAmount = (1.f / 8.f) + historyBlur * (1.f / 8.f);
		float historyFactor = lumaHistory * historyAmount * (1.f + historyBlur * historyAmount * 8.f);
		float blendFinal = saturate(historyFactor * rcp(lumaHistory + lumaContrast));

		finalColor = lerp(outColor, finalColor, blendFinal);
	}

	pSrt->dst_color[int2(dispatchThreadId.xy)] = float4(finalColor, 0);
}

struct MergeSplitBufferConstCsSrt
{
	RWTexture2D<float4>			m_dstColor;
	Texture2D<float4>			m_halfBuffer;
	uint4						m_params;
};

[numthreads(8, 8, 1)]
void Cs_MergeSplitScreenBufferPost(int2 dispatchThreadId : SV_DispatchThreadID, MergeSplitBufferConstCsSrt* pSrt : S_SRT_DATA)
{
	float4 finalColor = 0;
	if (dispatchThreadId.x >= pSrt->m_params.x && dispatchThreadId.x < pSrt->m_params.x + pSrt->m_params.z)
		finalColor = pSrt->m_halfBuffer[int2(dispatchThreadId.x - pSrt->m_params.x, dispatchThreadId.y)];

	pSrt->m_dstColor[int2(dispatchThreadId.x, dispatchThreadId.y + pSrt->m_params.y)] = finalColor;
}

// =============================================================================

struct CompositeDownsampledFlaresSrt
{
	RWTexture2D<float4>		dst_color;
	Texture2D<float4>		src_color;
	SamplerState			ssampler;	
	float4					params;
};

[numthreads(8, 8, 1)]
void Cs_CompositeDownsampledFlares(uint2 dispatchId : SV_DispatchThreadID, CompositeDownsampledFlaresSrt* pSrt : S_SRT_DATA)
{
	float2	bufferScale = pSrt->params.xy;
	float2	uv = (dispatchId.xy + float2(0.5, 0.5)) * bufferScale;

	float4	srcColor = pSrt->src_color.SampleLevel(pSrt->ssampler, uv, 0);
	float4	dstColor = pSrt->dst_color[int2(dispatchId.xy)];

	float	alpha = saturate(srcColor.a * 100.0f);

	pSrt->dst_color[int2(dispatchId.xy)] = srcColor + dstColor;
}

// =============================================================================

enum TemporalAaMethods	// sync with render-options.h
{
	kTemporalAaUE4,
	kTemporalAaUE4Hybrid,
	kTemporalAax2,
	kTemporalAaQuincunx,
	kTemporalAaTFQ
};

struct TemporalAaSharedData
{
	float4x4				m_currentToLastFrameMat;

	RWTexture2D<float4>		dst_color;
	RWTexture2D<float4>		m_dstPrimaryFloat;
	Texture2D<float4>		cur_color_buffer;
	Texture2D<float>		cur_depth_buffer;
	Texture2D<float>		last_depth_buffer;
	Texture2D<uint>			cur_dilated_stencil_buffer;
	Texture2D<float4>		accumulate_buffer;
	Texture2D<float2>		motion_vector_buffer;
	Texture2D<uint>			m_depthInfoBuffer;
	Texture2D<uint>			cur_stencil_buffer;
	Texture2D<uint>			last_stencil_buffer;
	Texture2D<float3>		dof_min_max_coc_buffer;
	SamplerState			samplerPoint;
	SamplerState			samplerLinear;

	float4					params;
	float2					m_depthParams;
	float2					m_projectionJitterOffsets;
	int						taaMethod;
	int						useVarianceClamping;
	int						useDepthAwareClamping;
	int						showJitteredFrame;
	int						m_accumulateCount;
	int						m_frameIdx;
	int						m_hasBokehDof;
	int						m_isHalfResTaa;
	int						showClampedPixels;
	int						m_showNonBlendedPixels;
	int						m_showPixelsWithGhostingIdDifference;
	int						m_showEdgePixels;
	float					distanceParam;
	float					m_healthParamDesat;
	float					m_bokehDofThreshold;
	float					m_varianceClampingGamma;
};

struct TemporalAaConstCsSrt
{
	TemporalAaSharedData*	pData;
	int2					screenOffset;
};

struct TemporalAaDepthData
{
	float4x4				m_currentToLastFrameMat;

	RWTexture2D<float>		dst_depth;
	Texture2D<float>		cur_depth_buffer;
	Texture2D<float>		accumulate_buffer;
	Texture2D<float2>		motion_vector_buffer;

	SamplerState			samplerPoint;
	SamplerState			samplerLinear;

	float4					params;
	float2					m_depthParams;
	float2					m_projectionJitterOffsets;
	int						useVarianceClamping;
	float					m_varianceClampingGamma;
};

struct TemporalAaDepthCsSrt
{
	TemporalAaDepthData*	pData;
	int2					screenOffset;
};

groupshared float g_colors_x[10][10];
groupshared float g_colors_y[10][10];
groupshared float g_colors_z[10][10];

void SetColor(int y, int x, float3 value)
{
	g_colors_x[y][x] = value.x;
	g_colors_y[y][x] = value.y;
	g_colors_z[y][x] = value.z;
}

float3 GetColor(int y, int x)
{
	return float3(g_colors_x[y][x], g_colors_y[y][x], g_colors_z[y][x]);
}

static const uint2 colorOffsets[8] = {uint2(0, 0), uint2(0, 1), uint2(0, 2), uint2(1, 0), uint2(1, 2), uint2(2, 0), uint2(2, 1), uint2(2, 2)};
static const float gaussianWeights[8] = {0.0625f, 0.125f, 0.0625f, 0.125f, 0.125f, 0.0625f, 0.125f, 0.0625f};

template<typename T, typename S>
void TemporalAA(int2 localDispatchThreadId : SV_DispatchThreadID, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, int groupIndex : SV_GroupIndex, S srt : S_SRT_DATA)
{
	int2 dispatchThreadId = localDispatchThreadId + srt.screenOffset;

	int2 baseThreadId = groupId.xy * int2(8, 8) + srt.screenOffset;
	{
		int arrayIdxY = groupIndex / 10;
		int arrayIdxX = groupIndex % 10;
		int srcIndexY = arrayIdxY + baseThreadId.y - 1;
		int srcIndexX = arrayIdxX + baseThreadId.x - 1;
		int2 srcIndexXy = clamp(int2(srcIndexX, srcIndexY), 0, int2(srt.pData->params.xy) - 1); // aarora: params.xy -> dimensions in pixels

#ifndef DEPTH
		SetColor(arrayIdxY, arrayIdxX, srt.pData->cur_color_buffer[srcIndexXy].xyz);
#endif

#if defined PHOTO_MODE || defined DEPTH
		g_depths[arrayIdxY][arrayIdxX] = srt.pData->cur_depth_buffer[srcIndexXy];
#endif
	}

	if (groupIndex < 36)
	{
		int arrayIdxY = (groupIndex + 64) / 10;
		int arrayIdxX = (groupIndex + 64) % 10;
		int srcIndexY = arrayIdxY + baseThreadId.y - 1;
		int srcIndexX = arrayIdxX + baseThreadId.x - 1;
		int2 srcIndexXy = clamp(int2(srcIndexX, srcIndexY), 0, int2(srt.pData->params.xy) - 1);

#ifndef DEPTH
		SetColor(arrayIdxY, arrayIdxX, srt.pData->cur_color_buffer[srcIndexXy].xyz);
#endif

#if defined PHOTO_MODE || defined DEPTH
		g_depths[arrayIdxY][arrayIdxX] = srt.pData->cur_depth_buffer[srcIndexXy];
#endif
	}

	int bIsHalfRes = false;
#ifndef DEPTH
	bIsHalfRes = srt.pData->m_isHalfResTaa;
#endif

	float2 uv = (dispatchThreadId.xy + float2(0.5f, 0.5f)) * srt.pData->params.zw;
	
	float2 fullResUv = uv;
	if (bIsHalfRes)
	{
		fullResUv = (2 * dispatchThreadId.xy + float2(0.5f, 0.5f)) * srt.pData->params.zw * 0.5f;
	}

#ifdef DEPTH
	T currentValue = g_depths[groupThreadId.y + 1][groupThreadId.x + 1];
#else 
	T currentValue = GetColor(groupThreadId.y + 1, groupThreadId.x + 1);
	T filteredCurrentValue = 0.25f * currentValue;
#endif

#ifndef DEPTH
	float4 finalColor = float4(currentValue, 1.f);
	float4 temporalAaColor = finalColor;

	// In the main TAA pass, we don't want to re-apply TAA on pixels that already had TAA from the DoF passes (since DoF does its own TAA).
	bool bUseDofColor = false;
	if (srt.pData->m_hasBokehDof)
	{
		float tileMinCoc = srt.pData->dof_min_max_coc_buffer.SampleLevel(srt.pData->samplerPoint, uv, 0).x;
		// If the min coc of the tile is greater than a certain threshold, it would certainly use the DoF blurred color (already TAA'd).
		if (tileMinCoc > srt.pData->m_bokehDofThreshold)
		{
			bUseDofColor = true;
		}
	}
#endif

#ifndef DEPTH
	if (!bUseDofColor)
	{
#endif
		T neighbors[8];
		T m1 = currentValue;
		T m2 = currentValue * currentValue;

		for (uint i = 0; i < 8; i++)
		{
			uint2 coords = groupThreadId.yx + colorOffsets[i];
#ifdef DEPTH
			T currentNeighbor = g_depths[coords.x][coords.y];
#else
			T currentNeighbor = GetColor(coords.x, coords.y);
			filteredCurrentValue += gaussianWeights[i] * currentNeighbor;
#endif
			neighbors[i] = currentNeighbor;
			m1 += currentNeighbor;
			m2 += currentNeighbor * currentNeighbor;
		}

		T neighborMin, neighborMax;
		T neighborMin2 = min(min(neighbors[0], neighbors[2]), min(neighbors[5], neighbors[7]));
		T neighborMax2 = max(max(neighbors[0], neighbors[2]), max(neighbors[5], neighbors[7]));
		neighborMin = min(min(min(neighbors[1], neighbors[3]), min(currentValue, neighbors[4])), neighbors[6]);
		neighborMax = max(max(max(neighbors[1], neighbors[3]), max(currentValue, neighbors[4])), neighbors[6]);
		neighborMin2 = min(neighborMin2, neighborMin);
		neighborMax2 = max(neighborMax2, neighborMax);
		neighborMin = neighborMin * 0.5f + neighborMin2 * 0.5f;
		neighborMax = neighborMax * 0.5f + neighborMax2 * 0.5f;

		// An Excursion in Temporal Supersampling. Marco Salvi.
		// Note: Variance clamping has almost same quality than variance clipping but at a lower cost.
		if (srt.pData->useVarianceClamping > 0)
		{
			m1 /= 9.0f;
			m2 /= 9.0f;
			T sigma = sqrt(abs(m2 - m1 * m1));
			neighborMin = max(neighborMin, m1 - sigma * srt.pData->m_varianceClampingGamma);
			neighborMax = min(neighborMax, m1 + sigma * srt.pData->m_varianceClampingGamma);
		}

#ifdef DEPTH
		float lumaMin = neighborMin;
		float lumaMax = neighborMax;
#else
		float lumaMin = Luma4(neighborMin.rgb);
		float lumaMax = Luma4(neighborMax.rgb);
#endif
		float lumaContrast = lumaMax - lumaMin;

#ifndef DEPTH
		uint depthInfo = srt.pData->m_depthInfoBuffer.SampleLevel(srt.pData->samplerPoint, fullResUv, 0);
		bool bEdge = depthInfo & 0x80;
		int2 minDepthOffset = int2(depthInfo, depthInfo >> 2) & 0x03;
#endif

#if defined PHOTO_MODE
#ifdef DEPTH
		float2 uvSS = (dispatchThreadId.xy + float2(0.5, 0.5)) * srt.pData->params.zw;
#else
		float2 uvSS = (dispatchThreadId.xy + minDepthOffset - 1 + float2(0.5, 0.5)) * srt.pData->params.zw;
#endif
		float ssX = (uvSS.x - 0.5f) * 2.0f;
		float ssY = (uvSS.y - 0.5f) * -2.0f;
#ifdef DEPTH
		float currentDepth = currentValue;
		float currentDepthVS = GetDepthVS(currentDepth, srt.pData->m_depthParams);
#else
		float currentDepth = g_depths[groupThreadId.y + minDepthOffset.y][groupThreadId.x + minDepthOffset.x];
		float currentDepthVS = GetDepthVS(currentDepth, srt.pData->m_depthParams);
#endif
		float4 inputPositionSS = float4(ssX, ssY, currentDepth, 1.0f) * currentDepthVS;
		float4 lastPositionSS = mul(inputPositionSS, srt.pData->m_currentToLastFrameMat);
		lastPositionSS.xy = lastPositionSS.xy / lastPositionSS.w * float2(0.5f, -0.5f) + 0.5f;
		float2 motionVector = lastPositionSS.xy - uvSS + srt.pData->m_projectionJitterOffsets;
#else
#ifdef DEPTH
		float2 motionVector = srt.pData->motion_vector_buffer.SampleLevel(srt.pData->samplerPoint, uv, 0);
#else
		float2 motionVector = srt.pData->motion_vector_buffer.SampleLevel(srt.pData->samplerPoint, fullResUv, 0, minDepthOffset - 1);
#endif
#endif

		float2 backTemp = motionVector * float2(2.f, -2.f) * srt.pData->params.xy;
		float historyBlurAmp = 2.f;
		float historyBlur = saturate(historyBlurAmp * (abs(backTemp.x) + abs(backTemp.y)));

		float2 lastFrameUv = uv + motionVector;
		float2 lastFrameFullResUv = fullResUv + motionVector;

#ifdef DEPTH
		T accumVal = srt.pData->accumulate_buffer.SampleLevel(srt.pData->samplerLinear, lastFrameUv, 0).r;
		float lumaHistory = accumVal;
#else
		T accumVal = srt.pData->accumulate_buffer.SampleLevel(srt.pData->samplerLinear, lastFrameUv, 0).rgb;
		float lumaHistory = Luma4(accumVal);
#endif

		lumaHistory = min(abs(lumaMin - lumaHistory), abs(lumaMax - lumaHistory));
		float historyAmount = (1.f / 8.f) + historyBlur * (1.f / 8.f);
		float historyFactor = lumaHistory * historyAmount * (1.f + historyBlur * historyAmount * 8.f);
		float blendFinal = saturate(historyFactor * rcp(lumaHistory + lumaContrast));

#ifdef DEPTH
		T outValue = clamp(accumVal, neighborMin, neighborMax);
		float temporalDepth = lerp(outValue, currentValue, blendFinal);

		srt.pData->dst_depth[dispatchThreadId.xy] = temporalDepth;
#else
		uint lastFgInfo = srt.pData->last_stencil_buffer.SampleLevel(srt.pData->samplerPoint, lastFrameFullResUv, 0) & 0x18;
		uint curFgInfo;
		if (bIsHalfRes)
		{
			// Don't use dilated stencil for half res
			curFgInfo = srt.pData->cur_stencil_buffer.SampleLevel(srt.pData->samplerPoint, fullResUv, 0) & 0x18;
		}
		else
		{
			curFgInfo = srt.pData->cur_dilated_stencil_buffer.SampleLevel(srt.pData->samplerPoint, uv, 0) & 0x18;
		}
		
		bool bUseCurrent = (lastFgInfo != curFgInfo && !bEdge) || (lastFrameUv.x < 0.f || lastFrameUv.x > 1.f || lastFrameUv.y < 0.f || lastFrameUv.y > 1.f);

		int taaMethod = srt.pData->taaMethod;

		bool shouldClamp = true;
		float curDepth = 0.f;

		if (taaMethod == kTemporalAaUE4Hybrid || srt.pData->useDepthAwareClamping)
		{
			float const clampThreshold = 0.001f;
			curDepth = srt.pData->cur_depth_buffer.SampleLevel(srt.pData->samplerLinear, uv, 0).r;
			float lastDepth = srt.pData->last_depth_buffer.SampleLevel(srt.pData->samplerLinear, lastFrameUv, 0).r;
			shouldClamp = (curDepth == 1.f) || (abs(curDepth - lastDepth) > clampThreshold) || any(isnan(accumVal));
		}

		T clampValue = clamp(accumVal, neighborMin, neighborMax);

		float4 srcColor;
		if (taaMethod == kTemporalAaUE4Hybrid)
		{
			// choose either ue4 or x2 based on distance
			if (curDepth < srt.pData->distanceParam)
			{
				taaMethod = kTemporalAaUE4;
			}
			else
			{
				taaMethod = kTemporalAax2;
			}
		}

		if (bIsHalfRes)
		{
			// HACK: changing the current value at all and not changing depth will mess up DoF. Workaround for now; may be jittery.
			filteredCurrentValue = currentValue;
		}

		if (taaMethod == kTemporalAax2)
		{
			T outValue = (shouldClamp) ? clampValue : accumVal;
			blendFinal = 0.5f;	// equal weight
			srcColor = float4(bUseCurrent ? filteredCurrentValue : lerp(outValue, currentValue, blendFinal), 1.f);
			temporalAaColor = float4(currentValue, 1.f);
			finalColor = srcColor;
		}
		else // if (taaMethod == kTemporalAaUE4)
		{
			T outValue = (shouldClamp || !srt.pData->useDepthAwareClamping) ? clampValue : accumVal;
			srcColor = float4(bUseCurrent ? filteredCurrentValue : lerp(outValue, currentValue, blendFinal), 1.f);
			temporalAaColor = srcColor;
			finalColor = srcColor;
		}

		if (srt.pData->m_showNonBlendedPixels)
		{
			finalColor.rgb = (lastFgInfo != curFgInfo && !bEdge) ? float3(0.f, 1.f, 0.f) : (finalColor.rgb * float3(1.f, 0.f, 0.f));
		}
		else
		{
			if (srt.pData->m_showPixelsWithGhostingIdDifference)
			{
				finalColor.rgb = (lastFgInfo != curFgInfo) ? float3(0.f, 1.f, 0.f) : (finalColor.rgb * float3(1.f, 0.f, 0.f));
			}
			if (srt.pData->m_showEdgePixels)
			{
				finalColor.rgb = (bEdge) ? float3(0.f, 1.f, 0.f) : (finalColor.rgb * float3(1.f, 0.f, 0.f));
			}
		}

#ifndef DEPTH
	}	// if (!bUseDofColor)
#endif

	if (srt.pData->m_healthParamDesat > 0)
	{
		finalColor.rgb = ApplyHealthEffectDesat(srt.pData->m_healthParamDesat, finalColor.rgb);
	}
	finalColor.rgb = (srt.pData->showJitteredFrame > 0) ? currentValue : finalColor.rgb;

	if (srt.pData->showClampedPixels)
	{
		if (srt.pData->taaMethod == kTemporalAax2)
		{
			finalColor.rgb = float3(1.f, 0.f, 0.f);
		}
		else
		{
			finalColor.rgb = float3(0.f, 1.f, 0.f);
		}
	}

	srt.pData->dst_color[dispatchThreadId.xy] = temporalAaColor;
	srt.pData->m_dstPrimaryFloat[int2(dispatchThreadId.xy)] = finalColor;
#endif
}

#ifdef DEPTH
[numthreads(8, 8, 1)]
void Cs_TemporalAADepth(int2 localDispatchThreadId : SV_DispatchThreadID, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, int groupIndex : SV_GroupIndex, TemporalAaDepthCsSrt srt : S_SRT_DATA)
{
	TemporalAA<float, TemporalAaDepthCsSrt>(localDispatchThreadId, groupId, groupThreadId, groupIndex, srt);
}
#else
[numthreads(8, 8, 1)]
void Cs_TemporalAA(int2 localDispatchThreadId : SV_DispatchThreadID, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, int groupIndex : SV_GroupIndex, TemporalAaConstCsSrt srt : S_SRT_DATA)
{
	TemporalAA<float3, TemporalAaConstCsSrt>(localDispatchThreadId, groupId, groupThreadId, groupIndex, srt);
}
#endif

struct TemporalAaAccumulateSrt
{
	RWTexture2D<float3>		m_rwDstColor;
	RWTexture2D<float>		m_rwDstAlpha;
	RWTexture2D<float3>		m_rwDstAccumulateColor;
	RWTexture2D<float>		m_rwDstAccumulateAlpha;
	Texture2D<float3>		m_txSrcColor;
	Texture2D<float>		m_txSrcAlpha;
	Texture2D<float3>		m_txSrcAccumulateColor;
	Texture2D<float>		m_txSrcAccumulateAlpha;
	float					m_accumulateRatio;
	float3					m_pad;
};

[numthreads(8, 8, 1)]
void Cs_TemporalAaAccumulate(int2 dispatchThreadId : SV_DispatchThreadID, TemporalAaAccumulateSrt* pSrt : S_SRT_DATA)
{
	float3 temporalAaColor = pSrt->m_txSrcColor[dispatchThreadId.xy];
	float temporalAaAlpha = pSrt->m_txSrcAlpha[dispatchThreadId.xy];
	if (pSrt->m_accumulateRatio < 1.0f)
	{
		temporalAaColor = lerp(pSrt->m_txSrcAccumulateColor[dispatchThreadId.xy], temporalAaColor, pSrt->m_accumulateRatio);
		temporalAaAlpha = lerp(pSrt->m_txSrcAccumulateAlpha[dispatchThreadId.xy], temporalAaAlpha, pSrt->m_accumulateRatio);
	}

	pSrt->m_rwDstAccumulateColor[dispatchThreadId.xy] = temporalAaColor;
	pSrt->m_rwDstAccumulateAlpha[dispatchThreadId.xy] = temporalAaAlpha;
	pSrt->m_rwDstColor[dispatchThreadId.xy] = temporalAaColor;
	pSrt->m_rwDstAlpha[dispatchThreadId.xy] = temporalAaAlpha;
}

struct ColorCopyArraySrt
{
	RWTexture2D<float4>		dst_color;
	Texture2DArray<float4>	src_color;
	SamplerState			ssampler;	
	float4					params;
};

[numthreads(8, 8, 1)]
void Cs_ColorCopyArray(uint2 dispatchThreadId : SV_DispatchThreadID, ColorCopyArraySrt* srt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId.xy + float2(0.5, 0.5)) * srt->params.zw;

	srt->dst_color[int2(dispatchThreadId.xy)] = srt->src_color.SampleLevel(srt->ssampler, float3(uv, srt->params.x), 0);
}

struct LDSampleTestSrt
{
	RWTexture2D<float4>	rwtOutput;
	Texture3D<float2> tSampleUvs;
	uint sampleUvsTextureWidth;
	uint numSamplesPerPixel;
	uint outputResolution;
};

[numthreads(1, 1, 1)]	//these numbers are for testing purpose only, will use full wavefront in real code
void Cs_LDSampleTest(uint dispatchThreadId : SV_DispatchThreadID, LDSampleTestSrt* pSrt : S_SRT_DATA)
{
	uint2 sampleCoord = uint2(dispatchThreadId % pSrt->sampleUvsTextureWidth, dispatchThreadId / pSrt->sampleUvsTextureWidth);

	for (uint i = 0; i < pSrt->numSamplesPerPixel; i++)
	{ 
		float2 uv = pSrt->tSampleUvs.Load(int4(sampleCoord, i, 0));
		pSrt->rwtOutput[uint2(uv * pSrt->outputResolution)] = float4(0.f, 0.f, 0.f, 0.f);
	}
}

struct CreateMinMaxMapsSrt
{
	uint						m_bufWidth;
	uint						m_bufHeight;
	float2						m_invBufSize;
	uint						m_slotIndex;
	uint3						m_pad;
//	RW_Texture2D_Array<float2>	m_minMaxMap0;
//	RW_Texture2D_Array<float2>	m_minMaxMap1;
	RW_Texture2D_Array<float2>	m_minMaxMap2;
	RW_Texture2D_Array<float2>	m_minMaxMap3;
	Texture2DArray<float>		m_srcDepthMap;
	SamplerState				m_ssampler;
};

[NUM_THREADS(8, 8, 1)]
void Cs_CreateMinMaxMaps(uint2 dispatchId : SV_DispatchThreadID, uint2 groupThreadId : SV_GroupThreadId, CreateMinMaxMapsSrt* pSrt : S_SRT_DATA)
{
	if (dispatchId.x < pSrt->m_bufWidth && dispatchId.y < pSrt->m_bufHeight)
	{
		float3 uv = float3(dispatchId.xy * pSrt->m_invBufSize, pSrt->m_slotIndex);

		float3 depth0 = pSrt->m_srcDepthMap.Gather(pSrt->m_ssampler, uv).wzx;
		float2 depth1 = pSrt->m_srcDepthMap.Gather(pSrt->m_ssampler, uv, int2(1, 0)).zy;
		float3 depth2 = pSrt->m_srcDepthMap.Gather(pSrt->m_ssampler, uv, int2(0, 1)).zxy;
		float  depth3 = pSrt->m_srcDepthMap.Gather(pSrt->m_ssampler, uv, int2(1, 1)).y;

		float3 minDepth3 = min3(depth0, depth2, float3(depth1, depth3));
		float3 maxDepth3 = max3(depth0, depth2, float3(depth1, depth3));

		float minDepth = min3(minDepth3.x, minDepth3.y, minDepth3.z);
		float maxDepth = max3(maxDepth3.x, maxDepth3.y, maxDepth3.z);

		float min_01 = min(minDepth, LaneSwizzle(minDepth, 0x1f, 0x00, 0x01));
		float max_01 = max(maxDepth, LaneSwizzle(maxDepth, 0x1f, 0x00, 0x01));
		float mipMin1 = min(min_01, LaneSwizzle(min_01, 0x1f, 0x00, 0x08));
		float mipMax1 = max(max_01, LaneSwizzle(max_01, 0x1f, 0x00, 0x08));

		float min_02 = min(mipMin1, LaneSwizzle(mipMin1, 0x1f, 0x00, 0x02));
		float max_02 = max(mipMax1, LaneSwizzle(mipMax1, 0x1f, 0x00, 0x02));
		float mipMin2 = min(min_02, LaneSwizzle(min_02, 0x1f, 0x00, 0x10));
		float mipMax2 = max(max_02, LaneSwizzle(max_02, 0x1f, 0x00, 0x10));

		float min_04 = min(mipMin2, LaneSwizzle(mipMin2, 0x1f, 0x00, 0x04));
		float max_04 = max(mipMax2, LaneSwizzle(mipMax2, 0x1f, 0x00, 0x04));
		float mipMin3 = min(min_04, ReadLane(min_04, 0x20));
		float mipMax3 = max(max_04, ReadLane(max_04, 0x20));

		//pSrt->m_minMaxMap0[int3(dispatchId.xy, pSrt->m_slotIndex)] = float2(minDepth, maxDepth);

		//if (groupThreadId.x % 2 == 0 && groupThreadId.y % 2 == 0)
		//	pSrt->m_minMaxMap1[int3(dispatchId.xy/2, pSrt->m_slotIndex)] = float2(mipMin1, mipMax1);

		if (groupThreadId.x % 4 == 0 && groupThreadId.y % 4 == 0)
			pSrt->m_minMaxMap2[int3(dispatchId.xy/4, pSrt->m_slotIndex)] = float2(mipMin2, mipMax2);

		if (groupThreadId.x == 0 && groupThreadId.y == 0)
			pSrt->m_minMaxMap3[int3(dispatchId.xy/8, pSrt->m_slotIndex)] = float2(mipMin3, mipMax3);
	}
}

struct CreateDownSampleSrt
{
	float2						params;
	float						maxClamp;
	float						pad;
	RWTexture2D<float4>			rwb_downSample;
	Texture2D<float4>			src_color;
	SamplerState				samplerLinear;	
};

[numthreads(8, 8, 1)]
void Cs_CreateDownSampleBuffer(uint2 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, CreateDownSampleSrt* pSrt : S_SRT_DATA)
{
	float2 uv = saturate(dispatchThreadId.xy * pSrt->params.xy);

	float3 srcColor = pSrt->src_color.SampleLevel(pSrt->samplerLinear, uv, 0).xyz;

	srcColor = min(srcColor, pSrt->maxClamp);

	float3 sumColor_01, sumColor_02, sumColor_04, sumColor_08, sumColor_10;
	sumColor_01.x = srcColor.x + LaneSwizzle(srcColor.x, 0x1f, 0x00, 0x01);
	sumColor_01.y = srcColor.y + LaneSwizzle(srcColor.y, 0x1f, 0x00, 0x01);
	sumColor_01.z = srcColor.z + LaneSwizzle(srcColor.z, 0x1f, 0x00, 0x01);
	sumColor_02.x = sumColor_01.x + LaneSwizzle(sumColor_01.x, 0x1f, 0x00, 0x02);
	sumColor_02.y = sumColor_01.y + LaneSwizzle(sumColor_01.y, 0x1f, 0x00, 0x02);
	sumColor_02.z = sumColor_01.z + LaneSwizzle(sumColor_01.z, 0x1f, 0x00, 0x02);
	sumColor_04.x = sumColor_02.x + LaneSwizzle(sumColor_02.x, 0x1f, 0x00, 0x04);
	sumColor_04.y = sumColor_02.y + LaneSwizzle(sumColor_02.y, 0x1f, 0x00, 0x04);
	sumColor_04.z = sumColor_02.z + LaneSwizzle(sumColor_02.z, 0x1f, 0x00, 0x04);
	sumColor_08.x = sumColor_04.x + LaneSwizzle(sumColor_04.x, 0x1f, 0x00, 0x08);
	sumColor_08.y = sumColor_04.y + LaneSwizzle(sumColor_04.y, 0x1f, 0x00, 0x08);
	sumColor_08.z = sumColor_04.z + LaneSwizzle(sumColor_04.z, 0x1f, 0x00, 0x08);
	sumColor_10.x = sumColor_08.x + LaneSwizzle(sumColor_08.x, 0x1f, 0x00, 0x10);
	sumColor_10.y = sumColor_08.y + LaneSwizzle(sumColor_08.y, 0x1f, 0x00, 0x10);
	sumColor_10.z = sumColor_08.z + LaneSwizzle(sumColor_08.z, 0x1f, 0x00, 0x10);

	if (groupThreadId.x == 0 && groupThreadId.y == 0)
	{
		float3 sumColor;
		sumColor.x = sumColor_10.x + ReadLane(sumColor_10.x, 0x20);
		sumColor.y = sumColor_10.y + ReadLane(sumColor_10.y, 0x20);
		sumColor.z = sumColor_10.z + ReadLane(sumColor_10.z, 0x20);
		pSrt->rwb_downSample[int2(groupId.xy)] = float4(sumColor / 64.0f, 0.0f);
	}
}

struct BlurBufferSrt
{
	float4					params;				// width, height, 1/width, 1/height
	float4					filterParams;		// dx, dy, scale
	uint					badDataFilterOn;
	uint3					pad;
	RWTexture2D<float4>		blurredTexture;
	Texture2D<float4>		sourceTexture;
	SamplerState			textureSampler;
};

[numthreads(8, 8, 1)]
void Cs_BlurBuffer(uint2 pixelIdx : SV_DispatchThreadID, BlurBufferSrt *pSrt : S_SRT_DATA)
{
	if (pixelIdx.x > (uint)pSrt->params.x || pixelIdx.y > (uint)pSrt->params.y)
		return;

	float2 uv = pixelIdx.xy * pSrt->params.zw;
	float2 dt = pSrt->filterParams.xy * pSrt->filterParams.z;

	// 1 10 45 120 210 252 

	float weights[11] = { 1.0f, 10.0f, 45.0f, 120.0f, 210.0f, 252.0f, 210.0f, 120.0f, 45.0f, 10.0f, 1.0f };
	float sumWeights = 1024.0f;

	// Doing 1D gaussian filter with 7 taps
	float3 color = float3(0.0f);
	for (int i = -5; i < 6; ++i)
	{
		float3 sampleColor = pSrt->sourceTexture.SampleLevel(pSrt->textureSampler, saturate(uv + i * dt), 0).rgb;
		if (!pSrt->badDataFilterOn || all(!isnan(sampleColor)))
			color += sampleColor * weights[i + 5];
	}
/*
	color  = pSrt->sourceTexture.SampleLevel(pSrt->textureSampler, uv - 3.0f * dt, 0).rgb * 0.006f;
	color += pSrt->sourceTexture.SampleLevel(pSrt->textureSampler, uv - 2.0f * dt, 0).rgb * 0.061f;
	color += pSrt->sourceTexture.SampleLevel(pSrt->textureSampler, uv - 1.0f * dt, 0).rgb * 0.242f;
	color += pSrt->sourceTexture.SampleLevel(pSrt->textureSampler, uv + 0.0f * dt, 0).rgb * 0.383f;
	color += pSrt->sourceTexture.SampleLevel(pSrt->textureSampler, uv + 1.0f * dt, 0).rgb * 0.242f;
	color += pSrt->sourceTexture.SampleLevel(pSrt->textureSampler, uv + 2.0f * dt, 0).rgb * 0.061f;
	color += pSrt->sourceTexture.SampleLevel(pSrt->textureSampler, uv + 3.0f * dt, 0).rgb * 0.006f;
	*/

	color /= sumWeights;

	pSrt->blurredTexture[(int2)pixelIdx] = float4(color, 0.0f);
}

struct CreateHalfResCompositeInfoSrt
{
	float4					m_vsDepthParams;
	RWTexture2D<uint>		m_rwBlendInfo;
	Texture2D<float>		m_depthBuffer;
	Texture2D<float>		m_dsDepthBuffer;
};

groupshared float gs_dsDepthVs[6][6];

[NUM_THREADS(8, 8, 1)]
void Cs_CreateHalfResCompositeInfo(uint2 dispatchId : SV_DispatchThreadID, 
								   uint2 groupThreadId : SV_GroupThreadId, 
								   uint2 groupId : SV_GroupID, 
								   CreateHalfResCompositeInfoSrt* pSrt : S_SRT_DATA)
{
	float destDepth = pSrt->m_depthBuffer[dispatchId];
	float destDepthVs = destDepth == 1.0f ? pSrt->m_vsDepthParams.w : (pSrt->m_vsDepthParams.x / (destDepth - pSrt->m_vsDepthParams.y));

	// Load (and convert to view space) the 6x6 downsampled depths
	// that cover the pixels in this full-res 8x8 block
	if (groupThreadId.x < 6 && groupThreadId.y < 6)
	{
		int2 offset = int2(groupThreadId) - 1;
		float dsDepth = pSrt->m_dsDepthBuffer.Load(int3(groupId * 4, 0), offset);
		float dsDepthVs = dsDepth == 1.0f ? pSrt->m_vsDepthParams.w : (pSrt->m_vsDepthParams.x / (dsDepth - pSrt->m_vsDepthParams.y));

		// Save in groupshared memory for fast access in the rest of the shader!
		gs_dsDepthVs[groupThreadId.x][groupThreadId.y] = dsDepthVs;
	}

	// Get all the half-res, view-space depths in the 3x3 block centered on the
	// full-res pixel we're processing with this thread
	uint2 cidx = groupThreadId / 2;
	float3 depthVs0 = float3(gs_dsDepthVs[cidx.x+0][cidx.y+0], gs_dsDepthVs[cidx.x+1][cidx.y+0], gs_dsDepthVs[cidx.x+2][cidx.y+0]);
	float3 depthVs1 = float3(gs_dsDepthVs[cidx.x+0][cidx.y+1], gs_dsDepthVs[cidx.x+1][cidx.y+1], gs_dsDepthVs[cidx.x+2][cidx.y+1]);
	float3 depthVs2 = float3(gs_dsDepthVs[cidx.x+0][cidx.y+2], gs_dsDepthVs[cidx.x+1][cidx.y+2], gs_dsDepthVs[cidx.x+2][cidx.y+2]);

	// Get the absolute value of the difference between the half-res depths at
	// each of the surrounding samples and the full-res depth of this pixel
	float3 absDepth[3] = { abs(depthVs0 - destDepthVs), 
						   abs(depthVs1 - destDepthVs), 
						   abs(depthVs2 - destDepthVs) };

	// Find the minimum and maximum view-space depth differences between each
	// half-res sample point and the full-res depth
	float3 minAbsDepth3 = float3(min3(absDepth[0].x, absDepth[0].y, absDepth[0].z),
								 min3(absDepth[1].x, absDepth[1].y, absDepth[1].z),
								 min3(absDepth[2].x, absDepth[2].y, absDepth[2].z));
	float minAbsDepth = min3(minAbsDepth3.x, minAbsDepth3.y, minAbsDepth3.z);

	float3 maxAbsDepth3 = float3(max3(absDepth[0].x, absDepth[0].y, absDepth[0].z), 
								 max3(absDepth[1].x, absDepth[1].y, absDepth[1].z), 
								 max3(absDepth[2].x, absDepth[2].y, absDepth[2].z));
	float maxAbsDepth = max3(maxAbsDepth3.x, maxAbsDepth3.y, maxAbsDepth3.z);

	// Determine the index of the half-res sample that is closest in depth to this pixel
	// The offset vector can be calculated as x = (idx & 3) - 1, y = (idx / 4) - 1
	uint x = 0, y = 0;
	     if (minAbsDepth == minAbsDepth3.y) y = 1;
	else if (minAbsDepth == minAbsDepth3.z)	y = 2;
	     if (minAbsDepth == absDepth[y].y) x = 1;
	else if (minAbsDepth == absDepth[y].z) x = 2;

	float4 slopError;
	slopError.x = abs((depthVs0.x + depthVs2.z) - depthVs1.y * 2) / 1.1414f;
	slopError.y = abs((depthVs0.y + depthVs2.y) - depthVs1.y * 2);
	slopError.z = abs((depthVs0.z + depthVs2.x) - depthVs1.y * 2) / 1.1414f;
	slopError.w = abs((depthVs1.x + depthVs1.z) - depthVs1.y * 2);

	float finalError = sqrt(dot(slopError, slopError)) / depthVs1.y;

	float depthScale = saturate((depthVs1.y - 3.0f) / 20.0f);
	const float maxError = lerp(0.02f, 0.5f, depthScale * depthScale);
	float lerpRatio = finalError < maxError ? 0.0f : 1.0f;

	// Create a curve based on the view space depth distance that will be used
	// to offset the uv coordinates used when sampling the half-res color buffer
	// more or less from the original sample point towards the point with the
	// closest absolute depth as calculated above
	float scale = min(50.0f / destDepthVs, pSrt->m_vsDepthParams.z);
	float uvOffsetScaler = lerpRatio * saturate(pow(saturate(maxAbsDepth * scale), 1.8f));

	// The output is packed with the upper 12 bits containing the uv offset
	// scaler, and the lower 4 bits containing the offset to the closest depth
	uint blendInfo = (uint(uvOffsetScaler * 15.0f) << 4) | (y << 2) | x;
	pSrt->m_rwBlendInfo[dispatchId] = blendInfo;
}



// For the half-res and the downsample shaders, we have an extra m_dstMin because often 
// those buffers are used for rendering where the minmax version wouldn't work. 
// So we do it as we're creating the min max to save on bandwidth. 

struct DownsampleStaticDepthMinMaxFullResSrt
{
	Texture2D<float>		m_srcStaticDepth;
	RW_Texture2D<float2>	m_dstDepthMinMax;
	SamplerState			m_linearClampSampler;
	float2					m_invDstSize;
};

[NUM_THREADS(8, 8, 1)]
void Cs_DownsampleStaticDepthMinMaxFullRes(uint2 dispatchId : S_DISPATCH_THREAD_ID, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, DownsampleStaticDepthMinMaxFullResSrt *pSrt : S_SRT_DATA)
{
	const float2 uv = (dispatchId + 0.5f) * pSrt->m_invDstSize;

	const float4 srcSamples = pSrt->m_srcStaticDepth.Gather(pSrt->m_linearClampSampler, uv);
	const float4 minDepth4 = srcSamples == 1.0f ? 2.0f : srcSamples;
	const float4 maxDepth4 = srcSamples == 1.0f ? -2.0f : srcSamples;

	float minDepth = min3(minDepth4.x, minDepth4.y, min(minDepth4.z, minDepth4.w));
	float maxDepth = max3(maxDepth4.x, maxDepth4.y, max(maxDepth4.z, maxDepth4.w));

	minDepth = min(minDepth, LaneSwizzle(minDepth, 0x1f, 0x00, 0x01));
	minDepth = min(minDepth, LaneSwizzle(minDepth, 0x1f, 0x00, 0x08));
	minDepth = min(minDepth, LaneSwizzle(minDepth, 0x1f, 0x00, 0x02));
	minDepth = min(minDepth, LaneSwizzle(minDepth, 0x1f, 0x00, 0x10));
	minDepth = min(minDepth, LaneSwizzle(minDepth, 0x1f, 0x00, 0x04));
	minDepth = min(minDepth, ReadLane(minDepth, 0x20));
	maxDepth = max(maxDepth, LaneSwizzle(maxDepth, 0x1f, 0x00, 0x01));
	maxDepth = max(maxDepth, LaneSwizzle(maxDepth, 0x1f, 0x00, 0x08));
	maxDepth = max(maxDepth, LaneSwizzle(maxDepth, 0x1f, 0x00, 0x02));
	maxDepth = max(maxDepth, LaneSwizzle(maxDepth, 0x1f, 0x00, 0x10));
	maxDepth = max(maxDepth, LaneSwizzle(maxDepth, 0x1f, 0x00, 0x04));
	maxDepth = max(maxDepth, ReadLane(maxDepth, 0x20));

	if (groupIndex == 0)
		pSrt->m_dstDepthMinMax[groupId] = float2(minDepth, maxDepth);
}

struct DownsampleDepthMinMaxFullResSrt
{
	Texture2D<float>		m_srcDepthMin;
	Texture2D<float>		m_srcDepthMax;
	RW_Texture2D<float2>	m_dstDepthMinMax;
	RW_Texture2D<float>		m_dstDsDepth;
	RW_Texture2D<float2>	m_dstDsDepthVariance;
	SamplerState			m_linearClampSampler;
	float2					m_invDstSize;
	float2					m_depthParams;
};

[NUM_THREADS(8, 8, 1)]
void Cs_DownsampleDepthMinMaxFullRes(uint2 dispatchId : S_DISPATCH_THREAD_ID, DownsampleDepthMinMaxFullResSrt *pSrt : S_SRT_DATA)
{
	const float2 uv = (dispatchId + 0.5f) * pSrt->m_invDstSize;

	const float4 minSamples = pSrt->m_srcDepthMin.Gather(pSrt->m_linearClampSampler, uv);
	const float4 maxSamples = pSrt->m_srcDepthMax.Gather(pSrt->m_linearClampSampler, uv);

	float minDepth = min3(minSamples.x, minSamples.y, min(minSamples.z, minSamples.w));
	float maxDepth = max3(maxSamples.x, maxSamples.y, max(maxSamples.z, maxSamples.w));

	pSrt->m_dstDepthMinMax[dispatchId] = float2(minDepth, maxDepth);
	
	// Instead of just pulling a single value out of primary depth for downsample, we'll take the max depth from the gather
	//pSrt->m_dstDsDepth[dispatchId] = pSrt->m_srcDepthMax[dispatchId.xy * 2];
	pSrt->m_dstDsDepth[dispatchId] = max3(maxSamples.x, maxSamples.y, max(maxSamples.z, maxSamples.w));

#if COMPUTE_VARIANCE
	float4 linearDepths;


	float linDepthClamp = 100.0f;

	linearDepths.x = min(linDepthClamp, GetLinearDepth(maxSamples.x, pSrt->m_depthParams));
	linearDepths.y = min(linDepthClamp, GetLinearDepth(maxSamples.y, pSrt->m_depthParams));
	linearDepths.z = min(linDepthClamp, GetLinearDepth(maxSamples.z, pSrt->m_depthParams));
	linearDepths.w = min(linDepthClamp, GetLinearDepth(maxSamples.w, pSrt->m_depthParams));

	float4 linDepthsSqaured = linearDepths * linearDepths;

	float avgDepth = (linearDepths.x + linearDepths.y + linearDepths.z + linearDepths.w) / 4.0f;
	float avgDepthSqr = (linDepthsSqaured.x + linDepthsSqaured.y + linDepthsSqaured.z + linDepthsSqaured.w) / 4.0f;

	pSrt->m_dstDsDepthVariance[dispatchId] = float2(avgDepth, avgDepthSqr);
#endif
}

struct DownsampleDepthMinMaxSrt
{
	Texture2D<float2>		m_srcDepthMinMax;
	Texture2D<float>		m_srcDepth;
	RW_Texture2D<float2>	m_dstDepthMinMax;
	RW_Texture2D<float>		m_dstDsDepth;
	SamplerState			m_linearClampSampler;
	float2					m_invDstSize;
};

[NUM_THREADS(8, 8, 1)]
void Cs_DownsampleDepthMinMax(uint2 dispatchId : S_DISPATCH_THREAD_ID, DownsampleDepthMinMaxSrt *pSrt : S_SRT_DATA)
{
	const float2 uv = (dispatchId + 0.5f) * pSrt->m_invDstSize;

	const float4 minSamples = pSrt->m_srcDepthMinMax.GatherRed(pSrt->m_linearClampSampler, uv);
	const float4 maxSamples = pSrt->m_srcDepthMinMax.GatherGreen(pSrt->m_linearClampSampler, uv);

	float minDepth = min3(minSamples.x, minSamples.y, min(minSamples.z, minSamples.w));
	float maxDepth = max3(maxSamples.x, maxSamples.y, max(maxSamples.z, maxSamples.w));

	pSrt->m_dstDepthMinMax[dispatchId] = float2(minDepth, maxDepth);
	pSrt->m_dstDsDepth[dispatchId] = maxDepth;//pSrt->m_srcDepthMinMax[dispatchId.xy * 2 + 1];
}

struct DownsampleDepthMinMaxSingleToSingleSrt
{
	Texture2D<float2>		m_srcDepthMinMax;
	RW_Texture2D<float2>	m_dstDepthMinMax;
	SamplerState			m_linearClampSampler;
	float2					m_invDstSize;
};

[NUM_THREADS(8, 8, 1)]
void Cs_DownsampleDepthMinMaxSingleToSingle(uint2 dispatchId : S_DISPATCH_THREAD_ID, DownsampleDepthMinMaxSingleToSingleSrt *pSrt : S_SRT_DATA)
{
	const float2 uv = (dispatchId + 0.5f) * pSrt->m_invDstSize;

	const float4 minSamples = pSrt->m_srcDepthMinMax.GatherRed(pSrt->m_linearClampSampler, uv);
	const float4 maxSamples = pSrt->m_srcDepthMinMax.GatherGreen(pSrt->m_linearClampSampler, uv);

	float minDepth = min3(minSamples.x, minSamples.y, min(minSamples.z, minSamples.w));
	float maxDepth = max3(maxSamples.x, maxSamples.y, max(maxSamples.z, maxSamples.w));

	pSrt->m_dstDepthMinMax[dispatchId] = float2(minDepth, maxDepth);
}

struct DownsampleDepthMinSrt
{
	Texture2D<float>	m_srcDepth;
	RW_Texture2D<float>	m_dstDepth;
	SamplerState		m_linearClampSampler;
	float2				m_invDstSize;
};

[NUM_THREADS(8, 8, 1)]
void Cs_DownsampleDepthMin(uint2 dispatchId : S_DISPATCH_THREAD_ID, DownsampleDepthMinSrt *pSrt : S_SRT_DATA)
{
	const float2 uv = (dispatchId + 0.5f) * pSrt->m_invDstSize;

	const float4 samples = pSrt->m_srcDepth.Gather(pSrt->m_linearClampSampler, uv);

	float minDepth = min3(samples.x, samples.y, min(samples.z, samples.w));

	pSrt->m_dstDepth[dispatchId] = minDepth;
}

struct DownsampleDepthMinWithInfoSrt
{
	Texture2D<float>	m_srcDepth;
	RW_Texture2D<float>	m_dstDepth;
	RW_Texture2D<float>	m_dstInfo;
	SamplerState		m_pointClampSampler;
	float2				m_invDstSize;
};

[NUM_THREADS(8, 8, 1)]
void Cs_DownsampleDepthMinWithInfo(uint2 dispatchId : S_DISPATCH_THREAD_ID, DownsampleDepthMinWithInfoSrt *pSrt : S_SRT_DATA)
{
	const float2 uv = (dispatchId + 0.5f) * pSrt->m_invDstSize;

	const float4 samples = pSrt->m_srcDepth.Gather(pSrt->m_pointClampSampler, uv);

	float minDepth = min3(samples.x, samples.y, min(samples.z, samples.w));

	// Follow the ordering of Gather (start with bottom left, counter-clockwise)
	float depthInfo = 0.0f;
	if (minDepth == samples.x)
	{
		depthInfo = 0.0f;
	}
	else if (minDepth == samples.y)
	{
		depthInfo = 0.25f;
	}
	else if (minDepth == samples.z)
	{
		depthInfo = 0.5f;
	}
	else
	{
		depthInfo = 0.75f;
	}

	pSrt->m_dstDepth[dispatchId] = minDepth;
	pSrt->m_dstInfo[dispatchId] = depthInfo;
}

struct ProjectMinMaxDepthToNextFrameSrt
{
	Texture2D<float2>		m_minMaxDepth;
	RWByteAddressBuffer		m_projectedDepth;
	float2					m_invBufferSize;
	float2					m_bufferSize;
	float2					m_scaledBufferSize;
	float2					m_vsDepthParams;
	float4					m_vsScaleOffset;
	float4x4				m_transformVSToNextFrameSS;
};

[NUM_THREADS(8, 8, 1)]
void Cs_ProjectMinMaxDepthToNextFrame(int2 dispatchId : S_DISPATCH_THREAD_ID, ProjectMinMaxDepthToNextFrameSrt *pSrt : S_SRT_DATA)
{
	float2 minmaxDepth = pSrt->m_minMaxDepth[dispatchId.xy];
	if (minmaxDepth.x <=  minmaxDepth.y)
	{
		const float2 uv0 = dispatchId * pSrt->m_invBufferSize;
		const float2 uv1 = (dispatchId + 1) * pSrt->m_invBufferSize;
		float depthMinVS = minmaxDepth.x == 1.0f ? 1e7 : (pSrt->m_vsDepthParams.x / (minmaxDepth.x - pSrt->m_vsDepthParams.y));
		float depthMaxVS = minmaxDepth.y == 1.0f ? 1e7 : (pSrt->m_vsDepthParams.x / (minmaxDepth.y - pSrt->m_vsDepthParams.y));
		float3 positionSS0 = float3(uv0 * pSrt->m_vsScaleOffset.xy + pSrt->m_vsScaleOffset.zw, 1.0f);
		float3 positionSS1 = float3(uv1 * pSrt->m_vsScaleOffset.xy + pSrt->m_vsScaleOffset.zw, 1.0f);
		float3 positionMinVS0 = positionSS0 * depthMinVS;
		float3 positionMinVS1 = positionSS1 * depthMinVS;
		float3 positionMaxVS0 = positionSS0 * depthMaxVS;
		float3 positionMaxVS1 = positionSS1 * depthMaxVS;

		float3 positionVS[8];
		positionVS[0] = positionMinVS0;
		positionVS[1] = float3(positionMinVS0.x, positionMinVS1.y, positionMinVS0.z);
		positionVS[2] = positionMinVS1;
		positionVS[3] = float3(positionMinVS1.x, positionMinVS0.y, positionMinVS0.z);
		positionVS[4] = positionMaxVS0;
		positionVS[5] = float3(positionMaxVS0.x, positionMaxVS1.y, positionMaxVS0.z);
		positionVS[6] = positionMaxVS1;
		positionVS[7] = float3(positionMaxVS1.x, positionMaxVS0.y, positionMaxVS0.z);

		float4 positionNextSS[8];
		float maxDepthVS = -1e7;
		float2 minXySS = 1e7;
		float2 maxXySS = -1e7;

		for (int i = 0; i < 8; i++)
		{
			positionNextSS[i] = mul(float4(positionVS[i], 1.0f), pSrt->m_transformVSToNextFrameSS);
			maxDepthVS = max(maxDepthVS, positionNextSS[i].w);
			positionNextSS[i].xy /= positionNextSS[i].w;

			minXySS = min(minXySS, positionNextSS[i].xy);
			maxXySS = max(maxXySS, positionNextSS[i].xy);
		}

		float2 minDispatchId = (minXySS * float2(0.5f, -0.5f) + 0.5f) * pSrt->m_bufferSize;
		float2 maxDispatchId = (maxXySS * float2(0.5f, -0.5f) + 0.5f) * pSrt->m_bufferSize;

		int2 iMinDispatchId = int2(floor(float2(minDispatchId.x, maxDispatchId.y) + 0.00001f));
		int2 iMaxDispatchId = int2(ceil(float2(maxDispatchId.x, minDispatchId.y) - 0.00001f));

		if (all(iMinDispatchId <= iMaxDispatchId) && all(iMinDispatchId < pSrt->m_bufferSize) && all(iMaxDispatchId >= 0))
		{
			int idx = dispatchId.y * pSrt->m_bufferSize.x + dispatchId.x;
			for (int i = iMinDispatchId.y; i < iMaxDispatchId.y; i++)
				for (int j = iMinDispatchId.x; j < iMaxDispatchId.x; j++)
				{
					uint uOldValue;
					pSrt->m_projectedDepth.InterlockedMax(idx * 4, asuint(maxDepthVS), uOldValue);
				}
		}
	}
}

struct DecodeNextMaxDepthBufferSrt
{
	ByteAddressBuffer		m_projectedDepthBuffer;
	RWTexture2D<float>		m_projectedDepth;
	float2					m_bufferSize;
};

[NUM_THREADS(8, 8, 1)]
void Cs_DecodeNextMaxDepthBuffer(int2 dispatchId : S_DISPATCH_THREAD_ID, DecodeNextMaxDepthBufferSrt *pSrt : S_SRT_DATA)
{
	int idx = dispatchId.y * pSrt->m_bufferSize.x + dispatchId.x;
	uint3 srcDepth0 = pSrt->m_projectedDepthBuffer.Load3((idx-1-pSrt->m_bufferSize.x) * 4);
	uint3 srcDepth1 = pSrt->m_projectedDepthBuffer.Load3((idx-1) * 4);
	uint3 srcDepth2 = pSrt->m_projectedDepthBuffer.Load3((idx-1+pSrt->m_bufferSize.x) * 4);

	if (dispatchId.y == 0)
		srcDepth0 = 0;
	if (dispatchId.y == pSrt->m_bufferSize.y-1)
		srcDepth2 = 0;
	if (dispatchId.x == 0)
	{
		srcDepth0.x = srcDepth1.x = srcDepth2.x = 0;
	}
	if (dispatchId.x == pSrt->m_bufferSize.x-1)
	{
		srcDepth0.z = srcDepth1.z = srcDepth2.z = 0;
	}

	uint maxDepth = max3(max3(srcDepth0.x, srcDepth0.y, srcDepth0.z),
						 max3(srcDepth1.x, srcDepth1.y, srcDepth1.z),
						 max3(srcDepth2.x, srcDepth2.y, srcDepth2.z));

	pSrt->m_projectedDepth[dispatchId] = maxDepth == 0 ? 1e7 : asfloat(maxDepth);
}

struct DownsampleNextMaxDepthBufferSrt
{
	Texture2D<float>		m_srcProjectedDepth;
	RWTexture2D<float>		m_dsProjectedDepth;
	SamplerState			m_linearClampSampler;
	float2					m_invBufferSize;
};

[NUM_THREADS(8, 8, 1)]
void Cs_DownsampleNextMaxDepthBuffer(int2 dispatchId : S_DISPATCH_THREAD_ID, DownsampleNextMaxDepthBufferSrt *pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchId.xy + 0.5f) * pSrt->m_invBufferSize;
	const float4 samples0 = pSrt->m_srcProjectedDepth.Gather(pSrt->m_linearClampSampler, uv - 0.5f * pSrt->m_invBufferSize);
	const float4 samples1 = pSrt->m_srcProjectedDepth.Gather(pSrt->m_linearClampSampler, uv + float2(-0.5f * pSrt->m_invBufferSize.x, 0.5f * pSrt->m_invBufferSize.y));
	const float4 samples2 = pSrt->m_srcProjectedDepth.Gather(pSrt->m_linearClampSampler, uv + float2(0.5f * pSrt->m_invBufferSize.x, -0.5f * pSrt->m_invBufferSize.y));
	const float4 samples3 = pSrt->m_srcProjectedDepth.Gather(pSrt->m_linearClampSampler, uv + 0.5f * pSrt->m_invBufferSize);

	pSrt->m_dsProjectedDepth[dispatchId] = max(max3(max3(samples0.x, samples0.y, samples0.z), 
													max3(samples0.w, samples1.x, samples1.y), 
													max3(samples1.z, samples1.w, samples2.x)),
											   max3(max3(samples2.y, samples2.z, samples2.w),
													max3(samples3.x, samples3.y, samples3.z),
													samples3.w));
}

struct DownsampleColorTextureSrt
{
	Texture2D<float4>		m_srcTexture;
	RW_Texture2D<float4>	m_dstTexture;
	SamplerState			m_linearClampSampler;
	float2					m_invDstSize;
};

[NUM_THREADS(8, 8, 1)]
void Cs_DownsampleColorTexture(uint2 dispatchId : S_DISPATCH_THREAD_ID, DownsampleColorTextureSrt *pSrt : S_SRT_DATA)
{
	const float2 uv = (dispatchId + 0.5f) * pSrt->m_invDstSize;
	float4 color = pSrt->m_srcTexture.SampleLevel(pSrt->m_linearClampSampler, uv, 0);
	pSrt->m_dstTexture[dispatchId] = color;
}

/************************************************************************/
/* MSAA Depth Resolve                                                   */
/************************************************************************/

struct Simple2xMsaaDepthResolveSrt
{
	Texture2DMS<float, 2>	m_srcTex;
	RWTexture2D<float>		m_dstTex;

	Texture2DMS<uint, 2>	m_srcStencil;
	RWTexture2D<uint>		m_dstStencil;
};

[numthreads(8, 8, 1)]
void Cs_Simple2xMsaaDepthResolve(int2 dispatchId : SV_DispatchThreadID, Simple2xMsaaDepthResolveSrt *pSrt : S_SRT_DATA)
{
	float depth = pSrt->m_srcTex.Load(dispatchId, 0);
	pSrt->m_dstTex[dispatchId] = depth;
	pSrt->m_dstStencil[dispatchId] = pSrt->m_srcStencil.Load(dispatchId, 0);
}

struct Complex2xMsaaDepthResolveSrt
{
	Texture2DMS<float, 2>	m_srcTex;
	RWTexture2D<float>		m_dstTex;
	RWTexture2D<float>		m_dstTexFull;

	Texture2DMS<uint, 2>	m_srcStencil;
	RWTexture2D<uint>		m_dstStencil;
	RWTexture2D<uint>		m_dstStencilFull;
	uint					m_frameId;
};

[numthreads(8, 8, 1)]
void Cs_Complex2xMsaaDepthResolve(int2 dispatchId : SV_DispatchThreadID, Complex2xMsaaDepthResolveSrt *pSrt : S_SRT_DATA)
{
	float depth0 = pSrt->m_srcTex.Load(dispatchId, 0);
	float depth1 = pSrt->m_srcTex.Load(dispatchId, 1);
	uint stencil0 = pSrt->m_srcStencil.Load(dispatchId, 0);
	uint stencil1 = pSrt->m_srcStencil.Load(dispatchId, 1);
	uint oddEvenLine = (dispatchId.y + pSrt->m_frameId) & 1;

	pSrt->m_dstTex[dispatchId] = depth0;
	pSrt->m_dstTexFull[uint2(2 * dispatchId.x + oddEvenLine, dispatchId.y)] = depth0;
	pSrt->m_dstTexFull[uint2(2 * dispatchId.x + (1 - oddEvenLine), dispatchId.y)] = depth1;
	pSrt->m_dstStencil[dispatchId] = stencil0;
	pSrt->m_dstStencilFull[uint2(2 * dispatchId.x + oddEvenLine, dispatchId.y)] = stencil0;
	pSrt->m_dstStencilFull[uint2(2 * dispatchId.x + (1 - oddEvenLine), dispatchId.y)] = stencil1;
}

struct Medium2xMsaaDepthResolveSrt
{
	Texture2DMS<float, 2>	m_srcTex;
	RWTexture2D<float>		m_dstTex;
	uint					m_frameId;
};

[numthreads(8, 8, 1)]
void Cs_Medium2xMsaaDepthResolve(int2 dispatchId : SV_DispatchThreadID, Medium2xMsaaDepthResolveSrt *pSrt : S_SRT_DATA)
{
	float depth0 = pSrt->m_srcTex.Load(dispatchId, 0);
	float depth1 = pSrt->m_srcTex.Load(dispatchId, 1);
	uint oddEvenLine = (dispatchId.y + pSrt->m_frameId) & 1;

	pSrt->m_dstTex[uint2(2 * dispatchId.x + oddEvenLine, dispatchId.y)] = depth0;
	pSrt->m_dstTex[uint2(2 * dispatchId.x + (1 - oddEvenLine), dispatchId.y)] = depth1;
}

/************************************************************************/
/* Motion Vector Checkerboard Depth Resolve                             */
/************************************************************************/

struct MotionVectorCheckerboardResolveSrt
{
	RWTexture2D<float2> m_fullRes;
	Texture2D<float2> m_checkerboardRes;
	uint m_frameId;
};

[numthreads(8, 8, 1)]
void Cs_MotionVectorCheckerboardResolve(int2 dispatchId : SV_DispatchThreadID, MotionVectorCheckerboardResolveSrt *pSrt : S_SRT_DATA)
{
	uint oddEvenLine = (dispatchId.y + pSrt->m_frameId) & 1;
	uint2 filtered_pos = uint2(dispatchId.x * 2 + (1 - oddEvenLine), dispatchId.y);
	uint2 passthru_pos = uint2(dispatchId.x * 2 + oddEvenLine, dispatchId.y);

	int2 offset = int2(1, -1);
	pSrt->m_fullRes[passthru_pos] = pSrt->m_checkerboardRes.Load(int3(dispatchId, 0));
	pSrt->m_fullRes[filtered_pos] = 0.25f * (pSrt->m_checkerboardRes.Load(int3(dispatchId, 0)) + pSrt->m_checkerboardRes.Load(int3(dispatchId, 0), int2(0, -1)) + pSrt->m_checkerboardRes.Load(int3(dispatchId, 0), int2(0, 1)) + pSrt->m_checkerboardRes.Load(int3(dispatchId, 0), int2(offset[oddEvenLine], 0)));
}

struct DownsampleDepthSrt
{
	RWTexture2D<float>		m_rwDsDepth;
	RWTexture2D<uint>		m_rwDsStencil;
	RWTexture2D<float>		m_rwDsDepthVS;
	RWTexture2D<uint>		m_rwSrcOffset;
	RWTexture2D<uint>		m_rwMblurUpscaleInfo;
	Texture2D<float>		m_txSrcDepth;
	Texture2D<uint>			m_txSrcStencil;
	SamplerState			m_linearClampSampler;
	uint					m_useSpecificCorner;
	uint					m_pad;
	float2					m_invBufferSize;
	float4					m_vsDepthParams;
};


[NUM_THREADS(8, 8, 1)]
void Cs_DownsampleDepthBuffer(uint2 dispatchId : S_DISPATCH_THREAD_ID, DownsampleDepthSrt *pSrt : S_SRT_DATA)
{
	float finalDepth = pSrt->m_txSrcDepth[dispatchId.xy * 2];
	int2 offset = int2(0, 0);
	uint mergeBit = 0;

	float4 neighborDepth;
	neighborDepth.x = finalDepth;

	if (pSrt->m_useSpecificCorner == 0)
	{
		neighborDepth.y = pSrt->m_txSrcDepth[dispatchId.xy * 2 + int2(1, 0)];
		neighborDepth.z = pSrt->m_txSrcDepth[dispatchId.xy * 2 + int2(0, 1)];
		neighborDepth.w = pSrt->m_txSrcDepth[dispatchId.xy * 2 + int2(1, 1)];

		float minDepth = min(min3(neighborDepth.x, neighborDepth.y, neighborDepth.z), neighborDepth.w);
		float maxDepth = max(max3(neighborDepth.x, neighborDepth.y, neighborDepth.z), neighborDepth.w);

		finalDepth = ((dispatchId.x ^ dispatchId.y) & 0x01) == 0 ? minDepth : maxDepth;

		     if (finalDepth == neighborDepth.y) mergeBit = 1, offset = int2(1, 0);
		else if (finalDepth == neighborDepth.z) mergeBit = 2, offset = int2(0, 1);
		else if (finalDepth == neighborDepth.w) mergeBit = 3, offset = int2(1, 1);
	}

	uint innerIndex = dispatchId.x & 0x03;

	uint shiftedBits = mergeBit << (innerIndex * 2);

	shiftedBits |= LaneSwizzle(shiftedBits, (uint)0x1f, (uint)0x00, (uint)0x01);
	shiftedBits |= LaneSwizzle(shiftedBits, (uint)0x1f, (uint)0x00, (uint)0x02);

	pSrt->m_rwDsDepth[dispatchId] = finalDepth;
	pSrt->m_rwDsStencil[dispatchId] = pSrt->m_txSrcStencil[dispatchId.xy * 2 + offset];
	pSrt->m_rwDsDepthVS[dispatchId] = finalDepth == 1.0f ? pSrt->m_vsDepthParams.w : (pSrt->m_vsDepthParams.x / (finalDepth - pSrt->m_vsDepthParams.y));

	if (innerIndex == 0)
	{
		pSrt->m_rwSrcOffset[int2(dispatchId.x / 4, dispatchId.y)] = shiftedBits;
	}
}

float CalculateDepthVS(float depthZ, float4 vsDepthParams)
{
	return depthZ == 1.0f ? vsDepthParams.w : (vsDepthParams.x / (depthZ - vsDepthParams.y));
}

uint GetMergeOffsetAndDepth(out float finalDepth, out uint finalStencil, int2 dsDispatchId, DownsampleDepthSrt *pSrt)
{
	uint mergeBit = 0;
	int2 offset = int2(0, 0);
	finalDepth = pSrt->m_txSrcDepth[dsDispatchId.xy * 2];

	if (pSrt->m_useSpecificCorner == 0)
	{
		float4 neighborDepth;
		neighborDepth.x = finalDepth;
		neighborDepth.y = pSrt->m_txSrcDepth[dsDispatchId.xy * 2 + int2(1, 0)];
		neighborDepth.z = pSrt->m_txSrcDepth[dsDispatchId.xy * 2 + int2(0, 1)];
		neighborDepth.w = pSrt->m_txSrcDepth[dsDispatchId.xy * 2 + int2(1, 1)];

		float minDepth = min(min3(neighborDepth.x, neighborDepth.y, neighborDepth.z), neighborDepth.w);
		float maxDepth = max(max3(neighborDepth.x, neighborDepth.y, neighborDepth.z), neighborDepth.w);

		finalDepth = ((dsDispatchId.x ^ dsDispatchId.y) & 0x01) == 0 ? minDepth : maxDepth;

		     if (finalDepth == neighborDepth.y) mergeBit = 1, offset = int2(1, 0);
		else if (finalDepth == neighborDepth.z) mergeBit = 2, offset = int2(0, 1);
		else if (finalDepth == neighborDepth.w) mergeBit = 3, offset = int2(1, 1);
	}

	finalStencil = pSrt->m_txSrcStencil[dsDispatchId.xy * 2 + offset];

	return mergeBit;
}

uint GetDsDepthOffset(float3 srcDepthVS[3], float refDepthVS)
{
	float3 depthVSDiff[3];
	depthVSDiff[0] = abs(srcDepthVS[0] - refDepthVS);
	depthVSDiff[1] = abs(srcDepthVS[1] - refDepthVS);
	depthVSDiff[2] = abs(srcDepthVS[2] - refDepthVS);

	float3 minDepthVSDiff3 = float3(min3(depthVSDiff[0].x, depthVSDiff[0].y, depthVSDiff[0].z),
									min3(depthVSDiff[1].x, depthVSDiff[1].y, depthVSDiff[1].z),
									min3(depthVSDiff[2].x, depthVSDiff[2].y, depthVSDiff[2].z));

	float minDepthVSDiff = min3(minDepthVSDiff3.x, minDepthVSDiff3.y, minDepthVSDiff3.z);

	uint y = 0;
	     if (minDepthVSDiff == minDepthVSDiff3.y) y = 1;
	else if (minDepthVSDiff == minDepthVSDiff3.z) y = 2;

	uint x = 0;
	     if (minDepthVSDiff == depthVSDiff[y].y) x = 1;
	else if (minDepthVSDiff == depthVSDiff[y].z) x = 2;

	return (y << 2) | x;
}

groupshared float	g_downsampleDepth[10][10];
groupshared float	g_downsampleDepthVS[10][10];
groupshared uint	g_downsampleStencil[10][10];
groupshared uint	g_downsampleOffset[10][10];

[NUM_THREADS(8, 8, 1)]
void Cs_DownsampleDepthBufferWithUpScaleInfo(int2 dispatchId : S_DISPATCH_THREAD_ID, int2 groupThreadId : SV_GroupThreadId, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, DownsampleDepthSrt *pSrt : S_SRT_DATA)
{
	int2 localIdx = int2(groupIndex % 10, groupIndex / 10);
	int2 srcIdx = groupId * 8 + localIdx - 1;
	
	g_downsampleOffset[localIdx.y][localIdx.x] = GetMergeOffsetAndDepth(g_downsampleDepth[localIdx.y][localIdx.x], g_downsampleStencil[localIdx.y][localIdx.x], srcIdx, pSrt);
	g_downsampleDepthVS[localIdx.y][localIdx.x] = CalculateDepthVS(g_downsampleDepth[localIdx.y][localIdx.x], pSrt->m_vsDepthParams);

	if (groupIndex < 36)
	{
		localIdx = int2((groupIndex + 64) % 10, (groupIndex + 64) / 10);
		srcIdx = groupId * 8 + localIdx - 1;
	
		g_downsampleOffset[localIdx.y][localIdx.x] = GetMergeOffsetAndDepth(g_downsampleDepth[localIdx.y][localIdx.x], g_downsampleStencil[localIdx.y][localIdx.x], srcIdx, pSrt);
		g_downsampleDepthVS[localIdx.y][localIdx.x] = CalculateDepthVS(g_downsampleDepth[localIdx.y][localIdx.x], pSrt->m_vsDepthParams); 
	}

	uint innerIndex = dispatchId.x & 0x03;
	uint shiftedBits = g_downsampleOffset[groupThreadId.y + 1][groupThreadId.x + 1] << (innerIndex * 2);

	shiftedBits |= LaneSwizzle(shiftedBits, (uint)0x1f, (uint)0x00, (uint)0x01);
	shiftedBits |= LaneSwizzle(shiftedBits, (uint)0x1f, (uint)0x00, (uint)0x02);

	float dsDepthZ = g_downsampleDepth[groupThreadId.y + 1][groupThreadId.x + 1];
	float dsDepthVS = g_downsampleDepthVS[groupThreadId.y + 1][groupThreadId.x + 1];
	uint dsStencil = g_downsampleStencil[groupThreadId.y + 1][groupThreadId.x + 1];
	pSrt->m_rwDsDepth[dispatchId] = dsDepthZ;
	pSrt->m_rwDsStencil[dispatchId] = dsStencil;
	pSrt->m_rwDsDepthVS[dispatchId] = dsDepthVS;
	if (innerIndex == 0)
		pSrt->m_rwSrcOffset[int2(dispatchId.x / 4, dispatchId.y)] = shiftedBits;


	int2 fsDispatchId0 = dispatchId * 2;
	int2 fsDispatchId1 = fsDispatchId0 + int2(1, 0);
	int2 fsDispatchId2 = fsDispatchId0 + int2(0, 1);
	int2 fsDispatchId3 = fsDispatchId0 + int2(1, 1);

	float4 depthZ4;
	depthZ4.x = pSrt->m_txSrcDepth[fsDispatchId0];
	depthZ4.y = pSrt->m_txSrcDepth[fsDispatchId1];
	depthZ4.z = pSrt->m_txSrcDepth[fsDispatchId2];
	depthZ4.w = pSrt->m_txSrcDepth[fsDispatchId3];

	float3 srcDepthZ[3];
	srcDepthZ[0] = float3(g_downsampleDepth[groupThreadId.y][groupThreadId.x], 
						  g_downsampleDepth[groupThreadId.y][groupThreadId.x+1], 
						  g_downsampleDepth[groupThreadId.y][groupThreadId.x+2]);
	srcDepthZ[1] = float3(g_downsampleDepth[groupThreadId.y+1][groupThreadId.x], 
						  g_downsampleDepth[groupThreadId.y+1][groupThreadId.x+1], 
						  g_downsampleDepth[groupThreadId.y+1][groupThreadId.x+2]);
	srcDepthZ[2] = float3(g_downsampleDepth[groupThreadId.y+2][groupThreadId.x], 
						  g_downsampleDepth[groupThreadId.y+2][groupThreadId.x+1], 
						  g_downsampleDepth[groupThreadId.y+2][groupThreadId.x+2]);

	float3 srcDepthVS[3];
	srcDepthVS[0] = float3(g_downsampleDepthVS[groupThreadId.y][groupThreadId.x], 
						  g_downsampleDepthVS[groupThreadId.y][groupThreadId.x+1], 
						  g_downsampleDepthVS[groupThreadId.y][groupThreadId.x+2]);
	srcDepthVS[1] = float3(g_downsampleDepthVS[groupThreadId.y+1][groupThreadId.x], 
						  g_downsampleDepthVS[groupThreadId.y+1][groupThreadId.x+1], 
						  g_downsampleDepthVS[groupThreadId.y+1][groupThreadId.x+2]);
	srcDepthVS[2] = float3(g_downsampleDepthVS[groupThreadId.y+2][groupThreadId.x], 
						  g_downsampleDepthVS[groupThreadId.y+2][groupThreadId.x+1], 
						  g_downsampleDepthVS[groupThreadId.y+2][groupThreadId.x+2]);

	float4 slopError;
	slopError.x = abs((srcDepthVS[0].x + srcDepthVS[2].z) - srcDepthVS[1].y * 2) / 1.1414f;
	slopError.y = abs((srcDepthVS[0].y + srcDepthVS[2].y) - srcDepthVS[1].y * 2);
	slopError.z = abs((srcDepthVS[0].z + srcDepthVS[2].x) - srcDepthVS[1].y * 2) / 1.1414f;
	slopError.w = abs((srcDepthVS[1].x + srcDepthVS[1].z) - srcDepthVS[1].y * 2);

	float finalError = sqrt(dot(slopError, slopError)) / srcDepthVS[1].y;

	float depthScale = saturate((srcDepthVS[1].y - 3.0f) / 20.0f);
	const float maxError = lerp(0.02f, 0.5f, depthScale * depthScale);
	float lerpRatio = finalError < maxError ? 0.0f : 1.0f;

	float minDepthZ = min3(min3(srcDepthZ[0].x, srcDepthZ[0].y, srcDepthZ[0].z),
							min3(srcDepthZ[1].x, srcDepthZ[1].y, srcDepthZ[1].z),
							min3(srcDepthZ[2].x, srcDepthZ[2].y, srcDepthZ[2].z));

	float maxDepthZ = max3(max3(srcDepthZ[0].x, srcDepthZ[0].y, srcDepthZ[0].z),
							max3(srcDepthZ[1].x, srcDepthZ[1].y, srcDepthZ[1].z),
							max3(srcDepthZ[2].x, srcDepthZ[2].y, srcDepthZ[2].z));

	float minDepthVS = minDepthZ == 1.0f ? pSrt->m_vsDepthParams.w : (pSrt->m_vsDepthParams.x / (minDepthZ - pSrt->m_vsDepthParams.y));
	float maxDepthVS = maxDepthZ == 1.0f ? pSrt->m_vsDepthParams.w : (pSrt->m_vsDepthParams.x / (maxDepthZ - pSrt->m_vsDepthParams.y));

	float4 depthVS4;
	depthVS4.x = depthZ4.x == 1.0f ? pSrt->m_vsDepthParams.w : (pSrt->m_vsDepthParams.x / (depthZ4.x - pSrt->m_vsDepthParams.y));
	depthVS4.y = depthZ4.y == 1.0f ? pSrt->m_vsDepthParams.w : (pSrt->m_vsDepthParams.x / (depthZ4.y - pSrt->m_vsDepthParams.y));
	depthVS4.z = depthZ4.z == 1.0f ? pSrt->m_vsDepthParams.w : (pSrt->m_vsDepthParams.x / (depthZ4.z - pSrt->m_vsDepthParams.y));
	depthVS4.w = depthZ4.w == 1.0f ? pSrt->m_vsDepthParams.w : (pSrt->m_vsDepthParams.x / (depthZ4.w - pSrt->m_vsDepthParams.y));

	float4 maxDepthVSDiff;
	maxDepthVSDiff.x = max(abs(depthVS4.x - minDepthVS), abs(depthVS4.x - maxDepthVS));
	maxDepthVSDiff.y = max(abs(depthVS4.y - minDepthVS), abs(depthVS4.y - maxDepthVS));
	maxDepthVSDiff.z = max(abs(depthVS4.z - minDepthVS), abs(depthVS4.z - maxDepthVS));
	maxDepthVSDiff.w = max(abs(depthVS4.w - minDepthVS), abs(depthVS4.w - maxDepthVS));

	float scale;
	float finalLerpRatio;

	scale = min(50.0f / depthVS4.x, pSrt->m_vsDepthParams.z);
	finalLerpRatio = lerpRatio * saturate(pow(saturate(maxDepthVSDiff.x * scale), 1.8f));
	pSrt->m_rwMblurUpscaleInfo[fsDispatchId0] = (uint(finalLerpRatio * 15.0f) << 4) | GetDsDepthOffset(srcDepthZ, depthZ4.x);

	scale = min(50.0f / depthVS4.y, pSrt->m_vsDepthParams.z);
	finalLerpRatio = lerpRatio * saturate(pow(saturate(maxDepthVSDiff.y * scale), 1.8f));
	pSrt->m_rwMblurUpscaleInfo[fsDispatchId1] = (uint(finalLerpRatio * 15.0f) << 4) | GetDsDepthOffset(srcDepthZ, depthZ4.y);

	scale = min(50.0f / depthVS4.z, pSrt->m_vsDepthParams.z);
	finalLerpRatio = lerpRatio * saturate(pow(saturate(maxDepthVSDiff.z * scale), 1.8f));
	pSrt->m_rwMblurUpscaleInfo[fsDispatchId2] = (uint(finalLerpRatio * 15.0f) << 4) | GetDsDepthOffset(srcDepthZ, depthZ4.z);

	scale = min(50.0f / depthVS4.w, pSrt->m_vsDepthParams.z);
	finalLerpRatio = lerpRatio * saturate(pow(saturate(maxDepthVSDiff.w * scale), 1.8f));
	pSrt->m_rwMblurUpscaleInfo[fsDispatchId3] = (uint(finalLerpRatio * 15.0f) << 4) | GetDsDepthOffset(srcDepthZ, depthZ4.w);
}

struct DownsampleQuarterDepthSrt
{
	Texture2D<float>		m_srcDepth;
	Texture2D<uint>			m_srcStencil;
	Texture2D<uint>			m_halfSrcOffset;
	RWTexture2D<float>		m_dstDepth;
	RWTexture2D<uint>		m_dstStencil;
	RWTexture2D<uint>		m_quarterSrcOffset;
	SamplerState			m_linearClampSampler;
	uint					m_useSpecificCorner;
	uint					m_pad;
	float2					m_invBufferSize;
};

[NUM_THREADS(8, 8, 1)]
void Cs_DownsampleQuarterDepthBuffer(uint2 dispatchId : S_DISPATCH_THREAD_ID, DownsampleQuarterDepthSrt *pSrt : S_SRT_DATA)
{
	float finalDepth = pSrt->m_srcDepth[dispatchId.xy * 2 + int2(1, 1)];
	uint2 offset = uint2(1, 1);

	float4 neighborDepth;
	neighborDepth.w = finalDepth;

	if (pSrt->m_useSpecificCorner == 0)
	{
		neighborDepth.x = pSrt->m_srcDepth[dispatchId.xy * 2];
		neighborDepth.y = pSrt->m_srcDepth[dispatchId.xy * 2 + int2(1, 0)];
		neighborDepth.z = pSrt->m_srcDepth[dispatchId.xy * 2 + int2(0, 1)];

		float minDepth = min(min3(neighborDepth.x, neighborDepth.y, neighborDepth.z), neighborDepth.w);
		float maxDepth = max(max3(neighborDepth.x, neighborDepth.y, neighborDepth.z), neighborDepth.w);

		finalDepth = ((dispatchId.x ^ dispatchId.y) & 0x01) == 0 ? minDepth : maxDepth;

		     if (finalDepth == neighborDepth.x) offset = uint2(0, 0); 
		else if (finalDepth == neighborDepth.y) offset = uint2(1, 0);
		else if (finalDepth == neighborDepth.z) offset = uint2(0, 1);
	}

	uint2 halfSrcOffset = dispatchId.xy * 2 + offset;

	uint fullSrcOffset = pSrt->m_halfSrcOffset[halfSrcOffset];

	uint halfOffset = (pSrt->m_halfSrcOffset[int2(halfSrcOffset.x / 4, halfSrcOffset.y)] >> ((halfSrcOffset.x & 0x03) * 2));
	uint2 finalOffset = offset * 2 + (uint2(halfOffset, halfOffset >> 1) & 0x01);

	pSrt->m_dstDepth[dispatchId] = finalDepth;
	pSrt->m_dstStencil[dispatchId] = pSrt->m_srcStencil[dispatchId.xy * 2 + offset];

	pSrt->m_quarterSrcOffset[int2(dispatchId.x, dispatchId.y)] = (finalOffset.y << 4) | finalOffset.x;
}

struct CreateHtileFromMaskSrt
{
	uint2					m_htileBufferSize;
	uint					m_sliceIndex;
	RWBuffer<uint>			m_depthTile;
	Texture2DArray<float>	m_txMask;
};

[NUM_THREADS(8, 8, 1)]
void Cs_CreateHtileFromMask(int2 dispatchId : S_DISPATCH_THREAD_ID, CreateHtileFromMaskSrt *pSrt : S_SRT_DATA)
{
	uint htileOffset = HTileOffsetInDwords(dispatchId.x, dispatchId.y, pSrt->m_htileBufferSize.x, false, g_isNeoMode);

	uint maxZ = 16383;
	uint minZ = 16383;
	uint htile = (maxZ << 18) | (minZ << 4) | 0x0;

	float mask = pSrt->m_txMask[int3(dispatchId.xy, pSrt->m_sliceIndex)];
	if (mask < 0.5f)
		htile = 0;

	pSrt->m_depthTile[htileOffset] = htile;
}

struct Gaussian9BlurSrt
{
	Texture2D<float4>		m_srcTex;
	RW_Texture2D<float4>	m_dstTex;
	SamplerState			m_linearSampler;
	float2					m_invSize;
};	

float4 Gaussian9BlurHelper(uint2 dispatchId, Gaussian9BlurSrt *pSrt, float2 dir)
{
	// Uses method described in http://rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/

	const float offsets[3] = {0, 1.38461538f, 3.23076923f};
	const float weights[3] = {0.22702702f, 0.31621621f, 0.07027027f};

	const float2 uv = (dispatchId + 0.5f) * pSrt->m_invSize;


	float4 result = pSrt->m_srcTex[dispatchId] * weights[0];
	for (uint i = 1; i < 3; ++i)
	{

		result += pSrt->m_srcTex.SampleLevel(pSrt->m_linearSampler, uv + dir*(offsets[i] * pSrt->m_invSize), 0) * weights[i];
		result += pSrt->m_srcTex.SampleLevel(pSrt->m_linearSampler, uv - dir*(offsets[i] * pSrt->m_invSize), 0) * weights[i];
	}


	return result;
}

float4 Gaussian5BlurHelper(int2 dispatchId, Gaussian9BlurSrt *pSrt, int2 dir)
{
	// TODO: Use the same method in the 9-tap filter
	const int offsets[3] = {0, 1, 2};
	const float weights[3] = {3/8.f, 1/4.f, 1/16.f};

	float4 result = pSrt->m_srcTex[dispatchId] * weights[0];
	for (int i = 1; i < 3; ++i)
	{
		result += pSrt->m_srcTex[dispatchId + dir*offsets[i]] * weights[i];
		result += pSrt->m_srcTex[dispatchId - dir*offsets[i]] * weights[i];
	}

	return result;
}


[NUM_THREADS(8, 8, 1)]
void Cs_Gaussian9BlurX(uint2 dispatchId : S_DISPATCH_THREAD_ID, Gaussian9BlurSrt *pSrt : S_SRT_DATA)
{
	pSrt->m_dstTex[dispatchId] = Gaussian9BlurHelper(dispatchId, pSrt, float2(1, 0));
}

[NUM_THREADS(8, 8, 1)]
void Cs_Gaussian9BlurY(uint2 dispatchId : S_DISPATCH_THREAD_ID, Gaussian9BlurSrt *pSrt : S_SRT_DATA)
{
	pSrt->m_dstTex[dispatchId] = Gaussian9BlurHelper(dispatchId, pSrt, float2(0, 1));
}

[NUM_THREADS(8, 8, 1)]
void Cs_Gaussian5BlurX(int2 dispatchId : S_DISPATCH_THREAD_ID, Gaussian9BlurSrt *pSrt : S_SRT_DATA)
{
	pSrt->m_dstTex[dispatchId] = float4(Gaussian5BlurHelper(dispatchId, pSrt, int2(1, 0)).xyz, 1.f);
}

[NUM_THREADS(8, 8, 1)]
void Cs_Gaussian5BlurY(int2 dispatchId : S_DISPATCH_THREAD_ID, Gaussian9BlurSrt *pSrt : S_SRT_DATA)
{
	pSrt->m_dstTex[dispatchId] = float4(Gaussian5BlurHelper(dispatchId, pSrt, int2(0, 1)).xyz, 1.f);
}

[NUM_THREADS(8, 8, 1)]
void Cs_Box3Blur(int2 dispatchId : S_DISPATCH_THREAD_ID, Gaussian9BlurSrt *pSrt : S_SRT_DATA)
{
#if 0
	float4 avg = pSrt->m_srcTex.SampleLevel(pSrt->m_linearSampler, dispatchId * pSrt->m_invSize, 0);
	avg += pSrt->m_srcTex.SampleLevel(pSrt->m_linearSampler, (dispatchId + float2(1.5f, 0.f)) * pSrt->m_invSize, 0);
	avg += pSrt->m_srcTex.SampleLevel(pSrt->m_linearSampler, (dispatchId + float2(0.f, 1.5f)) * pSrt->m_invSize, 0);
	avg += pSrt->m_srcTex[dispatchId + uint2(1,1)];
	avg /= 9;

#else
	const int2 offsets[9] = {
		{-1, -1}, {0, -1}, {1, -1},
		{-1, 0}, {0, 0}, {1, 0},
		{-1, 1}, {0, 1}, {1, 1}
	};

	const float weights[] = {1/16.f, 1/8.f, 1/16.f,
		1/8.f, 1/4.f, 1/8.f, 1/16.f, 1/8.f, 1/16.f};

	float4 avg=0;

	for (uint i = 0; i < 9; ++i)
	{
		avg += pSrt->m_srcTex[dispatchId + offsets[i]] * weights[i];
	}
#endif

	pSrt->m_dstTex[dispatchId] = avg;
}

// A cheap tone mapping algorithm that maps the range of HDR value to LDR to avoid value more than 1.0 to bleed back to foreground UI.
// This tone mapping curve is largely inspired by 'Filmic Tonemapping with Piecewise Power Curves', John Hable.
// http://filmicworlds.com/blog/filmic-tonemapping-with-piecewise-power-curves/
// However, since our primary focus of doing this is to prevent things from bleeding to UI, we don't really care about the toe part
// mentioned in the blog. And our mathematics is a bit different than the original one proposed in the blog.
float	ApplyCheapToneMapping(const in float linearColor, const in float clampedValue)
{
	// Basically, we divide the curve into three parts
	//  - Linear part
	//		this ranges from 0 to x0, which is hard coded as 0.6 in our implementation. We can expose the value in the future,
	//		but not this project since even if it is exposed, there is no bandwidth of tuning it for every task.
	//  - Clampped part
	//		clampping happens after x1, which is also hard coded as 4.0 in this implementation for the same reason.
	//	- Curvic part
	//		this connects the linear and clampped part in a way the derivatives of the whole curve is continuous, in other words, first order continuous.
	//		this part is the only part that requires a bit of math, it works this way
	//			- f(x) = -A * ( a - x )^B + b
	//			  |- f(x0)	= x0    \     a = x1
	//			  |  f'(f0) = 1   ---\    b = C
	//			  |  f(x1)	= C   ---/    B = ( a - x0 ) / ( b - x0 )
	//			  |- f'(x1) = 0     /     ln(A) = -ln(B) + ( 1 - B ) * ln( a - x0 )
	//
	//		C is the clampped value. Instead of evaluating A, ln(A) is used to avoid precision issues.

	constexpr float x0 = 0.6f;
	constexpr float x1 = 4.0f;
	constexpr float a = x1;
	const float C = clampedValue;
	const float b = C;
	const float B = ( a - x0 ) / ( b - x0 );
	const float lnA = -log(B) + ( 1 - B ) * log( a - x0 );

	if( linearColor <= x0 )
		return linearColor;
	if( linearColor >= x1 )
		return C;

	return -exp( lnA + B * log( a - linearColor ) ) + b;
}

float3	ApplyCheapToneMapping( const in float3 linear_color, const in float clamped_value ){
	return float3( ApplyCheapToneMapping(linear_color.r,clamped_value) , ApplyCheapToneMapping(linear_color.g,clamped_value) , ApplyCheapToneMapping(linear_color.b,clamped_value) );
}

#define kMaxNumVariableBlurWeights 11

struct VariableBlurSrt
{
	Texture2D<float3>		m_srcTex;
	RW_Texture2D<float3>	m_dstTex;
	int2					m_topLeft;
	int2					m_texSizeMinus1;
	float					m_clampedValue;
	float					m_weights[kMaxNumVariableBlurWeights + 1]; // We add one extra weight to avoid the gpu reading past the end of the array (shaders do not support short circuiting).
};

float3 VariableBlurHelper(VariableBlurSrt *pSrt, int2 coordSs, int2 dir , bool hdr_mode )
{
	float3 total = 0;
	
	total += pSrt->m_srcTex[coordSs] * pSrt->m_weights[0];
	for (int i = 1; i < kMaxNumVariableBlurWeights && pSrt->m_weights[i] > 0; ++i)
	{
		if ( hdr_mode )
		{
			total += ApplyCheapToneMapping(pSrt->m_srcTex[clamp(coordSs - dir*i, int(0).xx, pSrt->m_texSizeMinus1)] , pSrt->m_clampedValue ) * pSrt->m_weights[i];
			total += ApplyCheapToneMapping(pSrt->m_srcTex[clamp(coordSs + dir*i, int(0).xx, pSrt->m_texSizeMinus1)] , pSrt->m_clampedValue ) * pSrt->m_weights[i];
		}
		else
		{
			total += pSrt->m_srcTex[clamp(coordSs - dir*i, int(0).xx, pSrt->m_texSizeMinus1)] * pSrt->m_weights[i];
			total += pSrt->m_srcTex[clamp(coordSs + dir*i, int(0).xx, pSrt->m_texSizeMinus1)] * pSrt->m_weights[i];
		}
	}

	return total;
}

[NUM_THREADS(8, 8, 1)]
void Cs_VariableBlurX(int2 dispatchThreadId : S_DISPATCH_THREAD_ID, VariableBlurSrt *pSrt : S_SRT_DATA)
{
	int2 coordSs = pSrt->m_topLeft + dispatchThreadId;
	pSrt->m_dstTex[coordSs] = VariableBlurHelper(pSrt, coordSs, int2(1, 0), false);
}

[NUM_THREADS(8, 8, 1)]
void Cs_VariableBlurX_HDR(int2 dispatchThreadId : S_DISPATCH_THREAD_ID, VariableBlurSrt *pSrt : S_SRT_DATA)
{
	int2 coordSs = pSrt->m_topLeft + dispatchThreadId;
	pSrt->m_dstTex[coordSs] = VariableBlurHelper(pSrt, coordSs, int2(1, 0), true);
}

[NUM_THREADS(8, 8, 1)]
void Cs_VariableBlurY(int2 dispatchThreadId : S_DISPATCH_THREAD_ID, VariableBlurSrt *pSrt : S_SRT_DATA)
{
	int2 coordSs = pSrt->m_topLeft + dispatchThreadId;
	pSrt->m_dstTex[coordSs] = VariableBlurHelper(pSrt, coordSs, int2(0, 1) , false);
}


struct CmaskUntileSrt
{
	DataBuffer<uint>	m_srcCmask;
	RW_Texture2D<uint>  m_resultCmask;
	uint				m_numTilesX;
};


[NUM_THREADS(8, 8, 1)]
void Cs_CmaskUntile(uint2 dispatchId : S_DISPATCH_THREAD_ID, 
	uint2 groupId : S_GROUP_ID, CmaskUntileSrt *pSrt : S_SRT_DATA)
{
	uint cmaskOffset, cmaskNybble;
	computeCmaskBufferNybbleOffset(cmaskOffset, cmaskNybble, dispatchId.x, dispatchId.y, pSrt->m_numTilesX, false, g_isNeoMode);

	const uint cmaskSrcData = pSrt->m_srcCmask[cmaskOffset];
	const uint nybble = cmaskSrcData >> (cmaskNybble*4);
	// Done!
	pSrt->m_resultCmask[dispatchId] = nybble;
}

struct FocusCameraDistanceSrt
{
	float2					m_aimCursorPosition;
	float2					m_aimCursorRadius;
	float2					m_depthParams;
	float2					m_bufferSizeParams;
	float4					m_screenParams;
	RWTexture2D<float2>		m_rwFocusDistBuffer;
	Texture2D<uint>			m_txStencilBuffer;
	Texture2D<float>		m_txDepthBuffer;
	Texture2D<float>		m_txCoverMask;
	Texture2D<uint4>		m_txGbuffer0;
	SamplerState			m_sSamplerPoint;
	SamplerState			m_sSamplerLinear;
};

[NUM_THREADS(8, 8, 1)]
void Cs_GetFocusPositionCameraDistance(uint2 dispatchId : SV_DispatchThreadID, FocusCameraDistanceSrt *pSrt : S_SRT_DATA) : SV_Target
{
	float2 localUv = (dispatchId + 0.5f) * pSrt->m_bufferSizeParams;

	float2 srcUv = pSrt->m_aimCursorPosition + ((localUv * 2.0 - 1.0) * pSrt->m_aimCursorRadius) * pSrt->m_screenParams.zw;

	float depthBufferZ = pSrt->m_txDepthBuffer.SampleLevel(pSrt->m_sSamplerPoint, srcUv, 0);
	float depthVS = pSrt->m_depthParams.y / (depthBufferZ - pSrt->m_depthParams.x);
	float coverMask = pSrt->m_txCoverMask.SampleLevel(pSrt->m_sSamplerLinear, localUv, 0);
	bool hasDitherAlpha = UnpackGBufferHasDitherAlpha(pSrt->m_txGbuffer0.SampleLevel(pSrt->m_sSamplerPoint, srcUv, 0));

	float2 focusDist;
	if (depthBufferZ >= 1.f || coverMask < 0.005f || hasDitherAlpha)
	{
		focusDist.y = 1000000.0f;
		focusDist.x = 1000000.0f;
	}
	else
	{
		uint stencilValue = pSrt->m_txStencilBuffer.SampleLevel(pSrt->m_sSamplerPoint, srcUv, 0);

		focusDist.y = (stencilValue & 0x80) != 0  ? depthVS : 1000000.0f;
		focusDist.x = depthVS;
	}

	float2 focusDist01, focusDist08, focusDist02, focusDist10;
	focusDist01.x = min(focusDist.x, LaneSwizzle(focusDist.x, 0x1f, 0x00, 0x01));
	focusDist08.x = min(focusDist01.x, LaneSwizzle(focusDist01.x, 0x1f, 0x00, 0x08));
	focusDist02.x = min(focusDist08.x, LaneSwizzle(focusDist08.x, 0x1f, 0x00, 0x02));
	focusDist10.x = min(focusDist02.x, LaneSwizzle(focusDist02.x, 0x1f, 0x00, 0x10));
	focusDist01.y = min(focusDist.y, LaneSwizzle(focusDist.y, 0x1f, 0x00, 0x01));
	focusDist08.y = min(focusDist01.y, LaneSwizzle(focusDist01.y, 0x1f, 0x00, 0x08));
	focusDist02.y = min(focusDist08.y, LaneSwizzle(focusDist08.y, 0x1f, 0x00, 0x02));
	focusDist10.y = min(focusDist02.y, LaneSwizzle(focusDist02.y, 0x1f, 0x00, 0x10));

	if ((dispatchId.x % 4) == 0 && (dispatchId.y % 4) == 0)
		pSrt->m_rwFocusDistBuffer[dispatchId / 4] = focusDist10;
}

struct Collect8x8FocusDistanceSrt
{
	RWBuffer<float>				m_rwFocusDistBuffer;
	Texture2D<float2>			m_txFocusDistBuffer;
};

[NUM_THREADS(8, 8, 1)]
void CS_Collect8x8FocusDistanceBuffer(uint2 dispatchId : SV_DispatchThreadID, Collect8x8FocusDistanceSrt *pSrt : S_SRT_DATA)
{
	float2 focusDist = pSrt->m_txFocusDistBuffer[dispatchId];

	float2 focusDist01, focusDist08, focusDist02, focusDist10, focusDist04, finalFocusDist;
	focusDist01.x = min(focusDist.x, LaneSwizzle(focusDist.x, 0x1f, 0x00, 0x01));
	focusDist08.x = min(focusDist01.x, LaneSwizzle(focusDist01.x, 0x1f, 0x00, 0x08));
	focusDist02.x = min(focusDist08.x, LaneSwizzle(focusDist08.x, 0x1f, 0x00, 0x02));
	focusDist10.x = min(focusDist02.x, LaneSwizzle(focusDist02.x, 0x1f, 0x00, 0x10));
	focusDist04.x = min(focusDist10.x, LaneSwizzle(focusDist10.x, 0x1f, 0x00, 0x04));
	finalFocusDist.x = min(focusDist04.x, ReadLane(focusDist04.x, 0x20));
	focusDist01.y = min(focusDist.y, LaneSwizzle(focusDist.y, 0x1f, 0x00, 0x01));
	focusDist08.y = min(focusDist01.y, LaneSwizzle(focusDist01.y, 0x1f, 0x00, 0x08));
	focusDist02.y = min(focusDist08.y, LaneSwizzle(focusDist08.y, 0x1f, 0x00, 0x02));
	focusDist10.y = min(focusDist02.y, LaneSwizzle(focusDist02.y, 0x1f, 0x00, 0x10));
	focusDist04.y = min(focusDist10.y, LaneSwizzle(focusDist10.y, 0x1f, 0x00, 0x04));
	finalFocusDist.y = min(focusDist04.y, ReadLane(focusDist04.y, 0x20));

	if (dispatchId.x == 0 && dispatchId.y == 0)
		pSrt->m_rwFocusDistBuffer[0] = finalFocusDist.y < 500000.0f ? finalFocusDist.y : finalFocusDist.x;
}

uint CalculateDilateDepth(int2 groupThreadId, float srcDepth[10][10])
{
	float centerDepth = srcDepth[groupThreadId.y + 1][groupThreadId.x + 1];
	float4 cornerDepth = float4(srcDepth[groupThreadId.y][groupThreadId.x], 
								srcDepth[groupThreadId.y + 2][groupThreadId.x + 0], 
								srcDepth[groupThreadId.y + 0][groupThreadId.x + 2], 
								srcDepth[groupThreadId.y + 2][groupThreadId.x + 2]);

	float minDepth1 = min3(centerDepth, cornerDepth.x, cornerDepth.y);
	float minDepth = min3(minDepth1, cornerDepth.z, cornerDepth.w);

	int bitOffset = 4;
	if (minDepth != minDepth1)
	{
		cornerDepth.xy = cornerDepth.zw;
		bitOffset = 12;
	}

	if (minDepth == cornerDepth.y)
		bitOffset += 4;
	else if (minDepth == centerDepth)
		bitOffset = 0;

	return (0x000a2805 >> bitOffset);
}

uint CalculateTemporalDepthInfo(int2 groupThreadId, float srcDepth[10][10], uint srcStencil[10][10])
{
	uint offsetBits = CalculateDilateDepth(groupThreadId, srcDepth);

	uint orStencilBits = srcStencil[groupThreadId.y + 0][groupThreadId.x + 0] | srcStencil[groupThreadId.y + 1][groupThreadId.x + 0] | srcStencil[groupThreadId.y + 2][groupThreadId.x + 0] |
						 srcStencil[groupThreadId.y + 0][groupThreadId.x + 1] | srcStencil[groupThreadId.y + 1][groupThreadId.x + 1] | srcStencil[groupThreadId.y + 2][groupThreadId.x + 1] |
						 srcStencil[groupThreadId.y + 0][groupThreadId.x + 2] | srcStencil[groupThreadId.y + 1][groupThreadId.x + 2] | srcStencil[groupThreadId.y + 2][groupThreadId.x + 2];

	uint andStencilBits = srcStencil[groupThreadId.y + 0][groupThreadId.x + 0] & srcStencil[groupThreadId.y + 1][groupThreadId.x + 0] & srcStencil[groupThreadId.y + 2][groupThreadId.x + 0] &
						  srcStencil[groupThreadId.y + 0][groupThreadId.x + 1] & srcStencil[groupThreadId.y + 1][groupThreadId.x + 1] & srcStencil[groupThreadId.y + 2][groupThreadId.x + 1] &
						  srcStencil[groupThreadId.y + 0][groupThreadId.x + 2] & srcStencil[groupThreadId.y + 1][groupThreadId.x + 2] & srcStencil[groupThreadId.y + 2][groupThreadId.x + 2];

	return ((orStencilBits & 0x18) != (andStencilBits & 0x18) ? 0x80 : 0x00) | (offsetBits & 0x0f);
}

groupshared float		g_dilatedDepth[10][10];
groupshared float		g_opaqueAlphaDepth[10][10];
groupshared uint		g_opaqueAlphaStencil[10][10];

struct DilateStencilCombineSrt
{
	RWTexture2D<uint>	m_rwDilatedStencilBuffer;
	RWTexture2D<uint>	m_rwTemporalAaDepthInfo;
	RWTexture2D<uint>	m_rwDilateOffsetBuffer;
	Texture2D<float>	m_depthBuffer;
	Texture2D<float>	m_opaqueAlphaDepthBuffer;
	Texture2D<uint>		m_stencilBuffer;
	Texture2D<uint>		m_opaqueAlphaStencilBuffer;
	uint				m_frameId;
};

[NUM_THREADS(8, 8, 1)]
void CS_DilateStencilCombine(int2 dispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadId, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, DilateStencilCombineSrt* pSrt : S_SRT_DATA)
{
	int srcX = groupIndex % 10;
	int srcY = groupIndex / 10;
	int2 srcDispatchId = int2(groupId.x * 8 + srcX - 1, groupId.y * 8 + srcY - 1);

	float depth = pSrt->m_depthBuffer[srcDispatchId];
	float opaqueAlphaDepth = pSrt->m_opaqueAlphaDepthBuffer[srcDispatchId];
	uint stencil = pSrt->m_stencilBuffer[srcDispatchId];
	uint opaqueAlphaStencil = pSrt->m_opaqueAlphaStencilBuffer[srcDispatchId];
	
	g_dilatedDepth[srcY][srcX] = (stencil & 0x18) == (opaqueAlphaStencil & 0x18) ? depth : opaqueAlphaDepth;
	g_opaqueAlphaDepth[srcY][srcX] = opaqueAlphaDepth;
	g_opaqueAlphaStencil[srcY][srcX] = opaqueAlphaStencil;

	if (groupIndex < 36)
	{
		srcX = (groupIndex + 64) % 10;
		srcY = (groupIndex + 64) / 10;
		srcDispatchId = int2(groupId.x * 8 + srcX - 1, groupId.y * 8 + srcY - 1);

		depth = pSrt->m_depthBuffer[srcDispatchId];
		opaqueAlphaDepth = pSrt->m_opaqueAlphaDepthBuffer[srcDispatchId];
		stencil = pSrt->m_stencilBuffer[srcDispatchId];
		opaqueAlphaStencil = pSrt->m_opaqueAlphaStencilBuffer[srcDispatchId];

		g_dilatedDepth[srcY][srcX] = (stencil & 0x18) == (opaqueAlphaStencil & 0x18) ? depth : opaqueAlphaDepth;
		g_opaqueAlphaDepth[srcY][srcX] = opaqueAlphaDepth;
		g_opaqueAlphaStencil[srcY][srcX] = opaqueAlphaStencil;
	}

	uint offsetBits = CalculateDilateDepth(groupThreadId, g_dilatedDepth);

	int2 localOffset = int2(offsetBits, offsetBits >> 2) & 0x03;
	pSrt->m_rwDilatedStencilBuffer[dispatchThreadId] = g_opaqueAlphaStencil[groupThreadId.y + localOffset.y][groupThreadId.x + localOffset.x];
	pSrt->m_rwTemporalAaDepthInfo[dispatchThreadId] = CalculateTemporalDepthInfo(groupThreadId, g_opaqueAlphaDepth, g_opaqueAlphaStencil);

	uint shiftBits = ((dispatchThreadId.y & 0x01) * 2 + (dispatchThreadId.x & 0x01)) * 4;

	uint maskBits = (offsetBits & 0x0f) << shiftBits;

	maskBits |= LaneSwizzle(maskBits, (uint)0x1f, (uint)0x00, (uint)0x01);
	maskBits |= LaneSwizzle(maskBits, (uint)0x1f, (uint)0x00, (uint)0x08);

	if (shiftBits == 0)
		pSrt->m_rwDilateOffsetBuffer[dispatchThreadId / 2] = maskBits;
}

struct DilateStencilCombineMSSrt
{
	RWTexture2D<uint>		m_rwDilatedStencilBuffer;
	RWTexture2D<uint>		m_rwTemporalAaDepthInfo;
	RWTexture2D<uint>		m_rwDilateOffsetBuffer;
	Texture2DMS<float, 2>	m_depthBuffer;
	Texture2DMS<float, 2>	m_opaqueAlphaDepthBuffer;
	Texture2DMS<uint, 2>	m_stencilBuffer;
	Texture2DMS<uint, 2>	m_opaqueAlphaStencilBuffer;
	uint					m_frameId;
};

[NUM_THREADS(8, 8, 1)]
void CS_DilateStencilCombineMS(int2 dispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadId, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, DilateStencilCombineMSSrt* pSrt : S_SRT_DATA)
{
	int srcX = groupIndex % 10;
	int srcY = groupIndex / 10;
	int2 srcDispatchId = int2(groupId.x * 8 + srcX - 1, groupId.y * 8 + srcY - 1);
	int2 location = int2(srcDispatchId.x / 2, srcDispatchId.y);
	int sampleIndex = (srcDispatchId.x + srcDispatchId.y + pSrt->m_frameId) & 1;

	float depth = pSrt->m_depthBuffer.Load(location, sampleIndex);
	float opaqueAlphaDepth = pSrt->m_opaqueAlphaDepthBuffer.Load(location, sampleIndex);
	uint stencil = pSrt->m_stencilBuffer.Load(location, sampleIndex);
	uint opaqueAlphaStencil = pSrt->m_opaqueAlphaStencilBuffer.Load(location, sampleIndex);
	
	g_dilatedDepth[srcY][srcX] = (stencil & 0x18) == (opaqueAlphaStencil & 0x18) ? depth : opaqueAlphaDepth;
	g_opaqueAlphaDepth[srcY][srcX] = opaqueAlphaDepth;
	g_opaqueAlphaStencil[srcY][srcX] = opaqueAlphaStencil;

	if (groupIndex < 36)
	{
		srcX = (groupIndex + 64) % 10;
		srcY = (groupIndex + 64) / 10;
		srcDispatchId = int2(groupId.x * 8 + srcX - 1, groupId.y * 8 + srcY - 1);
		location = int2(srcDispatchId.x / 2, srcDispatchId.y);
		sampleIndex = (srcDispatchId.x + srcDispatchId.y + pSrt->m_frameId) & 1;

		depth = pSrt->m_depthBuffer.Load(location, sampleIndex);
		opaqueAlphaDepth = pSrt->m_opaqueAlphaDepthBuffer.Load(location, sampleIndex);
		stencil = pSrt->m_stencilBuffer.Load(location, sampleIndex);
		opaqueAlphaStencil = pSrt->m_opaqueAlphaStencilBuffer.Load(location, sampleIndex);

		g_dilatedDepth[srcY][srcX] = (stencil & 0x18) == (opaqueAlphaStencil & 0x18) ? depth : opaqueAlphaDepth;
		g_opaqueAlphaDepth[srcY][srcX] = opaqueAlphaDepth;
		g_opaqueAlphaStencil[srcY][srcX] = opaqueAlphaStencil;
	}

	uint offsetBits = CalculateDilateDepth(groupThreadId, g_dilatedDepth);

	int2 localOffset = int2(offsetBits, offsetBits >> 2) & 0x03;
	pSrt->m_rwDilatedStencilBuffer[dispatchThreadId] = g_opaqueAlphaStencil[groupThreadId.y + localOffset.y][groupThreadId.x + localOffset.x];
	pSrt->m_rwTemporalAaDepthInfo[dispatchThreadId] = CalculateTemporalDepthInfo(groupThreadId, g_opaqueAlphaDepth, g_opaqueAlphaStencil);

	uint shiftBits = ((dispatchThreadId.y & 0x01) * 2 + (dispatchThreadId.x & 0x01)) * 4;

	uint maskBits = (offsetBits & 0x0f) << shiftBits;

	maskBits |= LaneSwizzle(maskBits, (uint)0x1f, (uint)0x00, (uint)0x01);
	maskBits |= LaneSwizzle(maskBits, (uint)0x1f, (uint)0x00, (uint)0x08);

	if (shiftBits == 0)
		pSrt->m_rwDilateOffsetBuffer[dispatchThreadId / 2] = maskBits;
}

struct PreTemporalAaDepthPassSrt
{
	RWTexture2D<uint>	m_rwTemporalAaDepthInfo;
	RWTexture2D<uint>	m_rwDilateOffsetBuffer;
	Texture2D<float>	m_opaqueAlphaDepthBuffer;
	Texture2D<uint>		m_opaqueAlphaStencilBuffer;
};

[NUM_THREADS(8, 8, 1)]
void CS_PreTemporalAaDepthPass(int2 dispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadId, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, PreTemporalAaDepthPassSrt* pSrt : S_SRT_DATA)
{
	int srcX = groupIndex % 10;
	int srcY = groupIndex / 10;
	int2 srcDispatchId = int2(groupId.x * 8 + srcX - 1, groupId.y * 8 + srcY - 1);

	g_opaqueAlphaDepth[srcY][srcX] = pSrt->m_opaqueAlphaDepthBuffer[srcDispatchId];
	g_opaqueAlphaStencil[srcY][srcX] = pSrt->m_opaqueAlphaStencilBuffer[srcDispatchId];

	if (groupIndex < 36)
	{
		srcX = (groupIndex + 64) % 10;
		srcY = (groupIndex + 64) / 10;
		srcDispatchId = int2(groupId.x * 8 + srcX - 1, groupId.y * 8 + srcY - 1);

		g_opaqueAlphaDepth[srcY][srcX] = pSrt->m_opaqueAlphaDepthBuffer[srcDispatchId];
		g_opaqueAlphaStencil[srcY][srcX] = pSrt->m_opaqueAlphaStencilBuffer[srcDispatchId];
	}

	pSrt->m_rwTemporalAaDepthInfo[dispatchThreadId] = CalculateTemporalDepthInfo(groupThreadId, g_opaqueAlphaDepth, g_opaqueAlphaStencil);
	if ((dispatchThreadId.x & 0x01) + (dispatchThreadId.y & 0x01) == 0)
		pSrt->m_rwDilateOffsetBuffer[dispatchThreadId / 2] = 0x5555;
}

struct TestUpscaleDepthBufferSrt
{
	float2					m_pixelSize;
	float2					m_pad;
	RWTexture2D<float2>		m_rwTestUpscaleDepthBuffer;
	Texture2D<float>		m_srcDepthBuffer;
	Texture2D<float>		m_srcDsDepthBuffer;
	Texture2D<uint>			m_upscaleInfoBuffer;
};

[NUM_THREADS(8, 8, 1)]
void CS_TestUpscaleDepthBuffer(int2 dispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadId, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, TestUpscaleDepthBufferSrt* pSrt : S_SRT_DATA)
{
	uint mergeInfo = pSrt->m_upscaleInfoBuffer[dispatchThreadId.xy];

	int2 dsDispatchId = dispatchThreadId.xy / 2 + (int2(mergeInfo, mergeInfo >> 2) & 0x03) - 1;
	
	float dsDepth = pSrt->m_srcDsDepthBuffer[dsDispatchId];

	pSrt->m_rwTestUpscaleDepthBuffer[dispatchThreadId] = float2(dsDepth, pSrt->m_srcDepthBuffer[dispatchThreadId.xy]);
}

struct CreateHdrLutSrt
{
	float					m_hdrLutLeastChangeBetweenSamples;
	float3					m_pad;
	RWTexture3D<float>		m_rwHdrLutBuffer;
	Texture3D<float3>		m_srcLutTexture;
};

[numthreads(1, 8, 8)]
void CS_CreateHdrLutX0(int3 dispatchThreadId : SV_DispatchThreadId, CreateHdrLutSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float a0 = pSrt->m_srcLutTexture[dispatchThreadId].r;
	float a1 = pSrt->m_srcLutTexture[dispatchThreadId+int3(1, 0, 0)].r;
	float a2 = pSrt->m_srcLutTexture[dispatchThreadId+int3(2, 0, 0)].r;
	pSrt->m_rwHdrLutBuffer[dispatchThreadId] = a0;
	pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(1, 0, 0)] = a1;
	pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(2, 0, 0)] = a2;

	for (int i = 3; i < 32; i++)
	{
		float a3 = pSrt->m_srcLutTexture[dispatchThreadId+int3(i, 0, 0)].r;

		float maxX = a2 + (a2 - a1) * pSrt->m_hdrLutLeastChangeBetweenSamples;

		if (a3.x == 1.0f)
			a3 = max((a2 - a1) * 3.0f + a0, maxX);
		else
			a3 = max(a3, maxX);

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(i, 0, 0)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}

	for (int i = 32; i < 64; i++)
	{
		float maxX = a2 + (a2 - a1) * pSrt->m_hdrLutLeastChangeBetweenSamples;

		float a3 = max((a2 - a1) * 3.0f + a0, maxX);

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(i, 0, 0)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}
}

[numthreads(8, 1, 8)]
void CS_CreateHdrLutX1(int3 dispatchThreadId : SV_DispatchThreadId, CreateHdrLutSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float a0 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 29, 0)];
	float a1 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 30, 0)];
	float a2 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 31, 0)];

	for (int i = 32; i < 64; i++)
	{
		float a3 = (a2 - a1) * 3.0f + a0;

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, i, 0)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}
}

[numthreads(8, 8, 1)]
void CS_CreateHdrLutX2(int3 dispatchThreadId : SV_DispatchThreadId, CreateHdrLutSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float a0 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, 29)];
	float a1 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, 30)];
	float a2 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, 31)];

	for (int i = 32; i < 64; i++)
	{
		float a3 = (a2 - a1) * 3.0f + a0;

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, i)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}
}

[numthreads(8, 1, 8)]
void CS_CreateHdrLutY0(int3 dispatchThreadId : SV_DispatchThreadId, CreateHdrLutSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float a0 = pSrt->m_srcLutTexture[dispatchThreadId].g;
	float a1 = pSrt->m_srcLutTexture[dispatchThreadId+int3(0, 1, 0)].g;
	float a2 = pSrt->m_srcLutTexture[dispatchThreadId+int3(0, 2, 0)].g;
	pSrt->m_rwHdrLutBuffer[dispatchThreadId] = a0;
	pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 1, 0)] = a1;
	pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 2, 0)] = a2;

	for (int i = 3; i < 32; i++)
	{
		float a3 = pSrt->m_srcLutTexture[dispatchThreadId+int3(0, i, 0)].g;

		float maxX = a2 + (a2 - a1) * pSrt->m_hdrLutLeastChangeBetweenSamples;

		if (a3.x == 1.0f)
			a3 = max((a2 - a1) * 3.0f + a0, maxX);
		else
			a3 = max(a3, maxX);

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, i, 0)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}

	for (int i = 32; i < 64; i++)
	{
		float maxX = a2 + (a2 - a1) * pSrt->m_hdrLutLeastChangeBetweenSamples;

		float a3 = max((a2 - a1) * 3.0f + a0, maxX);

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, i, 0)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}
}

[numthreads(1, 8, 8)]
void CS_CreateHdrLutY1(int3 dispatchThreadId : SV_DispatchThreadId, CreateHdrLutSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float a0 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(29, 0, 0)];
	float a1 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(30, 0, 0)];
	float a2 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(31, 0, 0)];

	for (int i = 32; i < 64; i++)
	{
		float a3 = (a2 - a1) * 3.0f + a0;

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(i, 0, 0)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}
}

[numthreads(8, 8, 1)]
void CS_CreateHdrLutY2(int3 dispatchThreadId : SV_DispatchThreadId, CreateHdrLutSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float a0 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, 29)];
	float a1 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, 30)];
	float a2 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, 31)];

	for (int i = 32; i < 64; i++)
	{
		float a3 = (a2 - a1) * 3.0f + a0;

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, i)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}
}

[numthreads(8, 8, 1)]
void CS_CreateHdrLutZ0(int3 dispatchThreadId : SV_DispatchThreadId, CreateHdrLutSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float a0 = pSrt->m_srcLutTexture[dispatchThreadId].b;
	float a1 = pSrt->m_srcLutTexture[dispatchThreadId+int3(0, 0, 1)].b;
	float a2 = pSrt->m_srcLutTexture[dispatchThreadId+int3(0, 0, 2)].b;
	pSrt->m_rwHdrLutBuffer[dispatchThreadId] = a0;
	pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, 1)] = a1;
	pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, 2)] = a2;

	for (int i = 3; i < 32; i++)
	{
		float a3 = pSrt->m_srcLutTexture[dispatchThreadId+int3(0, 0, i)].b;

		float maxX = a2 + (a2 - a1) * pSrt->m_hdrLutLeastChangeBetweenSamples;

		if (a3.x == 1.0f)
			a3 = max((a2 - a1) * 3.0f + a0, maxX);
		else
			a3 = max(a3, maxX);

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, i)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}

	for (int i = 32; i < 64; i++)
	{
		float maxX = a2 + (a2 - a1) * pSrt->m_hdrLutLeastChangeBetweenSamples;

		float a3 = max((a2 - a1) * 3.0f + a0, maxX);

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 0, i)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}
}

[numthreads(8, 1, 8)]
void CS_CreateHdrLutZ1(int3 dispatchThreadId : SV_DispatchThreadId, CreateHdrLutSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float a0 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 29, 0)];
	float a1 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 30, 0)];
	float a2 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, 31, 0)];

	for (int i = 32; i < 64; i++)
	{
		float a3 = (a2 - a1) * 3.0f + a0;

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(0, i, 0)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}
}

[numthreads(1, 8, 8)]
void CS_CreateHdrLutZ2(int3 dispatchThreadId : SV_DispatchThreadId, CreateHdrLutSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float a0 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(29, 0, 0)];
	float a1 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(30, 0, 0)];
	float a2 = pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(31, 0, 0)];

	for (int i = 32; i < 64; i++)
	{
		float a3 = (a2 - a1) * 3.0f + a0;

		pSrt->m_rwHdrLutBuffer[dispatchThreadId+int3(i, 0, 0)] = a3;

		a0 = a1;
		a1 = a2;
		a2 = a3;
	}
}

struct CreateHdrLutMergeSrt
{
	float					m_hdrLutGammaCurve;
	float3					m_pad;
	RWTexture3D<float3>		m_rwHdrLutBuffer;
	Texture3D<float>		m_srcLutRTexture;
	Texture3D<float>		m_srcLutGTexture;
	Texture3D<float>		m_srcLutBTexture;
};

[numthreads(4, 4, 4)]
void CS_CreateHdrLutMerge(int3 dispatchThreadId : SV_DispatchThreadId, CreateHdrLutMergeSrt* pSrt : S_SRT_DATA) : SV_Target
{
	pSrt->m_rwHdrLutBuffer[dispatchThreadId] = pow(float3(pSrt->m_srcLutRTexture[dispatchThreadId],
														  pSrt->m_srcLutGTexture[dispatchThreadId],
														  pSrt->m_srcLutBTexture[dispatchThreadId]), pSrt->m_hdrLutGammaCurve);
}

struct EncodeDepthSrt
{
	Texture2D<float>		m_srcTexture;
	RW_Texture2D<float4>	m_dstTexture;
};

[NUM_THREADS(8, 8, 1)]
void Cs_EncodeDepth(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID, EncodeDepthSrt *pSrt : S_SRT_DATA)
{
	float depth = pSrt->m_srcTexture[dispatchThreadId];

	// http://aras-p.info/blog/2009/07/30/encoding-floats-to-rgba-the-final/
	float4 res = float4(1, 255, 65025, 160581375) * depth;
	res = frac(res);
	res -= res.yzww * float4(1.f / 255.f, 1.f / 255.f, 1.f / 255.f, 0.f);

	pSrt->m_dstTexture[dispatchThreadId] = res;
}

struct DebugDrawShadowInfoSrt
{
	RWTexture2D<float4>		m_dstShadowInfo;
	Texture2D<uint4>		m_srcShadowInfo;
	SamplerState			m_samplerPoint;	
	uint					m_shadowIdx;
	float2					m_invBufferSize;
	uint3					m_pad;
};

[numthreads(8, 8, 1)]
void Cs_DebugShadowInfo(int2 dispatchThreadId : SV_DispatchThreadID, DebugDrawShadowInfoSrt* pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId.xy + 0.5f) * pSrt->m_invBufferSize;
	uint4 shadowInfo4 = pSrt->m_srcShadowInfo.SampleLevel(pSrt->m_samplerPoint, uv, 0);
	
	float shadowInfo = ((shadowInfo4[pSrt->m_shadowIdx / 4] >> ((pSrt->m_shadowIdx & 0x03) * 8)) & 0xff) / 255.0f;
	pSrt->m_dstShadowInfo[int2(dispatchThreadId.xy)] = shadowInfo;
}

// Dummy shader that has a V# to the global buffer used for compressed vertex attributes. We pass the global buffer
// to shaders as a uint4, and convert it to a V# on demand in the shader, to work around a Razor issue where it
// tries to grab the entire level heap on every draw (which takes forever and spams std out with error messages).
// To work around that workaround we have one compute shader with a real V# to the global buffer so Razor knows
// there is valid memory there. If we don't do this Razor will often get address out of bounds exceptions and fail
// during replays.
struct DummyShaderToTrickRazorSrt
{
	RWBuffer<uint> m_globalVertBuffer0;
	RWBuffer<uint> m_globalVertBuffer1;
	RWBuffer<uint> m_globalVertBuffer2;
	uint m_dummy;
};

[numthreads(64, 1, 1)]
void Cs_DummyShaderToTrickRazor(DummyShaderToTrickRazorSrt srt : S_SRT_DATA)
{
	if (srt.m_dummy)
	{
		srt.m_globalVertBuffer0[0] = 0;
	}
}

struct SumAllPixelsSrt
{
	Texture2D<uint> m_pixels;
	uint m_gdsCounter;
};

[numthreads(8, 8, 1)]
void Cs_SumAllPixels(uint groupIndex : SV_GroupIndex, uint2 dispatchThreadId : SV_DispatchThreadID, SumAllPixelsSrt srt : S_SRT_DATA)
{
	uint value = srt.m_pixels[dispatchThreadId];
	uint sum = CrossLaneAdd(value);

	if (groupIndex == 0)
	{
		__ds_add_u32(srt.m_gdsCounter >> 16, sum, __kDs_GDS);
	}
}
