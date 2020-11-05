#define GLOBAL static

#define ND_PSSL
#define IS_COMPUTE_SHADER
#define IS_SUBSURFACE_COMPUTE_SHADER
#define HAS_SSSS

#include "../include/math-util.fxi"
#include "../include/global-definition.fxi"
#include "../physical-shader/globals.fxi"

#include "structs.fxi"
#include "packing.fxi"

#include "../physical-shader/misc.fxi"
#include "../physical-shader/brdf.fxi"

#include "deferred-util.fxi"
#include "texture-table.fxi"

struct SkinDiffuseConstants
{
	float4x4					m_screenToAltWorldMat;
	float4x4					m_worldToScreenMat;
	float2						m_invScreenSize;
	float2						m_screenViewParameter;

	float3						m_sunlightDir;
	float						m_cutsceneSunMultiplier;
	float						m_characterCutsceneSunMultiplier;
	float						m_genericRenderSettingDebug;
	float						m_enableColorizedAo;
	float						m_ambientCharacterScaleHair;
	float						m_ambientCharacterScaleTeeth;
	float3						m_ambientTint;
	float						m_nonshadowOmniLightsFakeOcclusion;
	float3						m_sunlightColor;
	float						m_areaSunlightScale;
	float						m_defaultSunShadow;
	float2						m_sunShadowFade;
	uint2						m_bufferSize;
	float						m_sunRadius;

	float3						m_cameraWS;
	uint4						m_lowDiscrepancySampleParams;

	uint						m_enableMultipleScattering;
	float						m_transBlockerDist;
	float						m_nonshadowSpotLightsFakeOcclusion;
	float						m_nonshadowFlashlightFakeOcclusion;

	uint						m_enableCloudShadowCaustics;
	float						m_cloudShadowCausticsWaterHeight;
	float						m_cloudShadowCausticsAmbientIntensity;
	float						m_eyeLightWrapBias;
	float						m_eyeScreenSpaceShadowStrength;
	uint						m_eyeTeethScreenSpaceShadowForNonShadowLight;
	float						m_eyeTeethRuntimeLightUseSsaoStrength;
	float						m_eyeTeethScreenSpaceShadowMinDepthMult;
	float						m_teethScreenSpaceShadowStrength;
	float						m_teethAmbientLightingMultiplier;
	float						m_teethLightingMultiplier;
	float						m_eyeAmbientLightingMultiplier;
	float						m_eyeLightingMultiplier;
};

struct SkinDiffuseRwBuf
{
	RWTexture2D<float3>			m_rwTarget;
};

struct SkinDiffuseBufs 
{
	Texture2D<uint4>			m_gBuffer0;
	Texture2D<uint4>			m_gBuffer1;
	Texture2D<uint4>			m_gBuffer2;
	Texture2D<float3>			m_ambientBaseBuffer;
	Texture2D<float3>			m_ambientDirectionalBuffer;
	Texture2D<float>			m_depthBuffer;
	Texture2D<float>			m_opaqueAlphaDepthBuffer;
	Texture2D<float3>			m_bounceLightBuffer;
	Texture2D<uint>				m_texMaterialMask;
	Texture2D<uint>				m_stencilBuffer;
	Texture2D<float4>			m_cloudShadow;
	Texture2D<float4>			m_ssaoBuffer;
	Texture2D<float>			m_specBrdfEnergyLut;

	LocalShadowTable*			m_pLocalShadowTextures;
	Texture2D<float>			m_runtimeLightSssBuffer;
	Texture2DArray<float2>		m_minMaxMaps;
	Texture2D<float>			m_sunShadowTexture;
	Texture3D<float2>			m_lowDiscrepancyTexture;

	RegularBuffer<LightDesc>	m_lightsBuffer;
	RegularBuffer<uint>			m_numLightsPerTileBuffer;
	RegularBuffer<uint>			m_lightsPerTileBuffer;
};

struct SkinDiffuseSamplers
{
	SamplerState				m_linearSampler;
	SamplerComparisonState		m_shadowSampler;
	SamplerState				m_linearClampVSampler;
	SamplerState				m_linearClampSampler;
	SamplerState				m_pointSampler;
};

struct SkinDiffusePsSrt
{
	SkinDiffuseBufs *          pBufs;
	SkinDiffuseConstants *     pConsts;
	SkinDiffuseSamplers *      pSamplers;
};

struct SkinDiffuseCsSrt
{
	SkinDiffuseBufs *          pBufs;
	SkinDiffuseConstants *     pConsts;
	SkinDiffuseSamplers *      pSamplers;
	SkinDiffuseRwBuf *         pOutput;
};

void OverrideMaterial(inout Setup setup, inout BrdfParameters brdfParameters)
{
	// We don't care about most stuff in material mask
	setup.materialMask.hasFuzz = setup.materialMask.hasMetallic = 
		setup.materialMask.hasHair = setup.materialMask.hasFur = setup.materialMask.hasEmissive = false;

	setup.materialMask.isCharacter = true;

	brdfParameters.metallic = 0;
	brdfParameters.baseColor = float3(1,1,1);
	setup.fuzzBlend = 0;
}

void LoadGBuffer(uint2 coord, SkinDiffuseBufs *pBufs, uint materialMask, out BrdfParameters brdfParameters, inout Setup setup)
{
	uint4 sample0 = pBufs->m_gBuffer0.Load(int3(coord, 0)).zwzw;
	uint4 sample1 = pBufs->m_gBuffer1.Load(int3(coord, 0));

	InitBrdfParameters(brdfParameters);
	UnpackGBuffer(sample0, sample1, brdfParameters, setup);
	UnpackMaterialMask(materialMask, sample1, setup.materialMask);

	uint4 sample2 = pBufs->m_gBuffer2.Load(int3(coord, 0));
	UnpackEyesSpecialMaterialMask(sample2, setup.materialMask);
	UnpackExtraGBuffer(sample2, setup.materialMask, brdfParameters, setup);
	
	// Override material mask parameters (mostly for shader optimization - won't affect outcome)
	OverrideMaterial(setup, brdfParameters);
}

float3 GetPositionWS(uint2 coord, float depthZ, SkinDiffuseConstants *pConsts)
{
	float2 ndc = ((float2)coord + float2(0.5f, 0.5f)) * pConsts->m_invScreenSize;
	ndc.x = ndc.x * 2.f - 1.f;
	ndc.y = (1 - ndc.y) * 2.f - 1.f;

	float4 positionAS = mul(float4(ndc, depthZ, 1.f), pConsts->m_screenToAltWorldMat);
	positionAS.xyz /= positionAS.w;

	float3 positionWS = positionAS.xyz + pConsts->m_cameraWS;

	return positionWS;
}

void InitSetup(uint2 coord, SkinDiffuseConstants *pConsts, SkinDiffuseBufs *pBufs, SkinDiffuseSamplers *pSamplers, inout Setup setup)
{
	// Get depthZ 
	const float depthZ			= pBufs->m_depthBuffer.Load(int3(coord, 0));
	const float viewZ			= pConsts->m_screenViewParameter.y / (depthZ - pConsts->m_screenViewParameter.x);

	setup.passId				= kPassDeferred;
	setup.positionWS			= GetPositionWS(coord, depthZ, pConsts);
	setup.viewWS				= pConsts->m_cameraWS - setup.positionWS;
	setup.cameraPositionWS		= pConsts->m_cameraWS;
	setup.distanceToCamera		= length(setup.viewWS);
	setup.viewWS				/= setup.distanceToCamera;
	setup.depthVS				= viewZ;
	setup.isFg					= true;
	setup.screenPosition.xy		= coord + 0.5f;
	setup.stencilBits			= pBufs->m_stencilBuffer.Load(int3(coord, 0));
	setup.sunDirWS				= pConsts->m_sunlightDir;
	setup.screenUv				= setup.screenPosition.xy * pConsts->m_invScreenSize;
	setup.ssao					= pBufs->m_ssaoBuffer.SampleLevel(pSamplers->m_linearClampSampler, setup.screenUv, 0).ra;
	setup.ambientShadow			= 1.0;

	g_altWorldOrigin			= pConsts->m_cameraWS;
}

float3 GetRuntimeLightsAccumulation(uint2 groupId, const BrdfParameters brdfParameters, const Setup setup, SkinDiffuseConstants *pConsts, SkinDiffuseBufs *pBufs)
{
	float3 totalLight = 0;
	
	uint tileIndex = groupId.x + groupId.y * (pConsts->m_bufferSize.x/TILE_SIZE);
	
	uint numLights = ReadFirstLane(pBufs->m_numLightsPerTileBuffer[tileIndex]);

	for (uint i = 0; i < numLights; ++i)
	{
		uint lightIndex = tileIndex*MAX_LIGHTS_PER_TILE + i;
		const LightDesc lightDesc = pBufs->m_lightsBuffer[ReadFirstLane(pBufs->m_lightsPerTileBuffer[lightIndex])];

		DeferredDebug dummyDebug;
		totalLight += GetRuntimeLightAccumulation(dummyDebug, lightDesc, brdfParameters, setup, false, false);
	}

	return totalLight;
}

void ComputeSunShadow(inout Setup setup, const BrdfParameters brdfParameters, uint2 coord, SkinDiffuseConstants *pConsts, SkinDiffuseBufs *pBufs, SkinDiffuseSamplers *pSamplers)
{
	g_sunShadowFade = pConsts->m_sunShadowFade;
	
	float sunShadowDynamic = max(pBufs->m_sunShadowTexture.Load(int3(coord, 0)) - 1.0f / 255.0f, 0.0f) * 255.0f / 254.0f;
	float2 cloudShadowSample = pBufs->m_cloudShadow.SampleLevel(pSamplers->m_pointSampler, (coord + 0.5f) * pConsts->m_invScreenSize, 0).rb;
	float cloudShadow = cloudShadowSample.x;
	float causticsSample = cloudShadowSample.y;
	
	setup.sunShadow = CalculateSunShadow(setup, brdfParameters, cloudShadow, sunShadowDynamic, brdfParameters.extraSunShadow, kPassDeferred);
	setup.sunShadow *= CalculateSunShadowCaustics(setup, causticsSample);
}

float3 SkinDiffuse(uint2 dispatchId, uint2 groupId, in SkinDiffuseBufs *pBufs, in SkinDiffuseConstants *pConsts, in SkinDiffuseSamplers *pSamplers, in bool isDebug)
{	
	// Set up some globals
	g_tSpecBrdfEnergyLut				= pBufs->m_specBrdfEnergyLut;
	g_enableMultipleScattering			= pConsts->m_enableMultipleScattering;
	g_localShadowsData					= pBufs->m_pLocalShadowTextures;
	g_tRuntimeLightSss					= pBufs->m_runtimeLightSssBuffer;
	g_tMinMaxShadow						= pBufs->m_minMaxMaps;
	g_tCurrentDepth						= pBufs->m_depthBuffer;
	g_tOpaquePlusAlphaDepth				= pBufs->m_opaqueAlphaDepthBuffer;
	g_linearSampler						= pSamplers->m_linearSampler;
	g_shadowSampler						= pSamplers->m_shadowSampler;
	g_linearClampVSampler				= pSamplers->m_linearClampVSampler;
	g_linearClampSampler				= pSamplers->m_linearClampSampler;
	g_pointSampler						= pSamplers->m_pointSampler;
	g_tLowDiscrepancy					= pBufs->m_lowDiscrepancyTexture;
	g_lowDiscrepancySampleParams		= pConsts->m_lowDiscrepancySampleParams;
	g_areaSunlightScale					= pConsts->m_areaSunlightScale;
	g_graphicsQualityFlags				= 0; // always calculate diffuse in subsurface pass
	g_enableColorizedAo					= pConsts->m_enableColorizedAo;
	g_ambientCharacterScaleHair			= pConsts->m_ambientCharacterScaleHair;
	g_ambientCharacterScaleTeeth		= pConsts->m_ambientCharacterScaleTeeth;
	g_ambientTint						= float4(pConsts->m_ambientTint, 0.0);
	g_nonshadowOmniLightsFakeOcclusion	= pConsts->m_nonshadowOmniLightsFakeOcclusion * 0.5;	// Less intense on skin, visually tuned
	g_nonshadowSpotLightsFakeOcclusion	= pConsts->m_nonshadowSpotLightsFakeOcclusion * 0.5;	// Less intense on skin, visually tuned
	g_nonshadowFlashlightFakeOcclusion	= pConsts->m_nonshadowFlashlightFakeOcclusion * 0.5;	// Less intense on skin, visually tuned

	g_genericRenderSettingDebug			= pConsts->m_genericRenderSettingDebug;

	g_eyeLightWrapBias					= pConsts->m_eyeLightWrapBias;
	g_eyeScreenSpaceShadowStrength		= pConsts->m_eyeScreenSpaceShadowStrength;
	g_eyeTeethScreenSpaceShadowForNonShadowLight = pConsts->m_eyeTeethScreenSpaceShadowForNonShadowLight;
	g_eyeTeethRuntimeLightUseSsaoStrength = pConsts->m_eyeTeethRuntimeLightUseSsaoStrength;
	g_eyeTeethScreenSpaceShadowMinDepthMult = pConsts->m_eyeTeethScreenSpaceShadowMinDepthMult;
	g_teethScreenSpaceShadowStrength	= pConsts->m_teethScreenSpaceShadowStrength;
	g_teethAmbientLightingMultiplier	= pConsts->m_teethAmbientLightingMultiplier;
	g_teethLightingMultiplier			= pConsts->m_teethLightingMultiplier;
	g_eyeAmbientLightingMultiplier		= pConsts->m_eyeAmbientLightingMultiplier;
	g_eyeLightingMultiplier				= pConsts->m_eyeLightingMultiplier;

	g_mWorldToScreen					= pConsts->m_worldToScreenMat;
	g_mScreenToAltWorld					= pConsts->m_screenToAltWorldMat;
	
	// For screen-space shadows on eyes
	g_tGBuffer1							= pBufs->m_gBuffer1;
	g_tGBuffer2							= pBufs->m_gBuffer2;
	g_debugParams.z						= 0;
	g_vScreenViewParameter.xy			= pConsts->m_screenViewParameter;

	g_skinTransBlockerDist				= pConsts->m_transBlockerDist;

	// cloud shadow caustics
	g_enableCloudShadowCaustics = pConsts->m_enableCloudShadowCaustics;
	g_cloudShadowCausticsWaterHeight = pConsts->m_cloudShadowCausticsWaterHeight;
	g_cloudShadowCausticsAmbientIntensity = pConsts->m_cloudShadowCausticsAmbientIntensity;

	uint2 coord							= dispatchId.xy;
	uint materialMask					= pBufs->m_texMaterialMask.Load(int3(coord, 0));

	bool isSkin = materialMask & MASK_BIT_SKIN;
	if (!isSkin)
		return float3(0,0,0);

	Setup setup = (Setup)0;
	BrdfParameters brdfParameters;
	LoadGBuffer(coord, pBufs, materialMask, brdfParameters, setup);

	InitSetup(coord, pConsts, pBufs, pSamplers, setup);
	setup.sunColor						= pConsts->m_sunlightColor;
	setup.sunDirWS						= pConsts->m_sunlightDir;
	setup.sunRadius						= pConsts->m_sunRadius;
	setup.ambientBaseColor				= pBufs->m_ambientBaseBuffer.Load(int3(coord, 0));
	setup.ambientDirectionalColor		= pBufs->m_ambientDirectionalBuffer.Load(int3(coord, 0));
	
	float2 dummyMotionVector;
	ComputeSunShadow(setup, brdfParameters, coord, pConsts, pBufs, pSamplers);
	float3 sunLight = CalculateSunLight(setup, brdfParameters, pConsts->m_cutsceneSunMultiplier, pConsts->m_characterCutsceneSunMultiplier);
	float3 runtimeLight = GetRuntimeLightsAccumulation(groupId, brdfParameters, setup, pConsts, pBufs);

	float2 ambientShadowSample = pBufs->m_cloudShadow.SampleLevel(pSamplers->m_pointSampler, (coord + 0.5f) * pConsts->m_invScreenSize, 0).gb;
	float ambientShadow = ambientShadowSample.x;
	ambientShadow *= CalculateAmbientCaustics(setup, ambientShadowSample.y);
	float3 ambientLight = CalculateAmbientLighting(setup, brdfParameters, dummyMotionVector, ambientShadow);

	float3 bounceLight = pBufs->m_bounceLightBuffer.SampleLevel(pSamplers->m_linearClampSampler, setup.screenUv, 0).rgb * setup.ssao.y;

	float3 totalLight = sunLight + runtimeLight + ambientLight + bounceLight;
	
	return totalLight;
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CS_SkinDiffuse(uint3 dispatchId : S_DISPATCH_THREAD_ID, uint3 groupId : S_GROUP_ID, SkinDiffuseCsSrt *pSrt : S_SRT_DATA)
{
	g_enableSkinTransHQ = false;
	pSrt->pOutput->m_rwTarget[dispatchId.xy] = SkinDiffuse(dispatchId.xy, groupId.xy, pSrt->pBufs, pSrt->pConsts, pSrt->pSamplers, false);
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CS_SkinDiffuseHQ(uint3 dispatchId : S_DISPATCH_THREAD_ID, uint3 groupId : S_GROUP_ID, SkinDiffuseCsSrt *pSrt : S_SRT_DATA)
{
	g_enableSkinTransHQ = true;
	pSrt->pOutput->m_rwTarget[dispatchId.xy] = SkinDiffuse(dispatchId.xy, groupId.xy, pSrt->pBufs, pSrt->pConsts, pSrt->pSamplers, false);
}

struct PS_PosTex
{
    float4 Pos		: SV_POSITION;
    float2 Tex		: TEXCOORD0;
};

float4 PS_SkinDiffuse(PS_PosTex psInput, SkinDiffusePsSrt *pSrt : S_SRT_DATA)
{
	g_enableSkinTransHQ = false;

	uint2 dispatchId = (uint2)psInput.Pos.xy; 
	uint2 groupId = uint2(dispatchId.x / TILE_SIZE, dispatchId.y / TILE_SIZE);

	return float4(SkinDiffuse(dispatchId, groupId, pSrt->pBufs, pSrt->pConsts, pSrt->pSamplers, false), 1.0f);
}

float4 PS_SkinDiffuseHQ(PS_PosTex psInput, SkinDiffusePsSrt *pSrt : S_SRT_DATA)
{
	g_enableSkinTransHQ = true;

	uint2 dispatchId = (uint2)psInput.Pos.xy; 
	uint2 groupId = uint2(dispatchId.x / TILE_SIZE, dispatchId.y / TILE_SIZE);

	return float4(SkinDiffuse(dispatchId, groupId, pSrt->pBufs, pSrt->pConsts, pSrt->pSamplers, false), 1.0f);
}

struct SsssSrt
{
	Texture2D<float3>			m_ssssInput;
	Texture2D<uint4>			m_gBuffer2;
	Texture2D<float>			m_depthBuffer;
	Texture2D<uint>				m_materialMask;
	Texture2D<float2>			m_randomRotTexture;
	RWTexture2D<float3>			m_ssssOutput;

	SamplerState				m_pointClampSampler;
	SamplerState				m_linearClampSampler;

	float2						m_screenViewParameter;
	float2						m_invScreenSize;
	float						m_blurRadiusScale;
	uint3						m_sampleParams;
	float						m_ssRadiusMultiplier;
	float						m_maxDepthDiffMultiplier;
};

#define NUM_SSSS_SAMPLES 13

enum SsssBlurTypes
{
	SSSS_NON_SEP_BLUR=0,
	SSSS_SEP_HOR_BLUR,
	SSSS_SEP_VERT_BLUR
};

static const float2 ssssNonSepKernelOffsets[NUM_SSSS_SAMPLES] =
{
	{ 0.000000f, 0.000000f },
	{ 1.633992f, 0.036795f },
	{ 0.177801f, 1.717593f },
	{ -0.194906f, 0.091094f },
	{ -0.239737f, -0.220217f },
	{ -0.003530f, -0.118219f },
	{ 1.320107f, -0.181542f },
	{ 5.970690f, 0.253378f },
	{ -1.089250f, 4.958349f },
	{ -4.015465f, 4.156699f },
	{ -4.063099f, -4.110150f },
	{ -0.638605f, -6.297663f },
	{ 2.542348f, -3.245901f }
};

static const float3 ssssNonSepKernelWeights[NUM_SSSS_SAMPLES] =
{
	{ 0.220441f, 0.437000f, 0.635000f },
	{ 0.076356f, 0.064487f, 0.039097f },
	{ 0.116515f, 0.103222f, 0.064912f },
	{ 0.064844f, 0.086388f, 0.062272f },
	{ 0.131798f, 0.151695f, 0.103676f },
	{ 0.025690f, 0.042728f, 0.033003f },
	{ 0.048593f, 0.064740f, 0.046131f },
	{ 0.048092f, 0.003042f, 0.000400f },
	{ 0.048845f, 0.005406f, 0.001222f },
	{ 0.051322f, 0.006034f, 0.001420f },
	{ 0.061428f, 0.009152f, 0.002511f },
	{ 0.030936f, 0.002868f, 0.000652f },
	{ 0.073580f, 0.023239f, 0.009703f }
};

/// Kernel generated using the demo for Separable Subsurface Scattering, J. Jiminez et al.
/// https://users.cg.tuwien.ac.at/zsolnai/wp/wp-content/uploads/2014/12/s4_cpp_source.zip
///
/// xyz = weights, w = offset
static const float4 ssssSepKernel[NUM_SSSS_SAMPLES] =
{
	// center
	float4(0.0580662f, 0.112548f, 0.188667f, 0.0f),

	// right / top side
	float4(0.0717388f, 0.123999f, 0.179005f, 0.0718912f),
	float4(0.0765218f, 0.105977f, 0.112441f, 0.287565f),
	float4(0.0719599f, 0.0798198f, 0.0620495f, 0.647021f),
	float4(0.0638999f, 0.0561362f, 0.0313713f, 1.15026f),
	float4(0.0539951f, 0.0364919f, 0.0137218f, 1.79728f),
	float4(0.0430412f, 0.0213753f, 0.00503899f, 2.58808f),
	float4(0.0322905f, 0.0112044f, 0.00154874f, 3.52267f),
	float4(0.0228101f, 0.00523629f, 0.000390691f, 4.60104f),
	float4(0.0153233f, 0.00219856f, 8.20448e-05f, 5.82319f),
	float4(0.0100178f, 0.000855952f, 1.43897e-05f, 7.18912f),
	float4(0.00674159f, 0.000333281f, 2.31379e-06f, 8.69883f),
	float4(0.00262682f, 9.77844e-05f, 4.20641e-07f, 10.3523f)
};

void AccumulateSamples(in float2 sampleTC, in uint sampleIndex, in float centerDepthVS, in float maxDepthDiff,
					   in float3 ssssKernelWeights, in SsssSrt *pSrt, inout float3 accumSsss, inout float3 weightSum)
{
	float3 sampleSsss = pSrt->m_ssssInput.SampleLevel(pSrt->m_linearClampSampler, sampleTC, 0.0f).rgb;
	float sampleDepthVS = pSrt->m_depthBuffer.SampleLevel(pSrt->m_pointClampSampler, sampleTC, 0.0f).r;
	sampleDepthVS = pSrt->m_screenViewParameter.y / (sampleDepthVS - pSrt->m_screenViewParameter.x);

	if (abs(centerDepthVS - sampleDepthVS) <= maxDepthDiff)
	{
		accumSsss += sampleSsss * ssssKernelWeights;
		weightSum += ssssKernelWeights;
	}
}

void Ssss(in uint2 coord, in SsssSrt *pSrt, in SsssBlurTypes blurType)
{
	uint materialMask = pSrt->m_materialMask.Load(int3(coord, 0));
	bool isSkin = materialMask & MASK_BIT_SKIN;
	if (!isSkin)
	{
		pSrt->m_ssssOutput[coord] = float3(0.0f, 0.0f, 0.0f);
		return;
	}

	uint4 gbuffer2Data = pSrt->m_gBuffer2[coord];
	float ssStrength = UnpackUInt(gbuffer2Data.z).y;
	float ssRadius = UnpackUInt(gbuffer2Data.w).x * kMaxSsRadius;
	ssRadius *= pSrt->m_ssRadiusMultiplier;
	bool hasEyes = gbuffer2Data.z & MASK_BIT_SPECIAL_EYES;
	bool hasTeeth = gbuffer2Data.z & MASK_BIT_SPECIAL_TEETH;

	// normalize by kernel width
	if (blurType == SSSS_NON_SEP_BLUR)
		ssRadius /= abs(ssssNonSepKernelOffsets[11].y);
	else
		ssRadius /= ssssSepKernel[12].w;

	float centerDepthVS = pSrt->m_depthBuffer.Load(int3(coord, 0)).r;
	centerDepthVS = pSrt->m_screenViewParameter.y / (centerDepthVS - pSrt->m_screenViewParameter.x);
	float pixelRadius = ssRadius * pSrt->m_blurRadiusScale / centerDepthVS;

	float2 sampleVec;
	if (blurType == SSSS_NON_SEP_BLUR)
		sampleVec = float2(pixelRadius, pixelRadius);
	else if (blurType == SSSS_SEP_HOR_BLUR)
		sampleVec = float2(pixelRadius, 0.0f);
	else
		sampleVec = float2(0.0f, pixelRadius);

	// random rotation
	uint2 randomRotTC = (coord + pSrt->m_sampleParams.xy) & pSrt->m_sampleParams.z;
	float2 randomRot = pSrt->m_randomRotTexture.Load(int3(randomRotTC, 0)).xy;
	float2x2 randomRotMat;
	if ((blurType == SSSS_NON_SEP_BLUR) || (blurType == SSSS_SEP_HOR_BLUR))
	{
		randomRotMat[0] = float2(randomRot.x, -randomRot.y);
		randomRotMat[1] = float2(randomRot.y, randomRot.x);
	}
	else
	{
		randomRotMat[0] = float2(randomRot.y, randomRot.x);
		randomRotMat[1] = float2(-randomRot.x, randomRot.y);
	} 

	const float maxDepthDiff = ((hasEyes || hasTeeth) ? ssRadius * 0.01f : ssRadius * 0.1f) * pSrt->m_maxDepthDiffMultiplier;

	float2 centerTC = (coord + 0.5f) * pSrt->m_invScreenSize; 
	float3 centerSsss = pSrt->m_ssssInput.Load(int3(coord, 0)).rgb;

	float3 accumSsss;
	float3 weightSum;
	if (blurType == SSSS_NON_SEP_BLUR)
	{
		accumSsss = centerSsss * ssssNonSepKernelWeights[0];
		weightSum = ssssNonSepKernelWeights[0];
	}
	else
	{
		accumSsss = centerSsss * ssssSepKernel[0].rgb;
		weightSum = ssssSepKernel[0].rgb;
	}

	if (blurType == SSSS_NON_SEP_BLUR)
	{
		UNROLL
		for (uint i = 1; i < NUM_SSSS_SAMPLES; i++)
		{
			float2 sampleTC = mul(randomRotMat, sampleVec * ssssNonSepKernelOffsets[i]) + centerTC;
			AccumulateSamples(sampleTC, i, centerDepthVS, maxDepthDiff, ssssNonSepKernelWeights[i], pSrt, accumSsss, weightSum);
		}
	}
	else
	{
		uint startIndex;
		if (blurType == SSSS_SEP_HOR_BLUR)
			startIndex = randomRotTC.x;
		else
			startIndex = ((coord.y + pSrt->m_sampleParams.x) & pSrt->m_sampleParams.z);

		UNROLL
		for (uint i = 1; i < NUM_SSSS_SAMPLES; i += 4)
		{
			uint sampleIndex = startIndex + i;
			float2 sampleTC = mul(randomRotMat, ssssSepKernel[sampleIndex].a * sampleVec);

			// left / bottom side
			AccumulateSamples(centerTC - sampleTC , sampleIndex, centerDepthVS, maxDepthDiff, ssssSepKernel[sampleIndex].rgb, pSrt, accumSsss, weightSum);

			// right / top side
			AccumulateSamples(centerTC + sampleTC, sampleIndex, centerDepthVS, maxDepthDiff, ssssSepKernel[sampleIndex].rgb, pSrt, accumSsss, weightSum);
		}
	}

	accumSsss /= weightSum;

	pSrt->m_ssssOutput[coord] = lerp(centerSsss, accumSsss, ssStrength);
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CS_SsssNonSep(uint3 dispatchId : S_DISPATCH_THREAD_ID, SsssSrt *pSrt : S_SRT_DATA)
{
	Ssss(dispatchId.xy, pSrt, SSSS_NON_SEP_BLUR);
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CS_SsssSepHor(uint3 dispatchId : S_DISPATCH_THREAD_ID, SsssSrt *pSrt : S_SRT_DATA)
{
	Ssss(dispatchId.xy, pSrt, SSSS_SEP_HOR_BLUR);
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CS_SsssSepVert(uint3 dispatchId : S_DISPATCH_THREAD_ID, SsssSrt *pSrt : S_SRT_DATA)
{
	Ssss(dispatchId.xy, pSrt, SSSS_SEP_VERT_BLUR);
}
