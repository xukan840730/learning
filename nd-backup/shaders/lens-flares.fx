//--------------------------------------------------------------------------------------
// File: lens-flares.fx
//
// Copyright (c) 2014 Naughty Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "global-funcs.fxi"
#include "post-globals.fxi"

struct PS_VPosPos
{
    float4 ViewPos	: SV_POSITION;
    float4 Pos		: TEXCOORD0;
};

#define kMaxCascadeLevels	8
struct SunlightShadowConstants
{
	float4				m_cascadedLevelDist[kMaxCascadeLevels/4];
	float4				m_parameters0[kMaxCascadeLevels];
	float4				m_parameters1[kMaxCascadeLevels];
	float4				m_matricesRows[kMaxCascadeLevels*3];
	float4				m_blurParams;
	float4				m_otherParams;
	float				m_cascadedZoffset[kMaxCascadeLevels];
	float4				m_temporalParameters1[kMaxCascadeLevels];
	float4				m_shadowParams;
	float4				m_shadowInfo;
	float				m_shadowSampleParams[4];
	float2				m_samples[16];
	float4				m_clipPlaneWS[2];
};

float NdFetchShadowMip0(float4 coords, Texture2DArray txShadowBuffer, SamplerComparisonState sSamplerShadow)
{
	return txShadowBuffer.SampleCmpLOD0(sSamplerShadow, coords.xyz, coords.w).r;
}

//--------------------------------------------------------------------------------------
float SunShadow(float4 positionWS, SunlightShadowConstants *pSunShadowConstants,
                Texture2DArray txShadowBuffer, SamplerComparisonState sSamplerShadow)
{
	// Simplified from SunShadow in layered-shader/misc.fxi

	float resultShadow = 1.0f;
	float depthVS = positionWS.w;

	float distToClipPlane = max(abs(dot(float4(positionWS.xyz, 1.0f), pSunShadowConstants->m_clipPlaneWS[0])), 
								abs(dot(float4(positionWS.xyz, 1.0f), pSunShadowConstants->m_clipPlaneWS[1])));

	float4 cmpResult0 = distToClipPlane > pSunShadowConstants->m_cascadedLevelDist[0].xyzw;
	float4 cmpResult1 = distToClipPlane > pSunShadowConstants->m_cascadedLevelDist[1].xyzw;
	uint iCascadedLevel = (uint)dot(cmpResult0, 1.0) + (uint)dot(cmpResult1, 1.0);
	int baseMatrixIdx = iCascadedLevel*3;

	float cascadedDepthRange = pSunShadowConstants->m_parameters0[iCascadedLevel].x;
	float cascadedDepthOffset = pSunShadowConstants->m_parameters0[iCascadedLevel].y;

	float4 shadowTexCoord;
	shadowTexCoord.x = dot(float4(positionWS.xyz, 1.0), pSunShadowConstants->m_matricesRows[baseMatrixIdx+0]);
	shadowTexCoord.y = dot(float4(positionWS.xyz, 1.0), pSunShadowConstants->m_matricesRows[baseMatrixIdx+1]);
	shadowTexCoord.z = (float)iCascadedLevel;
	shadowTexCoord.w = dot(float4(positionWS.xyz, 1.0), pSunShadowConstants->m_matricesRows[baseMatrixIdx+2]);

	float distFromLightSrc = shadowTexCoord.w * cascadedDepthRange + cascadedDepthOffset;
	float adjDist = length(float2(ddx(distFromLightSrc), ddy(distFromLightSrc))) / cascadedDepthRange;

	float sumShadowInfo = 0;
	float depthWithOffset = shadowTexCoord.w - (min(adjDist, 20.0f / 65536.0f) + 5.0f / 65536.0f) * 4.0f;

	shadowTexCoord.w = depthWithOffset;
	sumShadowInfo = NdFetchShadowMip0(shadowTexCoord, txShadowBuffer, sSamplerShadow);

	float invShadowInfo = 1.0f - sumShadowInfo;
	float finalShadowInfo = saturate((1.0f - invShadowInfo * invShadowInfo) / 0.75f);

	resultShadow = finalShadowInfo;

	return resultShadow;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
struct VsFlareConsts
{
    matrix		m_worldViewProjMat;
};

struct VsFlareBufs
{
	DataBuffer<float3> pos;
	DataBuffer<float2> tex;
};

struct VsFlareSrt
{
	VsFlareConsts *pConsts;	
	VsFlareBufs *pBufs;
};

PS_VPosPos VS_FlareShadowSample(VsFlareSrt srt : S_SRT_DATA, uint vertexIdx : S_VERTEX_ID)
{
	PS_VPosPos output;

	float3 inputPos = srt.pBufs->pos[vertexIdx];
	float2 inputTex = srt.pBufs->tex[vertexIdx];

	float4 hPos = mul(float4(inputPos, 1.0), srt.pConsts->m_worldViewProjMat);

	output.ViewPos = float4(inputTex, 0.5f, 1.0f);
	output.Pos = float4(inputPos, hPos.w);
	return output;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

struct PsFlareSrt
{
	SunlightShadowConstants *pConstants;
	Texture2DArray *pTexture;
	SamplerComparisonState *pSampler;
};

float4 PS_FlareShadowSample(PS_VPosPos input, PsFlareSrt srt : S_SRT_DATA) : SV_Target
{
	float shadow = SunShadow(input.Pos, srt.pConstants, *srt.pTexture, *srt.pSampler);
	return float4(shadow, 1-shadow, 0, 1);
}

//--------------------------------------------------------------------------------------
// Compute Shader
//--------------------------------------------------------------------------------------

struct BrightnessCalcSrt
{
	Texture2D<float4>  tex_test;
	RWTexture2D<float> rwt_bright;
};

groupshared float g_accumVals[64];

[numthreads(64, 1, 1)]
void CS_FlareBrightnessCalc(uint3 dtId : SV_GroupThreadID, 
							uint3 groupId : SV_GroupID,
                            BrightnessCalcSrt *pSrt : S_SRT_DATA)
{
	float colTotal = 0.0f;

	[loop]
	for (uint iCol = 0; iCol < 64; iCol++)
	{
		colTotal += pSrt->tex_test[uint2(dtId.x, iCol)].x;
	}
	g_accumVals[dtId.x] = colTotal;

	// Reduce
	[loop]
	for (uint iNumThread = 32; iNumThread > 1; iNumThread = iNumThread >> 1)
	{
		if (dtId.x < iNumThread)
		{
			g_accumVals[dtId.x] += g_accumVals[dtId.x+iNumThread];
		}
	}
	if (dtId.x == 0)
	{
		pSrt->rwt_bright[uint2(groupId.x,0)] = (g_accumVals[0] + g_accumVals[1]) / (64.0f*64.0f);
	}
}

//--------------------------------------------------------------------------------------
// Compute Shader
//--------------------------------------------------------------------------------------

struct BrightSampleDesc
{
	float2	m_topLeft;
	float2	m_bottomRight;
	float	m_testDepth;
	float m_newMethod;
	float	m_pad[2];
};

struct BrightnessCalcDepthSrt
{
	float4								m_projectionParamsZ;
	uint								m_sampleIdx;
	StructuredBuffer<BrightSampleDesc>	m_bufSamples;
	Texture2D<float>					m_depthTex;
	RWTexture2D<float>					m_rwBrightTex;
};

[numthreads(64, 1, 1)]
void CS_FlareBrightnessCalcDepth(uint3 dtId : SV_GroupThreadID, 
								 uint3 groupId : SV_GroupID,
                                 BrightnessCalcDepthSrt *pSrt : S_SRT_DATA)
{
	uint				idx = groupId.x + 1;
	BrightSampleDesc	bs = pSrt->m_bufSamples[idx];
	float2				fsize = bs.m_bottomRight - bs.m_topLeft;
	uint2				topLeft = (uint2)(floor(bs.m_topLeft));
	uint2				size = (uint2)fsize;

#define NEW_VER_FLARES  1
#if NEW_VER_FLARES
	uint2 bottomRight = (uint2)(floor(bs.m_bottomRight));
	if (bs.m_newMethod > 0.0f)
	{
		size = bottomRight - topLeft + uint2(1, 1);
	}
	
	float xStartFactor = 1.0 - (bs.m_topLeft.x - topLeft.x);
	float yStartFactor = 1.0 - (bs.m_topLeft.y - topLeft.y);

	float xEndFactor = (bs.m_bottomRight.x - bottomRight.x);
	float yEndFactor = (bs.m_bottomRight.y - bottomRight.y);
#endif

	
	float colTotal = 0.0f;

	uint numLoops = (size.x + 63) / 64;
	for (uint i = 0; i < numLoops; ++i)
	{
		uint iCol = i * 64 + dtId.x;
		float xFactor = (iCol == 0) ? (xStartFactor) : ((iCol == size.x - 1) ? xEndFactor : 1.0f);
		if (iCol < size.x)
		{
			for (uint iRow = 0; iRow < size.y; ++iRow)
			{
				float yFactor = (iRow == 0) ? (yStartFactor) : ((iRow == size.y - 1) ? yEndFactor : 1.0f);

				uint2 samp = uint2(iCol, iRow) + topLeft;
				float screenZ = pSrt->m_depthTex[samp];
				float viewZ = pSrt->m_projectionParamsZ.y / (screenZ - pSrt->m_projectionParamsZ.x);

				float val = ((viewZ > bs.m_testDepth) ? 1.0f : 0.0f);

				#if NEW_VER_FLARES
				if (bs.m_newMethod > 0.0f)
				{
					val *= (xFactor * yFactor);
				}
				#endif
				colTotal += val;
			}
		}
	}

	g_accumVals[dtId.x] = colTotal;

	// Do not need a barrier here, because the threadgroup is exactly one wavefront in size

	// Reduce
	for (uint iNumThread = 32; iNumThread > 1; iNumThread = iNumThread >> 1)
	{
		if (dtId.x < iNumThread)
		{
			g_accumVals[dtId.x] += g_accumVals[dtId.x + iNumThread];
		}
	}
	if (dtId.x == 0)
	{
		pSrt->m_rwBrightTex[uint2(idx, 0)] = (g_accumVals[0] + g_accumVals[1])  / (fsize.y * fsize.x);
	}
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_DebugScreenSample(PS_PosTex input, BrightnessCalcDepthSrt *pSrt : S_SRT_DATA) : SV_Target
{
	uint				idx = pSrt->m_sampleIdx;
	BrightSampleDesc	bs = pSrt->m_bufSamples[idx];
 	float2				size = bs.m_bottomRight - bs.m_topLeft;
 	float2				uv = bs.m_topLeft + size*input.Tex;

	float screenZ = pSrt->m_depthTex[(uint2)uv];
	float viewZ = pSrt->m_projectionParamsZ.y / (screenZ - pSrt->m_projectionParamsZ.x);

	float visible = (viewZ > bs.m_testDepth) ? 1.0 : 0.0;
	return float4(1 - visible, visible, 0, 1);
}

