/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIM_CHAIN_H
#define ANIM_CHAIN_H

#include "corelib/containers/ringqueue.h"
#include "corelib/math/float-utils.h"
#include "corelib/util/timeframe.h"
#include "ndlib/util/pid-controller.h"
#include "ndlib/resource/resource-table.h"

class AnimControl;
class ArtItemAnim;
namespace ndanim {
struct ClipData;
}  // namespace ndanim
struct FgAnimData;

/// --------------------------------------------------------------------------------------------------------------- ///
/// An AnimChain encapsulates a chain of animations that are to be played back-to-back. This is a temp hack until
/// we can make ArtItemAnim into a chain of clips. This class is currently used only by the cinematic system.
/// --------------------------------------------------------------------------------------------------------------- ///

class AnimChain
{
public:
	static float GetSecondsPerFrame() { return 1.0f/30.0f; } // 1 frame == 1/30 sec, indep of the sample rate of the anim used for compression purposes
	static float GetFramesPerSecond() { return 30.0f; }      // 1 frame == 1/30 sec, indep of the sample rate of the anim used for compression purposes

	static const int  kMaxAnims = 16;
	enum TimeMode
	{
		kTimeModeLocal,
		kTimeModeGlobal,
		kTimeModeCount
	};

	enum AdvanceStatus
	{
		kNoChange,
		kChangeIndex,
	};

	class GlobalTimeIndex;

	class LocalTimeIndex
	{
	public:
		LocalTimeIndex() { }
		LocalTimeIndex(const GlobalTimeIndex& global, const AnimChain& animChain);

		void Clear();
		void SetFrom(const GlobalTimeIndex& global, const AnimChain& animChain);
		void SetFrom(int animIndex, const GlobalTimeIndex& global, const AnimChain& animChain);
		void SetFromSeconds(int animIndex, float localTimeSec, const AnimChain& animChain);
		void SetFromPhase(int animIndex, float localPhase, const AnimChain& animChain);
		void SetFromFrame(int animIndex, float localFrameIndex, const AnimChain& animChain);
		void SetToInvalid();

		int GetAnimIndex() const { return m_animIndex; }

		float AsTimeSec() const { return m_timeSec; }
		float AsPhase() const { return m_phase; }
		float AsFrame() const { return m_timeSec * AnimChain::GetFramesPerSecond(); }
		float AsMayaFrame() const { return m_mayaFrame; }
		bool IsValid() const { return (m_animIndex >= 0); }

		I32		m_animIndex;
		F32		m_timeSec;
		F32		m_phase;
		F32		m_mayaFrame;
	};

	class GlobalTimeIndex
	{
	public:
		GlobalTimeIndex() { }
		GlobalTimeIndex(const LocalTimeIndex& local, const AnimChain& animChain);

		void Clear();
		void SetFrom(const LocalTimeIndex& local, const AnimChain& animChain);
		void SetFromSeconds(float globalTimeSec, const AnimChain& animChain, bool allowOutOfBounds = false);
		void SetFromPhase(float globalPhase, const AnimChain& animChain, bool allowOutOfBounds = false);
		void SetFromFrame(float frameIndex, const AnimChain& animChain, bool allowOutOfBounds = false);
		void SetFromMayaFrame(float frameIndex, const AnimChain& animChain, bool allowOutOfBounds = false);

		float AsTimeSec() const { return m_timeSec; }
		float AsPhase() const { return m_phase; }
		float AsFrame() const { return (m_timeSec * AnimChain::GetFramesPerSecond()); }
		float AsMayaFrame() const { return m_mayaFrame; }

		F32		m_timeSec;
		F32		m_phase;
		F32		m_mayaFrame;
	};

	class TimeInterval // represents an (open, closed] time interval, except when the start time is 0 in which case it is [closed @ 0, closed]
	{
	public:
		TimeInterval() { m_timeSec[0] = m_timeSec[1] = 0.0f; }
		TimeInterval(float timeSec0, float timeSec1) { Set(timeSec0, timeSec1); }

		void Set(float timeSec0, float timeSec1)
		{
			m_timeSec[0] = (timeSec0 == 0.0f) ? DecrementBy1Ulp(0.0f) : timeSec0;
			m_timeSec[1] = timeSec1;
		}

		bool ContainsTimeIndex(float timeSec) const
		{
			return (m_timeSec[0] < timeSec && timeSec <= m_timeSec[1]);
		}

		F32		m_timeSec[2];
	};

	struct AnimInfo
	{
		StringId64	m_animId;
		I32			m_mayaStartFrame;
		I32			m_mayaEndFrame;
		I16			m_cameraIndex;
		bool		m_disabled;	// is the actor disabled/not present/invisible during this part of the chain?
		bool		m_missing; // is this animation AWOL?
	};

public:
	static void StartUp();
	static void ShutDown();
	static AnimChain* PoolAlloc(const char* debugInfo);
	static AnimChain* PoolFree(AnimChain* pAnimChain, const char* debugInfo);
	static int GetPoolUsedCount();

	AnimChain();
	//AnimChain(const AnimChain& other);			// POD, shallow copy is sufficient
	//AnimChain& operator=(const AnimChain& other);	// POD, shallow copy is sufficient

	void Init(StringId64 cinematicId, StringId64 chainAnimId, int numAnims, const AnimInfo aAnimInfo[], float globalStartPhase, AnimControl* pAnimControl);
	void InitLocalStartPhase(int animIndex, float localStartPhase);
	void InitGlobalStartTime(const GlobalTimeIndex& globalTime);
	void Destroy();
	void Clear();

	void RefreshIsMaster();
	bool IsMaster() const;

	void RefreshIsCamera();
	bool IsCamera() const;

	// QUERIES

	bool IsValid() const;
	bool IsSameChain(const AnimChain* pOther) const;

	StringId64 GetCinematicId() const { return m_cinematicId; }

	StringId64 GetChainAnimId() const { return m_chainAnimId; } // virtual animation id representing the entire chain

	int GetNumAnimations() const { return m_numAnims; }

	void RequestLooping(bool looping) { m_looping = looping; }
	bool IsLooping() const { return (m_looping && m_numAnims > 1); }

	void SetGlobalFadeInSec(float seconds);
	void SetGlobalFadeOutSec(float seconds);
	void SetGlobalEndPhase(float globalEndPhase);
	float GetFadeInSec(int animIndex = -1) const;
	float GetFadeOutSec(int animIndex = -1) const;
	float GetEndPhase(int animIndex = -1) const;

	float GetDurationSeconds(TimeMode timeMode = kTimeModeGlobal, int animIndex = -1) const;
	int GetDurationFrames(TimeMode timeMode = kTimeModeGlobal, int animIndex = -1) const; // 1 frame == 1/30 sec, indep of the sample rate of the anim used for compression purposes
	TimeFrame GetDuration(TimeMode timeMode = kTimeModeGlobal, int animIndex = -1) const;

	int GetMayaStartFrame(int animIndex = -1) const
	{
		if (animIndex < 0 || animIndex >= m_numAnims)
			animIndex = m_localTimeIndex.m_animIndex;
		return m_aAnimInfo[animIndex].m_mayaStartFrame;
	}

	int GetMayaEndFrame(int animIndex = -1) const
	{
		if (animIndex < 0 || animIndex >= m_numAnims)
			animIndex = m_localTimeIndex.m_animIndex;
		return m_aAnimInfo[animIndex].m_mayaEndFrame;
	}

	int GetCameraIndex(int animIndex = -1) const;

	// TIME INDEX

	AdvanceStatus SetGlobalTimeSec(float globalTimeSec); // only used for shuttling (time warp)

	AdvanceStatus GetStatusForNextFrameLocalPhase(float localPhaseCurAnim, int* pAnimIndexDelta = nullptr, int* pNewAnimIndex = nullptr);
	void ChangeAnimIndexByNextFrameLocalPhase(float localPhaseCurAnim);
	void UpdateLocalPhase(float localPhase);

	LocalTimeIndex GetLocalTimeIndex() const { return m_localTimeIndex; }
	GlobalTimeIndex GetGlobalTimeIndex() const { return m_globalTimeIndex; }

	float GetCurrentLocalPhase() const { return m_localTimeIndex.m_phase; }
	int GetCurrentAnimIndex() const { return m_localTimeIndex.m_animIndex; }

	void SetClockScale(const FgAnimData* pAnimData, float scale);
	float GetClockScale() const { return m_clockScale; }

	GlobalTimeIndex GetPreviousGlobalTimeIndex() const { return m_prevGlobalTimeIndex; }
	TimeInterval GetCurrentTimeInterval() const { return TimeInterval(m_prevGlobalTimeIndex.m_timeSec, m_globalTimeIndex.m_timeSec); }
	bool IsCurrentGlobalTimeSeconds(float globalTimeSec) const { return GetCurrentTimeInterval().ContainsTimeIndex(globalTimeSec); }

	// AUDIO SYNC

	void SyncToAudio(const FgAnimData* pAnimData, float audioTimeSec);
	void SetAudioSyncEnabled(bool enable);

	// DIRECT ACCESS TO ANIMS

	int ResolveAnimIndex(int animIndex = -1) const
	{
		if (animIndex < 0 || animIndex >= m_numAnims)
			animIndex = m_localTimeIndex.m_animIndex;
		return animIndex;
	}

	StringId64 GetAnimId(int animIndex = -1) const
	{
		return m_aAnimInfo[ResolveAnimIndex(animIndex)].m_animId;
	}

	bool IsAnimDisabled(int animIndex = -1) const
	{
		return m_aAnimInfo[ResolveAnimIndex(animIndex)].m_disabled;
	}

	bool IsAnimMissing(int animIndex = -1) const
	{
		return m_aAnimInfo[ResolveAnimIndex(animIndex)].m_missing;
	}

	ArtItemAnimHandle GetAnim(int animIndex = -1) const
	{
		return m_aAnimInfo[ResolveAnimIndex(animIndex)].m_anim;
	}

	const ndanim::ClipData* GetClip(int index) const;

	// DEBUGGING

	typedef StaticRingQueue<float, 100> AudioSyncClockScaleHistory;
	void AllocAudioSyncClockScaleHistory();
	const AudioSyncClockScaleHistory* GetAudioSyncClockScaleHistory() const { return m_pAudioSyncClockScaleHistory; }
	bool IsAudioSyncActive() const { return m_audioSyncActive; }

private:
	friend class LocalTimeIndex;
	friend class GlobalTimeIndex;

	struct AnimInfoInternal : public AnimInfo
	{
		ArtItemAnimHandle		m_anim;
	};

	void AppendAnim(const AnimInfo& animInfo, AnimControl* pAnimControl);
	static TimeFrame GetAnimDuration(const AnimInfoInternal& animInfo);
	void UpdatePreviousGlobalTimeIndex();

	AnimInfoInternal			m_aAnimInfo[kMaxAnims];
	I32							m_numAnims;

	StringId64					m_cinematicId;
	StringId64					m_chainAnimId;

	F32							m_clockScale;
	F32							m_globalFadeInSec;  // fade in for entire chain, applied to anim[0]
	F32							m_globalFadeOutSec; // fade out for entire chain, applied anim[N-1]
	LocalTimeIndex				m_localEnd;			// end phase in "local space" (i.e. in terms of the phase of the last anim in the chain)

	TimeFrame					m_aCumulativeGlobalDuration[kMaxAnims];

	LocalTimeIndex				m_localTimeIndex;
	GlobalTimeIndex				m_globalTimeIndex;
	GlobalTimeIndex				m_prevGlobalTimeIndex;
	I64							m_lastFramePrevUpdated = -1;

	PidController				m_audioSyncPid;
	AudioSyncClockScaleHistory*	m_pAudioSyncClockScaleHistory;

	I64							m_lastMasterGameFrame;
	I64							m_lastCameraGameFrame;
	bool						m_audioSyncActive;
	bool						m_audioSyncDisabled;
	bool						m_looping;
	bool						m_overlappedMayaFrames;
};

#endif // ANIM_CHAIN_H
