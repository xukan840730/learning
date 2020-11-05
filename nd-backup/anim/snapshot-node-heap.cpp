/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/snapshot-node-heap.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-node-library.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-snapshot-node-out-of-memory.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/memory/relocatable-heap-rec.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/profiling/profiling.h"

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap* g_pGlobalSnapshotNodeHeap;

/// --------------------------------------------------------------------------------------------------------------- ///
static void PrintSizeString(IStringBuilder* pStrOut, size_t numBytes)
{
	const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
	const int numUnits	= ARRAY_COUNT(units);

	int unitIndex	= 0;
	U64 unitVal		= 1;
	float converted = (float)numBytes;
	while (unitIndex + 1 < numUnits)
	{
		if (converted < 1024)
			break;
		converted /= 1024;
		unitVal *= 1024;
		unitIndex += 1;
	}

	if (unitIndex == 0)
		pStrOut->append_format("%d bytes", numBytes);
	else if ((numBytes & ((1ULL << (unitIndex * 10)) - 1)) == 0)
		pStrOut->append_format("%0.0f %s", converted, units[unitIndex]);
	else
		pStrOut->append_format("%0.1f %s", converted, units[unitIndex]);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SnapshotNodeHeap::Init(U8* memory, size_t numBytes, U32 maxNumItems, U32 heapId)
{
	m_reloHeap.Init(memory, numBytes, maxNumItems, heapId, 0);

	m_recordHighWater	 = 0;
	m_usedBytesHighWater = 0;
	m_lastReloFrame		 = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SnapshotNodeHeap::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

	m_reloHeap.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SnapshotNodeHeap::DebugDraw(const RelocatableHeap::DebugDrawParams& params, WindowContext* pContext) const
{
	STRIP_IN_FINAL_BUILD;

	m_reloHeap.m_drawMapBar = true;

	m_reloHeap.DebugDraw(pContext, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SnapshotNodeHeap::DebugPrint(MsgOutput output) const
{
	const U32 heapId		 = m_reloHeap.GetHeapId();
	const U32 numRecords	 = m_reloHeap.GetActiveRecordCount();
	const U32 maxRecords	 = m_reloHeap.GetMaxRecordCount();
	const U32 totalMem		 = m_reloHeap.GetTotalAvailMemory();
	const U32 allFreeMem	 = m_reloHeap.GetTotalFreeMemory();
	const U32 biggestFreeMem = m_reloHeap.GetBiggestFreeBlockSize();

	StringBuilder<512> desc;
	desc.append_format("Snapshot Heap [0x%.8x]", heapId);
	
	desc.append_format(" [Records: %d / %d (high: %d)]", numRecords, maxRecords, m_recordHighWater);

	desc.append(" [Memory: ");
	PrintSizeString(&desc, totalMem - allFreeMem);
	desc.append(" / ");
	PrintSizeString(&desc, totalMem);

	desc.append(" (high: ");
	PrintSizeString(&desc, m_usedBytesHighWater);
	
	desc.append(") (largest gap: ");
	PrintSizeString(&desc, biggestFreeMem);
	desc.append(")]\n");

	PrintTo(output, desc.c_str());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SnapshotNodeHeap::Update()
{
	//PROFILE(Animation, SnapshotHeapUpdate);
	PROFILE_ACCUM(SnapshotHeapUpdate);

	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

	m_reloHeap.m_relocationDebug  = FALSE_IN_FINAL_BUILD(g_animOptions.m_testSnapshotRelocation);
	m_reloHeap.m_relocateOnceTest = FALSE_IN_FINAL_BUILD(g_animOptions.m_testSnapshotRelocation
														 && (m_lastReloFrame
															 < EngineComponents::GetNdFrameState()->m_gameFrameNumber));

	m_reloHeap.Update();

	m_lastReloFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumber;

	m_recordHighWater	 = Max(m_recordHighWater, m_reloHeap.GetActiveRecordCount());
	m_usedBytesHighWater = Max(m_usedBytesHighWater, m_reloHeap.GetTotalAvailMemory() - m_reloHeap.GetTotalFreeMemory());
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNode* SnapshotNodeHeap::GetNodeByIndex(Index idx) const
{
	if (idx == kOutOfMemoryIndex)
	{
		return &AnimSnapshotNodeOutOfMemory::GetSingleton();
	}

	ANIM_ASSERT(idx < m_reloHeap.GetMaxRecordCount());

	const RelocatableHeapRecord* pRec = m_reloHeap.GetRecord(idx);

	AnimSnapshotNode* pNode = pRec ? PunPtr<AnimSnapshotNode*>(pRec->m_pMutableMem) : nullptr;

	return pNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNode* SnapshotNodeHeap::AllocateNode(StringId64 typeId, Index* pIndexOut /* = nullptr */)
{
	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

	const U32F size = g_animNodeLibrary.GetSizeForTypeId(typeId);
	if (size == 0)
	{
		return nullptr;
	}

	RelocatableHeapRecord* pRec = m_reloHeap.AllocBlock(size);
	U8* pMem = pRec ? pRec->m_pMutableMem : nullptr;
	SnapshotNodeHeap::Index newIndex = pRec ? m_reloHeap.GetRecordIndex(pRec) : kOutOfMemoryIndex;
	AnimSnapshotNode* pNewNode = pMem ? g_animNodeLibrary.CreateFromTypeId(typeId, pRec->m_pMutableMem, newIndex) : nullptr;

	if (pRec && pNewNode)
	{
		pNewNode->SetHeapRecord(pRec);
		pRec->m_pItem = pNewNode;
	}
	else
	{
		newIndex = kOutOfMemoryIndex;
	}

	if (pIndexOut)
	{
		*pIndexOut = newIndex;
	}

	return pNewNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SnapshotNodeHeap::ReleaseNode(Index idx)
{
	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

	ANIM_ASSERT(idx < m_reloHeap.GetMaxRecordCount());

	if (RelocatableHeapRecord* pRec = m_reloHeap.GetRecord(idx))
	{
		m_reloHeap.FreeBlock(pRec);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void InitGlobalSnapshotNodeHeap()
{
	AllocateJanitor mem(kAllocAnimation, FILE_LINE_FUNC);

	const size_t memSize = 4 * 1024 * 1024;
	U8* pMem = NDI_NEW U8[memSize];

	g_pGlobalSnapshotNodeHeap = NDI_NEW SnapshotNodeHeap;
	g_pGlobalSnapshotNodeHeap->Init(pMem, memSize, 4096, SID_VAL("Global"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap* GetGlobalSnapshotNodeHeap()
{
	return g_pGlobalSnapshotNodeHeap;
}
