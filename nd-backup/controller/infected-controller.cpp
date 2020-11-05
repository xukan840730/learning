/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/infected-controller.h"

#include "corelib/util/random.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/ai/component/move-performance.h"

#include "game/ai/base/ai-game-util.h"
#include "game/ai/characters/infected.h"
#include "game/player/melee/process-melee-action.h"
#include "game/scriptx/h/hit-reactions-defines.h"

static const StringId64 kaThrowAnimId[] = 
{
	SID("throw-object"), 
	SID("throw-object-b"), 
	SID("throw-object-c"), 
};

static const StringId64 kaPartialThrowAnimId[] =
{
	SID("throw-partial"),
	SID("throw-partial-b"),
	SID("throw-partial-b"),
};

static const U32 kThrowAnimCount = ARRAY_COUNT(kaThrowAnimId);
STATIC_ASSERT(kThrowAnimCount > 0);

/// --------------------------------------------------------------------------------------------------------------- ///
class AiInfectedController : public IAiInfectedController
{
	AnimActionWithSelfBlend m_animAction;
	AnimAction m_partialAnimAction;

	bool	m_isThrowing		: 1;
	bool	m_isPartialThrowing : 1;
	U32		m_nextThrowAnimIdx	: 2;


public:
	/// --------------------------------------------------------------------------------------------------------------- ///
	AiInfectedController() 
		: m_animAction()
		, m_partialAnimAction(SID("infected-partial"))
		, m_isThrowing(false) 
		, m_isPartialThrowing(false)
		, m_nextThrowAnimIdx(RandomIntRange(0, kThrowAnimCount - 1))
	{ }

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void Reset() override 
	{ 
		m_animAction.Reset(); 
		m_partialAnimAction.Reset();
	}

	virtual void OnHitReactionPlayed() override { m_animAction.Reset(); }

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool IsBusy() const override { return !m_animAction.IsDone(); }

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool IsPartialBusy() const override { return !m_partialAnimAction.IsDone(); }

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual U64 CollectHitReactionStateFlags() const override
	{
		const Infected* pInfected = static_cast<const Infected*>(GetCharacter());
		if (!pInfected)
			return 0UL;

		if (pInfected->IsSleeping())
			return DC::kHitReactionStateMaskInfectedSleeping;

		const ProcessMeleeAction* pAction = pInfected->GetCurrentMeleeAction();

		if (pAction)
		{
			switch (pInfected->GetArchetype().GetCharacterClass())
			{
			case DC::kNpcCharacterClassInfectedClicker:
				if (pAction->IsPhantom() || !pAction->GetDefender())
					return DC::kHitReactionStateMaskInfectedFrenzy;
				break;
			case DC::kNpcCharacterClassInfectedBloater:
			case DC::kNpcCharacterClassInfectedRatking:
				if (pAction->IsPhantom())
					return DC::kHitReactionStateMaskInfectedMeleeCharge;
				break;
			}
		}

		if (m_isThrowing)
			return DC::kHitReactionStateMaskCombatThrowGrenade;

		return 0UL;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void UpdateStatus() override
	{
		PROFILE(AI, InfCon_UpdateStatus);

		const NavCharacter* pNavChar = GetCharacter();
		if (!pNavChar)
		{
			return;
		}

		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		if (!pAnimControl)
		{
			return;
		}

		m_animAction.Update(pAnimControl);
		m_partialAnimAction.Update(pAnimControl);

		if (!m_animAction.IsValid() || m_animAction.IsDone())
		{
			m_isThrowing = false;
		}

		if (!m_partialAnimAction.IsValid() || m_partialAnimAction.IsDone())
		{
			m_isPartialThrowing = false;
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool ShouldInterruptNavigation() const override
	{
		NavCharacter* pNavChar = GetCharacter();
		if (!pNavChar)
		{
			return false;
		}

		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		if (!pAnimControl)
		{
			return false;
		}

		return IsBusy();
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Use the demeanor-change transition to re-enter idle to trigger the animation
	virtual bool StartSleep() override
	{
		NavCharacter* pNavChar = GetCharacter();
		if (!pNavChar)
		{
			return false;
		}

		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		if (!pAnimControl)
		{
			return false;
		}

		if (pNavChar->GetCurrentDemeanor() != kDemeanorAmbient)
		{
			pNavChar->ForceDemeanor(kDemeanorAmbient, AI_LOG);
		}

		FadeToStateParams params;
		params.m_stateStartPhase = RandomFloatRange(0.0f, 1.0f);
		params.m_animFadeTime = 0.4f;

		m_animAction.FadeToState(pAnimControl, SID("s_idle-sleep"), params, AnimAction::kFinishOnTransitionTaken);

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool SleepStir() override
	{
		NavCharacter* pNavChar = GetCharacter();
		if (!pNavChar)
		{
			return false;
		}

		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		if (!pAnimControl)
		{
			return false;
		}

		m_animAction.Request(pAnimControl, SID("sleep-stir"), AnimAction::kFinishOnNonTransitionalStateReached);

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool WanderStir() override
	{
		if (!PlayPerformance(SID("wander-stir")))
		{
			return false;
		}

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool ThrowPartial() override
	{
		NavCharacter* pNavChar = GetCharacter();
		if (!pNavChar)
		{
			return false;
		}

		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		if (!pAnimControl)
		{
			return false;
		}

		AnimSimpleLayer::FadeOutOnCompleteParams fadeOutParams;
		fadeOutParams.m_enabled = true;
		fadeOutParams.m_fadeTime = 0.4f;
		m_partialAnimAction.FadeToAnim(pAnimControl, 
			kaPartialThrowAnimId[m_nextThrowAnimIdx], 
			0.0f, 
			0.3f, 
			DC::kAnimCurveTypeUniformS, 
			fadeOutParams, 
			false, 
			false, 
			AnimAction::kFinishOnAnimEnd);

		m_isPartialThrowing = true;

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool IsPartialThrowActive() const override
	{
		return m_isPartialThrowing;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual StringId64 GetNextThrowAnimId() const override
	{
		return kaThrowAnimId[m_nextThrowAnimIdx];
	}

	virtual StringId64 GetNextThrowProjectileSpawnAnimId() const override
	{
		const NavCharacter *const pNavChar = GetCharacter();
		if (!pNavChar)
		{
			return INVALID_STRING_ID_64;
		}

		const AnimControl *const pAnimControl = pNavChar->GetAnimControl();
		if (!pAnimControl)
		{
			return INVALID_STRING_ID_64;
		}

		const StringId64 animId = GetNextThrowAnimId();
		const StringId64 mappedAnimId = pAnimControl->GetAnimOverlays()->LookupTransformedAnimId(animId);

		if (mappedAnimId == SID("bloater-throw-l-hip"))
		{
			return SID("bloater-throw-l-hip--gun");
		}

		if (mappedAnimId == SID("bloater-throw-r-hip"))
		{
			return SID("bloater-throw-r-hip--gun");
		}

		if (mappedAnimId == SID("bloater-throw-r-shoulder"))
		{
			return SID("bloater-throw-r-shoulder--gun");
		}

		return INVALID_STRING_ID_64;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool Throw(const Vector* pDir) override
	{
		NavCharacter* pNavChar = GetCharacter();
		if (!pNavChar)
			return false;

		BoundFrame bf = pNavChar->GetBoundFrame();
		if (pDir)
			bf.SetRotation(QuatFromLookAt(*pDir, GetLocalY(bf.GetLocator())));

		if (!PlayPerformance(GetNextThrowAnimId(), SID("s_performance-ap"), &bf, 0.4f, DC::kAnimCurveTypeEaseOut))
			return false;

		m_isThrowing = true;
		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void PickNextThrowAnim() override
	{
		m_nextThrowAnimIdx = RandomIntRange(0, kThrowAnimCount - 1);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void InterruptPerformance() override
	{
		NavCharacter* pNavChar = GetCharacter();
		if (!pNavChar)
		{
			return;
		}

		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		if (!pAnimControl)
		{
			return;
		}

		AnimSimpleLayer* pInfectedLayer = pAnimControl->GetSimpleLayerById(SID("infected-partial"));
		if (!pInfectedLayer->IsFadedOut())
		{
			pInfectedLayer->Fade(0.0f, 0.1f);
		}

		pInfectedLayer = pAnimControl->GetSimpleLayerById(SID("infected-additive"));
		if (!pInfectedLayer->IsFadedOut())
		{
			pInfectedLayer->Fade(0.0f, 0.1f);
		}

		if (!m_animAction.IsDone())
		{
			if (AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer())
			{
				pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
				pBaseLayer->RequestTransition(SID("exit"));
			}
			m_animAction.Reset();
		}
	}

private:
	/// --------------------------------------------------------------------------------------------------------------- ///
	bool PlayPerformance(StringId64 animId,
						 StringId64 stateId = SID("s_performance"),
						 BoundFrame* pBf	= nullptr,
						 F32 fadeTime		= 0.4f,
						 DC::AnimCurveType blendType = DC::kAnimCurveTypeInvalid,
						 AnimAction::FinishCondition finishCondition = AnimAction::kFinishOnAnimEndEarly)
	{
		NavCharacter* pNavChar = GetCharacter();
		if (!pNavChar)
		{
			return false;
		}

		if (!pNavChar->IsType(SID("Infected")))
		{
			return false;
		}

		BoundFrame defaultBf = pNavChar->GetBoundFrame();
		if (!pBf)
			pBf = &defaultBf;

		AI::SetPluggableAnim(pNavChar, animId);

		FadeToStateParams params;
		params.m_animFadeTime = fadeTime;
		params.m_blendType = blendType;
		if (pBf)
		{
			params.m_apRef = *pBf;
			params.m_apRefValid = true;
		}

		m_animAction.ClearSelfBlendParams();
		m_animAction.FadeToState(pNavChar->GetAnimControl(), stateId, params, finishCondition);

		NavAnimHandoffDesc handoff;
		handoff.SetStateChangeRequestId(m_animAction.GetStateChangeRequestId(), stateId);
		handoff.m_steeringPhase = -1.0f;
		handoff.m_motionType	= kMotionTypeMax;

		pNavChar->ConfigureNavigationHandOff(handoff, FILE_LINE_FUNC);

		return true;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiInfectedController* CreateAiInfectedController()
{
	return NDI_NEW AiInfectedController;
}
