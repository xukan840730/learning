/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/motion-matching/motion-matching-debug.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/level/art-item-anim.h"
#include "gamelib/level/art-item-skeleton.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingDebug::DrawFuturePose(const AnimSample& curPose,
										 const Locator& refLoc,
										 Color32 c,
										 float timeInFutureSec,
										 float lineWidth)
{
	const ArtItemAnim* pAnim = curPose.Anim().ToArtItem();

	if (!pAnim)
	{
		return;
	}

	AnimSample futureSample = curPose;
	const U32F loopCount = futureSample.Advance(Seconds(timeInFutureSec));

	Locator extraTransform = kIdentity;

	if (loopCount > 0)
	{
		Locator loopTransform;
		EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), 1.0f, &loopTransform, futureSample.Mirror());

		for (U32F i = 0; i < loopCount; ++i)
		{
			extraTransform = extraTransform.TransformLocator(loopTransform);
		}
	}
	else if (futureSample.Phase() < 0.0f)
	{
		EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), 1.0f, &extraTransform, futureSample.Mirror());
		extraTransform = Inverse(extraTransform);
		futureSample   = AnimSample(futureSample.Anim(), futureSample.Phase() + 1.0f, futureSample.Mirror());
	}

	Locator aligns[2];
	EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), curPose.Phase(), &aligns[0], futureSample.Mirror());
	EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), futureSample.Phase(), &aligns[1], futureSample.Mirror());

	Locator futureAlign = refLoc.TransformLocator(aligns[0].UntransformLocator(extraTransform.TransformLocator(aligns[1])));

	DebugDrawFullAnimPose(futureSample, futureAlign, c, lineWidth);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingDebug::DebugDrawFullAnimPose(const AnimSample& animSample,
												const Locator& alignLoc,
												Color32 c,
												float lineWidth)
{
	const ArtItemAnim* pAnim = animSample.Anim().ToArtItem();
	const ArtItemSkeleton* pSkeleton = pAnim ? ResourceTable::LookupSkel(pAnim->m_skelID).ToArtItem() : nullptr;

	if (!pAnim || !pSkeleton)
	{
		return;
	}

	const float frame = animSample.Phase() * pAnim->m_pClipData->m_fNumFrameIntervals;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	Transform* aJointTransformsOs	  = NDI_NEW Transform[pSkeleton->m_numGameplayJoints];
	ndanim::JointParams* aJointParams = NDI_NEW ndanim::JointParams[pSkeleton->m_numAnimatedGameplayJoints];

	bool valid = AnimateObject(alignLoc.AsTransform(),
							   pSkeleton,
							   pAnim,
							   frame,
							   aJointTransformsOs,
							   aJointParams,
							   nullptr,
							   nullptr,
							   animSample.Mirror() ? AnimateFlags::kAnimateFlag_Mirror
												   : AnimateFlags::kAnimateFlag_None);

	const ndanim::DebugJointParentInfo* pParentInfo = ndanim::GetDebugJointParentInfoTable(pSkeleton->m_pAnimHierarchy);
	const I32F numAnimatedJointsInFirstSegment = ndanim::GetNumAnimatedJointsInSegment(pSkeleton->m_pAnimHierarchy, 0);

	for (I32F iJoint = 0; iJoint < numAnimatedJointsInFirstSegment; ++iJoint)
	{
		const I32F iParent = pParentInfo[iJoint].m_parent;
		if (iParent < 0)
			continue;

		const Point posOs = aJointTransformsOs[iJoint].GetTranslation();
		const Point parentPosOs = aJointTransformsOs[iParent].GetTranslation();

		const Point posWs = alignLoc.TransformPoint(posOs);
		const Point parentPosWs = alignLoc.TransformPoint(parentPosOs);

		g_prim.Draw(DebugLine(posWs, parentPosWs, c, lineWidth, kPrimDisableDepthTest), kPrimDuration1FrameNoRecord);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingDebug::DrawTrajectory(const AnimSample& animSample,
										 const Locator& refLoc,
										 Color32 c,
										 int trajSamples,
										 float trajMaxTime,
										 float stoppingFaceDist)
{
	Point prevPos = refLoc.GetTranslation();
	int numSteps  = trajSamples * 4;
	for (int i = 1; i < numSteps; ++i)
	{
		float trajTime = float(i) / (numSteps - 1) * trajMaxTime;
		Maybe<MMLocomotionState> maybeAlignInFuture = ComputeLocomotionStateInFuture(animSample, trajTime, stoppingFaceDist);

		if (maybeAlignInFuture.Valid())
		{
			MMLocomotionState futureAlign = maybeAlignInFuture.Get();
			Point curPos = refLoc.TransformPoint(futureAlign.m_alignOs.Pos());
			g_prim.Draw(DebugLine(prevPos, curPos, kColorRed, 3.0f, kPrimDisableDepthTest), kPrimDuration1FrameNoRecord);
			prevPos = curPos;
		}
	}
}
