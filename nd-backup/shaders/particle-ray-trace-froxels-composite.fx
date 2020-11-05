/*
* Copyright (c) 2014 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "packing.fxi"
#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "global-const-buffers.fxi"
#include "tile-util.fxi"
#include "atomics.fxi"
#include "particle-cs.fxi"

#include "particle-ray-trace-cs-defines.fxi"
#include "particle-ray-trace-cs-ps.fxi"



[NUM_THREADS(8, 8, 1)]
void CS_ApplyFogDirectlyToFullRes(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsApplyFroxelsToDepthSrt *pSrt : S_SRT_DATA)
{
	float2 pos0 = dispatchId.xy;

	float4 destArr[1];

	float ssdepth[1];// GetLinearDepth(pSrt->m_depthTexture[pos], pSrt->m_depthParams);

	int intersectedAnyFlags = 0;
	uint4 clrtsharplo = __get_tsharplo(pSrt->m_destTexture0);
	uint4 clrtsharphi = __get_tsharphi(pSrt->m_destTexture0);
	uint common_opts = __kImage_RO | __kImage_texture2d;

	uint4 opaqueDepthtsharplo = __get_tsharplo(pSrt->m_opaqueDepthTexture);
	uint4 opaqueDepthtsharphi = __get_tsharphi(pSrt->m_opaqueDepthTexture);

	uint4 opaqueAlphaDepthtsharplo = __get_tsharplo(pSrt->m_opaqueAlphaDepthTexture);
	uint4 opaqueAlphaDepthtsharphi = __get_tsharphi(pSrt->m_opaqueAlphaDepthTexture);

	destArr[0] = float4(0, 0, 0, 1); //  pSrt->m_destTexture0[pos0];



	uint stencil = pSrt->m_opaquePlusAlphaStencil[pos0];

	bool isPlayer = stencil & 0x40;
	
		// todo: we need a third combination of neo + regular resolution of this shader to use this optimization
#if 1// IS_NEO_MODE

//FROXELS_CHECK_WATER is a vesion of shade that doesnt do compositing below/above water. instead it just applies fog up to water
#if FROXELS_CHECK_WATER || FROXELS_USE_WATER
	
	bool isWater = stencil & 0x02;
#endif

#if FROXELS_CHECK_WATER || (FROXELS_POST_WATER  && !FROXELS_UNDER_WATER) || (FROXELS_POST_WATER  && FROXELS_UNDER_WATER)// in above water + post water pass we have to apply on top of the water depth. and below water in pre water render we apply outside world only on top of water
		if (isWater)
		{
			image_load_result_t result = __image_load_mip(uint4(pos0, 0, 0), 0, opaqueAlphaDepthtsharplo, opaqueAlphaDepthtsharphi, common_opts);
			ssdepth[0] = bit_cast<float>(result.data);
		}
		else
#endif
		{
			image_load_result_t result = __image_load_mip(uint4(pos0, 0, 0), 0, opaqueDepthtsharplo, opaqueDepthtsharphi, common_opts);
			ssdepth[0] = bit_cast<float>(result.data);
		}
#else
		// with 1080p, we don't need extra stride
		image_load_result_t result = __image_load_mip(uint4(pos0, 0, 0), 0, depthtsharplo, 0, common_opts | __kImage_R128);
		ssdepth[0] = bit_cast<float>(result.data);;
#endif
	

#if (FROXELS_PRE_WATER && !FROXELS_UNDER_WATER) || (FROXELS_PRE_WATER && FROXELS_UNDER_WATER) // in pre water pass we apply fog only on top of water pixels. the post water pass will apply in top of everything
	
	if (isWater)
#endif
	{
		{
			PartRayTraceSetup setup;

			float3 uvw;

#if FROXELS_DYNAMIC_RES
			uvw.xy = pos0.xy * pSrt->m_screenSizeInv;
#else
			uvw.xy = pos0.xy / float2(SCREEN_NATIVE_RES_W_F, SCREEN_NATIVE_RES_H_F);
#endif
			// todo: this was debug option to allow composite fog from behind geo on top of geo
			// this would require also forcing the fog to exist behind geo

			float linearDepth = GetLinearDepth(ssdepth[0], pSrt->m_depthParams);
			float linearDepthOrig = linearDepth;
			//linearDepth = max(lerp(0, pSrt->m_fixedDepth, saturate((linearDepth-3) / 4.0)), linearDepth);

			uvw.z = CameraLinearDepthToFroxelZCoordExp(linearDepth, pSrt->m_fogGridOffset);

			float oneSliceDepth = DepthDerivativeAtSlice(uvw.z * kGridDepth);
			
			//linearDepth += oneSliceDepth * pSrt->m_fogCompositingDepthOffset;
			//uvw.z = CameraLinearDepthToFroxelZCoordExp(linearDepth, pSrt->m_fogGridOffset);

			// faster, but technically now linear depth doesnt match froxel coord, but that's ok
			uvw.z = saturate(uvw.z + pSrt->m_fogCompositingDepthOffset); //  pSrt->m_fogCompositingDepthOffset is negative or 0 and is pre-divided by grid depth

#if FROXELS_DYNAMIC_RES
			float2 pixelPosNorm = pos0.xy  * pSrt->m_screenSizeInv;
#else
			float2 pixelPosNorm = pos0.xy / float2(SCREEN_NATIVE_RES_W_F, SCREEN_NATIVE_RES_H_F);
#endif

#if !FROXELS_CHECK_REFLECTION
			float3 positionVs = float3(((pixelPosNorm)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * linearDepth;
			float3 posWs = mul(float4(positionVs, 1.0), pSrt->m_mVInv).xyz;
			float3 positionWS = posWs;
#endif

#if FROXELS_CHECK_REFLECTION
			// reflection pass

			if (pos0.x == 100 && pos0.y == 100)
			{
				//_SCE_BREAK();

			}
#if 0
			/*
			float3 MaCalculateViewVector(float3 aWorldPosition)
			{
			return (float3)(g_vCameraPosition.xyz - aWorldPosition.xyz);
			}

			setup.cameraWS = MaCalculateViewVector(setup.positionWS);
			setup.distanceToCamera = length(setup.cameraWS);
			setup.viewWS = setup.cameraWS / setup.distanceToCamera;
			*/

			float3 viewWS = normalize(pSrt->m_camPosWS - posWs);

			float3 flatReflectionVector = -MaReflect(viewWS, pSrt->g_reflectionSurfacePlane.xyz);

			float3 pointPlusFlatReflectionX = positionWS + flatReflectionVector;
			float worldFlatReflectionPosW = dot(pSrt->g_worldReflectionMat_3, float4(pointPlusFlatReflectionX.xyz, 1.0));
			float2 worldFlatReflectionPos =
				float2(dot(pSrt->g_worldReflectionMat_0, float4(pointPlusFlatReflectionX.xyz, 1.0)) / worldFlatReflectionPosW,
				dot(pSrt->g_worldReflectionMat_1, float4(pointPlusFlatReflectionX.xyz, 1.0)) / worldFlatReflectionPosW);
			

			float reflDepth = /*pSrt->m_reflectionDepthTexture. */ pSrt->m_opaqueAlphaDepthTexture.SampleLevel(pSrt->m_pointSampler, worldFlatReflectionPos, 0).x; // NdFetchReflectionDepthPoint(worldFlatReflectionPos);
			float reflViewDepth = pSrt->g_reflectionScreenViewParameter.y / (reflDepth - pSrt->g_reflectionScreenViewParameter.x);
			float2 reflNdc = worldFlatReflectionPos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
#endif
			// actually since we are writing on top of reflection, we should nto be doing calculations of transforming from main view pixel to reflection pixel

			float2	worldFlatReflectionPos = pixelPosNorm;
			float reflDepth = /*pSrt->m_reflectionDepthTexture. */ pSrt->m_opaqueAlphaDepthTexture.SampleLevel(pSrt->m_pointSampler, worldFlatReflectionPos, 0).x; // NdFetchReflectionDepthPoint(worldFlatReflectionPos);
			float reflViewDepth = pSrt->g_reflectionScreenViewParameter.y / (reflDepth - pSrt->g_reflectionScreenViewParameter.x);
			float2 reflNdc = worldFlatReflectionPos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);


			float4 reflViewSpacePos = float4(reflNdc * pSrt->g_reflectionScreenViewParameter1.xy * reflViewDepth, reflViewDepth, 1.f);
			float3 reflWorldPosition;
			reflWorldPosition.x = dot(float4(pSrt->g_reflectionViewWorldParameters[0].xy * pSrt->g_reflectionScreenViewParameter1.zw, pSrt->g_reflectionViewWorldParameters[0].zw), reflViewSpacePos);
			reflWorldPosition.y = dot(float4(pSrt->g_reflectionViewWorldParameters[1].xy * pSrt->g_reflectionScreenViewParameter1.zw, pSrt->g_reflectionViewWorldParameters[1].zw), reflViewSpacePos);
			reflWorldPosition.z = dot(float4(pSrt->g_reflectionViewWorldParameters[2].xy * pSrt->g_reflectionScreenViewParameter1.zw, pSrt->g_reflectionViewWorldParameters[2].zw), reflViewSpacePos);
			//float reflDist = length(positionWS - reflWorldPosition);


			// have world position of the point in the reflection. we will now sample at that location. it could be completely out of range of volumetric fog too since it is in vew space of main camera

			float4 reflPosH = mul(float4(reflWorldPosition, 1), pSrt->m_mVP);
			float3 reflPosNdc = reflPosH.xyz / reflPosH.w;

			uvw.x = reflPosNdc.x * 0.5f + 0.5f;
			uvw.y = -reflPosNdc.y * 0.5f + 0.5f;

			//uvw.x = min(uvw.x, 0.9f);
			//uvw.x = max(uvw.x, 0.1f);
			//uvw.y = min(uvw.y, 0.9f);
			//uvw.y = max(uvw.y, 0.1f);

			ssdepth[0] = reflPosNdc.z;
			
			// we clamp reflection lookups
			linearDepth = min(pSrt->m_maxDist, GetLinearDepth(ssdepth[0], pSrt->m_depthParams));

			uvw.z = CameraLinearDepthToFroxelZCoordExp(linearDepth, pSrt->m_fogGridOffset);

			// todo: we might want to apply offset here too, but let's see if it is actually needed.
#endif

			//uvw.z -= 1.0 / 64.0 / 2.0; // we need to adjust sampling point
			float4 resultPixel;

			// note we shouldnt be doing this line because it is for sampling as SH , not direct accumulation
			//density = clamp(density - pSrt->g_particleFogControls0.x, pSrt->g_particleFogControls0.z, pSrt->g_particleFogControls0.w);

			resultPixel.rgba = UnpackFinalFogAccumValue(SampleFinalFogAccumPackedValue(pSrt->m_srcFogFroxelsTemp, pSrt->m_linearSampler, uvw), pSrt->m_accumFactor);

			//resultPixel.rgba = pSrt->m_srcFogFroxelsTemp.SampleLevel(pSrt->m_linearSampler, uvw, 0).xyzw;

			float density = resultPixel.a; //  clamp(resultPixel.a, 0, 1); // note no reason to clamp since texture is unorm
										   // in alpha we need to store how much of background we can see
			resultPixel.a = density;

#if FROXELS_CHECK_REFLECTION
			//resultPixel.rgb = float3(0.1, 0.1, 0.1);
			//resultPixel.a = reflDist;// linearDepth;

#endif
			//resultPixel.rgb = float3(1, 1, 1) * (1.0 - resultPixel.a);

			destArr[0] = resultPixel;

			float4 destColor = pSrt->m_destTexture0[pos0];

			//if (!isfinite(uvw.z))
			//{
			//	resultPixel = float4(1.0, 0, 0, 0.5);
			//}

			// srcColor is already pre-blended with its alpha
			//resultPixel = float4(0.1, 0, 0, 0.9);

			

			#if !FROXELS_CHECK_REFLECTION
			{

				// first construct view space look direction
				// the convert it to world space and use the height component as measurement of how far from horizon


				// underwater darkening
				#if FROXELS_USE_WATER
				float3 darkening = float3(1.0, 1.0, 1.0);
				#if (FROXELS_POST_WATER && FROXELS_UNDER_WATER) || (FROXELS_PRE_WATER && !FROXELS_UNDER_WATER) 
					// we do not absorb on water pixels when under water because SSR will receive absorption incorrectly
					// instead, we do the absorption in the fx water shader
					// ssr not receiving any extra absorption is technically also incorrect, but it looks much less wrong than if we do double absorption on ssr
					#if (FROXELS_POST_WATER && FROXELS_UNDER_WATER)
					if (!isWater)
					#endif
					{
						float waterPlaneHeight = pSrt->m_startHeight;
						float pixelHeight = posWs.y;
						float cameraHeight = pSrt->m_camPosWS.y;
						float sceneDepth = linearDepthOrig;
						float pixelUnderwaterHeight = waterPlaneHeight - pixelHeight;
						float lightDistance = 0;
						float cameraUnderWaterHeight = waterPlaneHeight - cameraHeight;

						if (cameraUnderWaterHeight > 0.0f || pixelUnderwaterHeight > 0.0f)
						{
							pixelUnderwaterHeight = max(pixelUnderwaterHeight, 0.0f);

							float differenceHeight = abs(pixelHeight - cameraHeight) + 0.00001f;
							float percentageUnderWater = abs(max(0, cameraUnderWaterHeight) - max(0, pixelUnderwaterHeight)) / differenceHeight;

							sceneDepth *= percentageUnderWater;
							sceneDepth = min(sceneDepth, pSrt->m_bgAbsorbMaxDist) * pSrt->m_bgAbsorbDistanceIntensity;

							pixelUnderwaterHeight = min(pixelUnderwaterHeight, pSrt->m_bgAbsorbMaxHeight) * pSrt->m_bgAbsorbHeightIntensity;
							lightDistance = pixelUnderwaterHeight + sceneDepth;
							float3 bgAbsorption = pSrt->m_absorbSpeed;
							float3 absorptionAttenuation = exp(-lightDistance * bgAbsorption);
							darkening = max(absorptionAttenuation, 0.01f);
						}
					}
				#endif

				destColor.xyz *= darkening;

				#endif

				// horizon blend
				
				float causticsMult = pSrt->m_causticsTexture.SampleLevel(SetWrapSampleMode(pSrt->m_linearSampler), float3(positionVs.xy / 40.0f, pSrt->m_causticsZ), 0.0f).x;

				//destColor.xyz *= causticsMult;

				float3 dirVs = normalize(positionVs);


				// conver direction to Ws
				float3 dirWs = mul(float4(dirVs, 0.0), pSrt->m_mVInv).xyz;

				float3 normalWS = dirWs;
				float horizonBlend = 1.0 - saturate((normalWS.y - pSrt->m_horizonBlendHeight) / pSrt->m_horizonBlendHardness);	// Construct Y position from the normal, and a user-specified depth
				float horizonBlend2 = 1.0 - saturate((normalWS.y - pSrt->m_horizonBlend2Height) / pSrt->m_horizonBlend2Hardness);	// Construct Y position from the normal, and a user-specified depth

				if (ssdepth[0] != 1.0)
				{
					horizonBlend *= saturate((linearDepth - pSrt->m_horizonBlendTightness) / pSrt->m_horizonBlendTightnessRange);
					horizonBlend2 *= saturate((linearDepth - pSrt->m_horizonBlend2Tightness) / pSrt->m_horizonBlend2TightnessRange);
				}
				
				horizonBlend *= horizonBlend;
				horizonBlend2 = min(pSrt->m_horizonBlend2Intensity, horizonBlend2);
				//horizonBlend2 *= horizonBlend2;

				// Horizon blend
				float3 mipFogColor = float3(1.0, 1.0, 1.0);
				if (horizonBlend > 0.001 || horizonBlend2 > 0.001)
				{
					// sky texture
					float3 dirToFroxel = normalWS;

					float2 texUvCoord = GetFogSphericalCoords(dirToFroxel, pSrt->m_skyAngleOffset);
					//float numMipLevels = 8;
					//float mipBias = (1.0 - sqrt(saturate((pixelDepth - pSrt->m_mipfadeDistance) / (numMipLevels*pSrt->m_mipfadeDistance))))*numMipLevels;
					float mipBias = 0;

					mipFogColor = pSrt->m_skyTexture.SampleLevel(SetWrapSampleMode(pSrt->m_linearSampler), texUvCoord, mipBias);
				
					destColor.xyz = lerp(destColor.xyz, pSrt->m_horizonBlendColor * mipFogColor, horizonBlend);

					destColor.xyz = lerp(destColor.xyz, lerp(pSrt->m_horizonBlend2Color, pSrt->m_horizonBlend2Color * mipFogColor, pSrt->m_horizonBlend2SkyFactor), horizonBlend2);
				}

				//resultPixel.xyz = lerp(resultPixel.xyz, pSrt->m_horizonBlendColor * mipFogColor, horizonBlend);

				//horizonBlend = 1.0;

				// w is how much of background is visible
				// if horizon blend is 1.0 we want no bg visible so w goes to 0
				//resultPixel.w =  saturate(resultPixel.w * (1.0f - horizonBlend));
			}
			#endif

			// compositing
			#if !FROXELS_CHECK_REFLECTION
			if (ssdepth[0] == 1.0)
			{
				#if FROXELS_DEBUG_COMPOSITE
				if (pSrt->m_debugIsolateOverSky)
				{
					destColor.rgb = float3(0, 0, 0);
				}
				#endif

				// sky
				destColor.rgb = destColor.rgb * (resultPixel.a + (1.0 - resultPixel.a) *  (1.0 - pSrt->m_fogOpacityFactorOverSky)) + resultPixel.rgb * pSrt->m_fogOpacityFactorOverSky;
			}
			else
			#endif
			{
				// "contrast"
				// TODO:	change this to a multiplier, get rid of min. after E3.

				float3 fadedColor = lerp(float3(pSrt->m_contrastMin), float3(pSrt->m_contrastMax), destColor.xyz);
				float fadedColorAmount = saturate((linearDepth - pSrt->m_contrastStart) / pSrt->m_contrastRange);

				#if !FROXELS_CHECK_REFLECTION
					destColor.xyz = lerp(destColor.xyz, fadedColor, fadedColorAmount);
				#endif

				// hack for water oversatuarting with fog

				// we need to "subtrack" fog from reflected pixel
				float4 destColorOrig = min(destColor, max(float4(0, 0, 0, 0), destColor.rgba - resultPixel.rgba) / max(resultPixel.a, 0.0001));


				// apply fog after distance contrast
				#if (FROXELS_CHECK_WATER || (FROXELS_POST_WATER && !FROXELS_UNDER_WATER) || (FROXELS_POST_WATER && FROXELS_UNDER_WATER)) && FROXELS_REFLECTION_HACK
				if (isWater)
				{
					// how much of this hack is applied is controlled by a threshold
					// we want to apply this hack when the fog is thicker, because it is more visible
					// and otherwise keep behavior the same when we don't apply that much fog onto surface
					float f = saturate((pSrt->m_waterReflectionHackApplyStart - resultPixel.a) * pSrt->m_waterReflectionHackApplyRangeInv);
					destColor.rgb = lerp(destColor.rgb, destColorOrig.rgb, f);
				}
				//else
				#endif

				#if FROXELS_DEBUG_COMPOSITE
				if (pSrt->m_debugIsolateOverGeo)
				{
					destColor.rgb = float3(0, 0, 0);
				}
				#endif

				
				destColor.rgb = destColor.rgb * resultPixel.a + resultPixel.rgb;
			}

			//destColor.g = int(uvw.x * 160) % 2;

			//float closeToCenterOfFroxel = 1.0 - frac(linearFogDepth - 0.5); // only works for non exponential depth fog
			
#if 0
			int iSlice = uvw.z * 64;
			int color = iSlice % 4;

			destColor.rgb = float3(0.5, 0.5, 0.5);
			if (iSlice >= 20 && iSlice <= 23)
			{
				if (color == 0)
				{
					destColor.rgb = float3(1, 0, 0);
				}
				else if (color == 1)
				{
					destColor.rgb = float3(0, 1, 0);
				}
				else if (color == 2)
				{
					destColor.rgb = float3(0, 0, 1);
				}
				else if (color == 3)
				{
					destColor.rgb = float3(1, 1, 0);
				}
			}

			//destColor.rgb = float3(uvw.xy, 0);
#endif
			

			// destColor.rgb = 0.5 * float3(resultPixel.a, resultPixel.a, resultPixel.a);
			//closeToCenterOfFroxel = fmod(uvw.z, 1.0 / 64.0) / (1.0 / 64.0);
			//destColor.rgb = float3(closeToCenterOfFroxel, closeToCenterOfFroxel, closeToCenterOfFroxel);

			pSrt->m_destTexture0[pos0] = destColor;


		}
	}

	//float4 c = destArr[0];
	// todo: we need a third combination of neo + regular resolution of this shader to use this optimization
#if 1// IS_NEO_MODE
	//__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), __get_tsharphi(pSrt->m_destTexture0), __kImage_texture2d);
#else
	//__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);
#endif
}

