/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/dialog-look.h"

#include "corelib/math/point-util.h"
#include "corelib/math/vector-util.h"

#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/render/util/prim-server-wrapper.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/audio/arm.h"
#include "gamelib/audio/nd-vox-controller-base.h"
#include "gamelib/gameplay/ai/controller/nd-animation-controllers.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/scriptx/h/nd-ai-defines.h"
#include "gamelib/scriptx/h/nd-gesture-script-defines.h"
#include "gamelib/tasks/task-graph-mgr.h"

/// --------------------------------------------------------------------------------------------------------------- ///
DialogLook::DialogLook()
	: m_state(DialogLook::kInactive)
	, m_voxId(INVALID_STRING_ID_64)
	, m_settingsId(SID("*default-dialog-look-settings*"))
	, m_enable(true)
	, m_oneFrameDisabled(false)
	, m_oneFrameGesturesDisabled(false)
	, m_myLookBlend(0.0f)
	, m_disabledTime(TimeFrameNegInfinity())
	, m_gestureDisabledTime(TimeFrameNegInfinity())
	, m_myPrevLookBlend(0.0f)
	, m_myLookAngleSignHysteresis(0.0f)
	, m_lookAtPos(kZero)
	, m_lookAtBlend(0.0f)
	, m_lastOutOfAngleTime(TimeFrameNegInfinity())
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DialogLook::Start(Character* const pOwner, bool isSpeaking, const Character* const pOther)
{
	const DC::AiDialogLookSettings* const pSettings = ScriptManager::LookupInNamespace<DC::AiDialogLookSettings>(m_settingsId,
																												 SID("ai"),
																												 nullptr);

	if (!m_enable || (pSettings == nullptr) ||
		pOwner == pOther)	// Character talking to self
	{
		EnterInactive();
		return;
	}

	const DC::VoxInfo* const pVoxInfo = pOwner->GetVoxControllerConst().GetCurrentVoxInfo();
	if (pVoxInfo &&
		(pVoxInfo->m_params.m_options & DC::kVoxOptionsDisableSystemicGesture))
	{
		EnterInactive();
		return;
	}

	GAMEPLAY_ASSERT(pOwner && pOwner->IsKindOf(g_type_Character));
	GAMEPLAY_ASSERT(pOther && pOther->IsKindOf(g_type_Character));

	m_owner = pOwner;
	m_other = pOther;

	m_isSpeaking = isSpeaking;
	if (m_isSpeaking)
	{
		m_voxId = pOwner->GetVoxControllerConst().GetCurrentVoxId();
		m_startTime = pOwner->GetCurTime();
	}
	else
	{
		m_voxId = pOther->GetVoxControllerConst().GetCurrentVoxId();
		m_startTime = pOther->GetCurTime();
	}
	m_lookStartTime = m_startTime + Seconds(AI::RandomRange(pSettings->m_delay));

	if (m_startTime - m_lastOutOfAngleTime < Seconds(pSettings->m_outOfAngleCooldown))
	{
		m_lookEndTime = m_startTime;
		m_noLookReason = SID("out-of-angle-cooldown-not-passed");
	}
	else
	{
		m_lookEndTime = TimeFramePosInfinity();
	}


	const TimeFrame curTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();
	m_lastUpdateTime = curTime - EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetDeltaTimeFrame();

	EnterPrepare();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DialogLook::Update(bool forceDisable)
{
	const TimeFrame currentTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();
	const float dt = ToSeconds(currentTime - m_lastUpdateTime);

	// Disabled via task flag?
	if (const GameTaskSubnode* const pSubNode = g_taskGraphMgr.GetActiveContinueSubnode())
	{
		if (pSubNode->m_flags & DC::kNdTaskNodeFlagDisableDialogLook)
		{
			EnterInactive();
			return;
		}
	}

	// No settings?
	const Character* const pOwner = m_owner.ToProcess();
	const DC::AiDialogLookSettings* const pSettings = ScriptManager::LookupInNamespace<DC::AiDialogLookSettings>(m_settingsId,
																												 SID("ai"),
																												 nullptr);
	if (!pSettings || forceDisable || !pOwner)
	{
		EnterInactive();
		return;
	}

	if (pOwner && pOwner->OnNarrowBalanceBeam())
	{
		EnterInactive();
		return;
	}

	float angle = 0.0f;

	if (m_currentAngle > 0.0f)
		angle = m_currentAngle;

	float maxAngle = pSettings->m_standingRangeAngle;
	if (const Character* pCharacter = Character::FromProcess(pOwner))
	{
		if (Length(pCharacter->GetVelocityWs()) > 0.2f)
		{
			maxAngle = pSettings->m_movingRangeAngle;
		}
	}

	if (m_lookEndTime == TimeFramePosInfinity())
	{
		// we are within angle, so do nothing
		if (angle > maxAngle)
		{
			m_lookEndTime = currentTime + Seconds(AI::RandomRange(pSettings->m_outOfAngleDuration));
			m_noLookReason = SID("out-of-angle-time-expired");
			m_lastOutOfAngleTime = m_lookEndTime;
		}
	}

	// No other
	const Character* const pOther = m_other.ToProcess();
	if (!pOther)
	{
		EnterInactive();
		return;
	}

	GAMEPLAY_ASSERT(pOther->IsKindOf(g_type_Character));

	BlendLook(currentTime, dt, pSettings);
	m_lastUpdateTime = currentTime;

	// No owner
	if (pOwner->IsAimingFirearm())
	{
		EnterInactive();
		return;
	}

	//
	const DC::Map* const pEmotionMap = ScriptManager::LookupInNamespace<DC::Map>(pOwner->GetEmotionMapId(),
																				 SID("ai"),
																				 nullptr);
	if (!pEmotionMap)
	{
		EnterInactive();
		return;
	}

	// One frame disable
	if (m_oneFrameDisabled)
	{
 		EnterInactive();
 		return;
	}

	StringId64 currentVoxId = m_isSpeaking ? pOwner->GetVoxControllerConst().GetCurrentVoxId()
										   : pOther->GetVoxControllerConst().GetCurrentVoxId();

	if (currentVoxId != m_voxId)
	{
		EnterInactive();
		return;
	}

	switch (m_state)
	{
	case DialogLook::kPrepare:
		UpdatePrepare(currentTime, pSettings, pEmotionMap);
		break;
	case DialogLook::kTalking:
		UpdateTalking(currentTime, dt, pSettings);
		break;
	case DialogLook::kSilence:
		UpdateSilence(currentTime, pSettings, pEmotionMap);
		break;
	default:
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DialogLook::EnterInactive()
{
	const Character* const pOwner = m_owner.ToProcess();
	if (m_state == DialogState::kTalking && pOwner)
	{
		m_gestureHandle.Clear(pOwner->GetGestureController());
	}

	const DC::AiDialogLookSettings* const pSettings = ScriptManager::LookupInNamespace<DC::AiDialogLookSettings>(m_settingsId,
																												 SID("ai"),
																												 nullptr);

	if (pSettings && m_lookEndTime == TimeFramePosInfinity() && pOwner)
	{
		m_noLookReason = SID("in-angle-time-expired");
		m_lookEndTime = pOwner->GetCurTime() + Seconds(AI::RandomRange(pSettings->m_inAngleExtraHold));
	}

	if (pOwner && pOwner->GetCurTime() > m_disabledTime)
	{
		m_oneFrameDisabled = false;
	}

	if (pOwner && pOwner->GetCurTime() > m_gestureDisabledTime)
	{
		m_oneFrameGesturesDisabled = false;
	}

	m_state = DialogState::kInactive;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DialogLook::EnterPrepare()
{
	m_timer = TimeFramePosInfinity();	// Reset timer for prepare time
	m_currentAngle = 0.0f;
	m_state = DialogLook::kPrepare;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DialogLook::UpdatePrepare(const TimeFrame currentTime,
							   const DC::AiDialogLookSettings* const pSettings,
							   const DC::Map* const pEmotionMap)
{
	if (m_timer == TimeFramePosInfinity())
	{
		const Character* const pOwner = m_owner.ToProcess();
		const ArmProcHandle handle = pOwner->GetVoxControllerConst().GetAudioHandle();

		const float lenInSecs = EngineComponents::GetAudioManager()->GetLength(handle);
		if (lenInSecs > 0.0f)	// If lengthInSeconds <= 0.0f, we are still waiting to know how long this sound is, don't play the gesture yet
		{
			if (lenInSecs < pSettings->m_shortDialogThreshold)
			{
				const F32 randomDelay = AI::RandomRange(pSettings->m_shortDialogGestureDelay);
				m_timer = m_startTime + Seconds(randomDelay);
			}
			else
			{
				const F32 randomDelay = AI::RandomRange(pSettings->m_longDialogGestureDelay);
				m_timer = m_startTime + Seconds(randomDelay);
			}
		}
	}
	else if (currentTime > m_timer)
	{
		EnterTalking(currentTime, pSettings, pEmotionMap);
	}

	// Figure out how much time left in this vox
	const Character* const pOwner = m_owner.ToProcess();

	bool inPerformance = false;
	if (pOwner && pOwner->GetEnteredActionPack() != nullptr && pOwner->GetEnteredActionPack()->GetType() == ActionPack::kCinematicActionPack)
		inPerformance = true;
	if (pOwner->IsKindOf(g_type_NavCharacter) && !((NavCharacter*)pOwner)->GetNdAnimationControllers()->GetLocomotionController()->AllowDialogLookGestures())
		inPerformance = true;

	if (m_oneFrameGesturesDisabled || inPerformance)
	{
		if (pOwner)
			m_gestureHandle.Clear(pOwner->GetGestureController());
	}


}

/// --------------------------------------------------------------------------------------------------------------- ///
void DialogLook::EnterTalking(const TimeFrame currentTime,
							  const DC::AiDialogLookSettings* const pSettings,
							  const DC::Map* const pEmotionMap)
{
	ANIM_ASSERT(m_state == DialogState::kPrepare || m_state == DialogState::kSilence);

	// Figure out how much time left in this vox
	const Character* const pOwner = m_owner.ToProcess();

	const ArmProcHandle handle = pOwner->GetVoxControllerConst().GetAudioHandle();

	if (handle == ARM_PROC_INVALID)
	{
		EnterInactive();
		return;
	}

	const float lenInSecs = EngineComponents::GetAudioManager()->GetLength(handle);

	if (lenInSecs < 0.0f)
	{
		EnterPrepare();
		return;
	}

	const TimeFrame endTime = m_startTime + Seconds(lenInSecs);
	const float pastTimeInSecs = ToSeconds(currentTime - m_startTime);
	const float leftTimeInSecs = lenInSecs - pastTimeInSecs;

	if (leftTimeInSecs <= 0.0f)		// No more time left
	{
		EnterInactive();
		return;
	}

	const StringId64 emotion = pOwner->GetEmotionControl()->GetEmotionalState().m_emotion;
	const DC::EmotionEntry* pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, emotion);
	if (!pEmotionEntry)
	{
		pEmotionEntry = ScriptManager::MapLookup<DC::EmotionEntry>(pEmotionMap, SID("neutral"));
	}

	StringId64 gestureId;
	if (leftTimeInSecs < pSettings->m_superShortDialogThreshold)
	{
		gestureId = pEmotionEntry->m_superShortConversationGesture;
	}
	else if (leftTimeInSecs < pSettings->m_shortDialogThreshold)	// Short gesture
	{
		gestureId = pEmotionEntry->m_shortConversationGesture;
	}
	else
	{
		gestureId = pEmotionEntry->m_longConversationGesture;
	}
	
	bool inPerformance = false;
	if (pOwner && pOwner->GetEnteredActionPack() != nullptr && pOwner->GetEnteredActionPack()->GetType() == ActionPack::kCinematicActionPack)
		inPerformance = true;
	if (pOwner->IsKindOf(g_type_NavCharacter) && !((NavCharacter*)pOwner)->GetNdAnimationControllers()->GetLocomotionController()->AllowDialogLookGestures())
		inPerformance = true;

	if (gestureId != SID("null") && !m_oneFrameGesturesDisabled && !inPerformance)
	{
		const StringId64 remappedGesture = Gesture::RemapGestureAndIncrementVariantIndices(gestureId, pOwner->GetAnimControl());
		const DC::GestureDef* pGesture = Gesture::LookupGesture(remappedGesture);
		if (!pGesture)
		{
			gestureId = SID("generic-vox-gesture-short-neutral");
		}

		Gesture::PlayArgs args;
		args.SetPriority(DC::kGesturePriorityLow);
		args.m_pOutGestureHandle = &m_gestureHandle;
		if (pOwner->NeedToPlayGesturesOnLayer2())
		{
			args.m_gestureLayer = pOwner->GetGestureController()->GetFirstRegularGestureLayerIndex() + 1;
		}
		pOwner->GetGestureController()->Play(gestureId, args);
	}

	
	m_timer = TimeFramePosInfinity();	// Reset timer for silence

	m_state = DialogState::kTalking;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool DialogLook::IsGesturePlaying() const
{
	if (const Character* const pOwner = m_owner.ToProcess())
	{
		return m_gestureHandle.Playing(pOwner->GetGestureController());
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DialogLook::UpdateTalking(const TimeFrame currentTime, float dt, const DC::AiDialogLookSettings* const pSettings)
{
	const bool isSilent = IsSilent();
	if (m_timer == TimeFramePosInfinity())	// Last frame not silent
	{
		if (isSilent)
		{
			m_timer = currentTime;	// First time set timer for silence
		}
	}
	else	// Last frame silent
	{
		if (isSilent)
		{
			static float kMaxSilenceTime = 0.6f;

			const float timeSinceSilenceInSecs = ToSeconds(currentTime - m_timer);
			if (timeSinceSilenceInSecs > kMaxSilenceTime)
			{
				EnterSilence(currentTime);
			}
		}
		else
		{
			m_timer = TimeFramePosInfinity();	// Reset timer for silence
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DialogLook::EnterSilence(const TimeFrame currentTime)
{
	ANIM_ASSERT(m_state == DialogState::kTalking);

	if (m_state == DialogState::kTalking)
	{
		const Character* const pOwner = m_owner.ToProcess();
		m_gestureHandle.Clear(pOwner->GetGestureController());
	}

	m_timer = TimeFramePosInfinity();	// Reset timer for talking

	m_state = DialogState::kSilence;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DialogLook::UpdateSilence(const TimeFrame currentTime,
							   const DC::AiDialogLookSettings* const pSettings,
							   const DC::Map* const pEmotionMap)
{
	const bool isTalking = !IsSilent();
	if (m_timer == TimeFramePosInfinity())	// Last frame not talking
	{
		if (isTalking)
		{
			m_timer = currentTime;	// First time set timer for talking
		}
	}
	else	// Last frame talking
	{
		if (isTalking)
		{
			static float kMaxTalkingTime = 0.4f;

			const float timeSinceTalkingInSecs = ToSeconds(currentTime - m_timer);
			if (timeSinceTalkingInSecs > kMaxTalkingTime)
			{
				EnterTalking(currentTime, pSettings, pEmotionMap);
			}
		}
		else
		{
			m_timer = TimeFramePosInfinity();	// Reset timer for talking
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool DialogLook::IsSilent() const
{
	bool isSilent = true;

	const int kMaxLayers = 20;
	NdPhoneme phonemeIndex[kMaxLayers];
	float animBlend[kMaxLayers];
	int numAnims = 0;

	const Character* const pOwner = m_owner.ToProcess();

	const NdVoxControllerBase& rVoxController = pOwner->GetVoxControllerConst();
	const NdLipsyncArticulationBlend artic = rVoxController.GetLipsyncArticulation();
	if (artic.m_uCurTime != 0xffffffffU)
	{
		const I64 range = artic.m_end.m_uStartTime - artic.m_start.m_uStartTime;
		ANIM_ASSERT(artic.m_uCurTime >= artic.m_start.m_uStartTime);

		const bool enableVisemeBlending = false;
		const float endWeight = (range == 0 || !enableVisemeBlending) ?
								0.0f :
								float(artic.m_uCurTime - artic.m_start.m_uStartTime) / range;
		const float startWeight = 1.0f - endWeight;

		for (I32F iPhoneme = 0; (iPhoneme < LIPSYNC_MAX_PPA) && (iPhoneme < kMaxLayers); iPhoneme++)
		{
			const NdPhoneme phoneme = artic.m_start.m_auPhonemes[iPhoneme];
			const U8 weight = artic.m_start.m_auWeights[iPhoneme];
			if (phoneme != kNdPhonemeInvalid)
			{
				phonemeIndex[numAnims] = phoneme;
				animBlend[numAnims] = (F32)weight / 100.0f * startWeight;
				numAnims++;
			}
		}

		for (I32F iPhoneme = 0; (iPhoneme < LIPSYNC_MAX_PPA) && (iPhoneme < kMaxLayers); iPhoneme++)
		{
			const NdPhoneme phoneme = artic.m_end.m_auPhonemes[iPhoneme];
			const U8 weight = artic.m_end.m_auWeights[iPhoneme];
			if (phoneme != kNdPhonemeInvalid)
			{
				I32F startPhoneme = 0;
				for (startPhoneme = 0; startPhoneme < numAnims; ++startPhoneme)
				{
					if (phonemeIndex[startPhoneme] == phoneme)
					{
						break;
					}
				}

				phonemeIndex[startPhoneme] = phoneme;
				const float curWeight = (F32)weight / 100.0f * endWeight;
				if (startPhoneme == numAnims)
				{
					animBlend[startPhoneme] = curWeight;
					numAnims++;
				}
				else
				{
					animBlend[startPhoneme] += curWeight;
				}
			}
		}

		bool hasAnyPhoneme = false;
		bool onlyFullNdPhonemeX = true;
		for (int i = 0; i < kNdPhonemeCount; i++)
		{
			float phonemeBlend = 0.0f;

			for (int j = 0; j < numAnims; j++)
			{
				if (phonemeIndex[j] == i)
				{
					phonemeBlend = animBlend[j];
				}
			}

			if (phonemeBlend > 0.01f)
			{
				hasAnyPhoneme = true;

				if (i != kNdPhoneme_x ||
					phonemeBlend < 1.0f)
				{
					onlyFullNdPhonemeX = false;
				}
			}
		}

		isSilent = !hasAnyPhoneme || onlyFullNdPhonemeX;
	}

	return isSilent;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DialogLook::BlendLook(const TimeFrame currentTime, float dt, const DC::AiDialogLookSettings* const pSettings)
{
	const float kBlendInTime = 1.0f;
	const float kBlendOutTime = 1.0f;
	const float kMaxLookAngle = 120.0;

	// my look blend
	float myCurvedLookBlend = 0.0f;
	const float timeSinceSpeak = ToSeconds(currentTime - m_lookStartTime);

	if (currentTime > m_lookEndTime || m_oneFrameDisabled)
	{
		m_myLookBlend -= dt / kBlendOutTime;
		m_myLookBlend = Max(0.0f, m_myLookBlend);

	}
	else if (m_myLookBlend < 1.0f)
	{
		m_myLookBlend += dt / kBlendInTime;
		m_myLookBlend = Min(1.0f, m_myLookBlend);
	}
	myCurvedLookBlend = CalculateCurveValue(m_myLookBlend, DC::kAnimCurveTypeUniformS);

	const Character* const pOwner = m_owner.ToProcess();
	const Character* const pOther = m_other.ToProcess();

	GAMEPLAY_ASSERT(pOwner && pOwner->IsKindOf(g_type_Character));
	GAMEPLAY_ASSERT(pOther && pOther->IsKindOf(g_type_Character));

	const Point myEye = pOwner->GetEyeWs().GetPosition();
	const Vector myFaceDir = GetLocalZ(pOwner->GetRotation());
	const Point otherEye = pOther->GetEyeWs().GetPosition();
	const Vector playerFaceDir = GetLocalZ(pOther->GetRotation());

	// my look
	if (m_myLookBlend > 0.0f)
	{
		const Point myLookAtPos = otherEye;
		const Vector myFaceDirXZ = SafeNormalize(VectorFromXzAndY(myFaceDir, kZero), kUnitZAxis);
		const Vector myLookVec = myLookAtPos - myEye;
		const Vector myLookVecXZ = VectorFromXzAndY(myLookVec, kZero);
		const Vector myLookDirXZ = SafeNormalize(myLookVecXZ, kUnitZAxis);
		const float myLookCross = Cross(myFaceDirXZ, myLookDirXZ).Y();
		const float myLookDot = Dot(myFaceDirXZ, myLookDirXZ);
		const float myAngleAbsRad = Acos(myLookDot);
		const float myAngleAbsDeg = RADIANS_TO_DEGREES(myAngleAbsRad);
		const float myAngleSign = Sign(myLookCross);

		m_currentAngle = myAngleAbsDeg;

		if (m_myPrevLookBlend <= 0.0f)
		{
			m_myLookAngleSignHysteresis = myAngleSign;
		}

		const float myMaxLookAngleAbs = kMaxLookAngle;
		const float myFinalAngleSign = myAngleAbsDeg < myMaxLookAngleAbs ? myAngleSign : m_myLookAngleSignHysteresis;
		const float myFinalAngleDeg = myFinalAngleSign * Min(myAngleAbsDeg, myMaxLookAngleAbs);
		const Point myNewLookAtPos = PointFromXzAndY(myEye + Length(myLookVecXZ) * RotateVectorAbout(myFaceDirXZ, kUnitYAxis, DEGREES_TO_RADIANS(myFinalAngleDeg)), myLookAtPos);

		m_lookAtPos = myNewLookAtPos;
		m_lookAtBlend = myCurvedLookBlend;

		if (myAngleAbsDeg < myMaxLookAngleAbs)
		{
			m_myLookAngleSignHysteresis = myAngleSign;
		}
	}
	else if (m_myPrevLookBlend > 0.0f)
	{
		m_lookAtPos = kOrigin;
		m_lookAtBlend = 0.0f;
	}

	if (g_ndAiOptions.m_displayDialogLookDebug)
	{
		PrimServerWrapper ps(pOwner->GetParentSpace());

		ps.DrawString(pOwner->GetTranslation() + Vector(0, 1.7f, 0), StringBuilder<256>("Dialog Look is %s\n", m_lookAtBlend == 0.0f ? StringBuilder<256>("Off - %s", DevKitOnly_StringIdToString(m_noLookReason)).c_str() : "On").c_str(), kColorGreen, 0.5f);
	}

	m_myPrevLookBlend = m_myLookBlend;
	// end: my look
}
