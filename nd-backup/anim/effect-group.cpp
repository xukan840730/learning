/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/effect-group.h"

#include "corelib/memory/relocate.h"
#include "corelib/util/msg.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/scriptx/h/eff-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const F32 kEffectFrameBegin		 = 0.0f;  // Beginning; first frame of animation.
const F32 kEffectFrameEnd		 = -1.0f; // End; last frame of animation.
const F32 kEffectFrameStart		 = -2.0f; // Start; occurs anytime animation speed is set != 0.
const F32 kEffectFrameStop		 = -3.0f; // Stop; occurs anytime animation speed is set == 0.
const F32 kEffectInvalidEndFrame = -5000.0f;
extern bool g_printEffDebugging;
extern bool g_printEffDebuggingAnim;

bool g_disableJointGroupFilter = false;

static ScriptPointer<DC::Map> s_effMinBlendTable;
static bool s_effMinBlendTableInitialized = false;

/// --------------------------------------------------------------------------------------------------------------- ///
static float LookupEffMinBlend(StringId64 effTypeId)
{
	PROFILE_ACCUM(LookupEffMinBlend);

	if (!s_effMinBlendTableInitialized)
	{
		s_effMinBlendTable = ScriptPointer<DC::Map>(SID("*eff-min-blend-table*"), SID("eff"));
		s_effMinBlendTableInitialized = true;
	}
	
	const DC::Map* pMinBlendMap = s_effMinBlendTable;

	if (!pMinBlendMap)
		return -1.0f;

	const DC::MinBlendTableEntry* pEntry = ScriptManager::MapLookup<DC::MinBlendTableEntry>(pMinBlendMap, effTypeId);

	if (!pEntry)
		return -1.0f;

	return pEntry->m_minBlend;
}

/// --------------------------------------------------------------------------------------------------------------- ///
EffectAnimEntry::EffectAnimEntry(StringId64 name, F32 frame, EffectAnimEntryTag* pTags, U32 numTags)
	: m_name(name)
	, m_frame(frame)
	, m_numTags(numTags)
	, m_tags(pTags)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 EffectAnimEntry::GetNameId() const
{
	return m_name;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 EffectAnimEntry::GetFrame() const
{
	return m_frame;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 EffectAnimEntry::GetEndFrame() const
{
	const EffectAnimEntryTag* pEndFrame = GetTagByName(SID("frame-end"));
	F32 endFrame = kEffectInvalidEndFrame;

	if (pEndFrame)
	{
		endFrame = static_cast<F32>(pEndFrame->GetValueAsU32());

		if (endFrame < 0.0f || endFrame > 100000)
		{
			// most likely float parsed as integer, try float
			endFrame = pEndFrame->GetValueAsF32();
		}
	}

	return endFrame;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EffectAnimEntry::HasEndFrame() const
{
	return GetTagByName(SID("frame-end"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 EffectAnimEntry::GetNumTags() const
{
	return m_numTags;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EffectAnimEntryTag* EffectAnimEntry::GetTagByName(StringId64 name) const
{
	if (m_numTags)
	{
		const EffectAnimEntryTag* pTags = m_tags;
		for (I32F iTag = 0; iTag < m_numTags; iTag++)
		{
			const EffectAnimEntryTag* pTag = pTags + iTag;
			if (pTag->GetNameId() == name)
				return pTag;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EffectAnimEntryTag* EffectAnimEntry::GetTagByIndex(U32F index) const
{
	if (index >= m_numTags)
		return nullptr;

	return &m_tags[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EffectAnimEntry::Clone(const EffectAnimEntry& other,
							StringId64 newEffType,
							int numReplaceTags, 
							StringId64* replaceKeys, 
							const char** replaceValues,
							int numNewTags, 
							StringId64* newKeys, 
							const char** newValues,
							const Memory::Context& context, 
							bool dontCopyExistingTags)
{
	m_name = (newEffType != INVALID_STRING_ID_64) ? newEffType : other.m_name;
	m_frame = other.m_frame;

	const int numExistingTags = (dontCopyExistingTags ? 0 : other.m_numTags);
	const int numTags = numExistingTags + numNewTags;

	EffectAnimEntryTag* tags = NDI_NEW(context, kAlign16) EffectAnimEntryTag[numTags];
	if (!tags)
		return false;

	// replace any that should be replaced
	int k = 0;
	for (int i = 0; i < numExistingTags; i++)
	{
		int j = 0;
		for ( ; j < numReplaceTags; j++)
		{
			if (other.m_tags[i].GetNameId() == replaceKeys[j])
				break;
		}

		if (j < numReplaceTags)
		{
			// found a replacement for this key
			ANIM_ASSERT(other.m_tags[i].GetNameId() == replaceKeys[j]);
			if (replaceValues[j] != nullptr)
				tags[k++].Construct(replaceKeys[j], replaceValues[j]);
		}
		else
		{
			tags[k++] = other.m_tags[i];
		}
	}

	// now append any new tags
	ANIM_ASSERT(k <= numExistingTags);
	for (int j = 0; j < numNewTags; j++, k++)
	{
		ANIM_ASSERT(k < numTags);
		tags[k].Construct(newKeys[j], newValues[j]);
	}

	// finally swap in the new array
	m_numTags = k;
	m_tags = tags;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EffectAnimEntry::Clone(const EffectAnimEntry& other,
							StringId64 newEffType,
							int numReplaceTags,
							StringId64* replaceKeys, 
							StringId64* replaceValues,
							int numNewTags, 
							StringId64* newKeys,
							StringId64* newValues,
							const Memory::Context& context,
							bool dontCopyExistingTags)
{
	m_name = (newEffType != INVALID_STRING_ID_64) ? newEffType : other.m_name;
	m_frame = other.m_frame;

	const int numExistingTags = (dontCopyExistingTags ? 0 : other.m_numTags);
	const int numTags = numExistingTags + numNewTags;

	EffectAnimEntryTag* tags = NDI_NEW(context, kAlign16) EffectAnimEntryTag[numTags];
	if (!tags)
		return false;

	// replace any that should be replaced
	int k = 0;
	for (int i = 0; i < numExistingTags; i++)
	{
		int j = 0;
		for ( ; j < numReplaceTags; j++)
		{
			if (other.m_tags[i].GetNameId() == replaceKeys[j])
				break;
		}

		if (j < numReplaceTags)
		{
			// found a replacement for this key
			ANIM_ASSERT(other.m_tags[i].GetNameId() == replaceKeys[j]);
			tags[k++].Construct(replaceKeys[j], replaceValues[j]);
		}
		else
		{
			tags[k++] = other.m_tags[i];
		}
	}

	// now append any new tags
	ANIM_ASSERT(k <= numExistingTags);
	for (int j = 0; j < numNewTags; j++, k++)
	{
		ANIM_ASSERT(k < numTags);
		tags[k].Construct(newKeys[j], newValues[j]);
	}

	// finally swap in the new array
	m_numTags = k;
	m_tags = tags;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectGroup::GetEffectsInSemiOpenInterval(const EffectAnim* pEffectAnim,
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
											   const AnimInstance* pInstance)
{
	const bool didNotAdvance = (iOpenFrame == iClosedFrame) && (iLastFrame > 0);
	if (pEffectAnim == nullptr || didNotAdvance || !pTriggeredEffects)
	{
		return;
	}

	I32F numEffects = pEffectAnim->m_numEffects;
	if (numEffects > 0 && g_printEffDebuggingAnim)
	{
		MsgAnim(FRAME_NUMBER_FMT "Opn=%.9g Cls=%.9g Lst=%.9g Fwd=%d Loop=%d %s\n",
				FRAME_NUMBER,
				iOpenFrame,
				iClosedFrame,
				iLastFrame,
				playingForward,
				looping,
				DevKitOnly_StringIdToString(pEffectAnim->m_nameId));
	}

	for (I32F iEffect = 0; iEffect < numEffects; iEffect++)
	{
		F32 startFrameToUse = iOpenFrame;
		const EffectAnimEntry* effect = &pEffectAnim->m_pEffects[iEffect];
		F32 effectFrame = effect->GetFrame();
		F32 effectEndFrame = effect->GetEndFrame() + 0.000001f; // nudge to make sure the final frame passes the test.

		if (const EffectAnimEntryTag* pForward = effect->GetTagByName(SID("animation-direction")))
		{
			const StringId64 direction = pForward->GetValueAsStringId();

			if (direction == SID("forward") && !playingForward)
			{
				continue;
			}

			if (direction == SID("backward") && playingForward)
			{
				continue;
			}
		}

		if (const EffectAnimEntryTag* pForward = effect->GetTagByName(SID("play-if-skipped")))
		{
			startFrameToUse = 0.0f;
		}

		float minBlend = -1.0f;

		if (const EffectAnimEntryTag* pMinBlendTag = effect->GetTagByName(SID("min-blend")))
		{
			minBlend = pMinBlendTag->GetValueAsF32();
		}
		else if (stateMinBlend >= 0.0f)
		{
			minBlend = stateMinBlend;
		}
		else
		{
			minBlend = LookupEffMinBlend(effect->GetNameId());
		}

		if (minBlend >= 0.0f && (animBlend < minBlend))
		{
			if (FALSE_IN_FINAL_BUILD(g_printEffDebuggingAnim))
			{
				MsgAnim(FRAME_NUMBER_FMT "Skipping %s from '%s' because blend %f is below minimum threshold %f\n",
						FRAME_NUMBER,
						DevKitOnly_StringIdToString(effect->GetNameId()),
						DevKitOnly_StringIdToString(pEffectAnim->m_nameId),
						animBlend,
						minBlend);
			}

			continue;
		}

		// Check for magic frame numbers.
		if (effectFrame < 0.0f)
		{
			if (effectFrame == kEffectFrameEnd)
			{
				effectFrame = iLastFrame;
			}
		}
		else
		{
			if (playingForward && effectFrame == 0.0f)
			{
				// Special case: Any effect on frame 0 of an animation is moved to frame 0.00001
				// so it will be included in the (open, closed] interval.
				effectFrame = 0.000001f;
			}
			else if (!playingForward && effectFrame == iLastFrame)
			{
				// Special case: Any effect on frame LAST of an animation is moved to frame iLastFrame - 0.00001
				// so it will be included in the [closed, open) interval at the start of the line.
				effectFrame = iLastFrame - 0.000001f;
			}
		}


		bool add;
		if (playingForward)
		{
			add = ((!looping && effectFrame == 0 && startFrameToUse == 0)
				|| (effectFrame > startFrameToUse && effectFrame <= iClosedFrame)
				|| (effectEndFrame > 0 && effectFrame <= iClosedFrame && effectEndFrame >= iClosedFrame) );
		}
		else
		{
			add = ((!looping && effectFrame == iLastFrame && startFrameToUse == iLastFrame)
				|| (effectFrame >= iClosedFrame && effectFrame < startFrameToUse)
				|| (effectEndFrame > 0 && effectFrame < startFrameToUse && effectEndFrame >= startFrameToUse) );
		}

		StringId64 nameTag = INVALID_STRING_ID_64;
		if (const EffectAnimEntryTag* pTag = effect->GetTagByName(SID("name")))
		{
			nameTag = pTag->GetValueAsStringId();
		}

		if (g_printEffDebuggingAnim)
		{
			MsgAnim(FRAME_NUMBER_FMT "effectFrame=%.9g endFrame=%.9g add=%d %s %s %s\n",
					FRAME_NUMBER,
					effectFrame,
					effectEndFrame,
					add,
					DevKitOnly_StringIdToString(effect->GetNameId()),
					nameTag != INVALID_STRING_ID_64 ? DevKitOnly_StringIdToString(nameTag) : "",
					add ? " ********" : "");
		}

		if (add)
		{
			// Add the effect if it lies in the semi-open interval [iOpenFrame, iClosedFrame).
			// ** In the special case of iClosedFrame==iLastFrame, the interval is fully closed, to ensure
			// ** we pick up effects on the last frame for non-looping animations. (Looping animations
			// ** should never have effects on the last frame, as it is a duplicate of the first frame.)

			pTriggeredEffects->Add(&pEffectAnim->m_pEffects[iEffect],
								   pEffectAnim->m_nameId,
								   isFlipped,
								   topState,
								   stateId == SID("s_motion-match-locomotion"),
								   animBlend,
								   pInstance);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectGroup::GetTriggeredEffects(const EffectAnim* pEffectAnim,
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
									  const AnimInstance* pInstance)
{
	bool wrapped = ((playingForward && iOldFrame > iNewFrame)
				|| (!playingForward && iNewFrame > iOldFrame));

	if (!wrapped)
	{
		// Collect effects either in the interval:
		//   If playing forward:   ---(old, new]---->
		//   If playing backward:  <--[new, old)-----
		GetEffectsInSemiOpenInterval(pEffectAnim,
									 iOldFrame,
									 iNewFrame,
									 iLastFrame,
									 looping,
									 isFlipped,
									 topState,
									 animBlend,
									 stateId,
									 stateMinBlend,
									 playingForward,
									 pTriggeredEffects,
									 pInstance);
	}
	else
	{
		// Collect effects in two intervals:
		//   If playing forward:  [0, new]-------------->(old, LAST]
		//   If playing backward: [0, old)<--------------[new, LAST]

		F32 fOpen1, fClosed1;
		F32 fOpen2, fClosed2;
		if (playingForward)
		{
			fOpen1		= 0.0f;
			fClosed1	= iNewFrame;
			fOpen2		= iOldFrame;
			fClosed2	= iLastFrame;
		}
		else
		{
			fOpen1		= iOldFrame;
			fClosed1	= 0.0f;
			fOpen2		= iLastFrame;
			fClosed2	= iNewFrame;
		}

		GetEffectsInSemiOpenInterval(pEffectAnim,
									 fOpen1,
									 fClosed1,
									 iLastFrame,
									 looping,
									 isFlipped,
									 topState,
									 animBlend,
									 stateId,
									 stateMinBlend,
									 playingForward,
									 pTriggeredEffects,
									 pInstance);

		GetEffectsInSemiOpenInterval(pEffectAnim,
									 fOpen2,
									 fClosed2,
									 iLastFrame,
									 looping,
									 isFlipped,
									 topState,
									 animBlend,
									 stateId,
									 stateMinBlend,
									 playingForward,
									 pTriggeredEffects,
									 pInstance);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EffectAnimEntry* EffectGroup::GetEffectById(const EffectAnim* pEffectAnim, StringId64 effectId)
{
	I32F numEffects = pEffectAnim->m_numEffects;
	for (I32F iEffect = 0; iEffect < numEffects; iEffect++)
	{
		const EffectAnimEntry* pEffect = &pEffectAnim->m_pEffects[iEffect];
		if (pEffect->GetNameId() == effectId)
		{
			return pEffect;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
EffectList::EffectList() :
	m_pAnimInfo(nullptr),
	m_maxEffects(0),
	m_numEffects(0)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectList::Init(U32F maxEffects)
{
	m_maxEffects = maxEffects;
	if (maxEffects == 0)
	{
		m_pAnimInfo = nullptr;
	}
	else
	{
		m_pAnimInfo = NDI_NEW (kAlign16) EffectAnimInfo[maxEffects];
	}

	m_startPhase = 0.0f;
	m_endPhase = 0.0f;

	Clear();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectList::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pAnimInfo, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectList::Add(const EffectAnimEntry* pEffect,
					 StringId64 anim,
					 bool wasFlipped,
					 bool topTrackInstance,
					 bool isMotionMatchingState,
					 float animBlend,
					 const AnimInstance* pSourceInstance)
{
	if (m_numEffects >= m_maxEffects)
		return;

	const AnimInstance::ID instId = pSourceInstance ? pSourceInstance->GetId() : INVALID_ANIM_INSTANCE_ID;
	const StringId64 layerId = pSourceInstance ? pSourceInstance->GetLayerId() : INVALID_STRING_ID_64;

	// Add the anim info for this effect
	EffectAnimInfo* pInfo = m_pAnimInfo + m_numEffects;
	pInfo->m_anim		  = anim;
	pInfo->m_pEffect	  = pEffect;
	pInfo->m_wasFlipped	  = wasFlipped;
	pInfo->m_topTrackInstance = topTrackInstance;
	pInfo->m_instId		  = instId;
	pInfo->m_layerId	  = layerId;
	pInfo->m_animBlend	  = animBlend;
	pInfo->m_isMotionMatchingState = isMotionMatchingState;

	if (const EffectAnimEntryTag* pGroupTag = pEffect->GetTagByName(SID("filter-group")))
	{
		pInfo->m_filterGroupId = pGroupTag->GetValueAsStringId().GetValue();
	}
	else if (!g_disableJointGroupFilter)
	{
		const EffectAnimEntryTag* pJointTag = pEffect->GetTagByName(SID("joint"));

		if (pJointTag)
		{
			pInfo->m_filterGroupId = pJointTag->GetValueAsStringId().GetValue();
		}
	}
	else
	{
		pInfo->m_filterGroupId = (uintptr_t)pEffect;
	}

	++m_numEffects;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectList::Remove(U32F index)
{
	if (index < m_numEffects)
	{
		for (U32F i = index; i < m_numEffects - 1; ++i)
		{
			m_pAnimInfo[i] = m_pAnimInfo[i + 1];
		}
		--m_numEffects;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectList::Flip(U32F index)
{
	if (index < m_numEffects)
	{
		m_pAnimInfo[index].m_wasFlipped = !m_pAnimInfo[index].m_wasFlipped;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EffectAnimInfo* EffectList::Get(U32F index) const
{
	ANIM_ASSERT(index < m_numEffects);
	return &m_pAnimInfo[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
#if EFF_VALIDATE_PHASE_RANGES
volatile bool g_enableEffValidation = false;
volatile bool g_enableEffValidationAssert = false;
void EffectList::ValidatePhaseRange(F32 durationSec)
{
	if (durationSec > 0.0f && g_enableEffValidation)
	{
		const F32 startSec = m_startPhase * durationSec;
		const F32 endSec = m_endPhase * durationSec;
		const F32 deltaSec = Abs(endSec - startSec);
		const F32 deltaFrames = deltaSec * 30.0f;

		// trying to catch a bug where m_startPhase was 0.0 and m_endPhase was 0.26 on a 3-second anim;
		// let's allow up to 6 frames worth of EFFs before we signal that there's a problem
		if (deltaFrames > 9.0f)
		{
			if (g_enableEffValidationAssert)
			{
				ANIM_ASSERTF(deltaFrames <= 9.0f, ("EffectList: sp=%g (%.2f sec), ep=%g (%.2f sec) d=%.2f sec (%.0f frames).\n",
					m_startPhase, startSec, m_endPhase, endSec, deltaSec, deltaFrames));
			}
			else
			{
				MsgAnim("EffectList: sp=%g (%.2f sec), ep=%g (%.2f sec) d=%.2f sec (%.0f frames).\n",
					m_startPhase, startSec, m_endPhase, endSec, deltaSec, deltaFrames);
			}
		}
	}
}
#endif
