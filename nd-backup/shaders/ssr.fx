#include "packing.fxi"
#include "global-funcs.fxi"

#define ND_PSSL
#include "compressed-vsharp.fxi"
#include "compressed-tangent-frame.fxi"

#define MAX_NUM_PLANAR_SSR_PLANES 7

static const float3 planarSsrDebugColors[MAX_NUM_PLANAR_SSR_PLANES] =
{
	float3(1.0f, 0.0f, 0.0f),
	float3(0.0f, 1.0f, 0.0f),
	float3(0.0f, 0.0f, 1.0f),
	float3(1.0f, 1.0f, 0.0f),
	float3(1.0f, 0.0f, 1.0f),
	float3(0.0f, 1.0f, 1.0f),
	float3(1.0f, 1.0f, 1.0f)
};

struct SsrConsts
{
	float4x4			m_viewToWorld;
	float4x4			m_worldToScreen;
	float4x4			m_planarShapeTransforms[MAX_NUM_PLANAR_SSR_PLANES];
	float3				m_cameraPos;
	float4				m_screenToViewParamsXY;
	float2				m_screenToViewParamsZ;
	float				m_rayLength;
	float				m_maxDepthDiff;	// threshold to detect depth buffer discontinuity
	float				m_startFadeRoughness;
	float				m_maxRoughness;	// greater than this will not compute reflection
	float				m_intensity;
	float				m_maxDistance;	// further than this from the camera will not compute reflection
	float				m_viewReflectionRayMaxAngleCosine;
	float				m_upwardNormalLimitThreshold;
	uint				m_numSteps;
	uint2				m_noiseSampleOffsets;
	uint				m_noiseSampleMask;
	uint				m_includeSky : 1;
	uint				m_dontKillSkyRay : 1;
	uint				m_halfRes : 1;
	uint				m_forceZeroRoughness : 1;
	uint				m_fgOnly : 1;
	uint				m_ssrOnWater : 1;
	uint				m_ssrOnWaterOnly : 1;
	uint				m_ssrOnHair : 1;
	uint				m_ssrScreenEdgeReflect : 1;
	float				m_ssrClipBorder;
	float				m_ssrFillHoles;
	float				m_ssrScreenEdgeFade;
	float4				m_bufferDims;	// width, height, 1.f / width, 1.f / height
	float2				m_planarFadeParams;
	uint				m_planarNumRoughnessSamples;
	float				m_planarNormalizeFactor;
	float2				m_planarMaxRoughnessRadius;
	float2				m_planarMaxNormalRadius;
	uint				m_numReflectionPlanes;
	CompressedVSharp	m_planarShapePositions[MAX_NUM_PLANAR_SSR_PLANES];
	DataBuffer<float3>	m_planarShapeNormals[MAX_NUM_PLANAR_SSR_PLANES];
};

struct SsrPlaneInfo
{
	float4 m_plane;
	float4 m_worldToPlane0;
	float4 m_worldToPlane1;
};

struct SsrRaycastSrtData
{
	RWTexture2D<float4>	rwtxSsr;
	Texture2D<float4>	txDepthBuffer;
	Texture2D<float4>   txColorBuffer;
	Texture2D<uint4>	txGBuffer0;
	Texture2D<float4>	txGBufferTransparent;
	Texture2D<uint>		materialMaskBuffer;
	Texture2D<uint>		stencilBuffer;
	Texture2D<float2>	txMotionVector;
	Texture3D<float2>	txNoise;
	SamplerState        sSamplerLinear;
};

struct SsrRaycastSrt
{
	SsrConsts *pConsts;
	SsrRaycastSrtData *pData;
};

struct SsrPlanarExtractPlaneInfoSrtData
{
	RWStructuredBuffer<SsrPlaneInfo> rwBufferPlaneInfo;
};

struct SsrPlanarExtractPlaneInfoSrt
{
	SsrConsts							*pConsts;
	SsrPlanarExtractPlaneInfoSrtData	*pData;
	uint4								globalVertBuffer;
};

struct SsrPlanarComputeSrtData
{
	RWTexture2D<uint>				rwtxPlanarSsr;
	Texture2D<float>				txDepthBuffer;
	Texture2D<uint>					stencilBuffer;
	StructuredBuffer<SsrPlaneInfo>	planeInfoBuffer;
};

struct SsrPlanarComputeSrt
{
	SsrConsts				*pConsts;
	SsrPlanarComputeSrtData	*pData;
};

struct SsrPlanarResolveSrtData
{
	RWTexture2D<float4>				rwtxSsr;
	Texture2D<uint>					txPlanarSsr;
	Texture2D<float4>				txColorBuffer;
	Texture2D<uint4>				txGBuffer0;
	Texture2D<float4>				txGBufferTransparent;
	Texture2D<uint>					stencilBuffer;
	Texture2D<float2>				txMotionVector;
	Texture3D<float2>				txNoise;
	Texture2D<uint>					txLastStencil;
	StructuredBuffer<SsrPlaneInfo>	planeInfoBuffer;
	SamplerState					sSamplerLinear;
};

struct SsrPlanarResolveSrt
{
	SsrConsts *pConsts;
	SsrPlanarResolveSrtData *pData;
};

struct SsrPlanarDebugSrtData
{
	RWTexture2D<float4>				rwtxSsr;
	Texture2D<uint>					txPlanarSsr;
};

struct SsrPlanarDebugSrt
{
	SsrPlanarDebugSrtData *pData;
};

struct PixelOutput
{
	float4 col : SV_Target;
};

struct SsrDepthBufferPsSrtData
{
	Texture2D<float>	txDepthBuffer;
};

struct SsrDepthBufferPsSrt
{
	SsrDepthBufferPsSrtData* pData;
};

float4 PS_SsrDepthBuffer(PS_PosTex input, SsrDepthBufferPsSrt srt : S_SRT_DATA)
{
	uint2 screenLocation = uint2(input.Pos.xy) * 2;
	float depthUpperLeft  = srt.pData->txDepthBuffer.Load(int3(screenLocation + uint2(0, 0), 0));
	float depthUpperRight = srt.pData->txDepthBuffer.Load(int3(screenLocation + uint2(1, 0), 0));
	float depthLowerLeft  = srt.pData->txDepthBuffer.Load(int3(screenLocation + uint2(0, 1), 0));
	float depthLowerRight = srt.pData->txDepthBuffer.Load(int3(screenLocation + uint2(1, 1), 0));

	return float4(depthUpperLeft, depthUpperRight, depthLowerLeft, depthLowerRight);
}

float LoadDepthBuffer(int3 location, SsrRaycastSrt srt)
{
	return srt.pData->txDepthBuffer.Load(location / 2)[(location.x & 1) + (location.y & 1) * 2];
}

float4 Ssr(float2 inputPosXy, float2 inputTex, SsrRaycastSrt srt)
{
	float4 col = 0.0f;

	float2 screenPos;
	uint2 screenLocation;

	bool bIncludeSky = srt.pConsts->m_includeSky;
	bool bDontKillSkyRay = srt.pConsts->m_dontKillSkyRay;
	bool bHalfRes = srt.pConsts->m_halfRes;
	bool bForceZeroRoughness = srt.pConsts->m_forceZeroRoughness;
	bool bSsrOnWater = srt.pConsts->m_ssrOnWater;
	bool bSsrOnWaterOnly = srt.pConsts->m_ssrOnWaterOnly;
	bool bSsrOnHair = srt.pConsts->m_ssrOnHair;
	bool bSsrFillHoles = srt.pConsts->m_ssrFillHoles > 0.0;
	bool bSsrScreenEdgeReflect = srt.pConsts->m_ssrScreenEdgeReflect;
	bool bSsrScreenEdgeFade = srt.pConsts->m_ssrScreenEdgeFade > 0.0;

	if (bHalfRes)
	{
		screenLocation = uint2(inputPosXy) * 2;
		screenPos = screenLocation + 0.5f;
	}
	else
	{
		screenPos = inputPosXy;
		screenLocation = uint2(screenPos);
	}

	float depthBufferZ = LoadDepthBuffer(int3(screenLocation, 0), srt);
	uint stencilBits = srt.pData->stencilBuffer.Load(int3(screenLocation, 0));
	bool bIsWater = stencilBits & 2;
	if (bIsWater && !bSsrOnWater)
	{
		return col;
	}

	if (!bIsWater && bSsrOnWaterOnly)
	{
		return col;
	}

	float viewSpaceZ = srt.pConsts->m_screenToViewParamsZ.y / (depthBufferZ - srt.pConsts->m_screenToViewParamsZ.x);
	if (viewSpaceZ > srt.pConsts->m_maxDistance && !bIsWater)
	{
		return col;
	}

	float4 normalBufferData = bIsWater ? srt.pData->txGBufferTransparent.Load(int3(screenLocation, 0)) : UnpackGBufferNormalRoughness(srt.pData->txGBuffer0.Load(int3(screenLocation, 0)));
	if (normalBufferData.w > srt.pConsts->m_maxRoughness)
	{
		return col;
	}

	float roughnessFade = srt.pConsts->m_startFadeRoughness >= srt.pConsts->m_maxRoughness ? 1.0f : saturate(1.0f - (normalBufferData.w - srt.pConsts->m_startFadeRoughness) / (srt.pConsts->m_maxRoughness - srt.pConsts->m_startFadeRoughness));

	uint materialMaskSample = srt.pData->materialMaskBuffer.Load(int3(screenLocation, 0));
	bool bIsHair = materialMaskSample & (1 << 8);
	if (bIsHair && !bSsrOnHair)
	{
		return col;
	}

	float2 uv = inputTex;
	float2 ndc = uv * float2(2.0f, -2.0f) - float2(1.0f, -1.0f);
	float4 edgeFadeMask = float4(1.0);
	if (bSsrScreenEdgeFade)
		edgeFadeMask = float4(1.0f, 1.0f, 1.0f, saturate(smoothstep(0.0, srt.pConsts->m_ssrScreenEdgeFade, 1.0 - abs(ndc.x)) / srt.pConsts->m_ssrScreenEdgeFade));
	float4 viewSpacePos = float4((ndc + srt.pConsts->m_screenToViewParamsXY.zw)* srt.pConsts->m_screenToViewParamsXY.xy * viewSpaceZ, viewSpaceZ, 1.0f);

	// world position
	float3 worldPos = mul(viewSpacePos, srt.pConsts->m_viewToWorld).xyz;

	// compute reflection in world space
	float3 worldViewDir = normalize(srt.pConsts->m_cameraPos - worldPos);
	float3 worldNormal = normalBufferData.xyz;

	// Only apply to pixels that are facing upward
	if(worldNormal.y < srt.pConsts->m_upwardNormalLimitThreshold)
		return col;
	
	float3 tangent, bitangent;
	frisvad(worldNormal, tangent, bitangent);

	uint2 noiseSampleCoord = (uint2(inputPosXy) + srt.pConsts->m_noiseSampleOffsets) & srt.pConsts->m_noiseSampleMask;
	float2 noise = (srt.pData->txNoise.Load(int4(noiseSampleCoord, 0, 0)) + 1.f) * 0.5f;

	float alpha = normalBufferData.w * normalBufferData.w;
	float3 H = sqrt(noise.y / (1.0f - noise.y)) * (alpha * cos(2.0f * kPi * noise.x) * tangent + alpha * sin(2.0f * kPi * noise.x) * bitangent) + worldNormal;
	H = normalize(H);

	float3 worldReflDir = reflect(-worldViewDir, bForceZeroRoughness ? worldNormal : H);

	if (dot(worldReflDir, worldNormal) <= 0.f)	// incoming ray can't be from under the surface
	{
		return col;
	}

	if (dot(worldReflDir, worldViewDir) >= srt.pConsts->m_viewReflectionRayMaxAngleCosine)
	{
		return col;
	}
	
	float4 worldRayEnd = float4(worldPos + worldReflDir *  srt.pConsts->m_rayLength, 1.0f);
	float4 clipRayEnd = mul(worldRayEnd, srt.pConsts->m_worldToScreen);
	float4 clipRayStart = mul(float4(worldPos, 1.0f), srt.pConsts->m_worldToScreen);

	// clip the reflection ray to near plane
	if (clipRayEnd.z < 0.0f)
	{
		float t = clipRayStart.z / (clipRayStart.z - clipRayEnd.z);
		clipRayEnd = clipRayStart + t * (clipRayEnd - clipRayStart);
	}

	float3 ndcRayEnd = clipRayEnd.xyz / clipRayEnd.w;
	float3 ndcRayStart = float3(ndc, depthBufferZ);

	// clip the reflection ray to left/right planes
	if (ndcRayEnd.x > srt.pConsts->m_ssrClipBorder)
	{
		float t = (srt.pConsts->m_ssrClipBorder - ndcRayStart.x) / (ndcRayEnd.x - ndcRayStart.x);
		ndcRayEnd = ndcRayStart + t * (ndcRayEnd - ndcRayStart);
	}
	else if (ndcRayEnd.x < -srt.pConsts->m_ssrClipBorder)
	{
		float t = (-srt.pConsts->m_ssrClipBorder - ndcRayStart.x) / (ndcRayEnd.x - ndcRayStart.x);
		ndcRayEnd = ndcRayStart + t * (ndcRayEnd - ndcRayStart);
	}

	// clip the reflection ray to top/bottom planes
	if (ndcRayEnd.y > srt.pConsts->m_ssrClipBorder)
	{
		float t = (srt.pConsts->m_ssrClipBorder - ndcRayStart.y) / (ndcRayEnd.y - ndcRayStart.y);
		ndcRayEnd = ndcRayStart + t * (ndcRayEnd - ndcRayStart);
	}
	else if (ndcRayEnd.y < -srt.pConsts->m_ssrClipBorder)
	{
		float t = (-srt.pConsts->m_ssrClipBorder - ndcRayStart.y) / (ndcRayEnd.y - ndcRayStart.y);
		ndcRayEnd = ndcRayStart + t * (ndcRayEnd - ndcRayStart);
	}

	float3 screenRayEnd = float3((ndcRayEnd.xy * float2(0.5f, -0.5f) + 0.5f) * srt.pConsts->m_bufferDims.xy, ndcRayEnd.z);
	float3 screenRayStart = float3(screenPos, depthBufferZ);
	float3 screenRay = screenRayEnd - screenRayStart;
	
	float3 screenRayStep;
	uint numSteps;

	if (abs(screenRay.x) >= abs(screenRay.y))
	{
		screenRayStep = float3(1.0f, screenRay.y / screenRay.x, screenRay.z / screenRay.x) * sign(screenRay.x);
		numSteps = abs(uint(screenRayEnd.x) - uint(screenRayStart.x));
	}
	else
	{
		screenRayStep = float3(screenRay.x / screenRay.y, 1.0f, screenRay.z / screenRay.y) * sign(screenRay.y);
		numSteps = abs(uint(screenRayEnd.y) - uint(screenRayStart.y));
	}

	if (numSteps > srt.pConsts->m_numSteps)
	{
		screenRayStep *= float(numSteps) / srt.pConsts->m_numSteps;
		numSteps = srt.pConsts->m_numSteps;
	}

	uint numBinarySteps = uint(log2(max(abs(screenRayStep.x), abs(screenRayStep.y))));
	float3 lastPixel = screenRayStart;
	float lastZBufferZ = depthBufferZ;
	bool bBehind = false;
	bool bOnceBehind = false;
	// offset the inital step so it is nevel 0.0
	float initialStepScale = noise.y + 0.1; 
	float3 initialScreenRayStep = screenRayStep * initialStepScale;
	float3 fallbackPixel = float3(0.0);
	float minDepthDiff = 1.0f;

	for (uint i = 0; i < numSteps; i++)
	{
		float3 currentPixel = lastPixel + (i == 0 ? initialScreenRayStep : screenRayStep);
		if (bSsrScreenEdgeReflect)
		{
			currentPixel.xy *= srt.pConsts->m_bufferDims.zw;
			if (currentPixel.x < 0.0 || currentPixel.x > 1.0)
			{
				currentPixel.x = min(abs(currentPixel.x), 2.0 - currentPixel.x);
				screenRayStep.x = -screenRayStep.x;
			}
			if (currentPixel.y < 0.0 || currentPixel.y > 1.0)
			{
				currentPixel.y = min(abs(currentPixel.y), 2.0 - currentPixel.y);
				screenRayStep.y = -screenRayStep.y;
			}
			currentPixel.xy *= srt.pConsts->m_bufferDims.xy;
		}
		float currentZBufferZ = LoadDepthBuffer(int3(currentPixel.xy, 0), srt);

		if (bBehind)
		{
			if (currentZBufferZ > currentPixel.z)
			{
				bBehind = false;
				if (abs(currentPixel.z - currentZBufferZ) < minDepthDiff && bSsrFillHoles)
				{
					fallbackPixel = currentPixel;
					minDepthDiff = abs(currentPixel.z - currentZBufferZ);
				}
			}
		}
		else if (currentZBufferZ <= currentPixel.z)	// potential hit, start binary search refinement
		{
			float3 lastSegPixel = lastPixel;
			float3 currentSegPixel = currentPixel;
			float currentSegZBufferZ = currentZBufferZ;
			float lastSegZBufferZ = lastZBufferZ;

			for (uint j = 0; j < numBinarySteps; j++)
			{
				float3 midSegPixel = (lastSegPixel + currentSegPixel) * 0.5f;
				float midSegZBufferZ = LoadDepthBuffer(int3(midSegPixel.xy, 0), srt);

				if (midSegZBufferZ <= midSegPixel.z)
				{
					currentSegPixel = midSegPixel;
					currentSegZBufferZ = midSegZBufferZ;
				}
				else
				{
					lastSegPixel = midSegPixel;
					lastSegZBufferZ = midSegZBufferZ;
				}
			}

			float3 midSegPixel = (lastSegPixel + currentSegPixel) * 0.5f;
			float midSegZBufferZ = LoadDepthBuffer(int3(midSegPixel.xy, 0), srt);

			if (abs((lastSegZBufferZ + currentSegZBufferZ) * 0.5f - midSegZBufferZ) <= srt.pConsts->m_maxDepthDiff)
			{
				float t = (lastSegZBufferZ - lastSegPixel.z) / (lastSegZBufferZ - lastSegPixel.z + currentSegPixel.z - currentSegZBufferZ);
				float3 intersectionPixel = lastSegPixel + t * (currentSegPixel - lastSegPixel);
				float2 intersectionUv = intersectionPixel.xy * srt.pConsts->m_bufferDims.zw;
				float2 intersectionMotionVector = srt.pData->txMotionVector.Load(int3(intersectionPixel.xy, 0));
				float2 lastFrameIntersectionUv = intersectionUv + intersectionMotionVector;
				if (bSsrScreenEdgeReflect)
					lastFrameIntersectionUv = min(abs(lastFrameIntersectionUv), 2.0 - lastFrameIntersectionUv);
				float4 outColor = srt.pData->txColorBuffer.SampleLevel(srt.pData->sSamplerLinear, lastFrameIntersectionUv, 0.0f);
				col = float4(outColor.rgb, roughnessFade * outColor.a * srt.pConsts->m_intensity) * edgeFadeMask;
				return col;
			}
			else
			{
				bBehind = true;
				// the last pixel that's in front of the depth buffer before the first invalid hit
				if (abs(currentPixel.z - currentZBufferZ) < minDepthDiff && bSsrFillHoles)
				{
					fallbackPixel = currentPixel;
					minDepthDiff = abs(currentPixel.z - currentZBufferZ);
				}
				if (!bDontKillSkyRay)
					bOnceBehind = true;
			}
		}

		lastZBufferZ = currentZBufferZ;
		lastPixel = currentPixel;
	}

	if (bIncludeSky && !bOnceBehind && screenRayStep.z > 0.0f)
	{
		float2 farPlanePixel = lastPixel.xy + (1.0f - lastPixel.z) / screenRayStep.z * screenRayStep.xy;
		if (farPlanePixel.x > 0.0f && farPlanePixel.x < srt.pConsts->m_bufferDims.x && farPlanePixel.y > 0.0f && farPlanePixel.y < srt.pConsts->m_bufferDims.y)
		{
			float depthBuffer = LoadDepthBuffer(int3(farPlanePixel.xy, 0), srt);
			if (depthBuffer == 1.0f)
			{
				float2 farPlaneUv = farPlanePixel * srt.pConsts->m_bufferDims.zw;
				float2 farPlaneMotionVector = srt.pData->txMotionVector.Load(int3(farPlanePixel.xy, 0));
				float2 lastFrameFarPlaneUv = farPlaneUv + farPlaneMotionVector;
				float4 outColor = srt.pData->txColorBuffer.SampleLevel(srt.pData->sSamplerLinear, lastFrameFarPlaneUv, 0.0f);
				col = float4(outColor.rgb, outColor.a * roughnessFade * srt.pConsts->m_intensity) * edgeFadeMask;
				return col;
			}
		}
	}
	else if (bSsrFillHoles && (minDepthDiff < srt.pConsts->m_maxDepthDiff * 1000.0f))
	{
		float2 fallbackPixelUv = fallbackPixel.xy * srt.pConsts->m_bufferDims.zw;
		float4 outColor = srt.pData->txColorBuffer.SampleLevel(srt.pData->sSamplerLinear, fallbackPixelUv, 0.0f);
		col = float4(outColor.rgb, roughnessFade * outColor.a) * edgeFadeMask;
		col.a *= srt.pConsts->m_ssrFillHoles;
		col.a *= srt.pConsts->m_intensity;
		return col;
	}

	return col * edgeFadeMask * srt.pConsts->m_intensity;
}

PixelOutput PS_Ssr(PS_PosTex input, SsrRaycastSrt srt : S_SRT_DATA)
{
	PixelOutput OUT;
	
	OUT.col = Ssr(input.Pos.xy, input.Tex, srt);

	return OUT;
}

[numthreads(8, 8, 1)]
void CS_Ssr(uint3 dispatchThreadId : SV_DispatchThreadId, SsrRaycastSrt srt : S_SRT_DATA)
{
	float2 screenPos = dispatchThreadId.xy + 0.5f;

	// note that Ssr internally works at full res (i.e. srt.pConsts->m_bufferDims is always full res)
	float2 uv = screenPos * srt.pConsts->m_bufferDims.zw;
	uv = srt.pConsts->m_halfRes ? (uv * 2.0f) : uv;
	
	float4 col = Ssr(screenPos, uv, srt);
	srt.pData->rwtxSsr[dispatchThreadId.xy] = col;
}

// 1 bit for rotation sign
// 14 bits for rotation direction
// 14 bits for offset length
// 3 bits for plane index
uint EncodeSampleOffset(in float2 sampleOffset, in uint planeIndex)
{
	float sampleOffsetLen = length(sampleOffset);
	
	// max length in normalized screen space is sqrt(2)
	uint iSampleOffsetLen = uint(sampleOffsetLen * rsqrt(2.0f) * 16383.0f);

	float sampleOffsetRot = (sampleOffset.x / sampleOffsetLen) * 0.5f + 0.5f;
	uint iSampleOffsetRot = uint(sampleOffsetRot * 16383.0f);

	uint sampleOffsetSign = sampleOffset.y < 0.0f ? 1 : 0;

	return (planeIndex << 29u) | (iSampleOffsetLen << 15u) | (iSampleOffsetRot << 1u) | sampleOffsetSign;
}

float2 DecodeSampleOffset(in uint encodedSampleOffset)
{
	float sampleOffsetLen = float((encodedSampleOffset >> 15u) & 0x3fff) * sqrt(2.0f) / 16383.0f;

	float sampleOffsetRot = float((encodedSampleOffset >> 1u) & 0x3fff) / 16383.0f;
	sampleOffsetRot = sampleOffsetRot * 2.0f - 1.0f;

	float sampleOffsetSign = ((encodedSampleOffset & 0x1) == 0) ? 1.0f : -1.0f;

	float2 sampleOffset;
	sampleOffset.x = sampleOffsetRot;
	sampleOffset.y = sqrt(1.0f - sampleOffsetRot * sampleOffsetRot) * sampleOffsetSign;
	sampleOffset *= sampleOffsetLen;

	return sampleOffset;
}

uint GetPlaneIndex(in uint encodedSampleOffset)
{
	return (encodedSampleOffset >> 29u) & 0x7;
}

bool IsSampleOffsetValid(in uint encodedSampleOffset)
{
	return (encodedSampleOffset != 0) && ((encodedSampleOffset & 0x1fffffff) != 0x1fffffff);
}

float3 GetWorldSpacePosition(in float2 screenPos, in float depthBufferZ, in SsrConsts *pConsts, out float viewSpaceZ)
{
	viewSpaceZ = pConsts->m_screenToViewParamsZ.y / (depthBufferZ - pConsts->m_screenToViewParamsZ.x);
	float2 uv = screenPos * pConsts->m_bufferDims.zw;
	float2 ndc = uv * float2(2.0f, -2.0f) - float2(1.0f, -1.0f);
	float4 viewSpacePos = float4((ndc + pConsts->m_screenToViewParamsXY.zw) * pConsts->m_screenToViewParamsXY.xy * viewSpaceZ, viewSpaceZ, 1.0f);
	return mul(viewSpacePos, pConsts->m_viewToWorld).xyz;
}

float GetDistanceToPlane(in float4 plane, in float3 position)
{
	return dot(plane.xyz, position) + plane.w;
}

float3 GetReflectedPosition(in float3 position, in float3 planeNormal, in float distanceToPlane)
{
	return position - planeNormal * (distanceToPlane * 2.0f);
}

float4 SampleColorBuffer(in float2 sampleCoords, in SsrPlanarResolveSrt srt)
{
	// calculate distant fade
	float2 dist = saturate(abs(sampleCoords * 2.0f - 1.0f) - srt.pConsts->m_planarFadeParams.x) * srt.pConsts->m_planarFadeParams.y;
	float distFade = 1.0f - max(dist.x, dist.y);

	float4 result = srt.pData->txColorBuffer.SampleLevel(srt.pData->sSamplerLinear, sampleCoords, 0.0f);
	result.a *= distFade;

	return result;
}

[numthreads(1, 1, 1)]
void CS_SsrPlanarExtractPlaneInfo(uint3 dispatchThreadId : SV_DispatchThreadId, SsrPlanarExtractPlaneInfoSrt srt : S_SRT_DATA)
{
	uint planeIndex = dispatchThreadId.x;

	SsrPlaneInfo planeInfo;

	// calculate reflection plane
	float3 normalLS;
	LoadCompressedTangentFrameN(srt.pConsts->m_planarShapeNormals[planeIndex], normalLS, 0);
	float3 normalWS = normalize(mul(normalLS, (float3x3)srt.pConsts->m_planarShapeTransforms[planeIndex]));
	float3 positionLS = LoadVertexAttribute<float3, 72>(srt.globalVertBuffer, srt.pConsts->m_planarShapePositions[planeIndex], 0);
	float3 positionWS = mul(float4(positionLS, 1.0f), srt.pConsts->m_planarShapeTransforms[planeIndex]).xyz;
	planeInfo.m_plane.xyz = normalWS;
	planeInfo.m_plane.w = -dot(normalWS, positionWS);

	// calculate world-to-plane matrix
	float3 dir = -planeInfo.m_plane.xyz;
	float3 up = abs(dir.y) > 0.99f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
	float3 left = normalize(cross(up, dir));
	up = normalize(cross(dir, left));
	planeInfo.m_worldToPlane0 = float4(left, 0.0f);
	planeInfo.m_worldToPlane1 = float4(up, 0.0f);

	srt.pData->rwBufferPlaneInfo[planeIndex] = planeInfo;
}

[numthreads(8, 8, 1)]
void CS_SsrPlanarCompute(uint3 dispatchThreadId : SV_DispatchThreadId, SsrPlanarComputeSrt srt : S_SRT_DATA)
{
	bool bIncludeSky = srt.pConsts->m_includeSky;
	bool bHalfRes = srt.pConsts->m_halfRes;

	int2 screenLocation = dispatchThreadId.xy;
	int2 screenLocationFullRes = bHalfRes ? (dispatchThreadId.xy * 2) : dispatchThreadId.xy;
	float2 screenPos = screenLocationFullRes + 0.5f;

	// compute world space position
	float depthBufferZ = srt.pData->txDepthBuffer.Load(int3(screenLocationFullRes, 0)).x;
	float viewSpaceZ;
	float3 worldPos = GetWorldSpacePosition(screenPos, depthBufferZ, srt.pConsts, viewSpaceZ);

	// check fragment type
	bool bIsSky = (depthBufferZ == 1.0f);
	uint stencilBits = srt.pData->stencilBuffer.Load(int3(screenLocationFullRes, 0));
	bool bIsWater = stencilBits & 0x2;

	// early out for fragments that shouldn't cast reflections
	if (bIsSky && !bIncludeSky)
	{
		return;
	}

	uint currentPlaneIndex = GetPlaneIndex(srt.pData->rwtxPlanarSsr[screenLocation]);

	for (uint i = 0; i < srt.pConsts->m_numReflectionPlanes; i++)
	{
		// get reflection plane
		uint reflectionPlaneIndex = i;
		float4 reflectionPlane = srt.pData->planeInfoBuffer[reflectionPlaneIndex].m_plane;

		// double-sided reflections
		if (GetDistanceToPlane(reflectionPlane, srt.pConsts->m_cameraPos) < 0.0f)
		{
			reflectionPlane = -reflectionPlane;
		}

		// calculate reflected position in world space
		float distanceToPlane = GetDistanceToPlane(reflectionPlane, worldPos);
		float3 reflectedPosWS = GetReflectedPosition(worldPos, reflectionPlane.xyz, distanceToPlane);

		// early out for fragments that shouldn't cast reflections
		float3 dirWS = normalize(srt.pConsts->m_cameraPos - reflectedPosWS);
		if ((reflectionPlaneIndex == currentPlaneIndex) || (distanceToPlane < 0.0f) ||
			(dot(dirWS, reflectionPlane.xyz) >= srt.pConsts->m_viewReflectionRayMaxAngleCosine))
		{
			continue;
		}

		// buffer dimensions
		float2 bufferSize = bHalfRes ? (srt.pConsts->m_bufferDims.xy * 0.5f) : srt.pConsts->m_bufferDims.xy;
		float2 invBufferSize = bHalfRes ? (srt.pConsts->m_bufferDims.zw * 2.0f) : srt.pConsts->m_bufferDims.zw;

		// calculate reflected position in screen space
		float4 reflectedPosCS = mul(float4(reflectedPosWS, 1.0f), srt.pConsts->m_worldToScreen);
		reflectedPosCS.xy /= reflectedPosCS.w;
		float2 reflectedPosSS = (reflectedPosCS.xy * float2(0.5f, -0.5f) + 0.5f) * bufferSize;
		int2 iReflectedPosSS = int2(reflectedPosSS);

		// early out for fragments that shouldn't receive reflections
		uint encodedSampleOffset = srt.pData->rwtxPlanarSsr[iReflectedPosSS];
		uint samplePlaneIndex = GetPlaneIndex(encodedSampleOffset);
		if ((encodedSampleOffset == 0) || (samplePlaneIndex != reflectionPlaneIndex))
		{
			continue;
		}
		
		// Encode sample offset so that length is encoded in top most bits. By outputting the resulting bitmask
		// with InterlockedMin, the fragment will survive, that is closest to reflection plane.
		float2 sampleOffset = float2(screenLocation) - reflectedPosSS;
		encodedSampleOffset = EncodeSampleOffset(sampleOffset * invBufferSize, reflectionPlaneIndex);

		// Write to multiple locations to close gaps coming from scattering.
		InterlockedMin(srt.pData->rwtxPlanarSsr[iReflectedPosSS], encodedSampleOffset);
		InterlockedMin(srt.pData->rwtxPlanarSsr[iReflectedPosSS + int2(1, 0)], encodedSampleOffset);
		InterlockedMin(srt.pData->rwtxPlanarSsr[iReflectedPosSS + int2(0, 1)], encodedSampleOffset);
		InterlockedMin(srt.pData->rwtxPlanarSsr[iReflectedPosSS + int2(1, 1)], encodedSampleOffset);
	}
}

[numthreads(8, 8, 1)]
void CS_SsrPlanarResolve(uint3 dispatchThreadId : SV_DispatchThreadId, SsrPlanarResolveSrt srt : S_SRT_DATA)
{
	bool bHalfRes = srt.pConsts->m_halfRes;
	bool bForceZeroRoughness = srt.pConsts->m_forceZeroRoughness;

	int2 screenLocation = dispatchThreadId.xy;
	int2 screenLocationFullRes = bHalfRes ? (dispatchThreadId.xy * 2) : dispatchThreadId.xy;

	// early out if no sample offset information available
	uint encodedSampleOffset = srt.pData->txPlanarSsr.Load(int3(screenLocation, 0)).x;
	if (encodedSampleOffset == 0xffffffff)
	{
		srt.pData->rwtxSsr[screenLocation] = float4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}

	// check if fragment is water
	uint stencilBits = srt.pData->stencilBuffer.Load(int3(screenLocationFullRes, 0));
	bool bIsWater = stencilBits & 0x2;

	// fetch normal/ roughness and early out when roughness over threshold
	float4 normalBufferData = bIsWater ? srt.pData->txGBufferTransparent.Load(int3(screenLocationFullRes, 0)) :
		UnpackGBufferNormalRoughness(srt.pData->txGBuffer0.Load(int3(screenLocationFullRes, 0)));
	if (normalBufferData.w > srt.pConsts->m_maxRoughness)
	{
		srt.pData->rwtxSsr[screenLocation] = float4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}

	// handle normals
	uint planeIndex = GetPlaneIndex(encodedSampleOffset);
	float3 normalWS = normalBufferData.xyz;
	float2 normalLS;
	normalLS.x = dot(srt.pData->planeInfoBuffer[planeIndex].m_worldToPlane0.xyz, normalWS);
	normalLS.y = dot(srt.pData->planeInfoBuffer[planeIndex].m_worldToPlane1.xyz, normalWS);
	int2 sampleLocation = screenLocation + int2(normalLS * srt.pConsts->m_planarMaxNormalRadius);

	// early out if at perturbed location no sample offset information available
	encodedSampleOffset = srt.pData->txPlanarSsr.Load(int3(sampleLocation, 0)).x;
	if (encodedSampleOffset == 0xffffffff)
	{
		srt.pData->rwtxSsr[screenLocation] = float4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}

	// buffer dimensions
	float2 invBufferSize = bHalfRes ? (srt.pConsts->m_bufferDims.zw * 2.0f) : srt.pConsts->m_bufferDims.zw;

	// decode sample offset and apply motion vector
	float2 sampleOffset = DecodeSampleOffset(encodedSampleOffset);
	float2 sampleCoords = float2(sampleLocation) * invBufferSize + sampleOffset;
	float2 motionVector = srt.pData->txMotionVector.Load(int3(sampleCoords * srt.pConsts->m_bufferDims.xy, 0));
	sampleCoords += motionVector;

	if (srt.pConsts->m_fgOnly)
	{
		uint resultStencil = srt.pData->txLastStencil.Load(int3(sampleCoords * srt.pConsts->m_bufferDims.xy, 0)) & 0x20;
		if (resultStencil == 0)
		{
			srt.pData->rwtxSsr[screenLocation] = float4(0.0f, 0.0f, 0.0f, 0.0f);
			return;
		}
	}

	float4 result = IsSampleOffsetValid(encodedSampleOffset) ? SampleColorBuffer(sampleCoords, srt) : float4(0.0f, 0.0f, 0.0f, 0.0f);

	if (!bForceZeroRoughness)
	{
		float roughness = normalBufferData.w;
		uint2 noiseSampleCoord = (screenLocation + srt.pConsts->m_noiseSampleOffsets) & srt.pConsts->m_noiseSampleMask;
		float4 fallbackColor = result;

		for (uint i = 0; i < srt.pConsts->m_planarNumRoughnessSamples; i++)
		{
			float2 noise = srt.pData->txNoise.Load(int4(noiseSampleCoord, i, 0)).xy;
			int2 newSampleLocation = sampleLocation + int2(noise * srt.pConsts->m_planarMaxRoughnessRadius * roughness);
			encodedSampleOffset = srt.pData->txPlanarSsr.Load(int3(newSampleLocation, 0)).x;

			// decode sample offset and apply motion vector
			sampleCoords = float2(newSampleLocation)* invBufferSize + DecodeSampleOffset(encodedSampleOffset);
			sampleCoords += motionVector;

			result += IsSampleOffsetValid(encodedSampleOffset) ? SampleColorBuffer(sampleCoords, srt) : fallbackColor;
		}

		result *= srt.pConsts->m_planarNormalizeFactor;
	}

	// calculate and apply roughness fade
	float roughnessFade = (srt.pConsts->m_startFadeRoughness >= srt.pConsts->m_maxRoughness) ? 1.0f :
	saturate(1.0f - (normalBufferData.w - srt.pConsts->m_startFadeRoughness) / (srt.pConsts->m_maxRoughness - srt.pConsts->m_startFadeRoughness));
	result.a *= roughnessFade;
	result.a *= srt.pConsts->m_intensity;

	// write out reflection color
	srt.pData->rwtxSsr[screenLocation] = result;
}

[numthreads(8, 8, 1)]
void CS_SsrPlanarDebug(uint3 dispatchThreadId : SV_DispatchThreadId, SsrPlanarDebugSrt srt : S_SRT_DATA)
{
	int2 screenLocation = dispatchThreadId.xy;

	uint encodedSampleOffset = srt.pData->txPlanarSsr.Load(int3(screenLocation, 0)).x;
	if (encodedSampleOffset == 0xffffffff)
	{
		srt.pData->rwtxSsr[screenLocation] = float4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}

	uint planeIndex = GetPlaneIndex(encodedSampleOffset);
	
	srt.pData->rwtxSsr[screenLocation] = float4(planarSsrDebugColors[planeIndex], 1.0f);
}
