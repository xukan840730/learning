/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#if ENABLE_NAV_LEDGES

#include "gamelib/gameplay/nav/nav-ledge-graph-mgr.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-defines.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-util.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"

NavLedgeGraphMgr g_navLedgeGraphMgr;
NavLedgeGraphMgr* NavLedgeGraphMgr::s_pSingleton = &g_navLedgeGraphMgr;

/// --------------------------------------------------------------------------------------------------------------- ///
static void NavError(const char* strMsg, ...)
{
	char strBuffer[512];
	va_list args;
	va_start(args, strMsg);
	vsnprintf(strBuffer, 512, strMsg, args);
	va_end(args);
	MsgErr("Nav: ");
	MsgErr(strBuffer);
	if (g_ssMgr.m_logIgcAnimations)
		MsgErr(strBuffer);
	else
		MsgConErr(strBuffer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLedgeGraphMgr::NavLedgeGraphMgr()
{
	m_idCounter = 1;

	m_usedLedgeGraphs.ClearAllBits();
	memset(m_ledgeGraphEntries, 0, sizeof(NavLedgeGraphEntry));

	for (U32F i = 0; i < kMaxObservers; ++i)
	{
		m_fnLoginObservers[i] = nullptr;
		m_fnRegisterObservers[i] = nullptr;
		m_fnUnregisterObservers[i] = nullptr;
		m_fnLogoutObservers[i] = nullptr;
	}

	m_numLoginObservers = 0;
	m_numRegisterObservers = 0;
	m_numUnregisterObservers = 0;
	m_numLogoutObservers = 0;

	m_accessLock.m_atomic.Set(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::Init()
{
	if (Memory::IsDebugMemoryAvailable())
	{
		AllocateJanitor jj(kAllocDebug, FILE_LINE_FUNC);

		static const size_t kMaxSelectedGraphs = 16;
		m_pSelectionStorage = NDI_NEW(kAllocDebug) StringId64[kMaxSelectedGraphs];
		m_selection.InitSelection(kMaxSelectedGraphs, m_pSelectionStorage);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::Shutdown()
{
	if (m_pSelectionStorage)
	{
		m_selection.ResetStorage();
		NDI_DELETE_ARRAY_CONTEXT(kAllocDebug, m_pSelectionStorage);
		m_pSelectionStorage = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::AddLoginObserver(PFnNotifyObserver fn)
{
	NAV_ASSERTF(m_numLoginObservers < kMaxObservers, ("Increase NavLedgeGraphMgr::kMaxObservers."));
	m_fnLoginObservers[m_numLoginObservers++] = fn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::AddRegisterObserver(PFnNotifyObserver fn)
{
	NAV_ASSERTF(m_numRegisterObservers < kMaxObservers, ("Increase NavLedgeGraphMgr::kMaxObservers."));
	m_fnRegisterObservers[m_numRegisterObservers++] = fn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::AddUnregisterObserver(PFnNotifyObserver fn)
{
	NAV_ASSERTF(m_numUnregisterObservers < kMaxObservers, ("Increase NavLedgeGraphMgr::kMaxObservers."));
	m_fnUnregisterObservers[m_numUnregisterObservers++] = fn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::AddLogoutObserver(PFnNotifyObserver fn)
{
	NAV_ASSERTF(m_numLogoutObservers < kMaxObservers, ("Increase NavLedgeGraphMgr::kMaxObservers."));
	m_fnLogoutObservers[m_numLogoutObservers++] = fn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeGraphMgr::AddLedgeGraph(NavLedgeGraph* pGraphToAdd)
{
	AtomicLockJanitor jj(&m_accessLock, FILE_LINE_FUNC);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	bool success = false;

	if (true)
	{
		// check for duplicate nav ledge graphs
		NavLedgeGraphBits::Iterator iter(m_usedLedgeGraphs);

		for (U32 iEntry = iter.First(); iEntry < iter.End(); iEntry = iter.Advance())
		{
			const NavLedgeGraphEntry& entry = m_ledgeGraphEntries[iEntry];

			if (const NavLedgeGraph* pGraph = entry.m_pLedgeGraph)
			{
				ASSERT(pGraph != pGraphToAdd);
				if (pGraph->GetNameId() == pGraphToAdd->GetNameId() && pGraphToAdd->GetNameId() != INVALID_STRING_ID_64)
				{
					ASSERT(false);
					NavError("Two nav ledge graphs are named %s, from level %s and level %s\n",
							 pGraphToAdd->GetName(),
							 DevKitOnly_StringIdToStringOrHex(pGraph->GetLevelId()),
							 DevKitOnly_StringIdToStringOrHex(pGraphToAdd->GetLevelId()));
				}
			}
		}
	}

	const U32 iFreeEntry = m_usedLedgeGraphs.FindFirstClearBit();

	// did we find a free entry?
	if (iFreeEntry < kMaxNavLedgeGraphCount)
	{
		NavLedgeGraphEntry& entry = m_ledgeGraphEntries[iFreeEntry];

		const U32F uniqueId = m_idCounter++;

		// disallow unique id of 0, this allows kInvalidMgrId to be 0
		if ((m_idCounter & 0xFFFF) == 0)
		{
			m_idCounter++;
		}

		entry.m_registered = false;
		entry.m_pLedgeGraph = pGraphToAdd;
		entry.m_bindSpawnerNameId = pGraphToAdd->GetBindSpawnerNameId();
		entry.m_hLedgeGraph.m_managerIndex = iFreeEntry;
		entry.m_hLedgeGraph.m_uniqueId = uniqueId;
		entry.m_boundingBoxLs = pGraphToAdd->GetBoundingBoxLs();
		entry.m_loc = pGraphToAdd->GetBoundFrame();

		pGraphToAdd->m_handle = entry.m_hLedgeGraph;

		m_usedLedgeGraphs.SetBit(iFreeEntry);

		success = true;
	}
	else
	{
		NavError("NavLedgeGraphMgr is full (%d entries): Failed to register ledge graph (%s, from level %s)\n",
				 (int)kMaxNavLedgeGraphCount,
				 pGraphToAdd->GetName(),
				 DevKitOnly_StringIdToStringOrHex(pGraphToAdd->GetLevelId()));
	}

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::SetRegistrationEnabled(NavLedgeGraph* pGraph, bool enabled)
{
	NAV_ASSERT(pGraph);
	if (!pGraph)
		return;

	const U32F iGraph = pGraph->m_handle.m_managerIndex;
	NAV_ASSERT(iGraph < kMaxNavLedgeGraphCount);

	if ((iGraph < kMaxNavLedgeGraphCount) && m_usedLedgeGraphs.IsBitSet(iGraph))
	{
		NavLedgeGraphEntry& entry = m_ledgeGraphEntries[iGraph];

		if (entry.m_pLedgeGraph == pGraph)
		{
			entry.m_registered = enabled;
			pGraph->m_gameData.m_registered = enabled;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::RemoveLedgeGraph(NavLedgeGraph* pGraphToRemove)
{
	AtomicLockJanitor jj(&m_accessLock, FILE_LINE_FUNC);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	NAV_ASSERT(pGraphToRemove);
	if (!pGraphToRemove)
		return;
	
	const U32F iGraph = pGraphToRemove->m_handle.m_managerIndex;
	NAV_ASSERT(iGraph < kMaxNavLedgeGraphCount);

	NavLedgeGraphEntry& entry = m_ledgeGraphEntries[iGraph];

	NAV_ASSERT(entry.m_pLedgeGraph == pGraphToRemove);
	NAV_ASSERT(m_usedLedgeGraphs.IsBitSet(iGraph));

	if ((iGraph < kMaxNavLedgeGraphCount) && (entry.m_pLedgeGraph == pGraphToRemove))
	{
		entry = NavLedgeGraphEntry();
	}

	m_usedLedgeGraphs.ClearBit(iGraph);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::OnLogin(NavLedgeGraph* pGraph)
{
	NAV_ASSERT(pGraph);
	if (!pGraph)
		return;

	for (U32F i = 0; i < m_numLoginObservers; ++i)
	{
		m_fnLoginObservers[i](pGraph);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::OnRegister(NavLedgeGraph* pGraph)
{
	NAV_ASSERT(pGraph);
	if (!pGraph)
		return;

	for (U32F i = 0; i < m_numRegisterObservers; ++i)
	{
		m_fnRegisterObservers[i](pGraph);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::OnUnregister(NavLedgeGraph* pGraph)
{
	NAV_ASSERT(pGraph);
	if (!pGraph)
		return;

	for (U32F i = 0; i < m_numUnregisterObservers; ++i)
	{
		m_fnUnregisterObservers[i](pGraph);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphMgr::OnLogout(NavLedgeGraph* pGraph)
{
	NAV_ASSERT(pGraph);
	if (!pGraph)
		return;

	for (U32F i = 0; i < m_numLogoutObservers; ++i)
	{
		m_fnLogoutObservers[i](pGraph);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavLedgeGraph* NavLedgeGraphMgr::LookupRegisteredNavLedgeGraph(NavLedgeGraphHandle hLedgeGraph) const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (hLedgeGraph.m_u32 == 0)
		return nullptr;

	const NavLedgeGraph* pGraph = nullptr;
	const U32F iGraph = hLedgeGraph.m_managerIndex;
	
	NAV_ASSERT(iGraph < kMaxNavLedgeGraphCount);

	if ((iGraph < kMaxNavLedgeGraphCount) && m_usedLedgeGraphs.IsBitSet(iGraph))
	{
		const NavLedgeGraphEntry& entry = m_ledgeGraphEntries[iGraph];

		if (const NavLedgeGraph* pEntryGraph = entry.m_pLedgeGraph)
		{
			if (entry.m_registered && (pEntryGraph->m_handle == entry.m_hLedgeGraph))
			{
				pGraph = pEntryGraph;
			}
		}
	}

	return pGraph;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeGraphMgr::IsNavLedgeGraphHandleValid(NavLedgeGraphHandle hLedgeGraph) const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (hLedgeGraph.m_u32 == 0)
		return false;

	const U32F iGraph = hLedgeGraph.m_managerIndex;

	if (iGraph < kMaxNavLedgeGraphCount)
	{
		const NavLedgeGraphEntry& entry = m_ledgeGraphEntries[iGraph];

		if (entry.m_pLedgeGraph && (entry.m_pLedgeGraph->m_handle == entry.m_hLedgeGraph) && entry.m_registered)
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavLedgeGraphMgr::GetNavLedgeGraphList(NavLedgeGraph** navLedgeGraphList, U32F maxListLen) const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	U32F ledgeGraphCount = 0;

	NavLedgeGraphBits::Iterator iter(m_usedLedgeGraphs);

	for (U32 iEntry = iter.First(); iEntry < iter.End() && ledgeGraphCount < maxListLen; iEntry = iter.Advance())
	{
		const NavLedgeGraphEntry& entry = m_ledgeGraphEntries[iEntry];
		NAV_ASSERT(entry.m_pLedgeGraph);

		navLedgeGraphList[ledgeGraphCount++] = entry.m_pLedgeGraph;
	}
	
	return ledgeGraphCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeGraphMgr::FindLedgeGraph(FindNavLedgeGraphParams* pParams) const
{
	NAV_ASSERT(pParams);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (!pParams)
		return false;

	U32F navLedgeGraphCount = 0;
	const NavLedgeGraph* navLedgeGraphList[kMaxNavLedgeGraphCount];
	const Point posWs = pParams->m_pointWs;

	{
		const StringId64 bindSpawnerNameId = pParams->m_bindSpawnerNameId;
		NavLedgeGraphBits::Iterator iter(m_usedLedgeGraphs);

		for (U32F iGraph = iter.First(); iGraph < iter.End(); iGraph = iter.Advance())
		{
			const NavLedgeGraphEntry& entry = m_ledgeGraphEntries[iGraph];

			if (!entry.m_registered)
				continue;

			if ((entry.m_bindSpawnerNameId == bindSpawnerNameId)
				|| (bindSpawnerNameId == Nav::kMatchAllBindSpawnerNameId))
			{
				const Point posLs = entry.m_pLedgeGraph->WorldToLocal(posWs);

				Aabb bboxLs = entry.m_pLedgeGraph->GetBoundingBoxLs();
				bboxLs.Expand(pParams->m_searchRadius);

				if (bboxLs.ContainsPoint(posLs))
				{
					navLedgeGraphList[navLedgeGraphCount++] = entry.m_pLedgeGraph;
				}
			}
		}
	}

	float bestDist = kLargeFloat;

	NavLedgeGraph::FindLedgeParams findLedgeParams;
	findLedgeParams.m_searchRadius = pParams->m_searchRadius;

	for (I32F i = 0; i < navLedgeGraphCount; ++i)
	{
		const NavLedgeGraph* pGraph = navLedgeGraphList[i];

		if (!pGraph->IsRegistered())
			continue;

		findLedgeParams.m_point = pGraph->WorldToLocal(posWs);

		if (!pGraph->FindClosestLedgeLs(&findLedgeParams))
			continue;

		if (findLedgeParams.m_dist <= bestDist)
		{
			bestDist = findLedgeParams.m_dist;
			pParams->m_nearestPointWs = pGraph->LocalToWorld(findLedgeParams.m_nearestPoint);
			pParams->m_pLedgeGraph = pGraph;
			pParams->m_pNavLedge = findLedgeParams.m_pLedge;
		}
	}

	return pParams->m_pNavLedge != nullptr;
}

#endif // ENABLE_NAV_LEDGES