/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/camera/camera-rewind.h"

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
#include "ndlib/util/jaf-anim-recorder-manager.h"

#include "gamelib/camera/camera-manager.h"

PROCESS_REGISTER(CameraControlRewind, CameraControl);

CAMERA_REGISTER_RANKED(kCameraRewind, CameraControlRewind, CameraStartInfo, CameraPriority(CameraPriority::Class::kForced, CameraPriority::LevelMax - 2), CameraRank::Debug);

/// --------------------------------------------------------------------------------------------------------------- ///
CameraControlRewind::CameraControlRewind() : CameraControl()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraBlendInfo CameraControlRewind::CameraStart(const CameraStartInfo& baseStartInfo)
{
	m_flags.m_runWhenPaused = true;
	return CameraControl::CameraStart(baseStartInfo);
}


/// --------------------------------------------------------------------------------------------------------------- ///
CameraLocation CameraControlRewind::UpdateLocation(const CameraInstanceInfo& info)
{
	F32 fov;
	F32 nearPlaneDist;
	Locator camLoc = JAFAnimRecorderManager::GetManager().GetCurrentFrameCamera(fov, nearPlaneDist);
	CameraLocation camLocation = CameraLocation(camLoc);
	camLocation.SetFov(fov);
	camLocation.SetNearPlaneDist(nearPlaneDist);
	
	return camLocation;
}



