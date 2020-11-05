/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nd-player-joypad.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/render/gui2/nd-message-manager.h"
#include "gamelib/camera/camera-joypad.h"

#include "ndlib/camera/camera-final.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/io/joypad.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/scriptx/h/joypad-defines.h"
#include "ndlib/settings/fader.h"
#include "ndlib/settings/priority.h"
#include "ndlib/settings/settings.h"
#include "ndlib/scriptx/h/joypad-defines-internal.h"

class NdJoypadGesture;

static const float kDefaultCamCutTimeoutSec = 3.0f;

bool g_enableCameraCutCompensate[kMaxCameraManagers] = { false };// , false, false, false, false, false, false, false, false, false, false, false };
bool g_forceCameraCutCompensate[kMaxCameraManagers] = { false };// , false, false, false, false, false, false, false, false, false, false, false };

NdPlayerJoypad::NdPlayerJoypad()
{
	m_inited = false;
	m_screenIndex = 0;
	m_playerIndex = 0;
	m_joypadId = 0;
	m_lastFrameNumber = 0;
	m_stickMove = kZero;
	m_stickCamera = kZero;

	m_cameraLocator.SetPos(Point(SMath::kZero));
	m_cameraLocator.SetRot(Quat(SMath::kIdentity));

	m_oldStickDir = kZero;
	m_hasOldCameraLoc = false;

	m_innerDead = 0.0f;
	m_outerDead = 1.0f;

	for (int i=0; i<kStickCount; i++)
	{
		m_stickSpeed[i] = 0.0f;
		m_cameraRelStickWS[i] = kZero;
		m_playerRelStickWS[i] = kZero;
		m_screenPlaneDir[i] = kZero;
	}

	m_pitchAngleRecentChange = 0.0f;

#if !FINAL_BUILD
	m_debugDisabled = false;
#endif

	m_freezeIntensity = false;
	m_overrideCameraLocator = MAYBE::kNothing;
	m_overrideAmount = 0.0f;
}

void NdPlayerJoypad::SmoothRawInput()
{
	Joypad& joypad = EngineComponents::GetNdFrameState()->m_joypad[m_joypadId];

	m_stickMove = Vec2(SMath::kZero);
	m_stickCamera = Vec2(SMath::kZero);

	bool cheating = joypad.IsCommandActive(SID("debug-cheat"), true) && !DebugFlyDisable();

	if (cheating || (!joypad.IsCommandDisabled(SID("move")) && !FALSE_IN_FINAL_BUILD(m_debugDisabled)))
	{
		const DC::JoypadCommand* pCommand = joypad.GetCommand(SID("move"));
		if (pCommand)
		{
			joypad.GetDeadZoneAdjustedStick(&m_stickMove, m_innerDead * m_innerDeadZoneMultiplier, m_outerDead, pCommand->m_stick.m_type);
		}
	}

	if (cheating || !joypad.IsCommandDisabled(SID("camera")))
	{
		const DC::JoypadCommand* pCommand = joypad.GetCommand(SID("camera"));
		if (pCommand)
		{
			joypad.GetDeadZoneAdjustedStick(&m_stickCamera, m_innerDead, m_outerDead, pCommand->m_stick.m_type);
		}
	}
}

void NdPlayerJoypad::SetFreezeIntensity(bool freeze)
{
	if (!m_freezeIntensity && freeze)
	{
		m_freezeIntensity = freeze;
		m_frozenIntensity = 1.0f;
		m_frozenMoveStick = m_stickMove;
		if (Length(m_stickMove) < 0.2f)
		{
			m_frozenMoveStick = Vec2(0, -1);
		}

	}
	else if (m_freezeIntensity && !freeze)
	{
		m_freezeIntensity = freeze;
	}
}

void NdPlayerJoypad::SetFreezeCamera(bool freeze)
{
	if (freeze)
		SetOverrideCamera(m_cameraLocator);
	else
		UnsetOverrideCamera();
}

void NdPlayerJoypad::SetOverrideCamera(const Locator &cameraLocator)
{
	m_overrideCameraLocator = cameraLocator;
}

void NdPlayerJoypad::UnsetOverrideCamera()
{
	m_overrideCameraLocator = MAYBE::kNothing;
	if (m_freezeIntensity)
		m_frozenMoveStick = Vec2(0, -1);
}

void NdPlayerJoypad::EnableCameraCutCompensate(U32 playerIndex)
{
	ALWAYS_ASSERT(playerIndex < kMaxCameraManagers);
	StringId64 sid = (playerIndex == 0) ? SID("enable-camera-cut-compensate-1") : SID("enable-camera-cut-compensate-2");

	SettingSetDefault(&g_enableCameraCutCompensate[playerIndex], false);
	SettingSetPers(sid, &g_enableCameraCutCompensate[playerIndex], true, kPlayerModePriority);

	g_enableCameraCutCompensate[playerIndex] = true;
}

void NdPlayerJoypad::ForceCameraCutCompensate(U32 playerIndex)
{
	ALWAYS_ASSERT(playerIndex < kMaxCameraManagers);
	StringId64 sid = (playerIndex == 0) ? SID("force-camera-cut-compensate-1") : SID("force-camera-cut-compensate-2");

	SettingSetDefault(&g_forceCameraCutCompensate[playerIndex], false);
	SettingSetPers(sid, &g_forceCameraCutCompensate[playerIndex], true, kPlayerModePriority);

	g_forceCameraCutCompensate[playerIndex] = true;
}

bool NdPlayerJoypad::AreSticksSwapped() const
{
	const JoypadSchemeCollection& schemes = JoypadSchemeCollection::Get();
	const JoypadInputScheme& scheme = schemes.GetCurrentInputScheme();
	const bool result = scheme.AreSticksSwapped();
	return result;
}

bool NdPlayerJoypad::AreAimSticksSwapped() const
{
	const JoypadSchemeCollection& schemes = JoypadSchemeCollection::Get();
	const JoypadInputScheme& scheme = schemes.GetCurrentInputScheme();
	const bool result = scheme.AreAimSticksSwapped();
	return result;
}

bool NdPlayerJoypad::CameraTimerExpired()
{
	return (GetProcessClock()->GetCurTime() - m_lastCameraGoodTime > Seconds(kDefaultCamCutTimeoutSec));
}

bool NdPlayerJoypad::CameraMinimalChange()
{
	Vector currentDir = GetLocalZ(g_mainCameraInfo[GetPlayerIndex()].GetRotation());
	Vector oldDir = GetLocalZ(m_cameraLocator.GetRotation());

	float angle = RADIANS_TO_DEGREES(Acos(MinMax((float)Dot(currentDir, oldDir), -1.0f, 1.0f)));

	if (GetProcessDeltaTime() < 0.0001f )
		return true;

	return (angle / GetProcessDeltaTime() < 500.0f);
}

void NdPlayerJoypad::UpdateCamDirections()
{
	U32 playerIndex = GetPlayerIndex();
	Vector oldDir = GetLocalZ(m_cameraLocator.GetRotation());
	Vector currentDir = GetLocalZ(g_mainCameraInfo[m_playerIndex].GetRotation());

	Vec2 newStickDir = m_stickMove;
	Normalize(&newStickDir);

	if ((!g_enableCameraCutCompensate[m_playerIndex] && !g_forceCameraCutCompensate[m_playerIndex])
	||  !m_hasOldCameraLoc
	||  (!g_forceCameraCutCompensate[m_playerIndex] && CameraMinimalChange())
	||  CameraTimerExpired()
	||  Length(m_stickMove) < 0.5f
	||  Dot(newStickDir, m_oldStickDir) < 0.8f)
	{
		m_cameraLocator = g_mainCameraInfo[m_playerIndex].GetLocator();
		m_hasOldCameraLoc = true;
		m_lastCameraGoodTime = GetProcessClock()->GetCurTime();
		m_oldStickDir = newStickDir;
	}
}

void NdPlayerJoypad::ComputeWSInput()
{
	Vec2 smoothedSticks[kStickCount] = {m_stickMove, m_stickCamera};

	for (int i = 0; i < kStickCount; i++)
	{
		float stickLen = Length(smoothedSticks[i]);
		if (i == kStickMove && m_freezeIntensity)
		{
			const float stickOverrideAmount = LerpScaleClamp(0.3f, 0.5f, 1.0f, 0.0f, stickLen);
			smoothedSticks[i] = Lerp(smoothedSticks[i], m_frozenMoveStick, stickOverrideAmount);

			stickLen = m_frozenIntensity;
		}
		
		if (stickLen > 0.0f)
		{
			Vec2 stick = smoothedSticks[i];
			stick /= stickLen;

			stickLen = MinMax(stickLen, 0.0f, 1.0f);

			if (m_overrideCameraLocator.Valid())
			{
				const Locator actualCam = g_mainCameraInfo[m_playerIndex].GetLocator();
				const Locator frozenCam = GetCameraLocator();
				const Vector actualZ = GetLocalZ(actualCam);
				const Vector frozenZ = GetLocalZ(frozenCam);

				if (Dot(actualZ, frozenZ) < -0.5f)
				{
					stick.x *= -1;
				}
			}

			float angleRot = Atan2(-stick.y, -stick.x);
			angleRot -= PI / 2.0f; // because we want to start from up, not from right, since forward is into the screen

			if (i == kStickMove && m_freezeIntensity)
			{
				const float stickOverrideAmount = LerpScaleClamp(0.3f, 0.5f, 1.0f, 0.0f, stickLen);
				smoothedSticks[i] = Lerp(smoothedSticks[i], Vec2(0, -1), stickOverrideAmount);
			}
			
			// Compute camera-relative world-space stick.
			{
				// overriden cam dir
				Vector camDirForward = GetLocalZ(GetCameraLocator().GetRotation());

				// actual camera dir
				Vector actualCamDirForward = GetLocalZ(m_cameraLocator.GetRotation());
				
				const float actualCurrentRot = Atan2(actualCamDirForward.Z(), actualCamDirForward.X());
				const float currentRot = Atan2(camDirForward.Z(), camDirForward.X());

				// unmodified ws angle
				// wsAngle = currentRot + angleRot
				// delta from actual camera dir:
				// delta = wsAngle - actualCurrentRot
				// therefore:
				// wsAngle = actualCurrentRot + delta
				// can modify angle sensitivity by applying func to delta:
				// wsAngle = actualCurrentRot + f(delta)

				float origDelta = angleRot + currentRot - actualCurrentRot;
				if (origDelta > PI)
					origDelta -= 2.0f * PI;
				if (origDelta < -PI)
					origDelta += 2.0f * PI;

				float delta = origDelta;

				float wsRot = actualCurrentRot + delta;

				Vector wsDir(Cos(wsRot), 0.0f, Sin(wsRot));

				m_stickSpeed[i] = stickLen;
				m_cameraRelStickWS[i] = wsDir * m_stickSpeed[i];

				Vector camDirLeft = GetLocalX(GetCameraLocator().GetRotation());
				m_screenPlaneDir[i] = camDirLeft*-stick.X() + Vector(0,-stick.Y(),0);
				m_screenPlaneDir[i] = Normalize(m_screenPlaneDir[i]);
			}

			// Compute player-relative world-space stick.
			if (NdGameObject *pPlayer = EngineComponents::GetNdGameInfo()->GetGlobalPlayerGameObject(m_playerIndex))
			{
				Vector playerDirForward = GetLocalZ(pPlayer->GetRotation());
				float currentRot = Atan2(playerDirForward.Z(), playerDirForward.X());
				currentRot += angleRot;

				Vector wsDir(Cos(currentRot), 0.0f, Sin(currentRot));
				m_playerRelStickWS[i] = wsDir * stickLen;
			}
			else
			{
				m_playerRelStickWS[i] = kZero;
			}
		}
		else
		{
			m_screenPlaneDir[i] = kZero;
			m_stickSpeed[i] = 0.0f;
			m_cameraRelStickWS[i] = m_playerRelStickWS[i] = kZero;
		}
	}
}

///-----------------------------------------------------------------------------------------------///
Maybe<Vec2> NdPlayerJoypad::GetGyroFlickX() const
{
	const I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	if (m_gyroFlickDetector[0].m_flickDetectFN == currFN)
		return m_gyroFlickDetector[0].m_flickDir;
	else
		return MAYBE::kNothing;
}

///-----------------------------------------------------------------------------------------------///
Maybe<Vec2> NdPlayerJoypad::GetGyroFlickY() const
{
	const I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	if (m_gyroFlickDetector[1].m_flickDetectFN == currFN)
		return m_gyroFlickDetector[1].m_flickDir;
	else
		return MAYBE::kNothing;
}

///-----------------------------------------------------------------------------------------------///
static bool LocalExtremaFound(float currGyroVelRad, float lastGyroVelRad0, float lastGyroVelRad1)
{
	bool cond1 = lastGyroVelRad0 > lastGyroVelRad1 && lastGyroVelRad0 > currGyroVelRad;  // local maxima
	bool cond2 = lastGyroVelRad0 < lastGyroVelRad1 && lastGyroVelRad0 < currGyroVelRad;  // local minima
	return cond1 || cond2;
}

///-----------------------------------------------------------------------------------------------///
void NdPlayerJoypad::UpdateGyroFlick()
{
	const float dt = GetProcessDeltaTime();
	const TimeFrame currTime = GetProcessClock()->GetCurTime();

	const Joypad* pJoypad = EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId);

	const Vec3 rawGyroVelRad = Vec3(pJoypad->GetAngularX(), pJoypad->GetAngularY(), pJoypad->GetAngularZ());
	const Vec2 gyroCamSpeedRad = CameraJoypad::GyroRawToCamSpeed(rawGyroVelRad);

	m_gyroCurrCamSpdRad = gyroCamSpeedRad;

	const Vec2 angAcc = (m_gyroCurrCamSpdRad - m_gyroLastCamSpdRad[0]) / dt;
	m_gyroCamAcc = angAcc;

	const float kFlickSpeedThreshold[2] = { 1.5f, 1.5f };

	const float kFlickStopThreshold[2] = { 0.4f, 0.4f };
	const float kFlickStopAccThreshold[2] = { 10.f, 10.f };

	// the following algorithm is based on the fact gyro flick is like an big impulse followed by a small impulse in the opposite direction.
	// the solution is to detect local velocity extrema which is greater than threshold.
	// feel free to try other cool ideas!

	for (int kk = 0; kk < 2; kk++)
	{
		GyroFlickDetector& detector = m_gyroFlickDetector[kk];

		switch (detector.m_state)
		{
		case GyroFlickState::kStable:
		{
			bool flickDetected = false;
			if (Abs(gyroCamSpeedRad[kk]) > kFlickSpeedThreshold[kk])
			{
				if (LocalExtremaFound(gyroCamSpeedRad[kk], m_gyroLastCamSpdRad[0][kk], m_gyroLastCamSpdRad[1][kk]))
				{
					if (Sign(angAcc[kk]) != Sign(gyroCamSpeedRad[kk]))
						flickDetected = true;
				}
			}

			if (flickDetected)
			{
				detector.m_state = GyroFlickState::kMove;				
				detector.m_flickDir = Normalize(gyroCamSpeedRad);
				detector.m_flickDetectFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
				detector.m_flickDetectTime = currTime;

				detector.m_lastFlickExtrema = gyroCamSpeedRad[kk];
			}
		}
		break;

		case GyroFlickState::kMove:
		{
			const bool stopped = Abs(gyroCamSpeedRad[kk]) < kFlickStopThreshold[kk] && Abs(angAcc[kk]) < kFlickStopAccThreshold[kk];
			if (stopped)
			{
				detector.m_state = GyroFlickState::kStable;
				detector.m_lastFlickExtrema = 0.f;
			}
			else
			{
				bool localExtremaFound = false;
				bool flickDetected = false;
				if (Abs(gyroCamSpeedRad[kk]) > kFlickSpeedThreshold[kk])
				{
					if (LocalExtremaFound(gyroCamSpeedRad[kk], m_gyroLastCamSpdRad[0][kk], m_gyroLastCamSpdRad[1][kk]))
					{
						if (Sign(angAcc[kk]) != Sign(gyroCamSpeedRad[kk]))
						{
							localExtremaFound = true;

							const TimeDelta timeSinceLastFlick = currTime - detector.m_flickDetectTime;
							//MsgCon("since-last-flick: %d %f\n", kk, timeSinceLastFlick.ToSeconds());

							// the closer to the last flick impulse, the stronger this impulse needs to be registered as flick.
							float thresholdScale = 1.f;
							if (timeSinceLastFlick > TimeDelta::Seconds(0.3f))
								thresholdScale = LerpScaleClamp(0.3f, 0.6f, 1.f, 0.7f, timeSinceLastFlick.ToSeconds());

							bool rejected = false;
							if (Abs(gyroCamSpeedRad[kk]) < Abs(detector.m_lastFlickExtrema) * thresholdScale &&
								Sign(gyroCamSpeedRad[kk]) != Sign(detector.m_lastFlickExtrema))
							{
								rejected = true;
							}

							if (!rejected)
								flickDetected = true;
						}
					}
				}

				if (localExtremaFound)
				{
					//MsgCon("local-extrema-found : %d \n", kk);
				}

				if (flickDetected)
				{
					//MsgCon("flickDetected2 : %d \n", kk);

					detector.m_flickDir = Normalize(gyroCamSpeedRad);
					detector.m_flickDetectFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
					detector.m_flickDetectTime = currTime;

					detector.m_lastFlickExtrema = gyroCamSpeedRad[kk];
				}
			}
		}
		break;
		}
	}

	m_gyroLastCamSpdRad[1] = m_gyroLastCamSpdRad[0];
	m_gyroLastCamSpdRad[0] = m_gyroCurrCamSpdRad;
}

void NdPlayerJoypad::Init(U32 id, U32 screenIndex, U32 playerIndex, float innerDead, float outerDead, bool resetJoypadData)
{
	m_inited = true;
	m_screenIndex = screenIndex;
	m_playerIndex = playerIndex;
	m_joypadId = id;
	m_innerDead = innerDead;
	m_innerDeadZoneMultiplier = 1.0f;
	m_outerDead = outerDead;
	m_lastFrameNumber = 0;
	if (resetJoypadData)
	{
		EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->Reset(true);
	}
}

Locator NdPlayerJoypad::GetStickCameraLocator()
{
	return m_cameraLocator;
}

Maybe<Locator> NdPlayerJoypad::GetOverrideCameraLocator()
{
	return m_overrideCameraLocator;
}

float NdPlayerJoypad::GetPitchAngle()
{
	PROFILE_AUTO(Joypad);
	return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->GetPitchAngle();
}

Quat NdPlayerJoypad::GetOrientation()
{
	PROFILE_AUTO(Joypad);
	return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->m_orientation;
}

Locator NdPlayerJoypad::GetCameraLocator()
{
	if (m_overrideCameraLocator.Valid())
	{
		Locator overrideLoc = m_overrideCameraLocator.Get();
		return Lerp(m_cameraLocator, overrideLoc, m_overrideAmount);
	}
		
	return m_cameraLocator;
}

float NdPlayerJoypad::GetPitchAngleRecentChange()
{
	return m_pitchAngleRecentChange;
}

Vector NdPlayerJoypad::GetStickWS(Stick stick) const
{
	return m_cameraRelStickWS[stick];
}

Vector NdPlayerJoypad::GetStickWsReadOnly(Stick stick) const
{
	return m_cameraRelStickWS[stick];
}

Vector NdPlayerJoypad::GetPlayerRelativeStickWS(Stick stick)
{
	return m_playerRelStickWS[stick];
}

Vec2 NdPlayerJoypad::GetStick(Stick stick) const
{
	Vec2 smoothedSticks[2] = {m_stickMove, m_stickCamera};
	return smoothedSticks[stick];
}

float NdPlayerJoypad::GetStickSpeed(Stick stick) const
{
	return m_stickSpeed[stick];
}

const DC::JoypadCommand* NdPlayerJoypad::GetCommand(StringId64 id) const
{
	PROFILE_AUTO(Joypad);
	return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->GetCommand(id);
}

bool NdPlayerJoypad::IsCommandActive(StringId64 id, bool ignoreDisable) const
{
	PROFILE_AUTO(Joypad);
	if (GetFadeToBlack() == 1.f)
		return false;

	return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->IsCommandActive(id, ignoreDisable);
}

bool NdPlayerJoypad::IsCommandDisabled(StringId64 id) const
{
	PROFILE_AUTO(Joypad);
	return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->IsCommandDisabled(id);
}

bool NdPlayerJoypad::IsHoldCommandHeld(StringId64 id) const
{
	PROFILE_AUTO(Joypad);
	return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->IsHoldCommandHeld(id);
}

TimeFrame NdPlayerJoypad::GetLastTimeCommandPressed(StringId64 id) const
{
	PROFILE_AUTO(Joypad);
	return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->GetLastTimeCommandPressed(id);
}

Maybe<float> NdPlayerJoypad::IsHoldButtonCommand(StringId64 id) const
{
	PROFILE_AUTO(Joypad);
	return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->IsHoldButtonCommand(id);
}

bool NdPlayerJoypad::IsCommandAnalog(StringId64 id) const
{
	PROFILE_AUTO(Joypad);
	if (GetFadeToBlack() == 1.f)
		return false;
	return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->IsCommandAnalog(id);
}

F32 NdPlayerJoypad::GetCommandAnalog(StringId64 id) const
{
	PROFILE_AUTO(Joypad);
	return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->GetCommandAnalog(id);
}


void NdPlayerJoypad::ForceCommand(StringId64 id)
{
	PROFILE_AUTO(Joypad);
	return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->ForceCommand(id);
}

void NdPlayerJoypad::ClearCommand(StringId64 id)
{
	PROFILE_AUTO(Joypad);
	EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->ClearCommand(id);
}


void NdPlayerJoypad::ClearSingleCommand(StringId64 id)
{
	PROFILE_AUTO(Joypad);
	EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->ClearSingleCommand(id);
}


void NdPlayerJoypad::UnclearCommand(StringId64 id)
{
	PROFILE_AUTO(Joypad); 
	EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->UnclearCommand(id);
}

///-----------------------------------------------------------------------------------------------///
void NdPlayerJoypad::Update ()
{
	I64 currentFrameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
	bool messageActive = EngineComponents::GetNdMessageManager()->GetNumPendingMessages() > 0;
	Joypad* pJoyPad = EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId);
	const Clock* pClock = EngineComponents::GetNdFrameState()->GetClock(pJoyPad->m_clockId);
	bool unpaused = (!pClock->IsPaused() || g_ndConfig.m_pDMenuMgr->IsProgPausedStep());
	if ((unpaused || messageActive) && m_lastFrameNumber != currentFrameNumber)
	{
		PROFILE_AUTO(Joypad);
		{
			PROFILE(Joypad, UpdateJoypadScheme);
			UpdateJoypadScheme();
		}

		m_lastFrameNumber = currentFrameNumber;
		{
			PROFILE(Joypad, SmoothRawInput);
			SmoothRawInput();
		}
		{
			PROFILE(Joypad, UpdateCamDirections);
			UpdateCamDirections();
			UpdateGyroFlick();
		}
		{
			PROFILE(Joypad, ComputeWSInput);
			ComputeWSInput();
		}

		m_pitchAngleRecentChange = EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId)->m_pitchAngleRunningDelta;

		{
			PROFILE(Joypad, PostUpdate);
			PostUpdate();
		}
	}
}

NdJoypadGesture * NdPlayerJoypad::GetGesture(StringId64 sid) const
{
	Joypad& joypad = EngineComponents::GetNdFrameState()->m_joypad[m_joypadId];
	return joypad.GetGesture(sid);
}

bool DebugFlyDisable()
{
	return !EngineComponents::GetNdGameInfo()->m_cheatEnable || EngineComponents::GetNdGameInfo()->m_debugFlyDisable;
}

#if !FINAL_BUILD
	void NdPlayerJoypad::DebugDisableStick(bool disable)
	{
		m_debugDisabled = disable;
	}

	bool NdPlayerJoypad::IsStickDebugDisabled() const
	{
		return m_debugDisabled;
	}
#endif
