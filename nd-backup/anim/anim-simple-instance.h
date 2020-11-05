/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/msg.h"

#include "ndlib/anim/anim-instance.h"
#include "ndlib/process/bound-frame.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/resource/resource-table.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimTable;
class ArtItemAnim;
struct AnimCameraCutInfo;
struct FgAnimData;
struct EvaluateChannelParams;

namespace ndanim
{
	struct ClipData;
	struct JointParams;
	struct SharedTimeIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSimpleInstance : public AnimInstance
{
public:
	friend class NdActorViewerObject;

	AnimSimpleInstance();

	virtual bool IsSimple() const override { return true; }

	void Init(ID id,
			  const AnimTable* pAnimTable,
			  ArtItemAnimHandle anim,
			  F32 startPhase,
			  F32 playbackRate,
			  F32 fadeTime,
			  DC::AnimCurveType blendType,
			  F32 effStartFrame = -1.0f,
			  ndanim::SharedTimeIndex* pSharedTime = nullptr);

	void Init(ID id,
			  const AnimTable* pAnimTable,
			  StringId64 translatedAnimNameId,
			  F32 startPhase,
			  F32 playbackRate,
			  F32 fadeTime,
			  DC::AnimCurveType blendType,
			  F32 effStartFrame = -1.0f,
			  ndanim::SharedTimeIndex* pSharedTime = nullptr);

	bool IsValid() const;

	bool IsLooping() const;
	void SetLooping(bool onoff);

	virtual bool IsFlipped() const override;
	void SetFlipped(bool f);

	virtual F32 GetMayaFrame() const override;
	F32 PhaseToMayaFrame(float phase) const;
	F32 GetSample() const { return m_curSample; }

	F32 GetPhase() const override;
	F32 GetPhaseCeiling(F32* pFrame = nullptr, F32* pFrameCeil = nullptr) const;
	F32 GetFramesPerSecond() const;
	virtual U32 GetFrameCount() const override;
	virtual F32 GetDuration() const override;

	F32 GetPrevPhase() const override;
	virtual F32 GetPrevMayaFrame() const override;

	virtual F32 GetFade() const override;

	void SetPlaybackRate(F32 playbackRate);
	F32 GetPlaybackRate() const { return m_playbackRate; }

	void SetPhase(F32 phase, bool bSingleStepping = false);
	void SetFrame(F32 f, bool bSingleStepping = false);

	void SetAnim(ArtItemAnimHandle animHandle, const StringId64 animId = INVALID_STRING_ID_64);
	ArtItemAnimHandle GetAnim() const
	{
		return m_animHandle;
	}
	StringId64 GetAnimId() const
	{
		return m_animId;
	}
	virtual const AnimTable* GetAnimTable() const override
	{
		return m_pAnimTable;
	}
	const ndanim::ClipData* GetClip() const;

	void PhaseUpdate(F32 deltaTime, const FgAnimData* pAnimData);

	// relocate the node
	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override;


	// AlignToActionPackOrigin() causes the current animation to be played such that the
	// apReference within the animation (if any) lines up with the master apOrigin specified.  This
	// may cause the object's align (m_pGameObject->m_loc) to move or rotate off its original
	// spawner locator.  AlignToActionPackOrigin() must be called AFTER AddAnimation(), because
	// AddAnimation() always clears the apOrigin (i.e. the align will no longer move).
	void AlignToActionPackOrigin(bool onoff);
	bool IsAlignedToActionPackOrigin() const;

	void SetApChannelName(StringId64 apId) { m_apChannelName = apId;}
	StringId64 GetApChannelName() const { return m_apChannelName; }

	// Returns the raw apOrigin.
	virtual void SetApOrigin(const BoundFrame& apRef) override;
	virtual const BoundFrame& GetApOrigin() const override;

	U32F EvaluateChannels(const StringId64* pChannelNames,
						  U32F numChannels,
						  F32 phase,
						  ndanim::JointParams* pOutChannelJoints,
						  bool mirror		= false,
						  bool wantRawScale = false,
						  AnimCameraCutInfo* pCameraCutInfo = nullptr) const;

	U32F EvaluateChannels(const StringId64* pChannelNames,
						  U32F numChannels,
						  ndanim::JointParams* pOutChannelJoints,
						  const EvaluateChannelParams& params) const;

	U32F EvaluateFloatChannels(const StringId64* pChannelNames,
							   U32F numChannels,
							   float* pOutChannelFloats,
							   const EvaluateChannelParams& params) const;

	void ForceRefreshAnimPointers();

	void DebugPrint(MsgOutput output) const;

	virtual bool IsFrozen() const override { return m_flags.m_frozen; }
	virtual void SetFrozen(bool f) override { m_flags.m_frozen = f; }

	bool IsAligningLocation() const { return !m_flags.m_noUpdateLocation; }
	void SetNoAlignLocation() {m_flags.m_noUpdateLocation = true; }
	virtual void SetSkipPhaseUpdateThisFrame(bool f) override { m_flags.m_skipPhaseUpdateThisFrame = f; }
	void SetFirstUpdatePhase(float phase) { m_firstUpdatePhase = phase; }

	void EnableCameraCutDetection() { m_flags.m_enableCameraCutDetection = true; }
	bool DidCameraCutThisFrame() const { return m_flags.m_cameraCutThisFrame; }
	float GetRemainderTime() const	{ return m_remainderTime; }

	virtual ID GetId() const override { return m_id; }

	virtual float GetTotalFadeTime() const { return m_fadeTimeTotal; }

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
			bool m_alignToApOrigin : 1;
			bool m_user : 1;	// user flag, used by game code
			bool m_noUpdateLocation : 1;
			bool m_cameraCutThisFrame : 1;
			bool m_enableCameraCutDetection : 1;
			bool m_skipPhaseUpdateThisFrame : 1; // If we just transitioned to this frame we don't want to update this frame
			bool m_phaseUpdatedManuallyThisFrame : 1;	// HACK for cinematic capture mode
		};
	};

	// qword 0 - 5
	BoundFrame					m_apOrigin;
	ArtItemAnimHandle			m_animHandle;
	StringId64					m_animId;
	const AnimTable*			m_pAnimTable;
	F32							m_curSample;  // current sample index

	// qword 6
	F32							m_curPhase;  // current time (non looping)
	F32							m_prevPhase;
	F32							m_remainderTime;
	F32							m_playbackRate;

	// qword 7
	// When blended in - use these parameters
	F32							m_fadeTimeLeft;
	F32							m_fadeTimeTotal;
	F32							m_currentFade;
	Flags						m_flags;
	U8							m_blendType; // DC::AnimCurveType
	ID							m_id;
	float						m_firstUpdatePhase = -1.f; // the desired phase during for first update. To support syncing player/weapon anim during auto-transition

	StringId64					m_apChannelName;
	ndanim::SharedTimeIndex*	m_pSharedTime;
};
