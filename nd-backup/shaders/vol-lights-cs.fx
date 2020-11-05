#include "global-funcs.fxi"

cbuffer VolLightsInfoBuffer : register( b0 )
{
	uint4					g_sampleInfo;
	float4					g_linearDepthParams;
	float4					g_viewSpaceParams;
	float4					g_lightPosition;
	float4					g_lightDirection;
	float4					g_lightColor;
	float4					g_shadowScreenViewParameter;
	float4					g_sampleRightVector;
	float4					g_sampleDirVector;
	float4					g_sampleAngleInfo;
	float4					g_spotShaftInfoParameter;
	float4					g_worldShadowMat[3];
};

#define kMaxNumCascaded		8

struct	VolSunlightGlobalParams
{
	float4					m_viewFrustumPlanes[4];
	float4					m_sunLightDir;
	float4					m_sampleRightVector;
	float4					m_sampleDirVector;
	float4					m_cascadedLevelDist[kMaxNumCascaded / 4];
	float4					m_sunShadowMat[kMaxNumCascaded * 5];
};

cbuffer VolSunlightGlobalInfoBuffer : register( b0 )
{
	VolSunlightGlobalParams		g_volLightGlobalParams;
};

struct	VolSunlightParams
{
	uint4					m_sunSampleInfo;
	float4					m_screenCoordInfo;
	float4					m_screenScaleOffset;
	float4					m_linearDepthParams;
	float4					m_sunSampleAngleInfo;
};

cbuffer VolSunlightInfoBuffer : register( b1 )
{
	VolSunlightParams		g_volLightParams;
};

struct	VolSunlightBlockParams
{
	uint4					m_blockInfo;
	float4					m_sampleAngleInfo;
	float4					m_clipPlanes[2];
	float4					m_clipDistance;
};

cbuffer VolSunlightBlockInfoBuffer : register( b1 )
{
	VolSunlightBlockParams		g_volLightBlockParams;
};

#define kNumMaxThreads	1024

Texture2D<float>					tDepthBuffer				: register(t0);
Texture2D<float>					tDepthBuffer1				: register(t1);
Texture2DArray<float>				tShadowInfoBuffer			: register(t0);
ByteAddressBuffer					tVolInfoBuffer				: register(t1);
RWByteAddressBuffer					uVolSampleInfoBuffer		: register(u0);
RWByteAddressBuffer					uVolSampleInfoBuffer1		: register(u1);
RWTexture2D<float4>					uVolSampleBuffer			: register(u0);
RWTexture2D<float4>					uVolLightAccuBuffer			: register(u0);

groupshared uint g_currentSampleIdx;

[numthreads(kNumMaxThreads, 1, 1)]
void CS_VolLightsCircleInfoCreate( uint groupThreadId : SV_GroupThreadId )
{
	float4 lightPosition = g_lightPosition;
	float4 lightDirection = g_lightDirection;
	float4 sampleRightVector = g_sampleRightVector;
	float4 sampleDirVector = g_sampleDirVector;
	float4 sampleAngleInfo = g_sampleAngleInfo;

	float sqrCosConeAngle = sampleAngleInfo.z;
	//float cosConeAngle = sampleAngleInfo.w;
	float invNumSampleRays = 1.0f / (float)g_sampleInfo.x;

	if (groupThreadId == 0)
		g_currentSampleIdx = 0;

	GroupMemoryBarrierWithGroupSync();

	uint curSampleIdx;
	InterlockedAdd(g_currentSampleIdx, 1u, curSampleIdx);

	while (curSampleIdx < g_sampleInfo.x)
	{
		float cosAngle, sinAngle;
		float sampleAngle = (curSampleIdx + 0.5f) * invNumSampleRays * sampleAngleInfo.x + sampleAngleInfo.y;
		sincos(sampleAngle, sinAngle, cosAngle);

		// sampleDir is a vector from light position.
		float3 sampleDir = normalize(cosAngle * sampleDirVector.xyz + sinAngle * sampleRightVector.xyz);
		float3 vecToLight = normalize(lightPosition.xyz);

		bool isPositiveDir = (dot(lightDirection, lightPosition) > 0.0);

		float3 lightToSample = isPositiveDir ? lightPosition.xyz : - lightPosition.xyz;

		// dot(|t * Sd - Lp|, Ld)  = cos(Alpha);
		float sampleDir_Dot_LightDir = dot(sampleDir, lightDirection.xyz);
		float lightToSample_Dot_LightDir = dot(lightToSample, lightDirection.xyz);
		float lightToSample_Dot_SampleDir = dot(lightToSample, sampleDir);
		float A = sampleDir_Dot_LightDir * sampleDir_Dot_LightDir - sqrCosConeAngle;
		float B = lightToSample_Dot_LightDir * sampleDir_Dot_LightDir - lightToSample_Dot_SampleDir * sqrCosConeAngle;
		float C = lightToSample_Dot_LightDir * lightToSample_Dot_LightDir - dot(lightToSample, lightToSample) * sqrCosConeAngle;
		float delta = B * B - A * C;
		float invA = 1.0f / A;

		float t0 = 0, t1 = 0;
		int discard_t0 = 1, discard_t1 = 1;

		if (delta > 0.0f)
		{
			float sqrtDelta = sqrt(delta);
			t0 = (-B + sqrtDelta) * invA;
			t1 = (-B - sqrtDelta) * invA;

			if (t0 > 0 || t1 > 0)
			{
				float tt0 = lightToSample_Dot_LightDir + t0 * sampleDir_Dot_LightDir;
				float tt1 = lightToSample_Dot_LightDir + t1 * sampleDir_Dot_LightDir;
			
				if (tt0 > 0 && t0 > 0)
				{
					discard_t0 = 0;
				}
				if (tt1 > 0 && t1 > 0)
				{
					discard_t1 = 0;
				}
			}
		}

		float2 validAngle = float2(0, 0); 

		if (discard_t0 < 0.5 && discard_t1 < 0.5)
		{
			float cosBeta0 = dot(normalize(t0 * sampleDir + lightToSample), vecToLight);
			float cosBeta1 = dot(normalize(t1 * sampleDir + lightToSample), vecToLight);

			if (cosBeta0 > cosBeta1)
				validAngle = float2(cosBeta0, cosBeta1);
			else
				validAngle = float2(cosBeta1, cosBeta0);
		}
		else if (discard_t0 < 0.5 || discard_t1 < 0.5) 
		{
			t0 = discard_t0 > 0.5 ? t1 : t0;

			float cosBeta0 = dot(normalize(t0 * sampleDir + lightToSample), vecToLight);

			if (isPositiveDir)
				validAngle = float2(1.0, cosBeta0);
			else
				validAngle = float2(cosBeta0, -1.0);
		}

		if (validAngle.x == 0.0 && validAngle.y == 0.0)
		{
			StoreAsFloat4(uVolSampleInfoBuffer, float4(0, 0, cosAngle, sinAngle), curSampleIdx * 16);
		}
		else
		{
			validAngle.x = acos(validAngle.x);
			validAngle.y = acos(validAngle.y);

			float invRange = 1.0 / (validAngle.y - validAngle.x);

			StoreAsFloat4(uVolSampleInfoBuffer, float4(validAngle.x * invRange, invRange, cosAngle, sinAngle), curSampleIdx * 16);
		}

		InterlockedAdd(g_currentSampleIdx, 1u, curSampleIdx);
	}
}

//=================================================================================================
[numthreads(kNumMaxThreads, 1, 1)]
void CS_VolLightsLinearInfoCreate( uint groupThreadId : SV_GroupThreadId )
{
	float4 lightPosition = g_lightPosition;
	float4 lightDirection = g_lightDirection;
	float4 sampleRightVector = g_sampleRightVector;
	float4 sampleDirVector = g_sampleDirVector;
	float4 sampleAngleInfo = g_sampleAngleInfo;

	float sqrCosConeAngle = sampleAngleInfo.z;
	float cosConeAngle = sampleAngleInfo.w;
	float invNumSampleRays = 1.0f / (float)g_sampleInfo.x;

	if (groupThreadId == 0)
		g_currentSampleIdx = 0;

	GroupMemoryBarrierWithGroupSync();

	uint curSampleIdx;
	InterlockedAdd(g_currentSampleIdx, 1u, curSampleIdx);

	while (curSampleIdx < g_sampleInfo.x)
	{
		float cosAngle, sinAngle;
		float sampleAngle = (curSampleIdx + 0.5f) * invNumSampleRays * sampleAngleInfo.x + sampleAngleInfo.y;
		sincos(sampleAngle, sinAngle, cosAngle);

		// sampleDir is a vector from light position.
		float3 sampleDir = normalize(cosAngle * sampleDirVector.xyz + sinAngle * sampleRightVector.xyz);
	
		// we using lightPos as sample dir reference vector "D", and sampleDir as right reference vector "R".
		// so, test vector T = D * cos(Beta) + R * sin(Beta);
		// <=> dot (T, lightDir) = cos(coneAngle);
		// <=> dot (D, lightDir) * cos(Beta) + dot (R, lightDir) * sin(Beta) = cosconeAngle;
		// let A = dot(D, lightDir), B = dot(R, lightDir), C = cosConeAngle;
		// <=> (sqr(A) + sqr(B)) * sqrCosBeta - 2 * A * C * cosBeta + (sqr(C) - sqr(B)) = 0.0f;
	
		float3 vecToLight = normalize(lightPosition.xyz);

		float dotDL = dot(vecToLight, lightDirection.xyz);
		float dotRL = dot(sampleDir, lightDirection.xyz);

		float sqrDotDL = dotDL * dotDL;
		float sqrDotRL = dotRL * dotRL;

		float A = sqrDotDL + sqrDotRL;
		float invA = 1.0f / A;

		float B = (- dotDL * cosConeAngle);
		float C = (sqrCosConeAngle - sqrDotRL);

		float delta = B * B - A * C;

		float2 validAngle = float2(0, 0);

		bool isPositiveDir = (dot(lightDirection, lightPosition) > 0.0);

		float testPassFlg = 0;

		if (dotRL == 0.0)
		{
			float invDotDL = 1.0 / dotDL;
			float cosBeta = clamp(cosConeAngle * invDotDL, -1.0, 1.0);

			if (isPositiveDir)
				validAngle = float2(1.0, cosBeta);
			else
				validAngle = float2(cosBeta, -1.0);
		}
		else if (delta > 0.0) // has intersection.
		{
			float invDotRL = 1.0 / dotRL;
			float sqrtDelta = sqrt(delta);
			float cosBeta0 = clamp((-B + sqrtDelta) * invA, -1.0, 1.0);
			float cosBeta1 = clamp((-B - sqrtDelta) * invA, -1.0, 1.0);
			float tempA = cosConeAngle * invDotRL;
			float tempB = dotDL * invDotRL;

			float sinBeta0 = saturate(tempA - cosBeta0 * tempB);
			float sinBeta1 = saturate(tempA - cosBeta1 * tempB); 

			if (sinBeta0 > 0 && sinBeta1 > 0)
			{
				if (cosBeta0 > cosBeta1)
					validAngle = float2(cosBeta0, cosBeta1);
				else
					validAngle = float2(cosBeta1, cosBeta0);
			}
			else if (sinBeta0 > 0)
			{
				if (isPositiveDir)
					validAngle = float2(1.0, cosBeta0);
				else
					validAngle = float2(cosBeta0, -1.0);
			}
			else if (sinBeta1 > 0)
			{
				if (isPositiveDir)
					validAngle = float2(1.0, cosBeta1);
				else
					validAngle = float2(cosBeta1, -1.0);
			}

			testPassFlg = 2;
		}
		else if (delta > -1e-7)
		{
			//float invDotRL = 1.0 / dotRL;
			float cosBeta = clamp(-B * invA, -1.0, 1.0);
			if (isPositiveDir)
				validAngle = float2(1.0, cosBeta);
			else
				validAngle = float2(cosBeta, -1.0);

			testPassFlg = 3;
		}

		if (validAngle.x == 0.0 && validAngle.y == 0.0)
		{
			StoreAsFloat4(uVolSampleInfoBuffer, float4(0, 0, cosAngle, sinAngle), curSampleIdx * 16);
		}
		else
		{
			validAngle.x = acos(validAngle.x);
			validAngle.y = acos(validAngle.y);
			float invRange = 1.0 / (validAngle.y - validAngle.x);
			StoreAsFloat4(uVolSampleInfoBuffer, float4(validAngle.x * invRange, invRange, cosAngle, sinAngle), curSampleIdx * 16);
		}

		InterlockedAdd(g_currentSampleIdx, 1u, curSampleIdx);
	}
}

float CalculateCircleShaftLength(float3 shadowBufferPos, float4 lightRayScaled, float3 lightPos, float3 dirVector, float2 shadowScreenViewParameter, float lightIdx)
{
	float	depthZ			= tShadowInfoBuffer.SampleLevel(g_sSamplerPoint, float3(shadowBufferPos.xy / shadowBufferPos.z, lightIdx), 0).r;
	float   shadowSpaceZ	= (float)(shadowScreenViewParameter.y / (depthZ - shadowScreenViewParameter.x));
	float3	rayDir			= normalize(lightPos + (shadowSpaceZ / lightRayScaled.w) * lightRayScaled.xyz);
	return					  dot(rayDir, dirVector);
}

[numthreads(kNumMaxThreads, 1, 1)]
void CS_VolLightsInfoCreate( uint groupThreadId : SV_GroupThreadId )
{

	if (groupThreadId == 0)
		g_currentSampleIdx = 0;

	GroupMemoryBarrierWithGroupSync();

	uint curSampleIdx;
	InterlockedAdd(g_currentSampleIdx, 1u, curSampleIdx);

	while (curSampleIdx < g_sampleInfo.x)
	{
		float4 coneIntersectInfo = LoadAsFloat4(tVolInfoBuffer, curSampleIdx * 16);

		float4 lightPosition = g_lightPosition;
		float4 lightDirection = g_lightDirection;
		float4 sampleRightVector = g_sampleRightVector;
		float4 sampleDirVector = g_sampleDirVector;
		float2 shadowScreenViewParameter = g_shadowScreenViewParameter.xy;

		if (coneIntersectInfo.x != 0 || coneIntersectInfo.y != 0)	
		{
			float3 vLightPos = lightPosition.xyz;
			float3 vLightDir = lightDirection.xyz;

			float3 sampleDir = normalize(coneIntersectInfo.z * sampleDirVector.xyz + coneIntersectInfo.w * sampleRightVector.xyz);
		
			float4 dirVector, rightVector;
			dirVector.xyz = normalize(vLightPos);
			rightVector.xyz = sampleDir;

			dirVector.w = dot(vLightDir, dirVector.xyz);
			rightVector.w = dot(vLightDir, rightVector.xyz);

			float3 shadowPosBase = float3(dot(g_worldShadowMat[0], float4(vLightPos, 1.0)),
										  dot(g_worldShadowMat[1], float4(vLightPos, 1.0)),
										  dot(g_worldShadowMat[2], float4(vLightPos, 1.0)));
		
			float3 shadowPosDir = float3(dot(g_worldShadowMat[0].xyz, dirVector.xyz),
										  dot(g_worldShadowMat[1].xyz, dirVector.xyz),
										  dot(g_worldShadowMat[2].xyz, dirVector.xyz));

			float3 shadowPosRight = float3(dot(g_worldShadowMat[0].xyz, rightVector.xyz),
										  dot(g_worldShadowMat[1].xyz, rightVector.xyz),
										  dot(g_worldShadowMat[2].xyz, rightVector.xyz));

			float invRange = 1.0 / coneIntersectInfo.y;
			float stepCosAngle = invRange / 256.0;
			float rotCosAngle = coneIntersectInfo.x * invRange + 0.5 * stepCosAngle;
			for (int iSample = 0; iSample < 64; iSample++) 
			{
				float4 blockSamples;
				float cosAngle, sinAngle;
				sincos(rotCosAngle, sinAngle, cosAngle);

				float4 lightRayScaled = cosAngle * dirVector + sinAngle * rightVector;
				float3 shadowBufferPos = shadowPosBase + cosAngle * shadowPosDir + sinAngle * shadowPosRight;
				blockSamples.x = CalculateCircleShaftLength(shadowBufferPos, lightRayScaled, vLightPos, dirVector.xyz, shadowScreenViewParameter, g_sampleInfo.y);
				rotCosAngle += stepCosAngle;

				sincos(rotCosAngle, sinAngle, cosAngle);

				lightRayScaled = cosAngle * dirVector + sinAngle * rightVector;
				shadowBufferPos = shadowPosBase + cosAngle * shadowPosDir + sinAngle * shadowPosRight;
				blockSamples.y = CalculateCircleShaftLength(shadowBufferPos, lightRayScaled, vLightPos, dirVector.xyz, shadowScreenViewParameter, g_sampleInfo.y);
				rotCosAngle += stepCosAngle;

				sincos(rotCosAngle, sinAngle, cosAngle);

				lightRayScaled = cosAngle * dirVector + sinAngle * rightVector;
				shadowBufferPos = shadowPosBase + cosAngle * shadowPosDir + sinAngle * shadowPosRight;
				blockSamples.z = CalculateCircleShaftLength(shadowBufferPos, lightRayScaled, vLightPos, dirVector.xyz, shadowScreenViewParameter, g_sampleInfo.y);
				rotCosAngle += stepCosAngle;

				sincos(rotCosAngle, sinAngle, cosAngle);

				lightRayScaled = cosAngle * dirVector + sinAngle * rightVector;
				shadowBufferPos = shadowPosBase + cosAngle * shadowPosDir + sinAngle * shadowPosRight;
				blockSamples.w = CalculateCircleShaftLength(shadowBufferPos, lightRayScaled, vLightPos, dirVector.xyz, shadowScreenViewParameter, g_sampleInfo.y);
				rotCosAngle += stepCosAngle;

				//StoreAsFloat4(uVolSampleInfoBuffer, blockSamples, curSampleIdx * 1024 + iSample * 16);
				uVolSampleBuffer[uint2(iSample, curSampleIdx)] = blockSamples;
			}
		}
		else
		{
			for (int iSample = 0; iSample < 64; iSample++) 
				uVolSampleBuffer[uint2(iSample, curSampleIdx)] = float4(0, 0, 0, 0);
		}

		InterlockedAdd(g_currentSampleIdx, 1u, curSampleIdx);
	}
}

void GetVolColorByGivenScreenCoord(in uint2 bufferOfs, float3 vLightPos, float3 vLightDir, float sqrLightRadius)
{
	float2 fBufferOfs			= float2(bufferOfs);

	float2	screenSpaceXY		= fBufferOfs.xy * g_viewSpaceParams.xy + g_viewSpaceParams.zw;
	float	viewSpaceZ			= 1.0f / (tDepthBuffer[bufferOfs].x * g_linearDepthParams.x + g_linearDepthParams.y);
	float3	vViewSpacePosition	= float3(screenSpaceXY, 1.0) * viewSpaceZ;

	float	viewDist = length(vViewSpacePosition);
	float3	viewDir = vViewSpacePosition / viewDist;
	
	float	sqrCosConeAngle = g_lightDirection.w;
	float	lightPos_Dot_LightDir = dot(vLightPos, vLightDir);
	float	lightPos_Dot_LightPos = dot(vLightPos, vLightPos);
	float	C = lightPos_Dot_LightDir * lightPos_Dot_LightDir - lightPos_Dot_LightPos * sqrCosConeAngle;

	float	rayDir_Dot_LightDir = dot(viewDir, vLightDir);
	float	lightPos_Dot_rayDir = dot(vLightPos, viewDir);

	float	A = rayDir_Dot_LightDir * rayDir_Dot_LightDir - sqrCosConeAngle;
	float	B = lightPos_Dot_rayDir * sqrCosConeAngle - rayDir_Dot_LightDir * lightPos_Dot_LightDir;

	float	delta = B * B - A * C;
	float	invA = 1.0f / A;

	float t0 = 0, t1 = 0;
	if (delta > 0)
	{
		float sqrt_delta = sqrt(delta);
		if (invA < 0)
		{
			t0 = (-B + sqrt_delta) * invA;
			t1 = (-B - sqrt_delta) * invA;
		}
		else
		{
			t0 = (-B - sqrt_delta) * invA;
			t1 = (-B + sqrt_delta) * invA;
		}	
		
		float tt0 = t0 * rayDir_Dot_LightDir - lightPos_Dot_LightDir;
		float tt1 = t1 * rayDir_Dot_LightDir - lightPos_Dot_LightDir;
		
		if (tt0 < 0 && tt1 < 0)
		{
			t0 = 0; t1 = 0;
		}
		else if (tt0 < 0)
		{
			if (vLightDir.z > 0)
			{
				t0 = t1; t1 = viewDist;
			}
			else
			{
				t0 = 0;
			}
		}
		else if (tt1 < 0)
		{
			if (vLightDir.z > 0)
			{
				t1 = viewDist;
			}
			else
			{
				t1 = t0; t0 = 0;
			}
		}
		else 
		{
			t1 = min(t1, viewDist);
		}
	
		float C1 = sqrLightRadius + lightPos_Dot_rayDir * lightPos_Dot_rayDir - lightPos_Dot_LightPos;
		if (C1 > 0)
		{
			float sqrtC1 = sqrt(C1);
			float ttt0 = lightPos_Dot_rayDir - sqrtC1;
			float ttt1 = lightPos_Dot_rayDir + sqrtC1;
			
			t0 = max(ttt0, t0);
			t1 = min(ttt1, t1);
		}
		else
		{
			t0 = 0; t1 = 0;
		}
	}

	uVolLightAccuBuffer[bufferOfs] = t0 - t1;
}

//=================================================================================================
[numthreads(kNumMaxThreads, 1, 1)]
void CS_DrawVolLightAccuBuffer(/* uint groupThreadId : SV_GroupThreadId */)
{
	uint2 bufferOfs = 0;
	GetVolColorByGivenScreenCoord(bufferOfs, g_lightPosition.xyz, g_lightDirection.xyz, g_lightPosition.w);
}

float GetSunlightVolValue(float3 samplePos, float cascadedLevel)
{
	int iCascadedLevel = (int)cascadedLevel;
	float4 vsSamplePos = float4(samplePos, 1.0);

	float3 shadowCoords;
	shadowCoords.x = dot(vsSamplePos, g_volLightGlobalParams.m_sunShadowMat[iCascadedLevel*5+0]);
	shadowCoords.y = dot(vsSamplePos, g_volLightGlobalParams.m_sunShadowMat[iCascadedLevel*5+1]);
	shadowCoords.z = cascadedLevel;

	float depthZ = tShadowInfoBuffer.SampleLevel(g_sSamplerPoint, shadowCoords, 0).r;

	float4 screenSpacePos = float4(shadowCoords.xy, depthZ, 1.0f);

	float3 shadowPos;
	shadowPos.x = dot(screenSpacePos, g_volLightGlobalParams.m_sunShadowMat[iCascadedLevel*5+2]);
	shadowPos.y = dot(screenSpacePos, g_volLightGlobalParams.m_sunShadowMat[iCascadedLevel*5+3]);
	shadowPos.z = dot(screenSpacePos, g_volLightGlobalParams.m_sunShadowMat[iCascadedLevel*5+4]);

	return dot(normalize(shadowPos), g_volLightGlobalParams.m_sunLightDir.xyz);
}

float GetSunlightVolValue(float3 samplePos)
{
	uint4 cmpResult0 = samplePos.z > g_volLightGlobalParams.m_cascadedLevelDist[0].xyzw;
	uint4 cmpResult1 = samplePos.z > g_volLightGlobalParams.m_cascadedLevelDist[1].xyzw;
	float cascadedLevel = min(dot((float4)cmpResult0, 1.0f) + dot((float4)cmpResult1, 1.0f), 7.0f);
	return GetSunlightVolValue(samplePos, cascadedLevel);
}

int GetMinMaxRayLine(out float3 finalRays[2], float3 cuttingPlane)
{
	float3 clipLines[4];

	clipLines[0] = cross(cuttingPlane, float3(-1.0f, 0.0f, g_volLightParams.m_screenCoordInfo.x));
	clipLines[1] = cross(cuttingPlane, float3(1.0f, 0.0f, g_volLightParams.m_screenCoordInfo.x));
	clipLines[2] = cross(cuttingPlane, float3(0.0f, -1.0f, g_volLightParams.m_screenCoordInfo.y));
	clipLines[3] = cross(cuttingPlane, float3(0.0f, 1.0f, g_volLightParams.m_screenCoordInfo.y));

#if 0
	float3 tmpFinalRays[4];

	float4 ZComponent = float4(clipLines[0].z, clipLines[1].z, clipLines[2].z, clipLines[3].z);
	float4 XYComponent = float4(clipLines[0].y, clipLines[1].y, clipLines[2].x, clipLines[3].x);
	float4 invZComponent = 1.0f / ZComponent;

	bool4 cmpResult = ZComponent != 0 && (abs(XYComponent * invZComponent) <= g_volLightParams.m_screenCoordInfo.yyxx * 1.001f);

	int numValidRays = (int)dot(cmpResult, 1.0f);
	int numClipLines = 0;

	if (numValidRays = 2)
	{
		if (cmpResult.x)
			finalRays[numClipLines++] = normalize(clipLines[0]);
		if (cmpResult.y)
			finalRays[numClipLines++] = normalize(clipLines[1]);
		if (cmpResult.z)
			finalRays[numClipLines++] = normalize(clipLines[2]);
		if (cmpResult.w)
			finalRays[numClipLines++] = normalize(clipLines[3]);
	}
	else if (numValidRays > 2)
	{
		int baseIdx = (cmpResult.x && cmpResult.y) ? 0 : 2;
		finalRays[0] = normalize(clipLines[baseIdx+0]);
		finalRays[1] = normalize(clipLines[baseIdx+1]);
		numClipLines = 2;
	}

	finalRays[0] = finalRays[0].z > 0.0f ? finalRays[0] : -finalRays[0];
	finalRays[1] = finalRays[1].z > 0.0f ? finalRays[1] : -finalRays[1];

	return numClipLines;
#else
	float4 normalizedLine[4];
	normalizedLine[0].xyz = normalize(clipLines[0].z > 0.0f? clipLines[0] : -clipLines[0]);
	normalizedLine[1].xyz = normalize(clipLines[1].z > 0.0f? clipLines[1] : -clipLines[1]);
	normalizedLine[2].xyz = normalize(clipLines[2].z > 0.0f? clipLines[2] : -clipLines[2]);
	normalizedLine[3].xyz = normalize(clipLines[3].z > 0.0f? clipLines[3] : -clipLines[3]);

	float3 averageVector = normalize(normalizedLine[0].xyz + normalizedLine[1].xyz + normalizedLine[2].xyz + normalizedLine[3].xyz);
	float3 angleReferencePlane = normalize(cross(cuttingPlane, averageVector));

	normalizedLine[0].w = dot(normalizedLine[0].xyz, angleReferencePlane);
	normalizedLine[1].w = dot(normalizedLine[1].xyz, angleReferencePlane);
	normalizedLine[2].w = dot(normalizedLine[2].xyz, angleReferencePlane);
	normalizedLine[3].w = dot(normalizedLine[3].xyz, angleReferencePlane);

			
	float4 minLine0 = normalizedLine[0];
	float4 maxLine0 = normalizedLine[1];
	if (normalizedLine[0].w > normalizedLine[1].w)
	{
		minLine0 = normalizedLine[1];
		maxLine0 = normalizedLine[0];
	}
			
	float4 minLine1 = normalizedLine[2];
	float4 maxLine1 = normalizedLine[3];
	if (normalizedLine[2].w > normalizedLine[3].w)
	{
		minLine1 = normalizedLine[3];
		maxLine1 = normalizedLine[2];
	}

	if (minLine1.w > maxLine0.w)
	{
		finalRays[0] = maxLine0.xyz;
		finalRays[1] = minLine1.xyz;
	}
	else if(minLine0.w > maxLine1.w)
	{
		finalRays[0] = maxLine1.xyz;
		finalRays[1] = minLine0.xyz;
	}
	else 
	{
		finalRays[0] = minLine0.w > minLine1.w ? minLine0.xyz : minLine1.xyz;
		finalRays[1] = maxLine0.w < maxLine1.w ? maxLine0.xyz : maxLine1.xyz;
	}

	return 2;
#endif
}

[numthreads(kNumMaxThreads, 1, 1)]
//=================================================================================================
void CS_VolSunLightInfoCreate( uint groupThreadId : SV_GroupThreadId )
{
	float invNumSampleRays = 1.0f / (float)g_volLightParams.m_sunSampleInfo.x;
	float invNumSamples = 1.0f / (float)g_volLightParams.m_sunSampleInfo.y;

	if (groupThreadId == 0)
		g_currentSampleIdx = 0;

	GroupMemoryBarrierWithGroupSync();

	uint curSampleIdx;
	InterlockedAdd(g_currentSampleIdx, 1u, curSampleIdx);

	while (curSampleIdx < g_volLightParams.m_sunSampleInfo.x)
	{
		float sampleAngle = (curSampleIdx + 0.5f) * invNumSampleRays * g_volLightParams.m_sunSampleAngleInfo.x + g_volLightParams.m_sunSampleAngleInfo.y;

		float cosAngle, sinAngle;
		sincos(sampleAngle, sinAngle, cosAngle);

		float3 sampleDir = sinAngle * g_volLightGlobalParams.m_sampleRightVector.xyz + cosAngle * g_volLightGlobalParams.m_sampleDirVector.xyz;

		// we have to clamp view direction, make sure it's within view frame of eye.
		float3 cuttingPlane = normalize(cross(g_volLightGlobalParams.m_sunLightDir.xyz, sampleDir));

		float3 finalRays[2];
		GetMinMaxRayLine(finalRays, cuttingPlane);

		float cosMinAngle = dot(normalize(finalRays[0].xyz), g_volLightGlobalParams.m_sunLightDir.xyz);
		float cosMaxAngle = dot(normalize(finalRays[1].xyz), g_volLightGlobalParams.m_sunLightDir.xyz);

		if (cosMinAngle > cosMaxAngle)
		{
			float tmpCosAngle = cosMinAngle;
			cosMinAngle = cosMaxAngle;
			cosMaxAngle = tmpCosAngle;
		}

		float cosAngleRangeScale = 1.0f / (cosMaxAngle - cosMinAngle);
		float cosAngleRnageOffset = -cosMinAngle * cosAngleRangeScale;

		float a = cosAngleRangeScale * (1.0f - kClampCap);
		float b = ((cosAngleRnageOffset - 0.5f) * (1.0f - kClampCap) + 0.5f);

		sampleDir = normalize(finalRays[0].xyz + finalRays[1].xyz);

		float maxShadowDist = g_volLightParams.m_screenCoordInfo.z;

		float maxSampleDist = maxShadowDist / sampleDir.z;

		float3 farestSamplePos = sampleDir * maxSampleDist;

		float4 baseSampleDist = float4(0.5 * invNumSamples, 1.5 * invNumSamples, 2.5 * invNumSamples, 3.5 * invNumSamples);

		for (uint iSample = 0; iSample < (g_volLightParams.m_sunSampleInfo.y >> 2); iSample++)
		{
			float4 blockSamples;

			float4 distanceRatio = CalculateSampleDistribute4(baseSampleDist);
			baseSampleDist += 4.0f * invNumSamples;

			blockSamples.x = GetSunlightVolValue(distanceRatio.x * farestSamplePos);

			blockSamples.y = GetSunlightVolValue(distanceRatio.y * farestSamplePos);

			blockSamples.z = GetSunlightVolValue(distanceRatio.z * farestSamplePos);

			blockSamples.w = GetSunlightVolValue(distanceRatio.w * farestSamplePos);

			uVolSampleBuffer[uint2(iSample, curSampleIdx)] = saturate(blockSamples * a + b);
		}

 		StoreAsFloat4(uVolSampleInfoBuffer1, float4(sampleDir, maxSampleDist), curSampleIdx * 32);
 		StoreAsFloat4(uVolSampleInfoBuffer1, float4(a, b, cosMinAngle, cosMaxAngle), curSampleIdx * 32 + 16);

		InterlockedAdd(g_currentSampleIdx, 1u, curSampleIdx);
	}
}

float PackScreenPos(float2 pos)
{
	float2 sampleRayPos = saturate(pos.xy * g_volLightParams.m_screenScaleOffset.xy + g_volLightParams.m_screenScaleOffset.zw) * 65535.0f;
	uint2  uSampleRayPos = (uint2)sampleRayPos;

	uint result = (uSampleRayPos.y << 16) | uSampleRayPos.x;
	return asfloat(result);
}

[numthreads(kNumMaxThreads, 1, 1)]
//=================================================================================================
void CS_VolSunLightCircleInfoCreate(uint groupThreadId : SV_GroupThreadId)
{
#if 0
	float invNumSampleRays = 1.0f / (float)g_volLightParams.m_sunSampleInfo.x;
	float fNumSamples = (float)g_volLightParams.m_sunSampleInfo.y;
	//float invNumSamples = 1.0f / fNumSamples;

	if (groupThreadId == 0)
	{
		g_currentSampleIdx = 0;
	}

	GroupMemoryBarrierWithGroupSync();

	uint curSampleIdx;
	InterlockedAdd(g_currentSampleIdx, 1u, curSampleIdx);

	while (curSampleIdx < g_volLightParams.m_sunSampleInfo.x)
	{
		float sampleAngle = (curSampleIdx + 0.5f) * invNumSampleRays * g_volLightParams.m_sunSampleAngleInfo.x + g_volLightParams.m_sunSampleAngleInfo.y;

		float cosAngle, sinAngle;
		sincos(sampleAngle, sinAngle, cosAngle);

		float3 sampleDir = sinAngle * g_volLightGlobalParams.m_sampleRightVector.xyz + cosAngle * g_volLightGlobalParams.m_sampleDirVector.xyz;

		// we have to clamp view direction, make sure it's within view frame of eye.
		float3 cuttingPlane = normalize(cross(g_volLightGlobalParams.m_sunLightDir.xyz, sampleDir));

		float3 finalRays[2];
		GetMinMaxRayLine(finalRays, cuttingPlane);

		float2 refRayAngle;
		refRayAngle.x = dot(g_volLightGlobalParams.m_sunLightDir.xyz, finalRays[0].xyz);
		refRayAngle.y = dot(g_volLightGlobalParams.m_sunLightDir.xyz, finalRays[1].xyz);

		float2 sinAngle0 = 1.0f - sqrt(max(1.0f - refRayAngle * refRayAngle, 0.0f));
		refRayAngle = refRayAngle > 0 ? sinAngle0 : -sinAngle0;

		float fMinAngle, fMaxAngle;
		if (g_volLightGlobalParams.m_sunLightDir.z > 0.0f)
		{
			fMinAngle = min(refRayAngle.x, refRayAngle.y);
			fMaxAngle = 1.0f;
		}
		else
		{
			fMinAngle = -1.0f;
			fMaxAngle = max(refRayAngle.x, refRayAngle.y);
		}

		float fAngleRangeScale = 1.0f / (fMaxAngle - fMinAngle);
		float fAngleRnageOffset = -fMinAngle * fAngleRangeScale;

		float a = fAngleRangeScale * (1.0f - kClampCap);
		float b = ((fAngleRnageOffset - 0.5f) * (1.0f - kClampCap) + 0.5f) - 1.0f / 65536.0f;

		float3 sampleRayEnd[2];

		float3 normZRay[2];
		normZRay[0] = finalRays[0].xyz / finalRays[0].z;
		normZRay[1] = finalRays[1].xyz / finalRays[1].z;

		float maxShadowDist = g_volLightParams.m_screenCoordInfo.z;
		sampleRayEnd[0] = normZRay[0] * maxShadowDist;
		sampleRayEnd[1] = normZRay[1] * maxShadowDist;

		float normRayDotN[2];
		normRayDotN[0] = dot(normZRay[0], sampleDir);
		normRayDotN[1] = dot(normZRay[1], sampleDir);

		float t0 = normRayDotN[0] * maxShadowDist;
		float t1 = normRayDotN[1] * maxShadowDist;

		float t = saturate(-t0 / (t1 - t0));

		float3 splitSamplePos = t * (sampleRayEnd[1] - sampleRayEnd[0]) + sampleRayEnd[0];

		int splitNumberBy4 = t > 0.5f ? int(t * fNumSamples / 4.0f) : (int(t * fNumSamples / 4.0f) + 1);

		int numSamples = int(splitNumberBy4 * 4.0f);
		float sampleDistRatio = numSamples / maxShadowDist;
		float4 rayT[2];
		rayT[0] = g_volLightGlobalParams.m_cascadedLevelDist[0] * sampleDistRatio;
		rayT[1] = g_volLightGlobalParams.m_cascadedLevelDist[1] * sampleDistRatio;

		float3 stepSampleRay = (sampleRayEnd[0] - splitSamplePos) / numSamples;
		float3 startSamplePos = splitSamplePos + stepSampleRay;
		int iSample;
		for (iSample = 0; iSample < numSamples; iSample += 4)
		{
			float4 fSampleIdx = iSample + float4(0, 1, 2, 3);

			uint4 levelIdx;
			levelIdx.x = dot((uint4)(fSampleIdx.x > rayT[0]), 1u) + dot((uint4)(fSampleIdx.x > rayT[1]), 1u);
			levelIdx.y = dot((uint4)(fSampleIdx.y > rayT[0]), 1u) + dot((uint4)(fSampleIdx.y > rayT[1]), 1u);
			levelIdx.z = dot((uint4)(fSampleIdx.z > rayT[0]), 1u) + dot((uint4)(fSampleIdx.z > rayT[1]), 1u);
			levelIdx.w = dot((uint4)(fSampleIdx.w > rayT[0]), 1u) + dot((uint4)(fSampleIdx.w > rayT[1]), 1u);

			float4 blockSamples;
			blockSamples.x = GetSunlightVolValue(startSamplePos, min(levelIdx.x, g_volLightParams.m_sunSampleInfo.z));
			startSamplePos += stepSampleRay;

			blockSamples.y = GetSunlightVolValue(startSamplePos, min(levelIdx.y, g_volLightParams.m_sunSampleInfo.z));
			startSamplePos += stepSampleRay;

			blockSamples.z = GetSunlightVolValue(startSamplePos, min(levelIdx.z, g_volLightParams.m_sunSampleInfo.z));
			startSamplePos += stepSampleRay;

			blockSamples.w = GetSunlightVolValue(startSamplePos, min(levelIdx.w, g_volLightParams.m_sunSampleInfo.z));
			startSamplePos += stepSampleRay;

			float4 newSinAngle = 1.0f - sqrt(max(1.0f - blockSamples * blockSamples, 0.0f));
			newSinAngle = blockSamples > 0 ? newSinAngle : -newSinAngle;

			uVolSampleBuffer[uint2(iSample >> 2, curSampleIdx)] = newSinAngle * a + b;
		}

		int numSamples1 = g_volLightParams.m_sunSampleInfo.y - numSamples;
		sampleDistRatio = numSamples1 / maxShadowDist;
		rayT[0] = g_volLightGlobalParams.m_cascadedLevelDist[0] * sampleDistRatio;
		rayT[1] = g_volLightGlobalParams.m_cascadedLevelDist[1] * sampleDistRatio;
		stepSampleRay = (sampleRayEnd[1] - splitSamplePos) / numSamples1;
		startSamplePos = splitSamplePos + stepSampleRay;
		for (iSample = numSamples; iSample < int(g_volLightParams.m_sunSampleInfo.y); iSample += 4)
		{
			float4 fSampleIdx = iSample - numSamples + float4(0, 1, 2, 3);

			uint4 levelIdx;
			levelIdx.x = dot((uint4)(fSampleIdx.x > rayT[0]), 1u) + dot((uint4)(fSampleIdx.x > rayT[1]), 1u);
			levelIdx.y = dot((uint4)(fSampleIdx.y > rayT[0]), 1u) + dot((uint4)(fSampleIdx.y > rayT[1]), 1u);
			levelIdx.z = dot((uint4)(fSampleIdx.z > rayT[0]), 1u) + dot((uint4)(fSampleIdx.z > rayT[1]), 1u);
			levelIdx.w = dot((uint4)(fSampleIdx.w > rayT[0]), 1u) + dot((uint4)(fSampleIdx.w > rayT[1]), 1u);

			float4 blockSamples;
			blockSamples.x = GetSunlightVolValue(startSamplePos, min(levelIdx.x, g_volLightParams.m_sunSampleInfo.z));
			startSamplePos += stepSampleRay;

			blockSamples.y = GetSunlightVolValue(startSamplePos, min(levelIdx.y, g_volLightParams.m_sunSampleInfo.z));
			startSamplePos += stepSampleRay;

			blockSamples.z = GetSunlightVolValue(startSamplePos, min(levelIdx.z, g_volLightParams.m_sunSampleInfo.z));
			startSamplePos += stepSampleRay;

			blockSamples.w = GetSunlightVolValue(startSamplePos, min(levelIdx.w, g_volLightParams.m_sunSampleInfo.z));
			startSamplePos += stepSampleRay;

			float4 newSinAngle = 1.0f - sqrt(max(1.0f - blockSamples * blockSamples, 0.0f));
			newSinAngle = blockSamples > 0 ? newSinAngle : -newSinAngle;

			uVolSampleBuffer[uint2(iSample >> 2, curSampleIdx)] = newSinAngle * a + b;
		}

		float2 packRayEnd;
		packRayEnd.x = PackScreenPos(normZRay[0].xy);
		packRayEnd.y = PackScreenPos(normZRay[1].xy);
		float packRayCenter = PackScreenPos(splitSamplePos.xy / splitSamplePos.z);

		float3 shadowCoords;
		shadowCoords.x = g_volLightGlobalParams.m_sunShadowMat[0].w;
		shadowCoords.y = g_volLightGlobalParams.m_sunShadowMat[1].w;
		shadowCoords.z = 0;
	
		float depthZ0 = tShadowInfoBuffer.SampleLevel(g_sSamplerPoint, shadowCoords+float3(-1/2048.0f, -1/2048.0f, 0), 0).r;
		float depthZ1 = tShadowInfoBuffer.SampleLevel(g_sSamplerPoint, shadowCoords+float3(1/2048.0f, -1/2048.0f, 0), 0).r;
		float depthZ2 = tShadowInfoBuffer.SampleLevel(g_sSamplerPoint, shadowCoords+float3(-1/2048.0f, 1/2048.0f, 0), 0).r;
		float depthZ3 = tShadowInfoBuffer.SampleLevel(g_sSamplerPoint, shadowCoords+float3(1/2048.0f, 1/2048.0f, 0), 0).r;

		float maxDepthZ = max(max(depthZ0, depthZ1), max(depthZ2, depthZ3));
		float minDepthZ = min(min(depthZ0, depthZ1), min(depthZ2, depthZ3));
		float depthZ = g_volLightGlobalParams.m_sunLightDir.z > 0.0f ? maxDepthZ : minDepthZ;
		float4 screenSpacePos = float4(shadowCoords.xy, depthZ, 1.0f);
		float vsBlockerDist = dot(screenSpacePos, g_volLightGlobalParams.m_sunShadowMat[4]);

		float2 screenUv = float2(g_volLightGlobalParams.m_sunLightDir.xy / (g_volLightGlobalParams.m_sunLightDir.z * g_volLightParams.m_screenCoordInfo.xy) * -0.5f + 0.5f);
		float centerDepthZ = tDepthBuffer1.SampleLevel(g_sSamplerPoint, screenUv, 0);
		float centerViewSpaceZ = 1.0f / (centerDepthZ * g_volLightParams.m_linearDepthParams.x + g_volLightParams.m_linearDepthParams.y);

		bool bIsVisible = g_volLightGlobalParams.m_sunLightDir.z > 0.0f ? (centerViewSpaceZ < vsBlockerDist * g_volLightParams.m_sunSampleInfo.w) : (centerViewSpaceZ * g_volLightParams.m_sunSampleInfo.w > vsBlockerDist);

		uint mergeBits = splitNumberBy4 | ((bIsVisible ? 1 : 0) << 15);

		StoreAsFloat4(uVolSampleInfoBuffer1, float4(packRayEnd, packRayCenter, asfloat(mergeBits)), curSampleIdx * 32);
		StoreAsFloat4(uVolSampleInfoBuffer1, float4(a, b, normRayDotN[0], normRayDotN[1]), curSampleIdx * 32 + 16);

		InterlockedAdd(g_currentSampleIdx, 1u, curSampleIdx);
	}
#endif
}

//=================================================================================================
[numthreads(kNumMaxThreads, 1, 1)]
void CS_VolSunLightBlockSampleCreate(uint groupThreadId : SV_GroupThreadId)
{
	if (groupThreadId.x < g_volLightBlockParams.m_blockInfo.x)
	{
		float3 viewFrustumPlanes[4];
		viewFrustumPlanes[0] = g_volLightGlobalParams.m_viewFrustumPlanes[0].xyz;
		viewFrustumPlanes[1] = g_volLightGlobalParams.m_viewFrustumPlanes[1].xyz;
		viewFrustumPlanes[2] = g_volLightGlobalParams.m_viewFrustumPlanes[2].xyz;
		viewFrustumPlanes[3] = g_volLightGlobalParams.m_viewFrustumPlanes[3].xyz;
		float4 sampleAngleInfo = g_volLightBlockParams.m_sampleAngleInfo;
		float4 sunLightDir = g_volLightGlobalParams.m_sunLightDir;
		float3 sampleRightVector = g_volLightGlobalParams.m_sampleRightVector.xyz;
		float3 sampleDirVector = g_volLightGlobalParams.m_sampleDirVector.xyz;
		float3 shadowClipPlanes[2];
		shadowClipPlanes[0] = g_volLightBlockParams.m_clipPlanes[0].xyz;
		shadowClipPlanes[1] = g_volLightBlockParams.m_clipPlanes[1].xyz;
		float4 shadowClipDist = float4(g_volLightBlockParams.m_clipPlanes[0].w, g_volLightBlockParams.m_clipPlanes[1].w, g_volLightBlockParams.m_clipDistance.xy);

		float sampleLineIndex = (float)groupThreadId.x / (float)g_volLightBlockParams.m_blockInfo.x;
		float sampleAngle = sampleLineIndex * sampleAngleInfo.z + sampleAngleInfo.w;

		float cosAngle, sinAngle;
		sincos(sampleAngle, sinAngle, cosAngle);
	
		float3 sampleDir = sinAngle * sampleRightVector + cosAngle * sampleDirVector;

		// we have to clamp view direction, make sure it's within view frame of eye.
		float3 cuttingPlane = normalize(cross(sunLightDir.xyz, sampleDir));

		float3 clipLine[4];
		clipLine[0] = normalize(cross(cuttingPlane, viewFrustumPlanes[0]));
		clipLine[1] = normalize(cross(cuttingPlane, viewFrustumPlanes[1]));
		clipLine[2] = normalize(cross(cuttingPlane, viewFrustumPlanes[2]));
		clipLine[3] = normalize(cross(cuttingPlane, viewFrustumPlanes[3]));

		if (clipLine[0].z < 0) clipLine[0] = -clipLine[0];
		if (clipLine[1].z < 0) clipLine[1] = -clipLine[1];
		if (clipLine[2].z < 0) clipLine[2] = -clipLine[2];
		if (clipLine[3].z < 0) clipLine[3] = -clipLine[3];

		// camera not align sunlight dirction.
		if (sampleAngleInfo.x == 0.0)
		{
			float3 normal0_1 = cross(clipLine[0], clipLine[1]);
			float3 normal2_3 = cross(clipLine[2], clipLine[3]);

			// flip 2, 3 if they are not in the same direction as 0, 1.
			if (dot(normal0_1, normal2_3) < 0)
			{
				float3 tempVector = clipLine[2];
				clipLine[2] = clipLine[3];
				clipLine[3] = tempVector;
			}

			float3 normal2_0 = cross(clipLine[2], clipLine[0]);
			float3 normal1_3 = cross(clipLine[1], clipLine[3]);

			float  dot2_1 = dot(normal2_0, normal0_1);
			float  dot0_3 = dot(normal0_1, normal1_3);

			float3 minVec = (dot2_1 > 0) ? clipLine[0] : clipLine[2];
			float3 maxVec = (dot0_3 > 0) ? clipLine[1] : clipLine[3];

			float3 refDir = normalize(cross(cuttingPlane, sunLightDir.xyz));

			float3 newSampleDir;
			if (dot(cross(minVec, refDir), cross(refDir, maxVec)) > 0.0)
				newSampleDir = refDir;
			else
				newSampleDir = normalize(maxVec + minVec);

			sampleDir = dot(newSampleDir, sampleDir) > 0.0 ? newSampleDir : -newSampleDir;
		}
		else
		{
			float3 rayDir[4];
			float refAngle = abs(dot(sunLightDir.xyz, sampleDir));
			float3 newSampleDir = sampleDir;
			for (int iRay = 0; iRay < 4; iRay++)
			{
				rayDir[iRay] = cross(sunLightDir.xyz, clipLine[iRay]);
				float curAngle = abs(dot(sunLightDir.xyz, clipLine[iRay]));
				if (dot(rayDir[iRay], cuttingPlane) > 0 && curAngle > refAngle)
				{
					newSampleDir = clipLine[iRay];
					refAngle = curAngle;
				}
			}	

			sampleDir = normalize(sunLightDir.xyz + newSampleDir * 4.0);
		}

		float dotDir1 = dot(shadowClipPlanes[0].xyz, sampleDir);
		float dotDir2 = dot(shadowClipPlanes[1].xyz, sampleDir);
	
		float rayNearDist, rayFarDist;
		if (dotDir1 > 0.0)
		{
			rayNearDist = -shadowClipDist.x / dotDir1;
			rayFarDist = shadowClipDist.z / dotDir1; 
		}
		else
		{
			rayNearDist = shadowClipDist.z / dotDir1;
			rayFarDist = -shadowClipDist.x / dotDir1; 
		}
	
		if (dotDir2 > 0.0)
		{
			rayNearDist = max(rayNearDist, -shadowClipDist.y / dotDir2);
			rayFarDist = min(rayFarDist, shadowClipDist.w / dotDir2);
		}
		else
		{
			rayNearDist = max(rayNearDist, shadowClipDist.w / dotDir2);
			rayFarDist = min(rayFarDist, -shadowClipDist.y / dotDir2);
		}
	
		rayNearDist = max(rayNearDist, 0.0);
		rayFarDist = max(rayFarDist, 0.0);	
	
		float3 sampleStep = sampleDir * (rayFarDist - rayNearDist) / 64.0f;
		float3 samplePos = sampleDir * rayNearDist + sampleStep * 0.5f;
	
		for (int iSample = 0; iSample < 16; iSample++) 
		{
			float4 result;
			result.x = GetSunlightVolValue(samplePos);
			samplePos += sampleStep;
			result.y = GetSunlightVolValue(samplePos);
			samplePos += sampleStep;
			result.z = GetSunlightVolValue(samplePos);
			samplePos += sampleStep;
			result.w = GetSunlightVolValue(samplePos);
			samplePos += sampleStep;

			uVolSampleBuffer[uint2(iSample, groupThreadId.x)] = result;
		}
	}
}
