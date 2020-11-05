/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/ik/ik-chain.h"
#include "ndlib/anim/ik/ik-defs.h"

class ArtItemSkeleton;

/// --------------------------------------------------------------------------------------------------------------- ///
class IkChainSetup
{
public:
	virtual bool Init(StringId64 endJoint, const ArtItemSkeleton* pSkeleton);

	bool ValidateIkChain();
	int FindJointChainIndex(StringId64 jointName, const ArtItemSkeleton* pSkeleton);

	IkChainSetupData m_data;
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum LegSet : U8
{
	kBackLegs, // Bipeds use this.
	kFrontLegs,
	kNumLegSets,
};

/// --------------------------------------------------------------------------------------------------------------- ///
static CONST_EXPR const char* GetLegSetName(LegSet legs)
{
	switch (legs)
	{
	case kFrontLegs:
		return "Front";
	case kBackLegs:
		return "Back";
	}

	return "<unknown>";
}

/// --------------------------------------------------------------------------------------------------------------- ///
class LegIkChainSetup : public IkChainSetup
{
public:
	typedef IkChainSetup ParentClass;

	virtual bool Init(StringId64 endJointId, const ArtItemSkeleton* pSkeleton) override;
	virtual bool Init(StringId64 endJointId,
					  const ArtItemSkeleton* pSkeleton,
					  const StringId64* aJointNames,
					  bool reverseKnee = false);

	LegIkJoints m_legJoints;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct LegIkChainSetups
{
	LegIkChainSetup m_legSetups[kLegCount];
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ArmIkChainSetup : public IkChainSetup
{
public:
	typedef IkChainSetup ParentClass;

	virtual bool Init(StringId64 endJointId, const ArtItemSkeleton* pSkeleton) override;

	I32F GetShoulderJointIndex() const { return GetJointIndex(m_armJoints.m_shoulderJoint); }
	I32F GetElbowJointIndex() const { return GetJointIndex(m_armJoints.m_elbowJoint); }
	I32F GetWristJointIndex() const { return GetJointIndex(m_armJoints.m_wristJoint); }
	I32F GetPropJointIndex() const { return GetJointIndex(m_armJoints.m_handPropJoint); }

private:
	I32F GetJointIndex(U32F iLocal) const
	{
		ANIM_ASSERT(iLocal < m_data.m_numIndices);
		return m_data.m_jointIndices[iLocal];
	}

public:
	ArmIkJoints m_armJoints;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ArmIkChainSetups
{
	ArmIkChainSetup m_armSetups[kArmCount];
};
