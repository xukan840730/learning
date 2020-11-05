/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef TOOLS_KDTREE_H
#define TOOLS_KDTREE_H

#include "common/tree/primitive-tree.h"

class KdTree;
class KdTreeBuilder;

// ---------------------------------------------------------------------------------------------------------------------

// Options
struct KdTreeOptions
{
	bool				m_dumpInputPrims;
	bool				m_dumpInsertMeshes;				// dump details of the InsertMeshes() algorithm
	bool				m_dumpFinalTree;				// dump the final tree

	KdTreeOptions()
		: m_dumpInputPrims(false)
		, m_dumpInsertMeshes(false)
		, m_dumpFinalTree(false)
	{ }
};

extern KdTreeOptions g_kdTreeOptions;

// Since our split planes are always parallel to one of the world axes,
// we don't need to store a full normal; this will do!
enum KdTreeSplitAxis
{
	kSplitAxisX,
	kSplitAxisY,
	kSplitAxisZ,
	kSplitAxisForceU32 = 0xFFFFFFFFu
};

// ---------------------------------------------------------------------------------------------------------------------
// KdTree
// ---------------------------------------------------------------------------------------------------------------------
// A KdTree is a binary spacial hierarchical subdivision structure that
// allows fast intersection tests with its leaves. When building the tree,
// at each step the node in question and its contents are examined to find
// the axis-aligned splitting plane through the node's bounding volume that
// meets a specific criteria. This implementation uses the Surface Area
// Heuristic (SAH), which uses the minimization of a cost function to choose
// the best splitting plane for each node, as well as deciding when to stop
// subdividing the tree. If you're interested in how it works, there's a ton
// of literature online. ;)
// ---------------------------------------------------------------------------------------------------------------------

// Contains the clipped AABB for a primitive
struct KdTreePrimitive
{
	I32					m_objIndex;
	I32					m_primIndex;

	KdTreePrimitive()
		: m_objIndex(-1)
		, m_primIndex(-1)
	{
	}
};

// A node in our tree! If it is an internal node, it splits its volume into
// two children by choosing an axis to split along and the best place to make
// that split. If it is a leaf node, it has no children. If it has children,
// it is not a leaf node. Simple as that!
struct KdTreeNode
{
	static const U16	kInvalidNodeIndex = 0xFFFFu;
	static const U16	kBvhInvalidNodeIndex = 0x8000u;

	F32					m_splitPlaneDistance;	// Distance from the origin to the split plane

	U16					m_iLeft;				// Index of the child node on the positive side of the split
	U16					m_iRight;				// Index of the child node on the negative side of the split

	U32					m_iFirstPrimitive;		// Index of first primitive assigned to this node (must always be a valid index)
												// Used for left node in bvh trees

	U16					m_numPrimitives;		// Number of (contiguous) primitives assigned to this node (can be zero)
												// Used for left node in bvh trees

	U16					m_splitAxis;			// Which axis this node splits its volume by

	// --------------------------------------------
	// These aren't written out for the standard KdTree, only for the BVH!
	Aabb				m_leftChildAabb;
	Aabb				m_rightChildAabb;

	U32					m_iLeftFirstPrimitive;
	U32					m_iRightFirstPrimitive; 
	U16					m_leftNumPrimitives;		// Number of (contiguous) primitives assigned to this node (can be zero)
	U16					m_rightNumPrimitives;		// Number of (contiguous) primitives assigned to this node (can be zero)

	KdTreeNode()
		: m_splitPlaneDistance(0.0f)
		, m_iLeft(kInvalidNodeIndex)
		, m_iRight(kInvalidNodeIndex)
		, m_iFirstPrimitive(0)
		, m_numPrimitives(0)
		, m_splitAxis(kSplitAxisX)
	{
	}

	int GetNumPrimitives() const { return m_numPrimitives; }
	KdTreePrimitive* GetPrimitive(const KdTree* pTree, int iPrim) const;
};

class KdTree : public PrimitiveTree
{
public:
	static Scalar kSplitTolerance;

	explicit KdTree(PrimitiveProxy* pProxy) // load constructor
		: PrimitiveTree(pProxy)
	{
		// leave all data unchanged; valid data will already have been loaded from the pak file
	}
	KdTree() : PrimitiveTree(nullptr) {}

	virtual ~KdTree();

	virtual StringId64 GetTreeType() const;

	const KdTreeNode* GetRootNode() const { return (m_totalNodes != 0) ? &m_aNodes[0] : NULL; }
	const Aabb& GetRootAabb() const { return m_rootAabb; }
	int GetMaxDepth() const { return m_maxDepth; }

	int GetNumNodes() const { return m_totalNodes; }
	const KdTreeNode* GetNode(U32 iNode) const { return (iNode >= 0 && iNode < m_totalNodes) ? &m_aNodes[iNode] : NULL; }

	int GetNumPrimitives() const { return m_totalPrimitives; }
	KdTreePrimitive* GetPrimitive(U32 iPrim) const { return (iPrim >= 0 && iPrim < m_totalPrimitives) ? &m_aPrimitives[iPrim] : NULL; }

//protected: // for the tools, just let everything hang out!
//	friend class		KdTreeBuilder;

	Aabb				m_rootAabb;
	KdTreeNode*		m_aNodes;		// array of all nodes: root node is m_aNodes[0]
	KdTreePrimitive*	m_aPrimitives;	// array of all primitives referenced by the nodes: each node's prims are contiguous in this array
	U32					m_totalNodes;
	U32					m_totalPrimitives;
	U32					m_maxDepth;
};

// ---------------------------------------------------------------------------------------------------------------------

struct BvhAabb
{
	F32		m_min[3];
	F32		m_max[3];

	BvhAabb() {}

	BvhAabb(const Aabb& aabb)
	{
		m_min[0] = aabb.m_min.X();
		m_min[1] = aabb.m_min.Y();
		m_min[2] = aabb.m_min.Z();

		m_max[0] = aabb.m_max.X();
		m_max[1] = aabb.m_max.Y();
		m_max[2] = aabb.m_max.Z();
	}
};

// ---------------------------------------------------------------------------------------------------------------------
// KdTreeBuilder
// ---------------------------------------------------------------------------------------------------------------------

// Contains the clipped AABB for a primitive
struct KdTreeBuilderPrimitive : public KdTreePrimitive
{
	Aabb				m_aabb;			// The (possibly clipped!) AABB, created from the mesh's OBB
	const char*			m_name;
	const char*			m_primName;
	KdTreeBuilderPrimitive*	m_pNext;		// Singly-linked list

	KdTreeBuilderPrimitive()
		: KdTreePrimitive()
		, m_name(NULL)
		, m_primName(NULL)
		, m_pNext(NULL)
	{
	}
};

// A node in our tree! If it is an internal node, it splits its volume into
// two children by choosing an axis to split along and the best place to make
// that split. If it is a leaf node, it has no children. If it has children,
// it is not a leaf node. Simple as that!
struct KdTreeBuilderNode
{
	Aabb					m_aabb;					// aabb for the node.

	KdTreeSplitAxis			m_splitAxis;			// Which axis this node splits its volume by
	F32						m_splitPlaneDistance;	// Distance from the origin to the split plane

	KdTreeBuilderNode*		m_pLeft;				// The child node on the positive side of the split
	KdTreeBuilderNode*		m_pRight;				// The child node on the negative side of the split
	KdTreeBuilderPrimitive*	m_pPrimitives;			// All the primitives directly inside this leaf node

	KdTreeBuilderNode()
		: m_splitAxis(kSplitAxisX)
		, m_splitPlaneDistance(0.0f)
		, m_pLeft(NULL)
		, m_pRight(NULL)
		, m_pPrimitives(NULL)
	{
	}

	bool IsLeafNode() const
	{
		return m_pLeft == NULL && m_pRight == NULL;
	}
};

class KdTreeBuilder : public PrimitiveTreeBuilder
{
public:
	struct BuildStats
	{
		U64 m_elapsedTicks;			// Number of raw timer ticks that elapsed while building the tree
		U32 m_numInputPrimitives;	// Total number of valid primitives used as input
		U32 m_numPrimitives;		// Total number of KdTreePrimitives in the tree
		U32 m_numUniquePrimitives;	// Total number of unique primitives if we copy them for each node to keep them contiguous
		U32 m_numNodes;				// Total number of KdTreeNodes in the tree
		U32 m_maxDepth;				// The maximum depth the tree extends down to
		U32 m_maxLeafSize;			// The maximum size of the leaf for splitting purposes
		F32 m_rootVolume;			// The volume of the root node
		F32 m_occupiedVolume;		// The total volume of all the leaf nodes' AABBs
		U32 m_totalMem;				// Number of bytes this tree and its data occupies
		U32 m_totalMemEx;			// Number of bytes the compacted "ex" tree occupies
		U32 m_numLeafNodes;			// Number of leaf nodes
		U32 m_numEmptyNodes;		// Number of leaf nodes that are empty

		BuildStats()
			: m_elapsedTicks(0)
			, m_numInputPrimitives(0)
			, m_numPrimitives(0)
			, m_numUniquePrimitives(0)
			, m_numNodes(0)
			, m_maxDepth(0)
			, m_maxLeafSize(0)
			, m_rootVolume(0.0f)
			, m_occupiedVolume(0.0f)
			, m_totalMem(0)
			, m_totalMemEx(0)
			, m_numLeafNodes(0)
			, m_numEmptyNodes(0)
		{
		}
	};

	struct SahParams
	{
		F32 m_Ct;
		F32 m_Ci;
		F32 m_lambda;

		SahParams(F32 Ct, F32 Ci, F32 lambda)
			: m_Ct(Ct), m_Ci(Ci), m_lambda(lambda)
		{
		}
	};

	KdTreeBuilder(PrimitiveProxy* pProxy, F32 sahCt, F32 sahCi, F32 sahLambda, U32F maxDepth, U32F maxLeafSize = 0, bool bvh = false)
		: PrimitiveTreeBuilder(pProxy)
		, m_sah(sahCt, sahCi, sahLambda)
		, m_pRoot(NULL)
		, m_maxDepth(maxDepth)
		, m_maxLeafSize(maxLeafSize)
		, m_bvh(bvh)
	{
	}

	virtual ~KdTreeBuilder();

	virtual StringId64 GetTreeType() const;

	virtual PrimitiveTree* Build();

	const BuildStats& GetBuildStats() const { return m_buildStats; }
	int GetBuildStatsAsString(char* buffer, size_t bufferCapacity) const;

	const KdTreeBuilderNode* GetRootNode() const { return m_pRoot; }
	const Aabb& GetRootAabb() const { return m_rootAabb; }

	FILE* m_fp; // for debug dumping ONLY

private:
	bool	CompactTree(KdTree* pTree, KdTreeBuilderNode* pBNode, U32 iNode);
	bool	CompactBvh(KdTree* pTree, KdTreeBuilderNode* pBNode, U32 iNode);
	U16		AddPrimitives(KdTree* pTree, KdTreeBuilderNode* pBNode);

	// this data is only used while building the tree
	SahParams			m_sah;
	BuildStats			m_buildStats;
	Aabb				m_rootAabb;
	KdTreeBuilderNode*	m_pRoot;
	U32					m_maxDepth;				//  (0 = any depth, use max leaf size instead)
	U32					m_maxLeafSize;			//	(0 = use max depth instead)
	bool				m_bvh;					// Process as BVH instead of kdtree
};

// ---------------------------------------------------------------------------------------------------------------------
// Inlines
// ---------------------------------------------------------------------------------------------------------------------

inline KdTreePrimitive* KdTreeNode::GetPrimitive(const KdTree* pTree, int iPrim) const
{
	return (iPrim >= 0 && iPrim < m_numPrimitives) ? pTree->GetPrimitive(m_iFirstPrimitive + iPrim) : NULL;
}

#endif