/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/anim/motion-matching/motion-matching.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/nd-anim-util.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-anim.h"

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingOptions g_motionMatchingOptions;

/// --------------------------------------------------------------------------------------------------------------- ///
static Locator GetLocatorDelta(const ArtItemAnim* pAnim, float phase, bool mirror, float* pYawSpeedOut)
{
	float dt = pAnim->m_pClipData->m_secondsPerFrame;
	float nextPhaseRaw = phase + pAnim->m_pClipData->m_phasePerFrame;

	if (nextPhaseRaw > 1.0f)
	{
		nextPhaseRaw = phase - pAnim->m_pClipData->m_phasePerFrame;
		dt = -dt;
	}

	if (nextPhaseRaw < 0.0f)
	{
		return kIdentity;
	}

	bool valid = true;
	Locator align[2];
	valid = valid && EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), phase, &align[0], mirror);
	valid = valid && EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), nextPhaseRaw, &align[1], mirror);

	if (!valid)
	{
		return kIdentity;
	}

	const Locator deltaLoc = align[0].UntransformLocator(align[1]);

	if (pYawSpeedOut)
	{
		const Vector deltaDirXz = AsUnitVectorXz(GetLocalZ(deltaLoc), kUnitZAxis);
		const float deltaYaw = Atan2(deltaDirXz.X(), deltaDirXz.Z());
		const float yawSpeed = (Abs(dt) > 0.0f) ? (deltaYaw / dt) : 0.0f;
		*pYawSpeedOut = yawSpeed;
	}

	return deltaLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vector ComputeAlignVel(const ArtItemAnim* pAnim, float phase, bool mirror, const Locator& deltaLoc)
{
	Locator alignVel = kIdentity;
	bool alignVelValid = false;
	
	if (pAnim)
	{
		alignVelValid = EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("alignVel"), phase, &alignVel, mirror);
	}

	if (alignVelValid)
	{
		return AsVector(alignVel.GetTranslation());
	}

	float dt = pAnim->m_pClipData->m_secondsPerFrame;
	float nextPhaseRaw = phase + pAnim->m_pClipData->m_phasePerFrame;

	if (nextPhaseRaw > 1.0f)
	{
		nextPhaseRaw = phase - pAnim->m_pClipData->m_phasePerFrame;
		dt = -dt;
	}

	if (nextPhaseRaw < 0.0f)
	{
		return kZero;
	}

	const Vector alignVelVec = (Abs(dt) > 0.0f) ? (AsVector(deltaLoc.Pos()) / dt) : kZero;
	return alignVelVec;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<MMLocomotionState> ComputeLocomotionStateInFuture(const AnimSample& animSample,
														float timeInFutureSec,
														float stoppingFaceDist)
{
	PROFILE_AUTO(Animation);	
	const ArtItemAnim* pAnim = animSample.Anim().ToArtItem();
	if (!pAnim)
	{
		return MAYBE::kNothing;
	}

	const float phasePerSecond = (pAnim->m_pClipData->m_phasePerFrame / pAnim->m_pClipData->m_secondsPerFrame)
								 * animSample.Rate();
	const float nextPhaseRaw = animSample.Phase() + (timeInFutureSec * phasePerSecond);
	const bool isLooping	 = (pAnim->m_flags & ArtItemAnim::kLooping);
	const bool mirrored		 = animSample.Mirror();

	float nextPhase = nextPhaseRaw;
	if (nextPhase > 1.0f && nextPhase < 1.00001f && !isLooping)
	{
		nextPhase = 1.0f;
	}

	AnimSample futureSample(animSample.Anim(), nextPhase);

	Locator aligns[2];

	if (!EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), animSample.Phase(), &aligns[0], mirrored))
	{
		return MAYBE::kNothing;
	}

	Locator extraTransform(kIdentity);

	float futurePhase = futureSample.Phase();
	while (futurePhase > 1.0f)
	{
		if (isLooping)
		{
			Locator lastAlign;
			if (!EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), 1.0f, &lastAlign, mirrored))
			{
				return MAYBE::kNothing;
			}

			extraTransform = lastAlign;
			futurePhase -= 1.0f;
		}
		else
		{
			futurePhase = 1.0f;
			return MAYBE::kNothing;
		}
	}

	// We dont really support going in the past beyond the first frame
	if (futurePhase < 0.0f)
	{
		return MAYBE::kNothing;
	}

	if (!EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), futurePhase, &aligns[1], mirrored))
	{
		return MAYBE::kNothing;
	}
	
	float yawSpeed = 0.0f;
	const Locator futureDelta = GetLocatorDelta(pAnim, futurePhase, mirrored, &yawSpeed);

	Locator futureAlignOs = aligns[0].UntransformLocator(extraTransform.TransformLocator(aligns[1]));

	// (JDB) seeing locators come out of this function where the quaternion passes the normalization test, but the
	// results of GetLocalZ() of said locator do not. 
	futureAlignOs.SetRot(Normalize(futureAlignOs.Rot()));

	//Locator futureAlignVel;
	//bool alignVelValid = EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("alignVel"), futurePhase, &futureAlignVel, mirrored);
	const Vector velAlignSpace = ComputeAlignVel(pAnim, futurePhase, mirrored, futureDelta);
	const Vector velOs = futureAlignOs.TransformVector(velAlignSpace);

	Locator strafeDirLoc;
	const bool dirChannel = EvaluateChannelInAnim(pAnim->m_skelID,
												  pAnim,
												  SID("apReference-strafe-dir"),
												  futurePhase,
												  &strafeDirLoc,
												  mirrored);

	Locator goalLoc;
	const bool goalChannel = EvaluateChannelInAnim(pAnim->m_skelID,
												   pAnim,
												   SID("apReference-goal"),
												   futurePhase,
												   &goalLoc,
												   mirrored);

	Vector facingDirOs = dirChannel ? GetLocalZ(futureAlignOs.TransformLocator(strafeDirLoc))
									: GetLocalZ(futureAlignOs);

	if (goalChannel && (Dist(goalLoc.Pos(), kOrigin) < stoppingFaceDist))
	{
		facingDirOs = GetLocalZ(futureAlignOs.TransformLocator(goalLoc));
	}

	Locator pathLocOs;
	const bool pathChannel = EvaluateChannelInAnim(pAnim->m_skelID,
												   pAnim,
												   SID("apReference-path"),
												   futurePhase,
												   &pathLocOs,
												   mirrored);

	const Point pathPosOs = pathChannel ? futureAlignOs.TransformPoint(pathLocOs.GetTranslation())
										: futureAlignOs.Pos();

	MMLocomotionState ret;
	ret.m_alignOs	  = futureAlignOs;
	ret.m_velocityOs  = velOs;
	ret.m_strafeDirOs = facingDirOs;
	ret.m_pathPosOs	  = pathPosOs;
	ret.m_yawSpeed	  = yawSpeed;

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimSample::GetAnimNameId() const
{
	StringId64 res = INVALID_STRING_ID_64;
	if (const ArtItemAnim* pAnim = m_animHandle.ToArtItem())
	{
		res = pAnim->GetNameId();
	}
	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimSample::Frame() const
{
	const ArtItemAnim* pAnim = Anim().ToArtItem();
	if (!pAnim)
	{
		return 0.0f;
	}

	// NB: we don't use pClipData->m_numTotalFrames as this is really m_numTotalSAMPLES and is one more than the number of frame intervals
	const float maxFrameSample		 = pAnim->m_pClipData->m_fNumFrameIntervals;
	const float mayaFramesCompensate = 30.0f * pAnim->m_pClipData->m_secondsPerFrame;
	return Round(m_phase * maxFrameSample * mayaFramesCompensate * 1200.0f) / 1200.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimSample::Sample() const
{
	const ArtItemAnim* pAnim = Anim().ToArtItem();
	if (!pAnim)
	{
		return 0.0f;
	}

	// NB: we don't use pClipData->m_numTotalFrames as this is really m_numTotalSAMPLES and is one more than the number of frame intervals
	const float maxFrameSample = pAnim->m_pClipData->m_fNumFrameIntervals;
	return Round(maxFrameSample * m_phase * 1200.0f) / 1200.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimSample::Advance(TimeFrame time)
{
	const ArtItemAnim* pAnim = Anim().ToArtItem();
	if (!pAnim)
	{
		return 0;
	}

	const bool looping = pAnim->m_flags & ArtItemAnim::kLooping;
	const float prevPhase = m_phase;

	U32F loopCount = 0;

	m_phase += time.ToSeconds() * pAnim->m_pClipData->m_phasePerFrame * pAnim->m_pClipData->m_framesPerSecond * m_rate;

	if (m_phase > 1.0f)
	{
		if (looping)
		{
			while (m_phase > 1.0f)
			{
				++loopCount;
				m_phase -= 1.0f;
			}
		}
		else
		{
			m_phase = 1.0f;
		}
	}
	
	while (m_phase < 0.0f)
	{
		if (looping)
		{
			++loopCount;
			m_phase += 1.0f;
		}
		else
		{
			m_phase = 0.0f;
		}
	}

	ANIM_ASSERTF(m_phase >= 0.0f && m_phase <= 1.0f,
				 ("Advance phase failed: %f (prev: %f dt: %f rate: %f anim: '%s')",
				  m_phase,
				  prevPhase,
				  time.ToSeconds(),
				  m_rate,
				  pAnim->GetName()));

	return loopCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSample::RoundToNearestFrame()
{
	const ArtItemAnim* pAnim = Anim().ToArtItem();
	if (!pAnim)
	{
		return;
	}

	// Round to the nearest sampled frame.
	// NB: we don't use pClipData->m_numTotalFrames as this is really m_numTotalSAMPLES and is one more than the number of frame intervals
	const float maxFrameSample = pAnim->m_pClipData->m_fNumFrameIntervals;

	if (maxFrameSample > 0.0f)
	{
		const float prevPhase = m_phase;

		const float frame		 = m_phase * maxFrameSample;
		const float clampedFrame = Round(frame);

		m_phase = clampedFrame / maxFrameSample;

		ANIM_ASSERTF(m_phase >= 0.0f && m_phase <= 1.0f,
					 ("RoundToNearestFrame failed: %f (clamped: %f max: %f anim: '%s')",
					  m_phase,
					  prevPhase,
					  clampedFrame,
					  maxFrameSample,
					  pAnim->GetName()));
	}
	else
	{
		m_phase = 0.0f;
	}
}
