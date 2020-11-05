/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-process-ragdoll.h"

#include "gamelib/camera/camera-manager.h"
#include "gamelib/fx/splashers.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/ndphys/composite-body.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-pose-layer.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/camera/camera-final.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/render/dev/render-options.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim.h"

PROCESS_REGISTER_ABSTRACT(NdProcessRagdoll, Character);

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdProcessRagdoll::Init(const ProcessSpawnInfo& info)
{
	m_isInStealthVegetationEvaluated = false;
	m_isInStealthVegetation = false;
	m_inLoopingPoseAnim = false;

	NdRagdollSpawnInfo* pRagdollInfo = (NdRagdollSpawnInfo*)info.m_pUserData;
	const Character* pSourceChar = pRagdollInfo->m_processToKillOnSpawn.ToProcess();
	m_ambientOccludersId = pSourceChar ? pSourceChar->GetDefaultAmbientOccludersId() : INVALID_STRING_ID_64;

	m_sourcePose = DualSnapshotNode();

	Err result = ParentClass::Init(info);

	m_cachedFootSurfaceType = pSourceChar ? pSourceChar->GetCachedFootSurfaceType() : INVALID_STRING_ID_64;

	if (pSourceChar)
	{
		for (int i=0; i<kMeshRaycastCount; i++)
		{
			const NdGameObject::MeshRaycastResult* pGroundResult = pSourceChar->GetGroundMeshRaycastResultConst((MeshRaycastType)i);
			if (pGroundResult)
				m_groundMeshRaycastResult[i] = *pGroundResult;
		}
	}

	SetAllowThreadedUpdate(true);

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdProcessRagdoll::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	m_sourcePose.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdProcessRagdoll::PreUpdate()
{
	ParentClass::PreUpdate();

	PrepareDeferredBasePoseJoints();

	const Point pos = GetTranslation();

	if (!m_isInStealthVegetationEvaluated || !m_pRagdoll->IsSleeping())
	{
		NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		Point aTestPoint[6];
		U32 numTestPoints = 0;
		const AttachSystem* pAttachSystem = GetAttachSystem();
		if (pAttachSystem)
		{
			StringId64 aTestPointId[6] = 
			{
				SID("targHead"), 
				SID("targPelvis"), 
				SID("targLElbow"), 
				SID("targRElbow"), 
				SID("targLKnee"), 
				SID("targRKnee")
			};

			numTestPoints = 0;
			for (U32 i = 0; i < 6; ++i)
			{
				AttachIndex idx;
				if (pAttachSystem->FindPointIndexById(&idx, aTestPointId[i]))
				{
					aTestPoint[numTestPoints++] = pAttachSystem->GetAttachPosition(idx);
				}
			}
		}

		if (numTestPoints == 0)
		{
			aTestPoint[0] = pos;
			numTestPoints = 1;
		}

		m_isInStealthVegetation = false;

		const U32 minNumPointsInStealth = (numTestPoints / 2) + 1;
		U32 numPointsInStealth = 0;
		for (U32 i = 0; i < numTestPoints; ++i)
		{
			FindBestNavMeshParams nmParams;
			NavMesh::FindPointParams polyParams;
			nmParams.m_pointWs = aTestPoint[i];
			nmParams.m_cullDist = 0.0f;
			nmParams.m_yThreshold = 2.0f;
	
			nmMgr.FindNavMeshWs(&nmParams);
	
			if (nmParams.m_pNavPoly && nmParams.m_pNavPoly->IsStealth())
			{
				if (++numPointsInStealth >= minNumPointsInStealth)
				{
					m_isInStealthVegetation = true;
					break;
				}
			}
		}
		m_isInStealthVegetationEvaluated = true;
	}

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	if (!m_navLocation.IsValid() || DistSqr(pos, m_navLocation.GetPosWs()) > Sqr(0.2f))
	{
		m_navLocation = NavUtil::ConstructNavLocation(pos, NavLocation::Type::kNavPoly, 0.4f);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdProcessRagdoll::GetSite(StringId64 nameId) const
{
	const AttachSystem* pAs = GetAttachSystem();
	if (!pAs)
		return ParentClass::GetSite(nameId);
	
	switch (nameId.GetValue())
	{
		case SID_VAL("AimAtPos"):
		{
			AttachIndex index;
			bool found = GetAttachSystem()->FindPointIndexById(&index, SID("targChest"));
			if (found)
				return Locator(GetAttachSystem()->GetAttachPosition(index), GetRotation());
		}
		break;

		case SID_VAL("LowestPos"):
		{
			AttachIndex index;
			bool found = GetAttachSystem()->FindPointIndexById(&index, SID("targLAnkle"));
			if (found)
				return Locator(GetAttachSystem()->GetAttachPosition(index), GetRotation());
		}
		break;
	}

	// use root by default
	return ParentClass::GetSite(nameId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdProcessRagdoll::CopyJointsFrom(const NdGameObject* pSrcObject)
{
	FgAnimData* pAnimData = GetAnimData();
	AnimControl* pAnimControl = GetAnimControl();
	JointCache* pMyJointCache = pAnimControl ? pAnimControl->GetJointCache() : nullptr;

	if (!pAnimData || !pMyJointCache || !pSrcObject)
		return;

	const AnimControl* pSrcAnimControl = pSrcObject->GetAnimControl();
	const JointCache* pSrcCache = pSrcAnimControl ? pSrcAnimControl->GetJointCache() : nullptr;

	if (pSrcCache)
	{
		pSrcCache->Duplicate(pMyJointCache);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdProcessRagdoll::CreateBasePoseNode(const DualSnapshotNode* pSourcePose)
{
	AnimControl* pAnimControl = GetAnimControl();
	const FgAnimData* pAnimData = GetAnimData();
	const ArtItemSkeleton* pSkel = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem();
	const ndanim::JointHierarchy* pSkeleton = pAnimSkel->m_pAnimHierarchy;

	m_sourcePose.Init(pAnimData);
	m_sourcePose.Copy(*pSourcePose, pAnimData);

	const bool multiSeg = (pAnimSkel->m_pAnimHierarchy->m_numSegments > 1) || (pSkel->m_pAnimHierarchy->m_numSegments > 1);

	pAnimControl->AllocatePoseLayer(SID("base-pose"), ndanim::kBlendSlerp, kRagdollLayerPriorityBasePose);
	if (AnimPoseLayer* pPoseLayer = pAnimControl->CreatePoseLayer(SID("base-pose"), &m_sourcePose))
	{
		if (multiSeg)
		{
			pPoseLayer->EnableDeferredExecution(true);
		
			PrepareDeferredBasePoseJoints();
		}

		pPoseLayer->Fade(1.0f, 0.0f);
	}

	if (multiSeg)
	{
		SetStopWhenPaused(false);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdProcessRagdoll::PrepareDeferredBasePoseJoints()
{
	AnimControl* pAnimControl = GetAnimControl();
	const FgAnimData* pAnimData = GetAnimData();

	const ArtItemSkeleton* pSkel = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem();
	const bool multiSeg = (pAnimSkel->m_pAnimHierarchy->m_numSegments > 1) || (pSkel->m_pAnimHierarchy->m_numSegments > 1);

	if (multiSeg)
	{
		if (AnimPoseLayer* pPoseLayer = pAnimControl->GetPoseLayerById(SID("base-pose")))
		{
			AllocateJanitor jj(kAllocGameToGpuRing, FILE_LINE_FUNC);
			DualSnapshotNode* pLayerPose = NDI_NEW DualSnapshotNode;

			pLayerPose->Init(pAnimData);
			pLayerPose->Copy(m_sourcePose, pAnimData);

			pPoseLayer->SetSnapshotNode(pLayerPose);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdProcessRagdoll::StartPoseAnim(StringId64 poseAnim, bool looping)
{
	if (poseAnim == INVALID_STRING_ID_64)
		return false;

	AnimControl* pAnimControl = GetAnimControl();
	AnimSimpleLayer* pAnimLayer = pAnimControl->CreateSimpleLayer(SID("pose-anim"), ndanim::kBlendSlerp, kRagdollLayerPriorityPoseAnim);

	if (!pAnimLayer)
		return false;

	if (!pAnimLayer->RequestFadeToAnim(poseAnim))
		return false;

	pAnimLayer->Fade(1.0f, 0.0f);

	if (AnimSimpleInstance* pInst = pAnimLayer->CurrentInstance())
	{
		pInst->SetLooping(looping);
		m_inLoopingPoseAnim = looping;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdProcessRagdoll::IsInPoseAnim() const
{
	const AnimControl* pAnimControl = GetAnimControl();
	if (!pAnimControl)
		return false;

	const AnimSimpleLayer* pAnimPoseLayer = pAnimControl->GetSimpleLayerById(SID("pose-anim"));
	if (!pAnimPoseLayer)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdProcessRagdoll::DislodgeFromPoseAnim()
{
	AnimControl* pAnimControl = GetAnimControl();
	if (!pAnimControl)
		return;

	AnimSimpleLayer* pAnimPoseLayer = pAnimControl->GetSimpleLayerById(SID("pose-anim"));
	ASSERT(pAnimPoseLayer);
	if (!pAnimPoseLayer)
		return;

	pAnimPoseLayer->FadeOutAndDestroy(0.2f);

	m_inLoopingPoseAnim = false;

	PhysicalizeRagdoll();
}

static void FindClosestPointOnAabb(const Aabb& aabb, Point_arg p, Point& closestPt)
{
	closestPt = p;
	closestPt = Max(closestPt, aabb.m_min);
	closestPt = Min(closestPt, aabb.m_max);
}

// Basically a copy of Character::IsClippingCameraPlane but with few modifications for ragdoll
// We should unify this code when there is time
bool NdProcessRagdoll::IsClippingCameraPlane(bool useCollCastForBackpack, float* pNearestApproach) const
{
	PROFILE_AUTO(Camera);

	// expand a bit when hidden so when we unhide small motions will not clip back into the camera
	const Scalar surfaceExpand = GetDrawControl()->IsNearCamera() ? SCALAR_LC(0.1f) : SCALAR_LC(0.02f);

	RenderCamera renderCam = g_mainCameraInfo[0].GetRenderCamera();
	const Locator camLocWs = CameraManager::Get().GetNoManualLocator();
	renderCam.m_camLeft = GetLocalX(camLocWs.GetRotation());
	renderCam.m_camUp = GetLocalY(camLocWs.GetRotation());
	renderCam.m_camDir = GetLocalZ(camLocWs.GetRotation());
	renderCam.m_position = camLocWs.GetTranslation();

	const float nearDist = renderCam.m_nearDist;

	const Vector planeNormalWs = renderCam.GetDir();
	const Point planeCenterWs = renderCam.m_position + planeNormalWs * nearDist;
	const Scalar kMaxIntersectingDist = SCALAR_LC(2.0f);
	
	const bool debugDraw = g_renderOptions.m_displayIsClippingCamera && DebugSelection::Get().IsProcessOrNoneSelected(this);

	// This test is a quick way to avoid all this, which happens if we're far away from the center
	if (DistSqr(GetAnimData()->m_jointCache.GetJointLocatorWs(0).GetTranslation(), planeCenterWs) > Sqr(kMaxIntersectingDist) && !debugDraw)
	{
		return false;
	}

	// to clip, the intersection point must be within a circle around the near plane
	Vec4 frustumPointArray[8];
	renderCam.GetFrustumPoints(frustumPointArray, Vector(kZero));
	const Scalar clipRadius = Dist(Point(kOrigin) + Vector(frustumPointArray[0]), planeCenterWs);

	if (debugDraw)
	{
		g_prim.Draw(DebugCircle(planeCenterWs, planeNormalWs, clipRadius, kColorBlue));
	}

	if (pNearestApproach)
		*pNearestApproach = kLargestFloatSqrt;

	const Quat cameraRot = renderCam.GetRotation();
	const float cameraWidth = Dist(Vector(frustumPointArray[0]), Vector(frustumPointArray[1]));
	const float cameraHeight = Dist(Vector(frustumPointArray[0]), Vector(frustumPointArray[2]));
	const Vec2 cameraWidthHeight = Vec2(cameraWidth, cameraHeight);

	return CharacterCollision::IsClippingCameraPlane(m_pRagdoll, 
													 false,
													 surfaceExpand, 
													 -1.f,
													 planeCenterWs, 
													 planeNormalWs, 
													 cameraRot,
													 cameraWidthHeight,
													 clipRadius, 
													 pNearestApproach, 
													 debugDraw);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NdProcessRagdoll::WaterDepth() const
{
	const SplasherSkeletonInfo* pSplasherSkel = GetSplasherController() ? GetSplasherController()->GetSkel() : nullptr;
	if (pSplasherSkel)
	{
		float waterDepth[2] = {0.0f, 0.0f};

		Point chrPos = GetTranslation();

		for (int iLeg=0; iLeg<2; iLeg++)
		{
			int footJoint = pSplasherSkel->GetFootInWaterJoint(iLeg);
			if (footJoint >= 0)
			{
				Point waterSurfacePos;
				bool bIsUnderwater = pSplasherSkel->GetWaterSurfacePos(footJoint, &waterSurfacePos, nullptr);
				if (bIsUnderwater)
					waterDepth[iLeg] = waterSurfacePos.Y() - chrPos.Y();
			}
		}

		return Max(0.0f, Lerp(waterDepth[0], waterDepth[1], 0.5f));
	}

	return 0.0f;
}
