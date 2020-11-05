/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef CAMERAFIXED_H
#define CAMERAFIXED_H

#include "gamelib/camera/camera-control.h"
#include "ndlib/camera/camera-location.h"
#include "gamelib/camera/camera-shake.h"
#include "ndlib/process/bound-frame.h"

class EntitySpawner;
struct CameraJoypad;
namespace DC { struct CameraFixedSettings; }

class CameraMoveRotate
{
private:
	SpringTracker<float> m_xSpring;
	SpringTracker<float> m_ySpring;
	SpringTrackerAngleDegrees m_horizontalAngleSpring;
	SpringTrackerAngleDegrees m_verticalAngleSpring;
	float x;
	float y;
	float m_horizontalAngle;
	float m_verticalAngle;
	bool m_isFirstFrame;
	Point m_lastPlayerPosition;
	float m_maxAngleHoriz;
	float m_maxAngleVert;
	U32 m_playerIndex;
	Vector m_lastDirection;
	SpringTracker<float> m_finalDirectionSpring;
	bool m_deadStickReset = true;

	void UpdateInput(const Locator& currentLocator, const Vector& playerDirection1, const Vector& playerDirection2, const CameraJoypad& pad, float fov, float& targetHAngle, float& targetVAngle, float scaleMovment);
	void GetAngles(const Locator& currentLocator, const Vector& cameraDir, bool playerIsMoving, float& targetHAngle, float& targetVAngle);

public:

	void Init(float maxAngleHoriz, float maxAngleVert, bool reset) {
		m_maxAngleHoriz = maxAngleHoriz; m_maxAngleVert = maxAngleVert; m_deadStickReset = reset;
	}
	void SetPlayerIndex(U32 playerIndex) { m_playerIndex = playerIndex; }
	CameraMoveRotate();
	Locator GetLocator(const CameraJoypad& pad, const Locator& currentLocator, const Point& playerPosition, const Vector& cameraDir, float fov, float scaleMovement = 1.0f, bool wasCut = false);
};

struct CameraFixedStartInfo : public CameraStartInfo
{
	float m_distortion;
	float m_zeroPlaneDist;
	bool m_dontKillOnFocusDeath;
	bool m_acquirePosition;
	bool m_noRoll;

	CameraFixedStartInfo()
		: CameraStartInfo(SID("CameraFixedStartInfo"))
		, m_distortion(-1.0f)
		, m_zeroPlaneDist(-1.0f)
		, m_dontKillOnFocusDeath(false)
		, m_acquirePosition(false)
		, m_noRoll(false)
	{ }
};

FWD_DECL_PROCESS_HANDLE(CameraControlFixed);

class CameraControlFixed : public CameraControl
{
private:
	typedef CameraControl ParentClass;

public:
	FROM_PROCESS_DECLARE(CameraControlFixed);

	CameraMoveRotate m_cameraControl;

	bool m_disableLookAround;
	BoundFrame m_frame;
	CameraShake m_cameraShake;
	float m_fov;
	float m_distortion;
	float m_zeroPlaneDist;
	float m_dofDistance; // CAM_TODO: support new DOF parameters
	float m_nearNear;
	float m_nearFar;
	float m_farNear;
	float m_farFar;
	float m_dofGlobalIntensity;
	float m_nearIntensity;
	float m_movementScale;
	bool m_noRoll;

	virtual Err Init(const ProcessSpawnInfo& spawnInfo) override;
	virtual StringId64 GetBaseCameraSettingsId() const;
	virtual CameraBlendInfo CameraStart(const CameraStartInfo& startInfo) override;
	void ReadSettingsFromSpawner(DC::CameraFixedSettings& cameraSettings, const EntitySpawner* pSpawner);
	void SetDof(float dofDistance, float nearNear, float nearFar, float farNear, float farFar, float globalIntensity=-1.0f, float nearIntensity=-1.0f);
	virtual CameraLocation StartLocation(const CameraControl* pPrevCamera) const override;
	virtual CameraLocation UpdateLocation(const CameraInstanceInfo& info) override;
	virtual void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound) override;
	void SetNewLocation(Locator &loc, float fov, float zeroPlaneDist=-1.f, float distortion=-1.f);
	void SetNewLocation(BoundFrame &loc, float fov, float zeroPlaneDist=-1.f, float distortion=-1.f);
};

CAMERA_DECLARE(kCameraFixed);
CAMERA_DECLARE(kCameraFixedOverride);

#endif

