/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/motion-matching/motion-pose.h"
#include "gamelib/gameplay/nd-subsystem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class BaseSegmentMotionPose : public IMotionPose
{
public:
	virtual IMotionPose* Clone() const override;
	virtual IMotionPose::BodyData GetCenterOfMassOs() const override;
	virtual IMotionPose::BodyData GetJointDataByIdOs(StringId64 jointId) const override;
	virtual Locator GetJointLocatorOs(StringId64 jointId) const override;
	virtual bool HasDataForJointId(StringId64 jointId) const override;
	virtual Vector GetFacingOs() const override { return  SafeNormalize(VectorXz(GetLocalZ(GetJointLocatorOs(SID("spined")).Rot())), kUnitZAxis); }

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual void DebugDraw(const Locator& charLocWs,
						   Color drawColor,
						   DebugPrimTime tt = kPrimDuration1FrameAuto) const override;

private:	
	struct JointData
	{
		Locator m_loc;
		Vector  m_vel;
	};

	BaseSegmentMotionPose() {};	
	void Init(ArtItemSkeletonHandle hSkel);
	int JointIdToIndex(StringId64 jointId) const;

	ListArray<JointData> m_jointLocsOs;
	JointData m_comOs;
	
	ArtItemSkeletonHandle m_hSkel; // To translate id's to indices

	friend class PoseTracker;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class PoseTracker : public NdSubsystem
{
public:
	typedef NdSubsystem ParentClass;

	virtual Err Init(const SubsystemSpawnInfo& info) override;

	SUBSYSTEM_UPDATE_ASYNC(PostJointUpdate);

	const IMotionPose& GetPose() const;

	void OnTeleport() { m_teleported = true; }
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

private:
	BaseSegmentMotionPose m_pose[2];	
	BoundFrame m_poseLocs[2];
	TimeFrame m_poseTime[2];
	int m_currentPoseIndex = 0;
	int m_numPoses = 0;
	bool m_teleported = false;
};
