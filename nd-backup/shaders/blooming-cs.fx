#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "post-processing-common.fxi"

struct BloomDownsampleSrt
{
	RW_Texture2D<float4>	m_rwDsTexture;
	Texture2D<float3>		m_srcTexture;
	Texture2D<float>		m_srcDepth;
	Texture2D<float3>		m_srcMblurColor;
	Texture2D<float>		m_srcMblurAlpha;
	SamplerState			m_sSamplerPoint;
	SamplerState			m_sSamplerLinear;
	float2					m_invBufferSize;
	float					m_bloomThreshold;
	uint					m_frameIdx;
	uint					m_enableMblur;
};

[numthreads(8, 8, 1)]
void Cs_BloomDownsample(int2 dispatchThreadID : SV_DispatchThreadID, BloomDownsampleSrt* pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadID + 0.5f) * pSrt->m_invBufferSize;

	float3 sampleColor = pSrt->m_srcTexture.SampleLevel(pSrt->m_sSamplerLinear, uv, 0).xyz;
	if (pSrt->m_enableMblur)
	{
		float mblurAlpha = pSrt->m_srcMblurAlpha.SampleLevel(pSrt->m_sSamplerLinear, uv, 0).r;

		// If mblurAlpha is 1.0, then we know that we are not blending any motion blur color in
		float3 mblurColor = float3(0.0f);
		if (mblurAlpha < 1.0f)
		{
			mblurColor = pSrt->m_srcMblurColor.SampleLevel(pSrt->m_sSamplerLinear, uv, 0);
		}

		sampleColor = (mblurColor * (1.0f - mblurAlpha)) + (sampleColor * mblurAlpha);
	}
	float  depthInfo = pSrt->m_srcDepth.SampleLevel(pSrt->m_sSamplerPoint, uv, 0);
	float  zFarScale = depthInfo == 1.0f ? pSrt->m_bloomThreshold : 1.0f;

#ifdef QUARTER_RES
	// Sum horizontally
	float3 sumSample_01 = sampleColor;
	float  zFarScale_01 = zFarScale;
	sumSample_01.x += LaneSwizzle(sumSample_01.x, 0x1f, 0x00, 0x01);
	sumSample_01.y += LaneSwizzle(sumSample_01.y, 0x1f, 0x00, 0x01);
	sumSample_01.z += LaneSwizzle(sumSample_01.z, 0x1f, 0x00, 0x01);
	zFarScale_01 += LaneSwizzle(zFarScale_01, 0x1f, 0x00, 0x01);
	// Sum vertically
	sumSample_01.x += LaneSwizzle(sumSample_01.x, 0x1f, 0x00, 0x08);
	sumSample_01.y += LaneSwizzle(sumSample_01.y, 0x1f, 0x00, 0x08);
	sumSample_01.z += LaneSwizzle(sumSample_01.z, 0x1f, 0x00, 0x08);
	zFarScale_01 += LaneSwizzle(zFarScale_01, 0x1f, 0x00, 0x08);

	sumSample_01 *= 0.25f;
	zFarScale_01 *= 0.25f;

	if ((dispatchThreadID.x & 1) + (dispatchThreadID.y & 1) == 0)
		pSrt->m_rwDsTexture[dispatchThreadID / 2] = float4(sumSample_01, zFarScale_01);
#else // HALF_RES
	pSrt->m_rwDsTexture[dispatchThreadID] = float4(sampleColor, zFarScale);
#endif
}

struct BloomBlurInfoParams
{
	float2				m_invBufferSize;
	float2				m_bloomThreshold;
	float2				m_bloomScale;
	float2				m_lensDirtIntensity;
};

struct CreateBaseBloomBuffers
{
	ByteAddressBuffer		tonemap_param;
};

struct CreateBaseBloomTextures
{
	RWTexture2D<float4>		rwb_dst;
	Texture2D<float4>		src_color;
};

struct CreateBaseBloomSamplers
{
	SamplerState samplerLinear;	
};

struct CreateBaseBloomSrt
{
	BloomBlurInfoParams     *consts;
	CreateBaseBloomBuffers  *bufs;
	CreateBaseBloomTextures *texs;
	CreateBaseBloomSamplers *smpls;
};

float3 GetBloomBaseColor(float3 inputColor, float brightness, BloomBlurInfoParams* params)
{
	float clampedBrightness = min(brightness, params->m_bloomThreshold.y);
	float bloomBrightness = max(clampedBrightness - params->m_bloomThreshold.x, 0.0);

	float finalBloomBrightness = clampedBrightness * params->m_bloomScale.x;
	if (bloomBrightness > 0.0f)
		finalBloomBrightness = max(finalBloomBrightness, bloomBrightness * params->m_bloomScale.y);

	return brightness > 0.0f ? (finalBloomBrightness / brightness) * inputColor : 0.0f;
}

[numthreads(8, 8, 1)]
void Cs_CreateBaseBloomTexSimplify(uint3 dispatchThreadID : SV_DispatchThreadID, CreateBaseBloomSrt srt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadID.xy + 0.5f) * srt.consts->m_invBufferSize;

	float4 sampleInfo = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv, 0);

	float3 sumSample = sampleInfo.xyz * sampleInfo.w;

	float tonemapParam = asfloat(srt.bufs->tonemap_param.Load(0));
	float brightness = max(dot(sumSample, s_luminanceVector.xyz) * tonemapParam, 0);
	sumSample = GetBloomBaseColor(sumSample, brightness, srt.consts);

	float lensDirtLuma = min(brightness, srt.consts->m_lensDirtIntensity.y) * srt.consts->m_lensDirtIntensity.x;

	srt.texs->rwb_dst[dispatchThreadID.xy] = float4(sumSample, lensDirtLuma);
}

struct BloomLayersMergeConstants
{
	float4		m_bloomLayerParam[7];
	float4		m_lensDirtParams;
};

struct MergeBloomLayerTextures
{
	RWTexture2D<float4>		rwb_merge;
	Texture2D<float4>		src_color;
	Texture2D<float4>		m_lensDirtTexture;
};

struct MergeBloomLayerSamplers
{
	SamplerState samplerLinear;	
};

struct MergeBloomLayerSrt
{
	BloomLayersMergeConstants *consts;
	MergeBloomLayerTextures	  *texs;
	MergeBloomLayerSamplers   *smpls;
};

[numthreads(8, 8, 1)]
void CS_MergeDownBloomLayers(uint3 dispatchThreadId : SV_DispatchThreadId, MergeBloomLayerSrt srt : S_SRT_DATA) : SV_Target
{
	float2 uv = (dispatchThreadId.xy + 0.0f) / srt.consts->m_bloomLayerParam[0].xy;
	float4 sumBloomLayers = 0;
	sumBloomLayers =						 GetSmoothLayer(srt.texs->src_color, srt.smpls->samplerLinear, uv, srt.consts->m_bloomLayerParam[6].xyz, 6);
	sumBloomLayers = sumBloomLayers * 1.5f + GetSmoothLayer(srt.texs->src_color, srt.smpls->samplerLinear, uv, srt.consts->m_bloomLayerParam[5].xyz, 5);
	sumBloomLayers = sumBloomLayers * 1.5f + GetSmoothLayer(srt.texs->src_color, srt.smpls->samplerLinear, uv, srt.consts->m_bloomLayerParam[4].xyz, 4);
	sumBloomLayers = sumBloomLayers * 1.5f + GetSmoothLayer(srt.texs->src_color, srt.smpls->samplerLinear, uv, srt.consts->m_bloomLayerParam[3].xyz, 3);
	sumBloomLayers = sumBloomLayers * 1.5f + GetSmoothLayer(srt.texs->src_color, srt.smpls->samplerLinear, uv, srt.consts->m_bloomLayerParam[2].xyz, 2);
	sumBloomLayers = sumBloomLayers * 1.5f + GetSmoothLayer(srt.texs->src_color, srt.smpls->samplerLinear, uv, srt.consts->m_bloomLayerParam[1].xyz, 1);
	sumBloomLayers *= 1.5f;

	if (srt.consts->m_lensDirtParams.x > 0)
	{
		float2 maskUv = (uv - 0.5) * srt.consts->m_lensDirtParams.y + 0.5;
		float3 lensDirtMask = srt.texs->m_lensDirtTexture.SampleLevel(srt.smpls->samplerLinear, maskUv, 0).rgb;
		float2 distortUv = GetDistortionOffset(1.0 - uv, 4.0,  0.25);

		float4 lensDirtBloom = GetSmoothLayer(srt.texs->src_color, srt.smpls->samplerLinear, distortUv, float3(srt.consts->m_bloomLayerParam[5].xy, 1.0f), 5, true);
		float3 lensDirtBloomLuma = dot(lensDirtBloom.rgb, s_luminanceVector.xyz);
		lensDirtBloom.rgb = lensDirtBloom.rgb * lensDirtBloom.a / max(lensDirtBloomLuma, 0.00001f);

		sumBloomLayers.rgb += lensDirtMask * lensDirtBloom.rgb;
	}

	srt.texs->rwb_merge[int2(dispatchThreadId.xy)] = float4(sumBloomLayers.xyz, 1.f);
}

