/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "corelib/math/gamemath.h"
#include "corelib/util/bigsort.h"
#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/ndphys/rope/rope2-util.h"
#include "gamelib/ndphys/rope/physvectormath.h"
#include "gamelib/ndphys/rope/rope-mgr.h"
#include "gamelib/ndphys/havok-game-cast-filter.h"
#include "gamelib/ndphys/havok-collision-cast.h"
#include "gamelib/ndphys/havok-data.h"
#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/level/artitem.h"

#include "physics/havokext/havok-cast-filter.h"
#include "physics/havokext/havok-shapetag-codec.h"

#include <Common/Base/Math/Vector/hkVector4Util.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Geometry/Internal/Algorithms/RayCast/hkcdRayCastTriangle.h>

#include <Physics/Physics/Collide/Shape/Composite/Compound/hknpCompoundShape.h>
#include <Physics/Physics/Collide/Shape/Composite/Mesh/Compressed/hknpCompressedMeshShape.h>
#include <Physics/Physics/Collide/Shape/Convex/Triangle/hknpTriangleShape.h>
#include <Physics/Physics/Collide/Shape/Convex/Box/hknpBoxShape.h>
#include <Physics/Physics/Collide/Shape/Convex/Sphere/hknpSphereShape.h>
#include <Physics/Physics/Collide/Shape/Convex/Capsule/hknpCapsuleShape.h>
#include <Physics/Physics/Collide/Shape/Convex/Cylinder/hknpCylinderShape.h>
#include <Physics/Physics/Collide/Shape/hknpShapeCollector.h>
#include <Physics/Physics/Collide/Query/Collector/hknpCollisionQueryCollector.h>
#include <Physics/Physics/Collide/Query/hknpCollisionQuery.h>

// Ripped off havok code and modified to avoid flipping normal in the direction of where the ray comes from
//
//	Intersects a line segment with a triangle. Returns 1 if the segment intersects the triangle,
//	and 0 otherwise.

inline hkBool32 HK_CALL hkcdSegmentTriangleIntersectND(	
	const hkcdRay& ray,
	hkVector4Parameter vA, hkVector4Parameter vB, hkVector4Parameter vC,
	hkSimdRealParameter tolerance, hkVector4& normalOut, hkSimdReal& fractionInOut)
{
	hkVector4Comparison cmpPQ;
	hkVector4 vN;
	{
		hkVector4 vAB;	vAB.setSub(vB, vA);
		hkVector4 vAC;	vAC.setSub(vC, vA);
		hkVector4 vAP;	vAP.setSub(ray.m_origin, vA);
		vN.setCross(vAB, vAC);

		const hkSimdReal distAP		= vN.dot<3>(vAP);				// dist_a
		const hkSimdReal projLen	= vN.dot<3>(ray.m_direction);	// dist_a - dist_b
		const hkSimdReal distAQ		= distAP + projLen;				// dist_b
		const hkSimdReal distance	= -(distAP / projLen);			// distance

		// cmpPQ = (dist_a * dist_b >= 0.0f) && ( (dist_a != 0.0f) || (dist_b == 0.0f) )
		cmpPQ.setOr(distAP.notEqualZero(), distAQ.equalZero());
		cmpPQ.setAnd((distAP * distAQ).greaterEqualZero(), cmpPQ);

		// cmpPQ = (distance < fractionInOut) && ( (dist_a * dist_b < 0.0f) || ( (dist_a == 0.0f) && (dist_b != 0.0f) ) )
		cmpPQ.setAndNot(distance.less(fractionInOut), cmpPQ);

		// Early exit
		if ( !cmpPQ.anyIsSet() )
		{
			return false;
		}

		// Set proper sign for the normal
		fractionInOut = distance;
		normalOut = vN;
	}

	hkVector4 vDots;
	{
		// Compute intersection point
		hkVector4 vI;	vI.setAddMul(ray.m_origin, ray.m_direction, fractionInOut);

		// Test whether the intersection point is inside the triangle
		hkVector4 vIA;	vIA.setSub(vA, vI);
		hkVector4 vIB;	vIB.setSub(vB, vI);
		hkVector4 vIC;	vIC.setSub(vC, vI);

		hkVector4 nIAB;	nIAB.setCross(vIA, vIB);
		hkVector4 nIBC;	nIBC.setCross(vIB, vIC);
		hkVector4 nICA;	nICA.setCross(vIC, vIA);

		hkVector4Util::dot3_1vs4(vN, nIAB, nIBC, nICA, vN, vDots);
	}

	// Decide whether we have an intersection or not
	{
		// Normalize normal
		const hkSimdReal squaredNormalLen = vDots.getComponent<3>();
		normalOut.mul(squaredNormalLen.sqrtInverse());

		hkVector4 edgeTol;	edgeTol.setAll(-squaredNormalLen * tolerance);

		hkVector4Comparison cmp0;
		cmp0.setAnd(vDots.greaterEqual(edgeTol), cmpPQ);
		return cmp0.allAreSet(hkVector4ComparisonMask::MASK_XYZ);
	}
}

void* allocFromScratch(U32F size, char*& pScratchMem, U32F& memSize)
{
	if (size <= memSize)
	{
		void* ptr = pScratchMem;
		pScratchMem += size;
		memSize -= size;
		return ptr;
	}

	return NDI_NEW (kAllocSingleGameFrame, kAlign16) char[size];
}

void RopeColTreeBuilder::Init(const RopeColTree* pTree, U16 maxNumElements, U16 elemSize, char*& pScratchMem, U32F& memSize)
{
	m_pTree = pTree;
	m_numNodes = pTree->GetNumNodes();
	m_maxNumElements = maxNumElements;
	m_elementSize = elemSize;

	m_pNumIndices = (U16*)allocFromScratch(m_numNodes * sizeof(U16), pScratchMem, memSize);
	memset(m_pNumIndices, 0, m_numNodes * sizeof(U16));
	m_pBlockIndex = (U16*)allocFromScratch(m_numNodes * sizeof(U16), pScratchMem, memSize);

	m_pElements = (char*)allocFromScratch(maxNumElements * elemSize, pScratchMem, memSize);
	m_numElements = 0;

	m_maxNumBlocks = maxNumElements <= pTree->GetNumNodes() ? maxNumElements : ((U32F)((maxNumElements - pTree->GetNumNodes()) / (F32)NodeIndicesBlock::kNumIndices + 1.0f) + pTree->GetNumNodes());
	m_pIndicesBlocks = (NodeIndicesBlock*)allocFromScratch(m_maxNumBlocks * sizeof(NodeIndicesBlock), pScratchMem, memSize);
	m_numBlocks = 0;

	m_overflow = false;
}

I32F RopeColTreeBuilder::AddElement(char* pElement, const Aabb& aabb)
{
	if (m_numElements >= m_maxNumElements)
	{
		m_overflow = true;
		return -1;
	}

	U16 node = m_pTree->FindNode(aabb);

	NodeIndicesBlock* pBlock;
	U32F indexInsideBlock = m_pNumIndices[node] % NodeIndicesBlock::kNumIndices;
	if (indexInsideBlock == 0)
	{
		if (m_numBlocks >= m_maxNumBlocks)
		{
			ASSERT(false);
			m_overflow = true;
			return -1;
		}
		pBlock = &m_pIndicesBlocks[m_numBlocks];
		pBlock->m_nextBlock = m_pBlockIndex[node];
		m_pBlockIndex[node] = m_numBlocks;
		m_numBlocks++;
	}
	else
	{
		pBlock = &m_pIndicesBlocks[m_pBlockIndex[node]];
	}
	pBlock->m_indices[indexInsideBlock] = m_numElements;

	m_pNumIndices[node]++;

	char* pElemStorage = m_pElements + m_numElements * m_elementSize;
	memcpy(pElemStorage, pElement, m_elementSize);
	m_numElements++;

	return m_numElements-1;
}

void RopeColTreeBuilder::Compile(char* pElementsOut, U16& numElementsOut, RopeColTreeNode* pNodesOut, U16* pElementIndexTranslation)
{
	for (U16 ii = 0; ii<m_numNodes; ii++)
	{
		U16 numIndices = m_pNumIndices[ii];
		U16 nextBlock = m_pBlockIndex[ii];
		pNodesOut[ii].m_numElems = numIndices;
		pNodesOut[ii].m_elemIndex = numElementsOut;
		U32F numIndicesInBlock = numIndices % NodeIndicesBlock::kNumIndices;
		if (numIndicesInBlock == 0)
			numIndicesInBlock = NodeIndicesBlock::kNumIndices;
		while (numIndices)
		{
			NodeIndicesBlock* pBlock = &m_pIndicesBlocks[nextBlock];
			for (U16 jj = 0; jj<numIndicesInBlock; jj++)
			{
				U16 oldIndex = pBlock->m_indices[jj];
				if (pElementIndexTranslation)
					pElementIndexTranslation[oldIndex] = numElementsOut;
				memcpy(pElementsOut, m_pElements + oldIndex * m_elementSize, m_elementSize);
				pElementsOut += m_elementSize;
				numElementsOut++;
			}
			numIndices -= numIndicesInBlock;
			numIndicesInBlock = NodeIndicesBlock::kNumIndices;
			nextBlock = pBlock->m_nextBlock;
		}
	}

	ASSERT(numElementsOut == m_numElements);
}

void RopeColTree::Init(F32 maxRopeLen)
{
	F32 aabbSize = Sqrt(0.33f * Sqr(maxRopeLen)); // 30% extra for stretching
	U32F numDimSplits = 0;
	while (aabbSize >= 1.0f && numDimSplits < 4) 
	{
		numDimSplits++;
		aabbSize *= 0.5f;
	}
	m_maxNumLevels = 1 + 3*numDimSplits;
	m_numLevels = 0;
}

void RopeColTree::Reset()
{
	m_numLevels = 0;
}

void RopeColTree::CreateTree(const Aabb& aabb, F32 minNodeSize)
{
	m_aabb = aabb;
	Aabb levelAabb = aabb;
	U16 level = 0;
	m_levelAxis = 0;
	while (level < m_maxNumLevels-1)
	{
		Vector size = levelAabb.GetSize();
		if (size.X() >= size.Y() && size.X() >= size.Z())
		{
			if (size.X() < minNodeSize)
				break;
			levelAabb.m_max.SetX(levelAabb.m_min.X() + 0.5f * size.X());
		}
		else if (size.Y() >= size.Z())
		{
			if (size.Y() < minNodeSize)
				break;
			m_levelAxis |= 1 << (level * 2);
			levelAabb.m_max.SetY(levelAabb.m_min.Y() + 0.5f * size.Y());
		}
		else
		{
			if (size.Y() < minNodeSize)
				break;
			m_levelAxis |= 2 << (level * 2);
			levelAabb.m_max.SetZ(levelAabb.m_min.Z() + 0.5f * size.Z());
		}
		level++;
	}
	m_numLevels = level + 1;
}

U16 RopeColTree::FindNode(const Aabb& aabb) const
{
	v128 min = aabb.m_min.QuadwordValue();
	v128 max = aabb.m_max.QuadwordValue();
	v128 nodeMin = m_aabb.m_min.QuadwordValue();
	v128 nodeMax = m_aabb.m_max.QuadwordValue();

	U16 level = 0;
	U16 node = 0;
	while (level < m_numLevels-1)
	{
		U32F splitAxis = (m_levelAxis >> (level * 2)) & 0x3;
		U32F splitAxisMask = 1 << splitAxis;
		v128 split = SMATH_VEC_MUL(SMATH_VEC_ADD(nodeMin, nodeMax), SMATH_VEC_REPLICATE_FLOAT(0.5f));
		if (SMATH_VEC_MOVEMASK(SMATH_VEC_CMPLE(max, split)) & splitAxisMask)
		{
			// Left
			node++;
			SMATH_VEC_SET_ELEMENT(nodeMax, splitAxis, split);
		}
		else if (SMATH_VEC_MOVEMASK(SMATH_VEC_CMPGE(min, split)) & splitAxisMask)
		{
			// Right
			node += (1 << (m_numLevels - level - 1));
			SMATH_VEC_SET_ELEMENT(nodeMin, splitAxis, split);
		}
		else
		{
			return node;
		}
		level++;
	}
	return node;
}

bool RopeColTree::TraverseNode(const v128 min, const v128 max, const v128 nodeMin, const v128 nodeMax, U16 level, U16 node, TraverseCallback callback, void* pUserData) const
{
	if (!callback(node, pUserData))
		return false;

	if (level < m_numLevels-1)
	{
		U32F splitAxis = (m_levelAxis >> (level * 2)) & 0x3;
		U32F splitAxisMask = 1 << splitAxis;
		v128 split = SMATH_VEC_MUL(SMATH_VEC_ADD(nodeMin, nodeMax), SMATH_VEC_REPLICATE_FLOAT(0.5f));
		if (SMATH_VEC_MOVEMASK(SMATH_VEC_CMPLT(min, split)) & splitAxisMask)
		{
			// Left
			v128 childMax = nodeMax;
			SMATH_VEC_SET_ELEMENT(childMax, splitAxis, split);
			if (!TraverseNode(min, max, nodeMin, childMax, level+1, node+1, callback, pUserData))
				return false;
		}
		if (SMATH_VEC_MOVEMASK(SMATH_VEC_CMPGT(max, split)) & splitAxisMask)
		{
			// Right
			v128 childMin = nodeMin;
			SMATH_VEC_SET_ELEMENT(childMin, splitAxis, split);
			if (!TraverseNode(min, max, childMin, nodeMax, level+1, node + (1 << (m_numLevels - level - 1)), callback, pUserData))
				return false;
		}
	}

	return true;
}

bool RopeColTree::TraverseNodeBundle(const Aabbv128* pAabb, U32F numAabbs, const ExternalBitArray* pBitArray, const v128 nodeMin, const v128 nodeMax, U16 level, U16 node, TraverseBundleCallback callback, void* pUserData) const
{
	if (!callback(node, pBitArray, pUserData))
		return false;

	if (level < m_numLevels-1)
	{
		U32F numBlocks = ExternalBitArray::DetermineNumBlocks(numAabbs);

		U64* pLeftStorage = NDI_NEW U64[numBlocks];
		ExternalBitArray leftBitArray(numAabbs, pLeftStorage);

		U64* pRightStorage = NDI_NEW U64[numBlocks];
		ExternalBitArray rightBitArray(numAabbs, pRightStorage);

		U32F splitAxis = (m_levelAxis >> (level * 2)) & 0x3;
		U32F splitAxisMask = 1 << splitAxis;
		v128 split = SMATH_VEC_MUL(SMATH_VEC_ADD(nodeMin, nodeMax), SMATH_VEC_REPLICATE_FLOAT(0.5f));

		bool left = false;
		bool right = false;
		for (U32F ii = pBitArray->FindFirstSetBit(); ii<numAabbs; ii = pBitArray->FindNextSetBit(ii))
		{
			if (SMATH_VEC_MOVEMASK(SMATH_VEC_CMPLT(pAabb[ii].m_min, split)) & splitAxisMask)
			{
				leftBitArray.SetBit(ii);
				left = true;
			}
			if (SMATH_VEC_MOVEMASK(SMATH_VEC_CMPGT(pAabb[ii].m_max, split)) & splitAxisMask)
			{
				rightBitArray.SetBit(ii);
				right = true;
			}
		}

		if (left)
		{
			v128 childMax = nodeMax;
			SMATH_VEC_SET_ELEMENT(childMax, splitAxis, split);
			if (!TraverseNodeBundle(pAabb, numAabbs, &leftBitArray, nodeMin, childMax, level+1, node+1, callback, pUserData))
				return false;
		}
		if (right)
		{
			v128 childMin = nodeMin;
			SMATH_VEC_SET_ELEMENT(childMin, splitAxis, split);
			if (!TraverseNodeBundle(pAabb, numAabbs, &rightBitArray, childMin, nodeMax, level+1, node + (1 << (m_numLevels - level - 1)), callback, pUserData))
				return false;
		}
	}

	return true;
}

void RopeColTree::TraverseBundle(const Aabb* pAabb, U32F numAabbs, TraverseBundleCallback callback, void* pUserData) const
{ 
	ScopedTempAllocator jjAlloc(FILE_LINE_FUNC);

	U64* pStorage = NDI_NEW U64[ExternalBitArray::DetermineNumBlocks(numAabbs)];
	ExternalBitArray bitArray(numAabbs, pStorage, true);

	TraverseNodeBundle(reinterpret_cast<const Aabbv128*>(pAabb), numAabbs, &bitArray, m_aabb.m_min.QuadwordValue(), m_aabb.m_max.QuadwordValue(), 0, 0, callback, pUserData); 
}

//class RopeColCollector : public hkpCdPointCollector
//{
//public:
//	RopeColCache* m_pCache;
//	U16 m_shapeIndex;
//	U32F m_pointIndex;
//	Point m_pnt;
//	Vector m_vec;
//	Scalar m_len;
//	Scalar m_radius;
//	virtual void addCdPoint( const hkpCdPoint& point ) override;
//	void addTri(const hkpShape* pShape, hkpShapeKey key, const Locator& loc, const Locator* pBackStepLoc, const Aabb& aabb);
//};

void RopeColCache::Init(U32F maxPoints, F32 ropeLen)
{
	m_maxShapes = maxPoints ? 255 : 0;
	m_maxTris = maxPoints * 16;
	m_maxEdges = maxPoints * 16;

	m_pShapes = NDI_NEW RopeColliderHandle[m_maxShapes];
	m_pTris = NDI_NEW RopeColTri[m_maxTris];
	m_pEdges = NDI_NEW RopeColEdge[m_maxEdges];

	m_tree.Init(1.3f * ropeLen); // 30% extra for stretching

	U32F maxNumTreeNodes = m_tree.GetMaxNumNodes();
	m_pTriTreeNodes	= NDI_NEW RopeColTreeNode[maxNumTreeNodes];
	m_pEdgeTreeNodes = NDI_NEW RopeColTreeNode[maxNumTreeNodes];

	m_pTreeBuilderAllocBuffer = NDI_NEW U8[sizeof(ScopedTempAllocator)];

	Reset();
}

void RopeColCache::Reset()
{
	m_numShapes = 0;
	m_numTris = 0;
	m_numEdges = 0;
	m_numEdgesBeforeTrimming = 0;
	m_overflow = false;
	m_tree.Reset();
}

void RopeColCache::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pShapes, delta, lowerBound, upperBound);
	RelocatePointer(m_pTris, delta, lowerBound, upperBound);
	RelocatePointer(m_pEdges, delta, lowerBound, upperBound);
	RelocatePointer(m_pTriTreeNodes, delta, lowerBound, upperBound);
	RelocatePointer(m_pEdgeTreeNodes, delta, lowerBound, upperBound);
	RelocatePointer(m_pTreeBuilderAllocBuffer, delta, lowerBound, upperBound);
}

void RopeColCache::StartBuild(const Aabb& aabb)
{
	m_tree.CreateTree(aabb, 1.0f);

	m_pTreeBuilderAlloc = new(m_pTreeBuilderAllocBuffer) ScopedTempAllocator(FILE_LINE_FUNC);

	U32F scratchMem = m_pTreeBuilderAlloc->GetFreeSize();
	U32 translationTableSize = m_maxTris * sizeof(U16) + 16;
	scratchMem -= translationTableSize;
	char* pScratchMem = NDI_NEW char[scratchMem];

	m_triTreeBuilder.Init(&m_tree, m_maxTris, sizeof(RopeColTri), pScratchMem, scratchMem);
	m_edgeTreeBuilder.Init(&m_tree, m_maxEdges, sizeof(RopeColEdge), pScratchMem, scratchMem);
}

void RopeColCache::EndBuild()
{
	PROFILE(Rope, CompileTree);

	U16* pTriTranslation = NDI_NEW U16[m_triTreeBuilder.m_numElements];

	m_triTreeBuilder.Compile((char*)m_pTris, m_numTris, m_pTriTreeNodes, pTriTranslation);

	RopeColEdge* pEdges = (RopeColEdge*)m_edgeTreeBuilder.m_pElements;
	for (U32F ii = 0; ii<m_edgeTreeBuilder.m_numElements; ii++)
	{
		pEdges[ii].m_triIndex = pTriTranslation[pEdges[ii].m_triIndex];
		ASSERT(pEdges[ii].m_triIndex >= 0 && pEdges[ii].m_triIndex < m_numTris);
	}

	m_edgeTreeBuilder.Compile((char*)m_pEdges, m_numEdges, m_pEdgeTreeNodes);
	m_numEdgesBeforeTrimming = m_numEdges;

	delete m_pTreeBuilderAlloc;
	m_pTreeBuilderAlloc = nullptr;

	m_overflow = m_triTreeBuilder.m_overflow || m_edgeTreeBuilder.m_overflow;
}

I32F RopeColCache::AddShape(const RopeColliderHandle& shape)
{
	if (m_numShapes == m_maxShapes)
	{
		m_overflow = true;
		return -1;
	}

	m_pShapes[m_numShapes] = shape;
	m_numShapes++;
	return m_numShapes-1;
}

I32F RopeColCache::AddTri(const RopeColTri& tri)
{
	if (m_numTris == m_maxTris)
	{
		m_overflow = true;
		return -1;
	}

	m_pTris[m_numTris] = tri;
	m_numTris++;
	return m_numTris-1;
}

I32F RopeColCache::AddEdge(const RopeColEdge& edge)
{
	if (m_numEdges == m_maxEdges)
	{
		m_overflow = true;
		return -1;
	}

	m_pEdges[m_numEdges] = edge;
	m_numEdges++;
	return m_numEdges-1;
}


I32F RopeColCache::AddTriBuild(const RopeColTri& tri, const Aabb& aabb)
{
	return m_triTreeBuilder.AddElement((char*)&tri, aabb);
}

I32F RopeColCache::AddEdgeBuild(const RopeColEdge& edge)
{
	Aabb aabb((NoOpConstructor()));
	Point endPnt = edge.m_pnt + Vector(edge.m_vec) * edge.m_vec.W();
	aabb.m_min = Min(edge.m_pnt, endPnt);
	aabb.m_max = Max(edge.m_pnt, endPnt);
	return m_edgeTreeBuilder.AddElement((char*)&edge, aabb);
}

struct TreeTraverseData
{
	const RopeColTreeNode* m_pTreeNodes;
	ExternalBitArray* m_pCandidates;
};

bool TreeTraverseCallback(U16 index, void* pUserData)
{
	TreeTraverseData* pData = reinterpret_cast<TreeTraverseData*>(pUserData);
	const RopeColTreeNode& treeNode = pData->m_pTreeNodes[index];
	if (treeNode.m_numElems)
	{
		pData->m_pCandidates->SetBitRange(treeNode.m_elemIndex, treeNode.m_elemIndex+treeNode.m_numElems-1);
	}
	return true;
}

void RopeColCache::FindCandidateTris(const Aabb& aabb, ExternalBitArray* pCandidateTris) const
{
	if (m_numTris == 0 || m_tree.m_numLevels == 0)
		return;

	TreeTraverseData data;
	data.m_pTreeNodes = m_pTriTreeNodes;
	data.m_pCandidates = pCandidateTris;
	m_tree.Traverse(aabb, TreeTraverseCallback, &data);
}

void RopeColCache::FindCandidateEdges(const Aabb& aabb, ExternalBitArray* pCandidateEdges) const
{
	if (m_numEdges == 0 || m_tree.m_numLevels == 0)
		return;

	TreeTraverseData data;
	data.m_pTreeNodes = m_pEdgeTreeNodes;
	data.m_pCandidates = pCandidateEdges;
	m_tree.Traverse(aabb, TreeTraverseCallback, &data);
}

struct TrimHits
{
	U16 m_triIndex;
	F32 m_t;
	F32 m_dt;
};

I32 CompareTrimHits(TrimHits& a, TrimHits& b)
{
	return a.m_t < b.m_t ? -1 : 1;
}

bool RopeColCache::CheckEdgeTrims(U16 edgeIndex, const Rope2* pOwner)
{
	ASSERT(edgeIndex < m_numEdges);
	RopeColEdge& edge = m_pEdges[edgeIndex];
	if (edge.m_startTriIndex != -2)
		// Already has been checked
		return false;

	ScopedTempAllocator allocJj(FILE_LINE_FUNC);

	I16 edgeTri = edge.m_triIndex;

	Point pnt = edge.m_pnt;
	Vector vec(edge.m_vec);
	Scalar len = edge.m_vec.W();
	Point endPnt = pnt + vec * len;

	const U32F kMaxHits = 48;
	TrimHits trimHits[kMaxHits];
	U32F numHits = 0;

	{
		U64* pCandidateTrisData = NDI_NEW U64[ExternalBitArray::DetermineNumBlocks(m_numTris)];
		ExternalBitArray candidateTris(m_numTris, pCandidateTrisData);

		{
			Aabb aabb((NoOpConstructor()));
			aabb.m_min = Min(pnt, endPnt);
			aabb.m_max = Max(pnt, endPnt);

			FindCandidateTris(aabb, &candidateTris);
		}

		Scalar minLenForEndPntCheck = Min(len, Scalar(1.0f)); // if edge is very long the hit may be at the end point by the numerical imprecision multiplied by long edge can give a higher distance

		hkcdRay ray;
		ray.m_origin = hkVector4(pnt.QuadwordValue());
		ray.m_direction = hkVector4((vec * len).QuadwordValue());
		const hkSimdReal tolerance = hkSimdReal(0.0001f);

		for (U64 triIndex = candidateTris.FindFirstSetBit(); triIndex < m_numTris; triIndex = candidateTris.FindNextSetBit(triIndex))
		{
			if (triIndex == edgeTri)
				// My own triangle
				continue;

			const RopeColTri& tri = m_pTris[triIndex];

			bool hit[2];
			hkSimdReal fraction[2];
			hkVector4 normal[2];
			if (tri.m_key != hknpShapeKey::invalid())
			{
				// Triangle
				fraction[0] = hkSimdReal(2.0f);
				hit[0] = hkcdSegmentTriangleIntersectND(ray, hkVector4(tri.m_pnt[0].QuadwordValue()), hkVector4(tri.m_pnt[1].QuadwordValue()), hkVector4(tri.m_pnt[2].QuadwordValue()), 
															tolerance, normal[0], fraction[0]);
				hit[1] = false;
			}
			else
			{
				// This whole feature of edges trimmed by convex shapes was all broken (missing RB xfm)
				// Rather than fixing it right before ship, feels safer to just disable it
#ifdef ENABLE_AFTER_T2_SHIPS
				// Convex shape
				const RopeColliderHandle& shape = m_pShapes[tri.m_shapeIndex];
				// PHYSICS_ASSERT(shape.IsRigidBody()); // what would custom shape be doing in col cache?
				if (!shape.IsValid())
				{
					// Body has gone away since it was inserted in the colCache
					continue;
				}

				const hknpShape* pShape;
				{
					HavokMarkForReadJanitor jj;
					pShape = shape.GetShape(pOwner);
				}

				Locator loc = shape.GetLocator(pOwner);
				hkTransform shapeXfm = HkTransformFromLocator(loc);

				if (shape.GetListIndex() >= 0)
				{
					PHYSICS_ASSERT(pShape->getType() == hknpShapeType::COMPOUND);
					const hknpShapeInstance& inst = static_cast<const hknpCompoundShape*>(pShape)->getInstance(hknpShapeInstanceId(shape.GetListIndex()));
					pShape = inst.getShape();
					shapeXfm.setMul(shapeXfm, inst.getTransform());
				}

				hkVector4 origin;
				hkVector4 direction;
				origin.setTransformedInversePos(shapeXfm, ray.m_origin);
				direction.setRotatedInverseDir(shapeXfm.getRotation(), ray.m_direction);

				hknpRayCastQuery query;
				query.m_ray.setOriginDirection(origin, direction);
					
				hknpInplaceTriangleShape targetTriangle( 0.0f );
				hknpCollisionQueryContext queryContext( nullptr, targetTriangle.getTriangleShape() );

				hknpQueryFilterData filterData;
				HavokClosestHitCollector collector;

				hknpShapeQueryInfo queryInfo;
				queryInfo.m_rootShape = pShape;
				queryInfo.m_shapeToWorld = &shapeXfm;

				pShape->castRayImpl( &queryContext, query, filterData, queryInfo, &collector);
				hit[0] = collector.hasHit();
				if (hit[0])
				{
					normal[0] = collector.getHit().m_normal;
					fraction[0] = collector.getHit().m_fraction;
				}

				// On convex shapes we need to cast ray in opposite direction also
				origin.add(direction);
				direction.setNeg4(direction);
				query.m_ray.setOriginDirection(origin, direction);

				pShape->castRayImpl( &queryContext, query, filterData, queryInfo, &collector);
				hit[1] = collector.hasHit();
				if (hit[1])
				{
					normal[1] = collector.getHit().m_normal;
					fraction[1] = collector.getHit().m_fraction;
				}
#else
				hit[0] = false;
				hit[1] = false;
#endif
			}

			for (U32F ii = 0; ii<2; ii++)
			{
				if (!hit[ii])
					continue;

				if (fraction[ii] * minLenForEndPntCheck < 0.0001f || fraction[ii] * minLenForEndPntCheck > minLenForEndPntCheck - 0.0001f)
				{
					// Hitting too close to the end points of the edge
					// @@JS: This is not good enough, we can penetrate through if a collision plane clips through the start/end point of 2 connecting edges
					continue;
				}

				F32 dt = Dot(Vector(normal[ii].getQuad()), vec); 
				if (Abs(dt) < Scalar(0.001f))
				{
					// Normal almost parallel to the edge
					continue;
				}

				// Check that the triangle we hit does not have outer edges that are crossing with our edge
				// In that case this is not a collision face trim
				if (tri.m_outerEdgesBits)
				{
					Point hitPnt = pnt + (F32)fraction[ii] * Vector(ray.m_direction.getQuad());
					bool bOuterEdge = false;
					for(U32F kk=0; kk<3; ++kk)
					{	
						if ((tri.m_outerEdgesBits >> kk) & 1)
						{
							// This is outer edge
							Point edgePnt = tri.m_pnt[kk];
							Vector edgeVec = Normalize(tri.m_pnt[(kk+1)%3] - edgePnt);
							if (DistSqr(hitPnt, edgePnt + Dot(hitPnt-edgePnt, edgeVec) * edgeVec) < kScEdgeTol2)
							{
								bOuterEdge = true;
								break;
							}
						}
					}
					if (bOuterEdge)
						continue;
				}

				ASSERT(numHits < kMaxHits);
				if (numHits >= kMaxHits)
					break;

				trimHits[numHits].m_triIndex = triIndex;
				trimHits[numHits].m_t = fraction[ii];
				trimHits[numHits].m_dt = dt;
				numHits++;
			}
		}
	}

	if (numHits == 0)
		return false;

	QuickSortStack(trimHits, numHits, CompareTrimHits);

	// Remove trims that would produce invalid edges shorter than tolerance (overlapping collision kind of stuff)
	{
		U32 ii = 1;
		while (ii < numHits)
		{
			F32 dt = trimHits[ii].m_t - trimHits[ii-1].m_t;
			if (dt * len <= kEdgeTol)
			{
				if (trimHits[ii-1].m_dt * trimHits[ii].m_dt > 0.0f)
				{
					U32 iRemove = trimHits[ii].m_dt > 0.0f ? ii-1 : ii;
					memmove(trimHits+iRemove, trimHits+iRemove+1, (numHits-iRemove-1)*sizeof(trimHits[0]));
					numHits--;
				}
				else
				{
					// In and out of collision within a tolerance distance, ignore this "cut"
					U32 iRemove = ii-1;
					memmove(trimHits+iRemove, trimHits+iRemove+2, (numHits-iRemove-2)*sizeof(trimHits[0]));
					numHits -= 2;
				}
			}
			else
			{
				ii++;
			}
		}
	}

	bool oldReplaced = false;
	Scalar trimStartT(kZero);
	I16 trimStartTri = -1;
	U16 numExtraTrims = 0;
	U16 secondTrimIndex = 0;
	for (U32F ii = 0; ii<numHits; ii++)
	{
		if (trimHits[ii].m_dt > 0.0f)
		{
			trimStartT = trimHits[ii].m_t;
			trimStartTri = trimHits[ii].m_triIndex;
		}
		else if (!oldReplaced || trimStartTri >= 0)
		{
			if (!oldReplaced)
			{
				edge.m_pnt = pnt + trimStartT * len * vec;
				edge.m_vec.SetW((trimHits[ii].m_t - trimStartT) * len);
				edge.m_startTriIndex = trimStartTri;
				edge.m_endTriIndex = trimHits[ii].m_triIndex;
				oldReplaced = true;
			}
			else
			{
				RopeColEdge newEdge = edge;
				newEdge.m_pnt = pnt + trimStartT * len * vec;
				newEdge.m_vec.SetW((trimHits[ii].m_t - trimStartT) * len);
				newEdge.m_startTriIndex = trimStartTri;
				newEdge.m_endTriIndex = trimHits[ii].m_triIndex;
				I32F newEdgeIndex = AddEdge(newEdge);
				if (newEdgeIndex < 0)
					break; // out of space
				if (numExtraTrims == 0)
					secondTrimIndex = newEdgeIndex;
				numExtraTrims++;
			}
			trimStartTri = -1;
		}
	}

	if (trimStartTri >= 0)
	{
		if (!oldReplaced)
		{
			edge.m_pnt = pnt + trimStartT * len * vec;
			edge.m_vec.SetW((Scalar(1.0f) - trimStartT) * len);
			edge.m_startTriIndex = trimStartTri;
			edge.m_endTriIndex = -1;
			oldReplaced = true;
		}
		else
		{
			RopeColEdge newEdge = edge;
			newEdge.m_pnt = pnt + trimStartT * len * vec;
			newEdge.m_vec.SetW((Scalar(1.0f) - trimStartT) * len);
			newEdge.m_startTriIndex = trimStartTri;
			newEdge.m_endTriIndex = -1;
			I32F newEdgeIndex = AddEdge(newEdge);
			if (newEdgeIndex >= 0)
			{
				if (numExtraTrims == 0)
					secondTrimIndex = newEdgeIndex;
				numExtraTrims++;
			}
		}
	}

	if (!oldReplaced)
	{
		edge.m_startTriIndex = -1;
		edge.m_endTriIndex = -1;
	}
	else
	{
		edge.m_numExtraTrims = numExtraTrims;
		edge.m_secondTrimIndex = secondTrimIndex;
	}

	return oldReplaced;
}

U16 RopeColCache::FitPointToTrimmedEdge(U16 edgeIndex, Point_arg pos, const RopeColTriId& startTriId, const RopeColTriId& endTriId, Point& posOut)
{
	const RopeColEdge& edge0 = m_pEdges[edgeIndex];
	U16 startIndex = edgeIndex;
	if (startTriId.m_shape.IsValid())
	{
		bool found = false;
		if (edge0.m_startTriIndex >= 0)
		{
			const RopeColTri& tri = m_pTris[edge0.m_startTriIndex];
			if (startTriId.m_triKey == tri.m_key && startTriId.m_shape == m_pShapes[tri.m_shapeIndex])
			{
				found = true;
			}
		}
		if (!found)
		{
			for (U32F ii = 0; ii<edge0.m_numExtraTrims; ii++)
			{
				RopeColEdge& edge = m_pEdges[edge0.m_secondTrimIndex+ii];
				const RopeColTri& tri = m_pTris[edge.m_startTriIndex];
				ASSERT(edge.m_startTriIndex >= 0);
				if (startTriId.m_triKey == tri.m_key && startTriId.m_shape == m_pShapes[tri.m_shapeIndex])
				{
					startIndex = edge0.m_secondTrimIndex+ii;
					break;
				}
			}
		}
	}

	U16 endIndex = edge0.m_numExtraTrims > 0 ? edge0.m_secondTrimIndex + edge0.m_numExtraTrims - 1 : edgeIndex;
	if (endTriId.m_shape.IsValid())
	{
		bool found = false;
		if (edge0.m_endTriIndex >= 0)
		{
			const RopeColTri& tri = m_pTris[edge0.m_endTriIndex];
			if (endTriId.m_triKey == tri.m_key && endTriId.m_shape == m_pShapes[tri.m_shapeIndex])
			{
				found = true;
			}
		}
		if (!found)
		{
			for (U32F ii = 0; ii<edge0.m_numExtraTrims; ii++)
			{
				RopeColEdge& edge = m_pEdges[edge0.m_secondTrimIndex+ii];
				const RopeColTri& tri = m_pTris[edge.m_endTriIndex];
				if (edge.m_endTriIndex >= 0)
				{
					if (endTriId.m_triKey == tri.m_key && endTriId.m_shape == m_pShapes[tri.m_shapeIndex])
					{
						endIndex = edge0.m_secondTrimIndex+ii;
						break;
					}
				}
			}
		}
	}

	JAROS_ALWAYS_ASSERT(startIndex <= endIndex);
	if (startIndex > endIndex)
	{
		endIndex = startIndex;;
	}

	U32F index = startIndex;
	U32F bestIndex = index;
	Scalar bestDist(FLT_MAX);
	posOut = pos;
	do 
	{
		RopeColEdge& edge = m_pEdges[index];
		Scalar t = Dot(pos - edge.m_pnt, Vector(edge.m_vec));
		Scalar dist;
		Point thisPos;
		if (t < 0.0f)
		{
			dist = -t;
			thisPos = edge.m_pnt;
		}
		else if (t > edge.m_vec.W())
		{
			dist = t - edge.m_vec.W();
			thisPos = edge.m_pnt + t * Vector(edge.m_vec);
		}
		else
		{
			bestDist = 0.0f;
			bestIndex = index;
			posOut = pos;
			break;
		}
		if (dist < bestDist)
		{
			bestDist = dist;
			posOut = thisPos;
			bestIndex = index;
		}
		index++;
		if (index < edge0.m_secondTrimIndex)
			index = edge0.m_secondTrimIndex;
	} while (index <= endIndex);

	return bestIndex;
}

void RopeColCache::GetTriId(U16 triIndex, RopeColTriId& id) const
{
	ASSERT(triIndex<m_numTris);
	const RopeColTri& tri = m_pTris[triIndex];
	const RopeColliderHandle& shape = m_pShapes[tri.m_shapeIndex];
	id.m_shape = shape;
	id.m_triKey = tri.m_key;
}

void RopeColCache::GetEdgeId(U16 edgeIndex, RopeColEdgeId& id) const
{
	ASSERT(edgeIndex<m_numEdges);
	const RopeColEdge& edge = m_pEdges[edgeIndex];
	const RopeColTri& tri = m_pTris[edge.m_triIndex];
	const RopeColliderHandle& shape = m_pShapes[tri.m_shapeIndex];
	id.m_shape = shape;
	id.m_triKey = tri.m_key;
	id.m_edgeIndex = edge.m_edgeIndex;
	if (edge.m_startTriIndex >= 0)
		GetTriId(edge.m_startTriIndex, id.m_startTriId);
	if (edge.m_endTriIndex >= 0)
		GetTriId(edge.m_endTriIndex, id.m_endTriId);
}

I16 RopeColCache::FindEdgeIndex(const RopeColEdgeId& id, const Rope2* pOwner)
{
	for (U32F iShape = 0; iShape<m_numShapes; iShape++)
	{
		if (m_pShapes[iShape] == id.m_shape)
		{
			for (U32F iTri = 0; iTri < m_numTris; iTri++)
			{
				if (m_pTris[iTri].m_key == id.m_triKey && m_pTris[iTri].m_shapeIndex == iShape)
				{
					for (U32F iEdge = 0; iEdge<m_numEdges; iEdge++)
					{
						if (m_pEdges[iEdge].m_triIndex == iTri && m_pEdges[iEdge].m_edgeIndex == id.m_edgeIndex)
						{
							// We found the edge. Now check for trims
							if (m_pEdges[iEdge].m_startTriIndex == -2)
							{
								// Not trimmed yet, trim it
								CheckEdgeTrims(iEdge, pOwner);
							}
							return iEdge;
						}
					}
					return -1;
				}
			}
			return -1;
		}
	}
	return -1;
}

struct FindTriTraverseData
{
	const RopeColTreeNode* m_pTreeNodes;
	const RopeColTri* m_pTris;
	hknpShapeKey m_triKey;
	U16 m_shapeIndex;
	I16 m_triIndexOut;
};

bool FindTriTraverseCallback(U16 index, void* pUserData)
{
	FindTriTraverseData* pData = reinterpret_cast<FindTriTraverseData*>(pUserData);
	const RopeColTreeNode& treeNode = pData->m_pTreeNodes[index];
	for (U16 ii = 0; ii<treeNode.m_numElems; ii++)
	{
		const RopeColTri& tri = pData->m_pTris[treeNode.m_elemIndex+ii];
		if (tri.m_key == pData->m_triKey && tri.m_shapeIndex == pData->m_shapeIndex)
		{
			pData->m_triIndexOut = treeNode.m_elemIndex+ii;
			return false;
		}
	}
	return true;
}

struct FindEdgeTraverseData
{
	const RopeColTreeNode* m_pTreeNodes;
	const RopeColEdge* m_pEdges;
	U16 m_triIndex;
	U16 m_edgeIndex;
	I16 m_edgeIndexOut;
};

bool FindEdgeTraverseCallback(U16 index, void* pUserData)
{
	FindEdgeTraverseData* pData = reinterpret_cast<FindEdgeTraverseData*>(pUserData);
	const RopeColTreeNode& treeNode = pData->m_pTreeNodes[index];
	for (U16 ii = 0; ii<treeNode.m_numElems; ii++)
	{
		const RopeColEdge& edge = pData->m_pEdges[treeNode.m_elemIndex+ii];
		if (edge.m_triIndex == pData->m_triIndex && edge.m_edgeIndex == pData->m_edgeIndex)
		{
			pData->m_edgeIndexOut = treeNode.m_elemIndex+ii;
			return false;
		}
	}
	return true;
}

I16 RopeColCache::FindEdgeIndex(const RopeColEdgeId& id, Point_arg pos, Point& pos1Out, const Rope2* pOwner)
{
	U32F iShape;
	for (iShape = 0; iShape<m_numShapes; iShape++)
	{
		if (m_pShapes[iShape] == id.m_shape)
			break;
	}
	if (iShape == m_numShapes)
		return -1;

	Locator shapeLoc = m_pShapes[iShape].GetLocator(pOwner);
	Locator shapeLocPrev = m_pShapes[iShape].GetPrevLocator(pOwner);
	pos1Out = shapeLoc.TransformPoint(shapeLocPrev.UntransformPoint(pos));

	Vector vecPadding = 2.0f * Vector(kScEdgeTol);
	Aabb aabb(pos1Out - vecPadding, pos1Out + vecPadding);

	FindTriTraverseData findTriData;
	findTriData.m_pTreeNodes = m_pTriTreeNodes;
	findTriData.m_pTris = m_pTris;
	findTriData.m_triKey = id.m_triKey;
	findTriData.m_shapeIndex = iShape;
	findTriData.m_triIndexOut = -1;
	m_tree.Traverse(aabb, FindTriTraverseCallback, &findTriData);

	if (findTriData.m_triIndexOut < 0)
		return -1;

	FindEdgeTraverseData findEdgeData;
	findEdgeData.m_pTreeNodes = m_pEdgeTreeNodes;
	findEdgeData.m_pEdges = m_pEdges;
	findEdgeData.m_triIndex = findTriData.m_triIndexOut;
	findEdgeData.m_edgeIndex = id.m_edgeIndex;
	findEdgeData.m_edgeIndexOut = -1;
	m_tree.Traverse(aabb, FindEdgeTraverseCallback, &findEdgeData);

	I16 iEdge = findEdgeData.m_edgeIndexOut;
	if (iEdge < 0)
		return -1;

	// We found the edge. Now check for trims
	if (m_pEdges[iEdge].m_startTriIndex == -2)
	{
		// Not trimmed yet, trim it
		CheckEdgeTrims(iEdge, pOwner);
	}
	return iEdge;
}

void Rope2::AddShapeEdgeCol(Point_arg p0, Point_arg p1, Vector_arg norm, I32F triIndex, U16 edgeIndex, const Aabb& aabb)
{
	Scalar edgeLen;
	Vector edgeVec = Normalize(p1 - p0, edgeLen);

	// Check edge is in the aabb
	Vector vecEps(FLT_EPSILON, FLT_EPSILON, FLT_EPSILON);
	Vector edgeVecNonZero(SMATH_VEC_SEL(edgeVec.QuadwordValue(), vecEps.QuadwordValue(), SMATH_VEC_CMPEQ(edgeVec.QuadwordValue(), SMATH_VEC_SET_ZERO())));
	Vector edgeVecRecip = Recip(edgeVecNonZero);
	Vector tBoxMin = (aabb.m_min - p0) * edgeVecRecip;
	Vector tBoxMax = (aabb.m_max - p0) * edgeVecRecip;
	Vector tMin = Min(tBoxMin, tBoxMax);
	Vector tMax = Max(tBoxMin, tBoxMax);
	Scalar ttMin = MaxComp(tMin);
	Scalar ttMax = MinComp(tMax);
	if (ttMin <= ttMax && !IsNegative(ttMax) && ttMin <= edgeLen)
	{
		RopeColEdge edge;
		edge.m_vec = edgeVec.GetVec4();
		edge.m_vec.SetW(edgeLen);
		edge.m_pnt = p0;
		edge.m_normal = norm;
		edge.m_triIndex = triIndex;
		edge.m_edgeIndex = edgeIndex;
		m_colCache.AddEdgeBuild(edge);
	}
}

void Rope2::AddShapeDoubleEdgeCol(Point_arg p0, Point_arg p1, Vector_arg norm0, Vector_arg norm1, I32F triIndex, U16 edgeIndex, const Aabb& aabb)
{
	Scalar edgeLen;
	Vector edgeVec = Normalize(p1 - p0, edgeLen);

	// Check edge is in the aabb
	Vector vecEps(FLT_EPSILON, FLT_EPSILON, FLT_EPSILON);
	Vector edgeVecNonZero(SMATH_VEC_SEL(edgeVec.QuadwordValue(), vecEps.QuadwordValue(), SMATH_VEC_CMPEQ(edgeVec.QuadwordValue(), SMATH_VEC_SET_ZERO())));
	Vector edgeVecRecip = Recip(edgeVecNonZero);
	Vector tBoxMin = (aabb.m_min - p0) * edgeVecRecip;
	Vector tBoxMax = (aabb.m_max - p0) * edgeVecRecip;
	Vector tMin = Min(tBoxMin, tBoxMax);
	Vector tMax = Max(tBoxMin, tBoxMax);
	Scalar ttMin = MaxComp(tMin);
	Scalar ttMax = MinComp(tMax);
	if (ttMin <= ttMax && !IsNegative(ttMax) && ttMin <= edgeLen)
	{
		RopeColEdge edge;
		edge.m_vec = edgeVec.GetVec4();
		edge.m_vec.SetW(edgeLen);
		edge.m_pnt = p0;
		edge.m_normal = norm0;
		edge.m_triIndex = triIndex;
		edge.m_edgeIndex = edgeIndex;
		m_colCache.AddEdgeBuild(edge);

		// Now the opposite edge
		edge.m_vec = -edgeVec.GetVec4();
		edge.m_vec.SetW(edgeLen);
		edge.m_pnt = p1;
		edge.m_normal = norm1;
		edge.m_triIndex = triIndex;
		edge.m_edgeIndex = edgeIndex+1;
		m_colCache.AddEdgeBuild(edge);
	}
}

void Rope2::AddShapeEdges(U32F numVertices, const Point* pVertices, U32F numFaces, const Vector* pFaces, const U32F* pNumFaceVertices, const U16* pVertexIndices, 
	const Transform& xfm, const Transform& xfmWithScale, U32F iTri, const Aabb& aabb)
{
	Point* pVtxWs = STACK_ALLOC_ALIGNED(Point, numVertices, kAlign16);
	ALWAYS_ASSERT(pVtxWs);
	for (U32F ii = 0; ii<numVertices; ii++)
	{
		pVtxWs[ii] = pVertices[ii] * xfmWithScale;
	}

	U32F vtxIndex = 0;
	U32F iEdge = 0;
	for (U32F ii = 0; ii<numFaces; ii++)
	{
		Vector norm = pFaces[ii] * xfm;
		U32F numVtx = pNumFaceVertices[ii];
		if (numVtx > 2)
		{
			for (U32F iVtx = 0; iVtx<numVtx-1; iVtx++)
			{
				AddShapeEdgeCol(pVtxWs[pVertexIndices[vtxIndex+iVtx]], pVtxWs[pVertexIndices[vtxIndex+iVtx+1]], norm, iTri, iEdge, aabb);
				iEdge++;
			}
			AddShapeEdgeCol(pVtxWs[pVertexIndices[vtxIndex+numVtx-1]], pVtxWs[pVertexIndices[vtxIndex]], norm, iTri, iEdge, aabb);
			iEdge++;
		}
		vtxIndex += numVtx;
	}
}

void RopeColCache::DebugDrawTri(U32F iTri)
{
#if FINAL_BUILD && !ALLOW_ROPE_DEBUGGER_IN_FINAL
	return;
#endif

	DebugPrimTime tt = kPrimDuration1Frame; //kPrimDuration1FramePauseable

	//const RenderCamera& cam = GetRenderCamera(0);
	//const Vector camDir = Vector(Vec4(kUnitZAxis) * cam.m_mtx.m_viewToWorld);
	//Vector offset = -0.0005f * camDir;

	RopeColTri& tri = m_pTris[iTri];
	DebugDrawLine(tri.m_pnt[0], tri.m_pnt[1], kColorWhite, tt);
	DebugDrawLine(tri.m_pnt[1], tri.m_pnt[2], kColorWhite, tt);
	DebugDrawLine(tri.m_pnt[2], tri.m_pnt[0], kColorWhite, tt);
	Point avg = AveragePos(tri.m_pnt[0], tri.m_pnt[1], tri.m_pnt[2]);
	Vector normal = SafeNormalize(Cross(tri.m_pnt[1]-tri.m_pnt[0], tri.m_pnt[2]-tri.m_pnt[0]), kZero);
	DebugDrawLine(avg, avg + 0.25f*normal, kColorWhite, tt);

	if (g_ropeMgr.m_debugDrawColCacheTriIndex)
	{
		char buf[5];
		snprintf(buf, 5, "%i", iTri);
		DebugDrawString(avg + 0.1f*normal, buf, kColorWhite, tt);
	}
}

void RopeColCache::DebugDrawEdge(U32F iEdge, bool active, I8 positive)
{
#if FINAL_BUILD && !ALLOW_ROPE_DEBUGGER_IN_FINAL
	return;
#endif

	DebugPrimTime tt = kPrimDuration1Frame; //kPrimDuration1FramePauseable

	const RenderCamera& cam = GetRenderCamera(0);
	const Vector camDir = Vector(Vec4(kUnitZAxis) * cam.m_mtx.m_viewToWorld);
	Vector offset = -0.001f * camDir;

	Color blue = active ? kColorBlue : Color(0.0f, 0.0f, 0.25f);
	Color white = active ? kColorWhite : Color(0.25f, 0.25f, 0.25f);
	Color yellow = active ? kColorYellow : Color(1.f, 1.f, 0.f, 0.2f);

	RopeColEdge& edge = m_pEdges[iEdge];
	DebugDrawSphere(edge.m_pnt+offset, 0.005f, blue, tt);
	Point endPt = edge.m_pnt + Vector(edge.m_vec) * edge.m_vec.W();
	DebugDrawLine(edge.m_pnt+offset, endPt+offset, blue, tt);
	const Vector edgeVec(edge.m_vec);

	if (g_ropeMgr.m_debugDrawColCacheEdgeNorm)
	{
		DebugDrawLine(edge.m_pnt, edge.m_pnt + 0.4f * edge.m_normal, white, tt);
		if (g_ropeMgr.m_debugDrawColCacheEdgeIndex)
			DebugDrawString(edge.m_pnt + 0.4f * edge.m_normal, StringBuilder<64>("%i", iEdge).c_str(), white, tt, nullptr, 0.4f);
	}

	if (g_ropeMgr.m_debugDrawColCacheEdgeBiNorm)
	{
		const Vector edgeBinormal = Cross(edgeVec, edge.m_normal);
		DebugDrawLine(edge.m_pnt, edge.m_pnt + 0.4f * edgeBinormal, yellow);
		if (g_ropeMgr.m_debugDrawColCacheEdgeIndex)
			DebugDrawString(edge.m_pnt + 0.4f * edgeBinormal, StringBuilder<64>("%i", iEdge).c_str(), yellow, tt, nullptr, 0.4f);
	}

	char buf[6];
	U8 bufPos = 0;
	if (g_ropeMgr.m_debugDrawColCacheEdgeIndex)
	{
		bufPos += snprintf(buf+bufPos, 6-bufPos, "%i", iEdge);
	}
	if (g_ropeMgr.m_debugDrawEdgePositivness && positive >= 0)
	{
		buf[bufPos] = positive ? '+' : '-';
		bufPos++;
		buf[bufPos] = 0;
	}
	if (bufPos)
		DebugDrawString(edge.m_pnt + 0.1f*Vector(edge.m_vec), buf, white, tt, nullptr, 0.7f);
}

void RopeColCache::DebugDraw(const Rope2* pOwner)
{
#if FINAL_BUILD && !ALLOW_ROPE_DEBUGGER_IN_FINAL
	return;
#endif

	if (g_ropeMgr.m_debugDrawColCacheTris)
	{
		for (U32F iTri = 0; iTri<m_numTris; iTri++)
		{
			DebugDrawTri(iTri);
		}
	}

	if (g_ropeMgr.m_debugDrawColCacheEdges)
	{
		for (U32F iEdge = 0; iEdge<m_numEdges; iEdge++)
		{
			DebugDrawEdge(iEdge);
		}
	}

	if (g_ropeMgr.m_debugDrawColCacheColliders)
	{
		U32F iShape = 0;
		while (iShape < m_numShapes)
		{
			RopeCollider colliderBuffer;
			const RopeCollider* pCollider = m_pShapes[iShape].GetCollider(&colliderBuffer, pOwner);
			HavokMeshDrawData** ppDebugDrawData = nullptr;
			const hknpShape* pShape = pCollider->m_pShape;
			if (m_pShapes[iShape].GetListIndex() == -1)
			{
				if (RigidBody* pRigidBody = const_cast<RigidBody*>(m_pShapes[iShape].GetRigidBody()))
				{
					if (pRigidBody->GetBackgroundData())
					{
						ppDebugDrawData = &const_cast<HavokBackgroundData*>(pRigidBody->GetBackgroundData())->m_pMeshDrawData;
					}
					else if (pRigidBody->GetProtoBody())
					{
						ppDebugDrawData = &const_cast<HavokProtoBody*>(pRigidBody->GetProtoBody())->m_pMeshDrawData;
					}
				}
			}
			if (pShape)
			{
				HavokDebugDrawShape(pShape, pCollider->m_loc, kColorBlue, CollisionDebugDrawConfig::MenuOptions(), ppDebugDrawData);
			}
			iShape++;
		}
	}
}

void Rope2::AddTriEdgeCol( const hknpShape* pShape, hknpShapeKey key, const Locator& loc, U32F iShape, const Aabb& aabb, const hknpCompressedMeshShape* pMesh, const ExternalBitArray* pOuterEdges)
{
	if (pShape->getType() != hknpShapeType::TRIANGLE)
	{
		ASSERT(false);
		return;
	}
	const hknpTriangleShape* pHkTri = static_cast<const hknpTriangleShape*>(pShape);
	RopeColTri tri;
	tri.m_shapeIndex = iShape;
	tri.m_key = key;
	Vector normal;
	tri.m_pnt[0] = loc.TransformPoint(Point(pHkTri->getVertices()[0].getQuad()));
	tri.m_pnt[1] = loc.TransformPoint(Point(pHkTri->getVertices()[1].getQuad()));
	tri.m_pnt[2] = loc.TransformPoint(Point(pHkTri->getVertices()[2].getQuad()));
	normal = Cross(tri.m_pnt[1]-tri.m_pnt[0], tri.m_pnt[2]-tri.m_pnt[0]);
	Scalar normalLen;
	normal = Normalize(normal, normalLen);
	if (normalLen < 0.0001f)
	{
		// Degenerate tri, we should fix this in tools anyway
		JAROS_ASSERT(false);
		return;
	}

	U32F localKey = pMesh->calcLocalShapeKey(key);

	tri.m_outerEdge0 = pOuterEdges->IsBitSet(localKey*3+0);
	tri.m_outerEdge1 = pOuterEdges->IsBitSet(localKey*3+1);
	tri.m_outerEdge2 = pOuterEdges->IsBitSet(localKey*3+2);

	Aabb triAabb;
	triAabb.IncludePoint(tri.m_pnt[0]);
	triAabb.IncludePoint(tri.m_pnt[1]);
	triAabb.IncludePoint(tri.m_pnt[2]);

	I32F triIndex = m_colCache.AddTriBuild(tri, triAabb);
	if (triIndex < 0)
		return;

	for(U32F kk=0; kk<3; ++kk)
	{	
		if ((tri.m_outerEdgesBits >> kk) & 1)
		{
			// This is outer edge
			Point edgePnt = tri.m_pnt[kk];
			Vector edgeVec = tri.m_pnt[(kk+1)%3] - edgePnt;
			AddShapeEdgeCol(tri.m_pnt[kk], tri.m_pnt[(kk+1)%3], normal, triIndex, kk, aabb);
		}
	}

	if (pHkTri->isQuad())
	{
		// Second triangle in the quad has local key + 1
		PHYSICS_ASSERT((localKey & 1) == 0);
		U32F localKey1 = localKey+1;
		tri.m_key = pMesh->buildShapeKeyFromLocalKey(localKey1);
		tri.m_pnt[1] = tri.m_pnt[2];
		tri.m_pnt[2] = loc.TransformPoint(Point(pHkTri->getVertices()[3].getQuad()));

		tri.m_outerEdge0 = pOuterEdges->IsBitSet(localKey1*3+0);
		tri.m_outerEdge1 = pOuterEdges->IsBitSet(localKey1*3+1);
		tri.m_outerEdge2 = pOuterEdges->IsBitSet(localKey1*3+2);

		triAabb.SetEmpty();
		triAabb.IncludePoint(tri.m_pnt[0]);
		triAabb.IncludePoint(tri.m_pnt[1]);
		triAabb.IncludePoint(tri.m_pnt[2]);

		triIndex = m_colCache.AddTriBuild(tri, triAabb);
		if (triIndex < 0)
			return;

		for(U32F kk=1; kk<3; ++kk)
		{	
			if ((tri.m_outerEdgesBits >> kk) & 1)
			{
				// This is outer edge
				Point edgePnt = tri.m_pnt[kk];
				Vector edgeVec = tri.m_pnt[(kk+1)%3] - edgePnt;
				AddShapeEdgeCol(tri.m_pnt[kk], tri.m_pnt[(kk+1)%3], normal, triIndex, kk, aabb);
			}
		}
	}
}

void Rope2::CollideStrainedWithMesh(const hknpCompressedMeshShape* pMesh, const RopeColliderHandle& colShape, const HavokCastFilter* pFilter)
{
	PROFILE(Havok, CollideStrainedWithMesh);

	const ExternalBitArray* pOuterEdges = GetMeshOuterEdges(pMesh);
	if (!pOuterEdges)
		return;

	I32F iShape = m_colCache.AddShape(colShape);
	if (iShape < 0)
		return;

	const RigidBody* pBody = colShape.GetRigidBody();
	PHYSICS_ASSERT(pBody);

	Locator loc = colShape.GetLocator(this);

	Aabb localAabb;
	CalcAabbInSpace(loc, localAabb);
	hkAabb havokLocalAabb(hkVector4(localAabb.m_min.QuadwordValue()), hkVector4(localAabb.m_max.QuadwordValue()));
	hkLocalArray<hknpShapeKey> hits(1000);

	{
		PROFILE(Havok, queryAabb);

		hknpCollisionQueryContext queryContext;
		queryContext.m_dispatcher = g_havok.m_pWorld->m_collisionQueryDispatcher;

		hknpAabbQuery query(havokLocalAabb);
		query.m_filter = const_cast<HavokCastFilter*>(pFilter);
		query.m_shapeTagCodec = g_havok.m_pShapeTagCodec;

		hknpShapeQueryInfo queryShapeInfo;

		hknpQueryFilterData targetShapeFilterData;

		hknpShapeQueryInfo targetShapeInfo;
		targetShapeInfo.m_rootShape = pMesh;

		pMesh->queryAabbImpl(&queryContext, query, queryShapeInfo, targetShapeFilterData, targetShapeInfo, &hits, nullptr);
	}

	U32F ii = 0;
	while (ii < hits.getSize())
	{
		hknpShapeCollector collector;
		pMesh->getLeafShapes(&hits[ii], hits.getSize()-ii, &collector);
		for (U32F iTri = 0; iTri<collector.getNumShapes(); iTri++)
		{
			hknpInplaceTriangleShape triPrototype;
			const hknpShape* pChildShape = collector.getTriangleShape(iTri, triPrototype.getTriangleShape());
			AddTriEdgeCol(pChildShape, hits[ii+iTri], loc, iShape, m_aabb, pMesh, pOuterEdges);
		}
		ii += collector.getNumShapes();
	}
}

static const U32F kNumEdgesPerimeter = 12;
STATIC_ASSERT(kNumEdgesPerimeter == (kNumEdgesPerimeter / 4) * 4);

// Cylinder
static const U32F kCylinderNumVertices = kNumEdgesPerimeter * 2;
static Point s_cylinderVertices[kCylinderNumVertices];
static const U32F kCylinderNumFaces = kNumEdgesPerimeter + 2;
static Vector s_cylinderFaces[kCylinderNumFaces];
static U32F s_cylinderNumFaceVertices[kCylinderNumFaces];
static U16 s_cylinderVertexIndices[kNumEdgesPerimeter * 4 + 2 * kNumEdgesPerimeter];

// Half sphere
static const U32F kHalfSphereNumVertices = kNumEdgesPerimeter * (kNumEdgesPerimeter/4) + 1;
static Point s_halfSphereVertices[kHalfSphereNumVertices];
static const U32F kHalfSphereNumFaces = kNumEdgesPerimeter * (kNumEdgesPerimeter/4);
static Vector s_halfSphereFaces[kHalfSphereNumFaces];
static U32F s_halfSphereNumFaceVertices[kHalfSphereNumFaces];
static U16 s_halfSphereVertexIndices[(kNumEdgesPerimeter * (kNumEdgesPerimeter/4) - 1) * 4 + kNumEdgesPerimeter * 3];

void Rope2::InitPrimitiveEdges()
{
	F32 s_circleCos[kNumEdgesPerimeter];
	F32 s_circleSin[kNumEdgesPerimeter];
	const F32 fiPerEdge = 2.0f * PI / kNumEdgesPerimeter;
	for (U32F ii = 0; ii<kNumEdgesPerimeter; ii++)
	{
		F32 fi = ii * fiPerEdge;
		s_circleCos[ii] = Cos(fi);
		s_circleSin[ii] = Sin(fi);
	}
	
	// Cylinder
	for (U32F ii = 0; ii<kNumEdgesPerimeter; ii++)
	{
		s_cylinderVertices[ii] = Point(s_circleCos[ii], s_circleSin[ii], -0.5f);
		s_cylinderVertices[ii+kNumEdgesPerimeter] = Point(s_circleCos[ii], s_circleSin[ii], 0.5f);
	}
	for (U32F ii = 0; ii<kNumEdgesPerimeter-1; ii++)
	{
		s_cylinderNumFaceVertices[ii] = 4;
		s_cylinderVertexIndices[ii*4 + 0] = ii;
		s_cylinderVertexIndices[ii*4 + 1] = ii+1;
		s_cylinderVertexIndices[ii*4 + 2] = ii+1+kNumEdgesPerimeter;
		s_cylinderVertexIndices[ii*4 + 3] = ii+kNumEdgesPerimeter;
		Vector v = s_cylinderVertices[ii+1] - s_cylinderVertices[ii];
		s_cylinderFaces[ii] = Normalize(Vector(v.Y(), -v.X(), 0.0f));
	}
	{
		s_cylinderNumFaceVertices[kNumEdgesPerimeter-1] = 4;
		s_cylinderVertexIndices[(kNumEdgesPerimeter-1)*4 + 0] = kNumEdgesPerimeter-1;
		s_cylinderVertexIndices[(kNumEdgesPerimeter-1)*4 + 1] = 0;
		s_cylinderVertexIndices[(kNumEdgesPerimeter-1)*4 + 2] = kNumEdgesPerimeter;
		s_cylinderVertexIndices[(kNumEdgesPerimeter-1)*4 + 3] = 2*kNumEdgesPerimeter-1;
		Vector v = s_cylinderVertices[0] - s_cylinderVertices[kNumEdgesPerimeter-1];
		s_cylinderFaces[kNumEdgesPerimeter-1] = Normalize(Vector(v.Y(), -v.X(), 0.0f));
	}
	s_cylinderNumFaceVertices[kNumEdgesPerimeter] = kNumEdgesPerimeter;
	s_cylinderNumFaceVertices[kNumEdgesPerimeter+1] = kNumEdgesPerimeter;
	s_cylinderFaces[kNumEdgesPerimeter] = Vector(0.0f, 0.0f, -1.0f);
	s_cylinderFaces[kNumEdgesPerimeter+1] = Vector(0.0f, 0.0f, 1.0f);
	for (U32F ii = 0; ii<kNumEdgesPerimeter; ii++)
	{
		s_cylinderVertexIndices[kNumEdgesPerimeter*4 + ii] = kNumEdgesPerimeter - 1 - ii;
		s_cylinderVertexIndices[kNumEdgesPerimeter*4 + kNumEdgesPerimeter + ii] = kNumEdgesPerimeter + ii;
	}

	// Half sphere
	const U32F kNumEdgesQuadrant = kNumEdgesPerimeter/4;
	for (U32F ii = 0; ii<kNumEdgesQuadrant; ii++)
	{
		F32 z = s_circleSin[ii];
		F32 xyf = s_circleCos[ii];
		for (U32F jj = 0; jj<kNumEdgesPerimeter; jj++)
		{
			s_halfSphereVertices[ii*kNumEdgesPerimeter + jj] = Point(xyf * s_circleCos[jj], xyf * s_circleSin[jj], z);
		}
	}
	s_halfSphereVertices[kHalfSphereNumVertices-1] = Point(0.0f, 0.0f, 1.0f);

	U32F iVtxIndex = 0;
	for (U32F ii = 0; ii<kNumEdgesQuadrant-1; ii++)
	{
		for (U32F jj = 0; jj<kNumEdgesPerimeter; jj++)
		{
			s_halfSphereNumFaceVertices[ii*kNumEdgesPerimeter + jj] = 4;
			s_halfSphereVertexIndices[iVtxIndex] = ii*kNumEdgesPerimeter + jj;
			U32F nextJJ = jj == kNumEdgesPerimeter-1 ? 0 : jj+1;
			s_halfSphereVertexIndices[iVtxIndex+1] = ii*kNumEdgesPerimeter + nextJJ;
			s_halfSphereVertexIndices[iVtxIndex+2] = (ii+1)*kNumEdgesPerimeter + nextJJ;
			s_halfSphereVertexIndices[iVtxIndex+3] = (ii+1)*kNumEdgesPerimeter + jj;
			Point a = s_halfSphereVertices[s_halfSphereVertexIndices[iVtxIndex]];
			Point b = s_halfSphereVertices[s_halfSphereVertexIndices[iVtxIndex+1]];
			Point c = s_halfSphereVertices[s_halfSphereVertexIndices[iVtxIndex+3]];
			s_halfSphereFaces[ii*kNumEdgesPerimeter + jj] = Normalize(Cross(b-a, c-a));
			iVtxIndex += 4;
		}
	}
	for (U32F jj = 0; jj<kNumEdgesPerimeter; jj++)
	{
		s_halfSphereNumFaceVertices[(kNumEdgesQuadrant-1)*kNumEdgesPerimeter + jj] = 3;
		s_halfSphereVertexIndices[iVtxIndex] = (kNumEdgesQuadrant-1)*kNumEdgesPerimeter + jj;
		U32F nextJJ = jj == kNumEdgesPerimeter-1 ? 0 : jj+1;
		s_halfSphereVertexIndices[iVtxIndex+1] = (kNumEdgesQuadrant-1)*kNumEdgesPerimeter + nextJJ;
		s_halfSphereVertexIndices[iVtxIndex+2] = kHalfSphereNumVertices-1;
		Point a = s_halfSphereVertices[s_halfSphereVertexIndices[iVtxIndex]];
		Point b = s_halfSphereVertices[s_halfSphereVertexIndices[iVtxIndex+1]];
		Point c = s_halfSphereVertices[s_halfSphereVertexIndices[iVtxIndex+2]];
		s_halfSphereFaces[(kNumEdgesQuadrant-1)*kNumEdgesPerimeter + jj] = Normalize(Cross(b-a, c-a));
		iVtxIndex += 3;
	}
}

void Rope2::CollideStrainedWithShape(const hknpShape* pShape, const RopeColliderHandle& colShape, const Locator& loc)
{
	PROFILE(Havok, CollideStrainedWithShape);

	I32F iShape = m_colCache.AddShape(colShape);
	if (iShape < 0)
		return;

	hkTransform hkLoc;
	hkQuaternion hkQuat;
	hkQuat.m_vec = hkVector4(loc.GetRotation().QuadwordValue());
	hkAabb hkaabb;
	pShape->calcAabb(hkTransform(hkQuat, hkVector4(loc.GetTranslation().QuadwordValue())), hkaabb);

	RopeColTri colTri;
	colTri.m_shapeIndex = iShape;
	colTri.m_outerEdgesBits = 0;
	colTri.m_key.setInvalid();
	I32F iTri = m_colCache.AddTriBuild(colTri, Aabb(Point(hkaabb.m_min.getQuad()), Point(hkaabb.m_max.getQuad())));
	if (iTri < 0)
		return;

	// g_prim.Draw(DebugBox(Transform(Quat(kIdentity), m_aabbStrained.GetCenter()), m_aabbStrained.GetScale(), kColorWhite), Seconds(3.0f));

	switch (pShape->getType())
	{
	case hknpShapeType::BOX:
	{
		const hknpBoxShape* pBox = static_cast<const hknpBoxShape*>(pShape);
		const hkcdObb& obb = pBox->getObb();
		hkQuaternion q(obb.getRotation());
		Locator boxLoc = loc.TransformLocator(Locator(Point(obb.getTranslation().getQuad()), Quat(q.m_vec.getQuad())));
		hkVector4 he;
		obb.getHalfExtents(he);
		Vector vx(he(0), 0.0f, 0.0f);
		Vector vy(0.0f, he(1), 0.0f);
		Vector vz(0.0f, 0.0f, he(2));
		Point a0 = boxLoc.TransformPoint(Point(kOrigin) - vx - vy + vz);
		Point b0 = boxLoc.TransformPoint(Point(kOrigin) + vx - vy + vz);
		Point c0 = boxLoc.TransformPoint(Point(kOrigin) + vx + vy + vz);
		Point d0 = boxLoc.TransformPoint(Point(kOrigin) - vx + vy + vz);
		Point a1 = boxLoc.TransformPoint(Point(kOrigin) - vx - vy - vz);
		Point b1 = boxLoc.TransformPoint(Point(kOrigin) + vx - vy - vz);
		Point c1 = boxLoc.TransformPoint(Point(kOrigin) + vx + vy - vz);
		Point d1 = boxLoc.TransformPoint(Point(kOrigin) - vx + vy - vz);
		Vector vecX = GetLocalX(boxLoc.GetRotation());
		Vector vecY = GetLocalY(boxLoc.GetRotation());
		Vector vecZ = GetLocalZ(boxLoc.GetRotation());
		// Upper rim
		AddShapeDoubleEdgeCol(a0, b0, vecZ, -vecY, iTri, 0, m_aabb);
		AddShapeDoubleEdgeCol(b0, c0, vecZ, vecX, iTri, 2, m_aabb);
		AddShapeDoubleEdgeCol(c0, d0, vecZ, vecY, iTri, 4, m_aabb);
		AddShapeDoubleEdgeCol(d0, a0, vecZ, -vecX, iTri, 6, m_aabb);
		// Lower rim
		AddShapeDoubleEdgeCol(a1, d1, -vecZ, -vecX, iTri, 8, m_aabb);
		AddShapeDoubleEdgeCol(d1, c1, -vecZ, vecY, iTri, 10, m_aabb);
		AddShapeDoubleEdgeCol(c1, b1, -vecZ, vecX, iTri, 12, m_aabb);
		AddShapeDoubleEdgeCol(b1, a1, -vecZ, -vecY, iTri, 14, m_aabb);
		// Vertical edges
		AddShapeDoubleEdgeCol(a1, a0, -vecX, -vecY, iTri, 16, m_aabb);
		AddShapeDoubleEdgeCol(b1, b0, -vecY, vecX, iTri, 18, m_aabb);
		AddShapeDoubleEdgeCol(c1, c0, vecX, vecY, iTri, 20, m_aabb);
		AddShapeDoubleEdgeCol(d1, d0, vecY, -vecX, iTri, 22, m_aabb);
		break;
	}

	case hknpShapeType::CYLINDER:
	{
		const hknpCylinderShape* pCylinder = static_cast<const hknpCylinderShape*>(pShape);
		Point vtxA = Point(pCylinder->getPointA().getQuad());
		Point vtxB = Point(pCylinder->getPointB().getQuad());
		Transform xfm = loc.AsTransform();
		Transform canonXfm(Mat44FromLookAt(vtxB - vtxA, kUnitYAxis, Lerp(vtxA, vtxB, 0.5f), true));
		Transform scaleXfm;
		scaleXfm.SetScale(Vector(Scalar(pCylinder->getRadius().m_real), Scalar(pCylinder->getRadius().m_real), Dist(vtxA, vtxB)));
		xfm = canonXfm * xfm;
		Transform xfmWithScale = scaleXfm * xfm;
		AddShapeEdges(kCylinderNumVertices, s_cylinderVertices, kCylinderNumFaces, s_cylinderFaces, s_cylinderNumFaceVertices, s_cylinderVertexIndices, xfm, xfmWithScale, iTri, m_aabb);
		break;
	}

	case hknpShapeType::SPHERE:
	{
		const hknpSphereShape* pSphere = static_cast<const hknpSphereShape*>(pShape);
		Locator sphereLoc = loc.TransformLocator(Locator(Point(pSphere->getVertices()[0].getQuad()), Quat(kIdentity)));
		Transform xfm = sphereLoc.AsTransform();
		Transform scaleXfm;
		scaleXfm.SetScale(Vector(pSphere->m_convexRadius, pSphere->m_convexRadius, pSphere->m_convexRadius));
		Transform xfmWithScale = scaleXfm * xfm;
		AddShapeEdges(kHalfSphereNumVertices, s_halfSphereVertices, kHalfSphereNumFaces, s_halfSphereFaces, s_halfSphereNumFaceVertices, s_halfSphereVertexIndices, xfm, xfmWithScale, iTri, m_aabb);
		Transform rotateAroundX;
		rotateAroundX.SetRotateX(PI);
		xfm = rotateAroundX * xfm;
		xfmWithScale = scaleXfm * xfm;
		AddShapeEdges(kHalfSphereNumVertices, s_halfSphereVertices, kHalfSphereNumFaces, s_halfSphereFaces, s_halfSphereNumFaceVertices, s_halfSphereVertexIndices, xfm, xfmWithScale, iTri, m_aabb);
		break;
	}

	case hknpShapeType::CAPSULE:
	{
		const hknpCapsuleShape* pCapsule = static_cast<const hknpCapsuleShape*>(pShape);
		Point vtxA = Point(pCapsule->getPointA().getQuad());
		Point vtxB = Point(pCapsule->getPointB().getQuad());
		Scalar h = Dist(vtxA, vtxB);
		Transform locXfm = loc.AsTransform();
		Transform canonXfm(Mat44FromLookAt(vtxB - vtxA, kUnitYAxis, Lerp(vtxA, vtxB, 0.5f), true));
		Transform scaleXfm;
		scaleXfm.SetScale(Vector(Scalar(pCapsule->getRadius().m_real), Scalar(pCapsule->getRadius().m_real), h));
		Transform xfm = canonXfm * locXfm;
		Transform xfmWithScale = scaleXfm * xfm;
		AddShapeEdges(kCylinderNumVertices, s_cylinderVertices, kCylinderNumFaces, s_cylinderFaces, s_cylinderNumFaceVertices, s_cylinderVertexIndices, xfm, xfmWithScale, iTri, m_aabb);

		scaleXfm.SetScale(Vector(Scalar(pCapsule->getRadius().m_real)));
		Transform capShift(kIdentity);
		capShift.SetTranslation(Point(0.0f, 0.0f, 0.5f*h));
		xfm = capShift * xfm;
		xfmWithScale = scaleXfm * xfm;
		AddShapeEdges(kHalfSphereNumVertices, s_halfSphereVertices, kHalfSphereNumFaces, s_halfSphereFaces, s_halfSphereNumFaceVertices, s_halfSphereVertexIndices, xfm, xfmWithScale, iTri, m_aabb);
		capShift.SetTranslation(Point(0.0f, 0.0f, -h));
		Transform rotateAroundX;
		rotateAroundX.SetRotateX(PI);
		xfm = rotateAroundX * capShift * xfm;
		xfmWithScale = scaleXfm * xfm;
		AddShapeEdges(kHalfSphereNumVertices, s_halfSphereVertices, kHalfSphereNumFaces, s_halfSphereFaces, s_halfSphereNumFaceVertices, s_halfSphereVertexIndices, xfm, xfmWithScale, iTri, m_aabb);
		break;
	}

	default:
	{
		const hknpConvexPolytopeShape* pConvex = pShape->asConvexPolytopeShape();
		if (pConvex)
		{
			const hkRelArray<hkcdVertex>& origVerts = pConvex->getVertices();
			if (origVerts.getSize() == 0)
				return;

			const hkRelArray<hknpConvexShape::VertexIndex>& indices = pConvex->getFaceVertexIndices();

			Point* vtx = STACK_ALLOC_ALIGNED(Point, origVerts.getSize(), kAlign16);
			for (U32F ii = 0; ii<origVerts.getSize(); ii++)
			{
				vtx[ii] = loc.TransformPoint(Point(origVerts[ii].getQuad()));
			}

			U32F iEdge = 0;

			for (U32F kk=0; kk<pConvex->getNumberOfFaces(); ++kk)
			{
				hkVector4 plane = pConvex->getPlanes()[kk];
				Vector norm = loc.TransformVector(Vector(plane.getQuad()));

				I32 numIndices = pConvex->getFaces()[kk].m_numIndices;
				U32F index = pConvex->getFaces()[kk].m_firstIndex;
				Point p0 = vtx[indices[index++]];
				Point pFirst = p0;
				for (I32 ll=1; ll<numIndices; ++ll)
				{
					Point p1 = vtx[indices[index++]];
					AddShapeEdgeCol(p0, p1, norm, iTri, iEdge, m_aabb);
					iEdge++;
					p0 = p1;
				}
				AddShapeEdgeCol(p0, pFirst, norm, iTri, iEdge, m_aabb);
				iEdge++;
			}
		}
		break;
	}

	}
}
