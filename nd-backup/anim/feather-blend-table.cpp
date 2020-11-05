/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/feather-blend-table.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/feather-blend-defines.h"

#include "gamelib/level/art-item-skeleton.h"

#include <orbisanim/util.h>

/// --------------------------------------------------------------------------------------------------------------- ///
FeatherBlendTable g_featherBlendTable;

/// --------------------------------------------------------------------------------------------------------------- ///
struct FeatherBlendScriptObserver : public ScriptObserver
{
	FeatherBlendScriptObserver()
		: ScriptObserver(FILE_LINE_FUNC)
	{
	}

	virtual void OnModuleImported(StringId64 moduleId, Memory::Context allocContext) override
	{
		if (moduleId == g_featherBlendTable.GetScriptModuleId())
		{
			g_featherBlendTable.RequestDcUpdate();
		}
	}

	virtual void OnSymbolImported(StringId64 moduleId,
								  StringId64 symbol,
								  StringId64 type,
								  const void* pData,
								  const void* pOldData,
								  Memory::Context allocContext) override
	{
		if (type == SID("feather-blend") || type == SID("feather-blend-entry"))
		{
			g_featherBlendTable.RequestDcUpdate();
		}
	}
};

FeatherBlendScriptObserver g_featherBlendScriptObserver;

/// --------------------------------------------------------------------------------------------------------------- ///
void FeatherBlendTable::Init()
{
	AllocateJanitor jj(kAllocAnimation, FILE_LINE_FUNC);

	const size_t maxEntries = 512;

	m_indexTable.Init(maxEntries, FILE_LINE_FUNC);
	m_entries.Init(maxEntries, kAllocAnimation, kAlign16, FILE_LINE_FUNC);

	m_dcModuleId = SID("feather-blends");

	ScriptManager::RegisterObserver(&g_featherBlendScriptObserver);

	const size_t workingMemSize = 512 * 1024;
	m_pWorkingMem = NDI_NEW U8[workingMemSize];
	m_workingMemAllocator.Init(m_pWorkingMem, workingMemSize);

	m_needDcUpdate = false;
	m_usageHw = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct GatherSkelData
{
	SkeletonId m_skelIds[512] = { INVALID_SKELETON_ID };
	U16 m_counts[512] = { 0 };
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool GatherSkelsInUse(const FeatherBlendTable::Entry* pEntry, uintptr_t userData, bool free)
{
	if (free)
		return true;

	GatherSkelData* pData = (GatherSkelData*)userData;

	for (int i = 0; i < ARRAY_COUNT(pData->m_skelIds); ++i)
	{
		if (pData->m_skelIds[i] == INVALID_SKELETON_ID)
		{
			pData->m_skelIds[i] = pEntry->m_skelId;
			pData->m_counts[i]++;
			break;
		}
		else if (pData->m_skelIds[i] == pEntry->m_skelId)
		{
			pData->m_counts[i]++;
			break;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FeatherBlendTable::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (!g_animOptions.m_printFeatherBlendStats)
		return;

	const size_t freeSize  = m_workingMemAllocator.GetFreeSize();
	const size_t highWater = m_workingMemAllocator.GetHighWater();
	const size_t totalSize = m_workingMemAllocator.GetMemSize();
	const U32F numEntries  = m_entries.GetNumElementsUsed();
	const U32F numIndices  = m_indexTable.Size();

	MsgCon("Feather Blend Table [%s]:\n", DevKitOnly_StringIdToString(m_dcModuleId));
	MsgCon(" %d Entries [high: %d]\n", numEntries, m_usageHw);
	MsgCon(" %d Indices\n", numIndices);
	MsgCon(" Memory: %d / %d (%d bytes free) [high: %d]\n", totalSize - freeSize, totalSize, freeSize, highWater);

	{
		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
		GatherSkelData* pData = NDI_NEW GatherSkelData;
		m_entries.ForEachElement(GatherSkelsInUse, (uintptr_t)pData);

		if (pData->m_counts[0])
		{
			MsgCon(" Skels in Use:\n");

			for (int i = 0; i < ARRAY_COUNT(pData->m_skelIds); ++i)
			{
				if (pData->m_skelIds[i] == INVALID_SKELETON_ID)
					break;

				MsgCon("   %d %s\n", pData->m_counts[i], ResourceTable::LookupSkelName(pData->m_skelIds[i]));
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F FeatherBlendTable::LoginFeatherBlend(const StringId64 featherBlendId, const FgAnimData* pAnimData)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_disableFeatherBlends))
	{
		return -1;
	}

	const ArtItemSkeleton* pSkel = pAnimData ? pAnimData->m_animateSkelHandle.ToArtItem() : nullptr;

	if (!pSkel || featherBlendId == INVALID_STRING_ID_64)
	{
		return -1;
	}

	if (!m_indexTable.IsInitialized() || !m_entries.IsInitialized())
	{
		return -1;
	}

	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

	Key k;
	k.m_featherBlendId = featherBlendId;
	k.m_skelId = pSkel->m_skelId;

	I32F foundIndex = -1;
	Entry* pEntry = nullptr;

	IndexHashTable::ConstIterator existingItr = m_indexTable.Find(k);
	if (existingItr != m_indexTable.End())
	{
		foundIndex = existingItr->m_data;
		pEntry = m_entries.GetElement(foundIndex);

		ANIM_ASSERT(m_entries.IsElementUsed(foundIndex));
		ANIM_ASSERTF(pEntry->m_featherBlendId == featherBlendId,
					 ("FB Entry '%d' expected '%s' but is '%s'",
					  foundIndex,
					  DevKitOnly_StringIdToString(featherBlendId),
					  DevKitOnly_StringIdToString(pEntry->m_featherBlendId)));
		ANIM_ASSERT(pEntry->m_skelId == pSkel->m_skelId);
	}
	else if (!m_entries.IsFull())
	{
		const DC::FeatherBlend* pDcData = ScriptManager::LookupInModule<DC::FeatherBlend>(featherBlendId,
																						  m_dcModuleId,
																						  nullptr);

		pEntry = pDcData ? (Entry*)m_entries.Alloc() : nullptr;

		if (pEntry)
		{
			AllocateJanitor jj(&m_workingMemAllocator, FILE_LINE_FUNC);

			if (CreateChannelFactorsForSkel(pDcData, pSkel, &pEntry->m_ppChannelFactors, &pEntry->m_pNumChannelFactors))
			{
				pEntry->m_featherBlendId = featherBlendId;
				pEntry->m_skelId		 = pSkel->m_skelId;
				pEntry->m_numChannelGroups = OrbisAnim::AnimHierarchy_GetNumChannelGroups(pSkel->m_pAnimHierarchy);
				pEntry->m_defaultBlend	   = pDcData->m_defaultBlend;
				pEntry->m_blendTime		   = pDcData->m_blendTime;

				foundIndex = m_entries.GetBlockIndex(pEntry);

				MsgAnimVerbose("FeatherBlend: Setting '%s' skel 0x%x index %d\n",
							   DevKitOnly_StringIdToString(featherBlendId),
							   pSkel->m_skelId.GetValue(),
							   foundIndex);

				m_indexTable.Set(k, foundIndex);
				m_usageHw = Max(m_usageHw, (I32)m_entries.GetNumElementsUsed());
			}
			else
			{
				MsgAnimVerbose("FeatherBlend: Failed to create, deleting entry index %d\n",
							   m_entries.GetBlockIndex(pEntry));

				m_entries.Free(pEntry);
			}
		}
	}

	ANIM_ASSERT(m_entries.GetNumElementsUsed() == m_indexTable.Size());

	return foundIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FeatherBlendTable::LogoutAllEntries(const ArtItemSkeleton* pSkel)
{
	if (!pSkel)
		return;

	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

	AllocateJanitor jj(&m_workingMemAllocator, FILE_LINE_FUNC);

	U32F numEntries = 0;
	U32F numIndices = 0;

	for (Entry& entry : m_entries)
	{
		if (entry.m_skelId == pSkel->m_skelId)
		{
			DeleteEntryData(&entry);

			++numEntries;
		}
	}

	for (IndexHashTable::Iterator itr = m_indexTable.Begin(); itr != m_indexTable.End();)
	{
		const IndexHashTable::HashTableNode* pNode = *itr;

		if (pNode->m_key.m_skelId == pSkel->m_skelId)
		{
			MsgAnimVerbose("FeatherBlend: Deleting '%s' skel 0x%x index %d\n",
						   DevKitOnly_StringIdToString(pNode->m_key.m_featherBlendId),
						   pNode->m_key.m_skelId.GetValue(),
						   pNode->m_data);

			m_entries.FreeIndex(pNode->m_data, true);

			itr = m_indexTable.Erase(itr);

			++numIndices;
		}
		else
		{
			itr++;
		}
	}

	ANIM_ASSERT(m_entries.GetNumElementsUsed() == m_indexTable.Size());
	ANIM_ASSERT(numEntries == numIndices);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const FeatherBlendTable::Entry* FeatherBlendTable::GetEntry(I32F index) const
{
	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

	if ((index < 0) || (index > m_entries.GetNumElements()))
		return nullptr;

	if (!m_entries.IsElementUsed(index))
		return nullptr;

	const Entry* pEntry = (const Entry*)m_entries.GetElement(index);

	return pEntry;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 FeatherBlendTable::GetFeatherBlendId(I32F index) const
{
	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

	if ((index < 0) || (index > m_entries.GetNumElements()))
		return INVALID_STRING_ID_64;

	if (!m_entries.IsElementUsed(index))
		return INVALID_STRING_ID_64;

	const Entry* pEntry = (const Entry*)m_entries.GetElement(index);
	return pEntry ? pEntry->m_featherBlendId : INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float FeatherBlendTable::CreateChannelFactorsForAnimCmd(const ArtItemSkeleton* pSkel,
														I32F index,
														float baseBlendVal,
														const OrbisAnim::ChannelFactor* const** pppChannelFactorsOut,
														const U32** ppCountTableOut,
														float tableBlendValue)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_disableFeatherBlends))
		return baseBlendVal;

	if (index < 0)
		return baseBlendVal;

	if (!pppChannelFactorsOut || !ppCountTableOut)
		return baseBlendVal;

	*pppChannelFactorsOut = nullptr;
	*ppCountTableOut = nullptr;

	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

	if (!m_entries.IsElementUsed(index))
	{
		return baseBlendVal;
	}

	const Entry* pEntry = (const Entry*)m_entries.GetElement(index);

	if (!pEntry)
	{
		return baseBlendVal;
	}

	if (pEntry->m_skelId != pSkel->m_skelId)
	{
		return baseBlendVal;
	}

	if (pEntry->m_numChannelGroups > 0)
	{
		ANIM_ASSERTF(pEntry->m_pNumChannelFactors,
					 ("Feather Blend Entry '%s' Specifies %d Groups but has no count data",
					  DevKitOnly_StringIdToString(pEntry->m_featherBlendId),
					  pEntry->m_numChannelGroups));

		ANIM_ASSERTF(pEntry->m_ppChannelFactors,
					 ("Feather Blend Entry '%s' Specifies %d Groups but has no channel data",
					  DevKitOnly_StringIdToString(pEntry->m_featherBlendId),
					  pEntry->m_numChannelGroups));

		AllocateJanitor jj(kAllocGameToGpuRing, FILE_LINE_FUNC);

		OrbisAnim::ChannelFactor** ppChannelFactors = NDI_NEW OrbisAnim::ChannelFactor*[pEntry->m_numChannelGroups];
		U32* pCountTable = NDI_NEW U32[pEntry->m_numChannelGroups];

		ANIM_ASSERT(ppChannelFactors);
		ANIM_ASSERT(pCountTable);

		for (U32F iChannelGroup = 0; iChannelGroup < pEntry->m_numChannelGroups; ++iChannelGroup)
		{
			const U32F groupCount = pEntry->m_pNumChannelFactors[iChannelGroup];

			pCountTable[iChannelGroup] = groupCount;

			if (groupCount > 0)
			{
				ppChannelFactors[iChannelGroup] = NDI_NEW OrbisAnim::ChannelFactor[groupCount];

				ANIM_ASSERT(ppChannelFactors[iChannelGroup]);

				for (U32F iEntry = 0; iEntry < groupCount; ++iEntry)
				{
					const OrbisAnim::ChannelFactor& entryFactor = pEntry->m_ppChannelFactors[iChannelGroup][iEntry];
					OrbisAnim::ChannelFactor* pFactor = &ppChannelFactors[iChannelGroup][iEntry];

					pFactor->m_channelId   = entryFactor.m_channelId;
					pFactor->m_blendFactor = Limit01(entryFactor.m_blendFactor * baseBlendVal);

					if (tableBlendValue < 1.0f)
					{
						pFactor->m_blendFactor = Lerp(1.0f, pFactor->m_blendFactor, tableBlendValue) * baseBlendVal;
					}
				}
			}
			else
			{
				ppChannelFactors[iChannelGroup] = nullptr;
			}
		}

		*pppChannelFactorsOut = ppChannelFactors;
		*ppCountTableOut = pCountTable;
	}

	if (tableBlendValue < 1.0f)
	{
		return Limit01(Lerp(1.0f, pEntry->m_defaultBlend, tableBlendValue) * baseBlendVal);
	}

	return Limit01(baseBlendVal * pEntry->m_defaultBlend);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FeatherBlendTable::RequestDcUpdate()
{
	AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);
	m_needDcUpdate = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FeatherBlendTable::ApplyDcUpdate()
{
	if (m_needDcUpdate)
	{
		AtomicLockJanitor accessLock(&m_accessLock, FILE_LINE_FUNC);

		m_entries.ForEachUsedElement(RegenEntryData, 0);

		m_needDcUpdate = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
bool FeatherBlendTable::RegenEntryData(U8* pElement, uintptr_t data, bool free)
{
	if (!pElement || free)
		return true;

	Entry* pEntry = (Entry*)pElement;

	const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(pEntry->m_skelId).ToArtItem();
	const DC::FeatherBlend* pDcData = ScriptManager::LookupInModule<DC::FeatherBlend>(pEntry->m_featherBlendId,
																					  g_featherBlendTable.m_dcModuleId,
																					  nullptr);

	AllocateJanitor jj(&g_featherBlendTable.m_workingMemAllocator, FILE_LINE_FUNC);

	DeleteEntryData(pEntry);

	if (CreateChannelFactorsForSkel(pDcData, pSkel, &pEntry->m_ppChannelFactors, &pEntry->m_pNumChannelFactors))
	{
		pEntry->m_defaultBlend = pDcData->m_defaultBlend;
		pEntry->m_blendTime = pDcData->m_blendTime;
	}
	else
	{
		MsgWarn("Failed to recreate feather blend entry data for '%s' (skel '%s'), deleting entry\n",
				DevKitOnly_StringIdToString(pEntry->m_featherBlendId),
				pSkel ? pSkel->GetName() : "<null>");

		Key k;
		k.m_featherBlendId = pEntry->m_featherBlendId;
		k.m_skelId = pEntry->m_skelId;

		IndexHashTable::Iterator itr = g_featherBlendTable.m_indexTable.Find(k);

		if (itr != g_featherBlendTable.m_indexTable.End())
		{
			g_featherBlendTable.m_indexTable.Erase(itr);
		}
		else
		{
			ANIM_ASSERTF(false, ("Orphaned feather blend entry '%s'", DevKitOnly_StringIdToString(pEntry->m_featherBlendId)));
		}

		MsgAnim("FeatherBlend: Failed to regen, deleting entry index %d\n", g_featherBlendTable.m_entries.GetBlockIndex(pEntry));

		// NB: pEntry will be garbage after this call!
		g_featherBlendTable.m_entries.Free(pEntry);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
bool FeatherBlendTable::CreateChannelFactorsForSkel(const DC::FeatherBlend* pDcData,
													const ArtItemSkeleton* pSkel,
													const OrbisAnim::ChannelFactor* const** pppChannelFactorsOut,
													const U32** ppCountTableOut)
{
	if (pppChannelFactorsOut)
	{
		*pppChannelFactorsOut = nullptr;
	}

	if (ppCountTableOut)
	{
		*ppCountTableOut = nullptr;
	}

	if (!pDcData || !pSkel)
	{
		return false;
	}

	ScopedTempAllocator scopedTemp(false, FILE_LINE_FUNC);

	HeapAllocator* pTempMemAllocator = scopedTemp.GetAllocatorInstance();

	struct WorkingEntry
	{
		U32 m_iChannelGroup = -1;
		U32 m_iCgLocalJointIndex = -1;
	};

	WorkingEntry* aWorkingList = nullptr;

	if (pDcData->m_count > 0)
	{
		AllocateJanitor temp(pTempMemAllocator, FILE_LINE_FUNC);
		aWorkingList = NDI_NEW WorkingEntry[pDcData->m_count];
	}

	const U32F numChannelGroups = OrbisAnim::AnimHierarchy_GetNumChannelGroups(pSkel->m_pAnimHierarchy);
	const U32F numProcessingGroups = OrbisAnim::AnimHierarchy_GetNumProcessingGroups(pSkel->m_pAnimHierarchy);

	U32* pCountTable = NDI_NEW U32[numChannelGroups];
	OrbisAnim::ChannelFactor** apFactorTable = NDI_NEW OrbisAnim::ChannelFactor*[numChannelGroups];

	for (U32F iChannelGroup = 0; iChannelGroup < numChannelGroups; ++iChannelGroup)
	{
		pCountTable[iChannelGroup] = 0;
		apFactorTable[iChannelGroup] = nullptr;
	}

	for (U32F iEntry = 0; iEntry < pDcData->m_count; ++iEntry)
	{
		const DC::FeatherBlendEntry& entry = pDcData->m_entries[iEntry];

		const I64 iOutputJoint = FindJoint(pSkel->m_pJointDescs, pSkel->m_numTotalJoints, entry.m_jointName);
		if (iOutputJoint < 0)
		{
			MsgWarn("feather-blend '%s' references non-existent joint '%s' for skeleton '%s'\n",
					DevKitOnly_StringIdToString(pDcData->m_name),
					DevKitOnly_StringIdToString(entry.m_jointName),
					ResourceTable::LookupSkelName(pSkel->m_skelId));
			continue;
		}

		const U32 iAnimatedJoint = OrbisAnim::AnimHierarchy_OutputJointIndexToAnimatedJointIndex(pSkel->m_pAnimHierarchy, iOutputJoint);

		if (iAnimatedJoint == -1)
		{
			MsgWarn("feather-blend '%s' skeleton '%s' can't use joint '%s' as it's not in the animated joint set\n",
					DevKitOnly_StringIdToString(pDcData->m_name),
					ResourceTable::LookupSkelName(pSkel->m_skelId),
					DevKitOnly_StringIdToString(entry.m_jointName));
			continue;
		}

		U32F iChannelGroup = -1;
		U32F iCgLocalJointIndex = -1;

		for (U32F iProcessingGroup = 0; iProcessingGroup < numProcessingGroups; ++iProcessingGroup)
		{
			const OrbisAnim::ProcessingGroup* pProcessingGroup = OrbisAnim::AnimHierarchy_GetProcessingGroup(pSkel->m_pAnimHierarchy, iProcessingGroup);

			if (!pProcessingGroup)
				continue;

			if ((iAnimatedJoint >= pProcessingGroup->m_firstAnimatedJoint) &&
				(iAnimatedJoint < (pProcessingGroup->m_firstAnimatedJoint + pProcessingGroup->m_numAnimatedJoints)))
			{
				const I32F iSegment = OrbisAnim::AnimHierarchy_ProcessingGroupIndexToSegmentIndex(pSkel->m_pAnimHierarchy,
																								  iProcessingGroup);
				const OrbisAnim::AnimHierarchySegment* pSegment = OrbisAnim::AnimHierarchy_GetSegment(pSkel->m_pAnimHierarchy,
																									  iSegment);

				ANIM_ASSERT(pSegment);

				iChannelGroup = pSegment->m_firstChannelGroup + pProcessingGroup->m_firstChannelGroup;
				iCgLocalJointIndex = iAnimatedJoint - pProcessingGroup->m_firstAnimatedJoint;
				break;
			}
		}

		if (iChannelGroup == -1)
		{
			continue;
		}

		const U32F curCount = pCountTable[iChannelGroup];
		++(pCountTable[iChannelGroup]);

		aWorkingList[iEntry].m_iChannelGroup = iChannelGroup;
		aWorkingList[iEntry].m_iCgLocalJointIndex = iCgLocalJointIndex;
	}

	for (U32F iChannelGroup = 0; iChannelGroup < numChannelGroups; ++iChannelGroup)
	{
		if (pCountTable[iChannelGroup] == 0)
			continue;

		apFactorTable[iChannelGroup] = NDI_NEW OrbisAnim::ChannelFactor[pCountTable[iChannelGroup]];
		
		U32F iLocalEntry = 0;

		for (U32F iEntry = 0; iEntry < pDcData->m_count; ++iEntry)
		{
			const DC::FeatherBlendEntry& entry = pDcData->m_entries[iEntry];

			if (aWorkingList[iEntry].m_iChannelGroup == iChannelGroup)
			{
				apFactorTable[iChannelGroup][iLocalEntry].m_blendFactor = entry.m_blendFactor;
				apFactorTable[iChannelGroup][iLocalEntry].m_channelId = aWorkingList[iEntry].m_iCgLocalJointIndex;
				++iLocalEntry;
			}
		}

		ANIM_ASSERT(iLocalEntry == pCountTable[iChannelGroup]);
	}

	if (pppChannelFactorsOut)
	{
		*pppChannelFactorsOut = apFactorTable;
	}

	if (ppCountTableOut)
	{
		*ppCountTableOut = pCountTable;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void FeatherBlendTable::DeleteEntryData(Entry* pEntry)
{
	if (!pEntry)
		return;

	if (pEntry->m_pNumChannelFactors)
	{
		NDI_DELETE [] pEntry->m_pNumChannelFactors;
		pEntry->m_pNumChannelFactors = nullptr;
	}

	if (pEntry->m_ppChannelFactors)
	{
		for (U32F iChannelGroup = 0; iChannelGroup < pEntry->m_numChannelGroups; ++iChannelGroup)
		{
			if (pEntry->m_ppChannelFactors[iChannelGroup])
			{
				NDI_DELETE [] pEntry->m_ppChannelFactors[iChannelGroup];
			}
		}
		NDI_DELETE [] pEntry->m_ppChannelFactors;
		pEntry->m_ppChannelFactors = nullptr;
	}
}
