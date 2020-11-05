/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-simple-layer.h"

#include "corelib/memory/relocate.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/color.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-streaming.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/nd-anim-align-util.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/util/string-cache.h"

#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSimpleLayer::FadeRequestParams::FadeRequestParams()
	: m_blendType(DC::kAnimCurveTypeUniformS)
	, m_startPhase(0.0f)
	, m_fadeTime(0.0f)
	, m_playbackRate(1.0f)
	, m_layerFade(1.0f)
	, m_firstUpdateEffGatherFrame(-1.0f)
	, m_forceLoop(false)
	, m_freezeSrc(false)
	, m_freezeDst(false)
	, m_mirror(false)
	, m_alignToAp(false)
	, m_noAlignLocation(false)
	, m_skipFirstFrameUpdate(false)
	, m_firstUpdatePhase(-1.f)
	, m_apRefChannelId(SID("apReference"))
	, m_phaseMode(DC::kAnimatePhaseExplicit)
	, m_pSharedTime(nullptr)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSimpleLayer::AnimSimpleLayer(AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot, U32F numInstancesPerLayer)
	: AnimLayer(kAnimLayerTypeSimple, pAnimTable, pOverlaySnapshot)
{
	ANIM_ASSERT(numInstancesPerLayer <= kMaxSupportedInstances);
	m_pAllocatedInstances = NDI_NEW (kAlign128) AnimSimpleInstance[numInstancesPerLayer];
	m_numAllocatedInstances = numInstancesPerLayer;

	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::Reset()
{
	// By default a simple layer is fully faded in.
	Fade(1.0f, 0.0f);
	SetCurrentFade(1.0f);

	m_usedInstances.ClearAllBits();
	m_numInstances = 0;

	m_preBlend = nullptr;
	m_postBlend = nullptr;
	m_numInstancesStarted = 0;
	m_szDebugPrintError = nullptr;
	m_debugId = INVALID_STRING_ID_64;

	m_blendCallbackUserData = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	for (U32F i = 0; i < m_numAllocatedInstances; ++i)
	{
		m_pAllocatedInstances[i].Relocate(deltaPos, lowerBound, upperBound);
	}
	RelocatePointer(m_pAllocatedInstances, deltaPos, lowerBound, upperBound);

	for (U32F i = 0; i < m_numInstances; ++i)
	{
		RelocatePointer(m_pInstanceList[i], deltaPos, lowerBound, upperBound);
	}

	RelocatePointer((void*&)m_blendCallbackUserData, deltaPos, lowerBound, upperBound);

	AnimLayer::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimSimpleLayer::GetNumFadesInProgress() const
{
	// The controller is considered 'busy' if a fade between states is in progress.
	return m_numInstances >= 1 ? m_numInstances - 1 : 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleLayer::GetApRefFromCurrentInstance(BoundFrame& bf) const
{
	if (m_numInstances > 0)
	{
		bf = m_pInstanceList[0]->GetApOrigin();
		return true;
	}
	else
	{
		// This BoundFrame isn't really valid -- caller should check IsValid() and use
		// the object's BoundFrame instead if it's invalid.
		bf = BoundFrame(kIdentity);
		return false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleLayer::GetApRefFromCurrentInstance(Locator& loc) const
{
	BoundFrame bf;
	bool valid = GetApRefFromCurrentInstance(bf);
	loc = bf.GetLocator();
	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void LerpJointParams(U32F srcMask,
							U32F dstMask,
							const ndanim::JointParams* pSrcParams,
							ndanim::JointParams* pDstParams,
							size_t numLocators,
							float tt)
{
	if (!pSrcParams || !pDstParams)
		return;

	for (U32F i = 0; i < numLocators; ++i)
	{
		const bool srcValid = (srcMask & (i << 1ULL)) != 0;
		const bool dstValid = (dstMask & (i << 1ULL)) != 0;

		if (srcValid && dstValid)
		{
			pDstParams[i] = Lerp(pSrcParams[i], pDstParams[i], tt);
		}
		else if (srcValid)
		{
			pDstParams[i] = pSrcParams[i];
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimSimpleLayer::EvaluateChannels(const StringId64* pChannelNames,
									   size_t numChannels,
									   ndanim::JointParams* pOutChannelJoints,
									   const EvaluateChannelParams& params,
									   FadeMethodToUse fadeMethod /* = kUseMotionFade */,
									   float* pBlendValsOut /* = nullptr */) const
{
	if (numChannels <= 0)
		return 0;

	U32F evalMask = 0;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	ndanim::JointParams* pInstParams = NDI_NEW ndanim::JointParams[numChannels];

	for (I32F i = I32F(m_numInstances) - 1; i >= 0; --i)
	{
		const AnimSimpleInstance* pInstance = GetInstance(i);

		const U32F instEvalMask = pInstance->EvaluateChannels(pChannelNames, numChannels, pInstParams, params);

		LerpJointParams(instEvalMask, evalMask, pInstParams, pOutChannelJoints, numChannels, pInstance->GetFade());

		evalMask |= instEvalMask;
	}

	return evalMask;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimSimpleLayer::EvaluateFloatChannels(const StringId64* pChannelNames,
											U32F numChannels,
											float* pOutChannelFloats,
											const EvaluateChannelParams& params) const
{
	if (numChannels <= 0)
		return 0;

	U32F evalMask = 0;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	for (U32F j = 0; j < numChannels; ++j)
	{
		pOutChannelFloats[j] = 0.0f;
	}


	float* pInstFloats = STACK_ALLOC(float, numChannels);

	for (I32F i = I32F(m_numInstances) - 1; i >= 0; --i)
	{
		const AnimSimpleInstance* pInstance = GetInstance(i);

		const float tt = pInstance->GetFade();

		const U32F instEvalMask = pInstance->EvaluateFloatChannels(pChannelNames, numChannels, pInstFloats, params);

		for (U32F j = 0; j < numChannels; ++j)
		{
			pOutChannelFloats[j] = Lerp(pOutChannelFloats[j], pInstFloats[j], tt);
		}

		evalMask |= instEvalMask;
	}

	return evalMask;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::Setup(StringId64 name, ndanim::BlendMode blendMode)
{
	AnimLayer::Setup(name, blendMode);

	Reset();

	// By default a simple layer is fully faded in.
	Fade(1.0f, 0.0f);
	SetCurrentFade(1.0f);

	m_usedInstances.ClearAllBits();

	ANIM_ASSERT(m_preBlend == nullptr);
	ANIM_ASSERT(m_postBlend == nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::DebugPrint(MsgOutput output, U32 priority) const
{
	STRIP_IN_FINAL_BUILD;

	if (!Memory::IsDebugMemoryAvailable())
		return;

	if (!g_animOptions.m_debugPrint.ShouldShow(GetName()))
		return;

	if (GetCurrentFade() <= 0.0f || m_numInstances == 0)
		return;

	SetColor(output, 0xFF000000 | 0x00FFFFFF);
	PrintTo(output, "-----------------------------------------------------------------------------------------\n");

	if (!g_animOptions.m_debugPrint.m_simplified)
	{
		SetColor(output, 0xFF000000 | 0x0055FF55);
		PrintTo(output,
				"AnimSimpleLayer \"%s\": [%s], Pri %d, Cur Fade: %1.2f, Des Fade: %1.2f",
				DevKitOnly_StringIdToString(m_name),
				m_blendMode == ndanim::kBlendSlerp ? "blend" : "additive",
				priority,
				GetCurrentFade(),
				GetDesiredFade());

		if (const FeatherBlendTable::Entry* pEntry = g_featherBlendTable.GetEntry(m_featherBlendIndex))
		{
			PrintTo(output, " [%s:%.2f] ", DevKitOnly_StringIdToString(pEntry->m_featherBlendId), 1.0f);
		}

		PrintTo(output, "\n");
	}
	else
	{
		SetColor(output, 0xFF000000 | 0x0055FF55);
		PrintTo(output, "AnimSimpleLayer \"%s\"\n", DevKitOnly_StringIdToString(m_name));
	}

	for (U32F i = 0; i < m_numInstances; ++i)
	{
		m_pInstanceList[i]->DebugPrint(output);
	}

	SetColor(output, kColorWhite.ToAbgr8());

	if (m_szDebugPrintError)
	{
		SetColor(output,  kColorRed.ToAbgr8());
		PrintTo(output, m_szDebugPrintError);
		SetColor(output,  kColorWhite.ToAbgr8());
		PrintTo(output, "\n");
	}
} 

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleLayer::RequestFadeToAnim(StringId64 animId, const FadeRequestParams& params /* = FadeRequestParams() */)
{
	const StringId64 transformedAnimId = m_pOverlaySnapshot ? m_pOverlaySnapshot->LookupTransformedAnimId(animId) : animId;
	ArtItemAnimHandle anim = m_pAnimTable ? m_pAnimTable->LookupAnim(transformedAnimId) : ArtItemAnimHandle();

	if (ShouldAssertOnStateChanges())
	{
		ANIM_ASSERTF(false, ("%s: SimpleLayer attempting to fade to anim '%s' but fades are currently locked out",
								/*m_pAnimData ? DevKitOnly_StringIdToStringOrHex(m_pAnimData->m_hProcess.GetUserId()) :*/ "(unknown)", DevKitOnly_StringIdToStringOrHex(animId)));
	}

	if (anim.ToArtItem() == nullptr)
	{
		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_warnOnMissingAnimations))
		{
			MsgAnim("WARNING - Animation(" PFS_PTR "): AnimSimpleLayer::RequestFadeToAnim can't find animation %s\n",
					this,
					DevKitOnly_StringIdToString(transformedAnimId));
		}
		return false;
	}

	return RequestFadeToAnim(anim, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleLayer::RequestFadeToAnim(ArtItemAnimHandle anim, const FadeRequestParams& params /* = FadeRequestParams() */)
{
	if (anim.ToArtItem() == nullptr)
	{
		return false;
	}

	AnimSimpleInstance* pCurrInst = CurrentInstance();
	const StringId64 previousAnimId = pCurrInst ? pCurrInst->GetAnimId() : INVALID_STRING_ID_64;
	const float previousAnimPhase = pCurrInst ? pCurrInst->GetPhase() : 0.0f;

	if (ShouldAssertOnStateChanges())
	{
		ANIM_ASSERTF(false, ("%s: SimpleLayer attempting to fade to anim '%s' but fades are currently locked out",
								/*m_pAnimData ? DevKitOnly_StringIdToStringOrHex(m_pAnimData->m_hProcess.GetUserId()) :*/ "(unknown)", DevKitOnly_StringIdToStringOrHex(anim.ToArtItem()->GetNameId())));
	}

	AnimSimpleInstance* pNewInst = nullptr;
	const U64 freeInstanceIndex = m_usedInstances.FindFirstClearBit();
	if (freeInstanceIndex >= m_numAllocatedInstances)
	{
/*		if (params.m_fadeTime != 0.0f)
		{
			MsgAnim("ERROR - Animation(" PFS_PTR "): AnimSimpleLayer::RequestFadeToAnim reclaiming a contributing state instance\n", this);
		}*/

		// Let's reuse the oldest instance as it is about to get faded out.
		pNewInst = m_pInstanceList[m_numAllocatedInstances - 1];
		m_numInstances = m_numAllocatedInstances - 1;
	}
	else
	{
		pNewInst = &m_pAllocatedInstances[freeInstanceIndex];
		m_usedInstances.SetBit(freeInstanceIndex);
	}
	ANIM_ASSERT(pNewInst);

	const bool sameAnim = anim.ToArtItem()->GetNameId() == previousAnimId;

	float animStartPhase = params.m_startPhase;
	switch (params.m_phaseMode)
	{
	case DC::kAnimatePhasePrevious:
		animStartPhase = previousAnimPhase;
		break;
	case DC::kAnimatePhaseOneMinusPrevious:
		animStartPhase = 1.0f - previousAnimPhase;		
		break;
	default:
		break;
	}

	++m_numInstancesStarted;
	if (m_numInstancesStarted == INVALID_ANIM_INSTANCE_ID.GetValue())
	{
		++m_numInstancesStarted; // handle wrap-around
	}

	pNewInst->Init(AnimSimpleInstance::ID(m_numInstancesStarted),
				   GetAnimTable(),
				   anim,
				   Limit01(animStartPhase),
				   params.m_playbackRate,
				   params.m_fadeTime,
				   params.m_blendType,
				   params.m_firstUpdateEffGatherFrame,
				   params.m_pSharedTime
				   );

	if (GetName() == SID("base"))
	{
		pNewInst->EnableCameraCutDetection();
	}

	if (params.m_forceLoop)
	{
		pNewInst->SetLooping(true);
	}

	if (params.m_noAlignLocation)
	{
		pNewInst->SetNoAlignLocation();
	}

	if (params.m_freezeDst)
	{
		pNewInst->SetFrozen(true);
	}
	
	// Push the new instance at the front of the stack
	for (U32F i = m_numInstances; i > 0; --i)
	{
		m_pInstanceList[i] = m_pInstanceList[i - 1];
	}
	m_pInstanceList[0] = pNewInst;
	++m_numInstances;

	if (m_numInstances > 1)
	{
		m_pInstanceList[1]->SetFrozen(params.m_freezeSrc);
	}

	// Always clear the AP origin here; if the caller wants to align to the apReference (s)he
	// needs to call AlignToActionPackOrigin() *after* successfully calling AddAnimation().
	if (params.m_alignToAp)
	{
		pNewInst->SetApOrigin(params.m_apRef);
		pNewInst->AlignToActionPackOrigin(true);
	}
	else
	{
		pNewInst->AlignToActionPackOrigin(false);
	}
	pNewInst->SetApChannelName(params.m_apRefChannelId);

	pNewInst->SetFlipped(params.m_mirror);

	m_fadeOutParams = params.m_layerFadeOutParams;

	// Because we fade to this animation this frame we don't want to adjust that the very first thing we do when this object updates.
	pNewInst->SetSkipPhaseUpdateThisFrame(params.m_skipFirstFrameUpdate);

	if (params.m_firstUpdatePhase >= 0.f)
		pNewInst->SetFirstUpdatePhase(params.m_firstUpdatePhase);

	// If you request an animation it is assumed that you want it to show. ;)
	// Hence, fade the layer in.
	Fade(params.m_layerFade, params.m_fadeTime, params.m_blendType);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::BeginStep(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData* pAnimData)
{
	StepInternal(deltaTime, pTriggeredEffects, pAnimData);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::FinishStep(F32 deltaTime, EffectList* pTriggeredEffects)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSimpleInstance* AnimSimpleLayer::GetInstance(U32F index)
{ 
	ANIM_ASSERT(index < m_numInstances); 
	return m_pInstanceList[index]; 
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimSimpleInstance* AnimSimpleLayer::GetInstance(U32F index) const
{ 
	ANIM_ASSERT(index < m_numInstances); 
	return m_pInstanceList[index]; 
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimSimpleInstance* AnimSimpleLayer::GetInstanceById(AnimSimpleInstance::ID id) const
{ 
	for (int iInst = 0; iInst < m_numInstances; ++iInst)
	{		
		if (m_pInstanceList[iInst]->GetId() == id)
			return m_pInstanceList[iInst];
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::CollectContributingAnims(AnimCollection* pCollection) const
{
	for (int iInst = 0; iInst < m_numInstances; ++iInst)
	{
		const AnimSimpleInstance* pInstance = m_pInstanceList[iInst];
		const ArtItemAnim* pAnim = pInstance->GetAnim().ToArtItem();
		pCollection->Add(pAnim);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::CreateAnimCmds(const AnimCmdGenLayerContext& context, AnimCmdList* pAnimCmdList) const
{
	m_szDebugPrintError = nullptr;

	U32F firstUnusedInstanceIndex = context.m_instanceZeroIsValid ? 1 : 0;

	// We need to blend the states starting from the oldest one to the newest to get the proper result. 
	// This forces us to do a lookup from the end of the 'normalized fade' list.
	U32F numEvaluatedInstances = 0;

	for (I32F i = I32F(m_numInstances) - 1; i >= 0; --i)
	{
		PROFILE(Animation, Simple_CreateAnimCmds);

		const AnimSimpleInstance* pInst = m_pInstanceList[i];

		const ArtItemAnim* pAnim = pInst->GetAnim().ToArtItem();
		const ArtItemAnim* pOldAnim = pAnim;
		const StringId64 animId = pInst->GetAnimId();
		ndanim::BlendMode layerBlendMode = GetBlendMode();

		if (pAnim)
		{
			float sample = pInst->GetSample();
			if (pAnim->m_flags & ArtItemAnim::kStreaming)
			{
				float phase = pInst->GetPhase();
				AnimStreamNotifyUsage(pAnim, pAnim->GetNameId(), phase);
				float animPhase = phase;
				phase = AnimStreamGetChunkPhase(pAnim, pAnim->GetNameId(), animPhase);
				pAnim = AnimStreamGetArtItem(pAnim, pAnim->GetNameId(), animPhase);

				if (pAnim)
				{
					const float maxFrameSample = static_cast<float>(pAnim->m_pClipData->m_numTotalFrames) - 1.0f;

					sample = maxFrameSample * phase;
				}
			}

			if (layerBlendMode == ndanim::kBlendSlerp)
			{
				const ndanim::ClipData* pClipData = pAnim->m_pClipData;
				if (pClipData->m_clipFlags & ndanim::kClipAdditive)
				{
					layerBlendMode = ndanim::kBlendAdditive;
				}
			}

			if (pInst->DidCameraCutThisFrame())
			{
				AnimationSetSkewTime(pInst->GetRemainderTime());
			}

			// The animation needs to be retargeted
			if (pAnim)
			{
				pAnimCmdList->AddCmd_EvaluateClip(pAnim, firstUnusedInstanceIndex, sample);
			}
			else
			{
				pAnimCmdList->AddCmd_EvaluateBindPose(firstUnusedInstanceIndex);
				if (pOldAnim)
				{
					static char buf[256];
					sprintf(buf, "Anim %s not found! All previous layers overwritten with bindpose", pOldAnim->GetName());
					ShowError(buf);
				}
				else
				{
					ShowError("Anim not found! All previous layers overwritten with bindpose");
				}
			}
		}
		else
		{
			pAnimCmdList->AddCmd_EvaluateBindPose(firstUnusedInstanceIndex);

			const char* errorString = "Anim not found! All previous layers overwritten with bindpose";
			if (!IsFinalBuild() && (animId != INVALID_STRING_ID_64))
			{
				//IStringBuilder* pBuilder = NDI_NEW (kAllocDoubleGameFrame) StringBuilder<128>(
				//	"Anim %s not found! All previous layers overwritten with bindpose",
				//	DevKitOnly_StringIdToString(animId));
				//
				//errorString = pBuilder->c_str();

				char buf[256];
				sprintf(buf, "Anim %s not found! All previous layers overwritten with bindpose", DevKitOnly_StringIdToString(animId));

				const char* copiedBuf = CopyDebugString(buf, FILE_LINE_FUNC);
				if (copiedBuf)
				{
					errorString = copiedBuf;
				}
			}

			ShowError(errorString);
		}

		const bool flipped = pInst->IsFlipped();
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
			F32 nodeBlendToUse = GetCurrentFade();

			const OrbisAnim::ChannelFactor* const* ppChannelFactors = nullptr;
			const U32* pNumChannelFactors = nullptr;

			if (m_featherBlendIndex >= 0)
			{
				nodeBlendToUse = g_featherBlendTable.CreateChannelFactorsForAnimCmd(context.m_pAnimateSkel,
																					m_featherBlendIndex,
																					nodeBlendToUse,
																					&ppChannelFactors,
																					&pNumChannelFactors);
			}

			// blend all instances beyond the first down to the first
			if (ppChannelFactors)
			{
				pAnimCmdList->AddCmd_EvaluateFeatherBlend(0,
														  firstUnusedInstanceIndex,
														  firstUnusedInstanceIndex,
														  layerBlendMode,
														  nodeBlendToUse,
														  ppChannelFactors,
														  pNumChannelFactors,
														  m_featherBlendIndex);
			}
			else
			{
				pAnimCmdList->AddCmd_EvaluateBlend(0,
												   firstUnusedInstanceIndex,
												   firstUnusedInstanceIndex,
												   layerBlendMode,
												   nodeBlendToUse);
			}

			// 'unflip' the base after our additive flip hack
			if (flipped && layerBlendMode == ndanim::kBlendAdditive)
			{
				pAnimCmdList->AddCmd_EvaluateFlip(firstUnusedInstanceIndex);
			}
		}

		++numEvaluatedInstances;
		++firstUnusedInstanceIndex;

		// If we have 2 or more evaluated instances it's time to blend them together
		if (numEvaluatedInstances >= 2)
		{
			// blend the two states together (They have blended with the previous layer individually)
			ANIM_ASSERT(firstUnusedInstanceIndex >= 2);
			pAnimCmdList->AddCmd_EvaluateBlend(firstUnusedInstanceIndex - 2,
											   firstUnusedInstanceIndex - 1,
											   firstUnusedInstanceIndex - 2,
											   ndanim::kBlendSlerp,
											   pInst->GetFade());
			--firstUnusedInstanceIndex;
		}
	}

	ANIM_ASSERT(numEvaluatedInstances > 0);

	if (context.m_instanceZeroIsValid)
	{
		if (m_preBlend)
		{
			m_preBlend(pAnimCmdList,
					   context.m_pAnimateSkel->m_skelId,
					   0,
					   1,
					   0,
					   ndanim::kBlendSlerp,
					   m_blendCallbackUserData);
		}

		// This step could be avoided but we need to have the final blend in instance 0. If we could blend into a third instance it would be nice.
		//pAnimCmdList->AddCmd_EvaluateCopy(1, 0);
		pAnimCmdList->AddCmd_EvaluateBlend(0, 1, 0, ndanim::kBlendSlerp, 1.0f);

		if (m_postBlend)
		{
			m_postBlend(pAnimCmdList,
						context.m_pAnimateSkel->m_skelId,
						0,
						1,
						0,
						ndanim::kBlendSlerp,
						m_blendCallbackUserData);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleLayer::GetAnimAlignDelta(const Locator& curAlign, Locator& alignDeltaOut) const
{
	Locator accumAlignDelta = Locator(kIdentity);

	bool success = true;

	for (I32F i = I32F(m_numInstances) - 1; i >= 0; --i)
	{
		const AnimSimpleInstance* pInst = m_pInstanceList[i];
		if (!pInst)
			continue;

		Locator instAlignDelta = Locator(kIdentity);

		success &= GetAnimAlignDelta(pInst, curAlign, instAlignDelta);

		accumAlignDelta = Lerp(accumAlignDelta, instAlignDelta, pInst->GetFade());
	}

	alignDeltaOut = accumAlignDelta;

	alignDeltaOut.SetRotation(Normalize(alignDeltaOut.GetRotation()));

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleLayer::GetAnimAlignDelta(const AnimSimpleInstance* pInst, const Locator& curAlign, Locator& alignDelta) const
{
	const float oldPhase = pInst->GetPrevPhase();
	const float newPhase = pInst->GetPhase();

	bool success = true;

	if (pInst->IsAlignedToActionPackOrigin())
	{
		Locator newAlign = curAlign;
		NdAnimAlign::DetermineAlignWs(pInst, curAlign, newAlign);

		alignDelta = curAlign.UntransformLocator(newAlign);
	}
	else
	{
		if (newPhase >= oldPhase)
		{
			success = GetAnimAlignDeltaRange(pInst, oldPhase, newPhase, alignDelta);
		}
		else
		{
			Locator animDeltaA = Locator(kIdentity);
			Locator animDeltaB = Locator(kIdentity);

			success &= GetAnimAlignDeltaRange(pInst, oldPhase, 1.0f, animDeltaA);
			success &= GetAnimAlignDeltaRange(pInst, 0.0f, newPhase, animDeltaB);

			const Quat rotDelta = animDeltaA.Rot() * animDeltaB.Rot();
			const Point posDelta = animDeltaA.Pos() + (animDeltaB.Pos() - kOrigin);

			ANIM_ASSERT(IsFinite(posDelta));
			ANIM_ASSERT(IsFinite(rotDelta));

			alignDelta.SetRotation(rotDelta);
			alignDelta.SetTranslation(posDelta);
		}
	}

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleLayer::GetAnimAlignDeltaRange(const AnimSimpleInstance* pInst, 
											 float startPhase, 
											 float endPhase,
											 Locator& alignDelta) const
{
	Locator startLoc = Locator(kIdentity);
	Locator endLoc = Locator(kIdentity);

	const ArtItemAnim* pArtItemAnim = pInst->GetAnim().ToArtItem();

	if (!pArtItemAnim)
	{
		return false;
	}

	ndanim::JointParams startParams;
	ndanim::JointParams endParams;

	EvaluateChannelParams startEvalParams;
	startEvalParams.m_pAnim = pArtItemAnim;
	startEvalParams.m_channelNameId = SID("align");
	startEvalParams.m_phase = startPhase;
	startEvalParams.m_mirror = pInst->IsFlipped();
	
	EvaluateChannelParams endEvalParams(startEvalParams);
	endEvalParams.m_phase = endPhase;

	bool success = true;
	success &= EvaluateChannelInAnim(GetAnimTable()->GetSkelId(), &startEvalParams, &startParams);
	success &= EvaluateChannelInAnim(GetAnimTable()->GetSkelId(), &endEvalParams, &endParams);

	startLoc = Locator(startParams.m_trans, startParams.m_quat);
	endLoc = Locator(endParams.m_trans, endParams.m_quat);

	if (success)
	{
		alignDelta = startLoc.UntransformLocator(endLoc);
	}
	else
	{
		alignDelta = Locator(kIdentity);
	}

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::ForceRefreshAnimPointers() 
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimSimpleInstance* pCurrentInst = m_pInstanceList[i];
		pCurrentInst->ForceRefreshAnimPointers();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::DebugOnly_ForceUpdateOverlaySnapshot(AnimOverlaySnapshot* pNewSnapshot)
{
	m_pOverlaySnapshot = pNewSnapshot;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::ShowError(const char* szErr) const
{
	STRIP_IN_FINAL_BUILD;
	m_szDebugPrintError = szErr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleLayer::IsValid() const
{
	return CurrentInstance() != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimSimpleInstance* AnimSimpleLayer::CurrentInstance() const
{
	return m_numInstances > 0 ? m_pInstanceList[0] : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSimpleInstance* AnimSimpleLayer::CurrentInstance()
{
	return m_numInstances > 0 ? m_pInstanceList[0] : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::DeleteNonContributingInstances()
{
	bool parentFullyBlendedIn = false;

	const U32F numInstances = m_numInstances;
	for (U32F i = 0; i < numInstances; ++i)
	{
		const AnimSimpleInstance* pCurrentInst = m_pInstanceList[i];
		if (parentFullyBlendedIn)
		{
			// Ok, let's remove this instance. Find the instance index and clear it.
			U64 usedInstanceIndex = 0;
			bool found = false;
			for (U32F j = 0; j < m_numAllocatedInstances; ++j)
			{
				if (m_pInstanceList[i] == &m_pAllocatedInstances[j])
				{
					found = true;
					break;
				}
				else
				{
					++usedInstanceIndex;
				}
			}
			ANIM_ASSERT(found);
			m_usedInstances.ClearBit(usedInstanceIndex);
		}
		else if (pCurrentInst->GetFade() == 1.0f)
		{
			parentFullyBlendedIn = true;

			// All remaining instances will be cleared due to them not having any effect any longer.
			m_numInstances = i + 1;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::TakeTransitions()
{
	// If we already are using up all instances we avoid doing anything this frame.
	// We might want to do something but that would result in a pop.
	if (m_numInstances == 4)
		return;

	//////////////////////////////////////////////////////////////////////////
	// TO BE DONE - Convert to take requests instead of immediate change - CGY
	//////////////////////////////////////////////////////////////////////////

	// 	while (m_requestsPending > 0)
	// 	{
	// 		StateChangeRequest& request = m_changeRequestsPendingList[0];
	//
	// 		FadeToAnim();
	//
	// 		// Move request to the 'processed' queue and mark as failed.
	// 		RemoveChangeRequestByIndex(0, StateChangeRequest::kStatusFlagTaken);
	// 	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::UpdateInstancePhases(F32 deltaTime, const FgAnimData* pAnimData)
{
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		AnimSimpleInstance* pCurrentInst = m_pInstanceList[i];

		// Update the phase and play effects.
		pCurrentInst->PhaseUpdate(deltaTime, pAnimData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::DeleteAllInstances()
{
	m_usedInstances.ClearAllBits();
	m_numInstances = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleLayer::StepInternal(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData* pAnimData)
{
	PROFILE(Animation, AnimSimpleLayer_Step);

	//	MsgOut("## Anim ## - AnimSimpleLayer::StepInternal\n");

	//if (!m_numInstances)
	//	return;

	// Prefetch all instances
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		Memory::PrefetchForLoad(m_pInstanceList[i]);
	}

	// Delete instances that have faded out
	DeleteNonContributingInstances();

	// Take requested transitions before advancing the phase. Otherwise
	// there will be a sync-problem between controllers expecting a transition
	// to be taken and it never was.
	// 	TakeTransitions();

	// Update the phase of the states. Clamp at 1.0 and save carry-over phase if needed
	UpdateInstancePhases(deltaTime, pAnimData);

	if (m_fadeOutParams.m_enabled && m_numInstances)
	{
		const AnimSimpleInstance* pCurrInstance = CurrentInstance();
		F32 endPhase = MinMax01(m_fadeOutParams.m_endPhase);
		if (m_fadeOutParams.m_fadeEarly)
		{
			const ArtItemAnim* pCurAnim = pCurrInstance->GetAnim().ToArtItem();
			F32 phaseOut = pCurAnim->m_pClipData->m_phasePerFrame * pCurAnim->m_pClipData->m_framesPerSecond
						   * m_fadeOutParams.m_fadeTime;
			endPhase = endPhase - phaseOut;
		}
		if (pCurrInstance && !pCurrInstance->IsLooping() && pCurrInstance->GetPhase() >= endPhase)
		{
			Fade(0.0f, m_fadeOutParams.m_fadeTime, m_fadeOutParams.m_blendType);
			m_fadeOutParams.m_enabled = false;
		}
	}

	// Delete instances that have faded out
	DeleteNonContributingInstances();

	// Fade in/out the layer
	AnimLayer::BeginStep(deltaTime, nullptr, pAnimData);

	// If the layer is fully faded out and wants to be faded out we remove all states.
	if (GetCurrentFade() == 0.0f && GetCurrentFade() == GetDesiredFade())
	{
		DeleteAllInstances();
	}

	// Get all triggered EFFs
	for (U32F i = 0; i < m_numInstances; ++i)
	{
		const AnimSimpleInstance* pInstance = m_pInstanceList[i];
		const ArtItemAnim* pAnim = pInstance->GetAnim().ToArtItem();
		if (pAnim && pAnim->m_pEffectAnim)
		{
			const ndanim::ClipData* pClipData = pAnim->m_pClipData;

			const float maxFrameSample = static_cast<float>(pInstance->GetFrameCount()) - 1.0f;
			const float mayaFramesCompensate = 30.0f * pClipData->m_secondsPerFrame;
			const float maxMayaFrameIndex = maxFrameSample * mayaFramesCompensate;

			F32 iOldFrame = pInstance->GetPrevPhase() * maxMayaFrameIndex;
			F32 iNewFrame = pInstance->GetPhase() * maxMayaFrameIndex;

			bool playingForward = (pInstance->GetPlaybackRate() >= 0.0f);

			const EffectAnim* pEffectAnim = pAnim->m_pEffectAnim;
			EffectGroup::GetTriggeredEffects(pEffectAnim,
											 iOldFrame, iNewFrame, maxMayaFrameIndex,
											 playingForward,
											 pInstance->IsLooping(),
											 false, /*not flipped*/
											 i == 0 /*topState*/,
											 GetCurrentFade(),
											 0.0f,
											 pInstance->GetAnimId(),
											 pTriggeredEffects,
											 pInstance);
		}
	}
}
