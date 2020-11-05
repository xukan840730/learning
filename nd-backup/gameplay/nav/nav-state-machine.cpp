/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-state-machine.h"

#include "ndlib/anim/anim-actor.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/util/bitarray128.h"

#include "gamelib/gameplay/ai/agent/nav-character-adapter.inl"
#include "gamelib/gameplay/ai/agent/nav-character-util.h"
#include "gamelib/gameplay/ai/controller/nd-traversal-controller.h"
#include "gamelib/gameplay/ai/controller/nd-weapon-controller.h"
#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nav/nav-job-scheduler.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/scriptx/h/npc-demeanor-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
// magic constants
const float kApResolveDistMax = 16.0f; // max path distance at which we begin resolving action pack entry anims
const float kApReserveDist	  = 30.0f; // path distance at which we begin reserving action packs (except TAPs)

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackEntryDefSetupDefaults(ActionPackEntryDef* pDef,
									 const ActionPack* pActionPack,
									 const NavCharacterAdapter& pNavChar)
{
	*pDef = ActionPackEntryDef();

	pDef->m_apReference = pActionPack->GetBoundFrame();

	const Scalar apRegPtDist = pNavChar->GetNavControl()->GetActionPackEntryDistance();
	const Point entryPointWs = pActionPack->GetDefaultEntryPointWs(apRegPtDist);

	// save entry locator in Npcs parent space
	Locator locWs = pActionPack->GetLocatorWs();
	locWs.SetTranslation(entryPointWs);

	pDef->m_hResolvedForAp = ActionPackHandle(); // leave this invalid so we don't think we've successfully resolved 
	pDef->m_stopBeforeEntry = true;
	pDef->m_entryNavLoc		= pActionPack->GetRegisteredNavLocation();
	pDef->m_entryNavLoc.UpdatePosPs(pDef->m_apReference.GetParentSpace().UntransformPoint(entryPointWs));
	pDef->m_entryRotPs = pNavChar.ToGameObject()->GetParentSpace().UntransformLocator(locWs).Rot();

	pDef->m_mtSubcategoryId = pNavChar->GetCurrentMtSubcategory();
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline bool DistanceWithinApResolveRange(const float dist)
{
	return dist <= kApResolveDistMax;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool LocomotionReadyRequirementsMet(const NavCharacter* pNavChar, const DC::AnimState* pDcState)
{
	STRIP_IN_FINAL_BUILD_VALUE(true);

	const AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;

	if (!pDcState || !pAnimControl)
		return false;

	TransitionQueryInfo qi;
	qi.m_pOverlaySnapshot = pAnimControl->GetAnimOverlaySnapshot();
	qi.m_pAnimTable		  = &pAnimControl->GetAnimTable();
	qi.m_pInfoCollection  = pAnimControl->GetInfoCollection();
	qi.m_isTopIntance	  = true;

	bool valid = true;

	static StringId64 s_requiredTransitions[] =
	{
		SID("idle"),
		SID("move"),
		SID("step"),
		SID("turn-in-place"),
		SID("demeanor-change"),
	};

	for (U32F i = 0; i < ARRAY_COUNT(s_requiredTransitions); ++i)
	{
		const StringId64 transitionId = s_requiredTransitions[i];

		if (!AnimStateTransitionExists(pDcState, transitionId, qi))
		{
			MsgErr("anim-state '%s' says it's locomotion ready but has no '%s' transition!\n",
				   pDcState->m_name.m_string.GetString(),
				   DevKitOnly_StringIdToString(transitionId));
			valid = false;
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Locator ComputeTapPathAnimAdjustLs(const NavCharacter* pNavChar,
										  const TraversalActionPack* pTap,
										  const PathWaypointsEx& pathPs,
										  const PathWaypointsEx& postTapPathPs)
{
	if (!pTap || !pNavChar)
		return kIdentity;
	if (!pathPs.IsValid() || !postTapPathPs.IsValid())
		return kIdentity;

	const Locator& parentSpace = pNavChar->GetParentSpace();

	const Point pathEndPs		= pathPs.GetEndWaypoint();
	const Point nextPathStartPs = postTapPathPs.GetWaypoint(0);

	const Point pathEndWs		= parentSpace.TransformPoint(pathEndPs);
	const Point nextPathStartWs = parentSpace.TransformPoint(nextPathStartPs);

	const Locator animAdjustLs = pTap->GetApAnimAdjustLs(pathEndWs, nextPathStartWs);

	return animAdjustLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::NavCommandContext::Reset()
{
	m_status	= NavContextStatus::kInvalid;
	m_commandId = 0;
	m_command	= NavCommand();

	m_hNextActionPack = nullptr;
	m_actionPackEntry = ActionPackEntryDef();

	m_nextNavGoal		 = NavLocation();
	m_nextWaypointType	 = kWaypointFinalGoal;
	m_nextNavReachedType = kNavGoalReachedTypeStop;

	m_finalNavGoal = NavLocation();

	m_hPathSpline = nullptr;
	m_curPathSplineArcStart = 0.0f;
	m_curPathSplineArcGoal	= 0.0f;
	m_finalPathSplineArc	= 0.0f;
	m_pathSplineArcStep		= 0.0f;
	m_splineAdvanceStartTolerance = -1.0f;
	m_beginSplineAdvancement	  = false;

	m_nextNavPosPs = m_finalNavPosPs = kInvalidPoint;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::NavCommandContext::EnterNewParentSpace(const Transform& matOldToNew,
															 const Locator& oldParentSpace,
															 const Locator& newParentSpace)
{
	m_nextNavPosPs	= m_nextNavPosPs * matOldToNew;
	m_finalNavPosPs = m_finalNavPosPs * matOldToNew;

	m_actionPackEntry.m_entryVelocityPs = m_actionPackEntry.m_entryVelocityPs * matOldToNew;

	const Quat rotTransform		   = Locator(matOldToNew).Rot();
	m_actionPackEntry.m_entryRotPs = rotTransform * m_actionPackEntry.m_entryRotPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::NavCommandContext::GetEffectiveMoveArgs(NavMoveArgs& moveArgs) const
{
	moveArgs = m_command.GetMoveArgs();

	moveArgs.m_motionType = m_command.GetMotionType();
	moveArgs.m_goalRadius = m_command.GetGoalRadius();

	if (m_nextWaypointType == kWaypointAPDefaultEntry)
	{
		moveArgs.m_goalRadius = Min(0.1f, moveArgs.m_goalRadius);
	}

	switch (m_nextWaypointType)
	{
	case kWaypointAPDefaultEntry:
	case kWaypointAPResolvedEntry:
		moveArgs.m_goalFaceDirValid = true;
		moveArgs.m_goalFaceDirPs = GetLocalZ(m_actionPackEntry.m_entryRotPs);
		moveArgs.m_recklessStopping = m_actionPackEntry.m_recklessStopping;
		if (m_actionPackEntry.m_alwaysStrafe)
			moveArgs.m_strafeMode = DC::kNpcStrafeAlways;
		if (m_actionPackEntry.m_slowInsteadOfAutoStop)
			moveArgs.m_slowInsteadOfAutoStop = true;
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavStateMachine::NavStateMachine()
	: m_nextCommandId(kInvalidCommandId)
	, m_pAnimationControllers(nullptr)
	, m_pNavChar(nullptr)
	, m_pInterruptedState(nullptr)
	, m_actionPackUsageState(kActionPackUsageNone)
	, m_actionPackUsageType(ActionPack::kInvalidActionPack)
	, m_shouldUpdatePath(false)
	, m_resumeStateRequested(false)
	, m_abandonInterruptedCommand(false)
	, m_currentPathDirPs(kUnitZAxis)
{
	for (U32F i = 0; i < 5; ++i)
	{
		m_pPrevStates[i] = nullptr;
	}

	m_interruptSourceFile = "<invalid-file>";
	m_interruptSourceLine = -1;
	m_interruptSourceFunc = "<invalid-func>";

	m_activeContext.Reset();
	m_nextContext.Reset();
	m_queuedCommand.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Init(NdAnimationControllers* pAnimationControllers,
						   NavControl* pNavControl,
						   NavCharacter* pNavCharacter)
{
	m_pAnimationControllers = pAnimationControllers;
	m_pNavChar = pNavCharacter;

	m_currentPathDirPs = GetLocalZ(pNavCharacter->GetRotationPs());

	m_hCurrentActionPack = nullptr;

	m_pathStatus = PathStatus();

	GetStateMachine().Init(this, FILE_LINE_FUNC, true, sizeof(bool));

	StopMovingImmediately(false, true, "Init");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pAnimationControllers, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pNavChar, deltaPos, lowerBound, upperBound);

	GetStateMachine().Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::LogMsg(const char* str, ...) const
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	char buf[1024];
	vsnprintf(buf, 1024, str, vargs);
	va_end(vargs);
	::LogMsgAppend(m_pNavChar, kNpcLogChannelNav, buf);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::LogMsgDetails(const char* str, ...) const
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	char buf[1024];
	vsnprintf(buf, 1024, str, vargs);
	va_end(vargs);
	::LogMsgAppend(m_pNavChar, kNpcLogChannelNavDetails, buf);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::LogHdr(const char* str, ...) const
{
	STRIP_IN_FINAL_BUILD;

	if (!m_pNavChar)
		return;

	::LogHdr(m_pNavChar, kNpcLogChannelNav);
	::LogMsg(m_pNavChar,
			 kNpcLogChannelNav,
			 "nav-%d ",
			 (m_activeContext.m_commandId != kInvalidCommandId) ? m_activeContext.m_commandId : m_nextCommandId);
	va_list vargs;
	va_start(vargs, str);
	char buf[1024];
	vsnprintf(buf, 1024, str, vargs);
	va_end(vargs);
	::LogMsgAppend(m_pNavChar, kNpcLogChannelNav, buf);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::LogHdrDetails(const char* str, ...) const
{
	STRIP_IN_FINAL_BUILD;

	if (!m_pNavChar)
		return;

	::LogHdr(m_pNavChar, kNpcLogChannelNavDetails);
	::LogMsg(m_pNavChar,
			 kNpcLogChannelNavDetails,
			 "nav-%d ",
			 (m_activeContext.m_commandId != kInvalidCommandId) ? m_activeContext.m_commandId : m_nextCommandId);
	va_list vargs;
	va_start(vargs, str);
	char buf[1024];
	vsnprintf(buf, 1024, str, vargs);
	va_end(vargs);
	::LogMsgAppend(m_pNavChar, kNpcLogChannelNavDetails, buf);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::LogPoint(Point_arg pt) const
{
	STRIP_IN_FINAL_BUILD;

	::LogPoint(m_pNavChar, kNpcLogChannelNav, pt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::LogVector(Vector_arg v) const
{
	STRIP_IN_FINAL_BUILD;

	::LogVector(m_pNavChar, kNpcLogChannelNav, v);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::LogActionPack(const ActionPack* pAp) const
{
	STRIP_IN_FINAL_BUILD;

	::LogActionPack(m_pNavChar, kNpcLogChannelNav, pAp);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::LogActionPackDetails(const ActionPack* pAp) const
{
	STRIP_IN_FINAL_BUILD;

	::LogActionPack(m_pNavChar, kNpcLogChannelNavDetails, pAp);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::LogLocator(const Locator& loc) const
{
	STRIP_IN_FINAL_BUILD;

	::LogLocator(m_pNavChar, kNpcLogChannelNav, loc);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::LogNavLocation(const NavLocation& navLoc) const
{
	STRIP_IN_FINAL_BUILD;

	::LogNavLocation(m_pNavChar, kNpcLogChannelNav, navLoc);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::LogNavLocationDetails(const NavLocation& navLoc) const
{
	STRIP_IN_FINAL_BUILD;

	::LogNavLocation(m_pNavChar, kNpcLogChannelNavDetails, navLoc);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* NavStateMachine::GetStateDesc() const
{
	const ActionPack* pActionPack = m_hCurrentActionPack.ToActionPack();

	if (IsState(kStateUsingActionPack) && pActionPack)
	{
		return ActionPack::GetShortTypeName(pActionPack->GetType());
	}

	return GetStateName("<none>");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::SetFinalNavLocation(NavCommandContext& cmdContext,
										  const NavLocation& newLoc,
										  const char* strFuncName)
{
	if (FALSE_IN_FINAL_BUILD(!IsReasonable(newLoc.GetPosPs())))
	{
		LogHdr("SetFinalNavLocation[%s]: Waypoint for command %d is unreasonable!\n",
			   strFuncName,
			   cmdContext.m_commandId);
		MailNpcLogTo(m_pNavChar, "john_bellomy@naughtydog.com", "Unreasonable final waypoint pos!", FILE_LINE_FUNC);
	}
	NAV_ASSERT(IsReasonable(newLoc.GetPosPs()));

	cmdContext.m_finalNavGoal = newLoc;

	if (m_pNavChar)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		const Point posWs = newLoc.GetPosWs();

		const Locator& parentSpace = m_pNavChar->GetParentSpace();
		cmdContext.m_finalNavPosPs = parentSpace.UntransformPoint(posWs);
	}
	else
	{
		cmdContext.m_finalNavPosPs = newLoc.GetPosPs();
	}

	LogHdr("SetFinalNavLocation[%s]: Setting Final Nav Location for command %d to ",
		   strFuncName,
		   cmdContext.m_commandId);
	LogNavLocation(newLoc);
	LogMsg("\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::SetNextWaypoint(NavCommandContext& cmdContext,
									  const NavLocation& newLoc,
									  WaypointType newType,
									  NavGoalReachedType reachedType,
									  const char* strFuncName)
{
	if (FALSE_IN_FINAL_BUILD(!IsReasonable(newLoc.GetPosPs())))
	{
		LogHdr("SetNextWaypoint[%s]: Waypoint for command %d is unreasonable!\n", strFuncName, cmdContext.m_commandId);
		MailNpcLogTo(m_pNavChar, "john_bellomy@naughtydog.com", "Unreasonable Next waypoint pos!", FILE_LINE_FUNC);
	}
	NAV_ASSERT(IsReasonable(newLoc.GetPosPs()));

	const WaypointType prevType = cmdContext.m_nextWaypointType;
	const NavGoalReachedType prevReachedType = cmdContext.m_nextNavReachedType;
	const Point prevPtPs = cmdContext.m_nextNavPosPs;

	cmdContext.m_nextWaypointType	= newType;
	cmdContext.m_nextNavGoal		= newLoc;
	cmdContext.m_nextNavReachedType = reachedType;

	if (m_pNavChar)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		const Point posWs = newLoc.GetPosWs();

		const Locator& parentSpace = m_pNavChar->GetParentSpace();
		cmdContext.m_nextNavPosPs  = parentSpace.UntransformPoint(posWs);
	}
	else
	{
		cmdContext.m_nextNavPosPs = newLoc.GetPosPs();
	}

	const Point newPtPs = cmdContext.m_nextNavPosPs;

	// JDB: if this suddenly starts firing a lot feel free to delete it and lmk
	// NAV_ASSERT((newType != kWaypointAPResolvedEntry) || cmdContext.m_actionPackEntry.m_controllerResolved);

	// if something has changed,
	if (FALSE_IN_FINAL_BUILD((newType != prevType) || Dist(newPtPs, prevPtPs) > 0.01f)
		|| (prevReachedType != reachedType))
	{
		LogHdr("SetNextWaypoint[%s]: Waypoint for command %d changed to type %s-%s (was %s-%s) new loc ",
			   strFuncName,
			   cmdContext.m_commandId,
			   GetWaypointTypeName(newType),
			   NavCommand::GetGoalReachedTypeName(reachedType),
			   GetWaypointTypeName(prevType),
			   NavCommand::GetGoalReachedTypeName(prevReachedType));
		LogNavLocation(newLoc);
		LogMsg("\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::IsWaitingForActionPack() const
{
	return m_activeContext.m_nextWaypointType == kWaypointAPWait;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::IsWaitingForPath() const
{
	return m_nextContext.m_status == NavContextStatus::kWaitingForPath;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavCharacter* NavStateMachine::GetApBlockingChar() const
{
	const ActionPack* pNextAp = m_hCurrentActionPack.ToActionPack();
	if (!pNextAp || pNextAp->GetType() != ActionPack::kCoverActionPack)
		return nullptr;

	const CoverActionPack* pNextCoverAp = (const CoverActionPack*)pNextAp;
	const NavCharacter* pBlockingChar	= pNextCoverAp->GetBlockingNavChar();

	if (pBlockingChar == m_pNavChar)
		return nullptr;

	return pBlockingChar;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::IsNormalMovementSuppressed() const
{
	INdAiTraversalController* pTraversalController = m_pAnimationControllers->GetTraversalController();
	return pTraversalController && pTraversalController->IsNormalMovementSuppressed();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::IsMovementStalled() const
{
	return GetStallTimeElapsed() > Seconds(0.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::IsFirstPathForCommandPending() const
{
	const NavCommandContext* pKickedContext = GetCommandContext(m_pathStatus.m_kickedCommandId);
	if (!pKickedContext || (pKickedContext->m_status != NavContextStatus::kWaitingForPath))
	{
		return false;
	}

	if (m_pathStatus.m_kickedCommandId == m_pathStatus.m_processedCommandId)
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 NavStateMachine::AddCommand(const NavCommand& command)
{
	// decide if we should keep this command or not
	bool killCommand = false;
	const NavCommand::Type cmdType = command.GetType();
	const bool isCmdMoveToAp	   = cmdType == NavCommand::kMoveToActionPack;

	const NavCommand::Status curStatus = GetStatus();
	const bool commandPending = IsCommandPending();

	switch (cmdType)
	{
	case NavCommand::kStopAndStand:
		{
			const INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController();

			if (pNavAnimController && pNavAnimController->IsInterrupted())
			{
			}
			else if (cmdType == m_activeContext.m_command.GetType())
			{
				// if previous command was a StopAndStand,
				// and if previous command is in progress or succeeded,
				switch (curStatus)
				{
				case NavCommand::kStatusCommandInProgress:
				case NavCommand::kStatusCommandSucceeded:
					{
						killCommand = command.HasSameGoalFaceDir(m_activeContext.m_command);
					}
					break;
				}
			}
			else if (IsState(kStateStopped) && !commandPending && (curStatus != NavCommand::kStatusCommandInProgress))
			{
				// Note: we may be stopped waiting to use an AP! So don't kill the command in that case
				const NavCommand& compareCommand = m_activeContext.m_command;

				if (compareCommand.IsValid())
				{
					killCommand = command.HasSameGoalFaceDir(compareCommand);
				}
				else
				{
					killCommand = true;
				}
			}
		}
		break;

	case NavCommand::kMoveToLocation:
	case NavCommand::kMoveToActionPack:
		// don't run kill check if we don't have a pending command and our last command failed
		if (commandPending
			|| (curStatus != NavCommand::kStatusCommandFailed && curStatus != NavCommand::kStatusAwaitingCommands))
		{
			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

			// if no command pending, and prev command is in progress and if this move command is identical to previous command
			const MotionConfig& motionConfig = m_pNavChar->GetMotionConfig();
			const Locator& parentSpace		 = m_pNavChar->GetParentSpace();
			const Point finalGoalPs = GetFinalDestPointPs();

			const NavCommand& compareCommand = commandPending ? GetPendingCommand() : m_activeContext.m_command;
			const bool sameType = cmdType == compareCommand.GetType();

			const Point newFinalGoalPs = isCmdMoveToAp
											 ? kOrigin
											 : parentSpace.UntransformPoint(command.GetGoalLocation().GetPosWs());
			NAV_ASSERT(IsReasonable(newFinalGoalPs));

			const bool withinMoveDistXZ = isCmdMoveToAp ? true
														: (DistXz(finalGoalPs, newFinalGoalPs)
														   < motionConfig.m_ignoreMoveDistance);
			const bool withinMoveDistY = isCmdMoveToAp ? true : Abs(finalGoalPs.Y() - newFinalGoalPs.Y()) < 1.5f;

			const NavMoveArgs& newMoveArgs	   = command.GetMoveArgs();
			const NavMoveArgs& compareMoveArgs = compareCommand.GetMoveArgs();

			const bool sameGoalRadius = Abs(command.GetGoalRadius() - compareCommand.GetGoalRadius()) < 0.1f;
			const bool sameAp		  = command.GetGoalActionPackMgrId() == compareCommand.GetGoalActionPackMgrId();
			const bool sameReachedType	 = command.GetGoalReachedType() == compareCommand.GetGoalReachedType();
			const bool sameStrafeMode	 = newMoveArgs.m_strafeMode == compareMoveArgs.m_strafeMode;
			const bool sameMoveSetSubCat = command.GetMtSubcategory() == compareCommand.GetMtSubcategory();
			const bool sameMt = command.GetMotionType() == compareCommand.GetMotionType();
			const bool sameGoalFaceDirValid = newMoveArgs.m_goalFaceDirValid == compareMoveArgs.m_goalFaceDirValid;
			const bool sameGoalFaceDir		= sameGoalFaceDirValid
										 && (!newMoveArgs.m_goalFaceDirValid
											 || (Dot(newMoveArgs.m_goalFaceDirPs, compareMoveArgs.m_goalFaceDirPs)
												 > NavCommand::kGoalDirEqualityDotThresh));
			const bool sameApUserData = !isCmdMoveToAp
										|| command.GetActionPackUserData() == compareCommand.GetActionPackUserData();

			const bool canKillCommand = sameMt && sameType && withinMoveDistXZ && withinMoveDistY && sameGoalRadius
										&& sameAp && sameReachedType && sameMoveSetSubCat && sameStrafeMode
										&& sameGoalFaceDir && sameApUserData;

			if (canKillCommand)
			{
				// we are already working on this command
				killCommand = true;
			}
			else if (isCmdMoveToAp && IsActionPackUsageEntered()
					 && (m_hCurrentActionPack.GetMgrId() == command.GetGoalActionPackMgrId()) && !commandPending)
			{
				killCommand = true;
			}
		}
		break;

	case NavCommand::kMoveAlongSpline:
		break;

	case NavCommand::kMoveInDirection:
		if ((commandPending || IsCommandInProgress() || IsState(kStateMovingBlind))
			&& (curStatus != NavCommand::kStatusCommandFailed))
		{
			NavCommand& compareCommand		   = commandPending ? GetPendingCommand() : m_activeContext.m_command;
			const NavMoveArgs& newMoveArgs	   = command.GetMoveArgs();
			const NavMoveArgs& compareMoveArgs = compareCommand.GetMoveArgs();

			const bool sameType		  = cmdType == compareCommand.GetType();
			const bool sameStrafeMode = newMoveArgs.m_strafeMode == compareMoveArgs.m_strafeMode;
			const bool sameMoveSetSubCat = command.GetMtSubcategory() == compareCommand.GetMtSubcategory();
			const bool sameMt = command.GetMotionType() == compareCommand.GetMotionType();

			const bool canKillCommand = sameType && sameStrafeMode && sameMoveSetSubCat && sameMt;

			if (canKillCommand)
			{
				// we are already working on this command
				killCommand = true;

				compareCommand.UpdateMoveDirPs(command.GetMoveDirPs());
			}
		}
		break;

	case NavCommand::kSteerToLocation:
		if ((commandPending || IsCommandInProgress() || IsState(kStateMovingBlind))
			&& (curStatus != NavCommand::kStatusCommandFailed))
		{
			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

			// if no command pending, and prev command is in progress and if this move command is identical to previous command
			const MotionConfig& motionConfig = m_pNavChar->GetMotionConfig();
			const Locator& parentSpace = m_pNavChar->GetParentSpace();
			const Point finalGoalPs = GetFinalDestPointPs();

			NavCommand& compareCommand = commandPending ? GetPendingCommand() : m_activeContext.m_command;
			const bool sameType = cmdType == compareCommand.GetType();

			const Point newFinalGoalPs = parentSpace.UntransformPoint(command.GetGoalLocation().GetPosWs());
			NAV_ASSERT(IsReasonable(newFinalGoalPs));

			const bool withinMoveDistXZ = (DistXz(finalGoalPs, newFinalGoalPs) < motionConfig.m_ignoreMoveDistance);
			const bool withinMoveDistY	= Abs(finalGoalPs.Y() - newFinalGoalPs.Y()) < 1.5f;

			const NavMoveArgs& newMoveArgs = command.GetMoveArgs();
			const NavMoveArgs& compareMoveArgs = compareCommand.GetMoveArgs();

			const bool sameGoalRadius = Abs(command.GetGoalRadius() - compareCommand.GetGoalRadius()) < 0.1f;
			const bool sameAp = command.GetGoalActionPackMgrId() == compareCommand.GetGoalActionPackMgrId();
			const bool sameStrafeMode = newMoveArgs.m_strafeMode == compareMoveArgs.m_strafeMode;
			const bool sameMoveSetSubCat = command.GetMtSubcategory() == compareCommand.GetMtSubcategory();
			const bool sameMt = command.GetMotionType() == compareCommand.GetMotionType();

			const bool canKillCommand = sameMt && sameType && withinMoveDistXZ && withinMoveDistY && sameGoalRadius
										&& sameMoveSetSubCat && sameStrafeMode;

			if (canKillCommand)
			{
				// we are already working on this command
				killCommand = true;

				compareCommand.UpdateSteerRateDps(command.GetSteerRateDps());
			}
		}
		break;
	}

	if (!killCommand)
	{
		++m_nextCommandId;
		if (m_nextCommandId == kInvalidCommandId)
			++m_nextCommandId;
	}

	const char* strStatus = "";
	if (killCommand)
	{
		strStatus = "(command killed)";
	}
	else if (commandPending)
	{
		strStatus = "(*** WARNING *** Command overwrite in progress!)";
	}

	if (!killCommand)
	{
		switch (command.GetType())
		{
		case NavCommand::kStopAndStand:
			LogHdr("Received command StopAndStand %s [%s:%d %s]\n",
				   strStatus,
				   command.GetSourceFile(),
				   command.GetSourceLine(),
				   command.GetSourceFunc());
			break;

		case NavCommand::kMoveToLocation:
			LogHdr("Received command MoveTo %s ", GetMotionTypeName(command.GetMotionType()));
			LogNavLocation(command.GetGoalLocation());
			LogMsg(" from ");
			LogPoint(m_pNavChar->GetNavigationPosPs());
			LogMsg(" %s  [%s:%d %s]\n",
				   strStatus,
				   command.GetSourceFile(),
				   command.GetSourceLine(),
				   command.GetSourceFunc());
			break;

		case NavCommand::kMoveToActionPack:
			LogHdr("Received command MoveToActionPack %s (", GetMotionTypeName(command.GetMotionType()));
			LogActionPack(command.GetGoalActionPack());
			LogMsg(") from ");
			LogPoint(m_pNavChar->GetNavigationPosPs());
			LogMsg(" %s [%s:%d %s]\n",
				   strStatus,
				   command.GetSourceFile(),
				   command.GetSourceLine(),
				   command.GetSourceFunc());
			break;

		case NavCommand::kMoveAlongSpline:
			{
				const CatmullRom* pPathSpline = command.GetPathSpline();
				LogHdr("Received command MoveAlongSpline %s '%s' [%0.2f:%0.2f] from ",
					   GetMotionTypeName(command.GetMotionType()),
					   pPathSpline ? pPathSpline->GetBareName() : "<null>",
					   command.GetSplineArcStart(),
					   command.GetSplineArcGoal());

				LogPoint(m_pNavChar->GetNavigationPosPs());
				LogMsg(" %s [%s:%d %s]\n",
					   strStatus,
					   command.GetSourceFile(),
					   command.GetSourceLine(),
					   command.GetSourceFunc());
			}
			break;

		case NavCommand::kMoveInDirection:
			LogHdr("Received command MoveInDirection %s -> %s %s [%s:%d %s]\n",
				   GetMotionTypeName(command.GetMotionType()),
				   PrettyPrint(command.GetMoveDirPs()),
				   strStatus,
				   command.GetSourceFile(),
				   command.GetSourceLine(),
				   command.GetSourceFunc());
			break;

		case NavCommand::kSteerToLocation:
			LogHdr("Received command SteerTo %s ", GetMotionTypeName(command.GetMotionType()));
			LogNavLocation(command.GetGoalLocation());
			LogMsg(" from ");
			LogPoint(m_pNavChar->GetNavigationPosPs());
			LogMsg(" %s  [%s:%d %s]\n",
				   strStatus,
				   command.GetSourceFile(),
				   command.GetSourceLine(),
				   command.GetSourceFunc());
			break;
		}
	}

	if (!killCommand)
	{
		m_queuedCommand = command;
	}

	return m_nextCommandId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavStateMachine::NavCommandContext* NavStateMachine::GetCommandContext(U32F commandId)
{
	if (m_activeContext.m_commandId == commandId)
		return &m_activeContext;
	if (m_nextContext.m_commandId == commandId)
		return &m_nextContext;
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavStateMachine::NavCommandContext* NavStateMachine::GetCommandContext(U32F commandId) const
{
	if (m_activeContext.m_commandId == commandId)
		return &m_activeContext;
	if (m_nextContext.m_commandId == commandId)
		return &m_nextContext;
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::ProcessCommand(NavCommand& cmd)
{
	bool activateImmediately = false;

	m_nextContext.Reset();
	m_nextContext.m_command	  = cmd;
	m_nextContext.m_commandId = m_nextCommandId;
	cmd.Reset();

	switch (m_nextContext.m_command.GetType())
	{
	case NavCommand::kStopAndStand:
		activateImmediately = ProcessCommand_StopAndStand(m_nextContext);
		break;

	case NavCommand::kMoveToLocation:
	case NavCommand::kMoveToActionPack:
	case NavCommand::kMoveAlongSpline:
	case NavCommand::kSteerToLocation:
			activateImmediately = ProcessCommand_MoveTo(m_nextContext);
		break;

	case NavCommand::kMoveInDirection:
		activateImmediately = ProcessCommand_MoveInDirection(m_nextContext);
		break;

	case NavCommand::kNone:
	default:
		StopPathFailed(m_nextContext, "Invalid command");
		activateImmediately = true;
		break;
	}

	m_shouldUpdatePath = m_nextContext.m_command.PathFindRequired()
						 && (m_nextContext.m_status == NavContextStatus::kWaitingForPath);

	if (activateImmediately)
	{
		if (m_activeContext.IsInProgress())
		{
			SetContextStatus(m_activeContext, NavContextStatus::kTerminated, "Stomping with new command");
		}

		m_activeContext = m_nextContext;
		m_nextContext.Reset();
	}

	ValidateActionPackUsage();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::ProcessCommand_StopAndStand(NavCommandContext& cmdContext)
{
	LogHdr("Processing command StopAndStand - STATUS = %s\n", NavCommand::GetStatusName(GetStatus()));

	ClearPath();

	const NavCommand& cmd = cmdContext.m_command;

	NavCharacter* pNavChar = m_pNavChar;
	INavAnimController* pNavAnimController = pNavChar->GetActiveNavAnimController();

	if (!pNavAnimController)
	{
		SetContextStatus(cmdContext, NavContextStatus::kFailed, "ProcessCommand_StopAndStand (no nav anim controller)");
		return true;
	}

	const bool animIsInterrupted = pNavAnimController->IsInterrupted();

	const StringId64 stateId = GetStateId();

	NavLocation goalLoc = pNavChar->GetNavLocation();

	SetContextStatus(cmdContext, NavContextStatus::kActive, "ProcessCommand_StopAndStand");

	bool pushAnim = false;

	switch (stateId.GetValue())
	{
	case SID_VAL("Stopped"):
		if (animIsInterrupted || cmd.GetMoveArgs().m_goalFaceDirValid)
		{
			pushAnim = true;
		}
		else
		{
			SetContextStatus(cmdContext, NavContextStatus::kSucceeded, "ProcessCommand_StopAndStand (already stopped)");
		}
		break;

	case SID_VAL("UsingActionPack"):
	case SID_VAL("EnteringActionPack"):
	case SID_VAL("UsingTraversalActionPack"):
		ExitActionPack();
		break;

	case SID_VAL("MovingAlongPath"):
	case SID_VAL("SteeringAlongPath"):
	case SID_VAL("Stopping"):
	case SID_VAL("MovingBlind"):
		{
			pushAnim = true;
		}
		break;

	case SID_VAL("StartingToExitActionPack"):
	case SID_VAL("ExitingActionPack"):
		{
			if (const TraversalActionPack* pTap = GetTraversalActionPack())
			{
				goalLoc = pTap->GetDestNavLocation();
			}
		}
		break;

	case SID_VAL("Resuming"):
		StopMovingImmediately(true, false, "ProcessCommand_StopAndStand[Resuming]");
		break;

	default:
		NAV_ASSERTF(false, ("Unhandled state '%s'", DevKitOnly_StringIdToString(stateId)));
		break;
	}

	SetFinalNavLocation(cmdContext, goalLoc, "ProcessCommand_StopAndStand");
	SetNextWaypoint(cmdContext, goalLoc, kWaypointFinalGoal, kNavGoalReachedTypeStop, "ProcessCommand_StopAndStand");

	if (pushAnim)
	{
		LogHdr("ProcessCommand_StopAndStand: NavAnimController::StopLocomoting()\n");

		NavAnimStopParams params;
		params.m_waypoint	= GetWaypointContract(cmdContext);
		params.m_goalRadius = cmd.GetGoalRadius();
		pNavAnimController->StopNavigating(params);

		SetCurrentActionPack(nullptr, "ProcessCommand_StopAndStand");
		GoStopping();
		GetStateMachine().TakeStateTransition();
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::TryFailCommandMoveTo(const NavCommand& cmd)
{
	// if at first you don't succeed... ?
	bool failed = false;

	INavAnimController* pNavAnimController = m_pNavChar ? m_pNavChar->GetActiveNavAnimController() : nullptr;
	if (!pNavAnimController)
	{
		failed = true;
	}

	if (cmd.GetMotionType() >= kMotionTypeMax)
	{
		LogHdr("Processing command MoveTo FAILED with invalid motion type\n");
		failed = true;
	}

	switch (cmd.GetType())
	{
	case NavCommand::kMoveToActionPack:
		{
			ActionPack* pGoalAp = cmd.GetGoalActionPack();

			if (!pGoalAp)
			{
				// Fail the command if MoveToActionPack and action pack has gone stale
				LogHdr("Processing command MoveToActionPack (but action pack has disappeared!) FAILED\n");
				failed = true;
			}
			else if (pGoalAp->GetType() == ActionPack::kCoverActionPack)
			{
				const CoverActionPack* pGoalCoverAp = (const CoverActionPack*)pGoalAp;
				const NavCharacter* pBlockingChar	= pGoalCoverAp->GetBlockingNavChar();
				if (pBlockingChar && (pBlockingChar != m_pNavChar))
				{
					LogHdr("Processing command MoveToActionPack (but action pack is blocked by '%s') FAILED\n",
						   pBlockingChar->GetName());
					failed = true;
				}
			}

			if (!failed && pGoalAp)
			{
				const ActionPack::Type apType = pGoalAp->GetType();
				ActionPackController* pActionPackController = m_pAnimationControllers->GetControllerForActionPackType(apType);
				if (!pActionPackController)
				{
					LogHdr("Processing command MoveToActionPack (but no controller for action pack) FAILED\n");
					failed = true;
				}
			}
		}
		break;

	case NavCommand::kMoveAlongSpline:
		{
			const CatmullRom* pSpline = cmd.GetPathSpline();

			if (!pSpline)
			{
				LogHdr("Processing command MoveAlongSpline (but spline has disappeared!) FAILED\n");
				failed = true;
			}
		}
		break;

	case NavCommand::kMoveInDirection:
		{
			const Vector moveDirPs = cmd.GetMoveDirPs();
			const float vecLen	   = Length(moveDirPs);
			if (vecLen < 0.01f)
			{
				LogHdr("Processing command MoveInDirection FAILED (move dir is too small %s)", PrettyPrint(moveDirPs));
				failed = true;
			}
		}
		break;
	}

	return failed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::ProcessCommand_MoveTo(NavCommandContext& cmdContext)
{
	const NavCommand& cmd = cmdContext.m_command;

	if (TryFailCommandMoveTo(cmd))
	{
		StopPathFailed(cmdContext, "Move-to command failed");
		return true;
	}

	const NavMoveArgs& moveArgs = cmd.GetMoveArgs();

	ActionPack* pGoalAp = cmd.GetGoalActionPack();
	cmdContext.m_hNextActionPack = nullptr;

	NavGoalReachedType nextNavReachedType = cmd.GetGoalReachedType();
	WaypointType nextWaypointType		  = kWaypointFinalGoal;
	ActionPack* pNextAp = nullptr;

	const CatmullRom* const pCmdSpline = cmd.GetPathSpline();

	cmdContext.m_hPathSpline = pCmdSpline;

	const float splineArcStart = cmd.GetSplineArcStart();
	const float splineArcGoal  = cmd.GetSplineArcGoal();
	const float finalSplineArc = cmd.GetSplineArcGoal();
	const float splineArcStep  = cmd.GetSplineArcStep();
	const float splineAdvanceStartTolerance = cmd.GetSplineAdavanceStartTolerance();
	bool beginSplineAdvancement = false;

	if (pCmdSpline)
	{
		const Locator charLocWs = m_pNavChar->GetLocator();
		float curArcLen = 0.0f;

		Nav::UpdateSplineAdvancement(charLocWs,
									 kZero,
									 pCmdSpline,
									 splineArcGoal,
									 splineArcStep,
									 m_pNavChar->GetOnSplineRadius(),
									 splineAdvanceStartTolerance,
									 beginSplineAdvancement,
									 curArcLen);
	}

	cmdContext.m_curPathSplineArcStart = splineArcStart;
	cmdContext.m_curPathSplineArcGoal  = splineArcGoal;
	cmdContext.m_finalPathSplineArc	   = splineArcGoal;
	cmdContext.m_pathSplineArcStep	   = splineArcStep;
	cmdContext.m_splineAdvanceStartTolerance = splineAdvanceStartTolerance;
	cmdContext.m_beginSplineAdvancement		 = beginSplineAdvancement;

	NavLocation finalNavGoal = cmd.GetGoalLocation();

	const NavCommand::Type cmdType = cmd.GetType();

	switch (cmdType)
	{
	case NavCommand::kMoveToActionPack:
		{
			ActionPackResolveInput apResolveInfo = MakeApResolveInput(cmd, pGoalAp);

			const ActionPack::Type apType = pGoalAp->GetType();
			ActionPackController* pActionPackController = m_pAnimationControllers->GetControllerForActionPackType(apType);
			NAV_ASSERT(pActionPackController);

			if (pActionPackController->ResolveDefaultEntry(apResolveInfo, pGoalAp, &cmdContext.m_actionPackEntry))
			{
				cmdContext.m_actionPackEntry.m_mtSubcategoryId = apResolveInfo.m_mtSubcategory;
			}
			else
			{
				ActionPackEntryDefSetupDefaults(&cmdContext.m_actionPackEntry, pGoalAp, m_pNavChar);
			}

			finalNavGoal = cmdContext.m_actionPackEntry.m_entryNavLoc;

			nextNavReachedType = cmdContext.m_actionPackEntry.m_stopBeforeEntry ? kNavGoalReachedTypeStop
																				: kNavGoalReachedTypeContinue;
			nextWaypointType = kWaypointAPDefaultEntry;
			pNextAp = pGoalAp;
		}
		break;

	case NavCommand::kMoveAlongSpline:
		if (pCmdSpline)
		{
			const Point goalPosWs = pCmdSpline->EvaluatePointAtArcLength(splineArcGoal);

			finalNavGoal = m_pNavChar->AsReachableNavLocationWs(goalPosWs, NavLocation::Type::kNavPoly);
		}
		break;
	}

	if (FALSE_IN_FINAL_BUILD(true))
	{
		switch (cmdType)
		{
		case NavCommand::kMoveToLocation:
			LogHdr("Processing command MoveTo %s%s to ",
				   GetMotionTypeName(cmd.GetMotionType()),
				   (moveArgs.m_strafeMode == DC::kNpcStrafeAlways) ? "-Strafe" : "");
			LogNavLocation(finalNavGoal);
			LogMsg(" from %s ", PrettyPrint(m_pNavChar->GetNavigationPosPs()));
			if (moveArgs.m_goalFaceDirValid)
			{
				LogMsg(" (face-dir %s)", PrettyPrint(moveArgs.m_goalFaceDirPs));
			}
			LogMsg("\n");
			break;

		case NavCommand::kMoveToActionPack:
			LogHdr("Processing command MoveToActionPack %s%s (",
				   GetMotionTypeName(cmd.GetMotionType()),
				   (moveArgs.m_strafeMode == DC::kNpcStrafeAlways) ? "-Strafe" : "");
			LogActionPack(pGoalAp);
			LogMsg(") starting from (");
			LogLocator(m_pNavChar->GetLocator());
			LogMsg(")");
			if (moveArgs.m_apUserData)
			{
				LogMsg(" <%d>", moveArgs.m_apUserData);
			}
			LogMsg("\n");
			break;

		case NavCommand::kMoveAlongSpline:
			LogHdr("Processing command MoveAlongSpline %s%s '%s' [%0.2f %s -> %0.2f %s @ %0.2fm step] from %s ",
				   GetMotionTypeName(cmd.GetMotionType()),
				   (moveArgs.m_strafeMode == DC::kNpcStrafeAlways) ? "-Strafe" : "",
				   pCmdSpline->GetBareName(),
				   splineArcStart,
				   PrettyPrint(pCmdSpline->EvaluatePointGlobal(splineArcStart)),
				   splineArcGoal,
				   PrettyPrint(pCmdSpline->EvaluatePointGlobal(splineArcGoal)),
				   splineArcStep,
				   PrettyPrint(m_pNavChar->GetNavigationPosPs()));

			if (moveArgs.m_goalFaceDirValid)
			{
				LogMsg(" (face-dir %s)", PrettyPrint(moveArgs.m_goalFaceDirPs));
			}
			LogMsg("\n");
			break;
		}
	}

	NAV_ASSERT((cmdType != NavCommand::kMoveAlongSpline) || pCmdSpline);

	SetContextStatus(cmdContext, NavContextStatus::kWaitingForPath, "ProcessCommand_MoveTo");
	SetFinalNavLocation(cmdContext, finalNavGoal, "ProcessCommand_MoveTo");
	SetNextWaypoint(cmdContext, finalNavGoal, nextWaypointType, nextNavReachedType, "ProcessCommand_MoveTo");
	SetNextActionPack(cmdContext, pNextAp, "ProcessCommand_MoveTo");

	if (TryEarlyCompleteMoveCommand(cmdContext))
	{
		GetStateMachine().TakeStateTransition();
		return true;
	}
	else if (IsActionPackUsageEntered())
	{
		ClearPath();
	}

	m_shouldUpdatePath = true;

	bool activateImmediately = false;

	if (moveArgs.m_hChaseChar.Assigned())
	{
		activateImmediately = true;
	}
	else
	{
		NavJobScheduler::Get().NotifyWantToMove(m_pNavChar);
	}

	return activateImmediately;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::ProcessCommand_MoveInDirection(NavCommandContext& cmdContext)
{
	const NavCommand& cmd = cmdContext.m_command;

	if (TryFailCommandMoveTo(cmd))
	{
		StopPathFailed(cmdContext, "Move in dir command failed");
		return true;
	}

	LogHdr("Processing command MoveInDirection %s '%s'\n",
		   GetMotionTypeName(cmd.GetMotionType()),
		   PrettyPrint(cmd.GetMoveDirPs()));

	ClearPath();

	const NavLocation finalNavGoal = m_pNavChar->GetNavLocation();
	SetFinalNavLocation(cmdContext, finalNavGoal, "ProcessCommand_MoveInDirection");
	SetNextWaypoint(cmdContext,
					finalNavGoal,
					kWaypointFinalGoal,
					kNavGoalReachedTypeContinue,
					"ProcessCommand_MoveInDirection");

	if (IsActionPackUsageEntered())
	{
		ExitActionPack();
		SetContextStatus(cmdContext, NavContextStatus::kActive, "ProcessCommand_MoveInDirection [AP]");
	}
	else if (INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController())
	{
		SetContextStatus(cmdContext, NavContextStatus::kSucceeded, "ProcessCommand_MoveInDirection");

		NavAnimStartParams startParams;
		startParams.m_moveDirPs = cmd.GetMoveDirPs();
		startParams.m_waypoint	= GetWaypointContract(cmdContext);
		startParams.m_moveArgs	= cmd.GetMoveArgs();

		pNavAnimController->StartNavigating(startParams);

		GoMovingBlind();
		GetStateMachine().TakeStateTransition();
	}
	else
	{
		SetContextStatus(cmdContext, NavContextStatus::kFailed, "ProcessCommand_MoveInDirection [Failed]");
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::SetContextStatus(NavCommandContext& cmdContext, NavContextStatus status, const char* msg)
{
	if (cmdContext.m_status != status)
	{
		LogHdr("Setting command %d status to '%s' (was '%s') : %s\n",
			   cmdContext.m_commandId,
			   GetNavContextStatusName(status),
			   GetNavContextStatusName(cmdContext.m_status),
			   msg);

		cmdContext.m_status = status;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::TryEarlyCompleteMoveCommand(NavCommandContext& cmdContext)
{
	const NavCommand& cmd = cmdContext.m_command;
	const NavCommand::Type cmdType = cmd.GetType();

	switch (cmdType)
	{
	case NavCommand::kMoveToLocation:
	case NavCommand::kMoveInDirection:
	case NavCommand::kSteerToLocation:
		break;

	case NavCommand::kMoveToActionPack:
		{
			if (!IsState(kStateUsingActionPack) && !IsState(kStateEnteringActionPack))
			{
				return false;
			}

			const ActionPack* pGoalAp = cmd.GetGoalActionPack();

			if (!pGoalAp)
			{
				return false;
			}

			if (pGoalAp->GetMgrId() == m_hCurrentActionPack.GetMgrId())
			{
				const NavContextStatus newStatus = IsState(kStateEnteringActionPack) ? NavContextStatus::kActive
																					 : NavContextStatus::kSucceeded;

				SetContextStatus(cmdContext, newStatus, "TryEarlyCompleteMoveCommand[CurAP]");

				return true;
			}
			else
			{
				if (!pGoalAp || !pGoalAp->IsAvailableFor(m_pNavChar))
				{
					return false;
				}

				ActionPackController* pDestController = m_pAnimationControllers->GetControllerForActionPackType(pGoalAp->GetType());
				if (!pDestController)
				{
					return false;
				}

				const ActionPackResolveInput apResolveInfo = MakeApResolveInput(cmd, pGoalAp);

				if (!pDestController->ResolveForImmediateEntry(apResolveInfo, pGoalAp, &cmdContext.m_actionPackEntry))
				{
					return false;
				}

				if (!SetCurrentActionPack(pGoalAp, "TryEarlyCompleteMoveCommand"))
				{
					LogHdr("TryEarlyCompleteMoveCommand: Failed to change to my goal AP\n");

					return false;
				}

				if (!pDestController->EnterImmediately(pGoalAp, cmdContext.m_actionPackEntry))
				{
					LogHdr("TryEarlyCompleteMoveCommand: EnterImmediately() failed!\n");

					return false;
				}

				SetContextStatus(cmdContext, NavContextStatus::kActive, "TryEarlyCompleteMoveCommand[NewAP]");

				OnEnterActionPack(pGoalAp);
				GoEnteringActionPack();
				GetStateMachine().TakeStateTransition();

				return true;
			}
		}
		break;

	case NavCommand::kMoveAlongSpline:
		{
			const CatmullRom* pCmdSpline = cmdContext.m_hPathSpline.ToCatmullRom();
			const float splineArcStart	 = cmd.GetSplineArcStart();
			const float splineArcGoal	 = cmd.GetSplineArcGoal();
			const float finalSplineArc	 = cmd.GetSplineArcGoal();
			const float splineArcStep	 = cmd.GetSplineArcStep();

			if (pCmdSpline && !pCmdSpline->IsLooped() && cmdContext.m_beginSplineAdvancement
				&& Sign(splineArcGoal - splineArcStart) != Sign(splineArcStep))
			{
				SetContextStatus(cmdContext, NavContextStatus::kSucceeded, "TryEarlyCompleteMoveCommand[Spline]");
				return true;
			}
		}
		break;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::ExitActionPack()
{
	if (IsActionPackUsageEntered())
	{
		const ActionPack::Type apType = GetActionPackUsageType();
		ActionPackController* pActionPackController = m_pAnimationControllers->GetControllerForActionPackType(apType);
		if (pActionPackController)
		{
			GoStartingToExitActionPack();
			GetStateMachine().TakeStateTransition();
		}
		else
		{
			if (m_activeContext.IsInProgress())
			{
				SetContextStatus(m_activeContext, NavContextStatus::kFailed, "ExitActionPack: No Controller");
			}

			SetCurrentActionPack(nullptr, "ExitActionPack");

			GoStopped();
			GetStateMachine().TakeStateTransition();
		}
	}
	else
	{
		if (m_activeContext.IsInProgress())
		{
			SetContextStatus(m_activeContext, NavContextStatus::kFailed, "ExitActionPack: Not in an AP (?)");
		}

		GoStopped();
		GetStateMachine().TakeStateTransition();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::StopMovingImmediately(bool pushAnim, bool changeActiveCmd, const char* pStrMsg)
{
	LogHdr("StopMovingImmediately: %s [pushAnim: %s] [changeActiveCmd: %s]\n",
		   pStrMsg ? pStrMsg : "",
		   pushAnim ? "true" : "false",
		   changeActiveCmd ? "true" : "false");

	INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController();

	if (pushAnim && pNavAnimController)
	{
		NavAnimStopParams params;
		params.m_waypoint.m_motionType	= kMotionTypeMax;
		params.m_waypoint.m_reachedType = kNavGoalReachedTypeStop;
		params.m_waypoint.m_rotPs		= m_pNavChar->GetRotationPs();
		params.m_goalRadius = 1.0f;

		pNavAnimController->StopNavigating(params);
	}

	if (changeActiveCmd)
	{
		const NavLocation goalLoc = m_pNavChar->GetNavLocation();
		SetNextWaypoint(m_activeContext, goalLoc, kWaypointFinalGoal, kNavGoalReachedTypeStop, pStrMsg);
		SetFinalNavLocation(m_activeContext, goalLoc, pStrMsg);
	}

	if (!IsState(kStateStopping) && !IsState(kStateStopped))
	{
		GoStopping();
		GetStateMachine().TakeStateTransition();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::StopPathFailed(NavCommandContext& cmdContext, const char* msg)
{
	LogHdr("StopPathFailed [nav-%d]: %s %s %s\n",
		   cmdContext.m_commandId,
		   msg ? msg : "",
		   IsCommandInProgress() ? "status CommandFailed" : "",
		   m_hNavPathJob.IsValid() ? "(Will ignore in-flight single path find)" : "");

	if (cmdContext.IsInProgress() || (cmdContext.m_status == NavContextStatus::kInvalid))
	{
		SetContextStatus(cmdContext, NavContextStatus::kFailed, msg);
	}

	// yuck
	if (cmdContext.m_commandId == m_activeContext.m_commandId)
	{
		ClearPath();

		m_shouldUpdatePath = false;

		bool canStopMoving = true;

		if (IsState(kStateStartingToExitActionPack))
		{
			// let StartingToExitActionPack handle this by issuing Exit() without a path (we check for status command failed)
			canStopMoving = false;
		}
		else if (IsState(kStateUsingActionPack))
		{
			// inside an actionpack (and not a TAP) lets just stay here? like we might be in cover
			canStopMoving = false;
		}
		else if (IsState(kStateUsingTraversalActionPack))
		{
			if (CanProcessCommands())
			{
				if (INdAiTraversalController* pTapController = m_pAnimationControllers->GetTraversalController())
				{
					pTapController->Exit(nullptr);
				}

				canStopMoving = true;
			}
		}

		if (canStopMoving)
		{
			StopMovingImmediately(true, true, "StopPathFailed");

			SetCurrentActionPack(nullptr, "StopPathFailed");
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::ClearPath()
{
	LogHdr("Clearing path%s\n", m_hNavPathJob.IsValid() ? " (and ignoring in-flight results)" : "");

	m_currentPathPs.Clear();

	m_pNavChar->GetNavControl()->ClearCachedPathPolys();

	m_pathStatus = PathStatus();
	m_pathStatus.m_lastPathFoundTime	 = m_pNavChar->GetCurTime();
	m_pathStatus.m_ignoreInFlightResults = m_hNavPathJob.IsValid();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::InternalReset(const ResetType resetType)
{
	AtomicLockJanitor lock(&m_pathResultsLock, FILE_LINE_FUNC);

	// stomp any pending command
	m_queuedCommand.Reset();
	m_activeContext.Reset();
	m_nextContext.Reset();

	const Point newPosPs = m_pNavChar ? m_pNavChar->GetTranslationPs() : GetFinalDestPointPs();

	NavControl* pNavControl = m_pNavChar ? m_pNavChar->GetNavControl() : nullptr;

	// Reset the nav control (except on shutdown, when we may need navmesh for probing during death anims)
	if (pNavControl && resetType != kResetTypeShutdown)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		pNavControl->ResetNavMesh(nullptr);
		pNavControl->UpdatePs(newPosPs, m_pNavChar);
	}

	// this comes before ReleaseActionPack() to avoid triggering an ASSERT in ReleaseActionPack()
	GoStopped();
	GetStateMachine().TakeStateTransition();

	SetCurrentActionPack(nullptr, "InternalReset");
	ClearPath();

	m_shouldUpdatePath	   = false;
	m_resumeStateRequested = false;
	m_abandonInterruptedCommand = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Shutdown()
{
	LogHdr("SHUTDOWN\n");

	// InternalReset assumes Init was called but it isn't in rare cases (like spawning a bad character type)
	if (m_pNavChar)
	{
		InternalReset(kResetTypeShutdown);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Reset(bool playIdle /*= true*/)
{
	LogHdr("Reset NavStateMachine and all animation controllers");
	if (IsCommandPending())
	{
		LogMsg(" (stomped on a pending command %s)", GetPendingCommand().GetTypeName());
	}
	LogMsg("\n");

	InternalReset(kResetTypeReset);

	if (NavControl* pNavControl = m_pNavChar->GetNavControl())
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		const Point navPosPs = m_pNavChar->GetTranslationPs();

		pNavControl->UpdatePs(navPosPs, m_pNavChar);
	}

	INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController();

	// sometimes this is called from the NavChar destructor...
	if (NdAnimationControllers* pAnimControllers = m_pNavChar->GetNdAnimationControllers())
	{
		pAnimControllers->ResetNavigation(pNavAnimController, playIdle);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const TraversalActionPack* NavStateMachine::GetTraversalActionPack() const
{
	if (const ActionPack* pCurAp = m_hCurrentActionPack.ToActionPack())
	{
		if (pCurAp->GetType() == ActionPack::kTraversalActionPack)
		{
			return (const TraversalActionPack*)pCurAp;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
TraversalActionPack* NavStateMachine::GetTraversalActionPack()
{
	if (ActionPack* pCurAp = m_hCurrentActionPack.ToActionPack())
	{
		if (pCurAp->GetType() == ActionPack::kTraversalActionPack)
		{
			return (TraversalActionPack*)pCurAp;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::SwitchActionPack(ActionPack* pNewAp)
{
	NAV_ASSERT(pNewAp && (pNewAp->IsAvailableFor(m_pNavChar) || pNewAp->IsReservedBy(m_pNavChar)));

	if (ActionPackHandle(pNewAp).ToActionPack() == nullptr)
	{
		MsgConErr("NPC %s: SwitchActionPack() to ActionPack '%s' that isn't registered\n",
				  m_pNavChar->GetName(),
				  pNewAp ? pNewAp->GetName() : "<null>");
		return;
	}

	const bool interrupted = IsState(kStateInterrupted);

	LogHdr("SwitchActionPack: Switched to new action pack");
	if (IsCommandPending() && !interrupted)
	{
		LogMsg(" stomped on a pending command %s", GetPendingCommand().GetTypeName());
	}
	LogMsg("\n");

	if (!interrupted)
	{
		// stomp any pending command
		InternalReset(kResetTypeReset);
	}
	else if (m_resumeStateRequested)
	{
		m_resumeStateRequested = false;
	}

	// Reset the nav control
	if (NavControl* pNavControl = m_pNavChar->GetNavControl())
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		const Point navPosPs = m_pNavChar->GetNavigationPosPs();

		pNavControl->UpdatePs(navPosPs, m_pNavChar);
	}

	LogHdr("SwitchActionPack: NavAnimController::Reset()\n");
	INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController();
	pNavAnimController->Reset();

	pNewAp->SetInSwap(true);

	const bool successfulReserve = SetCurrentActionPack(pNewAp, "SwitchActionPack");

	NAV_ASSERT(successfulReserve);
	if (successfulReserve)
	{
		OnEnterActionPack(pNewAp);
	}
	else
	{
		LogHdr("FAILED to reserve swap ActionPack ");
		LogActionPack(pNewAp);
		LogMsg("\n");

		if (!interrupted)
		{
			InternalReset(kResetTypeReset);
		}
	}

	pNewAp->SetInSwap(false);

	if (successfulReserve)
	{
		if (GetActionPackUsageType() != ActionPack::kTraversalActionPack)
		{
			if (!IsState(kStateUsingActionPack))
			{
				GoUsingActionPack();
				GetStateMachine().TakeStateTransition();
			}
		}
		else
		{
			const TraversalActionPack* pTap = (const TraversalActionPack*)pNewAp;

			// this is for TeleportIntoTap(), final dest point should be tap exit.
			SetNextWaypoint(m_activeContext,
							pTap->GetDestNavLocation(),
							kWaypointFinalGoal,
							kNavGoalReachedTypeStop,
							"SwitchActionPack");

			SetNextActionPack(m_activeContext, nullptr, "SwitchActionPack");

			ClearPath();

			EnterTraversalActionPack();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const WaypointContract NavStateMachine::GetWaypointContract(const NavCommandContext& context) const
{
	WaypointContract waypointContract;

	const Quat myRotPs = m_pNavChar->GetRotationPs();
	Quat rotPs		   = myRotPs;
	NavGoalReachedType goalReachedType = kNavGoalReachedTypeStop;
	bool exactApproach = false;

	MotionType motionType = &m_activeContext == &context ? m_pNavChar->GetRequestedMotionType()
														 : context.m_command.GetMotionType();

	const ActionPackEntryDef& apEntryDef = context.m_actionPackEntry;

	switch (context.m_nextWaypointType)
	{
	case kWaypointAPResolvedEntry:
	case kWaypointAPDefaultEntry:
		NAV_ASSERT(IsFinite(apEntryDef.m_entryRotPs));

		if (apEntryDef.m_preferredMotionType < kMotionTypeMax)
		{
			motionType = apEntryDef.m_preferredMotionType;
		}

		rotPs = apEntryDef.m_entryRotPs;
		goalReachedType = apEntryDef.m_stopBeforeEntry ? kNavGoalReachedTypeStop : kNavGoalReachedTypeContinue;
		exactApproach = true;
		break;

	case kWaypointAPWait:
		rotPs = myRotPs;
		goalReachedType = kNavGoalReachedTypeStop;
		break;

	case kWaypointFinalGoal:
		{
			Vector faceVecXzPs = kUnitZAxis;
			const NavMoveArgs& moveArgs = context.m_command.GetMoveArgs();

			if (moveArgs.m_goalFaceDirValid)
			{
				faceVecXzPs = VectorXz(moveArgs.m_goalFaceDirPs);
			}
			else
			{
				const Point facePosPs  = m_pNavChar->GetFacePositionPs();
				const Point finalPosPs = context.m_finalNavPosPs;

				NAV_ASSERTF(IsReasonable(facePosPs), ("Npc '%s' has garbage face position", m_pNavChar->GetName()));
				NAV_ASSERTF(IsReasonable(finalPosPs), ("Npc '%s' has garbage goal position", m_pNavChar->GetName()));

				faceVecXzPs = AsUnitVectorXz(facePosPs - finalPosPs, GetLocalZ(myRotPs));
			}

			rotPs = QuatFromLookAt(faceVecXzPs, kUnitYAxis);
			goalReachedType = context.m_nextNavReachedType;

			exactApproach = moveArgs.m_destAnimId != INVALID_STRING_ID_64;
		}
		break;
	}

	NAV_ASSERT(IsFinite(rotPs));

	waypointContract.m_rotPs		 = rotPs;
	waypointContract.m_reachedType	 = goalReachedType;
	waypointContract.m_motionType	 = motionType;
	waypointContract.m_exactApproach = exactApproach;

	return waypointContract;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::NextWaypointIsForAp(const NavCommandContext& cmdContext) const
{
	bool isForAp = false;

	switch (cmdContext.m_nextWaypointType)
	{
	case kWaypointAPResolvedEntry:
	case kWaypointAPDefaultEntry:
	case kWaypointAPWait:
		isForAp = true;
		break;
	}

	return isForAp;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Interrupt(const char* sourceFile, U32F sourceLine, const char* sourceFunc)
{
	m_interruptSourceFile = sourceFile;
	m_interruptSourceLine = sourceLine;
	m_interruptSourceFunc = sourceFunc;

	if (IsState(kStateInterrupted))
	{
		// already interrupted, do nothing
		return;
	}

	LogHdr("Interrupt: command Interrupted (all animation controllers interrupted) : %s:%d:%s\n",
		   sourceFile,
		   sourceLine,
		   sourceFunc);

	m_pInterruptedState			= GetStateMachine().GetStateDescriptor();
	m_resumeStateRequested		= false;
	m_abandonInterruptedCommand = false;

	// Reset navigation anim controllers
	m_pAnimationControllers->InterruptNavControllers();

	if (INdAiTraversalController* pTapController = m_pAnimationControllers->GetTraversalController())
	{
		pTapController->Reset();
	}

	if (IsState(kStateUsingActionPack) || IsState(kStateEnteringActionPack) || IsState(kStateStartingToExitActionPack)
		|| IsState(kStateExitingActionPack))
	{
		const ActionPack::Type apType = GetActionPackUsageType();

		if (AnimActionController* pActionPackController = m_pAnimationControllers->GetControllerForActionPackType(apType))
		{
			// only reset a controller that is not interrupting navigation
			if (!pActionPackController->ShouldInterruptNavigation())
			{
				pActionPackController->Reset();
			}
		}
	}

	GoInterrupted();
	GetStateMachine().TakeStateTransition();

	SetCurrentActionPack(nullptr, "Interrupt");
	ClearPath();
	m_shouldUpdatePath = false;

	m_activeContext.m_actionPackEntry = ActionPackEntryDef();

	if (m_activeContext.IsValid() && !m_activeContext.IsInProgress())
	{
		m_activeContext.Reset();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Resume()
{
	if (!m_pInterruptedState)
		return;

	const AnimControl* pAnimControl		  = m_pNavChar->GetAnimControl();
	const AnimStateLayer* pBaseLayer	  = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurInstance = pBaseLayer->CurrentStateInstance();
	const StringId64 currentAnimState	  = pCurInstance ? pCurInstance->GetStateName() : INVALID_STRING_ID_64;
	const bool curStateValid = pCurInstance ? (pCurInstance->GetStateFlags() & DC::kAnimStateFlagLocomotionReady)
											: false;
	const StateChangeRequest* pChange = pBaseLayer->GetPendingChangeRequest(0);
	bool pendingValidStateChange	  = false;

	/*
		MAIL_ASSERT(Animation,
					!curStateValid || LocomotionReadyRequirementsMet(m_pNavChar, pCurInstance->GetState()),
					("anim-state '%s' isn't actually locomotion-ready", pCurInstance->GetState()->m_name.m_string.c_str()));
	*/

	if (pChange && pChange->m_type == StateChangeRequest::kTypeFlagDirectFade)
	{
		if (const DC::AnimState* pAnimState = AnimActorFindState(pAnimControl->GetActor(), pChange->m_transitionId))
		{
			ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
			AnimStateSnapshot stateSnapshot;
			const U32F maxNodeSize = g_animNodeLibrary.GetMaxNodeSize();
			const U32F memSize	   = 64 * maxNodeSize;
			U8* pSnapshotMem	   = NDI_NEW U8[memSize];
			NAV_ASSERT(pSnapshotMem);
			SnapshotNodeHeap heap;
			heap.Init(pSnapshotMem, memSize, 64, 12345);
			stateSnapshot.Init(&heap);

			const AnimTable& animTable	  = pAnimControl->GetAnimTable();
			const AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays();
			const FgAnimData* pAnimData	  = &pAnimControl->GetAnimData();

			const DC::AnimInfoCollection* pInfoCollection = pAnimControl->GetInfoCollection();

			stateSnapshot.SnapshotAnimState(pAnimState,
											pOverlays ? pOverlays->GetSnapshot() : nullptr,
											nullptr,
											&animTable,
											pInfoCollection,
											pAnimData);
			pendingValidStateChange = stateSnapshot.m_animState.m_flags & DC::kAnimStateFlagLocomotionReady;

			/*
			MAIL_ASSERT(Animation,
						!pendingValidStateChange || LocomotionReadyRequirementsMet(m_pNavChar, &stateSnapshot.m_animState),
						("anim-state '%s' isn't actually locomotion-ready", stateSnapshot.m_animState.m_name.m_string.c_str()));
			*/
		}
	}

	// If we are being asked to resume navigation but we detect that we are not in one of the states that are valid
	// for handing over control we refuse the resume command and wait until next frame.
	if (curStateValid || pendingValidStateChange)
	{
		m_resumeStateRequested = true;
		LogHdr("Resume requested\n");
		/*
				if (INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController())
				{
					// force neutral facing since we don't necessarily know if we'll
					// be exit to moving or to idle
					// (and if we'll be moving, we don't know if it'll be strafing as well)
					pNavAnimController->UpdateDesiredFacingDirectionPs(GetLocalZ(m_pNavChar->GetRotationPs()));
				}
		*/
	}
	else
	{
		if (pChange && pChange->m_type == StateChangeRequest::kTypeFlagTransition)
		{
			LogHdr("Resume requested while in anim state '%s'. This is the wrong state but a transition exist. Aborting Resume and wait until next frame.\n",
				   DevKitOnly_StringIdToString(currentAnimState));
		}
		else
		{
			LogHdr("ERROR - Resume requested while in anim state '%s'. ABORTING RESUME\n",
				   DevKitOnly_StringIdToString(currentAnimState));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::AbandonInterruptedCommand()
{
	if (!m_pInterruptedState)
		return;

	if (!m_abandonInterruptedCommand)
	{
		LogHdr("AbandonInterruptedCommand()\n");
		m_abandonInterruptedCommand = true;
	}

	if (m_queuedCommand.IsValid())
	{
		LogHdr("AbandonInterruptedCommand also clearing queued command '%s'\n", m_queuedCommand.GetTypeName());

		m_queuedCommand.Reset();
	}

	if (m_nextContext.IsValid())
	{
		LogHdr("AbandonInterruptedCommand also clearing waiting-for-first-path command '%s'\n",
			   m_nextContext.m_command.GetTypeName());

		m_nextContext.Reset();
	}

	if (m_activeContext.IsInProgress())
	{
		SetContextStatus(m_activeContext, NavContextStatus::kFailed, "AbandonInterruptedCommand");
	}

	ClearPath();
	m_shouldUpdatePath = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::SetCurrentActionPack(const ActionPack* pAp, const char* strFuncName)
{
	const U32 curMgrId = m_hCurrentActionPack.GetMgrId();
	const U32 desMgrId = pAp ? pAp->GetMgrId() : ActionPackMgr::kInvalidMgrId;

	if (curMgrId == desMgrId)
	{
		ValidateActionPackUsage();
		return true;
	}

	switch (GetActionPackUsageState())
	{
	case kActionPackUsageReserved:
		LogHdr("SetCurrentActionPack[%s]: Releasing reserved AP\n", strFuncName);
		ReleaseActionPack();
		m_hCurrentActionPack = nullptr;
		break;
	case kActionPackUsageEntered:
		LogHdr("SetCurrentActionPack[%s]: Exiting entered AP\n", strFuncName);
		OnExitActionPack();
		ReleaseActionPack();
		m_hCurrentActionPack = nullptr;
		break;
	case kActionPackUsageNone:
		NAV_ASSERT(!m_hCurrentActionPack.IsValid());
		break;
	}

	bool success		 = false;
	m_hCurrentActionPack = pAp;

	if (pAp)
	{
		if (ReserveActionPack())
		{
			success = true;
		}
		else
		{
			m_hCurrentActionPack = nullptr;
		}
	}
	else
	{
		ReleaseActionPack();
		m_hCurrentActionPack = nullptr;
		success = true;
	}

	if (FALSE_IN_FINAL_BUILD(success))
	{
		LogHdr("SetCurrentActionPack[%s]: Setting current AP to ", strFuncName ? strFuncName : "<none>");
		LogActionPack(pAp);
		LogMsg("\n");
	}

	ValidateActionPackUsage();

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::SetNextActionPack(NavCommandContext& cmdContext,
										const ActionPack* pNextAp,
										const char* strFuncName)
{
	const ActionPack* pPrevNextAp = cmdContext.m_hNextActionPack.ToActionPack();

	const bool nextApChanged = pNextAp != pPrevNextAp;

	if (nextApChanged)
	{
		LogHdr("SetNextActionPack[%s]: Updating next AP for command %d to ", strFuncName, cmdContext.m_commandId);
		LogActionPack(pNextAp);
		LogMsg("\n");

		cmdContext.m_hNextActionPack = pNextAp;
	}

	return nextApChanged;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::ReserveActionPack()
{
	ActionPack* pActionPack = m_hCurrentActionPack.ToActionPack();

	NAV_ASSERT(pActionPack);

	if (FALSE_IN_FINAL_BUILD(m_actionPackUsageState != NavStateMachine::kActionPackUsageNone))
	{
		MailNpcLogTo(m_pNavChar, "john_bellomy@naughtydog.com", "Double-Reserving AP", FILE_LINE_FUNC);
	}

	NAV_ASSERT(m_actionPackUsageState == NavStateMachine::kActionPackUsageNone);

	bool success = false;

	// Try to reserve the action pack.
	if (pActionPack && pActionPack->IsAvailableFor(m_pNavChar))
	{
		// we need to remember the type of action pack so we can call the right controller's Exit() if it goes away
		m_actionPackUsageType = pActionPack->GetType();

		// Special case: Taps have a special way of being reserved
		if (TraversalActionPack* pTap = TraversalActionPack::FromActionPack(pActionPack))
		{
			if (pTap->ShouldDoBriefReserve())
			{
				// reservation might still fail because of concurrency woes
				success = pTap->BriefReserve(m_pNavChar);
			}
			else
			{
				// reservation might still fail because of concurrency woes
				success = pTap->Reserve(m_pNavChar);
			}
		}
		else
		{
			// reservation might still fail because of concurrency woes
			success = pActionPack->Reserve(m_pNavChar);
		}
	}

	if (success)
	{
		NAV_ASSERT(pActionPack && (pActionPack->IsReservedBy(m_pNavChar)));

		// Make this action pack the current action pack by memorizing all information.
		m_actionPackUsageState = kActionPackUsageReserved;

		LogHdr("Reserving Action Pack (");
		LogActionPack(pActionPack);
		LogMsg(")\n");
	}
	else if (pActionPack && pActionPack->IsReservedBy(m_pNavChar) && !pActionPack->HasUnboundedReservations())
	{
		LogHdr("FAILED to Reserve Action Pack (");
		LogActionPack(pActionPack);
		LogMsg(") but I'm still holding reservation (maybe it became blocked) releasing\n");

		pActionPack->Release(m_pNavChar);
	}
	else
	{
		LogHdr("FAILED to Reserve Action Pack (");
		LogActionPack(pActionPack);
		LogMsg(")\n");
	}

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::ReleaseActionPack()
{
	if (m_actionPackUsageState != kActionPackUsageNone)
	{
		ActionPack* pAp = m_hCurrentActionPack.ToActionPack();

		LogHdr("ReleaseActionPack - ActionPack (");

		if (!pAp)
		{
			LogMsg("<null>)\n");
		}
		else if (pAp->IsReservedBy(m_pNavChar))
		{
			LogActionPack(pAp);
			LogMsg(")\n");

			pAp->Release(m_pNavChar);
		}
		else
		{
			LogActionPack(pAp);
			LogMsg(") but I'm not the reservation holder, so doing nothing\n");
		}
	}

	m_actionPackUsageState = kActionPackUsageNone;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::BeginEnterActionPack(ActionPack* pActionPack, const char* strFuncName)
{
	ValidateActionPackUsage();

	NAV_ASSERT(pActionPack);

	if (GetActionPackUsageState() != kActionPackUsageReserved)
	{
		LogHdr("BeginEnterActionPack[%s]: Called on an AP we haven't reserved!\n", strFuncName);

		MailNpcLogTo(m_pNavChar,
					 "john_bellomy@naughtydog.com",
					 "BeginEnterActionPack called on an action pack that we haven't reserved!",
					 FILE_LINE_FUNC);
	}

	// if we are ready to enter the action pack,
	NAV_ASSERT(GetActionPackUsageState() == kActionPackUsageReserved);

	const ActionPack::Type apUsageType = GetActionPackUsageType();
	ActionPackController* pActionPackController = m_pAnimationControllers->GetControllerForActionPackType(apUsageType);

	const ActionPackResolveInput apResolveInput = MakeApResolveInput(m_activeContext.m_command, pActionPack);

	if (!m_activeContext.m_actionPackEntry.IsValidFor(pActionPack))
	{
		m_activeContext.m_actionPackEntry = ActionPackEntryDef();

		LogHdr("BeginEnterActionPack[%s]: Moving to default entry without a valid item, trying a last minute default resolve\n",
			   strFuncName);

		pActionPackController->ResolveDefaultEntry(apResolveInput, pActionPack, &m_activeContext.m_actionPackEntry);
	}

	NAV_ASSERT((m_activeContext.m_actionPackEntry.m_hResolvedForAp.GetMgrId() == ActionPackMgr::kInvalidMgrId)
			   || m_activeContext.m_actionPackEntry.IsValidFor(pActionPack));

	LogHdr("BeginEnterActionPack[%s]: ", strFuncName);
	LogActionPack(pActionPack);
	if (m_activeContext.m_actionPackEntry.m_apUserData)
	{
		LogMsg(" <%d>", m_activeContext.m_actionPackEntry.m_apUserData);
	}
	LogMsg("\n");

	// hacky but we want the tap controller to be able to determine the exit animation immediately
	if (apUsageType == ActionPack::kTraversalActionPack)
	{
		LogHdr("Activating Post-TAP path with %d waypoints\n", m_pathStatus.m_postTapPathPs.GetWaypointCount());

		PathWaypointsEx newPathPs	  = m_pathStatus.m_postTapPathPs;
		const Locator tapAnimAdjustLs = m_pathStatus.m_tapAnimAdjustLs;

		ClearPath();

		m_pathStatus.m_kickedCommandId	  = m_activeContext.m_commandId;
		m_pathStatus.m_processedCommandId = m_activeContext.m_commandId;

		m_pathStatus.m_pathPs = newPathPs;
		m_currentPathPs		  = newPathPs;
		m_pathStatus.m_tapAnimAdjustLs = tapAnimAdjustLs;
		m_shouldUpdatePath = true;
	}
	else
	{
		ClearPath();

		m_shouldUpdatePath = false;
	}

	pActionPackController->Enter(apResolveInput, pActionPack, m_activeContext.m_actionPackEntry);

	// enter action pack
	OnEnterActionPack(pActionPack);

	// Reached the goal action pack
	if (pActionPack == GetGoalActionPack())
	{
		LogHdr("BeginEnterActionPack[%s]: reached goal action pack\n", strFuncName);
	}
	// Reached a traversal action pack
	else
	{
		if (apUsageType != ActionPack::kTraversalActionPack)
		{
			LogHdr("BeginEnterActionPack[%s] called on an action pack that isn't our goal AP, but isn't a TAP either! [%s] [goal: %s] [apUsageType: %s]\n",
				   strFuncName,
				   pActionPack->GetName(),
				   GetGoalActionPack() ? GetGoalActionPack()->GetName() : "<null>",
				   ActionPack::GetTypeName(apUsageType));
			MailNpcLogTo(m_pNavChar,
						 "john_bellomy@naughtydog.com",
						 "BeginEnterActionPack called on an action pack that isn't our goal AP, but isn't a TAP either!",
						 FILE_LINE_FUNC);
		}

		NAV_ASSERT(apUsageType == ActionPack::kTraversalActionPack);
	}

	if (apUsageType == ActionPack::kTraversalActionPack)
	{
		EnterTraversalActionPack();

		if (m_activeContext.IsInProgress() && (pActionPack == GetGoalActionPack()))
		{
			SetContextStatus(m_activeContext, NavContextStatus::kSucceeded, "Reached goal TAP");
		}
	}
	else
	{
		GoEnteringActionPack();
	}

	ValidateActionPackUsage();

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::OnEnterActionPack(const ActionPack* pActionPack)
{
	NAV_ASSERT(pActionPack);
	NAV_ASSERT(pActionPack == m_hCurrentActionPack.ToActionPack());
	NAV_ASSERT(m_actionPackUsageState == kActionPackUsageReserved);

	if (!pActionPack)
		return;

	const ActionPack::Type apType = GetActionPackUsageType();

	NAV_ASSERT(pActionPack->GetType() == apType);

	LogHdr("OnEnterActionPack: usage state -> entered\n");
	m_actionPackUsageState = kActionPackUsageEntered;

	switch (apType)
	{
	case ActionPack::kCoverActionPack:
		{
			const CoverActionPack* pCoverAp = (const CoverActionPack*)pActionPack;
			m_pNavChar->SetTakingCover(true);
			m_pNavChar->SetTakingCoverDirectionWs(pCoverAp->GetCoverDirectionWs());
			m_pNavChar->SetTakingCoverAngle(75.0f, 85.0f); // magic numbers!!!
		}
		break;

	case ActionPack::kTurretActionPack:
		{
			m_pNavChar->SetTakingCover(true);
			m_pNavChar->SetTakingCoverDirectionWs(GetLocalZ(pActionPack->GetLocatorWs().Rot()));
			m_pNavChar->SetTakingCoverAngle(45.0f);
		}
		break;

	default:
		m_pNavChar->SetTakingCover(false);
		break;
	}

	m_pAnimationControllers->InterruptNavControllers();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::OnExitActionPack()
{
	if (FALSE_IN_FINAL_BUILD(!IsActionPackUsageEntered()))
	{
		LogHdr("Called OnExitActionPack() but usage state is actually '%s' !\n",
			   GetActionPackUsageStateName(GetActionPackUsageState()));
		MailNpcLogTo(m_pNavChar,
					 "john_bellomy@naughtydog.com",
					 "Called OnExitActionPack() but usage state isn't Entered",
					 FILE_LINE_FUNC);
	}

	NAV_ASSERT(IsActionPackUsageEntered());

	if (m_pNavChar->GetTakingCover())
	{
		m_pNavChar->SetTakingCover(false);
	}

	if (m_activeContext.m_actionPackEntry.IsValidFor(m_hCurrentActionPack))
	{
		m_activeContext.m_actionPackEntry = ActionPackEntryDef();
	}

	// Clean up after ourselves.
	ExitTraversalActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::EnterTraversalActionPack()
{
	TraversalActionPack* pTap = GetTraversalActionPack();

	NAV_ASSERT(pTap);
	if (!pTap || !pTap->IsEnabled())
	{
		LogHdr("EnterTraversalActionPack() - AP 0x%.8x Disappeared! Turned Invalid: '%s'\n",
			   m_hCurrentActionPack.GetMgrId(),
			   m_hCurrentActionPack.HasTurnedInvalid() ? "YES" : "no");

		MailNpcLogTo(m_pNavChar,
					 "john_bellomy@naughtydog.com",
					 "EnterTraversalActionPack() - TAP disappeared!",
					 FILE_LINE_FUNC);

		StopPathFailed(m_activeContext, "EnterTraversalActionPack");
		return;
	}

	NAV_ASSERT(pTap->GetType() == ActionPack::kTraversalActionPack);
	NAV_ASSERT(GetActionPackUsageType() == ActionPack::kTraversalActionPack);
	LogHdr("EnterTraversalActionPack (");
	LogActionPack(pTap);
	LogMsg(")\n");

	if (ActionPackMutex* pMutex = pTap->GetMutex())
	{
		const bool enabled = pMutex->TryEnable(pTap, m_pNavChar);

		if (FALSE_IN_FINAL_BUILD(!enabled))
		{
			LogHdr("Failed to enter TAP!\n");
			MailNpcLogTo(m_pNavChar, "john_bellomy@naughtydog.com", "Failed to enter TAP", FILE_LINE_FUNC);
		}

		NAV_ASSERTF(enabled,
					("EnterTraversalActionPack: Unable to enable TAP mutex - Should have been tested in IsAvailable call in ReserveActionPack"));
	}

	pTap->AddUser(m_pNavChar);
	pTap->ResetReadyToUseTimer();

	SetNextWaypoint(m_activeContext,
					m_activeContext.m_finalNavGoal,
					kWaypointFinalGoal,
					m_activeContext.m_command.GetGoalReachedType(),
					"EnterTraversalActionPack");

	SetNextActionPack(m_activeContext, m_activeContext.m_command.GetGoalActionPack(), "EnterTraversalActionPack");

	GoUsingTraversalActionPack();
	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::ExitTraversalActionPack()
{
	TraversalActionPack* pTap = GetTraversalActionPack();

	if (!pTap)
		return;

	LogHdr("ExitTraversalActionPack (");
	LogActionPack(pTap);
	LogMsg(")\n");

	if (m_actionPackUsageState == kActionPackUsageEntered)
	{
		if (pTap->IsSingleUse())
		{
			pTap->ResetReadyToUseTimer();
			pTap->Enable(false);
		}

		pTap->RemoveUser(m_pNavChar);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::ResumeMoving(bool needNewPath, bool ignorePendingCommand)
{
	LogHdr("ResumeMoving: needNewPath: %s ignorePendingCommand: %s\n",
		   needNewPath ? "true" : "false",
		   ignorePendingCommand ? "true" : "false");

	// If a new command was given we want to do that instead of resuming something old...
	if (!ignorePendingCommand && IsCommandPending())
	{
		SetNextWaypoint(m_activeContext,
						m_activeContext.m_finalNavGoal,
						kWaypointFinalGoal,
						m_activeContext.m_command.GetGoalReachedType(),
						"ResumeMoving(2)");

		m_shouldUpdatePath = needNewPath;

		GoStopped();
		GetStateMachine().TakeStateTransition();
		return;
	}

	if (!m_activeContext.IsValid() || (m_activeContext.m_status == NavContextStatus::kFailed))
	{
		StopMovingImmediately(true, false, "Resume Moving Failsafe");
		return;
	}

	if (!m_currentPathPs.IsValid() || needNewPath)
	{
		ClearPath();

		// this should force us to re-resolve any entries that we might've otherwise assumed we could use
		// (or think we didn't need to but end up pathing to a point that wasn't our *real* animated entry point)
		m_activeContext.m_actionPackEntry = ActionPackEntryDef();

		m_shouldUpdatePath = true;

		SetContextStatus(m_activeContext, NavContextStatus::kWaitingForPath, "ResumeMoving");

		const ActionPack* pGoalAp = m_activeContext.m_command.GetGoalActionPack();

		// reset next goal to our final and kick a new path
		SetNextWaypoint(m_activeContext,
						m_activeContext.m_finalNavGoal,
						pGoalAp ? kWaypointAPDefaultEntry : kWaypointFinalGoal,
						m_activeContext.m_command.GetGoalReachedType(),
						"ResumeMoving");

		SetNextActionPack(m_activeContext, pGoalAp, "ResumeMoving-NewPath");

		m_pNavChar->SetRequestedMotionType(m_activeContext.m_command.GetMotionType());

		GoResuming();
	}
	else
	{
		INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController();

		switch (m_activeContext.m_command.GetType())
		{
		case NavCommand::kStopAndStand:
			StopMovingImmediately(true, true, "Resume Moving");
			break;

		case NavCommand::kMoveToLocation:
		case NavCommand::kMoveToActionPack:
		case NavCommand::kMoveAlongSpline:
			if (pNavAnimController)
			{
				m_shouldUpdatePath = true;

				NavAnimStartParams params;
				params.m_pPathPs = &m_currentPathPs;
				params.m_waypoint = GetWaypointContract(m_activeContext);

				m_activeContext.GetEffectiveMoveArgs(params.m_moveArgs);

				LogHdr("ResumeMoving: NavAnimController::StartNavigating(%s, %s)\n",
						GetMotionTypeName(m_activeContext.m_command.GetMotionType()),
						GetGoalReachedTypeName(params.m_waypoint.m_reachedType));

				m_pNavChar->SetRequestedMotionType(params.m_moveArgs.m_motionType);

				if (params.m_moveArgs.m_motionType >= kMotionTypeMax)
				{
					MailNpcLogTo(m_pNavChar,
									"john_bellomy@naughtydog.com",
									"Started moving without motion type",
									FILE_LINE_FUNC);
				}

				pNavAnimController->StartNavigating(params);

				GoMovingAlongPath();
				GetStateMachine().TakeStateTransition();
			}
			else
			{
				StopPathFailed(m_activeContext, "ResumeMoving: No nav controller");
			}
			break;

		case NavCommand::kSteerToLocation:
			if (pNavAnimController)
			{
				m_shouldUpdatePath = true;

				NavAnimStartParams startParams;
				startParams.m_moveDirPs = m_currentPathDirPs;
				startParams.m_waypoint = GetWaypointContract(m_activeContext);
				startParams.m_moveArgs = m_activeContext.m_command.GetMoveArgs();

				m_pNavChar->SetRequestedMotionType(startParams.m_moveArgs.m_motionType);

				pNavAnimController->StartNavigating(startParams);

				GoSteeringAlongPath();
				GetStateMachine().TakeStateTransition();
			}
			else
			{
				StopPathFailed(m_activeContext, "ResumeMoving: No nav controller");
			}
			break;

		case NavCommand::kMoveInDirection:
			if (pNavAnimController)
			{
				NavAnimStartParams startParams;
				startParams.m_moveDirPs = m_activeContext.m_command.GetMoveDirPs();
				startParams.m_waypoint = GetWaypointContract(m_activeContext);
				startParams.m_moveArgs = m_activeContext.m_command.GetMoveArgs();

				m_pNavChar->SetRequestedMotionType(startParams.m_moveArgs.m_motionType);

				pNavAnimController->StartNavigating(startParams);

				SetContextStatus(m_activeContext, NavContextStatus::kSucceeded, "ResumeMovingInDirection");

				GoMovingBlind();
				GetStateMachine().TakeStateTransition();
			}
			else
			{
				StopPathFailed(m_activeContext, "ResumeMoving: No nav controller (2)");
			}
			break;
		}
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::IsDynamicallyClear(NavLocation loc) const
{
	bool isClear = false;

	if (const NavControl* pNavControl = m_pNavChar->GetNavControl())
	{
		isClear = !pNavControl->IsNavLocationBlocked(loc, true);
	}

	return isClear;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::DetectRepathOrPathFailure()
{
	bool pathFailure = false;
	const MotionConfig& motionConfig  = m_pNavChar->GetMotionConfig();
	const float pathIssuedTimeElapsed = ToSeconds(m_pNavChar->GetCurTime() - m_pathStatus.m_lastPathIssuedTime);

	NavCommandContext* pCmdContext = GetCommandContext(m_pathStatus.m_kickedCommandId);

	// detect if we should trigger a refresh of the path finding (also detect if path finding has failed)
	// Stop if we don't have enough information in the dynamic path...
	// We can't walk around in the dark... ;-)
	if (!pCmdContext && (m_pathStatus.m_kickedCommandId != kInvalidCommandId))
	{
		StopPathFailed(m_activeContext, "Path Command Context Disappeared (?!)");
		pathFailure = true;
	}
	else if (m_pathStatus.m_pathUpdated && !m_pathStatus.m_pathFound)
	{
		// if path failed, command fails
		StopPathFailed(*pCmdContext, "DetectRepathOrPathFailure: no path found");
		pathFailure = true;
	}
	else if (IsMovingToWaypoint() && !m_currentPathPs.IsValid()
			 && (m_pathStatus.m_kickedCommandId == m_activeContext.m_commandId))
	{
		// if path failed, command fails
		StopPathFailed(m_activeContext, "DetectRepathOrPathFailure: current path disappeared");
		pathFailure = true;
	}
	else if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_enablePathFailTesting))
	{
		StopPathFailed(*pCmdContext, "Forced Path Fail");
		pathFailure = true;
	}
	else if (m_pathStatus.m_suspended)
	{
	}
	else if (pathIssuedTimeElapsed > 0.5f)
	{
		m_shouldUpdatePath = true;
	}

	const bool waitingForNewPath = m_shouldUpdatePath && !m_pathStatus.m_suspended;

	return waitingForNewPath || pathFailure;
}

/// --------------------------------------------------------------------------------------------------------------- ///
TimeFrame NavStateMachine::GetStallTimeElapsed() const
{
	TimeFrame stallTime = Seconds(0.0f);

	if ((m_activeContext.m_nextWaypointType == kWaypointAPWait) && IsState(kStateStopped))
	{
		if (const BaseState* pCurState = GetState())
		{
			stallTime = m_pNavChar->GetCurTime() - pCurState->GetStateStartTime();
		}
	}

	return stallTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavCommand::Status NavStateMachine::GetStatus() const
{
	if (m_queuedCommand.IsValid())
	{
		return NavCommand::kStatusCommandPending;
	}

	if (m_nextContext.IsValid())
	{
		if (m_nextContext.IsInProgress())
			return NavCommand::kStatusCommandInProgress;

		return NavCommand::kStatusCommandPending;
	}

	if (m_activeContext.IsValid())
	{
		switch (m_activeContext.m_status)
		{
		case NavContextStatus::kWaitingForPath:
		case NavContextStatus::kActive:
			return NavCommand::kStatusCommandInProgress;

		case NavContextStatus::kFailed:
			return NavCommand::kStatusCommandFailed;
		case NavContextStatus::kSucceeded:
			return NavCommand::kStatusCommandSucceeded;

		case NavContextStatus::kTerminated:
		case NavContextStatus::kInvalid:
			break;
		}
	}

	return NavCommand::kStatusAwaitingCommands;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsPlayerBlockingLineOfMotion(Point_arg startPosWs, Point_arg endPosWs, float thresholdDist = 0.5f)
{
	const NdGameObject* pPlayer = EngineComponents::GetNdGameInfo()->GetPlayerGameObject();
	if (!pPlayer)
		return false;

	const Point playerPosWs = GetProcessSnapshot<CharacterSnapshot>(pPlayer)->GetTranslation();
	const Scalar dist		= DistPointSegment(playerPosWs, startPosWs, endPosWs);
	if (dist > thresholdDist)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::CanProcessCommands() const
{
	if (m_queuedCommand.IsValid() && m_queuedCommand.PathFindRequired())
	{
		if (IsFirstPathForCommandPending())
		{
			return false;
		}

		if (m_nextContext.m_status == NavContextStatus::kWaitingForPath)
		{
			if (m_shouldUpdatePath || m_hNavPathJob.IsValid())
			{
				return false;
			}
		}
	}

	bool canProcess = false;

	if (const BaseState* pCurState = GetState())
	{
		canProcess = pCurState->CanProcessCommands();
	}

	return canProcess;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugPathFindBlocker(const NavCharacter* pNavChar,
								 const DynamicNavBlocker* pBlocker,
								 Color clr,
								 const char* strReason = nullptr,
								 DebugPrimTime tt = kPrimDuration1FrameAuto)
{
	STRIP_IN_FINAL_BUILD;

	const ProcessHandle hBlockerOwner = pBlocker->GetOwner();
	const NdLocatableObject* pBlockerLocatable = NdLocatableObject::FromProcess(hBlockerOwner.ToProcess());
	const Locator& parentSpace = pBlockerLocatable ? pBlockerLocatable->GetParentSpace() : kIdentity;
	const Point posWs = parentSpace.TransformPoint(pBlocker->GetPosPs());

	pBlocker->DebugDrawShape(clr, tt);

	Color clrA = clr;
	clrA.SetA(0.3f * clr.A());

	const Point myPosWs = pNavChar->GetTranslation() + Vector(0.0f, 0.3f, 0.0f);

	g_prim.Draw(DebugLine(myPosWs, posWs, clrA, clr, 3.0f, kPrimEnableHiddenLineAlpha), tt);

	if (strReason && DebugSelection::Get().IsProcessSelected(pNavChar))
	{
		g_prim.Draw(DebugString(posWs, strReason, clr, 0.6f), tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
bool NavStateMachine::NsmPathFindBlockerFilter(const SimpleNavControl* pNavControl,
											   const DynamicNavBlocker* pBlocker,
											   BoxedValue userData,
											   bool forPathing,
											   bool debug)
{
	const NavCharacter* pNavChar = NavCharacter::FromProcess(userData.GetProcess());

	if (!pBlocker)
	{
		return false;
	}

	if (pNavControl->IsNavBlockerIgnoredFromFilter(pBlocker, forPathing, debug))
	{
		if (FALSE_IN_FINAL_BUILD(debug && (pBlocker->GetOwner().ToProcess() != pNavChar)))
		{
			DebugPathFindBlocker(pNavChar, pBlocker, kColorRed, "Filtered by NavControl");
		}
		return false;
	}

	AI_ASSERT(pNavChar);
	const ProcessHandle hBlockerOwner = pBlocker->GetOwner();

	const bool isPlayer = EngineComponents::GetNdGameInfo()->IsPlayerHandle(hBlockerOwner);
	if (isPlayer && pNavChar->ShouldPathThroughPlayer())
	{
		if (FALSE_IN_FINAL_BUILD(debug))
		{
			DebugPathFindBlocker(pNavChar, pBlocker, kColorRed, "ShouldPathThroughPlayer");
		}
		return false;
	}

	const Process* pBlockerOwner = hBlockerOwner.ToProcess();
	const Locator& parentSpace = pNavChar->GetParentSpace();

	if (const NavCharacter* pBlockerNavChar = NavCharacter::FromProcess(pBlockerOwner))
	{
		const NavCharacterSnapshot* pBlockerSnapshot = GetProcessSnapshot< NavCharacterSnapshot>(pBlockerNavChar);
		const Locator& theirParentSpace = pBlockerNavChar->GetParentSpace();

		const Point myPosWs = pNavChar->GetTranslation();
		const Point theirPosWs = pBlockerNavChar->GetTranslation();

		const float distToChar = Dist(myPosWs, theirPosWs);

		if (distToChar > g_navCharOptions.m_obeyedNpcPathBlockerMaxDist)
		{
			if (FALSE_IN_FINAL_BUILD(debug))
			{
				DebugPathFindBlocker(pNavChar, pBlocker, kColorRed, "Too Far");
			}
			return false;
		}

		const Vector myDirPs = AsUnitVectorXz(pNavChar->GetPathDirPs(), kZero);
		const Vector myVelocityPs = pNavChar->GetVelocityPs();
		const Vector theirVelOtherPs = pBlockerNavChar->GetVelocityPs();
		const Vector theirVelocityPs = parentSpace.UntransformVector(theirParentSpace.TransformVector(theirVelOtherPs));

		Scalar theirSpeedXz;
		const Vector theirDirPs = AsUnitVectorXz(theirVelocityPs, kZero, theirSpeedXz);

		const INavAnimController* pNavController = pNavChar->GetActiveNavAnimController();
		const INavAnimController* pTheirNavController = pBlockerNavChar->GetActiveNavAnimController();

		const float mySpeedXz = pNavController->GetMaxMovementSpeed();
		theirSpeedXz = Max(theirSpeedXz, pTheirNavController->GetMaxMovementSpeed());

		if (pBlockerSnapshot->m_followingPath && pBlockerSnapshot->m_currentPathPs.IsValid())
		{
			const Point myPosTheirPs = theirParentSpace.UntransformPoint(myPosWs);

			float distToTheirPath = kLargeFloat;
			pBlockerSnapshot->m_currentPathPs.ClosestPoint(myPosTheirPs, &distToTheirPath);

			const float blockerRadius = pBlocker->GetBoundingRadius();
			const float pfRadius = pNavControl->GetEffectivePathFindRadius();
			const float combinedRadius = blockerRadius + pfRadius + 0.01f;

			const bool imGoingFaster = (theirSpeedXz + 0.5f) < mySpeedXz;

			if ((distToTheirPath > combinedRadius) && !imGoingFaster && (distToChar > combinedRadius))
			{
				if (FALSE_IN_FINAL_BUILD(debug))
				{
					DebugPathFindBlocker(pNavChar, pBlocker, kColorRed, "Far from their path");
				}
				return false;
			}
		}
		else if (pBlockerSnapshot->m_interruptedOrMovingBlind)
		{
			// animation controlled or blind moving, so just infer their desired move dir
			const bool imMoving = pNavChar->IsMoving();
			const bool theyreMoving = (Length(theirVelocityPs) > 0.5f) || pBlockerNavChar->IsMoving();

			if (imMoving)
			{
				if (theyreMoving)
				{
					const float moveAngleDiffRad = SafeAcos(DotXz(myDirPs, theirDirPs));
					const float moveAngleDiffDeg = RADIANS_TO_DEGREES(moveAngleDiffRad);

					const bool theyreGoingFaster = (mySpeedXz + 0.5f) < theirSpeedXz;

					if (moveAngleDiffDeg <= g_navCharOptions.m_obeyedNpcPathBlockerSameDirDeg && theyreGoingFaster)
					{
						if (FALSE_IN_FINAL_BUILD(debug))
						{
							DebugPathFindBlocker(pNavChar, pBlocker, kColorRed, "Moving in same direction");
						}
						return false;
					}
				}
			}
			else if (theyreMoving)
			{
				if (distToChar > g_navCharOptions.m_obeyedNpcPathBlockerStandingDist)
				{
					if (FALSE_IN_FINAL_BUILD(debug))
					{
						DebugPathFindBlocker(pNavChar, pBlocker, kColorRed, "Moving while I'm not");
					}

					// i'm standing still, ignore moving npcs
					return false;
				}
			}
		}
	}
	else if (const Character* pBlockerChar = Character::FromProcess(pBlockerOwner))
	{
		// non-npc character object... most likely the player
		if (pBlockerChar->CanBePushedByNpc())
		{
			const NavStateMachine* pNsm	 = pNavChar->GetNavStateMachine();

			const CharacterSnapshot* pBlockingCharSnapshot = GetProcessSnapshot<CharacterSnapshot>(pBlockerChar);

			const NavCommandContext* pCmdContext = pNsm->GetCommandContext(pNsm->m_pathStatus.m_kickedCommandId);

			if (pCmdContext && pCmdContext->IsInProgress() && pCmdContext->m_command.GetMoveArgs().m_ignorePlayerOnGoal
				&& isPlayer && pNsm->IsCommandInProgress())
			{
				const NavLocation& finalGoal = pCmdContext->m_finalNavGoal;
				const Point finalPosWs		 = finalGoal.GetPosWs();
				if (Dist(finalPosWs, pBlockingCharSnapshot->GetTranslation()) < NavMoveArgs::kIgnorePlayerOnGoalRadius)
				{
					if (FALSE_IN_FINAL_BUILD(debug))
					{
						DebugPathFindBlocker(pNavChar, pBlocker, kColorRed, "Ignored player on goal");
					}

					return false;
				}
			}

			const Vector myVelocityPs = pNavChar->GetVelocityPs();
			const Vector myDirPs	  = SafeNormalize(myVelocityPs, kZero);

			Vector theirVelWs = pBlockingCharSnapshot->GetParentSpace().TransformVector(pBlockingCharSnapshot->GetVelocityPs());

			if (isPlayer)
			{
				if (pBlockingCharSnapshot->GetStickSpeed(NdPlayerJoypad::kStickMove) <= (pBlockingCharSnapshot->IsSprinting() ? 0.0f : 0.5f))
				{
					theirVelWs = kZero;
				}
				else
				{
					const Vector stickWs = pBlockingCharSnapshot->GetStickWS(NdPlayerJoypad::kStickMove);
					theirVelWs = theirVelWs * Dot(theirVelWs, stickWs);
				}
			}

			const Vector theirVelPs = parentSpace.UntransformVector(theirVelWs);
			const Vector theirDirPs = SafeNormalize(theirVelPs, kZero);
			const float theirSpeedPs = LengthXz(theirVelPs);
			const float mySpeedPs = LengthXz(myVelocityPs);

			if ((DotXz(myDirPs, theirDirPs) > 0.5f) && (theirSpeedPs > 2.0f) && (mySpeedPs > 0.5f))
			{
				if (FALSE_IN_FINAL_BUILD(debug))
				{
					DebugPathFindBlocker(pNavChar, pBlocker, kColorRed, "Moving in the same dir");
				}

				return false;
			}
		}
	}

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		DebugPathFindBlocker(pNavChar, pBlocker, kColorGreen);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::BeginPathFind()
{
	PROFILE_AUTO(Navigation);

	NavControl* pNavCon = m_pNavChar->GetNavControl();
	if (!pNavCon)
		return;

	AtomicLockJanitor lock(&m_pathResultsLock, FILE_LINE_FUNC);

	const Locator& curLocPs	   = m_pNavChar->GetLocatorPs();
	const Locator& parentSpace = m_pNavChar->GetParentSpace();
	const U32F baseTraversalSkillMask = pNavCon->GetTraversalSkillMask();

	NavLocation startLoc = pNavCon->GetNavLocation();
	ActionPackHandle hPreferredTap = nullptr;
	float tapBias = 0.0f;

	const bool startLocValid = GetPathFindStartLocation(&startLoc, &hPreferredTap, &tapBias);

	m_hNavPathJob.WaitForJob();

	if (!m_shouldUpdatePath || !startLocValid)
	{
		return;
	}

	NavCommandContext& cmdContext = m_nextContext.IsValid() ? m_nextContext : m_activeContext;
	const NavCommand& cmd		  = cmdContext.m_command;

	if (!cmd.PathFindRequired() || !cmdContext.IsValid())
	{
		LogHdrDetails("BeginPathFind: Ignoring requested path find for command (%s) because it's not needed\n",
					  GetWaypointTypeName(cmdContext.m_nextWaypointType));
		m_shouldUpdatePath = false;
		return;
	}

	const U32F allowedTraversalSkillMask = FALSE_IN_FINAL_BUILD(g_navCharOptions.m_forceNoNavTaps)
											   ? 0
											   : cmd.GetMoveArgs().m_allowedTraversalSkillMask;

	const U32F traversalSkillMask = baseTraversalSkillMask & allowedTraversalSkillMask;

	if (FALSE_IN_FINAL_BUILD(traversalSkillMask != baseTraversalSkillMask))
	{
		const U32F lostSkillMask = baseTraversalSkillMask & ~traversalSkillMask;
		StringBuilder<256> skills;
		StringBuilder<256> lostSkills;
		if (0 == traversalSkillMask)
		{
			skills.append("NONE!");
			lostSkills.format("ALL!");
		}
		else if (g_ndConfig.m_pGetTraversalSkillMaskString)
		{
			(*g_ndConfig.m_pGetTraversalSkillMaskString)(traversalSkillMask, &skills);
			(*g_ndConfig.m_pGetTraversalSkillMaskString)(lostSkillMask, &lostSkills);
		}
		else
		{
			skills.clear();
			lostSkills.clear();
		}

		LogHdr("Using non-standard traversal skills: %s (lost: %s) [0x%x]\n",
			   skills.c_str(),
			   lostSkills.c_str(),
			   traversalSkillMask);
	}

	NavLocation goalLoc = cmdContext.m_nextNavGoal;

	if (cmdContext.m_nextWaypointType == kWaypointFinalGoal)
	{
		goalLoc = cmdContext.m_finalNavGoal;
	}

	// so that NsmPathFindBlockerFilter can get the correct command context
	m_pathStatus.m_kickedCommandId = cmdContext.m_commandId;

	const Character* pChaseChar = cmd.GetMoveArgs().m_hChaseChar.ToProcess();
	const PathfindRequestHandle hChasePathReq = pChaseChar ? pChaseChar->GetPathfindRequestHandle()
														   : PathfindRequestHandle();

	if (hChasePathReq.IsValid() && (cmdContext.m_nextWaypointType == kWaypointFinalGoal)
		&& !IsState(kStateUsingTraversalActionPack) && g_navCharOptions.m_enableChasePathFinds)
	{
		LogHdrDetails("BeginPathFind: Doing Chase Path Find for command %d (%s) from ",
					  cmdContext.m_commandId,
					  GetWaypointTypeName(cmdContext.m_nextWaypointType));
		LogNavLocationDetails(startLoc);
		LogMsgDetails("\n");

		//const U64 startTime = TimerGetRawCount();

		PathfindManager& pfMgr = PathfindManager::Get();

		const Nav::FindUndirectedPathsParams* pFindPathParams = nullptr;
		pfMgr.GetParams(hChasePathReq, &pFindPathParams);

		Nav::BuildPathParams buildParams;
		buildParams.m_smoothPath = Nav::kFullSmoothing;
		buildParams.m_finalizePathWithProbes = true;
		buildParams.m_finalizeProbeMaxDist = 5.0f;
		buildParams.m_reversePath = true;
		buildParams.m_truncateAfterTap = 2;
		//buildParams.m_wideTapInstanceSeed = (m_pNavChar->GetUserId().GetValue() * m_pNavChar->GetProcessId()) ^ m_pNavChar->GetProcessId();
		buildParams.m_wideTapInstanceSeed = m_pNavChar->GetProcessId();

		Nav::PathFindContext contextOverride;

		if (pFindPathParams)
		{
			contextOverride = pFindPathParams->m_context;
		}

		contextOverride.m_ownerLocPs  = m_pNavChar->GetLocatorPs();
		if (const ActionPack* const pReservedAp = m_pNavChar->GetReservedActionPack())
			contextOverride.m_ownerReservedApMgrId = pReservedAp->GetMgrId();

		if (m_pNavChar->IsKindOf(SID("Horse")))
			contextOverride.m_isHorse = true;

		contextOverride.m_parentSpace = m_pNavChar->GetParentSpace();
		contextOverride.m_pathRadius  = GetPathFindRadiusForCommand(cmdContext);

		buildParams.m_pContextOverride = &contextOverride;

/*
		if (DebugSelection::Get().IsProcessSelected(m_pNavChar))
		{
			buildParams.m_debugDrawPortals = true;
			buildParams.m_debugDrawTime = Seconds(1.5f);
		}
*/

		Nav::BuildPathResults buildResults;
		pfMgr.BuildPath(hChasePathReq, buildParams, startLoc, &buildResults);

		if (buildResults.m_goalFound && buildResults.m_pathWaypointsPs.IsValid())
		{
/*
			if (DebugSelection::Get().IsProcessSelected(m_pNavChar))
			{
				buildResults.m_pathWaypointsPs.DebugDraw(contextOverride.m_parentSpace, true, 0.0f, PathWaypointsEx::ColorScheme(), Seconds(1.5f));
			}
*/

			NavControl::PathFindResults results;
			Nav::SplitPathAfterTap(buildResults.m_pathWaypointsPs, &results.m_pathPs, &results.m_postTapPathPs);

			if (results.m_pathPs.IsValid() && !results.m_postTapPathPs.IsValid())
			{
				// not split after TAP?
				// truncate tail at desired distance before chase character
				const F32 distBeforeChaseChar = cmd.GetMoveArgs().m_distBeforeChaseChar;
				if (distBeforeChaseChar > 0.0f)
				{
					PathWaypointsEx& pathPs = results.m_pathPs;

					const U32 numWaypoints = pathPs.GetWaypointCount();
					const F32 fullPathLen = pathPs.ComputePathLength();
					const F32 desiredPathLen = Max(0.0f, fullPathLen - distBeforeChaseChar);

					F32 currAccumulatedPathLen = 0.0f;
					F32 prevAccumulatedPathLen = 0.0f;
					Point prevWaypoint = pathPs.GetWaypoint(0);
					for (I32 iWaypoint = 1; iWaypoint < numWaypoints; ++iWaypoint)
					{
						const Point currWaypoint = pathPs.GetWaypoint(iWaypoint);
						const F32 segmentLen = Dist(currWaypoint, prevWaypoint);

						currAccumulatedPathLen += segmentLen;
						if (currAccumulatedPathLen >= desiredPathLen)
						{
							const F32 t = MinMax01((desiredPathLen - prevAccumulatedPathLen) / Max(0.01f, segmentLen));
							const Point newEndWaypoint = Lerp(prevWaypoint, currWaypoint, t);

							pathPs.UpdateWaypoint(iWaypoint, newEndWaypoint);

							if (iWaypoint + 1 < numWaypoints)
								pathPs.TruncatePath(iWaypoint + 1);

							//pathPs.DebugDraw(kIdentity, true);

							break;
						}

						prevWaypoint = currWaypoint;
						prevAccumulatedPathLen = currAccumulatedPathLen;
					}
				}
			}
/*
			if (DebugSelection::Get().IsProcessOrNoneSelected(m_pNavChar))
			{
				results.m_pathPs.DebugDraw(parentSpace, true, 0.0f, PathWaypointsEx::ColorScheme(), Seconds(5.0f));
			}
*/

			m_pathStatus.m_pathFound = true;
			results.m_hNavAp = results.m_pathPs.IsValid() ? results.m_pathPs.GetEndActionPackHandle() : ActionPackHandle();

			SetPathFindResults(results);
		}
		else
		{
			m_pathStatus.m_pathFound = false;
		}

		m_pathStatus.m_pathUpdated = true;
		m_shouldUpdatePath		   = false;

		//const U64 endTime = TimerGetRawCount();
		//MsgCon("[%s] Cheap Path Build took %fms\n", m_pNavChar->GetName(), ConvertTicksToMilliseconds(endTime - startTime));
	}
	else
	{
		NavControl::PathFindOptions pfOptions;
		pfOptions.m_parentSpace = parentSpace;
		pfOptions.m_startLoc	= startLoc;
		pfOptions.m_goalLoc		= goalLoc;
		pfOptions.m_traversalSkillMask	 = traversalSkillMask;
		pfOptions.m_hPreferredTap		 = hPreferredTap;
		pfOptions.m_navBlockerFilterFunc = NsmPathFindBlockerFilter;
		pfOptions.m_nbFilterFuncUserData = m_pNavChar;
		pfOptions.m_pPathSpline	 = cmdContext.m_hPathSpline.ToCatmullRom();
		pfOptions.m_pathRadius	 = cmdContext.m_command.GetMoveArgs().m_pathRadius;
		pfOptions.m_waitForPatch = true;

		if (pfOptions.m_pPathSpline)
		{
			const float totalArcLen = pfOptions.m_pPathSpline->GetTotalArcLength();

			if (const TraversalActionPack* pAp = GetTraversalActionPack())
			{
				const Point tapExitPosWs = pAp->GetExitPointWs();
				const Point curPosWs = m_pNavChar->GetTranslation();
				const Locator tapExitLocWs = Locator(tapExitPosWs, pAp->GetBoundFrame().GetRotationWs());
				const Vector moveDirWs = SafeNormalize(tapExitPosWs - curPosWs, kZero);

				cmdContext.m_curPathSplineArcGoal = cmdContext.m_finalPathSplineArc;

				Nav::UpdateSplineAdvancement(tapExitLocWs,
											 moveDirWs,
											 pfOptions.m_pPathSpline,
											 cmdContext.m_curPathSplineArcGoal,
											 cmdContext.m_pathSplineArcStep,
											 m_pNavChar->GetOnSplineRadius(),
											 cmdContext.m_splineAdvanceStartTolerance,
											 cmdContext.m_beginSplineAdvancement,
											 cmdContext.m_curPathSplineArcStart);
			}
			else if (cmdContext.m_nextWaypointType != kWaypointFinalGoal)
			{
				NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
				const Point nextPosWs = cmdContext.m_nextNavGoal.GetPosWs();
				cmdContext.m_curPathSplineArcGoal = pfOptions.m_pPathSpline->FindArcLengthClosestToPoint(nextPosWs);
			}
			else
			{
				cmdContext.m_curPathSplineArcGoal = cmdContext.m_finalPathSplineArc;
			}
		}

		// Update: backing this out because sometimes they will do very long spline paths that eventually go through a TAP
		// and this breaks spline advancement logic. Given that the original case for the below change was ultimately
		// handled with a different fix, I'm going to back this out for now and punt on the nightmare that is fucking splines
		// until next game.
/*

		if ((cmdContext.m_nextWaypointType != kWaypointFinalGoal) || IsState(kStateUsingTraversalActionPack))
		{
			// never do spline path to AP entries since bad things can happen if the resolved
			// entry doesn't live reasonably near the spline itself
			pfOptions.m_pPathSpline = nullptr;
		}
*/
		// hey late game major bugs still happening because of course fucking spline paths.
		if ((cmdContext.m_nextWaypointType != kWaypointFinalGoal) && !g_navCharOptions.m_disableSplineTailGoal)
		{
			pfOptions.m_enableSplineTailGoal = true;
		}

		pfOptions.m_splineArcStart = cmdContext.m_curPathSplineArcStart;
		pfOptions.m_splineArcGoal  = cmdContext.m_curPathSplineArcGoal;
		pfOptions.m_splineArcStep  = cmdContext.m_pathSplineArcStep;

		if (FALSE_IN_FINAL_BUILD(true))
		{
			LogHdrDetails("BeginPathFind: Doing %sPath Find%s for command %d (%s) from ",
						  pfOptions.m_pPathSpline ? "Spline " : "",
						  (m_pNavChar && m_pNavChar->ShouldPathThroughPlayer()) ? " (THROUGH PLAYER)" : "",
						  cmdContext.m_commandId,
						  GetWaypointTypeName(cmdContext.m_nextWaypointType));

			LogNavLocationDetails(startLoc);
			LogMsgDetails(" to ");
			LogNavLocationDetails(goalLoc);

			if (pfOptions.m_pathRadius >= 0.0f)
			{
				LogMsgDetails(" (path radius: %0.1f)", pfOptions.m_pathRadius);
			}

			if (NextWaypointIsForAp(cmdContext))
			{
				LogMsgDetails(" NextAp: ");
				LogActionPackDetails(cmdContext.m_hNextActionPack.ToActionPack());
			}

			if (pfOptions.m_pPathSpline)
			{
				LogMsgDetails(" [spline start: %0.3f goal: %0.3f step: %0.3f%s]",
							  pfOptions.m_splineArcStart,
							  pfOptions.m_splineArcGoal,
							  pfOptions.m_splineArcStep,
							  pfOptions.m_enableSplineTailGoal ? " <tail-goal>" : "");
			}

			LogMsgDetails("\n");
		}

		m_hNavPathJob = pNavCon->BeginPathFind(m_pNavChar, pfOptions, FILE_LINE_FUNC);

		m_pNavChar->ClearPathThroughPlayer();
	}

	m_pathStatus.m_lastPathIssuedTime	 = m_pNavChar->GetCurTime();
	m_pathStatus.m_ignoreInFlightResults = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::GatherPathFindResults()
{
	PROFILE(AI, NSM_GatherPathFindResults);

	AtomicLockJanitor lock(&m_pathResultsLock, FILE_LINE_FUNC);

	NavControl* pNavCon			= m_pNavChar->GetNavControl();
	const bool updateNavControl = !IsState(kStateUsingTraversalActionPack);

	if (updateNavControl)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		const Point navPosPs = m_pNavChar->GetNavigationPosPs();

		pNavCon->UpdatePs(navPosPs, m_pNavChar);
	}

	const U8 prevPathCount = m_currentPathPs.GetWaypointCount();

	// prepare to generate the next path,
	const TimeFrame curTime = m_pNavChar->GetCurTime();

	if (m_pathStatus.m_ignoreInFlightResults)
	{
		if (m_hNavPathJob.IsValid())
		{
			LogHdr("Ignoring latest pathfind results\n");

			m_hNavPathJob.WaitForJob();
			m_hNavPathJob = NavControl::PathFindJobHandle();

			m_pathStatus.m_ignoreInFlightResults = false;
		}
	}
	else if (m_hNavPathJob.IsValid())
	{
		NavControl::PathFindResults results;
		m_pathStatus.m_pathFound = pNavCon->CollectPathFindResults(m_hNavPathJob, results, true);

		SetPathFindResults(results);
	}
	else if (m_pathStatus.m_pathUpdated && (m_pathStatus.m_kickedCommandId == m_pathStatus.m_processedCommandId))
	{
		// LogHdr("GatherPathFindResults: no path job, but path was updated and processed, so clearing flag (?)\n");
		m_pathStatus.m_pathUpdated = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::SetPathFindResults(const NavControl::PathFindResults& results)
{
	m_pathStatus.m_pathPs = results.m_pathPs;

	if (FALSE_IN_FINAL_BUILD(true))
	{
		if (m_pathStatus.m_pathFound)
		{
			LogHdrDetails("SetPathFindResults: Found path (%d waypoints)",
						  m_pathStatus.m_pathPs.GetWaypointCount());

			if (m_pathStatus.m_pathPs.IsValid())
			{
				LogMsgDetails(" [end: ");
				::LogPoint(m_pNavChar, kNpcLogChannelNavDetails, m_pathStatus.m_pathPs.GetEndWaypoint());
				LogMsgDetails("]");
			}

			if (results.m_hNavAp.IsValid())
			{
				LogMsgDetails(" [nav ap: ");
				LogActionPackDetails(results.m_hNavAp.ToActionPack());
				LogMsgDetails("]");
			}

			if (results.m_postTapPathPs.IsValid())
			{
				LogMsgDetails(" (post tap path with %d waypoints)", results.m_postTapPathPs.GetWaypointCount());
			}

			LogMsgDetails("\n");
		}
		else
		{
			LogHdrDetails("SetPathFindResults: Path NOT found!\n");
		}
	}

	m_pathStatus.m_pathUpdated = true;
	m_shouldUpdatePath = false;

	NavCommandContext* pCmdContext = GetCommandContext(m_pathStatus.m_kickedCommandId);

	const bool movingToTap = m_pathStatus.m_hTraversalActionPack.IsValid()
		&& (m_pathStatus.m_hTraversalActionPack == pCmdContext->m_hNextActionPack);

	if (!pCmdContext
		|| ((results.m_hNavAp != m_pathStatus.m_hTraversalActionPack)
			&& (results.m_hNavAp != pCmdContext->m_hNextActionPack) && !movingToTap))
	{
		LogHdrDetails("SetPathFindResults: New post-TAP path with %d waypoints\n",
					  results.m_postTapPathPs.GetWaypointCount());
		m_pathStatus.m_postTapPathPs = results.m_postTapPathPs;

		const TraversalActionPack* pTap = results.m_hNavAp.ToActionPack<TraversalActionPack>();

		m_pathStatus.m_tapAnimAdjustLs = ComputeTapPathAnimAdjustLs(m_pNavChar,
																	pTap,
																	m_pathStatus.m_pathPs,
																	m_pathStatus.m_postTapPathPs);
	}

	if (pCmdContext && (results.m_splineArcStart >= 0.0f))
	{
		LogHdrDetails("Updating spline arc start for command 'nav-%d' because path find results moved us from %0.3f to %0.3f\n",
					  pCmdContext->m_commandId,
					  pCmdContext->m_curPathSplineArcStart,
					  results.m_splineArcStart);

		pCmdContext->m_curPathSplineArcStart = results.m_splineArcStart;
	}

	if (results.m_hNavAp != m_pathStatus.m_hTraversalActionPack)
	{
		LogHdr("SetPathFindResults: Updating Nav AP from ");
		LogActionPack(m_pathStatus.m_hTraversalActionPack.ToActionPack());
		LogMsg(" to ");
		LogActionPack(results.m_hNavAp.ToActionPack());
		LogMsg("\n");

		m_pathStatus.m_hTraversalActionPack = results.m_hNavAp;
	}

	NAV_ASSERT(!m_pathStatus.m_pathFound || (m_pathStatus.m_pathPs.GetWaypointCount() > 0));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::ProcessPathFindResults()
{
	PROFILE_AUTO(AI);

	if (!m_pathStatus.m_pathFound || !m_pathStatus.m_pathUpdated)
		return;

	if (IsInterrupted())
		return;

	NavCommandContext* pCmdContext = GetCommandContext(m_pathStatus.m_kickedCommandId);

	if (!pCmdContext)
		return;

	NavCharacter* pNavChar	   = m_pNavChar;
	const Locator& parentSpace = pNavChar->GetParentSpace();
	INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController();

	const NavLocation pathEndNavLoc = m_pathStatus.m_pathPs.GetEndAsNavLocation(parentSpace);
	const ActionPack* pNavAp		= m_pathStatus.m_hTraversalActionPack.ToActionPack();
	const bool usingCurAp		  = (GetActionPackUsageState() == kActionPackUsageEntered);
	const bool pathIsForActiveCmd = pCmdContext->m_commandId == m_activeContext.m_commandId;

	// NB: We should only be working with the active context when considering 'current' action pack
	// because that is singular global state, not part of the context itself (we can't reserve more than one AP at a
	// time, for example) Also, if we are entered into our current AP just set next instead and let the exiting state
	// update current when the exiting process is complete.
	if (!usingCurAp && pathIsForActiveCmd)
	{
		// IN THE FUTURE ... Cases we need to consider:
		// 1. We were moving to our final goal, but then the path came back saying we need to use a TAP instead
		// 2. We were moving to a TAP, but the path came back saying no TAP was needed
		// 3. We were moving to a TAP, but the path came back saying use a different TAP instead
		// 4. We were moving to our next AP (not necessarily our final goal) and nothing changed
		// 5. We were moving to our final goal and nothing changed

		// FOR NOW ... because our path find goal is always to our next AP, even if that AP is a TAP
		// on our way to our true destination, we never know if a better TAP would become available
		// But that also means that if the pathfind results came back with a valid Nav AP then we
		// should always switch to it.

		if (pNavAp)
		{
			if (pCmdContext->m_command.GetType() == NavCommand::kSteerToLocation)
			{
				StopPathFailed(*pCmdContext, "Steering path through AP!");
				return;
			}
			else
			{
				SetNextActionPack(*pCmdContext, pNavAp, "ProcessPathFindResults[New-TAP]");

				if (SetCurrentActionPack(pNavAp, "ProcessPathFindResults[TAP-Changed]"))
				{
					// moving to a new AP (either a new TAP or our goal AP)
					SetNextWaypoint(*pCmdContext,
									pathEndNavLoc,
									kWaypointAPDefaultEntry,
									kNavGoalReachedTypeStop,
									"ProcessPathFindResults[New-AP]");
				}
				else
				{
					SetNextWaypoint(*pCmdContext,
									pathEndNavLoc,
									kWaypointAPWait,
									kNavGoalReachedTypeStop,
									"ProcessPathFindResults[New-TAP-Wait]");
				}
			}
		}
		else
		{
			SetNextWaypoint(*pCmdContext,
							pathEndNavLoc,
							pCmdContext->m_nextWaypointType,
							pCmdContext->m_nextNavReachedType,
							"ProcessPathFindResults[Def]");
		}

#if 0
		const ActionPack* pCurAp = m_hCurrentActionPack.ToActionPack();
		const ActionPack* pGoalAp = pCmdContext->m_command.GetGoalActionPack();
		const ActionPack* pNextAp = pNavAp ? pNavAp : pGoalAp;

		const bool curApIsGoal = (pCurAp == pGoalAp);
		const bool nextApChanged = (pCurAp != pNextAp);

		if ((!curApIsGoal || pNavAp) && nextApChanged)
		{
			// if we got here we got a pathfind result changed which TAP we were using (possibly no TAP now)
			if (SetCurrentActionPack(pNextAp, "ProcessPathFindResults[TAP-Changed]"))
			{
				if (pNextAp)
				{
					// moving to a new AP (either a new TAP or our goal AP)
					SetNextWaypoint(*pCmdContext,
									pathEndNavLoc,
									kWaypointAPDefaultEntry,
									kNavGoalReachedTypeStop,
									"ProcessPathFindResults[New-AP]");
				}
				else
				{
					// was moving through a TAP, but our pathfind got to goal location without one
					SetNextWaypoint(*pCmdContext,
									pathEndNavLoc,
									kWaypointFinalGoal,
									pCmdContext->m_command.GetGoalReachedType(),
									"ProcessPathFindResults[TAP-Gone]");
				}
			}
			else if (pNavAp)
			{
				SetNextActionPack(*pCmdContext, pNavAp, "ProcessPathFindResults[New-TAP]");

				SetNextWaypoint(*pCmdContext,
								pathEndNavLoc,
								kWaypointAPWait,
								kNavGoalReachedTypeStop,
								"ProcessPathFindResults[New-TAP-Wait]");
			}
			else
			{
				LogHdr("Failed to change to next AP ");
				LogActionPack(pNextAp);
				LogMsg(" - Failing command\n");

				StopPathFailed(*pCmdContext, "Failed to change APs");
			}
		}
		else
		{
			SetNextWaypoint(*pCmdContext,
							pathEndNavLoc,
							pCmdContext->m_nextWaypointType,
							pCmdContext->m_nextNavReachedType,
							"ProcessPathFindResults[CurAP]");
		}
#endif
	}
	else
	{
		// our next action pack will be either the next traversal action pack in our path or the final goal action pack (if we have one)
		const ActionPack* pNextAp = pNavAp ? pNavAp : pCmdContext->m_hNextActionPack.ToActionPack();

		if (usingCurAp && pNextAp && (pNextAp == GetActionPack()))
		{
			LogHdr("Ignoring path results that has us going to the AP we're already using\n");
			ClearPath();
		}
		else
		{
			const bool nextApChanged = SetNextActionPack(*pCmdContext, pNextAp, "ProcessPathFindResults");

			if (pNextAp)
			{
				bool resolveValid	   = false;
				NavLocation apEntryLoc = pathEndNavLoc;

				if (ActionPackController* pApController = m_pAnimationControllers->GetControllerForActionPack(pNextAp))
				{
					ActionPackResolveInput apResolveInfo = MakeApResolveInput(pCmdContext->m_command, pNextAp);
					ActionPackEntryDef newApEntry;

					if (pApController->ResolveDefaultEntry(apResolveInfo, pNextAp, &newApEntry))
					{
						pCmdContext->m_actionPackEntry = newApEntry;
						resolveValid = true;

						apEntryLoc = newApEntry.m_entryNavLoc;
					}
				}

				SetNextWaypoint(*pCmdContext,
								apEntryLoc,
								kWaypointAPDefaultEntry,
								kNavGoalReachedTypeStop,
								resolveValid ? "ProcessPathFindResults[AP-Def]" : "ProcessPathFindResults[AP-Path]");

				m_pathStatus.m_pathPs.UpdateEndLoc(parentSpace, apEntryLoc);
			}
			else
			{
				SetNextWaypoint(*pCmdContext,
								pathEndNavLoc,
								kWaypointFinalGoal,
								pCmdContext->m_command.GetGoalReachedType(),
								"ProcessPathFindResults");
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::GetPathFindStartLocation(NavLocation* pStartLocOut,
											   ActionPackHandle* phPreferredTapOut,
											   float* pTapBiasOut) const
{
	NavLocation startLoc;
	ActionPackHandle hPreferredTap = m_hCurrentActionPack;
	float tapBias = -3.0f;
	bool valid	  = false;

	if (const BaseState* pState = GetStateMachine().GetState())
	{
		valid = pState->GetPathFindStartLocation(&startLoc, &hPreferredTap, &tapBias);
	}

	NavControl* pNavCon = m_pNavChar->GetNavControl();
	if (!valid && pNavCon)
	{
		startLoc = pNavCon->GetNavLocation();
		valid	 = true;
	}

	if (pStartLocOut)
		*pStartLocOut = startLoc;
	if (phPreferredTapOut)
		*phPreferredTapOut = hPreferredTap;
	if (pTapBiasOut)
		*pTapBiasOut = tapBias;

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::PatchPathStartPs(Point_arg newStartPs)
{
	NAV_ASSERT(IsReasonable(newStartPs));

	if (!m_currentPathPs.IsValid())
	{
		return;
	}

	m_currentPathPs.UpdateWaypoint(0, newStartPs);
	PathWaypointsEx prevPathPs = m_currentPathPs;

	m_currentPathPs.Clear();
	m_currentPathPs.AddWaypoint(prevPathPs, 0);

	const I32F waypointCount = prevPathPs.GetWaypointCount();

	I32F iNewPathStart = (waypointCount - 1);

	for (I32F i = 1; i < (waypointCount - 1); ++i)
	{
		const Point leg0 = prevPathPs.GetWaypoint(i);
		const Point leg1 = prevPathPs.GetWaypoint(i + 1);

		Scalar tt;
		DistPointSegmentXz(newStartPs, leg0, leg1, nullptr, &tt);

		if (tt <= SCALAR_LC(0.0f) || tt >= SCALAR_LC(1.0f))
		{
			iNewPathStart = i;
			break;
		}
	}

	for (I32F i = iNewPathStart; i < waypointCount; ++i)
	{
		m_currentPathPs.AddWaypoint(prevPathPs, i);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::PatchPathEnd(const NavCommandContext& cmdContext,
								   const NavLocation endLoc,
								   const char* strFuncName)
{


	const Locator& parentSpace = m_pNavChar->GetParentSpace();

	if (cmdContext.m_commandId == m_activeContext.m_commandId)
	{
		if (m_currentPathPs.IsValid())
		{
			const Point prevEndPs = m_currentPathPs.GetEndWaypoint();
			const Point newEndPs = endLoc.GetPosPs();
			if ((DistXz(prevEndPs, newEndPs) > 2.0f) || (Abs(prevEndPs.Y() - newEndPs.Y()) > 1.5f))
			{
				LogHdr("PatchPathEnd[%s] Patching end path loc to ", strFuncName);
				LogNavLocation(endLoc);
				LogMsg(" and requesting new path\n");
				m_shouldUpdatePath = true;
			}

			m_currentPathPs.UpdateEndLoc(parentSpace, m_activeContext.m_nextNavGoal);
		}

		if (INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController())
		{
			pNavAnimController->PatchCurrentPathEnd(m_activeContext.m_nextNavGoal);
		}
	}

	if (m_pathStatus.m_pathPs.IsValid() && (m_pathStatus.m_processedCommandId == cmdContext.m_commandId))
	{
		m_pathStatus.m_pathPs.UpdateEndLoc(parentSpace, m_activeContext.m_nextNavGoal);
	}

}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavStateMachine::GetPathFindRadiusForCommand(const NavCommandContext& cmdContext) const
{
	if (cmdContext.m_command.GetType() == NavCommand::kSteerToLocation)
	{
		return 0.0f;
	}

	const float cmdRadius = cmdContext.IsValid() ? cmdContext.m_command.GetMoveArgs().m_pathRadius : -1.0f;

	if (cmdRadius >= 0.0f)
	{
		return cmdRadius;
	}

	const NavControl* pNavControl = m_pNavChar ? m_pNavChar->GetNavControl() : nullptr;
	if (pNavControl)
	{
		return pNavControl->GetEffectivePathFindRadius();
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::StopAtPathEndPs(const IPathWaypoints* pPathPs) const
{
	if (!m_activeContext.IsValid() || !pPathPs || !pPathPs->IsValid())
	{
		return false;
	}

	const Point endPosPs  = pPathPs->GetEndWaypoint();
	const Point nextPosPs = m_activeContext.m_nextNavPosPs;

	NavMoveArgs moveArgs;
	m_activeContext.GetEffectiveMoveArgs(moveArgs);

	const float goalRadius = Max(moveArgs.m_goalRadius, 0.5f);
	const float goalErr	   = DistXz(endPosPs, nextPosPs);

	if ((goalErr > goalRadius) || (Abs(nextPosPs.Y() - endPosPs.Y()) > 1.0f))
	{
		return true;
	}

	return moveArgs.m_goalReachedType == kNavGoalReachedTypeStop;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::CheckActiveCommandStillValid(NavCommandContext& cmdContext)
{
	if (m_queuedCommand.IsValid())
	{
		return true;
	}

	if (!cmdContext.IsInProgress())
	{
		return true;
	}

	const bool isForActiveCmd = cmdContext.m_commandId == m_activeContext.m_commandId;
	// check action pack handles are still valid
	if (isForActiveCmd && !IsState(kStateMovingAlongPath) && !IsState(kStateStopped))
	{
		return true;
	}

	// if we are moving to an action pack and it turns invalid,
	const ActionPack* pGoalAp = cmdContext.m_command.GetGoalActionPack();
	const bool goalApInvalid  = !pGoalAp || !pGoalAp->IsEnabled();
	if (cmdContext.m_command.GetType() == NavCommand::kMoveToActionPack && cmdContext.IsInProgress() && goalApInvalid)
	{
		// command failed
		if (IsState(kStateMovingAlongPath))
		{
			StopPathFailed(cmdContext, "Command failed because action pack disappeared");
		}
		else
		{
			SetContextStatus(cmdContext, NavContextStatus::kFailed, "CheckActiveCommandStillValid: AP Disappeared");
		}

		return false;
	}

	if (!IsActionPackUsageEntered() && cmdContext.m_hNextActionPack.IsValid())
	{
		const ActionPack* pNextAp = cmdContext.m_hNextActionPack.ToActionPack();
		if (!pNextAp || !pNextAp->IsEnabled())
		{
			if (isForActiveCmd)
			{
				LogHdr("CheckActiveCommandStillValid next AP disappeared! Attempting to resume moving\n");

				SetNextActionPack(cmdContext, nullptr, "CheckActiveCommandStillValid");
				SetCurrentActionPack(nullptr, "CheckActiveCommandStillValid");
				StopMovingImmediately(true, false, "CheckActiveCommandStillValid");
				ResumeMoving(true, false);

				return false;
			}
			else
			{
				StopPathFailed(cmdContext, "CheckActiveCommandStillValid");
			}
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::GetMoveAlongSplineCurrentArcLen(const StringId64 splineNameId, F32& outArcLen) const
{
	if (m_activeContext.m_command.GetType() == NavCommand::kMoveAlongSpline)
	{
		const CatmullRom* const pPathSpline = m_activeContext.m_hPathSpline.ToCatmullRom();
		const SplineData* const pSplineData = pPathSpline ? pPathSpline->GetSplineData() : nullptr;

		if (pSplineData && (pSplineData->m_nameId == splineNameId))
		{
			outArcLen = m_activeContext.m_curPathSplineArcStart;
			return true;
		}
	}

	outArcLen = 0.0f;
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Update()
{
	PROFILE(AI, NSM_Update);

	NavCommandContext& cmdContext = m_nextContext.IsValid() ? m_nextContext : m_activeContext;

	if (!CheckActiveCommandStillValid(cmdContext))
	{
		return;
	}

	if ((m_pathStatus.m_processedCommandId == m_activeContext.m_commandId) && m_activeContext.IsValid())
	{
		const U32F prevCount = m_currentPathPs.GetWaypointCount();

		if (IsActionPackUsageEntered() && !IsState(kStateStartingToExitActionPack) && !IsState(kStateExitingActionPack))
		{
			// don't update path
		}
		else
		{
			UpdateCurrentPath(m_pathStatus.m_pathPs, &m_currentPathPs, &m_currentPathDirPs);
		}

		if (FALSE_IN_FINAL_BUILD(prevCount != m_currentPathPs.GetWaypointCount()))
		{
			LogHdrDetails("UpdateCurrentPath: m_currentPathPs waypoint count went from %d to %d\n",
						  prevCount,
						  m_currentPathPs.GetWaypointCount());
		}
	}

	if (m_pNavChar->CanResumeOrProcessNavCommands())
	{
		TryProcessResume();

		// called during Update phase prior to updating animation controllers
		// First we handle transitions due to commands
		if (CanProcessCommands() && m_queuedCommand.IsValid())
		{
			if (m_queuedCommand.GetType() == NavCommand::kStopAndStand || CanProcessMoveCommands())
			{
				PROFILE(AI, NSM_ProcessCommand);
				ProcessCommand(m_queuedCommand);
			}
		}
	}

	const bool wasSuspended	 = m_pathStatus.m_suspended;
	m_pathStatus.m_suspended = ShouldSuspendPathFind();
	if (!m_pathStatus.m_suspended)
	{
		BeginPathFind();
	}

	UpdateSplineAdvancement();

	{
		AtomicLockJanitor lock(&m_pathResultsLock, FILE_LINE_FUNC);

		ProcessPathFindResults();

		if (m_pathStatus.m_kickedCommandId != m_pathStatus.m_processedCommandId)
		{
			// last minute sanity/recovery check
			if (!m_pathStatus.m_pathUpdated && !m_shouldUpdatePath && (m_pathStatus.m_kickedCommandId != 0))
			{
				LogHdr("Waiting to process our first path for command, but path isn't updated (?) requesting new path\n");
				m_shouldUpdatePath = true;
			}

			HandleFirstPathForCommand();
		}
	}

	ValidateActionPackUsage();

	if (m_activeContext.m_nextWaypointType == kWaypointFinalGoal
		&& (GetActionPackUsageState() == kActionPackUsageReserved) && m_pathStatus.m_pathFound && !wasSuspended)
	{
		LogHdr("Past ProcessPathFindResults but still holding an AP reservation when moving to final goal! wasSuspended = %s, pathUpdated = %s, pathFound = %s, apTurnedInvalid = %s\n",
			   wasSuspended ? "TRUE" : "false",
			   m_pathStatus.m_pathUpdated ? "true" : "FALSE",
			   m_pathStatus.m_pathFound ? "true" : "FALSE",
			   m_hCurrentActionPack.HasTurnedInvalid() ? "TRUE" : "false");

		// MailNpcLogTo(m_pNavChar, "john_bellomy@naughtydog.com", "Past ProcessPathFindResults but still holding an AP reservation when moving to final goal!", FILE_LINE_FUNC);

		SetCurrentActionPack(nullptr, "ProcessPathFindResults-failsafe");
	}

	GetStateMachine().TakeStateTransition();

	if (NavStateMachine::BaseState* pState = GetState())
	{
		pState->Update();
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::PostRootLocatorUpdate()
{
	if (NavStateMachine::BaseState* pState = GetState())
	{
		pState->PostRootLocatorUpdate();
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::HandleFirstPathForCommand()
{
	if (!m_pathStatus.m_pathUpdated || m_shouldUpdatePath || IsInterrupted())
	{
		return;
	}

	if (m_actionPackUsageState == kActionPackUsageEntered)
	{
		const ActionPack::Type apType = GetActionPackUsageType();
		ActionPackController* pActionPackController = m_pAnimationControllers->GetControllerForActionPackType(apType);
		if (!pActionPackController->IsReadyToExit())
			return;
	}

	LogHdr("Handling new path for command %d (last processed command: %d)\n",
		   m_pathStatus.m_kickedCommandId,
		   m_pathStatus.m_processedCommandId);

	m_pathStatus.m_processedCommandId = m_pathStatus.m_kickedCommandId;
	m_pathStatus.m_pathUpdated = false;

	if (m_pathStatus.m_processedCommandId == m_nextContext.m_commandId)
	{
		const MotionConfig& mc = m_pNavChar->GetMotionConfig();

		if (!m_pathStatus.m_pathFound && mc.m_dontStopOnFailedMoves
			&& (m_nextContext.m_status == NavContextStatus::kWaitingForPath))
		{
			LogHdr("HandleFirstPathForCommand: Ignoring move command %d '%s' because no path was found and we don't want to stop on failed moves\n",
				   m_nextContext.m_commandId,
				   m_nextContext.m_command.GetTypeName(),
				   GetNavContextStatusName(m_nextContext.m_status));
			m_nextContext.Reset();
			return;
		}

		LogHdr("HandleFirstPathForCommand: Activating move command %d '%s' (status: %s)\n",
			   m_nextContext.m_commandId,
			   m_nextContext.m_command.GetTypeName(),
			   GetNavContextStatusName(m_nextContext.m_status));

		m_activeContext = m_nextContext;
		m_nextContext.Reset();
	}

	if (m_activeContext.m_status != NavContextStatus::kWaitingForPath)
	{
		if (m_activeContext.m_status == NavContextStatus::kFailed)
		{
			StopPathFailed(m_activeContext, "HandleFirstPathForCommand-deadcmd");
		}

		LogHdr("HandleFirstPathForCommand doing nothing because new command status is '%s'\n",
			   GetNavContextStatusName(m_activeContext.m_status));

		return;
	}

	if (m_pathStatus.m_pathFound)
	{
		UpdateCurrentPath(m_pathStatus.m_pathPs, &m_currentPathPs, &m_currentPathDirPs);
	}
	else
	{
		m_currentPathPs.Clear();
		StopPathFailed(m_activeContext, "HandleFirstPathForCommand no found path");
		return;
	}

	SetContextStatus(m_activeContext, NavContextStatus::kActive, "Processed First Path");

	INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController();

	if ((m_actionPackUsageState != kActionPackUsageNone)
		&& (m_hCurrentActionPack.IsValid() && (m_hCurrentActionPack == m_activeContext.m_hNextActionPack))
		&& !IsState(kStateResuming))
	{
		const bool navInterrupted = pNavAnimController->IsInterrupted();

		LogHdr("Told to go to an AP we're already using (");
		LogActionPack(m_hCurrentActionPack.ToActionPack());
		LogMsg(") so doing nothing %s\n", navInterrupted ? "" : "(but reissuing move to nav controller)");

		m_activeContext.m_hNextActionPack = ActionPackHandle();

		if (!navInterrupted)
		{
			NavAnimStartParams params;
			params.m_pPathPs  = GetLastFoundPathPs();
			params.m_waypoint = GetWaypointContract(m_activeContext);

			m_activeContext.GetEffectiveMoveArgs(params.m_moveArgs);

			pNavAnimController->StartNavigating(params);
		}

		if (IsState(kStateStopping))
		{
			GoMovingAlongPath();
			GetStateMachine().TakeStateTransition();
		}

		return;
	}

	if (m_actionPackUsageState == kActionPackUsageEntered)
	{
		const ActionPack::Type apType = GetActionPackUsageType();

		ActionPackController* pActionPackController = m_pAnimationControllers->GetControllerForActionPackType(apType);

		const bool pathFailed = !m_pathStatus.m_pathFound;
		const bool shouldStop = (m_activeContext.m_command.GetType() == NavCommand::kStopAndStand) || pathFailed;

		LogHdr("HandleFirstPathForCommand[Ap]: First path to exit AP %s for command %d shouldStop = %s (lastCommand = %s, commandStatus = %s, pathFailed = %s)\n",
			   ActionPack::GetTypeName(apType),
			   m_activeContext.m_commandId,
			   shouldStop ? "true" : "false",
			   GetNavContextStatusName(m_activeContext.m_status),
			   m_activeContext.m_command.GetTypeName(),
			   pathFailed ? "TRUE" : "false");

		if (shouldStop)
		{
			pActionPackController->Exit(nullptr);
		}
		else
		{
			pActionPackController->Exit(&m_currentPathPs);
		}

		GoExitingActionPack(false);
		GetStateMachine().TakeStateTransition();
		return;
	}

	const bool isSteeringCmd = m_activeContext.m_command.GetType() == NavCommand::kSteerToLocation;

	// now that we've fully committed to the new command context, make the pending next AP our new current AP
	ActionPack* pActionPack = m_activeContext.m_hNextActionPack.ToActionPack();

	if (isSteeringCmd)
	{
		if (pActionPack)
		{
			StopPathFailed(m_activeContext, "Steered to AP!");
			return;
		}
		else
		{
			const Vector curFwPs = GetLocalZ(m_pNavChar->GetLocatorPs());
			const float curAngleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(m_currentPathDirPs, curFwPs)));
			if (curAngleDiffDeg >= g_navCharOptions.m_steerFailAngleDeg)
			{
				SetContextStatus(m_activeContext, NavContextStatus::kFailed, "Initial steering angle failure!");
				return;
			}
		}
	}

	// NB: this will also try to reserve the actionpack!
	if (SetCurrentActionPack(pActionPack, "HandleFirstPathForCommand"))
	{
		// successfully reserved our AP
		NAV_ASSERT(m_actionPackUsageState == (pActionPack ? kActionPackUsageReserved : kActionPackUsageNone));
	}
	else if (pActionPack->GetType() == ActionPack::kTraversalActionPack)
	{
		SetNextWaypoint(m_activeContext,
						m_activeContext.m_nextNavGoal,
						kWaypointAPWait,
						kNavGoalReachedTypeStop,
						"HandleFirstPathForCommand[No-Ap]");
	}
	else
	{
		LogHdr("Failed to set current AP to '");
		LogActionPack(pActionPack);
		LogMsg("'! Failing nav command.\n");

		StopPathFailed(m_activeContext, "AP Reserve Failed");
		return;
	}

	const NavControl* pNavControl = m_pNavChar->GetNavControl();

	const float pathLength = m_currentPathPs.ComputePathLength();

	// save this off because patching path end for AP entries might trigger a new pathfind, but it's not a valid reason to delay moving
	const bool neededNewPath = m_shouldUpdatePath;
	bool canStartMoving = true;

	// If the next waypoint is an action pack and we can use it now then skip moving and enter arriving immediately
	if (pActionPack && IsNextWaypointActionPackEntry() && (m_actionPackUsageState == kActionPackUsageReserved))
	{
		ActionPackEntryDef& apEntry = m_activeContext.m_actionPackEntry;

		ActionPackResolveInput apResolveInfo = MakeApResolveInput(m_activeContext.m_command, pActionPack);

		const ActionPack::Type apType = pActionPack->GetType();
		ActionPackController* pActionPackController = m_pAnimationControllers->GetControllerForActionPackType(apType);
		NAV_ASSERT(pActionPackController);

		ActionPackEntryDef newApEntry;

		const bool closeForFullResolve = DistanceWithinApResolveRange(pathLength);
		const bool sameSpace = Nav::IsOnSameNavSpace(pNavControl->GetNavLocation(),
													 pActionPack->GetRegisteredNavLocation());
		const bool fullResolveAllowed = closeForFullResolve && sameSpace;

		const bool entryLocWasValid = apEntry.IsValidFor(pActionPack);
		const bool entryResolved	= fullResolveAllowed
								   && pActionPackController->ResolveEntry(apResolveInfo, pActionPack, &newApEntry);

		const bool defaultEntryResolved = !entryResolved && !entryLocWasValid
										  && pActionPackController->ResolveDefaultEntry(apResolveInfo,
																						pActionPack,
																						&newApEntry);

		NavGoalReachedType entryReachedType = kNavGoalReachedTypeStop;

		bool canEnterImmediately = false;

		if (entryResolved || defaultEntryResolved)
		{
			apEntry = newApEntry;
			apEntry.m_mtSubcategoryId = apResolveInfo.m_mtSubcategory;

			entryReachedType	= apEntry.m_stopBeforeEntry ? kNavGoalReachedTypeStop : kNavGoalReachedTypeContinue;
			canEnterImmediately = ActionPackController::TestForImmediateEntry(m_pNavChar, apEntry);
		}

		bool enterImmediately = canEnterImmediately && pActionPack->IsAvailableFor(m_pNavChar);

		if (enterImmediately && !pActionPack->IsReservedBy(m_pNavChar))
		{
			LogHdr("HandleFirstPathForCommand: resolved entry - trying to enter immediately into AP ");
			LogActionPack(pActionPack);
			LogMsg("but need to re-reserve AP first!\n");

			// last chance to reserve an AP that might've been swept out from under us
			enterImmediately = ReserveActionPack();
		}

		TraversalActionPack* pTap = TraversalActionPack::FromActionPack(pActionPack);
		if (enterImmediately && pTap)
		{
			enterImmediately = pTap->TryAddUser(m_pNavChar);
		}

		// If the stars align we and we can reserve the action pack we can enter right now...
		if (enterImmediately)
		{
			LogHdr("HandleFirstPathForCommand: short-circuit directly into action pack (");
			LogActionPack(pActionPack);
			LogMsg(") anim = %s\n", DevKitOnly_StringIdToString(apEntry.m_entryAnimId));

			const Demeanor curDemeanor = m_pNavChar->GetCurrentDemeanor();
			const Demeanor reqDemeanor = m_pNavChar->GetRequestedDemeanor();
			if (curDemeanor != reqDemeanor)
			{
				LogHdr("Forcing demeanor update before AP short-circuit\n");
				m_pNavChar->ForceDemeanor(reqDemeanor, AI_LOG);
			}

			BeginEnterActionPack(pActionPack, "FirstPath-Immediate");
			canStartMoving = false;
		}
		else if (entryResolved || defaultEntryResolved)
		{
			const NavLocation nextLoc = apEntry.m_entryNavLoc;

			pNavAnimController->SetMoveToTypeContinuePoseMatchAnim(apEntry.m_entryAnimId, apEntry.m_phase);

			// Ok, we couldn't enter into the action pack right away but at least we have resolved it and we can move to the resolved location
			if (entryResolved)
			{
				LogHdr("HandleFirstPathForCommand - resolved immediately, ap type %s\n",
					   ActionPack::GetTypeName(pActionPack->GetType()));
				SetNextWaypoint(m_activeContext,
								nextLoc,
								kWaypointAPResolvedEntry,
								entryReachedType,
								"HandleFirstPathForCommand - ApEntry");
			}
			else
			{
				LogHdr("HandleFirstPathForCommand - resolved default immediately, ap type %s\n",
					   ActionPack::GetTypeName(pActionPack->GetType()));
				SetNextWaypoint(m_activeContext,
								nextLoc,
								kWaypointAPDefaultEntry,
								entryReachedType,
								"HandleFirstPathForCommand - DefApEntry");
			}

			if (Nav::IsOnSameNavSpace(m_activeContext.m_nextNavGoal, pNavControl->GetNavLocation()))
			{
				PatchPathEnd(m_activeContext, m_activeContext.m_nextNavGoal, "FirstPath-ResolvedEntry");
			}
		}
		else if (!apEntry.m_hResolvedForAp.IsValid())
		{
			pNavAnimController->SetMoveToTypeContinuePoseMatchAnim(m_activeContext.m_command.GetDestAnimId(),
																   m_activeContext.m_command.GetDestAnimPhase());
		}
	}

	if (canStartMoving && (!neededNewPath || IsState(kStateResuming)))
	{
		if (m_currentPathPs.IsValid())
		{
			NavAnimStartParams params;
			params.m_waypoint = GetWaypointContract(m_activeContext);
			m_activeContext.GetEffectiveMoveArgs(params.m_moveArgs);

			if (isSteeringCmd)
			{
				params.m_moveDirPs = GetLocalZ(m_pNavChar->GetRotationPs());
			}
			else
			{
				params.m_pPathPs = &m_currentPathPs;
			}

			LogHdr("HandleFirstPathForCommand: NavAnimController::StartNavigating(%s, %s)\n",
				   GetMotionTypeName(m_activeContext.m_command.GetMotionType()),
				   GetGoalReachedTypeName(params.m_waypoint.m_reachedType));

			if (params.m_moveArgs.m_motionType >= kMotionTypeMax)
			{
				MailNpcLogTo(m_pNavChar,
							 "john_bellomy@naughtydog.com",
							 "Started moving without motion type",
							 FILE_LINE_FUNC);
			}

			pNavAnimController->StartNavigating(params);

			if (isSteeringCmd)
			{
				GoSteeringAlongPath();
			}
			else
			{
				GoMovingAlongPath();
			}
			GetStateMachine().TakeStateTransition();
		}
		else
		{
			StopPathFailed(m_activeContext, "HandleFirstPathForCommand: Invalid path!");
		}
	}
	else
	{
		LogHdr("HandleFirstPathForCommand: Did not start moving (canStartMoving: %s) (neededNewPath: %s)\n",
			   TrueFalse(canStartMoving),
			   TrueFalse(neededNewPath));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::EnterNewParentSpace(const Transform& matOldToNew,
										  const Locator& oldParentSpace,
										  const Locator& newParentSpace)
{
	m_activeContext.EnterNewParentSpace(matOldToNew, oldParentSpace, newParentSpace);
	m_nextContext.EnterNewParentSpace(matOldToNew, oldParentSpace, newParentSpace);

	m_pathStatus.m_pathPs.ChangeParentSpace(matOldToNew);
	m_pathStatus.m_postTapPathPs.ChangeParentSpace(matOldToNew);

	m_currentPathPs.ChangeParentSpace(matOldToNew);
	m_currentPathDirPs = m_currentPathDirPs * matOldToNew;

	// if no pending commands (don't want to stomp on pending command)
	if (IsCommandInProgress() && m_pathStatus.m_pathUpdated)
	{
		// if we changed spaces after path was updated, abort path
		m_pathStatus.m_pathUpdated = false;
		m_shouldUpdatePath = true;
		LogHdr("ChangeParentSpace: pathUpdated = false\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::UpdateSplineAdvancement()
{
	if (m_activeContext.m_command.GetType() != NavCommand::kMoveAlongSpline)
		return;

	const bool debugDraw = false;

	const F32 moveSpeed		= Length(m_pNavChar->GetVelocityPs());
	const Vector moveDirPs	= AsUnitVectorXz(m_pNavChar->GetVelocityPs(), GetLocalZ(m_pNavChar->GetRotationPs()));
	const Vector moveDirWs	= m_pNavChar->GetParentSpace().TransformVector(moveDirPs);
	const Locator charLocWs = m_pNavChar->GetLocator();

	Vector moveDirToUseWs = moveDirWs;

	// JDB: 1m/s is a bad stopped speed threshold, as we have ambient walk sets that spend the majority of their time
	// going slower than that. In the future we should probably query the active nav anim controller for what it's
	// effect stopped speed is and use that. But for now we'll fallback to path dir if we are trying to move.
	if (moveSpeed < 0.4f)
	{
		if (IsCommandInProgress())
		{
			const Vector pathDirWs = m_pNavChar->GetParentSpace().TransformVector(GetPathDirPs());

			if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_displayNavState && DebugSelection::Get().IsProcessSelected(m_pNavChar)))
			{
				const Point drawPosWs = m_pNavChar->GetTranslation() + kUnitYAxis;
				g_prim.Draw(DebugArrow(drawPosWs, pathDirWs, kColorRed, kPrimEnableHiddenLineAlpha));
				g_prim.Draw(DebugString(drawPosWs + pathDirWs, StringBuilder<256>("%0.3fm/s", moveSpeed).c_str(), kColorRed, 0.6f));
			}

			moveDirToUseWs = pathDirWs;
		}
		else
		{
			moveDirToUseWs = kZero;
		}
	}

	Nav::UpdateSplineAdvancement(charLocWs,
								 moveDirToUseWs,
								 m_activeContext.m_hPathSpline.ToCatmullRom(),
								 m_activeContext.m_curPathSplineArcGoal,
								 m_activeContext.m_pathSplineArcStep,
								 m_pNavChar->GetOnSplineRadius(),
								 m_activeContext.m_splineAdvanceStartTolerance,
								 m_activeContext.m_beginSplineAdvancement,
								 m_activeContext.m_curPathSplineArcStart,
								 FALSE_IN_FINAL_BUILD(debugDraw));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::UpdatePathDirectionPs()
{
	INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController();

	const WaypointContract waypointContract = GetWaypointContract(m_activeContext);
	pNavAnimController->UpdatePathWaypointsPs(m_currentPathPs, waypointContract);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::NavCommandContext::DebugPrint(const NavCharacter* pNavChar,
													DoutBase* pDout,
													const char* preamble /* = "" */) const
{
	STRIP_IN_FINAL_BUILD;

	if (!IsValid())
	{
		pDout->Printf("%s<Invalid>\n", preamble);
		return;
	}

	pDout->Printf("%sID:                 %d\n", preamble, m_commandId);
	pDout->Printf("%sStatus:             %s\n", preamble, GetNavContextStatusName(m_status));
	pDout->Printf("%sCommand:            ", preamble);
	m_command.DebugPrint(pDout, g_navCharOptions.m_displayNavStateSourceInfo);

	pDout->Printf("%sGoalLocation:       ", preamble);
	m_finalNavGoal.DebugPrint(pDout);
	pDout->Printf("\n");

	pDout->Printf("%sGoalRadius:         %.2f\n", preamble, m_command.GetGoalRadius());

	pDout->Printf("%sNextLocation:       ", preamble);
	m_nextNavGoal.DebugPrint(pDout);
	pDout->Printf("\n");

	pDout->Printf("%sNextWaypointType:   %s\n", preamble, GetWaypointTypeName(m_nextWaypointType));

	if (m_command.GetType() == NavCommand::kSteerToLocation)
	{
		pDout->Printf("%sSteer Rate:         %0.1fdeg/sec\n", preamble, m_command.GetSteerRateDps());
	}
	else
	{
		pDout->Printf("%sNavGoalReachedType: %s\n", preamble, NavCommand::GetGoalReachedTypeName(m_nextNavReachedType));

		const ActionPack* pNextAp = m_hNextActionPack.ToActionPack();
		const ActionPack* pGoalAp = m_command.GetGoalActionPack();

		pDout->Printf("%sNextAP:             %s (0x%x)\n",
					  preamble,
					  pNextAp ? pNextAp->GetName() : "nullptr",
					  m_hNextActionPack.GetMgrId());
		pDout->Printf("%sGoalAP:             %s (0x%x)\n",
					  preamble,
					  pGoalAp ? pGoalAp->GetName() : "nullptr",
					  m_command.GetGoalActionPackMgrId());
	}

	if ((m_nextWaypointType == kWaypointAPResolvedEntry) || (m_nextWaypointType == kWaypointAPDefaultEntry))
	{
		if (m_actionPackEntry.m_entryAnimId != INVALID_STRING_ID_64)
		{
			pDout->Printf("%sAP Entry Anim:      %s @ %0.3f (%s)\n",
						  preamble,
						  DevKitOnly_StringIdToString(m_actionPackEntry.m_entryAnimId),
						  m_actionPackEntry.m_phase,
						  m_actionPackEntry.m_stopBeforeEntry ? "stop" : "continue");
		}
	}

	if (const CatmullRom* pPathSpline = m_hPathSpline.ToCatmullRom())
	{
		pDout->Printf("%sPath Spline:        %s\n", preamble, pPathSpline->GetBareName());
		pDout->Printf("%sSpline Arc:         [%0.3f -> %0.3f @ %0.3f (final: %0.3f)] %s\n",
					  preamble,
					  m_curPathSplineArcStart,
					  m_curPathSplineArcGoal,
					  m_pathSplineArcStep,
					  m_finalPathSplineArc,
					  m_beginSplineAdvancement ? "Advancing" : "");
	}

	const NavControl* pNavCon = pNavChar ? pNavChar->GetNavControl() : nullptr;

	if (m_command.PathFindRequired() && pNavCon)
	{
		const U32F baseTraversalSkillMask = pNavCon->GetTraversalSkillMask();

		const U32F allowedTraversalSkillMask = FALSE_IN_FINAL_BUILD(g_navCharOptions.m_forceNoNavTaps)
												   ? 0
												   : m_command.GetMoveArgs().m_allowedTraversalSkillMask;

		const U32F traversalSkillMask = baseTraversalSkillMask & allowedTraversalSkillMask;

		if (traversalSkillMask != baseTraversalSkillMask)
		{
			const U32F lostSkillMask = baseTraversalSkillMask & ~traversalSkillMask;
			StringBuilder<256> lostSkills;

			if (0 == traversalSkillMask)
			{
				lostSkills.format("ALL!");
			}
			else if (g_ndConfig.m_pGetTraversalSkillMaskString)
			{
				(*g_ndConfig.m_pGetTraversalSkillMaskString)(lostSkillMask, &lostSkills);
			}
			else
			{
				lostSkills.clear();
			}

			pDout->Printf("%sCustom TAP Mask:    0x%x\n", preamble, traversalSkillMask);
			pDout->Printf("%sLost TAP Skills:    %s\n", preamble, lostSkills.c_str());
		}
	}

	const NavMoveArgs& moveArgs = m_command.GetMoveArgs();
	if (moveArgs.m_pathRadius >= 0.0f)
	{
		pDout->Printf("%sPath Find Radius:   %0.1fm\n", preamble, moveArgs.m_pathRadius);
	}

	if (const Character* pChaseChar = moveArgs.m_hChaseChar.ToProcess())
	{
		pDout->Printf("%sChase Char:         %s [inst. id: %d]\n",
					  preamble,
					  pChaseChar->GetName(),
					  pNavChar->GetProcessId());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::DebugDumpToStream(DoutBase* pDout) const
{
	STRIP_IN_FINAL_BUILD;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	// const bool isBusy = m_pNavChar->GetNdAnimationControllers()->IsBusy();
	pDout->Printf("NavStateMachine [%s]:\n", m_pNavChar->GetName());
	pDout->Printf("  CommandStatus:        %s\n", NavCommand::GetStatusName(GetStatus()));
	pDout->Printf("  Can Process Commands: %s\n", CanProcessCommands() ? "true" : "false");
	pDout->Printf("  Can Process Move Commands: %s\n", CanProcessMoveCommands() ? "true" : "false");

	if (m_activeContext.IsValid())
	{
		pDout->Printf("  ActiveCommand:\n");
		m_activeContext.DebugPrint(m_pNavChar, pDout, "    ");
	}

	if (m_nextContext.IsValid())
	{
		pDout->Printf("  NextCommand:\n");
		m_nextContext.DebugPrint(m_pNavChar, pDout, "    ");
	}

	if (m_queuedCommand.IsValid())
	{
		pDout->Printf("  QueuedCommand:        ");
		m_queuedCommand.DebugPrint(pDout, g_navCharOptions.m_displayNavStateSourceInfo);
	}

	pDout->Printf("  State:                %s\n", GetStateName("<none>"));

	pDout->Printf("  PrevStates:           ");
	const char* spacer = "";
	for (U32F i = 0; i < ARRAY_COUNT(m_pPrevStates); ++i)
	{
		if (!m_pPrevStates[i])
			continue;
		pDout->Printf("%s%s", spacer, m_pPrevStates[i]->GetStateName());
		spacer = ", ";
	}
	pDout->Printf("\n");

	if (m_pInterruptedState)
	{
		pDout->Printf("  InterruptedState:     %s", m_pInterruptedState->GetStateName());

		if (g_navCharOptions.m_displayNavStateSourceInfo)
		{
			pDout->Printf(" [%s:%d %s]", m_interruptSourceFile, m_interruptSourceLine, m_interruptSourceFunc);
		}

		pDout->Printf("\n");
	}

	pDout->Printf("  Path Status:          %s, %s%s%s%s[Kicked: %d Processed: %d]\n",
				  m_pathStatus.m_pathFound ? "Found" : "Not Found",
				  m_pathStatus.m_pathUpdated ? "Updated, " : "",
				  m_pathStatus.m_suspended ? "Suspended, " : "",
				  m_pathStatus.m_ignoreInFlightResults ? "Ignored, " : "",
				  m_shouldUpdatePath ? "Want New Path, " : "",
				  m_pathStatus.m_kickedCommandId,
				  m_pathStatus.m_processedCommandId);

	pDout->Printf("  StallTime:            %.2f\n", ToSeconds(GetStallTimeElapsed()));

	const ActionPack* pCurrAp = m_hCurrentActionPack.ToActionPack();
	const ActionPack* pPathAp = m_pathStatus.m_hTraversalActionPack.ToActionPack();

	pDout->Printf("  CurrAP:               %s (0x%x)\n",
				  pCurrAp ? pCurrAp->GetName() : "nullptr",
				  m_hCurrentActionPack.GetMgrId());
	pDout->Printf("  PathAP:               %s (0x%x)\n",
				  pPathAp ? pPathAp->GetName() : "nullptr",
				  m_pathStatus.m_hTraversalActionPack.GetMgrId());

	NavLocation startLoc;
	ActionPackHandle hPreferredTap;
	float tapBias;
	if (GetPathFindStartLocation(&startLoc, &hPreferredTap, &tapBias))
	{
		const ActionPack* pPrefAp = hPreferredTap.ToActionPack();

		pDout->Printf("  pPrefAp:              %s (0x%x) @ %0.3f\n",
					  pPrefAp ? pPrefAp->GetName() : "nullptr",
					  hPreferredTap.GetMgrId(),
					  tapBias);
	}

	pDout->Printf("  ActionPackUsageState: %s\n", GetActionPackUsageStateName(GetActionPackUsageState()));
	pDout->Printf("  ActionPackUsageType:  %s\n", ActionPack::GetTypeName(GetActionPackUsageType()));

	if (m_activeContext.IsValid() && m_activeContext.IsInProgress() && m_activeContext.m_hPathSpline.IsValid())
	{
		pDout->Printf("  Cur Spline Arc:       %.2f\n", m_activeContext.m_curPathSplineArcStart);
	}

	pDout->Printf("  Req. MotionType:      %s\n", GetMotionTypeName(m_pNavChar->GetRequestedMotionType()));

	if (const NavControl* pNavControl = m_pNavChar->GetNavControl())
	{
		float movingRadius, idleRadius, pathRadius;
		const float curRadius = pNavControl->GetNavAdjustRadiusConfig(movingRadius, idleRadius, pathRadius);
		pDout->Printf("  Nav Radius:           %0.3fm [moving: %0.3fm, idle: %0.3fm, path: %0.3fm",
					  curRadius,
					  movingRadius,
					  idleRadius,
					  pathRadius);

		float subSysRadius = curRadius;

		if (const NdSubsystemAnimController* pActiveSubSys = m_pNavChar->GetActiveSubsystemController())
		{
			subSysRadius = pActiveSubSys->GetNavAdjustRadius(curRadius);
		}

		if (subSysRadius != curRadius)
		{
			pDout->Printf(", subsys: %0.3fm]\n", subSysRadius);
		}
		else
		{
			pDout->Printf("]\n");
		}

		const Nav::StaticBlockageMask obeyedStaticBlockers = pNavControl->GetObeyedStaticBlockers();
		StringBuilder<256> staticBlockers;
		if (obeyedStaticBlockers)
		{
			Nav::GetStaticBlockageMaskStr(obeyedStaticBlockers, &staticBlockers);
		}
		else
		{
			staticBlockers.format("<none>");
		}
		pDout->Printf("  Obeyed Static Blkrs:  %s\n", staticBlockers.c_str());
	}

	Nav::PathFindContext pathContext;
	m_pNavChar->SetupPathContext(pathContext);
	if (pathContext.m_pathCostTypeId)
	{
		pDout->Printf("  Pathfind Cost Type:   %s\n", DevKitOnly_StringIdToString(pathContext.m_pathCostTypeId));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	const Locator& parentSpace = m_pNavChar->GetParentSpace();
	const NavControl* pNavControl = m_pNavChar->GetNavControl();
	const Point myPosPs = m_pNavChar->GetTranslationPs();

	PrimServerWrapper psPrim(parentSpace);
	psPrim.EnableHiddenLineAlpha();
	psPrim.SetDuration(kPrimDuration1FramePauseable);

	if (g_navCharOptions.m_displayPath)
	{
		Locator modParentSpace = parentSpace;
		const Vector vo		   = Vector(0.0f, 0.3f, 0.0f);
		modParentSpace.SetPos(modParentSpace.Pos() + vo);

		NavLocation startLoc;
		ActionPackHandle hPreferredTap;
		float tapBias;
		const bool startLocValid = GetPathFindStartLocation(&startLoc, &hPreferredTap, &tapBias);

		if (startLocValid)
		{
			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
			const Point pfStartWs = startLoc.GetPosWs();
			g_prim.Draw(DebugCross(pfStartWs, 0.2f, kColorCyan, kPrimEnableHiddenLineAlpha, "pf start"));
		}

		const NavCommandContext* pCmdContext = GetCommandContext(m_pathStatus.m_kickedCommandId);

		const float cmdRadius = pCmdContext ? pCmdContext->m_command.GetMoveArgs().m_pathRadius : -1.0f;
		const Nav::PathFindContext pfContext = pNavControl->GetPathFindContext(m_pNavChar, parentSpace);
		const float drawRadius = (cmdRadius >= 0.0f) ? cmdRadius : pfContext.m_pathRadius;

		if (m_pathStatus.m_pathPs.IsValid())
		{
			PathWaypointsEx::ColorScheme cs;
			cs.m_groundLeg0 = kColorPurple;
			cs.m_groundLeg1 = kColorCyan;

			const Point navPosPs = m_pNavChar->GetNavigationPosPs();
			psPrim.DrawSphere(navPosPs, 0.1f, kColorRedTrans);
			psPrim.DrawString(navPosPs, "navPos", kColorRedTrans, 0.5f);

			m_pathStatus.m_pathPs.DebugDraw(modParentSpace, false, drawRadius, cs);

			const float pathAgeSec = ToSeconds(m_pNavChar->GetCurTime() - m_pathStatus.m_lastPathIssuedTime);

			StringBuilder<256> desc;
			desc.append_format("Last Path (%0.1f sec ago)", pathAgeSec);

			psPrim.DrawString(m_pathStatus.m_pathPs.GetWaypoint(0) + vo, desc.c_str(), kColorWhiteTrans, 0.5f);
		}

		if (m_pathStatus.m_postTapPathPs.IsValid())
		{
			PathWaypointsEx::ColorScheme cs;
			cs.m_groundLeg0 = kColorYellow;
			cs.m_groundLeg1 = kColorBlue;

			m_pathStatus.m_postTapPathPs.DebugDraw(modParentSpace, true, drawRadius, cs);
		}
	}

	const Point curPosPs = m_pNavChar->GetNavigationPosPs();
	const bool isBusy	 = m_pNavChar->GetNdAnimationControllers()->IsBusy();

	StringBuilder<256> statusStr;

	statusStr.clear();

	if (const INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController())
	{
		statusStr.append_format("%s", pNavAnimController->GetDebugStatusStr());
	}
	else
	{
		statusStr.append_format("[Unknown Nav] ");
	}

	if (isBusy)
	{
		statusStr.append_format("BUSY ");
	}

	statusStr.append_format("%s ", GetStateDesc());

	const float dist = m_pNavChar->GetPathLength();

	if (IsCommandInProgress() && !IsCommandStopAndStand())
	{
		statusStr.append_format("%5.2f ", dist);
	}

	const float stallTimeSec = ToSeconds(GetStallTimeElapsed());
	if (stallTimeSec > 0.0f)
	{
		statusStr.append_format("[stall: %0.1fs]", stallTimeSec);
	}

	if (IsInterrupted())
	{
		BitArray128 controllerFlags;
		const U32F numControllers = m_pAnimationControllers->GetShouldInterruptNavigationForEach(controllerFlags);

		if (controllerFlags.BitCount())
		{
			statusStr.append_format("[");

			for (U32F ii = 0; ii < numControllers; ++ii)
			{
				if (controllerFlags.GetBit(ii))
				{
					statusStr.append_format(" %s", m_pAnimationControllers->GetControllerName(ii));
				}
			}

			statusStr.append_format(" ]");
		}
	}

	psPrim.DrawString(curPosPs + Vector(0.0f, 0.3f, 0.0f), statusStr.c_str(), isBusy ? kColorRed : kColorGreen, 0.75f);

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (IsCommandPending())
	{
		const NavCommand& pendingCmd = GetPendingCommand();

		if (const ActionPack* pAp = pendingCmd.GetGoalActionPack())
		{
			const Point pendingPosPs = m_pNavChar->GetParentSpace().UntransformPoint(pAp->GetRegistrationPointWs());
			psPrim.DrawString(pendingPosPs, "Pending AP", kColorBlueTrans, 0.75f);
			psPrim.DrawCross(pendingPosPs, 0.2f, kColorBlueTrans);
		}
		else
		{
			const Point pendingPosPs = parentSpace.UntransformPoint(pendingCmd.GetGoalLocation().GetPosWs());
			psPrim.DrawString(pendingPosPs, "Pending BF", kColorBlueTrans, 0.75f);
			psPrim.DrawCross(pendingPosPs, 0.2f, kColorBlueTrans);
			psPrim.DrawArrow(curPosPs, pendingPosPs, 0.5f, kColorBlueTrans);

			if (pendingCmd.GetMoveArgs().m_goalFaceDirValid)
			{
				psPrim.DrawArrow(pendingPosPs, pendingCmd.GetMoveArgs().m_goalFaceDirPs, 0.5f, kColorBlueTrans);
			}
		}
	}

	psPrim.DrawCross(curPosPs, 0.25f, kColorCyanTrans);

	const Point cur	 = curPosPs + Vector(0.0f, 0.1f, 0.0f);
	const Point next = GetNextDestPointPs() + Vector(0.0f, 0.1f, 0.0f);
	const Point goal = GetFinalDestPointPs() + Vector(0.0f, 0.1f, 0.0f);

	psPrim.DrawLine(cur, next, kColorBlueTrans);
	psPrim.DrawLine(next, goal, kColorBlueTrans);

	if (Dist(next, goal) > 0.2f)
	{
		StringBuilder<512> nextDesc;
		nextDesc.append(GetWaypointTypeName(m_activeContext.m_nextWaypointType));

		if (((m_activeContext.m_nextWaypointType == kWaypointAPDefaultEntry)
			 || (m_activeContext.m_nextWaypointType == kWaypointAPResolvedEntry))
			&& (m_activeContext.m_actionPackEntry.m_entryAnimId != INVALID_STRING_ID_64
				&& m_activeContext.m_actionPackEntry.m_hResolvedForAp == m_activeContext.m_hNextActionPack))
		{
			nextDesc.append("\n");
			nextDesc.append_format("%s @ %0.3f (%s)",
								   DevKitOnly_StringIdToString(m_activeContext.m_actionPackEntry.m_entryAnimId),
								   m_activeContext.m_actionPackEntry.m_phase,
								   m_activeContext.m_actionPackEntry.m_stopBeforeEntry ? "stop" : "continue");
		}

		psPrim.DrawString(next, nextDesc.c_str(), kColorBlue, 0.65f);
		psPrim.DrawCross(next, 0.25f, kColorBlue);
		const Color facingClr = m_activeContext.m_command.GetMoveArgs().m_goalFaceDirValid ? kColorBlue : kColorCyan;
		psPrim.DrawArrow(next, GetLocalZ(GetWaypointContract(m_activeContext).m_rotPs), 0.5f, facingClr);

		if (m_activeContext.m_nextWaypointType != kWaypointFinalGoal)
		{
			psPrim.DrawString(goal, GetWaypointTypeName(kWaypointFinalGoal), kColorBlue, 0.75f);
			psPrim.DrawCross(goal, 0.25f, kColorBlue);
		}
	}
	else if (m_activeContext.m_nextWaypointType != kWaypointFinalGoal)
	{
		psPrim.DrawString(goal,
						  StringBuilder<256>("%s-%s",
											 GetWaypointTypeName(kWaypointFinalGoal),
											 GetWaypointTypeName(m_activeContext.m_nextWaypointType))
							  .c_str(),
						  kColorBlue,
						  0.75f);
		psPrim.DrawCross(goal, 0.25f, kColorBlue);
	}
	else
	{
		psPrim.DrawString(goal, GetWaypointTypeName(kWaypointFinalGoal), kColorBlue, 0.75f);
		psPrim.DrawCross(goal, 0.25f, kColorBlue);
	}

	if (m_activeContext.m_command.GetMoveArgs().m_goalFaceDirValid)
	{
		psPrim.DrawArrow(goal, m_activeContext.m_command.GetMoveArgs().m_goalFaceDirPs, 0.5f, kColorBlue);
	}
	else if (IsNextWaypointActionPackEntry())
	{
		const WaypointContract wc = GetWaypointContract(m_activeContext);
		const Vector apDirPs	  = GetLocalZ(wc.m_rotPs);
		psPrim.DrawArrow(goal, apDirPs, 0.5f, kColorBlue);
	}

	if (!IsState(kStateStopped) && !IsState(kStateUsingActionPack) && !IsState(kStateUsingTraversalActionPack)
		&& !IsState(kStateInterrupted))
	{
		const ActionPack* pAp = GetActionPack();
		Color apColor		  = kColorBlue;
		if (!pAp)
		{
			pAp		= GetGoalActionPack();
			apColor = kColorGreen;
		}
		if (pAp)
		{
			ActionPackResolveInput input = MakeApResolveInput(m_activeContext.m_command, pAp);

			apColor.SetA(0.25f);
			m_pNavChar->DebugDrawActionPack(pAp, &input);

			const Point apPosWs = pAp->GetBoundFrame().GetLocatorWs().Pos();
			const Point apPosPs = m_pNavChar->GetParentSpace().UntransformPoint(apPosWs);
			psPrim.DrawLine(cur, apPosPs, Color(0.0f, 0.0f, 0.0f, 0.25f), apColor);
		}
	}

	const CatmullRom* pPathSpline = m_activeContext.m_hPathSpline.ToCatmullRom();
	if (IsCommandInProgress() && pPathSpline)
	{
		const float totalLen = pPathSpline->GetTotalArcLength();
		CatmullRom::DrawOptions opts;
		opts.m_minSegment = -1;
		opts.m_maxSegment = -1;
		opts.m_drawAfterGlobalT = m_activeContext.m_curPathSplineArcStart / totalLen;
		opts.m_drawBeforeGlobalT = m_activeContext.m_curPathSplineArcGoal / totalLen;
		opts.m_drawClosestPoint = true;
		pPathSpline->Draw(&opts);
	}

	if (IsState(kStateUsingTraversalActionPack))
	{
		if (const TraversalActionPack* pAp = GetTraversalActionPack())
		{
			m_pNavChar->DebugDrawActionPack(pAp);
		}
	}

	if (const BaseState* pCurState = GetState())
	{
		pCurState->DebugDraw();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ActionPack* NavStateMachine::GetGoalOrPendingActionPack() const
{
	if (IsCommandPending())
	{
		return GetPendingCommand().GetGoalActionPack();
	}

	return m_activeContext.m_command.GetGoalActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
uintptr_t NavStateMachine::GetGoalOrPendingApUserData() const
{
	if (IsCommandPending())
	{
		return GetPendingCommand().GetActionPackUserData();
	}

	return m_activeContext.m_command.GetActionPackUserData();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ActionPack* NavStateMachine::GetGoalActionPack() const
{
	return m_activeContext.m_command.GetGoalActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavStateMachine::GetNextDestPointPs() const
{
	Point destPosPs = kOrigin;

	if (m_activeContext.IsValid())
	{
		destPosPs = m_activeContext.m_nextNavPosPs;
	}
	else if (m_nextContext.IsValid())
	{
		destPosPs = m_nextContext.m_nextNavPosPs;
	}
	else if (m_pNavChar)
	{
		destPosPs = m_pNavChar->GetTranslationPs();
	}

	return destPosPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavStateMachine::GetFinalDestPointPs() const
{
	Point destPosPs = kOrigin;

	if (m_activeContext.IsValid())
	{
		destPosPs = m_activeContext.m_finalNavPosPs;
	}
	else if (m_nextContext.IsValid())
	{
		destPosPs = m_nextContext.m_finalNavPosPs;
	}
	else if (m_pNavChar)
	{
		destPosPs = m_pNavChar->GetTranslationPs();
	}

	return destPosPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::GetCanStrafe() const
{
	if (m_activeContext.IsValid())
		return m_activeContext.m_command.GetMoveArgs().m_strafeMode != DC::kNpcStrafeNever;

	if (m_nextContext.IsValid())
		return m_nextContext.m_command.GetMoveArgs().m_strafeMode != DC::kNpcStrafeNever;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NavStateMachine::GetGoalFaceDirPs() const
{
	Vector faceVecXzPs = m_pNavChar ? AsUnitVectorXz(GetLocalZ(m_pNavChar->GetRotationPs()), kUnitZAxis) : kUnitZAxis;
	if (m_activeContext.IsValid() && m_activeContext.m_command.GetMoveArgs().m_goalFaceDirValid)
	{
		faceVecXzPs = AsUnitVectorXz(m_activeContext.m_command.GetMoveArgs().m_goalFaceDirPs, faceVecXzPs);
	}
	else if (m_nextContext.IsValid() && m_nextContext.m_command.GetMoveArgs().m_goalFaceDirValid)
	{
		faceVecXzPs = AsUnitVectorXz(m_nextContext.m_command.GetMoveArgs().m_goalFaceDirPs, faceVecXzPs);
	}
	else if (m_pNavChar)
	{
		const Point facePosPs = m_pNavChar->GetFacePositionPs();
		if (m_activeContext.IsValid())
		{
			faceVecXzPs = AsUnitVectorXz(facePosPs - m_activeContext.m_finalNavPosPs, faceVecXzPs);
		}
		else if (m_nextContext.IsValid())
		{
			faceVecXzPs = AsUnitVectorXz(facePosPs - m_nextContext.m_finalNavPosPs, faceVecXzPs);
		}
	}

	return faceVecXzPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::ShouldSwapApReservations(const ActionPack* pAp) const
{
	if (!pAp || pAp->IsAvailableFor(m_pNavChar))
		return false;

	if (!pAp->IsReserved() || pAp->IsReservedBy(m_pNavChar))
		return false;

	const Process* pOtherProc = pAp->GetReservationHolder();
	if (!pOtherProc)
		return false;

	if (!pOtherProc->IsKindOf(g_type_NavCharacter))
		return false;

	const NavCharacter* pNavChar	 = (const NavCharacter*)pOtherProc;
	const NavStateMachine* pOtherNsm = pNavChar->GetNavStateMachine();
	if (!pOtherNsm)
		return false;

	if (pOtherNsm->GetActionPackUsageState() != kActionPackUsageReserved)
		return false;

	const float myPathLen	 = GetCurrentPathLength();
	const float otherPathLen = pOtherNsm->GetCurrentPathLength();

	if (myPathLen + 0.5f < otherPathLen)
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::SwapApReservations(const ActionPack* pAp)
{
	if (!pAp)
		return;

	Process* pOtherProc = pAp->GetReservationHolder();
	if (!pOtherProc)
		return;

	if (!pOtherProc->IsKindOf(g_type_NavCharacter))
		return;

	NavCharacter* pNavChar = static_cast<NavCharacter*>(pOtherProc);

	LogHdr("Booting '%s' off AP and swapping reservations\n", pNavChar->GetName());

	pNavChar->NotifyApReservationLost(pAp);

	if (SetCurrentActionPack(pAp, "SwapApReservations"))
	{
		SetNextWaypoint(m_activeContext,
						m_activeContext.m_nextNavGoal,
						kWaypointAPDefaultEntry,
						kNavGoalReachedTypeStop,
						"SwapApReservations");
	}
	else
	{
		LogHdr("Failed to set current AP to '");
		LogActionPack(pAp);
		LogMsg("'! Failing nav command.\n");

		StopPathFailed(m_activeContext, "AP Swap Reservation Failed");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::NotifyApReservationLost(const ActionPack* pAp)
{
	NAV_ASSERT(m_hCurrentActionPack.ToActionPack() == pAp);
	NAV_ASSERT(GetActionPackUsageState() == kActionPackUsageReserved);

	if (m_hCurrentActionPack.ToActionPack() != pAp)
		return;

	if (GetActionPackUsageState() != kActionPackUsageReserved)
		return;

	WaitForActionPack(m_activeContext, pAp, "NotifyApReservationLost");
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::ShouldSuspendPathFind() const
{
	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_disableNavJobScheduling))
	{
		return false;
	}

	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_forcePathSuspension))
	{
		return true;
	}

	if (m_activeContext.m_command.GetMoveArgs().m_hChaseChar.Assigned())
	{
		return false;
	}

	if (NavJobScheduler::Get().CanRunThisFrame(m_pNavChar))
	{
		return false;
	}
	else
	{
		return true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::UpdateCurrentPath(const PathWaypointsEx& srcPath, PathWaypointsEx* pPathPs, Vector* pDirPs) const
{
	PROFILE_AUTO(Navigation);

	NAV_ASSERT(pPathPs);
	NAV_ASSERT(pDirPs);

	const NavLocation myLoc = m_pNavChar->GetNavLocation();
	const Point myPosPs		= myLoc.GetPosPs();

	pPathPs->Clear();
	*pDirPs = kZero;

	if (!srcPath.IsValid())
	{
		return;
	}

	const U32F srcCount = srcPath.GetWaypointCount();

	pPathPs->AddWaypoint(myLoc);

	if (srcCount == 2)
	{
		pPathPs->AddWaypoint(srcPath, 1);

		const Point dirPosPs = pPathPs->GetEndWaypoint();
		*pDirPs = AsUnitVectorXz(dirPosPs - myPosPs, kZero);
		return;
	}

	float distToPath = kLargeFloat;
	I32F iCurLeg	 = -1;

	const float biasRadius = GetPathFindRadiusForCommand(m_activeContext);

	const Point closestPosPs = srcPath.ClosestPointXz(myPosPs, &distToPath, &iCurLeg, nullptr, biasRadius);

	const MotionConfig& mc = m_pNavChar->GetMotionConfig();

	const float advanceDist = Max(mc.m_pathRejoinDist - distToPath, 0.0f);

	I32F iNewCurLeg			= iCurLeg;
	const Point rejoinPosPs = srcPath.AdvanceAlongPathXz(closestPosPs, advanceDist, iCurLeg, &iNewCurLeg);

	NavLocation rejoinLocPs = myLoc;
	rejoinLocPs.UpdatePosPs(rejoinPosPs);

	iCurLeg = iNewCurLeg + 1;

	if (iCurLeg < srcCount)
	{
		const Vector firstDirPs	 = AsUnitVectorXz(rejoinPosPs - myPosPs, kZero);
		const Vector secondDirPs = AsUnitVectorXz(srcPath.GetWaypoint(iCurLeg) - rejoinPosPs, firstDirPs);

		const float dotP = Dot(firstDirPs, secondDirPs);

		if (dotP < 0.9f)
		{
			pPathPs->AddWaypoint(rejoinLocPs);
		}
	}
	else
	{
		pPathPs->AddWaypoint(rejoinLocPs);
	}

	for (I32F iLeg = iCurLeg; iLeg < srcCount; ++iLeg)
	{
		pPathPs->AddWaypoint(srcPath, iLeg);
	}

	{
		//const float lookAheadDist = Max(Length(m_pNavChar->GetVelocityPs()) * 0.5f, 1.25f);
		//const Point dirPosPs = pPathPs->GetPointAtDistance(lookAheadDist);
		const Vector rejoinDirPs = AsUnitVectorXz(rejoinPosPs - myPosPs, kZero);
		*pDirPs = rejoinDirPs;

		//g_prim.Draw(DebugArrow(myPosPs + kUnitYAxis, rejoinDirPs, kColorOrange, 0.5f, kPrimEnableHiddenLineAlpha));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::TryProcessResume()
{
	if (!m_resumeStateRequested)
		return false;

	m_resumeStateRequested = false;

	if (m_pNavChar->IsAnimationControlled())
	{
		LogHdr("TryProcessResume() called but am still animation controlled, ignoring request\n");
		return false;
	}

	LogHdr("Resuming Interrupted State & resetting animation controllers (resume state: %s)\n",
		   m_pInterruptedState ? m_pInterruptedState->GetStateName() : "<null>");

	// don't reset the nav anim controllers because it is in an interrupted state with important state data about how to handoff back into AI control
	BitArray128 bitArrayExclude;
	m_pAnimationControllers->GetNavAnimControllerIndices(bitArrayExclude);
	m_pAnimationControllers->ResetExcluding(bitArrayExclude);

	m_interruptSourceFile = "<invalid-file>";
	m_interruptSourceLine = -1;
	m_interruptSourceFunc = "<invalid-func>";

	const StringId64 interruptedStateId = m_pInterruptedState ? m_pInterruptedState->GetStateId()
															  : INVALID_STRING_ID_64;
	m_pInterruptedState = nullptr;

	bool shouldResumeMoving = false;

	switch (interruptedStateId.GetValue())
	{
	case SID_VAL("Stopped"):
		{
			// if we were interrupted while stopped,
			// but we were blocked or waiting on an action pack,
			switch (m_activeContext.m_nextWaypointType)
			{
			case kWaypointAPWait:
			case kWaypointAPDefaultEntry:
			case kWaypointAPResolvedEntry:
				{
					// pretend we were moving so we resume properly
					shouldResumeMoving = true;
				}
				break;

			case kWaypointFinalGoal:
				// we really were stopped
				break;

			default:
				// any other value here shouldn't happen
				LogHdr("Resuming from state stopped to an unexpected waypoint type '%s'\n",
					   GetWaypointTypeName(m_activeContext.m_nextWaypointType));
				MailNpcLogTo(m_pNavChar,
							 "john_bellomy@naughtydog.com",
							 "Resuming from state stopped to an unexpected waypoint type",
							 FILE_LINE_FUNC);
				DEBUG_HALT();
				break;
			}
		}
		break;

	case SID_VAL("MovingAlongPath"):
	case SID_VAL("SteeringAlongPath"):
	case SID_VAL("UsingTraversalActionPack"):
	case SID_VAL("EnteringActionPack"):
	case SID_VAL("StartingToExitActionPack"):
	case SID_VAL("ExitingActionPack"):
		shouldResumeMoving = true;
		break;
	}

	if (!shouldResumeMoving && (m_activeContext.m_status == NavContextStatus::kWaitingForPath))
	{
		shouldResumeMoving = true;
	}

	if (shouldResumeMoving && !m_activeContext.IsValid())
	{
		shouldResumeMoving = false;
	}

	if (shouldResumeMoving && m_abandonInterruptedCommand)
	{
		shouldResumeMoving = false;
		m_abandonInterruptedCommand = false;
	}

	NavControl* pNavControl = m_pNavChar->GetNavControl();
	NavAnimHandoffDesc handoff;

	if (shouldResumeMoving)
	{
		LogHdr("Resuming a Move\n");

		NAV_ASSERT(IsReasonable(m_activeContext.m_finalNavGoal.GetPosPs()));

		// Reset the nav control
		{
			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

			pNavControl->ResetNavMesh(nullptr);
			const Point navPosPs = m_pNavChar->GetNavigationPosPs();
			pNavControl->UpdatePs(navPosPs, m_pNavChar);
		}

		ResumeMoving(true, false);
	}
	else if (m_pNavChar->PeekValidHandoff(&handoff))
	{
		if (handoff.m_motionType < kMotionTypeMax)
		{
			LogHdr("Resuming navigation with a valid moving handoff, going to blind moving\n");

			INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController();

			NavAnimStartParams startParams;
			startParams.m_moveArgs.m_motionType = handoff.m_motionType;
			startParams.m_moveArgs.m_goalReachedType = kNavGoalReachedTypeContinue;
			startParams.m_moveDirPs = SafeNormalize(m_pNavChar->GetVelocityPs(), GetLocalZ(m_pNavChar->GetRotationPs()));
			startParams.m_waypoint.m_motionType = handoff.m_motionType;
			startParams.m_waypoint.m_reachedType = kNavGoalReachedTypeContinue;
			startParams.m_waypoint.m_rotPs = m_pNavChar->GetRotationPs();

			pNavAnimController->StartNavigating(startParams);

			GoMovingBlind();
			m_shouldUpdatePath = false;
		}
		else
		{
			LogHdr("Resuming navigation to stopped state\n");

			INavAnimController* pNavAnimController = m_pNavChar->GetActiveNavAnimController();

			NavAnimStopParams stopParams;
			stopParams.m_goalRadius = m_activeContext.m_command.GetGoalRadius();
			stopParams.m_waypoint.m_motionType	= kMotionTypeMax;
			stopParams.m_waypoint.m_reachedType = kNavGoalReachedTypeStop;
			stopParams.m_waypoint.m_rotPs		= m_pNavChar->GetRotationPs();

			pNavAnimController->StopNavigating(stopParams);

			GoStopped();
			m_shouldUpdatePath = false;
		}
	}
	else
	{
		if (!IsCommandPending())
		{
			LogHdr("Resuming navigation with invalid/non-moving handoff (and no pending cmd), forcing nav anim controller to stop\n");

			StopMovingImmediately(true, false, "TryProcessResume-NoHandoff");
		}
		else
		{
			GoStopped();
		}
	}

	GetStateMachine().TakeStateTransition();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static NdAtomicLock s_apSwapLock;
void NavStateMachine::CheckForApReservationSwap()
{
	const ActionPack* pActionPack = m_activeContext.m_hNextActionPack.ToActionPack();
	if (!pActionPack)
		return;

	if (m_activeContext.m_nextWaypointType != kWaypointAPWait)
		return;

	AtomicLockJanitor lock(&s_apSwapLock, FILE_LINE_FUNC);

	if (ShouldSwapApReservations(pActionPack))
	{
		SwapApReservations(pActionPack);

		if (NavStateMachine::BaseState* pState = GetState())
		{
			pState->OnApReservationSwapped();
		}

		GetStateMachine().TakeStateTransition();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackResolveInput NavStateMachine::MakeApResolveInput(const NavCommand& navCmd, const ActionPack* pActionPack) const
{
	const NavCharacter* pNavChar = m_pNavChar;

	ActionPackResolveInput input;

	input.m_frame	   = pNavChar->GetBoundFrame();
	input.m_moving	   = pNavChar->IsMoving();
	input.m_velocityPs = pNavChar->GetVelocityPs();

	if (!input.m_moving)
	{
		const StringId64 nsmStateId = GetStateId();
		switch (nsmStateId.GetValue())
		{
		case SID_VAL("MovingAlongPath"):
		case SID_VAL("SteeringAlongPath"):
			input.m_moving = true;
			break;
		}
	}

	const NavMoveArgs& moveArgs = navCmd.GetMoveArgs();

	input.m_motionType	  = navCmd.GetMotionType();
	input.m_mtSubcategory = navCmd.GetMtSubcategory();

	//if (!input.m_mtSubcategory || input.m_mtSubcategory == SID("default"))
	if (input.m_mtSubcategory == INVALID_STRING_ID_64)
	{
		// NB: Maybe this should be GetDefaultMotionTypeSubcategory() ? Will need to check with Michal about
		// preferred AP entry behavior.
		input.m_mtSubcategory = SID("normal");
	}

	if (pActionPack && (pActionPack->GetMgrId() == navCmd.GetGoalActionPackMgrId()))
	{
		input.m_apUserData = navCmd.GetActionPackUserData();
	}

	switch (moveArgs.m_strafeMode)
	{
	case DC::kNpcStrafeAlways:
		input.m_strafing = true;
		break;

	case DC::kNpcStrafeNever:
		input.m_strafing = false;
		break;
	}

	return input;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::ValidateActionPackUsage() const
{
	STRIP_IN_FINAL_BUILD;

	switch (m_actionPackUsageState)
	{
	case kActionPackUsageNone:
		if (m_hCurrentActionPack.IsValid())
		{
			MailNpcLogTo(m_pNavChar, "john_bellomy@naughtydog.com", "AP usage none but still have AP", FILE_LINE_FUNC);
		}
		NAV_ASSERT(!m_hCurrentActionPack.IsValid());
		break;

	case kActionPackUsageReserved:
		if (!m_hCurrentActionPack.IsValid())
		{
			MailNpcLogTo(m_pNavChar, "john_bellomy@naughtydog.com", "AP reserved/entered without AP", FILE_LINE_FUNC);
		}
		NAV_ASSERT(m_hCurrentActionPack.IsValid());
		break;

	case kActionPackUsageEntered:
		if (!m_hCurrentActionPack.IsValid())
		{
			MailNpcLogTo(m_pNavChar, "john_bellomy@naughtydog.com", "AP reserved/entered without AP", FILE_LINE_FUNC);
		}
		NAV_ASSERT(m_hCurrentActionPack.IsValid());
		if (const ActionPack* pAp = m_hCurrentActionPack.ToActionPack())
		{
			if (FALSE_IN_FINAL_BUILD(!pAp->IsReservedBy(m_pNavChar)))
			{
				LogHdr("ActionPack ");
				LogActionPack(pAp);
				LogMsg(" says I'm not the reservation holder when I should be\n");

				MailNpcLogTo(m_pNavChar,
							 "john_bellomy@naughtydog.com",
							 "AP says I'm not reservation holder",
							 FILE_LINE_FUNC);
			}
			NAV_ASSERT(pAp->IsReservedBy(m_pNavChar));
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::WaitForActionPack(NavCommandContext& cmdContext, const ActionPack* pActionPack, const char* logStr)
{
	NAV_ASSERT(pActionPack);

	LogHdr("WaitForActionPack[%s]: ", logStr);
	LogActionPack(pActionPack);
	LogMsg("\n");

	ActionPackEntryDef& apEntry = cmdContext.m_actionPackEntry;
	ActionPackResolveInput defResolveInfo = MakeApResolveInput(cmdContext.m_command, pActionPack);

	ActionPackController* pActionPackController = m_pAnimationControllers->GetControllerForActionPack(pActionPack);

	if (!pActionPackController || !pActionPackController->ResolveDefaultEntry(defResolveInfo, pActionPack, &apEntry))
	{
		// couldn't even get a new default entry anim, so use defaults
		ActionPackEntryDefSetupDefaults(&apEntry, pActionPack, m_pNavChar);
	}

	// release the action pack and wait for it to become available again
	SetCurrentActionPack(nullptr, logStr);
	SetNextActionPack(cmdContext, pActionPack, logStr);
	SetNextWaypoint(cmdContext, apEntry.m_entryNavLoc, kWaypointAPWait, kNavGoalReachedTypeStop, logStr);
}

/************************************************************************/
/* Base State                                                           */
/************************************************************************/
void NavStateMachine::BaseState::OnEnter()
{
	NavStateMachine* pSelf = GetSelf();

	LogHdr("NavState change [%s -> %s]", pSelf->GetPrevStateName("<none>"), m_pDesc->GetStateName());

	if (const ActionPack* pAp = pSelf->GetActionPack())
	{
		LogMsg(" (");
		LogActionPack(pAp);
		LogMsg(")");
	}

	LogMsg("\n");

	const Fsm::StateDescriptor* pCurStateDesc = pSelf->GetStateMachine().GetStateDescriptor();

	if (pCurStateDesc != pSelf->m_pPrevStates[0])
	{
		// Shift all actions down. Nice and slow. :-)
		pSelf->m_pPrevStates[4] = pSelf->m_pPrevStates[3];
		pSelf->m_pPrevStates[3] = pSelf->m_pPrevStates[2];
		pSelf->m_pPrevStates[2] = pSelf->m_pPrevStates[1];
		pSelf->m_pPrevStates[1] = pSelf->m_pPrevStates[0];
		pSelf->m_pPrevStates[0] = pCurStateDesc;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::BaseState::GetPathFindStartLocation(NavLocation* pStartLocOut,
														  ActionPackHandle* phPreferredTapOut,
														  float* pTapBiasOut) const
{
	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;

	NavLocation startLoc;
	ActionPackHandle hPreferredTap = pSelf->m_hCurrentActionPack;

	if (pSelf->m_actionPackUsageState == kActionPackUsageNone)
	{
		hPreferredTap = pSelf->m_pathStatus.m_hTraversalActionPack;
	}

	bool valid = false;

	if (NavControl* pNavCon = pNavChar->GetNavControl())
	{
		startLoc = pNavCon->GetNavLocation();
		valid	 = true;
	}

	const float pathDist = pSelf->GetCurrentPathLength();
	const float tapBias	 = LerpScale(5.0f, 15.0f, -50.0f, -3.0f, pathDist);

	if (pStartLocOut)
		*pStartLocOut = startLoc;
	if (phPreferredTapOut)
		*phPreferredTapOut = hPreferredTap;
	if (pTapBiasOut)
		*pTapBiasOut = tapBias;

	return valid;
}

/************************************************************************/
/* Stopped                                                              */
/************************************************************************/

class NavStateMachine::Stopped : public NavStateMachine::BaseState
{
	virtual void OnEnter() override;
	virtual void Update() override;
	virtual bool CanProcessCommands() const override { return true; }
	virtual void OnApReservationSwapped() override;
};

FSM_STATE_REGISTER(NavStateMachine, Stopped, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Stopped::OnEnter()
{
	BaseState::OnEnter();

	NavStateMachine* pSelf = GetSelf();
	pSelf->m_pNavChar->OnStopped();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Stopped::Update()
{
	NavStateMachine* pSelf = GetSelf();

	PROFILE(AI, NSM_Stopped);
	switch (pSelf->m_activeContext.m_nextWaypointType)
	{
	case kWaypointAPWait:
		{
			if (GetActionPackUsageState() != kActionPackUsageNone)
			{
				MailNpcLogTo(pSelf->m_pNavChar,
							 "john_bellomy@naughtydog.com",
							 "In AP Wait but holding a reservation",
							 FILE_LINE_FUNC);
			}

			NAV_ASSERT(GetActionPackUsageState() == kActionPackUsageNone);

			if (const ActionPack* pActionPack = pSelf->m_activeContext.m_hNextActionPack.ToActionPack())
			{
				// if action pack we're waiting for becomes available,
				if (const NavCharacter* pBlockingChar = pSelf->GetApBlockingChar())
				{
					LogHdr("UpdateStateStopped: Next AP blocked by '%s' PATH FAILED\n", pBlockingChar->GetName());
					pSelf->StopPathFailed(pSelf->m_activeContext, "Next AP is blocked");
				}
				else if (pActionPack->IsAvailableFor(pSelf->m_pNavChar))
				{
					pSelf->ResumeMoving(true, false);
				}
				else if (!pActionPack->IsEnabled())
				{
					const TraversalActionPack* pTap = (pActionPack->GetType() == ActionPack::kTraversalActionPack)
														  ? (const TraversalActionPack*)pActionPack
														  : nullptr;
					if (!pTap || pTap->HasUsageTimerExpired())
					{
						pSelf->ResumeMoving(true, false);
					}
				}
			}
			else
			{
				// action pack we were waiting for blinked out of existence
				// Try to figure out whether it was a Tap or the goalAp
				// if we should have a goal Ap
				if (pSelf->m_activeContext.m_command.GetType() == NavCommand::kMoveToActionPack)
				{
					// if goal action pack still exists,
					if (const ActionPack* pGoalAp = GetGoalActionPack())
					{
						// it must have been a TAP that vanished because our goal still exists
						pSelf->ResumeMoving(true, false);
					}
					else if (pSelf->m_activeContext.IsInProgress())
					{
						// our goal has vanished, command failed
						SetContextStatus(pSelf->m_activeContext,
										 NavContextStatus::kFailed,
										 "Stopped AP Wait Disappeared");
					}
				}
				else
				{
					// it must have been a TAP that vanished because we're going to a point
					pSelf->ResumeMoving(true, false);
				}
			}

			const float lastPathTimeElapsed = ToSeconds(pSelf->m_pNavChar->GetCurTime()
														- pSelf->m_pathStatus.m_lastPathIssuedTime);
			if (lastPathTimeElapsed > 0.5f)
			{
				LogHdr("UpdateStateStopped: waiting for path to unblock - new path!\n");
				pSelf->m_shouldUpdatePath = true;
			}
		}
		break;

	case kWaypointAPDefaultEntry:
	case kWaypointAPResolvedEntry:
		if (!pSelf->IsFirstPathForCommandPending()
			&& (pSelf->m_activeContext.m_status != NavContextStatus::kWaitingForPath))
		{
			LogHdr("UpdateStateStopped: now have a valid AP waypoint, resuming moving\n");
			pSelf->ResumeMoving(true, false);
		}
		break;

	case kWaypointFinalGoal:
		if (pSelf->m_activeContext.IsInProgress() && pSelf->m_activeContext.IsCommandStopAndStand())
		{
			SetContextStatus(pSelf->m_activeContext,
							 NavContextStatus::kSucceeded,
							 "UpdateStateStopped Fallthrough Catch");
		}

		if ((pSelf->m_nextContext.m_status == NavContextStatus::kWaitingForPath) && !pSelf->m_shouldUpdatePath)
		{
			pSelf->m_shouldUpdatePath = true;
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Stopped::OnApReservationSwapped()
{
	NavStateMachine* pSelf = GetSelf();

	pSelf->m_shouldUpdatePath = true;
	pSelf->m_pathStatus.m_lastPathFoundTime
		= pSelf->m_pNavChar->GetCurTime(); // reset this time so that it doesn't think the dynamic path has been failing
}

/************************************************************************/
/* Moving (following path)                                              */
/************************************************************************/

class NavStateMachine::MovingAlongPath : public NavStateMachine::BaseState
{
	virtual void OnEnter() override;
	virtual void Update() override;
	virtual void PostRootLocatorUpdate() override;
	virtual bool CanProcessCommands() const override { return true; }

	bool TryCompleteCommand();
};

FSM_STATE_REGISTER(NavStateMachine, MovingAlongPath, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::MovingAlongPath::OnEnter()
{
	BaseState::OnEnter();

	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;
	NavCommandContext& activeContext = pSelf->m_activeContext;

	if (pSelf->NextWaypointIsForAp(activeContext))
	{
		if (const ActionPack* pNextAp = activeContext.m_hNextActionPack.ToActionPack())
		{
			NdAnimationControllers* pAnimControllers = GetAnimationControllers();
			ActionPackController* pActionPackController = pAnimControllers->GetControllerForActionPack(pNextAp);

			ActionPackResolveInput defResolveInfo = pSelf->MakeApResolveInput(activeContext.m_command, pNextAp);

			ActionPackEntryDef& apEntry = activeContext.m_actionPackEntry;

			if (pActionPackController && pActionPackController->UpdateEntry(defResolveInfo, pNextAp, &apEntry))
			{
				SetNextWaypoint(activeContext,
								apEntry.m_entryNavLoc,
								activeContext.m_nextWaypointType,
								apEntry.m_stopBeforeEntry ? kNavGoalReachedTypeStop : kNavGoalReachedTypeContinue,
								"MovingAlongPath::OnEnter");

			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::MovingAlongPath::TryCompleteCommand()
{
	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;
	INavAnimController* pNavAnimController = pNavChar->GetActiveNavAnimController();

	const bool animHasArrived = pNavAnimController->HasArrivedAtGoal();
	const bool animIsBusy	  = pNavAnimController->IsBusy();

	NavCommandContext& activeContext = pSelf->m_activeContext;

	bool canArrive = false;
	switch (activeContext.m_nextWaypointType)
	{
	case kWaypointAPDefaultEntry:
	case kWaypointAPResolvedEntry:
		{
			if (activeContext.m_actionPackEntry.IsValidFor(pSelf->m_hCurrentActionPack))
			{
				canArrive = true;
			}
		}
		break;

	case kWaypointAPWait:
	case kWaypointFinalGoal:
		{
			canArrive = true;
		}
		break;
	}

	bool completed = false;

	if (canArrive && animHasArrived && !animIsBusy)
	{
		switch (activeContext.m_nextWaypointType)
		{
		case kWaypointAPWait:
			if (!animIsBusy)
			{
				pSelf->GoStopped();
				completed = true;
			}
			break;

		case kWaypointAPDefaultEntry:
		case kWaypointAPResolvedEntry:
			{
				ActionPack* pActionPack = pSelf->m_hCurrentActionPack.ToActionPack();

				if (pActionPack && pActionPack->IsEnabled())
				{
					bool canEnter = false;

					if (pActionPack->IsReservedBy(pNavChar) && pActionPack->IsAvailableFor(pNavChar))
					{
						canEnter = true;
						if (TraversalActionPack* pTap = TraversalActionPack::FromActionPack(pActionPack))
						{
							canEnter = pTap->TryAddUser(pNavChar);
						}
					}

					if (canEnter)
					{
						pSelf->BeginEnterActionPack(pActionPack, "TryCompleteCommand");
						completed = true;
					}
					else
					{
						pSelf->WaitForActionPack(activeContext, pActionPack, "TryCompleteCommand[ApWait]");

						pSelf->StopMovingImmediately(true, false, "TryCompleteCommand[ApWait]");
					}
				}
				else
				{
					StopPathFailed(activeContext, "Arrived at missing AP");
				}
			}
			break;

		case kWaypointFinalGoal:
			{
				// Stop moving if this was the final stop.
				const NavGoalReachedType goalReachedType = pNavAnimController->GetCurrentGoalReachedType();

				if (goalReachedType == kNavGoalReachedTypeStop)
				{
					pSelf->GoStopped();
				}
				else
				{
					pSelf->GoMovingBlind();
				}

				SetContextStatus(activeContext, NavContextStatus::kSucceeded, "Arrived at final goal");

				completed = true;
			}
			break;
		}
	}

	if (completed)
	{
		NavControl* pNavControl = pNavChar->GetNavControl();

		pNavControl->ClearCachedPathPolys();
	}

	return completed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::MovingAlongPath::Update()
{
	PROFILE(AI, NSM_UpdateActionMoving);

	NavStateMachine* pSelf		  = GetSelf();
	NavCharacter* pNavChar		  = pSelf->m_pNavChar;
	const NavControl* pNavControl = pNavChar->GetNavControl();
	INavAnimController* pNavAnimController = pNavChar->GetActiveNavAnimController();

	const bool animHasArrived = pNavAnimController->HasArrivedAtGoal();

	// this must come before the action pack resolve otherwise it will trigger a false repath and NPC will get stuck repathing and resolving
	if (pSelf->DetectRepathOrPathFailure() && !animHasArrived)
	{
		if (!pSelf->m_currentPathPs.IsValid() && pSelf->IsWaitingForActionPack())
		{
			LogHdr("UpdateStateMoving: StopLocomoting() for blocked AP\n");

			pSelf->StopMovingImmediately(true, false, "UpdateStateMoving-BlockedAp");
		}

		return;
	}

	if (!pSelf->m_currentPathPs.IsValid() && !animHasArrived)
	{
		pSelf->StopPathFailed(pSelf->m_activeContext, "NSM in Moving state with no path");
		// MailNpcLogTo(pNavChar, "john_bellomy@naughtydog.com", "NSM in Moving state with no path", FILE_LINE_FUNC);
		return;
	}

	if ((pSelf->m_activeContext.m_command.GetType() == NavCommand::kMoveAlongSpline)
		&& (nullptr == pSelf->m_activeContext.m_hPathSpline.ToCatmullRom()))
	{
		pSelf->StopPathFailed(pSelf->m_activeContext, "MoveAlongSpline Spline Disappeared!");
		return;
	}

	const Point oldWaypointPs = pSelf->GetNextDestPointPs();
	const WaypointType oldWaypointType = pSelf->m_activeContext.m_nextWaypointType;

	/* If we're moving to an action pack and HasArrivedAtGoal() is true then let's prevent our Ap status from changing this frame
	 * Since even if we issue a new pathfind the locomotion controller will already consider itself done next frame.
	 */
	bool checkApproach		= false;
	bool needActionPack		= false;
	ActionPack* pActionPack = pSelf->m_hCurrentActionPack.ToActionPack();
	const float pathLength	= pSelf->m_currentPathPs.ComputePathLength();

	// if we are heading to an action pack,
	switch (pSelf->m_activeContext.m_nextWaypointType)
	{
	case kWaypointAPWait:
		{
			if (ActionPack* pNextAp = pSelf->m_activeContext.m_hNextActionPack.ToActionPack())
			{
				if (SetCurrentActionPack(pNextAp, "Moving AP Wait Recovery"))
				{
					pActionPack = pNextAp;

					SetNextWaypoint(pSelf->m_activeContext,
									pSelf->m_activeContext.m_nextNavGoal,
									kWaypointAPDefaultEntry,
									pSelf->m_activeContext.m_nextNavReachedType,
									"Moving AP Wait Recovery");
				}
			}
			else
			{
				// action pack we were waiting for blinked out of existence
				// Try to figure out whether it was a Tap or the goalAp
				// if we should have a goal Ap
				if (pSelf->m_activeContext.m_command.GetType() == NavCommand::kMoveToActionPack)
				{
					// if goal action pack still exists,
					if (const ActionPack* pGoalAp = GetGoalActionPack())
					{
						// it must have been a TAP that vanished because our goal still exists
						pSelf->ResumeMoving(true, false);
						return;
					}
					else if (pSelf->m_activeContext.IsInProgress())
					{
						// our goal has vanished, command failed
						SetContextStatus(pSelf->m_activeContext,
										 NavContextStatus::kFailed,
										 "Moving AP Wait Disappeared");
					}
				}
				else
				{
					// it must have been a TAP that vanished because we're going to a point
					pSelf->ResumeMoving(true, false);
					return;
				}
			}
		}
		break;
	case kWaypointAPDefaultEntry:
	case kWaypointAPResolvedEntry:
		if (pActionPack)
		{
			const bool isTap		= (pActionPack->GetType() == ActionPack::kTraversalActionPack);
			const float reserveDist = isTap ? 0.0f : kApReserveDist;

			if (isTap)
			{
				INdAiWeaponController* pWeaponController = pSelf->m_pAnimationControllers->GetWeaponController();
				const float travelTime = pNavAnimController->EstimateRemainingTravelTime();

				if ((travelTime < 0.5f) && pWeaponController)
				{
					pWeaponController->TryExtendSuppression(0.5f);
				}
			}

			// if the action pack we are heading to suddenly becomes unavailable (i.e. player blocked),
			if (!pActionPack->IsAvailableFor(pNavChar))
			{
				bool allowApproach = false;

				if (isTap)
				{
					const TraversalActionPack* pTap = (const TraversalActionPack*)pActionPack;
					allowApproach = (pathLength >= 1.0f) || pTap->IsBriefReserved();
				}

				if (!allowApproach)
				{
					pSelf->WaitForActionPack(pSelf->m_activeContext, pActionPack, "MovingAlongPath[ApBlocked]");

					if (animHasArrived)
					{
						LogHdr("Ap no longer available, but anim controller has arrived, stopping\n");

						pSelf->StopMovingImmediately(true, false, "MovingAlongPath-MissedAp");

						return;
					}

					if (isTap)
					{
						// if it isn't available for any reason OTHER THAN a brief reservation,
						TraversalActionPack* pTap = (TraversalActionPack*)pActionPack;
						if (!pTap->IsBriefReserved() && !pTap->IsReserved())
						{
							// regenerate the path
							pSelf->m_shouldUpdatePath = true;
							pSelf->UpdatePathDirectionPs();

							LogHdr("TAP is not brief-reserved! redoing pathfind ");
							LogActionPack(pActionPack);
							LogMsg("\n");
							return;
						}
					}
				}
			}
			// if we haven't reserved the action pack and we are within the reserve distance,
			else if (GetActionPackUsageState() == kActionPackUsageNone && pathLength < reserveDist)
			{
				// try to reserve
				if (!ReserveActionPack())
				{
					// this may be a bit strange if we already resolved
					pSelf->WaitForActionPack(pSelf->m_activeContext, pActionPack, "UpdateStateMoving[ReserveFail]");
				}
			}
		}
		break;
	}

	ActionPackEntryDef& apEntry = pSelf->m_activeContext.m_actionPackEntry;

	switch (pSelf->m_activeContext.m_nextWaypointType)
	{
	case kWaypointAPDefaultEntry:
	case kWaypointAPResolvedEntry:
		{
			if (!pActionPack)
			{
				if (pSelf->m_nextContext.IsValid())
				{
					SetContextStatus(pSelf->m_activeContext, NavContextStatus::kFailed, "ActionPack disappeared!");
				}
				else
				{
					StopPathFailed(pSelf->m_activeContext, "ActionPack disappeared!");
				}
				checkApproach = false;
			}
			else
			{
				// close enough to resolve?
				checkApproach = true;
				needActionPack = true;
			}
		}
		break;

	case kWaypointFinalGoal:
	case kWaypointAPWait:
		{
			checkApproach = true;
		}
		break;
	}

	const float waypointDelta = Dist(oldWaypointPs, pSelf->GetNextDestPointPs());
	const float goalRadius	  = pSelf->m_activeContext.m_command.GetGoalRadius();
	if (checkApproach && (waypointDelta > goalRadius))
	{
		LogHdr("UpdateStateMoving: forcing checkApproach to false because waypoint moved from ");
		LogPoint(oldWaypointPs);
		LogMsg(" to ");
		LogNavLocation(pSelf->m_activeContext.m_nextNavGoal);
		LogMsg(" (%0.2fm away)\n", waypointDelta);
		checkApproach = false;
	}

	bool canArrive = false;

	if (checkApproach)
	{
		// Stop updating the movement direction
		const MotionConfig& motionConfig = pNavChar->GetMotionConfig();

		if (needActionPack)
		{
			// close enough to reserve?
			if (GetActionPackUsageState() == kActionPackUsageNone)
			{
				if (!ReserveActionPack())
				{
					pSelf->WaitForActionPack(pSelf->m_activeContext, pActionPack, "UpdateStateMoving(5)");
				}
			}
		}
		switch (pSelf->m_activeContext.m_nextWaypointType)
		{
		case kWaypointAPWait:
			canArrive = true;
			break;

		case kWaypointAPDefaultEntry:
		case kWaypointAPResolvedEntry:
		case kWaypointFinalGoal:
			{
				const bool waypointMoved = Dist(oldWaypointPs, pSelf->GetNextDestPointPs()) > 0.25f;
				if ((oldWaypointType != pSelf->m_activeContext.m_nextWaypointType) || waypointMoved)
				{
					LogHdr("UpdateStateMoving: can arrive ");
					if (oldWaypointType != pSelf->m_activeContext.m_nextWaypointType)
					{
						LogMsg("[changed waypointType to %s from %s] ",
							   GetWaypointTypeName(pSelf->m_activeContext.m_nextWaypointType),
							   GetWaypointTypeName(oldWaypointType));
					}
					if (waypointMoved)
					{
						LogMsg("[changed waypoint to ");
						LogNavLocation(pSelf->m_activeContext.m_nextNavGoal);
						LogMsg(" from ");
						LogPoint(oldWaypointPs);
						LogMsg(" a change of %5.3f in XZ and %5.3f in Y] ",
							   (float)DistXz(oldWaypointPs, pSelf->GetNextDestPointPs()),
							   (float)Abs(oldWaypointPs.Y() - pSelf->GetNextDestPointPs().Y()));
					}
					LogMsg("\n");
				}

				canArrive = true;
			}
			break;

		default:
			break;
		}
	}

	if (canArrive)
	{
		if (animHasArrived)
		{
			LogHdr("UpdateStateMoving: LocoCtrl HasArrivedAtGoal = TRUE\n");

			if (!TryCompleteCommand())
			{
				pSelf->StopPathFailed(pSelf->m_activeContext, "Anim Arrived but can't complete command");
			}

			pSelf->GetStateMachine().TakeStateTransition();
		}
		else if (pNavAnimController->HasMissedWaypoint() && pNavAnimController->CanProcessCommand()
				 && pSelf->m_currentPathPs.IsValid())
		{
			if (pNavControl->IsPointStaticallyBlockedPs(pSelf->GetNextDestPointPs()))
			{
				LogHdr("UpdateStateMoving: NavAnimController failed to reach goal and next waypoint is statically blocked - failing! (");
				LogActionPack(pActionPack);
				LogMsg(")\n");

				StopPathFailed(pSelf->m_activeContext, "AP is unreachable");
			}
			else
			{
				LogHdr("UpdateStateMoving: NavAnimController failed to reach the goal! - StartNavigating (");
				LogActionPack(pActionPack);
				LogMsg(")\n");

				NavAnimStartParams params;
				params.m_pPathPs  = &pSelf->m_currentPathPs;
				params.m_waypoint = GetWaypointContract(pSelf->m_activeContext);

				pSelf->m_activeContext.GetEffectiveMoveArgs(params.m_moveArgs);

				pNavAnimController->StartNavigating(params);
			}
		}
		else
		{
			if (!pSelf->m_currentPathPs.IsValid())
			{
				LogHdr("UpdateStateMoving: no path found! - ResumeMoving (");
				LogActionPack(pActionPack);
				LogMsg(")\n");

				// g_prim.Draw(DebugString(pNavChar->GetTranslation(), "aborted arriving2", kColorRed), Seconds(1));

				pSelf->ResumeMoving(true, false);
			}
		}
	}

	pSelf->UpdatePathDirectionPs();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::MovingAlongPath::PostRootLocatorUpdate()
{
	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;
	INavAnimController* pNavAnimController = pNavChar->GetActiveNavAnimController();

	if (!pSelf->m_activeContext.IsInProgress() && pSelf->m_nextContext.IsValid())
	{
		return;
	}

	if (TryCompleteCommand())
	{
		pSelf->GetStateMachine().TakeStateTransition();
		return;
	}

	const MotionConfig& motionConfig = pNavChar->GetMotionConfig();

	pNavAnimController->SetMoveToTypeContinuePoseMatchAnim(pSelf->m_activeContext.m_command.GetDestAnimId(),
														   pSelf->m_activeContext.m_command.GetDestAnimPhase());

	const bool animHasArrived = pNavAnimController->HasArrivedAtGoal();

	ActionPack* pActionPack = pSelf->m_hCurrentActionPack.ToActionPack();
	const float pathLength	= pSelf->m_currentPathPs.ComputePathLength();
	bool apStatusChanged	= false;

	ActionPackEntryDef& apEntry = pSelf->m_activeContext.m_actionPackEntry;

	NdAnimationControllers* pAnimControllers	= GetAnimationControllers();
	ActionPackController* pActionPackController = pAnimControllers->GetControllerForActionPack(pActionPack);

	if ((GetActionPackUsageState() != kActionPackUsageNone) || pSelf->IsNextWaypointActionPackEntry())
	{
		if (!pActionPack || !pActionPack->IsEnabled())
		{
			LogHdr("Moving::PostRootLocatorUpdate() - AP 0x%.8x Disappeared! Turned Invalid: '%s'\n",
				   pSelf->m_hCurrentActionPack.GetMgrId(),
				   pSelf->m_hCurrentActionPack.HasTurnedInvalid() ? "YES" : "no");
			StopPathFailed(pSelf->m_activeContext, "AP disappeared while moving");
			return;
		}
	}

	bool shouldPatchPath = false;
	bool resolveValid	 = false;
	const bool animHasArrivedAtAp = animHasArrived && apEntry.IsValidFor(pActionPack);

	switch (pSelf->m_activeContext.m_nextWaypointType)
	{
	case kWaypointAPDefaultEntry:
		{
			ActionPackResolveInput defResolveInfo = pSelf->MakeApResolveInput(pSelf->m_activeContext.m_command,
																			  pActionPack);

			NAV_ASSERT(pActionPack);
			NAV_ASSERT(GetAnimationControllers());

			// close enough to resolve?
			PROFILE(AI, NSM_PRLU_Resolve);

			// don't bother resolving a new entry if we're already there
			if (DistanceWithinApResolveRange(pathLength) && !animHasArrivedAtAp)
			{
				// try to resolve
				const bool hadValidDefaultAnimEntry = apEntry.IsValidFor(pActionPack);

				if (pActionPackController)
				{
					ActionPackEntryDef newEntryDef;
					const bool apResolved = pActionPackController->ResolveEntry(defResolveInfo,
																				pActionPack,
																				&newEntryDef);
					const bool posClear	  = apResolved && IsDynamicallyClear(newEntryDef.m_entryNavLoc);
					if (posClear)
					{
						resolveValid = true;
						apEntry		 = newEntryDef;
						apEntry.m_mtSubcategoryId = defResolveInfo.m_mtSubcategory;
					}
				}

				if (resolveValid)
				{
					pNavAnimController->SetMoveToTypeContinuePoseMatchAnim(apEntry.m_entryAnimId, apEntry.m_phase);

					SetNextWaypoint(pSelf->m_activeContext,
									apEntry.m_entryNavLoc,
									kWaypointAPResolvedEntry,
									apEntry.m_stopBeforeEntry ? kNavGoalReachedTypeStop : kNavGoalReachedTypeContinue,
									"PostRootLocatorUpdate(resolve)");

					apStatusChanged = true;
				}
				else
				{
					// resolve failed, need to set m_actionPackEntry to something or we may crash
					if (hadValidDefaultAnimEntry
						&& pActionPackController->UpdateEntry(defResolveInfo, pActionPack, &apEntry))
					{
						// update our existing default entry anim if we have one
						resolveValid = true;
					}
					else if (pActionPackController->ResolveDefaultEntry(defResolveInfo, pActionPack, &apEntry))
					{
						// no existing default anim, try and setup a new one
						resolveValid = true;
					}
					else
					{
						// couldn't even get a new default entry anim, so use defaults
						ActionPackEntryDefSetupDefaults(&apEntry, pActionPack, pNavChar);
					}

					SetNextWaypoint(pSelf->m_activeContext,
									apEntry.m_entryNavLoc,
									kWaypointAPDefaultEntry,
									apEntry.m_stopBeforeEntry ? kNavGoalReachedTypeStop : kNavGoalReachedTypeContinue,
									"PostRootLocatorUpdate(resolve-def)");

					shouldPatchPath = true;
				}
			}
		}
		break;

	case kWaypointAPResolvedEntry:
		{
			PROFILE(AI, NSM_PRLU_Resolve2);
			ActionPackResolveInput defResolveInfo = pSelf->MakeApResolveInput(pSelf->m_activeContext.m_command,
																			  pActionPack);

			NAV_ASSERT(pActionPack);
			NAV_ASSERT(GetAnimationControllers());

			// don't bother resolving a new entry if we're already there
			if (animHasArrivedAtAp)
			{
				// try and get as up to date info as possible
				ActionPackEntryDef updatedEntry = apEntry;
				if (pActionPackController
					&& pActionPackController->UpdateEntry(defResolveInfo, pActionPack, &updatedEntry))
				{
					apEntry = updatedEntry;
				}
				else
				{
					LogHdr("We've arrived at our resolved AP entry but the update failed\n");
				}
			}
			else if (DistanceWithinApResolveRange(pathLength))
			{
				if (pActionPackController && pActionPackController->UpdateEntry(defResolveInfo, pActionPack, &apEntry))
				{
					resolveValid = IsDynamicallyClear(apEntry.m_entryNavLoc);

					if (!resolveValid)
					{
						AiLogNavDetails(pNavChar, "Resolved entry turned invalid, IsDynamicallyClear failed\n");
					}
				}
				else
				{
					AiLogNavDetails(pNavChar, "Resolved entry turned invalid, UpdateEntry failed\n");
				}

				if (resolveValid)
				{
					pNavAnimController->SetMoveToTypeContinuePoseMatchAnim(apEntry.m_entryAnimId, apEntry.m_phase);

					// NOTE: this change will potentially cause ShouldUpdatePath() logic to generate a false positive result until the next frame
					const NavLocation nextLoc = apEntry.m_entryNavLoc;
					SetNextWaypoint(pSelf->m_activeContext,
									nextLoc,
									kWaypointAPResolvedEntry,
									apEntry.m_stopBeforeEntry ? kNavGoalReachedTypeStop : kNavGoalReachedTypeContinue,
									"PostRootLocatorUpdate(2)");

					shouldPatchPath = true;
				}
				else
				{
					WaypointType nextType = kWaypointAPDefaultEntry;

					// resolve failed, need to set m_actionPackEntry to something or we may crash
					if (pActionPackController->ResolveEntry(defResolveInfo, pActionPack, &apEntry))
					{
						nextType	 = kWaypointAPResolvedEntry;
						resolveValid = true;
					}
					else if (pActionPackController->ResolveDefaultEntry(defResolveInfo, pActionPack, &apEntry))
					{
						resolveValid = true;
					}
					else
					{
						ActionPackEntryDefSetupDefaults(&apEntry, pActionPack, pNavChar);
					}

					// NOTE: this change will potentially cause ShouldUpdatePath() logic to generate a false positive result until the next frame
					const NavLocation nextLoc = apEntry.m_entryNavLoc;
					SetNextWaypoint(pSelf->m_activeContext,
									nextLoc,
									nextType,
									apEntry.m_stopBeforeEntry ? kNavGoalReachedTypeStop : kNavGoalReachedTypeContinue,
									"PostRootLocatorUpdate(3)");

					pSelf->m_shouldUpdatePath = true;
					apStatusChanged = true;
					shouldPatchPath = true;

					LogHdr("PostRootLocatorUpdate: entry became invalid - Repath to new entry point ");
					LogNavLocation(pSelf->m_activeContext.m_nextNavGoal);
					LogMsg("\n");
				}
			}
		}
		break;
	}

	if (resolveValid)
	{
		bool enterImmediately = pActionPack->IsAvailableFor(pNavChar)
								&& ActionPackController::TestForImmediateEntry(pNavChar, apEntry);

		if (enterImmediately && !pActionPack->IsReservedBy(pNavChar))
		{
			LogHdr("PostRootLocatorUpdate: resolved entry - trying to enter immediately into AP ");
			LogActionPack(pActionPack);
			LogMsg("but need to re-reserve AP first!\n");

			// last chance to reserve an AP that might've been swept out from under us
			if (GetActionPackUsageState() != kActionPackUsageNone)
			{
				ReleaseActionPack();
			}

			enterImmediately = ReserveActionPack();
		}

		TraversalActionPack* pTap = TraversalActionPack::FromActionPack(pActionPack);
		if (enterImmediately && pTap)
		{
			enterImmediately = pTap->TryAddUser(pNavChar);
		}

		if (enterImmediately)
		{
			pSelf->m_shouldUpdatePath = false;
			shouldPatchPath = false;
			apStatusChanged = false;

			BeginEnterActionPack(pActionPack, "Moving-PRLU-Immediate");
		}
		else if (apStatusChanged)
		{
			pSelf->m_shouldUpdatePath = true;
			shouldPatchPath = true;

			LogHdr("PostRootLocatorUpdate: resolved entry - Repath to new entry point (%0.2fm away) ",
				   float(Dist(pNavChar->GetTranslationPs(), pSelf->GetNextDestPointPs())));
			LogNavLocation(pSelf->m_activeContext.m_nextNavGoal);
			LogMsg("\n");
		}
	}

	if (apStatusChanged)
	{
		// prevent a stopping animation from triggering this frame with the new path
		pNavAnimController->InvalidateCurrentPath();

		// make sure we have the new waypoint contract pushed to the nav anim controller
		pSelf->UpdatePathDirectionPs();
	}

	if (shouldPatchPath && (pSelf->m_currentPathPs.GetWaypointCount() == 2) && pSelf->m_pathStatus.m_pathFound
		&& Nav::IsOnSameNavSpace(pNavChar->GetNavLocation(), pSelf->m_activeContext.m_nextNavGoal))
	{
		pSelf->PatchPathEnd(pSelf->m_activeContext, pSelf->m_activeContext.m_nextNavGoal, "Moving-PathSwitch");
	}
}

/************************************************************************/
/* Moving (following direction)                                         */
/************************************************************************/

class NavStateMachine::MovingBlind : public NavStateMachine::BaseState
{
	virtual void Update() override;
	virtual bool CanProcessCommands() const override { return true; }
};

FSM_STATE_REGISTER(NavStateMachine, MovingBlind, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::MovingBlind::Update()
{
	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf ? pSelf->m_pNavChar : nullptr;
	INavAnimController* pNavAnimController = pNavChar ? pNavChar->GetActiveNavAnimController() : nullptr;

	if (pNavAnimController && (pSelf->m_activeContext.m_command.GetType() == NavCommand::kMoveInDirection))
	{
		const Vector moveDirPs = pSelf->m_activeContext.m_command.GetMoveDirPs();

		pNavAnimController->UpdateMoveDirPs(moveDirPs);

		pSelf->m_currentPathDirPs = SafeNormalize(moveDirPs, GetLocalZ(pNavChar->GetLocatorPs()));
	}

	if (pSelf->m_nextContext.m_status == NavContextStatus::kWaitingForPath && !pSelf->m_shouldUpdatePath)
	{
		pSelf->m_shouldUpdatePath = true;
	}
}

/************************************************************************/
/* Steering to achieve direction driven by path results                 */
/************************************************************************/

class NavStateMachine::SteeringAlongPath : public NavStateMachine::BaseState
{
	virtual void OnEnter() override;
	virtual void Update() override;
	virtual bool CanProcessCommands() const override { return true; }

	virtual void DebugDraw() const override
	{
		STRIP_IN_FINAL_BUILD;

		const NavStateMachine* pSelf = GetSelf();
		const NavCharacter* pNavChar = pSelf->m_pNavChar;
		const Locator& parentSpace = pNavChar->GetParentSpace();
		const Point drawPosWs = parentSpace.TransformPoint(pNavChar->GetTranslationPs() + kUnitYAxis);

		g_prim.Draw(DebugArrow(drawPosWs, parentSpace.TransformVector(m_curSteerDirPs), kColorCyan));
		g_prim.Draw(DebugArrow(drawPosWs, parentSpace.TransformVector(pSelf->m_currentPathDirPs), kColorMagenta));
	}

	Vector m_curSteerDirPs;
};

FSM_STATE_REGISTER(NavStateMachine, SteeringAlongPath, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::SteeringAlongPath::OnEnter()
{
	BaseState::OnEnter();

	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;

	m_curSteerDirPs = pSelf->m_currentPathDirPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::SteeringAlongPath::Update()
{
	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;
	INavAnimController* pNavAnimController = pNavChar ? pNavChar->GetActiveNavAnimController() : nullptr;

	if (pSelf->DetectRepathOrPathFailure())
		return;

	if (!pSelf->m_currentPathPs.IsValid())
		return;

	if (!pNavAnimController)
	{
		SetContextStatus(pSelf->m_activeContext, NavContextStatus::kFailed, "Nav controller gone");
		pSelf->GoStopped();
		return;
	}

	const NavCommand& activeCmd = pSelf->m_activeContext.m_command;
	if (activeCmd.GetType() != NavCommand::kSteerToLocation)
		return;

	const float steerRateDps = activeCmd.GetSteerRateDps();
	const float curAngleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(pSelf->m_currentPathDirPs, m_curSteerDirPs)));

	if (curAngleDiffDeg >= g_navCharOptions.m_steerFailAngleDeg)
	{
		SetContextStatus(pSelf->m_activeContext, NavContextStatus::kFailed, "Steering angle failure");
		pSelf->GoMovingBlind();
		return;
	}

	if (steerRateDps > NDI_FLT_EPSILON)
	{
		const float dt = pNavChar->GetClock()->GetDeltaTimeInSeconds();
		const Quat rotDiff = QuatFromVectors(m_curSteerDirPs, pSelf->m_currentPathDirPs);

		const float maxAngleDiffRad = DEGREES_TO_RADIANS(dt * steerRateDps);
		const Quat delta = LimitQuatAngle(rotDiff, maxAngleDiffRad);

		m_curSteerDirPs = Rotate(delta, m_curSteerDirPs);

		pNavAnimController->UpdateMoveDirPs(m_curSteerDirPs);
	}

	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		const Point goalPosWs = activeCmd.GetGoalLocation().GetPosWs();
		const Point myPosWs = pNavChar->GetTranslation();

		const float goalRadius = Max(activeCmd.GetMoveArgs().m_goalRadius, pNavChar->GetMotionConfig().m_minimumGoalRadius);
		const float distToGoal = DistXz(goalPosWs, myPosWs);

		if (distToGoal < goalRadius && (Abs(goalPosWs.Y() - myPosWs.Y()) < 1.0f))
		{
			SetContextStatus(pSelf->m_activeContext, NavContextStatus::kSucceeded, "Reached steer goal");
			pSelf->GoMovingBlind();
			return;
		}
	}
}

/************************************************************************/
/* Stopping                                                             */
/************************************************************************/

class NavStateMachine::Stopping : public NavStateMachine::BaseState
{
	virtual void Update() override;
	virtual bool CanProcessCommands() const override { return true; }
};

FSM_STATE_REGISTER(NavStateMachine, Stopping, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Stopping::Update()
{
	PROFILE(AI, NSM_Stopping);

	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;
	INavAnimController* pNavAnimController = pNavChar->GetActiveNavAnimController();

	const bool animHasArrived = pNavAnimController ? pNavAnimController->HasArrivedAtGoal() : true;

	if (animHasArrived)
	{
		const bool animIsBusy = pNavAnimController ? pNavAnimController->IsBusy() : false;

		LogHdr("UpdateStateStopping: HasArrivedAtGoal = TRUE, %s busy\n", animIsBusy ? "IS" : "NOT");

		if (!animIsBusy)
		{
			if (pSelf->m_activeContext.IsInProgress()
				&& (pSelf->m_activeContext.m_command.GetType() == NavCommand::Type::kStopAndStand)
				&& (pSelf->m_activeContext.m_nextWaypointType == kWaypointFinalGoal))
			{
				// if we were navigating, we have reached the goal
				SetContextStatus(pSelf->m_activeContext,
								 NavContextStatus::kSucceeded,
								 "UpdateStateStopping Goal Reached");
			}

			pSelf->GoStopped();

			pSelf->GetStateMachine().TakeStateTransition();
		}
	}
}

/************************************************************************/
/* Using Traversal AP                                                   */
/************************************************************************/

class NavStateMachine::UsingTraversalActionPack : public NavStateMachine::BaseState
{
	virtual void OnEnter() override;
	virtual void Update() override;
	virtual bool CanProcessCommands() const override;
	virtual bool GetPathFindStartLocation(NavLocation* pStartLocOut,
										  ActionPackHandle* phPreferredTapOut,
										  float* pTapBiasOut) const override;

	virtual bool IsEnteringActionPack() const override
	{
		return !IsActionPackEntryComplete();
	}

	virtual bool IsActionPackEntryComplete() const override
	{
		const INdAiTraversalController* pTraversalController = GetAnimationControllers()->GetTraversalController();
		return pTraversalController->IsEntryCommitted();
	}

	NavLocation m_pathFindStartLoc;
	bool m_startLocValid;
};

FSM_STATE_REGISTER(NavStateMachine, UsingTraversalActionPack, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::UsingTraversalActionPack::OnEnter()
{
	BaseState::OnEnter();

	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;

	m_startLocValid = false;

	const TraversalActionPack* pTap = GetTraversalActionPack();
	INdAiTraversalController* pTraversalController = GetAnimationControllers()->GetTraversalController();

	if (pTap && pTraversalController)
	{
		const Point exitPosPs = pTraversalController->GetExitPathOriginPosPs();

		m_pathFindStartLoc = pTap->GetDestNavLocation();
		m_pathFindStartLoc.UpdatePosPs(exitPosPs);
		m_startLocValid = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::UsingTraversalActionPack::Update()
{
	PROFILE(AI, NSM_UsingTAP);

	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;
	INdAiTraversalController* pTraversalController = GetAnimationControllers()->GetTraversalController();

	NAV_ASSERT(pTraversalController);

	const TraversalActionPack* pTap = GetTraversalActionPack();

	if (!pTap || pTap->IsNoPush())
	{
		// if are playing traversal anim, we should push the player
		pNavChar->RequestPushPlayerAway();
	}

	if ((!pTap || !pTap->IsAvailableFor(pNavChar)) && CanProcessCommands())
	{
		LogHdr("TAP disappeared! failing command\n");

		if (pTraversalController)
		{
			pTraversalController->Exit(nullptr);
		}

		pSelf->StopPathFailed(pSelf->m_activeContext, "Using TAP Disappeared");
		return;
	}

	if (pTap && pTraversalController->SafeToBindToDestSpace() && pTap->ChangesParentSpaces())
	{
		const Binding& destSpace = pTap->GetDestBinding();
		if (!pNavChar->GetBinding().IsSameBinding(destSpace))
		{
			pNavChar->BindToRigidBody(destSpace.GetRigidBody());
		}
	}

	const bool tapDone = !pTraversalController->IsBusy();

	if (NavControl* pNavControl = pNavChar->GetNavControl())
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		const Point myPosPs = pNavChar->GetTranslationPs();
		const NavLocation destNavLoc = pTap ? pTap->GetDestNavLocation() : NavLocation();
		const NavMesh* pDestMesh = destNavLoc.ToNavMesh();

		if (tapDone && pTap)
		{
			pNavControl->SetNavLocation(pNavChar, destNavLoc);
			pNavControl->UpdatePs(myPosPs, pNavChar);
		}
		else if (pDestMesh)
		{
			pNavControl->UpdatePs(myPosPs, pNavChar, pDestMesh);
		}
	}

	INdAiWeaponController* pWeaponController = pSelf->m_pAnimationControllers->GetWeaponController();
	if (pWeaponController
		&& ((pSelf->m_activeContext.m_nextWaypointType == kWaypointAPDefaultEntry)
			|| (pSelf->m_activeContext.m_nextWaypointType == kWaypointAPResolvedEntry)))
	{
		if (const TraversalActionPack* pNextTap = pSelf->m_activeContext.m_hNextActionPack.ToActionPack<TraversalActionPack>())
		{
			if (Dist(pSelf->m_activeContext.m_nextNavPosPs, pNavChar->GetTranslation()) < 3.0f)
			{
				pWeaponController->TryExtendSuppression(0.5f);
			}
		}
	}

	if (tapDone)
	{
		const AnimControl* pAnimControl		  = pNavChar->GetAnimControl();
		const AnimStateLayer* pBaseLayer	  = pAnimControl->GetBaseStateLayer();
		const AnimStateInstance* pCurInstance = pBaseLayer->CurrentStateInstance();
		const bool curStateValid = pCurInstance ? (pCurInstance->GetStateFlags() & DC::kAnimStateFlagLocomotionReady)
												: false;

		/*
				MAIL_ASSERT(Animation,
							!curStateValid || LocomotionReadyRequirementsMet(pNavChar, pCurInstance->GetState()),
							("anim-state '%s' isn't actually locomotion-ready", pCurInstance->GetState()->m_name.m_string.c_str()));
		*/

		if (curStateValid)
		{
			LogHdr("Done using TAP\n");

			const ActionPack* pNextAp = nullptr;

			if ((pSelf->m_activeContext.m_nextWaypointType == kWaypointAPDefaultEntry)
				|| (pSelf->m_activeContext.m_nextWaypointType == kWaypointAPResolvedEntry))
			{
				pNextAp = pSelf->m_activeContext.m_hNextActionPack.ToActionPack();
			}

			SetCurrentActionPack(pNextAp, "UsingTraversalActionPack_Update");

			if (INavAnimController* pNewNavAnimController = pNavChar->GetActiveNavAnimController())
			{
				pNewNavAnimController->Activate();
			}

			const bool needNewPath	  = !pSelf->m_currentPathPs.IsValid();
			pSelf->m_shouldUpdatePath = needNewPath;
			pSelf->m_pathStatus.m_lastPathIssuedTime = pNavChar->GetCurTime();
			pSelf->ResumeMoving(needNewPath, true);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::UsingTraversalActionPack::CanProcessCommands() const
{
	INdAiTraversalController* pTraversalController = GetAnimationControllers()->GetTraversalController();

	return pTraversalController && pTraversalController->CanProcessCommands();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::UsingTraversalActionPack::GetPathFindStartLocation(NavLocation* pStartLocOut,
																		 ActionPackHandle* phPreferredTapOut,
																		 float* pTapBiasOut) const
{
	const NavStateMachine* pSelf = GetSelf();
	const NavCharacter* pNavChar = pSelf->m_pNavChar;
	const NavControl* pNavCon	 = pNavChar ? pNavChar->GetNavControl() : nullptr;

	NavLocation startLoc = m_pathFindStartLoc;
	ActionPackHandle hPreferredTap = pSelf->m_activeContext.m_hNextActionPack;
	float tapBias = -15.0f;

	if (pNavCon && pSelf->m_nextContext.IsValid())
	{
		startLoc	  = pNavCon->GetNavLocation();
		hPreferredTap = pSelf->m_hCurrentActionPack;
		tapBias		  = -50.0f;
	}

	if (pStartLocOut)
		*pStartLocOut = startLoc;

	if (phPreferredTapOut)
		*phPreferredTapOut = hPreferredTap;

	if (pTapBiasOut)
		*pTapBiasOut = tapBias;

	return m_startLocValid;
}

/************************************************************************/
/* Playing Enter Animation                                              */
/************************************************************************/

class NavStateMachine::EnteringActionPack : public NavStateMachine::BaseState
{
	virtual void Update() override;
	virtual bool CanProcessCommands() const override;
	virtual bool IsEnteringActionPack() const override { return true; }
};

FSM_STATE_REGISTER(NavStateMachine, EnteringActionPack, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::EnteringActionPack::Update()
{
	PROFILE(AI, NSM_PlayingEnterAnim);

	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;
	const ActionPack::Type apType = GetActionPackUsageType();
	ActionPackController* pActionPackController = GetAnimationControllers()->GetControllerForActionPackType(apType);

	bool shouldAbort = pSelf->IsCommandPending();

	if (!shouldAbort)
	{
		const bool isClear = IsDynamicallyClear(pNavChar->GetNavLocation());

		// we can only safely abort the enter animation if we have nav mesh clearance, otherwise we'll get stuck
		if (isClear)
		{
			// figure out if we should abort the enter animation
			if (const ActionPack* pAp = GetActionPack())
			{
				const float apEntryDist = pNavChar->GetNavControl()->GetActionPackEntryDistance();
				const Point defEntryWs	= pAp->GetDefaultEntryPointWs(apEntryDist);
				const Point myPosWs		= pNavChar->GetTranslation();

				if (pNavChar->CaresAboutPlayerBlockage(pAp))
				{
					const bool apBlocked   = pAp->IsPlayerBlocked();
					const bool lineBlocked = IsPlayerBlockingLineOfMotion(myPosWs, defEntryWs, 0.35f);
					if (apBlocked || lineBlocked)
					{
						shouldAbort = true;
						LogHdr("Aborting AP Enter Animation! %s\n",
							   apBlocked ? "(AP is blocked)" : "(motion line is blocked)");
					}
				}
			}
			else
			{
				// action pack vanished
				shouldAbort = true;
				LogHdr("Aborting Cover Enter Animation because action pack vanished!\n");
			}
		}
		else if (!pNavChar->IsBuddyNpc())
		{
			// if we aren't clear on the nav mesh and can't abort this enter anim, we should push the player
			pNavChar->RequestPushPlayerAway();
		}
	}

	if (shouldAbort && pActionPackController && pActionPackController->RequestAbortAction())
	{
		LogHdr("Successfully aborted enter action\n");

		if (pSelf->m_activeContext.IsInProgress())
		{
			// maybe we should not fail in this case and instead try to recover, or go into a wait for blockage to clear mode
			SetContextStatus(pSelf->m_activeContext, NavContextStatus::kFailed, "Aborting AP Enter");
		}

		pSelf->GoStartingToExitActionPack();
	}
	// Once we no longer are transitioning.... consider the goal reached
	else if (!pActionPackController || pActionPackController->IsEnterComplete())
	{
		if (ActionPack* pAp = GetActionPack()) // does the action pack still exist?
		{
			if (pSelf->m_activeContext.IsInProgress())
			{
				SetNextWaypoint(pSelf->m_activeContext,
								pSelf->m_activeContext.m_finalNavGoal,
								kWaypointFinalGoal,
								pSelf->m_activeContext.m_command.GetGoalReachedType(),
								"UpdateStatePlayingEnterAnimation");

				if (pSelf->m_activeContext.m_command.GetGoalActionPack() == pAp)
				{
					SetContextStatus(pSelf->m_activeContext, NavContextStatus::kSucceeded, "Entered ActionPack");
				}
			}

			if (pSelf->m_activeContext.m_actionPackEntry.m_autoExitAfterEnter)
			{
				pActionPackController->Exit(nullptr);

				pSelf->GoExitingActionPack(true);
			}
			else
			{
				pSelf->GoUsingActionPack();
			}
		}
		else
		{
			LogHdr("UpdateStatePlayingEnterAnimation: ActionPack vanished, exiting\n");

			if (pSelf->m_activeContext.IsInProgress())
			{
				SetContextStatus(pSelf->m_activeContext, NavContextStatus::kFailed, "EnteringActionPack AP Gone");
			}

			pSelf->GoStartingToExitActionPack();
		}
	}

	pSelf->GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::EnteringActionPack::CanProcessCommands() const
{
	const ActionPack::Type apType = GetActionPackUsageType();
	NdAnimationControllers* pAnimControllers	= GetAnimationControllers();
	ActionPackController* pActionPackController = pAnimControllers->GetControllerForActionPackType(apType);
	return !pActionPackController || !pActionPackController->IsBusy();
}

/************************************************************************/
/* Interrupted                                                          */
/************************************************************************/

class NavStateMachine::Interrupted : public NavStateMachine::BaseState
{
	virtual void Update() override {}
	virtual bool CanProcessCommands() const override { return false; }
};

FSM_STATE_REGISTER(NavStateMachine, Interrupted, kPriorityMedium);

/************************************************************************/
/* Resuming                                                             */
/************************************************************************/

class NavStateMachine::Resuming : public NavStateMachine::BaseState
{
	virtual void OnEnter() override;
	virtual void Update() override {}
	virtual bool CanProcessCommands() const override { return true; }
};

FSM_STATE_REGISTER(NavStateMachine, Resuming, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::Resuming::OnEnter()
{
	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;

	BaseState::OnEnter();

	pSelf->m_shouldUpdatePath = true;

	INavAnimController* pNavAnimController = pNavChar->GetActiveNavAnimController();

	NavAnimHandoffDesc handoff;
	if (pNavAnimController && pNavAnimController->IsInterrupted() && !pNavAnimController->IsCommandInProgress()
		&& pNavChar->PeekValidHandoff(&handoff))
	{
		if (handoff.m_motionType < kMotionTypeMax)
		{
			LogHdr("Waiting to resume with a valid moving handoff, telling nav anim controller to go blind moving\n");

			NavAnimStartParams startParams;

			const Vector fwPs = GetLocalZ(pNavChar->GetRotationPs());
			Scalar curSpeed;
			startParams.m_moveDirPs = SafeNormalize(pNavChar->GetVelocityPs(), fwPs, curSpeed);

			if (curSpeed < 0.5f)
			{
				startParams.m_moveDirPs = fwPs;
			}

			startParams.m_moveArgs.m_motionType		 = handoff.m_motionType;
			startParams.m_moveArgs.m_goalReachedType = kNavGoalReachedTypeContinue;
			startParams.m_waypoint.m_motionType		 = handoff.m_motionType;
			startParams.m_waypoint.m_reachedType	 = kNavGoalReachedTypeContinue;
			startParams.m_waypoint.m_rotPs = pNavChar->GetRotationPs();

			pNavAnimController->StartNavigating(startParams);
		}
		else
		{
			LogHdr("Waiting to resume with a valid stopped handoff, telling nav anim controller to go idle\n");

			NavAnimStopParams stopParams;
			stopParams.m_goalRadius = pSelf->m_activeContext.IsValid()
										  ? pSelf->m_activeContext.m_command.GetGoalRadius()
										  : 0.0f;

			stopParams.m_waypoint.m_motionType	= kMotionTypeMax;
			stopParams.m_waypoint.m_reachedType = kNavGoalReachedTypeStop;
			stopParams.m_waypoint.m_rotPs		= pNavChar->GetRotationPs();

			pNavAnimController->StopNavigating(stopParams);
		}
	}
}

/************************************************************************/
/* Using AP                                                             */
/************************************************************************/

class NavStateMachine::UsingActionPack : public NavStateMachine::BaseState
{
	virtual void Update() override;
	virtual bool CanProcessCommands() const override;
	virtual bool GetPathFindStartLocation(NavLocation* pStartLocOut,
										  ActionPackHandle* phPreferredTapOut,
										  float* pTapBiasOut) const override;

	virtual bool IsActionPackEntryComplete() const override { return true; }
};

FSM_STATE_REGISTER(NavStateMachine, UsingActionPack, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::UsingActionPack::Update()
{
	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;

	const ActionPack* pAp = GetActionPack();

	if (!pAp && !IsCommandPending() && !pNavChar->IsBusy())
	{
		LogHdr("UpdateStateUsingActionPack: action pack is gone, ExitActionPack\n");
		// issue a phantom command to exit the action pack
		pSelf->ExitActionPack();
		return;
	}

	const ActionPack::Type apType = GetActionPackUsageType();
	NdAnimationControllers* pAnimControllers = GetAnimationControllers();

	ActionPackController* pActionPackController = pAnimControllers
													  ? pAnimControllers->GetControllerForActionPackType(apType)
													  : nullptr;

	if (pActionPackController && pActionPackController->ShouldAutoExitAp(pAp))
	{
		pActionPackController->Exit(nullptr);

		pSelf->GoExitingActionPack(true);
		pSelf->GetStateMachine().TakeStateTransition();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::UsingActionPack::GetPathFindStartLocation(NavLocation* pStartLocOut,
																ActionPackHandle* phPreferredTapOut,
																float* pTapBiasOut) const
{
	const ActionPack::Type apType = GetActionPackUsageType();
	const NdAnimationControllers* pAnimControllers = GetAnimationControllers();

	const ActionPackController* pActionPackController = pAnimControllers
															? pAnimControllers->GetControllerForActionPackType(apType)
															: nullptr;

	if (!pActionPackController)
		return false;

	if (phPreferredTapOut)
		*phPreferredTapOut = ActionPackHandle();

	return pActionPackController->GetExitPathOrigin(pStartLocOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavStateMachine::UsingActionPack::CanProcessCommands() const
{
	const ActionPack::Type apType = GetActionPackUsageType();

	// JDB: we're too late to make this true for all AP types, so for now just fix the known issue we have with
	// exiting cover during transitional states
	if (apType != ActionPack::kCoverActionPack)
	{
		return true;
	}

	const NdAnimationControllers* pAnimControllers = GetAnimationControllers();

	const ActionPackController* pActionPackController = pAnimControllers
															? pAnimControllers->GetControllerForActionPackType(apType)
															: nullptr;

	const bool busy = pActionPackController && pActionPackController->IsBusy();

	return !busy;
}

/************************************************************************/
/* Starting to exit AP                                                  */
/************************************************************************/

class NavStateMachine::StartingToExitActionPack : public NavStateMachine::BaseState
{
	virtual void OnEnter() override
	{
		BaseState::OnEnter();

		// JDB: Thought this might've been the source of a bug which turned out to be
		// something else. Still, I think this might be a good overall change
		// to ensure that when we need to leave an AP with a fresh path
		// the nav anim controller doesn't prematurely 'arrive' at its goal
#if 0
		// prevent a stopping animation from triggering this frame with the new path
		NavStateMachine* pSelf = GetSelf();
		NavCharacter* pNavChar = pSelf ? pSelf->m_pNavChar : nullptr;
		INavAnimController* pNavAnimController = pNavChar ? pNavChar->GetActiveNavAnimController() : nullptr;

		if (pNavAnimController)
		{
			pNavAnimController->InvalidateCurrentPath();
		}
#endif
	}

	virtual void Update() override;
	virtual bool CanProcessCommands() const override { return false; }
};

FSM_STATE_REGISTER(NavStateMachine, StartingToExitActionPack, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::StartingToExitActionPack::Update()
{
	NavStateMachine* pSelf = GetSelf();
	NavCharacter* pNavChar = pSelf->m_pNavChar;

	const bool commandFailed = pSelf->m_activeContext.m_status == NavContextStatus::kFailed;
	const bool pathFailed	 = pSelf->m_pathStatus.m_pathUpdated && !pSelf->m_pathStatus.m_pathFound;
	const bool shouldStop	 = !pSelf->m_activeContext.m_command.PathFindRequired() || commandFailed || pathFailed;

	if (!shouldStop)
	{
		if (!pSelf->m_pathStatus.m_pathFound)
		{
			pSelf->m_shouldUpdatePath = true;
		}

		if (pSelf->DetectRepathOrPathFailure())
			return;

		if (!pSelf->m_currentPathPs.IsValid())
			return;
	}

	const ActionPack::Type apType = GetActionPackUsageType();
	ActionPackController* pActionPackController = GetAnimationControllers()->GetControllerForActionPackType(apType);

	if (!pActionPackController->IsReadyToExit())
		return;

	LogHdr("WaitingForPathToExitAp: %s shouldStop = %s (lastCommand = %s, commandStatus = %s, pathFailed = %s)\n",
		   ActionPack::GetTypeName(GetActionPackUsageType()),
		   shouldStop ? "true" : "false",
		   GetNavContextStatusName(pSelf->m_activeContext.m_status),
		   pSelf->m_activeContext.m_command.GetTypeName(),
		   pathFailed ? "TRUE" : "false");

	if (shouldStop)
	{
		pActionPackController->Exit(nullptr);
	}
	else
	{
		pActionPackController->Exit(&pSelf->m_currentPathPs);
	}

	pSelf->GoExitingActionPack(false);
	pSelf->GetStateMachine().TakeStateTransition();
}

/************************************************************************/
/* Exiting AP                                                           */
/************************************************************************/

class NavStateMachine::ExitingActionPack : public NavStateMachine::BaseState
{
	virtual void Update() override;
	virtual bool CanProcessCommands() const override { return false; }
};

FSM_STATE_REGISTER_ARG(NavStateMachine, ExitingActionPack, kPriorityMedium, bool);

/// --------------------------------------------------------------------------------------------------------------- ///
void NavStateMachine::ExitingActionPack::Update()
{
	NavStateMachine* pSelf	= GetSelf();
	NavCharacter* pNavChar	= pSelf->m_pNavChar;
	NavControl* pNavControl = pNavChar->GetNavControl();

	INavAnimController* pNavAnimController = pNavChar->GetActiveNavAnimController();
	const ActionPack::Type apType = GetActionPackUsageType();
	ActionPackController* pActionPackController = GetAnimationControllers()->GetControllerForActionPackType(apType);

	const bool isBusy = pActionPackController && pActionPackController->IsBusy();

	const AnimControl* pAnimControl		  = pNavChar->GetAnimControl();
	const AnimStateLayer* pBaseLayer	  = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurInstance = pBaseLayer->CurrentStateInstance();
	const bool curStateValid = pCurInstance ? (pCurInstance->GetStateFlags() & DC::kAnimStateFlagLocomotionReady)
											: false;

	if (!isBusy && curStateValid)
	{
		const bool commandFailed = pSelf->m_activeContext.m_status == NavContextStatus::kFailed;

		if (commandFailed)
		{
			const NavLocation navLoc = pNavChar->GetNavLocation();
			pSelf->SetFinalNavLocation(pSelf->m_activeContext, navLoc, "ExitingActionPack-Failed");
			pSelf->SetNextWaypoint(pSelf->m_activeContext,
								   navLoc,
								   kWaypointFinalGoal,
								   kNavGoalReachedTypeStop,
								   "ExitingActionPack-Failed");
		}

		const WaypointContract waypointContract = GetWaypointContract(pSelf->m_activeContext);
		const float goalRadius = pSelf->m_activeContext.m_command.GetGoalRadius();

		if (pActionPackController)
		{
			pActionPackController->OnExitComplete();
		}

		{
			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

			// find new nav mesh
			pNavControl->ResetNavMesh(nullptr);

			// go ahead and try to find a valid nav location immediately
			// so that we do not have 1 frame of unnecessarily invalid navloc
			pNavControl->UpdatePs(pNavChar->GetTranslationPs(), pNavChar);
		}

		const ActionPack* pNextAp = nullptr;
		const ActionPack* pCurAp  = pSelf->m_hCurrentActionPack.ToActionPack();

		if ((pSelf->m_activeContext.m_nextWaypointType == kWaypointAPDefaultEntry)
			|| (pSelf->m_activeContext.m_nextWaypointType == kWaypointAPResolvedEntry))
		{
			pNextAp = pSelf->m_activeContext.m_hNextActionPack.ToActionPack();
		}

		if (pNextAp == pCurAp)
		{
			SetCurrentActionPack(nullptr, "ExitingActionPack_PreClear");
		}

		if (SetCurrentActionPack(pNextAp, "ExitingActionPack_Update"))
		{
		}
		else if (pNextAp)
		{
			pSelf->WaitForActionPack(pSelf->m_activeContext, pNextAp, "ExitingActionPack_Update");
		}

		const bool wantToMove = pSelf->m_activeContext.IsValid()
								&& (pSelf->m_activeContext.m_command.GetType() != NavCommand::kStopAndStand);

		const bool wantToStopAndStand = commandFailed || !wantToMove;
		const bool autoExiting		  = GetStateArg<bool>();
		if (autoExiting)
		{
			NavAnimHandoffDesc handoff;
			if (pNavChar->PeekValidHandoff(&handoff) && (handoff.m_motionType < kMotionTypeMax))
			{
				LogHdr("Resuming navigation with a valid moving handoff, going to blind moving\n");

				NavAnimStartParams startParams;
				startParams.m_moveArgs.m_motionType		 = handoff.m_motionType;
				startParams.m_moveArgs.m_goalReachedType = kNavGoalReachedTypeContinue;
				startParams.m_moveDirPs = GetLocalZ(pNavChar->GetRotationPs());
				startParams.m_waypoint.m_motionType	 = handoff.m_motionType;
				startParams.m_waypoint.m_reachedType = kNavGoalReachedTypeContinue;
				startParams.m_waypoint.m_rotPs		 = pNavChar->GetRotationPs();

				pNavAnimController->StartNavigating(startParams);

				pSelf->GoMovingBlind();
				pSelf->m_shouldUpdatePath = false;
			}
			else
			{
				NavAnimStopParams params;
				params.m_waypoint	= waypointContract;
				params.m_goalRadius = goalRadius;

				pNavAnimController->StopNavigating(params);

				pSelf->GoStopping();
			}
		}
		else if (wantToStopAndStand) /* Trying to exit to a stop command */
		{
			LogHdr("UpdateStateExitingActionPack: Handoff to stopping (cur state: '%s')\n",
				   DevKitOnly_StringIdToString(pCurInstance->GetStateName()));

			NavAnimStopParams params;
			params.m_waypoint	= waypointContract;
			params.m_goalRadius = goalRadius;
			params.m_faceValid	= pSelf->m_activeContext.IsValid()
								 && pSelf->m_activeContext.m_command.GetMoveArgs().m_goalFaceDirValid;

			//g_prim.Draw(DebugArrow(pNavChar->GetTranslation() + kUnitYAxis, GetLocalZ(waypointContract.m_rotPs), kColorWhite, 0.5f, kPrimEnableHiddenLineAlpha), Seconds(1.0f));

			pNavAnimController->StopNavigating(params);

			pSelf->GoStopping();
		}
		else if (pSelf->m_activeContext.IsInProgress())
		{
			if (pSelf->m_currentPathPs.IsValid())
			{
				NavAnimStartParams params;
				if (pSelf->m_activeContext.m_command.GetType() == NavCommand::kSteerToLocation)
					params.m_moveDirPs = pSelf->m_currentPathDirPs;
				else
					params.m_pPathPs  = &pSelf->m_currentPathPs;
				params.m_waypoint = waypointContract;

				pSelf->m_activeContext.GetEffectiveMoveArgs(params.m_moveArgs);

				LogHdr("NavStateMachine::ExitingActionPack::Update: NavAnimController::StartNavigating(%s, %s)\n",
					   GetMotionTypeName(params.m_moveArgs.m_motionType),
					   GetGoalReachedTypeName(params.m_waypoint.m_reachedType));

				if (params.m_moveArgs.m_motionType >= kMotionTypeMax)
				{
					MailNpcLogTo(pNavChar,
								 "john_bellomy@naughtydog.com",
								 "Started moving without motion type",
								 FILE_LINE_FUNC);
				}

				pNavAnimController->StartNavigating(params);

				if (pSelf->m_activeContext.m_command.GetType() == NavCommand::kSteerToLocation)
					pSelf->GoSteeringAlongPath();
				else
					pSelf->GoMovingAlongPath();
			}
			else if (pSelf->m_activeContext.m_command.PathFindRequired())
			{
				StopPathFailed(pSelf->m_activeContext, "Exiting AP with invalid path");
			}
			else if (LengthSqr(pSelf->m_activeContext.m_command.GetMoveDirPs()) > NDI_FLT_EPSILON)
			{
				pSelf->SetNextWaypoint(pSelf->m_activeContext,
									   pNavChar->GetNavLocation(),
									   kWaypointFinalGoal,
									   kNavGoalReachedTypeContinue,
									   "AP Exit to blind Move");
				pSelf->SetContextStatus(pSelf->m_activeContext, NavContextStatus::kSucceeded, "AP Exit to Blind Move");

				NavAnimStartParams startParams;
				startParams.m_moveArgs	= pSelf->m_activeContext.m_command.GetMoveArgs();
				startParams.m_moveDirPs = pSelf->m_activeContext.m_command.GetMoveDirPs();
				startParams.m_waypoint	= waypointContract;

				pNavAnimController->StartNavigating(startParams);

				pSelf->GoMovingBlind();
				pSelf->m_shouldUpdatePath = false;
			}
			else
			{
				StopPathFailed(pSelf->m_activeContext, "Exiting AP with invalid moveDir");
			}
		}
		else /* Trying to exit to a move command */
		{
			pSelf->StopMovingImmediately(true, false, "Exiting AP with nothing to do");
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCommand::DebugPrint(DoutBase* pDout, bool withSource) const
{
	pDout->Printf("%s", GetTypeName());

	switch (m_type)
	{
	case kStopAndStand:
		{
			const float radius = GetGoalRadius();
			if (radius >= 0.0f)
				pDout->Printf(" @ 0.2fm");
		}
		break;

	case kMoveToLocation:
		{
			pDout->Printf(" %s %s", GetMotionTypeName(GetMotionType()), GetGoalReachedTypeName(GetGoalReachedType()));

			const StringId64 catId = GetMtSubcategory();
			if (catId != INVALID_STRING_ID_64)
			{
				pDout->Printf(" [%s]", DevKitOnly_StringIdToString(catId));
			}

			pDout->Printf(" to ");
			m_goalLocation.DebugPrint(pDout);
		}
		break;

	case kMoveToActionPack:
		{
			pDout->Printf(" %s", GetMotionTypeName(GetMotionType()));

			const StringId64 catId = GetMtSubcategory();
			if (catId != INVALID_STRING_ID_64)
			{
				pDout->Printf(" [%s]", DevKitOnly_StringIdToString(catId));
			}

			if (ActionPack* pGoalAp = GetGoalActionPack())
			{
				pDout->Printf(" to %s", pGoalAp->GetName());
			}
			else
			{
				pDout->Printf(" to <missing-ap>");
			}
		}
		break;

	case kMoveAlongSpline:
		{
			pDout->Printf(" %s %s", GetMotionTypeName(GetMotionType()), GetGoalReachedTypeName(GetGoalReachedType()));

			if (const CatmullRom* pPathSpline = GetPathSpline())
			{
				pDout->Printf(" on %s from %0.1f (%s) to %0.1f (%s) @ %0.1f step",
							  pPathSpline->GetBareName(),
							  GetSplineArcStart(),
							  PrettyPrint(pPathSpline->EvaluatePointGlobal(GetSplineArcStart())),
							  GetSplineArcGoal(),
							  PrettyPrint(pPathSpline->EvaluatePointGlobal(GetSplineArcGoal())),
							  GetSplineArcStep());
			}
			else
			{
				pDout->Printf(" on <missing-spline>");
			}
		}
		break;

	case kMoveInDirection:
		pDout->Printf(" %s %s @ %s",
					  GetMotionTypeName(GetMotionType()),
					  GetGoalReachedTypeName(GetGoalReachedType()),
					  PrettyPrint(GetMoveDirPs(), kPrettyPrintLowPrecision));
		break;

	case kSteerToLocation:
		{
			pDout->Printf(" %s", GetMotionTypeName(GetMotionType()));

			const StringId64 catId = GetMtSubcategory();
			if (catId != INVALID_STRING_ID_64)
			{
				pDout->Printf(" [%s]", DevKitOnly_StringIdToString(catId));
			}

			pDout->Printf(" to ");
			m_goalLocation.DebugPrint(pDout);
		}
		break;
	}

	if (withSource)
	{
		pDout->Printf(" [%s:%d %s]", GetSourceFile(), GetSourceLine(), GetSourceFunc());
	}

	pDout->Printf("\n");
}
