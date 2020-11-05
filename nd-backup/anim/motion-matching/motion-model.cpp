/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/motion-matching/motion-model.h"

#include "corelib/math/golden-section-min.h"
#include "corelib/math/segment-util.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/util/pd-tracker.h"
#include "ndlib/util/point-curve.h"

#include "gamelib/anim/motion-matching/motion-matching.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/character-locomotion.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-poly-edge-gatherer.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/scriptx/h/motion-matching-defines.h"
#include "gamelib/spline/catmull-rom.h"

/// --------------------------------------------------------------------------------------------------------------- ///
// might want to move these to settings?
static const float kPathAdvanceNudgeDist = 0.05f;
static const float kMinStopDist = 0.1f;
static const float kVelocityLPFAlpha = 12.5f;

/// --------------------------------------------------------------------------------------------------------------- ///
struct SpringVelFunParams
{
	float m_a;
	float m_b;
	float m_w0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static float SpringVel(float t, uintptr_t data)
{
	const SpringVelFunParams* pParams = reinterpret_cast<const SpringVelFunParams*>(data);
	return (pParams->m_a + (pParams->m_b * t)) * Exp(-pParams->m_w0 * t);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ApproximateStoppingDistance(const float vel, const float accel, const float springConst)
{
	const float x0 = vel;
	const float w0 = Sqrt(springConst);
	const float A  = x0;
	const float B  = accel + w0 * x0;

	SpringVelFunParams params;
	params.m_a	= A;
	params.m_b	= B;
	params.m_w0 = w0;

	//auto vT = [A, B, w0](const float t){ (A + B*t)*Exp(-w0*t); };
	const float endTime = Ln(0.01f) / -w0;

	return IntegrateGaussLegendre5(0, endTime, SpringVel, reinterpret_cast<uintptr_t>(&params));
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float ApproximateStoppingDistance(Vector_arg vel, Vector_arg accel, const float springConst)
{
	const float kSqr = Sqr(springConst);

	const float stopDistX = ApproximateStoppingDistance(vel.X(), accel.X(), kSqr);
	const float stopDistZ = ApproximateStoppingDistance(vel.Z(), accel.Z(), kSqr);
	const float stopDist = Length(Vector(stopDistX, 0.0f, stopDistZ));

	return stopDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetRemainingPathDistXz(const IPathWaypoints* pPathPs, U32F iPathLeg, Point_arg pos)
{
	NAV_ASSERT(pPathPs);

	float dist = 0.0f;

	const U32F numWaypoints = pPathPs->GetWaypointCount();

	if (iPathLeg < numWaypoints)
	{
		Point prevPos = pos;
		Point nextPos = pPathPs->GetWaypoint(iPathLeg);

		dist += DistXz(pos, nextPos);

		for (U32F i = iPathLeg + 1; i < numWaypoints; ++i)
		{
			prevPos = nextPos;
			nextPos = pPathPs->GetWaypoint(i);

			dist += DistXz(prevPos, nextPos);
		}
	}

	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModel::MotionModel()
	: m_pos(kOrigin)
	, m_velocity(kZero)
	, m_facingVelocity(kZero)
	, m_pathVelocity(kZero)
	, m_desiredVelocity(kZero)
	, m_desiredFacing(kUnitZAxis)
	, m_facing(kUnitZAxis)
	, m_userFacing(MAYBE::kNothing)
	, m_lastPathId(-1)
	, m_proceduralClampDist(0.0f)
	, m_numSavedCorners(0)
	, m_animError(kZero)
	, m_pathOffsetDist(0.0f)
	, m_charInIdle(false)
	, m_applyStrafeRotation(true)
	, m_forceTurnRate(-1.0)
	, m_horseCircleModeFacingAdjust(0.0)
	, m_proceduralClampDistShrunk(false)
	, m_turnRateDps(-1.0f)
	, m_lastTurnRateSelCosAngle(-1.0f)
	, m_customDataValid(false)
	, m_projectionEnabled(false)
	, m_runningIntoWall(false)
	, m_onPath(false)
	, m_projPlane(kOrigin, kUnitYAxis)
	, m_maxSpeed(-1.0f)
	, m_yawSpeed(0.0f)
	, m_driftTheta(0.0f)
	, m_sprungSpeed(0.0f)
{
	m_velSpring.Reset();
	m_speedSpring.Reset();
	m_facingSpring.Reset();
	m_driftSpring.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModel::MotionModel(Point_arg pos, Vector_arg facing, Vector_arg vel, const Vector* pAccel)
	: m_pos(pos)
	, m_velocity(vel)
	, m_pathVelocity(vel)
	, m_desiredVelocity(vel)
	, m_lastPathId(-1)
	, m_proceduralClampDist(0.0f)
	, m_numSavedCorners(0)
	, m_animError(kZero)
	, m_pathOffsetDist(0.0f)
	, m_charInIdle(false)
	, m_applyStrafeRotation(true)
	, m_forceTurnRate(-1.0)
	, m_horseCircleModeFacingAdjust(0.0)
	, m_proceduralClampDistShrunk(false)
	, m_turnRateDps(-1.0f)
	, m_lastTurnRateSelCosAngle(-1.0f)
	, m_customDataValid(false)
	, m_projectionEnabled(false)
	, m_runningIntoWall(false)
	, m_onPath(false)
	, m_projPlane(kOrigin, kUnitYAxis)
	, m_sprungSpeed(Length(vel))
	, m_maxSpeed(-1.0f)
	, m_yawSpeed(0.0f)
	, m_driftTheta(0.0f)
	, m_userFacing(MAYBE::kNothing)
{
	m_velSpring.Reset();
	m_speedSpring.Reset();
	m_facingSpring.Reset();
	m_driftSpring.Reset();

	m_facing = AsUnitVectorXz(facing, kUnitZAxis);
	m_desiredFacing = facing;
	m_facingVelocity = facing * Length(vel);

	if (pAccel)
	{
		m_velSpring.m_speed = *pAccel;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionModel::Step(const Locator& refLocator,
					   const MotionModelInput& input,
					   const DC::MotionModelSettings* pSettings,
					   float deltaTime,
					   bool debug /* = false */)
{
	PROFILE(Animation, MotionModel_Step);

	const Point prevPos = m_pos;

	const Vector upPs = GetLocalY(input.m_charLocPs);
	Vector planeNormalPs = upPs;

	if ((pSettings->m_projectionMode == DC::kProjectionModeGroundPlane)
		&& (Abs(Length(input.m_groundNormalPs) - 1.0f) < 0.1f))
	{
		planeNormalPs = input.m_groundNormalPs;
	}

	m_projPlane = Plane(input.m_charLocPs.Pos(), planeNormalPs);
	m_projectionEnabled = pSettings->m_projectionMode != DC::kProjectionModeNone;

	float desiredClampDist = 0.0f;
	m_applyStrafeRotation = true;
	m_forceTurnRate = -1.0f;
	m_lastPathLength = -1.0f;

	const Vector prevFacing = m_facing;
	bool doingCustomStep = false;

	bool onPath = false;

	if (input.m_pInterface && input.m_pInterface->HasCustomModelStep())
	{
		m_runningIntoWall = false;
		m_lastPathId = -1;
		m_pathOffsetDist = 0.0f;

		desiredClampDist = Step_Custom(refLocator, input, pSettings, deltaTime, debug);
		doingCustomStep = true;
	}
	else if (input.m_pPathInput && input.m_pPathInput->m_pathPs.IsValid())
	{
		if (input.m_pPathInput->m_pathId != m_lastPathId)
		{
			m_turnRateDps = -1.0f;
			m_velSpring.Reset();
			m_speedSpring.Reset();
			m_facingSpring.Reset();
			m_driftSpring.Reset();
		}

		if (IsOnPath(input, pSettings))
		{
			onPath = true;

			m_runningIntoWall = false;

			desiredClampDist = Step_Path(refLocator, input, pSettings, deltaTime, debug);
		}
		else
		{
			desiredClampDist = Step_PathTransition(refLocator, input, pSettings, deltaTime, debug);
		}

		m_lastPathId = input.m_pPathInput->m_pathId;
		m_customDataValid = false;
	}
	else
	{
		m_runningIntoWall = false;
		m_lastPathId = -1;
		m_customDataValid = false;
		m_pathOffsetDist = 0.0f;

		desiredClampDist = input.m_horseOptions.m_useHorseStick
							   ? Step_StickHorse(input, pSettings, deltaTime, debug)
							   : Step_Stick(refLocator, input, pSettings, deltaTime, debug);
	}

	ANIM_ASSERT(desiredClampDist <= pSettings->m_transClampDist);

	if (m_proceduralClampDistShrunk && (m_onPath == onPath))
	{
		m_proceduralClampDistShrunk = false;
	}
	else
	{
		const float kProcClampAlpha = 10.0f;
		const float clampTT = Limit01(kProcClampAlpha * deltaTime);
		const float filteredClampDist = ((desiredClampDist - m_proceduralClampDist) * clampTT) + m_proceduralClampDist;

		m_proceduralClampDist = Max(filteredClampDist, desiredClampDist);
	}

	m_onPath = onPath;

	const float oldFacingCosAngle = Dot(m_facing, m_desiredFacing);

	if (oldFacingCosAngle > g_motionMatchingOptions.m_turnRateResetCosAngle)
	{
		m_turnRateDps = -1.0f;
	}

	if (m_userFacing.Valid())
	{
		ANIM_ASSERT(IsReasonable(m_userFacing.Get()));

		m_desiredFacing = m_userFacing.Get();

		m_turnRateDps = GetTurnRateDps(pSettings, m_facing, m_desiredFacing, &m_lastTurnRateSelCosAngle);

		TurnTowards(m_desiredFacing, deltaTime, input.m_facingBiasPs, m_turnRateDps);
	}
	else if (!doingCustomStep)
	{
		m_desiredFacing = SafeNormalize(ProjectOntoPlane(GetDesiredVel(), upPs), input.m_facingPs.Otherwise(m_facing));

		if (pSettings->m_alwaysInterpolateFacing)
		{
			m_turnRateDps = GetTurnRateDps(pSettings, m_facing, m_desiredFacing, &m_lastTurnRateSelCosAngle);

			TurnTowards(m_desiredFacing, deltaTime, input.m_facingBiasPs, m_turnRateDps);
		}
		else
		{
			m_facing = m_desiredFacing;
		}
	}

	const Angle prevAngle = Angle::FromRadians(Atan2(prevFacing.X(), prevFacing.Z()));
	const Angle nextAngle = Angle::FromRadians(Atan2(m_facing.X(), m_facing.Z()));
	const float diffRad	  = DEGREES_TO_RADIANS(nextAngle.AngleDiff(prevAngle));

 	m_yawSpeed = input.m_baseYawSpeed * pSettings->m_baseYawSpeedFactor;
	m_yawSpeed += ((deltaTime > 0.0f) ? (diffRad / deltaTime) : 0.0f);

	ANIM_ASSERT(IsReasonable(input.m_groundNormalPs));

	if ((Abs(input.m_translationSkew - 1.0f) > NDI_FLT_EPSILON) && (input.m_translationSkew > 0.0f)
		&& !FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_disableTranslationSkewing))
	{
		const Vector deltaPosPs = m_pos - prevPos;
		const Point scaledPosPs = prevPos + (deltaPosPs * input.m_translationSkew);

		m_pos = PlaneProjectPos(scaledPosPs);
	}

	ANIM_ASSERT(IsReasonable(m_pos));

	if (debug)
	{
		const Point pos = GetPos();
		const Vector curFacing = GetFacing();
		const Vector desFacing = GetDesiredFacing();

		PrimServerWrapper ps(refLocator);
		ps.SetDuration(kPrimDuration1FrameAuto);
		ps.EnableHiddenLineAlpha();

		ps.DrawCross(pos, 0.1f, kColorOrange);
		ps.DrawArrow(pos, curFacing * 0.2f, 0.1f, kColorOrangeTrans);
		ps.DrawArrow(pos, SafeNormalize(m_velocity, kZero) * 0.2f, 0.1f, kColorGreenTrans);

		//ps.DrawArrow(pos, input.m_groundNormalPs, 0.25f, kColorGrayTrans);

		if (DotXz(curFacing, desFacing) < 0.95f)
		{
			ps.DrawArrow(pos, desFacing * 0.2f, 0.1f, kColorCyanTrans);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
Quat MotionModel::ComputeTurnQuatWithNoExternalBias(const Vector curFacing, const Vector desiredFacing)
{
	Quat deltaQuat;

	const float dotP = Dot(curFacing, desiredFacing);
	if (Abs(dotP) > 0.99f)
	{
		const Vector bias = curFacing.Y() > 0.9f ? Cross(curFacing, kUnitXAxis) : Cross(curFacing, kUnitYAxis);

		deltaQuat = QuatFromVectorsBiased(curFacing, desiredFacing, bias);
	}
	else
	{
		deltaQuat = QuatFromVectors(curFacing, desiredFacing);
	}

	return deltaQuat;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionModel::TurnTowards(Vector_arg desiredFacing,
							  float deltaTime,
							  const Maybe<Vector>& facingBiasPs,
							  F32 turnRateDps)
{
	const float maxTurnRad = DEGREES_TO_RADIANS(turnRateDps * deltaTime);

	Quat deltaQuat = kIdentity;

	const float dotP = Dot(m_facing, desiredFacing);

	if (facingBiasPs.Valid() && (dotP < 0.0f))
	{
		const Vector bias = facingBiasPs.Get();
		deltaQuat = QuatFromVectorsBiased(m_facing, desiredFacing, bias);

#if ENABLE_ANIM_ASSERTS
		const Vector rotTest = Rotate(deltaQuat, m_facing);
		const float testDot = Dot(rotTest, desiredFacing);
		ANIM_ASSERTF(testDot > 0.99f,
					 ("QuatFromVectorsBiased failed %s -> %s (%s) = %s but rotTest is %s (dot: %f)",
					  PrettyPrint(m_facing),
					  PrettyPrint(m_desiredFacing),
					  PrettyPrint(bias),
					  PrettyPrint(deltaQuat),
					  PrettyPrint(rotTest),
					  testDot));
#endif
	}
	else
	{
		deltaQuat = ComputeTurnQuatWithNoExternalBias(m_facing, desiredFacing);
	}

	const Quat turnQuat = LimitQuatAngle(deltaQuat, maxTurnRad);

	m_facing = Rotate(turnQuat, m_facing);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionModel::DebugDraw(const Locator& refLocator) const
{
	STRIP_IN_FINAL_BUILD;

	PrimServerWrapper ps(refLocator);

	ps.EnableHiddenLineAlpha();

	const Point posPs = GetPos() + Vector(0.0f, 0.1f, 0.0f);
	ps.DrawCross(posPs, 0.15f, kColorBlue);
	ps.DrawCircle(posPs, kUnitYAxis, GetProceduralClampDist(), kColorBlueTrans);

	const float sprungSpeed = GetSprungSpeed();
	StringBuilder<256> desc;
	desc.append_format("%0.1fm/s [sprung: %0.1fm/s %0.1frad/s]", (float)Length(GetVel()), sprungSpeed, m_yawSpeed);

	if (m_turnRateDps >= 0.0f)
	{
		desc.append_format(" [turn: %0.1fdeg/s]", m_turnRateDps);
	}

	if (m_runningIntoWall)
	{
		desc.append("\n RUNNING INTO WALL\n");
	}

	if (m_proceduralClampDistShrunk)
	{
		desc.append("\n Clamp Dist Shrunk\n");
	}

	ps.DrawString(posPs, desc.c_str(), kColorCyanTrans, 0.5f);

	const Vector curFacing = GetFacing();
	const Vector desFacing = GetDesiredFacing();

	ps.DrawArrow(posPs, curFacing * 0.25f, 0.1f, kColorBlueTrans);
	ps.DrawArrow(posPs, SafeNormalize(m_velocity, kZero) * 0.25f, 0.1f, kColorGreenTrans);

	if (Dot(curFacing, desFacing) < 0.95f)
	{
		ps.DrawArrow(posPs, desFacing * 0.25f, 0.1f, kColorCyanTrans);
	}

	if (Length(m_animError) >= NDI_FLT_EPSILON)
	{
		ps.DrawArrow(posPs, m_animError, 0.1f, kColorRedTrans);
	}

	if (g_motionMatchingOptions.m_drawOptions.m_drawPathCorners)
	{
		for (U32F iCorner = 0; iCorner < m_numSavedCorners; ++iCorner)
		{
			const PathCorner& corner = m_savedCorners[iCorner];

			const Point cornerPos = refLocator.TransformPoint(corner.m_pos);

			ps.DrawCross(cornerPos, 0.1f, kColorCyan);
			ps.DrawString(cornerPos + Vector(0.0f, 0.1f, 0.0f), StringBuilder<64>("s-%d %0.1fdeg", (int)iCorner, corner.m_angleDeg).c_str(), kColorWhiteTrans, 0.5f);
		}

		for (U32F iCorner = 0; iCorner < m_numEffectiveCorners; ++iCorner)
		{
			const EffectivePathCorner& corner = m_effectiveCorners[iCorner];

			const Point cornerPos = refLocator.TransformPoint(corner.m_pos);

			ps.DrawCross(cornerPos, 0.1f, kColorBlue);
			ps.DrawString(cornerPos, StringBuilder<64>("%0.1fdeg / %0.1fm/s", corner.m_angleDeg, corner.m_maxSpeed).c_str(), kColorWhiteTrans, 0.5f);
			ps.DrawCircle(cornerPos, kUnitYAxis, corner.m_radiusMin, kColorBlue);
			ps.DrawCircle(cornerPos, kUnitYAxis, corner.m_radiusMax, kColorBlueTrans);
		}
	}

	if (false)
	{
		const Vector initialVel = m_pathVelocity; //Vector(3.0f, 0.0f, 1.0f); //kUnitZAxis;
		const float springK = 4.5f;

		const float approxDist = ApproximateStoppingDistance(initialVel, kZero, springK);

		float trueDist = 0.0f;
		SpringTracker<Vector> vecSpring;

		U32F numFrames = 0;
		Vector curVel = initialVel;
		const float dt = ToSeconds(Frames(1));

		while (true)
		{
			trueDist += Length(curVel) * dt;
			curVel = vecSpring.Track(curVel, kZero, dt, springK);
			++numFrames;

			if (Length(curVel) < NDI_FLT_EPSILON)
				break;
		}

		MsgCon("approx: %fm\n", approxDist);
		MsgCon("true:   %fm (%d frames / %0.2f seconds)\n", trueDist, numFrames, ToSeconds(Frames(numFrames)));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionModel::Step_Stick(const Locator& refLocator,
							  const MotionModelInput& input,
							  const DC::MotionModelSettings* pSettings,
							  float deltaTime,
							  bool debugDraw)
{
	PROFILE(Animation, MM_StepStick);

	ANIM_ASSERT(IsReasonable(deltaTime));
	ANIM_ASSERT(pSettings);
	ANIM_ASSERT(!input.m_facingPs.Valid() || IsReasonable(input.m_facingPs.Get()));

	m_pos += m_animError;
	m_animError = kZero;

	m_userFacing = input.m_facingPs;

	Vector desiredModelDirPs = PlaneProjectVector(input.m_velDirPs);

	if (input.m_avoidNavObstacles)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		if (!m_nearbyNavEdges.Valid())
		{
			if (const NavPoly* pPoly = input.m_charNavLoc.ToNavPoly())
			{
				AllocateJanitor alloc(kAllocSingleFrame, FILE_LINE_FUNC);

				NavPolyEdgeGatherer* pEdgeGather = NDI_NEW NavPolyEdgeGatherer();
				const NavMesh* pNavMesh = pPoly->GetNavMesh();

				NavMesh::BaseProbeParams probeParams;
				probeParams.m_dynamicProbe = !input.m_obeyedBlockers.AreAllBitsClear();
				probeParams.m_obeyedBlockers = input.m_obeyedBlockers;
				probeParams.m_obeyedStaticBlockers = input.m_obeyedStaticBlockers;

				const Point searchOriginLs = pNavMesh->WorldToLocal(input.m_charNavLoc.GetPosWs());
				pEdgeGather->Init(searchOriginLs, 4.0f, 1.0f, probeParams, 128, 2);
				pEdgeGather->Execute(pPoly);

				m_nearbyNavEdges = pEdgeGather;
			}
			else
			{
				m_nearbyNavEdges = nullptr;
			}
		}

		if (m_nearbyNavEdges.Valid())
		{
			NavObstacles obstacles(m_nearbyNavEdges);
			const float avoidanceRadius = pSettings->m_transClampDist;
			Point closestEdgePos = kInvalidPoint;
			desiredModelDirPs = ApplyObstacleAvoidancePs(desiredModelDirPs,
														 refLocator,
														 m_pos,
														 m_pos + (m_velocity * 0.5f),
														 obstacles,
														 avoidanceRadius,
														 &closestEdgePos,
														 debugDraw);

			if (Length(input.m_velDirPs) > 0.0f)
			{
				const float distToEdge = DistXz(closestEdgePos, m_pos);
				const float wallThreshold = pSettings->m_transClampDist * 0.5f;
				
				if ((Length(desiredModelDirPs) == 0.0f) && (distToEdge <= wallThreshold))
				{
					if (Length(m_velocity) < NDI_FLT_EPSILON)
					{
						// nudge the model back towards align if we're running into walls but also not moving 
						// (can happen when the model is stuck on the edge of the nav mesh)
						desiredModelDirPs = AsUnitVectorXz(input.m_charLocPs.Pos() - m_pos, kZero);
					}
					m_runningIntoWall = true;
				}
				else
				{
					m_runningIntoWall = false;
				}
			}
		}
	}

	m_maxSpeed = GetMaxSpeedForDirection(pSettings, desiredModelDirPs, input.m_facingPs);

	const Vector desiredVelocityInFuture = desiredModelDirPs * m_maxSpeed;
	ANIM_ASSERT(IsReasonable(desiredVelocityInFuture));

	SetDesiredVel(desiredModelDirPs * m_maxSpeed);

	const float curSpeed = Length(m_velocity);
	const bool decelerating = Length(GetDesiredVel()) < NDI_FLT_EPSILON;

	const float springKMult = m_runningIntoWall ? pSettings->m_criticalOffPathSpringMult
												: (IsFollowingPath() ? pSettings->m_offPathSpringMult : 1.0f);

	const float vectorK = decelerating
							  ? (GetVelKDecelForDirection(pSettings, desiredModelDirPs, input.m_facingPs) * springKMult)
							  : GetVelKForDirection(pSettings, desiredModelDirPs, input.m_facingPs, curSpeed);

	const float maxAccel = pSettings->m_maxAccel;
	const float speedK	 = pSettings->m_speedSpringConst;

	ANIM_ASSERT(IsReasonable(vectorK));
	ANIM_ASSERT(IsReasonable(speedK));
	ANIM_ASSERT(IsReasonable(maxAccel));

	const float desSpeed = Length(m_desiredVelocity);

	m_sprungSpeed = m_speedSpring.Track(m_sprungSpeed, curSpeed, deltaTime, (speedK >= 0.0f) ? speedK : vectorK);

	Vector newVel = m_velSpring.Track(m_velocity, m_desiredVelocity, deltaTime, vectorK);

	if ((maxAccel > 0.0f) && Length(m_velSpring.m_speed) > maxAccel)
	{
		Vector accel = SafeNormalize(m_desiredVelocity - m_velocity, kZero) * maxAccel;
		newVel		 = accel * deltaTime + m_velocity;
		m_velSpring.m_speed = accel;
	}

	// JDB: Uncomment if you want your velocity vector to pop w.r.t. changes in ground plane orientation
	// newVel = PlaneProjectVector(newVel);

	m_velocity	   = newVel;
	m_pathVelocity = newVel;
	ANIM_ASSERT(IsReasonable(m_velocity));

	const Vector moveVec = PlaneProjectVector(m_velocity * deltaTime);
	m_pos += moveVec;

	if (input.m_pInterface)
	{
		input.m_pInterface->AdjustStickStep(m_pos, m_velocity, deltaTime);
	}

	ANIM_ASSERT(IsReasonable(m_pos));

	float clampDist = pSettings->m_transClampDist;

	return clampDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionModel::Step_StickHorse(const MotionModelInput& input,
								   const DC::MotionModelSettings* pSettings,
								   float deltaTime,
								   bool debugDraw)
{
	PROFILE(Animation, MM_StepStickHorse);

	PROFILE_AUTO(Animation);

	ANIM_ASSERT(IsReasonable(deltaTime));
	ANIM_ASSERT(pSettings);
	ANIM_ASSERT(!input.m_facingPs.Valid() || IsReasonable(input.m_facingPs.Get()));

#ifdef HTOWNSEND
	CONST_EXPR bool kDebug = false;
	CONST_EXPR bool kBasicDebug = false;
#else
	CONST_EXPR bool kDebug = false;
	CONST_EXPR bool kBasicDebug = false;
#endif

	const bool thrashRight = input.m_horseOptions.m_horseTurningState & kHorseTurningStateRight;
	const bool thrashLeft = input.m_horseOptions.m_horseTurningState & kHorseTurningStateLeft;

	if (kBasicDebug)
	{
		static I64 s_lastPrintedFrame = -1000;
		const I64 gameFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
		if (gameFrame > s_lastPrintedFrame)
		{
			s_lastPrintedFrame = gameFrame;
			MsgConPauseable("%sUserFacing%s\n", GetTextColorString(kTextColorGreen), GetTextColorString(kTextColorNormal));
			MsgConPauseable("%sMomentumDir%s\n", GetTextColorString(kTextColorBlue), GetTextColorString(kTextColorNormal));
			MsgConPauseable("%sGoalFacing%s\n", GetTextColorString(kTextColorRed), GetTextColorString(kTextColorNormal));
			MsgConPauseable("%sNewVelDir%s\n", GetTextColorString(kTextColorMagenta), GetTextColorString(kTextColorNormal));
			MsgConPauseable("%sDriftFacing%s\n", GetTextColorString(kTextColorCyan), GetTextColorString(kTextColorNormal));
			if (input.m_horseOptions.m_fadingOutOfCircleStrafe)
				MsgConPauseable("FADING OUT OF CIRCLE STRAFE\n");

			if (thrashLeft)
				MsgConPauseable("Thrash LEFT\n");

			if (thrashRight)
				MsgConPauseable("Thrash RIGHT\n");

			//if (false)
			//{
			//	const int kDebugBufferSize = 1024;
			//	char debugCurveBuffer[kDebugBufferSize];
			//
			//	NdUtil::PrintPointCurve(pDriftCurve, debugCurveBuffer, kDebugBufferSize);
			//
			//	MsgConPauseable("drift Curve:\n%s", debugCurveBuffer);
			//}
		}
	}

	U32 debugCounterForPrinting = 0;

	if (kDebug || kBasicDebug)
	{
		static I64 lastPrintedGameFrame = -1000;
		static U32 debugCounter = 0;
		const I64 gameFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
		debugCounter = gameFrame == lastPrintedGameFrame ? debugCounter + 1 : 0;
		debugCounterForPrinting = debugCounter;
		lastPrintedGameFrame = gameFrame;
		MsgConPauseable("---- Step %2u ----\n", debugCounter);
	}

	//if (kDebug || kBasicDebug)
	//{
	//	MsgConPauseable("Circle Strafe Drift blend: %.4f\n", input.m_horseOptions.m_circleStrafeDriftBlend);
	//}

	Point debugPt = m_pos + Vector(0.0f, 0.3f, 0.0f);

	m_applyStrafeRotation = false;
	m_pos += m_animError;
	m_animError = kZero;

	// don't do circle mode behavior until we actually are in the turn state, not enter state
	//const bool inCircleMode = input.m_horseOptions.m_horseTurningState & (kHorseTurningStateEnter | kHorseTurningStateTurn);
	const bool inCircleMode = input.m_horseOptions.m_horseTurningState & kHorseTurningStateTurn;
	const bool inUTurnMode = input.m_horseOptions.m_horseTurningState & kHorseTurningStateUTurn;
	const bool inEnterCircleMode = input.m_horseOptions.m_horseTurningState & kHorseTurningStateEnter;
	const bool inCircleOrUTurnMode = inCircleMode || inUTurnMode/* || inEnterCircleMode*/;

	const Vector curFacing = m_facing;
	const Vector curVelocity = m_facingVelocity; //m_velocity;

	//MsgConPauseable("m_facingVelocity Length: %.2f\n", (float)Length(m_facingVelocity));

	const Vector curVelDirPs = SafeNormalize(curVelocity, curFacing);
	Vector momentumDir = pSettings->m_horseMomentumSpeed > 0.0f
		? SafeNormalize(LerpScale(0.0f, pSettings->m_horseMomentumSpeed, curFacing, curVelDirPs, m_sprungSpeed), curFacing)
		: curVelDirPs;

	if (input.m_horseOptions.m_forceFacingDir)
		momentumDir = curFacing;

	ANIM_ASSERT(IsNormal(momentumDir));

	Vector circleFacing = -momentumDir;

	Vector desiredModelDirPs = Slerp(input.m_velDirPs, circleFacing, input.m_horseOptions.m_circleStrafeDriftBlend);
	if (LengthSqr(input.m_velDirPs) < 0.001f && input.m_horseOptions.m_circleStrafeDriftBlend < 0.001f)
		desiredModelDirPs = curFacing;

	if (input.m_horseOptions.m_fadingOutOfCircleStrafe)
		desiredModelDirPs = AsUnitVectorXz(input.m_velDirPs, curFacing);
	//Vector desiredModelDirPs = inCircleMode
	//	? Slerp(input.m_velDirPs, circleFacing, input.m_horseOptions.m_circleStrafeDriftBlend)
	//	: SafeNormalize(input.m_velDirPs, curFacing);

	if (kDebug)
	{
		g_prim.Draw(DebugArrow(debugPt, desiredModelDirPs, kColorPink, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
		debugPt += Vector(0.0f, 0.1f, 0.0f);
	}

	const float maxAccel = pSettings->m_maxAccel;
	const float speedK = pSettings->m_speedSpringConst;

	ANIM_ASSERT(IsReasonable(speedK));
	ANIM_ASSERT(IsReasonable(maxAccel));

	const Vector velDir = Slerp(desiredModelDirPs, input.m_horseOptions.m_strafingVelPs, input.m_horseOptions.m_strafingBlend);

	m_maxSpeed = GetMaxSpeedForDirection(pSettings, velDir, momentumDir);
	if (input.m_horseOptions.m_maxSpeed > 0.0f)
		m_maxSpeed = Min(m_maxSpeed, input.m_horseOptions.m_maxSpeed);


	Vector desiredVel = LengthSqr(input.m_velDirPs) >= 0.0001f? (velDir * m_maxSpeed) : kZero;

	if (input.m_horseOptions.m_needToBackUp && LengthSqr(desiredVel) < Scalar(0.001f))
		desiredVel = velDir * 0.1f;

	SetDesiredVel(desiredVel);

	const float curSpeed = Length(GetVel());

	const float desSpeed = Length(m_desiredVelocity);
	if (kDebug)
		MsgConPauseable("Desired Speed: %.3f\n", desSpeed);

	const bool tooSlowForDesiredDir = desSpeed < 0.2f && !input.m_horseOptions.m_fadingOutOfCircleStrafe;
	const Vector goalFacing = tooSlowForDesiredDir
		? momentumDir
		: desiredModelDirPs;

	const Vector effectiveDirectionForSpeed = input.m_horseOptions.m_fadingOutOfCircleStrafe
		? goalFacing
		: Slerp(goalFacing, -momentumDir, input.m_horseOptions.m_circleStrafeDriftBlend);
	//const Vector effectiveDirectionForSpeed = inCircleMode ? Slerp(goalFacing, -momentumDir, input.m_horseOptions.m_circleStrafeDriftBlend) : goalFacing;

	F32 targetSpeed = Min(desSpeed, GetMaxSpeedForDirection(pSettings, curVelocity, effectiveDirectionForSpeed));
	const bool decelerating = curSpeed > targetSpeed;

	// Entering circle mode can cause problems if dipping below the idle limit before transitioning into circle moveset
	const float speedPctSpeed = inCircleMode ? ((curSpeed + 0.3f) > targetSpeed ? pSettings->m_maxSpeed : 0.0f) : (decelerating ? pSettings->m_maxSpeed : curSpeed);

	const float vectorK = decelerating ? GetVelKDecelForDirection(pSettings, desSpeed > 0.1f ? desiredModelDirPs : momentumDir, momentumDir)
		: GetVelKForDirection(pSettings, desiredModelDirPs, momentumDir, speedPctSpeed);

	ANIM_ASSERT(IsReasonable(vectorK));

	float velSpringK = ((speedK >= 0.0f) ? speedK : vectorK);

	if (input.m_horseOptions.m_requestImmediateStop)
	{
		if (input.m_horseOptions.m_needToBackUp)
		{
			targetSpeed = 0.1f;
		}
		else
		{
			targetSpeed = 0.f;
		}
		velSpringK = 10.f;
	}

	m_sprungSpeed = m_speedSpring.Track(m_sprungSpeed, targetSpeed, deltaTime, velSpringK);

	Vector newVelDir = momentumDir;

	const Angle curUnitAngle = Angle::FromRadians(Atan2(momentumDir.X(), momentumDir.Z()));
	const Angle goalUnitAngle = Angle::FromRadians(Atan2(goalFacing.X(), goalFacing.Z()));
	const float angleDiff = goalUnitAngle.AngleDiff(curUnitAngle);
	float diffRad = DEGREES_TO_RADIANS(angleDiff);

	//if (desSpeed > NDI_FLT_EPSILON || !input.m_horseOptions.m_fadingOutOfCircleStrafe)
	if (true)
	{
		// Facing update
		// Dampened Spring

		// Force turn direction
		if ((inCircleOrUTurnMode || Dot(goalFacing, momentumDir) < -0.5f) && ((diffRad < 0.0f) ? thrashLeft : thrashRight))
		{
			const float preAdjustDiffRad = diffRad;
			diffRad += PI_TIMES_2 * -Sign(diffRad);
			if (kDebug)
			{
				MsgConPauseable("%sAdjusting circleModeDiffRad from %.3f to %.3f%s\n", GetTextColorString(kTextColorYellow), GetTextColorString(kTextColorNormal));
			}
		}
		float circleModeDiffRad = diffRad;

		float maxTurnRateDeg = GetTurnRateDpsForDirection(pSettings, momentumDir, goalFacing, speedPctSpeed);
		if (input.m_horseOptions.m_maxTurnRateDps >= 0.0f)
		{
			maxTurnRateDeg = Min(maxTurnRateDeg, input.m_horseOptions.m_maxTurnRateDps);
		}

		const F32 maxTurnRate = DEGREES_TO_RADIANS(maxTurnRateDeg);
		const float maxTurnRad = maxTurnRate * deltaTime;

		const DC::PointCurve* pNormalModeFacingSpringCurve = Sign(m_facingSpring.m_speed) == Sign(diffRad) ? &pSettings->m_horseFacingKDecelDirectional : &pSettings->m_horseFacingKAccelDirectional;
		const float normalModeFacingSpringK = EvaluateDirectionalCurve(pNormalModeFacingSpringCurve, momentumDir, goalFacing);

		const DC::PointCurve* pCircleModeFacingSpringCurve = Sign(m_facingSpring.m_speed) == Sign(circleModeDiffRad) ? &pSettings->m_horseFacingKDecelDirectional : &pSettings->m_horseFacingKAccelDirectional;
		const float circleModeFacingSpringK = EvaluateDirectionalCurve(pNormalModeFacingSpringCurve, momentumDir, goalFacing);

		const float circleModeDeltaRad = maxTurnRad * -Sign(circleModeDiffRad);
		const float circleModeFacingSpringSpeed = circleModeFacingSpringK * -Sign(circleModeDiffRad);

		const float normalModeNewRad = m_facingSpring.TrackAngleRad(diffRad, 0.0f, deltaTime, normalModeFacingSpringK, pSettings->m_horseFacingDampenRatio);
		float normalModeDeltaRad = normalModeNewRad - diffRad;
		const float normalModeDeltaRadAbs = Abs(normalModeDeltaRad);
		if (normalModeDeltaRadAbs > maxTurnRad)
		{
			const float excess = normalModeDeltaRadAbs - maxTurnRad;
			normalModeDeltaRad -= excess * Sign(normalModeDeltaRad);
		}

		const float deltaRad = input.m_horseOptions.m_fadingOutOfCircleStrafe
			? normalModeDeltaRad
			: Lerp(normalModeDeltaRad, circleModeDeltaRad, input.m_horseOptions.m_circleStrafeDriftBlend);

		if (kDebug)
			MsgConPauseable("DeltaRad: %.3f\n", deltaRad);

		newVelDir = RotateVectorAbout(momentumDir, kUnitYAxis, -deltaRad);
		newVelDir = SafeNormalize(newVelDir, goalFacing);
	}
	else
	{
		if (Abs(m_yawSpeed) > 0.01f && curSpeed > 0.1f)
		{
			const F32 speedPct = speedPctSpeed / pSettings->m_maxSpeed;
			const F32 momentumFactor = Limit01(pSettings->m_horseStopFacingMomentum);

			const F32 turnSpeedFactor = LerpScale(0.0f, 1.0f, momentumFactor, 1.0f, speedPct);

			newVelDir = RotateVectorAbout(momentumDir, kUnitYAxis, m_yawSpeed * deltaTime * turnSpeedFactor);
			newVelDir = SafeNormalize(newVelDir, momentumDir);
		}
	}

	const DC::PointCurve* pDriftCurve = &(pSettings->m_horseDriftFactorDirectional);
	if (pSettings->m_horseDriftFactorDirectionalIdle.m_count > 0 && pSettings->m_horseDriftFactorDirectionalIdle.m_values[0] > -180.0f)
	{
		const float speedPct = speedPctSpeed / pSettings->m_maxSpeed;
		if (speedPct < pSettings->m_movingGroupSpeedPercentThreshold)
		{
			pDriftCurve = &(pSettings->m_horseDriftFactorDirectionalIdle);
		}
	}

	const Vector driftFacing = tooSlowForDesiredDir ? goalFacing : Slerp(input.m_velDirPs, circleFacing, input.m_horseOptions.m_circleStrafeDriftBlend);

	Maybe<bool> hintDir = inCircleOrUTurnMode ? thrashLeft : Maybe<bool>();
	const F32 driftFactor = Clamp(EvaluateDirectionalCurveWithHint(pDriftCurve, momentumDir, driftFacing, hintDir, input.m_horseOptions.m_circleStrafeDriftBlend), -180.0f, 180.0f);

	//MsgConPauseable("driftFactor: %.3f\n", driftFactor);

	// Nudge current vel to handle minor fluctuation in idle.
	const F32 curVelocityDot = Dot(SafeNormalize(curVelocity + 0.3 * goalFacing, kZero), goalFacing);
	const F32 facingVelocityDot = Dot(curFacing, goalFacing);

	// Magic Horse Facing: Make sure this matches RiddenSkill
	const Vector realVelocity = (
		g_motionMatchingOptions.m_disableHorseReverseMode
			? Slerp(newVelDir, input.m_horseOptions.m_strafingVelPs, input.m_horseOptions.m_strafingBlend)
			: Slerp(momentumDir, -curFacing, Limit01(curVelocityDot - facingVelocityDot))
		) * m_sprungSpeed;

	Vector facingVelocity = newVelDir * m_sprungSpeed * (1.0f - input.m_horseOptions.m_strafingBlend);
	Vector newVel = realVelocity;

	if (maxAccel > 0.0f && m_speedSpring.m_speed > maxAccel)
	{
		const Vector accel		 = facingVelocity - curVelocity;
		const F32 accelMagnitude = Length(accel);

		if (accelMagnitude > maxAccel)
		{
			const Vector accelDir = SafeNormalize(accel, kZero);

			facingVelocity = accelDir * maxAccel * deltaTime + curVelocity;

			m_speedSpring.m_speed = maxAccel;
		}
	}

	const Vector newVelNormalized = SafeNormalize(newVel, curFacing);

	const Angle newVelAngle = Angle::FromRadians(Atan2(newVelNormalized.X(), newVelNormalized.Z()));
	const Angle curFacingAngle = Angle::FromRadians(Atan2(curFacing.X(), curFacing.Z()));

	//if (kDebug)
	//{
	//	g_prim.Draw(DebugArrow(debugPt, newVelNormalized, kColorMagenta, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
	//	debugPt += Vector(0.0f, 0.1f, 0.0f);
	//}

	float curDrift = curFacingAngle.AngleDiff(newVelAngle);
	float desiredDrift = driftFactor;

	if (!inCircleOrUTurnMode && !input.m_horseOptions.m_fadingOutOfCircleStrafe)
	{
		desiredDrift = Lerp(driftFactor, curDrift, input.m_horseOptions.m_circleStrafeDriftBlend);
	}
	else if (kDebug || kBasicDebug)
	{
		MsgConPauseable("using circle strafe drift mode\n");
	}

	const float curDiff = curFacingAngle.AngleDiff(newVelAngle);

	m_forceTurnRate = 10000.0f; // We control the horizontal.

	const F32 prevDrift = m_driftTheta;

	if (kDebug || kBasicDebug)
		MsgConPauseable("desiredDrift: %.3f\n", desiredDrift);
	m_driftTheta = m_driftSpring.Track(m_driftTheta, desiredDrift, deltaTime, pSettings->m_horseDriftK);

	if (input.m_horseOptions.m_fadingOutOfCircleStrafe)
	{
		m_driftTheta = desiredDrift;
		m_driftSpring.m_speed = (m_driftTheta - prevDrift) / deltaTime;
	}

	//if (input.m_horseOptions.m_fadingOutOfCircleStrafe && ((desiredDrift > 0.0f && m_driftTheta > desiredDrift) || (desiredDrift < 0.0f && m_driftTheta < desiredDrift)))
	//{
	//	m_driftTheta = desiredDrift;
	//}

	if (Abs(m_driftTheta) > Abs(curDiff) && ((m_driftTheta > curDiff) ^ (diffRad > 0.0f)))
	{
		if (kDebug)
			MsgConPauseable("%sadjusting Drift Theta from %.3f to %.3f because of trajectory%s\n", GetTextColorString(kTextColorCyan), m_driftTheta, curDiff, GetTextColorString(kTextColorNormal));
		// Clamp to avoid turning away from trajectory
		m_driftTheta = curDiff;

		// I don't trust this line. This doesn't seem like the correct way to calculate speed

		const float newDriftSpringSpeed = (m_driftTheta - prevDrift) / deltaTime;

		if (kDebug)
			MsgConPauseable("drift spring speed changing from %.3f to %.3f\n", m_driftSpring.m_speed, newDriftSpringSpeed );

		m_driftSpring.m_speed = newDriftSpringSpeed;
	}

	if (kDebug)
		MsgConPauseable("Drift Theta: %.3f\n", m_driftTheta);

	if (input.m_horseOptions.m_forceFacingDir)
	{
		if (kBasicDebug || kDebug)
			MsgConPauseable("Forcing facing dir\n");
		m_userFacing = input.m_facingPs.Get();
	}
	else
	{
		m_userFacing = RotateVectorAbout(newVelDir, kUnitYAxis, DEGREES_TO_RADIANS(m_driftTheta));
		m_userFacing = AsUnitVectorXz(m_userFacing.Get(), curFacing);
	}

	CONST_EXPR float kTextSize = 0.4f;
	if (kBasicDebug)
	{
		StringBuilder<8> numStr("%d", debugCounterForPrinting);
		g_prim.Draw(DebugArrow(debugPt, driftFacing, kColorCyanTrans, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha), numStr.c_str(), kTextSize), kPrimDuration1FramePauseable);
		debugPt += Vector(0.0f, 0.02f, 0.0f);

		g_prim.Draw(DebugArrow(debugPt, momentumDir, kColorBlueTrans, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha), numStr.c_str(), kTextSize), kPrimDuration1FramePauseable);
		debugPt += Vector(0.0f, 0.02f, 0.0f);

		g_prim.Draw(DebugArrow(debugPt, newVelDir, kColorMagentaTrans, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha), numStr.c_str(), kTextSize), kPrimDuration1FramePauseable);
		debugPt += Vector(0.0f, 0.02f, 0.0f);

		g_prim.Draw(DebugArrow(debugPt, m_userFacing.Get(), kColorGreenTrans, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha), numStr.c_str(), kTextSize), kPrimDuration1FramePauseable);
		debugPt += Vector(0.0f, 0.02f, 0.0f);

		g_prim.Draw(DebugArrow(debugPt, goalFacing, kColorRedTrans, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha), numStr.c_str(), kTextSize), kPrimDuration1FramePauseable);
		debugPt += Vector(0.0f, 0.02f, 0.0f);
	}

	if (kDebug)
	{
		StringBuilder<8> numStr("%d", debugCounterForPrinting);
		g_prim.Draw(DebugArrow(debugPt, driftFacing, kColorRedTrans, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha), numStr.c_str(), kTextSize), kPrimDuration1FramePauseable);
		debugPt += Vector(0.0f, 0.02f, 0.0f);

		g_prim.Draw(DebugArrow(debugPt, momentumDir, kColorBlueTrans, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha), numStr.c_str(), kTextSize), kPrimDuration1FramePauseable);
		debugPt += Vector(0.0f, 0.02f, 0.0f);

		g_prim.Draw(DebugArrow(debugPt, m_userFacing.Get(), kColorGreenTrans, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha), numStr.c_str(), kTextSize), kPrimDuration1FramePauseable);
		debugPt += Vector(0.0f, 0.02f, 0.0f);

		g_prim.Draw(DebugArrow(debugPt, newVelDir, kColorOrangeTrans, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha), numStr.c_str(), kTextSize), kPrimDuration1FramePauseable);
		debugPt += Vector(0.0f, 0.02f, 0.0f);

		g_prim.Draw(DebugArrow(debugPt, curFacing, kColorWhiteTrans, 0.25f, PrimAttrib(kPrimEnableHiddenLineAlpha), numStr.c_str(), kTextSize), kPrimDuration1FramePauseable);
		debugPt += Vector(0.0f, 0.02f, 0.0f);
	}


	m_velocity	   = newVel;
	m_pathVelocity = newVel;
	m_facingVelocity = facingVelocity;
	ANIM_ASSERT(IsReasonable(m_velocity));

	const Vector moveVec = PlaneProjectVector(m_velocity * deltaTime);
	m_pos += moveVec;

	ANIM_ASSERT(IsReasonable(m_pos));

	const float clampDist = pSettings->m_transClampDist;

	return clampDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point AdvanceClosestPointOnPath(Point_arg pos, I32F closestLeg, const IPathWaypoints* pPath, float distance)
{
	const I32F count = pPath->GetWaypointCount();

	for (I32F i = closestLeg + 1; i < count; ++i)
	{
		Point P0 = pPath->GetWaypoint(i - 1);
		Point P1 = pPath->GetWaypoint(i);

		// find the point that intersects the segment at the proper distance
		// Intersect line P = P0 + t*(P1 - P0) and distance func ||pos - P|| = distance
		// simplify to
		//
		// ||pos - P0 - t(P1 - P0)|| = d
		//
		// substitute
		//
		// A = pos - P0
		// B = P1 - P0
		//
		// expand to
		//
		// A*A - 2*A*B*t + B*B*t^2 = distance^2
		//
		// solve the quadradic with
		//
		// a = B*B
		// b = -2*A*B
		// c = A*A - distance^2
		//

		Vector A = pos - P0;
		A.SetY(0);
		Vector B = P1 - P0;
		B.SetY(0);

		float a = Dot(B, B);
		float b = -2.0f * Dot(A, B);
		float c = Dot(A, A) - distance * distance;

		float determinate = b * b - 4.0f * a*c;

		if (determinate < 0.0f)
		{
			// the circle doesn't intersect this segment
			continue;
		}

		if (Abs(a) < 0.0001f)
		{
			// we perfectly intersect point P0 at the right distance, return that
			return P0;
		}

		float t0 = (-b + Sqrt(determinate)) / (2.0f * a);
		float t1 = (-b - Sqrt(determinate)) / (2.0f * a);

		Point T0 = P0 + t0 * B;
		Point T1 = P0 + t1 * B;

		Vector dirT0 = T0 - pos;
		Vector dirT1 = T1 - pos;

		Point result = T0;
		float t = t0;
		if (Dot(dirT0, B) < 0.0f || t > 1.0f || t < 0.0f)
		{
			t = t1;
			result = T1;
		}

		// we don't intersect this segment, just the line, go to the next segment
		if (t < 0.0f || t > 1.0f)
			continue;

		float dist = Length(result - pos);

		// return the point of intersection
		return result;
	}

	// we didn't intersect any of the segments at the right distance, so return the next point on the path
	return pPath->GetWaypoint(closestLeg);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point ClosestPointOnPath(Point_arg pos,
						 const IPathWaypoints* pPath,
						 I32F* pClosestLegOut,
						 bool stopAtGoal,
						 I32F iStartingLeg /* = -1 */,
						 F32 bias /* = 0.0f */)
{
	Point closestPos = kOrigin;
	float bestD = kLargeFloat;
	I32F closestLeg = 0;

	const I32F count = pPath->GetWaypointCount();

	if (iStartingLeg < 0)
	{
		iStartingLeg = 0;
	}

	if (count <= 0)
	{
		if (pClosestLegOut)
			*pClosestLegOut = -1;

		return pos;
	}
	else if (count == 1)
	{
		if (pClosestLegOut)
			*pClosestLegOut = 0;

		return pos;
	}
	else if (iStartingLeg >= count)
	{
		if (pClosestLegOut)
			*pClosestLegOut = count - 1;

		return pPath->GetEndWaypoint();
	}

	Point prevPos = pPath->GetWaypoint(iStartingLeg);

	for (I32F i = iStartingLeg + 1; i < count; ++i)
	{
		Point nextPos = pPath->GetWaypoint(i);

		Point p = kOrigin;
		float d = kLargeFloat;
		Scalar tt = kZero;

		const bool clampBegin = (i > 1);
		const bool clampEnd = stopAtGoal || (i < (count - 1));

		if (clampBegin && clampEnd)
		{
			// [0, 1]
			d = DistPointSegmentXz(pos, prevPos, nextPos, &p, &tt);
		}
		else if (clampBegin)
		{
			// [0, +inf)
			d = DistPointRayXz(pos, prevPos, nextPos, &p, &tt);
		}
		else if (clampEnd)
		{
			// (-inf, 1]
			d = DistPointRayXz(pos, nextPos, prevPos, &p, &tt);
		}
		else
		{
			// (-inf, +inf)
			d = DistPointLineXz(pos, prevPos, nextPos, &p, &tt);
		}

		// consider height deltas in 2m increments
		const Scalar py	   = Lerp(prevPos.Y(), nextPos.Y(), tt);
		const Scalar yDiff = Abs(pos.Y() - py);
		const Scalar dy	   = SCALAR_LC(2.0f) * Floor(yDiff * SCALAR_LC(0.5f));

		d += dy;

		float biasToUse = bias;
		if ((closestLeg + 1) == (i - 1))
		{
			biasToUse = 0.0f;
		}

		if ((d + biasToUse) < bestD)
		{
			bestD = d;

			if (tt > 1.0f || tt < 0.0f)
				bestD += bias;

			closestPos = PointFromXzAndY(p, pos);
			closestLeg = i - 1;
		}

		prevPos = nextPos;
	}

	if (pClosestLegOut)
		*pClosestLegOut = closestLeg;

	return closestPos;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vector SnapPathVelocity(Vector_arg vel, Vector_arg pathDir)
{
	const Scalar speed = Length(vel);
	Vector snapped = vel;

	if (DotXz(vel, pathDir) >= 0.0f)
	{
		snapped = pathDir * speed;
	}
	else
	{
		snapped = pathDir * SCALAR_LC(-1.0f) * speed;
	}

	return snapped;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionModel::Step_Path(const Locator& refLocator,
							 const MotionModelInput& input,
							 const DC::MotionModelSettings* pSettings,
							 float deltaTime,
							 bool debugDraw)
{
	PROFILE(Animation, MM_StepPath);

	ANIM_ASSERT(IsReasonable(deltaTime));
	ANIM_ASSERT(pSettings);
	ANIM_ASSERT(input.m_pPathInput->m_pathPs.IsValid());

	const IPathWaypoints* pPathPs = &input.m_pPathInput->m_pathPs;
	const I32F numWaypoints		  = pPathPs->GetWaypointCount();
	const bool stopAtGoal		  = input.m_pPathInput->m_stopAtGoal;
	const float speedAtGoal		  = input.m_pPathInput->m_speedAtGoal;

	const float animErrDist = LengthXz(m_animError);
	if (animErrDist > 0.01f)
	{
		//const float errDist = Sqrt(animErrDistSqr);
		//const Vector projAnimError = pathDirPs * Max(DotXz(m_animError, pathDirPs), 0.0f);
		//m_pos += projAnimError;

		const Point newPos = ClosestPointOnPath(m_pos + m_animError,
												pPathPs,
												nullptr,
												stopAtGoal,
												-1,
												pSettings->m_transClampDist);

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			const Vector errDir = SafeNormalize(m_animError, kZero);

			PrimServerWrapper ps(refLocator);
			ps.SetDuration(kPrimDuration1FramePauseable);
			ps.EnableHiddenLineAlpha();

			ps.DrawArrow(m_pos, errDir, 0.25f, kColorRedTrans);
			ps.DrawArrow(m_pos, newPos, 0.25f, kColorRed);
			ps.DrawString(m_pos + errDir, StringBuilder<64>("animErr: %0.3fm", animErrDist).c_str(), kColorRedTrans, 0.5f);
			ps.DrawCross(newPos, 0.1f, kColorRed);
		}

		m_pos = newPos;
	}
	else
	{
		m_pos += Vector(0.0f, m_animError.Y(), 0.0f);
	}

	m_animError = kZero;

	const Point prevPos = GetPos();

	I32F iPathLeg = 0;
	const Point closestPathPosPs = ClosestPointOnPath(prevPos,
													  pPathPs,
													  &iPathLeg,
													  stopAtGoal,
													  -1,
													  pSettings->m_transClampDist);

	Point prevWaypointPs = pPathPs->GetWaypoint(iPathLeg);
	Point nextWaypointPs = pPathPs->GetWaypoint(iPathLeg + 1);

	while ((iPathLeg < (numWaypoints - 2)) && (DistXz(prevWaypointPs, nextWaypointPs) < kPathAdvanceNudgeDist))
	{
		++iPathLeg;
		prevWaypointPs = nextWaypointPs;
		nextWaypointPs = pPathPs->GetWaypoint(iPathLeg + 1);
	}

	while ((iPathLeg < (numWaypoints - 2)) && (DistXz(prevPos, nextWaypointPs) < kPathAdvanceNudgeDist))
	{
		++iPathLeg;
		prevWaypointPs = nextWaypointPs;
		nextWaypointPs = pPathPs->GetWaypoint(iPathLeg + 1);
	}

	bool onLastLeg = (iPathLeg == (numWaypoints - 2)) && input.m_pPathInput->m_pathEndsAtGoal;
	const bool startedOnLastLeg = onLastLeg;

	const float remLegDistance = (onLastLeg && !stopAtGoal) ? kLargeFloat : (float)DistXz(nextWaypointPs, closestPathPosPs);
	const Vector pathDirPs = AsUnitVectorXz(nextWaypointPs - prevWaypointPs, kZero);

	bool changedPath = false;

	if (m_lastPathId != input.m_pPathInput->m_pathId)
	{
		const float dotP = DotXz(m_velocity, pathDirPs);

		m_pathVelocity	  = dotP * pathDirPs;
		m_numSavedCorners = 0;
		m_turnRateDps	  = -1.0f;

		changedPath = true;

		m_pathOffsetDist = Min(DistXz(prevPos, closestPathPosPs), m_proceduralClampDist);
	}
	else
	{
		m_pathOffsetDist = Max(m_pathOffsetDist - (deltaTime * 0.075f), 0.0f);
	}

	const Vector pathPosDelta = LimitVectorLength(VectorXz(prevPos - closestPathPosPs), 0.0f, m_pathOffsetDist);

	const Vector curAccel = GetAccel();
	const float curSpeed = Length(m_pathVelocity);

	const Vector goalFacingPs = GetLocalZ(input.m_pPathInput->m_goalRotPs);

	const float decelK = input.m_pPathInput->m_decelKMult * GetVelKDecelForDirection(pSettings, pathDirPs, m_desiredFacing);
	const float goalDecelK = input.m_pPathInput->m_decelKMult * GetVelKDecelForDirection(pSettings, pathDirPs, goalFacingPs);

	//g_prim.Draw(DebugString(m_pos + kUnitYAxis, StringBuilder<128>("goalDecelK: %0.3f", goalDecelK).c_str()));
	//g_prim.Draw(DebugLine(m_pos, m_pos + kUnitYAxis, kColorWhiteTrans));

	const float stopDist = Max(ApproximateStoppingDistance(m_pathVelocity, curAccel, goalDecelK), kMinStopDist);
	const float stopFaceDist = input.m_pPathInput->m_stoppingFaceDist;

	if (onLastLeg && (stopFaceDist >= 0.0f) && (remLegDistance < stopFaceDist))
	{
		m_applyStrafeRotation = false;
		m_userFacing = goalFacingPs;
	}
	else if (input.m_pPathInput->m_strafeDirPs.Valid())
	{
		m_userFacing = input.m_pPathInput->m_strafeDirPs;
	}
	else
	{
		m_userFacing = MAYBE::kNothing;
	}

	RecordNewCorners(pPathPs, changedPath, decelK, pSettings);

	m_maxSpeed = GetMaxSpeedForDirection(pSettings, pathDirPs, m_desiredFacing);

	const float maxSpeed = GetCornerLimitedMaxSpeed(prevPos, m_maxSpeed, decelK, pSettings);

	float desSpeed = maxSpeed;

	if ((speedAtGoal >= 0.0f) && (speedAtGoal < maxSpeed))
	{
		const float speedDelta = maxSpeed - speedAtGoal;
		//const float curA = Length(curAccel);
		//const float slowDownDist = ApproximateStoppingDistance(speedDelta, curA, decelK);
		//const float slowDownDist = ApproximateStoppingDistance(pathDirPs * speedDelta, curAccel, decelK);
		const float slowDownDist = ApproximateStoppingDistance(speedDelta, 0.0f, goalDecelK);
		//const float slowDownDist = (speedDelta / maxSpeed) * stopDist;
		//const float slowDownDist = stopDist;

		float remPathDist	= 0.0f;
		const bool pastGoal = onLastLeg && (DotXz(m_pos - nextWaypointPs, prevWaypointPs - nextWaypointPs) < 0.0f);

		if (!pastGoal)
		{
			remPathDist = DistXz(m_pos, nextWaypointPs);
			Point prevPs = nextWaypointPs;
			for (U32F iLeg = iPathLeg + 1; iLeg < numWaypoints; ++iLeg)
			{
				const Point waypointPs = pPathPs->GetWaypoint(iLeg);
				remPathDist += DistXz(prevPs, waypointPs);
				prevPs = waypointPs;
			}
		}

		if (slowDownDist >= remPathDist)
		{
			desSpeed = speedAtGoal;
			//desSpeed = 0.0f;
		}
	}

	if (onLastLeg && (stopDist >= remLegDistance) && stopAtGoal)
	{
		SetDesiredVel(kZero);
		desSpeed = 0.0f;
	}
	else if (Length(input.m_velDirPs) < NDI_FLT_EPSILON)
	{
		SetDesiredVel(kZero);
		desSpeed = 0.0f;
	}
	else
	{
		const Vector pathVelPs = PlaneProjectVector(pathDirPs * desSpeed);
		SetDesiredVel(pathVelPs);
	}

	const bool decelerating = desSpeed < curSpeed;

	const float vectorK = decelerating ? decelK
									   : GetVelKForDirection(pSettings, pathDirPs, m_desiredFacing, curSpeed);

	const float maxAccel = pSettings->m_maxAccel;

	//Vector newVel = m_velSpring.Track(m_velocity, m_desiredVelocity, deltaTime, vectorK);
	// newVel = Length(newVel) * pathDirPs;
	Vector newVel = m_velSpring.Track(m_pathVelocity, m_desiredVelocity, deltaTime, vectorK);

	if ((pSettings->m_maxAccel > 0.0f) && Length(m_velSpring.m_speed) > pSettings->m_maxAccel)
	{
		Vector accel = SafeNormalize(m_desiredVelocity - m_pathVelocity, kZero) * pSettings->m_maxAccel;
		newVel		 = accel * deltaTime + m_pathVelocity;
		m_velSpring.m_speed = accel;
	}

	// m_velocity	   = newVel;
	m_pathVelocity = SnapPathVelocity(newVel, pathDirPs);

	if (onLastLeg && stopAtGoal && m_charInIdle
		&& ((remLegDistance + pSettings->m_transClampDist) < input.m_pPathInput->m_goalRadius))
	{
		SetDesiredVel(kZero);
		m_velocity	   = kZero;
		m_pathVelocity = kZero;
		m_velSpring.Reset();
	}

	const float dotP = DotXz(m_pathVelocity, pathDirPs);
	const bool goingWrongWay = dotP < 0.0f;

	Point desiredNewPosPs = closestPathPosPs;

	const float newSpeed = Length(newVel);
	float remTravelDist = pSettings->m_applyPathModeFix ? (newSpeed * deltaTime) : (curSpeed * deltaTime);

	while (remTravelDist > NDI_FLT_EPSILON)
	{
		const float distToWaypoint = (onLastLeg && !stopAtGoal) ? kLargeFloat : (float)DistXz(desiredNewPosPs, nextWaypointPs);

		const Vector curPathDirPs = AsUnitVectorXz(nextWaypointPs - prevWaypointPs, kZero);
		const Vector travelDirPs = goingWrongWay ? -curPathDirPs : curPathDirPs;

		const float desLegTravelDist = remTravelDist;
		const float legTravelDist = Limit(desLegTravelDist, 0.0f, distToWaypoint);

		Point nextPosPs;

		if ((legTravelDist > distToWaypoint) && !goingWrongWay)
		{
			nextPosPs = nextWaypointPs;
		}
		else
		{
			const Point desNextPosPs = desiredNewPosPs + (travelDirPs * legTravelDist);
			const Point closestPosPs = ClosestPointOnPath(desNextPosPs,
														  pPathPs,
														  nullptr,
														  stopAtGoal,
														  iPathLeg,
														  pSettings->m_transClampDist);
			nextPosPs = closestPosPs;
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			PrimServerWrapper ps(refLocator);
			ps.SetDuration(kPrimDuration1FramePauseable);
			ps.EnableHiddenLineAlpha();

			ps.DrawCross(desiredNewPosPs, 0.02f, kColorRed);
			ps.DrawCross(nextPosPs, 0.025f, kColorGreen);
			ps.DrawArrow(desiredNewPosPs, nextPosPs, 0.1f, kColorOrange);
		}

		desiredNewPosPs = nextPosPs;

		const float travelRatio = Limit01(legTravelDist / desLegTravelDist);
		remTravelDist -= travelRatio * remTravelDist;

		if (iPathLeg < (numWaypoints - 2))
		{
			++iPathLeg;

			prevWaypointPs = nextWaypointPs;
			nextWaypointPs = pPathPs->GetWaypoint(iPathLeg + 1);
			onLastLeg = iPathLeg >= (numWaypoints - 2) && input.m_pPathInput->m_pathEndsAtGoal;
		}
		else
		{
			break;
		}
	}

	// don't need to find closest because we do it in the loop (or set it to next waypoint exactly)
	const Point newPos = desiredNewPosPs + pathPosDelta;

	SetPos(newPos);

	m_pos = PlaneProjectPos(m_pos);
	m_pathVelocity = PlaneProjectVector(m_pathVelocity);

	if (changedPath)
	{
		m_velocity = m_pathVelocity;
	}
	else if (deltaTime > NDI_FLT_EPSILON)
	{
		const Vector updatedVel = (PlaneProjectPos(desiredNewPosPs) - PlaneProjectPos(closestPathPosPs)) / deltaTime;
		m_velocity = m_velocity + ((updatedVel - m_velocity) * Limit01(deltaTime * kVelocityLPFAlpha));
		m_velocity = PlaneProjectVector(m_velocity);
		m_velocity = LimitVectorLength(m_velocity, 0.0f, maxSpeed);
	}

	const float speedK = pSettings->m_speedSpringConst;

	m_sprungSpeed = m_speedSpring.Track(m_sprungSpeed,
										Length(m_velocity),
										deltaTime,
										(speedK >= 0.0f) ? speedK : vectorK);


	m_lastPathLength = GetRemainingPathDistXz(pPathPs, iPathLeg, m_pos);

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		PrimServerWrapper ps(refLocator);
		ps.SetDuration(kPrimDuration1FramePauseable);
		ps.EnableHiddenLineAlpha();

		const Vector vo = Vector(0.0f, 0.1f, 0.0f);
		//g_prim.Draw(DebugArrow(prevPos + vo, SafeNormalize(m_pathVelocity, kZero), kColorYellow, 0.1f, kPrimEnableHiddenLineAlpha));
		//ps.DrawArrow(prevPos + vo, pathDirPs, 0.1f, goingWrongWay ? kColorRed : kColorGreen);

		ps.DrawSphere(prevPos, 0.02f, kColorRedTrans);
		ps.DrawSphere(newPos, 0.025f, kColorGreenTrans);

		const Point textPos = prevPos + Vector(0.0f, 0.3f, 0.0f);

		//ps.EnableWireFrame();
		//ps.DrawSphere(closestPathPosPs, 0.1f, kColorPink);

		//g_prim.Draw(DebugArrow(newPos + kUnitYAxis, m_velocity, changedPath ? kColorOrange : kColorGreen, 0.25f, kPrimEnableHiddenLineAlpha), kPrimDuration1Frame);

		StringBuilder<256> desc;
		desc.append_format("k: %0.3f%s", vectorK, decelerating ? " <decel>" : "");

		desc.append_format(" [goalK: %0.3f]", goalDecelK);

		if (maxSpeed < m_maxSpeed)
		{
			desc.append_format(" %0.1fm/s", maxSpeed);
		}

		if (!desc.empty())
		{
			ps.DrawLine(prevPos, textPos, kColorWhiteTrans);
			ps.DrawString(textPos, desc.c_str(), kColorWhiteTrans, 0.5f);
		}

		if (m_userFacing.Valid())
		{
			ps.DrawArrow(newPos, m_userFacing.Get() * 0.25f, 0.2f, kColorBlueTrans);
		}
	}

	float clampDist = pSettings->m_transClampDist;

	clampDist -= (animErrDist * 1.25f);
	clampDist = Max(clampDist, 0.0f);

	return clampDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static CatmullRom* CreateSplineFromPath(const IPathWaypoints* pPath)
{
	if (!pPath || !pPath->IsValid())
		return nullptr;

	ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);

	const size_t numPoints = pPath->GetWaypointCount();
	ListArray<Locator> points(numPoints);

	for (I32F iPt = 0; iPt < numPoints; iPt++)
	{
		const Point posPs = pPath->GetWaypoint(iPt);
		points.PushBack(Locator(posPs));
	}

	AllocateJanitor frameAlloc(kAllocSingleFrame, FILE_LINE_FUNC);

	return CatmullRomBuilder::CreateCatmullRom(points, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionModel::Step_PathTransition(const Locator& refLocator,
									   const MotionModelInput& input,
									   const DC::MotionModelSettings* pSettings,
									   float deltaTime,
									   bool debugDraw)
{
	PROFILE(Animation, MM_StepPathTrans);

	const IPathWaypoints* pPathPs = &input.m_pPathInput->m_pathPs;

	MotionModelInput transInput = input;
	transInput.m_velDirPs = kZero;
	transInput.m_facingPs = MAYBE::kNothing;
	transInput.m_avoidNavObstacles = true;

	if (pPathPs && pPathPs->IsValid())
	{
		const Point pos = m_pos + m_animError;

		const bool stopAtGoal = input.m_pPathInput->m_stopAtGoal;

		const I32F numWaypoints = pPathPs->GetWaypointCount();

		I32F iPathLeg = 0;
		const Point closestPathPosPs = ClosestPointOnPath(pos,
														  pPathPs,
														  &iPathLeg,
														  stopAtGoal,
														  -1,
														  pSettings->m_transClampDist);

		m_pathOffsetDist = DistXz(pos, closestPathPosPs);

		Point prevWaypointPs = pPathPs->GetWaypoint(iPathLeg);
		Point nextWaypointPs = pPathPs->GetWaypoint(iPathLeg + 1);

		while ((iPathLeg < (numWaypoints - 2)) && (DistXz(pos, nextWaypointPs) < kPathAdvanceNudgeDist))
		{
			++iPathLeg;
			prevWaypointPs = nextWaypointPs;
			nextWaypointPs = pPathPs->GetWaypoint(iPathLeg + 1);
		}

		const bool onLastLeg = iPathLeg == (numWaypoints - 2);
		const float remLegDistance = (onLastLeg && !stopAtGoal) ? kLargeFloat : (float)DistXz(nextWaypointPs, closestPathPosPs);
		const float stopFaceDist = input.m_pPathInput->m_stoppingFaceDist;

		if (onLastLeg && (stopFaceDist >= 0.0f) && (remLegDistance < stopFaceDist))
		{
			const Vector goalFacingPs = GetLocalZ(input.m_pPathInput->m_goalRotPs);
			transInput.m_facingPs = goalFacingPs;
		}
		else if (input.m_pPathInput->m_strafeDirPs.Valid())
		{
			transInput.m_facingPs = input.m_pPathInput->m_strafeDirPs;
		}
		else
		{
			transInput.m_facingPs = MAYBE::kNothing;
		}

		const Vector desiredPathDirPs = AsUnitVectorXz(nextWaypointPs - prevWaypointPs, kZero);
		Scalar speed;
		const Vector curMoveDirPs = AsUnitVectorXz(m_velocity, desiredPathDirPs, speed);

		if (onLastLeg)
		{
			const Point midPos = Lerp(pPathPs->GetEndWaypoint(), closestPathPosPs, 0.5f);
			transInput.m_velDirPs = AsUnitVectorXz(midPos - pos, kZero);
		}
		else if (speed > 0.5f && DotXz(desiredPathDirPs, curMoveDirPs) < -0.1f)
		{
			transInput.m_velDirPs = kZero;
		}
		else if (m_runningIntoWall && (Length(m_velocity) > 0.1f))
		{
			transInput.m_velDirPs = kZero;
		}
		else
		{
			if (!m_transitionPathPs.Valid())
			{
				m_transitionPathPs = CreateSplineFromPath(pPathPs);
			}

			if (const CatmullRom* pPathSpline = m_transitionPathPs)
			{
				MotionModelSettingsSupplier ss(pSettings);
				MotionModelIntegrator integratorPs(*this, &ss);

				transInput.m_velDirPs = SolveInputDirToFollowSplinePs(pPathSpline,
																	  integratorPs,
																	  input.m_facingPs,
																	  Seconds(deltaTime));
			}
		}

		if (onLastLeg && stopAtGoal)
		{
			const Vector curAccel = GetAccel();
			const float decelK = GetVelKDecelForDirection(pSettings, transInput.m_velDirPs, transInput.m_facingPs);
			const float stopDist  = Max(ApproximateStoppingDistance(m_velocity, curAccel, decelK), kMinStopDist);

			if (stopDist >= remLegDistance)
			{
				transInput.m_velDirPs = kZero;
			}
		}

		m_lastPathLength = GetRemainingPathDistXz(pPathPs, iPathLeg, pos);
	}
	else
	{
		m_lastPathLength = -1.0f;
	}

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		PrimServerWrapper ps(refLocator);
		ps.EnableHiddenLineAlpha();

		ps.DrawArrow(m_pos + kUnitYAxis, SafeNormalize(transInput.m_velDirPs, kZero) * 0.5f, 0.25f, kColorCyan);
		ps.DrawArrow(m_pos + kUnitYAxis, SafeNormalize(m_velocity, kZero) * 0.5f, 0.25f, kColorMagentaTrans);
	}

	Step_Stick(refLocator, transInput, pSettings, deltaTime, debugDraw);

	// don't bother with variable clamp distance in this mode, just return max
	return pSettings->m_transClampDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionModel::IsOnPath(const MotionModelInput& input, const DC::MotionModelSettings* pSettings) const
{
	PROFILE(Animation, MotionModel_IsOnPath);

	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_forceOffPath))
	{
		return false;
	}

	const IPathWaypoints* pPathPs = &input.m_pPathInput->m_pathPs;
	const U32F waypointCount = pPathPs->GetWaypointCount();
	const bool stopAtGoal = input.m_pPathInput->m_stopAtGoal;

	ANIM_ASSERT(pPathPs && pPathPs->IsValid());

	I32F closestLeg		   = -1;
	const Point closestPos = ClosestPointOnPath(m_pos, pPathPs, &closestLeg, stopAtGoal, -1, 0.0f);

	const float distToPath = DistXz(closestPos, m_pos);
	const float testRadius = Max(1.1f * pSettings->m_transClampDist, 0.15f);

	if (distToPath > testRadius)
	{
		return false;
	}

	const float distToGoal = DistXz(m_pos, pPathPs->GetEndWaypoint());

	if (distToGoal <= testRadius)
	{
		// basically already there
		return true;
	}

	const Vector curVel = GetPathVel();

	Point pathPos0 = pPathPs->GetWaypoint(closestLeg);
	Point pathPos1 = pPathPs->GetWaypoint(closestLeg + 1);

	Vector pathDir = AsUnitVectorXz(pathPos1 - pathPos0, m_desiredFacing);
	Vector curDir  = AsUnitVectorXz(curVel, pathDir);

	const float initialRating = DotXz(pathDir, curDir);
	float curRating = initialRating;
	float nextRating = -kLargeFloat;
	float prevRating = -kLargeFloat;

	const float distToPos0 = DistXz(m_pos, pathPos0);
	if ((distToPos0 < pSettings->m_transClampDist) && (closestLeg > 0))
	{
		const Point pathPosPrev = pPathPs->GetWaypoint(closestLeg - 1);
		const Vector prevDir = AsUnitVectorXz(pathPos0 - pathPosPrev, pathDir);

		prevRating = DotXz(prevDir, curDir);

		if (prevRating > curRating)
		{
			curRating = prevRating;
			pathPos1 = pathPos0;
			pathPos0 = pathPosPrev;
			pathDir = prevDir;
			curDir = AsUnitVectorXz(curVel, pathDir);
		}
	}

	const float distToPos1 = DistXz(m_pos, pathPos1);
	if ((distToPos1 < pSettings->m_transClampDist) && (closestLeg < (waypointCount - 2)))
	{
		const Point pathPosNext = pPathPs->GetWaypoint(closestLeg + 2);
		const Vector nextDir = AsUnitVectorXz(pathPosNext - pathPos1, pathDir);

		nextRating = DotXz(nextDir, curDir);

		if (nextRating > curRating)
		{
			curRating = nextRating;
			pathPos0 = pathPos1;
			pathPos1 = pathPosNext;
			pathDir = nextDir;
			curDir = AsUnitVectorXz(curVel, pathDir);
		}
	}

	const float maxSpeed = GetMaxSpeedForDirection(pSettings, curDir, m_desiredFacing);
	const float curSpeed = Length(curVel);

	bool onPath = true;

	if (curSpeed > 0.5f)
	{
		float angleThreshDeg = pSettings->m_onPathTestAngleSameDeg;

		if (input.m_pPathInput->m_pathId != m_lastPathId)
		{
			angleThreshDeg = LerpScaleClamp(maxSpeed * 0.5f,
											maxSpeed,
											pSettings->m_onPathTestAngleMaxDeg,
											pSettings->m_onPathTestAngleMinDeg,
											curSpeed);
		}

		const float angleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(curDir, pathDir)));


		if (angleDiffDeg > angleThreshDeg)
		{
			//g_prim.Draw(DebugArrow(m_pos + Vector(0.0f, 0.1f, 0.0f), pathDir, kColorRed, 0.4f, kPrimEnableHiddenLineAlpha), Seconds(1.0f));
			onPath = false;
		}

/*
		if (!onPath && (closestLeg > 0) && DistXz(pathPos0, m_pos) < pSettings->m_transClampDist)
		{
			const Point pathPosPrev = pPathPs->GetWaypoint(closestLeg - 1);
			const Vector prevDir = AsUnitVectorXz(pathPos0 - pathPosPrev, kZero);
			const float prevAngleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(curDir, prevDir)));

			if (prevAngleDiffDeg <= angleThreshDeg)
			{
				onPath = true;
			}
		}

		if (!onPath && (closestLeg < (waypointCount - 1)) && DistXz(pathPos1, m_pos) < pSettings->m_transClampDist)
		{
			const Point pathPosNext = pPathPs->GetWaypoint(closestLeg + 2);
			const Vector nextDir = AsUnitVectorXz(pathPosNext - pathPos1, kZero);
			const float nextAngleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(curDir, nextDir)));

			if (nextAngleDiffDeg <= angleThreshDeg)
			{
				onPath = true;
			}
		}
*/
	}

	if (onPath)
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionModel::RecordNewCorners(const IPathWaypoints* pPath,
								   bool newPath,
								   float decelSpringK,
								   const DC::MotionModelSettings* pSettings)
{
	const I32F numWaypoints = pPath->GetWaypointCount();
	const float cullRadius = 7.5f;

	for (I32F iCorner = (m_numSavedCorners - 1); iCorner >= 0; --iCorner)
	{
		const float distToCorner = DistXz(m_pos, m_savedCorners[iCorner].m_pos);

		if (distToCorner > cullRadius)
		{
			m_savedCorners[iCorner] = m_savedCorners[m_numSavedCorners - 1];
			--m_numSavedCorners;
		}
	}

	U32F cornerCountStart = m_numSavedCorners;

	for (I32F i = 1; i < (numWaypoints - 1); ++i)
	{
		if (m_numSavedCorners >= kMaxSavedCorners)
		{
			break;
		}

		const Point p0 = pPath->GetWaypoint(i - 1);
		const Point p1 = pPath->GetWaypoint(i);
		const Point p2 = pPath->GetWaypoint(i + 1);

		const float distToVert = DistXz(p1, m_pos);
		if (distToVert > cullRadius)
		{
			continue;
		}

		bool alreadyExists = false;
		for (I32F iCorner = 0; iCorner < m_numSavedCorners; ++iCorner)
		{
			if (DistXz(p1, m_savedCorners[iCorner].m_pos) < 0.1f)
			{
				alreadyExists = true;
				break;
			}
		}

		if (alreadyExists)
		{
			continue;
		}

		const Vector dir0		 = AsUnitVectorXz(p1 - p0, kZero);
		const Vector dir1		 = AsUnitVectorXz(p2 - p1, kZero);
		const float angleDiffDeg = RADIANS_TO_DEGREES(GetRelativeXzAngleDiffRad(dir0, dir1));

		if (Abs(angleDiffDeg) < g_motionMatchingOptions.m_pathCornerGatherAngleDeg)
		{
			continue;
		}

		m_savedCorners[m_numSavedCorners].m_pos = p1;
		m_savedCorners[m_numSavedCorners].m_angleDeg = angleDiffDeg;
		++m_numSavedCorners;
	}

	if (newPath)
	{
		m_numEffectiveCorners = 0;
		cornerCountStart = 0;
	}

	if (m_numSavedCorners > cornerCountStart)
	{
		U32F numCornersRemoved = 0;
		U32F numNewCorners = 0;
		U32F numNewCornerMerges = 0;

		for (I32F iCorner = (m_numEffectiveCorners - 1); iCorner >= 0; --iCorner)
		{
			const float distToCorner = DistXz(m_pos, m_effectiveCorners[iCorner].m_pos);

			if (distToCorner > cullRadius)
			{
				m_effectiveCorners[iCorner] = m_effectiveCorners[m_numEffectiveCorners - 1];
				--m_numEffectiveCorners;
				++numCornersRemoved;
			}
		}

		bool dirty[kMaxSavedCorners] = { false };

		if ((m_numSavedCorners > 0) && (m_numEffectiveCorners < kMaxSavedCorners))
		{
			for (U32F i = cornerCountStart; i < m_numSavedCorners; ++i)
			{
				const PathCorner& nextCorner = m_savedCorners[i];

				EffectivePathCorner& prevCorner = (m_numEffectiveCorners > 0)
													  ? m_effectiveCorners[m_numEffectiveCorners - 1]
													  : m_effectiveCorners[0];

				if ((m_numEffectiveCorners > 0) && (Sign(nextCorner.m_angleDeg) == Sign(prevCorner.m_angleDeg))
					&& (DistXz(nextCorner.m_pos, prevCorner.m_pos) < 1.0f))
				{
					prevCorner.m_angleDeg += m_savedCorners[i].m_angleDeg;
					prevCorner.m_pos = Lerp(prevCorner.m_pos, nextCorner.m_pos, 0.5f);

					dirty[m_numEffectiveCorners - 1] = true;
					++numNewCornerMerges;
				}
				else if (m_numEffectiveCorners < kMaxSavedCorners)
				{
					EffectivePathCorner& newCorner = m_effectiveCorners[m_numEffectiveCorners];
					newCorner.m_pos = nextCorner.m_pos;
					newCorner.m_angleDeg = nextCorner.m_angleDeg;

					dirty[m_numEffectiveCorners] = true;
					++m_numEffectiveCorners;
					++numNewCorners;
				}
			}
		}

		for (I32F i = 0; i < m_numEffectiveCorners; ++i)
		{
			const EffectivePathCorner& corner = m_effectiveCorners[i];

			if (!dirty[i])
			{
				//ANIM_ASSERT(corner.m_maxSpeed >= pSettings->m_pathCornerSpeedMin);
				continue;
			}

			const float absAngleDiffDeg = Abs(corner.m_angleDeg);

			const float maxCornerSpeed = LerpScaleClamp(pSettings->m_pathCornerAngleMin,
														pSettings->m_pathCornerAngleMax,
														pSettings->m_pathCornerSpeedMax,
														pSettings->m_pathCornerSpeedMin,
														absAngleDiffDeg);

			const float maxSpeed = GetMaxSpeedForDirection(pSettings, kUnitZAxis, Vector(kUnitZAxis));
			const float speedDelta = Max(maxSpeed - maxCornerSpeed, 0.0f);
			const float timeAtMinSpeed = pSettings->m_pathCornerTimeAtMinSpeed;
			const float distAtMinSpeed = Max(maxCornerSpeed * timeAtMinSpeed, 0.1f);
			const float stoppingDist = ApproximateStoppingDistance(speedDelta, 0.0f, decelSpringK);
			const float radiusMax =  Limit(stoppingDist, 0.5f, 10.0f);

			m_effectiveCorners[i].m_maxSpeed = maxCornerSpeed;
			m_effectiveCorners[i].m_radiusMin = distAtMinSpeed;
			m_effectiveCorners[i].m_radiusMax = distAtMinSpeed + radiusMax;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionModel::GetCornerLimitedMaxSpeed(Point_arg pos,
											float desiredSpeed,
											float decelSpringK,
											const DC::MotionModelSettings* pSettings) const
{
	float speed = desiredSpeed;

	for (I32F iCorner = 0; iCorner < m_numEffectiveCorners; ++iCorner)
	{
		const EffectivePathCorner& corner = m_effectiveCorners[iCorner];

		const float absAngleDiffDeg = Abs(corner.m_angleDeg);

		if (Abs(corner.m_angleDeg) < pSettings->m_pathCornerAngleFloor)
		{
			continue;
		}

		const float distToCorner = DistXz(pos, corner.m_pos);

		if (distToCorner >= corner.m_radiusMax)
		{
			continue;
		}

		const float baseTT = LerpScaleClamp(corner.m_radiusMin, corner.m_radiusMax, 1.0f, 0.0f, distToCorner);
		const float tt	   = CalculateCurveValue(baseTT, DC::kAnimCurveTypeUniformS);
		const float interpMaxCornerSpeed = Lerp(desiredSpeed, corner.m_maxSpeed, tt);

		speed = Min(interpMaxCornerSpeed, speed);
	}

	return speed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionModel::GetTurnRateDps(const DC::MotionModelSettings* pSettings,
								  Vector_arg curFaceDir,
								  Vector_arg desFaceDir,
								  float* pNewFacingCosAngleOut /* = nullptr */) const
{
	if (m_forceTurnRate > 0.0f)
		return m_forceTurnRate;

	I32F closestCorner = -1;
	float bestDist = kLargeFloat;

	if (pSettings->m_turnRateDpsDirectional.m_count > 0)
	{
		for (U32F i = 0; i < m_numEffectiveCorners; ++i)
		{
			const float distToCorner = DistXz(m_pos, m_effectiveCorners[i].m_pos);

			if (distToCorner >= m_effectiveCorners[i].m_radiusMax)
				continue;

			if (distToCorner < bestDist)
			{
				bestDist = distToCorner;
				closestCorner = i;
			}
		}
	}

	float turnRateDps = m_turnRateDps;

	if (closestCorner >= 0)
	{
		const float cornerAngleDeg = m_effectiveCorners[closestCorner].m_angleDeg;
		turnRateDps = NdUtil::EvaluatePointCurve(cornerAngleDeg, &pSettings->m_turnRateDpsDirectional);

		if (pNewFacingCosAngleOut)
			*pNewFacingCosAngleOut = kLargeFloat;
	}
	else
	{
		const float newFacingCosAngle = Dot(m_facing, m_desiredFacing);

		if (newFacingCosAngle < m_lastTurnRateSelCosAngle)
		{
			turnRateDps = -1.0f;
		}

		const float curSpeed = Length(GetVel());

		if (turnRateDps < 0.0f)
		{
			turnRateDps = GetTurnRateDpsForDirection(pSettings, curFaceDir, desFaceDir, curSpeed);

			if (pNewFacingCosAngleOut)
				*pNewFacingCosAngleOut = newFacingCosAngle;
		}
		else if (pSettings->m_turnRateDpsDirectional.m_count > 0)
		{
			const DC::PointCurve* pTurnCurve = &pSettings->m_turnRateDpsDirectional;
			const DC::PointCurve* pIdleTurnCurve = &pSettings->m_turnRateDpsDirectionalIdle;

			if (pIdleTurnCurve->m_count > 0 && curSpeed > 0.0f && pIdleTurnCurve->m_values[0] > 0.0f)
			{
				const float speedPct = curSpeed / pSettings->m_maxSpeed;
				if (speedPct < pSettings->m_movingGroupSpeedPercentThreshold)
				{
					pTurnCurve = pIdleTurnCurve;
				}
			}

			// NB: this is actually the wrong thing to use here, since we really want the *signed* angle we used
			// when we last selected the turn rate, but that's a bigger change than I want to make this close to ship
			const float prevAngleDeg = RADIANS_TO_DEGREES(SafeAcos(m_lastTurnRateSelCosAngle));
			turnRateDps = NdUtil::EvaluatePointCurve(prevAngleDeg, pTurnCurve);
		}
	}

	return turnRateDps;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionModel::Step_Custom(const Locator& refLocator,
							   const MotionModelInput& input,
							   const DC::MotionModelSettings* pSettings,
							   float deltaTime,
							   bool debugDraw)
{
	PROFILE(Animation, MM_StepAnim);

	float clampDist = pSettings->m_transClampDist;

	if (!input.m_pInterface)
	{
		return clampDist;
	}

	if (deltaTime < NDI_FLT_EPSILON)
	{
		return clampDist;
	}

	const Point prevPos = m_pos;

	Locator alignLoc;
	float clampFactor = 1.0f;

	if (!input.m_pInterface->DoCustomModelStep(*this, input, deltaTime, &alignLoc, &clampFactor, debugDraw))
	{
		return clampDist;
	}

	const Point newPos = alignLoc.Pos();
	const Vector newVel = (newPos - prevPos) / deltaTime;
	const Vector newFacing = GetLocalZ(alignLoc);

	SetPos(newPos);
	SetVel(newVel);

	m_desiredVelocity = newVel;
	m_facing		= newFacing;
	m_desiredFacing = newFacing;
	m_userFacing	= MAYBE::kNothing;

	Scalar newSpeed;
	const Vector moveDir = SafeNormalize(m_velocity, kZero, newSpeed);

	m_velSpring.Reset();
	m_speedSpring.Reset();
	m_sprungSpeed = newSpeed;
	m_maxSpeed = GetMaxSpeedForDirection(pSettings, moveDir, m_desiredFacing);

	return clampDist * clampFactor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SMath::Point ComputeStoppingPosition(const MotionModelIntegrator& model)
{
	PROFILE_AUTO(Animation);
	// Maybe find an analytic way to do this
	MotionModelIntegrator curModel = model;
	float dt = 1.0f / 30.0f;
	MotionModelInput input;
	input.m_velDirPs = kZero;
	int iter		 = 0;
	while (Length(curModel.Model().GetVel()) > 0.01f)
	{
		curModel = curModel.Step(input, dt);
		++iter;
	}
	return curModel.Model().GetPos();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SMath::Point ApproximateStoppingPosition(const MotionModelIntegrator& model)
{
	PROFILE_AUTO(Animation);
	auto settings = model.GetSettings();

	const MotionModel& mm = model.Model();
	const Vector modelVel = mm.GetVel();
	const Vector modelAcc = mm.GetAccel();
	const float decelK = GetVelKDecelForDirection(&settings, mm.GetDesiredVel(), mm.GetUserFacing());
	const float springConstSqr = Sqr(decelK);

	const float dx = ApproximateStoppingDistance(modelVel.X(), modelAcc.X(), springConstSqr);
	const float dz = ApproximateStoppingDistance(modelVel.Z(), modelAcc.Z(), springConstSqr);

	//const float dx = ApproximateStoppingDistance(modelVel.X(), 0.0f, springConstSqr);
	//const float dz = ApproximateStoppingDistance(modelVel.Z(), 0.0f, springConstSqr);

	const Point stopPos = mm.GetPos() + Vector(dx, 0.0f, dz);

	return stopPos;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float EvaluateDirectionalCurve(const DC::PointCurve* pCurve, Vector_arg moveDir, Maybe<Vector> maybeFaceDir)
{
	Angle moveAngle(0.0f);
	if (maybeFaceDir.Valid())
	{
		Scalar moveLen = kZero;
		Vector dir = SafeNormalize(moveDir, kZero, moveLen);
		if (moveLen > 0.0f)
		{
			const Vector faceDir = SafeNormalize(maybeFaceDir.Get(), dir);
			moveAngle = AngleFromXZVec(faceDir) - AngleFromXZVec(moveDir);
		}
	}
	const float speed = NdUtil::EvaluatePointCurve(moveAngle.ToDegrees(), pCurve);
	return speed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float EvaluateDirectionalCurveWithHint(const DC::PointCurve* pCurve,
									   Vector_arg moveDir,
									   Maybe<Vector> maybeFaceDir /* = MAYBE::kNothing */,
									   Maybe<bool> hintDirLeft /* = MAYBE::kNothing */,
									   float hintAdjustStrength /* = 1.0f */)
{
	Angle moveAngle(0.0f);
	if (maybeFaceDir.Valid())
	{
		Scalar moveLen = kZero;
		Vector dir = SafeNormalize(moveDir, kZero, moveLen);
		if (moveLen > 0.0f)
		{
			const Vector faceDir = SafeNormalize(maybeFaceDir.Get(), dir);
			moveAngle = AngleFromXZVec(faceDir) - AngleFromXZVec(moveDir);
		}

		if (hintDirLeft.Valid())
		{
			const float curDeg = moveAngle.ToDegrees();
			if (hintDirLeft.Get() ^ (curDeg < 0.0f))
			{
				const float curValue = moveAngle.m_value;
				const float desiredValue = 179.9f * -Sign(curDeg);
				const float finalValue = Lerp(curValue, desiredValue, MinMax01(hintAdjustStrength));
				moveAngle.Assign(finalValue);
			}
		}
	}
	const float speed = NdUtil::EvaluatePointCurve(moveAngle.ToDegrees(), pCurve);
	return speed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetMaxSpeedForDirection(const DC::MotionModelSettings* pSettings,
							  Vector_arg moveDir,
							  Maybe<Vector> maybeFaceDir /* = MAYBE::kNothing */)
{
	float speed = pSettings->m_maxSpeed;
	if (pSettings->m_maxSpeedDirectional.m_count > 0)
	{
		speed = EvaluateDirectionalCurve(&pSettings->m_maxSpeedDirectional, moveDir, maybeFaceDir);
	}
	return speed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetVelKForDirection(const DC::MotionModelSettings* pSettings,
						  Vector_arg moveDir,
						  Maybe<Vector> maybeFaceDir, /* = MAYBE::kNothing */
						  float curSpeed /* = 0.0f */)
{
	float k = pSettings->m_velocitySpringConst;

	if (pSettings->m_velocityKDirectional.m_count > 0)
	{
		const DC::PointCurve* pKCurve = &pSettings->m_velocityKDirectional;
		const DC::PointCurve* pIdleKCurve = &pSettings->m_velocityKDirectionalIdle;

		if (pIdleKCurve->m_count > 0 && curSpeed > 0.0f && pIdleKCurve->m_values[0] > 0.0f)
		{
			const float speedPct = curSpeed / pSettings->m_maxSpeed;
			if (speedPct < pSettings->m_movingGroupSpeedPercentThreshold)
			{
				pKCurve = pIdleKCurve;
			}
		}

		k = EvaluateDirectionalCurve(pKCurve, moveDir, maybeFaceDir);
	}

	return k;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetVelKDecelForDirection(const DC::MotionModelSettings* pSettings,
							   Vector_arg moveDir,
							   Maybe<Vector> maybeFaceDir /* = MAYBE::kNothing */)
{
	float k = -1.0f;

	bool hasDecel = (pSettings->m_velocitySpringConstDecel >= 0.0f);

	if (!hasDecel && (pSettings->m_velocityKDecelDirectional.m_count > 0))
	{
		hasDecel = pSettings->m_velocityKDecelDirectional.m_values[0] >= 0.0f;
	}

	if (hasDecel)
	{
		k = pSettings->m_velocitySpringConstDecel;

		if (pSettings->m_velocityKDecelDirectional.m_count > 0)
		{
			k = EvaluateDirectionalCurve(&pSettings->m_velocityKDecelDirectional, moveDir, maybeFaceDir);
		}

		ANIM_ASSERTF(k >= 0.0f, ("Invalid decel spring K for model settings"));
	}
	else
	{
		k = GetVelKForDirection(pSettings, moveDir, maybeFaceDir);
	}

	return k;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetTurnRateDpsForDirection(const DC::MotionModelSettings* pSettings,
								 Vector_arg curFaceDir,
								 Vector_arg desFaceDir,
								 float curSpeed /* = -1.0f */)
{
	float turnRate = pSettings->m_turnRateDps;

	if (pSettings->m_turnRateDpsDirectional.m_count > 0)
	{
		const DC::PointCurve* pTurnCurve = &pSettings->m_turnRateDpsDirectional;
		const DC::PointCurve* pIdleTurnCurve = &pSettings->m_turnRateDpsDirectionalIdle;

		if (pIdleTurnCurve->m_count > 0 && curSpeed > 0.0f && pIdleTurnCurve->m_values[0] > 0.0f)
		{
			const float speedPct = curSpeed / pSettings->m_maxSpeed;
			if (speedPct < pSettings->m_movingGroupSpeedPercentThreshold)
			{
				pTurnCurve = pIdleTurnCurve;
			}
		}

		turnRate = EvaluateDirectionalCurve(pTurnCurve, curFaceDir, desFaceDir);
	}

	return turnRate;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDrawSample(const Locator& parentSpace,
							const MotionModel& motion,
							Point_arg prevPos,
							I32F i,
							float sampleTime,
							bool recordSample)
{
	PrimServerWrapper ps(parentSpace);
	ps.SetDuration(kPrimDuration1FrameAuto);
	ps.EnableWireFrame();
	ps.EnableHiddenLineAlpha();

	const Point modelPos = motion.GetPos();
	const Vector vo = Vector(0.0f, 0.03f, 0.0f); //Vector(0.0f, float(i) * 0.03f, 0.0f);
	const Point drawPos = modelPos + vo;

	const Color sampleColor = recordSample ? kColorYellow : kColorBlueTrans;

	ps.DrawCross(drawPos, 0.05f, sampleColor);
	ps.DrawCross(modelPos, 0.05f, sampleColor);
	ps.DrawLine(drawPos, modelPos, sampleColor);

	ps.DisableWireFrame();

	ps.SetLineWidth(4.0f);
	ps.DrawLine(prevPos, modelPos, kColorBlueTrans);

	bool drawFacing = true;
	if (drawFacing)
	{
		const Vector curFacing = motion.GetFacing();
		const Vector desFacing = motion.GetDesiredFacing();
		ps.DrawArrow(drawPos, curFacing * 0.25f, 0.15f, recordSample ? kColorOrange : kColorGrayTrans);

		if (recordSample)
		{
			ps.DrawString(drawPos + (curFacing * 0.25f), "facing", kColorOrangeTrans, 0.5f);
		}

		if ((Dot(curFacing, desFacing) < 0.95f) && recordSample)
		{
			ps.DrawArrow(drawPos, desFacing * 0.25f, 0.25f, recordSample ? kColorGreen : kColorGreenTrans);

			ps.DrawString(drawPos + (desFacing * 0.25f), "des-facing", kColorGreenTrans, 0.5f);
		}
	}

	Point textPos = drawPos;

	StringBuilder<256> desc;

	const Vector vel = motion.GetVel();
	Scalar speed = kZero;
	const Vector velDir = SafeNormalize(vel, kZero, speed);
	if (speed > NDI_FLT_EPSILON)
	{
		ps.DrawArrow(drawPos, velDir * 0.25f, 0.2f, sampleColor);
		desc.append_format("act: %0.2fm/s\n", float(speed));
	}

	const Vector desVel = motion.GetDesiredVel();
	Scalar desSpeed = kZero;
	const Vector desVelDir = SafeNormalize(desVel, kZero, desSpeed);
	if (desSpeed > NDI_FLT_EPSILON)
	{
		ps.DrawArrow(drawPos, desVelDir * 0.25f, 0.2f, kColorCyanTrans);
		desc.append_format("des: %0.2fm/s\n", float(desSpeed));
	}

	if (recordSample)
	{
		const float sampleSpeed = Length(motion.GetVel());
		const float sampleYaw = motion.GetYawSpeed();
		desc.append_format("%0.2fs [%0.1fm/s %0.1frad/s]\n", sampleTime, sampleSpeed, sampleYaw);
	}

	if (!desc.empty())
	{
		ps.DrawString(drawPos, desc.c_str(), recordSample ? kColorYellow : kColorCyanTrans, 0.5f);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GetDesiredTrajectory(const Locator& refLocator,
						  const Locator& parentSpace,
						  MotionModel motion,
						  const IMotionModelSettingsSupplier* pSettings,
						  int numSamples,
						  float endTime,
						  MotionModelInputFunc futureStick,
						  AnimTrajectory* pTrajectoryOut,
						  bool debug /* false */)
{
	PROFILE_AUTO(Animation);

	Point prevPos = motion.GetPos();

	const bool debugDraw = FALSE_IN_FINAL_BUILD(debug && g_motionMatchingOptions.m_drawOptions.m_drawTrajectorySamples);
	const bool debugDrawModel = FALSE_IN_FINAL_BUILD(debugDraw && g_motionMatchingOptions.m_drawOptions.m_drawMotionModel);

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		DebugDrawSample(parentSpace, motion, motion.GetPos(), 0, 0.0f, false);
	}

	const I32F numSteps = 1 + (numSamples * g_motionMatchingOptions.m_trajectorySampleResolution);
	const float invSteps = (numSteps > 1) ? (1.0f / float(numSteps - 1)) : 1.0f;
	const float dt = endTime * invSteps;

	for (I32F i = 1; i < numSteps; ++i)
	{
		const float tt = float(i) * invSteps;
		const float sampleTime = endTime * tt;

		MotionModelIntegrator integeratorPs = MotionModelIntegrator(motion, pSettings);
		integeratorPs.SetTime(Seconds(sampleTime));
		const MotionModelInput modelInput = futureStick(integeratorPs,
														Seconds(sampleTime),
														/* finalize = */ false);

		const DC::MotionModelSettings settings = pSettings->GetSettings(Seconds(sampleTime));

		prevPos = motion.GetPos();
		motion.Step(parentSpace, modelInput, &settings, dt, debugDrawModel);

		const bool recordSample = i % g_motionMatchingOptions.m_trajectorySampleResolution == 0;

		if (recordSample)
		{
			AnimTrajectorySample goal;
			goal.SetTime(sampleTime);
			goal.SetPosition(refLocator.UntransformPoint(motion.GetPos()));
			goal.SetVelocity(refLocator.UntransformVector(motion.GetVel()));
			goal.SetFacingDir(refLocator.UntransformVector(motion.GetFacing()));
			goal.SetYawSpeed(motion.GetYawSpeed());

			pTrajectoryOut->Add(goal);
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			DebugDrawSample(parentSpace, motion, prevPos, i, sampleTime, recordSample);
		}
	}

	{
		const MotionModelInput modelInput = futureStick(MotionModelIntegrator(motion, pSettings),
														Seconds(endTime),
														/* finalize = */ true);

		const DC::MotionModelSettings settings = pSettings->GetSettings(Seconds(endTime));
		motion.Step(parentSpace, modelInput, &settings, dt, debugDrawModel);

		const Vector tailFacingPs = motion.GetFacing();
		const Vector tailFacingOs = refLocator.UntransformVector(tailFacingPs);

		pTrajectoryOut->UpdateTailFacing(tailFacingOs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModelIntegrator MotionModelIntegrator::Step(const MotionModelInput& input, float deltaTime) const
{
	const DC::MotionModelSettings settings = m_pSettingsSupplier->GetSettings(m_timeInFuture);

	MotionModel nextModel = m_model;
	// NB: The locator here is only used for debugging so ... punt
	nextModel.Step(kIdentity, input, &settings, deltaTime);

	MotionModelIntegrator result = MotionModelIntegrator(nextModel, m_pSettingsSupplier);
	result.m_timeInFuture = m_timeInFuture + Seconds(deltaTime);
	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionModelIntegrator::GetMaxSpeed() const
{
	const auto settings = m_pSettingsSupplier->GetSettings(m_timeInFuture);
	return MaxSpeed(settings);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionModelIntegrator::GetMaxAccel() const
{
	const auto settings = m_pSettingsSupplier->GetSettings(m_timeInFuture);
	return MaxAccel(Model(), settings);
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::MotionModelSettings MotionModelIntegrator::GetSettings() const
{
	return m_pSettingsSupplier->GetSettings(m_timeInFuture);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionModelIntegrator::MaxSpeed(const DC::MotionModelSettings& settings)
{
	if (settings.m_maxSpeedDirectional.m_count <= 0)
	{
		return settings.m_maxSpeed;
	}
	else
	{
		float maxSpeed = 0.0f;
		for (int i = 0; i < settings.m_maxSpeedDirectional.m_count; i++)
		{
			maxSpeed = Max(maxSpeed, settings.m_maxSpeedDirectional.m_values[i]);
		}
		return maxSpeed;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
float MotionModelIntegrator::MaxAccel(const MotionModel& model, const DC::MotionModelSettings& settings)
{
	const float maxSpeed = MaxSpeed(settings);

	const float springK = GetVelKForDirection(&settings, model.GetDesiredVel(), model.GetUserFacing());

	SpringTracker<float> speedSpring;
	float speed = maxSpeed;
	speedSpring.Track(speed, -maxSpeed, 1.0f / 30.0f, springK);

	if (settings.m_maxAccel > 0.0f)
	{
		return Min(Abs(speedSpring.m_speed), settings.m_maxAccel);
	}

	return Abs(speedSpring.m_speed);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float ApproximateCurvatureAtAtclLength(const CatmullRom* pSpline, float s, float ds = 0.02f)
{
	PROFILE_AUTO(Animation);
	Point p[3];
	Vector dir;
	Vector tan;
	pSpline->EvaluateAtArcLength(s - ds, p[0], dir, tan);
	pSpline->EvaluateAtArcLength(s, p[1], dir, tan);
	pSpline->EvaluateAtArcLength(s + ds, p[2], dir, tan);

	Vector vel = (p[2] - p[0]) / (ds*2.0f);
	Vector accel = (p[2] - p[1] + p[0] - p[1]) / (Sqr(ds));

	const float denom = Pow((Sqr(vel.X()) + Sqr(vel.Z())), 1.5f);

	float k = 0.0f;

	if (denom > NDI_FLT_EPSILON)
	{
		k = (vel.X()*accel.Z() - vel.Z()*accel.X()) / denom;
	}

	return k;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct VelErrorFuncContext
{
	const MotionModelIntegrator& m;
	Vector_arg targetVel;
	const float dt;
	const Maybe<Vector> facingDir;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static float VelErrorFunc(float x, void* pContext)
{
	PROFILE_AUTO(Animation);
	const VelErrorFuncContext* pFuncContext = static_cast<const VelErrorFuncContext*>(pContext);

	Angle a(x);
	Vector inputDir = VectorFromAngleXZ(a);
	MotionModelInput input;
	input.m_facingPs = pFuncContext->facingDir;
	input.m_velDirPs = inputDir;
	float dist = LengthSqr(pFuncContext->m.Step(input, pFuncContext->dt).Model().GetVel() - pFuncContext->targetVel);
	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vector SolveInputForDesiredVelPs(const MotionModelIntegrator& m,
										Vector_arg targetVelPs,
										const float dt,
										const Maybe<Vector> facingDirPs)
{
	PROFILE_AUTO(Animation);
	MotionModelInput input;
	Vector bestInputPs = kZero;
	input.m_velDirPs = bestInputPs;
	input.m_facingPs = facingDirPs;
	Vector bestVelPs = m.Step(input, dt).Model().GetVel();
	Scalar minVelDist = LengthSqr(bestVelPs - targetVelPs);

	VelErrorFuncContext context{ m, targetVelPs, dt, facingDirPs };

	float f0 = VelErrorFunc(0.0f, &context);
	float f180 = VelErrorFunc(180.0f, &context);

	float ax, bx, cx;
	if (f0 > f180)
	{
		ax = 0.0f;
		bx = 180.0f;
		cx = 360.0f;
	}
	else
	{
		ax = 180.0f;
		bx = 360.0f;
		cx = 540.0f;
	}

	auto result = GolenSectionMinimizer::Minimize(VelErrorFunc, ax, bx, cx, &context, 1.0f);

	if (std::get<1>(result) < minVelDist)
	{
		return VectorFromAngleXZ(Angle(std::get<0>(result)));
	}

	return bestInputPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector SolveInputDirToFollowSplinePs(const CatmullRom* pSplinePs,
									 const MotionModelIntegrator& integratorPs,
									 Maybe<Vector> facingDirPs,
									 TimeFrame delta)
{
	PROFILE_AUTO(Animation);
	PDTracker3D trackerPs;

	const float maxSpeed = integratorPs.GetMaxSpeed();
	const float maxAccel = integratorPs.GetMaxAccel() * 0.25f;

	const Point modelPosPs = integratorPs.Model().GetPos();
	const Vector modelVelPs = LimitVectorLength(integratorPs.Model().GetVel(), 0.0f, maxSpeed);
	const float modelSpeed = Length(modelVelPs);

	trackerPs.SetPos(modelPosPs);
	trackerPs.SetVel(modelVelPs);

	//Magic numbers from pole placement with desired poles [-4.0 -3.2]
	trackerPs.SetPosGain(12.8f);
	trackerPs.SetVelGain(7.2f);

	trackerPs.SetMaxSpeed(maxSpeed);
	trackerPs.SetMaxAccel(1000.0f);

	F32 s = pSplinePs->FindArcLengthClosestToPoint(trackerPs.Pos());
	float curveInitial = ApproximateCurvatureAtAtclLength(pSplinePs, s);

	F32	lookAheadTime = 0.2f;
	F32	lookAheadSpeed = Min(modelSpeed, Sqrt(maxAccel / Abs(curveInitial)));
	F32	lookAheadDist = lookAheadSpeed * lookAheadTime; //Max(lookAheadSpeed * lookAheadTime, 0.25f);
	Point  p;
	Vector dir;
	Vector tan;
	pSplinePs->EvaluateAtArcLength(s + lookAheadDist, p, dir, tan);

	const float k = ApproximateCurvatureAtAtclLength(pSplinePs, s + lookAheadDist);

	//if (modelSpeed < g_navCharOptions.m_locomotionController.m_mmStoppedSpeed)
/*
	if (modelSpeed < 1.0f)
	{
		const Vector desInputDir = SafeNormalize(p - modelPosWs, dir);
		const Vector inputDirXz = AsUnitVectorXz(desInputDir, kZero);
		return inputDirXz;
	}
*/

	float maxSpeedForTurn = maxSpeed;
	if (Abs(k) > NDI_FLT_EPSILON)
	{
		maxSpeedForTurn = Min(maxSpeed, Sqrt(maxAccel / Abs(k)));
	}

	Vector desiredVel = SafeNormalize(dir, kZero) * maxSpeedForTurn;

	const Point lastPointPs = pSplinePs->GetControlPoint(pSplinePs->GetControlPointCount());
	if (Dist(p, lastPointPs) < 0.05f)
	{
		desiredVel = kZero;
	}

	if (Dist(p, modelPosPs) < 0.05f)
	{
		const Vector inputDirXz = AsUnitVectorXz(dir, kZero);
		return inputDirXz;
	}

	Vector accel = trackerPs.Step(p, desiredVel, ToSeconds(delta));

	const Vector deltaVelPs = trackerPs.Vel() - modelVelPs;
	const Vector inputDirPs = (Length(deltaVelPs) < 0.2f) ? dir : SafeNormalize(deltaVelPs, kZero);
	const Vector inputDirXzPs = AsUnitVectorXz(inputDirPs, kZero);
	//SolveInputForDesiredVel(m, tracker.Vel(), GetProcessDeltaTime(), facingDir);

	if (FALSE_IN_FINAL_BUILD(false))
	{
		//g_prim.Draw(DebugCross(tracker.Pos(), 0.2f, kColorGreen));

		const float deltaS = ToSeconds(delta);

		const Vector vo = Vector(0.0f, 1.0f + deltaS, 0.0f);

		g_prim.Draw(DebugCross(modelPosPs + vo, 0.15f, kColorBlue));
		if (delta > Seconds(0.0f))
		{
			g_prim.Draw(DebugString(modelPosPs + vo, StringBuilder<256>("%0.2fs", deltaS).c_str(), kColorWhite, 0.5f));
		}

		//g_prim.Draw(DebugArrow(modelPosWs + vo, p + vo, kColorGreen));
		//g_prim.Draw(DebugArrow(modelPosWs + vo, modelPosWs + vo + modelVelWs, kColorBlue));

		//g_prim.Draw(DebugArrow(modelPosWs + vo, tracker.Vel(), kColorOrange));
		g_prim.Draw(DebugArrow(modelPosPs + vo, inputDirXzPs, kColorGreen));
		g_prim.Draw(DebugArrow(modelPosPs + vo, SafeNormalize(desiredVel, kZero), kColorBlue));

		//pSplinePs->Draw();
	}

	return inputDirXzPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Segment NavObstacles::GetObstacleWs(int i) const
{
	const NavPolyEdge& edge = m_pEdges->GetResults()[i];
	if (!edge.m_pSourceMesh)
	{
		return Segment(kOrigin, kOrigin);
	}

	return Segment(edge.m_pSourceMesh->LocalToWorld(edge.m_v0), edge.m_pSourceMesh->LocalToWorld(edge.m_v1));
}

/// --------------------------------------------------------------------------------------------------------------- ///
int NavObstacles::NumObstacles() const
{
	if (!m_pEdges)
	{
		return 0;
	}

	return m_pEdges->GetResults().Size();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector ApplyObstacleAvoidancePs(Vector_arg desInputPs,
								const Locator& parentSpace,
								const MotionModelIntegrator& integratorPs,
								const IObstacles& obstacles,
								float avoidanceRadius,
								const Point* pStoppingPosPs /* = nullptr */,
								bool debugDraw /* = false */)
{
	const Point stopPosPs = pStoppingPosPs ? *pStoppingPosPs : ComputeStoppingPosition(integratorPs);
	const Point posPs	  = integratorPs.Model().GetPos();

	return ApplyObstacleAvoidancePs(desInputPs,
									parentSpace,
									posPs,
									stopPosPs,
									obstacles,
									avoidanceRadius,
									nullptr,
									debugDraw);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector ApplyObstacleAvoidancePs(Vector_arg desInputPs,
								const Locator& parentSpace,
								Point_arg posPs,
								Point_arg futurePosPs,
								const IObstacles& obstacles,
								float avoidanceRadius,
								Point* pClosestEdgePosOut /* = nullptr */,
								bool debugDraw /* = false */)
{
	PROFILE_AUTO(Animation);

	Vector inputPs = desInputPs;

	const Segment moveSegPs = Segment(posPs, futurePosPs);
	const Vector curMoveDirPs = SafeNormalize(futurePosPs - posPs, kZero);

	float closestDist = kLargeFloat;
	Point closestEdgePosPs = kInvalidPoint;

	const I32F numObs = obstacles.NumObstacles();

	for (I32F iObs = 0; iObs < numObs; iObs++)
	{
		const Segment edgeWs = obstacles.GetObstacleWs(iObs);
		const Segment edgePs = Segment(parentSpace.UntransformPoint(edgeWs.a), parentSpace.UntransformPoint(edgeWs.b));

		Point closestPosPs = posPs;
		const float edgeDist = DistPointSegmentXz(posPs, edgePs.a, edgePs.b, &closestPosPs);
		const Vector dirPs = AsUnitVectorXz(posPs - closestPosPs, kZero);
		const Vector wallNormalPs = RotateY90(AsUnitVectorXz(edgePs.b - edgePs.a, kZero));

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			PrimServerWrapper ps(parentSpace);
			ps.EnableHiddenLineAlpha();
			ps.DrawLine(edgePs.a, edgePs.b, kColorRed);
			ps.DrawArrow(AveragePos(edgePs.a, edgePs.b), wallNormalPs * 0.2f, 0.25f, kColorRed);
		}

		const Scalar dotP = Dot(dirPs, wallNormalPs);
		if ((dotP < 0.0f) && (edgeDist > 0.15f))
			continue;

		const Segment edgeSegPs = Segment(edgePs.a, edgePs.b);

/*
		float edgeT = -1.0f;
		if (!IntersectSegmentStadiumXz(edgeSegPs, moveSegPs, avoidanceRadius, edgeT))
			continue;
*/

		Scalar t0;
		Scalar t1;
		Scalar dist = DistSegmentSegmentXz(moveSegPs, edgeSegPs, t0, t1);

		if (dist >= avoidanceRadius)
			continue;

		if (dist < closestDist)
		{
			closestDist = dist;
			closestEdgePosPs = closestPosPs;
		}

		// if we're moving into the wall, just zero out our input
		const Scalar movingIntoWallDot = Dot(curMoveDirPs, wallNormalPs);
		//const float stopThreshold = LerpScale(avoidanceRadius * 0.25f, avoidanceRadius, -0.25f, -0.75f, edgeDist);
		const float stopThreshold = -0.75f;
		const bool stop = movingIntoWallDot < stopThreshold;
		if (stop)
		{
			inputPs = kZero;
		}
		else
		{
			const Scalar inputDotP = Dot(inputPs, dirPs);

			if (inputDotP >= 0.0f)
				continue;

			inputPs -= Dot(inputPs, dirPs) * dirPs;
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			PrimServerWrapper ps(parentSpace);

			ps.EnableHiddenLineAlpha();

			//ps.DrawArrow(closestPosPs, dirPs, 0.5f, kColorRed);
			ps.DrawArrow(closestPosPs, wallNormalPs, 0.5f, stop ? kColorRed : kColorYellow);
			//StringBuilder<256> desc;
			//desc.append_format("in dot: %f\n", float(inputDotP));
			//ps.DrawString(closestPosPs + wallNormalPs, desc.c_str(), kColorYellow, 0.5f);
			//ps.DrawArrow(closestPosPs + wallNormalPs, inputPs, 0.5f, kColorCyanTrans);
			//ps.DrawArrow(closestPosPs + wallNormalPs, desInputPs, 0.5f, kColorMagentaTrans);
		}

		if (stop)
		{
			break;
		}
	}

	if (pClosestEdgePosOut)
	{
		*pClosestEdgePosOut = closestEdgePosPs;
	}

	return SafeNormalize(inputPs, kZero);
}
