/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/tap-controller.h"

#include "gamelib/camera/camera-manager.h"
#include "gamelib/gameplay/ai/agent/nav-character-adapter.inl"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/level/level-mgr.h"

#include "common/audio/dialog-manager.h"

#include "game/ai/agent/npc.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/controller/locomotion-controller.h"
#include "game/scriptx/h/hit-reactions-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
TapController* CreateTapController()
{
	return NDI_NEW TapController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F TapController::GetMaxStateAllocSize()
{
	const U32F stateAllocSize = sizeof(BaseState) + 256;

	return Max(ParentClass::GetMaxStateAllocSize(), stateAllocSize);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TapController::Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl)
{
	ParentClass::Init(pCharacter, pNavControl);

	StateMachine& fsm = GetStateMachine();
	fsm.Init(this, FILE_LINE_FUNC, true, sizeof(BoundFrame));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TapController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	ParentClass::Init(pNavChar, pNavControl);

	StateMachine& fsm = GetStateMachine();
	fsm.Init(this, FILE_LINE_FUNC, true, sizeof(BoundFrame));
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 TapController::CollectHitReactionStateFlags() const
{
	const NavCharacter* pNavChar    = GetCharacter();
	const TraversalActionPack* pTap = GetTap();

	if (!pTap || (pTap->GetType() != ActionPack::kTraversalActionPack))
	{
		return DC::kHitReactionStateMaskAdditiveOnly;
	}

	U64 state = 0;

	if (pTap->AreNormalDeathsAllowed() && pNavChar && pNavChar->IsDead())
	{
		state |= DC::kHitReactionStateMaskMoving;
	}
	else if (pTap->IsLadder())
	{
		if (m_onGround)
		{
			state |= DC::kHitReactionStateMaskIdle;
		}
		else
		{
			state |= DC::kHitReactionStateMaskAdditiveOnly;
		}
	}
	else if (m_onGround || (m_enableNavMeshAdjust && m_groundAdjustFactor > 0.9f))
	{
		const Vector velPs = pNavChar->GetVelocityPs();
		const float speedPs = Length(velPs);

		if (speedPs > 2.0f)
		{
			state |= DC::kHitReactionStateMaskMoving;
		}
		else
		{
			state |= DC::kHitReactionStateMaskIdle;
		}
	}
	else
	{
		state |= DC::kHitReactionStateMaskAdditiveOnly;
	}

	return state;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TapController::IsAimingFirearm() const
{
	const TraversalActionPack* pTap = GetTap();
	if (!pTap)
	{
		return false;
	}

	const NdGameObject* pCharacter = GetCharacterGameObject();
	if (!pTap)
	{
		return false;
	}

	AnimControl* pAnimControl = pCharacter->GetAnimControl();
	DC::AnimTopInfo* pTopInfo = pAnimControl->TopInfo<DC::AnimTopInfo>();

	return pTopInfo->m_gestureEnableBlend > 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TapController::WasShot() const
{
	bool wasShot = false;

	if (const Npc* pNpc = Npc::FromProcess(GetCharacter()))
	{
		const TimeFrame timeInTap = GetContextProcessCurTime() - m_enterTime;
		wasShot = pNpc->GetGotShotTimeElapsed() < timeInTap;
	}

	return wasShot;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TapController::RequestDialog(StringId64 dialogId, NdGameObject* pSpeaker, const TraversalActionPack* pTap)
{
	if (!pTap)
	{
		return;
	}

	AUTO_FACT_DICTIONARY(facts);

	facts.Set(SID("speaker"), pSpeaker->GetUserId());
	facts.Set(SID("action-pack"), pTap->GetSpawnerId());
	facts.Set(SID("action-pack-type"), pTap->GetTypeIdForScript());
	facts.Set(SID("boost-climb-up"), false);

	EngineComponents::GetDialogManager()->RequestDialog(dialogId, facts);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TapController::EnterRopeClimb()
{
	m_ropeClimbDir = 1.0f;
	GoRopeClimb();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TapController::SetRopeClimbDir(float dir)
{
	m_ropeClimbDir = dir;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float TapController::GetRopeClimbDir() const
{
	return m_ropeClimbDir;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Demeanor TapController::GetFinalDemeanor() const
{
	const NavCharacterAdapter pNavChar = GetCharacterAdapter();
	AI_ASSERT(pNavChar);

	Demeanor desiredDemeanor = pNavChar->GetRequestedDemeanor();

	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		const NavMesh* pDestNavMesh = GetDestNavMesh();
		StringId64 forcedDemId		= AI::NavMeshForcedDemeanor(pDestNavMesh);

		Demeanor forcedDem;
		if (DemeanorFromSid(forcedDem, forcedDemId) && pNavChar->HasDemeanor(forcedDem.ToI32()))
		{
			desiredDemeanor = forcedDem;
		}
	}

	return desiredDemeanor;
}
