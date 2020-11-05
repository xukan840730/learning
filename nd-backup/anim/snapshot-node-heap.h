/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/memory/relocatable-heap.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNode;
class WindowContext;

/// --------------------------------------------------------------------------------------------------------------- ///
class SnapshotNodeHeap
{
public:
	typedef U32 Index;
	static const Index kOutOfMemoryIndex = -1;

	void Init(U8* memory, size_t numBytes, U32 maxNumItems, U32 heapId);
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	void DebugDraw(const RelocatableHeap::DebugDrawParams& params, WindowContext* pContext) const;
	void DebugPrint(MsgOutput output) const;
	size_t GetMaxNumNodes() const { return m_reloHeap.GetMaxRecordCount(); }
	void Update();

	AnimSnapshotNode* GetNodeByIndex(Index idx) const;
	
	template <typename T>
	T* GetNodeByIndex(Index idx) const
	{
		return (T*)GetNodeByIndex(idx);
	}

	AnimSnapshotNode* AllocateNode(StringId64 typeId, Index* pIndexOut = nullptr);
	
	template <typename T>
	T* AllocateNode(Index* pIndexOut = nullptr)
	{
		AnimSnapshotNode* pNode = AllocateNode(T::GetStaticTypeId(), pIndexOut);
		return T::FromAnimNode(pNode);
	}
	
	void ReleaseNode(Index idx);

	void* GetMemoryBase() const  { return m_reloHeap.GetMemoryBase(); }

	U32 GetHeapId() const { return m_reloHeap.GetHeapId(); }

	void DrawMapBar(bool draw) const { m_reloHeap.m_drawMapBar = draw; }
	void PrintRecordList(bool print) const { m_reloHeap.m_printRecordListCon = print; }

private:
	mutable NdAtomicLock m_accessLock;
	RelocatableHeap m_reloHeap;
	
	U32 m_recordHighWater;
	U32 m_usedBytesHighWater;
	U64 m_lastReloFrame;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void InitGlobalSnapshotNodeHeap();
SnapshotNodeHeap* GetGlobalSnapshotNodeHeap();
