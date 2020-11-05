/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/agent/nav-character.h"

#include "game/ai/controller/animation-controllers.h"
#include "game/ai/controller/tap-controller.h"
#include "game/character/character-rope.h"
#include "game/rope-interface.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class TapController::RopeClimb : public TapController::BaseState
{
	TypedSubsystemHandle<ICharacterRope> m_hCharacterRope;

	virtual void OnEnter() override;
	virtual void OnExit() override;

	virtual void UpdateStatus(const TraversalActionPack* pTap) override;
	virtual void RequestAnimations(const TraversalActionPack* pTap) override;

	static void NpcRopeClimbInputFunc(Character* pChar, ICharacterRope::InputData* pData);
};

/// --------------------------------------------------------------------------------------------------------------- ///
void TapController::RopeClimb::OnEnter()
{
	TapController* pSelf = static_cast<TapController*>(GetSelf());
	NavCharacter* pChar	 = pSelf->GetCharacter();

	Locator grabLoc = pChar->GetLocator();
	grabLoc.Move(2.75f * pChar->GetUp());

	RopeInterface* pRope = g_wallRopeManager.FindBestWallRope(pChar->GetUniformScale(), kZero, 0.0f, 10.0f, grabLoc);
	if (pRope == nullptr)
		return;

	Point grabPt = pRope->ClosestPointOnRope(grabLoc.Pos());
	Point pinPt	 = pRope->GetPinPoint();

	float ropeLen	= pRope->GetRopeLength();
	float distToPin = Length(grabPt - pinPt);
	if (distToPin > 0.5f * ropeLen)
		pSelf->SetRopeClimbDir(1.0f);
	else
		pSelf->SetRopeClimbDir(-1.0f);

	// g_prim.Draw( DebugSphere(grabPt, 0.1f, kColorOrange), Seconds(1.0f) );

	Vector playerToRope = grabPt - pChar->GetTranslation();
	float yDist			= playerToRope.Y();
	float xzDist		= LengthXz(playerToRope);

	// if (yDist < 2.5f || yDist > 3.0f || xzDist > 2.0f)
	//	return;

	int enterState = (yDist > 2.0f) ? ICharacterRope::kEnterStateClimbUp : ICharacterRope::kEnterStateDropDown;
	Locator finalGrabLoc = Locator(grabPt, pChar->GetRotation());

	ICharacterRope::SpawnInfo spawnInfo(SID("CharacterRope"), pChar);
	spawnInfo.m_startState = enterState;
	spawnInfo.m_pRope	   = pRope;
	spawnInfo.m_grabLoc	   = finalGrabLoc;
	spawnInfo.m_inputFunc  = NpcRopeClimbInputFunc;
	m_hCharacterRope	   = (ICharacterRope*)NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap,
															spawnInfo,
															FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TapController::RopeClimb::OnExit()
{
	TapController* pSelf   = static_cast<TapController*>(GetSelf());
	NavCharacter* pNavChar = pSelf->GetCharacter();
	AnimStateLayer* pBaseLayer = pNavChar->GetAnimControl()->GetBaseStateLayer();

	NavAnimHandoffDesc exitHandoff;
	exitHandoff.m_motionType  = kMotionTypeWalk;

	FadeToStateParams params;
	params.m_animFadeTime = 0.2f;
	StateChangeRequest::ID reqId = pBaseLayer->FadeToState(SID("s_idle"), params);
	exitHandoff.SetStateChangeRequestId(reqId, SID("s_idle"));

	pNavChar->ConfigureNavigationHandOff(exitHandoff, FILE_LINE_FUNC);
	pSelf->ExitedTap();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TapController::RopeClimb::UpdateStatus(const TraversalActionPack* pTap)
{
	TapController* pSelf = static_cast<TapController*>(GetSelf());
	ICharacterRope* pCharacterRope = m_hCharacterRope.ToSubsystem();

	ICharacterRope::ExitData exitData;
	bool exit = true;

	if (pCharacterRope)
	{
		exit = pCharacterRope->ShouldExit(&exitData);
	}

	if (exit)
	{
		pSelf->GoNone();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TapController::RopeClimb::RequestAnimations(const TraversalActionPack* pTap)
{
	TapController* pSelf = static_cast<TapController*>(GetSelf());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TapController::RopeClimb::NpcRopeClimbInputFunc(Character* pChar, ICharacterRope::InputData* pData)
{
	const NavCharacter* pNavChar = NavCharacter::FromProcess(pChar);
	GAMEPLAY_ASSERT(pNavChar);

	if (pNavChar)
	{
		const TapController* pTapController = pNavChar->GetAnimationControllers()->GetTraversalController();
		GAMEPLAY_ASSERT(pTapController);

		pData->m_shouldDrop		  = false;
		pData->m_verticalMovement = pTapController->GetRopeClimbDir();
	}
}

FSM_STATE_REGISTER(TapController, RopeClimb, kPriorityMedium);
