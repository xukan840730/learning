/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/ik/jacobian-ik-plugin.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/nd-anim-plugins.h"

/// --------------------------------------------------------------------------------------------------------------- ///
OrbisAnim::Status JacobianIkPluginCallback(const JacobianIkPluginData* pJacobianIkPlugin,
										   OrbisAnim::JointParams* pJointParamsLs,
										   const OrbisAnim::AnimHierarchySegment* pSegment,
										   OrbisAnim::ValidBits* pValidBitsOut)
{
	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	JointSet* pJointSet = pJacobianIkPlugin->m_pJointSet;
	pJointSet->ReadFromJointParams((ndanim::JointParams*)pJointParamsLs,
								   pSegment->m_firstJoint,
								   pSegment->m_numJoints,
								   pJacobianIkPlugin->m_rootScale);

	JacobianIkInstance instance = pJacobianIkPlugin->m_ikInstance;

	SolveJacobianIK(&instance);

	pJointSet->WriteJointParamsBlend(pJacobianIkPlugin->m_ikFade,
									 (ndanim::JointParams*)pJointParamsLs,
									 pSegment->m_firstJoint,
									 pSegment->m_numJoints);

	if (pValidBitsOut && pJacobianIkPlugin && pJacobianIkPlugin->m_pIkJacobianMap)
	{
		for (I32F iEff = 0; iEff < pJacobianIkPlugin->m_pIkJacobianMap->m_numEndEffectors; ++iEff)
		{
			const I32F iJoint = pJacobianIkPlugin->m_pIkJacobianMap->m_endEffectorEntries[iEff].endEffectorOffset;

			pJointSet->WriteJointValidBits(iJoint, pSegment->m_firstJoint, pValidBitsOut);
		}
	}

	return OrbisAnim::kSuccess;
}
