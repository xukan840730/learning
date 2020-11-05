/*
* Copyright (c) 2019 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "corelib/math/intersection.h"

#include "ndlib/camera/camera-option.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"

#include "gamelib/camera/camera-strafe-collision.h"
#include "gamelib/camera/camera-strafe-base.h"
#include "gamelib/camera/nd-camera-utility.h"

#include "gamelib/player/nd-player-collision-2.h"

//#include "game/player/player-options.h"
//#include "game/photo-mode/photo-mode-manager.h"

FINAL_CONST bool g_debugDrawCameraCeilingProbe = true;
FINAL_CONST bool g_debugDrawCameraCollGroup[StrafeCameraCollision::kProbeGroupCount] = { true, true };
FINAL_CONST bool g_debugDrawCameraDistOnlyProbes = false;
FINAL_CONST bool g_debugDrawCameraAdjustedProbes = false;
FINAL_CONST bool g_debugDrawCameraNearPlaneProbes = true;
FINAL_CONST bool g_debugDrawCameraSmallObjectsProbes = false;  // PAT CameraBlockNear
FINAL_CONST bool g_debugDrawCameraSafePointProbe = true;
FINAL_CONST bool g_debugDrawCameraStrafePhotoMode = false;

FINAL_CONST I32 g_debugDrawSingleCameraProbe = -1;

FINAL_CONST F32 kHorizontalProbeFanHalfAngleDeg = 35.0f;
const float kMinSpringConstantOut = 0.2f; // FIXME: should go into the .dc file

// =================================================================================================================================
// begin camera-collision.cpp
//

void StrafeCameraCollision::OnCameraStart(const DC::CameraStrafeCollisionSettings* pSettings, const NdGameObject* pFocusObj, float nearPlaneDist)
{
	m_neckAttachIndex = AttachIndex::kInvalid;
	if (pFocusObj != nullptr)
	{
		pFocusObj->TryFindAttachPoint(&m_neckAttachIndex, SID("targNeck"));
	}

	m_pullInSpring.Reset();
	m_desiredPullInPercent = 1.0f;
	m_pullInPercent = 1.0f;			// post-spring
	m_pullOutSpringConst = -1.0f;	// < 0.0f means uninitialized
	m_nearPlaneCloseFactor = 0.0f;

	m_nearestApproach = kLargestFloatSqrt;
	m_nearestApproachSpring.Reset();

	m_cameraNearDist = nearPlaneDist;
	m_cameraNearDistSpring.Reset();

	m_collisionPullInH = 1.0f / Max(pSettings->m_pullInH, 0.01f);
	m_collisionPullInV = 1.0f / Max(pSettings->m_pullInV, 0.01f);

	m_probesValid = false;
	I32 aNumProbes[kProbeGroupCount] = { kNumProbesHoriz, kNumProbesVert };
	for (U32F iGroup = 0; iGroup < kProbeGroupCount; ++iGroup)
	{
		ProbeGroup& group = m_aProbeGroup[iGroup];
		group.m_probeCount = 0;
		group.m_stickFactor[0] = group.m_stickFactor[1] = 0.f;
		group.m_stickFactorSpring[0].Reset();
		group.m_stickFactorSpring[1].Reset();
		group.m_aProbeDebug = nullptr;

#if !FINAL_BUILD
		if (Memory::IsDebugMemoryAvailable())
		{
			I32 numProbes = aNumProbes[iGroup];
			group.m_aProbeDebug = NDI_NEW(kAllocDebug) ProbeGroup::ProbeDebug[numProbes];
		}
#endif
	}

	m_ceilingYAdjust = m_ceilingYAdjustExtra = 0.0f;
	m_timeSinceFadeIn = pSettings->m_fadeOutTimer;

	m_lastPullInTime = TimeFrameNegInfinity();

	m_disablePushedInSafePos = false;
	m_initialized = false;

	m_keepInvalid = false;

	m_numBlockingCharacters = 0;
}

void StrafeCameraCollision::InitState(float pullInPercent)
{
	m_dampedNeckY = 0.0f;
	m_pushedInSafePosBlend = 0.0f;
	m_pushedInSafePosSpring.Reset();

	m_pullInPercent = pullInPercent;
	if (pullInPercent < 1.0f)
		m_lastPullInTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();

	m_initialized = true;

	for (U32F iGroup = 0; iGroup < kProbeGroupCount; ++iGroup)
	{
		ProbeGroup& group = m_aProbeGroup[iGroup];

#if !FINAL_BUILD
		if (group.m_aProbeDebug)
		{
			for (int iProbe = 0; iProbe < group.m_probeCount; ++iProbe)
			{
				group.m_aProbeDebug[iProbe].m_pullInPercent = pullInPercent;
				group.m_aProbeDebug[iProbe].m_actualPullInPercent = pullInPercent;
				group.m_aProbeDebug[iProbe].m_distOnlyPullInPercent = pullInPercent;
			}
		}
#endif
	}
}

///--------------------------------------------------------------------------------------------------------///
void StrafeCameraCollision::ResetNearDist(float nearDist)
{
	GAMEPLAY_ASSERT(nearDist > 0.f);

	m_cameraNearDist = nearDist;
	m_cameraNearDistSpring.Reset();
}

void StrafeCameraCollision::OnKillProcess()
{
#if !FINAL_BUILD
	{
		AllocateJanitor jj(kAllocDebug, FILE_LINE_FUNC);

		for (U32F iGroup = 0; iGroup < kProbeGroupCount; ++iGroup)
		{
			ProbeGroup& group = m_aProbeGroup[iGroup];
			if (group.m_aProbeDebug)
			{
				NDI_DELETE[] group.m_aProbeDebug;
				group.m_aProbeDebug = nullptr;
			}
		}
	}
#endif
}

void StrafeCameraCollision::KickProbes(const CameraControlStrafeBase* pCamera,
									   CameraArc& verticalArc,
									   float currentArc,
									   const CollideFilter& collFilter,
									   float nearPlaneHalfWidthWs,
									   float nearPlaneHalfHeightWs,
									   Vector cameraShakePosOffsetCs)
{
	GAMEPLAY_ASSERT(m_initialized);
	GAMEPLAY_ASSERT(nearPlaneHalfWidthWs > 0);
	GAMEPLAY_ASSERT(nearPlaneHalfHeightWs > 0);
	m_probesValid = false;

	// Instead of casting to the camera position, we are going to cast to the near clip plane.  This solves issues
	// where the look at direction was vastly different from the direction between the safe position and the desired position
	// so the collision spheres could actually "miss" the near clip plane.
	const Vector cameraToTarget = m_targetPosWs - m_cameraPosWs;
	m_cameraForwardWs = SafeNormalize(cameraToTarget, kZero);

	if (m_keepInvalid)
		return;

	// Note: The probe radius calculation assumes that (without camera shake) the near plane of the camera is passing through center of the probe.
	//                 radius calculation can be seen as if we're getting hypotenuse of the near plane
	//
	// (A) Without camera shake                /  (B) With camera shake                                 /  (C) Similarly for 3D shake,         
	//                                         /                                                        /          we calculate
	//    y-axis                               /  y-axis                                                /             dx, dy & dz
	//   |                                     /        |      _______            dx = w/2 + shake-x    /      
	//   |    _______    c = center of         /        |    _|_ _ _  |           dy = h/2 + shake-y    /       
	//   | h |   ,c  |           probe         /        |   ' |_,___'_|_ _ _                            /       Probe radius
	//   |   |_______|                         /        |   '_'_c_ _'_ _ _ _ |- shake-y                 /             = sqrt( dx*dx + dy*dy + dz*dz )
	//   |     w       	  (the rectangle is    /        |   ' '                                         /
	//   |______________    camera near plane) /        |    T                                          / 
	//              x-axis                     /        |    shake-x                                    /   
	//                                         /        |_______________________  x-axis                /       
	//                                         /                                                        /
	//   Probe radius                          /            Probe radius                                /     
	//    = sqrt( (h/2)*(h/2) + (w/2)*(w/2) )  /                = sqrt( dx*dx + dy*dy )                 /    
	//                                         /                                                        /   
	const float dx = nearPlaneHalfWidthWs + fabsf(cameraShakePosOffsetCs.X());
	const float dy = nearPlaneHalfHeightWs + fabsf(cameraShakePosOffsetCs.Y());
	const float dz = fabsf(cameraShakePosOffsetCs.Z());

	float inflateRadius = g_cameraOptions.m_inflateCameraCollision ? 0.2f : 0.0f;
	const float probeRadius = Sqrt(dx*dx + dy*dy + dz*dz) + inflateRadius;

	if (pCamera && ShouldDrawCollision(*pCamera))
	{
		const float probeRadiusWithoutShake = Sqrt((nearPlaneHalfWidthWs * nearPlaneHalfWidthWs) + (nearPlaneHalfHeightWs * nearPlaneHalfHeightWs)) + inflateRadius;
		MsgConPauseable("Extra Probe Radius: %.3f (Camera Shake)\n", probeRadius - probeRadiusWithoutShake);
	}

	KickBlockingCharacterProbe();

	int iProbeHoriVert = 0;
	iProbeHoriVert += PopulateProbesHorizontal(m_aHoriVertProbes, iProbeHoriVert, kNumProbesHoriz, probeRadius);
	iProbeHoriVert += PopulateProbesVertical(m_aHoriVertProbes, iProbeHoriVert, kNumProbesVert, probeRadius, verticalArc, currentArc);

	int iProbeOthers = 0;
	iProbeOthers += PopulateCeilingProbe(pCamera, m_aOtherProbes, iProbeOthers, probeRadius);
	iProbeOthers += PopulateSafePointProbe(m_aOtherProbes, iProbeOthers, probeRadius);
	iProbeOthers += PopulateNearDistProbe(m_aOtherProbes, iProbeOthers);
	iProbeOthers += PopulatePhotoModeProbe(m_aOtherProbes, iProbeOthers, probeRadius, pCamera); // IMPORTANT: use a slightly larger probe to keep the horiz probes from "catching" and pulling in oddly at the extremes

	GAMEPLAY_ASSERT(m_aProbeGroup[kHorizontal].m_probeCount <= kNumProbesHoriz);
	GAMEPLAY_ASSERT(m_aProbeGroup[kVertical].m_probeCount <= kNumProbesVert);

	int actualNumProbesOthers = kNumProbesCeiling + kNumProbesSafePoint + kNumProbesNearDistPoint;
	actualNumProbesOthers += pCamera->IsPhotoMode() ? kNumProbesPhotoMode : 0;

	int actualNumProbesHoriVert = 0;
	for (U32F iGroup = 0; iGroup < kProbeGroupCount; ++iGroup)
		actualNumProbesHoriVert += m_aProbeGroup[iGroup].m_probeCount;

	m_probesValid = (0 != actualNumProbesHoriVert + actualNumProbesOthers);
	if (m_probesValid)
	{
		// HACK to ignore the player's horse when checking for collision in the way of the camera

		const U16 jobOpenFlags = (ICollCastJob::kCollCastSingleSidedCollision | ICollCastJob::kCollCastSingleFrame);

		// kick hori/vert probes.
		if (actualNumProbesHoriVert > 0)
		{
			// Exclude kCameraBlockNear from horizontal and vertical probes because we don't want to include thin features into our statistics
			// We have the extra pull-in probe to check CameraBlockNear collision in front of the camera
			CollideFilter filterWithoutBlockNear = collFilter;
			filterWithoutBlockNear.SetPatExclude(Pat(collFilter.GetPatExclude().m_bits | (Pat::kCameraBlockNearMask | Pat::kStealthVegetationMask)));

			SphereCastJob& job = m_hvJob;
			job.Open(actualNumProbesHoriVert, 1, jobOpenFlags, ICollCastJob::kClientCamera);
			for (int kk = 0; kk < actualNumProbesHoriVert; kk++)
			{
				const SimpleCastProbe& probe = m_aHoriVertProbes[kk];
				job.SetProbeExtents(kk, probe.m_pos, probe.m_vec, 1.0f);
				job.SetProbeRadius(kk, probe.m_radius);
				job.SetProbeFilter(kk, filterWithoutBlockNear);
			}

			job.Kick(FILE_LINE_FUNC, actualNumProbesHoriVert);
		}

		// kick other probes.
		if (actualNumProbesOthers > 0)
		{
			SphereCastJob& job = m_otherJob;

			job.Open(actualNumProbesOthers, 1, jobOpenFlags, ICollCastJob::kClientCamera);
			for (U32F i = 0; i < actualNumProbesOthers; ++i)
			{
				const SimpleCastProbe& probe = m_aOtherProbes[i];
				job.SetProbeExtents(i, probe.m_pos, probe.m_vec, 1.0f);
				job.SetProbeRadius(i, probe.m_radius);
				job.SetProbeFilter(i, collFilter);
			}

			{
				CollideFilter ceilingProbeFilter = collFilter;
				ceilingProbeFilter.SetLayerInclude(collFilter.GetLayerInclude() & ~(Collide::kLayerMaskWater));
				ceilingProbeFilter.SetPatExclude(Pat(collFilter.GetPatExclude().m_bits | Pat::kStealthVegetationMask));
				job.SetProbeFilter(0, ceilingProbeFilter);
			}

			if (m_safePointProbeIgnoreWater)
			{
				CollideFilter safePointProbeFilter = collFilter;
				safePointProbeFilter.SetLayerInclude(collFilter.GetLayerInclude() & ~(Collide::kLayerMaskWater));
				job.SetProbeFilter(kNumProbesCeiling, safePointProbeFilter);
			}

			if (m_nearPlaneProbeIgnoreWater)
			{
				CollideFilter nearDistProbeFilter = collFilter;
				nearDistProbeFilter.SetLayerInclude(collFilter.GetLayerInclude() & ~(Collide::kLayerMaskWater));
				job.SetProbeFilter(kNumProbesCeiling + 1, nearDistProbeFilter);
			}

			job.Kick(FILE_LINE_FUNC, actualNumProbesOthers);
		}

		// The spherecast which goes from camera-idle to safe position will benefit from NOT HAVING the kCollCastSingleSidedCollision & HAVING kCollCastAllStartPenetrations
		//   Because the start position for that sphere cast could be inside a geometry.
		//   In that case the sphere cast has to detect the back face of that geometry!
		const U16 smallObjectsJobOpenFlag = (jobOpenFlags | ICollCastJob::kCollCastAllStartPenetrations) & (~ICollCastJob::kCollCastSingleSidedCollision);
		
		// We'll kick the small object job
		PopulateSmallObjectProbes(m_smallObjectProbes, probeRadius);
		m_smallObjectsJob.Open(kSmallObjectProbesCount, 1, smallObjectsJobOpenFlag, ICollCastJob::kClientCamera);

		CollideFilter filterOnlySmallObjects = collFilter;
		filterOnlySmallObjects.SetPatInclude(Pat(Pat::kCameraBlockNearMask));

		for (U32F iSmall = 0; iSmall < kSmallObjectProbesCount; iSmall++)
		{
			m_smallObjectsJob.SetProbeExtents(iSmall, m_smallObjectProbes[iSmall].m_pos, m_smallObjectProbes[iSmall].m_vec, 1.0f);
			m_smallObjectsJob.SetProbeRadius(iSmall, m_smallObjectProbes[iSmall].m_radius);
			m_smallObjectsJob.SetProbeFilter(iSmall, filterOnlySmallObjects);
		}

		m_smallObjectsJob.Kick(FILE_LINE_FUNC, kSmallObjectProbesCount);
	}
}

I32 StrafeCameraCollision::PopulatePullInProbe(const CameraControlStrafeBase* pCamera, SimpleCastProbe probes[], const int iStartProbe, F32 fRadius)
{
	SimpleCastProbe& curProbe = probes[iStartProbe];

	const Scalar angleRad = 0.0f;
	const Vector pivotToProbeWs = m_cameraPosWs - m_horizontalPivotWs;

	const Point arcPointWs = m_horizontalPivotWs + pivotToProbeWs;

	// Instead of casting to the camera position, we are going to cast to the near clip plane.  This solves issues
	// where the look at direction was vastly different from the direction between the safe position and the desired position
	// so the collision spheres could actually "miss" the near clip plane.
	const Vector nearPlaneAdjustWs = m_cameraForwardWs * m_cameraNearDist;
	//if (g_drawT1StrafeCamColl && g_debugDrawCameraCollGroup[iGroup])
	//	g_prim.Draw(DebugLine(arcPointWs, cameraForwardWs, kColorYellow, 2.0f, kPrimEnableHiddenLineAlpha));

	const Point finalSafePos = GetFinalSafePos();
	curProbe.m_pos = finalSafePos;
	curProbe.m_radius = fRadius;

	Vector fullVec = (arcPointWs + nearPlaneAdjustWs) - finalSafePos;
	F32 fullLen = Length(fullVec);
	curProbe.m_vec = Min(1.0f, m_pullInPercent + 0.4f / fullLen) * fullVec;

	return 1;
}

I32 StrafeCameraCollision::PopulateSmallObjectProbes(SimpleCastProbe probes[], F32 fRadius)
{
	int numProbesPopulated = 0;

	SimpleCastProbe& probeSafeToCamera = probes[kSafePosToCamera];
	SimpleCastProbe& probeCameraToSafe = probes[kCameraToSafePos];

	const Scalar angleRad = 0.0f;
	const Vector pivotToProbeWs = m_cameraPosWs - m_horizontalPivotWs;

	const Point arcPointWs = m_horizontalPivotWs + pivotToProbeWs;

	// Instead of casting to the camera position, we are going to cast to the near clip plane.  This solves issues
	// where the look at direction was vastly different from the direction between the safe position and the desired position
	// so the collision spheres could actually "miss" the near clip plane.
	const Vector nearPlaneAdjustWs = m_cameraForwardWs * m_cameraNearDist;

	const Point finalSafePos = GetFinalSafePos();
	probeSafeToCamera.m_pos = finalSafePos;
	probeSafeToCamera.m_radius = fRadius;

	const Vector fullVec = (arcPointWs + nearPlaneAdjustWs) - finalSafePos;
	probeSafeToCamera.m_vec = fullVec;

	probeCameraToSafe.m_pos		=  probeSafeToCamera.m_pos + probeSafeToCamera.m_vec;	// Start pos is same as the probeSafeToCamera's END POS
	probeCameraToSafe.m_vec		= -probeSafeToCamera.m_vec;								//   and we want to go from probeSafeToCamera's end --to--> start. 
	probeCameraToSafe.m_radius	=  probeSafeToCamera.m_radius;
	numProbesPopulated++;

	return numProbesPopulated;
}

I32 StrafeCameraCollision::PopulateCeilingProbe(const CameraControlStrafeBase* pCamera, SimpleCastProbe probes[], const int iStartProbe, F32 fRadius)
{
	SimpleCastProbe& curProbe = probes[iStartProbe];

	const Character* pChar = m_hCharacter.ToProcess();
	// super hack for camera on boat
	if (!pChar)
		pChar = Character::FromProcess(EngineComponents::GetNdGameInfo()->GetPlayerGameObject());

	if (!pChar)
		return 0;

	float radiusAdjust = 0.5f;

	curProbe.m_radius = fRadius * radiusAdjust; // use a thinner probe to prevent spurious collisions during climb-ups etc. (BUT we must account for this reduction below...)
	const Scalar radius(curProbe.m_radius);

	// cast to a raised safe position, which has the effect of keeping the REAL safe pos this distance BELOW the ceiling collision point (if any)
	m_ceilingYAdjustExtra = fRadius * (1.f / radiusAdjust);
	const Point raisedSafePosWs = pCamera->GetRawSafePos() + m_cameraUpDirWs * Scalar(m_ceilingYAdjustExtra + curProbe.m_radius + 0.15f);

	Point playerAlignWs = pChar->GetTranslation();
	float extraVertOffset = 0.0f;
	if (pChar->IsCrouched()) // try to adjust ceiling probe to account for safe pos that is offset from player align... but can't go too far or we'll start getting artificial ceiling probe hits!
	{
		Point safePosWs = PointFromXzAndY(pCamera->GetCameraSafePos(), playerAlignWs);
		playerAlignWs = Lerp(playerAlignWs, safePosWs, SCALAR_LC(0.30f));

	}
	if (pChar->IsInStealthVegetation())
	{
		if (!pChar->IsProne())
		{
			if (pChar->IsCrouched() || pChar->IsState(SID("Evade")))
			{
				extraVertOffset = 0.5f;
			}
		}
	}
	Point lookAtPosWs = pChar->GetSite(SID("LookAtPos")).GetTranslation();

	// if player is swimming, player's neck might be lower than align, using neck pos to calc verticalProj is dangerous.
	if (m_neckAttachIndex != AttachIndex::kInvalid && !pChar->IsSwimming())
	{
		Point neckPosWs = pChar->GetAttachSystem()->GetLocator(m_neckAttachIndex).GetTranslation();
		Scalar neckY = Max(neckPosWs.Y(), pChar->GetTranslation().Y() + SCALAR_LC(0.5f));
		neckPosWs.SetY(neckY);
		lookAtPosWs = PointFromXzAndY(lookAtPosWs, neckPosWs);
	}

	Scalar verticalProj = Dot(lookAtPosWs - playerAlignWs, m_cameraUpDirWs);
	const Scalar topProj = Dot(raisedSafePosWs - playerAlignWs, m_cameraUpDirWs);
	verticalProj = Min(verticalProj, topProj - radius); // make sure to always cast at least one radius worth of distance
	const Point probeStartWs = playerAlignWs + m_cameraUpDirWs * (verticalProj + extraVertOffset);
	curProbe.m_pos = probeStartWs;

	const Vector startToSafeWs = raisedSafePosWs - probeStartWs;
	curProbe.m_vec = m_cameraUpDirWs * Dot(startToSafeWs, m_cameraUpDirWs);

	return 1;
}

I32 StrafeCameraCollision::PopulateSafePointProbe(SimpleCastProbe probes[], const int iStartProbe, F32 fRadius)
{
	SimpleCastProbe& curProbe = probes[iStartProbe];

	const float radius = fRadius;
	const float probeAdjustDist = fRadius - radius + 0.05f;

	curProbe.m_pos = m_pushedInSafePosWs - Vector(0.0f, 0.05f, 0.0f);	// Drop the probe a bit since the safe pos is sometimes touching the ceiling
	curProbe.m_vec = m_safePosWs - curProbe.m_pos;
	curProbe.m_pos += probeAdjustDist * SafeNormalize(curProbe.m_vec, kZero);
	curProbe.m_radius = radius;

	return 1;
}

I32 StrafeCameraCollision::PopulateNearDistProbe(SimpleCastProbe probes[], const int iStartProbe)
{
	SimpleCastProbe& curProbe = probes[iStartProbe];

	const Character* pChar = m_hCharacter.ToProcess();
	// super hack for camera on boat
	if (!pChar)
		pChar = Character::FromProcess(EngineComponents::GetNdGameInfo()->GetPlayerGameObject());

	if (!pChar)
		return 0;

	Point probeStartPosWs = m_horizontalPivotWs;
	if (m_neckAttachIndex != AttachIndex::kInvalid && !pChar->IsSwimming())
	{
		const Point neckPosWs = pChar->GetAttachSystem()->GetLocator(m_neckAttachIndex).GetTranslation();
		const Scalar neckY = Max(neckPosWs.Y(), pChar->GetTranslation().Y() + SCALAR_LC(0.5f));

		m_dampedNeckY = MinMax(m_dampedNeckY, (float)neckY - 0.07f, (float)neckY + 0.07f);
		//MsgCon("Neck: %.3f\n", m_dampedNeckY);
		probeStartPosWs.SetY(m_dampedNeckY);
	}

	curProbe.m_pos = probeStartPosWs;
	curProbe.m_vec = Vector(0.0f, 0.5f, 0.0f);
	curProbe.m_radius = 0.2f;

	return 1;
}

I32 StrafeCameraCollision::PopulatePhotoModeProbe(SimpleCastProbe probes[], const int iStartProbe, F32 fRadius, const CameraControlStrafeBase* pCamera)
{
	if (pCamera->IsPhotoMode())
	{
		Vector cameraDirXZ = pCamera->GetYawController().GetCurrentDirWs();
		ALWAYS_ASSERT(IsFinite(cameraDirXZ));

		const Vector& forward = cameraDirXZ;
		const Vector up(kUnitYAxis);
		const Vector left(Normalize(Cross(up, forward)));

		const Vector offsetX(left * Scalar(m_dcOffsetMaxXzRadius));
		const Vector offsetMinY(up * Scalar(m_dcOffsetMinY));
		const Vector offsetMaxY(up * Scalar(m_dcOffsetMaxY));
		const Point preOffsetTargetPos = pCamera->GetRawSafePos() - pCamera->GetOffsetForPhotoMode();	// Remove the offset, and start the probes from there.
		const Point probeOriginAtPlayer = PointFromXzAndY(pCamera->GetBaseTargetPos(), preOffsetTargetPos);

		probes[iStartProbe + 0].m_pos = probeOriginAtPlayer;
		probes[iStartProbe + 0].m_vec = -offsetX;
		probes[iStartProbe + 0].m_radius = fRadius * 1.1f;

		probes[iStartProbe + 1].m_pos = probeOriginAtPlayer;
		probes[iStartProbe + 1].m_vec = offsetX;
		probes[iStartProbe + 1].m_radius = fRadius * 1.1f;

		probes[iStartProbe + 2].m_pos = probeOriginAtPlayer;
		probes[iStartProbe + 2].m_vec = offsetMinY;
		probes[iStartProbe + 2].m_radius = fRadius * 1.1f;

		probes[iStartProbe + 3].m_pos = probeOriginAtPlayer;
		probes[iStartProbe + 3].m_vec = offsetMaxY;
		probes[iStartProbe + 3].m_radius = fRadius * 1.1f;

		return kNumProbesPhotoMode;
	}
	return 0;
}

I32 StrafeCameraCollision::PopulateProbesHorizontal(SimpleCastProbe probes[], const int iStartProbe, const int numProbes, F32 radius)
{
	const I32F iGroup = kHorizontal;

	const Scalar nearDist(m_cameraNearDist);
	const float fNumProbes = (float)numProbes;
	ALWAYS_ASSERT((numProbes & 1u) != 0u); // odd number of probes

	const Vector pivotToCamera = m_cameraPosWs - m_horizontalPivotWs;

	const float fProbeSpacing = ((numProbes > 1) ? (1.0f / (fNumProbes - 1.0f)) : 1.0f);

	int iProbe;
	float fiProbe;
	for (iProbe = 0, fiProbe = 0.0f; iProbe < numProbes; ++iProbe, fiProbe += 1.0f)
	{
		SimpleCastProbe& curProbe = probes[iStartProbe + iProbe];

		const Scalar arc = 2.0f*(fiProbe * fProbeSpacing - 0.5f) * 1.0f/m_collisionPullInH; // range: [-1.0f, +1.0f], times scaling
		const Scalar angleRad = arc * Scalar(DEGREES_TO_RADIANS(kHorizontalProbeFanHalfAngleDeg)); // range: [-halfAngle, +halfAngle], times scaling
		const Quat rot = QuatFromAxisAngle(m_cameraUpDirWs, angleRad);
		const Vector pivotToProbeWs = Rotate(rot, pivotToCamera);

		const Point arcPointWs = m_horizontalPivotWs + pivotToProbeWs;

		// Instead of casting to the camera position, we are going to cast to the near clip plane.  This solves issues
		// where the look at direction was vastly different from the direction between the safe position and the desired position
		// so the collision spheres could actually "miss" the near clip plane.
		const Vector cameraForwardWs = Rotate(rot, m_cameraForwardWs);
		const Vector nearPlaneAdjustWs = cameraForwardWs * nearDist;
		//if (g_drawT1StrafeCamColl && g_debugDrawCameraCollGroup[iGroup])
		//	g_prim.Draw(DebugLine(arcPointWs, cameraForwardWs, kColorYellow, 2.0f, kPrimEnableHiddenLineAlpha));

		const Point finalSafePos = GetFinalSafePos();
		curProbe.m_pos = finalSafePos;
		curProbe.m_radius = radius;
		curProbe.m_vec = (arcPointWs + nearPlaneAdjustWs) - finalSafePos;
	}

	m_aProbeGroup[iGroup].m_fCurrentArc = 0.5f; // we are always at the center of the 'arc' horizontally
	m_aProbeGroup[iGroup].m_fProbeSpacing = fProbeSpacing;
	m_aProbeGroup[iGroup].m_probeCount = numProbes;
	m_aProbeGroup[iGroup].m_iStartProbe = iStartProbe;
	m_aProbeGroup[iGroup].m_pullInDistMult = m_collisionPullInH;

	return numProbes;
}

I32 StrafeCameraCollision::PopulateProbesVertical(SimpleCastProbe probes[], const int iStartProbe, const int numProbes,
	F32 radius,
	CameraArc& verticalArc,
	float fCurrentArc)
{
	const I32F iGroup = kVertical;

	const Scalar nearDist(m_cameraNearDist);

	ALWAYS_ASSERT((numProbes & 1u) != 0u); // odd number of probes
	const float fNumProbes = (float)numProbes;

	const Vector centerToCameraWs = m_cameraPosWs - m_arcCenterWs;
	const Scalar y = Dot(centerToCameraWs, m_cameraUpDirWs);
	const Vector centerToCameraY = y * m_cameraUpDirWs;
	const Vector centerToCameraXZ = centerToCameraWs - centerToCameraY;
	const Vector centerToCameraDirXZ = SafeNormalize(centerToCameraXZ, kZero);

	const float fNumProbesMinus1 = (fNumProbes - 1.0f);
	const float fHalfNumProbes = fNumProbesMinus1 * 0.5f;
	const float fProbeSpacing = ((fNumProbes > 1.0f) ? (1.0f / fNumProbesMinus1) : 1.0f) / m_collisionPullInV;

	// effectively march up and down along arc, starting from the current arc point,
	// until we either run out of probes or hit the ends of the arc

	int actualNumProbes = 0;

	int iProbe = 0;
	float fArc = fCurrentArc - 0.5f / m_collisionPullInV;
	for (; iProbe < numProbes; ++iProbe, fArc += fProbeSpacing)
	{
		SimpleCastProbe& curProbe = probes[iStartProbe + actualNumProbes];
		++actualNumProbes;

		const float fArcClamped = MinMax01(fArc);
		const Vec2 offset = verticalArc.CalcCameraOffset(fArcClamped);
		const Vector offsetWs = CameraArc::OffsetToWorldSpace(offset, m_arcCenterWs, centerToCameraDirXZ, m_cameraUpDirWs);
		const Point arcPointWs = m_arcCenterWs + offsetWs;

		// Instead of casting to the camera position, we are going to cast to the near clip plane.  This solves issues
		// where the look at direction was vastly different from the direction between the safe position and the desired position
		// so the collision spheres could actually "miss" the near clip plane.
		const Vector cameraToTarget = m_targetPosWs - arcPointWs;
		const Vector cameraForwardWs = SafeNormalize(cameraToTarget, kZero);
		const Vector nearPlaneAdjustWs = cameraForwardWs * nearDist;
		//if (g_drawT1StrafeCamColl && g_debugDrawCameraCollGroup[iGroup])
		//	g_prim.Draw(DebugLine(arcPointWs, cameraForwardWs, kColorOrange, 2.0f, kPrimEnableHiddenLineAlpha));

		const float fArcDistFromCenter = MinMax01(Abs(fCurrentArc - fArc) * 2.0f);

		const Point finalSafePos = GetFinalSafePos();
		curProbe.m_pos = finalSafePos;
		curProbe.m_radius = radius; //LerpScale(0.0f, 1.0f, radius, radius * 0.5f, fArcDistFromCenter);
		curProbe.m_vec = (arcPointWs + nearPlaneAdjustWs) - finalSafePos;
	}

	m_aProbeGroup[iGroup].m_fCurrentArc = fCurrentArc;
	m_aProbeGroup[iGroup].m_fProbeSpacing = fProbeSpacing;
	m_aProbeGroup[iGroup].m_probeCount = actualNumProbes;
	m_aProbeGroup[iGroup].m_iStartProbe = iStartProbe;
	m_aProbeGroup[iGroup].m_pullInDistMult = m_collisionPullInV;

	return actualNumProbes;
}

void StrafeCameraCollision::KickBlockingCharacterProbe()
{
	const Vector pivotToProbeWs = m_cameraPosWs - m_horizontalPivotWs;
	const Point arcPointWs = m_horizontalPivotWs + pivotToProbeWs;

	const Vector nearPlaneAdjustWs = m_cameraForwardWs * m_cameraNearDist;
	
	const Point finalSafePos = GetFinalSafePos();

	Vector fullVec = (arcPointWs + nearPlaneAdjustWs) - finalSafePos;
	F32 fullLen = Length(fullVec);

	const Point startPt = finalSafePos;
	const Vector probeVec = Min(1.0f, m_pullInPercent + 0.4f / fullLen) * fullVec;

	m_blockingCharacterJob.Open(1, kMaxBlockingCharacters, ICollCastJob::kCollCastSingleSidedCollision | ICollCastJob::kCollCastSingleFrame, ICollCastJob::kClientCamera);
	m_blockingCharacterJob.SetProbeExtents(0, startPt, startPt + probeVec);
	m_blockingCharacterJob.SetProbeFilter(0, CollideFilter(Collide::kLayerMaskNpc));
	m_blockingCharacterJob.Kick(FILE_LINE_FUNC, 1);
}

void StrafeCameraCollision::GatherBlockingCharacterProbe()
{
	if (m_blockingCharacterJob.IsValid())
	{
		m_blockingCharacterJob.Wait();
		m_numBlockingCharacters = 0;
		for (int iContact = 0; iContact < kMaxBlockingCharacters; ++iContact)
		{
			if (m_blockingCharacterJob.IsContactValid(0, iContact))
			{
				const Character* pBlockingCharacter = Character::FromProcess(m_blockingCharacterJob.GetContactObject(0, iContact).m_hGameObject.ToProcess());
				if (pBlockingCharacter)
					m_ahBlockingCharacters[m_numBlockingCharacters++] = pBlockingCharacter;
			}
		}
		m_blockingCharacterJob.Close();
	}
}

bool StrafeCameraCollision::IsCharacterBlockingCamera(const Character* pCharacter) const
{
	for (int i = 0; i < m_numBlockingCharacters; ++i)
	{
		if (pCharacter == m_ahBlockingCharacters[i].ToProcess())
			return true;
	}
	return false;
}

void StrafeCameraCollision::ComputeGroupPullInPercent(SimpleCastProbe probes[], ProbeGroup& group, ProbeGroupIndex iGroup, const CamWaterPlane* pWaterPlane)
{
	group.m_pullInPercent = 1.0f;
	group.m_urgency = 0.0f;

	const float fNumProbesMinus1 = ((float)group.m_probeCount - 1.0f);
	const float fHalfNumProbes = fNumProbesMinus1 * 0.5f;

	int iProbe;
	float fiProbe;
	float fStartArc = group.m_fCurrentArc - 0.5f / group.m_pullInDistMult;
	for (iProbe = 0, fiProbe = 0.0f; iProbe < group.m_probeCount; ++iProbe, fiProbe += 1.0f)
	{
		float actualPullInPercent;
		{
			int index = group.m_iStartProbe + iProbe;
			GAMEPLAY_ASSERT(index >= 0 && index < kNumProbesHoriVert);
			const SimpleCastProbe& curProbe = probes[index];
			actualPullInPercent = (curProbe.m_cc.m_time >= 0.0f) ? curProbe.m_cc.m_time : 1.0f;

			// apply water plane probe.
			if (pWaterPlane && pWaterPlane->m_waterFound)
			{
				float disTT = DoSphereProbeAgainstVirtualWall(curProbe.m_pos, curProbe.m_vec, curProbe.m_radius, pWaterPlane->m_plane, true, nullptr, nullptr);
				if (disTT >= 0.f && disTT < actualPullInPercent)
				{
					actualPullInPercent = disTT;
				}
			}
		}

		const float fArc = fStartArc + fiProbe * group.m_fProbeSpacing;
		const float fArcClamped = MinMax01(fArc);
		const float fSignedArcDistFromCenter = fArc - group.m_fCurrentArc; // negative below camera, positive above
		const float fArcDistFromCenter = Abs(fSignedArcDistFromCenter);

		// pretend we didn't pull in as far when we're farther from the center
		const float fNormDistFromCenter = MinMax01(fArcDistFromCenter * 2.0f * group.m_pullInDistMult);

		// select the appropriate stick direction factor for this probe
		const int iDir = (fSignedArcDistFromCenter > 0.0f) ? 1 : 0;
		const float fStickFactor = group.m_stickFactor[iDir];

		// the stick factor only has an effect on probes distant from the center
		// closer to the center, act like the stick is pushed fully (for fastest collision response)
		const float fDistAdjStickFactor = LerpScale(0.0f, 1.0f, 1.0f, fStickFactor, fNormDistFromCenter);
		const float fDistFactor = 1.0f - fNormDistFromCenter;
		const float fNetFactor = LerpScale(0.0f, 1.0f, 0.3f, 1.0f, fDistAdjStickFactor) * fDistFactor;

		const float pullInPercent = Lerp(1.0f, actualPullInPercent, fNetFactor); // factor=0: not pulled in at all, factor=1: full pull-in
		const float urgency = (1.0f - Pow(fNormDistFromCenter, 2.3f / 10.0f)); // EXPERIMENTING HERE (this urgency curve helps reduce sudden pop ins)

		if (group.m_aProbeDebug)
		{
			group.m_aProbeDebug[iProbe].m_actualPullInPercent = actualPullInPercent;
			group.m_aProbeDebug[iProbe].m_distOnlyPullInPercent = Lerp(1.0f, actualPullInPercent, fDistFactor);
			group.m_aProbeDebug[iProbe].m_pullInPercent = pullInPercent;
			group.m_aProbeDebug[iProbe].m_normDistFromCenter = fNormDistFromCenter;
		}

		if (group.m_pullInPercent > pullInPercent)
		{
			group.m_pullInPercent = pullInPercent;

			if (group.m_urgency < urgency)
				group.m_urgency = urgency;	// IMPORTANT so side probes don't artifically reduce our urgency!
		}
	}
}

void StrafeCameraCollision::ComputeGroupPullInPercentPhotoMode(SimpleCastProbe probes[], ProbeGroup& group, ProbeGroupIndex iGroup)
{
	group.m_pullInPercent = 1.0f;
	group.m_urgency = 0.99f;

	int iProbe = group.m_probeCount / 2;
	float fiProbe = (float)iProbe;
	float fStartArc = group.m_fCurrentArc - 0.5f;

	{
		const SimpleCastProbe& curProbe = probes[group.m_iStartProbe + iProbe];

		const float actualPullInPercent = (curProbe.m_cc.m_time >= 0.0f) ? curProbe.m_cc.m_time : 1.0f;

		if (group.m_aProbeDebug)
		{
			group.m_aProbeDebug[iProbe].m_actualPullInPercent = actualPullInPercent;
			group.m_aProbeDebug[iProbe].m_distOnlyPullInPercent = actualPullInPercent;
			group.m_aProbeDebug[iProbe].m_pullInPercent = actualPullInPercent;
			group.m_aProbeDebug[iProbe].m_normDistFromCenter = 0.0f;
		}

		group.m_pullInPercent = actualPullInPercent;
	}
}

static void AdjustForSnow(SimpleCastProbe aProbe[], int count)
{
	const DC::SurfaceType snowSurfaceType = EngineComponents::GetNdPhotoModeManager()->GetSnowSurfaceType();

	for (int i = 0; i < count; ++i)
	{
		if (aProbe[i].m_cc.m_time >= 0.0f && aProbe[i].m_cc.m_pat.GetSurfaceType() == snowSurfaceType)
		{
			static float kSnowDepth = 0.45f; // meters (better to assume too deep than not deep enough!)
			static float kMaxSlopeDegrees = 30.0f;
			static bool g_debugDrawSnowAdjustment = false;

			const Scalar ksSnowDepth = Scalar(kSnowDepth);
			const Vector normalWs = aProbe[i].m_cc.m_normal;
			const Scalar slopeCosine = Dot(normalWs, kUnitYAxis);
			const Point contactWs = aProbe[i].m_cc.m_contact;
			const Scalar probeLen = Length(aProbe[i].m_vec);

			if (slopeCosine >= Cos(DegreesToRadians(kMaxSlopeDegrees /*+ 5.0f*/))) // ignore collisions with any surface that isn't roughly horizontal
			{
				const Vector probeDirWs = aProbe[i].m_vec * SCALAR_LC(-1.0f) * SafeRecip(probeLen, SCALAR_LC(1.0f));
				const Scalar probeDist = probeLen * aProbe[i].m_cc.m_time;
				const Scalar cosine = Dot(normalWs, probeDirWs);

				if (cosine > SCALAR_LC(0.0f)) // ignore back-facing collisions
				{
					const Scalar invCosine = SafeRecip(cosine, SCALAR_LC(1.0f));
					const Scalar adjustmentLen = ksSnowDepth * invCosine; // * LerpScale(Cos(DegreesToRadians(kMaxSlopeDegrees + 5.0f)), Cos(DegreesToRadians(kMaxSlopeDegrees)),
					                                                      //           0.0f, 1.0f, slopeCosine);
					const Scalar adjustmentLenClamped = Min(adjustmentLen, probeDist);
					const Vector adjustmentAlongRayWs = probeDirWs * adjustmentLenClamped;

					const Scalar invProbeLen = SafeRecip(probeLen, SCALAR_LC(0.0f));
					const Scalar adjustmentInTime = adjustmentLenClamped * invProbeLen;

					const Point contactAboveSnowWs = contactWs + normalWs * ksSnowDepth;
					const Point contactOnSnowWs = contactWs + adjustmentAlongRayWs;

					aProbe[i].m_cc.m_contact = contactOnSnowWs;
					aProbe[i].m_cc.m_time -= adjustmentInTime;
					if (aProbe[i].m_cc.m_time < 0.0f)
						aProbe[i].m_cc.m_time = 0.0f; // should never occur due to clamped adjustment above

					if (g_debugDrawSnowAdjustment)
					{
					//	char angleStr[64];
					//	snprintf(angleStr, sizeof(angleStr), "%.1f deg", RadiansToDegrees(Acos(slopeCosine)));
					//	g_prim.Draw(DebugString(contactWs, angleStr, 0, kColorGray, 0.5f));

						g_prim.Draw(DebugSphere(contactWs, 0.01f, kColorYellow, PrimAttrib(kPrimDisableHiddenLineAlpha)));
						g_prim.Draw(DebugSphere(contactOnSnowWs, 0.01f, kColorCyan, PrimAttrib(kPrimDisableHiddenLineAlpha)));
						g_prim.Draw(DebugLine(aProbe[i].m_pos, aProbe[i].m_vec, kColorYellow, 2.0f, PrimAttrib(kPrimDisableHiddenLineAlpha)));
						g_prim.Draw(DebugLine(contactWs, contactAboveSnowWs, kColorMagenta, 2.0f, PrimAttrib(kPrimDisableHiddenLineAlpha)));
						g_prim.Draw(DebugLine(contactWs, contactOnSnowWs, kColorCyan, 2.0f, PrimAttrib(kPrimDisableHiddenLineAlpha)));
						g_prim.Draw(DebugLine(contactAboveSnowWs, contactOnSnowWs, kColorGray, 2.0f, PrimAttrib(kPrimDisableHiddenLineAlpha)));
					}
				}
			}
		}
	}
}

float StrafeCameraCollision::ComputePullInPercent(CameraControlStrafeBase* pCamera, 
												  const DC::CameraStrafeCollisionSettings* pSettings,
												  float dt,
												  bool bTopControl, 
												  float defaultNearDist, 
												  const CamWaterPlane& waterPlane, 
												  bool collideWaterPlane, 
												  bool ignoreCeilingProbe, 
												  bool photoMode)
{
	// photomode camera needs collision and should ignore "script disable camera collision"
	if (g_cameraOptions.m_disableFollowCameraCollision && !photoMode)
	{
		return 1.0f;
	}

	const TimeFrame curTime = EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetCurTime();

	float springConstantIn = 50.0f;
	float effSpringConstantIn = springConstantIn;
	float pullInUrgency = 0.0f;
	float desiredPullInPercent = 1.0f;

	if (!m_probesValid)
	{
		// If we don't have good ray cast data, don't even try to compute the camera location, just assume no collision
		// This is to prevent the camera popping every time the character punches.
		m_pullInPercent = 1.0f;
		m_ceilingYAdjust = 0.0f;
	}
	else
	{
		// gather hori/vert probes.
		{
			SimpleCastGather(m_hvJob, m_aHoriVertProbes, kNumProbesHoriVert);
			if (pCamera->IsPhotoMode())
			{
				AdjustForSnow(&m_aHoriVertProbes[0], kNumProbesHoriVert);
			}
		}

		// gather other probes.
		{
			const int numProbes = pCamera->IsPhotoMode() ? kNumProbesOthers : (kNumProbesOthers - kNumProbesPhotoMode);
			
#if !FINAL_BUILD
			const bool drawAny = g_strafeCamDbgOptions.m_drawT1Coll && 
								(
									g_debugDrawCameraCeilingProbe ||
									g_debugDrawCameraSafePointProbe ||
									g_debugDrawCameraNearPlaneProbes ||
									g_debugDrawCameraStrafePhotoMode
								);

			if (drawAny && 
				m_otherJob.IsValid())
			{
				m_otherJob.Wait();

				ICollCastJob::DrawConfig cfg(kPrimDuration1FramePauseable, 0, kNumProbesOthers);
				cfg.m_hitColor = kColorRedTrans;

				I32F iStartProbe = 0;
				if (g_debugDrawCameraCeilingProbe)
				{
					cfg.m_noHitColor = Color(0.6f, 0.4f, 0.5f, 0.33f);
					cfg.m_startProbe = iStartProbe;
					cfg.m_endProbe = iStartProbe + kNumProbesCeiling;

					m_otherJob.DebugDraw(cfg);
				}

				iStartProbe += kNumProbesCeiling;
				if (g_debugDrawCameraSafePointProbe)
				{
					cfg.m_startProbe = iStartProbe;
					cfg.m_endProbe = kNumProbesSafePoint + kNumProbesSafePoint;
					cfg.m_noHitColor = Color(0.4f, 0.6f, 0.5f, 0.33f);

					m_otherJob.DebugDraw(cfg);
				}

				iStartProbe += kNumProbesSafePoint;
				if (g_debugDrawCameraNearPlaneProbes)
				{
					cfg.m_startProbe = iStartProbe;
					cfg.m_endProbe = iStartProbe + kNumProbesNearDistPoint;
					cfg.m_noHitColor = Color(0.5f, 0.4f, 0.6f, 0.33f);

					m_otherJob.DebugDraw(cfg);
				}

				iStartProbe += kNumProbesNearDistPoint;
				if (g_debugDrawCameraStrafePhotoMode && 
					pCamera->IsPhotoMode())
				{
					cfg.m_startProbe = iStartProbe;
					cfg.m_endProbe = iStartProbe + kNumProbesPhotoMode;
					cfg.m_noHitColor = Color(0.5f, 0.4f, 0.6f, 0.33f);

					m_otherJob.DebugDraw(cfg);
				}
			}
#endif

			SimpleCastGather(m_otherJob, m_aOtherProbes, numProbes);
		}

		Plane plane;
		if (EngineComponents::GetNdPhotoModeManager()->ShouldClipCameraToPlane(plane))
		{
//#if !FINAL_BUILD
//			{
//				Scalar distOriginToPlane = -plane.GetD();
//				Vector planeNormalWs = plane.GetNormal();
//
//				Scalar distOriginToProbeStart = Dot(m_aProbe[0].m_pos - kOrigin, planeNormalWs);
//				Scalar distProbeStartToPlane = distOriginToPlane - distOriginToProbeStart;
//
//				Point pointOnPlaneWs = m_aProbe[0].m_pos + distProbeStartToPlane * planeNormalWs;
//				g_prim.Draw(DebugPlaneCross(pointOnPlaneWs, planeNormalWs, kColorMagentaTrans, 1.0f, 2.0f, PrimAttrib(kPrimEnableHiddenLineAlpha)));
//			}
//#endif

			for (int iGroup = 0; iGroup < kProbeGroupCount; ++iGroup)
			{
				const ProbeGroup& group = m_aProbeGroup[iGroup];
				for (int iProbe = 0; iProbe < group.m_probeCount; ++iProbe)
				{
					const int jProbe = iProbe + group.m_iStartProbe;
					ClipProbeAgainstPlane(m_aHoriVertProbes[jProbe], plane);
				}
			}

			{
				const int numNonPhotoModeProbes = kNumProbesOthers - kNumProbesPhotoMode;
				const int iStartProbe = numNonPhotoModeProbes;
				const int numProbes = pCamera->IsPhotoMode() ? 
									kNumProbesOthers : 
									numNonPhotoModeProbes;
				for (int iProbe = iStartProbe; iProbe < numProbes; iProbe++)
				{
					ClipProbeAgainstPlane(m_aOtherProbes[iProbe], plane);
				}
			}
		}

		if (pCamera->IsPhotoMode())
		{	// Clamp offset range based on probes
			const int iStartProbe = kNumProbesOthers - kNumProbesPhotoMode;

			float offsetMaxXz0;
			float offsetMaxXz1;
			float offsetMinY;
			float offsetMaxY;
			{
				const float tt0 = m_aOtherProbes[iStartProbe + kProbePhotoModeXzLeft].m_cc.m_time >= 0.0f ?
								m_aOtherProbes[iStartProbe + kProbePhotoModeXzLeft].m_cc.m_time :
								1.0f;
				const float tt = Min(tt0, 1.0f);
				offsetMaxXz0 = -m_dcOffsetMaxXzRadius * tt;
			}
			{
				const float tt0 = m_aOtherProbes[iStartProbe + kProbePhotoModeXzRight].m_cc.m_time >= 0.0f ?
								m_aOtherProbes[iStartProbe + kProbePhotoModeXzRight].m_cc.m_time :
								1.0f;
				const float tt = Min(tt0, 1.0f);
				offsetMaxXz1 = m_dcOffsetMaxXzRadius * tt;
			}
			{
				const float tt0 = m_aOtherProbes[iStartProbe + kProbePhotoModeMinY].m_cc.m_time >= 0.0f ?
								m_aOtherProbes[iStartProbe + kProbePhotoModeMinY].m_cc.m_time :
								1.0f;
				const float tt = Min(tt0, 1.0f);
				offsetMinY = m_dcOffsetMinY * tt;
			}
			{
				const float tt0 = m_aOtherProbes[iStartProbe + kProbePhotoModeMaxY].m_cc.m_time >= 0.0f ?
								m_aOtherProbes[iStartProbe + kProbePhotoModeMaxY].m_cc.m_time :
								1.0f;
				const float tt = Min(tt0, 1.0f);
				offsetMaxY = m_dcOffsetMaxY * tt;
			}

			pCamera->SetPhotoModeOffsetRange(offsetMaxXz0, offsetMaxXz1, offsetMinY, offsetMaxY);
		}
		
		{
			// determine ceiling y adjustment
			if (ignoreCeilingProbe)
			{
				m_ceilingYAdjust = 0.f;
			}
			else
			{
				const SimpleCastProbe& probe = m_aOtherProbes[0];
				const float tt = probe.m_cc.m_time >= 0.0f ? probe.m_cc.m_time : 1.0f;
				m_ceilingYAdjust = (1.0f - tt) * Length(probe.m_vec) * -1.0f;
			}

			// determine pushed in safe pos
			float targetPushedInSafePos = 0.0f;
			{
				const SimpleCastProbe& probe = m_aOtherProbes[kNumProbesCeiling];
				if (!m_disablePushedInSafePos && probe.m_cc.m_time >= 0.0f)
					targetPushedInSafePos = 1.0f - probe.m_cc.m_time;
			}
			float springConst = (targetPushedInSafePos > m_pushedInSafePosBlend) ? 30.0f : 10.0f;
			m_pushedInSafePosBlend = m_pushedInSafePosSpring.Track(m_pushedInSafePosBlend, targetPushedInSafePos, dt, springConst);

			// Bring down the near plane when backed into a wall, and/or in a low ceiling (esp with the cam facing down)
			if (bTopControl)
			{
				const SimpleCastProbe& probe = m_aOtherProbes[kNumProbesCeiling + 1];
				m_nearPlaneCloseFactor = LerpScaleClamp(0.2f, 0.7f, 0.0f, 1.0f, m_pushedInSafePosBlend);
				if (probe.m_cc.m_time >= 0.0f)
				{
					float ceilingFactor = LerpScale(1.0f, 0.5f, 0.5f, 1.0f, probe.m_cc.m_time);
					m_nearPlaneCloseFactor += MinMax01(ceilingFactor)*LerpScale(0.4f, 1.0f, 0.9f, 1.5f, m_aProbeGroup[kVertical].m_fCurrentArc);
				}
			}

			for (I32 iGroup = 0; iGroup < kProbeGroupCount; ++iGroup)
			{
				ProbeGroup& group = m_aProbeGroup[iGroup];

				for (int iDir = 0; iDir < ProbeGroup::kStickFactorCount; ++iDir)
				{
					const int iStickDir = (iGroup * 2) + iDir;
					const TimeFrame timeSinceStickDir = curTime - pCamera->GetLastStickDirTime(iStickDir);
					const float idealStickFactor = (timeSinceStickDir <= Seconds(1.0f / 30.0f)) ? 1.0f : 0.0f; // stick factor: stick is... 1=pushed, 0=not pushed

					if (idealStickFactor > group.m_stickFactor[iDir])
					{
						// ramp up quickly
						group.m_stickFactor[iDir] = group.m_stickFactorSpring[iDir].Track(group.m_stickFactor[iDir], idealStickFactor, dt, 5.0f, +1);
					}
					else
					{
						// relax more slowly
						group.m_stickFactor[iDir] = group.m_stickFactorSpring[iDir].Track(group.m_stickFactor[iDir], idealStickFactor, dt, 2.0f, -1);
					}
				}

				// we only need vertical probes to collide with water plane, don't care about horizontal probes.
				const CamWaterPlane* pWaterPlane = (collideWaterPlane && iGroup == kVertical) ? &waterPlane : nullptr;
				ComputeGroupPullInPercent(m_aHoriVertProbes, group, (ProbeGroupIndex)iGroup, pWaterPlane);
			}

			// photomode camera needs collision and should ignore "script disable camera collision"
			if (!g_cameraOptions.m_disableStrafeCameraCollPullIn || photoMode)
			{
				ProbeGroupIndex winningGroupIndex;
				if (m_aProbeGroup[kHorizontal].m_urgency == m_aProbeGroup[kVertical].m_urgency)
					winningGroupIndex = (m_aProbeGroup[kHorizontal].m_pullInPercent < m_aProbeGroup[kVertical].m_pullInPercent) ? kHorizontal : kVertical;
				else
					winningGroupIndex = (m_aProbeGroup[kHorizontal].m_urgency > m_aProbeGroup[kVertical].m_urgency) ? kHorizontal : kVertical;
				const ProbeGroup& winningGroup = m_aProbeGroup[winningGroupIndex];

				if (pSettings->m_useConservative)
				{
					desiredPullInPercent = Min(m_aProbeGroup[0].m_pullInPercent, m_aProbeGroup[1].m_pullInPercent);
					pullInUrgency = Max(m_aProbeGroup[0].m_urgency, m_aProbeGroup[1].m_urgency);
				}
				else
				{
					desiredPullInPercent = winningGroup.m_pullInPercent;
					pullInUrgency = winningGroup.m_urgency;
				}
			}

			ASSERT(pSettings->m_fadeOutSpring <= springConstantIn); // presumably "out" is a nice slow spring, while "in" is much faster (more urgent!)
			effSpringConstantIn = LerpScale(0.0f, 1.0f, pSettings->m_fadeOutSpring, springConstantIn, pullInUrgency);
		}
	}

	// now spring-track the current pull-in percent to the desired value to smooth things out

	ASSERT(desiredPullInPercent >= 0.0f && desiredPullInPercent <= 1.1f);
	const float pullInDelta = desiredPullInPercent - m_pullInPercent;

	if (pullInDelta <= 0.0f)
	{
		// PULL IN

		// HACK! if we stop y input because hitting ceiling or floor, try avoid using big spring const to fix pop.
		if (pullInUrgency >= 1.0f)
			effSpringConstantIn = pCamera->GetStopPullInHitCeilingFloor() ? 20.f : 150.0f; // avoid clip-thru when actual camera sphere is in collision

		m_desiredPullInPercent = desiredPullInPercent;
		if (m_lastPullInTime == TimeFrameNegInfinity())
		{
			m_pullInPercent = desiredPullInPercent;
			m_pullInSpring.Reset();
		}
		else
		{
			m_pullInPercent = m_pullInSpring.Track(m_pullInPercent, desiredPullInPercent, dt, effSpringConstantIn, +1);
		}
		m_lastPullInTime = curTime;
		m_pullOutSpringConst = kMinSpringConstantOut;
	}
	else
	{
		// RELAX BACK OUT

		// don't start relaxing back out if (a) we pulled in recently, or (b) the player is messing with the camera stick
		const float kPullOutTimeWindow = pSettings->m_pullOutTimer;
		const float secondsSinceLastPullIn = ToSeconds(curTime - m_lastPullInTime);
		const float secondsSinceLastStickTouch = pSettings->m_fadeOutDuringStickMovement ? secondsSinceLastPullIn : ToSeconds(curTime - pCamera->GetLastStickTouchTime());
		const float minSeconds = Min(secondsSinceLastPullIn, secondsSinceLastStickTouch);

		float scaledSpringConstantOut = pSettings->m_fadeOutSpring;
		if (kPullOutTimeWindow > 0.0f)
			scaledSpringConstantOut = LerpScale(0.0f, kPullOutTimeWindow, kMinSpringConstantOut, pSettings->m_fadeOutSpring, minSeconds);

		// but if (a) the target is moving or (b) we had external request to pull out, pull out as quickly as possible
		const float secondsSinceTargetMove = ToSeconds(curTime - pCamera->GetLastTargetMoveTime());
		if (secondsSinceTargetMove < 2.0f / 30.0f)
			scaledSpringConstantOut = pSettings->m_fadeOutSpring;

		// only allow the pull-out spring const to go up, never down (until next pull-in)
		if (m_pullOutSpringConst == -1.0f || m_pullOutSpringConst < scaledSpringConstantOut)
			m_pullOutSpringConst = scaledSpringConstantOut;

		// allow overriding spring constant for situtions where relaxed pull out causes clipping.
		// override is stays active until next pull-in
		if(g_cameraOptions.m_overridePulloutConstant)
			m_pullOutSpringConst = g_cameraOptions.m_pulloutConstant;

		m_desiredPullInPercent = desiredPullInPercent;
		m_pullInPercent = m_pullInSpring.Track(m_pullInPercent, desiredPullInPercent, dt, m_pullOutSpringConst, -1);
	}

	m_pullInPercent = Limit01(m_pullInPercent);

	return m_pullInPercent;
}


void StrafeCameraCollision::GatherSmallObjectsJob()
{
	if (m_smallObjectsJob.IsKicked())
	{
		m_smallObjectsJob.Wait();
		COLL_CAST_USAGE_ASSERT(kSmallObjectProbesCount == m_smallObjectsJob.NumProbes());

		for (U32F i = 0; i < kSmallObjectProbesCount; i++)
		{
			// Fill out the probe data
			m_smallObjectProbes[i].m_pos = m_smallObjectsJob.GetProbeStart(i);
			m_smallObjectProbes[i].m_vec = m_smallObjectsJob.GetProbeEnd(i) - m_smallObjectProbes[i].m_pos;
			m_smallObjectProbes[i].m_radius = m_smallObjectsJob.GetProbeRadius(i);

			if (m_smallObjectsJob.IsContactValid(i, 0) && m_smallObjectsJob.GetContactPat(i, 0).GetCameraBlockNear())
			{
				// Contact with a small-object
				m_smallObjectProbes[i].m_cc.m_time = m_smallObjectsJob.GetContactT(i, 0);
				if (m_smallObjectProbes[i].m_cc.m_time >= 0.0f)
				{
					// Fill the collision data
					ICollCastJob::ContactObject hitObject = m_smallObjectsJob.GetContactObject(i, 0);

					m_smallObjectProbes[i].m_cc.m_contact = m_smallObjectsJob.GetContactPoint(i, 0); // NOTE: for sphere jobs, we add the lever later...
					m_smallObjectProbes[i].m_cc.m_normal = m_smallObjectsJob.GetContactNormal(i, 0);
					m_smallObjectProbes[i].m_cc.m_hGameObject = hitObject.m_hGameObject;
					m_smallObjectProbes[i].m_cc.m_pRigidBody = hitObject.m_pRigidBody;
					m_smallObjectProbes[i].m_cc.m_pat = m_smallObjectsJob.GetContactPat(i, 0);
					m_smallObjectProbes[i].m_cc.m_contact += m_smallObjectsJob.GetContactLever(i, 0);
				}
			}
			else
			{
				m_smallObjectProbes[i].m_cc.m_time = -1.0f;
			}
		}
	}
	else
	{
		// If the job is not even kicked,
		//  then fill the data as if there was no collision detected by small objects probes
		for (U32F i = 0; i < kSmallObjectProbesCount; i++)
		{
			m_smallObjectProbes[i].m_cc.m_time = -1.0f;
		}
	}
}

void StrafeCameraCollision::CloseSmallObjectsJob()
{
	m_smallObjectsJob.Close();
}

bool StrafeCameraCollision::IsDebugDrawingSmallObjectsJob(const CameraControlStrafeBase *pCamera) const
{
	return FALSE_IN_FINAL_BUILD(ShouldDrawCollision(*pCamera) && g_debugDrawCameraSmallObjectsProbes);
}

bool StrafeCameraCollision::IsClippingWithSmallObjects(const CameraControlStrafeBase *pCamera, float pullInPercent)
{
	bool isClipping = false;	// output

	if (m_smallObjectsJob.IsValid())
	{
		m_smallObjectsJob.Wait();

		const float contactTSafeToCam = m_smallObjectsJob.GetContactT(kSafePosToCamera, 0);
		const float contactTCamToSafe = m_smallObjectsJob.GetContactT(kCameraToSafePos, 0);

		const float tSafeToCam = (contactTSafeToCam < 0.0f) ? 1.0f : contactTSafeToCam;		// goes from left to RIGHT, as per following comment diagram
		const float tCamToSafe = (contactTCamToSafe < 0.0f) ? 1.0f : contactTCamToSafe;		// goes from RIGHT to left

		// Get collision area in terms of t
		// 
		//               start> | collision area | <end
		//                      |                |
		//    t=0               |t=0.4    t=0.5  |t=0.6               t=1
		//    *-----------------|---------*------|--------------------*
		//   safe                    camera pos                       camera
		//    pos                  (after current                     pos (ideal)
		//                                pull-in)
		//
		// Note: In this example we wanna make sure that "camera pos t" is outside the whole collision area!
		//       => Check if the camera pos t (= 0.5) is not in range [0.4, 0.6]
		const float tCollisionAreaStart = tSafeToCam;
		const float tCollisionAreaEnd = 1.0f - tCamToSafe;

		// Get camera position in terms of t
		const float tCamNearPlanePos = pullInPercent;		// pullInPecent is 1.0 if cam is on ideal position; 0.0 if cam on safe position

		// If the near plane t is in no camera region, we're clipping with small objects
		if ((tCamNearPlanePos >= tCollisionAreaStart) && (tCamNearPlanePos <= tCollisionAreaEnd))
			isClipping = true;

		// Debug
		if (IsDebugDrawingSmallObjectsJob(pCamera))
		{
			const Point safePosWs = m_smallObjectsJob.GetProbeStart(kSafePosToCamera);
			const Point camPosIdealWs = m_smallObjectsJob.GetProbeStart(kCameraToSafePos);
			const Vector safeToCamIdealWs = camPosIdealWs - safePosWs;

			const Point collStartWs = safePosWs + (safeToCamIdealWs * tCollisionAreaStart);
			const Point collEndWs = safePosWs + (safeToCamIdealWs * tCollisionAreaEnd);
			const Point camNearPlanePosWs = safePosWs + (safeToCamIdealWs * tCamNearPlanePos);

			if (tCollisionAreaStart < tCollisionAreaEnd)	// otherwise there is no collision
			{
				g_prim.Draw(DebugSphere(collStartWs, 0.01f, kColorYellow, PrimAttrib(kPrimEnableHiddenLineAlpha)), DebugPrimTime(kPrimDuration1FramePauseable));
				g_prim.Draw(DebugString(collStartWs, "\nsmall-obj-start", kColorYellow, 0.5f), DebugPrimTime(kPrimDuration1FramePauseable));

				g_prim.Draw(DebugSphere(collEndWs, 0.01f, kColorYellow, PrimAttrib(kPrimEnableHiddenLineAlpha)), DebugPrimTime(kPrimDuration1FramePauseable));
				g_prim.Draw(DebugString(collEndWs, "\nsmall-obj-end", kColorYellow, 0.5f), DebugPrimTime(kPrimDuration1FramePauseable));
			}

			const Color32 camPosColor = isClipping ? kColorRed : kColorWhite;
			g_prim.Draw(DebugSphere(camNearPlanePosWs, 0.01f, camPosColor, PrimAttrib(kPrimEnableHiddenLineAlpha)), DebugPrimTime(kPrimDuration1FramePauseable));
			g_prim.Draw(DebugString(camNearPlanePosWs, "cam-near-plane-in", camPosColor, 0.5f), DebugPrimTime(kPrimDuration1FramePauseable));
		}
	}

	return isClipping;
}

float StrafeCameraCollision::ComputePullInPercentToAvoidSmallObjects(float deltaTime)
{
	const float timeSafePosToCamera = m_smallObjectProbes[kSafePosToCamera].m_cc.m_time;
	const float pullInProbeHitPercent = MinMax01((timeSafePosToCamera >= 0.0f) ? timeSafePosToCamera : 1.0f);

	const int kMainHorizProbeIndex = kNumProbesHoriz / 2;
	const F32 desiredPullInPercent = pullInProbeHitPercent * Length(m_smallObjectProbes[kSafePosToCamera].m_vec) / Length(m_aHoriVertProbes[kMainHorizProbeIndex].m_vec);	// probe.m_vec is the whole displacement from its start pos to end pos (not just the direction)

	const float pullInDelta = desiredPullInPercent - m_pullInPercent;

	if (pullInDelta <= 0.0f)
	{
		const TimeFrame curTime = EngineComponents::GetNdFrameState()->GetClock(kCameraClock)->GetCurTime();
		const I32 springDirection = +1;

		m_desiredPullInPercent = desiredPullInPercent;
		m_pullInPercent = m_pullInSpring.Track(m_pullInPercent, desiredPullInPercent, deltaTime, 150.0f, springDirection);

		m_lastPullInTime = curTime;
		m_pullOutSpringConst = kMinSpringConstantOut;
	}
	
	return m_pullInPercent;
}

void StrafeCameraCollision::UpdateVerticalDirection()
{
	const ProbeGroup& group = m_aProbeGroup[kVertical];
	const SimpleCastProbe* probes = m_aHoriVertProbes;

	int iProbe;
	float fiProbe;
	float fStartArc = group.m_fCurrentArc - 0.5f;

	int iCenterProbeIndex = -1;

	// find center probe first.
	for (iProbe = 0, fiProbe = 0.0f; iProbe < group.m_probeCount; ++iProbe, fiProbe += 1.0f)
	{
		int index = group.m_iStartProbe + iProbe;
		GAMEPLAY_ASSERT(index >= 0 && index < kNumProbesHoriVert);
		const SimpleCastProbe& curProbe = probes[index];

		const float fArc = fStartArc + fiProbe * group.m_fProbeSpacing;
		const float fArcClamped = MinMax01(fArc);
		const float fSignedArcDistFromCenter = fArc - group.m_fCurrentArc; // negative below camera, positive above
		const float fArcDistFromCenter = Abs(fSignedArcDistFromCenter);

		// pretend we didn't pull in as far when we're farther from the center
		const float fNormDistFromCenter = MinMax01(fArcDistFromCenter * 2.0f);

		if (fNormDistFromCenter < group.m_fProbeSpacing)
		{
			iCenterProbeIndex = iProbe;
			break;
		}
	}

	if (iCenterProbeIndex < 0)
		return;

	{
		int index = group.m_iStartProbe + iCenterProbeIndex;
		GAMEPLAY_ASSERT(index >= 0 && index < kNumProbesHoriVert);
		const SimpleCastProbe& centerProbe = probes[index];
		if (centerProbe.m_cc.m_time < 0.f)
		{
			m_verticalSafeDir = kBothDirSafe;
			return;
		}
	}

	float upDirTotalTime = 0.f;
	float downDirTotalTime = 0.f;

	for (iProbe = 0, fiProbe = 0.0f; iProbe < group.m_probeCount; ++iProbe, fiProbe += 1.0f)
	{
		if (iProbe == iCenterProbeIndex)
			continue;

		int index = group.m_iStartProbe + iProbe;
		GAMEPLAY_ASSERT(index >= 0 && index < kNumProbesHoriVert);
		const SimpleCastProbe& curProbe = probes[index];
		const float actualPullInPercent = (curProbe.m_cc.m_time >= 0.0f) ? curProbe.m_cc.m_time : 1.0f;

		const float fArc = fStartArc + fiProbe * group.m_fProbeSpacing;
		const float fArcClamped = MinMax01(fArc);
		const float fSignedArcDistFromCenter = fArc - group.m_fCurrentArc; // negative below camera, positive above

		//g_prim.Draw(DebugString(curProbe.m_pos + curProbe.m_vec, StringBuilder<64>("%d", iProbe).c_str(), kColorWhite, 0.5f), kPrimDuration1FramePauseable);
		if (fSignedArcDistFromCenter < 0.f)
			downDirTotalTime += actualPullInPercent;
		else
			upDirTotalTime += actualPullInPercent;
	}

	m_verticalSafeDir = upDirTotalTime > downDirTotalTime ? kUpDirSafe : kDownDirSafe;
}

Point StrafeCameraCollision::GetFinalSafePos() const
{
	return Lerp(m_safePosWs, m_pushedInSafePosWs, m_pushedInSafePosBlend);
}

//SimpleCastProbe *StrafeCameraCollision::GetProbes(int &numHorizontal, int &numVertical, float &horizontalProbeFanHalfAngleDeg)
//{
//	numHorizontal = kNumProbesHoriz;
//	numVertical = kNumProbesVert;
//	horizontalProbeFanHalfAngleDeg = kHorizontalProbeFanHalfAngleDeg;
//
//	return &m_aProbe[0];
//}

//const SimpleCastProbe* StrafeCameraCollision::GetProbes(ProbeGroupIndex index, I32* outNumProbes) const
//{
//	ALWAYS_ASSERT(index == kHorizontal || index == kVertical);
//	if (index == kHorizontal)
//	{
//		*outNumProbes = kNumProbesHoriz;
//		return m_aProbe + (kNumProbesCeiling + kNumProbesSafePoint + kNumProbesNearDistPoint);
//	}
//	else if (index == kVertical)
//	{
//		*outNumProbes = kNumProbesVert;
//		return m_aProbe + (kNumProbesCeiling + kNumProbesSafePoint + kNumProbesNearDistPoint + kNumProbesHoriz);
//	}
//
//	return nullptr;
//}

void StrafeCameraCollision::DebugDraw(const CameraControlStrafeBase* pCamera) const
{
	STRIP_IN_FINAL_BUILD;

	if (ShouldDrawCollision(*pCamera))
	{
		ICollCastJob::DrawConfig cfg(kPrimDuration1FramePauseable, 0, kNumProbesCeiling);
		cfg.m_hitColor = kColorRedTrans;

		if (g_debugDrawCameraSmallObjectsProbes && 
			m_smallObjectsJob.IsValid())
		{
			m_smallObjectsJob.Wait();

			// Small objects probe			
			cfg.m_startProbe = 0;
			cfg.m_endProbe = cfg.m_startProbe + 2;
			cfg.m_noHitColor = Color(0.0f, 1.0f, 0.0f, 0.33f);
			m_smallObjectsJob.DebugDraw(cfg);

			Color probeColor = Color(0.0f, 0.0f, 1.0f, 0.33f);
			Point pullProbeStart = m_smallObjectsJob.GetProbeStart(cfg.m_startProbe);
			Vector pullProbeDir = m_smallObjectsJob.GetProbeDir(cfg.m_startProbe);
			g_prim.Draw(DebugLine(pullProbeStart, pullProbeDir, probeColor, 2.0f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
		}

		for (I32 iGroup = 0; iGroup < kProbeGroupCount; ++iGroup)
		{
			if (g_debugDrawCameraCollGroup[iGroup])
			{
				const ProbeGroup& group = m_aProbeGroup[iGroup];

				// No, drawn below.
				//if (g_debugDrawSingleCameraProbe >= 0)
				//	m_job.DebugDraw(kPrimDuration1FramePauseable, g_debugDrawSingleCameraProbe, g_debugDrawSingleCameraProbe + 1);
				//else
				//	m_job.DebugDraw(kPrimDuration1FramePauseable, group.m_iStartProbe, group.m_iStartProbe + group.m_probeCount);

				if (group.m_aProbeDebug)
				{
					int iProbe;
					float fiProbe;
					float fStartArc = group.m_fCurrentArc - 0.5f;
					for (iProbe = 0, fiProbe = 0.0f; iProbe < group.m_probeCount; ++iProbe, fiProbe += 1.0f)
					{
						const int jProbe = group.m_iStartProbe + iProbe;

						if (g_debugDrawSingleCameraProbe >= 0 && jProbe != g_debugDrawSingleCameraProbe)
							continue;

						const SimpleCastProbe& curProbe = m_aHoriVertProbes[jProbe];

						const float actualPullInPercent = group.m_aProbeDebug[iProbe].m_actualPullInPercent;
						const float distOnlyPullInPercent = group.m_aProbeDebug[iProbe].m_distOnlyPullInPercent;
						const float pullInPercent = group.m_aProbeDebug[iProbe].m_pullInPercent;

						const Color centerProbeColor = (curProbe.m_cc.m_time < 0.0f) ? kColorGrayTrans : kColorRedTrans;
						const Color otherProbeColor = (curProbe.m_cc.m_time < 0.0f) ? kColorDarkGrayTrans : kColorOrangeTrans;
						const Color probeColor = (group.m_aProbeDebug[iProbe].m_normDistFromCenter < group.m_fProbeSpacing) ? centerProbeColor : otherProbeColor;

						const Point actualCenterWs = curProbe.m_pos + curProbe.m_vec * actualPullInPercent;
						g_prim.Draw(DebugLine(curProbe.m_pos, curProbe.m_vec, probeColor, 2.0f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
						g_prim.Draw(DebugSphere(actualCenterWs, curProbe.m_radius, probeColor, PrimAttrib(kPrimEnableDepthTest, kPrimEnableWireframe)), kPrimDuration1FramePauseable);

						if (g_debugDrawCameraDistOnlyProbes)
						{
							const Point distOnlyCenterWs = curProbe.m_pos + curProbe.m_vec * distOnlyPullInPercent;
							g_prim.Draw(DebugSphere(distOnlyCenterWs, curProbe.m_radius, kColorWhiteTrans, PrimAttrib(kPrimEnableDepthTest, kPrimEnableWireframe)), kPrimDuration1FramePauseable);
						}
						if (g_debugDrawCameraAdjustedProbes)
						{
							const Point finalCenterWs = curProbe.m_pos + curProbe.m_vec * pullInPercent;
							g_prim.Draw(DebugSphere(finalCenterWs, curProbe.m_radius, kColorYellowTrans, PrimAttrib(kPrimEnableDepthTest, kPrimEnableWireframe)), kPrimDuration1FramePauseable);
						}
					}
				}
			}
		}
	}
}

//
// end camera-collision.cpp
// =================================================================================================================================
