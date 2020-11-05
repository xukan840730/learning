/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-location.h"

#include "ndlib/math/pretty-math.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/debug/ai-msg-log.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-defines.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-mgr.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-util.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly.h"

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLocation::IsValid() const
{
	return IsReasonable(m_posPs) && ParentClass::IsValid();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavLocation::GetPosWs() const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	Point posWs = m_posPs;
	const Type type = GetType();

	switch (type)
	{
	case Type::kNavPoly:
		{
			if (const NavMesh* pNavMesh = ToNavMesh())
			{
				posWs = pNavMesh->ParentToWorld(m_posPs);
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case Type::kNavLedge:
		{
			if (const NavLedgeGraph* pLedgeGraph = ToNavLedgeGraph())
			{
				posWs = pLedgeGraph->ParentToWorld(m_posPs);
			}
		}
		break;
#endif
	}

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetPs(Point_arg posPs, NavPolyHandle hNavPoly)
{
	m_posPs = posPs;

	SetNavPoly(hNavPoly);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetPs(Point_arg posPs, const NavPoly* pNavPoly)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	m_posPs = posPs;

	SetNavPoly(pNavPoly);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetPs(Point_arg posPs, const NavMesh* pNavMesh)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (!pNavMesh)
		return;

	if (const NavPoly* pPoly = pNavMesh->FindContainingPolyPs(posPs))
	{
		SetPs(posPs, pPoly);
	}
	else
	{
		SetWs(pNavMesh->ParentToWorld(posPs));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetPs(Point_arg posPs, const NavHandle& navHandle)
{
	m_posPs = posPs;

	*(NavHandle*)this = navHandle;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetWs(Point_arg posWs)
{
	m_posPs = posWs;

	SetNone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetWs(Point_arg posWs, NavPolyHandle hNavPoly)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (const NavMesh* pNavMesh = hNavPoly.ToNavMesh())
	{
		m_posPs = pNavMesh->WorldToParent(posWs);

		SetNavPoly(hNavPoly);
	}
	else
	{
		SetWs(posWs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetWs(Point_arg posWs, const NavPoly* pNavPoly)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	const NavMesh* pNavMesh = pNavPoly ? pNavPoly->GetNavMesh() : nullptr;
	if (pNavMesh)
	{
		m_posPs = pNavMesh->WorldToParent(posWs);

		SetNavPoly(pNavPoly);
	}
	else
	{
		SetWs(posWs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetWs(Point_arg posWs, const NavMesh* pNavMesh)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (!pNavMesh)
		return;

	const Point posPs = pNavMesh->WorldToParent(posWs);

	if (const NavPoly* pPoly = pNavMesh->FindContainingPolyPs(posPs))
	{
		SetPs(posPs, pPoly);
	}
	else
	{
		SetWs(posWs);
	}
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetPs(Point_arg posPs, NavLedgeHandle hLedge)
{
	m_posPs = posPs;

	SetNavLedge(hLedge);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetPs(Point_arg posPs, const NavLedge* pNavLedge)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	m_posPs = posPs;

	SetNavLedge(pNavLedge);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetPs(Point_arg posPs, const NavLedgeGraph* pLedgeGraph)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (!pLedgeGraph)
		return;

	if (const NavLedge* pLedge = pLedgeGraph->FindClosestLedgePs(posPs, 0.25f))
	{
		SetPs(posPs, pLedge);
	}
	else
	{
		SetWs(pLedgeGraph->ParentToWorld(posPs));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetWs(Point_arg posWs, NavLedgeHandle hLedge)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (const NavLedgeGraph* pGraph = hLedge.ToLedgeGraph())
	{
		m_posPs = pGraph->WorldToParent(posWs);

		SetNavLedge(hLedge);
	}
	else
	{
		SetWs(posWs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetWs(Point_arg posWs, const NavLedge* pNavLedge)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (pNavLedge)
	{
		const NavLedgeGraph* pGraph = pNavLedge->GetNavLedgeGraphHandle().ToLedgeGraph();

		m_posPs = pGraph->WorldToParent(posWs);

		SetNavLedge(pNavLedge);
	}
	else
	{
		SetWs(posWs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetWs(Point_arg posWs, const NavLedgeGraph* pLedgeGraph)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (!pLedgeGraph)
		return;

	const Point posPs = pLedgeGraph->WorldToParent(posWs);

	if (const NavLedge* pLedge = pLedgeGraph->FindClosestLedgePs(posPs, 0.25f))
	{
		SetPs(posPs, pLedge);
	}
	else
	{
		SetWs(posWs);
	}
}
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::SetWs(Point_arg posWs, const NavHandle& navHandle)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	const Point posPs = navHandle.WorldToParent(posWs);
	SetPs(posPs, navHandle);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::DebugDraw(Color clr /* = kColorYellow */, DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	ParentClass::DebugDraw(clr, tt);

	const Point posWs = GetPosWs();
	g_prim.Draw(DebugCross(posWs, 0.25f, clr, kPrimEnableHiddenLineAlpha), tt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::DebugPrint(DoutBase* pDout) const
{
	STRIP_IN_FINAL_BUILD;

	if (!pDout)
		return;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (!IsValid())
	{
		pDout->Print("<Invalid>");
		return;
	}

	const Type type = GetType();

	pDout->Printf("%s", PrettyPrint(m_posPs, kPrettyPrintLowPrecision | kPrettyPrintUseParens | kPrettyPrintInsertCommas));

	if (IsNull())
	{
		pDout->Print(" [Null]");
	}
	else
	{
		switch (type)
		{
		case Type::kNavPoly:
			{
				if (const NavMesh* pMesh = ToNavMesh())
				{
					const NavPolyHandle hPoly = GetNavPolyHandle();
					pDout->Printf(" [%s iPoly %d]", pMesh->GetName(), hPoly.GetPolyId());
				}
			}
			break;
#if ENABLE_NAV_LEDGES
		case Type::kNavLedge:
			{
				if (const NavLedgeGraph* pGraph = ToNavLedgeGraph())
				{
					const NavLedgeHandle hLedge = GetNavLedgeHandle();
					pDout->Printf(" [%s iLedge %d]", pGraph->GetName(), hLedge.GetLedgeId());
				}
			}
			break;
#endif

		case Type::kNone:
			pDout->Printf(" [None]");
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLocation::DebugPrint(DoutMemChannels* pDout) const
{
	STRIP_IN_FINAL_BUILD;

	if (!pDout)
		return;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (!IsValid())
	{
		pDout->Appendf("<Invalid>");
		return;
	}

	const Type type = GetType();

	pDout->Appendf("%s", PrettyPrint(m_posPs, kPrettyPrintLowPrecision | kPrettyPrintUseParens | kPrettyPrintInsertCommas));

	if (IsNull())
	{
		pDout->Appendf(" [Null]");
	}
	else
	{
		switch (type)
		{
		case Type::kNavPoly:
			{
				if (const NavMesh* pMesh = ToNavMesh())
				{
					const NavPolyHandle hPoly = GetNavPolyHandle();
					pDout->Appendf(" [%s iPoly %d]", pMesh->GetName(), hPoly.GetPolyId());
				}
			}
			break;
#if ENABLE_NAV_LEDGES
		case Type::kNavLedge:
			{
				if (const NavLedgeGraph* pGraph = ToNavLedgeGraph())
				{
					const NavLedgeHandle hLedge = GetNavLedgeHandle();
					pDout->Appendf(" [%s iLedge %d]", pGraph->GetName(), hLedge.GetLedgeId());
				}
			}
			break;
#endif

		case Type::kNone:
			pDout->Appendf(" [None]");
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLocation NavUtil::ConstructNavLocation(Point_arg posWs,
										  NavLocation::Type navType,
										  F32 convolutionRadius,
										  Segment probeSegment /* = kZero */,
										  F32 findMeshRadius /* = 1.0f */,
										  F32 findMeshYThreshold /* = 2.0f */,
										  Nav::StaticBlockageMask staticBlockageMask /* = kStaticBlockageMaskAll */)
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	bool searchForNavPoly = false;
#if ENABLE_NAV_LEDGES
	bool searchForNavLedge = false;
#endif

	switch (navType)
	{
	case NavLocation::Type::kNavPoly:
		searchForNavPoly = true;
#if ENABLE_NAV_LEDGES
		searchForNavLedge = false;
#endif
		break;

#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		searchForNavPoly = false;
		searchForNavLedge = true;
		break;
#endif

	default:
		searchForNavPoly = true;
#if ENABLE_NAV_LEDGES
		searchForNavLedge = true;
#endif
		break;
	}

	FindBestNavMeshParams nmParams;
	NavMesh::FindPointParams polyParams;
	const NavMesh* pMesh = nullptr;

	nmParams.m_pointWs = posWs;
	nmParams.m_cullDist = findMeshRadius;
	nmParams.m_yThreshold = findMeshYThreshold;
	nmParams.m_bindSpawnerNameId = Nav::kMatchAllBindSpawnerNameId;
	nmParams.m_obeyedStaticBlockers = staticBlockageMask;

	if (searchForNavPoly)
	{
		nmMgr.FindNavMeshWs(&nmParams);

		if (nmParams.m_pNavMesh && nmParams.m_pNavPoly)
		{
			pMesh = nmParams.m_pNavMesh;

			polyParams.m_point = pMesh->WorldToLocal(nmParams.m_nearestPointWs);
			polyParams.m_depenRadius = convolutionRadius;
			polyParams.m_searchRadius = 1.0f;
			polyParams.m_crossLinks = true;
			polyParams.m_capsuleSegment = probeSegment;
			polyParams.m_obeyedStaticBlockers = staticBlockageMask;

			pMesh->FindNearestPointLs(&polyParams);
		}
	}
#if ENABLE_NAV_LEDGES
	FindNavLedgeGraphParams ledgeParams;

	if (searchForNavLedge)
	{
		ledgeParams.m_pointWs = posWs;
		ledgeParams.m_searchRadius = 1.0f;
		ledgeParams.m_bindSpawnerNameId = Nav::kMatchAllBindSpawnerNameId;

		NavLedgeGraphMgr::Get().FindLedgeGraph(&ledgeParams);
	}
#endif

	NavLocation ret;

#if ENABLE_NAV_LEDGES
	if (polyParams.m_pPoly && ledgeParams.m_pNavLedge)
	{
		const Point polyPosWs = pMesh->LocalToWorld(polyParams.m_nearestPoint);

		if (Dist(posWs, polyPosWs) < Dist(posWs, ledgeParams.m_nearestPointWs))
		{
			const NavMesh* pDestMesh = polyParams.m_pPoly->GetNavMesh();
			ret.SetPs(pDestMesh->WorldToParent(polyPosWs), polyParams.m_pPoly);
		}
		else
		{
			ret.SetPs(ledgeParams.m_pLedgeGraph->WorldToParent(ledgeParams.m_nearestPointWs), ledgeParams.m_pNavLedge);
		}
	}
	else
#endif
	if (polyParams.m_pPoly)
	{
		const Point polyPosWs = pMesh->LocalToWorld(polyParams.m_nearestPoint);

		const NavMesh* pDestMesh = polyParams.m_pPoly->GetNavMesh();
		ret.SetPs(pDestMesh->WorldToParent(polyPosWs), polyParams.m_pPoly);
	}
#if ENABLE_NAV_LEDGES
	else if (ledgeParams.m_pNavLedge)
	{
		ret.SetPs(ledgeParams.m_pLedgeGraph->WorldToParent(ledgeParams.m_nearestPointWs), ledgeParams.m_pNavLedge);
	}
#endif
	else
	{
		ret.SetWs(posWs);
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLocation NavUtil::TryAdjustNavLocationPs(NavLocation startingLoc, Vector_arg moveVecPs)
{
	const NavLocation::Type navType = startingLoc.GetType();
	NavLocation ret = startingLoc;
	const Point startingPosPs = startingLoc.GetPosPs();

	switch (navType)
	{
	case NavLocation::Type::kNavPoly:
		{
			if (const NavPoly* pPoly = startingLoc.ToNavPoly())
			{
				const NavMesh* pMesh = pPoly->GetNavMesh();

				NavMesh::ProbeParams probeParams;
				probeParams.m_start = pMesh->ParentToLocal(startingPosPs);
				probeParams.m_move = moveVecPs;
				probeParams.m_pStartPoly = pPoly;

				const NavMesh::ProbeResult res = pMesh->ProbeLs(&probeParams);

				ret.UpdatePosPs(startingPosPs + moveVecPs);

				if (res == NavMesh::ProbeResult::kReachedGoal)
				{
					ret.SetNavPoly(probeParams.m_pReachedPoly);
				}
				else
				{
					ret.SetNone();
				}
			}
			else
			{
				ret.SetNone();
				ret.UpdatePosPs(startingPosPs + moveVecPs);
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
#endif
	case NavLocation::Type::kNone:
		{
			ret.UpdatePosPs(startingPosPs + moveVecPs);
		}
		break;
	}

	return ret;
}
