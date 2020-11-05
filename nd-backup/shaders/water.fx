
#include "global-funcs.fxi"
#include "water-funcs.fxi"

float4 posToProj4(float3 position, matrix mVP)
{
	return mul(float4(position.xyz, 1.0), mVP);
}


VertexShaderOutput Vs_QuadTree(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID, SrtVsData srt : S_SRT_DATA)
{
	VertexShaderOutput OUT;

	int numVerticesPerBlock = srt.m_consts->m_numVerticesPerBlock;
	//	int sizeBlock = srt.m_consts->m_sizeBlock;
	// Get the offset into the vertex data
	int index = instanceId * numVerticesPerBlock + vertexId;
	
	float4		basePosition = srt.m_grid[index].m_basePosition;
	float3		position     = srt.m_grid[index].m_position;
	float3		normal       = srt.m_grid[index].m_normal.xyz;
	float4		color0	     = srt.m_grid[index].m_color0;
	float4		color1	     = srt.m_grid[index].m_color1;
	float3		positionPrev = srt.m_grid[index].m_positionPrev;
	float       foam         = color0.z;
	float       subsurface   = color0.w;
	float2      flow         = color0.xy;
	float       strain       = color1.x;

	OUT.hPosition.xyzw  = posToProj4(position.xyz - srt.m_consts->m_altWorldOrigin.xyz, srt.m_consts->m_mAltWorldToScreen);
	OUT.lastFramePositionHS = posToProj4(positionPrev.xyz - srt.m_consts->m_altWorldOriginLastFrame.xyz, srt.m_consts->m_mAltWorldToScreenLastFrame).xyw;
	OUT.worldPosition  =  position;
	OUT.worldNormal     = normalize(normal);

	float3 bn = normalize(cross(float3(1,0,0), normal));
	float3 tn = normalize(cross(normal, bn));
	
	OUT.worldTangent    = float4(tn, 0); 
	OUT.worldBinormal   = bn;

	float2 uv = basePosition.xy;

	// uv = abs(uv) + .00001;
	// float2 uvi = floor(uv);
	// uv = uv - uvi;
	OUT.uv0			    	= basePosition.xy; // base position
	OUT.uv1			    	= basePosition.zw; // master Tx uv coords

	OUT.waveFoamMask		= foam;
	OUT.waveSubsurfaceMask	= subsurface;

	// Output float4(strain, lerp, unused, unused)
	OUT.color0              = color1;	
	
	return OUT;
};

// Debug Vs Shader
// Set extra information into color channels
VertexShaderOutput Vs_QuadTreeDebug(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID, SrtVsData srt : S_SRT_DATA)
{
	VertexShaderOutput OUT;

	int numVerticesPerBlock = srt.m_consts->m_numVerticesPerBlock;
	int index = instanceId * numVerticesPerBlock + vertexId;
	
	float4		basePosition = srt.m_grid[index].m_basePosition;
	float3		position     = srt.m_grid[index].m_position;
	float3		normal       = srt.m_grid[index].m_normal.xyz;
	float4		color0	     = srt.m_grid[index].m_color0;
	float4		color1	     = srt.m_grid[index].m_color1;
	float3		positionPrev = srt.m_grid[index].m_positionPrev;
	float       foam         = color0.z;
	float       subsurface   = color0.w;
	float2      flow         = color0.xy;

	OUT.hPosition.xyzw  = posToProj4(position.xyz - srt.m_consts->m_altWorldOrigin.xyz, srt.m_consts->m_mAltWorldToScreen);
	OUT.lastFramePositionHS = posToProj4(positionPrev.xyz - srt.m_consts->m_altWorldOriginLastFrame.xyz, srt.m_consts->m_mAltWorldToScreenLastFrame).xyw;
	OUT.worldPosition  =  position;
	OUT.worldNormal    =  normal;

	float3 bn = normalize(cross(float3(1,0,0), normal));
	float3 tn = normalize(cross(normal, bn));
	OUT.worldTangent    = float4(tn, 0); 
	OUT.worldBinormal   = bn; 

	float2 uv = basePosition.xy;

	// uv = abs(uv) + .00001;
	// float2 uvi = floor(uv);
	// uv = uv - uvi;
	OUT.uv0			    	= basePosition.xy; // base position
	OUT.uv1			    	= basePosition.zw; // master Tx uv coords

	OUT.waveFoamMask		= foam;
	OUT.waveSubsurfaceMask	= subsurface;	

	OUT.color0              = color1;
	// For modes that don't have an interpolator, use the color0
	// to pass the information
	// e.g
	// if (srt.m_consts->m_showFlags & kShowTextureUV) { OUT.color0 = something; }

	return OUT;
}


//static const float3 kSlightlyDesatViolet = float3(170.0/256, 151.0/256, 210.0/256);
static const float3 kSlightlyDesatBlue = float3(130.0/256, 151.0/256, 210.0/256);

struct SrtPsData
{
	QuadBlockConstantInfo * m_consts;
	TexturesAndSamplers   * m_texturesAndSamplers;
};

float3 hdr(float3 color, float exposure) 
{
	return 1.0 - exp(-color * exposure);
}

float3 Sky( float3 ray )
{
	return .5 * float3(.4,.45,.5);
}

float3 ShadeOcean( float3 pos, float3 norm, float3 ray )
{
	//	float3 norm = OceanNormal(pos);
	float ndotr = dot(ray,norm);

	float fresnel = saturate(pow(1.0-abs(ndotr),5.0));
	
	float3 reflectedRay = ray-2.0*norm*ndotr;
	float3 refractedRay = ray+(-cos(1.33*acos(-ndotr))-ndotr)*norm;	
	refractedRay = normalize(refractedRay);

	// reflection
	float3 reflection = .2 * Sky(reflectedRay);
	
	float3 col = .5 * float3(0.04,.14,.14); // float3(0.04,.14,.14); // under-sea colour
	col = lerp( col, reflection, fresnel );

	// foam
	//col = mix( col, float3(1), WaveCrests(pos) );
	
	return col;
}

float3 GetCheckboard(float2 uv)
{
	float3 checker;
	if (uv.x > 0.5) {
		if (uv.y > 0.5) {
			checker.rgb = float3(1) ;
		} else {
			checker.rgb = float3(.2);
		}
	} else {
		if (uv.y > 0.5) {
			checker.rgb = float3(.2) ;
		} else {
			checker.rgb = float3(1);
		}
	}
	return checker;
}

float3 MaVec3TangentToWorld (float3 aTangentVector, float3 aWorldTangent, float3 aWorldBinormal, float3 aWorldNormal)
{
	float3 vWorldVector;

	vWorldVector.xyz  = aTangentVector.x * aWorldTangent.xyz;
	vWorldVector.xyz += aTangentVector.y * aWorldBinormal.xyz;
	vWorldVector.xyz += aTangentVector.z * aWorldNormal.xyz;
	return vWorldVector.xyz; //Return without normalizing
}

float3 MaVec3TangentToWorldNormalized (float3 aTangentVector, float3 aWorldTangent, float3 aWorldBinormal, float3 aWorldNormal)
{
	// We don't need to normalize if we assume the vectors form an orthonormal basis (which they do)
	return MaVec3TangentToWorld (aTangentVector, aWorldTangent, aWorldBinormal, aWorldNormal);
}

float3 MaComputeNormalFromRGPixel (float2 aRGPixel)
{
	float3 vTangentNormal;

	vTangentNormal.xy = aRGPixel.rg * 2.0 - 1.0;
	vTangentNormal.z = sqrt (saturate (1.0 - dot (vTangentNormal.xy, vTangentNormal.xy)));

	return normalize (vTangentNormal.xyz);
}

float3 MaComputeNormalFromRGPixelScaled (float2 aRGPixel, float bumpScalePerPixel)
{
	//Do this before the scale so we get a valid normal
	float3 vTangentNormal = MaComputeNormalFromRGPixel (aRGPixel.rg);

	//Now scale and normalize
	vTangentNormal.xy  *= (float)bumpScalePerPixel;
	vTangentNormal.xyz  = normalize (vTangentNormal.xyz);

	return vTangentNormal.xyz;
}

// motion vector rendering
PixelShaderOutput2 Ps_QuadTreeDepth(VertexShaderOutput interp, SrtPsData srt : S_SRT_DATA) : SV_Target
{
	PixelShaderOutput2 OUT;

	float3 worldPosition = interp.worldPosition; 
	float3 worldNormal   = interp.worldNormal;
	//	float3 positionPrev  = interp.positionPrev.xyz;

	OUT.color0 = float4(worldNormal, 0.0);

	float4 g_mPJitterOffsets = srt.m_consts->m_projectionJitterOffsets;
	float2 positionHS = interp.hPosition.xy * srt.m_consts->m_screenSizeParam.xw * float2(2.f, -2.f) + float2(-1.f, 1.f);

	float2 motionBlurSS = positionHS - g_mPJitterOffsets.xy;
	float2 motionBlurLastFrameSS = interp.lastFramePositionHS.xy / interp.lastFramePositionHS.z - g_mPJitterOffsets.zw;
	OUT.color1 = float4((motionBlurLastFrameSS - motionBlurSS) * float2(0.5f, -0.5f), 0.f, 0.f);

	return OUT;
}

float4 Ps_QuadTree(VertexShaderOutput interp, SrtPsData srt : S_SRT_DATA) : SV_Target
{
	float3 worldPosition = interp.worldPosition; 
	float3 worldNormal   = interp.worldNormal;
	float  foam          = interp.waveFoamMask; 
	float  subsurface    = interp.waveSubsurfaceMask;
	float  strain        = interp.color0.r;
	float2 flow          = interp.color0.rg;  
	float  amplitude     = interp.color0.r;
	float  height        = interp.color0.r;
	float  alpha         = interp.color0.r;
	float3 heightNormal  = interp.color0.rgb;
	float4 color0        = interp.color0;	
	float2 uv            = interp.uv0;
	float2 textureUv     = interp.uv1;
	
	float3 detailNormal = 0;
	float4 color;

	float3 levelColors[11] = {{1,1,1}, {1,0,0}, {0,1,0}, {.5,.5,.5}, {0,0,1}, {0,1,1}, {1,0,1}, {1,1,0}, {0,1,1}, {1,1,0}, {1,.5,0}};

	TexturesAndSamplers   *textures = srt.m_texturesAndSamplers;
	SamplerState      textureSampler = srt.m_texturesAndSamplers->g_linearClampSampler;

	float3 L = float3(0, 1, 0);
	float3 N = normalize(worldNormal + detailNormal);
	float light = 0.125 + (1 - 0.125) * (0.5 + 2.5 * dot(N, L)); 
	//color.rgb = light * kSlightlyDesatBlue;
	float3 shaded = kSlightlyDesatBlue * saturate(dot(N,L));
	
	color.rgb = shaded;
	color.a = 1.0;

	// Watery color
	float3 view = normalize(srt.m_consts->m_camera.xyz - worldPosition);
	float  fresnel = 0.02 + 0.98 * pow(1.0 - dot(worldNormal, view), 5.0);
	float3 sky = fresnel * srt.m_consts->m_skyColor.xyz;
	float diffuse = clamp(dot(worldNormal, normalize(srt.m_consts->m_sunDirection.xyz)), 0.0, 1.0);
	float3 water = (1.0 - fresnel) * srt.m_consts->m_oceanColor.xyz * srt.m_consts->m_skyColor.xyz * diffuse;
	float3 hcolor = sky + water;

	float2 positionHS = interp.hPosition.xy * srt.m_consts->m_screenSizeParam.xw * float2(2.f, -2.f) + float2(-1.f, 1.f);

	if (srt.m_consts->m_showFlags & kShowNormals) {
		color.rgb = normalize(worldNormal) * .5 + .5;
		color.a = 1.0;
	}

	if (srt.m_consts->m_showFlags & kShowNormalsFlat) {
		float3 tx = ddx(worldPosition);
		float3 tz = ddy(worldPosition);
		float3 n = normalize(cross(tz,tx));
		color.rgb = n * .5 + .5;

		if (positionHS.x < 0) {
			color.rgb = normalize(worldNormal) * .5 + .5;
			// float3 normalMap  = textures->m_heightfieldNormalTx.SampleLevel(textureSampler, textureUv, 0).rgb; 
			// color.rgb = normalMap;
		}
	}

	if (srt.m_consts->m_showFlags & kShowFoam) {
		color.rgb = foam;
	}

	if (srt.m_consts->m_showFlags & kShowFresnel) {
		color.rgb = fresnel; 
	}


	if (srt.m_consts->m_showFlags & kShowSubsurface) {

		//color.rgb = float3(interp.uv0.xy, interp.uv1.x) + interp.worldNormal.xyz + interp.worldPosition.xyz + interp.uv2uv3.xyz + interp.waterControl.xyz + interp.foamUV.xyz + interp.color1.xyz;
		//color.rg = .1 * interp.worldPosition.xz  + float2(10,10);
		color.rgb = subsurface;
	}

	if (srt.m_consts->m_showFlags & kShowFlow) {
		color.rg = flow;
		color.b = 0;
	}

	if (srt.m_consts->m_showFlags & kShowAmplitude) {
		color.rgb = amplitude;
	}

	if (srt.m_consts->m_showFlags & kShowCheckerboard) {
		float2 uvc = 0;
		uvc = nfmod(uv.xy, float2(1.0));

		float2 uv2 = nfmod(worldPosition.xz, float2(1.0));

		float3 checker = GetCheckboard(uvc);

		float3 shadeOcean = ShadeOcean( worldPosition, worldNormal.xyz, view );
		//shadeOcean = lerp(shadeOcean, 9.0 * float3(0.004, 0.036, 0.027), 3.0 * subsurface);
		//float3 shadeOcean = color0;

		color.rgb = shaded * checker;
		color.rgb = shaded * 12.0 * checker * shadeOcean;
	}

	if (srt.m_consts->m_showFlags & kShowOcean) {

		float2 baseUV = uv.xy;
		float2 flowUV = uv.xy;
		if (srt.m_consts->m_useFlags & (kUseUVDistortionTexture | kUseFlow | kUseHeightmap)) 
		{
			flowUV.x = (baseUV.x - srt.m_consts->m_deformationPosScale.x) / (srt.m_consts->m_deformationPosScale.z);
			flowUV.y = (baseUV.y - srt.m_consts->m_deformationPosScale.y) / (srt.m_consts->m_deformationPosScale.w);
		}

		float2 uvbase = srt.m_consts->m_uvScale * uv.xy;
		float3 normal = worldNormal.xyz;
		Texture2D<float4> normalMapTx    = srt.m_texturesAndSamplers->m_normalMapTx;
		SamplerState textureSamplerClamp = srt.m_texturesAndSamplers->g_linearClampSampler;

		float3 flowTx = srt.m_texturesAndSamplers->m_flowTx.SampleLevel(textureSamplerClamp, flowUV, 0).rgb;
		float2 flowDir = flowTx.xy * 2.0 - 1.0;

		float timeIntervalInSec = srt.m_consts->m_flowInterval; 
		float timeFlow   = srt.m_consts->m_time / timeIntervalInSec;

		float2 scrollTime = frac(float2(timeFlow, timeFlow - 0.5f)) * 2.0f - 1.0f;

		float  flowMag = srt.m_consts->m_flowMagnitude;
		float  scrollTime1 = (scrollTime.x * flowMag) * srt.m_consts->m_flowStrength.w;
		float  scrollTime2 = (scrollTime.y * flowMag) * srt.m_consts->m_flowStrength.w;

		float2 uv1 = uvbase - (flowDir * flowMag *.5) + flowDir * scrollTime1 ;
		float2 uv2 = uvbase - (flowDir * flowMag *.5) + flowDir * scrollTime2 ;

		float fblendVal = abs(2.0 * (1.0f - frac(timeFlow - 0.5f)) - 1.0f);

		float3 normalMap1 = normalMapTx.SampleLevel(textureSampler, uv1, 0).rgb;
		float3 normalMap2 = normalMapTx.SampleLevel(textureSampler, uv2, 0).rgb;

		float3 normalMap = lerp(normalMap1, normalMap2, fblendVal);

		float3 normalWS = MaComputeNormalFromRGPixelScaled( normalMap.xy, 1.0);

		normalWS = lerp(normalWS.xyz, worldNormal.xyz, .25);

		//normalTS.xy = NdFetchNormal(uv);
		//float3 normalWS = MaVec3TangentToWorldNormalized(normal, float3(1,0,0), float3(0,0,1), normal);

		float3 shadeOcean = ShadeOcean( worldPosition, normalWS, view );
		color.rgb = shaded * 5.0 * shadeOcean;

	}

#if 0
	if (srt.m_consts->m_showFlags & kShowCheckerboardFlow) {

		float2 uvbase = nfmod(srt.m_consts->m_uvScale * uv.xy, float2(1.0));

		float3 checker = 0;

		{
			float timeIntervalInSec = srt.m_consts->m_flowInterval; 
			float timeFlow   = srt.m_consts->m_time / timeIntervalInSec;

			float2 scrollTime = frac(float2(timeFlow, timeFlow - 0.5f)) * 2.0f - 1.0f;
			float2 xz1 = (scrollTime.x * srt.m_consts->m_flowMagnitude) * flow.xy  + uvbase;
			float2 xz2 = (scrollTime.y * srt.m_consts->m_flowMagnitude) * flow.xy  + uvbase;

			float fblendVal1 = abs(2.0 * (1.0f - frac(timeFlow - 0.5f)) - 1.0f);
			float fblendVal2 = 1.0f - fblendVal1;

			float3 checker1 = GetCheckboard(xz1);
			float3 checker2 = GetCheckboard(xz2);

			checker = fblendVal1 * checker1 + fblendVal2 * checker2;
		}

		//checker = GetCheckboard(uv);

		float3 shadeOcean = ShadeOcean( worldPosition, worldNormal.xyz, view );

		color.rgb = shaded * 12.0 * checker * shadeOcean;
	}
#endif

	if (srt.m_consts->m_showFlags & kShowStrain) {
		color.rgb = strain;
	}
	if (srt.m_consts->m_showFlags & kShowDepthMasks) {
		color.rgb = color0.rgb;
	}
	
	if (srt.m_consts->m_showFlags & kShowLerpDistance) {
		// float d = max(0, dot(1, color0.xyz));
		// color.rgb = (d < srt.m_consts->m_debug0) ? float3(0) : float3(1);
		color.rgb = color0.xyz; 
	}

	bool inside =  (textureUv.x > 0 && textureUv.x < 1 && textureUv.y > 0 && textureUv.y < 1) ? true : false;

	// Any feature that uses the main Texture

	{
		if (srt.m_consts->m_showFlags & kShowHeight) {
			color.rgb = height;
		}
		if (srt.m_consts->m_showFlags & kShowTextureUV) {
				color.rg = textureUv;
				color.b = 0;
		}
		if (srt.m_consts->m_showFlags & kShowFrequencyRGB) {
			float3 frequency = textures->m_frequencyMaskTx.SampleLevel(textureSampler, float2(textureUv.x,textureUv.y), 0).rgb;
			color.rgb = frequency;
		}
		if (srt.m_consts->m_showFlags & kShowFrequencyAlpha) {
			float frequency = textures->m_frequencyMaskTx.SampleLevel(textureSampler, float2(textureUv.x,textureUv.y), 0).a;
			color.rgb = frequency;
		}

		if (srt.m_consts->m_showFlags & kShowDistortUV) {
			float2 distort = textures->m_uvDistortionTx.SampleLevel(textureSampler, float2(textureUv.x,textureUv.y), 0).rg * 2.0 - 1.0;
			color.rg = distort;
			color.b = 0;
		}
		if (srt.m_consts->m_showFlags & kShowHeightNormal) {
			color.rgb = heightNormal;
		}
		if (srt.m_consts->m_showFlags & kShowAlpha) {
			alpha = textures->m_alphaMaskTx.SampleLevel(textureSampler, float2(textureUv.x,textureUv.y), 0).r;
			color.rgb = (alpha < .1) ? float3(.5,0,0) : float3(1,1,1);
		}
	}
	if (!inside) {
		color.r += .2;
	}
	
	// // DEBUG
	// //
	// // if (srt.m_consts->m_showFlags & kShowNormals) {
	// // 	color.rgb = color0.rgb;
	// // }
	// color.a = 1.0;
	// if (srt.m_consts->m_showFlags & kShowSubsurface) {
	// 	color.rgb = color0.rgb;
	// }
	// if (srt.m_consts->m_showFlags & kShowFoam) {
	// 	float d = color0.x;
	// 	color.rgb = (d < srt.m_consts->m_debug0) ? float3(1,0,0) : float3(0,1,0);
	// }
	// if (srt.m_consts->m_showFlags & kShowCheckerboard) {
	// 	color.rgb = isnan(color0.rgb) ? float3(1,0,0) : float3(0,1,0);
	// }
	
	return color;
}

struct WaterProperties {
	float4 c0 : SV_Target0;
	float4 c1 : SV_Target1;
	float4 c2 : SV_Target2;
	float4 c3 : SV_Target3;
	float4 c4 : SV_Target4;
};

// This shader is only for when we override the rendering of water properties
WaterProperties Ps_QuadTreeWaterProperties(VertexShaderOutput interp, SrtPsData srt : S_SRT_DATA)
{
	float3 worldPosition = interp.worldPosition; 
	float3 worldNormal   = interp.worldNormal;
	float  foam          = interp.waveFoamMask; 
	float  subsurface    = interp.waveSubsurfaceMask;
	float2 uv            = interp.uv0;
	float2 textureUv     = interp.uv1;

	WaterProperties OUT;

	TexturesAndSamplers   *textures = srt.m_texturesAndSamplers;
	SamplerState	       textureSampler = srt.m_texturesAndSamplers->g_linearClampSampler;
	float2 foamChurn = textures->m_foamChurnTx.SampleLevel(textureSampler, textureUv, 0);
	float2 uvOffset = srt.m_consts->m_waterPropertiesScaleOffset.xy;
	float  uvScale  = srt.m_consts->m_waterPropertiesScaleOffset.z;

	

	// we need to compute previous camera position
	float2 prevUv = 1.0 - (((worldPosition.xz - (srt.m_consts->m_cameraPrev.xz - uvScale.xx)) / (2.0 * uvScale.xx) ));

	// previous height
	// Decay and copy the height values from the previous camera position
	// and compare them to the current wave height
	// Make sure that values outside the previous camera (outside the texture), just initialize them to a very low height

	


	float  height   = textures->m_heightTx.SampleLevel(textureSampler, prevUv, 0);

	height = (prevUv.x >= 0 && prevUv.x <= 1 && prevUv.y >= 0 && prevUv.y <= 1) ? height : -1000;
	height -= (srt.m_consts->m_decayRate * srt.m_consts->m_delta);
	
	float heightDelta = max(height, worldPosition.y);

	OUT.c0 = 0;
	OUT.c1 = 0;
	OUT.c2 = 0;	
	OUT.c3 = 0;
	OUT.c3.xy = float2(foam, subsurface);
	OUT.c4 = 0;
	OUT.c4.x = heightDelta;

	return OUT;
}
