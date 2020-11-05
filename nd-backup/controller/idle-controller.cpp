/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "game/ai/controller/idle-controller.h"

#include "corelib/util/angle.h"
#include "corelib/util/random.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/ai/base/nd-ai-util.h"

#include "game/ai/base/ai-game-debug.h"
#include "game/ai/base/ai-game-util.h"
#include "game/ai/controller/animation-controller-config.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/npc-idle-defines.h"
#include "ndlib/util/msg.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"

static StringId64				kIdleRandomnessAnim = SID("idle-randomness");

//--------------------------------------------------------------------------------------
class AiIdleController : public IAiIdleController
{
public:
	AnimAction				m_animAction;
	TimeFrame				m_idleStartTime;
	CachedAnimLookup		m_idleRandomnessLookup;
	AnimSimpleInstance::ID	m_idleRandomnessInstId;

	AiIdleController();

	virtual bool PlayPerformance(StringId64 animId) override;

	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;

	virtual bool IsBusy() const override;
	virtual void Reset() override;
};

//--------------------------------------------------------------------------------------
AiIdleController::AiIdleController()
: m_idleStartTime(TimeFramePosInfinity())
, m_idleRandomnessInstId(INVALID_ANIM_INSTANCE_ID)
{
	m_idleRandomnessLookup.SetSourceId(kIdleRandomnessAnim);
}

//------------------------------------------------------------------------------------------------------------------
bool AiIdleController::PlayPerformance(StringId64 animId)
{
	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
	{
		return false;
	}

	FadeToStateParams params;
	params.m_apRef = pNavChar->GetBoundFrame();
	params.m_apRefValid = true;
	params.m_animFadeTime = 0.4f;

	AI::SetPluggableAnim(pNavChar, animId);
	m_animAction.FadeToState(pNavChar->GetAnimControl(),
							 SID("s_performance"),
							 params,
							 AnimAction::kFinishOnNonTransitionalStateReached);

	return true;
}

//--------------------------------------------------------------------------------------
void AiIdleController::RequestAnimations()
{
	PROFILE(Animation, IdleCon_ReqAnims);

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurInst = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
	const StringId64 curStateId = pCurInst ? pCurInst->GetStateName() : INVALID_STRING_ID_64;

	if (AnimSimpleLayer* pRandomnessLayer = pAnimControl->GetSimpleLayerById(SID("idle-randomness")))
	{
		const Clock* pClock = pNavChar->GetClock();

		m_idleRandomnessLookup = pAnimControl->LookupAnimCached(m_idleRandomnessLookup);
		const ArtItemAnim* pDesAnim = m_idleRandomnessLookup.GetAnim().ToArtItem();

		const AnimSimpleInstance* pSimpInst = pRandomnessLayer->CurrentInstance();
		const bool layerFadedUp = pRandomnessLayer->GetDesiredFade() >= 1.0f;

		const StringId64 curAnimId = pSimpInst ? pSimpInst->GetAnimId() : INVALID_STRING_ID_64;
		const ArtItemAnim* pCurAnim = pSimpInst ? pSimpInst->GetAnim().ToArtItem() : nullptr;

		const bool isPlayingRightAnim = pCurAnim && (pCurAnim == pDesAnim);
		const bool timeToPlayRandomness = pClock->TimePassed(Seconds(g_aiGameOptions.m_idleRandomnessDelaySec), m_idleStartTime);
		
		if ((curStateId == SID("s_idle")) && pDesAnim && timeToPlayRandomness)
		{
			if (isPlayingRightAnim)
			{
				// do nothing
			}
			else
			{
				AnimSimpleLayer::FadeRequestParams reqParams;
				reqParams.m_forceLoop = true;
				reqParams.m_fadeTime = g_aiGameOptions.m_idleRandomnessFadeUpSec;
				reqParams.m_blendType = g_aiGameOptions.m_idleRandomnessFadeUpCurve;
				reqParams.m_startPhase = Rand();

				if (pRandomnessLayer->RequestFadeToAnim(kIdleRandomnessAnim, reqParams))
				{
					m_idleRandomnessInstId = pRandomnessLayer->CurrentInstance()->GetId();
					pRandomnessLayer->Fade(1.0f, g_aiGameOptions.m_idleRandomnessFadeUpSec, g_aiGameOptions.m_idleRandomnessFadeUpCurve);
				}
				else
				{
					m_idleRandomnessInstId = INVALID_ANIM_INSTANCE_ID;
					pRandomnessLayer->Fade(0.0f, g_aiGameOptions.m_idleRandomnessFadeDownSec, g_aiGameOptions.m_idleRandomnessFadeDownCurve);
				}
			}
		}
		else
		{
			if ((curStateId != SID("s_idle")) || !pDesAnim)
			{
				m_idleStartTime = pNavChar->GetCurTime();
			}

			const bool isPlayingIdleRandomness = pSimpInst && (pSimpInst->GetId() == m_idleRandomnessInstId);

			if (isPlayingIdleRandomness || layerFadedUp)
			{
				pRandomnessLayer->Fade(0.0f, g_aiGameOptions.m_idleRandomnessFadeDownSec, g_aiGameOptions.m_idleRandomnessFadeDownCurve);
				m_idleRandomnessInstId = INVALID_ANIM_INSTANCE_ID;
			}
		}
	}
}

//--------------------------------------------------------------------------------------
void AiIdleController::UpdateStatus()
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	m_animAction.Update(pAnimControl);
}

//--------------------------------------------------------------------------------------
bool AiIdleController::IsBusy() const
{
	// The idle state is never really too busy. :-)
	// We can fix this later if we need to. Right now the turn is REALLY long and makes the character unresponsive.

	// cowboy here... trying to fix it!
	return !m_animAction.IsDone();
}

//--------------------------------------------------------------------------------------
void AiIdleController::Reset() // call this if we've done something to force the npc out of his state (like a hit reaction) so skill exits dont try and call this
{
	m_animAction.Reset();
}

//--------------------------------------------------------------------------------------
IAiIdleController* CreateAiIdleController()
{
	return NDI_NEW AiIdleController;
}
