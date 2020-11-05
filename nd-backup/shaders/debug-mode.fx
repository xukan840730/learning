/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#define ND_PSSL

#include "global-funcs.fxi"
#include "compressed-vsharp.fxi"
#include "compressed-tangent-frame.fxi"

//------------------------------------------------------------------------------------------------------------

struct Interpolators
{
	float4 positionHS : S_POSITION;
	float4 positionWS : TEXCOORD1;
	float3 normalWS   : TEXCOORD2;
	custominterp float4 customPositionWS : TEXCOORD0;
	custominterp float4 customPositionHS : TEXCOORD3;
};

//------------------------------------------------------------------------------------------------------------

struct ViewMatrices
{
	row_major float4x4 localToWorld;
	row_major float4x4 localToScreen;
};

struct DebugModeSrt 
{
	CompressedVSharp position;
	DataBuffer<float3> normal;
	uint4 globalVSharp;
	RegularBuffer<ViewMatrices> matrices;
	uint numVertices;
};

float3 Rotate(float3 aVector, row_major float4x4 mtx)
{
	float3 ret;
	ret.x = dot(mtx[0].xyz, aVector.xyz);
	ret.y = dot(mtx[1].xyz, aVector.xyz);
	ret.z = dot(mtx[2].xyz, aVector.xyz);
	return ret.xyz;
}

Interpolators Vs_DebugMode(DebugModeSrt *pSrt : S_SRT_DATA, uint vertexId : S_VERTEX_ID, uint instanceId : S_INSTANCE_ID)
{
	float4 positionLS = float4(LoadVertexAttribute<float3, 72>(pSrt->globalVSharp, pSrt->position, vertexId, instanceId, pSrt->numVertices), 1.0f);

	float3 normalLS;
	LoadCompressedTangentFrameN(pSrt->normal, normalLS, vertexId, instanceId, pSrt->numVertices);
 
	float4 positionWS = mul(pSrt->matrices[instanceId].localToWorld, positionLS);
	float4 positionHS = mul(pSrt->matrices[instanceId].localToScreen, positionLS);

	float3 normalWS = normalize(Rotate(normalLS, pSrt->matrices[instanceId].localToWorld));

	Interpolators result;
   
	result.positionHS = positionHS;
	result.positionWS = positionWS;
	result.normalWS   = normalWS;
	result.customPositionHS = positionHS;
	result.customPositionWS = positionWS;

	return result;
}

//------------------------------------------------------------------------------------------------------------

struct WhiteSrt
{
	float4 m_color;
};

float4 Ps_OutputWhite(Interpolators interp, WhiteSrt srt : S_SRT_DATA) : S_TARGET_OUTPUT
{
	float upVec = interp.normalWS.y * 0.5 + 0.5;
	return srt.m_color * lerp(0.6, 1.0, upVec);
}


//------------------------------------------------------------------------------------------------------------

float3 Grid(float3 pos)
{
	float rad = 0.125/2;
	float3 m = frac(pos+rad);

	return (all(m > rad) ? kSlightlyDesatViolet : (m.x <= rad || m.z <= rad) ? kWhite : kLightGrayishViolet);
}

float3 GridMulti(float3 pos) 
{
	float3 dx = ddx(pos);
	float3 dy = ddy(pos);

#if 1
	float3 ofs = dx / 4 - dy / 4;

	float3 sum = float3(0, 0, 0);
	float r = 0.5/2;
	
	sum += Grid(pos - 1 * r * dx + 2 * r * dy + ofs);
	sum += Grid(pos + 2 * r * dx - 1 * r * dy + ofs);
	sum += Grid(pos + 1 * r * dx - 2 * r * dy + ofs);
	sum += Grid(pos - 2 * r * dx - 1 * r * dy + ofs);

	return sum / 4;
#else
	float3 ofs = dx / 8 - dy / 8;

	float3 sum = float3(0, 0, 0);
	float r = 0.5/4;
	
	sum += Grid(pos - 1 * r * dx + 4 * r * dy + ofs);
	sum += Grid(pos + 3 * r * dx + 3 * r * dy + ofs);
	sum += Grid(pos - 3 * r * dx + 2 * r * dy + ofs);
	sum += Grid(pos + 2 * r * dx + 1 * r * dy + ofs);
	sum += Grid(pos - 2 * r * dx + 0 * r * dy + ofs);
	sum += Grid(pos + 3 * r * dx - 1 * r * dy + ofs);
	sum += Grid(pos - 4 * r * dx - 2 * r * dy + ofs);
	sum += Grid(pos + 1 * r * dx - 3 * r * dy + ofs);

	return sum / 8;
#endif
}

float4 Ps_Grid(Interpolators interp) : SV_Target
{
	float4 color;
	float3 L = float3(0, 1, 0);
	float3 N = interp.normalWS;
	float light = 0.125 + (1 - 0.125) * (0.5 + 0.5 * dot(N, L)); 
	color.rgb = light * GridMulti(interp.positionWS.xyz);
	color.a = 1.0;
	return color;
}

//------------------------------------------------------------------------------------------------------------

float4 Ps_Book(Interpolators interp) : SV_Target
{
	float4 color;
	float3 L = float3(0, 1, 0);
	float3 N = interp.normalWS;
	float light = 0.125 + (1 - 0.125) * (0.5 + 0.5 * dot(N, L)); 
	color.rgb = light * kSlightlyDesatViolet;
	color.a = 1.0;
	return color;
}

//------------------------------------------------------------------------------------------------------------

float4 Ps_BookFaceNormals(Interpolators interp) : SV_Target
{
	float4 color;
	float3 L = float3(0, 1, 0);
	float3 p0 = GetParameterP0(interp.customPositionWS).xyz;
	float3 p1 = GetParameterP1(interp.customPositionWS).xyz;
	float3 p2 = GetParameterP2(interp.customPositionWS).xyz;
	float3 N = normalize(cross(p1-p0, p2-p0));
	float light = 0.125 + (1 - 0.125) * (0.5 + 0.5 * dot(N, L)); 
	color.rgb = light * kSlightlyDesatViolet;
	color.a = 1.0;
	return color;
}

//------------------------------------------------------------------------------------------------------------

float Len(float3 d) 
{
	return sqrt(dot(d, d));
}

#if 0
float4 Ps_Density(Interpolators interp) : SV_Target
{
	float3 p0 = GetParameterP0(interp.customPositionWS).xyz;
	float3 p1 = GetParameterP1(interp.customPositionWS).xyz;
	float3 p2 = GetParameterP2(interp.customPositionWS).xyz;
	float area = Len(cross(p1-p0, p2-p0))*0.5f;
	float4 color;
	color.rgb = saturate(area * (100.0f*100.0f/50.0f)); // 50 cm^2 = 1.0
	color.a = 1.0;
	return color;
}
#else
float4 Ps_Density(Interpolators interp) : SV_Target
{
	float2 ss = float2(1, 1); //float2(1920, 1080);
	float4 h0 = GetParameterP0(interp.customPositionHS);
	float4 h1 = GetParameterP1(interp.customPositionHS);
	float4 h2 = GetParameterP2(interp.customPositionHS);
	float2 p0 = h0.xy / h0.w * ss;
	float2 p1 = h1.xy / h1.w * ss;
	float2 p2 = h2.xy / h2.w * ss;
	float area = abs(p0.x * (p1.y - p2.y) + p1.x * (p2.y - p0.y) + p2.x * (p0.y - p1.y)) * 0.5f;
	// Magic numbers from here onward
	float val = sqrt(area)*70.0f; 
	float4 color = float4(GetHeatmapColor(sqrt(1.0 / val - 0.05)), 1.0);
	return color;
}
#endif

//------------------------------------------------------------------------------------------------------------

float4 Edge(float2 bary, float4 color1, float4 color2, float wf)
{
	if (bary.x <= wf || bary.y <= wf || (bary.x + bary.y >= 1.0-wf))
		return color1;
	else
		return color2;
}

float4 Ps_BookEdge(Interpolators interp) : SV_Target
{
	float3 L = float3(0, 1, 0);
	float3 N = interp.normalWS;
	float light = 0.125 + (1 - 0.125) * (0.5 + 0.5 * dot(N, L)); 

//	float2 bary = float2(__getSpecialVgprFloat(__v_i_persp_sample),
//	                     __getSpecialVgprFloat(__v_j_persp_sample));
	float2 bary = float2(__getSpecialVgprFloat(__v_i_persp_center),
	                     __getSpecialVgprFloat(__v_j_persp_center));

	float2 dx = ddx(bary);
	float2 dy = ddy(bary);

	float4 sum = float4(0.0f, 0.0f, 0.0f, 1.0f);
	float4 clr1 = float4(0.0f, 0.0f, 0.0f, 1.0f);
	float4 clr2 = float4(light * kSlightlyDesatViolet, 1.0f);
	float wf = 0.05f;

#if 0
	//    0 |
	//      |   1
	// -+-+-+-+-+-
	//  3   |
	//      | 2
	float r = 0.25f;
	float2 ofs = r * (dx - dy);
	float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
	color += Edge(bary - 1 * r * dx + 2 * r * dy + ofs, clr1, clr2, wf);
	color += Edge(bary + 2 * r * dx + 1 * r * dy + ofs, clr1, clr2, wf);
	color += Edge(bary + 1 * r * dx - 2 * r * dy + ofs, clr1, clr2, wf);
	color += Edge(bary - 2 * r * dx - 1 * r * dy + ofs, clr1, clr2, wf);
	return color * 0.25f;
#else
	// "Queens" pattern
	float r = 0.125f;
	float2 ofs = r * (dx - dy);
	float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
	color += Edge(bary - 1 * r * dx + 4 * r * dy + ofs, clr1, clr2, wf); // 0
	color += Edge(bary + 3 * r * dx + 3 * r * dy + ofs, clr1, clr2, wf); // 1
	color += Edge(bary - 3 * r * dx + 2 * r * dy + ofs, clr1, clr2, wf); // 2
	color += Edge(bary + 2 * r * dx + 1 * r * dy + ofs, clr1, clr2, wf); // 3
	color += Edge(bary - 2 * r * dx + 0 * r * dy + ofs, clr1, clr2, wf); // 4
	color += Edge(bary + 3 * r * dx - 1 * r * dy + ofs, clr1, clr2, wf); // 5
	color += Edge(bary - 4 * r * dx - 2 * r * dy + ofs, clr1, clr2, wf); // 6
	color += Edge(bary + 1 * r * dx - 3 * r * dy + ofs, clr1, clr2, wf); // 7
	return color * (1.0f/8);
#endif
}

//------------------------------------------------------------------------------------------------------------

void Ps_Overdraw(float2 screenPos : S_POSITION, RW_Texture2D<uint> debugTex : S_SRT_DATA)
{
	uint2 screenCoords = (uint2)screenPos;

	AtomicAdd(debugTex[screenCoords], 1);
}

//------------------------------------------------------------------------------------------------------------
struct TimingSrt
{
	unsigned long *pTimestampBegin;
	unsigned long *pTimestampEnd;
	unsigned long *pGBufferBegin;
	unsigned long *pGBufferEnd;
	OcclusionQueryResults *pOcclusionResults;
};

float4 Ps_GBufferTimingPerPixel(TimingSrt *pSrt : S_SRT_DATA) : S_TARGET_OUTPUT0
{
	const unsigned long deltaGBufferTicks = *pSrt->pGBufferEnd - *pSrt->pGBufferBegin;
	//const float deltaGBufferMs = deltaGBufferTicks / 800000.f;
	const float deltaGBufferMs = 8.0f;	// Based on 8ms nonparallel budget
	const float overheadFudgeFactor = 0.006f;	// Magic number, to account for nonparallel overhead
	const int minPixelFudgeFactor = 256;		// Magic number, to account for nonparallel overhead

	const unsigned long deltaTicks = *pSrt->pTimestampEnd - *pSrt->pTimestampBegin;
	const float deltaMs = deltaTicks / 800000.0f;
	const float percentTime = max(deltaMs - overheadFudgeFactor, 0.0001f) / deltaGBufferMs;

	// Count how many pixels we rendered
	unsigned long totalCount = 0;
	for (int i = 0; i < 8; ++i)
	{
		totalCount += (pSrt->pOcclusionResults->m_results[i].m_zPassCountEnd - pSrt->pOcclusionResults->m_results[i].m_zPassCountBegin);
	}
	totalCount = totalCount + minPixelFudgeFactor;	
	const float percentScreen = totalCount / (1920*1080.f);

	return float4(GetHeatmapColor(sqrt(2.0 * percentTime / percentScreen)), 1.0);	// Magic number 2.0
}

float4 Ps_GBufferTiming(TimingSrt *pSrt : S_SRT_DATA) : S_TARGET_OUTPUT0
{
	const unsigned long deltaGBufferTicks = *pSrt->pGBufferEnd - *pSrt->pGBufferBegin;
	//const float deltaGBufferMs = deltaGBufferTicks / 800000.f;
	const float deltaGBufferMs = 0.1f;		//	Magic number, absolute duration seems to work better than relative

	const unsigned long deltaTicks = *pSrt->pTimestampEnd - *pSrt->pTimestampBegin;
	const float deltaMs = deltaTicks / 800000.0f;
	const float percentTime = deltaMs / deltaGBufferMs;

	return float4(GetHeatmapColor(percentTime), 1.0);		
}

float4 Ps_GBufferPixels(TimingSrt *pSrt : S_SRT_DATA) : S_TARGET_OUTPUT0
{
	// Count how many pixels we rendered
	unsigned long totalCount = 0;
	for (int i = 0; i < 8; ++i)
	{
		totalCount += (pSrt->pOcclusionResults->m_results[i].m_zPassCountEnd - pSrt->pOcclusionResults->m_results[i].m_zPassCountBegin);
	}
	const float percentScreen = 32.0f * totalCount / (1920*1080.f);	// Magic number 32.0
	
	return float4(GetHeatmapColor(sqrt(percentScreen)), 1.0);	
}
