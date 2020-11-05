/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef NDPHYS_ROPE2_COL_H 
#define NDPHYS_ROPE2_COL_H 

// #define ROPE2_EDGES_PER_POINT

#include "corelib/memory/scoped-temp-allocator.h"

#include "gamelib/ndphys/rope/rope2-collider.h"
#include "gamelib/ndphys/collision-filter.h"

class HavokRayCastJob;

struct RopeColTri
{
	Point m_pnt[3];
	//Vector m_normal;
	U16 m_shapeIndex;
	union
	{
		U8 m_outerEdgesBits;
		struct  
		{
			U8 m_outerEdge0 : 1;
			U8 m_outerEdge1 : 1;
			U8 m_outerEdge2 : 1;
		};
	};
	hknpShapeKey m_key;
};

struct RopeColEdge
{
	I16 m_triIndex;
	U16 m_edgeIndex;
	I16 m_startTriIndex; // if edge starts because of collision with a triangle, otherwise -1 if no collision or -2 if have not been checked yet
	I16 m_endTriIndex; // if edge ends because of collision with a triangle, otherwise -1 if no collision or -2 if have not been checked yet
	U16 m_numExtraTrims;
	U16 m_secondTrimIndex;
	Point m_pnt;
	Vec4 m_vec; // normalized edge vector. edge length is in W
	Vector m_normal;

	RopeColEdge() : m_startTriIndex(-2), m_endTriIndex(-2), m_numExtraTrims(0) {}
};

struct RopeColTriId
{
	RopeColliderHandle m_shape;
	hknpShapeKey m_triKey; 
};

struct RopeColEdgeId
{
	RopeColliderHandle m_shape;
	hknpShapeKey m_triKey; 
	U16 m_edgeIndex;
	RopeColTriId m_startTriId;
	RopeColTriId m_endTriId;
};

struct RopeColTreeNode
{
	U16 m_elemIndex;
	U16 m_numElems;
};

class RopeColTree
{
public:
	typedef bool TraverseCallback(U16 index, void* pUserData);
	typedef bool TraverseBundleCallback(U16 index, const ExternalBitArray* pBitArray, void* pUserData);

	struct Aabbv128
	{
		v128 m_min;
		v128 m_max;
	};

	RopeColTree()
		: m_maxNumLevels(0)
		, m_numLevels(0)
	{}

	void Init(F32 maxRopeLen);
	void Reset();
	void CreateTree(const Aabb& aabb, F32 minNodeSize);

	U16 GetMaxNumNodes() const { return (1 << m_maxNumLevels) - 1; }
	U16 GetNumNodes() const { return (1 << m_numLevels) - 1; }

	U16 FindNode(const Aabb& aabb) const;
	bool TraverseNode(const v128 min, const v128 max, v128 nodeMin, v128 nodeMax, U16 level, U16 node, TraverseCallback callback, void* pUserData) const;
	void Traverse(const Aabb& aabb, TraverseCallback callback, void* pUserData) const { TraverseNode(aabb.m_min.QuadwordValue(), aabb.m_max.QuadwordValue(), m_aabb.m_min.QuadwordValue(), m_aabb.m_max.QuadwordValue(), 0, 0, callback, pUserData); }

	bool TraverseNodeBundle(const Aabbv128* pAabb, U32F numAabbs, const ExternalBitArray* pBitArray, const v128 nodeMin, const v128 nodeMax, U16 level, U16 node, TraverseBundleCallback callback, void* pUserData) const;
	void TraverseBundle(const Aabb* pAabb, U32F numAabbs, TraverseBundleCallback callback, void* pUserData) const;

public:
	U16 m_maxNumLevels;
	U16 m_numLevels;
	U64 m_levelAxis; // each 2 bits determine which axis we split at each level
	Aabb m_aabb;
};

class RopeColTreeBuilder
{
public:
	struct NodeIndicesBlock
	{
		enum 
		{
			kNumIndices = 31
		};
		U16 m_indices[kNumIndices];
		U16 m_nextBlock;
	};

	void Init(const RopeColTree* pTree, U16 maxNumElements, U16 elemSize, char*& pScratchMem, U32F& memSize);
	I32F AddElement(char* pElement, const Aabb& aabb);
	void Compile(char* pElementsOut, U16& numElementsOut, RopeColTreeNode* pNodesOut, U16* pElementIndexTranslation = nullptr);

	const RopeColTree* m_pTree;
	U16 m_numNodes;
	U16* m_pNumIndices;
	U16* m_pBlockIndex;
	U32F m_maxNumBlocks;
	U32F m_numBlocks;
	NodeIndicesBlock* m_pIndicesBlocks;
	char* m_pElements;
	U16 m_maxNumElements;
	U16 m_numElements; 
	U16 m_elementSize;

	bool m_overflow;
};


class RopeColCache
{
public:
	RopeColliderHandle* m_pShapes;
	RopeColTri* m_pTris;
	RopeColEdge* m_pEdges;

	U16 m_maxShapes;
	U16 m_maxTris;
	U16 m_maxEdges;

	U16 m_numShapes;
	U16 m_numTris;
	U16 m_numEdges;
	U16 m_numEdgesBeforeTrimming;

	RopeColTree m_tree;
	RopeColTreeNode* m_pTriTreeNodes;
	RopeColTreeNode* m_pEdgeTreeNodes;

	CollideFilter m_filter;

	RopeColTreeBuilder m_triTreeBuilder;
	RopeColTreeBuilder m_edgeTreeBuilder;
	ScopedTempAllocator* m_pTreeBuilderAlloc; 
	U8* m_pTreeBuilderAllocBuffer;

	bool m_overflow;

public:
	RopeColCache() {};

	void Init(U32F maxPoints, F32 ropeLen);
	void Reset();
	void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);

	void StartBuild(const Aabb& aabb);
	void EndBuild();

	I32F AddShape(const RopeColliderHandle& shape);
	I32F AddTri(const RopeColTri& tri);
	I32F AddEdge(const RopeColEdge& edge);
	I32F AddTriBuild(const RopeColTri& tri, const Aabb& aabb);
	I32F AddEdgeBuild(const RopeColEdge& edge);

	void FindCandidateTris(const Aabb& aabb, ExternalBitArray* pCandidateTris) const;
	void FindCandidateEdges(const Aabb& aabb, ExternalBitArray* pCandidateEdges) const;

	void GetTriId(U16 triIndex, RopeColTriId& id) const;
	void GetEdgeId(U16 edgeIndex, RopeColEdgeId& id) const;
	I16 FindEdgeIndex(const RopeColEdgeId& id, const Rope2* pOwner);
	I16 FindEdgeIndex(const RopeColEdgeId& id, Point_arg pos, Point& pos1Out, const Rope2* pOwner);

	I16 GetPointFirstEdgeIndex(U32F iPoint);
	I16 GetPointLastEdgeIndex(U32F iPoint);

	bool CheckEdgeTrims(U16 edgeIndex, const Rope2* pOwner);
	U16 FitPointToTrimmedEdge(U16 edgeIndex, Point_arg pos, const RopeColTriId& startTriId, const RopeColTriId& endTriId, Point& posOut);

	void DebugDraw(const Rope2* pOwner);
	void DebugDrawTri(U32F iTri);
	void DebugDrawEdge(U32F iEdge, bool active = true, I8 positive = -1);
};

#endif // NDPHYS_ROPE2_COL_H 

