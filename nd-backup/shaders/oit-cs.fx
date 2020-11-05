//--------------------------------------------------------------------------------------
// File: oit-cs.fx
//
// Copyright (c) Naughty Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "oit.fxi"

struct OitCommitData
{
	int						m_numLayers;

	Buffer<uint>			m_pixelStartOffset;
	Buffer<uint>			m_numPixels;
	Buffer<uint>			m_pixelList;

	Texture2D<uint2>		m_oitColor[kOitMaxNumLayers];
	RWTexture2D<int>		m_oitCount;
	RWTexture2D<float4>		m_output;
};

struct OitCommitSrt
{
	uint					m_objectId;
	uint					m_padding0;
	OitCommitData *			m_pOitCommitData;
};

[numthreads(64, 1, 1)]
void CS_OitCommit(uint dispatchId : SV_DispatchThreadId, OitCommitSrt srt : S_SRT_DATA)
{
	uint pixelIndex = dispatchId;
	uint numPixels = srt.m_pOitCommitData->m_numPixels[srt.m_objectId];
	if (pixelIndex >= numPixels)
	{
		return;
	}

	uint mask = 0;
	uint offset = srt.m_pOitCommitData->m_pixelStartOffset[srt.m_objectId];
	uint2 pixel = OitUnpackPixelIndex(srt.m_pOitCommitData->m_pixelList[offset + pixelIndex], mask);

	int begin = srt.m_pOitCommitData->m_oitCount[pixel] - 1;
	int end = __v_ffbl_b32(mask);

	if (end > begin)
	{
		return;
	}

	srt.m_pOitCommitData->m_oitCount[pixel] = end;

	float3 dst = srt.m_pOitCommitData->m_output[pixel].rgb;
	for (int i = kOitMaxNumLayers - 1; i >= 0; i--)
	{
		if (i <= begin && i >= end)
		{
			float4 src = OitUnpackColor(srt.m_pOitCommitData->m_oitColor[i][pixel]);
			dst = lerp(dst, src.rgb, src.a);
		}
	}

	srt.m_pOitCommitData->m_output[pixel] = float4(dst, 1.0f);
}
