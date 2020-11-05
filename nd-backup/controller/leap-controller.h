/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/util/finite-state-machine.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/controller/action-pack-controller.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nav/leap-action-pack.h"

#include "game/scriptx/h/melee-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class Character;
class HavokSphereCastJob;

 struct LeapAttackInfo
 {
	 LeapAttackInfo() { Clear(); }

	 void Clear()
	 {
		 m_hAp = ActionPackHandle();
		 m_leapAttack.m_leapType = DC::kLeapAttackTypeCount;
		 m_isValid = false;
		 m_clearanceProbe.Close();
	 }

	 ActionPackHandle	m_hAp;
	 HavokSphereCastJob	m_clearanceProbe;
	 DC::LeapAttack		m_leapAttack;
	 bool				m_isValid;
 };

/// --------------------------------------------------------------------------------------------------------------- ///
class AiLeapController : public ActionPackController
{
public:
	typedef ActionPackController ParentClass;

	class BaseState : public Fsm::TState<AiLeapController>
	{
	public:
		FSM_BIND_TO_SELF(AiLeapController);
		virtual void RequestAnimations() {}
		virtual void UpdateStatus() {}
		virtual bool IsBusy() const { return false; }

		NdGameObject* GetCharacterGameObject() const { return Self().GetCharacterGameObject(); }
	};

	typedef Fsm::TStateMachine<BaseState> StateMachine;

	FSM_BASE_DECLARE(StateMachine);

public:
	FSM_STATE_DECLARE(None);
	FSM_STATE_DECLARE_ARG(Entering, ActionPackEntryDef);
	FSM_STATE_DECLARE(Waiting);
	FSM_STATE_DECLARE(Leaping);
	FSM_STATE_DECLARE(Landing);

	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	virtual void Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void Reset() override;
	virtual void Interrupt() override;

	// For Fsm
	U32F GetMaxStateAllocSize() const;
	void GoInitialState() { GoNone(); }
	const Clock* GetClock() const;

	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual bool IsBusy() const override;
	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;

	void UpdateBestLeapAps();

	virtual bool ResolveDefaultEntry(const ActionPackResolveInput& input,
									 const ActionPack* pActionPack,
									 ActionPackEntryDef* pDefOut) const override;

	virtual bool ResolveEntry(const ActionPackResolveInput& input,
							  const ActionPack* pActionPack,
							  ActionPackEntryDef* pDefOut) const override;

	virtual bool UpdateEntry(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 ActionPackEntryDef* pDefOut) const override;

	virtual void Enter(const ActionPackResolveInput& input,
					   ActionPack* pActionPack,
					   const ActionPackEntryDef& entryDef) override;
	virtual void Exit(const PathWaypointsEx* pExitPathPs) override;

	virtual void DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const override {}
	virtual void DebugDrawExits(const ActionPackResolveInput& input,
								const ActionPack* pActionPack,
								const IPathWaypoints* pPathPs) const override
	{
	}

	const ActionPackHandle GetLeapApHandle() const { return m_hActionPack; }

	bool SetLeapAttack(DC::LeapAttackTypeMask typeMask);
	bool IsCurrentLeapValid();

	bool TryStartMeleeMove();
	bool GetDefenderApRef(const ArtItemAnim* pLeapAnim, BoundFrame* pApRefOut) const;

	const LeapAttackInfo& GetBestLeapAttack(DC::LeapAttackType leapType)
	{
		AI_ASSERT(leapType < DC::kLeapAttackTypeCount);
		return m_aBestLeap[leapType];
	}

	const char* GetLeapAttackTypeName(DC::LeapAttackType leapType) const;

	MutableCharacterHandle m_hDefender;
	DC::LeapAttack m_leapAttack;

private:
	static bool SetupLeapClearanceProbe(HavokSphereCastJob& sphereJob,
										const NdGameObject* pTarget,
										const ActionPack* pLeapAp);
	static void ProcessLeapClearanceProbe(HavokSphereCastJob& sphereJob, bool& isClear);

private:
	LeapAttackInfo m_aBestLeap[DC::kLeapAttackTypeCount];

public:
	static BoundFrame GetLeapApForGround(const LeapActionPack* pLeapAp);
	static BoundFrame GetLeapApForLip(const LeapActionPack* pLeapAp);
};

/// --------------------------------------------------------------------------------------------------------------- ///
AiLeapController* CreateAiLeapController();
