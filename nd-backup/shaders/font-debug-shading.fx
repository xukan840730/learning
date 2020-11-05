#include "cmask-util.fxi"
#include "global-funcs.fxi"
#include "post-processing-common.fxi"
#include "sh.fxi"
#include "probe-occlusion.fxi"

static const float PI		= 3.1415926535897932f;
static const float EPSILON	= 0.0001f;

// multisample AA patterns
static const uint kSampleCountNoAA = 1;
static float3 s_aSampleNoAA[kSampleCountNoAA] =
{
	float3(0.5, 0.5, 1.0)
};
static const uint kSampleCountQuincunx = 5;
static float3 s_aSampleQuincunx[kSampleCountQuincunx] =
{
	float3(0.5, 0.5, 0.2),
	float3(0.0, 0.0, 0.2),
	float3(1.0, 0.0, 0.2),
	float3(1.0, 1.0, 0.2),
	float3(0.0, 1.0, 0.2)
};
static const uint kSampleCountQuincunx2 = 5;
static float3 s_aSampleQuincunx2[kSampleCountQuincunx2] =
{
	float3(0.5, 0.5, 0.2),
	float3(0.0, 0.5, 0.2),
	float3(0.5, 0.0, 0.2),
	float3(1.0, 0.5, 0.2),
	float3(0.5, 1.0, 0.2)
};
static const uint kSampleCountQ15 = 4;
static float3 s_aSampleQ15[kSampleCountQ15] =
{
	float3(0.380392, 0.141176, 0.25),
	float3(0.858824, 0.380392, 0.25),
	float3(0.141176, 0.619608, 0.25),
	float3(0.619608, 0.858824, 0.25)
};
static const uint kSampleCountQ31 = 8;
static float3 s_aSampleQ31[kSampleCountQ31] =
{
	float3(0.258824, 0.498039,  0.125),
	float3(0.498039, 0.741176,  0.125),
	float3(0.419608, 0.258824,  0.125),
	float3(0.741176, 0.419608,  0.125),
	float3(0.0588235, 0.180392, 0.125),
	float3(0.819608, 0.0588235, 0.125),
	float3(0.180392, 0.941176,  0.125),
	float3(0.941176, 0.819608,  0.125)
};
static const uint kSampleCount4x4 = 16;
static float3 s_aSample4x4[kSampleCount4x4] =
{
	float3(0.125, 0.125, 0.0625),
	float3(0.125, 0.375, 0.0625),
	float3(0.125, 0.625, 0.0625),
	float3(0.125, 0.875, 0.0625),
	float3(0.375, 0.125, 0.0625),
	float3(0.375, 0.375, 0.0625),
	float3(0.375, 0.625, 0.0625),
	float3(0.375, 0.875, 0.0625),
	float3(0.625, 0.125, 0.0625),
	float3(0.625, 0.375, 0.0625),
	float3(0.625, 0.625, 0.0625),
	float3(0.625, 0.875, 0.0625),
	float3(0.875, 0.125, 0.0625),
	float3(0.875, 0.375, 0.0625),
	float3(0.875, 0.625, 0.0625),
	float3(0.875, 0.875, 0.0625)
};
static const uint kSampleCountChecker4 = 8;
static float3 s_aSampleChecker4[kSampleCountChecker4] =
{
	float3(0.125, 0.125, 0.125),
	float3(0.125, 0.625, 0.125),
	float3(0.375, 0.375, 0.125),
	float3(0.375, 0.875, 0.125),
	float3(0.625, 0.125, 0.125),
	float3(0.625, 0.625, 0.125),
	float3(0.875, 0.375, 0.125),
	float3(0.875, 0.875, 0.125)
};

static const uint kSampleCountGaussian3 = 9;

static float3 s_aSampleGaussian3[kSampleCountGaussian3] =
{
	float3(-1.0,  1.0, 0.0470588), float3(0.0,  1.0, 0.1215686), float3(1.0,  1.0, 0.0470588),
	float3(-1.0,  0.0, 0.1215686), float3(0.0,  0.0, 0.3254902), float3(1.0,  0.0, 0.1215686),
	float3(-1.0, -1.0, 0.0470588), float3(0.0, -1.0, 0.1215686), float3(1.0, -1.0, 0.0470588),
};

// select your multisample AA pattern for ellipse drawing here!
#define kSampleCountEllipseAA		kSampleCountChecker4
#define s_aSampleEllipseAA			s_aSampleChecker4

// select your blur kernal to apply on top of the AA pattern for ellise drawing
#define kSampleBlurCountEllipse		kSampleCountGaussian3
#define s_aSampleBlurEllipse		s_aSampleGaussian3

//------------------------------------------------------------------------------------------------------------
// Data Structs
//------------------------------------------------------------------------------------------------------------
struct VertexOutputPosOnly 
{
    float4 position		: SV_POSITION;
};

//------------------------------------------------------------------------------------------------------------
struct VertexOutputNormal
{
    float4 position		: SV_POSITION;
	float3 normal		: TEXCOORD0;
};

//------------------------------------------------------------------------------------------------------------
struct VertexOutputColor
{
    float4 position		: SV_POSITION;
	float4 color		: COLOR0;
};

//------------------------------------------------------------------------------------------------------------
struct VertexOutputColorWidth
{
    float4 position		: SV_POSITION;
	float4 color		: COLOR0;
	float width			: TEXCOORD0;
};

//------------------------------------------------------------------------------------------------------------
struct VertexOutputColorDepth
{
    float4 position		: SV_POSITION;
	float4 color		: COLOR0;
	float depth			: TEXCOORD0;
};

//------------------------------------------------------------------------------------------------------------
struct VertexOutputColorOnlyWidth
{
    float4 position		: SV_POSITION;
	float width			: TEXCOORD0;
};

//------------------------------------------------------------------------------------------------------------
struct VertexOutputColorOnlyDepth
{
    float4 position		: SV_POSITION;
	float depth			: TEXCOORD0;
};

//------------------------------------------------------------------------------------------------------------
struct VertexOutput
{
    float4 position		: SV_POSITION;
    float2 uv			: TEXCOORD0;
	float4 color		: COLOR0;
};

//------------------------------------------------------------------------------------------------------------
struct VertexOutputFont
{
    float4 position		: SV_POSITION;
    float2 uv			: TEXCOORD0;
	float4 color		: COLOR0;
	float4 channelMask	: TEXCOORD1;
	float4 outlineColor	: TEXCOORD2;
	float4 distressUv   : TEXCOORD3;
};

//------------------------------------------------------------------------------------------------------------
struct PixelOutput
{
  float4 col			: SV_Target;
};

struct PixelOutputWithCoverage
{
  float4 col			: SV_Target;
  uint coverage			: SV_Coverage;
};

struct Vs3dGlobalParams
{
    matrix		m_altWorldViewProjMat;
	matrix		m_toAltWorldMat;
	float4		m_jitterOffset;
};

struct Vs2dGlobalParams
{
	float4		m_screenScaleVector;
};

struct Vs2dGlobalScaleParams
{
	float4		m_screenScaleVector[5]; // MUST match kDebug2DCoordsCount
};

struct DebugColorParams
{
	float4		m_debugColor;
};

struct DebugLightProbeParams
{
	float4		m_shes[9];
	float		m_shadow;
	float		m_alpha;
	uint2		m_occlusion;
};

struct ScreenSizeParams
{
    int4		m_screenSize;
};

float4 Get3dHPosition(in float4 inPosition, in matrix toAltWorldMat, in matrix altWorld2ProjMat, in float4 jitterOffset)
{
	float4 posAltWorld = mul(inPosition, toAltWorldMat);
	float projW = dot(posAltWorld, float4(altWorld2ProjMat[0][3], altWorld2ProjMat[1][3], altWorld2ProjMat[2][3], altWorld2ProjMat[3][3]));
	return mul(posAltWorld, altWorld2ProjMat) - jitterOffset * projW;
}

float4 Get2dHPosition(in float3 inPositionXYZ, in float w, in float4 screenScaleOfsVec)
{
	return float4(inPositionXYZ.xy * screenScaleOfsVec.xy + screenScaleOfsVec.zw, inPositionXYZ.z, w);
}

//------------------------------------------------------------------------------------------------------------
// Vertex Shaders
//------------------------------------------------------------------------------------------------------------
struct WorldTransformVsSrt
{
	DataBuffer<float4> m_posBuf;
	DataBuffer<float2> m_uvBuf;
	DataBuffer<float4> m_colorBuf;
	Vs3dGlobalParams* m_pParams;
};

VertexOutput WorldTransformVS(WorldTransformVsSrt* pSrt: S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutput OUT;

	float4 position = pSrt->m_posBuf[vertexId];
	float2 uv = pSrt->m_uvBuf[vertexId];
	float4 color = pSrt->m_colorBuf[vertexId];

	OUT.position = Get3dHPosition(position, pSrt->m_pParams->m_toAltWorldMat, pSrt->m_pParams->m_altWorldViewProjMat, pSrt->m_pParams->m_jitterOffset);
	OUT.uv = uv;
	OUT.color = color;
	
	return OUT;
}

struct WorldTransformColorVsSrt
{
	DataBuffer<float4> m_posBuf;
	DataBuffer<float4> m_colorBuf;
	Vs3dGlobalParams* m_pParams;
};

VertexOutputColor WorldTransformColorVS(WorldTransformColorVsSrt* pSrt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutputColor OUT;

	float4 position = pSrt->m_posBuf[vertexId];
	float4 color = pSrt->m_colorBuf[vertexId];

	OUT.position = Get3dHPosition(position, pSrt->m_pParams->m_toAltWorldMat, pSrt->m_pParams->m_altWorldViewProjMat, pSrt->m_pParams->m_jitterOffset);
	OUT.color = color;
	
	return OUT;
}

struct WorldTransformColorWidthEsSrt
{
	DataBuffer<float4> m_posBuf;
	DataBuffer<float4> m_colorBuf;
	DataBuffer<float> m_widthBuf;
	Vs3dGlobalParams* m_pParams;
};

VertexOutputColorWidth WorldTransformColorWidthES(WorldTransformColorWidthEsSrt* pSrt: S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutputColorWidth OUT;

	float4 position = pSrt->m_posBuf[vertexId];
	float4 color = pSrt->m_colorBuf[vertexId];
	float width = pSrt->m_widthBuf[vertexId];

	OUT.position = Get3dHPosition(position, pSrt->m_pParams->m_toAltWorldMat, pSrt->m_pParams->m_altWorldViewProjMat, pSrt->m_pParams->m_jitterOffset);
	OUT.color = color;
	OUT.width = width;
	
	return OUT;
}

struct WorldTransformPosOnlyVsSrt
{
	DataBuffer<float4> m_posBuf;
	Vs3dGlobalParams* m_pParams;
};

VertexOutputPosOnly WorldTransformPosOnlyVS(WorldTransformPosOnlyVsSrt srt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutputPosOnly OUT;

	float4 position = srt.m_posBuf[vertexId];

	OUT.position = Get3dHPosition(position, srt.m_pParams->m_toAltWorldMat, srt.m_pParams->m_altWorldViewProjMat, srt.m_pParams->m_jitterOffset);
	
	return OUT;
}

VertexOutputColorDepth WorldTransformColorDepthVS(WorldTransformColorVsSrt* pSrt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutputColorDepth OUT;

	float4 position = pSrt->m_posBuf[vertexId];
	float4 color = pSrt->m_colorBuf[vertexId];

	OUT.position = Get3dHPosition(position, pSrt->m_pParams->m_toAltWorldMat, pSrt->m_pParams->m_altWorldViewProjMat, pSrt->m_pParams->m_jitterOffset);
	OUT.color = color;
	OUT.depth = OUT.position.z;
	
	return OUT;
}

VertexOutputColorOnlyDepth WorldTransformPosOnlyDepthVS(WorldTransformPosOnlyVsSrt srt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutputColorOnlyDepth OUT;

	float4 position = srt.m_posBuf[vertexId];

	OUT.position = Get3dHPosition(position, srt.m_pParams->m_toAltWorldMat, srt.m_pParams->m_altWorldViewProjMat, srt.m_pParams->m_jitterOffset);
	OUT.depth = OUT.position.z;
	
	return OUT;
}

VertexOutputNormal WorldTransformNormalVS(WorldTransformPosOnlyVsSrt srt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutputNormal OUT;

	float4 position = srt.m_posBuf[vertexId];

	OUT.position = Get3dHPosition(position, srt.m_pParams->m_toAltWorldMat, srt.m_pParams->m_altWorldViewProjMat, srt.m_pParams->m_jitterOffset);
	OUT.normal = normalize(position.xyz);

	return OUT;
}

//------------------------------------------------------------------------------------------------------------
struct PassThroughVsSrt
{
	DataBuffer<float3> m_posBuf;
	DataBuffer<float2> m_uvBuf;
	DataBuffer<float4> m_colorBuf;
	Vs2dGlobalParams* m_pParams;
};

VertexOutput PassThroughVS(PassThroughVsSrt* pSrt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutput OUT;

	float3 position = pSrt->m_posBuf[vertexId];
	float2 uv = pSrt->m_uvBuf[vertexId];
	float4 color = pSrt->m_colorBuf[vertexId];
	
	OUT.position = Get2dHPosition(position, 1.0, pSrt->m_pParams->m_screenScaleVector);
	OUT.uv = uv;
	OUT.color = color;
	
	return OUT;
}

//------------------------------------------------------------------------------------------------------------
struct PassThroughColorVsSrt
{
	DataBuffer<float3> m_posBuf;
	DataBuffer<float4> m_colorBuf;
	Vs2dGlobalParams* m_pParams;
};

VertexOutputColor PassThroughColorVS(PassThroughColorVsSrt* pSrt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutputColor OUT;
	
	float3 position = pSrt->m_posBuf[vertexId];
	float4 color = pSrt->m_colorBuf[vertexId];
	
	OUT.position = Get2dHPosition(position, 1.0, pSrt->m_pParams->m_screenScaleVector);
	OUT.color = color;
	
	return OUT;
}

//------------------------------------------------------------------------------------------------------------
struct PassThroughColorNoScaleVsSrt
{
	DataBuffer<float4> m_posBuf;
	DataBuffer<float4> m_colorBuf;
};

VertexOutputColor PassThroughColorNoScaleVS(PassThroughColorNoScaleVsSrt srt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutputColor OUT;

	float4 position = srt.m_posBuf[vertexId];
	float4 color = srt.m_colorBuf[vertexId];
	
	OUT.position = position;
	OUT.color = color;
	
	return OUT;
}

//------------------------------------------------------------------------------------------------------------
struct PassThroughColorScaleVsSrt
{
	DataBuffer<float3> m_posBuf;
	DataBuffer<uint> m_scaleBuf;
	DataBuffer<float4> m_colorBuf;
	Vs2dGlobalScaleParams* m_pParams;
};

VertexOutputColor PassThroughColorScaleVS(PassThroughColorScaleVsSrt* pSrt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutputColor OUT;

	float3 position = pSrt->m_posBuf[vertexId];
	uint scale = pSrt->m_scaleBuf[vertexId];
	float4 color = pSrt->m_colorBuf[vertexId];

	uint scaleType = scale;
	float4 vertexScaleVector = pSrt->m_pParams->m_screenScaleVector[scaleType];
	
	OUT.position = Get2dHPosition(position, 1.0, vertexScaleVector);
	OUT.color = color;
	
	return OUT;
}

//------------------------------------------------------------------------------------------------------------
struct PassThrough4dVsSrt
{
	DataBuffer<float4> m_posBuf;
	DataBuffer<float2> m_uvBuf;
	DataBuffer<float4> m_colorBuf;
};

VertexOutput PassThrough4dVS(PassThrough4dVsSrt* pSrt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutput OUT;

	float4 position = pSrt->m_posBuf[vertexId];
	float2 uv = pSrt->m_uvBuf[vertexId];
	float4 color = pSrt->m_colorBuf[vertexId];
	
	OUT.position = position;//Get2dHPosition(IN.position.xyz, IN.position.w, screenScaleVector);
	OUT.uv = uv;
	OUT.color = color;
	
	return OUT;
}

//------------------------------------------------------------------------------------------------------------
struct FontPassThroughVsSrt // font
{
	DataBuffer<float4> m_posBuf;
	DataBuffer<float3> m_uvBuf;			// outlineAlpha in z
	DataBuffer<float4> m_colorBuf;
	DataBuffer<float4> m_maskBuf;		// channelMask
};

VertexOutputFont FontPassThroughVS(FontPassThroughVsSrt* pSrt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutputFont OUT;

	float4 position = pSrt->m_posBuf[vertexId];
	float3 uv = pSrt->m_uvBuf[vertexId];
	float4 color = pSrt->m_colorBuf[vertexId];
	float4 mask = pSrt->m_maskBuf[vertexId];
	
	OUT.position = position;//Get2dHPosition(IN.position.xyz, IN.position.w, screenScaleVector);
	OUT.uv = uv.xy;
	OUT.color = color;
	OUT.channelMask = mask;
	
	// choose light or dark outline based on color
	//float3 darkOutline = IN.color.rgb * 0.25f;
	//float3 lightOutline = saturate(IN.color.rgb + float3(0.5, 0.5, 0.5));
	//float colorIsLight = step(0.25f, max(IN.color.r, max(IN.color.g, IN.color.b)));
	//OUT.outlineColor.rgb = lerp(lightOutline, darkOutline, colorIsLight);
	OUT.outlineColor.rgb = color.rgb * 0.25f;
	OUT.outlineColor.a = uv.z;
	OUT.outlineColor.rgb = lerp(color.rgb, OUT.outlineColor.rgb, OUT.outlineColor.a);
	
	// calc the distress texture uv (1-for-1 screen pixels)
	float2 distressTextureSize = float2(128,128);
	float2 screenSize = float2(1920, 1080);
	float screenOutputAspect = 16.0f/9.0f;
	OUT.distressUv.x = position.x * screenSize.x * 0.5f / distressTextureSize.x;
	OUT.distressUv.y = position.y * screenSize.x * 0.5f / screenOutputAspect / distressTextureSize.y;

	OUT.distressUv.z = 128.0f;
	OUT.distressUv.w = 128.0f;

	return OUT;
}

//------------------------------------------------------------------------------------------------------------
// Gui2
//------------------------------------------------------------------------------------------------------------

struct Gui2VsSrt
{
	DataBuffer<float4> m_posBuf;
	DataBuffer<float2> m_uvBuf;
	DataBuffer<float2> m_stBuf;
	DataBuffer<float4> m_colorBuf;
	DataBuffer<float4> m_color2Buf;
	Vs2dGlobalParams*  m_pParams;
};

struct Gui2VertexOutput
{
    float4 position		: SV_POSITION;
    float2 uv			: TEXCOORD0;
    float2 st			: TEXCOORD0;
	float4 color		: COLOR0;
	float4 color2		: COLOR0;
};

Gui2VertexOutput Gui2PassThroughVS(Gui2VsSrt* pSrt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	Gui2VertexOutput OUT;

	float4 position = pSrt->m_posBuf[vertexId];
	float2 uv = pSrt->m_uvBuf[vertexId];
	float2 st = pSrt->m_stBuf[vertexId];
	float4 color = pSrt->m_colorBuf[vertexId];
	float4 color2 = pSrt->m_color2Buf[vertexId];

	OUT.position = Get2dHPosition(position.xyz, 1.0, pSrt->m_pParams->m_screenScaleVector); //position;
	OUT.uv = uv;
	OUT.st = st;
	OUT.color = color;
	OUT.color2 = color2;
	
	return OUT;
}

//------------------------------------------------------------------------------------------------------------
// Gui2 Clipping
//------------------------------------------------------------------------------------------------------------

struct Gui2ClipPlane
{
    float2 m_pt;
	float2 m_normal;
};

struct Gui2ClipPoly
{
	Gui2ClipPlane	m_aPlane[4];
	float			m_invert;
};

struct Gui2ClipCircle
{
    float2 m_center;
	float  m_radiusSqr;
};

struct Gui2ClipSprite
{
	Texture2D		m_texture;
	float4x4		m_mtxSsToUv;
};

static const int kClipPolyIndexCapacity = 4;
static const int kClipCircleIndexCapacity = 4;
static const int kClipSpriteIndexCapacity = 4;

struct Gui2PsSrt
{
	StructuredBuffer<Gui2ClipPoly>		m_vClipPolys;
	StructuredBuffer<Gui2ClipCircle>	m_vClipCircles;
	StructuredBuffer<Gui2ClipSprite>	m_vClipSprites;
	SamplerState						m_sClipSpriteSampler;

	uint								m_clipPolyCount;
	uint								m_clipPolyIndices[kClipPolyIndexCapacity];

	uint								m_clipCircleCount;
	uint								m_clipCircleIndices[kClipCircleIndexCapacity];

	uint								m_clipSpriteCount;
	uint								m_clipSpriteIndices[kClipSpriteIndexCapacity];

	float2								m_dropShadowOffsetPix;
	uint2								m_screenSize;
};

////

float Gui2SampleClipAlpha_Polygn(float4 position : SV_POSITION, Gui2PsSrt* pSrt, int j)
{
	int i = pSrt->m_clipPolyIndices[j];
	float polyAlpha = 1.0f;

	for (int k = 0; k < 4; ++k)
	{
		float2 delta = position.xy - pSrt->m_vClipPolys[i].m_aPlane[k].m_pt;
		float cosine = dot(delta, pSrt->m_vClipPolys[i].m_aPlane[k].m_normal);
		float planeMaskAlpha = step(0.0, cosine);
		polyAlpha *= planeMaskAlpha;
	}

	// NOTE: a negative m_invert indicates an "inverted" clip region, so calculate:
	//          0.0 + polyAlpha    when not inverted
	//          1.0 - polyAlpha    when inverted
	float invert = pSrt->m_vClipPolys[i].m_invert; // 1.0 when not inverted, -1.0 when inverted
	float base = clamp(-invert, 0.0, 1.0); // 0.0 when not inverted,  1.0 when inverted
	polyAlpha = base + invert * polyAlpha;

	return polyAlpha;
}

float Gui2DetermineClipAlpha_Polygn(float4 pixelTopLeft : SV_POSITION, Gui2PsSrt* pSrt, int j, float3 aSample[], uint sampleCount, float4 pixelCenter)
{
	float centerAlpha = Gui2SampleClipAlpha_Polygn(pixelCenter, pSrt, j);

	float alpha = 0.0;
	for (uint i = 0; i < sampleCount; ++i)
	{
		float4 offset = float4(aSample[i].x, aSample[i].y, 0.0, 1.0); // * pSrt->m_uvPerPixel;
		float4 position = pixelTopLeft + offset;
		alpha += aSample[i].z * Gui2SampleClipAlpha_Polygn(position, pSrt, j);
	}
	return saturate(alpha) * centerAlpha;
}

////

float Gui2SampleClipAlpha_Circle(float4 position : SV_POSITION, Gui2PsSrt* pSrt, int j)
{
	int i = pSrt->m_clipCircleIndices[j];

	float2 delta = position.xy - pSrt->m_vClipCircles[i].m_center;
	float distSqr = dot(delta, delta);
	// NOTE: a negative m_radiusSqr indicates an "inverted" clip region
	float circleMaskAlpha = step(sign(pSrt->m_vClipCircles[i].m_radiusSqr) * distSqr, pSrt->m_vClipCircles[i].m_radiusSqr);

	return circleMaskAlpha;
}

float Gui2DetermineClipAlpha_Circle(float4 pixelTopLeft : SV_POSITION, Gui2PsSrt* pSrt, int j, float3 aSample[], uint sampleCount)
{
	float alpha = 0.0;
	for (uint i = 0; i < sampleCount; ++i)
	{
		float4 offset = float4(aSample[i].x, aSample[i].y, 0.0, 1.0); // * pSrt->m_uvPerPixel;
		float4 position = pixelTopLeft + offset;
		alpha += aSample[i].z * Gui2SampleClipAlpha_Circle(position, pSrt, j);
	}
	return saturate(alpha);
}

////

float Gui2DetermineClipAlpha_Sprite(float4 pixelCenter : SV_POSITION, Gui2PsSrt* pSrt, int j)
{
	int i = pSrt->m_clipSpriteIndices[j];
	
	float4 uv4 = mul(pixelCenter, pSrt->m_vClipSprites[i].m_mtxSsToUv);
	float4 texColor = pSrt->m_vClipSprites[i].m_texture.Sample(pSrt->m_sClipSpriteSampler, uv4.xy);
	float spriteAlpha = texColor.r; // assumes a greyscale image, so just sample the RED channel (L8R: actually use a 1-channel image?)
	
	return spriteAlpha;
}

////

float Gui2DetermineClipAlpha(float4 pixelCenter : SV_POSITION, Gui2PsSrt* pSrt, bool bProgressBarAA = false)
{
	float clipAlpha = 1.0;
	float4 pixelTopLeft = pixelCenter + float4(-0.5, -0.5, 0.0, 0.0);

	for (int j = 0; j < pSrt->m_clipPolyCount; ++j)
	{
		// for polygon masks (straight edges) it really doesn't look good without a full 4x4 AA
		if (bProgressBarAA)
			clipAlpha *= Gui2SampleClipAlpha_Polygn(pixelCenter, pSrt, j);
		else
			clipAlpha *= Gui2DetermineClipAlpha_Polygn(pixelTopLeft, pSrt, j, s_aSample4x4, kSampleCount4x4, pixelCenter);
	}
	for (int j = 0; j < pSrt->m_clipCircleCount; ++j)
	{
		// for circle masks we can get away with the half-as-expensive 4x4 checkerboard
		clipAlpha *= Gui2DetermineClipAlpha_Circle(pixelTopLeft, pSrt, j, s_aSampleChecker4, kSampleCountChecker4);
	}

	// no AA on sprites
	for (int j = 0; j < pSrt->m_clipSpriteCount; ++j)
	{
		clipAlpha *= Gui2DetermineClipAlpha_Sprite(pixelCenter, pSrt, j);
	}

	return clipAlpha;
}

//------------------------------------------------------------------------------------------------------------
// Text2 (Gui2)
//------------------------------------------------------------------------------------------------------------

#define TEXT2_R8UNORM 1 // set to 1 to enable R8unorm textures

#if TEXT2_R8UNORM
typedef float  Text2Color;
#define TEXT2_ALPHA(color) color
#define TEXT2_COLOR_BLACK  0
#else
typedef float4 Text2Color;
#define TEXT2_ALPHA(color) color.a
#define TEXT2_COLOR_BLACK  float4(0,0,0,0)
#endif

struct Text2PsSrt
{
	Gui2PsSrt*						m_pBase;
	Texture2D<Text2Color>			m_texture;
	SamplerState					m_sSampler;
	float							m_uvPerTexel;
	float							m_premultAlpha;
};

float GetText2Alpha(float4 pixelCenter : SV_POSITION, float2 uv, Text2PsSrt* pSrt : S_SRT_DATA)
{
	Text2Color texSample = pSrt->m_texture.Sample(pSrt->m_sSampler, uv.xy);
	float texAlpha = TEXT2_ALPHA(texSample);			// later use channelMask to allow us to layer up to 4 pages of glyphs into a single ARGB texture map

	float clipAlpha = Gui2DetermineClipAlpha(pixelCenter, pSrt->m_pBase);

	return texAlpha * clipAlpha;
}

PixelOutput Text2RegularPS(Gui2VertexOutput IN, Text2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0; 
	float4 textAlpha = GetText2Alpha(IN.position, IN.uv, pSrt);
	OUT.col.a = textAlpha * IN.color.a;
	OUT.col.rgb = IN.color.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	return OUT;
}

PixelOutput Text2DropShadowPS(Gui2VertexOutput IN, Text2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0;

	float4 offset;
	offset.xy = pSrt->m_pBase->m_dropShadowOffsetPix;
	offset.z = offset.w = 0.0;
	float2 offsetUv = offset * pSrt->m_uvPerTexel;

	float bgAlpha = GetText2Alpha(IN.position - offset, IN.uv - offsetUv, pSrt);
	float4 bgColor = IN.color2;
	bgColor.a *= bgAlpha;

	float fgAlpha = GetText2Alpha(IN.position, IN.uv, pSrt);
	float4 fgColor = IN.color;
	fgColor.a *= fgAlpha;

	if (fgAlpha == 1.0)
	{
		// this is required so that the FG will *mask*off* the BG at every pixel
		// where the FG glyph is fully opaque (before accounting for IN.color.a)
		OUT.col.a = fgColor.a;
		OUT.col.rgb = fgColor.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}
	else
	{
		// everywhere else, use the alpha compositing formula to get the correct result
		OUT.col.a = fgColor.a + (1.0 - fgColor.a) * bgColor.a;
		float3 outRgb = fgColor.rgb * fgColor.a + bgColor.rgb * bgColor.a * (1.0 - fgColor.a);
		if (OUT.col.a != 0.f)
		{
			outRgb /= OUT.col.a;
		}
		OUT.col.rgb = outRgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}

	return OUT;
}

PixelOutput Text2OutlinePS(Gui2VertexOutput IN, Text2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0; 

	Text2Color texSample = pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy);

	float jitter = pSrt->m_uvPerTexel; // jitter by one texel, but in uv space
	Text2Color jitteredColor = TEXT2_COLOR_BLACK;

	jitteredColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy + float2(-jitter, -jitter));
	jitteredColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy + float2(-jitter, 0.0));
	jitteredColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy + float2(-jitter, jitter));
	
	jitteredColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy + float2(0.0, -jitter));
	jitteredColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy + float2(0.0, 0.0));
	jitteredColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy + float2(0.0, jitter));
	
	jitteredColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy + float2(jitter, -jitter));
	jitteredColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy + float2(jitter, 0.0));
	jitteredColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy + float2(jitter, jitter));

	float texAlpha = TEXT2_ALPHA(texSample); //saturate(TEXT2_ALPHA(texSample) * 1.25);			// later use channelMask to allow us to layer up to 4 pages of glyphs into a single ARGB texture map
	float jitteredAlpha = TEXT2_ALPHA(jitteredColor);	// later use channelMask to allow us to layer up to 4 pages of glyphs into a single ARGB texture map

	float clipAlpha = Gui2DetermineClipAlpha(IN.position, pSrt->m_pBase);

	OUT.col.a = clipAlpha * saturate(jitteredAlpha + texAlpha) * IN.color.a; // adding jitteredAlpha here makes each glyph "fatter" by one texel, hence providing the outline
	// multiplying by texAlpha here sends the color to black for the outline
	OUT.col.rgb = texAlpha * IN.color.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);

	return OUT;
}

struct Text2SoftOutlinePsSrt
{
	Gui2PsSrt*						m_pBase;
	Texture2D<Text2Color>			m_texture;
	SamplerState					m_sSampler;
	float							m_uvPerTexel;
	float							m_premultAlpha;
	uint							m_softOutlineSizePx;
	float							m_softOutlineFalloffStart;
	Texture2D<Text2Color>			m_softOutlineTexture;
	uint							m_sliceIndex;
};

// Soft outlines first written to a texture, then sampled when used
PixelOutput Text2SoftOutlineGeneratePS(Gui2VertexOutput IN, Text2SoftOutlinePsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0;

	float2 uv = IN.uv.xy;

	float2 texDimensions;
	pSrt->m_texture.GetDimensions(texDimensions.x, texDimensions.y);
	float outlineSizeResFriendly = (float)pSrt->m_softOutlineSizePx;	// Change outline size based on reference input texture and screen size
	outlineSizeResFriendly *= texDimensions.y / 512.f;
	outlineSizeResFriendly *= (float)(pSrt->m_pBase->m_screenSize.y / 1080);
	int outlineSizePix = int(outlineSizeResFriendly);
	float oneByOutlineSizeSquared = 1.f / float(outlineSizePix * outlineSizePix);

	float4 bgColor = float4(0.f, 0.f, 0.f, 0.f);
	for (int i = 0; i <= (outlineSizePix * 2); i++)
	{
		for (int j = 0; j <= (outlineSizePix * 2); j++)
		{
			int2 pixOffset = int2((i - outlineSizePix), (j - outlineSizePix));

			float2 sampleUv = uv + (pixOffset * pSrt->m_uvPerTexel);
			float4 sampleColor = pSrt->m_texture.Sample(pSrt->m_sSampler, sampleUv);

			float falloff = 1.f - (float(pixOffset.x * pixOffset.x + pixOffset.y * pixOffset.y) * oneByOutlineSizeSquared);
			falloff = saturate(falloff);
			falloff *= pSrt->m_softOutlineFalloffStart;
			bgColor += sampleColor * falloff;
		}
	}
	bgColor /= float(outlineSizePix);	// Averaging with outlizeSize instead of outlineSize^2 since a perfect edge pixel would have closer to <outlineSize> positive samples than <outlineSize^2> samples
	bgColor = saturate(bgColor);

	OUT.col = bgColor;
	return OUT;
}

float GetText2Alpha(float4 pixelCenter : SV_POSITION, float2 uv, Text2SoftOutlinePsSrt* pSrt : S_SRT_DATA)
{
	Text2Color texSample = pSrt->m_texture.Sample(pSrt->m_sSampler, uv.xy);
	float texAlpha = TEXT2_ALPHA(texSample);			// later use channelMask to allow us to layer up to 4 pages of glyphs into a single ARGB texture map

	float clipAlpha = Gui2DetermineClipAlpha(pixelCenter, pSrt->m_pBase);

	return texAlpha * clipAlpha;
}

float4 GetText2Color2WithClip(float4 pixelCenter : SV_POSITION, float2 uv, Text2SoftOutlinePsSrt* pSrt : S_SRT_DATA)
{
	float4 texSample = pSrt->m_softOutlineTexture.Sample(pSrt->m_sSampler, uv);
	float texAlpha = TEXT2_ALPHA(texSample);			// later use channelMask to allow us to layer up to 4 pages of glyphs into a single ARGB texture map

	float clipAlpha = Gui2DetermineClipAlpha(pixelCenter, pSrt->m_pBase);

	return float4(texSample.xyz, (texSample.a * clipAlpha));
}

PixelOutput Text2SoftOutlinePS(Gui2VertexOutput IN, Text2SoftOutlinePsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0;
	
	float2 uv = IN.uv.xy;

	float4 bgColor = GetText2Color2WithClip(IN.position, uv, pSrt);
	bgColor *= IN.color2;

	if (uv.x < 0.f || uv.y < 0.f || uv.x > 1.f || uv.y > 1.f)	// The clamp sampler will screw us over here and add shadows way beyond where they're needed.
	{
		bgColor = float4(0.f, 0.f, 0.f, 0.f);
	}

	float fgAlpha = GetText2Alpha(IN.position, IN.uv, pSrt);
	float4 fgColor = IN.color;
	fgColor.a *= fgAlpha;

	if (fgAlpha == 1.0)
	{
		// this is required so that the FG will *mask*off* the BG at every pixel
		// where the FG glyph is fully opaque (before accounting for IN.color.a)
		OUT.col.a = fgColor.a;
		OUT.col.rgb = fgColor.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}
	else
	{
		// everywhere else, use the alpha compositing formula to get the correct result
		OUT.col.a = fgColor.a + (1.0 - fgColor.a) * bgColor.a;
		float3 outRgb = fgColor.rgb * fgColor.a + bgColor.rgb * bgColor.a * (1.0 - fgColor.a);
		if (OUT.col.a != 0.f)
		{
			outRgb /= OUT.col.a;
		}
		OUT.col.rgb = outRgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}

	return OUT;
}

// Soft outlines done in real time (not very optimal)
PixelOutput Text2SoftOutlineDebugPS(Gui2VertexOutput IN, Text2SoftOutlinePsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0;

	float2 uvPerPixel = pSrt->m_uvPerTexel;
	float2 uv = IN.uv.xy;
	float2 uvZeroToOne = IN.st.xy;	// "Outer" UVs (outline only area) will have this value outside the [0, 1] range

	float4 bgColor = float4(0.f, 0.f, 0.f, 0.f);
	int outlineSizePix = int(pSrt->m_softOutlineSizePx * pSrt->m_pBase->m_screenSize.y / 1440);	// 1440p is reference
	float oneByOutlineSizeSquared = 1.f / float(outlineSizePix * outlineSizePix);

	for (int i = 0; i < (outlineSizePix * 2); i++)
	{
		for (int j = 0; j < (outlineSizePix * 2); j++)
		{
			int2 pixOffset = int2((i - outlineSizePix), (j - outlineSizePix));

			float2 sampleUv = uv + (pixOffset * uvPerPixel);
			float4 sampleColor = pSrt->m_texture.Sample(pSrt->m_sSampler, sampleUv);

			// Don't count samples outside the glyph's UV range so as to not get shadows from other glyphs
			float2 sampleUvZeroToOne = uvZeroToOne + (pixOffset * uvPerPixel);
			bool sampleUvOutsideZeroOne = (sampleUvZeroToOne.x < 0.f || sampleUvZeroToOne.y < 0.f || sampleUvZeroToOne.x > 1.f || sampleUvZeroToOne.y > 1.f);
			sampleColor = (!sampleUvOutsideZeroOne) ? sampleColor : float4(0.f, 0.f, 0.f, 0.f);

			float falloff = 1.f - (float(pixOffset.x * pixOffset.x + pixOffset.y * pixOffset.y) * oneByOutlineSizeSquared);
			falloff *= pSrt->m_softOutlineFalloffStart;
			bgColor += sampleColor * falloff;
		}
	}
	bgColor /= float(outlineSizePix);	// Averaging with outlizeSize instead of outlineSize^2 since a perfect edge pixel would have closer to <outlineSize> positive samples than <outlineSize^2> samples
	bgColor *= IN.color2;
	bgColor = saturate(bgColor);

	float fgAlpha = GetText2Alpha(IN.position, IN.uv, pSrt);
	float4 fgColor = IN.color;
	
	// Outside the glyph's UV range, take no fg samples
	bool fgUvOutsideZeroOne = (uv.x < 0.f || uv.y < 0.f || uv.x > 1.f || uv.y > 1.f);
	fgColor = (!fgUvOutsideZeroOne) ? fgColor : float4(0.f, 0.f, 0.f, 0.f);

	fgColor.a *= fgAlpha;

	if (fgAlpha == 1.0)
	{
		// this is required so that the FG will *mask*off* the BG at every pixel
		// where the FG glyph is fully opaque (before accounting for IN.color.a)
		OUT.col.a = fgColor.a;
		OUT.col.rgb = fgColor.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}
	else
	{
		// everywhere else, use the alpha compositing formula to get the correct result
		OUT.col.a = fgColor.a + (1.0 - fgColor.a) * bgColor.a;
		float3 outRgb = fgColor.rgb * fgColor.a + bgColor.rgb * bgColor.a * (1.0 - fgColor.a);
		if (OUT.col.a != 0.f)
		{
			outRgb /= OUT.col.a;
		}
		OUT.col.rgb = outRgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}

	return OUT;
}

//------------------------------------------------------------------------------------------------------------
// Sprite2 (Gui2)
//------------------------------------------------------------------------------------------------------------

struct Sprite2PsSrt
{
	Gui2PsSrt*						m_pBase;
	Texture2D						m_texture;
	Texture2D						m_texture2;
	SamplerState					m_sSampler;
	float							m_premultAlpha;

	// for atlasing
	float2							m_uvMin;
	float2							m_uvRange;
	float							m_uvFmodOffset;

	// for blurring
	float							m_blurWeights[11];
	float2							m_uvPerPixel;
	float							m_targetBlurMipLevel;

	// for compositing
	uint							m_offscreenCmaskWidth;
	Buffer<uint>					m_offscreenCmask;

	// for soft outline
	uint							m_softOutlineSizePx;
	float							m_softOutlineFalloffStart;

	// for film grain
	Texture2D<float3>				m_filmGrainTexture;
	SamplerState					m_filmGrainSampler;
	float4							m_filmGrainIntensity;
	float4							m_filmGrainIntensity2;
	float4							m_filmGrainOffsetScale;
	float2							m_filmGrainUvScale;
};

float4 SampleOffscreenTexture(Sprite2PsSrt* pSrt, float2 uv, float2 uvPerPixel, bool isNeo)
{
	uint2 pixelPosition = (uint2)(uv / uvPerPixel);
	if (CMaskTileWritten(pixelPosition, pSrt->m_offscreenCmaskWidth, pSrt->m_offscreenCmask, false, isNeo))
		return pSrt->m_texture[pixelPosition];
	else
		return float4(0.0f);
}

float4 SampleSprite2OffscreenColorWithClip(float4 position, float2 uv, float2 uvPerPixel, bool isNeo, Sprite2PsSrt* pSrt)
{
	float4 texColor = SampleOffscreenTexture(pSrt, uv, uvPerPixel, isNeo);
	if (texColor.a != 0.0)
	{
		texColor.rgb /= texColor.a; // un-premultiply so we can do regular alpha blending when rendering the offscreen texture to the screen!
	}

	float clipAlpha = Gui2DetermineClipAlpha(position, pSrt->m_pBase);

	texColor.a *= clipAlpha;
	return texColor;
}

float4 SampleSprite2ColorWithClip(float4 position, float2 uv, Sprite2PsSrt* pSrt)
{
	float4 texColor = pSrt->m_texture.Sample(pSrt->m_sSampler, uv);
	float clipAlpha = Gui2DetermineClipAlpha(position, pSrt->m_pBase);

	texColor.a *= clipAlpha;
	return texColor;
}

float4 SampleSprite2Color2WithClip(float4 position, float2 uv, Sprite2PsSrt* pSrt)
{
	float4 texColor = pSrt->m_texture2.Sample(pSrt->m_sSampler, uv);
	float clipAlpha = Gui2DetermineClipAlpha(position, pSrt->m_pBase);

	texColor.a *= clipAlpha;
	return texColor;
}

PixelOutput Sprite2RegularPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0;

	float4 texColorClipped = SampleSprite2ColorWithClip(IN.position, IN.uv.xy, pSrt);
	float4 finalColor = texColorClipped * IN.color;
	OUT.col.a = finalColor.a;
	OUT.col.rgb = finalColor.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);

	return OUT;
}

PixelOutput Sprite2DropShadowPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0;

	float4 offset;
	offset.xy = pSrt->m_pBase->m_dropShadowOffsetPix;
	offset.z = offset.w = 0.0;
	float2 uvPerPixel = IN.st.xy;
	float2 offsetUv = offset * uvPerPixel;

	float4 bgColor = SampleSprite2ColorWithClip(IN.position - offset, IN.uv.xy - offsetUv, pSrt);
	float bgAlpha = bgColor.a;
	bgColor *= IN.color2;

	float4 fgColor = SampleSprite2ColorWithClip(IN.position, IN.uv.xy, pSrt);
	float fgAlpha = fgColor.a;
	fgColor *= IN.color;

	if (fgAlpha == 1.0)
	{
		// this is required so that the FG will *mask*off* the BG at every pixel
		// where the FG glyph is fully opaque (before accounting for IN.color.a)
		OUT.col.a = fgColor.a;
		OUT.col.rgb = fgColor.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}
	else
	{
		// everywhere else, use the alpha compositing formula to get the correct result
		OUT.col.a = fgColor.a + (1.0 - fgColor.a) * bgColor.a;
		float3 outRgb = fgColor.rgb * fgColor.a + bgColor.rgb * bgColor.a * (1.0 - fgColor.a);
		if (OUT.col.a != 0.f)
		{
			outRgb /= OUT.col.a;
		}
		OUT.col.rgb = outRgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}

	return OUT;
}

PixelOutput Sprite2DropShadowComposited(Gui2VertexOutput IN, Sprite2PsSrt* pSrt, bool isNeo)
{
	PixelOutput OUT = (PixelOutput)0;

	float4 offset;
	offset.xy = pSrt->m_pBase->m_dropShadowOffsetPix;
	offset.z = offset.w = 0.0;
	float2 uvPerPixel = IN.st.xy;
	float2 offsetUv = offset * uvPerPixel;

	float4 bgColor = SampleSprite2OffscreenColorWithClip(IN.position - offset, IN.uv.xy - offsetUv, uvPerPixel, isNeo, pSrt);
	float bgAlpha = bgColor.a;
	bgColor *= IN.color2;

	float4 fgColor = SampleSprite2OffscreenColorWithClip(IN.position, IN.uv.xy, uvPerPixel, isNeo, pSrt);
	float fgAlpha = fgColor.a;
	fgColor *= IN.color;

	if (fgAlpha == 1.0)
	{
		// this is required so that the FG will *mask*off* the BG at every pixel
		// where the FG glyph is fully opaque (before accounting for IN.color.a)
		OUT.col.a = fgColor.a;
		OUT.col.rgb = fgColor.rgb;
	}
	else
	{
		// everywhere else, use the alpha compositing formula to get the correct result
		OUT.col.a = fgColor.a + (1.0 - fgColor.a) * bgColor.a;
		float3 outRgb = fgColor.rgb * fgColor.a + bgColor.rgb * bgColor.a * (1.0 - fgColor.a);
		if (OUT.col.a != 0.f)
		{
			outRgb /= OUT.col.a;
		}
		OUT.col.rgb = outRgb;
	}

	return OUT;
}

PixelOutput Sprite2DropShadowCompositedPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	return Sprite2DropShadowComposited(IN, pSrt, false);
}

PixelOutput Sprite2DropShadowCompositedNeoPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	return Sprite2DropShadowComposited(IN, pSrt, true);
}

PixelOutput Sprite2SoftOutlineGeneratePS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0;
	
	float2 uvPerPixel = pSrt->m_uvPerPixel;

	float2 uv = IN.uv.xy;

	float4 bgColor = float4(0.f, 0.f, 0.f, 0.f);
	int outlineSizePix = int(pSrt->m_softOutlineSizePx * pSrt->m_pBase->m_screenSize.y / 1080);
	float oneByOutlineSizeSquared = 1.f / float(outlineSizePix * outlineSizePix);
	for (int i = 0; i <= (outlineSizePix * 2); i++)
	{
		for (int j = 0; j <= (outlineSizePix * 2); j++)
		{
			int2 pixOffset = int2((i - outlineSizePix), (j - outlineSizePix));

			float2 sampleUv = uv + (pixOffset * uvPerPixel);
			float4 sampleColor = pSrt->m_texture.Sample(pSrt->m_sSampler, sampleUv);
			sampleColor *= sampleColor.a;

			// Don't count samples outside [0, 1] - there's nothing there in the original image, and therefore, there can't be any shadow contribution
			bool sampleUvOutsideZeroOne = (sampleUv.x < 0.f || sampleUv.y < 0.f || sampleUv.x > 1.f || sampleUv.y > 1.f);
			sampleColor = (!sampleUvOutsideZeroOne) ? sampleColor : float4(0.f, 0.f, 0.f, 0.f);

			float falloff = 1.f - (float(pixOffset.x * pixOffset.x + pixOffset.y * pixOffset.y) * oneByOutlineSizeSquared);
			falloff = saturate(falloff);
			falloff *= pSrt->m_softOutlineFalloffStart;
			bgColor += sampleColor * falloff;
		}
	}
	bgColor /= float(outlineSizePix);	// Averaging with outlizeSize instead of outlineSize^2 since a perfect edge pixel would have closer to <outlineSize> positive samples than <outlineSize^2> samples
	bgColor = saturate(bgColor);

	OUT.col = bgColor;

	return OUT;
}

PixelOutput Sprite2SoftOutlinePS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0;

	float2 uvPerPixel = pSrt->m_uvPerPixel;

	float2 uv = IN.uv.xy;
	float2 st = IN.st.xy;

	float4 bgColor = SampleSprite2Color2WithClip(IN.position, st, pSrt);
	bgColor.g = bgColor.r;
	bgColor.b = bgColor.r;
	bgColor.a *= bgColor.r;
	bgColor *= IN.color2;

	float4 fgColor = SampleSprite2ColorWithClip(IN.position, uv, pSrt);
	float fgAlpha = fgColor.a;
	fgColor *= IN.color;

	// Outside [0, 1], take no fg samples - only bg should be present (since we're using a clamp sampler)
	bool fgUvOutsideZeroOne = (uv.x < 0.f || uv.y < 0.f || uv.x > 1.f || uv.y > 1.f);
	fgColor = (!fgUvOutsideZeroOne) ? fgColor : float4(0.f, 0.f, 0.f, 0.f);

	if (fgAlpha == 1.0)
	{
		// this is required so that the FG will *mask*off* the BG at every pixel
		// where the FG glyph is fully opaque (before accounting for IN.color.a)
		OUT.col.a = fgColor.a;
		OUT.col.rgb = fgColor.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}
	else
	{
		// everywhere else, use the alpha compositing formula to get the correct result
		OUT.col.a = fgColor.a + (1.0 - fgColor.a) * bgColor.a;
		float3 outRgb = fgColor.rgb * fgColor.a + bgColor.rgb * bgColor.a * (1.0 - fgColor.a);
		if (OUT.col.a != 0.f)
		{
			outRgb /= OUT.col.a;
		}
		OUT.col.rgb = outRgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}

	return OUT;
}

// Soft outlines done in real time (not very optimal)
PixelOutput Sprite2SoftOutlineDebugPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0;

	float2 uvPerPixel = IN.st.xy;

	float2 uv = IN.uv.xy;
	uv = pSrt->m_uvMin + (pSrt->m_uvRange * (uv / 1.f));	// RangeMap uv from [0, 1] to [uvMin, uvMin + uvRange]

	float4 bgColor = float4(0.f, 0.f, 0.f, 0.f);
	int outlineSizePix = int(pSrt->m_softOutlineSizePx * pSrt->m_pBase->m_screenSize.y / 1440);	// 1440p is reference;
	float oneByOutlineSizeSquared = 1.f / float(outlineSizePix * outlineSizePix);
	for (int i = 0; i < (outlineSizePix * 2); i++)
	{
		for (int j = 0; j < (outlineSizePix * 2); j++)
		{
			int2 pixOffset = int2((i - outlineSizePix), (j - outlineSizePix));

			float2 sampleUv = uv + (pixOffset * uvPerPixel);
			float4 sampleColor = SampleSprite2ColorWithClip((IN.position), sampleUv, pSrt);

			// Don't count samples outside [0, 1] - there's nothing there in the original image, and therefore, there can't be any shadow contribution
			bool sampleUvOutsideZeroOne = (sampleUv.x < 0.f || sampleUv.y < 0.f || sampleUv.x > 1.f || sampleUv.y > 1.f);
			sampleColor = (!sampleUvOutsideZeroOne) ? sampleColor : float4(0.f, 0.f, 0.f, 0.f);

			float falloff = 1.f - (float(pixOffset.x * pixOffset.x + pixOffset.y * pixOffset.y) * oneByOutlineSizeSquared);
			falloff *= pSrt->m_softOutlineFalloffStart;
			bgColor += sampleColor * falloff;
		}
	}
	bgColor /= float(outlineSizePix);	// Averaging with outlizeSize instead of outlineSize^2 since a perfect edge pixel would have closer to <outlineSize> positive samples than <outlineSize^2> samples
	bgColor *= IN.color2;
	bgColor = saturate(bgColor);

	float4 fgColor = SampleSprite2ColorWithClip(IN.position, uv, pSrt);
	float fgAlpha = fgColor.a;
	fgColor *= IN.color;

	// Outside [0, 1], take no fg samples - only bg should be present (since we're using a clamp sampler)
	bool fgUvOutsideZeroOne = (uv.x < 0.f || uv.y < 0.f || uv.x > 1.f || uv.y > 1.f);
	fgColor = (!fgUvOutsideZeroOne) ? fgColor : float4(0.f, 0.f, 0.f, 0.f);

	if (fgAlpha == 1.0)
	{
		// this is required so that the FG will *mask*off* the BG at every pixel
		// where the FG glyph is fully opaque (before accounting for IN.color.a)
		OUT.col.a = fgColor.a;
		OUT.col.rgb = fgColor.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}
	else
	{
		// everywhere else, use the alpha compositing formula to get the correct result
		OUT.col.a = fgColor.a + (1.0 - fgColor.a) * bgColor.a;
		float3 outRgb = fgColor.rgb * fgColor.a + bgColor.rgb * bgColor.a * (1.0 - fgColor.a);
		if (OUT.col.a != 0.f)
		{
			outRgb /= OUT.col.a;
		}
		OUT.col.rgb = outRgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}

	return OUT;
}

PixelOutput Sprite2SoftOutlineComposited(Gui2VertexOutput IN, Sprite2PsSrt* pSrt, bool isNeo)
{
	PixelOutput OUT = (PixelOutput)0;

	float2 uvPerPixel = IN.st.xy;

	float2 uv = IN.uv.xy;
	uv = pSrt->m_uvMin + (pSrt->m_uvRange * (uv / 1.f));	// RangeMap uv from [0, 1] to [uvMin, uvMin + uvRange]

	float4 bgColor = float4(0.f, 0.f, 0.f, 0.f);
	int outlineSizePix = int(pSrt->m_softOutlineSizePx * pSrt->m_pBase->m_screenSize.y / 1080.f);	// 1080p is reference;
	float oneByOutlineSizeSquared = 1.f / float(outlineSizePix * outlineSizePix + 1.f);	// Add 1 so the falloff below isn't ever totally zero
	for (int i = 0; i <= (outlineSizePix * 2); i++)
	{
		for (int j = 0; j <= (outlineSizePix * 2); j++)
		{
			int2 pixOffset = int2((i - outlineSizePix), (j - outlineSizePix));

			float2 sampleUv = uv + (pixOffset * uvPerPixel);
			float4 sampleColor = SampleSprite2OffscreenColorWithClip((IN.position + float4(pixOffset, 0.f, 0.f)), sampleUv, uvPerPixel, isNeo, pSrt);
			sampleColor.rgb *= sampleColor.a;

			// Don't count samples outside [0, 1] - there's nothing there in the original image, and therefore, there can't be any shadow contribution
			bool sampleUvOutsideZeroOne = (sampleUv.x < 0.f || sampleUv.y < 0.f || sampleUv.x > 1.f || sampleUv.y > 1.f);
			sampleColor = (!sampleUvOutsideZeroOne) ? sampleColor : float4(0.f, 0.f, 0.f, 0.f);

			float falloff = 1.f - (float(pixOffset.x * pixOffset.x + pixOffset.y * pixOffset.y) * oneByOutlineSizeSquared);
			falloff = saturate(falloff);
			falloff *= pSrt->m_softOutlineFalloffStart;
			bgColor += sampleColor * falloff;
		}
	}
	bgColor /= float(outlineSizePix * outlineSizePix);
	bgColor *= IN.color2;
	bgColor = saturate(bgColor);
	bgColor.a *= IN.color.a;	// Attenuate based on foreground alpha

	float4 fgColor = SampleSprite2OffscreenColorWithClip(IN.position, IN.uv.xy, uvPerPixel, isNeo, pSrt);

	// Outside [0, 1], take no fg samples - only bg should be present (since we're using a clamp sampler)
	bool fgUvOutsideZeroOne = (uv.x < 0.f || uv.y < 0.f || uv.x > 1.f || uv.y > 1.f);
	fgColor = (!fgUvOutsideZeroOne) ? fgColor : float4(0.f, 0.f, 0.f, 0.f);

	float fgAlpha = fgColor.a;
	fgColor *= IN.color;

	if (fgAlpha == 1.0)
	{
		// this is required so that the FG will *mask*off* the BG at every pixel
		// where the FG glyph is fully opaque (before accounting for IN.color.a)
		OUT.col.a = fgColor.a;
		OUT.col.rgb = fgColor.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha) /** float3(1.f, 0.f, 0.f)*/;
	}
	else
	{
		// everywhere else, use the alpha compositing formula to get the correct result
		OUT.col.a = fgColor.a + (1.0 - fgColor.a) * bgColor.a;
		float3 outRgb = fgColor.rgb * fgColor.a /** float3(0.f, 1.f, 0.f)*/ + bgColor.rgb * bgColor.a * (1.0 - fgColor.a) /** float3(0.f, 0.f, 1.f)*/;
		if (OUT.col.a != 0.f)
		{
			outRgb /= OUT.col.a;
		}
		OUT.col.rgb = outRgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	}

	return OUT;
}

PixelOutput Sprite2SoftOutlineCompositedPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	return Sprite2SoftOutlineComposited(IN, pSrt, false);
}

PixelOutput Sprite2SoftOutlineCompositedNeoPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	return Sprite2SoftOutlineComposited(IN, pSrt, true);
}

PixelOutput Sprite2Composited(Gui2VertexOutput IN, Sprite2PsSrt* pSrt, bool isNeo)
{
	PixelOutput OUT = (PixelOutput)0;

	float4 texColor = SampleOffscreenTexture(pSrt, IN.uv.xy, IN.st.xy, isNeo);
	float clipAlpha = Gui2DetermineClipAlpha(IN.position, pSrt->m_pBase);

	if (texColor.a != 0.0)
	{
		texColor.rgb /= texColor.a; // un-premultiply so we can do regular alpha blending when rendering the offscreen texture to the screen!
	}

	float4 finalColor = texColor * IN.color;
	OUT.col.a = clipAlpha * finalColor.a;
	OUT.col.rgb = finalColor.rgb;

	return OUT;
}

PixelOutput Sprite2CompositedPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	return Sprite2Composited(IN, pSrt, false);
}

PixelOutput Sprite2CompositedNeoPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	return Sprite2Composited(IN, pSrt, true);
}

PixelOutput Sprite2FilmGrainPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0; 

	float4 texColor = pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy);
	float clipAlpha = Gui2DetermineClipAlpha(IN.position, pSrt->m_pBase);

	float4 preGrainColor = texColor * IN.color;

	float2 uvFilmGrain = IN.position * pSrt->m_filmGrainUvScale; // create a (u,v) that ranges from (0,0) at top-left of screen to (1,1) at bottom-right
	float3 grainyColor = ApplyFilmGrain(pSrt->m_filmGrainTexture, pSrt->m_filmGrainSampler, pSrt->m_filmGrainIntensity, pSrt->m_filmGrainIntensity2, pSrt->m_filmGrainOffsetScale, uvFilmGrain, preGrainColor, true);

	float4 finalColor;
	finalColor.rgb = grainyColor;
	finalColor.a = preGrainColor.a;

	OUT.col.a = clipAlpha * finalColor.a;
	OUT.col.rgb = finalColor.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);

	return OUT;
}

// This variant applies the Y blur, using an X-blurred source texture as input.
PixelOutput Sprite2BlurPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0; 

	float3 texColor = 0;
	if (true)
	{
		float2 dir = float2(0.0, 1.0) * pSrt->m_uvPerPixel;
		texColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv).xyz * pSrt->m_blurWeights[0];
		float fi = 1.0;
		for (int i = 1; i < 11; ++i, fi += 1.0)
		{
			if (pSrt->m_blurWeights[i] > 0)
			{
				float2 offset = dir * fi;
				texColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv - offset).xyz * pSrt->m_blurWeights[i];
				texColor += pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv + offset).xyz * pSrt->m_blurWeights[i];
			}
		}
	}
	else
	{
		// trying it without a sampler... haven't got this one working yet!
		int2 coordSs = IN.uv / pSrt->m_uvPerPixel;

		int2 dir = int2(0, 1);
		texColor += pSrt->m_texture[coordSs] * pSrt->m_blurWeights[0];
		for (int i = 1; i < 11; ++i)
		{
			if (pSrt->m_blurWeights[i] > 0)
			{
				texColor += pSrt->m_texture[coordSs - dir * i] * pSrt->m_blurWeights[i];
				texColor += pSrt->m_texture[coordSs + dir * i] * pSrt->m_blurWeights[i];
			}
		}
	}

	float clipAlpha = Gui2DetermineClipAlpha(IN.position, pSrt->m_pBase);

	OUT.col.a = clipAlpha * IN.color.a;
	OUT.col.rgb = texColor.rgb * IN.color.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);

	return OUT;
}

// This variant takes an already fully-blurred texture as input.
PixelOutput Sprite2BlurredPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0; 

	float4 texColor = pSrt->m_texture.Sample(pSrt->m_sSampler, IN.uv.xy);
	float clipAlpha = Gui2DetermineClipAlpha(IN.position, pSrt->m_pBase);

	OUT.col.a = clipAlpha * IN.color.a;
	OUT.col.rgb = texColor.rgb * IN.color.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);

	return OUT;
}

PixelOutput Sprite2AtlasedPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0; 

	// uv clamping for atlased textures
	float2 uv = IN.uv;
	uv.x = clamp(uv.x, pSrt->m_uvMin.x, pSrt->m_uvMin.x + pSrt->m_uvRange.x);
	uv.y = clamp(uv.y, pSrt->m_uvMin.y, pSrt->m_uvMin.y + pSrt->m_uvRange.y);

	float4 texColor = pSrt->m_texture.Sample(pSrt->m_sSampler, uv.xy);
	float clipAlpha = Gui2DetermineClipAlpha(IN.position, pSrt->m_pBase);

	float4 finalColor = texColor * IN.color;
	OUT.col.a = clipAlpha * finalColor.a;
	OUT.col.rgb = finalColor.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);

	return OUT;
}

PixelOutput Sprite2TiledPS(Gui2VertexOutput IN, Sprite2PsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0;

	// uv wrapping for atlased textures (note: add uvFmodOffset to avoid negative input to fmod(), but loses 4 bits of FP precision)
	float2 uv = IN.uv;
	uv.x = fmod((uv.x - pSrt->m_uvMin.x + pSrt->m_uvFmodOffset*pSrt->m_uvRange.x), pSrt->m_uvRange.x) + pSrt->m_uvMin.x;
	uv.y = fmod((uv.y - pSrt->m_uvMin.y + pSrt->m_uvFmodOffset*pSrt->m_uvRange.y), pSrt->m_uvRange.y) + pSrt->m_uvMin.y;

	float4 texColor = pSrt->m_texture.Sample(pSrt->m_sSampler, uv);
	float clipAlpha = Gui2DetermineClipAlpha(IN.position, pSrt->m_pBase);

	float4 finalColor = texColor * IN.color;
	OUT.col.a = clipAlpha * finalColor.a;
	OUT.col.rgb = finalColor.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);

	return OUT;
}

//------------------------------------------------------------------------------------------------------------
// Gui2 Antialiased Polyline Drawing
//------------------------------------------------------------------------------------------------------------

struct Gui2PolylinePsSrt
{
	Gui2PsSrt*						m_pBase;
	float							m_lineWidth_pix;
	float							m_premultAlpha;
};

float LineAlpha(float u, float width_u)	// alpha for the body of a line segment extending along the v axis (also used for end caps, with v instead of u)
{
	float s = (u - 0.5) * 2.0;						// s is u but measured relative to the centerline of the quad, and normalized into [-1, 1]
	float p = (abs(s) - width_u) / (1.0 - width_u);	// p is positive when s lies outside the line width, negative when inside
	float b = saturate(p);							// clamp all negatives to zero
	float lineAlpha = lerp(1.0, 0.0, b);			// 1.0 when p is inside the line, lerping down to 0.0 when outside

	return lineAlpha;
}

float LineCapRoundAlpha(float2 uv, float width_u)	// alpha that traces out an AA semicircle centered on (u,v)==(0.5, 0.0)
{
	float s = (uv.x - 0.5) * 2.0;							// s is u but measured relative to the centerline of the quad, and normalized into [-1, 1]
	float t = uv.y * 2.0;									// t is v but scaled to match s
	float r = sqrt(s*s + t*t);								// radial distance from (u,v)==(0.5, 0.0) and (s,t)==(0.0, 0.0)

	float p = saturate((r - width_u) / (1.0 - width_u));	// p is positive when s lies outside the line width, negative when inside (clamped to zero)
	float b = saturate(p);									// clamp all negatives to zero
	float lineAlpha = lerp(1.0, 0.0, b);					// 1.0 when p is inside the line, lerping down to 0.0 when outside

	return lineAlpha;
}

PixelOutput Gui2ClearQuadPS(Gui2VertexOutput IN)
{
	PixelOutput OUT = (PixelOutput)0;
	OUT.col = IN.color;
	return OUT;
}

PixelOutput Gui2PolylinePS(Gui2VertexOutput IN, Gui2PolylinePsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0; 
	
	float s = IN.st.x;
	float t = IN.st.y;
	float uPerPixel = abs(s);	// we pass uPerPixel on a per-quad basis by passing it down as the s in (s,t) thru all four verts of each quad
	float vPerPixel = abs(t);	// we pass vPerPixel on a per-quad basis by passing it down as the t in (s,t) thru all four verts of each quad
	// NOTE: the SIGN BITS of s and t are used for determining the END CAP TYPE, so we abs-value them above!

	float width_u = pSrt->m_lineWidth_pix * uPerPixel;
	float width_v = pSrt->m_lineWidth_pix * vPerPixel;

	float lineAlpha;
	[branch] if (s >= 0.0f && t >= 0.0f) // no caps (s and t are positive)
	{
		lineAlpha = LineAlpha(IN.uv.x, width_u);
	}
	else
	{
		[branch] if (s >= 0.0f && t < 0.0f) // square caps (s positive, t negative)
		{
			float uAlpha = LineAlpha(IN.uv.x, width_u);
			float vAlpha = LineAlpha(IN.uv.y, width_v);
			lineAlpha = uAlpha * vAlpha;
		}
		else // round caps (s negative)
		{
			lineAlpha = LineCapRoundAlpha(IN.uv, width_u);
		}
	}

	float clipAlpha = Gui2DetermineClipAlpha(IN.position, pSrt->m_pBase);

	OUT.col.a = lineAlpha * clipAlpha * IN.color.a;
	OUT.col.rgb = IN.color.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);

	return OUT;
}

//------------------------------------------------------------------------------------------------------------
// Gui2 Ellipse/Circle
//------------------------------------------------------------------------------------------------------------

struct Gui2EllipsePsSrt
{
	Gui2PsSrt*						m_pBase;
	float2							m_uvPerPixel;		// multiply by this to convert from pixel space to UV space
	float							m_lineWidth_pix;
	float							m_beginAngle_rad;
	float							m_endAngle_rad;
	float							m_extraAngle_rad;
	float							m_beginAngle2_rad;
	float							m_endAngle2_rad;
	float							m_extraAngle2_rad;
	float							m_premultAlpha;
	float							m_guassianBlurWidth;
	float							m_alphaMultiplier;
	float							m_spaceWidthPixels;
	uint							m_numSpaces;
	uint							m_forceUniformSpaceArcLength;
	uint							m_disableClipAA;
};

float EllipseRadialSample(float2 uv, float radiusInnerSqr_uv, float radiusOuterSqr_uv)
{
	float radialLenSqr = dot(uv, uv);
	return step(radiusInnerSqr_uv, radialLenSqr) * step(radialLenSqr, radiusOuterSqr_uv);
}

float SampleEllipse(float2 uvPixelTopLeft, float radiusInnerSqr_uv, float radiusOuterSqr_uv, float2 uvPerPixel)
{
	float alpha = 0.0f;
	for (uint i = 0; i < kSampleCountEllipseAA; ++i)
	{
		float2 offset = s_aSampleEllipseAA[i].xy * uvPerPixel;
		float2 uv = uvPixelTopLeft + offset;
		alpha += s_aSampleEllipseAA[i].z * EllipseRadialSample(uv, radiusInnerSqr_uv, radiusOuterSqr_uv);
	}

	return saturate(alpha);
}

float EllipseRadialAlpha(float2 uvPixelTopLeft, float radiusInner_uv, float radiusOuter_uv, Gui2EllipsePsSrt* pSrt)
{
	float radiusInnerSqr_uv = radiusInner_uv * radiusInner_uv;
	float radiusOuterSqr_uv = radiusOuter_uv * radiusOuter_uv;

	// See if we're more than 2 pixels from the edges of the cirlce, in which case we'll never be a partially blurred and we can early out.
	float len = length(uvPixelTopLeft);
	if ((abs(len - radiusInner_uv) / pSrt->m_uvPerPixel) > 2.0f && (abs(len - radiusOuter_uv) / pSrt->m_uvPerPixel) > 2.0f)
	{
		return  EllipseRadialSample(uvPixelTopLeft, radiusInnerSqr_uv, radiusOuterSqr_uv);
	}

	float alpha = 0.0;
	if (pSrt->m_guassianBlurWidth <= 0.0f)
	{
		alpha = SampleEllipse(uvPixelTopLeft, radiusInnerSqr_uv, radiusOuterSqr_uv, pSrt->m_uvPerPixel);
	}
	else
	{
		for (uint i = 0; i < kSampleBlurCountEllipse; ++i)
		{
			float2 offset = pSrt->m_guassianBlurWidth * s_aSampleBlurEllipse[i].xy * pSrt->m_uvPerPixel;
			float2 uv = uvPixelTopLeft + offset;
			alpha += s_aSampleBlurEllipse[i].z * SampleEllipse(uv, radiusInnerSqr_uv, radiusOuterSqr_uv, pSrt->m_uvPerPixel);
		}
	}

	return saturate(alpha);
}

float ApplySoftOutlineToEllipseArc(float2 uv, float angle, inout float2 angles1, inout float2 angles2, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	float softOutlineWidth_pixels = pSrt->m_lineWidth_pix * 0.5f;

	// Extrude the soft outline on either side of the pie slice
	float radius_uv = sqrt(dot(uv, uv));
	float extraRadians = (softOutlineWidth_pixels * pSrt->m_uvPerPixel) / radius_uv;

	bool bDoesArcPassZero = pSrt->m_endAngle2_rad > pSrt->m_endAngle_rad || pSrt->m_beginAngle2_rad > pSrt->m_beginAngle_rad;

	// If extrusion will cause the pie slice to go beyond a full ellipse, clamp it
	if (bDoesArcPassZero)
	{
		float availableRadians = (2.0f * PI) + angles2.x - angles1.y;
		extraRadians = min(extraRadians, (availableRadians * 0.5f));
	}
	else
	{
		float availableRadians = (2.0f * PI) + angles1.x - angles1.y;
		extraRadians = min(extraRadians, (availableRadians * 0.5f));
	}

	// Change begin/end angles
	if (bDoesArcPassZero)
	{
		angles2.x -= extraRadians;
		angles1.y += extraRadians;
	}
	else
	{
		angles1.x -= extraRadians;
		if (angles1.x < 0.0f)
		{
			// Arc passes zero now!
			angles2.x = angles1.x + (2.0f * PI);
			angles2.y = 2.0f * PI;
			angles1.x = 0.0f;
		
			bDoesArcPassZero = true;
		}
		else
		{
			angles2.x = angles1.x;
		}

		angles1.y += extraRadians;
		if (bDoesArcPassZero)
		{
			// Do nothing
		}
		else if (angles1.y > (2.0f * PI))
		{
			angles2.x = angles1.x;
			angles2.y = 2.0f * PI;
			angles1.x = 0.0f;
			angles1.y -= 2.0f * PI;

			bDoesArcPassZero = true;
		}
		else
		{
			angles2.y = angles1.y;
		}
	}

	float softOutlineAlpha = 1.0f;
	{
		float startAngle = angles2.x;
		float endAngle = angles1.y;

		if (bDoesArcPassZero)
		{
			// Move the arc to start at 0 radians.
			float disp = 2.0f * PI - startAngle;
			startAngle = 0.0f;
			angle += disp;
			if (!(angle < (2.0f * PI)))
				angle -= 2.0f * PI;
			endAngle += disp;
		}

		softOutlineAlpha = min(softOutlineAlpha, saturate((endAngle - angle) / extraRadians));
		softOutlineAlpha = min(softOutlineAlpha, saturate((angle - startAngle) / extraRadians));
	}

	return softOutlineAlpha;
}

void ExpandEllipseArc(float extraRadiansBefore, float extraRadiansAfter, out float2 out_angles1, out float2 out_angles2)
{
	if (out_angles1.x == out_angles2.x && out_angles1.y == out_angles2.y)
	{
		// Arc does not cross 0
		out_angles1.x -= extraRadiansBefore;
		out_angles1.y += extraRadiansAfter;

		if (out_angles1.x < 0.0f || out_angles1.y >= (2.0f * PI))
		{
			// Arc now crosses 0
			while (out_angles1.x < 0.0f)
				out_angles1.x += 2.0f * PI;
			out_angles2.x = out_angles1.x;
			out_angles1.x = 0.0f;

			while (out_angles1.y >= (2.0f * PI))
				out_angles1.y -= 2.0f * PI;
			out_angles2.y = 2.0f * PI;
		}
		else
		{
			// Arc still doesn't cross 0
			out_angles2.x = out_angles1.x;
			out_angles2.y = out_angles1.y;
		}
	}
	else
	{
		// Arc crosses 0
		out_angles2.x -= extraRadiansBefore;
		out_angles1.y += extraRadiansAfter;
	}
}

float EllipseArcSample(float2 uv, float radiusInner_uv, bool bIsSoftOutline, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	//float2 uvAbs = abs(uv);
	float angle = PI - atan2(-uv.y, -uv.x); // convert from [-PI, PI] into [0, 2PI]
	angle = clamp(angle, 0.0, 2.0 * PI);

	float alpha = 1.0f;

	// Use the extra angles (which default to the end angles) for progress bars, etc.
	float2 angles1 = float2(pSrt->m_beginAngle_rad, pSrt->m_extraAngle_rad);
	float2 angles2 = float2(pSrt->m_beginAngle2_rad, pSrt->m_extraAngle2_rad);

	bool bIsPieSlice = abs((2.0f * PI) - (pSrt->m_endAngle_rad - pSrt->m_beginAngle_rad)) > EPSILON;

	if (bIsPieSlice && pSrt->m_forceUniformSpaceArcLength)
	{
		// Better (antialiased) edges for pie slices - mimic the approach used to narrow spaces for DashAlpha
		float radius_uv = sqrt(dot(uv, uv));

		float innerArcLength = 1.f * pSrt->m_uvPerPixel; // Single pixel width (assume), at the inner radius
		float spaceWidthRadians = innerArcLength / radius_uv; // Decrease the arc radians as we go outward

		// Move the edges of the arc
		float extraRadians = spaceWidthRadians * 0.5f;
		ExpandEllipseArc(extraRadians, extraRadians, angles1, angles2);
	}

	if (bIsPieSlice && bIsSoftOutline)
	{
		float softOutlineAlpha = ApplySoftOutlineToEllipseArc(uv, angle, angles1, angles2, pSrt);
		alpha *= softOutlineAlpha;
	}

	float arcAlpha1 = step(angles1.x, angle)  * step(angle, angles1.y);   // 1 iff begin  <= angle <= end
	float arcAlpha2 = step(angles2.x, angle) * step(angle, angles2.y);  // 1 iff begin2 <= angle <= end2
	float arcAlpha = saturate(arcAlpha1 + arcAlpha2);

	alpha *= arcAlpha; // * notZero + (1.0f - notZero);
	return alpha;
}

float EllipseArcAlpha(float2 uvPixelTopLeft, float2 uvPixelCenter, float radiusInner_uv, bool bIsSoftOutline, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	const float disableAAWidth = 0.1f;
	
	// Disable Arc AA at the middle since it will sample from both left and right and one of them won't pass.
	if (abs(uvPixelCenter.x) < disableAAWidth)
	{
		return EllipseArcSample(uvPixelCenter, radiusInner_uv, bIsSoftOutline, pSrt);
	}
	else
	{
		float alpha = 0.0;
		for (uint i = 0; i < kSampleCountEllipseAA; ++i)
		{
			float2 offset = s_aSampleEllipseAA[i].xy * pSrt->m_uvPerPixel;
			float2 uv = uvPixelTopLeft + offset;
			alpha += s_aSampleEllipseAA[i].z * EllipseArcSample(uv, radiusInner_uv, bIsSoftOutline, pSrt);
		}
		return saturate(alpha);
	}
}

// If the begin and end angles are the same, numDashes = numSpaces (wrap around), else numDashes = numSpaces + 1
uint EllipseGetNumDashes(uint numSpaces, float beginAngleRadians, float endAngleRadians)
{
	while (endAngleRadians > (2.0f * PI - EPSILON))
		endAngleRadians -= 2.0f * PI;
	if (abs(beginAngleRadians - endAngleRadians) > EPSILON)
	{
		return (numSpaces + 1);
	}
	else
	{
		return numSpaces;
	}
}

float EllipseDashSample(float2 uv, float radiusInner_uv, bool bIsSoftOutline, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	float angle = PI - atan2(-uv.y, -uv.x); // convert from [-PI, PI] into [0, 2PI]
	angle = clamp(angle, 0.0, 2.0 * PI);

	float alpha = 1.0f;

	bool bDoesArcPassZero = pSrt->m_endAngle2_rad > pSrt->m_endAngle_rad || pSrt->m_beginAngle2_rad > pSrt->m_beginAngle_rad;

	float beginAngleRadians = pSrt->m_beginAngle_rad;
	float endAngleRadians = pSrt->m_endAngle_rad;
	float arcRangeRadians = endAngleRadians - beginAngleRadians;
	if (bDoesArcPassZero)
	{
		arcRangeRadians += pSrt->m_endAngle2_rad - pSrt->m_beginAngle2_rad;
		beginAngleRadians = pSrt->m_beginAngle2_rad;
	}

	uint numDashes = EllipseGetNumDashes(pSrt->m_numSpaces, beginAngleRadians, endAngleRadians);

	float spaceWidthPixels = pSrt->m_spaceWidthPixels;
	float radius_uv = sqrt(dot(uv, uv));

	float softOutlineWidth_pixels = pSrt->m_lineWidth_pix * 0.5f;
	float softOutlineRadians = (softOutlineWidth_pixels * pSrt->m_uvPerPixel) / radius_uv;

	float radiusInner_pixels = radiusInner_uv / pSrt->m_uvPerPixel;
	float spaceWidthRadians = spaceWidthPixels / radiusInner_pixels;	// Since (radius * arcRadians = arcLength --> arcRadians = arcLength / radius)

	if (pSrt->m_forceUniformSpaceArcLength)
	{
		float innerArcLength = radiusInner_uv * spaceWidthRadians;

		float oldSpaceWidthRadians = spaceWidthRadians;
		spaceWidthRadians = innerArcLength / radius_uv; // Decrease the radians as we go outward

		// Move the shrunken arc forward (in radians) to align with the inner arc
		beginAngleRadians -= spaceWidthRadians * 0.5f;
	}

	float spacesTotalRadians = (float)pSrt->m_numSpaces * spaceWidthRadians;
	float dashesTotalRadians = arcRangeRadians - spacesTotalRadians;
	float dashWidthRadians = dashesTotalRadians / (float)numDashes;

	if (bDoesArcPassZero)
	{
		// Displace the begin and current angles so that they're between [0, 2PI]
		float diff = (2.0f * PI) - beginAngleRadians;
		beginAngleRadians = 0.0f;
		angle += diff;
		if (!(angle < (2.0f * PI)))
			angle -= 2.0f * PI;
	}

	// If the angle is within a space, alpha is 0
	for (uint i = 0; i < pSrt->m_numSpaces; i++)
	{
		float dashEnd = beginAngleRadians + ((float)(i + 1) * dashWidthRadians) + ((float)i * spaceWidthRadians);
		float nextDashStart = dashEnd + spaceWidthRadians;

		float dashAlpha = 1.f - (step(dashEnd, angle) * step(angle, nextDashStart));
		alpha *= dashAlpha;

		if (bIsSoftOutline)
		{
			// Add on soft outline when inside the space.
			float softOutlineAlpha = 0.0f;
			softOutlineAlpha = max(softOutlineAlpha, 1.0f - saturate((angle - dashEnd) / softOutlineRadians));
			softOutlineAlpha = max(softOutlineAlpha, 1.0f - saturate((nextDashStart - angle) / softOutlineRadians));
			alpha += (1.0f - dashAlpha) * softOutlineAlpha;
		}
	}

	return alpha;
}

float EllipseDashAlpha(float2 uvPixelTopLeft, float2 uvPixelCenter, float radiusInner_uv, bool bIsSoftOutline, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	if (pSrt->m_numSpaces == 0)
		return 1.0f;

	const float disableAAWidth = 0.1f;

	// Disable Arc AA at the middle since it will sample from both left and right and one of them won't pass.
	if (abs(uvPixelCenter.x) < disableAAWidth)
	{
		return EllipseDashSample(uvPixelCenter, radiusInner_uv, bIsSoftOutline, pSrt);
	}
	else
	{
		float alpha = 0.0;
		for (uint i = 0; i < kSampleCountEllipseAA; ++i)
		{
			float2 offset = s_aSampleEllipseAA[i].xy * pSrt->m_uvPerPixel;
			float2 uv = uvPixelTopLeft + offset;
			alpha += s_aSampleEllipseAA[i].z * EllipseDashSample(uv, radiusInner_uv, bIsSoftOutline, pSrt);
		}
		return saturate(alpha);
	}
}

// ---- Ellipse/Circle ----

PixelOutput Gui2EllipsePS(Gui2VertexOutput IN, float radiusOuter_uv, float radiusInner_uv, bool bIsSoftOutline, Gui2EllipsePsSrt* pSrt)
{
	PixelOutput OUT = (PixelOutput)0; 

	float2 uvRelativeToCenter = IN.uv - float2(0.5, 0.5);
	float2 uvPixelTopLeft = uvRelativeToCenter - float2(0.5, 0.5) * pSrt->m_uvPerPixel;

	float clipAlpha = Gui2DetermineClipAlpha(IN.position, pSrt->m_pBase);

	float radialAlpha = 0.0f;
	if (clipAlpha > 0.0f)
	{
		radialAlpha = EllipseRadialAlpha(uvPixelTopLeft, radiusInner_uv, radiusOuter_uv, pSrt);
	}

	float dashAlpha = 1.0f;
	if (pSrt->m_numSpaces > 0)
	{
		dashAlpha = EllipseDashAlpha(uvPixelTopLeft, uvRelativeToCenter, radiusInner_uv, bIsSoftOutline, pSrt);
	}

	OUT.col.a = clipAlpha * dashAlpha * saturate(radialAlpha * IN.color.a * pSrt->m_alphaMultiplier);
	OUT.col.rgb = IN.color.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);
	return OUT;
}

PixelOutput Gui2OutlineEllipsePS(Gui2VertexOutput IN, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	float  radiusOuter_uv = 0.5 - 1.0 * pSrt->m_uvPerPixel.x; // leave a one-pixel border for AA
	float  radiusInner_uv = 0.5 - (1.0 + pSrt->m_lineWidth_pix) * pSrt->m_uvPerPixel.x;

	return Gui2EllipsePS(IN, radiusOuter_uv, radiusInner_uv, false, pSrt);
}

float ComputeEllipseSoftOutlineFalloff(float2 uv, float radiusOuter_uv, float radiusInner_uv, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	float2 uvRelativeToCenter = uv - float2(0.5, 0.5);
	float radius_uv = length(uvRelativeToCenter);

	float lineWidth = pSrt->m_lineWidth_pix;
	float lineWidth_uv = lineWidth * pSrt->m_uvPerPixel.x;
	lineWidth_uv *= 0.5;	// Since the max outline distance from inner/outer radius is 0.5 line width

	float falloff = saturate(radius_uv - radiusOuter_uv);	// For outer outline
	falloff += saturate(radiusInner_uv - radius_uv);		// For inner outline
	falloff /= lineWidth_uv;
	falloff = 1.0 - saturate(falloff);
	return falloff;
}

PixelOutput Gui2SoftOutlineEllipsePS(Gui2VertexOutput IN, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	float radiusOuter_uv = 0.5 - 1.0 * pSrt->m_uvPerPixel.x; // leave a one-pixel border for AA
	float radiusInner_uv = 0.5 - (1.0 + pSrt->m_lineWidth_pix) * pSrt->m_uvPerPixel.x;

	float lineWidth = pSrt->m_lineWidth_pix;
	float softOutlineExtendedRadius_uv = 0.5 * lineWidth * pSrt->m_uvPerPixel.x;
	float radiusOuterOutline_uv = radiusOuter_uv + softOutlineExtendedRadius_uv;
	float radiusInnerOutline_uv = radiusInner_uv - softOutlineExtendedRadius_uv;
	PixelOutput OUT = Gui2EllipsePS(IN, radiusOuterOutline_uv, radiusInnerOutline_uv, true, pSrt);

	float falloff = ComputeEllipseSoftOutlineFalloff(IN.uv, radiusOuter_uv, radiusInner_uv, pSrt);	// Use original radii (before extension) to compute falloff
	OUT.col.a *= falloff;
	return OUT;
}

PixelOutput Gui2FilledEllipsePS(Gui2VertexOutput IN, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	float  radiusOuter_uv = 0.5 - 1.0 * pSrt->m_uvPerPixel.x; // leave a one-pixel border for AA
	float  radiusInner_uv = 0.0;

	return Gui2EllipsePS(IN, radiusOuter_uv, radiusInner_uv, false, pSrt);
}

// ---- Pie Slice of an Ellipse/Circle ----

PixelOutput Gui2PieSlicePS(Gui2VertexOutput IN, float radiusOuter_uv, float radiusInner_uv, bool bIsSoftOutline, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0; 

	float2 uvRelativeToCenter = IN.uv - float2(0.5, 0.5);
	float2 uvPixelTopLeft = uvRelativeToCenter - float2(0.5, 0.5) * pSrt->m_uvPerPixel;

	float radialAlpha = EllipseRadialAlpha(uvPixelTopLeft, radiusInner_uv, radiusOuter_uv, pSrt);
	float arcAlpha = EllipseArcAlpha(uvPixelTopLeft, uvRelativeToCenter, radiusInner_uv, bIsSoftOutline, pSrt);
	float clipAlpha = Gui2DetermineClipAlpha(IN.position, pSrt->m_pBase, (bool)pSrt->m_disableClipAA);
	float dashAlpha = 1.0f;
	if (pSrt->m_numSpaces > 0)
	{
		dashAlpha = EllipseDashAlpha(uvPixelTopLeft, uvRelativeToCenter, radiusInner_uv, bIsSoftOutline, pSrt);
	}

	OUT.col.a = radialAlpha * arcAlpha * clipAlpha * dashAlpha * IN.color.a;
	OUT.col.rgb = IN.color.rgb * lerp(1.0f, OUT.col.a, pSrt->m_premultAlpha);

	return OUT;
}

PixelOutput Gui2OutlinePieSlicePS(Gui2VertexOutput IN, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	float  radiusOuter_uv = 0.5 - 1.0 * pSrt->m_uvPerPixel.x; // leave a one-pixel border for AA
	float  radiusInner_uv = 0.5 - (1.0 + pSrt->m_lineWidth_pix) * pSrt->m_uvPerPixel.x;

	return Gui2PieSlicePS(IN, radiusOuter_uv, radiusInner_uv, false, pSrt);
}

PixelOutput Gui2SoftOutlinePieSlicePS(Gui2VertexOutput IN, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	float  radiusOuter_uv = 0.5 - 1.0 * pSrt->m_uvPerPixel.x; // leave a one-pixel border for AA
	float  radiusInner_uv = 0.5 - (1.0 + pSrt->m_lineWidth_pix) * pSrt->m_uvPerPixel.x;

	float lineWidth = pSrt->m_lineWidth_pix;
	float softOutlineExtendedRadius_uv = 0.5 * lineWidth * pSrt->m_uvPerPixel.x;
	float radiusOuterOutline_uv = radiusOuter_uv + softOutlineExtendedRadius_uv;
	float radiusInnerOutline_uv = radiusInner_uv - softOutlineExtendedRadius_uv;
	PixelOutput OUT = Gui2PieSlicePS(IN, radiusOuterOutline_uv, radiusInnerOutline_uv, true, pSrt);

	float falloff = ComputeEllipseSoftOutlineFalloff(IN.uv, radiusOuter_uv, radiusInner_uv, pSrt);	// Use original radii (before extension) to compute falloff
	OUT.col.a *= falloff;
	return OUT;
}

PixelOutput Gui2FilledPieSlicePS(Gui2VertexOutput IN, Gui2EllipsePsSrt* pSrt : S_SRT_DATA)
{
	float  radiusOuter_uv = 0.5 - 1.0 * pSrt->m_uvPerPixel.x; // leave a one-pixel border for AA
	float  radiusInner_uv = 0.0;

	return Gui2PieSlicePS(IN, radiusOuter_uv, radiusInner_uv, false, pSrt);
}

//------------------------------------------------------------------------------------------------------------
// Collision mesh debug draw

struct VsCollisionDebugParams
{
    matrix		m_altWorldViewProjMat;
	matrix		m_toAltWorldMat;
	matrix		m_meshToWorldMat;
	float4		m_jitterOffset;
	float4		m_lightDir;
	float4		m_camPos;
	float4		m_randomColor;
	uint2		m_patExclude;
	uint2		m_layerInclude;
	uint2		m_defaultLayerMask;
	uint2		m_highPatExclude;
	uint2		m_highLayerInclude;
	float		m_normalTreshold;
	uint		m_wireframe;
	uint		m_outerEdges;
	uint		m_instanceRandomColor;
};

struct CollisionDebugVsSrt
{
	DataBuffer<float4> m_posBuf;
	DataBuffer<float4> m_colorBuf;
	DataBuffer<uint2> m_patsBuf;
	VsCollisionDebugParams* m_pParams;
};

uint2 GetLayerMaskFromPat(uint2 pat2, uint2 defLayerMask)
{
	// We're slightly cheating here. We don't really know what is the layer of the triangle we just infer it from PAT
	bool noPhysics = pat2[0] & (1 << 26);					// kNoPhysicsShift = 26
	if ((pat2[1] & (1 << (35-32))) && !noPhysics)			// kBigPhysicsOnlyShift = 35
	{
		return uint2(1 << 19, 0);							// kLayerBigPhysicsBlocker = 19
	}
	else if (pat2[0] & (1 << 14))							// kWaterShift = 14
	{
		return uint2(1 << 2, 0);							// kLayerWater = 2
	}
	else if ((pat2[1] & (1 << (58-32))) && !noPhysics)		// kVehiclesThroughShift = 58
	{
		return uint2(1 << 28, 0);							// kLayerVehiclesThrough = 28
	}
	else if ((pat2[1] & (1 << (60-32))) && !noPhysics)		// kPushObjectsThroughShift = 60
	{
		return uint2(0, 1 << (32-32));						// kLayerPushObjectsThrough = 32
	}
	else
		return defLayerMask;
};

float4 GetTriInstanceRandomColor(float4 triData, float4 constRandColor)
{
	uint color16 = triData.b * (255*255) + triData.a * 255;
	if (color16 != 0)
	{
		float r = (color16 & 31) / 31.0f;
		float g = ((color16 >> 5) & 31) / 31.0f;
		float b = ((color16 >> 10) & 63) / 63.0f;
		return constRandColor * float4(r, g, b, 1.0f);
	}
	return constRandColor;
}

VertexOutputColor CollisionDebugVS(CollisionDebugVsSrt* pSrt : S_SRT_DATA, uint vertexId : S_VERTEX_ID)
{
	VertexOutputColor OUT;

	uint posId = vertexId;
	uint triId = vertexId / 3;
	uint triVtxId = vertexId - triId*3; 

	float4 triData = pSrt->m_colorBuf[vertexId];
	uint patIndex = triData.r * 255;
	uint2 pat2 = pSrt->m_patsBuf[patIndex];
	uint2 layer2 = GetLayerMaskFromPat(pat2, pSrt->m_pParams->m_defaultLayerMask);

	uint2 excludePat = pat2 & pSrt->m_pParams->m_patExclude;
	uint2 includeLayer = layer2 & pSrt->m_pParams->m_layerInclude;
	if ((excludePat.x || excludePat.y) || (includeLayer.x == 0 && includeLayer.y == 0))
	{
		// If exclude and this is vertex 2 we'll switch for vertex 1 to degenerate the triangle
		if (triVtxId == 2)
			posId--;
	}

	float4 outerEdgeColor = float4(0, 0, 0, 0);
	if (pSrt->m_pParams->m_outerEdges)
	{
		uint outerEdges = triData.g * 255;
		if (outerEdges == 0)
			outerEdgeColor = float4(0, 0, 0, 0);
		else if (outerEdges == 7)
			outerEdgeColor = float4(1, 1, 1, 1);
		else
		{
			int prevTriVtxId = (int)triVtxId-1;
			if (prevTriVtxId < 0)
				prevTriVtxId = 2;
			int nextTriVtxId = (triVtxId+1)%3;
			uint outerVtx = ((outerEdges >> triVtxId) & 1) + ((outerEdges >> prevTriVtxId) & 1);
			if (outerVtx == 0)
			{
				posId = triVtxId == 0 ? posId+1 : posId-1;
				outerEdgeColor = float4(1, 1, 1, 1);
			}
			else if (outerVtx == 2)
			{
				outerEdgeColor = float4(1, 1, 1, 1);
			}
			else if (((outerEdges >> nextTriVtxId) & 1) == 0)
			{
				outerEdgeColor = float4(1, 1, 1, 1);
			}
			else
			{
				outerEdgeColor = float4(0, 0, 0, 0);
			}
		}
	}

	float4 position = mul(pSrt->m_posBuf[posId], pSrt->m_pParams->m_meshToWorldMat);
	
	float4 normal = float4(normalize(cross(pSrt->m_posBuf[triId*3+1] - pSrt->m_posBuf[triId*3+0], pSrt->m_posBuf[triId*3+2] - pSrt->m_posBuf[triId*3+0])).xyz, 0.0f);
	normal = mul(normal, pSrt->m_pParams->m_meshToWorldMat);

	const float4 baseColor = float4(0.4f, 0.4f, 0.6f, 1.0f);
	const float4 backColor = float4(0.6f, 0.3f, 0.3f, 1.0f);
	const float4 highColor = float4(0.6f, 0.6f, 0.0f, 1.0f);

	float4 color = baseColor;
	if (dot((pSrt->m_pParams->m_camPos - position), normal) < 0.0f)
	{
		// Backside of triangle
		color = backColor;
		normal = -normal;
	}

	uint2 highPat = (pat2 & pSrt->m_pParams->m_highPatExclude);
	//uint2 highLayer = layer2 & pSrt->m_pParams->m_highLayerInclude;
	if (highPat.x != pat2.x || highPat.y != pat2.y || normal.y >= pSrt->m_pParams->m_normalTreshold)
	{
		color = highColor;
	}

	const float ambientLight = 0.1f;
	float diffLight = -dot(pSrt->m_pParams->m_lightDir, normal);
	float light = min(max(diffLight, 0.0f) + ambientLight, 1.0f);
	color = float4(light, light, light, 1.0f) * color;

	if (any(pSrt->m_pParams->m_randomColor != float4(0.0)))
	{
		if (pSrt->m_pParams->m_instanceRandomColor)
			color = GetTriInstanceRandomColor(triData, pSrt->m_pParams->m_randomColor);
		else
			color = pSrt->m_pParams->m_randomColor;
	}

	//{
	//	uint outerEdges = triData.g * 255;
	//	int prevTriVtxId = (int)triVtxId-1;
	//	if (prevTriVtxId < 0)
	//		prevTriVtxId = 2;
	//	int nextTriVtxId = (triVtxId+1)%3;
	//	uint outerVtx = ((outerEdges >> triVtxId) & 1) + ((outerEdges >> prevTriVtxId) & 1);
	//	color = float4(outerVtx * 0.5, outerVtx * 0.5, outerVtx * 0.5, 1);
	//}

	if (pSrt->m_pParams->m_wireframe)
		color = float4(1, 1, 1, 1);

	if (pSrt->m_pParams->m_outerEdges)
		color = outerEdgeColor;

	OUT.position = Get3dHPosition(position, pSrt->m_pParams->m_toAltWorldMat, pSrt->m_pParams->m_altWorldViewProjMat, pSrt->m_pParams->m_jitterOffset);
	OUT.color = color;
	
	return OUT;
}


//------------------------------------------------------------------------------------------------------------
// Geometry Shaders
//------------------------------------------------------------------------------------------------------------

// This geometry shader is used to create lines that are thick, by converting lines and a width value into quads
[maxvertexcount(4)]
void GS_ExpandLineToQuad(inout TriangleStream<VertexOutputColorDepth> triangleStream, line VertexOutputColorWidth verts[2], Vs2dGlobalParams* pParams : S_SRT_DATA)
{
	// The input width is screen-space pixel size, but our input is in homogenous clip space. Thus, what we need
	// to do is calculate the orthogonal vector to the vector between the two verts, then place our quad verts
	// offset along this vector by the width converted to clip space. Note that width should be a constant pixel
	// width, regardless of the perspective projection.

	float2 pixelsNear = pParams->m_screenScaleVector.xy;
	float nearDist = pParams->m_screenScaleVector.z;

	// If either of our points' Z positions are negative, that means that one of the points is behind
	// the near plane, and thus our calculations here will be incorrect unless we clip that point to
	// the near plane and calculate using that position instead.

	// If both clipped by near plane, just bail.
	if (verts[0].position.z <= 0.0 && verts[1].position.z <= 0.0)
		return;

	float4 hPos0;
	float4 hPos1;

	if (verts[0].position.z > 0.0 && verts[1].position.z > 0.0)
	{
		hPos0 = verts[0].position;
		hPos1 = verts[1].position;
	}
	else
	{
		float3 hRay = verts[1].position.xyz - verts[0].position.xyz;
		float t = -verts[0].position.z / hRay.z;
		float3 hPosNear = verts[0].position.xyz + hRay * t;

		if (verts[0].position.z > 0.0)
		{
			// verts[1] behind near plane
			hPos0 = verts[0].position;
			hPos1 = float4(hPosNear, nearDist);
		}
		else
		{
			// verts[0] behind near plane
			hPos0 = float4(hPosNear, nearDist);
			hPos1 = verts[1].position;
		}
	}

	float w0 = hPos0.w;
	float w1 = hPos1.w;
	float3 p0 = hPos0.xyz / w0;
	float3 p1 = hPos1.xyz / w1;

	float2 postProjLineDir = normalize(p1.xy - p0.xy);
	float2 postProjNormal = float2(postProjLineDir.y, -postProjLineDir.x);

	float2 width = float2(verts[0].width / pixelsNear.x, verts[0].width / pixelsNear.y);
	//width *= 2.0;

	// We know how far to move the verts to give us widths' worth of pixels, now figure out
	// which direction we need to move them in. The width already takes the ratio into account.
	float3 dirOffset = float3(postProjLineDir * width, 0.0);
	float3 nrmOffset = float3(postProjNormal * width, 0.0);

	float4 pts[4];
	pts[0] = float4(p0 - dirOffset - nrmOffset, 1.0) * w0;
	pts[1] = float4(p0 - dirOffset + nrmOffset, 1.0) * w0;
	pts[2] = float4(p1 + dirOffset - nrmOffset, 1.0) * w1;
	pts[3] = float4(p1 + dirOffset + nrmOffset, 1.0) * w1;

	VertexOutputColorDepth OUT;

	// Start
	OUT.color = verts[0].color;

	OUT.position = pts[0];
	OUT.depth = pts[0].z;
	triangleStream.Append(OUT);

	OUT.position = pts[1];
	OUT.depth = pts[1].z;
	triangleStream.Append(OUT);

	// End
	OUT.color = verts[1].color;

	OUT.position = pts[2];
	OUT.depth = pts[2].z;
	triangleStream.Append(OUT);

	OUT.position = pts[3];
	OUT.depth = pts[3].z;
	triangleStream.Append(OUT);
}

//------------------------------------------------------------------------------------------------------------
// Pixel Shaders
//------------------------------------------------------------------------------------------------------------
PixelOutput ColorOnlyThroughPS(DebugColorParams* pParams : S_SRT_DATA)
{
	PixelOutput OUT; 

	OUT.col = pParams->m_debugColor;

	return OUT;
}

PixelOutput ColorOnlyPS(VertexOutput IN)
{
	PixelOutput OUT; 

	OUT.col = IN.color;

	return OUT;
}

PixelOutput ColorGradientPS(VertexOutput IN)
{
	PixelOutput OUT;

	OUT.col.rgb = IN.color.rgb * (1.0f - IN.uv.y);
	OUT.col.a = 1.0;

	return OUT;
}

PixelOutput ColorHGradientPS(VertexOutput IN)
{
	PixelOutput OUT;

	OUT.col.rgb = IN.color.rgb * IN.uv.x;
	OUT.col.a = 1.0;

	return OUT;
}

PixelOutput ColorHuePS(VertexOutput IN)
{
	PixelOutput OUT;

	float3 hslColor = float3(0.0f, 0.5f, 0.5f);
	hslColor.x += IN.uv.y;
	OUT.col.rgb = HSLtoRGB(hslColor);
	OUT.col.a = 1.0;

	return OUT;
}

PixelOutput ColorSaturationPS(VertexOutput IN)
{
	PixelOutput OUT;

	float3 hslColor = RGBtoHSL(IN.color.rgb);
	hslColor.y = IN.uv.y;
	OUT.col.rgb = HSLtoRGB(hslColor);
	OUT.col.a = 1.0;

	return OUT;
}

PixelOutput ColorBrightnessPS(VertexOutput IN)
{
	PixelOutput OUT;

	float3 hslColor = RGBtoHSL(IN.color.rgb);
	hslColor.z = IN.uv.y;
	OUT.col.rgb = HSLtoRGB(hslColor);
	OUT.col.a = 1.0;

	return OUT;
}

PixelOutput ColorPickerPS(VertexOutput IN)
{
	PixelOutput OUT;

	float3 hslColor = RGBtoHSL(IN.color.rgb);
	hslColor.xy += IN.uv.xy;
	OUT.col.rgb = pow(HSLtoRGB(hslColor), 2.2);
	OUT.col.a = 1.0;

	return OUT;
}

PixelOutput DebugColorPS(VertexOutputColor IN)
{
	PixelOutput OUT; 

	OUT.col = IN.color;

	return OUT;
}

struct DrawTexArraySliceSrt
{
	Texture2D_Array<Text2Color> m_texArray;
	SamplerState				m_sSampler;
	uint						m_sliceIndex;
};

PixelOutput DrawTexArraySlicePS(VertexOutput IN, DrawTexArraySliceSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0;

	float4 sampleColor = pSrt->m_texArray.Sample(pSrt->m_sSampler, float3(IN.uv.xy, (float)pSrt->m_sliceIndex));
	OUT.col = sampleColor;
	return OUT;
}

#define kHiddenLineAlpha 0.25f
#define kHiddenLineFadeDistance 25.0f

float CalculateHiddenLineFade(float depth)
{
	return 1.0f - clamp(depth / kHiddenLineFadeDistance, 0.0f, 0.95f);
}

PixelOutput DebugColorHiddenLinePS(VertexOutputColorDepth IN)
{
	PixelOutput OUT; 

	float depthFade = CalculateHiddenLineFade(IN.depth);
	OUT.col = float4(IN.color.rgb, IN.color.a * kHiddenLineAlpha * depthFade);

	return OUT;
}

PixelOutput DebugColorOnlyHiddenLinePS(VertexOutputColorOnlyDepth IN, DebugColorParams* pParams : S_SRT_DATA)
{
	PixelOutput OUT;

	float depthFade = CalculateHiddenLineFade(IN.depth);
	OUT.col = float4(pParams->m_debugColor.rgb, pParams->m_debugColor.a * kHiddenLineAlpha * depthFade);

	return OUT;
}

PixelOutput DebugLightProbeDiffuseResponsePs(VertexOutputNormal IN, DebugLightProbeParams* pParams : S_SRT_DATA)
{
	PixelOutput OUT; 

	float3 lightCoeffs[SH9_NUM_COEFFS];
	lightCoeffs[0] = pParams->m_shes[0].xyz;
	lightCoeffs[1] = pParams->m_shes[1].xyz;
	lightCoeffs[2] = pParams->m_shes[2].xyz;
	lightCoeffs[3] = pParams->m_shes[3].xyz;
	lightCoeffs[4] = pParams->m_shes[4].xyz;
	lightCoeffs[5] = pParams->m_shes[5].xyz;
	lightCoeffs[6] = pParams->m_shes[6].xyz;
	lightCoeffs[7] = pParams->m_shes[7].xyz;
	lightCoeffs[8] = pParams->m_shes[8].xyz;

	float clampedCosLobeCoeffs[SH9_NUM_COEFFS];
	CalcClampedCosineLobeShCoeffs(normalize(IN.normal), clampedCosLobeCoeffs);

	// output diffuse response
	OUT.col.rgb = max(DotSh(lightCoeffs, clampedCosLobeCoeffs) / kPi, 0.0f);
	OUT.col.a = pParams->m_alpha;

	return OUT;
}

PixelOutput DebugLightProbeRadiancePs(VertexOutputNormal IN, DebugLightProbeParams* pParams : S_SRT_DATA)
{
	PixelOutput OUT;

	float3 lightCoeffs[SH9_NUM_COEFFS];
	lightCoeffs[0] = pParams->m_shes[0].xyz;
	lightCoeffs[1] = pParams->m_shes[1].xyz;
	lightCoeffs[2] = pParams->m_shes[2].xyz;
	lightCoeffs[3] = pParams->m_shes[3].xyz;
	lightCoeffs[4] = pParams->m_shes[4].xyz;
	lightCoeffs[5] = pParams->m_shes[5].xyz;
	lightCoeffs[6] = pParams->m_shes[6].xyz;
	lightCoeffs[7] = pParams->m_shes[7].xyz;
	lightCoeffs[8] = pParams->m_shes[8].xyz;

	// output radiance
	OUT.col.rgb = max(EvaluateSh(lightCoeffs, normalize(IN.normal)), 0.0f);
	OUT.col.a = pParams->m_alpha;

	return OUT;
}

PixelOutput DebugLightProbeShadowPs(VertexOutputNormal IN, DebugLightProbeParams* pParams : S_SRT_DATA)
{
	PixelOutput OUT;

	// output radiance
	OUT.col.rgb = pParams->m_shadow;
	OUT.col.a = pParams->m_alpha;

	return OUT;
}

PixelOutput DebugLightProbeOcclusionPs(VertexOutputNormal IN, DebugLightProbeParams* pParams : S_SRT_DATA)
{
	PixelOutput OUT;

	float occluderDepths[PROBE_OCCLUSION_NUM_OCCLUDER_DEPTHS];
	float3x3 invTbn;
	DecodeProbeOcclusion(pParams->m_occlusion, occluderDepths, invTbn);

	float3 dir = mul(invTbn, normalize(IN.normal));

	uint index = CubeMapFaceID(dir.x, dir.y, dir.z);
	float occluderDepth = saturate(occluderDepths[index]);

	// output occluder depth
	OUT.col.rgb = occluderDepth;
	OUT.col.a = pParams->m_alpha;

	return OUT;
}

//------------------------------------------------------------------------------------------------------------
struct FontPsSrt
{
	Texture2D m_txFont;
	Texture2D m_txDistress;
	SamplerState m_sSamplerLinear;
};

PixelOutput FontDebugPS(VertexOutputFont IN, FontPsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT = (PixelOutput)0; 

	float4 color = pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy);

	float4 alpha = float4(0,0,0,0);
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-1.0/128.0, -1.0/128.0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-1.0/128.0, 0.0/128.0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-1.0/128.0, 1.0/128.0));

	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(0.0/128.0, -1.0/128.0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(0.0/128.0, 0.0/128.0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(0.0/128.0, 1.0/128.0));

	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(1.0/128.0, -1.0/128.0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(1.0/128.0, 0.0/128.0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(1.0/128.0, 1.0/128.0));
	
	float col = dot(color, IN.channelMask);
	float a = dot(alpha, IN.channelMask);

	a += dot(IN.distressUv.xy, float2(0.00001, 0.00001));

	float outline = IN.outlineColor.a;
	OUT.col.rgb = lerp(1, col, outline) * IN.color.rgb;
	OUT.col.a = saturate(col + outline * a) * IN.color.a;

	return OUT;
}

//------------------------------------------------------------------------------------------------------------
PixelOutput FontGamePS(VertexOutputFont IN, FontPsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT; 

	float4 color = pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy);

	float4 alpha = float4(0,0,0,0);
	float offsetX = 1.0/IN.distressUv.z;
	float offsetY = 1.0/IN.distressUv.w;
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-offsetX, -offsetY));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-offsetX, 0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-offsetX, offsetY));

	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(0, -offsetY));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(0, 0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(0, offsetY));

	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(offsetX, -offsetY));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(offsetX, 0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(offsetX, offsetY));
	
	float col = dot(color, IN.channelMask);
	float a = saturate(dot(alpha, IN.channelMask));
	
	a = lerp(col, a, IN.outlineColor.a);
	a *= pSrt->m_txDistress.Sample(pSrt->m_sSamplerLinear, IN.distressUv.xy).r;

	OUT.col.rgb = lerp(IN.outlineColor.rgb, IN.color.rgb, col);
	OUT.col.a = a * IN.color.a;
	
	return OUT;
}

//------------------------------------------------------------------------------------------------------------
PixelOutput FontGamePSLegacy(VertexOutputFont IN, FontPsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT; 

	//float4 color = txFont.Sample(g_sSamplerLinear, IN.uv.xy);

	float4 alpha = float4(0,0,0,0);
	float2 offsetX = ddx(IN.uv.xy) *  1.5; // account for new resolution
	float2 offsetY = ddy(IN.uv.xy) * -1.5; // account for new resolution and y flip
	/*alpha += tex2D(sampler, IN.uv.xy + float2(-offsetX, -offsetY));
	alpha += tex2D(sampler, IN.uv.xy + float2(-offsetX, 0));
	alpha += tex2D(sampler, IN.uv.xy + float2(-offsetX, offsetY));

	alpha += tex2D(sampler, IN.uv.xy + float2(0, -offsetY));
	alpha += tex2D(sampler, IN.uv.xy + float2(0, 0));
	alpha += tex2D(sampler, IN.uv.xy + float2(0, offsetY));

	alpha += tex2D(sampler, IN.uv.xy + float2(offsetX, 0));
	alpha += tex2D(sampler, IN.uv.xy + float2(offsetX, 0));
	alpha += tex2D(sampler, IN.uv.xy + float2(offsetX, offsetY));*/


	float red = dot(pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy - 0.25 * offsetX), IN.channelMask);
	float green = dot(pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy), IN.channelMask);
	float blue = dot(pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + 0.25 * offsetX), IN.channelMask);

	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy - 1.0 * (offsetX - offsetY)) * IN.outlineColor.a;

	float col = (red+green+blue) / 3.0f; //dot(color, IN.channelMask);
	float a = saturate(dot(alpha, IN.channelMask));
	//IN.outlineColor.rgb *= 0.001;

	OUT.col.rgb = lerp(float3(0,0,0), IN.color.rgb, col);
	OUT.col.rgb = float3(blue, green, red) * IN.color.rgb;
	OUT.col.a = max(col,a) * IN.color.a;

	
	/*a = lerp(col, a, IN.outlineColor.a);
	//a *= tex2D(g_distresg_sSampler, IN.distressUv.xy).r;

	OUT.col.rgb = lerp(IN.outlineColor.rgba, IN.color.rgb, col);
	OUT.col.a = a * IN.color.a;*/
/*	
	float4 color = tex2D(sampler, IN.uv.xy);
	float col = dot(color, IN.channelMask);
	OUT.col.rgb = IN.color.rgb;
	OUT.col.a = col * IN.color.a;
*/	
	return OUT;
}

//------------------------------------------------------------------------------------------------------------
PixelOutput FontGameNoDistressPS(VertexOutputFont IN, FontPsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT; 

	float4 color = pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy);

	float4 alpha = float4(0,0,0,0);
	float4 shadow = float4(0,0,0,0);
	float offsetX = 1.0/IN.distressUv.z;
	float offsetY = 1.0/IN.distressUv.w;

	shadow += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-offsetX*.5, -offsetY*2.5));
	shadow += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-offsetX*.5, -offsetY*1.5));
	shadow += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-offsetX*.5, -offsetY*0.5));

	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-offsetX, -offsetY));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-offsetX, 0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(-offsetX, offsetY));

	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(0, -offsetY));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(0, 0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(0, offsetY));

	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(offsetX, -offsetY));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(offsetX, 0));
	alpha += pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy + float2(offsetX, offsetY));

	shadow = alpha;
	
	float s = dot(shadow, IN.channelMask);
	float col = dot(color, IN.channelMask);
	float a = saturate(dot(alpha, IN.channelMask));

	s = pow(abs(s), 0.45f);
	col = pow(abs(col), 0.45f);
	a = pow(a, 0.45f);

	a = lerp(col, a, IN.outlineColor.a);
	a *= pSrt->m_txDistress.Sample(pSrt->m_sSamplerLinear, IN.distressUv.xy).r;

	float4 pixelColor;
	pixelColor.rgb = lerp(IN.outlineColor.rgb, IN.color.rgb, col);
	pixelColor.a = a * IN.color.a;
	//pixelColor.rgba = 0;

	float4 shadowColor = float4(0,0,0, s*IN.color.a);
	float4 finalColor = lerp(shadowColor, pixelColor, pixelColor.a);

	OUT.col.rgba = finalColor;
	//OUT.col.a = a * IN.color.a;


	return OUT;
}

//------------------------------------------------------------------------------------------------------------
PixelOutput SymbolFontPS(VertexOutputFont IN, FontPsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT; 
	OUT.col = IN.color * pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy);
	OUT.col.a += IN.channelMask.x * 0.00001 + IN.distressUv.x * 0.00001;

	return OUT;
}

//------------------------------------------------------------------------------------------------------------
PixelOutput SymbolFontPSLegacy(VertexOutputFont IN, FontPsSrt* pSrt : S_SRT_DATA)
{
	PixelOutput OUT; 
	OUT.col.rgb = IN.color.rgb * pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy).rgb;
	OUT.col.a = IN.color.a * pSrt->m_txFont.Sample(pSrt->m_sSamplerLinear, IN.uv.xy).a;
	
//	OUT.col.a += IN.channelMask.x * 0.00001 + IN.distressUv.x * 0.00001;

	return OUT;
}
