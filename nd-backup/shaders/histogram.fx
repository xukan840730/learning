#include "global-srt.fxi"
#include "post-globals.fxi"
#include "post-processing-common.fxi"

struct HistogramTonemapParams
{
	uint4						m_histogramInfo;
	uint4						m_histogramInfo1;
	uint4						m_histogramInfo2;
	float4						m_screenSizeParams;
};

#define kNumHistogramBins		1024

groupshared uint g_blockCount;
groupshared uint g_histogramAccBufferU16[kNumHistogramBins / 2];

struct HistogramFastTexs
{
	Texture2D<float4>	tex_src;
	RWTexture2D<uint4>	rwt_dst;
};

struct HistogramFastSamplers
{
	SamplerState sSamplerLinear;
};

struct HistogramFastSrt
{
	HistogramTonemapParams *consts;
	HistogramFastTexs *texs;
	HistogramFastSamplers *smpls;
};

[numthreads(32, 16, 1)]
void CS_DoHistogramFast(uint3 dispatchThreadId : SV_DispatchThreadID, 
                        uint3 groupId: SV_GroupID, 
                        uint groupIndex : SV_GroupIndex,
                        HistogramFastSrt srt : S_SRT_DATA)
{
	g_histogramAccBufferU16[groupIndex] = 0;

	GroupMemoryBarrierWithGroupSync();

	uint blockId = groupId.y * srt.consts->m_histogramInfo.z + groupId.x;

	uint blockY = dispatchThreadId.y;
	uint blockX = dispatchThreadId.x;

	// create 256 sum color.

	float2 srcDispatchId = float2(blockX * 4 + 0.5f, blockY * 4 + 0.5f);

	float u0 = (srt.consts->m_histogramInfo2.x + srcDispatchId.x) * srt.consts->m_screenSizeParams.x;
	float v0 = (srt.consts->m_histogramInfo2.y + srcDispatchId.y) * srt.consts->m_screenSizeParams.y;
/*	float u1 = (blockX * 4 + 2.5f) * srt.consts->m_screenSizeParams.x;
	float v1 = (blockY * 4 + 2.5f) * srt.consts->m_screenSizeParams.y;

	float2 ratioX = float2(u0, u1) < 1.0f ? 1.0f : 0.0f;
	float2 ratioY = float2(v0, v1) < 1.0f ? 1.0f : 0.0f;

	float sumWeight = (ratioX.x + ratioX.y) * (ratioY.x + ratioY.y);*/

//	if (sumWeight > 0.0f)
	if (srcDispatchId.x < srt.consts->m_histogramInfo2.z && srcDispatchId.y < srt.consts->m_histogramInfo2.w)
	{
		float3 sumColor = 0.0f;
		sumColor += srt.texs->tex_src.SampleLevel(srt.smpls->sSamplerLinear, float2(u0, v0), 0).xyz;
/*		sumColor += srt.texs->tex_src.SampleLevel(srt.smpls->sSamplerLinear, float2(u1, v0), 0).xyz * ratioX.y;
		sumColor += srt.texs->tex_src.SampleLevel(srt.smpls->sSamplerLinear, float2(u0, v1), 0).xyz * ratioY.y;
		sumColor += srt.texs->tex_src.SampleLevel(srt.smpls->sSamplerLinear, float2(u1, v1), 0).xyz * ratioX.y * ratioY.y;*/

		float3 sampleColor = sumColor;//sumColor / sumWeight;
		float brightness = dot(sampleColor, kLuminanceFactor);
		float logBrightness = brightness == 0.0f ? -16 : clamp(log2(brightness), -16, 6);

		uint sampleIndex = uint(saturate((logBrightness + 16) / 22.0f) * (kNumHistogramBins - 1));

		uint uintIndex = sampleIndex / 2;
		uint uintShiftBit = sampleIndex % 2 == 0 ? 0x01 : 0x10000;

		uint oldValue = 0;
		InterlockedAdd(g_histogramAccBufferU16[uintIndex], uintShiftBit, oldValue);
	}

	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < kNumHistogramBins / 8)
	{
		srt.texs->rwt_dst[int2(groupIndex, blockId)] = uint4(g_histogramAccBufferU16[groupIndex * 4], 
															 g_histogramAccBufferU16[groupIndex * 4 + 1],
															 g_histogramAccBufferU16[groupIndex * 4 + 2], 
															 g_histogramAccBufferU16[groupIndex * 4 + 3]);
	}
}

struct DebugHistogramFastTexs
{
	Texture2D<float4>	tex_src;
	ByteBuffer			tonemap_info;
	Texture3D<float4>	lutTexture;
	RWTexture2D<uint4>	rwt_dst;
};

struct DebugHistogramParams
{
	uint				m_debugHistogramMode;
	uint				m_enableTonemap;
	uint				m_enableHdrLut;
	uint				m_enableLut;
	float4				m_tonemapControlParameter;
	float4				m_filmicTonemapParameterCmp0;
	float4				m_filmicTonemapParameterCmp1;
	float4				m_filmicTonemapParameter0;
	float4				m_contrastGamma;
	float4				m_lutParams;
};

struct DebugHistogramFastSrt
{
	HistogramTonemapParams	*consts;
	DebugHistogramParams	*debugConsts;
	DebugHistogramFastTexs	*texs;
	HistogramFastSamplers	*smpls;
};

[numthreads(32, 16, 1)]
void CS_DoDebugHistogramFast(uint3 dispatchThreadId : SV_DispatchThreadID, 
                        uint3 groupId: SV_GroupID, 
                        uint groupIndex : SV_GroupIndex,
                        DebugHistogramFastSrt srt : S_SRT_DATA)
{
	g_histogramAccBufferU16[groupIndex] = 0;

	GroupMemoryBarrierWithGroupSync();

	uint blockId = groupId.y * srt.consts->m_histogramInfo.z + groupId.x;

	uint blockY = dispatchThreadId.y;
	uint blockX = dispatchThreadId.x;

	// create 256 sum color.

	float2 srcDispatchId = float2(blockX * 4 + 0.5f, blockY * 4 + 0.5f);

	float u0 = (srt.consts->m_histogramInfo2.x + srcDispatchId.x) * srt.consts->m_screenSizeParams.x;
	float v0 = (srt.consts->m_histogramInfo2.y + srcDispatchId.y) * srt.consts->m_screenSizeParams.y;

	if (srcDispatchId.x < srt.consts->m_histogramInfo2.z && srcDispatchId.y < srt.consts->m_histogramInfo2.w)
	{
		float3 srcColor = srt.texs->tex_src.SampleLevel(srt.smpls->sSamplerLinear, float2(u0, v0), 0).xyz;

		float3 sampleColor = srcColor;
		if (srt.debugConsts->m_debugHistogramMode >= 4)
		{
			float3 exposedDiffuseColor = srcColor;
			if (srt.debugConsts->m_enableTonemap)
			{
				exposedDiffuseColor = ApplyExposure(srt.texs->tonemap_info, srt.debugConsts->m_tonemapControlParameter, srcColor);
				if(!srt.debugConsts->m_enableHdrLut)
				{
					exposedDiffuseColor = ApplyTonemapping( srt.debugConsts->m_filmicTonemapParameterCmp0,
															srt.debugConsts->m_filmicTonemapParameterCmp1, 
															srt.debugConsts->m_filmicTonemapParameter0.x, 
															float2(u0, v0), exposedDiffuseColor );
				}
			}

			if(srt.debugConsts->m_enableHdrLut && srt.debugConsts->m_enableLut) //do HDR lut
			{
				// Lut input is sRGB range [0,2], output is sRGB range [0,1]
				sampleColor = exposedDiffuseColor < 0.03928f / 12.92f ? 
							  exposedDiffuseColor * 12.92 : 
							  pow(exposedDiffuseColor, 1.0/2.4) * 1.055 - 0.055f;

				sampleColor = saturate(sampleColor/2.0);
		
				float3 lutIndex = sampleColor * srt.debugConsts->m_lutParams.y + srt.debugConsts->m_lutParams.z;
				sampleColor = srt.texs->lutTexture.SampleLevel(srt.smpls->sSamplerLinear, lutIndex, 0).xyz;
			}
			else
			{
				exposedDiffuseColor = saturate(exposedDiffuseColor);

				// converted to srgb space instead of gamma 2.2.
				sampleColor = exposedDiffuseColor < 0.03928f / 12.92f ? 
							  exposedDiffuseColor * 12.92 : 
							  pow(exposedDiffuseColor, 1.0/2.4) * 1.055 - 0.055f;

				if(srt.debugConsts->m_enableLut) //do lut
				{
					float3 lutIndex = saturate(sampleColor) * srt.debugConsts->m_lutParams.y + srt.debugConsts->m_lutParams.z;
					sampleColor = srt.texs->lutTexture.SampleLevel(srt.smpls->sSamplerLinear, lutIndex, 0).xyz;
				}
			}
		}

		float brightness = dot(sampleColor, kLuminanceFactor);
		if ((srt.debugConsts->m_debugHistogramMode & 0x03) == 0)
			brightness = sampleColor.r;
		else if ((srt.debugConsts->m_debugHistogramMode & 0x03) == 1)
			brightness = sampleColor.g;
		else if ((srt.debugConsts->m_debugHistogramMode & 0x03) == 2)
			brightness = sampleColor.b;

		float logBrightness = brightness == 0.0f ? -16 : clamp(log2(brightness), -16, 6);

		uint sampleIndex = uint(saturate((logBrightness + 16) / 22.0f) * (kNumHistogramBins - 1));

		uint uintIndex = sampleIndex / 2;
		uint uintShiftBit = sampleIndex % 2 == 0 ? 0x01 : 0x10000;

		uint oldValue = 0;
		InterlockedAdd(g_histogramAccBufferU16[uintIndex], uintShiftBit, oldValue);
	}

	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < kNumHistogramBins / 8)
	{
		srt.texs->rwt_dst[int2(groupIndex, blockId)] = uint4(g_histogramAccBufferU16[groupIndex * 4], 
															 g_histogramAccBufferU16[groupIndex * 4 + 1],
															 g_histogramAccBufferU16[groupIndex * 4 + 2], 
															 g_histogramAccBufferU16[groupIndex * 4 + 3]);
	}
}

struct HistogramCollectTexs
{
	Texture2D<uint4>	tex_src;
	RWTexture2D<uint4>	rwt_dst;
};

struct HistogramCollectSrt
{
	HistogramTonemapParams *consts;
	HistogramCollectTexs *texs;
};

[numthreads(64, 1, 1)]
void CS_DoHistogramCollect(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupId: SV_GroupID,
                           HistogramCollectSrt srt : S_SRT_DATA)
{
 	uint4 sumHistogram = 0;
	uint startLine = srt.consts->m_histogramInfo1.x * groupId.y;
 	for (uint i = startLine; i < min(startLine + srt.consts->m_histogramInfo1.x, srt.consts->m_histogramInfo.w); i++)
  		sumHistogram += srt.texs->tex_src[int2(dispatchThreadId.x, i)];
 
	srt.texs->rwt_dst[int2(dispatchThreadId.x, groupId.y)] = sumHistogram;
}

[numthreads(64, 1, 1)]
void CS_DoHistogramCollect1(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupId: SV_GroupID,
                            HistogramCollectSrt srt : S_SRT_DATA)
{
 	uint4 sumHistogram = 0;
	uint startLine = 8 * groupId.y;
 	for (uint i = startLine; i < startLine + 8; i++)
  		sumHistogram += srt.texs->tex_src[int2(dispatchThreadId.x, i)];
 
	srt.texs->rwt_dst[int2(dispatchThreadId.x, groupId.y)] = sumHistogram;
}

groupshared uint g_brightnessHistogramBuffer[1024];
groupshared uint g_brightnessHistogramBuffer1[512];
groupshared uint g_brightnessHistogramBuffer2[256];
groupshared uint g_brightnessHistogramBuffer3[128];
groupshared uint g_brightnessHistogramBuffer4[64];
groupshared uint g_brightnessHistogramBuffer5[32];
groupshared uint g_brightnessHistogramBuffer6[16];
groupshared uint g_brightnessHistogramBuffer7[8];
groupshared uint g_brightnessHistogramBuffer8[4];
groupshared uint g_brightnessHistogramBuffer9[2];
groupshared uint g_maxHistogramSamples[128];
groupshared uint g_blockCounter;

float GetSlotForGivenPercent(uint count)
{
	uint binIndex = 0;
	if (count > g_brightnessHistogramBuffer9[0])
	{
		binIndex = 2;
		count -= g_brightnessHistogramBuffer9[0];
	}

	if (count > g_brightnessHistogramBuffer8[binIndex])
	{
		count -= g_brightnessHistogramBuffer8[binIndex];
		binIndex ++;
	}
	binIndex = binIndex * 2;

	if (count > g_brightnessHistogramBuffer7[binIndex])
	{
		count -= g_brightnessHistogramBuffer7[binIndex];
		binIndex ++;
	}
	binIndex = binIndex * 2;

	if (count > g_brightnessHistogramBuffer6[binIndex])
	{
		count -= g_brightnessHistogramBuffer6[binIndex];
		binIndex ++;
	}
	binIndex = binIndex * 2;

	if (count > g_brightnessHistogramBuffer5[binIndex])
	{
		count -= g_brightnessHistogramBuffer5[binIndex];
		binIndex ++;
	}
	binIndex = binIndex * 2;

	if (count > g_brightnessHistogramBuffer4[binIndex])
	{
		count -= g_brightnessHistogramBuffer4[binIndex];
		binIndex ++;
	}
	binIndex = binIndex * 2;

	if (count > g_brightnessHistogramBuffer3[binIndex])
	{
		count -= g_brightnessHistogramBuffer3[binIndex];
		binIndex ++;
	}
	binIndex = binIndex * 2;

	if (count > g_brightnessHistogramBuffer2[binIndex])
	{
		count -= g_brightnessHistogramBuffer2[binIndex];
		binIndex ++;
	}
	binIndex = binIndex * 2;

	if (count > g_brightnessHistogramBuffer1[binIndex])
	{
		count -= g_brightnessHistogramBuffer1[binIndex];
		binIndex ++;
	}
	binIndex = binIndex * 2;

	if (count > g_brightnessHistogramBuffer[binIndex])
	{
		count -= g_brightnessHistogramBuffer[binIndex];
		binIndex ++;
	}

	return (float)binIndex + (float)count / (float)g_brightnessHistogramBuffer[binIndex];
}

struct HistogramMergeSrt
{
	HistogramTonemapParams *m_pConsts;
	uint					m_writeBrightness;
	uint					m_pad;
	RW_ByteBuffer			m_tonemapControluffer;
	Texture2D<uint4>		m_listTexture;
};

void MergeHistogram(inout uint4 sumHistogram0, inout uint4 sumHistogram1, uint4 packedHistogram)
{
	uint4 histogramHi = packedHistogram >> 16;
	uint4 histogramLo = packedHistogram & 0x0000ffff;

	sumHistogram0 += uint4(histogramLo.x, histogramHi.x, histogramLo.y, histogramHi.y);
	sumHistogram1 += uint4(histogramLo.z, histogramHi.z, histogramLo.w, histogramHi.w);
}

[numthreads(128, 1, 1)]
void CS_DoHistogramMerge(uint3 dispatchThreadId : SV_DispatchThreadID, 
                         HistogramMergeSrt* pSrt  : S_SRT_DATA)
{
	uint4 sumHistogram[2];
	sumHistogram[0] = sumHistogram[1] = 0;

	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->m_listTexture[int2(dispatchThreadId.x, 0)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->m_listTexture[int2(dispatchThreadId.x, 1)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->m_listTexture[int2(dispatchThreadId.x, 2)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->m_listTexture[int2(dispatchThreadId.x, 3)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->m_listTexture[int2(dispatchThreadId.x, 4)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->m_listTexture[int2(dispatchThreadId.x, 5)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->m_listTexture[int2(dispatchThreadId.x, 6)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->m_listTexture[int2(dispatchThreadId.x, 7)]);

	g_brightnessHistogramBuffer[dispatchThreadId.x*8] = sumHistogram[0].x;
	g_brightnessHistogramBuffer[dispatchThreadId.x*8+1] = sumHistogram[0].y;
	g_brightnessHistogramBuffer[dispatchThreadId.x*8+2] = sumHistogram[0].z;
	g_brightnessHistogramBuffer[dispatchThreadId.x*8+3] = sumHistogram[0].w;
	g_brightnessHistogramBuffer[dispatchThreadId.x*8+4] = sumHistogram[1].x;
	g_brightnessHistogramBuffer[dispatchThreadId.x*8+5] = sumHistogram[1].y;
	g_brightnessHistogramBuffer[dispatchThreadId.x*8+6] = sumHistogram[1].z;
	g_brightnessHistogramBuffer[dispatchThreadId.x*8+7] = sumHistogram[1].w;

	uint maxSumHistogram = max3(max3(sumHistogram[0].x, sumHistogram[0].y, sumHistogram[0].z),
								 max3(sumHistogram[0].w, sumHistogram[1].x, sumHistogram[1].y),
								 max(sumHistogram[1].z, sumHistogram[1].w));

	g_maxHistogramSamples[dispatchThreadId.x] = maxSumHistogram;

	uint sumHistogram1[4];
	sumHistogram1[0] = sumHistogram[0].x + sumHistogram[0].y;
	sumHistogram1[1] = sumHistogram[0].z + sumHistogram[0].w;
	sumHistogram1[2] = sumHistogram[1].x + sumHistogram[1].y;
	sumHistogram1[3] = sumHistogram[1].z + sumHistogram[1].w;

	g_brightnessHistogramBuffer1[dispatchThreadId.x*4] = sumHistogram1[0];
	g_brightnessHistogramBuffer1[dispatchThreadId.x*4+1] = sumHistogram1[1];
	g_brightnessHistogramBuffer1[dispatchThreadId.x*4+2] = sumHistogram1[2];
	g_brightnessHistogramBuffer1[dispatchThreadId.x*4+3] = sumHistogram1[3];

	uint sumHistogram2[2];
	sumHistogram2[0] = sumHistogram1[0] + sumHistogram1[1];
	sumHistogram2[1] = sumHistogram1[2] + sumHistogram1[3];
	g_brightnessHistogramBuffer2[dispatchThreadId.x*2] = sumHistogram2[0];
	g_brightnessHistogramBuffer2[dispatchThreadId.x*2+1] = sumHistogram2[1];

	g_brightnessHistogramBuffer3[dispatchThreadId.x] = sumHistogram2[0] + sumHistogram2[1];
	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId.x < 64)
	{
		g_brightnessHistogramBuffer4[dispatchThreadId.x] = g_brightnessHistogramBuffer3[dispatchThreadId.x * 2] + g_brightnessHistogramBuffer3[dispatchThreadId.x * 2 + 1];
		g_maxHistogramSamples[dispatchThreadId.x] = max(g_maxHistogramSamples[dispatchThreadId.x], g_maxHistogramSamples[dispatchThreadId.x + 64]);
	}
	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId.x < 32)
	{
		g_brightnessHistogramBuffer5[dispatchThreadId.x] = g_brightnessHistogramBuffer4[dispatchThreadId.x * 2] + g_brightnessHistogramBuffer4[dispatchThreadId.x * 2 + 1];
		g_maxHistogramSamples[dispatchThreadId.x] = max(g_maxHistogramSamples[dispatchThreadId.x], g_maxHistogramSamples[dispatchThreadId.x + 32]);
	}

	if (dispatchThreadId.x < 16)
	{
		g_brightnessHistogramBuffer6[dispatchThreadId.x] = g_brightnessHistogramBuffer5[dispatchThreadId.x * 2] + g_brightnessHistogramBuffer5[dispatchThreadId.x * 2 + 1];
		g_maxHistogramSamples[dispatchThreadId.x] = max(g_maxHistogramSamples[dispatchThreadId.x], g_maxHistogramSamples[dispatchThreadId.x + 16]);
	}

	if (dispatchThreadId.x < 8)
	{
		g_brightnessHistogramBuffer7[dispatchThreadId.x] = g_brightnessHistogramBuffer6[dispatchThreadId.x * 2] + g_brightnessHistogramBuffer6[dispatchThreadId.x * 2 + 1];
		g_maxHistogramSamples[dispatchThreadId.x] = max(g_maxHistogramSamples[dispatchThreadId.x], g_maxHistogramSamples[dispatchThreadId.x + 8]);
	}

	if (dispatchThreadId.x < 4)
	{
		g_brightnessHistogramBuffer8[dispatchThreadId.x] = g_brightnessHistogramBuffer7[dispatchThreadId.x * 2] + g_brightnessHistogramBuffer7[dispatchThreadId.x * 2 + 1];
		g_maxHistogramSamples[dispatchThreadId.x] = max(g_maxHistogramSamples[dispatchThreadId.x], g_maxHistogramSamples[dispatchThreadId.x + 4]);
	}

	if (dispatchThreadId.x < 2)
	{
		g_brightnessHistogramBuffer9[dispatchThreadId.x] = g_brightnessHistogramBuffer8[dispatchThreadId.x * 2] + g_brightnessHistogramBuffer8[dispatchThreadId.x * 2 + 1];
		g_maxHistogramSamples[dispatchThreadId.x] = max(g_maxHistogramSamples[dispatchThreadId.x], g_maxHistogramSamples[dispatchThreadId.x + 2]);
	}

	if (dispatchThreadId.x == 0)
	{
		float indexAndRatio = GetSlotForGivenPercent(pSrt->m_pConsts->m_histogramInfo1.y);
		float indexAndRatio1 = GetSlotForGivenPercent(pSrt->m_pConsts->m_histogramInfo1.w);
		float brightness = exp2(indexAndRatio / float(kNumHistogramBins - 1) * 22.0f - 16.0f);
		if (pSrt->m_writeBrightness)
			StoreAsFloat(pSrt->m_tonemapControluffer, brightness, 8);
		StoreAsUInt(pSrt->m_tonemapControluffer, indexAndRatio, 16);
		StoreAsUInt(pSrt->m_tonemapControluffer, max(g_maxHistogramSamples[0], g_maxHistogramSamples[1]), 20);
		StoreAsUInt(pSrt->m_tonemapControluffer, (uint)indexAndRatio1, 24);
	}
}

struct HistogramDebugSrt
{
	HistogramTonemapParams *consts;
	uint					channelType;
	uint2					bufferOffset;
	uint					pad;
	Texture2D<uint4>		tex_src;
	RWTexture2D<float4>		tex_dst;
	ByteBuffer				tonemap_info;
};

float4 GetDebugColor(uint channelId, float curHeight, uint curSlotIdx, uint oldSlotIdx, float refHeight, uint pickSlotIdx, uint slotIndex50, uint dstSlotIdx)
{
	float4 fillColors[4] = {float4(0.5f, 0, 0, 1), float4(0, 0.5f, 0, 1), float4(0, 0, 0.5f, 1), float4(0.1f, 0.1f, 0.1f, 1)};
	float4 dstBarColors[4] = {float4(0, 0, 1, 0), float4(0, 0, 1, 0), float4(1, 0, 0, 0), float4(0, 0, 1, 0)};
	float4 pickColors[4] = {float4(0, 1, 0, 0), float4(1, 0, 0, 0), float4(0, 1, 0, 0), float4(0, 1, 0, 0)};

	uint curBlockIdx = uint(oldSlotIdx / (1024.0f/22.0f));
	uint nextBlockIdx = uint((oldSlotIdx + 1) / (1024.0f/22.0f));
	float4 finalColor = (curHeight > refHeight ? fillColors[channelId] : 0.0f) + 
		   ((pickSlotIdx == curSlotIdx) ? pickColors[channelId] : 0) + 
		   ((abs(dstSlotIdx -= oldSlotIdx) < 2) ? dstBarColors[channelId] : 0) +
		   ((slotIndex50 == curSlotIdx) ? float4(1, 1, 0, 0) : 0) + 
		   ((curBlockIdx != nextBlockIdx && (nextBlockIdx == 16 || ((uint)refHeight % 8) < 4)) ? float4(1, 1, 1, 0) : 0);

	finalColor.w = 0.5f;

	return finalColor;
}

uint ReverseScaleSlotIndex(out bool bNeedClip, uint slotIndex, HistogramDebugSrt* pSrt)
{
	float brightness = exp2(slotIndex / float(kNumHistogramBins - 1) * 22.0f - 16.0f);
	float tonemapScale = asfloat(pSrt->tonemap_info.Load(0));
	float scaledBrightness = brightness / tonemapScale;

	float scaledChart = (log2(scaledBrightness) + 16.0f) / 22.0f;
	bNeedClip = scaledChart < 0.0f;

	return uint(saturate(scaledChart) * float(kNumHistogramBins - 1));
}

uint ScaleSlotIndex(uint slotIndex, HistogramDebugSrt* pSrt)
{
	float brightness = exp2(slotIndex / float(kNumHistogramBins - 1) * 22.0f - 16.0f);
	float tonemapScale = asfloat(pSrt->tonemap_info.Load(0));
	float scaledBrightness = brightness * tonemapScale;
	return uint(saturate((log2(scaledBrightness) + 16.0f) / 22.0f) * float(kNumHistogramBins - 1));
}

[numthreads(1, 128, 1)]
void CS_DebugHistogramImage(uint3 dispatchThreadId : SV_DispatchThreadID, HistogramDebugSrt* pSrt  : S_SRT_DATA)
{
	uint4 sumHistogram[2];
	sumHistogram[0] = sumHistogram[1] = 0;

	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(dispatchThreadId.x, 0)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(dispatchThreadId.x, 1)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(dispatchThreadId.x, 2)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(dispatchThreadId.x, 3)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(dispatchThreadId.x, 4)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(dispatchThreadId.x, 5)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(dispatchThreadId.x, 6)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(dispatchThreadId.x, 7)]);

	uint slotIndex = pSrt->tonemap_info.Load(16);
	uint maxSamplesCount = pSrt->tonemap_info.Load(20);
	uint slotIndex50 = pSrt->tonemap_info.Load(24);

	float4 height0 = sumHistogram[0] / float(maxSamplesCount) * 128.0f;
	float4 height1 = sumHistogram[1] / float(maxSamplesCount) * 128.0f;

	uint refHeight = 127 - dispatchThreadId.y;

	uint curSlotIdx = dispatchThreadId.x * 8;
	pSrt->tex_dst[int2(curSlotIdx, dispatchThreadId.y) + pSrt->bufferOffset] = GetDebugColor(pSrt->channelType, height0.x, curSlotIdx, curSlotIdx, refHeight, slotIndex, slotIndex50, pSrt->consts->m_histogramInfo1.z); curSlotIdx++;
	pSrt->tex_dst[int2(curSlotIdx, dispatchThreadId.y) + pSrt->bufferOffset] = GetDebugColor(pSrt->channelType, height0.y, curSlotIdx, curSlotIdx, refHeight, slotIndex, slotIndex50, pSrt->consts->m_histogramInfo1.z); curSlotIdx++;
	pSrt->tex_dst[int2(curSlotIdx, dispatchThreadId.y) + pSrt->bufferOffset] = GetDebugColor(pSrt->channelType, height0.z, curSlotIdx, curSlotIdx, refHeight, slotIndex, slotIndex50, pSrt->consts->m_histogramInfo1.z); curSlotIdx++;
	pSrt->tex_dst[int2(curSlotIdx, dispatchThreadId.y) + pSrt->bufferOffset] = GetDebugColor(pSrt->channelType, height0.w, curSlotIdx, curSlotIdx, refHeight, slotIndex, slotIndex50, pSrt->consts->m_histogramInfo1.z); curSlotIdx++;
	pSrt->tex_dst[int2(curSlotIdx, dispatchThreadId.y) + pSrt->bufferOffset] = GetDebugColor(pSrt->channelType, height1.x, curSlotIdx, curSlotIdx, refHeight, slotIndex, slotIndex50, pSrt->consts->m_histogramInfo1.z); curSlotIdx++;
	pSrt->tex_dst[int2(curSlotIdx, dispatchThreadId.y) + pSrt->bufferOffset] = GetDebugColor(pSrt->channelType, height1.y, curSlotIdx, curSlotIdx, refHeight, slotIndex, slotIndex50, pSrt->consts->m_histogramInfo1.z); curSlotIdx++;
	pSrt->tex_dst[int2(curSlotIdx, dispatchThreadId.y) + pSrt->bufferOffset] = GetDebugColor(pSrt->channelType, height1.z, curSlotIdx, curSlotIdx, refHeight, slotIndex, slotIndex50, pSrt->consts->m_histogramInfo1.z); curSlotIdx++;
	pSrt->tex_dst[int2(curSlotIdx, dispatchThreadId.y) + pSrt->bufferOffset] = GetDebugColor(pSrt->channelType, height1.w, curSlotIdx, curSlotIdx, refHeight, slotIndex, slotIndex50, pSrt->consts->m_histogramInfo1.z);
}

#define kMaxSamples		32
struct TweakingToneCurveSrt
{
	float					m_opacity;
	float					m_selectX;
	float					m_selectRatio;
	uint					m_numSamplePoints;
	int						m_selectedSampleIdx;
	int						m_chooseSampleIdx;
	uint2					m_pad;
	float2					m_samplePoints[kMaxSamples];
	float					m_samplesY[1024];
	RWTexture2D<float4>		m_rwbImage;
};

bool IsHitSamplePoint(float2 pixelPoint, float2 samplePoint)
{
	float2 offsets = abs(samplePoint - pixelPoint);

	if (max(offsets.x, offsets.y) <= 3)
		return true;

	return false;
}

int IsHitSamplePoints(float2 pixelPoint, int selectSampleIdx, int chooseSampleIdx, uint numSamplePoints, float2 samplePoints[kMaxSamples])
{
	for (int i = 0; i < numSamplePoints; i++)
	{
		if (IsHitSamplePoint(pixelPoint, samplePoints[i]))
			return chooseSampleIdx == i ? 2 : (selectSampleIdx == i ? 1 : 0);
	}

	return -1;
}

[numthreads(1, 272, 1)]
void CS_TweakingToneCurve(uint3 dispatchThreadId : SV_DispatchThreadID, TweakingToneCurveSrt* pSrt  : S_SRT_DATA)
{
	float4 fillColor = float4(0.2, 0.2, 0.5, pSrt->m_opacity);
	float4 zeroColor = float4(0.0, 0.0, 0.0, pSrt->m_opacity);
	uint pixelHeight = 272 - dispatchThreadId.y;
	uint refHeight = (dispatchThreadId.x / 1024.0f) * 272.0f;

	{
		refHeight = pSrt->m_samplesY[dispatchThreadId.x];

		if (abs(pSrt->m_selectX - dispatchThreadId.x) <= 1)
			fillColor = float4(0.8, 0.2, 0.2, pSrt->m_opacity);
	}

	float4 finalColor = pixelHeight < refHeight ? fillColor : zeroColor;

	int hit = IsHitSamplePoints(float2(dispatchThreadId.x, 272 - dispatchThreadId.y), pSrt->m_selectedSampleIdx, pSrt->m_chooseSampleIdx, pSrt->m_numSamplePoints, pSrt->m_samplePoints);
	if (hit >= 0)
	{
		if (hit == 2)
			finalColor += float4(1, 1, 0, 0.2f);
		else if (hit == 1)
			finalColor += float4(1, 0, 0, 0.2f);
		else
			finalColor += float4(0, 1, 0, 0.2f);
	}

	pSrt->m_rwbImage[int2(dispatchThreadId.x, dispatchThreadId.y)] = finalColor;
}

[numthreads(1, 128, 1)]
void CS_DebugTonemappedHistogramImage(uint3 dispatchThreadId : SV_DispatchThreadID, HistogramDebugSrt* pSrt  : S_SRT_DATA)
{
	bool bNeedClip;
	uint newSlotIndex = ReverseScaleSlotIndex(bNeedClip, dispatchThreadId.x, pSrt);

	uint4 sumHistogram[2];
	sumHistogram[0] = sumHistogram[1] = 0;

	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(newSlotIndex / 8, 0)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(newSlotIndex / 8, 1)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(newSlotIndex / 8, 2)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(newSlotIndex / 8, 3)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(newSlotIndex / 8, 4)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(newSlotIndex / 8, 5)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(newSlotIndex / 8, 6)]);
	MergeHistogram(sumHistogram[0], sumHistogram[1], pSrt->tex_src[int2(newSlotIndex / 8, 7)]);

	uint4 curHistogram4 = (newSlotIndex % 8) < 4 ? sumHistogram[0] : sumHistogram[1];
	uint curHistogramHeight = bNeedClip ? 0 : curHistogram4[newSlotIndex % 4];

	uint slotIndex = pSrt->tonemap_info.Load(16);
	uint maxSamplesCount = pSrt->tonemap_info.Load(20);
	uint slotIndex50 = pSrt->tonemap_info.Load(24);
	uint targetSlotIndex = pSrt->consts->m_histogramInfo1.z;

	float height = curHistogramHeight / float(maxSamplesCount) * 128.0f;

	uint refHeight = 127 - dispatchThreadId.y;

	pSrt->tex_dst[int2(dispatchThreadId.x, dispatchThreadId.y) + pSrt->bufferOffset] = GetDebugColor(pSrt->channelType, height, newSlotIndex, dispatchThreadId.x, refHeight, slotIndex, slotIndex50, targetSlotIndex);
}
