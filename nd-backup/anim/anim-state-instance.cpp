/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-state-instance.h"

#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/camera/camera-interface.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/level/artitem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::Allocate(AnimStateLayer* pOwningLayer,
								 const AnimTable* pAnimTable,
								 const AnimOverlaySnapshot* pAnimOverlaySnapshot,
								 SnapshotNodeHeap* pSnapshotNodeHeap,
								 const StringId64* pChannelIds,
								 U32F numChannelIds,
								 const DC::AnimInfoCollection* pInfoCollection,
								 bool cacheTopInfo)
{
	m_pOwningLayer = pOwningLayer;
	m_pAnimTable = pAnimTable;
	m_pAnimOverlaySnapshot = pAnimOverlaySnapshot;

	ANIM_ASSERT(pInfoCollection);

	m_pChannelId	   = numChannelIds ? NDI_NEW(kAlign16) StringId64[numChannelIds] : nullptr;
	m_pChannelDelta	   = numChannelIds ? NDI_NEW(kAlign16) Locator[numChannelIds] : nullptr;
	m_pChannelPrevLoc  = numChannelIds ? NDI_NEW(kAlign16) Locator[numChannelIds] : nullptr;
	m_pChannelCurrLoc  = numChannelIds ? NDI_NEW(kAlign16) Locator[numChannelIds] : nullptr;
	m_numChannelDeltas = numChannelIds;

	for (U32F i = 0; i < numChannelIds; ++i)
	{
		ANIM_ASSERT(pChannelIds[i] != INVALID_STRING_ID_64);
		m_pChannelId[i] = pChannelIds[i];
	}

	m_stateSnapshot.Init(pSnapshotNodeHeap);
	m_stateSnapshot.AllocateAnimDeltaTweakXform();

	m_pInfoCollection = NDI_NEW DC::AnimInfoCollection(*pInfoCollection);
	U8* pInstanceInfo = NDI_NEW (kAlign16) U8[pInfoCollection->m_instanceSize];
	m_pInfoCollection->m_instance = (DC::AnimInstanceInfo*)pInstanceInfo;

	m_flags.m_cacheTopInfo = cacheTopInfo;
	if (cacheTopInfo)
	{
		ANIM_ASSERT(pInfoCollection->m_topSize > 0);
		U8* pTopInfo = NDI_NEW (kAlign16) U8[pInfoCollection->m_topSize];
		m_pInfoCollection->m_top = (DC::AnimTopInfo*)pTopInfo;
	}

	ANIM_ASSERT(m_pInfoCollection->m_actor);
	ANIM_ASSERT(m_pInfoCollection->m_instance);
	ANIM_ASSERT(m_pInfoCollection->m_top);

	m_effectiveFade = 0.0f;

	ResetChannelDeltas();
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::ValidBits AnimStateInstance::GetValidBitsFromState(const ArtItemSkeleton* pSkel, U32 iGroup) const
{
	return m_stateSnapshot.GetValidBitsFromAnimNodeTree(pSkel, iGroup);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pOwningLayer, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pChannelId, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pChannelDelta, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pChannelPrevLoc, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pChannelCurrLoc, deltaPos, lowerBound, upperBound);

	RelocatePointer(m_pAnimTable, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pAnimOverlaySnapshot, deltaPos, lowerBound, upperBound);

	RelocatePointer(m_pInfoCollection->m_actor, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pInfoCollection->m_instance, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pInfoCollection->m_top, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pInfoCollection, deltaPos, lowerBound, upperBound);

	m_stateSnapshot.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimStateInstance::GetPhaseAnim() const
{
	return m_stateSnapshot.m_translatedPhaseAnimName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle AnimStateInstance::GetPhaseAnimArtItem() const
{
	return AnimMasterTable::LookupAnim(m_stateSnapshot.m_translatedPhaseSkelId,
									   m_stateSnapshot.m_translatedPhaseHierarchyId,
									   m_stateSnapshot.m_translatedPhaseAnimName);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle AnimStateInstance::GetOriginalPhaseAnimArtItem() const
{
	ArtItemAnimHandle ret;
	if (const AnimTable* pAnimTable = GetAnimTable())
	{
		ret = pAnimTable->LookupAnim(m_stateSnapshot.m_originalPhaseAnimName);
	}
	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::ChangePhaseAnim(const ArtItemAnim* pAnim)
{
	if (!pAnim)
		return;

	if (m_stateSnapshot.m_translatedPhaseAnimName == pAnim->GetNameId())
		return;

	m_stateSnapshot.m_translatedPhaseAnimName	 = pAnim->GetNameId();
	m_stateSnapshot.m_translatedPhaseSkelId		 = pAnim->m_skelID;
	m_stateSnapshot.m_translatedPhaseHierarchyId = pAnim->m_pClipData->m_animHierarchyId;

	RefreshAnimPointers();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimStateInstance::EvaluateFloatChannels(const StringId64* channelNames,
											  U32F numChannels,
											  float* pOutChannelFloats,
											  const AnimStateEvalParams& params /* = AnimStateEvalParams() */) const
{
	bool wantFlip = false;

	switch (params.m_flipMode)
	{
	case DC::kAnimFlipModeNever:
		wantFlip = false;
		break;
	case DC::kAnimFlipModeAlways:
		wantFlip = true;
		break;
	case DC::kAnimFlipModeFromInstance:
		wantFlip = IsFlipped();
		break;
	default:
		ANIM_ASSERTF(false, ("Unknown flip mode %d", params.m_flipMode));
		wantFlip = false;
		break;
	}

	const DC::AnimInfoCollection* pInfoCollection = GetAnimInfoCollection();

	SnapshotEvaluateParams snapParams;
	snapParams.m_blendChannels = pInfoCollection->m_actor && (pInfoCollection->m_actor->m_blendChannels != 0);
	snapParams.m_statePhase	   = m_phase;
	snapParams.m_statePhasePre = m_prevPhase;
	snapParams.m_flipped	   = wantFlip;
	snapParams.m_pRemapLayer   = params.m_pRemapLayer;
	snapParams.m_wantRawScale  = params.m_wantRawScale;
	snapParams.m_disableRetargeting	  = params.m_disableRetargeting;
	snapParams.m_forceChannelBlending = params.m_forceChannelBlending;
	snapParams.m_pCameraCutInfo		  = params.m_pCameraCutInfo;

	const U32F retval = m_stateSnapshot.EvaluateFloat(channelNames, numChannels, pOutChannelFloats, snapParams);

	return retval;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimStateInstance::EvaluateChannels(const StringId64* channelNames,
										 U32F numChannels,
										 ndanim::JointParams* pOutChannelJoints,
										 const AnimStateEvalParams& params /* = AnimStateEvalParams() */) const
{
	bool wantFlip = false;

	switch (params.m_flipMode)
	{
	case DC::kAnimFlipModeNever:
		wantFlip = false;
		break;
	case DC::kAnimFlipModeAlways:
		wantFlip = true;
		break;
	case DC::kAnimFlipModeFromInstance:
		wantFlip = IsFlipped();
		break;
	default:
		ANIM_ASSERTF(false, ("Unknown flip mode %d", params.m_flipMode));
		wantFlip = false;
		break;
	}

	const DC::AnimInfoCollection* pInfoCollection = GetAnimInfoCollection();

	SnapshotEvaluateParams snapParams;
	snapParams.m_blendChannels = pInfoCollection->m_actor && (pInfoCollection->m_actor->m_blendChannels != 0);
	snapParams.m_statePhase	   = m_phase;
	snapParams.m_statePhasePre = m_prevPhase;
	snapParams.m_flipped	   = wantFlip;
	snapParams.m_pRemapLayer   = params.m_pRemapLayer;
	snapParams.m_wantRawScale  = params.m_wantRawScale;
	snapParams.m_disableRetargeting	  = params.m_disableRetargeting;
	snapParams.m_forceChannelBlending = params.m_forceChannelBlending;
	snapParams.m_pCameraCutInfo		  = params.m_pCameraCutInfo;

	const U32F retval = m_stateSnapshot.Evaluate(channelNames, numChannels, pOutChannelJoints, snapParams);

	return retval;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 AnimStateInstance::GetFrameCount() const
{
	const ArtItemAnim* pArtItemAnim = GetPhaseAnimArtItem().ToArtItem();
	if (pArtItemAnim)
	{
		const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;
		return pClipData ? pClipData->m_numTotalFrames : 0;
	}

	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstance::MayaFrame() const
{
	const ArtItemAnim* pArtItemAnim = GetPhaseAnimArtItem().ToArtItem();
	if (pArtItemAnim)
	{
		const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;

		return GetMayaFrameFromClip(pClipData, Phase());
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstance::PrevMayaFrame() const
{
	const ArtItemAnim* pArtItemAnim = GetPhaseAnimArtItem().ToArtItem();
	if (pArtItemAnim)
	{
		const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;

		return GetMayaFrameFromClip(pClipData, PrevPhase());
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstance::PhaseToMayaFrame(float phase) const
{
	const ArtItemAnim* pArtItemAnim = GetPhaseAnimArtItem().ToArtItem();
	if (pArtItemAnim)
	{
		const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;
		return GetMayaFrameFromClip(pClipData, MinMax01(phase));
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstance::MayaMaxFrame() const
{
	const ArtItemAnim* pArtItemAnim = GetPhaseAnimArtItem().ToArtItem();
	if (pArtItemAnim)
	{
		const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;

		return GetMayaFrameFromClip(pClipData, 1.0f);
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstance::GetDuration() const
{
	const ArtItemAnim* pArtItemAnim = GetPhaseAnimArtItem().ToArtItem();
	if (pArtItemAnim)
	{
		const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;
		return pClipData->m_fNumFrameIntervals * pClipData->m_secondsPerFrame;
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateInstance::GetActiveTransitionByDestState(StringId64 destStateId,
																			const DC::AnimInfoCollection* pInfoCollection,
																			IAnimTransitionSearch* pCustomSearch) const
{
	const DC::AnimTransition* pTrans = AnimStateGetTransitionByFinalState(&m_stateSnapshot.m_animState,
																		  destStateId,
																		  pInfoCollection,
																		  pCustomSearch);

	const TransitionQueryInfo& qi = MakeTransitionQueryInfo(pInfoCollection);
	
	if (pTrans && AnimTransitionActive(pTrans, qi))
	{
		return pTrans;
	}
	else if (pTrans)
	{
		//Try and find an active transition with the same name
		pTrans = GetActiveTransitionByName(pTrans->m_name, pInfoCollection);
		return pTrans;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstance::HasChannel(StringId64 channelId) const
{
	ndanim::JointParams jp;
	const bool found = 1 == EvaluateChannels(&channelId, 1, &jp);
	return found;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator AnimStateInstance::GetChannelDelta(StringId64 channelId) const
{
	ANIM_ASSERT(m_numChannelDeltas);

	for (U32F i = 0; i < m_numChannelDeltas; ++i)
	{
		bool correctChannel = false;

		if (m_pChannelId[i] == channelId)
		{
			Locator delta = m_pChannelDelta[i];

			const Vector deltaTrans = delta.GetTranslation() - Point(kZero);

			const Transform deltaTweak = IsAnimDeltaTweakEnabled() ? GetAnimDeltaTweakTransform() : Transform(kIdentity);
			const Vector scaledTrans = deltaTrans * deltaTweak;

			delta.SetTranslation(Point(kZero) + scaledTrans);
			
			return delta;
		}
	}

	return Locator(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// ex. I want to get align channel delta in apReference channel space.
const Locator AnimStateInstance::GetChannelDeltaInRefChannelSpace(StringId64 channelId, StringId64 refChannelId) const
{
	ANIM_ASSERT(m_numChannelDeltas);

	bool foundChannel = false;
	bool foundRefChannel = false;

	Locator prevLoc, currLoc;
	Locator prevRefLoc, currRefLoc;

	for (U32F i = 0; i < m_numChannelDeltas; ++i)
	{
		if (m_pChannelId[i] == channelId)
		{
			prevLoc = m_pChannelPrevLoc[i];
			currLoc = m_pChannelCurrLoc[i];

			foundChannel = true;
		}

		if (m_pChannelId[i] == refChannelId)
		{
			prevRefLoc = m_pChannelPrevLoc[i];
			currRefLoc = m_pChannelCurrLoc[i];

			foundRefChannel = true;
		}
	}

	if (foundChannel && foundRefChannel)
	{
		// both channels found.
		const Locator channelDelta2 = prevLoc.UntransformLocator(currLoc);
		const Locator channelDelta2InRefSpace = prevRefLoc.UntransformLocator(channelDelta2);

		return channelDelta2InRefSpace;
	}

	return Locator(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator AnimStateInstance::GetChannelPrevLoc(StringId64 channelId) const
{
	for (U32F i = 0; i < m_numChannelDeltas; ++i)
	{
		if (m_pChannelId[i] == channelId)
		{
			return m_pChannelPrevLoc[i];
		}
	}

	return Locator(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator AnimStateInstance::GetChannelCurLoc(StringId64 channelId) const
{
	for (U32F i = 0; i < m_numChannelDeltas; ++i)
	{
		if (m_pChannelId[i] == channelId)
		{
			return m_pChannelCurrLoc[i];
		}
	}

	return Locator(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstance::IsApLocatorActive() const
{
	return ((m_stateSnapshot.m_animState.m_flags & DC::kAnimStateFlagApMoveUpdate) != 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::ApplyApRestrictAdjustPs(Vector_arg apMovePs)
{
	m_apReference.AdjustTranslationPs(apMovePs);
	m_apRestrictAdjustPs += apMovePs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::Init(const DC::AnimState* pState,
							 const DC::AnimInfoCollection* pInfoCollection,
							 AnimOverlaySnapshot* pOverlaySnapshot,
							 const FgAnimData* pAnimData,
							 StringId64 prevPhaseAnimId,
							 const BlendOverlay* pAnimBlendOverlay,
							 const FadeToStateParams& params)
{
	//PROFILE(Animation, ASI_Init);

	// Get the actor info (the shared struct owned by AnimControl)
	DC::AnimInfoCollection* pLocalInfoCollection = m_pInfoCollection;
	pLocalInfoCollection->m_actor = pInfoCollection->m_actor;

	// Copy the instance info (snapshot how it looked when we created this instance)
	ANIM_ASSERT(pInfoCollection->m_instance);
	const DC::AnimInstanceInfo* pInstanceInfo = pInfoCollection->m_instance;
	memcpy(pLocalInfoCollection->m_instance, pInstanceInfo, pLocalInfoCollection->m_instanceSize);

	if (m_flags.m_cacheTopInfo)
	{
		ANIM_ASSERT(pInfoCollection->m_topSize == pLocalInfoCollection->m_topSize);
		const DC::AnimTopInfo* pTopInfo = pInfoCollection->m_top;
		memcpy(pLocalInfoCollection->m_top, pTopInfo, pLocalInfoCollection->m_topSize);
	}

	m_stateSnapshot.m_rootNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;

	UpdateFromState(pState, pOverlaySnapshot, pAnimData, &params);

	// potentially needed for state start phase func
	m_stateSnapshot.SetAnimDeltaTweakTransform(params.m_animDeltaTweakXform);

	// Select start phase
	{
		float stateStartPhaseToUse = params.m_stateStartPhase;

		// Do we have a random start phase?
		if (m_stateSnapshot.m_animState.m_flags & DC::kAnimStateFlagRandomStartPhase)
		{
			stateStartPhaseToUse = frand(0.0f, 1.0f);
		}

		if (m_stateSnapshot.m_animState.m_startPhaseFunc && !params.m_ignoreStartPhaseFunc)
		{
			DC::AnimInfoCollection scriptInfoCollection(*pLocalInfoCollection);
			scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
			scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
			scriptInfoCollection.m_top = scriptInfoCollection.m_top;

			DC::AnimStateSnapshotInfo dcSnapshot;
			dcSnapshot.m_translatedPhaseAnimName		= m_stateSnapshot.m_translatedPhaseAnimName;
			dcSnapshot.m_translatedPhaseAnimSkelId		= m_stateSnapshot.m_translatedPhaseSkelId.GetValue();
			dcSnapshot.m_translatedPhaseAnimHierarchyId	= m_stateSnapshot.m_translatedPhaseHierarchyId;
			dcSnapshot.m_stateSnapshot					= &m_stateSnapshot;

			StringId64 prevStateId = INVALID_STRING_ID_64;
			if (params.m_pPrevInstance)
			{
				prevStateId = params.m_pPrevInstance->GetStateName();
			}

			const DC::ScriptLambda* pLambda = m_stateSnapshot.m_animState.m_startPhaseFunc;

			//const DC::AnimState* pPrevDCState = params.m_pPrevInstance ? &params.m_pPrevInstance->m_stateSnapshot.m_animState : nullptr;

			const ScriptValue argv[] = { ScriptValue(&m_stateSnapshot.m_animState),
										 ScriptValue(&dcSnapshot),
										 ScriptValue(&scriptInfoCollection),
										 ScriptValue(params.m_stateStartPhase),
										 ScriptValue(prevStateId),
										 ScriptValue(prevPhaseAnimId) };

			stateStartPhaseToUse = ScriptManager::Eval(pLambda, SID("anim-state-start-phase-func"), ARRAY_COUNT(argv), argv).m_float;
		}

		m_phase = m_prevPhase = Limit01(stateStartPhaseToUse);
		ANIM_ASSERT(IsFinite(m_phase));
		m_phaseRateEstimate = 1.0f;

		if (params.m_phaseNetSkip > 0.f)
		{
			m_phase = Limit01(m_phase + params.m_phaseNetSkip);
		}
	}

	m_remainderTime = 0.0f;
	m_blendType = params.m_blendType;

	m_flags.m_transitionsEnabled = true;
	m_flags.m_phaseFrozen = params.m_freezeDestState;
	m_flags.m_phaseFrozenRequested = m_flags.m_phaseFrozen;
	m_flags.m_phaseFrozenDuringFadeIn = params.m_freezeDestState;
	m_flags.m_freezeFadingOutStates = params.m_freezeSrcState;
	m_flags.m_useMayaFadeStyle = false;
	m_flags.m_skipPhaseUpdateThisFrame = params.m_skipFirstFrameUpdate;
	m_flags.m_phaseUpdatedManuallyThisFrame = false;
	m_flags.m_phaseNetSkipAppliedThisFrame = params.m_phaseNetSkip >= 0.f; // normally false, true in MP if no skip phase is turned on for playing catch up on an animation
	m_flags.m_disableApRef = false;
	m_flags.m_disableFeatherBlend = false;
	m_flags.m_disableAutoTransitions = false;
	m_pSharedTime = params.m_pSharedTime;
	m_firstUpdatePhase = params.m_firstUpdatePhase;

	// Reset the position aware variables
	m_apReference = BoundFrame(kIdentity);
	m_flags.m_savedAlign = false;

	ResetChannelDeltas();

	ANIM_ASSERT(params.m_animFadeTime >= 0.0f);
	ANIM_ASSERT(params.m_motionFadeTime >= 0.0f);
	m_animFadeTotal = m_animFadeLeft = params.m_animFadeTime;
	m_motionFadeTotal = m_motionFadeLeft = params.m_motionFadeTime;

	// allow overwriting anim blend time
	if (pAnimBlendOverlay != nullptr)
	{
		const DC::BlendOverlayEntry* pEntry = pAnimBlendOverlay->Lookup(prevPhaseAnimId, m_stateSnapshot.m_translatedPhaseAnimName);
		if (pEntry != nullptr)
		{
			if (pEntry->m_animFadeTime >= 0.0f)
			{
				m_animFadeTotal = m_animFadeLeft = pEntry->m_animFadeTime;
				m_blendOverrideFlags |= kAnimFadeTimeOverriden;
			}
			if (pEntry->m_motionFadeTime >= 0.0f)
			{
				m_motionFadeTotal = m_motionFadeLeft = pEntry->m_motionFadeTime;
				m_blendOverrideFlags |= kMotionFadeTimeOverriden;
			}
			if (pEntry->m_curve != DC::kAnimCurveTypeInvalid)
			{
				m_blendType = pEntry->m_curve;
				m_blendOverrideFlags |= kAnimCurveOverriden;
			}
		}
	}
	m_blendOverrideFlags |= params.m_blendOverrideFlags; // blend time could be modified at state layer.

	UpdateAnimFade(0.0f);
	UpdateMotionFade(0.0f);

	m_stateSnapshot.RefreshPhasesAndBlends(m_phase, true, pLocalInfoCollection); // assume we always construct new state instances on the top

	m_customApRefId = params.m_customApRefId;
	SetApLocator(params.m_apRef);
	m_apRestrictAdjustPs = kZero;

	if (m_customApRefId != INVALID_STRING_ID_64)
	{
		for (U32F i = 0; i < m_numChannelDeltas; ++i)
		{
			if (m_pChannelId[i] == SID("apReference"))
			{
				m_pChannelId[i] = m_customApRefId;
			}
		}
	}

	m_startTimeAnimClock = GetProcessClock()->GetCurTime();
	m_customFeatherBlendTableIndex = params.m_customFeatherBlendTableIndex;
	m_featherBlendTableBlend = params.m_customFeatherBlendTableBlend;

	m_subsystemControllerId = params.m_subsystemControllerId;

	if ((m_customFeatherBlendTableIndex < 0) && (m_stateSnapshot.m_animState.m_featherBlend != INVALID_STRING_ID_64))
	{
		m_customFeatherBlendTableIndex = g_featherBlendTable.LoginFeatherBlend(m_stateSnapshot.m_animState.m_featherBlend,
																			   pAnimData);
	}

	m_stateSnapshot.m_animState.m_flags |= params.m_extraStateFlags;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 AnimStateInstance::GetFeatherBlendTableIndex() const
{
	return m_customFeatherBlendTableIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstance::GetFeatherBlendTableBlend() const
{
	return m_featherBlendTableBlend;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::RefreshAnimPointers()
{
	m_stateSnapshot.RefreshAnims();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::UpdateFromState(const DC::AnimState* pState,
										AnimOverlaySnapshot* pOverlaySnapshot,
										const FgAnimData* pAnimData,
										const FadeToStateParams* pFadeToStateParams)
{
	const DC::AnimInfoCollection* pInfoCollection = GetAnimInfoCollection();
	const AnimTable* pAnimTable = m_pAnimTable;

	m_stateSnapshot.SnapshotAnimState(pState,
									  pOverlaySnapshot,
									  pOverlaySnapshot,
									  pAnimTable,
									  pInfoCollection,
									  pAnimData,
									  pFadeToStateParams);

	m_stateSnapshot.RefreshAnims();

	m_stateSnapshot.RefreshTransitions(pInfoCollection, pAnimTable);

#if !FINAL_BUILD
	const AnimSnapshotNode* pRootNode = m_stateSnapshot.GetSnapshotNode(m_stateSnapshot.m_rootNodeIndex);
	if (pRootNode)
	{
		pRootNode->DebugSubmitAnimPlayCount(&m_stateSnapshot);
	}
#endif // !FINAL_BUILD
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimState* AnimStateInstance::GetState() const
{
	return &m_stateSnapshot.m_animState;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimActorInfo* AnimStateInstance::GetAnimActorInfo() const
{
	const DC::AnimInfoCollection* pInfoCollection = m_pInfoCollection;
	return pInfoCollection->m_actor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimInstanceInfo* AnimStateInstance::GetAnimInstanceInfo() const
{
	const DC::AnimInfoCollection* pInfoCollection = m_pInfoCollection;
	return pInfoCollection->m_instance;
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::AnimInstanceInfo* AnimStateInstance::GetAnimInstanceInfo()
{
	const DC::AnimInfoCollection* pInfoCollection = m_pInfoCollection;
	return pInfoCollection->m_instance;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTopInfo* AnimStateInstance::GetAnimTopInfo() const
{
	const DC::AnimInfoCollection* pInfoCollection = m_pInfoCollection;
	return pInfoCollection->m_top;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimInfoCollection* AnimStateInstance::GetAnimInfoCollection() const
{
	return m_pInfoCollection;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimStateInstance::GetStateName() const
{
	return m_stateSnapshot.m_animState.m_name.m_symbol;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const BoundFrame& AnimStateInstance::GetApLocator() const
{
	return m_apReference;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimStateInstance::GetApRefChannelId() const
{
	return (m_customApRefId != INVALID_STRING_ID_64) ? m_customApRefId : SID("apReference");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::ResetChannelDeltas()
{
	if (m_numChannelDeltas)
	{
		Locator* pChannelDeltas = m_pChannelDelta;
		Locator* pChannelPrevLoc = m_pChannelPrevLoc;
		Locator* pChannelCurrLoc = m_pChannelCurrLoc;
		for (U32F i = 0; i < m_numChannelDeltas; ++i)
		{
			pChannelDeltas[i] = Locator(kIdentity);
			pChannelPrevLoc[i] = Locator(kIdentity);
			pChannelCurrLoc[i] = Locator(kIdentity);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::OnRelease()
{
	m_pOwningLayer->InstanceCallBackDestroy(this);

	ReleaseSnapshotNodes();

	if (m_customApRefId != INVALID_STRING_ID_64)
	{
		for (U32F i = 0; i < m_numChannelDeltas; ++i)
		{
			if (m_pChannelId[i] == m_customApRefId)
			{
				m_pChannelId[i] = SID("apReference");
			}
		}

		m_customApRefId = INVALID_STRING_ID_64;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::Shutdown()
{
	ReleaseSnapshotNodes();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::ReleaseSnapshotNodes()
{
	if (const AnimSnapshotNode* pRootNode = GetRootSnapshotNode())
	{
		pRootNode->ReleaseNodeRecursive(m_stateSnapshot.m_pSnapshotHeap);
	}

	m_stateSnapshot.m_rootNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstance::Phase() const
{
	return m_phase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::SetPhase(float phase)
{
	ANIM_ASSERTF(phase >= 0.0f && phase <= 1.0f,
				 ("Trying to set invalid phase %f on state '%s' (anim: '%s')",
				  phase,
				  DevKitOnly_StringIdToString(GetStateName()),
				  DevKitOnly_StringIdToString(GetPhaseAnim())));

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_cinematicCaptureMode))
	{
		m_prevPhase = m_phase;
		m_flags.m_phaseUpdatedManuallyThisFrame = true;
	}
	m_phase = phase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstance::PrevPhase() const
{
	return m_prevPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstance::PhaseRateEstimate() const
{
	return m_phaseRateEstimate;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::SetApLocator(const BoundFrame& apReference)
{
	const StringId64 phaseAnim = GetPhaseAnim();

	ANIM_ASSERTF(IsFinite(apReference.GetLocatorPs()), (DevKitOnly_StringIdToString(GetStateName())));
	ANIM_ASSERTF(IsNormal(apReference.GetRotationPs()), (DevKitOnly_StringIdToString(GetStateName())));
	ANIM_ASSERTF(IsFinite(apReference.GetLocator()), (DevKitOnly_StringIdToString(GetStateName())));
	ANIM_ASSERTF(IsNormal(apReference.GetRotation()), (DevKitOnly_StringIdToString(GetStateName())));
	ANIM_ASSERTF(Length(apReference.GetTranslation()) < 100000.0f, (DevKitOnly_StringIdToString(GetStateName())));
	m_apReference = apReference;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::SetApTranslationOnly(Point_arg newApPos)
{
	ANIM_ASSERTF(IsFinite(newApPos), (DevKitOnly_StringIdToString(GetStateName())));
	ANIM_ASSERTF(Length(newApPos) < 10000.0f, (DevKitOnly_StringIdToString(GetStateName())));
	m_apReference.SetTranslation(newApPos);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::UpdateTopInfo(const DC::AnimTopInfo* pInfo)
{
	if (m_flags.m_cacheTopInfo)
	{
		const DC::AnimInfoCollection* pInfoCollection = m_pInfoCollection;
		memcpy(pInfoCollection->m_top, pInfo, pInfoCollection->m_topSize);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstance::GetUpdatedPhase(F32 oldPhase,
									   F32 deltaTime,
									   F32& estimatedAnimScale,
									   F32* pRemainderTime,
									   ndanim::SharedTimeIndex* pSharedTime,
									   F32* pEstimatedTimeLeftInAnim) const
{
	F32 newPhase = oldPhase;
	F32 phaseRemainder = 0.0f;
	F32 updateRate  = m_stateSnapshot.m_updateRate;
	if (!m_flags.m_phaseFrozen && !m_flags.m_skipPhaseUpdateThisFrame)
	{
		newPhase = oldPhase + FromUPS(m_stateSnapshot.m_updateRate, deltaTime);

		if (m_stateSnapshot.m_animState.m_phaseFunc)
		{
			// Fetch the info collection and resolve all the pointers
			const DC::AnimInfoCollection* pInfoCollection = GetAnimInfoCollection();
			DC::AnimInfoCollection scriptInfoCollection(*pInfoCollection);
			scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
			scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
			scriptInfoCollection.m_top = scriptInfoCollection.m_top;

			DC::AnimStateSnapshotInfo dcSnapshot;
			dcSnapshot.m_translatedPhaseAnimName		= m_stateSnapshot.m_translatedPhaseAnimName;
			dcSnapshot.m_translatedPhaseAnimSkelId		= m_stateSnapshot.m_translatedPhaseSkelId.GetValue();
			dcSnapshot.m_translatedPhaseAnimHierarchyId	= m_stateSnapshot.m_translatedPhaseHierarchyId;
			dcSnapshot.m_stateSnapshot					= &m_stateSnapshot;

			const AnimStateInstance* pInst = m_pOwningLayer->CurrentStateInstance();
			bool m_isTopIntance = pInst ? (pInst->GetId() == GetId()) : false;

			const ScriptValue argv[] =
			{
				ScriptValue(&m_stateSnapshot.m_animState),
				ScriptValue(&scriptInfoCollection),
				ScriptValue(oldPhase),
				ScriptValue(m_stateSnapshot.m_updateRate),
				ScriptValue(deltaTime),
				ScriptValue(&dcSnapshot),
				ScriptValue(m_isTopIntance)
			};

			const DC::ScriptLambda* pLambda = m_stateSnapshot.m_animState.m_phaseFunc;
			const float overrideNewPhase = ScriptManager::Eval(pLambda, SID("anim-state-phase-func"), ARRAY_COUNT(argv), argv).m_float;

			if (deltaTime > 0.0f)
			{
				estimatedAnimScale = (overrideNewPhase - oldPhase) / (newPhase - oldPhase);
			}

			ANIM_ASSERT(IsFinite(newPhase));

			newPhase = overrideNewPhase;
			updateRate = deltaTime == 0.0f ? 0.0f : (newPhase - oldPhase) / deltaTime;
		}

		if (m_firstUpdatePhase >= 0.f)
		{
			newPhase = m_firstUpdatePhase;
		}

		if (m_forceNonFractionalFrame >= 0)
		{
			const ArtItemAnim* pArtItemAnim = GetPhaseAnimArtItem().ToArtItem();
			if (pArtItemAnim)
			{
				F32 frameNumber = newPhase / pArtItemAnim->m_pClipData->m_phasePerFrame;
				I32 frameFloor = (I32)floor(frameNumber);
				if ((frameFloor + 1) == m_forceNonFractionalFrame && ((F32)frameFloor) != frameNumber)
				{
					newPhase = m_forceNonFractionalFrame * pArtItemAnim->m_pClipData->m_phasePerFrame;
				}
			}
		}

		if (newPhase > 1.0f)
		{
			phaseRemainder = newPhase - 1.0f;
			newPhase = 1.0f;
		}
		if (newPhase < 0.0f)
		{
			phaseRemainder = -newPhase;
			newPhase = 0.0f;
		}

		ndanim::SharedTimeIndex::GetOrSet(pSharedTime, &newPhase, FILE_LINE_FUNC);
	}

	ANIM_ASSERTF(newPhase >= 0.0f && newPhase <= 1.0f,
				 ("GetUpdatedPhase() computed invalid phase %f (prev: %f dt: %f ur: %f) state: '%s' anim: '%s'",
				  newPhase,
				  oldPhase,
				  deltaTime,
				  updateRate,
				  DevKitOnly_StringIdToString(GetStateName()),
				  DevKitOnly_StringIdToString(GetPhaseAnim())));

	if (pEstimatedTimeLeftInAnim)
	{
		const ArtItemAnimHandle phaseAnimHandle = GetPhaseAnimArtItem();
		const ArtItemAnim* pPhaseAnim = phaseAnimHandle.ToArtItem();
		if (!pPhaseAnim)
		{
			*pEstimatedTimeLeftInAnim = 0.0f;
		}
		else
		{
			const ndanim::ClipData* pClipData = pPhaseAnim->m_pClipData;
			const float phaseRemaining = MinMax01(1.0f - newPhase);
			const float framesRemaining = pClipData->m_phasePerFrame != 0.0f ? phaseRemaining / pClipData->m_phasePerFrame : NDI_FLT_MAX;
			const float secondsRemaining = pClipData->m_framesPerSecond != 0.0f ? framesRemaining / pClipData->m_framesPerSecond : NDI_FLT_MAX;
			*pEstimatedTimeLeftInAnim = secondsRemaining;
		}
	}

	// Convert the phase remainder to seconds
	*pRemainderTime = updateRate == 0 ? 0 : phaseRemainder / updateRate;
	return newPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstance::IsAboutToEnd(float deltaTime) const
{
	const F32 currentPhase = Phase();
	F32 estimatedAnimScale;
	F32 remainderTime;
	const float newPhase = GetUpdatedPhase(currentPhase, deltaTime, estimatedAnimScale, &remainderTime, nullptr);

	return newPhase >= 1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstance::IsAboutToEnd(float deltaTime, F32 fadeOutTime) const
{
	const F32 currentPhase = Phase();
	F32 estimatedAnimScale;
	F32 remainderTime;
	F32 timeUntilAnimEnds;
	const float newPhase = GetUpdatedPhase(currentPhase, deltaTime, estimatedAnimScale, &remainderTime, nullptr, &timeUntilAnimEnds);

	return timeUntilAnimEnds <= fadeOutTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::PhaseUpdate(F32 deltaTime, bool topState, EffectList* pTriggeredEffects)
{
	PROFILE(Animation, AnimStateInstance_PhaseUpdate);

	//	MsgOut("## Anim ## - AnimStateInstance::PhaseUpdate\n");
	const DC::AnimInfoCollection* pInfoCollection = m_pInfoCollection;

	// Should we extract movement information from the align?
	const U32 kMaxChannelDeltas = 3;
	ANIM_ASSERT(m_numChannelDeltas <= kMaxChannelDeltas);

	ndanim::JointParams preJointParams[kMaxChannelDeltas];
	ndanim::JointParams postJointParams[kMaxChannelDeltas];
	U32F validChannels = -1;

	F32 phaseOffset = 0;
	if (m_flags.m_phaseNetSkipAppliedThisFrame)
	{
		phaseOffset = m_phase - m_prevPhase;
		m_phase = m_prevPhase;
	}

	// Evaluate with the 'old' phase first
	if (m_numChannelDeltas)
	{
		SnapshotEvaluateParams params;
		params.m_statePhase = m_phase;
		params.m_statePhasePre = m_prevPhase;
		params.m_flipped = m_stateSnapshot.IsFlipped();
		params.m_blendChannels = pInfoCollection && pInfoCollection->m_actor
								 && (pInfoCollection->m_actor->m_blendChannels != 0);

		const StringId64* pChannelNames = m_pChannelId;
		m_stateSnapshot.RefreshPhasesAndBlends(m_phase, topState, pInfoCollection);
		validChannels = validChannels
						& m_stateSnapshot.Evaluate(pChannelNames,
												   m_numChannelDeltas,
												   &preJointParams[0],
												   params);
		MsgAnimVerbose("Animation(" PFS_PTR "): Sampling animation tree at pre-phase  %5.3f -[%5.3f ; %5.3f ; %5.3f] \n",
					   this,
					   m_phase,
					   (float)preJointParams[0].m_trans.X(),
					   (float)preJointParams[0].m_trans.Y(),
					   (float)preJointParams[0].m_trans.Z());
	}

	if (m_flags.m_phaseNetSkipAppliedThisFrame)
	{
		m_prevPhase += phaseOffset;
	}
	else if (TRUE_IN_FINAL_BUILD(!g_animOptions.m_cinematicCaptureMode || !m_flags.m_phaseUpdatedManuallyThisFrame))
	{
		m_prevPhase = m_phase;
	}

	OMIT_FROM_FINAL_BUILD(m_flags.m_phaseUpdatedManuallyThisFrame = false);
	m_flags.m_phaseNetSkipAppliedThisFrame = false;

	F32 remainderTime = 0.0f;
	m_phase = GetUpdatedPhase(m_prevPhase, deltaTime, m_phaseRateEstimate, &remainderTime, m_pSharedTime);
	m_firstUpdatePhase = -1.f;
	m_remainderTime = remainderTime;

	if (m_flags.m_skipPhaseUpdateThisFrame)
	{
		m_flags.m_skipPhaseUpdateThisFrame = false;
	}

#if ANIM_ENABLE_ANIM_CHAIN_TRACE
	extern bool g_animChainDebugTrace;
	extern bool g_animChainDebugTraceScrubbing;
	if (FALSE_IN_FINAL_BUILD(g_animChainDebugTrace && g_animChainDebugTraceScrubbing && deltaTime != 0.0f))
	{
		MsgCinematic(FRAME_NUMBER_FMT "PhaseUpdate(): %s: dt=%.6f oldPhase=%.6f newPhase=%.6f (STATE)%s\n",
					 FRAME_NUMBER,
					 "state-anim",
					 deltaTime,
					 m_prevPhase,
					 m_phase,
					 m_pSharedTime != nullptr ? " *global*" : "");
	}
#endif

	m_stateSnapshot.RefreshPhasesAndBlends(m_phase, topState, pInfoCollection);

	MsgAnimVerbose("Animation(" PFS_PTR "): '%s' PhaseUpdate (pre: %5.4f - post: %5.4f)\n",
				   this,
				   DevKitOnly_StringIdToString(this->GetStateName()),
				   m_prevPhase,
				   m_phase);

	// give custom nodes a chance to do time-dependent updates
	m_stateSnapshot.StepNodes(deltaTime, pInfoCollection);

	// ... and then with the new phase. (We need to do a refresh here to propagate changes to the actor/instance info
	// And example of this is strafing forward/left switching to forward/right. It is the same state but different info settings.
	// Not doing this will result in the character swirling around.

	if (m_numChannelDeltas)
	{
		const StringId64* pChannelNames = m_pChannelId;

		AnimCameraCutInfo cameraCutInfo;
		cameraCutInfo.m_cameraIndex = g_hackTopAnimatedCameraIndex;
		cameraCutInfo.m_didCameraCut = false;

		SnapshotEvaluateParams params;
		params.m_pCameraCutInfo = &cameraCutInfo;
		params.m_statePhase = m_phase;
		params.m_statePhasePre = m_prevPhase;
		params.m_flipped = m_stateSnapshot.IsFlipped();
		params.m_blendChannels	= pInfoCollection && pInfoCollection->m_actor
								 && (pInfoCollection->m_actor->m_blendChannels != 0);

		validChannels = validChannels
						& m_stateSnapshot.Evaluate(pChannelNames, m_numChannelDeltas, &postJointParams[0], params);

		const FgAnimData* pAnimData = m_pOwningLayer->GetAnimData();

		if (m_pOwningLayer && pAnimData && pAnimData->AreCameraCutsDisabled())
		{
			cameraCutInfo.m_didCameraCut = false; // ignore camera cuts when we have a dummy instance (for cinematics)
		}

		m_flags.m_cameraCutThisFrame = cameraCutInfo.m_didCameraCut;

		MsgAnimVerbose("Animation(" PFS_PTR "): Sampling animation tree at post-phase %5.3f -[%5.3f ; %5.3f ; %5.3f] \n",
					   this,
					   m_phase,
					   (float)postJointParams[0].m_trans.X(),
					   (float)postJointParams[0].m_trans.Y(),
					   (float)postJointParams[0].m_trans.Z());

		// A camera cut occurred. We need to advance the phase so that the phaseAnim is on the next higher sample.
		if (m_flags.m_cameraCutThisFrame)
		{
			const ArtItemAnim* pAnim = AnimMasterTable::LookupAnim(m_stateSnapshot.m_translatedPhaseSkelId,
																   m_stateSnapshot.m_translatedPhaseHierarchyId,
																   m_stateSnapshot.m_translatedPhaseAnimName)
										   .ToArtItem();
			const ArtItemAnim* pLocalAnim = pAnim;
			const ndanim::ClipData* pClipData = pLocalAnim->m_pClipData;

			const float maxFrameSample = static_cast<float>(pClipData->m_fNumFrameIntervals);
			const float currentFrameSample = m_phase * maxFrameSample;
			const float adjustedFrameSample = ceilf(currentFrameSample);
			const float lostTimeSec = (adjustedFrameSample - currentFrameSample) * pClipData->m_secondsPerFrame;
			const float adjustedPhase = adjustedFrameSample / maxFrameSample;

			if (!g_animOptions.m_disableFrameClampOnCameraCut)
			{
				ANIM_ASSERTF(adjustedPhase >= 0.0f && adjustedPhase <= 1.0f,
							 ("Camera cut computed invalid phase %f for state '%s' (anim: '%s')",
							  adjustedPhase,
							  DevKitOnly_StringIdToString(GetStateName()),
							  pLocalAnim->GetName()));

				m_phase = adjustedPhase;
				m_remainderTime = lostTimeSec;
			}

			m_stateSnapshot.RefreshPhasesAndBlends(m_phase, topState, pInfoCollection);
			MsgAnimVerbose("Animation(" PFS_PTR "): PhaseUpdate (pre: %5.3f - post: %5.3f) DUE TO CUT\n", this, m_prevPhase, m_phase);

			
#if !defined(NDI_ARCH_SPU) && !FINAL_BUILD
			if (g_animOptions.m_debugCameraCuts)
			{
				StringBuilder<256> sb("%05u: CUT detected in anim '%s' [AnimStateInstance::PhaseUpdate()] (state instance)\n"
					"       curFrame = %.1f  adjFrame = %.1f  lostSec = %.4f  adjPhase = %.4f\n",
					(U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, pAnim->GetName(),
					currentFrameSample, adjustedFrameSample, lostTimeSec, adjustedPhase);

				MsgConPauseable(sb.c_str());
				MsgCinematic(sb.c_str());
			}
#endif

			if (pAnimData && pAnimData->AreCameraCutNotificationsEnabled())
				g_ndConfig.m_pCameraInterface->NotifyCameraCut(FILE_LINE_FUNC);
		}

		if (m_stateSnapshot.m_animState.m_flags & DC::kAnimStateFlagBlendChannelDeltasInTree)
		{
			pChannelNames = m_pChannelId;

			SnapshotEvaluateParams snapParams;
			snapParams.m_statePhase	   = m_phase;
			snapParams.m_statePhasePre = m_prevPhase;
			snapParams.m_flipped	   = m_stateSnapshot.IsFlipped();
			snapParams.m_blendChannels = pInfoCollection->m_actor && (pInfoCollection->m_actor->m_blendChannels != 0);

			ndanim::JointParams jointParamDeltas[kMaxChannelDeltas];
			validChannels = validChannels
							& m_stateSnapshot.EvaluateDelta(pChannelNames,
															m_numChannelDeltas,
															&jointParamDeltas[0],
															snapParams);

			Locator* pChannelDeltas = m_pChannelDelta;
			Locator* pChannelPrevLoc = m_pChannelPrevLoc;
			Locator* pChannelCurrLoc = m_pChannelCurrLoc;
			for (U32F i = 0; i < m_numChannelDeltas; ++i)
			{
				if (0 == (validChannels & (1ULL << i)))
				{
					pChannelDeltas[i] = Locator(kIdentity);
					continue;
				}

				pChannelPrevLoc[i] = Locator(preJointParams[i].m_trans, preJointParams[i].m_quat);
				pChannelCurrLoc[i] = Locator(postJointParams[i].m_trans, postJointParams[i].m_quat);

				const Vector deltaTranslation = AsVector(jointParamDeltas[i].m_trans);
				const Quat deltaRotation = jointParamDeltas[i].m_quat;

				// We need to accumulate the deltas because we might get called multiple times in one update step
				// due to a state change and carry-over phase.
				Locator channelDelta = pChannelDeltas[i];
				const Quat currentDeltaRot = channelDelta.Rot();
				channelDelta.SetPos(channelDelta.Pos() + Rotate(currentDeltaRot, deltaTranslation));
				channelDelta.SetRot(currentDeltaRot * deltaRotation);
				pChannelDeltas[i] = channelDelta;

				ANIM_ASSERT(IsNormal(channelDelta.GetRotation()));
				ANIM_ASSERT(Length(channelDelta.GetTranslation()) < 10000.0f);
			}
		}
		else
		{
			Locator* pChannelDeltas = m_pChannelDelta;
			Locator* pChannelPrevLoc = m_pChannelPrevLoc;
			Locator* pChannelCurrLoc = m_pChannelCurrLoc;
			for (U32F i = 0; i < m_numChannelDeltas; ++i)
			{
				if (0 == (validChannels & (1ULL << i)))
				{
					pChannelDeltas[i] = Locator(kIdentity);
					continue;
				}

				const Locator preAlign = Locator(preJointParams[i].m_trans, preJointParams[i].m_quat);
				const Locator postAlign = Locator(postJointParams[i].m_trans, postJointParams[i].m_quat);
				pChannelPrevLoc[i] = preAlign;
				pChannelCurrLoc[i] = postAlign;

				const Quat preAlignConjugate = Normalize(Conjugate(preAlign.Rot()));
				const Vector deltaTranslation = Rotate(preAlignConjugate, postAlign.Pos() - preAlign.Pos());
				const Quat deltaRotation = Normalize(postAlign.Rot() * preAlignConjugate);

				// We need to accumulate the deltas because we might get called multiple times in one update step
				// due to a state change and carry-over phase.
				Locator channelDelta = pChannelDeltas[i];
				const Quat currentDeltaRot = Normalize(channelDelta.Rot());
				channelDelta.SetPos(channelDelta.Pos() + Rotate(currentDeltaRot, deltaTranslation));
				channelDelta.SetRot(currentDeltaRot * deltaRotation);
				pChannelDeltas[i] = channelDelta;

				ANIM_ASSERT(IsNormal(channelDelta.GetRotation()));
				ANIM_ASSERT(Length(channelDelta.GetTranslation()) < 10000.0f);
			}
			MsgAnimVerbose("Animation(" PFS_PTR "): '%s' Delta translation - [%5.3f ; %5.3f ; %5.3f] \n",
						   this,
						   DevKitOnly_StringIdToString(this->GetStateName()),
						   (float)pChannelDeltas[0].GetTranslation().X(),
						   (float)pChannelDeltas[0].GetTranslation().Y(),
						   (float)pChannelDeltas[0].GetTranslation().Z());
		}
	}

	if (HasSavedAlign())
	{
		const Locator saveAlignWs = GetApLocator().GetLocatorWs();
		const Locator alignDeltaLoc = GetChannelDelta(SID("align"));
		Locator saveAlignWsNew = saveAlignWs.TransformLocator(alignDeltaLoc);
		saveAlignWsNew.SetRot(Normalize(saveAlignWsNew.Rot()));
		BoundFrame saveAlignBf = GetApLocator();
		saveAlignBf.SetLocatorWs(saveAlignWsNew);
		SetSavedAlign(saveAlignBf);
	}
	
	// There are cases where we want to display a different phase compared to the phase used to extract align movement.
	F32 animPhase = m_phase;
	if (m_flags.m_useMayaFadeStyle && m_currentAnimFade < 1.0f)
	{
		animPhase = Limit01(animPhase + m_animFadeLeft * m_stateSnapshot.m_updateRate);
		// Propagate the new state phase to all anim nodes where scripts might run to calculate the new frames and blends.
		m_stateSnapshot.RefreshPhasesAndBlends(animPhase, topState, pInfoCollection);
	}

	// Get the triggered effects
	if (pTriggeredEffects)
	{
		const bool reversed = m_phase < m_prevPhase;
		// Get effects that should trigger this frame.
		m_stateSnapshot.GetTriggeredEffects(pInfoCollection,
											m_prevPhase,
											m_phase,
											IsFlipped(),
											m_effectiveFade,
											topState,
											reversed,
											GetStateName(),
											AnimFade(),
											pTriggeredEffects,
											this,
											m_stateSnapshot.m_updateRate);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::Loop(const DC::AnimInfoCollection* pInfoCollection, bool topState, const FadeToStateParams& params)
{
	PROFILE(Animation, ASI_Loop);

	DC::AnimInfoCollection* pInstInfoCollection = m_pInfoCollection;
	if (m_flags.m_cacheTopInfo)
	{
		const DC::AnimTopInfo* pTopInfo = pInfoCollection->m_top;
		memcpy(pInstInfoCollection->m_top, pTopInfo, pInstInfoCollection->m_topSize);
	}

	m_prevPhase = m_phase;
	m_phase = Limit01(params.m_stateStartPhase);
	m_remainderTime = 0.0f;

	m_flags.m_transitionsEnabled = true;
	m_flags.m_phaseFrozen = false;
	m_flags.m_phaseFrozenDuringFadeIn = false;
	m_flags.m_freezeFadingOutStates = false;

	m_stateSnapshot.RefreshPhasesAndBlends(m_phase, topState, pInstInfoCollection);

	SetApLocator(params.m_apRef);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateInstance::GetActiveTransitionByName(StringId64 transitionId,
																	   const DC::AnimInfoCollection* pInfoCollection) const
{
	if (m_flags.m_disableAutoTransitions && transitionId == SID("auto"))
		return nullptr;

	const TransitionQueryInfo qi = MakeTransitionQueryInfo(pInfoCollection);
	return AnimStateGetActiveTransitionByName(&m_stateSnapshot.m_animState, transitionId, qi);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstance::IsTransitionValid(StringId64 transitionId, const DC::AnimInfoCollection* pInfoCollection) const
{
	if (m_flags.m_disableAutoTransitions && transitionId == SID("auto"))
		return false;

	const TransitionQueryInfo qi = MakeTransitionQueryInfo(pInfoCollection);
	return AnimStateTransitionValid(&m_stateSnapshot.m_animState, transitionId, qi);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateInstance::GetActiveTransitionByStateName(StringId64 stateId,
																			const DC::AnimInfoCollection* pInfoCollection) const
{
	const TransitionQueryInfo qi = MakeTransitionQueryInfo(pInfoCollection);

	return AnimStateGetActiveTransitionByState(&m_stateSnapshot.m_animState, stateId, qi);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateInstance::GetActiveTransitionByStateFilter(IAnimStateFilter* pFilter,
																			  const DC::AnimInfoCollection* pInfoCollection) const
{
	const TransitionQueryInfo qi = MakeTransitionQueryInfo(pInfoCollection);

	return AnimStateGetActiveTransitionByStateFilter(&m_stateSnapshot.m_animState, pFilter, qi);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstance::FadeUpdate(F32 deltaTime, bool freezePhase)
{
	UpdateAnimFade(deltaTime);
	UpdateMotionFade(deltaTime);

	// If we aren't told to forcefully freeze the phase we check our internal flags
	// If we should be frozen during fade-in and any of the fades are still going we stay frozen.
	m_flags.m_phaseFrozen = freezePhase || m_flags.m_phaseFrozenRequested
							|| (m_flags.m_phaseFrozenDuringFadeIn && (m_animFadeLeft > 0.0f || m_motionFadeLeft > 0.0f));

	// If we are frozen by a newer state we also freeze older states.
	return m_flags.m_freezeFadingOutStates || freezePhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::UpdateMotionFade(float deltaTime)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_disableInstanceFades))
	{
		m_currentMotionFade = 1.0f;
		m_motionFadeLeft	= 0.0f;
		return;
	}

	// Update the fade
	if (m_motionFadeLeft > 0.0f)
	{
		m_motionFadeLeft -= deltaTime;

		float tt = Limit01((m_motionFadeTotal - m_motionFadeLeft) / m_motionFadeTotal);
		m_currentMotionFade = CalculateCurveValue(tt, m_blendType);
	}
	else
	{
		m_currentMotionFade = 1.0f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::UpdateAnimFade(float deltaTime)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_disableInstanceFades))
	{
		m_currentAnimFade = 1.0f;
		m_animFadeLeft	  = 0.0f;
		return;
	}

	// Update the fade
	if (m_animFadeLeft > 0.0f)
	{
		m_animFadeLeft -= deltaTime;

		float tt = Limit01((m_animFadeTotal - m_animFadeLeft) / m_animFadeTotal);
		m_currentAnimFade = CalculateCurveValue(tt, m_blendType);
	}
	else
	{
		m_currentAnimFade = 1.0f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
TransitionQueryInfo AnimStateInstance::MakeTransitionQueryInfo(const DC::AnimInfoCollection* pInfoCollection) const
{
	TransitionQueryInfo qi;

	qi.m_pInfoCollection = pInfoCollection;
	qi.m_pAnimTable = m_pAnimTable;
	qi.m_pOverlaySnapshot = m_pAnimOverlaySnapshot;

	qi.m_phase = Phase();
	qi.m_frame = qi.m_phase * static_cast<float>(m_stateSnapshot.m_phaseAnimFrameCount);
	qi.m_updateRate = m_stateSnapshot.m_updateRate;
	qi.m_stateFade = m_currentAnimFade;

	qi.m_stateId = GetStateName();
	qi.m_phaseAnimId = GetPhaseAnim();
	qi.m_phaseAnimLooping = false;

	if (const ArtItemAnim* pAnim = GetPhaseAnimArtItem().ToArtItem())
	{
		qi.m_phaseAnimLooping = pAnim->m_flags & ArtItemAnim::kLooping;
	}

	if (m_pOwningLayer)
	{
		const AnimStateInstance* pInst = m_pOwningLayer->CurrentStateInstance();
		qi.m_isTopIntance = pInst ? (pInst->GetId() == GetId()) : false;
		qi.m_hasFreeInstance = m_pOwningLayer->HasFreeInstance();
		qi.m_pStateBlendOverlay = m_pOwningLayer->GetStateBlendOverlay();
	}
	else
	{
		qi.m_isTopIntance = true;
		qi.m_hasFreeInstance = true;
		qi.m_pStateBlendOverlay = nullptr;
	}

	return qi;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::ForAllAnimations(AnimationVisitFunc visitFunc, uintptr_t userData) const
{
	if (const AnimSnapshotNode* pRootNode = m_stateSnapshot.GetSnapshotNode(m_stateSnapshot.m_rootNodeIndex))
	{
		pRootNode->ForAllAnimations(&m_stateSnapshot, visitFunc, userData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimStateInstance::GetLayerId() const
{ 
	return m_pOwningLayer ? m_pOwningLayer->GetName() : INVALID_STRING_ID_64; 
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstance::UpdateSavedAlign(const Locator& delta)
{
	ANIM_ASSERT(HasSavedAlign());
	BoundFrame saveAlignWs = GetApLocator();
	
	Locator saveAlignWsNew = saveAlignWs.GetLocator().TransformLocator(delta);
	if (!IsIdentity(saveAlignWsNew.GetRotation()))
	{
		saveAlignWsNew.SetRotation(Normalize(saveAlignWsNew.GetRotation()));
	}
	saveAlignWs.SetLocator(saveAlignWsNew);
	SetSavedAlign(saveAlignWs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateEvalParams::AnimStateEvalParams(const EvaluateChannelParams& params)
{
	m_disableRetargeting = params.m_disableRetargeting;
	m_pCameraCutInfo	 = params.m_pCameraCutInfo;
	m_wantRawScale		 = params.m_wantRawScale;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FadeToStateParams::ApplyBlendParams(const DC::BlendParams* pBlend)
{
	if (!pBlend)
	{
		return;
	}

	if (pBlend->m_animFadeTime >= 0.0f)
		m_animFadeTime = pBlend->m_animFadeTime;
	if (pBlend->m_motionFadeTime >= 0.0f)
		m_motionFadeTime = pBlend->m_motionFadeTime;
	if (pBlend->m_curve != DC::kAnimCurveTypeInvalid)
		m_blendType = pBlend->m_curve;
}
