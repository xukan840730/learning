/*
 * Copyright (c) 2004-2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

/*! \file cameraspline.cpp
	Author:  Vitaliy A. Genkin, vitaliy_genkin@naughtydog.com
	Date:    Thu 11/02/2006 17:30:15.29
	\brief Camera following a spline.

	Camera following a spline.
 */

#include "gamelib/camera/camera-spline.h"
#include "gamelib/camera/camera-manager.h"
#include "corelib/math/mathutils.h"
#include "corelib/math/matrix3x3.h"
#include "ndlib/render/display.h"
#include "ndlib/scriptx/h/dc-types.h"
#include "gamelib/scriptx/h/nd-camera-settings.h"
#include "ndlib/process/event.h"
#include "ndlib/io/joypad.h"
#include "corelib/util/angle.h"
#include "ndlib/process/process-mgr.h"
#include "gamelib/spline/catmull-rom.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/level/entity-spawner-group.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "ndlib/render/util/prim.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/gameplay/nav/platform-control.h"
#include "gamelib/camera/camera-manager.h"

PROCESS_REGISTER(CameraControlSpline, CameraControl);
CAMERA_REGISTER(kCameraSpline, CameraControlSpline, CameraSplineStartInfo, CameraPriority::Class::kGameplay);

bool g_debugSplineCam = false;


CameraControlSpline::CameraControlSpline()
{
}

bool CameraControlSpline::IsEquivalentTo(const CameraStartInfo& baseStartInfo) const
{
	if (!CameraControl::IsEquivalentTo(baseStartInfo))
		return false;

	const CameraSplineStartInfo& startInfo = *PunPtr<const CameraSplineStartInfo*>(&baseStartInfo);

	StringId64 currentSplineId = GetSplineId();
	if (currentSplineId != startInfo.m_splineId)
		return false;

	// Should we be checking these too?
	//	startInfo.m_splineCameraType
	//	startInfo.m_springMultiplier
	//	startInfo.m_scriptedTime

	return true; // passed all tests
}

CameraBlendInfo CameraControlSpline::CameraStart(const CameraStartInfo& baseStartInfo)
{
	CameraBlendInfo blendInfo = CameraControl::CameraStart(baseStartInfo);

	const CameraSplineStartInfo& startInfo = *PunPtr<const CameraSplineStartInfo*>(&baseStartInfo);

	NdGameObjectHandle hFocusObj = startInfo.GetFocusObject();
	StringId64 cameraSettingsId = startInfo.m_settingsId;

	m_springMultiplier = startInfo.m_springMultiplier;
	m_scriptedTime = startInfo.m_scriptedTime;
	m_hFocusObj = hFocusObj;
	m_flags.m_needsFocus = true;
	m_flags.m_suppressJoypadDuringBlend = true;

	m_splineCamType = startInfo.m_splineCameraType;
	m_splineId = startInfo.m_splineId;
	if (startInfo.m_spawnerId != INVALID_STRING_ID_64)
	{
		const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(startInfo.m_spawnerId);
		if (pSpawner != nullptr)
		{
			cameraSettingsId = pSpawner->GetData<StringId64>(SID("camera-settings"), cameraSettingsId);
		}
	}

	if (cameraSettingsId == INVALID_STRING_ID_64)
		cameraSettingsId = SID("*default-camera-spline-settings*");

	DC::CameraSplineSettings cameraSettings;
	ReadSettingsFromDcAndSpawner(*this, cameraSettings, cameraSettingsId, startInfo.m_spawnerId);

	m_enableShake = true;
	m_enableRotate = !cameraSettings.m_disableLookAround;
	m_noParentSpring = cameraSettings.m_noParentSpring;

	// This value is used in splineacquire, so we need to set it here first
	m_focusObjOffset = cameraSettings.m_focusObjOffset;

	SplineAcquire(startInfo.m_splineId);

	if (m_splineCamType == kSplineCameraScripted)
	{
		m_tt = 0.0f;
		m_oldTT = 0.0f;

		Locator deltaLoc;
		GetDesiredValues(m_currentLocator, m_currentFov, m_currentDistortion, m_currentZeroPlaneDist, deltaLoc, m_currentWaterHeight, true);
	}

	m_cameraShake.Init(GetPlayerIndex(), GameGlobalCameraLocator(GetPlayerIndex()).GetTranslation());
	m_movedBackDistance = 0.0f;

	m_focusObjOffset = cameraSettings.m_focusObjOffset;
	ALWAYS_ASSERT(IsFinite(cameraSettings.m_focusObjOffset));
	ALWAYS_ASSERT(Length(cameraSettings.m_focusObjOffset) < 10000.0f);

	return blendInfo;
}

void CameraControlSpline::ReadSettingsFromSpawner(DC::CameraSplineSettings& cameraSettings, const EntitySpawner* pSpawner)
{
	CameraControl::ReadBaseSettingsFromSpawner(cameraSettings, pSpawner);
}


void CameraControlSpline::SplineAcquire(StringId64 splineId)
{
	m_splineHandle = g_splineManager.FindByName(splineId);
	m_numSplineCameras = 0;

	if (!m_splineHandle.IsValid())
		return;

	// Get spline settings
	const CatmullRom* pSpline = m_splineHandle.ToCatmullRom();
	m_noParentSpring = pSpline->GetTagData<bool>(SID("no-parent-spring"), m_noParentSpring);
	m_enableRotate = !pSpline->GetTagData<bool>(SID("disable-look-around"), !m_enableRotate);
	m_enableShake = !pSpline->GetTagData<bool>(SID("disable-camera-shake"), !m_enableShake);
	m_movementScale = pSpline->GetTagData<float>(SID("movement-scale"), 1.0f);

	//----------------------------------------------------------
	// Gather the cameras for this spline
	LevelAssertNotInUseJanitor lj(FILE_LINE_FUNC);
	const LevelArray& array = EngineComponents::GetLevelMgr()->GetLevels();
	for (LevelArray::const_iterator lit = array.begin(); lit != array.end(); ++lit)
	{
		const Level * pLevel = *lit;
		if (const EntitySpawnerGroup * pSpawners = pLevel->GetEntitySpawners())
		{
			for (EntitySpawnerGroup::SpawnerArray::const_iterator sit = pSpawners->m_spawners.begin();
				 sit != pSpawners->m_spawners.end();
				 ++sit)
			{
				const EntitySpawner* pSpawner = *sit;
				if (pSpawner->GetData<StringId64>(SID("cam-spline-name"), INVALID_STRING_ID_64) == splineId)
				{
					float fov = pSpawner->GetData<float>(SID("fov"), 75.f);
					float distortion = pSpawner->GetData<float>(SID("3D-distortion"), -1.0f);
					float zeroPlaneDist = pSpawner->GetData<float>(SID("3D-zero-plane-dist"), -1.0f);
					I32 index = pSpawner->GetData<I32>(SID("cam-spline-index"), -1);
					float waterHeight = pSpawner->GetData<float>(SID("water-height"), 0.0f);
					if (index != -1)
					{
						m_splineCameras[m_numSplineCameras] = pSpawner;
						m_splineIndexes[m_numSplineCameras] = index;
						m_splineFov[m_numSplineCameras] = fov;
						m_splineDistortion[m_numSplineCameras] = distortion;
						m_splineZeroPlaneDist[m_numSplineCameras] = zeroPlaneDist;
						m_splineWaterHeight[m_numSplineCameras] = waterHeight;
						m_numSplineCameras++;
					}
				}
			}
		}
	}

	// sort the cameras for this spline
	for (int i=0; i<m_numSplineCameras; i++)
	{
		for (int j=i + 1; j<m_numSplineCameras; j++)
		{
			if (m_splineIndexes[j] < m_splineIndexes[i])
			{
				int tempInt = m_splineIndexes[j];
				const EntitySpawner* tempSpawner = m_splineCameras[j];
				float tempFov = m_splineFov[j];
				float tempWaterHeight = m_splineWaterHeight[j];
				float tempDistortion = m_splineDistortion[j];
				float tempZeroPlaneDist = m_splineZeroPlaneDist[j];

				m_splineIndexes[j] = m_splineIndexes[i];
				m_splineCameras[j] = m_splineCameras[i];
				m_splineFov[j] = m_splineFov[i];
				m_splineDistortion[j] = m_splineDistortion[i];
				m_splineZeroPlaneDist[j] = m_splineZeroPlaneDist[i];
				m_splineWaterHeight[j] = m_splineWaterHeight[i];

				m_splineIndexes[i] = tempInt;
				m_splineCameras[i] = tempSpawner;
				m_splineFov[i] = tempFov;
				m_splineDistortion[i] = tempDistortion;
				m_splineZeroPlaneDist[i] = tempZeroPlaneDist;
				m_splineWaterHeight[i] = tempWaterHeight;
			}
		}
	}

	m_distanceSpring.Reset();
	m_ttSpring.Reset();

	m_currentYPid.Reset();
	m_currentYPid.SetGainValues(3.5f, 2.0f, 0.0f, 5.0f);

	const NdGameObject* pGo = m_hFocusObj.ToProcess();

	Point position = kZero;
	if (pGo != nullptr)
	{
		position = pGo->GetSite(SID("LookAtPos")).GetTranslation();
	}
	m_tt = m_splineHandle.ToCatmullRom()->FindGlobalParamClosestToPoint(position, -1);
	m_oldTT = m_tt;
	m_highestTt = m_tt;

	if (m_numSplineCameras > 0)
	{
		Locator deltaLoc;
		GetDesiredValues(m_currentLocator, m_currentFov, m_currentDistortion, m_currentZeroPlaneDist, deltaLoc, m_currentWaterHeight, true);
	}

	m_currentY = m_currentLocator.GetTranslation().Y();
}

float FindGlobalArcLength(const CatmullRom* pSpline, float tt)
{
	CatmullRom::LocalParameter local = pSpline->GlobalParamToLocalParam(tt);

	float totalLength = 0.0f;
	if (local.m_iSegment > 1)
	{
		for (int i=0;i<(local.m_iSegment-1);i++)
		{
			totalLength += pSpline->GetSegmentArcLength(i);
		}
	}

	totalLength += pSpline->CalcLocalArcLength(local.m_iSegment, local.m_u);
	return totalLength;
}

void CameraControlSpline::GetDesiredValues(Locator& loc,
										   float& fov,
										   float& distortion,
										   float& zeroPlaneDist,
										   Locator& deltaLoc,
										   float& waterHeight,
										   bool isTopControl)
{
	if (!m_splineHandle.IsValid())
	{
		SplineAcquire(m_splineId);
	}

	if (!m_splineHandle.IsValid())
	{
		m_numSplineCameras = 0;
		loc = GameGlobalCameraLocator(GetPlayerIndex());
		fov = g_mainCameraInfo[GetPlayerIndex()].GetFov();
		distortion = -1.0f;
		zeroPlaneDist = -1.0f;
		waterHeight = 0.0f;
		return;
	}

	Point position = kZero;
	if (m_hFocusObj.ToProcess() != nullptr)
	{
		position = m_hFocusObj.ToProcess()->GetSite(SID("LookAtPos")).GetTranslation();
		position += m_focusObjOffset;
	}

	float desiredTT = m_splineHandle.ToCatmullRom()->FindGlobalParamClosestToPoint(position, -1);

	ALWAYS_ASSERT(IsFinite(desiredTT));

	m_oldTT = m_tt;

	switch (m_splineCamType)
	{
	case kSplineCameraNonLinear:
		{
			m_tt = desiredTT;
		}
		break;
	case kSplineCameraLinear:
	case kSplineCameraUpright:
		{
			m_tt = m_ttSpring.Track(m_tt, desiredTT, GetProcessDeltaTime(), 10.0f * m_springMultiplier);
			ALWAYS_ASSERT(IsFinite(m_tt));
		}
		break;
	case kSplineCameraScripted:
		{
			if (m_scriptedTime < 0.01f)
			{
				m_tt = 1.0f;
			}
			else
			{
				m_tt = m_tt + FromUPS(1.0f / m_scriptedTime);
			}

			m_tt = Limit01(m_tt);
		}
		break;
	}

	m_highestTt = Max(m_highestTt, m_tt);

	if (m_tt < m_highestTt)
	{
		float highestDistance = FindGlobalArcLength(m_splineHandle.ToCatmullRom(), m_highestTt);
		float curDistance = FindGlobalArcLength(m_splineHandle.ToCatmullRom(), m_tt);

		float distanceBackwards = highestDistance - curDistance;
		m_movedBackDistance = Max(m_movedBackDistance, distanceBackwards);
	}

	if (g_debugSplineCam)
	{
		g_prim.Draw( DebugSphere(position, 0.1f, kColorBlue), kPrimDuration1FramePauseable);

		Point p;
		Vector der;
		Vector tan;
		m_splineHandle.ToCatmullRom()->EvaluateGlobal(m_tt, p, der, tan);
		g_prim.Draw( DebugSphere(p, 0.1f, kColorGreen), kPrimDuration1FramePauseable);
		MsgConPauseable("tt = %f\n", m_tt);

		CatmullRom::DrawOptions options;
		options.m_pCullCenterWs = &position;
		options.m_cullRadius_m = 20.0f;
		options.m_minAccuracyDistance_m = 10.0f;
		options.m_drawPauseable = true;

		m_splineHandle.ToCatmullRom()->Draw(&options);
	}

	if (m_numSplineCameras > 1)
	{
		bool splineIsLooping = m_splineHandle.ToCatmullRom()->IsLooped();

		Point prevControlPoint;
		float prevControlPointTT;
		int start;
		int prevIndex;

		// if the spline is looping, we actual have one extra span to check
		if (splineIsLooping)
		{
			prevControlPoint = m_splineHandle.ToCatmullRom()->GetControlPoint(m_splineIndexes[m_numSplineCameras - 1]);
			prevControlPointTT = m_splineHandle.ToCatmullRom()->FindGlobalParamClosestToPoint(prevControlPoint, -1);
			start = 0;
			prevIndex = m_numSplineCameras - 1;
		}
		else
		{
			prevControlPoint = m_splineHandle.ToCatmullRom()->GetControlPoint(m_splineIndexes[0]);
			prevControlPointTT = m_splineHandle.ToCatmullRom()->FindGlobalParamClosestToPoint(prevControlPoint, -1);
			start = 1;
			prevIndex = 0;
		}

		for (int i=start; i<m_numSplineCameras; i++)
		{
			Point controlPoint = m_splineHandle.ToCatmullRom()->GetControlPoint(m_splineIndexes[i]);
			float controlPointTT = m_splineHandle.ToCatmullRom()->FindGlobalParamClosestToPoint(controlPoint, -1);

			float realPrevControlPointTT = controlPointTT;
			if (controlPointTT < prevControlPointTT)
				controlPointTT += 1.0f;

			if ((m_tt < controlPointTT && m_tt >= prevControlPointTT) || i == m_numSplineCameras - 1 || (m_tt < controlPointTT && i==start && !m_splineHandle.ToCatmullRom()->IsLooped()))
			{
				float d = (controlPointTT - prevControlPointTT);
				if (d < 0.001f)
					d = 0.001f;

				float u = MinMax01((controlPointTT - m_tt) / d);

				if (g_debugSplineCam)
				{
					MsgConPauseable("current index = %d\n", m_splineIndexes[i]);
					MsgConPauseable("u = %f\n", u);
					MsgConPauseable("controlPoint tt = %f\n", controlPointTT);
					MsgConPauseable("prev controlPoint tt = %f\n", prevControlPointTT);

					g_prim.Draw( DebugSphere(controlPoint, 0.2f, kColorWhite), kPrimDuration1FramePauseable);
					g_prim.Draw( DebugSphere(prevControlPoint, 0.2f, kColorWhite), kPrimDuration1FramePauseable);
				}

				// calculate the relative motion of the cameras parents so we can apply it later
				Locator deltaLocPrev(kIdentity);
				const NdGameObject* pPrevParent = m_splineCameras[prevIndex]->GetParentNdGameObject();
				if (pPrevParent != nullptr)
				{
					if (const CompositeBody* pCb = pPrevParent->GetCompositeBody())
					{
						const RigidBody* pRigidBodyBase = pCb->GetParentingBody();
						if (pRigidBodyBase != nullptr)
						{
							Locator prevLoc = pRigidBodyBase->GetPreviousLocatorCm().TransformLocator(m_splineCameras[prevIndex]->GetParentSpaceLocator());
							Locator curLoc = pRigidBodyBase->GetLocatorCm().TransformLocator(m_splineCameras[prevIndex]->GetParentSpaceLocator());
							deltaLocPrev.SetTranslation((curLoc.GetTranslation() - prevLoc.GetTranslation()) + Point(kZero));
							deltaLocPrev.SetRotation(Conjugate(prevLoc.GetRotation()) * curLoc.GetRotation());
						}
					}
				}

				Locator deltaLocCur(kIdentity);
				const NdGameObject* pCurParent = m_splineCameras[i]->GetParentNdGameObject();
				if (pCurParent != nullptr)
				{
					if (const CompositeBody* pCb = pCurParent->GetCompositeBody())
					{
						const RigidBody* pRigidBodyBase = pCb->GetParentingBody();
						if (pRigidBodyBase != nullptr)
						{
							Locator prevLoc = pRigidBodyBase->GetPreviousLocatorCm().TransformLocator(m_splineCameras[i]->GetParentSpaceLocator());
							Locator curLoc = pRigidBodyBase->GetLocatorCm().TransformLocator(m_splineCameras[i]->GetParentSpaceLocator());
							deltaLocCur.SetTranslation((curLoc.GetTranslation() - prevLoc.GetTranslation()) + Point(kZero));
							deltaLocCur.SetRotation(Conjugate(prevLoc.GetRotation()) * curLoc.GetRotation());
						}
					}
				}

				deltaLoc = Lerp(deltaLocPrev, deltaLocCur, 1.0f - u);

				Locator prevWorldSpaceLoc = m_splineCameras[prevIndex]->GetWorldSpaceLocator();
				Locator curWorldSpaceLoc = m_splineCameras[i]->GetWorldSpaceLocator();
				loc = Lerp(prevWorldSpaceLoc, curWorldSpaceLoc, 1.0f - u);
				fov = Lerp(m_splineFov[prevIndex], m_splineFov[i], 1.0f - u);

				float prevDistortion = GetDistortion(m_splineDistortion[prevIndex]);
				float currentDistortion = GetDistortion(m_splineDistortion[i]);
				distortion = Lerp(prevDistortion, currentDistortion, 1.0f - u);
				zeroPlaneDist = Lerp(GetZeroPlaneDist(m_splineZeroPlaneDist[prevIndex]), GetZeroPlaneDist(m_splineZeroPlaneDist[i]), 1.0f - u);
				waterHeight = Lerp(m_splineWaterHeight[prevIndex], m_splineWaterHeight[i], 1.0f - u);
				break;
			}

			prevIndex = i;
			prevControlPoint = controlPoint;
			prevControlPointTT = realPrevControlPointTT;
		}
	}
	else if (m_numSplineCameras > 0)
	{
		loc = m_splineCameras[m_numSplineCameras - 1]->GetWorldSpaceLocator();
		fov = m_splineFov[m_numSplineCameras - 1];
		distortion = GetDistortion(m_splineDistortion[m_numSplineCameras - 1]);
		zeroPlaneDist = GetZeroPlaneDist(m_splineZeroPlaneDist[m_numSplineCameras - 1]);
		waterHeight = m_splineWaterHeight[m_numSplineCameras - 1];
	}
	else
	{
		MsgConScriptError("Error! Spline camera can't find any cameras to associate with %s!\n", DevKitOnly_StringIdToString(m_splineId));
	}

}

void CameraControlSpline::CalculateUpVector()
{
	const NdGameObject* pPlayer = (NdGameObject*)m_hFocusObj.ToProcess();
	if (pPlayer)
	{
		NdGameObject* pPlatform = pPlayer->GetBoundPlatform();

		Quat parentQuat = Quat(kIdentity);

		if (pPlatform)
		{
			parentQuat = pPlatform->GetRotation();
		}

		m_upVector = GetLocalY(parentQuat);
	}
	else
	{
		m_upVector = kUnitYAxis;
	}
}

Vector CameraControlSpline::GetUp() const
{
	return m_upVector;
}

CameraLocation CameraControlSpline::UpdateLocation (const CameraInstanceInfo& info)
{
	CalculateUpVector();

	if (m_numSplineCameras == 0)
	{
		MsgConPauseable("Error! Spline camera can't find any cameras to associate with the current spline %s!\n", DevKitOnly_StringIdToString(m_splineId));
		return info.m_prevLocation;
	}

	Locator desiredLoc;
	Locator deltaLoc(kIdentity);
	float desiredFov;
	float desiredDistortion;
	float desiredZeorPlaneDist;
	float desiredWaterHeight;

	GetDesiredValues(desiredLoc, desiredFov, desiredDistortion, desiredZeorPlaneDist, deltaLoc, desiredWaterHeight, info.m_topControl);


	// apply the delta for the spline cameras parents this frame
	float deltaLocMag = 1000.0f;
	if (GetProcessDeltaTime() > 0.0f)
	{
		deltaLocMag = Length(deltaLoc.GetPosition() - Point(kZero)) / GetProcessDeltaTime();
	}

	if (m_noParentSpring)
	{
		m_currentLocator.SetTranslation(m_currentLocator.GetTranslation() + (deltaLoc.GetTranslation() - Point(kZero)));
		m_currentLocator.SetRot(m_currentLocator.GetRotation() * deltaLoc.GetRotation());
	}

	float delta = Min(Min(Abs(m_oldTT - m_tt), Abs((1.0f + m_oldTT) - m_tt)), Abs(m_oldTT - (m_tt + 1.0f)));

	float distance = Length(desiredLoc.Pos() - m_currentLocator.Pos());
	float newDistance = m_distanceSpring.Track(distance, 0.0f, GetProcessDeltaTime(), 8.0f * m_springMultiplier);
	float u;

	if (distance > 0.00001f)
		u = 1.0f - (newDistance / distance);
	else
		u = 1.0f;

	m_currentLocator = Lerp(m_currentLocator, desiredLoc, u);

	m_currentFov = Lerp(m_currentFov, desiredFov, u);

	m_currentDistortion = Lerp(m_currentDistortion, desiredDistortion, u);
	m_currentZeroPlaneDist = Lerp(m_currentZeroPlaneDist, desiredZeorPlaneDist, u);

	if (m_currentWaterHeight > 0.0f)
	{
		Point pos = m_currentLocator.GetTranslation();
		m_waterDetector.Update(FILE_LINE_FUNC, pos);
		if (m_waterDetector.IsValid())
		{
			float dt = GetProcessDeltaTime();
			float error = (m_waterDetector.WaterSurface().Y() + m_currentWaterHeight) - m_currentY;
			float deltaY = dt*m_currentYPid.Update(error, dt);
			m_currentY += deltaY;

			//m_currentY = m_currentYSpring.Track(m_currentY, m_waterDetector.WaterSurface().Y() + m_currentWaterHeight, dt, 20.0f);
			pos.SetY(m_currentY);
			m_currentLocator.SetTranslation(pos);

			if (g_debugSplineCam)
			{
				MsgConPauseable("Cam Spline Height Above Water: %.2f\n", (float)(m_currentY - m_waterDetector.WaterSurface().Y()));
				MsgConPauseable("Cam Spline Y Velocity: %.2f\n", deltaY/dt);
				MsgConPauseable("Cam Spline Y Error accum: %.2f\n", m_currentYPid.GetErrorAccum());
			}
		}
	}

	Locator newLoc;
	if (m_enableRotate)
	{
		Point position = kZero;
		if ( m_hFocusObj.ToProcess() != nullptr)
		{
			position = m_hFocusObj.ToProcess()->GetSite(SID("LookAtPos")).GetTranslation();
			position += m_focusObjOffset;
		}
		newLoc = m_cameraControl.GetLocator(info.m_joyPad, m_currentLocator, position, GetLocalZ(info.m_prevLocation.GetLocator().Rot()),  m_currentFov, m_movementScale);
	}
	else
		newLoc = m_currentLocator;

	float fov = m_currentFov;
	if (m_enableShake)
	{
		newLoc = m_cameraShake.GetLocator(GetPlayerIndex(), newLoc);
		fov = m_cameraShake.GetFov(GetPlayerIndex(), m_currentFov);
	}

	if (m_splineCamType == kSplineCameraUpright)
	{
		newLoc.SetRotation(QuatFromLookAt(GetLocalZ(newLoc.GetRotation()), kUnitYAxis));
	}


	// return the resulting location
	return CameraLocation(newLoc, fov, m_currentDistortion, m_currentZeroPlaneDist);
}
