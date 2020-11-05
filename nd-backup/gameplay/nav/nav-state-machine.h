/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/util/finite-state-machine.h"

#include "gamelib/gameplay/ai/agent/nav-character-adapter.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/waypoint-contract.h"
#include "gamelib/gameplay/nav/action-pack-entry-def.h"
#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-command.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/path-waypoints-ex.h"
#include "gamelib/spline/catmull-rom.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NdAnimationControllers;
class TraversalActionPack;

/// --------------------------------------------------------------------------------------------------------------- ///
class NavStateMachineBase
{
public:
	enum ActionPackUsageState
	{
		kActionPackUsageNone,
		kActionPackUsageReserved,
		kActionPackUsageEntered
	};

	enum ResetType
	{
		kResetTypeReset,
		kResetTypeShutdown
	};

	static const char* GetActionPackUsageStateName(ActionPackUsageState i)
	{
		switch (i)
		{
		case kActionPackUsageNone:		return "Usage None";
		case kActionPackUsageReserved:	return "Usage Reserved";
		case kActionPackUsageEntered:	return "Usage Entered";
		}

		return "<unknown ap usage state>";
	}

	enum WaypointType
	{
		kWaypointAPDefaultEntry,	// waypoint is the default entry point of the next action pack
		kWaypointAPResolvedEntry,	// waypoint is a resolved entry point of next action pack
		kWaypointAPWait,			// waypoint is an arbitrary reachable point near an action pack reserved by someone else
		kWaypointFinalGoal,			// waypoint is the final goal
	};

	static const char* GetWaypointTypeName(WaypointType tt)
	{
		switch (tt)
		{
		case kWaypointAPDefaultEntry:	return "APDefaultEntry";
		case kWaypointAPResolvedEntry:	return "APResolvedEntry";
		case kWaypointAPWait:			return "APWait";
		case kWaypointFinalGoal:		return "FinalGoal";
		}
		return "???";
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackEntryDefSetupDefaults(ActionPackEntryDef* pDef,
									 const ActionPack* pActionPack,
									 const NavCharacterAdapter& pNavChar);

/// --------------------------------------------------------------------------------------------------------------- ///
class NavStateMachine : public NavStateMachineBase
{
protected:
	static CONST_EXPR U32 kInvalidCommandId = 0;

	enum class NavContextStatus
	{
		kInvalid,
		kWaitingForPath,
		kActive,
		kTerminated,
		kFailed,
		kSucceeded
	};

	static const char* GetNavContextStatusName(NavContextStatus i)
	{
		switch (i)
		{
		case NavContextStatus::kInvalid:		return "Invalid";
		case NavContextStatus::kWaitingForPath:	return "Waiting For Path";
		case NavContextStatus::kActive:			return "Active";
		case NavContextStatus::kTerminated:		return "Failed";
		case NavContextStatus::kFailed:			return "Failed";
		case NavContextStatus::kSucceeded:		return "Succeeded";
		}

		return "<unknown status>";
	}

	struct NavCommandContext
	{
		NavCommandContext() { Reset(); }

		bool IsValid() const
		{
			return (m_status != NavContextStatus::kInvalid) && (m_commandId != kInvalidCommandId) && m_command.IsValid();
		}
		void Reset();
		void DebugPrint(const NavCharacter* pNavChar, DoutBase* pDout, const char* preamble = "") const;

		bool IsInProgress() const
		{
			switch (m_status)
			{
			case NavContextStatus::kWaitingForPath:
			case NavContextStatus::kActive:
				return true;
			}

			return false;
		}
		bool IsCommandStopAndStand() const { return m_command.GetType() == NavCommand::kStopAndStand; }

		void EnterNewParentSpace(const Transform& matOldToNew,
								 const Locator& oldParentSpace,
								 const Locator& newParentSpace);

		void GetEffectiveMoveArgs(NavMoveArgs& moveArgs) const;

		NavCommand	m_command;
		U32			m_commandId;

		NavContextStatus m_status;

		ActionPackHandle	m_hNextActionPack; // the next AP that we want to move to (could be a TAP)
		ActionPackEntryDef	m_actionPackEntry;

		// We have this because nav locations store only in their nav's parent space, not necessarily the parent space
		// the npc is currently using. These are guaranteed to be in *my* parent space.
		Point m_nextNavPosPs;
		Point m_finalNavPosPs;

		NavLocation			m_nextNavGoal;
		WaypointType		m_nextWaypointType;
		NavGoalReachedType	m_nextNavReachedType;

		NavLocation			m_finalNavGoal;

		CatmullRom::Handle	m_hPathSpline;
		float				m_curPathSplineArcStart;
		float				m_curPathSplineArcGoal;
		float				m_finalPathSplineArc;
		float				m_pathSplineArcStep;
		float				m_splineAdvanceStartTolerance;
		bool				m_beginSplineAdvancement;
	};

	class BaseState : public Fsm::TState<NavStateMachine>
	{
	public:
		virtual void OnEnter() override;
		virtual void Update() = 0;
		virtual void PostRootLocatorUpdate() {}
		virtual bool CanProcessCommands() const = 0;
		virtual void OnApReservationSwapped() {}
		virtual void DebugDraw() const {}

		void LogHdr(const char* str, ...) const
		{
			STRIP_IN_FINAL_BUILD;

			char strBuffer[2048];
			va_list args;
			va_start(args, str);
			vsnprintf(strBuffer, 2048, str, args);
			va_end(args);
			GetSelf()->LogHdr(strBuffer);
		}

		void LogHdrDetails(const char* str, ...) const
		{
			STRIP_IN_FINAL_BUILD;

			char strBuffer[2048];
			va_list args;
			va_start(args, str);
			vsnprintf(strBuffer, 2048, str, args);
			va_end(args);
			GetSelf()->LogHdrDetails(strBuffer);
		}

		void LogMsg(const char* str, ...) const
		{
			STRIP_IN_FINAL_BUILD;

			char strBuffer[2048];
			va_list args;
			va_start(args, str);
			vsnprintf(strBuffer, 2048, str, args);
			va_end(args);
			GetSelf()->LogMsg(strBuffer);
		}

		void LogMsgDetails(const char* str, ...) const
		{
			STRIP_IN_FINAL_BUILD;

			char strBuffer[2048];
			va_list args;
			va_start(args, str);
			vsnprintf(strBuffer, 2048, str, args);
			va_end(args);
			GetSelf()->LogMsgDetails(strBuffer);
		}


		void LogPoint(Point_arg pt) const									{ GetSelf()->LogPoint(pt);					}
		void LogActionPack(const ActionPack* pAp) const						{ GetSelf()->LogActionPack(pAp);			}
		void LogActionPackDetails(const ActionPack* pAp) const				{ GetSelf()->LogActionPackDetails(pAp);		}
		void LogLocator(const Locator& loc) const							{ GetSelf()->LogLocator(loc);				}
		void LogNavLocation(const NavLocation& navLoc) const				{ GetSelf()->LogNavLocation(navLoc);		}
		void LogNavLocationDetails(const NavLocation& navLoc) const			{ GetSelf()->LogNavLocationDetails(navLoc);	}

		void SetNextWaypoint(NavCommandContext& cmdContext,
							 const NavLocation& newLoc,
							 WaypointType newType,
							 NavGoalReachedType reachedType,
							 const char* strFuncName)
		{
			GetSelf()->SetNextWaypoint(cmdContext, newLoc, newType, reachedType, strFuncName);
		}

		void SetNextWaypoint(NavCommandContext& cmdContext, const NavLocation& newLoc, const char* strFuncName)
		{
			GetSelf()->SetNextWaypoint(cmdContext, newLoc, strFuncName);
		}

		const WaypointContract GetWaypointContract(const NavCommandContext& context) const	{ return GetSelf()->GetWaypointContract(context);}
		bool IsNextWaypointTypeStop() const									{ return GetSelf()->IsNextWaypointTypeStop(); }
		bool ReserveActionPack()											{ return GetSelf()->ReserveActionPack(); }
		void ReleaseActionPack()											{ GetSelf()->ReleaseActionPack();					}
		void OnExitActionPack()												{ GetSelf()->OnExitActionPack();					}

		bool IsActionPackUsageEntered() const								{ return GetSelf()->IsActionPackUsageEntered(); }

		virtual bool IsEnteringActionPack() const							{ return false; }
		virtual bool IsActionPackEntryComplete() const						{ return false; }

		void BeginEnterActionPack(ActionPack* pActionPack, const char* strFuncName) { GetSelf()->BeginEnterActionPack(pActionPack, strFuncName); }
		void StopPathFailed(NavCommandContext& cmdContext, const char* msg)	{ GetSelf()->StopPathFailed(cmdContext, msg);		}
		bool IsCommandInProgress() const									{ return GetSelf()->IsCommandInProgress();			}
		bool IsCommandPending() const										{ return GetSelf()->IsCommandPending();				}
		const ActionPack* GetGoalOrPendingActionPack() const				{ return GetSelf()->GetGoalOrPendingActionPack();	}
		const ActionPack* GetGoalActionPack() const							{ return GetSelf()->GetGoalActionPack();			}
		float GetGoalRadius() const											{ return GetSelf()->GetGoalRadius();				}
		const Point GetFinalDestPointPs() const								{ return GetSelf()->GetFinalDestPointPs();			}
		const ActionPack* GetActionPack() const								{ return GetSelf()->GetActionPack();				}
		ActionPack* GetActionPack()											{ return GetSelf()->GetActionPack();				}
		const TraversalActionPack* GetTraversalActionPack() const			{ return GetSelf()->GetTraversalActionPack();		}
		ActionPackUsageState GetActionPackUsageState() const				{ return GetSelf()->GetActionPackUsageState();		}
		ActionPack::Type GetActionPackUsageType() const						{ return GetSelf()->GetActionPackUsageType();		}
		NdAnimationControllers* GetAnimationControllers() const				{ return GetSelf()->m_pAnimationControllers;		}
		bool IsDynamicallyClear(NavLocation loc) const						{ return GetSelf()->IsDynamicallyClear(loc); }

		bool SetCurrentActionPack(const ActionPack* pAp, const char* strFuncName) { return GetSelf()->SetCurrentActionPack(pAp, strFuncName); }
		bool SetNextActionPack(NavCommandContext& cmdContext, const ActionPack* pNextAp, const char* strFuncName)
		{
			return GetSelf()->SetNextActionPack(cmdContext, pNextAp, strFuncName);
		}

		void SetContextStatus(NavCommandContext& cmdContext, NavContextStatus status, const char* msg)
		{
			return GetSelf()->SetContextStatus(cmdContext, status, msg);
		}

		virtual bool GetPathFindStartLocation(NavLocation* pStartLocOut,
											  ActionPackHandle* phPreferredTapOut,
											  float* pTapBiasOut) const;
	};

	typedef Fsm::TStateMachine<BaseState> StateMachine;

	FSM_BASE_DECLARE(StateMachine);

public:

	FSM_STATE_DECLARE(Stopped);
	FSM_STATE_DECLARE(MovingAlongPath);
	FSM_STATE_DECLARE(MovingBlind);
	FSM_STATE_DECLARE(SteeringAlongPath);
	FSM_STATE_DECLARE(Stopping);
	FSM_STATE_DECLARE(UsingTraversalActionPack);
	FSM_STATE_DECLARE(EnteringActionPack);
	FSM_STATE_DECLARE(UsingActionPack);
	FSM_STATE_DECLARE(StartingToExitActionPack);
	FSM_STATE_DECLARE_ARG(ExitingActionPack, bool);
	FSM_STATE_DECLARE(Interrupted);
	FSM_STATE_DECLARE(Resuming);

	U32F GetMaxStateAllocSize() { return sizeof(BaseState) + 128; }
	void GoInitialState() { GoStopped(); }
	TimeFrame GetStateTimePassed() const
	{
		if (const BaseState* pState = GetStateMachine().GetState())
		{
			return pState->GetStateTimePassed();
		}

		return Seconds(0.0f);
	}

	static const char* GetActionPackUsageStateName(ActionPackUsageState i)
	{
		switch (i)
		{
		case kActionPackUsageNone:		return "Usage None";
		case kActionPackUsageReserved:	return "Usage Reserved";
		case kActionPackUsageEntered:	return "Usage Entered";
		}

		return "<unknown ap usage state>";
	}

	NavStateMachine();

	void Init(NdAnimationControllers* pAnimationControllers, NavControl* pNavControl, NavCharacter* pNavCharacter);
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	U32 AddCommand(const NavCommand& request);
	bool IsCommandPending() const				{ return GetPendingCommand().GetType() != NavCommand::kNone; }
	bool IsCommandInProgress() const			{ return GetStatus() == NavCommand::kStatusCommandInProgress; }
	bool CanProcessCommands() const;
	bool CanProcessMoveCommands() const { return m_pNavChar && m_pNavChar->CanProcessNewMoveCommands(); }

	const NavCommand& GetMostRecentCommand() const
	{
		if (m_queuedCommand.IsValid())
			return m_queuedCommand;

		if (m_nextContext.IsValid())
			return m_nextContext.m_command;

		return m_activeContext.m_command;
	}

	bool IsCommandStopAndStand() const { return GetMostRecentCommand().GetType() == NavCommand::kStopAndStand; }

	bool GetMoveAlongSplineCurrentArcLen(const StringId64 splineNameId, F32& outArcLen) const;

	void GatherPathFindResults();

	void Update();
	void PostRootLocatorUpdate();

	void EnterNewParentSpace(const Transform& matOldToNew,
							 const Locator& oldParentSpace,
							 const Locator& newParentSpace);

	void Shutdown();	// release all global resources like action pack reservations, etc
	void Reset(bool playIdle = true);
	void Interrupt(const char* sourceFile, U32F sourceLine, const char* sourceFunc);
	void Resume();
	void AbandonInterruptedCommand();

	void DebugDraw() const;
	void DebugDumpToStream(DoutBase* pDout) const;

	const ActionPack* GetGoalOrPendingActionPack() const;
	uintptr_t GetGoalOrPendingApUserData() const;
	const ActionPack* GetGoalActionPack() const;
	float GetGoalRadius() const									{ return m_activeContext.m_command.GetGoalRadius(); }
	Point GetNextDestPointPs() const;
	Point GetFinalDestPointPs() const;
	bool GetCanStrafe() const;
	Vector GetGoalFaceDirPs() const;
	const NavLocation& GetFinalNavGoal() const					{ return m_activeContext.m_finalNavGoal; }
	const ActionPack* GetActionPack() const						{ return m_hCurrentActionPack.ToActionPack(); }
		  ActionPack* GetActionPack()							{ return m_hCurrentActionPack.ToActionPack(); }
	const ActionPack* GetPathActionPack() const					{ return m_pathStatus.m_hTraversalActionPack.ToActionPack(); }
	ActionPackUsageState GetActionPackUsageState() const		{ return m_actionPackUsageState; }
	ActionPack::Type GetActionPackUsageType() const				{ return m_actionPackUsageType; }

	const TraversalActionPack* GetTraversalActionPack() const;
	TraversalActionPack* GetTraversalActionPack();

	void SwitchActionPack(ActionPack* pNewActionPack);

	const WaypointContract GetProcessedWaypointContract() const { return GetWaypointContract(m_activeContext); }
	NavGoalReachedType GetGoalReachedType() const	{ return m_activeContext.m_nextNavReachedType; }
	NavGoalReachedType GetPendingGoalReachedType() const	{ NAV_ASSERT(IsCommandPending()); return GetPendingCommand().GetGoalReachedType(); }
	const ActionPackEntryDef* GetActionPackEntryDef() const	{ return &m_activeContext.m_actionPackEntry; }

	StringId64 GetPendingMtSubcategory() const				{ NAV_ASSERT(IsCommandPending()); return GetPendingCommand().GetMtSubcategory(); }
	StringId64 GetMtSubcategory() const						{ return m_activeContext.m_command.GetMtSubcategory(); }

	bool IsNormalMovementSuppressed() const;
	bool IsMovementStalled() const;
	bool IsFirstPathForCommandPending() const;
	bool IsWaitingForActionPack() const;
	bool IsWaitingForPath() const;
	const NavCharacter* GetApBlockingChar() const;

	bool IsActionPackUsageEntered() const { return m_actionPackUsageState == kActionPackUsageEntered; }

	bool IsActionPackEntryComplete() const
	{
		bool complete = false;
		if (const BaseState* pState = GetState())
		{
			complete = pState->IsActionPackEntryComplete();
		}
		return complete;
	}

	bool IsEnteringActionPack() const
	{
		bool complete = false;
		if (const BaseState* pState = GetState())
		{
			complete = pState->IsEnteringActionPack();
		}
		return complete;
	}

	bool IsExitingActionPack() const
	{
		return IsState(kStateStartingToExitActionPack) || IsState(kStateExitingActionPack);
	}

	bool IsInterrupted() const { return IsState(kStateInterrupted); }
	bool IsNextWaypointTypeStop() const { return m_activeContext.m_nextNavReachedType == kNavGoalReachedTypeStop; }
	bool IsStopped() const						{ return IsState(kStateStopped); }
	bool IsMovingToWaypoint() const				{ return IsState(kStateMovingAlongPath); }

	ActionPack::Type GetCurrentActionPackType() const { return IsActionPackEntryComplete() ? GetActionPackUsageType() : ActionPack::kInvalidActionPack; }

	bool IsPlayingWorldRelativeAnim() const		{ return IsState(kStateUsingTraversalActionPack) ||
														 IsState(kStateUsingActionPack) ||
														 IsState(kStateEnteringActionPack) ||
														 IsState(kStateStartingToExitActionPack) ||
														 IsState(kStateExitingActionPack); }

	bool IsNextWaypointActionPackEntry(const NavCommandContext* pCmdContext = nullptr) const
	{
		const NavCommandContext& cmdContext = pCmdContext ? *pCmdContext : m_activeContext;
		return (cmdContext.m_nextWaypointType == kWaypointAPDefaultEntry)
			   || (cmdContext.m_nextWaypointType == kWaypointAPResolvedEntry);
	}

	bool IsNextWaypointResolvedEntry(const NavCommandContext* pCmdContext = nullptr) const
	{
		const NavCommandContext& cmdContext = pCmdContext ? *pCmdContext : m_activeContext;
		return cmdContext.m_nextWaypointType == kWaypointAPResolvedEntry;
	}

	TimeFrame GetStallTimeElapsed() const;
	bool IsUsingBadTraversalActionPack() const
	{
		return (GetCurrentActionPackType() == ActionPack::kTraversalActionPack) && IsNormalMovementSuppressed();
	}

	// Outputs from the FSM
	NavCommand::Status GetStatus() const;
	U32 GetCommandId() const							{ return m_nextCommandId; }
	const Vector GetPathDirPs() const					{ return m_currentPathDirPs; }

	bool IsPathValid() const							{ return m_currentPathPs.IsValid(); }
	TimeFrame GetLastPathFoundTime() const				{ return m_pathStatus.m_lastPathFoundTime; }
	const PathWaypointsEx* GetCurrentPathPs() const		{ return m_currentPathPs.IsValid() ? &m_currentPathPs : nullptr; }
	const PathWaypointsEx* GetPostTapPathPs() const		{ return m_pathStatus.m_postTapPathPs.IsValid() ? &m_pathStatus.m_postTapPathPs : nullptr; }
	const PathWaypointsEx* GetLastFoundPathPs() const	{ return m_pathStatus.m_pathPs.IsValid() ? &m_pathStatus.m_pathPs : nullptr; }
	const Locator GetPathTapAnimAdjustLs() const		{ return m_pathStatus.m_tapAnimAdjustLs; }

	bool IsPathFound() const							{ return m_pathStatus.m_pathFound; }
	float GetCurrentPathLength() const					{ return m_currentPathPs.ComputePathLength(); }
	bool PathStatusSuspended() const					{ return m_pathStatus.m_suspended; }
	void NotifyApReservationLost(const ActionPack* pAp);

	const Clock* GetClock() const { return m_pNavChar->GetClock(); }

	void CheckForApReservationSwap();

	static bool NsmPathFindBlockerFilter(const SimpleNavControl* pNavControl,
										 const DynamicNavBlocker* pBlocker,
										 BoxedValue userData,
										 bool forPathing,
										 bool debug);

	bool GetPathFindStartLocation(NavLocation* pStartLocOut,
								  ActionPackHandle* phPreferredTapOut,
								  float* pTapBiasOut) const;

	const NavCommand& GetPendingCommand() const
	{
		if (m_queuedCommand.IsValid())
		{
			return m_queuedCommand;
		}

		if (m_nextContext.IsValid())
		{
			return m_nextContext.m_command;
		}

		return m_queuedCommand;
	}

	void PatchPathStartPs(Point_arg newStartPs);
	bool StopAtPathEndPs(const IPathWaypoints* pPathPs) const;

	bool IsFollowingPath() const
	{
		return m_activeContext.IsInProgress() && m_activeContext.m_command.PathFindRequired();
	}

private:
	NavCommand& GetPendingCommand()
	{
		if (m_queuedCommand.IsValid())
		{
			return m_queuedCommand;
		}

		if (m_nextContext.IsValid())
		{
			return m_nextContext.m_command;
		}

		return m_queuedCommand;
	}

	void InternalReset(const ResetType resetType);

	NavCommandContext* GetCommandContext(U32F commandId);
	const NavCommandContext* GetCommandContext(U32F commandId) const;

	void ProcessCommand(NavCommand& cmd);
	bool ProcessCommand_StopAndStand(NavCommandContext& cmdContext);
	bool ProcessCommand_MoveTo(NavCommandContext& cmdContext);
	bool ProcessCommand_MoveInDirection(NavCommandContext& cmdContext);

	void SetContextStatus(NavCommandContext& cmdContext, NavContextStatus status, const char* msg);

	bool CheckActiveCommandStillValid(NavCommandContext& cmdContext);
	bool TryFailCommandMoveTo(const NavCommand& cmd);

	void StopMovingImmediately(bool pushAnim, bool changeActiveCmd, const char* pStrMsg);
	void StopPathFailed(NavCommandContext& cmdContext, const char* msg);

	void BeginPathFind();
	void SetPathFindResults(const NavControl::PathFindResults& results);
	void ProcessPathFindResults();
	void HandleFirstPathForCommand();
	bool DetectRepathOrPathFailure();

	bool SetCurrentActionPack(const ActionPack* pAp, const char* strFuncName);
	bool SetNextActionPack(NavCommandContext& cmdContext, const ActionPack* pNextAp, const char* strFuncName);

	bool ReserveActionPack();
	void BeginEnterActionPack(ActionPack* pActionPack, const char* strFuncName);
	void OnEnterActionPack(const ActionPack* pActionPack);
	void ReleaseActionPack();
	bool TryEarlyCompleteMoveCommand(NavCommandContext& cmdContext);
	void ExitActionPack();
	void OnExitActionPack();

	void EnterTraversalActionPack();
	void ExitTraversalActionPack();

	void UpdateSplineAdvancement();

	void UpdatePathDirectionPs();
	void ResumeMoving(bool needNewPath, bool ignorePendingCommand);
	void ClearPath();
	bool IsDynamicallyClear(NavLocation loc) const;

	bool ShouldSwapApReservations(const ActionPack* pAp) const;
	void SwapApReservations(const ActionPack* pAp);

	void SetFinalNavLocation(NavCommandContext& cmdContext, const NavLocation& newLoc, const char* strFuncName);
	void SetNextWaypoint(NavCommandContext& cmdContext,
						 const NavLocation& newLoc,
						 WaypointType newType,
						 NavGoalReachedType reachedType,
						 const char* strFuncName);

	void SetNextWaypoint(NavCommandContext& cmdContext, const NavLocation& newLoc, const char* strFuncName)
	{
		SetNextWaypoint(cmdContext, newLoc, cmdContext.m_nextWaypointType, cmdContext.m_nextNavReachedType, strFuncName);
	}

	const WaypointContract GetWaypointContract(const NavCommandContext& context) const;
	bool NextWaypointIsForAp(const NavCommandContext& cmdContext) const;

	void PatchPathEnd(const NavCommandContext& cmdContext, const NavLocation endLoc, const char* strFuncName);

	float GetPathFindRadiusForCommand(const NavCommandContext& cmdContext) const;

	void LogHdr(const char* str, ...) const;
	void LogHdrDetails(const char* str, ...) const;
	void LogMsg(const char* str, ...) const;
	void LogMsgDetails(const char* str, ...) const;
	void LogPoint(Point_arg pt) const;
	void LogVector(Vector_arg v) const;
	void LogActionPack(const ActionPack* pAp) const;
	void LogActionPackDetails(const ActionPack* pAp) const;
	void LogLocator(const Locator& loc) const;
	void LogNavLocation(const NavLocation& navLoc) const;
	void LogNavLocationDetails(const NavLocation& navLoc) const;

	const char* GetStateDesc() const;

	bool ShouldSuspendPathFind() const;
	void UpdateCurrentPath(const PathWaypointsEx& srcPath, PathWaypointsEx* pPathPs, Vector* pDirPs) const;
	bool TryProcessResume();

	ActionPackResolveInput MakeApResolveInput(const NavCommand& navCmd, const ActionPack* pActionPack) const;
	void ValidateActionPackUsage() const;
	void WaitForActionPack(NavCommandContext& cmdContext, const ActionPack* pActionPack, const char* logStr);

	struct PathStatus
	{
		PathWaypointsEx		m_pathPs;
		PathWaypointsEx		m_postTapPathPs;

		Locator				m_tapAnimAdjustLs = Locator(kIdentity);
		ActionPackHandle	m_hTraversalActionPack = nullptr;
		ActionPackHandle	m_hLastAdjTap = nullptr;

		TimeFrame			m_lastPathIssuedTime = TimeFrameNegInfinity();
		TimeFrame			m_lastPathFoundTime = TimeFrameNegInfinity();

		U32					m_kickedCommandId = kInvalidCommandId;
		U32					m_processedCommandId = kInvalidCommandId;

		bool				m_pathUpdated = false;
		bool				m_pathFound = false;
		bool				m_suspended = false;
		bool				m_ignoreInFlightResults = false;
	};

	NdAtomicLock m_pathResultsLock;

	NavCommandContext m_activeContext;
	NavCommandContext m_nextContext;

	NavCommand	m_queuedCommand;
	U32			m_nextCommandId;

	NdAnimationControllers*	m_pAnimationControllers;
	NavCharacter*			m_pNavChar;

	const Fsm::StateDescriptor*	m_pInterruptedState;
	const Fsm::StateDescriptor*	m_pPrevStates[5];

	ActionPackHandle		m_hCurrentActionPack; // AP that we've either reserved or are actively using
	ActionPackUsageState	m_actionPackUsageState;
	ActionPack::Type		m_actionPackUsageType;

	NavControl::PathFindJobHandle m_hNavPathJob;

	bool			m_shouldUpdatePath;
	bool			m_resumeStateRequested;
	bool			m_abandonInterruptedCommand;

	PathStatus		m_pathStatus;

	PathWaypointsEx	m_currentPathPs;
	Vector			m_currentPathDirPs;

	const char*		m_interruptSourceFile;
	U32				m_interruptSourceLine;
	const char*		m_interruptSourceFunc;
};
