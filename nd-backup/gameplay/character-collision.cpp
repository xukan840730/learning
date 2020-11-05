/*
 * Copyright (c) 2008 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/character-collision.h"

#include "corelib/math/intersection.h"
#include "ndlib/render/dev/render-options.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/scriptx/h/characters-collision-settings-defines.h"

#include <Physics/Physics/Collide/Shape/Convex/Capsule/hknpCapsuleShape.h>
#include <Physics/Physics/Dynamics/Material/hknpMaterialLibrary.h>

// static 
void CharacterCollision::PostInit(NdGameObject& self, const DC::CharacterCollision* pCharColl)
{
	CompositeBody* pCompo = self.GetCompositeBody();
	if (!pCompo)
		return;

	GAMEPLAY_ASSERT(pCharColl);

	HavokMarkForWriteJanitor jj(true);

	for (U32F ii = 0; ii<pCompo->GetNumBodies(); ii++)
	{
		RigidBody* pBody = pCompo->GetBody(ii);

		if (pBody->GetGameLinkage() == RigidBody::kLinkedToObject)
		{
			// Align capsule is there just to block player
			pBody->AddPatBits(
				Pat::kShootThroughMask |
				Pat::kSeeThroughMask |
				Pat::kRopeThroughMask |
				Pat::kHearThroughMask |
				Pat::kHearThroughLoudMask |
				Pat::kNoPhysicsMask |
				Pat::kCameraPassThroughMask
			);
			pBody->SetNoBroadphase(true);
		}
		else
		{
			StringId64 tag = pBody->GetTag();

			if (tag == SID("ragdoll_collision"))
			{
				// Special collision only for power ragdoll
				// We use this on horse to provide collision for power ragdoll of a rider
				pBody->SetLayer(Collide::kLayerRagdollOnly);
			}
			else
			{
				// Other character capsules are ignored by player probe or camera
				pBody->AddPatBits(
					Pat::kPassThroughMask |
					Pat::kCameraPassThroughMask
				);

				pBody->SetSoftContact(true);

				{
					hknpMaterialId oldMaterialId = pBody->GetHavokBodyRw()->m_materialId;
					pBody->GetHavokBodyRw()->m_materialId = HavokMaterialId(HavokMaterialId::PRESET_CHARACTER);
					if (oldMaterialId.value() >= HavokMaterialId::NUM_GAME_PRESETS)
					{
						g_havok.m_pWorld->accessMaterialLibrary()->removeEntry(oldMaterialId);
					}
				}

				// Assign Character Collision DC settings as user data to each body
				for (U32F iDcSet = 0; iDcSet<pCharColl->m_settings->GetSize(); iDcSet++)
				{
					const DC::CharacterCollisionSettings* pDcSet = pCharColl->m_settings->At(iDcSet);
					for (U32F iTag = 0; iTag<pDcSet->m_numTags; iTag++)
					{
						if (pDcSet->m_tags[iTag] == tag)
						{
							pBody->SetUserData(reinterpret_cast<U64>(pDcSet));
							break;
						}
					}
				}
			}
		}
	}
}

//static 
void CharacterCollision::SetLayer(CompositeBody* pCompo, Collide::Layer layer)
{
	for (U32 ii = 0; ii<pCompo->GetNumBodies(); ii++)
	{
		if (pCompo->GetBody(ii)->GetLayer() != Collide::kLayerRagdollOnly)
		{
			pCompo->GetBody(ii)->SetLayer(layer);
		}
	}
}

// static 
StringId64 CharacterCollision::GetBodyAttachPoint(const RigidBody* pBody)
{
	if (!pBody)
		return INVALID_STRING_ID_64;

	const DC::CharacterCollisionSettings* pCollSet = reinterpret_cast<const DC::CharacterCollisionSettings*>(pBody->GetUserData());
	if (pCollSet && pCollSet->m_attachName != INVALID_STRING_ID_64)
	{
		return pCollSet->m_attachName;
	}
	return pBody->GetTag();
}

// static 
const DC::CharacterCollisionSettings* CharacterCollision::GetBodyCollisionSettings(const RigidBody* pBody)
{
	if (!pBody)
		return nullptr;

	return reinterpret_cast<const DC::CharacterCollisionSettings*>(pBody->GetUserData());
}

static bool BoxShapeCastAgainst(Point_arg cameraCenter, Quat_arg cameraRot, Vector_arg halfWidth, F32* pNearestApproach, bool debugDraw)
{
	ICollCastJob::Flags flags = ICollCastJob::kCollCastSynchronous | ICollCastJob::kCollCastHighPriority | ICollCastJob::kCollCastAllStartPenetrations;
	CollideFilter filter = CollideFilter(Collide::kLayerMaskPlayer);
	filter.SetPatInclude(Pat(Pat::kShootThroughMask));  // assume shoot-through is backpack

	HavokShapeCastJob job;
	job.Open(1, 1, flags, ICollCastJob::kClientCamera);

	job.SetProbeShape(0, HavokProbeShape(halfWidth, cameraRot));
	job.SetProbeExtents(0, cameraCenter, kZero, 1.0f);
	job.SetProbeFilter(0, filter);

	job.Kick(FILE_LINE_FUNC);
	job.Wait();

	if (FALSE_IN_FINAL_BUILD(debugDraw))
		job.DebugDraw();

	bool hasContact = job.IsContactValid(0, 0);
	if (pNearestApproach != nullptr && hasContact)
	{
		const Point contactPoint = job.GetContactPoint(0, 0);

		const Vector planeNormalWs = GetLocalZ(cameraRot);
		const Vector centerToClosestWs = contactPoint - cameraCenter;
		const Scalar distToAabbNormal = Dot(centerToClosestWs, planeNormalWs);
		const Vector toClosestNormalWs = distToAabbNormal * planeNormalWs;
		const Vector toClosestTangentialWs = centerToClosestWs - toClosestNormalWs;
		const Point closestOnNearPlaneWs = cameraCenter + toClosestTangentialWs;

		*pNearestApproach = Dist(closestOnNearPlaneWs, contactPoint);
	}

	job.Close();

	return hasContact;
}

// static 
bool CharacterCollision::IsClippingCameraPlane(const CompositeBody* pCompo, 
											   bool useCollCastForBackpack,
											   F32 surfaceExpand, 
											   F32 surfaceExpandBackpack,
											   Point_arg planeCenterWs, 
											   Vector_arg planeNormalWs, 
											   Quat_arg cameraRot,
											   Vec2_arg cameraWidthHeight,
											   F32 clipRadius, 
											   F32* pNearestApproach,
											   bool debugDraw)
{
	Scalar sqrNearestApproach(kLargestFloat);
	if (pNearestApproach)
		sqrNearestApproach = *pNearestApproach * (*pNearestApproach);

	bool clipping = false;

	HavokMarkForReadJanitor jj;

	for (U32F iBody = 0; iBody < pCompo->GetNumBodies(); ++iBody)
	{
		const RigidBody* pBody = pCompo->GetBody(iBody);
		if (!pBody)
			continue;

		const hknpShape* pShape = pBody->GetHavokShape();
		const Locator bodyLocWs = pBody->GetLocatorCm();

		const DC::CharacterCollisionBody* pSettings = pBody->GetCharacterCollisionBody();
		if (pSettings && pSettings->m_attachedToAlign)
			continue;

		const DC::CharacterCollisionSettings* pDcSet = CharacterCollision::GetBodyCollisionSettings(pBody);
		if (pDcSet && pDcSet->m_ignoreForCameraTest)
			continue;

		Pat testPat;

		if (pShape->getType() == hknpShapeType::CAPSULE)
		{
			// compute capsule - this should work for sphere (produces a capsule with both points the same), or box
			// (produces a near-sized capsule down the center of the box)
			const hknpCapsuleShape* pCapsule = static_cast<const hknpCapsuleShape*>(pShape);
			const Scalar radius = (F32)pCapsule->getRadius() + surfaceExpand;
			const Point capsuleAWs = bodyLocWs.TransformPoint(Point(pCapsule->getPointA().getQuad()));
			const Point capsuleBWs = bodyLocWs.TransformPoint(Point(pCapsule->getPointB().getQuad()));

			Point edgeIntersectWs;
			LinePlaneIntersect(planeCenterWs, planeNormalWs, capsuleAWs, capsuleBWs, &edgeIntersectWs);
			// edgeIntersectWs should contain the endpoint closest to the plane if it did not intersect the plane

			bool isIntersecting = false;
			const Scalar displacementToPlane = Dot(planeNormalWs, Vector(planeCenterWs - edgeIntersectWs));
			bool withinRadius = (Abs(displacementToPlane) <= radius);
			if (withinRadius || pNearestApproach)
			{
				// capsule is intersecting plane, determine if it also intersects a circle around the visible area

				const Point closestPosOnPlaneWs = edgeIntersectWs + planeNormalWs * displacementToPlane;

				const Vector fromCenterWs = closestPosOnPlaneWs - planeCenterWs;
				const Scalar distFromCenter = Length(fromCenterWs);
				const Point closestPosOnCircleWs = (distFromCenter > clipRadius)
					? planeCenterWs + fromCenterWs * (clipRadius / distFromCenter)
					: closestPosOnPlaneWs;

				const Scalar sqrDist = DistSqr(closestPosOnCircleWs, edgeIntersectWs);
				if (pNearestApproach && sqrNearestApproach > sqrDist)
					sqrNearestApproach = sqrDist;
				if (withinRadius && sqrDist < Sqr(radius))
				{
					isIntersecting = true;
					clipping = true;

					if (!debugDraw && !pNearestApproach)
					{
						break;
					}
				}
			}

			if (debugDraw)
			{
				g_prim.Draw(DebugCapsule(capsuleAWs, capsuleBWs, radius, isIntersecting ? kColorRed : kColorGreen));
			}
		}
		else if (useCollCastForBackpack && 
			pBody->GetSinglePat((testPat)) && 
			((testPat.m_bits & Pat::kShootThroughMask) != 0)) // assume backpack is shoot-through
		{
			float newNearestApproach = FLT_MAX;
			const Vector vExpansion = surfaceExpandBackpack >= 0.f ? Vector(surfaceExpandBackpack, surfaceExpandBackpack, surfaceExpandBackpack) : Vector(surfaceExpand, surfaceExpand, surfaceExpand);
			const Vector halfWidth = Vector(cameraWidthHeight.X() / 2.f, cameraWidthHeight.Y() / 2.f, 0.001f) + vExpansion;
			bool insectBox = BoxShapeCastAgainst(planeCenterWs, cameraRot, halfWidth, &newNearestApproach, debugDraw);
			if (insectBox)
			{
				clipping = true;

				if (Sqr(newNearestApproach) < sqrNearestApproach)
					sqrNearestApproach = Sqr(newNearestApproach);

				if (!debugDraw && !pNearestApproach)
				{
					break;
				}
			}
		}
		else
		{
			hkAabb hvkAabb;
			pShape->calcAabb(hkTransform::getIdentity(), hvkAabb);

			Point planeCenterBoxSpace = bodyLocWs.UntransformPoint(planeCenterWs);
			Aabb aabb(Point(hvkAabb.m_min.getQuad()), Point(hvkAabb.m_max.getQuad()));
			aabb.Expand(surfaceExpand);

			Point closestLs = planeCenterBoxSpace;
			aabb.ClipPoint(closestLs);
			Point closestWs = bodyLocWs.TransformPoint(closestLs);

			if (debugDraw)
			{
				g_prim.Draw(DebugLine(planeCenterWs, closestWs, kColorCyan, kColorBlue, 3.0f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugString(planeCenterWs, "plane-center", kColorWhite, 0.5f), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugString(closestWs, "closest", kColorWhite, 0.5f), kPrimDuration1FramePauseable);
			}

			const Vector centerToClosestWs = closestWs - planeCenterWs;
			const Scalar distToAabbNormal = Dot(centerToClosestWs, planeNormalWs);
			const Vector toClosestNormalWs = distToAabbNormal * planeNormalWs;
			const Vector toClosestTangentialWs = centerToClosestWs - toClosestNormalWs;
			const Point closestOnNearPlaneWs = planeCenterWs + toClosestTangentialWs;

			const Scalar closestRadiusSqr = LengthSqr(toClosestTangentialWs);
			const bool withinRadius = closestRadiusSqr < Sqr(clipRadius);

			if (debugDraw)
			{
				g_prim.Draw(DebugLine(closestOnNearPlaneWs, closestWs, kColorYellow, kColorOrange, 2.0f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugString(closestOnNearPlaneWs, "closest-on-near-plane", kColorYellow, 0.5f), kPrimDuration1FramePauseable);
			}

			const Scalar sqrDistToAabb = DistSqr(closestOnNearPlaneWs, closestWs);

			if (pNearestApproach && withinRadius && sqrNearestApproach > sqrDistToAabb)
				sqrNearestApproach = sqrDistToAabb;

			const bool insectBox = sqrDistToAabb < Sqr(SCALAR_LC(0.03f)) && withinRadius; //sqrDistToAabb < Sqr(clipRadius);
			if (insectBox)
			{
				clipping = true;

				if (!debugDraw && !pNearestApproach)
				{
					break;
				}
			}

			if (debugDraw)
			{
				g_prim.Draw(DebugCoordAxes(bodyLocWs, 0.2f), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugBox(Transform(bodyLocWs.Rot(), bodyLocWs.Pos()), aabb.m_min, aabb.m_max,
					insectBox ? kColorRed : kColorGreen, kPrimEnableWireframe),
					kPrimDuration1FramePauseable);
			}
		}
	}

	if (pNearestApproach)
		*pNearestApproach = (float)Sqrt(sqrNearestApproach);

	return clipping;
}