/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/audio/lipsync-types.h"
#include "gamelib/scriptx/h/vox-defines.h"

namespace DC
{
	struct EmotionEntry;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct EmotionalState
{
	StringId64 m_emotion = INVALID_STRING_ID_64;
	I32 m_priority;
	float m_blend;
	float m_fade;
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum
{
	kEmotionPriorityBase,
	kEmotionPriorityUnspecified,
	kEmotionPriorityCharacterIdleFromTaskGraph,
	kEmotionPriorityCharacterIdle,
	kEmotionPriorityCharacter,
	kEmotionPriorityDeath,
	kEmotionPriorityScript,
};

/// --------------------------------------------------------------------------------------------------------------- ///
class EmotionControl
{
public:
	EmotionControl();

	void SetOwner(NdGameObject* pOwner) { m_hOwner = pOwner; }

	void SetEmotionalState(StringId64 emotion, float blend, I32 priority, float holdTimeSec, float fade);

	EmotionalState GetEmotionalState() const;

private:
	NdGameObjectHandle m_hOwner;

	mutable EmotionalState m_curState;

	float m_holdTimeSec;
	TimeFrame m_lastStateTime;
};

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::EmotionEntry* GetDcEmotionEntry(const NdGameObject* pOwner, StringId64 emotion);
StringId64 GetPhonemeAnimationId(const NdGameObject* pOwner, StringId64 emotion);
StringId64 GetBaseEmotionAnim(const NdGameObject* pOwner, StringId64 emotion);
StringId64 GetIdleEmotionAnim(const NdGameObject* pOwner, StringId64 emotion);
StringId64 GetEmotionBlinkSettings(const NdGameObject* pOwner, StringId64 emotion);
