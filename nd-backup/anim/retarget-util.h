/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/skel-table.h"

class ArtItemAnim;
class ArtItemSkeleton;

namespace ndanim
{
	struct AnimatedJointPose;
	struct JointParams;
	struct ValidBits;
	struct ProcessingGroup;
	struct JointHierarchy;
} // namespace ndanim

/// --------------------------------------------------------------------------------------------------------------- ///
/// Retarget an animation from one skeleton to another
/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::ValidBits GetRetargetedValidBits(const ArtItemSkeleton* pSkel,
										 const ArtItemAnim* pArtItemAnim,
										 U32 iProcessingGroup);

struct RetargetInput
{
	// Used when we retarget an animation
	const ArtItemAnim* m_pArtItemAnim = nullptr;		// Might be a different hierarchy ID than the source skeleton (out-of-date anim)
	float m_sample = 0.0f;

	// Used when we retarget using a complete animated pose
	const ndanim::AnimatedJointPose* m_pInPose = nullptr;
	bool m_inPoseIsAdditive = false;
};


void RetargetPoseForSegment(const RetargetInput& input,									// The source for the joint data to be retargeted
							const ArtItemSkeleton* pSrcSkel,
							const ArtItemSkeleton* pTgtSkel,
							const SkelTable::RetargetEntry* pRetargetEntry,
							U32 iOutputSeg,												// Target segment index to be filled in by retargeted data
							ndanim::AnimatedJointPose* pOutPose);

bool ResolveJointIndexIntoValidBit(const ndanim::JointHierarchy* pAnimHierarchy,
								   U32F numProcGroups,
								   const ndanim::ProcessingGroup* pProcGroups,
								   U32F jointIdx,
								   U32* pOutChannelGroupIdx,
								   U32* pOutBitIdx);
