/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "corelib/math/locator.h"
#include "corelib/math/quat-util.h"
#include "corelib/math/solve-triangle.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"

#include "shared/math/point.h"
#include "shared/math/quat.h"
#include "shared/math/transform.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void SolveLegIk(LegIndex index,
				Point_arg ikTargetOs,
				JointSet* pJointSet,
				const Transform* pObjXformWs,
				const LegIkData* pLegIkData);

/// --------------------------------------------------------------------------------------------------------------- ///
void SolveStrideScaleIk(JointSet* pJointSet, const Transform* pObjXformWs, const JointModifierData* pData)
{
	const LegIkData* pLegIkData = pData->GetLegIkData();

	const LegIkData::JointIndices& leftIndices = pLegIkData->GetJointIndices(kLeftLeg);
	const LegIkData::JointIndices& rightIndices = pLegIkData->GetJointIndices(kRightLeg);

	const Locator lAnkle = pJointSet->GetJointLocOsIndex(leftIndices.m_ankle);
	const Locator rAnkle = pJointSet->GetJointLocOsIndex(rightIndices.m_ankle);
	ANIM_ASSERT(IsFinite(lAnkle));
	ANIM_ASSERT(IsFinite(rAnkle));

	const Transform strideXform = pData->GetStrideIkData()->m_strideTransform;
	const Point scaledLeftLegOS = (lAnkle.GetTranslation() - kOrigin) * strideXform + Point(kOrigin);
	const Point scaledRightLegOS = (rAnkle.GetTranslation() - kOrigin)* strideXform + Point(kOrigin);

	SolveLegIk(kLeftLeg, scaledLeftLegOS, pJointSet, pObjXformWs, pLegIkData);
	SolveLegIk(kRightLeg, scaledRightLegOS, pJointSet, pObjXformWs, pLegIkData);

	const Locator lAnklePostIk = pJointSet->GetJointLocOsIndex(leftIndices.m_ankle);
	const Locator rAnklePostIk = pJointSet->GetJointLocOsIndex(rightIndices.m_ankle);
	ANIM_ASSERT(IsFinite(lAnklePostIk));
	ANIM_ASSERT(IsFinite(rAnklePostIk));

	const Quat lAnkleDeltaOs = lAnkle.GetRotation()*Conjugate(lAnklePostIk.GetRotation());
	const Quat rAnkleDeltaOs = rAnkle.GetRotation()*Conjugate(rAnklePostIk.GetRotation());

	pJointSet->RotateJointOsIndex(leftIndices.m_ankle, lAnkleDeltaOs);
	pJointSet->RotateJointOsIndex(rightIndices.m_ankle, rAnkleDeltaOs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SolveLegIk(LegIndex index,
				Point_arg ikTargetOs,
				JointSet* pJointSet,
				const Transform* pObjXformWs,
				const LegIkData* pInput)
{
	const LegIkData::JointIndices& indices = pInput->GetJointIndices(index);

	const Locator startingLocUpperThigh	= pJointSet->GetJointLocOsIndex(indices.m_upperThigh);
	const Locator startingLocKnee		= pJointSet->GetJointLocOsIndex(indices.m_knee);
	const Locator startingLocAnkle		= pJointSet->GetJointLocOsIndex(indices.m_ankle);
	const Point startingPosUpperThigh	= startingLocUpperThigh.Pos();
	const Point startingPosKnee			= startingLocKnee.Pos();
	const Point startingPosAnkle		= startingLocAnkle.Pos();
	//const Vector startingThighVec = SafeNormalize(startingPosKnee - startingPosUpperThigh, kZero);
	//const Vector startingCalfVec = SafeNormalize(startingPosAnkle - startingPosKnee, kZero);


	const float debugT = Dist(startingPosUpperThigh, startingPosKnee);
	const float debugC = Dist(startingPosKnee, startingPosAnkle);

	const Vector toGoal = ikTargetOs - startingPosUpperThigh;

	float l = Length(toGoal);
	const float t = debugT; // ik->m_pSetup->m_lenThigh;
	const float c = debugC; // ik->m_pSetup->m_lenCalf;
	
	l = Min(l, t + c);

	const Vector oldThigh = Normalize(startingPosKnee - startingPosUpperThigh);
	const Vector oldCalf = Normalize(startingPosAnkle - startingPosKnee);

	const Vector hipForward = -startingLocUpperThigh.TransformVector(pInput->GetHipAxis(index));
	const Vector normOrigHipToAnkle = SafeNormalize(startingPosAnkle - startingPosUpperThigh, kZero);

	// either cross might degenerate, but not both
	const Scalar dotP = Abs(Dot(hipForward, normOrigHipToAnkle));
	Vector origPole(kZero); 
	if (dotP < SCALAR_LC(0.9f))
	{
		origPole = Normalize(Cross(normOrigHipToAnkle, hipForward));
	}
	else
	{
		origPole = Normalize(Cross(oldCalf, oldThigh));
	}

	const Locator alignLoc = Locator(*pObjXformWs);
	//MsgCon("Dist to goal: %f\n", (float)Dist(startingPosAnkle, ikTargetOs));
	PrimServerWrapper ps = PrimServerWrapper(alignLoc);
	ps.SetDuration(kPrimDuration1FramePauseable);
	//ps.DrawLine(startingLocUpperThigh.Pos(),	startingLocUpperThigh.Pos()+pInput->GetHipAxis(index), kColorRed);
/*
	ps.DrawLine(startingLocUpperThigh.Pos(),	startingLocKnee.Pos(), kColorRed);
	ps.DrawLine(startingLocKnee.Pos(),			startingLocAnkle.Pos(), kColorGreen);
	ps.DrawLine(startingLocUpperThigh.Pos(),	startingLocUpperThigh.Pos() + oldCalf, kColorWhite);
	ps.DrawLine(startingLocUpperThigh.Pos(),	startingLocUpperThigh.Pos() + oldThigh, kColorYellow);
	ps.DrawLine(startingLocUpperThigh.Pos(),	startingLocUpperThigh.Pos() + hipForward, kColorBlue);

	ps.DrawLine(startingLocUpperThigh.Pos(),	startingLocUpperThigh.Pos() + origPole, kColorOrange);
*/

	sTriangleAngles triangle = SolveTriangle(l, c, t);
	const Scalar kneeangle = triangle.a;	

	const Scalar startKneeAngle = SafeAcos(Dot(-oldThigh, oldCalf));

	// normalize toGoal
	const Vector normToGoal = SafeNormalize(toGoal, kZero);

/*
	MsgConPauseable("start knee angle: %f\n", (float)startKneeAngle);
	MsgConPauseable("Desired knee angle: %f\n", (float)triangle.a);
	MsgConPauseable("l = %f | t = %f | c = %f | t+c = %f\n", l, t, c, t+c);
	MsgConPauseable("dotP = %f\n", float(dotP));
*/

/*
	ps.DrawLine(startingLocUpperThigh.Pos(), startingLocUpperThigh.Pos() + origPole, kColorCyan);
	ps.DrawLine(startingLocUpperThigh.Pos(), startingLocUpperThigh.Pos() + toGoal, kColorMagenta);
*/

	//DebugDrawLine(alignLoc.TransformPoint(startingLocUpperThigh.GetTranslation()), alignLoc.TransformPoint(startingLocUpperThigh.GetTranslation()) + alignLoc.TransformVector(rotAxis), kColorMagenta, kPrimDuration1FramePauseable);

	if (Length(origPole) > kSmallestFloat)
	{
		//Set the knee angle
		const Quat kneeRotor = QuatFromAxisAngle(origPole, kneeangle - startKneeAngle);
		pJointSet->RotateJointOsIndex(indices.m_knee, kneeRotor);

		//Now orient the hip joint so the ankle is in the correct pos
		const Point firstStageAnkle = pJointSet->GetJointLocOsIndex(indices.m_ankle).Pos();	

		const Vector toAnkleFirstStage = firstStageAnkle - startingPosUpperThigh;
		const Quat thighRotor = QuatFromVectors(Normalize(toAnkleFirstStage), normToGoal);
		pJointSet->RotateJointOsIndex(indices.m_upperThigh, thighRotor);
	}
}
