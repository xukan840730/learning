/*
 * Copyright (c) 2009 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "corelib/math/locator.h"

#include "ndlib/anim/armik.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/ik/two-bone-ik.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void SolveWeaponGripIk(JointSet* pJointSet, const Transform* pObjXformWs, JointModifierData* pData)
{
	const JointModifierData::WeaponGripIkData& data = *pData->GetWeaponGripIkData();

	const U32 domHand = data.m_hand;
	const U32 offHand = data.m_hand ^ 1;

	const Transform initialLeftPropWs = pJointSet->GetJointLocWsIndex(data.m_jointIndices[offHand].m_prop).AsTransform();
	const Transform initialRightPropWs = pJointSet->GetJointLocWsIndex(data.m_jointIndices[domHand].m_prop).AsTransform();

	pData->GetOutputData()->m_weaponGripPropErr = Dist(initialLeftPropWs.GetTranslation(),
													   initialRightPropWs.GetTranslation());

	if (data.m_fade < NDI_FLT_EPSILON)
	{
		return;
	}

	const F32 handBlend = data.m_handBlend;

	if ((handBlend > 0.01f) && (handBlend < 0.99f) && data.m_pJacobianMap[0] && data.m_pJacobianMap[1])
	{
		JointSet* apJoints[2]		= { pJointSet, pJointSet };
		JacobianMap* apJacobians[2] = { data.m_pJacobianMap[0], data.m_pJacobianMap[1] };
		const float curvedFade = -2.0f * data.m_fade * data.m_fade * data.m_fade + 3.0f * data.m_fade * data.m_fade;
		if (curvedFade > 0.f)
		{
			WeaponIKFeather::SolveWeaponFeatherIk(apJoints, apJacobians, domHand, handBlend, curvedFade, 5, 0.9f);
		}

		// Do the normal ik afterwards because the jacobian doesn't always solve exactly but pretty close.
	}

	const Transform leftWristWs = pJointSet->GetJointLocWsIndex(data.m_jointIndices[offHand].m_wrist).AsTransform();
	const Transform leftPropWs  = pJointSet->GetJointLocWsIndex(data.m_jointIndices[offHand].m_prop).AsTransform();
	const Transform rightWristWs = pJointSet->GetJointLocWsIndex(data.m_jointIndices[domHand].m_wrist).AsTransform();
	const Transform rightPropWs  = pJointSet->GetJointLocWsIndex(data.m_jointIndices[domHand].m_prop).AsTransform();

	Transform toLeftWristTransform = leftWristWs * Inverse(leftPropWs);
	Transform finalTransformWs = toLeftWristTransform * rightPropWs;
	RemoveScale(&finalTransformWs);

	const Locator finalLocatorWs = Locator(finalTransformWs);

	ANIM_ASSERT(IsReasonable(finalLocatorWs));

	TwoBoneIkParams params;
	params.m_abortIfCantSolve = false;
	params.m_goalPos = finalLocatorWs.Pos();
	params.m_finalGoalRot = finalLocatorWs.Rot();
	params.m_jointOffsets[0] = pJointSet->GetJointOffset(data.m_jointIndices[offHand].m_shoulder);
	params.m_jointOffsets[1] = pJointSet->GetJointOffset(data.m_jointIndices[offHand].m_elbow);
	params.m_jointOffsets[2] = pJointSet->GetJointOffset(data.m_jointIndices[offHand].m_wrist);
	params.m_objectSpace = false;
	params.m_pJointSet = pJointSet;
	params.m_tt = data.m_fade;

	TwoBoneIkResults results;
	SolveTwoBoneIK(params, results);
}
