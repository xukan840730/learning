/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/emotion-control.h"

#include "ndlib/nd-frame-state.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/scriptx/h/nd-ai-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
float g_emotionBlendOutDefault = 2.0f;

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::EmotionEntry* GetDcEmotionEntry(const NdGameObject* pOwner, StringId64 emotion)
{
	const DC::Map* pEmotionMap = ScriptManager::LookupInNamespace<DC::Map>(pOwner->GetEmotionMapId(),
																		   SID("ai"),
																		   nullptr);

	if (pEmotionMap == nullptr)
	{
		return nullptr;
	}

	const DC::EmotionEntry* pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, emotion);
	if (pEmotionEntry == nullptr)
	{
		pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, SID("neutral"));
	}

	return pEmotionEntry;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 GetBaseEmotionAnim(const NdGameObject* pOwner, StringId64 emotion)
{
	const DC::Map* pEmotionMap = ScriptManager::LookupInNamespace<DC::Map>(pOwner->GetEmotionMapId(),
																		   SID("ai"),
																		   nullptr);

	if (pEmotionMap == nullptr)
	{
		return SID("bindpose-face-null-add");
	}

	const DC::EmotionEntry* pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, emotion);
	if (pEmotionEntry == nullptr)
	{
		pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, SID("neutral"));
	}

	return pEmotionEntry->m_baseFaceAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 GetIdleEmotionAnim(const NdGameObject* pOwner, StringId64 emotion)
{
	StringId64 idleOverride = INVALID_STRING_ID_64;

	if (const Character* pCharacter = Character::FromProcess(pOwner))
	{
		if (const HeartRateMonitor* pHeartRateMonitor = pCharacter->GetHeartRateMonitor())
		{
			if (const DC::BreathAnim* pBreathAnim = pHeartRateMonitor->GetCurrentBreathAnim())
			{
				idleOverride = pBreathAnim->m_emotionalIdleOverride;
			}
		}
	}

	if (idleOverride != INVALID_STRING_ID_64)
		return idleOverride;

	const DC::Map* pEmotionMap = ScriptManager::LookupInNamespace<DC::Map>(pOwner->GetEmotionMapId(),
																		   SID("ai"),
																		   nullptr);

	if (pEmotionMap == nullptr)
	{
		return SID("bindpose-face-null-add");
	}

	const DC::EmotionEntry* pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, emotion);
	if (pEmotionEntry == nullptr)
	{
		pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, SID("neutral"));
	}

	return pEmotionEntry->m_idleFaceAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 GetEmotionBlinkSettings(const NdGameObject* pOwner, StringId64 emotion)
{
	const StringId64 emotionMapId = pOwner ? pOwner->GetEmotionMapId() : SID("*emotion-map*");
	const DC::Map* pEmotionMap = ScriptManager::LookupInNamespace<DC::Map>(emotionMapId, SID("ai"), nullptr);
	if (!pEmotionMap)
	{
		return INVALID_STRING_ID_64;
	}

	const DC::EmotionEntry* pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, emotion);

	if (nullptr == pEmotionEntry)
	{
		pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, SID("neutral"));
	}

	return pEmotionEntry ? pEmotionEntry->m_blinkSettings : INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 GetPhonemeAnimationId(const NdGameObject* pOwner, StringId64 emotion)
{
	StringId64 emotionMapId = SID("*emotion-map*");

	if (pOwner != nullptr)
		emotionMapId = pOwner->GetEmotionMapId();

	const DC::Map* pEmotionMap = ScriptManager::LookupInNamespace<DC::Map>(emotionMapId, SID("ai"), nullptr);

	if (pEmotionMap == nullptr)
	{
		return SID("phonemes-neutral-full-set-01");
	}

	const DC::EmotionEntry* pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, emotion);
	if (pEmotionEntry == nullptr)
	{
		pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, SID("neutral"));
	}

	return pEmotionEntry->m_phonemeAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
EmotionControl::EmotionControl() : m_hOwner(nullptr), m_lastStateTime(TimeFrameNegInfinity())
{
	m_curState.m_emotion  = INVALID_STRING_ID_64;
	m_curState.m_priority = -1;
	m_curState.m_blend	  = 0.1f;

	m_holdTimeSec = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EmotionControl::SetEmotionalState(StringId64 emotion, float blend, I32 priority, float holdTimeSec, float fade)
{
	if (!m_hOwner.HandleValid())
		return;

	const NdGameObject* pOwner = m_hOwner.ToProcess();

	// lower priority request, ignore
	if (GetEmotionalState().m_priority > priority)
		return;

	StringId64 emotionMapId = SID("*emotion-map*");
	if (pOwner)
	{
		emotionMapId = pOwner->GetEmotionMapId();
	}

	const DC::Map* pEmotionMap = ScriptManager::LookupInNamespace<DC::Map>(pOwner->GetEmotionMapId(), SID("ai"), nullptr);

	const DC::EmotionEntry* pEmotionEntry = nullptr;
	if (pEmotionMap != nullptr)
	{
		pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, emotion);
	}

	if (holdTimeSec < 0.0f)
	{
		if (pEmotionEntry != nullptr)
			holdTimeSec = pEmotionEntry->m_holdTime;
		else
			holdTimeSec = 10.0f;
	}

	if (blend < 0.0f)
	{
		if (pEmotionEntry != nullptr)
			blend = pEmotionEntry->m_blendTime;
		else
			blend = g_emotionBlendOutDefault;
	}

	if (fade < 0.0f)
	{
		if (pEmotionEntry != nullptr)
			fade = pEmotionEntry->m_fade;
		else
			fade = 1.0f;
	}

	const Clock* pClock = pOwner ? pOwner->GetClock() : EngineComponents::GetNdFrameState()->GetClock(kGameClock);

	m_lastStateTime = pClock->GetCurTime();
	m_holdTimeSec	= holdTimeSec + 0.2f;

	m_curState.m_fade		= fade;
	m_curState.m_blend		= blend;
	m_curState.m_emotion	= emotion;
	m_curState.m_priority	= priority;
}

/// --------------------------------------------------------------------------------------------------------------- ///
EmotionalState EmotionControl::GetEmotionalState() const
{
	const NdGameObject* pOwner = m_hOwner.ToProcess();
	const Clock* pClock = pOwner ? pOwner->GetClock() : EngineComponents::GetNdFrameState()->GetClock(kGameClock);

	if (pClock->GetCurTime() - m_lastStateTime > Seconds(m_holdTimeSec))
	{
		m_curState.m_emotion  = INVALID_STRING_ID_64;
		m_curState.m_blend	  = g_emotionBlendOutDefault;
		m_curState.m_priority = -1;
	}

	return m_curState;
}
