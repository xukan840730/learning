/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/list-array.h"

#include "ndlib/resource/resource-table.h"
#include "ndlib/util/double-frame-ptr.h"
#include "ndlib/util/maybe.h"
#include "ndlib/util/tracker.h"

#include "gamelib/anim/motion-matching/gameplay-goal.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/path-waypoints.h"
#include "gamelib/scriptx/h/motion-matching-defines.h"

#include <functional>

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
	struct MotionModelSettings;
	struct MotionMatchingSettings;
	struct PointCurve;
}

class MotionModel;
class CatmullRom;
class NavPolyEdgeGatherer;
struct MotionModelInput;
class CharacterLocomotionInterface;

/// --------------------------------------------------------------------------------------------------------------- ///
typedef U8 HorseTurningState;
CONST_EXPR HorseTurningState kHorseTurningStateLeft		= 1ULL << 1; //2
CONST_EXPR HorseTurningState kHorseTurningStateRight	= 1ULL << 2; //4
CONST_EXPR HorseTurningState kHorseTurningStateEnter	= 1ULL << 3; //8
CONST_EXPR HorseTurningState kHorseTurningStateTurn		= 1ULL << 4; //16
CONST_EXPR HorseTurningState kHorseTurningStateExit		= 1ULL << 5; //32
CONST_EXPR HorseTurningState kHorseTurningStateUTurn	= 1Ull << 6; //64

CONST_EXPR HorseTurningState kHorseTurningStateOff			= 0;
CONST_EXPR HorseTurningState kHorseTurningStateEnterLeft	= kHorseTurningStateLeft	| kHorseTurningStateEnter;
CONST_EXPR HorseTurningState kHorseTurningStateEnterRight	= kHorseTurningStateRight	| kHorseTurningStateEnter;
CONST_EXPR HorseTurningState kHorseTurningStateTurnLeft		= kHorseTurningStateLeft	| kHorseTurningStateTurn;
CONST_EXPR HorseTurningState kHorseTurningStateTurnRight	= kHorseTurningStateRight	| kHorseTurningStateTurn;
CONST_EXPR HorseTurningState kHorseTurningStateExitLeft		= kHorseTurningStateLeft	| kHorseTurningStateExit;
CONST_EXPR HorseTurningState kHorseTurningStateExitRight	= kHorseTurningStateRight	| kHorseTurningStateExit;
CONST_EXPR HorseTurningState kHorseTurningStateUTurnLeft	= kHorseTurningStateLeft	| kHorseTurningStateUTurn;
CONST_EXPR HorseTurningState kHorseTurningStateUTurnRight	= kHorseTurningStateRight	| kHorseTurningStateUTurn;

/// --------------------------------------------------------------------------------------------------------------- ///
static const char* GetHorseCircleStateName(HorseTurningState s)
{
	switch (s)
	{
	case kHorseTurningStateOff:
		return "off";
		break;
	case kHorseTurningStateEnterLeft:
		return "enter-left";
		break;
	case kHorseTurningStateEnterRight:
		return "enter-right";
		break;
	case kHorseTurningStateTurnLeft:
		return "turn-left";
		break;
	case kHorseTurningStateTurnRight:
		return "turn-right";
		break;
	case kHorseTurningStateExitLeft:
		return "exit-left";
		break;
	case kHorseTurningStateExitRight:
		return "exit-right";
		break;
	case kHorseTurningStateUTurnLeft:
		return "U-turn-left";
		break;
	case kHorseTurningStateUTurnRight:
		return "U-turn-right";
		break;
	default:
		return "INVALID";
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsCircleStrafeEnabled(HorseTurningState s)
{
	return s == kHorseTurningStateTurnLeft || s == kHorseTurningStateTurnRight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct MotionModelPathInput
{
	TPathWaypoints<16> m_pathPs;
	Quat m_goalRotPs = kIdentity;
	Maybe<Vector> m_strafeDirPs = MAYBE::kNothing;
	float m_stoppingFaceDist	= 0.0f;
	U32 m_pathId		= -1;
	float m_goalRadius	= 0.0f;
	float m_speedAtGoal = -1.0f;
	float m_decelKMult = 1.0f;
	bool m_stopAtGoal	= true;
	bool m_pathEndsAtGoal = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MotionModelInput
{
	MotionModelInput() {}

	MotionModelInput(const Locator& locPs, Vector_arg v, const Maybe<Vector>& f)
		: m_charLocPs(locPs), m_velDirPs(v), m_facingPs(f)
	{
	}

	Locator m_charLocPs		 = Locator(kIdentity);
	Vector m_velDirPs		 = kZero;
	Vector m_groundNormalPs	 = kUnitYAxis;
	Maybe<Vector> m_facingPs = MAYBE::kNothing;
	Maybe<Vector> m_facingBiasPs = MAYBE::kNothing;

	NavLocation m_charNavLoc;
	NavBlockerBits m_obeyedBlockers = NavBlockerBits(false);
	Nav::StaticBlockageMask m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;

	const MotionModelPathInput* m_pPathInput		 = nullptr;
	const CharacterLocomotionInterface* m_pInterface = nullptr;

	struct MotionModelHorseOptions
	{
		HorseTurningState m_horseTurningState	= kHorseTurningStateOff;
		Vector m_strafingVelPs					= kZero; // velocity to use for strafing without adjusting facing
		float m_strafingBlend					= 0.0f;
		float m_circleStrafeDriftBlend			= 0.0f;
		float m_maxTurnRateDps					= -1.0f;
		float m_maxSpeed						= -1.0f;
		bool m_useHorseStick					= false;
		bool m_requestImmediateStop				= false;
		bool m_fadingOutOfCircleStrafe			= false; // false if we receive stick input while exiting circle strafe
		bool m_forceFacingDir					= false;
		bool m_needToBackUp						= false;
	};

	MotionModelHorseOptions m_horseOptions;

	float m_baseYawSpeed = 0.0f;
	float m_translationSkew = 1.0f;

	bool m_avoidNavObstacles = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionModel
{
public:
	static Quat ComputeTurnQuatWithNoExternalBias(const Vector curFacing, const Vector desiredFacing);

	MotionModel();
	MotionModel(Point_arg pos, Vector_arg facing, Vector_arg vel, const Vector* pAccel);

	Point GetPos() const { return m_pos; }
	Vector GetVel() const { return m_velocity; }
	Vector GetDesiredVel() const { return m_desiredVelocity; }
	Vector GetPathVel() const { return IsFollowingPath() ? m_pathVelocity : m_velocity; }
	Vector GetAccel() const { return m_velSpring.m_speed; }
	Vector GetFacing() const { return m_facing; }
	float GetYawSpeed() const { return m_yawSpeed; }
	Vector GetDesiredFacing() const { return m_desiredFacing; }
	Maybe<Vector> GetUserFacing() const { return m_userFacing; }
	float GetSprungSpeed() const { return m_sprungSpeed; }
	float GetMaxSpeed() const { return m_maxSpeed; }
	bool ShouldApplyStrafeRotation() const { return m_userFacing.Valid() && m_applyStrafeRotation; }

	// only meaningful for horses
	Vector GetHorseFacingVel() const { return m_facingVelocity; }

	void SetDesiredVel(Vector_arg vel) { m_desiredVelocity = vel; }

	void Step(const Locator& refLocator,
			  const MotionModelInput& input,
			  const DC::MotionModelSettings* pSettings,
			  float deltaTime,
			  bool debug = false);

	void SetPos(Point_arg p) { m_pos = p; }
	void SetVel(Vector_arg v) { m_velocity = v; m_pathVelocity = v; }
	void SetAccel(Vector_arg a) { m_velSpring.m_speed = a; }
	void SetFacing(Vector_arg f) { m_facing = f; }
	void SetAnimationError(Vector_arg errorVec) { m_animError = errorVec; }

	void ApplyTransform(const Transform& xform)
	{
		m_pos	   = m_pos * xform;
		m_velocity = m_velocity * xform;

		m_desiredVelocity = m_desiredVelocity * xform;
		m_animError		  = m_animError * xform;

		m_velSpring.m_speed = m_velSpring.m_speed * xform;

		m_facing		= m_facing * xform;
		m_desiredFacing = m_desiredFacing * xform;

		if (m_userFacing.Valid())
		{
			m_userFacing = m_userFacing.Get() * xform;
		}

		m_pathVelocity = m_pathVelocity * xform;
	}

	float GetProceduralClampDist() const { return m_proceduralClampDist; }
	void ReduceProceduralClampDist(float amount)
	{
		m_proceduralClampDistShrunk = true;
		m_proceduralClampDist -= Max(0.0f, amount);
		m_proceduralClampDist = Max(0.0f, m_proceduralClampDist);
	}

	void SetCharInIdle(bool inIdle) { m_charInIdle = inIdle; }

	void DebugDraw(const Locator& refLocator) const;

	bool IsFollowingPath() const { return m_lastPathId != -1; }
	bool IsOnPath() const { return m_onPath; }
	float GetPathOffsetDist() const { return m_pathOffsetDist; }
	U32F GetNumPathCorners() const { return m_numEffectiveCorners; }
	float GetPathLength() const { return m_lastPathLength; }

	bool IsCustomDataValid() const { return m_customDataValid; }

	template <class T>
	T& GetCustomData()
	{
		STATIC_ASSERT(sizeof(T) <= ARRAY_COUNT(m_customDataBuf));
		STATIC_ASSERT(ALIGN_OF(T) <= ALIGN_OF(m_customDataBuf));

		T& ret = *PunPtr<T*>(m_customDataBuf);

		if (!m_customDataValid)
		{
			ret = T();
			m_customDataValid = true;
		}

		return ret;
	}

	template <class T>
	const T& GetCustomData() const
	{
		STATIC_ASSERT(sizeof(T) <= ARRAY_COUNT(m_customDataBuf));
		STATIC_ASSERT(ALIGN_OF(T) <= ALIGN_OF(m_customDataBuf));

		ANIM_ASSERT(m_customDataValid);

		return *PunPtr<const T*>(m_customDataBuf);
	}

	void ResetCustomData() { m_customDataValid = false; }

private:
	struct PathCorner
	{
		Point m_pos;
		float m_angleDeg;
	};

	struct EffectivePathCorner : PathCorner
	{
		float m_radiusMin;
		float m_radiusMax;
		float m_maxSpeed;
	};

	float Step_Stick(const Locator& refLocator,
					 const MotionModelInput& input,
					 const DC::MotionModelSettings* pSettings,
					 float deltaTime,
					 bool debugDraw);

	float Step_Path(const Locator& refLocator,
					const MotionModelInput& input,
					const DC::MotionModelSettings* pSettings,
					float deltaTime,
					bool debugDraw);

	float Step_PathTransition(const Locator& refLocator,
							  const MotionModelInput& input,
							  const DC::MotionModelSettings* pSettings,
							  float deltaTime,
							  bool debugDraw);

	float Step_StickHorseOld(const MotionModelInput& input,
							 const DC::MotionModelSettings* pSettings,
							 float deltaTime,
							 bool debugDraw);

	float Step_StickHorse(const MotionModelInput& input,
						  const DC::MotionModelSettings* pSettings,
						  float deltaTime,
						  bool debugDraw);

	float Step_Custom(const Locator& refLocator,
					  const MotionModelInput& input,
					  const DC::MotionModelSettings* pSettings,
					  float deltaTime,
					  bool debugDraw);

	void RecordNewCorners(const IPathWaypoints* pPath,
						  bool newPath,
						  float decelSpringK,
						  const DC::MotionModelSettings* pSettings);

	float GetCornerLimitedMaxSpeed(Point_arg pos,
								   float desiredSpeed,
								   float decelSpringK,
								   const DC::MotionModelSettings* pSettings) const;

	float GetTurnRateDps(const DC::MotionModelSettings* pSettings,
						 Vector_arg curFaceDir,
						 Vector_arg desFaceDir,
						 float* pNewFacingCosAngleOut = nullptr) const;

	bool IsOnPath(const MotionModelInput& input, const DC::MotionModelSettings* pSettings) const;

	void TurnTowards(Vector_arg desiredFacing, float deltaTime, const Maybe<Vector>& facingBiasPs, F32 turnRateDps);

	Point PlaneProjectPos(Point_arg p) const { return m_projectionEnabled ? m_projPlane.ProjectPoint(p) : p; }

	Vector PlaneProjectVector(Vector_arg v) const
	{
		return m_projectionEnabled ? SafeNormalize(m_projPlane.ProjectVector(v), kZero) * Length(v) : v;
	}

	Point m_pos;
	Vector m_velocity;
	Vector m_desiredVelocity;
	Vector m_animError;

	SpringTracker<Vector> m_velSpring;
	SpringTracker<float> m_speedSpring;

	Maybe<Vector> m_userFacing;
	Vector m_facing;
	Vector m_desiredFacing;
	Plane m_projPlane;

	float m_sprungSpeed;
	float m_maxSpeed;
	float m_proceduralClampDist;
	float m_yawSpeed;
	float m_pathOffsetDist;
	float m_turnRateDps;
	float m_lastTurnRateSelCosAngle;
	float m_forceTurnRate;
	float m_horseCircleModeFacingAdjust;

	bool m_charInIdle;
	bool m_proceduralClampDistShrunk;
	bool m_applyStrafeRotation;
	bool m_customDataValid;
	bool m_projectionEnabled;
	bool m_runningIntoWall;
	bool m_onPath;

	// Horse Model Exclusives
	DampedSpringTracker m_facingSpring;
	SpringTracker<float> m_driftSpring;
	float m_driftTheta;
	Vector m_facingVelocity; // velocity without strafing

	// path data
	U32 m_lastPathId = -1;
	float m_lastPathLength = -1.0f;
	Vector m_pathVelocity = kZero;

	static CONST_EXPR size_t kMaxSavedCorners = 16;
	PathCorner m_savedCorners[kMaxSavedCorners];
	EffectivePathCorner m_effectiveCorners[kMaxSavedCorners];
	U32 m_numSavedCorners = 0;
	U32 m_numEffectiveCorners = 0;
	mutable SingleFramePtr<CatmullRom> m_transitionPathPs;
	mutable SingleFramePtr<NavPolyEdgeGatherer> m_nearbyNavEdges;

	// custom align functors should use this to store internal state that needs to
	// be steppable and thus integratable // e.g. animation phase
	static CONST_EXPR size_t kCustomDataSize = 16;
	ALIGNED(16) U8 m_customDataBuf[kCustomDataSize];
};

/// --------------------------------------------------------------------------------------------------------------- ///
class IMotionModelSettingsSupplier
{
public:
	virtual ~IMotionModelSettingsSupplier() {}
	virtual DC::MotionModelSettings GetSettings(TimeFrame timeInFuture) const = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionModelSettingsSupplier : public IMotionModelSettingsSupplier
{
public:
	MotionModelSettingsSupplier(const DC::MotionModelSettings* pSettings)
		: m_pSettings(pSettings)
	{
	}

	virtual DC::MotionModelSettings GetSettings(TimeFrame timeInFuture) const override
	{
		return *m_pSettings;
	}

private:
	const DC::MotionModelSettings* m_pSettings;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionModelIntegrator
{
public:
	MotionModelIntegrator(const MotionModel& model, const IMotionModelSettingsSupplier* pSettingsSupplier)
		: m_model(model), m_pSettingsSupplier(pSettingsSupplier), m_timeInFuture(Seconds(0.0))
	{
	}

	const MotionModel& Model() const { return m_model; }
	MotionModelIntegrator Step(const MotionModelInput& input, float deltaTime) const;
	TimeFrame Time() const { return m_timeInFuture; }
	float GetMaxSpeed() const;
	float GetMaxAccel() const;
	DC::MotionModelSettings GetSettings() const;

	void SetTime(TimeFrame timeInFuture) { m_timeInFuture = timeInFuture; }

	static float MaxSpeed(const DC::MotionModelSettings& pSettings);
	static float MaxAccel(const MotionModel& model, const DC::MotionModelSettings& pSettings);

private:
	MotionModel m_model;
	const IMotionModelSettingsSupplier* m_pSettingsSupplier;
	TimeFrame m_timeInFuture;
};

/// --------------------------------------------------------------------------------------------------------------- ///
using MotionModelInputFunc = std::function<MotionModelInput(const MotionModelIntegrator& integeratorPs,
															TimeFrame delta,
															bool finalize)>;

/// --------------------------------------------------------------------------------------------------------------- ///
float ApproximateStoppingDistance(const float vel, const float accel, const float springConst);
const Point ComputeStoppingPosition(const MotionModelIntegrator& model);
const Point ApproximateStoppingPosition(const MotionModelIntegrator& model);

/// --------------------------------------------------------------------------------------------------------------- ///
float EvaluateDirectionalCurve(const DC::PointCurve* pCurve,
							   Vector_arg moveDir,
							   Maybe<Vector> maybeFaceDir = MAYBE::kNothing);

float EvaluateDirectionalCurveWithHint(const DC::PointCurve* pCurve,
									   Vector_arg moveDir,
									   Maybe<Vector> maybeFaceDir = MAYBE::kNothing,
									   Maybe<bool> hintDirLeft = MAYBE::kNothing,
									   float hintAdjustStrength = 1.0f);

float GetMaxSpeedForDirection(const DC::MotionModelSettings* pSettings,
							  Vector_arg moveDir,
							  Maybe<Vector> maybeFaceDir = MAYBE::kNothing);

float GetVelKForDirection(const DC::MotionModelSettings* pSettings,
						  Vector_arg moveDir,
						  Maybe<Vector> maybeFaceDir = MAYBE::kNothing,
						  float curSpeed = -1.0f);

float GetVelKDecelForDirection(const DC::MotionModelSettings* pSettings,
							   Vector_arg moveDir,
							   Maybe<Vector> maybeFaceDir = MAYBE::kNothing);

float GetTurnRateDpsForDirection(const DC::MotionModelSettings* pSettings,
								 Vector_arg curFaceDir,
								 Vector_arg desFaceDir,
								 float curSpeed = -1.0f);

/// --------------------------------------------------------------------------------------------------------------- ///
void GetDesiredTrajectory(const Locator& refLocator,
						  const Locator& parentSpace,
						  MotionModel model,
						  const IMotionModelSettingsSupplier* pSettings,
						  int numSamples,
						  float endTime,
						  MotionModelInputFunc futureStick,
						  AnimTrajectory* pTrajectoryOut,
						  bool debug = false);

/// --------------------------------------------------------------------------------------------------------------- ///
Vector SolveInputDirToFollowSplinePs(const CatmullRom* pSplinePs,
									 const MotionModelIntegrator& integratorPs,
									 Maybe<Vector> facingDir,
									 TimeFrame delta);
/// --------------------------------------------------------------------------------------------------------------- ///
class IObstacles
{
public:
	virtual Segment GetObstacleWs(int i) const = 0;
	virtual int NumObstacles() const = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavObstacles : public IObstacles
{
public:
	NavObstacles(const NavPolyEdgeGatherer* pEdges) : m_pEdges(pEdges) {}

	virtual Segment GetObstacleWs(int i) const override;
	virtual int NumObstacles() const override;

private:
	const NavPolyEdgeGatherer* m_pEdges;
};

/// --------------------------------------------------------------------------------------------------------------- ///
Vector ApplyObstacleAvoidancePs(Vector_arg desInputPs,
								const Locator& parentSpace,
								const MotionModelIntegrator& integratorPs,
								const IObstacles& obstacles,
								float avoidanceRadius,
								const Point* pStoppingPosPs = nullptr,
								bool debugDraw = false);

/// --------------------------------------------------------------------------------------------------------------- ///
Vector ApplyObstacleAvoidancePs(Vector_arg desInputPs,
								const Locator& parentSpace,
								Point_arg posPs,
								Point_arg futurePosPs,
								const IObstacles& obstacles,
								float avoidanceRadius,
								Point* pClosestEdgePosOut /* = nullptr */,
								bool debugDraw = false);

/// --------------------------------------------------------------------------------------------------------------- ///
Point ClosestPointOnPath(Point_arg pos,
						 const IPathWaypoints* pPath,
						 I32F* pClosestLegOut,
						 bool stopAtGoal,
						 I32F iStartingLeg = -1,
						 F32 bias = 0.0f);

Point AdvanceClosestPointOnPath(Point_arg pos, I32F closestLeg, const IPathWaypoints* pPath, float distance);
