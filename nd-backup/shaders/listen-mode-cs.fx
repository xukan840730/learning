
#include "cmask-util.fxi"

template<class T>
constexpr bool is_float2(){
	return false;
}
template<>
constexpr bool is_float2<float2>() {
	return true;
}

template<class T>
constexpr bool is_float4(){
	return false;
}
template<>
constexpr bool is_float4<float4>() {
	return true;
}

// Keep in sync with the same enum in listen-mode-consts.h
enum ListenModeStencilBits
{
	kListenModeCharacterStencilBit = 0x01,
	kListenModeBuddyStencilBit = 0x02,
	kListenModeBuddyHiddenStencilBit = 0x04,
	kListenModeBuddyHiddenStencilBit_HCM_Hero = 0x04,
	kListenModeBuddyHiddenStencilBit_HCM_Interactive = 0x08,
};

// --- Visualize mask bits ------------------------------------------------------- //
struct VisualizeListenModeMaskSrt
{
	Texture2D<uint>		m_src;
	RWTexture2D<float4> m_dst;
};

[numthreads(8, 8, 1)]
void CS_ListenModeVisualizeMask(int2 dispatchId : SV_DispatchThreadId, VisualizeListenModeMaskSrt srt : S_SRT_DATA)
{
	uint mask = srt.m_src[dispatchId];
	srt.m_dst[dispatchId] = float4(mask & kListenModeCharacterStencilBit ? 1.0f : 0.0f,
								   mask & kListenModeBuddyStencilBit ? 1.0f : 0.0f,
								   mask & kListenModeBuddyHiddenStencilBit ? 1.0f : 0.0f,
								   1.0f);
}

// --- Seperate visible & hidden ------------------------------------------------- //
struct SeperateListenModeBufferSrt
{
	float2				m_depthParams;
	int2				m_size;

	Texture2D<float4>	m_hiddenColor;
	Texture2D<float>	m_hiddenDepth;
	Texture2D<float4>	m_visibleColor;
	Texture2D<float>	m_visibleDepth;
	Texture2D<float>	m_sceneDepth;

	RWTexture2D<float4> m_visible;
	RWTexture2D<float4> m_hidden;
};

float GetLinearDepth(float z, float2 params)
{
	return params.y / (z - params.x);
}

bool AlmostEqual(float a, float b, float tol)
{
	return abs(a - b) < tol;
}

[numthreads(8, 8, 1)]
void CS_SeperateListenModeBufferMp(int2 dispatchId : SV_DispatchThreadId, SeperateListenModeBufferSrt *pSrt : S_SRT_DATA)
{
	float hiddenZ = GetLinearDepth(pSrt->m_hiddenDepth[dispatchId], pSrt->m_depthParams);
	float z = GetLinearDepth(pSrt->m_sceneDepth[dispatchId], pSrt->m_depthParams);

	float hidden = hiddenZ > (z + 0.05f) ? 0.0f : 1.0f;
	float4 hiddenColor = pSrt->m_hiddenColor[dispatchId];

	pSrt->m_visible[dispatchId] = float4(0.0f, 0.0f, 0.0f, 0.0f);
	pSrt->m_hidden[dispatchId] = hidden * hiddenColor;
}

[numthreads(8, 8, 1)]
void CS_SeperateListenModeBuffer(int2 dispatchId : SV_DispatchThreadId, SeperateListenModeBufferSrt *pSrt : S_SRT_DATA)
{
	float visible = 0.0f;

	float4 visibleColor = pSrt->m_visibleColor[dispatchId];
	if (any(visibleColor > 0.0f))
	{
		// We are using the temporal (stabilized) depth so do a small search for a depth that is almost eqaul.
		float listenZ = GetLinearDepth(pSrt->m_visibleDepth[dispatchId], pSrt->m_depthParams);
		for (int y = -2; y < 2; y++)
		{
			for (int x = -2; x < 2; x++)
			{
				int2 index = clamp(dispatchId + int2(x, y), 0, pSrt->m_size);
				float z = GetLinearDepth(pSrt->m_sceneDepth[index], pSrt->m_depthParams);

				if (AlmostEqual(listenZ, z, 0.1f))
				{
					visible = 1.0f;
					break;
				}
			}
		}
	}

	float4 hiddenColor = float4(0.0f);
	if (visible == 0.0f)
	{
		hiddenColor = pSrt->m_hiddenColor[dispatchId];
		if (any(hiddenColor > 0.0f))
		{
			float listenZ = GetLinearDepth(pSrt->m_hiddenDepth[dispatchId], pSrt->m_depthParams);
			float z = GetLinearDepth(pSrt->m_sceneDepth[dispatchId], pSrt->m_depthParams);
			hiddenColor = z <= listenZ ? hiddenColor : float4(0.0f);
		}
	}

	pSrt->m_visible[dispatchId] = float4(visible * visibleColor.rgb, visibleColor.a);
	pSrt->m_hidden[dispatchId] = hiddenColor;
}

// --- Downsample ---------------------------------------------------------------- //
template<class T>
struct ListenModeDownsampleSrt
{
	float2				m_invDstSize;

	Texture2D<T>		m_src;
	RWTexture2D<T>		m_dst;

	SamplerState		m_linearSampler;
};

template<class T>
T Downsample(float2 center, float2 invDstSize, Texture2D<T> src, SamplerState linearSampler)
{
	T tl = src.SampleLevel(linearSampler, (center + float2(-1.0f, -1.0f)) * invDstSize, 0);
	T tr = src.SampleLevel(linearSampler, (center + float2( 1.0f, -1.0f)) * invDstSize, 0);
	T bl = src.SampleLevel(linearSampler, (center + float2(-1.0f,  1.0f)) * invDstSize, 0);
	T br = src.SampleLevel(linearSampler, (center + float2( 1.0f,  1.0f)) * invDstSize, 0);

	return (tl + tr + bl + br) / 4.0f;
}

[numthreads(8, 8, 1)]
void CS_ListenModeDownsample(uint2 dispatchId : SV_DispatchThreadId, ListenModeDownsampleSrt<float2> *pSrt : S_SRT_DATA)
{
	float2 center = dispatchId + 0.5f;
	pSrt->m_dst[dispatchId] = Downsample<float2>(center, pSrt->m_invDstSize, pSrt->m_src, pSrt->m_linearSampler);
}

[numthreads(8, 8, 1)]
void CS_ListenModeDownsample_HCM(uint2 dispatchId : SV_DispatchThreadId, ListenModeDownsampleSrt<float4> *pSrt : S_SRT_DATA)
{
	float2 center = dispatchId + 0.5f;
	pSrt->m_dst[dispatchId] = Downsample<float4>(center, pSrt->m_invDstSize, pSrt->m_src, pSrt->m_linearSampler);
}

// --- Upsample ------------------------------------------------------------------ //
template<class T>
struct ListenModeUpsampleSrt
{
	float2				m_invDstSize;

	Texture2D<T>		m_src;
	RWTexture2D<T>		m_dst;

	SamplerState		m_linearSampler;
};

template<class T>
T Upsample(float2 center, float2 invDstSize, Texture2D<T> src, SamplerState linearSampler)
{
	const T tl = src.SampleLevel(linearSampler, (center + float2(-1.0f, -1.0f)) * invDstSize, 0);
	const T tm = src.SampleLevel(linearSampler, (center + float2( 0.0f, -1.0f)) * invDstSize, 0);
	const T tr = src.SampleLevel(linearSampler, (center + float2( 1.0f, -1.0f)) * invDstSize, 0);
	const T ml = src.SampleLevel(linearSampler, (center + float2(-1.0f,  0.0f)) * invDstSize, 0);
	const T mm = src.SampleLevel(linearSampler, (center + float2( 0.0f,  0.0f)) * invDstSize, 0);
	const T mr = src.SampleLevel(linearSampler, (center + float2( 1.0f,  0.0f)) * invDstSize, 0);
	const T bl = src.SampleLevel(linearSampler, (center + float2(-1.0f,  1.0f)) * invDstSize, 0);
	const T bm = src.SampleLevel(linearSampler, (center + float2( 0.0f,  1.0f)) * invDstSize, 0);
	const T br = src.SampleLevel(linearSampler, (center + float2( 1.0f,  1.0f)) * invDstSize, 0);
	
	return (1.0f / 16.0f) * (tl + tr + bl + br) +
		   (2.0f / 16.0f) * (tm + ml + mr + bm) +
		   (4.0f / 16.0f) * (mm);
}

[numthreads(8, 8, 1)]
void CS_ListenModeUpsample(uint2 dispatchId : SV_DispatchThreadId, ListenModeUpsampleSrt<float2> *pSrt : S_SRT_DATA)
{
	pSrt->m_dst[dispatchId] = Upsample<float2>(dispatchId + 0.5f, pSrt->m_invDstSize, pSrt->m_src, pSrt->m_linearSampler);
}

[numthreads(8, 8, 1)]
void CS_ListenModeUpsample_HCM(uint2 dispatchId : SV_DispatchThreadId, ListenModeUpsampleSrt<float4> *pSrt : S_SRT_DATA)
{
	pSrt->m_dst[dispatchId] = Upsample<float4>(dispatchId + 0.5f, pSrt->m_invDstSize, pSrt->m_src, pSrt->m_linearSampler);
}

// --- Final Upsample ------------------------------------------------------------ //
template<class T>
struct ListenModeFinalUpsampleSrt
{
	float2				m_invDstSize;
	float				m_buddyOccludedFadeOutCutoff;
	float				m_buddyOccludedFadeOutCutoffRate;

	Texture2D<uint>		m_mask;
	Texture2D<T>		m_src;
	RWTexture2D<float4> m_dst;

	SamplerState		m_linearSampler;
};

[numthreads(8, 8, 1)]
void CS_ListenModeFinalUpsample(uint2 dispatchId : SV_DispatchThreadId, ListenModeFinalUpsampleSrt<float2> *pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchId + 0.5f) * pSrt->m_invDstSize;
	float2 fade = pSrt->m_src.SampleLevel(pSrt->m_linearSampler, uv, 0);

	if (pSrt->m_mask[dispatchId] & kListenModeBuddyStencilBit)
	{
		float value = fade.r;
		if (fade.g < pSrt->m_buddyOccludedFadeOutCutoff)
		{
			value *= 1.0f - pow(1.0f - (fade.g / pSrt->m_buddyOccludedFadeOutCutoff), pSrt->m_buddyOccludedFadeOutCutoffRate);
		}

		pSrt->m_dst[dispatchId] = float4(0.0f, 0.0f, 0.0f, value);
	}
	else
	{
		pSrt->m_dst[dispatchId] = float4(0.0f, 0.0f, 0.0f, fade.r);
	}
}

[numthreads(8, 8, 1)]
void CS_ListenModeFinalUpsample_HCM(uint2 dispatchId : SV_DispatchThreadId, ListenModeFinalUpsampleSrt<float4> *pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchId + 0.5f) * pSrt->m_invDstSize;
	float4 fade = pSrt->m_src.SampleLevel(pSrt->m_linearSampler, uv, 0);

	if (pSrt->m_mask[dispatchId] & kListenModeBuddyStencilBit)
	{
		float value = fade.b;
		if (fade.g < pSrt->m_buddyOccludedFadeOutCutoff)
		{
			value *= 1.0f - pow(1.0f - (fade.g / pSrt->m_buddyOccludedFadeOutCutoff), pSrt->m_buddyOccludedFadeOutCutoffRate);
		}

		pSrt->m_dst[dispatchId] = float4(0.0f, value, 0.0f, 0.0f);
	}
	else
	{
		pSrt->m_dst[dispatchId] = float4(fade.r, fade.b, fade.a, 0.0f);
	}
}

bool CheckBit(float2 center, float2 invDstSize, Texture2D<uint> src, SamplerState pointSampler, uint bit )
{
	const uint tl = src.SampleLevel(pointSampler, center + float2(-1.0f, -1.0f) * invDstSize, 0);
	const uint tr = src.SampleLevel(pointSampler, center + float2( 1.0f, -1.0f) * invDstSize, 0);
	const uint bl = src.SampleLevel(pointSampler, center + float2(-1.0f,  1.0f) * invDstSize, 0);
	const uint br = src.SampleLevel(pointSampler, center + float2( 1.0f,  1.0f) * invDstSize, 0);

	return ( tl & bit ) || ( tr & bit ) || ( bl & bit ) || ( br & bit );
}

// --- Split --------------------------------------------------------------------- //
template<typename T>
struct ListenModeSplitSrt
{
	float2					 m_invSrcSize;
	float2					 m_invDstSize;
	uint					 m_blurBuckets;;

	Texture2D<float>		 m_src;
	Texture2D<float>		 m_amt;
	Texture2D<uint>			 m_stencil;
	RWTexture2DArray<T>		 m_dst;

	SamplerState			 m_pointSampler;
	SamplerState			 m_linearSampler;
};

template<typename T, bool highContrastMode>
void Split(int2 dispatchId, ListenModeSplitSrt<T> *pSrt)
{
	float2 center = dispatchId + 0.5f;

	// The blur radius is being stored in the range [0,1] where 0 represents max blur and 1 represents no blur.
	float blur = Downsample(center, pSrt->m_invDstSize, pSrt->m_amt, pSrt->m_linearSampler);
	float fade = Downsample(center, pSrt->m_invDstSize, pSrt->m_src, pSrt->m_linearSampler);

	T value;
	if constexpr (is_float2<T>())
		value = T(fade, 1.0f);
	if constexpr (is_float4<T>())
		value = T(0.0f, 1.0f, 0.0f, 0.0f);

	// Determine if this pixel should blur over the buddy cutout
	float2 uv = (dispatchId * 2 + 0.5f) * pSrt->m_invSrcSize;
	uint stencil = pSrt->m_stencil.SampleLevel(pSrt->m_pointSampler, uv, 0);
	float fadeTest = pSrt->m_src.SampleLevel(pSrt->m_linearSampler, uv, 0);
	
	if constexpr ( highContrastMode )
	{
		const bool is_buddy_hero_pixel			= CheckBit( uv , pSrt->m_invDstSize, pSrt->m_stencil, pSrt->m_pointSampler , kListenModeBuddyHiddenStencilBit_HCM_Hero );
		const bool is_buddy_interactive_pixel	= CheckBit( uv , pSrt->m_invDstSize, pSrt->m_stencil, pSrt->m_pointSampler , kListenModeBuddyHiddenStencilBit_HCM_Interactive );
		if( is_buddy_hero_pixel )
			value.b = fade;
		else if( is_buddy_interactive_pixel )
			value.a = fade;
		else
			value.r = fade;

		if ( is_buddy_interactive_pixel || is_buddy_hero_pixel || fadeTest == 0.0f)
			value.g = 0.0f;
	}else
	{
		const bool is_buddy_pixel = ( (stencil & kListenModeBuddyHiddenStencilBit) != 0 );
		if ( is_buddy_pixel || fadeTest == 0.0f)
			value.g = 0.0f;
	}

	float t = (1.0f - blur) * (pSrt->m_blurBuckets - 1.0f);

	uint low = floor(t);
	uint high = ceil(t);

	if (low == high)
	{
		pSrt->m_dst[uint3(dispatchId, low)] = value;
	}
	else
	{
		float f = 1.0f - saturate(t - low);

		pSrt->m_dst[uint3(dispatchId, low)] = f * value;
		pSrt->m_dst[uint3(dispatchId, high)] = (1.0f - f) * value;
	}
}

[numthreads(8, 8, 1)]
void CS_ListenModeSplit(int2 dispatchId : SV_DispatchThreadId, ListenModeSplitSrt<float2> *pSrt : S_SRT_DATA)
{
	Split<float2,false>(dispatchId, pSrt);
}

[numthreads(8, 8, 1)]
void CS_ListenModeSplit_HCM(int2 dispatchId : SV_DispatchThreadId, ListenModeSplitSrt<float4> *pSrt : S_SRT_DATA)
{
	Split<float4,true>(dispatchId, pSrt);
}

// --- Gaussian Blur 9 ----------------------------------------------------------- //
template<class T>
struct ListenModeGaussianBlurSrt
{
	float2				m_invSize;

	Texture2D<T>		m_src;
	RWTexture2D<T>		m_dst;

	SamplerState		m_linearSampler;
};

template<class T>
T Gaussian9BlurHelper(uint2 dispatchId, ListenModeGaussianBlurSrt<T> *pSrt, float2 dir)
{
	// Uses method described in http://rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/

	const float offsets[3] = { 0, 1.38461538f, 3.23076923f };
	const float weights[3] = { 0.22702702f, 0.31621621f, 0.07027027f };

	const float2 uv = (dispatchId + 0.5f) * pSrt->m_invSize;

	T result = pSrt->m_src[dispatchId] * weights[0];
	for (uint i = 1; i < 3; ++i)
	{
		result += pSrt->m_src.SampleLevel(pSrt->m_linearSampler, uv + dir * (offsets[i] * pSrt->m_invSize), 0) * weights[i];
		result += pSrt->m_src.SampleLevel(pSrt->m_linearSampler, uv - dir * (offsets[i] * pSrt->m_invSize), 0) * weights[i];
	}

	return result;
}

[numthreads(8, 8, 1)]
void CS_ListenModeGaussianBlurX(int2 dispatchId : SV_DispatchThreadId, ListenModeGaussianBlurSrt<float2> *pSrt : S_SRT_DATA)
{
	pSrt->m_dst[dispatchId] = Gaussian9BlurHelper<float2>(dispatchId, pSrt, float2(1.0f, 0.0f));
}

[numthreads(8, 8, 1)]
void CS_ListenModeGaussianBlurY(int2 dispatchId : SV_DispatchThreadId, ListenModeGaussianBlurSrt<float2> *pSrt : S_SRT_DATA)
{
	pSrt->m_dst[dispatchId] = Gaussian9BlurHelper<float2>(dispatchId, pSrt, float2(0.0f, 1.0f));
}

[numthreads(8, 8, 1)]
void CS_ListenModeGaussianBlurX_HCM(int2 dispatchId : SV_DispatchThreadId, ListenModeGaussianBlurSrt<float4> *pSrt : S_SRT_DATA)
{
	pSrt->m_dst[dispatchId] = Gaussian9BlurHelper<float4>(dispatchId, pSrt, float2(1.0f, 0.0f));
}

[numthreads(8, 8, 1)]
void CS_ListenModeGaussianBlurY_HCM(int2 dispatchId : SV_DispatchThreadId, ListenModeGaussianBlurSrt<float4> *pSrt : S_SRT_DATA)
{
	pSrt->m_dst[dispatchId] = Gaussian9BlurHelper<float4>(dispatchId, pSrt, float2(0.0f, 1.0f));
}

// --- Combine ------------------------------------------------------------------- //
template<class T>
struct ListenModeCombineSrt
{
	float2				m_invDstSize;

	Texture2D<T>		m_src;
	RWTexture2D<T>		m_dst;

	SamplerState		m_linearSampler;
};

[numthreads(8, 8, 1)]
void CS_ListenModeCombine(int2 dispatchId : SV_DispatchThreadId, ListenModeCombineSrt<float2> *pSrt : S_SRT_DATA)
{
	pSrt->m_dst[dispatchId] += Upsample<float2>(dispatchId + 0.5f, pSrt->m_invDstSize, pSrt->m_src, pSrt->m_linearSampler);
}

[numthreads(8, 8, 1)]
void CS_ListenModeCombine_HCM(int2 dispatchId : SV_DispatchThreadId, ListenModeCombineSrt<float4> *pSrt : S_SRT_DATA)
{
	pSrt->m_dst[dispatchId] += Upsample<float4>(dispatchId + 0.5f, pSrt->m_invDstSize, pSrt->m_src, pSrt->m_linearSampler);
}
