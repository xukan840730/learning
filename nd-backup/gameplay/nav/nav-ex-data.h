/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/containers/fixedsizeheap.h"
#include "corelib/containers/hashtable.h"

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class DynamicNavBlocker;
class NavMesh;
class NavPoly;
class NavPolyEx;
struct NavMeshGapEx;

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavMeshPatchInput
{
	DynamicNavBlocker* m_pBlockers;
	I32* m_pMgrIndices;
	size_t m_numBlockers;
	size_t m_maxBlockers;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static const U32F kMaxNavPolys = 256;
static const U32F kMaxNavBlockers = kMaxDynamicNavBlockerCount;
typedef BitArray<kMaxNavPolys> NavPolyBits;
class NavPolyBlockerIntersections
{
public:
	void Init();
	void Reset();

	const U32F Size() const { return m_numNavPolys; }
	void AddEntry(const NavPoly *const pNavPoly, const U32F iBlocker);

	const NavPoly** m_pNavPolys;			// All nav polys
	U32F m_numNavPolys;
	NavBlockerBits* m_pNavPolyToBlockers;	// Map nav poly to nav blockers
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavPolyBlockerIslands
{
public:
#ifdef HEADLESS_BUILD
	static const U64 kMaxIslands = kMaxNavBlockers + 64;
#else
	static const U64 kMaxIslands = kMaxNavBlockers;
#endif

	struct Island
	{
		U16 m_navPolys[kMaxNavPolys];
		U32F m_numNavPolys;
		NavBlockerBits m_navBlockers;

#ifndef FINAL_BUILD
		enum IslandDebugDraw
		{
			kDrawIntersections,
			kDrawVertsSegs,
			kDrawTriangulation,
			kDrawPolys,
			kDrawCount
		};

		float m_drawYOffsetMs[kDrawCount];
#endif
	};

	void Init();
	void Reset();

public:
	Island* m_pIslands;
	U32F m_numIslands;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavExData
{
public:
	NavExData() : m_initialized(false) {}

	void Initialize(U32F maxExPolys, U32F maxExGaps, U32F maxExGapRefs, Memory::Context ctx);

	void AllocateNavPolyExArray(const NavPoly* pSourcePoly, NavPolyEx** ppNewExPolys, size_t numPolys);
	NavPolyEx* AllocateNavPolyEx(const NavPoly* pSourcePoly);
	void ResetNavPolyExHeap();

	const NavPolyEx* GetNavPolyExFromId(U32F polyExId) const;
	NavPolyEx* GetNavPolyExFromId(const U32F polyExId);

	NavMeshGapEx* AllocateGapEx();
	NavMeshGapEx* AllocateGapExUnsafe();
	void RegisterExGap(const NavMeshGapEx* pGap, const NavPoly* pPoly);
	void RegisterExGapUnsafe(const NavMeshGapEx* pGap, const NavPoly* pPoly);

	size_t GatherExGapsForPoly(const NavManagerId polyId, NavMeshGapEx* pGapsOut, size_t maxGapsOut) const;
	size_t GatherExGapsForPolyUnsafe(const NavManagerId polyId, NavMeshGapEx* pGapsOut, size_t maxGapsOut) const;

	NavPolyBlockerIntersections& GetPolyBlockerIntersections() { return m_polyBlockerTable; }
	NavPolyBlockerIslands& GetNavPolyBlockerIslands()
	{
		return m_navPolyBlockerIslands;
	}

	NavMeshPatchInput& GetNavMeshPatchInputBuffer() { return m_patchInput; }

	void ValidateExNeighbors();
	void DebugDrawExGaps(Color colObeyed,
						 Color colIgnored,
						 float yOffset,
						 DebugPrimTime tt = kPrimDuration1FrameAuto,
						 const NavBlockerBits* pObeyedBlockers = nullptr) const;

	void SetWaitForPatch()
	{
		if (m_waitForPatchCounter)
			m_waitForPatchCounter->SetValue(1);
	}

	ndjob::CounterHandle GetWaitForPatchCounter() const { return m_waitForPatchCounter; }

private:
	static void OnNavMeshUnregistered(const NavMesh* pMesh);

	mutable NdAtomicLock m_dataLock;

	bool m_initialized;

	FixedSizeHeap m_exPolyHeap;
	FixedSizeHeap m_exGapHeap;

	struct RegExGapEntry
	{
		U64 m_gapIndex;
		const NavPoly* m_pPoly;
	};

	typedef HashTable<NavManagerId, RegExGapEntry, true> GapExTable;
	GapExTable m_registeredExGaps;

	NavPolyBlockerIntersections m_polyBlockerTable;
	NavPolyBlockerIslands m_navPolyBlockerIslands;

	NavMeshPatchInput m_patchInput;

	ndjob::CounterHandle m_waitForPatchCounter;
};

extern NavExData g_navExData;

const NavPolyEx* GetNavPolyExFromId(U32F polyExId);
