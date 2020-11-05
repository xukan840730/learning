/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/util/bit-array.h"

#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPack;
class DoutMem;
#if ENABLE_NAV_LEDGES
class NavLedge;
class NavLedgeGraph;
#endif
class NavLocation;
class NavMesh;
class NavPoly;
class NavPolyEx;

#define ENABLE_NAV_PATH_NODE_SENTINELS 0
#define ENABLE_NAV_PATH_NODE_GRAPH_VALIDATION !FINAL_BUILD

/// --------------------------------------------------------------------------------------------------------------- ///
//
// NavPathNode -- node used for pathfinding
//
// These nodes come in 2 flavors:
//   - nodes that correspond to a nav poly
//   - nodes that are part of an action pack
//
// NOTE: pointer or indices to these nodes should not be cached persistently.  NavMeshes
//   come and go and the nodes can disappear.  The only exceptions to this are within
//   the NavPoly and TraversalActionPack classes because those classes are responsible for
//   creating and destroying the nodes (they own the nodes).
//
class NavPathNode
{
public:
#ifdef HEADLESS_BUILD
	typedef U32 NodeId;
	typedef U32 LinkId;
#else
	typedef U16 NodeId;
	typedef U16 LinkId;
#endif

	enum LinkType
	{
		kLinkTypeBiDirectional,
		kLinkTypeOutgoing,
		kLinkTypeIncoming
	};

	enum NodeType
	{
		kNodeTypeInvalid,
		kNodeTypePoly,
		kNodeTypePolyEx,
		kNodeTypeActionPackEnter,
		kNodeTypeActionPackExit,
#if ENABLE_NAV_LEDGES
		kNodeTypeNavLedge,
#endif // ENABLE_NAV_LEDGES
		kNodeTypeMax,
	};

	static const char* GetNodeTypeStr(const NodeType t)
	{
		switch (t)
		{
		case kNodeTypeInvalid:			return "<invalid>";
		case kNodeTypePoly:				return "NavPoly";
		case kNodeTypePolyEx:			return "NavPolyEx";
		case kNodeTypeActionPackEnter:	return "ActionPackEnter";
		case kNodeTypeActionPackExit:	return "ActionPackExit";
#if ENABLE_NAV_LEDGES
		case kNodeTypeNavLedge:			return "NavLedge";
#endif // ENABLE_NAV_LEDGES
		}

		return "<unknown>";
	}

	static bool IsActionPackNode(NodeType t)	{ return (t == kNodeTypeActionPackEnter) || (t == kNodeTypeActionPackExit); }
	static bool IsNavMeshNode(NodeType t)		{ return (t == kNodeTypePoly) || (t == kNodeTypePolyEx); }
#if ENABLE_NAV_LEDGES
	static bool IsNavLedgeNode(NodeType t)		{ return t == kNodeTypeNavLedge; }
#endif // ENABLE_NAV_LEDGES

	struct Link
	{
		LinkId	m_nextLinkId;
		LinkId	m_reverseLinkId;
		NodeId	m_nodeId;
		NodeId	m_staticNodeId; // for dynamic links
		Point	m_edgeVertsPs[2];
		U8		m_type;

		bool IsDynamic() const { return m_staticNodeId != NodeId(-1); }
	};

	struct ReverseLink
	{
		LinkId	m_nextLinkId;
		LinkId	m_forwardLinkId;
		NodeId	m_nodeId;
	};

#if ENABLE_NAV_PATH_NODE_SENTINELS
	static CONST_EXPR U32 kNavPathNodeSentinel0 = 0xdeadbeef;
	static CONST_EXPR U32 kNavPathNodeSentinel1 = 0xfeedface;
#endif // ENABLE_NAV_PATH_NODE_SENTINELS

	NavPathNode() :
#if ENABLE_NAV_PATH_NODE_SENTINELS
		m_sentinel0(kNavPathNodeSentinel0) ,
#endif // ENABLE_NAV_PATH_NODE_SENTINELS
		  m_posXPs(0.0f)
		, m_posYPs(0.0f)
		, m_posZPs(0.0f)
		, m_extraCost(0)
		, m_id(0)
		, m_linkId(0)
		, m_navMgrId(NavManagerId::kInvalidMgrId)
		, m_nodeType(kNodeTypeInvalid)
		, m_blockageMask(Nav::kStaticBlockageMaskNone)
#if ENABLE_NAV_PATH_NODE_SENTINELS
		, m_sentinel1(kNavPathNodeSentinel1)
#endif // ENABLE_NAV_PATH_NODE_SENTINELS
	{
	}

	bool IsValid() const
	{
		return (m_nodeType > kNodeTypeInvalid)
			&& (m_nodeType < kNodeTypeMax)
#if ENABLE_NAV_PATH_NODE_SENTINELS
			&& (m_sentinel0 == kNavPathNodeSentinel0)
			&& (m_sentinel1 == kNavPathNodeSentinel1)
#endif // ENABLE_NAV_PATH_NODE_SENTINELS
			;
	}

	bool IsCorrupted() const
	{
#if ENABLE_NAV_PATH_NODE_SENTINELS
		return (m_sentinel0 != kNavPathNodeSentinel0)
			|| (m_sentinel1 != kNavPathNodeSentinel1);
#else
		return false;
#endif // ENABLE_NAV_PATH_NODE_SENTINELS
	}

	// getters
	NavManagerId		GetNavManagerId() const			{ return m_navMgrId; }
	NavMeshHandle		GetNavMeshHandle() const		{ return NavMeshHandle(m_navMgrId); }
	NavPolyHandle		GetNavPolyHandle() const		{ return NavPolyHandle(m_navMgrId); }
	NavPolyExHandle		GetNavPolyExHandle() const		{ return NavPolyExHandle(m_navMgrId); }

	NodeType			GetNodeType() const				{ return static_cast<NodeType>(m_nodeType); }
	U32F				GetId() const					{ return m_id; }
	U32F				GetFirstLinkId() const			{ return m_linkId; }
	ActionPack*			GetActionPack() const			{ return m_hActionPack.ToActionPack(); }
	ActionPackHandle	GetActionPackHandle() const		{ return m_hActionPack; }
#if ENABLE_NAV_LEDGES
	NavLedgeHandle		GetNavLedgeHandle() const		{ return m_hNavLedge; }
#endif // ENABLE_NAV_LEDGES

	bool				IsBlocked(const Nav::StaticBlockageMask blockageType = Nav::kStaticBlockageMaskAll) const { return (m_blockageMask & blockageType) != 0; }
	float				GetExtraCost() const		{ return float(m_extraCost); }

	bool				IsActionPackNode() const	{ return IsActionPackNode(GetNodeType()); }
	bool				IsNavMeshNode() const		{ return IsNavMeshNode(GetNodeType()); }
#if ENABLE_NAV_LEDGES
	bool				IsNavLedgeNode() const		{ return IsNavLedgeNode(GetNodeType()); }
#endif // ENABLE_NAV_LEDGES

	// setters
	void AsActionPack(ActionPack* pAp,
					  Point_arg posPs,
					  const NavLocation& registrationLoc,
					  float pathCost,
					  bool exitNode);

	void AsActionPack(ActionPack* pAp,
					  Point_arg posPs,
					  const NavManagerId& navMgrId,
					  float pathCost,
					  bool exitNode);

	void AsNavPoly(const NavPoly& poly, Point_arg posPs);
	void AsNavPolyEx(const NavPolyEx* pPolyEx, Point_arg posPs);
#if ENABLE_NAV_LEDGES
	void AsNavLedge(const NavLedge* pLedge, Point_arg posPs);
#endif // ENABLE_NAV_LEDGES

	void SetBlockageMask(const Nav::StaticBlockageMask blockageMask) { m_blockageMask = blockageMask; }
	void SetExtraCost(float cost)
	{
		NAV_ASSERTF(Abs(cost) <= 127.0f, ("Path node extra cost overflow (%f > 127)", cost));
		m_extraCost = I8(cost);
	}

	friend class NavPathNodeMgr;

	const Point3 GetPositionPs() const
	{
		return Point3(m_posXPs, MakeF32(m_posYPs), m_posZPs);
	}

	Point WorldToParent(Point posWs) const;
	Point ParentToWorld(Point posPs) const;

protected:
	void SetPositionPs(Point_arg posPs)
	{
		NAV_ASSERT(IsReasonable(posPs));
		m_posXPs = posPs.X();
		m_posYPs = MakeF16(posPs.Y());
		m_posZPs = posPs.Z();
	}

private:

// these sizes assume no path node sentinels and no nav ledges.
// if this changes, please restructure this struct as needed and consider
// the repercussions to packing and total size.

															// Delta	|	Total	|	Comment
#if ENABLE_NAV_PATH_NODE_SENTINELS
	U32							m_sentinel0;				//			|			|
#endif
#if ENABLE_NAV_LEDGES
	NavLedgeHandle				m_hNavLedge;				//			|			|
#endif // ENABLE_NAV_LEDGES

	NavManagerId				m_navMgrId;					// 8		|	8		|
	ActionPackHandle			m_hActionPack;				// 4		|	12		|	AP only, ptr to AP
	F32							m_posXPs;					// 4		|	16		|
	F32							m_posZPs;					// 4		|	20		|
	F16							m_posYPs;					// 2		|	22		|
	NodeId						m_id;						// 2		|	24		|	index of this node in the global array of nodes
	LinkId						m_linkId;					// 2		|	26		|	index to linked list of Links
	LinkId						m_reverseLinkId;			// 2		|	28		|
	Nav::StaticBlockageMask		m_blockageMask;				// 2		|	30		|	whether node is blocked
	I8							m_extraCost;				// 1		|	31		|	AP only, extra traversal cost
	U8							m_nodeType : 3;				// 			|			|	type of node
	U8							m_pad : 5;					// 1		|   32		|   For a rainy day

	STATIC_ASSERT(sizeof(Nav::StaticBlockageMask) == sizeof(U16));

#if ENABLE_NAV_PATH_NODE_SENTINELS
	U32					m_sentinel1;
#endif
};

#if ENABLE_NAV_PATH_NODE_SENTINELS
STATIC_ASSERT(sizeof(NavPathNode) == 40);
#elif defined(HEADLESS_BUILD)
STATIC_ASSERT(sizeof(NavPathNode) == 40);
#else
STATIC_ASSERT(sizeof(NavPathNode) == 32);
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
//
// NavPathNodeMgr -- a collection of nodes and links used for path finding (a graph)
//
//   Nodes are points in 3d space with connections to other Nodes as well as
//     some other data for path finding
//
// These nodes come in 2 flavors:
//   - nodes that correspond to a nav poly
//   - nodes that are part of an action pack
//
//   Links are directional connections between pairs of nodes.  Nodes that have
//    bidirectional linkage (like adjacent polygons in a navmesh) use 2 Links.
//
class ALIGNED(16) NavPathNodeMgr
{
public:
	typedef NavPathNode Node;
	typedef NavPathNode::Link Link;
	typedef NavPathNode::ReverseLink ReverseLink;
	typedef NavPathNode::NodeId NodeId;
	typedef NavPathNode::LinkId LinkId;

	static CONST_EXPR NodeId kInvalidNodeId = NodeId(-1);
	static CONST_EXPR LinkId kInvalidLinkId = 0;  // link 0 is reserved as the free list

#ifdef HEADLESS_BUILD
	static CONST_EXPR U32 kMaxNodeCount = 64 * 1024; // NodeId(-1);
	static CONST_EXPR U32 kMaxLinkCount = 256 * 1024; // LinkId(-1);
#else
	static CONST_EXPR U16 kMaxNodeCount = 8 * 1024; // NodeId(-1);
	static CONST_EXPR U16 kMaxLinkCount = 32 * 1024; // LinkId(-1);
#endif

	typedef BitArray<kMaxNodeCount> NodeBits;

	NavPathNodeMgr();
	void		Init();

	void		Update();

#if ENABLE_NAV_PATH_NODE_GRAPH_VALIDATION
	void		Validate() const;
#else
	inline void Validate() const {}
#endif

	U32F GetMaxNodeCount() const		{ return m_maxNodeCount; }
	U32F GetMaxLinkCount() const		{ return m_maxLinkCount; }
	U32F GetNodeAllocFailCount() const	{ return m_nodeAllocFailCount; }
	U32F GetLinkAllocFailCount() const	{ return m_linkAllocFailCount; }

	Node&		GetNode(U32F i)				{ NAV_ASSERT(i < kMaxNodeCount); return m_nodes[i]; }
	const Node&	GetNode(U32F i) const		{ NAV_ASSERT(i < kMaxNodeCount); return m_nodes[i]; }
	Link&		GetLink(U32F i)				{ NAV_ASSERT(i < kMaxLinkCount); return m_links[i]; }
	const Link& GetLink(U32F i) const		{ NAV_ASSERT(i < kMaxLinkCount); return m_links[i]; }
	U32F GetMaxAllocatedNodeCount() const	{ return m_maxNodeAllocCount; }
	U32F GetMaxAllocatedLinkCount() const	{ return m_maxLinkAllocCount; }

	bool Failed() const { return GetNodeAllocFailCount() > 0 || GetLinkAllocFailCount() > 0; }

	void DebugDraw() const;
	void DebugPrintStats(MsgOutput output = kMsgCon) const;

	void AddExNodesFromNavPoly(const NavPoly* pPoly);
	void AddExLinksFromNavPoly(const NavPoly* pPoly);
	void RemoveNavPolyEx(NavPolyEx* pPolyEx);

private:
	void Reset();

	U32F AllocateNode();
	void RemoveAllLinks(U32F iNode);
	void RemoveNode(U32F iNode);
	void FreeNode(U32F iNode);
	bool TryFreeNode(U32F iNode);

	U32F AllocateLink();
	void FreeLink(Link* pLink);

	U32F AllocateReverseLink();
	void FreeReverseLink(ReverseLink* pLink);

	LinkId AddLink(U32F iFromNode,
				   U32F iToNode,
				   Point_arg edge0Ps,
				   Point_arg edge1Ps,
				   NavPathNode::LinkType linkType = NavPathNode::kLinkTypeBiDirectional,
				   U32F iStaticToNode = kInvalidNodeId);
	void AddLinkSafe(U32F iFromNode,
					 U32F iToNode,
					 Point_arg edge0Ps,
					 Point_arg edge1Ps,
					 NavPathNode::LinkType linkType = NavPathNode::kLinkTypeBiDirectional);
	bool RemoveLink(U32F iFromNode, U32F iToNode);

	// to be called by NavMesh
	bool AddNavMesh(NavMesh* pMesh);
	void RemoveNavMesh(NavMesh* pMesh);

#if ENABLE_NAV_LEDGES
	bool AddNavLedgeGraph(NavLedgeGraph* pLedgeGraph);
	void RemoveNavLedgeGraph(NavLedgeGraph* pLedgeGraph);
#endif // ENABLE_NAV_LEDGES

	U32 m_maxNodeCount;
	U32 m_maxLinkCount;
	U32 m_nodeAllocCount;
	U32 m_linkAllocCount;
	U32 m_maxNodeAllocCount;
	U32 m_maxLinkAllocCount;
	U32 m_nodeAllocFailCount;
	U32 m_linkAllocFailCount;
	NodeBits m_allocMap;		// map of allocated nodes
	Node* m_nodes;
	Link* m_links;
	ReverseLink* m_reverseLinks;

	friend class NavMesh;
	friend class TraversalActionPack;
	friend class NavPolyEx;

#if ENABLE_NAV_LEDGES
	friend class NavLedgeGraph;
#endif // ENABLE_NAV_LEDGES

public:
	char* m_pDebugLogMem;
	DoutMem* m_pDebugLog;
};

/// --------------------------------------------------------------------------------------------------------------- ///
extern NavPathNodeMgr g_navPathNodeMgr;

