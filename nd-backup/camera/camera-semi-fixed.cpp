/*
 * Copyright (c) 2004-2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

/*! \file camerasemifixed.cpp
    Author:  Vitaliy A. Genkin, vitaliy_genkin@naughtydog.com
    Date:    Tue 09/26/2006  9:41:54.27
    \brief Semi-fixed camera control implementation.

    Semi-fixed camera control implementation.
 */


#include "corelib/math/mathutils.h"
#include "corelib/math/matrix3x3.h"
#include "ndlib/render/display.h"
#include "ndlib/process/event.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/io/joypad.h"
#include "corelib/util/angle.h"
#include "ndlib/process/process-mgr.h"
#include "gamelib/camera/camera-semi-fixed.h"
#include "gamelib/scriptx/h/nd-camera-settings.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/camera/camera-manager.h"

PROCESS_REGISTER(CameraControlSemiFixed, CameraControl);
CAMERA_REGISTER(kCameraSemiFixed, CameraControlSemiFixed, CameraStartInfo, CameraPriority::Class::kGameplay);

namespace {

///
// Find the projection of "ptFreePoint" on the line with "ptRay" on the line and direction "ptRayDirection"
// A return value of "true" means that the resulting "pptRayProjection" is towards the positive
// direction "ptRayDirection", a return value of "false" means negative direction.
//
bool ProjectPointOnRay(Point_arg ptFreePoint,
					   Point_arg ptRay,
                       Vector_arg ptRayDirection /* non-zero */,
					   Point* pptRayProjection)
{
	// Find the cosine
    Vector rayDir = Normalize( ptRayDirection );
	float fCosineFactor = Dot(rayDir, ptFreePoint - ptRay);
    *pptRayProjection = ptRay + fCosineFactor*rayDir;

	return (fCosineFactor >= 0);
}
}


CameraBlendInfo CameraControlSemiFixed::CameraStart(const CameraStartInfo& startInfo)
{
	CameraBlendInfo blendInfo = CameraControl::CameraStart(startInfo);

	StringId64 cameraSettingsId = SID("*default-camera-semi-fixed-settings*");
	if (startInfo.m_settingsId != INVALID_STRING_ID_64)
		cameraSettingsId = startInfo.m_settingsId;

	const EntitySpawner* pSpawner = ReadSettingsFromDcAndSpawner(*this,
										m_cameraSettings,
										cameraSettingsId,
										startInfo.m_spawnerId);

	Locator initialLoc;
	if (startInfo.GetLocator())
		initialLoc = *startInfo.GetLocator();
	else
		initialLoc = m_cameraSettings.m_locator;

	SetupInternal(startInfo.GetFocusObject(), startInfo.m_spawnerId, initialLoc, startInfo.m_fov);

	return blendInfo;
}

void CameraControlSemiFixed::SetupInternal(NdGameObjectHandle hFocusObj, StringId64 spawnerId, const Locator& loc, float fov)
{
	m_hFocusObj = hFocusObj;
	m_flags.m_needsFocus = true;
	m_flags.m_suppressJoypadDuringBlend = true;

	char axis[3] = {'x', 'y', 'z'};
	char const* szSourceOffsetAxis = (spawnerId != INVALID_STRING_ID_64 && m_cameraSettings.m_sourceOffsetAxis) ? m_cameraSettings.m_sourceOffsetAxis : "";
	for (int k = 0; k < 3; ++k)
		AllowMoveAlongAxis(k, strchr(szSourceOffsetAxis, axis[k]) || strchr(szSourceOffsetAxis, axis[k] - 'x' + 'X'));

	// JQG: This leaves m_entityLoc uninitialized when we have no spawner, which I believe is a bug.
	//if (spawnerId != INVALID_STRING_ID_64)
	//{
	//	m_entityLoc = m_cameraSettings.m_locator;
	//}
	m_entityLoc = loc;
}

void CameraControlSemiFixed::ReadSettingsFromSpawner(DC::CameraSemiFixedSettings& cameraSettings, const EntitySpawner* pSpawner)
{
	ReadBaseSettingsFromSpawner(cameraSettings, pSpawner);

	const EntityDB * pDb = (pSpawner != nullptr) ? pSpawner->GetEntityDB() : nullptr;
	if (pDb)
	{
		cameraSettings.m_disableLookAround = pDb->GetData<I32>(SID("disable-look-around"), cameraSettings.m_disableLookAround);
		cameraSettings.m_sourceOffsetAxis = pDb->GetData<String>(SID("source-offset-axis"), String()).GetString();
	}
}

void CameraControlSemiFixed::GetTargetOffset(Vector * vecOut) const
{
	*vecOut = m_cameraSettings.m_targetOffset;
}

void CameraControlSemiFixed::GetSourceOffset(Vector * vecOut) const
{
	*vecOut = m_cameraSettings.m_sourceOffset;
}

void CameraControlSemiFixed::AllowMoveAlongAxis(int iAxis, bool bAllow)
{
	ASSERT(unsigned(iAxis) < 3U);
	m_bAllowMoveAlongAxis[iAxis] = bAllow;
}

void CameraControlSemiFixed::UpdateAxes ()
{
	if (m_spawnerId != INVALID_STRING_ID_64)
		m_axes.entity = m_cameraSettings.m_locator;
	//else
	//	m_location.GetLocator(&m_axes.entity);

	const NdGameObject* pProcGameObj = m_hFocusObj.ToProcess();
	if (pProcGameObj)
	{
		m_axes.target.SetPos(pProcGameObj->GetSite(SID("LookAtPos")).GetTranslation());
		m_axes.target.SetRot(pProcGameObj->GetLocator().Rot());

		m_axes.entityToTarget.SetPos(m_axes.entity.Pos());
 		Vector entityToTargetZ = m_axes.target.Pos() - m_axes.entityToTarget.Pos();
		m_axes.entityToTarget.SetRot(QuatFromLookAt(entityToTargetZ, kUnitYAxis));
	}
	else
	{
		//m_location.GetLocator(&m_axes.target);
		m_axes.entityToTarget = m_axes.entity;
	}

	Vector offset;
	GetTargetOffset(&offset);
	// Rotate(&offset, &m_axes.entityToTarget.q, &offset);
	offset = Rotate(m_axes.entity.Rot(), offset);
	m_lookAt = m_axes.target.Pos() + offset;

	GetSourceOffset(&offset);
	offset = Rotate( m_axes.entity.Rot(), offset);
	m_lookFrom = m_axes.entity.Pos() + offset;

	#if 0
	// debug
	g_prim.Draw( DebugCoordAxes(m_axes.entity,1.f) );
	g_prim.Draw( DebugCoordAxes(m_axes.target,0.9f) );
	g_prim.Draw( DebugCoordAxes(m_axes.entityToTarget,0.8f) );
	#endif
}

CameraLocation CameraControlSemiFixed::UpdateLocation (const CameraInstanceInfo& info)
{
	UpdateAxes();

	Vector vecRestriction(0.f, 0.f, 0.f); // 1. kRestrictLine = line direction     2. kRestrictPlane = plane normal
	int numFreeAxis =
		(m_bAllowMoveAlongAxis[0] ? 1 : 0) +
		(m_bAllowMoveAlongAxis[1] ? 1 : 0) +
		(m_bAllowMoveAlongAxis[2] ? 1 : 0);

	// Process the free axis
    switch(numFreeAxis)
    {
    case 1:
        {
            vecRestriction = Vector((m_bAllowMoveAlongAxis[0] ? 1.f : 0.f),
                (m_bAllowMoveAlongAxis[1] ? 1.f : 0.f),
                (m_bAllowMoveAlongAxis[2] ? 1.f : 0.f));
            vecRestriction = Rotate(m_entityLoc.Rot(), vecRestriction);
            Point lookFrom;
            ProjectPointOnRay(m_lookAt, m_entityLoc.Pos(), vecRestriction, &lookFrom);
            m_lookFrom = lookFrom;
        }
        break;

    case 2:
        {
            vecRestriction = Vector((m_bAllowMoveAlongAxis[0] ? 0.f : 1.f),
                (m_bAllowMoveAlongAxis[1] ? 0.f : 1.f),
                (m_bAllowMoveAlongAxis[2] ? 0.f : 1.f));
            vecRestriction = Rotate(m_entityLoc.Rot(), vecRestriction);
            Point lookFrom;
            ProjectPointOnPlane(m_lookAt, m_entityLoc.Pos(), vecRestriction, &lookFrom);
            m_lookFrom = lookFrom;
        }
        break;

    default:
        {
            m_lookFrom = m_entityLoc.Pos();
        }
        break;
    }

	// Apply source offset
	GetSourceOffset(&vecRestriction);
	vecRestriction = Rotate(m_entityLoc.Rot(), vecRestriction);
	m_lookFrom += vecRestriction;

	// return the resulting location
	return CameraLocation(m_lookFrom, m_lookAt - m_lookFrom, m_cameraSettings.m_fov);
}


