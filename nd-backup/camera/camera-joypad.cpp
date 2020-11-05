/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/camera/camera-joypad.h"
#include "gamelib/gameplay/nd-player-joypad.h"
#include "gamelib/render/gui2/menu2-page-stack.h"
#include "gamelib/render/gui2/menu2-root.h"

#include "ndlib/nd-game-info.h"
#include "ndlib/scriptx/h/joypad-defines-internal.h"

/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///

CameraJoypad::CameraJoypad (const Joypad& joypad)
{
	m_stickMove.Zero();
	m_stickCamera.Zero();
	m_rawStickMove.Zero();
	m_rawStickCamera.Zero();
	m_rawGyroVelRad = Vec3(kZero);
	m_stickScale = 1.0f;
	m_downDPadPressedWithRepeat = false;
	m_fromLookAtControl = false;

	bool cheating = joypad.IsCommandActive(SID("debug-cheat"), true) && !DebugFlyDisable();

	if (cheating || !joypad.IsCommandDisabled(SID("move")))
	{
		const DC::JoypadCommand* pCommand = joypad.GetCommand(SID("move"));
		if (pCommand)
		{
			joypad.GetDeadZoneAdjustedStick(&m_stickMove, 0.2f, 1.0f, pCommand->m_stick.m_type);

			DC::JoypadStick useStick = pCommand->m_stick.m_type;
			// if (EngineComponents::GetNdGameInfo()->m_swapSticks[0])
			// {
			// 	useStick = (useStick + 1) % 2;
			// }

			m_rawStickMove.x = joypad.m_rawAxis[useStick == DC::kJoypadStickLeft ? Joypad::kAxisLeftX : Joypad::kAxisRightX];
			m_rawStickMove.y = joypad.m_rawAxis[useStick == DC::kJoypadStickLeft ? Joypad::kAxisLeftY : Joypad::kAxisRightY];			
		}
	}

	if (cheating || !joypad.IsCommandDisabled(SID("camera")))
	{
		const DC::JoypadCommand* pCommand = joypad.GetCommand(SID("camera"));
		if (pCommand)
		{
			DC::JoypadStick useStick = pCommand->m_stick.m_type;

			// Prevent left stick from controlling both menu navigation and camera
			// (Left stick is always used to navigate menus, despite *joypad-swap-sticks*)
			if (Menu2::PageStack* pPageStack = Menu2::Root::Get().GetFocusedPageStack())
				if (pPageStack->GetTopPageId() == SID("menu-player-tabs/page-player-menu-tabs"))
					useStick = DC::kJoypadStickRight;

			joypad.GetDeadZoneAdjustedStick(&m_stickCamera, 0.2f, 1.0f, useStick);

			m_rawStickCamera.x = joypad.m_rawAxis[useStick == DC::kJoypadStickLeft ? Joypad::kAxisLeftX : Joypad::kAxisRightX];
			m_rawStickCamera.y = joypad.m_rawAxis[useStick == DC::kJoypadStickLeft ? Joypad::kAxisLeftY : Joypad::kAxisRightY];
			m_stickScale = joypad.m_stick[useStick].m_scale;
			m_rawGyroVelRad = Vec3(joypad.GetAngularX(), joypad.GetAngularY(), joypad.GetAngularZ());

			m_downDPadPressedWithRepeat = joypad.m_buttons & DC::kJoypadButtonDown;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

float CameraJoypad::GetX (U32 playerIndex) const
{
	if (EngineComponents::GetNdGameInfo()->m_CameraXMode[playerIndex])
		return -m_stickCamera.X();
	else
		return m_stickCamera.X();
}

/// --------------------------------------------------------------------------------------------------------------- ///

float CameraJoypad::GetY (U32 playerIndex) const
{
	if (EngineComponents::GetNdGameInfo()->m_CameraYMode[playerIndex])
		return -m_stickCamera.Y();
	else
		return m_stickCamera.Y();
}

/// --------------------------------------------------------------------------------------------------------------- ///

float CameraJoypad::GetXUnflipped () const
{
	return m_stickCamera.X();
}

/// --------------------------------------------------------------------------------------------------------------- ///

float CameraJoypad::GetYUnflipped () const
{
	return m_stickCamera.Y();
}

/// --------------------------------------------------------------------------------------------------------------- ///

float CameraJoypad::GetDampedY (U32 playerIndex) const
{
	float x = GetX(playerIndex);
	float y = GetY(playerIndex);
	return Sign(y) * LerpScale(LerpScale(0.3f, 0.6f, 0.0f, 0.3f, Abs(x)), 1.0f, 0.0f, 1.0f, Abs(y));
}

float CameraJoypad::GetDampedYUnflipped() const
{
	float x = GetXUnflipped();
	float y = GetYUnflipped();
	return Sign(y) * LerpScale(LerpScale(0.3f, 0.6f, 0.0f, 0.3f, Abs(x)), 1.0f, 0.0f, 1.0f, Abs(y));
}

/// --------------------------------------------------------------------------------------------------------------- ///

CameraJoypad CameraJoypadBlend (const CameraJoypad& pad0, const CameraJoypad& pad1, float blend)
{
	return CameraJoypad(Lerp(pad0.m_stickMove, pad1.m_stickMove, blend),
						Lerp(pad0.m_stickCamera, pad1.m_stickCamera, blend),
						Lerp(pad0.m_rawStickMove, pad1.m_rawStickMove, blend),
						Lerp(pad0.m_rawStickCamera, pad1.m_rawStickCamera, blend));
}

float g_rawGyroControllerYawToCameraYaw = 1.f;

/// --------------------------------------------------------------------------------------------------------------- ///
Vec2 CameraJoypad::GyroRawToCamSpeed(Vec3_arg rawGyroVelRad)
{
	Vec2 out;
	out.x = rawGyroVelRad.z;	// raw controller roll
	out.x += g_rawGyroControllerYawToCameraYaw * rawGyroVelRad.y; // raw controller yaw (optional)
	out.y = -rawGyroVelRad.x;	// raw controller pitch

	// camera-speed: positive is down, negative is up.

	return out;
}
