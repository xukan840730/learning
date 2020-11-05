/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/resource/resource-table.h"
#include "ndlib/script/script-pointer.h"

#include "gamelib/scriptx/h/blink-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class BlinkController : public IAnimCmdGenerator
{
public:
	using ParentClass = IAnimCmdGenerator;

	BlinkController(NdGameObject* pOwner);
	void ChangeSettings(StringId64 settingsId);
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	void Blink();
	void SuppressBlinking(TimeFrame duration);

	virtual void Step(float deltaTime) override;
	virtual void CreateAnimCmds(const AnimCmdGenLayerContext& context,
								AnimCmdList* pAnimCmdList,
								U32F outputInstance) const override;
	virtual float GetFadeMult() const override;

	virtual void DebugPrint(MsgOutput output) const override;
	virtual I32F GetFeatherBlendTableEntry() const override { return m_featherBlendTableIndex; }

private:
	using SettingsPointer = ScriptPointer<DC::BlinkSettings>;

	struct BlinkEntry
	{
		CachedAnimLookup m_animLookup;
		float m_phase = -1.0f;
	};
	
	void UpdateEmotionSettings();
	const DC::BlinkSettings* GetDcSettings() const;
	bool IsOwnerDead() const;

	static CONST_EXPR size_t kMaxActiveBlinks = 16;

	NdGameObject* m_pOwner;

	SettingsPointer m_emoSettings;
	SettingsPointer m_settings;

	BlinkEntry m_activeBlinks[kMaxActiveBlinks];
	U32 m_numActiveBlinks;
	U32 m_blinkAnimIndex;

	float m_nextBlinkDelaySec;
	float m_suppressionTimeSec;

	StringId64 m_lastEmoState;
	StringId64 m_activeFeatherBlend;
	I32 m_featherBlendTableIndex;

	CachedAnimLookup m_basePoseAnim;

	bool m_blinkedThisFrame;
};
