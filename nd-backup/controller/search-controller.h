/*
 * Copyright (c) 2008 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-action.h"
#include "ndlib/util/finite-state-machine.h"

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/controller/action-pack-controller.h"
#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/search-action-pack.h"
#include "gamelib/scriptx/h/ap-entry-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static float kTenseCornerCheckCooldown = 40.0f;

namespace DC
{
	struct CornerCheckAnims;
}

class SearchActionPack;

/// --------------------------------------------------------------------------------------------------------------- ///
class AiSearchController : public ActionPackController
{
	typedef ActionPackController ParentClass;

private:
	class BaseState : public Fsm::TState<AiSearchController>
	{
	public:
		virtual void OnEnter() override {}
		virtual void UpdateStatus() {}
		virtual bool TryIdlePerformance() { return false; }
		virtual bool TryStartPerformance() { return false; }
		virtual bool TryAcknowledgePerformance() { return false; }
		virtual void SetGoalLoc(const NavLocation& navLoc) {}
		virtual bool ShouldInterruptNavigation() const { return false; }

		virtual bool IsBusy() const { return false; }
		virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const {}

		virtual void OnReset() {}
		virtual void OnInterrupt() {}

		NavCharacter* GetCharacter() { return GetSelf()->GetCharacter(); }
		const NavCharacter* GetCharacter() const { return GetSelf()->GetCharacter(); }
	};

	typedef Fsm::TStateMachine<BaseState> StateMachine;

	FSM_BASE_DECLARE(StateMachine);

	FSM_STATE_DECLARE(Inactive);
	FSM_STATE_DECLARE(EnterCornerCheck);
	FSM_STATE_DECLARE(ExitCornerCheck);
	FSM_STATE_DECLARE(WaitPerformance);

public:
	// FSM INTERFACE
	U32F GetMaxStateAllocSize() const;

	const Clock* GetClock() const { return GetCharacter()->GetClock(); }
	void GoInitialState() { GoInactive(); }

	// CONTROLLER INTERFACE
	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	virtual void Reset() override;
	virtual void Interrupt() override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual U64 CollectHitReactionStateFlags() const override;
	virtual void ConfigureCharacter(Demeanor demeanor,
									const DC::NpcDemeanorDef* pDemeanorDef,
									const NdAiAnimationConfig* pAnimConfig) override;
	virtual void UpdateStatus() override;
	virtual bool IsBusy() const override;
	virtual bool ShouldInterruptNavigation() const override;
	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;

	// ACTION PACK INTERFACE
	virtual bool ResolveEntry(const ActionPackResolveInput& input,
							  const ActionPack* pActionPack,
							  ActionPackEntryDef* pDefOut) const override;
	virtual bool ResolveDefaultEntry(const ActionPackResolveInput& input,
									 const ActionPack* pActionPack,
									 ActionPackEntryDef* pDefOut) const override;
	virtual bool UpdateEntry(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 ActionPackEntryDef* pDefOut) const override;
	virtual void Enter(const ActionPackResolveInput& input,
					   ActionPack* pActionPack,
					   const ActionPackEntryDef& entryDef) override;

	virtual void DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const override;
	virtual void DebugDrawExits(const ActionPackResolveInput& input,
								const ActionPack* pActionPack,
								const IPathWaypoints* pPathPs) const override;

	// SEARCH CONTROLLER INTERFACE
	bool TryIdlePerformance();
	bool TryStartPerformance();
	bool TryAcknowledgePerformance();

	void SetGoalLoc(const NavLocation& navLoc);
	NavLocation GetGoalLoc() const;
	RegionHandle GetPulloutRegionHandle() const;

	bool IsInCornerCheck() const;
	TimeFrame GetLastCornerCheckTime() const { return m_lastCornerCheckTime; }
	
	bool IsInProneCheck() const;
	TimeFrame GetLastProneCheckTime() const { return m_lastProneCheckTime; }

	const SearchActionPack* GetCornerCheckAp() const { return m_hCorner.ToActionPack<SearchActionPack>(); }

	// m_hCorner becomes null once exit starts, but not m_hLatestCorner
	const SearchActionPack* GetLatestCornerCheckAp() const { return m_hLatestCorner.ToActionPack<SearchActionPack>(); }

	const PathWaypointsEx* GetPathPs() const;

	I32 GetEnterAnimCount(const ActionPack* pActionPack) const;
	I32 GetExitAnimCount(const ActionPack* pActionPack) const;

private:
	bool ResolveLastChanceEntry(const ActionPackResolveInput& input,
								const ActionPack* pActionPack,
								ActionPackEntryDef* pDefOut) const;

	virtual void MakeExitCharacterState(const ActionPack* pActionPack,
										const IPathWaypoints* pPathPs,
										ApExit::CharacterState* pCsOut) const override;

	void ConfigureCornerCheckNavigationHandoff();
	void EndCornerCheck();

	const DC::CornerCheckAnims* GetCornerCheckAnims(const SearchActionPack* pSearchAp, bool leftSide) const;

	const DC::ApEntryItemList* GetCornerCheckEntries(const SearchActionPack* pSearchAp, bool leftSide) const;
	const DC::ApExitAnimList* GetCornerCheckExits(const SearchActionPack* pSearchAp, bool leftSide) const;

	virtual void MakeEntryCharacterState(const ActionPackResolveInput& input,
										 const ActionPack* pActionPack,
										 ApEntry::CharacterState* pCsOut) const override;

private:
	AnimActionWithSelfBlend		m_animAction;
	MovePerformance				m_ledgeChecks;
	MovePerformance				m_grassChecks;

	ActionPackHandle			m_hCorner;
	ActionPackHandle			m_hLatestCorner;
	ActionPackEntryDef			m_entryDef;
	NavLocation					m_goalLoc;
	Point						m_exitPathOriginPs;

	F32							m_cornerCheckExitPhase;
	bool						m_cornerCheckDcNavHandoffParamsValid;
	DC::NavAnimHandoffParams	m_cornerCheckDcNavHandoffParams;

	TimeFrame					m_lastCornerCheckTime;
	TimeFrame					m_lastProneCheckTime;
	MotionType					m_enteredMotionType;

	PathWaypointsEx				m_pathPs;

	RegionHandle				m_hPulloutRegion;
};

/// --------------------------------------------------------------------------------------------------------------- ///
AiSearchController* CreateAiSearchController();
