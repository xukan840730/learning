/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-debug.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/anim/motion-matching/motion-matching.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NdGameObject;
struct MMPose;

namespace DC
{
	struct MotionMatchingPoseWeights;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class IMotionPose
{
public:
	struct BodyData
	{
		Point m_pos;
		Vector m_vel;
	};

	virtual ~IMotionPose() {}

	virtual IMotionPose* Clone() const = 0;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) {}
	virtual BodyData GetCenterOfMassOs() const = 0;
	virtual BodyData GetJointDataByIdOs(StringId64 jointId) const = 0;
	virtual Locator GetJointLocatorOs(StringId64 jointId) const = 0;
	virtual bool HasDataForJointId(StringId64 jointId) const = 0;
	virtual Vector GetFacingOs() const = 0;

	virtual void DebugDraw(const Locator& charLocWs, Color drawColor, DebugPrimTime tt = kPrimDuration1FrameAuto) const
	{
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionMatchingPose : public IMotionPose
{
public:
	static MotionMatchingPose* CreatePoseFromSample(const MMPose& poseDef, const AnimSample& animSample);
	static MotionMatchingPose* CreatePoseFromOther(const MMPose& poseDef, const IMotionPose& otherPose);

	MotionMatchingPose() : m_numJoints(0), m_facingOs(kUnitZAxis) {}
	
	virtual IMotionPose* Clone() const override;
	virtual BodyData GetCenterOfMassOs() const override { return m_comOs; }
	virtual BodyData GetJointDataByIdOs(StringId64 jointId) const override;
	virtual Locator GetJointLocatorOs(StringId64 jointId) const override;
	virtual bool HasDataForJointId(StringId64 jointId) const override;
	virtual Vector GetFacingOs() const override { return m_facingOs; }

	void UpdateJointOs(StringId64 jointId, const Locator& locOs, Vector_arg velOs);

	virtual void DebugDraw(const Locator& charLocWs,
						   Color drawColor,
						   DebugPrimTime tt = kPrimDuration1FrameAuto) const override;

private:
	static CONST_EXPR size_t kMaxJoints = 16;

	struct JointData
	{
		StringId64 m_jointId;
		Locator m_loc;
		Vector m_vel;
	};

	JointData m_jointsOs[kMaxJoints];
	BodyData m_comOs;
	Vector m_facingOs;
	U32 m_numJoints;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class RecordedMotionPose : public IMotionPose
{
public:
	RecordedMotionPose() : m_numJointEntries(0), m_facingJointId(INVALID_STRING_ID_64) {}

	explicit RecordedMotionPose(const IMotionPose& sourcePose, const MMPose& poseDef);

	virtual IMotionPose* Clone() const override;
	virtual BodyData GetCenterOfMassOs() const override { return m_comDataOs; }
	virtual BodyData GetJointDataByIdOs(StringId64 jointId) const override;
	virtual Locator GetJointLocatorOs(StringId64 jointId) const override;
	virtual bool HasDataForJointId(StringId64 jointId) const override;

	virtual Vector GetFacingOs() const override { return m_facingOs; }

private:
	static CONST_EXPR size_t kMaxParts = 16;

	BodyData m_comDataOs;

	BodyData m_jointDataOs[kMaxParts];
	StringId64 m_jointIds[kMaxParts];
	U32 m_numJointEntries;

	Vector m_facingOs;
	Locator m_facingJointLocOs;
	StringId64 m_facingJointId;
};
