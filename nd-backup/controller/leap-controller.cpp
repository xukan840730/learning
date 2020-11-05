/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/leap-controller.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/util/finite-state-machine.h"

#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nav/action-pack-entry-def.h"
#include "gamelib/gameplay/nav/leap-action-pack.h"
#include "gamelib/level/art-item-anim.h"

#include "ailib/nav/nav-ai-util.h"

#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/behavior/behavior-leap-attack.h"
#include "game/ai/coordinator/encounter-coordinator.h"
#include "game/ai/coordinator/encounter-melee.h"
#include "game/ai/knowledge/entity.h"
#include "game/framestate.h"
#include "game/player/melee/melee-util.h"
#include "game/player/melee/process-melee-action.h"
#include "game/player/player.h"

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame AiLeapController::GetLeapApForGround(const LeapActionPack* pLeapAp)
{
	if (!pLeapAp)
	{
		return BoundFrame(kIdentity);
	}

	BoundFrame apRef = pLeapAp->GetBoundFrame();
	const Point groundPosWs = PointFromXzAndY(pLeapAp->GetBoundFrame().GetTranslationWs(), pLeapAp->GetRegistrationPointWs());

	apRef.SetTranslationWs(groundPosWs);

	return apRef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame AiLeapController::GetLeapApForLip(const LeapActionPack* pLeapAp)
{
	return pLeapAp ? pLeapAp->GetBoundFrame() : BoundFrame(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	m_leapAttack = DC::LeapAttack();
	ParentClass::Init(pNavChar, pNavControl);

	GetStateMachine().Init(this, FILE_LINE_FUNC, true, sizeof(ActionPackEntryDef));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl)
{
	m_leapAttack = DC::LeapAttack();
	ParentClass::Init(pCharacter, pNavControl);

	GetStateMachine().Init(this, FILE_LINE_FUNC, true, sizeof(ActionPackEntryDef));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	GetStateMachine().Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Reset()
{
	for (int i =0; i < DC::kLeapAttackTypeCount; i++)
	{
		m_aBestLeap[i].Clear();
	}

	GoNone();
	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Interrupt()
{
	m_leapAttack = DC::LeapAttack();
	GoNone();
	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Clock* AiLeapController::GetClock() const
{
	const Clock* pClock = nullptr;

	if (const NdGameObject* pGo = GetCharacterGameObject())
	{
		pClock = pGo->GetClock();
	}
	else
	{
		pClock = EngineComponents::GetFrameState()->GetClock(kGameClock);
	}

	return pClock;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::RequestAnimations()
{
	ParentClass::RequestAnimations();

	GetStateMachine().TakeStateTransition();

	if (BaseState* pState = GetState())
	{
		pState->RequestAnimations();
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::UpdateStatus()
{
	GetStateMachine().TakeStateTransition();

	if (BaseState* pState = GetState())
	{
		pState->UpdateStatus();
	}

	const Npc* pNpc = Npc::FromProcess(GetCharacterGameObject());

	if (pNpc && pNpc->GetCurrentTargetEntity() && !pNpc->GetCurrentTargetEntity()->IsLost())
	{
		UpdateBestLeapAps();
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLeapController::IsBusy() const
{
	bool busy = false;

	if (const BaseState* pState = GetState())
	{
		busy = pState->IsBusy();
	}

	return busy;
}

//----------------------------------------------------------------------------------------------------
bool AiLeapController::SetupLeapClearanceProbe(HavokSphereCastJob& sphereJob, const NdGameObject* pTarget, const ActionPack* pLeapAp)
{
	if (sphereJob.IsValid())
	{
		AI_ASSERTF(false, ("Leap Clearance Probe not closed before setup."));
		return false;
	}

	if (!pTarget)
		return false;

	if (!pLeapAp)
		return false;

	Point targetPosWs;
	bool posSet = false;

	if (const AttachSystem* pAttach = pTarget->GetAttachSystem())
	{
		AttachIndex index = pAttach->FindPointIndexById(SID("targHead"));

		if (index == AttachIndex::kInvalid)
		{
			index = pAttach->FindPointIndexById(SID("targChest"));
		}

		if (index != AttachIndex::kInvalid)
		{
			targetPosWs = pAttach->GetAttachPosition(index);
			posSet = true;
		}
	}

	if (!posSet)
	{
		targetPosWs = pTarget->GetLocator().GetPosition() + Vector(0.0f, 1.0f, 0.0f);
	}

	if (!IsFinite(targetPosWs))
	{
		AI_ASSERTF(false, ("Leap Attack Clearance probe found invalid target position."));
		return false;
	}

	const F32 kProbeRadius = 0.5f;

	CollideFilter collideFilter = CollideFilter(Collide::kLayerMaskBackground);
	collideFilter.SetPatExclude(Pat(Pat::kPassThroughMask));

	sphereJob.Open(1);
	sphereJob.SetFilterForAllProbes(collideFilter);

	sphereJob.SetProbeExtents(0, pLeapAp->GetLocatorWs().GetPosition() + Vector(0.0f, 1.5f, 0.0f), targetPosWs);
	sphereJob.SetProbeRadius(0, kProbeRadius);

	sphereJob.Kick(FILE_LINE_FUNC);

	return true;
}

//----------------------------------------------------------------------------------------------------
void AiLeapController::ProcessLeapClearanceProbe(HavokSphereCastJob& sphereJob, bool& isClear)
{
	isClear = true;

	sphereJob.Wait();

	if (sphereJob.IsValid())
	{
		if (sphereJob.IsContactValid(0, 0))
			isClear = false;
	}

	sphereJob.Close();
}


//----------------------------------------------------------------------------------------------------------------------
void AiLeapController::UpdateBestLeapAps()
{
	const Npc* pNpc = Npc::FromProcess(GetCharacterGameObject());

	if (!pNpc)
		return;

	const DC::CombatParams* pCombatParams = pNpc->GetCombatParams();

	if (!pCombatParams || pCombatParams->m_leapAttackListId == INVALID_STRING_ID_64)
		return;

	const AiEncounterCoordinator* pCoord = pNpc->GetEncounterCoordinator();

	if (!pCoord)
		return;

	const AiEncounterMelee& melee = pCoord->GetMelee();

	LeapAttackInfo* aValidLeapAttacks = NDI_NEW (kAllocSingleFrame) LeapAttackInfo[DC::kLeapAttackTypeCount];

	bool anyValidAps = melee.FindLeapAttackAp(pNpc, m_hActionPack, aValidLeapAttacks);

	for (int i = 0; i < DC::kLeapAttackTypeCount; i++)
	{
		LeapAttackInfo& curLeapInfo = aValidLeapAttacks[i];
		LeapAttackInfo& bestLeapInfo = m_aBestLeap[i];

		if (!anyValidAps || !curLeapInfo.m_hAp.IsValid())
		{
			bestLeapInfo.m_hAp = nullptr;
			bestLeapInfo.m_isValid = false;
			bestLeapInfo.m_clearanceProbe.Close();
			continue;
		}

		if(bestLeapInfo.m_hAp != curLeapInfo.m_hAp || !bestLeapInfo.m_clearanceProbe.IsValid())
		{
			bestLeapInfo.m_isValid &= bestLeapInfo.m_hAp == curLeapInfo.m_hAp;
			bestLeapInfo.m_hAp = curLeapInfo.m_hAp;
			bestLeapInfo.m_leapAttack = curLeapInfo.m_leapAttack;
			bestLeapInfo.m_clearanceProbe.Close();
			SetupLeapClearanceProbe(bestLeapInfo.m_clearanceProbe, pNpc->GetCurrentTargetProcess(), curLeapInfo.m_hAp.ToActionPack());
		}
		else if (bestLeapInfo.m_clearanceProbe.IsValid())
		{
			ProcessLeapClearanceProbe(bestLeapInfo.m_clearanceProbe, bestLeapInfo.m_isValid);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLeapController::SetLeapAttack(DC::LeapAttackTypeMask typeMask)
{
	const Npc* pNpc = Npc::FromProcess(GetCharacterGameObject());

	if (!pNpc)
		return false;

	for (int i = 0; i < DC::kLeapAttackTypeCount; i++)
	{
		LeapAttackInfo& leapInfo = m_aBestLeap[i];
		if ((typeMask & 1U << i) && leapInfo.m_isValid)
		{
			m_leapAttack = leapInfo.m_leapAttack;
			m_hActionPack = leapInfo.m_hAp;
			m_hDefender = Character::FromProcess(pNpc->GetCurrentTargetMutableProcess());
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLeapController::IsCurrentLeapValid()
{
	if (!m_hActionPack.IsValid())
		return false;

	NavCharacter* pChar = GetCharacter();
	if (pChar && pChar->IsPlayingWorldRelativeAnim() && ActionPackHandle(pChar->GetReservedActionPack()) == m_hActionPack)
		return true; // Finish what we started.

	if (m_leapAttack.m_leapType >= DC::kLeapAttackTypeCount)
		return false;

	LeapAttackInfo& validLeap = m_aBestLeap[m_leapAttack.m_leapType];

	if (!validLeap.m_isValid)
		return false;

	if (validLeap.m_leapAttack.m_attackId != m_leapAttack.m_attackId)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AiLeapController::GetLeapAttackTypeName(DC::LeapAttackType leapType) const
{
	switch (leapType)
	{
	case DC::kLeapAttackTypeJumpDown:
		return "Jump Down";
	case DC::kLeapAttackTypeVault:
		return "Vault";
	case DC::kLeapAttackTypePullDown:
		return "Pull Down";
	}

	return "INVALID";
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	if (g_aiGameOptions.m_leap.m_display)
	{
		if (const char* pStateName = GetStateName(nullptr))
		{
			pPrinter->PrintText(kColorWhite, "Leap: %s\n", pStateName);
		}

		U32 alreadyDrawnMask = 0;

		for (int i = 0; i < DC::kLeapAttackTypeCount; i++)
		{
			if (alreadyDrawnMask & 1U << i)
				continue;

			const LeapAttackInfo& leapInfo = m_aBestLeap[i];

			if (leapInfo.m_hAp.IsValid())
			{
				StringBuilder<32> typesStringBuilder;
				typesStringBuilder.append(GetLeapAttackTypeName(i));
				for (int ii = i + 1; ii < DC::kLeapAttackTypeCount; ii++)
				{
					if (m_aBestLeap[ii].m_hAp.IsValid() && m_aBestLeap[ii].m_hAp == leapInfo.m_hAp)
					{
						typesStringBuilder.append_format(" | %s", GetLeapAttackTypeName(ii));
						alreadyDrawnMask &= 1U << ii;
					}
				}

				const ActionPack* pAp = leapInfo.m_hAp.ToActionPack();
				pAp->DebugDraw(kPrimDuration1FramePauseable);

				ScreenSpaceTextPrinter apPrinter(pAp->GetRegistrationPointWs());
				apPrinter.SetDuration(kPrimDuration1FramePauseable);

				apPrinter.PrintText(kColorWhite,
									"Type:   %s\nValid:  %s\nAttk:   %s\nActive: %s",
									typesStringBuilder.c_str(),
									leapInfo.m_isValid ? "true" : "false",
									DevKitOnly_StringIdToString(leapInfo.m_leapAttack.m_attackId),
									m_hActionPack == leapInfo.m_hAp ? "true" : "false");
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLeapController::ResolveDefaultEntry(const ActionPackResolveInput& input,
										   const ActionPack* pActionPack,
										   ActionPackEntryDef* pDefOut) const
{
	if (!pActionPack || !pDefOut)
		return false;

	if (pActionPack->GetType() != ActionPack::kLeapActionPack)
	{
		return false;
	}

	const LeapActionPack* pLeapAp = (const LeapActionPack*)pActionPack;
	const NdGameObject* pGo = GetCharacterGameObject();
	const SkeletonId skelId = pGo ? pGo->GetSkeletonId() : INVALID_SKELETON_ID;
	const AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;
	const ArtItemAnim* pEnterAnim = pAnimControl ? pAnimControl->LookupAnim(m_leapAttack.m_enterAnim).ToArtItem() : nullptr;

	if (!pEnterAnim)
	{
		return false;
	}

	const BoundFrame apRef = GetLeapApForGround(pLeapAp);
	const Locator apRefWs = apRef.GetLocatorWs();
	Locator enterAlignWs = pGo->GetLocator();

	if (!FindAlignFromApReference(skelId, pEnterAnim, 0.0f, apRefWs, SID("apReference-lip"), &enterAlignWs))
	{
		return false;
	}

	pDefOut->m_hResolvedForAp = pActionPack;
	pDefOut->m_entryNavLoc	  = NavUtil::ConstructNavLocation(enterAlignWs.Pos(), NavLocation::Type::kNavPoly, 0.45f);
	pDefOut->m_entryRotPs	  = pGo->GetParentSpace().UntransformLocator(enterAlignWs).Rot();
	pDefOut->m_entryAnimId	  = pEnterAnim->GetNameId();
	pDefOut->m_apReference	  = apRef;
	pDefOut->m_sbApReference  = GetLeapApForLip(pLeapAp);

	pDefOut->m_autoExitAfterEnter = true;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLeapController::ResolveEntry(const ActionPackResolveInput& input,
									const ActionPack* pActionPack,
									ActionPackEntryDef* pDefOut) const
{
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLeapController::UpdateEntry(const ActionPackResolveInput& input,
								   const ActionPack* pActionPack,
								   ActionPackEntryDef* pDefOut) const
{
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Enter(const ActionPackResolveInput& input,
							 ActionPack* pActionPack,
							 const ActionPackEntryDef& entryDef)
{
	ParentClass::Enter(input, pActionPack, entryDef);

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_leap.m_alwaysLeapToPlayer))
	{
		m_hDefender = GetPlayer();
	}

	GoEntering(entryDef);
	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Exit(const PathWaypointsEx* pExitPathPs)
{
	GoLeaping();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLeapController::TryStartMeleeMove()
{
	Character* pChar = Character::FromProcess(GetCharacterGameObject());
	Character* pDefender = m_hDefender.ToMutableProcess();

	if (!pChar || !pDefender)
		return false;

	MeleeMoveSearchParams searchParams;
	searchParams.AddAttacker(pChar);
	searchParams.AddDefender(pDefender);
	searchParams.m_scriptRequest = false;
	searchParams.m_moveOrListId = m_leapAttack.m_attackId;

	MeleeMoveStartParams startParams;
	if (FindMeleeMove(searchParams, startParams))
	{
		if (ProcessMeleeAction::StartMeleeMove(startParams))
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLeapController::GetDefenderApRef(const ArtItemAnim* pLeapAnim, BoundFrame* pApRefOut) const
{
	const NdGameObject* pGo = GetCharacterGameObject();
	const LeapActionPack* pLeapAp = m_hActionPack.ToActionPack<LeapActionPack>();
	const Character* pDefender = m_hDefender.ToProcess();

	if (!pGo || !pLeapAp || !pDefender)
		return false;

	const SkeletonId skelId = pGo->GetSkeletonId();

	Locator animGroundLs, animDefLs;
	if (!EvaluateChannelInAnim(skelId, pLeapAnim, SID("apReference-ground"), 0.0f, &animGroundLs))
	{
		return false;
	}

	if (!EvaluateChannelInAnim(skelId, pLeapAnim, SID("apReference-def"), 0.0f, &animDefLs))
	{
		return false;
	}

	const float kMaxLeapRotDeg = g_aiGameOptions.m_leap.m_maxLeapRotAngleDeg;

	const BoundFrame groundAp = GetLeapApForGround(pLeapAp);
	const Locator groundApWs = groundAp.GetLocatorWs();

	const Point defenderPosWs = pDefender->GetTranslation();

	const Locator defInGroundSpace = animGroundLs.UntransformLocator(animDefLs);
	const Vector defOffsetXzGs = VectorXz(defInGroundSpace.Pos() - kOrigin);
	const Vector defDirXzGs = SafeNormalize(defOffsetXzGs, kUnitZAxis);

	const float defLeapDist = Length(defOffsetXzGs);
	const float leapRangeMin = defLeapDist * g_aiGameOptions.m_leap.m_leapDistScaleMin;
	const float leapRangeMax = defLeapDist * g_aiGameOptions.m_leap.m_leapDistScaleMax;

	const Point desPosGs = groundApWs.UntransformPoint(defenderPosWs);
	const Vector desOffsetXzGs = VectorXz(desPosGs - kOrigin);
	const float desLeapDist = Length(desOffsetXzGs);

	const Quat xzRot = QuatFromVectors(defDirXzGs, SafeNormalize(desOffsetXzGs, kUnitZAxis));
	const Quat constrainedRot = LimitQuatAngle(xzRot, DEGREES_TO_RADIANS(kMaxLeapRotDeg));

	const Locator rotatedApGs = Locator(constrainedRot).TransformLocator(defInGroundSpace);
	const Vector constrainedDeltaGs = SafeNormalize(rotatedApGs.Pos() - kOrigin, kUnitZAxis) * Limit(desLeapDist, leapRangeMin, leapRangeMax);
	const Locator constrainedApGs = Locator(kOrigin + constrainedDeltaGs, rotatedApGs.Rot());
	const Locator constrainedApWs = groundApWs.TransformLocator(constrainedApGs);

	BoundFrame defAp = groundAp;

	defAp.SetLocatorWs(constrainedApWs);

	if (pApRefOut)
	{
		*pApRefOut = defAp;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// State None
/// --------------------------------------------------------------------------------------------------------------- ///

class AiLeapController::None : public AiLeapController::BaseState
{
	virtual void OnEnter() override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::None::OnEnter()
{
	AiLeapController* pSelf = GetSelf();
	pSelf->m_hDefender = MutableCharacterHandle();
}

FSM_STATE_REGISTER(AiLeapController, None, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// State Entering
/// --------------------------------------------------------------------------------------------------------------- ///

class AiLeapController::Entering : public AiLeapController::BaseState
{
	virtual void OnEnter() override;
	virtual void UpdateStatus() override;

	virtual bool IsBusy() const override { return true; }

	AnimActionWithSelfBlend m_enterAction;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Entering::OnEnter()
{
	AiLeapController* pSelf = GetSelf();

	if (pSelf->m_leapAttack.m_leapType == DC::kLeapAttackTypePullDown)
	{
		if (pSelf->TryStartMeleeMove())
		{
			pSelf->GoNone();
			return;
		}
	}

	const ActionPackEntryDef& entryDef = GetStateArg<ActionPackEntryDef>();
	NdGameObject* pGo = GetCharacterGameObject();
	AnimControl* pAnimControl = pGo->GetAnimControl();

	AI::SetPluggableAnim(pGo, entryDef.m_entryAnimId);

	FadeToStateParams params;
	params.m_customApRefId = SID("apReference-ground");
	params.m_stateStartPhase = entryDef.m_phase;
	params.m_apRef = entryDef.m_apReference;
	params.m_apRefValid = true;
	params.m_animFadeTime = 0.4f;
	params.m_motionFadeTime = 0.4f;

	m_enterAction.FadeToState(pAnimControl,
							  SID("s_leap-enter"),
							  params,
							  AnimAction::kFinishOnAnimEnd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Entering::UpdateStatus()
{
	AiLeapController* pSelf = GetSelf();
	NdGameObject* pGo = GetCharacterGameObject();
	AnimControl* pAnimControl = pGo->GetAnimControl();

	m_enterAction.Update(pAnimControl);

	if (m_enterAction.WasTransitionTakenThisFrame())
	{
		if (AnimStateInstance* pDestInstance = m_enterAction.GetTransitionDestInstance(pAnimControl))
		{
			const ActionPackEntryDef& entryDef = GetStateArg<ActionPackEntryDef>();

			SelfBlendAction::Params sbParams;
			sbParams.m_blendParams	   = NavUtil::ConstructSelfBlendParams(pDestInstance);
			sbParams.m_apChannelId	   = SID("apReference-lip");
			sbParams.m_constraintPhase = 1.0f;
			sbParams.m_destAp = entryDef.m_sbApReference;

			m_enterAction.ConfigureSelfBlend(pGo, sbParams);
		}
	}

	if (m_enterAction.IsDone() || !m_enterAction.IsValid())
	{
		pSelf->GoWaiting();
	}
}

FSM_STATE_REGISTER_ARG(AiLeapController, Entering, kPriorityMedium, ActionPackEntryDef);

/// --------------------------------------------------------------------------------------------------------------- ///
// State Waiting
/// --------------------------------------------------------------------------------------------------------------- ///

class AiLeapController::Waiting : public AiLeapController::BaseState
{
};

FSM_STATE_REGISTER(AiLeapController, Waiting, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// State Leaping
/// --------------------------------------------------------------------------------------------------------------- ///

class AiLeapController::Leaping : public AiLeapController::BaseState
{
public:
	virtual void OnEnter() override;
	virtual void UpdateStatus() override;

	virtual bool IsBusy() const override { return true; }

	AnimActionWithSelfBlend m_leapAction;
};

FSM_STATE_REGISTER(AiLeapController, Leaping, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Leaping::OnEnter()
{
	AiLeapController* pSelf = GetSelf();
	NdGameObject* pGo = GetCharacterGameObject();
	AnimControl* pAnimControl = pGo->GetAnimControl();

	const LeapActionPack* pLeapAp = pSelf->m_hActionPack.ToActionPack<LeapActionPack>();
	if (!pLeapAp)
	{
		pSelf->GoNone();
		return;
	}

	if (pSelf->m_leapAttack.m_leapAnim == INVALID_STRING_ID_64)
	{
		pSelf->TryStartMeleeMove();
		pSelf->GoNone();
	}

	AI::SetPluggableAnim(pGo, pSelf->m_leapAttack.m_leapAnim);

	FadeToStateParams params;
	params.m_customApRefId = SID("apReference-lip");
	params.m_stateStartPhase = 0.0f;
	params.m_apRef = GetLeapApForLip(pLeapAp);
	params.m_apRefValid = true;
	params.m_animFadeTime = 0.4f;
	params.m_motionFadeTime = 0.4f;

	m_leapAction.FadeToState(pAnimControl,
							 SID("s_leap-midair"),
							 params,
							 AnimAction::kFinishOnAnimEnd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Leaping::UpdateStatus()
{
	AiLeapController* pSelf = GetSelf();
	NdGameObject* pGo = GetCharacterGameObject();
	AnimControl* pAnimControl = pGo->GetAnimControl();

	m_leapAction.Update(pAnimControl);

	if (m_leapAction.WasTransitionTakenThisFrame())
	{
		if (const AnimStateInstance* pDestInstance = m_leapAction.GetTransitionDestInstance(pAnimControl))
		{
			BoundFrame defAp;
			if (pSelf->GetDefenderApRef(pDestInstance->GetPhaseAnimArtItem().ToArtItem(), &defAp))
			{
				SelfBlendAction::Params sbParams;
				sbParams.m_blendParams = NavUtil::ConstructSelfBlendParams(pDestInstance);
				sbParams.m_apChannelId = SID("apReference-def");
				sbParams.m_constraintPhase = 1.0f;
				sbParams.m_destAp = defAp;

				m_leapAction.ConfigureSelfBlend(pGo, sbParams);
			}
		}
	}

	if (!m_leapAction.IsValid() || m_leapAction.IsDone())
	{
		if (pSelf->TryStartMeleeMove())
		{
			pSelf->GoNone();
		}
		else
		{
			pSelf->GoLanding();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// State Landing
/// --------------------------------------------------------------------------------------------------------------- ///

class AiLeapController::Landing : public AiLeapController::BaseState
{
public:
	virtual void OnEnter() override;
	virtual void UpdateStatus() override;

	virtual bool IsBusy() const override { return m_landAction.IsValid() && !m_landAction.IsDone(); }

	AnimAction m_landAction;
};

FSM_STATE_REGISTER(AiLeapController, Landing, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Landing::OnEnter()
{
	AiLeapController* pSelf = GetSelf();
	NdGameObject* pGo = GetCharacterGameObject();
	AnimControl* pAnimControl = pGo->GetAnimControl();

	const LeapActionPack* pLeapAp = pSelf->m_hActionPack.ToActionPack<LeapActionPack>();
	if (!pLeapAp)
	{
		pSelf->GoNone();
		return;
	}

	if (pSelf->m_leapAttack.m_landingAnim != INVALID_STRING_ID_64)
		AI::SetPluggableAnim(pGo, pSelf->m_leapAttack.m_landingAnim);

	FadeToStateParams params;
	//params.m_customApRefId = SID("apReference-ground");
	params.m_stateStartPhase = 0.0f;
	//params.m_apRef = GetLeapApForGround(pLeapAp);
	//params.m_apRefValid = true;
	params.m_animFadeTime = 0.4f;
	params.m_motionFadeTime = 0.4f;

	m_landAction.FadeToState(pAnimControl,
							 SID("s_leap-exit"),
							 params,
							 AnimAction::kFinishOnNonTransitionalStateReached);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLeapController::Landing::UpdateStatus()
{
	AiLeapController* pSelf = GetSelf();
	NdGameObject* pGo = GetCharacterGameObject();
	AnimControl* pAnimControl = pGo->GetAnimControl();

	m_landAction.Update(pAnimControl);

	if (!m_landAction.IsValid() || m_landAction.IsDone())
	{
		pSelf->GoNone();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AiLeapController::GetMaxStateAllocSize() const
{
	return sizeof(Entering);
}

AiLeapController* CreateAiLeapController() { return NDI_NEW AiLeapController; }
