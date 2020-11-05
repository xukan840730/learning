/*
 * Copyright (c) 2009 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/nd-anim-util.h"

#include "corelib/math/locator.h"
#include "corelib/math/segment-util.h"
#include "corelib/memory/relocate.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-align-cache.h"
#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-copy-remap-layer.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/leg-fix-ik/leg-fix-ik-plugin.h"
#include "ndlib/anim/nd-anim-plugins.h"
#include "ndlib/anim/retarget-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/frame-params.h"
#include "ndlib/io/package-mgr.h"
#include "ndlib/netbridge/redisrpc-server.h"
#include "ndlib/process/clock.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/ndgi/ndgi-macros.h" // because SAFE_DELETE is here for some reason?
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/resource/resource-table-data.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/anim-blend-table-defines.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/level/art-item-skeleton.h"

#include <orbisanim/util.h>

#if !FINAL_BUILD
#include "ndlib/io/asset-view.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/netbridge/command-server.h"
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
static float s_animationSkewTime = 0.0f;
static float s_animationSkewMaxSeekTime = 0.005f;
static float s_currentAnimationSkewAdjustment = 0.0f;
static NdAtomicLock s_animateConcurrencyLock;

/// --------------------------------------------------------------------------------------------------------------- ///
DualSnapshotNode::DualSnapshotNode()
	: m_lastFrameUsed(0)
{
	for (int i = 0; i < ARRAY_COUNT(m_animSnapshot); i++)
	{
		m_animSnapshot[i].m_jointPose.m_pValidBitsTable = nullptr;
		m_animSnapshot[i].m_jointPose.m_pJointParams = nullptr;
		m_animSnapshot[i].m_jointPose.m_pFloatChannels = nullptr;
		m_animSnapshot[i].m_hierarchyId = 0;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
DualSnapshotNode::~DualSnapshotNode()
{
	for (int i = 0; i < ARRAY_COUNT(m_animSnapshot); i++)
	{
		SAFE_DELETE(m_animSnapshot[i].m_jointPose.m_pValidBitsTable);
		SAFE_DELETE(m_animSnapshot[i].m_jointPose.m_pJointParams);
		SAFE_DELETE(m_animSnapshot[i].m_jointPose.m_pFloatChannels);
		m_animSnapshot[i].m_hierarchyId = 0;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DualSnapshotNode::Init(const FgAnimData* pAnimData)
{
	const ArtItemSkeleton* pCurSkel = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem();

	ndanim::InitSnapshotNode(m_animSnapshot[0], pCurSkel->m_pAnimHierarchy);

	if (pAnimSkel && (pAnimSkel != pCurSkel))
	{
		ndanim::InitSnapshotNode(m_animSnapshot[1], pAnimSkel->m_pAnimHierarchy);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DualSnapshotNode::Copy(const DualSnapshotNode& source, const FgAnimData* pAnimData)
{
	const ArtItemSkeleton* pCurSkel	 = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem();

	ndanim::CopySnapshotNodeData(source.m_animSnapshot[0], &m_animSnapshot[0], pCurSkel->m_pAnimHierarchy);

	if (pAnimSkel && (pAnimSkel != pCurSkel))
	{
		ndanim::CopySnapshotNodeData(source.m_animSnapshot[1], &m_animSnapshot[1], pAnimSkel->m_pAnimHierarchy);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DualSnapshotNode::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_animSnapshot[0].m_jointPose.m_pValidBitsTable, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_animSnapshot[0].m_jointPose.m_pJointParams, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_animSnapshot[0].m_jointPose.m_pFloatChannels, deltaPos, lowerBound, upperBound);

	RelocatePointer(m_animSnapshot[1].m_jointPose.m_pValidBitsTable, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_animSnapshot[1].m_jointPose.m_pJointParams, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_animSnapshot[1].m_jointPose.m_pFloatChannels, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::SnapshotNode* DualSnapshotNode::GetSnapshoteNodeForHeirId(U32 heirId)
{
	ndanim::SnapshotNode* pRet = nullptr;

	if (m_animSnapshot[0].m_hierarchyId == heirId)
	{
		m_lastFrameUsed = GetCurrentFrameNumber();
		pRet = &m_animSnapshot[0];
	}

	if (m_animSnapshot[1].m_hierarchyId == heirId)
	{
		m_lastFrameUsed = GetCurrentFrameNumber();
		pRet = &m_animSnapshot[1];
	}

	return pRet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ndanim::SnapshotNode* DualSnapshotNode::GetSnapshoteNodeForHeirId(U32 heirId) const
{
	const ndanim::SnapshotNode* pRet = nullptr;

	if (m_animSnapshot[0].m_hierarchyId == heirId)
	{
		m_lastFrameUsed = GetCurrentFrameNumber();
		pRet = &m_animSnapshot[0];
	}

	if (m_animSnapshot[1].m_hierarchyId == heirId)
	{
		m_lastFrameUsed = GetCurrentFrameNumber();
		pRet = &m_animSnapshot[1];
	}

	return pRet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool DualSnapshotNode::IsInUse() const
{
	if (RenderFrameParams* pFrameParams = GetRenderFrameParams(m_lastFrameUsed))
	{
		return pFrameParams->m_renderFrameEndTick == 0;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DMENU::ItemEnumPair* GetAnimCurveDevMenuEnumPairs()
{
	static DMENU::ItemEnumPair s_animCurveTypes[] = {
		DMENU::ItemEnumPair("Invalid", DC::kAnimCurveTypeInvalid),
		DMENU::ItemEnumPair("Linear", DC::kAnimCurveTypeLinear),
		DMENU::ItemEnumPair("UniformS", DC::kAnimCurveTypeUniformS),
		DMENU::ItemEnumPair("EaseIn", DC::kAnimCurveTypeEaseIn),
		DMENU::ItemEnumPair("EaseOut", DC::kAnimCurveTypeEaseOut),
		DMENU::ItemEnumPair("LongTail", DC::kAnimCurveTypeLongTail),
		DMENU::ItemEnumPair("LongToe", DC::kAnimCurveTypeLongToe),
		DMENU::ItemEnumPair("QuadraticEaseIn", DC::kAnimCurveTypeQuadraticEaseIn),
		DMENU::ItemEnumPair("QuadraticEaseOut", DC::kAnimCurveTypeQuadraticEaseOut),
		DMENU::ItemEnumPair("QuadraticEaseInOut", DC::kAnimCurveTypeQuadraticEaseInOut),
		DMENU::ItemEnumPair("CubicEaseIn", DC::kAnimCurveTypeCubicEaseIn),
		DMENU::ItemEnumPair("CubicEaseOut", DC::kAnimCurveTypeCubicEaseOut),
		DMENU::ItemEnumPair("CubicEaseInOut", DC::kAnimCurveTypeCubicEaseInOut),
		DMENU::ItemEnumPair("QuarticEaseIn", DC::kAnimCurveTypeQuarticEaseIn),
		DMENU::ItemEnumPair("QuarticEaseOut", DC::kAnimCurveTypeQuarticEaseOut),
		DMENU::ItemEnumPair("QuarticEaseInOut", DC::kAnimCurveTypeQuarticEaseInOut),
		DMENU::ItemEnumPair("QuinticEaseIn", DC::kAnimCurveTypeQuinticEaseIn),
		DMENU::ItemEnumPair("QuinticEaseOut", DC::kAnimCurveTypeQuinticEaseOut),
		DMENU::ItemEnumPair("QuinticEaseInOut", DC::kAnimCurveTypeQuinticEaseInOut),
		DMENU::ItemEnumPair("SinusoidalEaseIn", DC::kAnimCurveTypeSinusoidalEaseIn),
		DMENU::ItemEnumPair("SinusoidalEaseOut", DC::kAnimCurveTypeSinusoidalEaseOut),
		DMENU::ItemEnumPair("SinusoidalEaseInOut", DC::kAnimCurveTypeSinusoidalEaseInOut),
		DMENU::ItemEnumPair("ExponentialEaseIn", DC::kAnimCurveTypeExponentialEaseIn),
		DMENU::ItemEnumPair("ExponentialEaseOut", DC::kAnimCurveTypeExponentialEaseOut),
		DMENU::ItemEnumPair("ExponentialEaseInOut", DC::kAnimCurveTypeExponentialEaseInOut),
		DMENU::ItemEnumPair("CircularEaseIn", DC::kAnimCurveTypeCircularEaseIn),
		DMENU::ItemEnumPair("CircularEaseOut", DC::kAnimCurveTypeCircularEaseOut),
		DMENU::ItemEnumPair("CircularEaseInOut", DC::kAnimCurveTypeCircularEaseInOut),

		DMENU::ItemEnumPair()
	};

	return s_animCurveTypes;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Calculate the blend value to use based on various blend curves. (tt is between 0.0 and 1.0f)
/// --------------------------------------------------------------------------------------------------------------- ///
float CalculateCurveValue(float tt, DC::AnimCurveType curveType)
{
	float blendValue = 1.0f;
	const float tMinus1 = tt - 1.0f;
	const float kPiDiv2 = PI * 0.5f;

	switch (curveType)
	{
	case DC::kAnimCurveTypeUniformS:
		blendValue = (tt * tt) * (3.0f - (2.0f * tt));
		break;

	case DC::kAnimCurveTypeLongTail:
		blendValue = 1.0f / (1.0f + Exp(-(pow(tt, 0.65f) * 1.1f - 0.5f) * 10));
		break;

	case DC::kAnimCurveTypeLongToe:
		blendValue = 1.0f - Sqr(Cos((tt * tt * tt) * PI_DIV_2));
		break;

	case DC::kAnimCurveTypeEaseIn:
		blendValue = (tt * tt * tt) * (2.5f + (-1.5f * (tt * tt)));
		break;

	case DC::kAnimCurveTypeEaseOut:
		tt		   = 1.0f - tt;
		blendValue = (tt * tt * tt) * (2.5f + (-1.5f * (tt * tt)));
		blendValue = 1.0f - blendValue;
		break;

	case DC::kAnimCurveTypeQuadraticEaseIn:
		blendValue = tt * tt;
		break;
	case DC::kAnimCurveTypeQuadraticEaseOut:
		blendValue = 1.0f - tMinus1 * tMinus1;
		break;
	case DC::kAnimCurveTypeQuadraticEaseInOut:
		if (tt < 0.5f)
			blendValue = 2.0f * tt * tt;
		else
			blendValue = 1.0f - 2.0f * tMinus1 * tMinus1;
		break;

	case DC::kAnimCurveTypeCubicEaseIn:
		blendValue = tt * tt * tt;
		break;
	case DC::kAnimCurveTypeCubicEaseOut:
		blendValue = tMinus1 * tMinus1 * tMinus1 + 1.0f;
		break;
	case DC::kAnimCurveTypeCubicEaseInOut:
		if (tt < 0.5f)
			blendValue = 4.0f * tt * tt * tt;
		else
			blendValue = 1.0f + 4.0f * tMinus1 * tMinus1 * tMinus1;
		break;

	case DC::kAnimCurveTypeQuarticEaseIn:
		blendValue = tt * tt * tt * tt;
		break;
	case DC::kAnimCurveTypeQuarticEaseOut:
		blendValue = 1.0f - tMinus1 * tMinus1 * tMinus1 * tMinus1;
		break;
	case DC::kAnimCurveTypeQuarticEaseInOut:
		if (tt < 0.5f)
			blendValue = 8.0f * tt * tt * tt * tt;
		else
			blendValue = 1.0f - 8.0f * tMinus1 * tMinus1 * tMinus1 * tMinus1;
		break;

	case DC::kAnimCurveTypeQuinticEaseIn:
		blendValue = tt * tt * tt * tt * tt;
		break;
	case DC::kAnimCurveTypeQuinticEaseOut:
		blendValue = 1.0f + tMinus1 * tMinus1 * tMinus1 * tMinus1 * tMinus1;
		break;
	case DC::kAnimCurveTypeQuinticEaseInOut:
		if (tt < 0.5f)
			blendValue = 16.0f * tt * tt * tt * tt * tt;
		else
			blendValue = 1.0f + 16.0f * tMinus1 * tMinus1 * tMinus1 * tMinus1 * tMinus1;
		break;

	case DC::kAnimCurveTypeSinusoidalEaseIn:
		blendValue = 1.0f - Cos(kPiDiv2 * tt);
		break;
	case DC::kAnimCurveTypeSinusoidalEaseOut:
		blendValue = Sin(kPiDiv2 * tt);
		break;
	case DC::kAnimCurveTypeSinusoidalEaseInOut:
		blendValue = 0.5f - 0.5f * Cos(PI * tt);
		break;

	case DC::kAnimCurveTypeExponentialEaseIn:
		blendValue = Pow(2.0f, 10.0f * tMinus1);
		break;
	case DC::kAnimCurveTypeExponentialEaseOut:
		blendValue = 1.0f - Pow(2.0f, -10.0f * tt);
		break;
	case DC::kAnimCurveTypeExponentialEaseInOut:
		{
			const float q = 2.0f * tt - 1.0f;
			if (tt < 0.5f)
			{
				blendValue = 0.5f * Pow(2.0f, 10.0f * q);
			}
			else
			{
				blendValue = 1.0f - 0.5f * Pow(2.0f, -10.0f * q);
			}
		}
		break;

	case DC::kAnimCurveTypeCircularEaseIn:
		blendValue = 1.0f - Sqrt(1.0f - tt * tt);
		break;
	case DC::kAnimCurveTypeCircularEaseOut:
		blendValue = Sqrt(1.0f - tMinus1 * tMinus1);
		break;
	case DC::kAnimCurveTypeCircularEaseInOut:
		if (tt < 0.5f)
			blendValue = 0.5f - 0.5f * Sqrt(1.0f - 4.0f * tt * tt);
		else
			blendValue = 0.5f + 0.5f * Sqrt(1.0f - 4.0f * tMinus1 * tMinus1);
		break;

	case DC::kAnimCurveTypeLinear:
	default:
		blendValue = tt;
		break;
	}

	// sanity check
	return Limit01(blendValue);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetMayaFrameFromClip(const ndanim::ClipData* pClipData, float phase)
{
	if (!pClipData || phase < 0.0f)
		return -1.0f;

	const float maxFrameSample = static_cast<float>(pClipData->m_fNumFrameIntervals);
	const float mayaFramesCompensate = 30.0f * pClipData->m_secondsPerFrame;
	const float totalMayaFrames = maxFrameSample * mayaFramesCompensate;

	return phase * totalMayaFrames;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetClipPhaseForMayaFrame(const ndanim::ClipData* pClipData, float mayaFrame)
{
	if (!pClipData || mayaFrame < 0.0f)
		return -1.0f;

	const float maxFrameSample = static_cast<float>(pClipData->m_fNumFrameIntervals);
	const float mayaFramesCompensate = 30.0f * pClipData->m_secondsPerFrame;
	const float totalMayaFrames = maxFrameSample * mayaFramesCompensate;

	if (totalMayaFrames <= 0.0f)
		return 0.0f;

	const float phase = Limit01(mayaFrame / totalMayaFrames);

	return phase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Point LerpChannelLocator(const CompressedChannel* pChannel, U16 lower, U16 upper, float tt)
{
	const float interTT = LerpScaleClamp((float)lower, (float)upper, 0.0f, 1.0f, tt);
	ndanim::JointParams loc1;
	ndanim::JointParams loc2;
	ReadFromCompressedChannel(pChannel, lower, &loc1);
	ReadFromCompressedChannel(pChannel, lower + 1, &loc2);
	Point result = Lerp(loc1.m_trans, loc2.m_trans, interTT);
	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AdvanceByIntegration(const CompressedChannel* pChannel,
						   float& desiredDist,
						   float startPhase,
						   AlignDistFunc distFunc)
{
	ANIM_ASSERT(pChannel);

	float curPhase = startPhase;
	float maxPhase = 1.0f;

	const CompressedChannel* pChannelLs = pChannel;

	const int numKeyFrames = pChannelLs->m_numSamples;
	// noextraframe
	//     if (!looping)
	//     {
	//         --numKeyFrames;
	//     }

	// since we're 0-based,
	// 0.0 is key 0
	// 1.0 is numKeyFrames - 1
	const float keyScalar = (float)(numKeyFrames - 1);

	// first, get the initial portion
	const float keyTT = (keyScalar * curPhase); 
	const U16 lower = (U16)floor(keyTT);
	U16 upper = lower + 1;

	const Point initial = LerpChannelLocator(pChannel, lower, upper, keyTT);

	float accumedDist = 0.0f;

	int maxNumKeyFrames = (int)ceil(maxPhase * keyScalar) + 1;
	if (maxPhase == 1.0f)
	{
		ANIM_ASSERT(maxNumKeyFrames == numKeyFrames);
	}

	curPhase = 1.0f;
	float lowerf = keyTT;
	Point currPos = initial;
	while (upper < maxNumKeyFrames) 
	{
		ndanim::JointParams next;
		ReadFromCompressedChannel(pChannel, upper, &next);

		const float dist = distFunc(currPos, next.m_trans);

		float newAccum = dist + accumedDist;
		if (newAccum >= desiredDist)
		{
			// just add the appropriate amount
			float percentageUsed = LerpScaleClamp(accumedDist, newAccum, 0.0f, 1.0f, desiredDist);
			float upperf = (float)upper;
			lowerf /= keyScalar;
			upperf /= keyScalar;

			accumedDist = desiredDist;

			curPhase = Lerp(lowerf, upperf, percentageUsed);
			break;
		}

		accumedDist = newAccum;
		currPos = next.m_trans;
		lowerf = (float)upper;
		++upper;
	}

	curPhase = Min(curPhase, maxPhase);

	desiredDist -= accumedDist;

	return curPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AdvanceByIntegration(const CompressedChannel* pChannel, float& desiredDist, float startPhase)
{
	return AdvanceByIntegration(pChannel, desiredDist, startPhase, Dist);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Get/Set the clock used to advance the animation system
/// --------------------------------------------------------------------------------------------------------------- ///
static Clock* GetAnimationClock()
{
	return g_animOptions.m_pAnimationClock;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimationTimeUpdate()
{
	const float currentDeltaTime = GetAnimationClock()->GetDeltaTimeInSeconds();
	if (currentDeltaTime > 0.0f)
	{
		// Are we done?
		if (s_animationSkewTime < 0.0001f && s_animationSkewTime > -0.0001f)
		{
			s_animationSkewTime = 0.0f;
			s_currentAnimationSkewAdjustment = 0.0f;
		}
		else
		{
			// If we are slowing down right after a camera cut we don't want to apply the entire correction here as that would
			// potentially slow the animation down to the point where it stops completely for a few frames.
			const float speedScale = Limit01(currentDeltaTime / (1.0f / 30.0f));
			const float oldSkewTime = s_animationSkewTime;
			Seek(s_animationSkewTime, 0.0f, s_animationSkewMaxSeekTime * speedScale);
			s_currentAnimationSkewAdjustment = s_animationSkewTime - oldSkewTime;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimationSetSkewTime(float seconds)
{
	s_animationSkewTime = seconds;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetAnimationSystemTime()
{
	const float animTime = ToSeconds(GetAnimationClock()->GetCurTime());
	return animTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetAnimationSystemDeltaTime()
{
	const float animTime = GetAnimationClock()->GetDeltaTimeInSeconds() * g_animOptions.m_masterClockScale;
	float adjustedTime = animTime;
	if (animTime > 0.0f)
	{
		// The adjustment could be negative
		adjustedTime = Max(animTime + s_currentAnimationSkewAdjustment, 0.0f);
	}
	return adjustedTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SetAnimationClock(Clock* pClock)
{
	g_animOptions.m_pAnimationClock = pClock;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Get the duration of an animation... 5 frames == 4 frame-intervals == 4 * secondsPerFrame
/// --------------------------------------------------------------------------------------------------------------- ///
float GetDuration(const ArtItemAnim* pAnim)
{
	if (!pAnim || !pAnim->m_pClipData)
		return -1.0f;

	return static_cast<float>(pAnim->m_pClipData->m_fNumFrameIntervals * pAnim->m_pClipData->m_secondsPerFrame);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float LimitBlendTime(const ArtItemAnim* pAnim, float startPhase, float desiredBlendTime)
{
	if (!pAnim || !pAnim->m_pClipData)
		return desiredBlendTime;

	const float remainingTime = (1.0f - startPhase) * pAnim->m_pClipData->m_secondsPerFrame
								* pAnim->m_pClipData->m_fNumFrameIntervals;
	const float blendTime = Min(desiredBlendTime, remainingTime);
	return blendTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetPhaseFromClipFrame(const ndanim::ClipData* pClipData, float frame)
{
	if (!pClipData || frame < 0.0f)
		return -1.0f;

	const float maxFrameSample = static_cast<float>(pClipData->m_fNumFrameIntervals);
	const float mayaFramesCompensate = 30.0f * pClipData->m_secondsPerFrame;
	const float totalMayaFrames = maxFrameSample * mayaFramesCompensate;

	return Limit01(frame / totalMayaFrames);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetPhaseFromClipTime(const ndanim::ClipData* pClipData, float time)
{
	if (!pClipData || time < 0.0f)
		return -1.0f;

	const float maxFrameSample = static_cast<float>(pClipData->m_fNumFrameIntervals);
	const float duration = maxFrameSample * pClipData->m_secondsPerFrame;

	return time / duration;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EvaluateChannelInAnim(SkeletonId skelId,
						   const ArtItemAnim* pAnim,
						   StringId64 chanNameId, 
						   float phase,
						   ndanim::JointParams* pParams, 
						   bool mirror /* = false */, 
						   bool wantRawScale /* = false */, 
						   AnimCameraCutInfo* pCameraCutInfo /*= nullptr*/)
{
	if (pParams == nullptr || pAnim == nullptr || chanNameId == INVALID_STRING_ID_64)
		return false;

	EvaluateChannelParams evalParams;
	evalParams.m_pAnim		   = pAnim;
	evalParams.m_channelNameId = chanNameId;
	evalParams.m_phase		   = phase;
	evalParams.m_mirror		   = mirror;
	evalParams.m_wantRawScale  = wantRawScale;
	evalParams.m_pCameraCutInfo = pCameraCutInfo;
	bool retval = EvaluateChannelInAnim(skelId, &evalParams, pParams);

	return retval;	
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EvaluateChannelInAnim(SkeletonId skelId, 
						   const ArtItemAnim* pAnim, 
						   StringId64 chanNameId, 
						   float phase,
						   Locator* pLocOut,
						   bool mirror /* = false */, 
						   bool wantRawScale /* = false */,
						   AnimCameraCutInfo* pCameraCutInfo /*= nullptr*/)
{
	if (pLocOut == nullptr || pAnim == nullptr || chanNameId == INVALID_STRING_ID_64)
		return false;

	ndanim::JointParams params;
	EvaluateChannelParams evalParams;
	evalParams.m_pAnim		   = pAnim;
	evalParams.m_channelNameId = chanNameId;
	evalParams.m_phase		   = phase;
	evalParams.m_mirror		   = mirror;
	evalParams.m_wantRawScale  = wantRawScale;
	evalParams.m_pCameraCutInfo = pCameraCutInfo;
	bool retval = EvaluateChannelInAnim(skelId, &evalParams, &params);
	*pLocOut	= Locator(params.m_trans, params.m_quat);

	return retval;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Get the value of a particular custom channel from an animation at phase 'phase'
/// --------------------------------------------------------------------------------------------------------------- ///
bool EvaluateChannelInAnim(const AnimControl* pAnimControl,
						   StringId64 animNameId, 
						   StringId64 chanNameId,
						   float phase, 
						   ndanim::JointParams* pParams,
						   bool mirror /* = false */,
						   bool wantRawScale /* = false */, 
						   AnimCameraCutInfo* pCameraCutInfo /*= nullptr*/)
{
	if (pParams == nullptr)
		return false;

	// Reset for sane value if something goes wrong...
	pParams->m_scale = Vector(Scalar(1.0f));
	pParams->m_quat	 = Quat(kIdentity);
	pParams->m_trans = Point(kZero);

	if (animNameId != INVALID_STRING_ID_64 && chanNameId != INVALID_STRING_ID_64)
	{
		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animNameId).ToArtItem();
		if (pAnim)
		{
			EvaluateChannelParams evalParams;
			evalParams.m_pAnim		   = pAnim;
			evalParams.m_channelNameId = chanNameId;
			evalParams.m_phase		   = phase;
			evalParams.m_mirror		   = mirror;
			evalParams.m_wantRawScale  = wantRawScale;
			evalParams.m_pCameraCutInfo = pCameraCutInfo;
			return EvaluateChannelInAnim(pAnimControl->GetAnimTable().GetSkelId(), &evalParams, pParams);
		}
	}

	return false;
}


/// --------------------------------------------------------------------------------------------------------------- ///
bool EvaluateChannelInAnim(const AnimControl* pAnimControl,
						   StringId64 animNameId,
						   StringId64 chanNameId,
						   float phase,
						   Locator* pLocOut,
						   bool mirror /* = false */,
						   bool wantRawScale /* = false */,
						   AnimCameraCutInfo* pCameraCutInfo /* = nullptr */)
{
	if (pLocOut == nullptr)
		return false;

	ndanim::JointParams params;
	bool retval = EvaluateChannelInAnim(pAnimControl,
										animNameId,
										chanNameId,
										phase,
										&params,
										mirror,
										wantRawScale,
										pCameraCutInfo);
	*pLocOut	= Locator(params.m_trans, params.m_quat);

	return retval;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindAlignFromApReference(SkeletonId skelId,
							  const ArtItemAnim* pAnim, 
							  float phase, 
							  const Locator& apRef,
							  StringId64 apRefNameId,
							  Locator* pOutAlign, 
							  bool mirror /* = false */)
{
	PROFILE(Animation, FindAlignFromApReference);
	if (!pOutAlign)
		return false;

	// apReference channel data is exported relative to the align, so we need to reverse it
	Locator apRefAs;
	if (!EvaluateChannelInAnim(skelId, pAnim, apRefNameId, phase, &apRefAs, mirror, false))
	{
		// Try evaluating the apRef as if it exist where the 'align' is on the first frame
		if (apRefNameId != SID("align"))
		{
			// If the animation is missing the channel we assume that there is a channel with that name at the first frame of the align.
			Locator fallbackLoc;
			if (!EvaluateChannelInAnim(skelId, pAnim, SID("align"), phase, &fallbackLoc, mirror, false))
			{
				// At least default the outgoing locator to something valid
				ANIM_ASSERT(IsNormal(apRef.GetRotation()));
				*pOutAlign = apRef;
				return false;
			}

			ANIM_ASSERT(IsNormal(fallbackLoc.GetRotation()));
			*pOutAlign = apRef.TransformLocator(fallbackLoc);
			ANIM_ASSERT(IsNormal(pOutAlign->GetRotation()));
			return true;
		}

		// At least default the outgoing locator to something valid
		*pOutAlign = apRef;
		return false;
	}

	ANIM_ASSERT(IsNormal(apRef.GetRotation()));
	ANIM_ASSERT(IsNormal(apRefAs.GetRotation()));

	const Locator alignAPs = Inverse(apRefAs);

	ANIM_ASSERT(IsNormal(alignAPs.GetRotation()));

	// Make sure the rotation is normalized!
	Locator align = apRef.TransformLocator(alignAPs);
	align.SetRotation(Normalize(align.GetRotation()));

	*pOutAlign = align;

	ANIM_ASSERT(IsNormal(pOutAlign->GetRotation()));

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindAlignFromApReference(const AnimControl* pAnimControl,
							  StringId64 animNameId, 
							  float phase, 
							  const Locator& apRef,
							  StringId64 apRefNameId,
							  Locator* pOutAlign, 
							  bool mirror /* = false */)
{
	SkeletonId skelId = pAnimControl->GetAnimTable().GetSkelId();
	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animNameId).ToArtItem();

	return FindAlignFromApReference(skelId, pAnim, phase, apRef, apRefNameId, pOutAlign, mirror);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindAlignFromApReference(const AnimControl* pAnimControl,
							  StringId64 animNameId, 
							  float phase, 
							  const Locator& apRef,
							  Locator* pOutAlign, 
							  bool mirror /* = false */)
{
	return FindAlignFromApReference(pAnimControl, animNameId, phase, apRef, SID("apReference"), pOutAlign, mirror);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindAlignFromApReference(const AnimControl* pAnimControl,
							  StringId64 animNameId,
							  const Locator& apRef,
							  Locator* pOutAlign,
							  bool mirror /* = false */)
{
	return FindAlignFromApReference(pAnimControl, animNameId, 0.0f, apRef, pOutAlign, mirror);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindFutureAlignFromAlign(const SkeletonId skelId,
							  const ArtItemAnim* pAnim,
							  float phase,
							  const Locator& currentAlign,
							  Locator* pOutAlign,
							  bool mirror /* = false */)
{
	return FindAlignFromApReference(skelId, pAnim, phase, currentAlign, SID("align"), pOutAlign, mirror);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindFutureAlignFromAlign(const AnimControl* pAnimControl,
							  StringId64 animNameId,
							  float phase,
							  const Locator& currentAlign,
							  Locator* pOutAlign,
							  bool mirror/* = false*/)
{
	const SkeletonId skelId = pAnimControl->GetAnimTable().GetSkelId();
	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animNameId).ToArtItem();

	return FindFutureAlignFromAlign(skelId, pAnim, phase, currentAlign, pOutAlign, mirror);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindFutureAlignFromAlign(const NdGameObject* pGo,
							  StringId64 animNameId,
							  float phase,
							  Locator* pOutAlign,
							  bool mirror /*= false*/)
{
	const AnimControl* pAnimControl = pGo->GetAnimControl();
	const Locator currLoc = pGo->GetLocator();

	const SkeletonId skelId = pAnimControl->GetAnimTable().GetSkelId();
	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animNameId).ToArtItem();

	return FindFutureAlignFromAlign(skelId, pAnim, phase, currLoc, pOutAlign, mirror);
}


/// --------------------------------------------------------------------------------------------------------------- ///
bool FindApReferenceFromAlign(SkeletonId skeletonId,
							  const ArtItemAnim* pAnim,
							  const Locator& align,
							  Locator* pOutApRef,
							  StringId64 apRefNameId,
							  float phase,
							  bool mirror /* = false */)
{
	PROFILE(Animation, FindApReferenceFromAlign);
	
	if (!pAnim)
		return false;

	if (!pOutApRef)
		return false;

	// apReference channel data is exported relative to the align, so we need to reverse it
	
	EvaluateChannelParams params;
	params.m_pAnim		   = pAnim;
	params.m_channelNameId = apRefNameId;
	params.m_phase		   = phase;
	params.m_mirror		   = mirror;

	ndanim::JointParams apRef;
	if (!EvaluateChannelInAnim(skeletonId, &params, &apRef))
	{		
		// At least default the outgoing locator to something valid
		*pOutApRef = align;
		return false;
	}
	
	Locator apRefAs(apRef.m_trans, apRef.m_quat);

	ANIM_ASSERT(IsNormal(align.GetRotation()));
	ANIM_ASSERT(IsNormal(apRefAs.GetRotation()));

	*pOutApRef = align.TransformLocator(apRefAs);

	// Make sure the rotation is normalized!	
	pOutApRef->SetRotation(Normalize(pOutApRef->GetRotation()));

	ANIM_ASSERT(IsNormal(pOutApRef->GetRotation()));

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindApReferenceFromAlign(SkeletonId skeletonId,
							  const ArtItemAnim* pAnim,
							  const Locator& align,
							  Locator* pOutApRef,
							  float phase,
							  bool mirror /* = false */)
{
	return FindApReferenceFromAlign(skeletonId, pAnim, align, pOutApRef, SID("apReference"), phase, mirror);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindApReferenceFromAlign(const AnimControl* pAnimControl,
							  StringId64 animNameId,
							  const Locator& align,
							  Locator* pOutApRef,
							  float phase,
							  bool mirror /* = false */)
{
	SkeletonId skelId		 = pAnimControl->GetAnimTable().GetSkelId();
	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animNameId).ToArtItem();
	return FindApReferenceFromAlign(skelId, pAnim, align, pOutApRef, SID("apReference"), phase, mirror);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 FindChannelsFromApReference(const AnimControl* pAnimControl,
								StringId64 animNameId,
								float phase,
								const Locator& apRef,
								const StringId64* channelNames,
								int numChannels,
								Locator* pLocsOut,
								bool mirror)
{
	SkeletonId skelId		 = pAnimControl->GetAnimTable().GetSkelId();
	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animNameId).ToArtItem();
	return FindChannelsFromApReference(skelId, pAnim, phase, apRef, channelNames, numChannels, pLocsOut, mirror);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 FindChannelsFromApReference(SkeletonId skeletonId,
								const ArtItemAnim* pAnim,
								float phase,
								const Locator& apRef,
								const StringId64* channelNames,
								int numChannels,
								Locator* pLocsOut,
								bool mirror)
{
	return FindChannelsFromApReference(skeletonId,
									   pAnim,
									   phase,
									   apRef,
									   SID("apReference"),
									   channelNames,
									   numChannels,
									   pLocsOut,
									   mirror);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 FindChannelsFromApReference(SkeletonId skeletonId,
								const ArtItemAnim* pAnim,
								float phase,
								const Locator& apRef,
								const StringId64 apRefId,
								const StringId64* channelNames,
								int numChannels,
								Locator* pLocsOut,
								bool mirror)
{
	Locator alignLoc;
	FindAlignFromApReference(skeletonId, pAnim, phase, apRef, apRefId, &alignLoc, mirror);

	U32F evaluatedChannels = 0;

	for (int i = 0; i < numChannels; i++)
	{
		Locator channelLoc;
		bool valid	= EvaluateChannelInAnim(skeletonId, pAnim, channelNames[i], phase, &channelLoc, mirror);
		pLocsOut[i] = alignLoc.TransformLocator(channelLoc);

		if (valid)
			evaluatedChannels |= (1 << i);
	}

	return evaluatedChannels;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CalculateJointParamsFromJointLocatorsWs(const JointCache& jc, 
											 ndanim::JointParams& outParams,
											 U32F iJoint, 
											 const Locator& jointLocatorWs,
											 const Locator& alignWs,
											 const Vector& invScale)
{
	Locator localSpaceJointLoc;

	I32F parentJoint = jc.GetParentJoint(iJoint);

	if (parentJoint != -1)
	{
		const Locator parentLocWs = jc.GetJointLocatorWs(parentJoint);

		// Convert the new world space location into a proper local space joint transform.
		localSpaceJointLoc = parentLocWs.UntransformLocator(jointLocatorWs);
	}
	else
	{
		localSpaceJointLoc = alignWs.UntransformLocator(jointLocatorWs);
	}

	outParams.m_scale = Vector(1.0f, 1.0f, 1.0f);
	outParams.m_quat = Normalize(localSpaceJointLoc.Rot());
	outParams.m_trans = kOrigin + invScale * (localSpaceJointLoc.Pos() - kOrigin);	
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Version of CalculateJointParams that works on objects set to ConfigNoAnimation.
// The difference is that we can't trust the world space locators in the joint cache,
// so we have to walk the full hierarchy.
void CalculateJointParamsFromJointLocatorsWsNoAnimation(const JointCache& jc, 
														ndanim::JointParams& outParams,
														U32F iJoint,
														const Locator& jointLocatorWs,
														const Locator& alignWs,
														const Vector& invScale)
{
	I32F parentJoint = jc.GetParentJoint(iJoint);
	Locator parentLoc(kIdentity);

	while (parentJoint != -1)
	{
		const ndanim::JointParams& parentLs = jc.GetJointParamsLs(parentJoint);
		const Locator parentLocLs(parentLs.m_trans, parentLs.m_quat);

		parentLoc = parentLocLs.TransformLocator(parentLoc);
		parentJoint = jc.GetParentJoint(parentJoint);
	}

	parentLoc = alignWs.TransformLocator(parentLoc);

	const Locator localSpaceJointLoc = parentLoc.UntransformLocator(jointLocatorWs);

	outParams.m_scale = Vector(1.0f, 1.0f, 1.0f);
	outParams.m_quat  = localSpaceJointLoc.Rot();
	outParams.m_trans = kOrigin + invScale * (localSpaceJointLoc.Pos() - kOrigin);	
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimateObject(const Transform& objectXform,
				   const ArtItemSkeleton* pArtItemSkeleton,
				   const ArtItemAnim* pAnim,
				   float sample,
				   Transform* pOutJointTransforms,
				   ndanim::JointParams* pOutJointParams,
				   float const* pInputControls,
				   float* pOutputControls,
				   AnimateFlags flags)
{
	const ndanim::JointHierarchy* pAnimHierarchy = pArtItemSkeleton->m_pAnimHierarchy;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	void* pPersistentData = NDI_NEW (kAlign16) U8[pArtItemSkeleton->m_persistentDataSize];
	memcpy(pPersistentData, pArtItemSkeleton->m_pInitialPersistentData, pArtItemSkeleton->m_persistentDataSize);

	const I32F numSegmentsToAnimate = (flags & AnimateFlags::kAnimateFlag_AllSegments) ? pArtItemSkeleton->m_numSegments : 1;
	const U32 requiredSegmentMask = (flags & AnimateFlags::kAnimateFlag_AllSegments) ? 0xFFFFFFFF : 1;

	AnimExecutionContext animExecContext;
	animExecContext.Init(pArtItemSkeleton, &objectXform, nullptr, nullptr, nullptr, nullptr, pPersistentData, Memory::TopAllocator());

	U32 outputMask = 0;
	if (pOutJointTransforms)
	{
		outputMask |= AnimExecutionContext::kOutputTransformsOs;

		for (int segmentIndex = 0; segmentIndex < numSegmentsToAnimate; ++segmentIndex)
		{
			const U32F firstJoint = ndanim::GetFirstJointInSegment(animExecContext.m_pSkel->m_pAnimHierarchy, segmentIndex);
			animExecContext.m_pSegmentData[segmentIndex].m_pJointTransforms = pOutJointTransforms + firstJoint;
		}
	}

	if (pOutJointParams)
	{
		outputMask |= AnimExecutionContext::kOutputJointParamsLs;

		if (flags & AnimateFlags::kAnimateFlag_IncludeProceduralJointParamsInOutput)
			animExecContext.m_includeProceduralJointParamsInOutput = true;

		for (int segmentIndex = 0; segmentIndex < numSegmentsToAnimate; ++segmentIndex)
		{
			const U32F firstJoint = ndanim::GetFirstJointInSegment(animExecContext.m_pSkel->m_pAnimHierarchy, segmentIndex);
			const U32F firstAnimatedJoint = ndanim::GetFirstAnimatedJointInSegment(animExecContext.m_pSkel->m_pAnimHierarchy, segmentIndex);

			animExecContext.m_pSegmentData[segmentIndex].m_pJointParams
				= pOutJointParams
				  + (flags & AnimateFlags::kAnimateFlag_IncludeProceduralJointParamsInOutput ? firstJoint
																							 : firstAnimatedJoint);
		}
	}

	// Hook up the plugin funcs
	animExecContext.m_pAnimPhasePluginFunc = EngineComponents::GetAnimMgr()->GetAnimPhasePluginHandler();
	animExecContext.m_pRigPhasePluginFunc = EngineComponents::GetAnimMgr()->GetRigPhasePluginHandler();


	AnimCmdList* pAnimCmdList = &animExecContext.m_animCmdList;

	pAnimCmdList->AddCmd(AnimCmd::kBeginSegment);

	pAnimCmdList->AddCmd(AnimCmd::kBeginAnimationPhase);

	pAnimCmdList->AddCmd_BeginProcessingGroup();
	pAnimCmdList->AddCmd_EvaluateClip(pAnim, 0, sample);
	if (flags & kAnimateFlag_Mirror)
	{
		pAnimCmdList->AddCmd_EvaluateFlip(0);
	}
	pAnimCmdList->AddCmd(AnimCmd::kEndProcessingGroup);

	// Merge work buffer allocations for all jointParams groups, floatChannel groups
	pAnimCmdList->AddCmd(AnimCmd::kEndAnimationPhase);

	// batch set-up commands to prepare for primary set processing
	pAnimCmdList->AddCmd_EvaluateJointHierarchyCmds_Prepare(pInputControls);
	pAnimCmdList->AddCmd_EvaluateJointHierarchyCmds_Evaluate();


	pAnimCmdList->AddCmd(AnimCmd::kEndSegment);

	ProcessRequiredSegments(requiredSegmentMask, outputMask, &animExecContext, nullptr);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimateJoints(const Transform& objectXform,
				   const ArtItemSkeleton* pArtItemSkeleton,
				   const ArtItemAnim* pAnim,
				   float sample,
				   const U32* pJointIndices,
				   U32F numJointIndices,
				   Transform* pOutJointTransforms,
				   ndanim::JointParams* pOutJointParams,
				   float const* pInputControls,
				   float* pOutputControls)
{
	AtomicLockJanitor alj(&s_animateConcurrencyLock, FILE_LINE_FUNC);

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	void* pPersistentData = NDI_NEW(kAlign16) U8[pArtItemSkeleton->m_persistentDataSize];
	memcpy(pPersistentData, pArtItemSkeleton->m_pInitialPersistentData, pArtItemSkeleton->m_persistentDataSize);

	AnimExecutionContext animExecContext;
	animExecContext.Init(pArtItemSkeleton, &objectXform, nullptr, nullptr, nullptr, nullptr, pPersistentData);

	// Setup temp buffers to hold the output as we don't support outputting just a single joint
	U32 outputMask = 0;
	Transform* pTempTransforms = nullptr;
	if (pOutJointTransforms)
	{
		outputMask |= AnimExecutionContext::kOutputTransformsOs;

		pTempTransforms = NDI_NEW (kAlign128) Transform[animExecContext.m_pSkel->m_numGameplayJoints];
		animExecContext.m_pSegmentData[0].m_pJointTransforms = pTempTransforms;
	}

	ndanim::JointParams* pTempJointParams = nullptr;
	if (pOutJointParams)
	{
		outputMask |= AnimExecutionContext::kOutputJointParamsLs;

		pTempJointParams = NDI_NEW (kAlign128) ndanim::JointParams[animExecContext.m_pSkel->m_numAnimatedGameplayJoints];
		animExecContext.m_pSegmentData[0].m_pJointParams = pTempJointParams;
	}

	// Hook up the plugin funcs
	animExecContext.m_pAnimPhasePluginFunc = EngineComponents::GetAnimMgr()->GetAnimPhasePluginHandler();
	animExecContext.m_pRigPhasePluginFunc = EngineComponents::GetAnimMgr()->GetRigPhasePluginHandler();

	U32 requiredSegmentMask = (1 << 0); // flags & kAnimateFlag_AllSegments ? 0xFFFFFFFF : 1;

	AnimCmdList* pAnimCmdList = &animExecContext.m_animCmdList;

	pAnimCmdList->AddCmd(AnimCmd::kBeginSegment);

	pAnimCmdList->AddCmd(AnimCmd::kBeginAnimationPhase);

	pAnimCmdList->AddCmd_BeginProcessingGroup();
	pAnimCmdList->AddCmd_EvaluateClip(pAnim, 0, sample);
	pAnimCmdList->AddCmd(AnimCmd::kEndProcessingGroup);

	// Merge work buffer allocations for all jointParams groups, floatChannel groups
	pAnimCmdList->AddCmd(AnimCmd::kEndAnimationPhase);

	// batch set-up commands to prepare for primary set processing
	pAnimCmdList->AddCmd_EvaluateJointHierarchyCmds_Prepare(pInputControls);
	pAnimCmdList->AddCmd_EvaluateJointHierarchyCmds_Evaluate();

	pAnimCmdList->AddCmd(AnimCmd::kEndSegment);

	ProcessRequiredSegments(requiredSegmentMask, outputMask, &animExecContext, nullptr);
	
	// Now copy the requested joints from the temp buffers
	if (pTempTransforms)
	{
		for (U32F outputJoint = 0; outputJoint < numJointIndices; ++outputJoint)
		{
			ANIM_ASSERT(outputJoint < pArtItemSkeleton->m_numTotalJoints);
			const U32 jointIndex = pJointIndices[outputJoint];
			pOutJointTransforms[outputJoint] = pTempTransforms[jointIndex] * objectXform;
		}
	}

	if (pTempJointParams)
	{
		for (U32F outputJoint = 0; outputJoint < numJointIndices; ++outputJoint)
		{
			ANIM_ASSERT(outputJoint < pArtItemSkeleton->m_numAnimatedGameplayJoints);
			const U32 jointIndex = pJointIndices[outputJoint];
			pOutJointParams[outputJoint] = pTempJointParams[jointIndex];
		}
	}		
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ComparePoses(const ArtItemSkeleton* pSkeleton,
				  const ArtItemAnim* pAnim1, 
				  float sample1, 
				  const ArtItemAnim* pAnim2, 
				  float sample2)
{
	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	if (pSkeleton->m_skelId != pAnim1->m_skelID || pSkeleton->m_skelId != pAnim2->m_skelID)
		return false;

	const U32F numTotalJoints	   = pSkeleton->m_numGameplayJoints;
	Transform* pOriginalTransforms = NDI_NEW Transform[numTotalJoints];
	Transform* pNewTransforms	   = NDI_NEW Transform[numTotalJoints];

	const U32F numAnimatedJoints = pSkeleton->m_numAnimatedGameplayJoints;
	ndanim::JointParams* pOriginalJointParams = NDI_NEW ndanim::JointParams[numAnimatedJoints];
	ndanim::JointParams* pNewJointParams	  = NDI_NEW ndanim::JointParams[numAnimatedJoints];

	// NOTE: We currently don't require any input controls to be driven by the game... if there are any input
	// controls, they are always filled in by animation plug-ins, not by the game code. So uninitialized is OK.
	const U32F numInputControls	  = pSkeleton->m_pAnimHierarchy->m_numInputControls;
	float* pOriginalInputControls = numInputControls > 0 ? NDI_NEW float[numInputControls] : nullptr;
	float* pNewInputControls	  = numInputControls > 0 ? NDI_NEW float[numInputControls] : nullptr;

	const U32F numOutputControls   = pSkeleton->m_pAnimHierarchy->m_numOutputControls;
	float* pOriginalOutputControls = numOutputControls > 0 ? NDI_NEW float[numOutputControls] : nullptr;
	float* pNewOutputControls	   = numOutputControls > 0 ? NDI_NEW float[numOutputControls] : nullptr;

	AnimateObject(Transform(kIdentity),
				  pSkeleton,
				  pAnim1,
				  sample1,
				  pOriginalTransforms,
				  pOriginalJointParams,
				  pOriginalInputControls,
				  pOriginalOutputControls);

	AnimateObject(Transform(kIdentity),
				  pSkeleton,
				  pAnim2,
				  sample2,
				  pNewTransforms,
				  pNewJointParams,
				  pNewInputControls,
				  pNewOutputControls);

	bool identical = true;
	for (U32F i = 0; i < numAnimatedJoints; ++i)
	{
		const Vector diffWs = pOriginalTransforms[i].GetTranslation() - pNewTransforms[i].GetTranslation();
		const Vector diffLs = pOriginalJointParams[i].m_trans - pNewJointParams[i].m_trans;
		const float diffLenWs = Length(diffWs);
		const float diffLenLs = Length(diffLs);
		if (diffLenLs > 0.01f)
		{
			const float mayaFramesCompensate1 = 30.0f * pAnim1->m_pClipData->m_secondsPerFrame;
			const float sampleMayaFrame1 = sample1 * mayaFramesCompensate1;
			const float mayaFramesCompensate2 = 30.0f * pAnim2->m_pClipData->m_secondsPerFrame;
			const float sampleMayaFrame2 = sample2 * mayaFramesCompensate1;
			MsgAnim("Joint Mismatch - '%s'(frame %.2f) != '%s'(frame %.2f) - [%s - %.2f cm].\n",
					pAnim1->GetName(),
					sampleMayaFrame1,
					pAnim2->GetName(),
					sampleMayaFrame2,
					pSkeleton->m_pJointDescs[i].m_pName,
					diffLenLs * 100.0f);
			identical = false;
		}
	}

	return identical;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Convenience functions to find a joint index by name
/// --------------------------------------------------------------------------------------------------------------- ///
I16 FindJoint(const SkelComponentDesc* descsBegin, U32 numJoints, StringId64 jid)
{
	for (int ii = 0; ii < numJoints; ++ii)
	{
		if (jid == descsBegin[ii].m_nameId)
			return (I16)ii;
	}
	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Convenience functions to find a joint name by index
/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 FindJointNameId(const ArtItemSkeleton* pSkel, const I16 jointIdex)
{
	if (pSkel && jointIdex >= 0 && jointIdex < pSkel->m_numTotalJoints)
	{
		return pSkel->m_pJointDescs[jointIdex].m_nameId;
	}
	
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Validate the joints in the joint cache to find bad joint data
/// --------------------------------------------------------------------------------------------------------------- ///
void ValidateJointCache(const FgAnimData* pAnimData)
{
	const JointCache& jointCache = pAnimData->m_jointCache;

	const Transform curXform(pAnimData->m_objXform);
	ANIM_ASSERT(IsOrthogonal(&curXform));
	const Point objWsPos = curXform.GetTranslation();
	ANIM_ASSERT(Length(objWsPos) < 100000.0f);
	ANIM_ASSERT(IsFinite(objWsPos));

	U32F numAnimatedJoints = jointCache.GetNumAnimatedJoints();
	U32F numTotalJoints = jointCache.GetNumTotalJoints();
	ANIM_ASSERT(numAnimatedJoints > 0 && numAnimatedJoints < 1000);
	ANIM_ASSERT(numTotalJoints > 0 && numTotalJoints < 1000);

	const ndanim::JointParams* pJointParams = jointCache.GetJointParamsLs();
	if (pJointParams)
	{
		for (U32F i = 0; i < numAnimatedJoints; ++i)
		{
			const ndanim::JointParams& currentJointParam = pJointParams[i];
			ANIM_ASSERT(IsFinite(currentJointParam.m_trans));
			ANIM_ASSERT(IsFinite(currentJointParam.m_scale));
			ANIM_ASSERT(IsFinite(currentJointParam.m_quat));
		}
	}

	if (jointCache.GetJointLocatorsWs())
	{
		Transform jointXform(SMath::kIdentity);
		for (U32F i = 0; i < numTotalJoints; ++i)
		{
			const Locator loc = jointCache.GetJointLocatorWs(i);
			const Point pos = loc.GetTranslation();
			const Quat rot = loc.GetRotation();

			//		ANIM_ASSERT(Abs(Length4(Vec4(rot.QuadwordValue())) - 1.0f) < 0.05f);
			ANIM_ASSERT(IsOrthogonal(&jointXform));
			ANIM_ASSERT(Length(pos) < 100000.0f);
			ANIM_ASSERT(IsFinite(pos));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Print the animation system commands (not ICE)
/// --------------------------------------------------------------------------------------------------------------- ///
void PrintAnimCmdList(const AnimCmdList* pAnimCmdList,
					  DoutBase* pOutput /* = GetMsgOutput(kMsgAnim) */,
					  const AnimCmd* pCrashCmd /* = nullptr */)
{
	STRIP_IN_FINAL_BUILD;

	if (!pOutput)
		return;

	const U32* const pAnimCmdStream = pAnimCmdList->GetBuffer();
	const U32F animCmdStreamSize = pAnimCmdList->GetNumWordsUsed();

	if (animCmdStreamSize == 0)
		return;

	// A few helper variables used during the animation blending.
	const AnimExecutionContext* pContext = nullptr;

	// Now parse all commands
	U32 beginProcGroupCmdIndex = 0;
	U32F currentWordInStream = 0;
	while (currentWordInStream < animCmdStreamSize)
	{
		const U32* currentCmdAddr = pAnimCmdStream + currentWordInStream;
		Memory::PrefetchForLoad(currentCmdAddr, 0x80);

		const AnimCmd* pCmd = reinterpret_cast<const AnimCmd*>(currentCmdAddr);

		if (pCmd == pCrashCmd)
		{
			pOutput->Print("Crashed Here --> ");
		}

		/*
		const U32 cmd = pAnimCmdStream[currentWordInStream];
		const U32 cmdType = cmd >> 16;
		const U32 numCmdWords = cmd & 0xFF;
		*/
		const U32 cmdType = pCmd->m_type;
		const U32 numCmdWords = pCmd->m_numCmdWords;

		currentWordInStream += numCmdWords;
		switch (cmdType) 
		{
		case AnimCmd::kBeginAnimationPhase:
			{
				pOutput->Printf("Begin Anim Phase\n");
			}
			break;

		case AnimCmd::kEndAnimationPhase:
			{
				pOutput->Printf("End Anim Phase\n");
			}
			break;

		case AnimCmd::kBeginSegment:
			{
				pOutput->Printf("Begin Segment\n");
			}
			break;
			
		case AnimCmd::kEndSegment:
			{
				pOutput->Printf("End Segment\n");
			}
			break;
			
		case AnimCmd::kBeginProcessingGroup:
			{
				const AnimCmd_BeginProcessingGroup* pCmdBeginProcGroup = static_cast<const AnimCmd_BeginProcessingGroup*>(pCmd);
				pOutput->Printf("Begin Processing Group [%d max instances]\n", pCmdBeginProcGroup->m_neededProcGroupInstances);
			}
			break;

		case AnimCmd::kEndProcessingGroup:
			{
				pOutput->Printf("End Processing Group\n");
			}
			break;

		case AnimCmd::kEvaluateClip:
			{
				const AnimCmd_EvaluateClip* pCmdEvalClip = static_cast<const AnimCmd_EvaluateClip*>(pCmd);
				if (pCmdEvalClip->m_pArtItemAnim)
				{
					pOutput->Printf("Clip: \"%s\"%s ", 
						pCmdEvalClip->m_pArtItemAnim->GetName(),
						pCmdEvalClip->m_pArtItemAnim->m_flags & ArtItemAnim::kAdditive ? "(add)" : "");
				}
				else
				{
					pOutput->Printf("Clip: No Art Item\n");
				}

				pOutput->Printf("keyframe sample: %1.2f", pCmdEvalClip->m_frame);

				const ndanim::ClipData* pClipData = pCmdEvalClip->m_pArtItemAnim->m_pClipData;
				if (pClipData)
				{
					pOutput->Printf(" (frame %1.2f) [all] ", pCmdEvalClip->m_frame * 30.0f / pClipData->m_framesPerSecond);
				}
				else
				{
					pOutput->Printf(" (No Clip Data) ");
				}

				pOutput->Printf("into instance %d ", (int)pCmdEvalClip->m_outputInstance);

				if (pContext && pContext->m_pSkel)
				{
					pOutput->Printf("%s\n",
									(pCmdEvalClip->m_pArtItemAnim->m_pClipData->m_animHierarchyId
									 == pContext->m_pSkel->m_hierarchyId)
										? ""
										: "(retargeted)");
				}
				else if (!pContext)
				{
					pOutput->Printf("(No Context)\n");
				}
				else if (!pContext->m_pSkel)
				{
					pOutput->Printf("(No Context Art Item Skeleton)\n");
				}
			}
			break;

		case AnimCmd::kEvaluateBlend:
			{
				const AnimCmd_EvaluateBlend* pCmdEvalBlend = static_cast<const AnimCmd_EvaluateBlend*>(pCmd);
				pOutput->Printf("Blend: Type(%s) of %.2f between instance %d and %d into %d\n",
								pCmdEvalBlend->m_blendMode == ndanim::kBlendSlerp ? "Slerp" : "Additive",
								pCmdEvalBlend->m_blendFactor,
								pCmdEvalBlend->m_leftInstance,
								pCmdEvalBlend->m_rightInstance,
								pCmdEvalBlend->m_outputInstance);
			}
			break;

		case AnimCmd::kEvaluateFeatherBlend:
			{
				const AnimCmd_EvaluateFeatherBlend* pCmdEvalFb = static_cast<const AnimCmd_EvaluateFeatherBlend*>(pCmd);
				const FeatherBlendTable::Entry* pEntry = g_featherBlendTable.GetEntry(pCmdEvalFb->m_featherBlendIndex);

				pOutput->Printf("kEvaluateFeatherBlend: '%s' Type(%s) of %.2f between instance %d and %d into %d\n",
								pEntry ? DevKitOnly_StringIdToString(pEntry->m_featherBlendId) : "<missing>",
								pCmdEvalFb->m_blendMode == ndanim::kBlendSlerp ? "Slerp" : "Additive",
								pCmdEvalFb->m_blendFactor,
								pCmdEvalFb->m_leftInstance,
								pCmdEvalFb->m_rightInstance,
								pCmdEvalFb->m_outputInstance);

			}
			break;

		case AnimCmd::kEvaluateFlip:
			{
				const AnimCmd_EvaluateFlip* pCmdEvalFlip = static_cast<const AnimCmd_EvaluateFlip*>(pCmd);
				pOutput->Printf("Flip: Flipping instance %d\n", (int)pCmdEvalFlip->m_outputInstance);
			}
			break;

		case AnimCmd::kEvaluateEmptyPose:
			{
				const AnimCmd_EvaluateEmptyPose* pCmdEvalEmptyPose = static_cast<const AnimCmd_EvaluateEmptyPose*>(pCmd);
				pOutput->Printf("Empty pose into %d\n", (int)pCmdEvalEmptyPose->m_outputInstance);
			}
			break;
			
		case AnimCmd::kEvaluatePose:
			{
				const AnimCmd_EvaluatePose* pCmdEvalPose = static_cast<const AnimCmd_EvaluatePose*>(pCmd);
				pOutput->Printf("Pose into %d\n", (U32)pCmdEvalPose->m_outputInstance);
			}
			break;

		case AnimCmd::kEvaluateSnapshot:
			{
				const AnimCmd_EvaluateSnapshot* pCmdEvalSnapshot = static_cast<const AnimCmd_EvaluateSnapshot*>(pCmd);
				pOutput->Printf("Snapshot from %d\n", (U32)pCmdEvalSnapshot->m_inputInstance);
			}
			break;

		case AnimCmd::kEvaluateSnapshotDeferred:
			{
				const AnimCmd_EvaluateSnapshotDeferred* pCmdEvalSnapshot = static_cast<const AnimCmd_EvaluateSnapshotDeferred*>(pCmd);
				pOutput->Printf("Snapshot (deferred capable) from %d\n", (U32)pCmdEvalSnapshot->m_inputInstance);
			}
			break;

		case AnimCmd::kEvaluateBindPose:
			{
				const AnimCmd_EvaluateBindPose* pCmdEvalBindPose = static_cast<const AnimCmd_EvaluateBindPose*>(pCmd);
				pOutput->Printf("Bind Pose into %d\n", (U32)pCmdEvalBindPose->m_outputInstance);
			}
			break;

		case AnimCmd::kEvaluateCopy:
			{
				const AnimCmd_EvaluateCopy* pCmdEvalCopy = static_cast<const AnimCmd_EvaluateCopy*>(pCmd);
				pOutput->Printf("Copy from %d to %d\n", (U32)pCmdEvalCopy->m_srcInstance, (U32)pCmdEvalCopy->m_destInstance);
			}
			break;

		case AnimCmd::kEvaluateImpliedPose:
			{
				const AnimCmd_EvaluateImpliedPose* pCmdEvalImpliedPose = static_cast<const AnimCmd_EvaluateImpliedPose*>(pCmd);
				pOutput->Printf("Implied Pose\n");
			}
			break;

		case AnimCmd::kEvaluateFullPose:
		{
			const AnimCmd_EvaluateFullPose* pCmdEvalFullPose = static_cast<const AnimCmd_EvaluateFullPose*>(pCmd);
			pOutput->Printf("Full Pose\n");
		}
		break;

		case AnimCmd::kEvaluateJointHierarchyCmds_Prepare:
			{
				const AnimCmd_EvaluateJointHierarchy_Prepare* pCmdEvalJointHier = static_cast<const AnimCmd_EvaluateJointHierarchy_Prepare*>(pCmd);
				pOutput->Printf("Joint Hierarchy Commands - Prepare\n");
			}
			break;

		case AnimCmd::kEvaluateJointHierarchyCmds_Evaluate:
			{
				const AnimCmd_EvaluateJointHierarchy_Evaluate* pCmdEvalJointHier = static_cast<const AnimCmd_EvaluateJointHierarchy_Evaluate*>(pCmd);
				pOutput->Printf("Joint Hierarchy Commands - Evaluate\n");
			}
			break;

		case AnimCmd::kEvaluateAnimPhasePlugin:
			{
				const AnimCmd_EvaluateAnimPhasePlugin* pCmdEvalPlugin = static_cast<const AnimCmd_EvaluateAnimPhasePlugin*>(pCmd);
				pOutput->Printf("Anim Phase Plugin - '%s'", DevKitOnly_StringIdToString(pCmdEvalPlugin->m_pluginId));

				switch (pCmdEvalPlugin->m_pluginId.GetValue())
				{
				case SID_VAL("leg-fix-ik"):
					{
						const LegFixIkPluginData* pLegFixIkPlugin = PunPtr<const LegFixIkPluginData*>(&pCmdEvalPlugin->m_blindData[0]);
						pOutput->Printf(" [base: %d blended: %d]", pLegFixIkPlugin->m_baseInstance, pLegFixIkPlugin->m_blendedInstance);
					}
					break;
				}
				pOutput->Printf("\n");
			}
			break;

		case AnimCmd::kEvaluateRigPhasePlugin:
			{
				const AnimCmd_EvaluateRigPhasePlugin* pCmdEvalPlugin = static_cast<const AnimCmd_EvaluateRigPhasePlugin*>(pCmd);
				pOutput->Printf("Rig Phase Plugin - '%s'\n", DevKitOnly_StringIdToString(pCmdEvalPlugin->m_pluginId));
			}
			break;

		case AnimCmd::kLayer:
			{
				const AnimCmd_Layer* pCmdLayer = static_cast<const AnimCmd_Layer*>(pCmd);
				pOutput->Printf("Layer - %s\n", DevKitOnly_StringIdToString(pCmdLayer->m_layerNameId));
			}
			break;

		case AnimCmd::kTrack:
			{
				const AnimCmd_Track* pCmdTrack = static_cast<const AnimCmd_Track*>(pCmd);
				pOutput->Printf("Track Ended - %d\n", pCmdTrack->m_trackIndex);
			}
			break;

		case AnimCmd::kState:
			{
				const AnimCmd_State* pCmdState = static_cast<const AnimCmd_State*>(pCmd);
				pOutput->Printf("State Ended - %s - fade: %f\n", DevKitOnly_StringIdToString(pCmdState->m_stateNameId), pCmdState->m_fadeTime);
			}
			break;

		case AnimCmd::kEvaluatePostRetarget:
			{
				const AnimCmd_EvaluatePostRetarget* pCmdPostRetarget = static_cast<const AnimCmd_EvaluatePostRetarget*>(pCmd);
				pOutput->Printf("PostRetargetAnim %s -> %s (input @ 0x%.8p)\n",
								ResourceTable::LookupSkelName(pCmdPostRetarget->m_pSrcSkel->m_skelId),
								ResourceTable::LookupSkelName(pCmdPostRetarget->m_pTgtSkel->m_skelId),
								pCmdPostRetarget->m_inputPose.m_pJointParams);
			}
			break;

		case AnimCmd::kEvaluateSnapshotPoseDeferred:
			{
				const AnimCmd_EvaluateSnapshotPoseDeferred* pCmdEvalPose = static_cast<const AnimCmd_EvaluateSnapshotPoseDeferred*>(pCmd);
				pOutput->Printf("Snapshot Pose (deferred capable) into %d\n", (U32)pCmdEvalPose->m_outputInstance);
			}
			break;

		case AnimCmd::kInitializeJointSet_AnimPhase:
			{
				pOutput->Printf("Initialize Joint Set (Anim Phase)\n");
			}
			break;

		case AnimCmd::kInitializeJointSet_RigPhase:
			{
				pOutput->Printf("Initialize Joint Set (Rig Phase)\n");
			}
			break;

		case AnimCmd::kCommitJointSet_AnimPhase:
			{
				pOutput->Printf("Commit Joint Set (Anim Phase)\n");
			}
			break;

		case AnimCmd::kCommitJointSet_RigPhase:
			{
				pOutput->Printf("Commit Joint Set (Rig Phase)\n");
			}
			break;

		default:
			pOutput->Printf("PrintAnimCmdList : Unhandled AnimCmd: %d\n", cmdType);
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache_UpdateWSSubPose_Recursive(const Locator& alignWs,
										  JointCache* pJc,
										  U32F iJoint,
										  bool debugDraw,
										  Color debugColor)
{
	if (iJoint == 0)
	{
		const ndanim::JointParams& rootJp = pJc->GetJointParamsLs(0);
		const float rootScale = rootJp.m_scale.X();
	
		const Point scaledTrans = Point(rootJp.m_trans.GetVec4() * rootScale);
		const Locator scaledRootJointLocLs(scaledTrans, rootJp.m_quat);
		const Locator rootJointLocWs = alignWs.TransformLocator(scaledRootJointLocLs);
		pJc->OverwriteJointLocatorWs(0, rootJointLocWs);
	}
	else
	{
		pJc->UpdateJointLocatorWs(iJoint);
	}

	if (debugDraw)
	{
		I32F parentJoint = pJc->GetParentJoint(iJoint);
		const Point jointPosWs = pJc->GetJointLocatorWs(iJoint).Pos();
		g_prim.Draw(DebugCross(jointPosWs, 0.05f, debugColor));
		if (parentJoint >= 0)
		{
			const Point parentPosWs = pJc->GetJointLocatorWs(parentJoint).Pos();
			g_prim.Draw(DebugLine(jointPosWs, parentPosWs, debugColor, kColorWhite));
		}
	}

	const ndanim::DebugJointParentInfo* pParentInfo = pJc->GetParentInfo();
	I32 child = pParentInfo[iJoint].m_child;
	if ((child != -1) && (child != 0) && (child != iJoint))
	{
		I32 firstchild = child;
		for (;;)
		{
			JointCache_UpdateWSSubPose_Recursive(alignWs, pJc, child, debugDraw, debugColor);
			I32 nextchild = pParentInfo[child].m_sibling;
			if ((nextchild == firstchild) || (nextchild == child) || (nextchild == -1))
				break;
			child = nextchild;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache_UpdateWSSubPose(const Locator& alignWs,
								JointCache* pJc,
								U32F iJoint,
								bool debugDraw /* = false */,
								Color debugColor /* = kColorBlue */)
{
	PROFILE_AUTO(Animation);
	JointCache_UpdateWSSubPose_Recursive(alignWs, pJc, iJoint, debugDraw, debugColor);
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimInstanceFlagBlender : public AnimStateLayer::InstanceBlender<float>
{
public:
	AnimInstanceFlagBlender(FadeMethodToUse fadeMethod, const DC::AnimStateFlag flags)
		: m_flags(flags), m_fadeMethod(fadeMethod)
	{
	}

	virtual float GetDefaultData() const override { return 0.0f; }
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut) override
	{
		const DC::AnimStateFlag stateFlag = pInstance->GetStateFlags();
		if (pInstance && (stateFlag & m_flags))
			*pDataOut = 1.0f;
		else
			*pDataOut = 0.0f;

		return true;
	}

	virtual float BlendData(const float& leftData,
							const float& rightData,
							float masterFade,
							float animFade,
							float motionFade) override
	{
		float fade = 0.0f;
		switch (m_fadeMethod)
		{
		case kUseMasterFade:
			fade = masterFade;
			break;
		case kUseAnimFade:
			fade = animFade;
			break;
		case kUseMotionFade:
			fade = motionFade;
			break;
		}

		const float factor = Lerp(leftData, rightData, fade);
		return factor;
	}

	DC::AnimStateFlag m_flags;
	FadeMethodToUse m_fadeMethod;
};

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputeFlagContribution(const AnimStateLayer* pLayer,
							  const DC::AnimStateFlag flags,
							  FadeMethodToUse fadeMethod /* = kUseMasterFade */)
{
	AnimInstanceFlagBlender b(fadeMethod, flags);
	const float factor = b.BlendForward(pLayer, 0.0f);

	return factor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct FloatChannelPair
{
	float m_val = 0.0f;
	bool m_valid = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class FloatChannelBlender : public AnimStateLayer::InstanceBlender<FloatChannelPair>
{
public:
	FloatChannelBlender(StringId64 channelId, float defaultValue, FadeMethodToUse fadeMethod)
		: m_channelId(channelId), m_default(defaultValue), m_fadeMethod(fadeMethod)
	{
	}

	virtual FloatChannelPair GetDefaultData() const { return { m_default, true }; }

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, FloatChannelPair* pDataOut)
	{
		if (!pInstance || !pDataOut)
			return false;

		float fVal;

		if (!pInstance->EvaluateFloatChannels(&m_channelId, 1, &fVal))
			return false;

		pDataOut->m_val = fVal;
		pDataOut->m_valid = true;
		return true;
	}

	virtual FloatChannelPair BlendData(const FloatChannelPair& leftData,
									   const FloatChannelPair& rightData,
									   float masterFade,
									   float animFade,
									   float motionFade)
	{
		if (leftData.m_valid && rightData.m_valid)
		{
			float tt = masterFade;
			switch (m_fadeMethod)
			{
			case kUseAnimFade: tt = animFade; break;
			case kUseMotionFade: tt = motionFade; break;
			case kUseMasterFade: tt = masterFade; break;
			}

			const float fVal = Lerp(leftData.m_val, rightData.m_val, tt);
			return { fVal, true };
		}
		else if (leftData.m_valid)
		{
			return leftData;
		}
		else if (rightData.m_valid)
		{
			return rightData;
		}

		return FloatChannelPair();
	}

private:
	StringId64 m_channelId;
	float m_default;
	FadeMethodToUse m_fadeMethod;
};

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputeFloatChannelContribution(const AnimStateLayer* pLayer,
									  StringId64 channelId,
									  float defaultValue,
									  FadeMethodToUse fadeMethod /* = kUseAnimFade */)
{
	if (!pLayer)
		return defaultValue;

	FloatChannelBlender b(channelId, defaultValue, fadeMethod);
	const FloatChannelPair pair = b.BlendForward(pLayer, { defaultValue, true });

	return pair.m_val;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct ChannelPair
{
	ndanim::JointParams m_sqt;
	U32F m_evalulatedChannelMask;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimInstanceChannelBlender : public AnimStateLayer::InstanceBlender<ChannelPair>
{
public:
	AnimInstanceChannelBlender(StringId64 channelId,
							   ndanim::JointParams neutralSqt,
							   bool wantRawScale,
							   bool blendWithNonContributingAnimations,
							   bool useMotionFade,
							   DC::AnimFlipMode flipMode,
							   bool disableRetargeting = false,
		bool blendWithNonContributingAnimsForReal = false)
		: m_allEvaluatedChannelsMask(0)
		, m_channelId(channelId)
		, m_neutralSqt(neutralSqt)
		, m_wantRawScale(wantRawScale)
		, m_useMotionFade(useMotionFade)
		, m_blendWithNonContributingAnimations(blendWithNonContributingAnimations)
		, m_flipMode(flipMode)
		, m_disableRetargeting(disableRetargeting)
		, m_blendWithNonContributingAnimsForReal(blendWithNonContributingAnimsForReal)
	{
	}

	virtual ChannelPair GetDefaultData() const override
	{
		ChannelPair p;
		p.m_sqt = m_neutralSqt;
		p.m_evalulatedChannelMask = 0;
		return p;
	}

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, ChannelPair* pDataOut) override
	{
		AnimStateEvalParams params;
		params.m_flipMode = m_flipMode;
		params.m_wantRawScale = m_wantRawScale;
		params.m_disableRetargeting = m_disableRetargeting;

		ChannelPair currentPair;
		currentPair.m_evalulatedChannelMask = pInstance->EvaluateChannels(&m_channelId, 1, &currentPair.m_sqt, params);
		m_allEvaluatedChannelsMask |= currentPair.m_evalulatedChannelMask;
		*pDataOut = currentPair;
		return true;
	}

	virtual ChannelPair BlendData(const ChannelPair& leftData,
								  const ChannelPair& rightData,
								  float masterFade,
								  float animFade,
								  float motionFade) override
	{
		ChannelPair result = leftData;
		const float fade   = m_useMotionFade ? motionFade : animFade;

		if (rightData.m_evalulatedChannelMask != 0u)
		{
			if (leftData.m_evalulatedChannelMask != 0u)
			{
				AnimChannelJointBlend(&result.m_sqt, leftData.m_sqt, rightData.m_sqt, fade);
			}
			else
			{
				if (m_blendWithNonContributingAnimsForReal)
				{
					AnimChannelJointBlend(&result.m_sqt, m_neutralSqt, rightData.m_sqt, fade);
				}
				else
				{
					AnimChannelJointBlend(&result.m_sqt, leftData.m_sqt, rightData.m_sqt, 1.0f);
				}
			}

			result.m_evalulatedChannelMask = rightData.m_evalulatedChannelMask;
		}
		else if (m_blendWithNonContributingAnimations)
		{
			AnimChannelJointBlend(&result.m_sqt, leftData.m_sqt, m_neutralSqt, fade);
		}

		return result;
	}

	U32F m_allEvaluatedChannelsMask;
	StringId64 m_channelId;
	ndanim::JointParams m_neutralSqt;
	bool m_wantRawScale;
	bool m_useMotionFade;
	bool m_blendWithNonContributingAnimations;
	bool m_disableRetargeting = false;
	bool m_blendWithNonContributingAnimsForReal;
	DC::AnimFlipMode m_flipMode;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// only use for push object, to get handAp channels
class AnimInstanceChannelBlenderConditional : public AnimInstanceChannelBlender
{
	typedef AnimInstanceChannelBlender ParentClass;
	AnimStateLayerFilterAPRefCallBack m_filterFunc;

public:
	AnimInstanceChannelBlenderConditional(StringId64 channelId,
										  ndanim::JointParams neutralSqt,
										  bool wantRawScale,
										  bool blendWithNonContributingAnimations,
										  bool useMotionFade,
										  DC::AnimFlipMode flipMode,
										  AnimStateLayerFilterAPRefCallBack filterCallback,
		                                  bool disableRetargeting = false)
		: AnimInstanceChannelBlender(channelId,
									 neutralSqt,
									 wantRawScale,
									 blendWithNonContributingAnimations,
									 useMotionFade,
									 flipMode,
			                         disableRetargeting)
		, m_filterFunc(filterCallback)
	{
	}

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, ChannelPair* pDataOut) override
	{
		if (m_filterFunc != nullptr)
		{
			if (m_filterFunc(pInstance) == false)
			{
				*pDataOut = GetDefaultData();
				return true;
			}
		}

		return ParentClass::GetDataForInstance(pInstance, pDataOut);
	}

	virtual ChannelPair BlendData(const ChannelPair& leftData,
								  const ChannelPair& rightData,
								  float masterFade,
								  float animFade,
								  float motionFade) override
	{
		ChannelPair result = leftData;
		const float fade   = m_useMotionFade ? motionFade : animFade;

		if (rightData.m_evalulatedChannelMask != 0u)
		{
			if (leftData.m_evalulatedChannelMask != 0u)
			{
				AnimChannelJointBlend(&result.m_sqt, leftData.m_sqt, rightData.m_sqt, fade);
			}
			else
			{
				AnimChannelJointBlend(&result.m_sqt, leftData.m_sqt, rightData.m_sqt, 1.0f);
			}

			result.m_evalulatedChannelMask = rightData.m_evalulatedChannelMask;
		}
		else if (leftData.m_evalulatedChannelMask != 0u)
		{
			AnimChannelJointBlend(&result.m_sqt, leftData.m_sqt, rightData.m_sqt, 0.0f);
			result.m_evalulatedChannelMask = leftData.m_evalulatedChannelMask;
		}
		else if (m_blendWithNonContributingAnimations)
		{
			AnimChannelJointBlend(&result.m_sqt, leftData.m_sqt, m_neutralSqt, fade);
		}

		return result;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimInstanceChannelBlenderWithRemap : public AnimInstanceChannelBlender
{
	typedef AnimInstanceChannelBlender ParentClass;

	const AnimCopyRemapLayer* m_pRemapLayer;

public:
	AnimInstanceChannelBlenderWithRemap(StringId64 channelId,
										ndanim::JointParams neutralSqt,
										bool wantRawScale,
										bool blendWithNonContributingAnimations,
										bool useMotionFade,
										DC::AnimFlipMode flipMode,
										const AnimCopyRemapLayer* pRemapLayer)
		: AnimInstanceChannelBlender(channelId,
									 neutralSqt,
									 wantRawScale,
									 blendWithNonContributingAnimations,
									 useMotionFade,
									 flipMode)
		, m_pRemapLayer(pRemapLayer)
	{
	}

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, ChannelPair* pDataOut) override
	{
		AnimStateEvalParams params;
		params.m_wantRawScale = m_wantRawScale;
		params.m_flipMode = m_flipMode;
		params.m_pRemapLayer = m_pRemapLayer;

		ChannelPair currentPair;
		currentPair.m_evalulatedChannelMask = pInstance->EvaluateChannels(&m_channelId, 1, &currentPair.m_sqt, params);

		m_allEvaluatedChannelsMask |= currentPair.m_evalulatedChannelMask;
		*pDataOut = currentPair;
		return true;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool GetAPReferenceByName(const AnimLayer* pAnimLayer,
						  StringId64 apRefNameId,
						  ndanim::JointParams& sqt,
						  bool blendWithNonContributingAnimations,
						  const ndanim::JointParams* pNeutralSqt,
						  bool wantRawScale,
						  bool useMotionFade,
						  DC::AnimFlipMode flipMode,
						  bool blendWithNonContributingAnimsForReal)
{
	if (!pAnimLayer)
	{
		return false;
	}
	U32F allEvaluatedChannelsMask = 0u;

	ndanim::JointParams neutralSqt;
	if (pNeutralSqt)
	{
		neutralSqt = *pNeutralSqt;
	}
	else
	{
		// If no neutral SQT is provided, use identity (this is in object space, a.k.a. align space).
		neutralSqt.m_scale = Vector(Scalar(1.0f));
		neutralSqt.m_quat = kIdentity;
		neutralSqt.m_trans = kZero;
	}

	sqt = neutralSqt;	// return the neutral pose if no animations contribute an apReference

	if (pAnimLayer->GetType() == kAnimLayerTypeSimple)
	{
		const AnimSimpleLayer* pBaseLayer = static_cast<const AnimSimpleLayer*>(pAnimLayer);
		const AnimSimpleInstance* pInst = pBaseLayer->CurrentInstance();
		if (pInst == nullptr)
			return false;

		const StringId64 alignChanId = apRefNameId;

		ndanim::JointParams currentSqt;
		allEvaluatedChannelsMask = pInst->EvaluateChannels(&alignChanId,
														   1,
														   pInst->GetPhase(),
														   &currentSqt,
														   pInst->IsFlipped(),
														   wantRawScale);
		if (allEvaluatedChannelsMask != 0u)
		{
			sqt = currentSqt;
		}
	}
	else if (pAnimLayer->GetType() == kAnimLayerTypeState)
	{
		const AnimStateLayer* pBaseLayer = static_cast<const AnimStateLayer*>(pAnimLayer);
		AnimInstanceChannelBlender b	 = AnimInstanceChannelBlender(apRefNameId,
																  neutralSqt,
																  wantRawScale,
																  blendWithNonContributingAnimations,
																  useMotionFade,
																  flipMode, false, blendWithNonContributingAnimsForReal);
		ChannelPair initial;
		initial.m_sqt = sqt;
		initial.m_evalulatedChannelMask = 0;
		ChannelPair p = b.BlendForward(pBaseLayer, initial);

		sqt = p.m_sqt;
		allEvaluatedChannelsMask = b.m_allEvaluatedChannelsMask;
	}
	else if (pAnimLayer->GetType() == kAnimLayerTypeCopyRemap)
	{
		const AnimCopyRemapLayer* pRemapLayer = static_cast<const AnimCopyRemapLayer*>(pAnimLayer);
		const AnimStateLayer* pRemappedLayer = pRemapLayer->GetTargetLayer();

		AnimInstanceChannelBlenderWithRemap b = AnimInstanceChannelBlenderWithRemap(apRefNameId,
																					neutralSqt,
																					wantRawScale,
																					blendWithNonContributingAnimations,
																					useMotionFade,
																					flipMode,
																					pRemapLayer);
		ChannelPair initial;
		initial.m_sqt = sqt;
		initial.m_evalulatedChannelMask = 0;
		ChannelPair p = b.BlendForward(pRemappedLayer, initial);

		sqt = p.m_sqt;
		allEvaluatedChannelsMask = b.m_allEvaluatedChannelsMask;
	}

	return (allEvaluatedChannelsMask != 0u);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GetAPReferenceByName(const AnimControl* pAnimControl, StringId64 apRefNameId, Locator& outLoc)
{
	if (!pAnimControl)
		return false;

	I32 numLayers = pAnimControl->GetNumLayers();
	ndanim::JointParams curSqt, newSqt;
	curSqt.m_scale = Vector(Scalar(1.0f));
	curSqt.m_quat = kIdentity;
	curSqt.m_trans = kZero;

	bool anyApFound = false;
	for (I32 i = 0; i < numLayers; i++)
	{
		const AnimLayer* pLayer = pAnimControl->GetLayerByIndex(i);
		if (!pLayer)
			continue;
		
		F32 fade = pLayer->GetCurrentFade();
		if (pLayer->GetCurrentFade() > 0.0f)
		{
			bool found = GetAPReferenceByName(pLayer, apRefNameId, newSqt);

			if (found)
			{
				anyApFound = true;
				AnimChannelJointBlend(&curSqt, curSqt, newSqt, fade);
			}
		}
	}

	outLoc.SetTranslation(curSqt.m_trans);
	outLoc.SetRotation(curSqt.m_quat);

	return anyApFound;
}


/// --------------------------------------------------------------------------------------------------------------- ///
bool GetAPReferenceByNameConditional(const AnimLayer* pAnimLayer,
									StringId64 apRefNameId,
									ndanim::JointParams& sqt,
									bool blendWithNonContributingAnimations,
									const ndanim::JointParams* pNeutralSqt,
									bool wantRawScale,
									bool useMotionFade,
									DC::AnimFlipMode flipMode,
									AnimStateLayerFilterAPRefCallBack filterCallback,
	                                bool disableRetargeting)
{
	if (!pAnimLayer)
	{
		return false;
	}
	U32F allEvaluatedChannelsMask = 0u;

	ndanim::JointParams neutralSqt;
	if (pNeutralSqt)
	{
		neutralSqt = *pNeutralSqt;
	}
	else
	{
		// If no neutral SQT is provided, use identity (this is in object space, a.k.a. align space).
		neutralSqt.m_scale = Vector(Scalar(1.0f));
		neutralSqt.m_quat  = kIdentity;
		neutralSqt.m_trans = kZero;
	}

	sqt = neutralSqt;	// return the neutral pose if no animations contribute an apReference

	if (pAnimLayer->GetType() == kAnimLayerTypeSimple)
	{
		const AnimSimpleLayer* pBaseLayer = static_cast<const AnimSimpleLayer*>(pAnimLayer);
		const AnimSimpleInstance* pInst = pBaseLayer->CurrentInstance();
		if (pInst == nullptr)
			return false;

		const StringId64 alignChanId = apRefNameId;

		ndanim::JointParams currentSqt;
		allEvaluatedChannelsMask = pInst->EvaluateChannels(&alignChanId, 1, pInst->GetPhase(), &currentSqt, pInst->IsFlipped(), wantRawScale);
		if (allEvaluatedChannelsMask != 0u)
		{
			sqt = currentSqt;
		}
	}
	else
	{
		const AnimStateLayer* pBaseLayer = static_cast<const AnimStateLayer*>(pAnimLayer);
		AnimInstanceChannelBlenderConditional b = AnimInstanceChannelBlenderConditional(apRefNameId,
																						neutralSqt,
																						wantRawScale,
																						blendWithNonContributingAnimations,
																						useMotionFade,
																						flipMode,
																						filterCallback,
			                                                                            disableRetargeting);
		ChannelPair initial;
		initial.m_sqt = sqt;
		initial.m_evalulatedChannelMask = 0;
		ChannelPair p = b.BlendForward(pBaseLayer, initial);

		sqt = p.m_sqt;
		allEvaluatedChannelsMask = b.m_allEvaluatedChannelsMask;
	}

	return (allEvaluatedChannelsMask != 0u);
}


/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimHasChannel(const ArtItemAnim* pAnim, const StringId64 channelId)
{
	if (!pAnim || !pAnim->m_pCompressedChannelList || !pAnim->m_pCompressedChannelList->m_channelNameIds)
		return false;

	for (U32F i = 0; i < pAnim->m_pCompressedChannelList->m_numChannels; ++i)
	{
		if (pAnim->m_pCompressedChannelList->m_channelNameIds[i] == channelId)
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimHasChannels(const ArtItemAnim* pAnim, const StringId64* pChannelIdList)
{
	if (pChannelIdList && (pChannelIdList[0] != INVALID_STRING_ID_64))
	{
		const StringId64* pChannelId;
		for (pChannelId = pChannelIdList; *pChannelId != INVALID_STRING_ID_64; ++pChannelId)
		{
			if (!AnimHasChannel(pAnim, *pChannelId))
			{
				return false;
			}
		}

		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawAnimationAlignPath(const AnimControl* pAnimControl,
								 StringId64 anim,
								 const Locator& apRef,
								 bool mirrored,
								 Color color,
								 TimeFrame drawTime,
								 bool drawStartEnd,
								 U32F numSamplePoints)
{
	if (!numSamplePoints)
	{
		return;
	}

	// Get the start (always!)
	Locator start;
	bool ret = FindAlignFromApReference(pAnimControl, anim, 0.0f, apRef, &start, mirrored);

	if (!ret)
	{
		g_prim.Draw(DebugString(apRef.GetTranslation(), "Unable to find align!", kColorRed), drawTime);
		return;
	}

	if (drawStartEnd)
	{
		g_prim.Draw(DebugCross(start.GetTranslation(), 0.25f, 2.0f, color), drawTime);
		g_prim.Draw(DebugString(start.GetTranslation(), DevKitOnly_StringIdToStringOrHex(anim), color), drawTime);

		g_prim.Draw(DebugCoordAxes(apRef, 0.25f), drawTime);
		g_prim.Draw(DebugString(apRef.GetTranslation(), "apRef", kColorWhite), drawTime);
	}

	Point prev = start.GetTranslation();
	--numSamplePoints;
	for (U32F i = 1; i <= numSamplePoints; ++i)
	{
		float t = i * (1.0f / numSamplePoints);
		Locator curLoc;
		ret = FindAlignFromApReference(pAnimControl, anim, t, apRef, &curLoc, mirrored);

		Point cur = curLoc.GetTranslation();
		//g_prim.Draw(DebugCross(cur, 0.1f, 1.0f, color), drawTime);
		g_prim.Draw(DebugLine(prev, cur, color, 3.0f), drawTime);
		prev = cur;
	}

	if (drawStartEnd)
	{
		g_prim.Draw(DebugCross(prev, 0.25f, 2.0f, color), drawTime);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetAlignSpeedAtPhase(const ArtItemAnim* pAnim, SkeletonId skelId, float phase,  AlignDistFunc distFunc)
{
	ndanim::JointParams evaledAlign[2];
	float samplePhases[2];

	const ndanim::ClipData* pClipDataLs = pAnim->m_pClipData;

	float deltaTime = 1.0f/30.0f;
	float deltaPhase = pClipDataLs->m_phasePerFrame*pClipDataLs->m_framesPerSecond*deltaTime;
	samplePhases[0] = Limit01(phase - deltaPhase);
	samplePhases[1] = Limit01(phase + deltaPhase);

	EvaluateChannelParams evaluateParams;
	evaluateParams.m_pAnim = pAnim;
	evaluateParams.m_channelNameId = SID("align");
	evaluateParams.m_phase		   = samplePhases[0];

	for (int i = 0; i < 2; ++i)
	{
		evaluateParams.m_phase = samplePhases[i];
		EvaluateChannelInAnim(skelId, &evaluateParams,&evaledAlign[i]);
	}
	float actualDeltaTime = (samplePhases[1] - samplePhases[0])/(pClipDataLs->m_phasePerFrame*pClipDataLs->m_framesPerSecond);
	return distFunc(evaledAlign[0].m_trans, evaledAlign[1].m_trans)/actualDeltaTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetAlignSpeedAtPhase(const ArtItemAnim* pAnim, SkeletonId skelId, float phase)
{
	return GetAlignSpeedAtPhase(pAnim, skelId, phase, Dist);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector GetAlignVelocityAtPhase(const ArtItemAnim* pAnim, SkeletonId skelId, float phase)
{
	ndanim::JointParams evaledAlign[2];
	float samplePhases[2];

	const ndanim::ClipData* pClipDataLs = pAnim->m_pClipData;

	float deltaTime = 1.0f / 30.0f;
	float deltaPhase = pClipDataLs->m_phasePerFrame*pClipDataLs->m_framesPerSecond*deltaTime;
	samplePhases[0] = Limit01(phase - deltaPhase);
	samplePhases[1] = Limit01(phase + deltaPhase);

	EvaluateChannelParams evaluateParams;
	evaluateParams.m_pAnim = pAnim;
	evaluateParams.m_channelNameId = SID("align");
	evaluateParams.m_phase		   = samplePhases[0];

	for (int i = 0; i < 2; ++i)
	{
		evaluateParams.m_phase = samplePhases[i];
		EvaluateChannelInAnim(skelId, &evaluateParams, &evaledAlign[i]);
	}
	float actualDeltaTime = (samplePhases[1] - samplePhases[0]) / (pClipDataLs->m_phasePerFrame*pClipDataLs->m_framesPerSecond);
	return (evaledAlign[1].m_trans - evaledAlign[0].m_trans) / actualDeltaTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void PrintSkeletonSegments(const ArtItemSkeleton* pSkel)
{
	const ndanim::JointHierarchy* pHierarchy = pSkel->m_pAnimHierarchy;
	
	MsgAnim("\n");
	MsgAnim("Segment Info for skeleton: %s\n", pSkel->GetName());
	for (int iSeg = 0; iSeg < pHierarchy->m_numSegments; iSeg++)
	{
		U32 firstJoint = ndanim::GetFirstJointInSegment(pHierarchy, iSeg);
		U32 numJoints = ndanim::GetNumJointsInSegment(pHierarchy, iSeg);
		MsgAnim("Segment: %i ProcessingGroups: %i\n", iSeg, GetNumProcessingGroupsInSegment(pHierarchy, iSeg));
		for (int iJoint = 0; iJoint < numJoints; iJoint++)
		{
			
			U32F jointIndex = firstJoint + iJoint;
			MsgAnim("    %03i: %s\n", jointIndex, pSkel->m_pJointDescs[jointIndex].m_pName);
		}
	}
	MsgAnim("\n");
	
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugPrintSkeletonSegments(const FgAnimData* pAnimData)
{
	if (const ArtItemSkeleton* pCurSkel = pAnimData->m_curSkelHandle.ToArtItem())
	{
		PrintSkeletonSegments(pCurSkel);
	}

	if (const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem())
	{
		PrintSkeletonSegments(pAnimSkel);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Animation Reload
#if !FINAL_BUILD
AnimReloadPakManager g_animReloadPakMgr;

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimReloadPakManager::Init()
{
	m_pkgs.Init(ResourceTable::AnimationData::kMaxNumOverrideAnims, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void AnimReloadAnimFunc(const ArtItemAnim* pAnim, uintptr_t data)
{
	U32F* pRefCount = (U32F*)data;

	if (ResourceTable::AnimationData::IsOverrideAnim(pAnim))
	{
		if (g_animOptions.m_debugAnimOverrides)
		{
			MsgCon(" -anim: %s\n", pAnim->GetName());
		}

		(*pRefCount)++;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimReloadPakManager::FrameEnd()
{
	if (g_animOptions.m_debugAnimOverrides)
	{
		MsgCon("-----------------------------------------------------------------------------------------\n");
		MsgCon("Animation Overrides: %d / %d\n",
			   ResourceTable::g_pAnimData->m_overrideTable.Size(),
			   ResourceTable::AnimationData::kMaxNumOverrideAnims);
	}

	for (HashTable<Package*, U32F>::Iterator pkgIter = m_pkgs.Begin(); pkgIter != m_pkgs.End(); pkgIter++)
	{
		Package* const pPkg = pkgIter->m_key;
		const char* const pPackageName = pPkg->GetNickName();

		if (g_animOptions.m_debugAnimOverrides)
		{
			MsgCon("-pak: %s\n", pPackageName);
		}

		U32F refCount = 0;
		ResourceTable::ForEachAnimation(AnimReloadAnimFunc, INVALID_SKELETON_ID, (uintptr_t)&refCount);

		pkgIter->m_data = refCount;
		if (!refCount)
		{
			m_pkgs.Erase(pkgIter);
			EngineComponents::GetPackageMgr()->ReleasePackage(pPkg);
		}
	}

	if (g_animOptions.m_debugAnimOverrides)
	{
		MsgCon("-----------------------------------------------------------------------------------------\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimReloadPakManager::AddPackage(const char* const pPackageName)
{
	const StringId64 pkgId = StringToStringId64(pPackageName);
	ASSERT(!EngineComponents::GetPackageMgr()
				->GetPackageByName(pkgId)); // Each animation reload package has an unique name

	Package* const pPkg = EngineComponents::GetPackageMgr()->SyncLoad(pkgId, pPackageName, nullptr);
	if (!pPkg)
	{
		MsgErr("Error loading pkg: %s\n", pPackageName);
		g_commandServer.PrintToScreen("ReloadAnimation errors. See TTY for details.\n", nullptr, kColorRed);

		return;
	}

	m_pkgs.Add(pPkg, 1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ReloadAnimation(const char* const actorName,
							const char* const pkgName,
							const ListArray<const char*>& animNames)
{
	MsgAnim("ReloadAnimation: %s, %s\n", actorName, pkgName);

	// Add this asset to the override asset view, so that the pak file later can be loaded.
	// pkgName -> "<actorName>-anim-reload-<buildId>.pak"
	AssetView::AddOverrideAsset(actorName, AssetType::kActor);

	g_animReloadPakMgr.AddPackage(pkgName);

	for (int i = 0; i < animNames.Size(); i++)
	{
		MsgAnim("Actor: %s, animation: %s reloaded.\n", actorName, animNames[i]);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
REGISTER_RPC_FUNC(bool,
				  ReloadAnimation,
				  (const char* const actorName, const char* const pkgName, const ListArray<const char*>& animNames));

#endif

/// --------------------------------------------------------------------------------------------------------------- ///
static bool AnimBlendTableEntryMatches(const DC::AnimBlendTableEntry& entry,
									   const StringId64 curAnimId,
									   const StringId64 desAnimId)
{
	if ((entry.m_sourceAnim != curAnimId) && (entry.m_sourceAnim != INVALID_STRING_ID_64))
	{
		return false;
	}

	if ((entry.m_destAnim != desAnimId) && (entry.m_destAnim != INVALID_STRING_ID_64))
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::BlendParams* LookupAnimBlendTableEntry(const DC::AnimBlendTable* pBlendTable,
												 const StringId64 curAnimId,
												 const StringId64 desAnimId,
												 bool disableWildcardSource)
{
	if (!pBlendTable)
		return nullptr;

	const DC::BlendParams* pBlend = pBlendTable->m_default;

	for (U32F iEntry = 0; iEntry < pBlendTable->m_count; ++iEntry)
	{
		const DC::AnimBlendTableEntry& entry = pBlendTable->m_entries[iEntry];

		if (disableWildcardSource && (entry.m_sourceAnim == INVALID_STRING_ID_64))
		{
			continue;
		}

		if (AnimBlendTableEntryMatches(entry, curAnimId, desAnimId))
		{
			pBlend = entry.m_blend;
		}
	}

	return pBlend;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 CalculatePoseError(const NdGameObject* pGo,
					   const StringId64 animId,
					   float phase,
					   bool debug,
					   const BoundFrame* pFrame)
{
	float error = 0.0f;

	const StringId64 jointNames[]	= { SID("l_wrist"), SID("r_wrist"), SID("l_ankle"), SID("r_ankle") };
	const StringId64 channelNames[] = { SID("lWrist"), SID("rWrist"), SID("lAnkle"), SID("rAnkle") };

	const I32 kNumJoints = 4;

	if (!pGo || (animId == INVALID_STRING_ID_64))
	{
		return kLargestFloat;
	}

	const AnimControl* pAnimControl = pGo->GetAnimControl();
	const JointCache* pJointCache = pAnimControl->GetJointCache();
	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();

	if (!pAnim)
	{
		return kLargestFloat;
	}

	for (U32F i = 0; i < kNumJoints; ++i)
	{
		StringId64 channelId = channelNames[i];
		StringId64 jointId = jointNames[i];

		const I32F jointIndex = pGo->FindJointIndex(jointId);
		if (jointIndex < 0)
		{
			continue;
		}

		Locator channelLoc;
		bool found = EvaluateChannelInAnimCached(pGo->GetSkeletonId(), pAnim, channelId, phase, false, &channelLoc);
		if (pFrame)
		{
			Locator apRefLoc;
			found = found && EvaluateChannelInAnimCached(pGo->GetSkeletonId(), pAnim, SID("apReference"), phase, false, &apRefLoc);
			
			const Locator channelLocApFrame = apRefLoc.UntransformLocator(channelLoc);
			channelLoc = pFrame->GetLocator().TransformLocator(channelLocApFrame);
		}
		
		if (!found)
			continue;

		Locator rootWs = pGo->GetLocator();
		const Locator &jointWs = pJointCache->GetJointLocatorWs(jointIndex);
		Locator jointRs = rootWs.UntransformLocator(jointWs);

		Scalar dist = Dist(jointRs.GetPosition(), channelLoc.GetTranslation());
		Scalar jointError = dist;

		// Debug drawing!
		if (FALSE_IN_FINAL_BUILD(debug))
		{
			// Draw in world space; bring the possibly mirrored joint back
			const Locator dbgJointWs = rootWs.TransformLocator(jointRs);
			const Locator dbgAnimJointWs = pGo->GetLocator().TransformLocator(channelLoc);

			const Point midpoint = Lerp(dbgJointWs.GetTranslation(), dbgAnimJointWs.GetTranslation(), SCALAR_LC(0.5f));

			DebugPrimTime tt = kPrimDuration1FrameAuto;

			g_prim.Draw(DebugStringFmt(midpoint,
									   kColorYellow,
									   0.5f,
									   "%s %s: %f",
									   DevKitOnly_StringIdToString(animId),
									   DevKitOnly_StringIdToString(channelId),
									   (float)jointError),
						tt);

			g_prim.Draw(DebugCross(dbgJointWs.Pos(), 0.1f, 2.0f, kColorGreen, PrimAttrib(kPrimEnableHiddenLineAlpha)),
						tt);
			g_prim.Draw(DebugCross(dbgAnimJointWs.Pos(), 0.1f, 2.0f, kColorBlue, PrimAttrib(kPrimEnableHiddenLineAlpha)),
						tt);
			g_prim.Draw(DebugLine(dbgJointWs.Pos(),
								  dbgAnimJointWs.Pos(),
								  kColorGreen,
								  kColorBlue,
								  2.0f,
								  PrimAttrib(kPrimEnableHiddenLineAlpha)),
						tt);
		}

		error += jointError;
	}

	return error;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float DistAlignPaths(const DistAlignPathsParams& params,
					 float* pBestSrcPhaseOut /* = nullptr */,
					 float* pBestDstPhaseOut /* = nullptr */)
{
	const ArtItemAnim* pSrcAnim = params.m_pSrcAnim;
	const ArtItemAnim* pDstAnim = params.m_pDstAnim;

	if (!pSrcAnim || !pDstAnim)
	{
		return -1.0f;
	}

	const float srcStartPhase = params.m_srcStartPhase;
	const float srcEndPhase	  = params.m_srcEndPhase;
	const float dstStartPhase = params.m_dstStartPhase;
	const float dstEndPhase	  = params.m_dstEndPhase;

	if (pBestSrcPhaseOut)
		*pBestSrcPhaseOut = kLargeFloat;
	if (pBestDstPhaseOut)
		*pBestDstPhaseOut = kLargeFloat;

	static CONST_EXPR float kBiasDist = 0.001f;

	const U32F numSrcSamples = Min(pSrcAnim->m_pClipData->m_numTotalFrames, uint16_t(params.m_maxSamplesPerAnim));
	const U32F numDstSamples = Min(pDstAnim->m_pClipData->m_numTotalFrames, uint16_t(params.m_maxSamplesPerAnim));

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	Point* aSrcSamples = NDI_NEW Point[numSrcSamples];
	Point* aDstSamples = NDI_NEW Point[numDstSamples];

	const float srcPhaseRange = srcEndPhase - srcStartPhase;
	const float dstPhaseRange = dstEndPhase - dstStartPhase;

	const float invSrcPhase = (numSrcSamples > 1) ? (srcPhaseRange / float(numSrcSamples - 1)) : 0.0f;
	const float invDstPhase = (numDstSamples > 1) ? (dstPhaseRange / float(numDstSamples - 1)) : 0.0f;

	const bool srcLooping = pSrcAnim->IsLooping();
	const bool dstLooping = pDstAnim->IsLooping();

	for (U32F iSrc = 0; iSrc < numSrcSamples; ++iSrc)
	{
		const float srcPhase = Limit01((float(iSrc) * invSrcPhase) + srcStartPhase);

		Locator alignLs;

		if (!FindAlignFromApReference(params.m_skelId, pSrcAnim, srcPhase, params.m_srcApLoc, params.m_srcApId, &alignLs))
		{
			return -1.0f;
		}

		aSrcSamples[iSrc] = alignLs.Pos();
	}

	for (U32F iDst = 0; iDst < numDstSamples; ++iDst)
	{
		const float dstPhase = Limit01((float(iDst) * invDstPhase) + dstStartPhase);

		Locator alignLs;

		if (!FindAlignFromApReference(params.m_skelId, pDstAnim, dstPhase, params.m_dstApLoc, params.m_dstApId, &alignLs))
		{
			return -1.0f;
		}

		aDstSamples[iDst] = alignLs.Pos();
	}

	float bestDist = kLargeFloat;
	float bestSrcPhase = srcEndPhase;
	float bestDstPhase = dstEndPhase;
	Point bestSrcPos = kOrigin;
	Point bestDstPos = kOrigin;

	for (U32F iSrc = 0; iSrc < numSrcSamples; ++iSrc)
	{
		const U32F iSrcNext = srcLooping ? ((iSrc + 1) % numSrcSamples) : Min(iSrc + 1, numSrcSamples - 1);
		
		const Segment srcSegLs = Segment(aSrcSamples[iSrc], aSrcSamples[iSrcNext]);

		const float srcPhase0 = (float(iSrc) * invSrcPhase) + srcStartPhase;
		const float srcPhase1 = (float(iSrcNext) * invSrcPhase) + srcStartPhase;

		for (U32F iDst = 0; iDst < numDstSamples; ++iDst)
		{
			const U32F iDstNext = dstLooping ? ((iDst + 1) % numDstSamples) : Min(iDst + 1, numDstSamples - 1);

			const Segment dstSegLs = Segment(aDstSamples[iDst], aDstSamples[iDstNext]);

			const float dstPhase0 = (float(iDst) * invDstPhase) + dstStartPhase;
			const float dstPhase1 = (float(iDstNext) * invDstPhase) + dstStartPhase;

			Scalar t0, t1;
			const float d = DistSegmentSegment(srcSegLs, dstSegLs, t0, t1);

			if (d < (bestDist - kBiasDist))
			{
				bestDist = d;
				bestSrcPhase = (srcPhase1 >= srcPhase0) ? Lerp(srcPhase0, srcPhase1, float(t0)) : 0.0f;
				bestDstPhase = (dstPhase1 >= dstPhase0) ? Lerp(dstPhase0, dstPhase1, float(t1)) : 0.0f;

				bestSrcPhase = Limit(bestSrcPhase, srcStartPhase, srcEndPhase);
				bestDstPhase = Limit(bestDstPhase, dstStartPhase, dstEndPhase);

				if (FALSE_IN_FINAL_BUILD(params.m_debugDraw))
				{
					bestSrcPos = Lerp(srcSegLs.a, srcSegLs.b, t0);
					bestDstPos = Lerp(dstSegLs.a, dstSegLs.b, t1);
				}
			}
		}
	}

	if (FALSE_IN_FINAL_BUILD(params.m_debugDraw && (bestDist < kLargeFloat)))
	{
		PrimServerWrapper ps = PrimServerWrapper(Locator(kIdentity));
		ps.SetDuration(params.m_debugDrawTime);
		ps.EnableHiddenLineAlpha();

		for (U32F iSrc = 0; iSrc < numSrcSamples - 1; ++iSrc)
		{
			ps.DrawCross(aSrcSamples[iSrc], 0.05f, kColorCyan);
			ps.DrawLine(aSrcSamples[iSrc], aSrcSamples[iSrc + 1], kColorCyan, kColorCyan);
		}
		ps.DrawCross(aSrcSamples[numSrcSamples - 1], 0.05f, kColorCyan);

		for (U32F iDst = 0; iDst < numDstSamples - 1; ++iDst)
		{
			ps.DrawCross(aDstSamples[iDst], 0.05f, kColorMagenta);
			ps.DrawLine(aDstSamples[iDst], aDstSamples[iDst + 1], kColorMagenta, kColorMagenta);
		}
		ps.DrawCross(aDstSamples[numDstSamples - 1], 0.05f, kColorMagenta);

		ps.DrawArrow(bestDstPos, bestSrcPos, 0.2f);
		ps.DrawLine(bestSrcPos + Vector(0.0f, 0.25f, 0.0f), bestSrcPos, kColorWhiteTrans);
		ps.DrawString(bestSrcPos + Vector(0.0f, 0.25f, 0.0f),
					  StringBuilder<256>("%0.2fm\nsrc: %0.3f\ndst: %0.3f", bestDist, bestSrcPhase, bestDstPhase).c_str(),
					  kColorWhiteTrans,
					  0.4f);
	}

	if (pBestSrcPhaseOut)
		*pBestSrcPhaseOut = bestSrcPhase;
	if (pBestDstPhaseOut)
		*pBestDstPhaseOut = bestDstPhase;

	return bestDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector GetAnimVelocityAtPhase(const ArtItemAnim* pAnim, float phase, U32F samplePadding /* = 1 */)
{
	if (!pAnim)
		return kZero;

	if (phase < 0.0f || phase > 1.0f)
		return kZero;

	if (samplePadding == 0)
		return kZero;

	const CompressedChannel* pAlignChannel = FindChannel(pAnim, SID("align"));

	if (!pAlignChannel)
		return kZero;

	const I32F maxSample = I32F(pAnim->m_pClipData->m_numTotalFrames);
	const I32F closestSample = Round(pAnim->m_pClipData->m_fNumFrameIntervals * phase);

	const I32F iStart = Max(closestSample - I32F(samplePadding), 0LL);
	const I32F iEnd = Min(iStart + I32F(samplePadding * 2), maxSample);

	const float phaseStart = float(iStart) * pAnim->m_pClipData->m_phasePerFrame;
	const float phaseEnd = float(iEnd) * pAnim->m_pClipData->m_phasePerFrame;

	const float deltaPhase = phaseEnd - phaseStart;
	const float deltaTime = deltaPhase * GetDuration(pAnim);

	if (deltaTime <= NDI_FLT_EPSILON)
		return kZero;

	ndanim::JointParams alignJp[2];
	ReadFromCompressedChannel(pAlignChannel, static_cast<U16>(iStart), &alignJp[0]);
	ReadFromCompressedChannel(pAlignChannel, static_cast<U16>(iEnd), &alignJp[1]);

	const Vector vel = (alignJp[1].m_trans - alignJp[0].m_trans) / deltaTime;

	return vel;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* GetJointName(const ArtItemSkeleton* pSkel, U32F iJoint, const char* def /* = nullptr */)
{
	if (!pSkel || (iJoint >= pSkel->m_numTotalJoints))
	{
		return def;
	}

	return pSkel->m_pJointDescs[iJoint].m_pName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ValidBitsDiffer(const ArtItemAnim* pAnimA, const ArtItemAnim* pAnimB)
{
	if (!pAnimA || !pAnimB || !pAnimA->m_pClipData || !pAnimB->m_pClipData)
	{
		return false;
	}

	const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(pAnimA->m_skelID).ToArtItem();

	if (!pSkel)
	{
		return false;
	}

	const U32 numProcessingGroups = ndanim::GetNumProcessingGroups(pAnimA->m_pClipData);

	for (U32F iGroup = 0; iGroup < numProcessingGroups; ++iGroup)
	{
		ndanim::ValidBits validBitsA;
		ndanim::ValidBits validBitsB;

		if (const ndanim::ValidBits* pValidBitsA = GetValidBitsArray(pAnimA->m_pClipData, iGroup))
		{
			validBitsA = pValidBitsA[0];
		}
		else
		{
			validBitsA.SetAllBits();
		}

		if (pAnimB->m_skelID != pAnimA->m_skelID)
		{
			validBitsB = GetRetargetedValidBits(pSkel, pAnimB, iGroup);
		}
		else if (const ndanim::ValidBits* pValidBitsB = GetValidBitsArray(pAnimB->m_pClipData, iGroup))
		{
			validBitsB = pValidBitsB[0];
		}
		else
		{
			validBitsB.SetAllBits();
		}

		if (!(validBitsA ^ validBitsB).IsEmpty())
		{
			return true;
		}
	}

	return false;
}


/// --------------------------------------------------------------------------------------------------------------- ///
SegmentMask GetDependentSegmentMask(const ArtItemSkeleton* pSkel, int segmentIndex)
{
	ANIM_ASSERT(pSkel);
	ANIM_ASSERT(segmentIndex < pSkel->m_numSegments && segmentIndex >= 0);

	const OrbisAnim::AnimHierarchySegment* pSegment = OrbisAnim::AnimHierarchy_GetSegment(pSkel->m_pAnimHierarchy, segmentIndex);

	SegmentMask depMask;
	STATIC_ASSERT(sizeof(depMask) == sizeof(pSegment->m_dependencyMask));
	depMask.SetData(pSegment->m_dependencyMask);
	depMask.ClearBitRange(segmentIndex, 63); // we cannot depend on later segments but the tools marked us as such for some yet unknown reason 

	SegmentMask outMask(false);
	outMask.SetBit(segmentIndex);

	for (int i = segmentIndex - 1; i >= 0; i--)
	{
		if (depMask.IsBitSet(i))
		{
			SegmentMask::BitwiseOr(&outMask, outMask, GetDependentSegmentMask(pSkel, i));
		}
	}
	return outMask;
}
