#define SKIP_GLOBAL_SAMPLERS

#include "global-funcs.fxi"
#include "cascaded-shadows.fxi"

struct SunShadowCreateMipsSrt
{
	Texture2D<float>		m_txShadowBuffer;
	RWTexture2D<float>		m_txShadowMip1;
	RWTexture2D<float>		m_txShadowMip2;
};

[numthreads(8, 8, 1)]
void Cs_SunShadowCreateMips(uint2 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, SunShadowCreateMipsSrt* pSrt : S_SRT_DATA)
{
	float srcDepth = pSrt->m_txShadowBuffer[dispatchThreadId];
	float minDepth = srcDepth;

	minDepth = min(minDepth, LaneSwizzle(minDepth, 0x1f, 0x00, 0x01));
	minDepth = min(minDepth, LaneSwizzle(minDepth, 0x1f, 0x00, 0x08));

	float shadowMip1 = minDepth;

	minDepth = min(minDepth, LaneSwizzle(minDepth, 0x1f, 0x00, 0x02));
	minDepth = min(minDepth, LaneSwizzle(minDepth, 0x1f, 0x00, 0x10));

	float shadowMip2 = minDepth;

	//pSrt->rwb_bloomMip1[dispatchThreadId.xy] = vsmMip1;
	if (groupThreadId.x % 2 == 0 && groupThreadId.y % 2 == 0)
		pSrt->m_txShadowMip1[dispatchThreadId.xy / 2] = shadowMip1;

	if (groupThreadId.x % 4 == 0 && groupThreadId.y % 4 == 0)
	{
		pSrt->m_txShadowMip2[dispatchThreadId.xy / 4] = shadowMip2;
	}
}

struct SunShadowTextures
{
	Texture2D<float>		txDepthBuffer; //				: register(t0);
	Texture2DArray<float>	txShadowBuffer; //				: register(t2);
	RWTexture2D<float>		uSunShadowCocBuffer; //			: register(u0);
	RWTexture2D<float>		uCastDistanceBuffer; //			: register(u2);
};

struct SunShadowSamplers
{
	SamplerState sSamplerLinear;
};

struct SunShadowSrt
{
	SunShadowConstants *pConsts;
	SunShadowTextures  *pTextures;
	SunShadowSamplers  *pSamplers;
};

struct PreSunShadowSrt
{
	RWTexture2D<float>		m_rwbSunShadowCocBuffer;
	Texture2D<float>		m_txDepthBuffer;
	Texture2DArray<float>	m_txShadowBuffer;
	Texture2D<float>		m_txHeroShadowBuffer;
	SamplerState			m_sSamplerLinear;
	SamplerState			m_sSamplerLinearClampBorderWhite;
	float4					m_screenScaleOffset;
	SunShadowConstants		*m_pConsts;
};

void SunShadowPrePass(uint2 dispatchThreadId, PreSunShadowSrt* pSrt, bool useHero)
{
	float depthBufferZ = pSrt->m_txDepthBuffer[int2(dispatchThreadId.xy) * 2];
	float depthVS = (float)(pSrt->m_pConsts->m_shadowParams.w / (depthBufferZ - pSrt->m_pConsts->m_shadowParams.z));
	float3 positionVS = float3(((dispatchThreadId.xy * 2 + 0.5f) * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f) * depthVS;

	float blurRadius = 0;
	if (depthVS < pSrt->m_pConsts->m_cascadedLevelDist[1].w)
	{	
		float4 shadowTexCoords;
		uint iCascadedLevel = ComputeCascadeLevelAndShadowTexCoords(positionVS, pSrt->m_pConsts->m_cascadedLevelDist,
			pSrt->m_pConsts->m_matricesRows, pSrt->m_pConsts->m_boxSelection, pSrt->m_pConsts->m_numCascades, shadowTexCoords);

		float cascadedDepthRange = pSrt->m_pConsts->m_parameters0[iCascadedLevel].x;

		shadowTexCoords.xy = saturate(shadowTexCoords.xy);

		float depthZ = pSrt->m_txShadowBuffer.SampleLevel(pSrt->m_sSamplerLinear, shadowTexCoords.xyz, 0);
		float4	blurParams0 = pSrt->m_pConsts->m_blurParams[0];
		float4	blurParams1 = pSrt->m_pConsts->m_blurParams[1];

		if (useHero && (iCascadedLevel == 0))
		{
			float3 heroShadowTexCoords;
			heroShadowTexCoords.x = dot(float4(positionVS.xyz, 1.0), pSrt->m_pConsts->m_heroMatricesRows[0]);
			heroShadowTexCoords.y = dot(float4(positionVS.xyz, 1.0), pSrt->m_pConsts->m_heroMatricesRows[1]);
			heroShadowTexCoords.z = dot(float4(positionVS.xyz, 1.0), pSrt->m_pConsts->m_heroMatricesRows[2]);
			heroShadowTexCoords.xy = saturate(heroShadowTexCoords.xy);

			float heroDepthZ = pSrt->m_txHeroShadowBuffer.SampleLevel(pSrt->m_sSamplerLinearClampBorderWhite, heroShadowTexCoords.xy, 0);
			if (heroDepthZ < depthZ)
			{
				cascadedDepthRange = pSrt->m_pConsts->m_heroParameters.z;
				shadowTexCoords.w = heroShadowTexCoords.z;
				depthZ = heroDepthZ;
				blurParams0 = pSrt->m_pConsts->m_heroBlurParams[0];
				blurParams1 = pSrt->m_pConsts->m_heroBlurParams[1];
			}
		}

		float castDist = max(shadowTexCoords.w - depthZ, 0) * cascadedDepthRange;

		blurRadius = saturate(castDist * blurParams0.x + blurParams0.y) * blurParams0.z + blurParams0.w;
		blurRadius += saturate(castDist * blurParams1.x + blurParams1.y) * blurParams1.z;
	}

	pSrt->m_rwbSunShadowCocBuffer[int2(dispatchThreadId.xy)] = blurRadius;
}

[numthreads(8, 8, 1)]
void CS_SunShadowResolvePrePass(uint2 dispatchThreadId : SV_DispatchThreadId, PreSunShadowSrt* pSrt : S_SRT_DATA)
{
	const bool useHero = false;
	
	SunShadowPrePass(dispatchThreadId, pSrt, useHero);
}

[numthreads(8, 8, 1)]
void CS_SunShadowWithHeroResolvePrePass(uint2 dispatchThreadId : SV_DispatchThreadId, PreSunShadowSrt* pSrt : S_SRT_DATA)
{
	const bool useHero = true;

	SunShadowPrePass(dispatchThreadId, pSrt, useHero);
}

#define kMaxSunShadowBlurRadius 64.0f

float GetSampleWeight(float dist, float distRef)
{
	return (1.0f - saturate(abs(dist - distRef) * 100.0f));
}

struct SunShadowResolveXTextures
{
	RWTexture2D<float2>					rwb_blur_buf_0;
	RWTexture2D<float2>					rwb_blur_buf_1;
	Texture2D<float>					blur_radius;
	Texture2D<float>					vs_depth;
	Texture2DArray<float>				shadow_buf;
	Texture2D<float2>					face_normal;
};

struct SunShadowResolveXSamplers
{
	SamplerState						samplerPoint;
	SamplerState						samplerLinear;
	SamplerComparisonState				samplerShadow;
};

struct SunShadowResolveXSrt
{
	SunShadowConstants					*consts;
	SunShadowResolveXTextures			*texs;
	SunShadowResolveXSamplers			*smpls;
};

struct SunShadowResolveYTextures
{
	RWTexture2D<float2>					rwb_blur_buf;
	Texture2D<float>					blur_radius;
	Texture2D<float>					vs_depth;
	Texture2D<float2>					tmp_shadow_buf0;
	Texture2D<float2>					tmp_shadow_buf1;
	Texture2D<float2>					face_normal;
};

struct SunShadowResolveYSamplers
{
	SamplerState						samplerPoint;
	SamplerState						samplerLinear;
};

struct SunShadowResolveYSrt
{
	SunShadowConstants					*consts;
	SunShadowResolveYTextures			*texs;
	SunShadowResolveYSamplers			*smpls;
};

#define NUM_BLUR_SAMPLES 8

struct SmoothDitheredShadowSrt
{
	float								m_blurWeights[NUM_BLUR_SAMPLES];
	float								m_blurDepthThreshold;
	float2								m_vsDepthParams;
	RWTexture2D<float>					m_rwbShadowInfo;
	Texture2D<float>					m_shadowInfo;
	Texture2D<float>					m_srcDepth;
};

groupshared uint   g_runtimeShadowInfo[16][8];
groupshared uint   g_runtimeShadowInfoY[16][8];
groupshared uint   g_runtimeShadowInfoZ[16][8];
groupshared uint   g_runtimeShadowInfoW[16][8];
groupshared float  g_shadowInfo[16][8];
groupshared float  g_vsDepth[16][8];

void CalculateSampleWeightU(out uint sWeights[8], out uint centerWeight, int2 sampleIdx, float cameraPixelScale, float fadeRange, float fadeExponent)
{
	float centerDepth = g_vsDepth[sampleIdx.x+4][sampleIdx.y];
	float pixelX = cameraPixelScale * centerDepth;
	float avgDepthVsSlope = (g_vsDepth[sampleIdx.x+5][sampleIdx.y] - g_vsDepth[sampleIdx.x+3][sampleIdx.y]) * 0.5f;
	float2 lineNormal = normalize(float2(-avgDepthVsSlope, pixelX));

	float distOffset[8];
	distOffset[0] = abs(dot(float2(-pixelX * 4.0f, g_vsDepth[sampleIdx.x+0][sampleIdx.y] - centerDepth), lineNormal));
	distOffset[1] = abs(dot(float2(-pixelX * 3.0f, g_vsDepth[sampleIdx.x+1][sampleIdx.y] - centerDepth), lineNormal));
	distOffset[2] = abs(dot(float2(-pixelX * 2.0f, g_vsDepth[sampleIdx.x+2][sampleIdx.y] - centerDepth), lineNormal));
	distOffset[3] = abs(dot(float2(-pixelX * 1.0f, g_vsDepth[sampleIdx.x+3][sampleIdx.y] - centerDepth), lineNormal));
	distOffset[4] = abs(dot(float2(pixelX * 1.0f, g_vsDepth[sampleIdx.x+5][sampleIdx.y] - centerDepth), lineNormal));
	distOffset[5] = abs(dot(float2(pixelX * 2.0f, g_vsDepth[sampleIdx.x+6][sampleIdx.y] - centerDepth), lineNormal));
	distOffset[6] = abs(dot(float2(pixelX * 3.0f, g_vsDepth[sampleIdx.x+7][sampleIdx.y] - centerDepth), lineNormal));
	distOffset[7] = abs(dot(float2(pixelX * 4.0f, g_vsDepth[sampleIdx.x+8][sampleIdx.y] - centerDepth), lineNormal));

	float sumWeights = 1.0f;
	float weights[8];
	for (int i = 0; i < 8; i++)
	{
		weights[i] = 1.0f - pow(saturate(distOffset[i] / fadeRange), fadeExponent);
		sumWeights += weights[i];
	}

	float invSumWeights = 1.0f / sumWeights;
	for (int i = 0; i < 8; i++)
		sWeights[i] = uint(weights[i] * invSumWeights * 256.0f); 

	centerWeight = uint(invSumWeights * 256.0f);
}

float CalculateBlurredDitherShadow(in int2 sampleIdx, in float blurWeights[NUM_BLUR_SAMPLES], in float blurDepthThreshold)
{
	float sumShadowInfo = g_shadowInfo[sampleIdx.x + 4][sampleIdx.y];
	float centerDepth = g_vsDepth[sampleIdx.x + 4][sampleIdx.y];
	float weightSum = 1.0f;

	UNROLL
	for (uint i = 0; i < (NUM_BLUR_SAMPLES / 2); i++)
	{
		uint index = i;
		float sampleDepth = g_vsDepth[sampleIdx.x + index][sampleIdx.y];
		float depthDiff = abs(centerDepth - sampleDepth);
		float weight = (1.0f - saturate(depthDiff * blurDepthThreshold)) * blurWeights[i];
		sumShadowInfo += g_shadowInfo[sampleIdx.x + index][sampleIdx.y] * weight;
		weightSum += weight;
	}

	UNROLL
	for (uint i = (NUM_BLUR_SAMPLES / 2); i < NUM_BLUR_SAMPLES; i++)
	{
		uint index = i + 1;
		float sampleDepth = g_vsDepth[sampleIdx.x + index][sampleIdx.y];
		float depthDiff = abs(centerDepth - sampleDepth);
		float weight = (1.0f - saturate(depthDiff * blurDepthThreshold)) * blurWeights[i];
		sumShadowInfo += g_shadowInfo[sampleIdx.x + index][sampleIdx.y] * weight;
		weightSum += weight;
	}

	return sumShadowInfo / weightSum;
}

[numthreads(8, 8, 1)]
void CS_SmoothDitheredShadowPassX(int2 dispatchThreadId : SV_DispatchThreadId, int groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, int2 groupId : SV_GroupID, SmoothDitheredShadowSrt* pSrt : S_SRT_DATA)
{
	int2 baseIdx = (int2)groupId.xy * 8 - int2(4, 0);
	int2 lineIdx = int2(groupIndex % 16, groupIndex / 16);
	int2 lineIdx1 = int2((groupIndex + 64) % 16, (groupIndex + 64) / 16);

	g_shadowInfo[lineIdx.x][lineIdx.y] = pSrt->m_shadowInfo[lineIdx + baseIdx];
	g_vsDepth[lineIdx.x][lineIdx.y] = (float)(pSrt->m_vsDepthParams.x / (pSrt->m_srcDepth[lineIdx + baseIdx] - pSrt->m_vsDepthParams.y));
	g_shadowInfo[lineIdx1.x][lineIdx1.y] = pSrt->m_shadowInfo[lineIdx1 + baseIdx];
	g_vsDepth[lineIdx1.x][lineIdx1.y] = (float)(pSrt->m_vsDepthParams.x / (pSrt->m_srcDepth[lineIdx1 + baseIdx] - pSrt->m_vsDepthParams.y));

	pSrt->m_rwbShadowInfo[int2(dispatchThreadId.xy)] = CalculateBlurredDitherShadow(groupThreadId.xy, pSrt->m_blurWeights, pSrt->m_blurDepthThreshold);
}

[numthreads(8, 8, 1)]
void CS_SmoothDitheredShadowPassY(int2 dispatchThreadId : SV_DispatchThreadId, int groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, int2 groupId : SV_GroupID, SmoothDitheredShadowSrt* pSrt : S_SRT_DATA)
{
	int2 baseIdx = (int2)groupId.xy * 8 - int2(0, 4);
	int2 lineIdx = int2(groupIndex % 8, groupIndex / 8);
	int2 lineIdx1 = int2((groupIndex + 64) % 8, (groupIndex + 64) / 8);

	g_shadowInfo[lineIdx.y][lineIdx.x] = pSrt->m_shadowInfo[lineIdx + baseIdx];
	g_vsDepth[lineIdx.y][lineIdx.x] = (float)(pSrt->m_vsDepthParams.x / (pSrt->m_srcDepth[lineIdx + baseIdx] - pSrt->m_vsDepthParams.y));
	g_shadowInfo[lineIdx1.y][lineIdx1.x] = pSrt->m_shadowInfo[lineIdx1 + baseIdx];
	g_vsDepth[lineIdx1.y][lineIdx1.x] = (float)(pSrt->m_vsDepthParams.x / (pSrt->m_srcDepth[lineIdx1 + baseIdx] - pSrt->m_vsDepthParams.y));

	pSrt->m_rwbShadowInfo[int2(dispatchThreadId.xy)] = CalculateBlurredDitherShadow(groupThreadId.yx, pSrt->m_blurWeights, pSrt->m_blurDepthThreshold);
}

struct ShadowSamplePattern
{
	float2		samples[64];
};

struct TemporalBlurSunShadowSrt
{
	SunShadowConstants			*m_pConsts;
	ShadowSamplePattern			*pSamplePattern;
	float4						m_screenScaleOffset;
	uint4						m_shadowSampleParams;
	float4						m_sunDirVS;
	Texture2D<float>			txDepthBuffer;
	Texture2DArray<float>		txShadowBuffer;
	Texture2D<float2>			txVsmShadowBuffer;
	Texture2D<float>			txHeroShadowBuffer;
	Texture2D<float>			txCocBuffer;

	RWTexture2D<float>			rwbShadowInfo;

	SamplerState				sSamplerPoint;
	SamplerState				sSamplerLinear;
	SamplerComparisonState		sSamplerShadow;
	SamplerComparisonState		sSamplerShadowClampBorderWhite;

	float2						m_linearDepthParams[kMaxNumCascaded];
	float						m_vsmLeakFix;
};

groupshared float2 g_shadowSamplePattern[64];

// light leak reduction for vsm
float linstep(float min, float max, float v)
{
	return clamp((v - min) / (max - min), 0, 1);
}
float ReduceLightBleeding(float p_max, float Amount)
{
	// Remove the [0, Amount] tail and linearly rescale (Amount, 1].
	return linstep(Amount, 1, p_max);
}

float doVsmCalc(float2 moments, float fragDepth, float leakFix)
{
	float lit = 0.0f;
	float E_x2 = moments.y;
	float Ex_2 = moments.x * moments.x;
	float variance = E_x2 - Ex_2;
	float mD = moments.x - fragDepth;
	float mD_2 = mD * mD;
	float p = variance / (variance + mD_2);

	p = ReduceLightBleeding(p, leakFix);

	lit = max(p, fragDepth <= moments.x);

	return lit;
}

float FindLod(float2 uv)
{
	float2 dx, dy;

	dx.x = uv.x - LaneSwizzle(uv.x, 0x1f, 0x00, 0x01);
	dx.y = uv.y - LaneSwizzle(uv.y, 0x1f, 0x00, 0x01);

	dy.x = uv.x - LaneSwizzle(uv.x, 0x1f, 0x00, 0x08);
	dy.y = uv.y - LaneSwizzle(uv.y, 0x1f, 0x00, 0x08);

	//	Mip calculation from equation 3.21 in:
	//	https://www.khronos.org/registry/OpenGL/specs/gl/glspec42.core.pdf

	float p = max(dot(dx, dx), dot(dy, dy));

	return (log2(p) * 0.5);
}

float SunShadow(uint2 screenCoord, uint2 groupThreadIndex, TemporalBlurSunShadowSrt *pSrt, bool autoDepthAdjust, bool fixRadius, bool useHero, bool useVsm, bool useMips)
{
	float2 screenUv = screenCoord * pSrt->m_pConsts->m_sizeParams.xy;
	float depthBufferZ = pSrt->txDepthBuffer[screenCoord];
	float depthVS = (float)(pSrt->m_pConsts->m_shadowParams.w / (depthBufferZ - pSrt->m_pConsts->m_shadowParams.z));
	float3 positionVS = float3((screenCoord * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f) * depthVS;

	float finalShadowInfo = 1.0f;
	if (depthVS < pSrt->m_pConsts->m_cascadedLevelDist[1].w)
	{
		finalShadowInfo = 0.0f;

		float slopeAngle = 0.0f;
		if (autoDepthAdjust)
		{
			float3 positionVS_x, positionVS_y;
			positionVS_x.x = LaneSwizzle(positionVS.x, 0x1f, 0x00, 0x01);
			positionVS_x.y = LaneSwizzle(positionVS.y, 0x1f, 0x00, 0x01);
			positionVS_x.z = LaneSwizzle(positionVS.z, 0x1f, 0x00, 0x01);

			positionVS_y.x = LaneSwizzle(positionVS.x, 0x1f, 0x00, 0x08);
			positionVS_y.y = LaneSwizzle(positionVS.y, 0x1f, 0x00, 0x08);
			positionVS_y.z = LaneSwizzle(positionVS.z, 0x1f, 0x00, 0x08);

			float3 deltaVS_x = positionVS_x - positionVS.xyz;
			float3 deltaVS_y = positionVS_y - positionVS.xyz;
			float3 normalWS = normalize(cross(deltaVS_x, deltaVS_y));
			float maxDeltaZ = max(abs(deltaVS_x.z), abs(deltaVS_y.z));
			uint2 oddIndex = groupThreadIndex & 0x01;

			slopeAngle = maxDeltaZ > 0.05f ? 0.0f : dot(normalWS, pSrt->m_sunDirVS.xyz) * ((oddIndex.x ^ oddIndex.y) > 0 ? 1.0f : -1.0f);
		}

		float4 shadowTexCoords;
		uint iCascadedLevel = ComputeCascadeLevelAndShadowTexCoords(positionVS, pSrt->m_pConsts->m_cascadedLevelDist,
			pSrt->m_pConsts->m_matricesRows, pSrt->m_pConsts->m_boxSelection, pSrt->m_pConsts->m_numCascades, shadowTexCoords);

		uint2 samplesCoord = (screenCoord + pSrt->m_shadowSampleParams.zw) & pSrt->m_shadowSampleParams.x;

		float rawBlurCoc;
		if (fixRadius)
		{ 
			rawBlurCoc = pSrt->m_pConsts->m_blurParams[0].w;
		}
		else
		{
			rawBlurCoc = pSrt->txCocBuffer.SampleLevel(pSrt->sSamplerLinear, screenUv, 0);
		}

		shadowTexCoords.w = shadowTexCoords.w * pSrt->m_pConsts->m_shadowParams.x + pSrt->m_pConsts->m_shadowParams.y;

		if (autoDepthAdjust)
		{
			shadowTexCoords.w -= max(pSrt->m_pConsts->m_cascadedZoffset[0][min(iCascadedLevel, 3)], 0.0f) * (1.0f - abs(slopeAngle));
		}
		else
		{
			shadowTexCoords.w -= max(pSrt->m_pConsts->m_cascadedZoffset[0][min(iCascadedLevel, 3)], 0.0f);
		}
		
		float2 meterToPixelRatio = pSrt->m_pConsts->m_parameters1[iCascadedLevel].xy;
		float2 blurCoc = rawBlurCoc * meterToPixelRatio;

		if (useHero && (iCascadedLevel == 0))
		{
			float3 heroShadowTexCoords;
			heroShadowTexCoords.x = dot(float4(positionVS, 1.0), pSrt->m_pConsts->m_heroMatricesRows[0]);
			heroShadowTexCoords.y = dot(float4(positionVS, 1.0), pSrt->m_pConsts->m_heroMatricesRows[1]);
			heroShadowTexCoords.z = dot(float4(positionVS, 1.0), pSrt->m_pConsts->m_heroMatricesRows[2]);
			float depthBias = clamp(depthVS * pSrt->m_pConsts->m_heroDepthBiasParams.x, pSrt->m_pConsts->m_heroDepthBiasParams.y, pSrt->m_pConsts->m_heroDepthBiasParams.z);
			heroShadowTexCoords.z = heroShadowTexCoords.z * pSrt->m_pConsts->m_shadowParams.x - depthBias;

			if (autoDepthAdjust)
			{
				heroShadowTexCoords.z -= max(pSrt->m_pConsts->m_heroDepthBiasParams.w, 0.0f) * (1.0f - abs(slopeAngle));
			}
			else
			{
				heroShadowTexCoords.z -= max(pSrt->m_pConsts->m_heroDepthBiasParams.w, 0.0f);
			}

			float2 heroMeterToPixelRatio = pSrt->m_pConsts->m_heroParameters.xy;
			float2 heroBlurCoc;
			if (fixRadius)
			{
				heroBlurCoc = pSrt->m_pConsts->m_heroBlurParams[0].w * heroMeterToPixelRatio;
			}
			else
			{
				heroBlurCoc = rawBlurCoc * heroMeterToPixelRatio;
			}

			for (uint i = 0; i < 4; i++)
			{
				float2 rawOffset = g_shadowSamplePattern[i * pSrt->m_shadowSampleParams.y * pSrt->m_shadowSampleParams.y + samplesCoord.y * pSrt->m_shadowSampleParams.y + samplesCoord.x];
			
				float2 baseOffset = rawOffset * blurCoc;
				float baseShadowInfo = pSrt->txShadowBuffer.SampleCmpLevelZero(pSrt->sSamplerShadow, float3(shadowTexCoords.xy + baseOffset, shadowTexCoords.z), shadowTexCoords.w);

				float2 heroOffset = rawOffset * heroBlurCoc;
				float heroShadowInfo = (heroShadowTexCoords.z > 1.0f) ? 1.0f :
					pSrt->txHeroShadowBuffer.SampleCmpLevelZero(pSrt->sSamplerShadowClampBorderWhite, float2(heroShadowTexCoords.xy + heroOffset), heroShadowTexCoords.z);

				finalShadowInfo += min(baseShadowInfo, heroShadowInfo);
			}
			finalShadowInfo = finalShadowInfo * 0.25f;
		}
		else
		{
			float mip = (iCascadedLevel == pSrt->m_pConsts->m_numCascades - 1) ? FindLod(shadowTexCoords.xy * pSrt->m_pConsts->m_shadowSizeParams.zw) : 0.0f;

			if (useVsm && (mip > 0.f))	// last cascade is a VSM
			{
				float2 finalMoments = 0.f;

				for (uint i = 0; i < 4; i++)
				{
					float2 offset = g_shadowSamplePattern[i * pSrt->m_shadowSampleParams.y * pSrt->m_shadowSampleParams.y + samplesCoord.y * pSrt->m_shadowSampleParams.y + samplesCoord.x] * blurCoc;
					float2 baseMoments = pSrt->txVsmShadowBuffer.SampleLevel(pSrt->sSamplerLinear, shadowTexCoords.xy + offset, mip);
					finalMoments += baseMoments;
				}

				finalMoments *= 0.25f;

				// convert shadowTexCoord to linear z
				float2 linearDepthParams = pSrt->m_linearDepthParams[(uint)shadowTexCoords.z];
				float shadowDepthVS = (shadowTexCoords.w - linearDepthParams.y) / linearDepthParams.x;

				finalShadowInfo = doVsmCalc(finalMoments, shadowDepthVS, pSrt->m_vsmLeakFix);
			}
			else if (useMips && (mip > 0.f))
			{
				for (uint i = 0; i < 4; i++)
				{
					float2 offset = g_shadowSamplePattern[i * pSrt->m_shadowSampleParams.y * pSrt->m_shadowSampleParams.y + samplesCoord.y * pSrt->m_shadowSampleParams.y + samplesCoord.x] * blurCoc;
	
					//float depthCmp = pSrt->txShadowBuffer.SampleLOD(pSrt->sSamplerPoint, float3(shadowTexCoords.xy + offset, shadowTexCoords.z), mip);	// THIS WORKS
					//float baseShadowInfo = (depthCmp >= shadowTexCoords.w);

					float4 depthCmpSamples = pSrt->txShadowBuffer.GatherLOD(pSrt->sSamplerPoint, float3(shadowTexCoords.xy + offset, shadowTexCoords.z), mip);
					float4 depthCmps = (depthCmpSamples >= shadowTexCoords.w);
					float baseShadowInfo = (depthCmps.x + depthCmps.y + depthCmps.z + depthCmps.w) * 0.25f;

					//float4 depthCmps = pSrt->txShadowBuffer.GatherCmpLOD(pSrt->sSamplerShadow, float3(shadowTexCoords.xy + offset, shadowTexCoords.z), shadowTexCoords.w, mip);
					//float baseShadowInfo = (depthCmps.x + depthCmps.y + depthCmps.z + depthCmps.w) * 0.25f;

					finalShadowInfo += baseShadowInfo;
				}
				finalShadowInfo = finalShadowInfo * 0.25f;
			}
			else
			{
				for (uint i = 0; i < 4; i++)
				{
					float2 offset = g_shadowSamplePattern[i * pSrt->m_shadowSampleParams.y * pSrt->m_shadowSampleParams.y + samplesCoord.y * pSrt->m_shadowSampleParams.y + samplesCoord.x] * blurCoc;
					float baseShadowInfo = pSrt->txShadowBuffer.SampleCmpLevelZero(pSrt->sSamplerShadow, float3(shadowTexCoords.xy + offset, shadowTexCoords.z), shadowTexCoords.w);

					finalShadowInfo += baseShadowInfo;
				}
				finalShadowInfo = finalShadowInfo * 0.25f;
			}
		}
	}
	return finalShadowInfo;
}

#ifndef USE_MIPS
#define USE_MIPS 0
#endif

#ifndef USE_VSM
#define USE_VSM 0
#endif

[numthreads(8, 8, 1)]
void CS_TemporalAaSunShadow(uint2 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, TemporalBlurSunShadowSrt* pSrt : S_SRT_DATA)
{
	const bool autoDepthAdjust = false;
	const bool fixRadius = false;
	const bool useHero = false;

	g_shadowSamplePattern[groupIndex] = pSrt->pSamplePattern->samples[groupIndex];

	pSrt->rwbShadowInfo[int2(dispatchThreadId.xy)] = SunShadow(dispatchThreadId.xy, groupThreadId, pSrt, autoDepthAdjust, fixRadius, useHero, USE_VSM, USE_MIPS);
}

[numthreads(8, 8, 1)]
void CS_TemporalAaSunShadowWithHero(uint2 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, TemporalBlurSunShadowSrt* pSrt : S_SRT_DATA)
{
	const bool autoDepthAdjust = false;
	const bool fixRadius = false;
	const bool useHero = true;
	
	g_shadowSamplePattern[groupIndex] = pSrt->pSamplePattern->samples[groupIndex];

	pSrt->rwbShadowInfo[int2(dispatchThreadId.xy)] = SunShadow(dispatchThreadId.xy, groupThreadId, pSrt, autoDepthAdjust, fixRadius, useHero, USE_VSM, USE_MIPS);
}

[numthreads(8, 8, 1)]
void CS_TemporalAaSunShadowFixRadius(uint2 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, TemporalBlurSunShadowSrt* pSrt : S_SRT_DATA)
{
	const bool autoDepthAdjust = false;
	const bool fixRadius = true;
	const bool useHero = false;

	g_shadowSamplePattern[groupIndex] = pSrt->pSamplePattern->samples[groupIndex];

	pSrt->rwbShadowInfo[int2(dispatchThreadId.xy)] = SunShadow(dispatchThreadId.xy, groupThreadId, pSrt, autoDepthAdjust, fixRadius, useHero, USE_VSM, USE_MIPS);
}

[numthreads(8, 8, 1)]
void CS_TemporalAaSunShadowFixRadiusWithHero(uint2 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, TemporalBlurSunShadowSrt* pSrt : S_SRT_DATA)
{
	const bool autoDepthAdjust = false;
	const bool fixRadius = true;
	const bool useHero = true;

	g_shadowSamplePattern[groupIndex] = pSrt->pSamplePattern->samples[groupIndex];

	pSrt->rwbShadowInfo[int2(dispatchThreadId.xy)] = SunShadow(dispatchThreadId.xy, groupThreadId, pSrt, autoDepthAdjust, fixRadius, useHero, USE_VSM, USE_MIPS);
}

[numthreads(8, 8, 1)]
void CS_TemporalAaSunShadowAutoDepthAdjust(uint2 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, TemporalBlurSunShadowSrt* pSrt : S_SRT_DATA)
{
	const bool autoDepthAdjust = true;
	const bool fixRadius = false;
	const bool useHero = false;

	g_shadowSamplePattern[groupIndex] = pSrt->pSamplePattern->samples[groupIndex];

	pSrt->rwbShadowInfo[int2(dispatchThreadId.xy)] = SunShadow(dispatchThreadId.xy, groupThreadId, pSrt, autoDepthAdjust, fixRadius, useHero, USE_VSM, USE_MIPS);
}

[numthreads(8, 8, 1)]
void CS_TemporalAaSunShadowAutoDepthAdjustWithHero(uint2 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, TemporalBlurSunShadowSrt* pSrt : S_SRT_DATA)
{
	const bool autoDepthAdjust = true;
	const bool fixRadius = false;
	const bool useHero = true;

	g_shadowSamplePattern[groupIndex] = pSrt->pSamplePattern->samples[groupIndex];

	pSrt->rwbShadowInfo[int2(dispatchThreadId.xy)] = SunShadow(dispatchThreadId.xy, groupThreadId, pSrt, autoDepthAdjust, fixRadius, useHero, USE_VSM, USE_MIPS);
}

[numthreads(8, 8, 1)]
void CS_TemporalAaSunShadowAutoDepthAdjustFixRadius(uint2 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, TemporalBlurSunShadowSrt* pSrt : S_SRT_DATA)
{
	const bool autoDepthAdjust = true;
	const bool fixRadius = true;
	const bool useHero = false;

	g_shadowSamplePattern[groupIndex] = pSrt->pSamplePattern->samples[groupIndex];

	pSrt->rwbShadowInfo[int2(dispatchThreadId.xy)] = SunShadow(dispatchThreadId.xy, groupThreadId, pSrt, autoDepthAdjust, fixRadius, useHero, USE_VSM, USE_MIPS);
}

[numthreads(8, 8, 1)]
void CS_TemporalAaSunShadowAutoDepthAdjustFixRadiusWithHero(uint2 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, TemporalBlurSunShadowSrt* pSrt : S_SRT_DATA)
{
	const bool autoDepthAdjust = true;
	const bool fixRadius = true;
	const bool useHero = true;

	g_shadowSamplePattern[groupIndex] = pSrt->pSamplePattern->samples[groupIndex];

	pSrt->rwbShadowInfo[int2(dispatchThreadId.xy)] = SunShadow(dispatchThreadId.xy, groupThreadId, pSrt, autoDepthAdjust, fixRadius, useHero, USE_VSM, USE_MIPS);
}
