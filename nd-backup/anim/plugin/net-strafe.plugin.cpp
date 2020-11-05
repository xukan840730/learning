/*
* Copyright (c) 2015 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"

class JointLimits;
class JointSet;

void SolveNetStrafeIk(JointSet* pJointSet, const Transform* pObjXformWs, const JointModifierData* pData)
{
	const JointModifierData::NetStrafeIkData& data = *pData->GetNetStrafeIkData();

	const float blend = data.m_chestRotBlend;
	if (blend > 0.0f)
	{
		const Quat chestRot = data.m_chestRot;
		JacobianMap *pJacobianMap = data.m_pJacobianMap;
		JointLimits *pJointLimits = data.m_pJointLimits;
		JacobianIkInstance ik;
		ik.m_pJoints = pJointSet;
		ik.m_pJacobianMap = pJacobianMap;
		ik.m_pJointLimits = pJointLimits;
		ik.m_blend = blend;
		ik.m_maxIterations = 5;
		ik.m_restoreFactor = 0.95f * blend;			// don't use strong restore when we're still blending in
		ik.m_goal[0].SetGoalRotation(chestRot);

		JacobianIkResult result = SolveJacobianIK(&ik);
	}
}
