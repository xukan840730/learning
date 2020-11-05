/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/investigate-controller.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/util/common.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/nav-control.h"

#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-util.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/scriptx/h/anim-npc-info.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AiInvestigateController : public IAiInvestigateController
{
private:
	AnimAction  m_animAction;

	bool RequestInvestigateAnimationInternal(StringId64 stateId)
	{
		NavCharacter* pChar = GetCharacter();
		if (!pChar)
			return false;

		AnimControl* pAnimControl = pChar->GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

		FadeToStateParams params;
		params.m_animFadeTime = 0.4f;

		m_animAction.FadeToState(pAnimControl, stateId, params, AnimAction::kFinishOnNonTransitionalStateReached);

		NavAnimHandoffDesc handoff;
		handoff.SetStateChangeRequestId(m_animAction.GetStateChangeRequestId(), stateId);
		handoff.m_steeringPhase = -1.0f;

		pChar->ConfigureNavigationHandOff(handoff, FILE_LINE_FUNC);

		return true;
	}

public:
	AiInvestigateController()
	{
	}

	virtual void UpdateStatus() override
	{
		const Character* pChar = GetCharacter();
		if (!pChar)
			return;

		AnimControl* pAnimControl = pChar->GetAnimControl();
		if (!pAnimControl)
			return;

		m_animAction.Update(pAnimControl);
	}

	virtual bool IsPlayingInvestigateAnimation() const override
	{
		return !m_animAction.IsDone();
	}

	virtual bool ShouldInterruptNavigation() const override
	{
		return !m_animAction.IsDone();
	}

	virtual bool IsBusy() const override
	{
		return !m_animAction.IsDone();
	}

	virtual void Reset() override
	{
		m_animAction.Reset();
	}

	virtual void Interrupt() override
	{
		NavCharacter* pChar = GetCharacter();
		if (!m_animAction.IsDone())
		{
			//pChar->StopAndStand(0.0f, FILE_LINE_FUNC);
			m_animAction.Request(pChar->GetAnimControl(), SID("interrupt"), AnimAction::kFinishOnTransitionTaken);
		}
	}

	virtual bool RequestLookAtDistantPoint() override
	{
		return RequestInvestigateAnimationInternal(SID("s_investigate-look-at-distant-point"));
	}

	virtual bool RequestLookAround() override
	{
		return RequestInvestigateAnimationInternal(SID("s_investigate-look-around"));
	}

	virtual bool RequestLookAroundShort() override
	{
		return RequestInvestigateAnimationInternal(SID("s_investigate-look-around-short"));
	}

	virtual bool RequestLookAroundDeadBody() override
	{
		return RequestInvestigateAnimationInternal(SID("s_investigate-look-around-dead-body"));
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiInvestigateController* CreateAiInvestigateController()
{
	return NDI_NEW AiInvestigateController;
}

