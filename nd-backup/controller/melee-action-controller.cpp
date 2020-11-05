/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/melee-action-controller.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/frame-params.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/process/clock.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/facts/fact-manager.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/health-system.h"

#include "ailib/nav/nav-ai-util.h"

#include "game/ai/agent/npc-manager.h"
#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/characters/buddy.h"
#include "game/ai/component/npc-configuration.h"
#include "game/ai/component/shooting-logic.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/controller/weapon-controller.h"
#include "game/ai/coordinator/encounter-coordinator.h"
#include "game/ai/knowledge/entity.h"
#include "game/event-attack.h"
#include "game/net/net-game-manager.h"
#include "game/net/net-info.h"
#include "game/player/melee/character-melee-control.h"
#include "game/player/melee/melee-util.h"
#include "game/player/melee/npc-melee-data-manager.h"
#include "game/player/melee/player-melee.h"
#include "game/player/melee/process-grapple-action-t1.h"
#include "game/player/melee/process-grapple-action.h"
#include "game/player/melee/process-melee-action.h"
#include "game/player/melee/process-player-anim-follower.h"
#include "game/player/player.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/melee-defines.h"
#include "game/weapon/process-weapon-base.h"

// Was in player-fight.h
const float kSteelFistHealth = 0.25f;

class MeleeActionController : public IAiMeleeActionController
{
	typedef AnimActionController ParentClass;
public:
	MeleeActionController ()
		: m_customBlendTime(0.0f)
		, m_numComboPunchesLanded(0)
		, m_numStunPunches(0)
		, m_killedByMelee(false)
		, m_waitingForIdleTransition(false)
		, m_damagedByMelee(false)
		, m_blockPartialAnimAction(SID("melee-partial"))
		, m_weaponGripAnimAction(SID("melee-hands"))
		, m_lastBlockState(INVALID_STRING_ID_64)
		, m_hadMeleeAction(false)
		, m_recentlyHadMeleeAction(false)
	{
		m_weaponGripAnim.SetSourceId(SID("melee-hands-anim"));
		m_enableWeaponGrip = true;
	}

	//NpcMeleeManagerHandle m_hMeleeManager;
	MutableProcessMeleeActionHandle m_hMeleeAction;
	MutableProcessMeleeActionHandle m_hNexMeleeAction;
	MutableProcessHandle m_hGrappleAction;
	MutableProcessPlayerAnimFollowerHandle m_hPlayerAnimFollowAction;
	F32 m_customBlendTime;
	StringId64 m_damagedByMeleeAttacker;
	TimeFrame m_damagedByMeleeTime;

	int m_numComboPunchesLanded;
	U32 m_numStunPunches;
	DC::AiMeleeType m_meleeType;
	U32 m_waitingForTransitioWatchdogTimer; // fail-safe timer. we had one case of melee transition somehow being cleared out, we need to detect that and continue
	bool m_killedByMelee;
	bool m_waitingForIdleTransition;
	bool m_damagedByMelee;
	bool m_hadMeleeAction;
	bool m_recentlyHadMeleeAction;
	bool m_enabledLookAt;
	Point m_lastHitPos;
	Vector m_lastHitDir;

	/////
	// idle
	enum IdleState {
		kNone,
		kNeedMeleeToIdle,
		kNeedLocoToIdle,
		kTurningLeft,
		kTurningRight,
		kIdle,
		kTurning,
		kRequestedIdleToMoveFw,
		kRequestedIdleToMoveBk,
		kRequestedIdleToMoveLt,
		kRequestedIdleToMoveRt,
		kIdleToMoveFwTransition,
		kIdleToMoveBkTransition,
		kIdleToMoveLtTransition,
		kIdleToMoveRtTransition,
		kIdleToMoveHandoff,
		kTrackingIdle,
		kTrackingMoveToMove
	};

	const DC::MeleeAttackBehavior *m_pDcMeleeBehavior;
	IdleState m_idleState;
	Point m_strafeReferencePt;

	TimeFrame m_lastAttackTime;
	SpringTrackerQuat	m_facingSpring;
	SpringTrackerPoint m_desiredLocationSpring;
	Point m_strafeDestination;
	float m_prevDistance;
	//


	StringId64 m_lastBlockState;
	AnimAction m_blockPartialAnimAction;
	AnimAction m_stepAnimationAction;


	AnimAction m_weaponGripAnimAction;
	CachedAnimLookup m_weaponGripAnim;
	StringId64 m_scheduledAnim;
	bool m_enableWeaponGrip;


	StringId64 m_requestedAnimId;
	bool m_requestedAnimIsBkwd;
	StringId64 m_requestedTransition;
	Quat m_requestedTransitionEndRot;
	float m_requestedTransitionAngleLeft;
	bool m_requestedTransitionIsTurnRight;
	bool m_requestedTransitionEndsInBkwd;

	StringId64 m_hitReactionMeleeAttackId;
	CharacterHandle m_hHitReactionMeleeAttacker;
	StringId64 m_hitReactionMeleeAnim;
	mutable I64 m_hitReactionAccessTime;

	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override
	{
		AnimActionController::Init(pNavChar, pNavControl);
		AI_ASSERT(pNavChar && pNavChar->IsKindOf(g_type_Npc));
		Npc *pNpc = static_cast<Npc*>(pNavChar);
		m_meleeType = *pNpc->GetArchetype().GetDefaultMeleeType();
		m_facingSpring.Reset();
		m_prevDistance = -1.0f;
		m_requestedAnimId = INVALID_STRING_ID_64;
		m_requestedAnimIsBkwd = false;
		m_requestedTransition = INVALID_STRING_ID_64;
		m_requestedTransitionEndsInBkwd = false;
		m_strafeDestination = Point(kZero);

		m_hitReactionMeleeAttackId = INVALID_STRING_ID_64;

		m_idleState = kNone;
		m_pDcMeleeBehavior = nullptr;
		m_customBlendTime = 0.0f;
		m_enabledLookAt = false;
	}

	virtual void StartGrappleAction(Process* pAction) override
	{
		m_hGrappleAction = pAction;
	}

	virtual bool HasJustFinishedMeleeAction() const override
	{
		return m_recentlyHadMeleeAction;
	}

	virtual bool ShouldInterruptSkills() const override
	{
		const Npc* pNpc = Npc::FromProcess(GetCharacter());
		if (!pNpc)
			return false;

		if (pNpc->IsGrappled() && pNpc->IsNetOwnedByMe())
			return true;

		const ProcessMeleeAction* pAction = pNpc->GetCurrentMeleeAction();
		if (pAction 
			&& !pAction->IsPhantom() 
			&& !pNpc->CanStartMeleeMove() 
			&& pNpc->GetDesiredSkillNum() != DC::kAiArchetypeSkillSurprise 
			&& pNpc->GetDesiredSkillNum() != DC::kAiArchetypeSkillBuddyCombat)
		{
			if (pAction->GetComboInitAttacker() != pNpc)
				return true;

			if (pNpc->GetArchetype().GetCharacterClass() == DC::kNpcCharacterClassEnemyEllie)
				return true;
		}

		return false;
	}

	bool ShouldInterruptNavigation() const override
	{
		if (IsBusy())
			return true;

		const Npc* pNpc = Npc::FromProcess(GetCharacter());
		if (!pNpc)
			return false;

		if (pNpc->IsGrappled())
			return true;

		return false;
	}

	virtual TimeFrame GetLastAttackTime() const override
	{
		return m_lastAttackTime;
	}

	virtual int GetNumComboPunchesLanded() const override
	{
		return m_numComboPunchesLanded;
	}

	virtual void OnCharacterProcessKilled() override
	{
		AbortMeleeAction();
	}

	virtual void OnNpcDeath() override
	{
		UpdateMeleeHandsAnim();
	}

	virtual bool IsAttacking() const override
	{
		if (!IsBusy())
			return false;

		const ProcessMeleeAction *pAction = m_hMeleeAction.ToProcess();
		if (!pAction)
			return false;

		const Character *pChar = GetCharacter();
		if (!pChar)
			return false;

		if (pAction->GetAttacker() != pChar && pAction->GetGrappler() != pChar)
			return false;

		return true;
	}

	virtual bool IsBeingBlocked() const override
	{
		return false;
	}

	void FadeOutHandLayer()
	{
		Npc* pNpc = (Npc*)(GetCharacter());
		if (!pNpc)
			return;

		AnimControl* pAnimControl = pNpc->GetAnimControl();
		if (!pAnimControl)
			return;

		if (AnimSimpleLayer* pMeleeHandsLayer = pAnimControl->GetSimpleLayerById(SID("melee-hands")))
		{
			pMeleeHandsLayer->Fade(0.0f, 0.1f);
		}
	}

	void ClearHandsOverlays()
	{
		Npc* pNpc = (Npc*)(GetCharacter());
		if (!pNpc)
			return;

		m_scheduledAnim = SID("invalid-melee-anim");

		AnimControl* pAnimControl = pNpc->GetAnimControl();
		if (!pAnimControl)
			return;

		AnimOverlays* pAnimOverlays = pAnimControl->GetAnimOverlays();
		if (!pAnimOverlays)
			return;

		pAnimOverlays->ClearLayer(SID("melee-hands"));
	}

	void RefreshHandsOverlays()
	{
		Npc* pNpc = (Npc*)(GetCharacter());
		if (!pNpc)
			return;

		AnimControl* pAnimControl = pNpc->GetAnimControl();
		if (!pAnimControl)
			return;

		AnimOverlays* pAnimOverlays = pAnimControl->GetAnimOverlays();
		if (!pAnimOverlays)
			return;

		const I32F layerIndex = pAnimOverlays->GetLayerIndex(SID("melee-hands"));
		if (layerIndex < 0)
			return;

		if (const ProcessWeaponBase *pWeapon = pNpc->GetWeapon())
		{
			StringId64 weaponDef = pWeapon->GetWeaponDefId();

			const StringId64 overlaySetName = weaponDef;
			//MsgOut("pNpc->GetOverlayBaseName(): %s\n", pNpc->GetOverlayBaseName());
			const StringId64 overlaySetMapId = StringId64Concat(SID("anim-overlay"), StringBuilder<256>("-%s-melee-hands", pNpc->GetOverlayBaseName()).c_str());

			const DC::AnimOverlaySet* pOverlaySet = nullptr;

			const DC::Map* pMap = ScriptManager::Lookup<DC::Map>(overlaySetMapId, nullptr);
			if (pMap)
			{
				const StringId64* pSetId = ScriptManager::MapLookup<StringId64>(pMap, overlaySetName);
				if (pSetId && (*pSetId != INVALID_STRING_ID_64))
				{
					pOverlaySet = ScriptManager::Lookup<DC::AnimOverlaySet>(*pSetId);
				}
			}

			if (pOverlaySet)
			{
				pAnimOverlays->SetOverlaySet(pOverlaySet);
			}
			else
			{
				pAnimOverlays->ClearLayer(SID("melee-hands"));
			}
		}
		else
		{
			pAnimOverlays->ClearLayer(SID("melee-hands"));
		}
#if 0
		if (AnimSimpleLayer* pMeleeHandsLayer = pAnimControl->GetSimpleLayerById(SID("melee-hands")))
		{
			StringId64 animId = pAnimControl->LookupAnimId(SID("melee-hands"));
			//if (animId)
			{
				if (animId)
				{
					AnimSimpleLayer::FadeRequestParams params;
					params.m_fadeTime = 0.1f;

					if (m_weaponGripAnimAction.FadeToAnim(pAnimControl, animId, 0.0f, 0.0f))
						pMeleeHandsLayer->Fade(1.0f, 0.1f);
					else
						pMeleeHandsLayer->Fade(0.0f, 0.1f);
				}
				else
				{
					pMeleeHandsLayer->Fade(0.0f, 0.1f);
				}
			}
			//m_highHandsAnim = newHighHandsAnim;
		}
#endif

	}


	virtual F32 GetLastBlendInfo() const override
	{
		return m_customBlendTime;
	}

	virtual void StartMeleeAction(ProcessMeleeAction* pAction, StringId64 weaponHandsAnim) override
	{
		m_enableWeaponGrip = weaponHandsAnim != INVALID_STRING_ID_64;

		m_customBlendTime = 0.0f;

		if (m_enableWeaponGrip)
		{
			m_weaponGripAnim.Reset();
			m_scheduledAnim = weaponHandsAnim;

		}
		else
			m_weaponGripAnim.SetSourceId(SID("melee-hands-anim"));

		Npc* pNpc = (Npc*)(GetCharacter());

		if (g_meleeOptions.m_multiCharacterBrawl)
		{
			if (!Character::IsBuddyNpc(pNpc))
			{
				AnimControl* pAnimControl = pNpc->GetAnimControl();
				AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays();

				pOverlays->SetOverlaySet(AI::CreateNpcAnimOverlayName("thug", "melee-face", "thug-brawl-use-lookat"), true);
			}
		}

		//MsgOut("    DoMeleeAction %s\n", DevKitOnly_StringIdToString(pAction->GetMeleeAttack()->m_name));

		// Are we already in one? It needs to be notified that we're
		// about to be controlled by a different action.
		if (m_hMeleeAction.Valid() || m_hPlayerAnimFollowAction.Valid())
		{
			m_hNexMeleeAction = pAction;
			EndPreviousMeleeAction(false, true);
			m_hNexMeleeAction = nullptr;
		}

		m_hMeleeAction = pAction;
		m_waitingForIdleTransition = false;

		if (g_meleeOptions.m_multiCharacterBrawl)
		{
			if (!Character::IsBuddyNpc(pNpc))
			{
				pNpc->SetLookAimMode(kLookAimPriorityBehaviorHigh, SID("LookAimTargetEntity"), FILE_LINE_FUNC);
				m_enabledLookAt = true;
			}
		}

		RefreshHandsOverlays();

		Player* pPlayer = pAction->GetPlayer();
		if (pPlayer == nullptr)
		{
			const AiEntity* pTarget = pNpc->GetCurrentMeleeTargetEntity();
			if (pTarget && pTarget->ToGameObject())
			{
				if (pTarget->ToGameObject()->IsType(SID("Player")))
				{
					pPlayer = const_cast<Player*>(static_cast<const Player*>(pTarget->ToGameObject()));
				}
			}
		}
		if (pPlayer)
		{
			// Don't add the brute.
			if (pNpc->GetArchetype().GetCharacterClass() == DC::kNpcCharacterClassScarBrute)
			{
				return;
			}

			/*
			ProcessNpcMeleeManager *pManager = pPlayer->m_pMeleeController->GetNpcMeleeManager();
			pManager->AddNpc(pNpc);
			m_hMeleeManager = pManager;
			*/
		}

		m_stepAnimationAction.Reset();
		m_requestedAnimId = INVALID_STRING_ID_64;
		m_requestedTransition = INVALID_STRING_ID_64;
		m_facingSpring.Reset();
	}

	virtual void StopMeleeAction() override
	{
		if (m_hMeleeAction.Valid() || m_hPlayerAnimFollowAction.Valid())
		{
			EndPreviousMeleeAction(false, false);
		}
	}

	bool WaitingForIdleTransition() const
	{
		const NavCharacter* pNavChar = GetCharacter();
		if (const AnimStateLayer* pLayer = pNavChar->GetAnimControl()->GetBaseStateLayer())
		{
			if (const AnimStateInstance* pInst = pLayer->CurrentStateInstance())
			{
				const StringId64 currentAnimState = pInst->GetStateName();
				const StateChangeRequest* pChange = pLayer->GetPendingChangeRequest(0);

				// Both s_idle and s_idle-fight are valid 'idle' states to be in.
				switch (currentAnimState.GetValue())
				{
				case SID_VAL("s_idle"):
				case SID_VAL("s_idle-fight"):
					return false;
				}

				if (pChange && pChange->m_type == StateChangeRequest::kTypeFlagDirectFade)
				{
					switch (pChange->m_transitionId.GetValue())
					{
					case SID_VAL("s_idle"):
					case SID_VAL("s_idle-fight"):
						return false;
					}
				}
			}
		}

		return true;
	}

	void EndPreviousMeleeAction(bool abort, bool transitionToNewMelee)
	{
		// This is called when we're ending a previous move because we're starting a new one.
		ASSERT(m_hMeleeAction.Valid() || m_hPlayerAnimFollowAction.Valid());

		//MsgOut("    EndPreviousMeleeAction %s\n", DevKitOnly_StringIdToString(m_hMeleeAction.ToProcess()->GetMeleeAttack()->m_name));
		Npc *pNpc = static_cast<Npc*>(GetCharacter());

		/*
		if (const ProcessNpcMeleeManager *pManager = m_hMeleeManager.ToProcess())
		{
			pManager->RemoveNpc(pNpc);
			m_hMeleeManager = nullptr;
		}
		*/

		// Let the action know that we're no longer part of it
		if (ProcessMeleeAction *pAction = m_hMeleeAction.ToMutableProcess())
		{
			CharacterSyncRemovedReason reason;

			if (abort)
				reason = kCharacterSyncAbort;
			else if (transitionToNewMelee)
				reason = kCharacterSyncEnd;
			else
				reason = kCharacterSyncExit;

			pAction->RemoveCharacter(pNpc, reason, SID("started new sync action")); // this will remove character from melee moe which will call CharacterMeleeControl::End which will call MeleeActionEnded() below

			m_hMeleeAction = nullptr;
		}

		if (ProcessPlayerAnimFollower* pFollow = m_hPlayerAnimFollowAction.ToMutableProcess())
		{
			pFollow->RemoveCharacter(pNpc, kCharacterSyncExit, SID("started new sync action"));
			m_hPlayerAnimFollowAction = nullptr;
		}

		if (!transitionToNewMelee)
		{
			FadeOutHandLayer();
		}

		//if (!transitionToNewMelee)
		//{
		//	m_idleState = kNeedMeleeToIdle;
		//}
	}

	virtual void AbortMeleeAction() override
	{
		if (m_hMeleeAction.Valid() || m_hPlayerAnimFollowAction.Valid())
		{
			EndPreviousMeleeAction(true, false);
		}
	}

	virtual void MeleeActionEnded (bool aborted) override
	{
		//MsgOut("    MeleeActionEnded %s\n", DevKitOnly_StringIdToString(m_hMeleeAction.ToProcess()->GetMeleeAttack()->m_name));

		// The NpcMeleeControl has told us the move is over.  Note that we can also get
		// here by way of EndPreviousMeleeAction() via the NpcMeleeControl's callback.
		Npc *pNpc = static_cast<Npc*>(GetCharacter());

		CharacterSyncRemovedReason reason;

		if (aborted)
			reason = kCharacterSyncAbort;
		else
			reason = kCharacterSyncExit;

		if (ProcessMeleeAction* pAction = m_hMeleeAction.ToMutableProcess())
		{
			pAction->RemoveCharacter(pNpc, reason, SID("controller ended")); // this will remove character from melee moe which will call CharacterMeleeControl::End which will call MeleeActionEnded() below
		}

		m_hMeleeAction = nullptr;

		/*
		if (const ProcessNpcMeleeManager *pManager = m_hMeleeManager.ToProcess())
		{
			pManager->RemoveNpc(pNpc);
			m_hMeleeManager = nullptr;
		}
		*/

		F32 delay = 0.0f;
		m_lastAttackTime = pNpc->GetClock()->GetCurTime() + Seconds(delay);

		if (m_enabledLookAt && !aborted)
		{
			pNpc->ClearLookAimMode(kLookAimPriorityBehaviorHigh, FILE_LINE_FUNC);
			m_enabledLookAt = false;
		}

		// We may have been told to go back to the mirrored fight idle, which transitions us to regular idle.
		// If so, we need to wait until that transition has been taken before continuing.
		m_waitingForTransitioWatchdogTimer = 0;
		m_waitingForIdleTransition = WaitingForIdleTransition();
	}

	virtual void StartFollowAnimAction(ProcessPlayerAnimFollower* pAction) override
	{
		m_hPlayerAnimFollowAction = pAction;
	}

	virtual ProcessMeleeAction* GetMeleeAction() const override
	{
		return m_hMeleeAction.ToMutableProcess();
	}

	virtual ProcessMeleeAction* GetNextMeleeAction() const override
	{
		return m_hNexMeleeAction.ToMutableProcess();
	}

	virtual const DC::MeleeAttack* GetNextMeleeAttack() const override
	{
		if (const ProcessMeleeAction* pNextAction = m_hNexMeleeAction.ToProcess())
		{
			return pNextAction->GetMeleeAttack();
		}
		return nullptr;
	}

	virtual const DC::AiMeleeType* GetMeleeType() const override
	{
		return &m_meleeType;
	}

	virtual void SetMeleeType(const DC::AiMeleeType& type) override
	{
		m_meleeType = type;
	}

	virtual void UpdateMeleeTypeFromHealth() override
	{
		// The brute needs to have his stun amount decremented as he gets low on health
		NavCharacter *pChar = GetCharacter();
		const IHealthSystem *pHealthSystem = pChar->GetHealthSystem();
		F32 healthPerc = pHealthSystem->GetHealthPercentage();

		if (pChar->IsKindOf(g_type_Npc))
		{
			Npc *pNpc = (Npc *)pChar;
// 			if (pNpc->IsBrute())
// 			{
// 				const DC::AiBruteSettings *pBruteSettings = pNpc->GetArchetype().GetInfo()->m_bruteSettings;
// 				I32 maxStuns = pBruteSettings ? pBruteSettings->m_meleeNumStuns : 3;
//
// 				NpcConfiguration::BruteInfo &bruteInfo = pNpc->GetConfiguration().m_bruteInfo;
//
// 				// We want to keep the number of stuns that have happened so far
// 				I32 timesStunned = maxStuns - bruteInfo.m_numStunsRemaining;
// 				I32 stunsFromDamage = maxStuns - (I32)((float)maxStuns * healthPerc);
//
// 				I32 highestStuns = timesStunned;
// 				if (timesStunned < stunsFromDamage)
// 				{
// 					highestStuns = stunsFromDamage;
// 				}
//
// 				bruteInfo.m_numStunsRemaining = maxStuns - highestStuns;
// 			}
		}

		if (healthPerc < kSteelFistHealth)
		{
			switch (m_meleeType.m_defense)
			{
			default:
				return;
			case DC::kAiMeleeDefenseMedium:
			case DC::kAiMeleeDefenseArmored:
				m_meleeType.m_defense = DC::kAiMeleeDefenseEasy;
				return;
			}
		}
	}

	virtual void Shutdown() override
	{
		AnimActionController::Shutdown();
	}

	virtual bool IsMeleeIdle() override
	{
		return m_requestedAnimId == SID("fist-strafe-idle");
	}

	const char *StrafeStateToString(IdleState state)
	{
		switch (state)
		{
		case kNone: return "kNone";
		case kNeedMeleeToIdle:return "kNeedMeleeToIdle";
		case kNeedLocoToIdle:return "kNeedLocoToIdle";
		case kTurningLeft:return "kTurningLeft";
		case kTurningRight:return "kTurningRight";
		case kIdle:return "kIdle";
		case kTurning:return "kTurning";
		case kRequestedIdleToMoveFw:return "kRequestedIdleToMoveFw";
		case kRequestedIdleToMoveBk:return "kRequestedIdleToMoveBk";
		case kRequestedIdleToMoveLt:return "kRequestedIdleToMoveLt";
		case kRequestedIdleToMoveRt:return "kRequestedIdleToMoveRt";
		case kIdleToMoveFwTransition:return "kIdleToMoveFwTransition";
		case kIdleToMoveBkTransition:return "kIdleToMoveBkTransition";
		case kIdleToMoveLtTransition:return "kIdleToMoveLtTransition";
		case kIdleToMoveRtTransition:return "kIdleToMoveRtTransition";
		case kIdleToMoveHandoff:return "kIdleToMoveHandoff";
		case kTrackingIdle:return "kTrackingIdle";
		case kTrackingMoveToMove: return "kTrackingMoveToMove";
		default: return "Not Translated";
		}
	}


	bool CanStartMoving() override
	{
		Npc* pNpc = (Npc*)(GetCharacter());

		switch (m_idleState)
		{
		case kTurningLeft:
		case kTurningRight:
		case kNeedMeleeToIdle:
		case kNeedLocoToIdle:
		case kIdleToMoveFwTransition:
		case kIdleToMoveBkTransition:
		case kIdleToMoveLtTransition:
		case kIdleToMoveRtTransition:
		case kRequestedIdleToMoveFw:
		case kRequestedIdleToMoveBk:
		case kRequestedIdleToMoveLt:
		case kRequestedIdleToMoveRt:
		case kTrackingMoveToMove:
			return false;
		case kIdleToMoveHandoff:

			return true;

			if (pNpc && m_stepAnimationAction.GetAnimPhase(pNpc->GetAnimControl()) >= 0.9f)
			{
				return true;
			}
			else
			{
				return false;
			}
		//case kNone:
		//	return true;
		default:
			if (m_stepAnimationAction.IsValid())
			{

				if (pNpc && m_stepAnimationAction.GetAnimPhase(pNpc->GetAnimControl()) == 0.0f)
					return false; // hasn't been taken yet
			}

			return true;
		}
	}

	void CancelMovingActions() override
	{
		if (m_stepAnimationAction.IsValid())
		{
			m_stepAnimationAction.Reset();
		}

		m_idleState = kNone;
	}

	void StartIdle(const DC::MeleeAttackBehavior *pDcMeleeBehavior) override
	{
		if (g_meleeOptions.m_multiCharacterBrawl)
			return;

		if (m_idleState != kIdle)
		{
			m_idleState = kNeedLocoToIdle;
			m_pDcMeleeBehavior = pDcMeleeBehavior;
		}
	}

	void TryGoForward() override
	{
		if (g_meleeOptions.m_multiCharacterBrawl)
			return;

		if (CanStartMoving() && m_idleState != kNone)
		{
			StringId64 charName = GetCharacter()->GetUserId();
			bool needMirror = IsRightFootForward(GetCharacter());
			if (needMirror && (m_idleState == kTrackingIdle || m_idleState == kIdle))
			{
				m_idleState = kRequestedIdleToMoveFw;
			}
		}
	}

	void TryGoBack() override
	{
		if (g_meleeOptions.m_multiCharacterBrawl)
			return;

		if (CanStartMoving() && m_idleState != kNone)
		{
			StringId64 charName = GetCharacter()->GetUserId();
			bool needMirror = IsRightFootForward(GetCharacter());
			if (needMirror)
			{
				m_idleState = kRequestedIdleToMoveBk;
			}
		}
	}

	void TryGoLeft(Point_arg refPt) override
	{
		if (g_meleeOptions.m_multiCharacterBrawl)
			return;

		if (CanStartMoving() && m_idleState != kNone)
		{
			StringId64 charName = GetCharacter()->GetUserId();
			bool needMirror = IsRightFootForward(GetCharacter());
			if (needMirror && (m_idleState == kTrackingIdle || m_idleState == kIdle))
			{
				m_idleState = kRequestedIdleToMoveLt;
				m_strafeReferencePt = refPt;
			}
		}
	}

	void TryGoRight(Point_arg refPt) override
	{
		if (g_meleeOptions.m_multiCharacterBrawl)
			return;

		if (CanStartMoving() && m_idleState != kNone)
		{
			StringId64 charName = GetCharacter()->GetUserId();
			bool needMirror = IsRightFootForward(GetCharacter());
			if (needMirror && (m_idleState == kTrackingIdle || m_idleState == kIdle))
			{
				m_idleState = kRequestedIdleToMoveRt;
				m_strafeReferencePt = refPt;
			}
		}
	}

	static bool IsAnimControllerBusyForSteps(const Npc* pNpc)
	{
		BitArray128 busyControllerExcludeBits;
		busyControllerExcludeBits.SetBit(kLocomotionController);
		busyControllerExcludeBits.SetBit(kMeleeActionController);
		busyControllerExcludeBits.SetBit(kWeaponController);
		busyControllerExcludeBits.SetBit(kHitController);
		busyControllerExcludeBits.SetBit(kPerformanceController);

		return pNpc->IsBusyExcludingControllers(busyControllerExcludeBits, kExcludeFades);
	}

	StringId64 GetStepMoveId(StepType type, StepDir dir) const
	{
		if (type < 0 || type >= kStepTypeCount)
			return INVALID_STRING_ID_64;

		if (dir < 0 || dir >= kStepDirCount)
			return INVALID_STRING_ID_64;

		const Npc* pNpc = Npc::FromProcess(GetCharacter());
		if (!pNpc)
			return INVALID_STRING_ID_64;

		const DC::AiMeleeSettings* pSettings = pNpc->GetArchetype().GetMeleeSettings();
		if (!pSettings)
			return INVALID_STRING_ID_64;


		switch (type)
		{
		case IAiMeleeActionController::kStepTypeQuick:
			switch (dir)
			{
			case IAiMeleeActionController::kStepDirLt: return pSettings->m_stepSettings.m_quickStepLt;
			case IAiMeleeActionController::kStepDirRt: return pSettings->m_stepSettings.m_quickStepRt;
			case IAiMeleeActionController::kStepDirFw: return pSettings->m_stepSettings.m_quickStepFw;
			case IAiMeleeActionController::kStepDirBw: return pSettings->m_stepSettings.m_quickStepBw;
			}
			break;

		case IAiMeleeActionController::kStepTypeReaction:
			switch (dir)
			{
			case IAiMeleeActionController::kStepDirLt: return pSettings->m_stepSettings.m_reactionStepLt;
			case IAiMeleeActionController::kStepDirRt: return pSettings->m_stepSettings.m_reactionStepRt;
			case IAiMeleeActionController::kStepDirFw: return pSettings->m_stepSettings.m_reactionStepFw;
			case IAiMeleeActionController::kStepDirBw: return pSettings->m_stepSettings.m_reactionStepBw;
			}
			break;
		}


		return INVALID_STRING_ID_64;
	}

	virtual bool CanStep(StepType type, StepDir dir) const override
	{
		Npc* pNpc = (Npc*)GetCharacter();

		if (pNpc->IsGrappled())
			return false;

		const StringId64 moveId = GetStepMoveId(type, dir);
		if (moveId == INVALID_STRING_ID_64)
			return false;

		const DC::MeleeAttack* pAttack = GetMeleeAttack(moveId);
		if (!pAttack || !pAttack->m_attackerInfo)
			return false;

		const StringId64 animId = pAttack->m_attackerInfo->m_anim;
		const ArtItemAnim* pAnim = pNpc->GetAnimControl()->LookupAnim(animId).ToArtItem();
		if (!pAnim)
			return false;

		const IAiHitController* pHitController = pNpc->GetAnimationControllers()->GetHitController();
		if (pHitController && pHitController->IsHitReactionPlaying() && pHitController->IsFullBodyReactionPlaying())
			return false;

		BitArray128 busyControllerExcludeBits;
		busyControllerExcludeBits.SetBit(kLocomotionController);
		busyControllerExcludeBits.SetBit(kWeaponController);
		busyControllerExcludeBits.SetBit(kHitController);
		busyControllerExcludeBits.SetBit(kDodgeController);

		const bool busy = pNpc->IsBusyExcludingControllers(busyControllerExcludeBits, kExcludeFades);
		if (busy)
			return false;

		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		const Locator npcLoc = pNpc->GetLocator();
		const NavMesh* pNpcMesh = pNpc->GetNavLocation().ToNavMesh();

		Locator endAp = npcLoc;
		if (const NdGameObject* pTarget = pNpc->GetCurrentMeleeTargetProcess())
		{
			const Vector targetDir = Normalize(pTarget->GetTranslation() - pNpc->GetTranslation());
			endAp.SetRotation(QuatFromLookAt(targetDir, kUnitZAxis));
		}
		DC::SelfBlendParams selfBlend;
		selfBlend.m_curve = DC::kAnimCurveTypeLinear;
		selfBlend.m_phase = 0.0f;
		selfBlend.m_time = GetDuration(pAnim);

		NavMesh::ProbeParams probeParams;
		const AI::AnimClearMotionDebugOptions debugOptions(g_aiGameOptions.m_melee.m_debugStepMotionClearnce);
		const bool clearMotion = AI::AnimHasClearMotion(pNpc->GetSkeletonId(), pAnim, npcLoc, endAp, &selfBlend, pNpcMesh, probeParams, false, debugOptions);
		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_melee.m_debugStepMotionClearnce))
		{
			Locator midLocLs;
			if (EvaluateChannelInAnim(pNpc->GetSkeletonId(), pAnim, SID("align"), 0.5f, &midLocLs))
			{
				const Point midPos = npcLoc.TransformLocator(midLocLs).GetPosition();
				g_prim.Draw(DebugString(midPos, DevKitOnly_StringIdToString(animId), clearMotion ? kColorGreen : kColorRed, 0.5f), debugOptions.m_duration);
			}
		}

		return clearMotion;
	}

	virtual bool TryStep(StepType type, StepDir dir) override
	{
		if (!CanStep(type, dir))
			return false;

		return DoStep(type, dir);
	}

	virtual bool DoStep(StepType type, StepDir dir) override
	{
		Npc* pNpc = (Npc*) GetCharacter();
		AiEncounterCoordinator* pCoord = pNpc->GetEncounterCoordinator();
		AiEncounterMelee& melee = pCoord->GetMelee();

		if (IsAnimControllerBusyForSteps(pNpc))
			return false;

		const StringId64 moveId = GetStepMoveId(type, dir);
		if (moveId == INVALID_STRING_ID_64)
			return false;

		MeleeMoveSearchParams searchParams;
		searchParams.m_context = pNpc->GetMeleeContext();
		searchParams.AddAttacker(pNpc);
		searchParams.m_scriptRequest = true;
		searchParams.m_moveOrListId = moveId;

		MeleeMoveStartParams startParams;
		if (!FindMeleeMove(searchParams, startParams))
			return false;

		ProcessMeleeAction::StartMeleeMove(startParams);
		melee.NotifyQuickStep();
		return true;
	}

	StringId64 ChooseIdleAnim(bool &tracking)
	{
		if (m_pDcMeleeBehavior && m_pDcMeleeBehavior->m_idleList && m_pDcMeleeBehavior->m_idleList->m_count)
		{
			int index = rand() % m_pDcMeleeBehavior->m_idleList->m_count;

			const DC::MeleeBehaviorIdleAnim &idleAnim = m_pDcMeleeBehavior->m_idleList->m_array[index];

			tracking = idleAnim.m_flags & DC::kMeleeBehaviorIdleAnimFlagsTrackPlayer;

			return idleAnim.m_anim;

		}
		tracking = false;
		return SID("idle-fight");

	}

	void UpdateStrafe()
	{
		Npc* pNpc = (Npc*)(GetCharacter());

		if (!pNpc)
			return;

		AnimControl *pAnimControl = pNpc->GetAnimControl();

		Point dest;
		float radius;
		Vector npcToLookAt = GetLocalZ(pNpc->GetLocator());

		if (m_hMeleeAction.ToProcess())
		{
			m_stepAnimationAction.Reset();
			return;
		}

		if (pNpc->IsInScriptedAnimationState())
		{
			m_stepAnimationAction.Reset();
			return;
		}


		if (m_stepAnimationAction.IsValid())
		{
			m_stepAnimationAction.Reset();
		}

		m_idleState = kNone;
		return;
	}

	virtual void UpdateStatus() override
	{
		Npc* pNpc = (Npc*)(GetCharacter());
		const AiEntity* pTarget = pNpc->GetCurrentMeleeTargetEntity();
		const bool busy = IsBusy();

		if (busy)
		{
			m_hadMeleeAction = true;
			m_recentlyHadMeleeAction = false;
		}
		else
		{
			if (m_hadMeleeAction)
				m_recentlyHadMeleeAction = true;
			else
				m_recentlyHadMeleeAction = false;

			m_hadMeleeAction = false;
		}

		if (m_waitingForIdleTransition)
		{
			m_waitingForIdleTransition = WaitingForIdleTransition();
		}

		if (Process* pFollowAction = m_hPlayerAnimFollowAction.ToMutableProcess())
		{
			// We must do this here, so that we can move the apRef before UpdateRootLocator() is called on the Npc
			SendEvent(SID("update-npc-ap-ref"), pFollowAction);

			/*
			if (pTarget)
			{
				if (pTarget->ToGameObject()->IsType(SID("Player")))
				{
					Player* pPlayer = const_cast<Player*>(static_cast<const Player*>(pTarget->ToGameObject()));

					pPlayer->m_pMeleeController->GetNpcMeleeManager()->RemoveNpc(pNpc);
				}
			}
			*/
		}

		AnimControl *pAnimControl = pNpc->GetAnimControl();

		// Update our block partial animation!
		StringId64 blockState = g_factMgr.FindFact(SID("block-state"), *pNpc->GetFactDict()).GetAsStringId();

		if (blockState == SID("none"))
		{
			blockState = INVALID_STRING_ID_64;
		}

		if (m_lastBlockState != blockState)
		{
			StringId64 state;

			if (blockState == SID("low"))
			{
				// Low block
				state = SID("s_melee-block-low");
			}
			else if (blockState == SID("high"))
			{
				// High block
				state = SID("s_melee-block-high");
			}
			else
			{
				// Not blocking at all!
				state = INVALID_STRING_ID_64;
			}

			AnimStateLayer *pLayer = pAnimControl->GetStateLayerById(SID("melee-partial"));
			if (pLayer)
			{
				if (state == INVALID_STRING_ID_64)
				{
					m_blockPartialAnimAction.Reset();
					pLayer->Fade(0.0f, 0.2f);
				}
				else
				{
					FadeToStateParams params;
					params.m_animFadeTime = 0.0f;
					m_blockPartialAnimAction.FadeToState(pAnimControl, state, params);
					pLayer->Fade(1.0f, 0.2f);
				}
			}

			m_lastBlockState = blockState;
		}



		m_weaponGripAnimAction.Update(pAnimControl);

		m_blockPartialAnimAction.Update(pAnimControl);

		m_stepAnimationAction.Update(pAnimControl);
		UpdateStrafe();

		if (m_hGrappleAction.ToProcess())
		{
			SendEvent(SID("update-npc-ap-ref"), m_hGrappleAction);
		}
	}

	void UpdateMeleeHandsAnim()
	{
		Npc* pNpc = (Npc*)(GetCharacter());
		if (!pNpc)
			return;

		AnimControl *pAnimControl = pNpc->GetAnimControl();

		if (m_enableWeaponGrip && InMeleeState() && pNpc->GetWeaponInHand())
			RefreshHandsOverlays();
		else
			ClearHandsOverlays();

		if (AnimSimpleLayer* pMeleeHandsLayer = pAnimControl->GetSimpleLayerById(SID("melee-hands")))
		{
			StringId64 oldResolvedId = m_weaponGripAnim.GetFinalResolvedId();
			if (m_scheduledAnim != INVALID_STRING_ID_64)
			{
				m_weaponGripAnim.SetSourceId(m_scheduledAnim);
				m_scheduledAnim = INVALID_STRING_ID_64;
			}

			CachedAnimLookup newWeaponGripAnim = pAnimControl->LookupAnimCached(m_weaponGripAnim);
			if (newWeaponGripAnim.GetFinalResolvedId() != oldResolvedId)
			{
				if (newWeaponGripAnim.GetAnim().ToArtItem())
				{
					if (m_weaponGripAnimAction.FadeToAnim(pAnimControl, newWeaponGripAnim.GetFinalResolvedId(), 0.0f, 0.0f))
						pMeleeHandsLayer->Fade(1.0f, 0.1f);
					else
						pMeleeHandsLayer->Fade(0.0f, 0.1f);
				}
				else
				{
					pMeleeHandsLayer->Fade(0.0f, 0.1f);
				}
			}
			m_weaponGripAnim = newWeaponGripAnim;
		}
	}
	virtual void RequestAnimations() override
	{
		ParentClass::RequestAnimations();

		UpdateMeleeHandsAnim();
	}

	bool InMeleeState () const
	{
		const Npc *pNpc = (Npc *)GetCharacter();
		const AnimStateLayer *pBaseLayer = pNpc->GetAnimControl()->GetBaseStateLayer();

		StringId64 curState = pBaseLayer->CurrentStateId();

		switch (curState.GetValue())
		{
		case SID_VAL("s_npc-melee-ground-ik"):
		case SID_VAL("s_npc-melee-ground-ik-no-ap"):
		case SID_VAL("s_npc-melee-ground-ik-navmesh"):
		case SID_VAL("s_npc-melee-ground-ik-no-ap-navmesh"):
		case SID_VAL("s_npc-melee-no-ground-ik"):
		case SID_VAL("s_npc-melee-no-ground-ik-no-ap"):
		case SID_VAL("s_npc-melee-no-ground-ik-navmesh"):
		case SID_VAL("s_npc-melee-no-ground-ik-no-ap-navmesh"):
		case SID_VAL("s_npc-melee-ground-no-ik"):
		case SID_VAL("s_npc-melee-ground-no-ik-no-ap"):
		case SID_VAL("s_npc-melee-ground-no-ik-navmesh"):
		case SID_VAL("s_npc-melee-ground-no-ik-no-ap-navmesh"):
		case SID_VAL("s_npc-melee-no-ground-no-ik"):
		case SID_VAL("s_npc-melee-no-ground-no-ik-no-ap"):
		case SID_VAL("s_npc-melee-no-ground-no-ik-navmesh"):
		case SID_VAL("s_npc-melee-no-ground-no-ik-no-ap-navmesh"):

		case SID_VAL("s_grapple-stand"):

		case SID_VAL("s_idle-fight-mirror"):
			return true;
			break;
		}

		return false;
	}

	virtual bool IsBusyInMelee() const override
	{
		const ProcessMeleeAction *pAction = m_hMeleeAction.ToProcess();
		bool result = pAction != nullptr;

		Npc* pNpc = (Npc*)(GetCharacter());
		if (pAction)
		{
			CharacterMeleeControl *pControl = pAction->GetCharControlFor(pNpc);
			if (pControl && pControl->GetCharacterMeleeInfo()->m_moveMode == DC::kMeleeMoveModeGesture)
				return false;

			if (pControl && pControl->GetCharacterMeleeInfo()->m_moveMode == DC::kMeleeMoveModePhantom)
				return false;
		}

		if (m_hPlayerAnimFollowAction.ToProcess())
		{
			result = true;
		}
		else if (m_waitingForIdleTransition)
		{
			// If we aren't in a melee state, we're not busy.
			if (!InMeleeState())
			{
				result = false;
			}
			else if (!pNpc->IsDead())
			{
				// Wait until we're done transitioning back to idle, UNLESS we're dead!
				result = true;
			}
		}
		//MsgOut("MeleeActionControl %s busy: %s\n", pNpc->GetName(), result ? "YES" : "NO");

		return result;
	}

	virtual bool IsBusy() const override
	{
		const ProcessMeleeAction *pAction = m_hMeleeAction.ToProcess();
		bool result = pAction != nullptr;

		Npc* pNpc = (Npc*)(GetCharacter());

		if (pAction)
		{
			CharacterMeleeControl *pControl = pAction->GetCharControlFor(pNpc);
			if (pControl && pControl->GetCharacterMeleeInfo()->m_moveMode == DC::kMeleeMoveModeGesture)
				return false;

			if (pControl && pControl->GetCharacterMeleeInfo()->m_moveMode == DC::kMeleeMoveModePhantom)
				return false;
		}

		if (m_hGrappleAction.Valid())
		{
			result = true;
		}

		if (m_hPlayerAnimFollowAction.ToProcess())
		{
			result = true;
		}
		else if (m_waitingForIdleTransition)
		{
			// If we aren't in a melee state, we're not busy.
			if (!InMeleeState())
			{
				result = false;
			}
			else if (!pNpc->IsDead())
			{
				// Wait until we're done transitioning back to idle, UNLESS we're dead!
				result = true;
			}
		}
		else if (m_stepAnimationAction.IsValid() && !m_stepAnimationAction.IsDone())
		{
			const bool kStepAnimActionIsBusy = true;
			result = kStepAnimActionIsBusy;
		}
		else if (m_idleState == kTrackingMoveToMove) // this animation is not triggered by our action
		{
			result = true;
		}

		//MsgConPauseable("MeleeActionControl %s busy: %s\n", pNpc->GetName(), result ? "YES" : "NO");

		return result;
	}

	virtual void Interrupt() override
	{
		AbortMeleeAction();
	}

	virtual F32 GetMinStoppingPowerForHitReaction() const override
	{
		const Character* pChar = GetCharacter();
		if (pChar && pChar->IsDead())
			return 0.0f;

		const ProcessMeleeAction *pAction = m_hMeleeAction.ToProcess();
		if (!pAction)
			return 0.0f;

		const F32 minStoppingPower = pAction->GetMinStoppingPowerForHitReaction();
		return minStoppingPower;
	}

	virtual bool CanPlayHitReaction(F32 stoppingPower) const override
	{
		const Character* pChar = GetCharacter();
		if (pChar && pChar->IsDead())
			return true;

		const ProcessMeleeAction *pAction = m_hMeleeAction.ToProcess();
		if (!pAction)
			return true;

		if (!pAction->IsUninterruptible())
			return true;

		const F32 minStoppingPower = GetMinStoppingPowerForHitReaction();
		return stoppingPower >= minStoppingPower;
	}

	virtual bool CanPlayExplosionReaction() const override
	{
		return true;
	}

	virtual void OnHitReactionPlayed() override
	{
		Npc* pNpc = static_cast<Npc*>(GetCharacter());

		if (!pNpc->IsDead())
		{
			if (IsBusy())
			{
				MsgPlayer("Npc %s Abort melee move because of hit reaction\n", GetCharacter()->GetName());
				if (const ProcessMeleeAction *pAction = m_hMeleeAction.ToProcess())
				{
					if (pAction->IsRemoteMove())
					{
						if (pAction->GetNumCharacters() == 1)
						{
							MsgPlayer("Sending SendNetEventAbortMelee()\n");

							// if this is a remote move for npc who has aborted on this machine because of
							// something that happened on this machine, like explosion damage.. we want to send the event
							SendNetEventAbortMelee(pAction);
						}
					}
				}
				AbortMeleeAction();
			}
		}
	}

	virtual void LogMeleeReact(const CloseCombatAttackInfo* pCloseAttack, StringId64 playedAnim) override
	{
		if (g_netInfo.IsNetActive())
		{
			m_hitReactionMeleeAttackId = pCloseAttack->m_attackId;

			if (pCloseAttack->m_hSourceGameObj.IsKindOf(g_type_Character))
			{
				m_hHitReactionMeleeAttacker = (Character*)(pCloseAttack->m_hSourceGameObj.ToProcess());
			}
			m_hitReactionMeleeAnim = playedAnim;
			m_hitReactionAccessTime = -1;
		}
	}

	virtual void GetLastMeleeAttackInfo(StringId64 &meleeAttackId, CharacterHandle	&hAttacker, StringId64 &anim) const override
	{
		if (m_hitReactionAccessTime == -1 || m_hitReactionAccessTime == GetCurrentFrameNumber())
		{
			meleeAttackId = m_hitReactionMeleeAttackId;
			hAttacker = m_hHitReactionMeleeAttacker;
			anim = m_hitReactionMeleeAnim;
			m_hitReactionAccessTime = GetCurrentFrameNumber();
		}
	}

	virtual void OnScriptedAnimationStart() override
	{
		if (IsBusy())
		{
			MsgPlayer("Npc %s End melee move because of scripted animation\n", GetCharacter()->GetName());
		}

		StopMeleeAction();
	}

	virtual U32 GetNumHits() const override
	{
		return m_numStunPunches;
	}

	virtual void IncrementNumHits() override
	{
		++m_numStunPunches;
		++m_numComboPunchesLanded;
	}

	virtual void ClearNumHits() override
	{
		m_numStunPunches = 0;
	}

	virtual void ResetNumHitsWithIndex (I32 index) override
	{
		m_numComboPunchesLanded += index;
		if (m_numComboPunchesLanded < 0)
		{
			m_numComboPunchesLanded = 0;
		}
	}

	virtual void IncrementBlocks() override
	{
		m_numComboPunchesLanded = 0;
	}

	virtual bool WasKilledByMelee() const override { return m_killedByMelee;}
	virtual void SetKilledByMelee() override { m_killedByMelee = true;}

	virtual void SetDamagedByMelee(StringId64 attackerId, Point_arg hitPos, Vector_arg hitDir) override
	{
		m_damagedByMelee = true;
		m_damagedByMeleeAttacker = attackerId;
		m_lastHitDir = hitDir;
		m_lastHitPos = hitPos;
		if (NavCharacter *pChar = GetCharacter())
		{
			m_damagedByMeleeTime = pChar->GetClock()->GetCurTime();
		}
	}

	virtual Point GetLastHitPos() override { return m_lastHitPos; }
	virtual Vector GetLastHitDir() override { return m_lastHitDir; }

	virtual void ClearDamagedByMelee() override { m_damagedByMelee = false; }
	virtual bool WasDamagedByMelee() const override { return m_damagedByMelee; }

	virtual bool WasDamagedByMeleeBy(StringId64 userId, TimeDelta time) const override
	{
		if (m_damagedByMelee && m_damagedByMeleeAttacker == userId)
		{
			if (NavCharacter *pChar = GetCharacter())
			{
				bool timePassed = pChar->GetClock()->TimePassed(time, m_damagedByMeleeTime);

				return !timePassed;
			}
		}

		return false;
	}
};


IAiMeleeActionController* CreateAiMeleeActionController()
{
	return NDI_NEW MeleeActionController;
}
