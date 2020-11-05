/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/ndphys/rope/rope2-internal.h"
#include "gamelib/ndphys/rope/rope2-collector.h"
#include "gamelib/ndphys/rope/rope-mgr.h"
#include "gamelib/ndphys/rope/physvectormath.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/ngen/compute-queue-mgr.h"
#include "ndlib/render/ngen/mesh.h"
#include "ndlib/render/post/post-shading-win.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "ndlib/debug//nd-dmenu.h"

#include "ndlib/render/ndgi/ndgi-platform.h"
#include "ndlib/render/ndgi/submit-utils.h"
#include "ndlib/render/ndgi/frame-submission.h"

//#include <Collide/Shape/Convex/Capsule/hkpCapsuleShape>

#define SIN_45	0.70710678119f

const Vector kVecDown(0.0f, -1.0f, 0.0f);

//extern F32 g_unstrechTolerance; 
extern F32 g_unstretchRelaxInv;
extern bool g_ropeMultiGridStartOnEdges;

extern F32 g_boundaryBendingCorrect;

F32 g_numItersPerEdgeP = 0.55f;
F32 g_numMultiGridItersPerEdgeP = 0.65f;
F32 g_maxNumItersPerNodeP = 85.0f;

bool g_ropeEmulOnCpu = false;

struct RopeMultiLevel
{
	U32F m_numNodes;
	U32F m_firstIndex;
	U32F m_numIters;
	U32F m_numNewNodes;
	U32F m_firstNewIndex;
};

struct RopeMultiNewNode
{
	U32F m_index;
	U32F m_indexA;
	U32F m_indexB;
};

struct RopeBendingGroup
{
	U32F m_index0;
	U32F m_index1;
	U32F m_index2;
	F32 m_k;
	bool m_delay2;
};

struct RopeConst
{
	U32F m_startNode;
	U32F m_numNodesAll;
	U32F m_numNodes; // without extra nodes for bending at start/end
	U32F m_nodesShift; // shift at the start due to the extra nodes for bending
	U32F m_numIters;
	U32F m_numLevels;
	U32F m_waveSize;
	U32F m_numBendingGroups;
	F32 m_bendingMinMR;
	U32 m_numSelfCollision;
	F32 m_selfCollisionRadius;
};

struct RopeNodeDataIn
{
	F32 m_constrPlanes[6][4];
	F32 m_constrBiPlanes[6][4];
	F32 m_distConstr[4];
	F32 m_frictionConstr[5];
	U32 m_numConstrPlanes;
	U32 m_numConstrEdges;
	F32 m_radius;
	F32 m_ropeDist;
	F32 m_invMass;
};

struct RopeNodeDataInOut
{
	F32 m_pos[4];
	U32F m_flags;
};

struct RopeBuffers
{
	ndgi::VSharp m_inLevelNewNodes;
	ndgi::VSharp m_inLevelIndices;
	ndgi::VSharp m_inLevels;
	ndgi::VSharp m_inBendingGroups;
	ndgi::VSharp m_in;
	ndgi::VSharp m_inOut;
	ndgi::VSharp m_inSelfCollision;
	ndgi::VSharp m_outForSkinning;
};

//static
ndgi::ComputeQueue Rope2::s_cmpQueue;

//static
void Rope2::Startup()
{
	s_cmpQueue.Create(1 * 1024);
	g_computeQueueMgr.MapComputeQueue("rope", &s_cmpQueue, 2, 4);

	InitPrimitiveEdges();
}

Vec4 getOuterEdgePlane(const Vec4 plane0, const Vec4 plane1, const Vec4 biPlane0, const Vec4 biPlane1, const Point& pos, const Scalar& radius, U8& planeMask)
{
	Scalar bih0 = Dot4(pos, biPlane0);
	Scalar bih1 = Dot4(pos, biPlane1);
	Scalar h0 = Dot4(pos, plane0);
	Scalar h1 = Dot4(pos, plane1);
	if (bih0 > 0.0f && bih1 > 0.0f)
	{
		Vector e = Vector((h0+radius)*plane0 + bih0*biPlane0);
		Point edgePnt = pos - e;
		Vector norm = Normalize(e);
		Scalar d = Dot(norm, edgePnt - kOrigin);
		Vec4 edgePlane = norm.GetVec4();
		edgePlane.SetW(-d - radius);
		planeMask = 3;
		return edgePlane;
	}
	planeMask = h0 > h1 ? 1 : 2;
	return h0 > h1 ? plane0 : plane1;
}

void RelaxConstraintsComputeEmul(U32F iNode, U32F iInNode, const RopeConst* pConst, const RopeNodeDataIn* pIn, float frictionMult, Point* posLDS, U8* flagsLDS)
{
	const RopeNodeDataIn& nodeDataIn = pIn[iInNode];

	if (nodeDataIn.m_invMass > 0.0f)
	{
		F32 radius = nodeDataIn.m_radius;
		Point pos = posLDS[iNode];
		U8 flags = flagsLDS[iNode];

		// Distance constraint
		{
			Vec4 distConstr(pIn[iInNode].m_distConstr);
			Point p = Point(distConstr);
			Vector v = pos - p;
			Scalar dist = Length(v);
			v = dist == Scalar(kZero) ? Vector(kZero) : v / dist;
			Scalar err = Max(Scalar(0.0f), dist - distConstr.W());
			pos -= err * v;
		}

		// Friction constraint
		{
			Point fPos(pIn[iInNode].m_frictionConstr);
			Vector v = pos - fPos;
			F32 dist0 = Length(v);
			F32 dist = dist0 * pIn[iInNode].m_frictionConstr[3];
			dist += pIn[iInNode].m_frictionConstr[4];
			dist = Min(dist0, dist);
			pos -= (dist0 == 0.0f ? 0.0f : dist/dist0) * frictionMult * v;
		}

		// Collision constraints
		U32 numPushPlanes = 0;
		Vec4 pushPlanes[4];
		U8 flagMask[4];

		for (U32 iEdge = 0; iEdge<nodeDataIn.m_numConstrEdges; iEdge++)
		{
			Vec4 plane0(nodeDataIn.m_constrPlanes[iEdge*2]);
			Vec4 plane1(nodeDataIn.m_constrPlanes[iEdge*2+1]);
			Vec4 biPlane0(nodeDataIn.m_constrBiPlanes[iEdge*2]);
			Vec4 biPlane1(nodeDataIn.m_constrBiPlanes[iEdge*2+1]);
			U8 planeMask;
			pushPlanes[numPushPlanes] = getOuterEdgePlane(plane0, plane1, biPlane0, biPlane1, pos, radius, planeMask);
			flagMask[numPushPlanes] = planeMask << iEdge*2;
			numPushPlanes++;
		}

		for (U32 iPlane = nodeDataIn.m_numConstrEdges*2; iPlane<nodeDataIn.m_numConstrPlanes; iPlane++)
		{
			pushPlanes[numPushPlanes] = Vec4(nodeDataIn.m_constrPlanes[iPlane]);
			flagMask[numPushPlanes] = 1 << iPlane;
			numPushPlanes++;
		}

		for (U32 iPushPlane = 0; iPushPlane<numPushPlanes; iPushPlane++)
		{
			float h = Dot4(pos, pushPlanes[iPushPlane]) - radius;
			flags |= (U8)(h < 0.0 ? flagMask[iPushPlane] : 0);
			pos -= Min(h, 0.0f) * Vector(pushPlanes[iPushPlane]);
		}

		posLDS[iNode] = pos;
		PHYSICS_ASSERT(pIn[iNode+pConst->m_startNode].m_numConstrPlanes > 0 || flags == 0);
		flagsLDS[iNode] = flags;
	}
}

void RelaxEdgePosComputeEmul(U32F i, U32F j, U32F iIn, U32F jIn, const RopeNodeDataIn* pIn, Vector& deltaLeft, Vector& deltaRight, Point* posLDS)
{
	Point p0 = posLDS[i];
	Point p1 = posLDS[j];

	Scalar w0 = pIn[iIn].m_invMass;
	Scalar w1 = pIn[jIn].m_invMass;
	if (w0 + w1 == 0.0f)
	{
		deltaLeft = kZero;
		deltaRight = kZero;
		return;
	}

	Scalar edgeLen0 = pIn[jIn].m_ropeDist - pIn[iIn].m_ropeDist;
	Scalar edgeLen;
	Vector vecEdge = SafeNormalize(p1-p0, kVecDown, edgeLen);
	Vector delta = (edgeLen - edgeLen0 * g_unstretchRelaxInv) * vecEdge;
	deltaLeft = delta * (w0 / (w0 + w1));
	deltaRight = delta - deltaLeft;

	JAROS_ASSERT(IsFinite(deltaLeft));
	JAROS_ASSERT(IsFinite(deltaRight));
}


void RelaxEdgePosUnstretchOnlyComputeEmul(U32F i, U32F j, U32F iIn, U32F jIn, const RopeNodeDataIn* pIn, Vector& deltaLeft, Vector& deltaRight, const Point* posLDS)
{
	Point p0 = posLDS[i];
	Point p1 = posLDS[j];

	Scalar w0 = pIn[iIn].m_invMass;
	Scalar w1 = pIn[jIn].m_invMass;
	if (w0 + w1 == 0.0f)
	{
		deltaLeft = kZero;
		deltaRight = kZero;
		return;
	}

	Scalar edgeLen0 = pIn[jIn].m_ropeDist - pIn[iIn].m_ropeDist;
	edgeLen0 *= g_unstretchRelaxInv;
	Scalar edgeLen;
	Vector vecEdge = SafeNormalize(p1-p0, kVecDown, edgeLen);
	if (j - i > 1)
		edgeLen = Max(edgeLen, edgeLen0); // only unstretch in multigrid, never de-compress

	Vector delta = (edgeLen - edgeLen0) * vecEdge;
	deltaLeft = delta * (w0 / (w0 + w1));
	deltaRight = delta - deltaLeft;

	JAROS_ASSERT(IsFinite(deltaLeft));
	JAROS_ASSERT(IsFinite(deltaRight));
}

void RelaxSelfCollisionComputeEmul(U32F i, U32F j, U32F iIn, U32F jIn, const RopeConst* pConst, const RopeNodeDataIn* pIn, Vector& deltaLeft, Vector& deltaRight, const Point* posLDS)
{
	Point p0 = posLDS[i];
	Point p1 = posLDS[j];

	Scalar w0 = pIn[iIn].m_invMass;
	Scalar w1 = pIn[jIn].m_invMass;
	if (w0 + w1 == 0.0f)
	{
		deltaLeft = kZero;
		deltaRight = kZero;
		return;
	}

	Scalar edgeLen;
	Vector vecEdge = SafeNormalize(p1-p0, kVecDown, edgeLen);
	edgeLen = Min(edgeLen, pConst->m_selfCollisionRadius);
	Vector delta = (edgeLen - pConst->m_selfCollisionRadius) * vecEdge;
	deltaLeft = delta * (w0 / (w0 + w1));
	deltaRight = delta - deltaLeft;

	JAROS_ASSERT(IsFinite(deltaLeft));
	JAROS_ASSERT(IsFinite(deltaRight));
}

void RelaxBendingComputeEmul(const RopeConst* pConst, const RopeNodeDataIn* pIn, const RopeBendingGroup* pGroup, Vector& deltaLeft, Vector& deltaMiddle, Vector& deltaRight, const Point* posLDS)
{
	deltaLeft = deltaMiddle = deltaRight = Vector(kZero);

	Point p0 = posLDS[pGroup->m_index0];
	Point p1 = posLDS[pGroup->m_index1];
	Point p2 = posLDS[pGroup->m_index2];

	U32F iIn0 = pGroup->m_index0 + pConst->m_startNode;
	U32F iIn1 = pGroup->m_index1 + pConst->m_startNode;
	U32F iIn2 = pGroup->m_index2 + pConst->m_startNode;

	Scalar w0 = pIn[iIn0].m_invMass;
	Scalar w1 = pIn[iIn1].m_invMass;
	Scalar w2 = pIn[iIn2].m_invMass;

	Vector l0Vec = p0 - p1;
	Scalar l0;
	Vector l0Dir = Normalize(l0Vec, l0);

	Vector l2Vec = p2 - p1;
	Scalar l2;
	Vector l2Dir = Normalize(l2Vec, l2);

	if (l0 < FLT_EPSILON || l2 < FLT_EPSILON)
		return;

	Point p0R = p1 + l0Dir;
	Point p2R = p1 + l2Dir;

	Scalar dR;
	Vector dDir = Normalize(p2R - p0R, dR);
	if (dR < FLT_EPSILON)
		return;

	Vector vecE = p1 - Lerp(p0R, p2R, Scalar(0.5f));
	Scalar eR;
	Vector biDir = Normalize(vecE, eR);

	Scalar mR = eR/dR;

	if (mR <= pConst->m_bendingMinMR)
		return;

	if (mR > 5.0f)
		// @@JS
		return;

	Scalar d0 = -Dot(l0Vec, dDir);
	Scalar d2 = Dot(l2Vec, dDir);
	ASSERT(d0 > Scalar(kZero) && d2 > Scalar(kZero));
	Scalar d0Sqr = d0 * d0;
	Scalar d2Sqr = d2 * d2;
	Scalar d = d0 + d2;
	Scalar dSqr = d * d;

	Scalar h = d2Sqr * w0 + dSqr * w1 + d0Sqr * w2;
	Vector sa = pGroup->m_k * 2.0f * (mR-pConst->m_bendingMinMR) / h * biDir;
	Vector sa0 = sa * d2Sqr * d0;
	Vector sa2 = sa * d0Sqr * d2;
	Vector sa1 = - sa0 - sa2;

	deltaLeft   = sa0 * w0;
	deltaMiddle = sa1 * w1;
	deltaRight  = sa2 * w2;

	JAROS_ASSERT(IsFinite(deltaLeft) && LengthSqr(deltaLeft) < 1e8f);
	JAROS_ASSERT(IsFinite(deltaMiddle) && LengthSqr(deltaMiddle) < 1e8f);
	JAROS_ASSERT(IsFinite(deltaRight) && LengthSqr(deltaRight) < 1e8f);
}

void RelaxPositionsComputeEmul(const RopeConst* pConst, const RopeNodeDataIn* pIn, const RopeBendingGroup* pInBendingGroups, 
	const RopeMultiLevel* pInLevels, const U32F* pInLevelIndices, const RopeMultiNewNode* pInLevelNewNodes, RopeNodeDataInOut* pInOut, const U32* pSelfCollision, F32* pOutForSkinning)
{
	PROFILE(Havok, Rope_RelaxPositionsComputeEmul);

	U32F numNodesAll = pConst->m_numNodesAll;
	U32F numNodes = pConst->m_numNodes;
	U32F nodesShift = pConst->m_nodesShift;
	U32F waveSize = pConst->m_waveSize;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	Point* posLDS = NDI_NEW (kAlign16) Point[numNodesAll];
	U8* flagsLDS = NDI_NEW (kAlign16) U8[numNodesAll];

	for (U32F ii = 0; ii<numNodesAll; ii++)
	{
		posLDS[ii] = Point(pInOut[ii+pConst->m_startNode].m_pos);
		flagsLDS[ii] = pInOut[ii+pConst->m_startNode].m_flags;
		PHYSICS_ASSERT(pIn[ii+pConst->m_startNode].m_numConstrPlanes > 0 || flagsLDS[ii] == 0);
	}

	Vector* pDeltaLeft = NDI_NEW (kAlign16) Vector[THREAD_COUNT];
	Vector* pDeltaMiddle = NDI_NEW (kAlign16) Vector[THREAD_COUNT];
	Vector* pDeltaRight = NDI_NEW (kAlign16) Vector[THREAD_COUNT];

	// First do this once to get some "working constraints" approximate before multigrid hides it all
	for (U32F ii = 0; ii<numNodes; ii++)
	{
		U32F iNode = ii + nodesShift;
		U32F iInNode = iNode + pConst->m_startNode;
		RelaxConstraintsComputeEmul(iNode, iInNode, pConst, pIn, 0.0f, posLDS, flagsLDS);
		// We need to save the positions now because those will serve as starting positions for multigrid
		memcpy(pInOut[iInNode].m_pos, &posLDS[iNode], sizeof(posLDS[iNode]));
	}

	// Multigrid unstretch + constraints
	for (U32F iLevel = 0; iLevel < pConst->m_numLevels; iLevel++)
	{
		U32F numLevelNodes = pInLevels[iLevel].m_numNodes;
		U32F thisWaveSize = Min(THREAD_COUNT, numLevelNodes);
		U32F numIters = pInLevels[iLevel].m_numIters;
		U32F firstIndex = pInLevels[iLevel].m_firstIndex;
		U32F waveSpace = numLevelNodes/thisWaveSize;

		for (U32F iIter = 0; iIter < numIters; iIter++)
		{
			// Load and relax
			for (U32F ii = 0; ii < thisWaveSize; ii++)
			{

				U32F iIndex = (iIter + ii*waveSpace) % numLevelNodes;
				U32F iNode = pInLevelIndices[firstIndex + iIndex];
				U32F iInNode = iNode + pConst->m_startNode;
				if (iIndex < numLevelNodes - 1)
				{
					U32F iNode1 = pInLevelIndices[firstIndex + iIndex + 1];
					U32F iInNode1 = iNode1 + pConst->m_startNode;
					RelaxEdgePosUnstretchOnlyComputeEmul(iNode, iNode1, iInNode, iInNode1, pIn, pDeltaLeft[ii], pDeltaRight[ii], posLDS);
				}
			}
			// Store
			for (U32F ii = 0; ii < thisWaveSize; ii++)
			{
				U32F iIndex = (iIter + ii*waveSpace) % numLevelNodes;
				U32F iNode = pInLevelIndices[firstIndex + iIndex];
				U32F iInNode = iNode + pConst->m_startNode;
				if (iIndex < numLevelNodes - 1)
				{
					posLDS[iNode] += pDeltaLeft[ii];
				}
			}
			for (U32F ii = 0; ii < thisWaveSize; ii++)
			{
				U32F iIndex = (iIter + ii*waveSpace) % numLevelNodes;
				if (iIndex < numLevelNodes - 1)
				{
					U32F iNode1 = pInLevelIndices[firstIndex + iIndex + 1];
					U32F iInNode1 = iNode1 + pConst->m_startNode;
					posLDS[iNode1] -= pDeltaRight[ii];
				}
			}

			// Constraints
			for (U32F ii = 0; ii < thisWaveSize; ii++)
			{
				U32F iIndex = (iIter + ii*waveSpace) % numLevelNodes;
				U32F iNode = pInLevelIndices[firstIndex + iIndex];
				U32F iInNode = iNode + pConst->m_startNode;
				RelaxConstraintsComputeEmul(iNode, iInNode, pConst, pIn, 0.9f, posLDS, flagsLDS);
			}
		}

		// Move all new points of the next grid level by proportional movement of the current level neighbors
		U32F numNewNodes = pInLevels[iLevel].m_numNewNodes;
		U32F firstNewIndex = pInLevels[iLevel].m_firstNewIndex;
		for (U32F ii = 0; ii < numNewNodes; ii++)
		{
			U32F newIndex = firstNewIndex + ii;
			U32F iNode = pInLevelNewNodes[newIndex].m_index;
			U32F iNodeA = pInLevelNewNodes[newIndex].m_indexA;
			U32F iNodeB = pInLevelNewNodes[newIndex].m_indexB;

			U32F iInNode = pConst->m_startNode + iNode;
			U32F iInNodeA = pConst->m_startNode + iNodeA;
			U32F iInNodeB = pConst->m_startNode + iNodeB;

			float ropeLen = pIn[iInNode].m_ropeDist;
			float ropeLenA = pIn[iInNodeA].m_ropeDist;
			float ropeLenB = pIn[iInNodeB].m_ropeDist;
			float fA = (ropeLen - ropeLenA) / (ropeLenB - ropeLenA);

			Point posA = posLDS[iNodeA];
			Point posA0(pInOut[iInNodeA].m_pos);
			Point posB = posLDS[iNodeB];
			Point posB0(pInOut[iInNodeB].m_pos);

			posLDS[iNode] += fA * (posA - posA0) + (1.0f - fA) * (posB - posB0);
		}
	}

	U32 selfCollisionPeriod = pConst->m_numSelfCollision ? (U32)(1.0f * pConst->m_numNodes / (F32)waveSize + 0.5f) : pConst->m_numIters;
	U32 waveSpace = pConst->m_numNodes/waveSize;
	for(U32F iIter = 0; iIter < pConst->m_numIters; ++iIter)
	{
		// Self collision
		if (iIter%selfCollisionPeriod == 0)
		{
			U32 numWaves = pConst->m_numSelfCollision / THREAD_COUNT;
			for (U32 iWave = 0; iWave<numWaves; iWave++)
			{
				// Load and relax
				for (U32F ii = 0; ii < THREAD_COUNT; ii++)
				{
					U32 iIndex = iWave*THREAD_COUNT + ii;
					U32 iNodesRaw = pSelfCollision[iIndex];
					if ((iNodesRaw & 0x00008000) == 0)
					{
						U32 iNodeWoShift = iNodesRaw & 0x0000ffff;
						U32 iNode = iNodeWoShift + nodesShift;
						U32 iInNode = iNode + pConst->m_startNode;
						U32 iNodeWoShift1 = (iNodesRaw & 0xffff0000) >> 16;
						U32 iNode1 = iNodeWoShift1 + nodesShift;
						U32 iInNode1 = iNode1 + pConst->m_startNode;
						RelaxSelfCollisionComputeEmul(iNode, iNode1, iInNode, iInNode1, pConst, pIn, pDeltaLeft[ii], pDeltaRight[ii], posLDS);
					}
				}

				// Store
				for (U32F ii = 0; ii < THREAD_COUNT; ii++)
				{
					U32 iIndex = iWave*THREAD_COUNT + ii;
					U32 iNodesRaw = pSelfCollision[iIndex];
					if ((iNodesRaw & 0x00008000) == 0)
					{
						U32 iNodeWoShift = iNodesRaw & 0x0000ffff;
						U32F iNode = iNodeWoShift + nodesShift;
						posLDS[iNode] += pDeltaLeft[ii];
					}
				}
				for (U32F ii = 0; ii < THREAD_COUNT; ii++)
				{
					U32 iIndex = iWave*THREAD_COUNT + ii;
					U32 iNodesRaw = pSelfCollision[iIndex];
					if ((iNodesRaw & 0x00008000) == 0)
					{
						U32 iNodeWoShift1 = (iNodesRaw & 0xffff0000) >> 16;
						U32F iNode1 = iNodeWoShift1 + nodesShift;
						posLDS[iNode1] -= pDeltaRight[ii];
					}
				}
			}
		}

		if (1)
		{
			// Bending load and relax
			for (U32F ii = 0; ii < waveSize; ii++)
			{
				U32F iGroup = (iIter + ii*waveSpace) % pConst->m_numBendingGroups;
				RelaxBendingComputeEmul(pConst, pIn, &pInBendingGroups[iGroup], pDeltaLeft[ii], pDeltaMiddle[ii], pDeltaRight[ii], posLDS);
			}

			// Bending store
			for (U32F ii = 0; ii < waveSize; ii++)
			{
				U32F iGroup = (iIter + ii*waveSpace) % pConst->m_numBendingGroups;
				U32F iNode = pInBendingGroups[iGroup].m_index0;
				posLDS[iNode] += pDeltaLeft[ii];
				JAROS_ASSERT(IsFinite(posLDS[iNode]) && LengthSqr(posLDS[iNode]) < 1e8f);
			}
			for (U32F ii = 0; ii < waveSize; ii++)
			{
				U32F iGroup = (iIter + ii*waveSpace) % pConst->m_numBendingGroups;
				U32F iNode = pInBendingGroups[iGroup].m_index1;
				posLDS[iNode] += pDeltaMiddle[ii];
				JAROS_ASSERT(IsFinite(posLDS[iNode]) && LengthSqr(posLDS[iNode]) < 1e8f);
			}
			for (U32F ii = 0; ii < waveSize; ii++)
			{
				U32F iGroup = (iIter + ii*waveSpace) % pConst->m_numBendingGroups;
				U32F iNode = pInBendingGroups[iGroup].m_index2;
				posLDS[iNode] += pDeltaRight[ii];
				JAROS_ASSERT(IsFinite(posLDS[iNode]) && LengthSqr(posLDS[iNode]) < 1e8f);
			}
		}
		else
		{
			for (U32F ii = 0; ii < pConst->m_numBendingGroups; ii++)
			{
				U32F iGroup = ii;
				RelaxBendingComputeEmul(pConst, pIn, &pInBendingGroups[iGroup], pDeltaLeft[ii], pDeltaMiddle[ii], pDeltaRight[ii], posLDS);
				U32F iNode0 = pInBendingGroups[iGroup].m_index0;
				U32F iNode1 = pInBendingGroups[iGroup].m_index1;
				U32F iNode2 = pInBendingGroups[iGroup].m_index2;
				posLDS[iNode0] += pDeltaLeft[ii];
				posLDS[iNode1] += pDeltaMiddle[ii];
				posLDS[iNode2] += pDeltaRight[ii];
			}
		}

		// Unstretch load and relax
		for (U32F ii = 0; ii < waveSize; ii++)
		{
			U32F iNodeWoShift = (iIter + ii*waveSpace) % numNodes;
			U32F iNode = iNodeWoShift + nodesShift;
			U32F iInNode = iNode + pConst->m_startNode;
			if (iNodeWoShift < numNodes - 1)
			{
				RelaxEdgePosComputeEmul(iNode, iNode+1, iInNode, iInNode+1, pIn, pDeltaLeft[ii], pDeltaRight[ii], posLDS);
			}
		}

		// Unstretch store
		for (U32F ii = 0; ii < waveSize; ii++)
		{
			U32F iNodeWoShift = (iIter + ii*waveSpace) % numNodes;
			U32F iNode = iNodeWoShift + nodesShift;
			if (iNodeWoShift < numNodes - 1)
			{
				posLDS[iNode] += pDeltaLeft[ii];
			}
		}
		for (U32F ii = 0; ii < waveSize; ii++)
		{
			U32F iNodeWoShift = (iIter + ii*waveSpace) % numNodes;
			U32F iNode = iNodeWoShift + nodesShift;
			if (iNodeWoShift < numNodes - 1)
			{
				posLDS[iNode+1] -= pDeltaRight[ii];
			}
		}

		// Constraints
		float frictionMult = 0.9f + 0.1f * (float)iIter/(float)(pConst->m_numIters-1); 
		for (U32F ii = 0; ii < waveSize; ii++)
		{
			U32F iNode = (iIter + ii*waveSpace) % numNodes + nodesShift;
			U32F iInNode = iNode + pConst->m_startNode;
			RelaxConstraintsComputeEmul(iNode, iInNode, pConst, pIn, frictionMult, posLDS, flagsLDS);
		}
	}

	for (U32F ii = 0; ii<numNodesAll; ii++)
	{
		memcpy(pInOut[ii+pConst->m_startNode].m_pos, &posLDS[ii], sizeof(posLDS[ii]));
		pInOut[ii+pConst->m_startNode].m_flags = flagsLDS[ii];
		memcpy(&pOutForSkinning[(ii+pConst->m_startNode)*4], &posLDS[ii], sizeof(posLDS[ii]));
	}
}

U32F Rope2::PrepareNextGridLevelCompute(U32F numNodes, U32F nodesShift, U32F* pGroups0, U32F* pGroups1, U32F*& pGroups, U32F& numGroups, RopeMultiNewNode* pMultiNewNodes, U32F& numNewNodes)
{
	U32F* pNewGroups = pGroups == pGroups0 ? pGroups1 : pGroups0;

	U32F group = 0;
	U32F newGroup = 0;
	U32F lastNode = 0;
	U32F largestGroupSize = 0;
	U32F ii = 0;
	do
	{
		U32F groupSize = pGroups[group++];
		if (groupSize > 1)
		{
			// Sub divide group
			U32F subGroupSize = groupSize / 2;

			if (pMultiNewNodes)
			{
				pMultiNewNodes[numNewNodes].m_index = ii + subGroupSize + nodesShift;
				pMultiNewNodes[numNewNodes].m_indexA = ii + nodesShift;
				pMultiNewNodes[numNewNodes].m_indexB = ii + groupSize + nodesShift;
				numNewNodes++;
			}

			pNewGroups[newGroup++] = subGroupSize;
			ii += subGroupSize;
			groupSize = groupSize - subGroupSize;
		}
		pNewGroups[newGroup++] = groupSize;
		ii += groupSize;
		largestGroupSize = Max(largestGroupSize, groupSize);
	} while (ii < numNodes-1);
	ASSERT(ii == numNodes-1);

	pGroups = pNewGroups;
	numGroups = newGroup;
	return largestGroupSize;
}

void Rope2::PrepareMultiGridCompute(U32 iStart, U32 iEnd, U32F nodesShift, F32 itersPerEdge, U32F& numLevels, ndgi::Buffer& hInLevels, ndgi::Buffer& hInLevelIndices, ndgi::Buffer& hInLevelNewNodes)
{
	PROFILE(Havok, Rope_PrepareMultiGridCompute);

	U32F numEdges = iEnd-iStart;
	U32F numNodes = numEdges+1;

	U32F maxNumLevels = 0;
	U32F maxNumNewNodes = numNodes;
	U32F numLevelEdges = numEdges;
	do
	{
		numLevelEdges = numLevelEdges/2 + 1;
		maxNumLevels++;
	} while (numLevelEdges > 2);

	U32F maxNumIndices = numEdges * maxNumLevels; // this is just an estimate of upper bound

	RopeMultiLevel* pLevels = STACK_ALLOC(RopeMultiLevel, maxNumLevels);
	U32F* pLevelsIndices = STACK_ALLOC(U32F, maxNumIndices);
	RopeMultiNewNode* pMultiNewNodes = STACK_ALLOC(RopeMultiNewNode, maxNumNewNodes);
	ALWAYS_ASSERT(pLevels && pLevelsIndices && pMultiNewNodes);

	numLevels = 0;
	U32F numIndices = 0;
	U32F numNewNodes = 0;

	U32F* pGroups0 = STACK_ALLOC(U32F, 2*numEdges);
	U32F* pGroups1 = pGroups0 + numEdges;
	U32F* pGroups = pGroups0;

	// First (the coarsest level) will be composed of all keyframed and edging nodes
	U32F group = 0;
	U32F lastNode = iStart;
	U32F largestGroupSize = 0;
	for (U32F ii = iStart+1; ii <= iEnd-1; ii++)
	{
		if (IsNodeKeyframedInt(ii) || (g_ropeMultiGridStartOnEdges && m_pConstr[ii].m_numEdges > 0))
		{
			U32F groupSize = ii - lastNode;
			largestGroupSize = Max(largestGroupSize, groupSize);
			pGroups[group++] = groupSize;
			lastNode = ii;
		}
	}
	U32F groupSize = iEnd - lastNode;
	largestGroupSize = Max(largestGroupSize, groupSize);
	pGroups[group++] = groupSize;
	if (group == 1)
	{
		// No edges, subdivide
		largestGroupSize = PrepareNextGridLevelCompute(numNodes, nodesShift, pGroups0, pGroups1, pGroups, group, nullptr, numNewNodes);
	}

	while (largestGroupSize > 1)
	{
		PHYSICS_ASSERT(numLevels < maxNumLevels);
		pLevels[numLevels].m_numNodes = 1;
		pLevels[numLevels].m_firstIndex = numIndices;
		PHYSICS_ASSERT(numIndices < maxNumIndices);
		pLevelsIndices[numIndices] = nodesShift;
		numIndices++;
		pLevels[numLevels].m_firstNewIndex = numNewNodes;

		group = 0;
		U32F ii;
		U32F jj = 0;
		do
		{
			ii = jj;
			jj += pGroups[group++];
			pLevels[numLevels].m_numNodes++;
			PHYSICS_ASSERT(numIndices < maxNumIndices);
			pLevelsIndices[numIndices] = nodesShift + jj;
			numIndices++;
		} while (jj < numNodes-1);

		U32F waveSize = Min(THREAD_COUNT, pLevels[numLevels].m_numNodes);
		F32 numIterEdges = (F32)numEdges / (F32)largestGroupSize;
		F32 numItersPerNode = numIterEdges * itersPerEdge;
		U32 numIterations = (U32)(numItersPerNode * numIterEdges/(F32)waveSize);
		pLevels[numLevels].m_numIters = numIterations;

		pLevels[numLevels].m_numNewNodes = 0;
		largestGroupSize = PrepareNextGridLevelCompute(numNodes, nodesShift, pGroups0, pGroups1, pGroups, group, pMultiNewNodes+numNewNodes, pLevels[numLevels].m_numNewNodes);
		numNewNodes += pLevels[numLevels].m_numNewNodes;
		PHYSICS_ASSERT(numNewNodes < maxNumNewNodes);
		numLevels++;
	}

	hInLevels.CreateRwStructuredBuffer(kAllocDoubleGameFrame, sizeof(RopeMultiLevel), numLevels, ndgi::kUsageDefault, pLevels);
	hInLevels.SetDebugName("rope-in-levels");

	hInLevelIndices.CreateRwStructuredBuffer(kAllocDoubleGameFrame, sizeof(U32F), numIndices, ndgi::kUsageDefault, pLevelsIndices);
	hInLevelIndices.SetDebugName("rope-in-levelIndices");

	hInLevelNewNodes.CreateRwStructuredBuffer(kAllocDoubleGameFrame, sizeof(RopeMultiNewNode), numNewNodes, ndgi::kUsageDefault, pMultiNewNodes);
	hInLevelNewNodes.SetDebugName("rope-in-levelNewNodes");
}

void Rope2::RelaxPositionsCompute(U32 iStart, U32 iEnd)
{
	PROFILE(Havok, Rope_RelaxPositionsCompute);

	for (U32F ii = iStart; ii<=iEnd; ii++)
	{
		m_pNodeFlags[ii] |= kNodeGpuOut;
	}

	U32F numNodes = iEnd - iStart + 1;

	// Add extra nodes at start/end for bending
	U32F numNodesAll = numNodes;
	U32F nodesShift = IsNodeKeyframedInt(iStart) && iStart > 0 ? 1 : 0;
	numNodesAll += nodesShift;
	numNodesAll += IsNodeKeyframedInt(iEnd) && iEnd < m_numPoints-1 ? 1 : 0;

	PHYSICS_ASSERT(numNodesAll <= MAX_NODE_COUNT);

	// Number of edges in the longest group
	U32F maxNumEdges = 1;
#if 1
	{
		U32 sec0 = iStart;
		while (sec0 < iEnd)
		{
			U32 sec1 = sec0+1;
			while (sec1 < iEnd && !IsNodeKeyframedInt(sec1))
			{
				sec1++;
			}
			maxNumEdges = Max(maxNumEdges, sec1-sec0);
			sec0 = sec1;
		}
	}
#else
	maxNumEdges = iEnd-iStart; // to keep the iterations constant for one piece of rope (regardless of how its sections are being keyframed)
#endif

	U32 waveSize = Min(THREAD_COUNT, numNodes/2); // divide by 2 is here to make sure all the nodes of the segment are not done at the same time because that does not converge sometimes
	F32 numItersPerNode = maxNumEdges * g_numItersPerEdgeP;
	numItersPerNode = Min(numItersPerNode, g_maxNumItersPerNodeP); // just a cap on num iters per node to prevent rope getting really expensive on gpu for long ropes. Shrug...
	U32 numIterations = (U32)(numItersPerNode * numNodes/(F32)waveSize) + (U32)(numNodes/(F32)waveSize + 1.0f);

	// Multi grid
	ndgi::Buffer hInLevels;
	ndgi::Buffer hInLevelIndices;
	ndgi::Buffer hInLevelNewNodes;

	U32F numLevels = 0;
	F32 multiGridItersPerEdge = g_numMultiGridItersPerEdgeP;
	if (maxNumEdges > 2 && multiGridItersPerEdge * maxNumEdges > 2.0f)
	{
		PrepareMultiGridCompute(iStart, iEnd, nodesShift, multiGridItersPerEdge, numLevels, hInLevels, hInLevelIndices, hInLevelNewNodes);
	}

	// Bending
	U32F numBendingGroups = numNodes - 2 + (numNodesAll - numNodes ) * 2;
	ndgi::Buffer hInBendingGroups;
	hInBendingGroups.CreateRwStructuredBuffer(kAllocDoubleGameFrame, sizeof(RopeBendingGroup), numBendingGroups, ndgi::kUsageDefault);
	hInBendingGroups.SetDebugName("rope-in-bending-groups");

	F32 bendingStiffnes = m_fBendingStiffness * m_scStepTime * 30.0f;

	RopeBendingGroup* pInBendingGroups = (RopeBendingGroup*)hInBendingGroups.GetBaseAddr();
	numBendingGroups = 0;
	if (IsNodeKeyframedInt(iStart) && iStart > 0)
	{
		pInBendingGroups[numBendingGroups].m_index0 = -1 + nodesShift;
		pInBendingGroups[numBendingGroups].m_index1 = 0 + nodesShift;
		pInBendingGroups[numBendingGroups].m_index2 = 1 + nodesShift;
		pInBendingGroups[numBendingGroups].m_k = g_boundaryBendingCorrect * bendingStiffnes;
		pInBendingGroups[numBendingGroups].m_delay2 = false;
		numBendingGroups++;

		pInBendingGroups[numBendingGroups].m_index0 = -1 + nodesShift;
		pInBendingGroups[numBendingGroups].m_index1 = 0 + nodesShift;
		pInBendingGroups[numBendingGroups].m_index2 = 2 + nodesShift;

		F32 d2 = m_pRopeDist[iStart+2] - m_pRopeDist[iStart];
		F32 kf = (2.0f*m_fSegmentLength+0.01f-d2) / m_fSegmentLength;
		//ASSERT(kf >= 0.0f && kf<= 1.0f);
		kf = MinMax01(kf);
		pInBendingGroups[numBendingGroups].m_k = kf * g_boundaryBendingCorrect * bendingStiffnes;

		pInBendingGroups[numBendingGroups].m_delay2 = true;
		numBendingGroups++;
	}
	for (U32F ii = 1; ii<numNodes-1; ii++)
	{
		pInBendingGroups[numBendingGroups].m_index0 = ii-1 + nodesShift;
		pInBendingGroups[numBendingGroups].m_index1 = ii + nodesShift;
		pInBendingGroups[numBendingGroups].m_index2 = ii+1 + nodesShift;
		pInBendingGroups[numBendingGroups].m_k = bendingStiffnes;
		pInBendingGroups[numBendingGroups].m_delay2 = false;
		numBendingGroups++;
	}
	if (IsNodeKeyframedInt(iEnd) && iEnd < m_numPoints-1)
	{
		// Intentionally switching the order here so that we can use "delay2" feature
		pInBendingGroups[numBendingGroups].m_index0 = numNodes + nodesShift;
		pInBendingGroups[numBendingGroups].m_index1 = numNodes-1 + nodesShift;
		pInBendingGroups[numBendingGroups].m_index2 = numNodes-3 + nodesShift;

		F32 d2 = m_pRopeDist[iEnd] - m_pRopeDist[iEnd-2];
		F32 kf = (2.0f*m_fSegmentLength+0.01f-d2) / m_fSegmentLength;
		//ASSERT(kf >= 0.0f && kf<= 1.0f);
		kf = MinMax01(kf);
		pInBendingGroups[numBendingGroups].m_k = kf * g_boundaryBendingCorrect * bendingStiffnes;

		pInBendingGroups[numBendingGroups].m_delay2 = true;
		numBendingGroups++;

		pInBendingGroups[numBendingGroups].m_index0 = numNodes-2 + nodesShift;
		pInBendingGroups[numBendingGroups].m_index1 = numNodes-1 + nodesShift;
		pInBendingGroups[numBendingGroups].m_index2 = numNodes + nodesShift;
		pInBendingGroups[numBendingGroups].m_k = g_boundaryBendingCorrect * bendingStiffnes;
		pInBendingGroups[numBendingGroups].m_delay2 = false;
		numBendingGroups++;
	}

	// Precalculate for bending test
	F32 halfFreeBendAnglePerSeg = Min(m_fFreeBendAngle, 0.5f*PI);
	F32 bendingMinMR = Max(0.00001f, Tan(halfFreeBendAnglePerSeg));

	// Consts
	RopeConst* pConst = NDI_NEW(kAllocDoubleGameFrame, kAlign64) RopeConst;
	pConst->m_numNodesAll = numNodesAll;
	pConst->m_numNodes = numNodes;
	pConst->m_nodesShift = nodesShift;
	pConst->m_numIters = numIterations;
	pConst->m_startNode = iStart - nodesShift;
	pConst->m_numBendingGroups = numBendingGroups;
	pConst->m_bendingMinMR = bendingMinMR;
	pConst->m_numLevels = numLevels;
	pConst->m_waveSize = waveSize;
	pConst->m_numSelfCollision = m_numSelfCollision;
	pConst->m_selfCollisionRadius = 1.0f*m_fRadius;

	if (m_emulOnCpu || g_ropeEmulOnCpu)
	{
		RopeNodeDataIn* pInData = (RopeNodeDataIn*)m_hCsInBuffer.GetBaseAddr();
		RopeNodeDataInOut* pInOutData = (RopeNodeDataInOut*)m_hCsInOutBuffer.GetBaseAddr();
		F32* pOutDataForSkinning = (F32*)m_hCsOutForSkinningBuffer.GetBaseAddr();
		RopeMultiLevel* pInLevels = (RopeMultiLevel*)hInLevels.GetBaseAddr();
		U32F* pInLevelIndices = (U32F*)hInLevelIndices.GetBaseAddr();
		RopeMultiNewNode* pInLevelNewNodes = (RopeMultiNewNode*)hInLevelNewNodes.GetBaseAddr();
		RelaxPositionsComputeEmul(pConst, pInData, pInBendingGroups, pInLevels, pInLevelIndices, pInLevelNewNodes, pInOutData, (U32*)m_pSelfCollision, pOutDataForSkinning);
		hInBendingGroups.Release();
		hInLevels.Release();
		hInLevelIndices.Release();
		hInLevelNewNodes.Release();
		return;
	}

	RopeBuffers* pBuffers = NDI_NEW(kAllocDoubleGameFrame, kAlign64) RopeBuffers;

	pBuffers->m_in = m_hCsInBuffer.GetVSharp(ndgi::kReadOnly);
	pBuffers->m_inOut = m_hCsInOutBuffer.GetVSharp(ndgi::kSystemCoherent);
	pBuffers->m_outForSkinning = m_hCsOutForSkinningBuffer.GetVSharp(ndgi::kSystemCoherent);
	pBuffers->m_inBendingGroups = hInBendingGroups.GetVSharp(ndgi::kReadOnly);
	if (pConst->m_numLevels > 0)
	{
		pBuffers->m_inLevels = hInLevels.GetVSharp(ndgi::kReadOnly);
		pBuffers->m_inLevelIndices = hInLevelIndices.GetVSharp(ndgi::kReadOnly);
		pBuffers->m_inLevelNewNodes = hInLevelNewNodes.GetVSharp(ndgi::kReadOnly);
	}
	if (m_numSelfCollision)
	{
		pBuffers->m_inSelfCollision.InitAsRegularBuffer(m_pSelfCollision, 2*sizeof(I16), m_numSelfCollision);
	}

	struct SrtData : public ndgi::ValidSrt<SrtData>
	{
		RopeConst*		m_pConsts;
		RopeBuffers*	m_pBuffs;

		void Validate(RenderFrameParams const *params, const GpuState *pGpuState)
		{
			ValidatePtr(params, pGpuState, m_pConsts);
			ValidatePtr(params, pGpuState, m_pBuffs);
			m_pBuffs->m_in.Validate(params, pGpuState);
			m_pBuffs->m_inOut.Validate(params, pGpuState);
			m_pBuffs->m_outForSkinning.Validate(params, pGpuState);
			m_pBuffs->m_inBendingGroups.Validate(params, pGpuState);
			if (m_pConsts->m_numLevels > 0)
			{
				m_pBuffs->m_inLevels.Validate(params, pGpuState);
				m_pBuffs->m_inLevelIndices.Validate(params, pGpuState);
				m_pBuffs->m_inLevelNewNodes.Validate(params, pGpuState);
			}
			if (m_pConsts->m_numSelfCollision > 0)
			{
				m_pBuffs->m_inSelfCollision.Validate(params, pGpuState);
			}
		}
	};

	SrtData srtData;
	srtData.m_pConsts = pConst;
	srtData.m_pBuffs = pBuffers;

	ndgi::ComputeShader *pShader = g_postShading.GetCsByIndex(kCsRopeRelaxPos);
	m_pCmpContext->SetCsShader(pShader);
	m_pCmpContext->SetCsSrt(&srtData, sizeof(SrtData));
	m_pCmpContext->SetCsSrtValidator(SrtData::ValidateSrt);

	m_pCmpContext->Dispatch(1, 1, 1, (ndgi::Label32*)nullptr);

	hInBendingGroups.Release();
	hInLevels.Release();
	hInLevelIndices.Release();
	hInLevelNewNodes.Release();
}

void Rope2::InitComputeBuffers()
{
	m_hCsInBuffer.CreateRwStructuredBuffer(kAllocDoubleGameFrame, sizeof(RopeNodeDataIn), m_numPoints, ndgi::kUsageDefault);
	m_hCsInBuffer.SetDebugName("rope-in-buffer");
	RopeNodeDataIn* pInData = (RopeNodeDataIn*)m_hCsInBuffer.GetBaseAddr();
	for (U32F ii = 0; ii < m_numPoints; ii++)
	{
		pInData[ii].m_invMass = m_pInvRelMass[ii];
		pInData[ii].m_ropeDist = m_pRopeDist[ii];
		pInData[ii].m_radius = m_pRadius[ii];
		pInData[ii].m_numConstrPlanes = m_pConstr[ii].m_numEdges*2 + (m_pConstr[ii].m_numPlanes - m_pConstr[ii].m_firstNoEdgePlane);
		pInData[ii].m_numConstrEdges = m_pConstr[ii].m_numEdges;
		for (U32F iEdgePlane = 0; iEdgePlane<m_pConstr[ii].m_numEdges*2; iEdgePlane++)
		{
			memcpy(pInData[ii].m_constrPlanes[iEdgePlane], &m_pConstr[ii].m_planes[m_pConstr[ii].m_edgePlanes[iEdgePlane]], sizeof(m_pConstr[ii].m_planes[0]));
		}
		memcpy(pInData[ii].m_constrPlanes[m_pConstr[ii].m_numEdges*2], &m_pConstr[ii].m_planes[m_pConstr[ii].m_firstNoEdgePlane], (m_pConstr[ii].m_numPlanes - m_pConstr[ii].m_firstNoEdgePlane) * sizeof(m_pConstr[ii].m_planes[0]));
		memcpy(pInData[ii].m_constrBiPlanes, m_pConstr[ii].m_biPlanes, m_pConstr[ii].m_numEdges*2 * sizeof(m_pConstr[ii].m_biPlanes[0]));
		Vec4 distConstr = m_pDistConstraints ? m_pDistConstraints[ii] : Vec4(0.0f, 0.0f, 0.0f, FLT_MAX);
		memcpy(pInData[ii].m_distConstr, &distConstr, sizeof(distConstr));
		Vec4 frictionConstr = m_pFrictionConstraints ? m_pFrictionConstraints[ii] : Vec4(kZero);
		memcpy(pInData[ii].m_frictionConstr, &frictionConstr, sizeof(frictionConstr));
		pInData[ii].m_frictionConstr[4] = m_pFrictionConstConstraints ? m_pFrictionConstConstraints[ii] : 0.0f;
	}

	m_hCsInOutBuffer.CreateRwStructuredBuffer(kAllocDoubleGameFrame, sizeof(RopeNodeDataInOut), m_numPoints, ndgi::kUsageDefault);
	m_hCsInOutBuffer.SetDebugName("rope-inout-buffer");
	RopeNodeDataInOut* pInOutData = (RopeNodeDataInOut*)m_hCsInOutBuffer.GetBaseAddr();
	for (U32F ii = 0; ii < m_numPoints; ii++)
	{
		memcpy(pInOutData[ii].m_pos, &m_pPos[ii], sizeof(m_pPos[ii]));
		ASSERT(m_pConstr[ii].m_numPlanes > 0 || m_pConstr[ii].m_flags == 0);
		pInOutData[ii].m_flags = 0;
		//for (U32 iEdgePlane = 0; iEdgePlane<m_pConstr[ii].m_numEdges*2; iEdgePlane++)
		//{
		//	pInOutData[ii].m_flags |= ((m_pConstr[ii].m_flags >> m_pConstr[ii].m_edgePlanes[iEdgePlane]) & 1) << iEdgePlane;
		//}
		//pInOutData[ii].m_flags |= ((m_pConstr[ii].m_flags & Constraint::kIsWorking) >> m_pConstr[ii].m_firstNoEdgePlane) << m_pConstr[ii].m_numEdges*2;
	}

	PHYSICS_ASSERT(m_hCsOutForSkinningBuffer.Valid());
	F32* pOutForSkinningData = (F32*)m_hCsOutForSkinningBuffer.GetBaseAddr();
	for (U32F ii = 0; ii < m_numPoints; ii++)
	{
		memcpy(&pOutForSkinningData[ii*4], &m_pPos[ii], sizeof(m_pPos[ii]));
	}
}

void Rope2::AllocComputeEarly(U32F maxNumPoints)
{
	if (!m_emulOnCpu && !g_ropeEmulOnCpu)
	{
		m_pCmpContext = NDI_NEW(kAllocGameToGpuRing) ndgi::ComputeContext("Rope");
		m_pCmpContext->Create(2304, kAllocGameToGpuRing);
	}

	m_hCsOutForSkinningBuffer.CreateRwStructuredBuffer(kAllocGameToGpuRing, sizeof(F32)*4, maxNumPoints, ndgi::kUsageDefault);
	m_hCsOutForSkinningBuffer.SetDebugName("rope-out-for-skinning-buffer");
}

void Rope2::OpenCompute()
{
	if (!m_emulOnCpu && !g_ropeEmulOnCpu)
	{
		PHYSICS_ASSERT(m_pCmpContext);
		m_pCmpContext->Open();
	}

	InitComputeBuffers();
}

void Rope2::CloseCompute()
{
	if (!m_emulOnCpu && !g_ropeEmulOnCpu)
	{
		m_pCmpContext->Close(ndgi::kCacheActionWbInvL2Volatile, true);

		SubmitQueue<1> sq;
		sq.Add(*m_pCmpContext, s_cmpQueue, -1, SubmitFlags::kDisableValidation);

		RenderFrameParams* pRenderFrameParams = GetCurrentRenderFrameParams();
		PerformSubmissionData* pSubmissionData = NDI_NEW(kAllocDoubleGameFrame, kAlign16) PerformSubmissionData();
		ProcessSubmitQueue(&sq, pRenderFrameParams, pSubmissionData);
	}

	if (m_pRopeSkinningData)
	{
		m_pRopeSkinningData->m_pRopeGpuSimOut = (const F32*)m_hCsOutForSkinningBuffer.GetBaseAddr();
	}
	m_hCsOutForSkinningBuffer.Release();
}

void Rope2::ComputeCleanup()
{
	if (!m_pGpuWaitCounter)
	{
		if (m_pCmpContext)
			m_pCmpContext->Release();
		m_pCmpContext = nullptr;
		m_pRopeSkinningData = nullptr;
	}

	if (m_hCsOutForSkinningBuffer.Valid())
		m_hCsOutForSkinningBuffer.Release();
}

struct WaitRopeComputeJobData
{
	ndgi::ComputeContext* m_pCmpContext;
};

void WaitRopeCompute(ndgi::ComputeContext* pCmpContext)
{
	if (pCmpContext)
	{
		pCmpContext->Wait();
		pCmpContext->Release();
	}
}

JOB_ENTRY_POINT(WaitRopeComputeJob)
{
	WaitRopeComputeJobData* pData = reinterpret_cast<WaitRopeComputeJobData*>(jobParam);
	WaitRopeCompute(pData->m_pCmpContext);
}

void Rope2::GatherCompute()
{
	const RopeNodeDataInOut* pInOutData = (const RopeNodeDataInOut*)m_hCsInOutBuffer.GetBaseAddr();

	I32F sectionStart = 0;
	for (I32F ii = 0; ii<m_numPoints; ii++)
	{
		m_pPos[ii] = Point(pInOutData[ii].m_pos);
		PHYSICS_ASSERT(IsFinite(m_pPos[ii]));
		for (U32 iEdgePlane = 0; iEdgePlane<m_pConstr[ii].m_numEdges*2; iEdgePlane++)
		{
			m_pConstr[ii].m_flags |= ((pInOutData[ii].m_flags >> iEdgePlane) & 1) << m_pConstr[ii].m_edgePlanes[iEdgePlane];
		}
		m_pConstr[ii].m_flags |= (pInOutData[ii].m_flags >> m_pConstr[ii].m_numEdges*2) << m_pConstr[ii].m_firstNoEdgePlane;
		ASSERT(m_pConstr[ii].m_numPlanes > 0 || m_pConstr[ii].m_flags == 0);

		if (m_pNodeFlags[ii] & (kNodeKeyframed|kNodeStrained))
		{
			if (ii > 0 && (m_pNodeFlags[ii-1] & (kNodeKeyframed|kNodeStrained)) == 0)
			{
				PostSolve(sectionStart, ii);
			}
			sectionStart = ii;
		}
	}

	if ((m_pNodeFlags[m_numPoints-1] & (kNodeKeyframed|kNodeStrained)) == 0)
	{
		PostSolve(sectionStart, m_numPoints-1);
	}

	m_hCsInBuffer.Release();
	m_hCsInOutBuffer.Release();
}

void Rope2::WaitAndGatherCompute()
{
	if (m_pRopeSkinningData)
	{
		// We don't need to wait for the Cs
		WaitRopeComputeJobData* pData = NDI_NEW (kAllocDoubleGameFrame) WaitRopeComputeJobData;
		pData->m_pCmpContext = m_pCmpContext;

		if (!m_pGpuWaitCounter)
		{
			m_pGpuWaitCounter = ndjob::AllocateCounter(FILE_LINE_FUNC);
		}
		m_pGpuWaitCounter->SetValue(1);

		ndjob::JobDecl jobDecl(WaitRopeComputeJob, (uintptr_t)pData);
		jobDecl.m_associatedCounter = m_pGpuWaitCounter;
		ndjob::RunJobs(&jobDecl, 1, nullptr, FILE_LINE_FUNC);

		g_ropeMgr.RegisterForPreProcessUpdate(this);
	}
	else
	{
		if (m_pGpuWaitCounter)
		{
			ndjob::FreeCounter(m_pGpuWaitCounter);
			m_pGpuWaitCounter = nullptr;
		}
		WaitRopeCompute(m_pCmpContext);
		GatherCompute();
	}

	// Clear for next frame
	m_pCmpContext = nullptr;
}

void Rope2::FillRopeNodesForSkinning(RopeBonesData* pRopeSkinningData)
{
	pRopeSkinningData->m_numNodes = m_numPoints;
	U32 iKey = 0;
	for (U32F ii = 0; ii<m_numPoints; ii++)
	{
		pRopeSkinningData->m_pNodes[ii].m_pos = m_pPos[ii];
		pRopeSkinningData->m_pNodes[ii].m_ropeDist = m_pRopeDist[ii];
		pRopeSkinningData->m_pNodes[ii].m_flags = m_pNodeFlags[ii];
		pRopeSkinningData->m_pNodes[ii].m_twistDir = m_pTwistDir ? m_pTwistDir[ii] : kUnitYAxis;
	}
}

void Rope2::FillDataForSkinningPreStep(RopeBonesData* pRopeSkinningData)
{
	// Save off and copy over the prev nodes
	pRopeSkinningData->m_pNodes = NDI_NEW (kAllocGameToGpuRing) RopeNodeData[m_numPoints];
	FillRopeNodesForSkinning(pRopeSkinningData);
	pRopeSkinningData->m_pPrevNodes = pRopeSkinningData->m_pNodes;
	pRopeSkinningData->m_numPrevNodes = pRopeSkinningData->m_numNodes;
}

void Rope2::FillDataForSkinningPostStep(RopeBonesData* pRopeSkinningData, bool paused)
{
	pRopeSkinningData->m_pNodes = NDI_NEW (kAllocDoubleGameFrame) RopeNodeData[m_maxNumPoints];
	FillRopeNodesForSkinning(pRopeSkinningData);
	if (!paused && m_pTwistDir)
	{
		pRopeSkinningData->m_pTwistDirOut = NDI_NEW (kAllocDoubleGameFrame) Vector[m_numPoints];
	}
	pRopeSkinningData->m_hasTwist = m_pTwistDir != nullptr;
}

void Rope2::WaitAndGatherAsyncSim()
{
	CheckStepNotRunning(); 

	if (m_pGpuWaitCounter && m_hCsInOutBuffer.Valid())
	{
		ndjob::WaitForCounter(m_pGpuWaitCounter);

		GatherCompute();
		PostGatherCompute();
	}
}

void Rope2::CopyBackTwistDir()
{
	if (m_pRopeSkinningData)
	{
		if (m_pRopeSkinningData->m_pTwistDirOut)
		{
			PHYSICS_ASSERT(m_pRopeSkinningData->m_numNodes == m_numPoints);
			for (U32 ii = 0; ii<m_numPoints; ii++)
			{
				m_pTwistDir[ii] = m_pRopeSkinningData->m_pTwistDirOut[ii];
			}
		}
		m_pRopeSkinningData = nullptr;
	}
}

void Rope2::PreProcessUpdate()
{
	WaitAndGatherAsyncSim();
	CopyBackTwistDir();
}
