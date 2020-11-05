/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/containers/typed-fixed-size-heap.h"
#include "corelib/containers/hashtable.h"

#include "gamelib/gameplay/nav/ap-handle-table.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class DynamicNavBlocker;
class NavManagerId;
class NavMesh;
class NavPoly;
class ProcessSpawnInfo;
class StaticNavBlocker;

/// --------------------------------------------------------------------------------------------------------------- ///
class NavBlockerMgr
{
public:
	typedef HashTable<NavManagerId, StaticNavBlockerBits> StaticBlockageTable;
	typedef TypedFixedSizeHeap<DynamicNavBlocker> DynamicNavBlockerHeap;
	typedef TypedFixedSizeHeap<StaticNavBlocker> StaticNavBlockerHeap;

	static NavBlockerMgr& Get() { return m_singleton; };

	void Init();
	void Update();
	void Shutdown();
	void DebugDraw() const;

	// ------------------------------------------ //
	// moving, triangulation computed dynamically
	DynamicNavBlocker* AllocateDynamic(Process* pProc,
									   const NavPoly* pPoly,
									   const char* sourceFile,
									   U32F sourceLine,
									   const char* sourceFunc);
	DynamicNavBlocker* GetDynamicNavBlocker(U32 iBlocker);
	const DynamicNavBlocker* GetDynamicNavBlocker(U32 iBlocker) const;
	I32F GetNavBlockerIndex(const DynamicNavBlocker* pDynBlocker) const;
	void FreeDynamic(DynamicNavBlocker* pBlocker);

	DynamicNavBlockerHeap GetDynamicBlockers() const
	{
		NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());
		return m_dynamicBlockers;
	}

	// ------------------------------------ //
	// tool-generated, static triangulation
	StaticNavBlocker* AllocateStatic(Process* pProc,
									 const BoundFrame& loc,
									 StringId64 bindSpawnerId,
									 StringId64 blockerDataId,
									 const ProcessSpawnInfo& spawnInfo,
									 const char* sourceFile,
									 U32F sourceLine,
									 const char* sourceFunc);

	StaticNavBlocker* GetStaticNavBlocker(U32 iBlocker);
	const StaticNavBlocker* GetStaticNavBlocker(U32 iBlocker) const;
	I32F GetNavBlockerIndex(const StaticNavBlocker* pStaticBlocker) const;
	void FreeStatic(StaticNavBlocker* pBlocker);
	void MarkDirty(const StaticNavBlocker* pBlocker) { m_staticBlockersDirty = true; }

	StaticNavBlockerHeap BeginStatic() const
	{
		NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());
		return m_staticBlockers;
	}

	void HACK_NukeStaticBlockageTable() { m_staticBlockageTable.Clear(); }

	// misc util
	static void CreateQuadVertsFromBoundingBox(const Locator& parentSpace,
											   Point_arg centerPs,
											   const Locator& locWs,
											   Point_arg minLs,
											   Point_arg maxLs,
											   Point vertsOut[4]);

private:
	static void OnNavMeshRegistered(const NavMesh* pMesh);
	static bool StaticBlockerAddNavMesh(StaticNavBlocker* pElement, uintptr_t userData, bool free);
	static void OnNavMeshUnregistered(const NavMesh* pMesh);
	static bool StaticBlockerRemoveNavMesh(StaticNavBlocker* pElement, uintptr_t userData, bool free);

	static bool DebugPrintDynamicBlocker(const DynamicNavBlocker* pElement, uintptr_t userData, bool free);
	static bool DebugDrawDynamicBlocker(const DynamicNavBlocker* pElement, uintptr_t userData, bool free);
	static bool DebugPrintStaticBlocker(const StaticNavBlocker* pElement, uintptr_t userData, bool free);
	static bool DebugDrawStaticBlocker(const StaticNavBlocker* pElement, uintptr_t userData, bool free);

private:
	static NavBlockerMgr m_singleton;

	DynamicNavBlockerHeap m_dynamicBlockers;
	StaticNavBlockerHeap m_staticBlockers;

	StaticBlockageTable m_staticBlockageTable;
	bool m_staticBlockersDirty;

	ApHandleTable m_apNavRefreshTable;

	friend class StaticNavBlocker;
};
