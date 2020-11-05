/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef GAMELIB_NAV_NODE_TABLE_H
#define GAMELIB_NAV_NODE_TABLE_H

#include "corelib/containers/hashtable.h"
#include "corelib/util/equality.h"
#include "corelib/util/hashfunctions.h"
#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"

class ActionPack;
class NavHandle;
class NavLocation;

namespace Nav
{
	struct PathFindContext;
	struct PathFindLocation;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class NavNodeKey
{
public:
	explicit NavNodeKey(U16 pathNodeId, U16 partitionId)
		: m_pathNodeId(pathNodeId)
		, m_partitionId(partitionId)
	{
	}

	explicit NavNodeKey()
		: m_pathNodeId(NavPathNodeMgr::kInvalidNodeId)
		, m_partitionId(0)
	{
	}

	explicit NavNodeKey(const NavHandle& hNav);
	explicit NavNodeKey(const NavLocation& navLoc, const Nav::PathFindContext& pathContext);
	explicit NavNodeKey(const Nav::PathFindLocation& pfLoc, const Nav::PathFindContext& pathContext);

	NavPathNode::NodeId GetPathNodeId() const { return m_pathNodeId; }
	U16 GetPartitionId() const { return m_partitionId; }

#ifdef HEADLESS_BUILD
	U64 GetCombinedValue() const { return m_u64; }
	U64 GetHashedValue() const { return m_u64; }

	bool operator == (const NavNodeKey& rhs) const { return (m_u64 == rhs.m_u64); }
#else
	U32 GetCombinedValue() const { return m_u32; }
	U32 GetHashedValue() const { return m_u32; }

	bool operator == (const NavNodeKey& rhs) const { return (m_u32 == rhs.m_u32); }
#endif

private:
#ifdef HEADLESS_BUILD
	union
	{
		struct
		{
			NavPathNode::NodeId m_pathNodeId;
			U16 m_partitionId;
			U8 m_padding[2];
		};
		U32 m_u64;
	};
#else
	union
	{
		struct
		{
			NavPathNode::NodeId m_pathNodeId;
			U16 m_partitionId;
		};
		U32 m_u32;
	};
#endif
	
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavPathNodeProxy
{
	NavPathNodeProxy() {}
	~NavPathNodeProxy() {}

	void Init(const NavPathNode& pathNode)
	{
		m_nodeType = pathNode.GetNodeType();

		if (IsNavMeshNode())
		{
			m_navMgrId = pathNode.GetNavManagerId();
		}
		else if (IsActionPackNode())
		{
			m_navMgrId = pathNode.GetNavManagerId();
#if ENABLE_NAV_LEDGES
			m_hNavLedge = pathNode.GetNavLedgeHandle();
#endif // ENABLE_NAV_LEDGES
			m_hActionPack = pathNode.GetActionPackHandle();
		}
#if ENABLE_NAV_LEDGES
		else if (IsNavLedgeNode())
		{
			m_hNavLedge = pathNode.GetNavLedgeHandle();
		}
#endif // ENABLE_NAV_LEDGES
		else
		{
			NAV_ASSERTF(false, ("Unknown path node type '%d'", m_nodeType));
		}
	}

	void Init(NavPathNode::NodeType nodeType, NavManagerId navManagerId, ActionPackHandle hActionPack)
	{
		m_nodeType = nodeType;
		m_navMgrId = navManagerId;
		m_hActionPack = hActionPack;
#if ENABLE_NAV_LEDGES
		m_hNavLedge = NavLedgeHandle();
#endif // ENABLE_NAV_LEDGES
	}

	void Invalidate()
	{
		m_nodeType = NavPathNode::kNodeTypeInvalid;
		m_hActionPack = ActionPackHandle();
		m_navMgrId = NavManagerId();
#if ENABLE_NAV_LEDGES
		m_hNavLedge = NavLedgeHandle();
#endif // ENABLE_NAV_LEDGES
	}

	NavPathNode::NodeType	GetNodeType() const				{ return static_cast<NavPathNode::NodeType>(m_nodeType); }

	NavManagerId			UnsafeGetNavManagerIdUnchecked() const { return m_navMgrId; }
	NavManagerId			GetNavManagerId() const			{ return (IsNavMeshNode() || IsActionPackNode()) ? m_navMgrId					: NavManagerId::kInvalidMgrId;	}
	NavMeshHandle			GetNavMeshHandle() const		{ return (IsNavMeshNode() || IsActionPackNode()) ? NavMeshHandle(m_navMgrId)	: NavMeshHandle();				}
	NavPolyHandle			GetNavPolyHandle() const		{ return (IsNavMeshNode() || IsActionPackNode()) ? NavPolyHandle(m_navMgrId)	: NavPolyHandle();				}
	NavPolyExHandle			GetNavPolyExHandle() const		{ return (IsNavMeshNode() || IsActionPackNode()) ? NavPolyExHandle(m_navMgrId)	: NavPolyExHandle();			}

#if ENABLE_NAV_LEDGES
	NavLedgeHandle			GetNavLedgeHandle() const		{ return (IsNavLedgeNode() || IsActionPackNode()) ? m_hNavLedge : NavLedgeHandle(); }
#endif // ENABLE_NAV_LEDGES

	void					SetActionPackHandle(ActionPackHandle hAp)	{ m_hActionPack = hAp; }
	ActionPack*				GetActionPack() const			{ return IsActionPackNode() ? m_hActionPack.ToActionPack() : nullptr; }
	ActionPackHandle		GetActionPackHandle() const		{ return IsActionPackNode() ? m_hActionPack : ActionPackHandle(); }

	bool					IsActionPackNode() const		{ return NavPathNode::IsActionPackNode(GetNodeType()); }
	bool					IsNavMeshNode() const			{ return NavPathNode::IsNavMeshNode(GetNodeType()); }

#if ENABLE_NAV_LEDGES
	bool					IsNavLedgeNode() const			{ return NavPathNode::IsNavLedgeNode(GetNodeType()); }
#endif // ENABLE_NAV_LEDGES

	Point					WorldToParent(Point posWs) const;
	Point					ParentToWorld(Point posPs) const;
	Locator					GetParentSpace() const;

#if ENABLE_NAV_LEDGES
	NavLedgeHandle			m_hNavLedge;
#endif // ENABLE_NAV_LEDGES
	NavManagerId			m_navMgrId;
	ActionPackHandle		m_hActionPack;

	U8						m_nodeType;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavNodeData
{
	NavNodeData() {}
	~NavNodeData() {}

	//NavPathNode m_pathNode;
	NavPathNodeProxy m_pathNode;	// [16]

	NavNodeKey m_parentNode;		// [4]
	Point3 m_pathNodePosPs;			// [12]
	F32 m_fromDist;					// [4]
	F32 m_fromCost;					// [4]
	F32 m_toCost;					// [4]
	U8 m_fringeNode;				// [1]
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavKeyDataPair
{
	NavNodeKey m_key;
	NavNodeData m_data;
};

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename _Key, typename _Data>
struct NavNodeTableTraits
{
	typedef _Key Key;
	typedef _Data Data;

	typedef HashT<_Key> HashFn;
	typedef equality<_Key, _Key> KeyCompare;
	typedef op_assign<_Key, _Key> KeyAssign;
	typedef op_assign<_Data, _Data> DataAssign;
	static I32 GetBucketForKey(const NavNodeKey& k, I32 numBuckets)
	{
#ifdef HEADLESS_BUILD
		return (I32)(k.GetHashedValue() % (U64)numBuckets);
#else
		return k.GetHashedValue() % numBuckets;
#endif
	}
};

typedef HashTable<NavNodeKey, NavNodeData, false, NavNodeTableTraits<NavNodeKey, NavNodeData>> NavNodeTable;

/// --------------------------------------------------------------------------------------------------------------- ///
struct TrivialHashNavNodeData
{
	Point3  m_posPs;						// 12
	ActionPackHandle m_hActionPack;			// 16
	U16		m_navMeshIndex;					// 18
	U16		m_uniqueId;						// 20
	U16		m_iPoly;						// 22
	U16		m_iParent;						// 24
private:
	F16		m_fromDist;						// 26
public:
	U8		m_nodeType;						// 27
	//U8		m_pad;						// 28

	TrivialHashNavNodeData() {}
	~TrivialHashNavNodeData() {}

	TrivialHashNavNodeData(const Point3 posPs, const ActionPackHandle hActionPack, const U16 navMeshIndex, const U16 uniqueId, const U16 iPoly, const U16 iParent, const float fromDist, const U8 nodeType) :
		m_posPs(posPs),
		m_hActionPack(hActionPack),
		m_navMeshIndex(navMeshIndex),
		m_uniqueId(uniqueId),
		m_iPoly(iPoly),
		m_iParent(iParent),
		m_fromDist(MakeF16(fromDist)),
		m_nodeType(nodeType)
	{
	}

	TrivialHashNavNodeData(const NavNodeData& data) :
		m_posPs(data.m_pathNodePosPs),
		m_hActionPack(data.m_pathNode.m_hActionPack),
		m_navMeshIndex(data.m_pathNode.GetNavManagerId().m_navMeshIndex),
		m_uniqueId(data.m_pathNode.GetNavManagerId().m_uniqueId),
		m_iPoly(data.m_pathNode.GetNavManagerId().m_iPoly),
		m_iParent(data.m_parentNode.GetPathNodeId()),
		m_fromDist(MakeF16(data.m_fromDist)),
		m_nodeType(data.m_pathNode.GetNodeType())
	{
	}

	bool IsValid() const
	{
		return m_nodeType != NavPathNode::kNodeTypeInvalid;
	}

	float GetFromDist() const
	{
		return MakeF32(m_fromDist);
	}
};
STATIC_ASSERT(sizeof(TrivialHashNavNodeData) == 28);

#endif // GAMELIB_NAV_NODE_TABLE_H
