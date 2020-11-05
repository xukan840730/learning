#define SKIP_GLOBAL_SAMPLERS

#include "global-funcs.fxi"

struct VsmConvertSrt
{
	RWTexture2D<float2>		m_txVsmShadowBuffer;
	Texture2D<float>		m_txShadowBuffer;
	SamplerState			m_samplerPoint;
	float4					m_shadowParams;
	float2					m_invSize;
};

[numthreads(8, 8, 1)]
void Cs_VsmShadowConvert(uint2 dispatchThreadId : SV_DispatchThreadID, VsmConvertSrt* pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId.xy + 0.5) * pSrt->m_invSize;
	float4 depth = pSrt->m_txShadowBuffer.GatherRed(pSrt->m_samplerPoint, uv);

	// moveto linear space - lightCam.screenToView will get us what we need here.

	float4 depthVS = (depth - pSrt->m_shadowParams.w) / pSrt->m_shadowParams.z;
	float4 depthVS2 = depthVS * depthVS;

	float finalDepth = 0.25f * (depthVS.x + depthVS.y + depthVS.z + depthVS.w);
	float finalDepth2 = 0.25f * (depthVS2.x + depthVS2.y + depthVS2.z + depthVS2.w);

	pSrt->m_txVsmShadowBuffer[dispatchThreadId] = float2(finalDepth, finalDepth2);
}

struct VsmCreateMipsSrt
{
	Texture2D<float2>		m_txVsmShadowBuffer;
	RWTexture2D<float2>		m_txVsmMip1;
	RWTexture2D<float2>		m_txVsmMip2;
};


[numthreads(8, 8, 1)]
void Cs_VsmShadowCreateMips(uint2 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, VsmCreateMipsSrt* pSrt : S_SRT_DATA)
{
	// move this to a linear sample later?

	float2 srcColor = pSrt->m_txVsmShadowBuffer[dispatchThreadId];

	float2 avgColor = srcColor;

	avgColor.x += LaneSwizzle(avgColor.x, 0x1f, 0x00, 0x01);
	avgColor.y += LaneSwizzle(avgColor.y, 0x1f, 0x00, 0x01);
	avgColor.x += LaneSwizzle(avgColor.x, 0x1f, 0x00, 0x08);
	avgColor.y += LaneSwizzle(avgColor.y, 0x1f, 0x00, 0x08);

	avgColor *= 0.25;
	float2 vsmMip1 = avgColor;

	avgColor.x += LaneSwizzle(avgColor.x, 0x1f, 0x00, 0x02);
	avgColor.y += LaneSwizzle(avgColor.y, 0x1f, 0x00, 0x02);
	avgColor.x += LaneSwizzle(avgColor.x, 0x1f, 0x00, 0x10);
	avgColor.y += LaneSwizzle(avgColor.y, 0x1f, 0x00, 0x10);
	avgColor *= 0.25;
	float2 vsmMip2 = avgColor;

	//pSrt->rwb_bloomMip1[dispatchThreadId.xy] = vsmMip1;
	if (groupThreadId.x % 2 == 0 && groupThreadId.y % 2 == 0)
		pSrt->m_txVsmMip1[dispatchThreadId.xy / 2] = vsmMip1;

	if (groupThreadId.x % 4 == 0 && groupThreadId.y % 4 == 0)
	{
		pSrt->m_txVsmMip2[dispatchThreadId.xy / 4] = vsmMip2;
	}
}