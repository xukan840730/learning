/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/camera/camera-fixed.h"
#include "gamelib/scriptx/h/nd-camera-settings.h"
#include "ndlib/settings/render-settings.h"
#include "ndlib/settings/settings.h"
#include "ndlib/settings/priority.h"
#include "gamelib/camera/scene-camera.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/camera/camera-manager.h"
#include "ndlib/render/dev/render-options.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/process/spawn-info.h"
#include "gamelib/ndphys/rigid-body.h"

PROCESS_REGISTER(CameraControlFixed, CameraControl);
FROM_PROCESS_DEFINE(CameraControlFixed);
CAMERA_REGISTER(kCameraFixed, CameraControlFixed, CameraFixedStartInfo, CameraPriority::Class::kGameplay);
CAMERA_REGISTER_RANKED(kCameraFixedOverride, CameraControlFixed, CameraFixedStartInfo, CameraPriority::Class::kGameplay, CameraRank::Override);

CameraMoveRotate::CameraMoveRotate()
{
	m_maxAngleHoriz = -1.0f;
	m_maxAngleVert = -1.0f;
	m_xSpring.Reset();
	m_ySpring.Reset();
	m_verticalAngleSpring.Reset();
	m_horizontalAngleSpring.Reset();
	x = 0.0f;
	y = 0.0f;
	m_horizontalAngle = 0.0f;
	m_verticalAngle = 0.0f;
	m_isFirstFrame = true;
	m_playerIndex = 0;
}

void CameraMoveRotate::UpdateInput(const Locator& currentLocator, const Vector& playerDirection1, const Vector& playerDirection2, const CameraJoypad& pad, float fov, float& targetHAngle, float& targetVAngle, float scaleMovement)
{
	Vector forward = GetLocalZ(currentLocator.Rot());
	Vector right = GetLocalX(currentLocator.Rot());
	Vector up = GetLocalY(currentLocator.Rot());

	float dot = Dot(playerDirection1, up);
	Vector horizDir1 = SafeNormalize(playerDirection1 - up * dot, kUnitZAxis);
	float horizPlayerAngle1 = RADIANS_TO_DEGREES(Acos(MinMax((float)Dot(horizDir1, forward), -1.0f, 1.0f)));
	if (Dot(playerDirection1, right) > 0.0f)
		horizPlayerAngle1 = -horizPlayerAngle1;

	dot = Dot(playerDirection2, up);
	Vector horizDir2 = SafeNormalize(playerDirection2 - up * dot, kUnitZAxis);
	float horizPlayerAngle2 = RADIANS_TO_DEGREES(Acos(MinMax((float)Dot(horizDir2, forward), -1.0f, 1.0f)));
	if (Dot(playerDirection2, right) > 0.0f)
		horizPlayerAngle2 = -horizPlayerAngle2;

	dot = Dot(playerDirection1, right);
	Vector vertDir1 = SafeNormalize(playerDirection1 - right * dot, kUnitZAxis);
	float vertPlayerAngle1 = RADIANS_TO_DEGREES(Acos(MinMax((float)Dot(vertDir1, forward), -1.0f, 1.0f)));
	if (Dot(playerDirection1, up) < 0.0f)
		vertPlayerAngle1 = -vertPlayerAngle1;

	dot = Dot(playerDirection2, right);
	Vector vertDir2 = SafeNormalize(playerDirection2 - right * dot, kUnitZAxis);
	float vertPlayerAngle2 = RADIANS_TO_DEGREES(Acos(MinMax((float)Dot(vertDir2, forward), -1.0f, 1.0f)));
	if (Dot(playerDirection2, up) < 0.0f)
		vertPlayerAngle2 = -vertPlayerAngle2;

	// deal with the stick
	float targetX = - pad.GetX(m_playerIndex);
	float targetY = pad.GetY(m_playerIndex);

	if (Abs(targetX) > 0.7f && Abs(targetY) < 0.3f)
		targetY = 0.0f;

	if (Abs(targetY) > 0.7f && Abs(targetX) < 0.3f)
		targetX = 0.0f;

	targetX *= g_cameraOptions.m_lookAroundSpeedScale;
	targetY *= g_cameraOptions.m_lookAroundSpeedScale;

	x = m_xSpring.Track(x, targetX, EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetDeltaTimeInSeconds(), 15.0f / scaleMovement);
	y = m_ySpring.Track(y, targetY, EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetDeltaTimeInSeconds(), 15.0f / scaleMovement);

	float xDelta = x * EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->FromUPS(40.0f * scaleMovement);
	float yDelta = y * EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->FromUPS(40.0f * scaleMovement);

	float maxHAngle = Min (- horizPlayerAngle1 + (fov / 3.0f), - horizPlayerAngle2 + (fov / 3.0f));
	float minHAngle = Max (- horizPlayerAngle1 + (- fov / 3.0f), - horizPlayerAngle2 + (- fov / 3.0f));

	float maxVAngle = Min (- vertPlayerAngle1 + (fov / 6.0f), - vertPlayerAngle2 + (fov / 6.0f));
	float minVAngle = Max (- vertPlayerAngle1 + (- fov / 6.0f), - vertPlayerAngle2 + (- fov / 6.0f));

	if (m_maxAngleHoriz > -1.0f)
	{
		maxHAngle = m_maxAngleHoriz;
		minHAngle = -m_maxAngleHoriz;
	}

	if (m_maxAngleVert > -1.0f)
	{
		maxVAngle = m_maxAngleVert;
		minVAngle = -m_maxAngleVert;
	}

	m_horizontalAngle += xDelta;
	m_verticalAngle += yDelta;

	if (Abs(x) < 0.01f && Abs(y) < 0.01f && m_deadStickReset)
	{
		float maxVertAngle = m_verticalAngle > 0.0f ? 5.0f : -5.0f;
		if (Abs(m_verticalAngle) > Abs(maxVertAngle))
			m_verticalAngle = m_verticalAngleSpring.Track(m_verticalAngle, maxVertAngle, EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetDeltaTimeInSeconds(), 5.0f);

		float maxHorizAngle = m_horizontalAngle > 0.0f ? 15.0f : -15.0f;
		if (Abs(m_horizontalAngle) > Abs(maxHorizAngle))
			m_horizontalAngle = m_horizontalAngleSpring.Track(m_horizontalAngle, maxHorizAngle, EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetDeltaTimeInSeconds(), 5.0f);
	}

	m_verticalAngle = m_verticalAngleSpring.Track(m_verticalAngle, MinMax(m_verticalAngle, minVAngle, maxVAngle), EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetDeltaTimeInSeconds(), 25.0f);
	m_horizontalAngle = m_horizontalAngleSpring.Track(m_horizontalAngle, MinMax(m_horizontalAngle, minHAngle, maxHAngle), EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetDeltaTimeInSeconds(), 25.0f);
}

void CameraMoveRotate::GetAngles(const Locator& currentLocator, const Vector& cameraDir, bool playerIsMoving, float& targetHAngle, float& targetVAngle)
{
	targetHAngle = m_horizontalAngle;
	targetVAngle = m_verticalAngle;

	if (playerIsMoving)
	{
		Seek(m_horizontalAngle, 0.0f, LerpScale(0.0f, 30.0f, EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->FromUPS(5.0f), EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->FromUPS(25.0f), Abs(m_horizontalAngle)));
		Seek(m_verticalAngle, 0.0f, LerpScale(0.0f, 30.0f, EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->FromUPS(5.0f), EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->FromUPS(25.0f), Abs(m_verticalAngle)));
	}

	Vector forward = GetLocalZ(currentLocator.Rot());
	Vector up = GetLocalY(currentLocator.Rot());
	Vector right = GetLocalX(currentLocator.Rot());
	float dot = Dot(cameraDir, up);
	Vector horizDir = SafeNormalize(cameraDir - up * dot, kUnitZAxis);
	float newHAngle = RADIANS_TO_DEGREES(Acos(MinMax((float)Dot(horizDir, forward), -1.0f, 1.0f)));
	if (Dot(cameraDir, right) < 0.0f)
		newHAngle = -newHAngle;

	if (Abs(newHAngle) < Abs(m_horizontalAngle))
		targetHAngle = newHAngle;

	Vector vertDir = Rotate(QuatFromAxisAngle(GetLocalY(currentLocator.Rot()), -DEGREES_TO_RADIANS(m_horizontalAngle)), cameraDir);
	float newVAngle = RADIANS_TO_DEGREES(Acos(MinMax((float)Dot(vertDir, forward), -1.0f, 1.0f)));
	if (Dot(cameraDir, up) > 0.0f)
		newVAngle = -newVAngle;
	if (Abs(newVAngle) < Abs(m_verticalAngle))
		targetVAngle = newVAngle;
}

Locator CameraMoveRotate::GetLocator(const CameraJoypad& pad, const Locator& currentLocator, const Point& playerPosition, const Vector& cameraDir, float fov, float scaleLocator, bool wasCut)
{
	fov *= g_cameraOptions.m_lookAroundLimitScale;

	Vector playerDirection = SafeNormalize(playerPosition - currentLocator.Pos(), kUnitZAxis);

	float targetVAngle = m_verticalAngle;
	float targetHAngle = m_horizontalAngle;

	if (!m_isFirstFrame)
	{
		bool playerIsMoving = false;
		if (Length(playerPosition - m_lastPlayerPosition) > 0.01f)
			playerIsMoving = true;

		GetAngles(currentLocator, cameraDir, playerIsMoving, targetHAngle, targetVAngle);
	}

	if (g_cameraOptions.m_moveRotatePointOverride)
	{
		Vector playerOverrideDirection1 = SafeNormalize(g_cameraOptions.m_moveRotatePoint1 - currentLocator.Pos(), kUnitZAxis);
		Vector playerOverrideDirection2 = SafeNormalize(g_cameraOptions.m_moveRotatePoint2 - currentLocator.Pos(), kUnitZAxis);
		UpdateInput(currentLocator, playerOverrideDirection1, playerOverrideDirection2, pad, fov, targetHAngle, targetVAngle, scaleLocator);
	}
	else
	{
		UpdateInput(currentLocator, playerDirection, playerDirection, pad, fov, targetHAngle, targetVAngle, scaleLocator);
	}

	Vector up = GetLocalY(currentLocator.Rot());
	if (g_cameraOptions.m_forceFixedRotateAroundY)
	{
		up = kUnitYAxis;
	}
	Vector direction = Rotate(QuatFromAxisAngle(GetLocalX(currentLocator.Rot()), DEGREES_TO_RADIANS(m_verticalAngle)), GetLocalZ(currentLocator.Rot()));
	direction = Rotate(QuatFromAxisAngle(up, DEGREES_TO_RADIANS(m_horizontalAngle)), direction);

	if (m_isFirstFrame || wasCut)
	{
		m_lastDirection = direction;
		m_finalDirectionSpring.Reset();
	}

	float springConstant = 20.0f;
	float angle = Acos(MinMax((float)Dot(m_lastDirection, direction), -1.0f, 1.0f));
	float newAngle = m_finalDirectionSpring.Track(angle, 0.0f, EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetDeltaTimeInSeconds(), springConstant);

	if (angle > 0.0001f)
	{
		float tt = Abs(newAngle / angle);

		direction = (Sin(newAngle * (1.0f - tt)) / Sin(newAngle)) * direction + (Sin(newAngle * tt) / Sin(newAngle)) * m_lastDirection;
	}

	m_lastDirection = direction;
	Quat desiredRotation = QuatFromLookAt(direction, up);


	Locator desiredLocator(currentLocator);
	desiredLocator.SetRot(desiredRotation);

	m_isFirstFrame = false;
	m_lastPlayerPosition = playerPosition;

	return desiredLocator;
}

Err CameraControlFixed::Init(const ProcessSpawnInfo& processSpawnInfo)
{
	const CameraControlSpawnInfo& spawnInfo = *PunPtr<const CameraControlSpawnInfo*>(&processSpawnInfo);

	m_cameraControl.SetPlayerIndex(spawnInfo.m_playerIndex);
	return CameraControl::Init(spawnInfo);
}

StringId64 CameraControlFixed::GetBaseCameraSettingsId() const
{
	return SID("*default-camera-fixed-settings*");
}



/// --------------------------------------------------------------------------------------------------------------- ///
CameraBlendInfo CameraControlFixed::CameraStart(const CameraStartInfo& baseStartInfo)
{
	CameraBlendInfo blendInfo = ParentClass::CameraStart(baseStartInfo);

	const CameraFixedStartInfo& startInfo = *PunPtr<const CameraFixedStartInfo*>(&baseStartInfo);
	m_dofDistance = -1.0f; // CAM_TODO: support new DOF parameters

	StringId64 cameraSettingsId = GetBaseCameraSettingsId();
	if (startInfo.m_settingsId != INVALID_STRING_ID_64)
		cameraSettingsId = startInfo.m_settingsId;

	DC::CameraFixedSettings cameraSettings;
	const EntitySpawner* pSpawner = ReadSettingsFromDcAndSpawner(*this, cameraSettings,
													cameraSettingsId, startInfo.m_spawnerId);

	m_distortion = startInfo.m_distortion >= 0 ? startInfo.m_distortion : cameraSettings.m_3DDistortion;
	m_zeroPlaneDist = startInfo.m_zeroPlaneDist > 0 ? startInfo.m_zeroPlaneDist : cameraSettings.m_3DZeroPlaneDist;
	m_noRoll = startInfo.m_noRoll;

	const NdGameObject* pParent = pSpawner ? pSpawner->GetParentNdGameObject() : nullptr;

	Locator initialLoc;
	if (startInfo.GetLocator())
	{
		initialLoc = *startInfo.GetLocator();
	}
	else
	{
		if (pSpawner)
		{
			initialLoc = pSpawner->GetWorldSpaceLocator();
		}
		else
		{
			MsgConScriptError("Fixed camera spawner is missing!  Spawner name is %s\n.", DevKitOnly_StringIdToString(startInfo.m_spawnerId));
			initialLoc = GameCameraLocator();
		}
	}

	m_hFocusObj = startInfo.GetFocusObject();
	m_flags.m_needsFocus = startInfo.GetFocusObject().HandleValid();
	m_flags.m_dontKillOnFocusDeath = startInfo.m_dontKillOnFocusDeath;

	m_dofDistance = -1.0f; // CAM_TODO: support new DOF parameters

	m_frame.SetLocator(initialLoc);
	m_frame.SetBinding(Binding(pParent != nullptr ? pParent->GetDefaultBindTarget() : nullptr));

	m_flags.m_suppressJoypadDuringBlend = true;
	m_fov = (startInfo.m_fov != CameraStartInfo::kUseDefaultFov) ? startInfo.m_fov : cameraSettings.m_fov;

	m_disableLookAround = cameraSettings.m_disableLookAround;

	if (!pSpawner)
		m_disableLookAround = true;

	if (startInfo.m_acquirePosition)
	{
		m_frame.SetLocator(GameCameraLocator());
		m_fov = g_mainCameraInfo->GetFov();
		m_distortion = g_renderOptions.m_distortion;
		m_zeroPlaneDist = g_renderOptions.m_zeroPlaneDist;
	}

	return blendInfo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControlFixed::SetNewLocation(Locator &loc, float fov, float zeroPlaneDist, float distortion)
{
	m_frame.SetLocator(loc);
	m_frame.RemoveBinding();
	m_fov = fov;
	if (zeroPlaneDist > 0.f)
		m_zeroPlaneDist = zeroPlaneDist;
	if (distortion >= 0.f)
		m_distortion = distortion;
}
/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControlFixed::SetNewLocation(BoundFrame &loc, float fov, float zeroPlaneDist, float distortion)
{
	m_frame = loc;
	m_fov = fov;
	if (zeroPlaneDist > 0.f)
		m_zeroPlaneDist = zeroPlaneDist;
	if (distortion >= 0.f)
		m_distortion = distortion;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraControlFixed::ReadSettingsFromSpawner(DC::CameraFixedSettings& cameraSettings, const EntitySpawner* pSpawner)
{
	CameraControl::ReadBaseSettingsFromSpawner(cameraSettings, pSpawner);

	const EntityDB * pDb = (pSpawner != nullptr) ? pSpawner->GetEntityDB() : nullptr;
	if (pDb)
	{
		cameraSettings.m_disableLookAround = !pDb->GetData<I32>(SID("enable-look-around"), false);
		m_movementScale = pDb->GetData<float>(SID("movement-scale"), 1.0f);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

CameraLocation CameraControlFixed::StartLocation (const CameraControl* pPrevCamera) const
{
	return CameraLocation(m_frame.GetLocator(), m_fov);
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraControlFixed::Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(offset_bytes, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControlFixed::SetDof(float dofDistance, float nearNear, float nearFar, float farNear, float farFar, float globalIntensity, float nearIntensity)
{
	m_dofDistance = dofDistance; // CAM_TODO: support new DOF parameters
	m_nearNear = nearNear;
	m_nearFar = nearFar;
	m_farNear = farNear;
	m_farFar = farFar;
	m_dofGlobalIntensity = globalIntensity;
	m_nearIntensity = nearIntensity;
}


/// --------------------------------------------------------------------------------------------------------------- ///

CameraLocation CameraControlFixed::UpdateLocation (const CameraInstanceInfo& info)
{
	Locator loc;


	if (m_dofDistance != -1.0f)
	{
		float globalIntensity = m_dofGlobalIntensity >= 0.0f ? m_dofGlobalIntensity : 0.35f;
		float nearIntensity = m_nearIntensity >= 0.0f ? m_nearIntensity : 0.4f;

		// CAM_TODO: support new DOF parameters
		SettingSet(&GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofBlurRadius, 1.0f,  kPlayerModePriority+2, info.m_normalBlend, this);
		SettingSet(&GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofNearStartIntensity, nearIntensity,  kPlayerModePriority+2, info.m_normalBlend, this);
		SettingSet(&GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofNearEndIntensity, 0.0f,  kPlayerModePriority+2, info.m_normalBlend, this);
		SettingSet(&GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofFarStartIntensity, 0.0f,  kPlayerModePriority+2, info.m_normalBlend, this);
		SettingSet(&GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofFarEndIntensity, LerpScale(5.0f, 10.0f, 1.0f, LerpScale(20.0f, 90.0f, 0.5f, 0.0f, m_dofDistance), m_dofDistance),  kPlayerModePriority+2, info.m_normalBlend, this);
		SettingSet(&GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofNearStart, m_nearNear,  kPlayerModePriority+2, info.m_normalBlend, this);
		SettingSet(&GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofNearEnd, Max(m_nearFar, m_nearNear) + 0.01f,  kPlayerModePriority+2, info.m_normalBlend, this);
		SettingSet(&GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofFarStart, m_farNear,  kPlayerModePriority+2, info.m_normalBlend, this);
		SettingSet(&GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofFarEnd, Max(m_farFar, m_farNear) + 0.01f,  kPlayerModePriority+2, info.m_normalBlend, this);
		SettingSet(&GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofGlobalIntensityMultiplier, globalIntensity,  kPlayerModePriority+2, info.m_normalBlend, this);
		SettingSet(&GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofBlurRadius, 1.0f,  kPlayerModePriority+2, info.m_normalBlend, this);
	}


	const NdGameObject* pFocusObj = m_hFocusObj.ToProcess();
	if (!m_disableLookAround && pFocusObj)
		loc = m_cameraControl.GetLocator(info.m_joyPad, m_frame.GetLocator(), pFocusObj->GetSite(SID("LookAtPos")).GetTranslation(), GetLocalZ(info.m_prevLocation.GetLocator().Rot()), m_fov, m_movementScale);
	else
		loc = m_frame.GetLocator();

	loc = m_cameraShake.GetLocator(GetPlayerIndex(), loc);

	if (m_noRoll)
	{
		Vector forward = GetLocalZ(loc.GetRotation());
		Quat rot = QuatFromLookAt(forward, kUnitYAxis);
		loc.SetRotation(rot);
	}


	ALWAYS_ASSERT(IsNormal(loc.GetRotation()));

	return CameraLocation(loc, m_fov, m_distortion, m_zeroPlaneDist);
}




