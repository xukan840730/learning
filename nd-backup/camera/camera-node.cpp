/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/camera/camera-node.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/util/curves.h"
#include "gamelib/camera/camera-control.h"
#include "gamelib/camera/camera-manager.h"

/// --------------------------------------------------------------------------------------------------------------- ///

const float kMinBlend = 1e-4f;

bool CameraNode::IsDead(const CameraControl* nextControl) const
{
	const CameraControl* control = m_handle.ToProcess();
	if (!control)
		return true;

	if (control->GetNodeFlags().m_dead && control->GetBlend() < kMinBlend)
		return true;

	return control->IsDead(nextControl);
}
