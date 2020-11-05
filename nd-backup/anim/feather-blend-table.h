/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/hashtable.h"
#include "corelib/containers/typed-fixed-size-heap.h"
#include "corelib/memory/allocator-pool.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemSkeleton;

namespace OrbisAnim
{
	struct ChannelFactor;
}

namespace DC
{
	struct FeatherBlend;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class FeatherBlendTable
{
public:
	struct Entry
	{
		SkeletonId m_skelId;
		StringId64 m_featherBlendId;
		U32 m_numChannelGroups;
		float m_defaultBlend;
		float m_blendTime;

		const OrbisAnim::ChannelFactor* const* m_ppChannelFactors;
		const U32* m_pNumChannelFactors;
	};

	void Init();
	void DebugDraw() const;

	I32F LoginFeatherBlend(const StringId64 featherBlendId, const FgAnimData* pAnimData);
	void LogoutAllEntries(const ArtItemSkeleton* pSkel);

	const Entry* GetEntry(I32F index) const;
	StringId64 GetFeatherBlendId(I32F index) const;

	float CreateChannelFactorsForAnimCmd(const ArtItemSkeleton* pSkel,
										 I32F index,
										 float baseBlendVal,
										 const OrbisAnim::ChannelFactor* const** pppChannelFactorsOut,
										 const U32** ppCountTableOut,
										 float tableBlendValue = 1.0f);

	void RequestDcUpdate();
	void ApplyDcUpdate();
	StringId64 GetScriptModuleId() const { return m_dcModuleId; }

private:
	static bool RegenEntryData(U8* pElement, uintptr_t data, bool free);

	static bool CreateChannelFactorsForSkel(const DC::FeatherBlend* pDcData,
											const ArtItemSkeleton* pSkel,
											const OrbisAnim::ChannelFactor* const** pppChannelFactorsOut,
											const U32** ppCountTableOut);

	static void DeleteEntryData(Entry* pEntry);

	struct Key
	{
		StringId64 m_featherBlendId;
		SkeletonId m_skelId;
		U32 padding = 0;
		
		bool operator == (const Key& rhs) const
		{
			return (m_skelId == rhs.m_skelId) && (m_featherBlendId == rhs.m_featherBlendId);
		}

		bool operator != (const Key& rhs) const
		{
			return !(*this == rhs);
		}
	};

	typedef HashTable<Key, I64> IndexHashTable;

	mutable NdAtomicLock m_accessLock;
	TypedFixedSizeHeap<Entry> m_entries;
	IndexHashTable m_indexTable;
	StringId64 m_dcModuleId;
	U8* m_pWorkingMem;
	PoolAllocator m_workingMemAllocator;
	bool m_needDcUpdate;
	I32 m_usageHw;
};

extern FeatherBlendTable g_featherBlendTable;
