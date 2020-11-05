/*
 * Copyright (c) 2011 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-state-instance-track.h"

#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-actor.h"
#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/process/bound-frame.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"

#include "gamelib/level/art-item-anim.h"
#include "gamelib/level/art-item-skeleton.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::Allocate(U32F maxStateInstances, const AnimOverlaySnapshot* pOverlaySnapshot)
{
	ANIM_ASSERT(maxStateInstances > 0);
	m_ppInstanceList = NDI_NEW (kAlign16) AnimStateInstance*[AlignSize(maxStateInstances, kAlign4)]; // We need this list to be a multiple of 16 bytes in size
	ANIM_ASSERT(m_ppInstanceList);
	m_maxNumInstances = maxStateInstances;

	if (pOverlaySnapshot)
	{
		m_pOverlaySnapshot = NDI_NEW (kAlign16) AnimOverlaySnapshot;
		m_pOverlaySnapshot->Init(pOverlaySnapshot->GetNumLayers(), pOverlaySnapshot->IsUniDirectional());
		m_pOverlaySnapshot->SetInstanceSeed(pOverlaySnapshot->GetInstanceSeed());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		RelocatePointer(m_ppInstanceList[i], deltaPos, lowerBound, upperBound);
	}
	RelocatePointer(m_ppInstanceList, deltaPos, lowerBound, upperBound);
	
	if (m_pOverlaySnapshot)
	{
		RelocateObject(m_pOverlaySnapshot, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_pOverlaySnapshot, deltaPos, lowerBound, upperBound);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::DebugPrint(MsgOutput output,
										bool additiveLayer,
										bool baseLayer,
										const FgAnimData* pAnimData,
										const AnimCopyRemapLayer* pRemapLayer) const
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimStateInstance* pCurrentInst = m_ppInstanceList[i];

		const AnimStateInstance* pNextInst = nullptr;

		if (i < (m_numInstances - 1))
		{
			pNextInst = m_ppInstanceList[i + 1];

			const ArtItemAnim* pCurPhaseAnim = pCurrentInst->GetPhaseAnimArtItem().ToArtItem();
			const ArtItemAnim* pNextPhaseAnim = pNextInst->GetPhaseAnimArtItem().ToArtItem();

			if (ValidBitsDiffer(pCurPhaseAnim, pNextPhaseAnim))
			{
				SetColor(output, kColorYellow.ToAbgr8());
				PrintTo(output, "    WARNING Blending mismatched partial sets (THIS WILL CAUSE A POP)\n");
				if (!g_animOptions.m_debugPrint.m_simplified)
				{
					PrintTo(output, "    '%s' != '%s'\n", pCurPhaseAnim->GetName(), pNextPhaseAnim->GetName());
				}
			}
		}

		pCurrentInst->m_stateSnapshot.DebugPrint(output,
												 pCurrentInst,
												 pAnimData,
												 pCurrentInst->GetAnimTable(),
												 pCurrentInst->GetAnimInfoCollection(),
												 pCurrentInst->Phase(),
												 pCurrentInst->AnimFade(),
												 pCurrentInst->AnimFadeTime(),
												 pCurrentInst->MotionFade(),
												 pCurrentInst->MotionFadeTime(),
												 pCurrentInst->GetEffectiveFade(),
												 pCurrentInst->IsFlipped(),
												 additiveLayer,
												 baseLayer,
												 pCurrentInst->GetFlags().m_phaseFrozen,
												 pCurrentInst->m_customApRefId,
												 pCurrentInst->GetFlags().m_disableFeatherBlend?-1:pCurrentInst->GetFeatherBlendTableIndex(),
												 pCurrentInst->GetFeatherBlendTableBlend(),
												 2,
												 pRemapLayer);

		if (pCurrentInst->MasterFade() == 1.0f)
			break;
	}
} 

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::CreateAnimCmds(const AnimStateLayer* pLayer,
											const AnimCmdGenLayerContext& context,
											AnimCmdList* pAnimCmdList,
											U32F outputInstanceIndex,
											U32F trackIndex,
											F32 layerFadeOverride) const
{
	ndanim::BlendMode layerBlendMode = pLayer->GetBlendMode();
	const float layerFade = layerFadeOverride>=0.0f?layerFadeOverride:pLayer->GetCurrentFade();

	const sAnimNodeBlendCallbacks& blendCallbacks = pLayer->m_blendCallbacks;

	U32F firstUnusedInstanceIndex = outputInstanceIndex;
	const F32 trackAnimFade = AnimFade();

	// We need to blend the states starting from the oldest one to the newest to get the proper result. 
	// This forces us to do a lookup from the end of the 'normalized fade' list.
	U32F numEvaluatedInstances = 0;
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		const I32F instanceIndex			= m_numInstances - i - 1;
		AnimStateInstance* pCurrentInst		= m_ppInstanceList[instanceIndex];

		AnimCmdGenInfo animCmdGenInfo;
		animCmdGenInfo.m_pInst				= pCurrentInst;
		animCmdGenInfo.m_pContext			= &context;
		animCmdGenInfo.m_pAnimTable			= pCurrentInst->GetAnimTable();
		animCmdGenInfo.m_pInfoCollection	= pCurrentInst->GetAnimInfoCollection();
		animCmdGenInfo.m_statePhase			= pCurrentInst->Phase();
		animCmdGenInfo.m_allowTreePruning	= true;
		animCmdGenInfo.m_blendCallBacks		= blendCallbacks;
		animCmdGenInfo.m_cameraCutThisFrame	= pCurrentInst->m_flags.m_cameraCutThisFrame;

		if (pCurrentInst->m_flags.m_cameraCutThisFrame)
		{
			// Another little hack. We save the time lost during the camera cut and adjust for that during the coming second
			AnimationSetSkewTime(pCurrentInst->GetRemainderTime());
		}

		pCurrentInst->m_stateSnapshot.GenerateAnimCommands(pAnimCmdList, firstUnusedInstanceIndex, &animCmdGenInfo);

		// do we flip this state's anim tree?
		const bool flipped = pCurrentInst->IsFlipped();
		if (layerBlendMode == ndanim::kBlendSlerp && pCurrentInst->GetRootSnapshotNode()->IsAdditive(&pCurrentInst->GetAnimStateSnapshot()))
		{
			layerBlendMode = ndanim::kBlendAdditive;
		}

		if (flipped)
		{
			if (layerBlendMode == ndanim::kBlendAdditive)
			{
				// I suspect we can't flip additive animations, therefore we flip the underlying animation instead.
				ANIM_ASSERT(context.m_instanceZeroIsValid);
				if (context.m_instanceZeroIsValid)
				{
					pAnimCmdList->AddCmd_EvaluateFlip(0);
				}
			}
			else
			{
				pAnimCmdList->AddCmd_EvaluateFlip(firstUnusedInstanceIndex);
			}
		}

		if (context.m_instanceZeroIsValid)
		{
			// blend the combined previous layers into each instance
			// this is so we can fill in any potentially undefined joints so those joints don't pop when the instance fades out
			const OrbisAnim::ChannelFactor* const* ppChannelFactors = nullptr;
			const U32* pNumChannelFactors = nullptr;

			float fadeToUse = layerFade;

			const I32F iInstanceFeatherBlend = pCurrentInst->GetFeatherBlendTableIndex();
			I32F featherBlendIndex = -1;

			if ((iInstanceFeatherBlend >= 0) && !pCurrentInst->m_flags.m_disableFeatherBlend)
			{
				const F32 featherBlendTableBlend = pCurrentInst->GetFeatherBlendTableBlend();
				featherBlendIndex = iInstanceFeatherBlend;

				fadeToUse = g_featherBlendTable.CreateChannelFactorsForAnimCmd(context.m_pAnimateSkel,
																			   iInstanceFeatherBlend,
																			   fadeToUse,
																			   &ppChannelFactors,
																			   &pNumChannelFactors,
																			   featherBlendTableBlend);
			}
			else if (pLayer->m_featherBlendIndex >= 0)
			{
				featherBlendIndex = pLayer->m_featherBlendIndex;

				fadeToUse = g_featherBlendTable.CreateChannelFactorsForAnimCmd(context.m_pAnimateSkel,
																			   pLayer->m_featherBlendIndex,
																			   fadeToUse,
																			   &ppChannelFactors,
																			   &pNumChannelFactors);
			}

			if (ppChannelFactors)
			{
				pAnimCmdList->AddCmd_EvaluateFeatherBlend(0,
														  firstUnusedInstanceIndex,
														  firstUnusedInstanceIndex,
														  layerBlendMode,
														  fadeToUse,
														  ppChannelFactors,
														  pNumChannelFactors,
														  featherBlendIndex);
			}
			else
			{
				pAnimCmdList->AddCmd_EvaluateBlend(0,
												   firstUnusedInstanceIndex,
												   firstUnusedInstanceIndex,
												   layerBlendMode,
												   fadeToUse);
			}

			// 'unflip' the base after our additive flip hack
			if (flipped && layerBlendMode == ndanim::kBlendAdditive)
			{
				pAnimCmdList->AddCmd_EvaluateFlip(firstUnusedInstanceIndex);
				pAnimCmdList->AddCmd_EvaluateFlip(0);
			}
		}

		if (blendCallbacks.m_postState)
		{
			blendCallbacks.m_postState(&animCmdGenInfo,
									   pAnimCmdList,
									   pCurrentInst,
									   context.m_pAnimateSkel->m_skelId,
									   firstUnusedInstanceIndex,
									   trackIndex,
									   instanceIndex);
		}

		++numEvaluatedInstances;
		++firstUnusedInstanceIndex;

		// If we have 2 or more states it's time to blend them together
		if (numEvaluatedInstances >= 2)
		{
			const F32 fade = pCurrentInst->AnimFade();

			const OrbisAnim::ChannelFactor* const* ppChannelFactors = nullptr;
			const U32* pNumChannelFactors = nullptr;
			const I32 featherBlendIndex = pCurrentInst->GetFeatherBlendTableIndex();

			float fadeToUse = fade;

			if (!pCurrentInst->m_flags.m_disableFeatherBlend && g_animOptions.m_enableBrokenInstanceFeatherBlends)
			{
				const F32 featherBlendTableBlend = pCurrentInst->GetFeatherBlendTableBlend();

				if (featherBlendIndex >= 0)
				{
					fadeToUse = g_featherBlendTable.CreateChannelFactorsForAnimCmd(context.m_pAnimateSkel,
																				   featherBlendIndex,
																				   fadeToUse,
																				   &ppChannelFactors,
																				   &pNumChannelFactors,
																				   featherBlendTableBlend);
				}
			}

			// blend the two states together (They have blended with the previous layer individually)
			ANIM_ASSERT(firstUnusedInstanceIndex >= 2);
			if (blendCallbacks.m_stateBlendFunc)
			{
				blendCallbacks.m_stateBlendFunc(context,
												pAnimCmdList,
												firstUnusedInstanceIndex - 2,
												firstUnusedInstanceIndex - 1,
												firstUnusedInstanceIndex - 2,
												ndanim::kBlendSlerp,
												fade);
			}
			else
			{
				if (ppChannelFactors)
				{
					pAnimCmdList->AddCmd_EvaluateFeatherBlend(firstUnusedInstanceIndex - 2,
															  firstUnusedInstanceIndex - 1,
															  firstUnusedInstanceIndex - 2,
															  ndanim::kBlendSlerp,
															  fadeToUse,
															  ppChannelFactors,
															  pNumChannelFactors,
															  featherBlendIndex);
				}
				else
				{
					pAnimCmdList->AddCmd_EvaluateBlend(firstUnusedInstanceIndex - 2,
													   firstUnusedInstanceIndex - 1,
													   firstUnusedInstanceIndex - 2,
													   ndanim::kBlendSlerp,
													   fadeToUse);
				}
			}

			--firstUnusedInstanceIndex;
		}

		if (g_animOptions.m_generatingNetCommands)
		{
			pAnimCmdList->AddCmd_State(pCurrentInst->GetStateName(), pCurrentInst->AnimFadeTime());
		}
	}

	ANIM_ASSERT(numEvaluatedInstances > 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::RefreshAnimPointers()
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimStateInstance* pInstance = m_ppInstanceList[i];
		pInstance->RefreshAnimPointers();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::DebugOnly_ForceUpdateInstanceSnapShots(const DC::AnimActor* pAnimActor,
																	const FgAnimData* pAnimData)
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimStateInstance* pCurrentInst = m_ppInstanceList[i];
		const DC::AnimState* pNewState = AnimActorFindState(pAnimActor, pCurrentInst->m_stateSnapshot.m_animState.m_name.m_symbol);
		ANIM_ASSERT(pNewState); // if you hit this it means you loaded an AnimActor that's missing a state you're using
		
		//Release current node before allocating new ones
		pCurrentInst->OnRelease();
		
		pCurrentInst->UpdateFromState(pNewState, m_pOverlaySnapshot, pAnimData);

		// Fake an update to refresh phases and blends
		pCurrentInst->PhaseUpdate(0.0f, i == 0, nullptr);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::UpdateAllApReferences(const BoundFrame& apReference,
												   AnimStateLayerFilterAPRefCallBack filterCallback)
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimStateInstance* pInstance = m_ppInstanceList[i];
		if (filterCallback)
		{
			if (!filterCallback(pInstance))
				continue;
		}
		pInstance->SetApLocator(apReference);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstanceTrack::UpdateAllApReferencesUntilFalse(const BoundFrame& apReference,
															 AnimStateLayerFilterAPRefCallBack filterCallback)
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimStateInstance* pInstance = m_ppInstanceList[i];
		if (filterCallback)
		{
			if (!filterCallback(pInstance))
				return false;
		}
		pInstance->SetApLocator(apReference);
	}

	return true;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::UpdateAllApReferencesTranslationOnly(Point_arg newApPos,
																  AnimStateLayerFilterAPRefCallBack filterCallback)
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimStateInstance* pInstance = m_ppInstanceList[i];
		if (filterCallback)
		{
			if (!filterCallback(pInstance))
				continue;
		}		
		pInstance->SetApTranslationOnly(newApPos);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TransformApLocator(BoundFrame& loc, const Locator& oldSpace, const Locator& newSpace)
{
	Point rel = oldSpace.UntransformPoint(loc.GetTranslationWs());
	loc.SetTranslation( newSpace.TransformPoint(rel) );
	Quat rotDelta = Conjugate(oldSpace.Rot()) * newSpace.Rot();
	rotDelta.SetX(0);
	rotDelta.SetZ(0);
	rotDelta = Normalize(rotDelta);
	loc.AdjustRotation( rotDelta );
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::TransformAllApReferences(const Locator& oldSpace, const Locator& newSpace)
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimStateInstance* pInstance = m_ppInstanceList[i];
		BoundFrame loc = pInstance->GetApLocator();
		TransformApLocator(loc, oldSpace, newSpace);
		pInstance->SetApLocator(loc);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstanceTrack::AnimStateInstanceTrack()
	: m_ppInstanceList(nullptr)
	, m_pOverlaySnapshot(nullptr)
	, m_maxNumInstances(0)
	, m_numInstances(0)
{
	m_pad[0] = 'C';
	m_pad[1] = 'W';
	m_pad[2] = 'B';
	m_pad[3] = 'Y';
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::Init(const AnimOverlaySnapshot* pOverlaySnapshot)
{
	UpdateOverlaySnapshot(pOverlaySnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::Reset()
{
	m_numInstances = 0;

	if (m_ppInstanceList)
	{
		const U32F instanceListSize = sizeof(AnimStateInstance*) * AlignSize(m_maxNumInstances, kAlign4);

		memset(m_ppInstanceList, 0, instanceListSize);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::UpdateOverlaySnapshot(const AnimOverlaySnapshot* pSourceSnapshot)
{
	if (!m_pOverlaySnapshot || !pSourceSnapshot)
		return;

	AnimOverlaySnapshot* pSnapshotLs = m_pOverlaySnapshot;
	pSnapshotLs->CopyFrom(pSourceSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstanceTrack::CanPushInstance() const
{
	return m_numInstances < m_maxNumInstances;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstanceTrack::PushInstance(AnimStateInstance* pInstance)
{
	ANIM_ASSERT(CanPushInstance());
	if (!CanPushInstance())
		return false;

	AnimStateInstance** ppInstanceList = m_ppInstanceList;

	for (U32F i = m_numInstances; i > 0; --i)
	{
		ppInstanceList[i] = ppInstanceList[i-1];
	}

	ppInstanceList[0] = pInstance;
	++m_numInstances;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateInstanceTrack::CurrentStateInstance() const
{
	if (m_numInstances > 0)
	{
		return GetInstance(0);
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* AnimStateInstanceTrack::CurrentStateInstance()
{
	if (m_numInstances > 0)
	{
		return GetInstance(0);
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateInstanceTrack::OldestStateInstance() const
{
	if (m_numInstances > 0)
	{
		return GetInstance(m_numInstances - 1);
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* AnimStateInstanceTrack::OldestStateInstance()
{
	if (m_numInstances > 0)
	{
		return GetInstance(m_numInstances - 1);
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* AnimStateInstanceTrack::GetInstance(U32F index)
{
	return m_ppInstanceList[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateInstanceTrack::GetInstance(U32F index) const
{
	return m_ppInstanceList[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* AnimStateInstanceTrack::ReclaimInstance()
{
	ANIM_ASSERT(m_numInstances > 0);
	if (m_numInstances == 0)
		return nullptr;

	--m_numInstances;

	AnimStateInstance* pReleasedInstance = m_ppInstanceList[m_numInstances];

	if (pReleasedInstance)
	{
		pReleasedInstance->OnRelease();
	}

	return pReleasedInstance;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateInstanceTrack::FindInstanceByName(StringId64 stateName) const
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		const U32F index = m_numInstances - i - 1;
		const AnimStateInstance* pInstance = m_ppInstanceList[index];
		if (pInstance && pInstance->GetStateName() == stateName)
			return pInstance;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateInstanceTrack::FindInstanceByNameNewToOld(StringId64 stateName) const
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		const AnimStateInstance* pInstance = m_ppInstanceList[i];
		if (pInstance && pInstance->GetStateName() == stateName)
			return pInstance;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateInstanceTrack::FindInstanceById(AnimInstance::ID id) const
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		const AnimStateInstance *pInstance = m_ppInstanceList[i];
		if (pInstance && pInstance->GetId() == id)
			return pInstance;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::DeleteNonContributingInstances(AnimStateLayer* pStateLayer)
{
	// We need at least 2 instances to have a non-contributing instance
	if (m_numInstances < 2)
		return;

	I32F index = 0;
	for (U32F iInstance = 0; iInstance < m_numInstances; ++iInstance)
	{
		// Load the instance
		const AnimStateInstance* pInstance = GetInstance(iInstance);
		if (pInstance->MasterFade() == 1.0f)
		{
			for (U32F iDelInst = iInstance + 1; iDelInst < m_numInstances - 1; ++iDelInst)
			{
				AnimStateInstance* pDelInstance = m_ppInstanceList[iDelInst];
				pStateLayer->ReleaseInstance(pDelInstance);
				m_ppInstanceList[iDelInst] = nullptr;
			}

			// keep the oldest instance until it's completely faded up
			if (iInstance < m_numInstances - 1)
			{
				AnimStateInstance* pOldestInstance = OldestStateInstance();

				m_ppInstanceList[m_numInstances - 1] = nullptr;

				if (pOldestInstance->MasterFade() < 1.0f)
				{
					m_ppInstanceList[iInstance + 1] = pOldestInstance;
					++iInstance;
				}
				else
				{
					pStateLayer->ReleaseInstance(pOldestInstance);
				}
			}

			m_numInstances = iInstance + 1;
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::ResetAnimStateChannelDeltas()
{
	I32F index = 0;
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimStateInstance* pInstance = m_ppInstanceList[i];

		// Preserve the align delta if we are extrapolating the align
		if (!(pInstance->GetStateFlags() & DC::kAnimStateFlagExtrapolateAlign) || pInstance->Phase() < 1.0f)
		{
			pInstance->ResetChannelDeltas();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstanceTrack::UpdateInstanceFadeEffects(F32 deltaTime, bool freezeNextInstancePhase)
{
	if (!m_numInstances)
		return freezeNextInstancePhase;

	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimStateInstance* pInstance = m_ppInstanceList[i];
		freezeNextInstancePhase = pInstance->FadeUpdate(deltaTime, freezeNextInstancePhase);
	}

	return freezeNextInstancePhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::UpdateInstancePhases(F32 deltaTime,
												  bool topLayer,
												  EffectList* pTriggeredEffects)
{
	if (!m_numInstances)
		return;

	bool topState = topLayer;

	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimStateInstance* pInstance = m_ppInstanceList[i];

		const DC::AnimStateFlag stateFlags = pInstance->GetStateFlags();
		const float phase = pInstance->Phase();

		// Preserve the phase delta (prev and current phase) if we are extrapolating the align
		if (!(stateFlags & DC::kAnimStateFlagExtrapolateAlign) || (phase < 1.0f))
		{
			// Update the phase and play effects.
			pInstance->PhaseUpdate(deltaTime, topState, pTriggeredEffects);
			
			topState = false;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateInstanceTrack::UpdateEffectiveFade(float effectiveTrackFade)
{
	float remFade = effectiveTrackFade;
	const AnimStateInstance* pOldestInstance = OldestStateInstance();

	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimStateInstance* pInstance = m_ppInstanceList[i];

		// NB: Our oldest instance dictates effective track fade, so don't double up the effect here
		const float instFade = (pInstance == pOldestInstance) ? 1.0f : pInstance->AnimFade();
		const float effectiveFade = remFade * instFade;

		pInstance->SetEffectiveFade(effectiveFade);

		remFade *= Limit01(1.0f - instFade);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstanceTrack::MasterFade() const
{
	const AnimStateInstance* pOldestInstance = OldestStateInstance();
	if (pOldestInstance)
	{
		return pOldestInstance->MasterFade();
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstanceTrack::AnimFade() const
{
	const AnimStateInstance* pOldestInstance = OldestStateInstance();
	if (pOldestInstance)
	{
		return pOldestInstance->AnimFade();
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimStateInstanceTrack::MotionFade() const
{
	const AnimStateInstance* pOldestInstance = OldestStateInstance();
	if (pOldestInstance)
	{
		return pOldestInstance->MotionFade();
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstanceTrack::WalkInstancesOldToNew(PFnVisitAnimStateInstance pfnCallback,
												   AnimStateLayer* pStateLayer,
												   uintptr_t userData)
{
	if (!pfnCallback)
		return false;

	const U32F numInstances = GetNumInstances();
	for (U32F i = 0u; i < numInstances; ++i)
	{
		AnimStateInstance* pInst = GetInstance(numInstances - i - 1);
		if (!pInst)
			continue;

		if (!pfnCallback(pInst, pStateLayer, userData))
			return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstanceTrack::WalkInstancesOldToNew(PFnVisitAnimStateInstanceConst pfnCallback,
												   const AnimStateLayer* pStateLayer,
												   uintptr_t userData) const
{
	if (!pfnCallback)
		return false;

	const U32F numInstances = GetNumInstances();
	for (U32F i = 0u; i < numInstances; ++i)
	{
		const AnimStateInstance* pInst = GetInstance(numInstances - i - 1);
		if (!pInst)
			continue;

		if (!pfnCallback(pInst, pStateLayer, userData))
			return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstanceTrack::WalkInstancesNewToOld(PFnVisitAnimStateInstance pfnCallback,
												   AnimStateLayer* pStateLayer,
												   uintptr_t userData)
{
	if (!pfnCallback)
		return false;

	const U32F numInstances = GetNumInstances();
	for (U32F i = 0u; i < numInstances; ++i)
	{
		AnimStateInstance* pInst = GetInstance(i);
		if (!pInst)
			continue;

		if (!pfnCallback(pInst, pStateLayer, userData))
			return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstanceTrack::WalkInstancesNewToOld(PFnVisitAnimStateInstanceConst pfnCallback,
												   const AnimStateLayer* pStateLayer,
												   uintptr_t userData) const
{
	if (!pfnCallback)
		return false;

	const U32F numInstances = GetNumInstances();
	for (U32F i = 0u; i < numInstances; ++i)
	{
		const AnimStateInstance* pInst = GetInstance(i);
		if (!pInst)
			continue;

		if (!pfnCallback(pInst, pStateLayer, userData))
			return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateInstanceTrack::HasInstance(const AnimStateInstance* pInstance) const
{
	const U32F numInstances = GetNumInstances();
	for (U32F i = 0u; i < numInstances; ++i)
	{
		const AnimStateInstance* pInst = GetInstance(i);

		if (pInst == pInstance)
			return true;
	}

	return false;
}
