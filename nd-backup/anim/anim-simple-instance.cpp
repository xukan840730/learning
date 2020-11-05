/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-simple-instance.h"

#include "corelib/memory/relocate.h"
#include "corelib/system/endian.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-stat.h"
#include "ndlib/anim/anim-streaming.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/camera/camera-interface.h"
#include "ndlib/nd-frame-state.h"

#include "gamelib/level/artitem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemSkeleton;

//STATIC_ASSERT((sizeof(AnimSimpleInstance) % 16) == 0);
//STATIC_ASSERT(sizeof(AnimSimpleInstance) == 128);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSimpleInstance::AnimSimpleInstance()
: AnimInstance()
, m_apOrigin(SMath::kIdentity)
, m_animId(0)
, m_pAnimTable(nullptr)
, m_curSample(0.0f)
, m_curPhase(0.0f)
, m_prevPhase(0.0f)
, m_remainderTime(0.0f)
, m_playbackRate(1.0f)
, m_fadeTimeLeft(0.0f)
, m_fadeTimeTotal(1.0f)
, m_currentFade(1.0f)
, m_blendType(DC::kAnimCurveTypeUniformS)
, m_pSharedTime(nullptr)
{
	m_flags.m_flipped = false;
	m_flags.m_looping = false;
	m_flags.m_alignToApOrigin = false;
	m_flags.m_user = false;
	m_flags.m_noUpdateLocation = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::Init(ID id,
							  const AnimTable* pAnimTable,
							  ArtItemAnimHandle anim,
							  F32 startPhase,
							  F32 playbackRate,
							  F32 fadeTime,
							  DC::AnimCurveType blendType,
							  F32 effStartFrame /*= -1.0f */,
							  ndanim::SharedTimeIndex* pSharedTime /* = nullptr */)
{
	m_id = id;
	m_pAnimTable = pAnimTable;
	m_pSharedTime = pSharedTime;

	if (!anim.ToArtItem())
		return;

	SetAnim(anim, anim.ToArtItem()->GetNameId());

	m_curPhase = startPhase;
	ndanim::SharedTimeIndex::GetOrSet(m_pSharedTime, &m_curPhase, FILE_LINE_FUNC);

	m_prevPhase = (effStartFrame < 0.0f) ? m_curPhase : effStartFrame;

	// The 'frame count' is 1-based but the 'sample frame' is 0-based
	const float maxFrameSample = static_cast<float>(GetFrameCount()) - 1.0f;
	m_curSample = m_curPhase * maxFrameSample;
	m_playbackRate = playbackRate;
	ANIM_ASSERT(IsFinite(m_playbackRate));

	if (fadeTime > 0.0f)
		m_currentFade = 0.0f;
	else
		m_currentFade = 1.0f;

	m_fadeTimeLeft = fadeTime;
	m_fadeTimeTotal = fadeTime;
	m_blendType = (U8)blendType;

	m_flags.m_frozen = false;
	m_flags.m_cameraCutThisFrame = false;
	m_flags.m_enableCameraCutDetection = false;
	m_flags.m_skipPhaseUpdateThisFrame = false;
	m_flags.m_phaseUpdatedManuallyThisFrame = false;

	g_animStat.SubmitPlayCount(anim.ToArtItem()->m_skelID, anim.ToArtItem()->GetNameId(), 1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::Init(ID id,
							  const AnimTable* pAnimTable,
							  StringId64 translatedAnimNameId,
							  F32 startPhase,
							  F32 playbackRate,
							  F32 fadeTime,
							  DC::AnimCurveType blendType,
							  F32 effStartFrame /* = -1.0f */,
							  ndanim::SharedTimeIndex* pSharedTime /* = nullptr */)
{
	m_pAnimTable = pAnimTable;

	ArtItemAnimHandle anim = m_pAnimTable->LookupAnim(translatedAnimNameId);
	if (!anim.ToArtItem())
		return;

	Init(id, pAnimTable, anim, startPhase, playbackRate, fadeTime, blendType, effStartFrame, pSharedTime);
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimSimpleInstance::GetPhaseCeiling(F32* pFrame, F32* pFrameCeil) const
{
	// The 'frame count' is 1-based but the 'sample frame' is 0-based
	const float maxFrameSample = static_cast<float>(GetFrameCount()) - 1.0f;
	const F32 phase = GetPhase();

	const F32 frame = phase * maxFrameSample;
	const F32 frameCeil = ceilf(frame);
	const F32 phaseCeil = frameCeil / maxFrameSample;

	if (pFrame) *pFrame = frame;
	if (pFrameCeil) *pFrameCeil = frameCeil;

	return phaseCeil;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimSimpleInstance::GetMayaFrame() const
{
	const ndanim::ClipData* pClip = GetClip();
	return pClip ? GetMayaFrameFromClip(pClip, GetPhase()) : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimSimpleInstance::GetPrevMayaFrame() const
{
	const ndanim::ClipData* pClip = GetClip();
	return pClip ? GetMayaFrameFromClip(pClip, GetPrevPhase()) : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimSimpleInstance::PhaseToMayaFrame(float phase) const
{
	const ndanim::ClipData* pClip = GetClip();
	return pClip ? GetMayaFrameFromClip(pClip, MinMax01(phase)) : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::SetPlaybackRate(F32 playbackRate)
{
	m_playbackRate = playbackRate;
	ANIM_ASSERT(IsFinite(m_playbackRate));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pAnimTable, delta, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::SetAnim(ArtItemAnimHandle anim, const StringId64 animId)
{
	const ArtItemAnim* pAnim = anim.ToArtItem();

	m_animHandle = anim;
	if (animId != INVALID_STRING_ID_64)
		m_animId = animId;
	else
		m_animId = pAnim ? StringToStringId64(pAnim->GetName()) : INVALID_STRING_ID_64;

	m_flags.m_looping = pAnim && (pAnim->m_flags & ArtItemAnim::kLooping);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::ForceRefreshAnimPointers()
{
	m_animHandle = m_pAnimTable->LookupAnim(m_animId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::AlignToActionPackOrigin(bool onoff)
{
	m_flags.m_alignToApOrigin = onoff;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleInstance::IsAlignedToActionPackOrigin() const
{
	return m_flags.m_alignToApOrigin;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::SetApOrigin(const BoundFrame& apRef)
{
	ANIM_ASSERT(IsFinite(apRef.GetLocatorPs()));
	ANIM_ASSERT(IsNormal(apRef.GetRotationPs()));
	ANIM_ASSERT(IsFinite(apRef.GetLocator()));
	ANIM_ASSERT(IsNormal(apRef.GetRotation()));
	m_apOrigin = apRef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const BoundFrame& AnimSimpleInstance::GetApOrigin() const
{
	return m_apOrigin;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimSimpleInstance::EvaluateChannels(const StringId64* pChannelNames, 
										  U32F numChannels, 
										  F32 phase, 
										  ndanim::JointParams* pOutChannelJoints,
										  bool mirror /* = false */, 
										  bool wantRawScale /* = false */, 
										  AnimCameraCutInfo* pCameraCutInfo /* = nullptr */) const
{
	EvaluateChannelParams evalParams;
	evalParams.m_mirror			= mirror;
	evalParams.m_wantRawScale	= wantRawScale;
	evalParams.m_pCameraCutInfo = pCameraCutInfo;
	evalParams.m_phase = phase;

	return EvaluateChannels(pChannelNames, numChannels, pOutChannelJoints, evalParams);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimSimpleInstance::EvaluateChannels(const StringId64* pChannelNames,
										  U32F numChannels,
										  ndanim::JointParams* pOutChannelJoints,
										  const EvaluateChannelParams& params) const
{
	U32F evaluatedChannels = 0;

	if (m_animHandle.ToArtItem())
	{
		const float phase = GetPhase();

		for (U32F ii = 0; ii < numChannels; ++ii)
		{
			pOutChannelJoints[ii].m_scale = Vector(SCALAR_LC(1.0f));
			pOutChannelJoints[ii].m_quat  = kIdentity;
			pOutChannelJoints[ii].m_trans = kOrigin;

			EvaluateChannelParams animParams = params;

			animParams.m_pAnim		   = m_animHandle.ToArtItem();
			animParams.m_channelNameId = pChannelNames[ii];

			if (animParams.m_phase < 0.0f)
			{
				animParams.m_phase = phase;
			}

			if (EvaluateChannelInAnim(m_pAnimTable->GetSkelId(), &animParams, &pOutChannelJoints[ii]))
			{
				evaluatedChannels |= (1 << ii);
			}
		}
	}

	return evaluatedChannels;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimSimpleInstance::EvaluateFloatChannels(const StringId64* pChannelNames,
											   U32F numChannels,
											   float* pOutChannelFloats,
											   const EvaluateChannelParams& params) const
{
	U32F evaluatedChannels = 0;

	if (m_animHandle.ToArtItem())
	{
		const float phase = GetPhase();

		for (U32F ii = 0; ii < numChannels; ++ii)
		{
			pOutChannelFloats[ii] = 0.0f;

			EvaluateChannelParams animParams = params;

			animParams.m_pAnim = m_animHandle.ToArtItem();
			animParams.m_channelNameId = pChannelNames[ii];

			if (animParams.m_phase < 0.0f)
			{
				animParams.m_phase = phase;
			}

			if (EvaluateCompressedFloatChannel(&animParams, &pOutChannelFloats[ii]))
			{
				evaluatedChannels |= (1 << ii);
			}
		}
	}

	return evaluatedChannels;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ListChannels(MsgOutput output, const ArtItemAnim* pAnim)
{
	const CompressedChannelList* pChannelList = pAnim->m_pCompressedChannelList;
	const U32F numChannels = pChannelList->m_numChannels;
	const StringId64* pChannelNames = pChannelList->m_channelNameIds;
	const CompressedChannel* const* ppChannels = pChannelList->m_channels;

	for (U32F i = 0; i < numChannels; ++i)
	{
		const StringId64 channelName = pChannelNames[i];

		PrintTo(output, "Channel %u = %s\n", i, DevKitOnly_StringIdToString(channelName));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::DebugPrint(MsgOutput output) const
{
	bool isForceLoop = g_animOptions.m_actorViewerOverrides.IsForceLoop();
	const ndanim::ClipData* pClip = GetClip();
	const SkeletonId skelId = m_pAnimTable->GetSkelId();
	const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(skelId).ToArtItem();

	if (m_animHandle.ToArtItem())
	{
		//const bool skelIdMatch = (pSkel->m_skelId == m_pAnim->m_skelID);
		//const bool hierarchyIdMatch = (pSkel->m_hierarchyId == pClip->m_hierarchyId);
		//const bool animSkelMismatch = (!skelIdMatch || !hierarchyIdMatch);
		const bool isRetargeted = (m_pAnimTable->GetSkelId() != m_animHandle.ToArtItem()->m_skelID);

		const ArtItemAnim* pAnim = m_animHandle.ToArtItem();
		const ArtItemAnim* pStreamingAnim = pAnim;

		if (m_animHandle.ToArtItem()->m_flags & ArtItemAnim::kStreaming)
		{
			pStreamingAnim = AnimStreamGetArtItem(m_animHandle.ToArtItem(), m_animHandle.ToArtItem()->GetNameId(), m_curPhase);
		}

		if (isRetargeted)
			SetColor(output, kColorMagenta);
		else
			SetColor(output, kColorCyan);

		const char* skelName = ResourceTable::LookupSkelName(m_animHandle.ToArtItem()->m_skelID);
		char retargetSourceSkelName[128];
		sprintf(&retargetSourceSkelName[0], "[%s]", skelName);

		if (!g_animOptions.m_debugPrint.m_simplified)
		{
			PrintTo(output,
				"  Animation: \"%s\"%s%s, frame %3.2f/%3.2f, phase %1.3f/1.00 (%.2f sec, %.2f FPS) (%.2f blend) ",
				pStreamingAnim == nullptr ? (pAnim ? "missing-chunk" : "missing-anim") : pStreamingAnim->GetName(),
				isRetargeted ? retargetSourceSkelName : "",
				m_animHandle.ToArtItem()->m_flags & ArtItemAnim::kAdditive ? "(add)" : "",
				GetMayaFrame(),
				(pClip ? 30.0f * pClip->m_secondsPerFrame * (static_cast<float>(GetFrameCount()) - 1.0f) : 0.0f),
				GetPhase(),
				GetDuration(),
				(pClip ? pClip->m_framesPerSecond : 0.0f),
				GetFade()
				);
			if (IsLooping())
				PrintTo(output, isForceLoop ? "looping(forced) " : "looping ");
			if (IsFlipped())
				PrintTo(output, "mirrored ");
			if (g_animOptions.m_debugPrint.m_showPakFileNames && m_animHandle.ToArtItem() && m_animHandle.ToArtItem()->m_pDebugOnlyPakName)
			{
				PrintTo(output, "pak: %s ", m_animHandle.ToArtItem()->m_pDebugOnlyPakName);
			}
		}
		else
		{
			PrintTo(output,
				"  Animation: \"%s\", frame %3.2f/%3.2f phase: %.2f\n",
				pStreamingAnim == nullptr ? (pAnim ? "missing-chunk" : "missing-anim") : pStreamingAnim->GetName(),
				GetMayaFrame(),
				(pClip ? 30.0f * pClip->m_secondsPerFrame * (static_cast<float>(GetFrameCount()) - 1.0f) : 0.0f),
				m_curPhase);
		}
	}
	else
	{
		SetColor(output, 0xFF000000 | 0x0000FFFF);
		PrintTo(output,"  null, frame 0.0/0.0, phase 0.00/0.00 (0.0 sec, 0.00 FPS) (0.00 blend)");
	}
	if (!g_animOptions.m_debugPrint.m_simplified)
	{
		PrintTo(output, "\n");
	}
}
/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimSimpleInstance::GetPhase() const
{
	return m_curPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimSimpleInstance::GetPrevPhase() const
{
	return m_prevPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::SetPhaseInternal(F32 phase)
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
void AnimSimpleInstance::SetPhase(F32 phase, bool bSingleStepping /* = false */)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_cinematicCaptureMode))
	{
		m_prevPhase = m_curPhase;
		m_flags.m_phaseUpdatedManuallyThisFrame = true;
	}
	else if (!bSingleStepping)
	{
		// We've warped to a new spot in the time line, so reset m_prevPhase.
		m_prevPhase = m_curPhase;
	}

	SetPhaseInternal(phase);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::SetFrame(F32 f, bool bSingleStepping /* = false */)
{
	// The 'frame count' is 1-based but the 'sample frame' is 0-based
	const float maxFrameSample = static_cast<float>(GetFrameCount()) - 1.0f;
	const float newPhase = f / maxFrameSample;

	SetPhase(newPhase, bSingleStepping);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ndanim::ClipData* AnimSimpleInstance::GetClip() const
{
	const ndanim::ClipData* pClipData = nullptr;
	if (m_animHandle.ToArtItem())
	{
		const ArtItemAnim* pAnim = m_animHandle.ToArtItem();
		if (pAnim->m_pClipData)
		{
			pClipData = pAnim->m_pClipData;
		}
	}

	return pClipData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleInstance::IsValid() const
{
	if (m_animHandle.ToArtItem() == nullptr)
		return false;

	const ArtItemAnim* pAnim = m_animHandle.ToArtItem();
	return pAnim->m_pClipData != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimSimpleInstance::GetFramesPerSecond() const
{
	return GetClip()->m_framesPerSecond;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 AnimSimpleInstance::GetFrameCount() const
{
	const ndanim::ClipData* pClipData = GetClip();
	return pClipData ? pClipData->m_numTotalFrames : 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimSimpleInstance::GetDuration() const
{
	const ndanim::ClipData* pClipData = GetClip();
	return pClipData ? pClipData->m_fNumFrameIntervals * pClipData->m_secondsPerFrame : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimSimpleInstance::GetFade() const
{
	return m_currentFade;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::SetLooping(bool onoff)
{
	m_flags.m_looping = onoff;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleInstance::IsLooping() const
{
	return m_flags.m_looping;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSimpleInstance::IsFlipped() const
{
	return m_flags.m_flipped;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::SetFlipped(bool f)
{
	m_flags.m_flipped = f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSimpleInstance::PhaseUpdate(F32 deltaTime, const FgAnimData* pAnimData)
{
	//	MsgOut("## Anim ## - AnimSimpleInstance::PhaseUpdate\n");

	if (m_flags.m_skipPhaseUpdateThisFrame)
	{
		m_flags.m_skipPhaseUpdateThisFrame = false;
		return;
	}

	bool abTimeOverflow = false;
	bool abTimeUnderflow = false;
	bool abTimeClamped = false;

	const ArtItemAnim* pAnim = m_animHandle.ToArtItem();
	if (pAnim && pAnim->m_pClipData)
	{
		const ndanim::ClipData* pClipData = pAnim->m_pClipData;

		// The 'frame count' is 1-based but the 'sample frame' is 0-based
		const float maxFrameSample = static_cast<float>(pClipData->m_fNumFrameIntervals);

		F32 oldPhase = GetPhase();
		if (TRUE_IN_FINAL_BUILD(!g_animOptions.m_cinematicCaptureMode || !m_flags.m_phaseUpdatedManuallyThisFrame))
			m_prevPhase = oldPhase;

		OMIT_FROM_FINAL_BUILD(m_flags.m_phaseUpdatedManuallyThisFrame = false);

		if (!m_flags.m_frozen)
		{
			m_curPhase += m_playbackRate * deltaTime * pClipData->m_phasePerFrame * pClipData->m_framesPerSecond;
			if (m_firstUpdatePhase >= 0.f)
			{
				m_curPhase = m_firstUpdatePhase;
				m_firstUpdatePhase = -1.f;
			}
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
			MsgCinematic(FRAME_NUMBER_FMT "PhaseUpdate(): %s: dt=%.6f oldPhase=%.6f newPhase=%.6f (SIMPLE)\n", FRAME_NUMBER, DevKitOnly_StringIdToString(m_animId), deltaTime, oldPhase, newPhase);
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

		MsgAnimVerbose("AnimSimpleInstance '%s': stepped phase from %.4f to %.4f\n", DevKitOnly_StringIdToString(GetAnimId()), oldPhase, newPhase);

		// We need to check for a camera cut... BLAH!!
		m_flags.m_cameraCutThisFrame = false;
		if (m_flags.m_enableCameraCutDetection && !g_animOptions.m_disableCameraCuts && !abTimeClamped && !abTimeOverflow && !abTimeUnderflow)
		{
			AnimCameraCutInfo cameraCutInfo;
			cameraCutInfo.m_cameraIndex = g_hackTopAnimatedCameraIndex;
			cameraCutInfo.m_didCameraCut = false;

			EvaluateChannelParams evalParams;
			evalParams.m_pAnim = m_animHandle.ToArtItem();
			evalParams.m_channelNameId = SID("align");
			evalParams.m_phase = newPhase;
			evalParams.m_mirror = m_flags.m_flipped;
			evalParams.m_wantRawScale = false;
			evalParams.m_pCameraCutInfo = &cameraCutInfo;

			bool result = false;
			ndanim::JointParams outChannelJoint;
			result = EvaluateChannelInAnim(pAnim->m_skelID, &evalParams, &outChannelJoint);
			if (result)
			{
				if (pAnimData && pAnimData->AreCameraCutsDisabled())
					cameraCutInfo.m_didCameraCut = false; // ignore camera cuts when we have a dummy instance (for cinematics)

				if (cameraCutInfo.m_didCameraCut)
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

#if !defined(NDI_ARCH_SPU) && !FINAL_BUILD
					if (g_animOptions.m_debugCameraCutsVerbose)
					{
						MsgCinematic("%05u: CUT detected in anim '%s' [AnimSimpleInstance::PhaseUpdate()] channel '%s' (simple instance)\n"
									 "       curFrame = %.1f  adjFrame = %.1f  lostSec = %.4f  adjPhase = %.4f\n",
									 (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, pAnim->GetName(), DevKitOnly_StringIdToStringOrHex(evalParams.m_channelNameId),
									 currentFrameSample, adjustedFrameSample, lostTimeSec, adjustedPhase);
					}
#endif
					
					if (pAnimData && pAnimData->AreCameraCutNotificationsEnabled())
						g_ndConfig.m_pCameraInterface->NotifyCameraCut(FILE_LINE_FUNC);
				}
			}
		}
	}
	// Update the fade
	if (m_fadeTimeLeft > 0.0f)
	{
		m_fadeTimeLeft -= deltaTime;

		F32 tt = Limit01((m_fadeTimeTotal - m_fadeTimeLeft) / m_fadeTimeTotal);
		m_currentFade = CalculateCurveValue(tt, m_blendType);
	}
	else
	{
		m_currentFade = 1.0f;
	}
}
