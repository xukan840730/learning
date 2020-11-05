/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-node-table.h"

#include "gamelib/gameplay/nav/nav-handle.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-find.h"
#include "gamelib/gameplay/nav/nav-poly.h"

/// --------------------------------------------------------------------------------------------------------------- ///
NavNodeKey::NavNodeKey(const NavHandle& hNav)
{
	const NavPathNode::NodeId pathNodeId = hNav.GetPathNodeId();
	const U16 partitionId = 0;

	*this = NavNodeKey(pathNodeId, partitionId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavNodeKey::NavNodeKey(const NavLocation& navLoc, const Nav::PathFindContext& pathContext)
{
	const NavPathNode::NodeId pathNodeId = navLoc.GetPathNodeId();
	U16 partitionId = 0;

	if (navLoc.GetType() == NavLocation::Type::kNavPoly)
	{
		if (const NavPoly* pPoly = navLoc.ToNavPoly())
		{
			const NavMesh* pNavMesh = pPoly->GetNavMesh();
			const Point posLs = pNavMesh->ParentToLocal(navLoc.GetPosPs());
			partitionId = Nav::ComputePartitionValueFromGaps(pathContext, pNavMesh, pPoly, posLs);
		}
	}

	*this = NavNodeKey(pathNodeId, partitionId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavNodeKey::NavNodeKey(const Nav::PathFindLocation& pfLoc, const Nav::PathFindContext& pathContext)
{
	const U16 pathNodeId = pfLoc.m_pathNodeId;
	U16 partitionId = 0;

	if (pfLoc.m_pNavPoly)
	{
		const NavMesh* pNavMesh = pfLoc.m_pNavPoly->GetNavMesh();
		const Point posLs = pNavMesh->WorldToLocal(pfLoc.m_posWs);
		partitionId = Nav::ComputePartitionValueFromGaps(pathContext, pNavMesh, pfLoc.m_pNavPoly, posLs);
	}

	*this = NavNodeKey(pathNodeId, partitionId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavPathNodeProxy::WorldToParent(Point posWs) const
{
	Point posPs = posWs;

	if (IsNavMeshNode() || IsActionPackNode())
	{
		if (const NavMesh* pMesh = GetNavMeshHandle().ToNavMesh())
		{
			posPs = pMesh->WorldToParent(posWs);
		}
	}
#if ENABLE_NAV_LEDGES
	else if (IsNavLedgeNode())
	{
		if (const NavLedgeGraph* pLedgeGraph = GetNavLedgeHandle().ToLedgeGraph())
		{
			posPs = pLedgeGraph->WorldToParent(posWs);
		}
	}
#endif // ENABLE_NAV_LEDGES

	return posPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavPathNodeProxy::ParentToWorld(Point posPs) const
{
	Point posWs = posPs;

	if (IsNavMeshNode() || IsActionPackNode())
	{
		if (const NavMesh* pMesh = GetNavMeshHandle().ToNavMesh())
		{
			posWs = pMesh->ParentToWorld(posPs);
		}
	}
#if ENABLE_NAV_LEDGES
	else if (IsNavLedgeNode())
	{
		if (const NavLedgeGraph* pLedgeGraph = GetNavLedgeHandle().ToLedgeGraph())
		{
			posWs = pLedgeGraph->ParentToWorld(posPs);
		}
	}
#endif // ENABLE_NAV_LEDGES

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NavPathNodeProxy::GetParentSpace() const
{
	Locator ps = kIdentity;

	if (IsNavMeshNode() || IsActionPackNode())
	{
		if (const NavMesh* pMesh = GetNavMeshHandle().ToNavMesh())
		{
			ps = pMesh->GetParentSpace();
		}
	}
#if ENABLE_NAV_LEDGES
	else if (IsNavLedgeNode())
	{
		if (const NavLedgeGraph* pLedgeGraph = GetNavLedgeHandle().ToLedgeGraph())
		{
			ps = pLedgeGraph->GetParentSpace();
		}
	}
#endif // ENABLE_NAV_LEDGES

	return ps;
}
