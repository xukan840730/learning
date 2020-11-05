/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef CAMERAREWIND_H
#define CAMERAREWIND_H

#include "gamelib/camera/camera-control.h"
#include "gamelib/gameplay/nd-game-object.h"

class CameraControlRewind : public CameraControl
{
public:
	
	CameraControlRewind();

	virtual CameraBlendInfo CameraStart(const CameraStartInfo& startInfo) override;

	CameraLocation UpdateLocation(const CameraInstanceInfo& info) override;

protected:
	virtual bool IsCameraSpaceBlendEnabled() const override { return false; }

private:
	virtual bool KeepLowerCamerasAlive() const override { return true; }
	virtual bool AffectGameCameraLocator() const override { return false; }
};

CAMERA_DECLARE(kCameraRewind);

#endif

