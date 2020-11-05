/*
 * Copyright (c) 2009 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "corelib/math/locator.h"
#include "corelib/math/quat-util.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"
#include "shared/math/point.h"
#include "shared/math/quat.h"


/// --------------------------------------------------------------------------------------------------------------- ///
const Locator LimitJointOs(const Locator& jointOs, JointSet* pJointSet, const JointModifierData* pData, U32F eyeIndex)
{
	//	PROFILE(AI, AiEyeModifier_LimitJoint);

	const JointModifierData::EyeIkData* pEyeIkData = pData->GetEyeIkData();

	const Locator parentLocOs = pJointSet->GetJointLocWsIndex(pEyeIkData->m_eyeParentJointId[eyeIndex]);

	const Locator invParentOs = Inverse(parentLocOs);

	const Locator bindPoseLs = pEyeIkData->m_defaultEyeJointLocLs[eyeIndex];
	const Locator invBindPoseLs = Inverse(bindPoseLs);

	const Locator desiredLs = invParentOs.TransformLocator(jointOs);
	const Locator desiredDelta = invBindPoseLs.TransformLocator(desiredLs);

	float xAngle = RADIANS_TO_DEGREES(Asin(Limit(GetLocalZ(desiredDelta.Rot()).X(), -1.0f, 1.0f)));
	float yAngle = RADIANS_TO_DEGREES(Asin(Limit(GetLocalZ(desiredDelta.Rot()).Y(), -1.0f, 1.0f)));

	float newXAngle = DEGREES_TO_RADIANS(Limit(xAngle, pEyeIkData->m_xAngleMin, pEyeIkData->m_xAngleMax));
	float newYAngle = DEGREES_TO_RADIANS(Limit(yAngle, pEyeIkData->m_yAngleMin, pEyeIkData->m_yAngleMax));

	float newX = Sin(newXAngle);
	float newY = Sin(newYAngle);

	const Vector newZdelta = Normalize(Vector(newX, newY, 1.0f));
	Locator limitedLsDelta = Locator(SMath::kZero, QuatFromLookAt(newZdelta, SMath::kUnitYAxis));

	const Locator limitedLs = bindPoseLs.TransformLocator(limitedLsDelta);
	const Locator limitedOs = parentLocOs.TransformLocator(limitedLs);

	return limitedOs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SolveEyeIk(JointSet* pJointSet, const Transform* pObjXformWs, const JointModifierData* pData)
{
	const JointModifierData::EyeIkData* pEyeIkData = pData->GetEyeIkData();
	
	// Disabled for now as it requires a new plugin that also output data like the leg modifier
#if 0
	if ((pEyeIkData->m_eyeJointId[0] < pEyeIkData->m_numAnimatedJoints)
		&& (pEyeIkData->m_eyeJointId[1] < pEyeIkData->m_numAnimatedJoints)
		&& pData->IsEyeOutputEnabled())
	{
		const Locator eyeLOs = pJointSet->GetJointLocWsIndex(pEyeIkData->m_eyeJointId[0]);
		const Locator eyeROs = pJointSet->GetJointLocWsIndex(pEyeIkData->m_eyeJointId[1]);

		const Locator alignLoc = Locator(*pObjXformWs);
		const Locator eyeLWs = alignLoc.TransformLocator(eyeLOs);
		const Locator eyeRWs = alignLoc.TransformLocator(eyeROs);

		pOutputData->m_averageEyeRotPreIkWs = Slerp(eyeLWs.Rot(), eyeRWs.Rot(), 0.5f);
	}
	else
	{
		pOutputData->m_averageEyeRotPreIkWs = pData->GetOutputData()->m_averageEyeRotPreIkWs;
	}
#endif

	for (U32F eyeIndex = 0; eyeIndex < 2; ++eyeIndex)
	{
		if (pEyeIkData->m_eyeJointId[eyeIndex] >= pEyeIkData->m_numAnimatedJoints)
			continue;

		const Locator eyeJointOs = pJointSet->GetJointLocWsIndex(pEyeIkData->m_eyeJointId[eyeIndex]);

		const Vector toPointOs = pEyeIkData->m_lookAtPointOs - eyeJointOs.Pos();
		const Vector normToPointOs = SafeNormalize(toPointOs, GetLocalZ(eyeJointOs.Rot()));
		const Quat newOrientOs = QuatFromLookAt(normToPointOs, kUnitYAxis);

		const Quat newOrientWithNoiseOs = newOrientOs; /* AddNoise(newOrient); */

		const Locator newJointLocOs = Locator(eyeJointOs.Pos(), newOrientWithNoiseOs);

		const Locator limitedJointOs = LimitJointOs(newJointLocOs, pJointSet, pData, eyeIndex);

		pJointSet->SetJointLocWsIndex(pEyeIkData->m_eyeJointId[eyeIndex], limitedJointOs);
	}
}
