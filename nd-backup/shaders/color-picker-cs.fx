#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "post-processing-common.fxi"
#include "packing.fxi"

// =====================================================================================================================

struct FetchPixelUVSrtData
{
	Texture2D<float4>   m_tSourceColorBuffer;
	RWByteAddressBuffer m_uPixelColorOnScreen;
	SamplerState        m_sSamplerLinear;
	float4              m_pixelScreenCoord;
};

[numthreads(1, 1, 1)]
void CS_FetchPixelColorByGivenCoordUV(FetchPixelUVSrtData *srt : S_SRT_DATA)
{
	float4 optColor = srt->m_tSourceColorBuffer.SampleLevel(srt->m_sSamplerLinear, srt->m_pixelScreenCoord.xy, 0);
	StoreAsFloat4(srt->m_uPixelColorOnScreen, optColor, 0);
}

// =====================================================================================================================

struct FetchPixelXYSrtData
{
	uint2               m_pixelScreenCoord;
	Texture2D<float4>   m_tSourceColorBuffer;
	RWByteAddressBuffer m_uPixelColorOnScreen;
};

[numthreads(1, 1, 1)]
void CS_FetchPixelColorByGivenCoordXY(FetchPixelXYSrtData *srt : S_SRT_DATA)
{
	//float4 optColor = srt->tSourceColorBuffer.Load(int3(srt->pixelScreenCoord.x, srt->pixelScreenCoord.y, 1));
	float4 optColor = srt->m_tSourceColorBuffer[int3(srt->m_pixelScreenCoord.x, srt->m_pixelScreenCoord.y, 1)];
	StoreAsFloat4(srt->m_uPixelColorOnScreen, optColor, 0);
}

// =====================================================================================================================

struct ColorPickerPixelCrossSrtData
{
	uint2               m_pixelScreenCoord;
	float3				m_crossColor;
	RWTexture2D<float4> m_tSourceColorBuffer;
};

[numthreads(8, 8, 1)]
void CS_DrawColorPickerPixelSampleCross(uint3 dispatchThreadId : SV_DispatchThreadId, ColorPickerPixelCrossSrtData *srt : S_SRT_DATA)
{
	uint2 thisPixel = dispatchThreadId.xy;

	uint sampX = srt->m_pixelScreenCoord.x;
	uint sampY = srt->m_pixelScreenCoord.y;

	int dX = abs((int)sampX - (int)thisPixel.x);
	int dY = abs((int)sampY - (int)thisPixel.y);

	float3 crossColor = srt->m_crossColor.xyz;
	int size = 35;

	if (dY < 2)
	{
		if (dX > 1 && dX <= size)
		{
			float t = (float)dX / (float)(size - 1);
			t = 1.0f - (t * t);
			float3 srcColor = srt->m_tSourceColorBuffer[thisPixel];
			float3 finalColor = lerp(srcColor, crossColor, t);
			srt->m_tSourceColorBuffer[dispatchThreadId.xy] = float4(finalColor, 1.0f);
		}
	}
	else if (dX < 2)
	{
		if (dY > 1 && dY <= size)
		{
			float t = (float)dY / (float)(size - 1);
			t = 1.0f - (t * t);
			float3 srcColor = srt->m_tSourceColorBuffer[thisPixel];
			float3 finalColor = lerp(srcColor, crossColor, t);
			srt->m_tSourceColorBuffer[dispatchThreadId.xy] = float4(finalColor, 1.0f);
		}
	}
}

// =====================================================================================================================

struct ColorPickerRegionBoxSrtData
{
	uint4               m_pixelScreenCoord;
	float3				m_boxColor;
	RWTexture2D<float4> m_tSourceColorBuffer;
};

[numthreads(8, 8, 1)]
void CS_DrawColorPickerRegionSampleBox(uint3 dispatchThreadId : SV_DispatchThreadId, ColorPickerRegionBoxSrtData *srt : S_SRT_DATA)
{
	int boxWidth = 2;

	int pX = (int)dispatchThreadId.x;
	int pY = (int)dispatchThreadId.y;

	int regionStartX = (int)srt->m_pixelScreenCoord.x;
	int regionStartY = (int)srt->m_pixelScreenCoord.y;
	int regionEndX = (int)srt->m_pixelScreenCoord.z;
	int regionEndY = (int)srt->m_pixelScreenCoord.w;

	int borderRegionStartX = ((int)srt->m_pixelScreenCoord.x - boxWidth) < 0 ? 0 : srt->m_pixelScreenCoord.x - boxWidth;
	int borderRegionStartY = ((int)srt->m_pixelScreenCoord.y - boxWidth) < 0 ? 0 : srt->m_pixelScreenCoord.y - boxWidth;
	int borderRegionEndX = srt->m_pixelScreenCoord.z + boxWidth;
	int borderRegionEndY = srt->m_pixelScreenCoord.w + boxWidth;

	float3 boxColor = srt->m_boxColor;
	float3 srcColor = srt->m_tSourceColorBuffer[dispatchThreadId.xy];

	bool isPixelInRegion = (pX >= regionStartX && pX <= regionEndX && pY >= regionStartY && pY <= regionEndY);
	bool isPixelInBorderRegion = (pX >= borderRegionStartX && pX <= borderRegionEndX && pY >= borderRegionStartY && pY <= borderRegionEndY);

	if (!isPixelInRegion)
	{
		if (isPixelInBorderRegion)
		{
			srt->m_tSourceColorBuffer[dispatchThreadId.xy] = float4(boxColor, 1.0f);
		}
		else
		{
			srt->m_tSourceColorBuffer[dispatchThreadId.xy] = float4(srcColor * 0.5f, 1.0f);
		}
	}
}

// =====================================================================================================================

struct AccumulateColorPickerRegionSrt
{
	uint4 m_pixelScreenCoord;
	RW_RegularBuffer<uint4> m_outAccumQuantizedColor;
	Texture2D<float4> m_tSrcColor;
};

uint ReduceSum(uint value)
{
	uint sum = value;

	sum += LaneSwizzle(sum, 0x1f, 0x00, 0x10);
	sum += LaneSwizzle(sum, 0x1f, 0x00, 0x08);
	sum += LaneSwizzle(sum, 0x1f, 0x00, 0x04);
	sum += LaneSwizzle(sum, 0x1f, 0x00, 0x02);
	sum += LaneSwizzle(sum, 0x1f, 0x00, 0x01);

	uint reduced = ReadLane(sum, 0) + ReadLane(sum, 32);
	return reduced;
}

[numthreads(8, 8, 1)]
void CS_AcculmulateColorPickerRegion(uint2 dispatchThreadID: SV_DispatchThreadID, AccumulateColorPickerRegionSrt* pSrt : S_SRT_DATA) : SV_Target
{
	uint pX = dispatchThreadID.x;
	uint pY = dispatchThreadID.y;

	uint regionStartX = pSrt->m_pixelScreenCoord.x;
	uint regionStartY = pSrt->m_pixelScreenCoord.y;
	uint regionEndX = pSrt->m_pixelScreenCoord.z;
	uint regionEndY = pSrt->m_pixelScreenCoord.w;

	bool isPixelInRegion = (pX >= regionStartX && pX <= regionEndX && pY >= regionStartY && pY <= regionEndY);

	// quantize color
	uint r = 0;
	uint g = 0;
	uint b = 0;
	uint a = 0;

	// only accumulate if within the cone of the flashlight
	if (isPixelInRegion)
	{
		// unpack base color
		float3 srcColor = pSrt->m_tSrcColor[uint2(pX, pY)].xyz;

		uint3 quantizedColor = (uint3)(srcColor * 512.0f);

		r = quantizedColor.x;
		g = quantizedColor.y;
		b = quantizedColor.z;
		a = 512;
	}

	// sum up within the wavefront using lane swizzling
	r = ReduceSum(r);
	g = ReduceSum(g);
	b = ReduceSum(b);
	a = ReduceSum(a);

	// only the first lane will atomically add the waverfront's accumulated color to the buffer
	if (__v_cndmask_b32(0, 1, 1))
	{
		__buffer_atomic_add(r, uint2(0, 0), __get_vsharp(pSrt->m_outAccumQuantizedColor), 0, 0); // r
		__buffer_atomic_add(g, uint2(0, 0), __get_vsharp(pSrt->m_outAccumQuantizedColor), 4, 0); // g
		__buffer_atomic_add(b, uint2(0, 0), __get_vsharp(pSrt->m_outAccumQuantizedColor), 8, 0); // b
		__buffer_atomic_add(a, uint2(0, 0), __get_vsharp(pSrt->m_outAccumQuantizedColor), 12, 0); // a
	}
}

// =====================================================================================================================

struct NormalizeColorPickerRegionAccumulationSrt
{
	RW_RegularBuffer<uint4> m_inAccumQuantizedColor;
	RW_RegularBuffer<float3> m_outColor;
};

[numthreads(1, 1, 1)]
void CS_NormalizeColorPickerRegionAccumulation(uint2 dispatchThreadID: SV_DispatchThreadID, NormalizeColorPickerRegionAccumulationSrt* pSrt : S_SRT_DATA) : SV_Target
{
	uint2 xy = dispatchThreadID;

	uint r = pSrt->m_inAccumQuantizedColor[0].x;
	uint g = pSrt->m_inAccumQuantizedColor[0].y;
	uint b = pSrt->m_inAccumQuantizedColor[0].z;
	uint norm = pSrt->m_inAccumQuantizedColor[0].w;

	if (norm > 0)
	{
		float normInv = 1.0f / (float)norm;
		float rOut = (float)r * normInv;
		float gOut = (float)g * normInv;
		float bOut = (float)b * normInv;
		pSrt->m_outColor[xy] = float3(rOut, gOut, bOut);
	}
	else
	{
		pSrt->m_outColor[xy] = float3(0.0f, 0.0f, 0.0f);
	}
}

// =====================================================================================================================

struct ApplyColorPickerZoomSrt
{
	float4x4 m_screenToZoomTransform;
	Texture2D<float4> m_tSrcColor;
	RW_Texture2D<float4> m_tDstColor;
};

[numthreads(8, 8, 1)]
void CS_ApplyColorPickerZoom(uint2 dispatchThreadID: SV_DispatchThreadID, ApplyColorPickerZoomSrt* pSrt : S_SRT_DATA) : SV_Target
{
	float4 pixelXY = float4((float2)dispatchThreadID, 0.0f, 1.0f);
	float4 pixelZoomXY = mul(pixelXY, pSrt->m_screenToZoomTransform);
	float3 srcColor = pSrt->m_tSrcColor[(uint2)pixelZoomXY.xy].xyz;
	pSrt->m_tDstColor[dispatchThreadID] = float4(srcColor, 1.0f);
}