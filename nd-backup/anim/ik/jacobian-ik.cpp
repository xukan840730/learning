/*
* Copyright (c) 2012 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "ndlib/anim/ik/jacobian-ik.h"

#include "corelib/memory/relocate.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/ik/vector-matrix.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/text/string-builder.h"

#include "gamelib/scriptx/h/ik-defines.h"

#include <Eigen/Dense>

/// --------------------------------------------------------------------------------------------------------------- ///
struct JacobianDampedLeastSquaresMatrixSet
{
	JacobianDampedLeastSquaresMatrixSet(int numEndEffectors, int numJoints)
		: jacobian(numEndEffectors*3, numJoints)
		, jacobianT(numJoints, numEndEffectors*3)
		, err(numEndEffectors*3, 1)
		, JxJT(numEndEffectors*3, numEndEffectors*3)
		, JxJT_damped_s(numEndEffectors*3, numEndEffectors*3 + 1)
		, F(numEndEffectors*3, 1)
		, deltaAnglesM(numJoints, 1)
	{}
	ScalarMatrix jacobian;
	ScalarMatrix jacobianT;
	ScalarMatrix err;
	ScalarMatrix JxJT;
	ScalarMatrix JxJT_damped_s;
	ScalarMatrix F;
	ScalarMatrix deltaAnglesM;
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool JacobianDampedLeastSquares(const JointSet* pJoints,
								const JacobianMap* pJacobianMap,
								const IkGoal* goals,
								const float errTolerance,
								JacobianDampedLeastSquaresMatrixSet& matrices,
								float* deltaAngles,
								float* outSqrErr);

/// --------------------------------------------------------------------------------------------------------------- ///
bool JacobianDampedLeastSquares(const JointSet* pJoints,
								const JacobianMap* pJacobianMap,
								const IkGoal* goals,
								const float errTolerance,
								const float dampingConstant,
								const float maxErr,
								float* deltaAngles,
								float* outSqrErr);

/// --------------------------------------------------------------------------------------------------------------- ///
const char* JacobianMap::AxisToString(JacobianMap::Axis type)
{
	switch (type)
	{
	case kAxisX: return "x";
	case kAxisY: return "y";
	case kAxisZ: return "z";
	}

	return "???";
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* JacobianMap::DofTypeToString(JacobianMap::DofType type)
{
	switch (type)
	{
	case kJointTypeRot: return "rot";
	case kJointTypeTrans: return "trans";
	}

	return "???";
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkGoal::SetGoalPosition(Point_arg pos, bool validate)
{
	// we don't support change goal type from position to others, etc
	ANIM_ASSERT(!validate || m_type == kPosition || m_type == kGoalNone);

	m_type = kPosition;
	m_goal = pos.GetVec4();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkGoal::SetGoalPlane(Plane plane)
{
	m_type = kPlane;
	m_goal = plane.GetVec4();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkGoal::SetGoalPlaneSide(Plane plane)
{
	m_type = kPlaneSide;
	m_goal = plane.GetVec4();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkGoal::SetGoalRotation(Quat_arg rot, bool validate)
{
	// we don't support change goal type from rotation to others, etc
	ANIM_ASSERT(!validate || m_type == kRotation || m_type == kGoalNone);

	m_type = kRotation;
	m_goal = rot.GetVec4();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkGoal::SetGoalLookTarget(Point_arg targetPos, LookTargetAxis lookAxis, Vector_arg upVec)
{
	m_type = kLookTarget;
	m_goal = targetPos.GetVec4();
	m_goalDataIntA = lookAxis;
	m_goalDataVec4A = upVec.GetVec4();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkGoal::SetGoalLookTargetWithY(Point_arg targetPos, LookTargetAxis lookAxis, Vector_arg upVec)
{
	m_type = kLookTargetWithY;
	m_goal = targetPos.GetVec4();
	m_goalDataIntA = lookAxis;
	m_goalDataVec4A = upVec.GetVec4();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkGoal::SetGoalLookDir(Vector_arg targetDir, Vector_arg lookAxisLS)
{
	m_type = kLookDir;
	m_goal = SafeNormalize(targetDir, kUnitZAxis).GetVec4();
	m_goalDataVec4A = SafeNormalize(lookAxisLS, kUnitZAxis).GetVec4();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkGoal::SetGoalLine(Point_arg targetPos, Vector_arg lineDir)
{
	ANIM_ASSERT(LengthSqr(lineDir) > 0.0f);

	m_type = kLine;
	m_goal = targetPos.GetVec4();
	m_goalDataVec4A = Normalize(lineDir).GetVec4();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point IkGoal::GetGoalPosition() const
{
	ANIM_ASSERT(m_type == kPosition);
	return Point(m_goal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Plane IkGoal::GetGoalPlane() const
{
	ANIM_ASSERT(m_type == kPlane);
	return Plane(m_goal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Plane IkGoal::GetGoalPlaneSide() const
{
	ANIM_ASSERT(m_type == kPlaneSide);
	return Plane(m_goal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkGoal::GetGoalLine(Point* pTargetPos, Vector* pTargetLine) const
{
	ANIM_ASSERT(m_type == kLine);
	*pTargetPos = Point(m_goal);
	*pTargetLine = Vector(m_goalDataVec4A);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SMath::Quat IkGoal::GetGoalRotation() const
{
	ANIM_ASSERT(m_type == kRotation);
	return Quat(m_goal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkGoal::GetGoalLookTarget(Point* targetPos, LookTargetAxis* lookAxis, Vector* upVec) const
{
	ANIM_ASSERT(m_type == kLookTarget || m_type == kLookTargetWithY);
	*targetPos = Point(m_goal);
	*lookAxis = (LookTargetAxis)m_goalDataIntA;
	*upVec = Vector(m_goalDataVec4A);
}

bool g_validateDerivatives = false;

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector IkGoal::ComputeJacobian(short jointOffset,
									 Vector_arg axis,
									 const char dofType,
									 const Locator& jointLocWs,
									 const Locator& endEffectorLoc,
									 const float rotationGoalFactor) const
{
	Vector entry = kZero;

	switch (m_type)
	{
	case IkGoal::kPosition:
	case IkGoal::kPlane:
	case IkGoal::kPlaneSide:
	case IkGoal::kLine:
		{
			if (dofType == JacobianMap::kJointTypeTrans)
			{
				entry = axis;
			}
			else
			{
				Point p = jointLocWs.Pos();
				entry = Cross(axis, endEffectorLoc.GetTranslation() - p);
			}
			break;
		}

	case IkGoal::kRotation:
	case IkGoal::kLookTarget:
	case IkGoal::kLookTargetWithY:
	case IkGoal::kLookDir:
		{
			if (dofType == JacobianMap::kJointTypeRot)
			{
				entry = rotationGoalFactor*axis;
			}
			break;
		}
	}

	if (dofType == JacobianMap::kJointTypeRot && (m_type == IkGoal::kLookTarget || m_type == IkGoal::kLookTargetWithY))
	{
			float dTheta = DEGREES_TO_RADIANS(0.1f);
			Quat q1 = QuatFromAxisAngle(axis, dTheta / 2);
			Quat q0 = QuatFromAxisAngle(axis, -dTheta / 2);
			Locator effJs = jointLocWs.UntransformLocator(endEffectorLoc);
			Locator e1 = Locator(jointLocWs.GetTranslation(), q1*jointLocWs.GetRotation()).TransformLocator(effJs);
			Locator e0 = Locator(jointLocWs.GetTranslation(), q0*jointLocWs.GetRotation()).TransformLocator(effJs);

			Vector err1 = ComputeError(e1, 10000.0f);
			Vector err0 = ComputeError(e0, 10000.0f);

			Vector dError = -(err1 - err0) / dTheta;

			entry = dError *rotationGoalFactor;
	}

	return entry;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector IkGoal::ComputeError(const Locator& endEffectorLocator, const float maxErrDist) const
{
	Vector errVec = kZero;

	switch (m_type)
	{
	case IkGoal::kPosition:
		{
			Point goalPos = GetGoalPosition();
			GAMEPLAY_ASSERT(IsFinite(goalPos));
			Point endEffectorPos = endEffectorLocator.Pos();
			GAMEPLAY_ASSERT(IsFinite(endEffectorPos));

			errVec = goalPos - endEffectorPos;
			float lenSq = LengthSqr(errVec);
			if (lenSq > Sqr(maxErrDist))
				errVec *= maxErrDist/Sqrt(lenSq);
			break;
		}
	case IkGoal::kPlane:
		{
			Plane goalPlane = GetGoalPlane();
			GAMEPLAY_ASSERT(IsFinite(goalPlane.GetVec4()));
			Point endEffectorPos = endEffectorLocator.Pos();
			GAMEPLAY_ASSERT(IsFinite(endEffectorPos));

			Point closestPointOnPlane = goalPlane.ProjectPoint(endEffectorPos);
			errVec = closestPointOnPlane - endEffectorPos;
			float lenSq = LengthSqr(errVec);
			if (lenSq > Sqr(maxErrDist))
				errVec *= maxErrDist/Sqrt(lenSq);
			break;
		}
	case IkGoal::kPlaneSide:
		{
			Plane goalPlane = GetGoalPlaneSide();
			GAMEPLAY_ASSERT(IsFinite(goalPlane.GetVec4()));
			Point endEffectorPos = endEffectorLocator.Pos();
			GAMEPLAY_ASSERT(IsFinite(endEffectorPos));

			Scalar dist = goalPlane.Dist(endEffectorPos);
			if (dist < 0.0f)
			{
				Point closestPointOnPlane = goalPlane.ProjectPoint(endEffectorPos);
				errVec = closestPointOnPlane - endEffectorPos;
				float lenSq = LengthSqr(errVec);
				if (lenSq > Sqr(maxErrDist))
					errVec *= maxErrDist/Sqrt(lenSq);
			}
			break;
		}
	case IkGoal::kLine:
		{
			Point goalPt;
			Vector goalLine;
			GetGoalLine(&goalPt, &goalLine);
			GAMEPLAY_ASSERT(IsFinite(goalPt));
			GAMEPLAY_ASSERT(IsFinite(goalLine));

			Point endEffectorPos = endEffectorLocator.Pos();
			GAMEPLAY_ASSERT(IsFinite(endEffectorPos));

			Point goalPt2 = goalPt + goalLine;
			Scalar goalPtTt = IndexOfPointOnEdge(goalPt, goalPt2, endEffectorPos);
			GAMEPLAY_ASSERT(IsFinite(goalPtTt));

			Point closestPtOnLine = Lerp(goalPt, goalPt2, goalPtTt);
			errVec = closestPtOnLine - endEffectorPos;
			float lenSq = LengthSqr(errVec);
			if (lenSq > Sqr(maxErrDist))
				errVec *= maxErrDist / Sqrt(lenSq);
			break;
		}
	case IkGoal::kRotation:
		{
			Quat goalRot = GetGoalRotation();
			Quat endEffectorRot = endEffectorLocator.Rot();
			GAMEPLAY_ASSERT(IsFinite(goalRot));
			GAMEPLAY_ASSERT(IsFinite(endEffectorRot));

			Quat diffRot = goalRot * Conjugate(endEffectorRot);

			Vec4 axis;
			float angle;
			diffRot.GetAxisAndAngle(axis, angle);

			if (angle > PI)
			{
				angle -= PI_TIMES_2;
			}

			angle = (angle > 0.05f) ? 0.05f : angle;
			angle = (angle < -0.05f) ? -0.05f : angle;

			errVec = angle*Vector(axis);

			break;
		}
	case IkGoal::kLookTarget:
		{
			Point targetPos;
			IkGoal::LookTargetAxis lookAxis;
			Vector upVec;

			GetGoalLookTarget(&targetPos, &lookAxis, &upVec);

			Vector endEffectorDir = kZero;
			switch (lookAxis)
			{
			case IkGoal::kLookTargetAxisY:		endEffectorDir =  GetLocalY(endEffectorLocator.Rot()); break;
			case IkGoal::kLookTargetAxisYNeg:	endEffectorDir = -GetLocalY(endEffectorLocator.Rot()); break;
			case IkGoal::kLookTargetAxisZ:		endEffectorDir =  GetLocalZ(endEffectorLocator.Rot()); break;
			case IkGoal::kLookTargetAxisZNeg:	endEffectorDir = -GetLocalZ(endEffectorLocator.Rot()); break;
			}

			GAMEPLAY_ASSERT(IsFinite(targetPos));
			GAMEPLAY_ASSERT(IsFinite(endEffectorLocator));
			GAMEPLAY_ASSERT(IsFinite(endEffectorDir));

			Vector lookDir = SafeNormalize(targetPos - endEffectorLocator.Pos(), kZero);

			Quat diffRot = RotationBetween(endEffectorDir.GetVec4(), lookDir.GetVec4());

			Vec4 axis;
			float angle;
			diffRot.GetAxisAndAngle(axis, angle);

			if (angle > PI)
			{
				angle -= PI_TIMES_2;
			}

			angle = (angle > Abs(maxErrDist)) ? Abs(maxErrDist) : angle;
			angle = (angle < -Abs(maxErrDist)) ? -Abs(maxErrDist) : angle;

			errVec = angle*Vector(axis);

			break;

		}
	case IkGoal::kLookTargetWithY:
	{
		Point targetPos;
		IkGoal::LookTargetAxis lookAxis;
		Vector upVec;

		GetGoalLookTarget(&targetPos, &lookAxis, &upVec);

		Vector endEffectorDir = kZero;
		GAMEPLAY_ASSERT(lookAxis == kLookTargetAxisZ);
		endEffectorDir = GetLocalZ(endEffectorLocator.Rot());
		Vector rVec = Cross(endEffectorDir, upVec);
		Vector finalUpVec = SafeNormalize(Cross(rVec, endEffectorDir), upVec);

		Vector endEffectorUp = GetLocalY(endEffectorLocator.Rot());
		
		GAMEPLAY_ASSERT(IsFinite(targetPos));
		GAMEPLAY_ASSERT(IsFinite(endEffectorLocator));
		GAMEPLAY_ASSERT(IsFinite(endEffectorDir));

		Vector lookDir = SafeNormalize(targetPos - endEffectorLocator.Pos(), kZero);

		Quat diffRot = RotationBetween(endEffectorDir.GetVec4(), lookDir.GetVec4());

		Vec4 axis;
		float angle;
		diffRot.GetAxisAndAngle(axis, angle);

		if (angle > PI)
		{
			angle -= PI_TIMES_2;
		}

		angle = (angle > Abs(maxErrDist)) ? Abs(maxErrDist) : angle;
		angle = (angle < -Abs(maxErrDist)) ? -Abs(maxErrDist) : angle;

		errVec = angle * Vector(axis);

		Quat diffRotY = RotationBetween(endEffectorUp.GetVec4(), finalUpVec.GetVec4());

		diffRotY.GetAxisAndAngle(axis, angle);

		if (angle > PI)
		{
			angle -= PI_TIMES_2;
		}

		angle = (angle > Abs(maxErrDist)) ? Abs(maxErrDist) : angle;
		angle = (angle < -Abs(maxErrDist)) ? -Abs(maxErrDist) : angle;

		errVec += angle * Vector(axis);

		break;

	}

	case IkGoal::kLookDir:
		{
			const Vector lookDirLS(m_goalDataVec4A);
			const Vector goalDir(m_goal);

			const Vector lookDir(endEffectorLocator.TransformVector(lookDirLS));

			Quat diffRot = QuatFromVectors(lookDir, goalDir);

			Vec4 axis;
			float angle;
			diffRot.GetAxisAndAngle(axis, angle);

			if (angle > PI)
			{
				angle -= PI_TIMES_2;
			}

			angle = (angle > 0.05f) ? 0.05f : angle;
			angle = (angle < -0.05f) ? -0.05f : angle;

			errVec = angle*Vector(axis);

			break;
		}

	}

	return errVec * GetStrength();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianDebugInfo::Init(const JacobianMap* pJacobian)
{
	ANIM_ASSERT(pJacobian);

	// setup unique joints.
	m_numUniqueJoints = pJacobian->m_numUniqueJoints;
	m_preJointIkOffset = NDI_NEW Locator[m_numUniqueJoints];
	m_postJointIkOffset = NDI_NEW Locator[m_numUniqueJoints];
	m_postJointLocWs = NDI_NEW Locator[m_numUniqueJoints];
	m_uniqueJointNames = NDI_NEW StringId64[m_numUniqueJoints];

	for (U32F ii = 0; ii < m_numUniqueJoints; ii++)
	{
		m_uniqueJointNames[ii] = pJacobian->m_uniqueJoints[ii].m_jointName;
	}

	// setup joints.
	m_numJoints = pJacobian->m_numJoints;
	m_finalDeltaAngles = NDI_NEW float[m_numJoints];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianDebugInfo::Clear()
{
	memset(m_preJointIkOffset, 0, sizeof(Locator) * m_numUniqueJoints);
	memset(m_postJointIkOffset, 0, sizeof(Locator) * m_numUniqueJoints);
	memset(m_postJointLocWs, 0, sizeof(Locator) * m_numUniqueJoints);
	memset(m_finalDeltaAngles, 0, sizeof(float) * m_numJoints);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianDebugInfo::CopyFrom(const JacobianDebugInfo& other)
{
	ANIM_ASSERT(m_numUniqueJoints == other.m_numUniqueJoints);
	ANIM_ASSERT(m_numJoints == other.m_numJoints);

	memcpy(m_preJointIkOffset, other.m_preJointIkOffset, sizeof(Locator) * m_numUniqueJoints);
	memcpy(m_postJointIkOffset, other.m_postJointIkOffset, sizeof(Locator) * m_numUniqueJoints);
	memcpy(m_postJointLocWs, other.m_postJointLocWs, sizeof(Locator) * m_numUniqueJoints);
	memcpy(m_finalDeltaAngles, other.m_finalDeltaAngles, sizeof(float) * m_numJoints);

	m_time = other.m_time;
	m_numIterationsUsed = other.m_numIterationsUsed;
	m_maxIterations = other.m_maxIterations;
	m_errSqr = other.m_errSqr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IterateSolver(JacobianSolverContext& context, JacobianIkResult& result)
{
	PROFILE(Animation, IterateSolver);
	JacobianIkInstance * ik = context.m_pIk;
	float* deltaAngles		= context.m_paDeltaAngles;
	float* deltaAngleSum = context.m_paDeltaAngleSum;

	// move UpdateAllJointLocsWs() here, so that JacobianDampedLeastSquares() can be a constant func. help to debug.
	{
		PROFILE(Animation, SolveIK_UpdateAllJointLocs);
		ik->m_pJoints->UpdateAllJointLocsWs(ik->m_pJacobianMap->m_rootJointOffset);
	}

	if (!JacobianDampedLeastSquares(ik->m_pJoints,
									ik->m_pJacobianMap,
									ik->m_goal,
									ik->m_errTolerance,
									ik->m_solverDampingFactor,
									ik->m_maxError,
									deltaAngles,
									&result.m_sqrErr))
	{
		// successfully solved.
		result.m_solved = true;
		return false;
	}

	{
		PROFILE(Animation, SolveIK_ApplyResult);
		Quat rot = kIdentity;
		Vector trans(kZero);
		const float maxDeltaAngleRad = context.m_maxDeltaAngleRad;
		for (int j = 0; j < ik->m_pJacobianMap->m_numJoints; j++)
		{
			const JacobianMap::JacobianJointEntry *pJointEntry = &ik->m_pJacobianMap->m_jointEntries[j];

			if (pJointEntry->m_dofType == JacobianMap::kJointTypeRot)
			{
				int jointOffset = pJointEntry->m_jointOffset;
				float halfAngle = 0.5f * deltaAngles[j];

				float totalAngleDelta = halfAngle + deltaAngleSum[j];
				// if we use max-delta-angle, we need clamp half angle so that the sum is no more than max-delta-angle
				if (maxDeltaAngleRad > 0.0f && abs(totalAngleDelta) > maxDeltaAngleRad)
				{
					if (totalAngleDelta > 0.0f)
					{
						halfAngle = Max(maxDeltaAngleRad - deltaAngleSum[j], 0.0f);
					}
					else if (totalAngleDelta < 0.0f)
					{
						halfAngle = Min(-maxDeltaAngleRad - deltaAngleSum[j], 0.0f);
					}
				}

				// delta is 0, we don't need change at all!
				if (abs(halfAngle) <= 0.0f)
					continue;

				Scalar sinHalfAngle, cosHalfAngle;
				SinCos(halfAngle, sinHalfAngle, cosHalfAngle);
				switch (pJointEntry->m_axis)
				{
				case JacobianMap::kAxisX:
					rot.SetX(sinHalfAngle);
					rot.SetY(kZero);
					rot.SetZ(kZero);
					rot.SetW(cosHalfAngle);
					break;
				case JacobianMap::kAxisY:
					rot.SetX(kZero);
					rot.SetY(sinHalfAngle);
					rot.SetZ(kZero);
					rot.SetW(cosHalfAngle);
					break;
				case JacobianMap::kAxisZ:
					rot.SetX(kZero);
					rot.SetY(kZero);
					rot.SetZ(sinHalfAngle);
					rot.SetW(cosHalfAngle);
					break;

				default:
					rot = kIdentity;
				}

				deltaAngleSum[j] += halfAngle;

				if (context.m_pDebugInfo != nullptr)
					context.m_pDebugInfo->AddFinalDeltaAngle(j, halfAngle);

				ik->m_pJoints->PostRotateJointLs(jointOffset, rot, false);
			}
			else
			{
				int jointOffset = pJointEntry->m_jointOffset;
				float delta = deltaAngles[j];

				switch (pJointEntry->m_axis)
				{
				case JacobianMap::kAxisX:
					trans = Vector(delta, 0.0f, 0.0f);
					break;
				case JacobianMap::kAxisY:
					trans = Vector(0.0f, delta, 0.0f);
					break;
				case JacobianMap::kAxisZ:
					trans = Vector(0.0f, 0.0f, delta);
					break;

				default:
					trans = kZero;
				}

				if (context.m_pDebugInfo != nullptr)
					context.m_pDebugInfo->AddFinalDeltaAngle(j, delta);

				deltaAngleSum[j] += delta;

				ik->m_pJoints->TranslateJointLs(jointOffset, trans, false);
			}
		}

		ik->m_pJoints->InvalidateChildren(ik->m_pJacobianMap->m_rootJointOffset);
	}


	if (!ik->m_disableJointLimits)
	{
		PROFILE(Animation, SolveIK_ApplyLimitsAndConstraints);
		if (ik->m_pJointLimits)
			ik->m_pJointLimits->ApplyJointLimits(ik->m_pJoints, &ik->m_pJacobianMap->m_jointSetMap);
		else if (ik->m_pConstraints)
			ik->m_pJoints->ApplyConstraints(ik->m_pJacobianMap, ik->m_debugDrawJointLimits);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JacobianSolverContext BeginSolve(JacobianIkInstance* ik, JacobianDebugInfo* debugInfo)
{
	PROFILE(Animation, BeginSolve);
	JointSet *pJoints = ik->m_pJoints;

	if (ik->m_updateAllJointLocs)
		pJoints->UpdateAllJointLocsWs();

	Locator *startLocs = NDI_NEW Locator[pJoints->GetNumJoints()];
	float *deltaAngles = NDI_NEW float[ik->m_pJacobianMap->m_numJoints];

	// record the joint delta sum.
	float* deltaAngleSum = NDI_NEW float[ik->m_pJacobianMap->m_numJoints];
	memset(deltaAngleSum, 0, sizeof(float) * ik->m_pJacobianMap->m_numJoints);

	{
		PROFILE(Animation, SolveJacobianIK_Restore);

		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
		float *ikRestorePcts = NDI_NEW float[pJoints->GetNumJoints()];

		for (int i = 0; i < pJoints->GetNumJoints(); i++)
		{
			ikRestorePcts[i] = ik->m_restoreFactor;
		}

		if (ik->m_pConstraints)
		{
			const DC::IkConstraintInfo *pDcConstraints = ik->m_pConstraints;
			// TODO: Precompute the values to avoid calling FindJointOffset a bunch of times each frame
			for (int iConstraint = 0; iConstraint < pDcConstraints->m_count; iConstraint++)
			{
				const DC::IkConstraint& constraint = pDcConstraints->m_array[iConstraint];

				int jointOffset = pJoints->FindJointOffset(constraint.m_jointName);
				if (jointOffset >= 0)
				{
					ikRestorePcts[jointOffset] = Limit01(constraint.m_ikRestorePct*ikRestorePcts[jointOffset]);
				}
			}
		}

		if (debugInfo != nullptr)
		{
			debugInfo->Clear();
		}

		{

			for (int i = 0; i < ik->m_pJacobianMap->m_numUniqueJoints; i++)
			{
				int jointOffset = ik->m_pJacobianMap->m_uniqueJoints[i].m_jointOffset;
				startLocs[jointOffset] = pJoints->GetJointLocLs(jointOffset);

				Locator identity = Locator(kIdentity);
				if (ik->m_pJacobianMap->m_uniqueJoints)
				{
					Locator diffLoc;
					Lerp(&diffLoc, identity, ik->m_pJacobianMap->m_uniqueJoints[i].m_ikOffset, ikRestorePcts[jointOffset]);

					diffLoc.SetRot(Normalize(diffLoc.Rot()));

					// fill debug info.
					if (debugInfo != nullptr)
					{
						debugInfo->SetPreJointIkOffset(i, ik->m_pJacobianMap->m_uniqueJoints[i].m_ikOffset);
					}

					pJoints->TransformJointLs(jointOffset, diffLoc);
				}
			}
		}
	}


	float sqrErr = 0.0f;
	const float maxDeltaAngleRad = DEGREES_TO_RADIANS(ik->m_maxDeltaAngleDeg);

	JacobianSolverContext context;
	context.m_pIk = ik;
	context.m_pDebugInfo = debugInfo;
	context.m_paDeltaAngles = deltaAngles;
	context.m_paDeltaAngleSum = deltaAngleSum;
	context.m_maxDeltaAngleRad = maxDeltaAngleRad;
	context.m_paStartLocs = startLocs;

	return context;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FinishSolve(JacobianSolverContext& context)
{
	PROFILE(Animation, FinishSolve);
	JacobianIkInstance * ik = context.m_pIk;
	JacobianDebugInfo* debugInfo = context.m_pDebugInfo;
	ik->m_pJoints->UpdateAllJointLocsWs(ik->m_pJacobianMap->m_rootJointOffset);

	if (ik->m_pJacobianMap->m_uniqueJoints)
	{
		for (int i = 0; i < ik->m_pJacobianMap->m_numUniqueJoints; i++)
		{
			int jointOffset = ik->m_pJacobianMap->m_uniqueJoints[i].m_jointOffset;

			Locator prevLoc = context.m_paStartLocs[jointOffset];
			Locator currLoc = ik->m_pJoints->GetJointLocLs(jointOffset);
			Locator diffLoc;

			diffLoc = prevLoc.UntransformLocator(currLoc);
			//Locator testLoc = prevLoc.TransformLocator(diffLoc);

			ik->m_pJacobianMap->m_uniqueJoints[i].m_ikOffset = Lerp(Locator(kIdentity), diffLoc, ik->m_blend);

			// fill debug info.
			if (debugInfo != nullptr)
			{
				debugInfo->SetPostJointIkOffset(i, ik->m_pJacobianMap->m_uniqueJoints[i].m_ikOffset);
				debugInfo->SetPostJointLocWs(i, ik->m_pJoints->GetRawJointLocWs(jointOffset));
			}
		}
	}

	if (ik->m_blend < 1.0f)
	{
		for (int i = 0; i < ik->m_pJacobianMap->m_numUniqueJoints; i++)
		{
			int jointOffset = ik->m_pJacobianMap->m_uniqueJoints[i].m_jointOffset;
			Locator blendedLoc = Lerp(context.m_paStartLocs[jointOffset], ik->m_pJoints->GetJointLocLs(jointOffset), ik->m_blend);
			ik->m_pJoints->SetJointLocLs(jointOffset, blendedLoc, false);
		}

		ik->m_pJoints->InvalidateChildren(ik->m_pJacobianMap->m_rootJointOffset);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
JacobianIkResult SolveJacobianIK(JacobianIkInstance* ik, JacobianDebugInfo* debugInfo)
{
	PROFILE(Animation, SolveJacobianIK);

	JacobianIkResult result;

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_ikOptions.m_disableAllIk && !ik->m_forceEnable))
		return result;

	if (!ik->m_pJacobianMap->m_valid)
	{
		return result;
	}

	ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);

	JacobianSolverContext context = BeginSolve(ik, debugInfo);

	int iteration = 0;
	for (; iteration < ik->m_maxIterations; iteration++)
	{
		if (!IterateSolver(context, result))
		{
			break;
		}
	}

	result.m_iteration = iteration;

	if (debugInfo != nullptr)
	{
		debugInfo->m_maxIterations = ik->m_maxIterations;
		debugInfo->m_numIterationsUsed = iteration;
		debugInfo->m_errSqr = result.m_sqrErr;
	}

	FinishSolve(context);


	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JacobianDampedLeastSquaresOrig(const JointSet* pJoints,
									const JacobianMap* pJacobianMap,
									const IkGoal* goals,
									const float errTolerance,
									JacobianDampedLeastSquaresMatrixSet& matrices,
									float* deltaAngles,
									float* outSqrErr)
{
	PROFILE(Animation, SolveIK_JacobianDampedLeastSquares);

	ANIM_ASSERT(pJoints != nullptr);
	ANIM_ASSERT(pJacobianMap != nullptr);

	static float kMaxErrDist = 0.05f;
	float kMaxErrDistSq = kMaxErrDist*kMaxErrDist;

	static float kDampingConstant = 0.5f;
	float kDampingConstantSq = kDampingConstant*kDampingConstant;

	int numEnabledEndEffectors = 0;
	// get enabled end-effectors.
	{
		PROFILE(Animation,JacobianDampedLeastSquares_CountEnabledEff);
		for (U32F iEndEffector = 0; iEndEffector < pJacobianMap->m_numEndEffectors; iEndEffector++)
		{
			const JacobianEndEffectorEntry& eachEntry = pJacobianMap->m_endEffectorEntries[iEndEffector];
			numEnabledEndEffectors += eachEntry.endEffectEnabled ? 1 : 0;
		}
	}

	SYSTEM_ASSERTF(numEnabledEndEffectors > 0, ("We don't support zero number end-effectors yet, do we really need that?"));

	pJacobianMap->CreateJacobianMatrix(goals, pJoints, matrices.jacobian);

	const ScalarMatrix &jacobian = matrices.jacobian;
	ScalarMatrix& jacobianT = matrices.jacobianT;
	jacobian.Transpose(&jacobianT);

	ScalarMatrix& err = matrices.err;
	Scalar sqrErr = 0.0f;


	I32 count = 0;
	{
		PROFILE(Animation, SolveIK_JacobianDampedLeastSquares_ComputeError);

		for (int iEndEffector = 0; iEndEffector < pJacobianMap->m_numEndEffectors; iEndEffector++)
		{
			const JacobianEndEffectorEntry& eachEntry = pJacobianMap->m_endEffectorEntries[iEndEffector];
			// skip those disabled end-effector.
			if (!eachEntry.endEffectEnabled)
				continue;

			Vector errVec = kZero;
			ANIM_ASSERT(eachEntry.endEffectorNameId == pJoints->GetJointId(eachEntry.endEffectorOffset));
			const Locator endEffectorLoc = pJoints->GetRawJointLocWs(eachEntry.endEffectorOffset).TransformLocator(eachEntry.endEffectJointOffset);
			ANIM_ASSERT(iEndEffector < JacobianIkInstance::kMaxNumGoals);
			const IkGoal& goal = goals[iEndEffector];

			if ((goal.GetGoalType() == IkGoal::kRotation) || (goal.GetGoalType() == IkGoal::kLookDir))
			{
				SYSTEM_ASSERTF(eachEntry.endEffectorGoalType == IkGoal::kRotation
								   || eachEntry.endEffectorGoalType == IkGoal::kLookDir,
							   ("the order of goals and the order of end-effectors must match! goal:%d, end-effector:%d",
								goal.GetGoalType(),
								eachEntry.endEffectorGoalType));
			}
			else
			{
				SYSTEM_ASSERTF(eachEntry.endEffectorGoalType == goal.GetGoalType(),
							   ("the order of goals and the order of end-effectors must match! goal:%d, end-effector:%d",
								goal.GetGoalType(),
								eachEntry.endEffectorGoalType));
			}

			errVec = goal.ComputeError(endEffectorLoc, kMaxErrDist);

			err.Set(3 * count + 0, 0, errVec.X());
			err.Set(3 * count + 1, 0, errVec.Y());
			err.Set(3 * count + 2, 0, errVec.Z());
			sqrErr += LengthSqr(errVec);

			count++;
		}
	}
	ANIM_ASSERT(count == numEnabledEndEffectors);

	//We are done
	if (sqrErr < Sqr(errTolerance))
	{
		return false;
	}

	// we may need to know how big error is.
	*outSqrErr = sqrErr;

	ScalarMatrix& JxJT = matrices.JxJT;
	MatrixMult(&JxJT, jacobian, jacobianT);

	ScalarMatrix& JxJT_damped = JxJT;
	JxJT_damped.AddToDiagonal(kDampingConstantSq);

	ScalarMatrix& JxJT_damped_s = matrices.JxJT_damped_s;
	{
		PROFILE(Animation, CombineMatrices);

		for (int i=0; i<JxJT_damped.GetNumRows(); i++)
		{
			for (int j=0; j<JxJT_damped.GetNumCols(); j++)
				JxJT_damped_s.Set(i, j, JxJT_damped.Get(i, j));

			JxJT_damped_s.Set(i, JxJT_damped.GetNumCols(), err.Get(i, 0));
		}
	}

	JxJT_damped_s.Solve();
	ScalarMatrix& F = matrices.F;
	for (int i=0; i<3 * numEnabledEndEffectors; i++)
		F.Set(i, 0, JxJT_damped_s.Get(i, JxJT_damped_s.GetNumCols()-1));

	ScalarMatrix& deltaAnglesM = matrices.deltaAnglesM;
	MatrixMult(&deltaAnglesM, jacobianT, F);

	for (int i=0; i<jacobianT.GetNumRows(); i++)
		deltaAngles[i] = deltaAnglesM.Get(i, 0);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JacobianDampedLeastSquares(const JointSet* pJoints,
								const JacobianMap* pJacobianMap,
								const IkGoal* goals,
								const float errTolerance,
								const float dampingConstant,
								const float maxErr,
								float* deltaAngles,
								float* outSqrErr)
{
	PROFILE(Animation, SolveIK_JacobianDampedLeastSquares);

	ANIM_ASSERT(pJoints != nullptr);
	ANIM_ASSERT(pJacobianMap != nullptr);

	float kMaxErrDist = 0.05f;
	if (maxErr >= 0.0f)
	{
		kMaxErrDist = maxErr;
	}
	float kMaxErrDistSq = kMaxErrDist*kMaxErrDist;

	static float kDampingConstant = 0.5f;
	float kDampingConstantSq = Sqr(dampingConstant);// kDampingConstant*kDampingConstant;

	int numEnabledEndEffectors = 0;
	// get enabled end-effectors.
	for (U32F iEndEffector = 0; iEndEffector < pJacobianMap->m_numEndEffectors; iEndEffector++)
	{
		const JacobianEndEffectorEntry& eachEntry = pJacobianMap->m_endEffectorEntries[iEndEffector];
		numEnabledEndEffectors += eachEntry.endEffectEnabled ? 1 : 0;
	}

	SYSTEM_ASSERTF(numEnabledEndEffectors > 0, ("We don't support zero number end-effectors yet, do we really need that?"));

	int jRows = 0, jCols = 0;
	pJacobianMap->GetJacobianSize(jRows, jCols);
	EMatrix J(jRows, jCols);
	pJacobianMap->CreateJacobianMatrix(goals, pJoints, J);

	EVector err(J.rows(), 1);
	Scalar sqrErr = 0.0f;


	I32 count = 0;
	{
		PROFILE(Animation, SolveIK_JacobianDampedLeastSquares_ComputeError);

		for (int iEndEffector = 0; iEndEffector < pJacobianMap->m_numEndEffectors; iEndEffector++)
		{
			const JacobianEndEffectorEntry& eachEntry = pJacobianMap->m_endEffectorEntries[iEndEffector];
			// skip those disabled end-effector.
			if (!eachEntry.endEffectEnabled)
				continue;

			Vector errVec = kZero;
			ANIM_ASSERT(eachEntry.endEffectorNameId == pJoints->GetJointId(eachEntry.endEffectorOffset));
			const Locator endEffectorLoc = pJoints->GetRawJointLocWs(eachEntry.endEffectorOffset).TransformLocator(eachEntry.endEffectJointOffset);
			ANIM_ASSERT(iEndEffector < JacobianIkInstance::kMaxNumGoals);
			const IkGoal& goal = goals[iEndEffector];

			if (goal.GetGoalType() == IkGoal::kRotation || goal.GetGoalType() == IkGoal::kLookDir)
				SYSTEM_ASSERTF(eachEntry.endEffectorGoalType == IkGoal::kRotation || eachEntry.endEffectorGoalType == IkGoal::kLookDir, ("the order of goals and the order of end-effectors must match! goal:%d, end-effector:%d", goal.GetGoalType(), eachEntry.endEffectorGoalType));
			else
				SYSTEM_ASSERTF(eachEntry.endEffectorGoalType == goal.GetGoalType(), ("the order of goals and the order of end-effectors must match! goal:%d, end-effector:%d", goal.GetGoalType(), eachEntry.endEffectorGoalType));

			errVec = goal.ComputeError(endEffectorLoc, kMaxErrDist);

			err(3 * count + 0, 0) = errVec.X();
			err(3 * count + 1, 0) = errVec.Y();
			err(3 * count + 2, 0) = errVec.Z();

			sqrErr += LengthSqr(errVec);

			count++;
		}
	}
	ANIM_ASSERT(count == numEnabledEndEffectors);

	//We are done
	if (sqrErr < Sqr(errTolerance))
	{
		return false;
	}

	// we may need to know how big error is.
	*outSqrErr = sqrErr;


	{
		PROFILE(Animation, SolveIK_JacobianDampedLeastSquares_SolveEigen_solve);
	//	EVector F = (J*J.transpose() + kDampingConstantSq*EMatrix::Identity(J.rows(), J.rows())).llt().solve(err);
		//EVector deltaAnglesM = J.transpose()*F;

#if 0
		EMatrix JT = J.transpose();
		EMatrix JxJT;
		{
			PROFILE(Animation, SolveIK_JacobianDampedLeastSquares_JxJT_Mult);
			JxJT = J*JT;
		}
		EMatrix D = kDampingConstantSq*EMatrix::Identity(J.rows(), J.rows());
		EMatrix JxJT_D = JxJT + D;
		EMatrix JxJT_D_s;
		{
			PROFILE(Animation, SolveIK_JacobianDampedLeastSquares_Solve);
			JxJT_D_s = JxJT_D.llt().solve(err);
		}
		EMatrix deltaAnglesM = JT * JxJT_D_s;
#else
		EMatrix deltaAnglesM = J.transpose() * ((J*J.transpose()) + kDampingConstantSq*EMatrix::Identity(J.rows(), J.rows())).llt().solve(err);
#endif

		for (int i = 0; i < deltaAnglesM.rows(); i++)
		{
			deltaAngles[i] = deltaAnglesM(i, 0);
			ANIM_ASSERT(IsFinite(deltaAngles[i]));
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JacobianMap::JacobianMap()
{
	m_rootJointOffset	 = -1;
	m_jointEntries		 = nullptr;
	m_endEffectorEntries = nullptr;
	m_numJoints		  = 0;
	m_numEndEffectors = 0;
	m_uniqueJoints	  = nullptr;
	m_numUniqueJoints = 0;
	m_rotationGoalFactor = 0.1f;
	m_valid = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JacobianMap::~JacobianMap()
{
	Destroy();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::InitInternal(const JointSet* pJoints,
							   const DC::IkConstraintInfo* pConstraints,
							   StringId64 rootId,
							   int numEndEffectors,
							   const EndEffectorDef* pEndEffectorDefs)
{
	ANIM_ASSERT(numEndEffectors >= 1);

	int rootOffset = pJoints->FindJointOffset(rootId);

	if (rootOffset < 0)
	{
		m_valid = false;
		return;
	}

	m_rootJointOffset = rootOffset;

	m_numEndEffectors = numEndEffectors;
	m_endEffectorEntries = NDI_NEW JacobianEndEffectorEntry[m_numEndEffectors];

	for (int ii = 0; ii < m_numEndEffectors; ii++)
	{
		StringId64 endEffectorId = pEndEffectorDefs[ii].m_jointId;
		JacobianEndEffectorEntry& newEntry = m_endEffectorEntries[ii];
		newEntry.endEffectorNameId = pEndEffectorDefs[ii].m_jointId;
		newEntry.endEffectorOffset = pJoints->FindJointOffset(endEffectorId);
		newEntry.endEffectorGoalType = pEndEffectorDefs[ii].m_goalType;
		newEntry.endEffectJointOffset = pEndEffectorDefs[ii].m_jointOffset;
		newEntry.endEffectEnabled = pEndEffectorDefs[ii].m_enabled;
		ANIM_ASSERT(newEntry.endEffectorOffset >= 0);
		ANIM_ASSERT(newEntry.endEffectorNameId != INVALID_STRING_ID_64);
	}

	for (int i = 0; i < m_numEndEffectors; i++)
	{
		ANIM_ASSERT(pJoints->IsAncestor(rootOffset, m_endEffectorEntries[i].endEffectorOffset));
	}

	m_numJoints = 0;
	m_numUniqueJoints = 0;
	for (int i = rootOffset; i < pJoints->GetNumJoints(); i++)
	{
		const DC::IkConstraint *pConstraint = nullptr;
		const StringId64 jointNameId = pJoints->GetJointId(i);
		if (pConstraints)
		{
			for (int c = 0; c < pConstraints->m_count; c++)
			{
				if (jointNameId == pConstraints->m_array[c].m_jointName)
				{
					pConstraint = &pConstraints->m_array[c];
					break;
				}
			}
		}

		for (int j = 0; j < m_numEndEffectors; j++)
		{
			if (pJoints->IsAncestor(i, m_endEffectorEntries[j].endEffectorOffset))
			{
				bool includeThisJoint = false;
				if (!pConstraint || (pConstraint->m_jointDof & DC::kIkJointDofX))
				{
					m_numJoints++;
					includeThisJoint = true;
				}
				if (!pConstraint || (pConstraint->m_jointDof & DC::kIkJointDofY))
				{
					m_numJoints++;
					includeThisJoint = true;
				}
				if (!pConstraint || (pConstraint->m_jointDof & DC::kIkJointDofZ))
				{
					m_numJoints++;
					includeThisJoint = true;
				}

				if (pConstraint && (pConstraint->m_jointDof & DC::kIkJointDofXTrans))
				{
					m_numJoints++;
					includeThisJoint = true;
				}

				if (pConstraint && (pConstraint->m_jointDof & DC::kIkJointDofYTrans))
				{
					m_numJoints++;
					includeThisJoint = true;
				}

				if (pConstraint && (pConstraint->m_jointDof & DC::kIkJointDofZTrans))
				{
					m_numJoints++;
					includeThisJoint = true;
				}

				if (includeThisJoint)
					m_numUniqueJoints++;

				break;
			}
		}
	}

	ANIM_ASSERT(m_numJoints > 0);
	ANIM_ASSERT(m_numUniqueJoints > 0);
	m_jointEntries = NDI_NEW JacobianJointEntry[m_numJoints];
	m_uniqueJoints = NDI_NEW UniqueJoint[m_numUniqueJoints];

	ResetJointIkOffsets();

	m_jointSetMap.Init(m_numUniqueJoints);

	int currJoint = 0;
	int currUniqueJoint = 0;
	for (int jointOffset = rootOffset; jointOffset < pJoints->GetNumJoints(); jointOffset++)
	{
		const DC::IkConstraint *pConstraint = nullptr;
		const StringId64 jointNameId = pJoints->GetJointId(jointOffset);
		int constraintOffset = -1;
		if (pConstraints)
		{
			for (int iConstraintOffset = 0; iConstraintOffset < pConstraints->m_count; iConstraintOffset++)
			{
				if (jointNameId == pConstraints->m_array[iConstraintOffset].m_jointName)
				{
					pConstraint = &pConstraints->m_array[iConstraintOffset];
					constraintOffset = iConstraintOffset;

					break;
				}
			}
		}

		for (int j = 0; j < m_numEndEffectors; j++)
		{
			if (pJoints->IsAncestor(jointOffset, m_endEffectorEntries[j].endEffectorOffset))
			{
				bool includeThisJoint = false;
				if (!pConstraint || (pConstraint->m_jointDof & DC::kIkJointDofX))
				{
					includeThisJoint = true;
					JacobianJointEntry& newEntry = m_jointEntries[currJoint];
					newEntry.m_jointNameId = jointNameId;
					newEntry.m_jointOffset = jointOffset;
					newEntry.m_axis = kAxisX;
					newEntry.m_dofType = kJointTypeRot;
					newEntry.m_ikFactor = pConstraint ? pConstraint->m_ikFactor : 1.0f;
					currJoint++;
				}

				if (!pConstraint || (pConstraint->m_jointDof & DC::kIkJointDofY))
				{
					includeThisJoint = true;
					JacobianJointEntry& newEntry = m_jointEntries[currJoint];
					newEntry.m_jointNameId = jointNameId;
					newEntry.m_jointOffset = jointOffset;
					newEntry.m_axis = kAxisY;
					newEntry.m_dofType = kJointTypeRot;
					newEntry.m_ikFactor = pConstraint ? pConstraint->m_ikFactor : 1.0f;
					currJoint++;
				}

				if (!pConstraint || (pConstraint->m_jointDof & DC::kIkJointDofZ))
				{
					includeThisJoint = true;
					JacobianJointEntry& newEntry = m_jointEntries[currJoint];
					newEntry.m_jointNameId = jointNameId;
					newEntry.m_jointOffset = jointOffset;
					newEntry.m_axis = kAxisZ;
					newEntry.m_dofType = kJointTypeRot;
					newEntry.m_ikFactor = pConstraint ? pConstraint->m_ikFactor : 1.0f;
					currJoint++;
				}

				if (pConstraint && (pConstraint->m_jointDof & DC::kIkJointDofXTrans))
				{
					includeThisJoint = true;
					JacobianJointEntry& newEntry = m_jointEntries[currJoint];
					newEntry.m_jointNameId = jointNameId;
					newEntry.m_jointOffset = jointOffset;
					newEntry.m_axis = kAxisX;
					newEntry.m_dofType = kJointTypeTrans;
					newEntry.m_ikFactor = pConstraint->m_ikFactor;
					currJoint++;
				}

				if (pConstraint && (pConstraint->m_jointDof & DC::kIkJointDofYTrans))
				{
					includeThisJoint = true;
					JacobianJointEntry& newEntry = m_jointEntries[currJoint];
					newEntry.m_jointNameId = jointNameId;
					newEntry.m_jointOffset = jointOffset;
					newEntry.m_axis = kAxisY;
					newEntry.m_dofType = kJointTypeTrans;
					newEntry.m_ikFactor = pConstraint->m_ikFactor;
					currJoint++;
				}

				if (pConstraint && (pConstraint->m_jointDof & DC::kIkJointDofZTrans))
				{
					includeThisJoint = true;
					JacobianJointEntry& newEntry = m_jointEntries[currJoint];
					newEntry.m_jointNameId = jointNameId;
					newEntry.m_jointOffset = jointOffset;
					newEntry.m_axis = kAxisZ;
					newEntry.m_dofType = kJointTypeTrans;
					newEntry.m_ikFactor = pConstraint->m_ikFactor;
					currJoint++;
				}

				if (includeThisJoint)
				{
					UniqueJoint& newItem = m_uniqueJoints[currUniqueJoint];
					newItem.m_jointOffset = jointOffset;
					newItem.m_jointName = jointNameId;

					m_jointSetMap.m_jointSetOffset[currUniqueJoint] = jointOffset;
					m_jointSetMap.m_jointLimitIndex[currUniqueJoint] = constraintOffset;

					currUniqueJoint++;
				}

				break;
			}
		}
	}

	ANIM_ASSERT(currJoint == m_numJoints);
	ANIM_ASSERT(currUniqueJoint == m_numUniqueJoints);

	for (int j = 0; j < m_numJoints; j++)
	{
		m_jointEntries[j].m_endEffectorAncestor = 0;

		int jointOffset = m_jointEntries[j].m_jointOffset;
		for (int e = 0; e < m_numEndEffectors; e++)
		{
			if (pJoints->IsAncestor(jointOffset, m_endEffectorEntries[e].endEffectorOffset))
				m_jointEntries[j].m_endEffectorAncestor |= 1 << e;
		}
	}

	for (int j = 0; j < m_numUniqueJoints - 1; j++)
	{
		ANIM_ASSERT(m_uniqueJoints[j].m_jointOffset < m_uniqueJoints[j + 1].m_jointOffset);
	}

	// it's valid jacobian matrix.
	m_valid = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::Init(const JointSet* pJoints, StringId64 rootId, int numEndEffectors, const EndEffectorDef* pEndEffectorDefs)
{
	ANIM_ASSERT(pJoints);

	InitInternal(pJoints, pJoints->GetJointConstraints(), rootId, numEndEffectors, pEndEffectorDefs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::InitWithConstraints(JointSet* pJoints,
									  StringId64 rootId,
									  int numEndEffectors,
									  const EndEffectorDef* pEndEffectorDefs,
									  StringId64 constraintInfoId)
{
	ANIM_ASSERT(pJoints);
	ScriptPointer<DC::IkConstraintInfo> pContraintInfo(constraintInfoId, SID("ik-settings"));
	InitInternal(pJoints, pContraintInfo, rootId, numEndEffectors, pEndEffectorDefs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_jointEntries, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_endEffectorEntries, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_uniqueJoints, deltaPos, lowerBound, upperBound);
	m_jointSetMap.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::Destroy()
{
	if (m_jointEntries)
	{
		NDI_DELETE [] m_jointEntries;
		m_jointEntries = nullptr;
	}

	if (m_endEffectorEntries)
	{
		NDI_DELETE [] m_endEffectorEntries;
		m_endEffectorEntries = nullptr;
	}

	if (m_uniqueJoints)
	{
		NDI_DELETE [] m_uniqueJoints;
		m_uniqueJoints = nullptr;
	}

	m_jointSetMap.Destroy();

	m_numEndEffectors = 0;
	m_numJoints = 0;
	m_numUniqueJoints = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::CreateJacobianMatrix(const IkGoal* goals, const JointSet* pJoints, ScalarMatrix& outMatrix) const
{
	PROFILE(Animation, SolveIK_CreateJacobianMatrix);

	const U32 kNumDof = 3;

	int numEnabledEndEffectors = GetNumEnabledEndEffectors();


	SYSTEM_ASSERTF(numEnabledEndEffectors > 0, ("We don't support zero number end-effectors yet, do we really need that?"));

	int numRows = kNumDof * numEnabledEndEffectors;
	int numCols = m_numJoints;

	ANIM_ASSERT(numRows == outMatrix.GetNumRows());
	ANIM_ASSERT(numCols == outMatrix.GetNumCols());
	ScalarMatrix *jacobianMatrix = &outMatrix;

	int count = 0;
	for (int e = 0; e < m_numEndEffectors; e++)
	{
		const JacobianEndEffectorEntry& eachEntry = m_endEffectorEntries[e];

		// skip those disabled end-effectors.
		if (!eachEntry.endEffectEnabled)
			continue;

		const int endEffectorOffset = eachEntry.endEffectorOffset;
		const Locator endEffectorPosWS = pJoints->GetRawJointLocWs(endEffectorOffset);
		ANIM_ASSERT(eachEntry.endEffectorNameId == pJoints->GetJointId(endEffectorOffset));

		ANIM_ASSERT(e < JacobianIkInstance::kMaxNumGoals);
		const IkGoal& goal = goals[e];

		if (goal.GetGoalType() == IkGoal::kRotation || goal.GetGoalType() == IkGoal::kLookDir)
		{
			SYSTEM_ASSERTF(eachEntry.endEffectorGoalType == IkGoal::kRotation
							   || eachEntry.endEffectorGoalType == IkGoal::kLookDir,
						   ("the order of goals and the order of end-effectors must match! goal:%d, end-effector:%d",
							goal.GetGoalType(),
							eachEntry.endEffectorGoalType));
		}
		else
		{
			SYSTEM_ASSERTF(eachEntry.endEffectorGoalType == goal.GetGoalType(),
						   ("the order of goals and the order of end-effectors must match! goal:%d, end-effector:%d",
							goal.GetGoalType(),
							eachEntry.endEffectorGoalType));
		}

		U32 endEffectorBit = 1<<e;

		for (int j = 0; j < m_numJoints; j++)
		{
			Vector entry = kZero;

			const JacobianMap::JacobianJointEntry* pJointEntry = &m_jointEntries[j];

			bool isAncestor = (pJointEntry->m_endEffectorAncestor & endEffectorBit) != 0;
			if (isAncestor)
			{
				int jointOffset = pJointEntry->m_jointOffset;
				float ikFactor = pJointEntry->m_ikFactor;

				const Locator& jointLocWs = pJoints->GetRawJointLocWs(jointOffset);

				Vector v;
				switch (pJointEntry->m_axis)
				{
				case JacobianMap::kAxisX:
					v = GetLocalX(jointLocWs.Rot());
					break;
				case JacobianMap::kAxisY:
					v = GetLocalY(jointLocWs.Rot());
					break;
				case JacobianMap::kAxisZ:
					v = GetLocalZ(jointLocWs.Rot());
					break;
				default:
					v = kZero;
					break;
				}

				entry = goal.ComputeJacobian(jointOffset,
											 v,
											 pJointEntry->m_dofType,
											 jointLocWs,
											 endEffectorPosWS,
											 m_rotationGoalFactor);

				entry *= ikFactor;
			}

			int rowOffset = kNumDof * count;
			jacobianMatrix->Set(rowOffset, j, entry.X());
			jacobianMatrix->Set(rowOffset + 1, j, entry.Y());
			jacobianMatrix->Set(rowOffset + 2, j, entry.Z());
		}

		count++;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::GetJacobianSize(int& outRows, int& outCols) const
{
	const U32 kNumDof = 3;

	int numEnabledEndEffectors = 0;
	// get enabled end-effectors.
	ANIM_ASSERT(m_numEndEffectors <= JacobianIkInstance::kMaxNumGoals);
	for (U32F e = 0; e < m_numEndEffectors; e++)
	{
		const JacobianEndEffectorEntry& eachEntry = m_endEffectorEntries[e];
		numEnabledEndEffectors += eachEntry.endEffectEnabled ? 1 : 0;
	}

	SYSTEM_ASSERTF(numEnabledEndEffectors > 0, ("We don't support zero number end-effectors yet, do we really need that?"));

	outRows = kNumDof * numEnabledEndEffectors;
	outCols = m_numJoints;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::CreateJacobianMatrix(const IkGoal* goals, const JointSet* pJoints, EMatrix& outJacobian) const
{
	PROFILE(Animation, SolveIK_CreateJacobianMatrix);

	const U32 kNumDof = 3;

	int numEnabledEndEffectors = 0;
	// get enabled end-effectors.
	ANIM_ASSERT(m_numEndEffectors <= JacobianIkInstance::kMaxNumGoals);
	for (U32F e = 0; e < m_numEndEffectors; e++)
	{
		const JacobianEndEffectorEntry& eachEntry = m_endEffectorEntries[e];
		numEnabledEndEffectors += eachEntry.endEffectEnabled ? 1 : 0;
	}

	SYSTEM_ASSERTF(numEnabledEndEffectors > 0, ("We don't support zero number end-effectors yet, do we really need that?"));

	int numRows = kNumDof * numEnabledEndEffectors;
	int numCols = m_numJoints;

	int count = 0;
	for (int e = 0; e < m_numEndEffectors; e++)
	{
		const JacobianEndEffectorEntry& eachEntry = m_endEffectorEntries[e];

		// skip those disabled end-effectors.
		if (!eachEntry.endEffectEnabled)
			continue;

		const int endEffectorOffset = eachEntry.endEffectorOffset;
		const Locator endEffectorLocWS = pJoints->GetRawJointLocWs(endEffectorOffset).TransformLocator(eachEntry.endEffectJointOffset);
		const StringId64 jointId = pJoints->GetJointId(endEffectorOffset);
		ANIM_ASSERT(eachEntry.endEffectorNameId == pJoints->GetJointId(endEffectorOffset));

		ANIM_ASSERT(e < JacobianIkInstance::kMaxNumGoals);
		const IkGoal& goal = goals[e];
		if (goal.GetGoalType() == IkGoal::kRotation || goal.GetGoalType() == IkGoal::kLookDir)
			SYSTEM_ASSERTF(eachEntry.endEffectorGoalType == IkGoal::kRotation || eachEntry.endEffectorGoalType == IkGoal::kLookDir, ("the order of goals and the order of end-effectors must match! goal:%d, end-effector:%d", goal.GetGoalType(), eachEntry.endEffectorGoalType));
		else
			SYSTEM_ASSERTF(eachEntry.endEffectorGoalType == goal.GetGoalType(), ("the order of goals and the order of end-effectors must match! goal:%d, end-effector:%d", goal.GetGoalType(), eachEntry.endEffectorGoalType));

		U32 endEffectorBit = 1<<e;

		for (int j = 0; j < m_numJoints; j++)
		{
			Vector entry = kZero;

			const JacobianMap::JacobianJointEntry *pJointEntry = &m_jointEntries[j];

			bool isAncestor = (pJointEntry->m_endEffectorAncestor & endEffectorBit) != 0;
			if (isAncestor)
			{
				int jointOffset = pJointEntry->m_jointOffset;
				float ikFactor = pJointEntry->m_ikFactor;

				const Locator& jointLocWs = pJoints->GetRawJointLocWs(jointOffset);

				Vector v;
				switch (pJointEntry->m_axis)
				{
				case JacobianMap::kAxisX:
					v = GetLocalX(jointLocWs.Rot());
					break;
				case JacobianMap::kAxisY:
					v = GetLocalY(jointLocWs.Rot());
					break;
				case JacobianMap::kAxisZ:
					v = GetLocalZ(jointLocWs.Rot());
					break;
				default:
					v = kZero;
					break;
				}

				entry = goal.ComputeJacobian(jointOffset,
											 v,
											 pJointEntry->m_dofType,
											 jointLocWs,
											 endEffectorLocWS,
											 m_rotationGoalFactor);

				entry *= ikFactor;
			}

			int rowOffset = kNumDof * count;
			outJacobian(rowOffset, j)	  = (float)entry.X();
			outJacobian(rowOffset + 1, j) = (float)entry.Y();
			outJacobian(rowOffset + 2, j) = (float)entry.Z();
		}

		count++;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::ResetJointIkOffsets()
{
	if (m_uniqueJoints != nullptr)
	{
		for (int i=0; i<m_numUniqueJoints; i++)
			m_uniqueJoints[i].m_ikOffset = Locator(kIdentity);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::SetEndEffectorEnabled(StringId64 endEffectorNameId, IkGoal::GoalType goalType, bool value)
{
	for (U32F ii = 0; ii < m_numEndEffectors; ii++)
	{
		JacobianEndEffectorEntry& eachEntry = m_endEffectorEntries[ii];
		if (eachEntry.endEffectorNameId == endEffectorNameId && eachEntry.endEffectorGoalType == goalType)
		{
			eachEntry.endEffectEnabled = value;
			return;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::SetEndEffectorOffset(StringId64 endEffectorNameId, const Locator& offset)
{
	for (U32F ii = 0; ii < m_numEndEffectors; ii++)
	{
		JacobianEndEffectorEntry& eachEntry = m_endEffectorEntries[ii];
		if (eachEntry.endEffectorNameId == endEffectorNameId)
		{
			eachEntry.endEffectJointOffset = offset;
			return;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianMap::DumpToTTY()
{
	STRIP_IN_FINAL_BUILD;

	MsgPlayer("jacobian-ik: begin dumping jacobian ----------\n");

	// dump joint set map.
	MsgPlayer("jacobian-ik: [joint-set-map], [num-joints:%d], [joint-set-offset, joint-limit-index]\n", m_jointSetMap.m_numJoints);
	MsgPlayer("jacobian-ik: ");
	for (U32F iJoint = 0; iJoint < m_jointSetMap.m_numJoints; iJoint++)
	{
		MsgPlayer("[%d, %d], ", m_jointSetMap.m_jointSetOffset[iJoint], m_jointSetMap.m_jointLimitIndex[iJoint]);
	}
	MsgPlayer("\n");

	// dump jacobian-joint-entry
	MsgPlayer("jacobian-ik: [joint-entry], [num-joints:%d]\n", m_numJoints);
	for (U32F ii = 0; ii < m_numJoints; ii++)
	{
		const JacobianJointEntry& entry = m_jointEntries[ii];
		MsgPlayer("jacobian-ik: [%d], name:%s, offset:%d, axis:%s, dof:%s, ik-factor:%.2f, end-effector-acenstor:%d\n",
			ii, DevKitOnly_StringIdToString(entry.m_jointNameId), entry.m_jointOffset, AxisToString(entry.m_axis), DofTypeToString(entry.m_dofType), entry.m_ikFactor, entry.m_endEffectorAncestor);
	}

	// dump end-effectors
	MsgPlayer("jacobian-ik: [end-effectors], [num-effectors:%d]\n", m_numEndEffectors);
	for (U32F ii = 0; ii < m_numEndEffectors; ii++)
	{
		const JacobianEndEffectorEntry& entry = m_endEffectorEntries[ii];
		const Locator& loc = entry.endEffectJointOffset;
		const Point pos = loc.GetTranslation();
		const Quat rot = loc.GetRotation();
		MsgPlayer("jacobian-ik: [%d], offset:%d, goal-type:%d, joint-offset:[(%0.2f, %0.2f, %0.2f), (%.2f, %.2f, %.2f, %.2f)]\n",
			ii, entry.endEffectorOffset, (I32)entry.endEffectorGoalType, (float)pos.X(), (float)pos.Y(), (float)pos.Z(), (float)rot.X(), (float)rot.Y(), (float)rot.Z(), (float)rot.W());
	}

	MsgPlayer("jacobian-ik: [unique-joint-list], [num-joints:%d]\n", m_numUniqueJoints);
	for (U32F iJoint = 0; iJoint < m_numUniqueJoints; iJoint++)
	{
		const Locator& loc = m_uniqueJoints[iJoint].m_ikOffset;
		const Point pos = loc.GetTranslation();
		const Quat rot = loc.GetRotation();
		MsgPlayer("jacobian-ik: [%d], [%d], ik-offset:[(%0.2f, %0.2f, %0.2f), (%.2f, %.2f, %.2f, %.2f)]\n",
			iJoint, m_uniqueJoints[iJoint].m_jointOffset, (float)pos.X(), (float)pos.Y(), (float)pos.Z(), (float)rot.X(), (float)rot.Y(), (float)rot.Z(), (float)rot.W());
	}

	MsgPlayer("jacobian-ik: end dumping jacobian ----------\n");
}

int JacobianMap::GetNumEnabledEndEffectors() const
{
	int numEnabledEndEffectors = 0;
	// get enabled end-effectors.
	ANIM_ASSERT(m_numEndEffectors <= JacobianIkInstance::kMaxNumGoals);
	for (U32F e = 0; e < m_numEndEffectors; e++)
	{
		const JacobianEndEffectorEntry& eachEntry = m_endEffectorEntries[e];
		numEnabledEndEffectors += eachEntry.endEffectEnabled ? 1 : 0;
	}
	return numEnabledEndEffectors;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void JacobianDebugInfo::DebugDrawJointLocWs(float length, Color xColor, Color yColor, Color zColor, bool needText) const
{
	STRIP_IN_FINAL_BUILD;

	for (U32F iJoint = 0; iJoint < m_numUniqueJoints; iJoint++)
	{
		const Point posWs = GetPostJointLocWs(iJoint).GetTranslation();
		const Vector xDirWs = GetLocalX(GetPostJointLocWs(iJoint).GetRotation());
		const Vector yDirWs = GetLocalY(GetPostJointLocWs(iJoint).GetRotation());
		const Vector zDirWs = GetLocalZ(GetPostJointLocWs(iJoint).GetRotation());

		g_prim.Draw(DebugLine(posWs, xDirWs * length, xColor, 2.0f, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(posWs, yDirWs * length, yColor, 2.0f, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(posWs, zDirWs * length, zColor, 2.0f, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);

		if (needText)
		{
			g_prim.Draw(DebugString(GetPostJointLocWs(iJoint).GetTranslation(),
				StringBuilder<64>("%s:%.2f", DevKitOnly_StringIdToString(GetUniqueJointName(iJoint)), m_time.ToSeconds()).c_str(),
				kColorWhite, 0.4f));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawJacobianJointDeltaWs(const JacobianDebugInfo& currInfo, const JacobianDebugInfo* prevInfo)
{
	STRIP_IN_FINAL_BUILD;

	currInfo.DebugDrawJointLocWs(0.1f, kColorRed, kColorGreen, kColorBlue, true);

	if (prevInfo != nullptr)
		prevInfo->DebugDrawJointLocWs(0.07f, kColorRedTrans, kColorGreenTrans, kColorBlueTrans, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawJacobianJointDeltaAngles(const JacobianMap& jacobian,
									   const JacobianDebugInfo** debugInfos,
									   const I32 numInfos,
									   float xOrigin,
									   float yOrigin,
									   float xSize,
									   float ySize,
									   float fScale)
{
	STRIP_IN_FINAL_BUILD;

	ANIM_ASSERT(debugInfos != nullptr);

	if (numInfos < 1)
		return;

	const JacobianDebugInfo& first = *debugInfos[0];

	static const Color debugColors[] =
	{
		kColorRed, kColorBlue, kColorGreen, kColorYellow,
		kColorOrange, kColorCyan, kColorMagenta, kColorDarkGray,
		kColorRedTrans, kColorBlackTrans, kColorGreenTrans, kColorYellowTrans,
		kColorOrangeTrans, kColorCyanTrans, kColorMagentaTrans, kColorWhite,
	};
	const I32 numColors = sizeof(debugColors) / sizeof(debugColors[0]);

	for (U32F iJoint = 0; iJoint < first.m_numJoints; iJoint++)
	{
		const Color color = debugColors[iJoint % numColors];

		// find the maximum value.
		float maxDeltaAngleRad = -1.0f;
		I32 maxDeltaAngleIndex = -1;
		for (U32F iInfo = 0; iInfo < numInfos; iInfo++)
		{
			const JacobianDebugInfo& eachEntry = *debugInfos[iInfo];
			float finalDeltaAngleRad = eachEntry.GetFinalDeltaAngle(iJoint);
			if (abs(finalDeltaAngleRad) > maxDeltaAngleRad)
			{
				maxDeltaAngleRad = abs(finalDeltaAngleRad);
				maxDeltaAngleIndex = iInfo;
			}
		}

		for (U32F iInfo = 0; iInfo < numInfos - 1; iInfo++)
		{
			const JacobianDebugInfo& eachEntry0 = *debugInfos[iInfo];
			const JacobianDebugInfo& eachEntry1 = *debugInfos[iInfo + 1];

			const float x0 = xOrigin + xSize / numInfos * iInfo;
			const float x1 = xOrigin + xSize / numInfos * (iInfo + 1);

			const float angleRad0 = eachEntry0.GetFinalDeltaAngle(iJoint);
			const float angleRad1 = eachEntry1.GetFinalDeltaAngle(iJoint);

			const float angleDeg0 = RADIANS_TO_DEGREES(angleRad0);
			const float angleDeg1 = RADIANS_TO_DEGREES(angleRad1);

			const float factor0 = angleDeg0 / fScale;
			const float factor1 = angleDeg1 / fScale;

			const float y0 = yOrigin + factor0 * ySize;
			const float y1 = yOrigin + factor1 * ySize;

			g_prim.Draw(DebugLine2D(Vec2(x0, y0), Vec2(x1, y1), kDebug2DNormalizedCoords, color));

			if (iInfo == numInfos - 2)
			{
				// last index.
				char text[64];
				const JacobianMap::JacobianJointEntry& jointEntry = jacobian.m_jointEntries[iJoint];
				snprintf(text, sizeof(text) - 1, "%s,%s:%.2f", DevKitOnly_StringIdToString(jointEntry.m_jointNameId), JacobianMap::AxisToString(jointEntry.m_axis), angleDeg1);
				g_prim.Draw(DebugString2D(Vec2(x1, y1), kDebug2DNormalizedCoords, text, color, 0.5f), kPrimDuration1FramePauseable);
			}
			else if (iInfo == maxDeltaAngleIndex && abs(angleDeg0) > 0.1f)
			{
				// max index.
				char text[64];
				const JacobianMap::JacobianJointEntry& jointEntry = jacobian.m_jointEntries[iJoint];
				snprintf(text, sizeof(text) - 1, "%s,%s:%.2f, t:%.2f", DevKitOnly_StringIdToString(jointEntry.m_jointNameId), JacobianMap::AxisToString(jointEntry.m_axis), angleDeg0, eachEntry0.m_time.ToSeconds());
				g_prim.Draw(DebugString2D(Vec2(x0, y0), kDebug2DNormalizedCoords, text, color, 0.5f), kPrimDuration1FramePauseable);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawJacobianIterationsTime(const JacobianDebugInfo** debugInfos,
									 const I32 numInfos,
									 float xOrigin,
									 float yOrigin,
									 float xSize,
									 float ySize,
									 float fScale)
{
	STRIP_IN_FINAL_BUILD;

	ANIM_ASSERT(debugInfos != nullptr);

	for (U32F iInfo = 0; iInfo < numInfos - 1; iInfo++)
	{
		const JacobianDebugInfo& entry0 = *debugInfos[iInfo];
		const JacobianDebugInfo& entry1 = *debugInfos[iInfo + 1];

		const float x0 = xOrigin + xSize / numInfos * iInfo;
		const float x1 = xOrigin + xSize / numInfos * (iInfo + 1);

		const float factor0 = entry0.m_numIterationsUsed / fScale;
		const float factor1 = entry1.m_numIterationsUsed / fScale;

		const float y0 = yOrigin - factor0 * ySize;
		const float y1 = yOrigin - factor1 * ySize;

		g_prim.Draw(DebugLine2D(Vec2(x0, y0), Vec2(x1, y1), kDebug2DNormalizedCoords, kColorWhite), kPrimDuration1FramePauseable);

		if (iInfo == numInfos - 2)
		{
			// last index.
			char text[64];
			snprintf(text, sizeof(text) - 1, "iter:%d", entry1.m_numIterationsUsed);
			g_prim.Draw(DebugString2D(Vec2(x1, y1), kDebug2DNormalizedCoords, text, kColorWhite, 0.5f), kPrimDuration1FramePauseable);
		}
	}
}


