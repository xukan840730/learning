/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-dummy-instance.h"

#include "corelib/memory/memory-map.h"
#include "corelib/system/endian.h"
#include "corelib/util/bit-array.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/nd-frame-state.h"

#include <orbisanim/animclip.h>

/// --------------------------------------------------------------------------------------------------------------- ///
static const size_t kMaxAnimDummyInstancePoolCapacity = 256;
static size_t s_animDummyInstancePoolCapacity;
static AnimDummyInstance* s_aAnimDummyInstancePool;
static BitArray<kMaxAnimDummyInstancePoolCapacity> s_animDummyInstancePoolAllocBits;
static NdAtomicLock s_animDummyInstancePoolLock;

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimDummyInstance::StartUp()
{
	size_t availBytes = Memory::GetSize(ALLOCATION_ANIM_DUMMY_INSTANCE);
	size_t elemBytes = sizeof(AnimDummyInstance);

	U8* pMem = NDI_NEW(kAlign16) U8[availBytes];
	ANIM_ASSERTF(pMem, ("Unable to allocate memory map block ALLOCATION_ANIM_DUMMY_INSTANCE -- check ALLOCATION_APP_CPU_FREE and ALLOCATION_APP_CPU_UNACCOUNTED"));

	{
		CustomAllocateJanitor jj(pMem, availBytes, FILE_LINE_FUNC);

		s_animDummyInstancePoolCapacity = availBytes / elemBytes;
		ANIM_ASSERTF(s_animDummyInstancePoolCapacity <= kMaxAnimDummyInstancePoolCapacity, ("ALLOCATION_ANIM_DUMMY_INSTANCE is larger than kMaxAnimDummyInstancePoolCapacity; please increase the latter"));

		s_aAnimDummyInstancePool = NDI_NEW AnimDummyInstance[s_animDummyInstancePoolCapacity];
		ANIM_ASSERTF(s_aAnimDummyInstancePool, ("AnimDummyInstance system is out of memory -- please increase ALLOCATION_ANIM_DUMMY_INSTANCE"));
	}

	s_animDummyInstancePoolAllocBits.ClearAllBits();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimDummyInstance::ShutDown() {}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
AnimDummyInstance* AnimDummyInstance::PoolAlloc()
{
	AtomicLockJanitor jj(&s_animDummyInstancePoolLock, FILE_LINE_FUNC);

	U64 i = s_animDummyInstancePoolAllocBits.FindFirstClearBit();
	if (i != ~0ULL && i < s_animDummyInstancePoolCapacity)
	{
		s_animDummyInstancePoolAllocBits.SetBit(i);
		return &s_aAnimDummyInstancePool[i];
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
AnimDummyInstance* AnimDummyInstance::PoolFree(AnimDummyInstance* pInstance)
{
	if (pInstance)
	{
		AtomicLockJanitor jj(&s_animDummyInstancePoolLock, FILE_LINE_FUNC);

		U8* pRawInstance = PunPtr<U8*>(pInstance);
		U8* pRawPool = PunPtr<U8*>(&s_aAnimDummyInstancePool[0]);

		ptrdiff_t delta = pRawInstance - pRawPool;
		bool valid = (delta >= 0 && delta < sizeof(AnimDummyInstance) * s_animDummyInstancePoolCapacity && delta % sizeof(AnimDummyInstance) == 0);
		ANIM_ASSERT(valid);
		if (valid)
		{
			//pInstance->Destroy();

			ptrdiff_t i = pInstance - &s_aAnimDummyInstancePool[0];
			s_animDummyInstancePoolAllocBits.ClearBit(i);
		}
	}

	return nullptr; // purely for caller convenience
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
int AnimDummyInstance::GetPoolUsedCount()
{
	return (int)s_animDummyInstancePoolAllocBits.CountSetBits();
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimDummyInstance::AnimDummyInstance()
: AnimInstance()
, m_animId(INVALID_STRING_ID_64)
, m_curPhase(0.0f)
, m_prevPhase(0.0f)
, m_remainderTime(0.0f)
, m_playbackRate(1.0f)
, m_fadeOutTimeLeft(-1.0f)
, m_hCameraCutAnim(nullptr)
, m_pSharedTime(nullptr)
{
	m_flags.m_raw = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDummyInstance::Init(ID id, const StringId64 animId, F32 num30HzFrameIntervals, F32 startPhase, F32 playbackRate, bool looping, ndanim::SharedTimeIndex* pSharedTime)
{
	m_hCameraCutAnim = nullptr;
	m_id = id;

	m_flags.m_raw = 0;

	// most of the clip data is unused, but we create one so we can pass it to standard functions that operate on one
	m_clip.m_magic = OrbisAnim::kAnimClipMagicVersion_Current;
	m_clip.m_animHierarchyId = 0;
	m_clip.m_totalSize = 0;
	m_clip.m_numGroups = 0;
	m_clip.m_numTotalBlocks = 0;
	m_clip.m_frameTableOffset = 0;
	m_clip.m_groupHeadersOffset = 0;
	m_clip.m_userDataOffset = 0;

	SetAnim(animId, num30HzFrameIntervals, looping);

	m_pSharedTime = pSharedTime;

	m_curPhase = startPhase;
	ndanim::SharedTimeIndex::GetOrSet(m_pSharedTime, &m_curPhase, FILE_LINE_FUNC);

	m_prevPhase = m_curPhase;

	// The 'frame count' is 1-based but the 'sample frame' is 0-based
	const float maxFrameSample = static_cast<float>(GetFrameCount()) - 1.0f;
	m_curSample = m_curPhase * maxFrameSample;
	m_playbackRate = playbackRate;
	ANIM_ASSERT(IsFinite(m_playbackRate));

	m_fadeOutTimeLeft = -1.0f; // negative value means "not currently fading out"
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimDummyInstance::GetMayaFrame() const
{
	return GetMayaFrameFromClip(&m_clip, GetPhase());
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimDummyInstance::GetPrevMayaFrame() const
{
	return GetMayaFrameFromClip(&m_clip, GetPrevPhase());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDummyInstance::SetPlaybackRate(F32 playbackRate)
{
	m_playbackRate = playbackRate;
	ANIM_ASSERT(IsFinite(m_playbackRate));
}

// -------------------------------------------------------------------------------------------------
void AnimDummyInstance::SetAnim(const StringId64 animId, F32 num30HzFrameIntervals, bool looping)
{
	m_animId = animId;

	m_clip.m_framesPerSecond = 30.0f;								// assume 30 FPS for dummy clips
	m_clip.m_clipFlags = looping ? OrbisAnim::kClipLooping : 0;
	m_clip.m_numTotalFrames = (U16)(num30HzFrameIntervals + 1.0f);
	m_clip.m_phasePerFrame = 1.0f / num30HzFrameIntervals;			// Inverse of (m_numTotalFrames-1), for easy access
	m_clip.m_fNumFrameIntervals = num30HzFrameIntervals;			// (float)(m_numTotalFrames-1), for easy access
	m_clip.m_secondsPerFrame = 1.0f / m_clip.m_framesPerSecond;

	m_flags.m_looping = looping;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDummyInstance::FadeOut(float fadeTimeSec)
{
	ANIM_ASSERTF(fadeTimeSec >= 0.0f, ("AnimDummyInstance::FadeOut(): invalid fadeTimeSec (%g)\n", fadeTimeSec));
	m_fadeOutTimeLeft = fadeTimeSec;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDummyInstance::DebugPrint(MsgOutput output) const
{
	bool isForceLoop = g_animOptions.m_actorViewerOverrides.IsForceLoop();
	const ndanim::ClipData* pClip = GetClip();

	//const bool skelIdMatch = (pSkel->m_skelId == m_pAnim->m_skelID);
	//const bool hierarchyIdMatch = (pSkel->m_hierarchyId == pClip->m_hierarchyId);
	//const bool animSkelMismatch = (!skelIdMatch || !hierarchyIdMatch);
	const bool isRetargeted = false;

	if (isRetargeted)
		SetColor(output, kColorMagenta);
	else
		SetColor(output, kColorCyan);

	PrintTo(output,
		"  Animation: \"%s\"%s%s, frame %3.2f/%3.2f, phase %1.3f/1.00 (%.2f sec, %.2f FPS) (%.2f blend)", 
		DevKitOnly_StringIdToString(m_animId), 
		/*isRetargeted ? retargetSourceSkelName :*/ "",
		/*m_pAnim->m_flags & ArtItemAnim::kAdditive ? "(add)" :*/ "",
		GetMayaFrame(), 
		(pClip ? 30.0f * pClip->m_secondsPerFrame * (static_cast<float>(GetFrameCount()) - 1.0f) : 0.0f),
		GetPhase(),
		GetDuration(),
		(pClip ? pClip->m_framesPerSecond : 0.0f),
		/*GetFade()*/1.0f
		);
	if (IsLooping())
		PrintTo(output, isForceLoop ? "looping(forced) " : "looping ");
	//if (IsFlipped())
	//	PrintTo(output, "mirrored ");

	PrintTo(output, "\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
const BoundFrame& AnimDummyInstance::GetApOrigin() const
{
	static const BoundFrame s_bfIdentity(kIdentity);
	return s_bfIdentity;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDummyInstance::SetCameraCutAnim(const ArtItemAnim* pAnim)
{
	m_hCameraCutAnim = pAnim;
}


/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimDummyInstance::GetPhase() const
{
	return m_curPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimDummyInstance::GetPrevPhase() const
{
	return m_prevPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDummyInstance::SetPhaseInternal(F32 phase)
{
	if (GetFrameCount() == 1)
		m_curPhase = 0.0f;
	else
		m_curPhase = phase;

	ndanim::SharedTimeIndex::GetOrSet(m_pSharedTime, &m_curPhase, FILE_LINE_FUNC);

	// The 'frame count' is 1-based but the 'sample frame' is 0-based
	const float maxFrameSample = static_cast<float>(GetFrameCount()) - 1.0f;
	m_curSample = m_curPhase * maxFrameSample;

	ANIM_ASSERT(IsFinite(m_curPhase));
	ANIM_ASSERT(IsFinite(m_curSample));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDummyInstance::SetPhase(F32 phase, bool bSingleStepping /* = false */)
{
	if (!bSingleStepping)
	{
		// We've warped to a new spot in the time line, so reset m_prevPhase.
		m_prevPhase = m_curPhase;
	}

	SetPhaseInternal(phase);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDummyInstance::SetFrame(F32 f, bool bSingleStepping /* = false */)
{
	// The 'frame count' is 1-based but the 'sample frame' is 0-based
	const float maxFrameSample = static_cast<float>(GetFrameCount()) - 1.0f;
	const float newPhase = f / maxFrameSample;

	SetPhase(newPhase, bSingleStepping);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimDummyInstance::IsValid() const
{
	return (m_animId != INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimDummyInstance::GetFramesPerSecond() const
{
	return m_clip.m_framesPerSecond;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 AnimDummyInstance::GetFrameCount() const
{
	const ndanim::ClipData* pClipData = GetClip();
	return pClipData ? pClipData->m_numTotalFrames : 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimDummyInstance::GetDuration() const
{
	const ndanim::ClipData* pClipData = GetClip();
	return pClipData ? pClipData->m_fNumFrameIntervals * pClipData->m_secondsPerFrame : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDummyInstance::SetLooping(bool onoff)
{
	m_flags.m_looping = onoff;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimDummyInstance::IsLooping() const
{
	return m_flags.m_looping;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDummyInstance::PhaseUpdate(F32 deltaTime)
{
	//	MsgOut("## Anim ## - AnimDummyInstance::PhaseUpdate\n");

	if (m_flags.m_skipPhaseUpdateThisFrame)
	{
		m_flags.m_skipPhaseUpdateThisFrame = false;
		return;
	}

	bool abTimeOverflow = false;
	bool abTimeUnderflow = false;
	bool abTimeClamped = false;

	const ndanim::ClipData* pClipData = GetClip();
	if (pClipData)
	{
		// The 'frame count' is 1-based but the 'sample frame' is 0-based
		const float maxFrameSample = static_cast<float>(pClipData->m_fNumFrameIntervals);

		F32 oldPhase = m_prevPhase = GetPhase();

		if (!m_flags.m_frozen)
		{
			m_curPhase += m_playbackRate * deltaTime * pClipData->m_phasePerFrame * pClipData->m_framesPerSecond;
			ndanim::SharedTimeIndex::GetOrSet(m_pSharedTime, &m_curPhase, FILE_LINE_FUNC);

			m_curSample = m_curPhase * maxFrameSample;

			ANIM_ASSERT(IsFinite(m_curPhase));
			ANIM_ASSERT(IsFinite(m_curSample));
		}


		F32 newPhase = GetPhase();

#if ANIM_ENABLE_ANIM_CHAIN_TRACE
		extern bool g_animChainDebugTrace;
		extern bool g_animChainDebugTraceScrubbing;
		if (FALSE_IN_FINAL_BUILD(g_animChainDebugTrace && g_animChainDebugTraceScrubbing && deltaTime != 0.0f))
		{
			MsgCinematic(FRAME_NUMBER_FMT "PhaseUpdate(): %s: dt=%.6f oldPhase=%.6f newPhase=%.6f (DISABLED)\n", FRAME_NUMBER, DevKitOnly_StringIdToString(m_animId), deltaTime, oldPhase, newPhase);
		}
#endif

		// handle looping
		F32 over = newPhase - 1.0f;

		if (over > 0.0f)
		{
			abTimeOverflow = true;
			if (!IsLooping())
			{
				SetPhaseInternal(1.0f);
				abTimeClamped = true;
			}
			else
			{
				SetPhaseInternal(over - floorf(over));
			}
		}

		F32 under = GetPhase();
		if (under < 0.0f)
		{
			abTimeUnderflow = true;
			if (!IsLooping())
			{
				SetPhaseInternal(0.0f);
				abTimeClamped = true;
			}
			else
			{
				SetPhaseInternal(1.0f + under);
			}
		}

		newPhase = GetPhase();

		// We need to check for a camera cut... BLAH!!
		m_flags.m_cameraCutThisFrame = false;
		const ArtItemAnim* pCameraCutAnim = m_hCameraCutAnim.ToAnim();
		if (/*m_flags.m_enableCameraCutDetection &&*/ !g_animOptions.m_disableCameraCuts)
		{
			if (pCameraCutAnim)
			{
				if (!abTimeClamped && !abTimeOverflow && !abTimeUnderflow)
				{
					AnimCameraCutInfo cameraCutInfo;
					cameraCutInfo.m_cameraIndex = g_hackTopAnimatedCameraIndex;
					cameraCutInfo.m_didCameraCut = false;

					EvaluateChannelParams evalParams;
					evalParams.m_pAnim = pCameraCutAnim;
					evalParams.m_channelNameId = SID("align");
					evalParams.m_phase = newPhase;
					evalParams.m_mirror = m_flags.m_flipped;
					evalParams.m_wantRawScale = false;
					evalParams.m_pCameraCutInfo = &cameraCutInfo;

					bool result = false;
					ndanim::JointParams outChannelJoint;
					result = EvaluateChannelInAnim(pCameraCutAnim->m_skelID, &evalParams, &outChannelJoint);
					if (result && cameraCutInfo.m_didCameraCut)
					{
						const float currentFrameSample = m_curPhase * maxFrameSample;
						const float adjustedFrameSample = ceilf(currentFrameSample);
						const float lostTimeSec = (adjustedFrameSample - currentFrameSample) * pClipData->m_secondsPerFrame;
						const float adjustedPhase = adjustedFrameSample / maxFrameSample;

						m_flags.m_cameraCutThisFrame = true;

						if (!g_animOptions.m_disableFrameClampOnCameraCut)
						{
							SetPhaseInternal(adjustedPhase);
							m_remainderTime = lostTimeSec;
						}

						if (FALSE_IN_FINAL_BUILD(g_animOptions.m_debugCameraCutsVerbose))
						{
							MsgCinematic("%05u: CUT detected in anim '%s' [AnimDummyInstance::PhaseUpdate()] channel '%s' (dummy instance for '%s')\n"
										 "       curFrame = %.1f  adjFrame = %.1f  lostSec = %.4f  adjPhase = %.4f\n",
										 (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, pCameraCutAnim->GetName(), DevKitOnly_StringIdToStringOrHex(evalParams.m_channelNameId),
										 DevKitOnly_StringIdToStringOrHex(GetAnimId()),
										 currentFrameSample, adjustedFrameSample, lostTimeSec, adjustedPhase);
						}
					}
				}
				else if (FALSE_IN_FINAL_BUILD(g_animOptions.m_debugCameraCutsVerbose))
				{
					MsgCinematic("%05u: CANNOT detect cuts in anim '%s' [AnimDummyInstance::PhaseUpdate()] channel '%s' (dummy instance for '%s')\n"
								 "       abTimeClamped = %d abTimeOverflow = %d abTimeUnderflow = %d\n",
								 (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, pCameraCutAnim->GetName(), "align",
								 DevKitOnly_StringIdToStringOrHex(GetAnimId()),
								 abTimeClamped, abTimeOverflow, abTimeUnderflow);
				}
			}
			else if (FALSE_IN_FINAL_BUILD(g_animOptions.m_debugCameraCutsVerbose))
			{
				MsgCinematic("%05u: CANNOT detect cuts in anim '%s' [AnimDummyInstance::PhaseUpdate()] channel '%s' (dummy instance for '%s')\n"
							 "       pCameraCutAnim = 0x%p\n",
							 (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, "(null)", "align",
							 DevKitOnly_StringIdToStringOrHex(GetAnimId()),
							 pCameraCutAnim);
			}
		}
	}

	// Update the fade
	if (m_fadeOutTimeLeft > 0.0f)
	{
		m_fadeOutTimeLeft -= deltaTime;
		if (m_fadeOutTimeLeft < 0.0f)
		{
			m_fadeOutTimeLeft = 0.0f; // exactly zero means "fully faded out -- ok to free the instance"
		}
	}
}
