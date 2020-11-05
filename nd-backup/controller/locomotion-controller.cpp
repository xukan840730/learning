/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/locomotion-controller.h"

#include "ndlib/anim/anim-align-cache.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/util/bitarray128.h"
#include "ndlib/util/double-frame-ptr.h"
#include "ndlib/util/finite-state-machine.h"

#include "gamelib/anim/motion-matching/motion-matching-debug.h"
#include "gamelib/anim/motion-matching/motion-matching-manager.h"
#include "gamelib/anim/motion-matching/motion-matching-set.h"
#include "gamelib/anim/motion-matching/pose-tracker.h"
#include "gamelib/gameplay/character-leg-raycaster.h"
#include "gamelib/debug/ai-msg-log.h"
#include "gamelib/gameplay/ai/agent/nav-character-util.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/character-leg-raycaster.h"
#include "gamelib/gameplay/character-motion-match-locomotion.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-poly-edge-gatherer.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/ndphys/havok-collision-cast.h"
#include "gamelib/scriptx/h/npc-demeanor-defines.h"

#include "ailib/nav/nav-ai-util.h"
#include "ailib/util/ai-lib-util.h"

#include "game/ai/agent/npc.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/demeanor-helper.h"
#include "game/ai/characters/horse.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/hit-reactions-defines.h"
#include "game/vehicle/horse-move-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
float kLocomotionProcRotFilter = 3.0f;

const float kInertiaSpeedScaleSpringK	   = 5.0f;
const float kInertiaSpeedScaleSpringKAccel = 7.0f;
const U32F kMovePerformanceCacheSize = 32;

/// --------------------------------------------------------------------------------------------------------------- ///
#if FINAL_BUILD
#define LocoLogStr(str)
#define LocoLog(str, ...)
#define LocoLogStrDetails(str)
#define LocoLogDetails(str, ...)
#else
#define LocoLogStr(str)                                                                                                \
	AiLogAnim(GetCharacter(),                                                                                          \
			  "[loco-%d] [%s : %s] " str,                                                                              \
			  GetCommandId(),                                                                                          \
			  GetStateName("<none>"),                                                                                  \
			  DevKitOnly_StringIdToString(GetAnimStateId()))
#define LocoLog(str, ...)                                                                                              \
	AiLogAnim(GetCharacter(),                                                                                          \
			  "[loco-%d] [%s : %s] " str,                                                                              \
			  GetCommandId(),                                                                                          \
			  GetStateName("<none>"),                                                                                  \
			  DevKitOnly_StringIdToString(GetAnimStateId()),                                                           \
			  __VA_ARGS__)
#define LocoLogStrDetails(str)                                                                                         \
	AiLogAnimDetails(GetCharacter(),                                                                                   \
					 "[loco-%d] [%s : %s] " str,                                                                       \
					 GetCommandId(),                                                                                   \
					 GetStateName("<none>"),                                                                           \
					 DevKitOnly_StringIdToString(GetAnimStateId()))
#define LocoLogDetails(str, ...)                                                                                       \
	AiLogAnimDetails(GetCharacter(),                                                                                   \
					 "[loco-%d] [%s : %s] " str,                                                                       \
					 GetCommandId(),                                                                                   \
					 GetStateName("<none>"),                                                                           \
					 DevKitOnly_StringIdToString(GetAnimStateId()),                                                    \
					 __VA_ARGS__)

#endif

/// --------------------------------------------------------------------------------------------------------------- ///
inline float GetXZAngleForVecDeg(Vector_arg v)
{
	return RADIANS_TO_DEGREES(Atan2(v.X(), v.Z()));
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline Vector GetVectorForXZAngleDeg(const float angleDeg)
{
	const float angleRad = DEGREES_TO_RADIANS(angleDeg);
	const Vector v		 = Vector(Sin(angleRad), 0.0f, Cos(angleRad));
	return v;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DebugPrintInstanceAngle(const AnimStateInstance* pInstance,
									const AnimStateLayer* pStateLayer,
									uintptr_t userData)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	const DC::AnimNpcTopInfo* pTopInfo = (const DC::AnimNpcTopInfo*)pInstance->GetAnimTopInfo();
	MsgCon("[%s] %f\n", DevKitOnly_StringIdToString(pInstance->GetStateName()), pTopInfo->m_locomotionAngleDeg);
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float PhaseAdd(float phase, float delta)
{
	float ret = phase + delta;

	while (ret < 0.0f)
		ret += 1.0f;

	ret = Fmod(ret);

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float PhaseDiff(float src, float dst)
{
	ANIM_ASSERTF(src >= 0.0f && src <= 1.0f, ("Bad src: %f", src));
	ANIM_ASSERTF(dst >= 0.0f && dst <= 1.0f, ("Bad dst: %f", dst));

	const float diff = dst - src;
	const float revDiff = (1.0f - Abs(diff)) * -1.0f * Sign(diff);

	//ANIM_ASSERT(Abs(PhaseAdd(src, diff) - dst) < 0.001f);
	//ANIM_ASSERT(Abs(PhaseAdd(src, revDiff) - dst) < 0.001f);

	const float shortestDiff = (Abs(diff) > 0.5f) ? revDiff : diff;
	//ANIM_ASSERT(Abs(shortestDiff) <= 1.0f);

	if (false)
	{
		const float predicted = PhaseAdd(src, shortestDiff);
		ANIM_ASSERT(Abs(predicted - dst) < 0.001f);
	}

	return shortestDiff;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// LocoCommand
/// --------------------------------------------------------------------------------------------------------------- ///
class LocoCommand
{
public:
	enum Status
	{
		kStatusAwaitingCommands,	// idle essentially (no goal, no path, not navigating)
		kStatusCommandPending,		// nav command pending (received but not processed)
		kStatusCommandInProgress,	// nav command in progress
		kStatusCommandSucceeded,	// nav command succeeded
		kStatusCommandFailed,		// nav command failed (we cannot move)
	};

	static const char* GetStatusName(Status i)
	{
		switch (i)
		{
		case kStatusAwaitingCommands:	return "Awaiting Commands";
		case kStatusCommandPending:		return "Command Pending";
		case kStatusCommandInProgress:	return "Command In Progress";
		case kStatusCommandSucceeded:	return "Command Succeeded";
		case kStatusCommandFailed:		return "Command Failed";
		}

		return "<unknown status>";
	}

	enum Type
	{
		kCmdInvalid,
		kCmdStopLocomoting,
		kCmdStartLocomoting,
	};

	static const char* GetTypeName(Type i)
	{
		switch (i)
		{
		case kCmdInvalid:			return "Invalid";
		case kCmdStopLocomoting:	return "StopLocomoting";
		case kCmdStartLocomoting:	return "StartLocomoting";
		}

		return "<unknown>";
	}

	LocoCommand()
	{
		Reset();
	}

	void Reset()
	{
		m_type = kCmdInvalid;
		m_waypointContract = WaypointContract();
		m_moveDirPs		   = kZero;
		m_moveArgs		   = NavMoveArgs();
		m_followingPath	   = false;
	}

	// Accessors
	bool					IsValid() const						{ return m_type != kCmdInvalid; }
	Type					GetType() const						{ return m_type; }
	const char*				GetTypeName() const					{ return GetTypeName(GetType()); }
	const WaypointContract&	GetWaypointContract() const			{ return m_waypointContract; }
	MotionType				GetMotionType() const				{ return m_moveArgs.m_motionType; }
	StringId64				GetMotionTypeSubcategory() const	{ return m_moveArgs.m_mtSubcategory; }
	DC::NpcStrafe			GetStrafeMode() const				{ return m_moveArgs.m_strafeMode; }
	float					GetGoalRadius() const				{ return m_moveArgs.m_goalRadius; }
	Vector					GetMoveDirPs() const				{ return m_moveDirPs; }
	bool					IsGoalFaceValid() const				{ return m_moveArgs.m_goalFaceDirValid; }
	NavGoalReachedType		GetGoalReachedType() const			{ return m_moveArgs.m_goalReachedType; }
	bool					IsFollowingPath() const				{ return m_followingPath; }

	const NavMoveArgs&		GetMoveArgs() const { return m_moveArgs; }

	// Configuration functions
	void AsStopLocomoting(const NavCharacter* pNavChar, const NavAnimStopParams& params)
	{
		Reset();
		m_type = kCmdStopLocomoting;
		m_waypointContract		= params.m_waypoint;
		m_moveArgs.m_goalRadius = Max(params.m_goalRadius, pNavChar->GetMotionConfig().m_minimumGoalRadius);
		m_moveArgs.m_goalFaceDirValid = params.m_faceValid;
		m_moveArgs.m_goalFaceDirPs = GetLocalZ(params.m_waypoint.m_rotPs);
	}

	void AsStartLocomoting(const NavCharacter* pNavChar, const NavAnimStartParams& params)
	{
		Reset();

		m_type = kCmdStartLocomoting;

		m_waypointContract = params.m_waypoint;
		m_moveDirPs		   = params.m_moveDirPs;
		m_moveArgs		   = params.m_moveArgs;
		m_followingPath	   = params.m_pPathPs != nullptr;
	}

	void Update(const WaypointContract& waypointContract)
	{
		m_waypointContract = waypointContract;
	}

	bool CanSharePathInfoWith(const LocoCommand& otherCmd) const
	{
		if ((m_type != otherCmd.m_type) || (m_type != kCmdStartLocomoting))
			return false;

		if (m_moveArgs.m_strafeMode != otherCmd.m_moveArgs.m_strafeMode)
			return false;

		if (m_waypointContract.m_reachedType != otherCmd.m_waypointContract.m_reachedType)
			return false;

		return true;
	}

	mutable bool m_reachedTypeForced; // for debug logging purposes only

private:
	Type				m_type;
	WaypointContract	m_waypointContract;
	Vector				m_moveDirPs;

	NavMoveArgs			m_moveArgs;
	bool				m_followingPath;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionState
{
public:
	MotionState()
	{
		Reset();
	}

	void Reset()
	{
		m_motionType = kMotionTypeMax;
		m_moving	 = false;
		m_strafing	 = false;
	}

	bool Equals(const MotionState& rhs, bool testMoving = false) const
	{
		if (m_motionType != rhs.m_motionType)
		{
			return false;
		}

		if (m_strafing != rhs.m_strafing)
		{
			return false;
		}

		if (testMoving && (m_moving != rhs.m_moving))
		{
			return false;
		}

		return true;
	}

	MotionType	m_motionType;
	bool		m_moving;
	bool		m_strafing;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AiLocomotionController : public IAiLocomotionController
{
public:
	typedef IAiLocomotionController ParentClass;

	class BaseState : public Fsm::TState<AiLocomotionController>
	{
	public:
		virtual void OnEnter() override;
		virtual void RequestAnimations() {}
		virtual void UpdateStatus() {}
		virtual void OnStartLocomoting(const LocoCommand& cmd) {}
		virtual void OnStopLocomoting(const LocoCommand& cmd) {}
		virtual void OnInterrupted();
		virtual bool IsBusy() const { return true; }
		virtual void OnConfigureCharacter(Demeanor demeanor) {}
		virtual float GetMaxMovementSpeed() const { return -1.0f; }

		virtual Vector  GetChosenFaceDirPs() const;

		virtual void DebugDraw() const {}

		virtual float GetWeaponChangeFade() const
		{
			return 1.0f;
		}

		virtual bool CanProcessCommand() const { return true; }

		virtual bool ShouldDoProceduralApRef(const AnimStateInstance* pInstance) const { return false; }
		virtual bool ShouldDoProceduralAlign(const AnimStateInstance* pInstance) const { return true; }

		virtual Point GetNavigationPosPs(Point_arg defPosPs) const { return defPosPs; }

		NavCharacter* GetCharacter() { return GetSelf()->GetCharacter(); }
		const NavCharacter* GetCharacter() const { return GetSelf()->GetCharacter(); }

		const NavControl* GetNavControl() const { return GetSelf()->GetNavControl(); }

		U32 GetCommandId() const { return GetSelf()->GetCommandId(); }
		StringId64 GetAnimStateId() const { return GetSelf()->GetAnimStateId(); }
		const char* GetStateName(const char* nameIfNoState) const { return GetSelf()->GetStateName(nameIfNoState); }
	};

	class LocomotionControllerFsm : public Fsm::TStateMachine<BaseState>
	{
		virtual bool ShouldChangeStatePriorityTest(Fsm::StateDescriptor::Priority current,
												   Fsm::StateDescriptor::Priority desired) const override
		{
			return desired <= current;
		}

		const NavCharacter* GetCharacter() const { return GetSelf<const AiLocomotionController>()->GetCharacter(); }
		U32 GetCommandId() const { return GetSelf<const AiLocomotionController>()->GetCommandId(); }
		StringId64 GetAnimStateId() const { return GetSelf<const AiLocomotionController>()->GetAnimStateId(); }

		virtual void OnNewStateRequested(const Fsm::StateDescriptor& desc,
										 const void* pStateArg,
										 size_t argSize,
										 const char* srcFile,
										 U32 srcLine,
										 const char* srcFunc) const override
		{
			LocoLog("New state requested: %s\n", desc.GetStateName());
		}
	};

protected:
	FSM_BASE_DECLARE(LocomotionControllerFsm);
public:

	struct IdlePerfArgs
	{
		ArtItemAnimHandle m_hAnim;
		Maybe<SsAction> m_ssAction;
		DC::AnimCurveType m_animCurveType = DC::kAnimCurveTypeInvalid;
		float m_fadeTime = 0.4f;
		bool m_loop = false;
		bool m_phaseSync = false;
		bool m_mirror = false;
	};

	FSM_STATE_DECLARE(Stopped);
	FSM_STATE_DECLARE_ARG(ChangingDemeanor, StringId64);
	FSM_STATE_DECLARE_ARG(ChangingWeaponUpDown, StringId64);
	FSM_STATE_DECLARE_ARG(MotionMatching, DC::BlendParams);
	FSM_STATE_DECLARE_ARG(PlayingMovePerformance, MovePerformance::Performance);
	FSM_STATE_DECLARE_ARG(PlayingIdlePerformance, IdlePerfArgs);
	FSM_STATE_DECLARE(Interrupted);
	FSM_STATE_DECLARE_ARG(Handoff, NavAnimHandoffDesc);
	FSM_STATE_DECLARE_ARG(Error, IStringBuilder*);

	void GoMotionMatching();

	U32F GetMaxStateAllocSize();
	void GoInitialState() { GoStopped(); }
	const Clock* GetClock() const { return GetCharacter()->GetClock(); }

	AiLocomotionController();
	~AiLocomotionController();
	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	const DC::NpcDemeanorDef* const* GetDemeanorDefinitions() const;
	U32F GetNumDemeanors() const;

	U32 IssueCommand(const LocoCommand& cmd);
	LocoCommand::Status GetCommandStatus() const { return m_commandStatus; }
	U32 GetCommandId() const { return m_commandId; }
	StringId64 GetAnimStateId() const;

	// from INavAnimController
	virtual Point GetNavigationPosPs() const override;

	virtual void StartNavigating(const NavAnimStartParams& params) override;
	virtual void StopNavigating(const NavAnimStopParams& params) override;
	virtual bool CanProcessCommand() const override;

	virtual bool IsCommandInProgress() const override { return m_commandStatus == LocoCommand::kStatusCommandInProgress; }
	virtual bool HasArrivedAtGoal() const override { return m_commandStatus == LocoCommand::kStatusCommandSucceeded; }
	virtual bool HasMissedWaypoint() const override { return m_commandStatus == LocoCommand::kStatusCommandFailed; }

	virtual NavGoalReachedType GetCurrentGoalReachedType() const override;
	virtual Locator GetGoalPs() const override;

	virtual bool IsMoving() const override { return m_motionState.m_moving; }
	virtual bool IsInterrupted() const override { return IsState(kStateInterrupted); }

	virtual void UpdatePathWaypointsPs(const PathWaypointsEx& pathWaypointsPs,
									   const WaypointContract& waypointContract) override;
	virtual void PatchCurrentPathEnd(const NavLocation& updatedEndLoc) override;
	virtual void InvalidateCurrentPath() override;

	virtual void UpdateMoveDirPs(Vector_arg moveDirPs) override;

	virtual void SetMoveToTypeContinuePoseMatchAnim(const StringId64 animId, float startPhase) override;

	virtual U64 CollectHitReactionStateFlags() const override;

	bool IsMovePerformanceAnimating() const;
	virtual bool MovePerformanceAllowed(MovePerformance::FindParams* pParamsOut) const override;
	virtual bool StartMovePerformance(MovePerformance::Performance& performance) override;

	virtual bool IsIdlePerformanceAllowed(bool allowExistingIdlePerformance = false,
										  IStringBuilder* pReasonOut		= nullptr) const override;
	virtual bool RequestIdlePerformance(StringId64 animId,
										const IdlePerfParams& params,
										IStringBuilder* pErrStrOut = nullptr) override;
	virtual bool IsPlayingIdlePerformance() const override { return IsState(kStatePlayingIdlePerformance); }

	virtual bool ShouldDoProceduralApRef(const AnimStateInstance* pInstance) const override;
	virtual bool CalculateProceduralAlign(const AnimStateInstance* pInstance,
										  const Locator& baseAlignPs,
										  Locator& alignOut) const override;
	virtual bool CalculateProceduralApRef(AnimStateInstance* pInstance) const override;
	virtual bool ShouldAdjustHeadToNav() const override { return !IsMoving(); }
	virtual U32 GetDesiredLegRayCastMode() const override { return LegRaycaster::kModeDefault; }

	virtual void ForceDefaultIdleState(bool playAnim = true) override;
	virtual StateChangeRequest::ID FadeToIdleState(const DC::BlendParams* pBlend = nullptr) override;

	virtual void FadeToIdleStateAndGoStopped(const DC::BlendParams* pBlend = nullptr) override
	{
		m_motionState.m_moving = false;

		if (const StringId64 desSetId = GetMotionMatchingSetForState(m_motionState))
		{
			if (pBlend)
			{
				GoMotionMatching(*pBlend);
			}
			else
			{
				GoMotionMatching();
			}
		}
		else
		{
			FadeToIdleState(pBlend);
			GoStopped();
		}
	}

	virtual void SuppressRetargetScale() override { m_suppressRetargetScale = true; }

	void ResetLocomotionHistory();

	void ProcessCommand();
	bool ProcessCommand_StartLocomoting(const LocoCommand& cmd);
	void ProcessCommand_StopLocomoting(const LocoCommand& cmd);

	void CompleteCommand(LocoCommand::Status commandStatus);

	bool ShouldKillCommand(const LocoCommand& cmd);
	bool IsFacingDesiredDirection() const;

	virtual void RequestDemeanor(Demeanor demeanor, AI_LOG_PARAM) override;
	virtual void ConfigureCharacter(Demeanor demeanor,
									const DC::NpcDemeanorDef* pDemeanorDef,
									const NdAiAnimationConfig* pAnimConfig) override;

	virtual void EnterNewParentSpace(const Transform& matOldToNew,
									 const Locator& oldParentSpace,
									 const Locator& newParentSpace) override;

	void ConfigureMoveSets(const DC::NpcDemeanorDef* pDemeanorDef);
	void ApplyMoveSet(const DC::NpcMoveSetDef* pMoveSet);

	virtual void ConvertToFromCheap() override;
	virtual float GetMaxMovementSpeed() const override;
	virtual float EstimateRemainingTravelTime() const override;

	virtual Demeanor GetRequestedDemeanor() const override { return m_desiredDemeanor; }
	virtual void RequestMotionType(MotionType motionType) override;
	virtual MotionType GetRequestedMotionType() const override { return m_desiredMotionState.m_motionType; }
	virtual MotionType GetCurrentMotionType() const override { return m_motionState.m_motionType; }
	virtual StringId64 GetRequestedMtSubcategory() const override;
	virtual StringId64 GetCurrentMtSubcategory() const override;
	virtual const DC::NpcMoveSetDef* GetCurrentMoveSet() const override;

	virtual bool IsDoingMovePerformance() const override { return IsState(kStatePlayingMovePerformance); }
	virtual bool AllowDialogLookGestures() const override { return !IsDoingMovePerformance() ? true : m_lastMovePerformance.m_allowDialogLookGestures; }
	virtual StringId64 GetMovePerformanceId() const override { return m_lastMovePerformance.m_dcAnimId; }

	void AdjustPathEndPointPs(PathInfo* pPathInfo,
							  const WaypointContract& waypointContract,
							  float goalRadius) const;

	void ForceReachedTypeStopIfNecessary(WaypointContract* pWaypointContract,
										 const IPathWaypoints& pathWaypointsPs,
										 const LocoCommand& cmd,
										 bool& outShouldWalk) const;

	bool IsAtEndOfWaypointPath(float padDist = 0.0f) const;

	// This will not request any further animations. It will just stop
	// attempting to move the actor. And will assume we are stopped already.
	// USE WITH CAUTION!!!
	virtual void Reset() override;
	// This will not request any further animations. It will just stop attempting to move the actor.
	// USE WITH CAUTION!!!
	virtual void Interrupt() override;
	const Vector GetChosenFaceDirWs() const;
	const Vector GetChosenFaceDirPs() const;
	const Vector GetFacingBaseVectorPs() const;
	void UpdateAnimActorInfo();
	StringId64 SetMoveModeOverlay(const MotionType mt);

	MotionState ChangeMotionState(const MotionState& curMotionState,
								  const MotionState& desiredMotionState,
								  bool forceAnimChange = false);

	bool TryDemeanorChange(const Demeanor oldDemeanor, const Demeanor newDemeanor, bool weaponStateUpToDate);
	virtual WeaponUpDownState GetDesiredWeaponUpDownState(bool apCheck) const override;
	virtual bool ConfigureWeaponUpDownOverlays(WeaponUpDownState desState) override;
	virtual bool IsPlayingDemeanorChangeAnim() const override;

	bool TryWeaponStateUpdate();
	virtual bool IsWeaponStateUpToDate() const override;

	Demeanor GetDesiredDemeanorInternal() const;
	virtual void BypassDemeanor(Demeanor demeanor) override;

	void SelectNewAction();

	bool AddMovePerformanceTable(StringId64 entryId, StringId64 tableId) override;
	bool RemoveMovePerformanceTable(StringId64 entryId) override;
	bool HasMovePerformanceTable(StringId64 entryId) const override;

	bool IsDesiredOrientationSatisfied(const float toleranceDeg = kMinTurnInPlaceAngleDeg) const;
	bool UpdateDistanceRefresh(bool updateEverything = false);
	// This controller will do it's best to accommodate the AI. The AI will request certain animation states such as
	// COMBAT/AMBIENT, Walk/Run and Strafe/Normal movement. The controller will migrate over to the desired values as
	// fast as it can making sure that the animation system have transitions for it. During this time the controller
	// will be 'busy.'
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;

	void UpdateWeaponUpDownTracking();
	virtual void PostRootLocatorUpdate() override;
	virtual bool IsBusy() const override;

	MotionState ApplyMotionTypeRestriction(const MotionState& inputState) const;
	virtual MotionType RestrictMotionType(const MotionType desiredMotionType, bool wantToStrafe) const override;
	virtual Vector GetMotionDirPs() const override;

	virtual bool IsStrafing() const override { return m_motionState.m_moving && m_motionState.m_strafing; }

	void UpdateSprungMoveDirPs(Vector_arg desiredMoveDirPs, bool snap = false);

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual float GetMovementSpeedScale() const override;

	virtual float PathRemainingDist() const override;

	virtual bool HasMoveSetFor(MotionType mt) const override;
	virtual bool CanStrafe(MotionType mt) const override;

	void OverrideMotionMatchingSetId(StringId64 newSetId, TimeDelta duration, float minPathLength);
	StringId64 GetMotionMatchingSetForState(const MotionState& ms) const;
	StringId64 GetDesiredMotionMatchingSetId() const;

	bool ShouldDoTargetPoseMatching() const;
	void DebugDrawTargetPoseMatching() const;
	void UpdateMotionMatchingTargetPose();

	virtual void DebugOnly_ForceMovePerfCacheRefresh() override;

	float GetDesiredMoveAngleDeg() const;
	float GetSoftClampAlphaOverride() const;

	virtual void PatchWaypointFacingDir(Vector_arg newFacingDir) override;

	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;

	void DebugPrintMotionState(const MotionState& ms, DoutBase* pDout) const
	{
		STRIP_IN_FINAL_BUILD;
		if (!pDout)
			return;

		pDout->Printf("%s", GetMotionTypeName(ms.m_motionType));

		if (ms.m_strafing)
		{
			pDout->Printf(" strafing");
		}

		if (ms.m_moving)
		{
			pDout->Printf(" moving");
		}
		else
		{
			pDout->Printf(" stopped");
		}

		const StringId64 mmSetId = GetMotionMatchingSetForState(ms);
		if (mmSetId != INVALID_STRING_ID_64)
		{
			pDout->Printf(" mm:%s", DevKitOnly_StringIdToString(mmSetId));
		}
	}
	void DebugPrintMotionState(const MotionState& ms, DoutMemChannels* pDout) const
	{
		STRIP_IN_FINAL_BUILD;
		if (!pDout)
			return;

		pDout->Appendf("%s", GetMotionTypeName(ms.m_motionType));

		if (ms.m_strafing)
		{
			pDout->Appendf(" strafing");
		}

		if (ms.m_moving)
		{
			pDout->Appendf(" moving");
		}
		else
		{
			pDout->Appendf(" stopped");
		}

		const StringId64 mmSetId = GetMotionMatchingSetForState(ms);
		if (mmSetId != INVALID_STRING_ID_64)
		{
			pDout->Appendf(" mm:%s", DevKitOnly_StringIdToString(mmSetId));
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static CONST_EXPR size_t GetMaxArgSize()
	{
		const size_t argSizes[] = {
			sizeof(NavUtil::MoveDirChangeParams), sizeof(CustomLocoStartAnim), sizeof(StringId64),
			sizeof(MovePerformance::Performance), sizeof(NavAnimHandoffDesc),  sizeof(IdlePerfArgs)
		};

		size_t maxArgSize = 0;
		for (U32F i = 0; i < ARRAY_COUNT(argSizes); ++i)
		{
			if (argSizes[i] > maxArgSize)
			{
				maxArgSize = argSizes[i];
			}
		}

		return maxArgSize;
	}

protected:
		StringId64 GetUpcomingDesiredDemeanor() const;

public:

	Demeanor	m_desiredDemeanor;
	MotionState	m_motionState;
	MotionState	m_desiredMotionState;

	bool m_unplannedStop;		// Stop and end of path or just abort and stop now.
	bool m_hasWeaponUpDownOverlays;
	bool m_changingDemeanor;
	bool m_suppressRetargetScale;
	bool m_everMovedTowardsGoal;

	WaypointContract m_waypointContract;

	StringId64 m_moveToTypeContinueMatchAnim;

	float m_moveToTypeContinueMatchStartPhase;
	float m_remainingPathDistance;

	Vector m_desiredMoveDirPs;
	Vector m_sprungMoveDirPs;

	PathInfo m_activePathInfo;

	LocoCommand::Status	m_commandStatus;
	U32					m_commandId;
	LocoCommand			m_pendingCommand;
	LocoCommand			m_activeCommand;
	LocoCommand			m_previousCommand;

	TimeFrame m_moveStartTime;

	SpringTracker<float> m_inertiaSpeedScaleSpring;
	SpringTracker<float> m_leanSpring;

	MovePerformance m_movePerformance;
	MovePerformance::Performance m_lastMovePerformance;

	const DC::NpcMoveSetDef* m_pCurMoveSets[kMotionTypeMax];
	PointCurvePointer m_exactApprochSca[kMotionTypeMax + 1]; // [kMotionTypeMax] used for goal reached type stopped

	StringId64 m_reqMoveSetSubcat[kMotionTypeMax];
	StringId64 m_actMoveSetSubcat[kMotionTypeMax];

	CachedAnimLookup m_cachedPoseMatchSourceLookup;
	CachedAnimLookup m_cachedPoseMatchDestLookup;

	struct TargetPoseMatchInfo
	{
		ArtItemAnimHandle m_matchLoopAnim = ArtItemAnimHandle();
		ArtItemAnimHandle m_matchDestAnim = ArtItemAnimHandle();

		bool m_valid = false;

		float m_desLoopPhaseAtGoal	 = -1.0f;
		float m_actLoopPhaseAtGoal	 = -1.0f;
		float m_lastMatchedLoopPhase = -1.0f;
		float m_remainingTravelTime	 = 0.0f;

		float m_targetPoseBlend = 0.0f;
		float m_translationSkew = 1.0f;

		float m_matchLoopSpeed	 = 0.0f;
		float m_destAnimSpeed	 = -1.0f;

		Point m_goalAnklesOs[2];
		Vector m_goalAnkleVelsOs[2];
	};

	TargetPoseMatchInfo m_poseMatchInfo;

	mutable AnimOverlaySnapshotHash m_lastWeaponOverlayHash;
	mutable bool m_lastWeaponUpToDate;

	mutable SingleFramePtr<CatmullRom> m_motionMatchPathPs;
	mutable SingleFramePtr<NavPolyEdgeGatherer> m_nearbyNavEdges;

	ICharacterLocomotion::LocomotionHistory m_locomotionHistory;

	AssertionAtomic	m_inUseAtomic;

	struct MmSetOverride
	{
		StringId64 m_setId	  = INVALID_STRING_ID_64;
		TimeFrame m_startTime = TimeFrameNegInfinity();
		TimeDelta m_duration  = Seconds(0.0f);
		float m_minPathLength = -1.0f;
	};

	MmSetOverride m_motionMatchingOverride;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiLocomotionController* CreateAiDefaultLocomotionController()
{
	return NDI_NEW AiLocomotionController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
bool IAiLocomotionController::IsKnownIdleState(StringId64 stateId)
{
	switch (stateId.GetValue())
	{
	case SID_VAL("s_idle"):
	case SID_VAL("s_idle-performance"):
	case SID_VAL("s_idle-fight"):
	case SID_VAL("s_idle-fight-mirror"):
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiLocomotionController::AiLocomotionController()
	: m_unplannedStop(false)
	, m_moveToTypeContinueMatchAnim(INVALID_STRING_ID_64)
	, m_moveToTypeContinueMatchStartPhase(0.0f)
	, m_desiredMoveDirPs(kUnitXAxis)
	, m_sprungMoveDirPs(kUnitXAxis)
	, m_desiredDemeanor((Demeanor)0)
	, m_remainingPathDistance(-1.0f)
	, m_suppressRetargetScale(false)
	, m_hasWeaponUpDownOverlays(false)
	, m_changingDemeanor(false)
{
	m_waypointContract.m_rotPs		 = kIdentity;
	m_waypointContract.m_reachedType = kNavGoalReachedTypeStop;
	m_waypointContract.m_motionType	 = kMotionTypeRun;

	m_commandId = 0;
	m_commandStatus = LocoCommand::kStatusAwaitingCommands;

	m_pendingCommand.Reset();
	m_activeCommand.Reset();
	m_previousCommand.Reset();

	m_inertiaSpeedScaleSpring.Reset();
	m_leanSpring.Reset();

	m_pCurMoveSets[kMotionTypeWalk]	  = nullptr;
	m_pCurMoveSets[kMotionTypeRun]	  = nullptr;
	m_pCurMoveSets[kMotionTypeSprint] = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiLocomotionController::~AiLocomotionController()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	AnimActionController::Init(pNavChar, pNavControl);

	m_desiredMoveDirPs = kUnitZAxis;
	m_desiredDemeanor  = pNavChar->GetCurrentDemeanor();

	m_pendingCommand.Reset();
	m_activeCommand.Reset();
	m_previousCommand.Reset();

	ResetLocomotionHistory();

	m_commandStatus = LocoCommand::kStatusAwaitingCommands;

	m_activePathInfo.Reset();

	m_inertiaSpeedScaleSpring.Reset();
	m_leanSpring.Reset();

	m_movePerformance.AllocateCache(kMovePerformanceCacheSize);

	m_lastWeaponUpToDate = false;

	const char* baseName = pNavChar->GetOverlayBaseName();
	const StringId64 weaponUpSetId = AI::CreateNpcAnimOverlayName(baseName, "weapon-up-down", "up");
	const StringId64 weaponDownSetId = AI::CreateNpcAnimOverlayName(baseName, "weapon-up-down", "down");
	const StringId64 noWeaponSetId = AI::CreateNpcAnimOverlayName(baseName, "weapon-up-down", "no-weapon");

	if ((ScriptManager::Lookup<DC::AnimOverlaySet>(weaponUpSetId, nullptr) != nullptr)
		&& (ScriptManager::Lookup<DC::AnimOverlaySet>(weaponDownSetId, nullptr) != nullptr)
		&& (ScriptManager::Lookup<DC::AnimOverlaySet>(noWeaponSetId, nullptr) != nullptr))
	{
		m_hasWeaponUpDownOverlays = true;
	}

	for (U32F i = 0; i < kMotionTypeMax; ++i)
	{
		m_reqMoveSetSubcat[i] = INVALID_STRING_ID_64;
		m_actMoveSetSubcat[i] = INVALID_STRING_ID_64;
	}

	m_moveStartTime = TimeFrameNegInfinity();

	const size_t maxArgSize = GetMaxArgSize();
	GetStateMachine().Init(this, FILE_LINE_FUNC, true, maxArgSize);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::NpcDemeanorDef* const* AiLocomotionController::GetDemeanorDefinitions() const
{
	const NavCharacter* pNavChar = GetCharacter();
	return pNavChar->GetDemeanorDefinitions();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AiLocomotionController::GetNumDemeanors() const
{
	const NavCharacter* pNavChar = GetCharacter();
	return pNavChar->GetNumDemeanors();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 AiLocomotionController::IssueCommand(const LocoCommand& cmd)
{
	AI_ASSERT(cmd.GetType() != LocoCommand::kCmdInvalid);

	++m_commandId;
	m_pendingCommand = cmd;
	m_commandStatus = LocoCommand::kStatusCommandPending;
	LocoLog("Issued: %s\n", cmd.GetTypeName());
	return m_commandId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiLocomotionController::GetAnimStateId() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const StringId64 stateId = pBaseLayer ? pBaseLayer->CurrentStateId() : INVALID_STRING_ID_64;
	return stateId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point AiLocomotionController::GetNavigationPosPs() const
{
	const NavCharacter* pNavChar = GetCharacter();
	Point posPs = pNavChar ? pNavChar->GetTranslationPs() : Point(kOrigin);

	if (IsCommandInProgress())
	{
		if (const BaseState* pState = GetState())
		{
			posPs = pState->GetNavigationPosPs(posPs);
		}
	}

	return posPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::ForceDefaultIdleState(bool playAnim /*= true*/)
{
	LocoLogStr("ForceDefaultIdleState()\n");

	ResetLocomotionHistory();

	if (playAnim)
	{
		DC::BlendParams blend;
		blend.m_animFadeTime = 0.0f;
		blend.m_motionFadeTime = 0.0f;

		FadeToIdleState(&blend);
	}

	m_motionState.m_moving = false;
	GoStopped();

	Reset();

	GoStopped();
	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StateChangeRequest::ID AiLocomotionController::FadeToIdleState(const DC::BlendParams* pBlend /* = nullptr */)
{
	LocoLog("FadeToIdleState [anim: %0.3fs] [motion: %0.3fs] [curve: %s]\n",
			pBlend ? pBlend->m_animFadeTime : -1.0f,
			pBlend ? pBlend->m_motionFadeTime : -1.0f,
			DC::GetAnimCurveTypeName(pBlend ? pBlend->m_curve : DC::kAnimCurveTypeInvalid));

	NavCharacter* pNavChar = GetCharacter();

	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	StateChangeRequest::ID changeId = StateChangeRequest::kInvalidId;

	if (pBaseLayer)
	{
		const StringId64 idleStateId = SID("s_idle");

		if (pBaseLayer->CurrentStateId() != idleStateId)
		{
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
				params.m_animFadeTime	= 0.4f;
				params.m_motionFadeTime = 0.4f;
				params.m_blendType		= DC::kAnimCurveTypeUniformS;
			}

			changeId = pBaseLayer->FadeToState(idleStateId, params);
		}
	}

	return changeId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::ResetLocomotionHistory()
{
	m_locomotionHistory.Reset();

	if (const NdGameObject* pGo = GetCharacterGameObject())
	{
		ICharacterLocomotion::LocomotionState locoState;
		locoState.m_alignPs = pGo->GetLocatorPs();
		locoState.m_time = TimeFrameNegInfinity();
		locoState.m_velPs = kZero;
		locoState.m_yawSpeed = 0.0f;

		m_locomotionHistory.Enqueue(locoState);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::StartNavigating(const NavAnimStartParams& params)
{
	NAV_ASSERT(IsFinite(params.m_waypoint.m_rotPs));

	if (FALSE_IN_FINAL_BUILD(params.m_moveArgs.m_motionType >= kMotionTypeMax))
	{
		MailNpcLogTo(GetCharacter(),
					 "john_bellomy@naughtydog.com",
					 "StartNavigating with invalid motion type",
					 FILE_LINE_FUNC);
	}

	AI_ASSERT(params.m_moveArgs.m_motionType < kMotionTypeMax);

	NavCharacter* pNavChar = GetCharacter();
	LocoCommand cmd;
	cmd.AsStartLocomoting(pNavChar, params);

	IssueCommand(cmd);

	if (params.m_pPathPs)
	{
		UpdatePathWaypointsPs(*params.m_pPathPs, params.m_waypoint);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::StopNavigating(const NavAnimStopParams& params)
{
	NAV_ASSERT(IsFinite(params.m_waypoint.m_rotPs));

	NavCharacter* pNavChar = GetCharacter();
	LocoCommand cmd;
	cmd.AsStopLocomoting(pNavChar, params);

	IssueCommand(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 AiLocomotionController::CollectHitReactionStateFlags() const
{
	if (IsInterrupted())
	{
		return 0;
	}

	if (IsMoving())
	{
		switch (m_motionState.m_motionType)
		{
		case kMotionTypeWalk:
			return DC::kHitReactionStateMaskWalking;

		case kMotionTypeRun:
			return DC::kHitReactionStateMaskRunning;

		case kMotionTypeSprint:
			return DC::kHitReactionStateMaskSprinting;

		default: // Normal walk and run
			return DC::kHitReactionStateMaskMoving;
		}
	}

	return DC::kHitReactionStateMaskIdle;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::IsMovePerformanceAnimating() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	const AnimStateInstance* pMovePerfInst = pBaseLayer ? pBaseLayer->FindInstanceByName(SID("s_performance-move"))
														: nullptr;

	return pMovePerfInst != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::MovePerformanceAllowed(MovePerformance::FindParams* pParamsOut) const
{
	const NavCharacter* pNavChar = GetCharacter();

	bool onHorse = false;
	const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs();
	if (const Npc* pNpc = Npc::FromProcess(pNavChar))
	{
		if (const Horse* pHorse = Horse::FromProcess(pNpc->m_hHorse.ToProcess()))
		{
			onHorse = true;
			pPathPs = pHorse->GetPathPs();
		}
	}

	if (!onHorse)
	{
		if (!IsMoving())
			return false;

		if (GetStateMachine().HasNewState())
			return false;

		if (!IsState(kStateMotionMatching) && !IsState(kStateHandoff))
			return false;

		if (!m_activePathInfo.IsValid())
			return false;

		if (pNavChar->OnStairs())
			return false;

		if (!m_desiredMotionState.m_moving)
			return false;

		if (pNavChar->IsWaitingForPath())
			return false;

		if (IsMovePerformanceAnimating())
			return false;
	}

	const float curPathLength = pPathPs ? pPathPs->ComputePathLengthXz() : 0.0f;

	if (curPathLength < 0.5f)
		return false;

	if (pParamsOut)
	{
		const bool reachedTypeStop = m_waypointContract.m_reachedType == kNavGoalReachedTypeStop;
		pParamsOut->m_pPathPs = pPathPs;
		pParamsOut->m_allowStoppingPerformance = reachedTypeStop;
		pParamsOut->m_moveDirPs = SafeNormalize(pNavChar->GetVelocityPs(), m_sprungMoveDirPs);
		pParamsOut->m_reqMotionType = m_motionState.m_motionType;
		pParamsOut->m_minRemainingPathDist = reachedTypeStop
												 ? g_navCharOptions.m_movePerformances.m_minRemPathDistStopped
												 : g_navCharOptions.m_movePerformances.m_minRemPathDistMoving;
		pParamsOut->m_speed = Length(pNavChar->GetVelocityPs());
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::StartMovePerformance(MovePerformance::Performance& performance)
{
	const NavCharacter* pNavChar = GetCharacter();

	LocoLog("Starting Move Performance '%s'\n", DevKitOnly_StringIdToString(performance.m_info.m_resolvedAnimId));

	MovePerformance::CalcStartPhase(pNavChar, &performance);

	m_lastMovePerformance = performance;

	GoPlayingMovePerformance(performance);
	GetStateMachine().TakeStateTransition();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::IsIdlePerformanceAllowed(bool allowExistingIdlePerformance /* = false */,
													  IStringBuilder* pReasonOut /* = nullptr */) const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	if (!pBaseLayer)
	{
		if (NULL_IN_FINAL_BUILD(pReasonOut))
		{
			pReasonOut->format("Npc has no base layer (?!)");
		}

		return false;
	}

	if (pBaseLayer->CurrentStateId() == SID("s_evade-state"))
	{
		if (NULL_IN_FINAL_BUILD(pReasonOut))
		{
			pReasonOut->format("Npc is evading");
		}

		return false;
	}

	const bool stateValid = IsState(kStateStopped)
							|| (IsState(kStateMotionMatching) && !m_motionState.m_moving)
							|| (allowExistingIdlePerformance && IsState(kStatePlayingIdlePerformance));

	bool stopped = stateValid && !pBaseLayer->AreTransitionsPending();

	if (!stopped && IsState(kStateInterrupted))
	{
		NavAnimHandoffDesc handoff;
		if (pNavChar->PeekValidHandoff(&handoff))
		{
			stopped = handoff.m_motionType == kMotionTypeMax;
		}
	}

	if (!stopped)
	{
		if (NULL_IN_FINAL_BUILD(pReasonOut))
		{
			pReasonOut->format("Npc is not stopped");
		}

		return false;
	}

	if (!CanProcessCommand())
	{
		if (NULL_IN_FINAL_BUILD(pReasonOut))
		{
			pReasonOut->format("Npc not ready to process commands (%s)", GetStateName("???"));
		}

		return false;
	}

	if (IsCommandInProgress())
	{
		if (NULL_IN_FINAL_BUILD(pReasonOut))
		{
			pReasonOut->format("Npc is processing command %d : %s", m_commandId, m_activeCommand.GetTypeName());
		}

		return false;
	}

	const NavLocation navLoc = pNavChar->GetNavLocation();

	if (navLoc.GetNavMeshHandle().IsNull())
	{
		if (NULL_IN_FINAL_BUILD(pReasonOut))
		{
			pReasonOut->format("Npc has no Nav Mesh");
		}

		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::RequestIdlePerformance(StringId64 animId,
													const IdlePerfParams& params,
													IStringBuilder* pErrStrOut /* = nullptr */)
{
	if (!IsIdlePerformanceAllowed(params.m_allowInExistingIdlePerformance, pErrStrOut))
	{
		return false;
	}

	LocoLog("RequestIdlePerformance '%s' %s [loop: %s] [skip clearance: %s]\n",
			DevKitOnly_StringIdToString(animId),
			params.m_pWaitAction ? "(wait)" : "",
			params.m_loop ? "true" : "false",
			params.m_disableClearanceCheck ? "true" : "false");

	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const ArtItemAnimHandle hAnim = pAnimControl ? pAnimControl->LookupAnim(animId) : ArtItemAnimHandle();
	const ArtItemAnim* pAnim = hAnim.ToArtItem();

	if (!pAnim)
	{
		if (NULL_IN_FINAL_BUILD(pErrStrOut))
		{
			pErrStrOut->format("Animation '%s' not found", DevKitOnly_StringIdToString(animId));
		}

		return false;
	}

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const NavLocation navLoc = pNavChar->GetNavLocation();
	const NavMesh* pNavMesh = navLoc.ToNavMesh();

	if (!pNavMesh)
	{
		if (NULL_IN_FINAL_BUILD(pErrStrOut))
		{
			pErrStrOut->format("Npc has no Nav Mesh");
		}

		return false;
	}

	const SkeletonId skelId = pNavChar->GetSkeletonId();
	const Locator locWs = pNavChar->GetLocator();

	const NavControl* pNavCon = GetNavControl();

	NavMesh::ProbeParams probeParams;
	probeParams.m_probeRadius = pNavCon->GetMaximumNavAdjustRadius();
	probeParams.m_pStartPoly = navLoc.ToNavPoly();
	probeParams.m_obeyedStaticBlockers = pNavCon->GetObeyedStaticBlockers();

	if (!params.m_disableClearanceCheck
		&& !AI::AnimHasClearMotion(skelId, pAnim, locWs, locWs, nullptr, pNavMesh, probeParams, false))
	{
		if (NULL_IN_FINAL_BUILD(pErrStrOut))
		{
			pErrStrOut->format("Anim is not clear on Nav Mesh");
		}

		return false;
	}

	IdlePerfArgs args;
	args.m_hAnim	 = hAnim;
	args.m_ssAction	 = MAYBE::kNothing;
	args.m_loop		 = params.m_loop;
	args.m_phaseSync = params.m_phaseSync;
	args.m_fadeTime	 = params.m_fadeTime;
	args.m_animCurveType = params.m_animCurveType;
	args.m_mirror		 = params.m_mirror;

	if (params.m_pWaitAction)
	{
		args.m_ssAction = *params.m_pWaitAction;
	}

	if (!GoPlayingIdlePerformance(args))
	{
		if (NULL_IN_FINAL_BUILD(pErrStrOut))
		{
			pErrStrOut->format("Failed to go to state (npc got preempted)");
		}

		return false;
	}

	GetStateMachine().TakeStateTransition();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::ProcessCommand_StartLocomoting(const LocoCommand& cmd)
{
	NavCharacter* pNavChar = GetCharacter();

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const StringId64 mtSubcategory = cmd.GetMotionTypeSubcategory();
	const MotionType motionType = cmd.GetMotionType();

	AI_ASSERT(motionType < kMotionTypeMax);

	m_reqMoveSetSubcat[motionType] = mtSubcategory;
	m_actMoveSetSubcat[motionType] = INVALID_STRING_ID_64;

	// pick a new move set, if we have variations or want to change sub-categories. this can change while the character is moving.
	const DC::NpcDemeanorDef* pDemeanorDef = pNavChar->GetCurrentDemeanorDef();
	if (!pDemeanorDef)
	{
		if (FALSE_IN_FINAL_BUILD(true))
		{
			StringBuilder<256>* pMsg = NDI_NEW(kAllocSingleFrame) StringBuilder<256>;

			pMsg->format("Trying to navigate with no valid demeanor for '%s'",
						 DemeanorToString(pNavChar->GetCurrentDemeanor()));

			const IStringBuilder* pArg = pMsg;
			GoError(pArg);
		}
		return false;
	}

	ConfigureMoveSets(pDemeanorDef);

	bool atLeastOneValid = false;

	for (U32F i = 0; i < kMotionTypeMax; ++i)
	{
		if (Nav::IsMoveSetValid(m_pCurMoveSets[i]))
		{
			atLeastOneValid = true;
			break;
		}
	}

	if (!atLeastOneValid)
	{
		LocoLogStr("Tried to move with no valid move sets, failing command\n");
		return false;
	}

	m_activePathInfo.Reset();

	m_desiredMoveDirPs = AsUnitVectorXz(cmd.GetMoveDirPs(), kZero);

	if (cmd.IsFollowingPath())
	{
		if (const PathWaypointsEx* pLastFoundPathPs = pNavChar->GetLastFoundPathPs())
		{
			const Point myPosPs = pNavChar->GetTranslationPs();

			I32F iLeg = -1;
			pLastFoundPathPs->ClosestPointXz(myPosPs, nullptr, &iLeg);

			const Point leg0 = pLastFoundPathPs->GetWaypoint(iLeg);
			const Point leg1 = pLastFoundPathPs->GetWaypoint(iLeg + 1);

			const Vector newDirPs = AsUnitVectorXz(leg1 - leg0, kZero);

			if ((pLastFoundPathPs->ComputePathLengthXz() > 0.5f) || (Abs(LengthSqr(m_desiredMoveDirPs) - 1.0f) > 0.1f))
			{
				m_desiredMoveDirPs = newDirPs;
			}

			m_activePathInfo.SetPathWaypointsPs(*pLastFoundPathPs);
		}
	}

	m_desiredMotionState.Reset();
	m_desiredMotionState.m_moving	  = true;
	m_desiredMotionState.m_strafing	  = cmd.GetStrafeMode() == DC::kNpcStrafeAlways;
	m_desiredMotionState.m_motionType = cmd.GetMotionType();

	m_everMovedTowardsGoal = false;

	const bool moving = m_motionState.m_moving;
	m_motionState = ApplyMotionTypeRestriction(m_desiredMotionState);
	m_motionState.m_moving = moving;

	SetMoveModeOverlay(m_motionState.m_motionType);

	m_unplannedStop = false;

	m_waypointContract = cmd.GetWaypointContract();

	if (const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs())
	{
		bool shouldWalk = false;
		ForceReachedTypeStopIfNecessary(&m_waypointContract, *pPathPs, cmd, shouldWalk);
	}

	if (BaseState* pState = GetState())
	{
		pState->OnStartLocomoting(cmd);
	}

	if (!moving && (cmd.GetType() == LocoCommand::kCmdStartLocomoting))
	{
		m_moveStartTime = pNavChar->GetCurTime();
	}

	if (DoutMemChannels* pDebugLog = NULL_IN_FINAL_BUILD(pNavChar->GetChannelDebugLog()))
	{
		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

		LocoLogStr("ProcessCommand_StartLocomoting ");
		DebugPrintMotionState(m_desiredMotionState, pDebugLog);
		pDebugLog->Appendf(" -> ");
		DebugPrintMotionState(m_motionState, pDebugLog);
		pDebugLog->Appendf("\n");
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::ProcessCommand_StopLocomoting(const LocoCommand& cmd)
{
	NavCharacter* pNavChar = GetCharacter();

	m_waypointContract = cmd.GetWaypointContract();
	m_waypointContract.m_reachedType = kNavGoalReachedTypeStop;

	// Let the state change set the 'moveInProgress' flag
	m_desiredMotionState.m_moving = false;
	m_unplannedStop = true;
	m_everMovedTowardsGoal = false;

	if (BaseState* pState = GetState())
	{
		pState->OnStopLocomoting(cmd);
	}

	m_activePathInfo.Reset();

	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	LocoLog("Processed: StopLocomoting : radius: %f\n", cmd.GetGoalRadius());
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::CanProcessCommand() const
{
	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_locomotionController.m_forceSuspendCommands))
	{
		return false;
	}

	bool canProcess = false;

	if (const BaseState* pCurState = GetState())
	{
		canProcess = pCurState->CanProcessCommand();
	}

	return canProcess;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::CompleteCommand(LocoCommand::Status commandStatus)
{
	m_commandStatus = commandStatus;
	m_activePathInfo.Reset();
	m_previousCommand = m_activeCommand;
	m_activeCommand.Reset();
	m_remainingPathDistance = -1.0f;

	m_poseMatchInfo = TargetPoseMatchInfo();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::ShouldKillCommand(const LocoCommand& cmd)
{
	if (m_commandStatus != LocoCommand::kStatusCommandInProgress)
		return false;

	if (m_activeCommand.GetType() != cmd.GetType())
		return false;

	if (cmd.GetMotionType() != m_desiredMotionState.m_motionType)
		return false;

/*
	if (cmd.GetPathWaypointsPs().GetWaypointCount() != 2)
		return false;

	const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs();
	const U32F pathLength = pPathPs ? pPathPs->GetWaypointCount() : 0;
	if (pathLength != 2)
		return false;

	const Point desGoalPs = cmd.GetPathWaypointsPs().GetWaypoint(1);
	const Point curGoalPs = m_activePathInfo.GetOrgEndWaypointPs();

	if (DistSqr(desGoalPs, curGoalPs) > 0.1f)
		return false;
*/

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::ProcessCommand()
{
	switch (m_pendingCommand.GetType())
	{
	case LocoCommand::kCmdStopLocomoting:
		ProcessCommand_StopLocomoting(m_pendingCommand);
		m_commandStatus = LocoCommand::kStatusCommandInProgress;
		break;

	case LocoCommand::kCmdStartLocomoting:
		if (ShouldKillCommand(m_pendingCommand))
		{
			LocoLogStr("Killing pending command");
		}
		else if (ProcessCommand_StartLocomoting(m_pendingCommand))
		{
			m_commandStatus = LocoCommand::kStatusCommandInProgress;
		}
		else
		{
			m_commandStatus = LocoCommand::kStatusCommandFailed;
		}
		break;

	default:
		m_commandStatus = LocoCommand::kStatusCommandFailed;
		AI_ASSERT(false);
		break;
	}

	m_previousCommand = m_activeCommand;
	m_activeCommand = m_pendingCommand;
	m_pendingCommand.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::IsFacingDesiredDirection() const
{
	const float facingAngleEpsilonDegrees = 5.0f;

	const Vector forwardWs = GetLocalZ(GetCharacter()->GetRotation());
	float deltaFacingAngleDegrees = RADIANS_TO_DEGREES(SafeAcos(Dot(forwardWs, GetChosenFaceDirWs())));

	return deltaFacingAngleDegrees <= facingAngleEpsilonDegrees;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::RequestDemeanor(Demeanor demeanor, AI_LOG_PARAM)
{
	if (m_changingDemeanor)
		return;

	if (demeanor == m_desiredDemeanor)
		return;

	NavCharacter* pNavChar = GetCharacter();
	LocoLog("RequestDemeanor: new desired='%s' prev desired='%s' current='%s'\n",
			pNavChar->GetDemeanorName(demeanor),
			pNavChar->GetDemeanorName(m_desiredDemeanor),
			pNavChar->GetDemeanorName(pNavChar->GetCurrentDemeanor()));

	m_desiredDemeanor = demeanor;

	// Filth Hack to make horse demeanors update for playtest.
	if (Npc* pNpc = Npc::FromProcess(pNavChar))
	{
		if (pNpc->GetHorse())
		{
			pNpc->ForceDemeanor(demeanor, AI_LOG);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::ConfigureCharacter(Demeanor demeanor,
												const DC::NpcDemeanorDef* pDemeanorDef,
												const NdAiAnimationConfig* pAnimConfig)
{
	PROFILE_AUTO(Animation);

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimOverlays* pOverlays = pAnimControl ? pAnimControl->GetAnimOverlays() : nullptr;

	AI_ASSERT(pDemeanorDef);

	// Update the valid states
	if (demeanor != m_desiredDemeanor)
	{
		LocoLog("New Demeanor: '%s' prev desired='%s' current='%s'\n",
				pNavChar->GetDemeanorName(demeanor),
				pNavChar->GetDemeanorName(m_desiredDemeanor),
				pNavChar->GetDemeanorName(pNavChar->GetCurrentDemeanor()));
	}
	else
	{
		LocoLog("Configure Character: '%s' (prev: '%s')\n",
				pNavChar->GetDemeanorName(demeanor),
				pNavChar->GetDemeanorName(pNavChar->GetPreviousDemeanor()));
	}

	for (int i = 0; i < kMotionTypeMax; ++i)
	{
		m_actMoveSetSubcat[i] = INVALID_STRING_ID_64;
	}

	ConfigureMoveSets(pDemeanorDef);

	if (BaseState* pCurState = GetState())
	{
		pCurState->OnConfigureCharacter(demeanor);

		GetStateMachine().TakeStateTransition();
	}

	const MotionState oldMotionState = m_motionState;

	// re-apply motion type restrictions for the new demeanor
	m_motionState		 = ApplyMotionTypeRestriction(m_motionState);
	m_desiredMotionState = ApplyMotionTypeRestriction(m_desiredMotionState);

	if (!m_changingDemeanor)
	{
		SetMoveModeOverlay(m_motionState.m_motionType);

		if (!m_motionState.Equals(oldMotionState))
		{
			LocoLog("ConfigureCharacter: external demeanor change detected, new motion type %s%s%s -> %s%s%s\n",
					GetMotionTypeName(oldMotionState.m_motionType),
					oldMotionState.m_strafing ? "-Strafe" : "",
					oldMotionState.m_moving ? "-Moving" : "",
					GetMotionTypeName(m_motionState.m_motionType),
					m_motionState.m_strafing ? "-Strafe" : "",
					m_motionState.m_moving ? "-Moving" : "");
		}
	}

	const StringId64 existingTableId = m_movePerformance.GetTableId(SID("move^move"));
	if (existingTableId != pDemeanorDef->m_moveToMoveSet)
	{
		m_movePerformance.RemoveTable(pNavChar, SID("move^move"));
		m_movePerformance.AddTable(pNavChar, SID("move^move"), pDemeanorDef->m_moveToMoveSet);
	}

	StringId64 exactScaId = SID("*npc-default-approach-sca-idle*");

	if (pDemeanorDef->m_exactApproachScaIdle != INVALID_STRING_ID_64)
	{
		exactScaId = pDemeanorDef->m_exactApproachScaIdle;
	}

	m_exactApprochSca[kMotionTypeMax].Configure(exactScaId, SID("npc-locomotion"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::EnterNewParentSpace(const Transform& matOldToNew,
												 const Locator& oldParentSpace,
												 const Locator& newParentSpace)
{
	for (ICharacterLocomotion::LocomotionState& locoState : m_locomotionHistory)
	{
		locoState.m_alignPs = Locator(locoState.m_alignPs.AsTransform() * matOldToNew);
		locoState.m_velPs = locoState.m_velPs * matOldToNew;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::ConfigureMoveSets(const DC::NpcDemeanorDef* pDemeanorDef)
{
	AI_ASSERT(pDemeanorDef);
	if (!pDemeanorDef)
		return;

	const NavCharacter* pNavChar = GetCharacter();
	const StringId64 defaultMtSubCat = pNavChar->GetDefaultMotionTypeSubcategory();

	bool atLeastOneValid = false;

	static CONST_EXPR StringId64 s_defaultScas[kMotionTypeMax] = { SID("*npc-default-approach-sca-walk*"),
																   SID("*npc-default-approach-sca-run*"),
																   SID("*npc-default-approach-sca-sprint*") };

	for (U32F i = 0; i < kMotionTypeMax; ++i)
	{
		const MotionType mt = MotionType(i);
		const DC::NpcMotionType dcMt = GameMotionTypeToDc(mt);

		StringId64 reqSubCat = m_reqMoveSetSubcat[i];

		if (reqSubCat == INVALID_STRING_ID_64)
		{
			reqSubCat = defaultMtSubCat;
		}

		const DC::NpcMotionTypeMoveSets& mtMoveSets = pDemeanorDef->m_moveSets[dcMt];
		const DC::NpcMoveSetContainer* pContainer	= Nav::PickMoveSetSubcat(mtMoveSets,
																			 reqSubCat,
																			 &m_actMoveSetSubcat[i]);

		const DC::NpcMoveSetDef* pMoveSet = Nav::PickMoveSet(pNavChar, pContainer);

		if (pMoveSet != m_pCurMoveSets[mt])
		{
			m_pCurMoveSets[mt] = pMoveSet;

			LocoLogDetails("New move set for mt '%s' : %s %s\n",
						   GetMotionTypeName(mt),
						   pMoveSet ? pMoveSet->m_name.m_string.GetString() : "<null>",
						   (pMoveSet && pMoveSet->m_motionMatchingSetId != INVALID_STRING_ID_64)
							   ? DevKitOnly_StringIdToString(pMoveSet->m_motionMatchingSetId)
							   : "");
		}

		StringId64 exactScaId = s_defaultScas[i];

		if (pMoveSet)
		{
			atLeastOneValid = true;

			if (pMoveSet->m_exactApproachSca != INVALID_STRING_ID_64)
			{
				exactScaId = pMoveSet->m_exactApproachSca;
			}
		}

		m_exactApprochSca[i].Configure(exactScaId, SID("npc-locomotion"));
	}

	if (FALSE_IN_FINAL_BUILD(!atLeastOneValid && Memory::IsDebugMemoryAvailable()))
	{
		StringBuilder<256>* pMsg = NDI_NEW(kAllocSingleFrame) StringBuilder<256>;
		pMsg->format("Demeanor '%s' has no valid move sets", pDemeanorDef->m_name.m_string.GetString());
		const IStringBuilder* pArg = pMsg;
		GoError(pArg);
		GetStateMachine().TakeStateTransition();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::ApplyMoveSet(const DC::NpcMoveSetDef* pMoveSet)
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimOverlays* pOverlays = pAnimControl ? pAnimControl->GetAnimOverlays() : nullptr;

	if (pMoveSet)
	{
		pOverlays->SetOverlaySet(pMoveSet->m_overlayName);
	}
	else
	{
		pOverlays->ClearLayer(SID("move-set"));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::ConvertToFromCheap()
{
	const NavCharacter* pNavChar = GetCharacter();
	const DC::NpcDemeanorDef* pDemeanorDef = pNavChar ? pNavChar->GetCurrentDemeanorDef() : nullptr;
	if (!pDemeanorDef)
		return;

	for (U32F i = 0; i < kMotionTypeMax; ++i)
	{
		m_actMoveSetSubcat[i] = INVALID_STRING_ID_64;
	}

	ConfigureMoveSets(pDemeanorDef);

	if (BaseState* pCurState = GetState())
	{
		const Demeanor demeanor = pNavChar->GetCurrentDemeanor();

		pCurState->OnConfigureCharacter(demeanor);

		GetStateMachine().TakeStateTransition();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiLocomotionController::GetMaxMovementSpeed() const
{
	const BaseState* pState = GetState();
	const float maxSpeed = pState ? pState->GetMaxMovementSpeed() : 0.0f;
	return maxSpeed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiLocomotionController::EstimateRemainingTravelTime() const
{
	if (m_poseMatchInfo.m_valid)
	{
		return m_poseMatchInfo.m_remainingTravelTime;
	}

	if (m_remainingPathDistance < NDI_FLT_EPSILON)
	{
		return -1.0f;
	}

	const float maxSpeed = GetMaxMovementSpeed();

	if (maxSpeed < NDI_FLT_EPSILON)
	{
		return -1.0f;
	}

	return m_remainingPathDistance / maxSpeed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::RequestMotionType(MotionType motionType)
{
	if (motionType < kMotionTypeWalk || motionType >= kMotionTypeMax)
	{
		LocoLog("RequestMotionType: Illegal motion type requested %d\n", motionType);
		return;
	}

	if (m_desiredMotionState.m_motionType != motionType)
	{
		LocoLog("RequestMotionType: %s -> %s\n",
				GetMotionTypeName(m_desiredMotionState.m_motionType),
				GetMotionTypeName(motionType));

		m_desiredMotionState.m_motionType = motionType;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiLocomotionController::GetRequestedMtSubcategory() const
{
	StringId64 mtSubCat = INVALID_STRING_ID_64;

	if (m_motionState.m_motionType != kMotionTypeMax)
	{
		mtSubCat = m_reqMoveSetSubcat[m_motionState.m_motionType];
	}

	if (mtSubCat == INVALID_STRING_ID_64)
	{
		const NavCharacter* pNavChar = GetCharacter();
		mtSubCat = pNavChar->GetDefaultMotionTypeSubcategory();
	}

	return mtSubCat;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiLocomotionController::GetCurrentMtSubcategory() const
{
	if (m_motionState.m_motionType < kMotionTypeMax)
	{
		return m_actMoveSetSubcat[m_motionState.m_motionType];
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::NpcMoveSetDef* AiLocomotionController::GetCurrentMoveSet() const
{
	MotionType motionType = GetCurrentMotionType();
	if (motionType < kMotionTypeWalk || motionType >= kMotionTypeMax)
		return nullptr;

	return m_pCurMoveSets[motionType];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::AdjustPathEndPointPs(PathInfo* pPathInfo,
												  const WaypointContract& waypointContract,
												  float goalRadius) const
{
	PathWaypointsEx* pPathWaypointsPs = pPathInfo ? pPathInfo->GetPathWaypointsPs() : nullptr;
	if (!pPathWaypointsPs)
		return;

	if (goalRadius < NDI_FLT_EPSILON)
		return;

	const U32F pathCount = pPathWaypointsPs->GetWaypointCount();
	if (pathCount != 2)
		return;

	const NavCharacter* pNavChar = GetCharacter();
	const Point myPosPs = pNavChar->GetTranslation();
	const Point endPosPs = pPathInfo->GetOrgEndWaypointPs();

	const float distToGoal = DistXz(myPosPs, endPosPs);
	const float effectTT = LerpScale(goalRadius, goalRadius * 1.5f, 1.0f, 0.0f, distToGoal);

	if (effectTT < NDI_FLT_EPSILON)
		return;

	const Vector velPs =  pNavChar->GetVelocityPs();

	Scalar speed;
	const Vector normVelocityPs = SafeNormalize(VectorXz(velPs), kZero, speed);

	const Vector curMoveDirPs = SafeNormalize(myPosPs - pNavChar->GetPrevLocatorPs().Pos(), normVelocityPs);

	float triggerSpeed = 2.0f;

	switch (m_motionState.m_motionType)
	{
	case kMotionTypeWalk:
		triggerSpeed = 1.0f;
		break;
	case kMotionTypeRun:
		triggerSpeed = 2.0f;
		break;
	case kMotionTypeSprint:
		triggerSpeed = 2.5f;
		break;
	}

	if (speed < triggerSpeed)
		return;

	//g_prim.Draw(DebugArrow(myPosPs + kUnitYAxis, curMoveDirPs, kColorOrange));
	//MsgCon("speed = %f\n", (float)speed);

	const Plane& goalPlanePs = pPathInfo->GetGoalPlanePs();
	const Ray moveRayPs = Ray(myPosPs, myPosPs + curMoveDirPs);

	Point projectedPosPs = endPosPs;
	if (PlaneRayIntersectionXz(goalPlanePs, moveRayPs, projectedPosPs))
	{
		const Vector toProjectedPos = LimitVectorLength(VectorXz(projectedPosPs - endPosPs), 0.0f, goalRadius);

		const Point updatedEndPosPs = endPosPs + (toProjectedPos * effectTT);

		pPathWaypointsPs->UpdateEndPoint(updatedEndPosPs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// If the committed path to a 'continue' goal is approaching from the wrong direction we force a goal reached of 'stop and face'
void AiLocomotionController::ForceReachedTypeStopIfNecessary(WaypointContract* pWaypointContract,
															 const IPathWaypoints& pathWaypointsPs,
															 const LocoCommand& cmd,
															 bool& outShouldWalk) const
{
	if (!pWaypointContract || (pWaypointContract->m_reachedType != kNavGoalReachedTypeContinue))
		return;

	if (IsStrafing())
		return;

	if (!cmd.IsGoalFaceValid())
		return;

	const U32F pathCount = pathWaypointsPs.GetWaypointCount();
	if (pathCount != 2)
		return;

	const NavCharacter* pNavChar = GetCharacter();
	const Point myPosPs = pNavChar->GetTranslation();
	const Point endPosPs = pathWaypointsPs.GetEndWaypoint();

	const float distToGoal = DistXz(myPosPs, endPosPs);
	const float goalRadius = Max(cmd.GetGoalRadius(), pNavChar->GetMotionConfig().m_minimumGoalRadius);
	if ((distToGoal <= goalRadius) && m_everMovedTowardsGoal)
		return;

	// We need to observe the 'useFacing' flag as a character can do a patrol where the waypoint contract isn't
	// oriented along the direction of the patrol path
	const Vector contractDirPs = GetLocalZ(pWaypointContract->m_rotPs);
	const Vector moveDirPs = SafeNormalize(endPosPs - myPosPs, contractDirPs);

	const float dotP = Dot(moveDirPs, contractDirPs);
	const float minDot = Cos(DEGREES_TO_RADIANS(75.0f));

	if (dotP >= minDot)
		return;

	if (cmd.GetMoveArgs().m_slowInsteadOfAutoStop && CanStrafe(kMotionTypeWalk))
	{
		outShouldWalk = true;
		LocoLogStr("Forcing walk motion type because approaching from wrong direction\n");
		if (FALSE_IN_FINAL_BUILD(true))
		{
			g_prim.Draw(DebugString(myPosPs + Vector(0.0f, 0.3f, 0.0f), "Forcing walk", kColorMagenta));
		}
		return;
	}

	if (FALSE_IN_FINAL_BUILD(true))
	{
		g_prim.Draw(DebugString(myPosPs + Vector(0.0f, 0.3f, 0.0f), "Forcing reached type stop", kColorRed));
	}

	pWaypointContract->m_reachedType = kNavGoalReachedTypeStop;

	if (!cmd.m_reachedTypeForced)
	{
		LocoLogStr("Forcing continue goal type to be stop because approaching from wrong direction\n");
		cmd.m_reachedTypeForced = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PatchCurrentPathEnd(const NavLocation& updatedEndLoc)
{
	// Don't listen to information unless we are actually processing a command
	if (m_commandStatus != LocoCommand::kStatusCommandInProgress)
		return;

	if (PathWaypointsEx* pActivePathPs = m_activePathInfo.GetPathWaypointsPs())
	{
		pActivePathPs->UpdateEndLoc(GetCharacter()->GetParentSpace(), updatedEndLoc);
		m_activePathInfo.SetPathWaypointsPs(*pActivePathPs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::InvalidateCurrentPath()
{
	LocoLogStr("InvalidateCurrentPath()\n");

	m_activePathInfo.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::UpdateMoveDirPs(Vector_arg moveDirPs)
{
	if (IsState(kStateMotionMatching))
	{
		m_sprungMoveDirPs = m_desiredMoveDirPs = AsUnitVectorXz(moveDirPs, m_desiredMoveDirPs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::UpdatePathWaypointsPs(const PathWaypointsEx& pathWaypointsPs,
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

	const bool sameAsCurrent = TestWaypointContractsEqual(waypointContract, m_waypointContract);

	if (!sameAsCurrent)
	{
		m_waypointContract = waypointContract;
	}

	bool shouldWalk = false;
	ForceReachedTypeStopIfNecessary(&m_waypointContract, pathWaypointsPs, m_activeCommand, shouldWalk);
	if (shouldWalk)
	{
		RequestMotionType(kMotionTypeWalk);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PatchWaypointFacingDir(Vector_arg newFacingDir)
{
	const Vector newFacingDirXz = AsUnitVectorXz(newFacingDir, GetLocalZ(m_waypointContract.m_rotPs));

	m_waypointContract.m_rotPs = QuatFromXZDir(newFacingDirXz);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::IsAtEndOfWaypointPath(float padDist /* = 0.0f */) const
{
	const NavCharacter* pNavChar = GetCharacter();

	if (!m_activePathInfo.IsValid())
	{
		return false;
	}

	const Point curPosPs = GetNavigationPosPs();

	const MotionConfig& mc = pNavChar->GetMotionConfig();
	const float goalRadius = Max(m_activeCommand.GetGoalRadius() + padDist, mc.m_minimumGoalRadius);

	// Check if we are too far away from our goal and we need to do some cleanup
	const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs();
	if (pPathPs && (m_activeCommand.GetType() != LocoCommand::kCmdStopLocomoting))
	{
		const U32F waypointCount = pPathPs->GetWaypointCount();
		if (waypointCount >= 2)
		{
			for (int i = 0; i < waypointCount; ++i)
			{
				const Point pointOnPathPs = pPathPs->GetWaypoint(i);
				const float yDelta		  = Abs(pointOnPathPs.Y() - curPosPs.Y());

				if (yDelta > 1.0f)
					return false;

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
// This will not request any further animations. It will just stop
// attempting to move the actor. And will assume we are stopped already.
// USE WITH CAUTION!!!
void AiLocomotionController::Reset()
{
	AssertNotInUseJanitor aniuj(&m_inUseAtomic, FILE_LINE_FUNC);

	// We will be in an undefined motion state until properly setup with a new 'MoveInDirection'
	// We maintain the 'desired' state values even though they won't be used.
	m_motionState.Reset();
	m_desiredMotionState.Reset();
	m_moveStartTime = TimeFrameNegInfinity();

	m_activePathInfo.Reset();

	m_moveToTypeContinueMatchAnim = INVALID_STRING_ID_64;
	m_moveToTypeContinueMatchStartPhase = 0.0f;

	m_inertiaSpeedScaleSpring.Reset();
	m_leanSpring.Reset();

	m_commandStatus = LocoCommand::kStatusAwaitingCommands;

	m_pendingCommand.Reset();
	m_activeCommand.Reset();
	m_previousCommand.Reset();

	ResetLocomotionHistory();

	m_motionMatchingOverride = MmSetOverride();

	LocoLogStr("Reset Locomotion\n");

	if (!IsState(kStateInterrupted))
	{
		GoStopped();
		GetStateMachine().TakeStateTransition();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// This will not request any further animations. It will just stop attempting to move the actor.
// USE WITH CAUTION!!!
void AiLocomotionController::Interrupt()
{
	AssertNotInUseJanitor aniuj(&m_inUseAtomic, FILE_LINE_FUNC);

	m_motionState.Reset();
	m_desiredMotionState.Reset();
	m_moveStartTime = TimeFrameNegInfinity();

	m_activePathInfo.Reset();

	m_moveToTypeContinueMatchAnim = INVALID_STRING_ID_64;
	m_moveToTypeContinueMatchStartPhase = 0.0f;

	m_inertiaSpeedScaleSpring.Reset();
	m_leanSpring.Reset();

	m_commandStatus = LocoCommand::kStatusAwaitingCommands;

	m_pendingCommand.Reset();
	m_activeCommand.Reset();
	m_previousCommand.Reset();

	m_motionMatchingOverride = MmSetOverride();

	LocoLogStr("Interrupt Locomotion\n");

	for (U32F i = 0; i < kMotionTypeMax; ++i)
	{
		m_reqMoveSetSubcat[i] = INVALID_STRING_ID_64;
		m_actMoveSetSubcat[i] = INVALID_STRING_ID_64;
	}

	if (BaseState* pState = GetState())
	{
		pState->OnInterrupted();
	}
	else
	{
		GoInterrupted();
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector AiLocomotionController::GetChosenFaceDirWs() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const Locator& parentSpace = pNavChar->GetParentSpace();
	const Vector chosenFacePs = GetChosenFaceDirPs();
	return parentSpace.TransformVector(chosenFacePs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector AiLocomotionController::GetChosenFaceDirPs() const
{
	Vector retPs = kUnitZAxis;

	if (const BaseState* pCurState = GetState())
	{
		retPs = pCurState->GetChosenFaceDirPs();
	}

	return retPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector AiLocomotionController::GetFacingBaseVectorPs() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const Vector npcForwardPs = GetLocalZ(pNavChar->GetRotationPs());
	const bool moving = IsMoving();
	const bool wantToMove = !m_unplannedStop && ((m_pendingCommand.GetType() == LocoCommand::kCmdStartLocomoting));

	if (!moving && !wantToMove)
	{
		return npcForwardPs;
	}

	return m_sprungMoveDirPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::UpdateAnimActorInfo()
{
	const NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	DC::AnimNpcTopInfo* pTopInfo = pAnimControl ? pAnimControl->TopInfo<DC::AnimNpcTopInfo>() : nullptr;
	if (!pTopInfo)
		return;

	const Vector baseFaceDirPs = GetFacingBaseVectorPs();
	const Vector chosenFaceDirPs = GetChosenFaceDirPs();

	const float moveAngleDegrees = GetDesiredMoveAngleDeg();
	const float faceDiffDegrees = RADIANS_TO_DEGREES(GetRelativeXzAngleDiffRad(baseFaceDirPs, chosenFaceDirPs));

	pTopInfo->m_moveAngle = moveAngleDegrees;
	pTopInfo->m_facingDiff = faceDiffDegrees;
	pTopInfo->m_locomotionAngleDeg = GetXZAngleForVecDeg(m_sprungMoveDirPs);
	pTopInfo->m_transitionSwitch = pNavChar ? !pNavChar->DisableStrafeMoveMoveAnimStateTransitions() : true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiLocomotionController::SetMoveModeOverlay(const MotionType mt)
{
	const DC::NpcMoveSetDef* pMoveSet = (mt < kMotionTypeMax) ? m_pCurMoveSets[mt] : nullptr;
	ApplyMoveSet(pMoveSet);
	return pMoveSet ? pMoveSet->m_overlayName : INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::TryDemeanorChange(const Demeanor oldDemeanor,
											   const Demeanor newDemeanor,
											   bool weaponStateUpToDate)
{
	if (oldDemeanor == newDemeanor)
		return true;

	if (IsState(kStateChangingDemeanor))
	{
		return false;
	}

	const U32F numDemeanors = GetNumDemeanors();
	if (oldDemeanor.ToI32() >= numDemeanors || newDemeanor.ToI32() >= numDemeanors)
	{
		LocoLogStr("TryDemeanorChange: invalid demeanor\n");
		return false;
	}

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	if (!pBaseLayer || pBaseLayer->AreTransitionsPending())
	{
		LocoLogStr("TryDemeanorChange: base layer transitions pending\n");
		return false;
	}

	StringId64 transitionId = SID("demeanor-change");
	const bool hasIdleTransition = pBaseLayer->IsTransitionValid(SID("demeanor-change-idle"));
	if (!pBaseLayer->IsTransitionValid(transitionId) && !hasIdleTransition)
	{
		LocoLogStr("TryDemeanorChange: transition not valid\n");
		return false;
	}

	if (!weaponStateUpToDate)
	{
		const WeaponUpDownState desState = GetDesiredWeaponUpDownState(true);
		ConfigureWeaponUpDownOverlays(desState);
	}

	StringId64 demeanorSid = INVALID_STRING_ID_64;
	SidFromDemeanor(demeanorSid, newDemeanor);
	const StringId64 demeanorChangeAnimId = StringId64Concat(demeanorSid, "-demeanor-change");

	const bool isMoving = IsMoving();

	const ArtItemAnim* pDemeanorChangeAnim = isMoving ? nullptr : pAnimControl->LookupAnim(demeanorChangeAnimId).ToArtItem();

	// Switch demeanor!
	m_changingDemeanor = true;
	pNavChar->ForceDemeanor(newDemeanor, AI_LOG);
	m_changingDemeanor = false;

	LocoLog("TryDemeanorChange: new demeanor %s\n", pNavChar->GetDemeanorName(newDemeanor));

	if (isMoving)
	{
		LocoLogStr("Re-applying motion type restrictions\n");

		// re-apply motion type restrictions for the new demeanor
		m_desiredMotionState = ApplyMotionTypeRestriction(m_desiredMotionState);

		// set move mode overlays to match new motion type
		SetMoveModeOverlay(m_desiredMotionState.m_motionType);

		m_motionState = m_desiredMotionState;
		m_motionState.m_moving = true;
	}
	else if (pDemeanorChangeAnim)
	{
		AI::SetPluggableAnim(pNavChar, pDemeanorChangeAnim->GetNameId());

		if (pBaseLayer->IsTransitionValid(SID("demeanor-change-anim")))
		{
			transitionId = SID("demeanor-change-anim");
		}
	}
	else if (hasIdleTransition)
	{
		transitionId = SID("demeanor-change-idle");
	}

	if (pBaseLayer->IsTransitionValid(transitionId))
	{
		GoChangingDemeanor(transitionId);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiLocomotionController::WeaponUpDownState AiLocomotionController::GetDesiredWeaponUpDownState(bool apCheck) const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AnimationControllers* pAnimControllers = pNavChar->GetAnimationControllers();
	const IAiWeaponController* pWeaponController = pAnimControllers->GetWeaponController();

	if (!pWeaponController || !m_hasWeaponUpDownOverlays)
	{
		return kNoWeapon;
	}

	const bool weaponInHand = (pWeaponController->GetCurrentGunState() == kGunStateOut) && pNavChar->HasFirearmInHand();

	if (!weaponInHand)
	{
		return kNoWeapon;
	}

	const ActionPack* pEnteredAp = pNavChar->GetEnteredActionPack();
	const ActionPackController* pApController = pEnteredAp ? pAnimControllers->GetControllerForActionPackType(pEnteredAp->GetType()) : nullptr;
	const bool apOverridesWeaponUp = (apCheck && pApController) ? pApController->OverrideWeaponUp() : false;
	const bool weaponUpRequested = pWeaponController->IsWeaponUpRequested() || apOverridesWeaponUp;

	if (weaponUpRequested)
	{
		return kWeaponUp;
	}
	else
	{
		return kWeaponDown;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::ConfigureWeaponUpDownOverlays(WeaponUpDownState desState)
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimOverlays* pOverlays = pAnimControl ? pAnimControl->GetAnimOverlays() : nullptr;

	if (!pOverlays || !m_hasWeaponUpDownOverlays)
		return false;

	const char* baseName = pNavChar->GetOverlayBaseName();
	const StringId64 weaponUpSetId = AI::CreateNpcAnimOverlayName(baseName, "weapon-up-down", "up");
	const StringId64 weaponDownSetId = AI::CreateNpcAnimOverlayName(baseName, "weapon-up-down", "down");
	const StringId64 noWeaponSetId = AI::CreateNpcAnimOverlayName(baseName, "weapon-up-down", "no-weapon");

	float target = 0.0f;

	switch (desState)
	{
	case kNoWeapon:
		if (noWeaponSetId != INVALID_STRING_ID_64)
		{
			pOverlays->SetOverlaySet(noWeaponSetId);
		}
		break;

	case kWeaponUp:
		target = 1.0f;
		if (weaponUpSetId != INVALID_STRING_ID_64)
		{
			pOverlays->SetOverlaySet(weaponUpSetId);
		}
		break;

	case kWeaponDown:
		if (weaponDownSetId != INVALID_STRING_ID_64)
		{
			pOverlays->SetOverlaySet(weaponDownSetId);
		}
		break;
	}

	//LocoLog("TryWeaponStateUpdate: Weapon is %s\n", GetWeaponUpDownStateName(desState));

	if (IAiWeaponController* pWeaponController = pNavChar->GetAnimationControllers()->GetWeaponController())
	{
		pWeaponController->UpdateWeaponUpDownPercent(0.0f, target);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::IsPlayingDemeanorChangeAnim() const
{
	return IsState(kStateChangingDemeanor);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::TryWeaponStateUpdate()
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	if (!pBaseLayer || pBaseLayer->AreTransitionsPending())
		return false;

	IAiWeaponController* pWeaponController = pNavChar->GetAnimationControllers()->GetWeaponController();
	if (!pWeaponController)
		return true;

	const WeaponUpDownState desState = GetDesiredWeaponUpDownState(true);

	const StringId64 transitionId = SID("demeanor-change");
	const StringId64 idleTransitionId = SID("demeanor-change-idle");

	// Now trigger the transitional animation
	if (pNavChar->GetEnteredActionPack())
	{
		if (!ConfigureWeaponUpDownOverlays(desState))
			return false;

		return true;
	}
	else if (IsState(kStateMotionMatching))
	{
		if (!ConfigureWeaponUpDownOverlays(desState))
			return false;

		if (NdSubsystemAnimController* pSubSysController = pNavChar->GetActiveSubsystemController())
		{
			pSubSysController->RequestRefreshAnimState();
		}
		else
		{
			GoMotionMatching();
		}

		return true;
	}
	else if (pBaseLayer->IsTransitionValid(transitionId))
	{
		if (!ConfigureWeaponUpDownOverlays(desState))
			return false;

		GoChangingWeaponUpDown(transitionId);

		return true;
	}
	else if (pBaseLayer->IsTransitionValid(idleTransitionId))
	{
		if (!ConfigureWeaponUpDownOverlays(desState))
			return false;

		GoChangingWeaponUpDown(idleTransitionId);

		return true;
	}
	else if (pNavChar->IsInScriptedAnimationState())
	{
		if (!ConfigureWeaponUpDownOverlays(desState))
			return false;

		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::IsWeaponStateUpToDate() const
{
	if (!m_hasWeaponUpDownOverlays)
		return true;

	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const AnimOverlays* pOverlays = pAnimControl ? pAnimControl->GetAnimOverlays() : nullptr;

	if (!pOverlays)
		return true;

	const AnimationControllers* pAnimControllers = pNavChar->GetAnimationControllers();
	const IAiWeaponController* pWeaponController = pAnimControllers->GetWeaponController();

	if (pWeaponController && pWeaponController->IsDoingWeaponSwitch())
	{
		return true;
	}

	const AnimOverlaySnapshotHash curHash = pOverlays->GetHash();

/*	if (m_lastWeaponOverlayHash == curHash)
		return m_lastWeaponUpToDate;*/

	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	if (pBaseLayer && !pBaseLayer->HasFreeInstance())
		return true;

	const char* baseName = pNavChar->GetOverlayBaseName();
	const StringId64 weaponUpSetId	 = AI::CreateNpcAnimOverlayName(baseName, "weapon-up-down", "up");
	const StringId64 weaponDownSetId = AI::CreateNpcAnimOverlayName(baseName, "weapon-up-down", "down");
	const StringId64 noWeaponSetId	 = AI::CreateNpcAnimOverlayName(baseName, "weapon-up-down", "no-weapon");

	const WeaponUpDownState desState = GetDesiredWeaponUpDownState(true);

	bool upToDate = true;

	switch (desState)
	{
	case kNoWeapon:
		upToDate = pOverlays->IsOverlaySet(noWeaponSetId);
		break;

	case kWeaponUp:
		upToDate = pOverlays->IsOverlaySet(weaponUpSetId);
		break;

	case kWeaponDown:
		upToDate = pOverlays->IsOverlaySet(weaponDownSetId);
		break;
	}

	m_lastWeaponOverlayHash = curHash;
	m_lastWeaponUpToDate = upToDate;

	return upToDate;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiLocomotionController::GetUpcomingDesiredDemeanor() const
{
	const NavCharacter* const pNavChar = GetCharacter();

	const BaseState* const pState = GetState();
	if (!pState)
		return INVALID_STRING_ID_64;

	const float maxSpeed = pState->GetMaxMovementSpeed();
	if (maxSpeed <= 0.0f)
		return INVALID_STRING_ID_64;

	if (!IsMoving())
		return INVALID_STRING_ID_64;

	const float curSpeed = Min(Length(pNavChar->GetVelocityWs()), maxSpeed);
	const float testSpeed = Lerp(curSpeed, maxSpeed, 0.82f);

	CONST_EXPR float kTimeAhead = 0.52f;
	const float distAhead = testSpeed * kTimeAhead;

	NavMesh::ProbeParams params;
	params.m_probeRadius = 0.0f;
	params.m_crossLinks = true;
	params.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;

	auto f = [](const NavPoly* pPoly, const NavPolyEx*, uintptr_t userData)
	{
		NavMesh::NpcStature& minStature = *(NavMesh::NpcStature*)userData;
		minStature = (NavMesh::NpcStature)Min((U8)minStature, (U8)pPoly->GetNavMesh()->GetNpcStature());

		//pPoly->DebugDrawEdges(kColorOrange, kColorPink, 0.05f);
	};

	NavMesh::NpcStature minStature = NavMesh::NpcStature::kStand;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const bool reachedTypeContinue = m_waypointContract.m_reachedType == kNavGoalReachedTypeContinue;
	if (reachedTypeContinue && m_commandStatus == LocoCommand::kStatusCommandSucceeded)
	{
		const NavLocation& navLoc = pNavChar->GetNavLocation();
		const Point startPs = navLoc.GetPosPs();
		const NavMesh* pStartMesh;
		const NavPoly* pStartPoly = navLoc.ToNavPoly(&pStartMesh);
		if (pStartPoly)
		{
			NAV_ASSERT(pStartMesh);
			params.m_start = params.m_endPoint = startPs;
			params.m_move = distAhead * m_desiredMoveDirPs;
			params.m_pStartPoly = pStartPoly;
			pStartMesh->WalkPolysInLinePs(params, f, (uintptr_t)&minStature);
		}
	}
	else
	{
		const PathWaypointsEx* pPathPs = pNavChar->GetPathPs();

		if (!pPathPs || !pPathPs->IsValid())
			pPathPs = m_activePathInfo.GetPathWaypointsPs();

		if (!pPathPs || !pPathPs->IsValid())
			return INVALID_STRING_ID_64;

		WalkPath(params, pPathPs, distAhead, f, (uintptr_t)&minStature, false);
	}

	if (minStature == NavMesh::NpcStature::kProne)
	{
		//g_prim.Draw(DebugString(pNavChar->GetTranslation() + Vector(0.0f, 1.0f, 0.0f), "PRONE", 1));
		return SID("crawl");
	}

	if (minStature == NavMesh::NpcStature::kCrouch)
	{
		//g_prim.Draw(DebugString(pNavChar->GetTranslation() + Vector(0.0f, 1.0f, 0.0f), "CROUCH", 1));
		return SID("crouch");
	}

	//g_prim.Draw(DebugString(pNavChar->GetTranslation() + Vector(0.0f, 1.0f, 0.0f), "STAND", 1));
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Demeanor AiLocomotionController::GetDesiredDemeanorInternal() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const NavControl* pNavCon = GetNavControl();

	const StringId64 curDemId = pNavCon->NavMeshForcedDemeanor();
	const StringId64 upcomingDemId = GetUpcomingDesiredDemeanor();

	StringId64 forcedDemId = INVALID_STRING_ID_64;

	if (curDemId == SID("crawl") || upcomingDemId == SID("crawl"))
	{
		forcedDemId = SID("crawl");
	}
	else if (curDemId == SID("crouch") || upcomingDemId == SID("crouch"))
	{
		forcedDemId = SID("crouch");
	}
	else
	{
		forcedDemId = curDemId;
	}

	Demeanor forcedDem;
	if (DemeanorFromSid(forcedDem, forcedDemId) && pNavChar->HasDemeanor(forcedDem.ToI32()))
	{
		return forcedDem;
	}

	return m_desiredDemeanor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::BypassDemeanor(Demeanor demeanor)
{
	NavCharacter* pNavChar = GetCharacter();
	m_changingDemeanor = true;
	pNavChar->ForceDemeanor(demeanor, AI_LOG);
	m_changingDemeanor = false;
	LocoLog("loco: bypassing demeanor to %s\n", pNavChar->GetDemeanorName(demeanor));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::SelectNewAction()
{
	PROFILE(Animation, SelectNewAction);

	NavCharacter* pNavChar = GetCharacter();

	// Check to see if we need to make any changes.

	const Demeanor curDemeanor = pNavChar->GetCurrentDemeanor();
	const Demeanor desiredDemeanor = GetDesiredDemeanorInternal();
	const bool demeanorUpToDate = (curDemeanor == desiredDemeanor);

	const bool weaponStateUpToDate = IsWeaponStateUpToDate();

	const StringId64 desSetId = GetMotionMatchingSetForState(m_motionState);
	const bool orientationSatisfied = IsDesiredOrientationSatisfied(kMinTurnInPlaceAngleDeg);
	const bool needToStartMoving = (IsState(kStateStopped) && m_desiredMotionState.m_moving) || !orientationSatisfied;

	IAiWeaponController* pWeaponController = pNavChar->GetAnimationControllers()->GetWeaponController();
	const bool primaryUpToDate = pWeaponController ? pWeaponController->IsPrimaryWeaponUpToDate() : true;

	if (primaryUpToDate && !demeanorUpToDate && TryDemeanorChange(curDemeanor, desiredDemeanor, weaponStateUpToDate))
	{
		LocoLog("Demeanor change successful from '%s' -> '%s'\n",
				pNavChar->GetDemeanorName(curDemeanor),
				pNavChar->GetDemeanorName(desiredDemeanor));
	}
	else if (primaryUpToDate && !weaponStateUpToDate && TryWeaponStateUpdate())
	{
		LocoLogStr("Weapon state update successful\n");
	}
	else if (needToStartMoving && !IsState(kStateMotionMatching) && desSetId != INVALID_STRING_ID_64)
	{
		LocoLog("Starting motion matching (set: %s) (des moving: %s) (orientation satisfied: %s)\n",
				DevKitOnly_StringIdToString(desSetId),
				m_desiredMotionState.m_moving ? "true" : "false",
				orientationSatisfied ? "true" : "false");

		GoMotionMatching();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::AddMovePerformanceTable(StringId64 entryId, StringId64 tableId)
{
	return m_movePerformance.AddTable(GetCharacter(), entryId, tableId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::RemoveMovePerformanceTable(StringId64 entryId)
{
	return m_movePerformance.RemoveTable(GetCharacter(), entryId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::HasMovePerformanceTable(StringId64 entryId) const
{
	return m_movePerformance.HasTable(GetCharacter(), entryId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionState AiLocomotionController::ApplyMotionTypeRestriction(const MotionState& inputState) const
{
	MotionState returnState = inputState;

	if (returnState.m_motionType < kMotionTypeMax || returnState.m_moving)
	{
		returnState.m_motionType = RestrictMotionType(inputState.m_motionType, inputState.m_strafing);
	}
	else
	{
		returnState.m_motionType = kMotionTypeMax;
		returnState.m_moving	 = false;
	}

	returnState.m_strafing = inputState.m_strafing && CanStrafe(returnState.m_motionType);

	return returnState;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::IsDesiredOrientationSatisfied(const float toleranceDeg /* = kMinTurnInPlaceAngleDeg */) const
{
	if (m_commandStatus != LocoCommand::kStatusCommandInProgress)
		return true;

	// Are we oriented properly?
	if (m_waypointContract.m_reachedType != kNavGoalReachedTypeStop)
		return true;

	Vector desiredFaceDirPs = GetChosenFaceDirPs();

	if (m_activePathInfo.IsValid() && !m_unplannedStop)
	{
		desiredFaceDirPs = GetLocalZ(m_waypointContract.m_rotPs);
	}

	const NavCharacter* pNavChar = GetCharacter();
	const Quat curRotPs = pNavChar->GetRotationPs();
	const Vector npcForwardPs = GetLocalZ(curRotPs);

	desiredFaceDirPs = AsUnitVectorXz(desiredFaceDirPs, npcForwardPs);

	const float cosValue = DotXz(npcForwardPs, desiredFaceDirPs);
	const float curAngleRad = SafeAcos(cosValue);
	const float curAngleDeg = RADIANS_TO_DEGREES(curAngleRad);

	if (Abs(curAngleDeg) >= toleranceDeg)
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// This controller will do it's best to accommodate the AI. The AI will request certain animation states such as
// COMBAT/AMBIENT, Walk/Run and Strafe/Normal movement. The controller will migrate over to the desired values as
// fast as it can making sure that the animation system have transitions for it. During this time the controller
// will be 'busy.'
void AiLocomotionController::RequestAnimations()
{
	PROFILE(AI, LocoCtrl_RequestAnimations);

	MarkInUseJanitor miuj(&m_inUseAtomic, FILE_LINE_FUNC);

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	LocomotionControllerFsm& fsm = GetStateMachine();

	if (pNavChar->IsDead())
	{
		return;
	}

	if (m_activePathInfo.IsValid() && m_activeCommand.IsValid())
	{
		AdjustPathEndPointPs(&m_activePathInfo, m_waypointContract, m_activeCommand.GetGoalRadius());
	}

	m_movePerformance.TryRebuildCache(pNavChar);

	// if pending command exists and we are in a suitable state,
	if (m_commandStatus == LocoCommand::kStatusCommandPending && CanProcessCommand())
	{
		PROFILE(AI, LocoCtrl_ProcessCommand);
		ProcessCommand();

		fsm.TakeStateTransition();
	}

	// Is the character done blending animations?
	//if (!pBaseLayer->AreTransitionsPending())
	{
		if (BaseState* pState = GetState())
		{
			PROFILE(AI, StateRequestAnimations);
			pState->RequestAnimations();
		}

		fsm.TakeStateTransition();
	}

	// We need to update the actor info after any potential changes so that the animation system gets the up to date data.
	if (!pNavChar->IsNavigationInterrupted())
	{
		PROFILE(AI, LocoCtrl_UpdateAnimActorInfo);
		UpdateAnimActorInfo();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::UpdateStatus()
{
	PROFILE(AI, LocoCtrl_UpdateStatus);

	MarkInUseJanitor miuj(&m_inUseAtomic, FILE_LINE_FUNC);

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	if (pNavChar->IsDead())
	{
		return;
	}

	UpdateWeaponUpDownTracking();

	// Update our state based on the state and status of our current action
	if (BaseState* pState = GetState())
	{
		pState->UpdateStatus();
	}

	GetStateMachine().TakeStateTransition();

	ICharacterLocomotion::LocomotionState locoState = ICharacterLocomotion::CreateLocomotionState(pNavChar);

	if (m_locomotionHistory.IsFull())
	{
		m_locomotionHistory.Drop(1);
	}

	m_locomotionHistory.Enqueue(locoState);

	if (const PathWaypointsEx* pActivePathPs = m_activePathInfo.GetPathWaypointsPs())
	{
		// Calculate the remaining path length
		const Point navPosPs = GetNavigationPosPs();

		const float totalDist = pActivePathPs->ComputePathLengthXz();

		float travelDist = 0.0f;
		pActivePathPs->ClosestPointXz(navPosPs, &travelDist);

		m_remainingPathDistance = totalDist - travelDist;
	}
	else if (m_remainingPathDistance >= 0.0f)
	{
		m_remainingPathDistance = -1.0f;
	}

	m_suppressRetargetScale = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::UpdateWeaponUpDownTracking()
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	IAiWeaponController* pWeaponController = pNavChar->GetAnimationControllers()->GetWeaponController();

	if (!pWeaponController)
		return;

	const AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays();
	const char* baseName = pNavChar->GetOverlayBaseName();
	const StringId64 weaponUpSetId = AI::CreateNpcAnimOverlayName(baseName, "weapon-up-down", "up");

	float target = 1.0f;
	if (!pOverlays->IsOverlaySet(weaponUpSetId))
	{
		target = 0.0f;
	}

	const BaseState* pCurState = GetState();
	const float weaponChangeFade = pCurState->GetWeaponChangeFade();

	pWeaponController->UpdateWeaponUpDownPercent(weaponChangeFade, target);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PostRootLocatorUpdate()
{
	PROFILE(AI, PostRootLocatorUpdate);

	MarkInUseJanitor miuj(&m_inUseAtomic, FILE_LINE_FUNC);

	const NavCharacter* pNavChar = GetCharacter();
	const Point navPosPs = GetNavigationPosPs();
	const Point posPs = pNavChar->GetTranslationPs();
	const Point lastPosPs = pNavChar->GetLastTranslationPs();
	const AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

	const U32F numWaypoints = m_activePathInfo.GetWaypointCount();

	PathWaypointsEx* pActivePathPs = m_activePathInfo.GetPathWaypointsPs();
	if (pActivePathPs)
	{
		pActivePathPs->UpdateWaypoint(0, posPs);
	}

	const bool isDoingMovingHandoff = IsState(kStateHandoff) && m_motionState.m_moving;
	const bool canTestForStop = isDoingMovingHandoff || IsState(kStateMotionMatching);

	if (canTestForStop && m_activePathInfo.IsValid() && (numWaypoints >= 2) && pActivePathPs)
	{
		// Kick move performance contact rays after loc is known so ray results will be valid next frame
		m_movePerformance.KickContactRayCasts(pNavChar, m_sprungMoveDirPs);

		const Point lastPointOnPathPs	= pActivePathPs->GetEndWaypoint();
		const Vector newToGoalPs		= lastPointOnPathPs - posPs;

		const Vector velPs				= pNavChar->GetVelocityPs();
		const bool numWaypointsCorrect	= numWaypoints == 2;

		if ((m_waypointContract.m_reachedType == kNavGoalReachedTypeContinue) && (numWaypoints == 2))
		{
			const F32 kEpsilon = 0.05f;
			const float goalRadius = Max(m_activeCommand.GetGoalRadius(), pNavChar->GetMotionConfig().m_minimumGoalRadius);

			// Adjust the arrival threshold based on velocity
			const Vector moveVecPs			= AsUnitVectorXz(posPs - lastPosPs, kZero);

			if (!m_everMovedTowardsGoal && DotXz(moveVecPs, newToGoalPs) > 0.0f)
			{
				m_everMovedTowardsGoal = true;
			}

			const Vector prevToGoalPs		= lastPointOnPathPs - lastPosPs;
			const float dotP1				= DotXz(prevToGoalPs, moveVecPs);
			const float dotP2				= DotXz(newToGoalPs, moveVecPs);
			const bool passedThisFrame		= (dotP1 > 0.0f) && (dotP2 < kEpsilon);
			const bool alreadyPassed		= (dotP1 < 0.0f) && (dotP2 < kEpsilon) && m_everMovedTowardsGoal;
			const bool passedOurDestPoint	= (passedThisFrame || alreadyPassed) && (m_remainingPathDistance < goalRadius);

			if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_locomotionController.m_displayDetails && DebugSelection::Get().IsProcessOrNoneSelected(pNavChar)))
			{
				MsgCon("[%s] [dots: %s%f%s / %s%f%s] passedThisFrame: %s%s%s alreadyPassed: %s%s%s passedOurDestPoint: %s%s%s pathDist: %s%0.3fm%s / %0.3fm\n",
					   pNavChar->GetName(),
					   GetTextColorString(dotP1 > 0.0f ? kTextColorGreen : kTextColorRed),
					   dotP1,
					   GetTextColorString(kTextColorNormal),
					   GetTextColorString(dotP2 < kEpsilon ? kTextColorGreen : kTextColorRed),
					   dotP2,
					   GetTextColorString(kTextColorNormal),
					   GetTextColorString(passedThisFrame ? kTextColorGreen : kTextColorRed),
					   passedThisFrame ? "true" : "false",
					   GetTextColorString(kTextColorNormal),
					   GetTextColorString(alreadyPassed ? kTextColorGreen : kTextColorRed),
					   alreadyPassed ? "true" : "false",
					   GetTextColorString(kTextColorNormal),
					   GetTextColorString(passedOurDestPoint ? kTextColorGreen : kTextColorRed),
					   passedOurDestPoint ? "true" : "false",
					   GetTextColorString(kTextColorNormal),
					   GetTextColorString(m_remainingPathDistance < goalRadius ? kTextColorGreen : kTextColorRed),
					   m_remainingPathDistance,
					   GetTextColorString(kTextColorNormal),
					   goalRadius);
			}

			if (passedOurDestPoint)
			{
				LocoLogStr("Completing move to type continue: (passed our dest point)\n");

				// if we have a command in progress,
				if (m_commandStatus == LocoCommand::kStatusCommandInProgress)
				{
					// succeeded
					m_moveToTypeContinueMatchAnim = INVALID_STRING_ID_64;
					m_moveToTypeContinueMatchStartPhase = 0.0f;
					CompleteCommand(LocoCommand::kStatusCommandSucceeded);
					LocoLogStr("SUCCEEDED - PostRootLocatorUpdate()\n");
				}

				if (!m_motionState.m_moving)
				{
					LocoLogStr("Finished reached type continue while MM, forcing moving flag to true\n");
					m_motionState.m_moving = true;
				}

				m_activePathInfo.Reset();

				if (HasNewState())
				{
					LocoLogStr("Removing request for new state so we can go to MotionMatching\n");
					GetStateMachine().RemoveNewState();
				}

				if (!IsState(kStateMotionMatching) && !isDoingMovingHandoff)
				{
					GoMotionMatching();
				}
			}
		}
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::IsBusy() const
{
	bool busy = true;

	if (const BaseState* pCurState = GetState())
	{
		busy = pCurState->IsBusy();
	}

	return busy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::ShouldDoProceduralApRef(const AnimStateInstance* pInstance) const
{
	if (m_unplannedStop)
	{
		return false;
	}

	if (pInstance && pInstance->HasSavedAlign())
	{
		return false;
	}

	bool shouldDo = false;

	if (const BaseState* pCurState = GetState())
	{
		shouldDo = pCurState->ShouldDoProceduralApRef(pInstance);
	}

	return shouldDo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::CalculateProceduralAlign(const AnimStateInstance* pInstance,
													  const Locator& baseAlignPs,
													  Locator& alignOut) const
{
	bool shouldDo = true;
	if (const BaseState* pCurState = GetState())
	{
		shouldDo = pCurState->ShouldDoProceduralAlign(pInstance);
	}

	if (!shouldDo)
		return false;

	const NavCharacter* pNavChar = GetCharacter();

	if (!pInstance || !pNavChar)
		return false;

	const DC::AnimNpcTopInfo* pTopInfo = (const DC::AnimNpcTopInfo*)pInstance->GetAnimTopInfo();
	if (!pTopInfo)
		return false;

	const bool moving = IsMoving();
	const DC::AnimStateFlag stateFlags = pInstance->GetStateFlags();
	const bool noProcRot = (stateFlags & DC::kAnimStateFlagNoProcRot);
	const bool apMoveUpdate = (stateFlags & DC::kAnimStateFlagApMoveUpdate);
	const bool animOnlyMoveUpdate = (stateFlags & DC::kAnimStateFlagAnimOnlyMoveUpdate);
	const bool ffaMoveUpdate = (stateFlags & DC::kAnimStateFlagFirstAlignRefMoveUpdate);
	const bool hasSavedAlign = pInstance->HasSavedAlign();
	const bool motionMatching = IsState(kStateMotionMatching);
	const bool applyProcRotation  = moving && !noProcRot && !apMoveUpdate && !animOnlyMoveUpdate && !ffaMoveUpdate
								   && !hasSavedAlign && !motionMatching;

	// no locomotion procedural align needed
	if (!applyProcRotation)
		return false;

	Vector animAlignMoveLs = kZero;

	// get the translation delta from the align or the path override if it exists
	if (pInstance->HasChannel(SID("apReference-path")))
	{
		const Locator animAlignDelta = pInstance->GetChannelDelta(SID("align"));
		const Locator pathDeltaLs = pInstance->GetChannelDelta(SID("apReference-path"));
		animAlignMoveLs = (animAlignDelta.Pos() - kOrigin) + (pathDeltaLs.Pos() - kOrigin);
	}
	else
	{
		const Locator animAlignDelta = pInstance->GetChannelDelta(SID("align"));
		animAlignMoveLs = animAlignDelta.Pos() - kOrigin;
	}

	const Vector procMoveDirPs = GetVectorForXZAngleDeg(pTopInfo->m_locomotionAngleDeg);

	const Vector procAlignMoveDirLs = baseAlignPs.UntransformVector(procMoveDirPs);
	const Vector animAlignMoveDirLs = SafeNormalize(animAlignMoveLs, procAlignMoveDirLs);

	const Quat baseRotAdjustLs = QuatFromVectors(animAlignMoveDirLs, procAlignMoveDirLs);

#if 1
	const float dt = pNavChar->GetClock()->GetDeltaTimeInSeconds();
	const float baseFilter = kLocomotionProcRotFilter;
	const float filterFactor = Limit01(dt * baseFilter);
	const Quat filteredRotAdjustLs = Slerp(kIdentity, baseRotAdjustLs, filterFactor);
	const Quat filteredRotPs = baseAlignPs.Rot() * filteredRotAdjustLs;
#else
	const Quat filteredRotPs = baseAlignPs.Rot() * baseRotAdjustLs;
#endif

	const Locator procBaseAlignPs = Locator(baseAlignPs.Pos(), filteredRotPs);
	const Locator animAlignDelta = baseAlignPs.UntransformLocator(alignOut);
	const Locator procAlignLoc = procBaseAlignPs.TransformLocator(animAlignDelta);

	alignOut = procAlignLoc;

	if (false)
	{
		g_prim.Draw(DebugArrow(baseAlignPs.Pos(),
							   baseAlignPs.TransformVector(animAlignMoveDirLs),
							   pInstance->HasChannel(SID("apReference-path")) ? kColorBlue : kColorOrange));
		g_prim.Draw(DebugArrow(baseAlignPs.Pos(), baseAlignPs.TransformVector(procAlignMoveDirLs), kColorCyan));

		g_prim.Draw(DebugCoordAxes(baseAlignPs, 0.5f, PrimAttrib(), 2.0f));
		g_prim.Draw(DebugCoordAxes(alignOut, 0.25f, PrimAttrib(), 4.0f));
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct ProceduralApUpdateData
{
	Locator m_orgAlignPs;
	Locator m_rotAlignPs;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static void UpdateProceduralAp(AnimStateInstance* pInstance, NdSubsystemAnimAction* pAction, uintptr_t userData)
{
	if (!pAction || !pAction->IsKindOf(g_type_SelfBlendAction))
		return;

	SelfBlendAction* pSelfBlend = (SelfBlendAction*)pAction;

	const ProceduralApUpdateData& data = *(const ProceduralApUpdateData*)userData;

	const BoundFrame& srcAp = pSelfBlend->GetSourceApRef();
	const BoundFrame& dstAp = pSelfBlend->GetDestApRef();

	const Locator& srcApPs = srcAp.GetLocatorPs();
	const Locator& dstApPs = dstAp.GetLocatorPs();

	const Locator srcApAs = data.m_orgAlignPs.UntransformLocator(srcApPs);
	const Locator dstApAs = data.m_orgAlignPs.UntransformLocator(dstApPs);

	const Locator newSrcApPs = data.m_rotAlignPs.TransformLocator(srcApAs);
	const Locator newDstApPs = data.m_rotAlignPs.TransformLocator(dstApAs);

	BoundFrame newSrcAp = srcAp;
	BoundFrame newDstAp = dstAp;

	newSrcAp.SetLocatorPs(newSrcApPs);
	newDstAp.SetLocatorPs(newDstApPs);

	pSelfBlend->UpdateSourceApRef(newSrcAp);
	pSelfBlend->UpdateDestApRef(newDstAp);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::CalculateProceduralApRef(AnimStateInstance* pInstance) const
{
	if (!pInstance)
		return false;

	const DC::AnimNpcTopInfo* pTopInfo = (const DC::AnimNpcTopInfo*)pInstance->GetAnimTopInfo();
	if (!pTopInfo)
		return false;

	if (!ShouldDoProceduralApRef(pInstance))
		return false;

	const NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
		return false;

	const float dt = pNavChar->GetClock()->GetDeltaTimeInSeconds();

	// side step the drifting problem for now
	if (dt <= 0.0f)
		return false;

	if (m_unplannedStop)
		return false;

	// figure out which direction our ap reference points us and back calculate a new ap ref
	// rotated about the align to point us in the desired procedural direction

	const BoundFrame orgApRef = pInstance->GetApLocator();

	const Locator apRefPs = orgApRef.GetLocatorPs();
	const Locator& alignPs = pNavChar->GetLocatorPs();
	const Locator apRefInAlignSpace = alignPs.UntransformLocator(apRefPs);

	Vector animAlignMoveLs = kZero;

	// get the translation delta from the align or the path override if it exists
	if (pInstance->HasChannel(SID("apReference-path")))
	{
		const Locator animAlignDeltaLs = pInstance->GetChannelDelta(SID("apReference-path"));
		animAlignMoveLs = animAlignDeltaLs.Pos() - kOrigin;
	}
	else
	{
		const Locator alignDeltaLs = pInstance->GetChannelDelta(SID("align"));
		animAlignMoveLs = alignDeltaLs.Pos() - kOrigin;
	}

	const Vector procMoveDirPs = GetVectorForXZAngleDeg(pTopInfo->m_locomotionAngleDeg);
	const Vector animAlignMoveDirPs = AsUnitVectorXz(alignPs.TransformVector(animAlignMoveLs), procMoveDirPs);

	BoundFrame procApRef = orgApRef;

	const Quat baseRotAdjust = QuatFromVectors(animAlignMoveDirPs, procMoveDirPs);

	const float baseFilter = kLocomotionProcRotFilter;
	const float filterFactor = Limit01(dt * baseFilter);
	const Quat filteredRotAdjust = Slerp(kIdentity, baseRotAdjust, filterFactor);
	const Quat filteredRotPs = Normalize(filteredRotAdjust * alignPs.Rot());

	const Locator procAlignPs = Locator(alignPs.Pos(), filteredRotPs);
	const Locator procApRefPs = procAlignPs.TransformLocator(apRefInAlignSpace);

	procApRef.SetLocatorPs(procApRefPs);
	procApRef.SetRotation(Normalize(procApRef.GetRotation()));

	AI_ASSERT(IsFinite(procApRef));
	AI_ASSERT(IsNormal(procApRef.GetRotationPs()));
	pInstance->SetApLocator(procApRef);

	NdGameObject* pGo = GetCharacterGameObject();
	NdSubsystemMgr* pSubSysMgr = pGo ? pGo->GetSubsystemMgr() : nullptr;

	if (pSubSysMgr)
	{
		ProceduralApUpdateData data;
		data.m_orgAlignPs = alignPs;
		data.m_rotAlignPs = procAlignPs;
		pSubSysMgr->ForEachInstanceAction(pInstance, UpdateProceduralAp, (uintptr_t)&data);
	}

	if (false)
	{
		PrimServerWrapper ps(pNavChar->GetParentSpace());
		ps.EnableHiddenLineAlpha();

		MsgCon("rot amount: %0.1f%%\n", filterFactor * 100.0f);
		MsgCon("procAp delta: %0.4fm\n", float(Dist(procApRef.GetTranslationWs(), orgApRef.GetTranslationWs())));
		MsgCon("procAp rot delta: %0.4fdeg\n", float(RADIANS_TO_DEGREES(SafeAcos(Dot(GetLocalZ(alignPs), GetLocalZ(filteredRotPs))))));
		MsgCon("vec angle delta: %0.4fdeg\n", float(RADIANS_TO_DEGREES(SafeAcos(Dot(animAlignMoveDirPs, procMoveDirPs)))));

		ps.DrawCoordAxes(alignPs);
		//ps.DrawString(alignPs.Pos(), "alignPs");
		ps.DrawArrow(alignPs.Pos() + Vector(0.0f, 0.1f, 0.0f), animAlignMoveDirPs, 0.5f, kColorCyan);
		ps.DrawArrow(alignPs.Pos() + Vector(0.0f, 0.3f, 0.0f), procMoveDirPs, 0.5f, kColorMagenta);

		//g_prim.Draw(DebugCoordAxesLabeled(apRef.GetLocatorWs(), "instanceApRef"));
		//g_prim.Draw(DebugLine(apRef.GetTranslationWs(), procApRef.GetTranslationWs(), kColorWhiteTrans, kColorGreenTrans), Seconds(3.0f));
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionType AiLocomotionController::RestrictMotionType(const MotionType desiredMotionType, bool wantToStrafe) const
{
	const NavCharacter* pNavChar = GetCharacter();
	const NavControl* pNavControl = GetNavControl();

	if (pNavControl->NavMeshForcesWalk())
	{
		if (desiredMotionType != kMotionTypeWalk)
		{
			LocoLog("Motion type '%s' restricted to walk because NavMesh forces it\n", GetMotionTypeName(desiredMotionType));
		}
		return kMotionTypeWalk;
	}

	const U64 validBits = Nav::GetValidMotionTypes(wantToStrafe, m_pCurMoveSets);

	if (validBits == 0ULL)
	{
		LocoLog("No valid motion states found trying to restrict motion type '%s'\n", GetMotionTypeName(desiredMotionType));
		return kMotionTypeMax;
	}

	const MotionType restrictedMotionType = Nav::GetRestrictedMotionTypeFromValid(GetCharacter(),
																				  desiredMotionType,
																				  wantToStrafe,
																				  validBits);
	AI_ASSERT(m_pCurMoveSets[restrictedMotionType]);

	if (desiredMotionType != restrictedMotionType)
	{
		LocoLog("Motion type '%s' restricted to '%s'\n",
				GetMotionTypeName(desiredMotionType),
				GetMotionTypeName(restrictedMotionType));
	}

	return restrictedMotionType;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector AiLocomotionController::GetMotionDirPs() const
{
	if (!IsMoving())
		return kZero;

	return m_sprungMoveDirPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ReadLocAndVelocityCached(const SkeletonId skelId,
									 const ArtItemAnim* pMatchAnim,
									 StringId64 channelId,
									 float phase,
									 Locator* pLocOut,
									 Vector* pVelOut)
{
	if (!pMatchAnim || !pLocOut || !pVelOut)
		return false;

	const float phaseDelta = pMatchAnim->m_pClipData->m_phasePerFrame;

	g_animAlignCache.TryCacheAnim(pMatchAnim, channelId, Limit01(phase - phaseDelta), Limit01(phase + phaseDelta));

	if (!EvaluateChannelInAnimCached(skelId, pMatchAnim, channelId, phase, false, pLocOut))
	{
		return false;
	}

	Locator locPre, locPost;

	const float phasePre = Limit01(phase - (phaseDelta * 0.5f));
	const float phasePost = Limit01(phase + (phaseDelta * 0.5f));

	if (!EvaluateChannelInAnimCached(skelId, pMatchAnim, channelId, phasePre, false, &locPre)
		|| !EvaluateChannelInAnimCached(skelId, pMatchAnim, channelId, phasePost, false, &locPost))
	{
		return false;
	}

	const float dt = (phasePost - phasePre) * GetDuration(pMatchAnim);
	if (dt <= NDI_FLT_EPSILON)
	{
		return false;
	}

	const Vector dp = locPost.Pos() - locPre.Pos();
	const Vector vel = dp * (1.0f / dt);

	*pVelOut = vel;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::UpdateSprungMoveDirPs(Vector_arg desiredMoveDirPs, bool snap /* = false */)
{
	ANIM_ASSERT(IsReasonable(desiredMoveDirPs));

	if (snap)
	{
		const float angleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(m_sprungMoveDirPs, desiredMoveDirPs)));
		if (angleDiffDeg > 10.0f)
		{
			LocoLogDetails("Snapping move dir (angle diff: %0.1f deg)\n", angleDiffDeg);
		}

		m_sprungMoveDirPs = desiredMoveDirPs;
	}
	else
	{
		NavCharacter* pNavChar = GetCharacter();
		const float dt = pNavChar->GetClock()->GetDeltaTimeInSeconds();
		const float tt = Limit01(dt * kLocomotionProcRotFilter);
		const Vector filteredDirPs = Slerp(m_sprungMoveDirPs, desiredMoveDirPs, tt);

		m_sprungMoveDirPs = AsUnitVectorXz(filteredDirPs, desiredMoveDirPs);

		ANIM_ASSERT(IsReasonable(filteredDirPs));
		ANIM_ASSERT(IsReasonable(m_sprungMoveDirPs));
	}

	m_sprungMoveDirPs = SafeNormalize(m_sprungMoveDirPs, kUnitXAxis);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::SetMoveToTypeContinuePoseMatchAnim(const StringId64 animId, float startPhase)
{
	m_moveToTypeContinueMatchAnim = animId;
	m_moveToTypeContinueMatchStartPhase = startPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	m_movePerformance.Relocate(deltaPos, lowerBound, upperBound);

	GetStateMachine().Relocate(deltaPos, lowerBound, upperBound);

	m_locomotionHistory.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiLocomotionController::GetMovementSpeedScale() const
{
	float baseScale = 1.0f;

	if (const DC::NpcMoveSetDef* pCurMoveSet = GetCurrentMoveSet())
	{
		baseScale *= pCurMoveSet->m_speedScale;
	}

	return baseScale;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiLocomotionController::PathRemainingDist() const
{
	if (m_remainingPathDistance < 0.0f)
	{
		return kLargeFloat;
	}

	return m_remainingPathDistance;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavGoalReachedType AiLocomotionController::GetCurrentGoalReachedType() const
{
	return m_waypointContract.m_reachedType;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::HasMoveSetFor(MotionType mt) const
{
	return Nav::IsMoveSetValid(m_pCurMoveSets[mt]);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::CanStrafe(MotionType mt) const
{
	if (mt >= kMotionTypeMax)
	{
		return false;
	}

	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_locomotionController.m_forceNonStrafeMovement))
	{
		return false;
	}

	bool canStrafe = false;

	if (m_pCurMoveSets[mt])
	{
		canStrafe = m_pCurMoveSets[mt]->m_strafing;
	}

	return canStrafe;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AiLocomotionController::GetGoalPs() const
{
	const NavCharacter* pNavChar = GetCharacter();

	Locator goalPs = pNavChar->GetLocatorPs();

	if (IsCommandInProgress())
	{
		goalPs.SetRot(m_waypointContract.m_rotPs);

		if (const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs())
		{
			goalPs.SetPos(pPathPs->GetEndWaypoint());
		}
	}

	return goalPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::OverrideMotionMatchingSetId(StringId64 newSetId, TimeDelta duration, float minPathLength)
{
	LocoLog("Overriding MotionMatching Set ID to '%s' for %0.3f seconds\n",
			DevKitOnly_StringIdToString(newSetId),
			ToSeconds(duration));

	m_motionMatchingOverride.m_setId		 = newSetId;
	m_motionMatchingOverride.m_duration		 = duration;
	m_motionMatchingOverride.m_minPathLength = minPathLength;
	m_motionMatchingOverride.m_startTime	 = GetClock()->GetCurTime();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiLocomotionController::GetMotionMatchingSetForState(const MotionState& ms) const
{
	const NavCharacter* pNavChar = GetCharacter();
	const DC::NpcDemeanorDef* pDemeanorDef = pNavChar->GetCurrentDemeanorDef();

	StringId64 setId = pDemeanorDef ? pDemeanorDef->m_motionMatchingSetId : INVALID_STRING_ID_64;

	const MotionType mt = ms.m_motionType;

	if ((mt < kMotionTypeMax) && m_pCurMoveSets[mt])
	{
		setId = m_pCurMoveSets[mt]->m_motionMatchingSetId;
	}

	const StringId64 overrideSetId = m_motionMatchingOverride.m_setId;
	if ((setId != INVALID_STRING_ID_64) && (overrideSetId != INVALID_STRING_ID_64))
	{
		const Clock* pClock = GetClock();
		const bool withinWindow = !pClock->TimePassed(m_motionMatchingOverride.m_duration,
													  m_motionMatchingOverride.m_startTime);
		const bool enoughPath	= (m_motionMatchingOverride.m_minPathLength < 0.0f)
								|| (m_remainingPathDistance >= 0.0f
									&& (m_remainingPathDistance >= m_motionMatchingOverride.m_minPathLength))
								|| (m_remainingPathDistance < 0.0f);

		if (withinWindow && enoughPath)
		{
			setId = overrideSetId;
		}
	}

	return setId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiLocomotionController::GetDesiredMotionMatchingSetId() const
{
	return GetMotionMatchingSetForState(m_desiredMotionState);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::NpcMoveSetDef* GetNextInitializedMoveSet(const DC::NpcMoveSetContainer* pMoveSetContainer)
{
	if (!pMoveSetContainer)
		return nullptr;

	const DC::NpcMoveSetDef* pRet = nullptr;

	switch (pMoveSetContainer->m_dcType.GetValue())
	{
	case SID_VAL("npc-move-set-variations"):
		{
			const DC::NpcMoveSetVariations* pVariations = static_cast<const DC::NpcMoveSetVariations*>(pMoveSetContainer);
			for (const DC::NpcMoveSetContainer* pChild : *(pVariations->m_children))
			{
				const DC::NpcMoveSetDef* pMoveSet = GetNextInitializedMoveSet(pChild);

				if (pMoveSet)
				{
					pRet = pMoveSet;
					break;
				}
			}
		}
		break;

	case SID_VAL("npc-move-set-def"):
		{
			const DC::NpcMoveSetDef* pMoveSet = static_cast<const DC::NpcMoveSetDef*>(pMoveSetContainer);
			pRet = pMoveSet;
		}
		break;

	default:
		AI_ASSERTF(false, ("Unknown npc-move-set-container type '%s'", DevKitOnly_StringIdToString(pMoveSetContainer->m_dcType)));
		break;
	}

	return pRet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::DebugOnly_ForceMovePerfCacheRefresh()
{
	NavCharacter* pNavChar = GetCharacter();
	const DC::NpcDemeanorDef* pDemeanorDef = pNavChar->GetCurrentDemeanorDef();

	const StringId64 existingTableId = m_movePerformance.GetTableId(SID("move^move"));

	m_movePerformance.RemoveTable(pNavChar, SID("move^move"));

	if (pDemeanorDef && (existingTableId != pDemeanorDef->m_moveToMoveSet))
	{
		m_movePerformance.AddTable(pNavChar, SID("move^move"), pDemeanorDef->m_moveToMoveSet);
	}

	m_movePerformance.RebuildCache(pNavChar);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiLocomotionController::GetDesiredMoveAngleDeg() const
{
	return NavUtil::GetRelativeXzAngleDiffDegPs(GetCharacter(), m_sprungMoveDirPs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiLocomotionController::GetSoftClampAlphaOverride() const
{
	if (!m_activeCommand.IsValid())
	{
		return -1.0f;
	}

	if (!m_waypointContract.m_exactApproach)
	{
		return -1.0f;
	}

	const MotionType mt = m_motionState.m_motionType;

	const DC::PointCurve* pScaCurve = m_exactApprochSca[mt]; // should also work if mt = kMotionTypeMax

	if (!pScaCurve || (pScaCurve->m_count <= 0))
	{
		return -1.0f;
	}

	const float curveInput = m_remainingPathDistance >= 0.0f ? m_remainingPathDistance : 0.0f;

	const float maxDist = pScaCurve->m_keys[pScaCurve->m_count - 1];

	if (curveInput >= maxDist)
	{
		return -1.0f;
	}

	const float sca = NdUtil::EvaluatePointCurve(curveInput, pScaCurve);

	return sca;
}

/// --------------------------------------------------------------------------------------------------------------- ///
#define PRINT_MOVESET_INFO(TypeStr, pMoveSet)                                                                          \
	if (pMoveSet)                                                                                                      \
	{                                                                                                                  \
		MsgCon("%-22s %s", TypeStr " Move Set:", pMoveSet->m_name.m_string.GetString());                               \
		if (pMoveSet->m_motionMatchingSetId != INVALID_STRING_ID_64)                                                   \
		{                                                                                                              \
			MsgCon(" (mm: %s)", DevKitOnly_StringIdToString(pMoveSet->m_motionMatchingSetId));                         \
		}                                                                                                              \
		MsgCon("\n");                                                                                                  \
	}                                                                                                                  \
	else                                                                                                               \
	{                                                                                                                  \
		MsgCon("%-22s %s", TypeStr " Move Set:", "<none>\n");                                                          \
	}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const Locator& parentSpace = pNavChar->GetParentSpace();
	const FgAnimData* pAnimData = pNavChar ? pNavChar->GetAnimData() : nullptr;
	if (!pAnimData)
		return;

	const Point currentPositionWs = pNavChar->GetTranslation();
	const DC::AnimNpcTopInfo* pTopInfo = pNavChar->GetAnimControl()->TopInfo<DC::AnimNpcTopInfo>();
	const PathWaypointsEx* pActivePathPs = m_activePathInfo.GetPathWaypointsPs();

	if (g_navCharOptions.m_locomotionController.m_display)
	{
		PrimServerWrapper ps = PrimServerWrapper(parentSpace);

		if (pActivePathPs)
		{
			const Point endPosPs = pActivePathPs->GetEndWaypoint();
			const Point orgEndPosPs = m_activePathInfo.GetOrgEndWaypointPs();

			pActivePathPs->DebugDraw(parentSpace, false);

			if (Dist(endPosPs, orgEndPosPs) > 0.01f)
			{
				ps.DrawCross(orgEndPosPs, 0.2f, kColorRed);
				ps.DrawArrow(orgEndPosPs, endPosPs, 0.25f, kColorRed);
			}
		}

		if (true)
		{
			const Point p = parentSpace.UntransformPoint(pNavChar->GetTranslation() + kUnitYAxis);

			const Vector charFaceDirPs = CalculateFaceDirXzPs(pNavChar);
			ps.DrawArrow(p + Vector(0.0f, 0.04f, 0.0f), charFaceDirPs, 0.5f, kColorGreen);
			ps.DrawString(p + Vector(0.0f, 0.04f, 0.0f) + charFaceDirPs, "Char-Face", kColorGreen, 0.5f);

			const Vector chosenFaceDirPs = GetChosenFaceDirPs();
			ps.DrawArrow(p + Vector(0.0f, -0.04f, 0.0f), chosenFaceDirPs, 0.5f, kColorYellow);
			ps.DrawString(p + Vector(0.0f, -0.04f, 0.0f) + chosenFaceDirPs, StringBuilder<256>("Cho-Face (%0.1f deg diff)", pTopInfo->m_facingDiff).c_str(), kColorYellow, 0.5f);

			const Vector faceBaseDirPs = GetFacingBaseVectorPs();
			ps.DrawArrow(p + Vector(0.0f, -0.04f, 0.0f), faceBaseDirPs, 0.5f, kColorYellowTrans);
			ps.DrawString(p + Vector(0.0f, -0.04f, 0.0f) + faceBaseDirPs, "Face-Base", kColorYellowTrans, 0.5f);

			if (IsStrafing() && IsMoving())
			{
				const Vector faceBasePs = GetFacingBaseVectorPs();
				ps.DrawArrow(p + Vector(0.0f, -0.04f, 0.0f), faceBasePs, 0.5f, kColorYellowTrans);
			}

			ps.DrawArrow(p + Vector(0.0f, -0.4f, 0.0f), m_desiredMoveDirPs, 0.5f, kColorWhite);
			ps.DrawString(p + Vector(0.0f, -0.4f, 0.0f) + m_desiredMoveDirPs, "Move", kColorWhite, 0.5f);

			ps.DrawArrow(p + Vector(0.0f, -0.52f, 0.0f), m_sprungMoveDirPs, 0.5f, Color(1.0f, 0.6f, 0.35f));
			ps.DrawString(p + Vector(0.0f, -0.52f, 0.0f) + m_sprungMoveDirPs, "S-Move", Color(1.0f, 0.6f, 0.35f), 0.5f);

			if (pActivePathPs && pActivePathPs->IsValid())
			{
				const Point endWaypointPs = pActivePathPs->GetEndWaypoint();
				const Vector waypointFaceDirPs = GetLocalZ(m_waypointContract.m_rotPs);
				ps.DrawArrow(endWaypointPs + Vector(0.0f, 0.65f, 0.0f), waypointFaceDirPs, 0.5f, Color(0.5f, 0.5f, 1.0f));
				ps.DrawString(endWaypointPs + Vector(0.0f, 0.65f, 0.0f) + waypointFaceDirPs, "Way-Face", Color(0.5f, 0.5f, 1.0f), 0.5f);
			}
			else if (m_activeCommand.IsValid() && m_activeCommand.IsGoalFaceValid())
			{
				const Vector waypointFaceDirPs = GetLocalZ(m_waypointContract.m_rotPs);
				ps.DrawArrow(p + Vector(0.0f, 0.65f, 0.0f), waypointFaceDirPs, 0.5f, Color(0.5f, 0.5f, 1.0f));
				ps.DrawString(p + Vector(0.0f, 0.65f, 0.0f) + waypointFaceDirPs, "Way-Face", Color(0.5f, 0.5f, 1.0f), 0.5f);
			}
		}

		StringBuilder<256> stateDesc;

		stateDesc.append_format("%s", GetStateName("<none>"));

		const bool ready = CanProcessCommand();

		if (ready)
		{
			stateDesc.append_format("-Ready");
		}

		if (IsCommandInProgress())
		{
			stateDesc.append_format("-InProg");
		}

		if (IsStrafing())
		{
			stateDesc.append_format(" (%0.1fdeg)", pTopInfo->m_facingDiff);
		}

		const float xzSpeed = LengthXz(pNavChar->GetVelocityPs());
		stateDesc.append_format(" (%0.2fm/s)", xzSpeed);

		ps.DrawString(pNavChar->GetTranslationPs() + kUnitYAxis, stateDesc.c_str(), ready ? kColorGreen : kColorRed, 0.6f);
	}

	if (g_navCharOptions.m_locomotionController.m_displayDetails && DebugSelection::Get().IsProcessSelected(pNavChar))
	{
		const DC::AnimNpcInfo* pActorInfo = pAnimControl->Info<DC::AnimNpcInfo>();

		const char* pStateName = GetStateName("<none>");

		MsgCon("\n[%s]\n", DevKitOnly_StringIdToString(pNavChar->GetUserId()));
		MsgCon("Command-%d: %s (%s) (pending: %s) (arrived at goal: %s, radius: %0.2fm)\n",
			   m_commandId,
			   LocoCommand::GetTypeName(m_activeCommand.GetType()),
			   LocoCommand::GetStatusName(m_commandStatus),
			   m_pendingCommand.GetTypeName(),
			   HasArrivedAtGoal() ? "yes" : "no",
			   m_activeCommand.GetGoalRadius());

		if (m_waypointContract.m_reachedType == kNavGoalReachedTypeContinue)
		{
			MsgCon("Has Ever Moved Towards Goal: %s\n", TrueFalse(m_everMovedTowardsGoal));
		}

		if (m_commandStatus == LocoCommand::kStatusCommandPending || m_commandStatus == LocoCommand::kStatusCommandInProgress)
		{
			MsgCon("(Command %s still in progress)\n", LocoCommand::GetTypeName(m_activeCommand.GetType()));
		}

		MsgCon("CanProcessCommand:     %s\n", CanProcessCommand() ? "Yes" : "No");

		MsgCon("State:                 %s", pStateName);

		if (const Fsm::StateDescriptor* pPrevState = GetStateMachine().GetPrevStateDescriptor())
		{
			MsgCon(" (prev: %s)", pPrevState->GetStateName());
		}

		if (const Fsm::StateDescriptor* pNextState = GetStateMachine().GetNextStateDescriptor())
		{
			MsgCon(" (next: %s)", pNextState->GetStateName());
		}

		if (IsBusy())
		{
			MsgCon(" *Busy*");
		}

		MsgCon("\n");

		MsgCon("MotionState:           ");
		DebugPrintMotionState(m_motionState, GetMsgOutput(kMsgCon));
		MsgCon("\n");

		MsgCon("DesiredMotionState:    ");
		DebugPrintMotionState(m_desiredMotionState, GetMsgOutput(kMsgCon));
		MsgCon("\n");

		const TimeFrame timeSince = pNavChar->GetClock()->GetTimePassed(m_moveStartTime);
		const TimeFrame year = Hours(8064);
		const TimeFrame month = Hours(672);
		const TimeFrame week = Hours(168);
		const TimeFrame day = Hours(24);

		if (m_moveStartTime == TimeFrameNegInfinity())
		{
			MsgCon("Move Start Time:       Never\n");
		}
/*
		else if (timeSince > year)
		{
			MsgCon("Move Start Time:       Last Year\n");
		}
*/
		else if (timeSince > month)
		{
			MsgCon("Move Start Time:       Last Month\n");
		}
		else if (timeSince > week)
		{
			MsgCon("Move Start Time:       Last Week\n");
		}
		else if (timeSince > day)
		{
			MsgCon("Move Start Time:       Yesterday\n");
		}
		else if (timeSince > Hours(3.0))
		{
			MsgCon("Move Start Time:       %0.3f hours ago\n", ToHours(timeSince));
		}
		else if (timeSince > Minutes(3.0))
		{
			MsgCon("Move Start Time:       %0.3f min ago\n", ToMinutes(timeSince));
		}
		else
		{
			MsgCon("Move Start Time:       %0.3f sec ago\n", ToSeconds(timeSince));
		}

		MsgCon("Allowed Motion Types:  ");
		if (m_pCurMoveSets[kMotionTypeWalk])	MsgCon("Walk");
		if (CanStrafe(kMotionTypeWalk))			MsgCon("[+Strafe]");
		if (m_pCurMoveSets[kMotionTypeRun])		MsgCon(" Run");
		if (CanStrafe(kMotionTypeRun))			MsgCon("[+Strafe]");
		if (m_pCurMoveSets[kMotionTypeSprint])	MsgCon(" Sprint");
		if (CanStrafe(kMotionTypeSprint))		MsgCon("[+Strafe]");
		MsgCon("\n");

		MsgCon("Sub Category:          %s (desired: %s)\n", DevKitOnly_StringIdToString(GetCurrentMtSubcategory()), DevKitOnly_StringIdToString(GetRequestedMtSubcategory()));
		MsgCon("Demeanor:              %s\n", pNavChar->GetDemeanorName(pNavChar->GetCurrentDemeanor()));
		MsgCon("Path Valid:            %s (%d waypoints)\n", m_activePathInfo.IsValid() ? "Yes" : "No", int(m_activePathInfo.GetWaypointCount()));
		MsgCon("Move-to-move Set:      %s\n", DevKitOnly_StringIdToString(m_movePerformance.GetTableId(SID("move^move"))));

		PRINT_MOVESET_INFO("Walk", m_pCurMoveSets[kMotionTypeWalk]);
		PRINT_MOVESET_INFO("Run", m_pCurMoveSets[kMotionTypeRun]);
		PRINT_MOVESET_INFO("Sprint", m_pCurMoveSets[kMotionTypeSprint]);

		MsgCon("Waypoint:              %s @ %.2fm%s\n",
			   (m_waypointContract.m_reachedType == kNavGoalReachedTypeStop) ? "Stop" : "Continue",
			   m_remainingPathDistance,
			   m_waypointContract.m_exactApproach ? " (exact)" : "");

		if ((m_waypointContract.m_reachedType == kNavGoalReachedTypeContinue) && (m_moveToTypeContinueMatchAnim != INVALID_STRING_ID_64))
		{
			MsgCon("Continue Match Anim:   %s @ %0.2f\n",
				   DevKitOnly_StringIdToString(m_moveToTypeContinueMatchAnim),
				   m_moveToTypeContinueMatchStartPhase);
		}

		m_movePerformance.DebugDraw();
	}

	if (const BaseState* pState = GetState())
	{
		pState->DebugDraw();
	}

	DebugDrawTargetPoseMatching();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::BaseState::OnEnter()
{
	AiLocomotionController* pSelf = GetSelf();
	const Fsm::StateDescriptor* pPrevState = pSelf->GetStateMachine().GetPrevStateDescriptor();
	LocoLog("Changing state [%s] -> [%s]\n", pPrevState ? pPrevState->GetStateName() : "<null>", m_pDesc->GetStateName());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::BaseState::OnInterrupted()
{
	AiLocomotionController* pSelf = GetSelf();
	pSelf->GoInterrupted();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector AiLocomotionController::BaseState::GetChosenFaceDirPs() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AiLocomotionController* pSelf = GetSelf();

	const bool moving = pSelf->IsMoving();
	const bool wantToMove = !pSelf->m_unplannedStop && ((pSelf->m_pendingCommand.GetType() == LocoCommand::kCmdStartLocomoting));
	const bool strafing = pSelf->IsStrafing();

	const I32F waypointCount = pSelf->m_activePathInfo.GetWaypointCount();

	Vector choFacePs = kUnitZAxis;

	if (moving || wantToMove)
	{
		if (strafing)
		{
			choFacePs = CalculateFaceDirXzPs(pNavChar);
		}
		else
		{
			choFacePs = pSelf->m_sprungMoveDirPs;
		}
	}
	else if (pSelf->m_activePathInfo.IsValid() || (pSelf->m_unplannedStop && pSelf->m_activeCommand.IsGoalFaceValid()))
	{
		choFacePs = GetLocalZ(pSelf->m_waypointContract.m_rotPs);
	}
	else
	{
		choFacePs = CalculateFaceDirXzPs(pNavChar);
	}

	return choFacePs;
}

/************************************************************************/
/* Stopped State                                                        */
/************************************************************************/
class AiLocomotionController::Stopped : public AiLocomotionController::BaseState
{
	virtual bool CanProcessCommand() const override;
	virtual bool IsBusy() const override { return false; }
	virtual void OnConfigureCharacter(Demeanor demeanor) override;

	virtual void RequestAnimations() override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::Stopped::CanProcessCommand() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurInstance = pBaseLayer->CurrentStateInstance();
	const bool curStateValid = pCurInstance ? (pCurInstance->GetStateFlags() & DC::kAnimStateFlagLocomotionReady) : false;

	BitArray128 bitArrayExclude;
	bitArrayExclude.SetBit(kWeaponController);

	const bool isBusy = pNavChar->IsBusyExcludingControllers(bitArrayExclude, kExcludeFades);

	if (curStateValid && !isBusy)
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Stopped::OnConfigureCharacter(Demeanor demeanor)
{
	AiLocomotionController* pSelf = GetSelf();

	if (pSelf->m_changingDemeanor)
		return;

	NavCharacter* pNavChar	   = GetCharacter();
	AnimControl* pAnimControl  = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	if (pBaseLayer && pBaseLayer->IsTransitionValid(SID("demeanor-change")))
	{
		const WeaponUpDownState desState = pSelf->GetDesiredWeaponUpDownState(true);

		pSelf->ConfigureWeaponUpDownOverlays(desState);

		if (!pBaseLayer->AreTransitionsPending())
		{
			pBaseLayer->RequestTransition(SID("demeanor-change"));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Stopped::RequestAnimations()
{
	AiLocomotionController* pSelf = GetSelf();
	const NavCharacter* pNavChar = GetCharacter();

	if (pSelf->m_motionState.m_moving)
	{
		LocoLogStr("MotionState says moving while in state Stopped!\n");

		MailNpcLogTo(pNavChar,
					 "john_bellomy@naughtydog.com",
					 "MotionState says moving while in state Stopped!",
					 FILE_LINE_FUNC);
	}

	AI_ASSERT(!pSelf->m_motionState.m_moving);
	pSelf->m_motionState.m_moving = false;

	if (pSelf->m_remainingPathDistance >= 0.0f)
	{
		pSelf->m_remainingPathDistance = -1.0f;
	}

	const bool isMoveCommand = (pSelf->m_activeCommand.GetType() == LocoCommand::kCmdStartLocomoting);
	const bool atEndOfPath = pSelf->IsAtEndOfWaypointPath();
	const bool positionSatisfied = !isMoveCommand || atEndOfPath;
	const bool orientationSatisfied = pSelf->IsDesiredOrientationSatisfied(kMinTurnInPlaceAngleDeg);

	if (pSelf->m_unplannedStop
		&& (pSelf->m_commandStatus == LocoCommand::kStatusCommandInProgress)
		&& isMoveCommand
		&& !pSelf->m_desiredMotionState.m_moving
		&& !positionSatisfied)
	{
		// if we had an unplanned stop in the middle of our locomoting command, resume moving
		pSelf->m_desiredMotionState.m_moving = true;
		pSelf->m_unplannedStop = false;
		LocoLogStr("Found myself stopped but with an incomplete move command in progress, resuming moving\n");
	}

	if (!orientationSatisfied || !positionSatisfied)
	{
		if (pSelf->m_desiredMotionState.m_motionType == kMotionTypeMax)
		{
			pSelf->m_desiredMotionState.m_motionType = kMotionTypeRun;
		}
		pSelf->m_motionState = pSelf->ApplyMotionTypeRestriction(pSelf->m_desiredMotionState);
		pSelf->m_motionState.m_moving = false;
	}

	pSelf->SelectNewAction();

	// if we have a command in progress and the path is committed we can assume that we have
	// arrived at our final location to the best of our abilities. Now we need to see if we missed the mark
	// and/or if we need to turn to get oriented properly.
	// (alternatively we may end up here because we were forced to stop due to obstruction, so we have to allow for unplanned stop)
	const bool pathCheck = !isMoveCommand || pSelf->m_activePathInfo.IsValid();
	if (pathCheck && pSelf->m_commandStatus == LocoCommand::kStatusCommandInProgress && !pSelf->HasNewState())
	{
		if (!positionSatisfied)
		{
			// it has failed
			pSelf->CompleteCommand(LocoCommand::kStatusCommandFailed);
			LocoLogStr("FAILED - RequestAnimations_StateStopped() - missedWaypoint\n");
		}
		else if (!orientationSatisfied)
		{
			pSelf->m_desiredMotionState.m_moving = false;
		}
		else
		{
			// succeeded
			pSelf->m_desiredMotionState.m_moving = false;
			pSelf->CompleteCommand(LocoCommand::kStatusCommandSucceeded);

			LocoLog("SUCCEEDED - reached goal (unplannedStop=%s)\n", pSelf->m_unplannedStop ? "true" : "FALSE");
		}
	}

}

FSM_STATE_REGISTER(AiLocomotionController, Stopped, kPriorityMedium);

/************************************************************************/
/* Demeanor change animation                                            */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
class AiLocomotionController::ChangingDemeanor : public AiLocomotionController::BaseState
{
	virtual void OnEnter() override;
	virtual void UpdateStatus() override;
	virtual void OnStartLocomoting(const LocoCommand& cmd) override { OnCommandStarted(cmd); }
	virtual void OnStopLocomoting(const LocoCommand& cmd) override { OnCommandStarted(cmd); }
	virtual void OnCommandStarted(const LocoCommand& cmd);

	virtual bool IsBusy() const override { return false; }

	virtual bool CanProcessCommand() const override { return false; }

protected:
	AnimAction m_demeanorAction;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::ChangingDemeanor::OnEnter()
{
	BaseState::OnEnter();

	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const StringId64& transitionId = GetStateArg<StringId64>();

	m_demeanorAction.Request(pAnimControl, transitionId, AnimAction::kFinishOnNonTransitionalStateReached);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::ChangingDemeanor::UpdateStatus()
{
	BaseState::UpdateStatus();

	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	m_demeanorAction.Update(pAnimControl);

	if (m_demeanorAction.IsDone())
	{
		const Fsm::StateDescriptor* pPrevState = pSelf->GetStateMachine().GetPrevStateDescriptor();

		const StringId64 desSetId = pSelf->GetMotionMatchingSetForState(pSelf->m_motionState);

		if (pSelf->IsCommandInProgress() && desSetId)
		{
			LocoLogStr("Done changing demeanor with a command in progress, going to motion matching\n");
			pSelf->GoMotionMatching();
		}
		else if ((pPrevState == &kStateChangingDemeanor) || (pPrevState == &kStatePlayingIdlePerformance)
				 || (nullptr == pPrevState))
		{
			pSelf->GoStopped();
		}
		else if (desSetId)
		{
			if (pSelf->m_motionState.m_moving)
			{
				pSelf->GoMotionMatching();
			}
			else
			{
				pSelf->GoStopped();
			}
		}
		else if (pPrevState == &kStateMotionMatching)
		{
			if (pSelf->m_motionState.m_moving)
			{
				pSelf->GoMotionMatching();
			}
			else
			{
				pSelf->GoStopped();
			}
		}
		else
		{
			pSelf->SetupNextState(*pPrevState);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::ChangingDemeanor::OnCommandStarted(const LocoCommand& cmd)
{
	m_demeanorAction.Reset();
}

FSM_STATE_REGISTER_ARG(AiLocomotionController, ChangingDemeanor, kPriorityMedium, StringId64);

/************************************************************************/
/* Weapon up/down change                                                */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
class AiLocomotionController::ChangingWeaponUpDown : public AiLocomotionController::ChangingDemeanor
{
	virtual bool CanProcessCommand() const override { return true; }

	virtual float GetWeaponChangeFade() const override
	{
		const NavCharacter* pNavChar = GetCharacter();
		const AnimControl* pAnimControl = pNavChar->GetAnimControl();

		if (const AnimStateInstance* pDestInst = m_demeanorAction.GetTransitionDestInstance(pAnimControl))
		{
			return pDestInst->AnimFade();
		}

		return ChangingDemeanor::GetWeaponChangeFade();
	}
};

FSM_STATE_REGISTER_ARG(AiLocomotionController, ChangingWeaponUpDown, kPriorityMedium, StringId64);

/************************************************************************/
/* Moving using motion matching                                         */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
class AiLocomotionController::MotionMatching : public AiLocomotionController::BaseState
{
public:
	virtual void OnEnter() override;
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual bool IsBusy() const override { return false; }
	virtual void DebugDraw() const override;

	virtual Vector GetChosenFaceDirPs() const override;
	virtual void OnConfigureCharacter(Demeanor demeanor) override;

	virtual float GetMaxMovementSpeed() const override;

	virtual Point GetNavigationPosPs(Point_arg defPosPs) const override
	{
		Point retPs = defPosPs;

		if (const AiLocomotionInterface* pInterface = m_hInterface.ToSubsystem())
		{
			const NavControl* pNavCon = GetNavControl();
			const Point modelPosPs = pInterface->GetModelPosPs();

			const float desRadius = Max(pNavCon->GetDesiredNavAdjustRadius() - NavMeshClearanceProbe::kNudgeEpsilon,
										0.0f);

			const Vector toModelPs = modelPosPs - defPosPs;
			const Vector limitedPs = LimitVectorLength(toModelPs, 0.0f, desRadius);
			retPs = defPosPs + limitedPs;
		}

		return retPs;
	}

	float GetStoppedSpeed(const AiLocomotionInterface* pInterface) const;
	U32F CountMovingMmAnimStates(float stoppedSpeed, bool debugDraw) const;

	bool WasInScriptedAnimation() const;
	bool ShouldEarlyCompleteCommand(const PathInfo& pathInfo) const;
	void UpdatePath(const DC::MotionModelSettings& modelSettings);

	bool m_success;
	TPathWaypoints<16> m_pathPs;
	I32 m_pathId;
	CharacterMmLocomotionHandle m_hController;
	AiLocomotionIntHandle m_hInterface;
};

/// --------------------------------------------------------------------------------------------------------------- ///
Vector AiLocomotionController::MotionMatching::GetChosenFaceDirPs() const
{
	const NavCharacter* pNavChar = GetCharacter();
	AiLocomotionController* pSelf = GetSelf();

	const bool wantToStop = pSelf->m_waypointContract.m_reachedType == kNavGoalReachedTypeStop
							|| (pSelf->m_activeCommand.GetType() == LocoCommand::kCmdStopLocomoting);

	Vector choFacePs = kUnitZAxis;
	const float padDist = pNavChar->GetMotionConfig().m_minimumGoalRadius;

	Maybe<Vector> overrideFacingPs = pNavChar->GetOverrideChosenFacingDirPs();
	if (overrideFacingPs.Valid())
	{
		g_prim.Draw(DebugArrow(pNavChar->GetTranslation(), overrideFacingPs.Get(), kColorMagenta), kPrimDuration1FramePauseable);

		choFacePs = overrideFacingPs.Get();
	}
	else if ((pSelf->IsAtEndOfWaypointPath(padDist) || !pSelf->m_activePathInfo.IsValid()) && wantToStop)
	{
		ANIM_ASSERT(IsFinite(pSelf->m_waypointContract.m_rotPs));

		choFacePs = GetLocalZ(pSelf->m_waypointContract.m_rotPs);
	}
	else if (pSelf->m_motionState.m_moving)
	{
		if (pSelf->m_motionState.m_strafing)
		{
			choFacePs = CalculateFaceDirXzPs(pNavChar);
		}
		else
		{
			ANIM_ASSERT(IsReasonable(pSelf->m_sprungMoveDirPs));

			choFacePs = pSelf->m_sprungMoveDirPs;
		}
	}
	else
	{
		choFacePs = CalculateFaceDirXzPs(pNavChar);
	}

	ANIM_ASSERT(IsReasonable(choFacePs));

	return choFacePs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::MotionMatching::OnConfigureCharacter(Demeanor demeanor)
{
	AiLocomotionController* pSelf = GetSelf();

	CharacterMotionMatchLocomotion* pController = m_hController.ToSubsystem();

	pSelf->SetMoveModeOverlay(pSelf->m_motionState.m_motionType);

	if (pController && pController->IsActiveController())
	{
		pController->RequestRefreshAnimState();

		if (AiLocomotionInterface* pInterface = pController->GetInterface<AiLocomotionInterface>(SID("AiLocomotionInterface")))
		{
			pInterface->ResetSpeedPercentHighWater();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiLocomotionController::MotionMatching::GetMaxMovementSpeed() const
{
	float maxSpeed = -1.0f;

	if (CharacterMotionMatchLocomotion* pController = m_hController.ToSubsystem())
	{
		maxSpeed = pController->GetMotionModelPs().GetMaxSpeed();
	}

	return maxSpeed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::MotionMatching::OnEnter()
{
	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();

	NdSubsystemMgr* pSubsystemMgr = pNavChar->GetSubsystemMgr();

	NAV_ASSERT(pSubsystemMgr);
	if (!pSubsystemMgr)
	{
		pSelf->GoStopped();
		return;
	}

	const StringId64 desSetId = pSelf->GetMotionMatchingSetForState(pSelf->m_motionState);

	if (desSetId == INVALID_STRING_ID_64)
	{
		MailNpcLogTo(pNavChar, "john_bellomy@naughtydog.com", "Went to MM with invalid set id", FILE_LINE_FUNC);
		pSelf->GoStopped();
		return;
	}

	NdSubsystem* pMmSubsytem = pSubsystemMgr->FindSubsystem(SID("CharacterMotionMatchLocomotion"));
	CharacterMotionMatchLocomotion* pController = static_cast<CharacterMotionMatchLocomotion*>(pMmSubsytem);
	if (!pController || !pController->IsActiveController())
	{
		Err spawnRes = Err::kOK;
		CharacterMotionMatchLocomotion::SpawnInfo spawnInfo(SID("CharacterMotionMatchLocomotion"), pNavChar);
		spawnInfo.m_locomotionInterfaceType = SID("AiLocomotionInterface");
		spawnInfo.m_pHistorySeed   = &pSelf->m_locomotionHistory;
		spawnInfo.m_pSpawnErrorOut = &spawnRes;

		if (!pSelf->m_motionState.m_moving)
		{
			spawnInfo.m_initialMotionBlendTime = 0.1f;
		}

		if (WasInScriptedAnimation())
		{
			spawnInfo.m_disableExternalTransitions = true;

			if (const SsAnimateController* pScriptController = pNavChar->GetPrimarySsAnimateController())
			{
				const SsAnimateParams& igcParams = pScriptController->GetParams();
				spawnInfo.m_initialBlendTime	 = igcParams.m_fadeOutSec;
				spawnInfo.m_initialBlendCurve	 = igcParams.m_fadeOutCurve;
			}
		}

		const DC::BlendParams& blendIn = GetStateArg<DC::BlendParams>();
		if (blendIn.m_animFadeTime >= 0.0f)
		{
			spawnInfo.m_initialBlendTime  = blendIn.m_animFadeTime;
			spawnInfo.m_initialBlendCurve = blendIn.m_curve;
		}

		pController = (CharacterMotionMatchLocomotion*)NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap,
																		   spawnInfo,
																		   FILE_LINE_FUNC);

		if (spawnRes == Err::kErrNotFound)
		{
			if (FALSE_IN_FINAL_BUILD(true))
			{
				StringBuilder<256>* pMsg = NDI_NEW(kAllocSingleFrame) StringBuilder<256>;
				pMsg->format("MotionMatching set '%s' was not found!", DevKitOnly_StringIdToString(desSetId));
				const IStringBuilder* pArg = pMsg;
				pSelf->GoError(pArg);
				return;
			}
			else
			{
				pSelf->FadeToIdleStateAndGoStopped();
				return;
			}
		}
	}
	else if (pController->TryChangeSetId(desSetId))
	{
		pController->ChangeInterface(SID("AiLocomotionInterface"));
	}
	else
	{
		LocoLog("Failed to change MotionMatching set id to '%s'\n", DevKitOnly_StringIdToString(desSetId));

		pController->Kill();
		pController = nullptr;
	}

	m_hController = pController;

	if (pController)
	{
		pSelf->UpdateSprungMoveDirPs(pSelf->m_desiredMoveDirPs, true);

		m_success = true;
	}
	else
	{
		LocoLogStr("Failed to create MotionMatching controller, fading to stopped\n");
		pSelf->FadeToIdleStateAndGoStopped();
		m_success = false;
	}

	m_pathId = pSelf->m_commandId;

	if (m_success)
	{
		pSelf->SetMoveModeOverlay(pSelf->m_motionState.m_motionType);

		const DC::MotionModelSettings modelSettings = pController->GetMotionSettings();

		UpdatePath(modelSettings);

		if (AiLocomotionInterface* pInterface = pController->GetInterface<AiLocomotionInterface>(SID("AiLocomotionInterface")))
		{
			m_hInterface = pInterface;

			pInterface->ResetSpeedPercentHighWater();
			pInterface->SetModelPosPs(pNavChar->GetTranslationPs());
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::MotionMatching::ShouldEarlyCompleteCommand(const PathInfo& pathInfo) const
{
	if (!pathInfo.IsValid())
	{
		return false;
	}

	const AiLocomotionController* pSelf = GetSelf();

	if (pSelf->m_motionState.m_moving)
	{
		return false;
	}

	const WaypointContract& waypointContract = pSelf->m_waypointContract;

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
void AiLocomotionController::MotionMatching::UpdatePath(const DC::MotionModelSettings& modelSettings)
{
	AiLocomotionController* pSelf = GetSelf();
	const NavCharacter* pNavChar = GetCharacter();

	if (!pSelf->IsCommandInProgress())
	{
		return;
	}

	if (!pSelf->m_activeCommand.IsFollowingPath())
	{
		m_pathPs.Clear();
		m_pathId = -1;
		return;
	}

	if (const PathWaypointsEx* pLastFoundPathPs = pNavChar->GetLastFoundPathPs())
	{
		const Point myPosPs = pNavChar->GetTranslationPs();

		I32F iLeg = -1;
		pLastFoundPathPs->ClosestPointXz(myPosPs, nullptr, &iLeg);

		const Point leg0 = pLastFoundPathPs->GetWaypoint(iLeg);
		const Point leg1 = pLastFoundPathPs->GetWaypoint(iLeg + 1);

		const Vector newDirPs = AsUnitVectorXz(leg1 - leg0, kZero);

		const float pathLen = pLastFoundPathPs->ComputePathLengthXz();
		const bool moveDirInvalid = (Abs(LengthSqr(pSelf->m_desiredMoveDirPs) - 1.0f) > 0.1f);

		if ((pathLen > 0.5f) || moveDirInvalid)
		{
			pSelf->m_desiredMoveDirPs = newDirPs;
		}

		I32F iPrevLeg = -1;
		Vector prevDirPs = kZero;

		if (m_pathPs.IsValid())
		{
			I32F iPrevClosest = -1;
			m_pathPs.ClosestPointXz(myPosPs, nullptr, &iPrevClosest);

			const Point prevLeg0Ps = m_pathPs.GetWaypoint(iPrevClosest);
			const Point prevLeg1Ps = m_pathPs.GetWaypoint(iPrevClosest + 1);

			prevDirPs = AsUnitVectorXz(prevLeg1Ps - prevLeg0Ps, kZero);
			iPrevLeg = iPrevClosest;

			if ((iPrevClosest + 2) < m_pathPs.GetWaypointCount() && (DistXz(myPosPs, prevLeg1Ps) < 0.25f))
			{
				const Point prevLeg2Ps = m_pathPs.GetWaypoint(iPrevClosest + 2);
				const Vector prevDirAltPs = AsUnitVectorXz(prevLeg2Ps - prevLeg1Ps, kZero);

				if (Dot(prevDirAltPs, newDirPs) > Dot(prevDirPs, newDirPs))
				{
					prevDirPs = prevDirAltPs;
					iPrevLeg = iPrevClosest + 1;
				}
			}

			if ((iPrevClosest > 0) && DistXz(myPosPs, prevLeg0Ps) < 0.25f)
			{
				const Point prevLeg00Ps = m_pathPs.GetWaypoint(iPrevClosest - 1);
				const Vector prevDirAltPs = AsUnitVectorXz(prevLeg0Ps - prevLeg00Ps, kZero);

				if (Dot(prevDirAltPs, newDirPs) > Dot(prevDirPs, newDirPs))
				{
					prevDirPs = prevDirAltPs;
					iPrevLeg = iPrevClosest - 1;
				}
			}
		}

		const Scalar dotP = Dot(prevDirPs, newDirPs);
		const float angleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(dotP));

		if (angleDiffDeg > modelSettings.m_newPathAngleDeg)
		{
			if (FALSE_IN_FINAL_BUILD(false && iPrevLeg >= 0))
			{
				g_prim.Draw(DebugArrow(myPosPs, prevDirPs, kColorCyan, 0.5f, kPrimEnableHiddenLineAlpha), Seconds(1.0f));
				g_prim.Draw(DebugArrow(myPosPs, newDirPs, kColorMagenta, 0.5f, kPrimEnableHiddenLineAlpha), Seconds(1.0f));
			}

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
void AiLocomotionController::MotionMatching::RequestAnimations()
{
	if (!m_success)
		return;

	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();

	CharacterMotionMatchLocomotion* pController = m_hController.ToSubsystem();
	const bool isActive = pController && pController->IsActiveController();
	if (!pController || pController->IsExiting() || !isActive)
	{
		LocoLog("MotionMatching controller disappeared (%s), failing command\n",
				!pController ? "nullptr" : (pController->IsExiting() ? "controller exiting" : "not active controller"));

		pSelf->CompleteCommand(LocoCommand::kStatusCommandFailed);
		pSelf->FadeToIdleStateAndGoStopped();
		return;
	}

	if (pSelf->HasNewState())
	{
		return;
	}

	const MotionState newMotionState = pSelf->ApplyMotionTypeRestriction(pSelf->m_desiredMotionState);
	if (!newMotionState.Equals(pSelf->m_motionState))
	{
		if (DoutMemChannels* pDebugLog = NULL_IN_FINAL_BUILD(pNavChar->GetChannelDebugLog()))
		{
			LocoLogStr("Updating Motion State from ");
			pSelf->DebugPrintMotionState(pSelf->m_motionState, pDebugLog);
			pDebugLog->Appendf(" to :");
			pSelf->DebugPrintMotionState(newMotionState, pDebugLog);
			pDebugLog->Appendf("\n");
		}

		pSelf->m_motionState.m_motionType = newMotionState.m_motionType;
		pSelf->m_motionState.m_strafing	  = newMotionState.m_strafing;

		pSelf->SetMoveModeOverlay(pSelf->m_motionState.m_motionType);
	}

	const DC::MotionModelSettings modelSettings = pController->GetMotionSettings();

	UpdatePath(modelSettings);

	pSelf->UpdateMotionMatchingTargetPose();

	if (pSelf->IsCommandInProgress())
	{
		if (ShouldEarlyCompleteCommand(pSelf->m_activePathInfo))
		{
			LocoLog("Ignoring short path update (%0.2fm) and completing command\n", pSelf->m_activePathInfo.ComputePathLengthXz());
			pSelf->CompleteCommand(LocoCommand::kStatusCommandSucceeded);
		}

		pSelf->UpdateSprungMoveDirPs(pSelf->m_desiredMoveDirPs, true);
	}

	pSelf->SelectNewAction();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::MotionMatching::WasInScriptedAnimation() const
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
float AiLocomotionController::MotionMatching::GetStoppedSpeed(const AiLocomotionInterface* pInterface) const
{
	const AiLocomotionController* pSelf = GetSelf();

	const float speedPercentHw = pInterface ? pInterface->GetSpeedPercentHighWater() : 0.0f;

	float stoppedSpeed = 0.0f;

	if (const DC::NpcMoveSetDef* pCurMoveSet = pSelf->GetCurrentMoveSet())
	{
		stoppedSpeed = pCurMoveSet->m_mmStoppedSpeed;
	}
	else
	{
		stoppedSpeed = g_navCharOptions.m_locomotionController.m_mmStoppedSpeed;
	}

	if (speedPercentHw >= 0.0f)
	{
		stoppedSpeed *= speedPercentHw;
	}

	stoppedSpeed = Max(stoppedSpeed, 0.3f);

	return stoppedSpeed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AiLocomotionController::MotionMatching::CountMovingMmAnimStates(float stoppedSpeed, bool debugDraw) const
{
	const AiLocomotionController* pSelf = GetSelf();
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
void AiLocomotionController::MotionMatching::UpdateStatus()
{
	if (!m_success)
		return;

	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	NdSubsystemMgr* pSubsystemMgr = pNavChar->GetSubsystemMgr();

	const SkeletonId skelId = pNavChar->GetSkeletonId();
	const AnimStateInstance* pTopMmInstance = nullptr;

	const CharacterMotionMatchLocomotion* pController = m_hController.ToSubsystem();
	if (!pController || !pController->IsActiveController())
	{
		return;
	}

	const AiLocomotionInterface* pInterface = pController->GetInterface<AiLocomotionInterface>(SID("AiLocomotionInterface"));

	const float stoppedSpeed = GetStoppedSpeed(pInterface);

	const bool exact = pSelf->m_waypointContract.m_exactApproach;

	const float padDist = exact ? 0.1f : pController->GetMotionModelPs().GetProceduralClampDist();

	const bool atEndOfPath = pSelf->IsAtEndOfWaypointPath(padDist);
	const float remPathDist = pSelf->m_remainingPathDistance < 0.0f ? kLargeFloat : pSelf->m_remainingPathDistance;
	const bool noMorePath = !pSelf->m_activePathInfo.IsValid() || (remPathDist < 0.5f) || atEndOfPath;
	const bool animIsStopped = noMorePath && (CountMovingMmAnimStates(stoppedSpeed, false) == 0);
	const bool animIsMoving = !animIsStopped;

	if (animIsMoving != pSelf->m_motionState.m_moving)
	{
		pSelf->m_motionState.m_moving = animIsMoving;
	}

/*	if (!pSelf->HasNewState())
	{
		const AnimControl* pAnimControl = pNavChar->GetAnimControl();
		const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

		if (!pBaseLayer->AreTransitionsPending() && pBaseLayer->CurrentStateId() == SID("s_idle"))
		{

		}
	}*/

	if (pSelf->IsCommandInProgress())
	{
		const Quat deltaRotPs = Conjugate(pNavChar->GetPrevLocatorPs().Rot()) * pNavChar->GetLocatorPs().Rot();
		const float rotDt = pNavChar->GetClock()->GetLastFrameDeltaTimeInSeconds();

		Vec4 rotAxisPs;
		float rotAngleRad = 0.0f;
		deltaRotPs.GetAxisAndAngle(rotAxisPs, rotAngleRad);

		const float rotSpeedDps = RADIANS_TO_DEGREES(rotAngleRad) / (rotDt < NDI_FLT_EPSILON ? 1.0f : rotDt);
		const bool notTurning = rotSpeedDps < 30.0f;

		const bool isMoveCommand = (pSelf->m_activeCommand.GetType() == LocoCommand::kCmdStartLocomoting);
		const bool positionSatisfied = !isMoveCommand || atEndOfPath;
		const bool orientationSatisfied = pSelf->IsDesiredOrientationSatisfied(kMinTurnInPlaceAngleDeg);
		const bool notTurningCheck = notTurning && animIsStopped;
		const bool reachedTypeStop = pSelf->m_waypointContract.m_reachedType == kNavGoalReachedTypeStop;

		// hacky check to see if motion matching has stopped
		if (positionSatisfied && orientationSatisfied && notTurningCheck && !animIsMoving)
		{
			pSelf->m_desiredMotionState.m_moving = false;

			pSelf->CompleteCommand(LocoCommand::kStatusCommandSucceeded);

			if (!animIsMoving && reachedTypeStop)
			{
				LocoLogStr("SUCCEEDED - MotionMatching reached goal\n");
				pSelf->m_motionState.m_moving = false;
			}
			else if (!reachedTypeStop)
			{
				LocoLogStr("SUCCEEDED - MotionMatching reached type continue\n");
			}
		}
		else if (!pSelf->m_desiredMotionState.m_moving && isMoveCommand)
		{
			pSelf->m_desiredMotionState.m_moving = true;
			LocoLogStr("Motion Matching in Stopped State, but movement is still pending. Forcing desired moving to true.\n");
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::MotionMatching::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (g_navCharOptions.m_locomotionController.m_displayDetails
		&& DebugSelection::Get().IsProcessSelected(GetCharacter()))
	{
		const CharacterMotionMatchLocomotion* pController = m_hController.ToSubsystem();
		if (!pController || !pController->IsActiveController())
		{
			return;
		}

		const AiLocomotionInterface* pInterface = pController->GetInterface<AiLocomotionInterface>(SID("AiLocomotionInterface"));
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

		MsgCon("MM Path Id: %d\n", m_pathId);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static CatmullRom* CreatePathFromWaypointsPs(const PathWaypointsEx& waypointsPs)
{
	ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);

	const size_t numPoints = waypointsPs.GetWaypointCount();
	ListArray<Locator> points(numPoints);

	for (I32F iPt = 0; iPt < numPoints; iPt++)
	{
		const Point posPs = waypointsPs.GetWaypoint(iPt);
		points.PushBack(Locator(posPs));
	}

	AllocateJanitor frameAlloc(kAllocSingleFrame, FILE_LINE_FUNC);

	return CatmullRomBuilder::CreateCatmullRom(points, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetStoppingFaceDist(const AiLocomotionController* pSelf,
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
static float ClosestMoveCyclePhase(const NavCharacter* pNavChar, const ArtItemAnim* pLoopAnim)
{
	const AnkleInfo* pAnklesOs = pNavChar ? pNavChar->GetAnkleInfo() : nullptr;

	if (!pAnklesOs || !pLoopAnim)
	{
		return -1.0f;
	}

	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

	if (pCurInstance && (pCurInstance->GetPhaseAnim() == pLoopAnim->GetNameId()) && !pCurInstance->IsFlipped())
	{
		return pCurInstance->Phase();
	}

	g_animAlignCache.TryCacheAnim(pLoopAnim, SID("lAnkle"), 0.0f, 1.0f);
	g_animAlignCache.TryCacheAnim(pLoopAnim, SID("rAnkle"), 0.0f, 1.0f);

	PoseMatchInfo mi;
	mi.m_startPhase = 0.0f;
	mi.m_endPhase	= 1.0f;
	mi.m_rateRelativeToEntry0 = true;
	mi.m_strideTransform	  = kIdentity;
	mi.m_inputGroundNormalOs  = pNavChar->GetLocatorPs().UntransformVector(pNavChar->GetGroundNormalPs());

	mi.m_entries[0].m_channelId		= SID("lAnkle");
	mi.m_entries[0].m_matchPosOs	= pAnklesOs->m_anklePos[0];
	mi.m_entries[0].m_matchVel		= pAnklesOs->m_ankleVel[0];
	mi.m_entries[0].m_velocityValid = true;
	mi.m_entries[0].m_valid			= true;

	mi.m_entries[1].m_channelId		= SID("rAnkle");
	mi.m_entries[1].m_matchPosOs	= pAnklesOs->m_anklePos[1];
	mi.m_entries[1].m_matchVel		= pAnklesOs->m_ankleVel[1];
	mi.m_entries[1].m_velocityValid = true;
	mi.m_entries[1].m_valid			= true;

	//mi.m_debug = true;
	//mi.m_debugDrawTime = Seconds(5.0f);
	//mi.m_debugDrawTime = kPrimDuration1FramePauseable;
	//mi.m_debugDrawLoc = pNavChar->GetLocator();
	//mi.m_debugPrint = true;

	const float matchPhase = CalculateBestPoseMatchPhase(pLoopAnim, mi).m_phase;
	return matchPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::ShouldDoTargetPoseMatching() const
{
	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_disableMmTargetPoseBlending))
		return false;

	const bool typeContinue = (m_waypointContract.m_reachedType == kNavGoalReachedTypeContinue);
	if (!typeContinue)
		return false;

	if (m_moveToTypeContinueMatchAnim == INVALID_STRING_ID_64)
		return false;

	if (m_remainingPathDistance < 0.0f)
		return false;

	const float curSpeed = Length(GetCharacter()->GetVelocityPs());
	if (curSpeed < 0.5f)
		return false;

	const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs();
	if (!pPathPs || !pPathPs->IsValid())
		return false;

	const float sharpestTurnCos = pPathPs->ComputeCosineSharpestTurnAngle(0.0f);

	if (sharpestTurnCos < 0.707f)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::DebugDrawTargetPoseMatching() const
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldDoTargetPoseMatching())
		return;

	if (m_poseMatchInfo.m_desLoopPhaseAtGoal < 0.0f || m_poseMatchInfo.m_actLoopPhaseAtGoal < 0.0f)
		return;

	if (!g_navCharOptions.m_locomotionController.m_debugDrawTargetPoseSkewing)
		return;

	const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs();

	const NavCharacter* pNavChar = GetCharacter();
	const SkeletonId skelId = pNavChar->GetSkeletonId();

	const ArtItemAnimHandle hLoopAnim = m_cachedPoseMatchSourceLookup.GetAnim();
	const ArtItemAnimHandle hDestAnim = m_cachedPoseMatchDestLookup.GetAnim();

	const ArtItemAnim* pLoopAnim = hLoopAnim.ToArtItem();
	const ArtItemAnim* pDestAnim = hDestAnim.ToArtItem();

	const float destPhase = m_moveToTypeContinueMatchStartPhase;

	const Locator& parentSpace = pNavChar->GetParentSpace();
	const Locator curLocWs = pNavChar->GetLocator();

	const Point pathEndPs = pPathPs->GetEndWaypoint();
	const Point pathPreEndPs = pPathPs->GetWaypoint(pPathPs->GetWaypointCount() - 2);
	const Vector pathDirPs = AsUnitVectorXz(pathEndPs - pathPreEndPs, GetLocalZ(pNavChar->GetRotationPs()));
	const Vector pathDirWs = parentSpace.TransformVector(pathDirPs);
	const Point pathEndWs = parentSpace.TransformPoint(pathEndPs);

	const Locator goalLocWs = Locator(pathEndWs, QuatFromLookAt(pathDirWs, kUnitYAxis));

	const AnimSample destAnimSample = AnimSample(hDestAnim, destPhase);
	const AnimSample targetLoopAnimSample = AnimSample(hLoopAnim, m_poseMatchInfo.m_desLoopPhaseAtGoal);
	const AnimSample curLoopOnGoalAnimSample = AnimSample(hLoopAnim, m_poseMatchInfo.m_actLoopPhaseAtGoal);

	MotionMatchingDebug::DebugDrawFullAnimPose(destAnimSample, goalLocWs, kColorGreenTrans);
	MotionMatchingDebug::DebugDrawFullAnimPose(targetLoopAnimSample, goalLocWs, kColorOrangeTrans);
	MotionMatchingDebug::DebugDrawFullAnimPose(curLoopOnGoalAnimSample, goalLocWs, kColorCyan);
	// MotionMatchingDebug::DebugDrawFullAnimPose(actCurLoopAnimSample, curLocWs, kColorMagenta);
	// MotionMatchingDebug::DebugDrawFullAnimPose(desCurLoopAnimSample, curLocWs, kColorCyanTrans);

	const float realTime = EngineComponents::GetNdFrameState()->GetClock(kRealClock)->GetCurTime().ToSeconds();

	const float futureDrawTime = Fmod(realTime);
	MotionMatchingDebug::DrawFuturePose(destAnimSample, goalLocWs, kColorGreen, futureDrawTime, 2.0f);
	MotionMatchingDebug::DrawFuturePose(targetLoopAnimSample, goalLocWs, kColorOrange, futureDrawTime, 2.0f);
	MotionMatchingDebug::DrawFuturePose(curLoopOnGoalAnimSample, goalLocWs, kColorCyanTrans, futureDrawTime - 1.0f, 2.0f);

	// const float drawTime = fmodf(realTime, m_poseMatchInfo.m_rewindTime);

	//MsgCon("drawTime = %f\n", drawTime);

	StringBuilder<256> desc;
	desc.append_format("Dest: '%s' @ %f (frame: %0.2f) [%0.3fm/s]\n",
					   pDestAnim->GetName(),
					   destPhase,
					   destAnimSample.Frame(),
					   m_poseMatchInfo.m_destAnimSpeed);
	desc.append_format("Loop: '%s' @ %f (frame: %0.2f) [%0.3fm/s]\n",
					   pLoopAnim->GetName(),
					   m_poseMatchInfo.m_desLoopPhaseAtGoal,
					   targetLoopAnimSample.Frame(),
					   m_poseMatchInfo.m_matchLoopSpeed);

	g_prim.Draw(DebugString(goalLocWs.Pos(), desc.c_str(), kColorWhite, 0.5f));

	Locator alignStart;
	Locator alignEnd;
	EvaluateChannelInAnimCached(skelId, pLoopAnim, SID("align"), 0.0f, false, &alignStart);
	EvaluateChannelInAnimCached(skelId, pLoopAnim, SID("align"), 1.0f, false, &alignEnd);

	const Point goalLAnkleWs = goalLocWs.TransformPoint(m_poseMatchInfo.m_goalAnklesOs[kLeftLeg]);
	const Point goalRAnkleWs = goalLocWs.TransformPoint(m_poseMatchInfo.m_goalAnklesOs[kRightLeg]);
	const Vector goalLAnkleVelWs = goalLocWs.TransformVector(m_poseMatchInfo.m_goalAnkleVelsOs[kLeftLeg]);
	const Vector goalRAnkleVelWs = goalLocWs.TransformVector(m_poseMatchInfo.m_goalAnkleVelsOs[kRightLeg]);

	g_prim.Draw(DebugCross(goalLAnkleWs, 0.1f, kColorGreen, kPrimDisableDepthTest, "lAnkle", 0.5f));
	g_prim.Draw(DebugCross(goalRAnkleWs, 0.1f, kColorGreen, kPrimDisableDepthTest, "rAnkle", 0.5f));
	g_prim.Draw(DebugArrow(goalLAnkleWs, goalLAnkleVelWs * 0.1f, kColorGreen, 0.1f, kPrimDisableDepthTest));
	g_prim.Draw(DebugArrow(goalRAnkleWs, goalRAnkleVelWs * 0.1f, kColorGreen, 0.1f, kPrimDisableDepthTest));

	{
		Locator locL, locR;
		Vector velL, velR;
		const bool evalL = ReadLocAndVelocityCached(skelId, pLoopAnim, SID("lAnkle"), m_poseMatchInfo.m_desLoopPhaseAtGoal, &locL, &velL);
		const bool evalR = ReadLocAndVelocityCached(skelId, pLoopAnim, SID("rAnkle"), m_poseMatchInfo.m_desLoopPhaseAtGoal, &locR, &velR);

		if (evalL && evalR)
		{
			const Point loopLAnkleWs = goalLocWs.TransformPoint(locL.Pos());
			const Point loopRAnkleWs = goalLocWs.TransformPoint(locR.Pos());
			const Vector loopLAnkleVelWs = goalLocWs.TransformVector(velL);
			const Vector loopRAnkleVelWs = goalLocWs.TransformVector(velR);

			g_prim.Draw(DebugCross(loopLAnkleWs, 0.1f, kColorOrange, kPrimDisableDepthTest));
			g_prim.Draw(DebugCross(loopRAnkleWs, 0.1f, kColorOrange, kPrimDisableDepthTest));
			g_prim.Draw(DebugArrow(loopLAnkleWs, loopLAnkleVelWs * 0.1f, kColorOrange, 0.1f, kPrimDisableDepthTest));
			g_prim.Draw(DebugArrow(loopRAnkleWs, loopRAnkleVelWs * 0.1f, kColorOrange, 0.1f, kPrimDisableDepthTest));
		}
	}

	const float loopDuration = GetDuration(pLoopAnim);
	const float loopDist = Dist(alignStart.Pos(), alignEnd.Pos());
	const float loopVelocity = loopDist / loopDuration;

	// const float rewindDistance = loopVelocity * m_poseMatchInfo.m_rewindTime;

	// const Locator rwAlignWs = Locator(goalLocWs.Pos() + (-1.0f * rewindDistance * GetLocalZ(goalLocWs)), goalLocWs.Rot());

	//MotionMatchingDebug::DrawFuturePose(desCurLoopAnimSample, rwAlignWs, kColorOrange, drawTime);
	// MotionMatchingDebug::DrawFuturePose(actCurLoopAnimSample, rwAlignWs, kColorCyan, drawTime);

	/*
		float m_matchLoopSpeed = 0.0f;
		float m_desLoopPhaseAtGoal = -1.0f;

		float m_actLoopPhaseAtGoal		 = -1.0f;
		float m_lastMatchedLoopPhase = -1.0f;
		float m_remainingTravelTime	 = 0.0f;

		float m_targetPoseBlend = 0.0f;
		float m_translationSkew = 1.0f;
	*/

	MsgCon("des loop phase at goal: %f [last matched @ %f]\n",
		   m_poseMatchInfo.m_desLoopPhaseAtGoal,
		   m_poseMatchInfo.m_lastMatchedLoopPhase);

	MsgCon("cur loop phase at goal: %f [diff %f]\n",
		   m_poseMatchInfo.m_actLoopPhaseAtGoal,
		   PhaseDiff(m_poseMatchInfo.m_actLoopPhaseAtGoal, m_poseMatchInfo.m_desLoopPhaseAtGoal));

	MsgCon("time remaining:         %0.3f sec (%0.1fm @ %0.3fm/s) [%d frames]\n",
		   m_poseMatchInfo.m_remainingTravelTime,
		   m_remainingPathDistance,
		   m_poseMatchInfo.m_matchLoopSpeed,
		   (Seconds(m_poseMatchInfo.m_remainingTravelTime).ToFrames() >> 1UL));

	MsgCon("translation skew:       %f\n", m_poseMatchInfo.m_translationSkew);
	MsgCon("actual speed:           %0.3fm/s\n", (float)Length(pNavChar->GetVelocityPs()));
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetLoopMatchPhaseError(float cur, float des)
{
	if (cur < 0.0f || des < 0.0f)
		return kLargeFloat;

	float diff = PhaseDiff(cur, des);

	// always prefer to slow down instead of speeding up
	if (diff < 0.0f)
		diff *= 2.0f;

	return Abs(diff);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::UpdateMotionMatchingTargetPose()
{
	PROFILE_AUTO(Animation);

	m_poseMatchInfo.m_valid = false;
	m_poseMatchInfo.m_targetPoseBlend = 0.0f;

	if (!ShouldDoTargetPoseMatching())
	{
		m_poseMatchInfo.m_translationSkew  = 1.0f;
		return;
	}

	const PathWaypointsEx* pPathPs = m_activePathInfo.GetPathWaypointsPs();
	const StringId64 destAnimId = m_moveToTypeContinueMatchAnim;

	const NavCharacter* pNavChar = GetCharacter();
	const SkeletonId skelId = pNavChar->GetSkeletonId();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	m_cachedPoseMatchSourceLookup.SetSourceId(SID("pose-match-loop-anim"));
	m_cachedPoseMatchDestLookup.SetSourceId(destAnimId);

	m_cachedPoseMatchSourceLookup = pAnimControl->LookupAnimCached(m_cachedPoseMatchSourceLookup);
	m_cachedPoseMatchDestLookup	  = pAnimControl->LookupAnimCached(m_cachedPoseMatchDestLookup);

	const ArtItemAnimHandle hLoopAnim = m_cachedPoseMatchSourceLookup.GetAnim();
	const ArtItemAnimHandle hDestAnim = m_cachedPoseMatchDestLookup.GetAnim();
	const ArtItemAnim* pLoopAnim	  = hLoopAnim.ToArtItem();
	const ArtItemAnim* pDestAnim	  = hDestAnim.ToArtItem();
	const float destPhase = m_moveToTypeContinueMatchStartPhase;

	if (!pLoopAnim || !pDestAnim)
		return;

	const float loopDuration = GetDuration(pLoopAnim);
	if (loopDuration < NDI_FLT_EPSILON)
		return;

	const float curSpeed = Length(pNavChar->GetVelocityPs());
	if (curSpeed < 0.5f)
		return;

	if (pLoopAnim != m_poseMatchInfo.m_matchLoopAnim.ToArtItem())
	{
		Locator locStart, locEnd;
		g_animAlignCache.TryCacheAnim(pLoopAnim, SID("align"), 0.0f, 1.0f);
		bool valid = true;
		valid = valid && EvaluateChannelInAnimCached(skelId, pLoopAnim, SID("align"), 0.0f, false, &locStart);
		valid = valid && EvaluateChannelInAnimCached(skelId, pLoopAnim, SID("align"), 1.0f, false, &locEnd);

		if (!valid)
			return;

		const float loopDist = Dist(locStart.Pos(), locEnd.Pos());
		const float loopSpeed = loopDist / loopDuration;

		m_poseMatchInfo.m_matchLoopAnim  = hLoopAnim;
		m_poseMatchInfo.m_matchLoopSpeed = loopSpeed;
	}

	const float loopSpeed = m_poseMatchInfo.m_matchLoopSpeed;

	const float remainingTravelTime = m_remainingPathDistance / loopSpeed;
	//const float remainingTravelTime = m_remainingPathDistance / curSpeed;
	//const float remainingTravelTime = m_remainingPathDistance / (loopSpeed * m_poseMatchInfo.m_translationSkew);

	const float currentLoopPhase	= ClosestMoveCyclePhase(pNavChar, pLoopAnim);
	const float remainingPhaseDelta = remainingTravelTime / loopDuration;
	const float loopPhaseAtGoal		= Fmod(currentLoopPhase + remainingPhaseDelta);

	m_poseMatchInfo.m_actLoopPhaseAtGoal  = loopPhaseAtGoal;
	m_poseMatchInfo.m_remainingTravelTime = remainingTravelTime;

	bool destAnimChanged = false;

	if (pDestAnim != m_poseMatchInfo.m_matchDestAnim.ToArtItem())
	{
		Locator locL, locR;
		Vector velL, velR;
		const bool evalL = ReadLocAndVelocityCached(skelId, pDestAnim, SID("lAnkle"), destPhase, &locL, &velL);
		const bool evalR = ReadLocAndVelocityCached(skelId, pDestAnim, SID("rAnkle"), destPhase, &locR, &velR);

		m_poseMatchInfo.m_goalAnklesOs[kLeftLeg]	 = evalL ? locL.Pos() : kOrigin;
		m_poseMatchInfo.m_goalAnkleVelsOs[kLeftLeg]	 = evalL ? velL : kZero;
		m_poseMatchInfo.m_goalAnklesOs[kRightLeg]	 = evalR ? locR.Pos() : kOrigin;
		m_poseMatchInfo.m_goalAnkleVelsOs[kRightLeg] = evalR ? velR : kZero;

		m_poseMatchInfo.m_matchDestAnim = hDestAnim;
		m_poseMatchInfo.m_destAnimSpeed = Length(GetAnimVelocityAtPhase(pDestAnim, destPhase, 2));

		destAnimChanged = true;
	}

	const float loopMatchPhaseDiff = m_poseMatchInfo.m_lastMatchedLoopPhase >= 0.0f
										 ? PhaseDiff(loopPhaseAtGoal, m_poseMatchInfo.m_lastMatchedLoopPhase)
										 : 0.0f;
	const float lastMatchedTimeDiff = Abs(loopMatchPhaseDiff) * loopDuration;

	const float matchTimeDiffTolerance = 0.25f; //LerpScale(1.0f, 5.0f, 1.5f, 0.5f, m_remainingPathDistance);

	const bool determineMatchPhase = destAnimChanged || (m_poseMatchInfo.m_desLoopPhaseAtGoal < 0.0f)
									 || (lastMatchedTimeDiff > matchTimeDiffTolerance);

	if (determineMatchPhase)
	{
		PoseMatchInfo mi;
		mi.m_startPhase = 0.0f;
		mi.m_endPhase = 1.0f;
		mi.m_rateRelativeToEntry0 = true;
		mi.m_strideTransform = Transform(kIdentity);
		mi.m_inputGroundNormalOs = pNavChar->GetLocatorPs().UntransformVector(pNavChar->GetGroundNormalPs());

		mi.m_entries[0].m_channelId = SID("lAnkle");
		mi.m_entries[0].m_matchPosOs = m_poseMatchInfo.m_goalAnklesOs[kLeftLeg];
		mi.m_entries[0].m_valid = true;
		mi.m_entries[0].m_matchVel = m_poseMatchInfo.m_goalAnkleVelsOs[kLeftLeg];
		mi.m_entries[0].m_velocityValid = true;

		mi.m_entries[1].m_channelId = SID("rAnkle");
		mi.m_entries[1].m_matchPosOs = m_poseMatchInfo.m_goalAnklesOs[kRightLeg];
		mi.m_entries[1].m_valid = true;
		mi.m_entries[1].m_matchVel = m_poseMatchInfo.m_goalAnkleVelsOs[kRightLeg];
		mi.m_entries[1].m_velocityValid = true;

		if (false)
		{
			const Locator& parentSpace = pNavChar->GetParentSpace();
			const Point pathEndPs = pPathPs->GetEndWaypoint();
			const Point pathPreEndPs = pPathPs->GetWaypoint(pPathPs->GetWaypointCount() - 2);
			const Vector pathDirPs = AsUnitVectorXz(pathEndPs - pathPreEndPs, GetLocalZ(pNavChar->GetRotationPs()));
			const Vector pathDirWs = parentSpace.TransformVector(pathDirPs);
			const Point pathEndWs = parentSpace.TransformPoint(pathEndPs);

			const Locator goalLocWs = Locator(pathEndWs, QuatFromLookAt(pathDirWs, kUnitYAxis));

			mi.m_debug = true;
			mi.m_debugPrint = true;
			mi.m_debugDrawLoc = goalLocWs;
			mi.m_debugDrawTime = Seconds(2.0f);
		}

		PoseMatchAlternates alts;
		alts.m_alternateThreshold = 5.0f;

		const float initialMatchPhase = CalculateBestPoseMatchPhase(pLoopAnim, mi, &alts).m_phase;

		if (initialMatchPhase < 0.0f)
		{
			m_poseMatchInfo.m_translationSkew = 1.0f;
			return;
		}

		const float initialMatchError = GetLoopMatchPhaseError(loopPhaseAtGoal, initialMatchPhase);

		float bestPhase = initialMatchPhase;
		float bestError = initialMatchError;

		for (U32F iAlt = 0; iAlt < alts.m_numAlternates; ++iAlt)
		{
			const float altPhase = alts.m_alternatePhase[iAlt];
			const float altError = GetLoopMatchPhaseError(loopPhaseAtGoal, altPhase);

			if (altError < bestError)
			{
				bestPhase = altPhase;
				bestError = altError;
			}
		}

		m_poseMatchInfo.m_lastMatchedLoopPhase = loopPhaseAtGoal;
		m_poseMatchInfo.m_desLoopPhaseAtGoal	 = bestPhase;
	}

	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
	const float dt = GetProcessDeltaTime();

	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_locomotionController.m_disablePoseMatchSkewing))
	{
		m_poseMatchInfo.m_translationSkew = 1.0f;
	}
	else if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_locomotionController.m_forceDesiredLoopPhase && pCurInstance
								  && pCurInstance->GetPhaseAnim() == pLoopAnim->GetNameId()))
	{
		m_poseMatchInfo.m_translationSkew = 1.0f;

		const float loopPhaseDiff = PhaseDiff(m_poseMatchInfo.m_actLoopPhaseAtGoal,
											  m_poseMatchInfo.m_desLoopPhaseAtGoal);

		if (Abs(loopPhaseDiff) > 0.05f)
		{
			const float curPhase = pCurInstance->Phase();
			const float newPhase = PhaseAdd(curPhase, loopPhaseDiff);
			pCurInstance->SetPhase(newPhase);
		}
	}
	else if (m_remainingPathDistance > 0.1f)
	{
		const float loopPhaseDiff = PhaseDiff(m_poseMatchInfo.m_actLoopPhaseAtGoal,
											  m_poseMatchInfo.m_desLoopPhaseAtGoal);

		float minSkew = 0.65f;

		switch (m_motionState.m_motionType)
		{
		case kMotionTypeRun:
		case kMotionTypeSprint:
			minSkew = 0.8f;
			break;
		}

		const float reqTimeDelta   = loopPhaseDiff * loopDuration;
		const float reqArrivalTime = remainingTravelTime + reqTimeDelta;
		const float reqSpeed	   = m_remainingPathDistance / reqArrivalTime;
		const float desSkew		   = Limit(Sqr(reqSpeed / loopSpeed), minSkew, 1.1f);

		if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_locomotionController.m_disableSkewLowPassFilter))
		{
			m_poseMatchInfo.m_translationSkew = desSkew;
		}
		else
		{
			const float curSkew		 = m_poseMatchInfo.m_translationSkew;
			const float filteredSkew = curSkew + ((desSkew - curSkew) * Limit01(dt * 10.0f));
			m_poseMatchInfo.m_translationSkew = filteredSkew;
		}
	}
	else
	{
		m_poseMatchInfo.m_translationSkew = 1.0f;
	}

	m_poseMatchInfo.m_valid = true;
}

FSM_STATE_REGISTER_ARG(AiLocomotionController, MotionMatching, kPriorityMedium, DC::BlendParams);

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::GoMotionMatching()
{
	static DC::BlendParams s_defBlend = { -1.0f, -1.0f, DC::kAnimCurveTypeInvalid };
	GoMotionMatching(s_defBlend);
}

/************************************************************************/
/* Playing an animation while moving along the path                     */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
class AiLocomotionController::PlayingMovePerformance : public AiLocomotionController::BaseState
{
	virtual void OnEnter() override;
	virtual void OnExit() override;
	virtual void OnStartLocomoting(const LocoCommand& cmd) override;
	virtual bool CanProcessCommand() const override;
	virtual bool IsBusy() const override { return false; }
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;

	AnimActionWithSelfBlend m_moveToMoveAction;
	MovePerformance::Performance m_performance;
	float m_speedComp;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PlayingMovePerformance::OnExit()
{
	NavCharacter* pNavChar = GetCharacter();
	if (Npc* pNpc = Npc::FromProcess(pNavChar))
	{
		pNpc->ClearLookAimMode(kLookAimPrioritySystem, FILE_LINE_FUNC);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PlayingMovePerformance::OnEnter()
{
	BaseState::OnEnter();

	AiLocomotionController* pSelf = GetSelf();
	m_performance = GetStateArg<MovePerformance::Performance>();

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	DC::AnimNpcInfo* pInfo = pAnimControl->Info<DC::AnimNpcInfo>();

	const NdSubsystemMgr* pSubsystemMgr = pNavChar->GetSubsystemMgr();
	const CharacterMotionMatchLocomotion* pMmSubsytem = (const CharacterMotionMatchLocomotion*)pSubsystemMgr->FindSubsystem(SID("CharacterMotionMatchLocomotion"));
	const float modelSpeed = pMmSubsytem ? pMmSubsytem->GetMotionModelPs().GetMaxSpeed() : -1.0f;


	m_speedComp = 1.0f;

	if (modelSpeed > 0.0f && m_performance.m_animSpeed > NDI_FLT_EPSILON)
	{
		m_speedComp = Limit(modelSpeed / m_performance.m_animSpeed, 0.8f, 1.1f);
	}

	AI::GetMovementSpeedScale(pNavChar, pInfo->m_speedFactor);
	pInfo->m_speedFactor *= m_speedComp;

	AI::SetPluggableAnim(pNavChar, m_performance.m_info.m_resolvedAnimId, m_performance.m_info.m_mirror);

	AnimAction::FinishCondition finishCondition = AnimAction::kFinishOnAnimEndEarly;
	StringId64 stateId = SID("s_performance-move");

	if (m_performance.m_flags & DC::kMovePerformanceFlagStoppingPerformance)
	{
		stateId = SID("s_performance-stop");
	}
	else if (m_performance.m_flags & DC::kMovePerformanceFlagInterruptable)
	{
		finishCondition = AnimAction::kFinishOnTransitionTaken;
	}

	if (Npc* pNpc = Npc::FromProcess(pNavChar))
	{
		pNpc->SetLookAimMode(kLookAimPrioritySystem, AiLookAimRequest(SID("LookAimNatural")), FILE_LINE_FUNC);
	}

	FadeToStateParams params;
	params.m_apRef = m_performance.m_startingApRef;
	params.m_apRefValid = true;
	params.m_stateStartPhase = m_performance.m_startPhase;
	params.ApplyBlendParams(&m_performance.m_blendIn);

	m_moveToMoveAction.FadeToState(pNavChar->GetAnimControl(), stateId, params, finishCondition);

	if (m_performance.m_selfBlend.m_phase >= 0.0f)
	{
		m_moveToMoveAction.SetSelfBlendParams(&m_performance.m_selfBlend, m_performance.m_rotatedApRef, pNavChar, 1.0f);
	}

	SendEvent(SID("move-performance-is-playing"), pNavChar, m_performance.m_info.m_resolvedAnimId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PlayingMovePerformance::OnStartLocomoting(const LocoCommand& cmd)
{
	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	if (pSelf->GetMotionMatchingSetForState(pSelf->m_motionState) != INVALID_STRING_ID_64)
	{
		pSelf->GoMotionMatching();
	}
	else
	{
		pSelf->GoStopped();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::PlayingMovePerformance::CanProcessCommand() const
{
	if (m_performance.m_flags & DC::kMovePerformanceFlagStoppingPerformance)
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PlayingMovePerformance::RequestAnimations()
{
	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const float dt = pNavChar->GetClock()->GetDeltaTimeInSeconds();
	DC::AnimNpcInfo* pInfo = pAnimControl->Info<DC::AnimNpcInfo>();
	static float kMoveToMoveSpeedFilter = 0.5f;

	AI::GetMovementSpeedScale(pNavChar, pInfo->m_speedFactor);
	pInfo->m_speedFactor *= m_speedComp;

	//MsgCon("%f (%f)\n", pInfo->m_speedFactor, m_speedComp);

	bool doSteering = false;
	if (m_performance.m_flags & DC::kMovePerformanceFlagFullySteerable)
	{
		doSteering = true;
	}

	if (AnimStateInstance* pMoveInstance = m_moveToMoveAction.GetTransitionDestInstance(pAnimControl))
	{
		if (pMoveInstance->GetStateName() == SID("s_performance-move-exit"))
			doSteering = true;

		if (doSteering)
		{
			pMoveInstance->GetMutableStateFlags() &= ~DC::kAnimStateFlagNoProcRot;
			pMoveInstance->GetMutableStateFlags() &= ~DC::kAnimStateFlagFirstAlignRefMoveUpdate;
		}
		else
		{
			pMoveInstance->GetMutableStateFlags() |= DC::kAnimStateFlagNoProcRot | DC::kAnimStateFlagFirstAlignRefMoveUpdate;
		}
	}

	if (doSteering)
	{
		if (const PathWaypointsEx* pPathWaypointsPs = pSelf->m_activePathInfo.GetPathWaypointsPs())
		{
			const Point myPosPs = pNavChar->GetTranslationPs();

			I32F iStartingLeg = -1;
			if (pPathWaypointsPs->GetWaypointCount() > 2)
			{
				const Point p1Ps = pPathWaypointsPs->GetWaypoint(1);

				const float distToNextLeg = DistXz(myPosPs, p1Ps);
				const float maxRadius = Max(pNavChar->GetMaximumNavAdjustRadius(), 0.5f);
				
				if (distToNextLeg < maxRadius)
				{
					iStartingLeg = 1;
				}
			}

			I32F closestLegOut;
			const Point closestPointPs = ClosestPointOnPath(myPosPs,
															pPathWaypointsPs,
															&closestLegOut,
															true,
															iStartingLeg,
															0.5f);

			const Point targetPointPs = AdvanceClosestPointOnPath(closestPointPs, closestLegOut, pPathWaypointsPs, 0.5f);

			//MsgCon("[%s] closestLeg: %d (starting: %d)\n", pNavChar->GetName(), closestLegOut, iStartingLeg);
			//g_prim.Draw(DebugSphere(targetPointPs + Vector(0.0f, 0.2f, 0.0f), 0.25f));

			const Vector dir = AsUnitVectorXz(targetPointPs - myPosPs, pSelf->m_desiredMoveDirPs);
			pSelf->UpdateSprungMoveDirPs(dir, false);
		}
		else
		{
			pSelf->UpdateSprungMoveDirPs(pSelf->m_desiredMoveDirPs, false);
		}
	}
	else
	{
		const Vector velPs = VectorXz(pNavChar->GetVelocityPs());
		const Vector moveDirPs = SafeNormalize(velPs, pSelf->m_desiredMoveDirPs);

		pSelf->UpdateSprungMoveDirPs(moveDirPs, true);
	}

	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_movePerformances.m_debugMoveToMoves))
	{
		const Vector vo = Vector(0.0f, 0.15f, 0.0f);
		g_prim.Draw(DebugSphere(pNavChar->GetLocator().Pos() + vo, 0.025f, kColorOrange));
		g_prim.Draw(DebugLine(pNavChar->GetPrevLocatorPs().Pos() + vo, pNavChar->GetLocator().Pos() + vo, kColorOrange, 4.0f), Seconds(3.0f));
	}

	BoundFrame apRef = m_performance.m_rotatedApRef;
	BoundFrame curApRef;
	if (pNavChar->GetApOrigin(curApRef))
	{
		Point posWs = apRef.GetTranslationWs();
		posWs.SetY(curApRef.GetTranslationWs().Y());
		apRef.SetTranslationWs(posWs);
		m_moveToMoveAction.SetSelfBlendApRef(apRef);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PlayingMovePerformance::UpdateStatus()
{
	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	m_moveToMoveAction.Update(pAnimControl);

	if (AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer())
	{
		const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
		if (pCurInstance && pCurInstance->GetStateName() == SID("s_performance-move") && pCurInstance->Phase() > m_performance.m_fitPhaseEnd)
			m_moveToMoveAction.Request(pAnimControl, SID("handoff"), AnimAction::kFinishOnNonTransitionalStateReached, &pNavChar->GetBoundFrame());
		if (pCurInstance && pCurInstance->GetStateName() == SID("s_performance-move-exit") && pCurInstance->Phase() > m_performance.m_exitPhase)
			pSelf->GoMotionMatching(m_performance.m_blendOut);
	}

	if (m_moveToMoveAction.IsDone() || (pSelf->m_remainingPathDistance >= 0.0f && pSelf->m_remainingPathDistance < 1.0f))
	{
		m_moveToMoveAction.Reset();
		const StringId64 desSetId = pSelf->GetMotionMatchingSetForState(pSelf->m_desiredMotionState);

		if (0 == (m_performance.m_flags & DC::kMovePerformanceFlagStoppingPerformance))
		{
			if (desSetId != INVALID_STRING_ID_64)
			{
				pSelf->GoMotionMatching(m_performance.m_blendOut);
			}
			else
			{
				pSelf->FadeToIdleStateAndGoStopped(&m_performance.m_blendOut);
			}
		}
		else
		{
			pSelf->m_motionState.m_moving = false;
			pSelf->GoStopped();
		}
	}
}

FSM_STATE_REGISTER_ARG(AiLocomotionController, PlayingMovePerformance, kPriorityMedium, MovePerformance::Performance);

/// --------------------------------------------------------------------------------------------------------------- ///
class AiLocomotionController::PlayingIdlePerformance : public AiLocomotionController::BaseState
{
	virtual void OnEnter() override;
	virtual void OnExit() override;
	virtual bool CanProcessCommand() const override { return true; }
	virtual bool IsBusy() const override { return false; }
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual void OnStartLocomoting(const LocoCommand& cmd) override
	{
		AiLocomotionController* pSelf = GetSelf();
		pSelf->GoStopped();
		pSelf->GetStateMachine().TakeStateTransition();
	}

	virtual void OnConfigureCharacter(Demeanor demeanor) override
	{
		AiLocomotionController* pSelf = GetSelf();

		pSelf->FadeToIdleStateAndGoStopped();
	}

	AnimAction m_idlePerfAction;
};

/// --------------------------------------------------------------------------------------------------------------- ///
FSM_STATE_REGISTER_ARG(AiLocomotionController,
					   PlayingIdlePerformance,
					   kPriorityMediumLow,
					   AiLocomotionController::IdlePerfArgs);

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PlayingIdlePerformance::OnEnter()
{
	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar		  = GetCharacter();
	AnimControl* pAnimControl	  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer	  = pAnimControl->GetBaseStateLayer();

	const IdlePerfArgs& args = GetStateArg<IdlePerfArgs>();

	const ArtItemAnim* pAnim = args.m_hAnim.ToArtItem();
	const StringId64 animId = pAnim ? pAnim->GetNameId() : INVALID_STRING_ID_64;

	LocoLog("Starting idle performance '%s'\n", pAnim->GetName());

	AI::SetPluggableAnim(pNavChar, animId, args.m_mirror);

	FadeToStateParams params;
	params.m_phaseSync = args.m_phaseSync;

	if (args.m_fadeTime >= 0.0f)
	{
		params.m_animFadeTime = args.m_fadeTime;
	}
	else
	{
		params.m_animFadeTime = 0.6f;
	}

	if (args.m_animCurveType != DC::kAnimCurveTypeInvalid)
	{
		params.m_blendType = args.m_animCurveType;
	}
	else
	{
		params.m_blendType = DC::kAnimCurveTypeUniformS;
	}

	const StringId64 animState = args.m_loop ? SID("s_idle-performance-looping") : SID("s_idle-performance");
	const AnimAction::FinishCondition finishCondition = args.m_loop ? AnimAction::kFinishOnLoopingAnimEnd
																	: AnimAction::kFinishOnNonTransitionalStateReached;

	m_idlePerfAction.FadeToState(pAnimControl,
								 animState,
								 params,
								 finishCondition);

	if (Npc* pNpc = Npc::FromProcess(pNavChar))
	{
		pNpc->SetLookAimMode(kLookAimPrioritySystem, SID("LookAimNatural"), FILE_LINE_FUNC);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PlayingIdlePerformance::OnExit()
{
	const IdlePerfArgs& args = GetStateArg<IdlePerfArgs>();

	NavCharacter* pNavChar = GetCharacter();
	if (Npc* pNpc = Npc::FromProcess(pNavChar))
	{
		pNpc->ClearLookAimMode(kLookAimPrioritySystem, FILE_LINE_FUNC);
	}

	if (args.m_ssAction.Valid())
	{
		AiLogScript(GetCharacter(), "Stopping idle performance action\n");
		SsAction waitAction = args.m_ssAction.Get();
		waitAction.Stop();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PlayingIdlePerformance::RequestAnimations()
{
	NavCharacter* pNavChar = GetCharacter();
	AiLocomotionController* pSelf = GetSelf();
	const Demeanor curDemeanor = pNavChar->GetCurrentDemeanor();
	const Demeanor desiredDemeanor = pSelf->GetDesiredDemeanorInternal();
	const bool demeanorUpToDate = (curDemeanor == desiredDemeanor);
	const bool weaponStateUpToDate = pSelf->IsWeaponStateUpToDate();

	if (!demeanorUpToDate && pSelf->TryDemeanorChange(curDemeanor, desiredDemeanor, weaponStateUpToDate))
	{
		LocoLog("Demeanor change successful from '%s' -> '%s'\n",
				pNavChar->GetDemeanorName(curDemeanor),
				pNavChar->GetDemeanorName(desiredDemeanor));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::PlayingIdlePerformance::UpdateStatus()
{
	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	m_idlePerfAction.Update(pAnimControl);

	if (m_idlePerfAction.IsDone() || !m_idlePerfAction.IsValid())
	{
		pSelf->GoStopped();
	}
}

/************************************************************************/
/* NPC is controlled by other systems                                   */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
class AiLocomotionController::Interrupted : public AiLocomotionController::BaseState
{
	virtual void OnEnter() override;

	virtual void OnStartLocomoting(const LocoCommand& cmd) override
	{
		OnCommandStarted(cmd);
	}
	virtual void OnStopLocomoting(const LocoCommand& cmd) override
	{
		// give our recently reset requested mt subcategory a chance to be reflected
		// in the move sets we use (which determines the motion matching set we use)
		AiLocomotionController* pSelf = GetSelf();
		const NavCharacter* pNavChar = GetCharacter();
		const DC::NpcDemeanorDef* pDemeanorDef = pNavChar ? pNavChar->GetCurrentDemeanorDef() : nullptr;
		if (pSelf && pDemeanorDef)
		{
			pSelf->ConfigureMoveSets(pDemeanorDef);
		}

		OnCommandStarted(cmd);
	}

	void OnCommandStarted(const LocoCommand& cmd);

	virtual bool CanProcessCommand() const override { return true; }
	virtual bool IsBusy() const override { return false; }

	virtual void RequestAnimations() override;

	virtual void OnConfigureCharacter(Demeanor demeanor) override
	{
		m_demeanorOutOfDate = true;
	}

	void ReconstructMotionState(const NavAnimHandoffDesc& handoff);
	MotionType DetermineCurrentMotionType() const;
	bool IsMoveSetApplied(const DC::NpcMoveSetContainer* pMoveSetContainer) const;

	bool IsInKnownIdleState() const;
	bool IsInKnownMotionMatchingState() const;

	bool m_demeanorOutOfDate = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Interrupted::OnEnter()
{
	BaseState::OnEnter();

	m_demeanorOutOfDate = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Interrupted::OnCommandStarted(const LocoCommand& cmd)
{
	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar		  = GetCharacter();
	AnimControl* pAnimControl	  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer	  = pAnimControl->GetBaseStateLayer();

	NavAnimHandoffDesc handoff = pNavChar->PopValidHandoff();

	const Demeanor curDemeanor	   = pNavChar->GetCurrentDemeanor();
	const Demeanor desiredDemeanor = pSelf->GetDesiredDemeanorInternal();

	if (curDemeanor != desiredDemeanor)
	{
		LocoLog("Starting handoff but demeanor out of date, applying new one (cur: '%s' want '%s')\n",
				pNavChar->GetDemeanorName(curDemeanor),
				pNavChar->GetDemeanorName(desiredDemeanor));

		const bool handoffWasValid = handoff.IsValid(pNavChar);

		pSelf->m_changingDemeanor = true;
		pNavChar->ForceDemeanor(desiredDemeanor, AI_LOG);
		pSelf->m_changingDemeanor = false;

		if (handoffWasValid)
		{
			if (const StateChangeRequest* pChangeReq = pBaseLayer->GetPendingChangeRequest(0))
			{
				LocoLog("Updating handoff post-demeanor change with change request %d\n", pChangeReq->m_id.GetVal());
				handoff.SetStateChangeRequestId(pChangeReq->m_id, INVALID_STRING_ID_64);
			}
			else
			{
				LocoLogStr("Updating handoff post-demeanor change to current state\n");
				handoff.SetAnimStateInstance(pBaseLayer->CurrentStateInstance());
			}
		}
	}

	ReconstructMotionState(handoff);

	// ensure that our demeanor change didn't invalidate our applied move set overlay
	pSelf->SetMoveModeOverlay(pSelf->m_motionState.m_motionType);

	if (IsInKnownIdleState())
	{
		LocoLog("Leaving interrupted in known idle state%s\n", m_demeanorOutOfDate ? " (demeanor out of date)" : "");

		if (m_demeanorOutOfDate)
		{
			pBaseLayer->RequestTransition(SID("demeanor-change"));
		}

		pSelf->m_motionState.m_moving = false;
		pSelf->GoStopped();
	}
	else if (handoff.IsValid(pNavChar))
	{
		pSelf->GoHandoff(handoff);
	}
	else if (IsInKnownMotionMatchingState())
	{
		LocoLogStr("WARNING: No handoff descriptor but recognize state as known motion matching\n");

		pSelf->FadeToIdleStateAndGoStopped();
	}
	else if (FALSE_IN_FINAL_BUILD(true))
	{
		StringBuilder<256>* pMsg = NDI_NEW(kAllocSingleFrame) StringBuilder<256>;
		pMsg->format("Trying to leave Interrupted state but have no valid handoff descriptor!");
		const IStringBuilder* pArg = pMsg;
		pSelf->GoError(pArg);
	}
	else
	{
		pSelf->FadeToIdleStateAndGoStopped();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Interrupted::ReconstructMotionState(const NavAnimHandoffDesc& handoff)
{
	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar		  = GetCharacter();

	pSelf->m_moveStartTime = TimeFrameNegInfinity();

	if (handoff.IsValid(pNavChar))
	{
		pSelf->m_motionState.m_moving	  = handoff.m_motionType != kMotionTypeMax;
		pSelf->m_motionState.m_strafing	  = false;
		if (pSelf->m_motionState.m_moving)
		{
			pSelf->m_motionState.m_motionType = pSelf->RestrictMotionType(handoff.m_motionType, false);
		}
		else
		{
			pSelf->m_motionState.m_motionType = kMotionTypeMax;
		}
	}
	else if (IsInKnownIdleState())
	{
		pSelf->m_motionState.m_moving = false;
	}
	else if (IsInKnownMotionMatchingState())
	{
		pSelf->m_motionState.m_moving = Length(pNavChar->GetVelocityPs()) > 0.2f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionType AiLocomotionController::Interrupted::DetermineCurrentMotionType() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const DC::NpcDemeanorDef* pDcDem = pNavChar->GetCurrentDemeanorDef();

	MotionType foundMt = kMotionTypeMax;

	for (MotionType mt = kMotionTypeWalk; mt < kMotionTypeMax; mt = MotionType(mt+1))
	{
		const DC::NpcMotionType dcMt = GameMotionTypeToDc(mt);
		const DC::NpcMotionTypeMoveSets& mtMoveSets = pDcDem->m_moveSets[dcMt];

		for (U32F iSubCat = 0; iSubCat < mtMoveSets.m_numSubcategories; ++iSubCat)
		{
			const DC::NpcMoveSetContainer* pMoveSetContainer = mtMoveSets.m_subcategories[iSubCat].m_moveSet;

			if (IsMoveSetApplied(pMoveSetContainer))
			{
				foundMt = mt;
				break;
			}
		}

		if (foundMt != kMotionTypeMax)
			break;
	}

	return foundMt;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::Interrupted::IsMoveSetApplied(const DC::NpcMoveSetContainer* pMoveSetContainer) const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const AnimOverlays* pOverlays = pAnimControl ? pAnimControl->GetAnimOverlays() : nullptr;

	if (!pMoveSetContainer || !pOverlays)
		return false;

	bool applied = false;

	switch (pMoveSetContainer->m_dcType.GetValue())
	{
	case SID_VAL("npc-move-set-variations"):
		{
			const DC::NpcMoveSetVariations* pVariations = static_cast<const DC::NpcMoveSetVariations*>(pMoveSetContainer);
			if (pVariations->m_numChildren > 0)
			{
				for (const DC::NpcMoveSetContainer* pChild : *(pVariations->m_children))
				{
					applied = IsMoveSetApplied(pChild);

					if (applied)
						break;
				}
			}
		}
		break;

	case SID_VAL("npc-move-set-def"):
		{
			const DC::NpcMoveSetDef* pDef = static_cast<const DC::NpcMoveSetDef*>(pMoveSetContainer);

			applied = pOverlays->IsOverlaySet(pDef->m_overlayName);
		}
		break;

	default:
		AI_ASSERTF(false, ("Unknown npc-move-set-container type '%s'", DevKitOnly_StringIdToString(pMoveSetContainer->m_dcType)));
		break;
	}

	return applied;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Interrupted::RequestAnimations()
{
	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();

	pSelf->UpdateSprungMoveDirPs(GetLocalZ(pNavChar->GetRotationPs()), true);

	if (!pSelf->IsWeaponStateUpToDate())
	{
		const WeaponUpDownState desState = pSelf->GetDesiredWeaponUpDownState(true);

		pSelf->ConfigureWeaponUpDownOverlays(desState);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::Interrupted::IsInKnownIdleState() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const StringId64 stateId = pBaseLayer->CurrentStateId();

	bool known = false;

	switch (stateId.GetValue())
	{
	case SID_VAL("s_idle"):
	case SID_VAL("s_idle-sleep"):
	case SID_VAL("s_idle-sleep-stir"):
	case SID_VAL("s_idle-fight"):
	case SID_VAL("s_idle-fight-mirror"):
		known = true;
		break;
	}

	return known;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::Interrupted::IsInKnownMotionMatchingState() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const StringId64 stateId = pBaseLayer->CurrentStateId();

	bool known = false;

	switch (stateId.GetValue())
	{
	case SID_VAL("s_motion-match-locomotion"):
		known = true;
		break;
	}

	return known;
}

FSM_STATE_REGISTER(AiLocomotionController, Interrupted, kPriorityMediumHigh);

/************************************************************************/
/*                                                                      */
/************************************************************************/

class AiLocomotionController::Handoff : public AiLocomotionController::BaseState
{
	virtual void OnEnter() override;
	virtual bool CanProcessCommand() const override { return true; }
	virtual bool IsBusy() const override { return false; }
	virtual void RequestAnimations() override;

	virtual void DebugDraw() const override;

	virtual bool ShouldDoProceduralApRef(const AnimStateInstance* pInstance) const override;
	virtual bool ShouldDoProceduralAlign(const AnimStateInstance* pInstance) const override;

	bool ShouldBlendOut(const AnimStateInstance* pInstance) const;
	bool TooLittleTimeToBlend(const AnimStateInstance* pInstance) const;

	static CONST_EXPR float kCutoffBlendTime = 0.1f;

	bool m_steeringEnabled;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Handoff::OnEnter()
{
	BaseState::OnEnter();

	NavCharacter* pNavChar		  = GetCharacter();
	AiLocomotionController* pSelf = GetSelf();
	const NavAnimHandoffDesc& handoffDesc = GetStateArg<NavAnimHandoffDesc>();

	m_steeringEnabled = false;

	const Locator& alignPs = pNavChar->GetLocatorPs();

	const Vector xzVelPs = VectorXz(pNavChar->GetVelocityPs());
	const float speed = Length(xzVelPs);
	const bool isMoving = speed > 1.0f;

	if (isMoving)
	{
		const Vector animMoveDirPs = SafeNormalize(xzVelPs, pSelf->m_desiredMoveDirPs);
		pSelf->UpdateSprungMoveDirPs(animMoveDirPs, true);
	}
	else
	{
		pSelf->UpdateSprungMoveDirPs(GetLocalZ(alignPs), true);
	}

	if (!pSelf->m_activePathInfo.IsValid())
	{
		pSelf->m_desiredMoveDirPs = pSelf->m_sprungMoveDirPs;
	}

	// re-create motion state
	if (handoffDesc.m_motionType < kMotionTypeMax)
	{
		pSelf->m_motionState.m_moving	 = true;
		pSelf->m_motionState.m_strafing	 = false;
		pSelf->m_motionState.m_motionType = pSelf->RestrictMotionType(handoffDesc.m_motionType, false);
	}
	else
	{
		pSelf->m_motionState.m_moving	 = false;
		pSelf->m_motionState.m_strafing	 = false;
		pSelf->m_motionState.m_motionType = kMotionTypeMax;
	}

	if (!pSelf->IsCommandInProgress())
	{
		pSelf->m_desiredMotionState = pSelf->m_motionState;
	}

	AI_ASSERT(handoffDesc.IsValid(pNavChar));
	if (!handoffDesc.IsValid(pNavChar))
	{
		pSelf->GoStopped();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Handoff::RequestAnimations()
{
	AiLocomotionController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
	const StringId64 curStateId = pCurInstance ? pCurInstance->GetStateName() : INVALID_STRING_ID_64;

	const NavAnimHandoffDesc& handoffDesc = GetStateArg<NavAnimHandoffDesc>();

	if (!pCurInstance)
	{
		LocoLogStr("Handoff state has no AnimStateInstance! Aborting\n");
		pSelf->GoStopped();
		return;
	}

	if (pSelf->IsCommandInProgress()
		&& pSelf->m_activeCommand.GetType() == LocoCommand::kCmdStopLocomoting
		&& !pSelf->m_motionState.m_moving
		&& !pSelf->m_desiredMotionState.m_moving
		&& pSelf->IsDesiredOrientationSatisfied()
		&& !(pBaseLayer->GetTransitionStatus(handoffDesc.GetChangeRequestId()) & StateChangeRequest::kStatusFlagPending))
	{
		LocoLogStr("Early completion of handoff to Stopped state\n");
		pSelf->FadeToIdleStateAndGoStopped();
		pSelf->CompleteCommand(LocoCommand::kStatusCommandSucceeded);
	}

	const Vector xzVelPs = VectorXz(pNavChar->GetVelocityPs());
	const float speed = Length(xzVelPs);
	const bool isMoving = speed > 1.0f;

	const bool withinSteeringWindow = (pCurInstance->Phase() >= handoffDesc.m_steeringPhase)
									  && (handoffDesc.m_steeringPhase >= 0.0f);
	const bool wantSteering = withinSteeringWindow && isMoving;

	if (m_steeringEnabled != wantSteering)
	{
		LocoLog("%s procedural steering [within window: %s] [moving: %s]\n",
				wantSteering ? "Enabling" : "Disabling",
				withinSteeringWindow ? "true" : "false",
				isMoving ? "true" : "false");

		m_steeringEnabled = wantSteering;
	}

	if (const PathWaypointsEx* pPathPs = pSelf->m_activePathInfo.GetPathWaypointsPs())
	{
		const Point myPosPs = pNavChar->GetTranslationPs();

		I32F iLeg = -1;
		pPathPs->ClosestPointXz(myPosPs, nullptr, &iLeg);

		const Point leg0 = pPathPs->GetWaypoint(iLeg);
		const Point leg1 = pPathPs->GetWaypoint(iLeg + 1);

		pSelf->m_desiredMoveDirPs = AsUnitVectorXz(leg1 - leg0, kZero);
	}

	if (m_steeringEnabled)
	{
		pSelf->UpdateSprungMoveDirPs(pSelf->m_desiredMoveDirPs);
	}
	else if (isMoving)
	{
		const Vector animMoveDirPs = SafeNormalize(xzVelPs, pSelf->m_desiredMoveDirPs);
		pSelf->UpdateSprungMoveDirPs(animMoveDirPs, true);
	}
	else
	{
		pSelf->UpdateSprungMoveDirPs(GetLocalZ(pNavChar->GetRotationPs()), true);
	}

	bool busy = false;

	if (!handoffDesc.IsValid(pNavChar))
	{
		LocoLog("Was doing handoff for instance '%u' but am now in state '%s' (instance '%u'), aborting to idle\n",
				handoffDesc.GetAnimStateId(pNavChar).GetValue(),
				DevKitOnly_StringIdToString(pCurInstance->GetStateName()),
				pCurInstance->GetId().GetValue());

		pSelf->m_motionState.m_moving = false;
		pSelf->GoStopped();
		return;
	}

	if (!pSelf->m_motionState.m_moving && !pSelf->m_desiredMotionState.m_moving && IsKnownIdleState(curStateId))
	{
		pSelf->GoStopped();
	}
	else if (IsKnownIdleState(curStateId))
	{
		pSelf->m_motionState.m_moving = false;
		pSelf->GoStopped();
	}
	else if (ShouldBlendOut(pCurInstance))
	{
		pSelf->m_motionState = pSelf->ApplyMotionTypeRestriction(pSelf->m_desiredMotionState);

		if (handoffDesc.m_handoffMmSetId != INVALID_STRING_ID_64)
		{
			pSelf->OverrideMotionMatchingSetId(handoffDesc.m_handoffMmSetId,
											   Seconds(handoffDesc.m_mmMaxDurationSec),
											   handoffDesc.m_mmMinPathLength);
		}

		const StringId64 desSetId = pSelf->GetMotionMatchingSetForState(pSelf->m_motionState);

		if (DoutMemChannels* pDebugLog = NULL_IN_FINAL_BUILD(pNavChar->GetChannelDebugLog()))
		{
			LocoLogStr("Leaving handoff to motion state :");
			pSelf->DebugPrintMotionState(pSelf->m_motionState, pDebugLog);
			pDebugLog->Appendf(" [mm set: %s]\n", DevKitOnly_StringIdToString(desSetId));
		}

		if (desSetId != INVALID_STRING_ID_64)
		{
			pSelf->GoMotionMatching();
		}
		else
		{
			pSelf->FadeToIdleStateAndGoStopped();
		}
	}
	else
	{
		MovePerformance::FindParams params;
		const bool shouldDoMoveToMove = pSelf->MovePerformanceAllowed(&params);
		params.m_debugEditIndex = g_navCharOptions.m_movePerformances.m_debugMoveToMoveIndex;

		if (shouldDoMoveToMove && !pSelf->IsStrafing() && pSelf->IsMoving() && !pSelf->GetStateMachine().HasNewState())
		{
			MovePerformance::Performance performance;

			if (pSelf->m_movePerformance.FindRandomPerformance(pNavChar, params, &performance))
			{
				pSelf->m_lastMovePerformance = performance;
				LocoLog("ChangeToDoingMoveToMove: Path length: %f\n", pSelf->m_activePathInfo.ComputePathLengthXz());
				pSelf->GoPlayingMovePerformance(performance);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Handoff::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	const AiLocomotionController* pSelf = GetSelf();
	const NavCharacter* pNavChar = GetCharacter();

	if (g_navCharOptions.m_movePerformances.m_debugMoveToMoves || g_navCharOptions.m_movePerformances.m_debugEditMoveToMoves)
	{
		MovePerformance::FindParams params;
		const bool shouldDoMoveToMove = pSelf->MovePerformanceAllowed(&params);

		if (shouldDoMoveToMove)
		{
			params.m_debugDraw = true;
			params.m_debugDrawIndex = g_navCharOptions.m_movePerformances.m_debugMoveToMoveIndex;

			params.m_debugEdit = g_navCharOptions.m_movePerformances.m_debugEditMoveToMoves;
			params.m_debugEditIndex = g_navCharOptions.m_movePerformances.m_debugMoveToMoveIndex;

			MovePerformance::Performance performance;
			pSelf->m_movePerformance.FindRandomPerformance(pNavChar, params, &performance);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::Handoff::ShouldDoProceduralAlign(const AnimStateInstance* pInstance) const
{
	if (!m_steeringEnabled)
	{
		return false;
	}

	const NavCharacter* pNavChar = GetCharacter();
	const NavAnimHandoffDesc& handoffDesc = GetStateArg<NavAnimHandoffDesc>();

	if (!pInstance)
	{
		return false;
	}

	if (pInstance->GetId() != handoffDesc.GetAnimStateId(pNavChar))
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::Handoff::ShouldDoProceduralApRef(const AnimStateInstance* pInstance) const
{
	if (!m_steeringEnabled)
	{
		return false;
	}

	const NavCharacter* pNavChar = GetCharacter();
	const NavAnimHandoffDesc& handoffDesc = GetStateArg<NavAnimHandoffDesc>();

	if (!pInstance)
	{
		return false;
	}

	if (pInstance->GetId() != handoffDesc.GetAnimStateId(pNavChar))
	{
		return false;
	}

	const DC::AnimStateFlag stateFlags = pInstance->GetStateFlags();
	if ((stateFlags & DC::kAnimStateFlagApMoveUpdate) == 0)
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLocomotionController::Handoff::ShouldBlendOut(const AnimStateInstance* pInstance) const
{
	const AiLocomotionController* pSelf = GetSelf();
	const NavAnimHandoffDesc& handoffDesc = GetStateArg<NavAnimHandoffDesc>();

	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	const StateChangeRequest::ID changeId = handoffDesc.GetChangeRequestId();
	if ((changeId != StateChangeRequest::kInvalidId) && pBaseLayer)
	{
		const StateChangeRequest::StatusFlag changeStatus = pBaseLayer->GetTransitionStatus(changeId);

		if (changeStatus == StateChangeRequest::kStatusFlagPending)
		{
			return false;
		}
	}

	if (pInstance && (pInstance->GetPhase() >= handoffDesc.m_exitPhase))
	{
		return true;
	}

	if (handoffDesc.m_handoffMmSetId != INVALID_STRING_ID_64)
	{
		return true;
	}

	const U32 subSysId = pInstance->GetSubsystemControllerId();
	const NdSubsystemMgr* pSubSysMgr = pNavChar->GetSubsystemMgr();
	const NdSubsystemAnimController* pSubSysAnim = pSubSysMgr ? pSubSysMgr->FindSubsystemAnimControllerById(subSysId)
															  : nullptr;

	if (pSubSysAnim && pSubSysAnim->GetType() == SID("CharacterMotionMatchLocomotion"))
	{
		return true;
	}

	if (!m_steeringEnabled)
	{
		const float dotP = DotXz(pSelf->m_desiredMoveDirPs, pSelf->m_sprungMoveDirPs);
		const float angleDeg = RADIANS_TO_DEGREES(SafeAcos(dotP));

		if (pSelf->m_motionState.m_moving && (angleDeg > 15.0f))
		{
			return true;
		}
	}

	if (pSelf->m_desiredMotionState.m_moving != pSelf->m_motionState.m_moving)
	{
		return true;
	}

	if (pSelf->m_motionState.m_motionType < kMotionTypeMax)
	{
		float minPathDist = 2.0f;

		if (m_steeringEnabled && (pSelf->m_activeCommand.GetGoalReachedType() == kNavGoalReachedTypeContinue))
		{
			minPathDist = 0.5f;
		}

		if (pSelf->m_remainingPathDistance >= 0.0f && (pSelf->m_remainingPathDistance < minPathDist))
		{
			return true;
		}
	}

	float blendOutTime = 0.4f;

	const StringId64 desiredMmSetId = pSelf->GetDesiredMotionMatchingSetId();
	if (desiredMmSetId != INVALID_STRING_ID_64)
	{
		MMSetPtr mmSet = desiredMmSetId;
		const DC::MotionMatchingSettings* pMmSettings = mmSet.GetSettings();
		if (pMmSettings && (pMmSettings->m_blendTimeSec >= 0.0f))
		{
			blendOutTime = pMmSettings->m_blendTimeSec;
		}
	}

	const ArtItemAnim* pPrevPhaseAnim = pInstance ? pInstance->GetPhaseAnimArtItem().ToArtItem() : nullptr;
	const float totalTime = pPrevPhaseAnim ? GetDuration(pPrevPhaseAnim) : -1.0f;

	if (totalTime > NDI_FLT_EPSILON)
	{
		const float remPhase = Limit01(1.0f - pInstance->Phase());
		const float remTime = remPhase * totalTime;

		if (blendOutTime >= remTime)
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
bool AiLocomotionController::Handoff::TooLittleTimeToBlend(const AnimStateInstance* pInstance) const
{
	if (!pInstance)
		return true;

	const float curPhase = pInstance->Phase();
	const float duration = pInstance->GetDuration();
	const float remTime = Limit01(1.0f - curPhase) * duration;

	return remTime <= kCutoffBlendTime;
}

FSM_STATE_REGISTER_ARG(AiLocomotionController, Handoff, kPriorityMedium, NavAnimHandoffDesc);

/************************************************************************/
/* Something went horribly, terribly wrong                              */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
class AiLocomotionController::Error : public AiLocomotionController::BaseState
{
	virtual void OnEnter() override;
	virtual void RequestAnimations() override;
	virtual bool CanProcessCommand() const override { return false; }

	StringBuilder<256>* m_pErrorMsg;
};

FSM_STATE_REGISTER_ARG(AiLocomotionController, Error, kPriorityMedium, IStringBuilder*);

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Error::OnEnter()
{
	STRIP_IN_FINAL_BUILD;

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	const IStringBuilder* pMsg = GetStateArg<IStringBuilder*>();

	m_pErrorMsg = nullptr;

	if (pMsg)
	{
		LocoLog("ERROR: %s\n", pMsg->c_str());
		MsgErr("[%s]: %s\n", DevKitOnly_StringIdToString(pNavChar->GetUserId()), pMsg->c_str());

		m_pErrorMsg = NDI_NEW(kAllocDebug) StringBuilder<256>("%s", pMsg->c_str());
	}

	if (FALSE_IN_FINAL_BUILD(pBaseLayer))
	{
		FadeToStateParams params;
		params.m_animFadeTime = 0.2f;
		pBaseLayer->FadeToState(SID("s_idle-zen"), params);
	}
	else
	{
		AiLocomotionController* pSelf = GetSelf();

		pSelf->CompleteCommand(LocoCommand::kStatusCommandFailed);
		pSelf->ForceDefaultIdleState();
	}

	//MailNpcLogTo(pNavChar, "john_bellomy@naughtydog.com", "Loco Went Error", FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionController::Error::RequestAnimations()
{
	STRIP_IN_FINAL_BUILD;

	const NavCharacter* pNavChar = GetCharacter();
	if (m_pErrorMsg && pNavChar)
	{
		const Point centerWs = pNavChar->GetBoundingSphere().GetCenter();
		g_prim.Draw(DebugString(centerWs, m_pErrorMsg->c_str(), kColorRed));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AiLocomotionController::GetMaxStateAllocSize()
{
	size_t maxSize = 0;
	maxSize = Max(maxSize, sizeof(Stopped));
	maxSize = Max(maxSize, sizeof(ChangingDemeanor));
	maxSize = Max(maxSize, sizeof(ChangingWeaponUpDown));
	maxSize = Max(maxSize, sizeof(MotionMatching));
	maxSize = Max(maxSize, sizeof(PlayingMovePerformance));
	maxSize = Max(maxSize, sizeof(PlayingIdlePerformance));
	maxSize = Max(maxSize, sizeof(Interrupted));
	maxSize = Max(maxSize, sizeof(Handoff));
	maxSize = Max(maxSize, sizeof(Error));

	return maxSize;
}

/// --------------------------------------------------------------------------------------------------------------- ///
TYPE_FACTORY_REGISTER(AiLocomotionInterface, CharacterLocomotionInterface);

/// --------------------------------------------------------------------------------------------------------------- ///
Err AiLocomotionInterface::Init(const SubsystemSpawnInfo& info)
{
	m_modelPosPs = kOrigin;
	m_speedPercentHighWater = 0.0f;
	m_stairUpDownFactor		= 0.0f;
	m_stairsStatus		= StairsStatus::kNone;
	m_desGroundNormalPs = kUnitYAxis;
	m_smoothedGroundNormalPs = kUnitYAxis;

	m_normalTrackerPs.Reset();

	return ParentClass::Init(info);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionInterface::Update(const Character* pChar, MotionModel& modelPs)
{
	ParentClass::Update(pChar, modelPs);

	const float curSpeed = modelPs.GetSprungSpeed();
	const float maxSpeed = modelPs.GetMaxSpeed();

	const float speedPercent = (maxSpeed > NDI_FLT_EPSILON) ? Limit01(curSpeed / maxSpeed) : 0.0f;

	m_speedPercentHighWater = Max(speedPercent, m_speedPercentHighWater);
	m_modelPosPs = PointFromXzAndY(modelPs.GetPos(), pChar->GetTranslationPs());

	AiLocomotionController* const pSelf = (AiLocomotionController*)GetSelf();

	const bool stairsAllowed = !((pSelf->m_remainingPathDistance < 0.5f)
								 && (pSelf->m_waypointContract.m_reachedType == kNavGoalReachedTypeStop));

	const NavCharacter* pNavChar = NavCharacter::FromProcess(pChar);

	m_stairsStatus = StairsStatus::kNone;
	m_desGroundNormalPs = GetEffectiveGroundNormalPs(pNavChar, stairsAllowed, m_stairsStatus);

	const float dt = pChar->GetClock()->GetDeltaTimeInSeconds();
	const float normalK = g_navCharOptions.m_locomotionController.m_groundNormalSpringK;
	m_smoothedGroundNormalPs = m_normalTrackerPs.Track(m_smoothedGroundNormalPs, m_desGroundNormalPs, dt, normalK);
	m_smoothedGroundNormalPs = SafeNormalize(m_smoothedGroundNormalPs, kUnitYAxis);

	const float stairsExitCostMod = (m_stairsStatus == StairsStatus::kOnStairs) ? 1.0f : -0.2f;
	const Vector stairLeftPs = Cross(m_smoothedGroundNormalPs, kUnitYAxis);
	const Vector stairUpPs = Cross(stairLeftPs, m_smoothedGroundNormalPs);
	const Vector stairUpXzPs = AsUnitVectorXz(stairUpPs, kUnitYAxis);
	m_stairUpDownFactor = Abs(Dot(GetLocalZ(pNavChar->GetLocatorPs()), stairUpXzPs)) * stairsExitCostMod;

	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_drawOptions.m_printActiveLayers))
	{
		MsgCon("[%s] stair up down factor: %f (%s)\n", pNavChar->GetName(), m_stairUpDownFactor, GetStairsStatusStr(m_stairsStatus));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLocomotionInterface::GetInput(ICharacterLocomotion::InputData* pData)
{
	if (!pData)
	{
		return;
	}

	AiLocomotionController* const pSelf = (AiLocomotionController*)GetSelf();
	if (!pSelf)
	{
		return;
	}

	NavCharacter* pNavChar = GetOwnerNavCharacter();

	const float groundAngleDeg	= RADIANS_TO_DEGREES(SafeAcos(m_smoothedGroundNormalPs.Y()));

	pData->m_desiredFacingPs = pSelf->GetChosenFaceDirPs();

	bool hasStairsDcSpec = false;
	if (const DC::NpcMoveSetDef* pCurMoveSet = pSelf->GetCurrentMoveSet())
	{
		ANIM_ASSERT((pCurMoveSet->m_mmLayersCount == 0) || pCurMoveSet->m_mmLayers);

		for (U32F i = 0; i < pCurMoveSet->m_mmLayersCount; ++i)
		{
			const DC::NpcMoveSetMmLayer& entry = pCurMoveSet->m_mmLayers[i];
			if (entry.m_layerId == INVALID_STRING_ID_64)
			{
				continue;
			}

			float entryCost = entry.m_costModifier;

			if (entry.m_layerId == SID("stairs"))
			{
				hasStairsDcSpec = true;

				entryCost *= m_stairUpDownFactor;

				if (m_stairsStatus == StairsStatus::kNone)
				{
					continue;
				}
			}

			if (entry.m_layerId == SID("slope"))
			{
				if ((groundAngleDeg < pCurMoveSet->m_mmSlopeAngleDeg) || // Flat ground
					(m_stairsStatus != StairsStatus::kNone))				 // Currently on stairs
				{
					continue;
				}
			}

			pData->m_mmParams.AddActiveLayer(entry.m_layerId, entryCost);
		}

		if (pCurMoveSet->m_mmFacingDelaySec >= 0.0f &&
			!pNavChar->TimePassed(Seconds(pCurMoveSet->m_mmFacingDelaySec), pSelf->m_moveStartTime))
		{
			pData->m_desiredFacingPs = MAYBE::kNothing;
		}
	}

	if (!hasStairsDcSpec && (m_stairsStatus != StairsStatus::kNone)
		&& !FALSE_IN_FINAL_BUILD(g_navCharOptions.m_locomotionController.m_disableStairsMmLayer))
	{
		const float stairsCost = -1.0f * g_navCharOptions.m_locomotionController.m_stairsLayerCostBias
								 * m_stairUpDownFactor;

		pData->m_mmParams.AddActiveLayer(SID("stairs"), stairsCost);
	}

	pData->m_groundNormalPs = m_smoothedGroundNormalPs;

	if (pSelf->IsState(AiLocomotionController::kStateMotionMatching))
	{
		pData->m_setId = pSelf->GetMotionMatchingSetForState(pSelf->m_motionState);

		if (pSelf->m_desiredMotionState.m_moving)
		{
			pData->m_desiredVelocityDirPs = pSelf->m_desiredMoveDirPs;
		}
		else
		{
			pData->m_desiredVelocityDirPs = kZero;
		}

		const AnimControl* pAnimControl = pNavChar->GetAnimControl();
		const DC::AnimNpcInfo* pActorInfo = pAnimControl ? pAnimControl->Info<DC::AnimNpcInfo>() : nullptr;
		if (pActorInfo)
		{
			pData->m_speedScale = pActorInfo->m_compositeMovementSpeedScale;
		}

		if (const Horse* pHorse = Horse::FromProcess(pNavChar))
		{
			if (pHorse->IsPlayerControlled() && !pHorse->GetPathPs())
			{
				pData->m_pStickFunc = Mm_StickFuncHorse;
				if (pHorse->GetMoveController()->ShouldDoSpeedScale())
					pData->m_speedScale = pHorse->GetSpeedScale();
			}
		}

		if (!pData->m_pStickFunc)
		{
			pData->m_pPathFunc = Mm_PathFunc;
		}

		pData->m_pGroupFunc = Mm_GroupFunc;

		pData->m_forceExternalPose = pSelf->m_poseMatchInfo.m_targetPoseBlend > NDI_FLT_EPSILON;
	}
	else
	{
		pData->m_desiredVelocityDirPs = kZero;
	}

	pData->m_transitionsId = SID("*npc-mm-set-transitions*");

	if (pSelf->m_poseMatchInfo.m_valid)
	{
		pData->m_translationSkew = pSelf->m_poseMatchInfo.m_translationSkew;
	}
	else
	{
		pData->m_translationSkew = 1.0f;
	}

	if (pNavChar->IsCheap())
	{
		pData->m_transitionInterval = Seconds(0.5f);
	}
	else if (pNavChar->GetAnimLod() == DC::kAnimLodFar)
	{
		pData->m_transitionInterval = Seconds(0.5f);
	}
	else if (pNavChar->GetAnimLod() == DC::kAnimLodMidrange)
	{
		pData->m_transitionInterval = Seconds(0.3f);
	}
	else if (pNavChar->GetAnimLod() == DC::kAnimLodNormal)
	{
		pData->m_transitionInterval = Seconds(0.15f);
	}

	if (pSelf->m_waypointContract.m_exactApproach)
	{
		pData->m_softClampAlphaOverride = pSelf->GetSoftClampAlphaOverride();
	}

	if (pSelf->IsCommandInProgress())
	{
		pData->m_softClampAlphaMinFactor = 0.1f;
	}

	if (pSelf->m_suppressRetargetScale)
	{
		pData->m_retargetScaleOverride = 1.0f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiLocomotionInterface::LimitProceduralLookAheadTime(float futureTime, const AnimTrajectory& trajectoryOs) const
{
	const AiLocomotionController* pSelf = (AiLocomotionController*)GetSelf();
	if (!pSelf)
		return futureTime;

	if (pSelf->m_activeCommand.GetType() == LocoCommand::kCmdStopLocomoting)
		return futureTime;

	if (!pSelf->IsState(AiLocomotionController::kStateMotionMatching))
		return futureTime;

	const AiLocomotionController::BaseState* pState = pSelf->GetState();
	if (!pState)
		return futureTime;

	const AiLocomotionController::MotionMatching* pMmState = (AiLocomotionController::MotionMatching*)pState;

	const IPathWaypoints* pPathPs = pMmState->m_pathPs.IsValid() ? &pMmState->m_pathPs : nullptr;

	if (!pPathPs || !pPathPs->IsValid())
		return futureTime;

	const NdGameObject* pGo = GetOwnerGameObject();
	const Locator& locPs = pGo->GetLocatorPs();

	I32F iStartingLeg = -1;
	pPathPs->ClosestPointXz(locPs.Pos(), nullptr, &iStartingLeg);

	if (iStartingLeg < 0)
		return futureTime;

	const U32F waypointCount = pPathPs->GetWaypointCount();
	const Vector baseDirPs = AsUnitVectorXz(pPathPs->GetWaypoint(iStartingLeg + 1) - pPathPs->GetWaypoint(iStartingLeg),
											kUnitZAxis);

	Vector prevDirPs = baseDirPs;

	float limitedTime = futureTime;

	static CONST_EXPR float kCosAngleThreshold = 0.70710678118f; // cos(45 deg)

	for (I32F iLeg = iStartingLeg + 1; iLeg < (waypointCount - 1); ++iLeg)
	{
		const Point leg0Ps = pPathPs->GetWaypoint(iLeg);
		const Point leg1Ps = pPathPs->GetWaypoint(iLeg + 1);
		const Vector nextDirPs = AsUnitVectorXz(leg1Ps - leg0Ps, prevDirPs);

		const float dotP = Dot(baseDirPs, nextDirPs);
		if (dotP < kCosAngleThreshold)
		{
			const Point leg0Os = locPs.UntransformPoint(leg0Ps);
			const float cornerTime = trajectoryOs.GetTimeClosestTo(leg0Os);

			if (cornerTime >= 0.0f)
			{
				limitedTime = Min(limitedTime, Max(cornerTime, 0.3f));

				if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_proceduralOptions.m_drawProceduralMotion))
				{
					PrimServerWrapper ps(pGo->GetParentSpace());
					ps.EnableWireFrame();
					ps.DrawSphere(leg0Ps, 0.1f);
					ps.EnableHiddenLineAlpha();
					ps.DrawArrow(leg0Ps, nextDirPs, 0.5f, kColorRedTrans);
					ps.DrawArrow(leg0Ps - baseDirPs, baseDirPs, 0.5f, kColorRedTrans);
					StringBuilder<256> desc = StringBuilder<256>("LIMITED: %0.1f deg @ %0.3f sec",
																 RADIANS_TO_DEGREES(SafeAcos(dotP)),
																 limitedTime);
					ps.DrawString(leg0Ps, desc.c_str(), kColorWhite, 0.5f);
				}

				break;
			}
		}

		prevDirPs = nextDirPs;
	}

	return limitedTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const IAiLocomotionController* AiLocomotionInterface::GetSelf() const
{
	const NavCharacter* pNavChar = GetOwnerNavCharacter();
	const AnimationControllers* pAnimControllers = pNavChar ? pNavChar->GetAnimationControllers() : nullptr;
	const IAiLocomotionController* pLocoController = pAnimControllers ? pAnimControllers->GetLocomotionController()
																	  : nullptr;

	return pLocoController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiLocomotionController* AiLocomotionInterface::GetSelf()
{
	NavCharacter* pNavChar = GetOwnerNavCharacter();
	AnimationControllers* pAnimControllers = pNavChar ? pNavChar->GetAnimationControllers() : nullptr;
	IAiLocomotionController* pLocoController = pAnimControllers ? pAnimControllers->GetLocomotionController() : nullptr;

	return pLocoController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const IMotionPose* AiLocomotionInterface::GetPose(const MotionMatchingSet* pArtItemSet, bool debug)
{
	const NavCharacter* pNavChar = GetOwnerNavCharacter();

	const AiLocomotionController* pSelf = (AiLocomotionController*)GetSelf();
	if (!pSelf)
		return nullptr;

	const IMotionPose* pPose = nullptr;

	if (const PoseTracker* pPoseTracker = pNavChar->GetPoseTracker())
	{
		pPose = &pPoseTracker->GetPose();
	}

	if ((pSelf->m_poseMatchInfo.m_targetPoseBlend > NDI_FLT_EPSILON) && pPose)
	{
		AllocateJanitor singleFrame(kAllocSingleGameFrame, FILE_LINE_FUNC);

		const MMPose& poseDef = pArtItemSet->m_pSettings->m_pose;

		MotionMatchingPose* pNewPose = MotionMatchingPose::CreatePoseFromOther(poseDef, *pPose);

		const Locator prevLAnkleLocOs = pNewPose->GetJointLocatorOs(SID("l_ankle"));
		const Locator prevRAnkleLocOs = pNewPose->GetJointLocatorOs(SID("r_ankle"));

		const IMotionPose::BodyData prevLAnkleOs = pNewPose->GetJointDataByIdOs(SID("l_ankle"));
		const IMotionPose::BodyData prevRAnkleOs = pNewPose->GetJointDataByIdOs(SID("r_ankle"));

		const float tt = pSelf->m_poseMatchInfo.m_targetPoseBlend; //g_motionMatchingOptions.m_targetPoseBlendFactor;

		const Point desLAnkleOs = pSelf->m_poseMatchInfo.m_goalAnklesOs[kLeftLeg];
		const Point desRAnkleOs = pSelf->m_poseMatchInfo.m_goalAnklesOs[kRightLeg];

		const Vector desLAnkleVelOs = pSelf->m_poseMatchInfo.m_goalAnkleVelsOs[kLeftLeg];
		const Vector desRAnkleVelOs = pSelf->m_poseMatchInfo.m_goalAnkleVelsOs[kRightLeg];

		const Locator newLAnkleOs = Locator(Lerp(prevLAnkleOs.m_pos, desLAnkleOs, tt), prevLAnkleLocOs.Rot());
		const Locator newRAnkleOs = Locator(Lerp(prevRAnkleOs.m_pos, desRAnkleOs, tt), prevRAnkleLocOs.Rot());

		const Vector newLAnkleVelOs = Lerp(prevLAnkleOs.m_vel, desLAnkleVelOs, tt);
		const Vector newRAnkleVelOs = Lerp(prevRAnkleOs.m_vel, desRAnkleVelOs, tt);

		pNewPose->UpdateJointOs(SID("l_ankle"), newLAnkleOs, newLAnkleVelOs);
		pNewPose->UpdateJointOs(SID("r_ankle"), newRAnkleOs, newRAnkleVelOs);

		if (FALSE_IN_FINAL_BUILD(debug))
		{
			const Point comWs = pNavChar->GetLocator().TransformPoint(pNewPose->GetCenterOfMassOs().m_pos);
			g_prim.Draw(DebugString(comWs + Vector(0.0f, 0.2f, 0.0f), StringBuilder<64>("tt: %f", tt).c_str(), kColorWhiteTrans, 0.5f));
			pNewPose->DebugDraw(pNavChar->GetLocator(), kColorYellow, kPrimDuration1FramePauseable);
		}

		pPose = pNewPose;
	}

	return pPose;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
MotionModelInput AiLocomotionInterface::Mm_StickFuncHorse(const Character* pChar,
														  const ICharacterLocomotion::InputData* pInput,
														  const MotionMatchingSet* pArtItemSet,
														  const DC::MotionMatchingSettings* pSettings,
														  const MotionModelIntegrator& integratorPs,
														  TimeFrame delta,
														  bool finalize,
														  bool debug)
{
	PROFILE(Animation, Mm_StickFuncHorse);

	const bool debugDraw = FALSE_IN_FINAL_BUILD(debug && g_navCharOptions.m_locomotionController.m_displayMmSamples
												&& DebugSelection::Get().IsProcessOrNoneSelected(pChar));

	MotionModelInput input;

	const Horse* pHorse = Horse::FromProcess(pChar);
	if (!pHorse)
		return input;

	const AnimationControllers* pAnimControllers = pHorse->GetAnimationControllers();
	AiLocomotionController* pSelf = (AiLocomotionController*)pAnimControllers->GetLocomotionController();
	if (!pSelf)
		return input;

	if (pSelf->IsInterrupted())
		return input;

	input.m_horseOptions.m_useHorseStick = true;

	if (pSelf->m_desiredMotionState.m_moving)
	{
		input.m_velDirPs = pSelf->m_desiredMoveDirPs;
		input.m_facingPs = pSelf->GetChosenFaceDirPs();
	}
	else
	{
		input.m_facingPs = pSelf->GetChosenFaceDirPs();
		input.m_velDirPs = kZero;
	}

	if (HorseMoveController* pMoveController = pHorse->GetMoveController())
	{
		input.m_horseOptions.m_requestImmediateStop = pMoveController->WantsToStop();
		input.m_horseOptions.m_needToBackUp = pMoveController->NeedsToBackUp();
		input.m_horseOptions.m_maxTurnRateDps = pMoveController->GetScriptedMaxTurnRateDps();
		input.m_horseOptions.m_strafingVelPs = pMoveController->GetStrafeVec();
		input.m_horseOptions.m_strafingBlend = pMoveController->GetStrafeBlend();
		input.m_horseOptions.m_maxSpeed = pMoveController->GetMaxSpeedAvoidance();

		if (pHorse->IsBraking())
		{
			input.m_velDirPs = kZero;
			return input;
		}

		input.m_horseOptions.m_horseTurningState = pMoveController->GetTurningState();
		input.m_horseOptions.m_circleStrafeDriftBlend = pMoveController->GetCircleStrafeDriftBlend();
		input.m_velDirPs = input.m_horseOptions.m_requestImmediateStop ? kZero : AsUnitVectorXz(pMoveController->GetStickDir(), kZero);
		const bool recentlyLeftCircleStrafe = pMoveController->RecentlyLeftCircleStrafe();

		if (LengthSqr(input.m_velDirPs) > 0.01f)
		{
			Vector unitDir = SafeNormalize(input.m_velDirPs, kZero);
			HorseAvoidance* pAvoidance = pMoveController->GetAvoidanceController();
			bool isMagnetActive = false;
			Vector magnetFacingDir = input.m_facingPs.Otherwise(kZero);
			float magnetMinStrafeBlend = 0.0f;
			if (pAvoidance)
			{
				bool magnetForcesFacing = false;
				const Vector magnetAdjustedDir = pAvoidance->AdjustMovementForHorseMagnet(input.m_velDirPs, isMagnetActive, magnetFacingDir, magnetMinStrafeBlend, input.m_horseOptions.m_strafingVelPs, magnetForcesFacing);
				if (isMagnetActive)
				{
					input.m_velDirPs = magnetAdjustedDir;
					input.m_horseOptions.m_strafingBlend = Max(input.m_horseOptions.m_strafingBlend, magnetMinStrafeBlend);
					input.m_facingPs = magnetFacingDir;
					input.m_horseOptions.m_forceFacingDir |= magnetForcesFacing;
				}
			}

			Vector adjustedFacingDir = input.m_facingPs.Otherwise(AsUnitVectorXz(GetLocalZ(pChar->GetLocator()),
																				 kUnitYAxis));

			input.m_velDirPs = pMoveController->AdjustMovement(input.m_velDirPs,
															   adjustedFacingDir,
															   pChar->GetLocator(),
															   finalize,
															   adjustedFacingDir);

			if (!isMagnetActive)
				input.m_facingPs = adjustedFacingDir;
		}
		else if (pMoveController->NoInputSinceCircleStrafeExit())
		{
			input.m_horseOptions.m_forceFacingDir = pMoveController->JustLeftCircleStrafe();

			if (recentlyLeftCircleStrafe)
			{
				input.m_horseOptions.m_fadingOutOfCircleStrafe = true;
				const Vector cachedCircleDir = pMoveController->GetCachedCircleStrafeDir();
				input.m_facingPs = cachedCircleDir;
			}
			else
			{
				input.m_horseOptions.m_forceFacingDir = true;
				input.m_facingPs = AsUnitVectorXz(GetLocalZ(pChar->GetLocator()), input.m_facingPs.Get());
			}
		}

		if (!recentlyLeftCircleStrafe/* && (input.m_horseOptions.m_horseTurningState & (kHorseTurningStateTurn | kHorseTurningStateExit))*/)
		{
			const float lookaheadSeconds = pMoveController->GetExitCircleStrafeVelocityLookaheadSeconds();
			if (lookaheadSeconds <= delta.ToSeconds())
			{
				pMoveController->CacheCircleStrafeDir(SafeNormalize(integratorPs.Model().GetVel(), input.m_facingPs.Otherwise(GetLocalZ(pHorse->GetRotation()))));
			}
		}
	}

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		const Point debugPt = integratorPs.Model().GetPos() + Vector(0.0f, 0.35f, 0.0f);

		g_prim.Draw(DebugArrow(debugPt, input.m_velDirPs * 3.0f, kColorGreen, 0.35f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugArrow(debugPt + Vector(0.0f, 0.05f, 0.0f), input.m_facingPs.Otherwise(kZero), kColorRed, 0.35f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);

		if (finalize)
		{
			MsgConPauseable("%sVelDirPs%s\n", GetTextColorString(kTextColorGreen), GetTextColorString(kTextColorNormal));
			MsgConPauseable("%sFacingPs%s\n", GetTextColorString(kTextColorRed), GetTextColorString(kTextColorNormal));

			MsgConPauseable("HorseTurningState: %s\n", GetHorseCircleStateName(input.m_horseOptions.m_horseTurningState));
			MsgConPauseable("Circle Strafe Drift Blend: %.3f\n", input.m_horseOptions.m_circleStrafeDriftBlend);
			MsgConPauseable("Max Turn Rate DPS: %.3f\n", input.m_horseOptions.m_maxTurnRateDps);
			MsgConPauseable(PRETTY_PRINT_BOOL(input.m_horseOptions.m_useHorseStick));
			MsgConPauseable(PRETTY_PRINT_BOOL(input.m_horseOptions.m_requestImmediateStop));
			MsgConPauseable(PRETTY_PRINT_BOOL(input.m_horseOptions.m_fadingOutOfCircleStrafe));
			MsgConPauseable(PRETTY_PRINT_BOOL(input.m_horseOptions.m_forceFacingDir));
		}
	}

	return input;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
MotionModelPathInput AiLocomotionInterface::Mm_PathFunc(const Character* pChar,
														const MotionMatchingSet* pArtItemSet,
														bool debug)
{
	MotionModelPathInput ret;

	const NavCharacter* pNavChar = NavCharacter::FromProcess(pChar);
	if (!pNavChar)
		return ret;

	const AiLocomotionController* pSelf = (AiLocomotionController*)pNavChar->GetAnimationControllers()->GetLocomotionController();
	if (!pSelf)
		return ret;

	if (pSelf->m_activeCommand.GetType() == LocoCommand::kCmdStopLocomoting)
		return ret;

	if (!pSelf->IsState(AiLocomotionController::kStateMotionMatching))
		return ret;

	const AiLocomotionController::BaseState* pState = pSelf->GetState();
	if (!pState)
		return ret;

	const bool reachedTypeContinue = pSelf->m_waypointContract.m_reachedType == kNavGoalReachedTypeContinue;

	if (reachedTypeContinue && pSelf->m_commandStatus == LocoCommand::kStatusCommandSucceeded)
	{
		// blind moving
		return ret;
	}

	const AiLocomotionController::MotionMatching* pMmState = (AiLocomotionController::MotionMatching*)pState;

	const IPathWaypoints* pPathPs = pMmState->m_pathPs.IsValid() ? &pMmState->m_pathPs : nullptr;

	if (!pPathPs)
		return ret;

	const float pathLen		  = pPathPs->ComputePathLengthXz();
	const float minGoalRadius = pNavChar->GetMotionConfig().m_minimumGoalRadius;

	if ((pathLen < minGoalRadius) && reachedTypeContinue)
	{
		return ret;
	}

	ret.m_pathPs.CopyFrom(&pMmState->m_pathPs);
	ret.m_goalRotPs	 = pSelf->m_waypointContract.m_rotPs;
	ret.m_stopAtGoal = pSelf->m_waypointContract.m_reachedType == kNavGoalReachedTypeStop;
	ret.m_pathId	 = pMmState->m_pathId;

	if (pSelf->m_activeCommand.GetMoveArgs().m_recklessStopping && ret.m_stopAtGoal
		&& (pPathPs->GetWaypointCount() == 2))
	{
		ret.m_decelKMult = g_navCharOptions.m_locomotionController.m_recklessStopKMult;
	}

	if (pNavChar->IsUsingNaturalFacePosition() && !pSelf->m_activeCommand.IsGoalFaceValid())
	{
		ret.m_stoppingFaceDist = -1.0f;
	}
	else
	{
		ret.m_stoppingFaceDist = GetStoppingFaceDist(pSelf, pArtItemSet, minGoalRadius);
	}

	if (pSelf->m_activeCommand.IsValid())
	{
		const float cmdGoalRadius = pSelf->m_activeCommand.GetGoalRadius();

		ret.m_goalRadius = Max(cmdGoalRadius, minGoalRadius);

		if (reachedTypeContinue)
		{
			// nudge down so that we ensure the goal reached type continue passedOurDestPoint math check
			// works reliably
			ret.m_goalRadius = Max(ret.m_goalRadius - 0.025f, 0.0f);
		}
	}
	else if (pSelf->m_motionState.m_moving)
	{
		ret.m_goalRadius = minGoalRadius;
	}
	else if (const NavControl* pNavControl = pNavChar->GetNavControl())
	{
		ret.m_goalRadius = pNavControl->GetIdleNavAdjustRadius();
	}
	else
	{
		ret.m_goalRadius = minGoalRadius;
	}

	if (reachedTypeContinue && (pSelf->m_moveToTypeContinueMatchAnim != INVALID_STRING_ID_64)
		&& pSelf->m_poseMatchInfo.m_valid)
	{
		ret.m_speedAtGoal = pSelf->m_poseMatchInfo.m_destAnimSpeed;
	}

	if (pSelf->IsCommandInProgress())
	{
		const PathWaypointsEx* pLivePathPs = pSelf->m_activePathInfo.GetPathWaypointsPs();
		if (pLivePathPs)
		{
			const Point lastPointOnPathPs = pLivePathPs->GetEndWaypoint();
			const Point curPathEndPs	  = ret.m_pathPs.GetEndWaypoint();

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

	if (pSelf->IsStrafing())
	{
		ret.m_strafeDirPs = pSelf->GetChosenFaceDirPs();
	}

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		pPathPs->DebugDraw(pNavChar->GetParentSpace(), true);
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
I32F AiLocomotionInterface::Mm_GroupFunc(const Character* pChar,
										 const MotionMatchingSet* pArtItemSet,
										 const DC::MotionMatchingSettings* pSettings,
										 const MotionModelIntegrator& integratorPs,
										 Point_arg trajectoryEndPosOs,
										 bool debug)
{
	const NavCharacter* pNavChar = NavCharacter::FromProcess(pChar);
	if (!pNavChar)
		return 0;

	const AnimationControllers* pAnimControllers = pNavChar->GetAnimationControllers();
	const AiLocomotionController* pSelf = (const AiLocomotionController*)pAnimControllers->GetLocomotionController();

	if (!pSelf)
		return 0;

	I32F desiredGroupIndex = 0;

	bool wantToMove = pSelf->m_desiredMotionState.m_moving;

	const PathWaypointsEx* pPathPs = pSelf->m_activePathInfo.GetPathWaypointsPs();
	const bool wantToStop = pSelf->m_waypointContract.m_reachedType == kNavGoalReachedTypeStop;

	if (pSettings && wantToMove && pPathPs && (pPathPs->GetWaypointCount() == 2) && wantToStop)
	{
		const float minGoalRadius = pNavChar->GetMotionConfig().m_minimumGoalRadius;
		const float stopDist = GetStoppingFaceDist(pSelf, pArtItemSet, minGoalRadius);

#if 1
		const Point stoppingPosPs = ApproximateStoppingPosition(integratorPs);
		const Point myPosPs = pChar->GetTranslationPs();
		const float remPathDist = pSelf->m_remainingPathDistance;
		const float pathDist = remPathDist >= 0.0f ? remPathDist : kLargeFloat;
		const float modelStopDist = Dist(myPosPs, stoppingPosPs);

		if (Abs(pathDist - modelStopDist) < stopDist)
		{
			wantToMove = false;
		}
#else
		const float distToGoal = Dist(trajectoryEndPosOs, kOrigin);

		if (distToGoal < stopDist)
		{
			wantToMove = false;
		}
#endif
	}

	if ((pSelf->IsCommandInProgress() || pSelf->HasArrivedAtGoal()) && wantToMove)
	{
		desiredGroupIndex = 1;
	}

	return desiredGroupIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
Vector AiLocomotionInterface::GetEffectiveGroundNormalPs(const NavCharacter* pNavChar,
														 bool stairsAllowed,
														 StairsStatus& stairsStatus)
{
	ANIM_ASSERT(pNavChar);

	const Vector charGroundNormalPs = pNavChar->GetGroundNormalPs();
	ANIM_ASSERT(IsReasonable(charGroundNormalPs));

	stairsStatus = StairsStatus::kNone;
	Vector groundNormalPs = charGroundNormalPs;

	if (!stairsAllowed)
	{
		return groundNormalPs;
	}

	const bool onStairs = pNavChar->OnStairs();

	LegRaycaster::PredictOnStairs::Result result = LegRaycaster::PredictOnStairs::Result::kInvalid;
	Vector predictOnStairsNormalWs = Vector(kUnitYAxis);

	const LegRaycaster* const pLegRaycaster = pNavChar->GetLegRaycaster();
	if (pLegRaycaster)
	{
		result = pLegRaycaster->GetPredictOnStairs().m_result;
		if (result != LegRaycaster::PredictOnStairs::Result::kInvalid)
		{
			predictOnStairsNormalWs = pLegRaycaster->GetPredictOnStairs().m_normalWs;
			ANIM_ASSERT(IsReasonable(predictOnStairsNormalWs));
		}
	}

	if (result == LegRaycaster::PredictOnStairs::Result::kInvalid)
	{	// Prediction invalid
		stairsStatus = onStairs ? StairsStatus::kOnStairs : StairsStatus::kNone;
	}
	else
	{	// Prediction valid
		if (!onStairs && result != LegRaycaster::PredictOnStairs::Result::kHitStairs)
		{
			// Has not been on stairs
			groundNormalPs = charGroundNormalPs;
		}
		else
		{
			// Has been on stairs, or about to enter, or about to exit
			const Locator& parentSpace = pNavChar->GetParentSpace();
			const Vector predictOnStairsNormalPs = parentSpace.UntransformVector(predictOnStairsNormalWs);
			ANIM_ASSERT(IsReasonable(predictOnStairsNormalPs));

			if (onStairs && result == LegRaycaster::PredictOnStairs::kHitGround)
			{
				stairsStatus = StairsStatus::kExiting;
			}
			else if (!onStairs && result == LegRaycaster::PredictOnStairs::kHitStairs)
			{
				stairsStatus = StairsStatus::kEntering;
			}
			else
			{
				stairsStatus = StairsStatus::kOnStairs;
			}

			groundNormalPs = predictOnStairsNormalPs;
		}
	}

	return groundNormalPs;
}
