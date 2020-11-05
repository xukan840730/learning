/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "common/tree/kd-tree.h"

#include "common/common.h"
#include "common/libmath/commonmath.h"
#include "common/hashes/ingamehash.h"
#include "common/imsg/msg.h"
#include "common/imsg/msg_macro_defs.h"
#include "common/util/timer.h"

#include <stdlib.h>

//#pragma optimize("", off) // uncomment when debugging in release mode

static void DumpTree(FILE* fp, KdTreeBuilderNode* pBNode, int depth);


// ----------------------------------------------------------------------------------------------------
// KdTreeBuilder
// ----------------------------------------------------------------------------------------------------

StringId64 KdTreeBuilder::GetTreeType() const
{
	return StringToStringId64("KdTree");
}

static KdTreeBuilderPrimitive * BuildPrimitiveList(const PrimitiveProxy* pProxy, Aabb& rootAabB, U32F& primListSize, KdTreeBuilder::BuildStats& stats)
{
	int numObjects = pProxy->GetNumObjects();

	KdTreeBuilderPrimitive* pBPrimList = NULL;
	for (int i = 0; i < numObjects; ++i)
	{
		if (!pProxy->ObjectValid(i))
			continue;

		int numPrims = pProxy->GetNumPrimitives(i);
		for (int j = 0; j < numPrims; ++j)
		{
			KdTreeBuilderPrimitive* pBPrim = NDI_NEW KdTreeBuilderPrimitive();
			ALWAYS_ASSERTF(pBPrim, ("out of memory building kd-tree"));

			pBPrim->m_objIndex = i;
			pBPrim->m_primIndex = j;
			pBPrim->m_aabb = pProxy->GetPrimitiveAabb(i, j);
			pBPrim->m_name = pProxy->GetName();
			pBPrim->m_primName = pProxy->GetPrimitiveName(i, j);

			pBPrim->m_pNext = pBPrimList;
			pBPrimList = pBPrim;

			rootAabB.Join(pBPrim->m_aabb);

			++primListSize;

			stats.m_totalMem += sizeof(KdTreeBuilderPrimitive);
		}
	}

	return pBPrimList;
}

static void SplitPrimitiveObbByAxisAlignedPlane(const PrimitiveProxy* pProxy, const KdTreeBuilderPrimitive* pBPrim, KdTreeSplitAxis splitAxis, Scalar_arg splitDistance, Aabb& left, Aabb& right)
{
	// Initialize the AABBs
	left.SetEmpty();
	right.SetEmpty();

	Point v[8];
	Obb obb;
	if (pBPrim->m_primIndex >= 0)
	{
		obb = pProxy->GetPrimitiveObb(pBPrim->m_objIndex, pBPrim->m_primIndex);
	}
	else
	{
		obb = pProxy->GetObjectObb(pBPrim->m_objIndex);
	}

	obb.GetCorners(v);

	// Test the points against the splitting plane, and add them to the
	// correct AABB. Also save off the distances in d[].
	Scalar d[8];
	for (int i = 0; i < 8; ++i)
	{
		d[i] = v[i][splitAxis] - splitDistance; // 1D vector from plane to vertex

		if (d[i] <= SMath::kScalarZero)
		{
			left.IncludePoint(v[i]);
		}
		if (d[i] >= SMath::kScalarZero) // NOT else if! (include on both sides if exactly on the plane)
		{
			right.IncludePoint(v[i]);
		}
	}

	const int edges[12][2] =
	{
		{0, 1}, {1, 2}, {2, 3}, {3, 0},
		{4, 5}, {5, 6}, {6, 7}, {7, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};

	// Now test the edges, splitting any that cross the plane
	// and adding the split point to both AABBs.
	for (int i = 0; i < 12; ++i)
	{
		const int j0 = edges[i][0];
		const int j1 = edges[i][1];

		const Scalar d0 = d[j0];
		const Scalar d1 = d[j1];

		// If this edge is split by the plane...
		// Use >= so that an edge that starts exactly on the plane won't be split.
		if ((d0 >= SMath::kScalarZero) != (d1 >= SMath::kScalarZero))
		{
			// ...then calculate the split point and add it to both bounding boxes!
			const Point v0 = v[j0];
			const Point v1 = v[j1];

			const Vector dir = v1 - v0;									// 3D vector: v0 --> v1
			const Scalar v0tov1 = dir[splitAxis];						// 1D vector: v0 --> v1
			const Scalar v0toPlane = (splitDistance - v0[splitAxis]);	// 1D vector: v0 --> plane
			ASSERT(Abs(v0tov1) > SMath::kScalarZero);		// edge can't be parallel to the plane
			ASSERT(Abs(v0tov1) >= Abs(v0toPlane));	// edge must be be split by the plane
			const Scalar t = v0toPlane * Recip(v0tov1);

			const Point p = v0 + t * dir;

			left.IncludePoint(p);
			right.IncludePoint(p);
		}
	}
}

enum SahEventType
{
	kEventEnd,
	kEventPlanar,
	kEventStart
};

struct SahEvent
{
	SahEventType m_type;
	Scalar m_dist;

	SahEvent() { }
	SahEvent(SahEventType type, Scalar_arg dist) : m_type(type), m_dist(dist) { }
};

static int CompareSahEvent(const void* rawA, const void* rawB) 
{
	const SahEvent* a = (const SahEvent*)rawA;
	const SahEvent* b = (const SahEvent*)rawB;
	// Compare distance
	if (a->m_dist < b->m_dist)
		return -1;
	else if (a->m_dist > b->m_dist)
		return 1;

	// Compare type
	if (a->m_type < b->m_type)
		return -1;
	else if (a->m_type > b->m_type)
		return 1;

	return 0;
}

static float CalcSAHCost(const KdTreeBuilder::SahParams& sah, float Rleft, float Rright, float Nleft, float Nplanar, float Nright)
{
	float lambda = (Nright == 0.0f || Nleft == 0.0f) ? sah.m_lambda : 1.0f;
	float costLeft  = lambda * (sah.m_Ct + sah.m_Ci * ((Nleft + Nplanar) * Rleft + Nright * Rright));
	float costRight = lambda * (sah.m_Ct + sah.m_Ci * (Nleft * Rleft + (Nright + Nplanar) * Rright));
	return Min(costLeft, costRight);
}

static void InsertMeshes(const PrimitiveProxy* pProxy, const KdTreeBuilder::SahParams& sah, const U32 kMaxDepth, const U32 kMaxLeafSize,
	KdTreeBuilderNode* pBNode, const Aabb& nodeAabb, KdTreeBuilderPrimitive* pBPrimList, U32F depth, KdTreeBuilder::BuildStats& stats, FILE* fp)
{
	pBNode->m_aabb = nodeAabb;

	if (depth > stats.m_maxDepth)
	{
		stats.m_maxDepth = depth;
	}

	// No mesh list? Bail!
	if (!pBPrimList)
		return;

	char indentation[64];
	indentation[0] = '\0';
	if (fp)
	{
		memset(indentation, ' ', sizeof(indentation)-1);
		int indentIndex = depth*2;
		if (indentIndex >= sizeof(indentation)-1)
			indentIndex = sizeof(indentation)-1;
		indentation[indentIndex] = '\0';
	}

	U32F numMeshes = 0;
	for (KdTreeBuilderPrimitive* pBPrim = pBPrimList; pBPrim != NULL; pBPrim = pBPrim->m_pNext)
	{
		++numMeshes;
	}

	if (fp)
	{
		fprintf(fp, "%s+++ InsertMeshes(): numMeshes = %u\n", indentation, (U32)numMeshes);
		fprintf(fp, "%s+++ node AABB: m = (%8.2f, %8.2f, %8.2f) M = (%8.2f, %8.2f, %8.2f)\n", indentation,
			(float)nodeAabb.m_min.X(), (float)nodeAabb.m_min.Y(), (float)nodeAabb.m_min.Z(),
			(float)nodeAabb.m_max.X(), (float)nodeAabb.m_max.Y(), (float)nodeAabb.m_max.Z());
	}

	float leafCost = sah.m_Ci * numMeshes;

	float bestCost = leafCost;
	int bestAxis = -1;	// Leaf is best so far!
	Scalar bestSplitDist = SMath::kScalarZero;

	// If we've already gone too far, make it a leaf and get out!
	if ((kMaxDepth && (depth < kMaxDepth)) ||
		(kMaxLeafSize && (numMeshes > kMaxLeafSize)))
	{
		Vector nodeSize = nodeAabb.GetSize();
		Scalar nodeSurfaceArea = nodeAabb.GetSurfaceArea();
		float invNodeSurfaceArea = (float)SafeRecip(nodeSurfaceArea, SMath::kScalarZero);

		for (int axis = 0; axis < 3; ++axis)
		{
			//ScopedTempAllocator jj(FILE_LINE_FUNC);
		
			// First, we need to build a collection of sorted begin/end "events" for
			// all of the mesh AABBs in this node
			U32F numEvents = 0;
			SahEvent* aEvents = NDI_NEW SahEvent[numMeshes * 2];
			ALWAYS_ASSERTF(aEvents, ("out of memory building kd-tree"));

			for (KdTreeBuilderPrimitive* pBPrim = pBPrimList; pBPrim != NULL; pBPrim = pBPrim->m_pNext)
			{
				const Aabb& aabb = pBPrim->m_aabb;
				Vector aabbSize = aabb.GetSize();

				if (aabbSize[axis] > SMath::kScalarZero)
				{
					aEvents[numEvents++] = SahEvent(kEventStart, aabb.m_min[axis]);
					aEvents[numEvents++] = SahEvent(kEventEnd, aabb.m_max[axis]);
				}
				else
				{
					aEvents[numEvents++] = SahEvent(kEventPlanar, aabb.m_min[axis]);
				}
			}

			qsort(&aEvents[0], numEvents, sizeof(aEvents[0]), CompareSahEvent);
		
			// Counters for the number of meshes on the left and right of the splitting plane.
			float Nleft = 0.0f;
			float Nright = (float)numMeshes;

			// Iterate through all of the events for this axis. We don't want to be testing
			// events that ended up at the exact same position more than once, hence this
			// funky while loop and the loops inside it!
			U32 i = 0;
			while (i < numEvents)
			{
				Scalar curSplitDist = aEvents[i].m_dist;

				// Number of prims starting, ending or lying IN the current splitting plane
				float Pend = 0.0f;
				float Pstart = 0.0f;
				float Pplanar = 0.0f;

				while (i < numEvents && aEvents[i].m_dist == curSplitDist && aEvents[i].m_type == kEventEnd)
				{
					Pend += 1.0f;
					++i;
				}

				while (i < numEvents && aEvents[i].m_dist == curSplitDist && aEvents[i].m_type == kEventPlanar)
				{
					Pplanar += 1.0f;
					++i;
				}

				while (i < numEvents && aEvents[i].m_dist == curSplitDist && aEvents[i].m_type == kEventStart)
				{
					Pstart += 1.0f;
					++i;
				}

				// The number of prims that were to the right is now decreased by the total number of planar and/or end events corresponding to this plane.
				Nright -= (Pplanar + Pend);

				// Ignore splits that occur at our node's extents
				if (curSplitDist > nodeAabb.m_min[axis] + KdTree::kSplitTolerance && curSplitDist < nodeAabb.m_max[axis] - KdTree::kSplitTolerance)
				{
					Aabb Vl, Vr;
					nodeAabb.SplitByAxisAlignedPlane(axis, curSplitDist, Vl, Vr, KdTree::kSplitTolerance);

					float SAl = (float)Vl.GetSurfaceArea();
					float SAr = (float)Vr.GetSurfaceArea();
					
					// Evaluate SAH!
					float Rleft = SAl * invNodeSurfaceArea;		// conditional probability right
					float Rright = SAr * invNodeSurfaceArea;	// conditional probability left
					float cost = CalcSAHCost(sah, Rleft, Rright, Nleft, Pplanar, Nright);

					if (cost < bestCost)
					{
						bestCost = cost;
						bestAxis = axis;
						bestSplitDist = curSplitDist;
					}
				}

				// Update counts
				Nleft += (Pstart + Pplanar);
			}

			delete[] aEvents;
		}
	}

	if (bestAxis == -1)
	{
		// No better splitting axis was found, make a leaf! Insert pMeshList
		// into pBNode's mesh list.
		int numLeafPrims = 0;
		while (pBPrimList)
		{
			KdTreeBuilderPrimitive* pBPrim = pBPrimList;
			KdTreeBuilderPrimitive* pBNext = pBPrim->m_pNext;

			pBPrim->m_pNext = pBNode->m_pPrimitives;
			pBNode->m_pPrimitives = pBPrim;

			pBPrimList = pBNext;

			++numLeafPrims;
			++stats.m_numUniquePrimitives;
		}

		// Update stats!
		stats.m_occupiedVolume += nodeAabb.GetVolume();

		if (fp)
		{
			fprintf(fp, "%s+++ NO BETTER AXIS: Created leaf node with %d prims\n", indentation, numLeafPrims);
		}
	}
	else
	{
		KdTreeSplitAxis splitAxis = (KdTreeSplitAxis)bestAxis;
		Scalar splitDist = bestSplitDist;

		// Set up the node, and create the children!
		pBNode->m_splitAxis = splitAxis;
		pBNode->m_splitPlaneDistance = (F32)splitDist;

		if (fp)
		{
			fprintf(fp, "%s+++ BEST AXIS: %s @ %g\n", indentation, (splitAxis == 0 ? "X" : (splitAxis == 1 ? "Y" : "Z")), pBNode->m_splitPlaneDistance);
		}

		// Figure out the mesh instance lists for each side. Note that we'll need to
		// split the OBBs for any of these instances that straddle the split plane,
		// giving us a smaller AABB that we'll use instead.
		KdTreeBuilderPrimitive* pLeftPrims = NULL;
		KdTreeBuilderPrimitive* pRightPrims = NULL;

		while (pBPrimList)
		{
			// Remove the mesh from the input list, since it's going to either
			// move to one of the children, or be split and disposed of
			KdTreeBuilderPrimitive* pBPrim = pBPrimList;
			pBPrimList = pBPrimList->m_pNext;

			// See if this mesh's AABB straddles this node's splitting plane, or which
			// child it belongs to if it is entirely on one side of the plane
			const Aabb& primAabb = pBPrim->m_aabb;

			if (primAabb.m_min[splitAxis] < splitDist && primAabb.m_max[splitAxis] > splitDist)
			{
				// AABB spans the splitting plane

				// We need to allocate another KdTreeMeshInstance for the outer node!
				KdTreeBuilderPrimitive* pLeftPrim = pBPrim;
				KdTreeBuilderPrimitive* pRightPrim = NDI_NEW KdTreeBuilderPrimitive();
				ALWAYS_ASSERTF(pRightPrim, ("out of memory building kd-tree"));

				pRightPrim->m_objIndex = pLeftPrim->m_objIndex;
				pRightPrim->m_primIndex = pLeftPrim->m_primIndex;

				++stats.m_numPrimitives;
				stats.m_totalMem += sizeof(KdTreeBuilderPrimitive);

				SplitPrimitiveObbByAxisAlignedPlane(pProxy, pBPrim, splitAxis, splitDist, pLeftPrim->m_aabb, pRightPrim->m_aabb);

				pLeftPrim->m_pNext = pLeftPrims;
				pLeftPrims = pLeftPrim;
				pRightPrim->m_pNext = pRightPrims;
				pRightPrims = pRightPrim;
			}
			else if (primAabb.m_max[splitAxis] <= splitDist)
			{
				// AABB lies entirely to the left the splitting plane
				pBPrim->m_pNext = pLeftPrims;
				pLeftPrims = pBPrim;
			}
			else
			{
				// AABB lies entirely to the right of the splitting plane
				ASSERT(primAabb.m_min[splitAxis] >= splitDist);
				pBPrim->m_pNext = pRightPrims;
				pRightPrims = pBPrim;
			}
		}

		// Set up the children and their AABBs
		// We know which mesh instances go where, insert them into our left and right children!

		pBNode->m_pLeft = NDI_NEW KdTreeBuilderNode();
		ALWAYS_ASSERTF(pBNode->m_pLeft, ("out of memory building kd-tree"));
		if (pBNode->m_pLeft)
		{
			++stats.m_numNodes;
			stats.m_totalMem += sizeof(KdTreeBuilderNode);
			
			Aabb leftAabb = nodeAabb;
			leftAabb.m_max.Set(splitAxis, splitDist);

			InsertMeshes(pProxy, sah, kMaxDepth, kMaxLeafSize, pBNode->m_pLeft, leftAabb, pLeftPrims, depth + 1, stats, fp);

			if (pBNode->m_pLeft->IsLeafNode())
			{
				++stats.m_numLeafNodes;
				if (!pBNode->m_pLeft->m_pPrimitives)
					++stats.m_numEmptyNodes;
			}
		}

		pBNode->m_pRight = NDI_NEW KdTreeBuilderNode();
		ALWAYS_ASSERTF(pBNode->m_pRight, ("out of memory building kd-tree"));
		if (pBNode->m_pRight)
		{
			++stats.m_numNodes;
			stats.m_totalMem += sizeof(KdTreeBuilderNode);
			
			Aabb rightAabb = nodeAabb;
			rightAabb.m_min.Set(splitAxis, splitDist);

			InsertMeshes(pProxy, sah, kMaxDepth, kMaxLeafSize, pBNode->m_pRight, rightAabb, pRightPrims, depth + 1, stats, fp);

			if (pBNode->m_pRight->IsLeafNode())
			{
				++stats.m_numLeafNodes;
				if (!pBNode->m_pRight->m_pPrimitives)
					++stats.m_numEmptyNodes;
			}
		}		
	}
}

PrimitiveTree* KdTreeBuilder::Build()
{
	U64 timeStart = TimerGetRawCount();

	KdTreeBuilderPrimitive* pBPrimList = NULL;
	U32F numInputPrimitives = 0;

	m_pRoot = NDI_NEW KdTreeBuilderNode();
	ALWAYS_ASSERTF(m_pRoot, ("out of memory building kd-tree"));
	pBPrimList = BuildPrimitiveList(m_pProxy, m_rootAabb, numInputPrimitives, m_buildStats);

	m_buildStats.m_numInputPrimitives = numInputPrimitives;
	m_buildStats.m_numNodes = 1;
	m_buildStats.m_totalMem += sizeof(KdTree) + sizeof(KdTreeBuilderNode);
	m_buildStats.m_rootVolume = m_rootAabb.GetVolume();

	// Now insert the meshes into the tree!
	FILE* fpInsertMeshes = (g_kdTreeOptions.m_dumpInsertMeshes ? m_fp : NULL);
	InsertMeshes(m_pProxy, m_sah, m_maxDepth, m_maxLeafSize, m_pRoot, m_rootAabb, pBPrimList, 0, m_buildStats, fpInsertMeshes);

	// Finally, convert the tree to its compact form
	KdTree* pTree = NDI_NEW KdTree(m_pProxy);
	ALWAYS_ASSERTF(pTree, ("out of memory building kd-tree (NDI_NEW KdTree)"));
	bool success = false;

	if (pTree)
	{
		ASSERT(m_buildStats.m_numNodes > 0);

		if (m_buildStats.m_numUniquePrimitives == 0) // will never happen
			m_buildStats.m_numUniquePrimitives = 1;

		pTree->m_aNodes = NDI_NEW KdTreeNode[m_buildStats.m_numNodes];
		pTree->m_aPrimitives = NDI_NEW KdTreePrimitive[m_buildStats.m_numUniquePrimitives];
		ALWAYS_ASSERTF(pTree->m_aNodes, ("out of memory building kd-tree (m_aNodes)"));
		ALWAYS_ASSERTF(pTree->m_aPrimitives, ("out of memory building kd-tree (m_aPrimitives)"));

		pTree->m_totalNodes = 1;
		pTree->m_totalPrimitives = 0;
		pTree->m_rootAabb = m_rootAabb;
		pTree->m_maxDepth = m_maxDepth;

		m_buildStats.m_totalMemEx = sizeof(KdTree) + m_buildStats.m_numNodes * sizeof(KdTreeNode) + m_buildStats.m_numUniquePrimitives * sizeof(KdTreePrimitive);

		if (m_bvh)
			success = CompactBvh(pTree, m_pRoot, 0);
		else
			success = CompactTree(pTree, m_pRoot, 0);

		if (!success)
		{
			return NULL;
		}
	}

	m_buildStats.m_elapsedTicks = TimerGetRawCount() - timeStart;

	char statsBuf[1024];
	int bytesUsed = GetBuildStatsAsString(statsBuf, sizeof(statsBuf));
	INOTE_VERBOSE(statsBuf);

	if (m_fp && g_kdTreeOptions.m_dumpFinalTree)
	{
		fprintf(m_fp, "0x%p - 0x%p: kd-tree '%s'\n", this, (U8*)this + sizeof(*this), m_pProxy->GetName());
		DumpTree(m_fp, m_pRoot, 1);
	}

	return pTree;
}

U16 KdTreeBuilder::AddPrimitives(KdTree* pTree, KdTreeBuilderNode* pBNode)
{
	U16 numPrims = 0;
	U16 maxPrims = (m_bvh) ? 0x7fff : 0xffff;

	for (const KdTreeBuilderPrimitive* pBPrim = pBNode->m_pPrimitives; pBPrim; pBPrim = pBPrim->m_pNext)
	{
		ALWAYS_ASSERT(pTree->m_totalPrimitives < m_buildStats.m_numUniquePrimitives);
		KdTreePrimitive* pPrim = &pTree->m_aPrimitives[pTree->m_totalPrimitives];

		pPrim->m_objIndex	= pBPrim->m_objIndex;
		pPrim->m_primIndex	= pBPrim->m_primIndex;

		(pTree->m_totalPrimitives)++;
		numPrims++;
		if (numPrims == maxPrims)
		{
			IERR("Too many primitives in one kd-tree node (more than %u)!\n", numPrims);
			IABORT("Unable to build kd-tree\n");
		}
	}

	return numPrims;
}

bool KdTreeBuilder::CompactTree(KdTree* pTree, KdTreeBuilderNode* pBNode, U32 iNode)
{
	bool retval = true;

	ALWAYS_ASSERT(iNode < pTree->m_totalNodes);
	KdTreeNode* pNode = &pTree->m_aNodes[iNode];

	pNode->m_splitAxis			= pBNode->m_splitAxis;
	pNode->m_splitPlaneDistance	= pBNode->m_splitPlaneDistance;
	pNode->m_iFirstPrimitive	= pTree->m_totalPrimitives;
	pNode->m_numPrimitives		= AddPrimitives(pTree, pBNode);
	
	if (pTree->m_totalNodes == KdTreeNode::kInvalidNodeIndex - 1) // if we're about to hit kInvalidNodeIndex...
	{
		IWARN_VERBOSE("Too many nodes in kd-tree (more than %u)!\n", pTree->m_totalNodes);
		return false;
	}
	pNode->m_iLeft				= pBNode->m_pLeft  ? (pTree->m_totalNodes)++ : KdTreeNode::kInvalidNodeIndex;

	if (pTree->m_totalNodes == KdTreeNode::kInvalidNodeIndex - 1) // if we're about to hit kInvalidNodeIndex...
	{
		IWARN_VERBOSE("Too many nodes in kd-tree (more than %u)!\n", pTree->m_totalNodes);
		return false;
	}
	pNode->m_iRight				= pBNode->m_pRight ? (pTree->m_totalNodes)++ : KdTreeNode::kInvalidNodeIndex;

	if (pBNode->m_pLeft)
		retval = retval && CompactTree(pTree, pBNode->m_pLeft, pNode->m_iLeft);

	if (pBNode->m_pRight)
		retval = retval && CompactTree(pTree, pBNode->m_pRight, pNode->m_iRight);

	return retval;
}

bool KdTreeBuilder::CompactBvh(KdTree* pTree, KdTreeBuilderNode* pBNode, U32 iNode)
{
	bool retval = true;

	ALWAYS_ASSERT(iNode < pTree->m_totalNodes);
	KdTreeNode* pNode = &pTree->m_aNodes[iNode];

	pNode->m_splitAxis			= pBNode->m_splitAxis;
	pNode->m_splitPlaneDistance	= pBNode->m_splitPlaneDistance;
	pNode->m_iFirstPrimitive	= pTree->m_totalPrimitives;
	pNode->m_numPrimitives		= AddPrimitives(pTree, pBNode);
	
	pNode->m_iLeftFirstPrimitive = 0;
	pNode->m_leftNumPrimitives = 0;
	pNode->m_iRightFirstPrimitive = 0;
	pNode->m_rightNumPrimitives = 0;

	memset(&pNode->m_leftChildAabb, 0, sizeof(Aabb));
	memset(&pNode->m_rightChildAabb, 0, sizeof(Aabb));
	pNode->m_iLeft = KdTreeNode::kBvhInvalidNodeIndex;
	pNode->m_iRight = KdTreeNode::kBvhInvalidNodeIndex;

	KdTreeBuilderNode* pLNode = pBNode->m_pLeft;
	KdTreeBuilderNode* pRNode = pBNode->m_pRight;

	// If I'm a node with no children, I'm a single node tree.
	// Stick all my prims in my left branch and call it a day!

	if (!pLNode && !pRNode)
	{
		pNode->m_leftChildAabb = pBNode->m_aabb;
		pNode->m_iLeftFirstPrimitive = pNode->m_iFirstPrimitive;
		pNode->m_leftNumPrimitives = pNode->m_numPrimitives;
		pNode->m_iLeft = ~pNode->m_leftNumPrimitives;

		// move my split plane
		pNode->m_splitPlaneDistance = FLT_MAX;
	}
	else
	{
		bool leftLeaf = pLNode && !pLNode->m_pLeft && !pLNode->m_pRight;
		bool rightLeaf = pRNode && !pRNode->m_pLeft && !pRNode->m_pRight;

		if (leftLeaf)
		{
			pNode->m_leftChildAabb = pLNode->m_aabb;
			pNode->m_iLeftFirstPrimitive = pTree->m_totalPrimitives;
			pNode->m_leftNumPrimitives = AddPrimitives(pTree, pLNode);
			pNode->m_iLeft = ~pNode->m_leftNumPrimitives;
		}
		else if (pLNode)
		{
			if (pTree->m_totalNodes == KdTreeNode::kBvhInvalidNodeIndex - 1) // if we're about to hit kInvalidNodeIndex...
			{
				IWARN_VERBOSE("Too many nodes in kd-tree (more than %u)!\n", pTree->m_totalNodes);
				return false;
			}
			pNode->m_leftChildAabb = pLNode->m_aabb;
			pNode->m_iLeft = (pTree->m_totalNodes)++;
			retval = retval && CompactBvh(pTree, pLNode, pNode->m_iLeft);
		}

		if (rightLeaf)
		{
			pNode->m_rightChildAabb = pRNode->m_aabb;
			pNode->m_iRightFirstPrimitive = pTree->m_totalPrimitives;
			pNode->m_rightNumPrimitives = AddPrimitives(pTree, pRNode);
			pNode->m_iRight = ~pNode->m_rightNumPrimitives;
		}
		else if (pRNode)
		{
			if (pTree->m_totalNodes == KdTreeNode::kBvhInvalidNodeIndex - 1) // if we're about to hit kInvalidNodeIndex...
			{
				IWARN_VERBOSE("Too many nodes in kd-tree (more than %u)!\n", pTree->m_totalNodes);
				return false;
			}
			pNode->m_rightChildAabb = pRNode->m_aabb;
			pNode->m_iRight	= (pTree->m_totalNodes)++;
			retval = retval && CompactBvh(pTree, pRNode, pNode->m_iRight);
		}
	}

	return retval;
}

KdTreeBuilder::~KdTreeBuilder()
{
	if (m_pRoot)
	{
		// Free up all the nodes and mesh instances in our tree
		const U32F kMaxDepth = m_buildStats.m_maxDepth * 2;
		KdTreeBuilderNode** apStackNodes = new KdTreeBuilderNode*[kMaxDepth];
		U32F stackDepth = 0;

		KdTreeBuilderNode* pBNode = m_pRoot;
		while (pBNode)
		{
			// Free up mesh instances
			while (pBNode->m_pPrimitives)
			{
				KdTreeBuilderPrimitive* pBPrim = pBNode->m_pPrimitives;
				pBNode->m_pPrimitives = pBNode->m_pPrimitives->m_pNext;
				NDI_DELETE pBPrim;
			}

			// If this is an interior node, push its children onto the stack
			if (!pBNode->IsLeafNode())
			{
				apStackNodes[stackDepth++] = pBNode->m_pLeft;
				apStackNodes[stackDepth++] = pBNode->m_pRight;
			}

			// Free this node!
			NDI_DELETE pBNode;

			// Pop one off the stack if we have one, otherwise we're done!
			pBNode = stackDepth ? apStackNodes[--stackDepth] : NULL;
		}

		m_pRoot = NULL;

		delete[] apStackNodes;
	}
}

int KdTreeBuilder::GetBuildStatsAsString(char* buf, size_t bufferCapacity) const
{
	char* origBuf = buf;
	char* endBuf = buf + bufferCapacity;
	#undef  BUF_REMAINING
	#define BUF_REMAINING ((endBuf > buf) ? (endBuf - buf) : 0)

	buf += snprintf(buf, BUF_REMAINING, "Built KdTree for %s in %fms:\n", m_pProxy->GetName(), ConvertTicksToSeconds(m_buildStats.m_elapsedTicks) * 1000.0f);
	buf += snprintf(buf, BUF_REMAINING, "              SAH: Ct=%g Ci=%g lambda=%g maxDepth=%u\n", m_sah.m_Ct, m_sah.m_Ci, m_sah.m_lambda, m_maxDepth);
	buf += snprintf(buf, BUF_REMAINING, " Input Primitives: %d\n", m_buildStats.m_numInputPrimitives);
	buf += snprintf(buf, BUF_REMAINING, "       Primitives: %d\n", m_buildStats.m_numPrimitives);
	buf += snprintf(buf, BUF_REMAINING, "            Nodes: %d\n", m_buildStats.m_numNodes);
	buf += snprintf(buf, BUF_REMAINING, "       Leaf Nodes: %d\n", m_buildStats.m_numLeafNodes);
	buf += snprintf(buf, BUF_REMAINING, " Empty Leaf Nodes: %d\n", m_buildStats.m_numEmptyNodes);
	buf += snprintf(buf, BUF_REMAINING, "        Max Depth: %d\n", m_buildStats.m_maxDepth);
	buf += snprintf(buf, BUF_REMAINING, "      Root Volume: %.1f m^3\n", m_buildStats.m_rootVolume);
	buf += snprintf(buf, BUF_REMAINING, "  Occupied Volume: %.1f m^3\n", m_buildStats.m_occupiedVolume);
	buf += snprintf(buf, BUF_REMAINING, " Total Memory Use: %.3f KiB\n", m_buildStats.m_totalMem / 1024.0f);
	buf += snprintf(buf, BUF_REMAINING, "  Total Memory Ex: %.3f KiB\n", m_buildStats.m_totalMemEx / 1024.0f);

	return (int)(buf - origBuf);
}

static void DumpTree(FILE* fp, KdTreeBuilderNode* pBNode, int depth)
{
	char indentation[64];
	memset(indentation, ' ', sizeof(indentation)-1);
	int indentIndex = depth*2;
	if (indentIndex >= sizeof(indentation)-1)
		indentIndex = sizeof(indentation)-1;
	indentation[indentIndex] = '\0';

	if (pBNode->IsLeafNode())
	{
		int numPrims = 0;
		for (const KdTreeBuilderPrimitive* pBPrim = pBNode->m_pPrimitives; pBPrim; pBPrim = pBPrim->m_pNext)
		{
			++numPrims;
		}

		fprintf(fp, "%s0x%p: BEST AXIS: %s @ %g (LEAF: %d prims)\n", indentation, pBNode,
			(pBNode->m_splitAxis == 0 ? "X" : (pBNode->m_splitAxis == 1 ? "Y" : "Z")),
			pBNode->m_splitPlaneDistance,
			numPrims);
	}
	else
	{
		ASSERT(pBNode->m_pPrimitives == NULL);
		fprintf(fp, "%s0x%p: BEST AXIS: %s @ %g\n", indentation, pBNode,
			(pBNode->m_splitAxis == 0 ? "X" : (pBNode->m_splitAxis == 1 ? "Y" : "Z")),
			pBNode->m_splitPlaneDistance);
	}

	if (pBNode->m_pLeft)
		DumpTree(fp, pBNode->m_pLeft, depth + 1);
	if (pBNode->m_pRight)
		DumpTree(fp, pBNode->m_pRight, depth + 1);
}
