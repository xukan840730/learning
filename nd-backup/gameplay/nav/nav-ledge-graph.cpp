/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#if ENABLE_NAV_LEDGES

#include "gamelib/gameplay/nav/nav-ledge-graph.h"

#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraph::Login(Level* pLevel)
{
	NavLedgeGraphMgr& lgMgr = NavLedgeGraphMgr::Get();
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	m_handle.Invalidate();

	if (kVersionId != m_versionId)
	{
		MsgErr("NavLedgeGraph '%s' in level '%s' has a version mismatch: got '%d', expected '%d'",
			m_strName, pLevel->GetName(),
			m_versionId, kVersionId);

		MsgConErr("Level '%s' has an out of date nav ledge graph ('%s' is ver %d, need %d), please %s\n",
			pLevel->GetName(),
			m_strName,
			m_versionId,
			kVersionId,
			kVersionId > m_versionId ? "rebuild" : "get code");

		return;
	}

	m_gameData.m_registered = false;
	m_gameData.m_attached = false;

	const StringId64 bindSpawnerNameId = GetBindSpawnerNameId();
	const EntitySpawner* pBindSpawner = nullptr;

	m_pBoundFrame = NDI_NEW BoundFrame(kIdentity);

	if (bindSpawnerNameId != INVALID_STRING_ID_64)
	{
		pBindSpawner = pLevel->LookupEntitySpawnerByBareNameId(bindSpawnerNameId);

		if (!pBindSpawner)
		{
			MsgConErr("NavLedgeGraph '%s' is bound to non-existent spawner '%s'\n", m_strName, DevKitOnly_StringIdToString(bindSpawnerNameId));
			return;
		}

		*m_pBoundFrame = pBindSpawner->GetBoundFrame();
	}

	m_pBoundFrame->SetLocatorPs(m_originPs);

	m_levelId = pLevel->GetNameId();

	lgMgr.AddLedgeGraph(this);

	for (I32F iLedge = 0; iLedge < m_numLedges; ++iLedge)
	{
		NavLedge& ledge = GetLedge(iLedge);
		ledge.m_hLedgeGraph = m_handle;
	}

	lgMgr.OnLogin(this);

	if (pBindSpawner)
	{
		SpawnerLoginChunkNavLedgeGraph* pChunk = NDI_NEW SpawnerLoginChunkNavLedgeGraph(this);

		pBindSpawner->AddLoginChunk(pChunk);
	}
	else
	{
		Register();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraph::Register()
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	NavLedgeGraphMgr& lgMgr = NavLedgeGraphMgr::Get();

	lgMgr.SetRegistrationEnabled(this, true);

	g_navPathNodeMgr.AddNavLedgeGraph(this);

	lgMgr.OnRegister(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraph::Unregister()
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	NavLedgeGraphMgr& lgMgr = NavLedgeGraphMgr::Get();

	lgMgr.OnUnregister(this);

	g_navPathNodeMgr.RemoveNavLedgeGraph(this);

	lgMgr.SetRegistrationEnabled(this, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraph::Logout(Level* pLevel)
{
	NavLedgeGraphMgr& lgMgr = NavLedgeGraphMgr::Get();
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	if (IsRegistered())
	{
		Unregister();
	}

	lgMgr.OnLogout(this);

	UnregisterAllActionPacks();

	lgMgr.RemoveLedgeGraph(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraph::UnregisterAllActionPacks()
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	if (m_handle.IsNull())
		return;

	for (I32F iLedge = 0; iLedge < m_numLedges; ++iLedge)
	{
		NavLedge& ledge = GetLedge(iLedge);

		while (ledge.m_pRegistrationList)
		{
			ActionPack* pAp = ledge.m_pRegistrationList;

			if (pAp->IsRegistered())
			{
				pAp->UnregisterImmediately();
			}
			else
			{
				ASSERT(false);
				ledge.m_pRegistrationList = nullptr;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeGraph::FindClosestLedgeLs(FindLedgeParams* pParams) const
{
	if (!pParams)
		return false;

	const NavLedge* pBestLedge = nullptr;
	float bestDist = pParams->m_searchRadius;
	Point closestPt = pParams->m_point;

	for (I32F iLedge = 0; iLedge < m_numLedges; ++iLedge)
	{
		const NavLedge* pLedge = GetLedge(iLedge);
		
		const Point v0 = pLedge->GetVertex0Ls();
		const Point v1 = pLedge->GetVertex1Ls();

		Point edgePt;
		const float d = DistPointSegment(pParams->m_point, v0, v1, &edgePt);

		if (d <= bestDist)
		{
			bestDist = d;
			closestPt = edgePt;
			pBestLedge = pLedge;
		}
	}

	pParams->m_pLedge = pBestLedge;
	pParams->m_nearestPoint = closestPt;
	pParams->m_dist = bestDist;

	return pParams->m_pLedge != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeGraph::FindClosestLedgePs(FindLedgeParams* pParams) const
{
	pParams->m_point = ParentToLocal(pParams->m_point);

	FindClosestLedgeLs(pParams);

	pParams->m_point = LocalToParent(pParams->m_point);
	pParams->m_nearestPoint = LocalToParent(pParams->m_nearestPoint);

	return pParams->m_pLedge != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavLedge* NavLedgeGraph::FindClosestLedgeLs(Point_arg posLs, float searchRadius) const
{
	FindLedgeParams params;
	params.m_point = posLs;
	params.m_searchRadius = searchRadius;

	FindClosestLedgeLs(&params);

	return params.m_pLedge;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavLedge* NavLedgeGraph::FindClosestLedgePs(Point_arg posPs, float searchRadius) const
{
	FindLedgeParams params;
	params.m_point = ParentToLocal(posPs);
	params.m_searchRadius = searchRadius;

	FindClosestLedgeLs(&params);

	return params.m_pLedge;
}

#endif // ENABLE_NAV_LEDGES