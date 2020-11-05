/*
* Copyright (c) 2014 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

#include "gamelib/gameplay/nav/nav-ex-data.h"

#include "corelib/memory/scoped-temp-allocator.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nav/nav-mesh-gap-ex.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"

NavExData g_navExData;

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyBlockerIslands::Init()
{
	m_pIslands = NDI_NEW Island[kMaxIslands];
	m_numIslands = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyBlockerIslands::Reset()
{
	m_numIslands = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyBlockerIntersections::Init()
{
	m_pNavPolys = NDI_NEW const NavPoly* [kMaxNavPolys];
	m_pNavPolyToBlockers = NDI_NEW NavBlockerBits[kMaxNavPolys];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyBlockerIntersections::Reset()
{
	m_numNavPolys = 0;
//#ifndef  FINAL_BUILD
//	m_navBlockers.ClearAllBits();
//#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyBlockerIntersections::AddEntry(const NavPoly* const pNavPoly, const U32F iBlocker)
{
	U32F navPolyIdx = m_numNavPolys;
	for (U32F iNavPoly0 = 0; iNavPoly0 < m_numNavPolys; iNavPoly0++)
	{
		const NavPoly* const pNavPoly0 = m_pNavPolys[iNavPoly0];
		if (pNavPoly0 == pNavPoly)
		{
			NavBlockerBits& navBlockerBits0 = m_pNavPolyToBlockers[iNavPoly0];
			navBlockerBits0.SetBit(iBlocker);

			navPolyIdx = iNavPoly0;
			break;
		}
	}

	if (navPolyIdx == m_numNavPolys)	// New nav poly
	{
		NAV_ASSERT(m_numNavPolys < kMaxNavPolys);

		m_pNavPolys[m_numNavPolys] = pNavPoly;

		NavBlockerBits& navBlockerBits = m_pNavPolyToBlockers[m_numNavPolys];
		navBlockerBits.ClearAllBits();
		navBlockerBits.SetBit(iBlocker);

		m_numNavPolys++;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPolyEx* GetNavPolyExFromId(U32F polyExId)
{
	return g_navExData.GetNavPolyExFromId(polyExId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavExData::Initialize(U32F maxExPolys, U32F maxExGaps, U32F maxExGapRefs, Memory::Context ctx)
{
	if (m_initialized)
		return;

	Memory::Allocator* pAllocator = Memory::GetAllocator(ctx);

	const size_t initialFree = pAllocator->GetFreeSize();

	m_exPolyHeap.Init(maxExPolys, sizeof(NavPolyEx), ctx, kAlign16, FILE_LINE_FUNC);
	m_exGapHeap.Init(maxExGaps, sizeof(NavMeshGapEx), ctx, kAlign16, FILE_LINE_FUNC);
	
	Memory::PushAllocator(ctx, FILE_LINE_FUNC);
	m_registeredExGaps.Init(maxExGapRefs, FILE_LINE_FUNC);

	m_polyBlockerTable.Init();
	m_navPolyBlockerIslands.Init();

	m_patchInput.m_pBlockers = NDI_NEW DynamicNavBlocker[kMaxDynamicNavBlockerCount];
	m_patchInput.m_maxBlockers = kMaxDynamicNavBlockerCount;
	m_patchInput.m_pMgrIndices = NDI_NEW I32[kMaxDynamicNavBlockerCount];
	m_patchInput.m_numBlockers = 0;

	EngineComponents::GetNavMeshMgr()->AddUnregisterObserver(OnNavMeshUnregistered);

	Memory::PopAllocator();
	const size_t postFree = pAllocator->GetFreeSize();
	MsgOut("NavExData allocated %d bytes\n", initialFree - postFree);

	m_waitForPatchCounter = ndjob::AllocateCounter(FILE_LINE_FUNC);

	m_initialized = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavExData::AllocateNavPolyExArray(const NavPoly* pSourcePoly, NavPolyEx** ppNewExPolys, size_t numPolys)
{
	if (!m_initialized)
	{
		return;
	}

	{
		AtomicLockJanitor lock(&m_dataLock, FILE_LINE_FUNC);

		for (U32F i = 0; i < numPolys; ++i)
		{
			NavPolyEx* pNewPolyEx = ppNewExPolys[i] = (NavPolyEx*)m_exPolyHeap.Alloc();
		}
	}

	for (U32F i = 0; i < numPolys; ++i)
	{
		NavPolyEx* pNewPolyEx = ppNewExPolys[i];

		if (!pNewPolyEx)
		{
			continue;
		}

		ASSERT(!pSourcePoly->IsLink());

		*pNewPolyEx = NavPolyEx();
		pNewPolyEx->m_hOrgPoly = pSourcePoly;
		pNewPolyEx->m_id = m_exPolyHeap.GetBlockIndex(pNewPolyEx) + 1;
		pNewPolyEx->m_ownerPathNodeId = pSourcePoly->GetPathNodeId();
		pNewPolyEx->m_sourceFlags = pSourcePoly->GetFlags();
		pNewPolyEx->m_sourceBlockage = pSourcePoly->GetBlockageMask();

		pNewPolyEx->m_adjPolys[0].Invalidate();
		pNewPolyEx->m_adjPolys[1].Invalidate();
		pNewPolyEx->m_adjPolys[2].Invalidate();
		pNewPolyEx->m_adjPolys[3].Invalidate();

		pSourcePoly->AddNavPolyExToList(pNewPolyEx);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavPolyEx* NavExData::AllocateNavPolyEx(const NavPoly* pSourcePoly)
{
	AtomicLockJanitor lock(&m_dataLock, FILE_LINE_FUNC);

	if (!m_initialized)
	{
		return nullptr;
	}

	NavPolyEx* pNewPolyEx = (NavPolyEx*)m_exPolyHeap.Alloc();

	if (!pNewPolyEx)
	{
		return nullptr;
	}

	ASSERT(!pSourcePoly->IsLink());

	*pNewPolyEx = NavPolyEx();
	pNewPolyEx->m_hOrgPoly = pSourcePoly;
	pNewPolyEx->m_id = m_exPolyHeap.GetBlockIndex(pNewPolyEx) + 1;
	pNewPolyEx->m_ownerPathNodeId = pSourcePoly->GetPathNodeId();
	pNewPolyEx->m_sourceFlags = pSourcePoly->GetFlags();
	pNewPolyEx->m_sourceBlockage = pSourcePoly->GetBlockageMask();

	pNewPolyEx->m_adjPolys[0].Invalidate();
	pNewPolyEx->m_adjPolys[1].Invalidate();
	pNewPolyEx->m_adjPolys[2].Invalidate();
	pNewPolyEx->m_adjPolys[3].Invalidate();

	pSourcePoly->AddNavPolyExToList(pNewPolyEx);

	return pNewPolyEx;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DetachNavPolyExFunctor(U8* pElement, uintptr_t data, bool isFree)
{
	if (isFree)
		return true;

	NavPolyEx* pPolyEx = (NavPolyEx*)pElement;
	if (!pPolyEx)
		return true;

	g_navPathNodeMgr.RemoveNavPolyEx(pPolyEx);

	const NavPoly* pPoly = pPolyEx->m_hOrgPoly.ToNavPoly();
	if (pPoly)
	{
		pPoly->DetachExPolys();
	}

	*pPolyEx = NavPolyEx();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavExData::ResetNavPolyExHeap()
{
	PROFILE_AUTO(Navigation);

	if (!m_initialized)
		return;

	AtomicLockJanitor lock(&m_dataLock, FILE_LINE_FUNC);

	g_navPathNodeMgr.Validate();

	{
		PROFILE(Navigation, DetachExPolys);

		m_exPolyHeap.ForEachElement(DetachNavPolyExFunctor, 0);
	}

	m_exPolyHeap.Clear();

	for (GapExTable::ConstIterator itr = m_registeredExGaps.Begin(); itr != m_registeredExGaps.End(); itr++)
	{
		const RegExGapEntry& entry = itr->m_data;
		const NavPoly* pPoly = entry.m_pPoly;
		if (!pPoly)
			continue;

		const_cast<NavPoly*>(pPoly)->SetHasExGaps(false);
	}

	m_exGapHeap.Clear();
	m_registeredExGaps.Clear(false);

	g_navPathNodeMgr.Validate();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPolyEx* NavExData::GetNavPolyExFromId(U32F polyExId) const
{
	if (!m_initialized)
	{
		return nullptr;
	}

	if (polyExId == NavPolyEx::kInvalidPolyExId)
	{
		return nullptr;
	}

	if (polyExId > m_exPolyHeap.GetNumElements())
	{
		return nullptr;
	}

	const NavPolyEx* pRet = (const NavPolyEx*)m_exPolyHeap.GetElement(polyExId - 1);

	if (pRet->GetId() == NavPolyEx::kInvalidPolyExId)
	{
		return nullptr;
	}

	ASSERT(pRet->GetId() == polyExId);

	if (pRet->GetId() != polyExId)
	{
		return nullptr;
	}

#if ENABLE_NAV_ASSERTS
	const NavPoly* pOwnerPoly = pRet->m_hOrgPoly.ToNavPoly();
	NAV_ASSERT(pOwnerPoly);
	bool isRegistered = false;
	for (const NavPolyEx* pPolyEx = pOwnerPoly->GetNavPolyExList(); pPolyEx; pPolyEx = pPolyEx->m_pNext)
	{
		if (pPolyEx->GetId() == pPolyEx->GetId())
		{
			isRegistered = true;
			break;
		}
	}
	NAV_ASSERT(isRegistered);
#endif

	return pRet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavPolyEx* NavExData::GetNavPolyExFromId(U32F polyExId)
{
	if (!m_initialized)
	{
		return nullptr;
	}

	if (polyExId == NavPolyEx::kInvalidPolyExId)
	{
		return nullptr;
	}

	if (polyExId > m_exPolyHeap.GetNumElements())
	{
		return nullptr;
	}

	NavPolyEx* const pRet = (NavPolyEx*)m_exPolyHeap.GetElement(polyExId - 1);
	if (pRet->GetId() == NavPolyEx::kInvalidPolyExId)
	{
		return nullptr;
	}

	ASSERT(pRet->GetId() == polyExId);
	if (pRet->GetId() != polyExId)
	{
		return nullptr;
	}

	return pRet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshGapEx* NavExData::AllocateGapEx()
{
	if (!m_initialized)
	{
		return nullptr;
	}

	AtomicLockJanitor lock(&m_dataLock, FILE_LINE_FUNC);

	return AllocateGapExUnsafe();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshGapEx* NavExData::AllocateGapExUnsafe()
{
	NavMeshGapEx* pNewGapEx = (NavMeshGapEx*)m_exGapHeap.Alloc();
	return pNewGapEx;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavExData::RegisterExGap(const NavMeshGapEx* pGap, const NavPoly* pPoly)
{
	if (!m_initialized)
	{
		return;
	}

	AtomicLockJanitor lock(&m_dataLock, FILE_LINE_FUNC);

	RegisterExGapUnsafe(pGap, pPoly);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavExData::RegisterExGapUnsafe(const NavMeshGapEx* pGap, const NavPoly* pPoly)
{
	const I64 index = m_exGapHeap.GetBlockIndex(pGap);
	const I64 numElements = m_exGapHeap.GetNumElements();
	if ((index < 0) || (index >= numElements))
	{
		NAV_ASSERTF(false, ("Gap pointer 0x%p is out of range (index: %d)", pGap, index));
		return;
	}

	if (!pPoly)
	{
		return;
	}

	if (m_registeredExGaps.IsFull())
	{
		MsgErr("Overflowed maximum number of registered ex gaps\n");
	}
	else
	{
		const NavManagerId polyId = pPoly->GetNavManagerId();
		RegExGapEntry newEntry;
		newEntry.m_gapIndex = U64(index);
		newEntry.m_pPoly = pPoly;
		m_registeredExGaps.Add(polyId, newEntry);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
size_t NavExData::GatherExGapsForPoly(const NavManagerId polyId, NavMeshGapEx* pGapsOut, size_t maxGapsOut) const
{
	AtomicLockJanitor lock(&m_dataLock, FILE_LINE_FUNC);

	return GatherExGapsForPolyUnsafe(polyId, pGapsOut, maxGapsOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
size_t NavExData::GatherExGapsForPolyUnsafe(const NavManagerId polyId, NavMeshGapEx* pGapsOut, size_t maxGapsOut) const
{
	if (!m_initialized || !pGapsOut)
		return 0;

	//PROFILE_ACCUM(GatherExGapsForPolyUnsafe);

	NAV_ASSERT(polyId.m_iPolyEx == 0);

	size_t numOut = 0;

	for (GapExTable::ConstIterator itr = m_registeredExGaps.Find(polyId); itr != m_registeredExGaps.End(); itr++)
	{
		if (itr->m_key != polyId)
			continue;

		if (numOut >= maxGapsOut)
			break;

		const RegExGapEntry& entry = itr->m_data;
		const U64 gapIndex = entry.m_gapIndex;

		NAV_ASSERT(gapIndex < m_exGapHeap.GetNumElements() && m_exGapHeap.IsElementUsed(gapIndex));

		pGapsOut[numOut] = *(const NavMeshGapEx*)m_exGapHeap.GetElement(gapIndex);
		++numOut;
	}

	return numOut;
}


/// --------------------------------------------------------------------------------------------------------------- ///
static bool ValidatePolyExNeighborsFunctor(U8* pElement, uintptr_t data, bool isFree)
{
	if (isFree)
		return true;

	NavPolyEx* pPolyEx = (NavPolyEx*)pElement;
	if (!pPolyEx)
		return true;

	for (U32F iV = 0; iV < pPolyEx->m_numVerts; ++iV)
	{
		if (pPolyEx->m_adjPolys[iV].m_iPolyEx == 0)
			continue;

		const NavPolyHandle hIncomingPoly = NavPolyHandle(pPolyEx->m_adjPolys[iV]);
		
		const NavPolyEx* pAdjEx = g_navExData.GetNavPolyExFromId(pPolyEx->m_adjPolys[iV].m_iPolyEx);
		NAV_ASSERT(pAdjEx && pAdjEx->m_hOrgPoly == hIncomingPoly);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavExData::ValidateExNeighbors()
{
	m_exPolyHeap.ForEachElement(ValidatePolyExNeighborsFunctor, 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavExData::DebugDrawExGaps(Color colObeyed,
								Color colIgnored,
								float yOffset,
								DebugPrimTime tt /* = kPrimDuration1FrameAuto */,
								const NavBlockerBits* pObeyedBlockers /* = nullptr */) const
{
	STRIP_IN_FINAL_BUILD;

	AtomicLockJanitor lock(&m_dataLock, FILE_LINE_FUNC);

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
	const size_t maxGaps = m_exGapHeap.GetNumElements();
	const size_t numBlocks = ExternalBitArray::DetermineNumBlocks(maxGaps);
	U64* pBlocks = NDI_NEW U64[numBlocks];

	ExternalBitArray drawnGaps;
	drawnGaps.Init(maxGaps, pBlocks);

	const Vector vo = Vector(kUnitYAxis) * yOffset;

	NavBlockerBits obeyedBlockers;
	if (pObeyedBlockers)
		obeyedBlockers = *pObeyedBlockers;
	else
		obeyedBlockers.SetAllBits();

	for (GapExTable::ConstIterator itr = m_registeredExGaps.Begin(); itr != m_registeredExGaps.End(); itr++)
	{
		const NavManagerId polyId = itr->m_key;
		const RegExGapEntry& entry = itr->m_data;
		const U64 gapIndex = entry.m_gapIndex;

		if (drawnGaps.IsBitSet(gapIndex))
			continue;

		const NavPolyHandle hPoly = NavPolyHandle(polyId);
		const NavPoly* pPoly = hPoly.ToNavPoly();
		const NavMesh* pMesh = hPoly.ToNavMesh();
		if (!pPoly || !pMesh)
			continue;

		const NavMeshGapEx& gap = *(const NavMeshGapEx*)m_exGapHeap.GetElement(gapIndex);

		if ((g_navMeshDrawFilter.m_drawGapMinDist >= 0.0f) && (gap.m_gapDist < g_navMeshDrawFilter.m_drawGapMinDist))
			continue;

		if ((g_navMeshDrawFilter.m_drawGapMaxDist >= 0.0f) && (gap.m_gapDist > g_navMeshDrawFilter.m_drawGapMaxDist))
			continue;

		drawnGaps.SetBit(gapIndex);
	}

	ExternalBitArray::Iterator gapsItr(drawnGaps);

	for (U64 iDrawGap = gapsItr.First(); iDrawGap < maxGaps; iDrawGap = gapsItr.Advance())
	{
		const NavMeshGapEx& gap = *(const NavMeshGapEx*)m_exGapHeap.GetElement(iDrawGap);

		StringBuilder<1024> desc;
		desc.format("[%d] %0.1fm\n", iDrawGap, gap.m_gapDist);
		const NavPoly* pFirstPoly = nullptr;

		for (GapExTable::ConstIterator itr = m_registeredExGaps.Begin(); itr != m_registeredExGaps.End(); itr++)
		{
			const NavManagerId polyId = itr->m_key;
			const RegExGapEntry& entry = itr->m_data;
			const U64 gapIndex = entry.m_gapIndex;
			if (gapIndex != iDrawGap)
				continue;

			if (!pFirstPoly)
				pFirstPoly = entry.m_pPoly;

			desc.append_format("iMesh : %d / iPoly : %d\n", polyId.m_navMeshIndex, polyId.m_iPoly);
		}

		const NavMesh* pMesh = pFirstPoly ? pFirstPoly->GetNavMesh() : nullptr;

		if (!pMesh)
		{
			continue;
		}

		NavBlockerBits masked;
		NavBlockerBits::BitwiseAnd(&masked, gap.m_blockerBits, obeyedBlockers);

		const bool obeyed = !masked.AreAllBitsClear();

		const Color col = obeyed ? colObeyed : colIgnored;

		const Point p0Ws = pMesh->LocalToWorld(gap.m_pos0Ls) + vo;
		const Point p1Ws = pMesh->LocalToWorld(gap.m_pos1Ls) + vo;

		const Point midWs = AveragePos(p0Ws, p1Ws);
		const Point textPosWs = midWs + Vector(0.0f, 0.5f + (float(iDrawGap) * 0.05f), 0.0f);

		g_prim.Draw(DebugLine(p0Ws, p1Ws, col, 3.0f, kPrimDisableDepthTest), tt);
		g_prim.Draw(DebugLine(midWs, textPosWs, col, 1.0f, kPrimDisableDepthTest), tt);
		g_prim.Draw(DebugString(textPosWs, desc.c_str(), col, 0.65f), tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NavExData::OnNavMeshUnregistered(const NavMesh* pMesh)
{
	if (!pMesh)
		return;

	AtomicLockJanitor lock(&g_navExData.m_dataLock, FILE_LINE_FUNC);
	const NavMeshHandle hMesh = pMesh;

	for (GapExTable::Iterator itr = g_navExData.m_registeredExGaps.Begin(); itr != g_navExData.m_registeredExGaps.End(); itr++)
	{
		RegExGapEntry& entry = itr->m_data;
		const NavPoly* pPoly = entry.m_pPoly;

		if (pPoly && (pPoly->GetNavMeshHandle() == hMesh))
		{
			const_cast<NavPoly*>(pPoly)->SetHasExGaps(false);
			entry.m_pPoly = nullptr;
		}
	}
}
