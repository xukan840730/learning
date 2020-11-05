/*
 * Copyright (c) 2004-2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

/*! \file camerasemifixed.h
    Author:  Vitaliy A. Genkin, vitaliy_genkin@naughtydog.com
    Date:    Tue 09/26/2006  9:41:53.77
    \brief Semi-fixed camera control implementation.

    Semi-fixed camera control implementation.
 */

#ifndef CAMERA_SEMI_FIXED_H
#define CAMERA_SEMI_FIXED_H

#include "gamelib/camera/camera-control.h"
#include "gamelib/scriptx/h/nd-camera-settings.h"

struct SceneCamera;

class CameraControlSemiFixed : public CameraControl
{
private:
	typedef CameraControl ParentClass;

	void SetupInternal(NdGameObjectHandle hFocusObj, StringId64 spawnerId, const Locator& loc, float fov);

public:
	Locator m_entityLoc;

	virtual CameraBlendInfo CameraStart(const CameraStartInfo& startInfo) override;

	void AllowMoveAlongAxis(int iAxis, bool bAllow);

	// CameraControl
	virtual CameraLocation UpdateLocation(const CameraInstanceInfo& info) override;

	void ReadSettingsFromSpawner(DC::CameraSemiFixedSettings& cameraSettings, const EntitySpawner* pSpawner);

protected:
	void UpdateAxes();
	void GetTargetOffset(Vector * vecOut) const;
	void GetSourceOffset(Vector * vecOut) const;

	struct Axes
	{
		Locator entity;
		Locator target;
		Locator entityToTarget;
	};
	Axes m_axes;

	DC::CameraSemiFixedSettings m_cameraSettings;
	Point m_lookAt;
	Point m_lookFrom;
	bool m_bAllowMoveAlongAxis[3];			//!< For every axis along which the movement is allowed the value should be "true"
};

CAMERA_DECLARE(kCameraSemiFixed);

#endif // CAMERA_SEMI_FIXED_H

