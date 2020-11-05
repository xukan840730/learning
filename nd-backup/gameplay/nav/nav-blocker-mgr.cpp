/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-blocker-mgr.h"

#include "ndlib/process/debug-selection.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
NavBlockerMgr NavBlockerMgr::m_singleton;

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavBlockerStats
{
	U32 m_numAllocated = 0;
	U32 m_numActive = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ApplyStaticBlockerEnabled(StaticNavBlocker* pBlocker, uintptr_t userData, bool free)
{
	if (free)
		return true;

	if (pBlocker->IsEnabledActive() != pBlocker->IsEnabledRequested())
	{
		pBlocker->ApplyEnabled();

		ApHandleTable& apNavRefreshTable = *(ApHandleTable*)userData;

		pBlocker->RefreshNearbyCovers(apNavRefreshTable);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavBlockerMgr::Init()
{
	AllocateJanitor allocJanitor(kAllocNpcGlobals, FILE_LINE_FUNC);

	m_dynamicBlockers.Init(kMaxDynamicNavBlockerCount, kAllocNpcGlobals, kAlign16, FILE_LINE_FUNC);

	m_staticBlockers.Init(kMaxStaticNavBlockerCount, kAllocNpcGlobals, kAlign16, FILE_LINE_FUNC);
	m_staticBlockageTable.Init(kMaxStaticNavBlockerCount* 8, FILE_LINE_FUNC);

	m_apNavRefreshTable.Init(1024, FILE_LINE_FUNC);

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	nmMgr.AddRegisterObserver(OnNavMeshRegistered);
	nmMgr.AddUnregisterObserver(OnNavMeshUnregistered);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavBlockerMgr::Update()
{
	if (m_staticBlockersDirty)
	{
		NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

		m_staticBlockers.ForEachElement(ApplyStaticBlockerEnabled, (uintptr_t)&m_apNavRefreshTable);

		m_staticBlockersDirty = false;
	}

	if (m_apNavRefreshTable.Size() > 0)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		ApHandleTable::Iterator itr = m_apNavRefreshTable.Begin();
		NAV_ASSERT(itr != m_apNavRefreshTable.End());

		ActionPackHandle hAp = itr->m_key;
		ActionPack* pAp = hAp.ToActionPack();
		if (pAp)
		{
			pAp->RefreshNavMeshClearance();
		}

		m_apNavRefreshTable.Erase(itr);
	}

	if (FALSE_IN_FINAL_BUILD(g_navMeshDrawFilter.m_drawAllStaticNavBlockers))
	{
		MsgCon("Static Nav Blocker Table: %d / %d entries\n", m_staticBlockers.GetNumElementsUsed(), m_staticBlockers.GetNumElements());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavBlockerMgr::Shutdown()
{
	m_dynamicBlockers.Shutdown();
	m_staticBlockers.Shutdown();
}

/// --------------------------------------------------------------------------------------------------------------- ///
DynamicNavBlocker* NavBlockerMgr::AllocateDynamic(Process* pProc,
												  const NavPoly* pPoly,
												  const char* sourceFile,
												  U32F sourceLine,
												  const char* sourceFunc)
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	DynamicNavBlocker* pAllocated = (DynamicNavBlocker*)m_dynamicBlockers.Alloc();

	if (pAllocated)
	{
		pAllocated->Init(pProc, pPoly, sourceFile, sourceLine, sourceFunc);
	}
	else
	{
		MsgErr("NavBlockerMgr::AllocateDynamic: ran out of DynamicNavBlockers trying to create one for process '%s'!\n", pProc ? pProc->GetName() : "<null>");
	}

	return pAllocated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
DynamicNavBlocker* NavBlockerMgr::GetDynamicNavBlocker(U32 iBlocker)
{
	return (DynamicNavBlocker*)m_dynamicBlockers.GetElement(iBlocker);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DynamicNavBlocker* NavBlockerMgr::GetDynamicNavBlocker(U32 iBlocker) const
{
	return (const DynamicNavBlocker*)m_dynamicBlockers.GetElement(iBlocker);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F NavBlockerMgr::GetNavBlockerIndex(const DynamicNavBlocker* pNavBlocker) const
{
	return m_dynamicBlockers.GetBlockIndex(pNavBlocker);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavBlockerMgr::FreeDynamic(DynamicNavBlocker* pBlocker)
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	m_dynamicBlockers.Free(pBlocker);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StaticNavBlocker* NavBlockerMgr::AllocateStatic(Process* pProc,
												const BoundFrame& loc,
												StringId64 bindSpawnerId,
												StringId64 blockerDataId,
												const ProcessSpawnInfo& spawnInfo,
												const char* sourceFile,
												U32F sourceLine,
												const char* sourceFunc)
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	StaticNavBlocker* pAllocated = (StaticNavBlocker*)m_staticBlockers.Alloc();

	if (pAllocated)
	{
		pAllocated->Init(pProc, loc, bindSpawnerId, blockerDataId, spawnInfo, sourceFile, sourceLine, sourceFunc);
	}
	else
	{
		MsgErr("NavBlockerMgr::AllocateStatic:  ran out of NavBlocker slots!\n");
		m_staticBlockers.ForEachElement(DebugPrintStaticBlocker, (uintptr_t)this);
		NAV_ASSERTF(false, ("NavBlockerMgr::AllocateStatic() ran out of NavBlocker slots!\n"));
	}

	return pAllocated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StaticNavBlocker* NavBlockerMgr::GetStaticNavBlocker(U32 iBlocker)
{
	return (StaticNavBlocker*)m_staticBlockers.GetElement(iBlocker);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const StaticNavBlocker* NavBlockerMgr::GetStaticNavBlocker(U32 iBlocker) const
{
	return (const StaticNavBlocker*)m_staticBlockers.GetElement(iBlocker);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F NavBlockerMgr::GetNavBlockerIndex(const StaticNavBlocker* pNavBlocker) const
{
	return m_staticBlockers.GetBlockIndex(pNavBlocker);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavBlockerMgr::FreeStatic(StaticNavBlocker* pBlocker)
{
	NAV_ASSERT(pBlocker);

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	if (pBlocker->IsEnabledActive())
	{
		pBlocker->RequestEnabled(false);
		pBlocker->ApplyEnabled();
	}

	m_staticBlockers.Free(pBlocker);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavBlockerMgr::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	NavBlockerStats dynamicStats;
	NavBlockerStats staticStats;

	m_dynamicBlockers.ForEachElement(DebugDrawDynamicBlocker, (uintptr_t)&dynamicStats);
	m_staticBlockers.ForEachElement(DebugDrawStaticBlocker, (uintptr_t)&staticStats);

	if (g_navMeshDrawFilter.m_printNavBlockerStats)
	{
		MsgCon("Dynamic Nav Blockers: %d / %d Active (%d Max)\n", dynamicStats.m_numActive, dynamicStats.m_numAllocated, kMaxDynamicNavBlockerCount);
		MsgCon("Static Nav Blockers: %d / %d Active (%d Max)\n", staticStats.m_numActive, staticStats.m_numAllocated, kMaxStaticNavBlockerCount);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavBlockerMgr::CreateQuadVertsFromBoundingBox(const Locator& parentSpace,
												   Point_arg centerPs,
												   const Locator& locWs,
												   Point_arg minLs,
												   Point_arg maxLs,
												   Point vertsOut[4])
{
	PROFILE_AUTO(Navigation);

	//  locWs gives the box orientation, and the 2 points define the box in that coordinate system
	// make sure that all axes have an extent of at least 0.25m
	const Scalar kMinimumExtent = SCALAR_LC(0.251f);
	Vector extent = maxLs - minLs;
	Point centerLs = minLs + 0.5f*extent;
	extent = Abs(extent);
	extent = Max(extent, Vector(kMinimumExtent));

	Point bbox[2];
	bbox[0] = centerLs - 0.5f*extent;
	bbox[1] = centerLs + 0.5f*extent;
	Point pts[8];
	// generate the 8 corner verts of the box
	for (U32F i = 0; i < 8; ++i)
	{
		// index them such that bit 0 of the index corresponds to X, bit 1 is Y, bit 2 is Z; each bit selects min or max
		pts[i] = Point(bbox[(i >> 0) & 1].X(), bbox[(i >> 1) & 1].Y(), bbox[(i >> 2) & 1].Z());
	}
	// i   xyz
	// -------
	// 0   ---
	// 1   +--
	// 2   -+-
	// 3   ++-
	// 4   --+
	// 5   +-+
	// 6   -++
	// 7   +++
	I8 indices[] =
	{
		1, 3, 7, 5, // +X
		4, 6, 2, 0, // -X
		2, 6, 7, 3, // +Y
		1, 5, 4, 0, // -Y
		4, 5, 7, 6, // +Z
		2, 3, 1, 0, // -Z
	};
	I32F iBestAxis = -1;
	float bestAxisDot = 0.0f;
	const Locator localToParent = parentSpace.UntransformLocator(locWs);
	const Transform mat(localToParent.Rot(), localToParent.Pos());
	for (I32F iAxis = 0; iAxis < 3; ++iAxis)
	{
		float absDot = Abs(mat.Get(iAxis, 1));
		if (absDot > bestAxisDot)
		{
			iBestAxis = iAxis;
			bestAxisDot = absDot;
		}
	}
	I32F iBase = iBestAxis * 2;
	if (mat.Get(iBestAxis, 1) < 0.0f)
	{
		++iBase;
	}
	iBase *= 4;
	I8* piPoly = &indices[iBase];

	const Vector alignPosPs = centerPs - kOrigin;

	for (I32F ii = 0; ii < 4; ++ii)
	{
		vertsOut[ii] = pts[piPoly[ii]] * mat - alignPosPs;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavBlockerMgr::OnNavMeshRegistered(const NavMesh* pMesh)
{
	PROFILE(Navigation, NavBlocker_RegisterNavMesh);

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());
	m_singleton.m_staticBlockers.ForEachElement(StaticBlockerAddNavMesh, (uintptr_t)pMesh);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavBlockerMgr::StaticBlockerAddNavMesh(StaticNavBlocker* pBlocker, uintptr_t userData, bool free)
{
	if (free)
		return true;

	const NavMesh* pMesh = (const NavMesh*)userData;

	pBlocker->OnNavMeshRegistered(pMesh);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavBlockerMgr::OnNavMeshUnregistered(const NavMesh* pMesh)
{
	PROFILE(Navigation, NavBlocker_UnregisterNavMesh);

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());
	m_singleton.m_staticBlockers.ForEachElement(StaticBlockerRemoveNavMesh, (uintptr_t)pMesh);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavBlockerMgr::StaticBlockerRemoveNavMesh(StaticNavBlocker* pBlocker, uintptr_t userData, bool free)
{
	if (free)
		return true;

	const NavMesh* pMesh = (const NavMesh*)userData;

	pBlocker->OnNavMeshUnregistered(pMesh);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavBlockerMgr::DebugPrintDynamicBlocker(const DynamicNavBlocker* pBlocker, uintptr_t userData, bool free)
{
	if (free)
		return true;

	const Process* pOwnerProc = pBlocker->GetOwner().ToProcess();
	const NavMesh* pNavMesh = pBlocker->GetNavMesh();

	const I32 iBlocker = m_singleton.GetNavBlockerIndex(pBlocker);

	MsgErr("DynamicNavBlocker %3d: navmesh '%s', owner '%s'\n", iBlocker, pNavMesh ? pNavMesh->GetName() : "?", pOwnerProc ? pOwnerProc->GetName() : "?");

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavBlockerMgr::DebugDrawDynamicBlocker(const DynamicNavBlocker* pBlocker, uintptr_t userData, bool free)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (free)
		return true;

	const Process* pOwnerProc = pBlocker->GetOwner().ToProcess();

	if (NavBlockerStats* pStats = (NavBlockerStats*)userData)
	{
		++pStats->m_numAllocated;
		if (pBlocker->GetNavPoly())
		{
			++pStats->m_numActive;
		}
	}

	const bool doGameObjectDraw = g_gameObjectDrawFilter.m_drawNavBlocker && DebugSelection::Get().IsProcessOrNoneSelected(pOwnerProc);
	const bool drawEverything = g_navMeshDrawFilter.m_drawAllDynamicNavBlockers;
	const bool doDebugDraw = doGameObjectDraw || drawEverything;

	if (doDebugDraw)
	{
		pBlocker->DebugDraw();
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavBlockerMgr::DebugPrintStaticBlocker(const StaticNavBlocker* pBlocker, uintptr_t userData, bool free)
{
	if (free)
		return true;

	const Process* pOwnerProc = pBlocker->GetOwner().ToProcess();

	const I32 iBlocker = m_singleton.GetNavBlockerIndex(pBlocker);

	MsgErr("StaticNavBlocker %3d: owner '%s'\n", iBlocker, pOwnerProc ? pOwnerProc->GetName() : "?");

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavBlockerMgr::DebugDrawStaticBlocker(const StaticNavBlocker* pBlocker, uintptr_t userData, bool free)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (free)
		return true;

	const Process* pOwnerProc = pBlocker->GetOwner().ToProcess();

	if (NavBlockerStats* pStats = (NavBlockerStats*)userData)
	{
		++pStats->m_numAllocated;
		if (pBlocker->IsEnabledActive())
		{
			++pStats->m_numActive;
		}
	}

	const bool doGameObjectDraw = g_gameObjectDrawFilter.m_drawNavBlocker && DebugSelection::Get().IsProcessOrNoneSelected(pOwnerProc);
	const bool drawEverything = g_navMeshDrawFilter.m_drawAllStaticNavBlockers;
	const bool doDebugDraw = doGameObjectDraw || drawEverything;

	if (doDebugDraw)
	{
		pBlocker->DebugDraw();
	}

	return true;
}
