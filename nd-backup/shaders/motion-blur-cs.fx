#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "post-processing-common.fxi"

#define kTileSizeX			8
#define kTileSizeY			8
#if (kTileSizeX == 16 && kTileSizeY == 16)
#define kTmpBufferSize		320 * 4
#elif (kTileSizeX == 16 && kTileSizeY == 8)
#define kTmpBufferSize		320 * 2
#else
#define kTmpBufferSize		320
#endif

#define kTileTotalSize		(kTileSizeX * kTileSizeY)
#define kWeightScale		1024.0f

uint GetTileBufferAddress(int2 iTexCoord, uint bufferWidth)
{
	int2 iGroupId = iTexCoord / 8;
	int2 iInnerId = iTexCoord - iGroupId * 8;

	int iInnerIndex = iInnerId.y * 8 + iInnerId.x;
	int iGroupIndex = iGroupId.y * (bufferWidth / 8) + iGroupId.x;

	return (iGroupIndex * 64 + iInnerIndex) * 8;
}

uint Part1by1(uint n)
{
    n &= 0x0000ffff;
    n = (n | (n << 8)) & 0x00FF00FF;
    n = (n | (n << 4)) & 0x0F0F0F0F;
    n = (n | (n << 2)) & 0x33333333;
    n = (n | (n << 1)) & 0x55555555;
    return n;
}


uint Unpart1by1(uint n)
{
   n&= 0x55555555;
   n = (n ^ (n >> 1)) & 0x33333333;
   n = (n ^ (n >> 2)) & 0x0f0f0f0f;
   n = (n ^ (n >> 4)) & 0x00ff00ff;
   n = (n ^ (n >> 8)) & 0x0000ffff;
   return n;
}


uint Morton_2D_Encode(uint x, uint y)
{
   return Part1by1(x) | (Part1by1(y) << 1);
}


void Morton_2D_Decode(uint n, inout uint x, inout uint y)
{
   x = Unpart1by1(n);
   y = Unpart1by1(n >> 1);
}

int2 GetDilateDispatchId(int2 dispatchId, uint iDilateOffset)
{
	int2 dilateOffset = ((int2(iDilateOffset, iDilateOffset >> 2) & 0x03) - 1);
	
	return dispatchId.xy + dilateOffset;
}

uint2 PackHdrMblurSamples(float3 blendSampleColor, float blendWeight)
{
	uint3 uBlendSampleColorLog = uint3(sqrt(blendSampleColor) * kWeightScale);
	uint sampleWeightLog = uint(sqrt(blendWeight) * kWeightScale);

	uint2 curPackRgbWeight;
	curPackRgbWeight.x = uBlendSampleColorLog.x | (uBlendSampleColorLog.y << 16);
	curPackRgbWeight.y = uBlendSampleColorLog.z | (sampleWeightLog << 16);

	return curPackRgbWeight;
}

float4 UnpackHdrMblurSamples(uint2 packedColor)
{
	float4 unpackedColor = float4(packedColor.x & 0xffff, packedColor.x >> 16, packedColor.y & 0xffff, packedColor.y >> 16) / kWeightScale;
	float3 finalColor = unpackedColor.w == 0.0f ? 0.0f : unpackedColor.xyz / unpackedColor.w;
	return float4(finalColor.xyz * finalColor.xyz, unpackedColor.w * unpackedColor.w);
}

struct PreMotionBlurInfoSrt
{
	float4							m_screenScaleOffset;
	int2							m_bufSize;
	int2							m_pixelOffset;
	float4							m_fullSizeInfo;
	int								m_bufferScale;
	float							m_vignetteBlurScale;
	float2							m_mblurTrigerScale;
	float2							m_depthParams;
	float							m_staticTrigger;
	float							m_vignetteBlurCenter;
	float2							m_projectionJitterOffsets;
	float							m_screenYScale;
	float							m_maxBlurRadius;
	float2							m_scopeSSCenter;
	float							m_scopeSSRadius;
	float							m_scopePostProcessBlend;
	float4x4						m_currentToLastFrameMat;
	float4x4						m_currentToFakeLastFrameMat;
	uint							m_frameId;
	uint							m_skipDilate;
	float							m_aspect;
	RWTexture2D<float2>				m_rwDepthVS;
	RWTexture2D<float2>				m_rwMotionVecPixels;
	RWTexture2D<float>				m_rwMotionBlurTileInfo;
	RWTexture2D<float4>				m_rwMotionBlurBbox;
	Texture2D<float>				m_txSrcDepth;
	Texture2D<float>				m_txSrcLastDepth;
	Texture2D<float2>				m_txMotionVector;
	Texture2D<uint>					m_txDilateOffsetBuffer;
	SamplerState					m_linearClampSampler;
};

struct PreMotionBlurInfoMSSrt
{
	float4							m_screenScaleOffset;
	int2							m_bufSize;
	int2							m_pixelOffset;
	float4							m_fullSizeInfo;
	int								m_bufferScale;
	float							m_vignetteBlurScale;
	float2							m_mblurTrigerScale;
	float2							m_depthParams;
	float							m_staticTrigger;
	float							m_vignetteBlurCenter;
	float2							m_projectionJitterOffsets;
	float							m_screenYScale;
	float							m_maxBlurRadius;
	float2							m_scopeSSCenter;
	float							m_scopeSSRadius;
	float							m_scopePostProcessBlend;
	float4x4						m_currentToLastFrameMat;
	float4x4						m_currentToFakeLastFrameMat;
	uint							m_frameId;
	uint							m_skipDilate;
	float							m_aspect;
	RWTexture2D<float2>				m_rwDepthVS;
	RWTexture2D<float2>				m_rwMotionVecPixels;
	RWTexture2D<float>				m_rwMotionBlurTileInfo;
	RWTexture2D<float4>				m_rwMotionBlurBbox;
	Texture2DMS<float, 2>			m_txSrcDepth;
	Texture2DMS<float, 2>			m_txSrcLastDepth;
	Texture2D<float2>				m_txMotionVector;
	Texture2D<uint>					m_txDilateOffsetBuffer;
	SamplerState					m_linearClampSampler;
};

void GetMinMaxBlurInfo(out float maxD, out float2 minBlurRadius, out float2 maxBlurRadius, int2 groupThreadId, float mblurDistScaled, float2 mvectorScaled)
{
	maxD = max(mblurDistScaled, LaneSwizzle(mblurDistScaled, 0x1f, 0x00, 0x01));
	maxD = max(maxD, LaneSwizzle(maxD, 0x1f, 0x00, 0x02));
	maxD = max(maxD, LaneSwizzle(maxD, 0x1f, 0x00, 0x04));
	maxD = max(maxD, LaneSwizzle(maxD, 0x1f, 0x00, 0x08));
	maxD = max(maxD, LaneSwizzle(maxD, 0x1f, 0x00, 0x10));
	maxD = max(maxD, ReadLane(maxD, 0x20));

	float minX = min(groupThreadId.x, groupThreadId.x + mvectorScaled.x);
	float minY = min(groupThreadId.y, groupThreadId.y + mvectorScaled.y);
	float maxX = max(groupThreadId.x, groupThreadId.x + mvectorScaled.x);
	float maxY = max(groupThreadId.y, groupThreadId.y + mvectorScaled.y);

	float minX_01 = min(minX, LaneSwizzle(minX, 0x1f, 0x00, 0x01));
	float minY_01 = min(minY, LaneSwizzle(minY, 0x1f, 0x00, 0x01));
	float maxX_01 = max(maxX, LaneSwizzle(maxX, 0x1f, 0x00, 0x01));
	float maxY_01 = max(maxY, LaneSwizzle(maxY, 0x1f, 0x00, 0x01));

	float minX_02 = min(minX_01, LaneSwizzle(minX_01, 0x1f, 0x00, 0x02));
	float minY_02 = min(minY_01, LaneSwizzle(minY_01, 0x1f, 0x00, 0x02));
	float maxX_02 = max(maxX_01, LaneSwizzle(maxX_01, 0x1f, 0x00, 0x02));
	float maxY_02 = max(maxY_01, LaneSwizzle(maxY_01, 0x1f, 0x00, 0x02));

	float minX_04 = min(minX_02, LaneSwizzle(minX_02, 0x1f, 0x00, 0x04));
	float minY_04 = min(minY_02, LaneSwizzle(minY_02, 0x1f, 0x00, 0x04));
	float maxX_04 = max(maxX_02, LaneSwizzle(maxX_02, 0x1f, 0x00, 0x04));
	float maxY_04 = max(maxY_02, LaneSwizzle(maxY_02, 0x1f, 0x00, 0x04));

	float minX_08 = min(minX_04, LaneSwizzle(minX_04, 0x1f, 0x00, 0x08));
	float minY_08 = min(minY_04, LaneSwizzle(minY_04, 0x1f, 0x00, 0x08));
	float maxX_08 = max(maxX_04, LaneSwizzle(maxX_04, 0x1f, 0x00, 0x08));
	float maxY_08 = max(maxY_04, LaneSwizzle(maxY_04, 0x1f, 0x00, 0x08));

	float minX_10 = min(minX_08, LaneSwizzle(minX_08, 0x1f, 0x00, 0x10));
	float minY_10 = min(minY_08, LaneSwizzle(minY_08, 0x1f, 0x00, 0x10));
	float maxX_10 = max(maxX_08, LaneSwizzle(maxX_08, 0x1f, 0x00, 0x10));
	float maxY_10 = max(maxY_08, LaneSwizzle(maxY_08, 0x1f, 0x00, 0x10));

	minBlurRadius.x = min(minX_10, ReadLane(minX_10, 0x20));
	minBlurRadius.y = min(minY_10, ReadLane(minY_10, 0x20));
	maxBlurRadius.x = max(maxX_10, ReadLane(maxX_10, 0x20));
	maxBlurRadius.y = max(maxY_10, ReadLane(maxY_10, 0x20));
}

[numthreads(8, 8, 1)]
void CS_MotionBlurPrePass(int3 dispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, int groupIndex : SV_GroupIndex, PreMotionBlurInfoSrt* pSrt : S_SRT_DATA)
{
	if (dispatchThreadId.y < pSrt->m_bufSize.y)
	{
		int2 srcDispatchThreadId = int2(dispatchThreadId.x << pSrt->m_bufferScale, dispatchThreadId.y << pSrt->m_bufferScale);
		uint maskBits = pSrt->m_txDilateOffsetBuffer[srcDispatchThreadId.xy / 2];

		uint shiftBits = 0;
		if (pSrt->m_bufferScale == 0)
			shiftBits = ((dispatchThreadId.y & 0x01) * 2 + (dispatchThreadId.x & 0x01)) * 4;

		int2 fsDispatchId = pSrt->m_skipDilate ? srcDispatchThreadId : GetDilateDispatchId(srcDispatchThreadId.xy, maskBits >> shiftBits);

		float srcDepth = pSrt->m_txSrcDepth[fsDispatchId];
		
		float currentDepthVS = GetDepthVS(srcDepth, pSrt->m_depthParams);

		float2 uvSS = float2((fsDispatchId + 0.5f) * pSrt->m_fullSizeInfo.zw);

		float ssX = (uvSS.x - 0.5f) * 2.0f;
		float ssY = (uvSS.y - 0.5f) * -2.0f;

		float4 inputPositionSS = float4(ssX, ssY, srcDepth, 1.0f) * currentDepthVS;
		float4 lastPositionSS = mul(inputPositionSS, pSrt->m_currentToLastFrameMat);

		lastPositionSS.xy = lastPositionSS.xy / lastPositionSS.w * float2(0.5f, -0.5f) + 0.5f;

		float2 motionVectorCalculate = (lastPositionSS.xy - uvSS + pSrt->m_projectionJitterOffsets) * pSrt->m_fullSizeInfo.xy;

		float2 motionVectorSrc = pSrt->m_txMotionVector[fsDispatchId];

		// Do extra effects-driven blur in the corners
		float2 offset = uvSS - 0.5;
		float vignetteBlurScale = pSrt->m_vignetteBlurScale;
		float2 vignetteBlurCenter = pSrt->m_vignetteBlurCenter;
		if (pSrt->m_scopePostProcessBlend > 0.0f)
		{
			float2 aspectCorrectCenter = pSrt->m_scopeSSCenter * float2(pSrt->m_aspect, 1.0f);
			float2 aspectCorrectUV = uvSS * float2(pSrt->m_aspect, 1.0f);
			float2 ssDisp = aspectCorrectUV - aspectCorrectCenter;
			if (length(ssDisp) >= pSrt->m_scopeSSRadius)
			{
				vignetteBlurScale = 10.0;
				vignetteBlurCenter = pSrt->m_scopeSSCenter;
			}
		}
		float2 hackMotionVectorSrc = offset * saturate(length(offset) - vignetteBlurCenter) * vignetteBlurScale;

		float2 motionVectorCorrect = motionVectorSrc * pSrt->m_fullSizeInfo.xy;

		float2 motionDiff = motionVectorCorrect - motionVectorCalculate;

		float lastDepthVS = lastPositionSS.w;
		float2 lastScreenUv = srcDispatchThreadId.xy + motionVectorCorrect;
		if (length(motionDiff) > pSrt->m_staticTrigger && all(lastScreenUv >= 0 && lastScreenUv < pSrt->m_fullSizeInfo.xy))
		{
			float lastDepth = pSrt->m_txSrcLastDepth[int2(lastScreenUv)];
			if (lastDepth < 1.0f)
				lastDepthVS = GetDepthVS(lastDepth, pSrt->m_depthParams);
		}

		float mblurDist = length(float2(motionVectorSrc.x, motionVectorSrc.y * pSrt->m_screenYScale));
		float mblurScale = mblurDist == 0.0f ? 0.0f : clamp((mblurDist - pSrt->m_mblurTrigerScale.x) * pSrt->m_mblurTrigerScale.y, 0.0f, pSrt->m_maxBlurRadius) / mblurDist;
		float2 mblurVectorScaled = (motionVectorSrc * mblurScale + hackMotionVectorSrc) * pSrt->m_bufSize;

		// disable motion blur on outside of scope when zoomed in with rifle
		if (pSrt->m_scopePostProcessBlend > 0.0f)
		{
			float2 aspectCorrectCenter = pSrt->m_scopeSSCenter * float2(pSrt->m_aspect, 1.0f);
			float2 aspectCorrectUV = uvSS * float2(pSrt->m_aspect, 1.0f);
			float2 ssDisp = aspectCorrectUV - aspectCorrectCenter;
			if (length(ssDisp) >= pSrt->m_scopeSSRadius)
			{
				mblurVectorScaled = lerp(mblurVectorScaled, float2(0.0f), pSrt->m_scopePostProcessBlend);
			}
		}

		float lastDepthVSScaled = (lastDepthVS - currentDepthVS) * mblurScale + currentDepthVS;

		float mblurDistScaled = mblurDist * mblurScale;

		pSrt->m_rwDepthVS[dispatchThreadId.xy] = float2(currentDepthVS, srcDepth == 1.0f ? 65535.0f : lastDepthVSScaled);
		pSrt->m_rwMotionVecPixels[dispatchThreadId.xy] = mblurVectorScaled;

		float maxD;
		float2 minBlurRadius;
		float2 maxBlurRadius;
		GetMinMaxBlurInfo(maxD, minBlurRadius, maxBlurRadius, groupThreadId, mblurDistScaled, mblurVectorScaled);

		if (groupIndex == 0)
		{
			pSrt->m_rwMotionBlurTileInfo[groupId.xy] = maxD > 1e-6 ? 1.0f : 0;
			pSrt->m_rwMotionBlurBbox[groupId.xy] = float4(minBlurRadius, maxBlurRadius);
		}
	}
}

void CsMotionBlurPrePassMS(int3 dispatchThreadId, int2 groupId, int2 groupThreadId, int groupIndex, PreMotionBlurInfoMSSrt* pSrt, bool bUseCheckerboardTexture)
{
	if (dispatchThreadId.y < pSrt->m_bufSize.y)
	{
		int2 srcDispatchThreadId = int2(dispatchThreadId.x << pSrt->m_bufferScale, dispatchThreadId.y << pSrt->m_bufferScale);
		uint maskBits = pSrt->m_txDilateOffsetBuffer[srcDispatchThreadId.xy / 2];

		uint shiftBits = 0;
		if (pSrt->m_bufferScale == 0)
			shiftBits = ((dispatchThreadId.y & 0x01) * 2 + (dispatchThreadId.x & 0x01)) * 4;

		int2 fsDispatchId = pSrt->m_skipDilate ? srcDispatchThreadId : GetDilateDispatchId(srcDispatchThreadId.xy, maskBits >> shiftBits);
		int2 location = int2(fsDispatchId.x / 2, fsDispatchId.y);
		int sampleIndex = (fsDispatchId.x + fsDispatchId.y + pSrt->m_frameId) & 1;
		float srcDepth = pSrt->m_txSrcDepth.Load(location, sampleIndex);
		
		float currentDepthVS = GetDepthVS(srcDepth, pSrt->m_depthParams);

		float2 uvSS = float2((fsDispatchId + 0.5f) * pSrt->m_fullSizeInfo.zw);

		float ssX = (uvSS.x - 0.5f) * 2.0f;
		float ssY = (uvSS.y - 0.5f) * -2.0f;

		float4 inputPositionSS = float4(ssX, ssY, srcDepth, 1.0f) * currentDepthVS;
		float4 lastPositionSS = mul(inputPositionSS, pSrt->m_currentToLastFrameMat);

		lastPositionSS.xy = lastPositionSS.xy / lastPositionSS.w * float2(0.5f, -0.5f) + 0.5f;

		float2 motionVectorCalculate = (lastPositionSS.xy - uvSS + pSrt->m_projectionJitterOffsets) * pSrt->m_fullSizeInfo.xy;

		float2 motionVectorSrc = 0;
		if (bUseCheckerboardTexture)
		{
			motionVectorSrc = GetCheckerBoardVector(fsDispatchId, pSrt->m_txMotionVector);
		}
		else
		{
			motionVectorSrc = pSrt->m_txMotionVector[fsDispatchId];
		}

		// Do extra effects-driven blur in the corners
		float2 offset = uvSS - 0.5;
		float vignetteBlurScale = pSrt->m_vignetteBlurScale;
		float2 vignetteBlurCenter = pSrt->m_vignetteBlurCenter;
		if (pSrt->m_scopePostProcessBlend > 0.0f)
		{
			float2 aspectCorrectCenter = pSrt->m_scopeSSCenter * float2(pSrt->m_aspect, 1.0f);
			float2 aspectCorrectUV = uvSS * float2(pSrt->m_aspect, 1.0f);
			float2 ssDisp = aspectCorrectUV - aspectCorrectCenter;
			if (length(ssDisp) >= pSrt->m_scopeSSRadius)
			{
				vignetteBlurScale = 10.0;
				vignetteBlurCenter = pSrt->m_scopeSSCenter;
			}
		}
		float2 hackMotionVectorSrc = offset * saturate(length(offset) - vignetteBlurCenter) * vignetteBlurScale;

		float2 motionVectorCorrect = motionVectorSrc * pSrt->m_fullSizeInfo.xy;

		float2 motionDiff = motionVectorCorrect - motionVectorCalculate;

		float lastDepthVS = lastPositionSS.w;
		float2 lastScreenUv = srcDispatchThreadId.xy + motionVectorCorrect;
		if (length(motionDiff) > pSrt->m_staticTrigger && all(lastScreenUv >= 0 && lastScreenUv < pSrt->m_fullSizeInfo.xy))
		{
			int2 lastLocation = int2(lastScreenUv);
			int lastSampleIndex = (lastLocation.x + lastLocation.y + ~pSrt->m_frameId) & 1;
			float lastDepth = pSrt->m_txSrcLastDepth.Load(int2(lastLocation.x / 2, lastLocation.y), lastSampleIndex);
			if (lastDepth < 1.0f)
				lastDepthVS = GetDepthVS(lastDepth, pSrt->m_depthParams);
		}

		float mblurDist = length(float2(motionVectorSrc.x, motionVectorSrc.y * pSrt->m_screenYScale));
		float mblurScale = mblurDist == 0.0f ? 0.0f : clamp((mblurDist - pSrt->m_mblurTrigerScale.x) * pSrt->m_mblurTrigerScale.y, 0.0f, pSrt->m_maxBlurRadius) / mblurDist;
		float2 mblurVectorScaled = (motionVectorSrc * mblurScale + hackMotionVectorSrc) * pSrt->m_bufSize;

		float lastDepthVSScaled = (lastDepthVS - currentDepthVS) * mblurScale + currentDepthVS;

		float mblurDistScaled = mblurDist * mblurScale;

		pSrt->m_rwDepthVS[dispatchThreadId.xy] = float2(currentDepthVS, srcDepth == 1.0f ? 65535.0f : lastDepthVSScaled);
		pSrt->m_rwMotionVecPixels[dispatchThreadId.xy] = mblurVectorScaled;

		float maxD;
		float2 minBlurRadius;
		float2 maxBlurRadius;
		GetMinMaxBlurInfo(maxD, minBlurRadius, maxBlurRadius, groupThreadId, mblurDistScaled, mblurVectorScaled);

		if (groupIndex == 0)
		{
			pSrt->m_rwMotionBlurTileInfo[groupId.xy] = maxD > 1e-6 ? 1.0f : 0;
			pSrt->m_rwMotionBlurBbox[groupId.xy] = float4(minBlurRadius, maxBlurRadius);
		}
	}
}

[numthreads(8, 8, 1)]
void CS_MotionBlurPrePassMS(int3 dispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, int groupIndex : SV_GroupIndex, PreMotionBlurInfoMSSrt* pSrt : S_SRT_DATA)
{
	CsMotionBlurPrePassMS(dispatchThreadId, groupId, groupThreadId, groupIndex, pSrt, false);
}

[numthreads(8, 8, 1)]
void CS_MotionBlurPrePassMSCheckerBoard(int3 dispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, int groupIndex : SV_GroupIndex, PreMotionBlurInfoMSSrt* pSrt : S_SRT_DATA)
{
	CsMotionBlurPrePassMS(dispatchThreadId, groupId, groupThreadId, groupIndex, pSrt, true);
}

[numthreads(8, 8, 1)]
void CS_MotionBlurPrePassPhotoMode(int3 dispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, int groupIndex : SV_GroupIndex, PreMotionBlurInfoSrt* pSrt : S_SRT_DATA)
{
	if (dispatchThreadId.y < pSrt->m_bufSize.y)
	{
		int2 srcDispatchThreadId = int2(dispatchThreadId.x << pSrt->m_bufferScale, dispatchThreadId.y << pSrt->m_bufferScale);

		int2 fsDispatchId = srcDispatchThreadId;

		float srcDepth = pSrt->m_txSrcDepth[fsDispatchId];
		
		float currentDepthVS = GetDepthVS(srcDepth, pSrt->m_depthParams);

		float2 uvSS = float2((fsDispatchId + 0.5f) * pSrt->m_fullSizeInfo.zw);

		float ssX = (uvSS.x - 0.5f) * 2.0f;
		float ssY = (uvSS.y - 0.5f) * -2.0f;

		float4 inputPositionSS = float4(ssX, ssY, srcDepth, 1.0f) * currentDepthVS;
		float4 lastPositionSS = mul(inputPositionSS, pSrt->m_currentToLastFrameMat);
		float4 fakeLastPositionSS = mul(inputPositionSS, pSrt->m_currentToFakeLastFrameMat);

		if (fakeLastPositionSS.w < 0.02f)
			fakeLastPositionSS = inputPositionSS + (fakeLastPositionSS - inputPositionSS) * (0.01f - inputPositionSS.w) / (fakeLastPositionSS.w - inputPositionSS.w);

		lastPositionSS.xy = lastPositionSS.xy / lastPositionSS.w * float2(0.5f, -0.5f) + 0.5f;
		fakeLastPositionSS.xy = fakeLastPositionSS.xy / fakeLastPositionSS.w * float2(0.5f, -0.5f) + 0.5f;

		float2 motionVectorCalculate = (lastPositionSS.xy - uvSS + pSrt->m_projectionJitterOffsets) * pSrt->m_fullSizeInfo.xy;

		float2 motionVectorSrc = pSrt->m_txMotionVector[fsDispatchId] + (fakeLastPositionSS.w > 0.01f ? (fakeLastPositionSS.xy - uvSS) : 0);

		// Do extra effects-driven blur in the corners
		float2 offset = uvSS - 0.5;
		float vignetteBlurScale = pSrt->m_vignetteBlurScale;
		float2 vignetteBlurCenter = pSrt->m_vignetteBlurCenter;
		if (pSrt->m_scopePostProcessBlend > 0.0f)
		{
			float2 aspectCorrectCenter = pSrt->m_scopeSSCenter * float2(pSrt->m_aspect, 1.0f);
			float2 aspectCorrectUV = uvSS * float2(pSrt->m_aspect, 1.0f);
			float2 ssDisp = aspectCorrectUV - aspectCorrectCenter;
			if (length(ssDisp) >= pSrt->m_scopeSSRadius)
			{
				vignetteBlurScale = 10.0;
				vignetteBlurCenter = pSrt->m_scopeSSCenter;
			}
		}
		motionVectorSrc += offset * saturate(length(offset) - vignetteBlurCenter) * vignetteBlurScale;

		float2 motionVectorCorrect = motionVectorSrc * pSrt->m_fullSizeInfo.xy;

		float2 motionDiff = motionVectorCorrect - motionVectorCalculate;

		float lastDepthVS = lastPositionSS.w;
		float2 lastScreenUv = srcDispatchThreadId.xy + motionVectorCorrect;
		if (length(motionDiff) > pSrt->m_staticTrigger && all(lastScreenUv >= 0 && lastScreenUv < pSrt->m_fullSizeInfo.xy))
		{
			float lastDepth = pSrt->m_txSrcLastDepth[int2(lastScreenUv)];
			if (lastDepth < 1.0f)
				lastDepthVS = GetDepthVS(lastDepth, pSrt->m_depthParams);
		}

		float mblurDist = length(float2(motionVectorSrc.x, motionVectorSrc.y * pSrt->m_screenYScale));
		float mblurScale = mblurDist == 0.0f ? 0.0f : clamp((mblurDist - pSrt->m_mblurTrigerScale.x) * pSrt->m_mblurTrigerScale.y, 0.0f, pSrt->m_maxBlurRadius) / mblurDist;
		float2 mblurVectorScaled = motionVectorSrc * mblurScale * pSrt->m_bufSize;

		float lastDepthVSScaled = (lastDepthVS - currentDepthVS) * mblurScale + currentDepthVS;

		float mblurDistScaled = mblurDist * mblurScale;

		pSrt->m_rwDepthVS[dispatchThreadId.xy] = float2(currentDepthVS, srcDepth == 1.0f ? 65535.0f : lastDepthVSScaled);
		pSrt->m_rwMotionVecPixels[dispatchThreadId.xy] = mblurVectorScaled;

		float maxD;
		float2 minBlurRadius;
		float2 maxBlurRadius;
		GetMinMaxBlurInfo(maxD, minBlurRadius, maxBlurRadius, groupThreadId, mblurDistScaled, mblurVectorScaled);

		if (groupIndex == 0)
		{
			pSrt->m_rwMotionBlurTileInfo[groupId.xy] = maxD > 1e-6 ? 1.0f : 0;
			pSrt->m_rwMotionBlurBbox[groupId.xy] = float4(minBlurRadius, maxBlurRadius);
		}
	}
}

void CsMotionBlurPrePassPhotoModeMS(int3 dispatchThreadId, int2 groupId, int2 groupThreadId, int groupIndex, PreMotionBlurInfoMSSrt* pSrt, bool bUseCheckerboardTexture)
{
	if (dispatchThreadId.y < pSrt->m_bufSize.y)
	{
		int2 srcDispatchThreadId = int2(dispatchThreadId.x << pSrt->m_bufferScale, dispatchThreadId.y << pSrt->m_bufferScale);

		int2 fsDispatchId = srcDispatchThreadId;
		int2 location = int2(fsDispatchId.x / 2, fsDispatchId.y);
		int sampleIndex = (fsDispatchId.x + fsDispatchId.y + pSrt->m_frameId) & 1;
		float srcDepth = pSrt->m_txSrcDepth.Load(location, sampleIndex);
		
		float currentDepthVS = GetDepthVS(srcDepth, pSrt->m_depthParams);

		float2 uvSS = float2((fsDispatchId + 0.5f) * pSrt->m_fullSizeInfo.zw);

		float ssX = (uvSS.x - 0.5f) * 2.0f;
		float ssY = (uvSS.y - 0.5f) * -2.0f;

		float4 inputPositionSS = float4(ssX, ssY, srcDepth, 1.0f) * currentDepthVS;
		float4 lastPositionSS = mul(inputPositionSS, pSrt->m_currentToLastFrameMat);
		float4 fakeLastPositionSS = mul(inputPositionSS, pSrt->m_currentToFakeLastFrameMat);

		if (fakeLastPositionSS.w < 0.02f)
			fakeLastPositionSS = inputPositionSS + (fakeLastPositionSS - inputPositionSS) * (0.01f - inputPositionSS.w) / (fakeLastPositionSS.w - inputPositionSS.w);

		lastPositionSS.xy = lastPositionSS.xy / lastPositionSS.w * float2(0.5f, -0.5f) + 0.5f;
		fakeLastPositionSS.xy = fakeLastPositionSS.xy / fakeLastPositionSS.w * float2(0.5f, -0.5f) + 0.5f;

		float2 motionVectorCalculate = (lastPositionSS.xy - uvSS + pSrt->m_projectionJitterOffsets) * pSrt->m_fullSizeInfo.xy;

		float2 motionVectorSrc = (fakeLastPositionSS.w > 0.01f ? (fakeLastPositionSS.xy - uvSS) : 0);
		if (bUseCheckerboardTexture)
		{
			motionVectorSrc += GetCheckerBoardVector(fsDispatchId, pSrt->m_txMotionVector);
		}
		else
		{
			motionVectorSrc += pSrt->m_txMotionVector[fsDispatchId];
		}

		// Do extra effects-driven blur in the corners
		float2 offset = uvSS - 0.5;
		float vignetteBlurScale = pSrt->m_vignetteBlurScale;
		float2 vignetteBlurCenter = pSrt->m_vignetteBlurCenter;
		if (pSrt->m_scopePostProcessBlend > 0.0f)
		{
			float2 aspectCorrectCenter = pSrt->m_scopeSSCenter * float2(pSrt->m_aspect, 1.0f);
			float2 aspectCorrectUV = uvSS * float2(pSrt->m_aspect, 1.0f);
			float2 ssDisp = aspectCorrectUV - aspectCorrectCenter;
			if (length(ssDisp) >= pSrt->m_scopeSSRadius)
			{
				vignetteBlurScale = 10.0;
				vignetteBlurCenter = pSrt->m_scopeSSCenter;
			}
		}
		motionVectorSrc += offset * saturate(length(offset) - vignetteBlurCenter) * vignetteBlurScale;

		float2 motionVectorCorrect = motionVectorSrc * pSrt->m_fullSizeInfo.xy;

		float2 motionDiff = motionVectorCorrect - motionVectorCalculate;

		float lastDepthVS = lastPositionSS.w;
		float2 lastScreenUv = srcDispatchThreadId.xy + motionVectorCorrect;
		if (length(motionDiff) > pSrt->m_staticTrigger && all(lastScreenUv >= 0 && lastScreenUv < pSrt->m_fullSizeInfo.xy))
		{
			int2 lastLocation = int2(lastScreenUv);
			int lastSampleIndex = (lastLocation.x + lastLocation.y + ~pSrt->m_frameId) & 1;
			float lastDepth = pSrt->m_txSrcLastDepth.Load(int2(lastLocation.x / 2, lastLocation.y), lastSampleIndex);
			if (lastDepth < 1.0f)
				lastDepthVS = GetDepthVS(lastDepth, pSrt->m_depthParams);
		}

		float mblurDist = length(float2(motionVectorSrc.x, motionVectorSrc.y * pSrt->m_screenYScale));
		float mblurScale = mblurDist == 0.0f ? 0.0f : clamp((mblurDist - pSrt->m_mblurTrigerScale.x) * pSrt->m_mblurTrigerScale.y, 0.0f, pSrt->m_maxBlurRadius) / mblurDist;
		float2 mblurVectorScaled = motionVectorSrc * mblurScale * pSrt->m_bufSize;

		float lastDepthVSScaled = (lastDepthVS - currentDepthVS) * mblurScale + currentDepthVS;

		float mblurDistScaled = mblurDist * mblurScale;

		pSrt->m_rwDepthVS[dispatchThreadId.xy] = float2(currentDepthVS, srcDepth == 1.0f ? 65535.0f : lastDepthVSScaled);
		pSrt->m_rwMotionVecPixels[dispatchThreadId.xy] = mblurVectorScaled;

		float maxD;
		float2 minBlurRadius;
		float2 maxBlurRadius;
		GetMinMaxBlurInfo(maxD, minBlurRadius, maxBlurRadius, groupThreadId, mblurDistScaled, mblurVectorScaled);

		if (groupIndex == 0)
		{
			pSrt->m_rwMotionBlurTileInfo[groupId.xy] = maxD > 1e-6 ? 1.0f : 0;
			pSrt->m_rwMotionBlurBbox[groupId.xy] = float4(minBlurRadius, maxBlurRadius);
		}
	}
}

[numthreads(8, 8, 1)]
void CS_MotionBlurPrePassPhotoModeMS(int3 dispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, int groupIndex : SV_GroupIndex, PreMotionBlurInfoMSSrt* pSrt : S_SRT_DATA)
{
	CsMotionBlurPrePassPhotoModeMS(dispatchThreadId, groupId, groupThreadId, groupIndex, pSrt, false);
}

[numthreads(8, 8, 1)]
void CS_MotionBlurPrePassPhotoModeMSCheckerBoard(int3 dispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadId, int groupIndex : SV_GroupIndex, PreMotionBlurInfoMSSrt* pSrt : S_SRT_DATA)
{
	CsMotionBlurPrePassPhotoModeMS(dispatchThreadId, groupId, groupThreadId, groupIndex, pSrt, true);
}

struct ShadowSamplePattern
{
	float		samples[4][4][8];
};

struct MotionBlurScatteringSrt
{
	float4							m_screenScaleOffset;
	uint2							m_bufSize;
	int2							m_pixelOffset;
	uint							m_bufferScale;
	uint							m_motionVectorTest;
	float							m_invDepthFadeRange;
	float							m_depthFadeOffset;
	float2							m_depthParams;
	float							m_pad;
	uint							m_numMaxSamples;
	float							m_ditherSamples[4][4];
	RWByteAddressBuffer				m_rwbAccBuffer;
	Texture2D<float4>				m_srcColor;
	Texture2D<float2>				m_depthVS;
	Texture2D<float2>				m_motionVectorPixels;
	Texture2D<float4>				m_motionVecPixelsBbox;
	ByteAddressBuffer				m_exposureControlBuffer;
	ShadowSamplePattern*			m_pSamplePattern;
};

#define kDepthFadeDist					0.3f
#define kPixelBlurTriggerThreshold		4.0f
#define kPixelDiffTriggerThresold		4.0f
#define kPixelBlurFadeRadius			8.0f
#define kPixelDiffFadeRadius			8.0f

float GetSampleFadeWeight(float thisDepthVS, float smplDepthVS, float2 thisMblurVec, float2 smplMblurVec, float invDepthFadeRange, float depthFadeOffset)
{
	float smplMblurLength = length(smplMblurVec);
	float thisMblurLength = length(thisMblurVec);
	float mblurLengthDiff = abs(smplMblurLength - thisMblurLength);

	float depthWeight = saturate((thisDepthVS - smplDepthVS) * invDepthFadeRange + depthFadeOffset);
	return depthWeight * (smplMblurLength < 1.0f ? 1.0f : saturate(mblurLengthDiff / smplMblurLength));
}

groupshared uint2 g_mblurAccuInfo[kTmpBufferSize];

[numthreads(kTileSizeX, kTileSizeY, 1)]
void CS_MotionBlurScatteringPass(int3 dispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadId, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, MotionBlurScatteringSrt* pSrt : S_SRT_DATA)
{
	if (dispatchThreadId.y < pSrt->m_bufSize.y)
	{
		float tonemapScale = asfloat(pSrt->m_exposureControlBuffer.Load(0));
		int2 srcDispatchThreadId = int2(dispatchThreadId.x << pSrt->m_bufferScale, dispatchThreadId.y << pSrt->m_bufferScale);// + pSrt->m_pixelOffset;

		float2 centerDepthVS = pSrt->m_depthVS[dispatchThreadId];
		float2 mblurVectorPixelScaled = pSrt->m_motionVectorPixels[dispatchThreadId];

		float currentDepthVS = centerDepthVS.x;
		float lastDepthVS = centerDepthVS.y;
		float diffDepthVSScaled = lastDepthVS - currentDepthVS;

		int2 groundStartIdx = groupId.xy * int2(kTileSizeX, kTileSizeY);

#if (kTileSizeX == 16 && kTileSizeY == 16)
		float4 mblurBbox = pSrt->m_motionVecPixelsBbox[groupId * 2 + groupThreadId];
		mblurBbox += groupThreadId.xyxy * 8;

		float minX_01 = min(mblurBbox.x, LaneSwizzle(mblurBbox.x, 0x1f, 0x00, 0x01));
		float minY_01 = min(mblurBbox.y, LaneSwizzle(mblurBbox.y, 0x1f, 0x00, 0x01));
		float maxX_01 = max(mblurBbox.z, LaneSwizzle(mblurBbox.z, 0x1f, 0x00, 0x01));
		float maxY_01 = max(mblurBbox.w, LaneSwizzle(mblurBbox.w, 0x1f, 0x00, 0x01));

		float minX_f = min(minX_01, LaneSwizzle(minX_01, 0x1f, 0x00, 0x08));
		float minY_f = min(minY_01, LaneSwizzle(minY_01, 0x1f, 0x00, 0x08));
		float maxX_f = max(maxX_01, LaneSwizzle(maxX_01, 0x1f, 0x00, 0x08));
		float maxY_f = max(maxY_01, LaneSwizzle(maxY_01, 0x1f, 0x00, 0x08));
#elif (kTileSizeX == 16)
		float4 mblurBbox = pSrt->m_motionVecPixelsBbox[groupId * int2(2, 1) + groupThreadId];
		mblurBbox += groupThreadId.xyxy * 8;

		float minX_f = min(mblurBbox.x, LaneSwizzle(mblurBbox.x, 0x1f, 0x00, 0x01));
		float minY_f = min(mblurBbox.y, LaneSwizzle(mblurBbox.y, 0x1f, 0x00, 0x01));
		float maxX_f = max(mblurBbox.z, LaneSwizzle(mblurBbox.z, 0x1f, 0x00, 0x01));
		float maxY_f = max(mblurBbox.w, LaneSwizzle(mblurBbox.w, 0x1f, 0x00, 0x01));
#else
		float4 mblurBbox = pSrt->m_motionVecPixelsBbox[groupId];
		float minX_f = mblurBbox.x;
		float minY_f = mblurBbox.y;
		float maxX_f = mblurBbox.z;
		float maxY_f = mblurBbox.w;
#endif
		uint iMinX = max(int(ReadLane(minX_f, 0x00)), 0);
		uint iMinY = max(int(ReadLane(minY_f, 0x00)), 0);
		uint iMaxX = min(int(ReadLane(maxX_f, 0x00) + 0.99f), int(pSrt->m_bufSize.x - 1));
		uint iMaxY = min(int(ReadLane(maxY_f, 0x00) + 0.99f), int(pSrt->m_bufSize.y - 1));

		int2 startCoord = int2(ReadLane(iMinX, 0x00), ReadLane(iMinY, 0x00));
		int2 endCoord = int2(ReadLane(iMaxX, 0x00), ReadLane(iMaxY, 0x00));

		int2 sizeCoord = endCoord - startCoord + 1;

		int totalSamples = sizeCoord.x * sizeCoord.y;

		if (totalSamples > kTmpBufferSize)
		{
			float scales = sqrt(float(totalSamples) / kTmpBufferSize);
			sizeCoord = int(sizeCoord * scales + 0.99);
			if (sizeCoord.x < sizeCoord.y)
			{
				sizeCoord.x = max(sizeCoord.x, kTileSizeX);
				sizeCoord.y = kTmpBufferSize / sizeCoord.x;
				startCoord.y = max(startCoord.y, groundStartIdx.y + kTileSizeY - sizeCoord.y);
			}
			else
			{
				sizeCoord.y = max(sizeCoord.y, kTileSizeY);
				sizeCoord.x = kTmpBufferSize / sizeCoord.y;
				startCoord.x = max(startCoord.x, groundStartIdx.x + kTileSizeX - sizeCoord.x);
			}
		}

		uint numItems = sizeCoord.x * sizeCoord.y;

		for (int i = groupIndex; i < numItems; i += kTileTotalSize)
			g_mblurAccuInfo[i] = 0;

		float mblurDist = length(mblurVectorPixelScaled);

		float pixelWeight = 1.0f;

		if (mblurDist > 0.0f)
		{
			float2 absMblurDist = abs(mblurVectorPixelScaled);

			float mblurRange = max(absMblurDist.x, absMblurDist.y);

			float numBlurSamples = min(mblurRange, pSrt->m_numMaxSamples);

			float4 mblurResult = float4(0, 0, 0, 1.0f);
			{
				if (numBlurSamples >= 1.0f)
				{
					pixelWeight = 1.0f / numBlurSamples;
					float lastWeight = 1.0f - pixelWeight * int(numBlurSamples);

					float4 thisSample = pSrt->m_srcColor[srcDispatchThreadId.xy];

					float fullPixelWeight = pixelWeight;
					float lastPixelWeight = lastWeight;

					float2 stepOffset = mblurVectorPixelScaled / numBlurSamples;
					float stepDepthVS = diffDepthVSScaled / numBlurSamples;

					int numSteps = int(numBlurSamples + 0.999f);

					float lerpRatio = saturate((float(numSteps) - float(pSrt->m_numMaxSamples)) / float(pSrt->m_numMaxSamples));
					float randOffsetStart = 1.0f;
					float2 texCoord = dispatchThreadId.xy + stepOffset * randOffsetStart;
					currentDepthVS += stepDepthVS * randOffsetStart;

					for (int i = 1; i < numSteps; i++)
					{
						float curSampleWeight = (i == numSteps-1) ? lastPixelWeight : fullPixelWeight;

						//float sampleRatio = pSrt->pSamplePattern->samples[groupThreadId.x % 4][groupThreadId.y % 4][i % 8];
						int2 iTexCoord = int2(round(texCoord));
						if (all(iTexCoord.xy < pSrt->m_bufSize.xy) && all(iTexCoord.xy > 0))
						{
							int2 localBufCoord = iTexCoord.xy - startCoord;

							float sampleDepthVS = pSrt->m_depthVS[iTexCoord.xy].x;
							float2 sampleMotionVectorPixels = pSrt->m_motionVectorPixels[iTexCoord.xy];

							float fadeWeight = GetSampleFadeWeight(currentDepthVS, sampleDepthVS, mblurVectorPixelScaled, sampleMotionVectorPixels, pSrt->m_invDepthFadeRange, pSrt->m_depthFadeOffset);

							int2 sampleDispatchThreadId = int2(iTexCoord.x << pSrt->m_bufferScale, iTexCoord.y << pSrt->m_bufferScale);

							float3 lerpedSampleColor = lerp(thisSample.xyz, pSrt->m_srcColor[sampleDispatchThreadId.xy].xyz, fadeWeight);

							float3 blendSampleColor = lerpedSampleColor * tonemapScale * curSampleWeight;
							uint2 curPackRgbWeight = PackHdrMblurSamples(blendSampleColor, curSampleWeight);

							if (all(localBufCoord < sizeCoord) && all(localBufCoord >= 0))
							{
								uint localBufIndex = localBufCoord.y * sizeCoord.x + localBufCoord.x;
								uint uOldAccuValue;
								InterlockedAdd(g_mblurAccuInfo[localBufIndex].x, curPackRgbWeight.x, uOldAccuValue);
								InterlockedAdd(g_mblurAccuInfo[localBufIndex].y, curPackRgbWeight.y, uOldAccuValue);
							}
							else
							{
								uint linearBufferAddress = GetTileBufferAddress(iTexCoord, pSrt->m_bufSize.x);
								uint uOldAccuValue;
								if (curPackRgbWeight.x > 0)
									pSrt->m_rwbAccBuffer.InterlockedAdd(linearBufferAddress, curPackRgbWeight.x, uOldAccuValue);
								if (curPackRgbWeight.y > 0)
									pSrt->m_rwbAccBuffer.InterlockedAdd(linearBufferAddress + 4, curPackRgbWeight.y, uOldAccuValue);
							}
						}

						texCoord += stepOffset;
						currentDepthVS += stepDepthVS;
					}
				}
			}
		}

		for (int i = groupIndex; i < numItems; i += kTileTotalSize)
		{
			int y = i / sizeCoord.x;
			int x = i - y * sizeCoord.x;

			int2 sampleThreadIndex = int2(x, y) + startCoord;

			uint linearBufferAddress = GetTileBufferAddress(sampleThreadIndex, pSrt->m_bufSize.x);

			uint uOldAccuValue;
			if (g_mblurAccuInfo[i].x > 0)
				pSrt->m_rwbAccBuffer.InterlockedAdd(linearBufferAddress, g_mblurAccuInfo[i].x, uOldAccuValue);
			if (g_mblurAccuInfo[i].y > 0)
				pSrt->m_rwbAccBuffer.InterlockedAdd(linearBufferAddress + 4, g_mblurAccuInfo[i].y, uOldAccuValue);
		}
	}
}

struct ResolveScatteringBufferSrt
{
	float4							m_screenScaleOffset;
	int2							m_bufSize;
	int2							m_bufSizeFull;
	int2							m_pixelOffset;
	uint							m_bufferScale;
	uint							m_blendLastFrameMethod;
	float2							m_invBufSize;
	float2							m_invBufSizeFull;
	uint							m_numMaxSamples;
	float2							m_mblurTrigerScale;
	float2							m_depthParams;
	uint							m_enableLastFrameBlend;
	uint							m_frameId;
	RWTexture2D<float3>				m_rwbMblur;
	RWTexture2D<float>				m_rwbMblurAlpha;
	ByteAddressBuffer				m_mblurAccBuffer;
	Texture2D<float2>				m_depthVS;
	Texture2D<float2>				m_motionVectorPixels;
	Texture2D<float4>				m_curFrameColor;
	Texture2D<uint>					m_curFrameStencil;
	Texture2D<float4>				m_lastFrameColor;
	Texture2D<float2>				m_lastFrameMotionVector;
//	Texture2D<float>				m_lastFrameDepth;
	Texture2D<uint>					m_lastFrameStencil;
	Texture2D<float2>				m_fullMotionVector;
	ByteAddressBuffer				m_exposureControlBuffer;
	SamplerState					m_pointClampSampler;
	SamplerState					m_linearClampSampler;
};

struct ResolveScatteringBufferMSSrt
{
	float4							m_screenScaleOffset;
	int2							m_bufSize;
	int2							m_bufSizeFull;
	int2							m_pixelOffset;
	uint							m_bufferScale;
	uint							m_blendLastFrameMethod;
	float2							m_invBufSize;
	float2							m_invBufSizeFull;
	uint							m_numMaxSamples;
	float2							m_mblurTrigerScale;
	float2							m_depthParams;
	uint							m_enableLastFrameBlend;
	uint							m_frameId;
	RWTexture2D<float3>				m_rwbMblur;
	RWTexture2D<float>				m_rwbMblurAlpha;
	ByteAddressBuffer				m_mblurAccBuffer;
	Texture2D<float2>				m_depthVS;
	Texture2D<float2>				m_motionVectorPixels;
	Texture2D<float4>				m_curFrameColor;
	Texture2DMS<uint, 2>			m_curFrameStencil;
	Texture2D<float4>				m_lastFrameColor;
	Texture2D<float2>				m_lastFrameMotionVector;
//	Texture2D<float>				m_lastFrameDepth;
	Texture2DMS<uint, 2>			m_lastFrameStencil;
	Texture2D<float2>				m_fullMotionVector;
	ByteAddressBuffer				m_exposureControlBuffer;
	SamplerState					m_pointClampSampler;
	SamplerState					m_linearClampSampler;
};

groupshared float3 g_curFrameMblurWeight[8][8];
groupshared float  g_lastFrameMblurWeight[12][12];

[numthreads(8, 8, 1)]
void CS_ResolveScatteringAccuBufferPass(int3 dispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadId, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, ResolveScatteringBufferSrt* pSrt : S_SRT_DATA)
{
	float invTonemapScale = 1.0f / asfloat(pSrt->m_exposureControlBuffer.Load(0));

	float2 motionVecPixels = pSrt->m_motionVectorPixels[dispatchThreadId.xy];

	float mblurDist = length(motionVecPixels);

	float curPixelWeight = 1.0f;

	if (mblurDist > 0.0f)
	{
		float clamppedMblurRange = min(max(abs(motionVecPixels.x), abs(motionVecPixels.y)), pSrt->m_numMaxSamples);

		if (clamppedMblurRange >= 1.0f)
			curPixelWeight = 1.0f / clamppedMblurRange;
	}

	uint2 packedColor;
	uint linearBufferAddress = GetTileBufferAddress(int2(dispatchThreadId.xy), pSrt->m_bufSize.x);
	packedColor = pSrt->m_mblurAccBuffer.Load2(linearBufferAddress);

	float4 unpackedColor = UnpackHdrMblurSamples(packedColor);
	float3 normalizedColor = unpackedColor.xyz * invTonemapScale;
	float saturateWeight = saturate(unpackedColor.w);

	float bgWeight = 1.0f - saturateWeight;
	float thisFrameWeight = min(curPixelWeight, bgWeight);
	float lastFrameWeight = max(bgWeight - thisFrameWeight, 0.0f);

	if (saturateWeight > 0.5f)
	{
		lastFrameWeight = 0;
		saturateWeight = 1.0f - thisFrameWeight;
	}

	float4 finalMblurInfo = saturateWeight > 0.0f ? float4(normalizedColor * saturateWeight, thisFrameWeight) : float4(0, 0, 0, thisFrameWeight);

	if (lastFrameWeight > 0.0f)
	{
		int2 srcDispatchThreadId = int2(dispatchThreadId.x << pSrt->m_bufferScale, dispatchThreadId.y << pSrt->m_bufferScale);

		float2 baseUv = (srcDispatchThreadId.xy + 0.5f) * pSrt->m_invBufSizeFull;

		float2 absPixelOffset = abs(motionVecPixels);
		int iSampleBlurRadius = int(clamp(max(absPixelOffset.x, absPixelOffset.y), 0.0f, max(pSrt->m_numMaxSamples, 12)));

		float referDepthVS = pSrt->m_depthVS[dispatchThreadId.xy].x;
		uint curStencil = pSrt->m_curFrameStencil[srcDispatchThreadId];

		float2 lastUv = baseUv + pSrt->m_fullMotionVector[srcDispatchThreadId].xy;
		float2 stepSampleOffset = pSrt->m_lastFrameMotionVector.SampleLevel(pSrt->m_pointClampSampler, lastUv, 0) / (iSampleBlurRadius + 1);

		float2 uv = lastUv;

		float sumWeight = 0.0f;
		float3 sumLastColor = 0.0f;

		if (pSrt->m_enableLastFrameBlend)
		{
			for (int i = 0; i < iSampleBlurRadius; i++)
			{
				uv -= stepSampleOffset;

				if (all(uv > 0.0f && uv < 1.0f))
				{
					uint sampleStencil = pSrt->m_lastFrameStencil.SampleLevel(pSrt->m_pointClampSampler, uv, 0);
					uint diffStencil = curStencil ^ sampleStencil;

					if ((diffStencil & 0xE0) == 0)
					{
						sumWeight += 1.0f;
						sumLastColor += pSrt->m_lastFrameColor.SampleLevel(pSrt->m_pointClampSampler, uv, 0).xyz;
					}
				}
			}
		}

		if (sumWeight < 0.5f / iSampleBlurRadius)
		{
			uv = baseUv;
			stepSampleOffset = normalize(motionVecPixels) * pSrt->m_invBufSize;

			for (int i = 0; i < iSampleBlurRadius; i++)
			{
				uv += stepSampleOffset;

				if (all(uv > 0.0f && uv < 1.0f))
				{
					uint sampleStencil = pSrt->m_curFrameStencil.SampleLevel(pSrt->m_pointClampSampler, uv, 0);
					uint diffStencil = curStencil ^ sampleStencil;

					if ((diffStencil & 0xE0) == 0)
					{
						sumWeight += 1.0f;
						sumLastColor += pSrt->m_curFrameColor.SampleLevel(pSrt->m_pointClampSampler, uv, 0).xyz;
					}
				}
			}
		}

		if (sumWeight < 0.5f / iSampleBlurRadius)
		{
			finalMblurInfo.w = 1.0f - saturateWeight;
		}
		else
		{
			float3 normalizedLastColor = sumLastColor / sumWeight;
			finalMblurInfo.xyz += normalizedLastColor * lastFrameWeight;
		}
	}

	pSrt->m_rwbMblur[int2(dispatchThreadId.xy)] = finalMblurInfo.xyz;
	pSrt->m_rwbMblurAlpha[int2(dispatchThreadId.xy)] = finalMblurInfo.w;
}

void CsResolveScatteringAccuBufferPassMS(int3 dispatchThreadId, int2 groupThreadId, int2 groupId, int groupIndex, ResolveScatteringBufferMSSrt* pSrt, bool bUseCheckerboardTexture)
{
	float invTonemapScale = 1.0f / asfloat(pSrt->m_exposureControlBuffer.Load(0));

	float2 motionVecPixels = pSrt->m_motionVectorPixels[dispatchThreadId.xy];

	float mblurDist = length(motionVecPixels);

	float curPixelWeight = 1.0f;

	if (mblurDist > 0.0f)
	{
		float clamppedMblurRange = min(max(abs(motionVecPixels.x), abs(motionVecPixels.y)), pSrt->m_numMaxSamples);

		if (clamppedMblurRange >= 1.0f)
			curPixelWeight = 1.0f / clamppedMblurRange;
	}

	uint2 packedColor;
	uint linearBufferAddress = GetTileBufferAddress(int2(dispatchThreadId.xy), pSrt->m_bufSize.x);
	packedColor = pSrt->m_mblurAccBuffer.Load2(linearBufferAddress);

	float4 unpackedColor = UnpackHdrMblurSamples(packedColor);
	float3 normalizedColor = unpackedColor.xyz * invTonemapScale;
	float saturateWeight = saturate(unpackedColor.w);

	float bgWeight = 1.0f - saturateWeight;
	float thisFrameWeight = min(curPixelWeight, bgWeight);
	float lastFrameWeight = max(bgWeight - thisFrameWeight, 0.0f);

	if (saturateWeight > 0.5f)
	{
		lastFrameWeight = 0;
		saturateWeight = 1.0f - thisFrameWeight;
	}

	float4 finalMblurInfo = saturateWeight > 0.0f ? float4(normalizedColor * saturateWeight, thisFrameWeight) : float4(0, 0, 0, thisFrameWeight);

	if (lastFrameWeight > 0.0f)
	{
		int2 srcDispatchThreadId = int2(dispatchThreadId.x << pSrt->m_bufferScale, dispatchThreadId.y << pSrt->m_bufferScale);

		float2 baseUv = (srcDispatchThreadId.xy + 0.5f) * pSrt->m_invBufSizeFull;

		float2 absPixelOffset = abs(motionVecPixels);
		int iSampleBlurRadius = int(clamp(max(absPixelOffset.x, absPixelOffset.y), 0.0f, max(pSrt->m_numMaxSamples, 12)));

		float referDepthVS = pSrt->m_depthVS[dispatchThreadId.xy].x;
		int2 srcLocation = int2(srcDispatchThreadId.x / 2, srcDispatchThreadId.y);
		int srcSampleIndex = (srcDispatchThreadId.x + srcDispatchThreadId.y + pSrt->m_frameId) & 1;
		uint curStencil = pSrt->m_curFrameStencil.Load(srcLocation, srcSampleIndex);

		float2 lastUv = baseUv;
		if (bUseCheckerboardTexture)
		{
			lastUv += GetCheckerBoardVector(srcDispatchThreadId, pSrt->m_fullMotionVector);
		}
		else
		{
			lastUv += pSrt->m_fullMotionVector[srcDispatchThreadId].xy;
		}

		float2 stepSampleOffset = pSrt->m_lastFrameMotionVector.SampleLevel(pSrt->m_pointClampSampler, lastUv, 0) / (iSampleBlurRadius + 1);

		float2 uv = lastUv;

		float sumWeight = 0.0f;
		float3 sumLastColor = 0.0f;

		if (pSrt->m_enableLastFrameBlend)
		{
			for (int i = 0; i < iSampleBlurRadius; i++)
			{
				uv -= stepSampleOffset;

				if (all(uv > 0.0f && uv < 1.0f))
				{
					int2 location = int2(uv * pSrt->m_bufSizeFull);
					int sampleIndex = (location.x + location.y + ~pSrt->m_frameId) & 1;
					uint sampleStencil = pSrt->m_lastFrameStencil.Load(int2(location.x / 2, location.y), sampleIndex);
					uint diffStencil = curStencil ^ sampleStencil;

					if ((diffStencil & 0xE0) == 0)
					{
						sumWeight += 1.0f;
						sumLastColor += pSrt->m_lastFrameColor.SampleLevel(pSrt->m_pointClampSampler, uv, 0).xyz;
					}
				}
			}
		}

		if (sumWeight < 0.5f / iSampleBlurRadius)
		{
			uv = baseUv;
			stepSampleOffset = normalize(motionVecPixels) * pSrt->m_invBufSize;

			for (int i = 0; i < iSampleBlurRadius; i++)
			{
				uv += stepSampleOffset;

				if (all(uv > 0.0f && uv < 1.0f))
				{
					int2 location = int2(uv * pSrt->m_bufSizeFull);
					int sampleIndex = (location.x + location.y + pSrt->m_frameId) & 1;
					uint sampleStencil = pSrt->m_curFrameStencil.Load(int2(location.x / 2, location.y), sampleIndex);
					uint diffStencil = curStencil ^ sampleStencil;

					if ((diffStencil & 0xE0) == 0)
					{
						sumWeight += 1.0f;
						sumLastColor += pSrt->m_curFrameColor.SampleLevel(pSrt->m_pointClampSampler, uv, 0).xyz;
					}
				}
			}
		}

		if (sumWeight < 0.5f / iSampleBlurRadius)
		{
			finalMblurInfo.w = 1.0f - saturateWeight;
		}
		else
		{
			float3 normalizedLastColor = sumLastColor / sumWeight;
			finalMblurInfo.xyz += normalizedLastColor * lastFrameWeight;
		}
	}

	pSrt->m_rwbMblur[int2(dispatchThreadId.xy)] = finalMblurInfo.xyz;
	pSrt->m_rwbMblurAlpha[int2(dispatchThreadId.xy)] = finalMblurInfo.w;
}

[numthreads(8, 8, 1)]
void CS_ResolveScatteringAccuBufferPassMS(int3 dispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadId, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, ResolveScatteringBufferMSSrt* pSrt : S_SRT_DATA)
{
	CsResolveScatteringAccuBufferPassMS(dispatchThreadId, groupThreadId, groupId, groupIndex, pSrt, false);
}

[numthreads(8, 8, 1)]
void CS_ResolveScatteringAccuBufferPassMSCheckerBoard(int3 dispatchThreadId : SV_DispatchThreadId, int2 groupThreadId : SV_GroupThreadId, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, ResolveScatteringBufferMSSrt* pSrt : S_SRT_DATA)
{
	CsResolveScatteringAccuBufferPassMS(dispatchThreadId, groupThreadId, groupId, groupIndex, pSrt, true);
}

float GetBlurSampleFadeWeight(float smplDepthVS, float centerDepthVS, float2 smplMblur, float2 curMblur, float invDepthFadeRange, float depthFadeOffset)
{
	float smplMblurDist = length(smplMblur);
	float curMblurDist = length(curMblur);
	float mblurDist = abs(smplMblurDist - curMblurDist);

	float depthWeight = saturate((smplDepthVS - centerDepthVS) * invDepthFadeRange + depthFadeOffset);
	return depthWeight;// * (curMblurDist < 1.0f ? 1.0f : saturate(mblurDist / curMblurDist));
}

struct MblurBufferBlurSrt
{
	float						m_blurDirection;
	float						m_blurDepthWeightRatio;
	float2						m_pad;
	float2						m_invBufferSize;
	float						m_invDepthFadeRange;
	float						m_depthFadeOffset;
	RWTexture2D<float3>			m_rwbMblurBlurredBuffer;
	RWTexture2D<float>			m_rwbMblurBlurredAlphaBuffer;
	Texture2D<float3>			m_txBlurSourceBuffer;
	Texture2D<float>			m_txBlurSourceAlphaBuffer;
	Texture2D<float2>			m_txMblurVectorPixel;
	Texture2D<float>			m_txMblurTileInfo;
	Texture2D<float2>			m_depthVS;
	SamplerState				m_linearClampSampler;
};

[numthreads(8, 8, 1)]
void CS_BlurMblurBuffer(int2 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, MblurBufferBlurSrt* pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId + 0.5f) * pSrt->m_invBufferSize;
	float tileInfo = pSrt->m_txMblurTileInfo.SampleLevel(pSrt->m_linearClampSampler, uv, 0);
	float2 motionVectorPixel = pSrt->m_txMblurVectorPixel.SampleLevel(pSrt->m_linearClampSampler, uv, 0);
	float4 sumColor;
	sumColor.xyz = pSrt->m_txBlurSourceBuffer[dispatchThreadId];
	sumColor.w = pSrt->m_txBlurSourceAlphaBuffer[dispatchThreadId];
	float centerDepthVS = pSrt->m_depthVS[dispatchThreadId].x;
	float sumWeight = 1.0f;

	bool hasMblur = length(motionVectorPixel) > 0.8f;

	if (!hasMblur && tileInfo > 0.001f) // no motion blur, but there is montion blur in nearby tile. 
	{
		float2 sumMotionVectorPixel = 0;
		float radiusPixels = 3.0f;
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
		for (int i = 0; i < 12; i++)
			sumMotionVectorPixel += pSrt->m_txMblurVectorPixel.SampleLevel(pSrt->m_linearClampSampler, uv + radiusPixels * possionPattern[i] * pSrt->m_invBufferSize, 0);

		if (length(sumMotionVectorPixel) > 0.8f)
		{
			motionVectorPixel = sumMotionVectorPixel;
			hasMblur = true;
		}
	}

	if (hasMblur)
	{
		float2 blurDir = normalize(motionVectorPixel);
		float2 sampleBlurDir = blurDir * pSrt->m_blurDirection;

		for (int i = 0; i < 6; i++)
		{
			float2 uv0 = uv + (i-6) * blurDir * pSrt->m_invBufferSize.xy;
			float2 uv1 = uv + (i+1) * blurDir * pSrt->m_invBufferSize.xy;
			float4 srcColor0;
			srcColor0.xyz = pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_linearClampSampler, uv0, 0);
			srcColor0.w = pSrt->m_txBlurSourceAlphaBuffer.SampleLevel(pSrt->m_linearClampSampler, uv0, 0);
			float depthVS0 = pSrt->m_depthVS.SampleLevel(pSrt->m_linearClampSampler, uv0, 0).x;
			float2 motionVectorPixel0 = pSrt->m_txMblurVectorPixel.SampleLevel(pSrt->m_linearClampSampler, uv0, 0);
			float4 srcColor1;
			srcColor1.xyz = pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_linearClampSampler, uv1, 0);
			srcColor1.w = pSrt->m_txBlurSourceAlphaBuffer.SampleLevel(pSrt->m_linearClampSampler, uv1, 0);
			float depthVS1 = pSrt->m_depthVS.SampleLevel(pSrt->m_linearClampSampler, uv1, 0).x;
			float2 motionVectorPixel1 = pSrt->m_txMblurVectorPixel.SampleLevel(pSrt->m_linearClampSampler, uv1, 0);

			float depthWeight = 0.0f;
			{
				float angleWeight0 = saturate(dot(sampleBlurDir, normalize(motionVectorPixel0))) * saturate((length(motionVectorPixel0) / (i + 1) - 0.9f) / 0.2f);
				depthWeight = 1.0f - GetBlurSampleFadeWeight(depthVS0, centerDepthVS, motionVectorPixel0, motionVectorPixel, pSrt->m_invDepthFadeRange, pSrt->m_depthFadeOffset) * pSrt->m_blurDepthWeightRatio;
				depthWeight *= angleWeight0;
				sumColor += srcColor0 * depthWeight;
				sumWeight += depthWeight;
			}

			{
				float angleWeight1 = saturate(dot(-sampleBlurDir, normalize(motionVectorPixel1))) * saturate((length(motionVectorPixel1) / (i + 1) - 0.9f) / 0.2f);
				depthWeight = 1.0f - GetBlurSampleFadeWeight(depthVS1, centerDepthVS, motionVectorPixel1, motionVectorPixel, pSrt->m_invDepthFadeRange, pSrt->m_depthFadeOffset) * pSrt->m_blurDepthWeightRatio;
				depthWeight *= angleWeight1;
				sumColor += srcColor1 * depthWeight;
				sumWeight += depthWeight;
			}
		}
	}

	float4 mblurInfo = sumColor / sumWeight;
	pSrt->m_rwbMblurBlurredBuffer[dispatchThreadId.xy] = mblurInfo.xyz;
	pSrt->m_rwbMblurBlurredAlphaBuffer[dispatchThreadId.xy] = mblurInfo.w;
}

struct MblurCompositeSrt
{
	uint						m_halfResolution;
	uint						m_pad;
	float2						m_pixelSize;
	RWTexture2D<float4>			m_rwbMergedBuffer;
	Texture2D<float4>			m_txBlurSourceBuffer;
	Texture2D<float4>			m_motionBlurColorInfo;
	Texture2D<uint>				m_dsDepthInfo;
	SamplerState				m_linearClampSampler;
};

[numthreads(8, 8, 1)]
void CS_MblurComposite(int2 dispatchThreadId : SV_DispatchThreadId, MblurCompositeSrt* pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId + 0.5f) * pSrt->m_pixelSize;
	float2 finalUv = uv;
	if (pSrt->m_halfResolution)
	{
		uint mergeInfo = pSrt->m_dsDepthInfo[dispatchThreadId.xy];

		float2 halfUv = ((dispatchThreadId.xy / 2 + (int2(mergeInfo, mergeInfo >> 2) & 0x03)) - 0.5f) * 2 * pSrt->m_pixelSize;
		
		float  lerpRatio = (mergeInfo >> 4) / 4095.0f;

/*		float4 mblurValueAlpha = pSrt->m_motionBlurColorInfo.GatherAlpha(srt.m_pData->m_linearClampSampler, uv0 + 0.5f * pixelSize, 0);

		float maxAlpha = max(max3(mblurValueAlpha.x, mblurValueAlpha.y, mblurValueAlpha.z), mblurValueAlpha.w);
		float minAlpha = min(min3(mblurValueAlpha.x, mblurValueAlpha.y, mblurValueAlpha.z), mblurValueAlpha.w);

		float alphaEdgeWeight = 1.0f - saturate((maxAlpha - minAlpha) / 0.25f);
		float weightOnAlpha = 1.0f;//lerp(1.0f, dot(mblurValueAlpha, 0.25f), alphaEdgeWeight * alphaEdgeWeight);
*/
		finalUv = lerp(uv, halfUv, lerpRatio/* * weightOnAlpha*/);
	}

	float4 mblurValue = pSrt->m_motionBlurColorInfo.SampleLevel(pSrt->m_linearClampSampler, finalUv, 0);

	float4 srcColor = pSrt->m_txBlurSourceBuffer[dispatchThreadId.xy];

	pSrt->m_rwbMergedBuffer[dispatchThreadId.xy] = float4(mblurValue.xyz + srcColor.xyz * mblurValue.w, srcColor.w);
}

#define SOFT_Z_EXTENT 1

// helper function, from the paper but adjusted to avoid division by zero
float Cone(float2 X, float2 Y, float2 v)
{
	// to avoid div by 0
	float Bias = 0.001f;

	// better for no background velocity
	return length(X - Y) < length(v);
}

// helper function, from the paper but adjusted to avoid division by zero
float Cylinder(float2 X, float2 Y, float2 v)
{
	// to avoid div by 0
	float Bias = 0.001f;

	return 1 - smoothstep(0.95f * length(v), 1.05f * length(v) + Bias, length(X - Y) );
}

// helper function, from the paper
// note this assumes negative z values
// is zb closer than za?
float SoftDepthCompare(float za, float zb)
{
	return saturate(1 - (za - zb) / SOFT_Z_EXTENT);
}

// [0, 1]
float RandFast( uint2 pixelPos, float magic = 3571.0 )
{
	float2 randomVal = ( 1.0 / 4320.0 ) * pixelPos + float2( 0.25, 0.0 );
	randomVal = frac( dot( randomVal * randomVal, magic ) );
	randomVal = frac( dot( randomVal * randomVal, magic ) );
	return randomVal.x;
}

float2 DepthCmp( float CenterDepth, float SampleDepth, float DepthScale )
{
	return saturate( 0.5 + float2( DepthScale, -DepthScale ) * (SampleDepth - CenterDepth) );
	//return SampleDepth > CenterDepth ? float2(1,0) : float2(0,1);
}

float2 SpreadCmp( float OffsetLength, float2 SpreadLength, float PixelToSampleScale )
{
	return saturate( PixelToSampleScale * SpreadLength - max( OffsetLength - 1, 0 ) );
	//return PixelToSampleScale * SpreadLength > OffsetLength ? 1 : 0;
	//return SpreadLength > SearchLengthPixels * OffsetLength / ( StepCount - 0.5 ) ? 1 : 0;
}

float SampleWeight( float CenterDepth, float SampleDepth, float OffsetLength, float CenterSpreadLength, float SampleSpreadLength, float PixelToSampleScale, float DepthScale )
{
	float2 DepthWeights = DepthCmp( CenterDepth, SampleDepth, DepthScale );
	float2 SpreadWeights = SpreadCmp( OffsetLength, float2( CenterSpreadLength, SampleSpreadLength ), PixelToSampleScale );
	return dot( DepthWeights, SpreadWeights );
}

struct CreateMaxAvgMotionVectorSrt
{
	int2							m_bufSize;
	uint							m_bufferScale;
	uint							m_pad;
	RWTexture2D<float2>				m_rwbMblurTile;
	Texture2D<float2>				m_motionVectorPixels;
};

[numthreads(8, 8, 1)]
void CS_CreateMaxAvgMotionVector(int2 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex, CreateMaxAvgMotionVectorSrt* pSrt : S_SRT_DATA)
{
	float2 motionVector = pSrt->m_motionVectorPixels[min(dispatchThreadId.xy, pSrt->m_bufSize-1)];
	float lengthMotionVec = length(motionVector);

	float max_01 = max(lengthMotionVec, LaneSwizzle(lengthMotionVec, 0x1f, 0x00, 0x01));
	float max_08 = max(max_01, LaneSwizzle(max_01, 0x1f, 0x00, 0x08));
	float max_02 = max(max_08, LaneSwizzle(max_08, 0x1f, 0x00, 0x02));
	float max_10 = max(max_02, LaneSwizzle(max_02, 0x1f, 0x00, 0x10));
	float max_04 = max(max_10, LaneSwizzle(max_10, 0x1f, 0x00, 0x04));
	float maxMotionVec = max(max_04, ReadLane(max_04, 0x20));

	float vec_x01 = motionVector.x + LaneSwizzle(motionVector.x, 0x1f, 0x00, 0x01);
	float vec_x08 = vec_x01 + LaneSwizzle(vec_x01, 0x1f, 0x00, 0x08);
	float vec_x02 = vec_x08 + LaneSwizzle(vec_x08, 0x1f, 0x00, 0x02);
	float vec_x10 = vec_x02 + LaneSwizzle(vec_x02, 0x1f, 0x00, 0x10);
	float vec_x04 = vec_x10 + LaneSwizzle(vec_x10, 0x1f, 0x00, 0x04);
	float sumVecX = vec_x04 + ReadLane(vec_x04, 0x20);

	float vec_y01 = motionVector.y + LaneSwizzle(motionVector.y, 0x1f, 0x00, 0x01);
	float vec_y08 = vec_y01 + LaneSwizzle(vec_y01, 0x1f, 0x00, 0x08);
	float vec_y02 = vec_y08 + LaneSwizzle(vec_y08, 0x1f, 0x00, 0x02);
	float vec_y10 = vec_y02 + LaneSwizzle(vec_y02, 0x1f, 0x00, 0x10);
	float vec_y04 = vec_y10 + LaneSwizzle(vec_y10, 0x1f, 0x00, 0x04);
	float sumVecY = vec_y04 + ReadLane(vec_y04, 0x20);

	if (maxMotionVec == lengthMotionVec)
	{
		pSrt->m_rwbMblurTile[int2(groupId.xy)] = lengthMotionVec == 0.0f ? float2(0, 0) : normalize(float2(sumVecX, sumVecY)) * lengthMotionVec;
	}
}

struct CalculateMotionVectorAngleRangeSrt
{
	int2							m_bufSize;
	uint							m_bufferScale;
	uint							m_pad;
	RWTexture2D<float2>				m_rwbMaxAngle;
	Texture2D<float2>				m_motionVectorPixels;
	RWTexture2D<float2>				m_avgMotionVector;
};

[numthreads(8, 8, 1)]
void CS_CalculateMotionVectorAngleRange(int2 dispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex, CalculateMotionVectorAngleRangeSrt* pSrt : S_SRT_DATA)
{
	float2 motionVector = pSrt->m_motionVectorPixels[min(dispatchThreadId.xy, pSrt->m_bufSize-1)];
	float lengthMotionVec = length(motionVector);

	float cosAngle = 1.0f;
	if (lengthMotionVec > 0)
		cosAngle = dot(normalize(motionVector), normalize(pSrt->m_avgMotionVector[groupId]));

	float minCosAngle_01 = min(cosAngle, LaneSwizzle(cosAngle, 0x1f, 0x00, 0x01));
	float minCosAngle_08 = min(minCosAngle_01, LaneSwizzle(minCosAngle_01, 0x1f, 0x00, 0x08));
	float minCosAngle_02 = min(minCosAngle_08, LaneSwizzle(minCosAngle_08, 0x1f, 0x00, 0x02));
	float minCosAngle_10 = min(minCosAngle_02, LaneSwizzle(minCosAngle_02, 0x1f, 0x00, 0x10));
	float minCosAngle_04 = min(minCosAngle_10, LaneSwizzle(minCosAngle_10, 0x1f, 0x00, 0x04));
	float minCosAngle = min(minCosAngle_04, ReadLane(minCosAngle_04, 0x20));

	if (groupIndex == 0)
		pSrt->m_rwbMaxAngle[int2(groupId.xy)] = acos(clamp(minCosAngle, -1.0f, 1.0f)) / 3.1415926f * 180.0f;
}


struct CreateMaxNeighborMotionVectorSrt
{
	int2							m_bufSize;
	float2							m_invBufferSize;
	RWTexture2D<float2>				m_rwbMblurTile;
	Texture2D<float2>				m_mblurTile;
};

groupshared float3 g_mblurTile[12][12];

[numthreads(8, 8, 1)]
void CS_CreateMaxNeighborMotionVector(int2 dispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int2 groupThreadId : SV_GroupThreadID, int groupIndex : SV_GroupIndex, CreateMaxNeighborMotionVectorSrt* pSrt : S_SRT_DATA)
{
	int2 baseIdx = (int2)groupId.xy * 8 - int2(2, 2);
	int2 lineIdx = int2(groupIndex / 12, groupIndex % 12);
	int2 lineIdx1 = int2((groupIndex + 64) / 12, (groupIndex + 64) % 12);
	int2 lineIdx2 = int2((groupIndex + 128) / 12, (groupIndex + 128) % 12);
	int2 srcLineIdx = lineIdx + baseIdx;
	int2 srcLineIdx1 = lineIdx1 + baseIdx;
	int2 srcLineIdx2 = lineIdx2 + baseIdx;

	g_mblurTile[lineIdx.x][lineIdx.y].xy = all(srcLineIdx >= 0 && srcLineIdx < pSrt->m_bufSize) ? pSrt->m_mblurTile[srcLineIdx] : 0;
	g_mblurTile[lineIdx.x][lineIdx.y].z = length(g_mblurTile[lineIdx.x][lineIdx.y].xy);
	g_mblurTile[lineIdx1.x][lineIdx1.y].xy = all(srcLineIdx1 >= 0 && srcLineIdx1 < pSrt->m_bufSize) ? pSrt->m_mblurTile[srcLineIdx1] : 0;
	g_mblurTile[lineIdx1.x][lineIdx1.y].z = length(g_mblurTile[lineIdx1.x][lineIdx1.y].xy);
	if (groupIndex < 16)
	{
		g_mblurTile[lineIdx2.x][lineIdx2.y].xy = all(srcLineIdx2 >= 0 && srcLineIdx2 < pSrt->m_bufSize) ? pSrt->m_mblurTile[srcLineIdx2] : 0;
		g_mblurTile[lineIdx2.x][lineIdx2.y].z = length(g_mblurTile[lineIdx2.x][lineIdx2.y].xy);
	}

	float radius = 1.14122f / 2;
	float3 centerMotionVec = g_mblurTile[groupThreadId.x + 2][groupThreadId.y + 2];

	for (int i = 0; i < 5; i++)
		for (int j = 0; j < 5; j++)
		{
			if (i != 2 && j != 2) // skip center tile.
			{
				float3 sampleMotionVec = g_mblurTile[groupThreadId.x + i][groupThreadId.y + j];
				if (sampleMotionVec.z > centerMotionVec.z ) // if sample's blur radius less then center rarius, skip it.
				{
					float2 direction = float2(i-2, j-2);
					if (dot(direction, sampleMotionVec.xy) < 0.0f) // if same direction, skip, since it won't blur into this tile.
					{
						float2 sphereCenter = direction;
						float distToCenter = length(sphereCenter);

						if (distToCenter - 2 * radius < sampleMotionVec.z) // within blur radius.
						{
							float2 movedSampleSphere = sphereCenter + normalize(sampleMotionVec.xy) * distToCenter;
							if (length(movedSampleSphere) < 2 * radius)
							{
								centerMotionVec = sampleMotionVec;
							}
						}
					}
				}
			}
		}

	pSrt->m_rwbMblurTile[dispatchThreadId.xy] = centerMotionVec.xy;
}

struct MotionBlurScatterToGatherPassSrt
{
	int2							m_bufSize;
	float2							m_invBufSizeFull;
	float2							m_invBufferSize;
	uint							m_bufferScale;
	uint							m_numSteps;
	float2							m_depthParams;
	float							m_invDepthFadeRange;
	float							m_depthFadeOffset;
	RWTexture2D<float3>				m_rwbMblurBuffer;
	RWTexture2D<float>				m_rwbMblurAlphaBuffer;
	Texture2D<float2>				m_txMaxVelocity;
	Texture2D<float4>				m_txSrcColor;
	Texture2D<float2>				m_depthVS;
	Texture2D<float2>				m_motionVectorPixels;
	Texture2D<float2>				m_txFullMotionVector;
	Texture2D<float4>				m_txLastFrameColor;
	Texture2D<float>				m_txMaxAngle;
	SamplerState					m_sPointClamp;
	SamplerState					m_sLinearClamp;
};

[numthreads(8, 8, 1)]
void CS_MotionBlurScatterToGatherPass(int2 dispatchThreadId : SV_DispatchThreadId, int2 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, MotionBlurScatterToGatherPassSrt* pSrt : S_SRT_DATA)
{
	float2 screenPos = dispatchThreadId.xy + 0.5f;

	int2 srcDispatchThreadId = int2(dispatchThreadId.x << pSrt->m_bufferScale, dispatchThreadId.y << pSrt->m_bufferScale);

	// Screen Quad UV 0..1
	float2 screenUv = (srcDispatchThreadId + 0.5f) * pSrt->m_invBufSizeFull;
	// screen position in [-1, 1] screen space
	float2 screenSpacePos = screenUv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

	const uint stepCount = pSrt->m_numSteps;

	float2 ditherVec = frac(screenPos.yx * float2(0.4 * 0.434, 0.4 * 0.434) + screenPos.xy * 0.434);
	float dither = ( ditherVec.x * 2 - 1 ) * ( ditherVec.y * 2 - 1 );
	float randomVal = 0;//RandFast( dispatchThreadId );
	float randomVal2 = 0;//RandFast( dispatchThreadId, 5521 );
	
	float2 tileJitter = ( float2( randomVal, randomVal2 ) - 0.5f ) * 0.5f;
	
	uint2 maxVelocityBufferSize;
	pSrt->m_txMaxVelocity.GetDimensions(maxVelocityBufferSize.x, maxVelocityBufferSize.y);
	float2 maxVelocityPixels = pSrt->m_txMaxVelocity.SampleLevel(pSrt->m_sLinearClamp, saturate(screenUv + tileJitter / maxVelocityBufferSize), 0);
	float2 maxVelocityScreen = maxVelocityPixels * pSrt->m_invBufferSize.xy;

	float3 centerColor = pSrt->m_txSrcColor[srcDispatchThreadId].rgb;
	float2 centerVelocityDepth = pSrt->m_depthVS[dispatchThreadId.xy];
	float  centerDepth = centerVelocityDepth.x;
	float  depthVSDiff = centerVelocityDepth.y - centerDepth;

	float4 finalColor = float4(0, 0, 0, 1.0f);

	float2 centerVelocity = pSrt->m_motionVectorPixels[dispatchThreadId.xy];

	//if( length( maxVelocityPixels ) >= 1.0 )
	{
		// in pixels
		float centerVelocityLength = length( centerVelocity );
	
		// Clip MaxVelocity to screen rect
		float2 invVelocityScreen = rcp( -maxVelocityScreen );
		float2 minIntersect = -invVelocityScreen - screenSpacePos * invVelocityScreen;
		float2 maxIntersect =  invVelocityScreen - screenSpacePos * invVelocityScreen;
		float2 farIntersect = max(minIntersect, maxIntersect);
		float intersect = saturate(min(farIntersect.x, farIntersect.y));

		// +/-
		float2 searchVectorPixels = -maxVelocityPixels.xy * intersect;
		float searchLengthPixels = length(searchVectorPixels);

		// converts pixel length to sample steps
		float pixelToSampleScale = (stepCount - 0.5f) / searchLengthPixels;

		float stepDepthVS = depthVSDiff / stepCount;
		float currentDepthVS = centerDepth;// + stepDepthVS * 0.5f;

		float4 colorAccum = 0;
		float totalSamples = 0;

		for( uint i = 0; i < stepCount; i++ )
		{
			float offsetLength = (float)i + 0.5/* + (randomVal - 0.5)*/;
			float offsetFraction = offsetLength / ( stepCount - 0.5 );

			float2 offsetPixels = offsetFraction * searchVectorPixels;

			{
				float2 sampleUV = screenUv + offsetPixels * pSrt->m_invBufferSize;

				float3 sampleColor = pSrt->m_txSrcColor.SampleLevel( pSrt->m_sPointClamp, sampleUV, 0 ).rgb;
				float sampleDepth = pSrt->m_depthVS.SampleLevel( pSrt->m_sPointClamp, sampleUV, 0 ).x;

				// in pixels
				float2 sampleVelocity = pSrt->m_motionVectorPixels.SampleLevel( pSrt->m_sPointClamp, sampleUV, 0 );
				float sampleVelocityLength = length(sampleVelocity);

				float weight = 0;
				if (dot(sampleVelocity, offsetPixels) < 0)
				{
					float weightScale = saturate((sampleVelocityLength / length(offsetPixels) - 0.8f) / 0.2f);
					weight = (1.0f - GetSampleFadeWeight(sampleDepth, currentDepthVS, sampleVelocity, centerVelocity, pSrt->m_invDepthFadeRange, pSrt->m_depthFadeOffset)) * weightScale;
					totalSamples += 1.0f;
					colorAccum += weight * float4(sampleColor.rgb, 1.0f);
				}
			}

			{
				float2 sampleUV = screenUv - offsetPixels * pSrt->m_invBufferSize;

				float3 sampleColor = pSrt->m_txSrcColor.SampleLevel( pSrt->m_sPointClamp, sampleUV, 0 ).rgb;
				float sampleDepth = pSrt->m_depthVS.SampleLevel( pSrt->m_sPointClamp, sampleUV, 0 ).x;

				// in pixels
				float2 sampleVelocity = pSrt->m_motionVectorPixels.SampleLevel( pSrt->m_sPointClamp, sampleUV, 0 );
				float sampleVelocityLength = length(sampleVelocity);

				float weight = 0;
				if (dot(sampleVelocity, offsetPixels) < 0)
				{
					float weightScale = saturate((sampleVelocityLength / length(offsetPixels) - 0.8f) / 0.2f);
					weight = (1.0f - GetSampleFadeWeight(sampleDepth, currentDepthVS, sampleVelocity, centerVelocity, pSrt->m_invDepthFadeRange, pSrt->m_depthFadeOffset)) * weightScale;
					totalSamples += 1.0f;
					colorAccum += weight * float4(sampleColor.rgb, 1.0f);
				}
			}

			//currentDepthVS += stepDepthVS;
		}

/*		float clamppedMblurRange = min(max(abs(centerVelocity.x), abs(centerVelocity.y)), stepCount);

		float curPixelWeight = 1.0f;
		if (clamppedMblurRange >= 1.0f)
			curPixelWeight = 1.0f / clamppedMblurRange;

		finalColor.xyz = (colorAccum.rgb / colorAccum.w) * (1.0f - curPixelWeight);
		finalColor.w = curPixelWeight;*/

		colorAccum /= (totalSamples + 1.0f);

		finalColor.xyz = colorAccum.rgb;
		finalColor.w = 1.0f - colorAccum.a;
	}

	pSrt->m_rwbMblurBuffer[dispatchThreadId.xy] = finalColor.xyz;
	pSrt->m_rwbMblurAlphaBuffer[dispatchThreadId.xy] = finalColor.w;
}
