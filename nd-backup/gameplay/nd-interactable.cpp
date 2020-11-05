/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nd-interactable.h"

#include "corelib/system/atomic-lock.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/highlights-mgr.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/camera/camera-manager.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nd-process-ragdoll.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/entitydb.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "gamelib/state-script/ss-manager.h"
#include "gamelib/render/gui2/hud2-root.h"

//static const float kStdAlpha = 0.4f;
bool g_suppressInteractable = false;

const char* InteractableSelectionToString(DC::InteractableSelection e)
{
	switch (e)
	{
	case DC::kInteractableSelectionOutOfRange: return "out-of-range";
	case DC::kInteractableSelectionInSelectionRange: return "in-selection";
	case DC::kInteractableSelectionInInteractionRange: return "in-interaction";
	}
	return "???";
}

struct InteractableActiveSet
{
	static const I32F kCapacity = 128;

	void Clear(I64 frameNumber);
	void Add(I64 frameNumber, NdGameObject* pInteractable);
	bool IsMember(const NdGameObject* pInteractable) const;
	NdGameObject* GetAt(I64 frameNumber, I32F i) const
	{
		GAMEPLAY_ASSERT(i >= 0 && i < kCapacity);
		if (frameNumber % 2 == 0)
		{
			return m_aahInteractable0[i].ToMutableProcess();
		}
		else
		{
			return m_aahInteractable1[i].ToMutableProcess();
		}
	}

private:
	MutableNdGameObjectHandle m_aahInteractable0[kCapacity];
	MutableNdGameObjectHandle m_aahInteractable1[kCapacity];
};

static InteractableActiveSet	s_interactableActiveSet;
static NdAtomicLock				s_interactableLock;

InteractableOptions g_interactableOptions;

// -------------------------------------------------------------------------------------------------
// NdGameObject::DebugDrawPickups
// -------------------------------------------------------------------------------------------------

void NdGameObject::DebugDrawPickups()
{
	STRIP_IN_FINAL_BUILD;

	if (!g_interactableOptions.DebugPlayerPickups())
	{
		return;
	}

	NdInteractControl* pInteractControl = GetInteractControl();
	if (!pInteractControl)
	{
		return;
	}

	if (!pInteractControl->IsPickup())
	{
		return;
	}

	const Point p = GetTranslation();
	const Vector v(0.0f, 20.0f, 0.0f);

	DebugLine d(p, p + v, kColorYellow, 1.0f);

	g_prim.Draw(d, kPrimDuration1FramePauseable);

	DebugString s(p + 2.5f*Vector(kUnitYAxis), DevKitOnly_StringIdToString(GetUserId()), kColorYellow, 0.5f);
	g_prim.Draw(s, kPrimDuration1FramePauseable);
}

// -------------------------------------------------------------------------------------------------
// NdInteractControl
// -------------------------------------------------------------------------------------------------

bool g_testDisableAdjustForTiltedFloor = false;

//static
//void NdInteractControl::AdjustApRefForTiltedFloor(BoundFrame& adjustedApRef, const BoundFrame& originalApRef, NdGameObject* pInteractable, NdGameObjectHandle hUser)
//{
//	adjustedApRef = originalApRef;
//	const Locator originalLoc = originalApRef.GetLocatorWs();
//
//	const EntitySpawner* pTiltedSpaceSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByNameId(EngineComponents::GetNdGameInfo()->m_cameraSpaceSpawner); // misnomer -- should be m_tiltedSpaceSpawner or something
//	if (pTiltedSpaceSpawner && !g_testDisableAdjustForTiltedFloor)
//	{
//		const Quat rotWs = originalLoc.GetRotation();
//		const Point posWs = originalLoc.GetTranslation();
//
//		const Vector apForwardWs = GetLocalZ(rotWs);
//		const Vector apUpWs = GetLocalY(rotWs);
//
//		//F32 g_uphillShift = -0.25f; // hack -- should really calculate this based on expected grab height and angle of cam space spawner
//
//		const Locator tiltedSpace = pTiltedSpaceSpawner->GetBoundFrame().GetLocatorWs();
//		const Locator worldSpace(kIdentity);
//		const Locator cameraSpace = Lerp(worldSpace, tiltedSpace, EngineComponents::GetNdGameInfo()->m_cameraSpaceBlend);
//
//		const Vector cameraUpWs = GetLocalY(cameraSpace.GetRotation());
//		float dotProdCameraUpAndApUp = Dot(apUpWs, cameraUpWs); dotProdCameraUpAndApUp = Clamp(dotProdCameraUpAndApUp, -1.f, 1.f);
//		float angleRadCameraUpAndApUp = Acos(dotProdCameraUpAndApUp);
//		float angleDegCameraUpAndApUp = RADIANS_TO_DEGREES(angleRadCameraUpAndApUp);
//		float uphillShift = angleDegCameraUpAndApUp * -0.027f;	// magic number...
//
//		const Vector cameraFwd = GetLocalZ(cameraSpace.GetRotation());
//		//const Vector uphillWs = SafeNormalize(Cross(GetLocalZ(rotWs), cameraForward), cameraForward);
//
//		const Vector cameraFwdInOriginalApSpace = originalLoc.UntransformVector(cameraFwd);
//		Vector cameraFwdInOriginalApSpaceXY = cameraFwdInOriginalApSpace;
//		// find camera fwd is closer to XY plane or ZY plane.
//		if (abs(cameraFwdInOriginalApSpace.X()) < abs(cameraFwdInOriginalApSpace.Z()))
//			cameraFwdInOriginalApSpaceXY.SetX(0.f);
//		else
//			cameraFwdInOriginalApSpaceXY.SetZ(0.f);
//		cameraFwdInOriginalApSpaceXY = SafeNormalize(cameraFwdInOriginalApSpaceXY, kZero);
//		const Vector projectedCameraFwd = originalLoc.TransformVector(cameraFwdInOriginalApSpaceXY);
//
//		const Vector uphillWs = projectedCameraFwd;
//
//		const Scalar cosine = Abs(Dot(uphillWs, apForwardWs));
//
//		const Quat levelRotWs = LevelQuat(rotWs);
//		const Quat adjustedRotWs = Slerp(levelRotWs, rotWs, cosine);
//
//		// shift the apRef slightly down the incline to recenter the character / prevent clip-through
//		const Vector shiftWs = Scalar(uphillShift) * uphillWs;
//		const Point adjustedPosWs = posWs + shiftWs;
//
//		adjustedApRef = BoundFrame(adjustedPosWs, adjustedRotWs, originalApRef.GetBinding());
//
//#ifndef FINAL_BUILD
//		if (g_interactableOptions.m_drawEntry)
//		{
//			g_prim.Draw(DebugCoordAxesLabeled(cameraSpace, "cameraS", 0.3f, PrimAttrib(kPrimDisableDepthTest), 3.f, kColorWhite, 0.7f));
//			g_prim.Draw(DebugCoordAxesLabeled(tiltedSpace, "tiltedS", 0.3f, PrimAttrib(kPrimDisableDepthTest), 3.f, kColorWhite, 0.7f));
//			g_prim.Draw(DebugArrow(posWs, cameraFwd, kColorOrange, 0.5f, PrimAttrib(kPrimDisableDepthTest)));
//			g_prim.Draw(DebugArrow(posWs, uphillWs, kColorMagenta, 0.5f, PrimAttrib(kPrimDisableDepthTest)));
//			g_prim.Draw(DebugCoordAxesLabeled(adjustedApRef.GetLocatorWs(), "adjustedAp", 0.3f, PrimAttrib(kPrimDisableDepthTest), 3.f, kColorWhite, 0.7f));
//		}
//#endif
//	}
//	else if(pInteractable && hUser.HandleValid() && pUser->GetAnimControl())
//	{
//		// if we're not in tilted building, then adjust the ap ref to the height of the player's ap ref if he plays the animation
//		// from where their height is right now
//		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
//		StringId64 animId = pInteractCtrl->GetAnimationId(pInteractable, pUser);
//		Locator playerApRefLs;
//		if(animId != INVALID_STRING_ID_64 && EvaluateChannelInAnim(pUser->GetAnimControl(), animId, SID("apReference"), 0.0f, &playerApRefLs))
//		{
//			Locator playerApRefWs = pUser->GetBoundFrame().GetLocator().TransformLocator(playerApRefLs);
//			Vector offsetFromUser = playerApRefWs.GetPosition() - adjustedApRef.GetLocator().GetPosition();
//			offsetFromUser.SetX(0);
//			offsetFromUser.SetZ(0);
//			adjustedApRef.AdjustTranslationWs(offsetFromUser);
//		}
//	}
//
//#ifndef FINAL_BUILD
//	if (g_interactableOptions.m_drawEntry)
//		g_prim.Draw(DebugCoordAxesLabeled(originalLoc, "origAp", 0.3f, PrimAttrib(kPrimDisableDepthTest), 3.f, kColorWhite, 0.7f));
//#endif
//}

void NdInteractControl::Init()
{
	s_interactableActiveSet.Clear(0);
	s_interactableActiveSet.Clear(1);
}

void NdInteractControl::ShutDown()
{}

Quat NdInteractControl::GetRotationForInteract() const
{
	const NdGameObject* pSelf = m_hSelf.ToProcess();
	return pSelf->GetRotation();
}

float NdInteractControl::GetLookAngleDiff(const Locator& cameraLoc, Point_arg targetPosWs)
{
	const Point camPos = cameraLoc.GetTranslation();
	const Vector camDir = GetLocalZ(cameraLoc.GetRotation());

	Vector camToTargetWs = SafeNormalize(targetPosWs - camPos, -camDir);

	F32 targetAngleRad = Acos(Dot(camToTargetWs, camDir));
	F32 targetAngleDeg = RADIANS_TO_DEGREES(targetAngleRad);

	return MinMax(targetAngleDeg, 0.f, 180.f);
}

bool NdInteractControl::IsWithinLookAngle(const Locator& cameraLoc, Point_arg targetPosWs, float angleDeg)
{
	const Point camPos = cameraLoc.GetTranslation();
	const Vector camDir = GetLocalZ(cameraLoc.GetRotation());

	Vector camToTargetWs = SafeNormalize(targetPosWs - camPos, -camDir);
	//F32 targetAngleRad = Acos(Dot(camToTargetWs, camDir));
	//F32 targetAngleDeg = RADIANS_TO_DEGREES(targetAngleRad);

	Quat rotTargetToCamera = QuatFromVectors(camDir, camToTargetWs);
	Vector vecTargetToCamera = Rotate(rotTargetToCamera, kUnitXAxis);

	float targetAngleRad = Acos(Dot(vecTargetToCamera, kUnitXAxis));
	float targetAngleDeg = RADIANS_TO_DEGREES(targetAngleRad);

	return targetAngleDeg < angleDeg;
}

bool NdInteractControl::IsWithinLookAngle(const Locator& cameraLoc, Point_arg targetPosWs, float angleHoriDeg, float angleVertDeg)
{
	const Point camPos = cameraLoc.GetTranslation();
	const Vector camDir = GetLocalZ(cameraLoc.GetRotation());

	Vector camToTargetWs = SafeNormalize(targetPosWs - camPos, -camDir);
	//F32 targetAngleRad = Acos(Dot(camToTargetWs, camDir));
	//F32 targetAngleDeg = RADIANS_TO_DEGREES(targetAngleRad);

	Quat rotTargetToCamera = QuatFromVectors(camDir, camToTargetWs);
	Vector vecTargetToCamera = Rotate(rotTargetToCamera, kUnitXAxis);

	Vector vecTargetToCameraHori = vecTargetToCamera; vecTargetToCameraHori.SetY(0.f);
	Vector vecTargetToCameraHoriNorm = SafeNormalize(vecTargetToCameraHori, kUnitXAxis);

	Vector vecTargetToCameraVert = vecTargetToCamera; vecTargetToCameraVert.SetZ(0.f);
	Vector vecTargetToCameraVertNorm = SafeNormalize(vecTargetToCameraVert, kUnitXAxis);

	float targetAngleHoriRad = Acos(Dot(vecTargetToCameraHoriNorm, kUnitXAxis));
	float targetAngleHoriDeg = RADIANS_TO_DEGREES(targetAngleHoriRad);

	float targetAngleVertRad = Acos(Dot(vecTargetToCameraVertNorm, kUnitXAxis));
	float targetAngleVertDeg = RADIANS_TO_DEGREES(targetAngleVertRad);

	//if (targetAngleDeg > pDef->m_interactionAngleDegreesHori)
	//	return false;
	return (targetAngleHoriDeg < angleHoriDeg && targetAngleVertDeg < angleVertDeg);
}

///---------------------------------------------------------------------------------------------------///
bool NdInteractControl::IsInActivationRange(NdGameObjectHandle hUser, Point_arg userPos, const Point* pPredictPos) const
{
	if (m_disabledFN != -1 && m_disabledFN >= EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused)
		return false;

	// NOTE: We used to use player velocity to allow him to pick up objects that he was not facing.
	// But in T1, with the whole "look at to select" system, it makes no sense to check facing (all
	// that matters is the camera), so we're not doing this anymore.
	//Scalar playerVel = 0.0f;

	const Point grabPosWs = GetSelectionTargetWs(hUser);

	ActivationThresholds thresholds;
	GetActivationThresholds(&thresholds, hUser);

	const DC::InteractableDef* pDef = GetDef();

	if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawActivationRegion))
	{
		const NdGameObject* pSelf = m_hSelf.ToProcess();

		if (DebugSelection::Get().IsProcessOrNoneSelected(pSelf))
		{
			if (thresholds.m_useRegion)
			{
				if (thresholds.m_hRegion.GetTarget())
					thresholds.m_hRegion.GetTarget()->DebugDraw();
			}
			else
			{
				g_prim.Draw(DebugCross(grabPosWs, 0.1f, 1.0f, kColorMagenta, kPrimEnableHiddenLineAlpha));
				g_prim.Draw(DebugCircle(grabPosWs, kUnitYAxis, thresholds.m_radius, kColorMagenta, kPrimEnableHiddenLineAlpha));

				if (!g_interactableOptions.DebugPlayerPickups())
				{
					g_prim.Draw(DebugString(grabPosWs, DevKitOnly_StringIdToString(pSelf->GetUserId()), kColorWhite, 0.5f));
				}
			}
		}
	}

	bool isInActivationRange = false;
	if (thresholds.m_useRegion)
	{
		// if we use region, just ignore height threshold
		const Region* pRegion = thresholds.m_hRegion.GetTarget();
		if (pRegion != nullptr)
		{
			isInActivationRange = pRegion->IsInside(userPos, 0.f);

			//if (!isInActivationRange && pPredictPos != nullptr)
			//	isInActivationRange = pRegion->IsInside(*pPredictPos, 0.f);
		}
	}
	else
	{
		const Vector playerToGrab = grabPosWs - userPos;
		const Scalar playerToGrabY = playerToGrab.Y();

		if (playerToGrabY > thresholds.m_yMin && playerToGrabY < thresholds.m_yMax)
		{
			const Vector playerToGrabXZ = VectorFromXzAndY(playerToGrab, kZero);

			if (pPredictPos != nullptr && !pDef->m_ignoreUserPredictionPos)
			{
				const Vector playerToPredictPos = VectorFromXzAndY(*pPredictPos - userPos, kZero);
				const Vector playerToPredictNorm = SafeNormalize(playerToPredictPos, kZero);
				const float playerToPredictLen = Length(playerToPredictPos);
				const float dotP = Dot(playerToGrabXZ, playerToPredictNorm);
				const Point closestPt = userPos + Clamp(dotP, 0.f, playerToPredictLen) * playerToPredictNorm;
				const Scalar currentDistXZ = DistXz(closestPt, grabPosWs);
				if (currentDistXZ < thresholds.m_radius)
					isInActivationRange = true;
			}
			else
			{
				const Scalar currentDistXZ = Length(playerToGrabXZ);
				if (currentDistXZ < thresholds.m_radius)
					isInActivationRange = true;
			}
		}
	}

	// check camera lookat angle.
	if (isInActivationRange && pDef->m_activationCameraAngleDegrees > 0.f)
	{
		const U32 playerIndex = EngineComponents::GetNdGameInfo()->GetPlayerIndex(hUser);
		if (playerIndex < kMaxPlayers)
		{
			Point camPos = GameGlobalCameraLocator(playerIndex).GetTranslation();
			Vector camDir = GameGlobalCameraDirection(playerIndex);

			Vector camToTargetWs = SafeNormalize(grabPosWs - camPos, -camDir);
			F32 targetAngleRad = Acos(Dot(camToTargetWs, camDir));
			F32 targetAngleDeg = RADIANS_TO_DEGREES(targetAngleRad);

			if (targetAngleDeg > pDef->m_activationCameraAngleDegrees)
				isInActivationRange = false;
		}
	}

	return isInActivationRange;
}

///---------------------------------------------------------------------------------------------------///
bool NdInteractControl::IsInInteractionRange(NdGameObjectHandle hUser, Point_arg userPos, const Point* pPredictPos) const
{
	if (m_disableInteractionFN != -1 && m_disableInteractionFN >= EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused)
		return false;

	const NdGameObject* pSelf = m_hSelf.ToProcess();
	GAMEPLAY_ASSERT(pSelf && pSelf->GetInteractControl() == this);

	const DC::InteractableDef* pDef = GetDef();
	if (!pDef)
		return false;

	ActivationThresholds thresholds;
	GetInteractionThresholds(&thresholds, hUser);

	if (thresholds.m_useRegion)
	{
		const Region* pRegion = thresholds.m_hRegion.GetTarget();

		if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractionRegion && pRegion != nullptr))
		{
			if (DebugSelection::Get().IsProcessOrNoneSelected(pSelf))
				pRegion->DebugDraw();
		}

		if (!pRegion)
			return false;

		if (pPredictPos != nullptr && !pDef->m_ignoreUserPredictionPos)
		{
			if (!pRegion->IsInside(userPos, 0.f) && !pRegion->IsInside(*pPredictPos, 0.f))
				return false;
		}
		else
		{
			if (!pRegion->IsInside(userPos, 0.f))
				return false;
		}
	}
	else
	{
		const Point grabPosWs = GetInteractionPos(hUser);
		const Scalar interactionRadius = thresholds.m_radius;

		if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractionRegion))
		{
			if (DebugSelection::Get().IsProcessOrNoneSelected(pSelf))
			{
				g_prim.Draw(DebugCross(grabPosWs, 0.1f, 1.0f, kColorOrange, kPrimEnableHiddenLineAlpha));
				g_prim.Draw(DebugCircle(grabPosWs, kUnitYAxis, interactionRadius, kColorOrange, kPrimEnableHiddenLineAlpha));
			}
		}

		const Vector playerToGrab = grabPosWs - userPos;
		const Scalar playerToGrabY = playerToGrab.Y();
		if (playerToGrabY < thresholds.m_yMin || playerToGrabY > thresholds.m_yMax)
			return false;

		const Vector playerToGrabXZ = VectorFromXzAndY(playerToGrab, kZero);

		if (pPredictPos != nullptr && !pDef->m_ignoreUserPredictionPos)
		{
			const Vector playerToPredictPos = VectorFromXzAndY(*pPredictPos - userPos, kZero);
			const Vector playerToPredictNorm = SafeNormalize(playerToPredictPos, kZero);
			const float playerToPredictLen = Length(playerToPredictPos);
			const float dotP = Dot(playerToGrabXZ, playerToPredictNorm);
			const Point closestPt = userPos + Clamp(dotP, 0.f, playerToPredictLen) * playerToPredictNorm;
			const Scalar currentDistXZ = DistXz(closestPt, grabPosWs);
			if (currentDistXZ > interactionRadius)
				return false;
		}
		else
		{
			const Scalar currentDistXZ = Length(playerToGrabXZ);
			if (currentDistXZ > interactionRadius)
				return false;
		}
	}

	// finally check camera angle.
	if (pDef->m_interactionCameraAngleDegreesHori > 0.f && pDef->m_interactionCameraAngleDegreesVert > 0.f)
	{
		const Point targetPosWs = GetSelectionTargetWs();

		const U32 playerIndex = EngineComponents::GetNdGameInfo()->GetPlayerIndex(hUser);
		if (playerIndex < kMaxPlayers)
		{
			if (!IsWithinLookAngle(GameGlobalCameraLocator(playerIndex), targetPosWs, pDef->m_interactionCameraAngleDegreesHori, pDef->m_interactionCameraAngleDegreesVert))
				return false;
		}
	}

	if (pDef->m_disallowInteractionAngleDegreesHori > 0.f)
	{
		const Point targetPosWs = GetSelectionTargetWs();
		const Vector objFwdNorm = GetLocalZ(GetRotationForInteract());
		const Vector objToUser = userPos - targetPosWs;
		const Vector objToUserNorm = SafeNormalize(objToUser, kZero);
		const float dotP = Dot(objFwdNorm, objToUserNorm);
		if (dotP > Cos(DEGREES_TO_RADIANS(pDef->m_disallowInteractionAngleDegreesHori)))
			return false;
	}

	if (pDef->m_allowInteractionAngleDegreesHori > 0.f)
	{
		const Point targetPosWs = GetSelectionTargetWs();
		const Vector objFwdNorm = GetLocalZ(GetRotationForInteract());
		const Vector objToUserXz = VectorFromXzAndY(userPos - targetPosWs, kZero);
		const Vector objToUserXzNorm = SafeNormalize(objToUserXz, kZero);
		const float dotP = Dot(objFwdNorm, objToUserXzNorm);
		if (dotP < Cos(DEGREES_TO_RADIANS(pDef->m_allowInteractionAngleDegreesHori)))
			return false;
	}

	return true;
}

///---------------------------------------------------------------------------------------------------///
void NdInteractControl::GetActivationThresholds(ActivationThresholds* pThresholds, NdGameObjectHandle hUser) const
{
	const DC::InteractableDef* pDef = GetDef();

	pThresholds->m_yMin = pDef ? pDef->m_activationHeightThresholdYMin : -0.5f;
	pThresholds->m_yMax = pDef ? pDef->m_activationHeightThresholdYMax : 2.5f;

	pThresholds->m_radius = pDef ? Max(pDef->m_activationRadius, pDef->m_interactionRadius) : 0.f;
	pThresholds->m_useRegion = false;
	pThresholds->m_hRegion = RegionHandle();

	StringId64 regionNameId = GetActivationRegionNameId();
	if (regionNameId != INVALID_STRING_ID_64)
	{
		pThresholds->m_hRegion.SetByName(regionNameId);
		pThresholds->m_useRegion = true;
	}
}

///---------------------------------------------------------------------------------------------------///
void NdInteractControl::GetInteractionThresholds(ActivationThresholds* pThresholds, NdGameObjectHandle hUser) const
{
	const DC::InteractableDef* pDef = GetDef();

	pThresholds->m_yMin = pDef ? pDef->m_interactionHeightThresholdYMin : -10000.0f;
	pThresholds->m_yMax = pDef ? pDef->m_interactionHeightThresholdYMax : 10000.0f;

	pThresholds->m_radius = pDef ? pDef->m_interactionRadius : 0.f;
	pThresholds->m_useRegion = false;
	pThresholds->m_hRegion = RegionHandle();

	StringId64 regionNameId = GetInteractionRegionNameId();
	if (regionNameId != INVALID_STRING_ID_64)
	{
		pThresholds->m_hRegion.SetByName(regionNameId);
		pThresholds->m_useRegion = true;
	}
}

///---------------------------------------------------------------------------------------------------///
Point NdInteractControl::GetInteractionPos(NdGameObjectHandle hUser, const Point userWs /* = kInvalidPoint*/) const
{
	const NdGameObject* pSelf = m_hSelf.ToProcess();
	ASSERT(pSelf && pSelf->GetInteractControl() == this);

	const DC::InteractableDef* pDef = GetDef();
	if (pDef)
	{
		Locator locWs = pSelf->GetLocator();
		if (!pDef->m_activationCenterOnAlign)
		{
			Point selectionTarget = GetSelectionTargetWs(hUser, userWs);
			if (hUser.HandleValid() && pDef->m_interactionSphereHeightUsePlayerLoc)
			{
				const NdGameObject* pUser = hUser.ToProcess();
				locWs.SetTranslation(PointFromXzAndY(selectionTarget, pUser->GetTranslation()));
			}
			else
			{
				// KANXU: I don't see any reason we should use object's location's Y, which can be so different from selection target.
				// and we should always make activation region and interaction region consistent.
				//locWs.SetTranslation(PointFromXzAndY(selectionTarget, locWs.GetTranslation()));
				locWs.SetTranslation(selectionTarget);
			}
		}

		const Point activationOffsetLs(pDef->m_activationOffsetX, pDef->m_activationOffsetY, pDef->m_activationOffsetZ);
		const Point centerWs = locWs.TransformPoint(activationOffsetLs);
		return centerWs;
	}

	return Point(kOrigin);
}

bool NdInteractControl::IsHighPriority() const
{
	const DC::InteractableDef* pDef = GetDef();
	bool isHighPri = (pDef ? pDef->m_highPriority : false);
	return isHighPri;
}

bool NdInteractControl::IsAutoInteract() const
{
	const DC::InteractableDef* pDef = GetDef();
	bool autoInteract = (pDef ? pDef->m_autoInteract : false);
	return autoInteract;
}

bool NdInteractControl::IsAccessibilityAutoPickupAllowed() const
{
	if (AreAccessibilityCuesDisabled())
		return false;

	const DC::InteractableDef* pDef = GetDef();
	const bool autoPickupAllowedByDef = (pDef ? pDef->m_allowAccessibilityAutoPickup : false);
	return autoPickupAllowedByDef && !IsAutoPickupDisabled();
}

bool NdInteractControl::AreAccessibilitySoundCuesAllowed() const
{
	if (AreAccessibilityCuesDisabled())
		return false;

	return true;
}

//static
void NdInteractControl::ActivateInteractable(NdGameObject* pInteractable)
{
	AtomicLockJanitor janitor(&s_interactableLock, FILE_LINE_FUNC);
	I64 frameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	s_interactableActiveSet.Add(frameNumber, pInteractable);
}

//static
void NdInteractControl::ClearOldFrame()
{
	AtomicLockJanitor janitor(&s_interactableLock, FILE_LINE_FUNC);
	I64 frameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	s_interactableActiveSet.Clear(frameNumber - 1);
}

//static
I32F NdInteractControl::GetActiveInteractables(NdGameObjectHandle hUser, I32F capacity, NdGameObject* apInteractable[])
{
	AtomicLockJanitor janitor(&s_interactableLock, FILE_LINE_FUNC);

	I32F index = 0;

	if (hUser.HandleValid() && hUser.IsType(SID("Player")))
	{
		U32 playerIndex = EngineComponents::GetNdGameInfo()->GetPlayerIndex(hUser);
		I64 frameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;

		if (playerIndex < kMaxPlayers)
		{
			for (I32F i = 0; i < InteractableActiveSet::kCapacity; ++i)
			{
				NdGameObject* pInteractable = s_interactableActiveSet.GetAt(frameNumber, i);
				if (pInteractable)
				{
					if (index < capacity)
					{
						apInteractable[index] = pInteractable;
						++index;
					}
					else
					{
						break;
					}
				}
			}
		}
	}

	return index;
}

//static
const char* NdInteractControl::GetAvailabilityAsString(InteractAvailability avail)
{
	switch (avail)
	{
	case kInteractableInvalid:		return "invalid";
	case kInteractableUnavailable:	return "unavailable";
	case kInteractableAvailable:	return "available";
	default:						return "???";
	}
}

bool NdInteractControl::GetEntryLocation(NdGameObjectHandle hUser, BoundFrame& entry, StringId64* outAnimId)
{
	if (outAnimId)
		*outAnimId = INVALID_STRING_ID_64;
	return false;
}

bool NdInteractControl::GetApReference(NdGameObjectHandle hUser, BoundFrame& apRef)
{
	return false;
}

int NdInteractControl::GetGrabPoints(Vector grabPointsLs[], int grabPointsCapacity, const Locator& playerLoc)
{
	// return zero to use the (one) default grab point -- derived classes may override to provide multiple grab points
	return 0;
}

void NdInteractControl::RequestInteract(MutableNdGameObjectHandle hUser, bool interactButtonHeld, bool interactButtonClicked, bool isPickupAll, bool isAccessibilityAutoInteract)
{
	SendEvent(SID("interaction-requested"), m_hSelf);
	m_interactRequestCount++;
}

void NdInteractControl::Interact(MutableNdGameObjectHandle hUser, const NdInteractArgs* pOptionalArgs, bool* pEquipped)
{
	if (pEquipped)
		*pEquipped = false;

	// call the virtual hook
	OnInteract(hUser, pOptionalArgs, pEquipped);

	SetObjectHasBeenInteracted(true);

	if (IsInteractionSingleUse())
		SetInteractionEnabled(false);

	// send the SID("interact") event to the script attached to the interactable, if any
	NdGameObject* pSelf = m_hSelf.ToMutableProcess();
	if (pSelf)
	{
		SendEvent(SID("interact"), pSelf);
	}
}

void NdInteractControl::OnInteract(MutableNdGameObjectHandle hUser, const NdInteractArgs* pOptionalArgs, bool* pEquipped)
{
	// do nothing by default -- derived classes may override
}

void NdInteractControl::EndInteraction()
{
	// do nothing by default -- derived classes may override
}

void NdInteractControl::PopulateHud2Request(Hud2::InteractableRequest& request, NdGameObjectHandle hUser) const
{
	// get the HUD icon from the interactable-def by default (derived classes may override)
	const DC::InteractableDef* pDef = GetDef();
	if (pDef && !pDef->m_hud2ButtonIcon.IsEmpty())
	{
		request.m_iconButton = pDef->m_hud2ButtonIcon.GetString();
	}
	else
	{
		request.m_iconButton = "html/missing.png";
	}
	if (pDef && !pDef->m_hud2IconInteract.IsEmpty())
		request.m_iconInteract = pDef->m_hud2IconInteract.GetString();
	else
		request.m_iconInteract = nullptr;

	if (pDef && !pDef->m_hud2IconSecondary.IsEmpty())
		request.m_iconAlt = pDef->m_hud2IconSecondary.GetString();
	else
		request.m_iconAlt = nullptr;

	if (!pDef->m_hud2WidgetClass.IsEmpty())
		request.m_widgetClass = pDef->m_hud2WidgetClass.GetString();
	else
		request.m_widgetClass = "hud-interactable/interact-simple";

	request.m_whichIconAndText = Hud2::InteractIconType::kShortGun;
	request.m_iconItem = nullptr; // derived classes should populate this
	request.m_hInteractable = m_hSelf;
	request.m_interactableDefId = GetDefId();
	request.m_itemId = INVALID_STRING_ID_64;
	request.m_abstractInteractId = INVALID_STRING_ID_64;
	request.m_selectionRange = request.m_params.m_isPreSelectedOnly ? DC::kInteractableSelectionInSelectionRange : DC::kInteractableSelectionInInteractionRange;
	request.m_alphaByDistance = pDef->m_alphaByDistance;
	request.m_alphaWhenOccluded = pDef->m_alphaWhenOccluded;
	request.m_showWhenOffscreen = AllowOffscreen();
	request.m_countsAsPickupPrompt = pDef->m_countsAsPickupPrompt;
	request.m_countsAsInteractPrompt = pDef->m_countsAsInteractPrompt;
	request.m_descCipherId = pDef->m_descCipherId;
	request.m_unavailableCipherId = pDef->m_unavailableCipherId;
	request.m_disableActivationCircle = pDef->m_disableActivationHudIcon;

	request.m_activationRegionUsed = GetActivationRegionNameId() != INVALID_STRING_ID_64;
//	request.m_hudElementScale = pDef->m_hudElementScale;
//	request.m_interactTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();
//	request.m_interactAvail = (pInteractable && pUser) ? GetAvailability(pInteractable, pUser) : kInteractableInvalid;
}

I32 NdInteractControl::GetHudAmmoCount()
{
	return 0;
}

StringId64 NdInteractControl::GetInteractCommandId() const
{
	const DC::InteractableDef* pDef = GetDef();
	return pDef ? pDef->m_interactCommandId : INVALID_STRING_ID_64;
}

bool NdInteractControl::ProbesFromCameraLocation() const
{
	const DC::InteractableDef* pDef = GetDef();
	return pDef && pDef->m_probeFromCameraLocation;
}

///---------------------------------------------------------------------------------------------------///
bool NdInteractControl::AllowOffscreen() const
{
	const DC::InteractableDef* pDef = GetDef();
	return pDef && pDef->m_allowOffscreen;
}

///---------------------------------------------------------------------------------------------------///
bool NdInteractControl::IsOffscreen(NdGameObjectHandle hUser) const
{
	const DC::InteractableDef* pDef = GetDef();
	if (pDef != nullptr)
	{
		const Point targetPos = GetSelectionTargetWs(hUser);

		const CameraLocation gameCameraInfo = GameCameraInfo();
		RenderCamera testCam;

		const Point cameraPos = gameCameraInfo.GetLocator().GetTranslation();
		const Vector cameraDir = GetLocalZ(gameCameraInfo.GetLocator().GetRotation());
		const Vector cameraUp = GetLocalY(gameCameraInfo.GetLocator().GetRotation());

		const RenderCamera realCamera = g_mainCameraInfo[0].GetRenderCamera();
		testCam.SetupCamera(cameraPos, cameraDir, cameraUp, realCamera.m_nearDist, realCamera.m_farDist, realCamera.m_zoom, realCamera.m_yAspect, realCamera.m_orthographic);

		if (pDef->m_offscreenRadius > 0.f)
		{
			if (!testCam.IsSphereInFrustum(Sphere(targetPos, pDef->m_offscreenRadius)))
				return true;
		}
		else if (!testCam.IsPointInFrustum(targetPos))
		{
			return true;
		}
	}

	return false;
}

///---------------------------------------------------------------------------------------------------///
bool NdInteractControl::IsInNavAssistRegion(NdGameObjectHandle hUser) const
{
	const NdGameObject* pUser = hUser.ToProcess();
	if (!pUser)
		return false;

	const Point userPos = pUser->GetTranslation();

	if (m_navAssistRegionNameId != INVALID_STRING_ID_64)
	{
		RegionHandle hRegion;
		hRegion.SetByName(m_navAssistRegionNameId);

		const Region* pRegion = hRegion.GetTarget();
		if (pRegion != nullptr)
		{
			return pRegion->IsInside(userPos, 0.f);
		}
	}

	return false;
}

// -------------------------------------------------------------------------------------------------
// InteractableActiveSet
// -------------------------------------------------------------------------------------------------

void InteractableActiveSet::Clear(I64 frameNumber)
{
	bool clear0 = frameNumber % 2 == 0;

	for (I32F i = 0; i < kCapacity; ++i)
	{
		if (clear0)
			m_aahInteractable0[i] = nullptr;
		else
			m_aahInteractable1[i] = nullptr;
	}
}

void InteractableActiveSet::Add(I64 frameNumber, NdGameObject* pInteractable)
{
	if (!pInteractable)
		return;

	MutableNdGameObjectHandle* pArray = (frameNumber % 2 == 0) ? m_aahInteractable0 : m_aahInteractable1;

	I32F iFreeSlot = kCapacity;
	for (I32F i = 0; i < kCapacity; ++i)
	{
		if (pInteractable == pArray[i].ToProcess())
			return; // already active

		if (!pArray[i].HandleValid())
		{
			iFreeSlot = i;
			break;
		}
	}
	GAMEPLAY_ASSERTF(iFreeSlot < kCapacity, ("too many interactable objects in close proximity -- increase InteractableActiveSet::kCapacity"));

	if (iFreeSlot < kCapacity)
	{
		pArray[iFreeSlot] = pInteractable;
	}
}

bool InteractableActiveSet::IsMember(const NdGameObject* pInteractable) const
{
	for (I32F playerIndex = 0; playerIndex < kMaxPlayers; ++playerIndex)
	{
		for (I32F i = 0; i < kCapacity; ++i)
		{
			const NdGameObject* pCurrent = GetAt(playerIndex, i);
			if (pCurrent == pInteractable)
			{
				return true;
			}
		}
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// NdInteractControl
// -------------------------------------------------------------------------------------------------

NdInteractControl::NdInteractControl()
	: m_defId(INVALID_STRING_ID_64)
	, m_pCustomDef(nullptr)
	, m_selectUsingJointIndex(-1)
	, m_selectionTargetSpawner(INVALID_STRING_ID_64)
	, m_selectedFrameNumber(-1)
	, m_preSelectedFrameNumber(-1)
	, m_initialized(false)
	, m_wasSelected(false)
	, m_disabled(false)
	, m_unavailableScript(false)
	, m_triggerOnly(false)
	, m_interactionSingleUse(false)
	, m_partnerEnabled(true)
	, m_wasPreSelected(false)
	, m_iconPermanent(false)
	, m_hasBeenInteracted(false)
	, m_hintSoundPlayed(false)
	, m_ignoreProbes(false)
	, m_isReachable(false)
	, m_hackInteractablesMgrIgnoreDisabledHack(false)
	, m_lastInteractedTime(Seconds(0.0f))
	, m_interactHighlightBrightness(0.0f)
	, m_selectHighlightBrightness(0.0f)
	, m_hcmModeOverride(MAYBE::kNothing)
	, m_customSelectionLocFunc(nullptr)
	, m_numHcmModeTargets(0)
	, m_accessibilityDisabled(false)
	, m_autoPickupDisabled(false)
{
	memset(&m_interactOverrideLambda, 0, sizeof(DC::ScriptLambda));
	memset(m_aHcmModeTargetObjId, 0, sizeof(m_aHcmModeTargetObjId));
}

NdInteractControl::~NdInteractControl()
{
}

Err NdInteractControl::Init(NdGameObject* pSelf, const ProcessSpawnInfo& spawn, const InitParams& params)
{
	ALWAYS_ASSERTF(!m_initialized, ("Attempt to re-initialize an NdInteractControl"));

	if (!pSelf)
		return Err::kErrBadData;

	m_hSelf = pSelf;

	m_selectUsingJointIndex = -1;
	m_selectedFrameNumber = -2;
	m_preSelectedFrameNumber = -2;
	m_lastInteractedTime = TimeFramePosInfinity();
	m_interactNavLocationsLastUpdateFrame = -1;
	m_numInteractNavLocations = 0;
	m_wasSelected = false;
	m_wasPreSelected = false;
	m_disabled = false;
	m_hackInteractablesMgrIgnoreDisabledHack = false;
	//m_highlightAlpha = kStdAlpha;
	m_interactHighlightBrightness = 0.0f;
	m_selectHighlightBrightness = 0.0f;
	m_hasBeenInteracted = false;
	m_hintSoundPlayed = false;
	m_unavailableSeenTime = TimeFrameZero();

	m_customSelectionLocFunc = nullptr;

	m_defId = params.m_useDefaultDefId ? params.m_defaultDefId : spawn.GetData<StringId64>(SID("interactable-def"), params.m_defaultDefId);
	m_pCustomDef = nullptr;

	CacheDefPtr();

	m_activationRegionNameId = spawn.GetData<StringId64>(SID("interactable-activation-region"), INVALID_STRING_ID_64);
	m_interactionRegionNameId = spawn.GetData<StringId64>(SID("interactable-interaction-region"), INVALID_STRING_ID_64);
	m_navAssistRegionNameId = spawn.GetData<StringId64>(SID("navigation-assist-region"), INVALID_STRING_ID_64);

	m_interactRequestCount = 0;

	const DC::InteractableDef* pDef = GetDef();
	if (!pDef)
	{
		MsgConScriptError("%s: def '%s' not found -- OBJECT WILL NOT BE INTERACTABLE\n", pSelf->GetName(), DevKitOnly_StringIdToString(m_defId));
		return Err::kErrBadData;
	}

	const EntitySpawner* pSpawner = static_cast<const SpawnInfo&>(spawn).m_pSpawner;
	const EntityDB* pEntDb = pSpawner ? pSpawner->GetEntityDB() : nullptr;
	if (pEntDb)
	{
		static const Attr s_aAttr[] =
		{
			{ SID("activation-radius"),							kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_activationRadius) },
			{ SID("activation-height-threshold-y-min"),			kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_activationHeightThresholdYMin) },
			{ SID("activation-height-threshold-y-max"),			kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_activationHeightThresholdYMax) },
			{ SID("activation-camera-angle-degrees"),			kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_activationCameraAngleDegrees) },
			{ SID("activation-offset-x"),						kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_activationOffsetX) },
			{ SID("activation-offset-y"),						kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_activationOffsetY) },
			{ SID("activation-offset-z"),						kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_activationOffsetZ) },
			{ SID("activation-center-on-align"),				kAttrTypeBoolean,	MEMBER_OFFSET_OF(DC::InteractableDef, m_activationCenterOnAlign) },
			{ SID("alpha-when-occluded"),						kAttrTypeBoolean,	MEMBER_OFFSET_OF(DC::InteractableDef, m_alphaWhenOccluded) },
			{ SID("alpha-by-distance"),							kAttrTypeBoolean,	MEMBER_OFFSET_OF(DC::InteractableDef, m_alphaByDistance) },
			{ SID("select-stickiness-pct"),						kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_selectStickinessPct) },
			{ SID("select-using-joint"),						kAttrTypeStringId,	MEMBER_OFFSET_OF(DC::InteractableDef, m_selectUsingJoint) },
			{ SID("select-using-align"),						kAttrTypeBoolean,	MEMBER_OFFSET_OF(DC::InteractableDef, m_selectUsingAlign) },
			{ SID("select-offset-x"),							kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_selectOffsetX) },
			{ SID("select-offset-y"),							kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_selectOffsetY) },
			{ SID("select-offset-z"),							kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_selectOffsetZ) },
			{ SID("select-offset-ws-y"),						kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_selectOffsetWsY) },
			{ SID("select-edge-max-dist-unsel"),				kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_selectEdgeMaxDistUnsel) },
			{ SID("select-edge-max-dist-sel"),					kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_selectEdgeMaxDistSel) },
			{ SID("base-cost"),									kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_baseCost) },
			{ SID("base-cost-weight"),							kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_baseCostWeight) },
			{ SID("dist-cost-weight"),							kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_distCostWeight) },
			{ SID("trigger-only"),								kAttrTypeBoolean,	MEMBER_OFFSET_OF(DC::InteractableDef, m_triggerOnly) },
			{ SID("also-trigger-on-melee-attack"),				kAttrTypeBoolean,	MEMBER_OFFSET_OF(DC::InteractableDef, m_alsoTriggerOnMeleeAttack) },
			{ SID("interaction-radius"),						kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_interactionRadius) },
			{ SID("interaction-height-threshold-y-min"),		kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_interactionHeightThresholdYMin) },
			{ SID("interaction-height-threshold-y-max"),		kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_interactionHeightThresholdYMax) },
			{ SID("interaction-camera-angle-degrees-hori"),		kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_interactionCameraAngleDegreesHori) },
			{ SID("interaction-camera-angle-degrees-vert"),		kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_interactionCameraAngleDegreesVert) },
			{ SID("disallow-interaction-angle-degrees-hori"),	kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_disallowInteractionAngleDegreesHori) },
			{ SID("allow-interaction-angle-degrees-hori"),		kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_allowInteractionAngleDegreesHori) },
			{ SID("select-highlight-settings"),					kAttrTypeStringId,	MEMBER_OFFSET_OF(DC::InteractableDef, m_selectHighlightSettings) },
			{ SID("interact-highlight-settings"),				kAttrTypeStringId,	MEMBER_OFFSET_OF(DC::InteractableDef, m_interactHighlightSettings) },
			{ SID("probe-offset-y-ws"),							kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_probeOffsetYWs) },
			{ SID("ignore-probes"),								kAttrTypeBoolean,	MEMBER_OFFSET_OF(DC::InteractableDef, m_ignoreProbes) },
			{ SID("probe-radius"),								kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_probeRadius) },
			{ SID("num-around-corner-probe"),					kAttrTypeI32,		MEMBER_OFFSET_OF(DC::InteractableDef, m_numAroundCornerProbe) },
			{ SID("around-corner-step"),						kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_aroundCornerStep) },
			{ SID("interaction-single-use"),					kAttrTypeBoolean,	MEMBER_OFFSET_OF(DC::InteractableDef, m_interactionSingleUse) },
			{ SID("auto-interact"),								kAttrTypeBoolean,	MEMBER_OFFSET_OF(DC::InteractableDef, m_autoInteract) },
			{ SID("stat-id"),									kAttrTypeStringId,	MEMBER_OFFSET_OF(DC::InteractableDef, m_statId) },
			{ SID("dlc-stat-id"),								kAttrTypeStringId,	MEMBER_OFFSET_OF(DC::InteractableDef, m_dlcStatId) },
			{ SID("no-draw-icon"),								kAttrTypeBoolean,	MEMBER_OFFSET_OF(DC::InteractableDef, m_noDrawIcon) },
			{ SID("hud-icon-offset-y-ws"),						kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_hudIconOffsetYWs) },
			{ SID("allow-offscreen"),							kAttrTypeBoolean,	MEMBER_OFFSET_OF(DC::InteractableDef, m_allowOffscreen) },
			{ SID("offscreen-radius"),							kAttrTypeFloat,		MEMBER_OFFSET_OF(DC::InteractableDef, m_offscreenRadius) },
			{ SID("interact-command-id"),						kAttrTypeStringId,	MEMBER_OFFSET_OF(DC::InteractableDef, m_interactCommandId) },
			{ SID("high-contrast-mode-type"),					kAttrTypeStringId,	MEMBER_OFFSET_OF(DC::InteractableDef, m_highContrastModeType) },
			{ SID("high-contrast-mode-type-disabled"),			kAttrTypeStringId,	MEMBER_OFFSET_OF(DC::InteractableDef, m_highContrastModeTypeDisabled) },
			{ SID("hcm-should-set-parent-proxy"), kAttrTypeBoolean, MEMBER_OFFSET_OF(DC::InteractableDef, m_hcmShouldSetParentProxy) },
		};

		DC::InteractableDef* pNewDef = SetCustomAttribes(s_aAttr, ARRAY_ELEMENT_COUNT(s_aAttr), pEntDb, pDef);
		if (pNewDef != nullptr)
		{
			pDef = m_pCustomDef = pNewDef;
		}
	}

	m_selectionTargetSpawner = spawn.GetData<StringId64>(SID("selection-target-spawner"), INVALID_STRING_ID_64);

	m_triggerOnly = pDef->m_triggerOnly;
	m_ignoreProbes = pDef->m_ignoreProbes;
	m_probeOffsetYWs = pDef->m_probeOffsetYWs;

	m_interactionSingleUse = !spawn.GetData<bool>(SID("no-disable-on-trigger"), !pDef->m_interactionSingleUse);

	m_disabled = spawn.GetData<bool>(SID("disabled"), false);
	m_unavailableScript = spawn.GetData<bool>(SID("unavailable"), m_unavailableScript);
	m_partnerEnabled = spawn.GetData<bool>(SID("partner-enabled"), m_partnerEnabled);

	m_numHcmModeTargets = spawn.GetDataArray<StringId64>(SID("high-contrast-mode-target-obj"), m_aHcmModeTargetObjId, kMaxHcmTargetObjects, INVALID_STRING_ID_64);
	for (int i = 0; i < kMaxHcmTargetObjects; ++i)
	{
		m_aHcmModeTargetObj[i] = nullptr;
	}

	if (pDef->m_selectUsingJoint != INVALID_STRING_ID_64)
	{
		m_selectUsingJointIndex = pSelf->FindJointIndex(pDef->m_selectUsingJoint);
		if (m_selectUsingJointIndex < 0 && !pDef->m_allowSelectJointMissing)
		{
			MsgConScriptError("%s: select-using-joint '%s' not found -- will use bounding sphere\n",
				pSelf->GetName(),
				DevKitOnly_StringIdToString(pDef->m_selectUsingJoint));
		}
	}

	// set up glow highlight
	if (!params.m_dontAllocateShaderParams)
	{
		pSelf->AllocateShaderInstanceParams();
		pSelf->SetSpawnShaderInstanceParams(spawn);
	}

	//m_lastHighlightUpdateTime = GetProcessClock()->GetCurTime();

	m_initialized = true;

	pSelf->RegisterWithInteractablesMgr();

	UpdateHighContrastMode();
	return Err::kOK;
}

DC::InteractableDef* NdInteractControl::AllocateInteractableDef() const
{
	// this is how we new a script pointer.
	I32 size = sizeof(StringId64) + sizeof(DC::InteractableDef);
	char* pData = NDI_NEW char[size];

	StringId64* pType = reinterpret_cast<StringId64*>(pData);
	*pType = SID("interactable-def");

	StringId64* pNext = pType + 1;
	DC::InteractableDef* pDef = reinterpret_cast<DC::InteractableDef*>(pNext);
	memset(pDef, 0, sizeof(DC::InteractableDef));

	return pDef;
}

void NdInteractControl::CopyInteractableDef(DC::InteractableDef* pDst, const DC::InteractableDef* pSrc) const
{
	GAMEPLAY_ASSERT(ScriptManager::TypeOf(pSrc) == SID("game-interactable-def") || ScriptManager::TypeOf(pSrc) == SID("interactable-def"));
	GAMEPLAY_ASSERT(ScriptManager::TypeOf(pDst) == SID("interactable-def"));
	*pDst = *pSrc;
}

DC::InteractableDef* NdInteractControl::SetCustomAttribes(const Attr* aAttr, const I32 numAttr, const EntityDB* pEntDb, const DC::InteractableDef* pOldDef)
{
	//bool forceCustomDef = pSpawner->GetData<bool>(SID("custom-interactable-def"), false);
	const bool forceCustomDef = ForceAllocateCustomDef();

	GAMEPLAY_ASSERT(numAttr <= 64);

	U64 specified = 0u;
	{
		U64 mask = 1u;
		for (I32F iAttr = 0; iAttr < numAttr; ++iAttr, mask = mask << 1)
		{
			if (pEntDb->GetRecord(aAttr[iAttr].m_key))
				specified |= mask;
		}
	}

	if (specified != 0u || forceCustomDef)
	{
		DC::InteractableDef* pNewDef = AllocateInteractableDef();
		if (pNewDef)
		{
			CopyInteractableDef(pNewDef, pOldDef);

			U64 mask = 1u;
			for (I32F iAttr = 0; iAttr < numAttr; ++iAttr, mask = mask << 1)
			{
				const Attr& attr = aAttr[iAttr];
				if (0u != (mask & specified))
				{
					switch (attr.m_type)
					{
					case kAttrTypeFloat:
						{
							float* pNewValue = (float*)((U8*)pNewDef + attr.m_offset);
							*pNewValue = pEntDb->GetData<float>(attr.m_key, 0.0f); // should never use the default value thanks to 'specified' bits
						}
						break;
					case kAttrTypeI32:
						{
							I32* pNewValue = (I32*)((U8*)pNewDef + attr.m_offset);
							*pNewValue = pEntDb->GetData<I32>(attr.m_key, 0); // should never use the default value thanks to 'specified' bits
						}
						break;
					case kAttrTypeStringId:
						{
							StringId64* pNewValue = (StringId64*)((U8*)pNewDef + attr.m_offset);
							*pNewValue = pEntDb->GetData<StringId64>(attr.m_key, INVALID_STRING_ID_64); // should never use the default value thanks to 'specified' bits
						}
						break;
					case kAttrTypeBoolean:
						{
							// according to Dan, boolean in DC is 2 bytes, the second byte is padding.
							ASSERT(sizeof(bool) == 1);
							bool* pNewValue = (bool*)((U8*)pNewDef + attr.m_offset);
							bool newValue = pEntDb->GetData<bool>(attr.m_key, false);
							*pNewValue = newValue;
						}
						break;
					default:
						ASSERTF(false, ("NdInteractControl: WTF?"));
						break;
					}
				}
			}

			return pNewDef;
		}
	}

	return nullptr;
}

void NdInteractControl::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pCustomDef, delta, lowerBound, upperBound);
}

void NdInteractControl::Update()
{
	// Some game object types require UpdateInternal() to be called -- they will override this method and call it.

	UpdateHighContrastModeTarget();
}

void NdInteractControl::UpdateInternal()
{
	if (!m_initialized)
		return;

	const NdGameObject* pSelf = m_hSelf.ToProcess();
	if (!pSelf || (!pSelf->StopWhenPaused() && GetProcessDeltaTime() == 0.0f))
		return;

	GAMEPLAY_ASSERTF(m_lastFrameUpdated != EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused,
		("interactable-def: %s, object: %s", DevKitOnly_StringIdToString(GetDefId()), DevKitOnly_StringIdToString(pSelf->GetUserId())));
	m_lastFrameUpdated = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;

	UpdateHighContrastModeTarget();

	const bool isSelected = IsSelected();
	const bool isPreSelected = IsPreSelected();

	if (m_wasSelected != isSelected)
	{
		if (isSelected)
		{
			g_ssMgr.BroadcastEvent(SID("interact-highlighted"), pSelf->GetUserId());
		}
	}

	if (m_wasPreSelected != isPreSelected)
	{
		if (isPreSelected)
		{
			g_ssMgr.BroadcastEvent(SID("interact-selected"), pSelf->GetUserId());
		}
	}

	// no more highlights
	//HighlightInternal(pSelf, isPreSelected || isSelected || m_wasSelected || m_wasPreSelected, isSelected || m_wasSelected );

	m_wasSelected = isSelected;
	m_wasPreSelected = isPreSelected;

	// debug draw
	if (g_interactableOptions.m_drawSelectionTarget)
	{
		if (DebugSelection::Get().IsProcessOrNoneSelected(pSelf) && pSelf)
		{
			const Point targetPosWs = GetSelectionTargetWs();
			g_prim.Draw(DebugCross(targetPosWs, 0.2f, (isSelected ? kColorRed : kColorDarkGray), kPrimEnableHiddenLineAlpha));
		}
	}
}

void NdInteractControl::UpdateHighContrastMode()
{
	UpdateHighContrastModeTarget();
	UpdateHighContrastModeType();
}

bool NdInteractControl::AddHighContrastModeTarget(StringId64 userId, NdGameObject* pGo)
{
	if (m_numHcmModeTargets >= kMaxHcmTargetObjects)
		return false;

	m_aHcmModeTargetObjId[m_numHcmModeTargets] = userId;
	m_aHcmModeTargetObj[m_numHcmModeTargets] = pGo;

	++m_numHcmModeTargets;

	if (pGo)
		UpdateHighContrastModeType();
	return true;
}

bool NdInteractControl::HasMissingHighContrastModeTargets() const
{
	for (int i = 0; i < m_numHcmModeTargets; ++i)
	{
		if (m_aHcmModeTargetObjId[i] == INVALID_STRING_ID_64)
			continue;

		if (!m_aHcmModeTargetObj[i].HandleValid())
			return true;
	}
	return false;
}

void NdInteractControl::RemoveHighContrastModeTarget(StringId64 userId, Maybe<DC::HCMModeType> newHCMMode)
{
	for (int i = 0; i < m_numHcmModeTargets; ++i)
	{
		if (m_aHcmModeTargetObjId[i] == userId)
		{
			if (newHCMMode.Valid())
			{
				NdGameObject* pTarget = m_aHcmModeTargetObj[i].ToMutableProcess();
				if (!pTarget)
				{
					// maybe we havn't yet found it, double check here
					pTarget = NdGameObject::LookupGameObjectByUniqueId(userId);
				}

				if (pTarget)
				{
					pTarget->SetHighContrastMode(newHCMMode.Get());
				}
			}

			// keep array densely packed
			for (int j = i; j < m_numHcmModeTargets - 1; ++j)
			{
				m_aHcmModeTargetObj[j] = m_aHcmModeTargetObj[j + 1];
				m_aHcmModeTargetObjId[j] = m_aHcmModeTargetObjId[j + 1];
			}
			return;
		}
	}
}

void NdInteractControl::RemoveAllHighContrastModeTargets(Maybe<DC::HCMModeType> newHCMMode)
{
	for (int i = 0; i < m_numHcmModeTargets; ++i)
	{
		if (newHCMMode.Valid())
		{
			NdGameObject* pTarget = m_aHcmModeTargetObj[i].ToMutableProcess();

			if (pTarget)
			{
				pTarget->SetHighContrastMode(newHCMMode.Get());
			}
		}
	}

	m_numHcmModeTargets = 0;
}

//static
const DC::InteractableDef* NdInteractControl::LookupDef(StringId64 defId)
{
	return ScriptManager::LookupInModule<DC::InteractableDef>(defId, SID("interactable"), nullptr);
}

const DC::GameInteractableDef* NdInteractControl::LookupGameDef(StringId64 defId)
{
	return ScriptManager::LookupInModule<DC::GameInteractableDef>(defId, SID("interactable"), nullptr);
}

///------------------------------------------------------------------------------------///
StringId64 NdInteractControl::GetDefId() const
{
	// use src object interactable-id
	if (m_hSrcDefObj.HandleValid())
	{
		const NdGameObject* pSrcObj = m_hSrcDefObj.ToProcess();
		const NdInteractControl* pSrcInteractCtrl = pSrcObj ? pSrcObj->GetInteractControl() : nullptr;
		if (pSrcInteractCtrl != nullptr)
		{
			return pSrcInteractCtrl->GetDefId();
		}
	}

	return m_defId;
}

///------------------------------------------------------------------------------------///
const DC::InteractableDef* NdInteractControl::GetDef() const
{
	// use src object interactable-id
	if (m_hSrcDefObj.HandleValid())
	{
		const NdGameObject* pSrcObj = m_hSrcDefObj.ToProcess();
		const NdInteractControl* pSrcInteractCtrl = pSrcObj ? pSrcObj->GetInteractControl() : nullptr;
		if (pSrcInteractCtrl != nullptr)
		{
			return pSrcInteractCtrl->GetDef();
		}
	}

	if (m_pCustomDef != nullptr)
		return m_pCustomDef;

	if (m_pInteractableDef != nullptr)
		return m_pInteractableDef;

	if (m_defId != INVALID_STRING_ID_64)
	{
		const DC::InteractableDef* pDef = ScriptManager::LookupInModule<const DC::InteractableDef>(m_defId,
																								   SID("interactable"),
																								   nullptr);
		return pDef;
	}

	return nullptr;
}

///------------------------------------------------------------------------------------///
StringId64 NdInteractControl::GetActivationRegionNameId() const
{
	if (m_hSrcDefObj.HandleValid())
	{
		const NdGameObject* pSrcObj = m_hSrcDefObj.ToProcess();
		const NdInteractControl* pSrcInteractCtrl = pSrcObj ? pSrcObj->GetInteractControl() : nullptr;
		if (pSrcInteractCtrl != nullptr)
		{
			return pSrcInteractCtrl->GetActivationRegionNameId();
		}
	}

	return m_activationRegionNameId;
}

///------------------------------------------------------------------------------------///
float NdInteractControl::GetMinPathDistanceToInteractable(const PathfindRequestHandle& hRequest) const
{
	if (m_numInteractNavLocations == 0)
		return NDI_FLT_MAX;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	AtomicLockJanitorRead interactNavLocationsLockJan(&m_interactNavLocationsLock, FILE_LINE_FUNC);

	float bestDist = NDI_FLT_MAX;
	for (int i = 0; i < m_numInteractNavLocations; ++i)
	{
		const float dist = PathfindManager::Get().GetFastApproxSmoothPathDistanceOnly(hRequest, m_interactNavLocations[i]);
		if (dist < bestDist)
			bestDist = dist;
	}

	return bestDist;
}

///------------------------------------------------------------------------------------///
float NdInteractControl::GetMinLinearDistanceToInteractable(Point posWs, float yScale, InteractAgilityMask* const pAgilityMask /* = nullptr*/) const
{
	if (pAgilityMask)
		*pAgilityMask = InteractAgilityMask::kNone;

	if (m_numInteractNavLocations == 0)
		return NDI_FLT_MAX;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	AtomicLockJanitorRead interactNavLocationsLockJan(&m_interactNavLocationsLock, FILE_LINE_FUNC);

	float bestDistSqr = NDI_FLT_MAX;
	InteractAgilityMask bestMask = InteractAgilityMask::kNone;
	for (int i = 0; i < m_numInteractNavLocations; ++i)
	{
		Vector delta = m_interactNavLocations[i].GetPosWs() - posWs;
		delta.SetY(delta.Y() * yScale);
		const float distSqr = LengthSqr(delta);
		if (distSqr < bestDistSqr)
		{
			bestDistSqr = distSqr;
			bestMask = m_interactAgilities[i];
		}
	}

	if (pAgilityMask)
		*pAgilityMask = bestMask;

	return bestDistSqr == NDI_FLT_MAX ? NDI_FLT_MAX : Sqrt(bestDistSqr);
}

///------------------------------------------------------------------------------------///
NavLocation NdInteractControl::GetClosestNavLocationToInteractableByPathDistance(const PathfindRequestHandle& hRequest, float* const pPathDist /* = nullptr*/) const
{
	if (m_numInteractNavLocations == 0)
	{
		if (pPathDist)
			*pPathDist = NDI_FLT_MAX;
		return NavLocation();
	}

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	AtomicLockJanitorRead interactNavLocationsLockJan(&m_interactNavLocationsLock, FILE_LINE_FUNC);

	float bestDist = NDI_FLT_MAX;
	int iBest = -1;
	for (int i = 0; i < m_numInteractNavLocations; ++i)
	{
		const float dist = PathfindManager::Get().GetFastApproxSmoothPathDistanceOnly(hRequest, m_interactNavLocations[i]);
		if (dist < bestDist)
		{
			bestDist = dist;
			iBest = i;
		}
	}

	if (pPathDist)
		*pPathDist = bestDist;

	return iBest == -1 ? NavLocation() : m_interactNavLocations[iBest];
}

///------------------------------------------------------------------------------------///
int NdInteractControl::GetInteractNavLocations(NavLocation* pOutNavLocs, int maxNavLocs) const
{
	if (m_numInteractNavLocations == 0)
		return 0;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	AtomicLockJanitorRead interactNavLocationsLockJan(&m_interactNavLocationsLock, FILE_LINE_FUNC);

	const int count = Min(m_numInteractNavLocations, maxNavLocs);

	for (int i = 0; i < count; ++i)
	{
		pOutNavLocs[i] = m_interactNavLocations[i];
	}

	return count;
}

///------------------------------------------------------------------------------------///
int NdInteractControl::GetInteractNavLocationAgilities(InteractAgilityMask* pOutAgilities, int maxAgilities) const
{
	if (m_numInteractNavLocations == 0)
		return 0;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	AtomicLockJanitorRead interactNavLocationsLockJan(&m_interactNavLocationsLock, FILE_LINE_FUNC);

	const int count = Min(m_numInteractNavLocations, maxAgilities);

	for (int i = 0; i < count; ++i)
	{
		pOutAgilities[i] = m_interactAgilities[i];
	}

	return count;
}

///------------------------------------------------------------------------------------///
NavLocation NdInteractControl::GetClosestNavLocationToInteractableByLinearDistance(Point posWs, float yScale, float* const pLinearDist /* = nullptr*/) const
{
	if (m_numInteractNavLocations == 0)
	{
		if (pLinearDist)
			*pLinearDist = NDI_FLT_MAX;
		return NavLocation();
	}

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	AtomicLockJanitorRead interactNavLocationsLockJan(&m_interactNavLocationsLock, FILE_LINE_FUNC);

	float bestDistSqr = NDI_FLT_MAX;
	int iBest = -1;
	for (int i = 0; i < m_numInteractNavLocations; ++i)
	{
		Vector delta = posWs - m_interactNavLocations[i].GetPosWs();
		delta.SetY(delta.Y() * yScale);
		const float distSqr = LengthSqr(delta);

		if (distSqr < bestDistSqr)
		{
			bestDistSqr = distSqr;
			iBest = i;
		}
	}

	if (pLinearDist)
		*pLinearDist = bestDistSqr == NDI_FLT_MAX ? NDI_FLT_MAX : Sqrt(bestDistSqr);

	return iBest == -1 ? NavLocation() : m_interactNavLocations[iBest];
}

///------------------------------------------------------------------------------------///
StringId64 NdInteractControl::GetInteractionRegionNameId() const
{
	if (m_hSrcDefObj.HandleValid())
	{
		const NdGameObject* pSrcObj = m_hSrcDefObj.ToProcess();
		const NdInteractControl* pSrcInteractCtrl = pSrcObj ? pSrcObj->GetInteractControl() : nullptr;
		if (pSrcInteractCtrl != nullptr)
		{
			return pSrcInteractCtrl->GetInteractionRegionNameId();
		}
	}

	return m_interactionRegionNameId;
}

///------------------------------------------------------------------------------------///
void NdInteractControl::CacheDefPtr()
{
	m_pInteractableDef = ScriptPointer<DC::InteractableDef>(m_defId, SID("interactable"));
}

///------------------------------------------------------------------------------------///
void NdInteractControl::OverrideInteractableDef(StringId64 interactDefId)
{
	ASSERT(interactDefId != INVALID_STRING_ID_64);
	if (ScriptManager::LookupInModule<DC::InteractableDef>(interactDefId, SID("interactable"), nullptr) == nullptr)
	{
		MsgConScriptError("override-interact-def %s not found\n", DevKitOnly_StringIdToString(interactDefId));
		return;
	}

	// fix cached value.
	m_pCustomDef = nullptr;
	m_defId = interactDefId;
	CacheDefPtr();
}

bool NdInteractControl::IsPreSelected() const
{
	I64 frameIndex = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	I64 framesSinceSelected = frameIndex - m_preSelectedFrameNumber;
	if (framesSinceSelected <= 1)
		return IsInteractionEnabled();
	else
		return false;
}

bool NdInteractControl::IsSelected() const
{
	I64 frameIndex = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	I64 framesSinceSelected = frameIndex - m_selectedFrameNumber;
	if (framesSinceSelected <= 1)
		return IsInteractionEnabled();
	else
		return false;
}

bool NdInteractControl::IsTriggerOnly() const
{
	return m_triggerOnly;
}

void NdInteractControl::NotifyInteracted(NdGameObjectHandle hUser)
{
	if (hUser.HandleValid() && hUser.IsType(SID("Player")))
	{
		m_iconPermanent = false;
	}

	Clock* pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);
	m_lastInteractedTime = pClock->GetCurTime();

	UpdateHighContrastModeType();

	//if (pSelf)
	//{
	//	StringId64 objectId = INVALID_STRING_ID_64;
	//	if (IsPickup())
	//	{
	//		objectId = GetHudIcon(pSelf, pUser);
	//	}
	//	if (objectId == INVALID_STRING_ID_64)
	//	{
	//		objectId = m_interactableDefId;
	//	}
	//}
	// JQG: not sure what objectId used to be used for... but this func is overridden by the game anyway
}

BoundFrame NdInteractControl::GetIconBoundFrame(NdGameObjectHandle hUser, const Point userWs) const
{
	bool isDeadBody = false;

	const DC::InteractableDef* pDef = GetDef();
	const Vector iconOffsetWs(0.f, pDef->m_hudIconOffsetYWs, 0.f);

	const NdGameObject* pSelf = m_hSelf.ToProcess();
	if (pSelf->IsKindOf(g_type_NdProcessRagdoll))
	{
		isDeadBody = true;
	}
	else if (const Character* const pChar = Character::FromProcess(pSelf))
	{
		if (pChar->IsDead())
		{
			isDeadBody = true;
		}
	}

	if (isDeadBody)
	{
		// ragdolls leave their aligns in funny places, so use the bounding sphere center instead
		const NdGameObject* pRagdoll = pSelf;
		const Sphere bounds = pRagdoll->GetDrawControl()->GetBoundingSphere();
		return BoundFrame(bounds.GetCenter() + iconOffsetWs);
	}
	//else if (hUser.HandleValid() && hUser.IsType(SID("Player")))
	//{
	//	if (pDef->m_ledgeLine)
	//	{
	//		if (pSelf->IsKindOf(g_type_NdGameObject))
	//		{
	//			U32 playerIndex = EngineComponents::GetGameInfo()->GetPlayerIndex(hUser);
	//			const RenderCamera& renderCamera = g_mainCameraInfo[playerIndex].GetRenderCamera();

	//			FgAnimData *animData = pSelf->GetAnimData();

	//			Point aabbCenter((animData->m_pBoundingInfo->m_aabb.m_min.GetVec4() + animData->m_pBoundingInfo->m_aabb.m_max.GetVec4()) * 0.5f);
	//			Point leftEnd(aabbCenter.X(), aabbCenter.Y(), aabbCenter.Z());
	//			Point rightEnd(aabbCenter.X(), aabbCenter.Y(), aabbCenter.Z());

	//			leftEnd.Set(pDef->m_ledgeLineAxis, animData->m_pBoundingInfo->m_aabb.m_min.Get(pDef->m_ledgeLineAxis));
	//			rightEnd.Set(pDef->m_ledgeLineAxis, animData->m_pBoundingInfo->m_aabb.m_max.Get(pDef->m_ledgeLineAxis));

	//			Point aabbCenterWs = aabbCenter * animData->m_objXform;
	//			Point leftEndWs = leftEnd * animData->m_objXform;
	//			Point rightEndWs = rightEnd * animData->m_objXform;

	//			F32 leftEndSsX, leftEndSsY;
	//			renderCamera.WorldToScreen(leftEndSsX, leftEndSsY, leftEndWs);

	//			F32 rightEndSsX, rightEndSsY;
	//			renderCamera.WorldToScreen(rightEndSsX, rightEndSsY, rightEndWs);

	//			F32 lineNormalX = rightEndSsY - leftEndSsY;
	//			F32 lineNormalY = -(rightEndSsX - leftEndSsX);
	//			F32 lineNormalLength = sqrtf(lineNormalX * lineNormalX + lineNormalY * lineNormalY) + 0.0001f;
	//			F32 lineScreenLength = lineNormalLength;
	//			lineNormalX /= lineNormalLength;
	//			lineNormalY /= lineNormalLength;

	//			F32 lineC = -(lineNormalX * leftEndSsX + lineNormalY * leftEndSsY);

	//			const F32 kScreenCenterX = (static_cast<float>(g_display.m_screenWidth) / 2.0f);
	//			const F32 kScreenCenterY = (static_cast<float>(g_display.m_screenHeight) / 2.0f);
	//			F32 distToScreenCenter = lineNormalX * kScreenCenterX + lineNormalY * kScreenCenterY + lineC;

	//			F32 closestPointX = kScreenCenterX - distToScreenCenter * lineNormalX;
	//			F32 closestPointY = kScreenCenterY - distToScreenCenter * lineNormalY;

	//			F32 lerpAlpha = (leftEndSsX != rightEndSsX) ? (closestPointX - leftEndSsX) / (rightEndSsX - leftEndSsX) :
	//				(leftEndSsY != rightEndSsY) ? (closestPointY - leftEndSsY) / (rightEndSsY - leftEndSsY) : 0.0f;

	//			lerpAlpha = Min(1.0f, Max(0.0f, lerpAlpha));

	//			Point iconPositionWs = Lerp(leftEndWs, rightEndWs, lerpAlpha);

	//			F32 lineLengthWs = Length(rightEndWs - leftEndWs);

	//			F32 leftLineLength = lineLengthWs * lerpAlpha;
	//			F32 rightLineLength = lineLengthWs * (1.0f - lerpAlpha);

	//			leftLineLength = Min(leftLineLength, pDef->m_ledgeLineMaxLength);
	//			rightLineLength = Min(rightLineLength, pDef->m_ledgeLineMaxLength);

	//			leftLine.Set(pDef->m_ledgeLineAxis, -leftLineLength);
	//			rightLine.Set(pDef->m_ledgeLineAxis, rightLineLength);

	//			Locator loc = pSelf->GetLocator();
	//			loc.SetTranslation(iconPositionWs + iconOffsetWs);

	//			return BoundFrame(loc);
	//		}
	//	}
	//}

	return BoundFrame(Locator(GetSelectionTargetWs(hUser, userWs) + iconOffsetWs));
}

//----------------------------------------------------------------------------------------------------------//
void NdInteractControl::DrawInteractHudIcon(MutableNdGameObjectHandle hUser, const NdInteractControl::DrawInteractParams& params)
{
	bool isBestSelection = g_suppressInteractable ? false : params.m_isBestSelection;

	const NdGameObject* pSelf = m_hSelf.ToProcess();
	const DC::InteractableDef* pDef = GetDef();
	if (!pDef || pDef->m_noDrawIcon)
		return;

	// show pre-selection for anything that isn't a pickup
	BoundFrame pickupBoundFrame(kOrigin);
	if (pSelf)
	{
		pickupBoundFrame = GetIconBoundFrame(hUser, kInvalidPoint);
	}

	if (IsInteractionEnabled()) // can't be selected if not enabled
	{
		if (params.m_avail == kInteractableUnavailable && m_unavailableSeenTime == TimeFrameZero())
			m_unavailableSeenTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();

		Hud2::InteractableRequest request;
		request.m_params = params;

		// -------------------------------------------------------------------------------------------------------------------
		// NOTE: assigning pickupBoundFrame, which is calculated by GetIconBoundFrame(...) above. If you look at
		// the various GetIconBoundFrame(...) in the codebase, they all have their own rules of applying offsets.
		// This means we end up applying ws offset twice for most interactables, what I am trying here is dont
		// hack in the offset if the bound frame is calculated by GetIconBoundFrame(...).
		//
		// request.m_offsetWs = Vector(0.0f, pDef->m_hudIconOffsetYWs, 0.0f);	// ... just hack in the offset like this
		// -------------------------------------------------------------------------------------------------------------------
		request.m_boundFrame = pickupBoundFrame;

		PopulateHud2Request(request, hUser);

		// should we move this check to PopulateHud2Request?
		//if (request.m_params.m_interactCmdId != INVALID_STRING_ID_64 && pPlayer->GetJoypad().IsHoldButtonCommand(request.m_params.m_interactCmdId).Valid())
		//{
		//	request.m_iconButton = "gen:interact";
		//	request.m_showText3 = true; // text3 is HOLD
		//}

		if (g_suppressInteractable)
			request.m_params.m_isBestSelection = false;

		Hud2::Root::Get()->ShowInteractIcon(request);

		if (params.m_isPreSelectedOnly)
			m_preSelectedFrameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
		else
			m_selectedFrameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	}
}

// no longer called, design would like an icon instead of highlighting, leaving it in for now
//void NdInteractControl::HighlightInternal(NdGameObject* pSelf, bool isSelected, bool isInteractable)
//{
//	TimeFrame currTime				= GetProcessClock()->GetCurTime();
//	float timeDelta					= ( currTime - m_lastHighlightUpdateTime ).ToSeconds();
//	m_lastHighlightUpdateTime		= currTime;
//
//	if (pSelf)
//	{
//		const DC::InteractableDef* pDef = GetDef();
//
//		const DC::HighlightSettings* selectHighlightSettings	= ScriptManager::LookupInModule<DC::HighlightSettings>(pDef->m_selectHighlightSettings, SID("highlight"));
//		const DC::HighlightSettings* interactHighlightSettings	= ScriptManager::LookupInModule<DC::HighlightSettings>(pDef->m_interactHighlightSettings, SID("highlight"));
//
//		U32 selectPulsePeriod			= (U32)( kTimeFramesPerSecond / selectHighlightSettings->m_pulseFrequency );
//		U32 selectPulsePhase			= currTime.GetRaw() % selectPulsePeriod;
//		float selectHighlightPulse		= Sin( (float)selectPulsePhase/(float)selectPulsePeriod * 2.0f * PI );
//
//		U32 interactPulsePeriod			= (U32)( kTimeFramesPerSecond / interactHighlightSettings->m_pulseFrequency );
//		U32 interactPulsePhase			= currTime.GetRaw() % interactPulsePeriod;
//		float interactHighlightPulse	= Sin( (float)interactPulsePhase/(float)interactPulsePeriod * 2.0f * PI );
//
//		ALWAYS_ASSERT( selectHighlightSettings );
//		ALWAYS_ASSERT( interactHighlightSettings );
//
//		float interactPulseTime		= pDef->m_interactionPulseTime;
//		float timeSinceInteract		= ( currTime > m_lastInteractedTime ) ? (currTime - m_lastInteractedTime ).ToSeconds() : FLT_MAX;
//		float interactionPulse		= 1.0f;
//		if ( timeSinceInteract < interactPulseTime )
//		{
//			isSelected			= true;			// act as if all was true, so the highlight doesn't fade out while we're pulsing
//			isInteractable		= true;
//			interactionPulse	= 1.0f + pDef->m_interactionPulseIntensity * Sin( timeSinceInteract / interactPulseTime * PI);
//		}
//
//		float selectionHighlightChangeSpeed = 1.0f / ( selectHighlightSettings->m_fadeTime + 0.01f );
//		if ( !isSelected )
//		{
//			selectionHighlightChangeSpeed *= -1.0f;
//		}
//		m_selectHighlightBrightness = Max( 0.0f, Min( 1.0f, m_selectHighlightBrightness + selectionHighlightChangeSpeed * timeDelta ) );
//
//
//		float interactHighlightChangeSpeed = 1.0f / ( interactHighlightSettings->m_fadeTime + 0.01f );
//		if ( !isInteractable )
//		{
//			interactHighlightChangeSpeed *= -1.0f;
//		}
//		m_interactHighlightBrightness = Max( 0.0f, Min( 1.0f, m_interactHighlightBrightness + interactHighlightChangeSpeed * timeDelta ) );
//
//
//		Color color = GetHighlightColor(pSelf);
//
//		Color selectColor( ( selectHighlightSettings->m_colorR == -1.0f ) ? color.R() : selectHighlightSettings->m_colorR,
//						   ( selectHighlightSettings->m_colorG == -1.0f ) ? color.G() : selectHighlightSettings->m_colorG,
//						   ( selectHighlightSettings->m_colorB == -1.0f ) ? color.B() : selectHighlightSettings->m_colorB,
//						   ( selectHighlightSettings->m_colorA == -1.0f ) ? color.A() : selectHighlightSettings->m_colorA * color.A() );
//
//		Color interactColor( ( interactHighlightSettings->m_colorR == -1.0f ) ? color.R() : interactHighlightSettings->m_colorR,
//							 ( interactHighlightSettings->m_colorG == -1.0f ) ? color.G() : interactHighlightSettings->m_colorG,
//							 ( interactHighlightSettings->m_colorB == -1.0f ) ? color.B() : interactHighlightSettings->m_colorB,
//							 ( interactHighlightSettings->m_colorA == -1.0f ) ? color.A() : interactHighlightSettings->m_colorA * color.A() );
//
//
//		const bool isPickup = NdInteractControl::IsPickup(GetStyle());
//
//		if ( isPickup )
//		{
//			pSelf->SetShimmerIntensityMultiplier( 1.0 - m_interactHighlightBrightness );
//
//			if ( m_interactHighlightBrightness > 0.0f )
//			{
//				Color finalColor(interactColor.R(), interactColor.G(), interactColor.B(), m_interactHighlightBrightness * interactionPulse * interactColor.A());
//
//				Vec4 highlightParamsHiFreq( interactHighlightSettings->m_hiFreqOffset,
//											interactHighlightSettings->m_hiFreqScale,
//											interactHighlightSettings->m_hiFreqPower,
//											interactHighlightSettings->m_hiFreqIntensity + interactHighlightPulse * interactHighlightSettings->m_hiFreqPulseAmplitude );
//
//				Vec4 highlightParamsLoFreq( interactHighlightSettings->m_loFreqOffset,
//											interactHighlightSettings->m_loFreqScale,
//											interactHighlightSettings->m_loFreqPower,
//											interactHighlightSettings->m_loFreqIntensity + interactHighlightPulse * interactHighlightSettings->m_loFreqPulseAmplitude);
//
//				pSelf->Highlight(finalColor, highlightParamsHiFreq, highlightParamsLoFreq, kHighlightStyleGlowObject);
//			}
//		}
//		else
//		{
//			F32 finalBrightness = Max( m_selectHighlightBrightness, m_interactHighlightBrightness );
//
//			pSelf->SetShimmerIntensityMultiplier( 1.0 - m_interactHighlightBrightness );
//
//			Color highlightColor = Lerp( selectColor, interactColor, m_interactHighlightBrightness );
//			Color finalColor(highlightColor.R(), highlightColor.G(), highlightColor.B(), finalBrightness * interactionPulse * highlightColor.A());
//
//			if ( finalBrightness > 0.0f )
//			{
//				Vec4 selectHighlightParamsHiFreq( selectHighlightSettings->m_hiFreqOffset,
//												  selectHighlightSettings->m_hiFreqScale,
//												  selectHighlightSettings->m_hiFreqPower,
//												  selectHighlightSettings->m_hiFreqIntensity + selectHighlightPulse * selectHighlightSettings->m_hiFreqPulseAmplitude);
//
//				Vec4 selectHighlightParamsLoFreq( selectHighlightSettings->m_loFreqOffset,
//												  selectHighlightSettings->m_loFreqScale,
//												  selectHighlightSettings->m_loFreqPower,
//												  selectHighlightSettings->m_loFreqIntensity + selectHighlightPulse * selectHighlightSettings->m_loFreqPulseAmplitude);
//
//				Vec4 interactHighlightParamsHiFreq( interactHighlightSettings->m_hiFreqOffset,
//													interactHighlightSettings->m_hiFreqScale,
//													interactHighlightSettings->m_hiFreqPower,
//													interactHighlightSettings->m_hiFreqIntensity + interactHighlightPulse * interactHighlightSettings->m_hiFreqPulseAmplitude);
//
//				Vec4 interactHighlightParamsLoFreq( interactHighlightSettings->m_loFreqOffset,
//													interactHighlightSettings->m_loFreqScale,
//													interactHighlightSettings->m_loFreqPower,
//													interactHighlightSettings->m_loFreqIntensity + interactHighlightPulse * interactHighlightSettings->m_loFreqPulseAmplitude);
//
//				Vec4 highlightParamsHiFreq = Lerp( selectHighlightParamsHiFreq, interactHighlightParamsHiFreq, m_interactHighlightBrightness );
//				Vec4 highlightParamsLoFreq = Lerp( selectHighlightParamsLoFreq, interactHighlightParamsLoFreq, m_interactHighlightBrightness );
//
//				pSelf->Highlight(finalColor, highlightParamsHiFreq, highlightParamsLoFreq, kHighlightStyleGlowObject);
//			}
//		}
//	}
//}

//float NdInteractControl::GetCameraAngleCost(NdGameObject* pSelf, U32F screenIndex, Point_arg camPosWs, Vector_arg camDirWs)
//{
//	const DC::InteractableDef* pDef = GetDef();
//	if (pSelf && pDef && IsInteractionEnabled())
//	{
//		const RenderCamera& renderCamera = GetRenderCamera(screenIndex);
//
//		const Sphere bounds = pSelf->GetBoundingSphere();
//
//		const Point targetPosWs = GetSelectionTargetWs();
//		Vector camToTargetWs = SafeNormalize(targetPosWs - camPosWs, -camDirWs);
//
//#if !FINAL_BUILD
//		if (g_interactableOptions.m_drawCost)
//		{
//			g_prim.Draw(DebugLine(camPosWs, targetPosWs, kColorBlue, 1.0f, PrimAttrib(0)), kPrimDuration1FrameAuto);
//			g_prim.Draw(DebugLine(camPosWs, camDirWs, kColorOrange, 1.0f, PrimAttrib(0)), kPrimDuration1FrameAuto);
//		}
//#endif
//
//		const float cosine = Dot(camToTargetWs, camDirWs);
//		float angle = RADIANS_TO_DEGREES(Acos(cosine));
//
//		if (pDef->m_allowOffscreen || renderCamera.IsSphereInFrustum(bounds)) // max angle is just the angle necessary to keep the object on-screen
//		{
//			// now apply hysteresis
//			if (IsSelected())
//			{
//				angle *= pDef->m_selectStickinessPct;
//			}
//			return angle;
//		}
//	}
//	return SCALAR_LC(kLargestFloat);
//}

InteractCost NdInteractControl::GetBaseCost(NdGameObjectHandle hUser) const
{
	return GetBaseCostInternal(hUser);
}

InteractCost NdInteractControl::GetBaseCostInternal(NdGameObjectHandle hUser) const
{
	const DC::InteractableDef* pDef = GetDef();

	if (hUser.HandleValid() && hUser.IsType(SID("Player")) && pDef && IsInteractionEnabled())
	{
		const U32 playerIndex = EngineComponents::GetNdGameInfo()->GetPlayerIndex(hUser);
		if (playerIndex < kMaxPlayers)
		{
			Point camPos = GameGlobalCameraLocator(playerIndex).GetTranslation();
			Vector camDir = GameGlobalCameraDirection(playerIndex);

			const NdGameObject* pUser = hUser.ToProcess();

			InteractCost result;
			result.m_rawCost = pDef->m_baseCost;
			const Point targetPosWs = GetSelectionTargetWs(hUser, kInvalidPoint, true);
			Vector camToTargetWs = SafeNormalize(targetPosWs - camPos, -camDir);
			F32 playerToTargetDist = Length(targetPosWs - pUser->GetTranslation());
			F32 targetAngle = Acos(Dot(camToTargetWs, camDir));

			result.m_rawCost += targetAngle * 10.f;
			result.m_weight = pDef->m_baseCostWeight;

			return result;
		}
	}

	return InteractCost();
}

Point NdInteractControl::GetSelectionTargetWs(NdGameObjectHandle hUser, const Point userWs /*= kInvalidPoint*/, bool debugDraw /* = false*/) const
{
	Point targetPosWs(kOrigin);

	const NdGameObject* pSelf = m_hSelf.ToProcess();
	const NdGameObject* pUser = hUser.ToProcess();
	const bool userPtrValid = pUser;
	const bool userPointValid = !AllComponentsEqual(userWs, kInvalidPoint);
	const bool userValid = userPtrValid || userPointValid;

	const DC::InteractableDef* pDef = GetDef();
	if (pSelf && pDef)
	{
		const Vector offsetLs(Scalar(pDef->m_selectOffsetX), Scalar(pDef->m_selectOffsetY), Scalar(pDef->m_selectOffsetZ));
		const Vector offsetWs(Scalar(pDef->m_selectOffsetWsX), Scalar(pDef->m_selectOffsetWsY), Scalar(pDef->m_selectOffsetWsZ));

		// We always want dead bodies to use the center of their bounding sphere, regardless of what the
		// InteractableDef has to say about it.
		bool ignore_Def_selectUsingAlign = false;

		if (const Character* const pChar = Character::FromProcess(pSelf))
		{
			if (pChar->IsDead())
			{
				ignore_Def_selectUsingAlign = true;
			}
		}
		else if (pSelf->IsKindOf(g_type_NdProcessRagdoll))
		{
			ignore_Def_selectUsingAlign = true;
		}

		// handle the case if this interactable will change grab joint based on user location and its own state.
		if (m_selectionTargetSpawner != INVALID_STRING_ID_64)
		{
			const EntitySpawner* pSpawner = nullptr;
			if (const EntitySpawner *pSelfSpawner = pSelf->GetSpawner())
			{
				if (const Level *pSelfLevel = pSelfSpawner->GetLevel())
				{
					pSpawner = pSelfLevel->LookupEntitySpawnerByBareNameId(m_selectionTargetSpawner, pSelf->GetUserNamespaceId());
					if (!pSpawner)
					{
						pSpawner = pSelfLevel->LookupEntitySpawnerByNameId(m_selectionTargetSpawner);
					}
				}
			}

			if (!pSpawner)
			{
				pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(m_selectionTargetSpawner, pSelf->GetUserNamespaceId());
			}
			if (!pSpawner)
			{
				pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByNameId(m_selectionTargetSpawner);
			}
			if (pSpawner)
			{
				targetPosWs = pSpawner->GetWorldSpaceLocator().TransformPoint(Point(kZero) + offsetLs) + offsetWs;

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					g_prim.Draw(DebugLine(pSelf->GetTranslation(), targetPosWs, kColorCyanTrans));
					g_prim.Draw(DebugSphere(targetPosWs, 0.07f, kColorCyan));
					g_prim.Draw(DebugString(targetPosWs, StringBuilder<128>("SelSpawner").c_str(), kColorWhite, 0.6f));
				}
			}
			else
			{
				MsgConScriptError("Target spawner %s not found for interactable %s\n", DevKitOnly_StringIdToString(m_selectionTargetSpawner), DevKitOnly_StringIdToString(pSelf->GetUserId()));
				targetPosWs = pSelf->GetLocator().TransformPoint(Point(kZero) + offsetLs);
			}
		}
		else if (pSelf && userValid && m_customSelectionLocFunc != nullptr)
		{
			const Locator jointLocWs = m_customSelectionLocFunc(pSelf, pUser, userWs);
			const Vector finalOffsetWs = jointLocWs.TransformVector(offsetLs) + offsetWs;
			targetPosWs = jointLocWs.GetTranslation() + finalOffsetWs;

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				g_prim.Draw(DebugSphere(targetPosWs, 0.07f, kColorCyan));
				g_prim.Draw(DebugString(targetPosWs, StringBuilder<128>("SelDynJoint").c_str(), kColorWhite, 0.6f));
			}
		}
		else if (pSelf && m_selectUsingJointIndex >= 0)
		{
			const Locator jointLocWs = pSelf->GetAnimControl()->GetJointCache()->GetJointLocatorWs(m_selectUsingJointIndex);
			const Vector finalOffsetWs = jointLocWs.TransformVector(offsetLs) + offsetWs;
			targetPosWs = jointLocWs.GetTranslation() + finalOffsetWs;

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				g_prim.Draw(DebugSphere(targetPosWs, 0.07f, kColorCyan));
				g_prim.Draw(DebugString(targetPosWs, StringBuilder<128>("SelJnt(%s)", DevKitOnly_StringIdToString(pDef->m_selectUsingJoint)).c_str(), kColorWhite, 0.6f));
			}
		}
		else if (!ignore_Def_selectUsingAlign && (!pSelf || pDef->m_selectUsingAlign))
		{
			const Locator locWs = pSelf->GetLocator();
			const Vector finalOffsetWs = locWs.TransformVector(offsetLs) + offsetWs;
			targetPosWs = locWs.GetTranslation() + finalOffsetWs;

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				g_prim.Draw(DebugSphere(targetPosWs, 0.07f, kColorCyan));
				g_prim.Draw(DebugString(targetPosWs, StringBuilder<128>("SelAlign").c_str(), kColorWhite, 0.6f));
			}
		}
		else
		{
			const Locator locWs = pSelf->GetLocator();
			const Vector finalOffsetWs = locWs.TransformVector(offsetLs) + offsetWs;

			if (pSelf->GetDrawControl())
				targetPosWs = pSelf->GetDrawControl()->GetBoundingSphere().GetCenter() + finalOffsetWs;
			else
				targetPosWs = locWs.GetTranslation() + finalOffsetWs;

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				g_prim.Draw(DebugSphere(targetPosWs, 0.07f, kColorCyan));
				g_prim.Draw(DebugString(targetPosWs, StringBuilder<128>("SelBoundingSphere").c_str(), kColorWhite, 0.6f));
			}
		}
	}

	if (0) { g_prim.Draw(DebugSphere(targetPosWs, 1.0f, kColorRed)); }

	return targetPosWs;
}

//-----------------------------------------------------------------------------------------------------
float NdInteractControl::GetProbeOffsetYWs() const
{
	return m_probeOffsetYWs;
}

//-----------------------------------------------------------------------------------------------------
float NdInteractControl::GetProbeRadius() const
{
	const DC::InteractableDef* pDef = GetDef();
	return pDef ? pDef->m_probeRadius : 0.f;
}

//-----------------------------------------------------------------------------------------------------
I32 NdInteractControl::GetNumAroundCorner() const
{
	const DC::InteractableDef* pDef = GetDef();
	return pDef ? pDef->m_numAroundCornerProbe : 0.f;
}

//-----------------------------------------------------------------------------------------------------
float NdInteractControl::GetAroundCornerStep() const
{
	const DC::InteractableDef* pDef = GetDef();
	return pDef ? pDef->m_aroundCornerStep : 0.f;
}

//-----------------------------------------------------------------------------------------------------
bool NdInteractControl::NoSecondaryDisplay() const
{
	const DC::InteractableDef* pDef = GetDef();
	return pDef ? pDef->m_noSecondaryDisplay : 0.f;
}

//-----------------------------------------------------------------------------------------------------
//float NdInteractControl::GetHudIconTargetScale(NdGameObjectHandle hUser)
//{
//	const DC::InteractableDef* pDef = GetDef();
//
//	if (pDef->m_activationAngleDegrees <= 0.f || pDef->m_interactionAngleDegreesHori <= 0.f)
//		return 1.f;
//
//	if (pDef->m_interactionAngleDegreesHori >= pDef->m_activationAngleDegrees)
//		return 1.f;
//
//	if (hUser.HandleValid() && hUser.IsType(SID("Player")) && pDef && IsInteractionEnabled())
//	{
//		const U32 playerIndex = EngineComponents::GetNdGameInfo()->GetPlayerIndex(hUser);
//
//		if (playerIndex <= 1)
//		{
//			Point camPos = GameGlobalCameraLocator(playerIndex).GetTranslation();
//			Vector camDir = GameGlobalCameraDirection(playerIndex);
//
//			const Point targetPosWs = GetSelectionTargetWs(hUser);
//			Vector camToTargetWs = SafeNormalize(targetPosWs - camPos, -camDir);
//			F32 targetAngleRad = Acos(Dot(camToTargetWs, camDir));
//			F32 targetAngleDeg = RADIANS_TO_DEGREES(targetAngleRad);
//
//			if (targetAngleDeg >= pDef->m_interactionAngleDegreesHori && targetAngleDeg <= pDef->m_activationAngleDegrees)
//			{
//				float factor = (targetAngleDeg - pDef->m_interactionAngleDegreesHori) / (pDef->m_activationAngleDegrees - pDef->m_interactionAngleDegreesHori);
//				factor = MinMax01(factor);
//				float targetScale = Lerp(1.f, 1.2f, factor);
//				return targetScale;
//			}
//		}
//	}
//
//	return 1.f;
//}

//bool NdInteractControl::NeedProbeStartYOffsetWhenLookUp() const
//{
//	const DC::InteractableDef* pDef = GetDef();
//	if (pDef != nullptr)
//		return pDef->m_needProbeStartOffsetYWhenLookUp;
//	else
//		return false;
//}

//float NdInteractControl::GetProbeStartYOffsetWhenLookUp() const
//{
//	const DC::InteractableDef* pDef = GetDef();
//	if (pDef != nullptr)
//		return pDef->m_probeStartOffsetYWhenLookUp;
//	else
//		return 0.f;
//}

void NdInteractControl::SetInteractionEnabled(bool enable, bool interactablesMgrIgnoreDisableHack /* = false*/)
{
	bool wasDisabled = m_disabled;
	m_disabled = !enable;

	if (wasDisabled != m_disabled)
		UpdateHighContrastModeType();

	// TODO(eomernick): Alter Carry rope to not require constant disabling so this isn't so ugly.
	m_hackInteractablesMgrIgnoreDisabledHack = interactablesMgrIgnoreDisableHack;
}

void NdInteractControl::DisableForOneFrame()
{
	m_disabledFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused + 1;
}

void NdInteractControl::DisableInteractionForOneFrame()
{
	m_disableInteractionFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused + 1;
}

bool NdInteractControl::ShouldPropagateHighContrastModeToOwner() const
{
	const NdGameObject* pSelf = m_hSelf.ToProcess();
	return pSelf && !pSelf->ManagesOwnHighContrastMode();
}

DC::HCMModeType NdInteractControl::GetCurrentHighContrastModeType() const
{
	StringId64 newHcmModeId = m_disabled ? GetHCMModeDisabled() : GetHCMMode();
	return FromStringId64ToHCMModeType(newHcmModeId);
}

void NdInteractControl::UpdateHighContrastModeType()
{
	const bool propagate = ShouldPropagateHighContrastModeToOwner();

	// -- DT 100745 - don't let "unavailable" interacts show up as interactable
	const bool unavailableFromScript = !g_interactableOptions.m_allowInteractionWithUnavailables && IsUnavailableFromScript();

	StringId64 newHCMMode = (m_disabled || unavailableFromScript) ? GetHCMModeDisabled() : GetHCMMode();

	m_hcmModeOverride	= m_disabled || newHCMMode == INVALID_STRING_ID_64
						? Maybe<DC::HCMModeType>(MAYBE::kNothing)
						: Maybe<DC::HCMModeType>(FromStringId64ToHCMModeType(newHCMMode));

	NdGameObject* pSelf = m_hSelf.ToMutableProcess();
	if (!pSelf)
		return;

	const Binding& binding = pSelf->GetBoundFrame().GetBinding();
	const RigidBodyBase* pRb = binding.GetRigidBodyBase();

	NdGameObject* pParentGo = pRb ? NdGameObject::FromProcess(pRb->GetOwner()) : nullptr;

	const DC::InteractableDef* pDef = GetDef();
	if (pParentGo && (!pDef || pDef->m_hcmShouldSetParentProxy))
		pParentGo->SetHighContrastModeProxy(pSelf);

	for (int i = 0; i < m_numHcmModeTargets; ++i)
	{
		NdGameObject* pHcmTarget = m_aHcmModeTargetObj[i].ToMutableProcess();
		if (pHcmTarget)
			pHcmTarget->SetHighContrastModeProxy(pSelf);
	}

	if (newHCMMode != INVALID_STRING_ID_64 && propagate)
	{
		if (pSelf)
			pSelf->SetHighContrastMode(newHCMMode);

		if (pParentGo && !pParentGo->ManagesOwnHighContrastMode() && !pParentGo->GetInteractControl())
			pParentGo->SetHighContrastMode(newHCMMode);

		for (int i = 0; i < m_numHcmModeTargets; ++i)
		{
			NdGameObject* pHcmTarget = m_aHcmModeTargetObj[i].ToMutableProcess();
			if (pHcmTarget && !pHcmTarget->ManagesOwnHighContrastMode())
				pHcmTarget->SetHighContrastMode(newHCMMode);
		}
	}
}

void NdInteractControl::UpdateHighContrastModeTarget()
{
	bool foundAnyObjects = false;
	for (int i = 0; i < m_numHcmModeTargets; ++i)
	{
		if (m_aHcmModeTargetObjId[i] == INVALID_STRING_ID_64)
			continue;

		if (m_aHcmModeTargetObj[i].HandleValid())
			continue;

		m_aHcmModeTargetObj[i] = NdGameObject::LookupGameObjectHandleByUniqueId(m_aHcmModeTargetObjId[i]);
		if (m_aHcmModeTargetObj[i].HandleValid())
			foundAnyObjects = true;
	}

	if (foundAnyObjects)
		UpdateHighContrastModeType();
}

bool NdInteractControl::IsInteractionEnabled() const
{
	if (m_initialized && !m_disabled)
	{
		return true;
	}

	return false;
}

void NdInteractControl::SetUnavailableFromScript(bool unavailable)
{
	const bool prevValue = m_unavailableScript;
	m_unavailableScript = unavailable;

	if (prevValue != unavailable)
		UpdateHighContrastModeType();
}

bool NdInteractControl::HideUnavailable() const
{
	if (const DC::InteractableDef* pDef = GetDef())
	{
		if (m_unavailableSeenTime != TimeFrameZero() && pDef->m_hideFullTime > 0 && EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetTimePassed(m_unavailableSeenTime).ToSeconds() > pDef->m_hideFullTime)
			return true;
	}
	return false;
}

void NdInteractControl::SetCustomDefActivationRadius(float radius)
{
	if (m_pCustomDef)
		m_pCustomDef->m_activationRadius = radius;
}

void NdInteractControl::SetCustomDefInteractRadius(float radius)
{
	if (m_pCustomDef)
		m_pCustomDef->m_interactionRadius = radius;
}

void NdInteractControl::SetCustomDefActivationHeight(float yMin, float yMax)
{
	if (m_pCustomDef != nullptr)
	{
		m_pCustomDef->m_activationHeightThresholdYMin = yMin;
		m_pCustomDef->m_activationHeightThresholdYMax = yMax;
	}
}

void NdInteractControl::SetCustomDefInteractionHeight(float yMin, float yMax)
{
	if (m_pCustomDef != nullptr)
	{
		m_pCustomDef->m_interactionHeightThresholdYMin = yMin;
		m_pCustomDef->m_interactionHeightThresholdYMax = yMax;
	}
}

StringId64 NdInteractControl::GetShowupHintSound() const
{
	const DC::InteractableDef* pDef = GetDef();
	if (pDef != nullptr)
		return pDef->m_showupHintSound;
	else
		return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdInteractControl::GetHCMMode() const
{
	const DC::InteractableDef* pDef = GetDef();
	return pDef ? pDef->m_highContrastModeType : INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdInteractControl::EvaluateForInteractablesMgrEvenWhenDisabled() const
{
	const DC::InteractableDef* pDef = GetDef();
	return pDef ? pDef->m_evaluateForInteractablesMgrEvenWhenDisabled : false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdInteractControl::GetHCMModeDisabled() const
{
	const DC::InteractableDef* pDef = GetDef();
	return pDef ? pDef->m_highContrastModeTypeDisabled : INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("interactable-get-def-id", DcInteractableGetDefId)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	const NdGameObject* pInteractable = args.NextGameObject();

	StringId64 defId = INVALID_STRING_ID_64;
	if (pInteractable != nullptr)
	{
		const NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl != nullptr)
		{
			defId = pCtrl->GetDefId();
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(defId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("interactable-highlight", DcInteractableHighlight)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pGameObject = args.NextGameObject();
	StringId64 colorId = args.NextStringId();
	const Color* pColor = args.NextPointer<Color>();
	DC::InteractableDepth depth = args.NextI32();

	HighlightStyle style(kHighlightStyleGlowNormal);
	switch (depth)
	{
	case DC::kInteractableDepthDisable:		style = kHighlightStyleGlowNoDepthTest;	break;
	case DC::kInteractableDepthInvert:		style = kHighlightStyleGlowInvertDepthTest;	break;
	}

	if (pGameObject != nullptr && (colorId != INVALID_STRING_ID_64 || pColor != nullptr))
	{
		Color realColor;
		if (pColor)
		{
			realColor = Color(pColor->r, pColor->g, pColor->b, pColor->a);
		}
		else
		{
			realColor = Color(1.0f, 1.0f, 1.0f, 1.0f); //NdInteractControl::GetStandardHighlightColor(colorId);
		}

		pGameObject->Highlight(realColor, style);
		return ScriptValue(true);
	}
	else
	{
		args.MsgScriptError("invalid color\n");
	}
	return ScriptValue(false);
}

static void DcInteractableEnableDisable(NdScriptArgIterator& args, bool enable)
{
	NdGameObject* pInteractable = args.NextGameObject();
	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			pCtrl->SetInteractionEnabled(enable);
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}
}

SCRIPT_FUNC("interactable-enable", DcInteractableEnable)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	DcInteractableEnableDisable(args, true);
	return ScriptValue(false);
}

SCRIPT_FUNC("interactable-disable", DcInteractableDisable)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	DcInteractableEnableDisable(args, false);
	return ScriptValue(false);
}

SCRIPT_FUNC("interactable-set-unavailable", DcInteractableSetUnavailable)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pInteractable = args.NextGameObject();
	bool unavailable = args.NextBoolean();
	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			const bool kScripted = true;
			pCtrl->SetUnavailableFromScript(unavailable);
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}
	return ScriptValue(false);
}

SCRIPT_FUNC("interactable-scripted-unavailable?", DcInteractableScriptedUnavailable)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();
	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			return ScriptValue(pCtrl->IsUnavailableFromScript());
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}
	return ScriptValue(false);
}

SCRIPT_FUNC("interactable-enabled?", DcInteractableEnabledP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();
	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			return ScriptValue(pCtrl->IsInteractionEnabled());
		}
	}
	return ScriptValue(false);
}

SCRIPT_FUNC("interactable-set-trigger-only", DcInteractableSetTriggerOnly)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();
	bool triggerOnly = args.NextBoolean();
	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			pCtrl->SetTriggerOnly(triggerOnly);
		}
	}
	return ScriptValue(false);
}

SCRIPT_FUNC("interactable-trigger-only?", DcInteractableTriggerOnlyP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();
	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			return ScriptValue(pCtrl->IsTriggerOnly());
		}
	}
	return ScriptValue(false);
}

SCRIPT_FUNC("interactable-selected?", DcInteractableSelectedP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();
	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			return ScriptValue(pCtrl->IsSelected());
		}
	}
	return ScriptValue(false);
}

SCRIPT_FUNC("interactable-set-partner-enabled", DcInteractableSetPartnerEnabled)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();
	bool partnerEnabled = args.NextBoolean();
	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			pCtrl->SetPartnerEnabled(partnerEnabled);
		}
	}
	return ScriptValue(false);
}

SCRIPT_FUNC("interactable-partner-enabled?", DcInteractablePartnerEnabledP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();
	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			return ScriptValue(pCtrl->IsPartnerEnabled());
		}
	}
	return ScriptValue(false);
}

SCRIPT_FUNC("override-interact-def", DcOverrideInteractDef)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pInteractable = args.NextGameObject();
	StringId64 interactDefId = args.NextStringId();
	if (pInteractable && interactDefId != INVALID_STRING_ID_64)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			pCtrl->OverrideInteractableDef(interactDefId);
		}
	}
	return ScriptValue(false);
}

///-------------------------------------------------------------------------------------------///
SCRIPT_FUNC("interactable-set-override-interaction-lambda", DcInteractableSetOverrideInteractionLambda)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pInteractable = args.NextGameObject();
	const DC::ScriptLambda* pLmb = args.NextPointer<DC::ScriptLambda>();

	if(pInteractable && pLmb)
	{
		NdInteractControl* pController = pInteractable->GetInteractControl();
		if(pController)
		{
			pController->SetInteractOverrideLambda(*pLmb);
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------///
SCRIPT_FUNC("interactable-use-def-from-other", DcInteractableUseDefFromOther)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pInteractable = args.NextGameObject();
	const NdGameObject* pOtherObj = args.NextGameObject();

	if (pInteractable != nullptr)
	{
		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl != nullptr)
		{
			if (pOtherObj == nullptr)
			{
				args.MsgScriptError("Cound't find source object\n");
			}

			pInteractCtrl->UseInteractableDefFromOther(pOtherObj);
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------///
SCRIPT_FUNC("interactable-clear-def-from-other", DcInteractableClearDefFromOther)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	NdGameObject* pInteractable = args.NextGameObject();

	if (pInteractable != nullptr)
	{
		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl != nullptr)
		{
			pInteractCtrl->UseInteractableDefFromOther(nullptr);
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------///
SCRIPT_FUNC("interactable-set-interact-radius", DcInteractableSetCustomInteractRadius)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pInteractable = args.NextGameObject();
	float radius = args.NextFloat();

	if (pInteractable)
	{
		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl)
		{
			if (pInteractCtrl->HasCustomDef())
			{
				pInteractCtrl->SetCustomDefInteractRadius(radius);
			}
			else
			{
				args.MsgScriptError("Object '%s' does not have a custom interactable def!\n", pInteractable->GetName());
			}
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------///
SCRIPT_FUNC("interactable-set-interaction-height", DcInteractableSetCustomInteractHeight)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	NdGameObject* pInteractable = args.NextGameObject();
	float yMin = args.NextFloat();
	float yMax = args.NextFloat();

	if (pInteractable)
	{
		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl)
		{
			if (pInteractCtrl->HasCustomDef())
			{
				pInteractCtrl->SetCustomDefInteractionHeight(yMin, yMax);
			}
			else
			{
				args.MsgScriptError("Object '%s' does not have a custom interactable def!\n", pInteractable->GetName());
			}
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------///
SCRIPT_FUNC("interactable-set-activation-radius", DcInteractableSetCustomActivationRadius)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pInteractable = args.NextGameObject();
	float radius = args.NextFloat();

	if (pInteractable)
	{
		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl)
		{
			if (pInteractCtrl->HasCustomDef())
			{
				pInteractCtrl->SetCustomDefActivationRadius(radius);
			}
			else
			{
				args.MsgScriptError("Object '%s' does not have a custom interactable def!\n", pInteractable->GetName());
			}
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------///
SCRIPT_FUNC("interactable-set-activation-height", DcInteractableSetCustomActivationHeight)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	NdGameObject* pInteractable = args.NextGameObject();
	float yMin = args.NextFloat();
	float yMax = args.NextFloat();

	if (pInteractable)
	{
		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl)
		{
			if (pInteractCtrl->HasCustomDef())
			{
				pInteractCtrl->SetCustomDefActivationHeight(yMin, yMax);
			}
			else
			{
				args.MsgScriptError("Object '%s' does not have a custom interactable def!\n", pInteractable->GetName());
			}
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------///
SCRIPT_FUNC("interactable-set-activation-region", DcInteractableSetActivationRegion)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pInteractable = args.NextGameObject();

	if (pInteractable)
	{
		StringId64 regionName = args.NextStringId();

		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl != nullptr)
		{
			pInteractCtrl->SetActivationRegionName(regionName);
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------///
SCRIPT_FUNC("interactable-get-activation-region", DcInteractableGetActivationRegion)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	const NdGameObject* pInteractable = args.NextGameObject();

	if (pInteractable)
	{
		const NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl != nullptr)
		{
			return ScriptValue(pInteractCtrl->GetActivationRegionNameId());
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------///
SCRIPT_FUNC("interactable-set-interaction-region", DcInteractableSetInteractionRegion)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pInteractable = args.NextGameObject();

	if (pInteractable)
	{
		StringId64 regionName = args.NextStringId();

		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl != nullptr)
		{
			pInteractCtrl->SetInteractionRegionName(regionName);
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------///
SCRIPT_FUNC("interactable-get-interaction-region", DcInteractableGetInteractionRegion)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	const NdGameObject* pInteractable = args.NextGameObject();

	if (pInteractable)
	{
		const NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl != nullptr)
		{
			return ScriptValue(pInteractCtrl->GetInteractionRegionNameId());
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("request-interact", DcRequestInteract)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();
	MutableNdGameObjectHandle hUser = args.NextGameObject();
	bool interactButtonHeld = args.NextBoolean();
	bool interactButtonClicked = args.NextBoolean();

	if (!hUser.HandleValid())
		hUser = EngineComponents::GetNdGameInfo()->GetPlayerGameObjectHandle();

	if (pInteractable)
	{
		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl)
		{
			pInteractCtrl->RequestInteract(hUser, interactButtonHeld, interactButtonClicked);
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("interact-with", DcInteractWith)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();
	MutableNdGameObjectHandle hUser = args.NextGameObject();
	bool disallowFallback = args.NextBoolean();

	if (!hUser.HandleValid() && !disallowFallback)
		hUser = EngineComponents::GetNdGameInfo()->GetPlayerGameObjectHandle();

	if (pInteractable)
	{
		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl)
		{
			pInteractCtrl->Interact(hUser);
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(0);
}


SCRIPT_FUNC("interactable-allow-hint-sound-once", DcInteractableAllowHintSoundOnce)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();
	if (pInteractable)
	{
		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl)
		{
			pInteractCtrl->SetHintSoundPlayed(false);
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("interactable-add-high-contrast-mode-target", DcInteractableAddHighContrastModeTarget)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pInteractable = args.NextGameObject();
	StringId64 targetId = args.GetStringId(); // in case the target hasn't spawned yet
	NdGameObject* pTarget = args.NextGameObject();

	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			const bool succeeded = pCtrl->AddHighContrastModeTarget(targetId, pTarget);
			if (!succeeded)
			{
				args.MsgScriptError("Could not add %s as high-contrast-mode-target for interactable %s, too many targets already?", DevKitOnly_StringIdToString(targetId), pInteractable->GetName());
			}
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("interactable-remove-high-contrast-mode-target", DcInteractableRemoveHighContrastModeTarget)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	NdGameObject* pInteractable = args.NextGameObject();
	StringId64 targetId = args.NextStringId();
	DC::HCMModeType newHcm = (DC::HCMModeType)args.NextU32();

	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			pCtrl->RemoveHighContrastModeTarget(targetId, newHcm);
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("interactable-remove-all-high-contrast-mode-targets", DcInteractableRemoveAllHighContrastModeTargets)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pInteractable = args.NextGameObject();
	DC::HCMModeType newHcm = (DC::HCMModeType)args.NextU32();

	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			pCtrl->RemoveAllHighContrastModeTargets(newHcm);
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("interactable-disable-accessibility-cues", DcInteractableDisableAccessibilityCues)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();

	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			pCtrl->SetAccessibilityCuesDisabled();
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("interactable-restore-accessibility-cues", DcInteractableRestoreAccessibilityCues)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();

	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			pCtrl->RestoreAccessibilityCues();
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("interactable-disable-single-interactable/f", DcInteractableSuppressSingleInteractable)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();

	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			pCtrl->DisableForOneFrame();
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("interactable-disable-single-interactable-interaction/f", DcInteractableSuppressSingleInteractableInteraction)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pInteractable = args.NextGameObject();

	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			pCtrl->DisableInteractionForOneFrame();
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("interactable-set-ignore-probes", DcInteractableSetIgnoreProbes)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pInteractable = args.NextGameObject();
	bool ignoreProbes = args.NextBoolean();

	if (pInteractable)
	{
		NdInteractControl* pCtrl = pInteractable->GetInteractControl();
		if (pCtrl)
		{
			pCtrl->SetIgnoreProbes(ignoreProbes);
		}
		else
		{
			args.MsgScriptError("Object '%s' is not interactable\n", pInteractable->GetName());
		}
	}

	return ScriptValue(0);
}

// ========================================================================

InteractAvailability NdInteractControl::DetermineAvailability(NdGameObjectHandle hUser) const
{
	return (hUser.HandleValid() && IsInteractionEnabled()) ? kInteractableAvailable : kInteractableInvalid;
}

InteractAvailability NdInteractControl::GetAvailability(NdGameObjectHandle hUser, bool useInteractablesMgrHack /* = false*/) const
{
	PROFILE_AUTO(GameLogic);

	InteractAvailability avail = DetermineAvailability(hUser); // call the virtual hook

	// Hack for carry rope, which needs to constantly disable the interactable due to screen space, which the Interactables Mgr shouldn't care about.
	if (!IsInteractionEnabled() && !(useInteractablesMgrHack && m_hackInteractablesMgrIgnoreDisabledHack))
		return kInteractableInvalid;

	if (avail == kInteractableAvailable && !g_interactableOptions.m_allowInteractionWithUnavailables && IsUnavailableFromScript())
	{
		avail = kInteractableUnavailable;
	}

	if (avail == kInteractableUnavailable && HideUnavailable())
	{
		avail = kInteractableInvalid;
	}

	if (g_interactableOptions.m_allInteractablesUnavailable && avail == kInteractableAvailable)
		avail = kInteractableUnavailable;

	ALWAYS_ASSERTF(avail >= kInteractableInvalid && avail <= kInteractableAvailable, ("NdInteractControl::GetAvailability(): Invalid availability value"));
	return avail;
}

