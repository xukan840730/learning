/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-control.h"

#include "corelib/containers/fixedsizeheap.h"

#include "ndlib/math/pretty-math.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/ai/agent/nav-character-util.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/faction-mgr.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/find-reachable-polys.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-mgr.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nav/nav-defines.h"
#include "gamelib/gameplay/nav/nav-ex-data.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-mgr.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-util.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-patch.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-build.h"
#include "gamelib/gameplay/nav/nav-path-find-job.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/path-waypoints-ex.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-subsystem-anim-action.h"
#include "gamelib/tasks/task-graph-mgr.h"
#include "gamelib/tasks/task-subnode.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::NavFlags::UpdateFrom(const NavMesh* pMesh, const NavPoly* pPoly)
{
	if (!pMesh || !pPoly)
	{
		m_navMesh.m_forcedDemeanorId  = INVALID_STRING_ID_64;

		m_navMesh.m_forcedSwim		  = false;
		m_navMesh.m_forcedDive		  = false;
		m_navMesh.m_forcedWalk		  = false;
		m_navMesh.m_forcedWade		  = false;
		m_navMesh.m_forcedCrouch      = false;
		m_navMesh.m_forcedProne       = false;

		m_navMesh.m_stealthVegetation = false;
	}
	else
	{
		m_navMesh.m_forcedDemeanorId  = AI::NavMeshForcedDemeanor(pMesh);

		m_navMesh.m_forcedSwim		  = pMesh->NavMeshForcesSwim();
		m_navMesh.m_forcedDive		  = pMesh->NavMeshForcesDive();
		m_navMesh.m_forcedWalk		  = pMesh->NavMeshForcesWalk();
		m_navMesh.m_forcedWade		  = AI::NavPolyForcesWade(pPoly);
		m_navMesh.m_forcedCrouch      = pMesh->NavMeshForcesCrouch();
		m_navMesh.m_forcedProne       = pMesh->NavMeshForcesProne();

		m_navMesh.m_stealthVegetation = pPoly->IsStealth();
	}
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::NavFlags::UpdateFrom(const NavLedgeGraph* pGraph, const NavLedge* pLedge)
{
	Reset();
}
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
SimpleNavControl::SimpleNavControl()
	: m_parentSpace(kIdentity)
	, m_lastPosPs(kOrigin)
	, m_ownerProcessId(0)
	, m_factionId(0)
	, m_navMeshAutoRebind(false)
	, m_addEnemyNavBlockers(false)
	, m_isSimple(true) // poor man's RTTI
	, m_activeNavType(NavLocation::Type::kNavPoly)
	, m_everHadValidNavMesh(false)
	, m_ignoreStaticNavBlocker(false)
	, m_ignorePlayerNavBlocker(false)
	, m_ownerProcessType(INVALID_STRING_ID_64)
	, m_obeyedBlockerFilterFunc(DefaultNavBlockerFilter)
	, m_obeyedBlockerFilterUserData(0)
	, m_movingNavAdjustRadius(0.0f)
	, m_idleNavAdjustRadius(-1.0f)
	, m_desNavAdjustRadius(0.0f)
	, m_curNavAdjustRadius(0.0f)
	, m_pathFindRadius(-1.0f)
	, m_minNpcStature(NavMesh::NpcStature::kProne)
	, m_obeyedStaticBlockersOverrideFrame(-1)
	, m_obeyedStaticBlockersOverride(0)
{
	m_cachedNavFlags.Reset();
	m_obeyedNavBlockers.ClearAllBits();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavControl::NavControl()
	: SimpleNavControl()
	, m_pathYThreshold(2.0f)
	, m_pathXZCullRadius(3.0f)
	, m_searchBindSpawnerNameId(Nav::kMatchAllBindSpawnerNameId)
	, m_traversalSkillMask(~0)
	, m_smoothPath(Nav::kFullSmoothing)
{
	m_isSimple = false; // poor man's RTTI

	ClearUndirectedPlayerBlockageCost();
	ClearDirectedPlayerBlockageCost();
	ClearCareAboutPlayerBlockageDespitePushingPlayer();
	ClearCachedPathPolys();

	m_truncateAfterTap = 2;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::ResetNavMesh(const NavMesh* pMesh, NdGameObject* pGameObj)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (pGameObj)
	{
		m_ownerProcessId = pGameObj->GetProcessId();
		m_ownerProcessType = pGameObj->GetTypeNameId();
	}

	const NavMesh* pPrevMesh = GetNavMesh();

	const NavPoly* pPoly = pMesh ? pMesh->FindContainingPolyPs(m_lastPosPs) : nullptr;

	SetNavPoly(pGameObj, m_lastPosPs, pPoly);

	if (pMesh != pPrevMesh)
	{
		AiLogNav(NavCharacter::FromProcess(pGameObj),
				 "ResetNavMesh @ %s : '%s' -> '%s' [poly: %d]\n",
				 PrettyPrint(m_lastPosPs),
				 pPrevMesh ? pPrevMesh->GetName() : nullptr,
				 pMesh ? pMesh->GetName() : nullptr,
				 pPoly ? pPoly->GetId() : -1);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavControl::HasNavLocationTurnedInvalid() const
{
	bool unloaded = false;

	switch (m_navLocation.GetType())
	{
	case NavLocation::Type::kNavPoly:
		unloaded = !m_navLocation.GetNavMeshHandle().IsNull() && !m_navLocation.GetNavMeshHandle().IsValid();
		break;
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		unloaded = !m_navLocation.GetNavLedgeGraphHandle().IsNull() && !m_navLocation.GetNavLedgeGraphHandle().IsValid();
		break;
#endif
	}

	return unloaded;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::SetNavPoly(const NdGameObject* pGameObj, Point_arg posPs, const NavPoly* pPoly)
{
	NAV_ASSERT(IsReasonable(posPs));

	const NavMesh* pOldMesh = m_navLocation.ToNavMesh();

	NavLocation newNavLoc;
	newNavLoc.SetPs(posPs, pPoly);

	SetNavLocation(pGameObj, newNavLoc);

	const NavMesh* pNewMesh = pPoly ? pPoly->GetNavMesh() : nullptr;

	if (pOldMesh != pNewMesh)
	{
		bool dead = false;
		if (const NavCharacter* pNavChar = NavCharacter::FromProcess(pGameObj))
			dead = pNavChar->IsDead();

		if (pOldMesh)
		{
			pOldMesh->DecOccupancyCount();
		}

		if (pNewMesh && !dead)
		{
			m_everHadValidNavMesh = true;
			pNewMesh->IncOccupancyCount();
		}
	}

	m_cachedNavFlags.UpdateFrom(pNewMesh, pPoly);
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::SetNavLedge(const NdGameObject* pGameObj, Point_arg posPs, const NavLedge* pLedge)
{
	NAV_ASSERT(IsReasonable(posPs));

	if (const NavMesh* pOldMesh = m_navLocation.ToNavMesh())
	{
		pOldMesh->DecOccupancyCount();
	}

	NavLocation newNavLoc;
	newNavLoc.SetPs(posPs, pLedge);

	SetNavLocation(pGameObj, newNavLoc);

	m_cachedNavFlags.UpdateFrom(pLedge->GetNavLedgeGraph(), pLedge);
}
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::SetPosWs(const NdGameObject* pGameObj, Point_arg posWs)
{
	NAV_ASSERT(IsReasonable(posWs));

	if (const NavMesh* pOldMesh = m_navLocation.ToNavMesh())
	{
		pOldMesh->DecOccupancyCount();
	}

	NavLocation newNavLoc;
	newNavLoc.SetWs(posWs);

	SetNavLocation(pGameObj, newNavLoc);

	m_cachedNavFlags.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::SetActiveNavType(const NdGameObject* pGameObj, const NavLocation::Type navType)
{
	if (m_activeNavType != navType)
	{
		AiLogNav(NavCharacter::FromProcess(pGameObj),
				 "SetActiveNavType '%s' -> %s'\n",
				 NavHandle::GetTypeName(m_activeNavType),
				 NavHandle::GetTypeName(navType));
		m_activeNavType = navType;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::SetNavLocation(const NdGameObject* pGameObj, const NavLocation& navLoc)
{
	NAV_ASSERT(IsReasonable(navLoc.GetPosPs()));

	const NavLocation prevNavLocation = m_navLocation;

	m_navLocation = navLoc;

	RefreshCachedNavFlags();

	if (FALSE_IN_FINAL_BUILD(!m_navLocation.SameShapeAs(prevNavLocation) && pGameObj
							 && pGameObj->IsKindOf(g_type_NavCharacter)))
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		AiLogNav(NavCharacter::FromProcess(pGameObj),
				 "SetNavLocation: nav shape changed from '%s' to '%s' @ %s\n",
				 prevNavLocation.GetShapeName(),
				 m_navLocation.GetShapeName(),
				 PrettyPrint(m_navLocation.GetPosPs()));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavControl::SetPathYThreshold(float thresh)
{
	NAV_ASSERT(thresh > 0.0f);
	m_pathYThreshold = thresh;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::UpdateNpcStature(const NdGameObject* const pGameObj)
{
	const NavCharacter* const pNavChar = NavCharacter::FromProcess(pGameObj);
	if (pNavChar)
	{
		m_minNpcStature = pNavChar->GetMinNavMeshStature();
	}
	else
	{
		m_minNpcStature = NavMesh::NpcStature::kProne;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::UpdatePs(Point_arg posPs, NdGameObject* pGameObj, const NavMesh* pAltMesh /* = nullptr */)
{
	if (!pGameObj)
	{
		SetPosWs(pGameObj, kOrigin);
		return;
	}

	if (const NavCharacter* const pNavChar = NavCharacter::FromProcess(pGameObj))
	{
		m_ignorePlayerNavBlocker = pNavChar->WantsToPushPlayerAway();
	}

	UpdateNavAdjustRadius(pGameObj);

	UpdateNpcStature(pGameObj);

	// not too sure if this is meant to be used for pathing or not
	m_obeyedNavBlockers = BuildObeyedBlockerList(false);

	NavLocation prevNavLocation = m_navLocation;

	m_parentSpace	   = pGameObj->GetParentSpace();
	m_ownerProcessId   = pGameObj->GetProcessId();
	m_ownerProcessType = pGameObj->GetTypeNameId();

	switch (m_activeNavType)
	{
	case NavLocation::Type::kNavPoly:
		UpdatePs_NavPoly(posPs, pGameObj, pAltMesh);
		break;
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		UpdatePs_NavLedge(posPs, pGameObj);
		break;
#endif
	default:
		SetPosWs(pGameObj, m_parentSpace.TransformPoint(posPs));
		break;
	}

	if (DynamicNavBlocker* pNavBlocker = pGameObj->GetNavBlocker())
	{
		if (pGameObj->IsNavBlockerEnabled())
		{
			pNavBlocker->SetNavPoly(m_navLocation.ToNavPoly());
		}
		else
		{
			pNavBlocker->SetNavPoly(nullptr);
		}
	}

	m_lastPosPs = m_navLocation.GetPosPs();

	if (m_navLocation.GetType() != NavLocation::Type::kNone)
	{
		m_lastGoodNavLocation = m_navLocation;
		m_lastGoodRotation	  = pGameObj->GetBoundFrame().GetRotation();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::UpdatePs_NavPoly(Point_arg posPs,
										NdGameObject* pGameObj,
										const NavMesh* pAltMesh /* = nullptr */)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());
	NAV_ASSERT(pGameObj);

	const NavMesh* pMesh = m_navLocation.ToNavMesh();
	const NavPoly* pPoly = m_navLocation.ToNavPoly();
	const StringId64 bindSpawnerNameId = pGameObj->GetBindSpawnerId();

	if (pPoly && pMesh)
	{
		if (pMesh->GetBindSpawnerNameId() != bindSpawnerNameId)
		{
			// force rebind
			pPoly = nullptr;
			pMesh = nullptr;
		}
		else if (pPoly->PolyContainsPointLs(pMesh->ParentToLocal(posPs)))
		{
			// our existing poly is good!
		}
		// if we have a start poly that contains the last point,
		else if (pPoly->PolyContainsPointLs(pMesh->ParentToLocal(m_lastPosPs)))
		{
			// use a navmesh probe to detect passing through navmesh links
			NavMesh::ProbeParams probe;
			probe.m_start	   = m_lastPosPs;
			probe.m_move	   = posPs - m_lastPosPs;
			probe.m_pStartPoly = pPoly;
			probe.m_obeyedStaticBlockers = GetObeyedStaticBlockers();

			const NavMesh::ProbeResult res = pMesh->ProbePs(&probe);

			if ((res != NavMesh::ProbeResult::kReachedGoal) || !probe.m_pReachedPoly)
			{
				pPoly = nullptr;
			}
			else
			{
				pPoly = probe.m_pReachedPoly;
				pMesh = pPoly->GetNavMesh();
			}
		}
		else
		{
			// force poly rebind
			pPoly = nullptr;
		}
	}

	// if we have a navmesh and no poly, search for nearest poly in the current mesh
	if (pMesh && !pPoly)
	{
		NavMesh::FindPointParams findPoly;
		findPoly.m_point		= posPs;
		findPoly.m_depenRadius	= GetMovingNavAdjustRadius();
		findPoly.m_searchRadius = findPoly.m_depenRadius + 0.5f;
		findPoly.m_crossLinks	= true;
		findPoly.m_obeyedStaticBlockers = GetObeyedStaticBlockers();

		pMesh->FindNearestPointPs(&findPoly);

		pPoly = findPoly.m_pPoly;
		pMesh = pPoly ? pPoly->GetNavMesh() : nullptr;

		if (pPoly && !pPoly->PolyContainsPointLs(pMesh->ParentToLocal(posPs)))
		{
			// force rebind
			pPoly = nullptr;
			pMesh = nullptr;
		}
	}

	// if we have a restricted alternate mesh, only look there for a potential binding (probably because we are using a TAP)
	if (!pPoly && pAltMesh)
	{
		NavMesh::FindPointParams findPoly;
		findPoly.m_point = posPs;
		findPoly.m_depenRadius = GetMovingNavAdjustRadius();
		findPoly.m_searchRadius = findPoly.m_depenRadius + 0.5f;
		findPoly.m_crossLinks = true;
		findPoly.m_obeyedStaticBlockers = GetObeyedStaticBlockers();

		pAltMesh->FindNearestPointPs(&findPoly);

		pPoly = findPoly.m_pPoly;
		pMesh = pPoly ? pPoly->GetNavMesh() : nullptr;
	}

	// if autoRebind enabled, and we get too far from current poly
	if (m_navMeshAutoRebind && pPoly)
	{
		Point nearestPosPs;
		pPoly->FindNearestPointPs(&nearestPosPs, posPs);
		if (Dist(posPs, nearestPosPs) > 1.0f)
		{
			// force rebind
			pPoly = nullptr;
			pMesh = nullptr;
		}
	}

	// last ditch effort to get us a valid nav poly...
	if (!pPoly && !pAltMesh)
	{
		const Point posWs = m_parentSpace.TransformPoint(posPs);

		FindBestNavMeshParams findNavMesh;
		findNavMesh.m_pointWs = posWs;
		findNavMesh.m_cullDist = 0.5f;
		findNavMesh.m_yThreshold = 2.0f;
		findNavMesh.m_bindSpawnerNameId = bindSpawnerNameId;
		findNavMesh.m_obeyedStaticBlockers = GetObeyedStaticBlockers();
		findNavMesh.m_swimMeshAllowed = m_canSwim;

		NavMeshMgr& mgr = *EngineComponents::GetNavMeshMgr();
		mgr.FindNavMeshWs(&findNavMesh);

		pMesh = findNavMesh.m_pNavMesh;
		pPoly = findNavMesh.m_pNavPoly;
	}

	NAV_ASSERTF(m_canSwim || !pMesh || (!pMesh->NavMeshForcesSwim() && !pMesh->NavMeshForcesDive()),
				("Npc '%s' bound to a %s nav mesh '%s' even though he can't swim!",
				 pGameObj->GetName(),
				 pMesh->NavMeshForcesDive() ? "dive" : "swim",
				 pMesh->GetName()));

	// update poly
	SetNavPoly(pGameObj, posPs, pPoly);
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::UpdatePs_NavLedge(Point_arg posPs, NdGameObject* pGameObj)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());
	NAV_ASSERT(pGameObj);

	const Point searchOriginPs = posPs;
	const Point searchOriginWs = m_parentSpace.TransformPoint(searchOriginPs);
	const float searchRadius   = 0.5f;

	const NavLedgeGraph* pGraph = m_navLocation.ToNavLedgeGraph();
	const NavLedge* pLedge		= nullptr;

	Point navPosPs = searchOriginPs;

	if (pGraph)
	{
		NavLedgeGraph::FindLedgeParams params;
		params.m_point		  = searchOriginPs;
		params.m_searchRadius = searchRadius;

		pGraph->FindClosestLedgePs(&params);

		navPosPs = params.m_nearestPoint;
	}

	if (!pLedge)
	{
		FindNavLedgeGraphParams findParams;

		findParams.m_bindSpawnerNameId = pGameObj->GetBindSpawnerId();
		findParams.m_pointWs	  = searchOriginWs;
		findParams.m_searchRadius = searchRadius;

		NavLedgeGraphMgr::Get().FindLedgeGraph(&findParams);

		pGraph = findParams.m_pLedgeGraph;
		pLedge = findParams.m_pNavLedge;

		navPosPs = m_parentSpace.UntransformPoint(findParams.m_nearestPointWs);
	}

	if (pLedge)
	{
		SetNavLedge(pGameObj, navPosPs, pLedge);
	}
	else
	{
		SetPosWs(pGameObj, searchOriginWs);
	}
}
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::SimpleUpdatePs(Point_arg posPs, const NavPoly* pPoly, NdGameObject* pGameObj)
{
	PROFILE(AI, NavControl_Update);

	m_parentSpace	   = pGameObj->GetParentSpace();
	m_ownerProcessId   = pGameObj->GetProcessId();
	m_ownerProcessType = pGameObj->GetTypeNameId();

	SetFactionId(pGameObj->GetFactionId().GetRawFactionIndex());

	UpdateNavAdjustRadius(pGameObj);

	SetNavPoly(pGameObj, posPs, pPoly);

	m_lastPosPs = posPs;

	m_obeyedNavBlockers = BuildObeyedBlockerList(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point SimpleNavControl::FindNearestPointOnNavMeshPs(Point_arg posPs) const
{
	Point outPoint		 = posPs;
	const NavMesh* pMesh = nullptr;
	const NavPoly* pPoly = m_navLocation.ToNavPoly(&pMesh);
	if (pMesh)
	{
		NavMesh::FindPointParams params;
		params.m_point		  = posPs;
		params.m_searchRadius = 1.0f;
		params.m_pStartPoly	  = pPoly;

		pMesh->FindNearestPointPs(&params);
		if (params.m_pPoly)
		{
			outPoint = params.m_nearestPoint;
		}
	}
	return outPoint;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavControl::IsNavBlockerIgnored(const DynamicNavBlocker* pBlocker) const
{
	if (!pBlocker)
		return false;

	const U32F ignoreId = pBlocker->GetIgnoreProcessId();

	if (0 == ignoreId)
		return false;

	bool ignored = false;

	for (U32F i = 0; i < kMaxNavBlockerIgnoreCount; ++i)
	{
		if (m_ignoreNavBlockerList[i].GetProcessId() == ignoreId)
		{
			ignored = true;
			break;
		}
	}

	return ignored;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavControl::FindReachablePolysInRadiusPs(NavLocation startLoc,
											  float radius,
											  NavPolyHandle* outList,
											  U32F maxPolyCount) const
{
	PROFILE(AI, NavCon_FindReachablePolysInRadius);

	Nav::FindReachablePolysParams findPolysParams;
	findPolysParams.m_context = GetPathFindContext(nullptr, m_parentSpace, DefaultNavBlockerFilter, 0);
	findPolysParams.AddStartPosition(startLoc);
	findPolysParams.m_radius = radius;
	findPolysParams.m_findPolyYThreshold = m_pathYThreshold;
	findPolysParams.m_bindSpawnerNameId	 = m_searchBindSpawnerNameId;
	findPolysParams.m_traversalSkillMask = GetTraversalSkillMask();
	findPolysParams.m_tensionMode		 = (*g_ndConfig.m_pGetTensionMode)();
	findPolysParams.m_factionMask		 = BuildFactionMask(GetFactionId());
	findPolysParams.m_playerBlockageCost = GetUndirectedPlayerBlockageCost();

	Vec4 costVecs[Nav::FindReachablePolysResults::kMaxPolyOutCount];
	const U32F numPolysOut = Min(Nav::FindReachablePolysResults::kMaxPolyOutCount, (U32)maxPolyCount);
	const U32F polyCount   = Nav::FindReachablePolys(outList, costVecs, numPolysOut, findPolysParams);
	return polyCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavControl::FindReachableActionPacksInRadiusWithTypeMaskPs(NavLocation startLoc,
																float radius,
																U32F typeMask,
																ActionPack** ppOutList,
																U32F maxCount) const
{
	PROFILE(AI, FindReachableActionPacksInRad);

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	FindReachableActionPacksParams findApsParams;
	findApsParams.m_context = GetPathFindContext(nullptr, m_parentSpace, DefaultNavBlockerFilter, 0);
	findApsParams.m_context.m_dynamicSearch = false;
	findApsParams.AddStartPosition(startLoc);
	findApsParams.m_radius = radius;
	findApsParams.m_findPolyYThreshold = m_pathYThreshold;
	findApsParams.m_bindSpawnerNameId  = m_searchBindSpawnerNameId;
	findApsParams.m_traversalSkillMask = GetTraversalSkillMask();
	findApsParams.m_tensionMode		   = (*g_ndConfig.m_pGetTensionMode)();
	findApsParams.m_factionMask		   = BuildFactionMask(GetFactionId());
	findApsParams.m_actionPackTypeMask = typeMask;
	findApsParams.m_actionPackEntryDistance = GetMaximumNavAdjustRadius();
	findApsParams.m_playerBlockageCost = GetUndirectedPlayerBlockageCost();

	const U32F actionPackCount = FindReachableActionPacks(ppOutList, maxCount, findApsParams);
	return actionPackCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavControl::FindReachableActionPacksInRadiusPs(NavLocation startLoc,
													float radius,
													ActionPack** ppOutList,
													U32F maxCount) const
{
	return FindReachableActionPacksInRadiusWithTypeMaskPs(startLoc, radius, 0xffffffff, ppOutList, maxCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavControl::FindReachableActionPacksByTypeInRadiusPs(NavLocation startLoc,
														  float radius,
														  ActionPack::Type tt,
														  ActionPack** ppOutList,
														  U32F maxCount) const
{
	return FindReachableActionPacksInRadiusWithTypeMaskPs(startLoc, radius, (1 << (tt & 0xff)), ppOutList, maxCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavControl::ClearLineOfMotionPs(Point_arg startPosPs,
										   Point_arg endPosPs,
										   bool radialProbe /* = true */,
										   Point* pReachedPosOut /* = nullptr */) const
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	bool clearLine	   = false;
	Point reachedPoint = startPosPs;

	const NavMesh* pMesh = m_navLocation.ToNavMesh();
	if (pMesh)
	{
		NavMesh::ProbeParams params;
		params.m_start		 = startPosPs;
		params.m_move		 = endPosPs - startPosPs;
		params.m_probeRadius = radialProbe ? m_curNavAdjustRadius : 0.0f;
		params.m_obeyedBlockers = m_obeyedNavBlockers;
		params.m_dynamicProbe	= true;
		params.m_obeyedStaticBlockers = GetObeyedStaticBlockers();

		const NavMesh::ProbeResult res = pMesh->ProbePs(&params);

		reachedPoint = params.m_endPoint;
		clearLine	 = res == NavMesh::ProbeResult::kReachedGoal;
	}

	if (pReachedPosOut)
		*pReachedPosOut = reachedPoint;

	return clearLine;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavControl::StaticClearLineOfMotionPs(Point_arg startPosPs,
												 Point_arg endPosPs,
												 bool radialProbe /* = true */,
												 NavMesh::ProbeParams* pResults /* = nullptr */) const
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	bool clearLine	   = false;
	Point reachedPoint = startPosPs;

	const NavMesh* pMesh = m_navLocation.ToNavMesh();
	if (pMesh)
	{
		NavMesh::ProbeParams params;
		params.m_start		 = startPosPs;
		params.m_move		 = endPosPs - startPosPs;
		params.m_probeRadius = radialProbe ? m_curNavAdjustRadius : 0.0f;
		params.m_obeyedBlockers.ClearAllBits();
		params.m_dynamicProbe = false;
		params.m_obeyedStaticBlockers = GetObeyedStaticBlockers();

		const NavMesh::ProbeResult res = pMesh->ProbePs(&params);

		reachedPoint = params.m_endPoint;
		clearLine = res == NavMesh::ProbeResult::kReachedGoal;

		if (pResults)
			*pResults = params;
	}

	return clearLine;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavControl::DynamicClearLineOfMotionPs(Point_arg startPosPs,
												  Point_arg endPosPs,
												  bool radialProbe /* = true */,
												  NavMesh::ProbeParams* pResults /*= nullptr */) const
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	bool clearLine = false;

	const NavMesh* pMesh = m_navLocation.ToNavMesh();
	if (pMesh)
	{
		NavMesh::ProbeParams params;
		params.m_start		  = startPosPs;
		params.m_move		  = endPosPs - startPosPs;
		params.m_probeRadius  = radialProbe ? m_curNavAdjustRadius : 0.0f;
		params.m_dynamicProbe = true;
		params.m_obeyedBlockers		  = m_obeyedNavBlockers;
		params.m_obeyedStaticBlockers = GetObeyedStaticBlockers();

		const NavMesh::ProbeResult res = pMesh->ProbePs(&params);

		if (pResults)
			*pResults = params;

		clearLine = res == NavMesh::ProbeResult::kReachedGoal;
	}

	return clearLine;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavControl::BlockersOnlyClearLineOfMotion(Point_arg startPosPs, Point_arg endPosPs) const
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	PROFILE(AI, BlockersOnlyClearLineOfMotion);

	const bool clearLine = true;

	return clearLine;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavControl::IsPointDynamicallyBlockedPs(Point posPs) const
{
	bool blocked = false;

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const Point posWs = m_parentSpace.TransformPoint(posPs);

	const NavMesh* pMesh = nmMgr.FindNavMeshWs(posWs);
	if (pMesh)
	{
		NavMesh::ClearanceParams params;
		params.m_point		  = posPs;
		params.m_radius		  = m_curNavAdjustRadius;
		params.m_dynamicProbe = true;
		params.m_obeyedBlockers		  = m_obeyedNavBlockers;
		params.m_obeyedStaticBlockers = GetObeyedStaticBlockers();

		const NavMesh::ProbeResult res = pMesh->CheckClearancePs(&params);

		blocked = res != NavMesh::ProbeResult::kReachedGoal;
	}

	return blocked;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavControl::IsPointStaticallyBlockedPs(Point posPs, float radius) const
{
	bool blocked = false;

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const Point posWs = m_parentSpace.TransformPoint(posPs);

	const NavMesh* pMesh = nmMgr.FindNavMeshWs(posWs);
	if (pMesh)
	{
		NavMesh::ClearanceParams params;
		params.m_point		  = posPs;
		params.m_radius		  = radius < 0.0f ? m_curNavAdjustRadius : radius;
		params.m_dynamicProbe = false;
		params.m_obeyedStaticBlockers = GetObeyedStaticBlockers();

		const NavMesh::ProbeResult res = pMesh->CheckClearancePs(&params);

		blocked = res != NavMesh::ProbeResult::kReachedGoal;
	}

	return blocked;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavControl::IsNavLocationBlocked(const NavLocation loc, bool dynamic) const
{
	bool blocked = false;

	if (loc.GetType() == NavLocation::Type::kNavPoly)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		const NavPoly* pPoly = loc.ToNavPoly();
		const NavMesh* pMesh = pPoly ? pPoly->GetNavMesh() : nullptr;

		if (pMesh)
		{
			NavMesh::ClearanceParams params;
			params.m_pStartPoly = pPoly;
			params.m_point		= loc.GetPosPs();
			params.m_radius		= m_curNavAdjustRadius;
			params.m_obeyedStaticBlockers = GetObeyedStaticBlockers();

			if (dynamic)
			{
				params.m_dynamicProbe	= dynamic;
				params.m_obeyedBlockers = m_obeyedNavBlockers;
			}

			const NavMesh::ProbeResult res = pMesh->CheckClearancePs(&params);

			blocked = res != NavMesh::ProbeResult::kReachedGoal;
		}
	}

	return blocked;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavControl::IsCurNavLocationBlocked(const NdGameObject* pOwner, bool debugDraw /* = false */) const
{
	bool blocked = false;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (const NavMesh* pMesh = m_navLocation.ToNavMesh())
	{
		NavMesh::ClearanceParams params;
		params.m_point	= pMesh->WorldToParent(m_navLocation.GetPosWs());
		params.m_radius = m_curNavAdjustRadius;
		params.m_obeyedStaticBlockers = GetObeyedStaticBlockers();
		params.m_dynamicProbe		  = !m_obeyedNavBlockers.AreAllBitsClear();
		params.m_obeyedBlockers		  = m_obeyedNavBlockers;

		if (const NavCharacter* pNavChar = NavCharacter::FromProcess(pOwner))
		{
			params.m_capsuleSegment = pNavChar->GetDepenetrationSegmentPs(params.m_point);
		}

		const NavMesh::ProbeResult res = pMesh->CheckClearancePs(&params);

		if (res != NavMesh::ProbeResult::kReachedGoal)
		{
			blocked = true;
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			if (Dist(params.m_capsuleSegment.a, params.m_capsuleSegment.b) > NDI_FLT_EPSILON)
			{
				DebugDrawFlatCapsule(pMesh->ParentToWorld(params.m_capsuleSegment.a),
									 pMesh->ParentToWorld(params.m_capsuleSegment.b),
									 params.m_radius,
									 blocked ? kColorRed : kColorGreen,
									 kPrimEnableHiddenLineAlpha);
			}
			else
			{
				g_prim.Draw(DebugCircle(pMesh->ParentToWorld(params.m_point + Vector(0.0f, 0.1f, 0.0f)),
										pMesh->ParentToWorld(kUnitYAxis),
										params.m_radius,
										blocked ? kColorRed : kColorGreen,
										kPrimEnableHiddenLineAlpha));
			}
		}
	}
	else
	{
		blocked = true;
	}

	return blocked;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::RefreshCachedNavFlags()
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	m_cachedNavFlags.Reset();

	switch (m_navLocation.GetType())
	{
	case NavLocation::Type::kNavPoly:
		{
			if (const NavPoly* pPoly = m_navLocation.ToNavPoly())
			{
				m_cachedNavFlags.UpdateFrom(pPoly->GetNavMesh(), pPoly);
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		{
			if (const NavLedge* pLedge = m_navLocation.ToNavLedge())
			{
				m_cachedNavFlags.UpdateFrom(pLedge->GetNavLedgeGraph(), pLedge);
			}
		}
		break;
#endif
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::ConfigureNavRadii(float movingNavRadius, float idleNavRadius, float pathFindRadius)
{
	NAV_ASSERT(IsReasonable(movingNavRadius));
	NAV_ASSERT(IsReasonable(idleNavRadius));
	NAV_ASSERT(movingNavRadius >= 0.0f);

	m_movingNavAdjustRadius = movingNavRadius;
	m_idleNavAdjustRadius	= idleNavRadius;
	m_pathFindRadius		= pathFindRadius;

	const float maxNavAdjustRadius = GetMaximumNavAdjustRadius();

	m_desNavAdjustRadius = Min(m_desNavAdjustRadius, maxNavAdjustRadius);
	m_curNavAdjustRadius = Min(m_curNavAdjustRadius, maxNavAdjustRadius);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::UpdateNavAdjustRadius(const NdGameObject* pGameObj)
{
	const NavCharacter* pNavChar = NavCharacter::FromProcess(pGameObj);
	const bool stopped = pNavChar && pNavChar->IsStopped() && !pNavChar->IsBusy(kExcludeFades);

	m_desNavAdjustRadius = m_movingNavAdjustRadius;

	if (stopped && (m_idleNavAdjustRadius >= 0.0f))
	{
		m_desNavAdjustRadius = m_idleNavAdjustRadius;
	}

	const NdSubsystemAnimController* pActiveSubSys = pGameObj ? pGameObj->GetActiveSubsystemController() : nullptr;
	if (pActiveSubSys)
	{
		m_desNavAdjustRadius = pActiveSubSys->GetNavAdjustRadius(m_desNavAdjustRadius);
	}

	if (pNavChar)
	{
		const F32 minAdjustRadius = pNavChar->GetMinNavSpaceDepenetrationRadius();
		m_desNavAdjustRadius = Max(m_desNavAdjustRadius, minAdjustRadius);
	}

	const float dt = pGameObj ? pGameObj->GetClock()->GetDeltaTimeInSeconds() : 0.0f;

	static CONST_EXPR float kNavAdjustRadiusRate = 2.0f;

	m_curNavAdjustRadius += (m_desNavAdjustRadius - m_curNavAdjustRadius) * dt * kNavAdjustRadiusRate;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Nav::FindSinglePathParams NavControl::GetFindSinglePathParams(const Locator& parentSpace,
															  const NavCharacter* pNavChar,
															  const NavLocation& startLoc,
															  const NavLocation& goalLoc,
															  PFnNavBlockerFilterFunc filterFunc,
															  BoxedValue userData) const
{
	Nav::FindSinglePathParams params;

	params.AddStartPosition(startLoc);
	params.ConstructPreferredPolys(GetCachedPathPolys());
	params.m_goal = goalLoc;

	params.m_context = GetPathFindContext(pNavChar, parentSpace, filterFunc, userData);

	params.m_findPolyYThreshold	  = m_pathYThreshold;
	params.m_findPolyXZCullRadius = m_pathXZCullRadius;
	params.m_bindSpawnerNameId	  = GetSearchBindSpawnerId();
	params.m_traversalSkillMask	  = GetTraversalSkillMask();
	params.m_tensionMode		= (*g_ndConfig.m_pGetTensionMode)();
	params.m_factionMask		= BuildFactionMask(GetFactionId());
	params.m_playerBlockageCost = GetDirectedPlayerBlockageCost();

	params.m_fixGapChecks = g_taskGraphMgr.IsSubnodeActive(SID("ellie-flashback-guitar"),
														   SID("efg-prologue-ride-to-jackson-above-gate"))
							|| g_taskGraphMgr.IsSubnodeActive(SID("ellie-flashback-guitar"),
															  SID("efg-prologue-ride-to-jackson-stream"));

	const bool isBuddy = pNavChar && pNavChar->IsKindOf(SID("Buddy"));
	// companions (and player pathfinds, which are in service of companion NPCs)
	// do their own accounting of node costs via custom cost functions
	if (isBuddy)
		params.m_ignoreNodeExtraCost = true;

	params.m_buildParams.m_portalShrink		= params.m_context.m_pathRadius;
	params.m_buildParams.m_truncateAfterTap = m_truncateAfterTap;

	params.m_buildParams.m_smoothPath	= Nav::kFullSmoothing;
	params.m_buildParams.m_portalShrink = 0.0f;

	params.m_buildParams.m_finalizePathWithProbes = true;

	params.m_buildParams.m_finalizeProbeMinDist = 10.0f;
	params.m_buildParams.m_finalizeProbeMaxDist = 20.0f;

	params.m_buildParams.m_finalizeProbeMaxDurationMs = 0.5f;
	params.m_buildParams.m_apEntryDistance = GetActionPackEntryDistance();

	if (pNavChar)
	{
		pNavChar->SetupPathBuildParams(params.m_context, params.m_buildParams);
	}

	return params;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Nav::PathFindContext NavControl::GetPathFindContext(const NavCharacter* pNavChar,
													const Locator& parentSpace,
													PFnNavBlockerFilterFunc filterFunc,
													BoxedValue userData) const
{
	Nav::PathFindContext context;

	if (pNavChar)
	{
		pNavChar->SetupPathContext(context);
	}

	context.m_obeyedStaticBlockers = GetObeyedStaticBlockers();
	context.m_parentSpace	= parentSpace;
	context.m_dynamicSearch = true;

	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		context.m_obeyedBlockers = Nav::BuildObeyedBlockerList(this, filterFunc, true, userData);
	}

	context.m_pathRadius = GetEffectivePathFindRadius();

	return context;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavControl::PathFindJobHandle NavControl::BeginPathFind(const NavCharacter* pNavChar,
														const PathFindOptions& options,
														const char* sourceFile,
														U32 sourceLine,
														const char* sourceFunc) const
{
	Nav::FindSinglePathParams params = GetFindSinglePathParams(options.m_parentSpace,
															   pNavChar,
															   options.m_startLoc,
															   options.m_goalLoc,
															   options.m_navBlockerFilterFunc,
															   options.m_nbFilterFuncUserData);

	if (options.m_pathRadius >= 0.0f)
	{
		params.m_context.m_pathRadius = options.m_pathRadius;
	}

	params.m_traversalSkillMask = options.m_traversalSkillMask;
	params.m_hPreferredTap		= options.m_hPreferredTap;

	const bool debugDraw = FALSE_IN_FINAL_BUILD(DebugSelection::Get().IsProcessSelected(pNavChar)
												&& g_navCharOptions.m_displaySinglePathfindJobs);

	params.m_debugDrawSearch  = debugDraw;
	params.m_debugDrawResults = debugDraw;
	params.m_buildParams.m_debugDrawResults = debugDraw;
	params.m_buildParams.m_debugDrawTime	= params.m_debugDrawTime;
	params.m_debugDrawTime = params.m_debugDrawTime;

	PathFindJobHandle handle;
	ndjob::CounterHandle hCounter;

	if (options.m_pPathSpline)
	{
		Nav::FindSplinePathParams splineParams(params);

		splineParams.m_buildParams	   = params.m_buildParams;
		splineParams.m_pPathSpline	   = options.m_pPathSpline;
		splineParams.m_arcStart		   = options.m_splineArcStart;
		splineParams.m_arcGoal		   = options.m_splineArcGoal;
		splineParams.m_arcObstacleStep = 3.5f;
		splineParams.m_arcStep		   = options.m_splineArcStep;
		splineParams.m_onSplineRadius  = pNavChar ? pNavChar->GetOnSplineRadius() : 1.0f;
		
		if (options.m_enableSplineTailGoal)
		{
			splineParams.m_tailGoalValid = true;
			splineParams.m_tailGoal		 = options.m_goalLoc;
		}

		if (FALSE_IN_FINAL_BUILD(DebugSelection::Get().IsProcessOrNoneSelected(pNavChar)
								 && g_navCharOptions.m_debugPrintFindPathParams))
		{
			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

			MsgOut("===[%d FindPath for %s]=========================================================\n",
				   EngineComponents::GetNdFrameState()->m_gameFrameNumber,
				   pNavChar->GetName());
			DebugPrintNavMeshPatchInput(GetMsgOutput(kMsgOut), g_navExData.GetNavMeshPatchInputBuffer());
			MsgOut("------------------------------------------------------------\n");
			DebugPrintFindSplinePathParams(splineParams, kMsgOut);
			MsgOut("============================================================\n");
		}

		Nav::FindSplinePathResults* pResults = NDI_NEW(kAllocDoubleGameFrame) Nav::FindSplinePathResults;

		handle.m_pSplinePathResults = pResults;
		hCounter = Nav::BeginSplinePathFind(splineParams,
											pResults,
											pNavChar,
											sourceFile,
											sourceLine,
											sourceFunc,
											ndjob::Priority::kGameFrameNormal,
											options.m_waitForPatch);
	}
	else
	{
		if (FALSE_IN_FINAL_BUILD(DebugSelection::Get().IsProcessOrNoneSelected(pNavChar)
								 && g_navCharOptions.m_debugPrintFindPathParams))
		{
			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

			MsgOut("===[%d FindPath for %s]=========================================================\n",
				   EngineComponents::GetNdFrameState()->m_gameFrameNumber,
				   pNavChar->GetName());
			DebugPrintNavMeshPatchInput(GetMsgOutput(kMsgOut), g_navExData.GetNavMeshPatchInputBuffer());
			MsgOut("------------------------------------------------------------\n");
			DebugPrintFindSinglePathParams(params, kMsgOut);
			MsgOut("============================================================\n");
		}

		Nav::FindSinglePathResults* pResults = NDI_NEW(kAllocDoubleGameFrame) Nav::FindSinglePathResults;

		handle.m_pSinglePathResults = pResults;

		hCounter = Nav::BeginSinglePathFind(params,
											pResults,
											pNavChar,
											sourceFile,
											sourceLine,
											sourceFunc,
											ndjob::Priority::kGameFrameNormal,
											options.m_waitForPatch);
	}

	handle.SetCounter(hCounter);

	return handle;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavControl::CollectPathFindResults(PathFindJobHandle& hJob,
										PathFindResults& results,
										bool updateCachedPolys /* = false */) const
{
	hJob.WaitForJob();

	bool foundPath = false;

	results.m_hNavAp = ActionPackHandle();
	results.m_pathPs.Clear();
	results.m_postTapPathPs.Clear();
	results.m_splineArcStart = -1.0f;

	const Nav::BuildPathResults* pBuildResults = nullptr;
	if (hJob.m_pSinglePathResults)
	{
		pBuildResults = &hJob.m_pSinglePathResults->m_buildResults;
	}
	else if (hJob.m_pSplinePathResults)
	{
		pBuildResults = &hJob.m_pSplinePathResults->m_buildResults;
		results.m_splineArcStart = hJob.m_pSplinePathResults->m_splineStartArc;
	}

	if (pBuildResults)
	{
		if (pBuildResults->m_goalFound && pBuildResults->m_pathWaypointsPs.IsValid())
		{
			foundPath = true;

			Nav::SplitPathAfterTap(pBuildResults->m_pathWaypointsPs, &results.m_pathPs, &results.m_postTapPathPs);
		}

		results.m_hNavAp = pBuildResults->m_hNavActionPack;

		if (updateCachedPolys)
		{
			memcpy(m_cachedPolys,
				   pBuildResults->m_cachedPolys,
				   sizeof(NavPolyHandle) * Nav::BuildPathResults::kNumCachedPolys);
		}
	}

	return foundPath;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
bool SimpleNavControl::DefaultNavBlockerFilter(const SimpleNavControl* pNavControl,
											   const DynamicNavBlocker* pBlocker,
											   BoxedValue userData,
											   bool forPathing,
											   bool debug)
{
	AI_ASSERT(pNavControl);
	AI_ASSERT(pBlocker);

	const U32 blockerProcId = pBlocker->GetOwner().GetProcessId();
	if (blockerProcId == pNavControl->m_ownerProcessId)
		return false;

	const FactionId myFactionId = FactionId(pNavControl->GetFactionId());

	const NavMesh* pNavMesh = pBlocker->GetNavMesh();
	if (!pNavMesh)
		return false;

	/*
		const StringId64 bindSpawnerId = pNavControl->m_navLocation.GetBindSpawnerNameId();
		const StringId64 otherBindSpawnerId = pNavMesh->GetBindSpawnerNameId();

		if (otherBindSpawnerId != bindSpawnerId)
		{
			return false;
		}
	*/

	if (pNavControl->IsNavBlockerIgnored(pBlocker))
	{
		return false;
	}

	// may ignore enemies
	const bool isEnemy = IsEnemy(FactionId(pBlocker->GetFactionId()), myFactionId);
	if (isEnemy && (!pBlocker->ShouldAffectEnemies() || !pNavControl->m_addEnemyNavBlockers))
	{
		return false;
	}

	if (!isEnemy && !pBlocker->ShouldAffectNonEnemies())
	{
		return false;
	}

	if (!forPathing && pNavControl->GetIgnorePlayerBlocker() && EngineComponents::GetNdGameInfo()->IsPlayerHandle(pBlocker->GetOwner()))
	{
		return false;
	}

	StringId64 blockProcessType = pBlocker->GetBlockProcessType();
	if (blockProcessType != INVALID_STRING_ID_64 && blockProcessType != pNavControl->m_ownerProcessType)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavBlockerBits SimpleNavControl::BuildObeyedBlockerList(bool forPathing) const
{
	return Nav::BuildObeyedBlockerList(this, m_obeyedBlockerFilterFunc, forPathing, m_obeyedBlockerFilterUserData);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::SetObeyedBlockerFilterFunc(PFnNavBlockerFilterFunc filterFunc, BoxedValue userData)
{
	m_obeyedBlockerFilterFunc = filterFunc ? filterFunc : DefaultNavBlockerFilter;
	m_obeyedBlockerFilterUserData = userData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavControl::ClearObeyedBlockerFilterFunc()
{
	m_obeyedBlockerFilterFunc = DefaultNavBlockerFilter;
	m_obeyedBlockerFilterUserData = BoxedValue();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavControl::AdjustMoveVectorPs(Point_arg startPosPs,
									Point_arg endPosPs,
									float adjustRadius,
									float maxMoveDist,
									const NavBlockerBits& obeyedBlockers,
									Point* pReachedPosPsOut,
									bool debugDraw /* = false */) const
{
	return AdjustMoveVectorPs(startPosPs,
							  endPosPs,
							  Segment(kZero, kZero),
							  adjustRadius,
							  maxMoveDist,
							  obeyedBlockers,
							  pReachedPosPsOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavControl::AdjustMoveVectorPs(Point_arg startPosPs,
									Point_arg endPosPs,
									Segment probePs,
									float adjustRadius,
									float maxMoveDist,
									const NavBlockerBits& obeyedBlockers,
									Point* pReachedPosPsOut,
									bool debugDraw /* = false */) const
{
	bool wasAdjusted = false;

	const NavPoly* pPoly = m_navLocation.ToNavPoly();
	const NavMesh* pMesh = pPoly ? pPoly->GetNavMesh() : nullptr;

	if (pMesh)
	{
		Nav::StaticBlockageMask obeyedStaticBlockers = GetObeyedStaticBlockers();

		wasAdjusted = AI::AdjustMoveToNavMeshPs(pMesh,
												pPoly,
												startPosPs,
												endPosPs,
												probePs,
												adjustRadius,
												maxMoveDist,
												obeyedStaticBlockers,
												m_minNpcStature,
												obeyedBlockers,
												pReachedPosPsOut,
												debugDraw);
	}
	else
	{
		*pReachedPosPsOut = endPosPs;
	}

	return wasAdjusted;
}
