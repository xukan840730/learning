/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ap-entry-cache.h"

#include "corelib/util/float16.h"

#include "ndlib/anim/anim-align-cache.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-util.h"

#include "gamelib/gameplay/ap-entry-util.h"
#include "gamelib/level/artitem.h"
#include "gamelib/scriptx/h/ap-entry-defines.h"

ApEntryCache g_apEntryCache;

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntryCache::Init(U32F maxEntries)
{
	if (maxEntries == 0)
		return false;

	m_entryTable.Init(maxEntries, FILE_LINE_FUNC);
	m_dataTable.Init(maxEntries, sizeof(DistanceTableEntry), kAllocInvalid, kAlign16, FILE_LINE_FUNC);

	m_initialized = true;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntryCache::TryAddEntry(const SkeletonId skelId, const DC::ApEntryItem* pDcEntry)
{
	//PROFILE_ACCUM(APEC_TryAddEntry);
	PROFILE(Animation, APEC_TryAddEntry);

	if (!pDcEntry || !pDcEntry)
		return false;

	ListEntryKey key;
	key.m_skelId = skelId;
	key.m_animId = pDcEntry->m_animName;

	{
		AtomicLockJanitorRead readLock(&m_accessLock, FILE_LINE_FUNC);

		if (m_entryTable.Find(key) != m_entryTable.End())
		{
			return true;
		}

		if (m_entryTable.IsFull())
		{
			return false;
		}
	}

	{
		AtomicLockJanitorWrite writeLock(&m_accessLock, FILE_LINE_FUNC);

		// sanity check to see if a competing thread got here the same time we did
		if (m_entryTable.Find(key) != m_entryTable.End())
		{
			return true;
		}

		DistanceTableEntry* pNewTableData = (DistanceTableEntry*)m_dataTable.Alloc();

		ListEntryData newData;
		newData.m_dataTableIndex = m_dataTable.GetBlockIndex(pNewTableData);

		pNewTableData->m_anim = ArtItemAnimHandle();
		pNewTableData->m_cacheSentinel = AnimLookupSentinal();
		pNewTableData->m_dataLock.Set(0);
		pNewTableData->m_distanceTableSize = 0;
		pNewTableData->m_phaseMin = kLargeFloat;
		pNewTableData->m_phaseMax = -kLargeFloat;
		pNewTableData->m_phaseDistMin = -1.0f;
		pNewTableData->m_phaseDistMax = -1.0f;

		m_entryTable.Set(key, newData);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle ApEntryCache::LookupEntryAnim(const SkeletonId skelId, const DC::ApEntryItem* pDcEntry)
{
	PROFILE_ACCUM(APEC_LookupEntryAnim);

	if (!pDcEntry)
	{
		return ArtItemAnimHandle();
	}

	const I32F dataTableIndex = GetDataTableIndex(skelId, pDcEntry->m_animName);

	if (dataTableIndex < 0)
	{
		return ArtItemAnimHandle();
	}

	DistanceTableEntry* pData = (DistanceTableEntry*)m_dataTable.GetElement(dataTableIndex);

	ANIM_ASSERT(pData);
	if (!pData)
	{
		return ArtItemAnimHandle();
	}

	NdAtomic64Janitor dataLock(&pData->m_dataLock);

	if (pData->m_cacheSentinel.IsValid())
	{
		return pData->m_anim;
	}
	else
	{
		return ArtItemAnimHandle();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntryCache::GetPhaseRangeForEntry(const SkeletonId skelId,
										 ArtItemAnimHandle hAnim,
										 const DC::ApEntryItem* pDcEntry,
										 float& phaseMinOut,
										 float& phaseMaxOut)
{
	phaseMinOut = phaseMaxOut = 0.0f;

	const I32F dataTableIndex = GetDataTableIndex(skelId, pDcEntry->m_animName);

	if (dataTableIndex < 0)
	{
		return false;
	}

	DistanceTableEntry* pData = (DistanceTableEntry*)m_dataTable.GetElement(dataTableIndex);

	ANIM_ASSERT(pData);
	if (!pData)
	{
		return false;
	}

	NdAtomic64Janitor dataLock(&pData->m_dataLock);

	if (pData->NeedsRefreshing(pDcEntry, hAnim))
	{
		RefreshDataEntry(pData, skelId, pDcEntry, hAnim);
	}

	if (pData->NeedsRefreshing(pDcEntry, hAnim))
	{
		// somehow our distance refresh failed?
		return false;
	}

	phaseMinOut = pData->m_phaseMin;
	phaseMaxOut = pData->m_phaseMax;

	if (!pDcEntry->m_phaseDistRange)
	{
		phaseMinOut = Limit(phaseMinOut, pDcEntry->m_phaseMin, pDcEntry->m_phaseMax);
		phaseMaxOut = Limit(phaseMaxOut, pDcEntry->m_phaseMin, pDcEntry->m_phaseMax);
	}

	ANIM_ASSERT(IsReasonable(phaseMinOut));
	ANIM_ASSERT(IsReasonable(phaseMaxOut));

	ANIM_ASSERT(phaseMinOut >= 0.0f && phaseMinOut <= 1.0f);
	ANIM_ASSERT(phaseMaxOut >= 0.0f && phaseMaxOut <= 1.0f);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ApEntryCache::TryGetPhaseForDistance(const SkeletonId skelId,
										   const DC::ApEntryItem* pDcEntry,
										   ArtItemAnimHandle anim,
										   const float distance)
{
	//PROFILE_ACCUM(APEC_TryGetPhaseForDistance);
	PROFILE(Animation, APEC_TryAddEntry);

	if (!pDcEntry || !pDcEntry)
	{
		return -1.0f;
	}

	const I32F dataTableIndex = GetDataTableIndex(skelId, pDcEntry->m_animName);

	if (dataTableIndex < 0)
	{
		return -1.0f;
	}

	DistanceTableEntry* pData = (DistanceTableEntry*)m_dataTable.GetElement(dataTableIndex);

	ANIM_ASSERT(pData);
	if (!pData)
	{
		return -1.0f;
	}

	NdAtomic64Janitor dataLock(&pData->m_dataLock);

	if (pData->NeedsRefreshing(pDcEntry, anim) || FALSE_IN_FINAL_BUILD(g_animOptions.m_forceApEntryCacheRefresh))
	{
		RefreshDataEntry(pData, skelId, pDcEntry, anim);
	}

	if (pData->NeedsRefreshing(pDcEntry, anim))
	{
		// somehow our distance refresh failed?
		return -1.0f;
	}

	const float dataPhaseMin = pData->m_phaseMin;
	const float dataPhaseMax = pData->m_phaseMax;

	const float desPhaseMin = pDcEntry->m_phaseDistRange ? dataPhaseMin : Max(pDcEntry->m_phaseMin, dataPhaseMin);
	const float desPhaseMax = pDcEntry->m_phaseDistRange ? dataPhaseMax : Min(pDcEntry->m_phaseMax, dataPhaseMax);

	const U32F tableSize = pData->m_distanceTableSize;

	if (pData->m_distanceTableSize == 0)
	{
		return -1.0f;
	}

	float fNumFrames = float(tableSize - 1);
	const float invTableSize = (tableSize > 1) ? 1.0f / fNumFrames : 0.0f;

	float prevPhase = desPhaseMin;
	float prevDist	= prevDist = MakeF32(pData->m_distanceTable[0]);

	float bestDistanceDiff = kLargeFloat;
	float bestPhase = prevPhase;

	for (U32F iSample = 0; iSample < tableSize; ++iSample)
	{
		const float alpha	  = float(iSample) * invTableSize;
		const float nextPhase = Lerp(dataPhaseMin, dataPhaseMax, alpha);
		const float nextDist  = MakeF32(pData->m_distanceTable[iSample]);

		const float clampedPhaseStart = Max(prevPhase, desPhaseMin);
		const float clampedPhaseEnd	  = Min(nextPhase, desPhaseMax);

		const float clampedDistStart = LerpScaleClamp(prevPhase, nextPhase, prevDist, nextDist, clampedPhaseStart);
		const float clampedDistEnd	 = LerpScaleClamp(prevPhase, nextPhase, prevDist, nextDist, clampedPhaseEnd);

		const float minClampedDist = Min(clampedDistStart, clampedDistEnd);
		const float maxClampedDist = Max(clampedDistStart, clampedDistEnd);

		const float bestLegDist = Limit(distance, minClampedDist, maxClampedDist);
		const float diff		= Abs(bestLegDist - distance);

		if (diff <= bestDistanceDiff)
		{
			bestPhase = LerpScale(clampedDistStart, clampedPhaseEnd, clampedPhaseStart, clampedPhaseEnd, bestLegDist);
			bestDistanceDiff = diff;
		}

		prevPhase = nextPhase;
		prevDist  = nextDist;

		if (nextPhase > desPhaseMax)
		{
			break;
		}
	}

	ANIM_ASSERT(IsReasonable(bestPhase));
	ANIM_ASSERT(bestPhase >= 0.0f && bestPhase <= 1.0f);

	return bestPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F ApEntryCache::GetDataTableIndex(const SkeletonId skelId, StringId64 animId) const
{
	AtomicLockJanitorRead readLock(&m_accessLock, FILE_LINE_FUNC);

	ListEntryKey key;
	key.m_skelId = skelId;
	key.m_animId = animId;

	EntryTable::ConstIterator itr = m_entryTable.Find(key);

	if (itr == m_entryTable.End())
	{
		return -1;
	}

	return itr->m_data.m_dataTableIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntryCache::RefreshDataEntry(DistanceTableEntry* pData,
									const SkeletonId skelId,
									const DC::ApEntryItem* pDcEntry,
									ArtItemAnimHandle anim)
{
	PROFILE(Animation, APEC_RefreshDataEntry);

	if (!pData || !pDcEntry)
		return;

	ANIM_ASSERT(pData->m_dataLock.Get() != 0);

	pData->m_cacheSentinel.Refresh();

	float desPhaseMin = pDcEntry->m_phaseMin;
	float desPhaseMax = pDcEntry->m_phaseMax;

	const ArtItemAnim* pAnim = anim.ToArtItem();

	const StringId64 apChannelId = ApEntry::GetApPivotChannelId(pAnim, pDcEntry);

	if (pDcEntry->m_phaseDistRange)
	{
		const float dist0 = pDcEntry->m_phaseDistRange->m_val0;
		const float dist1 = pDcEntry->m_phaseDistRange->m_val1;

		PhaseMatchParams params;
		params.m_apChannelId = apChannelId;
		params.m_distMode = PhaseMatchDistMode::kXz;

		const float phase0 = pAnim ? ComputePhaseToMatchDistance(skelId, pAnim, dist0, params) : -1.0f;
		const float phase1 = pAnim ? ComputePhaseToMatchDistance(skelId, pAnim, dist1, params) : -1.0f;

		pData->m_phaseDistMin = Min(dist0, dist1);
		pData->m_phaseDistMax = Max(dist0, dist1);

		desPhaseMin = Min(phase0, phase1);
		desPhaseMax = Max(phase0, phase1);
	}
	else if ((anim == pData->m_anim) && (desPhaseMin >= pData->m_phaseMin) && (desPhaseMax <= pData->m_phaseMax))
	{
		return;
	}

	pData->m_anim = anim;
	pData->m_distanceTableSize = 0;

	if (!pAnim)
	{
		return;
	}

	const float phaseMin = Min(pData->m_phaseMin, desPhaseMin);
	const float phaseMax = Max(pData->m_phaseMax, desPhaseMax);

	g_animAlignCache.TryCacheAnim(pAnim, apChannelId, phaseMin, phaseMax);

	const float deltaPhase = (phaseMax - phaseMin);
	const U32F numSamples  = 1 + U32F(deltaPhase * pAnim->m_pClipData->m_fNumFrameIntervals + 0.5f);

	const U32F tableSize	 = Min(U32F(kMaxDistTableSize), numSamples);
	const float invTableSize = tableSize > 1 ? 1.0f / float(tableSize - 1) : 0.0f;

	for (U32F iSample = 0; iSample < tableSize; ++iSample)
	{
		const float alpha = float(iSample) * invTableSize;
		const float phase = Lerp(phaseMin, phaseMax, alpha);

		Locator loc = Locator(kIdentity);
		EvaluateChannelInAnimCached(skelId, pAnim, apChannelId, phase, false, &loc);

		const float nextDist = DistXz(loc.Pos(), kOrigin);

		pData->m_distanceTable[iSample] = MakeF16(nextDist);
	}

	pData->m_distanceTableSize = tableSize;
	pData->m_phaseMin = phaseMin;
	pData->m_phaseMax = phaseMax;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntryCache::EvictAllEntries()
{
	if (!m_initialized)
		return;

	AtomicLockJanitorWrite writeLock(&m_accessLock, FILE_LINE_FUNC);

	m_entryTable.Clear();
	m_dataTable.Clear();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntryCache::DebugPrint() const
{
	STRIP_IN_FINAL_BUILD;

	if (g_animOptions.m_debugPrintApEntryCache)
	{
		AtomicLockJanitorRead readLock(&m_accessLock, FILE_LINE_FUNC);

		MsgCon("-------------------------------------\n");
		MsgCon("ApEntryCache:\n");
		MsgCon("  Entries: %d / %d\n", m_entryTable.Size(), m_entryTable.Size() + m_entryTable.FreeSize());
		MsgCon("  Data: %d / %d\n", m_dataTable.GetNumElementsUsed(), m_dataTable.GetNumElements());
		MsgCon("-------------------------------------\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntryCache::DistanceTableEntry::NeedsRefreshing(const DC::ApEntryItem* pDcEntry, ArtItemAnimHandle newAnim)
{
	ANIM_ASSERT(m_dataLock.Get() != 0);

	if (m_anim != newAnim)
	{
		return true;
	}

	if (pDcEntry->m_phaseDistRange)
	{
		const float phaseDistMin = Min(pDcEntry->m_phaseDistRange->m_val0, pDcEntry->m_phaseDistRange->m_val1);
		const float phaseDistMax = Max(pDcEntry->m_phaseDistRange->m_val0, pDcEntry->m_phaseDistRange->m_val1);

		if ((Abs(phaseDistMin - m_phaseDistMin) > NDI_FLT_EPSILON)
			|| (Abs(phaseDistMax - m_phaseDistMax) > NDI_FLT_EPSILON))
		{
			return true;
		}
	}
	else if ((pDcEntry->m_phaseMin < m_phaseMin) || (pDcEntry->m_phaseMax > m_phaseMax))
	{
		return true;
	}

	if (!m_cacheSentinel.IsValid())
	{
		return true;
	}

	return false;
}
