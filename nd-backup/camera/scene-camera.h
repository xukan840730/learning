/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef  BIG_SCENE_CAMERA_H
#define  BIG_SCENE_CAMERA_H

#include "ndlib/anim/anim-channel.h"

/* camera exported from maya - including animations */

/// these are array indices into the associated m_rawChannelInfo
enum CameraAnimationTracks
{
	kCatTranslateX,
	kCatTranslateY,
	kCatTranslateZ,
	kCatForwardX,
	kCatForwardY,
	kCatForwardZ,
	kCatUpX,
	kCatUpY,
	kCatUpZ,
	kCatHorizFOV,
	kCatVertFOV,
	kCatNearPlane,
	kCatFarPlane,

	kCatCount
};

struct SceneCamera
{
	char *	m_name;         //!< the full dag path to the camera as a string
	Vec3	m_position;     //!< defines the translation of the camera
	Vec3 	m_forward;      //!< defines the look-at vector for the camera
	Vec3	m_up;           //!< defines the up-vector for the camera
	F32     m_horizFov;     //!< defines clipping planes
	F32     m_vertFov;      //!< defines clipping planes
	F32     m_nearPlane;    //!< defines distance from camera origin to near plane along m_forward
	F32     m_farPlane;     //!< defines distance from camera origin to far plane along m_forward

	void*	m_anim;
};

struct SceneCameraTable
{
	I32				m_numCameras;
	SceneCamera*	m_cameraArray;
};

#endif // BIG_SCENE_CAMERA_H
