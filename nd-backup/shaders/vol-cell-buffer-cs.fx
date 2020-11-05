#define THREADS_PER_WAVEFRONT 64

#include "global-srt.fxi"
#include "post-globals.fxi"
#include "post-processing-common.fxi"

struct CompressedProbe
{
	float		m_colorScale;						// 4
	int3		m_shCompressed0;					// 28
	int4		m_shCompressed1;				
	uint2		m_occlusion;
};

// Expands a 10-bit integer into 30 bits
// by inserting 2 zeros after each bit.
uint expandBits(uint v)
{
    v = (v * 0x00010001u) & 0xFF0000FFu;
    v = (v * 0x00000101u) & 0x0F00F00Fu;
    v = (v * 0x00000011u) & 0xC30C30C3u;
    v = (v * 0x00000005u) & 0x49249249u;
    return v;
}

// Calculates a 30-bit Morton code for the
// given 3D point located within the unit cube [0,1].
uint morton3D(float x, float y, float z)
{
    uint ux = clamp(uint(x * 128.0f), 0, 127);
    uint uy = clamp(uint(y * 128.0f), 0, 127);
    uint uz = clamp(uint(z * 128.0f), 0, 127);
    uint xx = expandBits(ux);
    uint yy = expandBits(uy);
    uint zz = expandBits(uz);

    return xx * 4 + yy * 2 + zz;
}

#define kMaxNodesCount		128

groupshared uint			g_totalCount;
groupshared uint			g_totalProbeCount[3];
groupshared uint			g_totalProbeOffset[3];
groupshared uint			g_tmpCount;
groupshared uint			g_slotCount[64];
groupshared uint			g_slotOffset[64];
groupshared uint			g_slotTmpBuf[64 + 1];
groupshared uint2			g_nodeList[kMaxNodesCount + 2];
groupshared uint			g_nearVisibleIndex[64];
groupshared uint			g_midVisibleIndex[64];
groupshared uint			g_farVisibleIndex[64];

struct CullingProbesSrt
{
	uint						m_totalKdTreeNodes;
	float						m_volCellSize[3];
	float						m_maxBlurRadius;
	uint3						m_pad;
	float4						m_clipPlanes[4];
	float4						m_nearBBCenter;
	float4						m_nearBBRange;
	float4						m_midBBCenter;
	float4						m_midBBRange;
	float4						m_farBBCenter;
	float4						m_farBBRange;

	StructuredBuffer<float3>			m_srcProbePosList;
	StructuredBuffer<CompressedProbe>	m_srcProbesList;

	RWByteAddressBuffer					m_rwVisiProbeCountBuffer;
	RWStructuredBuffer<float3>			m_rwProbePosNearList;
	RWStructuredBuffer<CompressedProbe>	m_rwProbesNearList;
	RWStructuredBuffer<float3>			m_rwProbePosMidList;
	RWStructuredBuffer<CompressedProbe>	m_rwProbesMidList;
	RWStructuredBuffer<float3>			m_rwProbePosFarList;
	RWStructuredBuffer<CompressedProbe>	m_rwProbesFarList;
};

[numthreads(64, 1, 1)]
void CS_CullingProbes(int dispatchThreadId : SV_DispatchThreadId, int groupThreadId : SV_GroupThreadId, CullingProbesSrt* pSrt : S_SRT_DATA)
{
	if (groupThreadId < 3)
	{
		g_totalProbeCount[groupThreadId] = 0;
		g_totalProbeOffset[groupThreadId] = 0;
	}

	if (dispatchThreadId < pSrt->m_totalKdTreeNodes)
	{
		float3 probePos = pSrt->m_srcProbePosList[dispatchThreadId];
		float distToPlane[4];
		distToPlane[0] = dot(probePos, pSrt->m_clipPlanes[0].xyz) + pSrt->m_clipPlanes[0].w;
		distToPlane[1] = dot(probePos, pSrt->m_clipPlanes[1].xyz) + pSrt->m_clipPlanes[1].w;
		distToPlane[2] = dot(probePos, pSrt->m_clipPlanes[2].xyz) + pSrt->m_clipPlanes[2].w;
		distToPlane[3] = dot(probePos, pSrt->m_clipPlanes[3].xyz) + pSrt->m_clipPlanes[3].w;

		float minDistToPlane = min(min3(distToPlane[0], distToPlane[1], distToPlane[2]), distToPlane[3]);

		if (minDistToPlane > -pSrt->m_maxBlurRadius - pSrt->m_volCellSize[0]) // in near frustum.
		{
			if (all(abs(probePos - pSrt->m_nearBBCenter.xyz) < pSrt->m_nearBBRange))
			{
				uint currentIdx;
				InterlockedAdd(g_totalProbeCount[0], 1u, currentIdx);
				g_nearVisibleIndex[currentIdx] = dispatchThreadId;
			}
		}

		if (minDistToPlane > -pSrt->m_maxBlurRadius - pSrt->m_volCellSize[1]) // in mid frustum.
		{
			if (all(abs(probePos - pSrt->m_midBBCenter.xyz) < pSrt->m_midBBRange))
			{
				uint currentIdx;
				InterlockedAdd(g_totalProbeCount[1], 1u, currentIdx);
				g_midVisibleIndex[currentIdx] = dispatchThreadId;
			}
		}

		if (minDistToPlane > -pSrt->m_maxBlurRadius - pSrt->m_volCellSize[2]) // in far frustum.
		{
			if (all(abs(probePos - pSrt->m_farBBCenter.xyz) < pSrt->m_farBBRange))
			{
				uint currentIdx;
				InterlockedAdd(g_totalProbeCount[2], 1u, currentIdx);
				g_farVisibleIndex[currentIdx] = dispatchThreadId;
			}
		}
	}

	if (groupThreadId < 3 && g_totalProbeCount[groupThreadId] > 0)
	{
		pSrt->m_rwVisiProbeCountBuffer.InterlockedAdd(groupThreadId * 4, g_totalProbeCount[groupThreadId], g_totalProbeOffset[groupThreadId]);
	}

	if (groupThreadId < g_totalProbeCount[0])
	{
		pSrt->m_rwProbePosNearList[g_totalProbeOffset[0] + groupThreadId] = pSrt->m_srcProbePosList[g_nearVisibleIndex[groupThreadId]];
		pSrt->m_rwProbesNearList[g_totalProbeOffset[0] + groupThreadId] = pSrt->m_srcProbesList[g_nearVisibleIndex[groupThreadId]];
	}

	if (groupThreadId < g_totalProbeCount[1])
	{
		pSrt->m_rwProbePosMidList[g_totalProbeOffset[1] + groupThreadId] = pSrt->m_srcProbePosList[g_midVisibleIndex[groupThreadId]];
		pSrt->m_rwProbesMidList[g_totalProbeOffset[1] + groupThreadId] = pSrt->m_srcProbesList[g_midVisibleIndex[groupThreadId]];
	}

	if (groupThreadId < g_totalProbeCount[2])
	{
		pSrt->m_rwProbePosFarList[g_totalProbeOffset[2] + groupThreadId] = pSrt->m_srcProbePosList[g_farVisibleIndex[groupThreadId]];
		pSrt->m_rwProbesFarList[g_totalProbeOffset[2] + groupThreadId] = pSrt->m_srcProbesList[g_farVisibleIndex[groupThreadId]];
	}
}

struct CalculateBlockCenterPositionSrt
{
	uint						m_totalNumBlocks;
	float						m_maxBlurRadius;
	float						m_volBlockSize;
	uint						m_pad0;
	float3						m_centerPosWS;
	uint						m_pad1;
	uint3						m_blockSize;
	uint						m_pad2;

	RWStructuredBuffer<float3>	m_rwCenterPositionList;
};

[numthreads(64, 1, 1)]
void CS_CalculateBlockCenterPosition(int dispatchThreadId : SV_DispatchThreadId, CalculateBlockCenterPositionSrt* pSrt : S_SRT_DATA)
{
	if (dispatchThreadId < pSrt->m_totalNumBlocks)
	{
		uint3 newDispatchId;
		newDispatchId.x = dispatchThreadId % pSrt->m_blockSize.x;
		newDispatchId.y = (dispatchThreadId / pSrt->m_blockSize.x) % pSrt->m_blockSize.y;
		newDispatchId.z = (dispatchThreadId / pSrt->m_blockSize.x) / pSrt->m_blockSize.y;

		float3 positionWS = (newDispatchId + 0.5f) * pSrt->m_volBlockSize + pSrt->m_centerPosWS;

		pSrt->m_rwCenterPositionList[dispatchThreadId] = positionWS;
	}
}

struct CountBlockNodesSrt
{
	float						m_maxBlurRadius;
	float						m_volCellSize;
	uint						m_lodOffset;
	uint						m_pad;

	ByteAddressBuffer			m_visiProbeCountBuffer;
	StructuredBuffer<float3>	m_probePositionList;
	StructuredBuffer<float3>	m_centerPositionList;
	RWByteAddressBuffer			m_rwNodesCountList;
};

[numthreads(64, 1, 1)]
void CS_CountBlockNodes(int2 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, CountBlockNodesSrt* pSrt : S_SRT_DATA)
{
	uint nodeIndex = dispatchThreadId.x;
	uint blockIndex = dispatchThreadId.y;
	uint localIndex = groupThreadId.x;

	g_slotCount[localIndex] = 0;

	uint totalKdTreeNodes = ReadFirstLane(pSrt->m_visiProbeCountBuffer.Load(pSrt->m_lodOffset));

	if (nodeIndex < totalKdTreeNodes)
	{
		float edgeSize = pSrt->m_maxBlurRadius + pSrt->m_volCellSize * 8.0f;
	
		float3 edgeVec = (pSrt->m_probePositionList[nodeIndex].xyz - pSrt->m_centerPositionList[blockIndex]);
		float3 absEdgeVec = abs(edgeVec);

		if (max3(absEdgeVec.x, absEdgeVec.y, absEdgeVec.z) < edgeSize)
		{
			float3 normalizedEdgeVec = saturate(edgeVec / edgeSize * 0.5f + 0.5f);

			uint mortonCode = morton3D(normalizedEdgeVec.x, normalizedEdgeVec.y, normalizedEdgeVec.z);
			uint bucketIdx = mortonCode >> 15;

			uint currentSlotIdx;
			InterlockedAdd(g_slotCount[bucketIdx], 1u, currentSlotIdx);
			uint storeIdx;
		}
	}

	if ((localIndex & 0x01) == 0x01)
		g_slotCount[localIndex] += g_slotCount[localIndex - 1];

	if ((groupThreadId & 0x02) == 0x02)
		g_slotCount[localIndex] += g_slotCount[(localIndex&0x3E) - 1];

	if ((groupThreadId & 0x04) == 0x04)
		g_slotCount[localIndex] += g_slotCount[(localIndex&0x3C) - 1];

	if ((groupThreadId & 0x08) == 0x08)
		g_slotCount[localIndex] += g_slotCount[(localIndex&0x38) - 1];

	if ((groupThreadId & 0x10) == 0x10)
		g_slotCount[localIndex] += g_slotCount[(localIndex&0x30) - 1];

	if ((groupThreadId & 0x20) == 0x20)
		g_slotCount[localIndex] += g_slotCount[0x1f];

	uint index = blockIndex * 65 + localIndex;
	uint uOldAccuValue;
	if (g_slotCount[localIndex] > 0)
		pSrt->m_rwNodesCountList.InterlockedAdd(index * 4, g_slotCount[localIndex], uOldAccuValue);

}

struct CalculateBlockOffsetSrt
{
	uint						m_numBlocks;
	uint3						m_pad;

	RWStructuredBuffer<uint>	m_rwNodesCountList;
};

[numthreads(64, 1, 1)]
void CS_CalculateBlockOffset(int dispatchThreadId : SV_DispatchThreadId, CalculateBlockOffsetSrt* pSrt : S_SRT_DATA)
{
	int localIndex = dispatchThreadId;

	if (localIndex < pSrt->m_numBlocks)
		g_slotCount[localIndex] = pSrt->m_rwNodesCountList[localIndex * 65 + 63];
	else 
		g_slotCount[localIndex] = 0;

	if ((localIndex & 0x01) == 0x01)
		g_slotCount[localIndex] += g_slotCount[localIndex - 1];

	if ((localIndex & 0x02) == 0x02)
		g_slotCount[localIndex] += g_slotCount[(localIndex&0x3E) - 1];

	if ((localIndex & 0x04) == 0x04)
		g_slotCount[localIndex] += g_slotCount[(localIndex&0x3C) - 1];

	if ((localIndex & 0x08) == 0x08)
		g_slotCount[localIndex] += g_slotCount[(localIndex&0x38) - 1];

	if ((localIndex & 0x10) == 0x10)
		g_slotCount[localIndex] += g_slotCount[(localIndex&0x30) - 1];

	if ((localIndex & 0x20) == 0x20)
		g_slotCount[localIndex] += g_slotCount[0x1f];

	if (localIndex < pSrt->m_numBlocks)
		pSrt->m_rwNodesCountList[localIndex * 65 + 64] = g_slotCount[localIndex];
}

struct BucketBlockNodesSrt
{
	float						m_maxBlurRadius;
	float						m_volCellSize;
	uint						m_lodOffset;
	uint						m_pad;

	ByteAddressBuffer			m_visiProbeCountBuffer;
	StructuredBuffer<float3>	m_probePositionList;
	StructuredBuffer<float3>	m_centerPositionList;
	StructuredBuffer<uint>		m_nodesCountList;
	RWByteAddressBuffer			m_rwTmpNodesCountList;
	RWStructuredBuffer<uint2>	m_rwValidNodeList;
};

[numthreads(64, 1, 1)]
void CS_BucketBlockNodes(int2 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, BucketBlockNodesSrt* pSrt : S_SRT_DATA)
{
	uint nodeIndex = dispatchThreadId.x;
	uint blockIndex = dispatchThreadId.y;
	uint localIndex = groupThreadId.x;

	uint blockStartOffset = 0;
	if (blockIndex > 0)
		blockStartOffset = pSrt->m_nodesCountList[(blockIndex - 1) * 65 + 64];

	g_slotTmpBuf[0] = 0;
	g_slotTmpBuf[localIndex + 1] = pSrt->m_nodesCountList[blockIndex * 65 + localIndex];

	uint totalKdTreeNodes = ReadFirstLane(pSrt->m_visiProbeCountBuffer.Load(pSrt->m_lodOffset));
	if (nodeIndex < totalKdTreeNodes)
	{
		float edgeSize = pSrt->m_maxBlurRadius + pSrt->m_volCellSize * 8.0f;
	
		float3 edgeVec = (pSrt->m_probePositionList[nodeIndex].xyz - pSrt->m_centerPositionList[blockIndex]);
		float3 absEdgeVec = abs(edgeVec);

		if (max3(absEdgeVec.x, absEdgeVec.y, absEdgeVec.z) < edgeSize)
		{
			float3 normalizedEdgeVec = saturate(edgeVec / edgeSize * 0.5f + 0.5f);

			uint mortonCode = morton3D(normalizedEdgeVec.x, normalizedEdgeVec.y, normalizedEdgeVec.z);
			uint bucketIdx = mortonCode >> 15;

			uint storeIdx = blockIndex * 64 + bucketIdx;
			uint slotOffset;
			pSrt->m_rwTmpNodesCountList.InterlockedAdd(storeIdx * 4, 1, slotOffset);

			pSrt->m_rwValidNodeList[blockStartOffset + g_slotTmpBuf[bucketIdx] + slotOffset] = uint2(mortonCode, nodeIndex);
		}
	}
}

//[startIndex(0 : 5)][endIndex(6 : 12)][startOffset(13:26)][splitAxis(27:31)]
//[dstOffset(0 : 16)][endOffset(17 : 30)][validNode(31)]

// word 0:
#define kStartIdxBitCount		6
#define kEndIndexBitOfs			kStartIdxBitCount
#define kEndIdxBitCount			7
#define kStartOffsetBitOfs		(kEndIndexBitOfs + kEndIdxBitCount)
#define kStartOffsetBitCount	14
#define kSplitAxisBitOfs		(kStartOffsetBitOfs + kStartOffsetBitCount)

#define kStartIdxBitMask		((1<<kStartIdxBitCount) - 1)
#define kEndIdxBitMask			(((1<<kEndIdxBitCount) - 1) << kEndIndexBitOfs)
#define kStartOffsetBitMask		(((1<<kStartOffsetBitCount) - 1) << kStartOffsetBitOfs)

// word 1:
#define kDstOffsetBitCount		17
#define kEndOffsetBitOfs		kDstOffsetBitCount
#define kEndOffsetBitCount		14

#define kDstOffsetBitMask		((1<<kDstOffsetBitCount) - 1)
#define kEndOffsetBitMask		(((1<<kEndOffsetBitCount) - 1) << kEndOffsetBitOfs)

uint GetStartIndex(uint2 node)
{
	return node.x & kStartIdxBitMask;
}

uint GetEndIndex(uint2 node)
{
	return (node.x & kEndIdxBitMask) >> kEndIndexBitOfs;
}

uint GetClampStartOffset(uint2 node)
{
	return (node.x & kStartOffsetBitMask) >> kStartOffsetBitOfs;
}

uint GetSplitAxis(uint2 node)
{
	return node.x >> kSplitAxisBitOfs;
}

uint GetDstOffset(uint2 node)
{
	return node.y & kDstOffsetBitMask;
}

uint GetClampEndOffset(uint2 node)
{
	return (node.y & kEndOffsetBitMask) >> kEndOffsetBitOfs;
}

bool IsNeedCreateSubTreeNode(uint2 node)
{
	return node.y >= 0x80000000;
}

void IncrementSplitAxis(inout uint2 node)
{
	node.x += (1 << kSplitAxisBitOfs);
}

uint2 ReplaceStartEndIndex(uint2 node, uint startIdx, uint endIdx)
{
	return uint2((node.x & ~(kEndIdxBitMask | kStartIdxBitMask)) | startIdx | (endIdx << kEndIndexBitOfs),
				 node.y);
}

uint2 ReplaceStartIndexIncrementAxis(uint2 node, uint startIdx)
{
	return uint2(((node.x & ~kStartIdxBitMask) | startIdx) + (1 << kSplitAxisBitOfs),
				 node.y);
}

uint2 ReplaceEndIndexIncrementAxis(uint2 node, uint endIdx)
{
	return uint2(((node.x & ~kEndIdxBitMask) | (endIdx << kEndIndexBitOfs)) + (1 << kSplitAxisBitOfs),
				 node.y);
}

uint2 ReplaceStartIndexOffsetAndDstOffsetIncrementAxis(uint2 node, uint startIdx, uint startOffset, uint dstOffset)
{
	return uint2(((node.x & ~(kStartIdxBitMask | kStartOffsetBitMask)) | startIdx | (startOffset << kStartOffsetBitOfs)) + (1 << kSplitAxisBitOfs),
				 (node.y & ~kDstOffsetBitMask) | dstOffset);
}

uint2 ReplaceEndIndexOffsetAndDstOffsetIncrementAxis(uint2 node, uint endIdx, uint endOffset, uint dstOffset)
{
	return uint2(((node.x & ~kEndIdxBitMask) | (endIdx << kEndIndexBitOfs)) + (1 << kSplitAxisBitOfs),
				 (node.y & ~(kDstOffsetBitMask | kEndOffsetBitMask)) | dstOffset | (endOffset << kEndOffsetBitOfs));
}

void SetTreeNode(int idx, uint2 node, int bufferOffset, int blockIdx, RWStructuredBuffer<uint2> rwTreeNode, uint nodeOffset[65])
{
	if (IsNeedCreateSubTreeNode(node))
	{
		uint startIndex = GetStartIndex(node);
		uint srcOffset = GetClampStartOffset(node);
		uint dstOffset = GetDstOffset(node);

		uint endIndex = GetEndIndex(node);
		uint srcEndOffset = GetClampEndOffset(node);
		uint splitAxis = GetSplitAxis(node);

		uint medIndex = (startIndex + endIndex) / 2;
		uint splitOffset = clamp(nodeOffset[medIndex], srcOffset, srcEndOffset);
		uint startOffset = max(nodeOffset[startIndex], srcOffset);
		int endOffset = min((int)nodeOffset[endIndex] - 1, (int)srcEndOffset);

		int numLeftNodes = int(splitOffset - 1) - int(startOffset) + 1;
		int numRightNodes = endOffset - int(splitOffset + 1) + 1;

		bool bHasLeftNode = numLeftNodes > 0;
		bool bHasRightNode = numRightNodes > 0;

		int rightNodeStart = dstOffset + numLeftNodes + 1;

		if (endOffset == startOffset)
		{
			rwTreeNode[dstOffset + bufferOffset].x = splitOffset;
			rwTreeNode[dstOffset + bufferOffset].y = (0x1f << 27) | (blockIdx << 17) | bufferOffset;
		}
		else
		{
			// for a branch, we always has a left sub tree, or it will continue to next level.
			if (splitOffset <= startOffset || nodeOffset[startIndex] == nodeOffset[medIndex])
			{
				g_nodeList[idx + 1] = ReplaceStartIndexIncrementAxis(node, medIndex);
			}
			else if ((int)endOffset <= (int)splitOffset-1 || nodeOffset[medIndex] == nodeOffset[endIndex])
			{
				g_nodeList[idx] = ReplaceEndIndexIncrementAxis(node, medIndex);
			}
			else
			{
				uint nodeInfo = (bHasLeftNode ? (1 << 31) : 0) | ((bHasRightNode ? rightNodeStart : 0x3FFF) << 17) | splitOffset;
				rwTreeNode[dstOffset + bufferOffset].x = nodeInfo;
				rwTreeNode[dstOffset + bufferOffset].y = (splitAxis << 27) | (blockIdx << 17) | bufferOffset;

				if (bHasLeftNode)
				{
					g_nodeList[idx] = ReplaceEndIndexOffsetAndDstOffsetIncrementAxis(node, medIndex, splitOffset - 1, dstOffset + 1);
				}

				if (bHasRightNode)
				{
					g_nodeList[idx + 1] = ReplaceStartIndexOffsetAndDstOffsetIncrementAxis(node, medIndex, splitOffset + 1, rightNodeStart);
				}
			}
		}
	}
}

struct CreateL1TreekNodesSrt
{
	uint						m_argIndex;
	uint3						m_pad;

	StructuredBuffer<uint>		m_nodesCountList;
	RWStructuredBuffer<uint2>	m_rwTreeNode;
	RWStructuredBuffer<uint4>	m_rwBlockInfoList;
	RWByteAddressBuffer			m_rwArgsBuffer;
};

[numthreads(64, 1, 1)]
void CS_CreateL1TreekNodes(int2 dispatchThreadId : SV_DispatchThreadId, uint2 groupThreadId : SV_GroupThreadId, CreateL1TreekNodesSrt* pSrt : S_SRT_DATA)
{
	uint nodeIndex = dispatchThreadId.x;
	uint blockIndex = dispatchThreadId.y;
	uint localIndex = groupThreadId.x;

	uint blockStartOffset = 0;
	if (blockIndex > 0)
		blockStartOffset = pSrt->m_nodesCountList[(blockIndex - 1) * 65 + 64];

	g_slotTmpBuf[0] = 0;
	g_slotTmpBuf[localIndex + 1] = pSrt->m_nodesCountList[blockIndex * 65 + localIndex];

	g_nodeList[localIndex] = 0;
	g_nodeList[localIndex + 64] = 0;

	if (g_slotTmpBuf[64] > 0)
	{
		uint2 nodeInfo;
		nodeInfo.x = 64 << kEndIndexBitOfs;
		nodeInfo.y = ((g_slotTmpBuf[64] - 1) << kEndOffsetBitOfs) | 0x80000000;

		// level 0.
		uint offset = 0;
		if (localIndex == 0)
			SetTreeNode(offset, nodeInfo, blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);
		uint prevOffset = offset;
		offset += 2;

		// level 1.
		if (localIndex < 2)
			SetTreeNode(offset + localIndex * 2, g_nodeList[prevOffset + localIndex], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
		prevOffset = offset;
		offset += 4;

		// level 2.
		if (localIndex < 4)
			SetTreeNode(offset + localIndex * 2, g_nodeList[prevOffset + localIndex], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
		prevOffset = offset;
		offset += 8;

		// level 3.
		if (localIndex < 8)
			SetTreeNode(offset + localIndex * 2, g_nodeList[prevOffset + localIndex], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
		prevOffset = offset;
		offset += 16;

		// level 4.
		if (localIndex < 16)
			SetTreeNode(offset + localIndex * 2, g_nodeList[prevOffset + localIndex], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
		prevOffset = offset;
		offset += 32;

		// level 5.
		if (localIndex < 32)
			SetTreeNode(offset + localIndex * 2, g_nodeList[prevOffset + localIndex], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
		prevOffset = offset;

		uint2 leafNodeInfo = g_nodeList[prevOffset + localIndex];
		uint leafStartOfs = GetClampStartOffset(leafNodeInfo);
		uint leafEndOfs = GetClampEndOffset(leafNodeInfo);
		uint leafDstOffset = GetDstOffset(leafNodeInfo);

		uint slotCount = g_slotTmpBuf[localIndex + 1] - g_slotTmpBuf[localIndex];
		uint blockSizeInfo = slotCount | (g_slotTmpBuf[localIndex] << 16);

		if (slotCount > 0) // the block is not empty, we need allocate memory for it.
		{
			uint subBlockIndex;
			pSrt->m_rwArgsBuffer.InterlockedAdd(0, 1, subBlockIndex);
		
			pSrt->m_rwBlockInfoList[subBlockIndex].x = (blockIndex << 17) | blockStartOffset;
			pSrt->m_rwBlockInfoList[subBlockIndex].y = blockSizeInfo;
			pSrt->m_rwBlockInfoList[subBlockIndex].z = leafNodeInfo.x;
			pSrt->m_rwBlockInfoList[subBlockIndex].w = leafNodeInfo.y;
		}
	}
}

struct GenerateSubTreeSrt
{
	uint						m_blockShiftBits;
	uint						m_blockMask;
	uint2						m_pad;

	StructuredBuffer<uint4>		m_blockInfoList;
	RWStructuredBuffer<uint2>	m_rwSortedSubTree;
	RWStructuredBuffer<uint2>	m_rwTmpSortedSubTree;
	RWStructuredBuffer<uint2>	m_rwTreeNode;
	RWStructuredBuffer<uint4>	m_rwBlockInfoList;
	RWByteAddressBuffer			m_rwArgsBuffer;
};

[numthreads(64, 1, 1)]
void CS_GenerateSubTree(uint groupId : SV_GroupID, uint groupThreadId : SV_GroupThreadId, GenerateSubTreeSrt* pSrt : S_SRT_DATA)
{
	uint4 blockInfo = pSrt->m_blockInfoList[groupId];

	uint blockStartOffset = blockInfo.x & 0x1ffff;
	uint blockShiftBits = pSrt->m_blockShiftBits;
	uint blockMask = pSrt->m_blockMask;
	uint blockIndex = blockInfo.x >> 17;

	// only do create when number of nodes is bigger than 0.
	uint numNodes = blockInfo.y & 0xffff;
	uint nodeStart = blockInfo.y >> 16;

	// count the number of node in each slot.
	uint targetBufferOffset = blockStartOffset + nodeStart;

	{
		// sorting subeTree.
		{
			g_slotCount[groupThreadId] = 0;

			uint localIndex = groupThreadId;
			while (localIndex < numNodes)
			{
				uint nodeData = pSrt->m_rwSortedSubTree[targetBufferOffset + localIndex].x;
				uint bucketIdx = (nodeData >> blockShiftBits) & blockMask;
				uint currentSlotIdx;
				InterlockedAdd(g_slotCount[bucketIdx], 1u, currentSlotIdx);
				localIndex += 64;
			}

			g_slotOffset[groupThreadId] = groupThreadId == 0 ? 0 : g_slotCount[groupThreadId-1];

			if ((groupThreadId & 0x01) == 0x01)
				g_slotOffset[groupThreadId] += g_slotOffset[groupThreadId-1];

			if ((groupThreadId & 0x02) == 0x02)
				g_slotOffset[groupThreadId] += g_slotOffset[(groupThreadId&0x3E)-1];

			if ((groupThreadId & 0x04) == 0x04)
				g_slotOffset[groupThreadId] += g_slotOffset[(groupThreadId&0x3C)-1];

			if ((groupThreadId & 0x08) == 0x08)
				g_slotOffset[groupThreadId] += g_slotOffset[(groupThreadId&0x38)-1];

			if ((groupThreadId & 0x10) == 0x10)
				g_slotOffset[groupThreadId] += g_slotOffset[(groupThreadId&0x30)-1];

			if ((groupThreadId & 0x20) == 0x20)
				g_slotOffset[groupThreadId] += g_slotOffset[0x1F];

			g_slotTmpBuf[groupThreadId] = g_slotOffset[groupThreadId];

			// now insert node to each bucket slot.
			localIndex = groupThreadId;
			while (localIndex < numNodes)
			{
				uint2 nodeData = pSrt->m_rwSortedSubTree[targetBufferOffset + localIndex];
				uint bucketIdx = (nodeData.x >> blockShiftBits) & blockMask;
				uint currentSlotIdx;
				InterlockedAdd(g_slotTmpBuf[bucketIdx], 1u, currentSlotIdx);
				pSrt->m_rwTmpSortedSubTree[targetBufferOffset + currentSlotIdx] = nodeData;
				localIndex += 64;
			}

			localIndex = groupThreadId;
			while (localIndex < numNodes)
			{
				pSrt->m_rwSortedSubTree[targetBufferOffset + localIndex] = pSrt->m_rwTmpSortedSubTree[targetBufferOffset + localIndex];
				localIndex += 64;
			}
		}

		uint blockSizeInfo = g_slotCount[groupThreadId] | ((g_slotOffset[groupThreadId] + nodeStart) << 16);

		// valid block, we create a sub tree.
		if (blockInfo.w >= 0x80000000) 
		{
			uint startOfs = GetClampStartOffset(blockInfo.zw);
			uint endOfs = GetClampEndOffset(blockInfo.zw);

			g_slotCount[groupThreadId] = 0;

			uint localIdx = groupThreadId;
			while (localIdx < numNodes)
			{
				if (localIdx >= (startOfs - nodeStart) && localIdx <= (endOfs - nodeStart))
				{
					uint nodeData = pSrt->m_rwTmpSortedSubTree[targetBufferOffset + localIdx].x;
					uint bucketIdx = (nodeData >> blockShiftBits) & blockMask;
					uint currentSlotIdx;
					InterlockedAdd(g_slotCount[bucketIdx], 1u, currentSlotIdx);
				}

				localIdx += 64;
			}

			g_slotTmpBuf[0] = 0;
			g_slotTmpBuf[groupThreadId+1] = g_slotCount[groupThreadId];

			// create buffer offset.
			if ((groupThreadId & 0x01) == 0x01)
				g_slotTmpBuf[groupThreadId + 1] += g_slotTmpBuf[groupThreadId];

			if ((groupThreadId & 0x02) == 0x02)
				g_slotTmpBuf[groupThreadId + 1] += g_slotTmpBuf[groupThreadId&0x3E];

			if ((groupThreadId & 0x04) == 0x04)
				g_slotTmpBuf[groupThreadId + 1] += g_slotTmpBuf[groupThreadId&0x3C];

			if ((groupThreadId & 0x08) == 0x08)
				g_slotTmpBuf[groupThreadId + 1] += g_slotTmpBuf[groupThreadId&0x38];

			if ((groupThreadId & 0x10) == 0x10)
				g_slotTmpBuf[groupThreadId + 1] += g_slotTmpBuf[groupThreadId&0x30];

			if ((groupThreadId & 0x20) == 0x20)
				g_slotTmpBuf[groupThreadId + 1] += g_slotTmpBuf[0x20];

			g_slotTmpBuf[groupThreadId] += startOfs;
			if (groupThreadId == 0)
				g_slotTmpBuf[64] += startOfs;

			g_nodeList[groupThreadId] = 0;
			g_nodeList[groupThreadId + 64] = 0;
			
			uint2 rootNodeInfo = ReplaceStartEndIndex(blockInfo.zw, 0, 64);

			// level 0.
			uint offset = 0;
			if (groupThreadId == 0)
				SetTreeNode(offset, rootNodeInfo, blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);
			uint prevOffset = offset;
			offset += 2;

			// level 1.
			if (groupThreadId < 2)
				SetTreeNode(offset + groupThreadId * 2, g_nodeList[prevOffset + groupThreadId], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
			prevOffset = offset;
			offset += 4;

			// level 2.
			if (groupThreadId < 4)
				SetTreeNode(offset + groupThreadId * 2, g_nodeList[prevOffset + groupThreadId], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
			prevOffset = offset;
			offset += 8;

			// level 3.
			if (groupThreadId < 8)
				SetTreeNode(offset + groupThreadId * 2, g_nodeList[prevOffset + groupThreadId], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
			prevOffset = offset;
			offset += 16;

			// level 4.
			if (groupThreadId < 16)
				SetTreeNode(offset + groupThreadId * 2, g_nodeList[prevOffset + groupThreadId], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
			prevOffset = offset;
			offset += 32;

			// level 5.
			if (groupThreadId < 32)
				SetTreeNode(offset + groupThreadId * 2, g_nodeList[prevOffset + groupThreadId], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
			prevOffset = offset;

			uint2 leafNodeInfo = g_nodeList[prevOffset + groupThreadId];
			uint leafStartOfs = GetClampStartOffset(leafNodeInfo);
			uint leafEndOfs = GetClampEndOffset(leafNodeInfo);
			uint leafDstOffset = GetDstOffset(leafNodeInfo);

			if ( (blockSizeInfo & 0xffff) > 0) // the block is not empty, we need allocate memory for it.
			{
				uint subBlockIndex;
				pSrt->m_rwArgsBuffer.InterlockedAdd(0, 1, subBlockIndex);

				pSrt->m_rwBlockInfoList[subBlockIndex].x = blockInfo.x;
				pSrt->m_rwBlockInfoList[subBlockIndex].y = blockSizeInfo;
				pSrt->m_rwBlockInfoList[subBlockIndex].z = leafNodeInfo.x;
				pSrt->m_rwBlockInfoList[subBlockIndex].w = leafNodeInfo.y;
			}
		}
	}
}

[numthreads(64, 1, 1)]
void CS_GenerateSubTreeL2(uint groupId : SV_GroupID, uint groupThreadId : SV_GroupThreadId, GenerateSubTreeSrt* pSrt : S_SRT_DATA)
{
	uint4 blockInfo = pSrt->m_blockInfoList[groupId];

	uint blockStartOffset = blockInfo.x & 0x1ffff;
	uint blockShiftBits = pSrt->m_blockShiftBits;
	uint blockMask = pSrt->m_blockMask;
	uint blockIndex = blockInfo.x >> 17;

	// only do create when number of nodes is bigger than 0.
	uint numNodes = blockInfo.y & 0xffff;
	uint nodeStart = blockInfo.y >> 16;

	{
		uint targetBufferOffset = blockStartOffset + nodeStart;

		g_slotCount[groupThreadId] = 0;

		if (groupThreadId < numNodes)
		{
			uint nodeData = pSrt->m_rwSortedSubTree[targetBufferOffset + groupThreadId].x;
			uint bucketIdx = (nodeData >> blockShiftBits) & blockMask;
			uint currentSlotIdx;
			InterlockedAdd(g_slotCount[bucketIdx], 1u, currentSlotIdx);
		}

		g_slotOffset[groupThreadId] = groupThreadId == 0 ? 0 : g_slotCount[groupThreadId-1];

		if ((groupThreadId & 0x01) == 0x01)
			g_slotOffset[groupThreadId] += g_slotOffset[groupThreadId-1];

		if ((groupThreadId & 0x02) == 0x02)
			g_slotOffset[groupThreadId] += g_slotOffset[(groupThreadId&0x3E)-1];

		if ((groupThreadId & 0x04) == 0x04)
			g_slotOffset[groupThreadId] += g_slotOffset[(groupThreadId&0x3C)-1];

		g_slotTmpBuf[groupThreadId] = g_slotOffset[groupThreadId];

		if (groupThreadId < numNodes)
		{
			uint2 nodeData = pSrt->m_rwSortedSubTree[targetBufferOffset + groupThreadId];
			uint bucketIdx = (nodeData.x >> blockShiftBits) & blockMask;
			uint currentSlotIdx;
			InterlockedAdd(g_slotTmpBuf[bucketIdx], 1u, currentSlotIdx);
			g_nodeList[currentSlotIdx] = nodeData;
		}

		if (groupThreadId < numNodes)
			pSrt->m_rwSortedSubTree[targetBufferOffset + groupThreadId] = g_nodeList[groupThreadId];

		if (blockInfo.w >= 0x80000000)
		{
			uint startOfs = GetClampStartOffset(blockInfo.zw);
			uint endOfs = GetClampEndOffset(blockInfo.zw);

			g_slotCount[groupThreadId] = 0;

			uint localIdx = groupThreadId;
			if (localIdx < numNodes && localIdx >= (startOfs - nodeStart) && localIdx <= (endOfs - nodeStart))
			{
				uint nodeData = g_nodeList[localIdx].x;
				uint bucketIdx = (nodeData >> blockShiftBits) & blockMask;
				uint currentSlotIdx;
				InterlockedAdd(g_slotCount[bucketIdx], 1u, currentSlotIdx);
			}

			g_slotTmpBuf[0] = 0;
			g_slotTmpBuf[groupThreadId+1] = g_slotCount[groupThreadId];

			// create buffer offset.
			if ((groupThreadId & 0x01) == 0x01)
				g_slotTmpBuf[groupThreadId + 1] += g_slotTmpBuf[groupThreadId];

			if ((groupThreadId & 0x02) == 0x02)
				g_slotTmpBuf[groupThreadId + 1] += g_slotTmpBuf[groupThreadId&0x3E];

			if ((groupThreadId & 0x04) == 0x04)
				g_slotTmpBuf[groupThreadId + 1] += g_slotTmpBuf[groupThreadId&0x3C];

			g_slotTmpBuf[groupThreadId] += startOfs;
			g_nodeList[groupThreadId] = 0;
			
			uint2 rootNodeInfo = ReplaceStartEndIndex(blockInfo.zw, 0, 8);

			// level 0.
			uint offset = 0;
			if (groupThreadId == 0)
				SetTreeNode(offset, rootNodeInfo, blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);
			uint prevOffset = offset;
			offset += 2;

			// level 1.
			if (groupThreadId < 2)
				SetTreeNode(offset + groupThreadId * 2, g_nodeList[prevOffset + groupThreadId], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
			prevOffset = offset;
			offset += 4;

			// level 2.
			if (groupThreadId < 4)
				SetTreeNode(offset + groupThreadId * 2, g_nodeList[prevOffset + groupThreadId], blockStartOffset, blockIndex, pSrt->m_rwTreeNode, g_slotTmpBuf);	
			prevOffset = offset;

			if (groupThreadId < 8)
			{
				uint2 leafNodeInfo = g_nodeList[prevOffset + groupThreadId];
				uint leafStartOfs = GetClampStartOffset(leafNodeInfo);
				uint leafEndOfs = GetClampEndOffset(leafNodeInfo);
				uint leafDstOffset = GetDstOffset(leafNodeInfo);
				if (leafNodeInfo.y >= 0x80000000)
				{
					pSrt->m_rwTreeNode[leafDstOffset + blockStartOffset].x = ((leafEndOfs - leafStartOfs) << 17) | leafStartOfs;
					pSrt->m_rwTreeNode[leafDstOffset + blockStartOffset].y = (0x1f << 27) | blockInfo.x;
				}
			}
		}
	}
}

struct MergeKdTreeNodesInfoSrt
{
	uint						m_numBlocks;
	float						m_maxBlurRadius;
	float						m_volCellSize;
	float						m_pad;

	StructuredBuffer<float3>	m_centerPositionList;
	StructuredBuffer<uint>		m_nodesCountList;
	StructuredBuffer<uint2>		m_sortedSubTree;

	RWStructuredBuffer<uint2>	m_rwTreeNode;
};

[numthreads(64, 1, 1)]
void CS_MergeKdTreeNodesInfo(int dispatchThreadId : SV_DispatchThreadId, MergeKdTreeNodesInfoSrt* pSrt : S_SRT_DATA)
{
	uint totalNumNodes = pSrt->m_nodesCountList[(pSrt->m_numBlocks - 1) * 65 + 64];
	if (dispatchThreadId < totalNumNodes)
	{
		uint2 orgNodeInfo = pSrt->m_rwTreeNode[dispatchThreadId].xy;
		uint2 probeNodeInfo = pSrt->m_sortedSubTree[(orgNodeInfo.y & 0x1ffff) + (orgNodeInfo.x & 0x1ffff)];
		uint mortonCode = probeNodeInfo.x;
		uint srcProbeIdx = probeNodeInfo.y;
		// replace sorted node index with srcProbeIdx;
		orgNodeInfo.x = (orgNodeInfo.x & 0xfffe0000) | srcProbeIdx;

		uint splitAxisFull = orgNodeInfo.y >> 27;

		float splitPos = 0;
		uint splitAxis = 3;
		if (splitAxisFull != 0x1f)
		{
			splitAxis = splitAxisFull % 3;
			int shiftBits = 20 - (orgNodeInfo.y >> 27);
			mortonCode = (mortonCode >> shiftBits) << (shiftBits + splitAxis);

			uint selectBits = ((mortonCode >> 14) & 0x40) | 
							  ((mortonCode >> 12) & 0x20) |
							  ((mortonCode >> 10) & 0x10) |
							  ((mortonCode >> 8) & 0x08) |
							  ((mortonCode >> 6) & 0x04) |
							  ((mortonCode >> 4) & 0x02) |
							  ((mortonCode >> 2) & 0x01);

			splitPos = ((selectBits / 128.0f) * 2.0f - 1.0f) * (pSrt->m_volCellSize * 8.0f + pSrt->m_maxBlurRadius) + pSrt->m_centerPositionList[(orgNodeInfo.y >> 17) & 0x3ff][splitAxis];
		}
		orgNodeInfo.y = (asuint(splitPos) & 0xFFFFFFFC) | splitAxis;
		pSrt->m_rwTreeNode[dispatchThreadId].xy = orgNodeInfo;
	}
}

struct BlendTreeLightProbeSrt
{
	uint						m_pad;
	float						m_maxBlurRadius;
	float						m_volCellSize;
	float						m_ambientScale;
	float3						m_centerPosWS;
	uint						m_sizeX;
	uint3						m_blockSize;
	uint						m_sizeY;
	float3						m_lightPosWS;
	uint						m_pad1;
	float3						m_lightColor;
	uint						m_pad2;
	CompressedProbe				m_defalutProbe;

	StructuredBuffer<float3>	m_centerPositionList;
	StructuredBuffer<uint2>		m_sortedSubTree;
	StructuredBuffer<uint2>		m_treeNode;
	StructuredBuffer<float3>	m_probePositionList;
	StructuredBuffer<uint>		m_nodesCountList;
	StructuredBuffer<CompressedProbe>	m_probeList;

	RWTexture3D<float4>			m_rwbCellVolTexture1;
	RWTexture3D<float4>			m_rwbCellVolTexture2;
	RWTexture3D<float4>			m_rwbCellVolTexture3;
	RWTexture3D<float4>			m_rwbCellVolTexture4;
	RWTexture3D<float4>			m_rwbCellVolTexture5;
	RWTexture3D<float4>			m_rwbCellVolTexture6;
	RWTexture3D<float4>			m_rwbCellVolTexture7;
	RWTexture3D<float>			m_rwbCellVolScaleTex;
	RWTexture3D<uint>			m_rwbCellVolCountTex;
	RWTexture3D<float3>			m_rwbRuntimeLightDirWSTex;
	RWTexture3D<float3>			m_rwbRuntimeLightColorTex;
};

#define kMaxNumResults		10
#define kMaxStackDepth		16
#define kNumStackUint		((kMaxStackDepth + 1) / 2)

uint PopStack(inout int idx, uint stackBuffer[kNumStackUint])
{
	idx --;
	uint shiftBits = ((idx & 0x01) << 4);
	return (stackBuffer[idx / 2] >> shiftBits) & 0xffff;
}

void PushStack(inout int idx, uint value, inout uint stackBuffer[kNumStackUint]) 
{
	uint shiftBits = ((idx & 0x01) << 4);
	stackBuffer[idx / 2] = (shiftBits > 0 ? (stackBuffer[idx / 2] & 0xffff) : 0) | (value << shiftBits);
	idx ++;
}

void InternalNearestNeighbors(inout uint probeMaxCount, inout uint probeCount, inout uint resultList[kMaxNumResults], uint blockOffset, float3 positionWS, BlendTreeLightProbeSrt* pSrt)
{
	int stackTop = 0;
	uint stackBuffer[kNumStackUint];

	float maxDistSqr = pSrt->m_maxBlurRadius * pSrt->m_maxBlurRadius;

	PushStack(stackTop, 0, stackBuffer);

	while (stackTop > 0)
	{
		uint nodeIdx = PopStack(stackTop, stackBuffer);
		uint2 nodeInfo = pSrt->m_treeNode[blockOffset + nodeIdx].xy;
		uint probeDataIndex = nodeInfo.x & 0x1ffff;

		uint splitAxis = nodeInfo.y & 0x03;
		uint rightChild = (nodeInfo.x >> 17) & 0x3fff;
		uint numNodes = 1;
		if (splitAxis != 3)
		{
			float splitPos = asfloat(nodeInfo.y & 0xFFFFFFFC);
			bool hasRightNode = rightChild < 0x3fff;
			bool hasLeftNode = (nodeInfo.x >> 31) != 0;
			float planeDist = positionWS[splitAxis] - splitPos;

			if (hasRightNode && planeDist > -pSrt->m_maxBlurRadius)
			{
				if (stackTop < kMaxStackDepth)
					PushStack(stackTop, rightChild, stackBuffer);
			}

			if (planeDist < -pSrt->m_maxBlurRadius)
			{
				numNodes = 0;
			}

			if (hasLeftNode && planeDist < pSrt->m_maxBlurRadius)
			{
				if (stackTop < kMaxStackDepth)
					PushStack(stackTop, nodeIdx + 1, stackBuffer);
			}
		}
		else
		{
			numNodes += rightChild;
		}
		
		// split node is the first node on right.
		for (int i = 0; i < numNodes; i++)
		{
			float3 probePosition = pSrt->m_probePositionList[probeDataIndex + i];
			float3 probeDiffVec = probePosition - positionWS;
			float distSqr = dot(probeDiffVec, probeDiffVec);

			if (distSqr < maxDistSqr)
			{
				float weight = exp2(-distSqr / maxDistSqr * 12.0f);
				uint iWeight = uint(weight * 4095.0f) << 20;
				if (iWeight > 0)
				{
					uint nodeInfoT = iWeight | ((probeDataIndex + i) << 4);

					probeMaxCount ++;

					if (probeCount < kMaxNumResults)
					{
						uint compressedNodeInfo = nodeInfoT | probeCount;
						resultList[probeCount] = compressedNodeInfo;
						probeCount ++;
					}
					else
					{
						uint minSlot = min(min3(min3(resultList[0], resultList[1], resultList[2]), 
												min3(resultList[3], resultList[4], resultList[5]),
												min3(resultList[6], resultList[7], resultList[8])), 
											resultList[9]);

						if (iWeight > minSlot)
						{
							uint chooseSlotIdx = minSlot & 0x0f;
							uint compressedNodeInfo = nodeInfoT | chooseSlotIdx;
							resultList[chooseSlotIdx] = compressedNodeInfo;
						}
					}
				}
			}
		}
	}
}

[numthreads(4, 4, 4)]
void CS_BlendTreeLightProbe(int3 dispatchThreadId : SV_DispatchThreadId, int3 groupId : SV_GroupID, int groupIndex : SV_GroupIndex, BlendTreeLightProbeSrt* pSrt : S_SRT_DATA)
{
	uint probeCount = 0;
	uint probeMaxCount = 0;
	uint resultList[kMaxNumResults];

	float3 positionWS = (dispatchThreadId + 0.5f) * pSrt->m_volCellSize + pSrt->m_centerPosWS;

	uint3 vBlockIndex = groupId / 4;

	uint blockIndex = ReadFirstLane((vBlockIndex.z * pSrt->m_blockSize.y + vBlockIndex.y) * pSrt->m_blockSize.x + vBlockIndex.x);

	uint blockOffset = blockIndex == 0 ? 0 : pSrt->m_nodesCountList[(blockIndex - 1) * 65 + 64];
	blockOffset = ReadFirstLane(blockOffset);

	uint numNodes = pSrt->m_nodesCountList[blockIndex * 65 + 63];
	numNodes = ReadFirstLane(numNodes);

	if (numNodes > 0)
	{
		InternalNearestNeighbors(probeMaxCount, probeCount, resultList, blockOffset, positionWS, pSrt);
	}

	const float kCoeffScale = 1.0f / 127.0f;

	float sumWeights = 1.0f;

	CompressedProbe firstProbe = pSrt->m_defalutProbe;
	if (probeCount > 0)
	{
		uint nodeInfo = resultList[0];
		sumWeights = (nodeInfo >> 20) / 4095.0f;
		uint probeIdx = (nodeInfo >> 4) & 0xffff;
			
		firstProbe = pSrt->m_probeList[probeIdx];
	}

	float probeScale = firstProbe.m_colorScale * sumWeights * kCoeffScale;

	int3 isR0 = (int3)BitFieldExtract(firstProbe.m_shCompressed0, 0u, 8u);
	int3 isG0 = (int3)BitFieldExtract(firstProbe.m_shCompressed0, 8u, 8u);
	int3 isB0 = (int3)BitFieldExtract(firstProbe.m_shCompressed0, 16u, 8u);
	int3 isA0 = (int3)BitFieldExtract(firstProbe.m_shCompressed0, 24u, 8u);
	int4 isR1 = (int4)BitFieldExtract(firstProbe.m_shCompressed1, 0u, 8u);
	int4 isG1 = (int4)BitFieldExtract(firstProbe.m_shCompressed1, 8u, 8u);
	int4 isB1 = (int4)BitFieldExtract(firstProbe.m_shCompressed1, 16u, 8u);
	int4 isA1 = (int4)BitFieldExtract(firstProbe.m_shCompressed1, 24u, 8u);
	float shadow = (asuint(isA1.w) & 0x000000ff) / 255.0f;

	float3 s0 = isR0 * probeScale;
	float3 s1 = isG0 * probeScale;
	float3 s2 = isB0 * probeScale;
	float3 s3 = isA0 * probeScale;
	float4 s4 = isR1 * probeScale;
	float4 s5 = isG1 * probeScale;
	float4 s6 = isB1 * probeScale;
	float4 s7;
	s7.xyz = isA1.xyz * probeScale;
	s7.w = shadow * sumWeights;
	
	if (probeCount > 1)
	{
		for (int i = 1; i < probeCount; i++)
		{
			uint nodeInfo = resultList[i];

			float weight = (nodeInfo >> 20) / 4095.0f;
			uint probeIdx = (nodeInfo >> 4) & 0xffff;
			
			CompressedProbe probe = pSrt->m_probeList[probeIdx];
			probeScale = probe.m_colorScale * weight * kCoeffScale;

			isR0 = (int3)BitFieldExtract(probe.m_shCompressed0, 0u, 8u);
			isG0 = (int3)BitFieldExtract(probe.m_shCompressed0, 8u, 8u);
			isB0 = (int3)BitFieldExtract(probe.m_shCompressed0, 16u, 8u);
			isA0 = (int3)BitFieldExtract(probe.m_shCompressed0, 24u, 8u);
			isR1 = (int4)BitFieldExtract(probe.m_shCompressed1, 0u, 8u);
			isG1 = (int4)BitFieldExtract(probe.m_shCompressed1, 8u, 8u);
			isB1 = (int4)BitFieldExtract(probe.m_shCompressed1, 16u, 8u);
			isA1 = (int4)BitFieldExtract(probe.m_shCompressed1, 24u, 8u);
			shadow = (asuint(isA1.w) & 0x000000ff) / 255.0f;

			s0 += isR0 * probeScale;
			s1 += isG0 * probeScale;
			s2 += isB0 * probeScale;
			s3 += isA0 * probeScale;
			s4 += isR1 * probeScale;
			s5 += isG1 * probeScale;
			s6 += isB1 * probeScale;
			s7.xyz += isA1.xyz * probeScale;
			s7.w += shadow * weight;
			sumWeights += weight;
		}
	}

	float3 maxS0 = max3(abs(s0), abs(s1), abs(s2));
	float4 maxS1 = max3(abs(s4), abs(s5), abs(s6));
	float3 maxS = max3(maxS0, maxS1.xyz, abs(s3));
	maxS = max(maxS, abs(s7.xyz));
	float maxSS = max(max3(maxS.x, maxS.y, maxS.z), maxS1.w);

	float invMaxSS = 1.0f / maxSS;
	float invSumWeight = 1.0f / max(sumWeights, 0.00001f);

	s0 *= invMaxSS;
	s1 *= invMaxSS;
	s2 *= invMaxSS;
	s3 *= invMaxSS;
	s4 *= invMaxSS;
	s5 *= invMaxSS;
	s6 *= invMaxSS;
	s7.xyz *= invMaxSS;
	s7.w *= invSumWeight;

	pSrt->m_rwbCellVolTexture1[dispatchThreadId] = float4(s0.x, s3.x, s2.y, s1.z);
	pSrt->m_rwbCellVolTexture2[dispatchThreadId] = float4(s1.x, s0.y, s3.y, s2.z);
	pSrt->m_rwbCellVolTexture3[dispatchThreadId] = float4(s2.x, s1.y, s0.z, s3.z);
	pSrt->m_rwbCellVolTexture4[dispatchThreadId] = float4(s4.x, s7.x, s6.y, s5.z);
	pSrt->m_rwbCellVolTexture5[dispatchThreadId] = float4(s5.x, s4.y, s7.y, s6.z);
	pSrt->m_rwbCellVolTexture6[dispatchThreadId] = float4(s6.x, s5.y, s4.z, s7.z);
	pSrt->m_rwbCellVolTexture7[dispatchThreadId] = float4(s4.w, s5.w, s6.w, s7.w);
	pSrt->m_rwbCellVolScaleTex[dispatchThreadId] = maxSS * invSumWeight * pSrt->m_ambientScale;
	pSrt->m_rwbCellVolCountTex[dispatchThreadId] = probeMaxCount;

	float3 lightDirWeighted = positionWS.xyz - pSrt->m_lightPosWS.xyz;
	pSrt->m_rwbRuntimeLightDirWSTex[dispatchThreadId] = normalize(lightDirWeighted) * 0.5f + 0.5f;
	pSrt->m_rwbRuntimeLightColorTex[dispatchThreadId] = 1.0f / (1.0f + 25.0f * dot(lightDirWeighted, lightDirWeighted)) * pSrt->m_lightColor.xyz;
}

uint PopStack32(inout int idx, uint stackBuffer[32])
{
	idx --;
	uint shiftBits = ((idx & 0x01) << 4);
	return (stackBuffer[idx / 2] >> shiftBits) & 0xffff;
}

void PushStack32(inout int idx, uint value, inout uint stackBuffer[32]) 
{
	uint shiftBits = ((idx & 0x01) << 4);
	stackBuffer[idx / 2] = (shiftBits > 0 ? (stackBuffer[idx / 2] & 0xffff) : 0) | (value << shiftBits);
	idx ++;
}

void InternalNearestNeighborsOld(inout uint probeCount, inout uint probeInfoList[10],
							  int rootIdx, float3 centerPosLS, float3 edgeLenLS, 
							  float3 positionLS, float maxDistance, 
							  StructuredBuffer<uint2> probeKdTree, 
							  StructuredBuffer<float4> probePositionList)
{
	int stackTop = 0;
	uint stackBuffer[32];

	float maxDistSqr = maxDistance * maxDistance;

	if (all(abs(positionLS - centerPosLS) < edgeLenLS))
	{
		PushStack32(stackTop, rootIdx, stackBuffer);

		while (stackTop > 0)
		{
			uint nodeIdx = PopStack32(stackTop, stackBuffer);
			uint2 node = probeKdTree[nodeIdx];

			float splitPos = asfloat(node.x);
			uint  splitAxis = node.y & 0x03;
			uint  hasLeftChild = node.y & 0x04;
			uint  rightChild = node.y >> 3;

			float planeDist = 0;
			if (splitAxis != 3)
			{
				planeDist = positionLS[splitAxis] - splitPos;

				bool hasChild0 = hasLeftChild != 0;
				bool hasChild1 = rightChild < (1 << 29) - 1;

				uint child0 = nodeIdx + 1;
				uint child1 = rightChild;

				if (planeDist > 0.0f)
				{
					child0 ^= child1; child1 ^= child0; child0 ^= child1;
					hasChild0 ^= hasChild1; hasChild1 ^= hasChild0; hasChild0 ^= hasChild1;
				}

				if (hasChild0)
				{
					PushStack32(stackTop, child0, stackBuffer);
				}

				if (abs(planeDist) < maxDistance && hasChild1)
					PushStack32(stackTop, child1, stackBuffer);
			}
		
			if (abs(planeDist) < maxDistance)
			{
				float3 probeDiffVec = probePositionList[nodeIdx].xyz - positionLS;
				float distSqr = dot(probeDiffVec, probeDiffVec);

				if (distSqr < maxDistSqr)
				{
					float weight = exp2(-distSqr / maxDistSqr * 12.0f);
					uint iWeight = uint(weight * 4095.0f) << 20;
					uint nodeInfo = iWeight | (asuint(probePositionList[nodeIdx].w) << 4);
					if (probeCount < 10)
					{
						probeInfoList[probeCount] = nodeInfo | probeCount;
						probeCount ++;
					}
					else
					{
						uint minSlot = min(min3(min3(probeInfoList[0], probeInfoList[1], probeInfoList[2]), 
												min3(probeInfoList[3], probeInfoList[4], probeInfoList[5]),
												min3(probeInfoList[6], probeInfoList[7], probeInfoList[8])), probeInfoList[9]);

						if (iWeight > (minSlot & 0xFFF00000))
							probeInfoList[minSlot & 0x0f] = nodeInfo | (minSlot & 0x0f);
					}
				}
			}
		}
	}
}

struct BlendTreeLightProbeOldSrt
{
	uint						m_numNodeTrees;
	float						m_maxBlurRadius;
	float						m_volCellSize;
	float						m_ambientScale;
	float3						m_centerPosWS;
	float						m_pad1;
	uint						m_rootIdx[8];
	float4						m_centerPosLS[8];
	float4						m_edgeLenLS[8];
	float4						m_wsToLSMat0[8];
	float4						m_wsToLSMat1[8];
	float4						m_wsToLSMat2[8];
	CompressedProbe				m_defalutProbe;

	StructuredBuffer<uint2>		m_probeKdTree;
	StructuredBuffer<float4>	m_probePositionList;
	StructuredBuffer<CompressedProbe>	m_probeList;

	RWTexture3D<float4>			m_rwbCellVolTexture1;
	RWTexture3D<float4>			m_rwbCellVolTexture2;
	RWTexture3D<float4>			m_rwbCellVolTexture3;
	RWTexture3D<float4>			m_rwbCellVolTexture4;
	RWTexture3D<float4>			m_rwbCellVolTexture5;
	RWTexture3D<float4>			m_rwbCellVolTexture6;
	RWTexture3D<float4>			m_rwbCellVolTexture7;
	RWTexture3D<float>			m_rwbCellVolScaleTex;
};

[numthreads(4, 4, 4)]
void CS_BlendTreeLightProbeOld(int3 dispatchThreadId : SV_DispatchThreadId, BlendTreeLightProbeOldSrt* pSrt : S_SRT_DATA)
{
	uint probeCount = 0;
	uint nodeInfoList[10];

	float3 positionWS = (dispatchThreadId + 0.5f) * pSrt->m_volCellSize + pSrt->m_centerPosWS;

	for (int i = 0; i < pSrt->m_numNodeTrees; i++)
	{
		float3 positionLS;
		positionLS.x = dot(pSrt->m_wsToLSMat0[i], float4(positionWS, 1.0f));
		positionLS.y = dot(pSrt->m_wsToLSMat1[i], float4(positionWS, 1.0f));
		positionLS.z = dot(pSrt->m_wsToLSMat2[i], float4(positionWS, 1.0f));
		InternalNearestNeighborsOld(probeCount, nodeInfoList, pSrt->m_rootIdx[i], 
								 pSrt->m_centerPosLS[i].xyz, pSrt->m_edgeLenLS[i].xyz,
								 positionLS, pSrt->m_maxBlurRadius, 
								 pSrt->m_probeKdTree, pSrt->m_probePositionList);
	}

	const float kCoeffScale = 1.0f / 127.0f;

	float sumWeights = 1.0f;

	CompressedProbe firstProbe = pSrt->m_defalutProbe;
	if (probeCount > 0)
	{
		sumWeights = (nodeInfoList[0] >> 20) / 4095.0f;
		uint probeIdx = (nodeInfoList[0] >> 4) & 0xffff;
			
		firstProbe = pSrt->m_probeList[probeIdx];
	}

	float probeScale = firstProbe.m_colorScale * sumWeights * kCoeffScale;

	int3 isR0 = (int3)BitFieldExtract(firstProbe.m_shCompressed0, 0u, 8u);
	int3 isG0 = (int3)BitFieldExtract(firstProbe.m_shCompressed0, 8u, 8u);
	int3 isB0 = (int3)BitFieldExtract(firstProbe.m_shCompressed0, 16u, 8u);
	int3 isA0 = (int3)BitFieldExtract(firstProbe.m_shCompressed0, 24u, 8u);
	int4 isR1 = (int4)BitFieldExtract(firstProbe.m_shCompressed1, 0u, 8u);
	int4 isG1 = (int4)BitFieldExtract(firstProbe.m_shCompressed1, 8u, 8u);
	int4 isB1 = (int4)BitFieldExtract(firstProbe.m_shCompressed1, 16u, 8u);
	int4 isA1 = (int4)BitFieldExtract(firstProbe.m_shCompressed1, 24u, 8u);
	float shadow = (asuint(isA1.w) & 0x000000ff) / 255.0f;

	float3 s0 = isR0 * probeScale;
	float3 s1 = isG0 * probeScale;
	float3 s2 = isB0 * probeScale;
	float3 s3 = isA0 * probeScale;
	float4 s4 = isR1 * probeScale;
	float4 s5 = isG1 * probeScale;
	float4 s6 = isB1 * probeScale;
	float4 s7;
	s7.xyz = isA1.xyz * probeScale;
	s7.w = shadow * sumWeights;
	
	if (probeCount > 1)
	{
		for (int i = 1; i < probeCount; i++)
		{
			float weight = (nodeInfoList[i] >> 20) / 4095.0f;
			uint probeIdx = (nodeInfoList[i] >> 4) & 0xffff;
			
			CompressedProbe probe = pSrt->m_probeList[probeIdx];
			probeScale = probe.m_colorScale * weight * kCoeffScale;

			isR0 = (int3)BitFieldExtract(probe.m_shCompressed0, 0u, 8u);
			isG0 = (int3)BitFieldExtract(probe.m_shCompressed0, 8u, 8u);
			isB0 = (int3)BitFieldExtract(probe.m_shCompressed0, 16u, 8u);
			isA0 = (int3)BitFieldExtract(probe.m_shCompressed0, 24u, 8u);
			isR1 = (int4)BitFieldExtract(probe.m_shCompressed1, 0u, 8u);
			isG1 = (int4)BitFieldExtract(probe.m_shCompressed1, 8u, 8u);
			isB1 = (int4)BitFieldExtract(probe.m_shCompressed1, 16u, 8u);
			isA1 = (int4)BitFieldExtract(probe.m_shCompressed1, 24u, 8u);
			shadow = (asuint(isA1.w) & 0x000000ff) / 255.0f;

			s0 += isR0 * probeScale;
			s1 += isG0 * probeScale;
			s2 += isB0 * probeScale;
			s3 += isA0 * probeScale;
			s4 += isR1 * probeScale;
			s5 += isG1 * probeScale;
			s6 += isB1 * probeScale;
			s7.xyz += isA1.xyz * probeScale;
			s7.w += shadow * weight;
			sumWeights += weight;
		}
	}

	float3 maxS0 = max3(abs(s0), abs(s1), abs(s2));
	float4 maxS1 = max3(abs(s4), abs(s5), abs(s6));
	float3 maxS = max3(maxS0, maxS1.xyz, abs(s3));
	maxS = max(maxS, abs(s7.xyz));
	float maxSS = max(max3(maxS.x, maxS.y, maxS.z), maxS1.w);

	float invMaxSS = 1.0f / maxSS;
	float invSumWeight = 1.0f / max(sumWeights, 0.00001f);

	s0 *= invMaxSS;
	s1 *= invMaxSS;
	s2 *= invMaxSS;
	s3 *= invMaxSS;
	s4 *= invMaxSS;
	s5 *= invMaxSS;
	s6 *= invMaxSS;
	s7.xyz *= invMaxSS;
	s7.w *= invSumWeight;

	pSrt->m_rwbCellVolTexture1[dispatchThreadId] = float4(s0.x, s3.x, s2.y, s1.z);
	pSrt->m_rwbCellVolTexture2[dispatchThreadId] = float4(s1.x, s0.y, s3.y, s2.z);
	pSrt->m_rwbCellVolTexture3[dispatchThreadId] = float4(s2.x, s1.y, s0.z, s3.z);
	pSrt->m_rwbCellVolTexture4[dispatchThreadId] = float4(s4.x, s7.x, s6.y, s5.z);
	pSrt->m_rwbCellVolTexture5[dispatchThreadId] = float4(s5.x, s4.y, s7.y, s6.z);
	pSrt->m_rwbCellVolTexture6[dispatchThreadId] = float4(s6.x, s5.y, s4.z, s7.z);
	pSrt->m_rwbCellVolTexture7[dispatchThreadId] = float4(s4.w, s5.w, s6.w, s7.w);
	pSrt->m_rwbCellVolScaleTex[dispatchThreadId] = maxSS * invSumWeight * pSrt->m_ambientScale;
}

struct VolProbeBufferInfoSrt
{
	float4						m_depthParams;
	float4						m_viewToWorldMat[3];
	float4						m_screenScaleOffset;
	float4						m_centerPositionWS;
	float4						m_uvwScale;
	RWTexture2D<float3>			m_dstPrimaryColor;
	Texture2D<float>			m_srcDepth;
	Texture2D<uint4>			m_srcGbuffer0;
	SamplerState				m_samplerLinear;
	Texture3D<float4>			m_txCellVolTexture1;
	Texture3D<float4>			m_txCellVolTexture2;
	Texture3D<float4>			m_txCellVolTexture3;
	Texture3D<float4>			m_txCellVolTexture4;
	Texture3D<float4>			m_txCellVolTexture5;
	Texture3D<float4>			m_txCellVolTexture6;
	Texture3D<float4>			m_txCellVolTexture7;
	Texture3D<float>			m_txCellVolScaleTex;
	Texture3D<float3>			m_txRuntimeLightDirWSTex;
	Texture3D<float3>			m_txRuntimeLightColorTex;
};

float2 UnpackNormal(uint firstShort, uint extraBits)
{
	uint normalXHighBits = extraBits & 0xF00;
	uint normalYHighBits = (extraBits >> 4) & 0x700;
	float2 encodedNormal = float2((firstShort & 0xff) | normalXHighBits,
		(firstShort >> 8) | normalYHighBits);
	encodedNormal /= float2(4095, 2047);
	return encodedNormal;
}

#define CndMask(condition, trueVal, falseVal) (condition)? (trueVal) : (falseVal)

// Octahedron normal encoding borrowed from http://kriscg.blogspot.com/2014/04/octahedron-normal-vector-encoding.html
float2 OctWrap(float2 v)  
{
	return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0 : -1.0);  
}

float3 DecodeNormal(float2 encN)  
{
	encN = encN * 2.0 - 1.0;

	float3 n;
	n.z  = 1.0 - abs(encN.x) - abs(encN.y);
	//n.xy = n.z >= 0.0 ? encN.xy : OctWrap(encN.xy);
	n.xy = CndMask(n.z >= 0, encN.xy, OctWrap(encN.xy));
	n = normalize(n);
	return n;
}

float3 GetProbeColor(float3 positionVS, float3 dir, int2 dispatchThreadId, VolProbeBufferInfoSrt* pSrt)
{
	float u = 0, v = 0, w = 0;

	float3 positionWS;
	positionWS.x = dot(pSrt->m_viewToWorldMat[0], float4(positionVS, 1.0f));
	positionWS.y = dot(pSrt->m_viewToWorldMat[1], float4(positionVS, 1.0f));
	positionWS.z = dot(pSrt->m_viewToWorldMat[2], float4(positionVS, 1.0f));
	float3 uvw = (positionWS - pSrt->m_centerPositionWS) * pSrt->m_uvwScale;
	u = uvw.x;
	v = uvw.y;
	w = uvw.z;

	float3 probeColor = float3(0.5, 0, 0);
	if (u >= 0 && u <= 1.0f && v >= 0 && v <= 1.0f && w >= 0 && w <= 1.0f)
	{
		float4 a0 = pSrt->m_txCellVolTexture1.SampleLevel(pSrt->m_samplerLinear, float3(u, v, w), 0);
		float4 a1 = pSrt->m_txCellVolTexture2.SampleLevel(pSrt->m_samplerLinear, float3(u, v, w), 0);
		float4 a2 = pSrt->m_txCellVolTexture3.SampleLevel(pSrt->m_samplerLinear, float3(u, v, w), 0);
		float4 a3 = pSrt->m_txCellVolTexture4.SampleLevel(pSrt->m_samplerLinear, float3(u, v, w), 0);
		float4 a4 = pSrt->m_txCellVolTexture5.SampleLevel(pSrt->m_samplerLinear, float3(u, v, w), 0);
		float4 a5 = pSrt->m_txCellVolTexture6.SampleLevel(pSrt->m_samplerLinear, float3(u, v, w), 0);
		float4 a6 = pSrt->m_txCellVolTexture7.SampleLevel(pSrt->m_samplerLinear, float3(u, v, w), 0);
		float probeScale = pSrt->m_txCellVolScaleTex.SampleLevel(pSrt->m_samplerLinear, float3(u, v, w), 0);

		probeColor = CalculateProbeLight(dir, a0, a1, a2, a3, a4, a5, a6, probeScale);

		float3 lightDir = pSrt->m_txRuntimeLightDirWSTex.SampleLevel(pSrt->m_samplerLinear, float3(u, v, w), 0) * 2.0f - 1.0f;
		float3 lightColor = pSrt->m_txRuntimeLightColorTex.SampleLevel(pSrt->m_samplerLinear, float3(u, v, w), 0);

		probeColor += saturate(dot(-lightDir, dir)) * lightColor;
	}

	return probeColor;
}

[numthreads(8, 8, 1)]
void CS_DebugLightProbeVolCellInfo(int2 dispatchThreadId : SV_DispatchThreadID, VolProbeBufferInfoSrt* pSrt : S_SRT_DATA)
{
	float depthZ = pSrt->m_srcDepth[dispatchThreadId];
	float depthVS = 1.0f / (depthZ * pSrt->m_depthParams.x + pSrt->m_depthParams.y);

	float3 basePositionVS = float3((dispatchThreadId.xy * pSrt->m_screenScaleOffset.xy + pSrt->m_screenScaleOffset.zw), 1.0f);
	float3 positionVS = basePositionVS * depthVS;

	uint4 sample0 = pSrt->m_srcGbuffer0[dispatchThreadId];
	float2 encodedNormal = UnpackNormal(sample0.z, sample0.w);
	float3 dir = normalize(DecodeNormal(encodedNormal));

	float3 probeColor = 0;
#if 1
	probeColor = GetProbeColor(positionVS, dir, dispatchThreadId, pSrt);
#else
	dir = -normalize(positionVS);
	float3 dirWS;
	dirWS.x = dot(pSrt->m_viewToWorldMat[0].xyz, dir);
	dirWS.y = dot(pSrt->m_viewToWorldMat[1].xyz, dir);
	dirWS.z = dot(pSrt->m_viewToWorldMat[2].xyz, dir);

	for (int i = 0; i < 32; i++)
	{
		if (i == 0)
			probeColor = GetProbeColor(positionVS / 32.0f * (32 - i), dirWS, dispatchThreadId, pSrt) * (depthVS / 32);
		else
			probeColor = lerp(probeColor, GetProbeColor(positionVS / 32.0f * (32 - i), dirWS, dispatchThreadId, pSrt) * (depthVS / 32), 0.8f);
	}

	probeColor *= 0.1f;
#endif

	pSrt->m_dstPrimaryColor[dispatchThreadId] = depthZ == 1.0f ? float3(0, 0, 0) : probeColor;
}
