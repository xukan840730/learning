/*
* Copyright (c) 2019 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/camera/camera-strafe-yaw.h"
#include "gamelib/camera/camera-strafe-base.h"
#include "gamelib/camera/camera-animated.h"
#include "ndlib/camera/camera-option.h"

//#include "game/camera/camera-utility.h"

#include "ndlib/nd-frame-state.h"

///-----------------------------------------------------------------------------------------///
Angle SpringTrackUnitXZVector(DampedSpringTracker& spring, Vector_arg currentVec, Vector_arg desiredVec, float dt, const DC::SpringSettings* pSpringSettings, bool forceCWDir, float springConstOverride, bool debugDraw)
{
	Angle angDes = AngleFromUnitXZVec(desiredVec);
	Angle angCur = AngleFromUnitXZVec(currentVec);

	float diff = angCur.AngleDiff(angDes);
	if (forceCWDir)
	{
		//Make sure we rotate clockwise always even if its longer
		if (Cross(currentVec, desiredVec).Y() > 0.0f)
		{
			diff = 360.0f + diff;
		}
	}

	//float springConst = Sqr(pSpringSettings->m_springConst);
	float springConst = pSpringSettings->m_springConst;
	if (springConstOverride > 0.0f)
		springConst = springConstOverride;

	float newDiff = spring.Track(diff, 0.0f, dt, Sqr(springConst), pSpringSettings->m_dampingRatio);
	newDiff = MinMax(newDiff, diff - FromUPS(720.0f, dt), diff + FromUPS(720.0f, dt));

	Angle angNew = angDes + newDiff;

#ifdef ORIGINAL_T1_STRAFE
	if (debugDraw)
	{
		static char text[128];
		sprintf(text, "angle des = %.1f deg, cur = %.1f deg [diff = %.3f] new = %.1f deg",
			angDes.ToDegrees(), angCur.ToDegrees(), newDiff, angNew.ToDegrees());
		DrawCameraString(text, kColorWhite, 0.0f);
	}
#endif

	return angNew;
}

//////////////////////////////////////////
// CameraStrafeYawCtrl
/////////////////////////////////////////

static const float kUseDefaultSpringConst = -1.0f;

CameraStrafeYawCtrl::CameraStrafeYawCtrl()
{
	m_yawAngle[kCurrent].Reset();
	m_yawAngle[kRequested].Reset();
	m_yawAngle[kMarioRequested].Reset();

	m_yawSpring.Reset();
	m_lastSetDesiredDirTime = TimeFrameNegInfinity();
	m_desiredDirRequested = false;
	m_desiredDirForceCW = false;
	m_desiredDirForceCWMarioCam = false;
	m_desiredDirSpringConstMarioCam = -1.0f;
	m_desiredDirSpringConst = -1.0f;
	m_currentHorizontalSpeed = 0.0f;
	m_lastYawSpeed = 0.0f;
	m_horizontalSpeedMax = 10000.0f;
	m_activeInput = false;

	m_marioDirRequested = false;
	m_lastSetMarioDirTime = TimeFrameNegInfinity();
	m_yawSpringConstOverride = 1.0f;
	m_yawSpringConstOverrideFrame = 0;
	m_timeScaleMultiplier = 1.0f;
	m_timeScaleMultiplierValidFrame = 0;
}


void CameraStrafeYawCtrl::SetInitialYawAngle(Angle yaw)
{
	m_yawAngle[kCurrent].SetBoth(yaw);
	m_yawAngle[kRequested].SetBoth(yaw);
	m_yawAngle[kMarioRequested].SetBoth(yaw);

	m_yawSpring.Reset();
}

///--------------------------------------------------------------------------------------------///
void CameraStrafeYawCtrl::SetInitialDirPs(Vector_arg dir)
{
	const Vector dirXzNorm = SafeNormalize(VectorXz(dir), Vector(kUnitZAxis));
	const Angle angle = AngleFromXZVec(dirXzNorm);

	SetInitialYawAngle(angle);
	//MsgConPauseable("YawController::SetInitialDirection() Setting direction to: [%.3f, %.3f, %.3f]\n", float(dir.X()), float(dir.Y()), float(dir.Z()));
}

///--------------------------------------------------------------------------------------------///
void CameraStrafeYawCtrl::SetDesiredDirXZPs(Vector_arg unitDirXZ, float springConst, bool forceCWDir, F32 completionDot)
{
	const Vector dirXzNorm = SafeNormalize(VectorXz(unitDirXZ), Vector(kUnitZAxis));
	const Angle angle = AngleFromXZVec(dirXzNorm);

	m_yawAngle[kRequested].SetBoth(angle);
	m_desiredDirRequested = true;
	m_completionDot = completionDot;
	m_lastSetDesiredDirTime = EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetCurTime();
	m_desiredDirSpringConst = springConst;
	m_desiredDirForceCW = forceCWDir;
}

///--------------------------------------------------------------------------------------------///
void CameraStrafeYawCtrl::SetDesiredMarioDirXZ(Vector_arg unitDirXZ, float springConst, bool forceCwDir)
{
	const Vector dirXzNorm = SafeNormalize(VectorXz(unitDirXZ), Vector(kUnitZAxis));
	const Angle angle = AngleFromXZVec(dirXzNorm);

	m_yawAngle[kMarioRequested].SetBoth(angle);
	m_marioDirRequested = true;
	m_lastSetMarioDirTime = EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetCurTime();
	m_desiredDirSpringConstMarioCam = springConst;
	m_desiredDirForceCWMarioCam = forceCwDir;
}

///--------------------------------------------------------------------------------------------///
void CameraStrafeYawCtrl::ClearMarioDir()
{
	if (m_marioDirRequested)
	{
		m_marioDirRequested = false;
		m_yawSpring.Reset();
		m_desiredDirSpringConstMarioCam = kUseDefaultSpringConst;
		m_desiredDirForceCWMarioCam = false;
	}
}

///--------------------------------------------------------------------------------------------///
bool CameraStrafeYawCtrl::HasRequestedMarioDir()
{
	return m_marioDirRequested;
}

///--------------------------------------------------------------------------------------///
void CameraStrafeYawCtrl::SetRequestedAnglePs(Angle v)
{
	m_yawAngle[kRequested].SetBoth(v);
}

///--------------------------------------------------------------------------------------///
void CameraStrafeYawCtrl::SetMaintainAimPointAngle(Angle v)
{
	// only set main angle, don't touch angle for speed calculation.
	m_yawAngle[kRequested].m_angle[kAngleIndexMain] = v;
}

//void CameraStrafeYawCtrl::Rotate(Quat_arg q)
//{
//	{
//		const Vector newDir = SafeNormalize(VectorXz(::Rotate(q, VectorFromAngleXZ(m_yawAngle[kCurrent]))), kUnitZAxis);
//		m_yawAngle[kCurrent] = AngleFromUnitXZVec(newDir);
//	}
//
//	{
//		const Vector newDir = SafeNormalize(VectorXz(::Rotate(q, VectorFromAngleXZ(m_yawAngle[kRequested]))), kUnitZAxis);
//		m_yawAngle[kRequested] = AngleFromUnitXZVec(newDir);
//	}
//
//	{
//		const Vector newDir = SafeNormalize(VectorXz(::Rotate(q, VectorFromAngleXZ(m_yawAngle[kMarioRequested]))), kUnitZAxis);
//		m_yawAngle[kMarioRequested] = AngleFromUnitXZVec(newDir);
//	}
//
//	//MsgConPauseable("Current Yaw: %.2f %.2f %.2f\n", float(m_yawDirection[kCurrent].X()), float(m_yawDirection[kCurrent].Y()), float(m_yawDirection[kCurrent].Z()));
//}

void CameraStrafeYawCtrl::BlendHorizontalSettings(const DC::CameraStrafeBaseSettings* pMySettings,
												  const DC::CameraStrafeBaseSettings* pPrevSettings, 
												  const DC::CameraStrafeBaseSettings* pNextSettings, 
												  const float normalBlend, 
												  const float nextNormalBlend,
												  float* outHorizontalSpeed, 
												  float* outHorizontalSpeedMax, 
												  float* outHorizontalSpeedRampTime, 
												  float* outRotationSpeedMultiplier)
{
	*outHorizontalSpeed = pMySettings->m_horizontalSpeed;
	*outHorizontalSpeedMax = pMySettings->m_horizontalSpeedMax;
	*outHorizontalSpeedRampTime = pMySettings->m_horizontalSpeedRampTime;
	*outRotationSpeedMultiplier = pMySettings->m_rotationSpeedMultiplier;

	GAMEPLAY_ASSERT(normalBlend >= 0.0f);
	GAMEPLAY_ASSERT(normalBlend <= 1.0f);
	GAMEPLAY_ASSERT(nextNormalBlend >= 0.0f);
	GAMEPLAY_ASSERT(nextNormalBlend <= 1.0f);

	if (pNextSettings != nullptr)
	{
		*outHorizontalSpeed = Lerp(pMySettings->m_horizontalSpeed, pNextSettings->m_horizontalSpeed, nextNormalBlend);
		*outHorizontalSpeedMax = Lerp(pMySettings->m_horizontalSpeedMax, pNextSettings->m_horizontalSpeedMax, nextNormalBlend);
		*outHorizontalSpeedRampTime = Lerp(pMySettings->m_horizontalSpeedRampTime, pNextSettings->m_horizontalSpeedRampTime, nextNormalBlend);
		*outRotationSpeedMultiplier = Lerp(pMySettings->m_rotationSpeedMultiplier, pNextSettings->m_rotationSpeedMultiplier, nextNormalBlend);
	}
	else if (pPrevSettings != nullptr)
	{
		*outHorizontalSpeed = Lerp(pPrevSettings->m_horizontalSpeed, pMySettings->m_horizontalSpeed, normalBlend);
		*outHorizontalSpeedMax = Lerp(pPrevSettings->m_horizontalSpeedMax, pMySettings->m_horizontalSpeedMax, normalBlend);
		*outHorizontalSpeedRampTime = Lerp(pPrevSettings->m_horizontalSpeedRampTime, pMySettings->m_horizontalSpeedRampTime, normalBlend);
		*outRotationSpeedMultiplier = Lerp(pPrevSettings->m_rotationSpeedMultiplier, pMySettings->m_rotationSpeedMultiplier, normalBlend);
	}
}

void CameraStrafeYawCtrl::UpdateRequestYawFromJoypad(float x, 
													 float slowdownX, 
													 float targetLockSpeedX, 
													 bool stickNotFull, 
													 const float joypadThreshHold, 
													 const CameraInstanceInfo& info, 
													 const DC::CameraStrafeBaseSettings* pSettings, 
													 const float normalBlend, 
													 const bool isFadeInByDist,
													const float targetSpeed)
{
	m_activeInput = true;
	float absX = Abs(x);
	Clock* pCameraClock = EngineComponents::GetNdFrameState()->GetClock(kCameraClock);

	float horizontalSpeedMin;
	float horizontalSpeedMax;
	float horizontalSpeedRampTime;
	float rotationSpeedMultiplier;

	const DC::CameraStrafeBaseSettings* pNextStrafeSettings = nullptr;
	const DC::CameraStrafeBaseSettings* pPrevStrafeSettings = nullptr;
	float nextNormalBlend = 0.f;

	// HACK! HACK! HACK! To fix the dist camera orbiting at different speed, I am blending horizontal speed.
	// It only works with 2 dist camera stack, need revisit the design!
	if (info.m_nextControl != nullptr)
	{
		const CameraControlStrafeBase* pNextStrafeCamera = CameraControlStrafeBase::FromProcess(info.m_nextControl);
		if (pNextStrafeCamera != nullptr)
		{
			pNextStrafeSettings = pNextStrafeCamera->GetSettings();
			nextNormalBlend = pNextStrafeCamera->GetNormalBlend();
		}
		else if (info.m_nextControl->IsKindOf(g_type_CameraControlAnimated) && info.m_nextControl->GetBlend() == 1.f) // look for dist-camera-top
		{
			int playerIndex = info.m_nextControl->GetPlayerIndex();
			const CameraControl* pNextCamera2 = CameraManager::GetGlobal(playerIndex).GetNextCamera(info.m_nextControl);
			const CameraControlStrafeBase* pNextStrafeCamera2 = CameraControlStrafeBase::FromProcess(pNextCamera2);
			if (pNextStrafeCamera2 != nullptr)
			{
				pNextStrafeSettings = pNextStrafeCamera2->GetSettings();
				nextNormalBlend = pNextStrafeCamera2->GetNormalBlend();
			}
		}
	}

	if (info.m_prevControl != nullptr)
	{
		const CameraControlStrafeBase* pPrevStrafeCamera = CameraControlStrafeBase::FromProcess(info.m_prevControl);
		if (pPrevStrafeCamera != nullptr)
		{
			pPrevStrafeSettings = pPrevStrafeCamera->GetSettings();
		}
		else if (info.m_prevControl->IsKindOf(g_type_CameraControlAnimated) && info.m_prevControl->GetBlend() == 1.f) // look for dist-camera-base
		{
			int playerIndex = info.m_prevControl->GetPlayerIndex();
			const CameraControl* pPrevCamera2 = CameraManager::GetGlobal(playerIndex).GetPreviousCamera(info.m_prevControl);
			const CameraControlStrafeBase* pPrevStrafeCamera2 = CameraControlStrafeBase::FromProcess(pPrevCamera2);
			if (pPrevStrafeCamera2 != nullptr)
			{
				pPrevStrafeSettings = pPrevStrafeCamera2->GetSettings();
			}
		}
	}

	BlendHorizontalSettings(pSettings, 
							pPrevStrafeSettings, 
							pNextStrafeSettings, 
							normalBlend, 
							nextNormalBlend, 
							&horizontalSpeedMin, 
							&horizontalSpeedMax, 
							&horizontalSpeedRampTime, 
							&rotationSpeedMultiplier);

	// current horizontal speed should never be lower than horizontalSpeed
	m_currentHorizontalSpeed = Max(m_currentHorizontalSpeed, horizontalSpeedMin);

	if (stickNotFull && absX < 0.5f)
	{
		m_currentHorizontalSpeed = horizontalSpeedMin;
	}

	if (!m_desiredDirRequested && !m_marioDirRequested)
	{
		if (absX > joypadThreshHold || Abs(targetLockSpeedX) > 0.0f)
		{
			float rampSpeed = (horizontalSpeedMax - horizontalSpeedMin) / horizontalSpeedRampTime;
			Seek(m_currentHorizontalSpeed, horizontalSpeedMax, pCameraClock->FromUPS(rampSpeed));

			F32 speedCap = LerpScale(1.0f, 2.5f, m_horizontalSpeedMax, 80.0f, targetSpeed);
			if (horizontalSpeedMax > speedCap)
			{
				horizontalSpeedMax = speedCap;
			}
			m_currentHorizontalSpeed = Min(m_currentHorizontalSpeed, horizontalSpeedMax);

			const float assistedHorizontalSpeed = m_currentHorizontalSpeed * slowdownX;

			const float joypadSpeed = -1.0f * x * pCameraClock->FromUPS(assistedHorizontalSpeed);
			const float lockSpeed = pCameraClock->FromUPS(targetLockSpeedX);
			Angle deltaYaw(joypadSpeed * rotationSpeedMultiplier + lockSpeed);

			if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_displaySpeed))
			{
				const float dt = pCameraClock->GetDeltaTimeInSeconds();
				if (dt != 0.0f)
				{
					MsgConPauseable("  Camera Strafe Yaw Speed Degrees Per Second: %.3f\n", deltaYaw.ToDegrees() / dt);
				}
			}

			//MsgConPauseable("deltaYaw: %f\n", deltaYaw.ToDegrees());
			m_yawAngle[kRequested].Add(deltaYaw);
		}
		else
		{
			m_activeInput = false;
		}
	}

	//MsgConPauseable("YawController::UpdateRequestYawFromJoypad() kRequested direction: [%.3f, %.3f, %.3f]\n", float(m_yawDirection[kRequested].X()), float(m_yawDirection[kRequested].Y()), float(m_yawDirection[kRequested].Z()));
}

//float CameraStrafeYawCtrl::PredictRequestedYawDelta(const CameraJoypad& joyPad, 
//													const float sensitivityScale, 
//													const DC::CameraStrafeBaseSettings* pSettings, 
//													I32 playerIndex) const
//{
//	const float padx = joyPad.GetX(playerIndex);
//	const float x = padx * sensitivityScale;
//
//	const Clock* pCameraClock = EngineComponents::GetNdFrameState()->GetClock(kCameraClock);
//
//	float currentHorizontalSpeed = m_currentHorizontalSpeed;
//
//	float rampSpeed = (pSettings->m_horizontalSpeedMax - pSettings->m_horizontalSpeed) / pSettings->m_horizontalSpeedRampTime;
//	Seek(currentHorizontalSpeed, pSettings->m_horizontalSpeedMax, pCameraClock->FromUPS(rampSpeed));
//	currentHorizontalSpeed = Min(currentHorizontalSpeed, m_horizontalSpeedMax);
//
//	float deltaYaw = -1.0f * x * pCameraClock->FromUPS(currentHorizontalSpeed) * pSettings->m_rotationSpeedMultiplier;
//	return deltaYaw;
//}

void CameraStrafeYawCtrl::ClampRequestedYawDir(const Locator& targetToWorld, 
											   const DC::CameraStrafeBaseSettings* pSettings, 
											   const NdGameObject* pParentGo)
{
	// get into target space (i.e. player-align space), because the min/max angles are relative to target space
	// [T2-TODO]
	bool horizontalRangeInParentSpace = pSettings->m_horizontalRangeInParentSpace;
	float horizontalRangeMinDegrees = pSettings->m_horizontalRangeMinDegrees;
	float horizontalRangeMaxDegrees = pSettings->m_horizontalRangeMaxDegrees;

	if (g_cameraOptions.m_enableStrafeScriptConstraints)
	{
		horizontalRangeInParentSpace = false;
		horizontalRangeMinDegrees = -g_cameraOptions.m_maxHorizontalAngle * 0.5f;
		horizontalRangeMaxDegrees = g_cameraOptions.m_maxHorizontalAngle * 0.5f;
	}

	// Don't do anything if full range is allowed
	if (horizontalRangeMinDegrees == -180.0f && horizontalRangeMaxDegrees == 180.0f)
		return;

	Locator targetToParent = targetToWorld;
	if (pParentGo)
	{
		const Locator& parentToWorld = pParentGo->GetLocator();
		targetToParent = parentToWorld.UntransformLocator(targetToWorld);
	}

	for (int kk = 0; kk < kAngleIndexCount; kk++)
	{
		Vector yawDirTargetSpace;

		if (horizontalRangeInParentSpace)
			yawDirTargetSpace = -VectorFromAngleXZ(m_yawAngle[kRequested].m_angle[kk]);
		else if (g_cameraOptions.m_enableStrafeScriptConstraints)
		{
			Locator camSpace(Point(0, 0, 0), g_cameraOptions.m_referenceRotation);
			camSpace = FlattenLocator(camSpace);
			yawDirTargetSpace = camSpace.UntransformVector(::Rotate(m_parentSpaceRot, -VectorFromAngleXZ(m_yawAngle[kRequested].m_angle[kk])));

					//g_prim.Draw(DebugCoordAxes(Locator(targetToWorld.GetTranslation(), camSpace.GetRotation()), 1.0f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
					//g_prim.Draw(DebugLine(targetToWorld.GetTranslation(), -VectorFromAngleXZ(m_yawAngle[kRequested].m_angle[kk]), kColorYellow), kPrimDuration1FramePauseable);
		}
		else
			yawDirTargetSpace = targetToParent.UntransformVector(-VectorFromAngleXZ(m_yawAngle[kRequested].m_angle[kk]));

		float yawDegrees = RADIANS_TO_DEGREES(Atan2(yawDirTargetSpace.X(), yawDirTargetSpace.Z()));

		// 	g_prim.Draw(DebugCoordAxes(targetToWorld, 1.0f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
		// 	g_prim.Draw(DebugArrow(targetToWorld.GetTranslation(), m_yawDirection[kRequested], kColorCyan, 0.3f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
		// 	char yawText[64];
		// 	snprintf(yawText, sizeof(yawText), "%.1f", yawDegrees);
		// 	g_prim.Draw(DebugString(targetToWorld.GetTranslation() + VECTOR_LC(0.0f, 0.3f, 0.0f), yawText, kColorCyan, g_msgOptions.m_conScale), kPrimDuration1FramePauseable);

		ASSERT(horizontalRangeMinDegrees <= horizontalRangeMaxDegrees);
		const float rangeMidPoint = 0.5f*(horizontalRangeMinDegrees + horizontalRangeMaxDegrees);
		const float rangeMin = horizontalRangeMinDegrees - rangeMidPoint;
		const float rangeMax = horizontalRangeMaxDegrees - rangeMidPoint;

		const float yawClamped = MinMax(AngleMod(yawDegrees - rangeMidPoint), rangeMin, rangeMax) + rangeMidPoint;

		// back to world/parent space
		if (horizontalRangeInParentSpace)
			m_yawAngle[kRequested].m_angle[kk] = AngleFromUnitXZVec(Normalize(-VectorFromAngleXZ(Angle(yawClamped))));
		else if (g_cameraOptions.m_enableStrafeScriptConstraints)
		{
			Locator camSpace(Point(0, 0, 0), g_cameraOptions.m_referenceRotation);
			camSpace = FlattenLocator(camSpace);
			m_yawAngle[kRequested].m_angle[kk] = AngleFromUnitXZVec(-Unrotate(m_parentSpaceRot, Normalize(camSpace.TransformVector(VectorFromAngleXZ(Angle(yawClamped))))));

			//		g_prim.Draw(DebugArrow(targetToWorld.GetTranslation(), m_yawDirection[kRequested], kColorRed, 0.3f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
		}
		else
			m_yawAngle[kRequested].m_angle[kk] = AngleFromUnitXZVec(Normalize(-targetToParent.TransformVector(VectorFromAngleXZ(Angle(yawClamped)))));
		//ASSERT(IsUnitLength(m_yawDirection[kRequested]));

		//g_prim.Draw(DebugArrow(targetToWorld.GetTranslation(), m_yawDirection[kRequested], kColorOrange, 0.3f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
		//snprintf(yawText, sizeof(yawText), "%.1f", yaw.ToDegrees());
		//g_prim.Draw(DebugString(targetToWorld.GetTranslation() + VECTOR_LC(0.0f, 0.4f, 0.0f), yawText, kColorOrange, g_msgOptions.m_conScale), kPrimDuration1FramePauseable);
	}
}

DC::SpringSettings CameraStrafeYawCtrl::BlendSpringSettings(const DC::SpringSettings& mySettings, const DC::SpringSettings* prevSettings, const DC::SpringSettings* nextSettings, const float normalBlend, const float nextNormalBlend)
{
	DC::SpringSettings result = mySettings;
	if (nextSettings != nullptr)
	{
		result.m_springConst = Lerp(mySettings.m_springConst, nextSettings->m_springConst, nextNormalBlend);
		result.m_dampingRatio = Lerp(mySettings.m_dampingRatio, nextSettings->m_dampingRatio, nextNormalBlend);
	}
	else if (prevSettings != nullptr)
	{
		result.m_springConst = Lerp(prevSettings->m_springConst, mySettings.m_springConst, normalBlend);
		result.m_dampingRatio = Lerp(prevSettings->m_dampingRatio, mySettings.m_dampingRatio, normalBlend);
	}

	return result;
}

void CameraStrafeYawCtrl::UpdateCurrentYaw(float dt, 
										   const CameraInstanceInfo& info, 
										   const DC::CameraStrafeBaseSettings* pSettings, 
										   const float normalBlend, 
										   const bool isFadeInByDist, 
										   F32 distanceTravelled, 
										   bool inQuickTurn)
{
	const DC::SpringSettings mySettings = m_activeInput ? pSettings->m_yawSpringSettings.m_activeSettings : pSettings->m_yawSpringSettings.m_restingSettings;
	const DC::SpringSettings* pNextSettings = nullptr;
	const DC::SpringSettings* pPrevSettings = nullptr;
	float nextNormalBlend = 0.f;

	// HACK! HACK! HACK! To fix the dist camera orbiting at different speed, I am blending horizontal speed.
	// It only works with 2 dist camera stack, need revisit the design!
	if (info.m_nextControl != nullptr && (info.m_nextControl->IsFadeInByTravelDist() || info.m_nextControl->IsFadeInByDistToObj()))
	{
		const CameraControlStrafeBase* pNextStrafeCamera = CameraControlStrafeBase::FromProcess(info.m_nextControl);
		if (pNextStrafeCamera != nullptr)
		{
			const DC::CameraStrafeBaseSettings& nextStrafeSettings = *pNextStrafeCamera->GetSettings();
			pNextSettings = m_activeInput ? &nextStrafeSettings.m_yawSpringSettings.m_activeSettings : &nextStrafeSettings.m_yawSpringSettings.m_restingSettings;
			nextNormalBlend = pNextStrafeCamera->GetNormalBlend();
		}
		else if (info.m_nextControl->IsKindOf(g_type_CameraControlAnimated) && info.m_nextControl->GetBlend() == 1.f) // look for dist-camera-top
		{
			int playerIndex = info.m_nextControl->GetPlayerIndex();
			const CameraControl* pNextCamera2 = CameraManager::GetGlobal(playerIndex).GetNextCamera(info.m_nextControl);
			const CameraControlStrafeBase* pNextStrafeCamera2 = CameraControlStrafeBase::FromProcess(pNextCamera2);
			if (pNextStrafeCamera2 != nullptr)
			{
				const DC::CameraStrafeBaseSettings& nextStrafeSettings = *pNextStrafeCamera2->GetSettings();
				pNextSettings = m_activeInput ? &nextStrafeSettings.m_yawSpringSettings.m_activeSettings : &nextStrafeSettings.m_yawSpringSettings.m_restingSettings;
				nextNormalBlend = pNextStrafeCamera2->GetNormalBlend();
			}
		}
	}

	if (info.m_prevControl != nullptr && isFadeInByDist)
	{
		const CameraControlStrafeBase* pPrevStrafeCamera = CameraControlStrafeBase::FromProcess(info.m_prevControl);
		if (pPrevStrafeCamera != nullptr)
		{
			const DC::CameraStrafeBaseSettings& prevStrafeSettings = *pPrevStrafeCamera->GetSettings();
			pPrevSettings = m_activeInput ? &prevStrafeSettings.m_yawSpringSettings.m_activeSettings : &prevStrafeSettings.m_yawSpringSettings.m_restingSettings;
		}
		else if (info.m_prevControl->IsKindOf(g_type_CameraControlAnimated) && info.m_prevControl->GetBlend() == 1.f) // look for dist-camera-base
		{
			int playerIndex = info.m_prevControl->GetPlayerIndex();
			const CameraControl* pPrevCamera2 = CameraManager::GetGlobal(playerIndex).GetPreviousCamera(info.m_prevControl);
			const CameraControlStrafeBase* pPrevStrafeCamera2 = CameraControlStrafeBase::FromProcess(pPrevCamera2);
			if (pPrevStrafeCamera2 != nullptr)
			{
				const DC::CameraStrafeBaseSettings& prevStrafeSettings = *pPrevStrafeCamera2->GetSettings();
				pPrevSettings = m_activeInput ? &prevStrafeSettings.m_yawSpringSettings.m_activeSettings : &prevStrafeSettings.m_yawSpringSettings.m_restingSettings;
			}
		}
	}

	DC::SpringSettings settings = BlendSpringSettings(mySettings, pPrevSettings, pNextSettings, normalBlend, nextNormalBlend);

	if (m_desiredDirSpringConst != kUseDefaultSpringConst)
	{
		settings.m_springConst = m_desiredDirSpringConst;
		settings.m_dampingRatio = 1.0f;
	}

	// Spring the current yaw toward the requested/desired yaw.
	ASSERT(settings.m_springConst >= 0.0f);

	if (m_timeScaleMultiplierValidFrame == EngineComponents::GetNdFrameState()->m_gameFrameNumber)
	{
		dt *= m_timeScaleMultiplier;
	}

	const Angle oldYawForSpeedCalc = m_yawAngle[kCurrent].m_angle[kAngleIndexForCalcSpeed];

	// Shouldn't be checking this here, as auto-follow cameras won't work
	if (m_marioDirRequested /*&& g_cameraOptions.m_useHorizMarioCamera*/)
	{
		F32 dtMovement = distanceTravelled;
		if (m_desiredDirSpringConstMarioCam != kUseDefaultSpringConst)
		{
			settings.m_springConst = m_desiredDirSpringConstMarioCam;
		}
		else
		{
			settings.m_springConst = pSettings->m_marioCameraXzSpring;
		}

		if (pSettings->m_autoFollow)
		{
			dtMovement = pSettings->m_autoFollowBlendByTime ? dt : dtMovement;
			settings.m_springConst = pSettings->m_autoFollowXzSpring;
		}

		float springConstOverride = -1.0f;
		if (EngineComponents::GetNdFrameState()->m_gameFrameNumber == m_yawSpringConstOverrideFrame)
			springConstOverride = m_yawSpringConstOverride;

		for (int kk = 0; kk < kAngleIndexCount; kk++)
		{
			const Vector currDir = VectorFromAngleXZ(m_yawAngle[kCurrent].m_angle[kk]);
			const Vector requestedDir = VectorFromAngleXZ(m_yawAngle[kMarioRequested].m_angle[kk]);
			const Angle yaw = SpringTrackUnitXZVector(m_yawSpring.m_spring[kk], currDir, requestedDir, inQuickTurn ? dt : dtMovement, &settings, m_desiredDirForceCWMarioCam, springConstOverride);
			m_yawAngle[kRequested].m_angle[kk] = m_yawAngle[kCurrent].m_angle[kk] = yaw;
		}
	}
	else
	{
		bool springConstOverrideValid = EngineComponents::GetNdFrameState()->m_gameFrameNumber == m_yawSpringConstOverrideFrame;

		for (int kk = 0; kk < kAngleIndexCount; kk++)
		{
			if (settings.m_springConst > 0.0f || springConstOverrideValid)
			{
				float springConstOverride = springConstOverrideValid ? m_yawSpringConstOverride : -1.0f;
				const Vector currDir = VectorFromAngleXZ(m_yawAngle[kCurrent].m_angle[kk]);
				const Vector requestedDir = VectorFromAngleXZ(m_yawAngle[kRequested].m_angle[kk]);
				const Angle yaw = SpringTrackUnitXZVector(m_yawSpring.m_spring[kk], currDir, requestedDir, dt, &settings, m_desiredDirForceCW, springConstOverride);
				m_yawAngle[kCurrent].m_angle[kk] = yaw;
			}
			else
			{
				Angle prevAngle = m_yawAngle[kCurrent].m_angle[kk];
				m_yawAngle[kCurrent].m_angle[kk] = m_yawAngle[kRequested].m_angle[kk];
			}
		}

		if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_debugVehicleModeYawSpeed))
		{
			GraphDisplay::Params params = GraphDisplay::Params()
				.WithGraphType(GraphDisplay::kGraphTypeBar)
				.NoMaxText()
				.NoAvgText()
				.NoAvgLine()
				.NoMaxLine()
				.WithZeroLine();


			params.m_useMinMax = true;
			params.m_min = -360.0f;
			params.m_max = 360.0f;

			QUICK_PLOT(yawSpeed, "yawSpeed",
				619, 100, 267 * 2, 150,
				60,
				params,
				m_lastYawSpeed);

			MsgConPauseable("Last Yaw Speed: %.1f\n", m_lastYawSpeed);
		}

		const Vector currentDir = VectorFromAngleXZ(m_yawAngle[kCurrent].m_angle[kAngleIndexMain]);
		const Vector requestDir = VectorFromAngleXZ(m_yawAngle[kRequested].m_angle[kAngleIndexMain]);
		const float yawDot = Dot(currentDir, requestDir);
		if (yawDot > m_completionDot)
		{
			// Achieved desired direction -- reset the request.
			m_desiredDirSpringConst = kUseDefaultSpringConst;
			m_desiredDirRequested = false;
			m_desiredDirForceCW = false;
		}
	}

	// only use index 1 (without maintain aim-point interfere) to calculate last yaw speed.
	m_lastYawSpeed = dt <= 0.0f ? 0.0f : ((m_yawAngle[kCurrent].m_angle[kAngleIndexForCalcSpeed] - oldYawForSpeedCalc).ToDegrees() / dt);

	//MsgConPauseable("YawController::UpdateCurrentYaw() kCurrent direction: [%.3f, %.3f, %.3f]\n", float(m_yawDirection[kCurrent].X()), float(m_yawDirection[kCurrent].Y()), float(m_yawDirection[kCurrent].Z()));
}

bool CameraStrafeYawCtrl::HasRequestedDirectionXZ() const
{
	return m_desiredDirRequested;
}

void CameraStrafeYawCtrl::ClearRequestedDirectionXZ(bool clearVector /*= false */)
{
	m_desiredDirSpringConst = kUseDefaultSpringConst;
	m_desiredDirRequested = false;
	m_desiredDirForceCW = false;

	if (clearVector)
	{
		m_yawAngle[kRequested] = m_yawAngle[kCurrent];
		m_yawSpring.Reset();
	}
}

static float GetSpringInitialValue(const float desiredSpeed, const float springConst)
{
	SpringTracker<float> testSpring;
	testSpring.m_speed = desiredSpeed;
	float dt = 1.0 / 30.0f;
	float desiredDelta = desiredSpeed * dt;

	float kdt = springConst * dt;
	float ekt = Exp(-kdt);//1.0f / (1.0f + kdt + 0.48f * kdt * kdt + 0.235f * kdt * kdt * kdt);

	//float initialX = (desiredDelta/ekt - desiredSpeed*dt)/(springConst*dt-1);
	float initialX = (desiredSpeed*dt*(1 - ekt)) / (ekt*(springConst*dt + 1) - 1);
	float testX = testSpring.Track(initialX, 0.0f, dt, springConst);
	float actualSpeed = (testX - initialX) / dt;
	ASSERT(Abs(desiredSpeed - actualSpeed) < 0.01f);

	return initialX;
}

void CameraStrafeYawCtrl::SetInitialSpeed(float speed, float springConst)
{
	m_currentHorizontalSpeed = Abs(speed);
	m_yawSpring.SetSpeed(speed);

	float yawDiff = GetSpringInitialValue(speed, springConst);
	Angle yawOld = m_yawAngle[kCurrent].m_angle[kAngleIndexMain];
	Angle yawNew = yawOld - yawDiff;
	m_yawAngle[kMarioRequested].SetBoth(yawNew);
	m_yawAngle[kRequested].SetBoth(yawNew);

	//MsgCon("Init Request vs current diff: %f\n", (AngleFromUnitXZVec(m_yawDirection[kRequested]) - AngleFromUnitXZVec(m_yawDirection[kCurrent])).ToDegrees());
}

F32 CameraStrafeYawCtrl::GetSpringSpeed() const
{
	return m_yawSpring.m_spring[kAngleIndexMain].m_speed;
}

void CameraStrafeYawCtrl::ApplySpringConstOverride(float springConst)
{
	m_yawSpringConstOverrideFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
	m_yawSpringConstOverride = springConst;
}

void CameraStrafeYawCtrl::ApplyTimeScaleMultiplier(float timeScaleMultiplier)
{
	m_timeScaleMultiplierValidFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
	m_timeScaleMultiplier = timeScaleMultiplier;
}

///-------------------------------------------------------------------------------------------///
void CameraStrafeYawCtrl::InitParentSpace(const NdGameObject* pParentGo)
{
	m_parentSpaceRot = pParentGo ? pParentGo->GetRotation() : kIdentity;
	m_parentSpaceRotSpring.Reset();
	m_hPrevParentObj = pParentGo;
}

///-------------------------------------------------------------------------------------------///
void CameraStrafeYawCtrl::UpdateParentSpace(const NdGameObject* pParentGo, const DC::CameraStrafeBaseSettings* pSettings, float dt)
{
	Quat oldParentSpaceRot = m_parentSpaceRot;
	Quat targetParentSpaceRot(kIdentity);

	if (pParentGo != nullptr)
	{
		targetParentSpaceRot = pParentGo->GetRotation();
	}

	//I ran into an issue where getting on the car would cause the camera to pop to a new position due to entering a new parent space
	//so here we adjust the yaw to make sure it is the same world space direction after the parent space changes
	if (m_hPrevParentObj != NdGameObjectHandle(pParentGo))
	{
		m_parentSpaceRot = targetParentSpaceRot;
		AdjustYawForNewParentSpace(oldParentSpaceRot, m_parentSpaceRot);
	}
	else
	{
		const float springConst = pSettings != nullptr ? pSettings->m_parentSpaceYawSpringConstant : 50.f;
		m_parentSpaceRot = m_parentSpaceRotSpring.Track(m_parentSpaceRot, targetParentSpaceRot, dt, springConst);
	}

	m_hPrevParentObj = pParentGo;
}
///-------------------------------------------------------------------------------------------///
void CameraStrafeYawCtrl::AdjustYawForTargetRotation(Quat_arg prevTargetRot, Quat_arg newTargetRot)
{
	AdjustYawForNewParentSpace(prevTargetRot, newTargetRot);
}

///-------------------------------------------------------------------------------------------///
void CameraStrafeYawCtrl::AdjustYawForNewParentSpace(Quat_arg oldParentSpace, Quat_arg newParentSpace)
{
	for (int i = 0; i < kCount; ++i)
	{
		m_yawAngle[i].AdjustForNewSpace(oldParentSpace, newParentSpace);
	}
}

///-------------------------------------------------------------------------------------------///
Vector CameraStrafeYawCtrl::YawDirToWorldSpace(Vector_arg cameraDirXZPs) const
{
	return Rotate(m_parentSpaceRot, cameraDirXZPs);
}

///-------------------------------------------------------------------------------------------///
Vector CameraStrafeYawCtrl::YawDirFromWorldSpace(Vector_arg cameraDirXZWs) const
{
	return Unrotate(m_parentSpaceRot, cameraDirXZWs);
}

bool IsUnitLength(Vector_arg v)
{
	return (Abs(LengthSqr(v) - SCALAR_LC(1.0f)) < SCALAR_LC(0.001f)*SCALAR_LC(0.001f));
}