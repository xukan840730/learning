/*
 * Copyright (c) 2004-2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

/*! \file cameraspline.h
    Author:  Vitaliy A. Genkin, vitaliy_genkin@naughtydog.com
    Date:    Thu 11/02/2006 17:30:14.68
    \brief Camera following a spline.

    Camera following a spline.
 */

#ifndef CAMERA_SPLINE_H
#define CAMERA_SPLINE_H

#include "gamelib/spline/catmull-rom.h"
#include "gamelib/camera/camera-shake.h"
#include "gamelib/camera/camera-fixed.h"
#include "gamelib/ndphys/water-detector.h"
#include "ndlib/util/pid-controller.h"

namespace DC { struct CameraSplineSettings; }

const int kMaxNumberSplineCameras = 128;

enum SplineCameraType
{
	kSplineCameraLinear,
	kSplineCameraNonLinear,
	kSplineCameraScripted,
	kSplineCameraUpright,
};

struct CameraSplineStartInfo : public CameraStartInfo
{
	StringId64 m_splineId;
	SplineCameraType m_splineCameraType;
	float m_springMultiplier;
	float m_scriptedTime;

	CameraSplineStartInfo() 
		: CameraStartInfo(SID("CameraSplineStartInfo"))
		, m_splineId(INVALID_STRING_ID_64)
		, m_splineCameraType(kSplineCameraLinear)
		, m_springMultiplier(1.0f)
		, m_scriptedTime(0.0f)
	{ }
};

class CameraControlSpline : public CameraControl
{
private: typedef CameraControl ParentClass;
public:
	virtual CameraBlendInfo CameraStart(const CameraStartInfo& startInfo) override;
	virtual bool IsEquivalentTo(const CameraStartInfo& startInfo) const override;
	CameraControlSpline();

	// CameraControl
	virtual CameraLocation UpdateLocation(const CameraInstanceInfo& info) override;
	StringId64 GetSplineId() const { return m_splineId; }
	F32 GetMovedBackDistance() const { return m_movedBackDistance; }
	void DisableRotate() { m_enableRotate = false; }

	void ReadSettingsFromSpawner(DC::CameraSplineSettings& cameraSettings, const EntitySpawner* pSpawner);

protected:
	void GetDesiredValues(Locator& loc, float& fov, float& distortion, float& zeroPlaneDist, Locator& deltaLoc, float& waterHeight, bool isTopControl);
	void SplineAcquire(StringId64 splineId);
	void CalculateUpVector();
	Vector GetUp() const;

	SplineCameraType m_splineCamType;
	bool m_enableRotate;
	bool m_enableShake;
	bool m_noParentSpring;
	Vector m_upVector;
	BoundFrame m_lastPoint;
	float m_springMultiplier;
	float m_scriptedTime;
	CameraMoveRotate m_cameraControl;
	CameraShake m_cameraShake;
	StringId64 m_splineId;
	CatmullRom::Handle m_splineHandle;
	const EntitySpawner* m_splineCameras[kMaxNumberSplineCameras];
	float m_splineFov[kMaxNumberSplineCameras];
	float m_splineDistortion[kMaxNumberSplineCameras];
	float m_splineZeroPlaneDist[kMaxNumberSplineCameras];
	int m_splineIndexes[kMaxNumberSplineCameras];
	float m_splineWaterHeight[kMaxNumberSplineCameras];
	int m_numSplineCameras;
	SpringTracker<float> m_distanceSpring;
	SpringTracker<float> m_ttSpring;
	float m_tt;
	float m_oldTT;
	Locator m_currentLocator;
	float m_currentFov;
	float m_currentDistortion;
	float m_currentZeroPlaneDist;
	float m_currentWaterHeight;
	float m_movedBackDistance;
	float m_movementScale;
	float m_highestTt;
	WaterDetector m_waterDetector;
	float m_currentY;
	PidController m_currentYPid;
	Vector m_focusObjOffset;

};

CAMERA_DECLARE(kCameraSpline);

#endif // CAMERA_SPLINE_H

