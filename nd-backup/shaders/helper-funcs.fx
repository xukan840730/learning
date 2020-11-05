//--------------------------------------------------------------------------------------
// File: helper-funcs.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "packing.fxi"
#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "global-const-buffers.fxi"
#include "post-processing-common.fxi"
#include "oit.fxi"
#include "quat.fxi"
#include "texture-table.fxi"

#define ND_PSSL
#include "compressed-vsharp.fxi"


#define kShadowDensityBufferSize 1024

cbuffer vs3dGlobalParams : register( b0 )
{
    float4		g_cubemapLookAtPos;
	float4		g_scaleOffsetZ;
};

struct MsaaResolveConst
{
	float		m_linearDepthParamsX;
	float		m_linearDepthParamsY;
	float		m_invMsaaSampleCount;
	int			m_msaaSampleCount;
};

struct PS_PosOnlyViewPortInput
{
	float4 Pos		: SV_POSITION;
	uint   RtIndex	: SV_RenderTargetArrayIndex;
};

struct Vs_OutputLocalGaussian
{
	float4	Pos			: SV_POSITION;
	float4	uv1_uv2     : TEXCOORD0;
	float4	uv3_uv4     : TEXCOORD1;
	float4	uv5_uv6     : TEXCOORD2;
	float4	uv7_uv8     : TEXCOORD3;
	float4	uv9_uva     : TEXCOORD4;
	float4	uvb_uvc     : TEXCOORD5;
	float2	Tex			: TEXCOORD6;
};

struct Vs_OutputLocalGaussian5x5Bilin
{
	float4	Pos		: SV_POSITION;
	float4	uvLeft_uvRight	: TEXCOORD0;
	float2	uvCenter		: TEXCOORD1;
};

struct Vs_OutputCopyCascadeRegion
{
	float4	Pos		: SV_POSITION;
	float3	PosWS	: POSITION_WS;
};

struct Vs_OutputFurTbn
{
	float4	Pos		: SV_POSITION;
	float3	Normal	: NORMAL;
	float4	Tangent	: TANGENT;
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_PosTex VS_SimpleQuad(uint id : SV_VertexID)
{
	PS_PosTex Output;

	// Cool trick to do a full screen quad without input info
	// http://www.altdevblogaday.com/2011/08/08/interesting-vertex-shader-trick/

    Output.Tex = float2((id << 1) & 2, id & 2);
    Output.Pos = float4(Output.Tex * float2(2,-2) + float2(-1,1), 0, 1);
	return Output;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_Pos VS_StencilTetraPattern(uint id : SV_VertexID)
{
	PS_Pos Output;

	const float2 vertexPositions[6] = 
	{
		float2(-1.0f, -1.0f), float2(0.0f, 0.0f), float2(1.0f, -1.0f),
		float2(-1.0f, 1.0f), float2(0.0f, 0.0f), float2(1.0f, 1.0f)
	};

	Output.Pos = float4(vertexPositions[id], 0.0f, 1.0f);
	
	return Output;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_PosTex VS_ScreenSpace(ScreenSpaceVsSrt srt : S_SRT_DATA, uint vertexIdx : S_VERTEX_ID)
{
	PS_PosTex output = (PS_PosTex)0;
	output.Pos.xy = mul(float4(srt.pBufs->pos[vertexIdx], 1.0f), srt.pSs->m_screenMat).xy;
	output.Pos.zw = float2(0.0f, 1.0f);
	output.Tex = srt.pBufs->tex[vertexIdx].xy;

	return output;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------

struct AppDataColorOnlySrt
{
	DataBuffer<float2> pos;
};

PS_Pos VS_GivenScreenNearPos(AppDataColorOnlySrt srt : S_SRT_DATA, uint vertexIdx : S_VERTEX_ID)
{
	float2 inPos = srt.pos[vertexIdx];
	PS_Pos OUT;
	OUT.Pos = float4(inPos.x, inPos.y, -1.0, 1.0);
	return OUT;
}

PS_Pos VS_MaskColorWrite(AppDataColorOnlySrt srt : S_SRT_DATA, uint vertexIdx : S_VERTEX_ID)
{
	float2 inPos = srt.pos[vertexIdx];
	PS_Pos OUT;
	OUT.Pos = float4(inPos.x, inPos.y, 0.0, 1.0);
	return OUT;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------

struct CopyCascadeRegionVsSrt
{
	DataBuffer<float3>	m_vertexBuffer;
	float4				m_currentWorldToScreenRows[2];
};

Vs_OutputCopyCascadeRegion VS_CopyCascadeRegion(CopyCascadeRegionVsSrt srt : S_SRT_DATA, uint vertexIdx : S_VERTEX_ID)
{
	float3 inPos = srt.m_vertexBuffer[vertexIdx];

	Vs_OutputCopyCascadeRegion OUT;
	OUT.PosWS = inPos;
	OUT.Pos = float4(dot(float4(inPos, 1.0f), srt.m_currentWorldToScreenRows[0]),
					 dot(float4(inPos, 1.0f), srt.m_currentWorldToScreenRows[1]),
					 0.0f,
					 1.0f);

	return OUT;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------

struct FurTbnVsSrt
{
	uint4				m_globalVertexBuffer;
	CompressedVSharp	m_texCoords;
	DataBuffer<float3>	m_normals;
	DataBuffer<float4>	m_tangents;
};

Vs_OutputFurTbn VS_FurTbn(FurTbnVsSrt *pSrt : S_SRT_DATA, uint vertexIdx : S_VERTEX_ID)
{
	float2 texCoords = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertexBuffer, pSrt->m_texCoords, vertexIdx);
	float3 normal = pSrt->m_normals[vertexIdx];
	float4 tangent = pSrt->m_tangents[vertexIdx];

	Vs_OutputFurTbn OUT;
	OUT.Pos = float4(texCoords * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
	OUT.Normal	= normal;
	OUT.Tangent = tangent;

	return OUT;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------

struct WorldViewProjMatrix
{
	matrix					m_matrix;
};

struct WorldTransformVsSrt
{
	DataBuffer<float3>		m_posBuf;
	WorldViewProjMatrix*	m_params;
};

PS_Pos VS_WorldToScreen(WorldTransformVsSrt srt: S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	PS_Pos OUT;

	OUT.Pos = mul(float4(srt.m_posBuf[vertexId], 1.0), srt.m_params->m_matrix);
	
	return OUT;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_PosTex4x2 VS_ScreenSpaceDownsampleByHalf(ScreenSpaceVsSrt srt : S_SRT_DATA, 
                                            uint vertexIdx : S_VERTEX_ID)
{
	PS_PosTex4x2 output = (PS_PosTex4x2)0;
	float3 inPos = srt.pBufs->pos[vertexIdx];
	float2 inTex = srt.pBufs->tex[vertexIdx];
	output.Pos.xy = mul(float4(inPos, 1.0f), srt.pSs->m_screenMat).xy;
	output.Pos.zw = float2(0.0f, 1.0f);
	output.uv0_uv1 = inTex.xyxy + float4(-0.5, -0.5, 0.5, -0.5) * srt.pTs->texelSize.xyxy;
	output.uv2_uv3 = inTex.xyxy + float4(-0.5, 0.5, 0.5, 0.5) * srt.pTs->texelSize.xyxy;

	return output;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_PosTex VS_ScreenSpaceGivenZ(ScreenSpaceVsSrt srt : S_SRT_DATA, 
                               uint vertexIdx : S_VERTEX_ID)
{
	PS_PosTex output = (PS_PosTex)0;
	float3 inPos = srt.pBufs->pos[vertexIdx];
	float2 inTex = srt.pBufs->tex[vertexIdx];
	output.Pos = mul(float4(inPos, 1.0f), srt.pSs->m_screenMat);
	output.Tex = inTex.xy;

	return output;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
Vs_OutputLocalGaussian5x5Bilin VS_GaussBlur5x5Horizontal(ScreenSpaceVsSrt srt : S_SRT_DATA, 
                                                         uint vertexIdx : S_VERTEX_ID)
{
	Vs_OutputLocalGaussian5x5Bilin output;
	float3 inPos = srt.pBufs->pos[vertexIdx];
	float2 inTex = srt.pBufs->tex[vertexIdx];

	output.Pos.xy = mul(float4(inPos, 1.0f), srt.pSs->m_screenMat).xy;
	output.Pos.zw = float2(0.0f, 1.0f);
	output.uvCenter = inTex.xy;

	float4 shifts = float4(srt.pTs->texelSize.x, 0.0f, srt.pTs->texelSize.x, 0.0f);

	output.uvLeft_uvRight = output.uvCenter.xyxy + shifts * float4(-1.2, -1.2, 1.2, 1.2);

	return output;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
Vs_OutputLocalGaussian5x5Bilin VS_GaussBlur5x5Vertical(ScreenSpaceVsSrt srt : S_SRT_DATA,
                                                       uint vertexIdx : S_VERTEX_ID)
{
	Vs_OutputLocalGaussian5x5Bilin output;

	float3 inPos = srt.pBufs->pos[vertexIdx];
	float2 inTex = srt.pBufs->tex[vertexIdx];

	output.Pos.xy = mul(float4(inPos, 1.0f), srt.pSs->m_screenMat).xy;
	output.Pos.zw = float2(0.0f, 1.0f);
	output.uvCenter = inTex.xy;

	float4 shifts = float4(0.0f, srt.pTs->texelSize.y, 0.0f, srt.pTs->texelSize.y);

	output.uvLeft_uvRight = output.uvCenter.xyxy + shifts * float4(-1.2, -1.2, 1.2, 1.2);

	return output;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------

struct ScreenSpaceRegionParams
{
	float4 texCoordTransform;
};

struct RegionScreenSpaceVsSrt
{
	ScreenSpaceTransfromConsts *pSs;
	ScreenSpaceRegionParams *pRp;	
	ScreenSpaceBufs *pBufs;	
};

PS_PosTex VS_RegionScreenSpace(RegionScreenSpaceVsSrt srt : S_SRT_DATA,
                               uint vertexIdx : S_VERTEX_ID)
{
	PS_PosTex output = (PS_PosTex)0;

	float3 inPos = srt.pBufs->pos[vertexIdx];
	float2 inTex = srt.pBufs->tex[vertexIdx];

	output.Pos.xy = mul(float4(inPos, 1.0f), srt.pSs->m_screenMat).xy;
	output.Pos.zw = float2(0.0f, 1.0f);
	output.Tex = inTex.xy * srt.pRp->texCoordTransform.xy + srt.pRp->texCoordTransform.zw;

	return output;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------

struct GaussianBlurParams
{
	float4 gaussianBlurParams;
};

struct GaussianBlurVsSrt
{
	ScreenSpaceTransfromConsts *pSs;
	GaussianBlurParams *pGb;	
	ScreenSpaceBufs *pBufs;
};

Vs_OutputLocalGaussian VS_GaussianFilter(GaussianBlurVsSrt srt : S_SRT_DATA, 
                                         uint vertexIdx : S_VERTEX_ID)
{
	Vs_OutputLocalGaussian output = (Vs_OutputLocalGaussian)0;

	float3 inPos = srt.pBufs->pos[vertexIdx];
	float2 inTex = srt.pBufs->tex[vertexIdx];

	output.Pos.xy = mul(float4(inPos, 1.0f), srt.pSs->m_screenMat).xy;
	output.Pos.zw = float2(0.0, 1.0);
	output.uv1_uv2 = inTex.xyxy + srt.pGb->gaussianBlurParams.xyxy * float4(0, 2, -1, 1);
	output.uv3_uv4 = inTex.xyxy + srt.pGb->gaussianBlurParams.xyxy * float4(0, 1, 1, 1);
	output.uv5_uv6 = inTex.xyxy + srt.pGb->gaussianBlurParams.xyxy * float4(-2, 0, -1, 0);
	output.uv7_uv8 = inTex.xyxy + srt.pGb->gaussianBlurParams.xyxy * float4(1, 0, 2, 0);
	output.uv9_uva = inTex.xyxy + srt.pGb->gaussianBlurParams.xyxy * float4(-1, -1, 0, -1);
	output.uvb_uvc = inTex.xyxy + srt.pGb->gaussianBlurParams.xyxy * float4(1, -1, 0, -2);
	output.Tex = inTex.xy;
	return output;
}

struct ObjectIdPsSrt
{
	Texture2D<uint4>		texture;
	SamplerState			samplerState;
};


struct BufferCopyPsSrt
{
	Texture2D<float4>		texture;
	SamplerState			samplerState;
};

struct BufferCopyRemapPsSrt
{
	Texture2D<float4>		texture;
	SamplerState			samplerState;
	float                   srcMin;
	float                   srcMax;
	float                   dstMin;
	float                   dstMax;
};

struct BufferCopyUintPsSrt
{
	Texture2D<uint4>		texture;
	SamplerState			samplerState;
};

struct BufferCopyCubePsSrt
{
	TextureCube<float4>		texture;
	SamplerState			samplerState;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_GaussBlur5x5Bilin(BufferCopyPsSrt* srt : S_SRT_DATA, Vs_OutputLocalGaussian5x5Bilin input) : SV_Target
{
	float4 colorCenter = srt->texture.Sample(srt->samplerState, input.uvCenter) * 0.375;
	float4 colorLeft = srt->texture.Sample(srt->samplerState, input.uvLeft_uvRight.xy) * 0.3125;
	float4 colorRight = srt->texture.Sample(srt->samplerState, input.uvLeft_uvRight.zw) * 0.3125;

	return colorCenter + colorLeft + colorRight;
}

float4 PS_GaussBlur5x5BilinDilate(BufferCopyPsSrt* srt : S_SRT_DATA, Vs_OutputLocalGaussian5x5Bilin input) : SV_Target
{
	float4 colorCenter = srt->texture.Sample(srt->samplerState, input.uvCenter);
	float4 colorLeft = srt->texture.Sample(srt->samplerState, input.uvLeft_uvRight.xy);
	float4 colorRight = srt->texture.Sample(srt->samplerState, input.uvLeft_uvRight.zw);

	return min(colorCenter, min(colorLeft, colorRight));
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_ColorCopy(BufferCopyPsSrt* srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return srt->texture.Sample(srt->samplerState, input.Tex);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_ColorCopyGammaEncode(BufferCopyPsSrt* srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float4 srcColor = srt->texture.Sample(srt->samplerState, input.Tex);
	return float4(srcColor.rgb < 0.03928f / 12.92f ? srcColor.rgb * 12.92f : pow(srcColor.rgb, 1.f / 2.4f) * 1.055f - 0.055f, srcColor.a);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

struct ConstColorPsSrt
{
	float4 constColor;
};

float4 PS_ConstColor(ConstColorPsSrt srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return srt.constColor;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

float4 PS_EliminateFastClear(PS_PosTex input) : SV_Target
{
	return float4(0);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

struct CopySource2PsSrt
{
	Texture2D<float4>		texture0;
	Texture2D<float4>       texture1;
	SamplerState			samplerState;
};

float4 PS_RGDepthCopy(CopySource2PsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float depth = srt->texture1.Sample(srt->samplerState, input.Tex).x;
	float4 rg = srt->texture0.Sample(srt->samplerState, input.Tex);
	return float4(rg.x, rg.y, 1.0 - depth, 1.0);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
struct DownsampleRsmPsSrt
{
	Texture2D<float4>		texture0;
	Texture2D<float4>       texture1;
	SamplerState			samplerPoint;
	SamplerState			samplerLinear;
	float2					texelSize;
};

PixelShaderOutput2 PS_DownsampleRsm(DownsampleRsmPsSrt *srt : S_SRT_DATA, PS_PosTex input)
{
	PixelShaderOutput2 OUT;

	float4 color0 = srt->texture0.Sample(srt->samplerPoint, input.Tex + srt->texelSize * float2(-0.5f, -0.5f));
	float4 color1 = srt->texture0.Sample(srt->samplerPoint, input.Tex + srt->texelSize * float2(0.5f, -0.5f));
	float4 color2 = srt->texture0.Sample(srt->samplerPoint, input.Tex + srt->texelSize * float2(-0.5f, 0.5f));
	float4 color3 = srt->texture0.Sample(srt->samplerPoint, input.Tex + srt->texelSize * float2(0.5f, 0.5f));

	OUT.color0.rgb = color0.rgb * color0.a + color1.rgb * color1.a + color2.rgb * color2.a + color3.rgb * color3.a;
	OUT.color0.a = 1.f;
	OUT.color1 = srt->texture1.Sample(srt->samplerLinear, input.Tex);

	return OUT;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_MaskColorWrite(PS_PosTex input) : SV_Target
{
	return 1.0f;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

struct CopyCascadeRegionPsSrt
{
	RWTexture2D<float>	m_currentShadowMapCascade;
	Texture2D<float>	m_lastShadowMapCascade;
	SamplerState		m_samplerLinear;
	float4				m_lastWorldToScreenRows[2];
	float				m_lastDepthRange;
	float				m_invCurrentDepthRange;
	float				m_cascadeOffset;
};

void PS_CopyCascadeRegion(CopyCascadeRegionPsSrt *srt : S_SRT_DATA, Vs_OutputCopyCascadeRegion input)
{
	float2 shadowTC = float2(dot(float4(input.PosWS, 1.0f), srt->m_lastWorldToScreenRows[0]),
							 dot(float4(input.PosWS, 1.0f), srt->m_lastWorldToScreenRows[1]));
	shadowTC = shadowTC * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
	float lastDepth = srt->m_lastShadowMapCascade.SampleLevel(srt->m_samplerLinear, shadowTC, 0.0f).x;

	// Copy shadow map texels from lower cascade into higher cascade, but only when texels haven't
	// been clamped by the rasterizer.
	if ((lastDepth > 0.0f) && (lastDepth < 1.0f))
	{
		lastDepth = (lastDepth - 0.5f) * srt->m_lastDepthRange;
		lastDepth += srt->m_cascadeOffset;
		lastDepth = (lastDepth * srt->m_invCurrentDepthRange) + 0.5f;

		int2 screenCoords = int2(input.Pos.xy);
		float currentDepth = srt->m_currentShadowMapCascade[screenCoords];
		srt->m_currentShadowMapCascade[screenCoords] = min(currentDepth, lastDepth);
	}
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

uint PS_FurTbn(Vs_OutputFurTbn input)
{
	float3 tangent = normalize(input.Tangent.xyz);
	float3 normal = normalize(input.Normal);
	float handedness = -input.Tangent.w;
	float3 bitangent = cross(normal, tangent) * handedness;
	float3x3 tbn = float3x3(tangent, bitangent, normal);
	return EncodeTbnQuatCayley(tbn, handedness);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_ColorAlphaCopy(CopySource2PsSrt *srt : S_SRT_DATA, PS_PosTex input)
{
	return float4(srt->texture0.Sample(srt->samplerState, input.Tex).rgb, srt->texture1.Sample(srt->samplerState, input.Tex).r);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_ColorCopyBiased(BufferCopyPsSrt* srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return srt->texture.Sample(srt->samplerState, input.Tex) * 0.5f + 0.5f;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_ColorCopyRemap(BufferCopyRemapPsSrt* srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	// Remapping
	float4 t = (srt->texture.Sample(srt->samplerState, input.Tex) - srt->srcMin) / (srt->srcMax - srt->srcMin);
	return (1.0 - t) * srt->dstMin + t * srt->dstMax;
}
																				
//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_ColorCopyCube(BufferCopyCubePsSrt* srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float3 face0Coords = float3(1.0, input.Tex.y * 2.0 - 1.0, input.Tex.x * -2.0 + 1.0);
	return srt->texture.Sample(srt->samplerState, face0Coords);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

struct ColorCopyDecodeNormalRGSrtData
{
	Texture2D<uint4> txDiffuseU; // : register ( t0 );
	SamplerState sSamplerPoint;
};

float4 PS_ColorCopyDecodeNormalRG(ColorCopyDecodeNormalRGSrtData *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return float4(DecodeNormal(f16tof32(srt->txDiffuseU.Sample(srt->sSamplerPoint, input.Tex).rg)), 1);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
struct BufferCopyArrayPsSrt
{
	Texture2DArray<float4>		texture;
	SamplerState				samplerState;
	float4						params;
};

float4 PS_ColorCopyArray(BufferCopyArrayPsSrt* srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return srt->texture.Sample(srt->samplerState, float3(input.Tex, srt->params.x));
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

struct ColorSrtData 
{
	float4 m_color;
};

float4 PS_ColorWithAlphaCopy(ColorSrtData srt : S_SRT_DATA) : SV_Target
{
	return srt.m_color; 
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_ObjectIdDebug(ObjectIdPsSrt* srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	uint objectId = srt->texture.Sample(srt->samplerState, input.Tex).r >> 17;
	return float4(((objectId >> 8) & 0xff) / 255.0f, (objectId & 0xff) / 255.0f, 0, 1);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_PrimitiveIdDebug(ObjectIdPsSrt* srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	uint2 key = srt->texture.Sample(srt->samplerState, input.Tex).rg;
	uint3 primId = uint3((key.x >> 1) & 0xffff, key.y >> 16, key.y & 0xffff);

	uint id0 = min3(primId.x, primId.y, primId.z);
	uint id1 = med3(primId.x, primId.y, primId.z);
	uint id2 = max3(primId.x, primId.y, primId.z);

	return float4(id0 / 65535.0f, id1 / 65535.0f, id2 / 65535.0f, 1);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_PrimitiveIdDebugNeo(ObjectIdPsSrt* srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	uint primId = srt->texture.Sample(srt->samplerState, input.Tex).r;
	return float4(((primId >> 16) & 0x1) * 255.0f, ((primId >> 8) & 0xff) / 255.0f, (primId & 0xff) / 255.0f, 1);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

struct ConstColorTxMultPsSrt
{
	Texture2D<float4>  m_texture;
	SamplerState       m_samplerState;
	float4             m_constColor;
	float4             m_value;
};

float4 PS_ConstColorTxMult(ConstColorTxMultPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float4 txValue = srt->m_value * srt->m_texture.Sample(srt->m_samplerState, input.Tex).x;
	return txValue * srt->m_constColor;
}


//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

struct FsRefractionMsaaData
{
	Texture2DMS<float4> txDiffuseMs; // : register(t0);
	Texture2DMS<float> txDepthMs1; // : register(t1);
};

struct FsRefractionMsaaSrt
{
	MsaaResolveConst *pConsts;
	FsRefractionMsaaData *pData;
};

float4 PS_FsRefractionBuffer(FsRefractionMsaaSrt srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float4 screenPos = input.Pos;
	float4 sumColor = 0;
	float furthestDepth = 0.0f;

	int2 texCoord = int2(screenPos.xy);
	for (int iSample = 0; iSample < srt.pConsts->m_msaaSampleCount; iSample++)
	{
		sumColor += srt.pData->txDiffuseMs.Load(texCoord, iSample);
		float depth = srt.pData->txDepthMs1.Load(texCoord, iSample);
		if (depth <= 1.0f && depth > furthestDepth) furthestDepth = depth;
	}

	float3 avgColor = sumColor.xyz * srt.pConsts->m_invMsaaSampleCount;

	float viewSpaceDepth = (furthestDepth == 0.0f) ? 1.0f : (1.0f / (furthestDepth * srt.pConsts->m_linearDepthParamsX +
	                                                                 srt.pConsts->m_linearDepthParamsY));

	return float4(avgColor, viewSpaceDepth);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

struct FsRefractionNonMsaaData
{
	Texture2D<float4> txDiffuse; // : register(t0);
	Texture2D<float> txDepthBuffer1; // : register(t1);
	SamplerState sSamplerPoint;
	SamplerState sSamplerLinear;
};

struct FsRefractionNonMsaaSrt
{
	MsaaResolveConst *pConsts;
	FsRefractionNonMsaaData *pData;
};

float4 PS_FsRefractionBufferNonMsaa(FsRefractionNonMsaaSrt srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float4 screenPos = input.Pos;
	float4 sumColor = 0;
	float furthestDepth = 0.0f;

	int2 texCoord = int2(screenPos.xy);

	sumColor = srt.pData->txDiffuse.Sample(srt.pData->sSamplerLinear, input.Tex);
	float depth = srt.pData->txDepthBuffer1.Sample(srt.pData->sSamplerPoint, input.Tex);
	if (depth <= 1.0f && depth > furthestDepth) furthestDepth = depth;

	float3 avgColor = sumColor.xyz;

	float viewSpaceDepth = (furthestDepth == 0.0f) ? 1.0f : (1.0f / (furthestDepth * srt.pConsts->m_linearDepthParamsX +
	                                                                 srt.pConsts->m_linearDepthParamsY));

	return float4(avgColor, viewSpaceDepth);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_RedChannelCopy(BufferCopyPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return srt->texture.Sample(srt->samplerState, input.Tex).xxxx;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_GreenChannelCopy(BufferCopyPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return srt->texture.Sample(srt->samplerState, input.Tex).yyyy;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_BlueChannelCopy(BufferCopyPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return srt->texture.Sample(srt->samplerState, input.Tex).zzzz;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_InvRedChannelCopy(BufferCopyPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return 1.0f - srt->texture.Sample(srt->samplerState, input.Tex).xxxx;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_RedChannelCopyNorm(BufferCopyUintPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return srt->texture.Load(int3(input.Tex.xy, 0)).xxxx / 255.0;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_OitObjIdChannelCopy(BufferCopyUintPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	uint2 key = srt->texture.Sample(srt->samplerState, input.Tex).xy;
	uint objId = OitKeyGetObjId(key);
	return float4((objId & 0xff) / 255.0f, ((objId >> 8) & 0xff) / 255.0f, 0.0f, 1.0f);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_OitPrimIdChannelCopy(BufferCopyUintPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	uint2 key = srt->texture.Sample(srt->samplerState, input.Tex).xy;
	uint primId = OitKeyGetPrimId(key);
	return float4((primId & 0xff) / 255.0f, ((primId >> 8) & 0xff) / 255.0f, ((primId >> 16) & 0xff) / 255.0f, 1.0f);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_OitResolveColorChannelCopy(BufferCopyUintPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	uint2 color = srt->texture.Sample(srt->samplerState, input.Tex).xy;
	return OitUnpackColor(color);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_OitResolveAlphaChannelCopy(BufferCopyUintPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	uint2 color = srt->texture.Sample(srt->samplerState, input.Tex).xy;
	return OitUnpackColor(color).aaaa;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
struct ShowStencilBufferSrt
{
	Texture2D<uint> stencilBuffer;
	SamplerState samplerState;
};

float4 PS_ShowIsWaterStencilBit(ShowStencilBufferSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	uint stencilBits = srt->stencilBuffer.Load(int3(input.Tex.xy, 0));
	return (stencilBits & 2) ? float4(0.f, 1.f, 0.f, 1.f) : float4(1.f, 0.f, 0.f, 1.f);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_ShowIsUnshootableStencilBit(ShowStencilBufferSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	uint stencilBits = srt->stencilBuffer.Load(int3(input.Tex.xy, 0));
	return (stencilBits & 4) ? float4(0.f, 1.f, 0.f, 1.f) : float4(1.f, 0.f, 0.f, 1.f);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_ShowGhostingIDStencilBit(ShowStencilBufferSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return (srt->stencilBuffer.Load(int3(input.Tex.xy, 0)) & 0x18) / 255.f;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_RedGreenAsNormalMap(BufferCopyPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return float4(srt->texture.Sample(srt->samplerState, input.Tex).xy, 1.0, 1.0);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
struct CopyAlphaChannel
{
	Texture2D<float4> tSrc;
};

float PS_CopyAlphaChannel(CopyAlphaChannel srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return srt.tSrc.Load(int3(input.Pos.xy, 0)).w;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
struct DownsampleQuarterDepthConst
{
	float4 m_uvOffsets[16];
};

struct DownsampleQuarterDepthData
{
	Texture2D<float> txDepthBuffer;
	SamplerState sSamplerPoint;
};

struct DownsampleQuarterDepthConstSrt
{
	DownsampleQuarterDepthConst *pConsts;
	DownsampleQuarterDepthData *pData;
};

float PS_DownsampleQuarterDepthBuffer(DownsampleQuarterDepthConstSrt srt : S_SRT_DATA, PS_PosTex input) : SV_Depth
{
	float maxDepthBufferZ = 0.f;

	for (uint i = 0; i < 16; i++)
	{
		float2 uv = input.Tex + srt.pConsts->m_uvOffsets[i].xy;
		float depth = srt.pData->txDepthBuffer.Sample(srt.pData->sSamplerPoint, uv).x;
		maxDepthBufferZ = max(maxDepthBufferZ, depth);
	}

	return maxDepthBufferZ;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float PS_CopyDepthBuffer(BufferCopyPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Depth
{
	return srt->texture.Sample(srt->samplerState, input.Tex).x;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
struct DebugShadowBufferSrtData
{
	Texture2DArray txDebugShadowBuffer; 
	Texture2DArray txMaskBuffer;
	SamplerState sSamplerLinear;
	float4 shadowBufferIdx;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
struct DebugLinearShadowBufferSrtData
{
	LocalShadowTable* pLocalShadowTextures;
	SamplerState sSamplerLinear;
	uint localShadowTextureIdx;
	uint numShadowTextures;
	float shadowBufferNear;
	float shadowBufferFar;
};

float4 PS_DebugLinearShadowBuffer(DebugLinearShadowBufferSrtData *pSrtData : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float result = 0.0f;
	if (pSrtData->localShadowTextureIdx < pSrtData->numShadowTextures)
	{
		float zbufferDepth = pSrtData->pLocalShadowTextures->SampleLevel(pSrtData->sSamplerLinear, input.Tex, 0.0f, pSrtData->localShadowTextureIdx);
		float linearDepth = pSrtData->shadowBufferNear * pSrtData->shadowBufferFar / (pSrtData->shadowBufferFar - zbufferDepth * (pSrtData->shadowBufferFar - pSrtData->shadowBufferNear));
		result = 1.0f - linearDepth / pSrtData->shadowBufferFar;
	}
	return result;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_DebugSunShadowBuffer(DebugShadowBufferSrtData *pSrtData : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float3 uvCoords = float3(input.Tex.x, 1.0f - input.Tex.y, pSrtData->shadowBufferIdx.x);
	float4 finalColor = 0;
	if (pSrtData->txMaskBuffer.Sample(pSrtData->sSamplerLinear, uvCoords).x > 0.5f)
		finalColor = pSrtData->txDebugShadowBuffer.Sample(pSrtData->sSamplerLinear, uvCoords).x;

	return finalColor;
}

float4 PS_DebugVsmSunShadowBuffer(DebugShadowBufferSrtData *pSrtData : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float3 uvCoords = float3(input.Tex.x, 1.0f - input.Tex.y, pSrtData->shadowBufferIdx.x);
	float4 finalColor = 0;
	if (pSrtData->txMaskBuffer.Sample(pSrtData->sSamplerLinear, uvCoords).x > 0.5f)
		finalColor.xy = pSrtData->txDebugShadowBuffer.Sample(pSrtData->sSamplerLinear, uvCoords).xy;

	return finalColor;
}


//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS_AlphaChannelCopy(BufferCopyPsSrt *pSrtData : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	return pSrtData->texture.Sample(pSrtData->samplerState, input.Tex).wwww;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

// :NOTE: Exactly the same layout as BufferCopyPsSrt, so CPU side is similar
struct CubemapCopySrtData 
{
	TextureCube txCubemap; //: register( t0 );
	SamplerState sSamplerLinear;
};

float4 PS_CubemapCopy(CubemapCopySrtData *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float angle = input.Tex.x * 3.1415926f * 2.0f;

	float3 cubeRay = float3(cos(angle), input.Tex.y * 2.0f - 1.0f, sin(angle));

	return srt->txCubemap.Sample(srt->sSamplerLinear, normalize(cubeRay));
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

// :NOTE: Exactly the same layout as BufferCopyPsSrt, so CPU side is similar
struct ShowU32CountBufferSrtData 
{
	Texture2D<uint> tU32CountBuffer; // : register ( t0 );
	SamplerState sSamplerLinear; // unused
};

float4 PS_ShowU32CountBuffer(ShowU32CountBufferSrtData *pSrtData : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	uint2 clampedTexCoord =
	    clamp((uint2)(input.Tex.xy * kShadowDensityBufferSize), (uint2)0, (uint2)(kShadowDensityBufferSize - 1));
	uint count = pSrtData->tU32CountBuffer[clampedTexCoord];

	return count > 0 ? 1.0f : 0.0f;
}

//--------------------------------------------------------------------------------------
// Cubemap geometry shader test code
//--------------------------------------------------------------------------------------
[maxvertexcount(18)] void GS_MultiViewportCubemap(triangle PS_Pos input[3],
                                                  inout TriangleStream<PS_PosOnlyViewPortInput> triangleStream)
{
	PS_PosOnlyViewPortInput psInput;

	float4 posRay0 = input[0].Pos - g_cubemapLookAtPos;
	float4 posRay1 = input[1].Pos - g_cubemapLookAtPos;
	float4 posRay2 = input[2].Pos - g_cubemapLookAtPos;

	float3 viewPZ0 = posRay0.xyz * g_scaleOffsetZ.x + g_scaleOffsetZ.y;
	float3 viewPZ1 = posRay1.xyz * g_scaleOffsetZ.x + g_scaleOffsetZ.y;
	float3 viewPZ2 = posRay2.xyz * g_scaleOffsetZ.x + g_scaleOffsetZ.y;

	float3 viewNZ0 = -posRay0.xyz * g_scaleOffsetZ.x + g_scaleOffsetZ.y;
	float3 viewNZ1 = -posRay1.xyz * g_scaleOffsetZ.x + g_scaleOffsetZ.y;
	float3 viewNZ2 = -posRay2.xyz * g_scaleOffsetZ.x + g_scaleOffsetZ.y;

	// face 0. (x, y, z)
	psInput.RtIndex = 0;
	psInput.Pos = float4(-posRay0.x, posRay0.y, viewPZ0.z, posRay0.z);
	triangleStream.Append(psInput);
	psInput.Pos = float4(-posRay1.x, posRay1.y, viewPZ1.z, posRay1.z);
	triangleStream.Append(psInput);
	psInput.Pos = float4(-posRay2.x, posRay2.y, viewPZ2.z, posRay2.z);
	triangleStream.Append(psInput);
	triangleStream.RestartStrip();

	// face 1. (-x, y, -z).
	psInput.RtIndex = 1;
	psInput.Pos = float4(posRay0.x, posRay0.y, viewNZ0.z, -posRay0.z);
	triangleStream.Append(psInput);
	psInput.Pos = float4(posRay1.x, posRay1.y, viewNZ1.z, -posRay1.z);
	triangleStream.Append(psInput);
	psInput.Pos = float4(posRay2.x, posRay2.y, viewNZ2.z, -posRay2.z);
	triangleStream.Append(psInput);
	triangleStream.RestartStrip();

	// face 2. (x, -z, y).
	psInput.RtIndex = 2;
	psInput.Pos = float4(-posRay0.x, -posRay0.z, viewPZ0.y, posRay0.y);
	triangleStream.Append(psInput);
	psInput.Pos = float4(-posRay1.x, -posRay1.z, viewPZ1.y, posRay1.y);
	triangleStream.Append(psInput);
	psInput.Pos = float4(-posRay2.x, -posRay2.z, viewPZ2.y, posRay2.y);
	triangleStream.Append(psInput);
	triangleStream.RestartStrip();

	// face 3. (x, z, -y).
	psInput.RtIndex = 3;
	psInput.Pos = float4(-posRay0.x, posRay0.z, viewNZ0.y, -posRay0.y);
	triangleStream.Append(psInput);
	psInput.Pos = float4(-posRay1.x, posRay1.z, viewNZ1.y, -posRay1.y);
	triangleStream.Append(psInput);
	psInput.Pos = float4(-posRay2.x, posRay2.z, viewNZ2.y, -posRay2.y);
	triangleStream.Append(psInput);
	triangleStream.RestartStrip();

	// face 4. (-z, y, x).
	psInput.RtIndex = 4;
	psInput.Pos = float4(posRay0.z, posRay0.y, viewPZ0.x, posRay0.x);
	triangleStream.Append(psInput);
	psInput.Pos = float4(posRay1.z, posRay1.y, viewPZ1.x, posRay1.x);
	triangleStream.Append(psInput);
	psInput.Pos = float4(posRay2.z, posRay2.y, viewPZ2.x, posRay2.x);
	triangleStream.Append(psInput);
	triangleStream.RestartStrip();

	// face 5. (z, y, -x).
	psInput.RtIndex = 5;
	psInput.Pos = float4(-posRay0.z, posRay0.y, viewNZ0.x, -posRay0.x);
	triangleStream.Append(psInput);
	psInput.Pos = float4(-posRay1.z, posRay1.y, viewNZ1.x, -posRay1.x);
	triangleStream.Append(psInput);
	psInput.Pos = float4(-posRay2.z, posRay2.y, viewNZ2.x, -posRay2.x);
	triangleStream.Append(psInput);
	triangleStream.RestartStrip();
}

float4 PS_MultiViewportCubemap(/*PS_PosOnlyViewPortInput input*/) : SV_Target
{
	return float4(0.5, 0.5, 0.5, 1.0);
}

//--------------------------------------------------------------------------------------
// Linear Depth Screenshot code
//--------------------------------------------------------------------------------------

struct LinearDepthScreenshotSrtData
{
	Texture2D<float4> txDiffuse;
	SamplerState	  sSamplerLinear;	
	float		      nearDist;
	float		      farDist;
	float		      padding[2];
};

float4 PS_LinearDepthScreenshot(LinearDepthScreenshotSrtData *pSrtData : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float depth = pSrtData->txDiffuse.Sample(pSrtData->sSamplerLinear, input.Tex).x;

	float farDist = pSrtData->farDist;
	float nearDist = pSrtData->nearDist;

	float range = farDist - nearDist;
	float dist = (((2.0 * farDist * nearDist) / (farDist + nearDist - depth * range)) - nearDist) / range;

	float val = saturate(1.0 - dist);

	return float4(val, val, val, 1.0);
}

//--------------------------------------------------------------------------------------
// Cubemap Screenshot code
//--------------------------------------------------------------------------------------

struct CubemapScreenshotSrtData
{
	Texture2D<float4>		txFrame; // : register(t0);
	SamplerState			samplerPoint;
	float					rgbmMultiplier;

};

PixelShaderOutput2 PS_CubemapScreenshot(CubemapScreenshotSrtData *srt : S_SRT_DATA, PS_PosTex input)
{
	// flip screen x when do cubemap capture.
	float4 srcColor = srt->txFrame.Sample(srt->samplerPoint, float2(1.0f - input.Tex.x, input.Tex.y));

	// Calculate the rgbm color
	float4 rgbmColor = srcColor / srt->rgbmMultiplier;

	float	maxComp = max(rgbmColor.r, max(rgbmColor.g, rgbmColor.b));
	maxComp = clamp(maxComp, 1.0e-6, 1.0);
	maxComp = ceil(maxComp * 255.0) / 255.0;

	// Right now the surfer shaders are always doing rgbm, so fake the M value so everything looks right with 16 bit non RGBM textures
	float fakeM = pow(0.25, 0.45 * 0.45);

	return float4(srcColor.rgb, fakeM);
}

//=================================================================================================
struct GaussianFilterSrtData
{
	Texture2D<float4> txDiffuse;
	SamplerState sSamplerLinear;
};

struct GaussianFilterSrt
{
	GaussianFilterSrtData *pData;
	GaussianBlurParams *pConst;
};

float4 PS_GaussianFilter(Vs_OutputLocalGaussian input, GaussianFilterSrt srt : S_SRT_DATA) : SV_Target
{
	Texture2D<float4> texture = srt.pData->txDiffuse;
	SamplerState sSamplerLinear = srt.pData->sSamplerLinear;
	float4 params = srt.pConst->gaussianBlurParams; 

	float4 sample0 = texture.SampleLevel(sSamplerLinear, input.uv1_uv2.xy, params.z);
	float4 sample1 = texture.SampleLevel(sSamplerLinear, input.uv1_uv2.zw, params.z);
	float4 sample2 = texture.SampleLevel(sSamplerLinear, input.uv3_uv4.xy, params.z);
	float4 sample3 = texture.SampleLevel(sSamplerLinear, input.uv3_uv4.zw, params.z);
	float4 sample4 = texture.SampleLevel(sSamplerLinear, input.uv5_uv6.xy, params.z);
	float4 sample5 = texture.SampleLevel(sSamplerLinear, input.uv5_uv6.zw, params.z);
	float4 sample6 = texture.SampleLevel(sSamplerLinear, input.uv7_uv8.xy, params.z);
	float4 sample7 = texture.SampleLevel(sSamplerLinear, input.uv7_uv8.zw, params.z);
	float4 sample8 = texture.SampleLevel(sSamplerLinear, input.uv9_uva.xy, params.z);
	float4 sample9 = texture.SampleLevel(sSamplerLinear, input.uv9_uva.zw, params.z);
	float4 sample10 = texture.SampleLevel(sSamplerLinear, input.uvb_uvc.xy, params.z);
	float4 sample11 = texture.SampleLevel(sSamplerLinear, input.uvb_uvc.zw, params.z);
	float4 sample12 = texture.SampleLevel(sSamplerLinear, input.Tex.xy, params.z);

	float4 result = (sample0 + sample4 + sample7 + sample11) * 0.024882;
	result += (sample1 + sample3 + sample8 + sample10) * 0.067638;
	result += (sample2 + sample5 + sample6 + sample9) * 0.111515;
	result += sample12 * 0.183858;

	return result;
}

float4 PS_LuminanceToAlpha(BufferCopyPsSrt *srt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float4 retVal;

	retVal.rgb = srt->texture.Sample(srt->samplerState, input.Tex).rgb; //
	retVal.a = dot(retVal.rgb, float3(0.299, 0.587, 0.114));

	return retVal;
}

float4 PS_MovieRender(PS_PosTex input) : SV_Target
{
	float4 result;

	float3 ycbcr = float3(txDiffuse.Sample(g_sSamplerLinear, input.Tex).x - 0.0625f,
	                      txDiffuse1.Sample(g_sSamplerLinear, input.Tex).x - 0.5f,
	                      txDiffuse1.Sample(g_sSamplerLinear, input.Tex).y - 0.5f);

	result = float4(dot(float3(1.1644f, 0.0f, 1.7927f), ycbcr),      // R
	                dot(float3(1.1644f, -0.2133f, -0.5329f), ycbcr), // G
	                dot(float3(1.1644f, 2.1124f, 0.0f), ycbcr),      // B
	                1.0f);

	return result;
}

struct TestEnemyVisibleInfoSrtData
{
	Texture2D<uint> texCount;
	Texture2D<float4> texSource;
	SamplerState sSamplerPoint;
	SamplerState sSamplerLinear;
	float4 aimCursorPositionEnemyFocus;
	uint stencilTestValue;
};

float4 PS_TestEnemyVisibleInfo(PS_PosTex input, TestEnemyVisibleInfoSrtData *srt : S_SRT_DATA) : SV_Target
{
	float4 OUT = float4(0, 0, 0, 0);

	uint stencilValue = srt->texCount.Sample(srt->sSamplerPoint, srt->aimCursorPositionEnemyFocus.xy +
	                                                                 (input.Tex * 2.0 - float2(1.0, 1.0)) *
	                                                                     srt->aimCursorPositionEnemyFocus.zw).x;
	float testMask = srt->texSource.Sample(srt->sSamplerLinear, input.Tex).r;

	if ((stencilValue & srt->stencilTestValue) != 0 && testMask > 0.01)
		OUT = float4(1, 1, 1, 1);
	else
		discard;

	return OUT;
}

struct FocusCamDistData
{
	float4 aimCursorPositionFocusDistance;
	float4 screenParams;
	Texture2D<uint> texCount;
	Texture2D<float4> texSource1;
	Texture2D<float4> texSource2;
	SamplerState sSamplerPoint;
	SamplerState sSamplerLinear;
};

float4 PS_GetFocusPositionCameraDistance(PS_PosTex input, FocusCamDistData *srt : S_SRT_DATA) : SV_Target
{
	float4 OUT;
	float2 texCoords = float2(srt->aimCursorPositionFocusDistance.x, srt->aimCursorPositionFocusDistance.y) +
	                   ((input.Tex * 2.0 - float2(1.0, 1.0)) * srt->aimCursorPositionFocusDistance.zw + float2(0.5, 0.5)) *
	                       srt->screenParams.zw;

	float depthBufferZ = srt->texSource1.Sample(srt->sSamplerPoint, texCoords).x;
	float viewSpaceZ = srt->screenParams.y / (depthBufferZ - srt->screenParams.x);
	float coverMask = srt->texSource2.Sample(srt->sSamplerLinear, input.Tex).r;

	if (depthBufferZ >= 1.f || coverMask < 0.005)
	{
		OUT.y = 10000.0;
		OUT.x = 10000.0;
	}
	else
	{
		uint stencilValue = srt->texCount.Sample(srt->sSamplerPoint, texCoords).x;

		OUT.y = (stencilValue & 0x80) != 0  ? viewSpaceZ : 10000.0;
		OUT.x = viewSpaceZ;
	}

	return OUT;
}

float4 PS_DownSampleCameraDistanceHalfNearFilter(BufferCopyPsSrt *pSrtData : S_SRT_DATA, PS_PosTex4x2 input) : SV_Target
{
	float4 OUT;

	float2 depthBufferZ0 = pSrtData->texture.Sample(pSrtData->samplerState, input.uv0_uv1.xy).xy;
	float2 depthBufferZ1 = pSrtData->texture.Sample(pSrtData->samplerState, input.uv0_uv1.zw).xy;
	float2 depthBufferZ2 = pSrtData->texture.Sample(pSrtData->samplerState, input.uv2_uv3.xy).xy;
	float2 depthBufferZ3 = pSrtData->texture.Sample(pSrtData->samplerState, input.uv2_uv3.zw).xy;

	OUT.xy = min(min(depthBufferZ0, depthBufferZ1), min(depthBufferZ2, depthBufferZ3));
	OUT.zw = float2(0.f, 0.f);

	return OUT;
}

float4 PS_Collect8x8CameraZBuffer(BufferCopyPsSrt *pSrtData : S_SRT_DATA) : SV_Target
{
	float4 OUT;

	float baseUvY = 0.03125;
	float4 minCameraZ = (10000.0, 10000.0, 10000.0, 10000.0);

	for (int i = 0; i < 16; i++)
	{
		float4 cameraZ0, cameraZ1, cameraZ2, cameraZ3, cameraZ4, cameraZ5, cameraZ6, cameraZ7;
		cameraZ0.xy = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125, baseUvY)).xy;
		cameraZ0.zw = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 1, baseUvY)).xy;
		cameraZ1.xy = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 2, baseUvY)).xy;
		cameraZ1.zw = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 3, baseUvY)).xy;
		cameraZ2.xy = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 4, baseUvY)).xy;
		cameraZ2.zw = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 5, baseUvY)).xy;
		cameraZ3.xy = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 6, baseUvY)).xy;
		cameraZ3.zw = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 7, baseUvY)).xy;
		cameraZ4.xy = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 8, baseUvY)).xy;
		cameraZ4.zw = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 9, baseUvY)).xy;
		cameraZ5.xy = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 10, baseUvY)).xy;
		cameraZ5.zw = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 11, baseUvY)).xy;
		cameraZ6.xy = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 12, baseUvY)).xy;
		cameraZ6.zw = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 13, baseUvY)).xy;
		cameraZ7.xy = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 14, baseUvY)).xy;
		cameraZ7.zw = pSrtData->texture.Sample(pSrtData->samplerState, float2(0.03125 + 0.0625 * 15, baseUvY)).xy;
		minCameraZ = min(minCameraZ, min(min(cameraZ0, cameraZ1), min(cameraZ2, cameraZ3)));
		minCameraZ = min(minCameraZ, min(min(cameraZ4, cameraZ5), min(cameraZ6, cameraZ7)));
		baseUvY += 0.0625;
	}

	OUT.xy = min(minCameraZ.xy, minCameraZ.zw);
	OUT = OUT.y < 5000.0 ? OUT.y : OUT.x;

	return OUT;
}

struct SharpenSrt
{
	Texture2D<float4>	m_txSrc;
	Texture2D<float3>	m_txDofPresort;
	Texture2D<float3>	m_txFilmGrain;
	float4				m_filmGrainOffsetScale;
	float4				m_filmGrainIntensity;
	float4				m_filmGrainIntensity2;
	float4				m_tintColorScaleVector;
	float4				m_tintColorOffsetVector;
	float				m_weightScale;
	float				m_threshold;
	int					m_posScale;
	int					m_posOffset;
	int					m_isHdrMode;
	int					m_useCustomCurve;
	float				m_nitsPerUnit;
	int					m_useGammaConvert;
	float				m_customCurve[1024];
	SamplerState		m_pointSampler;
};

const static int2 sharpenOffsets[4] = {int2(0, -1), int2(-1, 0), int2(1, 0), int2(0, 1)};

static const float3x3 BT709_TO_BT2020 = {
	0.627403896, 0.329283038, 0.043313066,
	0.069097289, 0.919540395, 0.011362316,
	0.016391439, 0.088013308, 0.895595253
};
//input: L in normalized nits, output: normalized E
float3 applyPQ_OETF(float3 L)
{
	const float c1 = 0.8359375;//3424.f/4096.f;
	const float c2 = 18.8515625;//2413.f/4096.f*32.f;
	const float c3 = 18.6875; //2392.f/4096.f*32.f;
	const float m1 = 0.159301758125; //2610.f / 4096.f / 4;
	const float m2 = 78.84375;// 2523.f / 4096.f * 128.f;
	float3 Lm1 = pow(L, m1);
	float3 X = (c1 + c2 * Lm1) / (1 + c3 * Lm1);
	return pow(X, m2);
}

float4 GetSharpenColor(int3 pos, float4 currentColor, SharpenSrt *pSrt, float2 uv, bool applySharpen)
{
	float3 outColor = currentColor.xyz;

	if (applySharpen)
	{
		float4 neighborContribution = currentColor * 4.f;

		float weightScale = pSrt->m_weightScale;
		float threshold = pSrt->m_threshold;

		for (uint i = 0; i < 4; ++i)
		{
			neighborContribution -= pSrt->m_txSrc.Load(pos, sharpenOffsets[i]);;
		}

		neighborContribution.rgb = min(abs(neighborContribution.rgb), threshold) * sign(neighborContribution.rgb);

		if (pSrt->m_isHdrMode)
			outColor = max(currentColor.rgb + neighborContribution.rgb*weightScale, 0.0f);
		else
			outColor = saturate(currentColor.rgb + neighborContribution.rgb*weightScale);
	}

	outColor = ApplyFilmGrain( pSrt->m_txFilmGrain, pSrt->m_pointSampler, pSrt->m_filmGrainIntensity, pSrt->m_filmGrainIntensity2, pSrt->m_filmGrainOffsetScale, uv, outColor, true );

	// tint color.
	outColor = outColor * pSrt->m_tintColorScaleVector.xyz + pSrt->m_tintColorOffsetVector.xyz;

	return float4(outColor, currentColor.a);
}

float4 PS_Sharpen(SharpenSrt *pSrt : S_SRT_DATA, PS_PosTex input) : S_TARGET_OUTPUT
{
	int3 pos = int3(input.Pos.xy, 0);
	pos.x = pos.x * pSrt->m_posScale + pSrt->m_posOffset;
	float2 uv = input.Tex.xy;
	float4 currentColor = pSrt->m_txSrc.Load(pos);

	return GetSharpenColor(pos, currentColor, pSrt, uv, true);
}

float4 PS_SharpenAfterDof(SharpenSrt *pSrt : S_SRT_DATA, PS_PosTex input) : S_TARGET_OUTPUT
{
	int3 pos = int3(input.Pos.xy, 0);
	pos.x = pos.x * pSrt->m_posScale + pSrt->m_posOffset;
	float2 uv = input.Tex.xy;
	float4 currentColor = pSrt->m_txSrc.Load(pos);

	// presortInfo = (coc, background, foreground)
	float3 presortInfo = pSrt->m_txDofPresort.SampleLevel( pSrt->m_pointSampler, uv, 0 );
	bool applySharpen = presortInfo.x < 0.8;
	// Only sharpen pixels that are in focus (or a tiny bit out of focus)

	return GetSharpenColor(pos, currentColor, pSrt, uv, applySharpen);
}

struct ColorDarkeningSrt
{
	float m_darkendingAmount;
};

float4 PS_ColorDarkening(ColorDarkeningSrt srt : S_SRT_DATA, PS_PosTex input) : S_TARGET_OUTPUT
{
	return float4(0.0f, 0.0f, 0.0f, srt.m_darkendingAmount);
}