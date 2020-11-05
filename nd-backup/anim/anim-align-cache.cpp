/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-align-cache.h"

#include "corelib/util/razor-cpu.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/level/artitem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AnimAlignCache g_animAlignCache;
static const U32 kAACSentinalVal = 0x53544F50; //'STOP';

/// --------------------------------------------------------------------------------------------------------------- ///
AnimAlignCache::AnimAlignCache()
: m_accessLock(JlsFixedIndex::kAnimAlignCacheLock, SID("AnimAlignCache"))
, m_pEntries(nullptr)
, m_pSentinal(nullptr)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAlignCache::Init(U32F /*maxEntries*/, U32F /*maxLocators*/)
{
	m_lookUpTable.Init(kMaxEntries, FILE_LINE_FUNC);

	m_pEntries = NDI_NEW Entry[kMaxEntries];
	ANIM_ASSERT(m_pEntries);

	m_pRootEntry = m_pTailEntry = &m_sentinelEntry;
	m_sentinelEntry.m_pPrev = &m_sentinelEntry;
	m_sentinelEntry.m_pNext = &m_sentinelEntry;

#ifndef FINAL_BUILD
	memset(m_pEntries, 0xDE, sizeof(Entry)*kMaxEntries);
#endif

	m_usedEntries.ClearAllBits();

	m_pSentinal = NDI_NEW U32;
	*m_pSentinal = kAACSentinalVal;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimAlignCache::DebugPrint() const
{
	STRIP_IN_FINAL_BUILD;

	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	if (g_animOptions.m_aacDebugPrint)
	{
		const Clock* pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);

		const bool evictedRecently = false; // !pClock->TimePassed(m_lastEvictionTime, Seconds(5.0f));

		const U32 numEntries = m_usedEntries.CountSetBits();
		if (numEntries >= kMaxEntries)
			SetColor(kMsgCon, kColorRed);
		else if (evictedRecently)
			SetColor(kMsgCon, kColorYellow);
		else
			SetColor(kMsgCon, kColorWhite);

		MsgCon("Num Entries: %d / %d\n", numEntries, kMaxEntries);
	
		SetColor(kMsgCon, kColorWhite);
	}

	ANIM_ASSERT(!m_pSentinal || (*m_pSentinal == kAACSentinalVal));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimAlignCache::CacheAnim(const ArtItemAnim* pAnim,
							   StringId64 channelId,
							   Key k,
							   U32F totalSamples,
							   U32F sampleStart,
							   U32F numSamples)
{
	const CompressedChannel* pChannel = FindChannel(pAnim, channelId);
	if (!pChannel)
		return;

	// Find a new available entry for this animation
	U64 availableEntryIndex = m_usedEntries.FindFirstClearBit();
	if (availableEntryIndex >= kMaxEntries)
	{
		if (!EvictOldestEntry())
			return;

		availableEntryIndex = m_usedEntries.FindFirstClearBit();
	}
	ANIM_ASSERT(availableEntryIndex < kMaxEntries);
	m_usedEntries.SetBit(availableEntryIndex);

	ANIM_ASSERT(*m_pSentinal == kAACSentinalVal);


	Entry& newEntry = m_pEntries[availableEntryIndex];
	newEntry.m_key = k;
	newEntry.m_totalAnimSamples = totalSamples;
	newEntry.m_numCachedSamples = numSamples;
	newEntry.m_animSampleBase = sampleStart;
	m_lookUpTable.Add(k, &newEntry);

	// Insert ourselves before the newest node
	newEntry.m_pNext = m_pRootEntry->m_pNext;
	newEntry.m_pPrev = m_pRootEntry;
	m_pRootEntry->m_pNext->m_pPrev = &newEntry;
	m_pRootEntry->m_pNext = &newEntry;


	EvaluateChannelParams evalParams;
	evalParams.m_pAnim = pAnim;
	evalParams.m_channelNameId = channelId;
	evalParams.m_phase = 0.0f;

	ANIM_ASSERT(numSamples <= Entry::kMaxSamplesPerAnim);
	for (U32F iLoc = 0; iLoc < numSamples; ++iLoc)
	{
		const float samplePhase = float(sampleStart + iLoc) * pAnim->m_pClipData->m_phasePerFrame;
		evalParams.m_phase = samplePhase;

		ndanim::JointParams jp;
		EvaluateCompressedChannel(&evalParams, pChannel, &jp);

		newEntry.m_locators[iLoc].FromLocator(Locator(jp.m_trans, jp.m_quat));
	}

	ANIM_ASSERT(*m_pSentinal == kAACSentinalVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAlignCache::TryCacheAnim(const ArtItemAnim* pAnim, StringId64 channelId, float phaseStart, float phaseEnd)
{
	PROFILE_AUTO(Animation);
	PROFILE_ACCUM(TryCacheAnim);

	ANIM_ASSERT(*m_pSentinal == kAACSentinalVal);

#if !FINAL_BUILD
	if (g_animOptions.m_aacEvictAll)
	{
		EvictAllEntries();
		g_animOptions.m_aacEvictAll = false;
	}
#endif

	if (!pAnim || !pAnim->m_pClipData || (0 >= pAnim->m_pClipData->m_numTotalFrames))
		return false;

	phaseStart = Limit01(phaseStart);
	phaseEnd = Limit01(phaseEnd);

	const float deltaPhase = (phaseEnd - phaseStart);
	if (deltaPhase < 0.0f)
		return false;

	const Key k = MakeKey(pAnim, channelId);
	float phaseStartToUse = phaseStart;
	float phaseEndToUse = phaseEnd;

	bool isCached = false;
	{
		AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

		float rangeStart = 0.0f;
		float rangeEnd = 0.0f;
		isCached = GetCachedRange(k, rangeStart, rangeEnd);
		if (isCached)
		{
			const float entryPhaseStart = rangeStart;
			const float entryPhaseEnd = rangeEnd;

			if (entryPhaseStart <= phaseStart && entryPhaseEnd >= phaseEnd)
			{
				// our existing entry completely spans the requested timeline
				return true;
			}
			else
			{
				phaseStartToUse = Min(phaseStart, entryPhaseStart);
				phaseEndToUse = Max(phaseEnd, entryPhaseEnd);
			}
		}
	}

	AtomicLockJanitorWrite_Jls writeLock(&m_accessLock, FILE_LINE_FUNC);

	// If we get here it means that we had this animation cached but not enough of it, so we replace the whole thing with a larger cached range
	if (isCached)
	{
		EvictKey(k);
	}

	ANIM_ASSERT(*m_pSentinal == kAACSentinalVal);

	phaseStartToUse = Limit01(phaseStartToUse);
	phaseEndToUse = Limit01(phaseEndToUse);
	
	const U32F totalSamples = pAnim->m_pClipData->m_numTotalFrames;
	const U32F lastSample = pAnim->m_pClipData->m_numTotalFrames - 1;
	U32F sampleStart = U32F(Floor(float(lastSample) * phaseStartToUse));
	U32F sampleEnd = U32F(Ceil(float(lastSample) * phaseEndToUse));

	const float ps = float(sampleStart) / float(totalSamples - 1);

	if (ps > phaseStartToUse)
	{
		sampleStart = Min(sampleStart - 1, U32F(0));
	}

	const float pe = float(sampleEnd) / float(totalSamples - 1);

	if (pe < phaseEndToUse)
	{
		sampleEnd = Min(sampleEnd + 1, lastSample);
	}

	const U32F numSamples = sampleEnd - sampleStart + 1;

//	ANIM_ASSERTF(newEntry.PhaseStart() <= phaseStartToUse, ("%f > %f (%s %d + %d / %d)", newEntry.PhaseStart(), phaseStart, pAnim->GetName(), int(sampleStart), int(numSamples), int(totalSamples)));
//	ANIM_ASSERTF(newEntry.PhaseEnd() >= phaseEndToUse, ("%f < %f (%s %d + %d / %d)", newEntry.PhaseEnd(), phaseEndToUse, pAnim->GetName(), int(sampleStart), int(numSamples), int(totalSamples)));

	// Split the required samples into potentially multiple cache entries
	U32 numCachedSamplesAdded = 0;
	while (numCachedSamplesAdded < numSamples)
	{
		const bool lastEntry = numCachedSamplesAdded + Entry::kMaxSamplesPerAnim >= numSamples;
		const U32 numSamplesToCacheInEntry = lastEntry ? numSamples - numCachedSamplesAdded : Entry::kMaxSamplesPerAnim;
		const U32 firstSampleInEntry = sampleStart + numCachedSamplesAdded;

		// We need to duplicate the last sample in the intervals for tweening; 0-100, 100-200, 200-300
		numCachedSamplesAdded += lastEntry ? numSamplesToCacheInEntry : numSamplesToCacheInEntry - 1;	

		CacheAnim(pAnim, channelId, k, totalSamples, firstSampleInEntry, numSamplesToCacheInEntry);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAlignCache::ReadCachedChannel(const SkeletonId skelId,
									   const ArtItemAnim* pAnim,
									   StringId64 channelId,
									   float phase,
									   bool mirror,
									   Locator* pLocOut) const
{
	//PROFILE_AUTO(Animation);
	//PROFILE_ACCUM(ReadCachedChannel);

	if (!pAnim)
		return false;

	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	const Key k = MakeKey(pAnim, channelId);
	const I32F entryIdx = LookupEntry(k, phase);
	if (entryIdx < 0 || entryIdx > kMaxEntries)
		return false;

	const Entry& entry = m_pEntries[entryIdx];

	const U32F lastSample = entry.m_totalAnimSamples - 1;
	const float animT = phase * float(lastSample);
	const float animKeyframe0 = Floor(animT);
	const float animKeyframe1 = Ceil(animT);
	const float intraFrame = animT - animKeyframe0;

	if (animKeyframe0 < entry.m_animSampleBase)
		return false;
	if (animKeyframe1 > entry.m_animSampleBase + entry.m_numCachedSamples - 1)
		return false;

	const U32 entryK0 = U32(animKeyframe0) - entry.m_animSampleBase;
	const U32 entryK1 = U32(animKeyframe1) - entry.m_animSampleBase;

	const Locator locA = entry.m_locators[entryK0].ToLocator();
	const Locator locB = entry.m_locators[entryK1].ToLocator();
	Locator loc = Lerp(locA, locB, intraFrame);

	if (mirror)
	{
		Point p = loc.Pos();
		Quat r = loc.Rot();

		// mirror about X, this is NOT a conjugate, although conjugates work if Y is up.
		p.SetX(-1.0f * p.X());
		r = Quat(-r.X(), r.Y(), r.Z(), -r.W());

		loc.SetRotation(r);
		loc.SetTranslation(p);
	}

	// should we scale, is this remapped?
	if (pAnim->m_skelID != skelId && skelId != INVALID_SKELETON_ID)
	{
		// scale the translation
		const SkelTable::RetargetEntry* pRetargetEntry = SkelTable::LookupRetarget(pAnim->m_skelID, skelId);
		if (pRetargetEntry != nullptr && !pRetargetEntry->m_disabled)
		{
			const float retargetScale = pRetargetEntry->m_scale;

			loc.SetTranslation((loc.Pos() - kOrigin) * retargetScale + Point(kOrigin));
		}
	}

	if (pLocOut)
		*pLocOut = loc;

	ANIM_ASSERT(*m_pSentinal == kAACSentinalVal);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAlignCache::HasDataForAnim(const ArtItemAnim* pAnim, StringId64 channelId, float phase) const
{
	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	ANIM_ASSERT(m_pEntries);
	if (!pAnim)
		return false;

	const Key k = MakeKey(pAnim, channelId);
	const I32F entryIdx = LookupEntry(k, phase);
	if (entryIdx < 0 || entryIdx > kMaxEntries)
		return false;

	const float minPhase = m_pEntries[entryIdx].PhaseStart();
	const float maxPhase = m_pEntries[entryIdx].PhaseEnd();

	if (phase < minPhase)
		return false;

	if (phase > maxPhase)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAlignCache::HasDataForAnim(const ArtItemAnim* pAnim, StringId64 channelId, float minPhase, float maxPhase) const
{
	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	ANIM_ASSERT(m_pEntries);
	if (!pAnim)
		return false;

	const Key k = MakeKey(pAnim, channelId);

	float rangeStart = 0.0f;
	float rangeEnd = 0.0f;
	const bool isCached = GetCachedRange(k, rangeStart, rangeEnd);
	if (!isCached)
		return false;

	const float entryMinPhase = rangeStart;
	const float entryMaxPhase = rangeEnd;

	if (minPhase < entryMinPhase)
		return false;

	if (maxPhase > entryMaxPhase)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimAlignCache::EvictAllEntries()
{
	AtomicLockJanitorWrite_Jls writeLock(&m_accessLock, FILE_LINE_FUNC);

	m_usedEntries.ClearAllBits();
	m_lookUpTable.Clear();

	m_pRootEntry = m_pTailEntry = &m_sentinelEntry;
	m_sentinelEntry.m_pPrev = &m_sentinelEntry;
	m_sentinelEntry.m_pNext = &m_sentinelEntry;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimAlignCache::Key AnimAlignCache::MakeKey(const ArtItemAnim* pAnim, StringId64 channelId) const
{
	if (!pAnim)
		return Key(0);

	const U64 keyVal = ((uintptr_t)pAnim) + channelId.GetValue();
	return Key(keyVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F AnimAlignCache::LookupEntry(Key k, float phase) const
{
	ANIM_ASSERT(m_pEntries);

	LookupTable::ConstIterator iter = m_lookUpTable.Find(k);
	LookupTable::ConstIterator end = m_lookUpTable.End();
	while (iter != end)
	{
		// We can have multiple entries for this animation. Let's see if this is the correct phase range.
		Entry* pEntry = iter->m_data;
		const float startPhase = pEntry->PhaseStart();
		const float endPhase = pEntry->PhaseEnd();
		if (startPhase <= phase && endPhase >= phase)
		{
			const U32 entryIndex = pEntry - m_pEntries;
			return entryIndex;
		}

		iter = m_lookUpTable.Find(k, iter);
	}
	
	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAlignCache::GetCachedRange(Key k, float& startPhase, float& endPhase) const
{
	ANIM_ASSERT(m_pEntries);

	bool isCached = false;

	LookupTable::ConstIterator iter = m_lookUpTable.Find(k);
	LookupTable::ConstIterator end = m_lookUpTable.End();
	while (iter != end)
	{
		const Entry* pEntry = iter->m_data;

		if (isCached)
		{
			startPhase = Min(startPhase, pEntry->PhaseStart());
			endPhase = Max(endPhase, pEntry->PhaseEnd());
		}
		else
		{
			startPhase = pEntry->PhaseStart();
			endPhase = pEntry->PhaseEnd();
			isCached = true;
		}

		iter = m_lookUpTable.Find(k, iter);
	}
	
	return isCached;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAlignCache::EvictOldestEntry()
{
	ANIM_ASSERT(m_pEntries);
	if (m_usedEntries.AreAllBitsClear())
		return false;

	ANIM_ASSERT(m_pTailEntry && m_pRootEntry);
	ANIM_ASSERT(m_pTailEntry->m_pPrev);
	ANIM_ASSERT(m_pTailEntry->m_pPrev != &m_sentinelEntry);
	EvictKey(m_pTailEntry->m_pPrev->m_key);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimAlignCache::EvictKey(Key k)
{
	ScopedRazorCpuMarker marker("AnimAlignCache::EvictKey");

	ANIM_ASSERT(m_pEntries);

	ANIM_ASSERT(*m_pSentinal == kAACSentinalVal);

	// Remove all entries for this key
	LookupTable::Iterator iter = m_lookUpTable.Find(k);
	LookupTable::Iterator end = m_lookUpTable.End();
	while (iter != end)
	{
		Entry* pEntry = iter->m_data;
		const U32 entryIndex = pEntry - m_pEntries;

		m_usedEntries.ClearBit(entryIndex);

		// Erase the 'iter' element and update 'iter (2nd arg)' to now point to the next valid item
		m_lookUpTable.Erase(iter);

		// Unlink ourselves
		ANIM_ASSERT(pEntry != &m_sentinelEntry);
		pEntry->m_pNext->m_pPrev = pEntry->m_pPrev;
		pEntry->m_pPrev->m_pNext = pEntry->m_pNext;
		pEntry->m_pNext = nullptr;
		pEntry->m_pPrev = nullptr;

		// Let's just start over as it is quite undefined what will happen if I Erase AND search for duplicate elements.
		iter = m_lookUpTable.Find(k);
	}

	ANIM_ASSERT(*m_pSentinal == kAACSentinalVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EvaluateChannelInAnimCached(const SkeletonId skelId,
								 const ArtItemAnim* pAnim,
								 StringId64 channelId,
								 float phase,
								 bool mirror,
								 Locator* pLocOut)
{
	if (g_animAlignCache.ReadCachedChannel(skelId, pAnim, channelId, phase, mirror, pLocOut))
	{
		return true;
	}

	return EvaluateChannelInAnim(skelId, pAnim, channelId, phase, pLocOut, mirror);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindAlignFromApReferenceCached(SkeletonId skelId, 
									const ArtItemAnim* pAnim,
									float phase,
									const Locator& apRef, 
									StringId64 apRefNameId, 
									bool mirror, 
									Locator* pOutAlign)
{
	//PROFILE(Animation, FindAlignFromApRefCached);
	PROFILE_ACCUM(FindAlignFromApReferenceCached);

	if (!pOutAlign)
		return false;

	const bool hasApChannel = AnimHasChannel(pAnim, apRefNameId);
	const bool apCacheValid = !hasApChannel || g_animAlignCache.HasDataForAnim(pAnim, apRefNameId, phase);
	const bool alignCacheValid = g_animAlignCache.HasDataForAnim(pAnim, SID("align"), phase);
	const bool cacheValid = apCacheValid && alignCacheValid;

	if (!cacheValid)
	{
		return FindAlignFromApReference(skelId, pAnim, phase, apRef, apRefNameId, pOutAlign, mirror);
	}

	//Locator compLoc;
	//EvaluateChannelInAnim(skelId, pAnim, apRefNameId, phase, &compLoc, mirror, false);

	// apReference channel data is exported relative to the align, so we need to reverse it
	Locator apRefAs;
	if (!hasApChannel || !g_animAlignCache.ReadCachedChannel(skelId, pAnim, apRefNameId, phase, mirror, &apRefAs))
	{
		// Try evaluating the apRef as if it exist where the 'align' is on the first frame
		if (apRefNameId != SID("align"))
		{
			// If the animation is missing the channel we assume that there is a channel with that name at the first frame of the align.
			Locator fallbackLoc;
			if (!g_animAlignCache.ReadCachedChannel(skelId, pAnim, SID("align"), phase, mirror, &fallbackLoc))
			{
				// At least default the outgoing locator to something valid
				ANIM_ASSERT(IsNormal(apRef.GetRotation()));
				*pOutAlign = apRef;
				return FindAlignFromApReference(skelId, pAnim, phase, apRef, apRefNameId, pOutAlign, mirror);
			}
			else if (g_animAlignCache.HasDataForAnim(pAnim, apRefNameId, phase))
			{
				//ANIM_ASSERT(IsNormal(fallbackLoc.GetRotation()));
				*pOutAlign = apRef.TransformLocator(fallbackLoc);
				//ANIM_ASSERT(IsNormal(pOutAlign->GetRotation()));
				return true;
			}
			else
			{
				*pOutAlign = apRef;
				return FindAlignFromApReference(skelId, pAnim, phase, apRef, apRefNameId, pOutAlign, mirror);
			}
		}

		// At least default the outgoing locator to something valid
		*pOutAlign = apRef;
		return FindAlignFromApReference(skelId, pAnim, phase, apRef, apRefNameId, pOutAlign, mirror);
	}

	//ANIM_ASSERT(IsNormal(apRef.GetRotation()));
	//ANIM_ASSERT(IsNormal(apRefAs.GetRotation()));

	const Locator alignAPs = Inverse(apRefAs);

	//ANIM_ASSERT(IsNormal(alignAPs.GetRotation()));

	// Make sure the rotation is normalized!
	Locator align = apRef.TransformLocator(alignAPs);
	align.SetRotation(Normalize(align.GetRotation()));

	*pOutAlign = align;

	//ANIM_ASSERT(IsNormal(pOutAlign->GetRotation()));

	//FindAlignFromApReference(skelId, pAnim, phase, apRef, apRefNameId, &compLoc, mirror);

	return true;
}
