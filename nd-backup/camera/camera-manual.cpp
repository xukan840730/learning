/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/camera/camera-manual.h"

#include "corelib/math/mathutils.h"
#include "corelib/math/matrix3x3.h"
#include "corelib/util/angle.h"

#include "ndlib/camera/camera-final.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/io/joypad.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/nd-options.h"
#include "ndlib/ndphys/rigid-body-base.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/render/dev/render-options.h"
#include "ndlib/render/dev/surfer-probe.h"
#include "ndlib/render/display.h"

#include "gamelib/camera/camera-manager.h"
#include "gamelib/ndphys/rigid-body.h"

PROCESS_REGISTER(CameraControlManual, CameraControl);

CAMERA_REGISTER_RANKED(kCameraManual, CameraControlManual, CameraManualStartInfo, CameraPriority(CameraPriority::Class::kForced, CameraPriority::LevelMax - 1), CameraRank::Debug);
CAMERA_REGISTER_RANKED(kCameraManualBase, CameraControlManual, CameraManualStartInfo, CameraPriority::Class::kForced, CameraRank::Bottom);

bool g_cameraMovementOnMenu = true;  //!< Allows camera movements when menus are shown
bool g_disableManualCamera = false;
bool g_disableManualCameraJoypadMovement = false;
bool g_manualCameraInvertY = true;
bool g_manualCamAllowShakes = false;
bool g_manualCameraSurferCenterOverride = false;

/// --------------------------------------------------------------------------------------------------------------- ///
CameraControlManual::CameraControlManual() : CameraControl()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraBlendInfo CameraControlManual::CameraStart(const CameraStartInfo& baseStartInfo)
{
	const CameraManualStartInfo& startInfo = *PunPtr<const CameraManualStartInfo*>(&baseStartInfo);

	m_stayFocused = startInfo.m_stayFocused;
	m_twist = 0.0f;
	m_flags.m_runWhenPaused = true;
	m_flags.m_noDefaultDofBehavior = true;

	const Locator* pStartLoc = startInfo.GetLocator();
	const Point* pTargetPos = startInfo.GetTarget();

	if (pStartLoc && pTargetPos)
	{
		Point startPos = pStartLoc->GetTranslation();

		m_offset = Normalize(startPos - *pTargetPos);
		m_offset *= -1.f;
		m_offsetDist = Dist(startPos, *pTargetPos);
		m_targetPos = *pTargetPos;
		m_prevLocWs = m_prevLocPs = CameraLocation(startPos, *pTargetPos).GetLocator();
	}
	else
	{
		m_offset = g_mainCameraInfo[GetPlayerIndex()].GetDirection() * -1.f;
		m_offsetDist = 0.0f;
		m_targetPos = g_mainCameraInfo[GetPlayerIndex()].GetPosition(); //m_location.m_position;
		m_prevLocWs = m_prevLocPs = g_mainCameraInfo[GetPlayerIndex()].GetLocator();
	}

	m_camStickX = 0.0f;
	m_camStickY = 0.0f;
	m_moveStickX = 0.0f;
	m_moveStickY = 0.0f;
	m_moveStickYSpring.Reset();
	m_moveStickXSpring.Reset();
	m_camStickXSpring.Reset();
	m_camStickYSpring.Reset();

	m_cameraShake.Init(GetPlayerIndex(), g_mainCameraInfo[GetPlayerIndex()].GetPosition());
	m_handyCamBlend = 0.0f;
	m_handCamTracker.Reset();

	m_hasOrbitFocus = false;
	m_hasSavedOrbitFocus = false;
	m_mouseStart = false;
	m_prevManualCameraOrbit = false;

	if (g_cameraOptions.m_copyRunningCameraParametersIntoManualCamera)
	{
		g_cameraOptions.m_defaultFov = g_mainCameraInfo[GetPlayerIndex()].GetFov();
	}

	m_prevParentSpaceLocator = Locator(kIdentity);

	return CameraControl::CameraStart(baseStartInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator CameraControlManual::SetParentSpaceObject(const NdGameObject* pParentObject)
{
	Locator currentParentSpaceLoc(kIdentity);
	if (pParentObject)
	{
		while (pParentObject->GetBoundPlatform() != nullptr && pParentObject->GetBoundPlatform() != pParentObject)
		{
			pParentObject = pParentObject->GetBoundPlatform();
		}

		if (g_cameraOptions.m_useLocatorForManualCameraBind)
		{
			currentParentSpaceLoc = pParentObject->GetLocator();
		}
		else
		{
			currentParentSpaceLoc = pParentObject->GetParentSpace();
			if (const RigidBody* pParentRigidBody = pParentObject->GetDefaultBindTarget())
			{
				currentParentSpaceLoc = pParentRigidBody->GetLocatorCm();
			}
		}

		if (g_cameraOptions.m_onlyFollowPositionForManualCameraBind)
		{
			currentParentSpaceLoc.SetRotation(kIdentity);
		}
	}

	// If the parent space is different we need to modify our previous position as it is stored in parent space.
	if (m_attachedGameObject.ToProcess() != pParentObject)
	{
		// first revert back to world space.
		m_prevLocWs = m_prevParentSpaceLocator.TransformLocator(m_prevLocPs);

		m_prevLocPs = currentParentSpaceLoc.UntransformLocator(m_prevLocWs);
		m_attachedGameObject = pParentObject;

		m_prevParentSpaceLocator = currentParentSpaceLoc;
	}
	else if (pParentObject == nullptr && !IsClose(m_prevParentSpaceLocator, Locator(kIdentity), 0.001f, 0.999f))
	{
		m_prevLocWs = m_prevParentSpaceLocator.TransformLocator(m_prevLocPs);
		m_prevLocPs = m_prevLocWs;
		m_prevParentSpaceLocator = Locator(kIdentity);
	}

	return currentParentSpaceLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraLocation CameraControlManual::UpdateLocation(const CameraInstanceInfo& info)
{
	float angularSpeed = 1.0f;
	float horzTransSpeed = 6.0f;
	float vertTransSpeed = 4.0f;

	if (g_disableManualCamera)
		return CameraLocation(m_prevLocWs);

	int padIndex = CameraManager::GetGlobal(GetPlayerIndex()).GetInputPadIndex();

	if (!info.m_topControl && GetRank() == CameraRank::Bottom)
		return info.m_prevLocation;

	if (!g_renderOptions.IsSplitscreenMode())
	{
		EngineComponents::GetNdFrameState()->m_joypad[DC::kJoypadPadCamera2].SetSuppression(false, kSuppressionPriorityNet);
	}

	if (!info.m_topControl || GetRank() != CameraRank::Bottom)
	{
		if (padIndex == DC::kJoypadPadPlayer1)
		{
			if (!EngineComponents::GetNdFrameState()->m_joypad[padIndex].IsSuppressed() && !g_renderOptions.IsSplitscreenMode())
			{
				padIndex = DC::kJoypadPadCamera2;
			}
			else
			{
				padIndex = DC::kJoypadPadCamera1;
			}
		}
		else if (padIndex == DC::kJoypadPadPlayer2)
		{
			padIndex = DC::kJoypadPadCamera2;
		}
	}
	else
	{
		if (padIndex == DC::kJoypadPadPlayer1)
		{
			padIndex = DC::kJoypadPadReal1;
		}
		else if (padIndex == DC::kJoypadPadPlayer2)
		{
			padIndex = DC::kJoypadPadReal2;
		}
	}

	//MsgCon("padIndex = %d\n", padIndex);

	const Mouse& mouse = EngineComponents::GetNdFrameState()->m_mouse;
	const Joypad& joypad = EngineComponents::GetNdFrameState()->m_joypad[padIndex];
	Keyboard& keyboard = EngineComponents::GetNdFrameState()->m_keyboard;

	CameraJoypad campad(joypad);

	if (g_manualCameraInvertY)
		campad.m_stickCamera.y = -campad.m_stickCamera.y;
	else
		campad.m_stickCamera.y = campad.m_stickCamera.y;

	if (g_ndConfig.m_pDMenuMgr->IsProgPaused() && joypad.m_buttons & DC::kJoypadButtonX)
	{
		campad.m_stickMove.x = campad.m_stickMove.y = 0.0f;
	}

	const I32 clockId = g_ndOptions.m_dontSlowManualCamera ? kMenuClock : kRealClock;
	const float deltaTime = EngineComponents::GetNdFrameState()->GetClock(clockId)->GetDeltaTimeInSeconds();

	if (!g_cameraOptions.m_manualCameraInputSprings)
	{
		m_camStickX = campad.m_stickCamera.x;
		m_camStickY = campad.m_stickCamera.y;
		m_moveStickX = campad.m_stickMove.x;
		m_moveStickY = campad.m_stickMove.y;
	}
	else
	{
		m_camStickX = m_camStickXSpring.Track(m_camStickX, campad.m_stickCamera.x, deltaTime, 10.0f);
		m_camStickY = m_camStickYSpring.Track(m_camStickY, campad.m_stickCamera.y, deltaTime, 10.0f);
		m_moveStickX = m_moveStickXSpring.Track(m_moveStickX, campad.m_stickMove.x, deltaTime, 10.0f);
		m_moveStickY = m_moveStickYSpring.Track(m_moveStickY, campad.m_stickMove.y, deltaTime, 10.0f);
	}

	Vector manualPan = Vector(kZero);

	float manualPanX = 0;
	float manualPanY = 0;
	float manualPanZ = 0;

	float manualPanAbsX = 0;
	float manualPanAbsY = 0;
	float manualPanAbsZ = 0;

	bool manualCameraOrbit = g_cameraOptions.m_manualCameraOrbit;
	bool updateOrbitFocus = !m_hasOrbitFocus;
	bool manualMoveOrbitFocusPoint = false;
	bool focusOnObject = false;


	if (joypad.m_buttons & DC::kJoypadButtonCircle && g_cameraOptions.m_manualCameraOrbit)
	{
		// temporary disable the orbit camera
		manualCameraOrbit = false;
		manualMoveOrbitFocusPoint = true;
		if (g_surferProbeOptions.m_enabled)
		{
			g_manualCameraSurferCenterOverride = true;
		}
	}
	else
	{
		g_manualCameraSurferCenterOverride = false;
	}

	// It's a bit involved the logic to get a good feel for the Mouse and an orbit camera
	// The following are the conditions in which we capture (again) the orbit (focus) point
	// The user has override previously the orbit mode and moved freely, or
	// the user has selected a new focus point
	if (m_prevManualCameraOrbit == false ||
		g_cameraOptions.m_manualCameraFocusMouseOnSurferProbe ||
		(((mouse.m_buttonsReleased == kMouseButton_Left) && (mouse.m_buttons & kMouseKeyAlt)) ||
		 (mouse.m_buttonsPressed == (kMouseButton_Left | kMouseButton_Middle))))
	{
		updateOrbitFocus = true;
	}

	if (g_cameraOptions.m_manualCameraFocusMouseOnSurferProbe)
	{
		manualCameraOrbit = true;
		focusOnObject = true;
		g_cameraOptions.m_manualCameraFocusMouseOnSurferProbe = false;
	}
	else if (FALSE_IN_FINAL_BUILD(g_cameraOptions.m_orbitManualCameraOnSelection))
	{
		manualCameraOrbit = true;
		focusOnObject = true;
		updateOrbitFocus = true;
	}

	m_prevManualCameraOrbit = manualCameraOrbit;

	float orbitDistanceDelta = 0;

	if (manualCameraOrbit)
	{
		m_camStickX = -campad.m_stickCamera.x;
		m_camStickY = -campad.m_stickCamera.y;

		orbitDistanceDelta += .5f * campad.m_stickMove.y;
	}


	const NdGameObject* pCurrentParentObject = m_attachedGameObject.ToProcess();
	if (g_cameraOptions.m_bindManualCameraToSameObjectAsPlayer)
	{
		NdGameObject* pPlayer = CameraManager::Get().GetDefaultFocusObject();
		if (pPlayer)
		{
			const RigidBody* pParentBody = pPlayer->GetBoundRigidBody();
			pCurrentParentObject		 = pParentBody ? pParentBody->GetOwner() : nullptr;
		}
	}
	else if (g_cameraOptions.m_bindManualCameraSelObjectParent)
	{
		if (const NdGameObject* pSelObj = DebugSelection::Get().GetSelectedGameObject())
		{
			const RigidBody* pParentBody = pSelObj->GetBoundRigidBody();
			pCurrentParentObject		 = pParentBody ? pParentBody->GetOwner() : nullptr;
		}
	}
	else
	{
		pCurrentParentObject = nullptr;
	}

	if (g_cameraOptions.m_bindManualCameraToPlayer)
	{
		NdGameObject* pPlayer = CameraManager::Get().GetDefaultFocusObject();
		if (pPlayer)
		{
			pCurrentParentObject = pPlayer;
		}
	}

	if (g_cameraOptions.m_bindManualCameraToSelectedGameObject)
	{
		pCurrentParentObject = DebugSelection::Get().GetSelectedGameObject();
	}
	else if (g_cameraOptions.m_unbindManualCameraFromGameObject)
	{
		pCurrentParentObject = nullptr;
	}

	// Always reset these
	/*	g_cameraOptions.m_bindManualCameraToSelectedGameObject = false;
		g_cameraOptions.m_unbindManualCameraFromGameObject = false;*/

	Locator currentParentSpaceLoc = SetParentSpaceObject(pCurrentParentObject);

	if (manualCameraOrbit && updateOrbitFocus)
	{
		const NdGameObject* pOrbitObj;

		if (FALSE_IN_FINAL_BUILD(g_surferProbeOptions.m_enabled))
		{
			if (IsFinite(g_surferProbe.GetPosition()))
			{
				m_orbitFocus = g_surferProbe.GetPosition();
				m_orbitFocusStart = m_orbitFocus;
				m_orbitDistance	= Length(m_prevLocPs.Pos() - m_orbitFocus);
			}

			if (focusOnObject && m_orbitDistance > 5.0)
			{
				m_orbitDistance = 5.0;
			}
		}
		else if (FALSE_IN_FINAL_BUILD(g_cameraOptions.m_orbitManualCameraOnSelection)
			      && (pOrbitObj = DebugSelection::Get().GetSelectedGameObject()))
		{
			m_orbitFocus = pOrbitObj->GetBoundingSphere().GetCenter();

			if (!IsFinite(m_orbitFocus))
			{
				m_orbitFocus = kOrigin;
			}

			m_orbitFocusStart = m_orbitFocus;
			m_orbitDistance	= Length(m_prevLocPs.Pos() - m_orbitFocus);

			if (focusOnObject && (m_orbitDistance > 5.0))
			{
				m_orbitDistance = 5.0;
			}
		}
		else // default mode
		{
			bool shouldRestoreOrbitFocus = m_hasSavedOrbitFocus;
			if (shouldRestoreOrbitFocus)
			{
				// orbit about previous point
				m_orbitFocus = m_savedOrbitFocus;
				m_orbitDistance = Length(m_prevLocWs.GetPosition() - m_orbitFocus);
			}
			else if (pCurrentParentObject)
			{  // not sure how this is used, the restore point above will often take priority
				m_orbitDistance		 = Length(currentParentSpaceLoc.Pos() - m_prevLocPs.Pos());
				m_orbitFocus		 = m_prevLocPs.Pos() + m_orbitDistance * GetLocalZ(m_prevLocPs.Rot());
			}
			else
			{   // Mark a center
				m_orbitDistance		 = 5;
				m_orbitFocus		 = m_prevLocPs.Pos() + m_orbitDistance * GetLocalZ(m_prevLocPs.Rot());
			}
			m_orbitFocusStart   = m_orbitFocus;
			m_orbitDistanceStart = m_orbitDistance;
		}
		m_hasOrbitFocus = true;
	}

	const Vector currentFwdDirPs(GetLocalZ(m_prevLocPs.Rot()));

	float scalar = 1.0;
	const float fMaxY = 0.999f;

	// Get the camera axis
	const Vector vLeftPs = GetLocalX(m_prevLocPs.Rot());
	const Vector vUpPs   = GetLocalY(m_prevLocPs.Rot());
	const Vector vDirPs  = GetLocalZ(m_prevLocPs.Rot());

	const Vector vLeftAbs = Vector(vLeftPs.X(), 0.0f, vLeftPs.Z());
	const Vector vDirAbs  = Vector(vDirPs.X(), 0.0f, vDirPs.Z());
	const Vector vUpAbs   = Vector(kUnitYAxis);

	Vector eulerAngles;
	QuaternionToEulerAngles(&eulerAngles, m_prevLocPs.Rot());

	// pad buttons speed up or slow down manual camera
	U32 buttons = joypad.m_buttons & (DC::kJoypadButtonL2 | DC::kJoypadButtonR2);
	switch (buttons)
	{
	case DC::kJoypadButtonL2 | DC::kJoypadButtonR2:
		scalar *= 25.0f;
		angularSpeed = 1.5f;
		break;
	case DC::kJoypadButtonL2:
		scalar *= 5.0f;
		angularSpeed = 1.5f;
		break;
	case DC::kJoypadButtonR2:
		m_lastFastTime = GetProcessClock()->GetCurTime();
		scalar *= 0.25f;
		break;
	default:
		m_lastFastTime = GetProcessClock()->GetCurTime();
		break;
	}

	horzTransSpeed *= scalar * g_cameraOptions.m_manualCamSpeedMult;
	vertTransSpeed *= scalar * g_cameraOptions.m_manualCamSpeedMult * g_cameraOptions.m_manualCamVertSpeedMult;

	DMENU::MenuGroup* pDevMenu = g_ndConfig.m_pDMenuMgr->GetMenu(DMENU::kDevMenu);

	// Don't allow camera movement if the dev menu has an item that grabbed the joypad!
	if ((pDevMenu->IsOpen() && (!g_cameraMovementOnMenu || pDevMenu->m_pJoypadItem)) || g_disableManualCameraJoypadMovement)
	{
		horzTransSpeed = 0.0f;
		vertTransSpeed = 0.0f;
		angularSpeed = 0.0f;
	}

	// Camera control with stick
	float azDelta = -m_camStickX * angularSpeed * deltaTime * 180.0f;
	float elDelta = m_camStickY * angularSpeed * deltaTime * 90.0f;
	float dollyDelta = (-m_moveStickY * deltaTime * horzTransSpeed);

	float panLeft = 0.0f;
	float panUp = 0.0f;
	float panDir = 0.0f;
	float panLeftAbs = 0.0f;
	float panDirAbs = 0.0f;
	float panUpAbs = 0.0f;

	if ((joypad.m_buttons & DC::kJoypadButtonR1) && (!pDevMenu->IsOpen() || g_cameraMovementOnMenu))
	{
		panUp += deltaTime * vertTransSpeed;
	}
	if ((joypad.m_buttons & DC::kJoypadButtonL1) && (!pDevMenu->IsOpen() || g_cameraMovementOnMenu))
	{
		panUp -= deltaTime * vertTransSpeed;
	}

	panLeft += (-m_moveStickX * deltaTime * horzTransSpeed);

	Vector sourceMovePs = Vector(kZero);

	if (m_stayFocused)
		m_offsetDist = Max(0.5f, m_offsetDist - dollyDelta);
	else
		sourceMovePs += (GetLocalZ(m_prevLocPs.Rot()) * dollyDelta);

	bool finalizeMovement = false;
	bool absoluteZoom = false;
	bool print = false;

	if (g_cameraOptions.m_manualCameraMouse)
	{
		if ((mouse.m_buttonsPressed & (kMouseButton_Left | kMouseButton_Middle | kMouseButton_Right)) || mouse.m_wheel != 0)
		{
			if (!m_mouseStart)
			{
				m_startEulerAngles   = eulerAngles;
				m_mouseStart		 = true;
				m_startPosition		 = mouse.m_position;
				m_mousePrevLocPs	 = m_prevLocPs;
				m_lastPan			 = Vector(kZero);
				m_orbitDistanceStart = m_orbitDistance;
			}
		}
		bool hasMoved   = false;
		bool hasRotated = false;
		if ((m_mouseStart && (mouse.m_buttons & (kMouseButton_Left | kMouseButton_Middle | kMouseButton_Right))) || mouse.m_wheel != 0)
		{
			// drag
			Vec2  diff		 = m_startPosition - mouse.m_position;
			USize screenSize = g_display.GetDisplayUSize();
			diff			 = Vec2(diff.x / (float)screenSize.m_w, diff.y / (float)screenSize.m_h);

			if (mouse.m_buttons & kMouseButton_Left)
			{
				if (manualCameraOrbit)
				{
					diff = -diff;
				}
				azDelta  = -diff.x * 360.0f;
				elDelta  = diff.y * 180.0f;
				hasMoved = hasRotated = true;
				print				  = true;
			}
			if (mouse.m_buttons & kMouseButton_Middle)
			{
				float panHor = (manualCameraOrbit) ? m_orbitDistance : 5.0;
				float panVer = (manualCameraOrbit) ? m_orbitDistance : 9.0;
				panLeft		 = -panVer * diff.x;
				panUp		 = -panHor * diff.y;
				manualPan	= Vector(panLeft, panUp, 0);
				hasMoved = hasRotated = true;
				print				  = true;
			}
			if (mouse.m_buttons & kMouseButton_Right)
			{
				sourceMovePs += (GetLocalZ(m_prevLocPs.Rot()) * 20.0f * -diff.y);
				manualPan += Vector(0, 0, 20.0f * -diff.y);
				if (manualCameraOrbit)
				{
					orbitDistanceDelta += diff.y;
					manualPan	= Vector(kZero);
					sourceMovePs = Vector(kZero);
				}
				hasMoved = hasRotated = true;
				print				  = true;
				absoluteZoom		  = true;
			}
			if (mouse.m_wheel != 0)
			{
				float mouseWheel = g_cameraOptions.m_manualCameraWheelSpeed * mouse.m_wheel;
				sourceMovePs += (GetLocalZ(m_prevLocPs.Rot()) * mouseWheel);
				manualPan += Vector(0, 0, mouseWheel);
				if (manualCameraOrbit)
				{
					orbitDistanceDelta += mouseWheel / 2.0;
					manualPan	= Vector(kZero);
					sourceMovePs = Vector(kZero);
				}

				hasMoved = hasRotated = true;
				print				  = true;
				finalizeMovement	  = true;
			}
		}
		if (mouse.m_buttonsReleased || finalizeMovement)
		{
			m_mouseStart = false;
		}

		float panSpeed = g_cameraOptions.m_manualCameraPanSpeed;
		if (keyboard.IsDown(Keyboard::UP) || g_cameraOptions.m_manualCameraForward)
		{
			g_cameraOptions.m_manualCameraForward = false;
			if (manualCameraOrbit)
			{
				hasMoved = true;
				panDirAbs += panSpeed;
			}
			else
			{
				if (g_cameraOptions.m_manualCameraKeyboardMoveOnPlane)
					panDirAbs += panSpeed;
				else
					panDir += panSpeed;
			}
			finalizeMovement = true;
			print			 = true;
		}
		if (keyboard.IsDown(Keyboard::DOWN) || g_cameraOptions.m_manualCameraBackward)
		{
			g_cameraOptions.m_manualCameraBackward = false;
			if (manualCameraOrbit)
			{
				hasMoved = true;
				panDirAbs += -panSpeed;
			}
			else
			{
				if (g_cameraOptions.m_manualCameraKeyboardMoveOnPlane)
					panDirAbs += -panSpeed;
				else
					panDir += -panSpeed;
			}
			finalizeMovement = true;
			print			 = true;
		}
		if (keyboard.IsDown(Keyboard::LEFT) || g_cameraOptions.m_manualCameraLeft)
		{
			g_cameraOptions.m_manualCameraLeft = false;

			if (manualCameraOrbit)
			{
				hasMoved = true;
				if (g_cameraOptions.m_manualCameraKeyboardMoveOnPlane)
					manualPanX += panSpeed;
				else
					panLeftAbs += panSpeed;
			}
			else
			{
				if (g_cameraOptions.m_manualCameraKeyboardMoveOnPlane)
					panLeftAbs += panSpeed;
				else
					panLeft += panSpeed;
			}
			finalizeMovement = true;
			print			 = true;
		}
		if (keyboard.IsDown(Keyboard::RIGHT) || g_cameraOptions.m_manualCameraRight)
		{
			g_cameraOptions.m_manualCameraRight = false;
			if (manualCameraOrbit)
			{
				hasMoved = true;
				if (g_cameraOptions.m_manualCameraKeyboardMoveOnPlane)
					manualPanX += -panSpeed;
				else
					panLeftAbs += -panSpeed;
			}
			else
			{
				if (g_cameraOptions.m_manualCameraKeyboardMoveOnPlane)
					panLeftAbs += -panSpeed;
				else
					panLeft += -panSpeed;
			}
			finalizeMovement = true;
			print			 = true;
		}

		if (keyboard.IsDown(Keyboard::PGUP))
		{
			if (manualCameraOrbit)
			{
				hasMoved = true;
				manualPanY += panSpeed;
			}
			else
			{
				panUpAbs += panSpeed;
			}
			finalizeMovement = true;
			print			 = true;
		}
		if (keyboard.IsDown(Keyboard::PGDOWN))
		{
			if (manualCameraOrbit)
			{
				hasMoved = true;
				manualPanY += -panSpeed;
			}
			else
			{
				panUpAbs += -panSpeed;
			}
			finalizeMovement = true;
			print			 = true;
		}

		manualPan += Vector(manualPanX, manualPanY, manualPanZ);

		if (hasRotated)
		{
			eulerAngles = m_startEulerAngles;
		}
		if (hasMoved)
		{
			m_prevLocPs = m_mousePrevLocPs;
			manualPan   = vLeftPs * manualPan.X() + vUpPs * manualPan.Y() + vDirPs * manualPan.Z() + (vLeftAbs * panLeftAbs) + (vUpAbs * panUpAbs) + (vDirAbs * panDirAbs);
			m_lastPan   = manualPan;
		}
	}

	sourceMovePs += (vLeftPs * panLeft) + (vUpPs * panUp) + (vDirPs * panDir) + (vLeftAbs * panLeftAbs) + (vUpAbs * panUpAbs) + (vDirAbs * panDirAbs);

	// In degrees...
	float newEl = MinMax(RADIANS_TO_DEGREES((float)eulerAngles.Get(0)) + elDelta, -85.0f, 85.0f);
	float newAz = RADIANS_TO_DEGREES(eulerAngles.Get(1)) + azDelta;

	/*
	// Snap elevation to 0-degrees
	if(joypad.m_buttons & DC::kJoypadButtonR3)
	{
	newEl = 0.f;
	}
	*/

	// Calculate the new forward direction based on the given azimuth and elevation
	Quat qaz, qel;
	qaz = QuatFromAxisAngle(kUnitYAxis, DEGREES_TO_RADIANS(newAz));
	qel = QuatFromAxisAngle(kUnitXAxis, DEGREES_TO_RADIANS(newEl));

	Vector newFwdDirPs = Rotate(qel, Vector(kUnitZAxis));

	newFwdDirPs = Rotate(qaz, newFwdDirPs);

	if (g_cameraOptions.m_enableSpringForManualCameraBind)
	{
		Point newPos = m_prevParentSpaceLocator.GetPosition() + (currentParentSpaceLoc.GetPosition() - m_prevParentSpaceLocator.GetPosition()) * g_cameraOptions.m_springStrenghtForManualCameraBind;
		Quat newRot = Slerp(m_prevParentSpaceLocator.GetRotation(), currentParentSpaceLoc.GetRotation(), g_cameraOptions.m_springStrenghtForManualCameraBind);

		currentParentSpaceLoc.SetPosition(newPos);
		currentParentSpaceLoc.SetRotation(newRot);
		m_prevParentSpaceLocator = currentParentSpaceLoc;
	}

	if (manualCameraOrbit)
	{
		Vector dir = Normalize(-newFwdDirPs);
		Point orbitCenterLS = currentParentSpaceLoc.UntransformPoint(m_orbitFocus);

		Point newPoint = orbitCenterLS + m_orbitDistance * dir;
		sourceMovePs = (newPoint - m_prevLocPs.Pos());

		// The delta distance is a percentage of orignal orbit distance
		// This is because it 'feels' better to do small movements when the distance is small, and large movements when the distance is large
		if (absoluteZoom)
		{
			m_orbitDistance = m_orbitDistanceStart + orbitDistanceDelta * (m_orbitDistanceStart * 2.0);
		}
		else
		{
			m_orbitDistance += orbitDistanceDelta * (m_orbitDistance / 5.0);
		}

		// Clamp dolly move
		m_orbitDistance = Clamp(m_orbitDistance, CameraManager::GetGlobal(GetPlayerIndex()).GetNearPlaneDist(), 5000.0f);

		m_orbitFocus = m_orbitFocusStart;
		m_orbitFocus += manualPan;

		// Recenter the focus point if we are done moving
		if (mouse.m_buttonsReleased || finalizeMovement)
		{
			m_orbitFocusStart = m_orbitFocus + m_lastPan;
			m_orbitFocus = m_orbitFocusStart;
		}

	}

	if (!IsFinite(m_prevLocPs.Pos()))
	{
		m_prevLocPs.SetPosition(kOrigin);
	}

	if (!IsFinite(sourceMovePs))
	{
		sourceMovePs = kZero;
	}

	// start with the position and target
	const Point newSourcePs = m_prevLocPs.Pos() + sourceMovePs;
	Point newSourceWs = currentParentSpaceLoc.TransformPoint(newSourcePs);
	Vector newFwdDirWs = currentParentSpaceLoc.TransformVector(newFwdDirPs);
	Vector upWs = currentParentSpaceLoc.TransformVector(Vector(kUnitYAxis));

	// Keep the orientation such that the y-axis is up... even when attached to an object in
	// which case 'up' is parent space 'up' and not world space 'up'.
	CameraLocation camLocation = CameraLocation(newSourceWs, newFwdDirWs, upWs);

	// set the twist of the camera
	if (g_cameraOptions.m_allowManualCameraTwist)
	{
		if (!g_ndConfig.m_pDMenuMgr->IsMenuActive() && (info.m_normalBlend == 1.f))
		{
			if (joypad.m_buttons & DC::kJoypadButtonCircle)
			{
				m_twist += 90.0f * deltaTime;
			}
			else if (joypad.m_buttons & DC::kJoypadButtonSquare)
			{
				m_twist -= 90.0f * deltaTime;
			}
			else if (joypad.m_buttons & (DC::kJoypadButtonTriangle | DC::kJoypadButtonX))
			{
				m_twist = 0.0f;
			}
		}

		Locator loc = camLocation.GetLocator();
		loc.SetRot(QuatFromAxisAngle(GetLocalZ(camLocation.GetLocator().Rot()), DEGREES_TO_RADIANS(m_twist)) * camLocation.GetLocator().Rot());

		camLocation = CameraLocation(loc);
	}


	m_prevLocWs = camLocation.GetLocator();
	m_prevLocPs = currentParentSpaceLoc.UntransformLocator(m_prevLocWs);

	if (g_manualCamAllowShakes)
	{
		camLocation = CameraLocation(m_cameraShake.GetLocator(GetPlayerIndex(), camLocation.GetLocator(), 1.0f));
	}

	if (!IsFinite(camLocation.GetLocator()))
	{
		camLocation.SetLocator(Locator(kOrigin));
	}

	bool isRootCamera = info.m_stackIndex == 0;
	bool shouldUpdateOrbitFocus = (!isRootCamera) && (manualMoveOrbitFocusPoint);
	bool shouldDrawOrbitFocus = (!isRootCamera) && (manualMoveOrbitFocusPoint || g_cameraOptions.m_debugDrawFocusPoint);
	if (FALSE_IN_FINAL_BUILD(shouldUpdateOrbitFocus))
	{
		if (g_manualCameraSurferCenterOverride)
		{ // Use the surfer probe to update the focus. Duplicated from above, but for when we are not in orbitMode
			if (IsFinite(g_surferProbe.GetPosition()))
			{
				m_orbitFocus = g_surferProbe.GetPosition();
				m_orbitFocusStart = m_orbitFocus;
				m_orbitDistance	= Length(m_prevLocPs.Pos() - m_orbitFocus);
			}
			if (focusOnObject && m_orbitDistance > 5.0)
			{
				m_orbitDistance = 5.0;
			}
		}
		else
		{ // update orbit focus based on camera position
			m_orbitFocus = newSourceWs + newFwdDirWs * m_orbitDistance;
			m_orbitFocusStart = m_orbitFocus;
		}
		m_hasOrbitFocus = true;
		m_prevManualCameraOrbit = true;
	}
	if (FALSE_IN_FINAL_BUILD(g_cameraOptions.m_manualCameraOrbit && m_hasOrbitFocus))
	{
		m_savedOrbitFocus = m_orbitFocus;
		m_hasSavedOrbitFocus = true;
	}
	if (FALSE_IN_FINAL_BUILD(shouldDrawOrbitFocus))
	{   // draw the orbit focus point
		float radius = 0.03;
		PrimAttrib debugDisplayAttrib;
		debugDisplayAttrib.SetHiddenLineAlpha(true);
		debugDisplayAttrib.SetWireframe(true);
		g_prim.DrawNoHide(DebugSphere(m_orbitFocus, radius, kColorCyan, debugDisplayAttrib));
	}

	ALWAYS_ASSERT(IsFinite(camLocation.GetLocator()));

	camLocation.SetFov(g_cameraOptions.m_defaultFov);

	return camLocation;
}



