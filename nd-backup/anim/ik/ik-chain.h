/*
 * Copyright (c) 2011 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/math/locator.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/util/common.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(16) IkChainSetupData
{
	static const U32 kMaxJointIndices = 15;
	I32 m_numIndices;
	I32 m_jointIndices[kMaxJointIndices];

	IkChainSetupData()
		: m_numIndices(0)
	{
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(16) ArmIkJoints
{
	union
	{
		I32 m_jointIndices[4];
		struct  
		{
			I32 m_shoulderJoint;
			I32 m_elbowJoint;
			I32 m_wristJoint;
			I32 m_handPropJoint;
		};
	};
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(16) LegIkJoints
{
	union
	{
		I32 m_jointIndices[3];
		struct  
		{
			I32 m_upperThighJoint;
			I32 m_kneeJoint;
			I32 m_ankleJoint;			
		};
	};

	Vector	m_ankleUp;
	Vector  m_hipAxis;
	bool	m_bReverseKnee; // E.g. front legs for horse, dog.

};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ArmIkTarget
{
	Locator m_targetLoc;
	float m_blend;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct GunIkTargetInfo
{
	Point m_cameraRayStart		 = kOrigin;
	Vector m_cameraRayDir		 = kUnitZAxis;
	Locator m_reticleToPropJoint = kIdentity;
	float m_blend		 = 0.0f;
	bool m_ikGunRotation = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct IkChain
{
	//To do.. Support scale?
	const IkChainSetupData* m_pChainSetup;
	static const U32 kMaxJoints = 16;
	ndanim::JointParams m_params[kMaxJoints];
	Locator m_objectSpaceJointLocators[kMaxJoints];

	IkChain(const IkChainSetupData* pChainSetup, const ndanim::JointParams* pLocalSpaceParams)
		: m_pChainSetup(pChainSetup)		
	{
		for (U32F i = 0; i < kMaxJoints; ++i)
		{
			m_params[i].m_scale = Vector(1.0f, 1.0f, 1.0f);
			m_params[i].m_quat = kIdentity;
			m_params[i].m_trans = kZero;

			m_objectSpaceJointLocators[i] = Locator(kIdentity);
		}
		
		ANIM_ASSERT(m_pChainSetup->m_numIndices < kMaxJoints);
		for (I32F iJoint = 0; iJoint < m_pChainSetup->m_numIndices; ++iJoint)
		{
			const I32F index = m_pChainSetup->m_jointIndices[iJoint];
			ANIM_ASSERT(index >= 0);
			m_params[iJoint] = pLocalSpaceParams[index];
			ANIM_ASSERT(IsFinite(m_params[iJoint].m_trans));
			ANIM_ASSERT(IsFinite(m_params[iJoint].m_quat));
			ANIM_ASSERT(IsNormal(m_params[iJoint].m_quat));
			ANIM_ASSERT(IsFinite(m_params[iJoint].m_scale));
		}
		ComputeObjectSpaceLocators(m_pChainSetup->m_numIndices - 1);
	}

	void OutputJoints(ndanim::JointParams* pLocalSpaceParams, Scalar_arg blend)
	{
		for (I32F iJoint = 0; iJoint < m_pChainSetup->m_numIndices; ++iJoint)
		{
			const I32F index = m_pChainSetup->m_jointIndices[iJoint];
			ANIM_ASSERT(index >= 0);
			ndanim::JointParams& outputJoint = pLocalSpaceParams[index];
			ANIM_ASSERT(IsFinite(outputJoint.m_trans));
			ANIM_ASSERT(IsFinite(outputJoint.m_quat));
			ANIM_ASSERT(IsNormal(outputJoint.m_quat));
			ANIM_ASSERT(IsFinite(outputJoint.m_scale));
			ANIM_ASSERT(IsFinite(m_params[iJoint].m_trans));
			ANIM_ASSERT(IsFinite(m_params[iJoint].m_quat));
			ANIM_ASSERT(IsNormal(m_params[iJoint].m_quat));
			ANIM_ASSERT(IsFinite(m_params[iJoint].m_scale));
			outputJoint.m_trans = Lerp(outputJoint.m_trans, m_params[iJoint].m_trans, blend);
			outputJoint.m_quat = Slerp(outputJoint.m_quat, m_params[iJoint].m_quat, blend);
			outputJoint.m_scale = Lerp(outputJoint.m_scale, m_params[iJoint].m_scale, blend);
			ANIM_ASSERT(IsFinite(outputJoint.m_trans));
			ANIM_ASSERT(IsFinite(outputJoint.m_quat));
			ANIM_ASSERT(IsNormal(outputJoint.m_quat));
			ANIM_ASSERT(IsFinite(outputJoint.m_scale));
		}
	}

	void ComputeObjectSpaceLocators(I32F startIndex)
	{
		startIndex = Min(I32(startIndex), m_pChainSetup->m_numIndices - 1);
		for (I32F iJoint = startIndex; iJoint >= 0; iJoint--)
		{
			if (iJoint == m_pChainSetup->m_numIndices - 1)
			{
				m_objectSpaceJointLocators[iJoint] = Locator(m_params[iJoint].m_trans, m_params[iJoint].m_quat);
				ANIM_ASSERT(IsFinite(m_params[iJoint].m_trans));
				ANIM_ASSERT(IsFinite(m_params[iJoint].m_quat));
				ANIM_ASSERT(IsNormal(m_params[iJoint].m_quat));
				ANIM_ASSERT(IsFinite(m_objectSpaceJointLocators[iJoint]));
			}
			else
			{
				ANIM_ASSERT(IsFinite(m_params[iJoint].m_trans));
				ANIM_ASSERT(IsFinite(m_params[iJoint].m_quat));
				ANIM_ASSERT(IsNormal(m_params[iJoint].m_quat));
				ANIM_ASSERT(IsFinite(m_objectSpaceJointLocators[iJoint + 1]));
				m_objectSpaceJointLocators[iJoint] = m_objectSpaceJointLocators[iJoint + 1].TransformLocator(Locator(m_params[iJoint].m_trans, m_params[iJoint].m_quat));
				ANIM_ASSERT(IsFinite(m_objectSpaceJointLocators[iJoint]));
			}
		}
	}

	const Locator& GetObjectSpaceLocator(I32F jointIndex) const
	{
		ANIM_ASSERT(jointIndex < m_pChainSetup->m_numIndices);
		return m_objectSpaceJointLocators[jointIndex];
	}

	void RotateJointOS(I32F jointIndex, Quat_arg rotor) 
	{
		ANIM_ASSERT(jointIndex < m_pChainSetup->m_numIndices);
		const I32F parent = jointIndex + 1;

		if (parent < m_pChainSetup->m_numIndices)
		{
			ANIM_ASSERT(IsFinite(m_objectSpaceJointLocators[parent]));
			const Quat parentRotation = m_objectSpaceJointLocators[parent].Rot();
			const Quat premul = Conjugate(parentRotation) * rotor * parentRotation;

			m_params[jointIndex].m_quat = Normalize(premul * m_params[jointIndex].m_quat);
		}
		else
		{
			m_params[jointIndex].m_quat = Normalize(rotor * m_params[jointIndex].m_quat);
		}

		ComputeObjectSpaceLocators(parent);	
	}
};
