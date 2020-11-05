/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/character.h"

#include "gamelib/anim/gesture-handle.h"

class Character;

class DialogLook
{
private:
	enum DialogState
	{
		kInactive,
		kPrepare,
		kTalking,
		kSilence,
	};

	DialogState m_state;

	TimeFrame m_lastUpdateTime;

	TimeFrame m_startTime;
	TimeFrame m_lookStartTime;
	TimeFrame m_lookEndTime;

	// Used for tracking:
	// - how long in kPrepare
	// - how long the silence is in kTalking
	// - how long the talking is in kSilence
	TimeFrame m_timer;

	StringId64 m_voxId;
	StringId64 m_settingsId;

	bool m_enable;
	bool m_isSpeaking;
	bool m_oneFrameDisabled;
	bool m_oneFrameGesturesDisabled;

	CharacterHandle m_owner;
	CharacterHandle m_other;

	TimeFrame m_lastOutOfAngleTime;
	TimeFrame m_disabledTime;
	TimeFrame m_gestureDisabledTime;
	GestureHandle m_gestureHandle;

	float m_myLookBlend;
	float m_myPrevLookBlend;
	float m_myLookAngleSignHysteresis;

	Point m_lookAtPos;
	float m_lookAtBlend;

	float m_currentAngle;

	StringId64 m_noLookReason;

public:
	DialogLook();

	void SetSettingsId(StringId64 settingsId) { m_settingsId = settingsId; }
	void Enable(bool enable) { m_enable = enable; }
	void DisableForOneFrame(TimeFrame time) { m_oneFrameDisabled = true; m_disabledTime = time; }
	void DisableGesturesForOneFrame(TimeFrame time) { m_oneFrameGesturesDisabled = true; m_gestureDisabledTime = time; }
	void Start(Character* const pOwner, bool isSpeaking, const Character* const pOther);
	void Cancel() { EnterInactive(); }
	void Update(bool forceDisable);
	bool IsActive() { return m_state != DialogLook::kInactive; }

	bool IsGesturePlaying() const;

	Point GetLookAtPos() const { return m_lookAtPos; }
	float GetLookAtBlend() const { return m_lookAtBlend; }

private:
	void EnterInactive();

	void EnterPrepare();
	void UpdatePrepare(const TimeFrame currentTime,
					   const DC::AiDialogLookSettings* const pSettings,
					   const DC::Map* const pEmotionMap);

	void EnterTalking(const TimeFrame currentTime,
					  const DC::AiDialogLookSettings* const pSettings,
					  const DC::Map* const pEmotionMap);
	void UpdateTalking(const TimeFrame currentTime, float dt, const DC::AiDialogLookSettings* const pSettings);

	void EnterSilence(const TimeFrame currentTime);
	void UpdateSilence(const TimeFrame currentTime,
					   const DC::AiDialogLookSettings* const pSettings,
					   const DC::Map* const pEmotionMap);

	bool IsSilent() const;

	void BlendLook(const TimeFrame currentTime, float dt, const DC::AiDialogLookSettings* const pSettings);
};
