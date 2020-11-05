/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/msg.h"

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-instance.h"
#include "ndlib/process/bound-frame.h"

#include "gamelib/level/art-item-anim.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimTable;
namespace ndanim
{
	struct SharedTimeIndex; 
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// A dummy instance that represents a "fake" animation that isn't really playing, but must keep track of time as
/// though it really were playing (and keep in sync with AnimSimpleInstances that may be playing simultaneously).
class AnimDummyInstance : public AnimInstance
{
public:
	static void StartUp();
	static void ShutDown();
	static AnimDummyInstance* PoolAlloc();
	static AnimDummyInstance* PoolFree(AnimDummyInstance* pInstance);
	static int GetPoolUsedCount();

	AnimDummyInstance();

	virtual bool IsSimple() const override { return true; } // acts like an AnimSimpleInstance

	void Init(ID id,
	          const StringId64 animId,
	          F32 num30HzFrameIntervals,
	          F32 startPhase,
	          F32 playbackRate,
	          bool looping = false,
			  ndanim::SharedTimeIndex* pSharedTime = nullptr);

	virtual ID GetId() const override { return m_id; }

	bool IsValid() const;

	bool IsLooping() const;
	void SetLooping(bool onoff);

	virtual bool IsFlipped() const override { return false; }
	void SetFlipped(bool f);

	virtual F32 GetMayaFrame() const override;
	F32 GetSample() const { return m_curSample; }

	virtual F32 GetPhase() const override;
	F32 GetFramesPerSecond() const;
	virtual U32 GetFrameCount() const override;
	virtual F32 GetDuration() const override;

	virtual F32 GetPrevPhase() const override;
	virtual F32 GetPrevMayaFrame() const override;

	virtual float GetFade() const override { return HasFadedOut() ? 0.0f : 1.0f; }

	void SetPlaybackRate(F32 playbackRate);
	F32 GetPlaybackRate() const { return m_playbackRate; }

	void SetPhase(F32 phase, bool bSingleStepping = false);
	void SetFrame(F32 f, bool bSingleStepping = false);

	void SetAnim(const StringId64 animId, F32 num30HzFrameIntervals, bool looping = false);
	StringId64 GetAnimId() const { return m_animId; }
	const ndanim::ClipData* GetClip() const { return (m_animId != INVALID_STRING_ID_64) ? &m_clip : nullptr; }

	void PhaseUpdate(F32 deltaTime);

	void DebugPrint(MsgOutput output) const;

	virtual bool IsFrozen() const override { return m_flags.m_frozen; }
	virtual void SetFrozen(bool f) override { m_flags.m_frozen = f; }

	virtual void SetSkipPhaseUpdateThisFrame(bool f) override { m_flags.m_skipPhaseUpdateThisFrame = f; }

	bool DidCameraCutThisFrame() const { return m_flags.m_cameraCutThisFrame; }
	float GetRemainderTime() const	{ return m_remainderTime; }

	void FadeOut(float fadeTimeSec);
	bool HasFadedOut() const { return (m_fadeOutTimeLeft == 0.0f); }

	void SetCameraCutAnim(const ArtItemAnim* pAnim);

	// UNUSED API
	virtual const AnimTable* GetAnimTable() const override { return nullptr; }
	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override { }
	virtual void SetApOrigin(const BoundFrame& apRef) override { }
	virtual const BoundFrame& GetApOrigin() const override;
	virtual StringId64 GetLayerId() const override { return INVALID_STRING_ID_64; }
private:

	void SetPhaseInternal(F32 phase);

	union Flags
	{
		U16 m_raw;
		struct
		{
			bool m_flipped : 1;  // output should be x-flipped
			bool m_looping : 1;
			bool m_frozen : 1; // phase updates stopped
			bool m_user : 1;	// user flag, used by game code
			bool m_cameraCutThisFrame : 1;
			bool m_skipPhaseUpdateThisFrame : 1; // If we just transitioned to this frame we don't want to update this frame
		};
	};

	StringId64					m_animId;
	F32							m_curSample;
	F32							m_curPhase;  // current time (non looping)
	F32							m_prevPhase;
	F32							m_remainderTime;
	F32							m_playbackRate;
	F32							m_fadeOutTimeLeft;

	// simulated ClipData
	ndanim::ClipData			m_clip;

	// pointer^H^H^H^H^H^H^H ----HANDLE---- to a real animation from which I can get my camera cut information
	ArtItemAnimHandle_HACK		m_hCameraCutAnim;

	// When blended in - use these parameters
	Flags						m_flags;
	ID							m_id;

	ndanim::SharedTimeIndex*	m_pSharedTime;
};
