/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-overlay.h"

#include "corelib/math/mathutils.h"
#include "corelib/memory/relocate.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/anim-overlay-defines.h"
#include "ndlib/scriptx/h/dc-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct OverlayScriptObserver : public ScriptObserver
{
	bool m_needUpdate;

	OverlayScriptObserver() : ScriptObserver(FILE_LINE_FUNC)
		, m_needUpdate(false)
	{
	}

	virtual void OnModuleImported(StringId64 moduleId, Memory::Context allocContext) override
	{
		if (m_needUpdate)
		{
			m_needUpdate = false;
			UpdateAnimOverlays();
		}
	}

	virtual void OnModuleRelocated(StringId64 moduleId,
								   Memory::Context allocContext,
								   ptrdiff_t deltaPos,
								   uintptr_t lowerBound,
								   uintptr_t upperBound) override
	{
		if (m_needUpdate)
		{
			m_needUpdate = false;
			UpdateAnimOverlays();
		}
	}

	virtual void OnSymbolImported(StringId64 moduleId,
								  StringId64 symbol,
								  StringId64 type,
								  const void* pData,
								  const void* pOldData,
								  Memory::Context allocContext) override
	{
		if (type == SID("anim-overlay-set") || type == SID("anim-overlay-layer-priorities")
			|| type == SID("player-movement-anim-list"))
		{
			m_needUpdate = true;
		}						
	}

	virtual void OnSymbolRelocated(StringId64 moduleId,
								   StringId64 symbol,
								   StringId64 type,
								   const void* pOldData,
								   ptrdiff_t deltaPos,
								   uintptr_t lowerBound,
								   uintptr_t upperBound,
								   Memory::Context allocContext) override
	{
		if (type == SID("anim-overlay-set") || type == SID("anim-overlay-layer-priorities")
			|| type == SID("player-movement-anim-list"))
		{
			m_needUpdate = true;
		}						
	}

	static void UpdateAnimDataForNewOverlay(FgAnimData* pAnimData, uintptr_t data)
	{
		if (pAnimData && pAnimData->m_pAnimControl)
		{
			if (AnimOverlays* pOverlays = pAnimData->m_pAnimControl->GetAnimOverlays())
			{
				pOverlays->NotifyDcUpdated();
				pAnimData->m_pAnimControl->DebugOnly_ForceUpdateOverlaySnapshot(pOverlays->GetSnapshot());
				pAnimData->m_pAnimControl->ReloadScriptData();
			}
		}
	}

	void UpdateAnimOverlays()
	{		
		if (EngineComponents::GetAnimMgr())
			EngineComponents::GetAnimMgr()->ForAllUsedAnimData(UpdateAnimDataForNewOverlay);		
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
static const U32F kNumVariantIndices = 4;
static const U32F kVariantArraySize	 = sizeof(VariantIndex) * kNumVariantIndices;

static bool s_overlayScriptObserverRegistered = false;
static OverlayScriptObserver s_overlayScriptObserver;

STATIC_ASSERT((sizeof(AnimOverlays) % 16) == 0);
STATIC_ASSERT((kVariantArraySize % 16) == 0);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimOverlays::AnimOverlays()
	: m_pOverlaySnapshot(nullptr)
	, m_pPriorities(nullptr)
	, m_priorityNameId(INVALID_STRING_ID_64)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlays::Init(U32 instanceSeed, StringId64 priorityNameId, bool uniDirectional)
{
	m_priorityNameId = priorityNameId;

	const DC::AnimOverlayLayerPriorities* pPriorities = ScriptManager::Lookup<DC::AnimOverlayLayerPriorities>(priorityNameId);
	ANIM_ASSERT(pPriorities);
	m_pPriorities = pPriorities;

	const U32F numOverlayLayers = pPriorities->m_layerToPriority->m_count;
	m_pOverlaySnapshot = NDI_NEW AnimOverlaySnapshot;
	m_pOverlaySnapshot->Init(numOverlayLayers, uniDirectional);
	m_pOverlaySnapshot->SetInstanceSeed(instanceSeed);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F AnimOverlays::GetLayerIndex(StringId64 layerId) const
{
	ANIM_ASSERT(m_pPriorities);
	const I32* pIndex = ScriptManager::MapLookup<I32>(m_pPriorities->m_layerToPriority, layerId);
	return pIndex ? *pIndex : -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimOverlays::GetLayerName(I32F index) const
{
	ANIM_ASSERT(m_pPriorities);

	for (U32F mapIndex = 0; mapIndex < m_pPriorities->m_layerToPriority->m_count; ++mapIndex)
	{
		const I32 layerIndex = *(I32*)m_pPriorities->m_layerToPriority->m_data[mapIndex].m_ptr;
		if (layerIndex == index)
		{
			return m_pPriorities->m_layerToPriority->m_keys[mapIndex];
		}
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlays::ClearLayer(StringId64 layerId)
{
	const I32F iLayer = GetLayerIndex(layerId);
	if (iLayer < 0)
		return;

	ANIM_ASSERT(iLayer < GetNumLayers());

	m_pOverlaySnapshot->ClearLayer(iLayer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOverlays::SetOverlaySet(StringId64 overlaySetName, StringId64 nameSpace, bool allowMissing)
{
	const DC::AnimOverlaySet* pSet = ScriptManager::LookupInNamespace<DC::AnimOverlaySet>(overlaySetName,
																						  nameSpace,
																						  nullptr);

	ANIM_ASSERTF(pSet || allowMissing,
		("Anim Overlay Set '%s' not found\n", DevKitOnly_StringIdToString(overlaySetName)));

	if (!pSet)
	{
		return false;
	}

	return SetOverlaySet(pSet, allowMissing);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOverlays::SetOverlaySet(StringId64 overlaySetName, bool allowMissing)
{
	const DC::AnimOverlaySet* pSet = ScriptManager::Lookup<DC::AnimOverlaySet>(overlaySetName, nullptr);

	ANIM_ASSERTF(pSet || allowMissing,
				 ("Anim Overlay Set '%s' not found\n", DevKitOnly_StringIdToString(overlaySetName)));

	if (!pSet)
	{
		return false;
	}

	return SetOverlaySet(pSet, allowMissing);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOverlays::IsOverlaySet(StringId64 overlaySetName) const
{
	ANIM_ASSERT(m_pOverlaySnapshot);
	if (!m_pOverlaySnapshot)
		return false;

	const DC::AnimOverlaySet* pOverlaySet = ScriptManager::Lookup<DC::AnimOverlaySet>(overlaySetName, nullptr);
	if (pOverlaySet)
	{	
		I32 index = GetLayerIndex(pOverlaySet->m_layerId);
		ANIM_ASSERT(index >= 0);
		if ((index >= 0) && (m_pOverlaySnapshot->m_ppOverlaySets[index] == pOverlaySet))
			return true;		
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOverlays::SetOverlaySet(const DC::AnimOverlaySet* pOverlaySet, bool allowMissing)
{
	ANIM_ASSERT(m_pOverlaySnapshot);
	if (!m_pOverlaySnapshot)
		return false;

	ANIM_ASSERT(allowMissing || pOverlaySet);
	if (!pOverlaySet)
		return false;

	const I32F index = GetLayerIndex(pOverlaySet->m_layerId);
	ANIM_ASSERTF(allowMissing || index >= 0,
				 ("Can't find layer entry '%s' for overlay '%s' -- did you forget to add it to the priorities list for '%s'?",
				  DevKitOnly_StringIdToString(pOverlaySet->m_layerId),
				  DevKitOnly_StringIdToString(pOverlaySet->m_name),
				  m_pPriorities ? DevKitOnly_StringIdToString(m_pPriorities->m_baseName) : "<null>"));

	if (index < 0)
		return false;

	m_pOverlaySnapshot->SetOverlaySet(index, pOverlaySet);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOverlays::SetOverlaySetByLayer(const DC::AnimOverlaySet* pOverlaySet, StringId64 layerId)
{
	ANIM_ASSERT(m_pOverlaySnapshot);
	if (!m_pOverlaySnapshot)
		return false;

	const I32F index = GetLayerIndex(layerId);
	ANIM_ASSERTF(index >= 0,
				("Can't find layer entry '%s' for overlay '%s' -- did you forget to add it to the priorities list for '%s'?",
				 DevKitOnly_StringIdToString(pOverlaySet->m_layerId),
				 DevKitOnly_StringIdToString(pOverlaySet->m_name),
				 m_pPriorities ? DevKitOnly_StringIdToString(m_pPriorities->m_baseName) : "<null>"));

	if (index < 0)
		return false;

	m_pOverlaySnapshot->SetOverlaySet(index, pOverlaySet);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimOverlays::GetCurrentOverlay(StringId64 layerId) const
{
	if (const DC::AnimOverlaySet* pAnimOverlaySet = GetCurrentOverlaySet(layerId))
	{
		return pAnimOverlaySet->m_name;
	}
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimOverlaySet* AnimOverlays::GetCurrentOverlaySet(StringId64 layerId) const
{
	ANIM_ASSERT(m_pOverlaySnapshot);
	if (!m_pOverlaySnapshot)
		return nullptr;

	const I32F index = GetLayerIndex(layerId);
	if (index < 0)
		return nullptr;

	return m_pOverlaySnapshot->m_ppOverlaySets[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlays::NotifyDcUpdated()
{
	ANIM_ASSERT(m_pOverlaySnapshot);
	if (!m_pOverlaySnapshot)
		return;

	const U32F numOverlayLayers = m_pOverlaySnapshot->GetNumLayers();
	//Update the overlayset pointers
	const DC::AnimOverlaySet** ppOverlaySets = m_pOverlaySnapshot->m_ppOverlaySets;

	for (U32F iOverlay = 0; iOverlay < numOverlayLayers; ++iOverlay)
	{
		ppOverlaySets[iOverlay] = ScriptManager::Lookup<DC::AnimOverlaySet>(m_pOverlaySnapshot->m_overlaySetIds[iOverlay],
																			nullptr);

		if (ppOverlaySets[iOverlay] == nullptr)
		{
			m_pOverlaySnapshot->m_overlaySetIds[iOverlay] = INVALID_STRING_ID_64;
		}
	}

	//Update the priorities
	const DC::AnimOverlayLayerPriorities* pNewPriorities = ScriptManager::Lookup<DC::AnimOverlayLayerPriorities>(m_priorityNameId,
																												 nullptr);

	if (pNewPriorities && numOverlayLayers >= pNewPriorities->m_layerToPriority->m_count)
	{
		ScopedTempAllocator jj(FILE_LINE_FUNC);
		U32F prevNumOverlayLayers = numOverlayLayers;
		const DC::AnimOverlaySet** prevOverlaySets = NDI_NEW const DC::AnimOverlaySet*[prevNumOverlayLayers];
		memcpy(prevOverlaySets, ppOverlaySets, sizeof(const DC::AnimOverlaySet*) * prevNumOverlayLayers);

		m_pPriorities = pNewPriorities;
		m_pOverlaySnapshot->m_numOverlayLayers = (U8)m_pPriorities->m_layerToPriority->m_count;
		memset(ppOverlaySets, 0, sizeof(const DC::AnimOverlaySet*) * m_pOverlaySnapshot->m_numOverlayLayers);
		memset(m_pOverlaySnapshot->m_overlaySetIds, 0, sizeof(StringId64) * m_pOverlaySnapshot->m_numOverlayLayers);

		for (U32F iSet = 0; iSet < prevNumOverlayLayers; ++iSet)
		{
			if (prevOverlaySets[iSet] && GetLayerIndex(prevOverlaySets[iSet]->m_layerId) >= 0)
			{
				SetOverlaySet(prevOverlaySets[iSet]);
			}
		}
	}
	else
	{
		ANIM_ASSERTF(false, ("MR error: Removed or increased size of anim-overlay priorities"));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimOverlays::GetVariantIndex(StringId64 animNameId) const
{
	const AnimOverlaySnapshot* pOverlaySnapshot = m_pOverlaySnapshot;
	return pOverlaySnapshot->GetVariantIndex(animNameId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimOverlays::GetVariantIndexAndIncrement(StringId64 animNameId, U32F numVariants, bool randomize)
{
	AnimOverlaySnapshot* pOverlaySnapshot = m_pOverlaySnapshot;
	return pOverlaySnapshot->GetVariantIndexAndIncrement(animNameId, numVariants, randomize);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimOverlays::LookupTransformedAnimId(StringId64 animNameId) const
{
	CachedAnimOverlayLookup lookup;
	lookup.SetSourceId(animNameId);	
	lookup = LookupTransformedAnimId(lookup);
	return lookup.GetResolvedId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
CachedAnimOverlayLookup AnimOverlays::LookupTransformedAnimId(const CachedAnimOverlayLookup& lookup) const
{
	ANIM_ASSERT(m_pOverlaySnapshot);
	const AnimOverlaySnapshot* pOverlaySnapshot = m_pOverlaySnapshot;
	return pOverlaySnapshot->LookupTransformedAnimId(lookup);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimOverlaySnapshot::Result AnimOverlays::DevKitOnly_LookupTransformedAnimId(StringId64 animNameId) const
{
	ANIM_ASSERT(m_pOverlaySnapshot);
	const AnimOverlaySnapshot* pOverlaySnapshot = m_pOverlaySnapshot;
	return pOverlaySnapshot->DevKitOnly_LookupTransformedAnimId(animNameId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlays::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocateObject(m_pOverlaySnapshot, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pOverlaySnapshot, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pPriorities, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimOverlays::GetNumLayers() const
{
	const AnimOverlaySnapshot* pOverlaySnapshot = m_pOverlaySnapshot;
	return pOverlaySnapshot->GetNumLayers();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlays::RegisterScriptObserver()
{
	// Register script observer.
	if (!s_overlayScriptObserverRegistered)
	{
		s_overlayScriptObserverRegistered = true;
		ScriptManager::RegisterObserver(&s_overlayScriptObserver);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlays::DebugPrint() const
{
	const char* szDbgName = GetDebugName();

	if (szDbgName != nullptr)
		MsgCon("Overlays: %s, Hash: %llu\n", szDbgName, m_pOverlaySnapshot->GetHash().GetValue());
	else
		MsgCon("Overlays:, Hash: %llu\n", m_pOverlaySnapshot->GetHash().GetValue());

	for (U32F iSet = 0; iSet < m_pOverlaySnapshot->m_numOverlayLayers; ++iSet)
	{
		MsgCon("%2d: %s\n",
			   iSet,
			   m_pOverlaySnapshot->m_ppOverlaySets[iSet]
				   ? DevKitOnly_StringIdToString(m_pOverlaySnapshot->m_ppOverlaySets[iSet]->m_name)
				   : "(none)");
	}
}

/************************************************************************/
/* AnimOverlaySnapshot                                                  */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
AnimOverlaySnapshot::AnimOverlaySnapshot()
	: m_ppOverlaySets(nullptr)
	, m_numOverlayLayers(0)
	, m_szDbgName(nullptr)
{
	m_uniDirectional = false;
	m_instanceSeed = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOverlaySnapshot::Init(const U32F numLayers, bool uniDirectional)
{
	ANIM_ASSERT(numLayers < 256);

	m_numOverlayLayers = (U8)numLayers;
	m_uniDirectional = uniDirectional;

	if (numLayers > 0)
	{
		// We need this list to be a multiple of 16 bytes in size; aligned on 16 byte boundry
		const U64 alignedCount = AlignSize(numLayers, kAlign4);
		m_ppOverlaySets = NDI_NEW (kAlign16) const DC::AnimOverlaySet*[alignedCount];
		memset(m_ppOverlaySets, 0, sizeof(const DC::AnimOverlaySet*) * alignedCount);
	}
	else
	{
		m_ppOverlaySets = nullptr;
	}

	m_pVariantIndexArray = NDI_NEW(kAlign16) VariantIndex[kNumVariantIndices];
	memset(m_pVariantIndexArray, 0, sizeof(VariantIndex) * kNumVariantIndices);

	m_overlaySetIds = NDI_NEW StringId64[m_numOverlayLayers];
	memset(m_overlaySetIds, 0, sizeof(StringId64) * m_numOverlayLayers);

	RefreshHash();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlaySnapshot::CopyFrom(const AnimOverlaySnapshot* pSourceSnapshot)
{
	if (!pSourceSnapshot)
		return;

	ANIM_ASSERT(pSourceSnapshot->m_numOverlayLayers == m_numOverlayLayers);

	const U32F setListSize = sizeof(DC::AnimOverlaySet*) * AlignSize(m_numOverlayLayers, kAlign4);
	const DC::AnimOverlaySet* const* ppSourceSets = pSourceSnapshot->m_ppOverlaySets;
	const DC::AnimOverlaySet** ppDestSets = m_ppOverlaySets;

	memcpy(ppDestSets, ppSourceSets, setListSize);
	memcpy(m_overlaySetIds, pSourceSnapshot->m_overlaySetIds, sizeof(StringId64) * m_numOverlayLayers);

	InternalUpdateVariantsFrom(pSourceSnapshot);

	m_currentHash = pSourceSnapshot->m_currentHash;
	m_instanceSeed = pSourceSnapshot->m_instanceSeed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlaySnapshot::CopyInclude(const AnimOverlaySnapshot* pSourceSnapshot, const I32* layerIndices, const I32 numLayers)
{
	if (!pSourceSnapshot)
		return;

	if (numLayers == 0)
		return;

	ANIM_ASSERT(pSourceSnapshot->m_numOverlayLayers == m_numOverlayLayers);

	const DC::AnimOverlaySet* const* ppSourceSets1 = pSourceSnapshot->m_ppOverlaySets;
	const StringId64* pSourceSets2 = pSourceSnapshot->m_overlaySetIds;

	for (int ii = 0; ii < numLayers; ii++)
	{
		I32 layerIndex = layerIndices[ii];
		ANIM_ASSERT(layerIndex >= 0 && layerIndex < m_numOverlayLayers);		
		m_ppOverlaySets[layerIndex] = ppSourceSets1[layerIndex];
		m_overlaySetIds[layerIndex] = pSourceSets2[layerIndex];
	}

	InternalUpdateVariantsFrom(pSourceSnapshot);

	m_instanceSeed = pSourceSnapshot->m_instanceSeed;

	RefreshHash();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlaySnapshot::CopyExclude(const AnimOverlaySnapshot* pSourceSnapshot, const I32* layerIndices, const I32 numLayers)
{
	if (!pSourceSnapshot)
		return;

	ANIM_ASSERT(pSourceSnapshot->m_numOverlayLayers == m_numOverlayLayers);

	if (numLayers == 0)
	{
		CopyFrom(pSourceSnapshot);
		return;
	}

	// store temp values
	const DC::AnimOverlaySet** pSkippedSets1 = STACK_ALLOC(const DC::AnimOverlaySet*, numLayers);
	StringId64* pSkippedSets2 = STACK_ALLOC(StringId64, numLayers);

	for (int ii = 0; ii < numLayers; ii++)
	{
		I32 layerIndex = layerIndices[ii];
		ANIM_ASSERT(layerIndex >= 0 && layerIndex < m_numOverlayLayers);
		pSkippedSets1[ii] = m_ppOverlaySets[layerIndex];
		pSkippedSets2[ii] = m_overlaySetIds[layerIndex];
	}

	CopyFrom(pSourceSnapshot);

	// restore skipped values.
	for (int ii = 0; ii < numLayers; ii++)
	{
		I32 layerIndex = layerIndices[ii];
		ANIM_ASSERT(layerIndex >= 0 && layerIndex < m_numOverlayLayers);
		m_ppOverlaySets[layerIndex] = pSkippedSets1[ii];
		m_overlaySetIds[layerIndex] = pSkippedSets2[ii];
	}

	RefreshHash();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlaySnapshot::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_ppOverlaySets, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pVariantIndexArray, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_overlaySetIds, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimOverlaySnapshot::LookupTransformedAnimId(StringId64 animNameId) const
{
	return const_cast<AnimOverlaySnapshot*>(this)->InternalLookupTransformedAnimId(animNameId, false, false).m_animId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimOverlaySnapshot::Result AnimOverlaySnapshot::DevKitOnly_LookupTransformedAnimId(StringId64 animNameId) const
{
	return const_cast<AnimOverlaySnapshot*>(this)->InternalLookupTransformedAnimId(animNameId, false, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimOverlaySnapshot::LookupTransformedAnimIdAndIncrementVariantIndices(StringId64 animNameId)
{
	return InternalLookupTransformedAnimId(animNameId, true, false).m_animId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimOverlaySnapshot::Result AnimOverlaySnapshot::InternalLookupTransformedAnimId(StringId64 animNameId,
																				 bool incrementVariantIndices,
																				 bool debugHistory)
{
	PROFILE(Animation, AOS_LookupTransformedAnimId);

#ifdef ANIM_DEBUG
	const U64 startTime = TimerGetRawCount();
#endif

	if (INVALID_STRING_ID_64 == animNameId)
		return Result(INVALID_STRING_ID_64, nullptr);

	StringId64 result = animNameId;
	DbgTransformHistory* pHistory = nullptr;
	if (FALSE_IN_FINAL_BUILD(debugHistory))
	{
		Memory::Context context = kAllocSingleFrame;
		pHistory = NDI_NEW(context) DbgTransformHistory();
		pHistory->Allocate(context);
	}

	AnimOverlayIterator itr = GetNextEntry(AnimOverlayIterator(animNameId));
	if (FALSE_IN_FINAL_BUILD(pHistory != nullptr))
		pHistory->Append(itr);

	int loopCount = 0;
	while (itr.IsValid())
	{
		const DC::AnimOverlaySetEntry* pEntry = itr.GetEntry();
		ANIM_ASSERT(pEntry);

		switch (pEntry->m_type)
		{
		case DC::kAnimOverlayTypeReplace:
			result = pEntry->m_remapId;
			ANIM_ASSERT(result != INVALID_STRING_ID_64);
			if (IsUniDirectional() || itr.IsUnidirectional())
			{
				itr.UpdateSourceId(result);
			}
			else
			{
				itr = AnimOverlayIterator(result);
			}
			break;

		case DC::kAnimOverlayTypeVariant:
		case DC::kAnimOverlayTypeInstanceVariant:
		case DC::kAnimOverlayTypeRandom:
			{
				const DC::AnimOverlaySetEntry* variantList[16];
				U32F numVariants = 0;
				const DC::AnimOverlaySetEntry* pCurEntry = pEntry;
				const StringId64 sourceId = itr.GetSourceId();
				const StringId64 variantId = pEntry->m_variantGroup;
				U32 incrementGroupIndex = pEntry->m_flags & DC::kAnimOverlayFlagsChooseVariantIndex;
				AnimOverlayIterator prevItr = itr;
				do 
				{
					ANIM_ASSERT(pCurEntry->m_variantGroup == variantId);
					variantList[numVariants++] = pCurEntry;
					incrementGroupIndex |= pCurEntry->m_flags & DC::kAnimOverlayFlagsChooseVariantIndex;
					ANIM_ASSERT(numVariants <= ARRAY_ELEMENT_COUNT(variantList));
					prevItr = itr;
					itr = GetNextEntry(itr);

					const DC::AnimOverlaySetEntry* pNextEntry = itr.GetEntry();
					if (!pNextEntry)
						break;

					pCurEntry = pNextEntry;
					if (pCurEntry->m_type != pEntry->m_type
						|| pCurEntry->m_variantGroup != variantId)
						break;
				}
				while (numVariants < ARRAY_ELEMENT_COUNT(variantList));

				U32F baseIndex = 0;
				if (pEntry->m_type == DC::kAnimOverlayTypeInstanceVariant)
				{
					baseIndex = m_instanceSeed;
				}
				else if (variantId != INVALID_STRING_ID_64 && variantId != sourceId)
				{
					if (incrementGroupIndex && incrementVariantIndices)
					{
						const bool randomize = (pEntry->m_type == DC::kAnimOverlayTypeRandom);

						baseIndex = GetVariantIndexAndIncrement(variantId, numVariants, randomize);
					}
					else
					{
						baseIndex = GetVariantIndex(variantId);
					}
				}
				else if (incrementVariantIndices)
				{
					const bool randomize = (pEntry->m_type == DC::kAnimOverlayTypeRandom);

					baseIndex = GetVariantIndexAndIncrement(sourceId, numVariants, randomize);
				}
				else
				{
					baseIndex = GetVariantIndex(sourceId);
				}

				const U32F selectedIndex = baseIndex % numVariants;
				ANIM_ASSERT(selectedIndex < numVariants);
				const DC::AnimOverlaySetEntry* pSelectedRemap = variantList[selectedIndex];

				// restart the search with our new selected variant
				result = pSelectedRemap->m_remapId;
				ANIM_ASSERT(result != INVALID_STRING_ID_64);

				if (IsUniDirectional() || itr.IsUnidirectional())
				{
					itr.UpdateSourceId(result);
				}
				else if (result == sourceId)
				{
					//keep the current iterator
					itr = prevItr;
				}
				else
				{
					itr = AnimOverlayIterator(result);
				}
			}
			break;

		case DC::kAnimOverlayTypeBlend:
		case DC::kAnimOverlayTypeTree:
			// do nothing, just keep remapping the base animation
			break;
		}

		if (++loopCount == 100)
		{
			static bool errorReported = false;

			if (!errorReported)
			{
				AnimOverlayIterator errorItr = GetNextEntry(AnimOverlayIterator(animNameId));
				if (errorItr.GetLayerIndex() >= 0)
				{
					MsgConErr("Circular remap chain detected in remap starting with anim '%s' in overlay set '%s'\n",
							  DevKitOnly_StringIdToString(animNameId),
							  DevKitOnly_StringIdToString(m_ppOverlaySets[errorItr.GetLayerIndex()]->m_name));
				}
				errorReported = true;
			}

			break;
		}

		itr = GetNextEntry(itr);
		
		if (FALSE_IN_FINAL_BUILD(pHistory != nullptr))
			pHistory->Append(itr);
	}
	
#ifdef ANIM_DEBUG
	const U64 endTime = TimerGetRawCount();

	g_animOptions.m_overlayLookupTime += ConvertTicksToSeconds(startTime, endTime);
#endif
	
	return Result(result, pHistory);
}

/// --------------------------------------------------------------------------------------------------------------- ///
CachedAnimOverlayLookup AnimOverlaySnapshot::LookupTransformedAnimId(const CachedAnimOverlayLookup& lookup) const
{
	if (g_animOptions.m_enableOverlayCacheLookup)
	{
		if (lookup.GetHash() == m_currentHash)
		{
			if (FALSE_IN_FINAL_BUILD(g_animOptions.m_validateCachedAnimLookups))
			{				
				//Make sure the actual result matches the cached result.
				const StringId64 result = LookupTransformedAnimId(lookup.GetSourceId());
				ANIM_ASSERTF(result == lookup.GetResolvedId(),
							 ("CachedAnimOverlayLookup: actual result '%s' did not match cached result '%s'.",
							  DevKitOnly_StringIdToString(result),
							  DevKitOnly_StringIdToString(lookup.GetResolvedId())));
			}

			return lookup;
		}
	}

	const StringId64 sourceId = lookup.GetSourceId();
	const StringId64 resultId = LookupTransformedAnimId(sourceId);
	CachedAnimOverlayLookup overlayRes = CachedAnimOverlayLookup(sourceId, resultId, m_currentHash);
	
#ifdef ANIM_DEBUG
	ANIM_ASSERT((overlayRes.GetSourceId() == INVALID_STRING_ID_64)
				|| (overlayRes.GetResolvedId() != INVALID_STRING_ID_64));
#endif

	return overlayRes;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimOverlaySnapshot::GetVariantIndex(StringId64 animNameId) const
{
	ANIM_ASSERT(m_pVariantIndexArray);
	if (!m_pVariantIndexArray)
		return 0;

	const VariantIndex* pIndices = m_pVariantIndexArray;
	for (U32F i = 0; i < kNumVariantIndices; i++)
	{
		if (pIndices[i].m_animNameId == animNameId)
		{
			return pIndices[i].m_index;
		}
	}

	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimOverlaySnapshot::GetVariantIndexAndIncrement(StringId64 animNameId, U32F numVariants, bool randomize)
{
	VariantIndex* pIndices = m_pVariantIndexArray;

	U32F index = kNumVariantIndices - 1;
	VariantIndex newVariant;
	newVariant.m_animNameId = animNameId;
	newVariant.m_index = 0;

	for (U32F i = 0; i < kNumVariantIndices; i++)
	{
		if (pIndices[i].m_animNameId == animNameId)
		{
			index = i;
			newVariant.m_index = pIndices[i].m_index;
			break;
		}
	}

	for (U32F j = index; j >= 1; j--)
	{
		pIndices[j] = pIndices[j - 1];
	}

	pIndices[0] = newVariant;
	if (numVariants == 0)
	{
		pIndices[0].m_index = 0; // index is irrelevant anyway if there are zero variants -- should never happen
	}
	else
	{
		if (randomize && numVariants > 1)
		{
			U32F randomIndex = pIndices[0].m_index;

			do
			{
				randomIndex = urand() % numVariants;
			} while (randomIndex == pIndices[0].m_index);

			pIndices[0].m_index = randomIndex;
		}
		else
		{
			// just increment modulo the number of variants
			pIndices[0].m_index = (pIndices[0].m_index + 1) % numVariants;
		}
	}

	const U32F retVal = pIndices[0].m_index;

	RefreshHash();

	return retVal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlaySnapshot::UpdateVariantsFrom(const AnimOverlaySnapshot* pSourceSnapshot)
{
	if (!pSourceSnapshot)
		return;

	InternalUpdateVariantsFrom(pSourceSnapshot);
	RefreshHash(); // don't copy hash from pSourceSnapshot, it might have different overlays.
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlaySnapshot::InternalUpdateVariantsFrom(const AnimOverlaySnapshot* pSourceSnapshot)
{
	if (!pSourceSnapshot)
		return;

	const VariantIndex* pSourceIndices = pSourceSnapshot->m_pVariantIndexArray;
	VariantIndex* pDestIndices = m_pVariantIndexArray;

	memcpy(pDestIndices, pSourceIndices, kVariantArraySize);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlaySnapshot::ResetVariantIndexes()
{
	VariantIndex* pIndices = m_pVariantIndexArray;

	for (U32F i = 0; i < kNumVariantIndices; ++i)
	{
		pIndices[i].m_animNameId = INVALID_STRING_ID_64;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlaySnapshot::SetOverlaySet(U32F index, const DC::AnimOverlaySet* pOverlaySet)
{
	ANIM_ASSERT(index < m_numOverlayLayers);
	if (index >= m_numOverlayLayers)
		return;

	StringId64 newOverlaySetName = pOverlaySet ? pOverlaySet->m_name : INVALID_STRING_ID_64;
	if (m_overlaySetIds[index] == newOverlaySetName && m_ppOverlaySets[index] == pOverlaySet)
		return;

	m_overlaySetIds[index] = newOverlaySetName;
	m_ppOverlaySets[index] = pOverlaySet;

	RefreshHash();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlaySnapshot::ClearLayer(I32F layerIndex)
{
	ANIM_ASSERT(layerIndex < m_numOverlayLayers);
	if (layerIndex >= m_numOverlayLayers)
		return;

	if (m_overlaySetIds[layerIndex] == INVALID_STRING_ID_64 && 
		m_ppOverlaySets[layerIndex] == nullptr)
		return;

	m_overlaySetIds[layerIndex] = INVALID_STRING_ID_64;
	if (m_ppOverlaySets[layerIndex])
		m_ppOverlaySets[layerIndex] = nullptr;

	RefreshHash();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlaySnapshot::RefreshHash()
{
	U64 hashVal = SID_VAL("anim-overlay");

	//This will not detect a change caused by reloading the script modules.
	for (U32F i = 0; i < m_numOverlayLayers; ++i)
	{
		if (!m_ppOverlaySets[i])
			continue;

		hashVal += m_ppOverlaySets[i]->m_name.GetValue();
		hashVal += m_ppOverlaySets[i]->m_layerId.GetValue();
	}

	for (U32F i = 0; i < kNumVariantIndices; ++i)
	{
		hashVal += m_pVariantIndexArray[i].m_animNameId.GetValue() * m_pVariantIndexArray[i].m_index;
	}

	AnimOverlaySnapshotHash hash = AnimOverlaySnapshotHash(hashVal);

	if (hash != m_currentHash)
	{
		m_currentHash = hash;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimOverlayIterator AnimOverlaySnapshot::GetNextEntry(const AnimOverlayIterator& startingIterator) const
{
	PROFILE_ACCUM(AOS_GetNextEntry);

//  This profile nugget was requring a lot of time and messing with tuner
//	PROFILE(Animation, AOS_GetNextEntry);

	const U32F numOverlays = m_numOverlayLayers;
	const DC::AnimOverlaySet* const* ppOverlaySets = m_ppOverlaySets;

	const U32F startingLayer = startingIterator.m_layerIndex >= 0 ? startingIterator.m_layerIndex : 0;
	const U32F startingEntry = startingIterator.m_entryIndex >= 0 ? startingIterator.m_entryIndex + 1 : 0;

	U32F iLayer = startingLayer;
	U32F iEntry = startingEntry;

	for (; iLayer < m_numOverlayLayers; ++iLayer)
	{
		const DC::AnimOverlaySet* pOverlaySet = ppOverlaySets[iLayer];

		if (!pOverlaySet)
			continue;

		const U32F setArrayCount = pOverlaySet->m_animOverlaySetArrayCount;
		if (setArrayCount == 0)
			continue;

		const StringId64* pKeys = pOverlaySet->m_keys;

		for (; iEntry < setArrayCount; ++iEntry)
		{
			if (startingIterator.m_sourceId != pKeys[iEntry])
				continue;

			const DC::AnimOverlaySetEntry* pEntry = &pOverlaySet->m_animOverlaySetArray[iEntry];

			if (!pEntry)
				continue;

			const bool isState = (pEntry->m_flags & DC::kAnimOverlayFlagsIsState) != 0;
			if (startingIterator.m_matchState != isState)
				continue;

			AnimOverlayIterator newIterator = AnimOverlayIterator(startingIterator.m_sourceId,
																  startingIterator.m_matchState);
			newIterator.AdvanceTo(iLayer, iEntry, pEntry);

			// don't start over to check previous overlays.
			const bool isUnidirectional = (pEntry->m_flags & DC::kAnimOverlayFlagsUnidirectional) != 0;
			if (isUnidirectional)
				newIterator.SetUnidirectional(true);

			return newIterator;
		}
		
		iEntry = 0;
	}
	
	return AnimOverlayIterator(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOverlaySnapshot::DebugPrint(const AnimOverlayIterator& iter, char* pOutBuf, int numBytes) const
{
	int layerIndex = iter.GetLayerIndex();
	int entryIndex = iter.GetEntryIndex();

	const U32F numOverlays = m_numOverlayLayers;
	const DC::AnimOverlaySet* const* ppOverlaySets = m_ppOverlaySets;

	if (layerIndex >= 0 && layerIndex < numOverlays)
	{
		const DC::AnimOverlaySet* pOverlaySet = ppOverlaySets[layerIndex];
		if (pOverlaySet)
		{ 
			if (entryIndex >= 0 && entryIndex < pOverlaySet->m_animOverlaySetArrayCount)
			{
				const DC::AnimOverlaySetEntry* pEntry = &pOverlaySet->m_animOverlaySetArray[entryIndex];

				snprintf(pOutBuf, numBytes, "Layer Index: %d, Id: %s, Name: %s\n  [%s -> %s]\n",
					layerIndex,
					DevKitOnly_StringIdToString(pOverlaySet->m_layerId),
					DevKitOnly_StringIdToString(pOverlaySet->m_name),
					DevKitOnly_StringIdToString(iter.GetSourceId()),
					DevKitOnly_StringIdToString(pEntry->m_remapId));

				return true;
			}
		}
		else
		{
			snprintf(pOutBuf, numBytes, "Layer Index: %d : None\n", layerIndex);
			return true;
		}
	}

	return false;
}
