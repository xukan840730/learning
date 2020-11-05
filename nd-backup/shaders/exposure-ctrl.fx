#include "global-srt.fxi"

#define kBufferBlockSizeX			256
#define kBufferBlockSizeY			256
#define kTonemapFilterSize			128
#define kForceExposureValue			0x80000000
#define kExposureMethodManual		0x0
#define kExposureMethodExponential	0x1
#define kExposureMethodMask			0x1

struct ExposureCtrlConsts
{
    float						m_toneMapSetPoint;
	float						m_exposeFactor;
	float						m_displayBrightness;
	float						m_lightMin;
	float						m_lightMax;
	float						m_forcedExposureValue;
	float4						m_screenSizeParams;
	uint						m_postProcessingFlag;
	uint						m_filterBufferSize;
	uint						m_screenSizeYDiv8;
	uint						m_filterBufferCount;
};

struct ExposureCtrl
{
	float m_exposureScaleFactor;
	float m_exposureValue;
	float m_currentBrightness;
	float m_sceneLuminance;
};

groupshared float g_sumBrightness[kBufferBlockSizeX];


// =====================================================================================================================

struct SumXTexs
{
	Texture2D<float4> tex_color;
};

struct SumXBufs
{
	RWByteAddressBuffer rwb_brightness;
};

struct SumXSmpls
{
	SamplerState sSamplerLinear;
};

struct SumXSrt
{
	ExposureCtrlConsts *consts;
	SumXBufs *bufs;
	SumXTexs *texs;
	SumXSmpls *smpls;
};

[numthreads(kBufferBlockSizeX, 1, 1)]
void CS_SumBrightnessOnX(uint3 dispatchThreadId : SV_DispatchThreadID, 
                         uint3 groupId          : SV_GroupID, 
                         uint groupThreadId     : SV_GroupThreadId,
                         SumXSrt srt            : S_SRT_DATA)
{
	// create 256 sum color.
	float v = (groupId.y + 0.5f) * srt.consts->m_screenSizeParams.y;
	float u0 = (dispatchThreadId.x + kBufferBlockSizeX * 0 + 0.5f) * srt.consts->m_screenSizeParams.x;
	float u1 = (dispatchThreadId.x + kBufferBlockSizeX * 1 + 0.5f) * srt.consts->m_screenSizeParams.x;
	float u2 = (dispatchThreadId.x + kBufferBlockSizeX * 2 + 0.5f) * srt.consts->m_screenSizeParams.x;
	float u3 = (dispatchThreadId.x + kBufferBlockSizeX * 3 + 0.5f) * srt.consts->m_screenSizeParams.x;

	float3 color0 = srt.texs->tex_color.SampleLevel(srt.smpls->sSamplerLinear, float2(u0, v), 0).xyz;
	float3 color1 = srt.texs->tex_color.SampleLevel(srt.smpls->sSamplerLinear, float2(u1, v), 0).xyz;
	float3 color2 = srt.texs->tex_color.SampleLevel(srt.smpls->sSamplerLinear, float2(u2, v), 0).xyz;
	float3 color3 = srt.texs->tex_color.SampleLevel(srt.smpls->sSamplerLinear, float2(u3, v), 0).xyz;

	float4 pixelBrightness = float4(dot(color0, s_luminanceVector),
									dot(color1, s_luminanceVector),
									dot(color2, s_luminanceVector),
									dot(color3, s_luminanceVector));

	float4 logBrightness = max(pixelBrightness, 0.00001);

	float sumBrightness = logBrightness.x;
	sumBrightness += logBrightness.y;
	
	if (u2 < 1.0f)
		sumBrightness += logBrightness.z;
	
	if (u3 < 1.0f)
		sumBrightness += logBrightness.w;

	g_sumBrightness[groupThreadId.x] = sumBrightness;
	GroupMemoryBarrierWithGroupSync();

	if (groupThreadId.x < 128)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 128];
	GroupMemoryBarrierWithGroupSync();

	if (groupThreadId.x < 64)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 64];
	GroupMemoryBarrierWithGroupSync();

	if (groupThreadId.x < 32)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 32];

	if (groupThreadId.x < 16)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 16];

	if (groupThreadId.x < 8)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 8];

	if (groupThreadId.x < 4)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 4];

	if (groupThreadId.x < 2)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 2];

	if (groupThreadId.x < 1)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 1];

	if (groupThreadId.x == 0)
		srt.bufs->rwb_brightness.Store(groupId.y * 4, asuint(g_sumBrightness[0]));
}

// =====================================================================================================================

struct SumYBufs
{
	ByteAddressBuffer	buf_src;
	RWStructuredBuffer<ExposureCtrl> rwb_res;
};

struct SumYSrt
{
	ExposureCtrlConsts *consts;	
	SumYBufs *bufs;
};

[numthreads(kBufferBlockSizeY, 1, 1)]
void CS_SumBrightnessOnY(uint3 dispatchThreadId : SV_DispatchThreadID, 
                         uint groupThreadId     : SV_GroupThreadId,
                         SumYSrt srt            : S_SRT_DATA)
{
	if (groupThreadId.x < srt.consts->m_screenSizeYDiv8)
		g_sumBrightness[groupThreadId.x] = dot(LoadAsFloat4(srt.bufs->buf_src, dispatchThreadId.x * 16), 1.0f);
	else
		g_sumBrightness[groupThreadId.x] = 0.0f;

	GroupMemoryBarrierWithGroupSync();

	if (groupThreadId.x < 128)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 128];
	GroupMemoryBarrierWithGroupSync();

	if (groupThreadId.x < 64)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 64];
	GroupMemoryBarrierWithGroupSync();

	if (groupThreadId.x < 32)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 32];

	if (groupThreadId.x < 16)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 16];

	if (groupThreadId.x < 8)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 8];

	if (groupThreadId.x < 4)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 4];

	if (groupThreadId.x < 2)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 2];

	if (groupThreadId.x < 1)
		g_sumBrightness[groupThreadId.x] += g_sumBrightness[groupThreadId.x + 1];

	if (groupThreadId.x == 0)
	{
		srt.bufs->rwb_res[0].m_currentBrightness = g_sumBrightness[0] * srt.consts->m_screenSizeParams.z;
	}
}

// =====================================================================================================================

groupshared float g_currentBrightness;
groupshared uint  g_usedFilterBufferSize;
groupshared float g_tonemapFilterBuffer[kTonemapFilterSize];

struct ExposureCtrlBufs
{
	RWStructuredBuffer<ExposureCtrl> rwb_ctrlbuf;
	RWStructuredBuffer<float>        rwb_filterbuf;
};

struct ExposureCtrlSrt
{
	ExposureCtrlConsts *consts;
	ExposureCtrlBufs *bufs;
};

[numthreads(kTonemapFilterSize, 1, 1)]
void CS_ExposureControl(uint groupThreadId : SV_GroupThreadId,
                        ExposureCtrlSrt srt : S_SRT_DATA)
{
	uint count = 0;
	if (groupThreadId == 0 && srt.consts->m_filterBufferSize > 0)
	{
		g_currentBrightness = srt.bufs->rwb_ctrlbuf[0].m_currentBrightness;
		count = srt.consts->m_filterBufferCount;
		srt.bufs->rwb_filterbuf[count % srt.consts->m_filterBufferSize] = g_currentBrightness;
		g_usedFilterBufferSize = min(count + 1, srt.consts->m_filterBufferSize);
	}

	GroupMemoryBarrierWithGroupSync();

	g_tonemapFilterBuffer[groupThreadId] =
	    (groupThreadId < g_usedFilterBufferSize) ? srt.bufs->rwb_filterbuf[groupThreadId] : 0;

	GroupMemoryBarrierWithGroupSync();

	if (groupThreadId < 64)
		g_tonemapFilterBuffer[groupThreadId] += g_tonemapFilterBuffer[groupThreadId + 64];
	GroupMemoryBarrierWithGroupSync();

	if (groupThreadId < 32)
		g_tonemapFilterBuffer[groupThreadId] += g_tonemapFilterBuffer[groupThreadId + 32];

	if (groupThreadId < 16)
		g_tonemapFilterBuffer[groupThreadId] += g_tonemapFilterBuffer[groupThreadId + 16];

	if (groupThreadId < 8)
		g_tonemapFilterBuffer[groupThreadId] += g_tonemapFilterBuffer[groupThreadId + 8];

	if (groupThreadId < 4)
		g_tonemapFilterBuffer[groupThreadId] += g_tonemapFilterBuffer[groupThreadId + 4];

	if (groupThreadId < 2)
		g_tonemapFilterBuffer[groupThreadId] += g_tonemapFilterBuffer[groupThreadId + 2];

	if (groupThreadId < 1)
		g_tonemapFilterBuffer[groupThreadId] += g_tonemapFilterBuffer[groupThreadId + 1];

	
	if (groupThreadId == 0)
	{
		float filteredBrightness =
		    (g_usedFilterBufferSize == 0) ? g_currentBrightness : g_tonemapFilterBuffer[0] / g_usedFilterBufferSize;

		float exposureValue = 0;

		float luminanceValue = log2(filteredBrightness) + 6.0f;

		if (srt.consts->m_postProcessingFlag & kForceExposureValue)
		{
			exposureValue = srt.consts->m_forcedExposureValue;
		}
		else
		{
			// Final luminance L' = L + E. 
			uint mode = srt.consts->m_postProcessingFlag & kExposureMethodMask;

			if (mode == kExposureMethodExponential)
			{
				float wantedExposureValue = srt.consts->m_toneMapSetPoint - luminanceValue;
				float lastExposureValue = srt.bufs->rwb_ctrlbuf[0].m_exposureValue;
				exposureValue = lerp(lastExposureValue, wantedExposureValue, srt.consts->m_exposeFactor);
				exposureValue = clamp(exposureValue, srt.consts->m_lightMin, srt.consts->m_lightMax);
			}
		}

		float exposureScaleFactor = exp2(exposureValue + srt.consts->m_displayBrightness);

		//srt.bufs->rwb_ctrlbuf[0].m_currentBrightness; //unchanged
		srt.bufs->rwb_ctrlbuf[0].m_exposureScaleFactor = exposureScaleFactor;
		srt.bufs->rwb_ctrlbuf[0].m_exposureValue = exposureValue;
		srt.bufs->rwb_ctrlbuf[0].m_sceneLuminance = luminanceValue;
	}
}
