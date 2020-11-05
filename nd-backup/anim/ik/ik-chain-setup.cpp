/*
 * Copyright (c) 2011 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/ik/ik-chain-setup.h"

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/profiling/profile-cpu-categories.h"
#include "ndlib/profiling/profile-cpu.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/level/art-item-skeleton.h"

// TODO@QUAD set up ankle rotation offsets here

/// --------------------------------------------------------------------------------------------------------------- ///
bool IkChainSetup::Init(StringId64 endJoint, const ArtItemSkeleton* pSkeleton)
{
	PROFILE(Animation, IkChainSetup_Init);

	I32F endJointIndex = FindJoint(pSkeleton->m_pJointDescs, pSkeleton->m_numGameplayJoints, endJoint);
	if (endJointIndex < 0 || endJointIndex >= pSkeleton->m_numGameplayJoints) // not in segment0
		return false;

	I32F currentJointIndex = endJointIndex;
	I32F numJoints		   = 0;
	while (currentJointIndex >= 0)
	{
		numJoints++;
		currentJointIndex = ndanim::GetParentJoint(pSkeleton->m_pAnimHierarchy, currentJointIndex);
	}

	ANIM_ASSERT(numJoints <= IkChainSetupData::kMaxJointIndices);
	m_data.m_numIndices = numJoints;

	m_data.m_jointIndices[0] = endJointIndex;
	for (int iJoint = 1; iJoint < m_data.m_numIndices; ++iJoint)
	{
		m_data.m_jointIndices[iJoint] = ndanim::GetParentJoint(pSkeleton->m_pAnimHierarchy,
															   m_data.m_jointIndices[iJoint - 1]);
	}

	return ValidateIkChain();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IkChainSetup::ValidateIkChain()
{
	STRIP_IN_FINAL_BUILD_VALUE(true);

	int inGroup = -1;

	// make sure our ik chain doesn't cross the group boundary
	for (int iJoint = 0; iJoint < m_data.m_numIndices; ++iJoint)
	{
		int currentGroup = m_data.m_jointIndices[iJoint] / 128;
		if (inGroup != -1 && inGroup != currentGroup)
			return false;
		inGroup = currentGroup;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int IkChainSetup::FindJointChainIndex(StringId64 jointName, const ArtItemSkeleton* pSkeleton)
{
	const I32 jointIndex = FindJoint(pSkeleton->m_pJointDescs, pSkeleton->m_numGameplayJoints, jointName);

	for (int iJoint = 0; iJoint < m_data.m_numIndices; ++iJoint)
	{
		if (jointIndex == m_data.m_jointIndices[iJoint])
		{
			return iJoint;
		}
	}
	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ArmIkChainSetup::Init(StringId64 endJointId, const ArtItemSkeleton* pSkeleton)
{
	const bool ret = ParentClass::Init(endJointId, pSkeleton);

	if (!ret)
	{
		return false;
	}

	if (endJointId == SID("r_hand_prop_attachment"))
	{
		m_armJoints.m_shoulderJoint = FindJointChainIndex(SID("r_shoulder"), pSkeleton);
		m_armJoints.m_elbowJoint	= FindJointChainIndex(SID("r_elbow"), pSkeleton);
		m_armJoints.m_wristJoint	= FindJointChainIndex(SID("r_wrist"), pSkeleton);
		m_armJoints.m_handPropJoint = FindJointChainIndex(SID("r_hand_prop_attachment"), pSkeleton);
	}
	else
	{
		m_armJoints.m_shoulderJoint = FindJointChainIndex(SID("l_shoulder"), pSkeleton);
		m_armJoints.m_elbowJoint	= FindJointChainIndex(SID("l_elbow"), pSkeleton);
		m_armJoints.m_wristJoint	= FindJointChainIndex(SID("l_wrist"), pSkeleton);
		m_armJoints.m_handPropJoint = FindJointChainIndex(SID("l_hand_prop_attachment"), pSkeleton);
	}

	if ((m_armJoints.m_shoulderJoint < 0) || (m_armJoints.m_elbowJoint < 0) || (m_armJoints.m_wristJoint < 0)
		|| (m_armJoints.m_handPropJoint < 0))
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool LegIkChainSetup::Init(StringId64 endJointId, const ArtItemSkeleton* pSkeleton)
{
	static CONST_EXPR StringId64 leftJointNames[] = 
	{
		SID("l_upper_leg"),
		SID("l_knee"),
		SID("l_ankle"),
		SID("l_heel"),
		SID("l_ball"),
	};

	static CONST_EXPR StringId64 rightJointNames[] =
	{
		SID("r_upper_leg"), 
		SID("r_knee"),
		SID("r_ankle"),
		SID("r_heel"),
		SID("r_ball"),
	};


	const StringId64* aJointNames = endJointId == SID("r_ankle") ? rightJointNames : leftJointNames;

	return Init(endJointId, pSkeleton, aJointNames);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool LegIkChainSetup::Init(StringId64 endJointId,
						   const ArtItemSkeleton* pSkeleton,
						   const StringId64* aJointNames,
						   bool reverseKnee)
{	
	bool ret = ParentClass::Init(endJointId, pSkeleton);

	for (int i = 0; i < 3; ++i)
	{
		m_legJoints.m_jointIndices[i] = FindJointChainIndex(aJointNames[i], pSkeleton);
	}

	m_legJoints.m_hipAxis = kUnitYAxis;
	m_legJoints.m_bReverseKnee = reverseKnee;
	
	{
		const I32F jointIndex = FindJoint(pSkeleton->m_pJointDescs, pSkeleton->m_numGameplayJoints, aJointNames[0]);

		if (jointIndex < 0)
		{
			return false;
		}

		Transform invThigh(ndanim::GetInverseBindPoseTable(pSkeleton->m_pAnimHierarchy)[jointIndex].GetMat44());
		{
			// look at the Z component (facing)
			const Vec4 baseHipAxis(0.f, 0.f, -1.f, 0.f);

			// inverse transform into hip space
			m_legJoints.m_hipAxis = Vector(MulVectorMatrix(baseHipAxis, invThigh.GetMat44()));
		}
	}

	const I32F ankleIndex = FindJoint(pSkeleton->m_pJointDescs, pSkeleton->m_numGameplayJoints, aJointNames[2]);
	if (ankleIndex < 0)
	{
		return false;
	}
	
	const Mat44 invAnkle = ndanim::GetInverseBindPoseTable(pSkeleton->m_pAnimHierarchy)[ankleIndex].GetMat44();
	m_legJoints.m_ankleUp = Vector(MulVectorMatrix(Vec4(0.0f, 0.0f, 1.0f, 0.0f), invAnkle));

	ANIM_ASSERTF(IsNormal(m_legJoints.m_ankleUp),
				 ("Unable to determine ankle up dir (got %s), invAnkle matrix: %s",
				  PrettyPrint(m_legJoints.m_ankleUp),
				  PrettyPrint(invAnkle)));

	//static float s_counter = 0.0f;
	//const Point debugPt = Point(s_counter, 0.0f, 0.0f);
	//s_counter += 1.0f;
	//g_prim.Draw(DebugArrow(debugPt, m_legSetup.m_ankleUp, kColorGreen, 0.5f, PrimAttrib(kPrimEnableHiddenLineAlpha)), Seconds(100.0f));

	return ret;
}
