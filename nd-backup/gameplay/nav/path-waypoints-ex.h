/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-node-table.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/path-waypoints.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class PathWaypointsEx : public TPathWaypoints<32>
{
public:
	typedef TPathWaypoints<32> ParentClass;

	void AddWaypoint(Point_arg pos);
	void AddWaypoint(Point_arg pos, const NavPathNodeProxy& pathNode);
	void AddWaypoint(const PathWaypointsEx& srcPath, int srcIndex);
	void AddWaypoint(const NavLocation& srcLoc);
	void AddWaypoint(Point_arg pos, const NavManagerId& mgrId, NavPathNode::NodeType nodeType);

	NavManagerId GetNavId(int i) const;
	NavManagerId GetEndNavId() const;

	NavPathNode::NodeType GetNodeType(int i) const;
	NavPathNode::NodeType GetEndNodeType() const;

	bool IsNavMeshNode(int i) const
	{
		NAV_ASSERT(i < GetWaypointCount());
		return m_pathNodes[i].IsNavMeshNode();
	}

	bool IsActionPackNode(int i) const
	{
		NAV_ASSERT(i < GetWaypointCount());
		return m_pathNodes[i].IsActionPackNode();
	}

#if ENABLE_NAV_LEDGES
	NavLedgeHandle GetNavLedgeHandle(int i) const;
	NavLedgeHandle GetEndNavLedgeHandle() const;
#endif // ENABLE_NAV_LEDGES

	ActionPackHandle GetActionPackHandle(int i) const;
	ActionPackHandle GetEndActionPackHandle() const;

	NavLocation GetAsNavLocation(const Locator& pathSpace, int i) const;
	NavLocation GetEndAsNavLocation(const Locator& pathSpace) const;

	void UpdateNavManagerId(I32F i, const NavManagerId newId);
	void UpdateEndLoc(const Locator& pathSpace, const NavLocation& newEndLoc);
	void UpdateEndNode(const Locator& pathSpace, Point_arg newEndPos, const NavPathNodeProxy& newEndPathNode);

	bool IsGroundLeg(int iLeg) const;
#if ENABLE_NAV_LEDGES
	bool IsLedgeShimmy(int iLeg) const;
	bool IsLedgeJump(int iLeg) const;
#endif // ENABLE_NAV_LEDGES

	void AppendPath(const PathWaypointsEx& srcPath);
	void CopyFrom(const PathWaypointsEx* pSourceWaypoints);

	void Reverse();

	struct ColorScheme
	{
		ColorScheme()
		{
			m_groundLeg0 = kColorRed;
			m_groundLeg1 = kColorGreen;
			m_apLeg0 = kColorCyan;
			m_apLeg1 = kColorMagenta;
#if ENABLE_NAV_LEDGES
			m_ledgeShimmy0 = kColorYellow;
			m_ledgeShimmy1 = kColorOrange;
			m_ledgeJump0 = kColorBlue;
			m_ledgeJump1 = kColorPink;
#endif // ENABLE_NAV_LEDGES
		}

		Color m_groundLeg0;
		Color m_groundLeg1;
		Color m_apLeg0;
		Color m_apLeg1;
		Color m_ledgeShimmy0;
		Color m_ledgeShimmy1;
		Color m_ledgeJump0;
		Color m_ledgeJump1;
	};

	void DebugDraw(const Locator& pathSpace,
				   bool drawDetails,
				   float radius		  = 0.0f,
				   ColorScheme colors = ColorScheme(),
				   DebugPrimTime tt   = kPrimDuration1FrameAuto) const;

private:
	NavPathNodeProxy m_pathNodes[ParentClass::kMaxCount];
};
