#include "global-funcs.fxi"
#include "packing.fxi"
#include "probe-lighting.fxi"
#include "probe-occlusion.fxi"

#define kComputePassThreadGroupSizeX 16
#define kComputePassThreadGroupSizeYZ 2
#define kTileRes 2
#define kApplyPassThreadGroupSizeXY 8
#define kNumProbeTrees 64

#ifndef kMaxNumProbeResults
#define kMaxNumProbeResults 10
#endif

#ifndef kStackBufferSize
#define kStackBufferSize 32
#endif

enum CacheStates
{
	kCacheStateNoCaching,
	kCacheStateWriteCache,
	kCacheStateReadCache
};

struct CacheEntry
{
	uint shCoeffs[7];
	float scale;
};

struct ObjectInfo
{
	float4		tileToWorldMatrix0;
	float4		tileToWorldMatrix1;
	float4		tileToWorldMatrix2;
	float4		bounds;
	uint2		texOffsets;
	uint		cacheIndex;
};

struct CompressedProbe
{
	float	m_colorScale;
	int3	m_shCompressed0;
	int4	m_shCompressed1;
	uint2	m_occlusion;
#if USE_EXPANDED_COMPRESSED_PROBE
	int m_addlData;
#endif
};

struct SmallCompressedProbe
{
	float	m_colorScale;

	// need m_shCompressed0.x first 8 bits
	// need m_shCompressed0.y first 8 bits
	// need m_shCompressed0.z first 8 bits
	// need m_shCompressed1.w last 8 bits
	uint m_shCompressed;

	uint2	m_occlusion;

#if USE_EXPANDED_COMPRESSED_PROBE
	int m_addlData;
#endif
};

struct DecompressedProbe
{
	float3 s0;
	float3 s1;
	float3 s2;
	float3 s3;
	float4 s4;
	float4 s5;
	float4 s6;
	float3 s7;
};

struct ComputeVolProbeBufferSrt
{
	uint								m_rootIndices[kNumProbeTrees];
	uint								m_probeIndexOffsets[kNumProbeTrees];
	float4								m_centerPosLs[kNumProbeTrees];
	float4								m_edgeLengthLs[kNumProbeTrees];
	float4								m_wsToLsMatrices0[kNumProbeTrees];
	float4								m_wsToLsMatrices1[kNumProbeTrees];
	float4								m_wsToLsMatrices2[kNumProbeTrees];
	float4								m_lsToWsMatrices0[kNumProbeTrees];
	float4								m_lsToWsMatrices1[kNumProbeTrees];
	float4								m_lsToWsMatrices2[kNumProbeTrees];
	CompressedProbe						m_defaultProbe;
	uint								m_numNodeTrees;
	float								m_maxBlurRadius;
	uint								m_numObjects;
	uint								m_useOcclusion;
	float								m_occlusionScale;
	float								m_occlusionBias;
	StructuredBuffer<ObjectInfo>		m_buf_objectInfos;
	StructuredBuffer<uint2>				m_buf_probeNodes;
	StructuredBuffer<float4>			m_buf_probePos;
	StructuredBuffer<CompressedProbe>	m_buf_probes;
	RWTexture3D<float4>					m_rwt_probeShs[SH9_NUM_TEXTURES];
	RWTexture3D<float>					m_rwt_probeScale;
	RWStructuredBuffer<CacheEntry>		m_buf_cache;
};

// encode 4x signed normalized float into 1x uint
uint EncodeFloat4(in float4 value)
{
	uint4 iValue = uint4((value * 0.5f + 0.5f) * 255.0f);
	return iValue.x | (iValue.y << 8u) | (iValue.z << 16u) | (iValue.w << 24u);
}

// decode 1x uint into 4x signed normalized float
float4 DecodeFloat4(in uint value)
{
	float4 fValue = float4(value & 0xff, (value & 0x0000ff00) >> 8u, (value & 0x00ff0000) >> 16u, (value & 0xff000000) >> 24u);
	return (fValue / 255.0f) * 2.0 - 1.0f;
}

void WriteToCache(in RWTexture3D<float4> probeShs[SH9_NUM_TEXTURES], in RWTexture3D<float> probeScale,
				  in uint3 outputPos, in uint cacheIndex, in RWStructuredBuffer<CacheEntry> cacheBuffer)
{
	for (uint i = 0; i < SH9_NUM_TEXTURES; i++)
		cacheBuffer[cacheIndex].shCoeffs[i] = EncodeFloat4(probeShs[i][outputPos]);
	cacheBuffer[cacheIndex].scale = probeScale[outputPos];
}

void ReadFromCache(in RWStructuredBuffer<CacheEntry> cacheBuffer, in uint cacheIndex, in uint3 outputPos,
				   in RWTexture3D<float4> probeShs[SH9_NUM_TEXTURES], in RWTexture3D<float> probeScale)
{
	for (uint i = 0; i < SH9_NUM_TEXTURES; i++)
		probeShs[i][outputPos] = DecodeFloat4(cacheBuffer[cacheIndex].shCoeffs[i]);
	probeScale[outputPos] = cacheBuffer[cacheIndex].scale;
}

void PushStack(inout int index, inout uint stackBuffer[kStackBufferSize], in uint value)
{
	uint shiftBits = ((index & 0x01) << 4u);
	stackBuffer[index / 2] = (shiftBits > 0 ? (stackBuffer[index / 2] & 0xffff) : 0) | (value << shiftBits);
	index++;
}

uint PopStack(inout int index, in uint stackBuffer[kStackBufferSize])
{
	index--;
	uint shiftBits = ((index & 0x01) << 4u);
	return (stackBuffer[index / 2] >> shiftBits) & 0xffff;
}

uint EncodeProbeInfo(in float weight, in uint probeIndex, out uint iWeight)
{
	iWeight = uint(weight * 4095.0f) << 20u;
	return iWeight | (probeIndex << 4u);
}

void DecodeProbeInfo(in uint probeInfo, out float weight, out uint probeIndex)
{
	weight = (probeInfo >> 20u) / 4095.0f;
	probeIndex = (probeInfo >> 4u) & 0xffff;
}

void InternalNearestNeighbors(inout uint probeInfoList[kMaxNumProbeResults], inout uint probeCount, in ComputeVolProbeBufferSrt* pSrt,
							  in uint nodeTreeIndex, in float3 positionLS, in float3 obbCenterLS, in float sizeWeight)
{
	int stackTop = 0;
	uint stackBuffer[kStackBufferSize];
	float maxDistSqr = pSrt->m_maxBlurRadius * pSrt->m_maxBlurRadius;
	float3 cornerVec = positionLS - obbCenterLS;
	float3 cornerVecN = normalize(cornerVec);

	if (all(abs(positionLS - pSrt->m_centerPosLs[nodeTreeIndex].xyz) < pSrt->m_edgeLengthLs[nodeTreeIndex].xyz))
	{
		PushStack(stackTop, stackBuffer, pSrt->m_rootIndices[nodeTreeIndex]);

		while (stackTop > 0)
		{
			uint nodeIndex = PopStack(stackTop, stackBuffer);
			uint2 node = pSrt->m_buf_probeNodes[nodeIndex];

			float splitPos = asfloat(node.x);
			uint splitAxis = node.y & 0x03;
			uint hasLeftChild = node.y & 0x04;
			uint rightChild = node.y >> 3u;
			rightChild = (rightChild < ((1u << 29u) - 1)) ? (rightChild + pSrt->m_rootIndices[nodeTreeIndex]) : rightChild;

			float planeDist = 0.0f;
			if (splitAxis != 3)
			{
				bool hasChild0 = hasLeftChild != 0;
				bool hasChild1 = rightChild < (1u << 29u) - 1;
				uint child0 = nodeIndex + 1;
				uint child1 = rightChild;

				planeDist = positionLS[splitAxis] - splitPos;
				if (planeDist > 0.0f)
				{
					// swap values
					child0 ^= child1;
					child1 ^= child0;
					child0 ^= child1;
					hasChild0 ^= hasChild1;
					hasChild1 ^= hasChild0;
					hasChild0 ^= hasChild1;
				}

				if (hasChild0)
				{
					PushStack(stackTop, stackBuffer, child0);
				}
				if (abs(planeDist) < pSrt->m_maxBlurRadius && hasChild1)
				{
					PushStack(stackTop, stackBuffer, child1);
				}
			}

			if (abs(planeDist) < pSrt->m_maxBlurRadius)
			{
				float3 probeDiffVec = pSrt->m_buf_probePos[nodeIndex].xyz - positionLS;
				float distSqr = dot(probeDiffVec, probeDiffVec);

				if (distSqr < maxDistSqr)
				{
					float distWeight = exp2(-distSqr / maxDistSqr * 12.0f);
					float3 probeVec = normalize(pSrt->m_buf_probePos[nodeIndex].xyz - obbCenterLS);
					float angularWeight = dot(cornerVecN, probeVec) * 0.5f + 0.5f;
					float weight = distWeight * lerp(angularWeight, 1.0f, sizeWeight);
					uint probeIndex = asuint(pSrt->m_buf_probePos[nodeIndex].w) + pSrt->m_probeIndexOffsets[nodeTreeIndex];

					if (pSrt->m_useOcclusion > 0)
					{
						// Shrink bbox for occlusion evaluation to avoid unstable results resulting from bboxes that
						// are too large in comparison to parent mesh.
						float3 shrinkedPositionLS = cornerVec * 0.25f + obbCenterLS;
						probeVec = shrinkedPositionLS - pSrt->m_buf_probePos[nodeIndex].xyz;
						float probeVecLen = length(probeVec);
						probeVec /= max(probeVecLen, 0.00001f);

						float3x4 localToWorldMat;
						localToWorldMat[0] = pSrt->m_lsToWsMatrices0[nodeTreeIndex];
						localToWorldMat[1] = pSrt->m_lsToWsMatrices1[nodeTreeIndex];
						localToWorldMat[2] = pSrt->m_lsToWsMatrices2[nodeTreeIndex];
						float3 dir = mul(localToWorldMat, float4(probeVec, 0.0f)).xyz;

						CompressedProbe probe = pSrt->m_buf_probes[probeIndex];
						float occluderDepth = CalcProbeOccluderDepth(probe.m_occlusion, dir);
						float currentDepth = saturate(probeVecLen / PROBE_OCCLUSION_MAX_OCCLUDER_DEPTH);

						float occlusion = 1.0f - smoothstep(occluderDepth * pSrt->m_occlusionScale, occluderDepth, currentDepth + pSrt->m_occlusionBias);
						weight *= (occluderDepth < 1.0f) ? occlusion : 1.0f;
					}

					uint iWeight;
					uint probeInfo = EncodeProbeInfo(weight, probeIndex, iWeight);
					if (probeCount < kMaxNumProbeResults)
					{
						probeInfoList[probeCount] = probeInfo | probeCount;
						probeCount++;
					}
					else
					{
						// find slot with smallest weight
						uint minSlot = min(min3(min3(probeInfoList[0], probeInfoList[1], probeInfoList[2]),
												min3(probeInfoList[3], probeInfoList[4], probeInfoList[5]),
												min3(probeInfoList[6], probeInfoList[7], probeInfoList[8])),
										   probeInfoList[9]);

						if (iWeight > (minSlot & 0xfff00000))
						{
							uint probeInfoIndex = minSlot & 0x0f;
							probeInfoList[probeInfoIndex] = probeInfo | probeInfoIndex;
						}
					}
				}
			}
		}
	}
}

void DecompressProbe(in CompressedProbe compressedProbe, in float probeScale, out DecompressedProbe decompressedProbe)
{
	int3 r0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 0u, 8u);
	int3 g0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 8u, 8u);
	int3 b0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 16u, 8u);
	int3 a0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 24u, 8u);
	int4 r1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 0u, 8u);
	int4 g1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 8u, 8u);
	int4 b1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 16u, 8u);
	int4 a1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 24u, 8u);

	decompressedProbe.s0 = r0 * probeScale;
	decompressedProbe.s1 = g0 * probeScale;
	decompressedProbe.s2 = b0 * probeScale;
	decompressedProbe.s3 = a0 * probeScale;
	decompressedProbe.s4 = r1 * probeScale;
	decompressedProbe.s5 = g1 * probeScale;
	decompressedProbe.s6 = b1 * probeScale;
	decompressedProbe.s7 = a1.xyz * probeScale;
}

void DecompressProbeWithShadow(in CompressedProbe compressedProbe, in float probeScale, out DecompressedProbe decompressedProbe, inout float shadow)
{
	int3 r0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 0u, 8u);
	int3 g0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 8u, 8u);
	int3 b0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 16u, 8u);
	int3 a0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 24u, 8u);
	int4 r1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 0u, 8u);
	int4 g1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 8u, 8u);
	int4 b1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 16u, 8u);
	int4 a1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 24u, 8u);
	uint4 a2 = (uint4)BitFieldExtract(uint4(compressedProbe.m_shCompressed1), 24u, 8u);

	decompressedProbe.s0 = r0 * probeScale;
	decompressedProbe.s1 = g0 * probeScale;
	decompressedProbe.s2 = b0 * probeScale;
	decompressedProbe.s3 = a0 * probeScale;
	decompressedProbe.s4 = r1 * probeScale;
	decompressedProbe.s5 = g1 * probeScale;
	decompressedProbe.s6 = b1 * probeScale;
	decompressedProbe.s7 = a1.xyz * probeScale;

	if (a1.w != 0)
	{
	//	_SCE_BREAK();
	}
	shadow = a2.w / 255.0;
}


void DecompressSmallProbeWithShadow(in SmallCompressedProbe compressedProbe, in float probeScale, out DecompressedProbe decompressedProbe, inout float shadow)
{
	// need m_shCompressed0.x first 8 bits
	// need m_shCompressed0.x second 8 bits
	// need m_shCompressed0.x third 8 bits
	// need m_shCompressed1.w last 8 bits

	//int3 r0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 0u, 8u);
	//int3 g0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 8u, 8u);
	//int3 b0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 16u, 8u);
	//int3 a0 = (int3)BitFieldExtract(compressedProbe.m_shCompressed0, 24u, 8u);
	//int4 r1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 0u, 8u);
	//int4 g1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 8u, 8u);
	//int4 b1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 16u, 8u);
	//int4 a1 = (int4)BitFieldExtract(compressedProbe.m_shCompressed1, 24u, 8u);
	//uint4 a2 = (uint4)BitFieldExtract(uint4(compressedProbe.m_shCompressed1), 24u, 8u);

	int r0 = (int)BitFieldExtract(compressedProbe.m_shCompressed, 0u, 8u);
	int g0 = (int)BitFieldExtract(compressedProbe.m_shCompressed, 8u, 8u);
	int b0 = (int)BitFieldExtract(compressedProbe.m_shCompressed, 16u, 8u);
	int a2 = (int)BitFieldExtract(compressedProbe.m_shCompressed, 24u, 8u);

	decompressedProbe.s0.x = r0 * probeScale;
	decompressedProbe.s1.x = g0 * probeScale;
	decompressedProbe.s2.x = b0 * probeScale;

	if (a2 != 0)
	{
		//	_SCE_BREAK();
	}
	shadow = a2 / 255.0;
}

void ComputeVolProbeBuffer(in uint3 dispatchThreadId, in uint3 groupThreadId, in ComputeVolProbeBufferSrt* pSrt, CacheStates cacheState)
{
	uint objectIndex = dispatchThreadId.x / kTileRes;
	if (objectIndex >= pSrt->m_numObjects)
		return;

	bool overflow = (objectIndex >= kVolProbeLightingOverflowObjectId);
	objectIndex = overflow ? kVolProbeLightingOverflowObjectId : objectIndex;

	uint3 tileCoords = uint3(groupThreadId.x % kTileRes, groupThreadId.yz);
	uint3 outputPos = uint3(pSrt->m_buf_objectInfos[objectIndex].texOffsets + tileCoords.xy, tileCoords.z);
	
	uint cacheIndex;
	if (cacheState != kCacheStateNoCaching)
	{
		uint offset = tileCoords.z * kTileRes * kTileRes + tileCoords.y * kTileRes + tileCoords.x;
		cacheIndex = pSrt->m_buf_objectInfos[objectIndex].cacheIndex * kTileRes * kTileRes * kTileRes + offset;
	}

	if (cacheState != kCacheStateReadCache)
	{
		if (overflow)
		{
			// write out pink SH lighting environment to detect overflow cases
			pSrt->m_rwt_probeShs[0][outputPos] = float4(1.0f / (2.0f * sqrt(kPi)), 0.0f, 0.0f, 0.0f);
			pSrt->m_rwt_probeShs[1][outputPos] = float4(0.0f, 0.0f, 0.0f, 0.0f);
			pSrt->m_rwt_probeShs[2][outputPos] = float4(1.0f / (2.0f * sqrt(kPi)), 0.0f, 0.0f, 0.0f);
			pSrt->m_rwt_probeShs[3][outputPos] = float4(0.0f, 0.0f, 0.0f, 0.0f);
			pSrt->m_rwt_probeShs[4][outputPos] = float4(0.0f, 0.0f, 0.0f, 0.0f);
			pSrt->m_rwt_probeShs[5][outputPos] = float4(0.0f, 0.0f, 0.0f, 0.0f);
			pSrt->m_rwt_probeShs[6][outputPos] = float4(0.0f, 0.0f, 0.0f, 0.0f);
			pSrt->m_rwt_probeScale[outputPos] = 1.0f;
		}
		else
		{
			// calculate OBB corner and center in world space
			float3x4 tileToWorldMatrix;
			tileToWorldMatrix[0] = pSrt->m_buf_objectInfos[objectIndex].tileToWorldMatrix0;
			tileToWorldMatrix[1] = pSrt->m_buf_objectInfos[objectIndex].tileToWorldMatrix1;
			tileToWorldMatrix[2] = pSrt->m_buf_objectInfos[objectIndex].tileToWorldMatrix2;
			float3 positionWS = mul(tileToWorldMatrix, float4(tileCoords, 1.0f)).xyz;
			float3 obbCenterWS = pSrt->m_buf_objectInfos[objectIndex].bounds.xyz;

			// calculate size weight to modulate angular weight
			float sizeWeight = saturate(pSrt->m_buf_objectInfos[objectIndex].bounds.w / max(pSrt->m_maxBlurRadius, 0.0001f));

			// find 10 closest probes within search radius
			uint probeInfoList[kMaxNumProbeResults];
			uint probeCount = 0;
			for (uint i = 0; i < pSrt->m_numNodeTrees; i++)
			{
				float3x4 worldToLocalMat;
				worldToLocalMat[0] = pSrt->m_wsToLsMatrices0[i];
				worldToLocalMat[1] = pSrt->m_wsToLsMatrices1[i];
				worldToLocalMat[2] = pSrt->m_wsToLsMatrices2[i];
				float3 positionLS = mul(worldToLocalMat, float4(positionWS, 1.0f)).xyz;
				float3 obbCenterLS = mul(worldToLocalMat, float4(obbCenterWS, 1.0f)).xyz;
				InternalNearestNeighbors(probeInfoList, probeCount, pSrt, i, positionLS, obbCenterLS, sizeWeight);
			}

			const float kCoeffScale = 1.0f / 127.0f;

			// init probe sum
			float weightSum = 1.0f;
			CompressedProbe firstProbe = pSrt->m_defaultProbe;
			if (probeCount > 0)
			{
				uint probeIndex;
				DecodeProbeInfo(probeInfoList[0], weightSum, probeIndex);

				firstProbe = pSrt->m_buf_probes[probeIndex];
			}
			float probeScale = firstProbe.m_colorScale * weightSum * kCoeffScale;
			DecompressedProbe probeSum;
			DecompressProbe(firstProbe, probeScale, probeSum);

			// accumulate probe samples
			if (probeCount > 1)
			{
				for (uint i = 1; i < probeCount; i++)
				{
					float weight;
					uint probeIndex;
					DecodeProbeInfo(probeInfoList[i], weight, probeIndex);

					CompressedProbe probe = pSrt->m_buf_probes[probeIndex];
					probeScale = probe.m_colorScale * weight * kCoeffScale;
					DecompressedProbe probeSample;
					DecompressProbe(probe, probeScale, probeSample);

					probeSum.s0 += probeSample.s0;
					probeSum.s1 += probeSample.s1;
					probeSum.s2 += probeSample.s2;
					probeSum.s3 += probeSample.s3;
					probeSum.s4 += probeSample.s4;
					probeSum.s5 += probeSample.s5;
					probeSum.s6 += probeSample.s6;
					probeSum.s7 += probeSample.s7;
					weightSum += weight;
				}
			}

			// compute normalization factor
			float3 maxS0 = max3(abs(probeSum.s0), abs(probeSum.s1), abs(probeSum.s2));
			float4 maxS1 = max3(abs(probeSum.s4), abs(probeSum.s5), abs(probeSum.s6));
			float3 maxS = max3(maxS0, maxS1.xyz, abs(probeSum.s3));
			maxS = max(maxS, abs(probeSum.s7.xyz));
			float maxSS = max(max3(maxS.x, maxS.y, maxS.z), maxS1.w);
			float invMaxSS = 1.0f / maxSS;
			float invWeightSum = 1.0f / max(weightSum, 0.0001f);

			// normalize accumulated probes
			probeSum.s0 *= invMaxSS;
			probeSum.s1 *= invMaxSS;
			probeSum.s2 *= invMaxSS;
			probeSum.s3 *= invMaxSS;
			probeSum.s4 *= invMaxSS;
			probeSum.s5 *= invMaxSS;
			probeSum.s6 *= invMaxSS;
			probeSum.s7 *= invMaxSS;

			// calculate probe density
			float probeDensity = float(probeCount) / float(kMaxNumProbeResults);

			// output accumulated probe samples
			// (We don't use the baked sun shadow from the volumetric probe lighting system, since it is incoherent
			// with the baked sun shadow coming from lightmaps, that are binary. Instead we use the one probe per
			// object baked sun shadow and store in probeShs[6].w the probe density, that is used for debugging
			// purposes.)
			pSrt->m_rwt_probeShs[0][outputPos] = float4(probeSum.s0.x, probeSum.s3.x, probeSum.s2.y, probeSum.s1.z);
			pSrt->m_rwt_probeShs[1][outputPos] = float4(probeSum.s1.x, probeSum.s0.y, probeSum.s3.y, probeSum.s2.z);
			pSrt->m_rwt_probeShs[2][outputPos] = float4(probeSum.s2.x, probeSum.s1.y, probeSum.s0.z, probeSum.s3.z);
			pSrt->m_rwt_probeShs[3][outputPos] = float4(probeSum.s4.x, probeSum.s7.x, probeSum.s6.y, probeSum.s5.z);
			pSrt->m_rwt_probeShs[4][outputPos] = float4(probeSum.s5.x, probeSum.s4.y, probeSum.s7.y, probeSum.s6.z);
			pSrt->m_rwt_probeShs[5][outputPos] = float4(probeSum.s6.x, probeSum.s5.y, probeSum.s4.z, probeSum.s7.z);
			pSrt->m_rwt_probeShs[6][outputPos] = float4(probeSum.s4.w, probeSum.s5.w, probeSum.s6.w, probeDensity);
			pSrt->m_rwt_probeScale[outputPos] = maxSS * invWeightSum;
		}
		
		if (cacheState == kCacheStateWriteCache)
		{
			WriteToCache(pSrt->m_rwt_probeShs, pSrt->m_rwt_probeScale, outputPos, cacheIndex, pSrt->m_buf_cache);
		}
	}

	if (cacheState == kCacheStateReadCache)
	{
		ReadFromCache(pSrt->m_buf_cache, cacheIndex, outputPos, pSrt->m_rwt_probeShs, pSrt->m_rwt_probeScale);
	}
}

[numthreads(kComputePassThreadGroupSizeX, kComputePassThreadGroupSizeYZ, kComputePassThreadGroupSizeYZ)]
void CS_ComputeVolProbeBuffer(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID,
							  ComputeVolProbeBufferSrt* pSrt : S_SRT_DATA)
{
	ComputeVolProbeBuffer(dispatchThreadId, groupThreadId, pSrt, kCacheStateNoCaching);
}

[numthreads(kComputePassThreadGroupSizeX, kComputePassThreadGroupSizeYZ, kComputePassThreadGroupSizeYZ)]
void CS_ComputeVolProbeBufferWriteCache(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID,
										 ComputeVolProbeBufferSrt* pSrt : S_SRT_DATA)
{
	ComputeVolProbeBuffer(dispatchThreadId, groupThreadId, pSrt, kCacheStateWriteCache);
}

[numthreads(kComputePassThreadGroupSizeX, kComputePassThreadGroupSizeYZ, kComputePassThreadGroupSizeYZ)]
void CS_ComputeVolProbeBufferReadCache(uint3 dispatchThreadId : SV_DispatchThreadId, uint3 groupThreadId : SV_GroupThreadID,
										ComputeVolProbeBufferSrt* pSrt : S_SRT_DATA)
{
	ComputeVolProbeBuffer(dispatchThreadId, groupThreadId, pSrt, kCacheStateReadCache);
}

struct ApplyVolProbeBufferConstants
{
	float4x4					m_viewToWorld;
	float4						m_screenToViewParamsXY;
	float2						m_screenToViewParamsZ;
	float2						m_bufferDimensions;
	float						m_ambientBgScale;
	float						m_characterDirectionality;
	float3						m_characterFakeAmbient;
	float						m_probeDeringingStrength;
};

struct ApplyVolProbeBufferBufs 
{
	StructuredBuffer<float4x4>	m_buf_worldToTextureMatrices;
	StructuredBuffer<float4>	m_buf_tileBounds;
	
	Texture3D<float4>			m_tex_probeShs[SH9_NUM_TEXTURES];
	Texture3D<float>			m_tex_probeScale;
	Texture2D<float>			m_tex_primaryDepth;
	Texture2D<uint4>			m_tex_gbuffer0;
	Texture2D<uint>				m_tex_materialMask;
	Texture2D<uint>				m_tex_stencil;

	RWTexture2D<float3>			m_rwt_ambientBase;
	RWTexture2D<float3>			m_rwt_ambientDirectional;
	RWTexture2D<uint4>			m_rwt_gbuffer1;
};

struct ApplyVolProbeBufferSamplers
{
	SamplerState				m_smp_linear;
};

struct ApplyVolProbeBufferSrt
{
	ApplyVolProbeBufferBufs *			pBufs;
	ApplyVolProbeBufferConstants *		pConsts;
	ApplyVolProbeBufferSamplers *		pSamplers;
};

struct ApplyVolProbeOutput
{
	float3								m_rt_ambientBase		: SV_Target0;
	float3								m_rt_ambientDirectional : SV_Target1;
	uint4								m_rt_gbuffer1			: SV_Target2;
};

ApplyVolProbeOutput ApplyVolProbeBuffer(in int2 screenLocation, in ApplyVolProbeBufferBufs* pBufs, in ApplyVolProbeBufferConstants* pConsts, in ApplyVolProbeBufferSamplers* pSamplers)
{
	float2 screenPos = screenLocation + 0.5f;

	// check if pixel is volumetric probe-lit
	uint4 gbuffer1Data = pBufs->m_rwt_gbuffer1[screenLocation];
	bool isVolProbeLit = (gbuffer1Data.x & MASK_BIT_SPECIAL_HAS_VOL_PROBE_LIGHTING);
	
	if (!isVolProbeLit)
		discard;

	// check if pixel is background
	uint stencil = pBufs->m_tex_stencil[screenLocation];
	bool isBackground = (stencil & 0x20) == 0; // IMPORTANT: CHANGE THIS VALUE IF StencilBits kStencilBitIsFg BIT CHANGES!

	// compute world space position
	float depthBufferZ = pBufs->m_tex_primaryDepth.Load(int3(screenLocation, 0)).x;
	float viewSpaceZ = pConsts->m_screenToViewParamsZ.y / (depthBufferZ - pConsts->m_screenToViewParamsZ.x);
	float2 uv = screenPos * pConsts->m_bufferDimensions;
	float2 ndc = uv * float2(2.0f, -2.0f) - float2(1.0f, -1.0f);
	float4 viewSpacePos = float4((ndc + pConsts->m_screenToViewParamsXY.zw) * pConsts->m_screenToViewParamsXY.xy * viewSpaceZ, viewSpaceZ, 1.0f);
	float3 positionWS = mul(viewSpacePos, pConsts->m_viewToWorld).xyz;

	// sample world space normal
	float3 normalWS = UnpackGBufferNormal(pBufs->m_tex_gbuffer0.Load(int3(screenLocation, 0)));

	// sample object ID
	uint objectId = DecodeR11G11B10(pBufs->m_rwt_ambientBase[screenLocation]);

	// sample ambientScale
	float ambientScale = pBufs->m_rwt_ambientDirectional[screenLocation].x;

	// check if pixel is character
	uint extraMaterialMask = gbuffer1Data.w;
	bool isCharacter = (extraMaterialMask & MASK_BIT_EXTRA_CHARACTER);

	// check if pixel is hybrid lit
	bool isHybridProbeLit = (gbuffer1Data.x & MASK_BIT_SPECIAL_HAS_HYBRID_PROBE_LIGHTING);

	// check if pixel is translucent
	uint materialMask = pBufs->m_tex_materialMask.Load(int3(screenLocation, 0));
	bool isTranslucent = (materialMask & MASK_BIT_TRANSLUCENCY) && !(materialMask & MASK_BIT_SKIN);
	float translucency = 0.0f;
	if (isTranslucent)
	{
		const uint metallicOrTranslucencyBits = (gbuffer1Data.y >> 8u) & 0x1F;
		float metallicOrTranslucency = (float)metallicOrTranslucencyBits / 31.0f;
		const uint metallicSelector = (gbuffer1Data.y >> 13u) & 0x01;
		translucency = metallicSelector ? 0.0f : metallicOrTranslucency;
	}

	// compute lighting
	VolProbeLightingInput input;
	input.tex_probeShs = pBufs->m_tex_probeShs;
	input.tex_probeScale = pBufs->m_tex_probeScale;
	input.linearSampler = pSamplers->m_smp_linear;
	input.worldToTextureMatrix = pBufs->m_buf_worldToTextureMatrices[objectId];
	input.tileBounds = pBufs->m_buf_tileBounds[objectId];
	input.positionWS = positionWS;
	input.normalWS = normalWS;
	input.ambientScale = ambientScale;
	input.ambientBgScale = pConsts->m_ambientBgScale;
	input.characterDirectionality = pConsts->m_characterDirectionality;
	input.characterFakeAmbient = pConsts->m_characterFakeAmbient;
	input.probeDeringingStrength = pConsts->m_probeDeringingStrength;
	input.translucency = translucency;
	input.isBackground = isBackground;
	input.isCharacter = isCharacter;
	input.isTranslucent = isTranslucent;
	input.isHybridProbeLit = isHybridProbeLit;

	VolProbeLightingOutput output;
	ComputeVolProbeLighting(input, output);
	
	// apply hybrid probe lighting
	if (isHybridProbeLit)
	{
		float2 basis3Luminance = pBufs->m_rwt_ambientDirectional[screenLocation].yz;
		output.ambientBaseColor = SwapLuminance(output.ambientBaseColor, basis3Luminance.x);
		output.ambientDirectionalColor = SwapLuminance(output.ambientDirectionalColor, basis3Luminance.y);
		if (input.isTranslucent)
			output.ambientTranslucencyColor = SwapLuminance(output.ambientTranslucencyColor, basis3Luminance.x);
	}
	
	output.ambientBaseColor += output.ambientTranslucencyColor * input.translucency;

	// output gbuffer1 data with the computed dominant direction
	float2 encodedDomDir = EncodeNormal(output.dominantDirection);
	gbuffer1Data.z = PackFloat2ToUInt16(encodedDomDir.x, encodedDomDir.y);
	
	ApplyVolProbeOutput result = (ApplyVolProbeOutput)0;
	result.m_rt_ambientBase = output.ambientBaseColor;
	result.m_rt_ambientDirectional = output.ambientDirectionalColor;
	result.m_rt_gbuffer1 = gbuffer1Data;

	return result;
}

ApplyVolProbeOutput PS_ApplyVolProbeBuffer(PS_PosTex psInput, ApplyVolProbeBufferSrt* pSrt : S_SRT_DATA)
{
	return ApplyVolProbeBuffer((int2)psInput.Pos.xy, pSrt->pBufs, pSrt->pConsts, pSrt->pSamplers);
}