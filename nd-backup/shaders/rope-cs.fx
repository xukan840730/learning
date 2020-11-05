//
// Compute Shader
//

#define THREAD_COUNT 64
#define MAX_NODE_COUNT 300

#define FLT_EPSILON	0.00001

static const float g_unstretchRelaxInv = 0.985;

enum ConstraintFlags
{
	kIsWorking1 = 0x01,
	kIsWorking2 = 0x02,
	kIsWorking3 = 0x04,
	kIsWorking4 = 0x08,
	kIsOuterEdge = 0x10,
	kIsOuterEdge2 = 0x20,

	kAnyOuterEdge = kIsOuterEdge | kIsOuterEdge2,
};

struct RopeMultiLevel
{
	uint m_numNodes;
	uint m_firstIndex;
	uint m_numIters;
	uint m_numNewNodes;
	uint m_firstNewIndex;
};

struct RopeMultiNewNode
{
	uint m_index;
	uint m_indexA;
	uint m_indexB;
};

struct RopeBendingGroup
{
	uint m_index0;
	uint m_index1;
	uint m_index2;
	float m_k;
	bool m_delay2;
};

struct RopeConst
{
	uint m_startNode;
	uint m_numNodesAll;
	uint m_numNodes; // without extra nodes for bending at start/end
	uint m_nodesShift; // shift at the start due to the extra nodes for bending
	uint m_numIters;
	uint m_numLevels;
	uint m_waveSize;
	uint m_numBendingGroups;
	float m_bendingMinMR;
	uint m_numSelfCollision;
	float m_selfCollisionRadius;

};

struct RopeNodeDataIn
{
	float4 m_constrPlanes[6];
	float4 m_constrBiPlanes[6];
	float4 m_distConstr;
	float m_frictionConstr[5];
	uint m_numConstrPlanes;
	uint m_numConstrEdges;
	float m_radius;
	float m_ropeDist;
	float m_invMass;
};

struct RopeNodeDataInOut
{
	float4 m_pos;
	uint m_flags;
};

struct RopeBuffers
{
	StructuredBuffer<RopeMultiNewNode>		m_inLevelNewNodes;
	StructuredBuffer<uint>					m_inLevelIndices;
	StructuredBuffer<RopeMultiLevel>		m_inLevels;
	StructuredBuffer<RopeBendingGroup>		m_inBendingGroups;
	StructuredBuffer<RopeNodeDataIn>		m_in;
	RWStructuredBuffer<RopeNodeDataInOut>	m_inOut;
	StructuredBuffer<uint>					m_inSelfCollision;
	RWStructuredBuffer<float4>				m_outForSkinning;
};

struct SrtData
{
	RopeConst*		 m_pConsts;
	RopeBuffers*	 m_pBuffs;
};

thread_group_memory float3 posLDS[MAX_NODE_COUNT];
thread_group_memory uchar flagsLDS[MAX_NODE_COUNT];

float4 getOuterEdgePlane(const float4 plane0, const float4 plane1, const float4 biPlane0, const float4 biPlane1, const float4 pos, uchar& planeMask)
{
	float bih0 = dot(pos, biPlane0);
	float bih1 = dot(pos, biPlane1);
	float h0 = dot(plane0, pos);
	float h1 = dot(plane1, pos);
	if (bih0 > 0.0f && bih1 > 0.0f)
	{
		float3 e = h0*plane0.xyz + bih0*biPlane0.xyz;
		float3 edgePnt = pos.xyz - e;
		float3 norm = normalize(e);
		float d = dot(norm, edgePnt);
		planeMask = 3;
		return float4(norm, -d);
	}
	planeMask = h0 > h1 ? 1 : 2;
	return h0 > h1 ? plane0 : plane1;
}

void RelaxConstraints(uint iNode, uint iInNode, float frictionMult, RopeBuffers* pBuffs)
{
	const RopeNodeDataIn& nodeDataIn = pBuffs->m_in[iInNode];

	if (nodeDataIn.m_invMass > 0.0)
	{
		const float radius = nodeDataIn.m_radius;
		float4 pos = float4(posLDS[iNode].xyz, 1.0);
		uchar flags = flagsLDS[iNode];

		// Distance constraint
		{
			float3 v = (pos - pBuffs->m_in[iInNode].m_distConstr).xyz;
			float dist = length(v);
			v = dist == 0.0 ? float3(0.0, 0.0, 0.0) : v / dist;
			float err = max(0.0, dist - pBuffs->m_in[iInNode].m_distConstr.w);
			pos.xyz -= err * v;
		}

		// Friction Constraint
		{
			float3 v = pos.xyz - float3(pBuffs->m_in[iInNode].m_frictionConstr[0], pBuffs->m_in[iInNode].m_frictionConstr[1], pBuffs->m_in[iInNode].m_frictionConstr[2]);
			float dist0 = length(v);
			float dist = dist0 * pBuffs->m_in[iInNode].m_frictionConstr[3];
			dist += pBuffs->m_in[iInNode].m_frictionConstr[4];
			//if (dist >= dist0)
			//	pos.xyz = float3(pBuffs->m_in[iInNode].m_frictionConstr[0], pBuffs->m_in[iInNode].m_frictionConstr[1], pBuffs->m_in[iInNode].m_frictionConstr[2]);
			//else
			{
				dist = min(dist0, dist);
				pos.xyz -= (dist0 == 0.0f ? 0.0f : dist/dist0) * frictionMult * v;
			}
		}

		// Collision constraints

		uint numPushPlanes = 0;
		float4 pushPlanes[4];
		uchar flagMask[4];

		for (uint iEdge = 0; iEdge<nodeDataIn.m_numConstrEdges; iEdge++)
		{
			uchar planeMask;
			pushPlanes[numPushPlanes] = getOuterEdgePlane(nodeDataIn.m_constrPlanes[iEdge*2], nodeDataIn.m_constrPlanes[iEdge*2+1], nodeDataIn.m_constrBiPlanes[iEdge*2], nodeDataIn.m_constrBiPlanes[iEdge*2+1], pos, planeMask);
			flagMask[numPushPlanes] = (uchar)(planeMask << (iEdge*2));
			numPushPlanes++;
		}

		for (uint iPlane = 2*nodeDataIn.m_numConstrEdges; iPlane<nodeDataIn.m_numConstrPlanes; iPlane++)
		{
			pushPlanes[numPushPlanes] = nodeDataIn.m_constrPlanes[iPlane];
			flagMask[numPushPlanes] = 1 << (uchar)iPlane;
			numPushPlanes++;
		}

		for (uint iPushPlane = 0; iPushPlane<numPushPlanes; iPushPlane++)
		{
			float h = dot(pos, pushPlanes[iPushPlane]) - radius;
			flags |= h < 0.0 ? flagMask[iPushPlane] : 0;
			pos.xyz -= min(h, 0.0) * pushPlanes[iPushPlane].xyz;
		}

		posLDS[iNode] = pos.xyz;
		flagsLDS[iNode] = flags;
	}
}

[numthreads(THREAD_COUNT, 1, 1)]
void CS_RopeRelaxPos(uint3 dispatchThreadId : SV_DispatchThreadID, SrtData srt : S_SRT_DATA)
{
	uint threadId = dispatchThreadId.x;
	if (threadId >= srt.m_pConsts->m_numNodesAll)
		return;

	RopeConst* pCData = srt.m_pConsts;
	RopeBuffers* pBuffs = srt.m_pBuffs;

	// Load pos into LDS
	uint nodesPerThreadAll = (pCData->m_numNodesAll-1) / THREAD_COUNT + 1;
	for (uint ii = 0; ii < nodesPerThreadAll; ii++)
	{
		uint index = ii * THREAD_COUNT + threadId;
		if (index < pCData->m_numNodesAll)
		{
			posLDS[index] = pBuffs->m_inOut[pCData->m_startNode + index].m_pos.xyz;
			flagsLDS[index] = (char)pBuffs->m_inOut[pCData->m_startNode + index].m_flags;
		}
	}

	// First do this once to get some "working constraints" approximate before multigrid hides it all
	uint nodesPerThread = (pCData->m_numNodes-1) / THREAD_COUNT + 1;
	for (uint ii = 0; ii < nodesPerThread; ii++)
	{
		uint index = ii * THREAD_COUNT + threadId;
		if (index < pCData->m_numNodes)
		{
			index += pCData->m_nodesShift; 
			RelaxConstraints(index, pCData->m_startNode + index, 0.0f, pBuffs);
			// We need to save the positions now because those will serve as starting positions for multigrid
			pBuffs->m_inOut[pCData->m_startNode + index].m_pos.xyz = posLDS[index];
		}
	}

	// Multigrid unstretch + constraints
	for (uint iLevel = 0; iLevel < pCData->m_numLevels; iLevel++)
	{
		uint numNodes = pBuffs->m_inLevels[iLevel].m_numNodes;
		uint waveSize = min(THREAD_COUNT, numNodes);
		uint numIters = pBuffs->m_inLevels[iLevel].m_numIters;
		uint firstIndex = pBuffs->m_inLevels[iLevel].m_firstIndex;
		if (threadId < numNodes)
		{
			uint waveSpace = numNodes/waveSize;
			for (uint iIter = 0; iIter < numIters; iIter++)
			{
				uint iIndex = (iIter + threadId*waveSpace) % numNodes;
				uint iNode = pBuffs->m_inLevelIndices[firstIndex + iIndex];
				uint iInNode = pCData->m_startNode + iNode;

				// Unstretch
				if (iIndex < numNodes - 1)
				{
					uint iNode1 = pBuffs->m_inLevelIndices[firstIndex + iIndex + 1];
					uint iInNode1 = pCData->m_startNode + iNode1;

					float3 pos0 = posLDS[iNode];
					float3 pos1 = posLDS[iNode1];

					float w0 = pBuffs->m_in[iInNode].m_invMass;
					float w1 = pBuffs->m_in[iInNode1].m_invMass;

					if (w0 + w1 > 0.0f)
					{
						float edgeLen0 = pBuffs->m_in[iInNode1].m_ropeDist - pBuffs->m_in[iInNode].m_ropeDist;
						edgeLen0 *= g_unstretchRelaxInv;

						float3 edgeVec = pos1 - pos0;
						float edgeLen = length(edgeVec);
						edgeVec = edgeLen == 0.0 ? float3(0, -1, 0) : edgeVec/edgeLen;
						if (iNode1 - iNode > 1)
							edgeLen = max(edgeLen, edgeLen0); // only unstretch in multigrid, never de-compress

						float3 delta = (edgeLen - edgeLen0) * edgeVec;
						float3 deltaLeft = delta * (w0 / (w0 + w1));
						float3 deltaRight = delta - deltaLeft;

						posLDS[iNode] = pos0 + deltaLeft;
						GroupMemoryBarrierWithGroupSync();

						// This __invariant here makes the shader works as expected. Without it the multigrid becomes unstable. Makes no sense to me. Neither when looking at the differences in the assembly. 
						posLDS[iNode1] = __invariant(posLDS[iNode1] - deltaRight);
					}
				}

				// Constraints
				RelaxConstraints(iNode, iInNode, 0.9f, pBuffs);
			}
		}

		// Move all new points of the next grid level by proportional movement of the current level neighbors
		uint numNewNodes = pBuffs->m_inLevels[iLevel].m_numNewNodes;
		uint firstNewIndex = pBuffs->m_inLevels[iLevel].m_firstNewIndex;
		uint newNodesPerThread = (numNewNodes-1) / THREAD_COUNT + 1;
		for (uint ii = 0; ii < newNodesPerThread; ii++)
		{
			uint index = ii * THREAD_COUNT + threadId;
			if (index < numNewNodes)
			{
				uint newIndex = firstNewIndex + index;
				uint iNode = pBuffs->m_inLevelNewNodes[newIndex].m_index;
				uint iNodeA = pBuffs->m_inLevelNewNodes[newIndex].m_indexA;
				uint iNodeB = pBuffs->m_inLevelNewNodes[newIndex].m_indexB;

				uint iInNode = pCData->m_startNode + iNode;
				uint iInNodeA = pCData->m_startNode + iNodeA;
				uint iInNodeB = pCData->m_startNode + iNodeB;

				float ropeLen = pBuffs->m_in[iInNode].m_ropeDist;
				float ropeLenA = pBuffs->m_in[iInNodeA].m_ropeDist;
				float ropeLenB = pBuffs->m_in[iInNodeB].m_ropeDist;
				float fA = (ropeLen - ropeLenA) / (ropeLenB - ropeLenA);

				float3 posA = posLDS[iNodeA];
				float3 posA0 = pBuffs->m_inOut[iInNodeA].m_pos.xyz;
				float3 posB = posLDS[iNodeB];
				float3 posB0 = pBuffs->m_inOut[iInNodeB].m_pos.xyz;

				posLDS[iNode] += fA * (posA - posA0) + (1.0 - fA) * (posB - posB0);
			}
		}
	}

	// Last level solve with bending stiffness and self collision
	uint selfCollisionPeriod = pCData->m_numSelfCollision ? (uint)(1.0f * pCData->m_numNodes / (float)pCData->m_waveSize + 0.5f) : pCData->m_numIters;
	uint waveSpace = pCData->m_numNodes/pCData->m_waveSize;
	for (uint iIter = 0; iIter < pCData->m_numIters; iIter++)
	{
		// Self collision
		if (iIter%selfCollisionPeriod == 0)
		{
			uint numWaves = pCData->m_numSelfCollision / THREAD_COUNT;
			for (uint iWave = 0; iWave<numWaves; iWave++)
			{
				uint iIndex = iWave*THREAD_COUNT + threadId;
				uint iNodesRaw = pBuffs->m_inSelfCollision[iIndex];
				if ((iNodesRaw & 0x00008000) == 0)
				{
					uint iNodeWoShift = iNodesRaw & 0x0000ffff;
					uint iNode = iNodeWoShift + pCData->m_nodesShift;
					uint iInNode = pCData->m_startNode + iNode;
					uint iNodeWoShift1 = (iNodesRaw & 0xffff0000) >> 16;
					uint iNode1 = iNodeWoShift1 + pCData->m_nodesShift;
					uint iInNode1 = pCData->m_startNode + iNode1;

					float3 pos0 = posLDS[iNode];
					float3 pos1 = posLDS[iNode1];

					float w0 = pBuffs->m_in[iInNode].m_invMass;
					float w1 = pBuffs->m_in[iInNode1].m_invMass;

					if (w0 + w1 > 0.0f)
					{
						float3 edgeVec = pos1 - pos0;
						float edgeLen = length(edgeVec);
						edgeVec = edgeLen == 0.0 ? float3(0, -1, 0) : edgeVec/edgeLen;
						edgeLen = min(edgeLen, pCData->m_selfCollisionRadius);

						float3 delta = (edgeLen - pCData->m_selfCollisionRadius) * edgeVec;
						float3 deltaLeft = delta * (w0 / (w0 + w1));
						float3 deltaRight = delta - deltaLeft;

						posLDS[iNode] = pos0 + deltaLeft;
						GroupMemoryBarrierWithGroupSync();
						posLDS[iNode1] -= deltaRight;
					}
				}
			}
		}

		if (threadId < pCData->m_waveSize)
		{
			// Bending stiffness
			{
				//uint iGroup = (iIter*pCData->m_waveSize + threadId) % pCData->m_numBendingGroups;
				uint iGroup = (iIter + threadId*waveSpace) % pCData->m_numBendingGroups;

				uint iNode0 = pBuffs->m_inBendingGroups[iGroup].m_index0;
				uint iNode1 = pBuffs->m_inBendingGroups[iGroup].m_index1;
				uint iNode2 = pBuffs->m_inBendingGroups[iGroup].m_index2;

				float3 p0 = posLDS[iNode0];
				float3 p1 = posLDS[iNode1];
				float3 p2 = posLDS[iNode2];

				float w0 = pBuffs->m_in[iNode0+pCData->m_startNode].m_invMass;
				float w1 = pBuffs->m_in[iNode1+pCData->m_startNode].m_invMass;
				float w2 = pBuffs->m_in[iNode2+pCData->m_startNode].m_invMass;

				float3 l0Vec = p0 - p1;
				float l0 = length(l0Vec);

				float3 l2Vec = p2 - p1;
				float l2 = length(l2Vec);

				if (l0 < FLT_EPSILON || l2 < FLT_EPSILON)
					continue;

				float3 l0Dir = l0Vec/l0;
				float3 l2Dir = l2Vec/l2;

				float3 p0R = p1 + l0Dir;
				float3 p2R = p1 + l2Dir;

				float3 dVec = p2R - p0R;
				float dR = length(dVec);
				if (dR < FLT_EPSILON)
					continue;
				float3 dDir = dVec / dR;

				float3 vecE = p1 - 0.5f * (p0R + p2R);
				float eR = length(vecE);

				float mR = eR/dR;
				if (mR <= pCData->m_bendingMinMR)
					continue;

				if (mR <= 5.0f)
				{
					float3 biDir = vecE / eR;

					float d0 = -dot(l0Vec, dDir);
					float d2 = dot(l2Vec, dDir);
					float d0Sqr = d0 * d0;
					float d2Sqr = d2 * d2;
					float d = d0 + d2;
					float dSqr = d * d;

					float h = max(0.00001, d2Sqr * w0 + dSqr * w1 + d0Sqr * w2);
					float3 sa = pBuffs->m_inBendingGroups[iGroup].m_k * 2.0f * (mR-pCData->m_bendingMinMR) / h * biDir;
					float3 sa0 = sa * d2Sqr * d0;
					float3 sa2 = sa * d0Sqr * d2;
					float3 sa1 = - sa0 - sa2;

					float3 delta0 = sa0 * w0;
					float3 delta1 = sa1 * w1;
					float3 delta2 = sa2 * w2;

					posLDS[iNode0] = p0 + delta0;
					GroupMemoryBarrierWithGroupSync();
					posLDS[iNode1] += delta1;
					GroupMemoryBarrierWithGroupSync();
					posLDS[iNode2] += pBuffs->m_inBendingGroups[iGroup].m_delay2 ? float3(0.0) : delta2;
					GroupMemoryBarrierWithGroupSync();
					posLDS[iNode2] += pBuffs->m_inBendingGroups[iGroup].m_delay2 ? delta2 : float3(0.0);
				}
			}

			// Unstretch + constraint
			{
				//uint iNodeWoShift = (iIter*pCData->m_waveSize + threadId) % pCData->m_numNodes;
				uint iNodeWoShift = (iIter + threadId*waveSpace) % pCData->m_numNodes;
				uint iNode = iNodeWoShift + pCData->m_nodesShift;
				uint iInNode = pCData->m_startNode + iNode;

				if (iNodeWoShift < pCData->m_numNodes - 1)
				{
					float3 pos0 = posLDS[iNode];
					float3 pos1 = posLDS[iNode + 1];

					float w0 = pBuffs->m_in[iInNode].m_invMass;
					float w1 = pBuffs->m_in[iInNode + 1].m_invMass;

					if (w0 + w1 > 0.0f)
					{
						float edgeLen0 = pBuffs->m_in[iInNode + 1].m_ropeDist - pBuffs->m_in[iInNode].m_ropeDist;
						edgeLen0 *= g_unstretchRelaxInv;

						float3 edgeVec = pos1 - pos0;
						float edgeLen = length(edgeVec);
						edgeVec = edgeLen == 0.0 ? float3(0, -1, 0) : edgeVec/edgeLen;

						float3 delta = (edgeLen - edgeLen0) * edgeVec;
						float3 deltaLeft = delta * (w0 / (w0 + w1));
						float3 deltaRight = delta - deltaLeft;

						posLDS[iNode] = pos0 + deltaLeft;
						GroupMemoryBarrierWithGroupSync();
						posLDS[iNode+1] -= deltaRight;
					}
				}

				float frictionMult = 0.9f + 0.1f * (float)iIter/(float)(pCData->m_numIters-1); 
				RelaxConstraints(iNode, iInNode, frictionMult, pBuffs);
			}
		}
	}

	// Write pos back to main mem
	for (uint ii = 0; ii < nodesPerThread; ii++)
	{
		uint index = ii * THREAD_COUNT + threadId;
		if (index < pCData->m_numNodes)
		{
			index += pCData->m_nodesShift;
			pBuffs->m_inOut[pCData->m_startNode + index].m_pos.xyz = posLDS[index];
			pBuffs->m_inOut[pCData->m_startNode + index].m_flags = flagsLDS[index];
			pBuffs->m_outForSkinning[pCData->m_startNode + index].xyz = posLDS[index];
		}
	}
}
