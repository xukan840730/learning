/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIM_CHANNEL_CACHE_H
#define ANIM_CHANNEL_CACHE_H

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-debug.h"

template <class T>
class AnimChannelCache
{
public:
	
	AnimChannelCache() : 
	m_maxEntries(0)
	, m_numUsedEntries(0)
	, m_nextEntryAddIndex(0)
	, m_pKeys(nullptr)
	, m_pChannelsLocs(nullptr)
	{
	}

	void Init(U8* pCacheAddr, U32 cacheSize)
	{
		ANIM_ASSERT(IsPointerAligned(pCacheAddr, kAlign16));
		ANIM_ASSERT(IsSizeAligned(cacheSize, kAlign16));
		const U32F entrySize = sizeof(Locator) + sizeof(T);
		const U32F numEntries = (cacheSize / entrySize);
		m_maxEntries = numEntries;
		m_pChannelsLocs = (Locator*)pCacheAddr;
		m_pKeys = (T*)(pCacheAddr + numEntries * sizeof(Locator));

		m_nextEntryAddIndex = 0;
	}

	void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound)
	{
		RelocatePointer(m_pKeys, offset_bytes, lowerBound, upperBound);
		RelocatePointer(m_pChannelsLocs, offset_bytes, lowerBound, upperBound);
	}

	const Locator* Find(const T key) const
	{
		for (U32F i = 0; i < m_numUsedEntries; ++i)
		{
			if (key == m_pKeys[i])
			{
				return &m_pChannelsLocs[i];
			}
		}

		return nullptr;
	}

	void Add(const T key, const Locator& channelLoc)
	{
		m_pKeys[m_nextEntryAddIndex] = key;
		m_pChannelsLocs[m_nextEntryAddIndex] = channelLoc;
		m_nextEntryAddIndex = (m_nextEntryAddIndex + 1) % m_maxEntries;
		m_numUsedEntries = m_numUsedEntries < m_maxEntries ? m_numUsedEntries + 1 : m_maxEntries;
	}

	bool IsFull() const		{ return m_numUsedEntries == m_maxEntries; }

private:
	U32 m_maxEntries;
	U32 m_numUsedEntries;
	U32 m_nextEntryAddIndex;
	T* m_pKeys;
	Locator* m_pChannelsLocs;
};

#endif // ANIM_CHANNEL_CACHE_H
