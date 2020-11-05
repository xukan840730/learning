/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/util/timeframe.h"

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

class ProcessMeleeAction;
class ProcessPlayerAnimFollower;
struct CloseCombatAttackInfo;
FWD_DECL_PROCESS_HANDLE(Character);
class ProcessPlayerGrappleT1;
namespace DC
{
	struct AiMeleeType;
	struct MeleeBlendoutInfo;
	struct MeleeAttackBehavior;
	struct MeleeAttack;
} // namespace DC

struct IAiMeleeActionController : public AnimActionController
{
	enum StepType
	{
		kStepTypeInvalid = -1,
		kStepTypeQuick,
		kStepTypeReaction,
		kStepTypeCount
	};
	enum StepDir
	{
		kStepDirInvalid = -1,
		kStepDirFw,
		kStepDirBw,
		kStepDirLt,
		kStepDirRt,
		kStepDirCount
	};

	virtual void StartMeleeAction(ProcessMeleeAction* pAction, StringId64 weaponHandsAnim) = 0;
	virtual void StartGrappleAction(Process* pAction) = 0;
	virtual void StartFollowAnimAction(ProcessPlayerAnimFollower* pAction) = 0;
	virtual void StopMeleeAction()	= 0; // Called to stop the current move when another is starting
	virtual void AbortMeleeAction() = 0;
	virtual void MeleeActionEnded(bool aborted)		   = 0; // Called when a move is finished
	virtual ProcessMeleeAction* GetMeleeAction() const = 0;
	virtual ProcessMeleeAction* GetNextMeleeAction() const = 0;
	virtual const DC::MeleeAttack* GetNextMeleeAttack() const = 0;
	virtual bool IsAttacking() const		= 0;
	virtual bool IsBeingBlocked() const		= 0;
	virtual void OnCharacterProcessKilled() = 0;
	virtual void OnNpcDeath() = 0;

	virtual const DC::AiMeleeType* GetMeleeType() const	   = 0;
	virtual void SetMeleeType(const DC::AiMeleeType& type) = 0;
	virtual void UpdateMeleeTypeFromHealth() = 0;

	virtual U32 GetNumHits() const = 0;
	virtual int GetNumComboPunchesLanded() const = 0;

	virtual F32 GetLastBlendInfo() const = 0;

	virtual void IncrementNumHits() = 0;
	virtual void ClearNumHits()		= 0;
	virtual void ResetNumHitsWithIndex(I32 index) = 0;

	virtual void IncrementBlocks() = 0;

	virtual bool WasKilledByMelee() const = 0;
	virtual void SetKilledByMelee()		  = 0;

	virtual void SetDamagedByMelee(StringId64 attackerId, Point_arg pos, Vector_arg dir) = 0;
	virtual Point GetLastHitPos()	   = 0;
	virtual Vector GetLastHitDir()	   = 0;
	virtual void ClearDamagedByMelee() = 0;
	virtual bool WasDamagedByMelee() const = 0;
	virtual bool WasDamagedByMeleeBy(StringId64 userId, TimeDelta time) const = 0;

	virtual bool IsBusyInMelee() const = 0;

	virtual F32 GetMinStoppingPowerForHitReaction() const	 = 0;
	virtual bool CanPlayHitReaction(F32 stoppingPower) const = 0;
	virtual bool CanPlayExplosionReaction() const = 0;

	virtual bool CanStartMoving()	   = 0;
	virtual void CancelMovingActions() = 0;
	virtual void StartIdle(const DC::MeleeAttackBehavior* pDcMeleeBehavior) = 0;

	virtual void TryGoForward() = 0;
	virtual void TryGoBack()	= 0;
	virtual void TryGoLeft(Point_arg refPt)	 = 0;
	virtual void TryGoRight(Point_arg refPt) = 0;

	virtual bool CanStep(StepType type, StepDir dir) const = 0;
	virtual bool TryStep(StepType type, StepDir dir)	   = 0;
	virtual bool DoStep(StepType type, StepDir dir)		   = 0;

	virtual void LogMeleeReact(const CloseCombatAttackInfo* pCloseAttack, StringId64 playedAnim) = 0;
	virtual void GetLastMeleeAttackInfo(StringId64& meleeAttackId, CharacterHandle& hAttacker, StringId64& anim) const = 0;

	virtual void OnScriptedAnimationStart() = 0;

	virtual bool IsMeleeIdle() = 0;

	virtual TimeFrame GetLastAttackTime() const		= 0;
	virtual bool HasJustFinishedMeleeAction() const = 0;
};

IAiMeleeActionController* CreateAiMeleeActionController();
