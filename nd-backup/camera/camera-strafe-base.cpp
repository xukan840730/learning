/*
* Copyright (c) 2019 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/camera/camera-strafe-base.h"
#include "gamelib/camera/camera-manager.h"
#include "gamelib/camera/camera-node.h"
//#include "game/photo-mode/photo-mode-manager.h"

#include "corelib/math/bisection.h"

#include "ndlib/settings/settings.h"
#include "ndlib/camera/camera-option.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/render/dev/render-options.h"
#include "ndlib/settings/settings.h"
#include "ndlib/settings/priority.h"
#include "ndlib/util/point-curve.h"

#include "gamelib/level/level-mgr.h"
#include "gamelib/gameplay/nd-player-joypad.h"

PROCESS_REGISTER(CameraControlStrafeBase, CameraControl);
CAMERA_REGISTER(kCameraStrafeBase, CameraControlStrafeBase, CameraStrafeStartInfo, CameraPriority::Class::kPlayer);
FROM_PROCESS_DEFINE(CameraControlStrafeBase);

StrafeCameraDebugOptions g_strafeCamDbgOptions;

bool g_strafeCamUpdateDuringIgcBlend = false;
bool g_useMovementOffsetCamera = true;
bool g_enableRotatedSafePos = false;

///-------------------------------------------------------------------------------------------///
/// CameraControlStrafeBase: the minimal strafe-camera can just work.
///-------------------------------------------------------------------------------------------///
CameraBlendInfo CameraControlStrafeBase::CameraStart(const CameraStartInfo& baseStartInfo)
{
	ParentClass::CameraStart(baseStartInfo);

	const CameraStrafeStartInfo& startInfo = *PunPtr<const CameraStrafeStartInfo*>(&baseStartInfo);

	// initialize variables
	Initialize(startInfo);

	// initialize camera settings.
	InitCameraSettings(startInfo);

	// calculate initial look-at (yaw and arc)
	CalcInitArcAndYaw(startInfo);

	// ------------------------------------------------------------------------------------------ //
	// don't return from this function before this line... things won't be initialized properly!
	// ------------------------------------------------------------------------------------------ //

	CameraBlendInfo blendInfo = CalcBlendInfo(startInfo);

	OnCameraStart(startInfo);

	return blendInfo;
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_verticalArc.Relocate(offset_bytes, lowerBound, upperBound);
	ParentClass::Relocate(offset_bytes, lowerBound, upperBound);
}

///-------------------------------------------------------------------------------------------///
bool CameraControlStrafeBase::IsEquivalentTo(const CameraStartInfo& baseStartInfo) const
{
	const CameraStrafeStartInfo& startInfo = *PunPtr<const CameraStrafeStartInfo*>(&baseStartInfo);
	GAMEPLAY_ASSERT(startInfo.m_signature == SID("CameraStrafeStartInfo"));

	if (!ParentClass::IsEquivalentTo(startInfo))
		return false;

	if (startInfo.m_forceNotEquivalent)
		return false;

	const NdGameObject* pFocusGo = GetFocusObject().ToProcess();

	GAMEPLAY_ASSERT(startInfo.m_settingsId != INVALID_STRING_ID_64);
	StringId64 baseSettingsId = startInfo.m_settingsId;

	bool includeDistRemap = true;
	{
		const CameraControl* pPrevCamera = CameraManager::GetGlobal(GetPlayerIndex()).GetCurrentCamera();
		if (pPrevCamera && pPrevCamera->IsFadeInByDistToObj())
			includeDistRemap = false; // dist-remap is for that camera, not me
	}
	StringId64 desiredSettingsId = RemapCameraSettingsId(baseSettingsId, GetPlayerIndex(), includeDistRemap).m_remapSettingsId;

	if (startInfo.m_spawnerId != INVALID_STRING_ID_64)
	{
		if (const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(startInfo.m_spawnerId))
		{
			desiredSettingsId = pSpawner->GetData<StringId64>(SID("camera-settings"), desiredSettingsId);
		}
	}

	if (m_dcSettingsId != desiredSettingsId)
		return false;

	// DELETE THIS? since we support changing camera-parent in one camera, should we delete this?
	{
		const NdGameObject* pParentGo = GetCameraParent(pFocusGo);
		const NdGameObject* pOtherFocusGo = startInfo.GetFocusObject().ToProcess();
		const NdGameObject* pOtherParentGo = GetCameraParent(pOtherFocusGo);

		if (pParentGo != pOtherParentGo)
		{
			return false;
		}
	}

	return true; // passed all tests
}

///-------------------------------------------------------------------------------------------///
static float GetCameraSensitivityScaleBase()
{
	// Times 2 because the follow-sensitivity scale centers at 0.5
	const F32 *pCameraSensitivity = nullptr;

	if (g_ndConfig.m_pNetInfo->IsNetActive())
	{
		pCameraSensitivity = ScriptManager::LookupInModule<F32>(SID("*mp-base-follow-camera-sensitivity*"), SID("camera"));
	}
	else
	{
		pCameraSensitivity = ScriptManager::LookupInModule<F32>(SID("*sp-base-follow-camera-sensitivity*"), SID("camera"));
	}

	const F32 camSens = pCameraSensitivity ? *pCameraSensitivity : 2.0f;
	return camSens;
}

///-------------------------------------------------------------------------------------------///
float CameraControlStrafeBase::GetSensitivityScaleX() const
{
	const F32 camSens = GetCameraSensitivityScaleBase();
	const F32 followSensitivity = g_cameraOptions.m_followSensitivityX * g_cameraOptions.m_cameraSensitivityScaleX;
	//if (m_photoMode)
	//{
	//	followSensitivity = Clamp(followSensitivity, 0.0f, 0.5f);
	//}
	const float tt = m_disableControlsBlendIn ? 1.0f : LerpScaleClamp(0.1, 0.5f, 0.0f, 1.0f, EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetTimePassed(m_startTimeCameraClock).ToSeconds());
	return (camSens * followSensitivity) * tt;
}

///-------------------------------------------------------------------------------------------///
float CameraControlStrafeBase::GetSensitivityScaleY() const
{
	const F32 camSens = GetCameraSensitivityScaleBase();
	const F32 followSensitivity = g_cameraOptions.m_followSensitivityY * g_cameraOptions.m_cameraSensitivityScaleY;
	const float tt = m_disableControlsBlendIn ? 1.0f : LerpScaleClamp(0.1, 0.5f, 0.0f, 1.0f, EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetTimePassed(m_startTimeCameraClock).ToSeconds());
	return (camSens * followSensitivity) * tt;
}

static float kT1JoypadThresh = 0.01f;

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::ReadJoypad(const CameraJoypad& joyPad, const CameraInstanceInfo& info)
{
	const Clock& cameraClock = *EngineComponents::GetNdFrameState()->GetClock(kCameraClock);

	NdGameObject* pPlayerObj = EngineComponents::GetNdGameInfo()->GetGlobalPlayerGameObject(GetPlayerIndex());
	const Character* pPlayerChar = Character::FromProcess(pPlayerObj);
	if (!pPlayerChar)
		return;

	const TimeFrame curTime = cameraClock.GetCurTime();

	if (LengthSqr(pPlayerChar->GetNdJoypad()->GetStickWS(NdPlayerJoypad::kStickMove)) >= SCALAR_LC(0.5f))
		m_lastTargetMoveTime = curTime;

	float x = joyPad.GetX(GetPlayerIndex());
	float y = joyPad.GetY(GetPlayerIndex());

	if (g_cameraOptions.m_forceCameraStickX || g_cameraOptions.m_forceCameraStickY)
	{
		x = g_cameraOptions.m_forceCameraStickX;
		y = g_cameraOptions.m_forceCameraStickY;
	}

	if (g_cameraOptions.m_forceNoCameraInput)
	{
		x = y = 0.0f;
	}

	float absX = Abs(x);
	bool stickNotFull = ((x*x + y * y) <= 0.9f*0.9f);

	const DC::CameraStrafeBaseSettings* pSettings = GetSettings();

	UpdatePitch(pSettings, y);

	{
		const float sensitivityScaleX = GetSensitivityScaleX();
		m_yawControl.UpdateRequestYawFromJoypad(x*sensitivityScaleX, m_slowdownScale.x, 0.f, stickNotFull, kT1JoypadThresh, info, pSettings, GetNormalBlend(), ParentClass::IsFadeInByTravelDist() || ParentClass::IsFadeInByDistToObj(), pPlayerObj?(F32)LengthXz(pPlayerObj->GetVelocityWs()):0.0f);
		m_yawControl.ClampRequestedYawDir(pPlayerChar->GetLookAtAlignLocator(), pSettings, GetCameraParent(pPlayerChar));
	}

	// Update Motion Blur
	{
		if (x != 0.0f || y != 0.0f)
		{
			m_lastStickTouchTime = curTime;

			if (x < 0.0f)
				m_lastStickDirTime[kStickDirNegX] = curTime;
			else if (x > 0.0f)
				m_lastStickDirTime[kStickDirPosX] = curTime;

			if (y < 0.0f)
				m_lastStickDirTime[kStickDirNegY] = curTime;
			else if (y > 0.0f)
				m_lastStickDirTime[kStickDirPosY] = curTime;

			if (Abs(x) > pSettings->m_stickActiveMinDist)
				m_lastStickAxisTime[kStickAxisYaw] = curTime;

			if (Abs(y) > pSettings->m_stickActiveMinDist)
				m_lastStickAxisTime[kStickAxisPitch] = curTime;

			if (curTime - m_lastStickNoTouchTime > Seconds(pSettings->m_motionBlurRampDelay) && pSettings->m_motionBlurRampTime > 0.0f)
			{
				Seek(m_motionBlurIntensity, 1.0f, cameraClock.FromUPS(1.0f / pSettings->m_motionBlurRampTime));
			}
		}
		else
		{
			m_motionBlurIntensity = 0.0f;
			m_lastStickNoTouchTime = curTime;
		}

		// RENDER_SETTING_CLEANUP_TODO Old obsolete render setting, we can remove all of the code that drives this
		//SettingSet(&g_renderSettings[GetScreenIndex()].m_post.m_mbIntensity, m_motionBlurIntensity, kPlayerModePriority, 1.0f, this);
	}
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::UpdatePitch(const DC::CameraStrafeBaseSettings* pSettings, float y)
{
	const Clock& cameraClock = *EngineComponents::GetNdFrameState()->GetClock(kCameraClock);

	const float absY = Abs(y);
	const DC::SpringSettings* pCurveSpringSettings = (Abs(absY) > kT1JoypadThresh) 
		? &pSettings->m_curveSpringSettings.m_activeSettings 
		: &pSettings->m_curveSpringSettings.m_restingSettings;

	m_stopPullInHitCeilingFloor = false;
	if (pSettings->m_stopPullInHitCeilingFloor)
	{
		if ((y > 0.f && m_collision.m_verticalSafeDir == StrafeCameraCollision::kDownDirSafe) ||
			(y < 0.f && m_collision.m_verticalSafeDir == StrafeCameraCollision::kUpDirSafe))
		{
			if (m_pullInPercent < pSettings->m_stopPullInPercentageHitCeilingFloor)
			{
				m_stopPullInHitCeilingFloor = true;
			}
		}
	}

	float springConstant = Sqr(pCurveSpringSettings->m_springConst);
	if (m_stopPullInHitCeilingFloor)
	{
		y = 0.f;
		springConstant = Sqr(pSettings->m_yArcSpringHitCeilingFloor);
	}

	m_curveYControl = m_curveYSpring.Track(m_curveYControl, y, cameraClock.GetDeltaTimeInSeconds(), springConstant, pCurveSpringSettings->m_dampingRatio);

	if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_displaySpeed))
	{
		MsgConPauseable("[%u] %s:\n", GetProcessId(), DevKitOnly_StringIdToString(GetDCSettingsId()));
	}

	if (Abs(m_curveYControl) > kT1JoypadThresh)
	{
		const float sensitivityScaleY = GetSensitivityScaleY();
		float curveMove = m_curveYControl * cameraClock.FromUPS(pSettings->m_curveSpeed) * pSettings->m_zoomSpeedMultiplier * sensitivityScaleY;

		{
			float dAngle = GetDeltaAnglePerT(m_verticalArc, m_arc);
			// safe guard.
			if (dAngle == 0.f)
				dAngle = 1.f;

			curveMove *= m_slowdownScale.y;
		}

		m_origArcForPitchSpeed = m_arc;
		m_arc = m_verticalArc.MoveAlongCameraCurve(m_arc, curveMove);
		m_arc = MinMax(m_arc, m_arcRange.x, m_arcRange.y);
		m_arcDelta = m_arc - m_origArcForPitchSpeed;

		if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_displaySpeed))
		{
			MsgConPauseable("  Y Arc curve Move: %.3f\n", curveMove);
			Vec2 origOffset = m_verticalArc.CalcCameraOffset(m_origArcForPitchSpeed);
			Scalar origOffsetXZ = origOffset.X();
			Scalar origOffsetY = origOffset.Y();

			Vec2 newOffset = m_verticalArc.CalcCameraOffset(m_arc);
			Scalar newOffsetXZ = newOffset.X();
			Scalar newOffsetY = newOffset.Y();

			const float cameraArcDeltaY = newOffsetY - origOffsetY;
			const float cameraArcDeltaX = newOffsetXZ - origOffsetXZ;

			const float dt = cameraClock.GetDeltaTimeInSeconds();
			if (dt != 0.0f)
			{
				MsgConPauseable("  Camera Arc Delta Meters Per Second: [X: %.3f Y: %.3f]\n", cameraArcDeltaX / dt, cameraArcDeltaY / dt);
				MsgConPauseable("  Camera Arc Delta Meters This Frame: [X: %.3f Y: %.3f]\n", cameraArcDeltaX, cameraArcDeltaY);
			}
		}
	}

	if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_displayArc))
	{
		MsgConPauseable("Current Arc: %.3f\n", m_arc);
	}
}

///-------------------------------------------------------------------------------------------///
bool CameraControlStrafeBase::DisableInputThisFrame() const
{
	// KANXU: set non-top strafe camera input to zero so that long blend won't pop.
	if (!g_strafeCamUpdateDuringIgcBlend && 
		!IsPhotoMode())
	{
		const CameraControl* pCamera = CameraManager::GetGlobal(m_playerIndex).GetCurrentCamera();
		const CameraControlAnimated* pAnimatedCamera = CameraControlAnimated::FromProcess(pCamera);
		if (pAnimatedCamera != nullptr)
		{
			return true;
		}
	}

	// this is to fix weird camera blend in orphanage, pickup drake's file. the top fade-by-dist camera has 0.0 blend, so it can't turn.
	bool allowCamControlWhenBlendLessThan70 = false;
	{
		if (CameraControl::IsFadeInByTravelDist() && m_blendInfo.m_allowFadeOutByDistCamControlWhenBlendLessThan70)
			allowCamControlWhenBlendLessThan70 = true;
	}

	if (m_disableControlsBlendIn)
	{
		allowCamControlWhenBlendLessThan70 = true;
	}

	// HACK fucntion to prevent cameras blends more than 180 degrees when fade out
	if (CameraManager::GetGlobal(m_playerIndex).IsInputDisabledWhenCameraFadeOutBlended() && m_normalBlend < 0.2f && !allowCamControlWhenBlendLessThan70)
	{
		return true;
	}

	// m_disableInputUntil70 is a parameter of camera-fade-out
	if (m_disableInputUntil70 && m_blend < 0.7f)
	{
		return true;
	}

	return false;
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::Prepare(const CameraInstanceInfo& info)
{
	ParentClass::Prepare(info);

	const Clock& cameraClock = *EngineComponents::GetNdFrameState()->GetClock(kCameraClock);
	m_preparedDt = cameraClock.GetDeltaTimeInSeconds();

	const NdGameObject* pFocusGo = m_hFocusObj.ToProcess();

	//if (!pPlayer)
	//{
	//	if (Horse* pHorse = Horse::FromProcess(GetFocusObject()))
	//	{
	//		pPlayer = pHorse->GetPlayerRider();
	//	}
	//}

	const Character* pPlayerChar = Character::FromProcess(pFocusGo);

	bool stickMovement = pPlayerChar && (pPlayerChar->GetNdJoypad()->GetStickSpeed(NdPlayerJoypad::kStickMove) > 0.0f ||
		pPlayerChar->GetNdJoypad()->GetStickSpeed(NdPlayerJoypad::kStickCamera) > 0.0f);

	// Don't increase near dist if no stick movement (to prevent near plane oscillation when not moving)
	if (stickMovement || m_targetNearPlaneDist < m_collision.m_cameraNearDist)
		m_collision.m_cameraNearDist = m_collision.m_cameraNearDistSpring.Track(m_collision.m_cameraNearDist, m_targetNearPlaneDist, m_preparedDt, 5.0f);

	UpdateCollNearPlane(m_collision.m_cameraNearDist);

	if (!pFocusGo)
		return;

	const DC::CameraStrafeBaseSettings* pSettings = GetSettings();

	U32 playerIndex = GetPlayerIndex();
	//ASSERT(!pPlayer || pPlayer->GetPlayerIndex() == playerIndex);

	CameraJoypad joyPad = info.m_joyPad;

	if (DisableInputThisFrame())
	{
		joyPad.m_rawStickCamera = Vec2(0, 0);
		joyPad.m_stickCamera = Vec2(0, 0);
	}

	UpdateParentSpace(pFocusGo);

	ReadJoypad(joyPad, info);

	//const Point debugPt = pPlayer->GetTranslation();
	//g_prim.Draw(DebugArrow(debugPt, m_yawControl.GetRequestedDir(), kColorCyan, 0.5f, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
	//g_prim.Draw(DebugString(debugPt + m_yawControl.GetRequestedDir(), "RequestedDir", kColorCyan), kPrimDuration1FramePauseable);
	//g_prim.Draw(DebugArrow(debugPt, m_yawControl.GetCurrentDir(), kColorMagenta, 0.5f, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
	//g_prim.Draw(DebugString(debugPt + m_yawControl.GetCurrentDir(), "CurrentDir", kColorMagenta), kPrimDuration1FramePauseable);

	// Determine spring constant for yaw spring.
	m_yawControl.UpdateCurrentYaw(m_preparedDt, info, pSettings, GetNormalBlend(), ParentClass::IsFadeInByTravelDist() || ParentClass::IsFadeInByDistToObj(), 0.f, false);

	//if (!m_photoMode)
	{
		m_arc = ClampArcToPitchAngles(m_verticalArc, m_arc);
	}

	// Done modifying m_arc and m_yawControl.
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::PrepareFinalize(bool isTopControl, const CameraControl* pPrevControl)
{
	///-----------------------------------------------------------------------------///
	/// DO NOT MODIFY m_arc and m_yawControl! It should be done in Prepare()!
	///-----------------------------------------------------------------------------///

	ParentClass::PrepareFinalize(isTopControl, pPrevControl);

	const NdGameObject* pFocusGo = m_hFocusObj.ToProcess();
	if (!pFocusGo)
		return;

	bool initCollision = !m_prepareCalled;

	// if Prepare() fails, don't set m_prepareCalled to true.
	// There was a crash Prepare() returns when animated-camera is blended, m_prepareCalled is set, but strafe-camera collision is not initialized.
	m_prepareCalled = true;

	const Vector cameraDirXZ = m_yawControl.GetCurrentDirWs();

	// Get base target position (before vertical arc and all other adjustments).
	m_baseTargetPos = GetTargetPosition(pFocusGo);

	if (ShouldDrawCollision(*this))
		DSphere(Sphere(m_baseTargetPos, 0.01f), kColorBlue, "baseTargetPos");

	Point arcTargetPos = m_verticalArc.OffsetTargetPoint(m_baseTargetPos, cameraDirXZ, m_arc, kUnitYAxis);

	//g_prim.Draw(DebugArrow(baseTargetPos, cameraDirXZ, kColorGreen, 1.0f), kPrimDuration1FramePauseable);
	//g_prim.Draw(DebugCross(baseTargetPos, 1.0f, 1.0f, kColorYellow), kPrimDuration1FramePauseable);
	//g_prim.Draw(DebugCross(arcTargetPos, 1.0f, 1.0f, kColorRed), kPrimDuration1FramePauseable);
	//MsgConPauseable("Arc: %.3f\n", m_arc);

	Point cameraPos = CalcCameraPosition(m_baseTargetPos, cameraDirXZ, m_arc);
	//g_prim.Draw(DebugCross(cameraPos, 1.0f, 1.0f, kColorPurple), kPrimDuration1FramePauseable);

	// calculate side-offset
	Vector sideOffset;
	{
		const Vector cameraToTargetRaw = arcTargetPos - cameraPos;
		const Vector cameraLeft = SafeNormalize(Cross(kUnitYAxis, cameraToTargetRaw), kZero);
		sideOffset = m_currentSideOffset * cameraLeft * g_cameraOffsetScale;
	}

	if (ShouldDrawCollision(*this))
	{
		DSphere(Sphere(arcTargetPos, 0.01f), kColorGray, "arcTargetPos unrotated");
		DSphere(Sphere(cameraPos, 0.01f), kColorYellow, "cameraPos unrotated");
	}

	//Transform the parameters into the appropriate space
	{
		Locator cameraComputedSpace(m_baseTargetPos, kIdentity);
		Locator cameraSpace(m_baseTargetPos, QuatFromVectors(kUnitYAxis, GetCameraUpVector()));

		m_baseTargetPos = cameraSpace.TransformPoint(cameraComputedSpace.UntransformPoint(m_baseTargetPos));
		arcTargetPos = cameraSpace.TransformPoint(cameraComputedSpace.UntransformPoint(arcTargetPos));
		cameraPos = cameraSpace.TransformPoint(cameraComputedSpace.UntransformPoint(cameraPos));
	}

	m_preparedCameraDir = SafeNormalize(arcTargetPos - cameraPos, kUnitZAxis);

	if (ShouldDrawCollision(*this))
	{
		DSphere(Sphere(arcTargetPos, 0.01f), kColorGray, "arcTargetPos");
		DSphere(Sphere(cameraPos, 0.01f), kColorYellow, "cameraPos");
	}

	const Point lastPreparedTargetPos = m_preparedTargetPos;
	const Point lastPreparedCameraPos = m_preparedCameraPos;

	m_preparedTargetPos = arcTargetPos + sideOffset;
	m_preparedCameraPos = cameraPos + sideOffset;
	GAMEPLAY_ASSERT(IsFinite(m_preparedTargetPos));
	GAMEPLAY_ASSERT(IsFinite(m_preparedCameraPos));
	if (ShouldDrawCollision(*this))
	{
		DSphere(Sphere(m_preparedTargetPos, 0.01f), kColorWhite, "finalTargetPos");
		DSphere(Sphere(m_preparedCameraPos, 0.02f), kColorYellow, "finalCameraPos");
	}

	const Vector cameraToTarget = m_preparedTargetPos - m_preparedCameraPos;
	const Vector cameraForwardWs = SafeNormalize(cameraToTarget, kZero);
	CalcCameraSafePos(cameraForwardWs, m_baseTargetPos);

	const Vector cameraUpDirWs = GetCameraUpVector();
	GAMEPLAY_ASSERT(IsFinite(cameraUpDirWs));
	Point arcCenterWs = m_baseTargetPos + sideOffset; // center point used by m_verticalArc
	GAMEPLAY_ASSERT(IsFinite(arcCenterWs));

	if (FALSE_IN_FINAL_BUILD(true))
	{
		const Vector testCameraDir = CalcCameraDir(cameraDirXZ, m_arc);
		ASSERT(IsClose(m_preparedCameraDir, testCameraDir, 0.0001f));
	}

#ifndef FINAL_BUILD
	if (g_strafeCamDbgOptions.m_displayArc)
	{
		MsgConPauseable("T1 Strafe Camera:  %s\n", DevKitOnly_StringIdToString(m_dcSettingsId));
		if (g_strafeCamDbgOptions.m_displayArcInWorldSpace)
		{
			const Vector centerToCameraWs = m_preparedCameraPos - arcCenterWs;
			const Scalar y = Dot(centerToCameraWs, cameraUpDirWs);
			const Vector centerToCameraY = y * cameraUpDirWs;
			const Vector centerToCameraXZ = centerToCameraWs - centerToCameraY;
			const Vector centerToCameraDirXZ = SafeNormalize(centerToCameraXZ, kZero);
			//g_prim.Draw(DebugLine(arcCenterWs, m_preparedCameraPos, kColorMagenta));
			//g_prim.Draw(DebugLine(arcCenterWs, centerToCameraY, kColorMagentaTrans));
			//g_prim.Draw(DebugLine(arcCenterWs + centerToCameraY, centerToCameraXZ, kColorOrangeTrans));
			m_verticalArc.DisplayCurveWs(m_arc, arcCenterWs, centerToCameraDirXZ, cameraUpDirWs, kPrimDuration1FramePauseable);
		}
		else
		{
			m_verticalArc.DisplayCurve(m_arc, false, kPrimDuration1FramePauseable);
		}
	}
#endif

	if (initCollision)
	{
		InitCollisionState(pPrevControl, m_baseTargetPos, arcCenterWs, cameraUpDirWs);
	}

	//maybe there is a better way to get the locator?
	//UpdateMarkTarget(GameGlobalCameraLocator(m_playerIndex));
	const Character* pChar = Character::FromProcess(pFocusGo); // may be null

	GAMEPLAY_ASSERT(IsFinite(m_preparedCameraPos));
	GAMEPLAY_ASSERT(IsFinite(m_preparedTargetPos));

	m_collision.m_hCharacter = pChar;
	m_collision.m_cameraPosWs = m_preparedCameraPos;
	m_collision.m_targetPosWs = m_preparedTargetPos;
	m_collision.m_horizontalPivotWs = m_baseTargetPos;
	m_collision.m_arcCenterWs = arcCenterWs;
	m_collision.m_safePosWs = GetCameraSafePos();
	m_collision.m_pushedInSafePosWs = PointFromXzAndY(Lerp(m_baseTargetPos, m_collision.m_safePosWs, 0.25f), m_collision.m_safePosWs + Vector(0.0f, 0.1f, 0.0f));
	m_collision.m_cameraUpDirWs = cameraUpDirWs;

	CollideFilter collFilter = GetCollideFilter();
	m_collision.KickProbes(this, m_verticalArc, m_arc, collFilter, m_collNearPlane.x, m_collNearPlane.y);

	if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_drawT1Cam))
	{
		g_prim.Draw(DebugCross(m_baseTargetPos, 0.1f, kColorRed), kPrimDuration1FramePauseable);

		g_prim.Draw(DebugCross(arcTargetPos, 0.1f, kColorBlue), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugCross(m_preparedTargetPos, 0.2f, kColorCyan), kPrimDuration1FramePauseable);

		g_prim.Draw(DebugCross(cameraPos, 0.1f, kColorGreen), kPrimDuration1FramePauseable);

		g_prim.Draw(DebugLine(cameraPos, arcTargetPos, kColorGreen, kColorBlue), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(m_preparedCameraPos, m_preparedTargetPos, kColorYellow, kColorCyan), kPrimDuration1FramePauseable);
	}
}

///-------------------------------------------------------------------------------------------///
CameraLocation CameraControlStrafeBase::UpdateLocation(const CameraInstanceInfo& info)
{
	GAMEPLAY_ASSERT(m_prepareCalled);

	const NdGameObject* pFocusObj = m_hFocusObj.ToProcess();
	const Character* pPlayerChar = Character::FromProcess(pFocusObj);

	ALWAYS_ASSERT(m_safePosValid);

	const DC::CameraStrafeBaseSettings* pSettings = GetSettings();

	// Do camera collision.
	{
		if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_drawT1Coll || g_strafeCamDbgOptions.m_displaySafePos))
		{
			DSphere(Sphere(GetCameraSafePos(), 0.02f), kColorWhite, "safePos");
		}

		const float defaultNearDist = GetDefaultNearDist(info.m_prevControl);

		bool shouldCollideWaterPlane = ShouldCollideWaterPlane();
		bool ignoreCeilingProbe = IgnoreCeilingProbe() || pSettings->m_ignoreCeilingProbe;

		float pullInPercent = ComputePullInPercent(info, defaultNearDist, shouldCollideWaterPlane, ignoreCeilingProbe);

		UpdateNearPlane2(info.m_topControl, defaultNearDist, pullInPercent);

		m_preparedCameraPos = ComputeCameraPosition(m_collision.GetFinalSafePos(), m_preparedTargetPos, m_preparedCameraPos, pullInPercent, m_collision.m_cameraNearDist);
		if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_debugPullIn))
			MsgConPauseable("STRAFE CAM (PID#%d) pull-in percent = %g\n", GetProcessId(), (float)pullInPercent);

		GAMEPLAY_ASSERT(IsFinite(m_preparedCameraPos));
		GAMEPLAY_ASSERT(IsFinite(pullInPercent));
		m_pullInPercent = MinMax01(pullInPercent);

		m_collision.UpdateVerticalDirection();

		GAMEPLAY_ASSERT(IsFinite(m_pullInPercent));

		if (ShouldDrawCollision(*this))
		{
			DSphere(Sphere(m_collision.m_safePosWs, 0.02f), kColorWhite, "safePos", 0.7f);
			DSphere(Sphere(m_collision.m_pushedInSafePosWs, 0.02f), kColorWhite, "pushed in safePos", 0.7f);
			DSphere(Sphere(m_collision.GetFinalSafePos(), 0.02f), kColorWhite, "final safePos", 0.7f);
		}

		if (ShouldDrawCollision(*this))
		{
			MsgConPauseable("pullInPercent: %f\n", pullInPercent);
		}
	}
	GAMEPLAY_ASSERT(IsFinite(m_preparedCameraPos));
	GAMEPLAY_ASSERT(IsFinite(m_preparedCameraDir));

	if (ShouldDrawCollision(*this) || g_strafeCamDbgOptions.m_drawT1Cam)
		g_prim.Draw(DebugArrow(m_preparedCameraPos, m_preparedCameraDir * SCALAR_LC(0.25f), kColorCyanTrans, 0.3f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);

	// Build the final locator.

	Locator camLoc(info.m_prevLocation.GetLocator());
	camLoc.SetTranslation(m_preparedCameraPos);
	//Vector cameraToTarget = finalTargetPos - finalCameraPos;
	//Vector forwardUnit = SafeNormalize(cameraToTarget, SMath::kUnitZAxis);
	camLoc.SetRotation(QuatFromLookAt(m_preparedCameraDir, GetCameraUpVector(true)));

	const float fov = GetFov();

#if !FINAL_BUILD
	if (g_strafeCamDbgOptions.m_drawT1Cam)
	{
		g_prim.Draw(DebugCoordAxes(camLoc), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(camLoc.GetTranslation(), "C"), kPrimDuration1FramePauseable);

		CameraLocation closeCam(Locator(m_collision.GetFinalSafePos(), camLoc.GetRotation()), fov);
		CameraManager::DebugDrawCamera(closeCam, 9.0f / 16.0f, kColorGreen, kColorRed, kPrimDuration1FrameNoRecord);
	}
#endif

	camLoc.SetPosition(camLoc.GetPosition());

	return CameraLocation(camLoc, m_preparedTargetPos, fov, m_collision.m_cameraNearDist);
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::OnKillProcess()
{
	ParentClass::OnKillProcess();
	m_collision.OnKillProcess();
}

///-------------------------------------------------------------------------------------------///
const NdGameObject* CameraControlStrafeBase::GetCameraParent(const NdGameObject* pFocusGo)
{
	return pFocusGo ? pFocusGo->GetCameraParent().ToProcess() : nullptr;
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::UpdateParentSpace(const NdGameObject* pFocusGo)
{
	const NdGameObject* pParentGo = GetCameraParent(pFocusGo);
	const DC::CameraStrafeBaseSettings* pSettings = GetSettings();
	m_yawControl.UpdateParentSpace(pParentGo, pSettings, m_preparedDt);
}

///-------------------------------------------------------------------------------------------///
Locator CameraControlStrafeBase::GetBaseSafeLocWs() const
{
	const NdGameObject* pFocusObj = m_hFocusObj.ToProcess();
	return pFocusObj ? pFocusObj->GetLookAtAlignLocator() : Locator(kIdentity);
}

///-------------------------------------------------------------------------------------------///
/// Initialize member variables: camera-settings not initialized yet.
///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::Initialize(const CameraStartInfo& baseStartInfo)
{
	const CameraStrafeStartInfo& startInfo = *PunPtr<const CameraStrafeStartInfo*>(&baseStartInfo);

	m_hFocusObj = startInfo.GetFocusObject();
	m_flags.m_needsFocus = true;

	m_distToObj.Reset();

	m_safePosValid = false;
	m_prepareCalled = false;
	m_killWhenSelfFadedOut = baseStartInfo.m_killWhenSelfFadedOut;

	m_collNearPlane = Vec2(0, 0);
	m_targetNearPlaneDist = CameraManager::GetGlobal(GetPlayerIndex()).GetNearPlaneDist();

	m_baseDcSettingsId = INVALID_STRING_ID_64;
	m_dcSettingsId = INVALID_STRING_ID_64;

	m_preparedDt = 0.001f;
	m_preparedCameraPos = Point(0.0f, 1.0f, 0.0f);
	m_baseTargetPos = kOrigin;
	m_preparedTargetPos = kOrigin;
	m_preparedCameraDir = m_preparedTargetPos - m_preparedCameraPos;

	m_currentSideOffset = 0.f;

	// safe pos.
	m_rawSafePosWs = kOrigin;
	m_safePosCeilingAdjust = kZero;
	m_safePosCeilingAdjustSpring.Reset();

	// Set the start yaw. Note however that the yaw control is in parent space when camera parenting is enabled.
	const NdGameObject* pFocusObj = m_hFocusObj.ToProcess();
	const NdGameObject* pParentGo = GetCameraParent(pFocusObj);

	m_yawControl.InitParentSpace(pParentGo);

	m_arc = 0.5f;

	m_curveYControl = 0.0;
	m_curveYSpring.Reset();

	m_lastTargetMoveTime = TimeFrameNegInfinity();
	m_lastStickTouchTime = TimeFrameNegInfinity();
	m_lastStickNoTouchTime = TimeFrameNegInfinity();
	for (U32F iStickDir = 0; iStickDir < kStickDirCount; ++iStickDir)
		m_lastStickDirTime[iStickDir] = TimeFrameNegInfinity();

	for (U8 iStickAxis = 0; iStickAxis < kStickAxisCount; ++iStickAxis)
		m_lastStickAxisTime[iStickAxis] = TimeFrameNegInfinity();

	m_motionBlurIntensity = 0.0f;

	m_collideWithSimpleNpcs = true;

	m_slowdownScale = Vec2(1.0f, 1.0f);
	m_slowdownTracker.Reset();

	m_curFov = 45.0f;
	m_disableInputUntil70 = startInfo.m_disableInputUntil70;

	m_startTimeCameraClock = TimeFrameNegInfinity();
}

///-------------------------------------------------------------------------------------------///
CameraRemapSettings CameraControlStrafeBase::RemapSettingsId(StringId64 baseDcSettingsId, bool includeDistRemap) const
{
	const CameraRemapSettings remapSettings = RemapCameraSettingsId(baseDcSettingsId, GetPlayerIndex(), includeDistRemap);
	return remapSettings;
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::InitCameraSettings(const CameraStartInfo& baseStartInfo)
{
	const CameraStrafeStartInfo& startInfo = *PunPtr<const CameraStrafeStartInfo*>(&baseStartInfo);

	const NdGameObject* pFocusObj = m_hFocusObj.ToProcess();

	// get our settings from dc
	GAMEPLAY_ASSERT(startInfo.m_settingsId != INVALID_STRING_ID_64);
	m_baseDcSettingsId = startInfo.m_settingsId;

	if (AllowRemap())
	{
		bool includeDistRemap = true;
		bool prevCamIsClamber = false;
		{
			const CameraControl* pPrevCamera = CameraManager::GetGlobal(GetPlayerIndex()).GetCurrentCamera();
			if (pPrevCamera && pPrevCamera->IsFadeInByDistToObj())
				includeDistRemap = false; // dist-remap is for that camera, not me

			if (pPrevCamera && pPrevCamera->IsKindOf(SID("CameraControlClamber")))
				prevCamIsClamber = true;

			if (startInfo.m_distCamBase)
				includeDistRemap = false;
		}

		const CameraRemapSettings& remapSettings = RemapSettingsId(m_baseDcSettingsId, includeDistRemap);
		m_dcSettingsId = remapSettings.m_remapSettingsId;
		bool instantBlend = false;
		{
			if (remapSettings.m_requestStartTime > Seconds(0))
			{
				const TimeFrame& playerSpawnTime = GetCameraRemapSpawnTime(GetPlayerIndex());
				const TimeFrame& currTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();

				const float deltaTime1 = (currTime - playerSpawnTime).ToSeconds();
				const float deltaTime2 = (currTime - remapSettings.m_requestStartTime).ToSeconds();
				if (Abs(deltaTime1) < 0.5f && Abs(deltaTime2) < 0.5f)
				{
					instantBlend = true;
				}
			}
		}
		m_remapParams = CameraRemapParamsEx(remapSettings.m_params, instantBlend);

		// DEMO HACK! clamber camera keeps shifting after clamber. Don't use traverl-dist fade for it.
		if (prevCamIsClamber)
		{
			m_remapParams.m_fadeInDist = -1.0f;
			m_remapParams.m_fadeOutDist = -1.0f;
		}

		if (startInfo.m_pDistRemapParams != nullptr)
		{
			m_remapParams.m_distParams = *startInfo.m_pDistRemapParams;
		}
	}
	else
	{
		m_dcSettingsId = m_baseDcSettingsId;
	}

	const DC::CameraStrafeBaseSettings* pSettings = LookupSettings(m_dcSettingsId);
	GAMEPLAY_ASSERTF(pSettings, ("Trying to set an camera setting (%s) that doesn't exist!", DevKitOnly_StringIdToString(m_dcSettingsId)));
	GAMEPLAY_ASSERTF(pSettings->m_magicNumber == 80085, ("Trying to set an OLD follow camera settings! (%s)", DevKitOnly_StringIdToString(m_dcSettingsId)));
	m_collision.OnCameraStart(&pSettings->m_collSettings, pFocusObj, CameraManager::GetGlobal(GetPlayerIndex()).GetNearPlaneDist());

	m_curFov = pSettings->m_fov;
	m_fovSpring.Reset();

	m_verticalArc.Setup((DC::CameraArcSettings*)&pSettings->m_arcSettings, nullptr);

	m_currentSideOffset = pSettings->m_maxSideOffset * -1.f;
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::CalcInitArcAndYaw(const CameraStartInfo& baseStartInfo)
{
	const CameraStrafeStartInfo& startInfo = *PunPtr<const CameraStrafeStartInfo*>(&baseStartInfo);

	Quat startRot(kIdentity);

	const NdGameObject* pFocusObj = m_hFocusObj.ToProcess();

	const CameraControl* pPrevCamera = CameraManager::GetGlobal(GetPlayerIndex()).GetCurrentCamera();
	const Locator prevCamLoc = GameGlobalCameraLocator(GetPlayerIndex());

	if (pPrevCamera == nullptr && (startInfo.GetLocator() != nullptr || pFocusObj != nullptr))
	{
		if (startInfo.GetLocator() != nullptr)
		{
			startRot = startInfo.GetLocator()->GetRotation();
		}
		else if (pFocusObj != nullptr)
		{
			startRot = pFocusObj->GetRotation();
		}
	}
	else
	{
		if (startInfo.GetLocator() != nullptr)
		{
			startRot = startInfo.GetLocator()->GetRotation();
			//Dont read anything from the prev camera
			if (startInfo.m_forceCut)
			{
				pPrevCamera = nullptr;
			}
		}
		else
		{
			startRot = prevCamLoc.GetRotation();
		}
	}

	const CameraControlStrafeBase* pPrevStrafeCamera = CameraControlStrafeBase::FromProcess(pPrevCamera);

	m_yawControl.SetInitialDirPs(m_yawControl.YawDirFromWorldSpace(-GetLocalZ(startRot)));

	if (pPrevStrafeCamera != nullptr)
	{
		m_arc = pPrevStrafeCamera->m_arc;
		m_yawControl = pPrevStrafeCamera->m_yawControl;

		// if prev camera is underwater swim camera, and new camera is underwater strafe, because they have different tilted align from camera angle,
		// so we try to calculate a arc which has closest camera direction to previous camera direction.
		//if (pSettings->m_calcArcFromPrevCameraDirection)
		//	m_arc = CalcClosestArcFromCameraDir(pPrevStrafeCamera->m_preparedCameraDir);

		//if (const DC::CameraStrafeSettings* pPrevStrafeCamSettings = ScriptManager::Lookup<DC::CameraStrafeSettings>(pPrevCamera->GetDCSettingsId(), nullptr))
		//{
		//	if (pPrevStrafeCamSettings->m_calcNextCameraArcFromMyDirection)
		//		m_arc = CalcClosestArcFromCameraDir(pPrevStrafeCamera->m_preparedCameraDir);
		//}
	}

	const Vector cameraDirXZ = m_yawControl.GetCurrentDirWs();

	m_baseTargetPos = GetTargetPosition(pFocusObj);	// when a new camera start, it shouldn't be any next camera.
	m_preparedTargetPos = m_verticalArc.OffsetTargetPoint(m_baseTargetPos, cameraDirXZ, m_arc, kUnitYAxis);
	GAMEPLAY_ASSERT(IsFinite(m_preparedTargetPos));

	m_preparedCameraPos = CalcCameraPosition(m_baseTargetPos, cameraDirXZ, m_arc);
	GAMEPLAY_ASSERT(IsFinite(m_preparedCameraPos));

	m_preparedCameraDir = SafeNormalize(m_preparedTargetPos - m_preparedCameraPos, kUnitZAxis);

	{
		bool initializeYawDirOnly = startInfo.m_arc >= 0.0f;
		CameraId prevCameraId = pPrevCamera ? pPrevCamera->GetCameraId() : kCameraNone;

		if (pPrevStrafeCamera != nullptr)
		{
			if (initializeYawDirOnly)
				SetLookAtDirXz(GetLocalZ(prevCamLoc.GetRotation()));
			else
				SetLookAtDir(GetLocalZ(prevCamLoc.GetRotation()));
		}
		else if (!pPrevCamera)
		{
			if (initializeYawDirOnly)
				SetLookAtDirXz(GetLocalZ(startRot));
			else
				SetLookAtDir(GetLocalZ(startRot));
		}

		// Initialize for target dir resets the spring, so set it again here
		if (pPrevStrafeCamera != nullptr)
		{
			if (const DC::CameraStrafeBaseSettings* pPrevStrafeCamSettings = ScriptManager::Lookup<DC::CameraStrafeBaseSettings>(pPrevCamera->GetDCSettingsId(), nullptr))
			{
				float startSpeed = pPrevStrafeCamera->m_yawControl.GetSpringSpeed();
				m_yawControl.SetInitialSpeed(startSpeed, pPrevStrafeCamSettings->m_yawSpringSettings.m_activeSettings.m_springConst);
				m_yawControl.SetRequestedAnglePs(pPrevStrafeCamera->m_yawControl.GetRequestedAnglePs());
			}
		}
	}
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::NotifyCut(const CameraControl* pPrevControl)
{
	// reset camera-near-dist when it's camera cut.
	const float defaultNearDist = GetDefaultNearDist(pPrevControl);
	m_targetNearPlaneDist = defaultNearDist;
	m_collision.ResetNearDist(defaultNearDist);
}

///-------------------------------------------------------------------------------------------///
const DC::CameraStrafeBaseSettings* CameraControlStrafeBase::LookupSettings(StringId64 settingsId)
{
	GAMEPLAY_ASSERT(settingsId != INVALID_STRING_ID_64);

	const DC::CameraStrafeBaseSettings* pSettings = ScriptManager::Lookup<DC::CameraStrafeBaseSettings>(settingsId, nullptr);

	if (pSettings == nullptr)
	{
		MsgWarn("Couldn't find camera-strafe-settings: %s\n", DevKitOnly_StringIdToString(settingsId));
	}

	return pSettings;
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::UpdateCollNearPlane(float nearDist)
{
	GAMEPLAY_ASSERT(nearDist > 0);
	const RenderCamera& renderCam = g_mainCameraInfo[GetPlayerIndex()].GetRenderCamera();
	F32 aspectRatio = renderCam.GetAspect();
	m_collNearPlane.x = nearDist * Tan(DEGREES_TO_RADIANS(GetFov() * 0.5f));
	m_collNearPlane.x = Limit(m_collNearPlane.x, 0.005f, 10.0f);
	m_collNearPlane.y = m_collNearPlane.x * aspectRatio;
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::CalcCameraSafePos(Vector_arg cameraForwardWs, Point_arg baseTargetPosWs)
{
	const DC::CameraStrafeBaseSettings* pSettings = GetSettings();

	Point safePointAlignSpace = pSettings->m_safePoint;
	GAMEPLAY_ASSERT(IsFinite(safePointAlignSpace));

	Scalar ceilingYAdjust(0.0f);

	const NdGameObject* pFocusObj = m_hFocusObj.ToProcess();
	if (pFocusObj != nullptr)
	{
		if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_drawT1Coll))
		{
			const RenderCamera& renderCam = g_mainCameraInfo[GetPlayerIndex()].GetRenderCamera();
			const Point currentSafePosAs = GetBaseSafeLocWs().UntransformPoint(renderCam.m_position);
			MsgCon("currentCamPos in align space =  (%g, %g, %g)\n", (F32)currentSafePosAs.X(), (F32)currentSafePosAs.Y(), (F32)currentSafePosAs.Z());
			MsgCon("currentSafePos in align space = (%g, %g, %g)\n", (F32)safePointAlignSpace.X(), (F32)safePointAlignSpace.Y(), (F32)safePointAlignSpace.Z());
		}

		const Locator baseSafeLocWs = GetBaseSafeLocWs();
		GAMEPLAY_ASSERT(IsFinite(baseSafeLocWs));

		if (pSettings->m_camAlignAngleSafePointYCurve)
		{
			const Vector baseFlatZ = AsUnitVectorXz(GetLocalZ(baseSafeLocWs), kZero);
			const Vector camFlatZ = GameCameraDirectionXz();
			const float dotProd = Dot(camFlatZ, baseFlatZ);
			const float degrees = Abs(RADIANS_TO_DEGREES(Acos(dotProd)));

			const float newY = NdUtil::EvaluatePointCurve(degrees, pSettings->m_camAlignAngleSafePointYCurve);
			safePointAlignSpace.SetY(newY);
		}

		Point safePointWs;
		if (pSettings->m_cameraRelativeSafePointZ)
		{
			const Vector baseFlatZ = AsUnitVectorXz(GetLocalZ(baseSafeLocWs), kZero);
			const Scalar zScale = Dot(GameCameraDirectionXz(), baseFlatZ);
			safePointAlignSpace.SetZ(safePointAlignSpace.Z() * zScale);

			safePointWs = baseSafeLocWs.TransformPoint(safePointAlignSpace);
		}
		else
		{
			safePointWs = baseSafeLocWs.TransformPoint(safePointAlignSpace);
		}

		const Point desiredPlayerSafePointWs = safePointWs;

		const float desiredAdjY = (desiredPlayerSafePointWs - safePointWs).Get(1);

		if (m_safePointAdjY.Valid())
		{
			m_safePointAdjY = m_safePointAdjSpring.Track(m_safePointAdjY.Get(), desiredAdjY, pFocusObj->GetClock()->GetDeltaTimeInSeconds(), 12.f);
		}
		else
		{
			m_safePointAdjY = desiredAdjY;
			m_safePointAdjSpring.Reset();
		}
		Point playerSafePointWs = safePointWs + Vector(0.f, m_safePointAdjY.Get(), 0.f);;

		if (IsPhotoMode())
			playerSafePointWs = PointFromXzAndY(m_baseTargetPos, playerSafePointWs);

		// Move safe point up when camera is looking up (cute trick to avoid clipping thru player's head).
		const Scalar lookingUpFactor = Max(cameraForwardWs.Y(), SCALAR_LC(0.0f));
		const Scalar lookingUpYAdjust = ScalarLerpScale(SCALAR_LC(0.0f), SCALAR_LC(1.0f),
			SCALAR_LC(0.0f), Scalar(m_collision.m_cameraNearDist),
			lookingUpFactor);
		const Scalar finalLookingUpYAdjust = ScalarLerpScale(SCALAR_LC(0.0f), SCALAR_LC(0.5f), lookingUpYAdjust, SCALAR_LC(0.0f), Scalar(m_pullInPercent));
		playerSafePointWs += VectorFromXzAndY(Vector(kZero), Vector(finalLookingUpYAdjust.QuadwordValue()));

		const Vector baseTargetToSafeXz = VectorXz(playerSafePointWs - baseTargetPosWs);
		const Vector playerForwardXz = SafeNormalize(VectorXz(GetLocalZ(LevelQuat(pFocusObj->GetRotation()))), kZero);
		const Vector cameraForwardXz = SafeNormalize(VectorXz(cameraForwardWs), kZero);
		const Quat rotPlayerToCamera = QuatFromVectors(playerForwardXz, cameraForwardXz);
		const Vector rotatedBaseTargetToSafeXz = Rotate(rotPlayerToCamera, baseTargetToSafeXz);
		const Point rotCenterWs = PointFromXzAndY(baseTargetPosWs, playerSafePointWs);
		//g_prim.Draw(DebugLine(rotCenterWs, baseTargetToSafeXz, kColorOrangeTrans, 3.0f));
		//g_prim.Draw(DebugLine(rotCenterWs, rotatedBaseTargetToSafeXz, kColorOrange, 3.0f));

		const Point rotatedSafePointWs = g_enableRotatedSafePos ? rotCenterWs + rotatedBaseTargetToSafeXz : playerSafePointWs;

		m_rawSafePosWs = rotatedSafePointWs;

		// If the safe point is above the ceiling, bring it down...
		// When we're not pulled in, we actually bring it down MORE than necessary to prevent overhangs
		// from artificially pulling in the camera. But if we're tightly pulled in, only bring it down by
		// the bare minimum amount to avoid collision.

		float ceilingYAdjustNoExtra = Min(m_collision.m_ceilingYAdjust + m_collision.m_ceilingYAdjustExtra, 0.0f);
		//ceilingYAdjust = LerpScale(0.0f, 1.0f, ceilingYAdjustNoExtra, m_collision.m_ceilingYAdjust, m_pullInPercent); // EXPERIMENTING HERE
		ceilingYAdjust = ceilingYAdjustNoExtra;

		GAMEPLAY_ASSERT(IsFinite(safePointWs));
		GAMEPLAY_ASSERT(IsFinite(playerSafePointWs));
		GAMEPLAY_ASSERT(IsFinite(baseTargetToSafeXz));
		GAMEPLAY_ASSERT(IsFinite(rotPlayerToCamera));
		GAMEPLAY_ASSERT(IsFinite(rotPlayerToCamera));
		GAMEPLAY_ASSERT(IsFinite(rotatedBaseTargetToSafeXz));
		GAMEPLAY_ASSERT(IsFinite(rotCenterWs));
		GAMEPLAY_ASSERT(IsFinite(rotatedSafePointWs));
		GAMEPLAY_ASSERT(IsFinite(ceilingYAdjust));
	}
	else
	{
		m_rawSafePosWs = baseTargetPosWs;
	}

	if (!m_safePosValid)
	{
		m_safePosCeilingAdjust.SetY(ceilingYAdjust);
		m_safePosValid = true;
	}
	else
	{
		// If we're tightening the ceiling y adjust, do it very quickly to prevent any camera clip-thru.
		// If we're relaxing the ceiling y adjust, do it smoothly.
		const float springConstIn = 100.0f;
		const float pullInFactor = Pow(m_pullInPercent, 1.0f / 3.0f);
		const float springConstOut = LerpScale(0.0f, 1.0f, springConstIn * 0.5f, 1.0f, pullInFactor);
		const bool springingIn = (ceilingYAdjust < m_safePosCeilingAdjust.Y());
		const float springConst = springingIn ? springConstIn : springConstOut; // EXPERIMENTING HERE
		I32 springDir = springingIn ? +1 : -1;
		GAMEPLAY_ASSERT(IsFinite(m_safePosCeilingAdjust));
		GAMEPLAY_ASSERT(IsFinite(m_safePosCeilingAdjustSpring.GetTracker().m_speed));
		const Scalar smoothedYAdjust = m_safePosCeilingAdjustSpring.Track((F32)m_safePosCeilingAdjust.Y(), (F32)ceilingYAdjust, m_preparedDt, springConst, springDir);
		GAMEPLAY_ASSERT(IsFinite(smoothedYAdjust));
		m_safePosCeilingAdjust.SetY(smoothedYAdjust);
	}

	ALWAYS_ASSERT(IsFinite(m_rawSafePosWs));
	ALWAYS_ASSERT(IsFinite(m_safePosCeilingAdjust));
}

///-------------------------------------------------------------------------------------------///
Point CameraControlStrafeBase::GetCameraSafePos() const
{
	GAMEPLAY_ASSERT(m_safePosValid);
	GAMEPLAY_ASSERT(IsFinite(m_rawSafePosWs));
	return m_rawSafePosWs + m_safePosCeilingAdjust;
}

///-------------------------------------------------------------------------------------------///
float CameraControlStrafeBase::GetErrorForArcDir(float arc, void* pContext)
{
	sErrorForArcDirContext* pArcContext = static_cast<sErrorForArcDirContext*>(pContext);

	Point targetPoint = pArcContext->pVerticalArc->OffsetTargetPoint(kOrigin, kUnitXAxis, arc, kUnitYAxis);
	Vec2 cameraOffset = pArcContext->pVerticalArc->CalcCameraOffset(arc);
	Point cameraPoint = kOrigin + Vector(cameraOffset.X(), cameraOffset.Y(), 0.f); // IS THIS WRONG?

	Vector cameraDir = SafeNormalize(targetPoint - cameraPoint, kUnitZAxis);

	float angleDir = ComponentPerpendicularTo(pArcContext->aimDirInCameraCurveSpace, cameraDir).Y();

	float angle = Acos(MinMax((float)Dot(cameraDir, pArcContext->aimDirInCameraCurveSpace), -1.0f, 1.0f));
	angle *= Sign(angleDir);
	
	/*g_prim.Draw(DebugArrow(cameraPoint, cameraPoint + cameraDir, kColorRed), kPrimDuration1FramePauseable);
	g_prim.Draw(DebugArrow(cameraPoint, cameraPoint + pArcContext->aimDirInCameraCurveSpace, kColorGreen), kPrimDuration1FramePauseable);
	g_prim.Draw(DebugString(cameraPoint, StringBuilder<32>("%.3f\n", RADIANS_TO_DEGREES(angle)).c_str()), kPrimDuration1FramePauseable);*/

	return angle;
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::SetLookAtDirXz(Vector_arg lookAtDirWs)
{
	const Vector baseDirXZPs = m_yawControl.YawDirFromWorldSpace(lookAtDirWs);
	m_yawControl.SetInitialDirPs(-baseDirXZPs);
}

///-------------------------------------------------------------------------------------------///
bool CameraControlStrafeBase::CalcYawAndArcForTargetDir(Vector_arg targetDir, Angle* pOutYawAnglePs, float* pOutArc) const
{
	const Vector baseDirXZPs = m_yawControl.YawDirFromWorldSpace(targetDir);
	const Vector dirXzNorm = SafeNormalize(VectorXz(baseDirXZPs), Vector(kUnitZAxis));
	*pOutYawAnglePs = AngleFromXZVec(-baseDirXZPs);

	// Factor out camera up tilt
	Quat cameraSpaceRot = QuatFromVectors(kUnitYAxis, GetCameraUpVector());
	Vector aimDirUntilt = Unrotate(cameraSpaceRot, targetDir);

	// Get cam dir in arc space
	const Vector currDirPs = VectorFromAngleXZ(*pOutYawAnglePs);
	Vector cameraDirXZ = m_yawControl.YawDirToWorldSpace(currDirPs);
	Vector upDir = GetCameraUpVector();
	Vector left = SafeNormalize(Cross(upDir, cameraDirXZ), kUnitZAxis);
	Vector projAimDirWS = ProjectOntoPlane(aimDirUntilt, left);
	Quat cameraTargetSpaceRot = QuatFromXZDir(-left);
	Vector aimDirCameraSpace = Unrotate(cameraTargetSpaceRot, projAimDirWS);

	sErrorForArcDirContext bisectionContext;
	bisectionContext.pVerticalArc = &m_verticalArc;
	bisectionContext.aimDirInCameraCurveSpace = aimDirCameraSpace;

	// check a handful of segments and choose the best zero-crossing segment
	// helps handle local minima/maxima
	const int segments = 5;
	float segmentArc[segments + 1];
	{
		for (int i = 0; i < segments; i++)
			segmentArc[i] = m_arcRange.x + (m_arcRange.y - m_arcRange.x) * (float)i / (float)segments;
		segmentArc[segments] = m_arcRange.y;
	}

	float segmentEndpointError[segments + 1];
	for (int i = 0; i < segments; i++)
		segmentEndpointError[i] = GetErrorForArcDir(segmentArc[i], &bisectionContext);
	segmentEndpointError[segments] = GetErrorForArcDir(m_arcRange.y, &bisectionContext);

	int bestSegment = -1;
	float bestSegmentRange = 10000.0f;
	for (int i = 0; i < segments; i++)
	{
		const float endA = segmentEndpointError[i];
		const float endB = segmentEndpointError[i + 1];
		const float range = Abs(endA - endB);

		if (Sign(endA) != Sign(endB) && range < bestSegmentRange)
		{
			bestSegment = i;
			bestSegmentRange = range;
		}
	}

	bool result = false;
	float arc = m_arcRange.x;
	if (bestSegment >= 0)
	{
		const float endA = segmentArc[bestSegment];
		const float endB = segmentArc[bestSegment + 1];
		arc = Bisection::Solve(GetErrorForArcDir, endA, endB, &bisectionContext, 16, FLT_EPSILON);
		result = true;
	}
	else
	{
		// No solution, either max up or max down.
		arc = Abs(segmentEndpointError[0]) < Abs(segmentEndpointError[segments]) ? m_arcRange.x : m_arcRange.y;
	}

	*pOutArc = arc;
	return result;
}

///-------------------------------------------------------------------------------------------///
bool CameraControlStrafeBase::SetLookAtDir(Vector_arg lookAtDirWs)
{
	PROFILE(Player, StrafeCam_MaintainAim);

	Angle yawAnglePs = Angle(0);
	float newArc = 0.f;
	bool result = CalcYawAndArcForTargetDir(lookAtDirWs, &yawAnglePs, &newArc);

	m_yawControl.SetInitialDirPs(VectorFromAngleXZ(yawAnglePs));
	m_arc = newArc;

	return result;
}

///-------------------------------------------------------------------------------------------///
Point CameraControlStrafeBase::GetTargetPosition(const NdGameObject* pFocusGo) const
{
	const Point basePoint = GetBaseLookAtPointWs();
	Vector offsetWs = kZero;

	const DC::CameraStrafeBaseSettings* pSettings = GetSettings();

	if (pFocusGo && LengthSqr(pSettings->m_targetSettings.m_offset) > 0.f)
	{
		Vector offsetLs = pSettings->m_targetSettings.m_offset;
		offsetWs = Rotate(pFocusGo->GetRotation(), offsetLs);
	}

	const Point baseTargetPosWs = basePoint + offsetWs;

	if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_displayTarget))
	{
		g_prim.Draw(DebugCross(basePoint, 0.25f, Color(0.0f, 0.2f, 0.0f), PrimAttrib(0)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugCross(baseTargetPosWs, 0.2f, kColorGreen, PrimAttrib(0)), kPrimDuration1FramePauseable);
	}

	return baseTargetPosWs;
}

///-------------------------------------------------------------------------------------------///
Point CameraControlStrafeBase::GetBaseLookAtPointWs() const
{
	Point baseTargetPosWs(kOrigin);

	const NdGameObject* pFocusGo = m_hFocusObj.ToProcess();
	if (pFocusGo != nullptr)
	{
		const DC::CameraStrafeBaseSettings* pSettings = GetSettings();
		baseTargetPosWs = pFocusGo->GetSite(pSettings->m_targetSettings.m_lookAtSiteName).GetTranslation();

		StringId64 splineGuideY = GetLookAtSplineGuideY();

		// find closest point on spline and use its y for look-at target.
		if (splineGuideY != INVALID_STRING_ID_64)
		{
			const CatmullRom* pSplineY = g_splineManager.FindByName(splineGuideY);
			if (pSplineY != nullptr)
			{
				const Point closestPos = pSplineY->FindClosestPointOnSpline(baseTargetPosWs);
				baseTargetPosWs = PointFromXzAndY(baseTargetPosWs, closestPos);
			}
		}
	}

	return baseTargetPosWs;
}

///-------------------------------------------------------------------------------------------///
Point CameraControlStrafeBase::CalcCameraPosition(Point_arg baseTargetPos, Vector_arg cameraDirXZ, float arc) const
{
	Vec2 offset = m_verticalArc.CalcCameraOffset(arc);
	Scalar offsetXZ = offset.X();
	Scalar offsetY = offset.Y();
	Vector cameraOffset = Vector(cameraDirXZ.X() * offsetXZ, offsetY, cameraDirXZ.Z() * offsetXZ);

	Point cameraPos = baseTargetPos + cameraOffset;
	return cameraPos;
}

///-------------------------------------------------------------------------------------------///
Point CameraControlStrafeBase::ComputeCameraPosition(Point_arg safePosWs, Point_arg targetPosWs, Point_arg cameraPosWs, Scalar_arg pullInPercent, float nearDist)
{
	const Vector cameraDirWs = SafeNormalize(targetPosWs - cameraPosWs, kZero);
	const Point adjustedSafePosWs = safePosWs - cameraDirWs * Scalar(nearDist);
	const Point finalCameraPosWs = Lerp(adjustedSafePosWs, cameraPosWs, pullInPercent);

	if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_debugPullIn))
	{
		PrimAttrib attrib(kPrimEnableHiddenLineAlpha, kPrimEnableWireframe);
		g_prim.Draw(DebugLine(adjustedSafePosWs, cameraPosWs, kColorGrayTrans, 2.0f, attrib), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugSphere(adjustedSafePosWs, 0.05f, kColorWhite, attrib), kPrimDuration1FramePauseable);
		//g_prim.Draw(DebugString(adjustedSafePosWs, "adjusted-safe-pos", kColorWhite, 0.6f), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugSphere(cameraPosWs, 0.04f, kColorBlue, attrib), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugSphere(finalCameraPosWs, 0.05f, kColorCyan, attrib), kPrimDuration1FramePauseable);
	}

	return finalCameraPosWs;
}

///-------------------------------------------------------------------------------------------///
float CameraControlStrafeBase::ComputePullInPercent(const CameraInstanceInfo& info, float defaultNearDist, bool shouldCollideWaterPlane, bool ignoreCeilingProbe)
{
	const DC::CameraStrafeBaseSettings* pSettings = GetSettings();

	m_collision.GatherBlockingCharacterProbe();

	return m_collision.ComputePullInPercent(this,
											&pSettings->m_collSettings,
											m_preparedDt,
											info.m_topControl,
											defaultNearDist,
											info.m_pWaterPlane ? *info.m_pWaterPlane : CamWaterPlane(),
											shouldCollideWaterPlane,
											ignoreCeilingProbe,
											false);
}

///-------------------------------------------------------------------------------------------///
bool CameraControlStrafeBase::EnableNpcCollision(const NdGameObject* pFocusGo)
{
	if (g_ndConfig.m_pNetInfo->IsNetActive())
	{
		return false;
	}

	return pFocusGo && pFocusGo->CameraStrafeEnableNpcCollision();
}

///-------------------------------------------------------------------------------------------///
CollideFilter CameraControlStrafeBase::GetCollideFilter() const
{
	const NdGameObject* pFocusObj = m_hFocusObj.ToProcess();

	Collide::LayerMask layerMask = (Collide::kLayerMaskCameraCasts | Collide::kLayerMaskWater); // | Collide::kLayerMaskAlliedNpc);
	if (EnableNpcCollision(pFocusObj))
		layerMask = layerMask | Collide::kLayerMaskEnemyNpc; // U3FINAL: don't do this in net games because it screws up melee camera

	if (!m_collideWithSimpleNpcs)
	{
		layerMask = layerMask & ~Collide::kLayerMaskLayerSimpleNpc;
	}

	I64 mask = 1ULL << Pat::kCameraPassThroughShift;

	CollideFilter collFilter(layerMask, Pat(mask));
	return collFilter;
}

///-------------------------------------------------------------------------------------------///
static Scalar ComputePullInPercentFromCameraPosition(Point_arg safePosWs, Point_arg idealCameraPosWs, Point_arg targetPosWs, Point_arg finalCameraPosWs, float nearDist)
{
	const Vector cameraDirWs = SafeNormalize(targetPosWs - idealCameraPosWs, kZero);
	const Point adjustedSafePosWs = safePosWs - cameraDirWs * Scalar(nearDist);

	const Scalar idealDistFromSafe = Dist(adjustedSafePosWs, idealCameraPosWs);
	const Scalar distFromSafe = Dist(adjustedSafePosWs, finalCameraPosWs);
	const Scalar pullInPercent = Min(Max(distFromSafe * Recip(idealDistFromSafe), SCALAR_LC(0.0f)), SCALAR_LC(1.0f));

#if !FINAL_BUILD
	if (g_strafeCamDbgOptions.m_debugPullIn)
	{
		PrimAttrib attrib(kPrimEnableHiddenLineAlpha, kPrimEnableWireframe);
		g_prim.Draw(DebugLine(adjustedSafePosWs, idealCameraPosWs, kColorMagenta, 2.0f, attrib), Seconds(5.0f));
		g_prim.Draw(DebugSphere(adjustedSafePosWs, 0.03f, kColorGray, attrib), Seconds(5.0f));
		g_prim.Draw(DebugSphere(idealCameraPosWs, 0.02f, kColorGreen, attrib), Seconds(5.0f));
		g_prim.Draw(DebugSphere(finalCameraPosWs, 0.03f, kColorYellow, attrib), Seconds(5.0f));
		MsgCon("PREDICTED pull-in percent = %g\n", (float)pullInPercent);
	}
#endif

	return pullInPercent;
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::InitCollisionState(const CameraControl* pPrevCamera, Point_arg baseTargetPos, Point_arg arcCenterWs, Vector_arg cameraUpDirWs)
{
	const CameraControlStrafeBase* pPrevStrafeCam = CameraControlStrafeBase::FromProcess(pPrevCamera);
	if (pPrevCamera && (!pPrevStrafeCam || pPrevStrafeCam->m_pullInPercent < 0.99f) && !pPrevCamera->IsKindOf(SID("CameraControlZoom")) && GetBlendInfo().m_time != TimeFrameZero())
	{
		const Vector camDir = GameGlobalCameraDirection(GetPlayerIndex());
		const Locator camLoc = GameGlobalCameraLocator(GetPlayerIndex());

		const Point safePosWs = GetCameraSafePos();

		const Vector cameraDirWs = SafeNormalize(m_preparedTargetPos - camLoc.GetTranslation(), kZero);
		const Scalar test = Dot(cameraDirWs, m_preparedCameraDir);

		const Scalar pullInPercent = ComputePullInPercentFromCameraPosition(safePosWs, m_preparedCameraPos, m_preparedTargetPos, camLoc.GetTranslation(), CameraManager::GetGlobal(GetPlayerIndex()).GetNearPlaneDist());
		GAMEPLAY_ASSERT(IsFinite(pullInPercent));
		m_pullInPercent = MinMax01(pullInPercent);
	}
	else
	{
		m_pullInPercent = 1.0f;
	}

	m_collision.InitState(m_pullInPercent);
}

///-------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::UpdateNearPlane2(bool topControl, float defaultNearDist, float pullInPercent)
{
	if (!topControl ||
		IsPhotoMode())	// I dont want photo mode camera near plane dist to shrink to a very small sphere, and never increase again, it creats unwanted clippings.
	{
		return;
	}

	const NdGameObject* pFocusObj = m_hFocusObj.ToProcess();
	const Character* pPlayerChar = Character::FromProcess(pFocusObj);

	// adjust nearPlaneCloseFactor to account for near plane getting too close to player's collision capsules (the ones used for IsClippingCameraPlane())
	float nearPlaneCloseFactor = m_collision.m_nearPlaneCloseFactor;

	if (pPlayerChar && !pPlayerChar->IsSwimming())
	{
		const Vector cameraDirWs = SafeNormalize(m_preparedTargetPos - m_preparedCameraPos, kZero);
		const Point adjustedSafePosWs = m_collision.GetFinalSafePos() - cameraDirWs * Scalar(m_collision.m_cameraNearDist);
		const Point finalCameraPosWs = Lerp(adjustedSafePosWs, m_preparedCameraPos, pullInPercent);

		float nearestApproach = kLargestFloatSqrt;
		pPlayerChar->IsClippingCameraPlane(false, &nearestApproach);
		nearestApproach = Min(nearestApproach, 2.0f); // beyond 2 _ it's irrelevant anyway, and to keep the spring from freaking out below...

		const float springConstIn = 100.0f;
		const float springConstOut = 2.0f;
		const bool springingIn = (nearestApproach < m_collision.m_nearestApproach);
		const float springConst = springingIn ? springConstIn : springConstOut;
		m_collision.m_nearestApproach = m_collision.m_nearestApproachSpring.Track(m_collision.m_nearestApproach, nearestApproach, m_preparedDt, springConst);

		nearPlaneCloseFactor = LerpScale(0.0f, defaultNearDist, 1.0f, m_collision.m_nearPlaneCloseFactor, (float)m_collision.m_nearestApproach);

		F32 pullInFactor = LerpScale(0.0f, 0.8f, 0.4f, 0.0f, pullInPercent);
		if (pPlayerChar->IsProne())
			pullInFactor = LerpScale(0.4f, 1.0f, 0.8f, 0.2f, pullInPercent);

		nearPlaneCloseFactor = Max(pullInFactor, nearPlaneCloseFactor);

		if (FALSE_IN_FINAL_BUILD(g_strafeCamDbgOptions.m_debugPullIn))
		{
			g_prim.Draw(DebugCross(m_collision.GetFinalSafePos(), 0.01f, 2.0f, kColorOrange, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCross(adjustedSafePosWs, 0.01f, 2.0f, kColorCyan, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);

			g_prim.Draw(DebugLine(finalCameraPosWs, cameraDirWs*m_collision.m_nearestApproach, kColorMagenta, 2.0f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
			StringBuilder<128> msg("pullIn: %.3f nearestApproach: %.3f nearPlaneFactor: %.3f -> %.3f", pullInPercent, (float)m_collision.m_nearestApproach, m_collision.m_nearPlaneCloseFactor, nearPlaneCloseFactor);
			g_prim.Draw(DebugString(adjustedSafePosWs, msg.c_str(), kColorCyan, g_msgOptions.m_conScale), kPrimDuration1FramePauseable);
		}
	}

	if (nearPlaneCloseFactor > 0.0f)
	{
		float nearPlaneDist = Lerp(defaultNearDist, 0.025f, MinMax01(nearPlaneCloseFactor));
		//MsgCon("NearDistPlane: %.2f\n", nearPlaneDist);
		//if (m_aProbe[kNumProbesCeiling+1].m_cc.m_time >= 0.0f)
		m_targetNearPlaneDist = nearPlaneDist;
	}
	else
	{
		//if (Abs(g_renderOptions.m_nearDist - defaultNearDist) > 0.0001f)
		m_targetNearPlaneDist = defaultNearDist;
	}
}

///-------------------------------------------------------------------------------------------///
float CameraControlStrafeBase::GetDefaultNearDist(const CameraControl* pPrevControl) const
{
	const DC::CameraStrafeBaseSettings* pSettings = GetSettings();
	const float defaultNearDist = pSettings->m_defaultNearDist;
	return defaultNearDist;
}

///-------------------------------------------------------------------------------------------///
bool ShouldDrawCollision(const CameraControlStrafeBase& self)
{
	const bool photoModeActive = EngineComponents::GetNdPhotoModeManager() && 
								EngineComponents::GetNdPhotoModeManager()->IsActive();

	return FALSE_IN_FINAL_BUILD((
									g_strafeCamDbgOptions.m_drawBlendedColl || 
									(!photoModeActive && &self == CameraManager::GetGlobal(self.GetPlayerIndex()).GetCurrentCamera()) || 
									(photoModeActive && self.IsPhotoMode())
								) && 
								g_strafeCamDbgOptions.m_drawT1Coll);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraControlStrafeBase::IsBlendControlledByScript() const
{
	return m_blendInfo.m_type == CameraBlendType::kScript;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraControlStrafeBase::IsFadeInByTravelDist() const
{
	return m_blendInfo.m_type == CameraBlendType::kTravelDist && m_blendInfo.m_fadeDist > 0.f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraControlStrafeBase::IsFadeInByDistToObj() const
{
	return m_blendInfo.m_type == CameraBlendType::kDistToObj && m_blendInfo.m_distCamParams.Valid();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraControlStrafeBase::IsDead(const CameraControl* pNextControl) const
{
	static const float kMinBlend = 1e-4f;

	// for zoom camera on the top of camera stack, we don't call Abandon() on it. But it self fade out, kill it.
	if (m_killWhenSelfFadedOut && 
		m_selfFadeOut.m_requested && 
		m_selfFadeOut.m_currFade < kMinBlend)
	{
		return true;
	}

	if (m_flags.m_killWhenBlendedOut)
	{
		if (IsFadeInByDistToObj())
		{
			if (m_normalBlend <= FLT_EPSILON && m_blend * m_distWeight <= FLT_EPSILON && // we might not need "m_blend * m_distWeight <= FLT_EPSILON"
				m_distToObj.m_lastFrameDist > 0.f && m_distToObj.m_lastFrameDist > m_blendInfo.m_distCamParams.m_startBlendInDist)
			{
				if (!pNextControl || (pNextControl && pNextControl->GetRank() != CameraRank::Debug))
				{
					// if this dist camera is not top camera, and player leaves it, kill it.
					return true;
				}
			}
		}
		else if (m_normalBlend < kMinBlend && m_blend > 1.0f - kMinBlend)
		{
			if (pNextControl && pNextControl->IsFadeInByTravelDist() && pNextControl->GetBlend() < 1.f - kMinBlend)
				return false;

			const bool isStrafeCam = IsKindOf(SID("CameraControlStrafeBase"));

			if (pNextControl && pNextControl->IsFadeInByDistToObj() && isStrafeCam)
				return false;

			const bool nextCamIsAutoGen		= (pNextControl && pNextControl->GetTypeNameId() == SID("CameraControlStrafeAutoGenerated"));
			const bool thisCameraIsStrafe	= (GetTypeNameId() == SID("CameraControlStrafe"));

			// keep this camera alive even if we push a zoom camera on top, so we can blend back to this strafe cam.
			if (thisCameraIsStrafe && !nextCamIsAutoGen &&														// This Strafe Camera is not a base of an AutoGen Camera
				GetPriority().GetClass() != CameraPriority::Class::kSpecial &&									// This camera is not a special camera
				pNextControl && pNextControl->GetPriority().GetClass() == CameraPriority::Class::kSpecial)		// Next camera is a Zoom Camera
			{
				// We keep this camera alive to better maintain the aim point (of next zoom camera).
				// But if the next zoom camera is weaponless, as an example ZoomCameras for the Look At Prompt(s)..
				//   then the maintain aim point makes no sense. There is no reason to keep this strafe camera alive.
				if (pNextControl->IsForAimingWeapon())
				{
					// Auto aim rotates the ZoomCamera so much that the maintain aim point logic can't always keep up with it.
					// So we keep this camera alive only when auto aim is turned off.
					const bool autoAimEnabled = IsAutoAimEnabledForZoomCamera(pNextControl);
					if (!autoAimEnabled)
					{
						// If player does not intend to change where Strafe Camera is, do not get rid of this StrafeCamera
						if (IsNearZero(m_arcDelta) && IsNearZero(m_yawControl.GetLastYawSpeed()))
							return false;
					}
				}
			}

			if (isStrafeCam)
			{
				bool hasFadeInByDistToObj = false;
				const CameraControl* pTestCamera = pNextControl;
				while (pTestCamera != nullptr)
				{
					if (pTestCamera->IsFadeInByDistToObj())
					{
						hasFadeInByDistToObj = true;
						break;
					}
					else if (pTestCamera->IsKindOf(SID("CameraControlStrafeBase")))
					{
						// stop at another camera which can serve as remap-base.
						break;
					}

					pTestCamera = CameraManager::GetGlobal(m_playerIndex).GetNextCameraNoLock(pTestCamera);
				}

				return !hasFadeInByDistToObj;
			}

			return true;
		}
	}

	if (m_flags.m_dontKillOnFocusDeath)
		return false;

	bool lostFocus = (m_flags.m_needsFocus && (m_hFocusObj.ToProcess() == nullptr));
	return lostFocus;
}

///--------------------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::UpdateWeightByDistToObj(const DistCameraParams& distCamParams)
{
	ASSERT(!GetRunWhenPaused());
	const Clock* pClock;
	if (GetRunWhenPaused())
		pClock = EngineComponents::GetNdFrameState()->GetClock(kCameraClock);
	else
		pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);

	float distWeight = 0.f;

	const NdGameObject* pDistObj = distCamParams.m_hDistObj.ToProcess();
	if (!pDistObj)
		return;

	float distToObj;
	if (m_distToObj.m_readyToDie)
	{
		distToObj = Max(0.f, m_distToObj.m_lastFrameDist - pClock->GetDeltaTimeInSeconds() * 1.f);	// assume player keep 1m/s speed.
	}
	else
	{
		const NdGameObject* pFocusGo = GetFocusObject().ToProcess();
		distToObj = pDistObj->GetDistBlendCameraDist(pFocusGo); // use 2d distance to be consistent with InteractControl::IsInCameraRemapRange
	}

	// update start-blend-in-dist and fully-blend-in-dist
	GAMEPLAY_ASSERT(distToObj >= 0.f);
	{
		float startBlendInDist = distCamParams.m_startBlendInDist;
		if (m_distToObj.m_startBlendInDist == -1.f)
		{
			m_distToObj.m_startBlendInDist = Min(startBlendInDist, distToObj);
		}
		else if (m_distToObj.m_startBlendInDist < startBlendInDist &&
			m_distToObj.m_lastFrameDist > 0.f && distToObj != m_distToObj.m_lastFrameDist) // only update start-blend-in-dist when player moved
		{
			m_distToObj.m_startBlendInDist = Min(distCamParams.m_startBlendInDist, m_distToObj.m_startBlendInDist + pClock->GetDeltaTimeInSeconds() * 1.f);
		}

		if (m_distToObj.m_fullyBlendInDist == -1.f)
		{
			m_distToObj.m_fullyBlendInDist = Min(distCamParams.m_fullyBlendInDist, distToObj);
		}
		else if (m_distToObj.m_fullyBlendInDist < distCamParams.m_fullyBlendInDist &&
			m_distToObj.m_lastFrameDist > 0.f && distToObj != m_distToObj.m_lastFrameDist) // only update start-blend-in-dist when player moved
		{
			m_distToObj.m_fullyBlendInDist = Min(distCamParams.m_fullyBlendInDist, m_distToObj.m_fullyBlendInDist + pClock->GetDeltaTimeInSeconds() * 1.f);
		}
	}

	if (m_nodeFlags.m_dead)
	{
		distWeight = 0.f;
	}
	else if (distToObj > 0.f)
	{
		if (distCamParams.m_immediate)
		{
			// immediately cut to final blend.
			distWeight = LerpScaleClamp(distCamParams.m_startBlendInDist, distCamParams.m_fullyBlendInDist, 0.f, 1.f, distToObj);
		}
		else
		{
			// use startBlendInDist - 0.1f to smooth initial camera value when camera start
			//distWeight = Ease(LerpScaleClamp(startBlendInDist - 0.1f, fullyBlendInDist, 0.f, 1.f, distToObj) * timeFactor);
			if (distToObj < m_distToObj.m_fullyBlendInDist)
				distWeight = 1.f;
			else if (distToObj > m_distToObj.m_startBlendInDist)
				distWeight = 0.f;
			else
				distWeight = LerpScaleClamp(m_distToObj.m_startBlendInDist, m_distToObj.m_fullyBlendInDist, 0.f, 1.f, distToObj);
		}
	}

	if (distCamParams.m_maxBlendInValue > 0.f && distCamParams.m_maxBlendInValue <= 1.f)
		m_distWeight = MinMax(distWeight, 0.f, distCamParams.m_maxBlendInValue);
	else
		m_distWeight = MinMax01(distWeight);

	m_distToObj.m_lastFrameDist = distToObj;
}

///--------------------------------------------------------------------------------------------------------///
// TODO: pass in target travel dist instead of blend.
void CameraControlStrafeBase::UpdateBlendByTravelDist(float fadeStartDist, float fadeDist/*, TimeFrame timeToDist, float targetBlendToDist*/)
{
	ASSERT(!GetRunWhenPaused());
	const Clock* pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);
	bool paused = pClock->IsPaused();

	float newBlend = 0.f;
	const NdGameObject* pFocusGo = GetFocusObject().ToProcess();

	//if (timeToDist > Seconds(0.f))
	//{
	//	TimeFrame elapsedTime = pClock->GetTimePassed(GetStartTime());
	//	if (elapsedTime <= timeToDist)
	//	{
	//		m_blendFromTimeToDistProgress = (elapsedTime.ToSeconds() / timeToDist.ToSeconds());
	//		m_focusTargetTravelDist = m_blendFromTimeToDistProgress * targetBlendToDist * fadeDist;
	//	}
	//	else
	//	{
	//		m_focusTargetTravelDist += Dist(pFocusGo->GetTranslation(), m_lastFrameFocusPos);
	//		m_blendFromTimeToDistProgress = 1.f;
	//	}
	//}
	//else

	//allow focus object to control behavior
	if (const NdLocatableObject* pLocatable = NdLocatableObject::FromProcess(pFocusGo))
	{
		pLocatable->UpdateFocusTargetTravelDist(m_focusTargetTravelDist, m_lastFrameFocusBf);
	}
	else
	{
		//default behavior
		if (pFocusGo->GetBinding().IsSameBinding(m_lastFrameFocusBf.GetBinding()))
			m_focusTargetTravelDist += Dist(pFocusGo->GetTranslationPs(), m_lastFrameFocusBf.GetTranslationPs());
		m_lastFrameFocusBf = pFocusGo->GetBoundFrame();
		//m_blendFromTimeToDistProgress = 1.f;
	}

	if (m_blendInfo.m_instantBlend)
	{
		m_focusTargetTravelDist = Max(m_focusTargetTravelDist, fadeDist);
	}

	if (m_nodeFlags.m_dead)
		newBlend = 0.f;
	else if (fadeDist == 0.f)
		newBlend = 1.f;	// prevent divided by zero.
	else if (m_focusTargetTravelDist > 0.f && fadeDist > 0.f)
	{
		if (m_focusTargetTravelDist <= fadeStartDist)
			newBlend = 0.f;
		else
			newBlend = Ease(MinMax01((m_focusTargetTravelDist - fadeStartDist) / fadeDist));
	}

	m_blend = MinMax01(newBlend);
}

///--------------------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::UpdateBlend(const CameraControl* pPrevControl, const CameraNode* pNextNode)
{
	const NdGameObject* pFocusObj = GetFocusObject().ToProcess();

	float prevNormalBlend = m_normalBlend;

	const bool photoModeActive = EngineComponents::GetNdPhotoModeManager() && EngineComponents::GetNdPhotoModeManager()->IsActive();
	if (pFocusObj != nullptr && IsBlendControlledByScript())
	{
		const CameraControl* pNextControl = pNextNode ? pNextNode->m_handle.ToProcess() : nullptr;
		UpdateBlendByScript(pNextControl);
	}
	else if (pFocusObj != nullptr && IsFadeInByTravelDist() && !photoModeActive)
	{
		UpdateBlendByTravelDist(m_blendInfo.m_fadeStartDist, m_blendInfo.m_fadeDist/*, m_blendInfo.m_timeToDist, m_blendInfo.m_targetBlendToDist*/);
	}
	else
	{
		UpdateBlendByTime(pPrevControl, m_blendInfo.m_distToBlendSpeedMult);
	}

	if (pFocusObj != nullptr && IsFadeInByDistToObj() && !photoModeActive)
	{
		UpdateWeightByDistToObj(m_blendInfo.m_distCamParams);
	}

	// update location blend
	UpdateLocationBlendByTime();

	UpdateSelfFade();
	m_blend *= m_selfFadeOut.m_currFade;
	m_blend = MinMax01(m_blend);
}

///--------------------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::UpdateNormalBlend(float contributeBlend, const CameraNode* pNextNode, float contributeDistWeight)
{
	ParentClass::UpdateNormalBlend(contributeBlend, pNextNode, contributeDistWeight);

	if (m_blendInfo.m_type == CameraBlendType::kDistToObj)
	{
		const CameraControl* pNextControl = pNextNode ? pNextNode->m_handle.ToProcess() : nullptr;

		static const float kMinBlend = 1e-4f;
		// don't fade out myself if a new camera is a zoom camera.
		if (pNextControl != nullptr)
		{
			// don't fade out if next camera is zoom/manual/stick camera.
			if (//pNextControl->GetPriority().GetClass() != CameraPriority::Class::kSpecial &&
				pNextControl->GetRank() != CameraRank::Debug &&
				!pNextControl->IsSelfFadeOutRequested())
			{
				if (m_distToObj.m_readyToDie)
				{
					// once there's a newer camera, we can start fade out this camera.
					ResetToTimeBasedBlend();
				}
				// if there's an animated-camera on top, don't fade out myself until animated-camera fully blends in
				else if (!m_distToObj.m_readyToDie && m_blend > 1.f - kMinBlend && contributeBlend < kMinBlend)
				{
					m_distToObj.m_readyToDie = true;
				}
			}
		}
	}
	else if (m_blendInfo.m_type == CameraBlendType::kTravelDist)
	{
		const CameraControl* pNextControl = pNextNode ? pNextNode->m_handle.ToProcess() : nullptr;

		static const float kMinBlend = 1e-4f;
		if (m_blend == 1.0f)
		{
			// when dist camera is fully blended in, it can become a fully blended in time camera, so we can kill it safely
			ResetToTimeBasedBlend();
		}
		else if (pNextControl)
		{
			// when there's a next camera, and this dist camera is not contributing anymore
			// try convert to a time-based blend, so we can kill it safely
			if (pNextControl->GetRank() != CameraRank::Debug && contributeBlend < kMinBlend)
			{
				// But only if the new camera is NOT zoom camera
				//  because a ZoomCamera can blend out into me
				if (pNextControl->GetPriority().GetClass() != CameraPriority::Class::kSpecial)
				{
					ResetToTimeBasedBlend();
				}
			}
		}
	}

	// DO we still need this?
	if (m_normalBlend == 0.f && m_blendInfo.m_type == CameraBlendType::kTravelDist)
	{
		// revisit the design after demo: it should be, if there's a next camera, and current dist camera is not contributing, fade out myself.
		bool nextCameraIsFadeByDist = false;
		const CameraNode* nextNode = pNextNode;
		while (nextNode != nullptr)
		{
			const CameraControl* nextControl = nextNode->m_handle.ToProcess();
			if (!nextControl)
				break;

			if (nextControl && nextControl->IsFadeInByTravelDist())
			{
				nextCameraIsFadeByDist = true;
				break;
			}
			nextNode = nextNode->Next();
		}

		// fade out non-contributing dist blend camera
		if (nextCameraIsFadeByDist)
		{
			ResetToTimeBasedBlend();
		}
	}
}

///--------------------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::ResetToTimeBasedBlend()
{
	ParentClass::ResetToTimeBasedBlend();
	m_distWeight = -1.f;
}

///--------------------------------------------------------------------------------------------------------///
float CameraControlStrafeBase::GetPitchRad() const
{
	const Vector cameraDirXZ = m_yawControl.GetCurrentDirWs();
	const Vector cameraDir = CalcCameraDir(cameraDirXZ, m_arc);
	const float pitchRad = SafeAsin(cameraDir.Y() / Length(cameraDir));
	return pitchRad;
}

///--------------------------------------------------------------------------------------------------------///
Vector CameraControlStrafeBase::CalcCameraDir(Vector_arg cameraDirXZ, float arc) const
{
	const NdGameObject* pFocusGo = m_hFocusObj.ToProcess();
	if (!pFocusGo)
		return Vector(kUnitZAxis);

	// Get base target position (before vertical arc and all other adjustments).
	Point baseTargetPos = GetTargetPosition(pFocusGo);

	Point arcTargetPos = m_verticalArc.OffsetTargetPoint(baseTargetPos, cameraDirXZ, arc, kUnitYAxis);
	Point cameraPos = CalcCameraPosition(baseTargetPos, cameraDirXZ, arc);

	//Transform the parameters into the appropriate space
	{
		Locator cameraComputedSpace(baseTargetPos, kIdentity);
		Locator cameraSpace(baseTargetPos, QuatFromVectors(kUnitYAxis, GetCameraUpVector()));

		baseTargetPos = cameraSpace.TransformPoint(cameraComputedSpace.UntransformPoint(baseTargetPos));
		arcTargetPos = cameraSpace.TransformPoint(cameraComputedSpace.UntransformPoint(arcTargetPos));
		cameraPos = cameraSpace.TransformPoint(cameraComputedSpace.UntransformPoint(cameraPos));
	}

	Vector preparedCameraDir = SafeNormalize(arcTargetPos - cameraPos, kUnitZAxis);
	return preparedCameraDir;
}

///-----------------------------------------------------------------------------------------------///
float CameraControlStrafeBase::GetPitchSpeedDeg() const
{
	// calculate pitch speed.
	const Vector cameraDirXZ = m_yawControl.GetCurrentDirWs();
	const Vector prevCamDir = CalcCameraDir(cameraDirXZ, m_origArcForPitchSpeed);
	const Vector newCamDir = CalcCameraDir(cameraDirXZ, m_origArcForPitchSpeed + m_arcDelta);
	const float prevPitchRad = SafeAsin(prevCamDir.Y());
	const float newPitchRad = SafeAsin(newCamDir.Y());
	const float deltaPitchRad = newPitchRad - prevPitchRad; // don't take maintain-aim-point into account.
	float pitchSpeedRad = ToUPS(deltaPitchRad);
	return RADIANS_TO_DEGREES(pitchSpeedRad);
}

///--------------------------------------------------------------------------------------------------------///
void CameraControlStrafeBase::AdjustByPitchRad(float desiredPitchRad)
{
	const Vector cameraDirXZ = m_yawControl.GetCurrentDirWs();
	const float targetY = Sin(desiredPitchRad);
	const float xzLen = Sqrt(Max(1.f - Sqr(targetY), 0.f));
	const Vector targetDir = -cameraDirXZ * xzLen + Vector(kUnitYAxis) * targetY;

	Angle yawAnglePs;
	float newArc;
	bool valid = CalcYawAndArcForTargetDir(targetDir, &yawAnglePs, &newArc);
	if (valid)
	{
		m_arc = newArc;
	}
}