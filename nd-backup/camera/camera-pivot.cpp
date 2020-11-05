/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/camera/camera-pivot.h"
#include "gamelib/scriptx/h/nd-camera-settings.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "ndlib/process/process-mgr.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/camera/camera-manager.h"
#include "gamelib/ndphys/rigid-body.h"

PROCESS_REGISTER(CameraControlPivot, CameraControl);
CAMERA_REGISTER(kCameraPivot, CameraControlPivot, CameraPivotStartInfo, CameraPriority::Class::kGameplay);

CameraPivotStartInfo::CameraPivotStartInfo(const BoundFrame& frame)
	: CameraStartInfo(SID("CameraPivotStartInfo"))
	, m_isExplicit(true)
	, m_parentGoId(INVALID_STRING_ID_64)
{
	SetLocator(frame.GetLocatorWs());

	const RigidBody* pBindTarget = frame.GetBinding().GetRigidBody();
	if (pBindTarget)
	{
		const NdGameObject* pGo = pBindTarget->GetOwner();
		if (pGo)
			m_parentGoId = pGo->GetUserId();
	}
}

bool CameraPivotStartInfo::GetFrame(BoundFrame& frame) const
{
	if (m_isExplicit && GetLocator())
	{
		frame.SetLocatorWs(*GetLocator());
		if (m_parentGoId != INVALID_STRING_ID_64)
		{
			const NdGameObject* pGo = NdGameObject::LookupGameObjectByUniqueId(m_parentGoId);
			if (pGo)
			{
				// NB: This approach does not support binding to arbitrary joints.
				// What we really need is a way to figure out to which RigidBody/joint
				// the original BoundFrame was attached to... But in practice we don't
				// use that feature anyway, so we'll kick the can...
				frame.SetBinding(Binding(pGo->GetDefaultBindTarget()));
			}
		}
		return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraControlPivot::SetupInternal(NdGameObjectHandle hFocusObj,  const BoundFrame& frame)
{
	m_hFocusObj = hFocusObj;
	m_frame = frame;
	m_flags.m_needsFocus = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///

const EntitySpawner* CameraControlPivot::ExtractStartInfo(BoundFrame& frame, DC::CameraBaseSettings& cameraSettings,
														  const CameraPivotStartInfo& startInfo) const
{
	const DC::CameraBaseSettings* pCameraSettings = ScriptManager::Lookup<DC::CameraBaseSettings>(SID("*default-camera-base-settings*"));
	ASSERT(pCameraSettings);
	cameraSettings = *pCameraSettings;

	const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(startInfo.m_spawnerId);
	ReadBaseSettingsFromSpawner(cameraSettings, pSpawner);

	if (startInfo.IsExplicit())
	{
		// This indicates that the caller has provided all three bits of info.
		ASSERT(startInfo.GetLocator());
		startInfo.GetFrame(frame);
		//distance = startInfo.m_distance;
		//offsetFromPlayer = startInfo.m_offsetFromPlayer;
	}
	else
	{
		// Get these bits of info from our DC camera settings.
		frame = BoundFrame(cameraSettings.m_locator,
						   Binding((pSpawner && pSpawner->GetParentNdGameObject() != nullptr)
						   ? pSpawner->GetParentNdGameObject()->GetDefaultBindTarget()
						   : nullptr));
		//distance = cameraSettings.m_sourceOffset.Z();
		//offsetFromPlayer = false;
	}

	return pSpawner;
}

/// --------------------------------------------------------------------------------------------------------------- ///

CameraBlendInfo CameraControlPivot::CameraStart(const CameraStartInfo& baseStartInfo)
{
	CameraBlendInfo blendInfo = ParentClass::CameraStart(baseStartInfo);

	const CameraPivotStartInfo& startInfo = *PunPtr<const CameraPivotStartInfo*>(&baseStartInfo);

	BoundFrame frame;
	const EntitySpawner* pSpawner = ExtractStartInfo(frame, m_cameraSettings, startInfo);

	SetupInternal(startInfo.GetFocusObject(), frame);

	return blendInfo;
}

/// --------------------------------------------------------------------------------------------------------------- ///

bool CameraControlPivot::IsEquivalentTo(const CameraStartInfo& baseStartInfo) const
{
	if (!ParentClass::IsEquivalentTo(baseStartInfo))
		return false;

	const CameraPivotStartInfo& startInfo = *PunPtr<const CameraPivotStartInfo*>(&baseStartInfo);

	BoundFrame frame;
	DC::CameraBaseSettings cameraSettings;
	const EntitySpawner* pSpawner = ExtractStartInfo(frame, cameraSettings, startInfo);

	static float kPosTolerance = 0.01f; // 1 cm
	const Scalar kPosToleranceSqr(kPosTolerance*kPosTolerance);

	Vector dPos = frame.GetTranslationWs() - m_frame.GetTranslationWs();

	if (IsPositive(LengthSqr(dPos) - kPosToleranceSqr))
		return false;

	return true; // passed all tests
}

/// --------------------------------------------------------------------------------------------------------------- ///

CameraLocation CameraControlPivot::UpdateLocation (const CameraInstanceInfo& info)
{
	if (m_hFocusObj.ToProcess() == nullptr)
		return info.m_prevLocation;

	Locator oldLoc = info.m_prevLocation.GetLocator();

	Locator loc;
	loc.SetPos(m_frame.GetTranslation());

	Point targetPos = m_hFocusObj.ToProcess()->GetSite(SID("LookAtPos")).GetTranslation();
	targetPos += m_cameraSettings.m_targetOffset.X() * GetLocalX(m_cameraSettings.m_locator.Rot());
	targetPos += m_cameraSettings.m_targetOffset.Y() * GetLocalY(m_cameraSettings.m_locator.Rot());
	targetPos += m_cameraSettings.m_targetOffset.Z() * GetLocalZ(m_cameraSettings.m_locator.Rot());

	Vector forward = SafeNormalize(targetPos - loc.Pos(), SMath::kUnitYAxis);

	loc.SetRot(QuatFromLookAt(forward, SMath::kUnitYAxis));

	loc = m_cameraShake.GetLocator(GetPlayerIndex(), loc);

	return CameraLocation(loc, m_cameraSettings.m_fov);
}


