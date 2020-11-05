/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-snapshot-node-animation.h"

#include "corelib/util/random.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-copy-remap-layer.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-node-joint-limits.h"
#include "ndlib/anim/anim-node-library.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-snapshot-node-blend.h"
#include "ndlib/anim/anim-snapshot-node-instance-zero.h"
#include "ndlib/anim/anim-snapshot-node-unary.h"
#include "ndlib/anim/anim-stat.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/anim/anim-streaming.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/retarget-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/profiling/profile-cpu-categories.h"
#include "ndlib/profiling/profile-cpu.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/anim-overlay-defines.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static SnapshotNodeHeap::Index SnapshotAnimNodeAnimation(AnimStateSnapshot* pSnapshot,
														 const DC::AnimNode* pNode,
														 const SnapshotAnimNodeTreeParams& params,
														 SnapshotAnimNodeTreeResults& results);

ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(AnimSnapshotNodeAnimation,
									  AnimSnapshotNode,
									  SID("anim-node-animation"),
									  SnapshotAnimNodeAnimation);

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeAnimation);

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeAnimation::SnapshotNode(AnimStateSnapshot* pSnapshot,
											 const DC::AnimNode* pNode,
											 const SnapshotAnimNodeTreeParams& params,
											 SnapshotAnimNodeTreeResults& results)
{
	m_phaseMode = PhaseMode::kNormal;

	const DC::AnimNodeAnimation* pAnimNode = static_cast<const DC::AnimNodeAnimation*>(pNode);

	// Retrieve the animation for this node
	StringId64 animationId = pAnimNode->m_animation;

	if (pAnimNode->m_animationFunc)
	{
		DC::AnimInfoCollection scriptInfoCollection(*params.m_pInfoCollection);
		scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
		scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
		scriptInfoCollection.m_top = scriptInfoCollection.m_top;

		const ScriptValue argv[] = { ScriptValue(pAnimNode),
									 ScriptValue(&scriptInfoCollection),
									 ScriptValue(params.m_pAnimTable) };

		const DC::ScriptLambda* pLambda = pAnimNode->m_animationFunc;
		VALIDATE_LAMBDA(pLambda, "anim-node-name-func", m_animState.m_name.m_symbol);
		animationId = ScriptManager::Eval(pLambda, SID("anim-node-name-func"), ARRAY_COUNT(argv), argv).m_stringId;
	}

	bool flipped = false;

	if (pAnimNode->m_flipFunc)
	{
		DC::AnimInfoCollection scriptInfoCollection(*params.m_pInfoCollection);
		scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
		scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
		scriptInfoCollection.m_top = scriptInfoCollection.m_top;

		const ScriptValue argv[] = { ScriptValue(pAnimNode),
									 ScriptValue(&scriptInfoCollection),
									 ScriptValue(params.m_stateFlipped) };

		const DC::ScriptLambda* pLambda = pAnimNode->m_flipFunc;
		VALIDATE_LAMBDA(pLambda, "anim-node-flip-func", m_animState.m_name.m_symbol);
		flipped = ScriptManager::Eval(pLambda, SID("anim-node-flip-func"), ARRAY_COUNT(argv), argv).m_boolean;
	}
	
	const bool detachedPhase = (pAnimNode->m_flags & DC::kAnimNodeAnimationFlagDetachedPhase) != 0;
	const bool globalClockPhase = (pAnimNode->m_flags & DC::kAnimNodeAnimationFlagGlobalClockPhase) != 0;
	const bool randomStartPhase = (pAnimNode->m_flags & DC::kAnimNodeAnimationFlagRandomStartPhase) != 0;
	const bool detachedMirror = (pAnimNode->m_flags & DC::kAnimNodeAnimationFlagDetachedMirror) != 0;

	if (detachedMirror)
	{
		flipped = pSnapshot->IsFlipped() != flipped;
	}

	if (params.m_pFadeToStateParams && params.m_pFadeToStateParams->m_disableAnimReplacement)
	{
		m_noReplaceAnimOverlay = (pAnimNode->m_flags & DC::kAnimNodeAnimationFlagNoReplaceAnimOverlay) != 0;
	}
	else
	{
		m_noReplaceAnimOverlay = false;
	}

	m_origAnimation	= m_animation = animationId;
	m_skelId		= INVALID_SKELETON_ID;
	m_hierarchyId	= 0;
	m_artItemAnimHandle	= ArtItemAnimHandle();
	m_phaseFunc		= pAnimNode->m_phaseFunc;
	m_phase			= randomStartPhase ? RandomFloatRange(0.0f, 1.0f) : 0.0f;
	m_prevPhase		= m_phase;
	m_flipped       = flipped;

	if (detachedPhase)
	{
		m_phaseMode = PhaseMode::kDetached;
	}
	else if (globalClockPhase)
	{
		m_phaseMode = PhaseMode::kGlobalClock;
	}

	// Transform the node by the overlay if needed
	if (params.m_pConstOverlaySnapshot)
	{
		// still done in the snapshot object
	}
	else
	{
		m_animation	  = animationId;
		m_skelId	  = params.m_pAnimTable->GetSkelId();
		m_hierarchyId = params.m_pAnimTable->GetHierarchyId();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeAnimation::RefreshAnims(AnimStateSnapshot* pSnapshot)
{
	ArtItemAnimHandle artItemAnim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, m_animation);

	m_artItemAnimHandle = artItemAnim;

	if (artItemAnim.ToArtItem())
	{
		return true;
	}
	else
	{
		return false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeAnimation::StepNode(AnimStateSnapshot* pSnapshot,
										 float deltaTime,
										 const DC::AnimInfoCollection* pInfoCollection)
{
	const ArtItemAnim* pAnim = m_artItemAnimHandle.ToArtItem();
	const ndanim::ClipData* pClipData = pAnim ? pAnim->m_pClipData : nullptr;

	if (!pClipData)
		return;

	switch (m_phaseMode)
	{
	case PhaseMode::kDetached:
		{
			m_prevPhase = m_phase;

			const float deltaPhase = Limit01(deltaTime * pClipData->m_framesPerSecond * pClipData->m_phasePerFrame);
			m_phase = Fmod(m_phase + deltaPhase);
		}
		break;

	case PhaseMode::kGlobalClock:
		{
			const Clock* pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);

			const float curTimeSec = ToSeconds(pClock->GetCurTime());
			const float animDuration = GetDuration(pAnim);

			m_prevPhase = m_phase;
			m_phase = animDuration > 0.0f ? static_cast<float>(Fmod(curTimeSec / animDuration)) : 0.0f;
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeAnimation::RefreshPhasesAndBlends(AnimStateSnapshot* pSnapshot,
													   float statePhase,
													   bool topTrackInstance,
													   const DC::AnimInfoCollection* pInfoCollection)
{
	if (m_phaseMode != PhaseMode::kNormal)
	{
		return true;
	}

	float phase = statePhase;

	if (m_phaseFunc && m_artItemAnimHandle.ToArtItem())
	{
		// The 'frame count' is 1-based but the 'sample frame' is 0-based
		const ArtItemAnim* pArtItemAnim = m_artItemAnimHandle.ToArtItem();
		const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;
		const float maxFrameSample = static_cast<float>(pClipData->m_numTotalFrames) - 1.0f;

		// We need to be able to assert that the lambda is not larger than this... We need the size of the lambda
		DC::AnimInfoCollection scriptInfoCollection(*pInfoCollection);
		scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
		scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
		scriptInfoCollection.m_top = scriptInfoCollection.m_top;

		const ScriptValue argv[] = { ScriptValue(&scriptInfoCollection),
									 ScriptValue(statePhase),
									 ScriptValue(m_phase),
									 ScriptValue(maxFrameSample) };

		const DC::ScriptLambda* pLambda = m_phaseFunc;
		VALIDATE_LAMBDA(pLambda, "anim-node-phase-func", m_animState.m_name.m_symbol);
		phase = ScriptManager::Eval(pLambda, SID("anim-node-phase-func"), ARRAY_COUNT(argv), argv).m_float;
	}

	m_prevPhase = m_phase;
	m_phase = Limit01(phase);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeAnimation::ShouldHandleFlipInBlend() const
{
	const ArtItemAnim* pArtItemAnim = m_artItemAnimHandle.ToArtItem();
	const bool additive = pArtItemAnim && (pArtItemAnim->m_flags & ArtItemAnim::kAdditive);
	return additive;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeAnimation::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
													 AnimCmdList* pAnimCmdList,
													 I32F outputInstance,
													 const AnimCmdGenInfo* pCmdGenInfo) const
{
	const ArtItemAnim* pArtItemAnim = m_artItemAnimHandle.ToArtItem();
	
	const float nodePhase = m_phase;
	float clipPhase = nodePhase;

	if (pArtItemAnim && (pArtItemAnim->m_flags & ArtItemAnim::kStreaming))
	{
		AnimStreamNotifyUsage(pArtItemAnim, m_animation, nodePhase);
		clipPhase = AnimStreamGetChunkPhase(pArtItemAnim, m_animation, nodePhase);
		pArtItemAnim = AnimStreamGetArtItem(pArtItemAnim, m_animation, nodePhase);
	}

	if (pArtItemAnim)
	{
		// The 'frame count' is 1-based but the 'sample frame' is 0-based
		const float maxFrameSample = static_cast<float>(pArtItemAnim->m_pClipData->m_numTotalFrames) - 1.0f;
		float sample = clipPhase * maxFrameSample;

		if (pCmdGenInfo->m_cameraCutThisFrame)
		{
			sample = ceilf(sample);
		}

		pAnimCmdList->AddCmd_EvaluateClip(pArtItemAnim, outputInstance, sample);

		if (m_flipped && !ShouldHandleFlipInBlend())
		{
			pAnimCmdList->AddCmd_EvaluateFlip(outputInstance);
		}
	}
	else
	{
		pAnimCmdList->AddCmd_EvaluateBindPose(outputInstance);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::ValidBits AnimSnapshotNodeAnimation::GetValidBits(const ArtItemSkeleton* pSkel,
														  const AnimStateSnapshot* pSnapshot,
														  U32 iGroup) const
{
	if (!m_artItemAnimHandle.ToArtItem())
	{
		return ndanim::ValidBits(0ULL, 0ULL);
	}

	const ArtItemAnim* pArtItemAnim = m_artItemAnimHandle.ToArtItem();
	if (pArtItemAnim->m_flags & ArtItemAnim::kStreaming)
	{
		//Valid bits don't change during a stream so only grab the first chunk
		pArtItemAnim = AnimStreamGetArtItemFirstChunk(pArtItemAnim, m_animation);
	}

	return GetRetargetedValidBits(pSkel, pArtItemAnim, iGroup);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeAnimation::IsAdditive(const AnimStateSnapshot* pSnapshot) const
{
	bool additive = false;

	if (m_artItemAnimHandle.ToArtItem())
	{
		const ndanim::ClipData* pClipData = m_artItemAnimHandle.ToArtItem()->m_pClipData;
		if (pClipData->m_clipFlags & ndanim::kClipAdditive)
		{
			additive = true;
		}
	}

	return additive;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeAnimation::HasErrors(const AnimStateSnapshot* pSnapshot) const
{
	return m_artItemAnimHandle.ToArtItem() == nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeAnimation::HasLoopingAnimation(const AnimStateSnapshot* pSnapshot) const
{
	const ArtItemAnim* pArtItemAnim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, m_animation).ToArtItem();
	if (pArtItemAnim)
	{
		if (pArtItemAnim->m_flags & ArtItemAnim::kLooping)
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimSnapshotNodeAnimation::AddTriggeredEffectsForAnim(const ArtItemAnim* pArtItemAnim,
														   EffectUpdateStruct& effectParams,
														   float nodeBlend,
														   const AnimInstance* pInstance,
														   StringId64 stateId)
{
	const EffectAnim* pEffectAnim = pArtItemAnim ? pArtItemAnim->m_pEffectAnim : nullptr;

	if (!pEffectAnim)
		return;

	const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;
	// The 'frame count' is 1-based but the 'sample frame' is 0-based
	const float maxFrameSample = static_cast<float>(pClipData->m_numTotalFrames) - 1.0f;
	const float mayaFramesCompensate = 30.0f * pClipData->m_secondsPerFrame;
	const float maxMayaFrameIndex = maxFrameSample * mayaFramesCompensate;

	const bool looping = (pArtItemAnim->m_flags & ArtItemAnim::kLooping);

	const I32F startSize = effectParams.m_pTriggeredEffects->GetNumEffects();

	EffectGroup::GetTriggeredEffects(pEffectAnim,
									 effectParams.m_oldPhase * maxMayaFrameIndex,
									 effectParams.m_newPhase * maxMayaFrameIndex,
									 maxMayaFrameIndex,
									 !effectParams.m_isReversed,
									 looping,
									 effectParams.m_isFlipped,
									 effectParams.m_isTopState,
									 nodeBlend * effectParams.m_stateEffectiveFade,
									 effectParams.m_minBlend,
									 stateId,
									 effectParams.m_pTriggeredEffects,
									 pInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeAnimation::GetTriggeredEffects(const AnimStateSnapshot* pSnapshot,
													EffectUpdateStruct& effectParams,
													float nodeBlend,
													const AnimInstance* pInstance) const
{
	EffectUpdateStruct paramsCopy = effectParams; // should be okay, since our output is via a pointer

	if (m_phaseMode != PhaseMode::kNormal)
	{
		paramsCopy.m_oldPhase = m_prevPhase;
		paramsCopy.m_newPhase = m_phase;
		paramsCopy.m_isReversed = m_phase < m_prevPhase;
	}

	AddTriggeredEffectsForAnim(m_artItemAnimHandle.ToArtItem(),
							   paramsCopy,
							   nodeBlend,
							   pInstance,
							   pSnapshot->m_animState.m_name.m_symbol);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeAnimation::EvaluateFloatChannel(const AnimStateSnapshot* pSnapshot,
													 float* pOutChannelFloat,
													 const SnapshotEvaluateParams& evaluateParams) const
{
	bool channelEvaluated = false;

	if (m_artItemAnimHandle.ToArtItem())
	{
		ANIM_ASSERT(m_artItemAnimHandle.ToArtItem()->m_versionNumber < 1000);

		EvaluateChannelParams channelParams;
		channelParams.m_pAnim		  = m_artItemAnimHandle.ToArtItem();
		channelParams.m_channelNameId = evaluateParams.m_channelName;
		channelParams.m_phase		  = m_phase;
		channelParams.m_mirror		  = evaluateParams.m_flipped;
		channelParams.m_wantRawScale  = evaluateParams.m_wantRawScale;
		channelParams.m_pCameraCutInfo	   = evaluateParams.m_pCameraCutInfo;
		channelParams.m_disableRetargeting = evaluateParams.m_disableRetargeting;

		channelEvaluated = EvaluateCompressedFloatChannel(&channelParams, pOutChannelFloat);
	}

	if (!channelEvaluated)
	{
		*pOutChannelFloat = 1.0f;
	}

	return channelEvaluated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeAnimation::EvaluateChannel(const AnimStateSnapshot* pSnapshot,
												ndanim::JointParams* pOutChannelJoint,
												const SnapshotEvaluateParams& inputParams) const
{
	bool channelEvaluated = false;

	const ArtItemAnim* pArtItemAnim = m_artItemAnimHandle.ToArtItem();
	
	if (inputParams.m_pRemapLayer)
	{
		pArtItemAnim = inputParams.m_pRemapLayer->GetRemappedArtItem(pArtItemAnim, 0);
	}

	if (pArtItemAnim && pArtItemAnim->m_pCompressedChannelList
		&& pArtItemAnim->m_pCompressedChannelList->m_numChannels > 0)
	{
		ANIM_ASSERT(pArtItemAnim->m_versionNumber < 1000);

		EvaluateChannelParams channelParams;
		channelParams.m_pAnim = pArtItemAnim;
		channelParams.m_channelNameId = inputParams.m_channelName;
		channelParams.m_phase		  = m_phase;
		channelParams.m_mirror		  = inputParams.m_flipped;
		channelParams.m_wantRawScale  = inputParams.m_wantRawScale;
		channelParams.m_pCameraCutInfo	   = inputParams.m_pCameraCutInfo;
		channelParams.m_disableRetargeting = inputParams.m_disableRetargeting;

		if (m_flipped && (pSnapshot->m_animState.m_flags & DC::kAnimStateFlagSwapMirroredChannelPairs) != 0)
		{
			channelParams.m_mirror = m_flipped != channelParams.m_mirror;
			channelParams.m_channelNameId = LookupMirroredChannelPair(inputParams.m_channelName);
		}

		channelEvaluated = EvaluateChannelInAnim(m_skelId, &channelParams, pOutChannelJoint);

		const bool channelSwapped = (channelParams.m_channelNameId != inputParams.m_channelName);
		if (channelSwapped)
		{
			pOutChannelJoint->m_quat = RotateSwappedChannelPair(pOutChannelJoint->m_quat);
		}
	}

	if (!channelEvaluated)
	{
		pOutChannelJoint->m_scale = Vector(Scalar(1.0f));
		pOutChannelJoint->m_quat  = Quat(kIdentity);
		pOutChannelJoint->m_trans = Point(kOrigin);
	}

	return channelEvaluated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeAnimation::EvaluateChannelDelta(const AnimStateSnapshot* pSnapshot,
													 ndanim::JointParams* pOutChannelJoint,
													 const SnapshotEvaluateParams& inputParams) const
{
	bool channelEvaluated = false;

	if (m_artItemAnimHandle.ToArtItem() && (m_artItemAnimHandle.ToArtItem()->m_pCompressedChannelList->m_numChannels > 0))
	{
		ANIM_ASSERT(m_artItemAnimHandle.ToArtItem()->m_versionNumber < 1000);

		ndanim::JointParams outChannelJoint[2];
		{
			EvaluateChannelParams channelParams;
			channelParams.m_pAnim = m_artItemAnimHandle.ToArtItem();
			channelParams.m_channelNameId = inputParams.m_channelName;
			channelParams.m_phase = inputParams.m_statePhasePre;
			channelParams.m_mirror = inputParams.m_flipped;
			channelParams.m_wantRawScale = inputParams.m_wantRawScale;

			channelEvaluated = EvaluateChannelInAnim(m_skelId, &channelParams, &outChannelJoint[0]);
		}
		{
			EvaluateChannelParams channelParams;
			channelParams.m_pAnim = m_artItemAnimHandle.ToArtItem();
			channelParams.m_channelNameId = inputParams.m_channelName;
			channelParams.m_phase = inputParams.m_statePhase;
			channelParams.m_mirror = inputParams.m_flipped;
			channelParams.m_wantRawScale = inputParams.m_wantRawScale;

			channelEvaluated = EvaluateChannelInAnim(m_skelId, &channelParams, &outChannelJoint[1]);
		}
		if (channelEvaluated)
		{
			const Locator preAlign = Locator(outChannelJoint[0].m_trans, outChannelJoint[0].m_quat);
			const Locator postAlign = Locator(outChannelJoint[1].m_trans, outChannelJoint[1].m_quat);

			const Quat preAlignConjugate = Conjugate(preAlign.Rot());
			const Vector deltaTranslation = Rotate(preAlignConjugate, postAlign.Pos() - preAlign.Pos());
			const Quat deltaRotation = postAlign.Rot() * preAlignConjugate;

			pOutChannelJoint->m_scale = Vector(Scalar(1.0f));
			pOutChannelJoint->m_quat  = deltaRotation;
			pOutChannelJoint->m_trans = AsPoint(deltaTranslation);
		}
	}

	if (!channelEvaluated)
	{
		pOutChannelJoint->m_scale = Vector(Scalar(1.0f));
		pOutChannelJoint->m_quat  = Quat(kIdentity);
		pOutChannelJoint->m_trans = Point(kOrigin);
	}

	return channelEvaluated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeAnimation::CollectContributingAnims(const AnimStateSnapshot* pSnapshot,
														 float blend,
														 AnimCollection* pCollection) const
{
	if (blend > 0.001f && (pCollection->m_animCount < AnimIdCollection::kMaxAnimIds))
	{
		const ArtItemAnim* pAnim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, m_animation).ToArtItem();

		if (pAnim)
		{
			pCollection->m_animArray[pCollection->m_animCount] = pAnim;
			++pCollection->m_animCount;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeAnimation::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	STRIP_IN_FINAL_BUILD;

	const StringId64 origAnimName = m_origAnimation;
	const ArtItemAnim* pArtItemAnim = m_artItemAnimHandle.ToArtItem();

	if (pArtItemAnim)
	{
		const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;
		const float maxFrameSample = pClipData->m_fNumFrameIntervals; // NB: we don't use pClipData->m_numTotalFrames as this is really m_numTotalSAMPLES and is one more than the number of frame intervals
		const float mayaFramesCompensate = 30.0f * pClipData->m_secondsPerFrame;

		const ArtItemSkeleton* pAnimSkel = pData->m_pAnimData->m_animateSkelHandle.ToArtItem();
		const bool isRetargeted = pAnimSkel && pAnimSkel->m_hierarchyId != pArtItemAnim->m_pClipData->m_animHierarchyId;
		const SkelTable::RetargetEntry* pRetargetEntry = SkelTable::LookupRetarget(pArtItemAnim->m_skelID, pData->m_pAnimTable->GetSkelId());
		const bool slowRetarget = (pRetargetEntry == nullptr) || (pRetargetEntry->m_disabled != 0);
		
		U32 selectedColor = 0xFFFF0000;
		if (pData->m_additiveLayer && !(pArtItemAnim->m_flags & ArtItemAnim::kAdditive))
		{
			selectedColor = 0xFFFF0000;
		}
		else if (isRetargeted)
		{
			if (slowRetarget)
			{
				U8 redComponentValue = static_cast<U8>(64.0f * pData->m_parentBlendValue);
				U8 blueComponentValue = static_cast<U8>(255.0f * pData->m_parentBlendValue);
				if (pData->m_isBaseLayer && !(pArtItemAnim->m_flags & ArtItemAnim::kAdditive))
				{
					redComponentValue = static_cast<U8>(32.0f * pData->m_parentBlendValue);
					blueComponentValue = static_cast<U8>(255.0f * pData->m_parentBlendValue);
				}

				selectedColor = 0xFF555555 | redComponentValue | blueComponentValue << 16;
			}
			else
			{
				U8 redComponentValue = static_cast<U8>(255.0f * pData->m_parentBlendValue);
				U8 blueComponentValue = static_cast<U8>(255.0f * pData->m_parentBlendValue);
				if (pData->m_isBaseLayer && !(pArtItemAnim->m_flags & ArtItemAnim::kAdditive))
				{
					redComponentValue = static_cast<U8>(128.0f * pData->m_parentBlendValue);
					blueComponentValue = static_cast<U8>(255.0f * pData->m_parentBlendValue);
				}

				selectedColor = 0xFF555555 | redComponentValue | blueComponentValue << 16;
			}
		}
		else
		{
			U8 greenComponentValue = static_cast<U8>(255.0f * pData->m_parentBlendValue);
			U8 blueComponentValue = static_cast<U8>(255.0f * pData->m_parentBlendValue);
			if (pData->m_isBaseLayer && !(pArtItemAnim->m_flags & ArtItemAnim::kAdditive))
			{
				greenComponentValue = static_cast<U8>(128.0f * pData->m_parentBlendValue);
				blueComponentValue = static_cast<U8>(255.0f * pData->m_parentBlendValue);
			}

			selectedColor = 0xFF555555 | greenComponentValue << 8 | blueComponentValue << 16;
		}

		const ArtItemAnim* pStreamArtItemAnim = pArtItemAnim;
		if (pArtItemAnim->m_flags & ArtItemAnim::kStreaming)
		{
			pStreamArtItemAnim = AnimStreamGetArtItem(pArtItemAnim, m_animation, m_phase);
		}

		const char* skelName = ResourceTable::LookupSkelName(pArtItemAnim->m_skelID);
		char retargetSourceSkelName[128];
		sprintf(&retargetSourceSkelName[0], "[%s]", skelName);

		const char* origName = DevKitOnly_StringIdToString(origAnimName);
		
		if (!g_animOptions.m_debugPrint.m_simplified)
		{
			Tab(pData->m_output, pData->m_depth);
			SetColor(pData->m_output, selectedColor);
			const char *animName = (pStreamArtItemAnim == nullptr) ? "missing-stream-chunk" : pStreamArtItemAnim->GetName();
			if (pData->m_pRemapLayer)
			{
				StringId64 remapAnim = pData->m_pRemapLayer->RemapAnimation(StringToStringId64(animName), 0);
				animName = DevKitOnly_StringIdToString(remapAnim);
			}

			PrintTo(pData->m_output, "Animation: \"%s\"", animName);

			if (pStreamArtItemAnim && origName && (origAnimName != pStreamArtItemAnim->GetNameId()))
			{
				SetColor(pData->m_output, 0xFF999999);
				PrintTo(pData->m_output, " [%s]", origName);
			}
			if (m_flipped)
			{
				SetColor(pData->m_output, selectedColor);
				PrintTo(pData->m_output, " (flip%s)", ShouldHandleFlipInBlend() ? " in blend" : "");
			}
			SetColor(pData->m_output, selectedColor);
			PrintTo(pData->m_output,
					" %s%s%s frame: %1.2f/%1.2f ",
					isRetargeted ? retargetSourceSkelName : "",
					(isRetargeted && slowRetarget) ? " [SLOW!]" : "",
					pArtItemAnim->m_flags & ArtItemAnim::kAdditive ? "(add)" : "",
					m_phase * maxFrameSample * mayaFramesCompensate,
					maxFrameSample * mayaFramesCompensate);
			if (g_animOptions.m_debugPrint.m_showPakFileNames && pArtItemAnim && pArtItemAnim->m_pDebugOnlyPakName)
			{
				PrintTo(pData->m_output, "pak: %s ", pArtItemAnim->m_pDebugOnlyPakName);
			}

			if (pArtItemAnim->m_flags & ArtItemAnim::kStreaming)
			{
				const float chunkPhase = AnimStreamGetChunkPhase(pArtItemAnim, m_animation, m_phase);
				PrintTo(pData->m_output, "[chunk-phase: %0.3f] ", chunkPhase);
			}

			const bool skewedSampling = pArtItemAnim->m_flags & ArtItemAnim::kSkewedSampling;
			if (skewedSampling)
			{
				SetColor(pData->m_output, 0xFF00D4ED);
			}

			PrintTo(pData->m_output, "(%.0f FPS%s%s%s)\n",
				pClipData->m_framesPerSecond,
				skewedSampling ? " Skewed Sampling" : "",
				(m_phaseMode == PhaseMode::kNormal) ? "" : " ",
				(m_phaseMode == PhaseMode::kNormal) ? "" : GetPhaseModeStr(m_phaseMode));
		}
		else
		{
			Tab(pData->m_output, pData->m_depth);
			SetColor(pData->m_output, selectedColor);
			const char* animName = (pStreamArtItemAnim == nullptr) ? "missing-stream-chunk"
																   : pStreamArtItemAnim->GetName();
			if (pData->m_pRemapLayer)
			{
				StringId64 remapAnim = pData->m_pRemapLayer->RemapAnimation(StringToStringId64(animName), 0);
				animName = DevKitOnly_StringIdToString(remapAnim);
			}
			PrintTo(pData->m_output, "Animation: \"%s\"", animName);
			PrintTo(pData->m_output,
					" frame: %1.2f/%1.2f phase: %0.2f",
					m_phase * maxFrameSample * mayaFramesCompensate,
					maxFrameSample * mayaFramesCompensate,
					m_phase);
			if (g_animOptions.m_debugPrint.m_useEffectiveFade)
			{
				PrintTo(pData->m_output, " [eff. fade: %1.2f]", pData->m_parentBlendValue);
			}
			PrintTo(pData->m_output, "\n");
		}
	}
	else if (m_origAnimation == INVALID_STRING_ID_64)
	{
		SetColor(pData->m_output, kColorYellow.ToAbgr8());
		Tab(pData->m_output, pData->m_depth);
		PrintTo(pData->m_output, "Animation: INVALID_STRING_ID_64\n");
	}
	else
	{
		SetColor(pData->m_output, 0xFF0000FF);
		const char* name = DevKitOnly_StringIdToString(m_animation);
		const char* origName = DevKitOnly_StringIdToString(m_origAnimation);

		Tab(pData->m_output, pData->m_depth);
		PrintTo(pData->m_output, "Animation: \"%s [%s]\"  - MISSING\n", name, origName ? origName : "");
	}
	SetColor(pData->m_output, kColorWhite.ToAbgr8());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeAnimation::DebugSubmitAnimPlayCount(const AnimStateSnapshot* pSnapshot) const
{
	STRIP_IN_FINAL_BUILD;

	g_animStat.SubmitPlayCount(m_skelId, m_animation, 1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAnimDataEval::IAnimData* AnimSnapshotNodeAnimation::EvaluateNode(IAnimDataEval* pEval, const AnimStateSnapshot* pSnapshot) const
{
	return pEval->EvaluateDataFromAnim(m_artItemAnimHandle.ToArtItem(), m_phase, pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeAnimation::ForAllAnimationsInternal(const AnimStateSnapshot* pSnapshot,
														 AnimationVisitFunc visitFunc,
														 float combinedBlend,
														 uintptr_t userData) const
{
	if (m_artItemAnimHandle.ToArtItem() && visitFunc)
	{
		visitFunc(m_artItemAnimHandle.ToArtItem(), pSnapshot, this, combinedBlend, m_phase, userData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index TransformAnimNodeAnimation(AnimStateSnapshot* pSnapshot,
												   SnapshotNodeHeap::Index nodeIndex,
												   AnimOverlayIterator startingOverlayIterator,
												   const SnapshotAnimNodeTreeParams& params,
												   SnapshotAnimNodeTreeResults& results);

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ApplyOverlayTransform_Blend(const DC::AnimOverlaySetEntry* pSetEntry,
										AnimStateSnapshot* pSnapshot,
										SnapshotNodeHeap::Index nodeIndex,
										AnimOverlayIterator& overlayItr,
										const SnapshotAnimNodeTreeParams& params,
										AnimSnapshotNodeAnimation* pAnimationNode,
										SnapshotNodeHeap::Index& returnNodeIndex,
										SnapshotAnimNodeTreeResults& results)
{
	SnapshotNodeHeap* pNodeHeap = pSnapshot->m_pSnapshotHeap;
	AnimSnapshotNodeBlend* pNewBlendNode = pNodeHeap->AllocateNode<AnimSnapshotNodeBlend>(&returnNodeIndex);

	if (!pNewBlendNode)
	{
		MsgAnimErr("[%s] Ran out of snapshot memory adding overlay blend '%s' to '%s' (state '%s')\n",
				   DevKitOnly_StringIdToString(params.m_pAnimData->m_hProcess.GetUserId()),
				   DevKitOnly_StringIdToString(pSetEntry->m_remapId),
				   DevKitOnly_StringIdToString(pSetEntry->m_sourceId),
				   pSnapshot->m_animState.m_name.m_string.GetString());

		returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
		return true;
	}

	pNewBlendNode->m_featherBlendIndex = g_featherBlendTable.LoginFeatherBlend(pSetEntry->m_featherBlend,
																			   params.m_pAnimData);
	pNewBlendNode->m_blendFactor = 1.0f;
	pNewBlendNode->m_blendFunc	 = pSetEntry->m_blendFunc;
	pNewBlendNode->m_flags		 = AnimOverlayExtraBlendFlags(pSetEntry->m_flags);

	const AnimSnapshotNodeBlend* pSavedParent = params.m_pParentBlendNode;
	params.m_pParentBlendNode = pNewBlendNode;

	if (pSetEntry->m_flags & DC::kAnimOverlayFlagsBlendOverZero)
	{
		// special case where we went to blend over instance 0
		// left node is just instance 0
		SnapshotNodeHeap::Index leftAnimSnapshotIndex;
		if (AnimSnapshotNodeInstanceZero* pNewZeroNode = pNodeHeap->AllocateNode<AnimSnapshotNodeInstanceZero>(&leftAnimSnapshotIndex))
		{
			pNewBlendNode->m_leftIndex = leftAnimSnapshotIndex;
		}
		else
		{
			pNewBlendNode->m_leftIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
		}

		// a better fix (after ship) is to fix the macros so we don't have to undo the work done in AnimOverlayExtraBlendFlags()
		pNewBlendNode->m_flags |= DC::kAnimNodeBlendFlagEvaluateBothNodes;
		pNewBlendNode->m_flags &= ~DC::kAnimNodeBlendFlagEvaluateLeftNodeOnly;

		// right node
		// just point the left index to our existing node, and continue the overlay iteration
		pNewBlendNode->m_rightIndex = TransformAnimNodeAnimation(pSnapshot, nodeIndex, overlayItr, params, results);
	}
	else
	{
		// left node
		// just point the left index to our existing node, and continue the overlay iteration
		pNewBlendNode->m_leftIndex = TransformAnimNodeAnimation(pSnapshot, nodeIndex, overlayItr, params, results);

		// right node
		SnapshotNodeHeap::Index rightAnimSnapshotIndex;
		if (AnimSnapshotNodeAnimation* pNewAnimNode = pNodeHeap->AllocateNode<AnimSnapshotNodeAnimation>(&rightAnimSnapshotIndex))
		{
			const bool detachedPhase	= (pSetEntry->m_flags & DC::kAnimOverlayFlagsDetachedPhase) != 0;
			const bool globalClockPhase = (pSetEntry->m_flags & DC::kAnimOverlayFlagsGlobalClockPhase) != 0;
			const bool randomStartPhase = (pSetEntry->m_flags & DC::kAnimOverlayFlagsRandomStartPhase) != 0;
			const bool detachedMirror	= (pSetEntry->m_flags & DC::kAnimOverlayFlagsDetachedMirror) != 0;

			DC::AnimNodeAnimationFlag flags = 0;
			if (detachedPhase)
				flags |= DC::kAnimNodeAnimationFlagDetachedPhase;
			if (globalClockPhase)
				flags |= DC::kAnimNodeAnimationFlagGlobalClockPhase;
			if (randomStartPhase)
				flags |= DC::kAnimNodeAnimationFlagRandomStartPhase;
			if (detachedMirror)
				flags |= DC::kAnimNodeAnimationFlagDetachedMirror;

			DC::AnimNodeAnimation fauxDcAnimNode;
			memset(&fauxDcAnimNode, 0, sizeof(DC::AnimNodeAnimation));
			fauxDcAnimNode.m_animation = pSetEntry->m_remapId;
			fauxDcAnimNode.m_phaseFunc = pAnimationNode->m_phaseFunc;
			fauxDcAnimNode.m_flipFunc = pSetEntry->m_flipFunc;
			fauxDcAnimNode.m_flags = flags;

			pNewAnimNode->SnapshotNode(pSnapshot, &fauxDcAnimNode, params, results);

			AnimOverlayIterator freshIterator = AnimOverlayIterator(pSetEntry->m_remapId);

			pNewBlendNode->m_rightIndex = TransformAnimNodeAnimation(pSnapshot,
																	 rightAnimSnapshotIndex,
																	 freshIterator,
																	 params,
																	 results);
		}
		else
		{
			MsgAnimErr("[%s] Ran out of snapshot memory adding overlay blend '%s' to '%s' (state '%s')\n",
					   DevKitOnly_StringIdToString(params.m_pAnimData->m_hProcess.GetUserId()),
					   DevKitOnly_StringIdToString(pSetEntry->m_remapId),
					   DevKitOnly_StringIdToString(pSetEntry->m_sourceId),
					   pSnapshot->m_animState.m_name.m_string.GetString());

			pNewBlendNode->m_rightIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
		}
	}

	if (pNewBlendNode->m_flags & DC::kAnimNodeBlendFlagApplyJointLimits)
	{
		SnapshotNodeHeap::Index parentNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;

		if (AnimNodeJointLimits* pJointLimitsNode = pNodeHeap->AllocateNode<AnimNodeJointLimits>(&parentNodeIndex))
		{
			NdGameObjectHandle hGo = params.m_pAnimData->m_hProcess.CastHandleType<MutableNdGameObjectHandle>();
			pJointLimitsNode->SetCustomLimits(hGo,pNewBlendNode->m_customLimitsId);
			pJointLimitsNode->SetChildIndex(returnNodeIndex);
			returnNodeIndex = parentNodeIndex;
		}
	}

	results.m_newStateFlags |= AnimOverlayExtraStateFlags(pSetEntry->m_flags);

	params.m_pParentBlendNode = pSavedParent;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ApplyOverlayTransform_Tree(const DC::AnimOverlaySetEntry* pSetEntry,
									   AnimStateSnapshot* pSnapshot,
									   SnapshotNodeHeap::Index nodeIndex,
									   const SnapshotAnimNodeTreeParams& params,
									   AnimOverlayIterator& overlayItr,
									   AnimSnapshotNodeAnimation* pAnimationNode,
									   SnapshotNodeHeap::Index& returnNodeIndex,
									   SnapshotAnimNodeTreeResults& results)
{
	SnapshotNodeHeap* pNodeHeap = pSnapshot->m_pSnapshotHeap;

	// move the old anim node into the left side of a blend
	AnimSnapshotNodeAnimation oldAnimNodeAnim = *pAnimationNode;

	// Convert the existing anim node to a blend node
	if (AnimSnapshotNodeBlend* pNewBlendNode = pNodeHeap->AllocateNode<AnimSnapshotNodeBlend>(&returnNodeIndex))
	{
		pNewBlendNode->m_featherBlendIndex = g_featherBlendTable.LoginFeatherBlend(pSetEntry->m_featherBlend,
																				   params.m_pAnimData);
		pNewBlendNode->m_blendFactor = 1.0f;
		pNewBlendNode->m_blendFunc = pSetEntry->m_blendFunc;
		pNewBlendNode->m_flags = AnimOverlayExtraBlendFlags(pSetEntry->m_flags);

		const AnimSnapshotNodeBlend* pSavedParent = params.m_pParentBlendNode;
		params.m_pParentBlendNode = pNewBlendNode;

		pNewBlendNode->m_leftIndex = TransformAnimNodeAnimation(pSnapshot, nodeIndex, overlayItr, params, results);

		// and then apply any overlay transforms to the tree here
		pNewBlendNode->m_rightIndex = AnimStateSnapshot::SnapshotAnimNodeTree(pSnapshot,
																			  pSetEntry->m_tree,
																			  params,
																			  results);

		params.m_pParentBlendNode = pSavedParent;

		if (AnimSnapshotNode* pLeftNode = pNodeHeap->GetNodeByIndex(pNewBlendNode->m_leftIndex))
		{
			pLeftNode->OnAddedToBlendNode(params, pSnapshot, pNewBlendNode, true);
		}

		if (AnimSnapshotNode* pRightNode = pNodeHeap->GetNodeByIndex(pNewBlendNode->m_rightIndex))
		{
			pRightNode->OnAddedToBlendNode(params, pSnapshot, pNewBlendNode, false);
		}

		if (pNewBlendNode->m_flags & DC::kAnimNodeBlendFlagApplyJointLimits)
		{
			SnapshotNodeHeap::Index parentNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;

			if (AnimNodeJointLimits* pJointLimitsNode = pNodeHeap->AllocateNode<AnimNodeJointLimits>(&parentNodeIndex))
			{
				NdGameObjectHandle hGo = params.m_pAnimData->m_hProcess.CastHandleType<MutableNdGameObjectHandle>();
				pJointLimitsNode->SetCustomLimits(hGo, pNewBlendNode->m_customLimitsId);
				pJointLimitsNode->SetChildIndex(returnNodeIndex);
				returnNodeIndex = parentNodeIndex;
			}
		}

		results.m_newStateFlags |= AnimOverlayExtraStateFlags(pSetEntry->m_flags);
	}
	else
	{
		MsgAnimErr("[%s] Ran out of snapshot memory adding overlay tree to '%s' (state '%s')\n",
				   DevKitOnly_StringIdToString(params.m_pAnimData->m_hProcess.GetUserId()),
				   DevKitOnly_StringIdToString(pSetEntry->m_sourceId),
				   pSnapshot->m_animState.m_name.m_string.GetString());

		returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ApplyOverlayTransform_Unary(const DC::AnimOverlaySetEntry* pSetEntry,
										AnimStateSnapshot* pSnapshot,
										SnapshotNodeHeap::Index nodeIndex,
										const SnapshotAnimNodeTreeParams& params,
										AnimOverlayIterator& overlayItr,
										AnimSnapshotNodeAnimation* pAnimationNode,
										SnapshotNodeHeap::Index& returnNodeIndex,
										SnapshotAnimNodeTreeResults& results)
{
	SnapshotNodeHeap* pNodeHeap = pSnapshot->m_pSnapshotHeap;

	const DC::AnimNode* pDcNode = pSetEntry->m_tree;
	const StringId64 typeId = g_animNodeLibrary.LookupTypeIdFromDcType(pDcNode->m_dcType);
	const TypeFactory::Record* pRec = g_typeFactory.GetRecord(typeId);

	if (!pRec || !pRec->IsKindOf(g_type_AnimSnapshotNodeUnary))
	{
		returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
		return true;
	}

	returnNodeIndex = AnimStateSnapshot::SnapshotAnimNodeTree(pSnapshot, pDcNode, params, results);

	AnimSnapshotNode* pNewNode = pNodeHeap->GetNodeByIndex(returnNodeIndex);

	if (AnimSnapshotNodeUnary* pNewUnaryNode = AnimSnapshotNodeUnary::FromAnimNode(pNewNode))
	{
		SnapshotNodeHeap::Index childIndex = TransformAnimNodeAnimation(pSnapshot,
																		nodeIndex,
																		overlayItr,
																		params,
																		results);
		pNewUnaryNode->SetChildIndex(childIndex);
	}
	else
	{
		MsgAnimErr("[%s] Ran out of snapshot memory adding overlay unary to '%s' (state '%s')\n",
				   DevKitOnly_StringIdToString(params.m_pAnimData->m_hProcess.GetUserId()),
				   DevKitOnly_StringIdToString(pSetEntry->m_sourceId),
				   pSnapshot->m_animState.m_name.m_string.GetString());

		if (pNewNode && (returnNodeIndex != SnapshotNodeHeap::kOutOfMemoryIndex))
		{
			pNodeHeap->ReleaseNode(returnNodeIndex);
			pNewNode = nullptr;

			returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ApplyOverlayTransform_Random(const DC::AnimOverlaySetEntry* pSetEntry,
										 const SnapshotAnimNodeTreeParams& params,
										 AnimOverlayIterator& overlayItr,
										 AnimSnapshotNodeAnimation* pAnimationNode,
										 SnapshotAnimNodeTreeResults& results)
{
	if (pAnimationNode->m_noReplaceAnimOverlay)
	{
		return true;
	}

	const DC::AnimOverlaySetEntry* variantList[16];
	U32F numVariants = 0;
	const DC::AnimOverlaySetEntry* pNextEntry = pSetEntry;
	const StringId64 sourceId  = overlayItr.GetSourceId();
	const StringId64 variantId = pSetEntry->m_variantGroup;
	U32 incrementGroupIndex	   = pSetEntry->m_flags & DC::kAnimOverlayFlagsChooseVariantIndex;
	do
	{
		ANIM_ASSERT(pNextEntry->m_variantGroup == variantId);

		variantList[numVariants++] = pNextEntry;
		ANIM_ASSERT(numVariants <= ARRAY_ELEMENT_COUNT(variantList));

		incrementGroupIndex |= pSetEntry->m_flags & DC::kAnimOverlayFlagsChooseVariantIndex;

		overlayItr = params.m_pConstOverlaySnapshot->GetNextEntry(overlayItr);
		pNextEntry = overlayItr.GetEntry();

		if (!pNextEntry)
		{
			break;
		}

		if (pNextEntry->m_type != pSetEntry->m_type || pNextEntry->m_variantGroup != variantId)
		{
			break;
		}

	} while (numVariants < ARRAY_ELEMENT_COUNT(variantList));

	U32F selectedIndex = 0;
	if (params.m_disableRandomization)
	{
		// leave at 0
	}
	else if (pSetEntry->m_type == DC::kAnimOverlayTypeInstanceVariant)
	{
		selectedIndex = params.m_pConstOverlaySnapshot->GetInstanceSeed();
	}
	else if (variantId != INVALID_STRING_ID_64 && variantId != sourceId)
	{
		if (incrementGroupIndex && params.m_pMutableOverlaySnapshot)
		{
			const bool randomize = (pSetEntry->m_type == DC::kAnimOverlayTypeRandom);
			selectedIndex = params.m_pMutableOverlaySnapshot->GetVariantIndexAndIncrement(variantId, numVariants, randomize);
		}
		else
		{
			selectedIndex = params.m_pConstOverlaySnapshot->GetVariantIndex(variantId);
		}
	}
	else if (params.m_pMutableOverlaySnapshot)
	{
		const bool randomize = (pSetEntry->m_type == DC::kAnimOverlayTypeRandom);
		selectedIndex = params.m_pMutableOverlaySnapshot->GetVariantIndexAndIncrement(sourceId, numVariants, randomize);
	}
	else
	{
		selectedIndex = params.m_pConstOverlaySnapshot->GetVariantIndex(sourceId);
	}

	selectedIndex = selectedIndex % numVariants;
	ANIM_ASSERT(selectedIndex < numVariants);

	const DC::AnimOverlaySetEntry* pSelectedRemap = variantList[selectedIndex];

	results.m_newStateFlags |= AnimOverlayExtraStateFlags(pSelectedRemap->m_flags);

	pAnimationNode->m_animation = pSelectedRemap->m_remapId;

	const bool detachedPhase = (pSetEntry->m_flags & DC::kAnimOverlayFlagsDetachedPhase) != 0;
	const bool globalClockPhase = (pSetEntry->m_flags & DC::kAnimOverlayFlagsGlobalClockPhase) != 0;
	const bool randomStartPhase = (pSetEntry->m_flags & DC::kAnimOverlayFlagsRandomStartPhase) != 0;

	if (detachedPhase)
	{
		pAnimationNode->m_phaseMode = AnimSnapshotNodeAnimation::PhaseMode::kDetached;
	}
	else if (globalClockPhase)
	{
		pAnimationNode->m_phaseMode = AnimSnapshotNodeAnimation::PhaseMode::kGlobalClock;
	}

	pAnimationNode->m_phase = randomStartPhase ? RandomFloatRange(0.0f, 1.0f) : 0.0f;

	if (params.m_pConstOverlaySnapshot->IsUniDirectional() || overlayItr.IsUnidirectional())
	{
		overlayItr.UpdateSourceId(pSelectedRemap->m_remapId);
	}
	else if (pSelectedRemap->m_remapId == sourceId)
	{
		//Don't change the iterator
		return false;
	}
	else
	{
		// restart the search with our new selected variant
		overlayItr = AnimOverlayIterator(pSelectedRemap->m_remapId);
	}

	overlayItr = params.m_pConstOverlaySnapshot->GetNextEntry(overlayItr);

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ApplyOverlayTransform_Replace(const DC::AnimOverlaySetEntry* pSetEntry,
										  const SnapshotAnimNodeTreeParams& params,
										  AnimOverlayIterator& overlayItr,
										  AnimSnapshotNodeAnimation* pAnimationNode,
										  SnapshotAnimNodeTreeResults& results)
{
	if (pAnimationNode->m_noReplaceAnimOverlay)
	{
		return true;
	}

	pAnimationNode->m_animation = pSetEntry->m_remapId;

	const bool detachedPhase	= (pSetEntry->m_flags & DC::kAnimOverlayFlagsDetachedPhase) != 0;
	const bool globalClockPhase = (pSetEntry->m_flags & DC::kAnimOverlayFlagsGlobalClockPhase) != 0;
	const bool randomStartPhase = (pSetEntry->m_flags & DC::kAnimOverlayFlagsRandomStartPhase) != 0;

	if (detachedPhase)
	{
		pAnimationNode->m_phaseMode = AnimSnapshotNodeAnimation::PhaseMode::kDetached;
	}
	else if (globalClockPhase)
	{
		pAnimationNode->m_phaseMode = AnimSnapshotNodeAnimation::PhaseMode::kGlobalClock;
	}

	pAnimationNode->m_phase = randomStartPhase ? RandomFloatRange(0.0f, 1.0f) : 0.0f;

	results.m_newStateFlags |= AnimOverlayExtraStateFlags(pSetEntry->m_flags);

	if (params.m_pConstOverlaySnapshot->IsUniDirectional() || overlayItr.IsUnidirectional())
	{
		overlayItr.UpdateSourceId(pSetEntry->m_remapId);
	}
	else
	{
		overlayItr = AnimOverlayIterator(pSetEntry->m_remapId);
	}

	overlayItr = params.m_pConstOverlaySnapshot->GetNextEntry(overlayItr);

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index TransformAnimNodeAnimation(AnimStateSnapshot* pSnapshot,
												   SnapshotNodeHeap::Index nodeIndex,
												   AnimOverlayIterator startingOverlayIterator,
												   const SnapshotAnimNodeTreeParams& params,
												   SnapshotAnimNodeTreeResults& results)
{
	PROFILE(Animation, TransformAnimNodeAnimation);

	SnapshotNodeHeap* pNodeHeap = pSnapshot->m_pSnapshotHeap;
	AnimSnapshotNodeAnimation* pAnimationNode = pNodeHeap->GetNodeByIndex<AnimSnapshotNodeAnimation>(nodeIndex);

	const StringId64 origAnimId = (pAnimationNode->m_origAnimation != INVALID_STRING_ID_64)
									  ? pAnimationNode->m_origAnimation
									  : pAnimationNode->m_animation;

	pAnimationNode->m_origAnimation = origAnimId;

	AnimOverlayIterator overlayItr = params.m_pConstOverlaySnapshot
										 ? params.m_pConstOverlaySnapshot->GetNextEntry(startingOverlayIterator)
										 : AnimOverlayIterator(INVALID_STRING_ID_64);

	SnapshotNodeHeap::Index returnNodeIndex = nodeIndex;

	bool stop = origAnimId == INVALID_STRING_ID_64;

	U32F loopCount = 0;
	while (!stop && overlayItr.IsValid())
	{
		const DC::AnimOverlaySetEntry* pSetEntry = overlayItr.GetEntry();
		ANIM_ASSERT(pSetEntry);

		switch (pSetEntry->m_type)
		{
		case DC::kAnimOverlayTypeNoop:
			results.m_newStateFlags |= AnimOverlayExtraStateFlags(pSetEntry->m_flags);
			overlayItr = params.m_pConstOverlaySnapshot->GetNextEntry(overlayItr);
			break;

		case DC::kAnimOverlayTypeBlend:
			stop = ApplyOverlayTransform_Blend(pSetEntry,
											   pSnapshot,
											   nodeIndex,
											   overlayItr,
											   params,
											   pAnimationNode,
											   returnNodeIndex,
											   results);
			break;

		case DC::kAnimOverlayTypeTree:
			stop = ApplyOverlayTransform_Tree(pSetEntry,
											  pSnapshot,
											  nodeIndex,
											  params,
											  overlayItr,
											  pAnimationNode,
											  returnNodeIndex,
											  results);
			break;

		case DC::kAnimOverlayTypeUnary:
			stop = ApplyOverlayTransform_Unary(pSetEntry,
											   pSnapshot,
											   nodeIndex,
											   params,
											   overlayItr,
											   pAnimationNode,
											   returnNodeIndex,
											   results);
			break;

		case DC::kAnimOverlayTypeVariant:
		case DC::kAnimOverlayTypeInstanceVariant:
		case DC::kAnimOverlayTypeRandom:
			stop = ApplyOverlayTransform_Random(pSetEntry, params, overlayItr, pAnimationNode, results);
			break;

		case DC::kAnimOverlayTypeReplace:
			stop = ApplyOverlayTransform_Replace(pSetEntry, params, overlayItr, pAnimationNode, results);
			break;
		}

		if (stop && (returnNodeIndex == SnapshotNodeHeap::kOutOfMemoryIndex))
		{
			pAnimationNode = nullptr;
		}

		if (++loopCount == 100)
		{
			static bool s_errorReported = false;

			if (!s_errorReported)
			{
				AnimOverlayIterator errorItr = params.m_pConstOverlaySnapshot->GetNextEntry(AnimOverlayIterator(origAnimId));

				if (errorItr.GetLayerIndex() >= 0)
				{
					MsgConErr("[%s] Circular remap chain detected in remap starting with anim '%s' in overlay set '%s'\n",
							  DevKitOnly_StringIdToString(params.m_pAnimData->m_hProcess.GetUserId()),
							  DevKitOnly_StringIdToString(origAnimId),
							  DevKitOnly_StringIdToString(params.m_pConstOverlaySnapshot->GetOverlaySet(errorItr.GetLayerIndex())->m_name));
				}

				s_errorReported = true;
			}

			break;
		}
	}

	if (pAnimationNode)
	{
		pAnimationNode->m_origAnimation = origAnimId;
		pAnimationNode->m_animation		= pAnimationNode->m_animation;
		pAnimationNode->m_skelId		= params.m_pAnimTable->GetSkelId();
		pAnimationNode->m_hierarchyId	= params.m_pAnimTable->GetHierarchyId();
	}

	return returnNodeIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static SnapshotNodeHeap::Index SnapshotAnimNodeAnimation(AnimStateSnapshot* pSnapshot,
														 const DC::AnimNode* pNode,
														 const SnapshotAnimNodeTreeParams& params,
														 SnapshotAnimNodeTreeResults& results)
{
	ANIM_ASSERT(pNode->m_dcType == SID("anim-node-animation"));

	const StringId64 typeId = g_animNodeLibrary.LookupTypeIdFromDcType(pNode->m_dcType);

	SnapshotNodeHeap::Index returnNodeIndex = -1;

	ANIM_ASSERT(pSnapshot->m_pSnapshotHeap);
	AnimSnapshotNode* pNewNode = pSnapshot->m_pSnapshotHeap->AllocateNode(typeId, &returnNodeIndex);

	if (AnimSnapshotNodeAnimation* pNewAnimNode = AnimSnapshotNodeAnimation::FromAnimNode(pNewNode))
	{
		pNewAnimNode->SnapshotNode(pSnapshot, pNode, params, results);

		if (params.m_pConstOverlaySnapshot)
		{
			returnNodeIndex = TransformAnimNodeAnimation(pSnapshot,
														 returnNodeIndex,
														 AnimOverlayIterator(pNewAnimNode->m_animation, false),
														 params,
														 results);
		}
	}
	else
	{
		const DC::AnimNodeAnimation* pAnimNode = (const DC::AnimNodeAnimation*)pNode;

		MsgAnimErr("[%s] Ran out of snapshot memory creating new snapshot node '%s' for state '%s'\n",
				   DevKitOnly_StringIdToString(params.m_pAnimData->m_hProcess.GetUserId()),
				   DevKitOnly_StringIdToString(pNode->m_dcType),
				   pSnapshot->m_animState.m_name.m_string.GetString());

		returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
	}

	return returnNodeIndex;
}
