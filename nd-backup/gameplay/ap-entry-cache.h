/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/containers/fixedsizeheap.h"
#include "corelib/containers/hashtable.h"
#include "corelib/system/read-write-atomic-lock.h"

#include "ndlib/anim/anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
	struct ApEntryItem;
}

class ArtItemAnim;
template <typename T> struct HashT;

/// --------------------------------------------------------------------------------------------------------------- ///
class ApEntryCache
{
public:
	static const size_t kMaxDistTableSize = 128;

	bool Init(U32F maxEntries);

	bool TryAddEntry(const SkeletonId skelId, const DC::ApEntryItem* pDcEntry);
	ArtItemAnimHandle LookupEntryAnim(const SkeletonId skelId, const DC::ApEntryItem* pDcEntry);

	bool GetPhaseRangeForEntry(const SkeletonId skelId,
							   ArtItemAnimHandle hAnim,
							   const DC::ApEntryItem* pDcEntry,
							   float& phaseMinOut,
							   float& phaseMaxOut);

	float TryGetPhaseForDistance(const SkeletonId skelId, 
								 const DC::ApEntryItem* pDcEntry,
								 ArtItemAnimHandle anim,
								 const float distance);

	void EvictAllEntries();
	void DebugPrint() const;

	struct ListEntryKey
	{
		SkeletonId m_skelId;
		StringId64 m_animId;

		bool operator == (const ListEntryKey& rhs) const
		{
			return (m_skelId == rhs.m_skelId) && (m_animId == rhs.m_animId);
		}
	};

private:
	struct ListEntryData
	{
		I32 m_dataTableIndex = -1;
	};

	struct DistanceTableEntry
	{
		bool NeedsRefreshing(const DC::ApEntryItem* pDcEntry, ArtItemAnimHandle newAnim);

		NdAtomic64 m_dataLock;

		AnimLookupSentinal m_cacheSentinel;
		ArtItemAnimHandle m_anim;
		StringId64 m_apChannelId;

		F32 m_phaseMin;
		F32 m_phaseMax;
		F32 m_phaseDistMin;
		F32 m_phaseDistMax;
		U32 m_distanceTableSize;
		F16 m_distanceTable[kMaxDistTableSize];
	};

	I32F GetDataTableIndex(const SkeletonId skelId, StringId64 animId) const;

	void RefreshDataEntry(DistanceTableEntry* pData,
						  const SkeletonId skelId, 
						  const DC::ApEntryItem* pDcEntry,
						  ArtItemAnimHandle anim);

	mutable NdRwAtomicLock64 m_accessLock;

	typedef HashTable<ListEntryKey, ListEntryData> EntryTable;
	EntryTable m_entryTable;
	FixedSizeHeap m_dataTable;
	TimeFrame m_lastRefreshTime;
	bool m_initialized = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
template<>
struct HashT<ApEntryCache::ListEntryKey>
{
	inline uintptr_t operator () (const ApEntryCache::ListEntryKey& key) const
	{
		return (uintptr_t)(key.m_skelId.GetValue() + key.m_animId.GetValue());
	}
};

extern ApEntryCache g_apEntryCache;
