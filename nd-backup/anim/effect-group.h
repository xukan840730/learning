/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-instance.h"

#if !FINAL_BUILD
#define EFF_VALIDATE_PHASE_RANGES 1
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
class EffectAnimEntryTag;

/// --------------------------------------------------------------------------------------------------------------- ///
class EffectAnimEntry
{
public:
	EffectAnimEntry() {}
	EffectAnimEntry(StringId64 name, F32 frame, EffectAnimEntryTag* pTags, U32 numTags);

	StringId64 GetNameId() const;
	F32 GetFrame() const;
	F32 GetEndFrame() const;
	bool HasEndFrame() const;
	U32 GetNumTags() const;

	const EffectAnimEntryTag* GetTagByName(StringId64 name) const;
	const EffectAnimEntryTag* GetTagByIndex(U32F index) const;

	bool Clone(const EffectAnimEntry& other,
			   StringId64 newEffType,
	           int numReplaceTags,
			   StringId64* replaceKeys, 
			   const char** replaceValues,
	           int numNewTags, 
			   StringId64* newKeys,
			   const char** newValues,
	           const Memory::Context& context, 
			   bool dontCopyExistingTags = false);

	bool Clone(const EffectAnimEntry& other, 
			   StringId64 newEffType,
			   int numReplaceTags, 
			   StringId64* replaceKeys, 
			   StringId64* replaceValues,
			   int numNewTags, 
			   StringId64* newKeys,
			   StringId64* newValues,
			   const Memory::Context& context,
			   bool dontCopyExistingTags = false);

// 	void PrintToMsgCon() const;

private:
	StringId64 m_name;
	F32 m_frame;
	U32 m_numTags;
	EffectAnimEntryTag* m_tags;

	friend class EffectAnimEditor;
	friend class AnimStream;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// This is a debugging structure that we use to record
// what exactly we did - which anims we checked for EFFs
// so we can print it out or recreate it later.
class ALIGNED(16) EffectAnimInfo
{
public:
	StringId64				m_anim = INVALID_STRING_ID_64;
	const EffectAnimEntry*	m_pEffect = nullptr;
	mutable uintptr_t		m_filterGroupId = 0; // normally equals (uintptr_t)m_pEffect, but script-generated EFFs use the *original* EFF's address, not the new one allocated out of single-frame memory
	bool					m_wasFlipped = false;
	bool					m_topTrackInstance = false;
	bool					m_isMotionMatchingState = false;
	float					m_animBlend = 0.f;
	AnimInstance::ID		m_instId = INVALID_ANIM_INSTANCE_ID;
	StringId64				m_layerId = INVALID_STRING_ID_64;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class EffectAnim
{
public:
	StringId64 m_nameId = INVALID_STRING_ID_64;
	EffectAnimEntry* m_pEffects = nullptr;
	U64 m_numEffects = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class EffectList
{
public:

	EffectList();

	void Init(U32F maxEffects);
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	void Add(const EffectAnimEntry* pEffect,
			 StringId64 anim,
			 bool wasFlipped,
			 bool topTrackInstance,
			 bool isMotionMatchingState,
			 float animBlend,
			 const AnimInstance* pSourceInstance);

	void Remove(U32F index);
	void Clear() { m_numEffects = 0; }

	const EffectAnimInfo* Get(U32F index) const;
	U32F GetNumEffects() const { return m_numEffects; }
	U32F GetMaxEffects() const { return m_maxEffects; }

	void SetPhaseRange(F32 startPhase, F32 endPhase, F32 durationSec) { m_startPhase = startPhase; m_endPhase = endPhase; ValidatePhaseRange(durationSec); }
	F32 GetStartPhase() const { return m_startPhase; }
	F32 GetEndPhase() const { return m_endPhase; }

	void Flip(U32F index);

private:
#if EFF_VALIDATE_PHASE_RANGES
	void ValidatePhaseRange(F32 durationSec);
#else
	void ValidatePhaseRange(F32) { }
#endif

	EffectAnimInfo* m_pAnimInfo;
	U32 m_maxEffects;
	U32 m_numEffects;
	F32 m_startPhase;
	F32 m_endPhase;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class EffectGroup
{
public:
	// This function collects effects in the semi-open interval (open, closed].
	//
	// SPECIAL CASE: NON-LOOPING
	// -------------------------
	// If the interval encompasses frame index 0 and the animation is NOT looping, the interval
	// becomes FULLY CLOSED, because we want the designers to be able to tag frame index 0:
	//
	//     special case ***v
	//                     [C   C](o  c](o  c](o  c](o  c]
	//                     |-----+-----+-----+-----+---->|    NON-LOOPING
	//
	// If open > closed, we assume the animation is playing in reverse and the sense of the
	// intervals are REVERSED, including the special cases.  This behavior is identical to what
	// would happen if the animator had reversed the animation in Maya and exported it as an
	// entirely new clip.
	//                                                   v*** special case
	//                     [c  o)[c  o)[c  o)[c  o)[C   C]
	//                     |<----+-----+-----+-----+-----|    NON-LOOPING
	//
	// SPECIAL CASE: LOOPING
	// ---------------------
	// For a LOOPING animation, any effect placed on the 0th frame index is treated as if it were on
	// the last frame index (which is really the same thing since it wraps around).
	//
	//                      (o  c](o  c](o  c](o  c](o  c]
	//                     |-----+-----+-----+-----+---->|    LOOPING
	//                     X                             *    effect on frame 0 moved to last frame
	//
	// Likewise, when playing in reverse, if an effect is on the LAST frame of a loop, it is treated
	// as though it is on frame index 0.
	//
	//                     [c  o)[c  o)[c  o)[c  o)[c  o)
	//                     |<----+-----+-----+-----+-----|    LOOPING
	//                     *                             X    effect on frame LAST moved to frame 0
	//
	// NOTE: This function deals with frame *indices*, not frame *intervals*.  For example, a
	// 3-interval (4-index) animation looks like this:
	//		INDEX     0     1     2     3  (4 indices)
	//                |-----+-----+-----|
	//		INTERVAL     0     1     2     (3 intervals)
	// Elsewhere we refer to frame indices as "samples", but that's a misnomer here because the
	// number of "samples" depends on the sampling rate (30 FPS vs. 15 FPS), where as "frame
	// indices" are always 1/30th second apart, independent of the sample rate.
	//
	static void GetEffectsInSemiOpenInterval(const EffectAnim* pEffectAnim,
											 F32 iOpenFrame,
											 F32 iClosedFrame,
											 F32 iLastFrame,
											 bool looping,
											 bool isFlipped,
											 bool topState,
											 float animBlend,
											 StringId64 stateId,
											 float stateMinBlend,
											 bool playingForward,
											 EffectList* pTriggeredEffects,
											 const AnimInstance* pInstance);

	// Convenience function that collects all triggered effects correctly, handling all cases,
	// given the previous and current frame indices and some additional required information:
	//		iOldFrame			- Previous frame index
	//		iNewFrame			- Current frame index
	//		iLastFrame			- Highest valid frame index (equal to number of frame intervals)
	//		playingForward		- True if playing forward, false if playing in reverse
	//		looping				- True if animation is looping
	//		isFlipped			- True if animation is flipped/mirrored (needed for hands/feet)
	//		pTriggeredEffects	- Returns effects in this list
	//
	static void GetTriggeredEffects(const EffectAnim* pEffectAnim,
									F32 iOldFrame,
									F32 iNewFrame, 
									F32 iLastFrame,
									bool playingForward,
									bool looping,
									bool isFlipped,
									bool topState,
									float animBlend,
									float stateMinBlend,
									StringId64 stateId,
									EffectList* pTriggeredEffects,
									const AnimInstance* pInstance);

	static const EffectAnimEntry* GetEffectById(const EffectAnim* pEffectAnim, StringId64 effectId);
};
