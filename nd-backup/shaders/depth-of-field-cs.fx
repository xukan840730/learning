#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "post-processing-common.fxi"

#define kMaxBlurSamples 12

struct DofCocCalHalfTextures
{
	RWTexture2D<float>		rwb_coc;
	RWTexture2D<float>		rwb_coc_half;
	Texture2D<float>		src_depth;
};

struct DofCocCalHalfSrt
{
	DofBlurConstants			*consts;
	DofCocCalHalfTextures		*texs;
	uint m_frameId;
};

struct DofCocCalHalfMSTextures
{
	RWTexture2D<float>		rwb_coc;
	RWTexture2D<float>		rwb_coc_half;
	Texture2DMS<float, 2>	src_depth;
};

struct DofCocCalHalfMSSrt
{
	DofBlurConstants			*consts;
	DofCocCalHalfMSTextures		*texs;
	uint m_frameId;
};

groupshared float g_cocBuffer[8][8];
[numthreads(8, 8, 1)]
void CS_DepthOfFieldCocCalculatePassHalf(uint3 dispatchThreadId : SV_DispatchThreadId, DofCocCalHalfSrt srt : S_SRT_DATA) : SV_Target
{
	float coc = 0.0f;

	float viewSpaceWithAlphaZ = srt.texs->src_depth[int2(dispatchThreadId.xy)];
	float linearDepth = 1.0f / (viewSpaceWithAlphaZ * srt.consts->m_linearDepthParams.x + srt.consts->m_linearDepthParams.y);

	float fNumber = srt.consts->m_fNumber;
	if (fNumber >= srt.consts->m_minFNumber)
	{
		coc = GetCocRadius(srt.consts->m_fNumber, linearDepth, srt.consts->m_focusPlaneDist, srt.consts->m_lensfocalLength) * srt.consts->m_pixelFilmRatio;

		float absCoc = abs(coc);
		float2 scaleOffsetParams = coc > 0.0f ? srt.consts->m_dofBlurParams.xz : srt.consts->m_dofBlurParams.yw;
		float finalCoc = max(absCoc - scaleOffsetParams.y, 0.0f) * scaleOffsetParams.x;
		coc = coc > 0.0f ? finalCoc : -finalCoc;
	}
	else
	{
		coc = dot(saturate(linearDepth.xxxx * srt.consts->m_dofRangeScaleInfo.xyzw + srt.consts->m_dofRangeOffsetInfo.xyzw),
					float4(srt.consts->m_dofIntensityInfo.x, srt.consts->m_dofIntensityInfo.y, -srt.consts->m_dofIntensityInfo.y, srt.consts->m_dofIntensityInfo.w)) + srt.consts->m_dofIntensityInfo.z; //
	}

	srt.texs->rwb_coc[int2(dispatchThreadId.xy)] = coc;

	uint2 outputIndex = dispatchThreadId.xy / 2;
	uint2 offset = dispatchThreadId.xy & 0x01;
	if (offset.x == 0 && offset.y == 0)
		srt.texs->rwb_coc_half[dispatchThreadId.xy / 2] = coc < 0.0f ? coc : 0.0f;
}

[numthreads(8, 8, 1)]
void CS_DepthOfFieldCocCalculatePassHalfMS(uint3 dispatchThreadId : SV_DispatchThreadId, DofCocCalHalfMSSrt srt : S_SRT_DATA) : SV_Target
{
	float coc = 0.0f;

	int2 location = int2(dispatchThreadId.x / 2, dispatchThreadId.y);
	int sampleIndex = (dispatchThreadId.x + dispatchThreadId.y + srt.m_frameId) & 1;
	float viewSpaceWithAlphaZ = srt.texs->src_depth.Load(location, sampleIndex);
	float linearDepth = 1.0f / (viewSpaceWithAlphaZ * srt.consts->m_linearDepthParams.x + srt.consts->m_linearDepthParams.y);

	float fNumber = srt.consts->m_fNumber;
	if (fNumber >= srt.consts->m_minFNumber)
	{
		coc = GetCocRadius(srt.consts->m_fNumber, linearDepth, srt.consts->m_focusPlaneDist, srt.consts->m_lensfocalLength) * srt.consts->m_pixelFilmRatio;

		float absCoc = abs(coc);
		float2 scaleOffsetParams = coc > 0.0f ? srt.consts->m_dofBlurParams.xz : srt.consts->m_dofBlurParams.yw;
		float finalCoc = max(absCoc - scaleOffsetParams.y, 0.0f) * scaleOffsetParams.x;
		coc = coc > 0.0f ? finalCoc : -finalCoc;
	}
	else
	{
		coc = dot(saturate(linearDepth.xxxx * srt.consts->m_dofRangeScaleInfo.xyzw + srt.consts->m_dofRangeOffsetInfo.xyzw),
					float4(srt.consts->m_dofIntensityInfo.x, srt.consts->m_dofIntensityInfo.y, -srt.consts->m_dofIntensityInfo.y, srt.consts->m_dofIntensityInfo.w)) + srt.consts->m_dofIntensityInfo.z; //
	}

	srt.texs->rwb_coc[int2(dispatchThreadId.xy)] = coc;

	uint2 outputIndex = dispatchThreadId.xy / 2;
	uint2 offset = dispatchThreadId.xy - outputIndex * 2;
	if (offset.x == 0 && offset.y == 0)
		srt.texs->rwb_coc_half[outputIndex] = coc < 0.0f ? coc : 0.0f;
}

struct DofCocCalHalfOldTextures
{
	RWTexture2D<float>		rwb_coc;
	RWTexture2D<float>		rwb_coc_half;
	Texture2D<float4>		src_color;
	Texture2D<float>		listen_mask;
};

struct DofCocCalHalfOldSrt
{
	DofBlurConstants			*consts;
	DofCocCalHalfOldTextures	*texs;
};

[numthreads(8, 8, 1)]
void CS_DepthOfFieldCocCalculatePassHalfOld(uint3 dispatchThreadId : SV_DispatchThreadId, DofCocCalHalfOldSrt srt : S_SRT_DATA) : SV_Target
{
	float linearDepth = srt.texs->src_color[int2(dispatchThreadId.xy)].a;

	float2 cocNearFarIntensity = clamp(linearDepth.xx * srt.consts->m_dofRangeScaleInfo.xy + srt.consts->m_dofRangeOffsetInfo.xy, float2(0, 0), srt.consts->m_dofIntensityInfo.xy) * 
								 (srt.texs->listen_mask[int2(dispatchThreadId.xy)] < 0.5f ? 1.0f : 0.0f);
	float coc = max(cocNearFarIntensity.x, cocNearFarIntensity.y);
	coc = cocNearFarIntensity.x > 0.0f ? -coc : coc;

	srt.texs->rwb_coc[int2(dispatchThreadId.xy)] = coc;

	uint2 outputIndex = dispatchThreadId.xy / 2;
	uint2 offset = dispatchThreadId.xy - outputIndex * 2;
	if (offset.x == 0 && offset.y == 0)
		srt.texs->rwb_coc_half[outputIndex] = cocNearFarIntensity.x > 0.0f ? -cocNearFarIntensity.x : 0.0f;
}

struct DownSampleBufferSrt
{
	RWTexture2D<float3>				m_rwbColor;
	RWTexture2D<float>				m_rwbCocRadius;
	Texture2D<float3>				m_srcColor;
	Texture2D<float>				m_srcCocRadius;
};

[numthreads(8, 8, 1)]
void CS_DownSampleSrcBufferPass(int3 dispatchThreadId : SV_DispatchThreadId, DownSampleBufferSrt* pSrt : S_SRT_DATA)
{
	pSrt->m_rwbColor[int2(dispatchThreadId.xy)] = pSrt->m_srcColor[int2(dispatchThreadId.xy) * 2];
	pSrt->m_rwbCocRadius[int2(dispatchThreadId.xy)] = pSrt->m_srcCocRadius[int2(dispatchThreadId.xy) * 2];
}

#define kWeightScale		1024.0f

struct ShadowSamplePattern
{
	float		samples[4][4][8];
};

struct DepthOfFieldScatteringSrt
{
	float4							m_screenScaleOffset;
	int2							m_bufSize;
	uint							m_bufferScale;
	uint							m_numMaxSamples;
	float							m_ditherSamples[4][4];
	RWByteAddressBuffer				m_rwbAccBuffer;
	Texture2D<float3>				m_srcColor;
	Texture2D<float>				m_dofCocRadius;
	ByteAddressBuffer				m_exposureControlBuffer;
	ShadowSamplePattern*			m_pSamplePattern;
};

uint GetTileBufferAddress(int2 iTexCoord, uint bufferWidth)
{
	int2 iGroupId = iTexCoord / 16;
	int2 iInnerId = iTexCoord % 16;

	int iInnerIndex = iInnerId.y * 16 + iInnerId.x;
	int iGroupIndex = iGroupId.y * ((bufferWidth + 15) / 16) + iGroupId.x;

	return (iGroupIndex * 256 + iInnerIndex) * 8;
}

uint2 PackHdrDofSamples(float3 blendSampleColor, float blendWeight)
{
	uint3 uBlendSampleColorLog = uint3(sqrt(blendSampleColor) * kWeightScale);
	uint sampleWeightLog = uint(sqrt(blendWeight) * kWeightScale);

	uint2 curPackRgbWeight;
	curPackRgbWeight.x = uBlendSampleColorLog.x | (uBlendSampleColorLog.y << 16);
	curPackRgbWeight.y = uBlendSampleColorLog.z | (sampleWeightLog << 16);

	return curPackRgbWeight;
}

float4 UnpackHdrDofSamples(uint2 packedColor)
{
	float4 unpackedColor = float4(packedColor.x & 0xffff, packedColor.x >> 16, packedColor.y & 0xffff, packedColor.y >> 16) / kWeightScale;
	float3 finalColor = unpackedColor.w == 0.0f ? 0.0f : unpackedColor.xyz / unpackedColor.w;
	return float4(finalColor.xyz * finalColor.xyz, unpackedColor.w * unpackedColor.w);
}

int ConvertToCacheIdx(int2 localBufCoord, int cacheBufWidth)
{
	return localBufCoord.y * cacheBufWidth + localBufCoord.x;
}

int2 ConvertCacheIdxToLocalCoord(int idx, int cacheBufWidth)
{
	return int2(idx % cacheBufWidth, idx / cacheBufWidth);
}

#define kNumThreads				16
#define kLocalMaxRadius			8
#define kTotalNumThreads		(kNumThreads * kNumThreads)
#define kMaxLocalBufferWidth	(kNumThreads + kLocalMaxRadius * 2)
#define kTmpBufferSize			(kMaxLocalBufferWidth * kMaxLocalBufferWidth)

groupshared uint g_dofAccuInfoX[kTmpBufferSize];
groupshared uint g_dofAccuInfoY[kTmpBufferSize];
#define kMaxDofCocRadius	16.0f

[numthreads(kNumThreads, kNumThreads, 1)]
void CS_DepthOfFieldScatteringPass(int2 dispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, DepthOfFieldScatteringSrt* pSrt : S_SRT_DATA)
{
	int2 srcDispatchThreadId = dispatchThreadId;// + pSrt->m_pixelOffset;

	float centerCocRadius = pSrt->m_dofCocRadius[srcDispatchThreadId] * 0.5f;
	float dofCoc = clamp(centerCocRadius, -kMaxDofCocRadius, kMaxDofCocRadius);
	float absDofCoc = abs(dofCoc);

	float maxCoc_01 = max(absDofCoc, LaneSwizzle(absDofCoc, 0x1f, 0x00, 0x01));
	float maxCoc_02 = max(maxCoc_01, LaneSwizzle(maxCoc_01, 0x1f, 0x00, 0x02));
	float maxCoc_04 = max(maxCoc_02, LaneSwizzle(maxCoc_02, 0x1f, 0x00, 0x04));
	float maxCoc_08 = max(maxCoc_04, LaneSwizzle(maxCoc_04, 0x1f, 0x00, 0x08));
	float maxCoc_10 = max(maxCoc_08, LaneSwizzle(maxCoc_08, 0x1f, 0x00, 0x10));

	float maxDofCoc = max(maxCoc_10, ReadLane(maxCoc_10, 0x20));

	int iMaxDofCoc = int(maxDofCoc + 0.8f);

	int iClampedMaxCoc = kLocalMaxRadius;//min(iMaxDofCoc, kLocalMaxRadius);

	int localBufWidth = kNumThreads + iClampedMaxCoc * 2;

	int2 localBufStart = groupId.xy * kNumThreads - iClampedMaxCoc;

	uint numItems = localBufWidth * localBufWidth;

	for (int localBufIdx = groupIndex; localBufIdx < numItems; localBufIdx += kTotalNumThreads)
	{
		g_dofAccuInfoX[localBufIdx] = 0;
		g_dofAccuInfoY[localBufIdx] = 0;
	}

	int numSamples = absDofCoc > 3.6f ? 36 : 
					(absDofCoc > 3.0f ? 24 : 
					(absDofCoc > 2.6f ? 20 : 
					(absDofCoc > 2.0f ? 12 : 
					(absDofCoc > 1.6f ? 8 : 4))));

	float radiusScale = absDofCoc > 3.6f ? 1.0f / 3.16227766f : 
					(absDofCoc > 3.0f ? 1.0f / 2.828f : 
					(absDofCoc > 2.6f ? 1.0f / 2.236f : 
					(absDofCoc > 2.0f ? 1.0f / 2.0f : 
					(absDofCoc > 1.6f ? 1.0f / 1.41421f : 1.0f))));

	float maxCocSize = maxDofCoc * maxDofCoc;
	float curCocSize = absDofCoc * absDofCoc;

	if (dispatchThreadId.y < pSrt->m_bufSize.y)
	{
		float2 angle = (dispatchThreadId.yx * (0.4 * 0.434) + dispatchThreadId.xy * 0.434) * 3.1415926f * 2.0f;
		float2 sinCosAngle;
		sincos(angle.x, sinCosAngle.x, sinCosAngle.y);

		float2x2 rotMat = float2x2(float2(sinCosAngle.x, sinCosAngle.y), float2(-sinCosAngle.y, sinCosAngle.x));

		const float2 possionPattern[36] = { float2(1, 0),	float2(0, -1),	float2(-1, 0),	float2(0, 1),
											float2(1, 1),	float2(1, -1),	float2(-1, -1),	float2(-1, 1),
											float2(2, 0),	float2(0, -2),	float2(-2, 0),	float2(0, 2),
											float2(1, 2),	float2(2, 1),	float2(2, -1),	float2(1, -2),	float2(-1, -2),	float2(-2, -1),	float2(-2, 1),	float2(-1, 2),
											float2(2, 2),	float2(2, -2),	float2(-2, -2), float2(-2, 2),
											float2(3, 0),	float2(0, -3),	float2(-3, 0),	float2(0, 3),
											float2(3, 1),	float2(3, 1),	float2(3, -1),	float2(1, -3),	float2(-1, -3),	float2(-3, -1), float2(-3, 1),	float2(-1, 3)};

		float tonemapScale = asfloat(pSrt->m_exposureControlBuffer.Load(0));

		if (maxCocSize > 1.0f && curCocSize > 1.01f)
		{
			float3 thisSample = pSrt->m_srcColor[srcDispatchThreadId];
			float pixelWeight = (1.0f - 1.0f / curCocSize) / numSamples;

			uint2 curPackRgbWeight = PackHdrDofSamples(thisSample * pixelWeight, pixelWeight);

			for (int i = 0; i < numSamples; i++)
			{
				float2 pixelOffset = mul(possionPattern[i] * radiusScale, rotMat) * absDofCoc;
				int2 iBufCoord = int2(round(srcDispatchThreadId + pixelOffset));
				if (all(iBufCoord < pSrt->m_bufSize) && all(iBufCoord > 0))
				{
					float sampleCocRadius = pSrt->m_dofCocRadius[iBufCoord] * 0.5f;
					float lerpRatio = saturate(sampleCocRadius - centerCocRadius + 0.5f);

					if (lerpRatio > 0.001f)
					{
						float sampleWeight = pixelWeight * lerpRatio;
						uint2 samplePackRgbWeight = lerpRatio > 0.99f ? curPackRgbWeight : PackHdrDofSamples(thisSample * sampleWeight, sampleWeight);

						int2 localBufCoord = iBufCoord - localBufStart;

						if (all(localBufCoord < localBufWidth) && all(localBufCoord >= 0))
						{
							uint localBufIndex = ConvertToCacheIdx(localBufCoord, localBufWidth);
							uint uOldAccuValue;
							InterlockedAdd(g_dofAccuInfoX[localBufIndex], samplePackRgbWeight.x, uOldAccuValue);
							InterlockedAdd(g_dofAccuInfoY[localBufIndex], samplePackRgbWeight.y, uOldAccuValue);
						}
						else
						{
							uint linearBufferAddress = GetTileBufferAddress(iBufCoord, pSrt->m_bufSize.x);
							uint uOldAccuValue;
							if (samplePackRgbWeight.x != 0)	pSrt->m_rwbAccBuffer.InterlockedAdd(linearBufferAddress, samplePackRgbWeight.x, uOldAccuValue);
							if (samplePackRgbWeight.y != 0)	pSrt->m_rwbAccBuffer.InterlockedAdd(linearBufferAddress + 4, samplePackRgbWeight.y, uOldAccuValue);
						}
					}
				}
			}
		}
	}

	ThreadGroupMemoryBarrierSync();

	for (int localBufIdx = groupIndex; localBufIdx < numItems; localBufIdx += kTotalNumThreads)
	{
		uint2 dofAccuInfo = uint2(g_dofAccuInfoX[localBufIdx], g_dofAccuInfoY[localBufIdx]);
		if ((dofAccuInfo.x | dofAccuInfo.y) != 0)
		{
			int2 sampleThreadIndex = ConvertCacheIdxToLocalCoord(localBufIdx, localBufWidth) + localBufStart;
			uint linearBufferAddress = GetTileBufferAddress(sampleThreadIndex, pSrt->m_bufSize.x);

			uint uOldAccuValue;
			if (dofAccuInfo.x != 0)	pSrt->m_rwbAccBuffer.InterlockedAdd(linearBufferAddress, dofAccuInfo.x, uOldAccuValue);
			if (dofAccuInfo.y != 0)	pSrt->m_rwbAccBuffer.InterlockedAdd(linearBufferAddress + 4, dofAccuInfo.y, uOldAccuValue);
		}
	}
}

struct ResolveScatteringBufferSrt
{
	int2							m_bufSize;
	uint							m_bufferScale;
	uint							m_frameId;
	RWTexture2D<float3>				m_rwbDof;
	RWTexture2D<float>				m_rwbDofAlpha;
	ByteAddressBuffer				m_dofAccBuffer;
	Texture2D<float>				m_dofCocRadius;
	Texture2D<float4>				m_curFrameColor;
	ByteAddressBuffer				m_exposureControlBuffer;
};

[numthreads(8, 8, 1)]
void CS_ResolveScatteringDofAccuBufferPass(int3 dispatchThreadId : SV_DispatchThreadId, ResolveScatteringBufferSrt* pSrt : S_SRT_DATA)
{
	uint2 packedColor;
	uint linearBufferAddress = GetTileBufferAddress(dispatchThreadId.xy, pSrt->m_bufSize.x);
	packedColor = pSrt->m_dofAccBuffer.Load2(linearBufferAddress);

	float4 unpackedColor = UnpackHdrDofSamples(packedColor);
	float saturateWeight = saturate(unpackedColor.w);
	float3 normalizedColor = unpackedColor.xyz;

	pSrt->m_rwbDof[int2(dispatchThreadId.xy)] = normalizedColor * saturateWeight;
	pSrt->m_rwbDofAlpha[int2(dispatchThreadId.xy)] = saturateWeight;
}

struct DofBufferBlurSrt
{
	float2						m_invBufferSize;
	float2						m_pad;
	RWTexture2D<float3>			m_rwbDofBlurredBuffer;
	RWTexture2D<float>			m_rwbDofBlurredAlphaBuffer;
	Texture2D<float3>			m_txBlurSourceBuffer;
	Texture2D<float>			m_txBlurSourceAlphaBuffer;
	Texture2D<float>			m_dofCocRadius;
	SamplerState				m_linearClampSampler;
};

[numthreads(8, 8, 1)]
void CS_BlurDofBuffer(int2 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, DofBufferBlurSrt* pSrt : S_SRT_DATA)
{
	float2 angle = frac(dispatchThreadId.yx * (0.4 * 0.434) + dispatchThreadId.xy * 0.434) * 3.1415926f * 2.0f;
	float2 sinCosAngle;
	sincos(angle.x, sinCosAngle.x, sinCosAngle.y);

	float2x2 rotMat = float2x2(float2(sinCosAngle.x, sinCosAngle.y), float2(-sinCosAngle.y, sinCosAngle.x));

	float2 uv = (dispatchThreadId + 0.5f) * pSrt->m_invBufferSize;
	float coc = max(abs(pSrt->m_dofCocRadius[dispatchThreadId.xy * 2] * 0.5f), 2.0f);

	float3 sumDofColor = pSrt->m_txBlurSourceBuffer[dispatchThreadId];
	float sumDofAlpha = pSrt->m_txBlurSourceAlphaBuffer[dispatchThreadId];

//	if (coc > 0.5f)
	{
		const float2 possionPattern[12] = { float2(-0.326212f, -0.405805f),
											float2(-0.840144f, -0.07358f),
											float2(-0.695914f, 0.457137f),
											float2(-0.203345f, 0.620716f),
											float2(0.96234f, -0.194983f),
											float2(0.473434f, -0.480026f),
											float2(0.519456f, 0.767022f),
											float2(0.185461f, -0.893124f),
											float2(0.507431f, 0.064425f),
											float2(0.89642f, 0.412458f),
											float2(-0.32194f, -0.932615f),
											float2(-0.791559f, -0.597705f)};

		float radiusPixels = coc;
	
		for (int i = 0; i < 12; i++)
		{
			sumDofColor += pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_linearClampSampler, uv + min(radiusPixels, 2.0f) * possionPattern[i] * pSrt->m_invBufferSize, 0);
			sumDofAlpha += pSrt->m_txBlurSourceAlphaBuffer.SampleLevel(pSrt->m_linearClampSampler, uv + min(radiusPixels, 2.0f) * possionPattern[i] * pSrt->m_invBufferSize, 0);
		}

		sumDofColor /= 13.0f;
		sumDofAlpha /= 13.0f;
	}

	pSrt->m_rwbDofBlurredBuffer[dispatchThreadId.xy] = sumDofColor;
	pSrt->m_rwbDofBlurredAlphaBuffer[dispatchThreadId.xy] = sumDofAlpha;
}

struct MergeDofBufferSrt
{
	float2							m_invBufferSize;
	float2							m_pad;
	RWTexture2D<float3>				m_rwbPrimaryColor;
	Texture2D<float3>				m_srcDofColor;
	Texture2D<float>				m_srcDofAlpha;
	SamplerState					m_linearClampSampler;
};

[numthreads(8, 8, 1)]
void CS_MergeBackToPrimaryBufferPass(int3 dispatchThreadId : SV_DispatchThreadId, MergeDofBufferSrt* pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId.xy + 0.5f) * pSrt->m_invBufferSize;
	float3 dofColor = pSrt->m_srcDofColor.SampleLevel(pSrt->m_linearClampSampler, uv, 0);
	float dofAlpha = pSrt->m_srcDofAlpha.SampleLevel(pSrt->m_linearClampSampler, uv, 0);

	pSrt->m_rwbPrimaryColor[dispatchThreadId.xy] = pSrt->m_rwbPrimaryColor[dispatchThreadId.xy] * (1.0f - dofAlpha) + dofColor;
}

groupshared float4 g_srcColorBuffer[89];
groupshared float2 g_mblurVecBuffer[89];


/*
	NUM_THREADS_MAJOR and NUM_THREADS_MINOR are defined in
	files.json.
*/

// Just to get the other techniques to compile
#ifndef NUM_THREADS_MAJOR
#define NUM_THREADS_MAJOR 64
#endif
#ifndef NUM_THREADS_MINOR
#define NUM_THREADS_MINOR 1
#endif

#define kNumLdsUnits ((kMaxBlurSamples*2+1)+NUM_THREADS_MAJOR)

groupshared float g_mblurVecBuffer_x[NUM_THREADS_MINOR][kNumLdsUnits];
groupshared float g_mblurVecBuffer_y[NUM_THREADS_MINOR][kNumLdsUnits];

groupshared float g_srcColorBuffer_x[NUM_THREADS_MINOR][kNumLdsUnits];
groupshared float g_srcColorBuffer_y[NUM_THREADS_MINOR][kNumLdsUnits];
groupshared float g_srcColorBuffer_z[NUM_THREADS_MINOR][kNumLdsUnits];
groupshared float g_srcColorBuffer_w[NUM_THREADS_MINOR][kNumLdsUnits];

void SetSrcColorBuffer(int threadRow, int idx, float4 value)
{
	g_srcColorBuffer_x[threadRow][idx] = value.x;
	g_srcColorBuffer_y[threadRow][idx] = value.y;
	g_srcColorBuffer_z[threadRow][idx] = value.z;
	g_srcColorBuffer_w[threadRow][idx] = value.w;
}

float4 GetSrcColorBuffer(int threadRow, int idx)
{
	return float4(g_srcColorBuffer_x[threadRow][idx],
		g_srcColorBuffer_y[threadRow][idx],
		g_srcColorBuffer_z[threadRow][idx],
		g_srcColorBuffer_w[threadRow][idx]);
}

void SetMblurVecBuffer(int threadRow, int idx, float2 value)
{
	g_mblurVecBuffer_x[threadRow][idx] = value.x;
	g_mblurVecBuffer_y[threadRow][idx] = value.y;
}

float2 GetMblurVecBuffer(int threadRow, int idx)
{
	return float2(g_mblurVecBuffer_x[threadRow][idx], g_mblurVecBuffer_y[threadRow][idx]);
}

#define kCocWeight 0.1f

struct DofSimpleBlurSharedData
{
	RWTexture2D<float4>		m_dstColor;
	Texture2D<float4>		m_srcColor;
	Texture2D<float>		m_srcCoc;
	Texture2D<float>		m_blurredCoc;

	SamplerState			m_linearSampler;

	int2					m_size;
	float2					m_invSize;

};

struct DofSimpleBlurSrt
{
	DofSimpleBlurSharedData		*m_pData;
	int2						m_screenOffset;
};

[numthreads(NUM_THREADS_MAJOR, NUM_THREADS_MINOR, 1)]
void CS_DepthOfFieldSimplePassXWithHalfBuffer(int2 localDispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadID, DofSimpleBlurSrt srt : S_SRT_DATA)
{
	int2 dispatchThreadId = localDispatchThreadId.xy + srt.m_screenOffset;
	int threadRow = groupThreadId.y;
#if NUM_THREADS_MINOR == 1
		threadRow = 0;
#endif

	float g_sampleWeight[kMaxBlurSamples+1] = {0.985f, 0.95f, 0.9f, 0.825f, 0.77f, 0.7f, 0.625f, 0.5f, 0.43f, 0.35f, 0.28f, 0.2f, 0.1f};

	float2 uv = (dispatchThreadId.xy + 0.5) * srt.m_pData->m_invSize.xy;

	int idx = groupThreadId.x;
	int2 iUv = int2(max(dispatchThreadId.x, kMaxBlurSamples) - kMaxBlurSamples, dispatchThreadId.y);

	SetSrcColorBuffer(threadRow, idx, float4(srt.m_pData->m_srcColor[iUv].xyz,
		saturate(abs(srt.m_pData->m_srcCoc[iUv] * kCocWeight))));

	if (idx < (kMaxBlurSamples*2+1))
	{
		iUv = int2(min(dispatchThreadId.x+NUM_THREADS_MAJOR-kMaxBlurSamples, srt.m_pData->m_size.x - 1), dispatchThreadId.y);
		SetSrcColorBuffer(threadRow, idx+NUM_THREADS_MAJOR, float4(srt.m_pData->m_srcColor[iUv].xyz,
			saturate(abs(srt.m_pData->m_srcCoc[iUv] * kCocWeight))));
	}

	float blurredCoc = srt.m_pData->m_blurredCoc.SampleLevel(srt.m_pData->m_linearSampler, uv, 0).x;
	float blurRadius = min(max(srt.m_pData->m_srcCoc[int2(dispatchThreadId.xy)], blurredCoc), kMaxBlurSamples);

	float fNumSamples = blurRadius;
	int numSamples = int(fNumSamples);

	ThreadGroupMemoryBarrierSync();

	float refWeight = numSamples > 0 ? lerp(g_sampleWeight[numSamples-1], g_sampleWeight[numSamples], frac(fNumSamples)) : 0.0f;
	float4 centerSample = GetSrcColorBuffer(threadRow, idx+kMaxBlurSamples);
	float sumWeight = max((1.0f - refWeight)  * (numSamples == 0 ? 1.0f : centerSample.w), 0.01f);
	float3 sumColor = centerSample.xyz * sumWeight;

	for (int i = 1; i <= numSamples; i++)
	{
		float weight = max(g_sampleWeight[i - 1] - refWeight, 0);

		float4 leftSample = GetSrcColorBuffer(threadRow, idx + kMaxBlurSamples - i);
		float4 rightSample = GetSrcColorBuffer(threadRow, idx + kMaxBlurSamples + i);

		float leftWeight = weight * leftSample.w;
		float rightWeight = weight * rightSample.w;

		sumColor += leftSample.xyz * leftWeight + rightSample.xyz * rightWeight;
		sumWeight += leftWeight + rightWeight;
	}

	srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)] = float4(sumColor / sumWeight, 0.0f);
}

[numthreads(NUM_THREADS_MINOR, NUM_THREADS_MAJOR, 1)]
void CS_DepthOfFieldSimplePassYWithHalfBuffer(int2 localDispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadID, DofSimpleBlurSrt srt : S_SRT_DATA)
{
	int2 dispatchThreadId = localDispatchThreadId.xy + srt.m_screenOffset;
	int threadCol = groupThreadId.x;
#if NUM_THREADS_MINOR == 1
		threadCol = 0;
#endif

	float g_sampleWeight[kMaxBlurSamples+1] = {0.985f, 0.95f, 0.9f, 0.825f, 0.77f, 0.7f, 0.625f, 0.5f, 0.43f, 0.35f, 0.28f, 0.2f, 0.1f};

	float2 uv = (dispatchThreadId.xy + 0.5) * srt.m_pData->m_invSize.xy;

	int idx = groupThreadId.y;
	int2 iUv = int2(dispatchThreadId.x, max(dispatchThreadId.y, kMaxBlurSamples) - kMaxBlurSamples);

	SetSrcColorBuffer(threadCol, idx, float4(srt.m_pData->m_srcColor[iUv].xyz,
		saturate(abs(srt.m_pData->m_srcCoc[iUv] * kCocWeight))));
	
	if (idx < (kMaxBlurSamples*2+1))
	{
		iUv = int2(dispatchThreadId.x, min(dispatchThreadId.y+NUM_THREADS_MAJOR-kMaxBlurSamples, srt.m_pData->m_size.y-1));

		SetSrcColorBuffer(threadCol, idx+NUM_THREADS_MAJOR, float4(srt.m_pData->m_srcColor[iUv].xyz,
			saturate(abs(srt.m_pData->m_srcCoc[iUv] * kCocWeight))));
	}
	
	float blurredCoc = srt.m_pData->m_blurredCoc.SampleLevel(srt.m_pData->m_linearSampler, uv, 0).x;
	float blurRadius = min(max(srt.m_pData->m_srcCoc[int2(dispatchThreadId.xy)], blurredCoc), kMaxBlurSamples);

	float fNumSamples = blurRadius;
	int numSamples = int(fNumSamples);

	ThreadGroupMemoryBarrierSync();

	float refWeight = numSamples > 0 ? lerp(g_sampleWeight[numSamples], g_sampleWeight[numSamples+1], frac(fNumSamples)) : 0.0f;
	float4 centerSample = GetSrcColorBuffer(threadCol, idx+kMaxBlurSamples);
	float sumWeight = max((1.0f - refWeight) * (numSamples == 0 ? 1.0f : centerSample.w), 0.01f);
	float3 sumColor = centerSample.xyz * sumWeight;

	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
		{
			float weight = max(g_sampleWeight[i-1] - refWeight, 0);
			float4 upSample = GetSrcColorBuffer(threadCol, idx + kMaxBlurSamples - i);
			float4 downSample = GetSrcColorBuffer(threadCol, idx + kMaxBlurSamples + i);
			float upWeight = weight * upSample.w;
			float downWeight = weight * downSample.w;

			sumColor += upSample.xyz * upWeight + downSample.xyz * downWeight;
			sumWeight += upWeight + downWeight;
		}
	}

	if (dispatchThreadId.y < srt.m_pData->m_size.y)
	{
		srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)] = float4(sumColor / sumWeight, 0.0f);
	}
}

struct DofSimpleBlurSharedData1
{
	RWTexture2D<float4>		m_dstColor;
	RWTexture2D<float2>		m_dstMblurVec;
	Texture2D<float4>		m_srcColor;
	Texture2D<float2>		m_srcMblurVec;
	Texture2D<float>		m_srcCoc;
	Texture2D<float>		m_blurredCoc;

	SamplerState			m_linearSampler;

	int2					m_size;
	float2					m_invSize;

};

struct DofSimpleBlurSrt1
{
	DofSimpleBlurSharedData1	*m_pData;
	int2						m_screenOffset;
};



[numthreads(NUM_THREADS_MAJOR, NUM_THREADS_MINOR, 1)]
void CS_DepthOfFieldSimplePassXWithHalfBuffer1(int2 localDispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadID, DofSimpleBlurSrt1 srt : S_SRT_DATA)
{
	int2 dispatchThreadId = localDispatchThreadId.xy + srt.m_screenOffset;
	int threadRow = groupThreadId.y;

	float g_sampleWeight[kMaxBlurSamples+1] = {0.985f, 0.95f, 0.9f, 0.825f, 0.77f, 0.7f, 0.625f, 0.5f, 0.43f, 0.35f, 0.28f, 0.2f, 0.1f};

	float2 uv = (dispatchThreadId.xy + 0.5) * srt.m_pData->m_invSize.xy;

	int idx = groupThreadId.x;
	int2 iUv = int2(max(dispatchThreadId.x, kMaxBlurSamples) - kMaxBlurSamples, dispatchThreadId.y);

	SetSrcColorBuffer(threadRow, idx, float4(srt.m_pData->m_srcColor[iUv].xyz,
		saturate(abs(srt.m_pData->m_srcCoc[iUv] * kCocWeight))));
	SetMblurVecBuffer(threadRow, idx, srt.m_pData->m_srcMblurVec[iUv]);

	if (idx < (kMaxBlurSamples*2+1))
	{
		iUv = int2(min(dispatchThreadId.x+NUM_THREADS_MAJOR-kMaxBlurSamples, srt.m_pData->m_size.x - 1), dispatchThreadId.y);
		SetSrcColorBuffer(threadRow, idx+NUM_THREADS_MAJOR, float4(srt.m_pData->m_srcColor[iUv].xyz,
			saturate(abs(srt.m_pData->m_srcCoc[iUv] * kCocWeight))));
		SetMblurVecBuffer(threadRow, idx+NUM_THREADS_MAJOR, srt.m_pData->m_srcMblurVec[iUv]);
	}

	float blurredCoc = srt.m_pData->m_blurredCoc.SampleLevel(srt.m_pData->m_linearSampler, uv, 0).x;
	float blurRadius = min(max(srt.m_pData->m_srcCoc[int2(dispatchThreadId.xy)], blurredCoc), kMaxBlurSamples);

	float fNumSamples = blurRadius;
	int numSamples = int(fNumSamples);

	ThreadGroupMemoryBarrierSync();

	float refWeight = numSamples > 0 ? lerp(g_sampleWeight[numSamples-1], g_sampleWeight[numSamples], frac(fNumSamples)) : 0.0f;
	float4 centerSample = GetSrcColorBuffer(threadRow, idx+kMaxBlurSamples);
	float2 centerMblurVec = GetMblurVecBuffer(threadRow, idx+kMaxBlurSamples);
	float sumWeight = max((1.0f - refWeight)  * (numSamples == 0 ? 1.0f : centerSample.w), 0.01f);
	float3 sumColor = centerSample.xyz * sumWeight;
	float2 sumMblurVec = centerMblurVec * sumWeight;

	for (int i = 1; i <= numSamples; i++)
	{
		float weight = max(g_sampleWeight[i - 1] - refWeight, 0);

		float4 leftSample = GetSrcColorBuffer(threadRow, idx + kMaxBlurSamples - i);
		float4 rightSample = GetSrcColorBuffer(threadRow, idx + kMaxBlurSamples + i);
		float2 leftVec = GetMblurVecBuffer(threadRow, idx + kMaxBlurSamples - i);
		float2 rightVec = GetMblurVecBuffer(threadRow, idx + kMaxBlurSamples + i);

		float leftWeight = weight * leftSample.w;
		float rightWeight = weight * rightSample.w;

		sumColor += leftSample.xyz * leftWeight + rightSample.xyz * rightWeight;
		sumMblurVec += leftVec * leftWeight + rightVec * rightWeight;
		sumWeight += leftWeight + rightWeight;
	}

	srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)] = float4(sumColor / sumWeight, 0.0f);
	srt.m_pData->m_dstMblurVec[int2(dispatchThreadId.xy)] = sumMblurVec / sumWeight;
}

[numthreads(NUM_THREADS_MINOR, NUM_THREADS_MAJOR, 1)]
void CS_DepthOfFieldSimplePassYWithHalfBuffer1(int2 localDispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadID, DofSimpleBlurSrt1 srt : S_SRT_DATA)
{
	int2 dispatchThreadId = localDispatchThreadId.xy + srt.m_screenOffset;
	int threadCol = groupThreadId.x;
#if NUM_THREADS_MINOR == 1
		threadCol = 0;
#endif

	float g_sampleWeight[kMaxBlurSamples+1] = {0.985f, 0.95f, 0.9f, 0.825f, 0.77f, 0.7f, 0.625f, 0.5f, 0.43f, 0.35f, 0.28f, 0.2f, 0.1f};

	float2 uv = (dispatchThreadId.xy + 0.5) * srt.m_pData->m_invSize.xy;

	int idx = groupThreadId.y;
	int2 iUv = int2(dispatchThreadId.x, max(dispatchThreadId.y, kMaxBlurSamples) - kMaxBlurSamples);

	SetSrcColorBuffer(threadCol, idx, float4(srt.m_pData->m_srcColor[iUv].xyz,
		saturate(abs(srt.m_pData->m_srcCoc[iUv] * kCocWeight))));

	SetMblurVecBuffer(threadCol, idx, srt.m_pData->m_srcMblurVec[iUv]);
	
	if (idx < (kMaxBlurSamples*2+1))
	{
		iUv = int2(dispatchThreadId.x, min(dispatchThreadId.y+NUM_THREADS_MAJOR-kMaxBlurSamples, srt.m_pData->m_size.y-1));

		SetSrcColorBuffer(threadCol, idx+NUM_THREADS_MAJOR, float4(srt.m_pData->m_srcColor[iUv].xyz,
			saturate(abs(srt.m_pData->m_srcCoc[iUv] * kCocWeight))));
		SetMblurVecBuffer(threadCol, idx+NUM_THREADS_MAJOR, srt.m_pData->m_srcMblurVec[iUv]);
	}
	
	float blurredCoc = srt.m_pData->m_blurredCoc.SampleLevel(srt.m_pData->m_linearSampler, uv, 0).x;
	float blurRadius = min(max(srt.m_pData->m_srcCoc[int2(dispatchThreadId.xy)], blurredCoc), kMaxBlurSamples);

	float fNumSamples = blurRadius;
	int numSamples = int(fNumSamples);

	ThreadGroupMemoryBarrierSync();

	float refWeight = numSamples > 0 ? lerp(g_sampleWeight[numSamples], g_sampleWeight[numSamples+1], frac(fNumSamples)) : 0.0f;
	float4 centerSample = GetSrcColorBuffer(threadCol, idx+kMaxBlurSamples);
	float2 centerMblurVec = GetMblurVecBuffer(threadCol, idx+kMaxBlurSamples);
	float sumWeight = max((1.0f - refWeight) * (numSamples == 0 ? 1.0f : centerSample.w), 0.01f);
	float3 sumColor = centerSample.xyz * sumWeight;
	float2 sumMblurVec = centerMblurVec * sumWeight;

	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
		{
			float weight = max(g_sampleWeight[i-1] - refWeight, 0);
			float4 upSample = GetSrcColorBuffer(threadCol, idx + kMaxBlurSamples - i);
			float4 downSample = GetSrcColorBuffer(threadCol, idx + kMaxBlurSamples + i);
			float2 upVec = GetMblurVecBuffer(threadCol, idx + kMaxBlurSamples - i);
			float2 downVec = GetMblurVecBuffer(threadCol, idx + kMaxBlurSamples + i);
			float upWeight = weight * upSample.w;
			float downWeight = weight * downSample.w;

			sumColor += upSample.xyz * upWeight + downSample.xyz * downWeight;
			sumMblurVec += upVec * upWeight + downVec * downWeight;
			sumWeight += upWeight + downWeight;
		}
	}

	if (dispatchThreadId.y < srt.m_pData->m_size.y)
	{
		srt.m_pData->m_dstColor[int2(dispatchThreadId.xy)] = float4(sumColor / sumWeight, 0.0f);
		srt.m_pData->m_dstMblurVec[int2(dispatchThreadId.xy)] = sumMblurVec / sumWeight;
	}
}

struct DofBlurParams
{
	float4		m_dofBlurVector[3];
};

groupshared float4	g_dofSrcFgBuffer[64 + kMaxBlurSamples + 1];
groupshared float4	g_dofSrcBgBuffer[64 + kMaxBlurSamples + 1];
groupshared float4	g_dofSrcFgBuffer1[64 + kMaxBlurSamples + 1];
groupshared float4	g_dofSrcBgBuffer1[64 + kMaxBlurSamples + 1];

struct DofBlurTextures
{
	RWTexture2D<float4>		rwb_fg_color;
	RWTexture2D<float4>		rwb_bg_color;
	Texture2D<float4>		src_color;
	Texture2D<float>		fg_blur_radius;
	Texture2D<float>		bg_blur_radius;
};

struct DofBlurSamplers
{
	SamplerState samplerPoint;
	SamplerState samplerLinear;	
};

struct DofBlurPassSrt
{
	DofBlurParams				*consts;
	DofBlurTextures				*texs;
	DofBlurSamplers				*smpls;
};

[numthreads(1, 64, 1)]
void CS_BokehDofPass0BlurVector0(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID, DofBlurPassSrt srt : S_SRT_DATA)
{
	int idx = groupThreadId.y;

	float2 pixelUv = dispatchThreadId.xy + float2(0.5f, 0.5f);
	float2 pixelRayUv = float2(pixelUv.x + srt.consts->m_dofBlurVector[0].z * pixelUv.y, pixelUv.y);
	float2 uv = saturate(pixelRayUv * srt.consts->m_dofBlurVector[0].xy);
	float2 uv1 = saturate(uv + 64 * srt.consts->m_dofBlurVector[1].xy);

	float fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, kMaxBlurSamples);
	float bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, 0.0f, kMaxBlurSamples);
	float3 srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv, 0).xyz;

	g_dofSrcFgBuffer[idx] = float4(srcColor * fgBlurRadius, fgBlurRadius);
	g_dofSrcBgBuffer[idx] = float4(srcColor * bgBlurRadius, bgBlurRadius);

	if (idx < kMaxBlurSamples)
	{
		fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, kMaxBlurSamples);
		bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, 0.0f, kMaxBlurSamples);
		srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv1, 0).xyz;

		g_dofSrcFgBuffer[idx+64] = float4(srcColor * fgBlurRadius, fgBlurRadius);
		g_dofSrcBgBuffer[idx+64] = float4(srcColor * bgBlurRadius, bgBlurRadius);
	}

	float4 sumColor = g_dofSrcFgBuffer[idx];
	float fNumSamples = sumColor.w;
	int numSamples = int(fNumSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
			sumColor += g_dofSrcFgBuffer[idx + i];
	}
	srt.texs->rwb_fg_color[int2(dispatchThreadId.xy)] = sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);

	sumColor = g_dofSrcBgBuffer[idx];
	fNumSamples = sumColor.w;
	numSamples = int(fNumSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
			sumColor += g_dofSrcBgBuffer[idx + i];
	}
	srt.texs->rwb_bg_color[int2(dispatchThreadId.xy)] =  sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
}

[numthreads(1, 64, 1)]
void CS_BokehDofPass0BlurVector0Split(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID, DofBlurPassSrt srt : S_SRT_DATA)
{
	int2 screenCoord = int2(dispatchThreadId.x + asint(srt.consts->m_dofBlurVector[0].w), dispatchThreadId.y);
	int idx = groupThreadId.y;

	float splitPos = ((dispatchThreadId.x + 0.5f) * srt.consts->m_dofBlurVector[1].z + srt.consts->m_dofBlurVector[1].w);

	float2 pixelUv = screenCoord + float2(0.5f, 0.5f);
	float2 pixelRayUv = float2(pixelUv.x + srt.consts->m_dofBlurVector[0].z * pixelUv.y, pixelUv.y);
	float2 uv = pixelRayUv * srt.consts->m_dofBlurVector[0].xy;
	float2 uv1 = uv + 64 * srt.consts->m_dofBlurVector[1].xy;

	uv.x = uv.x > 1.0f ? uv.x - 1.0f : uv.x;
	uv1.x = uv1.x > 1.0f ? uv1.x - 1.0f : uv1.x;

	float fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, kMaxBlurSamples);
	float bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, 0.0f, kMaxBlurSamples);
	float3 srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv, 0).xyz;

	g_dofSrcFgBuffer[idx] = float4(srcColor * fgBlurRadius, fgBlurRadius);
	g_dofSrcBgBuffer[idx] = float4(srcColor * bgBlurRadius, bgBlurRadius);

	if (idx < kMaxBlurSamples)
	{
		fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, kMaxBlurSamples);
		bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, 0.0f, kMaxBlurSamples);
		srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv1, 0).xyz;

		g_dofSrcFgBuffer[idx+64] = float4(srcColor * fgBlurRadius, fgBlurRadius);
		g_dofSrcBgBuffer[idx+64] = float4(srcColor * bgBlurRadius, bgBlurRadius);
	}

	int clampedIndex = dispatchThreadId.y >= splitPos ? kMaxBlurSamples : splitPos - dispatchThreadId.y;

	float4 sumColor = g_dofSrcFgBuffer[idx];
	float fNumSamples = sumColor.w;
	int numSamples = min(int(fNumSamples), clampedIndex);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
			sumColor += g_dofSrcFgBuffer[idx + i];
	}
	srt.texs->rwb_fg_color[screenCoord] = sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);

	sumColor = g_dofSrcBgBuffer[idx];
	fNumSamples = sumColor.w;
	numSamples = min(int(fNumSamples), clampedIndex);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
			sumColor += g_dofSrcBgBuffer[idx + i];
	}
	srt.texs->rwb_bg_color[screenCoord] = sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
}

struct DofBlurCombineTextures
{
	RWTexture2D<float4>		rwb_fg_color0;
	RWTexture2D<float4>		rwb_bg_color0;
	RWTexture2D<float4>		rwb_fg_color1;
	RWTexture2D<float4>		rwb_bg_color1;
	Texture2D<float4>		src_color;
	Texture2D<float4>		src_fg_color1;
	Texture2D<float4>		src_bg_color1;
	Texture2D<float>		fg_blur_radius;
	Texture2D<float>		bg_blur_radius;
};

struct DofBlurCombinePassSrt
{
	DofBlurParams				*consts;
	DofBlurCombineTextures		*texs;
	DofBlurSamplers				*smpls;
};

[numthreads(1, 64, 1)]
void CS_BokehDofPass0BlurVector1(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID, DofBlurCombinePassSrt srt : S_SRT_DATA)
{
	int2 screenCoord = int2(dispatchThreadId.x + asint(srt.consts->m_dofBlurVector[0].w), dispatchThreadId.y);
	int idx = groupThreadId.y;

	float2 pixelUv = screenCoord + float2(0.5f, 0.5f);
	float2 pixelRayUv = float2(pixelUv.x + srt.consts->m_dofBlurVector[0].z * pixelUv.y, pixelUv.y);
	float2 uv = saturate(pixelRayUv * srt.consts->m_dofBlurVector[0].xy);
	float2 uv1 = saturate(uv - kMaxBlurSamples * srt.consts->m_dofBlurVector[1].xy);

	float2 uv_0 = float2(uv.x + uv.y * srt.consts->m_dofBlurVector[2].x, uv.y);
	float2 uv1_0 = float2(uv1.x + uv1.y * srt.consts->m_dofBlurVector[2].x, uv1.y);
	uv_0.x = uv_0.x < 0.0f ? uv_0.x + 1.0f : uv_0.x;
	uv1_0.x = uv1_0.x < 0.0f ? uv1_0.x + 1.0f : uv1_0.x;

	float fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, kMaxBlurSamples);
	float bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, 0.0f, kMaxBlurSamples);
	float3 srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv, 0).xyz;

	g_dofSrcFgBuffer[idx+kMaxBlurSamples] = float4(srcColor * fgBlurRadius, fgBlurRadius);
	g_dofSrcBgBuffer[idx+kMaxBlurSamples] = float4(srcColor * bgBlurRadius, bgBlurRadius);
	g_dofSrcFgBuffer1[idx+kMaxBlurSamples] = srt.texs->src_fg_color1.SampleLevel(srt.smpls->samplerLinear, uv_0, 0);
	g_dofSrcBgBuffer1[idx+kMaxBlurSamples] = srt.texs->src_bg_color1.SampleLevel(srt.smpls->samplerLinear, uv_0, 0);

	if (idx < kMaxBlurSamples)
	{
		fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, kMaxBlurSamples);
		bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, 0.0f, kMaxBlurSamples);
		srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv1, 0).xyz;

		g_dofSrcFgBuffer[idx] = float4(srcColor * fgBlurRadius, fgBlurRadius);
		g_dofSrcBgBuffer[idx] = float4(srcColor * bgBlurRadius, bgBlurRadius);
		g_dofSrcFgBuffer1[idx] = srt.texs->src_fg_color1.SampleLevel(srt.smpls->samplerLinear, uv1_0, 0);
		g_dofSrcBgBuffer1[idx] = srt.texs->src_bg_color1.SampleLevel(srt.smpls->samplerLinear, uv1_0, 0);
	}

	idx += kMaxBlurSamples;

	float4 sumColor = g_dofSrcFgBuffer[idx];
	float4 sumColor1 = g_dofSrcFgBuffer1[idx];
	float fNumSamples = sumColor.w;
	int numSamples = int(fNumSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
		{
			sumColor += g_dofSrcFgBuffer[idx - i];
			sumColor1 += g_dofSrcFgBuffer1[idx - i];
		}
	}
	srt.texs->rwb_fg_color0[screenCoord] = sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
	srt.texs->rwb_fg_color1[screenCoord] = sumColor1 * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);

	sumColor = g_dofSrcBgBuffer[idx];
	sumColor1 = g_dofSrcBgBuffer1[idx];
	fNumSamples = sumColor.w;
	numSamples = int(fNumSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
		{
			sumColor += g_dofSrcBgBuffer[idx - i];
			sumColor1 += g_dofSrcBgBuffer1[idx - i];
		}
	}
	srt.texs->rwb_bg_color0[screenCoord] = sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
	srt.texs->rwb_bg_color1[screenCoord] = sumColor1 * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
}

[numthreads(1, 64, 1)]
void CS_BokehDofPass0BlurVector1Split(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID, DofBlurCombinePassSrt srt : S_SRT_DATA)
{
	int2 screenCoord = int2(dispatchThreadId.xy);
	int idx = groupThreadId.y;

	float splitPos = ((dispatchThreadId.x + 0.5f) * srt.consts->m_dofBlurVector[1].z + srt.consts->m_dofBlurVector[1].w);

	float2 pixelUv = screenCoord + float2(0.5f, 0.5f);
	float2 pixelRayUv = float2(pixelUv.x + srt.consts->m_dofBlurVector[0].z * pixelUv.y, pixelUv.y);
	float2 uv = pixelRayUv * srt.consts->m_dofBlurVector[0].xy;
	float2 uv1 = uv - kMaxBlurSamples * srt.consts->m_dofBlurVector[1].xy;

	uv.x = uv.x < 0.0f ? uv.x + 1.0f : uv.x;
	uv1.x = uv1.x < 0.0f ? uv1.x + 1.0f : uv1.x;

	float2 uv_0 = float2(uv.x + uv.y * srt.consts->m_dofBlurVector[2].x, uv.y);
	float2 uv1_0 = float2(uv1.x + uv1.y * srt.consts->m_dofBlurVector[2].x, uv1.y);
	uv_0.x = uv_0.x < 0.0f ? uv_0.x + 1.0f : uv_0.x;
	uv1_0.x = uv1_0.x < 0.0f ? uv1_0.x + 1.0f : uv1_0.x;

	float fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, kMaxBlurSamples);
	float bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, 0.0f, kMaxBlurSamples);
	float3 srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv, 0).xyz;

	g_dofSrcFgBuffer[idx+kMaxBlurSamples] = float4(srcColor * fgBlurRadius, fgBlurRadius);
	g_dofSrcBgBuffer[idx+kMaxBlurSamples] = float4(srcColor * bgBlurRadius, bgBlurRadius);
	g_dofSrcFgBuffer1[idx+kMaxBlurSamples] = srt.texs->src_fg_color1.SampleLevel(srt.smpls->samplerLinear, uv_0, 0);
	g_dofSrcBgBuffer1[idx+kMaxBlurSamples] = srt.texs->src_bg_color1.SampleLevel(srt.smpls->samplerLinear, uv_0, 0);

	if (idx < kMaxBlurSamples)
	{
		fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, kMaxBlurSamples);
		bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, 0.0f, kMaxBlurSamples);
		srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv1, 0).xyz;

		g_dofSrcFgBuffer[idx] = float4(srcColor * fgBlurRadius, fgBlurRadius);
		g_dofSrcBgBuffer[idx] = float4(srcColor * bgBlurRadius, bgBlurRadius);
		g_dofSrcFgBuffer1[idx] = srt.texs->src_fg_color1.SampleLevel(srt.smpls->samplerLinear, uv1_0, 0);
		g_dofSrcBgBuffer1[idx] = srt.texs->src_bg_color1.SampleLevel(srt.smpls->samplerLinear, uv1_0, 0);
	}

	int clampedIndex = dispatchThreadId.y <= splitPos ? kMaxBlurSamples : dispatchThreadId.y - splitPos;

	idx += kMaxBlurSamples;

	float4 sumColor = g_dofSrcFgBuffer[idx];
	float4 sumColor1 = g_dofSrcFgBuffer1[idx];
	float fNumSamples = sumColor.w;
	int numSamples = min(int(fNumSamples), kMaxBlurSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
		{
			sumColor += g_dofSrcFgBuffer[idx - i];
			sumColor1 += g_dofSrcFgBuffer1[idx - i];
		}
	}
	srt.texs->rwb_fg_color0[screenCoord] = sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
	srt.texs->rwb_fg_color1[screenCoord] = sumColor1 * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);

	sumColor = g_dofSrcBgBuffer[idx];
	sumColor1 = g_dofSrcBgBuffer1[idx];
	fNumSamples = sumColor.w;
	numSamples = min(int(fNumSamples), kMaxBlurSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
		{
			sumColor += g_dofSrcBgBuffer[idx - i];
			sumColor1 += g_dofSrcBgBuffer1[idx - i];
		}
	}
	srt.texs->rwb_bg_color0[screenCoord] = sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
	srt.texs->rwb_bg_color1[screenCoord] = sumColor1 * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
}

struct DofBlurMergeTextures
{
	RWTexture2D<float4>		rwb_fg_color;
	Texture2D<float4>		src_fg_color0;
	Texture2D<float4>		src_bg_color0;
	Texture2D<float4>		src_fg_color1;
	Texture2D<float4>		src_bg_color1;
	Texture2D<float4>		src_color2;
	Texture2D<float4>		src_color3;
	Texture2D<float>		fg_blur_radius;
	Texture2D<float>		bg_blur_radius;
};

struct DofBlurMergePassSrt
{
	DofBlurParams				*consts;
	DofBlurMergeTextures		*texs;
	DofBlurSamplers				*smpls;
};

[numthreads(64, 1, 1)]
void CS_BokehDofPass1(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID, DofBlurMergePassSrt srt : S_SRT_DATA)
{
	int2 screenCoord = int2(dispatchThreadId.xy);
	int idx = groupThreadId.x;

	float2 pixelUv = screenCoord + float2(0.5f, 0.5f);
	float2 uv = saturate(pixelUv * srt.consts->m_dofBlurVector[0].xy);
	float2 uv1 = saturate(uv + kMaxBlurSamples * srt.consts->m_dofBlurVector[1].xy);

	float2 uv_0 = float2(uv.x + uv.y * srt.consts->m_dofBlurVector[2].x, uv.y);
	float2 uv1_0 = float2(uv1.x + uv1.y * srt.consts->m_dofBlurVector[2].x, uv1.y);
	uv_0.x = uv_0.x < 0.0f ? uv_0.x + 1.0f : uv_0.x;
	uv1_0.x = uv1_0.x < 0.0f ? uv1_0.x + 1.0f : uv1_0.x;

	float2 uv_1 = float2(uv.x - uv.y * srt.consts->m_dofBlurVector[2].x, uv.y);
	float2 uv1_1 = float2(uv1.x - uv1.y * srt.consts->m_dofBlurVector[2].x, uv1.y);
	uv_1.x = uv_1.x > 1.0f ? uv_1.x - 1.0f : uv_1.x;
	uv1_1.x = uv1_1.x > 1.0f ? uv1_1.x - 1.0f : uv1_1.x;

	float fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, kMaxBlurSamples);
	float bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, 0.0f, kMaxBlurSamples);

	g_dofSrcFgBuffer[idx+kMaxBlurSamples] = srt.texs->src_fg_color0.SampleLevel(srt.smpls->samplerLinear, uv_0, 0) + srt.texs->src_fg_color1.SampleLevel(srt.smpls->samplerLinear, uv_1, 0);
	g_dofSrcBgBuffer[idx+kMaxBlurSamples] = srt.texs->src_bg_color0.SampleLevel(srt.smpls->samplerLinear, uv_0, 0) + srt.texs->src_bg_color1.SampleLevel(srt.smpls->samplerLinear, uv_1, 0);

	if (idx < kMaxBlurSamples)
	{
		g_dofSrcFgBuffer[idx] = srt.texs->src_fg_color0.SampleLevel(srt.smpls->samplerLinear, uv1_0, 0) + srt.texs->src_fg_color1.SampleLevel(srt.smpls->samplerLinear, uv1_1, 0);
		g_dofSrcBgBuffer[idx] = srt.texs->src_bg_color0.SampleLevel(srt.smpls->samplerLinear, uv1_0, 0) + srt.texs->src_bg_color1.SampleLevel(srt.smpls->samplerLinear, uv1_1, 0);
	}

	idx += kMaxBlurSamples;

	float4 sumColor = g_dofSrcFgBuffer[idx];
	float fNumSamples = fgBlurRadius;
	int numSamples = min(int(fNumSamples), kMaxBlurSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
		{
			sumColor += g_dofSrcFgBuffer[idx - i];
		}
	}

	sumColor += g_dofSrcBgBuffer[idx];
	fNumSamples = bgBlurRadius;
	numSamples = min(int(fNumSamples), kMaxBlurSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
		{
			sumColor += g_dofSrcBgBuffer[idx - i];
		}
	}

	sumColor += srt.texs->src_color2.SampleLevel(srt.smpls->samplerLinear, uv_1, 0) + srt.texs->src_color3.SampleLevel(srt.smpls->samplerLinear, uv_1, 0);

	srt.texs->rwb_fg_color[screenCoord] = sumColor / sumColor.w;
}

groupshared float4	g_dofSrcBuffer[64 + kMaxBlurSamples + 1];
groupshared float4	g_dofSrcBuffer1[64 + kMaxBlurSamples + 1];

struct SimpleDofBlurTextures
{
	RWTexture2D<float4>		rwb_color;
	Texture2D<float4>		src_color;
	Texture2D<float>		fg_blur_radius;
	Texture2D<float>		bg_blur_radius;
};

struct SimpleDofBlurPassSrt
{
	DofBlurParams				*consts;
	SimpleDofBlurTextures		*texs;
	DofBlurSamplers				*smpls;
};

[numthreads(1, 64, 1)]
void CS_SimpleBokehDofPass0BlurVector0(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID, SimpleDofBlurPassSrt srt : S_SRT_DATA)
{
	int idx = groupThreadId.y;

	float2 pixelUv = dispatchThreadId.xy + float2(0.5f, 0.5f);
	float2 pixelRayUv = float2(pixelUv.x + srt.consts->m_dofBlurVector[0].z * pixelUv.y, pixelUv.y);
	float2 uv = saturate(pixelRayUv * srt.consts->m_dofBlurVector[0].xy);
	float2 uv1 = saturate(uv + 64 * srt.consts->m_dofBlurVector[1].xy);

	float fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, kMaxBlurSamples);
	float bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, 0.0f, kMaxBlurSamples);
	float blurRadius = max(fgBlurRadius, bgBlurRadius);
	float3 srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv, 0).xyz;

	g_dofSrcBuffer[idx] = float4(srcColor * blurRadius, blurRadius);

	if (idx < kMaxBlurSamples)
	{
		fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, kMaxBlurSamples);
		bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, 0.0f, kMaxBlurSamples);
		blurRadius = max(fgBlurRadius, bgBlurRadius);
		srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv1, 0).xyz;

		g_dofSrcBuffer[idx+64] = float4(srcColor * blurRadius, blurRadius);
	}

	float4 sumColor = g_dofSrcBuffer[idx];
	float fNumSamples = sumColor.w;
	int numSamples = int(fNumSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
			sumColor += g_dofSrcBuffer[idx + i];
	}
	srt.texs->rwb_color[int2(dispatchThreadId.xy)] = sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
}

[numthreads(1, 64, 1)]
void CS_SimpleBokehDofPass0BlurVector0Split(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID, SimpleDofBlurPassSrt srt : S_SRT_DATA)
{
	int2 screenCoord = int2(dispatchThreadId.x + asint(srt.consts->m_dofBlurVector[0].w), dispatchThreadId.y);
	int idx = groupThreadId.y;

	float splitPos = ((dispatchThreadId.x + 0.5f) * srt.consts->m_dofBlurVector[1].z + srt.consts->m_dofBlurVector[1].w);

	float2 pixelUv = screenCoord + float2(0.5f, 0.5f);
	float2 pixelRayUv = float2(pixelUv.x + srt.consts->m_dofBlurVector[0].z * pixelUv.y, pixelUv.y);
	float2 uv = pixelRayUv * srt.consts->m_dofBlurVector[0].xy;
	float2 uv1 = uv + 64 * srt.consts->m_dofBlurVector[1].xy;

	uv.x = uv.x > 1.0f ? uv.x - 1.0f : uv.x;
	uv1.x = uv1.x > 1.0f ? uv1.x - 1.0f : uv1.x;

	float fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, kMaxBlurSamples);
	float bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, 0.0f, kMaxBlurSamples);
	float blurRadius = max(fgBlurRadius, bgBlurRadius);
	float3 srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv, 0).xyz;

	g_dofSrcBuffer[idx] = float4(srcColor * blurRadius, blurRadius);

	if (idx < kMaxBlurSamples)
	{
		fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, kMaxBlurSamples);
		bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, 0.0f, kMaxBlurSamples);
		blurRadius = max(fgBlurRadius, bgBlurRadius);
		srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv1, 0).xyz;

		g_dofSrcBuffer[idx+64] = float4(srcColor * blurRadius, blurRadius);
	}

	int clampedIndex = dispatchThreadId.y >= splitPos ? kMaxBlurSamples : splitPos - dispatchThreadId.y;

	float4 sumColor = g_dofSrcBuffer[idx];
	float fNumSamples = sumColor.w;
	int numSamples = min(int(fNumSamples), clampedIndex);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
			sumColor += g_dofSrcBuffer[idx + i];
	}
	srt.texs->rwb_color[screenCoord] = sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
}

struct SimpleDofBlurCombineTextures
{
	RWTexture2D<float4>		rwb_color0;
	RWTexture2D<float4>		rwb_color1;
	Texture2D<float4>		src_color;
	Texture2D<float4>		src_color1;
	Texture2D<float>		fg_blur_radius;
	Texture2D<float>		bg_blur_radius;
};

struct SimpleDofBlurCombinePassSrt
{
	DofBlurParams					*consts;
	SimpleDofBlurCombineTextures	*texs;
	DofBlurSamplers					*smpls;
};

[numthreads(1, 64, 1)]
void CS_SimpleBokehDofPass0BlurVector1(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID, SimpleDofBlurCombinePassSrt srt : S_SRT_DATA)
{
	int2 screenCoord = int2(dispatchThreadId.x + asint(srt.consts->m_dofBlurVector[0].w), dispatchThreadId.y);
	int idx = groupThreadId.y;

	float2 pixelUv = screenCoord + float2(0.5f, 0.5f);
	float2 pixelRayUv = float2(pixelUv.x + srt.consts->m_dofBlurVector[0].z * pixelUv.y, pixelUv.y);
	float2 uv = saturate(pixelRayUv * srt.consts->m_dofBlurVector[0].xy);
	float2 uv1 = saturate(uv - kMaxBlurSamples * srt.consts->m_dofBlurVector[1].xy);

	float2 uv_0 = float2(uv.x + uv.y * srt.consts->m_dofBlurVector[2].x, uv.y);
	float2 uv1_0 = float2(uv1.x + uv1.y * srt.consts->m_dofBlurVector[2].x, uv1.y);
	uv_0.x = uv_0.x < 0.0f ? uv_0.x + 1.0f : uv_0.x;
	uv1_0.x = uv1_0.x < 0.0f ? uv1_0.x + 1.0f : uv1_0.x;

	float fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, kMaxBlurSamples);
	float bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, 0.0f, kMaxBlurSamples);
	float blurRadius = max(fgBlurRadius, bgBlurRadius);
	float3 srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv, 0).xyz;

	g_dofSrcBuffer[idx+kMaxBlurSamples] = float4(srcColor * blurRadius, blurRadius);
	g_dofSrcBuffer1[idx+kMaxBlurSamples] = srt.texs->src_color1.SampleLevel(srt.smpls->samplerLinear, uv_0, 0);

	if (idx < kMaxBlurSamples)
	{
		fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, kMaxBlurSamples);
		bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, 0.0f, kMaxBlurSamples);
		blurRadius = max(fgBlurRadius, bgBlurRadius);
		srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv1, 0).xyz;

		g_dofSrcBuffer[idx] = float4(srcColor * blurRadius, blurRadius);
		g_dofSrcBuffer1[idx] = srt.texs->src_color1.SampleLevel(srt.smpls->samplerLinear, uv1_0, 0);
	}

	idx += kMaxBlurSamples;

	float4 sumColor = g_dofSrcBuffer[idx];
	float4 sumColor1 = g_dofSrcBuffer1[idx];
	float fNumSamples = sumColor.w;
	int numSamples = int(fNumSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
		{
			sumColor += g_dofSrcBuffer[idx - i];
			sumColor1 += g_dofSrcBuffer1[idx - i];
		}
	}
	srt.texs->rwb_color0[screenCoord] = sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
	srt.texs->rwb_color1[screenCoord] = sumColor1 * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
}

[numthreads(1, 64, 1)]
void CS_SimpleBokehDofPass0BlurVector1Split(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID, SimpleDofBlurCombinePassSrt srt : S_SRT_DATA)
{
	int2 screenCoord = int2(dispatchThreadId.xy);
	int idx = groupThreadId.y;

	float splitPos = ((dispatchThreadId.x + 0.5f) * srt.consts->m_dofBlurVector[1].z + srt.consts->m_dofBlurVector[1].w);

	float2 pixelUv = screenCoord + float2(0.5f, 0.5f);
	float2 pixelRayUv = float2(pixelUv.x + srt.consts->m_dofBlurVector[0].z * pixelUv.y, pixelUv.y);
	float2 uv = pixelRayUv * srt.consts->m_dofBlurVector[0].xy;
	float2 uv1 = uv - kMaxBlurSamples * srt.consts->m_dofBlurVector[1].xy;

	uv.x = uv.x < 0.0f ? uv.x + 1.0f : uv.x;
	uv1.x = uv1.x < 0.0f ? uv1.x + 1.0f : uv1.x;

	float2 uv_0 = float2(uv.x + uv.y * srt.consts->m_dofBlurVector[2].x, uv.y);
	float2 uv1_0 = float2(uv1.x + uv1.y * srt.consts->m_dofBlurVector[2].x, uv1.y);
	uv_0.x = uv_0.x < 0.0f ? uv_0.x + 1.0f : uv_0.x;
	uv1_0.x = uv1_0.x < 0.0f ? uv1_0.x + 1.0f : uv1_0.x;

	float fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, kMaxBlurSamples);
	float bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, 0.0f, kMaxBlurSamples);
	float blurRadius = max(fgBlurRadius, bgBlurRadius);
	float3 srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv, 0).xyz;

	g_dofSrcBuffer[idx+kMaxBlurSamples] = float4(srcColor * blurRadius, blurRadius);
	g_dofSrcBuffer1[idx+kMaxBlurSamples] = srt.texs->src_color1.SampleLevel(srt.smpls->samplerLinear, uv_0, 0);

	if (idx < kMaxBlurSamples)
	{
		fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, kMaxBlurSamples);
		bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv1, 0).x, 0.0f, kMaxBlurSamples);
		blurRadius = max(fgBlurRadius, bgBlurRadius);
		srcColor = srt.texs->src_color.SampleLevel(srt.smpls->samplerLinear, uv1, 0).xyz;

		g_dofSrcBuffer[idx] = float4(srcColor * blurRadius, blurRadius);
		g_dofSrcBuffer1[idx] = srt.texs->src_color1.SampleLevel(srt.smpls->samplerLinear, uv1_0, 0);
	}

	int clampedIndex = dispatchThreadId.y <= splitPos ? kMaxBlurSamples : dispatchThreadId.y - splitPos;

	idx += kMaxBlurSamples;

	float4 sumColor = g_dofSrcBuffer[idx];
	float4 sumColor1 = g_dofSrcBuffer1[idx];
	float fNumSamples = sumColor.w;
	int numSamples = min(int(fNumSamples), kMaxBlurSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
		{
			sumColor += g_dofSrcBuffer[idx - i];
			sumColor1 += g_dofSrcBuffer1[idx - i];
		}
	}
	srt.texs->rwb_color0[screenCoord] = sumColor * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
	srt.texs->rwb_color1[screenCoord] = sumColor1 * (1.0f + kMaxBlurSamples) / (1.0f + numSamples);
}

struct SimpleDofBlurMergeTextures
{
	RWTexture2D<float4>		rwb_color;
	Texture2D<float4>		src_color0;
	Texture2D<float4>		src_color1;
	Texture2D<float4>		src_color2;
	Texture2D<float>		fg_blur_radius;
	Texture2D<float>		bg_blur_radius;
};

struct SimpleDofBlurMergePassSrt
{
	DofBlurParams					*consts;
	SimpleDofBlurMergeTextures		*texs;
	DofBlurSamplers					*smpls;
};

[numthreads(64, 1, 1)]
void CS_SimpleBokehDofPass1(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID, SimpleDofBlurMergePassSrt srt : S_SRT_DATA)
{
	int2 screenCoord = int2(dispatchThreadId.xy);
	int idx = groupThreadId.x;

	float2 pixelUv = screenCoord + float2(0.5f, 0.5f);
	float2 uv = saturate(pixelUv * srt.consts->m_dofBlurVector[0].xy);
	float2 uv1 = saturate(uv + kMaxBlurSamples * srt.consts->m_dofBlurVector[1].xy);

	float2 uv_0 = float2(uv.x + uv.y * srt.consts->m_dofBlurVector[2].x, uv.y);
	float2 uv1_0 = float2(uv1.x + uv1.y * srt.consts->m_dofBlurVector[2].x, uv1.y);
	uv_0.x = uv_0.x < 0.0f ? uv_0.x + 1.0f : uv_0.x;
	uv1_0.x = uv1_0.x < 0.0f ? uv1_0.x + 1.0f : uv1_0.x;

	float2 uv_1 = float2(uv.x - uv.y * srt.consts->m_dofBlurVector[2].x, uv.y);
	float2 uv1_1 = float2(uv1.x - uv1.y * srt.consts->m_dofBlurVector[2].x, uv1.y);
	uv_1.x = uv_1.x > 1.0f ? uv_1.x - 1.0f : uv_1.x;
	uv1_1.x = uv1_1.x > 1.0f ? uv1_1.x - 1.0f : uv1_1.x;

	float fgBlurRadius = min(srt.texs->fg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, kMaxBlurSamples);
	float bgBlurRadius = clamp(srt.texs->bg_blur_radius.SampleLevel(srt.smpls->samplerLinear, uv, 0).x, 0.0f, kMaxBlurSamples);
	float blurRadius = max(fgBlurRadius, bgBlurRadius);

	g_dofSrcBuffer[idx+kMaxBlurSamples] = srt.texs->src_color0.SampleLevel(srt.smpls->samplerLinear, uv_0, 0) + srt.texs->src_color1.SampleLevel(srt.smpls->samplerLinear, uv_1, 0);

	if (idx < kMaxBlurSamples)
	{
		g_dofSrcBuffer[idx] = srt.texs->src_color0.SampleLevel(srt.smpls->samplerLinear, uv1_0, 0) + srt.texs->src_color1.SampleLevel(srt.smpls->samplerLinear, uv1_1, 0);
	}

	idx += kMaxBlurSamples;

	float4 sumColor = g_dofSrcBuffer[idx];
	float fNumSamples = blurRadius;
	int numSamples = min(int(fNumSamples), kMaxBlurSamples);
	if (numSamples > 0)
	{
		for (int i = 1; i <= numSamples; i++)
		{
			sumColor += g_dofSrcBuffer[idx - i];
		}
	}

	sumColor += srt.texs->src_color2.SampleLevel(srt.smpls->samplerLinear, uv_1, 0);

	srt.texs->rwb_color[screenCoord] = sumColor / sumColor.w;
}
