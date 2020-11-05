#include "global-funcs.fxi"

#define kMaxThreadInOneGroup	1024
#define kLightIdx				0
#define kMinYOfs				1
#define kMaxYOfs				2
#define kLineRangeOfs			3

#ifndef kDsDepthTileSize
#define kDsDepthTileSize	8
#endif

RWTexture2D<float>					uShadowBlockVisiBuffer	: register(u0);

groupshared uint	g_currentLightIdx;
groupshared uint	g_numLightsVisible;
groupshared uint	g_numLightsCountBuffer[1024];

bool  SolveSinCosFunc(out float2 res, float3 fFact)
{
	float3 sqrFact = fFact * fFact;
	float invA = 1.0f / (sqrFact.x + sqrFact.y);
	float BDivA = fFact.x * fFact.z * invA;
	float CDivA = (sqrFact.z - sqrFact.y) * invA;

	float deltaDivSqrA = BDivA * BDivA - CDivA;
	res = float2(0, 0);
	if (deltaDivSqrA >= 0)
	{
		float sqrtDeltaDivSqrA = sqrt(deltaDivSqrA);
		res = float2(-BDivA + sqrtDeltaDivSqrA, -BDivA - sqrtDeltaDivSqrA);
	}

	return deltaDivSqrA >= 0.0f;
}

// a * sinTheta + b * cosThea + c = 0.
bool  GetSinCosFactor(out float4 sincosRes, float3 fFact)
{
	float3 absFact = abs(fFact);

	bool bNeedReverse = (absFact.y < absFact.x);
	float3 shiftedFact = bNeedReverse ? fFact.yxz : fFact.xyz;
	float3 shiftedFactDivY = shiftedFact.xyz / shiftedFact.y;

	float4 shiftedSincos;
	bool bValid = SolveSinCosFunc(shiftedSincos.xz, shiftedFact);
	shiftedSincos.yw = -(shiftedFactDivY.x * shiftedSincos.xz + shiftedFactDivY.z);

	sincosRes = bNeedReverse ? shiftedSincos.yxwz : shiftedSincos.xyzw;
	return bValid;
}

bool GetXBaseOnY(out float x0, out float x1, float3 a, float3 b, float3 c, float y)
{
	float3 yCol = float3(a.y, b.y, c.y);
	float3 zCol = float3(a.z, b.z, c.z);

	float3 aa = yCol - y * zCol;

	float4 sincosRes;
	bool bValid = GetSinCosFactor(sincosRes, aa);

	float3 v0 = a * sincosRes.x + b * sincosRes.y + c;
	float3 v1 = a * sincosRes.z + b * sincosRes.w + c;

	x0 = v0.x / v0.z;
	x1 = v1.x / v1.z;

	return bValid;
}

#define kMinClipZ 0.00001f

bool CalculateTangentPos(out float minX, out float maxX, out float minY, out float maxY, float3 rVec, float3 uVec, float3 circleCenter, float circleRadius, float rangeX, float rangeY)
{
	float3 rXc = cross(rVec, circleCenter);
	float3 negUxc = cross(circleCenter, uVec);
	//float3 uXc = cross(uVec, circleCenter);
	float3 rXv = cross(rVec, uVec) * circleRadius;

	// rXc.y * sin(theta) - uXc.y * cos(theta) + rXv.y = 0; when tangent on x == 0.
	// rXc.x * sin(theta) - uXc.x * cos(theta) + rXv.x = 0; when tangent on y == 0.

	float4 sincosRes;
	bool bValidOnX = GetSinCosFactor(sincosRes, float3(rXc.y, negUxc.y, rXv.y));

	float3 v0 = circleRadius * (rVec * sincosRes.y + uVec * sincosRes.x) + circleCenter;
	float3 v1 = circleRadius * (rVec * sincosRes.w + uVec * sincosRes.z) + circleCenter;

	minX = 0.0f;
	maxX = 0.0f;
	if (bValidOnX && (v0.z > kMinClipZ || v1.z > kMinClipZ))
	{
		float4 projVX = float4(v0.xy / v0.z, v1.xy / v1.z);

		if (v0.z <= kMinClipZ || v1.z <= kMinClipZ)
		{
			projVX.xyzw = v0.z > kMinClipZ ? projVX.xyxy : projVX.zwzw;
			projVX.xyzw = projVX.x > 0.0f ? float4(projVX.xy, rangeX, 0.0f) : float4(projVX.xy, -rangeX, 0.0f);
		}
		
		if (projVX.x > projVX.z)
			projVX.xyzw = projVX.zwxy;

		minX = projVX.x;
		maxX = projVX.z;
	}

	bool bValidOnY = GetSinCosFactor(sincosRes, float3(rXc.x, negUxc.x, rXv.x));

	float3 v2 = circleRadius * (rVec * sincosRes.y + uVec * sincosRes.x) + circleCenter;
	float3 v3 = circleRadius * (rVec * sincosRes.w + uVec * sincosRes.z) + circleCenter;

	minY = 0.0f;
	maxY = 0.0f;
	if (bValidOnY && (v2.z > kMinClipZ || v3.z > kMinClipZ))
	{
		float4 projVY = float4(v2.xy / v2.z, v3.xy / v3.z);

		if (v2.z <= kMinClipZ || v3.z <= kMinClipZ)
		{
			projVY.xyzw = v2.z > kMinClipZ ? projVY.xyxy : projVY.zwzw;
			projVY.xyzw = projVY.y > 0.0f ? float4(projVY.xy, 0.0f, rangeY) : float4(projVY.xy, 0.0f, -rangeY);
		}
		
		if (projVY.y > projVY.w)
			projVY.xyzw = projVY.zwxy;

		minY = projVY.y;
		maxY = projVY.w;
	}

	return true;
};

struct LightsListInfoParams
{
	uint4			m_lightListInfo;
	uint4			m_lightBufferInfo;
	float4			m_screenCameraInfo;
	float4			m_bufferSizeInfo;
};


struct LightPreCalculateTextures
{
	RWTexture2D<uint>		  rwb_light_cull_info;
};

struct LightsListInfo
{
	float4 posAndRadius;
	float4 dirAndAngle;
};

struct LightPreCalculateBuffers
{
	RegularBuffer<LightsListInfo> light_list;
};

struct LightPreCalculateSrt
{
	LightsListInfoParams		*consts;
	LightPreCalculateTextures	*texs;
	LightPreCalculateBuffers    *bufs;
};

[numthreads(kMaxThreadInOneGroup, 1, 1)]
void Cs_LightsPreCalculatePass(uint groupIndex : SV_GroupIndex, LightPreCalculateSrt srt : S_SRT_DATA)
{
	if (groupIndex == 0)
	{
		g_currentLightIdx = 0;
		g_numLightsVisible = 0;
	}

	GroupMemoryBarrierWithGroupSync();

	uint curLightIdx;
	InterlockedAdd(g_currentLightIdx, 1u, curLightIdx);

	[loop]
	while (curLightIdx < srt.consts->m_lightListInfo.x)
	{
		float4 lightPosAndRadius = srt.bufs->light_list[curLightIdx].posAndRadius;
		float4 lightDirAndAngle = srt.bufs->light_list[curLightIdx].dirAndAngle;

		float distToLight2 = dot(lightPosAndRadius.xyz, lightPosAndRadius.xyz);
		float lightRadius2 = lightPosAndRadius.w * lightPosAndRadius.w;
		float lightRadius = sqrt(lightRadius2);

		float3 rayToLight = normalize(lightPosAndRadius.xyz);
		float3 lightCenter = lightPosAndRadius.xyz;
		float rangeX = srt.consts->m_screenCameraInfo.x;
		float rangeY = srt.consts->m_screenCameraInfo.y;
		bool bIsPointLight = lightDirAndAngle.w == 0.0;

		//bool bCameraIsInsideLight = (distToLight2 < lightRadius2 * 1.02f);// && (bIsPointLight || dot(rayToLight, lightDirAndAngle.xyz) < -lightDirAndAngle.w); for now, treat spot light as a point light.
		
		[branch]
		if (1/*bCameraIsInsideLight*/) // wfq : turn off light culling code for now, since it cause some flikcering.
		{
			uint visiLightIdx = 0;
			InterlockedAdd(g_numLightsVisible, 1u, visiLightIdx);
			visiLightIdx ++;

			for (uint iRow = 0; iRow < srt.consts->m_lightBufferInfo.y; iRow++)
				srt.texs->rwb_light_cull_info[uint2(iRow + kLineRangeOfs, visiLightIdx)] = srt.consts->m_lightBufferInfo.x << 16;

			srt.texs->rwb_light_cull_info[uint2(kLightIdx, visiLightIdx)] = curLightIdx;
			srt.texs->rwb_light_cull_info[uint2(kMinYOfs, visiLightIdx)] = 0x00;
			srt.texs->rwb_light_cull_info[uint2(kMaxYOfs, visiLightIdx)] = srt.consts->m_lightBufferInfo.y;
		}
		else
		{
			// get camera distance to light, normalize toLight Vector.
			float oneSubRadiusToDist2 = 1.0f - lightRadius2 / distToLight2;
			float3 circleCenter = lightCenter * oneSubRadiusToDist2;
			float circleRadius = lightRadius * sqrt(oneSubRadiusToDist2);

			float3 rVec = normalize(cross(rayToLight.xyz, float3(0.0f, 1.0f, 0.0f)));
			float3 uVec = cross(rVec, rayToLight);

			// point light.
			[branch]
			if (bIsPointLight)
			{
				float minX, maxX, minY, maxY;
				bool result = CalculateTangentPos(minX, maxX, minY, maxY, rVec, uVec, circleCenter, circleRadius, rangeX, rangeY);

				float clampedMinX = minX / rangeX + 0.5f;
				float clampedMaxX = maxX / rangeX + 0.5f;
				float clampedMinY = -maxY / rangeY + 0.5f;
				float clampedMaxY = -minY / rangeY + 0.5f;

				bool lightNotInView = clampedMinX > 1.0f || clampedMaxX < 0.0f || clampedMinY > 1.0f || clampedMaxY < 0.0f;

				if (result && lightNotInView == false)
				{
					uint visiLightIdx = 0;
					InterlockedAdd(g_numLightsVisible, 1u, visiLightIdx);
					visiLightIdx++;

					int iMinY = 0;
					int iMaxY = 0;
					if (minX != 0.0f || maxX != 0.0f && minY != 0.0f || maxY != 0.0f)
					{
						float fBlockMinY = saturate(clampedMinY) * srt.consts->m_bufferSizeInfo.y;
						float fBlockMaxY = saturate(clampedMaxY) * srt.consts->m_bufferSizeInfo.y;

						iMinY = int(fBlockMinY);
						iMaxY = int(fBlockMaxY + 0.999999f);
					}

					uint iMinYDiv8 = iMinY >> 3;
					uint iMaxYDiv8 = (iMaxY + 7) >> 3;

					float fMinYDiv8 = -((float)iMinYDiv8 * 8.0f / srt.consts->m_bufferSizeInfo.y * 2.0f - 1.0f) * rangeY * 0.5f;

					float stepY = -8.0f / srt.consts->m_bufferSizeInfo.y * 2.0f * rangeY * 0.5f;
					float curY = fMinYDiv8;
					[loop]
					for (uint iY = iMinYDiv8; iY < iMaxYDiv8; iY++)
					{
						float x0, x1;
						bool isValid = GetXBaseOnY(x0, x1, uVec * circleRadius, rVec * circleRadius, circleCenter, curY);

						uint lineRange = 0;
						if (isValid)
						{
							uint iX1 = saturate(-x0 / rangeX + 0.5f) * srt.consts->m_bufferSizeInfo.x;
							uint iX0 = saturate(-x1 / rangeX + 0.5f) * srt.consts->m_bufferSizeInfo.x;
							lineRange = (iX0 >> 3) | (((iX1 + 7) >> 3) << 16);
						}

						srt.texs->rwb_light_cull_info[uint2(iY + kLineRangeOfs, visiLightIdx)] = lineRange;
						curY += stepY;
					}

					srt.texs->rwb_light_cull_info[uint2(kLightIdx, visiLightIdx)] = curLightIdx;
					srt.texs->rwb_light_cull_info[uint2(kMinYOfs, visiLightIdx)] = iMinYDiv8;
					srt.texs->rwb_light_cull_info[uint2(kMaxYOfs, visiLightIdx)] = iMaxYDiv8;
				}
			}
			else
			{
				float minX, maxX, minY, maxY;
				bool result = CalculateTangentPos(minX, maxX, minY, maxY, rVec, uVec, circleCenter, circleRadius, rangeX, rangeY);

				float clampedMinX = minX / rangeX + 0.5f;
				float clampedMaxX = maxX / rangeX + 0.5f;
				float clampedMinY = -maxY / rangeY + 0.5f;
				float clampedMaxY = -minY / rangeY + 0.5f;

				bool lightNotInView = clampedMinX > 1.0f || clampedMaxX < 0.0f || clampedMinY > 1.0f || clampedMaxY < 0.0f;

				if (result && lightNotInView == false)
				{
					uint visiLightIdx = 0;
					InterlockedAdd(g_numLightsVisible, 1u, visiLightIdx);
					visiLightIdx++;

					int iMinY = 0;
					int iMaxY = 0;
					if (minX != 0.0f || maxX != 0.0f && minY != 0.0f || maxY != 0.0f)
					{
						float fBlockMinY = saturate(clampedMinY) * srt.consts->m_bufferSizeInfo.y;
						float fBlockMaxY = saturate(clampedMaxY) * srt.consts->m_bufferSizeInfo.y;

						iMinY = int(fBlockMinY);
						iMaxY = int(fBlockMaxY + 0.999999f);
					}

					uint iMinYDiv8 = iMinY >> 3;
					uint iMaxYDiv8 = (iMaxY + 7) >> 3;

					float fMinYDiv8 = -((float)iMinYDiv8 * 8.0f / srt.consts->m_bufferSizeInfo.y * 2.0f - 1.0f) * rangeY * 0.5f;

					float stepY = -8.0f / srt.consts->m_bufferSizeInfo.y * 2.0f * rangeY * 0.5f;
					float curY = fMinYDiv8;
					[loop]
					for (uint iY = iMinYDiv8; iY < iMaxYDiv8; iY++)
					{
						float x0, x1;
						bool isValid = GetXBaseOnY(x0, x1, uVec * circleRadius, rVec * circleRadius, circleCenter, curY);

						uint lineRange = 0;
						if (isValid)
						{
							uint iX1 = saturate(-x0 / rangeX + 0.5f) * srt.consts->m_bufferSizeInfo.x;
							uint iX0 = saturate(-x1 / rangeX + 0.5f) * srt.consts->m_bufferSizeInfo.x;
							lineRange = (iX0 >> 3) | (((iX1 + 7) >> 3) << 16);
						}

						srt.texs->rwb_light_cull_info[uint2(iY + kLineRangeOfs, visiLightIdx)] = lineRange;
						curY += stepY;
					}

					srt.texs->rwb_light_cull_info[uint2(kLightIdx, visiLightIdx)] = curLightIdx;
					srt.texs->rwb_light_cull_info[uint2(kMinYOfs, visiLightIdx)] = iMinYDiv8;
					srt.texs->rwb_light_cull_info[uint2(kMaxYOfs, visiLightIdx)] = iMaxYDiv8;
				}
			}
		}

		InterlockedAdd(g_currentLightIdx, 1u, curLightIdx);
	}

	GroupMemoryBarrierWithGroupSync();

	if (groupIndex == 0)
	{
		srt.texs->rwb_light_cull_info[uint2(0, 0)] = g_numLightsVisible;
	}
}

struct LightSsCullingTextures
{
	RWTexture2D<uint2>		  rwb_light_visi_bits;
	Texture2D<uint>			  light_cull_info;
};

struct LightSsCullingSrt
{
	LightsListInfoParams		*consts;
	LightSsCullingTextures		*texs;
};

[numthreads(kMaxThreadInOneGroup, 1, 1)]
void Cs_LightsScreenSpaceCulling(uint groupIndex : SV_GroupIndex, LightSsCullingSrt srt : S_SRT_DATA)
{
	if (groupIndex == 0)
	{
		g_currentLightIdx = 0;
		g_numLightsVisible = srt.texs->light_cull_info[uint2(0, 0)];
	}

	for (uint i = groupIndex; i < srt.consts->m_lightListInfo.y; i += kMaxThreadInOneGroup)
		g_numLightsCountBuffer[i] = 0;

	GroupMemoryBarrierWithGroupSync();

	uint curLightIdx;
	InterlockedAdd(g_currentLightIdx, 1u, curLightIdx);

	while (curLightIdx < g_numLightsVisible)
	{
		uint iListIdx = curLightIdx + 1;
		uint iLightIdx = srt.texs->light_cull_info[uint2(kLightIdx, iListIdx)];
		uint iMinYDiv8 = srt.texs->light_cull_info[uint2(kMinYOfs, iListIdx)];
		uint iMaxYDiv8 = srt.texs->light_cull_info[uint2(kMaxYOfs, iListIdx)];

		//float4 lightInfo = LoadAsFloat4(tLightCullingListBuffer, iLightIdx * 16);

		if (iMinYDiv8 < iMaxYDiv8)
		{
			uint iYBlockStart = iMinYDiv8 / 4;
			uint iYBlockEnd = (iMaxYDiv8 + 3) / 4;

			for (uint iBlockY = iYBlockStart; iBlockY < iYBlockEnd; iBlockY++)
			{
				uint iBaseY = iBlockY * 4;
				uint4 iIdxY = uint4(iBaseY, iBaseY+1, iBaseY+2, iBaseY+3);
				uint4 bufOfsX = iIdxY + kLineRangeOfs;

				bool4 validIdx = (iIdxY >= iMinYDiv8) && (iIdxY < iMaxYDiv8);

				uint4 lineRange;
				lineRange.x = validIdx.x ? srt.texs->light_cull_info[uint2(bufOfsX.x, iListIdx)] : 0;
				lineRange.y = validIdx.y ? srt.texs->light_cull_info[uint2(bufOfsX.y, iListIdx)] : 0;
				lineRange.z = validIdx.z ? srt.texs->light_cull_info[uint2(bufOfsX.z, iListIdx)] : 0;
				lineRange.w = validIdx.w ? srt.texs->light_cull_info[uint2(bufOfsX.w, iListIdx)] : 0;

				uint4 minXDiv8 = lineRange & 0xffff;
				uint4 maxXDiv8 = lineRange >> 16;

				uint4 minBlockX = minXDiv8 / 8;
				uint4 maxBlockX = maxXDiv8 / 8;

				uint startBlockX = min(min(minBlockX.x, minBlockX.y), min(minBlockX.z, minBlockX.w));
				uint endBlockX = max(max(maxBlockX.x, maxBlockX.y), max(maxBlockX.z, maxBlockX.w));

				for (uint iBlockX = startBlockX; iBlockX <= endBlockX; iBlockX++)
				{
					uint4 startX = max(iBlockX * 8, minXDiv8);
					uint4 endX = min(iBlockX * 8 + 8, maxXDiv8);

					uint4 mask = endX > startX ? (((1 << (endX - startX)) - 1) << (startX - iBlockX * 8)) : 0;

					uint numLightsThisBlock = 0;
					uint bufferIdx = iBlockY * srt.consts->m_lightBufferInfo.z + iBlockX;

					InterlockedAdd(g_numLightsCountBuffer[bufferIdx], 1u, numLightsThisBlock);

					if (numLightsThisBlock < 255)
						srt.texs->rwb_light_visi_bits[uint2(numLightsThisBlock + 1, bufferIdx)] = uint2(iLightIdx, mask.x | (mask.y << 8) | (mask.z << 16) | (mask.w << 24));
				}
			}
		}

		InterlockedAdd(g_currentLightIdx, 1u, curLightIdx);
	}

	GroupMemoryBarrierWithGroupSync();

	for (uint i1 = groupIndex; i1 < srt.consts->m_lightListInfo.y; i1 += kMaxThreadInOneGroup)
		srt.texs->rwb_light_visi_bits[uint2(0, i1)] = g_numLightsCountBuffer[i1];
}

Texture2D<uint>		tShadowDensityBuffer		: register(t0);
groupshared uint shadowBlockCount[256] ;

[numthreads(16, 16, 1)]
void CS_GetShadowDensityBlockInfo( uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex )
{
	shadowBlockCount[groupIndex] = tShadowDensityBuffer[dispatchThreadId.xy];
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < 128)
		shadowBlockCount[groupIndex] = max(shadowBlockCount[groupIndex], shadowBlockCount[groupIndex + 128]);
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < 64)
		shadowBlockCount[groupIndex] = max(shadowBlockCount[groupIndex], shadowBlockCount[groupIndex + 64]);
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < 32)
		shadowBlockCount[groupIndex] = max(shadowBlockCount[groupIndex], shadowBlockCount[groupIndex + 32]);

	if (groupIndex < 16)
		shadowBlockCount[groupIndex] = max(shadowBlockCount[groupIndex], shadowBlockCount[groupIndex + 16]);

	if (groupIndex < 8)
		shadowBlockCount[groupIndex] = max(shadowBlockCount[groupIndex], shadowBlockCount[groupIndex + 8]);

	if (groupIndex < 4)
		shadowBlockCount[groupIndex] = max(shadowBlockCount[groupIndex], shadowBlockCount[groupIndex + 4]);

	if (groupIndex < 2)
		shadowBlockCount[groupIndex] = max(shadowBlockCount[groupIndex], shadowBlockCount[groupIndex + 2]);

	if (groupIndex < 1)
		shadowBlockCount[groupIndex] = max(shadowBlockCount[groupIndex], shadowBlockCount[groupIndex + 1]);

	if (groupIndex == 0)
	{
		uShadowBlockVisiBuffer[groupId.xy] = shadowBlockCount[0] > 0 ? 1.0f : 0.0f;
	}
}

Texture2D<float>		tShadowBlockVisiBuffer		: register(t0);
groupshared float shadowBlockCount16[16] ;

[numthreads(4, 4, 1)]
void CS_GetShadowDensityBlock4x4Info( uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex )
{
	shadowBlockCount16[groupIndex] = tShadowBlockVisiBuffer[dispatchThreadId.xy];
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < 8)
		shadowBlockCount16[groupIndex] = max(shadowBlockCount16[groupIndex], shadowBlockCount16[groupIndex + 8]);

	if (groupIndex < 4)
		shadowBlockCount16[groupIndex] = max(shadowBlockCount16[groupIndex], shadowBlockCount16[groupIndex + 4]);

	if (groupIndex < 2)
		shadowBlockCount16[groupIndex] = max(shadowBlockCount16[groupIndex], shadowBlockCount16[groupIndex + 2]);

	if (groupIndex < 1)
		shadowBlockCount16[groupIndex] = max(shadowBlockCount16[groupIndex], shadowBlockCount16[groupIndex + 1]);

	if (groupIndex == 0)
	{
		uShadowBlockVisiBuffer[groupId.xy] = shadowBlockCount16[0] > 0.0f ? 1.0f : 0.0f;
	}
}
