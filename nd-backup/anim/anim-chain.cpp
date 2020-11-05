/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-chain.h"

#include "corelib/memory/memory-map.h"
#include "corelib/memory/memory.h"
#include "corelib/util/bit-array.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/process/clock.h"

/// --------------------------------------------------------------------------------------------------------------- ///
#if !FINAL_BUILD
#define ANIM_CHAIN_REPORT_MAYA_FRAMES_OVERLAPPED 1
#endif

#if ANIM_CHAIN_REPORT_MAYA_FRAMES_OVERLAPPED
#include "ndlib/render/util/prim.h"		// DEV ONLY
#include "ndlib/text/string-builder.h"	// DEV ONLY
#endif

#include "gamelib/level/artitem.h" // tisk tisk

static const size_t kMaxAnimChainPoolCapacity = 512;
static size_t s_animChainPoolCapacity;
static AnimChain* s_aAnimChainPool;
static BitArray<kMaxAnimChainPoolCapacity> s_animChainPoolAllocBits;
static NdAtomicLock s_animChainPoolLock;
static const float kInverseTimeThreshold = 0.001f;
bool g_debugAnimChainPool = false;
bool g_animChainDisableMayaFramesOverlapped = false;
#if ANIM_ENABLE_ANIM_CHAIN_TRACE
	bool g_animChainDebugTrace = FALSE_IN_FINAL_BUILD(false);
	bool g_animChainDebugTraceScrubbing = FALSE_IN_FINAL_BUILD(false);
#endif
float FINAL_CONST kAudioSync_P = 0.02f;	// these settings seem to produce good tracking, but they could certainly be tweaked more if necessary
float FINAL_CONST kAudioSync_I = 1.0f; //1.5f;
float FINAL_CONST kAudioSync_D = 0.1f; //0.1f;
float FINAL_CONST kAudioSync_maxError = NDI_FLT_MAX;
float FINAL_CONST kAudioSync_errorThreshTight = 1.0f / 60.0f;
float FINAL_CONST kAudioSync_errorThreshLoose = 1.5f / 30.0f;
float FINAL_CONST kAudioSync_minScale = 0.8f;
float FINAL_CONST kAudioSync_maxScale = 1.0f/kAudioSync_minScale;
I64 FINAL_CONST kGameFrameSlop = 10000LL; // add some slop so that 0LL means "a long time ago"

/// --------------------------------------------------------------------------------------------------------------- ///
#if ANIM_CHAIN_REPORT_MAYA_FRAMES_OVERLAPPED
inline void ReportMayaFramesOverlapped(const AnimChain& animChain)
{
	if (!g_animChainDisableMayaFramesOverlapped)
	{
		StringBuilder<256> msg("WARNING: Maya frame ranges are overlapped in '%s.\nAs such, Maya frame indices (e.g., (on-cin-maya-frames ...)) will not work reliably.\nPlease use absolute frame indices only in this cinematic.\n",
		                       DevKitOnly_StringIdToString(animChain.GetCinematicId()));
		g_prim.Draw(DebugString2D(Vec2(0.038f, 0.76f), kDebug2DNormalizedCoords, msg.c_str(), kColorOrangeTrans, 0.6f), kPrimDuration1FramePauseable);
	}
}
#else
#define ReportMayaFramesOverlapped(c)
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::StartUp()
{
	size_t availBytes = Memory::GetSize(ALLOCATION_ANIM_CHAIN);
	size_t elemBytes = sizeof(AnimChain);

	U8* pMem = NDI_NEW(kAlign16) U8[availBytes];
	ANIM_ASSERTF(pMem, ("Unable to allocate memory map block ALLOCATION_ANIM_CHAIN -- check ALLOCATION_APP_CPU_FREE and ALLOCATION_APP_CPU_UNACCOUNTED"));

	{
		CustomAllocateJanitor jj(pMem, availBytes, FILE_LINE_FUNC);

		s_animChainPoolCapacity = availBytes / elemBytes;
		ANIM_ASSERTF(s_animChainPoolCapacity <= kMaxAnimChainPoolCapacity, ("ALLOCATION_ANIM_CHAIN is larger than kMaxAnimChainPoolCapacity; please increase the latter"));

		s_aAnimChainPool = NDI_NEW AnimChain[s_animChainPoolCapacity];
		ANIM_ASSERTF(s_aAnimChainPool, ("AnimChain system is out of memory -- please increase ALLOCATION_ANIM_CHAIN"));

		size_t usedBytes = s_animChainPoolCapacity * elemBytes;
		MsgCinematic("ALLOCATION_ANIM_CHAIN memory used: %llu/%llu bytes (%.1f %%): %llu capacity\n", usedBytes, availBytes, (float)usedBytes * 100.0f / (float)availBytes, s_animChainPoolCapacity);
	}

	s_animChainPoolAllocBits.ClearAllBits();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::ShutDown()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimChain* AnimChain::PoolAlloc(const char* debugInfo)
{
	AtomicLockJanitor jj(&s_animChainPoolLock, FILE_LINE_FUNC);

	U64 i = s_animChainPoolAllocBits.FindFirstClearBit();
	if (i != ~0ULL && i < s_animChainPoolCapacity)
	{
		s_animChainPoolAllocBits.SetBit(i);
		if (g_debugAnimChainPool)
			MsgCinematic("AnimChain::PoolAlloc() @ 0x%p: %d chains allocated (%s)\n", &s_aAnimChainPool[i], (int)s_animChainPoolAllocBits.CountSetBits(), debugInfo);
		return &s_aAnimChainPool[i];
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimChain* AnimChain::PoolFree(AnimChain* pAnimChain, const char* debugInfo)
{
	if (pAnimChain)
	{
		AtomicLockJanitor jj(&s_animChainPoolLock, FILE_LINE_FUNC);

		U8* pRawAnimChain = PunPtr<U8*>(pAnimChain);
		U8* pRawPool = PunPtr<U8*>(&s_aAnimChainPool[0]);

		ptrdiff_t delta = pRawAnimChain - pRawPool;
		bool valid = (delta >= 0 && delta < sizeof(AnimChain) * s_animChainPoolCapacity && delta % sizeof(AnimChain) == 0);
		ANIM_ASSERT(valid);
		if (valid)
		{
			pAnimChain->Destroy();

			ptrdiff_t i = pAnimChain - &s_aAnimChainPool[0];
			s_animChainPoolAllocBits.ClearBit(i);

			if (g_debugAnimChainPool)
				MsgCinematic("AnimChain::PoolFree() @ 0x%p: %d chains allocated (%s)\n", pAnimChain, (int)s_animChainPoolAllocBits.CountSetBits(), debugInfo);
		}
		else
		{
			MsgCinematic("AnimChain::PoolFree(): attempt to free invalid AnimChain @ 0x%p (%s)\n", pAnimChain, debugInfo);
		}
	}

	return nullptr; // purely for caller convenience
}

/// --------------------------------------------------------------------------------------------------------------- ///
int AnimChain::GetPoolUsedCount()
{
	return (int)s_animChainPoolAllocBits.CountSetBits();
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline float SafeDivide(float numerator, float denominator)
{
	return (denominator > kInverseTimeThreshold) ? (numerator / denominator) : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimChain::AnimChain()
	: m_numAnims(0)
	, m_cinematicId(INVALID_STRING_ID_64)
	, m_chainAnimId(INVALID_STRING_ID_64)
	, m_globalFadeInSec(0.0f)
	, m_globalFadeOutSec(0.0f)
	, m_clockScale(1.0f)
	, m_audioSyncActive(false)
	, m_audioSyncDisabled(false)
	, m_looping(false)
{
	m_localTimeIndex.Clear();
	m_globalTimeIndex.Clear();
	m_prevGlobalTimeIndex = m_globalTimeIndex;
	m_lastFramePrevUpdated = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	m_lastMasterGameFrame = m_lastCameraGameFrame = 0LL;
	m_localEnd.SetToInvalid();
}

/// --------------------------------------------------------------------------------------------------------------- ///
//AnimChain::AnimChain(const AnimChain& other)
//{
//	memcpy(this, &other, sizeof(*this)); // POD, shallow copy is sufficient
//}
//
/// --------------------------------------------------------------------------------------------------------------- ///
//AnimChain& AnimChain::operator=(const AnimChain& other)
//{
//	if (this != &other)
//	{
//		memcpy(this, &other, sizeof(*this)); // POD, shallow copy is sufficient
//	}
//	return *this;
//}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::Init(StringId64 cinematicId,
					 StringId64 chainAnimId,
					 I32 numAnims,
					 const AnimInfo aAnimInfo[],
					 float globalStartPhase,
					 AnimControl* pAnimControl)
{
	m_numAnims = 0;
	m_globalFadeInSec = 0.0f;
	m_globalFadeOutSec = 0.0f;
	m_localEnd.SetToInvalid();
	m_cinematicId = cinematicId;
	m_chainAnimId = chainAnimId;
	m_clockScale = 1.0f;
	m_lastMasterGameFrame = m_lastCameraGameFrame = 0LL;
	m_audioSyncActive = m_audioSyncDisabled = false;

	m_localTimeIndex.Clear();
	m_globalTimeIndex.Clear();
	m_prevGlobalTimeIndex = m_globalTimeIndex;
	m_lastFramePrevUpdated = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;

	memset(m_aCumulativeGlobalDuration, 0, sizeof(m_aCumulativeGlobalDuration)); // works because a zeroed TimeFrame means "0 seconds"

	if (!aAnimInfo || !pAnimControl)
		return;

	for (int i = 0; i < numAnims; i++)
		AppendAnim(aAnimInfo[i], pAnimControl);

	ANIM_ASSERT(m_numAnims == numAnims);

	m_overlappedMayaFrames = false;
	int maxMayaEndFrame = INT_MIN;

	TimeFrame cumulativeDuration = TimeFrameZero();
	for (int i = 0; i < m_numAnims; i++)
	{
		TimeDelta clipDuration = TimeDelta(GetAnimDuration(m_aAnimInfo[i]));

		cumulativeDuration += clipDuration;
		m_aCumulativeGlobalDuration[i] = cumulativeDuration;

		if (m_aAnimInfo[i].m_mayaStartFrame < maxMayaEndFrame)
			m_overlappedMayaFrames = true;
		maxMayaEndFrame = Max(maxMayaEndFrame, m_aAnimInfo[i].m_mayaEndFrame);
	}

	// init time index
	m_globalTimeIndex.SetFromPhase(globalStartPhase, *this);
	m_prevGlobalTimeIndex = m_globalTimeIndex;
	m_lastFramePrevUpdated = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	m_localTimeIndex.SetFrom(m_globalTimeIndex, *this);

	m_audioSyncPid.SetGainValues(kAudioSync_P, kAudioSync_I, kAudioSync_D, kAudioSync_maxError);
	m_audioSyncPid.Reset();

	m_pAudioSyncClockScaleHistory = nullptr;
}

void AnimChain::InitGlobalStartTime(const GlobalTimeIndex& globalTime)
{
	m_globalTimeIndex.SetFromSeconds(globalTime.AsTimeSec(), *this);
	m_prevGlobalTimeIndex = m_globalTimeIndex;
	m_lastFramePrevUpdated = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	m_localTimeIndex.SetFrom(m_globalTimeIndex, *this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::InitLocalStartPhase(int animIndex, float localStartPhase)
{
	m_localTimeIndex.SetFromPhase(animIndex, localStartPhase, *this);
	m_globalTimeIndex.SetFrom(m_localTimeIndex, *this);
	m_prevGlobalTimeIndex = m_globalTimeIndex;
	m_lastFramePrevUpdated = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::Destroy()
{
	if (m_pAudioSyncClockScaleHistory)
	{
		ANIM_ASSERT(Memory::IsDebugMemoryAvailable());

		AllocateJanitor jj(kAllocDebug, FILE_LINE_FUNC);
		NDI_DELETE m_pAudioSyncClockScaleHistory;

		m_pAudioSyncClockScaleHistory = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::Clear()
{
	Init(INVALID_STRING_ID_64, INVALID_STRING_ID_64, 0, nullptr, 0.0f, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::RefreshIsMaster()
{
	m_lastMasterGameFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused + kGameFrameSlop; // add some slop so that 0LL means "a long time ago"
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimChain::IsMaster() const
{
	I64 curGameFrame = (EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused + kGameFrameSlop); // add some slop so that 0LL means "a long time ago"
	I64 diff = curGameFrame - m_lastMasterGameFrame;
	return (diff == 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::RefreshIsCamera()
{
	m_lastCameraGameFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused + kGameFrameSlop; // add some slop so that 0LL means "a long time ago"
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimChain::IsCamera() const
{
	I64 curGameFrame = (EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused + kGameFrameSlop); // add some slop so that 0LL means "a long time ago"
	I64 diff = curGameFrame - m_lastCameraGameFrame;
	return (diff == 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::AllocAudioSyncClockScaleHistory()
{
	if (Memory::IsDebugMemoryAvailable())
	{
		m_pAudioSyncClockScaleHistory = NDI_NEW(kAllocDebug, kAlign64) AudioSyncClockScaleHistory;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::SetGlobalFadeInSec(float seconds)
{
	m_globalFadeInSec = seconds;
}
void AnimChain::SetGlobalFadeOutSec(float seconds)
{
	m_globalFadeOutSec = seconds;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::SetGlobalEndPhase(float globalEndPhase)
{
	if (globalEndPhase >= 0.0f)
	{
		GlobalTimeIndex global;
		global.SetFromPhase(globalEndPhase, *this);

		LocalTimeIndex local(global, *this);
		m_localEnd = local;
	}
	else
	{
		m_localEnd.SetToInvalid();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimChain::IsSameChain(const AnimChain* pOther) const
{
	// NB: if both are invalid, they should NOT count as the same chain
	return (m_chainAnimId != INVALID_STRING_ID_64 && pOther != nullptr && m_chainAnimId == pOther->m_chainAnimId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimChain::LocalTimeIndex::LocalTimeIndex(const GlobalTimeIndex& global, const AnimChain& animChain)
{
	SetFrom(global, animChain);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::LocalTimeIndex::Clear()
{
	m_animIndex = 0;
	m_timeSec = m_phase = m_mayaFrame = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::LocalTimeIndex::SetFrom(const GlobalTimeIndex& global, const AnimChain& animChain)
{
	const float globalDurationSec = animChain.GetDurationSeconds(kTimeModeGlobal);
	if (globalDurationSec == 0.0f)
	{
		MsgErr("Animation '%s' has a total duration of 0 seconds!", DevKitOnly_StringIdToStringOrHex(animChain.GetChainAnimId()));
	}

	const int numAnims = animChain.m_numAnims;

	if (global.m_timeSec < 0.0f || numAnims == 0 || globalDurationSec == 0.0f) // handle degenerate cases
	{
		m_animIndex = 0;
		m_timeSec = m_phase = 0.0f;
		m_mayaFrame = 0.0f;
		return;
	}

	float prevDurationSec = 0.0f; // search for the animation that contains the global time index
	for (int i = 0; i < numAnims; i++)
	{
		const TimeFrame curDuration = animChain.m_aCumulativeGlobalDuration[i];
		const float curDurationSec = ToSeconds(curDuration);

		if (global.m_timeSec < curDurationSec)
		{
			const float localTimeSec = global.m_timeSec - prevDurationSec;
			SetFromSeconds(i, localTimeSec, animChain);
			return;
		}

		prevDurationSec = curDurationSec;
	}

	// the time index must be at or past the end of the cinematic
	ANIM_ASSERT(global.m_timeSec >= globalDurationSec);
	const int lastAnimIndex = numAnims - 1;
	const TimeFrame penultimateDuration = (lastAnimIndex > 0) ? animChain.m_aCumulativeGlobalDuration[lastAnimIndex - 1] : TimeFrameZero();
	const float penultimateDurationSec = ToSeconds(penultimateDuration);
	const float localTimeSec = global.m_timeSec - penultimateDurationSec;
	SetFromSeconds(lastAnimIndex, localTimeSec, animChain);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::LocalTimeIndex::SetFrom(int animIndex, const GlobalTimeIndex& global, const AnimChain& animChain)
{
	const float globalDurationSec = animChain.GetDurationSeconds(kTimeModeGlobal);
	if (globalDurationSec == 0.0f)
	{
		MsgErr("Animation '%s' has a total duration of 0 seconds!", DevKitOnly_StringIdToStringOrHex(animChain.GetChainAnimId()));
	}

	const int numAnims = animChain.m_numAnims;

	if (global.m_timeSec < 0.0f || numAnims == 0 || globalDurationSec == 0.0f) // handle degenerate cases
	{
		m_animIndex = 0;
		m_timeSec = m_phase = 0.0f;
		m_mayaFrame = 0.0f;
		return;
	}

	const TimeFrame prevDuration = (animIndex > 0) ? animChain.m_aCumulativeGlobalDuration[animIndex - 1] : TimeFrameZero();
	const float prevDurationSec = ToSeconds(prevDuration);
	float localTimeSec = global.m_timeSec - prevDurationSec;

	localTimeSec = Max(0.0f, localTimeSec); // floating-point error may cause this to go slightly negative, so clamp it

	SetFromSeconds(animIndex, localTimeSec, animChain);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::LocalTimeIndex::SetFromSeconds(int animIndex, float localTimeSec, const AnimChain& animChain)
{
	float durationSec = animChain.GetDurationSeconds(kTimeModeLocal, animIndex);
	m_animIndex = animIndex;
	m_timeSec = localTimeSec;
	m_phase = SafeDivide(m_timeSec, durationSec);

	float mayaStartFrame = (float)animChain.GetMayaStartFrame(animIndex);
	float mayaEndFrame = (float)animChain.GetMayaEndFrame(animIndex);
	m_mayaFrame = mayaStartFrame + (m_phase * (mayaEndFrame - mayaStartFrame));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::LocalTimeIndex::SetFromPhase(int animIndex, float localPhase, const AnimChain& animChain)
{
	float durationSec = animChain.GetDurationSeconds(kTimeModeLocal, animIndex);
	m_animIndex = animIndex;
	m_phase = localPhase;
	m_timeSec = m_phase * durationSec;

	float mayaStartFrame = (float)animChain.GetMayaStartFrame(animIndex);
	float mayaEndFrame = (float)animChain.GetMayaEndFrame(animIndex);
	m_mayaFrame = mayaStartFrame + (m_phase * (mayaEndFrame - mayaStartFrame));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::LocalTimeIndex::SetFromFrame(int animIndex, float localFrameIndex, const AnimChain& animChain)
{
	float durationSec = animChain.GetDurationSeconds(kTimeModeLocal, animIndex);
	m_animIndex = animIndex;
	m_timeSec = SafeDivide(localFrameIndex, GetFramesPerSecond());
	m_phase = SafeDivide(m_timeSec, durationSec);

	float mayaStartFrame = (float)animChain.GetMayaStartFrame(animIndex);
	float mayaEndFrame = (float)animChain.GetMayaEndFrame(animIndex);
	m_mayaFrame = mayaStartFrame + (m_phase * (mayaEndFrame - mayaStartFrame));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::LocalTimeIndex::SetToInvalid()
{
	m_timeSec = -1.0f;
	m_phase = -1.0f;
	m_mayaFrame = -1.0f;
	m_animIndex = -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimChain::GlobalTimeIndex::GlobalTimeIndex(const LocalTimeIndex& local, const AnimChain& animChain)
{
	SetFrom(local, animChain);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::GlobalTimeIndex::Clear()
{
	m_timeSec = m_phase = m_mayaFrame = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::GlobalTimeIndex::SetFrom(const LocalTimeIndex& local, const AnimChain& animChain)
{
	int i = local.m_animIndex;
	if (i >= 1)
	{
		if (i >= animChain.m_numAnims)
		{
			i = animChain.m_numAnims - 1; // clamp to valid range
		}

		const TimeFrame prevDuration = animChain.m_aCumulativeGlobalDuration[i - 1];
		const float prevDurationSec = ToSeconds(prevDuration);

		m_timeSec = prevDurationSec + local.m_timeSec;
	}
	else
	{
		m_timeSec = local.m_timeSec;
	}

	const float totalDurationSec = animChain.GetDurationSeconds(kTimeModeGlobal);
	m_phase = SafeDivide(m_timeSec, totalDurationSec); // NB: dividing by duration is more accurate than multiplying by 1/duration

	m_mayaFrame = animChain.GetMayaStartFrame(local.GetAnimIndex()) + local.AsFrame();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::GlobalTimeIndex::SetFromSeconds(float globalTimeSec, const AnimChain& animChain, bool allowOutOfBounds)
{
	const float durationSec = animChain.GetDurationSeconds(kTimeModeGlobal);

	m_timeSec = globalTimeSec;
	m_phase = SafeDivide(globalTimeSec, durationSec); // NB: dividing by duration is more accurate than multiplying by 1/duration

	ANIM_ASSERT(allowOutOfBounds || (m_timeSec >= 0.0f && m_timeSec <= durationSec));
	ANIM_ASSERT(allowOutOfBounds || (m_phase >= 0.0f && m_phase <= 1.0f));

	LocalTimeIndex local;
	local.SetFrom(*this, animChain);
	m_mayaFrame = animChain.GetMayaStartFrame(local.GetAnimIndex()) + local.AsFrame();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::GlobalTimeIndex::SetFromPhase(float globalPhase, const AnimChain& animChain, bool allowOutOfBounds)
{
	float durationSec = animChain.GetDurationSeconds(kTimeModeGlobal);

	m_phase = globalPhase;
	m_timeSec = globalPhase * durationSec;

	ANIM_ASSERT(allowOutOfBounds || (m_timeSec >= 0.0f && m_timeSec <= durationSec));
	ANIM_ASSERT(allowOutOfBounds || (m_phase >= 0.0f && m_phase <= 1.0f));

	LocalTimeIndex local;
	local.SetFrom(*this, animChain);
	m_mayaFrame = animChain.GetMayaStartFrame(local.GetAnimIndex()) + local.AsFrame();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::GlobalTimeIndex::SetFromFrame(float frameIndex, const AnimChain& animChain, bool allowOutOfBounds)
{
	float timeSec = frameIndex / AnimChain::GetFramesPerSecond();
	SetFromSeconds(timeSec, animChain, allowOutOfBounds);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChain::GlobalTimeIndex::SetFromMayaFrame(float mayaFrameIndex, const AnimChain& animChain, bool allowOutOfBounds)
{
	float frameIndex = mayaFrameIndex - animChain.GetMayaStartFrame(0);
	float timeSec = frameIndex / AnimChain::GetFramesPerSecond();
	SetFromSeconds(timeSec, animChain, allowOutOfBounds);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimChain::AdvanceStatus AnimChain::SetGlobalTimeSec(float globalTimeSec) // only used for shuttling (time warp)
{
	float globalDurationSec = GetDurationSeconds(kTimeModeGlobal);

	GlobalTimeIndex global;
	global.SetFromSeconds(globalTimeSec, *this);

	LocalTimeIndex local;
	local.SetFrom(global, *this);

	AdvanceStatus status = (m_localTimeIndex.m_animIndex != local.m_animIndex) ? kChangeIndex : kNoChange;

	m_localTimeIndex = local;
	m_globalTimeIndex = global;
	m_prevGlobalTimeIndex = m_globalTimeIndex;
	m_lastFramePrevUpdated = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;

	return status;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimChain::AdvanceStatus AnimChain::GetStatusForNextFrameLocalPhase(F32 localPhaseCurAnim, int* pAnimIndexDelta, int* pNewAnimIndex)
{
	const char* animName = DevKitOnly_StringIdToStringOrHex(GetAnimId());

	float overflowPhase = localPhaseCurAnim - 1.0f;
	float underflowPhase = localPhaseCurAnim;
	if (overflowPhase >= 0.0f || underflowPhase < 0.0f)
	{
		LocalTimeIndex local;
		local.SetFromPhase(m_localTimeIndex.m_animIndex, localPhaseCurAnim, *this); // create a new local time index

		GlobalTimeIndex global; // convert to global
		global.SetFrom(local, *this);

		#if ANIM_ENABLE_ANIM_CHAIN_TRACE
			if (FALSE_IN_FINAL_BUILD(g_animChainDebugTrace))
				MsgCinematic("%08x: GetStatusForNextFrameLocalPhase(): %s: ai=%d lpca=%.6f global=%.3f\n", EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, animName, m_localTimeIndex.m_animIndex, localPhaseCurAnim, global.AsTimeSec());
		#endif

		// Search forward (or back) until we find a new anim index that encompasses this global time index.
		// Normally this loop will run exactly one iteration. However, in degenerate cases (e.g. a
		// chain that ends prematurely and contains a tail of 0-duration clips, or when the overflow
		// is so large as to actually SKIP one or more animations in the chain) the loop may run
		// multiple iterations.

		const int indexDelta = (overflowPhase >= 0.0f) ? 1 : -1;
		if (pAnimIndexDelta)
			*pAnimIndexDelta = indexDelta;

		int newAnimIndex = m_localTimeIndex.m_animIndex + indexDelta;
		if (IsLooping())
		{
			newAnimIndex = (m_numAnims + newAnimIndex) % m_numAnims;
			global.SetFromPhase(fmodf(global.AsPhase() + 1.0f, 1.0f), *this);
		}
		int numIterations = 0;
		while (newAnimIndex < m_numAnims && newAnimIndex >= 0 && numIterations < m_numAnims) // never advance past the end of the chain, or the start of the chain
		{
			LocalTimeIndex newLocal; // determine the new local time index
			newLocal.SetFrom(newAnimIndex, global, *this);

			#if ANIM_ENABLE_ANIM_CHAIN_TRACE
				if (FALSE_IN_FINAL_BUILD(g_animChainDebugTrace))
					MsgCinematic("%08x: GetStatusForNextFrameLocalPhase(): %s: trying ai=%d newLocal=%.6f\n", EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, animName, newAnimIndex, newLocal.AsPhase());
			#endif

			if (indexDelta > 0)
			{
				ANIM_ASSERTF(newLocal.AsPhase() >= 0.0f, ("Invalid (negative) local phase in current anim within the chain"));
				if (newLocal.AsPhase() < 1.0f)
				{
					#if ANIM_ENABLE_ANIM_CHAIN_TRACE
						if (FALSE_IN_FINAL_BUILD(g_animChainDebugTrace))
							MsgCinematic("%08x: GetStatusForNextFrameLocalPhase(): %s: SET TO ai=%d newLocal=%.6f\n", EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, animName, newAnimIndex, newLocal.AsPhase());
					#endif

					if (pNewAnimIndex)
						*pNewAnimIndex = newAnimIndex;
					return kChangeIndex;
				}
			}
			else
			{
				ANIM_ASSERTF(newLocal.AsPhase() < 1.0f, ("Invalid local phase in current anim within the chain"));
				if (newLocal.AsPhase() >= 0.0f)
				{
					#if ANIM_ENABLE_ANIM_CHAIN_TRACE
						if (FALSE_IN_FINAL_BUILD(g_animChainDebugTrace))
							MsgCinematic("%08x: GetStatusForNextFrameLocalPhase(): %s: SET TO ai=%d newLocal=%.6f\n", EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, animName, newAnimIndex, newLocal.AsPhase());
					#endif

					if (pNewAnimIndex)
						*pNewAnimIndex = newAnimIndex;
					return kChangeIndex;
				}
			}

			++numIterations;
			newAnimIndex += indexDelta;
			if (IsLooping())
				newAnimIndex = (m_numAnims + newAnimIndex) % m_numAnims;
		}
	}

	#if ANIM_ENABLE_ANIM_CHAIN_TRACE
		if (FALSE_IN_FINAL_BUILD(g_animChainDebugTrace))
			MsgCinematic("%08x: GetStatusForNextFrameLocalPhase(): %s: NO CHANGE\n", EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, animName);
	#endif

	if (pAnimIndexDelta)
		*pAnimIndexDelta = 0;
	if (pNewAnimIndex)
		*pNewAnimIndex = m_localTimeIndex.m_animIndex;

	ANIM_ASSERTF(localPhaseCurAnim >= 0.0f, ("Invalid (negative) local phase in current anim within the chain"));
	return kNoChange;
}

// -------------------------------------------------------------------------------------------------
void AnimChain::ChangeAnimIndexByNextFrameLocalPhase(F32 localPhaseCurAnim)
{
	// Latch in a new animation in the chain, using the (overflowed) phase from the previous animation.

	float overflowPhase = localPhaseCurAnim - 1.0f;
	float underflowPhase = localPhaseCurAnim;
	ANIM_ASSERT(underflowPhase < 0.0f || overflowPhase >= 0.0f); // either playing in reverse or forward

	LocalTimeIndex local;
	local.SetFromPhase(m_localTimeIndex.m_animIndex, localPhaseCurAnim, *this); // create a new local time index

	UpdatePreviousGlobalTimeIndex();
	m_globalTimeIndex.SetFrom(local, *this); // convert to global

	// Search forward (or back) until we find a new anim index that encompasses this global time index.
	// Normally this loop will run exactly one iteration. However, in degenerate cases (e.g. a
	// chain that ends prematurely and contains a tail of 0-duration clips, or when the overflow
	// is so large as to actually SKIP one or more animations in the chain) the loop may run
	// multiple iterations.
	int indexDelta = (overflowPhase >= 0.0f) ? 1 : -1;
	int newAnimIndex = m_localTimeIndex.m_animIndex + indexDelta;
	if (IsLooping())
	{
		newAnimIndex = (m_numAnims + newAnimIndex) % m_numAnims;
		m_globalTimeIndex.SetFromPhase(fmodf(m_globalTimeIndex.AsPhase() + 1.0f, 1.0f), *this);
	}
	const int lastAnimIndex = m_numAnims - 1;
	int numIterations = 0;
	while (newAnimIndex < m_numAnims && newAnimIndex >= 0 && numIterations < m_numAnims) // never advance past the end of the chain, or the start of the chain
	{
		LocalTimeIndex newLocal; // determine the new local time index
		newLocal.SetFrom(newAnimIndex, m_globalTimeIndex, *this);

		if (indexDelta > 0)
		{
			ANIM_ASSERTF(newLocal.AsPhase() >= 0.0f, ("Invalid (negative) local phase in current anim within the chain"));
			if (newLocal.AsPhase() < 1.0f || newAnimIndex == lastAnimIndex)
			{
				m_localTimeIndex = newLocal;

				ANIM_ASSERT(m_localTimeIndex.AsPhase() >= 0.0f);
				ANIM_ASSERTF(m_localTimeIndex.AsPhase() < 1.0f, ("The overflow must have been so large as to skip an entire animation in the chain!"));

				return;
			}
		}
		else
		{
			ANIM_ASSERTF(newLocal.AsPhase() < 1.0f, ("Invalid local phase in current anim within the chain"));
			if (newLocal.AsPhase() >= 0.0f || newAnimIndex == 0)
			{
				m_localTimeIndex = newLocal;

				ANIM_ASSERT(m_localTimeIndex.AsPhase() >= 0.0f);
				ANIM_ASSERTF(m_localTimeIndex.AsPhase() < 1.0f, ("The overflow must have been so large as to skip an entire animation in the chain!"));

				return;
			}
		}

		++numIterations;
		newAnimIndex += indexDelta;
		if (IsLooping())
			newAnimIndex = (m_numAnims + newAnimIndex) % m_numAnims;
	}

	ANIM_ASSERTF(m_numAnims == 0, ("Bug in the above loop - it should never drop through to here if m_numAnims!=0"));
	m_localTimeIndex.SetFrom(0, m_globalTimeIndex, *this);
}

void AnimChain::UpdatePreviousGlobalTimeIndex()
{
	if (EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused != m_lastFramePrevUpdated)
	{
		m_prevGlobalTimeIndex = m_globalTimeIndex;
		m_lastFramePrevUpdated = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	}
}

// -------------------------------------------------------------------------------------------------
void AnimChain::UpdateLocalPhase(F32 localPhase)
{
	// This function NEVER permits the local anim index to change, even if the local phase passes 1.0f
	// (which *is* valid at the end of a chain, for example). Changes in anim index can only be made by
	// calling SetGlobalTimeSec() (to "warp" or "teleport" the time index), or by first calling
	// GetStatusForNextFrameLocalPhase() and, iff it returns kChangeIndex, calling
	// ChangeAnimIndexByNextFrameLocalPhase().

	m_localTimeIndex.SetFromPhase(m_localTimeIndex.m_animIndex, localPhase, *this);
	
	UpdatePreviousGlobalTimeIndex();
	m_globalTimeIndex.SetFrom(m_localTimeIndex, *this); // convert to global

	if (m_overlappedMayaFrames)
		ReportMayaFramesOverlapped(*this);
}

// -------------------------------------------------------------------------------------------------
void AnimChain::SetClockScale(const FgAnimData* pAnimData, float scale)
{
	if (pAnimData)
		const_cast<FgAnimData*>(pAnimData)->m_animClockScale = scale; // FgAnimData is returned as const everywhere, but it really shouldn't be, so do this for caller convenience
	m_clockScale = scale;
}

// -------------------------------------------------------------------------------------------------
void AnimChain::SyncToAudio(const FgAnimData* pAnimData, float audioTimeSec)
{
	if (m_audioSyncDisabled)
		return;

	#if !FINAL_BUILD
	m_audioSyncPid.SetGainValues(kAudioSync_P, kAudioSync_I, kAudioSync_D, kAudioSync_maxError); // only so we can live-tweak the gains
	#endif

	float dt = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetDeltaTimeInSeconds();
	if (dt > 0.001f)
	{
		float animTimeSec = GetGlobalTimeIndex().AsTimeSec();
		float errorSec = audioTimeSec - animTimeSec;
		float absErrorSec = Abs(errorSec);

		if (m_audioSyncActive)
		{
			if (absErrorSec < kAudioSync_errorThreshTight)
				m_audioSyncActive = false;
		}
		else if (!m_audioSyncActive)
		{
			if (absErrorSec >= kAudioSync_errorThreshLoose)
				m_audioSyncActive = true;
		}

		float animClockScale = 1.0f;
		if (m_audioSyncActive && !g_animOptions.IsAudioSyncDisabled())
		{
			float controlVal = m_audioSyncPid.Update(errorSec, dt);

			animClockScale = 1.0f + (controlVal / dt);

			const float forcedSpeedFactor = EngineComponents::GetNdFrameState()->GetFinalSpeedMultiplier()
										  * EngineComponents::GetNdFrameState()->m_cinematicScrubMultiplier;
			const float maxScale = FALSE_IN_FINAL_BUILD(forcedSpeedFactor > 1.0f) ? kAudioSync_maxScale * 2.0f : kAudioSync_maxScale;

			animClockScale = MinMax(animClockScale, kAudioSync_minScale, maxScale); // clamp to +/-10% adjustment
		}
		else
		{
			m_audioSyncPid.Reset();
		}

		ANIM_ASSERT(pAnimData);
		SetClockScale(pAnimData, animClockScale);

		if (m_pAudioSyncClockScaleHistory)
		{
			if (m_pAudioSyncClockScaleHistory->IsFull())
				m_pAudioSyncClockScaleHistory->Dequeue();
			m_pAudioSyncClockScaleHistory->Enqueue(animClockScale);
		}
	}
}

// -------------------------------------------------------------------------------------------------
void AnimChain::SetAudioSyncEnabled(bool enable)
{
	m_audioSyncDisabled = !enable;

	if (m_audioSyncDisabled)
	{
		m_clockScale = 1.0f;
		m_audioSyncActive = false;
	}
}

// -------------------------------------------------------------------------------------------------
F32 AnimChain::GetFadeInSec(int animIndex) const
{
	if (animIndex < 0)
		animIndex = m_localTimeIndex.m_animIndex;

	if (animIndex == 0)
		return m_globalFadeInSec;
	else
		return 0.0f;
}

// -------------------------------------------------------------------------------------------------
F32 AnimChain::GetFadeOutSec(int animIndex) const
{
	if (animIndex < 0)
		animIndex = m_localTimeIndex.m_animIndex;

	if (animIndex == m_numAnims - 1)
		return m_globalFadeOutSec;
	else
		return 0.0f;
}

// -------------------------------------------------------------------------------------------------
F32 AnimChain::GetEndPhase(int animIndex) const
{
	if (animIndex < 0)
		animIndex = m_localTimeIndex.m_animIndex;

	if (m_localEnd.IsValid() && animIndex == m_localEnd.m_animIndex)
		return m_localEnd.AsPhase();
	else
		return -1.0f;
}

// -------------------------------------------------------------------------------------------------
void AnimChain::AppendAnim(const AnimInfo& animInfo, AnimControl* pAnimControl)
{
	I32 index = m_numAnims++;

	ANIM_ASSERT(index >= 0);
	ANIM_ASSERTF(index < kMaxAnims, ("AnimChain: Please increase kMaxAnims"));

	AnimInfo* pAnimInfo = &m_aAnimInfo[index];
	*pAnimInfo = animInfo; // slice copy

	m_aAnimInfo[index].m_anim = pAnimControl->LookupAnim(animInfo.m_animId);
	if (!m_aAnimInfo[index].m_anim.ToArtItem() && !m_aAnimInfo[index].m_disabled)
	{
		m_aAnimInfo[index].m_disabled = true;
		m_aAnimInfo[index].m_missing = true;
	}
	else
	{
		m_aAnimInfo[index].m_missing = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimChain::IsValid() const
{
	return (m_numAnims != 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*static*/ TimeFrame AnimChain::GetAnimDuration(const AnimInfoInternal& animInfo)
{
	if (animInfo.m_disabled)
	{
		I64 durationFrames = (I64)Max(animInfo.m_mayaEndFrame - animInfo.m_mayaStartFrame, 0);
		I64 timeFramesPerFrame = (I64)kTimeFramesPerSecond / (I64)GetFramesPerSecond(); // kTimeFramesPerSecond should be evenly divisible by GetFramesPerSecond() (defined to be 30)
		I64 durationTimeFrames = durationFrames * timeFramesPerFrame;
		return CreateTimeFrame(durationTimeFrames);
	}

	const ArtItemAnim* pAnim = animInfo.m_anim.ToArtItem();
	if (pAnim)
	{
		const ndanim::ClipData* pClipData = pAnim->m_pClipData;
		if (pClipData)
		{
			// We do not want to lose ANY accuracy to floating-point error, because clip durations must
			// be exact so we can convert from local phase to global seconds and back to local phase in
			// the next animation clip without ever losing "energy" (i.e., time) from the system.
			const I64 clipFrameIntervals = (I64)pClipData->m_fNumFrameIntervals;

			const I64 timeFramesPerClipFrame = (I64)kTimeFramesPerSecond / (I64)pClipData->m_framesPerSecond;
			#if ENABLE_ASSERT
			{
				const float fTimeFramesPerClipFrame = (kTimeFramesPerSecond / pClipData->m_framesPerSecond);
				ANIM_ASSERTF(fTimeFramesPerClipFrame == floorf(fTimeFramesPerClipFrame), ("kTimeFramesPerSecond should be evenly divisible by all possible values of m_framesPerSecond (30, 15, 10)"));
			}
			#endif

			const I64 clipTimeFrames = clipFrameIntervals * timeFramesPerClipFrame;
			TimeFrame clipDuration = CreateTimeFrame(clipTimeFrames);

			return clipDuration;
		}
	}
	return TimeFrameZero();
}

/// --------------------------------------------------------------------------------------------------------------- ///
TimeFrame AnimChain::GetDuration(TimeMode timeMode, int animIndex) const
{
	TimeFrame duration;

	if (timeMode == kTimeModeLocal)
	{
		animIndex = ResolveAnimIndex(animIndex); // negative means "use current anim"
		duration = GetAnimDuration(m_aAnimInfo[animIndex]);
	}
	else if (m_numAnims > 0)
	{
		duration = m_aCumulativeGlobalDuration[m_numAnims - 1];
	}
	else
	{
		duration = TimeFrameZero();
	}

	return duration;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int AnimChain::GetDurationFrames(TimeMode timeMode, int animIndex) const
{
	static const TimeFrame::Storage kTimeFramesPer30HzFrame = kTimeFramesPerSecond / 30; // 1 frame == 1/30 sec, indep of the sample rate of the anim used for compression purposes

	TimeFrame duration = GetDuration(timeMode, animIndex);
	TimeFrame::Storage durationTimeFrames = duration.GetRaw();
	TimeFrame::Storage durationFrames = durationTimeFrames / kTimeFramesPer30HzFrame;
	ANIM_ASSERTF(durationTimeFrames % kTimeFramesPer30HzFrame == 0, ("Animation durations should always be multiples of 1/30 second"));

	return durationFrames;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AnimChain::GetDurationSeconds(TimeMode timeMode, I32 animIndex) const
{
	TimeFrame duration = GetDuration(timeMode, animIndex);
	float durationSeconds = ToSeconds(duration);
	return durationSeconds;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int AnimChain::GetCameraIndex(int animIndex) const
{
	if (animIndex < 0)
		animIndex = m_localTimeIndex.m_animIndex;

	ANIM_ASSERT(animIndex >= 0 && animIndex < m_numAnims);

	return m_aAnimInfo[animIndex].m_cameraIndex;
}
