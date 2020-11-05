#define GLOBAL static

#define ND_PSSL
#define IS_COMPUTE_SHADER
#define ENABLE_FULL_COMPUTE_SRT

#include "../include/math-util.fxi"
#include "../include/color-util.fxi"
#include "../include/global-definition.fxi"
#include "../include/global-textures.fxi"
#include "../include/runtime-lights-common.fxi"
#include "../include/snoise.fxi"
#include "../physical-shader/globals.fxi"
#include "global-variables.fxi"
#include "structs.fxi"
#include "packing.fxi"
#include "../physical-shader/misc.fxi"
#include "../physical-shader/brdf.fxi"
#include "../physical-shader/area-lights.fxi"
#include "../physical-shader/specular-cubemap.fxi"
#include "global-const-buffers.fxi"
#include "deferred-util.fxi"
#include "atomics.fxi"
#include "reflection-util.fxi"
#include "new-volumetrics.fxi"
#include "clustered-lighting-utils.fxi"

enum DeferredLightingFlags
{
	kCalculateShadow = 0x01, // not used anymore
	kCalculateRuntimeLights = 0x02,
	kDebugMode = 0x04,
	kRefMode = 0x08,
	kDisableSpecular = 0x10,
};

#ifndef DISPATCH_TILE_SIZE
#define DISPATCH_TILE_SIZE TILE_SIZE
#endif

#define NUM_TOTAL_THREADS (DISPATCH_TILE_SIZE * DISPATCH_TILE_SIZE)
#define NUM_WAVEFRONTS (NUM_TOTAL_THREADS / 64)

static uint g_debugPermutationIndex = 0;

// General helper functions
void LoadGBuffer(uint2 coord, ComputeSrt *srt, out BrdfParameters brdfParameters, inout Setup setup, bool isDebug)
{	
	uint4 sample0 = srt->m_gBuffer0.Load(int3(coord, 0));
	uint4 sample1 = srt->m_gBuffer1.Load(int3(coord, 0));
	uint materialMaskSample = srt->m_materialMaskBuffer.Load(int3(coord, 0));

	InitBrdfParameters(brdfParameters);
	UnpackGBuffer(sample0, sample1, brdfParameters, setup);
	UnpackMaterialMask(materialMaskSample, sample1, setup.materialMask);

	ApplyPermutation(setup.materialMask);

	// Don't compute the fuzz brdf if its disabled.
	if (!setup.materialMask.hasFuzz)
	{
		setup.fuzzBlend = 0.0f;
	}

	// Check if we need any extra parameters
	if (materialMaskSample & (MASK_BIT_EMISSIVE | MASK_BIT_SPECULAR_NORMAL | MASK_BIT_HAIR | MASK_BIT_FUR | MASK_BIT_SKIN))	
	{
		uint4 sample2 = srt->m_gBuffer2.Load(int3(coord, 0));
		UnpackExtraGBuffer(sample2, setup.materialMask, brdfParameters, setup);
		// We only want to do this when debug, because these bits should only be applied in the subsurface compute pass
		if(isDebug)
			UnpackEyesSpecialMaterialMask(sample2, setup.materialMask);
	}
}

void SetGlobalParameters(ComputeSrt srt)
{
	g_linearSampler						= srt.m_linearSampler;
	g_linearClampSampler				= srt.m_linearClampSampler;
	g_pointSampler						= srt.m_pointSampler;
	g_vScreenViewParameter				= srt.m_runtimeParams.m_screenViewParameter;
	g_linearClampVSampler				= srt.m_linearClampVSampler;
	g_shadowSampler						= srt.m_shadowSampler;
	g_tLowDiscrepancy					= srt.m_lowDiscrepancyTexture;
	g_areaSunlightScale					= srt.m_areaSunlightScale;

	g_localShadowsData					= srt.m_pLocalShadowTextures;
	g_tMinMaxShadow						= srt.m_minMaxMaps;
	g_lowDiscrepancySampleParams		= srt.m_lowDiscrepancySampleParams;

	g_sunShadowFade						= srt.m_sunShadowFade;
	g_sunlightSpecScale					= srt.m_sunlightSpecScale;
	g_sunlightSpecScaleHair				= srt.m_sunlightSpecScaleHair;
	g_sunlightMinRoughness				= srt.m_sunlightMinRoughness;
	g_graphicsQualityFlags				= srt.m_graphicsQualityFlags;

	// Ambient shadow filter
	g_tFullScreenAmbientShadows			= srt.m_ambientShadowBuffer;
	g_tFullScreenDownSampledDepth		= srt.m_downsampledDepthBuffer;
	g_ambShadowParams					= srt.m_ambShadowParams;

	// Ambient specular
	g_tSpecularIBLBuffer				= srt.m_ambientSpecularBuffer;
	g_tAmbientBRDFLut					= srt.m_ambientBrdfLut;
	g_tReflOcclusionLookup				= srt.m_reflectionOcclusionLookup;

	// BRDF multiple scattering
	g_tSpecBrdfEnergyLut				= srt.m_specBrdfEnergyLut;
	g_enableMultipleScattering			= srt.m_enableMultipleScattering;

	g_iblOcclusionEnable				= srt.m_iblOcclusionEnable;
	g_basis3SpecScale					= srt.m_basis3SpecScale;
	g_ambientCharacterScaleHair			= srt.m_ambientCharacterScaleHair;
	g_ambientCharacterScaleTeeth		= srt.m_ambientCharacterScaleTeeth;
	g_nonshadowOmniLightsFakeOcclusion	= srt.m_nonshadowOmniLightsFakeOcclusion;
	g_nonshadowSpotLightsFakeOcclusion	= srt.m_nonshadowSpotLightsFakeOcclusion;
	g_nonshadowFlashlightFakeOcclusion	= srt.m_nonshadowFlashlightFakeOcclusion;

	g_iblScale							= srt.m_iblScale;
	g_iblScaleCharacter					= srt.m_iblScaleCharacter;
	g_iblScaleForeground				= srt.m_iblScaleForeground;
	g_iblScaleBackground				= srt.m_iblScaleBackground;
	g_ambientTint						= srt.m_ambientTint;
	g_ambientProjectionSpecular			= srt.m_ambientProjectionSpecular;
	g_enableColorizedAo					= srt.m_enableColorizedAo;
	g_genericRenderSettingDebug			= srt.m_genericRenderSettingDebug;

	g_eyeLightWrapBias					= srt.m_eyeLightWrapBias;
	g_eyeScreenSpaceShadowStrength		= srt.m_eyeScreenSpaceShadowStrength;
	g_eyeTeethScreenSpaceShadowForNonShadowLight = srt.m_eyeTeethScreenSpaceShadowForNonShadowLight;
	g_eyeTeethRuntimeLightUseSsaoStrength = srt.m_eyeTeethRuntimeLightUseSsaoStrength;
	g_eyeTeethScreenSpaceShadowMinDepthMult = srt.m_eyeTeethScreenSpaceShadowMinDepthMult;
	g_teethScreenSpaceShadowStrength	= srt.m_teethScreenSpaceShadowStrength;
	g_teethAmbientLightingMultiplier	= srt.m_teethAmbientLightingMultiplier;
	g_teethLightingMultiplier			= srt.m_teethLightingMultiplier;
	g_eyeAmbientLightingMultiplier		= srt.m_eyeAmbientLightingMultiplier;
	g_eyeLightingMultiplier				= srt.m_eyeLightingMultiplier;

	// IMPORTANT NOTE: g_altWorldOrigin is current the camera position
	// however, if you change it you MUST pass in the variable through the SRT instead
	// Also, don't forget to change ssss.fx
	g_altWorldOrigin					= srt.m_runtimeParams.m_cameraWS.xyz;

	// Skin 
	g_vScreenSizeParam					= float4(srt.m_runtimeParams.m_screenSize.zw, -srt.m_runtimeParams.m_screenSize.z, srt.m_runtimeParams.m_screenSize.w);
	g_mP								= srt.m_projMat;
	g_mWorldToScreen					= srt.m_worldToScreenMat;
	g_mScreenToAltWorld					= srt.m_screenToAltWorldMat;
	g_tFullScreenSubSurfaceMap			= srt.m_subsurfaceTexture;
	g_tFullScreenBounceLighting			= srt.m_bounceLightBuffer;
	g_tCurrentDepth						= srt.m_depthBuffer;
	g_tOpaquePlusAlphaDepth				= srt.m_opaqueAlphaDepthBuffer;

	// Temp for screen-space shadows test
	g_tGBuffer1							= srt.m_gBuffer1;
	g_tGBuffer2							= srt.m_gBuffer2;
	g_debugParams.z						= srt.m_debug.m_gbufferComponentVis;
	g_tRuntimeLightSss					= srt.m_runtimeLightSssBuffer;

	g_healthEffectControlEnemy			= srt.m_healthEffectControlEnemy;
	
	g_tNewVolumetricAmbientLight		= srt.m_newVolumetricsAmbientLight;

	g_enableCloudShadowCaustics			= srt.m_enableCloudShadowCaustics;
	g_cloudShadowCausticsWaterHeight	= srt.m_cloudShadowCausticsWaterHeight;
	g_cloudShadowCausticsAmbientIntensity = srt.m_cloudShadowCausticsAmbientIntensity;
	
	g_time									= srt.m_renderTime;
}

#if DISPATCH_TILE_SIZE > 8
groupshared uint s_orMaterialMask[NUM_WAVEFRONTS];
groupshared uint s_andMaterialMask[NUM_WAVEFRONTS];
#endif

void ReduceMaterialMask(uint materialMask, uint threadGroupIndex, out uint orReducedMask, out uint andReducedMask)
{
	// Step: Get 16
	uint reduce = LaneSwizzle(materialMask, 0x1Fu, 0x00u, 0x10u);
	orReducedMask = reduce | materialMask;
	andReducedMask = reduce & materialMask;
	// Step 2: 8th lane	
	orReducedMask |= LaneSwizzle(orReducedMask, 0x1Fu, 0x00u, 0x8u);
	andReducedMask &= LaneSwizzle(andReducedMask, 0x1Fu, 0x00u, 0x8u);
	// Step 3: 4th lane	
	orReducedMask |= LaneSwizzle(orReducedMask, 0x1Fu, 0x00u, 0x4u);
	andReducedMask &= LaneSwizzle(andReducedMask, 0x1Fu, 0x00u, 0x4u);
	// Step 4: 2nd lane	
	orReducedMask |= LaneSwizzle(orReducedMask, 0x1Fu, 0x00u, 0x2u);
	andReducedMask &= LaneSwizzle(andReducedMask, 0x1Fu, 0x00u, 0x2u);
	// Step 5: first lane
	orReducedMask |= LaneSwizzle(orReducedMask, 0x1Fu, 0x00u, 0x1u);
	andReducedMask &= LaneSwizzle(andReducedMask, 0x1Fu, 0x00u, 0x1u);
	// Finally, get the reduction of the other group of 32 registers
	orReducedMask = ReadFirstLane(orReducedMask) | ReadLane(orReducedMask, 32);
	andReducedMask = ReadFirstLane(andReducedMask) & ReadLane(andReducedMask, 32);

#if DISPATCH_TILE_SIZE >= 16
	uint waveFront = threadGroupIndex / 64;
	if (threadGroupIndex % 64 == 0)
	{
		s_orMaterialMask[waveFront] = orReducedMask;
		s_andMaterialMask[waveFront] = andReducedMask;
	}
	ThreadGroupMemoryBarrierSync();

	for (uint s = NUM_WAVEFRONTS/2; s > 0; s /= 2)
	{
		if (waveFront < s)
		{
			s_orMaterialMask[waveFront] |= s_orMaterialMask[waveFront + s];
			s_andMaterialMask[waveFront] &= s_andMaterialMask[waveFront + s];
		}
		ThreadGroupMemoryBarrierSync();
	}

	orReducedMask = ReadFirstLane(s_orMaterialMask[0]);
	andReducedMask = ReadFirstLane(s_andMaterialMask[0]);
#endif
}

#if DISPATCH_TILE_SIZE > 8
groupshared uint s_tileConstantValue[NUM_WAVEFRONTS];
#endif

bool IsTileConstantValue(bool wfIsConstant, uint threadGroupIndex)
{
#if DISPATCH_TILE_SIZE > 8
	uint waveFront = threadGroupIndex / 64;
	if (threadGroupIndex % 64 == 0)
		s_tileConstantValue[waveFront] = wfIsConstant;
	ThreadGroupMemoryBarrierSync();

	for (uint s = NUM_WAVEFRONTS/2; s > 0; s /= 2)
	{
		if (waveFront < s)
		{
			s_tileConstantValue[waveFront] &= s_tileConstantValue[waveFront + s];
		}
		ThreadGroupMemoryBarrierSync();
	}

	return ReadFirstLane(s_tileConstantValue[0]);
#else
	return wfIsConstant;
#endif
}

float GetHeightmapDepthDeltaDebug(const BrdfParameters brdfParameters, float displayMagnitude)
{
	return saturate((brdfParameters.heightmap * kHeightmapScale / displayMagnitude) * 0.5 + 0.5);
}

float3 ComputeHeightmapBloodPoolDebug(const BrdfParameters brdfParameters)
{
	float heightInCm = brdfParameters.heightmap;
	if (heightInCm < -1.0f)
	{
		return float3(0.0f, 0.0f, 0.0f);
	}
	else if ((heightInCm >= -1.0f) && (heightInCm < -0.5f))
	{
		float lerpValue = (heightInCm + 1.0f) / 0.5f;
		return lerp(float3(0.0f, 0.0f, 0.0f), float3(0.5f, 0.0f, 0.0f), lerpValue);
	}
	else if ((heightInCm >= -0.5f) && (heightInCm < 0.0f))
	{
		float lerpValue = (heightInCm + 0.5f) / 0.5f;
		return lerp(float3(0.5f, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), lerpValue);
	}
	else if ((heightInCm >= 0.0f) && (heightInCm < 0.5f))
	{
		float lerpValue = heightInCm / 0.5f;
		return lerp(float3(0.0f, 0.0f, 1.0f), float3(0.0f, 1.0f, 0.0f), lerpValue);
	}
	else if ((heightInCm >= 0.5f) && (heightInCm < 1.5f))
	{
		float lerpValue = heightInCm - 0.5f;
		return lerp(float3(0.0f, 1.0f, 0.0f), float3(1.0f, 1.0f, 1.0f), lerpValue);
	}
	else
	{
		return float3(1.0f, 0.0f, 1.0f);
	}
}

float3 ApplyDebug(float3 totalLight, BrdfParameters brdfParameters, Setup setup, ComputeSrt srt, uint2 tile, uint lightingMode)
{
	float3 finalColor = totalLight;

	if (srt.m_debug.m_gbufferComponentVis > 0)
	{
		float3 ambientBase = srt.m_ambientBaseBuffer.Load(int3(setup.screenPosition.xy, 0));
		float3 ambientDirectionalColor = srt.m_ambientDirectionalBuffer.Load(int3(setup.screenPosition.xy, 0));
		finalColor = dot(setup.normalWS, setup.viewWS) * 0.5 + 0.5;
		// Ordered by appearance in Gbuffer
		switch (srt.m_debug.m_gbufferComponentVis)
		{
			case  1: finalColor = brdfParameters.baseColor;										break;
			case  2: finalColor = pow(brdfParameters.specular, 2.2);							break;
			case  3: finalColor = pow(setup.normalWS*0.5+0.5,2.2);								break;
			case  4: finalColor = pow(brdfParameters.roughness, 2.2);							break;
			case  5: finalColor = pow(brdfParameters.translucency, 2.2);						break;
			case  8: finalColor = pow(brdfParameters.metallic, 2.2);							break;
			case  9: finalColor = pow(brdfParameters.ao, 2.2);									break;
			case 10: finalColor = pow(setup.dominantDirection*0.5+0.5,2.2);						break;
			case 11: finalColor = pow(setup.eyeDiffNormalWS*0.5+0.5,2.2);						break;
			case 13: finalColor = pow(setup.specularNormalWS*0.5+0.5,2.2);						break;
			case 19: finalColor = pow(brdfParameters.ssStrength, 2.2);							break;
			case 20: finalColor = pow(brdfParameters.ssRadius / kMaxSsRadius, 2.2);				break;
			case 22: finalColor = pow(setup.bitangentWS*0.5+0.5,2.2);							break;
			case 23: finalColor = pow(brdfParameters.secondarySpecular, 2.2);					break;
			case 24: finalColor = pow(brdfParameters.secondaryRoughness, 2.2);					break;
			case 27: finalColor = pow(brdfParameters.iblOcclusion, 2.2);						break;
			case 28: finalColor = ambientBase;													break;
			case 29: finalColor = ambientDirectionalColor;										break;
			case 30: finalColor = pow(setup.fuzzBlend, 2.2);									break;
			case 32: finalColor = brdfParameters.emissive;										break;
			case 34: finalColor = pow(brdfParameters.extraSunShadow, 2.2);						break;
			case 35: finalColor = pow(GetHeightmapDepthDeltaDebug(brdfParameters, 0.2), 2.2);	break;
			case 36: finalColor = pow(GetHeightmapDepthDeltaDebug(brdfParameters, 0.1), 2.2);	break;
			case 37: finalColor = pow(GetHeightmapDepthDeltaDebug(brdfParameters, 0.05), 2.2);	break;
			case 38: finalColor = pow(ComputeHeightmapBloodPoolDebug(brdfParameters), 2.2);		break;
			default: finalColor = 0;															break;
		}
	}
	else if (srt.m_debug.m_materialMaskVis > 0)
	{
		bool val = false;

		finalColor = dot(setup.normalWS, setup.viewWS) * 0.5 + 0.5;

		switch (srt.m_debug.m_materialMaskVis)
		{
			case  3: val = setup.materialMask.hasVolProbeLighting;	break;
			case  4: val = setup.materialMask.hasTranslucency;		break;
			case  5: val = setup.materialMask.hasFuzz;				break;
			case  6: val = setup.materialMask.hasMetallic;			break;
			case  7: val = setup.materialMask.hasSpecularNormal;	break;
			case  8: val = setup.materialMask.hasSkin;				break;
			case  9: val = setup.materialMask.hasEmissive;			break;
			case 10: val = setup.materialMask.hasHair;				break;
			case 11: val = setup.materialMask.hasFur;				break;
			case 12: val = !setup.materialMask.notReceiveAmbShadow;	break;
			case 13: val = setup.materialMask.isCharacter;			break;
			case 15: val = setup.materialMask.hasEyes;				break;
			case 16: val = setup.materialMask.hasTeeth;				break;
			case 17: val = setup.stencilBits & 0x01;				break;	// Stencil Deferred
			case 18: val = setup.stencilBits & 0x02;				break;	// Stencil Water
			case 19: val = setup.stencilBits & 0x08;				break;	// Stencil Ghosting ID 0
			case 20: val = setup.stencilBits & 0x10;				break;	// Stencil Ghosting ID 1
			case 21: val = setup.stencilBits & 0x20;				break;	// Stencil FG
			case 22: val = setup.stencilBits & 0x40;				break;	// Stencil Hero
			case 23: val = setup.stencilBits & 0x80;				break;	// Stencil Enemy
		}

		finalColor *= val ? float3(0,1,0) : float3(1,0,0);
	}

	if (lightingMode == kLightingModeTileZBinning && (srt.m_debug.m_clusteredLightsVisMode || srt.m_debug.m_lightsPerTileVis))
	{
		int cluster = GetZBinIndex(srt.m_clusteredSettings, setup.depthVS);
		float shading = abs(dot(setup.normalWS, setup.viewWS)) * 0.2 + 0.8;

		uint numLights = 0;
		if (srt.m_debug.m_clusteredLightsVisMode == kClusteredLightsVisModeTile)
		{
			numLights = GetNumLightsInZBinTile(srt.m_clusteredBins, tile);
		}
		else if (srt.m_debug.m_clusteredLightsVisMode >= kClusteredLightsVisModeZBinOrCluster)
		{
			numLights = GetNumLightsInZBin(srt.m_clusteredBins, cluster);
		}
		else
		{
			numLights = GetNumLightsInZBinCluster(srt.m_clusteredBins, uint3(tile, cluster));
		}

		if (numLights > 0)
		{
			finalColor = GetHeatmapColor(numLights * 0.5f) * shading;
		}
		else
		{
			finalColor = float3(0.0f, 0.0f, 0.5f) * shading;
		}
	}
	else if (lightingMode == kLightingModeTileClusteredLightLists && (srt.m_debug.m_clusteredLightsVisMode || srt.m_debug.m_lightsPerTileVis))
	{
		int cluster = GetClusterIndex(srt.m_clusteredSettings, setup.depthVS);
		float shading = abs(dot(setup.normalWS, setup.viewWS)) * 0.2 + 0.8;

		uint numLights = 0;

		if (srt.m_debug.m_clusteredLightsVisMode == kClusteredLightsVisModeTile)
		{
			
		}
		else if (srt.m_debug.m_clusteredLightsVisMode >= kClusteredLightsVisModeZBinOrCluster)
		{
			cluster = srt.m_debug.m_clusteredLightsVisMode - kClusteredLightsVisModeZBinOrCluster;
			numLights = srt.m_clusteredBins.m_lightCounts[uint3(tile, cluster)];
		}
		else
		{
			
		}

		if (numLights > 0)
		{
			finalColor = GetHeatmapColor(numLights * 0.5f) * shading;
			if (numLights == MAX_LIGHTS_PER_TILE)
			{
				uint2 screenLocation = uint2(setup.screenPosition.xy) / 2 & 1;
				finalColor = screenLocation.x ^ screenLocation.y;
			}
		}
		else
		{
			finalColor = float3(0.0f, 0.0f, 0.5f) * shading;
		}
	}
	else if (srt.m_debug.m_lightsPerTileVis)
	{
		const uint tileIndex = tile.x + mul24(tile.y, uint(srt.m_runtimeParams.m_screenSize.x)) / TILE_SIZE;
		const uint numTileVisibleLights = srt.m_lights.m_numLightsPerTileBufferRO[tileIndex];

		float shading = abs(dot(setup.normalWS, setup.viewWS)) * 0.2 + 0.8;
		if (numTileVisibleLights > 0)
		{
			finalColor = GetHeatmapColor(numTileVisibleLights*0.5) * shading;
			if (numTileVisibleLights == MAX_LIGHTS_PER_TILE)
			{
				uint2 screenLocation = uint2(setup.screenPosition.xy) / 2 & 1;
				finalColor = screenLocation.x ^ screenLocation.y;
			}
		}
		else
		{
			finalColor = float3(0, 0, 0.5) * shading;
		}
	}

	if (srt.m_debug.m_permutationVis)
	{
		finalColor = abs(dot(setup.normalWS, setup.viewWS)) * 0.125 + 0.875;
		float3 tint = srt.m_debug.m_permutationColors[g_debugPermutationIndex % kNumPermutations];
		finalColor *= pow(tint, 2.2);
	}

	if (srt.m_debug.m_basicDebugLighting)
	{
		finalColor = brdfParameters.baseColor * (dot(setup.normalWS, setup.viewWS) * 0.5 + 0.5);
	}

	return finalColor;
}

float3 GetPositionWS(uint2 screenCoord, float depthZ, ComputeSrt *srt)
{
	// Go from screen coordinates to NDC
	float2 ndc = ((float2)screenCoord + float2(0.5f, 0.5f)) * srt->m_runtimeParams.m_screenSize.zw;
	ndc.x = ndc.x * 2.f - 1.f;
	ndc.y = (1 - ndc.y) * 2.f - 1.f;

	float4 positionAS = mul(float4(ndc, depthZ, 1.f), srt->m_screenToAltWorldMat);
	positionAS.xyz /= positionAS.w;
	return positionAS.xyz + srt->m_runtimeParams.m_cameraWS.xyz;
}

float3 ComputeSkinSSS(uint2 screenCoord, const Setup setup, const BrdfParameters brdfParameters, ComputeSrt srt, bool isDebug)
{
	float3 diffuse = 0.0;
	diffuse = NdFetchFullScreenSubSurface(setup.screenUv).rgb;
	diffuse *= brdfParameters.baseColor * (1.0 - setup.fuzzBlend);

	return diffuse;
}

float3 GetRuntimeLightsAccumulation(ComputeSrt srt, BrdfParameters brdfParameters, Setup setup, uint2 tile, uint lightingMode, uint flags)
{	
	bool isDebug = (flags & kDebugMode) != 0;
	uint lightDescTypeDataMask = (flags & kDisableSpecular) ? ~LIGHT_EMIT_SPECULAR_BITS : ~0;

	float3 totalLight = 0;
	
	if (lightingMode == kLightingModeTileZBinning)
	{
		const uint cluster = GetZBinIndex(srt.m_clusteredSettings, setup.depthVS);
		uint2 zBin = srt.m_clusteredBins.m_zBins[cluster];
		if (zBin.x <= zBin.y)
		{
			uint offset = GetTileBitsOffset(srt.m_clusteredBins, tile);
			uint2 range = GetZBinTileBlockRange(offset, zBin);

			for (uint i = range.x; i <= range.y; i++)
			{
				uint block = GetZBinTileBlock(srt.m_clusteredBins, offset, zBin, i);

				#pragma loop(licm:never)
				while (block)
				{
					uint bit = FirstSetBit_Lo(block);
					block ^= 1 << bit;

					uint lightIndex = i * kClusteredLightingZBinTileBlockSizeInBits + bit;

					LightDesc lightDesc = srt.m_lights.m_lights[lightIndex];
					lightDesc.m_typeData &= lightDescTypeDataMask;

					uint goboType = (lightDesc.m_typeData & LIGHT_GOBO_TYPE_BITS) >> 9;

					if (goboType == kCausticsGobo && dot(float4(setup.positionWS, 1.f), srt.m_reflectionPlane) <= 0.0f)
					{
						continue;
					}

					totalLight += GetRuntimeLightAccumulation(srt.m_debug, lightDesc, brdfParameters, setup, isDebug, false);
				}
			}
		}
	}
	else if (lightingMode == kLightingModeTileClusteredLightLists)
	{

	}
	else if (lightingMode == kLightingModeTileLightLists)
	{
		// TODO: Save the multiplier in the srt
		const uint tileIndex = tile.x + mul24(tile.y, uint(srt.m_runtimeParams.m_screenSize.x)) / TILE_SIZE;
		const uint numLights = srt.m_lights.m_numLightsPerTileBufferRO[tileIndex];

		#pragma loop(licm:never)
		for (uint i = 0; i < numLights; ++i)
		{
			const uint lightIndex = srt.m_lights.m_lightsPerTileBufferRO[tileIndex*MAX_LIGHTS_PER_TILE + i];

			LightDesc lightDesc = srt.m_lights.m_lights[lightIndex];
			lightDesc.m_typeData &= lightDescTypeDataMask;

			uint goboType = (lightDesc.m_typeData & LIGHT_GOBO_TYPE_BITS) >> 9;

			if (goboType == kCausticsGobo && dot(float4(setup.positionWS, 1.f), srt.m_reflectionPlane) <= 0.0f)
			{
				continue;
			}

			totalLight += GetRuntimeLightAccumulation(srt.m_debug, lightDesc, brdfParameters, setup, isDebug, false);
		}
	}
	
	return totalLight;
}

void ComputeSunShadow(inout Setup setup, const BrdfParameters brdfParameters, ComputeSrt srt, bool isDebug)
{
	if(brdfParameters.extraSunShadow == 0)
	{
		setup.sunShadow = 0;
		return;
	}
	float sunShadowDynamic = max(srt.m_sunShadowTexture[int2(setup.screenPosition.xy)] - 1.0f / 255.0f, 0.0f) * 255.0f / 254.0f;
	float2 cloudShadowSample = srt.m_cloudShadowTexture.SampleLevel(srt.m_pointSampler, setup.screenUv, 0).rb;
	float cloudShadow = cloudShadowSample.x;
	float causticsSample = cloudShadowSample.y;
	
	setup.sunShadow = CalculateSunShadow(setup, brdfParameters, cloudShadow, sunShadowDynamic, brdfParameters.extraSunShadow, kPassDeferred);
	setup.sunShadow *= CalculateSunShadowCaustics(setup, causticsSample);
}

float3 GetNormalDerivative(uint2 screenCoord, float3 positionWS)
{
	float3 dx, dy;
	dx.x = LaneSwizzle(positionWS.x, 0x1f, 0x00, 0x01) - positionWS.x;
	dx.y = LaneSwizzle(positionWS.y, 0x1f, 0x00, 0x01) - positionWS.y;
	dx.z = LaneSwizzle(positionWS.z, 0x1f, 0x00, 0x01) - positionWS.z;
	dy.x = LaneSwizzle(positionWS.x, 0x1f, 0x00, 0x10) - positionWS.x;
	dy.y = LaneSwizzle(positionWS.y, 0x1f, 0x00, 0x10) - positionWS.y;
	dy.z = LaneSwizzle(positionWS.z, 0x1f, 0x00, 0x10) - positionWS.z;
	
	float3 normalDeriv = normalize(cross(dy, dx));
	
	bool bIsNegative = (screenCoord.x % 2) ^ (screenCoord.y % 2);	
	normalDeriv = bIsNegative ? -normalDeriv : normalDeriv;	
	return normalDeriv;
}

struct BonusRenderModeState
{
	float m_highlight;
};

#ifdef BONUS_DEFERRED_MODE

void BonusFlattenNormals(ComputeSrt *srt, uint2 screenCoord, inout BrdfParameters brdfParameters, inout Setup setup)
{
	if (g_bonusDeferredMode == kBonusDeferredMode8Bit)
	{
		uint2 samplePos = screenCoord + int2(-1, 0);
		float depth1 = srt->m_depthBuffer.Load(int3(samplePos, 0)).x;
		float3 positionWS1 = GetPositionWS(uint2(samplePos), depth1, srt);
		samplePos = screenCoord + int2(0, -1);
		float depth2 = srt->m_depthBuffer.Load(int3(samplePos, 0)).x;
		float3 positionWS2 = GetPositionWS(uint2(samplePos), depth2, srt);

		float3 vec1 = normalize(setup.positionWS - positionWS1);
		float3 vec2 = normalize(setup.positionWS - positionWS2);
		setup.normalWS = cross(vec2, vec1);
	}
}

float BonusCalculateHighlight(ComputeSrt *srt, uint2 screenCoord, inout BrdfParameters brdfParameters, inout Setup setup, float ao)
{
	float highlight = 0;
	if (g_bonusDeferredMode == kBonusDeferredModeCellShaded ||
		g_bonusDeferredMode == kBonusDeferredModeRainbow ||
		g_bonusDeferredMode == kBonusDeferredModeDungeon ||
		g_bonusDeferredMode == kBonusDeferredModePopPoster ||
		g_bonusDeferredMode == kBonusDeferredModeAnimatedNoir)
	{
		float highlightsDepth = setup.distanceToCamera;
		
		uint2 highlightSampleCoord = screenCoord;
		
		float neoScale = 1920.0 * g_vScreenSizeParam.x;
		
		// Wobbly edges
		if(g_bonusDeferredMode == kBonusDeferredModeCellShaded ||
		   g_bonusDeferredMode == kBonusDeferredModeRainbow)
		{
			float wobbleFrequency = 30.0;
			float wobbleMagnitude = 1.0;
			
			float2 screenUv = screenCoord * g_vScreenSizeParam.xw / g_vBufferScaleInfo.xy;
			
			float2 highlightSampleOffset = snoise(wobbleFrequency * screenUv + g_time) * wobbleMagnitude;

			if(g_bonusDeferredMode == kBonusDeferredModeRainbow)
				highlightSampleOffset = highlightSampleOffset * highlightSampleOffset * highlightSampleOffset * 4.0;
			
			highlightSampleCoord += highlightSampleOffset;
			
			float highlightDepthZ = srt->m_depthBuffer.Load(int3(highlightSampleCoord, 0)).x;
			float3 highlightPositionWS = GetPositionWS(uint2(highlightSampleCoord), highlightDepthZ, srt);
			float3 highlightViewWS = srt->m_runtimeParams.m_cameraWS.xyz - highlightPositionWS;
			highlightsDepth = length(highlightViewWS);
		}

		float maxDepth = 1000.0f;
		float minDepth = 0.02f * setup.distanceToCamera;
		float intensity = 80.0f * Pow2(dot(setup.normalWS, setup.viewWS) * 0.5f + 0.5f) / setup.distanceToCamera;
		float sampleDist = 6.0f * (1.0f + (1.0f - ao) - brdfParameters.translucency * 2.0f) / pow(setup.distanceToCamera + 1.0f, 0.25f);
		
		if (setup.materialMask.isCharacter)
		{
			sampleDist *= 0.4f;
			minDepth = 0.01f; 
		}
		
		if(g_bonusDeferredMode == kBonusDeferredModeRainbow)
		{
			if(brdfParameters.translucency > 0 && !setup.materialMask.hasHair)
				sampleDist *= 0.001;
				
			sampleDist *= 1.5;
		}
		
		sampleDist /= neoScale;

		const float2 highlightOffsets[8] =
		{
			normalize(float2(-1.0f, -1.0f)),
			normalize(float2(-1.0f,  1.0f)),
			normalize(float2( 1.0f, -1.0f)),
			normalize(float2( 1.0f,  1.0f)),
			          float2( 1.0f,  0.0f),
			          float2(-1.0f,  0.0f),
			          float2( 0.0f,  1.0f),
			          float2( 0.0f, -1.0f),
		};

		float delta = 0.0f;
		for (uint i = 0; i < 8; ++i)
		{
			float2 samplePos = highlightSampleCoord + highlightOffsets[i] * sampleDist;

			// Kind of sloppy
			float highlightDepthZ = srt->m_depthBuffer.Load(int3(samplePos, 0)).x;
			float3 highlightPositionWS = GetPositionWS(uint2(samplePos), highlightDepthZ, srt);
			float3 highlightViewWS = srt->m_runtimeParams.m_cameraWS.xyz - highlightPositionWS;
			float highlightsSample = length(highlightViewWS);
			float sampleDelta = highlightsSample - highlightsDepth;
			if(g_bonusDeferredMode == kBonusDeferredModeCellShaded ||
			   g_bonusDeferredMode == kBonusDeferredModeRainbow)
			{
				sampleDelta = abs(sampleDelta);
			}
				
			if(g_bonusDeferredMode == kBonusDeferredModeRainbow)
				delta += sampleDelta;
			else
				delta = max(sampleDelta, delta);
		}
		delta = clamp(delta - minDepth, 0.0f, maxDepth);
		highlight = delta * intensity;
	}
	
	return highlight;
}

#endif

BonusRenderModeState BonusRenderModeSetup(ComputeSrt *srt, uint2 screenCoord, inout BrdfParameters brdfParameters, inout Setup setup)
{
	BonusRenderModeState state;
	state.m_highlight = 1.0f;

#ifdef BONUS_DEFERRED_MODE
	float ao = min(brdfParameters.ao, srt->m_ssaoBuffer.SampleLevel(srt->m_linearClampSampler, setup.screenUv, 0).a);

	BonusFlattenNormals(srt, screenCoord, brdfParameters, setup);
	state.m_highlight = BonusCalculateHighlight(srt, screenCoord, brdfParameters, setup, ao);

	switch (g_bonusDeferredMode)
	{
	case kBonusDeferredModeCellShaded:
		{
			float3 newColor = sqrt(brdfParameters.baseColor);
			newColor = RGBtoHSL(newColor); 

			if (setup.materialMask.isCharacter)
			{
				newColor.g = saturate(newColor.g * 1.4f);
				newColor.b = saturate(newColor.b * 1.1f);
			}
			else
			{
				newColor.r = floor(newColor.r * 16.0f + 0.5f) / 16.0f;
				newColor.g = floor(newColor.g * 8.0f + 0.5f) / 8.0f;
				newColor.b = floor(newColor.b * 8.0f + 0.5f) / 8.0f; 

				float2 N = EncodeNormal(setup.normalWS);
				N = floor(N * 8.0f + 0.5f) / 8.0f;
				setup.normalWS = DecodeNormal(N);
				
				newColor.g = saturate(newColor.g * 0.5f);
				newColor.b = saturate(newColor.b * 0.9f);
			}

			newColor = HSLtoRGB(newColor);

			brdfParameters.baseColor = newColor * newColor;
			brdfParameters.ao = floor(brdfParameters.ao * 8.0f + 0.5f) / 8.0f;
			brdfParameters.metallic = 0;
			setup.materialMask.hasFuzz = false;
			setup.materialMask.hasSkin = false;
		}
		break;
	case kBonusDeferredModeAfterlife:
		{
			if (setup.materialMask.isCharacter)
			{
				brdfParameters.baseColor.r = brdfParameters.baseColor.g * 1.7f;
				brdfParameters.baseColor = dot(brdfParameters.baseColor, kLuminanceFactor);
				if (setup.materialMask.hasSkin)
				{
					brdfParameters.baseColor = lerp(float3(0.0f, 0.0f, 0.02f), 1.0f, brdfParameters.baseColor.r);
					brdfParameters.baseColor *= lerp(float3(0.0f, 0.0f, 0.2f), 1.0f, ao*ao);
					brdfParameters.roughness *= 0.8f;
					brdfParameters.ssStrength = 2.0f;
					brdfParameters.baseColor *= Pow8(ao) * 2.0f;
				}
				else if (setup.materialMask.hasFuzz)
				{
					brdfParameters.baseColor *= 0.2f;
				}
				else if (setup.materialMask.hasHair)
				{
					brdfParameters.baseColor *= 0.8f;
				}
				else
				{
					brdfParameters.baseColor *= 0.6f;
				}
			}
			else
			{
				float3 desatColor = dot(brdfParameters.baseColor, kLuminanceFactor);
				brdfParameters.baseColor = lerp(brdfParameters.baseColor, desatColor, 0.5f) * 0.7f;

				float slope = saturate((setup.normalWS.y * 0.5 + 0.5) * 1.5);
				
				brdfParameters.baseColor = lerp(brdfParameters.baseColor, float3(0.03f), slope);
				brdfParameters.roughness = lerp(brdfParameters.roughness, 0.9f, slope);
				if (slope > 0.1)
				{
					setup.materialMask.hasFuzz = true;
				}

				brdfParameters.baseColor = lerp(brdfParameters.baseColor, 0.01f, saturate(brdfParameters.translucency * 2.0f));
			}
		}
		break;
	case kBonusDeferredMode8Bit:
		{
			float3 newColor = sqrt(brdfParameters.baseColor);
			newColor = RGBtoHSL(newColor);

			newColor.r = floor(newColor.r * 16.0f + 0.5f) / 16.0f;
			newColor.g = floor(newColor.g * 8.0f + 0.5f) / 8.0f;
			newColor.b = floor(newColor.b * 8.0f + 0.5f) / 8.0f;

			if (setup.materialMask.isCharacter)
			{
				newColor.g = saturate(newColor.g * 1.2f);
			}
			else
			{
				newColor.g = saturate(newColor.g * 0.8f);
			}

			newColor = HSLtoRGB(newColor);
			brdfParameters.baseColor = newColor * newColor;

			float2 N = EncodeNormal(setup.normalWS);
			N = floor(N * 8.0f + 0.5f) / 8.0f;
			setup.normalWS = DecodeNormal(N);

			brdfParameters.ao = floor(brdfParameters.ao * 8.0f + 0.5f) / 8.0f;
			brdfParameters.metallic = 0;
		}
		break;
	case kBonusDeferredModeWatercolor:
		{
			state.m_highlight = (setup.materialMask.hasFuzz && setup.materialMask.isCharacter) ? 1.0f : 0.0f;
			setup.materialMask.hasFuzz = false;
			if (setup.materialMask.hasSkin)
				brdfParameters.baseColor.r = pow(brdfParameters.baseColor.r, 0.9f);
			setup.materialMask.hasSkin = false;
		}
		break;
	case kBonusDeferredModeDungeon:
		{
			if (setup.materialMask.isCharacter)
			{
				float3 upVec = float3(0, 1, 0);
				upVec = cross(-setup.viewWS, upVec);
				float3 leftVec = upVec;
				upVec = cross(upVec, -setup.viewWS);
				float3 lightVec = normalize(upVec + setup.viewWS * 0.7f + leftVec * 1.5f);
				
				setup.sunDirWS = lightVec;
				setup.dominantDirection = lightVec;
				
				if (!setup.materialMask.hasSkin)
					brdfParameters.baseColor = lerp(Luminance(brdfParameters.baseColor), brdfParameters.baseColor, 1.5f);
					
			}
			setup.materialMask.hasSkin = false;
		}
		break;
	case kBonusDeferredModeVoid:
		{
			bool isEnemyPixel = ((setup.stencilBits & 0xC0) == 0x80) ? true : false;
			bool isHeroPixel = ((setup.stencilBits & 0xC0) == 0x40) ? true : false;
			if (!setup.materialMask.isCharacter && !isHeroPixel && !setup.isFg)
			{
				brdfParameters.baseColor = float3(0.6f);
				brdfParameters.metallic = 0.0f;
				brdfParameters.roughness = 0.9f;
				setup.materialMask.hasFuzz = false;
				setup.materialMask.hasSkin = false;
				setup.materialMask.hasHair = false;
				setup.materialMask.hasFur = false;
			}
			else if (!setup.materialMask.isCharacter && !isHeroPixel && setup.isFg)
			{
				brdfParameters.baseColor = float3(1.0f);
				brdfParameters.metallic = 0.0f;
				brdfParameters.roughness = 0.3f;
				brdfParameters.specular = 1.0f;
				setup.materialMask.hasFuzz = false;
				setup.materialMask.hasSkin = false;
				setup.materialMask.hasHair = false;
				setup.materialMask.hasFur = false;
			}
			else
			{
				if (isEnemyPixel)
				{
					brdfParameters.baseColor = float3(0.6f, 0.0f, 0.0f);
					brdfParameters.metallic = 0.0f;
					brdfParameters.roughness = 0.3f;
					brdfParameters.specular = 1.0f;
					setup.materialMask.hasFuzz = false;
					setup.materialMask.hasSkin = false;
					setup.materialMask.hasHair = false;
					setup.materialMask.hasFur = false;
				}
				else if (!isHeroPixel)
				{
					brdfParameters.baseColor = float3(0.8f);
					brdfParameters.metallic = 0.0f;
					brdfParameters.roughness = 0.3f;
					brdfParameters.specular = 1.0f;
					setup.materialMask.hasFuzz = false;
					setup.materialMask.hasSkin = false;
					setup.materialMask.hasHair = false;
					setup.materialMask.hasFur = false;
				}
			}

			float3 lightColorTint = float3(0.85f, 0.95f, 1.0f);
			setup.ambientBaseColor = float3(Luminance(setup.ambientBaseColor) * lightColorTint);
			setup.ambientDirectionalColor = float3(Luminance(setup.ambientDirectionalColor) * lightColorTint);
			setup.sunColor = float3(Luminance(setup.sunColor) * lightColorTint);
		}
		break;
	case kBonusDeferredModeAnimatedNoir:
		{
			setup.ambientDirectionalColor += setup.ambientBaseColor;
			
			if (!setup.materialMask.isCharacter && !setup.isFg)
			{			
				setup.ambientBaseColor = float3(0.0f);
			}
			else
			{
				setup.materialMask.hasFuzz = false;
				if (!setup.materialMask.hasSkin)
					brdfParameters.roughness = 0.9f;
				setup.materialMask.hasSkin = false;
				setup.materialMask.hasHair = false;
				setup.materialMask.hasFur = false;
			}
			
			float redness = min(brdfParameters.baseColor.r - brdfParameters.baseColor.g, brdfParameters.baseColor.r - brdfParameters.baseColor.b) / brdfParameters.baseColor.r;
			float rednessThreshold = 0.85f;
			if (redness > rednessThreshold)
			{
				brdfParameters.baseColor.r += brdfParameters.baseColor.g + brdfParameters.baseColor.b;
				brdfParameters.baseColor.gb *= 0.0f;
			}
		}
		break;
	}
#endif

	return state;
}

void BonusRenderModePostSunShadow(inout Setup setup)
{
#ifdef BONUS_DEFERRED_MODE
	if (g_bonusDeferredMode == kBonusDeferredModeCellShaded || g_bonusDeferredMode == kBonusDeferredModeWatercolor)
	{
		setup.sunShadow = floor(setup.sunShadow * 6.0f + 0.5f) / 6.0f;
	}
	else if (g_bonusDeferredMode == kBonusDeferredModeAnimatedNoir && setup.isFg)
	{
		setup.sunShadow = floor(setup.sunShadow * 6.0f + 0.5f) / 6.0f;
	}
#endif
}

bool BonusRenderModeEnableSunLightSpec(const Setup setup)
{
#ifdef BONUS_DEFERRED_MODE
	if (g_bonusDeferredMode == kBonusDeferredModeCellShaded ||
		g_bonusDeferredMode == kBonusDeferredMode8Bit ||
		g_bonusDeferredMode == kBonusDeferredModeAnimatedNoir && setup.isFg && !setup.materialMask.isCharacter)
	{
		return false;
	}
#endif

	return true;
}

bool BonusRenderModeEnableRuntimeLightSpec(const Setup setup)
{
#ifdef BONUS_DEFERRED_MODE
	if (g_bonusDeferredMode == kBonusDeferredModeCellShaded ||
		g_bonusDeferredMode == kBonusDeferredMode8Bit ||
		g_bonusDeferredMode == kBonusDeferredModeWatercolor ||
		g_bonusDeferredMode == kBonusDeferredModeDungeon ||
		g_bonusDeferredMode == kBonusDeferredModeAnimatedNoir && setup.isFg && !setup.materialMask.isCharacter)
	{
		return false;
	}
#endif

	return true;
}

bool BonusRenderModeEnableAmbientLightSpec()
{
#ifdef BONUS_DEFERRED_MODE
	if (g_bonusDeferredMode == kBonusDeferredModeCellShaded ||
		g_bonusDeferredMode == kBonusDeferredMode8Bit ||
		g_bonusDeferredMode == kBonusDeferredModeDungeon)
	{
		return false;
	}
#endif

	return true;
}

void BonusRenderModePostBounceLight(BrdfParameters brdfParameters, inout float3 bounceLight)
{
#ifdef BONUS_DEFERRED_MODE
	if (g_bonusDeferredMode == kBonusDeferredModeVoid)
	{
		bounceLight = float3((max(bounceLight.x, bounceLight.y), bounceLight.z));
	}
#endif
}

void BonusRenderModeFinalize(BonusRenderModeState state, uint2 screenCoord, float depthZ, ComputeSrt *srt, Setup setup, inout float3 totalLight, BrdfParameters brdfParameters)
{
#ifdef BONUS_DEFERRED_MODE
	switch (g_bonusDeferredMode)
	{
	case kBonusDeferredModeCellShaded:
		{
			totalLight *= saturate(1.0f - state.m_highlight);
		}
		break;
	case kBonusDeferredModeRainbow:
		{
			float2 screenUv = screenCoord * g_vScreenSizeParam.xw / g_vBufferScaleInfo.xy;
			
			totalLight = snoise(500.0 * screenUv + g_time * 0.3);
			
			totalLight *= saturate(dot(setup.viewWS, setup.normalWS) * 2.0 - 1.0);

			float highlight = state.m_highlight / 1.5;	
			
			float3 highlightColor = float3(pow(0.25, 1.0 + highlight * 1.2f), 0, 0);
			
			if(setup.materialMask.isCharacter)
			{
				highlight *= 2.0;
				totalLight *= 2.0;
			}
		
			totalLight = lerp(totalLight, highlightColor, saturate(highlight));
			
			totalLight *= 3.0;
		}
		break;
	case kBonusDeferredModeAfterlife:
		{
			if (!setup.materialMask.isCharacter)
			{
				float dist = max(1.0f, setup.distanceToCamera / 60.0f);
				totalLight *= (1.0f / (dist*dist)) * 0.7f + 0.3f;
			}
		}
		break;
	case kBonusDeferredMode8Bit:
		{
			if (setup.materialMask.isCharacter)
			{
				totalLight *= 1.3f;
			}
		}
		break;
	case kBonusDeferredModeWatercolor:
		{
			float3 normalWS = setup.normalWS;
			if (state.m_highlight > 0.0f)
			{
				uint2 samplePos = screenCoord + int2(-1, 0);
				float depth1 = srt->m_depthBuffer.Load(int3(samplePos, 0)).x;
				float3 positionWS1 = GetPositionWS(uint2(samplePos), depth1, srt);
				samplePos = screenCoord + int2(0, -1);
				float depth2 = srt->m_depthBuffer.Load(int3(samplePos, 0)).x;
				float3 positionWS2 = GetPositionWS(uint2(samplePos), depth2, srt);
		    
				float3 vec1 = normalize(setup.positionWS - positionWS1);
				float3 vec2 = normalize(setup.positionWS - positionWS2);
				normalWS = cross(vec2, vec1);
			}
        
			totalLight += saturate(1.0f - dot(normalWS, setup.viewWS)) > 0.96f ? 25.0f * setup.ambientBaseColor : 0.0f;	
		}
		break;
	case kBonusDeferredModeDungeon:
		{
			float NdotV = saturate(dot(setup.normalWS, setup.viewWS));
			NdotV = floor(NdotV * 3.0f + 0.5f) / 3.0f;
			float edgeBlackenThreshold = 0.1f;
			float3 uneditedTotalLight = totalLight;
			if (setup.materialMask.isCharacter)
			{
				totalLight *= 1.7f;
				totalLight = lerp(totalLight, Luminance(totalLight), -0.4f);
				uneditedTotalLight = totalLight;
				float ssaoAmount = min(setup.ssao.x, setup.ssao.y);
				totalLight *= ssaoAmount < 0.5f ? 0.0f : 1.0f; 
				edgeBlackenThreshold = 0.4f;
			}
			else
			{
				totalLight = lerp(totalLight, Luminance(totalLight), 0.25f);
				totalLight *= 0.7f;
				totalLight *= state.m_highlight > 0.5f ? 0.0f : 1.0f;
			}
			
			float3 upVec = float3(0, 1, 0);
			upVec = cross(-setup.viewWS, upVec);
			float3 leftVec = upVec;
			upVec = cross(upVec, -setup.viewWS);
			float3 directionVec = Luminance(setup.ambientDirectionalColor) > Luminance(setup.sunColor) ? setup.dominantDirection : setup.sunDirWS;
			float3 lightVec = normalize(upVec + setup.viewWS * 0.7f + directionVec * 1.5f);
			if (setup.materialMask.isCharacter)
				lightVec = normalize(upVec + setup.viewWS * 0.7f + leftVec * 1.5f);
			
			float NdotL = dot(setup.normalWS, lightVec);
			float specNdotL = NdotL;
			NdotL = floor(NdotL * 3.0f + 0.5f) / 3.0f;
			totalLight *= NdotL < 0.0f ? 0.0f : 1.0f;
            
			totalLight *= NdotV < edgeBlackenThreshold ? 0.0f : 1.0f;
			
			if (specNdotL > 0.975f)
			{
				if (setup.materialMask.isCharacter)
					totalLight = 10.0f * uneditedTotalLight;
				else if (brdfParameters.translucency > 0.0f)
					totalLight = 5.0f * uneditedTotalLight;
					
				if (brdfParameters.metallic > 0.0f)
						totalLight = 30.0f * setup.ambientDirectionalColor;
			}
		}
		break;
	case kBonusDeferredModePopPoster:
		{
			float3 upVec = float3(0, 1, 0);
			upVec = cross(-setup.viewWS, upVec);
			float3 leftVec = upVec;
			upVec = cross(upVec, -setup.viewWS);
			float3 lightVec = normalize(upVec + setup.viewWS * 0.7f + leftVec * 1.5f);
			
			float3 extremeHighlightColor = float3(1.0f, 1.0f, 1.0f);
			float3 highlightColor = float3(1.0f, 0.9f, 0.5f);
			float3 midtoneColor = float3(0.8f, 0.0f, 0.0f);
			float3 shadowColor = float3(0.02f, 0.02f, 0.06f);
			float3 extremeShadowColor = float3(0.0f);
			
			if (setup.materialMask.isCharacter)
			{
				highlightColor = float3(1.0f, 1.0f, 0.0f);
				midtoneColor = float3(0.0f, 1.0f, 1.0f);
				shadowColor = float3(1.0f, 0.0f, 0.3f);
				if (setup.materialMask.hasHair)
				{
					highlightColor = float3(1.0f, 0.0f, 0.3f);
					midtoneColor = float3(1.0f, 0.0f, 0.3f);
					shadowColor = float3(0.0f);
				}
				else if (setup.materialMask.hasSkin)
				{
					highlightColor = float3(1.0f, 0.5f, 0.5f);
					midtoneColor = float3(1.0f, 0.5f, 0.5f);
					shadowColor = float3(1.0f, 0.2f, 0.2f);
				}
			}
			
			float NdotL = dot(lightVec, setup.normalWS);
			
			if (NdotL < 0.001f)
			{
				totalLight = shadowColor;
			}
			else if (NdotL < 0.5f)
			{
				totalLight = midtoneColor;
			}
			else
			{
				totalLight = highlightColor;
			}
			
			if (setup.materialMask.isCharacter)
			{
				if (setup.materialMask.hasSkin)
				{
					if (NdotL > 0.7f)
					{
						totalLight = extremeHighlightColor;
					}
				}
				else if (setup.materialMask.hasHair)
				{
					if (NdotL > 0.9f)
					{
						totalLight = extremeHighlightColor;
					}
					
					if (saturate(dot(setup.normalWS, setup.viewWS)) < 0.4f)
					{
						totalLight = extremeShadowColor;
					}
				}
				
				if (NdotL < -0.35f || state.m_highlight > 0.0f)
				{
					totalLight = extremeShadowColor;
				}
			}
		}
		break;
	case kBonusDeferredModeVoid:
		{
			if (setup.materialMask.isCharacter)
			{
				totalLight += setup.ambientBaseColor * (1.0f - saturate(dot(setup.viewWS, setup.normalWS)));
			}
		}
		break;
	case kBonusDeferredModeAnimatedNoir:
		{
			float redness = min(brdfParameters.baseColor.r - brdfParameters.baseColor.g, brdfParameters.baseColor.r - brdfParameters.baseColor.b) / brdfParameters.baseColor.r;
			float rednessThreshold = 0.8f;
			if (setup.materialMask.isCharacter || setup.isFg)
			{
				totalLight *= state.m_highlight > 0.3f ? 0.0f : 1.0f;
				totalLight *= saturate(setup.sunShadow + 0.5f);
				
				if (redness < rednessThreshold)
				{
					totalLight = float3(Luminance(totalLight));
					totalLight *= float3(1.05f, 1.00f, 0.95f);
				}
			}
			else
			{
				if (redness < rednessThreshold)
				{
					totalLight = float3(Luminance(totalLight));
				}
			}
		}
		break;
	}
	
	totalLight = max(float3(0.0f, 0.0f, 0.0f), totalLight);
	
#endif
}

float3 LightPixels(uint2 screenCoord, float depthZ, float viewZ, uint2 tile, ComputeSrt *srt, uint lightingMode, uint flags)
{
	bool isDebug = (flags & kDebugMode) != 0;

	// Load the gbuffer data
	BrdfParameters brdfParameters;
	Setup setup = (Setup)0;
	LoadGBuffer(screenCoord, srt, brdfParameters, setup, isDebug);

	// Setup setup (ha)
	float2 screenUvs				= ((float2)screenCoord + float2(0.5f, 0.5f)) * srt->m_runtimeParams.m_screenSize.zw;
	setup.positionWS				= GetPositionWS(screenCoord, depthZ, srt);
	setup.passId					= kPassDeferred;
	setup.viewWS					= srt->m_runtimeParams.m_cameraWS.xyz - setup.positionWS;
	setup.distanceToCamera			= length(setup.viewWS);
	setup.viewWS					/= setup.distanceToCamera;
	setup.cameraPositionWS			= srt->m_runtimeParams.m_cameraWS.xyz;
	setup.screenUv					= screenUvs;
	setup.depthVS					= viewZ;
	setup.screenPosition.xy			= screenCoord + 0.5f;
	setup.sunDirWS					= srt->m_lightDir;
	setup.sunColor					= srt->m_lightColor;
	setup.sunRadius					= srt->m_lightRadius;
	setup.stencilBits				= srt->m_stencilBuffer.Load(int3(screenCoord, 0));
	setup.isFg						= setup.stencilBits & 0x20;
	setup.normalDeriv				= GetNormalDerivative(screenCoord, setup.positionWS);
	setup.ssao						= srt->m_ssaoBuffer.SampleLevel(srt->m_linearClampSampler, screenUvs, 0).ra;
	setup.ssr						= srt->m_ssrBuffer.SampleLevel(srt->m_linearClampSampler, screenUvs, 0);
	setup.ambientBaseColor			= srt->m_ambientBaseBuffer.Load(int3(screenCoord, 0));
	setup.ambientDirectionalColor	= srt->m_ambientDirectionalBuffer.Load(int3(screenCoord, 0));
	setup.isRefMode					= (flags & kRefMode) != 0;

	if (isDebug && srt->m_debug.m_lightingSafeMode)
	{
		float3 uvw = float3(setup.screenUv, 0);
		uvw.z = CameraLinearDepthToFroxelZCoordExp(setup.depthVS, 0);
		
		float4 resultPixel = NdFetchNewVolumetricAmbientLight(uvw).xyzw;
		
		setup.ambientBaseColor = resultPixel.rgb * 8.0 / kPi;
		setup.ambientDirectionalColor = 0;
	}
		
	if ((!setup.isFg || !setup.materialMask.notReceiveAmbShadow) && !(setup.materialMask.hasSkin || setup.materialMask.hasEyes || setup.materialMask.hasTeeth))
	{
		setup.ambientShadow = srt->m_ambientShadowBuffer.SampleLevel(srt->m_pointSampler, screenUvs, 0).rgb;
	}
	else
	{
		setup.ambientShadow = float3(1, 1, 1);
	}

	BonusRenderModeState bonusState = BonusRenderModeSetup(srt, screenCoord, brdfParameters, setup);

	// Sun shadow
	ComputeSunShadow(setup, brdfParameters, *srt, isDebug);
	BonusRenderModePostSunShadow(setup);

	// Sunlight
	float3 sunLight = CalculateSunLight(setup, brdfParameters,
										srt->m_cutsceneSunMultiplier,
										srt->m_characterCutsceneSunMultiplier,
										BonusRenderModeEnableSunLightSpec(setup));

	// Runtime Light
	float3 runtimeLight = 0;
	if (lightingMode != kLightingModeNoRuntimeLights && (flags & kCalculateRuntimeLights) != 0)
	{
		uint runtimeLightFlags = BonusRenderModeEnableRuntimeLightSpec(setup) ? flags : (flags | kDisableSpecular);
		runtimeLight = GetRuntimeLightsAccumulation(*srt, brdfParameters, setup, tile, lightingMode, runtimeLightFlags);
	}

	// Emissive
	float3 emissiveLight = brdfParameters.emissive;

	// Bounce
	float3 bounceLight = srt->m_bounceLightBuffer.SampleLevel(srt->m_linearClampSampler, screenUvs, 0).rgb;
	BonusRenderModePostBounceLight(brdfParameters, bounceLight);

	// Subsurface Light and Bounce Light (the subsurface pass applies bounce, so we don't want to apply it here)
	float3 extraLight = 0;
	if (setup.materialMask.hasSkin && IsSubsurfaceEnabled())
	{
		extraLight = ComputeSkinSSS(screenCoord, setup, brdfParameters, *srt, isDebug);
	}
	else
	{
		extraLight = bounceLight * (1.0f - setup.ssr.a) * setup.ssao.y;

		// RSM lighting doesn't get baseColor applied for characters, so that it can be correctly combined with subsurface
		if (setup.materialMask.isCharacter)
		{
			extraLight *= brdfParameters.baseColor;
		}
	}
	
	// Ambient shadow
	float2 ambienShadowSample = srt->m_cloudShadowTexture.SampleLevel(srt->m_pointSampler, setup.screenUv, 0).gb;
	float ambientShadow = ambienShadowSample.x;
	ambientShadow *= CalculateAmbientCaustics(setup, ambienShadowSample.y);

	// Ambient Light
	float2 dummyMotionVector;
	float3 ambientLight = CalculateAmbientLighting(setup, brdfParameters,
												   dummyMotionVector, ambientShadow,
												   BonusRenderModeEnableAmbientLightSpec());
	
	// Combine light
	float3 totalLight = sunLight + runtimeLight + ambientLight + extraLight;
	totalLight += CalculateLocalBounceLighting(setup, brdfParameters, totalLight);
	totalLight += emissiveLight;

	ApplyEnemyHighlight(totalLight, setup.stencilBits);

	// WARNING: the same render target is used for both primary float and bounce lighting so 
	// non deferred pixels must pass-through the unmodified bounce light so that
	// forward shaders can read the correct bounce value
	uint isDeferred = setup.stencilBits & 1;
	if (!isDeferred)
	{
		totalLight = bounceLight;
	}

	BonusRenderModeFinalize(bonusState, screenCoord, depthZ, srt, setup, totalLight, brdfParameters);

	// Pre-exposure exposure bracketing
	ApplyExposureBracketing(totalLight, setup, srt->m_exposureBracketingParams.y, srt->m_exposureBracketingParams.x,
							srt->m_exposureBracketingStartDist, srt->m_exposureBracketingDistanceTerm);

	if (isDebug)
	{
		totalLight = ApplyDebug(totalLight, brdfParameters, setup, *srt, tile, lightingMode);
	} 
	
	return totalLight;
}

void DeferredLighting(uint dispatchIndex, uint2 groupThreadId, ComputeSrt *srt, uint lightingMode, uint flags)
{
	SetGlobalParameters(*srt);

	uint permutationIndex = (flags & kDebugMode) != 0 ? g_debugPermutationIndex : (PERMUTATION_INDEX + PERMUTATION_SET * kNumPermutations);
	uint tileIndex = srt->m_tileBuffers[permutationIndex * srt->m_maxPermutationTiles + dispatchIndex];

	uint2 groupId = uint2(tileIndex & 0xFFFF, (tileIndex >> 16) & 0xFFFF);
	uint2 screenCoord = groupId * DISPATCH_TILE_SIZE + groupThreadId;

	// Load linear depth
	const float depthZ = srt->m_depthBuffer.Load(int3(screenCoord, 0)).x;
	float viewZ = srt->m_runtimeParams.m_screenViewParameter.y / (depthZ - srt->m_runtimeParams.m_screenViewParameter.x);

	// Light the pixels
#if TILE_SIZE == 16 && DISPATCH_TILE_SIZE == 8
	uint2 tile = groupId / 2;
#else
	uint2 tile = groupId;
#endif

	float3 totalLight = 0;
	if (depthZ == 1)
	{
		totalLight = srt->m_clearColor.xyz;
	}
	else
	{
		totalLight = LightPixels(screenCoord, depthZ, viewZ, tile, srt, lightingMode, flags);
	}

	srt->m_rwAccumTarget[screenCoord] = float4(totalLight, 1.0f);
}

void DeferredLightingNonClassified(uint2 dispatchId, uint2 groupId, ComputeSrt *srt, uint lightingMode, uint flags)
{
	SetGlobalParameters(*srt);

	uint2 screenCoord = dispatchId.xy;

	// Load linear depth
	const float depthZ = srt->m_depthBuffer.Load(int3(screenCoord, 0)).x;
	float viewZ = srt->m_runtimeParams.m_screenViewParameter.y / (depthZ - srt->m_runtimeParams.m_screenViewParameter.x);

	// Light the pixels
#if TILE_SIZE == 16 && DISPATCH_TILE_SIZE == 8
	uint2 tile = groupId / 2;
#else
	uint2 tile = groupId;
#endif

	float3 totalLight = LightPixels(screenCoord, depthZ, viewZ, tile, srt, lightingMode, flags);

	if (depthZ == 1)
	{
		totalLight = srt->m_clearColor.xyz;
	}

	srt->m_rwAccumTarget[screenCoord] = float4(totalLight, 1.f);
}

struct DispatchSrt
{
	Texture2D<uint>		m_materialMaskBuffer;
	Texture2D<uint>		m_stencilBuffer;
	RWBuffer<uint2>		m_tileBuffers;

	uint				m_maxPermutationTiles;
	uint				m_uberShaderGdsOffsets[kNumPermutationSets * kNumPermutations];

	uint				m_uberPermutationTable[kNumShaderVariations / 4];
	uint				m_branchlessPermutationTable[kNumShaderVariations / 4];

	Buffer<uint>		m_numLightsPerTile;
	uint				m_numScreenLightTilesW;

	// Debug stuff
	uint				m_forceUberShader;
	uint				m_forceBasicShader;
	uint				m_disableForwardPass;
};

void DeferredDispatcher(uint3 dispatchId, uint3 groupId, uint groupIndex, DispatchSrt *srt, bool isDebug)
{
	// Load the material mask
	uint2 screenCoord = dispatchId.xy;

	// Load the stencil buffer
	uint stencil = srt->m_stencilBuffer.Load(int3(screenCoord, 0)) & 1;
	uint materialMask = srt->m_materialMaskBuffer.Load(int3(screenCoord, 0));

	// Forward objects are ignored
	materialMask = stencil ? materialMask : 0;

	// Merge the stencil bit into the material mask to get a reduction for free
	materialMask |= stencil << 31;

	// Reduce the material mask 
	uint orReducedMaskBits, andReducedMaskBits;
	ReduceMaterialMask(materialMask, groupIndex, orReducedMaskBits, andReducedMaskBits);

	uint reducedStencil = orReducedMaskBits & 0x80000000;

	if (reducedStencil == 0)
	{
		if (isDebug && srt->m_disableForwardPass)
		{
			orReducedMaskBits = (kNumShaderVariations - 1); // Force the uber shader
		}
		else
		{
			return;
		}
	}

	orReducedMaskBits &= (kNumShaderVariations - 1);
	andReducedMaskBits &= (kNumShaderVariations - 1);

	// Get the correct shader index
	//bool waveFrontConstantTile = __v_cmp_eq_u32(materialMask, ReadFirstLane(materialMask)) == ~0;
	//bool constantTileValue = IsTileConstantValue(waveFrontConstantTile, groupIndex);
	bool constantTileValue = orReducedMaskBits == andReducedMaskBits;
	
	uint tableSlot = orReducedMaskBits / 4;
	uint tableShift = (orReducedMaskBits - tableSlot * 4) * 8;

	uint tableIndex4 = constantTileValue ? srt->m_branchlessPermutationTable[tableSlot] : srt->m_uberPermutationTable[tableSlot];
	uint permutationIndex = (tableIndex4 >> tableShift) & 0xFF;

	if (isDebug && srt->m_forceUberShader)
	{
		permutationIndex = 1;
	}
	else if (isDebug && srt->m_forceBasicShader)
	{
		permutationIndex = 0;
	}

	if (groupIndex == 0)
	{
		{
			const uint2 tile = ReadFirstLane(screenCoord) / TILE_SIZE;
			const uint tileIndex = tile.x + tile.y * srt->m_numScreenLightTilesW;
			const uint numLights = srt->m_numLightsPerTile[tileIndex];

			if (numLights == 0)
			{
				permutationIndex += kSunOnlyPermutationSet * kNumPermutations;
			}
		}

		uint tileIndex = NdAtomicIncrement(srt->m_uberShaderGdsOffsets[permutationIndex]);
		srt->m_tileBuffers[permutationIndex * srt->m_maxPermutationTiles + tileIndex] = groupId.xy;
	}
}

[numthreads(DISPATCH_TILE_SIZE, DISPATCH_TILE_SIZE, 1)]
void CS_DeferredLighting(uint3 groupId : S_GROUP_ID, uint3 groupThreadId : S_GROUP_THREAD_ID,
						 ComputeSrt *srt : S_SRT_DATA)
{
	DeferredLighting(groupId.x, groupThreadId.xy, srt, kLightingModeTileLightLists, kCalculateRuntimeLights);
}

[numthreads(DISPATCH_TILE_SIZE, DISPATCH_TILE_SIZE, 1)]
void CS_DeferredLightingSunOnly(uint3 groupId : S_GROUP_ID, uint3 groupThreadId : S_GROUP_THREAD_ID,
								ComputeSrt *srt : S_SRT_DATA)
{
	DeferredLighting(groupId.x, groupThreadId.xy, srt, kLightingModeTileLightLists, 0);
}

[numthreads(DISPATCH_TILE_SIZE, DISPATCH_TILE_SIZE, 1)]
void CS_DeferredLightingZBin(uint3 groupId : S_GROUP_ID, uint3 groupThreadId : S_GROUP_THREAD_ID,
							 ComputeSrt *srt : S_SRT_DATA)
{
	DeferredLighting(groupId.x, groupThreadId.xy, srt, kLightingModeTileZBinning, kCalculateRuntimeLights);
}

[numthreads(DISPATCH_TILE_SIZE, DISPATCH_TILE_SIZE, 1)]
void CS_DeferredLightingCluster(uint3 groupId : S_GROUP_ID, uint3 groupThreadId : S_GROUP_THREAD_ID,
								ComputeSrt *srt : S_SRT_DATA)
{
	DeferredLighting(groupId.x, groupThreadId.xy, srt, kLightingModeTileClusteredLightLists, kCalculateRuntimeLights);
}

[numthreads(DISPATCH_TILE_SIZE, DISPATCH_TILE_SIZE, 1)]
void CS_DeferredLightingNonClassified(uint3 dispatchId : S_DISPATCH_THREAD_ID, uint3 groupId : S_GROUP_ID, ComputeSrt *srt : S_SRT_DATA)
{
	DeferredLightingNonClassified(dispatchId.xy, groupId.xy, srt, kLightingModeTileLightLists, kCalculateRuntimeLights);
}

[numthreads(DISPATCH_TILE_SIZE, DISPATCH_TILE_SIZE, 1)]
void CS_DeferredDispatcher(uint3 dispatchId : S_DISPATCH_THREAD_ID, uint3 groupId : S_GROUP_ID,
						   uint groupIndex : S_GROUP_INDEX, DispatchSrt *srt : S_SRT_DATA)
{
	DeferredDispatcher(dispatchId, groupId, groupIndex, srt, false);
}

[numthreads(DISPATCH_TILE_SIZE, DISPATCH_TILE_SIZE, 1)]
void CS_DeferredDispatcherDebug(uint3 dispatchId : S_DISPATCH_THREAD_ID, uint3 groupId : S_GROUP_ID,
								uint groupIndex : S_GROUP_INDEX, DispatchSrt *srt : S_SRT_DATA)
{
	DeferredDispatcher(dispatchId, groupId, groupIndex, srt, true);
}

struct DebugSrt
{
	ComputeSrt	*m_computeSrt;
	uint		m_permutationIndex;
};

[numthreads(DISPATCH_TILE_SIZE, DISPATCH_TILE_SIZE, 1)]
void CS_DeferredLightingDebug(uint3 groupId : S_GROUP_ID, uint3 groupThreadId : S_GROUP_THREAD_ID,
							  DebugSrt *srt : S_SRT_DATA)
{
	g_debugPermutationIndex = srt->m_permutationIndex;
	DeferredLighting(groupId.x, groupThreadId.xy, srt->m_computeSrt, kLightingModeTileLightLists, kCalculateRuntimeLights | kDebugMode);
}

[numthreads(DISPATCH_TILE_SIZE, DISPATCH_TILE_SIZE, 1)]
void CS_DeferredLightingDebugZBin(uint3 groupId : S_GROUP_ID, uint3 groupThreadId : S_GROUP_THREAD_ID,
								  DebugSrt *srt : S_SRT_DATA)
{
	g_debugPermutationIndex = srt->m_permutationIndex;
	DeferredLighting(groupId.x, groupThreadId.xy, srt->m_computeSrt, kLightingModeTileZBinning, kCalculateRuntimeLights | kDebugMode);
}

[numthreads(DISPATCH_TILE_SIZE, DISPATCH_TILE_SIZE, 1)]
void CS_DeferredLightingDebugCluster(uint3 groupId : S_GROUP_ID, uint3 groupThreadId : S_GROUP_THREAD_ID,
									 DebugSrt *srt : S_SRT_DATA)
{
	g_debugPermutationIndex = srt->m_permutationIndex;
	DeferredLighting(groupId.x, groupThreadId.xy, srt->m_computeSrt, kLightingModeTileClusteredLightLists, kCalculateRuntimeLights | kDebugMode);
}

[numthreads(DISPATCH_TILE_SIZE, DISPATCH_TILE_SIZE, 1)]
void CS_DeferredLightingDebugRefMode(uint3 groupId : S_GROUP_ID, uint3 groupThreadId : S_GROUP_THREAD_ID,
									 DebugSrt *srt : S_SRT_DATA)
{
	g_debugPermutationIndex = srt->m_permutationIndex;
	DeferredLighting(groupId.x, groupThreadId.xy, srt->m_computeSrt, kLightingModeTileLightLists, kCalculateRuntimeLights | kRefMode | kDebugMode);
}

