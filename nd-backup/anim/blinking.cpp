/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/blinking.h"

#include "corelib/util/random.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/nd-anim-util.h"

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-anim.h"

/// --------------------------------------------------------------------------------------------------------------- ///
BlinkController::BlinkController(NdGameObject* pOwner)
	: m_pOwner(pOwner)
	, m_nextBlinkDelaySec(-1.0f)
	, m_suppressionTimeSec(0.0f)
	, m_numActiveBlinks(0)
	, m_blinkAnimIndex(0)
	, m_featherBlendTableIndex(-1)
	, m_lastEmoState(INVALID_STRING_ID_64)
	, m_activeFeatherBlend(INVALID_STRING_ID_64)
	, m_blinkedThisFrame(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BlinkController::ChangeSettings(StringId64 settingsId)
{
	m_settings = SettingsPointer(settingsId, SID("blink"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BlinkController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pOwner, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BlinkController::Blink()
{
	if (m_blinkedThisFrame)
		return;

	if (m_suppressionTimeSec > 0.0f)
		return;

	const DC::BlinkSettings* pDcSettings = GetDcSettings();

	if (!pDcSettings || !pDcSettings->m_blinkAnims || (pDcSettings->m_blinkAnims->m_count == 0))
		return;

	if (m_numActiveBlinks >= kMaxActiveBlinks)
		return;

	const AnimControl* pAnimControl = m_pOwner ? m_pOwner->GetAnimControl() : nullptr;
	if (!pAnimControl)
		return;

	const StringId64 blinkAnimId = pDcSettings->m_blinkAnims->m_array[m_blinkAnimIndex];
	CachedAnimLookup animLookup;
	animLookup.SetSourceId(blinkAnimId);
	animLookup = pAnimControl->LookupAnimCached(animLookup);

	if (animLookup.GetAnim().IsNull())
		return;

	m_activeBlinks[m_numActiveBlinks].m_animLookup = animLookup;
	m_activeBlinks[m_numActiveBlinks].m_phase = 0.0f;
	++m_numActiveBlinks;

	m_blinkAnimIndex = (m_blinkAnimIndex + 1) % pDcSettings->m_blinkAnims->m_count;
	m_blinkedThisFrame = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BlinkController::SuppressBlinking(TimeFrame duration)
{
	m_suppressionTimeSec = ToSeconds(duration);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BlinkController::UpdateEmotionSettings()
{
	const EmotionControl* pEmoControl = m_pOwner ? m_pOwner->GetEmotionControl() : nullptr;
	if (!pEmoControl)
	{
		m_emoSettings = SettingsPointer();
		return;
	}

	const EmotionalState& state = pEmoControl->GetEmotionalState();

	if (state.m_emotion == m_lastEmoState)
	{
		return;
	}
	else if (!state.m_emotion)
	{
		m_emoSettings = SettingsPointer();
		return;
	}

	const StringId64 emoSettingsId = GetEmotionBlinkSettings(m_pOwner, state.m_emotion);

	if (emoSettingsId)
	{
		m_emoSettings = SettingsPointer(emoSettingsId, SID("blink"));
	}
	else
	{
		m_emoSettings = SettingsPointer();
	}

	m_lastEmoState = state.m_emotion;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BlinkController::Step(float deltaTime)
{
	m_blinkedThisFrame = false;

	ParentClass::Step(deltaTime);

	if (AnimControl* pAnimControl = m_pOwner ? m_pOwner->GetAnimControl() : nullptr)
	{
		for (int i = 0; i < m_numActiveBlinks; ++i)
		{
			m_activeBlinks[i].m_animLookup = pAnimControl->LookupAnimCached(m_activeBlinks[i].m_animLookup);
		}
	}
	else
	{
		m_numActiveBlinks = 0;
	}

	UpdateEmotionSettings();

	const DC::BlinkSettings* pDcSettings = GetDcSettings();
	if (!pDcSettings)
		return;

	const bool dead = IsOwnerDead();

	if (m_suppressionTimeSec > 0.0f)
	{
		m_suppressionTimeSec -= deltaTime;
	}

	if (m_nextBlinkDelaySec >= 0.0f)
	{
		m_nextBlinkDelaySec -= deltaTime;

		if ((m_nextBlinkDelaySec <= 0.0f) && !dead)
		{
			Blink();
			m_nextBlinkDelaySec = -1.0f;
		}
	}

	if (dead)
	{
		m_numActiveBlinks = 0;
	}
	else if (m_numActiveBlinks > 0)
	{
		for (I32F i = m_numActiveBlinks - 1; i >= 0; --i)
		{
			const ArtItemAnim* pBlinkAnim = m_activeBlinks[i].m_animLookup.GetAnim().ToArtItem();

			const float duration   = pBlinkAnim ? GetDuration(pBlinkAnim) : 0.0f;
			const float deltaPhase = (duration > NDI_FLT_EPSILON) ? (deltaTime / duration) : kLargeFloat;

			m_activeBlinks[i].m_phase += deltaPhase;

			if (m_activeBlinks[i].m_phase > 1.0f)
			{
				m_activeBlinks[i] = m_activeBlinks[m_numActiveBlinks - 1];
				m_activeBlinks[m_numActiveBlinks - 1] = BlinkEntry();
				--m_numActiveBlinks;
			}
		}
	}

	if ((m_nextBlinkDelaySec < 0.0f)
		&& ((pDcSettings->m_blinkInterval.m_flags & (DC::kRangeFlagUseLower | DC::kRangeFlagUseUpper))
			== pDcSettings->m_blinkInterval.m_flags)
		&& !dead)
	{
		m_nextBlinkDelaySec = RandomFloatRange(pDcSettings->m_blinkInterval.m_lower,
											   pDcSettings->m_blinkInterval.m_upper);
	}

	if (m_activeFeatherBlend != pDcSettings->m_featherBlendSettings)
	{
		const FgAnimData* pAnimData	 = m_pOwner ? m_pOwner->GetAnimData() : nullptr;

		m_featherBlendTableIndex = g_featherBlendTable.LoginFeatherBlend(pDcSettings->m_featherBlendSettings, pAnimData);
		m_activeFeatherBlend	 = pDcSettings->m_featherBlendSettings;
	}
	
	AnimControl* pAnimControl = m_pOwner ? m_pOwner->GetAnimControl() : nullptr;
	if (pAnimControl)
	{
		m_basePoseAnim.SetSourceId(pDcSettings->m_basePose);
		m_basePoseAnim = pAnimControl->LookupAnimCached(m_basePoseAnim);
	}
	else
	{
		m_basePoseAnim.Reset();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BlinkController::CreateAnimCmds(const AnimCmdGenLayerContext& context,
									 AnimCmdList* pAnimCmdList,
									 U32F outputInstance) const
{
	pAnimCmdList->AddCmd_EvaluateEmptyPose(outputInstance);
	
	if ((m_numActiveBlinks == 0) || FALSE_IN_FINAL_BUILD(g_animOptions.m_blinks.m_disableBlinks))
	{
		return;
	}

	if (const ArtItemAnim* pBasePoseAnim = m_basePoseAnim.GetAnim().ToArtItem())
	{
		pAnimCmdList->AddCmd_EvaluateClip(pBasePoseAnim, outputInstance, 0.0f);
	}

	const DC::BlinkSettings* pDcSettings = GetDcSettings();
	const DC::BlinkSettings dcSettings = pDcSettings ? *pDcSettings : DC::BlinkSettings();

	const float blendTime = dcSettings.m_blendTime;
	const DC::AnimCurveType blendCurve = dcSettings.m_blendCurve;

	for (I32F i = 0; i < m_numActiveBlinks; ++i)
	{
		const ArtItemAnim* pBlinkAnim = m_activeBlinks[i].m_animLookup.GetAnim().ToArtItem();

		if (!pBlinkAnim)
			continue;

		const float maxFrameSample = pBlinkAnim->m_pClipData->m_fNumFrameIntervals;
		const float blinkFrame	   = m_activeBlinks[i].m_phase * maxFrameSample;

		const float blinkDuration  = GetDuration(pBlinkAnim);
		const float blinkTimeSec   = m_activeBlinks[i].m_phase * blinkDuration;
		const float blinkFadeStart = LerpScaleClamp(0.0f, blendTime, 0.0f, 1.0f, blinkTimeSec);
		const float blinkFadeEnd   = LerpScaleClamp(blinkDuration - blendTime, blinkDuration, 1.0f, 0.0f, blinkTimeSec);
		const float blinkFade	   = Min(blinkFadeStart, blinkFadeEnd);
		const float blinkBlend	   = CalculateCurveValue(blinkFade, blendCurve);

		pAnimCmdList->AddCmd_EvaluateClip(pBlinkAnim, outputInstance + 1, blinkFrame);
		pAnimCmdList->AddCmd_EvaluateBlend(outputInstance,
										   outputInstance + 1,
										   outputInstance,
										   ndanim::kBlendSlerp,
										   blinkBlend);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float BlinkController::GetFadeMult() const
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_blinks.m_disableBlinks))
		return 0.0f;

	if (!m_settings.Valid())
		return 0.0f;

	return 1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BlinkController::DebugPrint(MsgOutput output) const
{
	STRIP_IN_FINAL_BUILD;

	ParentClass::DebugPrint(output);

	if (const DC::BlinkSettings* pEmoBlinks = m_emoSettings)
	{
		PrintTo(output,
				" %s [emo: %s]",
				DevKitOnly_StringIdToString(m_emoSettings.GetId()),
				DevKitOnly_StringIdToString(m_lastEmoState));
	}
	else
	{
		PrintTo(output, " %s", DevKitOnly_StringIdToString(m_settings.GetId()));
	}

	if (m_activeFeatherBlend)
	{
		PrintTo(output, " [feather blend: %s]", DevKitOnly_StringIdToString(m_activeFeatherBlend));
	}

	if (const ArtItemAnim* pBasePoseAnim = m_basePoseAnim.GetAnim().ToArtItem())
	{
		PrintTo(output, " [base: %s]", pBasePoseAnim->GetName());
	}

	if (g_animOptions.m_debugPrint.m_simplified)
	{
		PrintTo(output, " : %d blinks\n", m_numActiveBlinks);

	}
	else
	{
		PrintTo(output, "\n");

		if (m_suppressionTimeSec > 0.0f)
		{
			PrintTo(output, " ** SUPPRESSED ** (%0.1f sec rem)\n", m_suppressionTimeSec);
		}

		const DC::BlinkSettings* pDcSettings = GetDcSettings();
		const DC::BlinkSettings dcSettings = pDcSettings ? *pDcSettings : DC::BlinkSettings();

		const float blendTime = dcSettings.m_blendTime;
		const DC::AnimCurveType blendCurve = dcSettings.m_blendCurve;

		for (I32F i = 0; i < m_numActiveBlinks; ++i)
		{
			const ArtItemAnim* pBlinkAnim = m_activeBlinks[i].m_animLookup.GetAnim().ToArtItem();

			if (!pBlinkAnim)
				continue;

			const float maxFrameSample = pBlinkAnim->m_pClipData->m_fNumFrameIntervals;
			const float blinkFrame = m_activeBlinks[i].m_phase * maxFrameSample;

			const float blinkDuration = GetDuration(pBlinkAnim);
			const float blinkTimeSec = m_activeBlinks[i].m_phase * blinkDuration;
			const float blinkFadeStart = LerpScaleClamp(0.0f, blendTime, 0.0f, 1.0f, blinkTimeSec);
			const float blinkFadeEnd = LerpScaleClamp(blinkDuration - blendTime, blinkDuration, 1.0f, 0.0f, blinkTimeSec);
			const float blinkFade = Min(blinkFadeStart, blinkFadeEnd);
			const float blinkBlend = CalculateCurveValue(blinkFade, blendCurve);

			PrintTo(output,
					"  %s : %0.3f / %0.3f (%0.3f) @ %0.3f\n",
					pBlinkAnim->GetName(),
					blinkFrame,
					maxFrameSample,
					m_activeBlinks[i].m_phase,
					blinkBlend);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::BlinkSettings* BlinkController::GetDcSettings() const
{
	if (const DC::BlinkSettings* pEmoSettings = m_emoSettings)
	{
		return pEmoSettings;
	}

	return m_settings;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BlinkController::IsOwnerDead() const
{
	bool dead = false;

	if (const NavCharacter* pNavChar = NavCharacter::FromProcess(m_pOwner))
	{
		dead = pNavChar->IsDead();
	}

	return dead;
}
