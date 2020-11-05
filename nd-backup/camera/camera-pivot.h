/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef CAMERAPIVOT_H
#define CAMERAPIVOT_H

#include "ndlib/process/bound-frame.h"

#include "gamelib/camera/camera-control.h"
#include "gamelib/camera/camera-shake.h"
#include "gamelib/scriptx/h/nd-camera-settings.h"

struct CameraPivotStartInfo : public CameraStartInfo
{
	CameraPivotStartInfo()
		: CameraStartInfo(SID("CameraPivotStartInfo"))
		, m_isExplicit(false)
		, m_parentGoId(INVALID_STRING_ID_64)
	{ }
	CameraPivotStartInfo(const BoundFrame& frame);

	bool IsExplicit() const { return m_isExplicit; }
	bool GetFrame(BoundFrame& frame) const;

private:
	bool m_isExplicit;
	StringId64 m_parentGoId;
};

class CameraControlPivot : public CameraControl
{
private:
	typedef CameraControl ParentClass;
	DC::CameraBaseSettings m_cameraSettings;

	const EntitySpawner* ExtractStartInfo(BoundFrame& frame, DC::CameraBaseSettings& cameraSettings,
										  const CameraPivotStartInfo& startInfo) const;

	void SetupInternal(NdGameObjectHandle hFocusObj, const BoundFrame& frame);

public:
	BoundFrame m_frame;
	CameraShake m_cameraShake;

	virtual CameraBlendInfo CameraStart(const CameraStartInfo& startInfo) override;
	virtual bool IsEquivalentTo(const CameraStartInfo& startInfo) const override;
	CameraLocation UpdateLocation(const CameraInstanceInfo& info) override;
};

CAMERA_DECLARE(kCameraPivot);

#endif // CAMERAPIVOT_H

