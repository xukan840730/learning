/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-util.h"

#include "corelib/math/mathutils.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-actor.h"
#include "ndlib/anim/anim-align-cache.h"
#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/process/bound-frame.h"
#include "ndlib/process/clock.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"

#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static float DistFloatRange(float x, float a, float b, Scalar& tt)
{
	const float lo = Min(a, b);
	const float hi = Max(a, b);
	const float d = Max(x - hi, lo - x);

	tt = LerpScaleClamp(a, b, 0.0f, 1.0f, x);

	return Max(d, 0.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float DistFromMatchMode(Point_arg a, Point_arg b, const PhaseMatchParams& params)
{
	float dist = kLargeFloat;

	switch (params.m_distMode)
	{
	case PhaseMatchDistMode::k3d:
		dist = Dist(a, b);
		break;
	case PhaseMatchDistMode::kXz:
		dist = DistXz(a, b);
		break;
	case PhaseMatchDistMode::kY:
		dist = Abs(a.Y() - b.Y());
		break;

	case PhaseMatchDistMode::kProjected:
		dist = Abs(Dot(b - a, params.m_projectedBasis));
		break;
	}

	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float DistFromMatchMode(Point_arg p, const PhaseMatchParams& params)
{
	float dist = kLargeFloat;

	switch (params.m_distMode)
	{
	case PhaseMatchDistMode::k3d:
		dist = Length(p);
		break;
	case PhaseMatchDistMode::kXz:
		dist = LengthXz(p);
		break;
	case PhaseMatchDistMode::kY:
		dist = Abs(p.Y());
		break;
	case PhaseMatchDistMode::kProjected:
		dist = Abs(Dot(p - kOrigin, params.m_projectedBasis));
		break;
	}

	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float DistSegmentFromMatchMode(Point_arg p,
									  Point_arg seg0,
									  Point_arg seg1,
									  Scalar* pClosestT,
									  PhaseMatchDistMode distMode)
{
	float dist = kLargeFloat;

	switch (distMode)
	{
	case PhaseMatchDistMode::k3d:
		dist = DistPointSegment(p, seg0, seg1, nullptr, pClosestT);
		break;

	case PhaseMatchDistMode::kXz:
		dist = DistPointSegmentXz(p, seg0, seg1, nullptr, pClosestT);
		break;

	case PhaseMatchDistMode::kY:
		ANIM_ASSERT(pClosestT);
		dist = DistFloatRange(p.Y(), seg0.Y(), seg1.Y(), *pClosestT);
		break;
	}

	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Find out how much (expressed in phase) we need to advance the specified animation to travel 'distance' meters
/// if the current phase is 'currentPhase'
/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToTravelDistance(const ArtItemAnim* pAnim,
								   float currentPhase,
								   float distance,
								   float minAdvanceFrames,
								   float deltaTime,
								   bool slowOnly,
								   AlignDistFunc distFunc)
{
	ANIM_ASSERT(distance < 1000.0f);
	float additionalPhase = 0.0f;
	float newPhase = currentPhase;
	if (pAnim != nullptr)
	{
		const ndanim::ClipData* pClipData = pAnim->m_pClipData;
		float minAdvancePhase = minAdvanceFrames * pClipData->m_phasePerFrame;
		const CompressedChannel* pChannel = FindChannel(pAnim, SID("align"));

		float oldDistance = distance;
		while (distance > 0.0001f)
		{
			newPhase = AdvanceByIntegration(pChannel, distance, newPhase, distFunc);
			if (slowOnly)
			{
				newPhase = Min(newPhase,
							   currentPhase + (pClipData->m_phasePerFrame * pClipData->m_framesPerSecond * deltaTime));
			}

			newPhase = Max(newPhase, currentPhase + minAdvancePhase - additionalPhase);
			if (newPhase >= 1.0f)
			{
				newPhase -= 1.0f;
				additionalPhase += 1.0f;
			}

			// check for an infinite loop
			if (Abs(distance - oldDistance) < 0.00001f)
				break;
			oldDistance = distance;
		}
	}

	return newPhase + additionalPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToTravelDistance(const ArtItemAnim* pAnim,
								   float phase,
								   float distance,
								   float minAdvanceFrames,
								   float deltaTime,
								   bool slowOnly)
{
	return ComputePhaseToTravelDistance(pAnim, phase, distance, minAdvanceFrames, deltaTime, slowOnly, Dist);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Find out the blended delta movement of the ApReference locator since last animation update.
/// --------------------------------------------------------------------------------------------------------------- ///

struct ApDeltaTuple
{
	ApDeltaTuple() { m_delta = Locator(kIdentity); m_frozen = false; }
	Locator m_delta;
	bool m_frozen;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ApDeltaWalker : public AnimStateLayer::InstanceBlender<ApDeltaTuple>
{
protected:
	virtual ApDeltaTuple GetDefaultData() const override { return ApDeltaTuple(); }
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, ApDeltaTuple* pDataOut) override
	{
		if (!(pInstance->GetStateFlags() & DC::kAnimStateFlagApMoveUpdate))
			return false;

		ApDeltaTuple t = GetDefaultData();
		const Locator alignDelta = pInstance->GetChannelDelta(SID("align"));
		const Locator apDelta = pInstance->GetApChannelDelta();

		ndanim::JointParams params[2];
		StringId64 channelNames[] = { SID("align"), pInstance->GetApRefChannelId() /*SID("apReference")*/ };
		pInstance->EvaluateChannels(channelNames, 2, params);
		Locator finalAlign(params[0].m_trans, params[0].m_quat);
		Locator finalApRefAlignSpace(params[1].m_trans, params[1].m_quat);

		Locator initialAlign = finalAlign.TransformLocator(Inverse(alignDelta));
		Locator initialApRefAlignSpace = finalApRefAlignSpace.TransformLocator(Inverse(apDelta));

		Locator initialApRef = initialAlign.TransformLocator(initialApRefAlignSpace);
		Locator finalApRef = finalAlign.TransformLocator(finalApRefAlignSpace);

		Locator combinedDelta  = initialApRef.UntransformLocator(finalApRef);

		t.m_delta = combinedDelta;
		t.m_frozen = pInstance->GetFlags().m_phaseFrozen;
		*pDataOut = t;

		return true;

	}
	virtual ApDeltaTuple BlendData(const ApDeltaTuple& leftData,
								   const ApDeltaTuple& rightData,
								   float masterFade,
								   float animFade,
								   float motionFade) override
	{
		if (leftData.m_frozen)
		{
			return rightData;
		}

		ApDeltaTuple t;
		t.m_delta = Lerp(leftData.m_delta, rightData.m_delta, motionFade);
		t.m_frozen = rightData.m_frozen;
		return t;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void GetAPDelta(const AnimControl* pAnimControl, StringId64 layerId, Locator& rawAnimAPDelta)
{
	ApDeltaWalker walker;
	const AnimStateLayer* pStateLayer = pAnimControl->GetStateLayerById(layerId);
	ApDeltaTuple tuple = walker.BlendForward(pStateLayer, ApDeltaTuple());
	rawAnimAPDelta = tuple.m_delta;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToMatchDistance(SkeletonId skelId,
								  const ArtItemAnim* pAnim,
								  float targetDistance,
								  const PhaseMatchParams& params /* = PhaseMatchParams() */)
{
	PROFILE_AUTO(Animation);

	ANIM_ASSERT(targetDistance < 1000.0f);

	if (pAnim == nullptr)
		return params.m_minPhase;

	EvaluateChannelParams evalParams;
	evalParams.m_pAnim = pAnim;
	evalParams.m_channelNameId = params.m_apChannelId;
	evalParams.m_phase = params.m_maxPhase;
	evalParams.m_mirror = params.m_mirror;

	ndanim::JointParams jp;
	if (!EvaluateChannelInAnim(skelId, &evalParams, &jp))
	{
		return params.m_minPhase;
	}

	float bestPhase = params.m_minPhase;
	float prevPhase = params.m_maxPhase;
	float prevDist = DistFromMatchMode(jp.m_trans, params);

	const float phaseStep = params.m_minAdvanceFrames * pAnim->m_pClipData->m_phasePerFrame;
	float newPhase = params.m_maxPhase - phaseStep;
	float bestDistanceDiff = kLargeFloat;

	if (phaseStep == 0.0f)
		newPhase = params.m_minPhase;

	while (newPhase >= params.m_minPhase)
	{
		evalParams.m_phase = newPhase;

		if (!EvaluateChannelInAnim(skelId, &evalParams, &jp))
		{
			break;
		}

		const float nextDist = DistFromMatchMode(jp.m_trans, params);

		const float bestLegDist = Limit(targetDistance, Min(prevDist, nextDist), Max(prevDist, nextDist));
		const float diff = Abs(bestLegDist - targetDistance);

		if (diff <= bestDistanceDiff)
		{
			bestPhase = LerpScale(prevDist, nextDist, prevPhase, newPhase, bestLegDist);
			bestDistanceDiff = diff;
		}

		if (newPhase == params.m_minPhase)
			break;

		prevPhase = newPhase;
		prevDist = nextDist;
		newPhase -= phaseStep;
		newPhase = Max(params.m_minPhase, newPhase);
	}

	if (params.m_pBestDistOut)
	{
		*params.m_pBestDistOut = bestDistanceDiff;
	}

	return bestPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToMatchDistanceCached(SkeletonId skelId,
										const ArtItemAnim* pAnim,
										float targetDistance,
										const PhaseMatchParams& params /* = PhaseMatchParams() */)
{
	PROFILE_AUTO(Animation);

	ANIM_ASSERT(targetDistance < 1000000.0f);

	const float minPhase = params.m_minPhase;
	const float maxPhase = params.m_maxPhase;

	if (pAnim == nullptr)
		return minPhase;

	if (pAnim->m_pClipData == nullptr)
		return minPhase;

	if (pAnim->m_pClipData->m_numTotalFrames <= 1)
		return minPhase;

	const StringId64 channelId = params.m_apChannelId;

	if (!g_animAlignCache.HasDataForAnim(pAnim, channelId, minPhase, maxPhase))
	{
		return ComputePhaseToMatchDistance(skelId, pAnim, targetDistance, params);
	}

	Locator loc;
	if (!g_animAlignCache.ReadCachedChannel(skelId, pAnim, channelId, maxPhase, false, &loc))
	{
		return minPhase;
	}

	float bestPhase = minPhase;
	float prevPhase = maxPhase;
	float prevDist = DistFromMatchMode(loc.Pos(), params);
	const float phaseStep = params.m_minAdvanceFrames * pAnim->m_pClipData->m_phasePerFrame;
	float newPhase = maxPhase - phaseStep;
	float bestDistanceDiff = kLargeFloat;

	while (newPhase >= minPhase)
	{
		if (!g_animAlignCache.ReadCachedChannel(skelId, pAnim, channelId, newPhase, false, &loc))
			return minPhase;

		const float nextDist = DistFromMatchMode(loc.Pos(), params);
		const float bestLegDist = Limit(targetDistance, Min(prevDist, nextDist), Max(prevDist, nextDist));
		const float diff = Abs(bestLegDist - targetDistance);

		if (diff <= bestDistanceDiff)
		{
			bestPhase = LerpScale(prevDist, nextDist, prevPhase, newPhase, bestLegDist);
			bestDistanceDiff = diff;
		}

		if (newPhase == minPhase)
			break;

		prevPhase = newPhase;
		prevDist = nextDist;
		newPhase -= phaseStep;
		newPhase = Max(minPhase, newPhase);
	}

	return bestPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToMatchApAlignDistance(SkeletonId skelId,
										 const ArtItemAnim* pAnim,
										 const Locator& apLoc,
										 float targetDistance,
										 const PhaseMatchParams& params /* = PhaseMatchParams() */)
{
	PROFILE_AUTO(Animation);

	const float minPhase = params.m_minPhase;
	const float maxPhase = params.m_maxPhase;

	if (pAnim == nullptr)
		return minPhase;

	if (pAnim->m_pClipData == nullptr)
		return minPhase;

	if (pAnim->m_pClipData->m_numTotalFrames <= 1)
		return minPhase;

	Locator align;
	if (!FindAlignFromApReference(skelId, pAnim, params.m_maxPhase, apLoc, params.m_apChannelId, &align, params.m_mirror))
	{
		return params.m_minPhase;
	}

	float bestPhase = params.m_minPhase;
	float prevPhase = params.m_maxPhase;
	float prevDist	= DistFromMatchMode(apLoc.Pos(), align.Pos(), params);

	const float phaseStep = params.m_minAdvanceFrames * pAnim->m_pClipData->m_phasePerFrame;
	float newPhase = params.m_maxPhase - phaseStep;
	float bestDistanceDiff = kLargeFloat;

	if (phaseStep == 0.0f)
		newPhase = params.m_minPhase;

	while (newPhase >= params.m_minPhase)
	{
		if (!FindAlignFromApReference(skelId, pAnim, newPhase, apLoc, params.m_apChannelId, &align, params.m_mirror))
		{
			break;
		}

		const float nextDist = DistFromMatchMode(apLoc.Pos(), align.Pos(), params);

		const float bestLegDist = Limit(targetDistance, Min(prevDist, nextDist), Max(prevDist, nextDist));
		const float diff = Abs(bestLegDist - targetDistance);

		if (diff <= bestDistanceDiff)
		{
			bestPhase = LerpScale(prevDist, nextDist, prevPhase, newPhase, bestLegDist);
			bestDistanceDiff = diff;
		}

		if (newPhase == params.m_minPhase)
			break;

		prevPhase = newPhase;
		prevDist = nextDist;
		newPhase -= phaseStep;
		newPhase = Max(params.m_minPhase, newPhase);
	}

	if (params.m_pBestDistOut)
	{
		*params.m_pBestDistOut = bestDistanceDiff;
	}

	return bestPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToMatchDistanceFromEnd(const SkeletonId& skelId,
										 const ArtItemAnim* pAnim,
										 float targetDistance,
										 const PhaseMatchParams& params /* = PhaseMatchParams() */)
{
	const float minPhase = params.m_minPhase;
	const float maxPhase = params.m_maxPhase;

	if (pAnim == nullptr)
		return minPhase;

	g_animAlignCache.TryCacheAnim(pAnim, SID("align"), minPhase, 1.0f);

	Locator jp;

	if (!EvaluateChannelInAnimCached(skelId, pAnim, SID("align"), 1.0f, params.m_mirror, &jp))
	{
		return minPhase;
	}

	const float endDist = DistFromMatchMode(jp.Pos(), params);

	if (!EvaluateChannelInAnimCached(skelId, pAnim, SID("align"), maxPhase, params.m_mirror, &jp))
	{
		return minPhase;
	}

	float bestPhase = minPhase;
	float prevPhase = maxPhase;
	float prevDist = endDist - DistFromMatchMode(jp.Pos(), params);
	const float phaseStep = params.m_minAdvanceFrames * pAnim->m_pClipData->m_phasePerFrame;
	float newPhase = maxPhase - phaseStep;
	float bestDistanceDiff = kLargeFloat;

	while (newPhase >= minPhase)
	{
		if (!EvaluateChannelInAnimCached(skelId, pAnim, SID("align"), newPhase, params.m_mirror, &jp))
		{
			break;
		}

		const float nextDist = endDist - DistFromMatchMode(jp.Pos(), params);
		const float bestLegDist = Limit(targetDistance, Min(prevDist, nextDist), Max(prevDist, nextDist));
		const float diff = Abs(bestLegDist - targetDistance);

		if (diff <= bestDistanceDiff)
		{
			bestPhase = LerpScale(prevDist, nextDist, prevPhase, newPhase, bestLegDist);
			bestDistanceDiff = diff;
		}

		if (newPhase == minPhase)
			break;

		prevPhase = newPhase;
		prevDist = nextDist;
		newPhase -= phaseStep;
		newPhase = Max(minPhase, newPhase);
	}

	return bestPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToMatchApAlign(SkeletonId skelId,
								 const ArtItemAnim* pAnim,
								 const Locator& desAlign,
								 const Locator& apRef,
								 const PhaseMatchParams& params /* = PhaseMatchParams() */)
{
	if (pAnim == nullptr)
		return params.m_minPhase;

	Locator prevAlign, align;
	if (!FindAlignFromApReference(skelId,
								  pAnim,
								  params.m_maxPhase,
								  apRef,
								  params.m_apChannelId,
								  &prevAlign,
								  params.m_mirror))
	{
		return params.m_minPhase;
	}

	float bestPhase = params.m_maxPhase;
	float bestDist = DistFromMatchMode(prevAlign.Pos(), desAlign.Pos(), params);

	const float phaseStep = params.m_minAdvanceFrames * pAnim->m_pClipData->m_phasePerFrame;
	float prevPhase = params.m_maxPhase;
	float newPhase = params.m_maxPhase - phaseStep;

	while (newPhase >= params.m_minPhase)
	{
		if (!FindAlignFromApReference(skelId, pAnim, newPhase, apRef, params.m_apChannelId, &align, params.m_mirror))
		{
			break;
		}

		Scalar tt;
		const float thisDist = DistSegmentFromMatchMode(desAlign.Pos(),
														prevAlign.Pos(),
														align.Pos(),
														&tt,
														params.m_distMode);

		if (thisDist <= bestDist)
		{
			bestPhase = Lerp(prevPhase, newPhase, (float)tt);
			bestDist = thisDist;
		}

		if (newPhase == params.m_minPhase)
			break;

		prevPhase = newPhase;
		newPhase -= phaseStep;
		newPhase = Max(params.m_minPhase, newPhase);
		prevAlign = align;
	}

	if (params.m_pBestDistOut)
	{
		*params.m_pBestDistOut = bestDist;
	}

	return bestPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToNoAlignMovement(const ArtItemAnim* pAnim, const PhaseMatchParams& params /* = PhaseMatchParams() */)
{
	PROFILE_AUTO(Animation);

	if (pAnim == nullptr)
		return params.m_minPhase;

	EvaluateChannelParams evalParams;
	evalParams.m_pAnim		   = pAnim;
	evalParams.m_channelNameId = params.m_apChannelId;
	evalParams.m_phase		   = params.m_maxPhase;
	evalParams.m_mirror		   = params.m_mirror;

	ndanim::JointParams jp;
	const SkeletonId skelId = pAnim->m_skelID;
	EvaluateChannelInAnim(skelId, &evalParams, &jp);

	float bestPhase = params.m_maxPhase;
	float prevPhase = params.m_minPhase;
	float prevDist = DistFromMatchMode(jp.m_trans, params);

	const float phaseStep = params.m_minAdvanceFrames * pAnim->m_pClipData->m_phasePerFrame;
	float newPhase = params.m_minPhase;
	
	while (newPhase < params.m_maxPhase)
	{
		evalParams.m_phase = newPhase;
		EvaluateChannelInAnim(skelId, &evalParams, &jp);
		
		const float nextDist = DistFromMatchMode(jp.m_trans, params);

		if (nextDist == prevDist)
		{
			bestPhase = newPhase;
			break;
		}
		
		prevPhase = newPhase;
		newPhase += phaseStep;
		newPhase = Min(params.m_maxPhase, newPhase);
	}

	return bestPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CalculateAnimDistance(const ArtItemAnim* pAnim, F32 startPhase, F32 endPhase)
{
	PROFILE_AUTO(Animation);

	if (pAnim == nullptr)
		return 0.0f;

	StringId64 channelId = SID("apReference");

	EvaluateChannelParams evalParams;
	evalParams.m_pAnim		   = pAnim;
	evalParams.m_channelNameId = channelId;
	evalParams.m_phase		   = startPhase;
	ndanim::JointParams jp;
	const SkeletonId skelId = pAnim->m_skelID;
	EvaluateChannelInAnim(skelId, &evalParams, &jp);

	Point startPos = jp.m_trans;

	evalParams.m_phase = endPhase;
	EvaluateChannelInAnim(skelId, &evalParams, &jp);

	Point endPos = jp.m_trans;

	return DistXz(endPos, startPos);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GetRotatedApRefForEntry(SkeletonId skelId,
							 const ArtItemAnim* pAnim,
							 const Locator curLocWs,
							 const BoundFrame& defaultCoverApRef,
							 const Locator& defaultAlignWs,
							 Locator* pRotatedAlignWsOut,
							 BoundFrame* pRotatedApRefOut)
{
	const Point apRefPosWs = defaultCoverApRef.GetTranslationWs();

	Vector toDefaultAlignWs = defaultAlignWs.Pos() - apRefPosWs;
	Vector toCurrentAlignWs = curLocWs.Pos() - apRefPosWs;

	toDefaultAlignWs.SetY(0);
	toCurrentAlignWs.SetY(0);

	const Vector normToDefaultPs = SafeNormalize(toDefaultAlignWs, kUnitZAxis);
	const Vector normToCurrentPs = SafeNormalize(toCurrentAlignWs, kUnitZAxis);
	const Scalar dotP = Dot(normToDefaultPs, normToCurrentPs);

	float angleDiffRad = SafeAcos(dotP);
	if (IsNegative(Dot(Cross(kUnitYAxis, normToDefaultPs), normToCurrentPs)))
		angleDiffRad = -angleDiffRad;

	BoundFrame rotatedApRef = defaultCoverApRef;
	const Quat rotAdjustmentWs = Quat(kUnitYAxis, angleDiffRad);
	rotatedApRef.AdjustRotationWs(rotAdjustmentWs);

	if (pRotatedApRefOut)
		*pRotatedApRefOut = rotatedApRef;

	const Locator defaultCoverApRefPs = defaultCoverApRef.GetLocatorPs();
	const Locator rotatedCoverApRefPs = rotatedApRef.GetLocatorPs();

	const Locator defaultAlignLs = defaultCoverApRefPs.UntransformLocator(defaultAlignWs);
	const Locator rotatedAlignWs = rotatedCoverApRefPs.TransformLocator(defaultAlignLs);

	if (pRotatedAlignWsOut)
		*pRotatedAlignWsOut = rotatedAlignWs;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CalculateAnimPhaseFromStartTime(const ArtItemAnim* pArtItem,
									  const TimeFrame startTime,
									  const Clock* pClock)
{
	if (!pArtItem)
		return -1.0f;

	if (!pClock)
		return -1.0f;

	const float animDuration = GetDuration(pArtItem);

	if (animDuration <= 0.0f)
		return -1.0f;

	const TimeFrame curTime = pClock->GetCurTime();
	if (curTime <= startTime)
		// startTime is in the future, for now just return starting phase of 0
		return 0.0f;

	const TimeFrame timePassed = pClock->GetTimePassed(startTime);
	const float timePassedSec = ToSeconds(timePassed);
	const float startPhase = timePassedSec / animDuration;

	if (startPhase > 1.0f)
		return 1.0f;

	return startPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool DeparentAllApReferencesCallback(AnimStateInstance* pInstance, AnimStateLayer* pStateLayer, uintptr_t userData)
{
	if (pInstance)
	{
		const BoundFrame apRef = pInstance->GetApLocator();		
		pInstance->SetApLocator(BoundFrame(apRef.GetLocator(), Binding()));
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DeparentAllApReferences(AnimStateLayer* pStateLayer)
{
	pStateLayer->WalkInstancesNewToOld(DeparentAllApReferencesCallback, 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float FindEffPhase(const ArtItemAnim* pArtItemAnim,
				   StringId64 effNameId,
				   float defaultPhase /* = -1.0f */,
				   bool* pFoundOut /* = nullptr */)
{
	float phase = defaultPhase;
	bool found = false;

	const EffectAnim* pEffectAnim = pArtItemAnim ? pArtItemAnim->m_pEffectAnim : nullptr;
	if (pEffectAnim)
	{
		for (U32F ii = 0; ii < pEffectAnim->m_numEffects; ++ii)
		{
			const EffectAnimEntry& effectEntry = pEffectAnim->m_pEffects[ii];

			if (effectEntry.GetNameId() == effNameId)
			{
				phase = GetPhaseFromClipFrame(pArtItemAnim->m_pClipData, effectEntry.GetFrame());
				found = true;
				break;
			}
		}
	}

	if (pFoundOut)
		*pFoundOut = found;

	return phase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float FindEffDuration(const ArtItemAnim* pArtItemAnim, StringId64 effStartId, StringId64 effStopId)
{
	STRIP_IN_FINAL_BUILD_VALUE(0.0f);

	float effDuration = -1.0f;

	if (!pArtItemAnim)
		return effDuration;

	if (const EffectAnim* pEffectAnim = pArtItemAnim->m_pEffectAnim)
	{
		float startFrame = 0.0f;
		float stopFrame = 0.0f;

		for (U32F kk = 0; kk < pEffectAnim->m_numEffects; ++kk)
		{
			const EffectAnimEntry& effectEntry = pEffectAnim->m_pEffects[kk];
			if (effectEntry.GetNameId() == effStartId)
				startFrame = effectEntry.GetFrame();
			else if (effectEntry.GetNameId() == effStopId)
				stopFrame = effectEntry.GetFrame();
		}

		effDuration = (stopFrame - startFrame) / 30.0f;
	}

	return effDuration;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool OverlayChangedAnimState(const AnimStateLayer* pLayer, const AnimOverlays* pOverlays)
{
	bool animsChanged = false;
	if (pLayer->CurrentStateInstance() != nullptr)
	{	
		const StringId64 currentAnimName = pLayer->CurrentStateInstance()->GetAnimStateSnapshot().m_translatedPhaseAnimName;
		const StringId64 newAnimName = pOverlays->LookupTransformedAnimId(pLayer->CurrentState()->m_phaseAnimName);
		if (currentAnimName !=newAnimName)
			animsChanged = true;
	}
	return animsChanged;	
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle GetPhaseAnimFromStateId(const AnimControl* pAnimControl, StringId64 stateId)
{
	return GetPhaseAnimLookupFromStateId(pAnimControl, stateId).GetAnim();
}

/// --------------------------------------------------------------------------------------------------------------- ///
CachedAnimLookup GetPhaseAnimLookupFromStateId(const AnimControl* pAnimControl,
											   StringId64 stateId,
											   CachedAnimLookup* pPrevCache /*= nullptr*/,
											   const DC::AnimState** ppState /*= nullptr*/)
{
	const DC::AnimState* pAnimState = nullptr;
	if (ppState != nullptr && *ppState != nullptr)
	{
		if ((*ppState)->m_name.m_symbol == stateId)
		{
			pAnimState = *ppState;
		}
	}
	if (pAnimState == nullptr)
	{
		pAnimState = AnimActorFindState(pAnimControl->GetActor(), stateId);
		if (ppState != nullptr)
		{
			*ppState = pAnimState;
		}
	}

	CachedAnimLookup lookup;
	if (pPrevCache == nullptr)
		pPrevCache = &lookup;
	if (pAnimState)
		pPrevCache->SetSourceId(pAnimState->m_phaseAnimName);
	return pAnimControl->LookupAnimCached(*pPrevCache);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawAnimPose(const Locator& alignLoc, const ArtItemAnim* pAnim, float phase, bool mirrored, Color color)
{
	STRIP_IN_FINAL_BUILD;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	const ArtItemSkeleton *pSkeleton = ResourceTable::LookupSkel(pAnim->m_skelID).ToArtItem();

	float sample = phase/pAnim->m_pClipData->m_phasePerFrame;

	const U32F numTotalJoints = pSkeleton->m_numGameplayJoints;
	Transform* pOriginalTransforms = NDI_NEW Transform[numTotalJoints];

	const U32F numAnimatedJoints = pSkeleton->m_numAnimatedGameplayJoints;
	ndanim::JointParams* pOriginalJointParams = NDI_NEW ndanim::JointParams[numAnimatedJoints];

	// NOTE: We currently don't require any input controls to be driven by the game... if there are any input
	// controls, they are always filled in by animation plug-ins, not by the game code. So uninitialized is OK.
	const U32F numInputControls = pSkeleton->m_pAnimHierarchy->m_numInputControls;
	float* pOriginalInputControls = numInputControls > 0 ? NDI_NEW float[numInputControls] : nullptr;

	const U32F numOutputControls = pSkeleton->m_pAnimHierarchy->m_numOutputControls;
	float* pOriginalOutputControls = numOutputControls > 0 ? NDI_NEW float[numOutputControls] : nullptr;

	AnimateFlags flags = mirrored ? AnimateFlags::kAnimateFlag_Mirror : AnimateFlags::kAnimateFlag_None;
	AnimateObject(alignLoc.AsTransform(),
				  pSkeleton,
				  pAnim,
				  sample,
				  pOriginalTransforms,
				  pOriginalJointParams,
				  pOriginalInputControls,
				  pOriginalOutputControls,
				  flags);

	const ndanim::DebugJointParentInfo* pParentInfo = ndanim::GetDebugJointParentInfoTable(pSkeleton->m_pAnimHierarchy);

	for (int i = 0; i < numAnimatedJoints; ++ i)
	{
		const char* name = pSkeleton->m_pJointDescs[i].m_pName;

		if (strstr(name, "lips") ||
			strstr(name, "eye") ||
			strstr(name, "cheek") ||
			strstr(name, "brow") ||
			strstr(name, "neck") ||
			strstr(name, "nose") ||
			strstr(name, "chin") ||
			strstr(name, "holster"))
			continue;
		
		if (pParentInfo[i].m_parent >= 0)
		{
			Locator loc(pOriginalTransforms[i]);
			Locator locParent(pOriginalTransforms[pParentInfo[i].m_parent]);
			g_prim.Draw(DebugLine(loc.GetTranslation(), locParent.GetTranslation(), color), kPrimDuration1FramePauseable);
		}
		//MsgOut("[%d] %s\n", i, name);

		//Locator loc(pOriginalJointParams[i].m_trans, pOriginalJointParams[i].m_quat);
		Locator loc(pOriginalTransforms[i]);
		g_prim.Draw(DebugCoordAxes(loc, 0.05f), kPrimDuration1FramePauseable);
	}
}


/************************************************************************/
/* AnimChannelLocatorBlender                                            */
/************************************************************************/
AnimChannelLocatorBlender::AnimChannelLocatorBlender(const StringId64 channelId,
													 Locator defaultLoc /* = Locator(kIdentity) */,
													 bool forceChannelBlending /* = false */)
	: m_channelId(channelId), m_defaultLoc(defaultLoc), m_forceChannelBlending(forceChannelBlending)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AnimChannelLocatorBlender::GetDefaultData() const
{
	return m_defaultLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimChannelLocatorBlender::GetDataForInstance(const AnimStateInstance* pInstance, Locator* pDataOut)
{
	if (!pInstance || !pDataOut)
		return false;

	AnimStateEvalParams params;
	params.m_forceChannelBlending = m_forceChannelBlending;

	ndanim::JointParams jp;
	if (0 == pInstance->EvaluateChannels(&m_channelId, 1, &jp, params))
		return false;

	Point tweakedPos = jp.m_trans;

	if (pInstance->IsAnimDeltaTweakEnabled())
	{
		tweakedPos = (jp.m_trans - kOrigin) * pInstance->GetAnimDeltaTweakTransform() + Point(kOrigin);
	}

	*pDataOut = Locator(tweakedPos, jp.m_quat);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AnimChannelLocatorBlender::BlendData(const Locator& leftData,
											 const Locator& rightData,
											 float masterFade,
											 float animFade,
											 float motionFade)
{
	switch (m_channelId.GetValue())
	{
	case SID_VAL("align"):
	case SID_VAL("apReference"):
		return Lerp(leftData, rightData, motionFade);
	}

	return Lerp(leftData, rightData, animFade);
}

/************************************************************************/
/* AnimChannelDeltaBlender                                              */
/************************************************************************/
AnimChannelDeltaBlender::AnimChannelDeltaBlender(StringId64 channelId)
{
	m_channelId = (channelId == INVALID_STRING_ID_64) ? SID("align") : channelId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AnimChannelDeltaBlender::GetDefaultData() const
{
	return Locator(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimChannelDeltaBlender::GetDataForInstance(const AnimStateInstance* pInstance, Locator* pDataOut)
{
	if (m_channelId == SID("align"))
	{
		*pDataOut = pInstance->GetChannelDelta(m_channelId);
	}
	else
	{
		// Channels other than the align output movement relative to the align
		Locator alignDelta = pInstance->GetChannelDelta(SID("align"));
		Locator channelDelta = pInstance->GetChannelDelta(m_channelId);

		Locator channelDeltaWs = alignDelta.TransformLocator(channelDelta);
		
		*pDataOut = channelDeltaWs;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AnimChannelDeltaBlender::BlendData(const Locator& leftData,
										   const Locator& rightData,
										   float masterFade,
										   float animFade,
										   float motionFade)
{
	return Lerp(leftData, rightData, motionFade);
}

/************************************************************************/
/* AnimChannelFromApReferenceBlender                                    */
/************************************************************************/
AnimChannelFromApReferenceBlender::AnimChannelFromApReferenceBlender(const StringId64 channelId)
{
	m_channelId = channelId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Locator> AnimChannelFromApReferenceBlender::GetDefaultData() const
{
	return MAYBE::kNothing;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimChannelFromApReferenceBlender::GetDataForInstance(const AnimStateInstance* pInstance, Maybe<Locator>* pDataOut)
{
	if (!pInstance || !pDataOut)
		return false;

	StringId64 channelIds[2];
	channelIds[0] = SID("apReference");
	channelIds[1] = m_channelId;

	Locator apLocWs = pInstance->GetApLocator().GetLocator();

	AnimStateEvalParams params;
	params.m_flipMode = m_flipMode;

	ndanim::JointParams jp[2];
	U32 validChannels = pInstance->EvaluateChannels(channelIds, 2, jp, params);
	if (validChannels != 3)
		return false;

	Locator apRefLoc   = Locator(jp[0].m_trans, jp[0].m_quat);
	Locator channelLoc = Locator(jp[1].m_trans, jp[1].m_quat);

	Locator channelApDelta = apRefLoc.UntransformLocator(channelLoc);
	Locator channelLocWs   = apLocWs.TransformLocator(channelApDelta);

	*pDataOut = channelLocWs;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Locator> AnimChannelFromApReferenceBlender::BlendData(const Maybe<Locator>& leftData,
															const Maybe<Locator>& rightData,
															float masterFade,
															float animFade,
															float motionFade)
{
	if (!leftData.Valid())
		return rightData;
	else if (!rightData.Valid())
		return leftData;
	else
		return Lerp(leftData.Get(), rightData.Get(), animFade);
}
