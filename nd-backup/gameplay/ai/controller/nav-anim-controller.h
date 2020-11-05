/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"

#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/ai/waypoint-contract.h"
#include "gamelib/gameplay/nav/action-pack-entry-def.h"
#include "gamelib/gameplay/nav/nav-command.h"
#include "gamelib/gameplay/nav/path-waypoints-ex.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NavLocation;
class NdGameObject;
class SsAction;

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
	struct NavAnimHandoffParams;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavAnimStartParams
{
	const PathWaypointsEx*	m_pPathPs = nullptr;
	WaypointContract		m_waypoint;
	Vector					m_moveDirPs = kZero;

	NavMoveArgs m_moveArgs;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavAnimStopParams
{
	WaypointContract	m_waypoint;
	float				m_goalRadius;
	bool				m_faceValid = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavAnimHandoffDesc
{
	explicit NavAnimHandoffDesc()
	{
		Reset();
	}

	void Reset()
	{
		m_animRequestId = StateChangeRequest::kInvalidId;
		m_animStateId	= INVALID_ANIM_INSTANCE_ID;
		m_motionType	= kMotionTypeMax;

		m_steeringPhase	   = -1.0f;
		m_mmMaxDurationSec = -1.0f;
		m_mmMinPathLength  = -1.0f;
		m_exitPhase		   = 1.0f;

		m_stateNameId	 = INVALID_STRING_ID_64;
		m_exitModeId	 = INVALID_STRING_ID_64;
		m_handoffMmSetId = INVALID_STRING_ID_64;
	
		m_subsystemControllerId = 0;
	}

	bool IsValid(const NdGameObject* pChar) const;
	void SetStateChangeRequestId(StateChangeRequest::ID id, StringId64 stateNameId);
	void SetAnimStateInstance(const AnimStateInstance* pInst);
	void SetSubsystemControllerId(U32 id, StringId64 stateNameId);

	AnimStateInstance::ID GetAnimStateId(const NdGameObject* pChar) const;
	StateChangeRequest::ID GetChangeRequestId() const { return m_animRequestId; }
	U32 GetSubsystemControllerId() const { return m_subsystemControllerId; }

	void ConfigureFromDc(const DC::NavAnimHandoffParams* pDcParams);

	bool ShouldUpdateFrom(const NdGameObject* pChar, const NavAnimHandoffDesc& rhs) const;

	StringId64 GetStateNameId() const { return m_stateNameId; }

	MotionType m_motionType;
	float m_steeringPhase;
	float m_mmMaxDurationSec;
	float m_mmMinPathLength;
	float m_exitPhase;

	StringId64 m_exitModeId;
	StringId64 m_handoffMmSetId;

private:
	StringId64 m_stateNameId;

	StateChangeRequest::ID m_animRequestId;
	mutable AnimStateInstance::ID m_animStateId;
	U32 m_subsystemControllerId;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class INavAnimController : public AnimActionController
{
public:
	virtual bool IsNavAnimController() const override { return true; }

	virtual void Activate() {}

	virtual Point GetNavigationPosPs() const = 0;

	virtual void StartNavigating(const NavAnimStartParams& params) = 0;
	virtual void StopNavigating(const NavAnimStopParams& params) = 0;
	virtual bool CanProcessCommand() const = 0;
	virtual bool IsCommandInProgress() const = 0;

	virtual bool HasArrivedAtGoal() const = 0;
	virtual bool HasMissedWaypoint() const = 0;
	virtual NavGoalReachedType GetCurrentGoalReachedType() const = 0;
	virtual Locator GetGoalPs() const = 0;

	virtual bool IsMoving() const = 0;
	virtual bool IsInterrupted() const = 0;

	virtual void RequestMotionType(MotionType motionType) {}
	virtual bool HasMoveSetFor(MotionType mt) const { return false; }
	virtual bool IsStrafing() const { return false; }
	virtual bool CanStrafe(MotionType mt) const { return false; }

	virtual void UpdatePathWaypointsPs(const PathWaypointsEx& pathWaypointsPs, const WaypointContract& waypointContract) = 0;
	virtual void PatchCurrentPathEnd(const NavLocation& updatedEndLoc) = 0;
	virtual void InvalidateCurrentPath() = 0;

	virtual void UpdateMoveDirPs(Vector_arg moveDirPs) {}

	virtual void SetMoveToTypeContinuePoseMatchAnim(const StringId64 animId, float startPhase) = 0;

	virtual bool ShouldDoProceduralApRef(const AnimStateInstance* pInstance) const = 0;
	
	virtual bool CalculateProceduralAlign(const AnimStateInstance* pInstance,
										  const Locator& baseAlignPs,
										  Locator& alignOut) const = 0;

	virtual bool CalculateProceduralApRef(AnimStateInstance* pInstance) const = 0;

	virtual bool ShouldAdjustHeadToNav() const = 0;
	virtual U32 GetDesiredLegRayCastMode() const = 0;

	virtual const char* GetDebugStatusStr() const { return ""; }

	virtual void ForceDefaultIdleState(bool playAnim = true) = 0;
	virtual StateChangeRequest::ID FadeToIdleState(const DC::BlendParams* pBlend = nullptr) = 0;

	virtual float GetMovementSpeedScale() const { return 1.0f; }

	struct IdlePerfParams
	{
		const SsAction* m_pWaitAction = nullptr;
		float m_fadeTime = -1.0f;
		DC::AnimCurveType m_animCurveType = DC::kAnimCurveTypeInvalid;
		bool m_loop = false;
		bool m_disableClearanceCheck = false;
		bool m_allowInExistingIdlePerformance = false;
		bool m_phaseSync = false;
		bool m_mirror = false;
	};

	virtual bool IsIdlePerformanceAllowed(bool allowExistingIdlePerformance = false,
										  IStringBuilder* pReasonOut		= nullptr) const
	{
		return false;
	}
	virtual bool RequestIdlePerformance(StringId64 animId,
										const IdlePerfParams& params,
										IStringBuilder* pErrStrOut = nullptr)
	{
		return false;
	}
	virtual bool IsPlayingIdlePerformance() const { return false; }

	virtual void FadeToIdleStateAndGoStopped(const DC::BlendParams* pBlend = nullptr) {}

	virtual bool IsPlayingDemeanorChangeAnim() const { return false; }

	virtual void ConvertToFromCheap() {}

	virtual float GetMaxMovementSpeed() const { return 0.0f; }
	virtual float EstimateRemainingTravelTime() const { return -1.0f; }

protected:
	class PathInfo
	{
	public:
		PathInfo() { Reset(); }

		void Reset()
		{
			m_valid = false;
			m_waypointsPs.Clear();
			m_orgPathEndPs = kOrigin;
			m_goalPlanePs = Plane();
		}

		bool IsValid() const { return m_valid && m_waypointsPs.IsValid(); }

		void SetPathWaypointsPs(const PathWaypointsEx& waypointsPs, bool resetEndPoint = true)
		{
			bool wasValid = IsValid();

			if (waypointsPs.IsValid())
			{
				m_valid = true;
				const Point prevEndPs = (m_waypointsPs.GetWaypointCount() > 0) ? m_waypointsPs.GetEndWaypoint() : kOrigin;
				m_waypointsPs = waypointsPs;

				if (resetEndPoint || !wasValid)
				{
					m_orgPathEndPs = waypointsPs.GetEndWaypoint();
					const Point penultimatePosPs = waypointsPs.GetWaypoint(waypointsPs.GetWaypointCount() - 2);
					const Vector approachDirXzPs = SafeNormalize(VectorXz(m_orgPathEndPs - penultimatePosPs), kUnitZAxis);
					m_goalPlanePs = Plane(m_orgPathEndPs, -approachDirXzPs);
				}
			}
			else
			{
				Reset();
			}
		}

		bool CrossesGoalPlanePs(Point_arg p0Ps, Point_arg p1Ps) const
		{
			if (!IsValid())
				return false;

			const Point navGoalPs = m_waypointsPs.GetEndWaypoint();

			const Vector planeNormPs = m_goalPlanePs.GetNormal();
			const Scalar curDot = Dot(planeNormPs, p0Ps - navGoalPs);
			const Scalar desDot = Dot(planeNormPs, p1Ps - navGoalPs);

			return (Sign(curDot) != Sign(desDot));
		}

		Point GetOrgEndWaypointPs() const { return m_orgPathEndPs; }

		PathWaypointsEx* GetPathWaypointsPs() { return IsValid() ? &m_waypointsPs : nullptr; }
		const PathWaypointsEx* GetPathWaypointsPs() const { return IsValid() ? &m_waypointsPs : nullptr; }

		U32F GetWaypointCount() const
		{
			if (IsValid())
			{
				return m_waypointsPs.GetWaypointCount();
			}

			return 0;
		}

		float ComputePathLengthXz() const
		{
			return IsValid() ? m_waypointsPs.ComputePathLengthXz() : 0.0f;
		}

		const Plane& GetGoalPlanePs() const { return m_goalPlanePs; }

	private:
		bool m_valid;
		Point m_orgPathEndPs;
		Plane m_goalPlanePs;
		PathWaypointsEx m_waypointsPs;
	};
};
