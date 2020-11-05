#include "global-funcs.fxi"
#include "snoise.fxi"
#include "post-processing-common.fxi"
#include "../include/deferred-include.fxi"
#include "../include/tetra-shadows.fxi"
#include "under-water-fog.fxi"
#include "texture-table.fxi"
#define kMaxNumCascaded		8

struct  VolSpotLightParams
{
	float4					m_positionVS;
	float4					m_directionVS;
	float4					m_sampleRightVS;
	float4					m_sampleDirVS;
	float4					m_sampleAngleInfo;
	float2					m_maskTextureShift;
	float					m_sampleDirVectorNear;
	uint					m_isVolFlashLight;
	uint					m_numSlices;
	uint					m_numSamplesPerLine;
	float					m_localShadowTextureIdx;
	float					m_refAngleClamp;
	float4					m_shadowScreenViewParams;
	float4					m_invViewSpaceParams;
	float4					m_worldShadowMat[3];
	float4					m_viewFrustumPlanes[4];
};

struct  VolDirectLightParams
{
	float4					m_screenSize;
	int4					m_sizeParams;
	float4					m_viewSpaceXyParams;
	float4					m_linearDepthParams;
	float4					m_directionVS;
	float4					m_sampleRightVS;
	float4					m_sampleDirVS;
	float4					m_sampleAngleInfo;
	float4					m_color;
	float4					m_params;
	float4					m_shadowScreenViewParams;
	float4					m_invViewSpaceParams;
	float4					m_boxVertex[8];
	float4					m_viewFrustumPlanes[4];
	float4					m_clipPlanes[3];
	float4					m_directionWS;
	float4					m_waterSurfaceInfo;
	float4					m_viewToWorldMat[3];
	float4					m_clipDistance;
	float					m_cascadedLevelDist[kMaxNumCascaded];
//	float					m_shadowZScale[kMaxNumCascaded];
	float4					m_shadowMat[kMaxNumCascaded * 5];
};

struct PointSamplers
{
	SamplerState samplerPoint;
};

struct PointLinearSamplers
{
	SamplerState samplerPoint;
	SamplerState samplerLinear;
};

struct PointLinearrepeatSamplers
{
	SamplerState samplerPoint;
	SamplerState samplerLinearRepeat;
};

#define kNumMaxSamplesPerLine			512
#define kNumMaxSlices					512

void CreateScissorPolygon(inout uint numVertex, inout uint numIndex, inout float4 vertexList[8], inout uint indexList, uint idx0, uint idx1)
{
	if (vertexList[idx0].w > 0.0f)
	{
		indexList |= (idx0 << numIndex);
		numIndex += 3;
	}
	if (vertexList[idx0].w * vertexList[idx1].w < 0.0f)
	{
		vertexList[numVertex] = vertexList[idx0] + (vertexList[idx1] - vertexList[idx0]) * (0 - vertexList[idx0].w) / (vertexList[idx1].w - vertexList[idx0].w);
		indexList |= (numVertex << numIndex);

		numVertex ++;
		numIndex += 3;
	}
}

void CreateScissorPolygon6(inout uint numVertex, inout uint numIndex, inout float4 vertexList[6], inout uint indexList, uint idx0, uint idx1)
{
	if (vertexList[idx0].w > 0.0f)
	{
		indexList |= (idx0 << numIndex);
		numIndex += 3;
	}
	if (vertexList[idx0].w * vertexList[idx1].w < 0.0f)
	{
		vertexList[numVertex] = vertexList[idx0] + (vertexList[idx1] - vertexList[idx0]) * (0 - vertexList[idx0].w) / (vertexList[idx1].w - vertexList[idx0].w);
		indexList |= (numVertex << numIndex);

		numVertex ++;
		numIndex += 3;
	}
}

float3 LerpColorShift(float intensity, float3 lowerColor, float3 maxColor)
{
	float3 saturatedLowerColor = saturate(lowerColor);
	float lowerIntensity = dot(saturatedLowerColor, 0.33f);
	float3 finalColor;
	if (intensity > lowerIntensity)
		finalColor = lerp(lowerColor, maxColor, (intensity - lowerIntensity) / (1.0f - lowerIntensity));
	else
		finalColor = lerp(0, lowerColor, intensity / lowerIntensity);

	return finalColor;
}

void CalculateMinMaxCosAngle(inout float minCosAngle, 
							 inout float maxCosAngle, 
							 inout float minViewCosAngle, 
							 inout float maxViewCosAngle, 
							 float3 vertex, 
							 float3 lightPosition,
							 float3 dirVector,
							 float3 viewDirVector)
{
	float3 vertexToLight = vertex - lightPosition.xyz;
	float lengthToLight = length(vertexToLight);
	if (lengthToLight > 0.01f)
	{
		float cosLightAngle = dot(dirVector, vertexToLight) / lengthToLight;
		minCosAngle = min(minCosAngle, cosLightAngle);
		maxCosAngle = max(maxCosAngle, cosLightAngle);
	}

	float lengthToView = length(vertex);
	if (lengthToView > 0.01f)
	{
		float cosViewAngle = dot(viewDirVector, vertex) / lengthToView;
		minViewCosAngle = min(minViewCosAngle, cosViewAngle);
		maxViewCosAngle = max(maxViewCosAngle, cosViewAngle);
	}
}

void CalculateMinMaxCosAngle(inout float minViewCosAngle, 
							 inout float maxViewCosAngle, 
							 float3 vertex, 
							 float3 viewDirVector)
{
	float lengthToView = length(vertex);
	if (lengthToView > 0.01f)
	{
		float cosViewAngle = dot(viewDirVector, vertex) / lengthToView;
		minViewCosAngle = min(minViewCosAngle, cosViewAngle);
		maxViewCosAngle = max(maxViewCosAngle, cosViewAngle);
	}
}

void FindFrustumClipEdge(out float2 refRayMinY, out float2 refRayMaxY, float3 clipPlane, float4 viewFrustumPlanes[4])
{
	float3 clipRay0 = cross(clipPlane, viewFrustumPlanes[0]);
	float3 clipRay1 = cross(clipPlane, viewFrustumPlanes[1]);
	float3 clipRay2 = cross(clipPlane, viewFrustumPlanes[2]);
	float3 clipRay3 = cross(clipPlane, viewFrustumPlanes[3]);

	float2 refRayXP = clipRay0.xy / clipRay0.z;
	float2 refRayXN = clipRay1.xy / clipRay1.z;
	float2 refRayYP = clipRay2.xy / clipRay2.z;
	float2 refRayYN	= clipRay3.xy / clipRay3.z;

	refRayMinY = refRayXN;
	refRayMaxY = refRayXP;

	if (refRayMinY.y > refRayMaxY.y)
	{
		refRayMinY = refRayXP;
		refRayMaxY = refRayXN;
	}

	if (refRayYP.y > refRayMinY.y && refRayYP.y < refRayMaxY.y)
	{
		refRayMaxY = refRayYP;
	}

	if (refRayYN.y > refRayMinY.y && refRayYN.y < refRayMaxY.y)
	{
		refRayMinY = refRayYN;
	}
}

struct VolSpotLightInfoTextures
{
	RWTexture2D<float4>				rwb_info_buf;
};

struct VolSpotLightInfoSrt
{
	VolSpotLightParams				*consts;
	VolSpotLightInfoTextures		*texs;
};

//=================================================================================================
[numthreads(kNumMaxSlices, 1, 1)]
void CS_VolSpotLightLinearInfoCreateOpt(uint3 dispatchThreadId : SV_DispatchThreadID, VolSpotLightInfoSrt srt : S_SRT_DATA)
{
	float4 lightPosition = srt.consts->m_positionVS;
	float4 lightDirection = srt.consts->m_directionVS;
	float4 sampleRightVector = srt.consts->m_sampleRightVS;
	float4 sampleDirVector = srt.consts->m_sampleDirVS;
	float radius = lightPosition.w;

	float sqrCosConeAngle = srt.consts->m_sampleAngleInfo.z;
	float cosConeAngle = srt.consts->m_sampleAngleInfo.w;

	if (dispatchThreadId.x < srt.consts->m_numSlices)
	{
		float cosAngle, sinAngle;
		float sampleAngle = (dispatchThreadId.x + 0.5f) * srt.consts->m_sampleAngleInfo.x + srt.consts->m_sampleAngleInfo.y;
		sincos(sampleAngle, sinAngle, cosAngle);

		// sampleDir is a vector from light position.
		float3 sampleDir = normalize(cosAngle * sampleDirVector.xyz + sinAngle * sampleRightVector.xyz);
	
		float3 vecToLight = normalize(-lightPosition.xyz);

		float3 clipPlane = normalize(cross(vecToLight, sampleDir));

		float2 refRayMinY, refRayMaxY;
		FindFrustumClipEdge(refRayMinY, refRayMaxY, clipPlane, srt.consts->m_viewFrustumPlanes);

		float3 normalRefRay0 = normalize(float3(refRayMinY, 1.0f));
		float3 normalRefRay1 = normalize(float3(refRayMaxY, 1.0f));

		float angle0 = acos(clamp(dot(vecToLight, normalRefRay0), -1.0f, 1.0f));
		float angle1 = acos(clamp(dot(vecToLight, normalRefRay1), -1.0f, 1.0f));
		if (dot(sampleDir, normalRefRay0) < 0.0f)
			angle0 = -angle0;
		if (dot(sampleDir, normalRefRay1) < 0.0f)
			angle1 = -angle1;

		if (abs(angle0 - angle1) > 3.1415926f) // ok, wrong direction.
		{
			if (angle0 > angle1)
				angle1 += 2.0f * 3.1415926f;
			else
				angle0 += 2.0f * 3.1415926f;
		}

		if (angle0 > angle1)
		{
			float3 tempRefRay = normalRefRay1;
			normalRefRay1 = normalRefRay0;
			normalRefRay0 = tempRefRay;
		}

		float3 centerRefPos = normalRefRay0 + normalRefRay1;

		float3 viewRayClipPlane0 = cross(normalRefRay0, clipPlane);
		float3 viewRayClipPlane1 = cross(normalRefRay1, clipPlane);

		if (dot(viewRayClipPlane0, centerRefPos) < 0.0)
			viewRayClipPlane0 = -viewRayClipPlane0;

		if (dot(viewRayClipPlane1, centerRefPos) < 0.0)
			viewRayClipPlane1 = -viewRayClipPlane1;

		viewRayClipPlane0 = normalize(viewRayClipPlane0);
		viewRayClipPlane1 = normalize(viewRayClipPlane1);

		// we using lightPos as direction vector as "D", and sampleDir as right vector "R".
		// so, test vector T = D * cos(Beta) + R * sin(Beta);
		// <=> dot (T, lightDir) = cos(coneAngle);
		// <=> dot (D, lightDir) * cos(Beta) + dot (R, lightDir) * sin(Beta) = cosconeAngle;
		// let A = dot(D, lightDir), B = dot(R, lightDir), C = cosConeAngle;
		// <=> (sqr(A) + sqr(B)) * sqrCosBeta - 2 * A * C * cosBeta + (sqr(C) - sqr(B)) = 0.0f;

		float dotDL = dot(vecToLight, lightDirection.xyz);
		float dotRL = dot(sampleDir, lightDirection.xyz);

		float sqrDotDL = dotDL * dotDL;
		float sqrDotRL = dotRL * dotRL;

		float A = sqrDotDL + sqrDotRL;
		float invA = 1.0f / A;

		float B = (- dotDL * cosConeAngle);
		//float C = (sqrCosConeAngle - sqrDotRL);

		float delta = A - sqrCosConeAngle;

		float3 dirVector = 0;
		float3 rightVector = 0;
		float3 viewDirVector = 0;
		float3 viewRightVector = 0;

		float minCosAngle = 1.0f, maxCosAngle = -1.0f;
		float minViewCosAngle = 1.0f, maxViewCosAngle = -1.0f;

		float sinBeta0 = 0, sinBeta1 = 0, cosBeta0 = 0, cosBeta1 = 0;
		if (abs(dotRL) < 0.00005f)
		{
			float invDotDL = 1.0 / dotDL;
			cosBeta0 = cosConeAngle * invDotDL;
			cosBeta1 = cosBeta0;
			sinBeta0 = sqrt(saturate(1.0f - cosBeta0 * cosBeta0));
			sinBeta1 = -sinBeta0;
		}
		else if (delta > 0.0) // has intersection.
		{
			float invDotRL = 1.0 / dotRL;
			float sqrtDelta = dotRL * sqrt(delta);
			cosBeta0 = (-B + sqrtDelta) * invA;
			cosBeta1 = (-B - sqrtDelta) * invA;
			float tempA = cosConeAngle * invDotRL;
			float tempB = dotDL * invDotRL;

			sinBeta0 = tempA - cosBeta0 * tempB;
			sinBeta1 = tempA - cosBeta1 * tempB; 
		}
		else if (delta > -1e-6) // treate it as on tangent surface.
		{
			float invDotRL = 1.0 / dotRL;
			cosBeta0 = -B * invA, -1.0f, 1.0f;
			cosBeta1 = cosBeta0;
			float tempA = cosConeAngle * invDotRL;
			float tempB = dotDL * invDotRL;

			sinBeta0 = tempA - cosBeta0 * tempB;
			sinBeta1 = sinBeta0;
		}

		float3 edgeRay0 = normalize(vecToLight * cosBeta0 + sampleDir * sinBeta0);
		float3 edgeRay1 = normalize(vecToLight * cosBeta1 + sampleDir * sinBeta1);

		if (dot(edgeRay0, lightDirection.xyz) < 0.0f)
			edgeRay0 = -edgeRay0;
		if (dot(edgeRay1, lightDirection.xyz) < 0.0f)
			edgeRay1 = -edgeRay1;

		dirVector = edgeRay0;
		float3 upVector = normalize(cross(edgeRay0, edgeRay1));
		rightVector = cross(upVector, edgeRay0);

		viewDirVector = normalRefRay0;
		float3 viewUpVector = normalize(cross(normalRefRay0, normalRefRay1));
		viewRightVector = cross(viewUpVector, normalRefRay0);

		float3 edgeCenterRay = normalize(edgeRay0 + edgeRay1);

		float4 shapeVertex[8];

		float3 insertRay0 = normalize(edgeRay0 + edgeCenterRay);
		float3 insertRay1 = normalize(edgeRay1 + edgeCenterRay);

		shapeVertex[0].xyz = lightPosition.xyz;
		shapeVertex[1].xyz = lightPosition.xyz + edgeRay0 * radius;
		shapeVertex[2].xyz = lightPosition.xyz + insertRay0 / dot(edgeRay0, insertRay0) * radius;
		shapeVertex[3].xyz = lightPosition.xyz + edgeCenterRay * radius;
		shapeVertex[4].xyz = lightPosition.xyz + insertRay1 / dot(edgeRay1, insertRay1) * radius;
		shapeVertex[5].xyz = lightPosition.xyz + edgeRay1 * radius;

		shapeVertex[0].w = dot(viewRayClipPlane0, shapeVertex[0].xyz);
		shapeVertex[1].w = dot(viewRayClipPlane0, shapeVertex[1].xyz);
		shapeVertex[2].w = dot(viewRayClipPlane0, shapeVertex[2].xyz);
		shapeVertex[3].w = dot(viewRayClipPlane0, shapeVertex[3].xyz);
		shapeVertex[4].w = dot(viewRayClipPlane0, shapeVertex[4].xyz);
		shapeVertex[5].w = dot(viewRayClipPlane0, shapeVertex[5].xyz);

		uint numVertex = 6;
		uint shapeIndexList = 0;
		uint numIndex = 0;

		CreateScissorPolygon(numVertex, numIndex, shapeVertex, shapeIndexList, 0, 1);
		CreateScissorPolygon(numVertex, numIndex, shapeVertex, shapeIndexList, 1, 2);
		CreateScissorPolygon(numVertex, numIndex, shapeVertex, shapeIndexList, 2, 3);
		CreateScissorPolygon(numVertex, numIndex, shapeVertex, shapeIndexList, 3, 4);
		CreateScissorPolygon(numVertex, numIndex, shapeVertex, shapeIndexList, 4, 5);
		CreateScissorPolygon(numVertex, numIndex, shapeVertex, shapeIndexList, 5, 0);

		if (numIndex > 0)
		{
			uint tmpShapeIndexList = shapeIndexList;
			for (int i = 0; i < numIndex; i+=3)
			{
				uint idx = tmpShapeIndexList & 0x07;
				float distToPlane = dot(viewRayClipPlane1, shapeVertex[idx].xyz);
				shapeVertex[idx].w = distToPlane;

				if (distToPlane > 0.0f)
					CalculateMinMaxCosAngle(minCosAngle, maxCosAngle, minViewCosAngle, maxViewCosAngle, shapeVertex[idx].xyz, lightPosition.xyz, dirVector, viewDirVector);

				tmpShapeIndexList >>= 3;
			}

			tmpShapeIndexList = shapeIndexList;
			uint idx0 = tmpShapeIndexList & 0x07;
			uint idx1 = 0;
			for (int i = 0; i < numIndex-3; i+=3)
			{
				tmpShapeIndexList >>= 3;
				idx1 = tmpShapeIndexList & 0x07;
				if (shapeVertex[idx0].w * shapeVertex[idx1].w < 0.0f)
				{
					float4 edgeVector = shapeVertex[idx1] - shapeVertex[idx0];
					float3 insertVertex = shapeVertex[idx0].xyz + edgeVector.xyz * (0 - shapeVertex[idx0].w) / edgeVector.w;
					CalculateMinMaxCosAngle(minCosAngle, maxCosAngle, minViewCosAngle, maxViewCosAngle, insertVertex, lightPosition.xyz, dirVector, viewDirVector);
				}

				idx0 = idx1;
			}

			idx1 = shapeIndexList & 0x07;
			if (shapeVertex[idx0].w * shapeVertex[idx1].w < 0.0f)
			{
				float4 edgeVector = shapeVertex[idx1] - shapeVertex[idx0];
				float3 insertVertex = shapeVertex[idx0].xyz + edgeVector.xyz * (0 - shapeVertex[idx0].w) / edgeVector.w;
				CalculateMinMaxCosAngle(minCosAngle, maxCosAngle, minViewCosAngle, maxViewCosAngle, insertVertex, lightPosition.xyz, dirVector, viewDirVector);
			}
		}

		float3 newDirVector = normalize(dirVector.xyz * maxCosAngle + rightVector.xyz * sqrt(saturate(1.0f - maxCosAngle * maxCosAngle)));
		float3 newRightVector = normalize(cross(upVector, newDirVector));

		float3 newViewDirVector = normalize(viewDirVector.xyz * maxViewCosAngle + viewRightVector.xyz * sqrt(saturate(1.0f - maxViewCosAngle * maxViewCosAngle)));
		float3 newViewRightVector = normalize(cross(viewUpVector, newViewDirVector));


		float angleStart = acos(clamp(maxCosAngle, -1.0f, 1.0f));
		float angleEnd = acos(clamp(minCosAngle, -1.0f, 1.0f));

		float viewAngleStart = acos(clamp(maxViewCosAngle, -1.0f, 1.0f));
		float viewAngleEnd = acos(clamp(minViewCosAngle, -1.0f, 1.0f));

		srt.texs->rwb_info_buf[int2(dispatchThreadId.x, 0)] = float4(newDirVector, (angleEnd - angleStart) / kNumMaxSamplesPerLine);
		srt.texs->rwb_info_buf[int2(dispatchThreadId.x, 1)] = float4(newRightVector, 1.0f / (angleEnd - angleStart));
		srt.texs->rwb_info_buf[int2(dispatchThreadId.x, 2)] = float4(newViewDirVector, (viewAngleEnd - viewAngleStart) / kNumMaxSamplesPerLine);
		srt.texs->rwb_info_buf[int2(dispatchThreadId.x, 3)] = float4(newViewRightVector, 0.0f);
	}
}

float2 CaluclateRaySphereIntersection(float fNdotO, float fOdotO, float sqrRadius)
{
	float2 t = 0;

	float B = fNdotO;
	float C = fOdotO - sqrRadius;

	float delta = B * B - C;
	if (delta > 0)
	{
		float sqrtDelta = sqrt(delta);
		t.x = B - sqrtDelta;
		t.y = B + sqrtDelta;
	}

	return t;
}

//=================================================================================================
[numthreads(kNumMaxSlices, 1, 1)]
void CS_VolPointLightLinearInfoCreateOpt(uint3 dispatchThreadId : SV_DispatchThreadID, VolSpotLightInfoSrt srt : S_SRT_DATA)
{
	float3 lightPosVS = srt.consts->m_positionVS.xyz;
	float4 sampleRightVector = srt.consts->m_sampleRightVS;
	float4 sampleDirVector = srt.consts->m_sampleDirVS;
	float radius = srt.consts->m_positionVS.w;
	float sqrRadius = radius * radius;

	if (dispatchThreadId.x < srt.consts->m_numSlices)
	{
		float cosAngle, sinAngle;
		float sampleAngle = (dispatchThreadId.x + 0.5f) * srt.consts->m_sampleAngleInfo.x + srt.consts->m_sampleAngleInfo.y;
		sincos(sampleAngle, sinAngle, cosAngle);

		// sampleDir is a vector from light position.
		float3 sampleDir = normalize(cosAngle * sampleDirVector.xyz + sinAngle * sampleRightVector.xyz);
	
		float3 vecToLight = normalize(lightPosVS.z > 0 ? lightPosVS.xyz : -lightPosVS.xyz);

		float3 clipPlane = normalize(cross(vecToLight, sampleDir));

		float2 refRayMinY, refRayMaxY;
		FindFrustumClipEdge(refRayMinY, refRayMaxY, clipPlane, srt.consts->m_viewFrustumPlanes);

		float3 normalRefRay0 = normalize(float3(refRayMinY, 1.0f));
		float3 normalRefRay1 = normalize(float3(refRayMaxY, 1.0f));

		float dotVal0 = dot(sampleDir, normalRefRay0);
		float dotVal1 = dot(sampleDir, normalRefRay1);

		if (dotVal0 > dotVal1)
		{
			float3 tRay = normalRefRay1; normalRefRay1 = normalRefRay0; normalRefRay0 = tRay;
			float tVal = dotVal1; dotVal1 = dotVal0; dotVal0 = tVal;
		}

		float3 newDirVector = 0;
		float3 newRightVector = 0;
		float3 newViewDirVector = 0;
		float3 newViewRightVector = 0;

		float sampleAngleRange = 0;
		float viewAngleRange = 0;
		bool bPassCenter = false;

		if (dotVal1 > 0)
		{
			if (dotVal0 < 0)
			{
				normalRefRay0 = vecToLight;
				dotVal0 = 0.0f;
				bPassCenter = true;
			}

			float fOdotO = dot(lightPosVS.xyz, lightPosVS.xyz);
			if (fOdotO > sqrRadius)
			{
				float x = sqrt(fOdotO * sqrRadius / (fOdotO - sqrRadius));
				float3 ray = normalize(lightPosVS.xyz + sampleDir * x);

				float dotVal2 = dot(sampleDir, ray);

				if (dotVal2 < dotVal1)
				{
					dotVal1 = dotVal2;
					normalRefRay1 = ray;
				}

				if (dotVal2 < dotVal0)
				{
					dotVal0 = dotVal2;
					normalRefRay0 = ray;
				}
			}

			if (dotVal0 < dotVal1)
			{
				float angleStart = 0.0f;
				float angleEnd = kPi;
				if (bPassCenter)
				{
					if (lightPosVS.z < 0.0f)
					{
						float2 t = CaluclateRaySphereIntersection(dot(normalRefRay1, lightPosVS.xyz), fOdotO, sqrRadius);
						angleEnd = acos(dot(vecToLight, normalize(normalRefRay1 * t.y - lightPosVS.xyz)));
					}
				}
				else
				{	
					float2 t = CaluclateRaySphereIntersection(dot(normalRefRay0, lightPosVS.xyz), fOdotO, sqrRadius);
					
					if (t.x < 0) t.x = 0;
					if (t.y < 0) t.y = 0;

					if (t.x < t.y)
					{
						angleStart = acos(clamp(dot(vecToLight, normalize(normalRefRay0 * t.y - lightPosVS.xyz)), -1.0f, 1.0f));
						angleEnd = acos(clamp(dot(vecToLight, normalize(normalRefRay0 * t.x - lightPosVS.xyz)), -1.0f, 1.0f));
					}
				}

				sampleAngleRange = angleEnd - angleStart;

				float2 sincosAngle;
				sincos(angleStart, sincosAngle.x, sincosAngle.y);

				newDirVector = vecToLight * sincosAngle.y + sampleDir * sincosAngle.x;
				newRightVector = normalize(cross(cross(vecToLight, sampleDir), newDirVector));

				newViewDirVector = normalRefRay0;
				newViewRightVector = normalize(cross(cross(normalRefRay0, normalRefRay1), newViewDirVector));

				viewAngleRange = acos(dot(normalRefRay1, newViewDirVector));
			}
		}

		srt.texs->rwb_info_buf[int2(dispatchThreadId.x, 0)] = float4(newDirVector, sampleAngleRange / kNumMaxSamplesPerLine);
		srt.texs->rwb_info_buf[int2(dispatchThreadId.x, 1)] = float4(newRightVector, sampleAngleRange > 0.0f ? 1.0f / sampleAngleRange : 1.0f);
		srt.texs->rwb_info_buf[int2(dispatchThreadId.x, 2)] = float4(newViewDirVector, viewAngleRange / kNumMaxSamplesPerLine);
		srt.texs->rwb_info_buf[int2(dispatchThreadId.x, 3)] = float4(newViewRightVector, 0.0f);
	}
}

//=================================================================================================
groupshared float g_lineAngleSamplesSpot[kNumMaxSamplesPerLine];
groupshared float g_lineAngleMaskSamplesSpot[kNumMaxSamplesPerLine];

struct VolPointSampleShadowSrt
{
	VolSpotLightParams					*m_pConsts;
	RWTexture2D<float4>					m_volIntensityBuffer;
	Texture2D<float>					m_srcDepthVS;
	Texture2D<float4>					m_volSampleInfoBuffer;
	LocalShadowTable*					m_pLocalShadowTextures;
	Texture2D<float4>					m_goboTexArray;
	SamplerState 						m_pointSampler;
};

struct VolSpotSampleShadowSrt
{
	VolSpotLightParams					*m_pConsts;
	RWTexture2D<float4>					m_volIntensityBuffer;
	Texture2D<float>					m_srcDepthVS;
	Texture2D<float4>					m_volSampleInfoBuffer;
	LocalShadowTable*					m_pLocalShadowTextures;
	Texture2D<float4>					m_goboTexArray;
	SamplerState 						m_pointSampler;
};

void CalculateIntersectRay(out float finalT0, out float finalT1, 
						   float3 N, float3 O, float3 D, 
						   float sqrRadius, float cosConeAngle, float sqrCosConeAngle, float viewSpaceDist, float clipNear)
{
	uint numTs = 0;
	float finalT[3];

	float fNdotD = dot(N, D);
	float fOdotD = dot(O, D);
	float fNdotO = dot(N, O);
	float fOdotO = dot(O, O);

	// ray intersect with cone shape.
	float A = fNdotD * fNdotD - sqrCosConeAngle;
	float B = fNdotD * fOdotD - sqrCosConeAngle * fNdotO;
	float C = fOdotD * fOdotD - sqrCosConeAngle * fOdotO;

	float fBdivA = B / A;
	float fCdivA = C / A;

	float delta = fBdivA * fBdivA - fCdivA;
	if (delta > 0)
	{
		float sqrtDelta = sqrt(delta);
		float t0 = fBdivA - sqrtDelta;
		float t1 = fBdivA + sqrtDelta;

		float3 rayToT0 = t0 * N - O;
		float3 rayToT1 = t1 * N - O;

		float tt0 = t0 * fNdotD - fOdotD;
		float tt1 = t1 * fNdotD - fOdotD;

		if (tt0 > 0.0f && dot(rayToT0, rayToT0) < sqrRadius)
			finalT[numTs++] = t0;

		if (tt1 > 0.0f && dot(rayToT1, rayToT1) < sqrRadius)
			finalT[numTs++] = t1;
	}

	if (numTs < 2) // ok, the ray only intersect with one edge, need test the interset with sphere.
	{
		B = fNdotO;
		C = fOdotO - sqrRadius;

		delta = B * B - C;
		if (delta > 0)
		{
			float sqrtDelta = sqrt(delta);
			float t0 = B - sqrtDelta;
			float t1 = B + sqrtDelta;

			float3 rayToT0 = normalize(t0 * N - O);
			float3 rayToT1 = normalize(t1 * N - O);

			if (dot(rayToT0, D) > cosConeAngle)
				finalT[numTs++] = t0;

			if (dot(rayToT1, D) > cosConeAngle)
				finalT[numTs++] = t1;
		}
	}

	finalT0 = 0.0f; 
	finalT1 = viewSpaceDist;

	if (numTs > 0)
	{
		finalT0 = max(finalT0, min(finalT[0], finalT[1]));
		finalT1 = min(finalT1, max(finalT[0], finalT[1]));
	}

	if (clipNear > 0.0f)
	{
		float nearClipPlaneDist = -fOdotD - clipNear;

		float weightT0 = fNdotD * finalT0 + nearClipPlaneDist;
		float weightT1 = fNdotD * finalT1 + nearClipPlaneDist;

		if (weightT0 <= 0.0f && weightT1 <= 0.0f)
		{
			finalT1 = finalT0 = 0.0f;
		}
		else if (weightT0 < 0.0f || weightT1 < 0.0f)
		{
			float newT = -nearClipPlaneDist / fNdotD;

			if (weightT0 > 0)
				finalT1 = newT;
			else
				finalT0 = newT;
		}
	}
}

//=================================================================================================
[numthreads(kNumMaxSamplesPerLine, 1, 1)]
void CS_VolPointLightLineSampleCreate(uint3 dispatchThreadId : SV_DispatchThreadID, VolPointSampleShadowSrt* pSrt : S_SRT_DATA)
{
	float2 ditherFrac = (frac(dispatchThreadId.yx * 0.4 * 0.434 + dispatchThreadId.xy * 0.434) - 0.5f) * 2.0f;

	float3 view2WorldMat[3];
	view2WorldMat[0] = pSrt->m_pConsts->m_worldShadowMat[0].xyz;
	view2WorldMat[1] = pSrt->m_pConsts->m_worldShadowMat[1].xyz;
	view2WorldMat[2] = pSrt->m_pConsts->m_worldShadowMat[2].xyz;

	float4 refRayDir = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 0)];
	float4 refRayRight = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 1)];
	float4 viewRayDir = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 2)];
	float4 viewRayRight = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 3)];
	float4 rayAngleInfo = float4(refRayDir.w, refRayRight.w, viewRayDir.w, 0.0f);
	float3 lightRefRay = normalize(pSrt->m_pConsts->m_positionVS.xyz);

	float4 lightPosVS = pSrt->m_pConsts->m_positionVS;
	float radius = pSrt->m_pConsts->m_positionVS.w;
	float sqrRadius = radius * radius;

	{
		float sampleAngle = rayAngleInfo.x * (dispatchThreadId.x + 0.5f);
		float2 sincosAngle;
		sincos(sampleAngle, sincosAngle.x, sincosAngle.y);

		float3 lightRayVS = refRayDir.xyz * sincosAngle.y + refRayRight.xyz * sincosAngle.x;

		float3 lightRayWS;
		lightRayWS.x = dot(view2WorldMat[0].xyz, lightRayVS);
		lightRayWS.y = dot(view2WorldMat[1].xyz, lightRayVS);
		lightRayWS.z = dot(view2WorldMat[2].xyz, lightRayVS);

		lightRayWS = normalize(lightRayWS);
		float3 absLightRayWS = abs(lightRayWS);

		float3 lightSpacePos = CalcTetraLightSpacePos(-lightRayWS);
		float2 shadowTexCoord = float2(lightSpacePos.x*0.5f + 0.5f, lightSpacePos.y*-0.5f + 0.5f);
		float depthZ = pSrt->m_pLocalShadowTextures->SampleLevel(pSrt->m_pointSampler, shadowTexCoord, 0.0f, pSrt->m_pConsts->m_localShadowTextureIdx);

		float   shadowSpaceZ	= (float)(pSrt->m_pConsts->m_shadowScreenViewParams.y / (depthZ - pSrt->m_pConsts->m_shadowScreenViewParams.x));
		float	shadowRayLen	= shadowSpaceZ * length(lightRayWS / max3(absLightRayWS.x, absLightRayWS.y, absLightRayWS.z));
		float3	rayDir			= normalize(lightPosVS.xyz + shadowRayLen * lightRayVS);

		float	cosSampleAlpha = dot(rayDir, lightRefRay);
		g_lineAngleSamplesSpot[dispatchThreadId.x] = cosSampleAlpha;//cosSampleAlpha > pSrt->m_pConsts->m_refAngleClamp ? 1.1f : (cosSampleAlpha < -pSrt->m_pConsts->m_refAngleClamp ? -1.1f : cosSampleAlpha);
		g_lineAngleMaskSamplesSpot[dispatchThreadId.x] = 1.0f;
	}

	GroupMemoryBarrierWithGroupSync();

	float viewAngle = rayAngleInfo.z * (dispatchThreadId.x + 0.5f);

	float2 sincosAngle;
	sincos(viewAngle, sincosAngle.x, sincosAngle.y);

	float3 viewRay = normalize(viewRayDir.xyz * sincosAngle.y + 
							   viewRayRight.xyz * sincosAngle.x);

	float2 viewRayXy = viewRay.xy / viewRay.z;

	float2 uv = viewRayXy * pSrt->m_pConsts->m_invViewSpaceParams.xy + pSrt->m_pConsts->m_invViewSpaceParams.zw;
	float viewRayScale = length(float3(viewRayXy, 1.0f));

	float viewSpaceZ = pSrt->m_srcDepthVS.SampleLevel(pSrt->m_pointSampler, uv, 0).x;
	float viewSpaceDist = viewRayScale * viewSpaceZ;

	float3 N = viewRay;
	float3 O = lightPosVS.xyz;

	float2 t = CaluclateRaySphereIntersection(dot(N, O), dot(O, O), sqrRadius);
	float t0 = max(t.x, 0.0f);
	float t1 = min(t.y, viewSpaceDist);

	float visibility = 0.0f;
	float t2 = 0, t3 = 0;
	if (t0 < t1)
	{
		float3 viewRayFar = normalize(N * t1 - lightPosVS.xyz);
		float3 viewRayNear = normalize(N * t0 - lightPosVS.xyz);

		float2 sincosAngleFar = normalize(float2(dot(viewRayFar, refRayRight.xyz), 
												 dot(viewRayFar, refRayDir.xyz)));

		float2 sincosAngleNear = normalize(float2(dot(viewRayNear, refRayRight.xyz), 
												  dot(viewRayNear, refRayDir.xyz)));

		float angleFar = acos(clamp(sincosAngleFar.y, -1.0f, 1.0f));
		float angleNear = acos(clamp(sincosAngleNear.y, -1.0f, 1.0f));

/*		angleFar = sincosAngleFar.x > 0.0f ? angleFar : 0.0f;
		angleNear = sincosAngleNear.x > 0.0f ? angleNear : 0.0f;*/

		if (angleFar < angleNear)
		{
			float angleTemp = angleNear;
			angleNear = angleFar;
			angleFar = angleTemp;
		}

		t2 = angleNear;
		t3 = angleFar;

		float angleFarRatio = saturate(angleFar * rayAngleInfo.y);
		float angleNearRatio = saturate(angleNear * rayAngleInfo.y);

		float refValue = dot(viewRay, lightRefRay);

		if (angleFar > 0.0f && angleFarRatio > angleNearRatio)
		{
			float sumVisis = 0.0f;

			float angleRatioDiff = angleFarRatio - angleNearRatio;
			uint numSamples = clamp((angleRatioDiff * kNumMaxSamplesPerLine + 0.9999f), 1, pSrt->m_pConsts->m_numSamplesPerLine);
			float angleRatioStep = angleRatioDiff / numSamples;

			float angleRatio = angleNearRatio + angleRatioStep * (ditherFrac.x + ditherFrac.y) * 0.5f;

			for (int i = 0; i < numSamples; i++)
			{
				int idx = angleRatio * kNumMaxSamplesPerLine;
				if (idx >= 0 && idx < kNumMaxSamplesPerLine)
					sumVisis += (refValue > g_lineAngleSamplesSpot[idx]) * g_lineAngleMaskSamplesSpot[idx];
				angleRatio += angleRatioStep;
			}

			visibility = sumVisis / numSamples;
		}
	}

	pSrt->m_volIntensityBuffer[int2(dispatchThreadId.xy)] = float4(visibility, viewSpaceZ, 0, 0);
}

//=================================================================================================
[numthreads(kNumMaxSamplesPerLine, 1, 1)]
void CS_VolSpotLightLineSampleCreate(uint3 dispatchThreadId : SV_DispatchThreadID, VolSpotSampleShadowSrt* pSrt : S_SRT_DATA)
{
	float2 ditherFrac = (frac(dispatchThreadId.yx * 0.4 * 0.434 + dispatchThreadId.xy * 0.434) - 0.5f) * 2.0f;

	float4 refRayDir = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 0)];
	float4 refRayRight = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 1)];
	float4 viewRayDir = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 2)];
	float4 viewRayRight = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 3)];
	float4 rayAngleInfo = float4(refRayDir.w, refRayRight.w, viewRayDir.w, 0.0f);
	float3 lightRefRay = normalize(pSrt->m_pConsts->m_positionVS.xyz);

	float4 lightPosition = pSrt->m_pConsts->m_positionVS;
	float4 lightDirection = pSrt->m_pConsts->m_directionVS;
	float radius = lightPosition.w;
	float sqrRadius = radius * radius;
	float sqrCosConeAngle = pSrt->m_pConsts->m_sampleAngleInfo.z;
	float cosConeAngle = pSrt->m_pConsts->m_sampleAngleInfo.w;

	{
		float sampleAngle = rayAngleInfo.x * (dispatchThreadId.x + 0.5f);
		float2 sincosAngle;
		sincos(sampleAngle, sincosAngle.x, sincosAngle.y);

		float3 lightRay = normalize(refRayDir.xyz * sincosAngle.y + 
									refRayRight.xyz * sincosAngle.x);

		float3 samplePos = lightRay + lightPosition.xyz;

		float3 shadowPos = float3(dot(pSrt->m_pConsts->m_worldShadowMat[0], float4(samplePos, 1.0)),
								  dot(pSrt->m_pConsts->m_worldShadowMat[1], float4(samplePos, 1.0)),
								  dot(pSrt->m_pConsts->m_worldShadowMat[2], float4(samplePos, 1.0)));

		float2 bufferTexCoord	= saturate(shadowPos.xy / shadowPos.z);

		float   depthZ			= pSrt->m_pLocalShadowTextures->SampleLevel(pSrt->m_pointSampler, bufferTexCoord, 0.0f, pSrt->m_pConsts->m_localShadowTextureIdx);
		float   shadowSpaceZ	= (float)(pSrt->m_pConsts->m_shadowScreenViewParams.y / (depthZ - pSrt->m_pConsts->m_shadowScreenViewParams.x));
		float	shadowRayLen	= shadowSpaceZ / dot(lightRay, lightDirection.xyz);
		float3	rayDir			= normalize(lightPosition.xyz + shadowRayLen * lightRay);

		float	cosSampleAlpha = dot(rayDir, lightRefRay);
		g_lineAngleSamplesSpot[dispatchThreadId.x] = cosSampleAlpha;//(cosSampleAlpha > pSrt->m_pConsts->m_refAngleClamp ? 1.1f : (cosSampleAlpha < -pSrt->m_pConsts->m_refAngleClamp ? -1.1f : cosSampleAlpha)) - 0.1f;
		float2 texCoordNorm = bufferTexCoord * 2.0f - 1.0f;
		float  angleStrength = saturate(dot(texCoordNorm, texCoordNorm));
		float  flashIntensity = angleStrength < 0.1f ? cos(angleStrength * 9.0f * 3.1415926f / 2.0f) : cos(0.1f * 9.0f * 3.1415926f / 2.0f);
		g_lineAngleMaskSamplesSpot[dispatchThreadId.x] = pSrt->m_pConsts->m_isVolFlashLight ? (flashIntensity * 4.0f) : 1.0f;
	}

	GroupMemoryBarrierWithGroupSync();

	float viewAngle = rayAngleInfo.z * (dispatchThreadId.x + 0.5f);

	float2 sincosAngle;
	sincos(viewAngle, sincosAngle.x, sincosAngle.y);

	float3 viewRay = normalize(viewRayDir.xyz * sincosAngle.y + 
							   viewRayRight.xyz * sincosAngle.x);

	float2 viewRayXy = viewRay.xy / viewRay.z;

	float2 uv = viewRayXy * pSrt->m_pConsts->m_invViewSpaceParams.xy + pSrt->m_pConsts->m_invViewSpaceParams.zw;
	float viewRayScale = length(float3(viewRayXy, 1.0f));

	float viewSpaceZ = pSrt->m_srcDepthVS.SampleLevel(pSrt->m_pointSampler, uv, 0).x;
	float viewSpaceDist = viewRayScale * viewSpaceZ;

	float3 N = viewRay;
	float3 D = lightDirection.xyz;
	float3 O = lightPosition.xyz;

	float t0, t1;
	CalculateIntersectRay(t0, t1, N, O, D, sqrRadius, cosConeAngle, sqrCosConeAngle, viewSpaceDist, pSrt->m_pConsts->m_sampleDirVectorNear);

	float3 viewRayFar = normalize(N * t1 - lightPosition.xyz);
	float3 viewRayNear = normalize(N * t0 - lightPosition.xyz);

	float2 sincosAngleFar = normalize(float2(dot(viewRayFar, refRayRight.xyz), 
											 dot(viewRayFar, refRayDir.xyz)));

	float2 sincosAngleNear = normalize(float2(dot(viewRayNear, refRayRight.xyz), 
											  dot(viewRayNear, refRayDir.xyz)));

	float angleFar = acos(clamp(sincosAngleFar.y, -1.0f, 1.0f));
	float angleNear = acos(clamp(sincosAngleNear.y, -1.0f, 1.0f));

	angleFar = sincosAngleFar.x > 0.0f ? angleFar : 0.0f;
	angleNear = sincosAngleNear.x > 0.0f ? angleNear : 0.0f;

	if (angleFar < angleNear)
	{
		float angleTemp = angleNear;
		angleNear = angleFar;
		angleFar = angleTemp;
	}

	float angleFarRatio = saturate(angleFar * rayAngleInfo.y);
	float angleNearRatio = saturate(angleNear * rayAngleInfo.y);

	float refValue = dot(viewRay, lightRefRay);

	float visibility = 0.0f;
	if (angleFar > 0.0f && angleFarRatio > angleNearRatio)
	{
		float sumVisis = 0.0f;

		float angleRatioDiff = angleFarRatio - angleNearRatio;
		uint numSamples = clamp((angleRatioDiff * kNumMaxSamplesPerLine + 0.9999f), 1, pSrt->m_pConsts->m_numSamplesPerLine);
		float angleRatioStep = angleRatioDiff / numSamples;

		float angleRatio = angleNearRatio + angleRatioStep * (ditherFrac.x + ditherFrac.y) * 0.5f;

		for (int i = 0; i < numSamples; i++)
		{
			int idx = angleRatio * kNumMaxSamplesPerLine;
			if (idx >= 0 && idx < kNumMaxSamplesPerLine)
				sumVisis += (refValue > g_lineAngleSamplesSpot[idx]) * g_lineAngleMaskSamplesSpot[idx];
			angleRatio += angleRatioStep;
		}

		visibility = sumVisis / numSamples;
	}

	pSrt->m_volIntensityBuffer[int2(dispatchThreadId.xy)] = float4(visibility, viewSpaceZ, 0, 0);
}

//=================================================================================================
[numthreads(kNumMaxSamplesPerLine, 1, 1)]
void CS_VolSpotLightLineSampleWithMaskCreate(uint3 dispatchThreadId : SV_DispatchThreadID, VolSpotSampleShadowSrt* pSrt : S_SRT_DATA)
{
	float2 ditherFrac = (frac(dispatchThreadId.yx * 0.4 * 0.434 + dispatchThreadId.xy * 0.434) - 0.5f) * 2.0f;

	float4 refRayDir = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 0)];
	float4 refRayRight = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 1)];
	float4 viewRayDir = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 2)];
	float4 viewRayRight = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 3)];
	float4 rayAngleInfo = float4(refRayDir.w, refRayRight.w, viewRayDir.w, 0.0f);
	float3 lightRefRay = normalize(pSrt->m_pConsts->m_positionVS.xyz);

	float4 lightPosition = pSrt->m_pConsts->m_positionVS;
	float4 lightDirection = pSrt->m_pConsts->m_directionVS;
	float radius = lightPosition.w;
	float sqrRadius = radius * radius;
	float sqrCosConeAngle = pSrt->m_pConsts->m_sampleAngleInfo.z;
	float cosConeAngle = pSrt->m_pConsts->m_sampleAngleInfo.w;

	{
		float sampleAngle = rayAngleInfo.x * (dispatchThreadId.x + 0.5f);
		float2 sincosAngle;
		sincos(sampleAngle, sincosAngle.x, sincosAngle.y);

		float3 lightRay = normalize(refRayDir.xyz * sincosAngle.y + 
									refRayRight.xyz * sincosAngle.x);

		float3 samplePos = lightRay + lightPosition.xyz;

		float3 shadowPos = float3(dot(pSrt->m_pConsts->m_worldShadowMat[0], float4(samplePos, 1.0)),
								  dot(pSrt->m_pConsts->m_worldShadowMat[1], float4(samplePos, 1.0)),
								  dot(pSrt->m_pConsts->m_worldShadowMat[2], float4(samplePos, 1.0)));

		float2 bufferTexCoord	= saturate(shadowPos.xy / shadowPos.z);

		float	depthZ			= pSrt->m_pLocalShadowTextures->SampleLevel(pSrt->m_pointSampler, bufferTexCoord, 0.0f, pSrt->m_pConsts->m_localShadowTextureIdx);
		float   shadowSpaceZ	= (float)(pSrt->m_pConsts->m_shadowScreenViewParams.y / (depthZ - pSrt->m_pConsts->m_shadowScreenViewParams.x));
		float	shadowRayLen	= shadowSpaceZ / dot(lightRay, lightDirection.xyz);
		float3	rayDir			= normalize(lightPosition.xyz + shadowRayLen * lightRay);

		float	cosSampleAlpha = dot(rayDir, lightRefRay);
		g_lineAngleSamplesSpot[dispatchThreadId.x] = cosSampleAlpha;//cosSampleAlpha > pSrt->m_pConsts->m_refAngleClamp ? 1.1f : (cosSampleAlpha < -pSrt->m_pConsts->m_refAngleClamp ? -1.1f : cosSampleAlpha);
		g_lineAngleMaskSamplesSpot[dispatchThreadId.x] = dot(pSrt->m_goboTexArray.SampleLevel(pSrt->m_pointSampler, bufferTexCoord + pSrt->m_pConsts->m_maskTextureShift, 0).xyz, 0.333f);
	}

	GroupMemoryBarrierWithGroupSync();

	float viewAngle = rayAngleInfo.z * (dispatchThreadId.x + 0.5f);

	float2 sincosAngle;
	sincos(viewAngle, sincosAngle.x, sincosAngle.y);

	float3 viewRay = normalize(viewRayDir.xyz * sincosAngle.y + 
							   viewRayRight.xyz * sincosAngle.x);

	float2 viewRayXy = viewRay.xy / viewRay.z;

	float2 uv = viewRayXy * pSrt->m_pConsts->m_invViewSpaceParams.xy + pSrt->m_pConsts->m_invViewSpaceParams.zw;
	float viewRayScale = length(float3(viewRayXy, 1.0f));

	float viewSpaceZ = pSrt->m_srcDepthVS.SampleLevel(pSrt->m_pointSampler, uv, 0).x;
	float viewSpaceDist = viewRayScale * viewSpaceZ;

	float3 N = viewRay;
	float3 D = lightDirection.xyz;
	float3 O = lightPosition.xyz;

	float t0, t1;
	CalculateIntersectRay(t0, t1, N, O, D, sqrRadius, cosConeAngle, sqrCosConeAngle, viewSpaceDist, pSrt->m_pConsts->m_sampleDirVectorNear);

	float3 viewRayFar = normalize(N * t1 - lightPosition.xyz);
	float3 viewRayNear = normalize(N * t0 - lightPosition.xyz);

	float2 sincosAngleFar = normalize(float2(dot(viewRayFar, refRayRight.xyz), 
											 dot(viewRayFar, refRayDir.xyz)));

	float2 sincosAngleNear = normalize(float2(dot(viewRayNear, refRayRight.xyz), 
											  dot(viewRayNear, refRayDir.xyz)));

	float angleFar = acos(clamp(sincosAngleFar.y, -1.0f, 1.0f));
	float angleNear = acos(clamp(sincosAngleNear.y, -1.0f, 1.0f));

	angleFar = sincosAngleFar.x > 0.0f ? angleFar : 0.0f;
	angleNear = sincosAngleNear.x > 0.0f ? angleNear : 0.0f;

	if (angleFar < angleNear)
	{
		float angleTemp = angleNear;
		angleNear = angleFar;
		angleFar = angleTemp;
	}

	float angleFarRatio = saturate(angleFar * rayAngleInfo.y);
	float angleNearRatio = saturate(angleNear * rayAngleInfo.y);

	float refValue = dot(viewRay, lightRefRay);

	float visibility = 0.0f;
	if (angleFar > 0.0f && angleFarRatio > angleNearRatio)
	{
		float sumVisis = 0.0f;

		float angleRatioDiff = angleFarRatio - angleNearRatio;
		uint numSamples = clamp((angleRatioDiff * kNumMaxSamplesPerLine + 0.9999f), 1, pSrt->m_pConsts->m_numSamplesPerLine);
		float angleRatioStep = angleRatioDiff / numSamples;

		float angleRatio = angleNearRatio + angleRatioStep * (ditherFrac.x + ditherFrac.y) * 0.5f;

		for (int i = 0; i < numSamples; i++)
		{
			int idx = angleRatio * kNumMaxSamplesPerLine;
			if ((idx >= 0 && idx < kNumMaxSamplesPerLine) && refValue > g_lineAngleSamplesSpot[idx])
				sumVisis += g_lineAngleMaskSamplesSpot[idx];
			angleRatio += angleRatioStep;
		}

		visibility = sumVisis / numSamples;
	}

	pSrt->m_volIntensityBuffer[int2(dispatchThreadId.xy)] = float4(visibility, viewSpaceZ, 0, 0);
}

//=================================================================================================
[numthreads(kNumMaxSamplesPerLine, 1, 1)]
void CS_VolSpotLightLineSampleNoShadowWithMaskCreate(uint3 dispatchThreadId : SV_DispatchThreadID, VolSpotSampleShadowSrt* pSrt : S_SRT_DATA)
{
	float2 ditherFrac = (frac(dispatchThreadId.yx * 0.4 * 0.434 + dispatchThreadId.xy * 0.434) - 0.5f) * 2.0f;

	float4 refRayDir = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 0)];
	float4 refRayRight = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 1)];
	float4 viewRayDir = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 2)];
	float4 viewRayRight = pSrt->m_volSampleInfoBuffer[int2(dispatchThreadId.y, 3)];
	float4 rayAngleInfo = float4(refRayDir.w, refRayRight.w, viewRayDir.w, 0.0f);
	float3 lightRefRay = normalize(pSrt->m_pConsts->m_positionVS.xyz);

	float4 lightPosition = pSrt->m_pConsts->m_positionVS;
	float4 lightDirection = pSrt->m_pConsts->m_directionVS;
	float radius = lightPosition.w;
	float sqrRadius = radius * radius;
	float sqrCosConeAngle = pSrt->m_pConsts->m_sampleAngleInfo.z;
	float cosConeAngle = pSrt->m_pConsts->m_sampleAngleInfo.w;

	{
		float sampleAngle = rayAngleInfo.x * (dispatchThreadId.x + 0.5f);
		float2 sincosAngle;
		sincos(sampleAngle, sincosAngle.x, sincosAngle.y);

		float3 lightRay = normalize(refRayDir.xyz * sincosAngle.y + 
									refRayRight.xyz * sincosAngle.x);

		float3 samplePos = lightRay + lightPosition.xyz;

		float3 shadowPos = float3(dot(pSrt->m_pConsts->m_worldShadowMat[0], float4(samplePos, 1.0)),
								  dot(pSrt->m_pConsts->m_worldShadowMat[1], float4(samplePos, 1.0)),
								  dot(pSrt->m_pConsts->m_worldShadowMat[2], float4(samplePos, 1.0)));

		float2 bufferTexCoord	= shadowPos.xy / shadowPos.z;

		g_lineAngleMaskSamplesSpot[dispatchThreadId.x] = dot(pSrt->m_goboTexArray.SampleLevel(pSrt->m_pointSampler, bufferTexCoord + pSrt->m_pConsts->m_maskTextureShift, 0).xyz, 0.333f);
	}

	GroupMemoryBarrierWithGroupSync();

	float viewAngle = rayAngleInfo.z * (dispatchThreadId.x + 0.5f);

	float2 sincosAngle;
	sincos(viewAngle, sincosAngle.x, sincosAngle.y);

	float3 viewRay = normalize(viewRayDir.xyz * sincosAngle.y + 
							   viewRayRight.xyz * sincosAngle.x);

	float2 viewRayXy = viewRay.xy / viewRay.z;

	float2 uv = viewRayXy * pSrt->m_pConsts->m_invViewSpaceParams.xy + pSrt->m_pConsts->m_invViewSpaceParams.zw;
	float viewRayScale = length(float3(viewRayXy, 1.0f));

	float viewSpaceZ = pSrt->m_srcDepthVS.SampleLevel(pSrt->m_pointSampler, uv, 0).x;
	float viewSpaceDist = viewRayScale * viewSpaceZ;

	float3 N = viewRay;
	float3 D = lightDirection.xyz;
	float3 O = lightPosition.xyz;

	float t0, t1;
	CalculateIntersectRay(t0, t1, N, O, D, sqrRadius, cosConeAngle, sqrCosConeAngle, viewSpaceDist, pSrt->m_pConsts->m_sampleDirVectorNear);

	float3 viewRayFar = normalize(N * t1 - lightPosition.xyz);
	float3 viewRayNear = normalize(N * t0 - lightPosition.xyz);

	float2 sincosAngleFar = normalize(float2(dot(viewRayFar, refRayRight.xyz), 
											 dot(viewRayFar, refRayDir.xyz)));

	float2 sincosAngleNear = normalize(float2(dot(viewRayNear, refRayRight.xyz), 
											  dot(viewRayNear, refRayDir.xyz)));

	float angleFar = acos(clamp(sincosAngleFar.y, -1.0f, 1.0f));
	float angleNear = acos(clamp(sincosAngleNear.y, -1.0f, 1.0f));

	angleFar = sincosAngleFar.x > 0.0f ? angleFar : 0.0f;
	angleNear = sincosAngleNear.x > 0.0f ? angleNear : 0.0f;

	if (angleFar < angleNear)
	{
		float angleTemp = angleNear;
		angleNear = angleFar;
		angleFar = angleTemp;
	}

	float angleFarRatio = saturate(angleFar * rayAngleInfo.y);
	float angleNearRatio = saturate(angleNear * rayAngleInfo.y);

	float visibility = 0.0f;
	if (angleFar > 0.0f && angleFarRatio > angleNearRatio)
	{
		float sumVisis = 0.0f;

		float angleRatioDiff = angleFarRatio - angleNearRatio;
		uint numSamples = clamp((angleRatioDiff * kNumMaxSamplesPerLine + 0.9999f), 1, pSrt->m_pConsts->m_numSamplesPerLine);
		float angleRatioStep = angleRatioDiff / numSamples;

		float angleRatio = angleNearRatio + angleRatioStep * (ditherFrac.x + ditherFrac.y) * 0.5f;

		for (int i = 0; i < numSamples; i++)
		{
			int idx = angleRatio * kNumMaxSamplesPerLine;
			if (idx >= 0 && idx < kNumMaxSamplesPerLine)
				sumVisis += g_lineAngleMaskSamplesSpot[idx];
			angleRatio += angleRatioStep;
		}

		visibility = sumVisis / numSamples;
	}


	pSrt->m_volIntensityBuffer[int2(dispatchThreadId.xy)] = float4(visibility, viewSpaceZ, 0, 0);
}

int GetCascadedLevel(float cascadedLevelDist[8], int numCascadedLevel, float viewZ)
{
	bool bFound = false;
	int i = 0;
	while( i < numCascadedLevel-1 && !bFound)
	{
		if (cascadedLevelDist[i] >= viewZ)
			bFound = true;
		else
			i++;
	}

	return i;
}

float3 CalculateSunlightShadowRayDir(VolDirectLightParams* pParams,
									 Texture2DArray<float> txSunShadow,
									 SamplerState samplePoint,
									 float3 positionVS,
									 float viewZ0, float viewZ1)
{
	int numCascadedLevels = asint(pParams->m_linearDepthParams.w);

	int iCascadedLevel0 = GetCascadedLevel(pParams->m_cascadedLevelDist, numCascadedLevels, viewZ0);
	int iCascadedLevel1 = GetCascadedLevel(pParams->m_cascadedLevelDist, numCascadedLevels, viewZ1);

	int minCascadedlevel = min(iCascadedLevel0, iCascadedLevel1);
	int maxCascadedLevel = max(iCascadedLevel0, iCascadedLevel1);

	float4 posVS = float4(positionVS, 1.0);

	int rightCascadedLevel = minCascadedlevel;
	int i = minCascadedlevel;

	float3 shadowCoords;
	float cascadedDist = pParams->m_cascadedLevelDist[i];
	shadowCoords.x = dot(posVS, pParams->m_shadowMat[i*5+0]);
	shadowCoords.y = dot(posVS, pParams->m_shadowMat[i*5+1]);
	shadowCoords.z = i;

	float rightDepthZ = txSunShadow.SampleLevel(samplePoint, shadowCoords, 0).r;
	float shadowPosVSZ = dot(float4(shadowCoords.xy, rightDepthZ, 1.0f), pParams->m_shadowMat[i*5+4]);

	bool bFound = shadowPosVSZ <= cascadedDist;

	i++;
	float refShadowPosVSZ = shadowPosVSZ;

	while (i <= maxCascadedLevel && !bFound)
	{
		cascadedDist = pParams->m_cascadedLevelDist[i];
		shadowCoords.x = dot(posVS, pParams->m_shadowMat[i*5+0]);
		shadowCoords.y = dot(posVS, pParams->m_shadowMat[i*5+1]);
		shadowCoords.z = i;

		float depthZ = txSunShadow.SampleLevel(samplePoint, shadowCoords, 0).r;

		float4 posSS = float4(shadowCoords.xy, depthZ, 1.0f);
		shadowPosVSZ = dot(posSS, pParams->m_shadowMat[i*5+4]);

		if ((i == maxCascadedLevel || shadowPosVSZ <= cascadedDist) && shadowPosVSZ > refShadowPosVSZ)
		{
			rightCascadedLevel = i;
			rightDepthZ = depthZ;
			refShadowPosVSZ = shadowPosVSZ;
			bFound = true;
		}
		else if (shadowPosVSZ < refShadowPosVSZ)
		{
			bFound = true;
		}

		i++;
	}

	float4 posSS = float4(shadowCoords.xy, rightDepthZ, 1.0f);

	float3 shadowPosVS;
	shadowPosVS.x = dot(posSS, pParams->m_shadowMat[rightCascadedLevel*5+2]);
	shadowPosVS.y = dot(posSS, pParams->m_shadowMat[rightCascadedLevel*5+3]);
	shadowPosVS.z = dot(posSS, pParams->m_shadowMat[rightCascadedLevel*5+4]);

	return shadowPosVS;
}

float3 CalculateSunlightShadowRayDirCircle(VolDirectLightParams* pParams,
										   Texture2DArray<float> txSunShadow,
										   SamplerState samplePoint,
										   float3 positionVS,
										   float3 edgePositionVS,
										   float bFromSun)
{
	int numCascadedLevels = asint(pParams->m_linearDepthParams.w);
	int maxCascadedLevelIdx = numCascadedLevels - 1;

	int iEdgeCascadedLevel = GetCascadedLevel(pParams->m_cascadedLevelDist, numCascadedLevels, edgePositionVS.z);

	float4 posVS = float4(positionVS, 1.0);

	int i = iEdgeCascadedLevel;
	bool bFound = false;
	float3 shadowCoords = float3(0.0f, 0.0f, 0.0f);
	float depthZ = 0;
	while (i <= maxCascadedLevelIdx && !bFound)
	{
		shadowCoords.x = dot(posVS, pParams->m_shadowMat[i*5+0]);
		shadowCoords.y = dot(posVS, pParams->m_shadowMat[i*5+1]);
		shadowCoords.z = i;

		depthZ = txSunShadow.SampleLevel(samplePoint, shadowCoords, 0).r;

		if ((bFromSun && depthZ < 1.0f) || (!bFromSun && depthZ > 0.0f))
			bFound = true;
		else
			i++;
	}

	if (bFound == false)
		i = maxCascadedLevelIdx;

	float4 posSS = float4(shadowCoords.xy, depthZ, 1.0f);

	float3 shadowPosVS;
	shadowPosVS.x = dot(posSS, pParams->m_shadowMat[i*5+2]);
	shadowPosVS.y = dot(posSS, pParams->m_shadowMat[i*5+3]);
	shadowPosVS.z = dot(posSS, pParams->m_shadowMat[i*5+4]);

	return shadowPosVS;
}

float ConvertDistToLogSpace(float dist)
{
	float logDist = log(1.0f + abs(dist));
	return dist < 0.0f ? -logDist : logDist;
}

float ConvertLogDistToDistance(float logDist)
{
	float dist = exp(abs(logDist)) - 1.0f;
	return logDist < 0.0f ? -dist : dist;
}

struct VolSunSampleTextures
{
	RWTexture2D<float4>			rwb_sample_info;
};

struct VolSunSampleSrt
{
	VolDirectLightParams		*consts;
	VolSunSampleTextures		*texs;
};

//=================================================================================================
[numthreads(64, 1, 1)]
void CS_VolDirectLightInfoCreateOpt(uint3 dispatchThreadId : SV_DispatchThreadID, VolSunSampleSrt srt : S_SRT_DATA)
{
	float4 lightDirection = srt.consts->m_directionVS;
	float4 sampleRightVector = srt.consts->m_sampleRightVS;
	float4 sampleDirVector = srt.consts->m_sampleDirVS;

	if (dispatchThreadId.x < asuint(srt.consts->m_params.x))
	{
		float cosAngle, sinAngle;
		float sampleAngle = (dispatchThreadId.x + 0.5f) * srt.consts->m_sampleAngleInfo.x;
		sincos(sampleAngle, sinAngle, cosAngle);

		// sampleDir is a vector from light position.
		float3 sampleDir = normalize(cosAngle * sampleDirVector.xyz + sinAngle * sampleRightVector.xyz);

		float3 clipPlane = normalize(cross(lightDirection, sampleDir));

		float2 refRayMinY, refRayMaxY;
		FindFrustumClipEdge(refRayMinY, refRayMaxY, clipPlane, srt.consts->m_viewFrustumPlanes);

		if (srt.consts->m_params.w > 0.0f)
		{
			if (dot(float3(refRayMinY, 1.0f), sampleDir) > 0.0f)
			{
				float2 tempRefRay = refRayMinY;
				refRayMinY = refRayMaxY;
				refRayMaxY = tempRefRay;
			}
		}

		float3 normalizedZRefRay0 = float3(refRayMinY, 1.0f);
		float3 normalizedZRefRay1 = float3(refRayMaxY, 1.0f);

		float3 normalRefRay0 = normalize(normalizedZRefRay0);
		float3 normalRefRay1 = normalize(normalizedZRefRay1);
		float dotNormalRay = dot(normalRefRay0, normalRefRay1);
		float angleRange = acos(dotNormalRay);

		float3 dirVector = normalRefRay0;
		float3 upVector = normalize(cross(normalRefRay0, normalRefRay1));
		float3 rightVector = normalize(cross(upVector, dirVector));

		float4 sampleParams[4];
		if (srt.consts->m_params.w == 0.0f) // not face to/from sunlight.
		{
			float3 newSampleDir = normalRefRay0;
			float4 farClipPlane;
			farClipPlane.xyz = normalize(cross(clipPlane, lightDirection));
			farClipPlane.w = -dot(farClipPlane.xyz, normalRefRay0);

			if (dot(farClipPlane, float4(normalRefRay1, 1.0f)) * farClipPlane.w < 0.0f)
				newSampleDir = normalRefRay1;

			float maxDist = srt.consts->m_params.z / newSampleDir.z;
			float3 endPosition = newSampleDir * maxDist;
			float maxLogDist = ConvertDistToLogSpace(maxDist);

			float stepDist = maxLogDist / srt.consts->m_sizeParams.z;

			float3 clipVector = normalize(cross(lightDirection, cross(newSampleDir, lightDirection)));

			sampleParams[0] = float4(newSampleDir, stepDist == 0 ? 1.0f : stepDist);
			sampleParams[1] = float4(clipVector, maxDist / dot(endPosition, clipVector));
			sampleParams[2] = float4(normalizedZRefRay0.xy, normalizedZRefRay1.xy);
			sampleParams[3] = float4(0, 0, 0, 0);
		}
		else
		{
			// these points are start and end of the slice intersection with shadow map
			float3 startPos = normalizedZRefRay0 * srt.consts->m_cascadedLevelDist[kMaxNumCascaded-1];
			float3 endPos = normalizedZRefRay1 * srt.consts->m_cascadedLevelDist[kMaxNumCascaded-1];

			float3 sampleRay = endPos - startPos;
			float sampleRayLen = length(sampleRay);
			float3 sampleRayNrm = sampleRay / sampleRayLen;
			float3 clipVector = normalize(cross(lightDirection, cross(sampleRayNrm, lightDirection)));

			float startLogDist = ConvertDistToLogSpace(dot(startPos, clipVector));
			float endLogDist = ConvertDistToLogSpace(dot(endPos, clipVector));
			float stepLogDist = (endLogDist - startLogDist) / srt.consts->m_sizeParams.z;

			sampleParams[0] = float4(startPos, startLogDist);
			sampleParams[1] = float4(clipVector, stepLogDist);
			sampleParams[2] = float4(sampleRayNrm, sampleRayLen);
			sampleParams[3] = float4(0, 0, 0, 0);
		}

		srt.texs->rwb_sample_info[int2(dispatchThreadId.x, 0)] = sampleParams[0];
		srt.texs->rwb_sample_info[int2(dispatchThreadId.x, 1)] = sampleParams[1];
		srt.texs->rwb_sample_info[int2(dispatchThreadId.x, 2)] = sampleParams[2];
		srt.texs->rwb_sample_info[int2(dispatchThreadId.x, 3)] = sampleParams[3];
	}
}

struct VolSunSampleRaySrt
{
	VolDirectLightParams		*m_pConsts;
	RWTexture2D<float>			m_rwAngleSampleIndex;
	RWTexture2D<float>			m_rwMaskSample;
	Texture2D<float4>			m_txSampleInfo;
	Texture2DArray<float>		m_txSunShadow;
	Texture3D<float3>			m_txMaskTexture;
	SamplerState				m_samplerPoint;
	SamplerState				m_samplerLinear;
};

[numthreads(1, 64, 1)]
void CS_VolSunLightSliceShadowAngleArrayCreate(uint2 dispatchThreadId : SV_DispatchThreadID, uint2 groupId : SV_GroupID, VolSunSampleRaySrt* pSrt : S_SRT_DATA)
{
	float4 refSampleDir = pSrt->m_txSampleInfo[int2(groupId.x, 0)];
	float4 clipPlane = pSrt->m_txSampleInfo[int2(groupId.x, 1)];
	float4 edgeRay = pSrt->m_txSampleInfo[int2(groupId.x, 2)];
	float stepLogDist = refSampleDir.w;
	float4 lightDirection = pSrt->m_pConsts->m_directionVS;

	int sampleIdx = dispatchThreadId.y;

	float sampleDist = ConvertLogDistToDistance((sampleIdx + 0.5f) * stepLogDist);
	float3 positionVS = refSampleDir.xyz * sampleDist;

	float distToClipPlane = dot(clipPlane.xyz, positionVS);
	float viewZ0 = distToClipPlane / dot(clipPlane.xyz, float3(edgeRay.xy, 1.0f));
	float viewZ1 = distToClipPlane / dot(clipPlane.xyz, float3(edgeRay.zw, 1.0f));

	float3 shadowRayDir = CalculateSunlightShadowRayDir(pSrt->m_pConsts, 
													    pSrt->m_txSunShadow,
														pSrt->m_samplerPoint,
														positionVS,
														viewZ0, viewZ1);

	pSrt->m_rwAngleSampleIndex[int2(dispatchThreadId.xy)] = dot(shadowRayDir, lightDirection.xyz);
}

[numthreads(1, 64, 1)]
void CS_VolSunLightCircleShadowAngleArrayCreate(uint2 dispatchThreadId : SV_DispatchThreadID, uint2 groupId : SV_GroupID, VolSunSampleRaySrt* pSrt : S_SRT_DATA)
{
	float4 sampleStart = pSrt->m_txSampleInfo[int2(groupId.x, 0)];
	float4 clipPlane = pSrt->m_txSampleInfo[int2(groupId.x, 1)];
	float4 sampleRayNrm = pSrt->m_txSampleInfo[int2(groupId.x, 2)];
	float4 lightDirection = pSrt->m_pConsts->m_directionVS;

	float logDistStart = sampleStart.w;
	float distRange = sampleRayNrm.w;
	float stepLogDist = clipPlane.w;

	int sampleIdx = dispatchThreadId.y;

	float distToStart = ConvertLogDistToDistance(stepLogDist * (sampleIdx + 0.5f) + logDistStart);
	float t0 = (distToStart - dot(sampleStart.xyz, clipPlane.xyz)) / dot(sampleRayNrm.xyz, clipPlane.xyz);
	float3 positionVS = sampleRayNrm.xyz * t0 + sampleStart.xyz;

	float distToClip = dot(positionVS, clipPlane.xyz);
	
	float3 edgePositionVS = sampleStart.xyz;
	if (distToClip * distRange > 0)
		edgePositionVS += sampleRayNrm.xyz * distRange;

	float t = distToClip / dot(edgePositionVS, clipPlane.xyz);

	float3 shadowRayDir = CalculateSunlightShadowRayDirCircle(pSrt->m_pConsts, 
															  pSrt->m_txSunShadow,
															  pSrt->m_samplerPoint,
															  positionVS,
															  edgePositionVS * t,
															  lightDirection.z > 0.0f);

	pSrt->m_rwAngleSampleIndex[int2(dispatchThreadId.xy)] = dot(shadowRayDir, lightDirection.xyz);
}

[numthreads(1, 64, 1)]
void CS_VolSunLightSliceShadowAngleWithMaskArrayCreate(uint2 dispatchThreadId : SV_DispatchThreadID, uint2 groupId : SV_GroupID, VolSunSampleRaySrt* pSrt : S_SRT_DATA)
{
	float4 refSampleDir = pSrt->m_txSampleInfo[int2(groupId.x, 0)];
	float4 clipPlane = pSrt->m_txSampleInfo[int2(groupId.x, 1)];
	float4 edgeRay = pSrt->m_txSampleInfo[int2(groupId.x, 2)];
	float stepLogDist = refSampleDir.w;
	float4 lightDirection = pSrt->m_pConsts->m_directionVS;

	int sampleIdx = dispatchThreadId.y;

	float sampleDist = ConvertLogDistToDistance((sampleIdx + 0.5f) * stepLogDist);
	float3 positionVS = refSampleDir.xyz * sampleDist;

	float distToClipPlane = dot(clipPlane.xyz, positionVS);
	float viewZ0 = distToClipPlane / dot(clipPlane.xyz, float3(edgeRay.xy, 1.0f));
	float viewZ1 = distToClipPlane / dot(clipPlane.xyz, float3(edgeRay.zw, 1.0f));

	// viewZ0, viewZ1 are only used to find min and max cascade we want to look at
	float3 shadowRayDir = CalculateSunlightShadowRayDir(pSrt->m_pConsts, 
													    pSrt->m_txSunShadow,
														pSrt->m_samplerPoint,
														positionVS,
														viewZ0, viewZ1);
	// shadowRayDir is position of shadow in view space
	// dotting it with light direction gives us distance along light direction

	// note dispatchThreadId.x == groupId.x, it is the slice index
	pSrt->m_rwAngleSampleIndex[int2(dispatchThreadId.xy)] = dot(shadowRayDir, lightDirection.xyz);

	if (pSrt->m_pConsts->m_waterSurfaceInfo.x > 0)
	{
		float3 positionWS;
		positionWS.x = dot(pSrt->m_pConsts->m_viewToWorldMat[0], float4(positionVS, 1.0f));
		positionWS.y = dot(pSrt->m_pConsts->m_viewToWorldMat[1], float4(positionVS, 1.0f));
		positionWS.z = dot(pSrt->m_pConsts->m_viewToWorldMat[2], float4(positionVS, 1.0f));

		float t = (positionWS.y - pSrt->m_pConsts->m_waterSurfaceInfo.y) / pSrt->m_pConsts->m_directionWS.y;

		float3 collisionWS = positionWS - t * pSrt->m_pConsts->m_directionWS.xyz;

		pSrt->m_rwMaskSample[int2(dispatchThreadId.xy)] = pSrt->m_txMaskTexture.SampleLevel(pSrt->m_samplerLinear, float3(collisionWS.xz / 40.0f, pSrt->m_pConsts->m_waterSurfaceInfo.z), 0.0f).x; 
	}
}

[numthreads(1, 64, 1)]
void CS_VolSunLightCircleShadowAngleWithMaskArrayCreate(uint2 dispatchThreadId : SV_DispatchThreadID, uint2 groupId : SV_GroupID, VolSunSampleRaySrt* pSrt : S_SRT_DATA)
{
	float4 sampleStart = pSrt->m_txSampleInfo[int2(groupId.x, 0)];
	float4 clipPlane = pSrt->m_txSampleInfo[int2(groupId.x, 1)];
	float4 sampleRayNrm = pSrt->m_txSampleInfo[int2(groupId.x, 2)];
	float4 lightDirection = pSrt->m_pConsts->m_directionVS;

	float logDistStart = sampleStart.w;
	float distRange = sampleRayNrm.w;
	float stepLogDist = clipPlane.w;

	int sampleIdx = dispatchThreadId.y;

	float distToStart = ConvertLogDistToDistance(stepLogDist * (sampleIdx + 0.5f) + logDistStart);
	float t0 = (distToStart - dot(sampleStart.xyz, clipPlane.xyz)) / dot(sampleRayNrm.xyz, clipPlane.xyz);
	float3 positionVS = sampleRayNrm.xyz * t0 + sampleStart.xyz;

	float distToClip = dot(positionVS, clipPlane.xyz);
	
	float3 edgePositionVS = sampleStart.xyz;
	if (distToClip * distRange > 0)
		edgePositionVS += sampleRayNrm.xyz * distRange;

	float t = distToClip / dot(edgePositionVS, clipPlane.xyz);

	float3 shadowRayDir = CalculateSunlightShadowRayDirCircle(pSrt->m_pConsts, 
															  pSrt->m_txSunShadow,
															  pSrt->m_samplerPoint,
															  positionVS,
															  edgePositionVS * t,
															  lightDirection.z > 0.0f);

	pSrt->m_rwAngleSampleIndex[int2(dispatchThreadId.xy)] = dot(shadowRayDir, lightDirection.xyz);

	if (pSrt->m_pConsts->m_waterSurfaceInfo.x > 0)
	{
		float3 positionWS;
		positionWS.x = dot(pSrt->m_pConsts->m_viewToWorldMat[0], float4(positionVS, 1.0f));
		positionWS.y = dot(pSrt->m_pConsts->m_viewToWorldMat[1], float4(positionVS, 1.0f));
		positionWS.z = dot(pSrt->m_pConsts->m_viewToWorldMat[2], float4(positionVS, 1.0f));

		t = (positionWS.y - pSrt->m_pConsts->m_waterSurfaceInfo.y) / pSrt->m_pConsts->m_directionWS.y;

		float3 collisionWS = positionWS - t * pSrt->m_pConsts->m_directionWS.xyz;

		pSrt->m_rwMaskSample[int2(dispatchThreadId.xy)] = pSrt->m_txMaskTexture.SampleLevel(pSrt->m_samplerLinear, float3(collisionWS.xz / 5.0f, pSrt->m_pConsts->m_waterSurfaceInfo.z), 0.0f).x; 
	}
}

struct  VolFogNoiseParams
{
	float4						m_params;
};

struct VolSunFogNoiseTextures
{
	RWTexture3D<float4>			rwb_vol_noise;
};

struct VolSunFogNoiseSrt
{
	VolFogNoiseParams			*consts;
	VolSunFogNoiseTextures		*texs;;
};

float getNoise(float4 uvs)
{
	return abs(snoise(uvs) - 0.5f) * 2.0f;
}

[numthreads(4, 4, 4)]
void CS_Create3dFogNoiseBuffer(uint3 dispatchThreadId : SV_DispatchThreadId, VolSunFogNoiseSrt srt : S_SRT_DATA)
{
	float4 uv = float4(dispatchThreadId.xyz * float3(1.0f / 128.0f, 1.0f / 128.0f, 1.0f / 32.0f), srt.consts->m_params.w);

 	srt.texs->rwb_vol_noise[dispatchThreadId] = ((1.7*getNoise(uv) + 1.3*getNoise(uv*1.7f) + 1.1*getNoise(uv*3.5f) + getNoise(uv*7.1f)) * 0.1f + 0.05f);
}

struct VolLightInfoSrt
{
	float2						m_depthParams;
	uint						m_pixelScale;
	float						m_powerCurveExp;
	float						m_noiseMoving;
	float						m_maxFogDist;
	float						m_hasFogTexture;
	float						m_distanceGamma;
	float						m_fogMaxDensity;
	float3						m_cameraPosition;
	float4						m_fogColor;
	float						m_fogHeightStart;
	float						m_fogHeightRange;
	float						m_fogCameraHeightExp;
	float						m_fogDistanceStart;
	float						m_angleOffset;
	float						m_mipfadeDistance;
	float2						m_pad;
	float4						m_nearFogColor;
	float4						m_sunDirectionWS;
	float4						m_sunColor;
	float4						m_screenScaleOffset;
	float4						m_viewToWorldMat[3];
	RWTexture3D<float4>			m_rwbVolInfoBuffer;
	Texture2D<float4>			m_txSkyFogTex;
	SamplerState				m_sSamplerLinear;
};

struct VolLightCreateAccBufferSrt
{
	float2						m_depthParams;
	uint						m_pixelScale;
	float						m_invMaxFogDist;
	float						m_fogMaxDensity;
	float						m_fogOcclusion;
	int							m_numSteps;
	float						m_maxFogDist;
	float2						m_screenUvScale;
	float						m_powerCurveExp;
	float						m_maxSunShadowDist;
	float4						m_sampleDirVS;
	float4						m_sampleRightVS;
	float4						m_sunDirectionVS;
	float4						m_sampleAngleInfo;
	float4						m_viewToWorldVecY;
	float						m_fogHeightStart;
	float						m_fogHeightRange;
	float						m_fogCameraHeightExp;
	float						m_fogDistanceStart;
	float						m_fogDistanceEnd;
	float						m_cameraPosY;
	float						m_fogDensity;
	float						m_distanceGamma;
	float						m_volFogIntensity;
	uint						m_faceLightSrc;
	uint3						m_pad;
	float						m_ditherSamples[4][4];
	float4						m_screenScaleOffset;
	RWTexture2D<float4>			m_rwbVolAccBuffer;
	Texture2D<float>			m_txDepthVS;
	Texture3D<float4>			m_txFogInfoGrid;
	Texture2D<float4>			m_txSampleInfoBuffer;
	Texture2D<float>			m_txAngleSampleBuffer;
	SamplerState				m_sSamplerLinear;
};

float GetFogDensity(float pixelY, float cameraY, float distToCamera, float castDist, VolLightCreateAccBufferSrt* pSrt : S_SRT_DATA)
{
#if 0
	float heightDelta = pixelY - cameraY; 
	heightDelta = abs(heightDelta) < 0.01 ? 0.01 : heightDelta;
	pixelY = cameraY + heightDelta;

	float rayDensity = (exp2(pixelY * pSrt->m_fogHeightRange) - pSrt->m_fogCameraHeightExp) / (pSrt->m_fogHeightRange * heightDelta);

	// Artistic control
	float densityBias = 8.0 * pSrt->m_fogDensity;
	float modifiedDepth = pSrt->m_fogDensity * pow(max(distToCamera - pSrt->m_fogDistanceStart, 0) * densityBias, pSrt->m_distanceGamma) / densityBias;	
	
	float density = rayDensity * modifiedDepth;

	density = 1.0-saturate(exp2(-density));
	density = clamp(density, 0, pSrt->m_fogMaxDensity);
#else
	float density = castDist * pow(max(distToCamera - pSrt->m_fogDistanceStart, 0) * pSrt->m_fogDensity, pSrt->m_distanceGamma) * exp(min(pixelY * pSrt->m_fogHeightRange, 0.0f)) * pSrt->m_volFogIntensity;
#endif

	return saturate(density);
}

[numthreads(8, 8, 1)]
void CS_CreateVolLightsAccBuffer(uint3 dispatchThreadId : SV_DispatchThreadId, VolLightCreateAccBufferSrt* pSrt : S_SRT_DATA)
{
	float depthVS = min(pSrt->m_txDepthVS[int2(dispatchThreadId.xy)], pSrt->m_maxFogDist);
	float3 positionBaseVS = float3((dispatchThreadId.xy * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f);
	float3 N = normalize(positionBaseVS);

	float2 cosSinRefAngle = normalize(float2(dot(N, pSrt->m_sampleDirVS.xyz),  dot(N, pSrt->m_sampleRightVS.xyz)));
	float angle = acos(clamp(cosSinRefAngle.y > 0 ? cosSinRefAngle.x : -cosSinRefAngle.x, -0.9999999f, 0.9999999f));

	float sampleArrayIndex = saturate(angle * pSrt->m_sampleAngleInfo.y + pSrt->m_sampleAngleInfo.z) * pSrt->m_sampleAngleInfo.w;
	int iSampleArrayIndex = int(min(sampleArrayIndex, pSrt->m_sampleAngleInfo.w-1));

	float sampleRayInfo = pSrt->m_txSampleInfoBuffer[int2(iSampleArrayIndex, 0)].w;
	float4 clipVector = pSrt->m_txSampleInfoBuffer[int2(iSampleArrayIndex, 1)];
	float invRefRange = 1.0f / (pSrt->m_faceLightSrc ? clipVector.w : sampleRayInfo);
	float sampleIdxOfs = pSrt->m_faceLightSrc ? -sampleRayInfo * invRefRange : 0;

	float inNumSteps = 1.0f / pSrt->m_numSteps;

	float stepDepthVS = depthVS * inNumSteps;
	float curDepthVS = stepDepthVS * pSrt->m_ditherSamples[dispatchThreadId.x % 4][dispatchThreadId.y % 4];

	curDepthVS += stepDepthVS * pSrt->m_numSteps;

	float baseLightCastDist = dot(positionBaseVS, pSrt->m_sunDirectionVS.xyz);

	float curDistAlongCastRay = curDepthVS * baseLightCastDist;
	float stepDistAlongCastRay = stepDepthVS * baseLightCastDist;

	float baseClipDist = dot(positionBaseVS, clipVector.xyz) * (pSrt->m_faceLightSrc ? 1.0f : clipVector.w);

	float curClipDist = curDepthVS * baseClipDist;
	float stepClipDist = stepDepthVS * baseClipDist;
	float maxClipDist = abs(pSrt->m_maxSunShadowDist * baseClipDist);

	float basePositionYWS = dot(positionBaseVS, pSrt->m_viewToWorldVecY.xyz);

	float curPositionYWS = curDepthVS * basePositionYWS + pSrt->m_viewToWorldVecY.w;
	float stepPositionYWS = stepDepthVS * basePositionYWS;
	float pixelY = pSrt->m_fogHeightStart - curPositionYWS;
	float cameraY = pSrt->m_fogHeightStart - pSrt->m_cameraPosY;

	float2 screenUv = (dispatchThreadId.xy + 0.5f) * pSrt->m_screenUvScale;

//	float accuFogIntensity = GetFogDensity(pixelY - stepPositionYWS, cameraY, (curDepthVS + stepDepthVS) / N.z, stepPositionYWS / N.z, pSrt);

	float4 volColor = float4(0, 0, 0, 1);
	for (int i = 0; i < pSrt->m_numSteps; i++)
	{
		float sunShadowInfo = 1.0f;

		if (abs(curClipDist) < maxClipDist)
		{
			float sampleLogDist	= ConvertDistToLogSpace(curClipDist);
			int sampleIndex	= round(sampleLogDist * invRefRange + sampleIdxOfs);

			float distAlongSun = pSrt->m_txAngleSampleBuffer[int2(iSampleArrayIndex, sampleIndex)];
			sunShadowInfo = distAlongSun > curDistAlongCastRay ? 1.0f : 0;
		}

		float distRatio = curDepthVS * pSrt->m_invMaxFogDist;
		float4 fNodeColor = pSrt->m_txFogInfoGrid.SampleLevel(pSrt->m_sSamplerLinear, float3(screenUv, pow(distRatio, pSrt->m_powerCurveExp)), 0);

		float density = GetFogDensity(pixelY, cameraY, curDepthVS / N.z, stepDepthVS / N.z, pSrt) * sunShadowInfo;

//		density = max(accuFogIntensity - density, 0.0f);
//		accuFogIntensity = density;

		volColor.xyz = lerp(volColor.xyz, fNodeColor.xyz, density);
		volColor.w *= (1.0f - density);

		curDistAlongCastRay -= stepDistAlongCastRay;
		curClipDist -= stepClipDist;
		curDepthVS -= stepDepthVS;
		pixelY += stepPositionYWS;
	}

	pSrt->m_rwbVolAccBuffer[dispatchThreadId.xy] = float4(volColor.xyz, 1.0f - volColor.w);
}

struct SunAndAmbientCreateAccBufferSrt
{
	RWTexture2D<float4>			m_rwbVolAccBuffer;
	RWTexture2D<float>			m_rwbVolAccBuffer1;
	RWTexture2D<float>			m_rwbVolVisiBuffer;
	RWTexture2D<float>			m_rwbVolNoiseShaftBuffer;
	RWTexture2D<float>			m_rwbVolReferIntensity;
	Texture2D<float4>			m_txSkyFogTex;
	Texture2D<float>			m_txDepthVS;
	Texture2D<float>			m_txVolNoiseBuffer;
	Texture2D<float>			m_invSumGlobalNoise;
	Texture2D<float3>			m_txVolProbeFogColor;
	Texture2D<float>			m_txMaskSampleBuffer;
	Texture3D<float3>			m_txCausticTexture;
	Texture2D<float4>			m_txSampleInfoBuffer;
	Texture2D<float>			m_txAngleSampleBuffer;
	Texture2D<float>			m_txNoiseShaftTexture;
	SamplerState				m_sSamplerLinear;
	float4						m_sampleDirVS;
	float4						m_sampleRightVS;
	float4						m_sunDirectionVS;
	float4						m_sampleAngleInfo;
	float4						m_viewToWorldVecY;
	float4						m_screenScaleOffset;
	float4						m_fogColor;
	float4						m_nearFogColor;
	float4						m_sunDirectionWS;
	float4						m_sunColor;
	float4						m_viewToWorldMat[3];
	float3						m_cameraPosition;
	float						m_fogMaxDensity;
	float						m_fogOcclusion;
	int							m_numSteps;
	float						m_maxFogDist;
	float						m_maxSunShadowDist;
	float						m_fogHeightStart;
	float						m_fogHeightRange;
	float						m_fogCameraHeightExp;
	float						m_fogDistanceStart;
	float						m_fogDistanceEnd;
	float						m_cameraPosY;
	float						m_fogDensity;
	float						m_distanceGamma;
	float						m_angleOffset;
	float						m_mipfadeDistance;
	float						m_mipFogAlphaDensity;
	float						m_sunVolumetricsAffectSky;
	float						m_sunDensity;
	float						m_skyDist;
	uint						m_faceLightSrc;
	uint						m_pad1;
	float						m_waterPlaneHeight;
	float3						m_absorptionCoef;
	float						m_cameraDirY;
	float						m_drakeDepth;
	uint						m_useProbeForFogColor;
	float						m_fogNearColorPreservation;
	float2						m_pixelScaleXY;
	float						m_causticPlayTime;
	float						m_noiseSharpness;
	float						m_fogAffectSky;
	float						m_causticScale;
	float2						m_causticMoveVec;
	float						m_causticIntensityScale;
	float						m_causticIntensityExponet;
	float						m_causticDirectionalIntensity;
	float						m_causticAmbientIntensity;
	float						m_causticMiePhaseFuncG1;
	float						m_causticMiePhaseFuncGW1;
	float						m_causticMiePhaseFuncG2;
	float						m_causticMiePhaseFuncGW2;
	float						m_underWaterFogFadeColorR;
	float						m_underWaterFogFadeColorG;
	float						m_underWaterFogFadeColorB;
	float						m_underWaterFogIntensityInShadow;
	float						m_horizonBlendHeight;
	float						m_horizonBlendHardness;
	float						m_horizonBlendTightness;
	float						m_horizonBlendTightnessRange;
	float3						m_horizonBlendColor;
	float						m_underWaterVisiScale;
	float						m_underWaterVisiPowerCurve;
	float						m_underWaterColorShift;
	float						m_fogScaleOnSky;
};

float2 GetFogDensityIntegral(float pixelY, float cameraY, float depthVs, SunAndAmbientCreateAccBufferSrt* pSrt : S_SRT_DATA)
{
	return GetFogDensityIntegralS(pixelY, cameraY, depthVs, pSrt->m_distanceGamma,
								  pSrt->m_fogDistanceStart, pSrt->m_fogDensity, pSrt->m_fogMaxDensity, 
								  pSrt->m_fogHeightStart, pSrt->m_fogHeightRange, pSrt->m_fogCameraHeightExp,
								  pSrt->m_fogDistanceEnd);
}

float GetSunDensityIntegral(float depthVs, float sunDensity)
{
	float density = sunDensity * depthVs;
	density = 1.0-saturate(exp2(-density));
	return density;
}

//g_noiseScroll = the direction you want the noise to move
//g_noiseScale = size of the noise
//g_noiseSharpness = changes the look of the noise to produce something more crips


//g_distortionTimeScale = speed the noise distorts
//g_distortionScale = size of the noise
//g_distortionStrength = how much distortion


//g_density = overall thickness



float SampleDistortion(Texture3D<float4> txNoiseDistortion, SamplerState samplerLinear, float3 positionWS, float distortionScale, float distortionStrength, float time, float3 distortionTimeScale, float mipLevel)
{
	float3 noise = txNoiseDistortion.SampleLevel(samplerLinear, positionWS * distortionScale + time * distortionTimeScale, mipLevel).xyz;
	float finalNoise = noise.x * noise.y * noise.z;
	return finalNoise * distortionStrength;
}

float SampleNoise(Texture3D<float4> txNoise, Texture3D<float4> txNoiseDistortion, SamplerState samplerLinear, float3 positionWS, float3 noiseScroll, float time, float noiseScale, 
				  float noiseSharpness, float distortionScale, float distortionStrength, float3 distortionTimeScale, float mipLevel)
{
	float sampleDistort = SampleDistortion(txNoiseDistortion, samplerLinear, positionWS, distortionScale, distortionStrength, time, distortionTimeScale, mipLevel);
	float3 noise = pow(txNoise.SampleLevel(samplerLinear, (positionWS.xyz + sampleDistort + noiseScroll * time) * noiseScale, mipLevel).xyz, noiseSharpness);

	float finalNoise = 0.0f;
	finalNoise += noise.x ;
	finalNoise += noise.y * noise.y;
	finalNoise += noise.z * noise.z * noise.z * noise.z;

	finalNoise = saturate(finalNoise / 3.0f);
	return finalNoise;
}

float3 CalculateFogColor(out float fogOcclusion, out float2 density, out float3 oMipFogColor, bool bUseProbeFogColor, float3 probeFogColor, bool isSky, float pixelY, float cameraY, float pixelDepth, float3 pixelDirWS, SunAndAmbientCreateAccBufferSrt* pSrt)
{
	float3 fogColor = 0;

	density = GetFogDensityIntegral(pixelY, cameraY, pixelDepth, pSrt);
	fogOcclusion = pSrt->m_fogOcclusion;

	if(isSky)
	{
		density.x *= pSrt->m_fogAffectSky;
		fogOcclusion *= pSrt->m_fogAffectSky;
	}

	float2 texUvCoord = GetFogSphericalCoords(pixelDirWS, pSrt->m_angleOffset);
	float numMipLevels = 8;
	float mipBias = (1.0-sqrt(saturate((pixelDepth-pSrt->m_mipfadeDistance)/(numMipLevels*pSrt->m_mipfadeDistance))))*numMipLevels;
	float4 mipFogColor = pSrt->m_txSkyFogTex.SampleLevel(pSrt->m_sSamplerLinear, texUvCoord, mipBias);	
	oMipFogColor = mipFogColor.xyz;

	// Ambient lighting contribution
	if(density.x > 0.00001)
	{
		density.x = lerp(density.x, density.x * mipFogColor.w, pSrt->m_mipFogAlphaDensity);

		if (bUseProbeFogColor)
		{
			fogColor = probeFogColor * pSrt->m_fogColor.xyz;
		}
		else
		{
			fogColor = mipFogColor.xyz;
			// Tint
			fogColor *= pSrt->m_fogColor.xyz;

			// Near fog color
			if(pSrt->m_nearFogColor.w > 0.0)
			{
				float nearFogColorFalloff = saturate(pixelDepth/(pSrt->m_nearFogColor.w+0.01));
				fogColor = lerp(pSrt->m_nearFogColor.xyz, fogColor, nearFogColorFalloff);
			}
		}

		fogColor *= density.x;	
	}

	return fogColor;
}

void CreateSunAndAmbientAccBuffer(uint3 dispatchThreadId, SunAndAmbientCreateAccBufferSrt* pSrt, bool bHasVolShadow)
{
	float rawDepthVS = pSrt->m_txDepthVS[int2(dispatchThreadId.xy)];
	float depthVS = min(pSrt->m_txDepthVS[int2(dispatchThreadId.xy)], pSrt->m_maxFogDist);
	float3 positionBaseVS = float3((dispatchThreadId.xy * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f);
	float3 N = normalize(positionBaseVS);

	float2 ditherFrac = (frac(dispatchThreadId.yx * 0.4 * 0.434 + dispatchThreadId.xy * 0.434) - 0.5f) * 2.0f;

	float3 positionVS = float3(((dispatchThreadId.xy + 0.5f) * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f) * rawDepthVS;

	float3 posDirWS = float3(dot(pSrt->m_viewToWorldMat[0].xyz, positionVS),
							 dot(pSrt->m_viewToWorldMat[1].xyz, positionVS),
							 dot(pSrt->m_viewToWorldMat[2].xyz, positionVS));

	float3 positionWS = posDirWS + float3(pSrt->m_viewToWorldMat[0].w, pSrt->m_viewToWorldMat[1].w, pSrt->m_viewToWorldMat[2].w);

	float3 pixelDirWS = normalize(positionWS - pSrt->m_cameraPosition);

	float2 uv = dispatchThreadId.xy * pSrt->m_pixelScaleXY;
	float2 ditherUv = uv + ditherFrac * pSrt->m_pixelScaleXY;

	float pixelDepth = rawDepthVS/N.z;
	bool isSky = rawDepthVS > pSrt->m_skyDist;

	float2 cosSinRefAngle = normalize(float2(dot(N, pSrt->m_sampleDirVS.xyz),  dot(N, pSrt->m_sampleRightVS.xyz)));
	float angle = acos(clamp(cosSinRefAngle.y > 0 ? cosSinRefAngle.x : -cosSinRefAngle.x, -0.9999999f, 0.9999999f));

	float sampleArrayIndex = saturate(angle * pSrt->m_sampleAngleInfo.y + pSrt->m_sampleAngleInfo.z) * pSrt->m_sampleAngleInfo.w;
	int iSampleArrayIndex = int(min(sampleArrayIndex, pSrt->m_sampleAngleInfo.w-1));

	float inNumSteps = 1.0f / pSrt->m_numSteps;

	float stepDepthVS = depthVS * inNumSteps;
	float curDepthVS = stepDepthVS * (ditherFrac.x * 0.5f + 0.5f);

	curDepthVS += stepDepthVS * pSrt->m_numSteps;

	float baseLightCastDist = dot(positionBaseVS, pSrt->m_sunDirectionVS.xyz);

	float curDistAlongCastRay = curDepthVS * baseLightCastDist;
	float stepDistAlongCastRay = stepDepthVS * baseLightCastDist;

	// Depth-sample Logic Start //
	float sampleRayInfo = pSrt->m_txSampleInfoBuffer[int2(iSampleArrayIndex, 0)].w;
	float4 clipVector = pSrt->m_txSampleInfoBuffer[int2(iSampleArrayIndex, 1)];
	float invRefRange = 1.0f / (pSrt->m_faceLightSrc ? clipVector.w : sampleRayInfo);
	float sampleIdxOfs = pSrt->m_faceLightSrc ? -sampleRayInfo * invRefRange : 0;
	float baseClipDist = dot(positionBaseVS, clipVector.xyz) * (pSrt->m_faceLightSrc ? 1.0f : clipVector.w);

	// Depth-sample Logic End //

	float curClipDist = curDepthVS * baseClipDist;
	float stepClipDist = stepDepthVS * baseClipDist;
	float maxClipDist = abs(pSrt->m_maxSunShadowDist * baseClipDist);

	float pixelY = pSrt->m_fogHeightStart - positionWS.y;
	float cameraY = pSrt->m_fogHeightStart - pSrt->m_cameraPosY;

	float2 density;
	float fogOcclusion, sunFactor;

	// Ambient lighting contribution
	float3 mipFogColor;
	float3 fogColor = pSrt->m_txVolProbeFogColor.SampleLevel(pSrt->m_sSamplerLinear, uv, 0);
	fogColor = CalculateFogColor(fogOcclusion, density, mipFogColor, pSrt->m_useProbeForFogColor, fogColor, isSky, pixelY, cameraY, pixelDepth, pixelDirWS, pSrt);

	// Sun contribution
	sunFactor = -dot(pSrt->m_sunDirectionWS.xyz, pixelDirWS)*0.5+0.5;
	sunFactor = pow(sunFactor, pSrt->m_sunDirectionWS.w) * pSrt->m_sunColor.w;
	
	if(isSky)
		sunFactor *= pSrt->m_sunVolumetricsAffectSky;

	float a = min(pSrt->m_maxFogDist, pixelDepth);
	float b = pSrt->m_maxFogDist + pSrt->m_sunDensity;
	float a2 = a * a;
	float a3 = a2 * a;
	float b2 = b * b;
	float b3 = b2 * b;
	float volumeDensity = -a*(a3-4*a2*b+6*a*b2-4*b3)/(4*b3);
	float sunIntensity = volumeDensity * sunFactor / 32.0f; 

	float sunColorLuminance = dot(pSrt->m_sunColor.xyz, s_luminanceVector);
	float refDensity = density.y + sunIntensity * sunColorLuminance / (pSrt->m_fogDensity > 0.0f ? pSrt->m_fogDensity : 1.0f);

	float avgVisi = 0;
	float sumWeight = 0;
	if(sunFactor > 0)
	{
		float sunSampleCount = 0;
		for (int i = 0; i < pSrt->m_numSteps; i++)
		{
			int sunShadowInfo = 0;

			if (abs(curClipDist) < maxClipDist)
			{
				if (bHasVolShadow)
				{
					float sampleLogDist	= ConvertDistToLogSpace(curClipDist);

					// Depth-sample Logic Start //
					int sampleIndex = round(sampleLogDist * invRefRange + sampleIdxOfs);
					float distAlongSun = pSrt->m_txAngleSampleBuffer[int2(iSampleArrayIndex, sampleIndex)];
					// Depth-sample Logic End //

					sunShadowInfo = distAlongSun > curDistAlongCastRay ? 1 : 0;
				}
				else
				{
					sunShadowInfo = 1;
				}
			}

			// Aesthetically-tuned falloff, not based on physical phenomena
			float percentDepth = 1.0 - curDepthVS/(pSrt->m_maxFogDist + pSrt->m_sunDensity);
			sunSampleCount += sunShadowInfo * (percentDepth * percentDepth * percentDepth) * stepDepthVS ;	
			float weight = sqrt((float)i / pSrt->m_numSteps);
			avgVisi += sunShadowInfo * weight;
			sumWeight += weight;

			curDepthVS -= stepDepthVS;

			curDistAlongCastRay -= stepDistAlongCastRay;
			curClipDist -= stepClipDist;
		}
		sunFactor *= sunSampleCount / pSrt->m_numSteps;
	
		float3 sunFogColor = pSrt->m_sunColor.xyz * sunFactor;
				
		fogColor += sunFogColor;
	}

	// Density of 0 means no background is removed, 1 means all background is removed.  Fog color must be premultiplied by density.
	float finalDensity = density.x * fogOcclusion;

	if (pSrt->m_noiseSharpness > 0.0001f && pSrt->m_useProbeForFogColor == 0)
	{
		float invAvgGloalNoise = pSrt->m_invSumGlobalNoise[int2(0, 0)];
		float noise = pSrt->m_txVolNoiseBuffer.SampleLevel(pSrt->m_sSamplerLinear, ditherUv, 0) * invAvgGloalNoise;
		fogColor *= noise;
	}

	float3 normalWS = normalize(posDirWS);
	float horizonBlend = 1.0 - saturate( (normalWS.y - pSrt->m_horizonBlendHeight)/pSrt->m_horizonBlendHardness);	// Construct Y position from the normal, and a user-specified depth
	if(!isSky)
		horizonBlend *= saturate((pixelDepth - pSrt->m_horizonBlendTightness) / pSrt->m_horizonBlendTightnessRange);
	else
		fogColor.xyz *= pSrt->m_fogScaleOnSky;

	horizonBlend *= horizonBlend;

	// Horizon blend
	float4 finalColor;
	finalColor.xyz = lerp(fogColor.xyz, pSrt->m_horizonBlendColor * mipFogColor, horizonBlend);
	finalColor.w = saturate(1.0f - (1.0f - finalDensity) * (1.0f - horizonBlend));

	pSrt->m_rwbVolAccBuffer[dispatchThreadId.xy] = finalColor;
	pSrt->m_rwbVolVisiBuffer[dispatchThreadId.xy] = avgVisi / sumWeight;
	pSrt->m_rwbVolReferIntensity[dispatchThreadId.xy] = refDensity;
}

[numthreads(8, 8, 1)]
void CS_CreateSunAndAmbientAccBuffer(uint3 dispatchThreadId : SV_DispatchThreadId, SunAndAmbientCreateAccBufferSrt* pSrt : S_SRT_DATA)
{
	CreateSunAndAmbientAccBuffer(dispatchThreadId, pSrt, true);
}

[numthreads(8, 8, 1)]
void CS_CreateSunAndAmbientAccNoVisiBuffer(uint3 dispatchThreadId : SV_DispatchThreadId, SunAndAmbientCreateAccBufferSrt* pSrt : S_SRT_DATA)
{
	CreateSunAndAmbientAccBuffer(dispatchThreadId, pSrt, false);
}

[numthreads(8, 8, 1)]
void CS_CreateNoiseAndShaftBuffer(uint3 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, SunAndAmbientCreateAccBufferSrt* pSrt : S_SRT_DATA)
{
	float rawDepthVS = min(pSrt->m_txDepthVS[int2(dispatchThreadId.xy)], 2000.0f);
	float depthVS = min(rawDepthVS, pSrt->m_maxFogDist);

	float2 ditherFrac = (frac(dispatchThreadId.yx * 0.4 * 0.434 + dispatchThreadId.xy * 0.434) - 0.5f) * 2.0f;

	float3 positionBaseVS = float3(((dispatchThreadId.xy + 0.5f) * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f);
	float3 N = normalize(positionBaseVS);

	float3 positionVS = positionBaseVS * rawDepthVS;

	float2 uv = dispatchThreadId.xy * pSrt->m_pixelScaleXY;
	float2 ditherUv = uv + ditherFrac * pSrt->m_pixelScaleXY;

	float3 positionWS = float3(dot(pSrt->m_viewToWorldMat[0], float4(positionVS, 1.0f)),
							   dot(pSrt->m_viewToWorldMat[1], float4(positionVS, 1.0f)),
							   dot(pSrt->m_viewToWorldMat[2], float4(positionVS, 1.0f)));

	float3 pixelOffset = positionWS - pSrt->m_cameraPosition;
	float3 pixelDirWS = normalize(pixelOffset);

	float3 pixelDirWSNorm = pixelOffset / max(rawDepthVS, 0.0001f);

	float pixelDepth = rawDepthVS/N.z;

	float2 cosSinRefAngle = normalize(float2(dot(N, pSrt->m_sampleDirVS.xyz),  dot(N, pSrt->m_sampleRightVS.xyz)));
	float angle = acos(clamp(cosSinRefAngle.y > 0 ? cosSinRefAngle.x : -cosSinRefAngle.x, -0.9999999f, 0.9999999f));

	float sampleArrayIndex = saturate(angle * pSrt->m_sampleAngleInfo.y + pSrt->m_sampleAngleInfo.z) * pSrt->m_sampleAngleInfo.w;
	int iSampleArrayIndex = int(min(sampleArrayIndex, pSrt->m_sampleAngleInfo.w-1));

	float inNumSteps = 1.0f / pSrt->m_numSteps;

	float stepDepthVS = depthVS * inNumSteps;
	float curDepthVS = stepDepthVS * (ditherFrac.x * 0.5f + 0.5f);

	curDepthVS += stepDepthVS * pSrt->m_numSteps;

	float baseLightCastDist = dot(positionBaseVS, pSrt->m_sunDirectionVS.xyz);

	// Depth-sample Logic Start //
	float sampleRayInfo = pSrt->m_txSampleInfoBuffer[int2(iSampleArrayIndex, 0)].w;
	float4 clipVector = pSrt->m_txSampleInfoBuffer[int2(iSampleArrayIndex, 1)];
	float invRefRange = 1.0f / (pSrt->m_faceLightSrc ? clipVector.w : sampleRayInfo);
	float sampleIdxOfs = pSrt->m_faceLightSrc ? -sampleRayInfo * invRefRange : 0;
	float baseClipDist = dot(positionBaseVS, clipVector.xyz) * (pSrt->m_faceLightSrc ? 1.0f : clipVector.w);

	// Depth-sample Logic End //

	float curClipDist = curDepthVS * baseClipDist;
	float stepClipDist = stepDepthVS * baseClipDist;
	float maxClipDist = abs(pSrt->m_maxSunShadowDist * baseClipDist);

	float pixelY = pSrt->m_fogHeightStart - positionWS.y;
	float cameraY = pSrt->m_fogHeightStart - pSrt->m_cameraPosY;

	float fogOcclusion, sunFactor = 1.0f;

	float3 fogColor = 0;
	float3 absorptionFactor = 1;

	float avgCaustic = 0;

	float maxDepthVS = 0;
	float minDepthVS = 0;
	float sumCausticWeight = 0;

	float cosCastAngle = dot(pixelDirWS.xyz, pSrt->m_sunDirectionWS.xyz);
	float sinCastAngle = sqrt(max(1.0f - cosCastAngle * cosCastAngle, 0.0f));

	float maxCastDist = 20.0f;

	if (pixelDirWS.y > 0.0f)
	{
		float maxT = (pSrt->m_waterPlaneHeight - pSrt->m_cameraPosition.y) / pixelDirWS.y;
		maxCastDist = min(maxT, maxCastDist);
	}

	maxDepthVS = min(N.z * maxCastDist, rawDepthVS);

	float sumCausticValue = 0;

	int numSamples = pSrt->m_numSteps;
	float maxLogDepthVS = log2(1.0f + maxDepthVS);
	float stepLogDepthVS = maxLogDepthVS / numSamples;
	float curLogDepthVS = stepLogDepthVS * (ditherFrac.x * 0.5f + 0.5f);

	for (int i = 0; i < numSamples; i++)
	{
		float thisDepthVS = exp2(curLogDepthVS) - 1.0f;
		float sampleLogDist	= ConvertDistToLogSpace(thisDepthVS * baseClipDist);
		int sampleIndex = round(sampleLogDist * invRefRange + sampleIdxOfs);

		float fadeRadius = thisDepthVS / N.z * sinCastAngle;
		float3 curPosWS = pixelDirWSNorm * thisDepthVS + pSrt->m_cameraPosition.xyz;

		if (pSrt->m_waterPlaneHeight - curPosWS.y > 0.0f)
		{
			float t = (curPosWS.y - pSrt->m_waterPlaneHeight) / pSrt->m_sunDirectionWS.y;

			float3 collisionWS = curPosWS - t * pSrt->m_sunDirectionWS.xyz;
					
			float causticWeight = 1.0f;//exp(-t*0.2f-fadeRadius*0.5f);

			float distortRadius = t / 500.0f;

			float causticBlend = pSrt->m_txMaskSampleBuffer[int2(iSampleArrayIndex, sampleIndex)];

			float causticValue = causticBlend * causticWeight;
			sumCausticValue += causticValue;
			sumCausticWeight += causticWeight;
		}

		curLogDepthVS += stepLogDepthVS;
	}

	float invAvgGloalNoise = pSrt->m_invSumGlobalNoise[int2(0, 0)];
	float noise = (pSrt->m_noiseSharpness > 0.0001f && pSrt->m_useProbeForFogColor == 0) ? (pSrt->m_txVolNoiseBuffer.SampleLevel(pSrt->m_sSamplerLinear, ditherUv, 0) * invAvgGloalNoise) : 1.0f;
	avgCaustic = sumCausticWeight > 0.0f ? (0.2f + (sumCausticValue / sumCausticWeight) * max(-dot(pSrt->m_sunDirectionWS.xyz, pixelDirWS), 0.0f) * 0.8f) * noise : 0.0f;

	pSrt->m_rwbVolNoiseShaftBuffer[dispatchThreadId.xy] = saturate(avgCaustic);
}

float miePhase(float3 vd, float3 ld, float g)
{
    float mu = dot(vd.xyz,ld.xyz);

	float sqrG = g * g;

    float phaseM = 3 / (8 * 3.14159) * (saturate(1 - sqrG) * (1 + mu * mu))/((2 + sqrG) * pow(max(1 + sqrG - 2 * g * mu, 0.00001f), 1.5));
    return phaseM;
}

void CreateUnderWaterSunAndAmbientAccBuffer(uint3 dispatchThreadId, uint groupIndex, SunAndAmbientCreateAccBufferSrt* pSrt, bool bHasVolShadow)
{
	float rawDepthVS = min(pSrt->m_txDepthVS[int2(dispatchThreadId.xy)], 2000.0f);
	float depthVS = min(rawDepthVS, pSrt->m_maxFogDist);

	float2 ditherFrac = (frac(dispatchThreadId.yx * 0.4 * 0.434 + dispatchThreadId.xy * 0.434) - 0.5f) * 2.0f;

	float3 positionBaseVS = float3(((dispatchThreadId.xy + 0.5f) * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f);
	float3 N = normalize(positionBaseVS);

	float3 positionVS = positionBaseVS * rawDepthVS;

	float2 uv = dispatchThreadId.xy * pSrt->m_pixelScaleXY;
	float2 ditherUv = uv + ditherFrac * pSrt->m_pixelScaleXY;

	float3 positionWS = float3(dot(pSrt->m_viewToWorldMat[0], float4(positionVS, 1.0f)),
							   dot(pSrt->m_viewToWorldMat[1], float4(positionVS, 1.0f)),
							   dot(pSrt->m_viewToWorldMat[2], float4(positionVS, 1.0f)));

	float3 pixelOffset = positionWS - pSrt->m_cameraPosition;
	float3 pixelDirWS = normalize(pixelOffset);

	float3 pixelDirWSNorm = pixelOffset / max(rawDepthVS, 0.0001f);

	float pixelDepth = rawDepthVS/N.z;
	bool isSky = rawDepthVS > pSrt->m_skyDist;

	float2 cosSinRefAngle = normalize(float2(dot(N, pSrt->m_sampleDirVS.xyz),  dot(N, pSrt->m_sampleRightVS.xyz)));
	float angle = acos(clamp(cosSinRefAngle.y > 0 ? cosSinRefAngle.x : -cosSinRefAngle.x, -0.9999999f, 0.9999999f));

	float sampleArrayIndex = saturate(angle * pSrt->m_sampleAngleInfo.y + pSrt->m_sampleAngleInfo.z) * pSrt->m_sampleAngleInfo.w;
	int iSampleArrayIndex = int(min(sampleArrayIndex, pSrt->m_sampleAngleInfo.w-1));

	float inNumSteps = 1.0f / pSrt->m_numSteps;

	float stepDepthVS = depthVS * inNumSteps;
	float curDepthVS = stepDepthVS * (ditherFrac.x * 0.5f + 0.5f);

	curDepthVS += stepDepthVS * pSrt->m_numSteps;

	float baseLightCastDist = dot(positionBaseVS, pSrt->m_sunDirectionVS.xyz);

	float curDistAlongCastRay = curDepthVS * baseLightCastDist;
	float stepDistAlongCastRay = stepDepthVS * baseLightCastDist;

	// Depth-sample Logic Start //
	float sampleRayInfo = pSrt->m_txSampleInfoBuffer[int2(iSampleArrayIndex, 0)].w;
	float4 clipVector = pSrt->m_txSampleInfoBuffer[int2(iSampleArrayIndex, 1)];
	float invRefRange = 1.0f / (pSrt->m_faceLightSrc ? clipVector.w : sampleRayInfo);
	float sampleIdxOfs = pSrt->m_faceLightSrc ? -sampleRayInfo * invRefRange : 0;
	float baseClipDist = dot(positionBaseVS, clipVector.xyz) * (pSrt->m_faceLightSrc ? 1.0f : clipVector.w);

	// Depth-sample Logic End //

	float curClipDist = curDepthVS * baseClipDist;
	float stepClipDist = stepDepthVS * baseClipDist;
	float maxClipDist = abs(pSrt->m_maxSunShadowDist * baseClipDist);

	float pixelY = pSrt->m_fogHeightStart - positionWS.y;
	float cameraY = pSrt->m_fogHeightStart - pSrt->m_cameraPosY;

	// Find what percentage of the line from the camera to the pixel is underwater vs above water
	float waterPlaneHeight = pSrt->m_waterPlaneHeight;
	float pixelHeight = positionWS.y;
	float drakeDepth = pSrt->m_drakeDepth;
	float cameraHeight = pSrt->m_cameraPosition.y;
	float sceneDepth = rawDepthVS;

	float pixelUnderwaterHeight = waterPlaneHeight - pixelHeight;

	float lightDistance = 0;
	float adjCameraHeight = waterPlaneHeight - cameraHeight;

	if (adjCameraHeight > 0.0f || pixelUnderwaterHeight > 0.0f)
	{
		pixelUnderwaterHeight = max(pixelUnderwaterHeight, 0.0f);

		float differenceHeight = abs(pixelHeight - cameraHeight) + 0.00001f;
		float percentageUnderWater = abs(max(0, adjCameraHeight)-max(0, pixelUnderwaterHeight)) / differenceHeight ;

		float drakeDepthHack = abs(sceneDepth - drakeDepth);
		float startDistanceHack = lerp(pixelUnderwaterHeight, drakeDepthHack, pSrt->m_fogNearColorPreservation);
		pixelUnderwaterHeight = min(startDistanceHack, pixelUnderwaterHeight);

		// Find the total distance light travels through the water
		lightDistance = max(0, pixelUnderwaterHeight) + drakeDepthHack * percentageUnderWater;
	}

	float2 density;
	float fogOcclusion;

	float dotLightView = dot(pSrt->m_sunDirectionWS.xyz, pixelDirWS); 
	float sunFactor = -dotLightView*0.499+0.5;
	sunFactor = pow(sunFactor, pSrt->m_sunDirectionWS.w) * pSrt->m_sunColor.w;
	sunFactor *= (abs(pSrt->m_underWaterVisiPowerCurve) < 0.0001f ? 1.0f : saturate(pow((dotLightView * 0.4999f + 0.5f) * pSrt->m_underWaterVisiScale, pSrt->m_underWaterVisiPowerCurve)));

	// Ambient lighting contribution
	float3 mipFogColor;
	float3 fogColor = pSrt->m_txVolProbeFogColor.SampleLevel(pSrt->m_sSamplerLinear, uv, 0);
	fogColor = CalculateFogColor(fogOcclusion, density, mipFogColor, pSrt->m_useProbeForFogColor, fogColor, isSky, pixelY, cameraY, pixelDepth, pixelDirWS, pSrt);

	float shadowInfo = 0.0f;
	float avgCaustic = 0;
	float sumWeight = 0;

	float maxDepthVS = 0;
	float minDepthVS = 0;
	float sumCausticValue = 0;
	float sumCausticWeight = 0;

	float noise = 1.0f;

	float a = min(pSrt->m_maxFogDist, pixelDepth);
	float b = pSrt->m_maxFogDist + pSrt->m_sunDensity;
	float a2 = a * a;
	float a3 = a2 * a;
	float b2 = b * b;
	float b3 = b2 * b;
	float volumeDensity = -a*(a3-4*a2*b+6*a*b2-4*b3)/(4*b3);
	float sunIntensity = volumeDensity * sunFactor / 32.0f; 

	float sunColorLuminance = dot(pSrt->m_sunColor.xyz, s_luminanceVector);
	float refDensity = density.y + sunIntensity * sunColorLuminance / (pSrt->m_fogDensity > 0.0f ? pSrt->m_fogDensity : 1.0f);

//	float noiseShaft = pSrt->m_txNoiseShaftTexture.SampleLevel(pSrt->m_sSamplerLinear, ditherUv, 0);
	{
		float cosCastAngle = dot(pixelDirWS.xyz, pSrt->m_sunDirectionWS.xyz);
		float sinCastAngle = sqrt(max(1.0f - cosCastAngle * cosCastAngle, 0.0f));

		float maxCircleRadius = 10.0f;
		float maxTravelDist = 10.0f * abs(cosCastAngle);

		float maxCastDist = 20.0f;
	/*		if (sinCastAngle <= 0.0f)
			maxCastDist = 1000.0f;
		else
			maxCastDist = maxCircleRadius / sinCastAngle;
	*/
		if (pixelDirWS.y > 0.0f)
		{
			float maxT = (pSrt->m_waterPlaneHeight - pSrt->m_cameraPosition.y) / pixelDirWS.y;
			maxCastDist = min(maxT, maxCastDist);
		}

		float maxCastDepthVS = log2(1.0f + N.z * maxCastDist);
		maxDepthVS = min(N.z * maxCastDist, rawDepthVS);

		int numSamples = pSrt->m_numSteps;
		{
			float maxLogDepthVS = log2(1.0f + maxDepthVS);
			float stepLogDepthVS = maxLogDepthVS / numSamples;
			float curLogDepthVS = stepLogDepthVS * (ditherFrac.x * 0.5f + 0.5f);

			float lastLogDepthVS = 0.0f;

			for (int i = 0; i < numSamples; i++)
			{
				float thisDepthVS = exp2(curLogDepthVS) - 1.0f;

				if (thisDepthVS < rawDepthVS)
				{
					float fadeRadius = thisDepthVS / N.z * sinCastAngle;
					float3 curPosWS = pixelDirWSNorm * thisDepthVS + pSrt->m_cameraPosition.xyz;

					if (pSrt->m_waterPlaneHeight - curPosWS.y > 0.0f)
					{
						float t = (curPosWS.y - pSrt->m_waterPlaneHeight) / pSrt->m_sunDirectionWS.y;

						float3 collisionWS = curPosWS - t * pSrt->m_sunDirectionWS.xyz;
					
						float causticWeight = curLogDepthVS - lastLogDepthVS;//exp(-t*0.1f);// * exp(-fadeRadius*0.5f);

						float distortRadius = t / 500.0f;

						float causticBlend = pow(pSrt->m_txCausticTexture.SampleLevel(pSrt->m_sSamplerLinear, float3(collisionWS.xz * pSrt->m_causticScale + pSrt->m_causticMoveVec, pSrt->m_causticPlayTime), min(sqrt(t) / 2, 3)).x, 2.0f); 

						float causticValue = causticBlend * causticWeight;
						sumCausticValue += causticValue;
						sumCausticWeight += causticWeight;
					}
				}

				lastLogDepthVS = curLogDepthVS;
				curLogDepthVS += stepLogDepthVS;
			}
		}

		float invAvgGloalNoise = pSrt->m_invSumGlobalNoise[int2(0, 0)];
		noise = (pSrt->m_noiseSharpness > 0.0001f && pSrt->m_useProbeForFogColor == 0) ? (pSrt->m_txVolNoiseBuffer.SampleLevel(pSrt->m_sSamplerLinear, ditherUv, 0) * invAvgGloalNoise) : 1.0f;
		float fade = miePhase(pixelDirWS.xyz, -pSrt->m_sunDirectionWS.xyz, pSrt->m_causticMiePhaseFuncG1) * pSrt->m_causticMiePhaseFuncGW1;
		fade += miePhase(pixelDirWS.xyz, -pSrt->m_sunDirectionWS.xyz, pSrt->m_causticMiePhaseFuncG2) * pSrt->m_causticMiePhaseFuncGW2;
		float causticIntensity = pSrt->m_causticAmbientIntensity + (sumCausticValue / maxCastDepthVS) * fade * pSrt->m_causticDirectionalIntensity;

		causticIntensity *= sunFactor;

		avgCaustic = sumCausticWeight > 0.0f ? causticIntensity : 0.0f;
	}

//	if(sunFactor > 0)
	{
		float maxCastDist = 20.0f;
		float maxLogShadowDist = log2(1.0f + N.z * maxCastDist);
		if (pixelDirWS.y > 0.0f)
		{
			float maxT = (pSrt->m_waterPlaneHeight - pSrt->m_cameraPosition.y) / pixelDirWS.y;
			maxCastDist = min(maxT, maxCastDist);
		}

		maxDepthVS = min(N.z * maxCastDist, rawDepthVS);

		float lastLogDepthVS = log2(1.0f + curDepthVS);
		float sunSampleCount = 0;
		for (int i = 0; i < pSrt->m_numSteps; i++)
		{
			int sunShadowInfo = 0;

			curDepthVS -= stepDepthVS;
			float curLogDepthVS = log2(1.0f + curDepthVS);
			float weight = lastLogDepthVS - curLogDepthVS;
			lastLogDepthVS = curLogDepthVS;

			if (abs(curClipDist) < maxClipDist)
			{
				if (bHasVolShadow)
				{
					float sampleLogDist	= ConvertDistToLogSpace(curClipDist);

					// Depth-sample Logic Start //
					int sampleIndex = round(sampleLogDist * invRefRange + sampleIdxOfs);
					float distAlongSun = pSrt->m_txAngleSampleBuffer[int2(iSampleArrayIndex, sampleIndex)];
					// Depth-sample Logic End //

					sunShadowInfo = (distAlongSun > curDistAlongCastRay ? 1 : 0);
				}
				else
				{
					sunShadowInfo = 1;
				}
			}

			// Aesthetically-tuned falloff, not based on physical phenomena
			//float percentDepth = 1.0 - curDepthVS/(pSrt->m_maxFogDist + pSrt->m_sunDensity);

			sunSampleCount += sunShadowInfo * weight;// * (percentDepth * percentDepth * percentDepth) * stepDepthVS ;	

			curDistAlongCastRay -= stepDistAlongCastRay;
			curClipDist -= stepClipDist;
		}

		shadowInfo = sunSampleCount / (float)maxLogShadowDist;

		sunFactor *= shadowInfo;

		float3 underWaterFogFadeColor = float3(pSrt->m_underWaterFogFadeColorR, pSrt->m_underWaterFogFadeColorG, pSrt->m_underWaterFogFadeColorB);
		float3 shiftedColor = LerpColorShift(sunFactor, underWaterFogFadeColor, lerp(underWaterFogFadeColor, float3(1.05f, 1.0f, 1.2f), pSrt->m_underWaterColorShift));
		float3 sunFogColor = pSrt->m_sunColor.xyz * shiftedColor;

		float causticValue = pow(avgCaustic * pSrt->m_causticIntensityScale, pSrt->m_causticIntensityExponet) * noise;
		fogColor += sunFogColor;

		fogColor *= lerp(causticValue * shadowInfo, 1.0f, pSrt->m_underWaterFogIntensityInShadow);
	}

	// Density of 0 means no background is removed, 1 means all background is removed.  Fog color must be premultiplied by density.
	float finalDensity = density.x * fogOcclusion;

	pSrt->m_rwbVolAccBuffer[dispatchThreadId.xy] = float4(fogColor, finalDensity);
	pSrt->m_rwbVolAccBuffer1[dispatchThreadId.xy] = lightDistance;
	pSrt->m_rwbVolVisiBuffer[dispatchThreadId.xy] = shadowInfo;
	pSrt->m_rwbVolReferIntensity[dispatchThreadId.xy] = refDensity;
}

[numthreads(8, 8, 1)]
void CS_CreateUnderWaterSunAndAmbientAccBuffer(uint3 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, SunAndAmbientCreateAccBufferSrt* pSrt : S_SRT_DATA)
{
	CreateUnderWaterSunAndAmbientAccBuffer(dispatchThreadId, groupIndex, pSrt, true);
}

[numthreads(8, 8, 1)]
void CS_CreateUnderWaterSunAndAmbientAccNoVisiBuffer(uint3 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, SunAndAmbientCreateAccBufferSrt* pSrt : S_SRT_DATA)
{
	CreateUnderWaterSunAndAmbientAccBuffer(dispatchThreadId, groupIndex, pSrt, false);
}

struct CreateFullScreenNoiseBufferSrt
{
	RWTexture2D<float>			m_rwbNoiseAccBuffer;
	RWTexture2D<float3>			m_rwbProbeFogColorBuffer;
	RWTexture2D<float>			m_rwbSumNoiseAccBuffer;
	Texture2D<float>			m_txDepthVS;
	Texture3D<float4>			m_txNoiseTexture;
	Texture3D<float4>			m_txCellVolTexture1;
	Texture3D<float4>			m_txCellVolTexture2;
	Texture3D<float4>			m_txCellVolTexture3;
	Texture3D<float4>			m_txCellVolTexture4;
	Texture3D<float4>			m_txCellVolTexture5;
	Texture3D<float4>			m_txCellVolTexture6;
	Texture3D<float4>			m_txCellVolTexture7;
	Texture3D<float>			m_txCellVolScaleTex;
	SamplerState				m_sSamplerLinear;
	float4						m_screenScaleOffset;
	float4						m_viewToWorldMat[3];
	float3						m_cameraPosition;
	int							m_numSteps;
	float3						m_noiseScroll;
	float						m_time;
	float3						m_distortionTimeScale;
	float						m_maxFogDist;
	uint2						m_noiseBufferSize;
	float						m_noiseScale;
	float						m_noiseSharpness;
	float						m_distortionScale;
	float						m_distortionStrength;
	uint						m_enableProbeColorOnFog;
	float						m_extinctionCoeff;
	float3						m_centerPositionWS;
	float						m_noiseStrengthInProbe;
	float3						m_uvwScale;
	float						m_fogMaxNoiseExtinctionCoeff;
};

[numthreads(8, 8, 1)]
void CS_CreateFullScreenNoiseBuffer(uint3 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, uint2 groupId : SV_GroupID, CreateFullScreenNoiseBufferSrt* pSrt : S_SRT_DATA)
{
	float rawDepthVS = min(pSrt->m_txDepthVS[int2(dispatchThreadId.xy) * 2], 2000.0f);
	float depthVS = min(rawDepthVS, pSrt->m_maxFogDist);

	float2 ditherFrac = (frac(dispatchThreadId.yx * 0.4 * 0.434 + dispatchThreadId.xy * 0.434) - 0.5f) * 2.0f;

	float3 positionBaseVS = float3(((dispatchThreadId.xy * 2 + 0.5f) * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f);
	float3 N = normalize(positionBaseVS);

	float3 positionVS = positionBaseVS * rawDepthVS;

	float3 positionWS = float3(dot(pSrt->m_viewToWorldMat[0], float4(positionVS, 1.0f)),
							   dot(pSrt->m_viewToWorldMat[1], float4(positionVS, 1.0f)),
							   dot(pSrt->m_viewToWorldMat[2], float4(positionVS, 1.0f)));

	float3 pixelOffset = positionWS - pSrt->m_cameraPosition;
	float3 pixelDirWS = normalize(pixelOffset);

	float3 pixelDirWSNorm = pixelOffset / max(rawDepthVS, 0.0001f);

	float pixelDepth = rawDepthVS/N.z;

	int numSamples = pSrt->m_numSteps;
	float maxCastDist = 40.0f;
	float maxLogCastDist = log2(1.0f + maxCastDist);
	float stepLogDist = maxLogCastDist / numSamples;
	float startLogDist = stepLogDist * (ditherFrac.x + ditherFrac.y) * 0.25f;
	float lastLogDist = startLogDist + stepLogDist * (numSamples - 1);

	float clampLogCastDist = log2(1.0f + rawDepthVS / N.z);

	if (clampLogCastDist < lastLogDist)
	{
		numSamples = uint(max((clampLogCastDist - startLogDist) / stepLogDist, 0.0f)) + 1;
		maxCastDist = rawDepthVS / N.z;
	}

	float currLogDist = startLogDist + stepLogDist;
	float prevDist = 0.0f;

	float3 beginPosWS = pSrt->m_cameraPosition.xyz;
	float3 uvw = (beginPosWS - pSrt->m_centerPositionWS) * pSrt->m_uvwScale + float3(ditherFrac, ditherFrac.x * ditherFrac.y) * float3(1/64.0f, 1/16.0f, 1/64.0f) * 0.25f;
	float u = uvw.x;
	float v = uvw.y;
	float w = uvw.z;

	float4 a0 = pSrt->m_txCellVolTexture1.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
	float4 a1 = pSrt->m_txCellVolTexture2.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
	float4 a2 = pSrt->m_txCellVolTexture3.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
	float4 a3 = pSrt->m_txCellVolTexture4.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
	float4 a4 = pSrt->m_txCellVolTexture5.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
	float4 a5 = pSrt->m_txCellVolTexture6.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
	float4 a6 = pSrt->m_txCellVolTexture7.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
	float probeScale = pSrt->m_txCellVolScaleTex.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);// * noise;// * a6.w;

	float3 prevProbeColor = CalculateProbeLightBidir(pixelDirWS, a0, a1, a2, a3, a4, a5, a6, probeScale, 0.0f);

	float prevNoise = SampleNoise(pSrt->m_txNoiseTexture, pSrt->m_txNoiseTexture, pSrt->m_sSamplerLinear, beginPosWS, 
								  pSrt->m_noiseScroll, pSrt->m_time, pSrt->m_noiseScale, pSrt->m_noiseSharpness, 
								  pSrt->m_distortionScale, pSrt->m_distortionStrength, pSrt->m_distortionTimeScale, 1.0f);

	float accNoiseValue = 0.0f;
	float3 accProbeFogColor = 0.0f;

	for (int i = 0; i < numSamples; i++)
	{
		float currDist = min(exp2(currLogDist) - 1.0f, maxCastDist);

		float thisDepthVS = currDist * N.z;

		float3 curPosWS = pixelDirWSNorm * thisDepthVS + pSrt->m_cameraPosition.xyz;

		if (currDist > prevDist)
		{
			float deltaDist = currDist - prevDist;
			float expD1Tau = exp(-currDist * pSrt->m_extinctionCoeff);
			float expD0Tau = exp(-prevDist * pSrt->m_extinctionCoeff);

			float integralFactor = pSrt->m_extinctionCoeff == 0.0f ? deltaDist : ((expD0Tau - expD1Tau) / pSrt->m_extinctionCoeff);

			float noise = 1.0f;
			if (pSrt->m_noiseSharpness > 0.0001f)
			{
				noise = SampleNoise(pSrt->m_txNoiseTexture, pSrt->m_txNoiseTexture, pSrt->m_sSamplerLinear, curPosWS, 
											pSrt->m_noiseScroll, pSrt->m_time, pSrt->m_noiseScale, pSrt->m_noiseSharpness, 
											pSrt->m_distortionScale, pSrt->m_distortionStrength, pSrt->m_distortionTimeScale, 1.0f);

				float avgNoise = (noise + prevNoise) * 0.5f;
				accNoiseValue += avgNoise * integralFactor;
				prevNoise = noise;
			}

			if (pSrt->m_enableProbeColorOnFog > 0)
			{
				uvw = (curPosWS - pSrt->m_centerPositionWS) * pSrt->m_uvwScale + float3(ditherFrac, ditherFrac.x * ditherFrac.y) * float3(1/64.0f, 1/16.0f, 1/64.0f) * 0.25f;
				u = uvw.x;
				v = uvw.y;
				w = uvw.z;

				a0 = pSrt->m_txCellVolTexture1.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
				a1 = pSrt->m_txCellVolTexture2.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
				a2 = pSrt->m_txCellVolTexture3.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
				a3 = pSrt->m_txCellVolTexture4.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
				a4 = pSrt->m_txCellVolTexture5.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
				a5 = pSrt->m_txCellVolTexture6.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
				a6 = pSrt->m_txCellVolTexture7.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);
				probeScale = pSrt->m_txCellVolScaleTex.SampleLevel(pSrt->m_sSamplerLinear, float3(u, v, w), 0);// * a6.w;

				float extinctionCoeff = pSrt->m_noiseStrengthInProbe == 0.0f ? pSrt->m_extinctionCoeff : (pSrt->m_fogMaxNoiseExtinctionCoeff * noise);

				expD1Tau = exp(-currDist * extinctionCoeff);
				expD0Tau = exp(-prevDist * extinctionCoeff);

				integralFactor = extinctionCoeff == 0.0f ? deltaDist : ((expD0Tau - expD1Tau) / extinctionCoeff);

				float3 probeColor = CalculateProbeLightBidir(pixelDirWS, a0, a1, a2, a3, a4, a5, a6, probeScale, 1.0f - exp(-0.5f * prevDist));

				// using simplified integration function, since other integration per sample has precision issue.
				float3 avgProbeColor = (probeColor + prevProbeColor) * 0.5f;

				accProbeFogColor += avgProbeColor * integralFactor;
				prevProbeColor = probeColor;
			}
		}

		prevDist = currDist;
		currLogDist += stepLogDist;
	}

	float finalNoise = 0;
	float3 finalFogColor = 0;
	if (all(dispatchThreadId.xy < pSrt->m_noiseBufferSize.xy))
	{
		finalNoise = accNoiseValue;
		finalFogColor = accProbeFogColor;
	}

	float sumNoise = finalNoise;
	sumNoise += LaneSwizzle(sumNoise, 0x1f, 0x00, 0x01);
	sumNoise += LaneSwizzle(sumNoise, 0x1f, 0x00, 0x08);
	sumNoise += LaneSwizzle(sumNoise, 0x1f, 0x00, 0x02);
	sumNoise += LaneSwizzle(sumNoise, 0x1f, 0x00, 0x10);
	sumNoise += LaneSwizzle(sumNoise, 0x1f, 0x00, 0x04);
	sumNoise += ReadLane(sumNoise, 0x20);

	pSrt->m_rwbNoiseAccBuffer[dispatchThreadId.xy] = finalNoise;
	pSrt->m_rwbProbeFogColorBuffer[dispatchThreadId.xy] = finalFogColor;
	if (groupIndex == 0)
		pSrt->m_rwbSumNoiseAccBuffer[groupId.xy] = ReadFirstLane(sumNoise);
}

struct SumUpNoiseBufferSrt
{
	int2						m_noiseBufferSize;
	float						m_invNumSamples;
	float						m_pad;
	RWTexture2D<float>			m_rwbSumNoiseAccBuffer;
	Texture2D<float>			m_txSrcNoiseBuffer;
};

[numthreads(8, 8, 1)]
void CS_SumUpNoiseBuffer(uint3 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, uint2 groupId : SV_GroupID, SumUpNoiseBufferSrt* pSrt : S_SRT_DATA)
{
	float sumNoise = 0;

	if (all(dispatchThreadId.xy < pSrt->m_noiseBufferSize.xy))
		sumNoise = pSrt->m_txSrcNoiseBuffer[int2(dispatchThreadId.xy)];

	sumNoise += LaneSwizzle(sumNoise, 0x1f, 0x00, 0x01);
	sumNoise += LaneSwizzle(sumNoise, 0x1f, 0x00, 0x08);
	sumNoise += LaneSwizzle(sumNoise, 0x1f, 0x00, 0x02);
	sumNoise += LaneSwizzle(sumNoise, 0x1f, 0x00, 0x10);
	sumNoise += LaneSwizzle(sumNoise, 0x1f, 0x00, 0x04);
	sumNoise += ReadLane(sumNoise, 0x20);

	if (groupIndex == 0)
	{
		float finalSumNoise = ReadFirstLane(sumNoise);
		pSrt->m_rwbSumNoiseAccBuffer[groupId.xy] =  pSrt->m_invNumSamples > 0 ? 1.0f / (finalSumNoise * pSrt->m_invNumSamples) : finalSumNoise;
	}
}

struct VolBufferBlendSrt
{
	RWTexture2D<float4>			m_rwbVolMergedBuffer;
	Texture2D<float4>			m_txBlurSourceBuffer;
};

[numthreads(8, 8, 1)]
void CS_BlendVolFogAndVolLightsBuffers(int2 dispatchThreadId : SV_DispatchThreadId, VolBufferBlendSrt* pSrt : S_SRT_DATA)
{
	pSrt->m_rwbVolMergedBuffer[dispatchThreadId] += pSrt->m_txBlurSourceBuffer[dispatchThreadId];
}

struct VolBufferBlurSrt
{
	float2						m_depthParams;
	float2						m_invBufferSize;
	float						m_blurRadius;
	float3						m_pad;
	RWTexture2D<float4>			m_rwbVolBlurredBuffer;
	Texture2D<float4>			m_txBlurSourceBuffer;
	Texture2D<float4>			m_txBlurSourceBuffer1;
	Texture2D<float>			m_txDepthVS;
	SamplerState				m_pointClampSampler;
	SamplerState				m_linearClampSampler;
};

groupshared float4 g_loadedInColor[8][14];
groupshared float g_loadedInDepth[8][14];

float GetSampleWeight(float refDepthVS, float sampleDepthVS, float stepCount)
{
	float deltaDepthVS = abs(refDepthVS - sampleDepthVS);
	float depthRatio = deltaDepthVS / (refDepthVS * stepCount);

	const float errorOffset = 0.01f;
	const float ratioScale = 3.0f;

	return 1.0f - saturate((depthRatio - errorOffset) * ratioScale);
}

float4 AverageColorByDepthWeightFixRadius(int rowIdx, int colIdx, float4 loadedInColor[8][14], float loadedInDepth[8][14])
{
	float refDepth = loadedInDepth[rowIdx][colIdx+3];

	float weight0 = 0.2f * GetSampleWeight(refDepth, loadedInDepth[rowIdx][colIdx], 3.0f);
	float weight1 = 0.5f * GetSampleWeight(refDepth, loadedInDepth[rowIdx][colIdx+1], 2.0f);
	float weight2 = 0.8f * GetSampleWeight(refDepth, loadedInDepth[rowIdx][colIdx+2], 1.0f);
	float weight3 = 0.8f * GetSampleWeight(refDepth, loadedInDepth[rowIdx][colIdx+4], 1.0f);
	float weight4 = 0.5f * GetSampleWeight(refDepth, loadedInDepth[rowIdx][colIdx+5], 2.0f);;
	float weight5 = 0.2f * GetSampleWeight(refDepth, loadedInDepth[rowIdx][colIdx+6], 3.0f);;

	float invSumWeight = 1.0f / (1.0f + weight0 + weight1 + weight2 + weight3 + weight4 + weight5);

	weight0 *= invSumWeight;
	weight1 *= invSumWeight;
	weight2 *= invSumWeight;
	weight3 *= invSumWeight;
	weight4 *= invSumWeight;
	weight5 *= invSumWeight;

	float3 avgColor = loadedInColor[rowIdx][colIdx].xyz * weight0 +
					  loadedInColor[rowIdx][colIdx+1].xyz * weight1 +
					  loadedInColor[rowIdx][colIdx+2].xyz * weight2 +
					  loadedInColor[rowIdx][colIdx+3].xyz * invSumWeight +
					  loadedInColor[rowIdx][colIdx+4].xyz * weight3 +
					  loadedInColor[rowIdx][colIdx+5].xyz * weight4 +
					  loadedInColor[rowIdx][colIdx+6].xyz * weight5;

	return float4(avgColor, loadedInColor[rowIdx][colIdx+3].w);
}

[numthreads(8, 8, 1)]
void CS_BlurVolumetricBufferXFixRadius(int2 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, VolBufferBlurSrt* pSrt : S_SRT_DATA)
{
	int baseThreadIndexX = groupId.x * 8;
	int baseThreadIndexY = groupId.y * 8;
	{
		int localIndexX = (groupIndex % 14);
		int localIndexY = (groupIndex / 14);
		int srcIndexX = baseThreadIndexX + localIndexX - 3;
		int srcIndexY = baseThreadIndexY + localIndexY;
		g_loadedInColor[localIndexY][localIndexX] = pSrt->m_txBlurSourceBuffer[int2(srcIndexX, srcIndexY)];
		g_loadedInDepth[localIndexY][localIndexX] = pSrt->m_txDepthVS[int2(srcIndexX, srcIndexY)];
	}

	int groupIndex1 = groupIndex + 64;
	if (groupIndex1 < 14 * 8)
	{
		int localIndexX = (groupIndex1 % 14);
		int localIndexY = (groupIndex1 / 14);
		int srcIndexX = baseThreadIndexX + localIndexX - 3;
		int srcIndexY = baseThreadIndexY + localIndexY;
		g_loadedInColor[localIndexY][localIndexX] = pSrt->m_txBlurSourceBuffer[int2(srcIndexX, srcIndexY)];
		g_loadedInDepth[localIndexY][localIndexX] = pSrt->m_txDepthVS[int2(srcIndexX, srcIndexY)];
	}

	float4 avgColor = AverageColorByDepthWeightFixRadius(groupThreadId.y, groupThreadId.x, g_loadedInColor, g_loadedInDepth);

	pSrt->m_rwbVolBlurredBuffer[dispatchThreadId.xy] = avgColor;
}

[numthreads(8, 8, 1)]
void CS_BlurVolumetricBufferX2FixRadius(int2 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, VolBufferBlurSrt* pSrt : S_SRT_DATA)
{
	int baseThreadIndexX = groupId.x * 8;
	int baseThreadIndexY = groupId.y * 8;
	{
		int localIndexX = (groupIndex % 14);
		int localIndexY = (groupIndex / 14);
		int srcIndexX = baseThreadIndexX + localIndexX - 3;
		int srcIndexY = baseThreadIndexY + localIndexY;
		g_loadedInColor[localIndexY][localIndexX] = pSrt->m_txBlurSourceBuffer[int2(srcIndexX, srcIndexY)] + pSrt->m_txBlurSourceBuffer1[int2(srcIndexX, srcIndexY)];
		g_loadedInDepth[localIndexY][localIndexX] = pSrt->m_txDepthVS[int2(srcIndexX, srcIndexY)];
	}

	int groupIndex1 = groupIndex + 64;
	if (groupIndex1 < 14 * 8)
	{
		int localIndexX = (groupIndex1 % 14);
		int localIndexY = (groupIndex1 / 14);
		int srcIndexX = baseThreadIndexX + localIndexX - 3;
		int srcIndexY = baseThreadIndexY + localIndexY;
		g_loadedInColor[localIndexY][localIndexX] = pSrt->m_txBlurSourceBuffer[int2(srcIndexX, srcIndexY)] + pSrt->m_txBlurSourceBuffer1[int2(srcIndexX, srcIndexY)];
		g_loadedInDepth[localIndexY][localIndexX] = pSrt->m_txDepthVS[int2(srcIndexX, srcIndexY)];
	}

	float4 avgColor = AverageColorByDepthWeightFixRadius(groupThreadId.y, groupThreadId.x, g_loadedInColor, g_loadedInDepth);

	pSrt->m_rwbVolBlurredBuffer[dispatchThreadId.xy] = avgColor;
}

[numthreads(8, 8, 1)]
void CS_BlurVolumetricBufferYFixRadius(int2 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, VolBufferBlurSrt* pSrt : S_SRT_DATA)
{
	int baseThreadIndexX = groupId.x * 8;
	int baseThreadIndexY = groupId.y * 8;
	{
		int localIndexX = (groupIndex % 8);
		int localIndexY = (groupIndex / 8);
		int srcIndexX = baseThreadIndexX + localIndexX;
		int srcIndexY = baseThreadIndexY + localIndexY - 3;
		g_loadedInColor[localIndexX][localIndexY] = pSrt->m_txBlurSourceBuffer[int2(srcIndexX, srcIndexY)];
		g_loadedInDepth[localIndexX][localIndexY] = pSrt->m_txDepthVS[int2(srcIndexX, srcIndexY)];
	}

	int groupIndex1 = groupIndex + 64;
	if (groupIndex1 < 14 * 8)
	{
		int localIndexX = (groupIndex1 % 8);
		int localIndexY = (groupIndex1 / 8);
		int srcIndexX = baseThreadIndexX + localIndexX;
		int srcIndexY = baseThreadIndexY + localIndexY - 3;
		g_loadedInColor[localIndexX][localIndexY] = pSrt->m_txBlurSourceBuffer[int2(srcIndexX, srcIndexY)];
		g_loadedInDepth[localIndexX][localIndexY] = pSrt->m_txDepthVS[int2(srcIndexX, srcIndexY)];
	}

	float4 avgColor = AverageColorByDepthWeightFixRadius(groupThreadId.x, groupThreadId.y, g_loadedInColor, g_loadedInDepth);

	pSrt->m_rwbVolBlurredBuffer[dispatchThreadId.xy] = avgColor;
}

float4 AverageColorByDepthWeight(int2 dispatchThreadId, float2 centerUv, float2 blurVectorStep, VolBufferBlurSrt* pSrt)
{
	float refDepth = pSrt->m_txDepthVS.SampleLevel(pSrt->m_pointClampSampler, centerUv, 0);

	float2 ditherFrac = frac(dispatchThreadId.yx * 0.4 * 0.434 + dispatchThreadId.xy * 0.434) * blurVectorStep;

	float2 uv[6];
	uv[0] = centerUv - ditherFrac - blurVectorStep * 3.0f;
	uv[1] = centerUv - ditherFrac - blurVectorStep * 2.0f;
	uv[2] = centerUv - ditherFrac - blurVectorStep * 1.0f;
	uv[3] = centerUv + ditherFrac + blurVectorStep * 1.0f;
	uv[4] = centerUv + ditherFrac + blurVectorStep * 2.0f;
	uv[5] = centerUv + ditherFrac + blurVectorStep * 3.0f;

	float sampleDepth[6];
	sampleDepth[0] = pSrt->m_txDepthVS.SampleLevel(pSrt->m_pointClampSampler, uv[0], 0);
	sampleDepth[1] = pSrt->m_txDepthVS.SampleLevel(pSrt->m_pointClampSampler, uv[1], 0);
	sampleDepth[2] = pSrt->m_txDepthVS.SampleLevel(pSrt->m_pointClampSampler, uv[2], 0);
	sampleDepth[3] = pSrt->m_txDepthVS.SampleLevel(pSrt->m_pointClampSampler, uv[3], 0);
	sampleDepth[4] = pSrt->m_txDepthVS.SampleLevel(pSrt->m_pointClampSampler, uv[4], 0);
	sampleDepth[5] = pSrt->m_txDepthVS.SampleLevel(pSrt->m_pointClampSampler, uv[5], 0);

	float weight0 = 0.2f * GetSampleWeight(refDepth, sampleDepth[0], 3.0f);
	float weight1 = 0.5f * GetSampleWeight(refDepth, sampleDepth[1], 2.0f);
	float weight2 = 0.8f * GetSampleWeight(refDepth, sampleDepth[2], 1.0f);
	float weight3 = 0.8f * GetSampleWeight(refDepth, sampleDepth[3], 1.0f);
	float weight4 = 0.5f * GetSampleWeight(refDepth, sampleDepth[4], 2.0f);
	float weight5 = 0.2f * GetSampleWeight(refDepth, sampleDepth[5], 3.0f);

	float invSumWeight = 1.0f / (1.0f + weight0 + weight1 + weight2 + weight3 + weight4 + weight5);

	weight0 *= invSumWeight;
	weight1 *= invSumWeight;
	weight2 *= invSumWeight;
	weight3 *= invSumWeight;
	weight4 *= invSumWeight;
	weight5 *= invSumWeight;

	float3 srcColor[7];
	srcColor[0] = pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_pointClampSampler, uv[0], 0).xyz;
	srcColor[1] = pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_pointClampSampler, uv[1], 0).xyz;
	srcColor[2] = pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_pointClampSampler, uv[2], 0).xyz;
	srcColor[3] = pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_pointClampSampler, centerUv, 0).xyz;
	srcColor[4] = pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_pointClampSampler, uv[3], 0).xyz;
	srcColor[5] = pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_pointClampSampler, uv[4], 0).xyz;
	srcColor[6] = pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_pointClampSampler, uv[5], 0).xyz;

	float3 avgColor = srcColor[0] * weight0 +
					  srcColor[1] * weight1 +
					  srcColor[2] * weight2 +
					  srcColor[3] * invSumWeight +
					  srcColor[4] * weight3 +
					  srcColor[5] * weight4 +
					  srcColor[6] * weight5;

	return float4(avgColor, pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_linearClampSampler, centerUv, 0).w);
}

[numthreads(8, 8, 1)]
void CS_BlurVolumetricBufferX(int2 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, VolBufferBlurSrt* pSrt : S_SRT_DATA)
{
	float2 centerUv = (dispatchThreadId + 0.5f) * pSrt->m_invBufferSize;

	float4 avgColor = AverageColorByDepthWeight(dispatchThreadId, centerUv, float2(pSrt->m_invBufferSize.x , 0) * pSrt->m_blurRadius / 3.0f, pSrt);

	pSrt->m_rwbVolBlurredBuffer[dispatchThreadId.xy] = avgColor;
}

[numthreads(8, 8, 1)]
void CS_BlurVolumetricBufferY(int2 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, VolBufferBlurSrt* pSrt : S_SRT_DATA)
{
	float2 centerUv = (dispatchThreadId + 0.5f) * pSrt->m_invBufferSize;

	float4 avgColor = AverageColorByDepthWeight(dispatchThreadId, centerUv, float2(0, pSrt->m_invBufferSize.y) * pSrt->m_blurRadius / 3.0f, pSrt);

	pSrt->m_rwbVolBlurredBuffer[dispatchThreadId.xy] = avgColor;
}

struct VolBufferBlurNoDepthSrt
{
	float						m_depthWeightScale;
	float						m_pad;
	float2						m_invSize;
	int2						m_bufSize;
	int2						m_pad1;
	RWTexture2D<float4>			m_rwbVolBlurredBuffer;
	RWTexture2D<float>			m_rwbMinDepthVS;
	Texture2D<float4>			m_txBlurSourceBuffer;
	Texture2D<float>			m_txDepthVS;
	SamplerState				m_samplerLinear;
};

float GetWeightByDepthVS(float depthVS, float depthScale)
{
	return 1.0f / (1.0f + depthVS * depthScale);
}

[numthreads(8, 8, 1)]
void CS_BlurVolumetricBufferXNoDepth(int3 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, VolBufferBlurNoDepthSrt* pSrt : S_SRT_DATA)
{
	int baseThreadIndexX = groupId.x * 8;
	int baseThreadIndexY = groupId.y * 8;
	{
		int localIndexX = (groupIndex % 14);
		int localIndexY = (groupIndex / 14);
		int srcIndexX = baseThreadIndexX + localIndexX - 3;
		int srcIndexY = baseThreadIndexY + localIndexY;

		int2 srcIndex = int2(srcIndexX, srcIndexY) * 2;

		float depthVS0 = pSrt->m_txDepthVS[srcIndex];
		float depthVS1 = pSrt->m_txDepthVS[srcIndex + int2(1, 0)];
		float depthVS2 = pSrt->m_txDepthVS[srcIndex + int2(0, 1)];
		float depthVS3 = pSrt->m_txDepthVS[srcIndex + int2(1, 1)];

		int idx = depthVS0 < depthVS1 ? 0 : 1;
		float minDepthVS = min(depthVS0, depthVS1);

		idx = depthVS2 < minDepthVS ? 2 : idx;
		minDepthVS = min(minDepthVS, depthVS2);

		idx = depthVS3 < minDepthVS ? 3 : idx;
		minDepthVS = min(minDepthVS, depthVS3);

		g_loadedInColor[localIndexY][localIndexX] = pSrt->m_txBlurSourceBuffer[int2(srcIndexX, srcIndexY) * 2 + int2(idx & 0x01, idx >> 1)];
		g_loadedInDepth[localIndexY][localIndexX] = GetWeightByDepthVS(minDepthVS, pSrt->m_depthWeightScale);
	}

	int groupIndex1 = groupIndex + 64;
	if (groupIndex1 < 14 * 8)
	{
		int localIndexX = (groupIndex1 % 14);
		int localIndexY = (groupIndex1 / 14);
		int srcIndexX = baseThreadIndexX + localIndexX - 3;
		int srcIndexY = baseThreadIndexY + localIndexY;

		int2 srcIndex = int2(srcIndexX, srcIndexY) * 2;

		float depthVS0 = pSrt->m_txDepthVS[srcIndex];
		float depthVS1 = pSrt->m_txDepthVS[srcIndex + int2(1, 0)];
		float depthVS2 = pSrt->m_txDepthVS[srcIndex + int2(0, 1)];
		float depthVS3 = pSrt->m_txDepthVS[srcIndex + int2(1, 1)];

		int idx = depthVS0 < depthVS1 ? 0 : 1;
		float minDepthVS = min(depthVS0, depthVS1);

		idx = depthVS2 < minDepthVS ? 2 : idx;
		minDepthVS = min(minDepthVS, depthVS2);

		idx = depthVS3 < minDepthVS ? 3 : idx;
		minDepthVS = min(minDepthVS, depthVS3);

		g_loadedInColor[localIndexY][localIndexX] = pSrt->m_txBlurSourceBuffer[int2(srcIndexX, srcIndexY) * 2 + int2(idx & 0x01, idx >> 1)];
		g_loadedInDepth[localIndexY][localIndexX] = GetWeightByDepthVS(minDepthVS, pSrt->m_depthWeightScale);
	}

	float sumWeight = g_loadedInDepth[groupThreadId.y][groupThreadId.x+3];
	pSrt->m_rwbMinDepthVS[dispatchThreadId.xy] = sumWeight;
	float4 sumColor = g_loadedInColor[groupThreadId.y][groupThreadId.x+3] * sumWeight;
	for (int i = 0; i < 3; i++)
	{
		float weight0 = g_loadedInDepth[groupThreadId.y][groupThreadId.x+i];
		float weight1 = g_loadedInDepth[groupThreadId.y][groupThreadId.x+6-i];
		sumColor += g_loadedInColor[groupThreadId.y][groupThreadId.x+i] * weight0 + g_loadedInColor[groupThreadId.y][groupThreadId.x+6-i] * weight1;
		sumWeight += weight0 + weight1;
	}

	pSrt->m_rwbVolBlurredBuffer[dispatchThreadId.xy] = sumColor / sumWeight;
}

[numthreads(8, 8, 1)]
void CS_BlurVolumetricBufferYNoDepth(int3 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, VolBufferBlurNoDepthSrt* pSrt : S_SRT_DATA)
{
	int baseThreadIndexX = groupId.x * 8;
	int baseThreadIndexY = groupId.y * 8;
	{
		int localIndexX = (groupIndex % 8);
		int localIndexY = (groupIndex / 8);
		int srcIndexX = baseThreadIndexX + localIndexX;
		int srcIndexY = baseThreadIndexY + localIndexY - 3;
		g_loadedInColor[localIndexX][localIndexY] = pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_samplerLinear, (float2(srcIndexX, srcIndexY) + 0.5f) * pSrt->m_invSize * 0.5f, 0);
		g_loadedInDepth[localIndexX][localIndexY] = pSrt->m_txDepthVS[int2(srcIndexX, srcIndexY)];
	}

	int groupIndex1 = groupIndex + 64;
	if (groupIndex1 < 14 * 8)
	{
		int localIndexX = (groupIndex1 % 8);
		int localIndexY = (groupIndex1 / 8);
		int srcIndexX = baseThreadIndexX + localIndexX;
		int srcIndexY = baseThreadIndexY + localIndexY - 3;
		g_loadedInColor[localIndexX][localIndexY] = pSrt->m_txBlurSourceBuffer.SampleLevel(pSrt->m_samplerLinear, (float2(srcIndexX, srcIndexY) + 0.5f) * pSrt->m_invSize * 0.5f, 0);
		g_loadedInDepth[localIndexX][localIndexY] = pSrt->m_txDepthVS[int2(srcIndexX, srcIndexY)];
	}

	float sumWeight = g_loadedInDepth[groupThreadId.x][groupThreadId.y+3];
	float4 sumColor = g_loadedInColor[groupThreadId.x][groupThreadId.y+3];
	for (int i = 0; i < 3; i++)
	{
		float weight0 = g_loadedInDepth[groupThreadId.x][groupThreadId.y+i];
		float weight1 = g_loadedInDepth[groupThreadId.x][groupThreadId.y+6-i];
		sumColor += g_loadedInColor[groupThreadId.x][groupThreadId.y+i] * weight0 + g_loadedInColor[groupThreadId.x][groupThreadId.y+6-i] * weight1;
		sumWeight += weight0 + weight1;
	}

	pSrt->m_rwbVolBlurredBuffer[dispatchThreadId.xy] = sumColor / sumWeight;
}

struct ClearVolBlurBufferSrt
{
	RWTexture2D<float4>			m_rwbVolBlurredBuffer;
};

[numthreads(8, 8, 1)]
void CS_ClearBlurVolumetricBuffer(int3 dispatchThreadId : SV_DispatchThreadId, ClearVolBlurBufferSrt* pSrt : S_SRT_DATA)
{
	pSrt->m_rwbVolBlurredBuffer[dispatchThreadId.xy] = 0;
}

float GetSpotVolIntegrate(float t0, float t1, float cosHalfConeAngle, float radius, float fNdotD, float fNdotO, float fOdotD, float fOdotO)
{
	float sqr_a = max(fOdotO - fNdotO * fNdotO, 0.0f);
	float x0 = t0 - fNdotO;
	float x1 = t1 - fNdotO;
	float sqr_x0 = x0 * x0;
	float sqr_x1 = x1 * x1;

	float G0 = sqrt(sqr_x0 + sqr_a);
	float G1 = sqrt(sqr_x1 + sqr_a);

	float A = 1.0 / (radius * (1.0 - cosHalfConeAngle));
	float E = radius * (fNdotD * fNdotO - fOdotD) + cosHalfConeAngle * 0.5 * sqr_a;
	float F = log(max(x1 + G1, 0.000001f) / max(x0 + G0, 0.000001f));
	float H = fOdotD - radius * cosHalfConeAngle;

	float vol = radius * fNdotD * (G1 - G0) +
				E * F +
				cosHalfConeAngle * 0.5 * (x1 * G1 - x0 * G0) - 
				fNdotD * 0.5 * (t1 * t1 - t0 * t0) + 
				H * (t1 - t0);
	return A * max(vol, 0.0f);
}

// = integral((1 / (1 + b * dot(t*d - o, t*d - o)) * e + f) dt)
float GetPointVolIntegrate(float t0, float t1, float b, float radius, float fNdotO, float fOdotO)
{
	float f = -1.0f / (b * (radius * radius));
	float e = 1.0f - f;

	float invAlpha = 1.0f / sqrt(max(1.0f / b + fOdotO - fNdotO * fNdotO, 0.000001f));
	
	float xfact = e / b * invAlpha;
	float x0 = atan((t0 - fNdotO) * invAlpha);
	float x1 = atan((t1 - fNdotO) * invAlpha);

	return xfact * (x1 - x0) + f * (t1 - t0);
}

enum kVolLocalLightFlagTag
{
	kFlagDrawing			= 0x01,
	kFlagNeedVolShadow		= 0x02,
	kFlagPointLight			= 0x04
};

struct VolLocalLightInfo
{
	float4					m_lightPosAndRadius;
	float4					m_lightDirAndConAngle;
	float4					m_sampleRightVector;
	float4					m_sampleDirVectorNear;
	float2					m_sampleAngleInfo;
	float					m_numSampleSlices;
	float					m_directionalIntensity;
	float4					m_volColor;
	uint					m_flags;
	float					m_phaseFuncG;
	float					m_volDirFadePowerCurve;
	float					m_volDirFadeScale;
	Texture2D<float4>		m_sampleInfo;
	Texture2D<float4>		m_visibilityBuf;
};

struct VolLocalLightSrt
{
	uint					m_numLights;
	float					m_noiseSharpness;
	float2					m_pad;
	float4					m_screenSize;
	float4					m_viewSpaceXyParams;
	float4					m_linearDepthParams;

	SamplerState			m_samplerPoint;
	SamplerState			m_samplerLinear;
	Texture2D<float>		m_srcDepthVS;
	Texture2D<float>		m_globalNoise;
	Texture2D<float>		m_invSumGlobalNoise;
	RegularBuffer<uint>		m_lightsPerTileBuffer;
	RegularBuffer<uint>		m_numLightsPerTileBuffer;
	RWTexture2D<float4>		m_volLightAccBuffer;

	VolLocalLightInfo		m_volLightInfo[32];
};

void GetIntersectByLightShape(out float fFinalT0, out float fFinalT1, float fNdotD, float fOdotD, float fNdotO, float fOdotO, float sqrCosHalfConeAngle, float radius, float distVS, bool isPointLight)
{
	fFinalT0 = 0;
	fFinalT1 = 0;

	float B1 = fNdotO;
	float C1 = fOdotO - radius * radius;

	float delta1 = B1 * B1 - C1;
	if (delta1 > 0)
	{
		float sqrtDelta1 = sqrt(delta1);
		fFinalT0 = B1 - sqrtDelta1;
		fFinalT1 = B1 + sqrtDelta1;

		if (!isPointLight)
		{
			// ray intersect with cone shape.
			float A = fNdotD * fNdotD - sqrCosHalfConeAngle;
			float B = fNdotD * fOdotD - sqrCosHalfConeAngle * fNdotO;
			float C = fOdotD * fOdotD - sqrCosHalfConeAngle * fOdotO;

			float fBdivA = B / A;
			float fCdivA = C / A;

			float delta = fBdivA * fBdivA - fCdivA;
			if (delta > 0)
			{
				float sqrtDelta = sqrt(delta);
				float t0 = fBdivA - sqrtDelta;
				float t1 = fBdivA + sqrtDelta;

				float tt0 = t0 * fNdotD - fOdotD;
				float tt1 = t1 * fNdotD - fOdotD;

				if (fNdotD > 0)
				{
					if (tt0 < 0)
						t0 = 10000.0f;
					if (tt1 < 0)
						t1 = 10000.0f;
				}
				else
				{
					if (tt0 < 0)
						t0 = 0.0f;
					if (tt1 < 0)
						t1 = 0.0f;
				}

				fFinalT0 = max(fFinalT0, min(t0, t1));
				fFinalT1 = min(fFinalT1, max(t0, t1));
			}
		}

		fFinalT0 = clamp(fFinalT0, 0, distVS);
		fFinalT1 = clamp(fFinalT1, 0, distVS);
	}
}

[numthreads(TILE_SIZE / 2, TILE_SIZE / 2, 1)]
void CS_VolumetricProjectLight (int3 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, VolLocalLightSrt* pSrt : S_SRT_DATA)
{
	float2 tex = (dispatchThreadId.xy * 2 + 0.5f) * pSrt->m_screenSize.zw;

	float4 finalColor = float4(0, 0, 0, 0);
	float3 viewVector = float3(tex * pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw, 1.0);
	float viewDist = length(viewVector);
	float viewSpaceZ = pSrt->m_srcDepthVS[dispatchThreadId];
	float viewSpaceDist = viewDist * viewSpaceZ;

	const uint tileIndex = groupId.x + mul24(groupId.y, uint(pSrt->m_screenSize.x))/TILE_SIZE;

	const uint numVolLights = pSrt->m_numLightsPerTileBuffer[tileIndex];

	for (int i = 0; i < numVolLights; i++)
	{
		uint iLight = pSrt->m_lightsPerTileBuffer[tileIndex*MAX_LIGHTS_PER_TILE + i];

		VolLocalLightInfo lightInfo = pSrt->m_volLightInfo[iLight];
		bool isPointLight = (lightInfo.m_flags & kFlagPointLight) != 0;

		if (lightInfo.m_flags & kFlagDrawing)
		{
			float radius = lightInfo.m_lightPosAndRadius.w;
			float cosHalfConeAngle = lightInfo.m_lightDirAndConAngle.w;

			float3 N = normalize(viewVector);
			float3 D = lightInfo.m_lightDirAndConAngle.xyz;
			float3 O = lightInfo.m_lightPosAndRadius.xyz;
			float sqrCosHalfConeAngle = cosHalfConeAngle * cosHalfConeAngle;

			float fNdotD = dot(N, D);
			float fOdotD = dot(O, D);
			float fNdotO = dot(N, O);
			float fOdotO = dot(O, O);

			float fFinalT0 = 0;
			float fFinalT1 = 0;

			GetIntersectByLightShape(fFinalT0, fFinalT1, fNdotD, fOdotD, fNdotO, fOdotO, sqrCosHalfConeAngle, radius, viewSpaceDist, isPointLight);

			if (lightInfo.m_sampleDirVectorNear.w > 0.0f)
			{
				float4 nearClipPlane = float4(D, -fOdotD - lightInfo.m_sampleDirVectorNear.w);

				float weightT0 = fNdotD * fFinalT0 + nearClipPlane.w;
				float weightT1 = fNdotD * fFinalT1 + nearClipPlane.w;

				if (weightT0 <= 0.0f && weightT1 <= 0.0f)
				{
					fFinalT1 = fFinalT0 = 0.0f;
				}
				else if (weightT0 < 0.0f || weightT1 < 0.0f)
				{
					float newT = -nearClipPlane.w / fNdotD;

					if (weightT0 > 0)
						fFinalT1 = newT;
					else
						fFinalT0 = newT;
				}
			}

			if (fFinalT1 > fFinalT0)
			{
				float visibility = 1.0f;
				if (lightInfo.m_flags & kFlagNeedVolShadow)
				{
					float2 cosSinRefAngle = normalize(float2(dot(N, lightInfo.m_sampleDirVectorNear.xyz),  dot(N, lightInfo.m_sampleRightVector.xyz)));
					float angle = 0;
					if (isPointLight)
					{
						angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));
						if (cosSinRefAngle.y < 0.0f)
							angle = 2.0f * kPi - angle;
					}
					else
					{
						angle = acos(clamp(cosSinRefAngle.y > 0 ? cosSinRefAngle.x : -cosSinRefAngle.x, -1.0f, 1.0f));
					}

					float sampleArrayIndex = saturate(angle * lightInfo.m_sampleAngleInfo.x + lightInfo.m_sampleAngleInfo.y) * lightInfo.m_numSampleSlices;

					int sampleArrayIndexLow = int(min(sampleArrayIndex, lightInfo.m_numSampleSlices-1));
					int sampleArrayIndexHi = int(min(sampleArrayIndex + 1.0f, lightInfo.m_numSampleSlices-1));

					float4 viewRayDir = lightInfo.m_sampleInfo.Load(int3(sampleArrayIndexLow, 2, 0));
					float3 viewRayRight = lightInfo.m_sampleInfo.Load(int3(sampleArrayIndexLow, 3, 0)).xyz;

					cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight)));
					angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

					float lookUpIndexLow = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

					viewRayDir = lightInfo.m_sampleInfo.Load(int3(sampleArrayIndexHi, 2, 0));
					viewRayRight = lightInfo.m_sampleInfo.Load(int3(sampleArrayIndexHi, 3, 0)).xyz;

					cosSinRefAngle = normalize(float2(dot(N, viewRayDir.xyz),  dot(N, viewRayRight)));
					angle = acos(clamp(cosSinRefAngle.x, -1.0f, 1.0f));

					float lookUpIndexHi = clamp(angle / viewRayDir.w - 0.5f, 0, 512.0f);

					float2 visibility00 = lightInfo.m_visibilityBuf.Load(int3(min(lookUpIndexLow, 511.0), sampleArrayIndexLow, 0)).xy;
					float2 visibility01 = lightInfo.m_visibilityBuf.Load(int3(min(lookUpIndexLow+1.0f, 511.0), sampleArrayIndexLow, 0)).xy;
					float2 visibility10 = lightInfo.m_visibilityBuf.Load(int3(min(lookUpIndexHi, 511.0), sampleArrayIndexHi, 0)).xy;
					float2 visibility11 = lightInfo.m_visibilityBuf.Load(int3(min(lookUpIndexHi+1.0f, 511.0), sampleArrayIndexHi, 0)).xy;

					float diffZ00 = (viewSpaceZ - visibility00.y);
					float diffZ01 = (viewSpaceZ - visibility01.y);
					float diffZ10 = (viewSpaceZ - visibility10.y);
					float diffZ11 = (viewSpaceZ - visibility11.y);

					float y = frac(sampleArrayIndex);
					float x0 = frac(lookUpIndexLow);
					float x1 = frac(lookUpIndexHi);

					float2 offset00 = float2(x0, y);
					float2 offset01 = float2(1.0f - x0, y);
					float2 offset10 = float2(x1, 1.0f - y);
					float2 offset11 = float2(1.0f - x1, 1.0f - y);

					float weight00 = 1.0f / ((1.0f + diffZ00 * diffZ00) * (1.0f + dot(offset00, offset00)));
					float weight01 = 1.0f / ((1.0f + diffZ01 * diffZ01) * (1.0f + dot(offset01, offset01)));
					float weight10 = 1.0f / ((1.0f + diffZ10 * diffZ10) * (1.0f + dot(offset10, offset10)));
					float weight11 = 1.0f / ((1.0f + diffZ11 * diffZ11) * (1.0f + dot(offset11, offset11)));

					visibility = (visibility00.x * weight00 + visibility01.x * weight01 + visibility10.x * weight10 + visibility11.x * weight11) / (weight00 + weight01 + weight10 + weight11);
				}

				float fade = miePhase(N.xyz, -D.xyz, lightInfo.m_phaseFuncG);

				float volValue = isPointLight ? GetPointVolIntegrate(fFinalT0, fFinalT1, 1.0f, radius, fNdotO, fOdotO) : 
									GetSpotVolIntegrate(fFinalT0, fFinalT1, cosHalfConeAngle, radius, fNdotD, fNdotO, fOdotD, fOdotO);

				volValue = max(volValue, 0.0f);

				if (lightInfo.m_sampleRightVector.w > 0.001f)
				{
					float d0 = sqrt(max((fFinalT0 - 2.0f * fNdotO) * fFinalT0 + fOdotO, 1e-6));
					float d1 = sqrt(max((fFinalT1 - 2.0f * fNdotO) * fFinalT1 + fOdotO, 1e-6));

					float minD = min(d0, d1);
					float maxD = max(d0, d1);

					float fadeEnd = lightInfo.m_sampleDirVectorNear.w + lightInfo.m_sampleRightVector.w;

					float brightnessScale = 1.0f;
					if (minD < fadeEnd && (maxD - minD > 1e-6))
					{
						float r2 = lightInfo.m_sampleRightVector.w * lightInfo.m_sampleRightVector.w;
						float invR2 = 1.0f / r2;
						float a0 = -1.0f / 3.0f;
						float a1 = fadeEnd;
						float a2 = r2 - fadeEnd * fadeEnd;

						float tmpD = min(maxD, fadeEnd);
						float integrate0 = (((a0 * minD + a1) * minD + a2) * minD) * invR2;
						float integrate1 = (((a0 * tmpD + a1) * tmpD + a2) * tmpD) * invR2;
					
						brightnessScale = (max(integrate1 - integrate0, 0.0f) + (maxD - tmpD)) / (maxD - minD);
					}

					volValue *= brightnessScale;
				}

				float directionalIntensity = lightInfo.m_directionalIntensity;

				float lightFactor = pow(-fNdotD * 0.499f + 0.5f, lightInfo.m_volDirFadePowerCurve) * lightInfo.m_volDirFadeScale;

				float angleIntensity = lerp(1.0f, fade, directionalIntensity);

				float3 shiftedColor = LerpColorShift(visibility, float3(0.15f, 0.1f, 0.05f), float3(1.05f, 1.0f, 1.2f));

				finalColor += float4(volValue * (isPointLight ? shiftedColor : (shiftedColor * angleIntensity * lightFactor)) * lightInfo.m_volColor.xyz, 0.0f);
			}
		}
	}

	float invAvgGloalNoise = pSrt->m_invSumGlobalNoise[int2(0, 0)];
	float noise = pSrt->m_noiseSharpness > 0.0001f ? (pSrt->m_globalNoise.SampleLevel(pSrt->m_samplerLinear, tex, 0).x * invAvgGloalNoise) : 1.0f;
	finalColor.xyz *= noise;

	pSrt->m_volLightAccBuffer[int2(dispatchThreadId.xy)] = finalColor;
}

uint GetCascadedShadowLevelSimple(float depthVS, float4 cascadedLevelDist[2])
{
	float4 cmpResult0 = depthVS > cascadedLevelDist[0].xyzw;
	float4 cmpResult1 = depthVS > cascadedLevelDist[1].xyzw;

	return (uint)dot(cmpResult0, 1.0) + (uint)dot(cmpResult1, 1.0);
}	

struct BelowWaterVolSunShadowSrt
{
	float4						m_cascadedLevelDist[2];
	float4						m_matricesRows[3*kMaxNumCascaded];
	float4						m_screenScaleOffset;
	float2						m_depthParams;
	float						m_volumeDepth;
	uint						m_numSamples;

	RWTexture2D<float>			m_visiIntensity;
	Texture2D<float>			m_txDepthBuffer;
	Texture2D<float>			m_txDepthBufferVS;
	Texture2DArray<float>		m_txShadowBuffer;
	Texture2D<uint>				m_txSrcOffset;
	SamplerComparisonState		m_sSamplerShadow;
};

float SunShadowSimple(float3 positionVS, BelowWaterVolSunShadowSrt *pSrt)
{
	float depthVS = positionVS.z;

	float finalShadowInfo = 1.0f;
	if (depthVS < pSrt->m_cascadedLevelDist[1].w)
	{
		uint iCascadedLevel = GetCascadedShadowLevelSimple(depthVS, pSrt->m_cascadedLevelDist);
		int baseMatrixIdx = iCascadedLevel*3;

		float4 shadowTexCoord;
		shadowTexCoord.x = dot(float4(positionVS, 1.0), pSrt->m_matricesRows[baseMatrixIdx+0]);
		shadowTexCoord.y = dot(float4(positionVS, 1.0), pSrt->m_matricesRows[baseMatrixIdx+1]);
		shadowTexCoord.z = (float)iCascadedLevel;
		shadowTexCoord.w = dot(float4(positionVS, 1.0), pSrt->m_matricesRows[baseMatrixIdx+2]);

		finalShadowInfo = pSrt->m_txShadowBuffer.SampleCmpLevelZero(pSrt->m_sSamplerShadow, shadowTexCoord.xyz, shadowTexCoord.w);
	}

	return finalShadowInfo;
}

[numthreads(8, 8, 1)]
void CS_BelowWaterVolSunShadow(int3 dispatchThreadId : SV_DispatchThreadId, uint2 groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadId, uint groupIndex : SV_GroupIndex, BelowWaterVolSunShadowSrt* pSrt : S_SRT_DATA)
{
	uint offset = (pSrt->m_txSrcOffset[int2(dispatchThreadId.x / 4, dispatchThreadId.y)] >> ((dispatchThreadId.x & 0x03) * 2));
	int2 srcDispatchId = dispatchThreadId.xy * 2 + (int2(offset, offset >> 1) & 0x01);

	float random = frac(srcDispatchId.y * 0.4f * 0.434f + srcDispatchId.x * 0.434f);

	float waterDepthVS = pSrt->m_txDepthBufferVS[dispatchThreadId];
	float depthBufferZ = pSrt->m_txDepthBuffer[srcDispatchId];
	float destDepthVS = (float)(pSrt->m_depthParams.x / (depthBufferZ - pSrt->m_depthParams.y));

	const float fadeDepth = pSrt->m_volumeDepth;
	const uint numSamples = pSrt->m_numSamples;

	float finalVolVisi = 0.0f;
	if (destDepthVS - waterDepthVS > 0.01f)
	{
		float detaDepthVS = min(destDepthVS - waterDepthVS, fadeDepth) / numSamples;
		float depthVS = waterDepthVS + random * detaDepthVS;

		float sumVisi = 0.0f;
		for (int i = 0; i < numSamples; i++)
		{
			sumVisi += SunShadowSimple(float3(srcDispatchId * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw, 1.0f) * depthVS, pSrt);
			depthVS += detaDepthVS;
		}

		finalVolVisi = sumVisi / numSamples;
	}

	pSrt->m_visiIntensity[dispatchThreadId.xy] = finalVolVisi;
}

struct SmoothDitheredVolBufferSrt
{
	float4								m_params;
	RWTexture2D<float>					m_rwbVisiInfo;
	Texture2D<float>					m_srcVisiInfo;
	Texture2D<float>					m_srcDepthVS;
	SamplerState						m_linearClampSampler;
};

groupshared float g_shadowInfo[12][12];
groupshared float g_vsDepth[12][12];

[numthreads(8, 8, 1)]
void CS_SmoothDitheredVolBuffer(uint2 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, SmoothDitheredVolBufferSrt* pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId.xy + 0.5f) * pSrt->m_params.xy;
	float centerVisi = pSrt->m_srcVisiInfo.SampleLevel(pSrt->m_linearClampSampler, uv, 0);
	float centerDepthVS = pSrt->m_srcDepthVS.SampleLevel(pSrt->m_linearClampSampler, uv, 0);
	float2 uv0 = uv + pSrt->m_params.zw;
	float2 uv1 = uv - pSrt->m_params.zw;

	const float sWeights[5] = {0.9f, 0.75f, 0.5f, 0.25f, 0.1f};
	float sumVisis = centerVisi;
	float sumWeights = 1.0f;
	for (int i = 0; i < 5; i++)
	{
		float weight0 = 1.0f - saturate(abs(centerDepthVS - pSrt->m_srcDepthVS.SampleLevel(pSrt->m_linearClampSampler, uv0, 0)) / 0.05f);
		float weight1 = 1.0f - saturate(abs(centerDepthVS - pSrt->m_srcDepthVS.SampleLevel(pSrt->m_linearClampSampler, uv1, 0)) / 0.05f);
		float visi0 = pSrt->m_srcVisiInfo.SampleLevel(pSrt->m_linearClampSampler, uv0, 0);
		float visi1 = pSrt->m_srcVisiInfo.SampleLevel(pSrt->m_linearClampSampler, uv1, 0);

		sumVisis += visi0 * weight0;
		sumVisis += visi1 * weight1;

		uv0 += pSrt->m_params.zw;
		uv1 -= pSrt->m_params.zw;

		sumWeights += weight0 + weight1;
	}

	pSrt->m_rwbVisiInfo[int2(dispatchThreadId.xy)] = sumVisis / sumWeights;
}

[numthreads(8, 8, 1)]
void CS_SimpleSmoothDitheredVolBuffer(uint2 dispatchThreadId : SV_DispatchThreadId, uint groupIndex : SV_GroupIndex, int2 groupThreadId : SV_GroupThreadId, uint2 groupId : SV_GroupID, SmoothDitheredVolBufferSrt* pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId.xy + 0.5f) * pSrt->m_params.xy;
	float centerVisi = pSrt->m_srcVisiInfo.SampleLevel(pSrt->m_linearClampSampler, uv, 0);
	float centerDepthVS = pSrt->m_srcDepthVS.SampleLevel(pSrt->m_linearClampSampler, uv, 0);
	float2 uv0 = uv + pSrt->m_params.zw;
	float2 uv1 = uv - pSrt->m_params.zw;

	const float sWeights[5] = {0.9f, 0.75f, 0.5f, 0.25f, 0.1f};
	float sumVisis = centerVisi;
	float sumWeights = 1.0f;
	for (int i = 0; i < 5; i++)
	{
		float weight0 = 1.0f - saturate(abs(centerDepthVS - pSrt->m_srcDepthVS.SampleLevel(pSrt->m_linearClampSampler, uv0, 0)) / 0.05f);
		float weight1 = 1.0f - saturate(abs(centerDepthVS - pSrt->m_srcDepthVS.SampleLevel(pSrt->m_linearClampSampler, uv1, 0)) / 0.05f);
		float visi0 = pSrt->m_srcVisiInfo.SampleLevel(pSrt->m_linearClampSampler, uv0, 0);
		float visi1 = pSrt->m_srcVisiInfo.SampleLevel(pSrt->m_linearClampSampler, uv1, 0);

		weight0 *= (1.0f - visi0) * (1.0f - visi0);
		weight1 *= (1.0f - visi1) * (1.0f - visi1);

		sumVisis += visi0 * weight0;
		sumVisis += visi1 * weight1;
		uv0 += pSrt->m_params.zw;
		uv1 -= pSrt->m_params.zw;

		sumWeights += weight0 + weight1;
	}

	pSrt->m_rwbVisiInfo[int2(dispatchThreadId.xy)] = sumVisis / sumWeights;
}