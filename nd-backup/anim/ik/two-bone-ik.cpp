/*
 * Copyright (c) 2019 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/ik/two-bone-ik.h"
#include "ndlib/anim/anim-options.h"

#include "ndlib/anim/ik/joint-chain.h"

/// --------------------------------------------------------------------------------------------------------------- ///
bool SolveTwoBoneIK(const TwoBoneIkParams& params, TwoBoneIkResults& results)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_ikOptions.m_disableAllIk))
		return false;

	ANIM_ASSERT(IsReasonable(params.m_goalPos));
	ANIM_ASSERT(IsFinite(params.m_finalGoalRot));
	ANIM_ASSERT(IsNormal(params.m_finalGoalRot));

	JointSet* pJointSet = params.m_pJointSet;

	const I32F iJoint0 = params.m_jointOffsets[0];
	const I32F iJoint1 = params.m_jointOffsets[1];
	const I32F iJoint2 = params.m_jointOffsets[2];

	if (!pJointSet || !pJointSet->IsJointDataValid(iJoint0) || !pJointSet->IsJointDataValid(iJoint1)
		|| !pJointSet->IsJointDataValid(iJoint2))
	{
		return false;
	}

	const Locator starting1Ls = pJointSet->GetJointLocLs(iJoint1);
	const Locator starting2Ls = pJointSet->GetJointLocLs(iJoint2);

	const Locator starting0Ws = params.m_objectSpace ? pJointSet->GetJointLocOs(iJoint0)
													 : pJointSet->GetJointLocWs(iJoint0);
	const Locator starting1Ws = params.m_objectSpace ? pJointSet->GetJointLocOs(iJoint1)
													 : pJointSet->GetJointLocWs(iJoint1);
	const Locator starting2Ws = params.m_objectSpace ? pJointSet->GetJointLocOs(iJoint2)
													 : pJointSet->GetJointLocWs(iJoint2);

	const Vector poleVecWs = -GetLocalY(starting1Ws.GetRotation());

	// find the distance from the end effector to the goal position
	const Scalar t = Dist(starting0Ws.Pos(), starting1Ws.Pos());
	const Scalar c = Dist(starting1Ws.Pos(), starting2Ws.Pos());

	Vector toGoalWs = params.m_goalPos - starting0Ws.Pos();
	if (params.m_stretchLimit >= 0.0f)
	{
		toGoalWs = LimitVectorLength(toGoalWs, 0.0f, params.m_stretchLimit * (t + c));
	}

	Scalar toGoalLen = kZero;
	const Vector normToGoalWs = SafeNormalize(toGoalWs, kUnitZAxis, toGoalLen);

	if (params.m_abortIfCantSolve && params.m_tt > 0.5f)
	{
		if ((toGoalLen > (t + c)) || (toGoalLen < Abs(t - c)))
		{
			return false;
		}
	}

	const Scalar l = Min(toGoalLen, t + c);

	// cos angle1 = - l^2 + t^2 + c^2
	//                ---------------
	//                     2 t c

	const Scalar num = Sqr(t) + Sqr(c) - Sqr(l);
	const Scalar denom = SCALAR_LC(2.0f) * t * c;

	const Scalar cosAngle1 = Clamp(AccurateDiv(num, denom), SCALAR_LC(-1.0f), SCALAR_LC(1.0f));
	const Scalar sinAngle1 = Sqrt(SCALAR_LC(1.0f) - Sqr(cosAngle1));

	ANIM_ASSERT(IsFinite(sinAngle1));

	// now, compute angle0
	//  c       =   l
	// ---         ---
	// sin Y       sin angle1

	// so
	//
	//  Y = Asin(c * sin angle1 / l )
	// 

	const Scalar angle0 = Asin(c * AccurateDiv(sinAngle1, l));

	const Quat qRotateUpperWs = QuatFromAxisAngle(poleVecWs, -angle0);

	const Vector startingUpperWs = SafeNormalize(starting1Ws.Pos() - starting0Ws.Pos(), kUnitZAxis);
	const Vector newUpperWs = SafeNormalize(Rotate(qRotateUpperWs, normToGoalWs), normToGoalWs);

	//-----------------------------------------------------------------------
	// okay, we know this new upper arm vector;
	// and we also know the old upper arm vector (from ws shoulder to world space elbow)
	// simply get the delta
	//-----------------------------------------------------------------------

	const Quat qIdentity = Quat(kIdentity);

	const Quat upperDeltaRotWs = QuatFromVectors(startingUpperWs, newUpperWs);
	const Quat fadedUpperDeltaRotWs = Slerp(qIdentity, upperDeltaRotWs, params.m_tt);

	if (params.m_objectSpace)
	{
		pJointSet->RotateJointOs(iJoint0, fadedUpperDeltaRotWs);
	}
	else
	{
		pJointSet->RotateJointWs(iJoint0, fadedUpperDeltaRotWs);
	}

	const Locator firstStage0Ws = params.m_objectSpace ? pJointSet->GetJointLocOs(iJoint0)
													   : pJointSet->GetJointLocWs(iJoint0);

	const Locator firstStage1Ws = firstStage0Ws.TransformLocator(starting1Ls);
	const Locator firstStage2Ws = firstStage1Ws.TransformLocator(starting2Ls);

	//-----------------------------------------------------------------------
	// also, we have to update our middle joint....
	// given our new world space position of our upper arm (computed by the above), figure out it's
	// delta transform in the same way.
	//-----------------------------------------------------------------------

	const Vector oldLowerWs = SafeNormalize(firstStage2Ws.Pos() - firstStage1Ws.Pos(), kUnitZAxis);
	const Vector newLowerWs = SafeNormalize(params.m_goalPos - firstStage1Ws.Pos(), oldLowerWs);

	const Quat lowerDeltaRotWs = QuatFromVectors(oldLowerWs, newLowerWs);
	const Quat fadedLowerDeltaRotWs = Slerp(qIdentity, lowerDeltaRotWs, params.m_tt);

	if (params.m_objectSpace)
	{
		pJointSet->RotateJointOs(iJoint1, fadedLowerDeltaRotWs);
	}
	else
	{
		pJointSet->RotateJointWs(iJoint1, fadedLowerDeltaRotWs);
	}

	const Locator secondStage1Ws = params.m_objectSpace ? pJointSet->GetJointLocOs(iJoint1)
														: pJointSet->GetJointLocWs(iJoint1);
	const Locator secondStage2Ws = params.m_objectSpace ? pJointSet->GetJointLocOs(iJoint2)
														: pJointSet->GetJointLocWs(iJoint2);

	// make our final end joint the desired rotation
	const Quat finalRotWs = Slerp(secondStage2Ws.Rot(), params.m_finalGoalRot, params.m_tt);
	const Locator finalLocWs = Locator(secondStage2Ws.Pos(), finalRotWs);

	if (params.m_objectSpace)
	{
		pJointSet->SetJointLocOs(iJoint2, finalLocWs);
	}
	else
	{
		pJointSet->SetJointLocWs(iJoint2, finalLocWs);
	}

	results.m_valid = true;
	results.m_outputGoalPos = secondStage2Ws.Pos();

	return true;
}
