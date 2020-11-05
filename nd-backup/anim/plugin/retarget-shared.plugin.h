/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/skel-table.h"
#include "ndlib/anim/anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemSkeleton;

/// --------------------------------------------------------------------------------------------------------------- ///
namespace ndanim
{
	struct AnimatedJointPose;
	struct JointHierarchy;
	struct JointParams;
	struct ProcessingGroup;
	struct ValidBits;
}


/// --------------------------------------------------------------------------------------------------------------- ///
struct RetargetWorkInfo
{
	RetargetWorkInfo()
		: m_srcSegIndex(0)
		, m_tgtSegIndex(0)
		, m_rootScale(1.0f)
		, m_jointDataIsAdditive(false)
		, m_pSrcHier(nullptr)
		, m_pTgtHier(nullptr)
		, m_pSrcSegJointParams(nullptr)
		, m_pSrcSegFloatChannels(nullptr)
		, m_pSrcValidBitsTable(nullptr)
		, m_pRetargetEntry(nullptr)
	{
		m_outputPose.m_pJointParams = nullptr;
		m_outputPose.m_pFloatChannels = nullptr;
		m_outputPose.m_pValidBitsTable = nullptr;
	}

	U32									m_srcSegIndex;
	U32									m_tgtSegIndex;

	F32 m_rootScale;
	bool m_jointDataIsAdditive;
	const ndanim::JointHierarchy* m_pSrcHier;
	const ndanim::JointHierarchy* m_pTgtHier;

	// We process one source segment...
	const ndanim::JointParams*			m_pSrcSegJointParams;
	const float*						m_pSrcSegFloatChannels;
	const ndanim::ValidBits*			m_pSrcValidBitsTable;

	// ... and output into a complete joint pose (all segments)
	ndanim::AnimatedJointPose			m_outputPose;
	const SkelTable::RetargetEntry*		m_pRetargetEntry;
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool DoJointRetarget(const SkelTable::JointRetarget& jointRetarget,
					 const ndanim::JointParams& srcJointLs,
					 const ndanim::JointParams& srcDefJointLs,
					 const ndanim::JointParams& tgtDefJointLs,
					 bool jointDataIsAdditive,
					 ndanim::JointParams* pRetargetJointOut);

/// --------------------------------------------------------------------------------------------------------------- ///
void RetargetJointsInSegment(const RetargetWorkInfo& workInfo);
