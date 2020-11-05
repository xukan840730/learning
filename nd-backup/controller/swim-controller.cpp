/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/swim-controller.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/util/finite-state-machine.h"

#include "gamelib/anim/motion-matching/motion-matching-def.h"
#include "gamelib/anim/motion-matching/motion-matching-set.h"
#include "gamelib/anim/motion-matching/pose-tracker.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/character-leg-raycaster.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/scriptx/h/npc-demeanor-defines.h"
#include "gamelib/state-script/ss-context.h"
#include "gamelib/state-script/ss-track-group.h"

#include "ailib/nav/nav-ai-util.h"

#include "game/ai/agent/npc.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/anim/anim-game-options.h"
#include "game/script-arg-iterator.h"
#include "game/scriptx/h/anim-npc-info.h"

/// --------------------------------------------------------------------------------------------------------------- ///
#if FINAL_BUILD
#define SwimLogStr(str)
#define SwimLog(str, ...)
#else
#define SwimLogStr(str)                                                                                                \
	AiLogAnim(GetCharacter(),                                                                                          \
			  AI_LOG,                                                                                                  \
			  "[swim-%d] [%s : %s] " str,                                                                              \
			  GetCommandId(),                                                                                          \
			  GetStateName("<none>"),                                                                                  \
			  DevKitOnly_StringIdToString(GetAnimStateId()))
#define SwimLog(str, ...)                                                                                              \
	AiLogAnim(GetCharacter(),                                                                                          \
			  AI_LOG,                                                                                                  \
			  "[swim-%d] [%s : %s] " str,                                                                              \
			  GetCommandId(),                                                                                          \
			  GetStateName("<none>"),                                                                                  \
			  DevKitOnly_StringIdToString(GetAnimStateId()),                                                           \
			  __VA_ARGS__)
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
static float kMinStoppingDistance = 0.05f;
static const float kSwimCommandDistTolerance = 1.0f;
static const float kMinTurnAngle = 45.0f;
static const float kSwimCommandDotTolerance = Cos(kMinTurnAngle);

/// --------------------------------------------------------------------------------------------------------------- ///
inline float GetXZAngleForVecDeg(Vector_arg v)
{
	return RADIANS_TO_DEGREES(Atan2(v.X(), v.Z()));
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline Vector GetVectorForXZAngleDeg(const float angleDeg)
{
	const float angleRad = DEGREES_TO_RADIANS(angleDeg);
	const Vector v = Vector(Sin(angleRad), 0.0f, Cos(angleRad));
	return v;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class SwimCommand
{
public:
	enum class Status
	{
		kNone,
		kPending,
		kInProgress,
		kSucceeded,
		kFailed,
	};

	static const char* GetStatusName(Status i)
	{
		switch (i)
		{
		case Status::kPending:		return "Command Pending";
		case Status::kInProgress:	return "Command In Progress";
		case Status::kSucceeded:	return "Command Succeeded";
		case Status::kFailed:		return "Command Failed";
		}

		return "<unknown status>";
	}

	enum class Type
	{
		kInvalid,
		kStartSwimming,
		kStopSwimming,
	};

	static const char* GetTypeName(Type i)
	{
		switch (i)
		{
		case Type::kInvalid:		return "Invalid";
		case Type::kStartSwimming:	return "StartSwimming";
		case Type::kStopSwimming:	return "StopSwimming";
		}

		return "<unknown>";
	}

	SwimCommand()
	{
		Reset();
	}

	void Reset()
	{
		m_id		 = -1;
		m_type		 = Type::kInvalid;
		m_status	 = Status::kNone;
		m_goalRadius = 0.0f;
	}

	// Accessors
	bool		IsValid() const		{ return m_type != Type::kInvalid; }
	Type		GetType() const		{ return m_type; }
	const char*	GetTypeName() const	{ return GetTypeName(GetType()); }

	bool		Succeeded() const	{ return m_status == Status::kSucceeded; }
	bool		Failed() const { return m_status == Status::kFailed; }
	bool		Pending() const { return m_status == Status::kPending; }
	bool		InProgress() const { return m_status == Status::kInProgress; }

	float GetGoalRadius() const		{ return m_goalRadius; }

	// Configuration functions
	void AsStopSwimming(const NavCharacter* pNavChar, U32 id, const NavAnimStopParams& params)
	{
		Reset();
		m_type = Type::kStopSwimming;
		m_id = id;
		m_status = Status::kPending;
		m_goalRadius = Max(params.m_goalRadius, pNavChar->GetMotionConfig().m_minimumGoalRadius);
		m_stopParams = params;
	}

	void AsStartSwimming(const NavCharacter* pNavChar, U32 id, const NavAnimStartParams& params)
	{
		Reset();
		m_type = Type::kStartSwimming;
		m_status = Status::kPending;
		m_goalRadius = Max(params.m_moveArgs.m_goalRadius, pNavChar->GetMotionConfig().m_minimumGoalRadius);
		m_startParams = params;
	}

	const NavAnimStartParams& GetStartParams() const { return m_startParams; }
	const NavAnimStopParams& GetStopParams() const { return m_stopParams; }

	void SetInProgress() { m_status = Status::kInProgress; }
	void SetSucceeded()	{ m_status = Status::kSucceeded; }
	void SetFailed()	{ m_status = Status::kFailed; }

	const WaypointContract&	GetWaypointContract() const 
	{
		return m_type == Type::kStartSwimming ? m_startParams.m_waypoint : m_stopParams.m_waypoint; 
	}

	void Update(const WaypointContract& waypointContract)
	{
		if (m_type == Type::kStartSwimming)
		{
			m_startParams.m_waypoint = waypointContract;
		}
		else
		{
			m_stopParams.m_waypoint = waypointContract;
		}
	}

	bool CanSharePathInfoWith(const SwimCommand& otherCmd) const
	{
		if ((m_type != otherCmd.m_type) || (m_type != Type::kStartSwimming))
			return false;

		if (m_startParams.m_moveArgs.m_strafeMode != otherCmd.m_startParams.m_moveArgs.m_strafeMode)
			return false;

		const WaypointContract& wc		= GetWaypointContract();
		const WaypointContract& otherWc = otherCmd.GetWaypointContract();

		if (wc.m_reachedType != otherWc.m_reachedType)
			return false;

		return true;
	}

	void DebugPrint(DoutBase* pOutput, const char* preamble) const
	{
		pOutput->Printf("%s%d %s : %s\n", preamble, m_id, GetTypeName(m_type), GetStatusName(m_status));
		pOutput->Printf("%sGoal Radius: %0.3fm\n", preamble, m_goalRadius);
	
		const WaypointContract&	wc = GetWaypointContract();
		pOutput->Printf("%sReached Type: %s\n", preamble, GetGoalReachedTypeName(wc.m_reachedType));
	}

private:
	U32					m_id;
	Type				m_type;
	Status				m_status;
	float				m_goalRadius;

	NavAnimStartParams	m_startParams;
	NavAnimStopParams	m_stopParams;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AiSwimController : public IAiSwimController
{
public:
	typedef IAiSwimController ParentClass;

	struct StopParams
	{
		const ArtItemAnim* m_pStopAnim = nullptr;
	};

	/************************************************************************/
	/* FSM stuff                                                            */
	/************************************************************************/
	class BaseState : public Fsm::TState<AiSwimController>
	{
	public:
		virtual void RequestAnimations() {}
		virtual void UpdateStatus() {}
		virtual void PostRootLocatorUpdate() {}
		virtual bool IsBusy() const { return false; }
		virtual bool CanProcessCommand() const { return false; }

		virtual Point GetNavigationPosPs(Point_arg defPosPs) const { return defPosPs; }

		NavCharacter* GetCharacter() { return GetSelf()->GetCharacter(); }
		const NavCharacter* GetCharacter() const { return GetSelf()->GetCharacter(); }

		virtual void OnStartNavigating(const SwimCommand& cmd)
		{
			if (g_navCharOptions.m_swimController.m_useMmSwimming)
			{
				GetSelf()->GoMotionMatching();
			}
			else
			{
				GetSelf()->GoStarting();
			}
		}
		virtual void OnStopNavigating(const SwimCommand& cmd) {}

		virtual void DebugDraw() const {}

		U32 GetCommandId() const { return GetSelf()->GetCommandId(); }
		StringId64 GetAnimStateId() const { return GetSelf()->GetAnimStateId(); }
		const char* GetStateName(const char* nameIfNoState) const { return GetSelf()->GetStateName(nameIfNoState); }
	};

	class SwimControllerFsm : public Fsm::TStateMachine<BaseState>
	{
		virtual bool ShouldChangeStatePriorityTest(Fsm::StateDescriptor::Priority current,
												   Fsm::StateDescriptor::Priority desired) const override
		{
			return desired <= current;
		}

		const NavCharacter* GetCharacter() const { return GetSelf<const AiSwimController>()->GetCharacter(); }
		U32 GetCommandId() const { return GetSelf<const AiSwimController>()->GetCommandId(); }
		StringId64 GetAnimStateId() const { return GetSelf<const AiSwimController>()->GetAnimStateId(); }

		virtual void OnNewStateRequested(const Fsm::StateDescriptor& desc,
										 const void* pStateArg,
										 size_t argSize,
										 const char* srcFile,
										 U32 srcLine,
										 const char* srcFunc) const override
		{
			SwimLog("New state requested: %s\n", desc.GetStateName());
		}
	};

	typedef SwimControllerFsm StateMachine;

	FSM_BASE_DECLARE(StateMachine);
public:

	FSM_STATE_DECLARE(Interrupted);
	FSM_STATE_DECLARE_ARG(Handoff, NavAnimHandoffDesc);
	FSM_STATE_DECLARE(Stopped);
	FSM_STATE_DECLARE(Starting);
	FSM_STATE_DECLARE(Swimming);
	FSM_STATE_DECLARE(BlindMoving);
	FSM_STATE_DECLARE_ARG(Stopping, StopParams);
	FSM_STATE_DECLARE(Turning);
	FSM_STATE_DECLARE(Error);
	FSM_STATE_DECLARE_ARG(MotionMatching, DC::BlendParams);

	U32F GetMaxStateAllocSize() { return sizeof(BaseState) + 512; }
	void GoInitialState();
	const Clock* GetClock() const { return GetCharacter()->GetClock(); }

	/************************************************************************/
	/* AnimAction stuff                                                     */
	/************************************************************************/

	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	virtual void Reset() override;			// Reset to a working default state
	virtual void Interrupt() override;		// Interrupt the controller to prevent any further changes

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual void PostRootLocatorUpdate() override;
	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;
	virtual bool IsBusy() const override;

	/************************************************************************/
	/* INavAnimController Stuff                                             */
	/************************************************************************/

	virtual Point GetNavigationPosPs() const override;

	virtual void StartNavigating(const NavAnimStartParams& params) override;
	virtual void StopNavigating(const NavAnimStopParams& params) override;
	virtual bool CanProcessCommand() const override;
	virtual bool IsCommandInProgress() const override
	{
		return m_activeCommand.InProgress() || m_pendingCommand.IsValid();
	}

	virtual bool HasArrivedAtGoal() const override;
	virtual bool HasMissedWaypoint() const override;
	virtual NavGoalReachedType GetCurrentGoalReachedType() const override;
	virtual Locator GetGoalPs() const override;

	virtual bool IsMoving() const override;
	virtual bool IsInterrupted() const override;

	virtual void UpdatePathWaypointsPs(const PathWaypointsEx& pathWaypointsPs,
									   const WaypointContract& waypointContract) override;
	virtual void PatchCurrentPathEnd(const NavLocation& updatedEndLoc) override;
	virtual void InvalidateCurrentPath() override;

	virtual void SetMoveToTypeContinuePoseMatchAnim(const StringId64 animId, float startPhase) override;

	virtual bool ShouldDoProceduralApRef(const AnimStateInstance* pInstance) const override;
	virtual bool CalculateProceduralAlign(const AnimStateInstance* pInstance,
										  const Locator& baseAlignPs,
										  Locator& alignOut) const override;
	virtual bool CalculateProceduralApRef(AnimStateInstance* pInstance) const override;

	virtual bool ShouldAdjustHeadToNav() const override;
	virtual U32 GetDesiredLegRayCastMode() const override { return LegRaycaster::kModeInvalid; }

	virtual const char* GetDebugStatusStr() const override
	{
		const NavControl* pNavControl = GetNavControl();
		if (pNavControl && pNavControl->NavMeshForcesDive())
			return "(Diving) ";
		else
			return "(Swimming) ";
	}

	virtual void ForceDefaultIdleState(bool playAnim = true) override;
	virtual StateChangeRequest::ID FadeToIdleState(const DC::BlendParams* pBlend = nullptr) override;

	virtual U64 CollectHitReactionStateFlags() const override
	{
		if (!IsInterrupted())
		{
			return DC::kHitReactionStateMaskSwimming;
		}
	
		return 0;
	}

	virtual float GetMovementSpeedScale() const override;

	/************************************************************************/
	/* The good stuff                                                       */
	/************************************************************************/

	void IssueCommand(SwimCommand& cmd);
	void ProcessCommand(const SwimCommand& cmd);
	void ProcessCommand_StartNavigating(const SwimCommand& cmd);
	void ProcessCommand_StopNavigating(const SwimCommand& cmd);
	void UpdateCurrentMoveDirPs(bool snap);

	U32 GetCommandId() const
	{
		return m_nextCommandId;
	}

	StringId64 GetAnimStateId() const;
	
	void UpdateAnimOverlay();
	void UpdateActivePathInfo(const PathInfo& newPathInfo);
	void OnCommandFinished();
	bool IsAtEndOfPath() const;
	bool IsDesiredOrientationSatisfied() const;
	const ArtItemAnim* PickStopAnim(const PathInfo& pathInfo, float* pDistNeededOut = nullptr) const;

	bool IsAtEndOfWaypointPath(float padDist = 0.0f) const;
	Vector GetChosenFaceDirPs() const;

	void GoMotionMatching();

	U32			m_nextCommandId;
	SwimCommand	m_lastCommand;
	SwimCommand	m_activeCommand;
	SwimCommand	m_pendingCommand;

	PathInfo	m_pendingPathInfo;
	PathInfo	m_activePathInfo;
	I32			m_activePathLeg;

	Point		m_lastNavigationPosPs;
	Vector		m_desiredMoveDirPs;
	Vector		m_currentMoveDirPs;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	ParentClass::Init(pNavChar, pNavControl);

	size_t maxArgSize = 0;
	maxArgSize = Max(maxArgSize, sizeof(StopParams));
	maxArgSize = Max(maxArgSize, sizeof(NavAnimHandoffDesc));

	StateMachine& fsm = GetStateMachine();
	fsm.Init(this, FILE_LINE_FUNC, true, maxArgSize);

	m_nextCommandId = 0;
	m_lastCommand.Reset();
	m_activeCommand.Reset();
	m_pendingCommand.Reset();

	m_pendingPathInfo.Reset();
	m_activePathInfo.Reset();
	m_activePathLeg = -1;

	m_desiredMoveDirPs = m_currentMoveDirPs = kZero;
	m_lastNavigationPosPs = GetNavigationPosPs();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::GoInitialState()
{
	GoInterrupted();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Reset()
{
	m_activeCommand.Reset();
	m_pendingCommand.Reset();
	
	m_pendingPathInfo.Reset();
	m_activePathInfo.Reset();
	m_activePathLeg = -1;

	GoInterrupted();

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Interrupt()
{
	m_activeCommand.Reset();
	m_pendingCommand.Reset();

	m_pendingPathInfo.Reset();
	m_activePathInfo.Reset();
	m_activePathLeg = -1;

	GoInterrupted();

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	StateMachine& fsm = GetStateMachine();
	fsm.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::RequestAnimations()
{
	const bool interrupted = IsInterrupted();
	if (!interrupted)
	{
		UpdateAnimOverlay();
	}

	StateMachine& fsm = GetStateMachine();

	if (m_pendingCommand.IsValid() && CanProcessCommand())
	{
		ProcessCommand(m_pendingCommand);

		m_pendingCommand.Reset();

		fsm.TakeStateTransition();
	}

	if (BaseState* pState = GetState())
	{
		pState->RequestAnimations();
	}

	if (!interrupted)
	{
		const float moveDirErrDot = DotXz(m_currentMoveDirPs, m_desiredMoveDirPs);
		const float desiredSpeedScale = IsMoving() ? LerpScale(0.75f, 0.0f, 1.0f, 0.25f, moveDirErrDot) : 1.0f;

		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		DC::AnimNpcInfo* pInfo = pAnimControl->Info<DC::AnimNpcInfo>();
		pInfo->m_speedFactor = AI::TrackSpeedFactor(pNavChar, pInfo->m_speedFactor, desiredSpeedScale, 0.5f);
	}

	fsm.TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::UpdateStatus()
{
	StateMachine& fsm = GetStateMachine();

	fsm.TakeStateTransition();

	if (BaseState* pState = GetState())
	{
		pState->UpdateStatus();
	}

	fsm.TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void  AiSwimController::PostRootLocatorUpdate()
{
	StateMachine& fsm = GetStateMachine();

	fsm.TakeStateTransition();

	if (BaseState* pState = GetState())
	{
		pState->PostRootLocatorUpdate();
	}

	fsm.TakeStateTransition();

	m_lastNavigationPosPs = GetNavigationPosPs();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	if (!g_navCharOptions.m_swimController.m_display)
		return;

	if (!pPrinter)
		return;

	ScreenSpaceTextPrinter& printer = *pPrinter;

	const NavCharacter* pNavChar = GetCharacter();
	MsgCon("[%s] %s\n", pNavChar->GetName(), GetStateName("<none>"));

	if (m_lastCommand.IsValid())
	{
		MsgCon("Last Command:\n");
		m_lastCommand.DebugPrint(GetMsgOutput(kMsgCon), "  ");
	}

	if (m_activeCommand.IsValid())
	{
		MsgCon("Active Command:\n");
		m_activeCommand.DebugPrint(GetMsgOutput(kMsgCon), "  ");
	}

	if (m_pendingCommand.IsValid())
	{
		MsgCon("Pending Command:\n");
		m_pendingCommand.DebugPrint(GetMsgOutput(kMsgCon), "  ");
	}

	const Locator& parentSpace = pNavChar->GetParentSpace();

	PrimServerWrapper ps = PrimServerWrapper(parentSpace);

	const Point p = GetNavigationPosPs();

	StringBuilder<256> stateDesc;
	stateDesc.append_format("Swim Controller (%s)", GetStateMachine().GetStateName("<none>"));
	
	if (CanProcessCommand())
	{
		stateDesc.append_format(" Ready");
	}

	printer.PrintText(kColorWhite, stateDesc.c_str());


	ps.DrawArrow(p + Vector(0.0f, 0.4f, 0.0f), m_desiredMoveDirPs, 0.5f, kColorWhite);
	ps.DrawString(p + Vector(0.0f, 0.4f, 0.0f) + m_desiredMoveDirPs, "Des-Move", kColorWhite, 0.5f);

	ps.DrawArrow(p + Vector(0.0f, 0.52f, 0.0f), m_currentMoveDirPs, 0.5f, Color(1.0f, 0.6f, 0.35f));
	ps.DrawString(p + Vector(0.0f, 0.52f, 0.0f) + m_currentMoveDirPs, "Cur-Move", Color(1.0f, 0.6f, 0.35f), 0.5f);

	const Vector chosenFaceDirPs = GetChosenFaceDirPs();
	ps.DrawArrow(p + Vector(0.0f, 0.6f, 0.0f), chosenFaceDirPs, 0.5f, kColorYellow);
	ps.DrawString(p + Vector(0.0f, 0.6f, 0.0f) + chosenFaceDirPs, "Cho-Face", kColorYellow, 0.5f);

	if (const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs())
	{
		PathWaypointsEx::ColorScheme colors;
		colors.m_groundLeg0 = kColorCyan;
		colors.m_groundLeg1 = kColorBlue;
		colors.m_apLeg0 = colors.m_ledgeJump0 = colors.m_ledgeShimmy0 = kColorGray;
		colors.m_apLeg1 = colors.m_ledgeJump1 = colors.m_ledgeShimmy1 = kColorDarkGray;

		pPathPs->DebugDraw(parentSpace, false, 0.0f, colors);

		const Point endPosPs = pPathPs->GetEndWaypoint();
		const Vector wayFacePs = GetLocalZ(m_activeCommand.GetWaypointContract().m_rotPs);
		ps.DrawArrow(endPosPs + Vector(0.0f, 0.25f, 0.0f), wayFacePs, 0.5f, Color(0.5f, 0.5f, 1.0f));
		ps.DrawString(endPosPs + Vector(0.0f, 0.25f, 0.0f) + wayFacePs, "Way-Face", Color(0.5f, 0.5f, 1.0f), 0.5f);
	}

	if (const PathWaypointsEx* pPathPs = m_pendingPathInfo.GetPathWaypointsPs())
	{
		PathWaypointsEx::ColorScheme colors;
		colors.m_groundLeg0 = kColorCyanTrans;
		colors.m_groundLeg1 = kColorBlueTrans;
		colors.m_apLeg0 = colors.m_ledgeJump0 = colors.m_ledgeShimmy0 = kColorGrayTrans;
		colors.m_apLeg1 = colors.m_ledgeJump1 = colors.m_ledgeShimmy1 = kColorDarkGrayTrans;

		Locator pathSpace = parentSpace;
		pathSpace.SetPos(pathSpace.Pos() + Vector(0.0f, 0.2f, 0.0f));

		pPathPs->DebugDraw(pathSpace, false, 0.0f, colors);

		const Point endPosPs = pPathPs->GetEndWaypoint();
		const Vector wayFacePs = GetLocalZ(m_pendingCommand.GetWaypointContract().m_rotPs);
		ps.DrawArrow(endPosPs + Vector(0.0f, 0.25f, 0.0f), wayFacePs, 0.5f, Color(0.5f, 0.5f, 1.0f, 0.33f));
		ps.DrawString(endPosPs + Vector(0.0f, 0.25f, 0.0f) + wayFacePs, "Way-Face", Color(0.5f, 0.5f, 1.0f, 0.33f), 0.5f);
	}

	if (const BaseState* pState = GetState())
	{
		pState->DebugDraw();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::IsBusy() const
{
	const StateMachine& fsm = GetStateMachine();

	if (const BaseState* pState = GetState())
	{
		return pState->IsBusy();
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point AiSwimController::GetNavigationPosPs() const
{
	const NavCharacter* pNavChar = GetCharacter();
	Point posPs = pNavChar ? pNavChar->GetTranslationPs() : Point(kOrigin);

	if (const BaseState* pState = GetState())
	{
		posPs = pState->GetNavigationPosPs(posPs);
	}

	return posPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::StartNavigating(const NavAnimStartParams& params)
{
	const NavCharacter* pNavChar = GetCharacter();
	m_pendingCommand.AsStartSwimming(pNavChar, m_nextCommandId++, params);

	if (params.m_pPathPs)
	{
		UpdatePathWaypointsPs(*params.m_pPathPs, params.m_waypoint);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::StopNavigating(const NavAnimStopParams& params)
{
	const NavCharacter* pNavChar = GetCharacter();
	m_pendingCommand.AsStopSwimming(pNavChar, m_nextCommandId++, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::CanProcessCommand() const
{
	if (const BaseState* pState = GetState())
	{
		return pState->CanProcessCommand();
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::HasArrivedAtGoal() const
{
	return !m_pendingCommand.IsValid() && m_activeCommand.Succeeded();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::HasMissedWaypoint() const
{
	return !m_pendingCommand.IsValid() && m_activeCommand.Failed();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavGoalReachedType AiSwimController::GetCurrentGoalReachedType() const
{
	return kNavGoalReachedTypeStop;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AiSwimController::GetGoalPs() const
{
	const NavCharacter* pNavChar = GetCharacter();

	Locator goalPs = pNavChar->GetLocatorPs();

	if (IsCommandInProgress())
	{
		goalPs.SetRot(m_activeCommand.GetWaypointContract().m_rotPs);

		if (const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs())
		{
			goalPs.SetPos(pPathPs->GetEndWaypoint());
		}
	}

	return goalPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::IsMoving() const
{
	return IsState(kStateStarting) || IsState(kStateSwimming) || IsState(kStateStopping);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::IsInterrupted() const
{
	return IsState(kStateInterrupted);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::UpdatePathWaypointsPs(const PathWaypointsEx& pathWaypointsPs,
											 const WaypointContract& waypointContract)
{
	if (m_pendingCommand.IsValid())
	{
		m_pendingCommand.Update(waypointContract);

		if (!m_activeCommand.CanSharePathInfoWith(m_pendingCommand))
		{
			return;
		}
	}

	m_activePathInfo.SetPathWaypointsPs(pathWaypointsPs);
	m_activeCommand.Update(waypointContract);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::PatchCurrentPathEnd(const NavLocation& updatedEndLoc)
{
	// Don't listen to information unless we are actually processing a command
	if (!IsCommandInProgress())
		return;

	if (PathWaypointsEx* pActivePathPs = m_activePathInfo.GetPathWaypointsPs())
	{
		pActivePathPs->UpdateEndLoc(GetCharacter()->GetParentSpace(), updatedEndLoc);
		m_activePathInfo.SetPathWaypointsPs(*pActivePathPs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::InvalidateCurrentPath()
{
	SwimLogStr("InvalidateCurrentPath()\n");

	m_pendingPathInfo.Reset();
	m_activePathInfo.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::SetMoveToTypeContinuePoseMatchAnim(const StringId64 animId, float startPhase)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::ShouldDoProceduralApRef(const AnimStateInstance* pInstance) const
{
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::CalculateProceduralAlign(const AnimStateInstance* pInstance,
												const Locator& baseAlignPs,
												Locator& alignOut) const
{
	const bool moving = IsState(kStateSwimming);
	const DC::AnimStateFlag stateFlags = pInstance->GetStateFlags();
	const bool noProcRot = (stateFlags & DC::kAnimStateFlagNoProcRot);
	const bool apMoveUpdate = (stateFlags & DC::kAnimStateFlagApMoveUpdate);
	const bool animOnlyMoveUpdate = (stateFlags & DC::kAnimStateFlagAnimOnlyMoveUpdate);
	const bool ffaMoveUpdate = (stateFlags & DC::kAnimStateFlagFirstAlignRefMoveUpdate);
	const bool applyProcRotation = (moving && !noProcRot && !apMoveUpdate && !animOnlyMoveUpdate && !ffaMoveUpdate);

	// no procedural align needed
	if (!applyProcRotation)
		return false;

	const Locator alignDelta = pInstance->GetChannelDelta(SID("align"));

	const Vector animMoveVecPs = alignDelta.Pos() - kOrigin;
	const Vector animMoveDirPs = SafeNormalize(animMoveVecPs, m_currentMoveDirPs);

	const Quat rotPs = QuatFromLookAt(m_currentMoveDirPs, kUnitYAxis);
	const Vector deltaPos = Length(animMoveVecPs) * m_currentMoveDirPs;

	alignOut = Locator(baseAlignPs.Pos() + deltaPos, rotPs);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::CalculateProceduralApRef(AnimStateInstance* pInstance) const
{
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::ShouldAdjustHeadToNav() const
{
	return IsState(kStateSwimming);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::ForceDefaultIdleState(bool playAnim /*= true*/)
{
	if (playAnim)
	{
		FadeToIdleState();
	}

	m_activePathInfo.Reset();
	m_pendingPathInfo.Reset();

	m_currentMoveDirPs = m_desiredMoveDirPs = kZero;

	GoStopped();
	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StateChangeRequest::ID AiSwimController::FadeToIdleState(const DC::BlendParams* pBlend /* = nullptr */)
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetStateLayerById(SID("base")) : nullptr;

	UpdateAnimOverlay();

	StateChangeRequest::ID changeId = StateChangeRequest::kInvalidId;

	if (pBaseLayer)
	{
		const StringId64 idleStateId = SID("s_swim-idle");

		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
		FadeToStateParams params;
		if (pBlend)
		{
			params.m_animFadeTime	= pBlend->m_animFadeTime;
			params.m_motionFadeTime = pBlend->m_motionFadeTime;
			params.m_blendType		= pBlend->m_curve;
		}
		else
		{
			params.m_animFadeTime = 0.0f;
		}
		
		changeId = pBaseLayer->FadeToState(idleStateId, params);
	}

	return changeId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiSwimController::GetMovementSpeedScale() const
{
	float scale = 1.0f;

	if (IsMoving())
	{
		const float moveDot = Dot(m_currentMoveDirPs, m_desiredMoveDirPs);
		scale = LerpScale(0.0f, 1.0f, 0.65f, 1.0f, Abs(moveDot));
	}

	return scale;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::ProcessCommand(const SwimCommand& cmd)
{
	m_activePathInfo = m_pendingPathInfo;
	m_pendingPathInfo.Reset();

	m_lastCommand = m_activeCommand;
	m_activeCommand = cmd;

	m_activeCommand.SetInProgress();

	UpdateAnimOverlay();

	switch (cmd.GetType())
	{
	case SwimCommand::Type::kStartSwimming:
		ProcessCommand_StartNavigating(cmd);
		break;

	case SwimCommand::Type::kStopSwimming:
		ProcessCommand_StopNavigating(cmd);
		break;

	default:
		AI_ASSERTF(false, ("Unknown swim command '%s'", cmd.GetTypeName()));
		return;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::ProcessCommand_StartNavigating(const SwimCommand& cmd)
{
	const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs();

	if (!pPathPs)
	{
		m_desiredMoveDirPs = cmd.GetStartParams().m_moveDirPs;

		if (!IsMoving())
		{
			m_currentMoveDirPs = m_desiredMoveDirPs;
		}
	}

	const bool translationSatisifed = IsAtEndOfPath();
	const bool orientationSatisfied = IsDesiredOrientationSatisfied();

	if (translationSatisifed && orientationSatisfied && !IsState(kStateInterrupted))
	{
		OnCommandFinished();
	}
	else if (BaseState* pState = GetState())
	{
		pState->OnStartNavigating(cmd);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::ProcessCommand_StopNavigating(const SwimCommand& cmd)
{
	PathInfo newPathInfo;
	newPathInfo.Reset();

	if (IsMoving())
	{
		StopParams params;
		params.m_pStopAnim = PickStopAnim(newPathInfo);
		GoStopping(params);
	}
	else
	{
		GoStopped();
	}

	UpdateActivePathInfo(newPathInfo);

	if (BaseState* pState = GetState())
	{
		pState->OnStopNavigating(cmd);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::UpdateCurrentMoveDirPs(bool snap)
{
	NavCharacter* pNavChar = GetCharacter();

	if (snap)
	{
		m_currentMoveDirPs = m_desiredMoveDirPs;
	}
	else
	{
		const float dt = pNavChar->GetClock()->GetDeltaTimeInSeconds();

		float swimRotFilter = 1.6f;

		if (const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs())
		{
			const float curPathLength = pPathPs->ComputePathLengthXz();

			swimRotFilter = LerpScale(0.75f, 2.0f, 5.0f, 1.0f, curPathLength);
		}

		const float alpha = Limit01(dt * swimRotFilter);

		m_currentMoveDirPs = AsUnitVectorXz(Slerp(m_currentMoveDirPs, m_desiredMoveDirPs, alpha), m_desiredMoveDirPs);
		/*
		const float curHeading = GetXZAngleForVecDeg(m_currentMoveDirPs);
		const float desHeading = GetXZAngleForVecDeg(m_desiredMoveDirPs);
		const float newHeading = Lerp(curHeading, desHeading, );
		m_currentMoveDirPs = GetVectorForXZAngleDeg(newHeading);
		*/
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiSwimController::GetAnimStateId() const
{
	STRIP_IN_FINAL_BUILD_VALUE(INVALID_STRING_ID_64);

	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const StringId64 stateId = pBaseLayer ? pBaseLayer->CurrentStateId() : INVALID_STRING_ID_64;
	return stateId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::UpdateAnimOverlay()
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimOverlays* pOverlays = pAnimControl ? pAnimControl->GetAnimOverlays() : nullptr;
	const NavControl* pNavControl = GetNavControl();

	if (!pOverlays || !pNavControl)
		return;

	StringId64 overlayId = INVALID_STRING_ID_64;

	if (pNavControl->NavMeshForcesDive())
	{
		overlayId = AI::CreateNpcAnimOverlayName(pNavChar->GetOverlayBaseName(), "move-set", "dive");
	}
	else
	{
		overlayId = AI::CreateNpcAnimOverlayName(pNavChar->GetOverlayBaseName(), "move-set", "swim");
	}

	pOverlays->SetOverlaySet(overlayId, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::UpdateActivePathInfo(const PathInfo& newPathInfo)
{
	m_activePathInfo = newPathInfo;
	const U32F newWaypointCount = m_activePathInfo.GetWaypointCount();

	const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs();

	const Point myPosPs = GetNavigationPosPs();

	NavUtil::CalculateAdvancementOnPath(myPosPs, pPathPs, &m_activePathLeg);

	if ((newWaypointCount > 0) && (m_activePathLeg >= 0) && m_activePathLeg < (newWaypointCount - 1))
	{
		NAV_ASSERT(pPathPs);

		const Point nextPosPs = pPathPs->GetWaypoint(m_activePathLeg + 1);
		m_desiredMoveDirPs = SafeNormalize(VectorXz(nextPosPs - myPosPs), kUnitZAxis);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::OnCommandFinished()
{
	const bool translationSatisifed = IsAtEndOfPath();
	const bool orientationSatisfied = IsDesiredOrientationSatisfied();

	if (translationSatisifed && orientationSatisfied)
	{
		m_activeCommand.SetSucceeded();
	}
	else
	{
		m_activeCommand.SetFailed();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::IsAtEndOfPath() const
{
	const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs();

	if (!pPathPs)
	{
		return Length(m_activeCommand.GetStartParams().m_moveDirPs) > NDI_FLT_EPSILON;
	}

	const Point myPosPs = GetNavigationPosPs();
	const Point pathEndPs = pPathPs->GetEndWaypoint();
	const float goalRadius = m_activeCommand.GetGoalRadius();

	const float distErr = Dist(myPosPs, pathEndPs);

	return distErr <= Max(goalRadius, kSwimCommandDistTolerance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::IsDesiredOrientationSatisfied() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const Vector goalFacingPs = GetLocalZ(m_activeCommand.GetWaypointContract().m_rotPs);
	const Vector myFacingPs = GetLocalZ(pNavChar->GetRotationPs());

	const float dotP = Dot(goalFacingPs, myFacingPs);
	const float angleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(dotP));

	return angleDiffDeg < kMinTurnAngle;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemAnim* AiSwimController::PickStopAnim(const PathInfo& pathInfo, float* pDistNeededOut /* = nullptr */) const
{
	const PathWaypointsEx* pPathPs = pathInfo.GetPathWaypointsPs();
	const WaypointContract& waypoint = m_activeCommand.GetWaypointContract();

	const bool hasPath = pPathPs != nullptr;

	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const SkeletonId skelId = pNavChar->GetSkeletonId();

	const Vector desFaceDirPs = GetLocalZ(waypoint.m_rotPs);

	const Point myPosPs = GetNavigationPosPs();
	const Vector curForwardPs = GetLocalZ(pNavChar->GetRotationPs());

	float stopDistanceNeeded = 0.0f;

	const U32F numWaypoints = pathInfo.GetWaypointCount();
	const Point endPosPs = hasPath ? pPathPs->GetWaypoint(numWaypoints - 1) : myPosPs;

	Vector approachDirPs = hasPath ? SafeNormalize(endPosPs - myPosPs, curForwardPs) : curForwardPs;

	if (hasPath && (numWaypoints > 2))
	{
		const Point preEndPosPs = pPathPs->GetWaypoint(numWaypoints - 2);

		approachDirPs = SafeNormalize(endPosPs - preEndPosPs, approachDirPs);
	}

	const Locator endLocPs = Locator(endPosPs, QuatFromLookAt(approachDirPs, kUnitYAxis));
	const Vector desiredFaceDirPs = GetLocalZ(waypoint.m_rotPs);
	const Vector desiredFaceDirLs = endLocPs.UntransformVector(desiredFaceDirPs);

	const StringId64 baseAnimNames[] =
	{
		SID("swim^idle-fw"),
		SID("swim^idle-bw-lt"),
		SID("swim^idle-bw-rt"),
		SID("swim^idle-lt"),
		SID("swim^idle-rt")
	};

	const ArtItemAnim* pStoppingAnim = nullptr;
	float bestMatchDot = -kLargeFloat;

	for (U32F iAnim = 0; iAnim < ARRAY_COUNT(baseAnimNames); ++iAnim)
	{
		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(baseAnimNames[iAnim]).ToArtItem();
		if (!pAnim)
			continue;

		Locator endLocLs;
		if (!EvaluateChannelInAnim(skelId, pAnim, SID("align"), 1.0f, &endLocLs))
			continue;

		const Vector animFaceDirPs = GetLocalZ(endLocLs.Rot());
		const float rating = Dot(desiredFaceDirLs, animFaceDirPs);

		if (rating > bestMatchDot)
		{
			bestMatchDot = rating;
			pStoppingAnim = pAnim;
			stopDistanceNeeded = Dist(endLocLs.Pos(), kOrigin);
		}
	}

	if (pDistNeededOut)
		*pDistNeededOut = stopDistanceNeeded;

	return pStoppingAnim;
}


/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::IsAtEndOfWaypointPath(float padDist /* = 0.0f */) const
{
	const NavCharacter* pNavChar = GetCharacter();

	if (!m_activePathInfo.IsValid())
	{
		return false;
	}

	const MotionConfig& mc = pNavChar->GetMotionConfig();
	const float goalRadius = Max(m_activeCommand.GetGoalRadius() + padDist, mc.m_minimumGoalRadius);

	// Check if we are too far away from our goal and we need to do some cleanup
	const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs();
	if (pPathPs && (m_activeCommand.GetType() != SwimCommand::Type::kStopSwimming))
	{
		const U32F waypointCount = pPathPs->GetWaypointCount();
		if (waypointCount >= 2)
		{
			for (int i = 0; i < waypointCount; ++i)
			{
				const Point pointOnPathPs = pPathPs->GetWaypoint(i);

				const NavControl* pNavControl = pNavChar->GetNavControl();
				const NavMeshHandle hCurMesh = pNavControl ? pNavControl->GetNavMeshHandle() : NavMeshHandle();
				const NavMeshHandle hDestMesh = NavMeshHandle(pPathPs->GetEndNavId());

				if (hCurMesh != hDestMesh)
				{
					return false;
				}

				const Point curPosPs = pNavChar->GetTranslationPs();
				const Vector xzMove = VectorXz(pointOnPathPs - curPosPs);
				const Scalar xzDist = Length(xzMove);

				if (xzDist > goalRadius)
				{
					return false;
				}
			}
		}
		else
		{
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector AiSwimController::GetChosenFaceDirPs() const
{
	const NavCharacter* pNavChar = GetCharacter();

	const WaypointContract& wc = m_activeCommand.GetWaypointContract();

	const bool reachedTypeStop = wc.m_reachedType == kNavGoalReachedTypeStop;
	const bool isStopCmd = m_activeCommand.GetType() == SwimCommand::Type::kStopSwimming;

	const bool wantToStop = reachedTypeStop || isStopCmd;

	Vector choFacePs = kUnitZAxis;
	const float padDist = pNavChar->GetMotionConfig().m_minimumGoalRadius;

	Maybe<Vector> overrideFacingPs = pNavChar->GetOverrideChosenFacingDirPs();
	if (overrideFacingPs.Valid())
	{
		choFacePs = overrideFacingPs.Get();
	}
	else if ((IsAtEndOfWaypointPath(padDist) || !m_activePathInfo.IsValid()) && wantToStop)
	{
		ANIM_ASSERT(IsFinite(wc.m_rotPs));

		choFacePs = GetLocalZ(wc.m_rotPs);
	}
	else if (IsMoving())
	{
		if (IsStrafing())
		{
			choFacePs = CalculateFaceDirXzPs(pNavChar);
		}
		else
		{
			ANIM_ASSERT(IsReasonable(m_currentMoveDirPs));

			choFacePs = m_currentMoveDirPs;
		}
	}
	else
	{
		choFacePs = CalculateFaceDirXzPs(pNavChar);
	}

	ANIM_ASSERT(IsReasonable(choFacePs));

	return choFacePs;
}

/************************************************************************/
/* Interrupted                                                          */
/************************************************************************/

class AiSwimController::Interrupted : public AiSwimController::BaseState
{
public:
	virtual void OnStartNavigating(const SwimCommand& cmd) override;
	virtual void OnStopNavigating(const SwimCommand& cmd) override;

	virtual bool CanProcessCommand() const override { return true; }
	virtual bool IsBusy() const override { return false; }

	void OnCommandStarted(const SwimCommand& cmd);
	bool IsInKnownMovingState() const;
	bool IsInKnownIdleState() const;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Interrupted::OnStartNavigating(const SwimCommand& cmd)
{
	OnCommandStarted(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Interrupted::OnStopNavigating(const SwimCommand& cmd)
{
	OnCommandStarted(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Interrupted::OnCommandStarted(const SwimCommand& cmd)
{
	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	const NavAnimHandoffDesc handoff = pNavChar->PopValidHandoff();

	if (pSelf->m_pendingPathInfo.IsValid())
	{
		pSelf->UpdateActivePathInfo(pSelf->m_pendingPathInfo);
		pSelf->m_pendingPathInfo.Reset();
	}

	if (handoff.IsValid(pNavChar))
	{
		pSelf->GoHandoff(handoff);
	}
	else if (IsInKnownMovingState())
	{
		if (g_navCharOptions.m_swimController.m_useMmSwimming)
		{
			pSelf->GoMotionMatching();
		}
		else if (pSelf->m_activePathInfo.IsValid())
		{
			pSelf->GoSwimming();
		}
		else
		{
			pSelf->GoBlindMoving();
		}
	}
	else if (IsInKnownIdleState())
	{
		pSelf->GoStopped();
	}
	else
	{
		if (FALSE_IN_FINAL_BUILD(true))
		{
			g_prim.Draw(DebugString(pNavChar->GetAimOriginWs(),
									"Swim controller trying to leave Interrupted state with no valid handoff descriptor!\n",
									kColorRed),
						Seconds(10.0f));
		}

		pSelf->ForceDefaultIdleState();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::Interrupted::IsInKnownMovingState() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const StringId64 stateId = pBaseLayer->CurrentStateId();

	bool known = false;

	switch (stateId.GetValue())
	{
	case SID_VAL("s_swim-move"):
		known = true;
		break;
	}

	return known;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::Interrupted::IsInKnownIdleState() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const StringId64 stateId = pBaseLayer->CurrentStateId();

	bool known = false;

	switch (stateId.GetValue())
	{
	case SID_VAL("s_swim-idle"):
	case SID_VAL("s_idle"):
		known = true;
		break;
	}

	return known;
}


FSM_STATE_REGISTER(AiSwimController, Interrupted, kPriorityMedium);

/************************************************************************/
/* Handoff                                                              */
/************************************************************************/

class AiSwimController::Handoff : public AiSwimController::BaseState
{
public:
	virtual void OnEnter() override;
	virtual void RequestAnimations() override;
	
	bool ShouldBlendOut(const AnimStateInstance* pInstance) const;
	void BlendOut(StringId64 stateId);
	void BlendOutToStopped();
	void BlendOutToSwimming();

	bool WasInScriptedAnimation() const
	{
		const StringId64 animStateId = GetAnimStateId();
		return IsKnownScriptedAnimationState(animStateId);
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Handoff::OnEnter()
{
	BaseState::OnEnter();

	NavCharacter* pNavChar = GetCharacter();
	AiSwimController* pSelf = GetSelf();
	const NavAnimHandoffDesc& handoffDesc = GetStateArg<NavAnimHandoffDesc>();

	AI_ASSERT(handoffDesc.IsValid(pNavChar));
	if (!handoffDesc.IsValid(pNavChar))
	{
		BlendOutToStopped();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Handoff::RequestAnimations()
{
	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
	const NavAnimHandoffDesc& handoffDesc = GetStateArg<NavAnimHandoffDesc>();

	if (!pCurInstance)
	{
		BlendOutToStopped();
		return;
	}

	if (pSelf->m_pendingPathInfo.IsValid())
	{
		pSelf->UpdateActivePathInfo(pSelf->m_pendingPathInfo);
		pSelf->m_pendingPathInfo.Reset();
	}

	if ((pCurInstance->Phase() >= handoffDesc.m_steeringPhase) && (handoffDesc.m_steeringPhase >= 0.0f))
	{
		pSelf->UpdateCurrentMoveDirPs(false);
	}
	else
	{
		pSelf->UpdateCurrentMoveDirPs(true);
	}

	if (!handoffDesc.IsValid(pNavChar))
	{
		BlendOutToStopped();
	}
	else if (ShouldBlendOut(pCurInstance))
	{
		if (handoffDesc.m_motionType != kMotionTypeMax)
		{
			BlendOutToSwimming();
		}
		else
		{
			BlendOutToStopped();
		}

		if (pSelf->m_activeCommand.IsValid())
		{
			if (g_navCharOptions.m_swimController.m_useMmSwimming)
			{
				pSelf->GoMotionMatching();
			}
			else
			{
				switch (pSelf->m_activeCommand.GetType())
				{
				case SwimCommand::Type::kStartSwimming:
					pSelf->GoStarting();
					break;

				case SwimCommand::Type::kStopSwimming:
					StopParams params;
					params.m_pStopAnim = pSelf->PickStopAnim(pSelf->m_activePathInfo);
					pSelf->GoStopping(params);
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::Handoff::ShouldBlendOut(const AnimStateInstance* pInstance) const
{
	const AiSwimController* pSelf = GetSelf();
	const NavAnimHandoffDesc& handoffDesc = GetStateArg<NavAnimHandoffDesc>();

	const float dotP = DotXz(pSelf->m_desiredMoveDirPs, pSelf->m_currentMoveDirPs);
	const float angleDeg = RADIANS_TO_DEGREES(SafeAcos(dotP));

	if (angleDeg > 15.0f)
	{
		return true;
	}

	const StringId64 stateId = pInstance ? pInstance->GetStateName() : INVALID_STRING_ID_64;

	if (stateId == SID("s_swim-idle"))
	{
		return true;
	}

	if (pInstance && (pInstance->GetPhase() >= handoffDesc.m_exitPhase))
	{
		return true;
	}

	if (pSelf->m_activeCommand.InProgress() && (pSelf->m_activeCommand.GetType() == SwimCommand::Type::kStopSwimming))
	{
		return true;
	}

	if (const ArtItemAnim* pAnim = pInstance->GetPhaseAnimArtItem().ToArtItem())
	{
		const float remTime = (1.0f - pInstance->GetPhase()) * GetDuration(pAnim);

		if (remTime < 1.0f)
		{
			return true;
		}
	}
	else
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Handoff::BlendOut(StringId64 stateId)
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	FadeToStateParams params;
	params.m_motionFadeTime = 0.2f;
	params.m_animFadeTime = 0.4f;
	params.m_blendType = DC::kAnimCurveTypeUniformS;

	if (WasInScriptedAnimation())
	{
		if (const SsAnimateController* pScriptController = pNavChar->GetPrimarySsAnimateController())
		{
			const SsAnimateParams& igcParams = pScriptController->GetParams();

			params.m_animFadeTime = igcParams.m_fadeOutSec;
			params.m_motionFadeTime = igcParams.GetMotionFadeOutSec();
			params.m_blendType = igcParams.m_fadeOutCurve;
		}
	}

	pBaseLayer->FadeToState(stateId, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Handoff::BlendOutToStopped()
{
	AiSwimController* pSelf = GetSelf();

	if (g_navCharOptions.m_swimController.m_useMmSwimming)
	{
		pSelf->GoMotionMatching();
	}
	else
	{
		BlendOut(SID("s_swim-idle"));

		pSelf->GoStopped();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Handoff::BlendOutToSwimming()
{
	AiSwimController* pSelf = GetSelf();

	if (g_navCharOptions.m_swimController.m_useMmSwimming)
	{
		pSelf->GoMotionMatching();
	}
	else
	{
		BlendOut(SID("s_swim-move"));

		pSelf->GoBlindMoving();
	}
}

FSM_STATE_REGISTER_ARG(AiSwimController, Handoff, kPriorityMedium, NavAnimHandoffDesc);

/************************************************************************/
/* Stopped                                                              */
/************************************************************************/

class AiSwimController::Stopped : public AiSwimController::BaseState
{
	virtual void OnEnter() override;

	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual bool CanProcessCommand() const override { return true; }
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Stopped::OnEnter()
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	AiSwimController* pSelf = GetSelf();

	pSelf->m_currentMoveDirPs = kZero;

	if (pBaseLayer)
	{
		if (pBaseLayer->CurrentStateId() != SID("s_swim-idle"))
		{
			FadeToStateParams params;
			params.m_animFadeTime = 0.2f;
			pBaseLayer->FadeToState(SID("s_swim-idle"), params);
		}
	}
	else
	{
		pSelf->GoInterrupted();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Stopped::RequestAnimations()
{
	AiSwimController* pSelf = GetSelf();
	pSelf->UpdateCurrentMoveDirPs(true);

	const bool activeCommandNotDone = pSelf->m_activeCommand.Pending() || pSelf->m_activeCommand.InProgress();

	if (activeCommandNotDone)
	{
		if ((pSelf->m_activeCommand.GetType() == SwimCommand::Type::kStartSwimming))
		{
			if (g_navCharOptions.m_swimController.m_useMmSwimming)
			{
				pSelf->GoMotionMatching();
			}
			else
			{
				pSelf->GoStarting();
			}
		}
		else
		{
			pSelf->m_activeCommand.SetSucceeded();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Stopped::UpdateStatus()
{
}

FSM_STATE_REGISTER(AiSwimController, Stopped, kPriorityMedium);

/************************************************************************/
/* Starting                                                             */
/************************************************************************/

class AiSwimController::Starting : public AiSwimController::BaseState
{
	virtual void OnEnter() override;

	virtual void UpdateStatus() override;
	virtual void PostRootLocatorUpdate() override;
	virtual bool IsBusy() const override { return true; }
	virtual bool CanProcessCommand() const override { return false; }
	virtual void DebugDraw() const override {}

	const ArtItemAnim* PickStartingAnim() const;

	AnimActionWithSelfBlend m_startAction;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Starting::OnEnter()
{
	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	//pSelf->UpdateCurrentMoveDirPs(true);

	const BoundFrame initialApRef = pNavChar->GetBoundFrame();

	const ArtItemAnim* pStartAnim = PickStartingAnim();

	BoundFrame targetApRef = initialApRef;

	Locator endLocLs;
	const SkeletonId skelId = pNavChar->GetSkeletonId();

	if (pStartAnim && EvaluateChannelInAnim(skelId, pStartAnim, SID("align"), 1.0f, &endLocLs))
	{
		const Vector animMoveDirLs = SafeNormalize(endLocLs.Pos() - kOrigin, kZero);
		const Vector animMoveDirPs = pNavChar->GetLocatorPs().TransformVector(animMoveDirLs);
		const Quat rotAdjustPs = QuatFromVectors(animMoveDirPs, pSelf->m_currentMoveDirPs);

		targetApRef.AdjustRotationPs(rotAdjustPs);
	}
	else
	{
		const Quat targetRotPs = QuatFromLookAt(pSelf->m_currentMoveDirPs, kUnitYAxis);
		targetApRef.SetRotationPs(targetRotPs);
	}

	AI::SetPluggableAnim(pNavChar, pStartAnim ? pStartAnim->GetNameId() : INVALID_STRING_ID_64);

	m_startAction.Request(pAnimControl, SID("swim"), AnimAction::kFinishOnNonTransitionalStateReached, &initialApRef);
	m_startAction.SetSelfBlendApRef(targetApRef);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Starting::UpdateStatus()
{
	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	
	m_startAction.Update(pAnimControl);

	const AnimStateInstance* pDestInstance = m_startAction.GetTransitionDestInstance(pAnimControl);

	if (m_startAction.WasTransitionTakenThisFrame() && pDestInstance && ((pDestInstance->GetStateFlags() & DC::kAnimStateFlagTransitional) != 0))
	{
		const DC::SelfBlendParams sb = NavUtil::ConstructSelfBlendParams(pDestInstance);
		m_startAction.SetSelfBlendParams(&sb, pNavChar, 1.0f);
	}

	if (!m_startAction.IsValid())
	{
		pSelf->GoStopped();
	}
	else if (m_startAction.IsDone())
	{
		pSelf->GoSwimming();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Starting::PostRootLocatorUpdate()
{
	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const Vector charDirPs = GetLocalZ(pNavChar->GetRotationPs());

	if (m_startAction.IsSelfBlendComplete())
	{
		pSelf->GoSwimming();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemAnim* AiSwimController::Starting::PickStartingAnim() const
{
	const StringId64 baseAnimNames[] =
	{
		SID("idle^swim-fw"),
		SID("idle^swim-lt"),
		SID("idle^swim-rt"),
		SID("idle^swim-bw-lt"),
		SID("idle^swim-bw-rt"),
	};

	const AiSwimController* pSelf = GetSelf();
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const SkeletonId skelId = pNavChar->GetSkeletonId();
	const ArtItemAnim* pBestAnim = nullptr;
	float bestMatchDot = -kLargeFloat;

	const Vector desMoveDirLs = pNavChar->GetLocatorPs().UntransformVector(pSelf->m_desiredMoveDirPs);

	for (U32F iAnim = 0; iAnim < ARRAY_COUNT(baseAnimNames); ++iAnim)
	{
		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(baseAnimNames[iAnim]).ToArtItem();
		if (!pAnim)
			continue;

		Locator endLocLs;
		if (!EvaluateChannelInAnim(skelId, pAnim, SID("align"), 1.0f, &endLocLs))
			continue;
		
		const Vector animMoveDirLs = SafeNormalize(endLocLs.Pos() - kOrigin, kZero);
		const float rating = Dot(desMoveDirLs, animMoveDirLs);

		if (rating > bestMatchDot)
		{
			bestMatchDot = rating;
			pBestAnim = pAnim;
		}
	}

	return pBestAnim;
}

FSM_STATE_REGISTER(AiSwimController, Starting, kPriorityMedium);

/************************************************************************/
/* Swimming                                                             */
/************************************************************************/

class AiSwimController::Swimming : public AiSwimController::BaseState
{
	virtual void OnEnter() override;

	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual void PostRootLocatorUpdate() override;
	virtual bool CanProcessCommand() const override { return true; }
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Swimming::OnEnter()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Swimming::RequestAnimations()
{
	AiSwimController* pSelf = GetSelf();

	if (pSelf->m_pendingPathInfo.IsValid())
	{
		pSelf->UpdateActivePathInfo(pSelf->m_pendingPathInfo);
		pSelf->m_pendingPathInfo.Reset();
	}

	pSelf->UpdateCurrentMoveDirPs(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Swimming::UpdateStatus()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Swimming::PostRootLocatorUpdate()
{
	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	float stopDistanceNeeded = 0.0f;
	const ArtItemAnim* pStopAnim = pSelf->PickStopAnim(pSelf->m_activePathInfo, &stopDistanceNeeded);
	if (!pStopAnim)
	{
		stopDistanceNeeded = 0.0f;
	}

	const PathWaypointsEx* pPathPs = pSelf->m_activePathInfo.GetPathWaypointsPs();

	const Point posPs		= pSelf->GetNavigationPosPs();
	const Point lastPosPs	= pSelf->m_lastNavigationPosPs;
	const Point pathEndPs	= pPathPs ? pPathPs->GetEndWaypoint() : posPs;
	const float kEpsilon	= 0.05f;
	const Vector toEndPs	= pathEndPs - posPs;

	const Vector prevToGoalPs		= pathEndPs - lastPosPs;
	const Vector newToGoalPs		= pathEndPs - posPs;
	const float dotP1				= DotXz(prevToGoalPs, toEndPs);
	const float dotP2				= DotXz(newToGoalPs, toEndPs);
	const bool passedThisFrame		= (dotP1 > 0.0f) && (dotP2 < kEpsilon);
	const bool alreadyPassed		= (dotP1 < 0.0f) && (dotP2 < kEpsilon) && (DistXz(posPs, pathEndPs) < kMinStoppingDistance);
	const bool passedOurDestPoint	= (passedThisFrame || alreadyPassed);

	bool stopDistanceMet = false;

	if (pPathPs && pPathPs->GetWaypointCount() == 2)
	{
		const float remDist = LengthXz(toEndPs);
		const float stopDist = Max(stopDistanceNeeded, kMinStoppingDistance);
		if (remDist <= stopDist)
		{
			stopDistanceMet = true;
		}
	}

	if (passedOurDestPoint || stopDistanceMet)
	{
		if (pSelf->m_activeCommand.GetWaypointContract().m_reachedType == kNavGoalReachedTypeStop)
		{
			StopParams params;
			params.m_pStopAnim = pStopAnim;
			pSelf->GoStopping(params);
		}
		else
		{
			pSelf->OnCommandFinished();

			pSelf->GoBlindMoving();
		}
	}
}

FSM_STATE_REGISTER(AiSwimController, Swimming, kPriorityMedium);

/************************************************************************/
/* BlindMoving                                                          */
/************************************************************************/

class AiSwimController::BlindMoving : public AiSwimController::BaseState
{
	virtual bool CanProcessCommand() const override { return true; }
	virtual bool IsBusy() const override { return false; }
};

FSM_STATE_REGISTER(AiSwimController, BlindMoving, kPriorityMedium);

/************************************************************************/
/* Stopping                                                             */
/************************************************************************/

class AiSwimController::Stopping : public AiSwimController::BaseState
{
	virtual void OnEnter() override;

	virtual void UpdateStatus() override;

	virtual bool CanProcessCommand() const override { return m_readyToMoveAgain; }
	virtual bool IsBusy() const override { return !m_readyToMoveAgain; }

	bool ShouldTurn() const;

	AnimActionWithSelfBlend m_stopAction;
	bool m_readyToMoveAgain;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Stopping::OnEnter()
{
	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const SkeletonId skelId = pNavChar->GetSkeletonId();

	//pSelf->UpdateCurrentMoveDirPs(true);

	const StopParams& params = GetStateArg<StopParams>();

	const Point myPosPs = pSelf->GetNavigationPosPs();
	float startPhase = 0.0f;
	BoundFrame initialApRef = pNavChar->GetBoundFrame();

	if (const ArtItemAnim* pStoppingAnim = params.m_pStopAnim)
	{
		if (const PathWaypointsEx* pPathPs = pSelf->m_activePathInfo.GetPathWaypointsPs())
		{
			const Point stopPosPs = pPathPs->GetEndWaypoint();
			const float remainingDist = DistXz(myPosPs, stopPosPs);

			startPhase = ComputePhaseToMatchDistanceFromEnd(skelId, pStoppingAnim, remainingDist);
		}

		const Locator& myLocPs = pNavChar->GetLocatorPs();
		Locator initialApRefPs = myLocPs;
		Locator startAlignLs = Locator(kIdentity);

		if (EvaluateChannelInAnim(skelId, pStoppingAnim, SID("align"), startPhase, &startAlignLs))
		{
			const Locator invStartAlignLs = Inverse(startAlignLs);
			initialApRefPs = myLocPs.TransformLocator(invStartAlignLs);
		}

		initialApRef.SetLocatorPs(initialApRefPs);

		AI::SetPluggableAnim(pNavChar, pStoppingAnim->GetNameId());
	}
	else
	{
		AI::SetPluggableAnim(pNavChar, INVALID_STRING_ID_64);
	}

	m_stopAction.Request(pAnimControl, SID("stop"), AnimAction::kFinishOnNonTransitionalStateReached, &initialApRef, startPhase);

	m_readyToMoveAgain = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Stopping::UpdateStatus()
{
	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const BoundFrame& curFrame = pNavChar->GetBoundFrame();
	const Point myPosPs = curFrame.GetTranslationPs();
	const DC::AnimNpcInfo* pInfo = pAnimControl->Info<DC::AnimNpcInfo>();

	m_stopAction.Update(pAnimControl);

	const AnimStateInstance* pInstance = m_stopAction.GetTransitionDestInstance(pAnimControl);

	if (m_stopAction.WasTransitionTakenThisFrame() && pInstance && ((pInstance->GetStateFlags() & DC::kAnimStateFlagTransitional) != 0))
	{
		if (!pInstance || !pInstance->GetPhaseAnimArtItem().ToArtItem())
		{
			pSelf->GoStopped();
			return;
		}

		const PathWaypointsEx* pPathPs = pSelf->m_activePathInfo.GetPathWaypointsPs();

		const SkeletonId skelId = pNavChar->GetSkeletonId();
		const Point lastPointOnPathPs = pPathPs ? pPathPs->GetEndWaypoint() : myPosPs;
		const Vector finalDestFaceDirPs = GetLocalZ(pSelf->m_activeCommand.GetWaypointContract().m_rotPs);

		const bool allowExactStop = pPathPs && (pPathPs->GetWaypointCount() == 2);
		const bool debugDraw = DebugSelection::Get().IsProcessSelected(pNavChar) && g_navCharOptions.m_swimController.m_displayStopDetails;
		const Transform deltaXform = Transform(kIdentity);
		const float goalRadius = pSelf->m_activeCommand.GetGoalRadius();

		const BoundFrame selfBlendFrame = NavUtil::CalculateStopApRef(skelId,
																	  pInstance,
																	  curFrame,
																	  lastPointOnPathPs,
																	  finalDestFaceDirPs,
																	  deltaXform,
																	  goalRadius,
																	  allowExactStop,
																	  debugDraw);

		const DC::SelfBlendParams sb = NavUtil::ConstructSelfBlendParams(pInstance, 1.0f, pInfo->m_speedFactor);

		m_stopAction.SetSelfBlendParams(&sb, selfBlendFrame, pNavChar, 1.0f);
	}

	if (!m_stopAction.IsValid() || m_stopAction.IsDone())
	{
		if (ShouldTurn())
		{
			pSelf->GoTurning();
		}
		else
		{
			pSelf->OnCommandFinished();

			pSelf->m_activePathInfo.Reset();

			pSelf->m_desiredMoveDirPs = kZero;

			pSelf->GoStopped();
		}
	}

	if (!m_readyToMoveAgain && pInstance && (pInstance->GetStateFlags() & DC::kAnimStateFlagTransitional) && m_stopAction.IsSelfBlendComplete())
	{
		const ArtItemAnim* pStoppingAnim = pInstance->GetPhaseAnimArtItem().ToArtItem();
		const SkeletonId skelId = pNavChar->GetSkeletonId();
		Locator curStoppingAlignLs, finalStoppingAlignLs;
		const float phase = pInstance->Phase();
		if (EvaluateChannelInAnim(skelId, pStoppingAnim, SID("align"), phase, &curStoppingAlignLs)
			&& EvaluateChannelInAnim(skelId, pStoppingAnim, SID("align"), 1.0f, &finalStoppingAlignLs))
		{
			const float remDist = Dist(curStoppingAlignLs.Pos(), finalStoppingAlignLs.Pos());
			const float orientDot = Dot(GetLocalZ(curStoppingAlignLs.Rot()), GetLocalZ(finalStoppingAlignLs.Rot()));

			const bool distMatch = remDist < kSwimCommandDistTolerance;
			const bool orientMatch = orientDot > kSwimCommandDotTolerance;

			if (distMatch && orientMatch)
			{
				m_readyToMoveAgain = true;

				pSelf->OnCommandFinished();
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::Stopping::ShouldTurn() const
{
	const AiSwimController* pSelf = GetSelf();
	const NavCharacter* pNavChar = GetCharacter();

	const Vector curFacingPs = GetLocalZ(pNavChar->GetRotationPs());
	const Vector desFacingPs = GetLocalZ(pSelf->m_activeCommand.GetWaypointContract().m_rotPs);

	const float cosAngle = Dot(curFacingPs, desFacingPs);
	const float angleRad = SafeAcos(cosAngle);

	static const float maxAngleRad = DEGREES_TO_RADIANS(kMinTurnAngle);

	if (angleRad >= maxAngleRad)
	{
		return true;
	}

	return false;
}

FSM_STATE_REGISTER_ARG(AiSwimController, Stopping, kPriorityMedium, AiSwimController::StopParams);

/************************************************************************/
/* Turning                                                              */
/************************************************************************/

class AiSwimController::Turning : public AiSwimController::BaseState
{
	virtual void OnEnter() override;

	virtual void UpdateStatus() override;
	virtual bool IsBusy() const override { return true; }

	const ArtItemAnim* PickTurnAnimation() const;

	AnimActionWithSelfBlend m_turnAction;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Turning::OnEnter()
{
	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	BoundFrame initialApRef = pNavChar->GetBoundFrame();
	BoundFrame targetApRef = initialApRef;

	const ArtItemAnim* pTurnAnim = PickTurnAnimation();

	if (pTurnAnim)
	{
		Locator initialApRefWs = Locator(kIdentity);
		const SkeletonId skelId = pNavChar->GetSkeletonId();
		FindApReferenceFromAlign(skelId, pTurnAnim, pNavChar->GetLocator(), &initialApRefWs, 0.0f);

		initialApRef.SetLocatorWs(initialApRefWs);
	}

	targetApRef.SetRotationPs(pSelf->m_activeCommand.GetWaypointContract().m_rotPs);

	AI::SetPluggableAnim(pNavChar, pTurnAnim ? pTurnAnim->GetNameId() : INVALID_STRING_ID_64);

	m_turnAction.Request(pAnimControl, SID("turn"), AnimAction::kFinishOnNonTransitionalStateReached, &initialApRef);

	DC::SelfBlendParams manualParams;
	manualParams.m_curve = DC::kAnimCurveTypeEaseIn;
	manualParams.m_time = pTurnAnim ? GetDuration(pTurnAnim) : 1.0f;
	manualParams.m_phase = 0.0f;
	
	m_turnAction.SetSelfBlendParams(&manualParams, targetApRef, pNavChar, 1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::Turning::UpdateStatus()
{
	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	m_turnAction.Update(pAnimControl);

	if (m_turnAction.IsDone() || !m_turnAction.IsValid())
	{
		pSelf->OnCommandFinished();

		pSelf->GoStopped();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemAnim* AiSwimController::Turning::PickTurnAnimation() const
{
	const StringId64 baseAnimNames[] =
	{
		SID("swim-idle^turn-lt"),
		SID("swim-idle^turn-rt"),
	};

	const AiSwimController* pSelf = GetSelf();
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const SkeletonId skelId = pNavChar->GetSkeletonId();
	const ArtItemAnim* pBestAnim = nullptr;
	float bestMatchDot = -kLargeFloat;

	const Vector desFacingPs = GetLocalZ(pSelf->m_activeCommand.GetWaypointContract().m_rotPs);
	const Vector desFacingLs = pNavChar->GetLocatorPs().UntransformVector(desFacingPs);

	for (U32F iAnim = 0; iAnim < ARRAY_COUNT(baseAnimNames); ++iAnim)
	{
		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(baseAnimNames[iAnim]).ToArtItem();
		if (!pAnim)
			continue;

		Locator endLocLs;
		if (!EvaluateChannelInAnim(skelId, pAnim, SID("align"), 1.0f, &endLocLs))
			continue;

		const Vector animDirLs = GetLocalZ(endLocLs.Rot());
		const float rating = Dot(desFacingLs, animDirLs);

		if (rating > bestMatchDot)
		{
			bestMatchDot = rating;
			pBestAnim = pAnim;
		}
	}

	return pBestAnim;
}

FSM_STATE_REGISTER(AiSwimController, Turning, kPriorityMedium);

/************************************************************************/
/* Error                                                                */
/************************************************************************/

class AiSwimController::Error : public AiSwimController::BaseState
{
	virtual void OnEnter() override
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
		AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

		if (pBaseLayer)
		{
			FadeToStateParams params;
			params.m_animFadeTime = 0.2f;

			pBaseLayer->FadeToState(SID("s_idle-zen"), params);
		}
	}

	virtual bool CanProcessCommand() const override { return false; }
};

FSM_STATE_REGISTER(AiSwimController, Error, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
class AiSwimMotionInterface : public CharacterLocomotionInterface
{
	typedef CharacterLocomotionInterface ParentClass;
	SUBSYSTEM_BIND(NavCharacter);

public:
	virtual void Update(const Character* pChar, MotionModel& modelPs) override;

	virtual void GetInput(ICharacterLocomotion::InputData* pData) override;
	virtual const IMotionPose* GetPose(const MotionMatchingSet* pArtItemSet, bool debug) override;

	void ResetSpeedPercentHighWater() { m_speedPercentHighWater = 0.0f; }
	float GetSpeedPercentHighWater() const { return m_speedPercentHighWater; }

	void SetModelPosPs(Point_arg posPs) { m_modelPosPs = posPs; }
	Point GetModelPosPs() const { return m_modelPosPs; }

private:
	const IAiSwimController* GetSelf() const;
	IAiSwimController* GetSelf();

	static MotionModelPathInput Mm_PathFunc(const Character* pChar, const MotionMatchingSet* pArtItemSet, bool debug);

	Point m_modelPosPs;
	float m_speedPercentHighWater;
};

typedef TypedSubsystemHandle<AiSwimMotionInterface> AiSwimMotionIntHandle;

/// --------------------------------------------------------------------------------------------------------------- ///
TYPE_FACTORY_REGISTER(AiSwimMotionInterface, CharacterLocomotionInterface);

/// --------------------------------------------------------------------------------------------------------------- ///
class AiSwimController::MotionMatching : public AiSwimController::BaseState
{
public:
	virtual void OnEnter() override;
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual void PostRootLocatorUpdate() override;
	virtual bool IsBusy() const override { return false; }
	virtual void DebugDraw() const override;
	virtual bool CanProcessCommand() const override { return true; }
	virtual void OnStartNavigating(const SwimCommand& cmd) override {}

	virtual Point GetNavigationPosPs(Point_arg defPosPs) const override
	{
		if (const AiSwimMotionInterface* pInterface = m_hInterface.ToSubsystem())
		{
			return pInterface->GetModelPosPs();
		}

		return defPosPs; 
	}

	float GetStoppedSpeed(const AiSwimMotionInterface* pInterface) const;
	U32F CountMovingMmAnimStates(float stoppedSpeed, bool debugDraw) const;

	bool WasInScriptedAnimation() const;
	bool ShouldEarlyCompleteCommand(const PathInfo& pathInfo) const;
	void UpdatePath(const DC::MotionModelSettings& modelSettings);

	bool m_success;
	TPathWaypoints<16> m_pathPs;
	I32 m_pathId;
	CharacterMmLocomotionHandle m_hController;
	AiSwimMotionIntHandle m_hInterface;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::GoMotionMatching()
{
	static DC::BlendParams s_defBlend = { -1.0f, -1.0f, DC::kAnimCurveTypeInvalid };
	GoMotionMatching(s_defBlend);
}
FSM_STATE_REGISTER_ARG(AiSwimController, MotionMatching, kPriorityMedium, DC::BlendParams);

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::MotionMatching::OnEnter()
{
	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();

	NdSubsystemMgr* pSubsystemMgr = pNavChar->GetSubsystemMgr();

	NAV_ASSERT(pSubsystemMgr);
	if (!pSubsystemMgr)
	{
		pSelf->GoStopped();
		return;
	}

	const StringId64 desSetId = SID("*npc-m-swim-surface*");

	NdSubsystem* pMmSubsytem = pSubsystemMgr->FindSubsystem(SID("CharacterMotionMatchLocomotion"));
	CharacterMotionMatchLocomotion* pController = static_cast<CharacterMotionMatchLocomotion*>(pMmSubsytem);
	if (!pController || !pController->IsActiveController())
	{
		Err spawnRes = Err::kOK;
		CharacterMotionMatchLocomotion::SpawnInfo spawnInfo(SID("CharacterMotionMatchLocomotion"), pNavChar);
		spawnInfo.m_locomotionInterfaceType = SID("AiSwimMotionInterface");
		//spawnInfo.m_pHistorySeed = &pSelf->m_locomotionHistory;
		spawnInfo.m_pSpawnErrorOut = &spawnRes;

		if (WasInScriptedAnimation())
		{
			spawnInfo.m_disableExternalTransitions = true;

			if (const SsAnimateController* pScriptController = pNavChar->GetPrimarySsAnimateController())
			{
				const SsAnimateParams& igcParams = pScriptController->GetParams();
				spawnInfo.m_initialBlendTime = igcParams.m_fadeOutSec;
				spawnInfo.m_initialBlendCurve = igcParams.m_fadeOutCurve;
			}
		}

		const DC::BlendParams& blendIn = GetStateArg<DC::BlendParams>();
		if (blendIn.m_animFadeTime >= 0.0f)
		{
			spawnInfo.m_initialBlendTime = blendIn.m_animFadeTime;
			spawnInfo.m_initialBlendCurve = blendIn.m_curve;
		}

		pController = (CharacterMotionMatchLocomotion*)NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap,
																		   spawnInfo,
																		   FILE_LINE_FUNC);

		if (spawnRes == Err::kErrNotFound)
		{
			SwimLog("MotionMatching set '%s' was not found! going to error state\n",
					DevKitOnly_StringIdToString(desSetId));

			pSelf->GoError();
			return;
		}
	}
	else if (pController->TryChangeSetId(desSetId))
	{
		pController->ChangeInterface(SID("AiSwimMotionInterface"));
	}
	else
	{
		SwimLog("Failed to change MotionMatching set id to '%s'\n", DevKitOnly_StringIdToString(desSetId));

		pController->Kill();
		pController = nullptr;
	}

	m_hController = pController;

	if (pController)
	{
		m_success = true;
	}
	else
	{
		SwimLogStr("Failed to create MotionMatching controller, fading to stopped\n");
		pSelf->GoStopped();
		m_success = false;
	}

	m_pathId = pSelf->m_nextCommandId;

	if (m_success)
	{
		const DC::MotionModelSettings modelSettings = pController->GetMotionSettings();

		UpdatePath(modelSettings);

		if (AiSwimMotionInterface* pInterface = pController->GetInterface<AiSwimMotionInterface>(SID("AiSwimMotionInterface")))
		{
			m_hInterface = pInterface;

			pInterface->SetModelPosPs(pNavChar->GetTranslationPs());
			pInterface->ResetSpeedPercentHighWater();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::MotionMatching::ShouldEarlyCompleteCommand(const PathInfo& pathInfo) const
{
	if (!pathInfo.IsValid())
	{
		return false;
	}

	const AiSwimController* pSelf = GetSelf();

	if (pSelf->m_activeCommand.InProgress() || (pSelf->m_activeCommand.GetType() == SwimCommand::Type::kStopSwimming))
	{
		return false;
	}

	const WaypointContract& waypointContract = pSelf->m_activeCommand.GetWaypointContract();

	if (waypointContract.m_reachedType != kNavGoalReachedTypeContinue)
	{
		return false;
	}

	const IPathWaypoints* pPathPs = pathInfo.GetPathWaypointsPs();
	const I32F numWaypoints = pPathPs ? pPathPs->GetWaypointCount() : -1;
	if (numWaypoints != 2)
	{
		return false;
	}

	const float pathLen = pPathPs->ComputePathLengthXz();
	if (pathLen > pSelf->m_activeCommand.GetGoalRadius())
	{
		return false;
	}

	const Vector curMoveDirPs = pSelf->m_desiredMoveDirPs;
	const Vector desMoveDirPs = SafeNormalize(pPathPs->GetWaypoint(1) - pPathPs->GetWaypoint(0),
											  curMoveDirPs);

	const float dotP = Abs(DotXz(curMoveDirPs, desMoveDirPs));

	if (dotP < 0.75f)
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::MotionMatching::UpdatePath(const DC::MotionModelSettings& modelSettings)
{
	AiSwimController* pSelf = GetSelf();
	const NavCharacter* pNavChar = GetCharacter();

	if (const PathWaypointsEx* pLastFoundPathPs = pNavChar->GetLastFoundPathPs())
	{
		const Point myPosPs = pNavChar->GetTranslationPs();

		I32F iPrevLeg = -1;
		Vector prevDirPs = kZero;
		if (m_pathPs.IsValid())
		{
			m_pathPs.ClosestPointXz(myPosPs, nullptr, &iPrevLeg);
			prevDirPs = AsUnitVectorXz(m_pathPs.GetWaypoint(iPrevLeg + 1) - m_pathPs.GetWaypoint(iPrevLeg), kZero);
		}

		I32F iLeg = -1;
		pLastFoundPathPs->ClosestPointXz(myPosPs, nullptr, &iLeg);

		const Point leg0 = pLastFoundPathPs->GetWaypoint(iLeg);
		const Point leg1 = pLastFoundPathPs->GetWaypoint(iLeg + 1);

		const Vector newDirPs = AsUnitVectorXz(leg1 - leg0, kZero);

		if ((pLastFoundPathPs->ComputePathLengthXz() > 0.5f) || (Abs(LengthSqr(pSelf->m_desiredMoveDirPs) - 1.0f) > 0.1f))
		{
			pSelf->m_desiredMoveDirPs = newDirPs;
		}

		const Scalar dotP = Dot(prevDirPs, newDirPs);
		const float angleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(dotP));

		if (angleDiffDeg > modelSettings.m_newPathAngleDeg)
		{
			++m_pathId;

			m_pathPs.CopyFrom(pLastFoundPathPs);
		}
		else
		{
			m_pathPs.CopyFrom(pLastFoundPathPs);
		}
	}
	else
	{
		m_pathPs.Clear();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::MotionMatching::RequestAnimations()
{
	if (!m_success)
		return;

	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();

	CharacterMotionMatchLocomotion* pController = m_hController.ToSubsystem();
	const bool isActive = pController && pController->IsActiveController();
	if (!pController || pController->IsExiting() || !isActive)
	{
		SwimLog("MotionMatching controller disappeared (%s), failing command\n",
				!pController ? "nullptr" : (pController->IsExiting() ? "controller exiting" : "not active controller"));

		pSelf->m_activeCommand.SetFailed();
		pSelf->GoStopped();
		return;
	}

	if (pSelf->HasNewState())
	{
		return;
	}

	if (pSelf->m_pendingPathInfo.IsValid())
	{
		pSelf->UpdateActivePathInfo(pSelf->m_pendingPathInfo);
		pSelf->m_pendingPathInfo.Reset();
	}

	const DC::MotionModelSettings modelSettings = pController->GetMotionSettings();

	UpdatePath(modelSettings);

	if (pSelf->IsCommandInProgress())
	{
		if (ShouldEarlyCompleteCommand(pSelf->m_activePathInfo))
		{
			SwimLog("Ignoring short path update (%0.2fm) and completing command\n", pSelf->m_activePathInfo.ComputePathLengthXz());
			pSelf->m_activeCommand.SetSucceeded();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSwimController::MotionMatching::WasInScriptedAnimation() const
{
	const StringId64 animStateId = GetAnimStateId();
	return IsKnownScriptedAnimationState(animStateId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct CountData
{
	SkeletonId m_skelId = INVALID_SKELETON_ID;;
	float m_speedScale = 1.0f;
	float m_stoppedSpeed = -1.0f;
	U32 m_count = 0;
	const AnimStateInstance* m_pTopMmInstance = nullptr;
	bool m_debugDraw = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsMmInstanceStopped(const SkeletonId skelId,
								const AnimStateInstance* pInstance,
								float speedScale,
								float stoppedSpeed,
								bool debugDraw)
{
	if (stoppedSpeed < 0.0f)
	{
		return false;
	}

	const ArtItemAnim* pAnim = pInstance ? pInstance->GetPhaseAnimArtItem().ToArtItem() : nullptr;

	if (!pAnim)
	{
		return true;
	}

	const float curPhase = pInstance->Phase();

	const float phase0 = Limit01(curPhase - pAnim->m_pClipData->m_phasePerFrame);
	const float phase1 = Limit01(curPhase + pAnim->m_pClipData->m_phasePerFrame);

	const float animDuration = GetDuration(pAnim);
	const float dt = (phase1 - phase0) * animDuration;

	if (dt < NDI_FLT_EPSILON)
	{
		return true;
	}

	Locator loc0, loc1;
	if (!EvaluateChannelInAnim(skelId, pAnim, SID("align"), phase0, &loc0)
		|| !EvaluateChannelInAnim(skelId, pAnim, SID("align"), phase1, &loc1))
	{
		return true;
	}

	const float dp = Dist(loc0.Pos(), loc1.Pos());

	const float speed = (dp / dt) * speedScale;

	const float dotP = Dot(GetLocalZ(loc0), GetLocalZ(loc1));
	const float angleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(dotP));
	const float angleSpeedDps = (angleDiffDeg / dt) * speedScale;

	const bool transStopped = (speed <= stoppedSpeed);
	const bool rotStopped = (angleSpeedDps <= g_navCharOptions.m_locomotionController.m_mmStoppedAngleSpeedDps);

	const bool stopped = transStopped && rotStopped;

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		MsgCon("  %-40s %s%0.3fm/s%s [rot: %s%0.1f deg/s%s]\n",
			   pAnim->GetName(),
			   GetTextColorString(transStopped ? kTextColorYellow : kTextColorRed),
			   speed,
			   GetTextColorString(kTextColorNormal),
			   GetTextColorString(rotStopped ? kTextColorYellow : kTextColorRed),
			   angleSpeedDps,
			   GetTextColorString(kTextColorNormal));
	}

	return stopped;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CountMovingMmAnimStatesCb(const AnimStateInstance* pInstance,
									  const AnimStateLayer* pStateLayer,
									  uintptr_t userData)
{
	if (!pInstance || (0 == userData))
		return true;

	CountData& data = *(CountData*)userData;

	data.m_pTopMmInstance = pInstance;

	switch (pInstance->GetStateName().GetValue())
	{
	case SID_VAL("s_motion-match-locomotion"):
		if (!IsMmInstanceStopped(data.m_skelId, pInstance, data.m_speedScale, data.m_stoppedSpeed, data.m_debugDraw))
		{
			++data.m_count;
		}
		break;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiSwimController::MotionMatching::GetStoppedSpeed(const AiSwimMotionInterface* pInterface) const
{
	const AiSwimController* pSelf = GetSelf();

	const float speedPercentHw = pInterface ? pInterface->GetSpeedPercentHighWater() : 0.0f;

	float stoppedSpeed = g_navCharOptions.m_swimController.m_mmStoppedSpeed;

	if (speedPercentHw >= 0.0f)
	{
		stoppedSpeed *= speedPercentHw;
	}

	stoppedSpeed = Max(stoppedSpeed, 0.3f);

	return stoppedSpeed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AiSwimController::MotionMatching::CountMovingMmAnimStates(float stoppedSpeed, bool debugDraw) const
{
	const AiSwimController* pSelf = GetSelf();
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const DC::AnimNpcInfo* pActorInfo = pAnimControl ? pAnimControl->Info<DC::AnimNpcInfo>() : nullptr;

	CountData data;
	data.m_skelId = pNavChar->GetSkeletonId();
	data.m_stoppedSpeed = stoppedSpeed;
	data.m_debugDraw = FALSE_IN_FINAL_BUILD(debugDraw);

	if (pActorInfo)
	{
		data.m_speedScale = pActorInfo->m_compositeMovementSpeedScale;
	}

	pBaseLayer->WalkInstancesOldToNew(CountMovingMmAnimStatesCb, (uintptr_t)&data);

	return data.m_count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::MotionMatching::UpdateStatus()
{
	if (!m_success)
		return;

	AiSwimController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	NdSubsystemMgr* pSubsystemMgr = pNavChar->GetSubsystemMgr();

	const SkeletonId skelId = pNavChar->GetSkeletonId();
	const AnimStateInstance* pTopMmInstance = nullptr;

	const CharacterMotionMatchLocomotion* pController = m_hController.ToSubsystem();
	if (!pController || !pController->IsActiveController())
	{
		return;
	}

	const AiSwimMotionInterface* pInterface = pController->GetInterface<AiSwimMotionInterface>(SID("AiSwimMotionInterface"));

	const float stoppedSpeed = GetStoppedSpeed(pInterface);

	const float padDist = pController->GetMotionModelPs().GetProceduralClampDist();

	const bool atEndOfPath = pSelf->IsAtEndOfWaypointPath(padDist);
	const bool noMorePath = !pSelf->m_activePathInfo.IsValid() || atEndOfPath;
	const bool animIsStopped = noMorePath && (CountMovingMmAnimStates(stoppedSpeed, false) == 0);
	const bool animIsMoving = !animIsStopped;

	if (pSelf->IsCommandInProgress())
	{
		const Quat deltaRotPs = Conjugate(pNavChar->GetPrevLocatorPs().Rot()) * pNavChar->GetLocatorPs().Rot();
		const float rotDt = pNavChar->GetClock()->GetLastFrameDeltaTimeInSeconds();

		Vec4 rotAxisPs;
		float rotAngleRad = 0.0f;
		deltaRotPs.GetAxisAndAngle(rotAxisPs, rotAngleRad);

		const float rotSpeedDps = RADIANS_TO_DEGREES(rotAngleRad) / (rotDt < NDI_FLT_EPSILON ? 1.0f : rotDt);
		const bool notTurning = rotSpeedDps < 30.0f;

		const bool isMoveCommand = (pSelf->m_activeCommand.GetType() == SwimCommand::Type::kStartSwimming);
		const bool positionSatisfied = !isMoveCommand || atEndOfPath;
		const bool orientationSatisfied = pSelf->IsDesiredOrientationSatisfied();
		const bool notTurningCheck = notTurning && animIsStopped;

		const WaypointContract& wc = pSelf->m_activeCommand.GetWaypointContract();
		const bool reachedTypeStop = wc.m_reachedType == kNavGoalReachedTypeStop;

		// hacky check to see if motion matching has stopped
		if (positionSatisfied && orientationSatisfied && notTurningCheck && !animIsMoving)
		{
			pSelf->m_activeCommand.SetSucceeded();

			if (!animIsMoving && reachedTypeStop)
			{
				SwimLogStr("SUCCEEDED - MotionMatching reached goal\n");
			}
			else if (!reachedTypeStop)
			{
				SwimLogStr("SUCCEEDED - MotionMatching reached type continue\n");
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::MotionMatching::PostRootLocatorUpdate()
{
	AiSwimController* pSelf = GetSelf();

	if (pSelf->m_activeCommand.InProgress()
		&& (pSelf->m_activeCommand.GetWaypointContract().m_reachedType == kNavGoalReachedTypeContinue))
	{
		const PathWaypointsEx* pPathPs = pSelf->m_activePathInfo.GetPathWaypointsPs();

		const Point posPs	  = pSelf->GetNavigationPosPs();
		const Point lastPosPs = pSelf->m_lastNavigationPosPs;
		const Point pathEndPs = pPathPs ? pPathPs->GetEndWaypoint() : posPs;
		const float kEpsilon  = 0.05f;
		const Vector toEndPs  = pathEndPs - posPs;

		const Vector prevToGoalPs = pathEndPs - lastPosPs;
		const Vector newToGoalPs  = pathEndPs - posPs;
		const float dotP1		  = DotXz(prevToGoalPs, toEndPs);
		const float dotP2		  = DotXz(newToGoalPs, toEndPs);
		const bool passedThisFrame = (dotP1 > 0.0f) && (dotP2 < kEpsilon);
		const bool alreadyPassed   = (dotP1 < 0.0f) && (dotP2 < kEpsilon)
								   && (DistXz(posPs, pathEndPs) < kMinStoppingDistance);
		const bool passedOurDestPoint = (passedThisFrame || alreadyPassed);

		if (passedOurDestPoint)
		{
			pSelf->m_activeCommand.SetSucceeded();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimController::MotionMatching::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (g_navCharOptions.m_swimController.m_displayDetails)
	{
		const CharacterMotionMatchLocomotion* pController = m_hController.ToSubsystem();
		if (!pController || !pController->IsActiveController())
		{
			return;
		}

		const AiSwimMotionInterface* pInterface = pController->GetInterface<AiSwimMotionInterface>(SID("AiSwimMotionInterface"));
		const float speedPercentHw = pInterface ? pInterface->GetSpeedPercentHighWater() : 0.0f;
		const float stoppedSpeed = GetStoppedSpeed(pInterface);
		const U32F numMovingInstances = CountMovingMmAnimStates(stoppedSpeed, false);

		MsgCon("MM Stopped Speed       %0.3fm/s (speed hw: %3.1f%%) [%s%d moving states%s]\n",
			   stoppedSpeed,
			   speedPercentHw * 100.0f,
			   GetTextColorString((numMovingInstances == 0) ? kTextColorYellow : kTextColorNormal),
			   numMovingInstances,
			   GetTextColorString(kTextColorNormal));

		CountMovingMmAnimStates(stoppedSpeed, true);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiSwimController* CreateAiSwimController() 
{
	return NDI_NEW AiSwimController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimMotionInterface::Update(const Character* pChar, MotionModel& modelPs)
{
	ParentClass::Update(pChar, modelPs);

	const float curSpeed = modelPs.GetSprungSpeed();
	const float maxSpeed = modelPs.GetMaxSpeed();

	const float speedPercent = (maxSpeed > NDI_FLT_EPSILON) ? Limit01(curSpeed / maxSpeed) : 0.0f;

	m_speedPercentHighWater = Max(speedPercent, m_speedPercentHighWater);
	m_modelPosPs = modelPs.GetPos();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSwimMotionInterface::GetInput(ICharacterLocomotion::InputData* pData)
{
	if (!pData)
		return;

	NavCharacter* pNavChar = GetOwnerNavCharacter();

	AiSwimController* pSelf = (AiSwimController*)GetSelf();
	if (!pSelf)
		return;

	pData->m_groundNormalPs	 = kUnitYAxis;
	pData->m_desiredFacingPs = pSelf->GetChosenFaceDirPs();

	if (const DC::NpcDemeanorDef* pDcDemDef = pNavChar->GetCurrentDemeanorDef())
	{
		const NavControl* pNavControl = pNavChar->GetNavControl();
		const bool diving = pNavControl && pNavControl->NavMeshForcesDive();

		if (diving && pDcDemDef->m_diveMmSet)
		{
			pData->m_setId = pDcDemDef->m_diveMmSet;
		}
		else
		{
			pData->m_setId = pDcDemDef->m_swimMmSet;
		}
	}
	else
	{
		pData->m_setId = SID("*npc-m-swim-surface*");
	}

	if (pSelf->m_activeCommand.InProgress())
	{
		pData->m_desiredVelocityDirPs = pSelf->m_desiredMoveDirPs;
		pData->m_pPathFunc = Mm_PathFunc;
	}
	else
	{
		pData->m_desiredVelocityDirPs = kZero;
	}

	const AnimControl* pAnimControl	  = pNavChar->GetAnimControl();
	const DC::AnimNpcInfo* pActorInfo = pAnimControl ? pAnimControl->Info<DC::AnimNpcInfo>() : nullptr;
	if (pActorInfo)
	{
		pData->m_speedScale = pActorInfo->m_compositeMovementSpeedScale;
	}

	pData->m_transitionsId = SID("*npc-mm-set-transitions*");
}

/// --------------------------------------------------------------------------------------------------------------- ///
const IAiSwimController* AiSwimMotionInterface::GetSelf() const
{
	const NavCharacter* pNavChar = GetOwnerNavCharacter();
	const AnimationControllers* pAnimControllers = pNavChar ? pNavChar->GetAnimationControllers() : nullptr;
	const IAiSwimController* pSwimController = pAnimControllers ? pAnimControllers->GetSwimController() : nullptr;

	return pSwimController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiSwimController* AiSwimMotionInterface::GetSelf()
{
	NavCharacter* pNavChar = GetOwnerNavCharacter();
	AnimationControllers* pAnimControllers = pNavChar ? pNavChar->GetAnimationControllers() : nullptr;
	IAiSwimController* pSwimController = pAnimControllers ? pAnimControllers->GetSwimController() : nullptr;

	return pSwimController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const IMotionPose* AiSwimMotionInterface::GetPose(const MotionMatchingSet* pArtItemSet, bool debug)
{
	const NavCharacter* pNavChar = GetOwnerNavCharacter();

	const AiSwimController* pSelf = (AiSwimController*)GetSelf();
	if (!pSelf)
		return nullptr;

	const IMotionPose* pPose = nullptr;

	if (const PoseTracker* pPoseTracker = pNavChar->GetPoseTracker())
	{
		pPose = &pPoseTracker->GetPose();
	}

	return pPose;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetStoppingFaceDist(const AiSwimController* pSelf,
								 const MotionMatchingSet* pArtItemSet,
								 float minGoalRadius)
{
	const MMSettings* pSettings = pArtItemSet ? pArtItemSet->m_pSettings : nullptr;
	const float facingStoppingDist = pSettings ? pSettings->m_goals.m_stoppingFaceDist : -1.0f;

	if (facingStoppingDist > 0.0f)
	{
		return facingStoppingDist;
	}

	return Max(pSelf->m_activeCommand.GetGoalRadius(), minGoalRadius);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
MotionModelPathInput AiSwimMotionInterface::Mm_PathFunc(const Character* pChar,
														const MotionMatchingSet* pArtItemSet,
														bool debug)
{
	MotionModelPathInput ret;

	const NavCharacter* pNavChar = NavCharacter::FromProcess(pChar);
	if (!pNavChar)
		return ret;

	const AiSwimController* pSelf = (AiSwimController*)pNavChar->GetAnimationControllers()->GetSwimController();
	if (!pSelf)
		return ret;

	if (!pSelf->m_activeCommand.InProgress())
		return ret;

	if (pSelf->m_activeCommand.GetType() == SwimCommand::Type::kStopSwimming)
		return ret;

	if (!pSelf->IsState(AiSwimController::kStateMotionMatching))
		return ret;

	const AiSwimController::BaseState* pState = pSelf->GetState();
	if (!pState)
		return ret;

	const AiSwimController::MotionMatching* pMmState = (AiSwimController::MotionMatching*)pState;

	const IPathWaypoints* pPathPs = pMmState->m_pathPs.IsValid() ? &pMmState->m_pathPs : nullptr;

	if (!pPathPs)
		return ret;

	const WaypointContract wc = pSelf->m_activeCommand.GetWaypointContract();

	const float pathLen = pPathPs->ComputePathLengthXz();
	const float minGoalRadius = pNavChar->GetMotionConfig().m_minimumGoalRadius;
	const bool reachedTypeContinue = wc.m_reachedType == kNavGoalReachedTypeContinue;

	if ((pathLen < minGoalRadius) && reachedTypeContinue)
	{
		return ret;
	}

	ret.m_pathPs.CopyFrom(&pMmState->m_pathPs);
	ret.m_goalRotPs	 = wc.m_rotPs;
	ret.m_stopAtGoal = wc.m_reachedType == kNavGoalReachedTypeStop;
	ret.m_pathId	 = pMmState->m_pathId;
	ret.m_goalRadius = Max(pSelf->m_activeCommand.GetGoalRadius() * 0.9f, 0.3f);
	ret.m_stoppingFaceDist = GetStoppingFaceDist(pSelf, pArtItemSet, minGoalRadius);

	if (pSelf->IsCommandInProgress())
	{
		const PathWaypointsEx* pLivePathPs = pSelf->m_activePathInfo.GetPathWaypointsPs();
		if (pLivePathPs)
		{
			const Point lastPointOnPathPs = pLivePathPs->GetEndWaypoint();
			const Point curPathEndPs = ret.m_pathPs.GetEndWaypoint();
			ret.m_pathEndsAtGoal = (DistXz(curPathEndPs, lastPointOnPathPs) < ret.m_goalRadius)
				&& (Abs(curPathEndPs.Y() - lastPointOnPathPs.Y()) < 1.0f);
		}
		else
		{
			ret.m_pathEndsAtGoal = false;
		}
	}
	else
	{
		ret.m_pathEndsAtGoal = true;
	}

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		pPathPs->DebugDraw(pNavChar->GetParentSpace(), true);
	}

	return ret;
}
