/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#if COMPOSITE_PARTICLE_GBUFFER_EXTENDED
#define USE_GBUFFER_2
#endif

#include "packing.fxi"
#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "global-const-buffers.fxi"
#include "tile-util.fxi"

struct CompositeDownsampleBuffersSrt
{
	float4					m_invBufSize;
	uint4					m_bounds;
	uint4					m_block;
	uint4					m_groupShift;
	RWTexture2D<float4>		m_blendColorBuffer;
	Texture2D<float4>		m_downsampledColorBuffer;
	Texture2D<uint>			m_downsampledCmaskBuffer;
	Texture2D<uint>			m_halfResMergeBuffer;
	SamplerState			m_linearSampler;
};

[NUM_THREADS(8, 8, 1)]
void CS_CompositeDownsampledBuffers(const uint2 dispatchId : SV_DispatchThreadID, 
									const uint2 groupThreadId : SV_GroupThreadID, 
									const uint2 groupId : SV_GroupID, 
									CompositeDownsampleBuffersSrt *pSrt : S_SRT_DATA)
{
	// If we're out of the bounds we want to composite, bail!
	if (dispatchId.x < pSrt->m_block.x  || dispatchId.y < pSrt->m_block.y  ||
		dispatchId.x > pSrt->m_bounds.z || dispatchId.y > pSrt->m_bounds.w)
		return;

	// Get the 8x8 block index, which we can use to get the correct cmask data,
	// calculate the 4x4 quadrant index for the current pixel in the block, then 
	// read the un-tiled and un-packed cmask and check whether or not the bit
	// for our quadrant is set!
	const uint2 group = groupId + pSrt->m_block.zw;
	const uint2 block = group >> pSrt->m_groupShift.xy;
	const uint quadrant = ((group.x >> pSrt->m_groupShift.z) & 1) + (((group.y >> pSrt->m_groupShift.w) & 1) << 1);
	const uint cmask = pSrt->m_downsampledCmaskBuffer[block];

	// If the cmask bit is clear, then we can be sure that none of the pixels in
	// this 4x4 quadrant of the 8x8 block of pixels were touched by any draw call
	// since the cmask was last cleared. Note that the hardware will write the
	// clear color to any of the un-touched pixels in a block the first time any
	// pixel in the block is touched.
	if ((cmask >> quadrant) & 1)
	{
		// We dispatch starting at a whole 8x8 block boundary, so subtract
		// the offset to get the actual position indices
		uint2 pos = dispatchId + pSrt->m_bounds.xy - pSrt->m_block.xy;

		// See Cs_CreateHalfResCompositeInfo
		uint mergeInfo = pSrt->m_halfResMergeBuffer[pos];
		float2 offset = float2((mergeInfo & 0x03) - 1.0f, ((mergeInfo >> 2) & 0x03) - 1.0f);
		float offsetScale = (mergeInfo >> 4) / 15.0f;

		float2 finalOffset = offset * pSrt->m_invBufSize.zw;

		float2 uv = float2((pos + 0.5f) * pSrt->m_invBufSize.xy);
		float2 halfUv = float2((pos * 0.5f + 0.5f) * pSrt->m_invBufSize.zw);
		float2 lerpUv = lerp(uv, halfUv + finalOffset, offsetScale);

		float4 dstColor = pSrt->m_blendColorBuffer[pos];
		float4 srcColor = pSrt->m_downsampledColorBuffer.SampleLevel(pSrt->m_linearSampler, lerpUv, 0);

		pSrt->m_blendColorBuffer[pos] = float4(srcColor.rgb + dstColor.rgb * (1.0f - saturate(srcColor.a)), dstColor.a);
	}
}

struct CompositeDownsampleFogBuffersSrt
{
	float4					m_invBufSize;
	float4					m_screenScaleOffset;
	uint					m_debugUpscaleEdge;
	float					m_volFogScale;
	float					m_maxVolFogCoc;
	float					m_pad;
	float3					m_absorptionCoefficient;
	float					m_skyDist;
	float					m_contrastMin;
	float					m_contrastMax;
	float					m_contrastStart;
	float					m_contrastRange;
	RWTexture2D<float4>		m_blendColorBuffer;
	RWTexture2D<float>		m_fogBlurCocBuffer;
	Texture2D<float>		m_depthBufferVS;
	Texture2D<float4>		m_downsampleColorBuffer;
	Texture2D<float>		m_downsampleColorBuffer1;
	Texture2D<uint>			m_halfResMergeBuffer;
	Texture2D<float4>		m_txSkyFogTex;
	SamplerState			m_linearSampler;
};

float4 CompositeDownsampledFogBuffersS(inout float4 dstColor, out float ratio, uint2 dispatchId, uint2 groupThreadId, uint2 groupId, CompositeDownsampleFogBuffersSrt* pSrt)
{
	float2 uv = float2((dispatchId.xy + 0.5f) * pSrt->m_invBufSize.xy);
	float2 halfUv = float2((dispatchId.xy / 2 + 0.5f) * pSrt->m_invBufSize.zw);

	uint mergeInfo = pSrt->m_halfResMergeBuffer[int2(dispatchId.xy)];

	int2 iFinalOffset = (int2(mergeInfo, mergeInfo >> 2) & 0x03) - 1;

	float2 finalOffset = float2(iFinalOffset) * pSrt->m_invBufSize.zw;

	float depthVS = pSrt->m_depthBufferVS[int2(dispatchId.xy) / 2 + iFinalOffset];

	float3 basePositionVS = float3(((dispatchId.xy + 0.5f) * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f);

	float pixelDepth = length(basePositionVS) * depthVS;

	// Do distance-based contrast
	bool isSky = pixelDepth > pSrt->m_skyDist;

	if(!isSky)
	{
		float3 fadedColor = lerp(float3(pSrt->m_contrastMin), float3(pSrt->m_contrastMax), dstColor.xyz);
		float fadedColorAmount = saturate((pixelDepth - pSrt->m_contrastStart)/pSrt->m_contrastRange);
		dstColor.xyz = lerp(dstColor.xyz, fadedColor, fadedColorAmount);
	}

	// Volumatrics
	ratio = (mergeInfo >> 4) / 15.0f;
	float2 lerpUv = lerp(uv, halfUv + finalOffset, ratio);
	float4 srcColor = pSrt->m_downsampleColorBuffer.SampleLevel(pSrt->m_linearSampler, lerpUv, 0);

	return srcColor;
}

[NUM_THREADS(8, 8, 1)]
void CS_CompositeDownsampledFogBuffers(uint2 dispatchId : SV_DispatchThreadID, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, CompositeDownsampleFogBuffersSrt* pSrt : S_SRT_DATA)
{
	float ratio;
	float4 dstColor = pSrt->m_blendColorBuffer[int2(dispatchId.xy)];
	float4 srcColor = CompositeDownsampledFogBuffersS(dstColor, ratio, dispatchId, groupThreadId, groupId, pSrt);

	float4 dstPlusVolColor = float4(srcColor.xyz + dstColor.xyz * (1.0f - srcColor.w), dstColor.w);

	pSrt->m_blendColorBuffer[int2(dispatchId.xy)] = pSrt->m_debugUpscaleEdge ? (ratio > 0.5f ? float4(1, 0, 0, 1) : float4(0, 0, 0, 0)) : dstPlusVolColor;
}

[NUM_THREADS(8, 8, 1)]
void CS_CompositeDownsampledFogBuffersCreateBlurBuffer(uint2 dispatchId : SV_DispatchThreadID, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, CompositeDownsampleFogBuffersSrt* pSrt : S_SRT_DATA)
{
	float ratio;
	float4 dstColor = pSrt->m_blendColorBuffer[int2(dispatchId.xy)];
	float4 srcColor = CompositeDownsampledFogBuffersS(dstColor, ratio, dispatchId, groupThreadId, groupId, pSrt);

	float4 dstPlusVolColor = float4(srcColor.xyz + dstColor.xyz * (1.0f - srcColor.w), dstColor.w);

	pSrt->m_blendColorBuffer[int2(dispatchId.xy)] = pSrt->m_debugUpscaleEdge ? (ratio > 0.5f ? float4(1, 0, 0, 1) : float4(0, 0, 0, 0)) : dstPlusVolColor;

	pSrt->m_fogBlurCocBuffer[int2(dispatchId.xy)] = min(dot(srcColor.rgb, s_luminanceVector) * pSrt->m_volFogScale, pSrt->m_maxVolFogCoc);
}

float4 CompositeDownsampledUnderWaterFogBuffersS(inout float4 dstColor, out float ratio, out float2 lerpUv, uint2 dispatchId, uint2 groupThreadId, uint2 groupId, CompositeDownsampleFogBuffersSrt* pSrt)
{
	float2 uv = float2((dispatchId.xy + 0.5f) * pSrt->m_invBufSize.xy);
	float2 halfUv = float2((dispatchId.xy / 2 + 0.5f) * pSrt->m_invBufSize.zw);

	uint mergeInfo = pSrt->m_halfResMergeBuffer[int2(dispatchId.xy)];

	int2 iFinalOffset = (int2(mergeInfo, mergeInfo >> 2) & 0x03) - 1;

	float2 finalOffset = float2(iFinalOffset) * pSrt->m_invBufSize.zw;

	float depthVS = pSrt->m_depthBufferVS[int2(dispatchId.xy) / 2 + iFinalOffset];
	float pixelDepth = length(float3((dispatchId.xy * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f)) * depthVS;

// Do distance-based contrast
	bool isSky = pixelDepth > pSrt->m_skyDist;

	if(!isSky)
	{
		float3 fadedColor = lerp(float3(pSrt->m_contrastMin), float3(pSrt->m_contrastMax), dstColor.xyz);
		float fadedColorAmount = saturate((pixelDepth - pSrt->m_contrastStart)/pSrt->m_contrastRange);
		dstColor.xyz = lerp(dstColor.xyz, fadedColor, fadedColorAmount);
	}

	ratio = (mergeInfo >> 4) / 15.0f;
	lerpUv = lerp(uv, halfUv + finalOffset, ratio);
	float4 srcColor = pSrt->m_downsampleColorBuffer.SampleLevel(pSrt->m_linearSampler, lerpUv, 0);

	return srcColor;
}

[NUM_THREADS(8, 8, 1)]
void CS_CompositeDownsampledUnderWaterFogBuffers(uint2 dispatchId : SV_DispatchThreadID, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, CompositeDownsampleFogBuffersSrt* pSrt : S_SRT_DATA)
{
	float ratio;
	float2 lerpUv;
	float4 dstColor = pSrt->m_blendColorBuffer[int2(dispatchId.xy)];
	float4 srcColor = CompositeDownsampledUnderWaterFogBuffersS(dstColor, ratio, lerpUv, dispatchId, groupThreadId, groupId, pSrt);

	float rAbsorption = dstColor.r / (dstColor.r + dstColor.g + dstColor.b);
	float3 biasedAbsorption = float3(lerp(pSrt->m_absorptionCoefficient.g, pSrt->m_absorptionCoefficient.r, rAbsorption), pSrt->m_absorptionCoefficient.g, pSrt->m_absorptionCoefficient.b);

	float  lightDistance = pSrt->m_downsampleColorBuffer1.SampleLevel(pSrt->m_linearSampler, lerpUv, 0);

	float3 absorptionAttenuation = exp(-lightDistance*biasedAbsorption);

	float4 dstPlusVolColor = float4(srcColor.xyz + dstColor.xyz * (1.0f - srcColor.w) * max(absorptionAttenuation, 0.01f), dstColor.w);

	pSrt->m_blendColorBuffer[int2(dispatchId.xy)] = pSrt->m_debugUpscaleEdge ? (ratio > 0.5f ? float4(1, 0, 0, 1) : float4(0, 0, 0, 0)) : dstPlusVolColor;
}

[NUM_THREADS(8, 8, 1)]
void CS_CompositeDownsampledUnderWaterFogBuffersCreateBlurBuffer(uint2 dispatchId : SV_DispatchThreadID, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, CompositeDownsampleFogBuffersSrt* pSrt : S_SRT_DATA)
{
	float ratio;
	float2 lerpUv;
	float4 dstColor = pSrt->m_blendColorBuffer[int2(dispatchId.xy)];
	float4 srcColor = CompositeDownsampledUnderWaterFogBuffersS(dstColor, ratio, lerpUv, dispatchId, groupThreadId, groupId, pSrt);

	float rAbsorption = dstColor.r / (dstColor.r + dstColor.g + dstColor.b);
	float3 biasedAbsorption = float3(lerp(pSrt->m_absorptionCoefficient.g, pSrt->m_absorptionCoefficient.r, rAbsorption), pSrt->m_absorptionCoefficient.g, pSrt->m_absorptionCoefficient.b);

	float  lightDistance = pSrt->m_downsampleColorBuffer1.SampleLevel(pSrt->m_linearSampler, lerpUv, 0);

	float3 absorptionAttenuation = exp(-lightDistance*biasedAbsorption);

	float4 dstPlusVolColor = float4(srcColor.xyz + dstColor.xyz * (1.0f - srcColor.w) * max(absorptionAttenuation, 0.01f), dstColor.w);

	pSrt->m_blendColorBuffer[int2(dispatchId.xy)] = pSrt->m_debugUpscaleEdge ? (ratio > 0.5f ? float4(1, 0, 0, 1) : float4(0, 0, 0, 0)) : dstPlusVolColor;

	pSrt->m_fogBlurCocBuffer[int2(dispatchId.xy)] = min(dot(srcColor.rgb, s_luminanceVector) * pSrt->m_volFogScale, pSrt->m_maxVolFogCoc);
}

struct CompositeDownsampledVolLightsSrt
{
	float4					m_invBufSize;
	RWTexture2D<float4>		m_blendColorBuffer;
	Texture2D<float4>		m_downsampleColorBuffer;
	Texture2D<uint>			m_halfResMergeBuffer;
	SamplerState			m_linearSampler;
};

[NUM_THREADS(8, 8, 1)]
void CS_CompositeDownsampledVolLightsBuffers(uint2 dispatchId : SV_DispatchThreadID, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, CompositeDownsampledVolLightsSrt* pSrt : S_SRT_DATA)
{
	float2 uv = float2((dispatchId.xy + 0.5f) * pSrt->m_invBufSize.xy);
	float2 halfUv = float2((dispatchId.xy / 2 + 0.5f) * pSrt->m_invBufSize.zw);

	uint mergeInfo = pSrt->m_halfResMergeBuffer[int2(dispatchId.xy)];

	int2 iFinalOffset = int2((mergeInfo & 0x03) - 1.0f, ((mergeInfo >> 2) & 0x03) - 1.0f);

	float2 finalOffset = float2(iFinalOffset) * pSrt->m_invBufSize.zw;

	float4 dstColor = pSrt->m_blendColorBuffer[int2(dispatchId.xy)];

	// Volumatrics
	float2 lerpUv = lerp(uv, halfUv + finalOffset, (mergeInfo >> 4) / 15.0f);
	float4 srcColor = pSrt->m_downsampleColorBuffer.SampleLevel(pSrt->m_linearSampler, lerpUv, 0);
	float4 dstPlusVolColor = float4(srcColor.xyz + dstColor.xyz, dstColor.w);

	pSrt->m_blendColorBuffer[int2(dispatchId.xy)] = dstPlusVolColor;
}

void GetMinMaxDepth(inout uint minIdx, inout float minDepthDiff, inout float maxDepthDiff, float sampleDepth, float refDepth, uint sumBits, uint testBit, uint finalOffset)
{
	if (sumBits & testBit)
	{
		float deltaDepthVS = abs(sampleDepth - refDepth);

		if (deltaDepthVS < minDepthDiff)
		{
			minDepthDiff = deltaDepthVS;
			minIdx = finalOffset;
		}

		if (deltaDepthVS > maxDepthDiff)
		{
			maxDepthDiff = deltaDepthVS;
		}
	}
}

struct CompositeDownsampleDsBuffersSrt
{
	float2					m_invBufSize;
	uint2					m_clampDsBufferXy;
	float4					m_vsDepthParams;
	float4					m_edgeDetectParams;
	RWTexture2D<float4>		m_blendColorBuffer;
	Texture2D<float>		m_depthBuffer;
	Texture2D<uint>			m_stencilBuffer;
	Texture2D<float>		m_dsResDepthBuffer;
	Texture2D<uint>			m_dsResStencilBuffer;
	Texture2D<float4>		m_downsampleColorBuffer;
	SamplerState			m_linearSampler;
};

groupshared float		g_downSampleDepth[6][6];
groupshared uint		g_downSampleStencil[6][6];

[NUM_THREADS(8, 8, 1)]
void CS_CompositeDownsampledQuarterBuffersSimple(uint2 dispatchId : SV_DispatchThreadID, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, CompositeDownsampleDsBuffersSrt* pSrt : S_SRT_DATA)
{
	float2 uv = float2((dispatchId.xy + 0.5f) * pSrt->m_invBufSize.xy);
	int2 centerIdx = int2(groupThreadId.xy / 4);

	float2 quarterUv = float2(((dispatchId.xy / 4 * 4) + 2.0f) * pSrt->m_invBufSize.xy);

	float destDepth = pSrt->m_depthBuffer.Load(int3(dispatchId.xy, 0));

	if (groupThreadId.x < 4 && groupThreadId.y < 4)
		g_downSampleDepth[groupThreadId.x][groupThreadId.y] = pSrt->m_dsResDepthBuffer.Load(int3(dispatchId.xy / 4 - 1 + groupThreadId, 0));

	float3 depth0 = float3(g_downSampleDepth[centerIdx.x+0][centerIdx.y+0], g_downSampleDepth[centerIdx.x+1][centerIdx.y+0], g_downSampleDepth[centerIdx.x+2][centerIdx.y+0]);
	float3 depth1 = float3(g_downSampleDepth[centerIdx.x+0][centerIdx.y+1], g_downSampleDepth[centerIdx.x+1][centerIdx.y+1], g_downSampleDepth[centerIdx.x+2][centerIdx.y+1]);
	float3 depth2 = float3(g_downSampleDepth[centerIdx.x+0][centerIdx.y+2], g_downSampleDepth[centerIdx.x+1][centerIdx.y+2], g_downSampleDepth[centerIdx.x+2][centerIdx.y+2]);

	float3 absDepth0 = abs(depth0 - destDepth);
	float3 absDepth1 = abs(depth1 - destDepth);
	float3 absDepth2 = abs(depth2 - destDepth);

	float3 minAbsDepth3;
	minAbsDepth3.x = min3(absDepth0.x, absDepth0.y, absDepth0.z);
	minAbsDepth3.y = min3(absDepth1.x, absDepth1.y, absDepth1.z);
	minAbsDepth3.z = min3(absDepth2.x, absDepth2.y, absDepth2.z);

	float minAbsDepth = min3(minAbsDepth3.x, minAbsDepth3.y, minAbsDepth3.z);

	float3 maxAbsDepth3;
	maxAbsDepth3.x = max3(absDepth0.x, absDepth0.y, absDepth0.z);
	maxAbsDepth3.y = max3(absDepth1.x, absDepth1.y, absDepth1.z);
	maxAbsDepth3.z = max3(absDepth2.x, absDepth2.y, absDepth2.z);

	float maxAbsDepth = max3(maxAbsDepth3.x, maxAbsDepth3.y, maxAbsDepth3.z);

	float vsRefDepth = pSrt->m_vsDepthParams.x / (destDepth - pSrt->m_vsDepthParams.y);

	float maxDepthDiff = (pSrt->m_vsDepthParams.x / (vsRefDepth + pSrt->m_vsDepthParams.z) + pSrt->m_vsDepthParams.y) - destDepth;

	float2 finalOffset = float2(0, 0);
	if      (minAbsDepth == absDepth0.x)
		finalOffset = float2(-1, -1);
	else if (minAbsDepth == absDepth0.y)
		finalOffset = float2(0, -1);
	else if (minAbsDepth == absDepth0.z)
		finalOffset = float2(1, -1);
	else if (minAbsDepth == absDepth1.x)
		finalOffset = float2(-1, 0);
	else if (minAbsDepth == absDepth1.y)
		finalOffset = float2(0, 0);
	else if (minAbsDepth == absDepth1.z)
		finalOffset = float2(1, 0);
	else if (minAbsDepth == absDepth2.x)
		finalOffset = float2(-1, 1);
	else if (minAbsDepth == absDepth2.y)
		finalOffset = float2(0, 1);
	else if (minAbsDepth == absDepth2.z)
		finalOffset = float2(1, 1);

	finalOffset *= (pSrt->m_invBufSize.xy * 4);

	float4 srcColor = pSrt->m_downsampleColorBuffer.SampleLevel(pSrt->m_linearSampler, lerp(uv, quarterUv + finalOffset, saturate(maxAbsDepth / maxDepthDiff)), 0);
	pSrt->m_blendColorBuffer[int2(dispatchId.xy)] = srcColor;
}

[NUM_THREADS(8, 8, 1)]
void CS_CompositeDownsampledQuarterBuffers(int2 dispatchId : SV_DispatchThreadID, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, CompositeDownsampleDsBuffersSrt* pSrt : S_SRT_DATA)
{
	float2 uv = float2((dispatchId.xy + 0.5f) * pSrt->m_invBufSize.xy);
	int2 centerIdx = int2(groupThreadId.xy / 4);

	float2 quarterUv = float2(((dispatchId.xy / 4 * 4) + 2.0f) * pSrt->m_invBufSize.xy);

	float destDepth = pSrt->m_vsDepthParams.x / (pSrt->m_depthBuffer[dispatchId.xy] - pSrt->m_vsDepthParams.y);
	uint destStencil = pSrt->m_stencilBuffer[dispatchId.xy] & 0xe0;

	if (groupThreadId.x < 4 && groupThreadId.y < 4)
	{
		int2 quarterBufferCoord = (int2)clamp(dispatchId.xy / 4 - 1 + groupThreadId, 0.0f, pSrt->m_clampDsBufferXy);
		g_downSampleDepth[groupThreadId.x][groupThreadId.y] = pSrt->m_vsDepthParams.x / (pSrt->m_dsResDepthBuffer[quarterBufferCoord] - pSrt->m_vsDepthParams.y);
		g_downSampleStencil[groupThreadId.x][groupThreadId.y] = pSrt->m_dsResStencilBuffer[quarterBufferCoord] & 0xe0;
	}

	uint sumBits = 0;
	if (g_downSampleStencil[centerIdx.x+0][centerIdx.y+0] == destStencil) sumBits |= 0x001;
	if (g_downSampleStencil[centerIdx.x+1][centerIdx.y+0] == destStencil) sumBits |= 0x002;
	if (g_downSampleStencil[centerIdx.x+2][centerIdx.y+0] == destStencil) sumBits |= 0x004;
	if (g_downSampleStencil[centerIdx.x+0][centerIdx.y+1] == destStencil) sumBits |= 0x008;
	if (g_downSampleStencil[centerIdx.x+1][centerIdx.y+1] == destStencil) sumBits |= 0x010;
	if (g_downSampleStencil[centerIdx.x+2][centerIdx.y+1] == destStencil) sumBits |= 0x020;
	if (g_downSampleStencil[centerIdx.x+0][centerIdx.y+2] == destStencil) sumBits |= 0x040;
	if (g_downSampleStencil[centerIdx.x+1][centerIdx.y+2] == destStencil) sumBits |= 0x080;
	if (g_downSampleStencil[centerIdx.x+2][centerIdx.y+2] == destStencil) sumBits |= 0x100;

	bool bOnEdge = sumBits != 0 && sumBits != 0x1ff;
	sumBits = sumBits == 0 ? 0x1ff : sumBits;

	float minDepthDiff = 100000.0f;
	float maxDepthDiff = 0.0f;
	uint minIdx = 0;

	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+0][centerIdx.y+0], destDepth, sumBits, 0x001, 0x00);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+1][centerIdx.y+0], destDepth, sumBits, 0x002, 0x01);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+2][centerIdx.y+0], destDepth, sumBits, 0x004, 0x02);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+0][centerIdx.y+1], destDepth, sumBits, 0x008, 0x10);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+1][centerIdx.y+1], destDepth, sumBits, 0x010, 0x11);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+2][centerIdx.y+1], destDepth, sumBits, 0x020, 0x12);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+0][centerIdx.y+2], destDepth, sumBits, 0x040, 0x20);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+1][centerIdx.y+2], destDepth, sumBits, 0x080, 0x21);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+2][centerIdx.y+2], destDepth, sumBits, 0x100, 0x22);

	float4 slopError;
	slopError.x = abs((g_downSampleDepth[centerIdx.x+0][centerIdx.y+0] + g_downSampleDepth[centerIdx.x+2][centerIdx.y+2]) - g_downSampleDepth[centerIdx.x+1][centerIdx.y+1] * 2) / 1.1414f;
	slopError.y = abs((g_downSampleDepth[centerIdx.x+1][centerIdx.y+0] + g_downSampleDepth[centerIdx.x+1][centerIdx.y+2]) - g_downSampleDepth[centerIdx.x+1][centerIdx.y+1] * 2);
	slopError.z = abs((g_downSampleDepth[centerIdx.x+2][centerIdx.y+0] + g_downSampleDepth[centerIdx.x+0][centerIdx.y+2]) - g_downSampleDepth[centerIdx.x+1][centerIdx.y+1] * 2) / 1.1414f;
	slopError.w = abs((g_downSampleDepth[centerIdx.x+0][centerIdx.y+1] + g_downSampleDepth[centerIdx.x+2][centerIdx.y+1]) - g_downSampleDepth[centerIdx.x+1][centerIdx.y+1] * 2);

	float finalError = sqrt(dot(slopError, slopError)) / g_downSampleDepth[centerIdx.x+1][centerIdx.y+1];
	float2 finalOffset = ((int2(minIdx, minIdx >> 4) & 0x0f) - 1) * (pSrt->m_invBufSize.xy * 4);

	float lerpFactor = bOnEdge ? 1.0f : (finalError < pSrt->m_edgeDetectParams.z * lerp(1.0f, pSrt->m_edgeDetectParams.y, g_downSampleDepth[centerIdx.x+1][centerIdx.y+1] / pSrt->m_edgeDetectParams.x) ? 0.0f : 1.0f);
	float4 srcColor = pSrt->m_downsampleColorBuffer.SampleLevel(pSrt->m_linearSampler, lerp(uv, quarterUv + finalOffset, lerpFactor), 0);
	pSrt->m_blendColorBuffer[int2(dispatchId.xy)] = (pSrt->m_edgeDetectParams.w > 0 && lerpFactor == 1.0f) ? float4(1, 0, 0, 1) : srcColor;
}

[NUM_THREADS(8, 8, 1)]
void CS_CompositeDownsampledHalfBuffers(uint2 dispatchId : SV_DispatchThreadID, uint2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, CompositeDownsampleDsBuffersSrt* pSrt : S_SRT_DATA)
{
	float2 uv = float2((dispatchId.xy + 0.5f) * pSrt->m_invBufSize.xy);
	int2 centerIdx = int2(groupThreadId.xy / 2);

	float2 halfUv = float2(((dispatchId.xy / 2 * 2) + 1.0f) * pSrt->m_invBufSize.xy);

	float destDepth = pSrt->m_vsDepthParams.x / (pSrt->m_depthBuffer[dispatchId.xy] - pSrt->m_vsDepthParams.y);
	uint destStencil = pSrt->m_stencilBuffer[dispatchId.xy] & 0xe0;

	if (groupThreadId.x < 6 && groupThreadId.y < 6)
	{
		int2 halfBufferCoord = (int2)clamp(groupId.xy * 4 - 1 + groupThreadId, 0.0f, pSrt->m_clampDsBufferXy);
		g_downSampleDepth[groupThreadId.x][groupThreadId.y] = pSrt->m_vsDepthParams.x / (pSrt->m_dsResDepthBuffer[halfBufferCoord] - pSrt->m_vsDepthParams.y);
		g_downSampleStencil[groupThreadId.x][groupThreadId.y] = pSrt->m_dsResStencilBuffer[halfBufferCoord] & 0xe0;
	}

	uint sumBits = 0;
	if (g_downSampleStencil[centerIdx.x+0][centerIdx.y+0] == destStencil) sumBits |= 0x001;
	if (g_downSampleStencil[centerIdx.x+1][centerIdx.y+0] == destStencil) sumBits |= 0x002;
	if (g_downSampleStencil[centerIdx.x+2][centerIdx.y+0] == destStencil) sumBits |= 0x004;
	if (g_downSampleStencil[centerIdx.x+0][centerIdx.y+1] == destStencil) sumBits |= 0x008;
	if (g_downSampleStencil[centerIdx.x+1][centerIdx.y+1] == destStencil) sumBits |= 0x010;
	if (g_downSampleStencil[centerIdx.x+2][centerIdx.y+1] == destStencil) sumBits |= 0x020;
	if (g_downSampleStencil[centerIdx.x+0][centerIdx.y+2] == destStencil) sumBits |= 0x040;
	if (g_downSampleStencil[centerIdx.x+1][centerIdx.y+2] == destStencil) sumBits |= 0x080;
	if (g_downSampleStencil[centerIdx.x+2][centerIdx.y+2] == destStencil) sumBits |= 0x100;

	bool bOnEdge = sumBits != 0 && sumBits != 0x1ff;
	sumBits = sumBits == 0 ? 0x1ff : sumBits;

	float minDepthDiff = 100000.0f;
	float maxDepthDiff = 0.0f;
	uint minIdx = 0;

	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+0][centerIdx.y+0], destDepth, sumBits, 0x001, 0x00);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+1][centerIdx.y+0], destDepth, sumBits, 0x002, 0x01);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+2][centerIdx.y+0], destDepth, sumBits, 0x004, 0x02);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+0][centerIdx.y+1], destDepth, sumBits, 0x008, 0x10);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+1][centerIdx.y+1], destDepth, sumBits, 0x010, 0x11);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+2][centerIdx.y+1], destDepth, sumBits, 0x020, 0x12);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+0][centerIdx.y+2], destDepth, sumBits, 0x040, 0x20);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+1][centerIdx.y+2], destDepth, sumBits, 0x080, 0x21);
	GetMinMaxDepth(minIdx, minDepthDiff, maxDepthDiff, g_downSampleDepth[centerIdx.x+2][centerIdx.y+2], destDepth, sumBits, 0x100, 0x22);

	float4 slopError;
	slopError.x = abs((g_downSampleDepth[centerIdx.x+0][centerIdx.y+0] + g_downSampleDepth[centerIdx.x+2][centerIdx.y+2]) - g_downSampleDepth[centerIdx.x+1][centerIdx.y+1] * 2) / 1.1414f;
	slopError.y = abs((g_downSampleDepth[centerIdx.x+1][centerIdx.y+0] + g_downSampleDepth[centerIdx.x+1][centerIdx.y+2]) - g_downSampleDepth[centerIdx.x+1][centerIdx.y+1] * 2);
	slopError.z = abs((g_downSampleDepth[centerIdx.x+2][centerIdx.y+0] + g_downSampleDepth[centerIdx.x+0][centerIdx.y+2]) - g_downSampleDepth[centerIdx.x+1][centerIdx.y+1] * 2) / 1.1414f;
	slopError.w = abs((g_downSampleDepth[centerIdx.x+0][centerIdx.y+1] + g_downSampleDepth[centerIdx.x+2][centerIdx.y+1]) - g_downSampleDepth[centerIdx.x+1][centerIdx.y+1] * 2);

	float finalError = sqrt(dot(slopError, slopError)) / g_downSampleDepth[centerIdx.x+1][centerIdx.y+1];
	float2 finalOffset = ((int2(minIdx, minIdx >> 4) & 0x0f) - 1) * (pSrt->m_invBufSize.xy * 2);

	float lerpFactor = bOnEdge ? 1.0f : (finalError < pSrt->m_edgeDetectParams.z * lerp(1.0f, pSrt->m_edgeDetectParams.y, g_downSampleDepth[centerIdx.x+1][centerIdx.y+1] / pSrt->m_edgeDetectParams.x) ? 0.0f : 1.0f);
	float4 srcColor = pSrt->m_downsampleColorBuffer.SampleLevel(pSrt->m_linearSampler, lerp(uv, halfUv + finalOffset, lerpFactor), 0);
	pSrt->m_blendColorBuffer[int2(dispatchId.xy)] = (pSrt->m_edgeDetectParams.w > 0 && lerpFactor == 1.0f) ? float4(1, 0, 0, 1) : srcColor;
}

struct CompositeParticleGBufferSrt
{
	RW_Texture2D<uint4>	m_gbuffer0;
	RW_Texture2D<uint4>	m_gbuffer1;
	RW_Texture2D<uint4>	m_gbuffer2;
	RW_Texture2D<uint>	m_materialMaskBuffer;

	Texture2D<float4>	m_particleGBuffer0;
	Texture2D<float4>	m_particleGBuffer1;
	Texture2D<float4>	m_particleGBuffer2;
	Texture2D<uint>		m_particleGBuffer0Cmask;
	Texture2D<uint>		m_particleGBuffer1Cmask;
	Texture2D<uint>		m_particleGBuffer2Cmask;
	uint				m_numTilesX;
};

[NUM_THREADS(8, 8, 1)]
void Cs_CompositeParticleGBuffer(uint2 dispatchId : S_DISPATCH_THREAD_ID,
	uint2 groupThreadId : S_GROUP_THREAD_ID, uint2 groupId : S_GROUP_ID, CompositeParticleGBufferSrt *pSrt : S_SRT_DATA)
{
	// First thing, figure out if the quadrant is clear or not
	const uint cmask0Data = pSrt->m_particleGBuffer0Cmask[groupId];
	const uint cmask1Data = pSrt->m_particleGBuffer1Cmask[groupId];
	const uint cmask2Data = pSrt->m_particleGBuffer2Cmask[groupId];

	const uint quadrantIndex = (groupThreadId.x/4) + (groupThreadId.y/4)*2;
	const uint quadrantBit0 = (cmask0Data >> quadrantIndex) & 0x1;
	const uint quadrantBit1 = (cmask1Data >> quadrantIndex) & 0x1;
	const uint quadrantBit2 = (cmask2Data >> quadrantIndex) & 0x1;

	// We only do anything if the quadrant bit is set
	if (quadrantBit0 | quadrantBit1 | quadrantBit2)
	{
		const uint4 sample0 = pSrt->m_gbuffer0[dispatchId];
		const uint4 sample1 = pSrt->m_gbuffer1[dispatchId];
		const uint4 sample2 = pSrt->m_gbuffer2[dispatchId];
		uint materialMaskSample = pSrt->m_materialMaskBuffer[dispatchId];


		// Load and unpack the actual GBuffer contents
		// We really only care about sample0 - the compiler should optimize away any used parameter unpacking
		BrdfParameters brdfParameters = (BrdfParameters)0;
		Setup setup = (Setup)0;
		UnpackGBuffer(sample0, sample1, brdfParameters, setup);
		
		UnpackMaterialMask(materialMaskSample, sample1, setup.materialMask); // will generate .hasSpecularNormal

		UnpackExtraGBuffer(sample2, setup.materialMask, brdfParameters, setup);

		// Alpha blend all the parameters
		if (quadrantBit0)
		{
			const float4 particleGBuffer0 = pSrt->m_particleGBuffer0[dispatchId];
			setup.baseColorSRGB = setup.baseColorSRGB*saturate(1-particleGBuffer0.a) + particleGBuffer0.rgb;
		}

		float4 particleGBuffer2 = float4(0, 0, 0, 0);
		
		if (quadrantBit2)
		{
			particleGBuffer2 = pSrt->m_particleGBuffer2[dispatchId].xyzw;
		}

		if (quadrantBit1)
		{
			float4 particleGBuffer1 = pSrt->m_particleGBuffer1[dispatchId];
			const float3 particleNormal = particleGBuffer1.xyz;
			
			float3 flatNormal = setup.specularNormalWS; // by default we only care about gbuffer0 normal
			if (quadrantBit2 && particleGBuffer2.y > 0.0f)
			{
				float flatBlend = particleGBuffer2.y;
				if (setup.materialMask.hasSpecularNormal)
				{
					flatNormal = normalize(setup.specularNormalWS*saturate(1 - flatBlend) + setup.normalWS * flatBlend);
				}
				else
				{
					// can't do flat blend since we only have one normal.
					if (particleGBuffer1.a > 0.0f)
					{
						// if we have flat blend enabled and we are writing normals from particle


						// in case specular normal is not enabled, meaning diffuse normal is stored in gbuffer0
						// we want to enable specular normal, write current normal result into gbuffer 0
						// and write source normal into gbuffer2
						
						#if COMPOSITE_PARTICLE_GBUFFER_EXTENDED
						{
							setup.materialMask.hasSpecularNormal = true;

							// we will modify bits ourselves to avoid recomputation of the whole mask by the compiler
							pSrt->m_materialMaskBuffer[dispatchId] = materialMaskSample | MASK_BIT_SPECULAR_NORMAL;
							// manually write the normal, to avoid unpack/pack of the buffer. this will need to be changed in case we change code in packing.fxi
							const float2 encodedDiffuseNormal = EncodeNormal(flatNormal);
							pSrt->m_gbuffer2[dispatchId].y = PackFloat2ToUInt16(encodedDiffuseNormal.x, encodedDiffuseNormal.y);
						}
						#endif
					}

				}

			}

			setup.specularNormalWS = normalize(flatNormal*saturate(1 - particleGBuffer1.a) + particleNormal);
		}
		else if (quadrantBit2 && particleGBuffer2.y > 0.0f)
		{
			// do flat blend logic, in case we didnt write normals (alpha = 0) but we want flat blend we still want to write normals
			float3 flatNormal = setup.specularNormalWS; // by default we only care about gbuffer0 normal
			
			{
				float flatBlend = particleGBuffer2.y;

				flatNormal = normalize(setup.specularNormalWS*saturate(1 - flatBlend) + setup.normalWS * flatBlend);
			}

			setup.specularNormalWS = flatNormal;
		}
		if (quadrantBit2)
		{
			brdfParameters.roughness = brdfParameters.roughness*saturate(1-particleGBuffer2.w) + particleGBuffer2.x;
		}

		// Pack these parameters - again - we don't care about most of the parameters
		uint4 blendedSample0;
		uint4 blendedSample1, unusedSample2;
		uint unusedMask;
		PackGBufferGeneral(brdfParameters, setup, blendedSample0, blendedSample1,
			unusedSample2, unusedMask, 0);

		// Write out the new value
		pSrt->m_gbuffer0[dispatchId] = blendedSample0;
	}

	
}
