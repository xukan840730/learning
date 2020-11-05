/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-action.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AnimAction::AnimAction(StringId64 layerId) : m_layerId(layerId)
{
	ANIM_ASSERT(m_layerId != INVALID_STRING_ID_64);
	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimAction::AnimAction() : m_layerId(SID("base"))
{
	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimAction::Reset()
{
	m_fadeRequested		   = false;
	m_transitionId		   = INVALID_STRING_ID_64;
	m_stateChangeRequestId = StateChangeRequest::kInvalidId;
	m_lastStateChangeRequestStatus = m_stateChangeRequestStatus = StateChangeRequest::kStatusFlagInvalid;
	m_accumulatedTime	 = 0.0f;
	m_finishCondition	 = AnimAction::kFinishOnNonTransitionalStateReached;
	m_animActionStatus	 = kAnimActionInvalid;
	m_loopingAnimStateId = INVALID_STRING_ID_64;
	m_customApRefId		 = INVALID_STRING_ID_64;
	m_deferredPhase		 = -1.0f;
	m_deferredAp		 = BoundFrame(kIdentity);
	m_deferredApValid	 = false;
	m_startPhase		 = -1.0f;
	m_blendTime = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAction::Request(AnimControl* pControl,
						 StringId64 transitionId,
						 FinishCondition finishCond,
						 const BoundFrame* pApRef /* = nullptr */,
						 float startPhase /* = -1.0f */,
						 StringId64 apRefChannel /*= INVALID_STRING_ID_64*/)
{
	Reset();

	AnimStateLayer* pLayer = pControl->GetStateLayerById(m_layerId);
	ANIM_ASSERT(pLayer);

	m_transitionId	  = transitionId;
	m_finishCondition = finishCond;
	m_startPhase	  = startPhase;

	FadeToStateParams params;
	if (pApRef)
	{
		params.m_apRef = *pApRef;
		params.m_apRefValid = true;
	}
	params.m_stateStartPhase = startPhase;
	params.m_customApRefId = apRefChannel;
	m_stateChangeRequestId = pLayer->RequestTransition(m_transitionId, &params);

	InitStateChangeRequestStatus(pLayer);

	return pLayer->IsTransitionValid(transitionId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAction::RequestDeferred(AnimControl* pControl,
								 StringId64 transitionId,
								 float triggerPhase,
								 FinishCondition finishCond,
								 const BoundFrame* pApRef /* = nullptr */,
								 float startPhase /* = -1.0f */)
{
	if ((triggerPhase <= 0.0f) || (triggerPhase > 1.0f))
	{
		return Request(pControl, transitionId, finishCond, pApRef, startPhase);
	}

	Reset();

	m_deferredPhase	  = triggerPhase;
	m_deferredApValid = pApRef != nullptr;
	m_deferredAp	  = pApRef ? *pApRef : BoundFrame(kIdentity);

	m_transitionId	  = transitionId;
	m_finishCondition = finishCond;
	m_startPhase	  = startPhase;

	m_lastStateChangeRequestStatus = m_stateChangeRequestStatus = StateChangeRequest::kStatusFlagPending;
	m_animActionStatus = kAnimActionPending;

	AnimStateLayer* pLayer = pControl->GetStateLayerById(m_layerId);
	ANIM_ASSERT(pLayer);

	return pLayer->IsTransitionValid(transitionId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimAction::FadeToState(AnimControl* pControl,
							 StringId64 stateId,
							 const FadeToStateParams& params,
							 FinishCondition finishCondition /* = kFinishOnAnimEnd */)
{
	Reset();

	AnimStateLayer* pLayer = pControl->GetStateLayerById(m_layerId);
	ANIM_ASSERT(pLayer);
	if (!pLayer)
		return;

	pLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	m_fadeRequested	  = true;
	m_transitionId	  = stateId;
	m_finishCondition = finishCondition;
	m_customApRefId	  = params.m_customApRefId;

	m_stateChangeRequestId = pLayer->FadeToState(stateId, params);

	InitStateChangeRequestStatus(pLayer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAction::FadeToAnim(AnimControl* pControl,
							StringId64 nameId,
							float startPhase,
							float fadeTime,
							DC::AnimCurveType blendType /* = DC::kAnimCurveTypeUniformS */,
							bool freezeSrc /* = false */,
							bool mirror /* = false */,
							FinishCondition finishCondition /* = kFinishOnAnimEnd */)
{
	AnimSimpleLayer::FadeOutOnCompleteParams defaultFadeOut;
	return FadeToAnim(pControl, nameId, startPhase, fadeTime, blendType, defaultFadeOut, freezeSrc, mirror, finishCondition);
}

bool AnimAction::FadeToAnim(AnimControl* pControl,
	                        StringId64 nameId,
	                        float startPhase,
	                        float fadeInTime,
	                        DC::AnimCurveType blendType,
	                        const AnimSimpleLayer::FadeOutOnCompleteParams &fadeOutParams,
	                        bool freezeSrc,
	                        bool mirror,
	                        FinishCondition finishCondition)
{
	Reset();

	AnimSimpleLayer* pLayer = pControl->GetSimpleLayerById(m_layerId);
	ANIM_ASSERT(pLayer);

	m_transitionId = nameId;

	AnimSimpleLayer::FadeRequestParams params;
	params.m_startPhase = startPhase;
	params.m_fadeTime = fadeInTime;
	params.m_blendType = blendType;
	params.m_freezeSrc = freezeSrc;
	params.m_mirror = mirror;
	params.m_phaseMode = DC::kAnimatePhaseExplicit;
	params.m_layerFadeOutParams = fadeOutParams;
	m_blendTime = fadeInTime;

	const bool success = pLayer->RequestFadeToAnim(nameId, params);

	m_animActionStatus = success ? kAnimActionPending : kAnimActionFailed;
	m_lastStateChangeRequestStatus = m_stateChangeRequestStatus = success ? StateChangeRequest::kStatusFlagTaken
		: StateChangeRequest::kStatusFlagFailed;
	m_finishCondition = finishCondition;

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimAction::HandleManualFadeToState(AnimControl* pControl,
										 StringId64 stateId,
										 StateChangeRequest::ID requestId,
										 FinishCondition finishCondition /* = kFinishOnAnimEnd */)
{
	Reset();
	AnimStateLayer* pLayer = pControl->GetStateLayerById(m_layerId);
	ANIM_ASSERT(pLayer);
	if (!pLayer)
		return;

	m_fadeRequested		   = true;
	m_transitionId		   = stateId;
	m_finishCondition	   = finishCondition;
	m_stateChangeRequestId = requestId;

	InitStateChangeRequestStatus(pLayer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimAction::Update(AnimControl* pControl)
{
	m_lastStateChangeRequestStatus = m_stateChangeRequestStatus;

	if (m_animActionStatus == kAnimActionPending)
	{
		AnimLayer* pLayer = pControl->GetLayerById(m_layerId);
		ANIM_ASSERT(pLayer);

		if (m_deferredPhase >= 0.0f)
		{
			const float curPhase = GetCurrentInstancePhase(pLayer);

			if (curPhase >= m_deferredPhase)
			{
				const BoundFrame deferredAp = m_deferredAp;
				Request(pControl,
						m_transitionId,
						m_finishCondition,
						m_deferredApValid ? &deferredAp : nullptr,
						m_startPhase);
			}
			else
			{
				return;
			}
		}

		if (pLayer->GetType() == kAnimLayerTypeSimple)
		{
			CheckFinishCondition(pControl);
		}
		else
		{
			AnimStateLayer* pStateLayer = static_cast<AnimStateLayer*>(pLayer);

			// If the requested animation transition is still pending
			switch (m_stateChangeRequestStatus)
			{
			case StateChangeRequest::kStatusFlagInvalid:
				{
					m_animActionStatus = kAnimActionInvalid;
					break;
				}

			case StateChangeRequest::kStatusFlagFailed:
				{
					m_animActionStatus = kAnimActionFailed;
					break;
				}

			case StateChangeRequest::kStatusFlagPending:
				{
					m_stateChangeRequestStatus = pStateLayer->GetTransitionStatus(m_stateChangeRequestId);

					switch (m_stateChangeRequestStatus)
					{
					case StateChangeRequest::kStatusFlagInvalid:
					case StateChangeRequest::kStatusFlagFailed:
						m_animActionStatus = kAnimActionFailed;
						break;

						// The state change request was just serviced last AnimUpdate
						// Let's see if we are done according to the finish condition
					case StateChangeRequest::kStatusFlagTaken:
						CheckFinishCondition(pControl);
						break;
					}
					break;
				}

			case StateChangeRequest::kStatusFlagTaken:
				{
					CheckFinishCondition(pControl);
					break;
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimAction::CheckFinishCondition(AnimControl* pControl)
{
	AnimLayer* pLayer = pControl->GetLayerById(m_layerId);
	ANIM_ASSERT(pLayer);
	if (pLayer->GetType() == kAnimLayerTypeSimple)
	{
		AnimSimpleLayer* pSimpleLayer		= static_cast<AnimSimpleLayer*>(pLayer);
		AnimSimpleInstance* pSimpleInstance = pSimpleLayer->CurrentInstance();

		float endPhase = 1.0f;

		if (m_finishCondition == AnimAction::kFinishOnAnimEndEarly)
		{
			endPhase = 0.8f;
		}
		else if (m_finishCondition == AnimAction::kFinishOnAnimEndEarlyByBlend && pSimpleInstance)
		{
			if (m_blendTime > 0.0f)
			{
				endPhase = 1.0f
						   - (m_blendTime * pSimpleInstance->GetFramesPerSecond() / pSimpleInstance->GetFrameCount());
			}
			else
			{
				endPhase = 1.0f;
			}
		}

		if (pSimpleInstance == nullptr || pSimpleInstance->GetPhase() >= endPhase)
		{
			m_animActionStatus = kAnimActionCompleted;
		}
	}
	else
	{
		AnimStateLayer* pStateLayer = static_cast<AnimStateLayer*>(pLayer);
		const AnimStateInstance* pDestInstance	  = GetTransitionDestInstance(pControl);
		const AnimStateInstance* pCurrentInstance = pStateLayer->CurrentStateInstance();
		const StringId64 currentStateId = pCurrentInstance ? pCurrentInstance->GetStateName() : INVALID_STRING_ID_64;

		switch (m_finishCondition)
		{
		case AnimAction::kFinishOnTransitionTaken:
			m_animActionStatus = kAnimActionCompleted;
			break;

		case AnimAction::kFinishOnNonTransitionalStateReached:
			if (pStateLayer->IsInNonTransitionalState())
			{
				m_animActionStatus = kAnimActionCompleted;
			}

			// If we are waiting for a non transitional state but the state we are in does not contain an
			// auto transition to another state we need to abort or else we will be stuck forever.
			if (pCurrentInstance && (pCurrentInstance->Phase() == 1.0f)
				&& !pStateLayer->CanTransitionBeTakenThisFrame(SID("auto")))
			{
				m_animActionStatus = kAnimActionCompleted;
			}
			break;

		case AnimAction::kFinishOnAnimEnd:
			if (pDestInstance)
			{
				if (pDestInstance != pCurrentInstance)
				{
					m_animActionStatus = kAnimActionCompleted;
				}
				else if (pDestInstance->Phase() >= 1.0f)
				{
					m_animActionStatus = kAnimActionCompleted;
				}
			}
			else
			{
				m_animActionStatus = kAnimActionCompleted;
			}
			break;

		case AnimAction::kFinishOnAnimEndEarly:
			if (pDestInstance)
			{
				const float maxMayaFrame	   = pDestInstance->MayaMaxFrame();
				const float curMayaFrame	   = pDestInstance->MayaFrame();
				const float remainingMayaFrame = maxMayaFrame - curMayaFrame;
				if (pDestInstance != pCurrentInstance)
				{
					m_animActionStatus = kAnimActionCompleted;
				}
				else if (remainingMayaFrame < 6.0f)
				{
					m_animActionStatus = kAnimActionCompleted;
				}
			}
			else
			{
				m_animActionStatus = kAnimActionCompleted;
			}
			break;
		case AnimAction::kFinishOnAnimEndEarlyByBlend:
			if (pDestInstance)
			{
				const float frames = Max(m_blendTime * 29.97f, 0.0f);
				const float maxMayaFrame = pDestInstance->MayaMaxFrame();
				const float curMayaFrame = pDestInstance->MayaFrame();
				const float remainingMayaFrame = maxMayaFrame - curMayaFrame;
				if (pDestInstance != pCurrentInstance)
				{
					m_animActionStatus = kAnimActionCompleted;
				}
				else if (remainingMayaFrame <= frames)
				{
					m_animActionStatus = kAnimActionCompleted;
				}
			}
			else
			{
				m_animActionStatus = kAnimActionCompleted;
			}
			break;


		case AnimAction::kFinishOnLoopingAnimEnd:
			if (pDestInstance)
			{
				if (m_loopingAnimStateId == INVALID_STRING_ID_64)
				{
					m_loopingAnimStateId = pDestInstance->GetStateName();
				}

				if (pDestInstance != pCurrentInstance)
				{
					m_animActionStatus = kAnimActionCompleted;
				}
			}
			else
			{
				if ((m_loopingAnimStateId == INVALID_STRING_ID_64)
					|| pCurrentInstance->GetStateName() != m_loopingAnimStateId)
				{
					m_animActionStatus = kAnimActionCompleted;
				}
			}
			break;

		case AnimAction::kFinishOnBlendedOut:
			if (pDestInstance)
			{
				if (pDestInstance != pCurrentInstance)
				{
					if (pCurrentInstance->AnimFade() >= 1.0f)
					{
						m_animActionStatus = kAnimActionCompleted;
					}
				}
				else if (pDestInstance->Phase() >= 1.0f)
				{
					m_animActionStatus = kAnimActionCompleted;
				}
			}
			else
			{
				m_animActionStatus = kAnimActionCompleted;
			}
			break;

		case AnimAction::kFinishOnBlendedIn:
			if (pDestInstance)
			{
				const float mf = pDestInstance->MasterFade();
				if (mf >= 1.0f)
				{
					m_animActionStatus = kAnimActionCompleted;
				}
			}
			else
			{
				m_animActionStatus = kAnimActionCompleted;
			}
			break;

		default:
			ANIM_ASSERTF(false, ("Unknown finish condition '%d'", m_finishCondition));
			m_animActionStatus = kAnimActionCompleted;
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAction::WasTransitionTakenThisFrame() const
{
	return m_stateChangeRequestStatus == StateChangeRequest::kStatusFlagTaken
		   && m_lastStateChangeRequestStatus != StateChangeRequest::kStatusFlagTaken;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAction::CanTransitionBeTakenThisFrame(const AnimControl* pAnimControl, StringId64 transitionId) const
{
	const AnimStateLayer* pLayer = pAnimControl->GetStateLayerById(m_layerId);
	ANIM_ASSERT(pLayer);
	return pLayer->CanTransitionBeTakenThisFrame(transitionId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* AnimAction::GetTransitionDestInstance(AnimControl* pAnimControl)
{
	AnimStateInstance* pInst = nullptr;
	if (IsValid() && (m_stateChangeRequestStatus == StateChangeRequest::kStatusFlagTaken))
	{
		AnimStateLayer* pLayer = pAnimControl->GetStateLayerById(m_layerId);
		ANIM_ASSERT(pLayer);
		pInst = pLayer->GetTransitionDestInstance(m_stateChangeRequestId);
	}
	return pInst;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimAction::GetTransitionDestInstance(const AnimControl* pAnimControl) const
{
	const AnimStateInstance* pInst = nullptr;
	if (m_stateChangeRequestStatus == StateChangeRequest::kStatusFlagTaken)
	{
		const AnimStateLayer* pLayer = pAnimControl->GetStateLayerById(m_layerId);
		ANIM_ASSERT(pLayer);
		pInst = pLayer->GetTransitionDestInstance(m_stateChangeRequestId);
	}
	return pInst;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAction::IsTopInstance(const AnimControl* pAnimControl) const
{
	if (const AnimStateLayer* pLayer = pAnimControl->GetStateLayerById(m_layerId))
	{
		const AnimStateInstance* pTopInstance = pLayer->CurrentStateInstance();
		return pTopInstance && (pTopInstance == GetTransitionDestInstance(pAnimControl));
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimAction::GetPhaseAnimNameId(const AnimControl* pAnimControl) const
{
	if (m_stateChangeRequestStatus == StateChangeRequest::kStatusFlagPending)
		return INVALID_STRING_ID_64;

	if (m_stateChangeRequestStatus != StateChangeRequest::kStatusFlagTaken)
		return INVALID_STRING_ID_64;

	const AnimLayer* pLayer = pAnimControl->GetLayerById(m_layerId);
	ANIM_ASSERT(pLayer);

	switch (pLayer->GetType())
	{
	case kAnimLayerTypeState:
		{
			const AnimStateInstance* pInst = GetTransitionDestInstance(pAnimControl);
			if (!pInst)
				return INVALID_STRING_ID_64;

			return pInst->GetPhaseAnim();
		}

	case kAnimLayerTypeSimple:
		{
			const AnimSimpleInstance *pInst = ((const AnimSimpleLayer*)pLayer)->CurrentInstance();
			if (!pInst)
				return INVALID_STRING_ID_64;

			return pInst->GetAnimId();
		}
	default:
		return INVALID_STRING_ID_64;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimAction::GetAnimPhase(const AnimControl* pAnimControl) const
{
	if (m_stateChangeRequestStatus == StateChangeRequest::kStatusFlagPending)
		return 0.0f;

	if (m_stateChangeRequestStatus != StateChangeRequest::kStatusFlagTaken)
		return -1.0f;

	const AnimLayer* pLayer = pAnimControl->GetLayerById(m_layerId);
	ANIM_ASSERT(pLayer);

	switch (pLayer->GetType())
	{
	case kAnimLayerTypeState:
		{
			const AnimStateInstance* pInst = GetTransitionDestInstance(pAnimControl);
			if (!pInst)
				return -1.0f;

			return pInst->Phase();
		}

	case kAnimLayerTypeSimple:
		{
			const AnimSimpleInstance *pInst = ((const AnimSimpleLayer*)pLayer)->CurrentInstance();
			if (!pInst)
				return -1.0f;

			return pInst->GetPhase();
		}

	default:
		return -1.0f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimAction::GetDuration(const AnimControl* pAnimControl) const
{
	if (m_stateChangeRequestStatus == StateChangeRequest::kStatusFlagPending)
		return 0.0f;

	if (m_stateChangeRequestStatus != StateChangeRequest::kStatusFlagTaken)
		return -1.0f;

	const AnimLayer* pLayer = pAnimControl->GetLayerById(m_layerId);
	ANIM_ASSERT(pLayer);

	switch (pLayer->GetType())
	{
	case kAnimLayerTypeState:
	{
		const AnimStateInstance* pInst = GetTransitionDestInstance(pAnimControl);
		if (!pInst)
			return -1.0f;

		return pInst->GetDuration();
	}

	case kAnimLayerTypeSimple:
		{
			const AnimSimpleInstance *pInst = ((const AnimSimpleLayer*)pLayer)->CurrentInstance();
			if (!pInst)
				return -1.0f;

			return pInst->GetDuration();
		}

	default:
		return -1.0f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimAction::GetMayaMaxFrame(const AnimControl* pAnimControl) const
{
	if (m_stateChangeRequestStatus == StateChangeRequest::kStatusFlagPending)
		return 0.0f;

	if (m_stateChangeRequestStatus != StateChangeRequest::kStatusFlagTaken)
		return -1.0f;

	const AnimLayer* pLayer = pAnimControl->GetLayerById(m_layerId);
	ANIM_ASSERT(pLayer);

	switch (pLayer->GetType())
	{
	case kAnimLayerTypeState:
		{
			const AnimStateInstance* pInst = GetTransitionDestInstance(pAnimControl);
			if (!pInst)
				return -1.0f;

			return pInst->MayaMaxFrame();
		}

	case kAnimLayerTypeSimple:
		{
			const AnimSimpleInstance *pInst = ((const AnimSimpleLayer*)pLayer)->CurrentInstance();
			if (!pInst)
				return -1.0f;

			return (F32)pInst->GetFrameCount();
		}

	default:
		return -1.0f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimAction::GetAnimFrame(const AnimControl* pAnimControl) const
{
	if (m_stateChangeRequestStatus == StateChangeRequest::kStatusFlagPending)
		return 0.0f;

	if (m_stateChangeRequestStatus != StateChangeRequest::kStatusFlagTaken)
		return -1.0f;

	const AnimLayer* pLayer = pAnimControl->GetLayerById(m_layerId);
	ANIM_ASSERT(pLayer);

	switch (pLayer->GetType())
	{
	case kAnimLayerTypeState:
		{
			const AnimStateInstance* pInst = GetTransitionDestInstance(pAnimControl);
			if (!pInst)
				return -1.0f;

			return pInst->MayaFrame();
		}

	case kAnimLayerTypeSimple:
		{
			const AnimSimpleInstance *pInst = ((const AnimSimpleLayer*)pLayer)->CurrentInstance();
			if (!pInst)
				return -1.0f;

			return pInst->GetMayaFrame();
		}

	default:
		return -1.0f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAction::GetApReference(const AnimControl* pAnimControl, BoundFrame* pApRefOut) const
{
	if (!pApRefOut)
		return false;

	if (m_stateChangeRequestStatus == StateChangeRequest::kStatusFlagPending)
		return false;

	if (m_stateChangeRequestStatus != StateChangeRequest::kStatusFlagTaken)
		return false;

	const AnimLayer* pLayer = pAnimControl->GetLayerById(m_layerId);
	if (!pLayer)
		return false;

	switch (pLayer->GetType())
	{
	case kAnimLayerTypeState:
		{
			const AnimStateLayer* pStateLayer = (const AnimStateLayer*)pLayer;
			if (const AnimStateInstance* pInst = pStateLayer->CurrentStateInstance())
			{
				*pApRefOut = pInst->GetApLocator();
				return true;
			}
		}
		break;

	case kAnimLayerTypeSimple:
		{
			const AnimSimpleLayer* pSimpleLayer = (const AnimSimpleLayer*)pLayer;
			if (const AnimSimpleInstance* pInst = pSimpleLayer->CurrentInstance())
			{
				*pApRefOut = pInst->GetApOrigin();
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimAction::SetApReference(AnimControl* pAnimControl, const BoundFrame& apRef) const
{
	if (m_stateChangeRequestStatus == StateChangeRequest::kStatusFlagPending)
		return false;

	if (m_stateChangeRequestStatus != StateChangeRequest::kStatusFlagTaken)
		return false;

	const AnimLayer* pLayer = pAnimControl->GetLayerById(m_layerId);
	if (!pLayer)
		return false;

	switch (pLayer->GetType())
	{
	case kAnimLayerTypeState:
		{
			AnimStateLayer* pStateLayer = (AnimStateLayer*)pLayer;
			if (AnimStateInstance* pInst = pStateLayer->CurrentStateInstance())
			{
				pInst->SetApLocator(apRef);
				return true;
			}
		}
		break;

	case kAnimLayerTypeSimple:
		{
			AnimSimpleLayer* pSimpleLayer = (AnimSimpleLayer*)pLayer;
			if (AnimSimpleInstance* pInst = pSimpleLayer->CurrentInstance())
			{
				pInst->SetApOrigin(apRef);
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimAction::InitStateChangeRequestStatus(AnimStateLayer* pLayer)
{
	if (m_stateChangeRequestId != StateChangeRequest::kInvalidId)
	{
		m_lastStateChangeRequestStatus = m_stateChangeRequestStatus = pLayer->GetTransitionStatus(m_stateChangeRequestId);
		m_animActionStatus = kAnimActionPending;
	}
	else
	{
		m_lastStateChangeRequestStatus = m_stateChangeRequestStatus = StateChangeRequest::kStatusFlagFailed;
		m_animActionStatus = kAnimActionFailed;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimAction::GetCurrentInstancePhase(const AnimLayer* pLayer) const
{
	if (!pLayer)
		return 0.0f;

	float phase = 0.0f;

	const AnimLayerType type = pLayer->GetType();
	switch (type)
	{
	case kAnimLayerTypeSimple:
		{
			const AnimSimpleLayer* pSimpleLayer = static_cast<const AnimSimpleLayer*>(pLayer);
			const AnimSimpleInstance* pCurInst	= pSimpleLayer->CurrentInstance();
			phase = pCurInst ? pCurInst->GetPhase() : 0.0f;
		}
		break;

	case kAnimLayerTypeState:
		{
			const AnimStateLayer* pStateLayer = static_cast<const AnimStateLayer*>(pLayer);
			const AnimStateInstance* pCurInst = pStateLayer->CurrentStateInstance();
			phase = pCurInst ? pCurInst->Phase() : 0.0f;
		}
		break;
	}

	return phase;
}

/************************************************************************/
/*                                                                      */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
AnimActionWithSelfBlend::AnimActionWithSelfBlend()
{
	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActionWithSelfBlend::Reset()
{
	ClearSelfBlendParams();

	ParentClass::Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActionWithSelfBlend::ConfigureSelfBlend(NdGameObject* pOwner, const SelfBlendAction::Params& sbParams)
{
	SelfBlendAction* pSelfBlend = m_hSelfBlend.ToSubsystem();
	if (pSelfBlend)
	{
		if (sbParams.m_blendParams.m_phase < 0.0f)
		{
			pSelfBlend->Kill();
			pSelfBlend = nullptr;
		}
		else
		{
			pSelfBlend->UpdateParams(sbParams);
		}
	}
	else if ((sbParams.m_blendParams.m_phase >= 0.0f) && pOwner && pOwner->GetSubsystemMgr())
	{
		SubsystemSpawnInfo sbSpawnInfo(SID("SelfBlendAction"), pOwner);
		sbSpawnInfo.m_pUserData = &sbParams;
		pSelfBlend = (SelfBlendAction*)NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap,
														   sbSpawnInfo,
														   FILE_LINE_FUNC);

		m_hSelfBlend = pSelfBlend;
	}

	if (IsValid() && pSelfBlend && (pSelfBlend->GetActionState() == NdSubsystemAnimAction::ActionState::kUnattached)
		&& (m_stateChangeRequestId != StateChangeRequest::kInvalidId))
	{
		pSelfBlend->BindToAnimRequest(m_stateChangeRequestId, m_layerId);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActionWithSelfBlend::ClearSelfBlendParams()
{
	SelfBlendAction* pSelfBlend = m_hSelfBlend.ToSubsystem();
	if (pSelfBlend && (pSelfBlend->GetActionState() == NdSubsystemAnimAction::ActionState::kUnattached))
	{
		pSelfBlend->Kill();
	}
	m_hSelfBlend = SelfBlendHandle();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimActionWithSelfBlend::IsSelfBlendPending() const
{
	const SelfBlendAction* pSelfBlend = m_hSelfBlend.ToSubsystem();
	if (!pSelfBlend)
		return false;

	const float sbtt = pSelfBlend->GetCompletionPhase();

	return (sbtt >= 0.0f) && (sbtt < NDI_FLT_EPSILON);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActionWithSelfBlend::SetSelfBlendParams(const DC::SelfBlendParams* pParams,
												 const BoundFrame& finalApRef,
												 NdGameObject* pOwner,
												 float constraintPhase /* = -1.0f */)
{
	SelfBlendAction::Params sbParams;
	if (pParams)
	{
		sbParams.m_blendParams = *pParams;
	}
	sbParams.m_destAp = finalApRef;
	sbParams.m_constraintPhase = constraintPhase;

	ConfigureSelfBlend(pOwner, sbParams);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimActionWithSelfBlend::IsSelfBlendComplete() const
{
	const SelfBlendAction* pSelfBlend = m_hSelfBlend.ToSubsystem();

	const float sbtt = pSelfBlend ? pSelfBlend->GetCompletionPhase() : -1.0f;

	return sbtt >= 1.0f || sbtt < 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActionWithSelfBlend::SetSelfBlendApRef(const BoundFrame& apRef)
{
	if (SelfBlendAction* pSelfBlend = m_hSelfBlend.ToSubsystem())
	{
		pSelfBlend->UpdateDestApRef(apRef);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActionWithSelfBlend::InitStateChangeRequestStatus(AnimStateLayer* pLayer)
{
	ParentClass::InitStateChangeRequestStatus(pLayer);

	SelfBlendAction* pSelfBlend = m_hSelfBlend.ToSubsystem();

	if (pSelfBlend && (pSelfBlend->GetActionState() == NdSubsystemAnimAction::ActionState::kUnattached)
		&& (m_stateChangeRequestId != StateChangeRequest::kInvalidId))
	{
		pSelfBlend->BindToAnimRequest(m_stateChangeRequestId, m_layerId);
	}
}
