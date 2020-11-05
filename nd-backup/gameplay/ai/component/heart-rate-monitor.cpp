/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/component/heart-rate-monitor.h"

#include "corelib/util/random.h"

#include "ndlib/process/debug-selection.h"
#include "ndlib/process/event.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/util/point-curve.h"
#include "ndlib/util/quick-plot.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/audio/arm.h"
#include "gamelib/audio/nd-vox-controller-base.h"
#include "gamelib/audio/sfx-process.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/scriptx/h/anim-nav-character-info.h"

static const U32 s_uCcHeartRate		  = CcVar("heart-rate");
static const U32 s_uCcTargetHeartRate = CcVar("target-heart-rate");

CONST_EXPR StringId64 kBreathLayerId = SID("breath");

// ------------------------------------------------------------------------------------------------------
// HeartRateMonitor: Simulated persistent heart rate
// ------------------------------------------------------------------------------------------------------
void HeartRateMonitor::Init(StringId64 heartRateStateListId, StringId64 dcNamespace)
{
	m_heartRate = 0.0f;
	m_prevStateHeartRate   = 0.0f;
	m_heartRateStateId	   = INVALID_STRING_ID_64;
	m_lastPlayedGesture	   = INVALID_STRING_ID_64;
	m_heartRateStateListId = heartRateStateListId;
	m_lastStateTime		   = TimeFrameNegInfinity();
	m_nextGestureTime	   = TimeFrameNegInfinity();
	m_inExertion = false;
	m_suppressGesturesOneFrame = false;
	m_tensionModeOverride	   = -1;
	m_curBreathAnimFromScriptOverride = false;

	m_pCurBreathAnim = nullptr;
	m_breathAnimTableOverride = INVALID_STRING_ID_64;
	m_pHeartRateState  = nullptr;
	m_pHeartRateStates = ScriptPointer<DC::HeartRateStateList>(heartRateStateListId, dcNamespace);

	if (!m_pHeartRateStates)
	{
		MsgScriptErr("Failed to find heart rate settings: %s\n", DevKitOnly_StringIdToString(heartRateStateListId));
		Reset();
		return;
	}

	if (m_pHeartRateStates->m_gestureList != INVALID_STRING_ID_64)
	{
		m_pGestures = ScriptPointer<DC::HeartRateGestureList>(m_pHeartRateStates->m_gestureList, SID("ai"));
	}
}

// -----------------------------------------------------------------------------------------------------------
void HeartRateMonitor::Reset()
{
	m_heartRate = 0.0f;
	m_prevStateHeartRate  = 0.0f;
	m_heartRateStateId	  = INVALID_STRING_ID_64;
	m_lastPlayedGesture	  = INVALID_STRING_ID_64;
	m_lastStateTime		  = TimeFrameNegInfinity();
	m_nextGestureTime	  = TimeFrameNegInfinity();
	m_inExertion = false;
	m_suppressGesturesOneFrame = false;
	m_tensionModeOverride	   = -1;
	m_curBreathAnimFromScriptOverride = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void HeartRateMonitor::Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound) {}

/// --------------------------------------------------------------------------------------------------------------- ///
void HeartRateMonitor::UpdateDc()
{
	m_pHeartRateState = nullptr;
	if (m_pHeartRateStates)
	{
		for (int i = 0; i < m_pHeartRateStates->m_count; i++)
		{
			const DC::HeartRateState& heartRateState = m_pHeartRateStates->m_array[i];

			if (heartRateState.m_name == m_heartRateStateId)
			{
				m_pHeartRateState = &heartRateState;
				break;
			}
		}

		const StringId64 oldGestureListId = m_pGestures ? m_pGestures.GetId() : INVALID_STRING_ID_64;
		if (m_pHeartRateStates->m_gestureList != oldGestureListId)
		{
			m_pGestures = ScriptPointer<DC::HeartRateGestureList>(m_pHeartRateStates->m_gestureList, SID("ai"));
		}
	}

	m_pCurBreathAnim = nullptr;
}

// -----------------------------------------------------------------------------------------------------------
void HeartRateMonitor::SetBreathAnimTable(StringId64 tableId)
{
	if (tableId == INVALID_STRING_ID_64)
	{
		m_pBreathAnims = ScriptPointer<DC::BreathAnimTable>();
	}
	else
	{
		m_pBreathAnims = ScriptPointer<DC::BreathAnimTable>(tableId, SID("anim-character/character-breath"));
		AI_ASSERT(m_pBreathAnims);
	}
}

// -----------------------------------------------------------------------------------------------------------
void HeartRateMonitor::OverrideBreathAnimTableFrame(StringId64 tableId)
{
	m_breathAnimTableOverride = tableId;
	if (m_breathAnimTableOverride != INVALID_STRING_ID_64 && m_pBreathAnimsOverride.GetId() != m_breathAnimTableOverride)
	{
		m_pBreathAnimsOverride = ScriptPointer<DC::BreathAnimTable>(tableId, SID("anim-character/character-breath"));

		if (FALSE_IN_FINAL_BUILD(!m_pBreathAnimsOverride))
		{
			SetColor(kMsgCon, kColorRed.ToAbgr8());
			MsgCon("Failed to override breath-anim-table %s\n", DevKitOnly_StringIdToString(tableId));
			SetColor(kMsgCon, kColorWhite.ToAbgr8());
		}
	}
}

// -----------------------------------------------------------------------------------------------------------
void HeartRateMonitor::ComputeHeartRate(Character* pOwner)
{
	if (m_pHeartRateState == nullptr)
		return;

	if (m_lastStateTime <= TimeFrameZero())
	{
		m_heartRate = m_pHeartRateState->m_targetValue;
		return;
	}

	const TimeFrame curTime	   = pOwner->GetCurTime();
	const TimeFrame timePassed = pOwner->GetClock()->GetTimePassed(m_lastStateTime);

	const F32 delta		  = m_pHeartRateState->m_targetValue - m_prevStateHeartRate;
	const bool isDecaying = delta < 0.0f;

	const F32 equilibriumTime = NdUtil::EvaluatePointCurve(Abs(delta),
														   isDecaying ? m_pHeartRateState->m_decayDuration
																	  : m_pHeartRateState->m_growthDuration);

	const F32 secPassed = timePassed.ToSeconds();

	if (secPassed > equilibriumTime || equilibriumTime < NDI_FLT_EPSILON)
	{
		m_heartRate = m_pHeartRateState->m_targetValue;
		return;
	}

	const F32 phase = secPassed / equilibriumTime;
	const F32 slope = delta / equilibriumTime;
	const F32 linearComponent = m_prevStateHeartRate + (slope * secPassed);

	AI_ASSERT(phase >= 0.0f && phase <= 1.0f);

	if (isDecaying)
	{
		const F32 cosComponent = Cos(PI * phase) * Abs(delta / 2)
								 + ((m_pHeartRateState->m_targetValue + m_prevStateHeartRate) / 2);

		m_heartRate = Lerp(linearComponent, cosComponent, m_pHeartRateState->m_decayCosMagnitude);
	}
	else
	{
		const F32 sinComponent = Sin(PI * (phase / 2)) * Abs(delta) + m_prevStateHeartRate;

		m_heartRate = Lerp(linearComponent, sinComponent, m_pHeartRateState->m_growthSineMagnitude);
	}
}

// -----------------------------------------------------------------------------------------------------------
void HeartRateMonitor::ElevateHeartRateToTensionMax()
{
	if (m_pHeartRateState && m_pHeartRateState->m_elevateToTensionMax > 0.0)
	{
		m_heartRate = m_pHeartRateState->m_elevateToTensionMax;
		// Need to set something to trigger curve generation
		m_heartRateStateId = SID("ElevateHeartRateToTensionMax");
	}
}

// -----------------------------------------------------------------------------------------------------------
void HeartRateMonitor::Update(Character* pOwner)
{
	if (!pOwner)
		return;

	if (!pOwner->IsNetOwnedByMe())
	{
		UpdateDc();
		return;
	}


	bool wasInExertion = m_inExertion;
	HeartRateMonitor::HeartRateUpdateParams params = m_heartRateUpdateOverride.m_stateName == INVALID_STRING_ID_64
														 ? pOwner->GetHeartRateUpdateParams(m_tensionModeOverride)
														 : m_heartRateUpdateOverride;

	m_inExertion = params.m_inExertion;
	m_tensionModeOverride = -1;

	if (params.m_stateName != m_heartRateStateId && m_pHeartRateStates != nullptr)
	{
		StringId64 curHeartRateState = m_heartRateStateId;
		m_heartRateStateId = params.m_stateName;
		UpdateDc();

		if (m_pHeartRateState == nullptr || params.m_stateName != m_pHeartRateState->m_name)
		{
			m_heartRateStateId = curHeartRateState;
		}
		else
		{
			m_prevStateHeartRate = m_heartRate;
			m_lastStateTime		 = pOwner->GetCurTime();
		}
	}

	const float lastHeartRate = m_heartRate;

	if (!wasInExertion && m_inExertion && m_pHeartRateState)
	{
		const F32 maxHeartRate = (m_heartRate > m_pHeartRateState->m_targetValue + m_pHeartRateState->m_exertionOverCap
													+ NDI_FLT_EPSILON)
									 ? m_prevStateHeartRate
									 : m_pHeartRateState->m_targetValue + m_pHeartRateState->m_exertionOverCap;

		m_heartRate = Min(m_heartRate + m_pHeartRateState->m_exertionNudge, maxHeartRate);
		m_prevStateHeartRate = m_heartRate;
		m_lastStateTime		 = pOwner->GetCurTime();
	}

	ComputeHeartRate(pOwner);

	m_heartRate = Limit01(Min(m_heartRate, params.m_maxHeartRate));

	TryGesture(pOwner);

	if (!params.m_allowTrackDown && lastHeartRate > m_heartRate)
	{
		m_heartRate = lastHeartRate;
		m_prevStateHeartRate = m_heartRate;
		m_lastStateTime		 = pOwner->GetCurTime();
	}

	m_heartRateUpdateOverride.m_stateName = INVALID_STRING_ID_64;
	m_suppressGesturesOneFrame = false;

	m_pCurBreathAnim = GetValidBreathAnim(pOwner, &m_curBreathAnimFromScriptOverride);
	m_breathAnimTableOverride = INVALID_STRING_ID_64;

	if (pOwner->GetVoxControllerConst().GetCurrentVoxPriority() > DC::kVoxPriorityBreathHigh)
	{
		// Don't allow breath anims when vox is active
		if (AnimControl* pAnimControl = pOwner->GetAnimControl())
		{
			pAnimControl->FadeLayer(kBreathLayerId, 0.0f, 0.4f);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool HeartRateMonitor::TryGesture(Character* pOwner)
{
	if (m_suppressGesturesOneFrame)
		return false;

	IGestureController* pGestureController = pOwner->GetGestureController();
	if (!pGestureController)
		return false;

	if (m_lastPlayedGesture != INVALID_STRING_ID_64 && pGestureController->IsPlayingOnAnyLayer(m_lastPlayedGesture))
		return false;

	if (pOwner->GetCurTime() < m_nextGestureTime)
		return false;

	if (pOwner->IsDead())
		return false;

	if (!m_pHeartRateState)
		return false;

	if (!m_pGestures || m_pGestures->m_count < 1)
		return false;

	const DC::AiRangeval* pGestureRateRange = pOwner->GetHeartRateMonitor()
												  ->GetCurrentHeartRateState()
												  ->m_gestureRateRange;

	if (!pGestureRateRange || pGestureRateRange->m_val0 <= 0.0f || pGestureRateRange->m_val1 <= 0.0f)
		return false;

	BoxedValue hasTargetResponse = SendEvent(SID("has-target?"), pOwner);
	const bool hasTarget		 = hasTargetResponse.GetAsBool(false);

	static CONST_EXPR int kMaxValidGestures = 8;
	StringId64 validGestures[kMaxValidGestures];
	int foundValid = 0;
	for (int iGesture = 0; iGesture < m_pGestures->m_count && foundValid < kMaxValidGestures; iGesture++)
	{
		const DC::HeartRateGestureListEntry gesture = m_pGestures->m_array[iGesture];

		// invalid current HR
		if (gesture.m_validCurrentHrRange
			&& (gesture.m_validCurrentHrRange->m_val0 > m_heartRate
				|| gesture.m_validCurrentHrRange->m_val1 < m_heartRate))
			continue;

		// invalid target HR
		if (gesture.m_validTargetHrRange
			&& (gesture.m_validTargetHrRange->m_val0 > m_pHeartRateState->m_targetValue
				|| gesture.m_validTargetHrRange->m_val1 < m_pHeartRateState->m_targetValue))
			continue;

		if ((gesture.m_filter & DC::kHeartRateGestureFilterHasTarget) && !hasTarget)
			continue;

		if ((gesture.m_filter & DC::kHeartRateGestureFilterNoTarget) && hasTarget)
			continue;

		if (gesture.m_validStateList)
		{
			bool stateValid = false;
			for (int iState = 0; iState < gesture.m_validStateList->m_count; iState++)
			{
				if (gesture.m_validStateList->m_array[iState] == m_heartRateStateId)
				{
					stateValid = true;
					break;
				}
			}

			if (!stateValid)
				continue;
		}

		validGestures[foundValid++] = gesture.m_gestureId;
	}

	if (!foundValid)
		return false;

	int selectedIdx		= RandomIntRange(0, foundValid - 1);
	StringId64 gesture	= validGestures[selectedIdx];
	Gesture::Err result = pGestureController->Play(gesture);

	m_nextGestureTime = pOwner->GetCurTime()
						+ Seconds(RandomFloatRange(pGestureRateRange->m_val0, pGestureRateRange->m_val1));

	if (result.Success())
	{
		m_lastPlayedGesture = gesture;
	}
	else
	{
		MsgAnimErr("%s Failed to play Heart Rate Gesture: %s : %s\n",
				   pOwner->GetName(),
				   DevKitOnly_StringIdToString(gesture),
				   DevKitOnly_StringIdToString(result.m_errId));
	}

	return result.Success();
}

// -----------------------------------------------------------------------------------------------------------
const DC::BreathAnim* HeartRateMonitor::GetValidBreathAnim(const Character* pOwner, bool* pIsScriptOverrideOut, bool debug) const
{
	const bool debugDraw = FALSE_IN_FINAL_BUILD(debug);

	if (FALSE_IN_FINAL_BUILD(pIsScriptOverrideOut))
	{
		*pIsScriptOverrideOut = m_pBreathAnimsOverride && m_pBreathAnimsOverride.GetId() == m_breathAnimTableOverride;
	}

	const ScriptPointer<DC::BreathAnimTable> pBreathAnims = (m_pBreathAnimsOverride && m_pBreathAnimsOverride.GetId() == m_breathAnimTableOverride)
		|| (FALSE_IN_FINAL_BUILD(debugDraw && m_curBreathAnimFromScriptOverride))
		? m_pBreathAnimsOverride
		: m_pBreathAnims;

	if (!pBreathAnims)
	{
		return nullptr;
	}

	if (debugDraw)
	{
		MsgCon("Breath Anim - %s - %s\n", DevKitOnly_StringIdToString(pOwner->GetUserId()), DevKitOnly_StringIdToString(pBreathAnims.GetId()));
	}

	IGestureController* pGestureController = pOwner->GetGestureController();
	if (!pGestureController)
		return nullptr;

	if (m_lastPlayedGesture != INVALID_STRING_ID_64 && pGestureController->IsPlayingOnAnyLayer(m_lastPlayedGesture))
	{
		if (debugDraw)
		{
			MsgCon("  Playing HR Gesture\n");
		}
		return nullptr;
	}
	const F32 targetHeartRate = GetTargetHeartRate();
	static const F32 kHeartRateEpsilon		   = 0.01f;
	const DC::HeartRateTrackingDir trackingDir = targetHeartRate > m_heartRate + kHeartRateEpsilon
													 ? DC::kHeartRateTrackingDirIncreasing
													 : (targetHeartRate + kHeartRateEpsilon < m_heartRate
															? DC::kHeartRateTrackingDirDecreasing
															: DC::kHeartRateTrackingDirStable);

	for (int iBreath = 0; iBreath < pBreathAnims->m_count; iBreath++)
	{
		const DC::BreathAnim& breath = pBreathAnims->m_array[iBreath];

		if (breath.m_currentHeartRateRange
			&& (breath.m_currentHeartRateRange->m_val0 > m_heartRate
				|| breath.m_currentHeartRateRange->m_val1 < m_heartRate))
		{
			if (debugDraw)
			{
				MsgCon("  %s - Current HR invalid\n", DevKitOnly_StringIdToString(breath.m_debugName));
			}
			continue;
		}

		if (breath.m_targetHeartRateRange
			&& (breath.m_targetHeartRateRange->m_val0 > targetHeartRate
				|| breath.m_targetHeartRateRange->m_val1 < targetHeartRate))
		{
			if (debugDraw)
			{
				MsgCon("  %s - Target HR invalid\n", DevKitOnly_StringIdToString(breath.m_debugName));
			}
			continue;
		}

		if ((breath.m_heartRateTrackingDir & trackingDir) == 0)
		{
			if (debugDraw)
			{
				MsgCon("  %s - Tracking Dir invalid\n", DevKitOnly_StringIdToString(breath.m_debugName));
			}
			continue;
		}

		const F32 vertigoPct = Max(pOwner->GetCurrentVertigoPct(), pOwner->IsInVertigoRegion() ? 0.7f : 0.0f);
		if (breath.m_vertigoRange
			&& (breath.m_vertigoRange->m_val0 > vertigoPct
				|| breath.m_vertigoRange->m_val1 < vertigoPct))
		{
			if (debugDraw)
			{
				MsgCon("  %s - Vertigo Range invalid\n", DevKitOnly_StringIdToString(breath.m_debugName));
			}
			continue;
		}

		if (debugDraw)
		{
			F32 layerFade = 1.0;
			if (breath.m_heartRateFadeBase)
			{
				layerFade = LerpScale(breath.m_currentHeartRateRange->m_val0,
									  breath.m_currentHeartRateRange->m_val1,
									  breath.m_heartRateFadeBase->m_val0,
									  breath.m_heartRateFadeBase->m_val1,
									  m_heartRate);
			}

			if (breath.m_vertigoFadeBase && breath.m_vertigoRange)
			{
				layerFade += LerpScale(breath.m_vertigoRange->m_val0,
									   breath.m_vertigoRange->m_val1,
									   breath.m_vertigoFadeBase->m_val0,
									   breath.m_vertigoFadeBase->m_val1,
									   vertigoPct);
			}

			MsgCon("  %s - Chosen (base fade: %.3f)\n", DevKitOnly_StringIdToString(breath.m_debugName), layerFade);
		}

		return &breath;
	}

	//MsgConErr("No valid breath Anim\n");
	return nullptr;
}

// -----------------------------------------------------------------------------------------------------------
void HeartRateMonitor::HandleBreathStartEvent(Character* pOwner, F32 breathLength, int context)
{
	if (TryGesture(pOwner))
		return;

	if (!m_pCurBreathAnim)
	{
		return;
	}

	AnimControl* pAnimControl = pOwner->GetAnimControl();

	AI_ASSERT(pAnimControl);
	AI_ASSERT(m_pCurBreathAnim->m_animIdList);

	if (context < 0 || context >= m_pCurBreathAnim->m_animIdList->m_count)
	{
		return;
	}

	const StringId64 animId = m_pCurBreathAnim->m_animIdList->m_array[context];
	if (const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem())
	{
		const ndanim::ClipData* pClipData = pAnim->m_pClipData;
		const F32 animLength = pClipData->m_fNumFrameIntervals * pClipData->m_secondsPerFrame;

		const AnimLayer* pFaceLayer = pAnimControl->GetLayerById(SID("Face"));
		const AnimLayer* pFacialBaseLayer = pAnimControl->GetLayerById(SID("facial-base"));

		const bool hasValidFaceLayer = (pFaceLayer && pFaceLayer->GetCurrentFade() > 0.0f)
									   || (pFacialBaseLayer && pFacialBaseLayer->GetCurrentFade() > 0.0f);

		if (hasValidFaceLayer)
		{
			AnimSimpleLayer* pLayer = pAnimControl->GetSimpleLayerById(kBreathLayerId);

			if (!pLayer)
			{
				pLayer = pAnimControl->CreateSimpleLayer(kBreathLayerId);
				if (pLayer)
				{
					pLayer->SetCurrentFade(0.0f);
				}
			}

			if (pLayer)
			{
				AnimSimpleLayer::FadeRequestParams params;
				params.m_playbackRate = animLength / breathLength;
				F32 layerFade = 1.0f;

				if (m_pCurBreathAnim->m_heartRateFadeBase)
				{
					layerFade = LerpScale(m_pCurBreathAnim->m_currentHeartRateRange->m_val0,
										  m_pCurBreathAnim->m_currentHeartRateRange->m_val1,
										  m_pCurBreathAnim->m_heartRateFadeBase->m_val0,
										  m_pCurBreathAnim->m_heartRateFadeBase->m_val1,
										  m_heartRate);
				}

				if (m_pCurBreathAnim->m_vertigoFadeBase && m_pCurBreathAnim->m_vertigoRange)
				{
					const F32 vertigoPct = Max(pOwner->GetCurrentVertigoPct(),
											   pOwner->IsInVertigoRegion() ? 0.7f : 0.0f);
					layerFade += LerpScale(m_pCurBreathAnim->m_vertigoRange->m_val0,
										   m_pCurBreathAnim->m_vertigoRange->m_val1,
										   m_pCurBreathAnim->m_vertigoFadeBase->m_val0,
										   m_pCurBreathAnim->m_vertigoFadeBase->m_val1,
										   vertigoPct);
				}

				if (m_pCurBreathAnim->m_randomFadeVariance)
				{
					layerFade += RandomFloatRange(m_pCurBreathAnim->m_randomFadeVariance->m_val0,
												  m_pCurBreathAnim->m_randomFadeVariance->m_val1);
				}

				params.m_layerFade = Limit01(layerFade);

				const F32 curFade = pLayer->GetCurrentFade();
				const F32 curFadeDelta = curFade > 0.0f ? Abs(curFade - params.m_layerFade) : 0.0f;
				const F32 crossFadeFactor = curFadeDelta * 0.7f;

				params.m_fadeTime = Max(m_pCurBreathAnim->m_blendTime, crossFadeFactor);

				AnimSimpleLayer::FadeOutOnCompleteParams cparams;
				cparams.m_enabled = true;

				params.m_layerFadeOutParams = cparams;

				pLayer->RequestFadeToAnim(animId, params);
			}
			else
			{
				pLayer->Fade(0.0f, 0.0f);
			}
		}
	}
}

// -----------------------------------------------------------------------------------------------------------
void HeartRateMonitor::SetCcVars(SfxProcess* pSfx)
{
	if (!pSfx || !pSfx->IsValid())
		return;

	SendEvent(SID("set-variable"), pSfx, s_uCcHeartRate, GetCurrentHeartRate(), 0U);
	SendEvent(SID("set-variable"), pSfx, s_uCcTargetHeartRate, GetTargetHeartRate(), 0U);
}

// -----------------------------------------------------------------------------------------------------------
void HeartRateMonitor::DebugDraw(const Character* pOwner, ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	if (!m_pHeartRateState || !pOwner)
		return;

	const F32 gestureRateMin = m_pHeartRateState->m_gestureRateRange ? m_pHeartRateState->m_gestureRateRange->m_val0
																	 : -1.0f;
	const F32 gestureRateMax = m_pHeartRateState->m_gestureRateRange ? m_pHeartRateState->m_gestureRateRange->m_val1
																	 : -1.0f;

	if (g_ndAiOptions.m_debugHeartRateGraph && DebugSelection::Get().IsOnlyProcessSelected(pOwner))
	{
		GraphDisplay::Params params;
		params.m_dataColor = AI::IndexToColor(pOwner->GetUserId().GetValue()).ToAbgr8();
		if (!EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
		{
			QUICK_PLOT("Heart Rate", "Heart Rate", 800, 530, 300, 120, 120, params, m_heartRate);
		}

		ScreenSpaceTextPrinter printer(860.0f, 480.0f);
		printer.SetScale(0.8f);
		printer.PrintText(kColorGreen,
						  "%-25s\n  State: %-10s\n    Rate/Target: %0.3f / %0.3f\n    Gesture Rate: [ %0.2f - %0.2f ]\n",
						  pOwner->GetName(),
						  DevKitOnly_StringIdToString(m_heartRateStateId),
						  m_heartRate,
						  m_pHeartRateState->m_targetValue,
						  gestureRateMin,
						  gestureRateMax);
	}
	else if (DebugSelection::Get().IsProcessOrNoneSelected(pOwner))
	{
		if (g_ndAiOptions.m_debugHeartRateList)
		{
			const Color targColor = Lerp(kColorWhite, kColorRed, m_pHeartRateState->m_targetValue / 1.0f);

			SetColor(kMsgCon, kColorGreen.ToAbgr8());
			MsgCon("Heart Rate: %-32s  %-18s", pOwner->GetName(), DevKitOnly_StringIdToString(m_heartRateStateId));

			SetColor(kMsgCon, targColor.ToAbgr8());
			MsgCon("  Rate/Target: %0.3f / %0.3f", m_heartRate, m_pHeartRateState->m_targetValue);

			SetColor(kMsgCon, kColorGreen.ToAbgr8());
			if (gestureRateMin >= 0.0f)
			{
				MsgCon("\n    Gesture Rate: [ %0.2f - %0.2f ]", gestureRateMin, gestureRateMax);
			}

			MsgCon("\n");
			SetColor(kMsgCon, kColorWhite.ToAbgr8());
		}
	}

	if (g_ndAiOptions.m_debugHeartRateWs && pPrinter && DebugSelection::Get().IsProcessOrNoneSelected(pOwner))
	{
		Color curCol  = Lerp(kColorWhite, kColorRed, m_heartRate);
		Color targCol = Lerp(kColorWhite, kColorRed, m_pHeartRateState->m_targetValue);

		pPrinter->PrintText(targCol, "Target Rate: %.3f\n", m_pHeartRateState->m_targetValue);
		pPrinter->PrintText(curCol, "Heart Rate:  %.3f\n", m_heartRate);
	}

	if (g_ndAiOptions.m_debugBreathAnims && DebugSelection::Get().IsProcessOrNoneSelected(pOwner))
	{
		GetValidBreathAnim(pOwner, nullptr, true);
	}
}
