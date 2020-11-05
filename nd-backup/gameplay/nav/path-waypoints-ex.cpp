/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/path-waypoints-ex.h"

#include "corelib/math/segment-util.h"

#include "ndlib/render/util/text-printer.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-handle.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawHalfCircle(Point_arg center,
						 Vector_arg exposedVec,
						 float radius,
						 Color clr,
						 DebugPrimTime tt = kPrimDuration1FrameAuto);

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::AddWaypoint(Point_arg pos)
{
	NAV_ASSERT(IsReasonable(pos));

	const int count = GetWaypointCount();
	if (count < kMaxCount)
	{
		m_pathNodes[count].Invalidate();
	}

	ParentClass::AddWaypoint(pos);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::AddWaypoint(Point_arg pos, const NavPathNodeProxy& pathNode)
{
	NAV_ASSERT(IsReasonable(pos));

	const int count = GetWaypointCount();
	if (count < kMaxCount)
	{
		m_pathNodes[count] = pathNode;
	}

	ParentClass::AddWaypoint(pos);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::AddWaypoint(const PathWaypointsEx& srcPath, int srcIndex)
{
	AddWaypoint(srcPath.GetWaypoint(srcIndex), srcPath.m_pathNodes[srcIndex]);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::AddWaypoint(const NavLocation& srcLoc)
{
	NavPathNodeProxy proxyNode;
	proxyNode.Invalidate();

	const Point posPs = srcLoc.GetPosPs();

	switch (srcLoc.GetType())
	{
	case NavLocation::Type::kNavPoly:
		proxyNode.m_navMgrId = srcLoc.GetNavPolyHandle().GetManagerId();
		proxyNode.m_nodeType = NavPathNode::kNodeTypePoly;
		break;

#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		proxyNode.m_hNavLedge = srcLoc.GetNavLedgeHandle();
		proxyNode.m_nodeType = NavPathNode::kNodeTypeNavLedge;
		break;
#endif
	}

	AddWaypoint(posPs, proxyNode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::AddWaypoint(Point_arg pos, const NavManagerId& mgrId, NavPathNode::NodeType nodeType)
{
	NavPathNodeProxy proxyNode;
	proxyNode.Invalidate();

	proxyNode.m_navMgrId = mgrId;
	proxyNode.m_nodeType = nodeType;

	AddWaypoint(pos, proxyNode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavManagerId PathWaypointsEx::GetNavId(int i) const
{
	NAV_ASSERT(i < GetWaypointCount());
	if (m_pathNodes[i].IsNavMeshNode() || m_pathNodes[i].IsActionPackNode())
	{
		return m_pathNodes[i].GetNavManagerId();
	}

	return NavManagerId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavManagerId PathWaypointsEx::GetEndNavId() const
{
	const int count = GetWaypointCount();
	NAV_ASSERT(count > 0);
	return GetNavId(count - 1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavPathNode::NodeType PathWaypointsEx::GetNodeType(int i) const
{
	NAV_ASSERT(i < GetWaypointCount());
	return m_pathNodes[i].GetNodeType();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavPathNode::NodeType PathWaypointsEx::GetEndNodeType() const
{
	const int count = GetWaypointCount();
	NAV_ASSERT(count > 0);
	return GetNodeType(count - 1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackHandle PathWaypointsEx::GetActionPackHandle(int i) const
{
	NAV_ASSERT(i < GetWaypointCount());
	if (m_pathNodes[i].IsActionPackNode())
	{
		return m_pathNodes[i].GetActionPackHandle();
	}
	return ActionPackHandle();
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackHandle PathWaypointsEx::GetEndActionPackHandle() const
{
	const int count = GetWaypointCount();
	NAV_ASSERT(count > 0);
	return GetActionPackHandle(count - 1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLocation PathWaypointsEx::GetAsNavLocation(const Locator& pathSpace, int i) const
{
	NAV_ASSERT(i < GetWaypointCount());

	const Point posWs = pathSpace.TransformPoint(GetWaypoint(i));

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	NavLocation ret;

#if ENABLE_NAV_LEDGES
	if (m_pathNodes[i].IsNavLedgeNode())
	{
		ret.SetWs(posWs, m_pathNodes[i].GetNavLedgeHandle());
	}
	else
#endif
	{
		ret.SetWs(posWs, m_pathNodes[i].GetNavPolyHandle());
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLocation PathWaypointsEx::GetEndAsNavLocation(const Locator& pathSpace) const
{
	const int count = GetWaypointCount();
	NAV_ASSERT(count > 0);
	return GetAsNavLocation(pathSpace, count - 1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::UpdateNavManagerId(I32F i, const NavManagerId newId)
{
	const I32F count = GetWaypointCount();
	if (count <= 0 || i >= count)
		return;

	m_pathNodes[i].m_navMgrId = newId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::UpdateEndLoc(const Locator& pathSpace, const NavLocation& newEndLoc)
{
	const int count = GetWaypointCount();

	if (count < 2)
		return;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const Point endPosWs = newEndLoc.GetPosWs();
	const Point endPosPs = pathSpace.UntransformPoint(endPosWs);

	NavPathNodeProxy newEndNode;
	newEndNode.Invalidate();

	switch (newEndLoc.GetType())
	{
	case NavLocation::Type::kNavPoly:
		newEndNode.m_nodeType = NavPathNode::kNodeTypePoly;
		newEndNode.m_navMgrId = newEndLoc.GetNavPolyHandle().GetManagerId();
		break;
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		newEndNode.m_nodeType  = NavPathNode::kNodeTypeNavLedge;
		newEndNode.m_hNavLedge = newEndLoc.GetNavLedgeHandle();
		break;
#endif
	}

	UpdateEndNode(pathSpace, endPosPs, newEndNode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::UpdateEndNode(const Locator& pathSpace,
									Point_arg newEndPos,
									const NavPathNodeProxy& newEndPathNode)
{
	const int count = GetWaypointCount();

	if (count < 2)
		return;

	m_pathNodes[count - 1] = newEndPathNode;

	UpdateEndPoint(newEndPos);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathWaypointsEx::IsGroundLeg(int iLeg) const
{
	if (iLeg > (GetWaypointCount() - 1))
		return false;

	if (m_pathNodes[iLeg].IsNavMeshNode() || m_pathNodes[iLeg + 1].IsNavMeshNode())
		return true;

	if (m_pathNodes[iLeg].IsActionPackNode() && m_pathNodes[iLeg + 1].IsActionPackNode())
	{
		return m_pathNodes[iLeg].GetActionPackHandle() != m_pathNodes[iLeg + 1].GetActionPackHandle();
	}

	return false;
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
bool PathWaypointsEx::IsLedgeShimmy(int iLeg) const
{
	const NavLedgeHandle hLedge0 = m_pathNodes[iLeg].GetNavLedgeHandle();
	const NavLedgeHandle hLedge1 = m_pathNodes[iLeg + 1].GetNavLedgeHandle();
	if (hLedge0.IsNull() || hLedge1.IsNull())
		return false;

	return hLedge0 == hLedge1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathWaypointsEx::IsLedgeJump(int iLeg) const
{
	const NavLedgeHandle hLedge0 = m_pathNodes[iLeg].GetNavLedgeHandle();
	const NavLedgeHandle hLedge1 = m_pathNodes[iLeg + 1].GetNavLedgeHandle();
	if (hLedge0.IsNull() || hLedge1.IsNull())
		return false;

	return hLedge0 != hLedge1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLedgeHandle PathWaypointsEx::GetNavLedgeHandle(int i) const
{
	NAV_ASSERT(i < GetWaypointCount());
	if (m_pathNodes[i].IsNavLedgeNode() || m_pathNodes[i].IsActionPackNode())
	{
		return m_pathNodes[i].GetNavLedgeHandle();
	}

	return NavLedgeHandle();
}
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::AppendPath(const PathWaypointsEx& srcPath)
{
	for (int i = 0; (i < srcPath.GetWaypointCount()) && (GetWaypointCount() < ParentClass::kMaxCount); ++i)
	{
		AddWaypoint(srcPath, i);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::CopyFrom(const PathWaypointsEx* pSourceWaypoints)
{
	if (!pSourceWaypoints)
	{
		return;
	}

	Clear();
	AppendPath(*pSourceWaypoints);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::Reverse()
{
	const I32F count = GetWaypointCount();

	for (I32F i = 0; i < count / 2; ++i)
	{
		const I32F iR = count - i - 1;
		const NavPathNodeProxy low = m_pathNodes[i];
		const NavPathNodeProxy high = m_pathNodes[iR];

		m_pathNodes[iR] = low;
		m_pathNodes[i] = high;
	}

	ParentClass::Reverse();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathWaypointsEx::DebugDraw(const Locator& pathSpace,
								bool drawDetails,
								float radius /* = 0.0f */,
								ColorScheme colors /* = ColorScheme() */,
								DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	const int count = GetWaypointCount();

	if (0 == count)
		return;

	if ((count == 1) && (radius > kSmallestFloat))
	{
		const Point pos = pathSpace.TransformPoint(GetWaypoint(0));

		g_prim.Draw(DebugCircle(pos, kUnitYAxis, radius, colors.m_groundLeg0));
		g_prim.Draw(DebugCross(pos, radius * 0.25f, colors.m_groundLeg0));
		return;
	}

	float legRadius = radius;
	float prevLegRadius = 0.0f;

	Point prevPos = pathSpace.TransformPoint(GetWaypoint(0));
	Point prevPrevPos = prevPos;

	Point prev0 = prevPos;
	Point prev1 = prevPos;

	for (int i = 1; i < count; ++i)
	{
		const Point pos = pathSpace.TransformPoint(GetWaypoint(i));
		const bool groundLeg = IsGroundLeg(i);

		const float nextLegRadius = ((i < (count - 1)) && groundLeg) ? radius : 0.0f;
		const Vector offset = (groundLeg || IsGroundLeg(i - 1)) ? Vector(0.0f, 0.2f, 0.0f) : kZero;

		if (legRadius > NDI_FLT_EPSILON)
		{
			g_prim.Draw(DebugLine(prevPos + offset, pos + offset, colors.m_groundLeg0, colors.m_groundLeg1, 3.0f), tt);

			const Vector leg = pos - prevPos;
			const Vector legDir = SafeNormalize(leg, kZero);
			const float legLen = Length(leg);

			const Vector perp = Cross(kUnitYAxis, leg);
			const Vector perpDir = SafeNormalize(perp, kZero);

			Point p00 = prevPos + (perpDir * radius);
			Point p01 = pos + (perpDir * radius);

			Point p10 = prevPos - (perpDir * radius);
			Point p11 = pos - (perpDir * radius);

			float t = -1.0f;

			if (nextLegRadius > NDI_FLT_EPSILON)
			{
				const Point nextPos = pathSpace.TransformPoint(GetWaypoint(i));

				if (IntersectSegmentStadiumXz(Segment(p00, p01), Segment(pos, nextPos), radius, t))
				{
					p01 = Lerp(p00, p01, t);
				}

				if (IntersectSegmentStadiumXz(Segment(p10, p11), Segment(pos, nextPos), radius, t))
				{
					p11 = Lerp(p10, p11, t);
				}
			}
			else if (legLen > 0.001f)
			{
				DebugDrawHalfCircle(pos + offset, prevPos - pos, radius, colors.m_groundLeg1, tt);
			}

			if (prevLegRadius > NDI_FLT_EPSILON)
			{
				const Segment probeSegRev0 = Segment(p01 + (SafeNormalize(p00 - p01, kZero) * 0.01f), p00);

				if (IntersectSegmentStadiumXz(probeSegRev0, Segment(prevPrevPos, prevPos), radius, t))
				{
					p00 = Lerp(probeSegRev0.a, probeSegRev0.b, t);
				}

				const Segment probeSegRev1 = Segment(p11 + (SafeNormalize(p10 - p11, kZero) * 0.01f), p10);

				if (IntersectSegmentStadiumXz(probeSegRev1, Segment(prevPrevPos, prevPos), radius, t))
				{
					p10 = Lerp(probeSegRev1.a, probeSegRev1.b, t);
				}
			}
			else if (legLen > 0.001f)
			{
				DebugDrawHalfCircle(prevPos + offset, pos - prevPos, radius, colors.m_groundLeg0, tt);
			}
/*
			else if (i < (count - 1))
			{
				const Point nextPos = GetWaypoint(i + 1);
				DebugDrawHalfCircle(prevPos + offset, nextPos - prevPos, radius, colors.m_groundLeg0, tt);
			}
*/

			p00 = PointFromXzAndY(p00, prevPos);
			p10 = PointFromXzAndY(p10, prevPos);

			p01 = PointFromXzAndY(p01, pos);
			p11 = PointFromXzAndY(p11, pos);

			g_prim.Draw(DebugLine(p00 + offset, p01 + offset, colors.m_groundLeg0, colors.m_groundLeg1), tt);
			g_prim.Draw(DebugLine(p10 + offset, p11 + offset, colors.m_groundLeg0, colors.m_groundLeg1), tt);

			if (prevLegRadius > NDI_FLT_EPSILON)
			{
				const Vector v0start = SafeNormalize(prev0 - prevPos, kZero);
				const Vector v0end = SafeNormalize(p00 - prevPos, kZero);
				const Vector v1start = SafeNormalize(prev1 - prevPos, kZero);
				const Vector v1end = SafeNormalize(p10 - prevPos, kZero);

				const bool inside = Dot(kUnitYAxis, Cross(prevPos - prevPrevPos, pos - prevPos)) < 0.0f;

				for (int j = 1; j < 11; ++j)
				{
					const float t0 = float(j - 1) * 0.1f;
					const float t1 = float(j) * 0.1f;

					const Vector v00 = Slerp(v0start, v0end, t0) * radius;
					const Vector v01 = Slerp(v0start, v0end, t1) * radius;
					const Vector v10 = Slerp(v1start, v1end, t0) * radius;
					const Vector v11 = Slerp(v1start, v1end, t1) * radius;
					const Color clr = Slerp(colors.m_groundLeg0, colors.m_groundLeg1, t1);

					if (inside)
					{
						g_prim.Draw(DebugLine(prevPos + v00 + offset, prevPos + v01 + offset, clr, clr), tt);
					}
					else
					{
						g_prim.Draw(DebugLine(prevPos + v10 + offset, prevPos + v11 + offset, clr, clr), tt);
					}
				}
			}

			prev0 = p01;
			prev1 = p11;
		}
		else if (IsGroundLeg(i - 1))
		{
			g_prim.Draw(DebugLine(prevPos + offset, pos + offset, colors.m_groundLeg0, colors.m_groundLeg1, 3.0f), tt);
		}
#if ENABLE_NAV_LEDGES
		else if (IsLedgeShimmy(i - 1))
		{
			g_prim.Draw(DebugLine(prevPos + offset, pos + offset, colors.m_ledgeShimmy0, colors.m_ledgeShimmy1, 3.0f), tt);
		}
		else if (IsLedgeJump(i - 1))
		{
			g_prim.Draw(DebugLine(prevPos + offset, pos + offset, colors.m_ledgeJump0, colors.m_ledgeJump1, 3.0f), tt);
		}
#endif
		else
		{
			// ap leg
			g_prim.Draw(DebugLine(prevPos + offset, pos + offset, colors.m_apLeg0, colors.m_apLeg1, 3.0f), tt);
		}

		prevLegRadius = legRadius;
		legRadius = nextLegRadius;

		prevPrevPos = prevPos;
		prevPos = pos;
	}

	if (drawDetails)
	{
		for (int i = 0; i < count; ++i)
		{
			const NavPathNode::NodeType nodeType = GetNodeType(i);

			const Point pos = pathSpace.TransformPoint(GetWaypoint(i));
			const Vector offset = Vector(0, 0.5f + (float(nodeType) * 0.05f) + (float(i % 5) * 0.15f), 0);

			g_prim.Draw(DebugCross(pos, 0.05f, kColorWhiteTrans), tt);
			g_prim.Draw(DebugLine(pos, offset, kColorWhiteTrans), tt);

			TextPrinterWorldSpace tp = TextPrinterWorldSpace(tt);
			tp.Start(pos + offset);
			tp.SetTextScale(0.6f);

			const NavManagerId navId = GetNavId(i);

			tp.PrintF(kColorWhiteTrans, "%d : %s", (int)i, NavPathNode::GetNodeTypeStr(nodeType));
			if (NavPathNode::IsActionPackNode(nodeType))
			{
				ActionPackHandle hAp = GetActionPackHandle(i);
				if (const TraversalActionPack* pTap = hAp.ToActionPack<TraversalActionPack>())
				{
					tp.PrintF(kColorWhiteTrans, " [%s] 0x%x", pTap->GetName(), hAp.GetMgrId());
				}
				else
				{
					tp.PrintF(kColorWhiteTrans, " 0x%x", hAp.GetMgrId());
				}
			}
			tp.PrintF(kColorWhiteTrans, "\n");

			if (navId.IsValid())
			{
				tp.PrintF(kColorWhiteTrans, "iMesh : %d\n", navId.m_navMeshIndex);
				tp.PrintF(kColorWhiteTrans, "iPoly : %d\n", navId.m_iPoly);

				if (navId.m_iPolyEx > 0)
				{
					tp.PrintF(kColorWhiteTrans, "iPolyEx : %d\n", navId.m_iPolyEx);
				}
			}

#if ENABLE_NAV_LEDGES
			const NavLedgeHandle hNavLedge = GetNavLedgeHandle(i);
			if (!hNavLedge.IsNull())
			{
				tp.PrintF(kColorWhiteTrans, "iGraph : %d\n", hNavLedge.GetGraphIndex());
				tp.PrintF(kColorWhiteTrans, "iLedge : %d\n", hNavLedge.GetLedgeId());
			}
#endif
		}
	}
}
