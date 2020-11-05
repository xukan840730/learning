/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef CAMERAMANUAL_H
#define CAMERAMANUAL_H

#include "gamelib/camera/camera-shake.h"
#include "gamelib/camera/camera-control.h"
#include "gamelib/gameplay/nd-game-object.h"

struct CameraManualStartInfo : public CameraStartInfo
{
	CameraManualStartInfo() 
		: CameraStartInfo(SID("CameraManualStartInfo"))
		, m_target(kOrigin)
		, m_isTargetValid(false)
		, m_stayFocused(false)
	{ }

	void SetTarget(const Point& target) { m_isTargetValid = true; m_target = target; }
	const Point* GetTarget() const { return (m_isTargetValid) ? &m_target : nullptr; }

private:
	Point m_target;
	bool m_isTargetValid;
public:
	bool m_stayFocused;
};

class CameraControlManual : public CameraControl
{
public:
	I32 m_stayFocused;
	Vec3 m_targetPos;  // only used if m_stayFocused is true
	Vec3 m_offset;
	float m_offsetDist;
	float m_twist;
	TimeFrame m_lastFastTime;
	CameraShake m_cameraShake;
	SpringTracker<float> m_handCamTracker;
	float m_handyCamBlend;
	SpringTracker<float> m_camStickXSpring;
	SpringTracker<float> m_camStickYSpring;
	SpringTracker<float> m_moveStickXSpring;
	SpringTracker<float> m_moveStickYSpring;
	float m_moveStickX;
	float m_moveStickY;
	float m_camStickX;
	float m_camStickY;

	Locator m_prevParentSpaceLocator;
	Locator m_prevLocPs;
	Locator m_prevLocWs;

	bool    m_hasOrbitFocus;
	bool    m_hasSavedOrbitFocus;
	float   m_orbitDistance;
	float   m_orbitDistanceStart;
	Point   m_orbitFocus;
	Point   m_orbitFocusStart;
	float   m_savedOrbitDistance;
	Point   m_savedOrbitFocus;

	bool    m_mouseStart;
	bool    m_prevManualCameraOrbit;
	Vec2    m_startPosition;
	Vector  m_startEulerAngles;
	Locator m_mousePrevLocPs;
	Vector  m_lastPan;

	// Values used to make it possible to "bind" the manual camera to a game object
	// very handy for testing the player on fast-moving objects like trains
	NdGameObjectHandle	m_attachedGameObject;

	CameraControlManual();

	virtual CameraBlendInfo CameraStart(const CameraStartInfo& startInfo) override;

	CameraLocation UpdateLocation(const CameraInstanceInfo& info) override;

protected:
	virtual bool IsCameraSpaceBlendEnabled() const override { return false; }

private:
	const Locator SetParentSpaceObject(const NdGameObject* pParentObject);
	virtual bool KeepLowerCamerasAlive() const override { return true; }
	virtual bool AffectGameCameraLocator() const override { return false; }

};

CAMERA_DECLARE(kCameraManual);
CAMERA_DECLARE(kCameraManualBase);

#endif

