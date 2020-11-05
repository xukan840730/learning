/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/hashtable.h"
#include "corelib/math/locator.h"
#include "corelib/system/read-write-atomic-lock.h"
#include "corelib/util/bit-array.h"
#include "corelib/util/float16.h"

#include "ndlib/anim/anim-debug.h"

class ArtItemAnim;

//#define USE_HUGE_LOCATORS

/// --------------------------------------------------------------------------------------------------------------- ///
class SmallLocator
{
public:
	void FromLocator(const Locator& loc)
	{
#ifdef USE_HUGE_LOCATORS
		m_loc = loc;
#else
		const Point p = loc.Pos();
		ANIM_ASSERT(IsFinite(p));
		m_posX = MakeF16(p.X());
		m_posY = MakeF16(p.Y());
		m_posZ = MakeF16(p.Z());

		const Quat r = loc.Rot();
		ANIM_ASSERT(IsNormal(r));
		m_rotX = MakeF16(r.X());
		m_rotY = MakeF16(r.Y());
		m_rotZ = MakeF16(r.Z());
		m_rotW = MakeF16(r.W());
#endif
	}

	const Locator ToLocator() const
	{
#ifdef USE_HUGE_LOCATORS
		return m_loc;
#else
		const Point p = Point(MakeF32(m_posX), MakeF32(m_posY), MakeF32(m_posZ));
		const Quat baseR = Quat(MakeF32(m_rotX), MakeF32(m_rotY), MakeF32(m_rotZ), MakeF32(m_rotW));
		//ANIM_ASSERT(Length4(baseR.GetVec4()) > kSmallFloat);
		const Quat r = SafeNormalize(baseR, kIdentity);
		return Locator(p, r);
#endif
	}

private:
#ifdef USE_HUGE_LOCATORS
	Locator m_loc;
#else
	F16 m_posX;
	F16 m_posY;
	F16 m_posZ;

	F16 m_rotX;
	F16 m_rotY;
	F16 m_rotZ;
	F16 m_rotW;
#endif
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimAlignCache
{
public:
	AnimAlignCache();

	bool Init(U32F maxEntries, U32F maxLocators);
	void DebugPrint() const;
	bool TryCacheAnim(const ArtItemAnim* pAnim, StringId64 channelId, float phaseStart, float phaseEnd);
	bool ReadCachedChannel(const SkeletonId skelId,
						   const ArtItemAnim* pAnim,
						   StringId64 channelId,
						   float phase,
						   bool mirror,
						   Locator* pLocOut) const;
	bool HasDataForAnim(const ArtItemAnim* pAnim, StringId64 channelId, float phase) const;
	bool HasDataForAnim(const ArtItemAnim* pAnim, StringId64 channelId, float minPhase, float maxPhase) const;
	void EvictAllEntries();

	void AcquireReadLock(const char* sourceFile, U32F sourceLine, const char* srcFunc)	{ m_accessLock.AcquireReadLock(sourceFile, sourceLine, srcFunc); }
	void ReleaseReadLock(const char* sourceFile, U32F sourceLine, const char* srcFunc)	{ m_accessLock.ReleaseReadLock(sourceFile, sourceLine, srcFunc); }
	void AcquireWriteLock(const char* sourceFile, U32F sourceLine, const char* srcFunc)	{ m_accessLock.AcquireWriteLock(sourceFile, sourceLine, srcFunc); }
	void ReleaseWriteLock(const char* sourceFile, U32F sourceLine, const char* srcFunc)	{ m_accessLock.ReleaseWriteLock(sourceFile, sourceLine, srcFunc); }

private:
	CREATE_IDENTIFIER_TYPE(Key, U64);

	Key MakeKey(const ArtItemAnim* pAnim, StringId64 channelId) const;
	I32F LookupEntry(Key k, float phase) const;
	bool GetCachedRange(Key k, float& startPhase, float& endPhase) const;
	void CacheAnim(const ArtItemAnim* pAnim, StringId64 channelId, Key k, U32F totalSamples, U32F sampleStart, U32F numSamples);
	bool EvictOldestEntry(); 
	void EvictEntry(I32F entryIndex);
	void EvictKey(Key k);

	static const U32 kMaxEntries = 4096;

	struct Entry
	{
		static const U32 kMaxSamplesPerAnim = 128;

		Key m_key;

		U32 m_totalAnimSamples;			// How many samples does the cached animation have in total
		U32 m_animSampleBase;			// Which frame of this animation is the first sample
		U32	m_numCachedSamples;			// How many samples were cached

		Entry* m_pPrev;
		Entry* m_pNext;

		SmallLocator m_locators[kMaxSamplesPerAnim];

		float PhaseStart() const
		{
			if (m_totalAnimSamples <= 1)
				return 0.0f;
			
			const float ps = float(m_animSampleBase) / float(m_totalAnimSamples - 1);
			ANIM_ASSERT(ps >= 0.0f && ps <= 1.0f);
			return ps;
		}

		float PhaseEnd() const
		{
			if (m_totalAnimSamples <= 1)
				return 1.0f;

			const float pe = float(m_animSampleBase + m_numCachedSamples - 1) / float(m_totalAnimSamples - 1);
			ANIM_ASSERT(pe >= 0.0f && pe <= 1.0f);
			return pe;
		}
	};

	mutable NdRwAtomicLock64_Jls m_accessLock;

	// Hash table that allow duplicates
	typedef HashTable<Key, Entry*, true> LookupTable;

	LookupTable m_lookUpTable;			
	Entry* m_pEntries;
	Entry* m_pRootEntry;
	Entry* m_pTailEntry;
	Entry m_sentinelEntry;								// Convenience to avoid having to deal with NULL pointers when adding/removing items from the 'age' list

	BitArray<kMaxEntries> m_usedEntries;

	U32* m_pSentinal;
};

extern AnimAlignCache g_animAlignCache;

/// --------------------------------------------------------------------------------------------------------------- ///
// cache versions of utility functions
bool EvaluateChannelInAnimCached(const SkeletonId skelId,
								 const ArtItemAnim* pAnim,
								 StringId64 channelId,
								 float phase,
								 bool mirror,
								 Locator* pLocOut);

bool FindAlignFromApReferenceCached(SkeletonId skelId,
									const ArtItemAnim* pAnim,
									float phase,
									const Locator& apRef,
									StringId64 apRefNameId,
									bool mirror,
									Locator* pOutAlign);
