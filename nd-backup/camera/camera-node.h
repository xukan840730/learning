/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef CAMERANODE_H
#define CAMERANODE_H

#include "corelib/containers/linkednode.h"
#include "gamelib/camera/camera-manager.h"

class CameraNode : public LinkedNode<CameraNode, CameraNode*>
{
public:
	MutableCameraControlHandle m_handle;

	// TODO: to better support dist-to-obj/travel-dist blending, I would like to change CameraNodes from LinkedList to GeneralTree
	// So I could blend multiple cameras as group.

	bool IsDead(const CameraControl* nextControl) const;
};

#endif // CAMERANODE_H
