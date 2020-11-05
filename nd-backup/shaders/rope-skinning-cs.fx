#include "global-funcs.fxi"

#define ND_PSSL
#include "compressed-vsharp.fxi"
#include "compressed-tangent-frame.fxi"

#define kNumThreadsInGroup	   1024

#include "skinning.fxi"
#include "vol-probe-lighting-cs.fx"

struct ComputeRopeProbeBufferSrt
{
	uint									m_rootIndices[kNumProbeTrees];
	uint									m_probeIndexOffsets[kNumProbeTrees];
	float4									m_centerPosLs[kNumProbeTrees];
	float4									m_edgeLengthLs[kNumProbeTrees];
	float4									m_wsToLsMatrices0[kNumProbeTrees];
	float4									m_wsToLsMatrices1[kNumProbeTrees];
	float4									m_wsToLsMatrices2[kNumProbeTrees];
	float4									m_lsToWsMatrices0[kNumProbeTrees];
	float4									m_lsToWsMatrices1[kNumProbeTrees];
	float4									m_lsToWsMatrices2[kNumProbeTrees];
	float4									m_ropeLsToWsMatrix0;
	float4									m_ropeLsToWsMatrix1;
	float4									m_ropeLsToWsMatrix2;
	CompressedProbe							m_defaultProbe;
	uint									m_numRopeBones;
	uint									m_numNodeTrees;
	float									m_maxBlurRadius;
	uint									m_useOcclusion;
	float									m_occlusionScale;
	float									m_occlusionBias;
	StructuredBuffer<uint2>					m_buf_probeNodes;
	StructuredBuffer<float4>				m_buf_probePos;
	StructuredBuffer<CompressedProbe>		m_buf_probes;
	ByteAddressBuffer						m_buf_ropeBones;
	RWStructuredBuffer<CacheEntry>			m_rwbuf_ropeProbes;
};

void WriteToRopeProbeBuffer(in float4 probeShs[SH9_NUM_TEXTURES], in float probeScale, in uint index,
							in RWStructuredBuffer<CacheEntry> ropeProbeBuffer)
{
	for (uint i = 0; i < SH9_NUM_TEXTURES; i++)
		ropeProbeBuffer[index].shCoeffs[i] = EncodeFloat4(probeShs[i]);
	ropeProbeBuffer[index].scale = probeScale;
}

void ReadFromRopeProbeBuffer(in StructuredBuffer<CacheEntry> ropeProbeBuffer, in uint index,
							 out float4 probeShs[SH9_NUM_TEXTURES], out float probeScale)
{
	for (uint i = 0; i < SH9_NUM_TEXTURES; i++)
		probeShs[i] = DecodeFloat4(ropeProbeBuffer[index].shCoeffs[i]);
	probeScale = ropeProbeBuffer[index].scale;
}

void RopeInternalNearestNeighbors(inout uint probeInfoList[kMaxNumProbeResults], inout uint probeCount, in ComputeRopeProbeBufferSrt* pSrt,
								  in uint nodeTreeIndex, in float3 positionLS)
{
	int stackTop = 0;
	uint stackBuffer[kStackBufferSize];
	float maxDistSqr = pSrt->m_maxBlurRadius * pSrt->m_maxBlurRadius;

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
				float3 probeVec = positionLS - pSrt->m_buf_probePos[nodeIndex].xyz;
				float distSqr = dot(probeVec, probeVec);

				if (distSqr < maxDistSqr)
				{
					float weight = exp2(-distSqr / maxDistSqr * 12.0f);
					uint probeIndex = asuint(pSrt->m_buf_probePos[nodeIndex].w) + pSrt->m_probeIndexOffsets[nodeTreeIndex];

					if (pSrt->m_useOcclusion > 0)
					{
						float probeVecLen = sqrt(distSqr);
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

[numthreads(64, 1, 1)]
void Cs_ComputeRopeAmbientProbes(uint3 dispatchThreadId : SV_DispatchThreadId, ComputeRopeProbeBufferSrt* pSrt : S_SRT_DATA)
{
	uint boneIndex = dispatchThreadId.x;
	if (boneIndex < pSrt->m_numRopeBones)
	{
		BoneMatrix boneMatrix = LoadBoneMatrix(pSrt->m_buf_ropeBones, boneIndex);
		float3 bonePosRopeLS = float3(boneMatrix.m_row[0].w, boneMatrix.m_row[1].w, boneMatrix.m_row[2].w);

		float3x4 localToWorldMat;
		localToWorldMat[0] = pSrt->m_ropeLsToWsMatrix0;
		localToWorldMat[1] = pSrt->m_ropeLsToWsMatrix1;
		localToWorldMat[2] = pSrt->m_ropeLsToWsMatrix2;
		float3 bonePosWS = mul(localToWorldMat, float4(bonePosRopeLS, 1.0f)).xyz;

		// find 10 closest probes within search radius
		uint probeInfoList[kMaxNumProbeResults];
		uint probeCount = 0;
		for (uint i = 0; i < pSrt->m_numNodeTrees; i++)
		{
			float3x4 worldToLocalMat;
			worldToLocalMat[0] = pSrt->m_wsToLsMatrices0[i];
			worldToLocalMat[1] = pSrt->m_wsToLsMatrices1[i];
			worldToLocalMat[2] = pSrt->m_wsToLsMatrices2[i];
			float3 positionLS = mul(worldToLocalMat, float4(bonePosWS, 1.0f)).xyz;

			RopeInternalNearestNeighbors(probeInfoList, probeCount, pSrt, i, positionLS);
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

		// output accumulated probe samples
		float4 shCoeffs[SH9_NUM_TEXTURES];
		shCoeffs[0] = float4(probeSum.s0.x, probeSum.s3.x, probeSum.s2.y, probeSum.s1.z);
		shCoeffs[1] = float4(probeSum.s1.x, probeSum.s0.y, probeSum.s3.y, probeSum.s2.z);
		shCoeffs[2] = float4(probeSum.s2.x, probeSum.s1.y, probeSum.s0.z, probeSum.s3.z);
		shCoeffs[3] = float4(probeSum.s4.x, probeSum.s7.x, probeSum.s6.y, probeSum.s5.z);
		shCoeffs[4] = float4(probeSum.s5.x, probeSum.s4.y, probeSum.s7.y, probeSum.s6.z);
		shCoeffs[5] = float4(probeSum.s6.x, probeSum.s5.y, probeSum.s4.z, probeSum.s7.z);
		shCoeffs[6] = float4(probeSum.s4.w, probeSum.s5.w, probeSum.s6.w, 0.0f);
		probeScale = maxSS * invWeightSum;
		WriteToRopeProbeBuffer(shCoeffs, probeScale, boneIndex, pSrt->m_rwbuf_ropeProbes);
	}
}

struct RopeSkinningInfoBuffer
{
	float4			m_ropeLsToWsMatrix0;
	float4			m_ropeLsToWsMatrix1;
	float4			m_ropeLsToWsMatrix2;
	uint			m_numVertexes;
	uint			m_numBones;
	uint			m_numMeshSegments;
	float			m_meshSegmentLength;
	float			m_textureVRate;
};

struct RopeSkinningInputBuffers
{
	RWBuffer<float3>                        m_outPos;
	RWBuffer<float3>                        m_outNrm;
	RWBuffer<float4>                        m_outTan;
	RWBuffer<float2>                        m_outUv;
	RWBuffer<float3>                        m_outPosLastFrame;
	RWBuffer<float3>                        m_outAmbient;

	ByteAddressBuffer                       m_bone;
	ByteAddressBuffer                       m_prev;
	ByteAddressBuffer                       m_boneRopeDist;
	StructuredBuffer<CacheEntry>            m_probes;

	CompressedVSharp                        m_pos;
	DataBuffer<float3>                      m_nrm;
	DataBuffer<float4>                      m_tan;
	CompressedVSharp                        m_uv;
};

struct RopeSkinningSrtData
{
	uint4								 m_globalVertBuffer;
	RopeSkinningInputBuffers *           m_bufs;	
	RopeSkinningInfoBuffer *             m_pInfo;
};

[numthreads(kNumThreadsInGroup, 1, 1)]
void Cs_RopeSkinning(uint3 groupThreadId : SV_GroupThreadId,
					 uint3 dispatchThreadId : SV_DispatchThreadId,
					 RopeSkinningSrtData srt : S_SRT_DATA)
{
	uint numVertexes = srt.m_pInfo->m_numVertexes;
	uint numBones    = srt.m_pInfo->m_numBones;
	
	uint vertexId = dispatchThreadId.x; 
	if (vertexId < numVertexes)
	{
		float3 inputPos = LoadVertexAttribute<float3, 72>(srt.m_globalVertBuffer, srt.m_bufs->m_pos, vertexId);
		float2 inputUv = LoadVertexAttribute<float2, 32>(srt.m_globalVertBuffer, srt.m_bufs->m_uv, vertexId);

		float3 inputNrm;
		float4 inputTan;
		LoadCompressedTangentFrameNT(srt.m_bufs->m_nrm, srt.m_bufs->m_tan, inputNrm, inputTan, vertexId);

		int boneIdx = (int)(-inputPos.y / srt.m_pInfo->m_meshSegmentLength + 0.5);

		float3 inputPosSegLocal;
		if (boneIdx > 0 && boneIdx < srt.m_pInfo->m_numMeshSegments)
		{
			// To avoid modeling and numerical precision issues just put 0.0 into the local y
			inputPosSegLocal = float3(inputPos.x, 0, inputPos.z);
		}
		else
		{
			// ... unless it's a cap to preserve possible modeling fancyness at the caps
			inputPosSegLocal = float3(inputPos.x, inputPos.y + boneIdx * srt.m_pInfo->m_meshSegmentLength, inputPos.z);
		}

		// Put a cap (the last segment of the mesh) onto the last bone 
		boneIdx = boneIdx == srt.m_pInfo->m_numMeshSegments ? numBones-1 : boneIdx >= numBones-1 ? numBones-2 : boneIdx;

		float3 inputENrm = float3(0.0f);
		
		float3 skinnedPos     = float3(0.0f, 0.0f, 0.0f);
		float3 prevSkinnedPos = float3(0.0f, 0.0f, 0.0f);
		float3 skinnedNrm     = float3(0.0f, 0.0f, 0.0f);
		float3 skinnedENrm    = float3(0.0f, 0.0f, 0.0f);
		float4 skinnedTan     = float4(0.0f, 0.0f, 0.0f, inputTan.w);

		AddSkinnedInfluence(srt.m_bufs->m_bone, srt.m_bufs->m_prev, boneIdx, 1.0f,
							inputPosSegLocal, skinnedPos, prevSkinnedPos,
							inputNrm, skinnedNrm,
							inputENrm, skinnedENrm,
							inputTan.xyz, skinnedTan);

		// We do this because the boneMtx may contain scale
		skinnedNrm = normalize(skinnedNrm);

		float3 skinnedBinormal = cross(skinnedNrm, skinnedTan.xyz);
		skinnedTan.xyz = normalize(cross(skinnedBinormal, skinnedNrm));

		float2 outUv = inputUv;
		if (boneIdx > 0 && (boneIdx < numBones-1 || abs(inputNrm.y) < 0.1f))
		{
			// We moved this vtx along y so we need to adjust the u param
			// Don't do this on the first bone (to avoid division by zero) and last bone if normal is not horizontal (to not mess up with cap uvs)
			float boneRopeDist = LoadAsFloat(srt.m_bufs->m_boneRopeDist, 4 * boneIdx);
			//outUv.y = inputUv.y * boneRopeDist/-inputPos.y; // this does not work well. Input UVs don't have enough pecision (fp16)
			outUv.y = boneRopeDist * srt.m_pInfo->m_textureVRate;
		}

		// compute ambient lighting
		float3 normalLS = skinnedNrm;
		float3x4 localToWorldMat;
		localToWorldMat[0] = srt.m_pInfo->m_ropeLsToWsMatrix0;
		localToWorldMat[1] = srt.m_pInfo->m_ropeLsToWsMatrix1;
		localToWorldMat[2] = srt.m_pInfo->m_ropeLsToWsMatrix2;
		float3 normalWS = mul(localToWorldMat, float4(normalLS, 0.0f)).xyz;

		float4 shCoeffs[SH9_NUM_TEXTURES];
		float probeScale;
		ReadFromRopeProbeBuffer(srt.m_bufs->m_probes, boneIdx, shCoeffs, probeScale);
		float3 ambient = max(SampleProbe(normalWS, shCoeffs, probeScale), 0.0f);

		//
		// 32 / gcd(32, 10) = 32 / 2 = 16, half efficiency.
		// 
		srt.m_bufs->m_outPos[vertexId] = skinnedPos;
		srt.m_bufs->m_outNrm[vertexId] = skinnedNrm;
		srt.m_bufs->m_outTan[vertexId] = skinnedTan;
		srt.m_bufs->m_outUv[vertexId] = outUv;
		srt.m_bufs->m_outPosLastFrame[vertexId] = prevSkinnedPos;
		srt.m_bufs->m_outAmbient[vertexId] = ambient;
	}
}

