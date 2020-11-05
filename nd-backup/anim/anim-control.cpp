/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-control.h"

#include "corelib/math/mathutils.h"
#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-actor.h"
#include "ndlib/anim/anim-cmd-generator-layer.h"
#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-copy-remap-layer.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-dummy-instance.h"
#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-pose-layer.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-snapshot-layer.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/process/process-defines.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"
#include "ndlib/text/stringid-util.h"
#include "ndlib/util/type-factory.h"

#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/entity-spawner.h"

/// --------------------------------------------------------------------------------------------------------------- ///
STATIC_ASSERT(sizeof(AnimControl) % 16 == 0);

namespace DC
{
	TYPE_FACTORY_REGISTER_BASE(AnimActorInfo);
	TYPE_FACTORY_REGISTER_BASE(AnimInstanceInfo);
	TYPE_FACTORY_REGISTER_BASE(AnimTopInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimControl::AnimControl()
	: m_pAnimData(nullptr)
	, m_numLayers(0)
	, m_layerTypesContained(0)
	, m_animActorName(0)
	, m_pAnimActor(nullptr)
	, m_pInfoCollection(nullptr)
	, m_pAnimOverlays(nullptr)
	, m_pDummyInstance(nullptr)
	, m_layerCreationOptionBits(0u)
	, m_animStepJobFinished(false)
	, m_disableRandomization(false)
	, m_alwaysInitInstanceZero(false)
	, m_cameraCutsDisabled(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err AnimControl::Init(FgAnimData* pAnimData,
					  U32F maxTriggeredEffects,
					  U32F maxLayers,
					  Process* pOwner)
{
	m_pAnimData = pAnimData;
	m_hOwner = pOwner;

	if (const ArtItemSkeleton* pSkel = m_pAnimData->m_curSkelHandle.ToArtItem())
	{
		m_animTable.Init(pSkel->m_skelId, pSkel->m_hierarchyId);
	}

	// Make sure my bucket matches my top-level process tree.
	const Process* pProcess = m_pAnimData->m_hProcess.ToProcess();
	if (pProcess)
	{
		const ProcessBucket bucket = EngineComponents::GetProcessMgr()->GetProcessBucket(*pProcess);
		if (bucket != kProcessBucketUnknown)
		{
			EngineComponents::GetAnimMgr()->ChangeBucket(m_pAnimData, (U32F)bucket);
		}
	}

	m_triggeredEffects.Init(maxTriggeredEffects);
	m_maxLayers = (maxLayers + 3);
	m_layerEntries = NDI_NEW (kAlign16) LayerEntry[m_maxLayers];
	memset(m_layerEntries, 0, sizeof(LayerEntry) * m_maxLayers);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::Shutdown()
{
	for (U32F i = 0; i < m_maxLayers; ++i)
	{
		LayerEntry& entry = m_layerEntries[i];

		if (entry.m_pLayer)
		{
			entry.m_pLayer->Shutdown();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::EnableAsyncUpdate(U32 updateBitMask)
{
	ANIM_ASSERT(m_pAnimData);
	m_pAnimData->m_flags |= updateBitMask;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::DisableAsyncUpdate(U32 updateBitMask)
{
	ANIM_ASSERT(m_pAnimData);
	m_pAnimData->m_flags &= ~updateBitMask;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimControl::IsAsyncUpdateEnabled(U32 updateBitMask)
{
	ANIM_ASSERT(m_pAnimData);
	return (0 != (m_pAnimData->m_flags & updateBitMask));
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ndanim::JointHierarchy* AnimControl::GetSkeleton() const
{
	ANIM_ASSERT(m_pAnimData);
	return m_pAnimData->m_curSkelHandle.ToArtItem()->m_pAnimHierarchy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JointCache* AnimControl::GetJointCache()
{
	ANIM_ASSERT(m_pAnimData);
	return &m_pAnimData->m_jointCache;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const JointCache* AnimControl::GetJointCache() const
{
	ANIM_ASSERT(m_pAnimData);
	return &m_pAnimData->m_jointCache;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemSkeleton* AnimControl::GetArtItemSkel() const
{
	ANIM_ASSERT(m_pAnimData);
	return m_pAnimData->m_curSkelHandle.ToArtItem();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocateLayerHelper(AnimLayer* pLayer, AnimLayerType type, StringId64 nameId, U32 priority)
{
	ANIM_ASSERT(m_numLayers < m_maxLayers);
	ANIM_ASSERT(pLayer);
	ANIM_ASSERT(type != kAnimLayerTypeInvalid);
	LayerEntry& entry = m_layerEntries[m_numLayers++];
	entry.m_pLayer = pLayer;
	entry.m_nameId = nameId;
	entry.m_type = type;
	entry.m_priority = priority;
	entry.m_created = false;
	entry.m_fixedName = (nameId != INVALID_STRING_ID_64);

	m_layerTypesContained |= type;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocateSimpleLayer(StringId64 nameId, ndanim::BlendMode blendMode, U32 priority, U32F numInstancesInLayer)
{
	ANIM_ASSERT(m_numLayers < m_maxLayers);
	AnimSimpleLayer* pLayer = NDI_NEW (kAlign16) AnimSimpleLayer(&m_animTable, GetAnimOverlaySnapshot(), numInstancesInLayer);
	AllocateLayerHelper(pLayer, kAnimLayerTypeSimple, nameId, priority);
	pLayer->Setup(nameId, blendMode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocateSimpleLayer(U32F numInstancesInLayer)
{
	ANIM_ASSERT(m_numLayers < m_maxLayers);
	AnimSimpleLayer* pLayer = NDI_NEW (kAlign16) AnimSimpleLayer(&m_animTable, GetAnimOverlaySnapshot(), numInstancesInLayer);
	AllocateLayerHelper(pLayer, kAnimLayerTypeSimple, INVALID_STRING_ID_64, AnimStateLayerParams::kDefaultPriority);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocateStateLayer(StringId64 nameId, const AnimStateLayerParams& params)
{
	const U32 heapId = m_pAnimData ? (m_pAnimData->m_hProcess.GetProcessId() + nameId.GetValue()) : nameId.GetValue();
	AnimStateLayerParams nonConstParams = params;
	nonConstParams.m_snapshotHeapId = (params.m_snapshotHeapId == 0) ? heapId : params.m_snapshotHeapId;

	ANIM_ASSERT(m_numLayers < m_maxLayers);
	AnimStateLayer* pLayer = NDI_NEW AnimStateLayer(&m_animTable,
													m_pInfoCollection,
													GetAnimOverlaySnapshot(),
													m_pAnimData,
													nonConstParams);

	AllocateLayerHelper(pLayer, kAnimLayerTypeState, nameId, params.m_priority);

	if (nameId != INVALID_STRING_ID_64)
	{
		pLayer->Setup(nameId, params.m_blendMode, m_pAnimActor);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocatePoseLayer(StringId64 nameId, ndanim::BlendMode blendMode, U32 priority)
{
	ANIM_ASSERT(m_numLayers < m_maxLayers);
	AnimPoseLayer* pLayer = NDI_NEW (kAlign16) AnimPoseLayer(&m_animTable, GetAnimOverlaySnapshot());
	AllocateLayerHelper(pLayer, kAnimLayerTypePose, nameId, priority);

	pLayer->AnimLayer::Setup(nameId, blendMode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocatePoseLayer()
{
	ANIM_ASSERT(m_numLayers < m_maxLayers);
	AnimPoseLayer* pLayer = NDI_NEW (kAlign16) AnimPoseLayer(&m_animTable, GetAnimOverlaySnapshot());
	AllocateLayerHelper(pLayer, kAnimLayerTypePose, INVALID_STRING_ID_64, AnimStateLayerParams::kDefaultPriority);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocateSnapshotLayer(StringId64 nameId, ndanim::BlendMode blendMode, U32 priority)
{
	ANIM_ASSERT(m_numLayers < m_maxLayers);
	AnimSnapshotLayer* pLayer = NDI_NEW (kAlign16) AnimSnapshotLayer(&m_animTable, GetAnimOverlaySnapshot());
	AllocateLayerHelper(pLayer, kAnimLayerTypeSnapshot, nameId, priority);
	pLayer->AnimLayer::Setup(nameId, blendMode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocateCopyRemapLayer(StringId64 nameId, ndanim::BlendMode blendMode, U32 priority)
{
	ANIM_ASSERT(m_numLayers < m_maxLayers);
	AnimCopyRemapLayer* pLayer = NDI_NEW(kAlign16) AnimCopyRemapLayer(&m_animTable, GetAnimOverlaySnapshot());
	AllocateLayerHelper(pLayer, kAnimLayerTypeCopyRemap, nameId, priority);
	//pLayer->AnimLayer::Setup(nameId, blendMode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocateSnapshotLayer()
{
	ANIM_ASSERT(m_numLayers < m_maxLayers);
	AnimSnapshotLayer* pLayer = NDI_NEW (kAlign16) AnimSnapshotLayer(&m_animTable, GetAnimOverlaySnapshot());
	AllocateLayerHelper(pLayer, kAnimLayerTypeSnapshot, INVALID_STRING_ID_64, AnimStateLayerParams::kDefaultPriority);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocateCmdGeneratorLayer(StringId64 nameId, ndanim::BlendMode blendMode, U32 priority)
{
	ANIM_ASSERT(m_numLayers < m_maxLayers);
	AnimCmdGeneratorLayer* pLayer = NDI_NEW AnimCmdGeneratorLayer(&m_animTable, GetAnimOverlaySnapshot());
	AllocateLayerHelper(pLayer, kAnimLayerTypeCmdGenerator, nameId, priority);
	pLayer->AnimLayer::Setup(nameId, blendMode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocateCmdGeneratorLayer()
{
	ANIM_ASSERT(m_numLayers < m_maxLayers);
	AnimCmdGeneratorLayer* pLayer = NDI_NEW AnimCmdGeneratorLayer(&m_animTable, GetAnimOverlaySnapshot());
	AllocateLayerHelper(pLayer, kAnimLayerTypeCmdGenerator, INVALID_STRING_ID_64, AnimStateLayerParams::kDefaultPriority);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err AnimControl::SetupAnimActor(StringId64 animActorName)
{
	PROFILE(Processes, SetupAnimActor);

	if (animActorName == INVALID_STRING_ID_64)
		return Err::kOK;

	// setup AnimActor
	const DC::AnimActor* pActor = ScriptManager::Lookup<DC::AnimActor>(animActorName, nullptr);
	if (pActor == nullptr)
		return Err::kErrAnimActorSymbolNotFound;

	m_animActorName = animActorName;
	m_pAnimActor = pActor;

	//
	// NOTE: We're about to do some dangerous dynamically typed object construction and copying here.
	//		 We need to check *everything* beforehand, otherwise we might get some unknown results.
	//

	ANIM_ASSERT(m_pAnimActor->m_infoType.m_cType.m_symbol != INVALID_STRING_ID_64);
	DC::AnimActorInfo* pActorInfo = (DC::AnimActorInfo*)g_typeFactory.Create(m_pAnimActor->m_infoType.m_cType.m_symbol);
	ANIM_ASSERT(pActorInfo);
	if (!pActorInfo)
		return Err::kErrBadData;

	DC::AnimInstanceInfo* pInstanceInfo = (DC::AnimInstanceInfo*)g_typeFactory.Create(m_pAnimActor->m_instanceInfoType.m_cType.m_symbol);
	ANIM_ASSERT(pInstanceInfo);
	if (!pInstanceInfo)
		return Err::kErrBadData;

	DC::AnimTopInfo* pTopInfo = (DC::AnimTopInfo*)g_typeFactory.Create(m_pAnimActor->m_topInfoType.m_cType.m_symbol);
	ANIM_ASSERT(pTopInfo);
	if (!pTopInfo)
		return Err::kErrBadData;

	ANIM_ASSERT(m_pAnimActor->m_defaultInfo);
	if (!m_pAnimActor->m_defaultInfo)
		return Err::kErrBadData;

	ANIM_ASSERT(m_pAnimActor->m_defaultInstanceInfo);
	if (!m_pAnimActor->m_defaultInstanceInfo)
		return Err::kErrBadData;

	ANIM_ASSERT(m_pAnimActor->m_defaultTopInfo);
	if (!m_pAnimActor->m_defaultTopInfo)
		return Err::kErrBadData;

	ANIM_ASSERT(m_pAnimActor->m_defaultInfo->m_dcType == m_pAnimActor->m_infoType.m_dcType.m_symbol);
	if (m_pAnimActor->m_defaultInfo->m_dcType != m_pAnimActor->m_infoType.m_dcType.m_symbol)
		return Err::kErrBadData;

	ANIM_ASSERT(m_pAnimActor->m_defaultInstanceInfo->m_dcType == m_pAnimActor->m_instanceInfoType.m_dcType.m_symbol);
	if (m_pAnimActor->m_defaultInstanceInfo->m_dcType != m_pAnimActor->m_instanceInfoType.m_dcType.m_symbol)
		return Err::kErrBadData;

	ANIM_ASSERT(m_pAnimActor->m_defaultTopInfo->m_dcType == m_pAnimActor->m_topInfoType.m_dcType.m_symbol);
	if (m_pAnimActor->m_defaultTopInfo->m_dcType != m_pAnimActor->m_topInfoType.m_dcType.m_symbol)
		return Err::kErrBadData;

	// Store all the info structs in this collection struct
	m_pInfoCollection = NDI_NEW DC::AnimInfoCollection;
	ANIM_ASSERT(m_pInfoCollection);
	m_pInfoCollection->m_actor		= pActorInfo;
	m_pInfoCollection->m_instance	= pInstanceInfo;
	m_pInfoCollection->m_top		= pTopInfo;
	m_pInfoCollection->m_actorSize		= g_typeFactory.GetTypeSize(m_pAnimActor->m_infoType.m_cType.m_symbol);
	m_pInfoCollection->m_instanceSize	= g_typeFactory.GetTypeSize(m_pAnimActor->m_instanceInfoType.m_cType.m_symbol);
	m_pInfoCollection->m_topSize		= g_typeFactory.GetTypeSize(m_pAnimActor->m_topInfoType.m_cType.m_symbol);

	ANIM_ASSERT(IsSizeAligned(m_pInfoCollection->m_actorSize, kAlign16));
	ANIM_ASSERT(IsSizeAligned(m_pInfoCollection->m_instanceSize, kAlign16));
	ANIM_ASSERT(IsSizeAligned(m_pInfoCollection->m_topSize, kAlign16));

	// Clear
	memcpy(pActorInfo, m_pAnimActor->m_defaultInfo, m_pInfoCollection->m_actorSize);
	memcpy(pInstanceInfo, m_pAnimActor->m_defaultInstanceInfo, m_pInfoCollection->m_instanceSize);
	memcpy(pTopInfo, m_pAnimActor->m_defaultTopInfo, m_pInfoCollection->m_topSize);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err AnimControl::ConfigureStartState(StringId64 startStateOverride, bool useRandomStartPhase)
{
	PROFILE(Processes, AnimCtrl_ConfigureStartState);

	ANIM_ASSERT(m_pAnimActor);

	// Allocate additional layers - This should really be done outside of this class
	// Create hands & face layers
	const U32F basePriority = GetBaseLayerPriority();

	AnimSimpleLayer* pHandsLayer = CreateSimpleLayer(SID("hands"), ndanim::kBlendSlerp, basePriority - 9);
	AnimSimpleLayer* pFaceLayer = CreateSimpleLayer(SID("facial-base"), ndanim::kBlendSlerp, basePriority - 8);

	if (pHandsLayer)
		pHandsLayer->SetCurrentFade(0.0f);
	if (pFaceLayer)
		pFaceLayer->SetCurrentFade(0.0f);

	// Create the base layer
	AnimStateLayer* pBaseLayer = CreateStateLayer(SID("base"), ndanim::kBlendSlerp, basePriority);

	// Initialize all tracks to the start state
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_enableValidation))
	{
		const DC::AnimState* pTestState = nullptr;
		if (startStateOverride != INVALID_STRING_ID_64)
		{
			pTestState = AnimActorFindState(m_pAnimActor, startStateOverride);
		}
		if (!pTestState)
		{
			pTestState = AnimActorFindState(m_pAnimActor, m_pAnimActor->m_startState);
		}
		if (!pTestState)
		{
			MsgErr("Default state not found, erroring out as bad data!\n");
			return Err::kErrBadData;
		}
	}

	ANIM_ASSERT(pBaseLayer);
	const StringId64 startStateId = startStateOverride != INVALID_STRING_ID_64 ? startStateOverride : m_pAnimActor->m_startState;
	FadeToStateParams params;
	params.m_stateStartPhase = useRandomStartPhase ? frand(0.0f, 0.9f) : 0.0f;
	params.m_animFadeTime	 = 0.0f;
	params.m_motionFadeTime	 = 0.0f;
	pBaseLayer->FadeToState(startStateId, params);
	pBaseLayer->Fade(1.0f, 0.0f);

	if (DC::AnimActorInfo* pInfo = GetInfo())
		pInfo->m_randomNumber = frand(0.0f, 1.0f);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimTable& AnimControl::GetAnimTable()
{
	return m_animTable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimTable& AnimControl::GetAnimTable() const
{
	return m_animTable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CachedAnimLookupRaw AnimControl::LookupAnimCached(const CachedAnimLookupRaw& lookup) const
{
	// no overlays, no anim sets -- just raw animation lookups from the anim table

	if (!g_animOptions.m_validateCachedAnimLookups && lookup.CachedValueValid())
	{
		return lookup;
	}

	StringId64 sourceId = lookup.GetSourceId();
	CachedAnimLookupRaw res(sourceId);

	ArtItemAnimHandle anim = m_animTable.LookupAnim(sourceId);
	res.SetFinalResult(anim);

	ANIM_ASSERT(!g_animOptions.m_validateCachedAnimLookups || !lookup.CachedValueValid() || lookup.Equals(res));

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CachedAnimLookup AnimControl::LookupAnimCached(const CachedAnimLookup& lookup) const
{
	const AnimOverlays* pOverlays = GetAnimOverlays();

	if (!g_animOptions.m_validateCachedAnimLookups && lookup.CachedValueValid(pOverlays))
	{
		return lookup;
	}

	CachedAnimLookup res = CachedAnimLookup();
	res.SetSourceId(lookup.GetSourceId());

	if (pOverlays)
	{
		CachedAnimOverlayLookup overlayRes = pOverlays->LookupTransformedAnimId(lookup.GetOverlayLookup());
		res.SetOverlayResult(overlayRes);
	}

	const StringId64 overlayResId = res.GetFinalResolvedId();

	ArtItemAnimHandle anim = m_animTable.LookupAnim(overlayResId);

	res.SetFinalResult(overlayResId, anim);

	ANIM_ASSERT(!g_animOptions.m_validateCachedAnimLookups || !lookup.CachedValueValid(pOverlays) || lookup.Equals(res));

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimControl::LookupAnimId(StringId64 animNameId) const
{
	StringId64 translatedAnimId = animNameId;

	if (const AnimOverlays* pOverlays = GetAnimOverlays())
	{
		translatedAnimId = pOverlays->LookupTransformedAnimId(animNameId);
	}

	return translatedAnimId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimOverlaySnapshot::Result AnimControl::DevKitOnly_LookupAnimId(StringId64 animNameId) const
{
	const AnimOverlays* pOverlays = GetAnimOverlays();
	if (pOverlays)
		return pOverlays->DevKitOnly_LookupTransformedAnimId(animNameId);

	return AnimOverlaySnapshot::Result(animNameId, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AnimControl::DevKitOnly_LookupAnimName(StringId64 animNameId) const
{
	return DevKitOnly_StringIdToString(LookupAnimId(animNameId));
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle AnimControl::LookupAnim_NoTranslate(StringId64 animNameId) const
{
	return m_animTable.LookupAnim(animNameId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle AnimControl::LookupAnim(StringId64 animNameId) const
{
	StringId64 translatedAnimId = animNameId;
	const AnimOverlays* pOverlays = GetAnimOverlays();
	if (pOverlays)
	{
		translatedAnimId = pOverlays->LookupTransformedAnimId(animNameId);
	}
	return m_animTable.LookupAnim(translatedAnimId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle AnimControl::LookupAnimNoOverlays(StringId64 animNameId) const
{
	return m_animTable.LookupAnim(animNameId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle AnimControl::DevKitOnly_LookupAnimByIndex(int index) const
{
	return m_animTable.DevKitOnly_LookupAnimByIndex(index);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	if (m_pInfoCollection)
	{
		RelocatePointer(m_pInfoCollection->m_actor, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_pInfoCollection->m_instance, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_pInfoCollection->m_top, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_pInfoCollection, deltaPos, lowerBound, upperBound);
	}

	DeepRelocatePointer(m_pAnimOverlays, deltaPos, lowerBound, upperBound);

	for (U32F i = 0; i < m_maxLayers; ++i)
	{
		LayerEntry& entry = m_layerEntries[i];

		if (entry.m_pLayer)
		{
			entry.m_pLayer->Relocate(deltaPos, lowerBound, upperBound);
			RelocatePointer(entry.m_pLayer, deltaPos, lowerBound, upperBound);
		}
	}

	RelocatePointer(m_layerEntries, deltaPos, lowerBound, upperBound);

	m_triggeredEffects.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimLayer* AnimControl::CreateLayerHelper(AnimLayerType type, StringId64 layerName, U32F priority)
{
	AnimLayer* pNewLayer = nullptr;
	if (layerName == INVALID_STRING_ID_64)
		return pNewLayer;

	for (U32F i = 0; i < m_numLayers; ++i)
	{
		LayerEntry& entry = m_layerEntries[i];
		if (!entry.m_created && entry.m_pLayer && entry.m_pLayer->GetType() == type && entry.m_nameId == layerName)
		{
			entry.m_created = true;
			entry.m_priority = priority;
			pNewLayer = entry.m_pLayer;
			break;
		}
	}
	if (pNewLayer == nullptr)
	{
		for (U32F i = 0; i < m_numLayers; ++i)
		{
			LayerEntry& entry = m_layerEntries[i];
			if (!entry.m_created && entry.m_pLayer && entry.m_pLayer->GetType() == type && entry.m_nameId == INVALID_STRING_ID_64)
			{
				entry.m_nameId = layerName;
				entry.m_created = true;
				entry.m_priority = priority;
				pNewLayer = entry.m_pLayer;
				break;
			}
		}
	}
	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimLayer* AnimControl::CreateLayerHelper(StringId64 layerName)
{
	for (U32F i = 0; i < m_numLayers; ++i)
	{
		LayerEntry& entry = m_layerEntries[i];
		if (!entry.m_created && entry.m_pLayer && entry.m_nameId == layerName)
		{
			entry.m_created = true;
			return entry.m_pLayer;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSimpleLayer* AnimControl::CreateSimpleLayer(StringId64 layerName)
{
	AnimSimpleLayer* pNewLayer = GetSimpleLayerById(layerName);
	if (!pNewLayer)
	{
		pNewLayer = static_cast<AnimSimpleLayer*>(CreateLayerHelper(layerName));
	}

	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSimpleLayer* AnimControl::CreateSimpleLayer(StringId64 layerName, ndanim::BlendMode blendMode, U32F priority)
{
	AnimSimpleLayer* pNewLayer = GetSimpleLayerById(layerName);
	ANIM_ASSERT(pNewLayer == nullptr
				|| (pNewLayer->GetBlendMode() == blendMode && pNewLayer->GetType() == kAnimLayerTypeSimple));

	bool prioritySet = false;
	if (!pNewLayer)
	{
		pNewLayer = static_cast<AnimSimpleLayer*>(CreateLayerHelper(kAnimLayerTypeSimple, layerName, priority));
		prioritySet = true;
	}

	// now setup the new layer
	if (pNewLayer)
	{
		pNewLayer->Setup(layerName, blendMode);

		if (!prioritySet)
			SetLayerPriority(layerName, priority);
	}

	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateLayer* AnimControl::CreateStateLayer(StringId64 layerName, StringId64 startStateId)
{
	AnimStateLayer* pStateLayer = CreateStateLayer(layerName);
	const DC::AnimState* pStateEa = AnimActorFindState(m_pAnimActor, startStateId);

	if (pStateEa && pStateLayer)
	{
		FadeToStateParams defaultParams;
		pStateLayer->FadeToStateImmediate(pStateEa, defaultParams);
	}

	if (!pStateLayer)
		MsgAnim("Warning could not create state layer '%s'\n", DevKitOnly_StringIdToString(layerName));

	return pStateLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateLayer* AnimControl::CreateStateLayer(StringId64 layerName,
											  StringId64 startStateId,
											  ndanim::BlendMode blendMode,
											  U32F priority)
{
	AnimStateLayer* pStateLayer = CreateStateLayer(layerName, blendMode, priority);
	const DC::AnimState* pStateEa = AnimActorFindState(m_pAnimActor, startStateId);

	if (pStateEa && pStateLayer)
	{
		FadeToStateParams defaultParams;
		pStateLayer->FadeToStateImmediate(pStateEa, defaultParams);
	}

	if (!pStateLayer)
		MsgAnim("Warning could not create state layer '%s'\n", DevKitOnly_StringIdToString(layerName));

	return pStateLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateLayer* AnimControl::CreateStateLayer(StringId64 layerName)
{
	AnimStateLayer* pNewLayer = GetStateLayerById(layerName);
	if (!pNewLayer)
	{
		pNewLayer = static_cast<AnimStateLayer*>(CreateLayerHelper(layerName));
	}

	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateLayer* AnimControl::CreateStateLayer(StringId64 layerName, ndanim::BlendMode blendMode, U32F priority)
{
	AnimStateLayer* pNewLayer = GetStateLayerById(layerName);
	ANIM_ASSERT(pNewLayer == nullptr
				|| (pNewLayer->GetBlendMode() == blendMode && pNewLayer->GetType() == kAnimLayerTypeState));

	bool prioritySet = false;
	if (!pNewLayer)
	{
		pNewLayer = static_cast<AnimStateLayer*>(CreateLayerHelper(kAnimLayerTypeState, layerName, priority));
		prioritySet = true;
	}

	// now setup the new layer
	if (pNewLayer)
	{
		ANIM_ASSERT(m_pAnimActor);
		pNewLayer->Setup(layerName, blendMode, m_pAnimActor);

		if (!prioritySet)
			SetLayerPriority(layerName, priority);
	}

	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimPoseLayer* AnimControl::CreatePoseLayer(StringId64 layerName, const ndanim::PoseNode* pPoseNode)
{
	AnimPoseLayer* pNewLayer = GetPoseLayerById(layerName);
	if (!pNewLayer)
	{
		pNewLayer = static_cast<AnimPoseLayer*>(CreateLayerHelper(layerName));
	}

	// now setup the new layer
	if (pNewLayer)
	{
		pNewLayer->Setup(layerName, pNewLayer->GetBlendMode(), pPoseNode);
	}

	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimPoseLayer* AnimControl::CreatePoseLayer(StringId64 layerName,
											ndanim::BlendMode blendMode,
											U32F priority,
											const ndanim::PoseNode* pPoseNode)
{
	AnimPoseLayer* pNewLayer = GetPoseLayerById(layerName);
	ANIM_ASSERT(pNewLayer == nullptr
				|| (pNewLayer->GetBlendMode() == blendMode && pNewLayer->GetType() == kAnimLayerTypePose));
	if (!pNewLayer)
	{
		pNewLayer = static_cast<AnimPoseLayer*>(CreateLayerHelper(kAnimLayerTypePose, layerName, priority));
	}

	// now setup the new layer
	if (pNewLayer)
	{
		pNewLayer->Setup(layerName, blendMode, pPoseNode);
	}

	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimPoseLayer* AnimControl::CreatePoseLayer(StringId64 layerName, const DualSnapshotNode* pSnapshotNode)
{
	AnimPoseLayer* pNewLayer = GetPoseLayerById(layerName);
	if (!pNewLayer)
	{
		pNewLayer = static_cast<AnimPoseLayer*>(CreateLayerHelper(layerName));
	}

	// now setup the new layer
	if (pNewLayer)
	{
		pNewLayer->Setup(layerName, pNewLayer->GetBlendMode(), pSnapshotNode);
	}

	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotLayer* AnimControl::CreateSnapshotLayer(StringId64 layerName, DualSnapshotNode* pSnapshotNode)
{
	AnimSnapshotLayer* pNewLayer = GetSnapshotLayerById(layerName);
	if (!pNewLayer)
	{
		pNewLayer = static_cast<AnimSnapshotLayer*>(CreateLayerHelper(layerName));
	}

	// now setup the new layer
	if (pNewLayer)
	{
		pNewLayer->Setup(layerName, pNewLayer->GetBlendMode(), pSnapshotNode);
	}

	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimCopyRemapLayer* AnimControl::CreateCopyRemapLayer(StringId64 layerName,
													  const NdGameObject* pOwner,
													  const NdGameObject* pTarget,
													  StringId64 targetLayer,
													  StringId64 remapList)
{
	AnimCopyRemapLayer* pNewLayer = (AnimCopyRemapLayer*)GetLayerById(layerName);
	if (!pNewLayer)
	{
		pNewLayer = static_cast<AnimCopyRemapLayer*>(CreateLayerHelper(layerName));
	}

	// now setup the new layer
	if (pNewLayer)
	{
		pNewLayer->Setup(layerName, ndanim::kBlendSlerp, pOwner, pTarget, targetLayer, remapList);
	}

	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimCmdGeneratorLayer* AnimControl::CreateCmdGeneratorLayer(StringId64 layerName, IAnimCmdGenerator* pAnimCmdGenerator)
{
	AnimCmdGeneratorLayer* pNewLayer = GetCmdGeneratorLayerById(layerName);
	if (!pNewLayer)
	{
		pNewLayer = static_cast<AnimCmdGeneratorLayer*>(CreateLayerHelper(layerName));
	}

	// now setup the new layer
	if (pNewLayer)
	{
		pNewLayer->Setup(layerName, pNewLayer->GetBlendMode(), pAnimCmdGenerator);
	}

	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimCmdGeneratorLayer* AnimControl::CreateCmdGeneratorLayer(StringId64 layerName,
															ndanim::BlendMode blendMode,
															U32F priority,
															IAnimCmdGenerator* pAnimCmdGenerator)
{
	AnimCmdGeneratorLayer* pNewLayer = GetCmdGeneratorLayerById(layerName);
	ANIM_ASSERT(pNewLayer == nullptr
				|| (pNewLayer->GetBlendMode() == blendMode && pNewLayer->GetType() == kAnimLayerTypeCmdGenerator));

	bool prioritySet = false;
	if (!pNewLayer)
	{
		pNewLayer = static_cast<AnimCmdGeneratorLayer*>(CreateLayerHelper(kAnimLayerTypeCmdGenerator, layerName, priority));
		prioritySet = true;
	}

	// now setup the new layer
	if (pNewLayer)
	{
		pNewLayer->Setup(layerName, blendMode, pAnimCmdGenerator);

		if (!prioritySet)
			SetLayerPriority(layerName, priority);
	}

	return pNewLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimDummyInstance* AnimControl::CreateDummyInstance(const StringId64 animId,
													F32 num30HzFrameIntervals,
													F32 startPhase,
													F32 playbackRate,
													bool looping,
													ndanim::SharedTimeIndex* pSharedTime)
{
	m_pDummyInstance = AnimDummyInstance::PoolFree(m_pDummyInstance); // free the old one if any

	m_pDummyInstance = AnimDummyInstance::PoolAlloc();
	if (m_pDummyInstance)
	{
		m_pDummyInstance->Init(INVALID_ANIM_INSTANCE_ID,
							   animId,
							   num30HzFrameIntervals,
							   startPhase,
							   playbackRate,
							   looping,
							   pSharedTime);
	}

	return m_pDummyInstance;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimDummyInstance* AnimControl::GetDummyInstance() const
{
	return m_pDummyInstance;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::FadeOutDummyInstanceAndDestroy(float fadeOutSec)
{
	if (m_pDummyInstance)
	{
		m_pDummyInstance->FadeOut(fadeOutSec);

		if (m_pDummyInstance->HasFadedOut())
			DestroyDummyInstance();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::DestroyDummyInstance()
{
	m_pDummyInstance = AnimDummyInstance::PoolFree(m_pDummyInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::AnimInfoCollection* AnimControl::GetInfoCollection() const
{
	return m_pInfoCollection;
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::AnimActorInfo* AnimControl::GetInfo() const
{
	if (!m_pInfoCollection)
		return nullptr;

	return const_cast<DC::AnimActorInfo*>(m_pInfoCollection->m_actor);
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::AnimInstanceInfo* AnimControl::GetInstanceInfo() const
{
	if (!m_pInfoCollection)
		return nullptr;

	return const_cast<DC::AnimInstanceInfo*>(m_pInfoCollection->m_instance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::AnimTopInfo* AnimControl::GetTopInfo() const
{
	if (!m_pInfoCollection)
		return nullptr;

	return const_cast<DC::AnimTopInfo*>(m_pInfoCollection->m_top);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimActor* AnimControl::GetActor() const
{
	return m_pAnimActor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimControl::LayerEntry* AnimControl::FindLayerEntryById(StringId64 name)
{
	for (U32F i = 0; i < m_numLayers; ++i)
	{
		LayerEntry& entry = m_layerEntries[i];
		if (entry.m_created && entry.m_nameId == name)
			return &entry;
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimLayer* AnimControl::FindLayerById(StringId64 name) const
{
	if (name != INVALID_STRING_ID_64)
	{
		for (U32F i = 0; i < m_numLayers; ++i)
		{
			const LayerEntry& entry = m_layerEntries[i];

			if (entry.m_created && entry.m_nameId == name)
				return entry.m_pLayer;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::DestroyLayerById(StringId64 name)
{
	if (LayerEntry* pEntry = FindLayerEntryById(name))
	{
		if (pEntry->m_pLayer)
		{
			pEntry->m_pLayer->OnFree();
		}

		pEntry->m_created = false;
		if (!pEntry->m_fixedName)
			pEntry->m_nameId = INVALID_STRING_ID_64;
		pEntry->m_priority = AnimStateLayerParams::kDefaultPriority;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimControl::GetLayerNameByIndex(int index) const
{
	ANIM_ASSERT(index >= 0 && index < m_numLayers);
	return m_layerEntries[index].m_nameId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimLayer* AnimControl::GetLayerByIndex(int index)
{
	ANIM_ASSERT(index >= 0 && index < m_numLayers);

	LayerEntry& entry = m_layerEntries[index];
	if (entry.m_created)
		return entry.m_pLayer;

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSimpleLayer* AnimControl::FindSimpleLayerByAnim(StringId64 anim)
{
	for (int i=0;i<m_numLayers;i++)
	{
		LayerEntry& entry = m_layerEntries[i];
		if (entry.m_created && entry.m_type == kAnimLayerTypeSimple)
		{
			AnimSimpleLayer* pLayer = (AnimSimpleLayer*)entry.m_pLayer;
			if (pLayer->GetNumInstances())
			{
				if (pLayer->GetInstance(0)->GetAnimId() == anim)
				{
					return pLayer;
				}
			}
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimSimpleLayer* AnimControl::FindSimpleLayerByAnim(StringId64 anim) const
{
	return const_cast<AnimControl*>(this)->FindSimpleLayerByAnim(anim);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimLayer* AnimControl::GetLayerById(StringId64 name)
{
	return const_cast<AnimLayer*>(FindLayerById(name));
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSimpleLayer* AnimControl::GetSimpleLayerById(StringId64 name)
{
	AnimLayer* pAnimLayer = const_cast<AnimLayer*>(FindLayerById(name));
	if (pAnimLayer != nullptr && pAnimLayer->GetType() == kAnimLayerTypeSimple)
		return static_cast<AnimSimpleLayer*>(pAnimLayer);
	else
		return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSimpleLayer* AnimControl::GetSimpleLayerByIndex(I32 index)
{
	AnimLayer* pAnimLayer = const_cast<AnimLayer*>(GetLayerByIndex(index));
	if (pAnimLayer != nullptr && pAnimLayer->GetType() == kAnimLayerTypeSimple)
		return static_cast<AnimSimpleLayer*>(pAnimLayer);
	else
		return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateLayer* AnimControl::GetStateLayerById(StringId64 name)
{
	AnimLayer* pAnimLayer = const_cast<AnimLayer*>(FindLayerById(name));
	if (pAnimLayer != nullptr && pAnimLayer->GetType() == kAnimLayerTypeState)
		return static_cast<AnimStateLayer*>(pAnimLayer);
	else
		return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimPoseLayer* AnimControl::GetPoseLayerById(StringId64 name)
{
	AnimLayer* pAnimLayer = const_cast<AnimLayer*>(FindLayerById(name));
	if (pAnimLayer != nullptr && pAnimLayer->GetType() == kAnimLayerTypePose)
		return static_cast<AnimPoseLayer*>(pAnimLayer);
	else
		return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotLayer* AnimControl::GetSnapshotLayerById(StringId64 name)
{
	AnimLayer* pAnimLayer = const_cast<AnimLayer*>(FindLayerById(name));
	if (pAnimLayer != nullptr && pAnimLayer->GetType() == kAnimLayerTypeSnapshot)
		return static_cast<AnimSnapshotLayer*>(pAnimLayer);
	else
		return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimCmdGeneratorLayer* AnimControl::GetCmdGeneratorLayerById(StringId64 name)
{
	AnimLayer* pAnimLayer = const_cast<AnimLayer*>(FindLayerById(name));
	if (pAnimLayer != nullptr && pAnimLayer->GetType() == kAnimLayerTypeCmdGenerator)
		return static_cast<AnimCmdGeneratorLayer*>(pAnimLayer);
	else
		return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimLayer* AnimControl::GetLayerByIndex(int index) const
{
	ANIM_ASSERT(index >= 0 && index < m_numLayers);

	const LayerEntry& entry = m_layerEntries[index];
	if (entry.m_created)
		return entry.m_pLayer;

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimLayer* AnimControl::GetLayerById(StringId64 name) const
{
	return FindLayerById(name);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimSimpleLayer* AnimControl::GetSimpleLayerById(StringId64 name) const
{
	return const_cast<AnimControl*>(this)->GetSimpleLayerById(name);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateLayer* AnimControl::GetStateLayerById(StringId64 name) const
{
	return const_cast<AnimControl*>(this)->GetStateLayerById(name);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateLayer* AnimControl::GetStateLayerByIndex(I32 index) const
{
	return const_cast<AnimControl*>(this)->GetStateLayerByIndex(index);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateLayer* AnimControl::GetStateLayerByIndex(I32 index)
{
	AnimLayer* pAnimLayer = const_cast<AnimLayer*>(GetLayerByIndex(index));
	if (pAnimLayer != nullptr && pAnimLayer->GetType() == kAnimLayerTypeState)
		return static_cast<AnimStateLayer*>(pAnimLayer);
	else
		return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimPoseLayer* AnimControl::GetPoseLayerById(StringId64 name) const
{
	return const_cast<AnimControl*>(this)->GetPoseLayerById(name);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimSnapshotLayer* AnimControl::GetSnapshotLayerById(StringId64 name) const
{
	return const_cast<AnimControl*>(this)->GetSnapshotLayerById(name);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimCmdGeneratorLayer* AnimControl::GetCmdGeneratorLayerById(StringId64 name) const
{
	return const_cast<AnimControl*>(this)->GetCmdGeneratorLayerById(name);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateLayer* AnimControl::GetBaseStateLayer()
{
	const AnimLayer* pAnimLayer = FindLayerById(SID("base"));

	if (pAnimLayer && (pAnimLayer->GetType() == kAnimLayerTypeState))
	{
		return const_cast<AnimStateLayer*>(static_cast<const AnimStateLayer*>(pAnimLayer));
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateLayer* AnimControl::GetBaseStateLayer() const
{
	const AnimLayer* pAnimLayer = FindLayerById(SID("base"));

	if (pAnimLayer && (pAnimLayer->GetType() == kAnimLayerTypeState))
	{
		return static_cast<const AnimStateLayer*>(pAnimLayer);
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::BeginStep(float deltaTime)
{
	PROFILE_STRINGID(Animation, AnimControl_BeginStep, m_hOwner.ToProcess()->GetUserId());

	BeginStepInternal(deltaTime);
	FinishAnimStepJob();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::FinishStep(float deltaTime)
{
	PROFILE(Animation, AnimControl_FinishStep);

	// Now gather all the triggered effects from all the layers
	const U64 numLayers = m_numLayers;
	for (U64 i = 0; i < numLayers; ++i)
	{
		// If the layer has a state and a non-zero blend we update it.
		LayerEntry& entry = m_layerEntries[i];
		AnimLayer* pActiveLayer = entry.m_pLayer;
		ANIM_ASSERT(pActiveLayer);
		if (entry.m_created)
		{
			Memory::PrefetchForLoad(pActiveLayer);
			Memory::PrefetchForLoad(pActiveLayer, 0x80);

			pActiveLayer->FinishStep(deltaTime, pActiveLayer->TriggeredEffectsEnabled() ? &m_triggeredEffects : nullptr);

			// Free the layer if it wanted to be freed when faded out.
			if (pActiveLayer->m_freeWhenFadedOut && pActiveLayer->GetCurrentFade() == 0.0f)
			{
				pActiveLayer->OnFree();

				entry.m_created = false;
				if (!entry.m_fixedName)
					entry.m_nameId = INVALID_STRING_ID_64;
				entry.m_priority = AnimStateLayerParams::kDefaultPriority;
			}
		}
	}

	if (DC::AnimActorInfo* pInfo = GetInfo())
	{
		pInfo->m_randomNumber = frand(0.0f, 1.0f);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ShouldShowLayer(StringId64 layerName)
{
	if (g_animOptions.m_debugPrint.m_showLayersFilter.IsSelected(layerName))
	{
		return true;
	}

	if (g_animOptions.m_debugPrint.m_showLayersFilter.Valid())
	{
		return false;
	}

	if (g_animOptions.m_debugPrint.m_showOnlyBaseAndGestureLayers)
	{
		return layerName == SID("base") || layerName == SID("gesture-1") || layerName == SID("gesture-2");
	}

	if (g_animOptions.m_debugPrint.m_showOnlyGestureLayers)
	{
		return layerName == SID("gesture-1") || layerName == SID("gesture-2");
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::DebugPrint(const Locator& currentAlign, StringId64 aLayersToSuppress[], MsgOutput output) const
{
	const Process* pProcess = m_hOwner.ToProcess();

	SetColor(output, 0xFF000000 | 0x00FFFFFF);
	if (m_pAnimActor)
	{
		if (!g_animOptions.m_debugPrint.m_simplified)
		{
			PrintTo(output,
					"[%s] %s, "
					"Move Angle: %3.2f (%1.4f), "
					"Facing Diff: %3.2f, "
					"Dist to goal: %3.1f\n",
					pProcess ? pProcess->GetName() : "<null>",
					m_pAnimActor->m_name.m_string.GetString(),
					m_pInfoCollection->m_top ? m_pInfoCollection->m_top->m_moveAngle : 0.0f,
					m_pInfoCollection->m_top ? m_pInfoCollection->m_top->m_moveAngleChange : 0.0f,
					m_pInfoCollection->m_top ? m_pInfoCollection->m_top->m_facingDiff : 0.0f,
					m_pInfoCollection->m_actor ? m_pInfoCollection->m_actor->m_distanceToGoal : 0.0f);

			const ArtItemSkeleton* pCurSkel	 = m_pAnimData->m_curSkelHandle.ToArtItem();
			const ArtItemSkeleton* pAnimSkel = m_pAnimData->m_animateSkelHandle.ToArtItem();

			if (pCurSkel->m_hierarchyId != pAnimSkel->m_hierarchyId)
			{
				PrintTo(output,
						"[Post Retarget: Anim: '%s' Final: '%s']\n",
						ResourceTable::LookupSkelName(pAnimSkel->m_skelId),
						ResourceTable::LookupSkelName(pCurSkel->m_skelId));
			}
		}
		else
		{
			PrintTo(output, "%s\n", m_hOwner.ToProcess() ? m_hOwner.ToProcess()->GetName() : "<unknown>");
		}
	}
	else
	{
		if (pProcess)
		{
			PrintTo(output, "%s\n", pProcess->IsType(SID("NdLightObject")) ? pProcess->GetSpawner()->Name() : pProcess->GetName());
		}
		else
		{
			PrintTo(output, "AnimActor: (simple)\n");
		}
	}

	if (m_pDummyInstance)
	{
		SetColor(output, 0xFF000000 | 0x00FFFFFF);
		PrintTo(output, "-----------------------------------------------------------------------------------------\n");

		SetColor(output, 0xFF000000 | 0x0055FF55);
		PrintTo(output, "AnimDummyInstance\n");

		m_pDummyInstance->DebugPrint(output);
	}

	for (U32F i = 0; i < m_numLayers; ++i)
	{
		const LayerEntry& entry = m_layerEntries[i];
		if (entry.m_created)
		{
			if (!IsStringIdInList(entry.m_nameId, aLayersToSuppress))
			{
				const AnimLayer* pLayer = entry.m_pLayer;

				if (ShouldShowLayer(entry.m_nameId))
				{
					AnimLayerType type = pLayer->GetType();
					if (type == kAnimLayerTypeSimple)
					{
						static_cast<const AnimSimpleLayer*>(pLayer)->DebugPrint(output, entry.m_priority);
					}
					else if (type == kAnimLayerTypeState)
					{
						static_cast<const AnimStateLayer*>(pLayer)->DebugPrint(output, entry.m_priority, m_pInfoCollection->m_actor);
					}
					else if (type == kAnimLayerTypePose)
					{
						static_cast<const AnimPoseLayer*>(pLayer)->DebugPrint(output, entry.m_priority);
					}
					else if (type == kAnimLayerTypeCmdGenerator)
					{
						static_cast<const AnimCmdGeneratorLayer*>(pLayer)->DebugPrint(output, entry.m_priority);
					}
					else if (type == kAnimLayerTypeSnapshot)
					{
						static_cast<const AnimSnapshotLayer*>(pLayer)->DebugPrint(output, entry.m_priority);
					}
					else if (type == kAnimLayerTypeCopyRemap)
					{
						static_cast<const AnimCopyRemapLayer*>(pLayer)->DebugPrint(output, entry.m_priority, m_pAnimData);
					}
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimControl::GetNumFadesInProgress() const
{
	U32F numFadesInProgress = 0;

	for (U32F i = 0; i < m_numLayers; ++i)
	{
		const LayerEntry& entry = m_layerEntries[i];
		const AnimLayer* pLayer = entry.m_pLayer;
		if (entry.m_created && pLayer->IsValid())
		{
			numFadesInProgress += pLayer->GetNumFadesInProgress();
		}
	}

	return numFadesInProgress;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::CreateAnimCmds(const AnimCmdGenContext& context, AnimCmdList* pAnimCmdList) const
{
	PROFILE_ACCUM(AnimControl_CreateAnimCmds);

	const ndanim::JointHierarchy* pAnimHierarchy = context.m_pAnimateSkel->m_pAnimHierarchy;

	// Cowboy... (or Travis)... please, move this to the execution of the AnimExecContext
	if (m_pAnimData->m_animLod >= DC::kAnimLodAsleep /*|| m_pAnimData->m_animLod > DC::kAnimLodNormal*/)
	{
		pAnimCmdList->AddCmd_BeginProcessingGroup();
		pAnimCmdList->AddCmd_EvaluateEmptyPose(0);
		pAnimCmdList->AddCmd(AnimCmd::kEndProcessingGroup);

		return;
	}


 	{
		pAnimCmdList->AddCmd_BeginProcessingGroup();

		AnimCmdGenLayerContext layerContext(context);

		if (m_alwaysInitInstanceZero)
		{
			pAnimCmdList->AddCmd_EvaluateEmptyPose(0);
			layerContext.m_instanceZeroIsValid = true;
		}
		else
		{
			layerContext.m_instanceZeroIsValid = false;
		}

		int prevValidLayerIndex = -1;
		for (I32F layerIndex = 0; layerIndex < m_numLayers; ++layerIndex)
		{
			PROFILE(Animation, AnimControl_CreateAnimCmds_Layer);
			const LayerEntry& entry = m_layerEntries[layerIndex];
			AnimLayer* pLayer = entry.m_pLayer;

			if (g_animOptions.m_generatingNetCommands)
			{
				pAnimCmdList->AddCmd_Layer(pLayer->GetName());
			}

			// If the layer has a state and a non-zero blend we update it.
			if (entry.m_created && pLayer->IsValid())
			{
				ndanim::BlendMode blendMode = static_cast<ndanim::BlendMode>(pLayer->GetBlendMode());
				const bool isAdditiveLayer = blendMode == ndanim::kBlendAdditive;

				if (FALSE_IN_FINAL_BUILD(isAdditiveLayer && g_animOptions.m_disableAdditiveLayers))
					continue;

				if (FALSE_IN_FINAL_BUILD(pLayer->GetName() != SID("base") && g_animOptions.m_disableNonBaseLayers))
					continue;

				if (FALSE_IN_FINAL_BUILD(g_animOptions.m_disableLayerIndex >= 0 && layerIndex == g_animOptions.m_disableLayerIndex))
				{
					MsgCon("%s: layer %s disabled\n",
							m_pAnimData->m_hProcess.ToProcess() ? m_pAnimData->m_hProcess.ToProcess()->GetName() : "???",
							DevKitOnly_StringIdToString(pLayer->GetName()));
					continue;
				}


				if (prevValidLayerIndex >= 0 && entry.m_priority < m_layerEntries[prevValidLayerIndex].m_priority)
				{
					// we've added this layer after the sort, and it's not in the right place, don't animate using it
					continue;
				}

				prevValidLayerIndex = layerIndex;

				// Create the batches for each state tree to run on the SPUs
				const AnimLayerType type = pLayer->GetType();
				if (type == kAnimLayerTypeSimple)
				{
					static_cast<AnimSimpleLayer*>(pLayer)->CreateAnimCmds(layerContext, pAnimCmdList);
				}
				else if (type == kAnimLayerTypeState)
				{
					static_cast<AnimStateLayer*>(pLayer)->CreateAnimCmds(layerContext,
																		 pAnimCmdList,
																		 m_pInfoCollection->m_actor);
				}
				else if (type == kAnimLayerTypePose)
				{
					static_cast<AnimPoseLayer*>(pLayer)->CreateAnimCmds(layerContext, pAnimCmdList);
				}
				else if (type == kAnimLayerTypeCmdGenerator)
				{
					static_cast<AnimCmdGeneratorLayer*>(pLayer)->CreateAnimCmds(layerContext, pAnimCmdList);
				}
				else if (type == kAnimLayerTypeSnapshot)
				{
					static_cast<AnimSnapshotLayer*>(pLayer)->CreateAnimCmds(layerContext, pAnimCmdList);
				}
				else if (type == kAnimLayerTypeCopyRemap)
				{
					static_cast<AnimCopyRemapLayer*>(pLayer)->CreateAnimCmds(layerContext, pAnimCmdList);
				}
				else
				{
					ANIM_ASSERT(false);
				}

				layerContext.m_instanceZeroIsValid = true;
			}
		}

		// This will fill in bind pose joints for the entire hierarchy if we have no layers active or they are not valid.
		if (!layerContext.m_instanceZeroIsValid)
		{
			pAnimCmdList->AddCmd_EvaluateBindPose(0);
		}

		pAnimCmdList->AddCmd(AnimCmd::kEndProcessingGroup);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EffectList* AnimControl::GetTriggeredEffects() const
{
	return &m_triggeredEffects;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::SetLayerPriority(StringId64 layerName, U32F priority)
{
	if (LayerEntry* pEntry = FindLayerEntryById(layerName))
	{
		pEntry->m_priority = priority;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimControl::GetLayerPriority(StringId64 layerName)
{
	if (LayerEntry* pEntry = FindLayerEntryById(layerName))
	{
		return pEntry->m_priority;
	}

	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimControl::FadeLayer(StringId64 layerName, float desiredFade, float fadeTime, DC::AnimCurveType type)
{
	AnimLayer* pLayer = GetLayerById(layerName);
	if (pLayer)
	{
		pLayer->Fade(desiredFade, fadeTime, type);
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimControl::FadeOutLayerAndDestroy(StringId64 layerName, float blendTime, DC::AnimCurveType type)
{
	AnimLayer* pLayer = GetLayerById(layerName);
	if (pLayer)
	{
		pLayer->FadeOutAndDestroy(blendTime, type);
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::NotifyAnimTableUpdated()
{
	for (U32F i = 0; i < m_numLayers; ++i)
	{
		LayerEntry& entry = m_layerEntries[i];
		if (entry.m_created)
		{
			AnimLayer* pLayer = entry.m_pLayer;
			AnimLayerType type = pLayer->GetType();
			if (type == kAnimLayerTypeSimple)
			{
				AnimSimpleLayer* pSimpleLayer = static_cast<AnimSimpleLayer*>(pLayer);
				pSimpleLayer->ForceRefreshAnimPointers();
			}
			else if (type == kAnimLayerTypeState)
			{
				AnimStateLayer* pStateLayer = static_cast<AnimStateLayer*>(pLayer);
				pStateLayer->RefreshAnimPointers();
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// The AnimActor has changed! Reload all DC pointers and refresh all structs
void AnimControl::ReloadScriptData()
{
	PROFILE(Animation, ReloadScriptData);

	if (m_animActorName != INVALID_STRING_ID_64)
	{
		const DC::AnimActor* pAnimActor = ScriptManager::Lookup<DC::AnimActor>(m_animActorName, nullptr);
		if (pAnimActor)
		{
			for (U32F i = 0; i < m_numLayers; ++i)
			{
				LayerEntry& entry = m_layerEntries[i];
				if (entry.m_created &&
					entry.m_pLayer &&
					entry.m_pLayer->GetType() == kAnimLayerTypeState)
				{
					static_cast<AnimStateLayer*>(entry.m_pLayer)->ReloadScriptData(pAnimActor);
				}
			}

			m_pAnimActor = pAnimActor;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::DebugOnly_ForceUpdateOverlaySnapshot(AnimOverlaySnapshot* pNewSnapshot)
{
	for (U32F i = 0; i < m_numLayers; ++i)
	{
		LayerEntry& entry = m_layerEntries[i];
		if (!entry.m_created || !entry.m_pLayer)
			continue;

		switch (entry.m_pLayer->GetType())
		{
		case kAnimLayerTypeState:
			static_cast<AnimStateLayer*>(entry.m_pLayer)->DebugOnly_ForceUpdateOverlaySnapshot(pNewSnapshot);
			break;

		case kAnimLayerTypeSimple:
			static_cast<AnimSimpleLayer*>(entry.m_pLayer)->DebugOnly_ForceUpdateOverlaySnapshot(pNewSnapshot);
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 AnimControl::GetJointCount() const
{
	return ndanim::GetNumJointsInSegment(m_pAnimData->m_pSkeleton, 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AnimControl::GetJointName(U32 iJoint) const
{
	U32 jointCount = GetJointCount();
	if (iJoint < jointCount)
	{
		return m_pAnimData->m_curSkelHandle.ToArtItem()->m_pJointDescs[ iJoint ].m_pName;
	}
	else
	{
		return nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimControl::GetJointSid(U32 iJoint) const
{
	U32 jointCount = GetJointCount();
	if (iJoint < jointCount)
	{
		return m_pAnimData->m_curSkelHandle.ToArtItem()->m_pJointDescs[iJoint].m_nameId;
	}
	else
	{
		return INVALID_STRING_ID_64;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::AllocateOverlays(U32 instanceSeed, StringId64 priorityNameId, bool uniDirectional /* = false */)
{
	ANIM_ASSERT(m_pAnimOverlays == nullptr);
	m_pAnimOverlays = NDI_NEW AnimOverlays();
	m_pAnimOverlays->Init(instanceSeed, priorityNameId, uniDirectional);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimControl::SetCustomStateBlendOverlay(StringId64 blendOverlay)
{
	m_stateBlendOverlay.m_pCustomOverlay = ScriptPointer<DC::BlendOverlayMap>(blendOverlay);
	return m_stateBlendOverlay.m_pCustomOverlay != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::SetDefaultStateBlendOverlay(StringId64 blendOverlay)
{
	m_stateBlendOverlay.m_pDefaultOverlay = ScriptPointer<DC::BlendOverlayMap>(blendOverlay, SID("anim-blends"));;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::SetDefaultAnimBlendOverlay(StringId64 blendOverlay)
{
	m_animBlendOverlay.m_pDefaultOverlay = ScriptPointer<DC::BlendOverlayMap>(blendOverlay, SID("anim-blends"));;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::BlendOverlayEntry* AnimControl::LookupStateBlendOverlay(StringId64 srcId, StringId64 dstId) const
{
	return m_stateBlendOverlay.Lookup(srcId, dstId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimOverlaySnapshot* AnimControl::GetAnimOverlaySnapshot()
{
	return m_pAnimOverlays ? m_pAnimOverlays->GetSnapshot() : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimOverlaySnapshot* AnimControl::GetAnimOverlaySnapshot() const
{
	return m_pAnimOverlays ? m_pAnimOverlays->GetSnapshot() : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::ChangeAnimData(FgAnimData* pAnimData)
{
	m_pAnimData = pAnimData;
	for (U32 i = 0; i < m_numLayers; ++i)
	{
		if (m_layerEntries[i].m_type == kAnimLayerTypeState)
		{
			AnimStateLayer* pStateLayer = (AnimStateLayer*)m_layerEntries[i].m_pLayer;
			pStateLayer->m_pAnimData = pAnimData;
		}
	}

	const Process* pProcess = m_pAnimData->m_hProcess.ToProcess();
	if (pProcess)
	{
		const ProcessBucket bucket = EngineComponents::GetProcessMgr()->GetProcessBucket(*pProcess);
		if (bucket != kProcessBucketUnknown)
		{
			EngineComponents::GetAnimMgr()->ChangeBucket(m_pAnimData, (U32F)bucket);
		}
	}

	if (const ArtItemSkeleton* pSkel = m_pAnimData->m_curSkelHandle.ToArtItem())
	{
		m_animTable.Init(pSkel->m_skelId, pSkel->m_hierarchyId);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::SortLayers(LayerEntry* pEntries)
{
	ANIM_ASSERT(pEntries);
	PROFILE(Animation, AnCtrl_SortLayers);
	// Let's bubble sort the layers because they should all be in order except for the few times
	// when a layer was added/removed.
	// Let's also start from the back where it's most likely the new layer was added.
	bool itemsSwapped;
	do
	{
		itemsSwapped = false;

		for (U64 i = m_numLayers - 1; i > 0; --i)
		{
			U64 iA = i;
			U64 iB = i - 1;
			LayerEntry& entryA = pEntries[iA];
			LayerEntry& entryB = pEntries[iB];

			// 'A' has a higher index than 'B' hence will run later than 'A'.
			// If 'A' priority is lower than 'B' then we swap them
			if (entryA.m_priority < entryB.m_priority)
			{
				// Swap entries
				LayerEntry copyEntryA = entryA;
				entryA = entryB;
				entryB = copyEntryA;
				itemsSwapped = true;
			}
		}
	} while (itemsSwapped);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControl::BeginStepInternal(float deltaTime)
{
	PROFILE(Animation, AnimControl_BeginStepInternal);

	// Reset our triggered effects list.
	m_triggeredEffects.Clear();

	ANIM_ASSERT(m_numLayers);
	LayerEntry* pEntries = m_layerEntries;
	ANIM_ASSERT(pEntries);

	if (m_pAnimData)
		m_pAnimData->SetCameraCutsDisabled(m_pDummyInstance != nullptr || m_cameraCutsDisabled);

	if (m_pDummyInstance)
	{
		// Update the dummy instance, if we have one (for cinematics).
		// We must do it here, otherwise we get subtle phase mismatches between the dummy instances and the real instances.
		m_pDummyInstance->PhaseUpdate(deltaTime);

		if (m_pDummyInstance->HasFadedOut())
			DestroyDummyInstance();
	}

	if (m_numLayers > 1)
	{
		// Sort our layers in priority order!
		SortLayers(pEntries);
	}

	for (U64 i = 0; i < m_numLayers; ++i)
	{
		// If the layer has a state and a non-zero blend we update it.
		LayerEntry& entry = pEntries[i];
		if (entry.m_created)
		{
			ANIM_ASSERT(entry.m_pLayer);
			switch (entry.m_type)
			{
			case kAnimLayerTypeSimple:
				{
					AnimSimpleLayer* pSimpleLayer = static_cast<AnimSimpleLayer*>(entry.m_pLayer);
					EffectList* pTriggeredEffects = pSimpleLayer->TriggeredEffectsEnabled() ?  &m_triggeredEffects : nullptr;
					pSimpleLayer->AnimSimpleLayer::StepInternal(deltaTime, pTriggeredEffects, m_pAnimData);
				}
				break;

			case kAnimLayerTypeSnapshot:
				{
					AnimSnapshotLayer* pSnapshotLayer = static_cast<AnimSnapshotLayer*>(entry.m_pLayer);
					EffectList* pTriggeredEffects = pSnapshotLayer->TriggeredEffectsEnabled() ?  &m_triggeredEffects : nullptr;
					pSnapshotLayer->AnimLayer::BeginStep(deltaTime, pTriggeredEffects, m_pAnimData);
				}
				break;

			case kAnimLayerTypePose:
				{
					AnimPoseLayer* pPoseLayer = static_cast<AnimPoseLayer*>(entry.m_pLayer);
					EffectList* pTriggeredEffects = pPoseLayer->TriggeredEffectsEnabled() ?  &m_triggeredEffects : nullptr;
					pPoseLayer->AnimLayer::BeginStep(deltaTime, pTriggeredEffects, m_pAnimData);
				}
				break;

			case kAnimLayerTypeState:
				{
					AnimStateLayer* pStateLayer = static_cast<AnimStateLayer*>(entry.m_pLayer);
					EffectList* pTriggeredEffects = pStateLayer->TriggeredEffectsEnabled() ?  &m_triggeredEffects : nullptr;
					pStateLayer->AnimStateLayer::BeginStep(deltaTime, pTriggeredEffects, m_pAnimData);
				}
				break;

			case kAnimLayerTypeCmdGenerator:
				{
					EffectList* pTriggeredEffects = entry.m_pLayer->TriggeredEffectsEnabled() ?  &m_triggeredEffects : nullptr;
					static_cast<AnimCmdGeneratorLayer*>(entry.m_pLayer)->AnimCmdGeneratorLayer::BeginStep(deltaTime, pTriggeredEffects, m_pAnimData);
				}
				break;
			case kAnimLayerTypeCopyRemap:
			{
				EffectList* pTriggeredEffects = entry.m_pLayer->TriggeredEffectsEnabled() ? &m_triggeredEffects : nullptr;
				entry.m_pLayer->BeginStep(deltaTime, pTriggeredEffects, m_pAnimData);
			}

				break;
			default:
				ANIM_ASSERTF(false, ("Unknown anim layer type encountered"));
			}
		}
	}
}
