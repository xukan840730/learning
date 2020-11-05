/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/process/bound-frame.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/anim/self-blend.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimControl;
class AnimLayer;

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimAction
{
public:
	enum FinishCondition
	{
		kFinishOnTransitionTaken,
		kFinishOnNonTransitionalStateReached,
		kFinishOnAnimEnd,
		kFinishOnAnimEndEarly,
		kFinishOnAnimEndEarlyByBlend,
		kFinishOnLoopingAnimEnd,
		kFinishOnBlendedIn,
		kFinishOnBlendedOut,
	};

	AnimAction();
	AnimAction(StringId64 layerId);

	StringId64 GetLayerId() const { return m_layerId; }
	StringId64 GetTransitionIdOrFadeToStateId() const { return m_transitionId; }
	bool GetApReference(const AnimControl* pAnimControl, BoundFrame* pApRefOut) const;
	bool SetApReference(AnimControl* pAnimControl, const BoundFrame& apRef) const;

	void Reset();

	bool Request(AnimControl* pControl,
				 StringId64 transitionId,
				 FinishCondition finishCond,
				 const BoundFrame* pApRef = nullptr,
				 float startPhase = -1.0f,
				 StringId64 apRefChannel = INVALID_STRING_ID_64);

	bool RequestDeferred(AnimControl* pControl,
						 StringId64 transitionId,
						 float triggerPhase,
						 FinishCondition finishCond,
						 const BoundFrame* pApRef = nullptr,
						 float startPhase = -1.0f);

	void FadeToState(AnimControl* pControl,
					 StringId64 stateId,
					 const FadeToStateParams& params,
					 FinishCondition finishCondition = kFinishOnAnimEnd);

	bool FadeToAnim(AnimControl* pControl,
					StringId64 nameId,
					float startPhase,
					float fadeTime,
					DC::AnimCurveType blendType = DC::kAnimCurveTypeUniformS,
					bool freezeSrc = false,
					bool mirror = false,
					FinishCondition finishCondition = kFinishOnAnimEnd);

	bool FadeToAnim(AnimControl* pControl,
					StringId64 nameId,
					float startPhase,
					float fadeInTime,
					DC::AnimCurveType blendType,
					const AnimSimpleLayer::FadeOutOnCompleteParams& fadeOutParams,
					bool freezeSrc,
					bool mirror,
					FinishCondition finishCondition);

	void HandleManualFadeToState(AnimControl* pControl,
								 StringId64 stateId,
								 StateChangeRequest::ID requestId,
								 FinishCondition finishCondition = kFinishOnAnimEnd);

	void Update(AnimControl* pControl);

	bool IsDone() const
	{
		return m_animActionStatus != kAnimActionPending;
	}

	bool Failed() const
	{
		return m_animActionStatus == kAnimActionFailed;
	}

	bool Succeeded() const
	{
		return m_animActionStatus == kAnimActionCompleted;
	}

	bool IsValid() const
	{
		return m_animActionStatus != kAnimActionInvalid;
	}

	bool HasTransitionBeenProcessed() const
	{
		switch (m_stateChangeRequestStatus)
		{
		case StateChangeRequest::kStatusFlagPending:
		case StateChangeRequest::kStatusFlagInvalid:
			return false;

		default:
			return true;
		}
	}

	bool WasTransitionTakenThisFrame() const;
	bool CanTransitionBeTakenThisFrame(const AnimControl* pAnimControl, StringId64 transitionId) const;
	AnimStateInstance* GetTransitionDestInstance(AnimControl* pAnimControl);
	const AnimStateInstance* GetTransitionDestInstance(const AnimControl* pAnimControl) const;
	bool IsTopInstance(const AnimControl* pAnimControl) const;

	// returns -1 if the desired animation is not playing
	float GetAnimPhase(const AnimControl* pControl) const;
	float GetAnimFrame(const AnimControl* pControl) const;
	float GetMayaMaxFrame(const AnimControl* pControl) const;
	float GetDuration(const AnimControl* pAnimControl) const;

	// returns INVALID_STRING_ID_64 if the desired animation is not playing
	StringId64 GetPhaseAnimNameId(const AnimControl* pAnimControl) const;

	StateChangeRequest::StatusFlag GetTransitionStatus() const { return m_stateChangeRequestStatus; }
	StateChangeRequest::ID GetStateChangeRequestId() const { return m_stateChangeRequestId; }
	void SetBlendOutTime(F32 blendOutTime) { m_blendTime = blendOutTime; }

protected:
	virtual void InitStateChangeRequestStatus(AnimStateLayer* pLayer);

	void CheckFinishCondition(AnimControl* pControl);
	float GetCurrentInstancePhase(const AnimLayer* pLayer) const;

	enum AnimActionStatus
	{
		kAnimActionInvalid,
		kAnimActionPending,
		kAnimActionCompleted,
		kAnimActionFailed,
	};

	bool m_fadeRequested;
	bool m_deferredApValid;
	StringId64 m_layerId;
	StringId64 m_transitionId;
	StringId64 m_loopingAnimStateId;
	StringId64 m_customApRefId;
	StateChangeRequest::ID m_stateChangeRequestId;
	StateChangeRequest::StatusFlag m_stateChangeRequestStatus;
	StateChangeRequest::StatusFlag m_lastStateChangeRequestStatus;
	float m_accumulatedTime;
	float m_startPhase;
	float m_deferredPhase;
	FinishCondition m_finishCondition;
	AnimActionStatus m_animActionStatus;
	BoundFrame m_deferredAp;
	F32 m_blendTime;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimActionWithSelfBlend : public AnimAction
{
public:
	typedef AnimAction ParentClass;

	AnimActionWithSelfBlend();

	void Reset();
	void ClearSelfBlendParams();
	void ConfigureSelfBlend(NdGameObject* pOwner, const SelfBlendAction::Params& sbParams);

	// deprecated
	void SetSelfBlendParams(const DC::SelfBlendParams* pParams,
							const BoundFrame& finalApRef,
							NdGameObject* pOwner,
							float constraintPhase = -1.0f);

	void SetSelfBlendParams(const DC::SelfBlendParams* pParams,
							NdGameObject* pOwner,
							float constraintPhase = -1.0f)
	{
		SetSelfBlendParams(pParams, BoundFrame(kIdentity), pOwner, constraintPhase);
	}

	bool IsSelfBlendComplete() const;
	bool IsSelfBlendPending() const;
	float GetSelfBlendCompletion() const;

	void SetSelfBlendApRef(const BoundFrame& apRef);

	SelfBlendAction* GetSelfBlendAction() { return m_hSelfBlend.ToSubsystem(); }

private:
	virtual void InitStateChangeRequestStatus(AnimStateLayer* pLayer) override;

	SelfBlendHandle m_hSelfBlend;
};
