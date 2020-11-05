/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef CAMERAJOYPAD_H
#define CAMERAJOYPAD_H

#ifndef JOYPAD_H
#include "ndlib/io/joypad.h"
#endif

struct CameraJoypad
{
	Vec2 m_stickMove;							// x: -1.0 left, 1.0 right; y: -1.0 up,   1.0 down 
	Vec2 m_stickCamera;							// x: -1.0 left, 1.0 right; y: -1.0 up,   1.0 down
	Vec2 m_rawStickMove;						// x: -1.0 left, 1.0 right; y: -1.0 up,   1.0 down 
	Vec2 m_rawStickCamera;						// x: -1.0 left, 1.0 right; y: -1.0 up,   1.0 down
	Vec3 m_rawGyroVelRad = Vec3(kZero);			// joypad gyro angular velocity: angularX, angularY, angularZ (radians/second)
	float m_stickScale;
	bool m_downDPadPressedWithRepeat;
	bool m_fromLookAtControl;					// if true, this camera joypad is adjusted by look-at controller.
	// 32 bytes -- watch size b/c this structure is copied around
	
	CameraJoypad() 
		: m_stickMove(SMath::kZero)
		, m_stickCamera(SMath::kZero)
		, m_rawStickMove(SMath::kZero)
		, m_rawStickCamera(SMath::kZero)
		, m_rawGyroVelRad(Vec3(kZero))
		, m_stickScale(1.0f)
		, m_fromLookAtControl(false)
	{}
	CameraJoypad(const Joypad& joypad);
	CameraJoypad(Vec2 stickLeft, Vec2 stickRight, Vec2 rawStickLeft, Vec2 rawStickRight)
		: m_stickMove(stickLeft)
		, m_stickCamera(stickRight)
		, m_rawStickMove(rawStickLeft)
		, m_rawStickCamera(rawStickRight)
		, m_rawGyroVelRad(Vec3(kZero))
		, m_stickScale(1.0f)
		, m_fromLookAtControl(false)
	{}

	float GetX(U32 playerIndex) const;
	float GetY(U32 playerIndex) const;
	float GetXUnflipped() const;
	float GetYUnflipped() const;
	float GetDampedY(U32 playerIndex) const;
	float GetDampedYUnflipped() const;

	static Vec2 GyroRawToCamSpeed(Vec3_arg rawGyroVelRad);  // from raw joystick gyro angular velocity to camera speed. up: negative, down: positive.
};

extern CameraJoypad CameraJoypadBlend(const CameraJoypad& pad0, const CameraJoypad& pad1, float blend);

#endif // CAMERAJOYPAD_H
