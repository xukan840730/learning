#define SKIP_GLOBAL_SAMPLERS

#include "global-funcs.fxi"
//#include "post-globals.fxi"
//#include "post-processing-common.fxi"

struct DataBufferBlurPass1Srt
{
	uint								m_blurRadius;
	uint3								m_pad;
	Texture2D<float>					tSourceBuffer;
	RWTexture2D<float>					rwDestBuffer;
};

struct DataBlurConst
{
	int							m_bufferWidth;
	int							m_bufferHeight;
	int							m_segmentsWidth;
	int							m_segmentsHeight;
	int							m_blurRadius;
	float						m_blurWeight;
	int2						m_pad;
};

[numthreads(64, 1, 1)]
void CS_DataBufferBlurPassX1(uint3 dispatchThreadId : SV_DispatchThreadID,
                             DataBufferBlurPass1Srt* pSrt : S_SRT_DATA)
{
	uint2 bufSize;
	pSrt->tSourceBuffer.GetDimensions(bufSize.x, bufSize.y);

	float srcData = pSrt->tSourceBuffer[min(int2(dispatchThreadId.xy), int2(bufSize) - 1)];
	float srcData_s0 = dispatchThreadId.x % 2 == 1 ? 0 : srcData;

	float srcData_1 = srcData + LaneSwizzle(srcData, 0x1f, 0x00, 0x01);	//[01 01, 23 23, 45 45, 67 67.... 6263 6263]
	float srcData_m1 = srcData + LaneSwizzle(srcData_s0, 0x1f, 0x00, 0x01); //[0, 01, 2, 23, 4, 45, 6, 67.... 62, 6263]
	float srcData_s1 = (dispatchThreadId.x % 4) > 1 ? 0 : srcData_1;

	float srcData_2 = srcData_1 + LaneSwizzle(srcData_1, 0x1f, 0x00, 0x02); //[0123 0123, 4567 4567, ..... 60616263 60616263].
	float srcData_m2 = srcData_m1 + LaneSwizzle(srcData_s1, 0x1f, 0x00, 0x02); //[0 01 012 0123, ..... 60 6061 606162 60616263].
	float srcData_s2 = (dispatchThreadId.x % 8) > 3 ? 0 : srcData_2;

	float srcData_3	= srcData_2 + LaneSwizzle(srcData_2, 0x1f, 0x00, 0x04); //[0-7 0-7 0-7 0-7, 56-63 56-63 56-63 56-63].
	float srcData_m3 = srcData_m2 + LaneSwizzle(srcData_s2, 0x1f, 0x00, 0x04);
	float srcData_s3 = (dispatchThreadId.x % 16) > 7 ? 0 : srcData_3;

	float srcData_4 = srcData_3 + LaneSwizzle(srcData_3, 0x1f, 0x00, 0x08);
	float srcData_m4 = srcData_m3 + LaneSwizzle(srcData_s3, 0x1f, 0x00, 0x08);
	float srcData_s4 = (dispatchThreadId.x % 32) > 15 ? 0 : srcData_4;

	float srcData_5 = srcData_4 + LaneSwizzle(srcData_4, 0x1f, 0x00, 0x10);
	float srcData_m5 = srcData_m4 + LaneSwizzle(srcData_s4, 0x1f, 0x00, 0x10);
	float srcData_s5 = ReadLane(srcData_5, 0x00);

	float sumData = srcData_m5 + ((dispatchThreadId.x % 64) < 32 ? 0 : srcData_s5);

	pSrt->rwDestBuffer[int2(dispatchThreadId.xy)] = sumData;
}

[numthreads(16, 4, 1)]
void CS_DataBufferBlurPassX1_1(uint3 dispatchThreadId : SV_DispatchThreadID,
                               DataBufferBlurPass1Srt* pSrt : S_SRT_DATA)
{
	uint width, height;
	pSrt->tSourceBuffer.GetDimensions(width, height);
	int nextIndex = min(dispatchThreadId.x + pSrt->m_blurRadius, width - 1);
	int prevIndex = max((int)dispatchThreadId.x - pSrt->m_blurRadius, 0);
	float sumValue = pSrt->tSourceBuffer[int2(nextIndex, dispatchThreadId.y)] - pSrt->tSourceBuffer[int2(prevIndex, dispatchThreadId.y)];

	if (prevIndex / 64 != nextIndex / 64)
	{	
		int stepIndex = (nextIndex / 64) * 64 - 1;
		sumValue += pSrt->tSourceBuffer[int2(stepIndex, dispatchThreadId.y)];
	}

	pSrt->rwDestBuffer[int2(dispatchThreadId.xy)] = sumValue / (nextIndex - prevIndex + 1);
}


// ---------------------------------------------------------------------------------------------------------------------

[numthreads(1, 64, 1)]
void CS_DataBufferBlurPassY1(uint3 dispatchThreadId : SV_DispatchThreadID,
                             DataBufferBlurPass1Srt* pSrt : S_SRT_DATA)
{
	uint2 bufSize;
	pSrt->tSourceBuffer.GetDimensions(bufSize.x, bufSize.y);

	float srcData = pSrt->tSourceBuffer[min(int2(dispatchThreadId.xy), int2(bufSize) - 1)];
	float srcData_s0 = dispatchThreadId.y % 2 == 1 ? 0 : srcData;

	float srcData_1 = srcData + LaneSwizzle(srcData, 0x1f, 0x00, 0x01);	//[01 01, 23 23, 45 45, 67 67.... 6263 6263]
	float srcData_m1 = srcData + LaneSwizzle(srcData_s0, 0x1f, 0x00, 0x01); //[0, 01, 2, 23, 4, 45, 6, 67.... 62, 6263]
	float srcData_s1 = (dispatchThreadId.y % 4) > 1 ? 0 : srcData_1;

	float srcData_2 = srcData_1 + LaneSwizzle(srcData_1, 0x1f, 0x00, 0x02); //[0123 0123, 4567 4567, ..... 60616263 60616263].
	float srcData_m2 = srcData_m1 + LaneSwizzle(srcData_s1, 0x1f, 0x00, 0x02); //[0 01 012 0123, ..... 60 6061 606162 60616263].
	float srcData_s2 = (dispatchThreadId.y % 8) > 3 ? 0 : srcData_2;

	float srcData_3	= srcData_2 + LaneSwizzle(srcData_2, 0x1f, 0x00, 0x04); //[0-7 0-7 0-7 0-7, 56-63 56-63 56-63 56-63].
	float srcData_m3 = srcData_m2 + LaneSwizzle(srcData_s2, 0x1f, 0x00, 0x04);
	float srcData_s3 = (dispatchThreadId.y % 16) > 7 ? 0 : srcData_3;

	float srcData_4 = srcData_3 + LaneSwizzle(srcData_3, 0x1f, 0x00, 0x08);
	float srcData_m4 = srcData_m3 + LaneSwizzle(srcData_s3, 0x1f, 0x00, 0x08);
	float srcData_s4 = (dispatchThreadId.y % 32) > 15 ? 0 : srcData_4;

	float srcData_5 = srcData_4 + LaneSwizzle(srcData_4, 0x1f, 0x00, 0x10);
	float srcData_m5 = srcData_m4 + LaneSwizzle(srcData_s4, 0x1f, 0x00, 0x10);
	float srcData_s5 = ReadLane(srcData_5, 0x00);

	float sumData = srcData_m5 + ((dispatchThreadId.y % 64) < 32 ? 0 : srcData_s5);

	pSrt->rwDestBuffer[int2(dispatchThreadId.xy)] = sumData;
}

[numthreads(16, 4, 1)]
void CS_DataBufferBlurPassY1_1(uint3 dispatchThreadId : SV_DispatchThreadID,
                               DataBufferBlurPass1Srt* pSrt : S_SRT_DATA)
{
	uint width, height;
	pSrt->tSourceBuffer.GetDimensions(width, height);
	int nextIndex = min(dispatchThreadId.y + pSrt->m_blurRadius, height - 1);
	int prevIndex = max((int)dispatchThreadId.y - pSrt->m_blurRadius, 0);
	float sumValue = pSrt->tSourceBuffer[int2(dispatchThreadId.x, nextIndex)] - pSrt->tSourceBuffer[int2(dispatchThreadId.x, prevIndex)];

	if (prevIndex / 64 != nextIndex/ 64)
	{	
		int stepIndex = (nextIndex / 64) * 64 - 1;
		sumValue += pSrt->tSourceBuffer[int2(dispatchThreadId.x, stepIndex)];
	}

	pSrt->rwDestBuffer[int2(dispatchThreadId.xy)] = sumValue / (nextIndex - prevIndex + 1);
}

// ---------------------------------------------------------------------------------------------------------------------

struct DataBufferBlurPassXTextures
{
	Texture2D<float2>					tSourceBuffer2; //			: register(t0);
	RWTexture2D<float2>					rwDestBuffer2; //			: register(u0);
};

struct DataBufferBlurPassXSrt
{
	DataBlurConst *pConsts;
	DataBufferBlurPassXTextures *pTexs;
};

[numthreads(1, 64, 1)]
void CS_DataBufferBlurPassX2_0(uint3 dispatchThreadId : SV_DispatchThreadID,
                               DataBufferBlurPassXSrt srt : S_SRT_DATA)
{
	int iRow = dispatchThreadId.y;
	if (iRow < srt.pConsts->m_bufferHeight)
	{
		float2 sumData = 0.0f;
		int i;
		for (i = 0; i < srt.pConsts->m_blurRadius; i++)
			sumData += srt.pTexs->tSourceBuffer2[int2(i, iRow)];

		float sumWeights = srt.pConsts->m_blurRadius;
		for (i = 0; i < srt.pConsts->m_blurRadius; i++)
		{
			srt.pTexs->rwDestBuffer2[int2(i, iRow)] = sumData / sumWeights;
			sumData += srt.pTexs->tSourceBuffer2[int2(i + srt.pConsts->m_blurRadius, iRow)];
			sumWeights += 1.0f;
		}
	}
}

[numthreads(1, 64, 1)]
void CS_DataBufferBlurPassX2_1(uint3 dispatchThreadId : SV_DispatchThreadID,
                               DataBufferBlurPassXSrt srt : S_SRT_DATA)
{
	int iCol = srt.pConsts->m_bufferWidth - srt.pConsts->m_blurRadius;
	int iRow = dispatchThreadId.y;
	if (iRow < srt.pConsts->m_bufferHeight)
	{
		float2 sumData = 0.0f;
		int i;
		for (i = iCol; i < srt.pConsts->m_bufferWidth; i++)
			sumData += srt.pTexs->tSourceBuffer2[int2(i, iRow)];

		float sumWeights = srt.pConsts->m_blurRadius;
		for (i = srt.pConsts->m_bufferWidth-1; i >= iCol; i--)
		{
			srt.pTexs->rwDestBuffer2[int2(i, iRow)] = sumData / sumWeights;
			sumData += srt.pTexs->tSourceBuffer2[int2(i - srt.pConsts->m_blurRadius, iRow)];
			sumWeights += 1.0f;
		}
	}
}

[numthreads(1, 64, 1)]
void CS_DataBufferBlurPassX2(uint3 dispatchThreadId : SV_DispatchThreadID,
                             DataBufferBlurPassXSrt srt : S_SRT_DATA)
{
	int iCol = dispatchThreadId.x * srt.pConsts->m_segmentsWidth + srt.pConsts->m_blurRadius;
	int iRow = dispatchThreadId.y;
	if (iRow < srt.pConsts->m_bufferHeight)
	{
		float2 sumData = 0.0f;

		int i;
		for (i = iCol - srt.pConsts->m_blurRadius; i < iCol + srt.pConsts->m_blurRadius; i++)
			sumData += srt.pTexs->tSourceBuffer2[int2(i, iRow)];

		float invSumWeight = srt.pConsts->m_blurWeight;

		int endCol = min(iCol + srt.pConsts->m_segmentsWidth,
		                 srt.pConsts->m_bufferWidth - srt.pConsts->m_blurRadius);

		for (i = iCol; i < endCol; i++)
		{
			srt.pTexs->rwDestBuffer2[int2(i, iRow)] = sumData * invSumWeight;
			sumData += srt.pTexs->tSourceBuffer2[int2(i + srt.pConsts->m_blurRadius, iRow)] -
			           srt.pTexs->tSourceBuffer2[int2(i - srt.pConsts->m_blurRadius, iRow)];
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------

struct DataBufferBlurPassYTextures
{
	Texture2D<float2>					tSourceBuffer2; //			: register(t0);
	RWTexture2D<float>					rwDestBuffer1; //			: register(u0);
	RWTexture2D<float>					rwDestBuffer3; //			: register(u1);
};

struct DataBufferBlurPassYSrt
{
	DataBlurConst *pConsts;
	DataBufferBlurPassYTextures *pTexs;	
};

[numthreads(64, 1, 1)]
void CS_DataBufferBlurPassY2_0(uint3 dispatchThreadId : SV_DispatchThreadID,
                               DataBufferBlurPassYSrt srt : S_SRT_DATA)
{
	int iCol = dispatchThreadId.x;
	if (iCol < srt.pConsts->m_bufferWidth)
	{
		float2 sumData = 0.0f;
		int i;
		for (i = 0; i < srt.pConsts->m_blurRadius; i++)
			sumData += srt.pTexs->tSourceBuffer2[int2(iCol, i)];

		float sumWeights = srt.pConsts->m_blurRadius;
		for (i = 0; i < srt.pConsts->m_blurRadius; i++)
		{
			float2 result = sumData / sumWeights;
			srt.pTexs->rwDestBuffer1[int2(iCol, i)] = result.x;
			srt.pTexs->rwDestBuffer3[int2(iCol, i)] = result.y;
			sumData += srt.pTexs->tSourceBuffer2[int2(iCol, i + srt.pConsts->m_blurRadius)];
			sumWeights += 1.0f;
		}
	}
}

[numthreads(64, 1, 1)]
void CS_DataBufferBlurPassY2_1(uint3 dispatchThreadId : SV_DispatchThreadID,
                               DataBufferBlurPassYSrt srt : S_SRT_DATA)
{
	int iCol = dispatchThreadId.x;
	int iRow = srt.pConsts->m_bufferHeight - srt.pConsts->m_blurRadius;
	if (iCol < srt.pConsts->m_bufferWidth)
	{
		float2 sumData = 0.0f;
		int i;
		for (i = iRow; i < srt.pConsts->m_bufferHeight; i++)
			sumData += srt.pTexs->tSourceBuffer2[int2(iCol, i)];

		float sumWeights = srt.pConsts->m_blurRadius;
		for (i = srt.pConsts->m_bufferHeight-1; i >= iRow; i--)
		{
			float2 result = sumData / sumWeights;
			srt.pTexs->rwDestBuffer1[int2(iCol, i)] = result.x;
			srt.pTexs->rwDestBuffer3[int2(iCol, i)] = result.y;
			sumData += srt.pTexs->tSourceBuffer2[int2(iCol, i - srt.pConsts->m_blurRadius)];
			sumWeights += 1.0f;
		}
	}
}

[numthreads(64, 1, 1)]
void CS_DataBufferBlurPassY2(uint3 dispatchThreadId : SV_DispatchThreadID,
                             DataBufferBlurPassYSrt srt : S_SRT_DATA)
{
	int iCol = dispatchThreadId.x;
	int iRow = dispatchThreadId.y * srt.pConsts->m_segmentsHeight + srt.pConsts->m_blurRadius;
	if (iCol < srt.pConsts->m_bufferWidth)
	{
		float2 sumData = 0.0f;

		int i;
		for (i = iRow - srt.pConsts->m_blurRadius; i < iRow + srt.pConsts->m_blurRadius; i++)
			sumData += srt.pTexs->tSourceBuffer2[int2(iCol, i)];

		float invSumWeight = srt.pConsts->m_blurWeight;

		int endRow = min(iRow + srt.pConsts->m_segmentsHeight,
		                 srt.pConsts->m_bufferHeight - srt.pConsts->m_blurRadius);

		for (i = iRow; i < endRow; i++)
		{
			float2 result = sumData * invSumWeight;
			srt.pTexs->rwDestBuffer1[int2(iCol, i)] = result.x;
			srt.pTexs->rwDestBuffer3[int2(iCol, i)] = result.y;
			sumData += srt.pTexs->tSourceBuffer2[int2(iCol, i + srt.pConsts->m_blurRadius)] -
			           srt.pTexs->tSourceBuffer2[int2(iCol, i - srt.pConsts->m_blurRadius)];
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------

struct DataBlurPrePassTextures
{
	RWTexture2D<float>		rwb_blur;
	Texture2D<float>		src_data;
};

struct DataBlurPrePassSrt
{
	DataBlurConst				*consts;
	DataBlurPrePassTextures		*texs;
};

groupshared float g_sumBlurRadius[128];

[numthreads(64, 1, 1)]
void CS_DataBufferBlurPrePassX_0(uint3 dispatchThreadId : SV_DispatchThreadID, DataBlurPrePassSrt srt : S_SRT_DATA)
{
	uint idx = dispatchThreadId.x;
	uint u = dispatchThreadId.x;
	g_sumBlurRadius[idx] =
	    (idx < srt.consts->m_blurRadius) ? min(srt.texs->src_data[int2(u, dispatchThreadId.y)], 0.0f) : 0.0f;
	g_sumBlurRadius[idx + 64] =
	    (idx + 64 < srt.consts->m_blurRadius) ? min(srt.texs->src_data[int2(u + 64, dispatchThreadId.y)], 0.0f) : 0.0f;

	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 64];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 32];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 16];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 8];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 4];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 2];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 1];
	GroupMemoryBarrierWithGroupSync();

	if (idx == 0)
		srt.texs->rwb_blur[int2(0, dispatchThreadId.y)] = g_sumBlurRadius[0];
}

[numthreads(64, 1, 1)]
void CS_DataBufferBlurPrePassX_1(uint3 dispatchThreadId : SV_DispatchThreadID, DataBlurPrePassSrt srt : S_SRT_DATA)
{
	uint idx = dispatchThreadId.x;
	uint u = srt.consts->m_bufferWidth - 1 - dispatchThreadId.x;
	g_sumBlurRadius[idx] =
	    (idx < srt.consts->m_blurRadius) ? min(srt.texs->src_data[int2(u, dispatchThreadId.y)], 0.0f) : 0.0f;
	g_sumBlurRadius[idx + 64] = ((idx + 64) < srt.consts->m_blurRadius)
	                                ? min(srt.texs->src_data[int2(u - 64, dispatchThreadId.y)], 0.0f)
	                                : 0.0f;
	GroupMemoryBarrierWithGroupSync();

	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 64];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 32];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 16];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 8];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 4];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 2];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 1];
	GroupMemoryBarrierWithGroupSync();

	if (idx == 0)
		srt.texs->rwb_blur[int2(33, dispatchThreadId.y)] = g_sumBlurRadius[0];
}

[numthreads(64, 1, 1)]
void CS_DataBufferBlurPrePassX_C(uint3 groupId : SV_GroupID, 
                                 uint3 groupThreadId : SV_GroupThreadId, 
                                 DataBlurPrePassSrt srt : S_SRT_DATA)
{
	uint idx = groupThreadId.x;
	uint u = groupId.x * srt.consts->m_segmentsWidth + idx;
	g_sumBlurRadius[idx] =
	    (idx < srt.consts->m_blurRadius * 2) ? min(srt.texs->src_data[int2(u, groupId.y)], 0.0f) : 0.0f;
	g_sumBlurRadius[idx + 64] =
	    ((idx + 64) < srt.consts->m_blurRadius * 2) ? min(srt.texs->src_data[int2(u + 64, groupId.y)], 0.0f) : 0.0f;

	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 64];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 32];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 16];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 8];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 4];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 2];
	GroupMemoryBarrierWithGroupSync();
	g_sumBlurRadius[idx] += g_sumBlurRadius[idx + 1];
	GroupMemoryBarrierWithGroupSync();

	if (idx == 0)
		srt.texs->rwb_blur[int2(groupId.x + 1, groupId.y)] = g_sumBlurRadius[0];
}

// ---------------------------------------------------------------------------------------------------------------------

struct DataBlurPassTextures
{
	RWTexture2D<float>		rwb_blur;
	Texture2D<float>		src_data;
	Texture2D<float>		src_data1;
};

struct DataBlurPassSrt
{
	DataBlurConst				*consts;
	DataBlurPassTextures		*texs;
};

[numthreads(1, 64, 1)]
void CS_DataBufferBlurPassX_0(uint3 dispatchThreadId : SV_DispatchThreadID, DataBlurPassSrt srt : S_SRT_DATA)
{
	int iRow = dispatchThreadId.y;
	if (iRow < srt.consts->m_bufferHeight)
	{
		float sumData = srt.texs->src_data1[int2(0, iRow)];

		float sumWeights = srt.consts->m_blurRadius;
		for (int i = 0; i < srt.consts->m_blurRadius; i++)
		{
			srt.texs->rwb_blur[int2(i, iRow)] = -sumData / sumWeights;
			sumData += min(srt.texs->src_data[int2(i + srt.consts->m_blurRadius, iRow)], 0.0f);
			sumWeights += 1.0f;
		}
	}
}

[numthreads(1, 64, 1)]
void CS_DataBufferBlurPassX_1(uint3 dispatchThreadId : SV_DispatchThreadID, DataBlurPassSrt srt : S_SRT_DATA)
{
	int iCol = srt.consts->m_bufferWidth - srt.consts->m_blurRadius;
	int iRow = dispatchThreadId.y;
	if (iRow < srt.consts->m_bufferHeight)
	{
		float sumData = srt.texs->src_data1[int2(33, iRow)];

		float sumWeights = srt.consts->m_blurRadius;
		for (int i = srt.consts->m_bufferWidth-1; i >= iCol; i--)
		{
			srt.texs->rwb_blur[int2(i, iRow)] = -sumData / sumWeights;
			sumData += min(srt.texs->src_data[int2(i - srt.consts->m_blurRadius, iRow)], 0.0f);
			sumWeights += 1.0f;
		}
	}
}

[numthreads(1, 64, 1)]
void CS_DataBufferBlurPassX_C(uint3 dispatchThreadId : SV_DispatchThreadID, 
                              uint3 groupId : SV_GroupID, 
                              DataBlurPassSrt srt : S_SRT_DATA)
{
	int iCol = dispatchThreadId.x * srt.consts->m_segmentsWidth + srt.consts->m_blurRadius;
	int iRow = dispatchThreadId.y;
	if (iRow < srt.consts->m_bufferHeight)
	{
		float sumData = srt.texs->src_data1[int2(groupId.x + 1, iRow)];

		float invSumWeight = srt.consts->m_blurWeight;
		int endCol = min(iCol + srt.consts->m_segmentsWidth, srt.consts->m_bufferWidth - srt.consts->m_blurRadius);
		for (int i = iCol; i < endCol; i++)
		{
			srt.texs->rwb_blur[int2(i, iRow)] = -sumData * invSumWeight;
			sumData += min(srt.texs->src_data[int2(i + srt.consts->m_blurRadius, iRow)], 0.0f) -
			           min(srt.texs->src_data[int2(i - srt.consts->m_blurRadius, iRow)], 0.0f);
		}
	}
}


[numthreads(64, 1, 1)]
void CS_DataBufferBlurPassHalfY_0(uint3 dispatchThreadId : SV_DispatchThreadID, DataBlurPrePassSrt srt : S_SRT_DATA)
{
	int iCol = dispatchThreadId.x;
	if (iCol < srt.consts->m_bufferWidth)
	{
		float sumData = 0.0f;
		int i;
		for (i = 0; i < srt.consts->m_blurRadius; i++)
			sumData += srt.texs->src_data[int2(iCol, i)];

		float sumWeights = srt.consts->m_blurRadius;
		for (i = 0; i < srt.consts->m_blurRadius; i++)
		{
			srt.texs->rwb_blur[int2(iCol, i)] = sumData / sumWeights;
			sumData += srt.texs->src_data[int2(iCol, i + srt.consts->m_blurRadius)];
			sumWeights += 1.0f;
		}
	}
}

[numthreads(64, 1, 1)]
void CS_DataBufferBlurPassHalfY_1(uint3 dispatchThreadId : SV_DispatchThreadID, DataBlurPassSrt srt : S_SRT_DATA)
{
	int iCol = dispatchThreadId.x;
	int iRow = srt.consts->m_bufferHeight - srt.consts->m_blurRadius;
	if (iCol < srt.consts->m_bufferWidth)
	{
		float sumData = 0.0f;
		int i;
		for (i = iRow; i < srt.consts->m_bufferHeight; i++)
			sumData += srt.texs->src_data[int2(iCol, i)];

		float sumWeights = srt.consts->m_blurRadius;
		for (i = srt.consts->m_bufferHeight-1; i >= iRow; i--)
		{
			srt.texs->rwb_blur[int2(iCol, i)] = sumData / sumWeights;
			sumData += srt.texs->src_data[int2(iCol, i - srt.consts->m_blurRadius)];
			sumWeights += 1.0f;
		}
	}
}

[numthreads(64, 1, 1)]
void CS_DataBufferBlurPassHalfY_C(uint3 dispatchThreadId : SV_DispatchThreadID, DataBlurPassSrt srt : S_SRT_DATA)
{
	int iCol = dispatchThreadId.x;
	int iRow = dispatchThreadId.y * srt.consts->m_segmentsHeight + srt.consts->m_blurRadius;
	if (iCol < srt.consts->m_bufferWidth)
	{
		float sumData = 0.0f;

		int i;
		for (i = iRow - srt.consts->m_blurRadius; i < iRow + srt.consts->m_blurRadius; i++)
			sumData += srt.texs->src_data[int2(iCol, i)];

		float invSumWeight = srt.consts->m_blurWeight;

		int endRow = min(iRow + srt.consts->m_segmentsHeight, srt.consts->m_bufferHeight - srt.consts->m_blurRadius);
		for (i = iRow; i < endRow; i++)
		{
			srt.texs->rwb_blur[int2(iCol, i)] = sumData * invSumWeight;
			sumData += srt.texs->src_data[int2(iCol, i + srt.consts->m_blurRadius)] -
			           srt.texs->src_data[int2(iCol, i - srt.consts->m_blurRadius)];
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------

struct DataBufferBlurPassTextures
{
	Texture2D<float4>   tSourceDataBuffer; // : register(t0);
	RWTexture2D<float4> rwDestDataBuffer; // : register(u0);
};

struct DataBufferBlurPassSrt
{
	DataBlurConst *pConsts;
	DataBufferBlurPassTextures *pTexs;
};

[numthreads(128, 1, 1)]
void CS_DataBufferBlurPassX(uint3 dispatchThreadId : SV_DispatchThreadID, 
                            DataBufferBlurPassSrt srt : S_SRT_DATA)
{
	int iRow = dispatchThreadId.x;
	if (iRow < srt.pConsts->m_bufferHeight)
	{
		float4 sumData = 0.0f;
		for (int i = 0; i < srt.pConsts->m_blurRadius; i++)
			sumData += srt.pTexs->tSourceDataBuffer[int2(i, iRow)];

		float sumWeights =  srt.pConsts->m_blurRadius;

		int blurFadeInIndex = srt.pConsts->m_blurRadius;
		int blurFadeEndIdx = srt.pConsts->m_bufferWidth - srt.pConsts->m_blurRadius;

		int iNextCol = srt.pConsts->m_blurRadius;
		for (int iCol = 0; iCol < blurFadeInIndex; iCol++)
		{
			sumData += srt.pTexs->tSourceDataBuffer[int2(iNextCol, iRow)];
			sumWeights += 1.0f;
			iNextCol ++;

			srt.pTexs->rwDestDataBuffer[int2(iCol, iRow)] = sumData / sumWeights;
		}

		int iPrevCol = 0;
		float invSumWeight = 1.0f / sumWeights;
		for (int iCol1 = blurFadeInIndex; iCol1 < blurFadeEndIdx; iCol1++)
		{
			sumData += srt.pTexs->tSourceDataBuffer[int2(iNextCol, iRow)] - 
				       srt.pTexs->tSourceDataBuffer[int2(iPrevCol, iRow)];
			iNextCol ++;
			iPrevCol ++;

			srt.pTexs->rwDestDataBuffer[int2(iCol1, iRow)] = sumData * invSumWeight;
		}

		for (int iCol2 = blurFadeEndIdx; iCol2 < srt.pConsts->m_bufferWidth; iCol2++)
		{
			sumData -= srt.pTexs->tSourceDataBuffer[int2(iPrevCol, iRow)];
			sumWeights -= 1.0f;
			iPrevCol ++;

			srt.pTexs->rwDestDataBuffer[int2(iCol2, iRow)] = sumData / sumWeights;
		}
	}
}

[numthreads(128, 1, 1)]
void CS_DataBufferBlurPassY(uint3 dispatchThreadId : SV_DispatchThreadID, 
                            DataBufferBlurPassSrt srt : S_SRT_DATA)
{
	int iCol = dispatchThreadId.x;
	if (iCol < srt.pConsts->m_bufferWidth)
	{
		float4 sumData = 0.0f;
		for (int i = 0; i < srt.pConsts->m_blurRadius; i++)
			sumData += srt.pTexs->tSourceDataBuffer[int2(iCol, i)];

		float sumWeights =  srt.pConsts->m_blurRadius;

		int blurFadeInIndex = srt.pConsts->m_blurRadius;
		int blurFadeEndIdx = srt.pConsts->m_bufferHeight - srt.pConsts->m_blurRadius;

		int iNextRow = srt.pConsts->m_blurRadius;
		for (int iRow = 0; iRow < blurFadeInIndex; iRow++)
		{
			sumData += srt.pTexs->tSourceDataBuffer[int2(iCol, iNextRow)];
			sumWeights += 1.0f;
			iNextRow ++;

			srt.pTexs->rwDestDataBuffer[int2(iCol, iRow)] = sumData / sumWeights;
		}

		int iPrevRow = 0;
		float invSumWeight = 1.0f / sumWeights;
		for (int iRow1 = blurFadeInIndex; iRow1 < blurFadeEndIdx; iRow1++)
		{
			sumData += srt.pTexs->tSourceDataBuffer[int2(iCol, iNextRow)] - 
			           srt.pTexs->tSourceDataBuffer[int2(iCol, iPrevRow)];

			iNextRow ++;
			iPrevRow ++;

			srt.pTexs->rwDestDataBuffer[int2(iCol, iRow1)] = sumData * invSumWeight;
		}

		for (int iRow2 = blurFadeEndIdx; iRow2 < srt.pConsts->m_bufferHeight; iRow2++)
		{
			sumData -= srt.pTexs->tSourceDataBuffer[int2(iCol, iPrevRow)];
			sumWeights -= 1.0f;
			iPrevRow ++;

			srt.pTexs->rwDestDataBuffer[int2(iCol, iRow2)] = sumData / sumWeights;
		}
	}
}
