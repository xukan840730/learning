/*
* Copyright (c) 2004 Naughty Dog, Inc. 
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#ifndef ND_PLAYER_JOYPAD_H
#define ND_PLAYER_JOYPAD_H

#include "ndlib/util/maybe.h"
#include "ndlib/engine-components.h"
#include "ndlib/nd-frame-state.h"

class NdJoypadGesture;
namespace DC {
struct JoypadCommand;
}  // namespace DC

class NdPlayerJoypad
{
public:
	enum Stick
	{
		kStickMove,
		kStickCamera,

		kStickCount
	};

	NdPlayerJoypad();

	void Init(U32 id, U32 screenIndex, U32 playerIndex, float innerDead, float outerDead, bool resetJoypadData = true);
	void Update();

	Vector GetStickWS(Stick stick) const;					// camera-relative world-space stick velocity
	Vector GetStickWsReadOnly(Stick stick) const;
	Vector GetPlayerRelativeStickWS(Stick stick);	// player-relative world-space stick velocity
	Vec2 GetStick(Stick stick) const;						// joypad-space stick velocity
	float GetStickSpeed(Stick stick) const;				// stick speed (coord-space independent)

	float GetPitchAngle();
	void ClearCommand(StringId64 id);
	void ClearSingleCommand(StringId64 id);
	void UnclearCommand(StringId64 id);
	Locator GetStickCameraLocator();
	Maybe<Locator> GetOverrideCameraLocator();
	const DC::JoypadCommand* GetCommand(StringId64 id) const;
	bool IsCommandActive(StringId64 id, bool ignoreDisable = false) const;
	bool IsCommandDisabled(StringId64 id) const;
	bool IsHoldCommandHeld(StringId64 id) const;
	Maybe<float> IsHoldButtonCommand(StringId64 id) const;
	TimeFrame GetLastTimeCommandPressed(StringId64 id) const;
	void ForceCommand(StringId64 id);
	float GetPitchAngleRecentChange();
	U32 GetScreenIndex() { return m_screenIndex; }
	U32 GetPlayerIndex() { return m_playerIndex; }
	float GetInnerDeadZone() const { return m_innerDead; }
	void SetInnerDeadZoneMultiplier(float mult) { m_innerDeadZoneMultiplier = mult; }
	float GetOuterDeadZone() const { return m_outerDead; }
	bool IsCommandAnalog(StringId64 id) const;
	float GetCommandAnalog(StringId64 id) const;

	void SetFreezeIntensity(bool freeze);
	
	bool GetFreezeCamera() const { return m_overrideCameraLocator.Valid(); }
	void SetFreezeCamera(bool freeze);

	void SetOverrideCamera(const Locator &cameraLocator);
	void UnsetOverrideCamera();
	void SetOverrideCameraBlend(float blend) { m_overrideAmount = blend; }

	Quat GetOrientation();
	Locator GetCameraLocator();

	Joypad* ToJoypad() const { return EngineComponents::GetNdFrameState()->GetJoypad(m_joypadId); }
	U32 GetJoypadId() const { return m_joypadId; }

	NdJoypadGesture * GetGesture(StringId64 sid) const;

#if !FINAL_BUILD
	void DebugDisableStick(bool disable);
	bool IsStickDebugDisabled() const;
#endif

	static void EnableCameraCutCompensate(U32 playerIndex); // for use by DC script -- call every frame
	static void ForceCameraCutCompensate(U32 playerIndex); // for use by C++ code -- call every frame

	bool AreSticksSwapped() const;
	bool AreAimSticksSwapped() const;

	Maybe<Vec2> GetGyroFlickX() const; // camera yaw
	Maybe<Vec2> GetGyroFlickY() const; // camera pitch

protected:
	virtual void UpdateJoypadScheme() {};
	virtual void PostUpdate() {};

	void SmoothRawInput();
	void UpdateCamDirections();
	void ComputeWSInput();
	bool CameraTimerExpired();
	bool CameraMinimalChange();
	void UpdateGyroFlick();

	//-------------------------------
	// Joypad index
	//-------------------------------
	bool m_inited;
#if !FINAL_BUILD
	bool m_debugDisabled;
#endif
	U32 m_screenIndex;
	U32 m_playerIndex;
	U32 m_joypadId;
	I64 m_lastFrameNumber;

	//-------------------------------
	// sticks directions (joypad space)
	//-------------------------------
	Vec2 m_stickMove;
	Vec2 m_stickCamera;

	//-------------------------------
	// camera cut compensate stuff
	//-------------------------------
	Maybe<Locator> m_overrideCameraLocator;
	float m_overrideAmount;
	Locator m_cameraLocator;
	TimeFrame m_lastCameraGoodTime;
	Vec2 m_oldStickDir;
	bool m_hasOldCameraLoc;

	//-------------------------------
	// dead zones
	//-------------------------------
	float m_innerDead;
	float m_outerDead;
	float m_innerDeadZoneMultiplier;

	//-------------------------------
	// stick direction/intensity (world space, camera-relative)
	//-------------------------------
	float m_stickSpeed[kStickCount];
	Vector m_cameraRelStickWS[kStickCount];

	//-------------------------------
	// stick direction/intensity (world space, player-relative)
	//-------------------------------
	Vector m_playerRelStickWS[kStickCount];

	//-------------------------------
	// direction the stick's being pressed, in the plane that is
	// perpendicular to camera left and world up
	// currently not normalized
	//-------------------------------
	Vector m_screenPlaneDir[kStickCount];

	float m_pitchAngleRecentChange;
	
	float m_frozenIntensity;
	Vec2 m_frozenMoveStick;
	bool m_freezeIntensity;

	//-------------------------------
	// gyro flick: ps4 only
	//-------------------------------
	enum class GyroFlickState
	{
		kStable,
		kMove,
	};

	struct GyroFlickDetector
	{
		GyroFlickState m_state = GyroFlickState::kStable;
		Vec2 m_flickDir = Vec2(kZero);
		I64 m_flickDetectFN = -1;
		TimeFrame m_flickDetectTime;

		float m_lastFlickExtrema = 0.f;		// velocity local extrema when flick detected
	};

	static const int kNumDetetors = 2;
	GyroFlickDetector m_gyroFlickDetector[kNumDetetors];

	Vec2 m_gyroCurrCamSpdRad = Vec2(0, 0);						// current frame camera speed (radians/second)
	Vec2 m_gyroLastCamSpdRad[2] = { Vec2(kZero), Vec2(kZero) };	// last 2 frames camera speed (radians/second)
	Vec2 m_gyroCamAcc = Vec2(0, 0);								// angAcc = (curr - last) / dt

};

bool DebugFlyDisable();

#endif // PLAYER_JOYPAD_H

