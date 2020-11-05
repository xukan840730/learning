/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */


#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/ndphys/rope/rope2-point-col.h"
#include "gamelib/ndphys/rope/physvectormath.h"
#include "gamelib/ndphys/rope/physalignedarrayonstack.h"
#include "gamelib/ndphys/rope/rope-mgr.h"
#include "ndlib/profiling/profiling.h"
#include "corelib/math/gamemath.h"
#include "corelib/math/intersection.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/process/debug-selection.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"

#include <Physics/Physics/Dynamics/Body/hknpBody.h>
#include <Physics/Physics/Collide/Shape/Convex/Triangle/hknpTriangleShape.h>
#include <Physics/Physics/Collide/Shape/Convex/Box/hknpBoxShape.h>
#include <Physics/Physics/Collide/Shape/Convex/Sphere/hknpSphereShape.h>
#include <Physics/Physics/Collide/Shape/Convex/Capsule/hknpCapsuleShape.h>
#include <Physics/Physics/Collide/Shape/Convex/Cylinder/hknpCylinderShape.h>
#include <Physics/Physics/Collide/Query/Collector/hknpCollisionQueryCollector.h>
#include <Physics/Physics/Collide/Query/hknpCollisionQuery.h>
#include <Geometry/Internal/Algorithms/Gsk/hkcdGsk.h>

static const Scalar kOneHalf(0.5f);
static const Scalar kTiny(1e-15f);

U64 s_allSetBitArray1024Storage[1024/64];
static ExternalBitArray s_allSetBitArray1024(1024, s_allSetBitArray1024Storage, true);

static bool isCharacterBody(const RopeColliderHandle& hCollider)
{
	const RigidBody* pBody = hCollider.GetRigidBody();
	if (pBody) 
	{
		U32F layer = pBody->GetLayer();
		return ((1ULL << layer) & Collide::kLayerMaskCharacterAndProp) != 0;
	}
	return false;
}

void Rope2::CollideWithColCacheCollider(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, U32 triIndex, const ExternalBitArray* pTestPoints)
{
	switch (pCollider->m_pShape->getType())
	{
	case hknpShapeType::TRIANGLE:
		PHYSICS_ASSERT(false);
		break;

	case hknpShapeType::SPHERE:
		CollideWithSphere(pCollider, hCollider, pTestPoints);
		break;

	case hknpShapeType::BOX:
		CollideWithBox(pCollider, hCollider, pTestPoints);
		break;

	case hknpShapeType::CAPSULE:
		CollideWithCapsule(pCollider, hCollider, pTestPoints);
		break;

	case hknpShapeType::USER_0:
		PHYSICS_ASSERT(false);
		break;

	default:
		if (const hknpConvexPolytopeShape* pConvex = pCollider->m_pShape->asConvexPolytopeShape())
		{
			CollideWithColCacheConvex(pCollider, hCollider, pConvex, triIndex, pTestPoints);
		}
		else
		{
			PHYSICS_ASSERT(false);
		}
	}
}

void Rope2::CollideWithCollider(const RopeCollider* pCollider, const RopeColliderHandle& hCollider)
{
	switch (pCollider->m_pShape->getType())
	{
	case hknpShapeType::TRIANGLE:
		CollideWithTriangle(pCollider, hCollider);
		break;

	case hknpShapeType::SPHERE:
		CollideWithSphere(pCollider, hCollider);
		break;

	case hknpShapeType::BOX:
		CollideWithBox(pCollider, hCollider);
		break;

	case hknpShapeType::CAPSULE:
		CollideWithCapsule(pCollider, hCollider);
		break;

	case hknpShapeType::USER_0:
		CollideWithPlane(pCollider, hCollider);
		break;

	default:
		if (const hknpConvexPolytopeShape* pConvex = pCollider->m_pShape->asConvexPolytopeShape())
		{
			CollideWithConvex(pCollider, hCollider, pConvex);
		}
	}
}

void Rope2::CollideWithSphere(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const ExternalBitArray* pTestPoints)
{
	const hknpSphereShape* pSphere = static_cast<const hknpSphereShape*>(pCollider->m_pShape);

	Scalar	scContactRadius	(m_fRadius);
	Scalar	scShell			(scContactRadius * 1.5f);
	Scalar	scRadius		(pSphere->m_convexRadius);
	Point	ptCenter		(pCollider->m_loc.TransformPoint(Point(pSphere->getVertices()[0].getQuad())));
	Scalar	scPushRadius	(scRadius + scContactRadius);

	bool isChar = isCharacterBody(hCollider);

	bool bAnyInContact = false;

	if (!pTestPoints)
	{
		PHYSICS_ASSERT(m_lastDynamicPoint < 1024);
		pTestPoints = &s_allSetBitArray1024;
	}

	U32F numPointsRel = m_lastDynamicPoint - m_firstDynamicPoint + 1;
	for (I32F iBeadRel = pTestPoints->FindFirstSetBit(); iBeadRel < numPointsRel; iBeadRel = pTestPoints->FindNextSetBit(iBeadRel))
	{
		I32F iBead = m_firstDynamicPoint + iBeadRel;

		// Strained sections of rope do not collide with characters
		if (IsNodeStrainedInt(iBead) || IsNodeKeyframedInt(iBead) || (isChar && m_pRopeDist[iBead] < m_minCharCollisionDist))
			continue;

		Point	ptBead				(m_pPos[iBead]);
		Vector	vecCenter2Bead		(ptBead - ptCenter);

		SMath::Vec4	plane(SafeNormalize(vecCenter2Bead, kUnitYAxis).GetVec4());
		ASSERT(plane.W() == 0.0f);

		plane.SetW(-Scalar(FastDot(ptCenter.QuadwordValue(), plane.QuadwordValue())) - scRadius); // plane has 0 in the last component here, so we can dot it with the point
		Scalar planeDotBead(FastDot(MakeXYZ1(ptBead.QuadwordValue()), plane.QuadwordValue()));

		if (planeDotBead < scShell && CheckConstraintPlane(iBead, plane))
		{
			m_pConstr[iBead].AddPlane(plane, hCollider);
			bAnyInContact = true;
		}
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	#if ROPE_DEBUG_DRAW
	if (g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		Color color = bAnyInContact ? kColorRed : kColorGray;
		g_prim.Draw(DebugSphere(ptCenter, scRadius, color, PrimAttrib(kPrimEnableWireframe)), kPrimDuration1FramePauseable);
	}
	#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

void Rope2::CollideWithPlane(const RopeCollider* pCollider, const RopeColliderHandle& hCollider)
{
	Point ptCenter = pCollider->m_loc.GetTranslation();
	Vector norm = GetLocalZ(pCollider->m_loc.GetRotation());
	SMath::Vec4	plane(norm.GetVec4());

	bool isChar = isCharacterBody(hCollider);

	bool bAnyInContact = false;

	for (I32F i = m_firstDynamicPoint; i <= m_lastDynamicPoint; ++i)
	{
		// Strained sections of rope do not collide with characters
		if (IsNodeStrainedInt(i) || IsNodeKeyframedInt(i) || (isChar && m_pRopeDist[i] < m_minCharCollisionDist) || m_pRadius[i] == 0.0f)
			continue;

		Scalar scContactRadius(m_pRadius[i]);
		Scalar scShell(scContactRadius * 1.5f);
		Scalar planeD = -Dot(norm, ptCenter - kOrigin);
		plane.SetW(planeD);

		Point ptBead(m_pPos[i]);
		Scalar planeDotBead(FastDot(MakeXYZ1(ptBead.QuadwordValue()), plane.QuadwordValue()));

		if (planeDotBead < scShell && CheckConstraintPlane(i, plane))
		{
			m_pConstr[i].AddPlane(plane, hCollider);
			bAnyInContact = true;
		}
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#if ROPE_DEBUG_DRAW
	if (g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		Color color = bAnyInContact ? kColorRed : kColorGray;
		DebugDrawPlane(plane, ptCenter, color, 0.5f * MaxComp(m_aabbSlacky.GetSize()), color, 0.25f * MaxComp(m_aabbSlacky.GetSize()), PrimAttrib(kPrimEnableWireframe), kPrimDuration1FramePauseable);
	}
#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

void Rope2::CollidePointWithPlane(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider)
{
	Point ptCenter = pCollider->m_loc.GetTranslation();
	Vector norm = GetLocalZ(pCollider->m_loc.GetRotation());
	SMath::Vec4	plane(norm.GetVec4());

	Scalar planeD = Dot(norm, ptCenter - kOrigin);
	plane.SetW(planeD);

	bool bInContact = false;
	if (CheckConstraintPlane(iPoint, plane))
	{
		m_pConstr[iPoint].AddPlane(plane, hCollider);
		bInContact = true;
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#if ROPE_DEBUG_DRAW
	if (bInContact && g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		Color color = bInContact ? kColorRed : kColorGray;
		DebugDrawPlane(plane, ptCenter, color, 0.5f * MaxComp(m_aabbSlacky.GetSize()), color, 0.25f * MaxComp(m_aabbSlacky.GetSize()), PrimAttrib(kPrimEnableWireframe), kPrimDuration1FramePauseable);
	}
#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

void Rope2::CollideWithBox(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const ExternalBitArray* pTestPoints)
{
	const hknpBoxShape* pBox = static_cast<const hknpBoxShape*>(pCollider->m_pShape);
	CollideWithBox(pCollider, hCollider, pBox, pTestPoints);
}

void Rope2::CollideWithBox(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const hknpBoxShape* pBox, const ExternalBitArray* pTestPoints)
{
	bool isChar = isCharacterBody(hCollider);

	Locator loc = pCollider->m_loc;

	const hkcdObb& obb = pBox->getObb();
	hkQuaternion boxQuat(obb.getRotation());
	Locator boxLoc = loc.TransformLocator(Locator(Point(obb.getTranslation().getQuad()), Quat(boxQuat.m_vec.getQuad())));
	hkVector4 he;
	obb.getHalfExtents(he);
	he.add(hkVector4(pBox->m_convexRadius, pBox->m_convexRadius, pBox->m_convexRadius));
	Vector halfSize(he.getQuad());

	Scalar scThreshold(2e-2f);
	Point ptThreshold(scThreshold);

	// Grow box by radius
	Vector vcRadius(m_fRadius, m_fRadius, m_fRadius);
	Vector vcHalfSizeRad = halfSize + vcRadius;
	v128 negRadius = SMATH_VEC_REPLICATE_FLOAT(-m_fRadius);

	Locator rayLoc(pCollider->m_locPrev.GetTranslation(), loc.GetRotation()); // previous position + current rotation
	Vector shapeMove(loc.GetTranslation() - pCollider->m_locPrev.GetTranslation());

	bool bAnyInContact = false;

	hknpInplaceTriangleShape targetTriangle( 0.0f );
	hknpCollisionQueryContext queryContext( nullptr, targetTriangle.getTriangleShape() );

	hknpRayCastQuery query;
					
	hknpQueryFilterData filterData;
	HavokClosestHitCollector collector;

	hknpShapeQueryInfo queryInfo;
	queryInfo.m_rootShape = nullptr;
	hkTransform shapeXfm; shapeXfm.setIdentity();
	queryInfo.m_shapeToWorld = &shapeXfm;

	if (!pTestPoints)
	{
		PHYSICS_ASSERT(m_lastDynamicPoint < 1024);
		pTestPoints = &s_allSetBitArray1024;
	}

	U32F numPointsRel = m_lastDynamicPoint - m_firstDynamicPoint + 1;
	for (I32F iBeadRel = pTestPoints->FindFirstSetBit(); iBeadRel < numPointsRel; iBeadRel = pTestPoints->FindNextSetBit(iBeadRel))
	{
		I32F iBead = m_firstDynamicPoint + iBeadRel;

		// Strained sections of rope do not collide with characters
		if (IsNodeStrainedInt(iBead) || IsNodeKeyframedInt(iBead) || (isChar && m_pRopeDist[iBead] < m_minCharCollisionDist))
			continue;

		if (m_pConstr[iBead].HasCollider(hCollider))
			continue;

		Point ptBead(m_pPos[iBead]);
		{
			Point rayFrom = rayLoc.UntransformPoint(m_pLastPos[iBead]);
			Point rayTo = rayLoc.UntransformPoint(m_pPos[iBead] - shapeMove);

			query.m_ray.setEndPoints(hkVector4(rayFrom.QuadwordValue()), hkVector4(rayTo.QuadwordValue()));

			collector.reset();
			pBox->castRayImpl( &queryContext, query, filterData, queryInfo, &collector);
			if (collector.hasHit())
			{
				ptBead = m_pLastPos[iBead] + collector.getHit().m_fraction * (m_pPos[iBead] - shapeMove - m_pLastPos[iBead]) + shapeMove;
			}
		}

		Point ptBeadLocal = boxLoc.UntransformPoint(ptBead);
		Point ptBeadLocalAbs = Abs(ptBeadLocal);
		//Point ptBeadLocalNext = loc.UntransformPoint(m_pPos[iBead] + m_pVel[iBead] * m_scLastStepTime);
		//Point ptBeadLocalNextAbs = Abs(ptBeadLocalNext);
		Point ptBeadTest = ptBeadLocalAbs; //Min(ptBeadLocalAbs, ptBeadLocalNextAbs);
		Point planeDist = ptBeadTest - vcHalfSizeRad;
		if (AllComponentsLessThan(planeDist, ptThreshold))
		{
			// Collision!
			// Which coordinate is closest 
			v128 dist = planeDist.QuadwordValue();
			v128 distZxy = SMATH_VEC_SHUFFLE_ZXY(dist);
			v128 maxMask = SMATH_VEC_CMPGT(dist, distZxy);
			v128 plane1Mask, plane2Mask;
			if (SMATH_VEC_MOVEMASK(SMATH_VEC_AND(maxMask, SMATH_VEC_GET_MASKOFF_ZW())) == 0x01)
			{
				plane1Mask = SMATH_VEC_GET_MASK_X();
				plane2Mask = SMATH_VEC_SEL(SMATH_VEC_GET_MASK_Y(), SMATH_VEC_GET_MASK_Z(), SMATH_VEC_REPLICATE_Z(maxMask));
			}
			else if (SMATH_VEC_MOVEMASK(SMATH_VEC_AND(maxMask, SMATH_VEC_GET_MASK_YZ())) == 0x02)
			{
				plane1Mask = SMATH_VEC_GET_MASK_Y();
				plane2Mask = SMATH_VEC_SEL(SMATH_VEC_GET_MASK_Z(), SMATH_VEC_GET_MASK_X(), SMATH_VEC_REPLICATE_X(maxMask));
			}
			else
			{
				plane1Mask = SMATH_VEC_GET_MASK_Z();
				plane2Mask = SMATH_VEC_SEL(SMATH_VEC_GET_MASK_X(), SMATH_VEC_GET_MASK_Y(), SMATH_VEC_REPLICATE_Y(maxMask));
			}

			v128 signs = SMATH_VEC_AND(SMATH_VEC_GET_MASK_SIGN(), ptBeadLocal.QuadwordValue());
			v128 ones = SMATH_VEC_OR(SMATH_VEC_REPLICATE_FLOAT(1.0f), signs);
			v128 points = SMATH_VEC_OR(halfSize.QuadwordValue(), signs);

			v128 plane1Norm = SMATH_VEC_AND(ones, plane1Mask);
			v128 plane1Point = SMATH_VEC_AND(points, plane1Mask);
			Vector vcPlane1Norm = boxLoc.TransformVector(Vector(plane1Norm));
			Point ptPlane1Point = boxLoc.TransformPoint(Point(plane1Point));
			Scalar scPlane1D = -Dot(ptPlane1Point - kOrigin, vcPlane1Norm);
			Vec4 plane1 = vcPlane1Norm.GetVec4();
			plane1.SetW(scPlane1D);
			I32 iPlane0 = -1;
			if (CheckConstraintPlane(iBead, plane1))
			{
				iPlane0 = m_pConstr[iBead].AddPlane(plane1, hCollider);
				bAnyInContact = true;
			}

			if (SMATH_VEC_ALL_COMPONENTS_GE(SMATH_VEC_AND(dist, plane2Mask), negRadius))
			{
				v128 plane2Norm = SMATH_VEC_AND(ones, plane2Mask);
				v128 plane2Point = SMATH_VEC_AND(points, plane2Mask);
				Vector vcPlane2Norm = boxLoc.TransformVector(Vector(plane2Norm));
				Point ptPlane2Point = boxLoc.TransformPoint(Point(plane2Point));
				Scalar scPlane2D = -Dot(ptPlane2Point - kOrigin, vcPlane2Norm);
				Vec4 plane2 = vcPlane2Norm.GetVec4();
				plane2.SetW(scPlane2D);
				if (CheckConstraintPlane(iBead, plane2))
				{
					I32 iPlane1 = m_pConstr[iBead].AddPlane(plane2, hCollider);
					if (iPlane0 >= 0 & iPlane1 >= 0)
						m_pConstr[iBead].AddEdge(iPlane0, iPlane1);
					bAnyInContact = true;
				}
			}

		}
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#if ROPE_DEBUG_DRAW
	if (g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		Color color = bAnyInContact ? kColorRed : kColorGray;
		g_prim.Draw(DebugBox(boxLoc.AsTransform(), 2.0f*halfSize, color, PrimAttrib(kPrimEnableWireframe)), kPrimDuration1FramePauseable);
	}
#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

void Rope2::CollidePointWithBox(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider)
{
	const hknpBoxShape* pBox = static_cast<const hknpBoxShape*>(pCollider->m_pShape);

	Locator loc = pCollider->m_loc;

	const hkcdObb& obb = pBox->getObb();
	hkQuaternion boxQuat(obb.getRotation());
	Locator boxLoc = loc.TransformLocator(Locator(Point(obb.getTranslation().getQuad()), Quat(boxQuat.m_vec.getQuad())));
	hkVector4 he;
	obb.getHalfExtents(he);
	Vector halfSize(he.getQuad());

	// Grow box by radius
	Vector vcRadius(m_fRadius, m_fRadius, m_fRadius);
	Vector vcHalfSizeRad = halfSize + vcRadius;
	v128 negRadius = SMATH_VEC_REPLICATE_FLOAT(-m_fRadius);

	Point ptBeadLocal = boxLoc.UntransformPoint(refPos);
	Point ptBeadLocalAbs = Abs(ptBeadLocal);
	Point ptBeadTest = ptBeadLocalAbs;
	Point planeDist = ptBeadTest - vcHalfSizeRad;

	// Which coordinate is closest 
	v128 dist = planeDist.QuadwordValue();
	v128 distZxy = SMATH_VEC_SHUFFLE_ZXY(dist);
	v128 maxMask = SMATH_VEC_CMPGT(dist, distZxy);
	v128 plane1Mask, plane2Mask;
	if (SMATH_VEC_MOVEMASK(SMATH_VEC_AND(maxMask, SMATH_VEC_GET_MASKOFF_ZW())) == 0x01)
	{
		plane1Mask = SMATH_VEC_GET_MASK_X();
		plane2Mask = SMATH_VEC_SEL(SMATH_VEC_GET_MASK_Y(), SMATH_VEC_GET_MASK_Z(), SMATH_VEC_REPLICATE_Z(maxMask));
	}
	else if (SMATH_VEC_MOVEMASK(SMATH_VEC_AND(maxMask, SMATH_VEC_GET_MASK_YZ())) == 0x02)
	{
		plane1Mask = SMATH_VEC_GET_MASK_Y();
		plane2Mask = SMATH_VEC_SEL(SMATH_VEC_GET_MASK_Z(), SMATH_VEC_GET_MASK_X(), SMATH_VEC_REPLICATE_X(maxMask));
	}
	else
	{
		plane1Mask = SMATH_VEC_GET_MASK_Z();
		plane2Mask = SMATH_VEC_SEL(SMATH_VEC_GET_MASK_X(), SMATH_VEC_GET_MASK_Y(), SMATH_VEC_REPLICATE_Y(maxMask));
	}

	v128 signs = SMATH_VEC_AND(SMATH_VEC_GET_MASK_SIGN(), ptBeadLocal.QuadwordValue());
	v128 ones = SMATH_VEC_OR(SMATH_VEC_REPLICATE_FLOAT(1.0f), signs);
	v128 points = SMATH_VEC_OR(halfSize.QuadwordValue(), signs);

	v128 plane1Norm = SMATH_VEC_AND(ones, plane1Mask);
	v128 plane1Point = SMATH_VEC_AND(points, plane1Mask);
	Vector vcPlane1Norm = boxLoc.TransformVector(Vector(plane1Norm));
	Point ptPlane1Point = boxLoc.TransformPoint(Point(plane1Point));
	Scalar scPlane1D = -Dot(ptPlane1Point - kOrigin, vcPlane1Norm);
	Vec4 plane1 = vcPlane1Norm.GetVec4();
	plane1.SetW(scPlane1D);

	bool bInContact = false;

	I32 iPlane0 = -1;
	if (CheckConstraintPlane(iPoint, plane1))
	{
		iPlane0 = m_pConstr[iPoint].AddPlane(plane1, hCollider);
		bInContact = true;
	}

	if (SMATH_VEC_ALL_COMPONENTS_GE(SMATH_VEC_AND(dist, plane2Mask), negRadius))
	{
		v128 plane2Norm = SMATH_VEC_AND(ones, plane2Mask);
		v128 plane2Point = SMATH_VEC_AND(points, plane2Mask);
		Vector vcPlane2Norm = boxLoc.TransformVector(Vector(plane2Norm));
		Point ptPlane2Point = boxLoc.TransformPoint(Point(plane2Point));
		Scalar scPlane2D = -Dot(ptPlane2Point - kOrigin, vcPlane2Norm);
		Vec4 plane2 = vcPlane2Norm.GetVec4();
		plane2.SetW(scPlane2D);
		if (CheckConstraintPlane(iPoint, plane2))
		{
			I32 iPlane1 = m_pConstr[iPoint].AddPlane(plane2, hCollider);
			if (iPlane0 >= 0 & iPlane1 >= 0)
				m_pConstr[iPoint].AddEdge(iPlane0, iPlane1);
			bInContact = true;
		}
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#if ROPE_DEBUG_DRAW
	if (bInContact && g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		Color color = bInContact ? kColorRed : kColorGray;
		g_prim.Draw(DebugBox(boxLoc.AsTransform(), 2.0f*halfSize, color, PrimAttrib(kPrimEnableWireframe)), kPrimDuration1FramePauseable);
	}
#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

void Rope2::CollideWithCapsule(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const ExternalBitArray* pTestPoints)
{
	PROFILE(Havok, CollideWithCapsule);

	//if (m_bSleeping)
	//{
	//	WakeUp();
	//	MsgHavok("Rope 0x%08X (%s) woke up by capsule contact\n", (uintptr_t)this, m_hOwner.ToProcess() ? m_hOwner.ToProcess()->GetName() : "null");
	//}

	bool isChar = isCharacterBody(hCollider);

	const hknpCapsuleShape* pCapsule = static_cast<const hknpCapsuleShape*>(pCollider->m_pShape);
	
	Locator loc = pCollider->m_loc;
	Point vecVtxA = loc.TransformPoint(Point(pCapsule->getPointA().getQuad()));
	Point vecVtxB = loc.TransformPoint(Point(pCapsule->getPointB().getQuad()));
	Scalar scRadius(pCapsule->getRadius());

	Vector	vecA2B			(vecVtxB - vecVtxA);

	Scalar	scHeightSqr		(LengthSqr(vecA2B));
	Scalar	scHeightInv		(RecipSqrt(scHeightSqr));
	VB32	isHeightTiny	(CompareLE(scHeightSqr, kTiny));
	Scalar	scHeight		(Select(scHeightSqr*scHeightInv,	Scalar(SMath::kZero), isHeightTiny));
	Vector	vecUnitDir		(Select(vecA2B*scHeightInv,			Vector(SMath::kZero), isHeightTiny));
	Scalar	scHalfHeight	(kOneHalf*scHeight);

	Vector	vecHalfA2B		(kOneHalf*vecA2B);
	Point	ptCenter		(vecVtxA + vecHalfA2B);

	Locator rayLoc(pCollider->m_locPrev.GetTranslation(), loc.GetRotation()); // previous position + current rotation
	Vector shapeMove(loc.GetTranslation() - pCollider->m_locPrev.GetTranslation());
	Vector shapeMoveLoc = loc.UntransformVector(shapeMove);

	hknpInplaceTriangleShape targetTriangle( 0.0f );
	hknpCollisionQueryContext queryContext( nullptr, targetTriangle.getTriangleShape() );

	hknpRayCastQuery query;
					
	hknpQueryFilterData filterData;
	HavokClosestHitCollector collector;

	hknpShapeQueryInfo queryInfo;
	queryInfo.m_rootShape = nullptr;
	hkTransform shapeXfm; shapeXfm.setIdentity();
	queryInfo.m_shapeToWorld = &shapeXfm;

	bool bAnyInContact = false;

	if (!pTestPoints)
	{
		PHYSICS_ASSERT(m_lastDynamicPoint < 1024);
		pTestPoints = &s_allSetBitArray1024;
	}

	U32F numPointsRel = m_lastDynamicPoint - m_firstDynamicPoint + 1;
	for (I32F iBeadRel = pTestPoints->FindFirstSetBit(); iBeadRel < numPointsRel; iBeadRel = pTestPoints->FindNextSetBit(iBeadRel))
	{
		I32F iBead = m_firstDynamicPoint + iBeadRel;

		// Strained sections of rope do not collide with characters
		if (IsNodeStrainedInt(iBead) || IsNodeKeyframedInt(iBead) || (isChar && m_pRopeDist[iBead] < m_minCharCollisionDist) || m_pRadius[iBead] == 0.0f)
			continue;

		if (m_pConstr[iBead].HasCollider(hCollider))
			continue;

		Scalar	scContactRadius	(m_pRadius[iBead]);
		Scalar	scShell			(scContactRadius * 1.5f + 0.03f);

		Point ptBead(m_pPos[iBead]);
		{
			Point rayFrom = rayLoc.UntransformPoint(m_pLastPos[iBead]);
			Point rayTo = rayLoc.UntransformPoint(m_pPos[iBead] - shapeMove);

			query.m_ray.setEndPoints(hkVector4(rayFrom.QuadwordValue()), hkVector4(rayTo.QuadwordValue()));

			collector.reset();
			pCapsule->castRayImpl( &queryContext, query, filterData, queryInfo, &collector);
			if (collector.hasHit())
			{
				ptBead = m_pLastPos[iBead] + collector.getHit().m_fraction * (m_pPos[iBead] - shapeMove - m_pLastPos[iBead]) + shapeMove;
			}
		}

		Vector	vecCenter2Bead		(ptBead - ptCenter);

		// projection of vecCenter2Bead onto capsule's axis, clamped to the capsule's core line segment
		Scalar	scCore				(Min(Max(Dot(vecCenter2Bead, vecUnitDir), -scHalfHeight), scHalfHeight));
		// vector from center of capsule to the projected point along the core line segment
		Vector	vecCenter2Core		(scCore * vecUnitDir);
		// vector from projected core point to bead -- this vector has the awesome property of being
		// at right angles to the axis of the capsule as long as the projected point lies *within*
		// the capsule's line segment; otherwise it is the vector from the center of one of the
		// spheres to the bead, in which case things degenerate into a simple sphere collision
		Vector	vecCore2Bead		(vecCenter2Bead - vecCenter2Core);

		Scalar	scDistFromCoreSqr	(LengthSqr(vecCore2Bead));
		Scalar	scDistFromCoreInv	(RecipSqrt(scDistFromCoreSqr));
		Simd::Mask isDistFromCoreTiny	(CompareLE(scDistFromCoreSqr, kTiny));
		Scalar	scDistFromCore		(Select(scDistFromCoreSqr*scDistFromCoreInv, Scalar(SMath::kZero), isDistFromCoreTiny));

		// safe normalize of dist from core, which becomes our contact plane's normal
		Vector			vecPlaneNorm		(scDistFromCoreInv*vecCore2Bead);
		SMath::Vec4		plane				(Select(vecPlaneNorm.GetVec4(), SMath::Vec4(SMath::kZero), isDistFromCoreTiny));
		ASSERT(plane.W() == 0.0f);

		Point	ptCore				(ptCenter + vecCenter2Core);
		Point	ptSurface			(ptCore + scRadius*vecPlaneNorm);
		Scalar	scPlaneD			(-Dot3(ptSurface.GetVec4(), plane));

		plane.SetW(scPlaneD);

		Scalar	scDistFromCapsule	(scDistFromCore - scRadius);

		if (scDistFromCapsule < scShell && CheckConstraintPlane(iBead, plane))
		{
			m_pConstr[iBead].AddPlane(plane, hCollider);

			bAnyInContact = true;
		}
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	#if ROPE_DEBUG_DRAW
	if (g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		// Build a transformation matrix from capsule's actual space into a canonical space in which the capsule is vertical.
		Vector j(vecUnitDir);
		Vector i, k;
		static F32 kDotThresh = 0.01f;
		if ((F32)Abs(Dot(j, kUnitXAxis)) <= 1.0f - kDotThresh)
		{
			k = Normalize(Cross(kUnitXAxis, j));
			i = Normalize(Cross(j, k));
		}
		else
		{
			i = Normalize(Cross(j, kUnitZAxis));
			k = Normalize(Cross(i, j));
		}
		Transform xfmOrient(i, j, k, ptCenter);

		Vector vecScale(scRadius, scHalfHeight, scRadius);
		Transform xfmScale;
		xfmScale.SetScale(vecScale);

		Transform xfm(xfmScale * xfmOrient);

		Color color = bAnyInContact ? kColorRed : kColorGray;

		g_prim.Draw(DebugPrimShape(xfm, DebugPrimShape::kCylinder, color, PrimAttrib(kPrimEnableWireframe)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugSphere(Sphere(vecVtxA, scRadius), color, PrimAttrib(kPrimEnableWireframe)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugSphere(Sphere(vecVtxB, scRadius), color, PrimAttrib(kPrimEnableWireframe)), kPrimDuration1FramePauseable);
	}
	#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

void Rope2::CollidePointWithCapsule(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider)
{
	//if (m_bSleeping)
	//{
	//	WakeUp();
	//	MsgHavok("Rope 0x%08X (%s) woke up by capsule contact\n", (uintptr_t)this, m_hOwner.ToProcess() ? m_hOwner.ToProcess()->GetName() : "null");
	//}

	const hknpCapsuleShape* pCapsule = static_cast<const hknpCapsuleShape*>(pCollider->m_pShape);

	Locator loc = pCollider->m_loc;
	Point vecVtxA = loc.TransformPoint(Point(pCapsule->getPointA().getQuad()));
	Point vecVtxB = loc.TransformPoint(Point(pCapsule->getPointB().getQuad()));
	Scalar scRadius(pCapsule->getRadius());

	Vector	vecA2B			(vecVtxB - vecVtxA);

	Scalar	scHeightSqr		(LengthSqr(vecA2B));
	Scalar	scHeightInv		(RecipSqrt(scHeightSqr));
	VB32	isHeightTiny	(CompareLE(scHeightSqr, kTiny));
	Scalar	scHeight		(Select(scHeightSqr*scHeightInv,	Scalar(SMath::kZero), isHeightTiny));
	Vector	vecUnitDir		(Select(vecA2B*scHeightInv,			Vector(SMath::kZero), isHeightTiny));
	Scalar	scHalfHeight	(kOneHalf*scHeight);

	Vector	vecHalfA2B		(kOneHalf*vecA2B);
	Point	ptCenter		(vecVtxA + vecHalfA2B);

	Point	ptBead				(refPos);
	Vector	vecCenter2Bead		(ptBead - ptCenter);

	// projection of vecCenter2Bead onto capsule's axis, clamped to the capsule's core line segment
	Scalar	scCore				(Min(Max(Dot(vecCenter2Bead, vecUnitDir), -scHalfHeight), scHalfHeight));
	// vector from center of capsule to the projected point along the core line segment
	Vector	vecCenter2Core		(scCore * vecUnitDir);
	// vector from projected core point to bead -- this vector has the awesome property of being
	// at right angles to the axis of the capsule as long as the projected point lies *within*
	// the capsule's line segment; otherwise it is the vector from the center of one of the
	// spheres to the bead, in which case things degenerate into a simple sphere collision
	Vector	vecCore2Bead		(vecCenter2Bead - vecCenter2Core);

	Scalar	scDistFromCoreSqr	(LengthSqr(vecCore2Bead));
	Scalar	scDistFromCoreInv	(RecipSqrt(scDistFromCoreSqr));
	Simd::Mask isDistFromCoreTiny	(CompareLE(scDistFromCoreSqr, kTiny));
	Scalar	scDistFromCore		(Select(scDistFromCoreSqr*scDistFromCoreInv, Scalar(SMath::kZero), isDistFromCoreTiny));

	// safe normalize of dist from core, which becomes our contact plane's normal
	Vector			vecPlaneNorm		(scDistFromCoreInv*vecCore2Bead);
	SMath::Vec4		plane				(Select(vecPlaneNorm.GetVec4(), SMath::Vec4(SMath::kZero), isDistFromCoreTiny));
	ASSERT(plane.W() == 0.0f);

	Point	ptCore				(ptCenter + vecCenter2Core);
	Point	ptSurface			(ptCore + scRadius*vecPlaneNorm);
	Scalar	scPlaneD			(-Dot3(ptSurface.GetVec4(), plane));

	plane.SetW(scPlaneD);

	bool bInContact = false;

	if (CheckConstraintPlane(iPoint, plane))
	{
		m_pConstr[iPoint].AddPlane(plane, hCollider);
		bInContact = true;
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#if ROPE_DEBUG_DRAW
	if (bInContact && g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		// Build a transformation matrix from capsule's actual space into a canonical space in which the capsule is vertical.
		Vector j(vecUnitDir);
		Vector i, k;
		static F32 kDotThresh = 0.01f;
		if ((F32)Abs(Dot(j, kUnitXAxis)) <= 1.0f - kDotThresh)
		{
			k = Normalize(Cross(kUnitXAxis, j));
			i = Normalize(Cross(j, k));
		}
		else
		{
			i = Normalize(Cross(j, kUnitZAxis));
			k = Normalize(Cross(i, j));
		}
		Transform xfmOrient(i, j, k, ptCenter);

		Vector vecScale(scRadius, scHalfHeight, scRadius);
		Transform xfmScale;
		xfmScale.SetScale(vecScale);

		Transform xfm(xfmScale * xfmOrient);

		Color color = bInContact ? kColorRed : kColorGray;

		g_prim.Draw(DebugPrimShape(xfm, DebugPrimShape::kCylinder, color, PrimAttrib(kPrimEnableWireframe)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugSphere(Sphere(vecVtxA, scRadius), color, PrimAttrib(kPrimEnableWireframe)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugSphere(Sphere(vecVtxB, scRadius), color, PrimAttrib(kPrimEnableWireframe)), kPrimDuration1FramePauseable);
	}
#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

void Rope2::CollideWithTriangle(const RopeCollider* pCollider, const RopeColliderHandle& hCollider)
{
	const hknpTriangleShape* pTriangle = static_cast<const hknpTriangleShape*>(pCollider->m_pShape);

	Locator loc = pCollider->m_loc;
	Point pt0 = loc.TransformPoint(Point(pTriangle->getVertices()[0].getQuad()));
	Point pt1 = loc.TransformPoint(Point(pTriangle->getVertices()[1].getQuad()));
	Point pt2 = loc.TransformPoint(Point(pTriangle->getVertices()[2].getQuad()));

	CollideWithTransformedTriangle(pCollider, hCollider, pt0, pt1, pt2);
}

void Rope2::CollideWithTransformedTriangle(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, Point_arg pt0, Point_arg pt1, Point_arg pt2, const ExternalBitArray* pTestPoints)
{
	bool isChar = isCharacterBody(hCollider);

	Locator loc = pCollider->m_loc;

	Scalar scZero					(0.0f);
	Scalar scThreshold				(-m_fRadius - 1e-3f);
	Scalar scThreshold2				(m_fRadius + 1e-2f);
	Scalar scThresholdSqr			(scThreshold * scThreshold);
	Scalar scRadius					(m_fRadius);

	Scalar vecEdge0Len, vecEdge1Len, vecEdge2Len;
	Vector vecEdge0					(SafeNormalize(pt1 - pt0, kUnitXAxis, vecEdge0Len));
	Vector vecEdge1					(SafeNormalize(pt2 - pt1, kUnitXAxis, vecEdge1Len));
	Vector vecEdge2					(SafeNormalize(pt0 - pt2, kUnitXAxis, vecEdge2Len));
	Vector vecNorm					(SafeNormalize(Cross(vecEdge0, -vecEdge2), kUnitYAxis));
	Vector vecBiEdge0				(Cross(vecNorm, vecEdge0));
	Vector vecBiEdge1				(Cross(vecNorm, vecEdge1));
	Vector vecBiEdge2				(Cross(vecNorm, vecEdge2));

	Scalar scPlaneD					(-Dot(vecNorm, pt0 - kOrigin));
	SMath::Vec4 plane				(SMATH_VEC_SEL(scPlaneD.QuadwordValue(), vecNorm.QuadwordValue(), SMATH_VEC_CAST_TO_UINT(SMATH_VEC_GET_MASKOFF_W())));

	Vector vecOffset				= scRadius * vecNorm;

	bool bAnyInContact = false;

	if (!pTestPoints)
	{
		PHYSICS_ASSERT(m_lastDynamicPoint < 1024);
		pTestPoints = &s_allSetBitArray1024;
	}

	U32F numPointsRel = m_lastDynamicPoint - m_firstDynamicPoint + 1;
	for (I32F iBeadRel = pTestPoints->FindFirstSetBit(); iBeadRel < numPointsRel; iBeadRel = pTestPoints->FindNextSetBit(iBeadRel))
	{
		I32F iBead = m_firstDynamicPoint + iBeadRel;

		// Strained sections of rope do not collide with characters
		if (IsNodeStrainedInt(iBead) || IsNodeKeyframedInt(iBead) || (isChar && m_pRopeDist[iBead] < m_minCharCollisionDist))
			continue;

		// if we're close to the plane (taking into account our motion along the plane normal from the last frame,
		// as well as our connection to the previous point), then we should check if the bead is "inside" the
		// triangle and possibly add a constraint

		Point ptBead			(m_pPos[iBead]);
		Scalar scAbovePlane		(Dot4(plane, ptBead.GetVec4()));

		{
			// Raycast (lastPos, pos) against the triangle
			Scalar scLastAbovePlane(Dot4(plane, m_pLastPos[iBead].GetVec4()));
			Scalar d = scLastAbovePlane - scAbovePlane;
			Scalar f = scLastAbovePlane / d;
			if ((d != Scalar(kZero)) & (f > 0.0f) & (f < 1.0f))
			{
				Point pI = Lerp(m_pLastPos[iBead], m_pPos[iBead], f);
				Vector c(
					Dot(vecBiEdge0, pI - pt0),
					Dot(vecBiEdge1, pI - pt1),
					Dot(vecBiEdge2, pI - pt2)
				);
				if (AllComponentsGreaterThanOrEqual(c, kZero) & CheckConstraintPlane(iBead, plane))
				{
					// Hit! Move the current pos
					m_pPos[iBead] = pI;

					m_pConstr[iBead].AddPlane(plane, hCollider);
					bAnyInContact = true;

					continue;
				}
			}
		}
		
		// Use either vector from last pos or pull towards neighboring nodes which ever moves more into the plane
		Vector vecBeadVel		(ptBead - m_pLastPos[iBead]);
		Vector vecPrevPullVel1	(m_pPos[Max(0, iBead-1)] - ptBead);
		Vector vecPrevPullVel2	(m_pPos[Min((I32F)m_numPoints-1, iBead+1)] - ptBead);
		Scalar scBeadProj1		(Dot(vecNorm, vecBeadVel));
		Scalar scBeadProj2		(Dot(vecNorm, vecPrevPullVel1));
		Scalar scBeadProj3		(Dot(vecNorm, vecPrevPullVel2));
		Scalar scBeadProj		(Min(scBeadProj1, Min(scBeadProj2, scBeadProj3))); 

		Scalar scMaxAbove		(scThreshold2 - Min(scZero, scBeadProj));

		bool bAppend			= (SMATH_VEC_ALL_COMPONENTS_LT(SMATH_VEC_ABS(scAbovePlane.QuadwordValue()), scMaxAbove.QuadwordValue()));

		if (bAppend)
		{
			Vector cNow(
				Dot(vecBiEdge0, ptBead - pt0),
				Dot(vecBiEdge1, ptBead - pt1),
				Dot(vecBiEdge2, ptBead - pt2)
			);

			Vector cLast(
				Dot(vecBiEdge0, m_pLastPos[iBead] - pt0),
				Dot(vecBiEdge1, m_pLastPos[iBead] - pt1),
				Dot(vecBiEdge2, m_pLastPos[iBead] - pt2)
			);

			Vector c = Max(cNow, cLast);

			bool isInside		= SMATH_VEC_ALL_COMPONENTS_GE(c.QuadwordValue(), scThreshold.QuadwordValue());

			if (isInside)
			{
				Point ptBeadOnPlane = ptBead - scAbovePlane * vecNorm;

				bool isInsideForReal = true;
				U32F edgeTestMask = (SMATH_VEC_MOVEMASK(SMATH_VEC_CMPLT(c.QuadwordValue(), scZero.QuadwordValue()))) & 0x7;
				if (edgeTestMask != 0)
				{
					// Well, we are not really inside the triangle. We are inside the triangle that has been
					// grown outwards by moving the edges perpendicular outside by rope radius.
					// But if the angle at a vertex is sharp and we collide with that vertex of a triangle we could be out of triangle by a lot
					// actually and we would be getting false positives.
					// Bellow we test if we are colliding with a vertex and in that case test for distance from the vertex
					// @@JS: This piece of code is definitely sub-optimal

					if (edgeTestMask & 0x1)
					{
						Scalar d = Dot(ptBead - pt0, vecEdge0);
						if (d < 0.0f)
							isInsideForReal = DistSqr(pt0, ptBeadOnPlane) < scThresholdSqr;
						else if (d > vecEdge0Len)
							isInsideForReal = DistSqr(pt1, ptBeadOnPlane) < scThresholdSqr;
					}
					else if (edgeTestMask & 0x2)
					{
						Scalar d = Dot(ptBead - pt1, vecEdge1);
						if (d < 0.0f)
							isInsideForReal = DistSqr(pt1, ptBeadOnPlane) < scThresholdSqr;
						else if (d > vecEdge1Len)
							isInsideForReal = DistSqr(pt2, ptBeadOnPlane) < scThresholdSqr;
					}
					else if (edgeTestMask & 0x4)
					{
						Scalar d = Dot(ptBead - pt2, vecEdge2);
						if (d < 0.0f)
							isInsideForReal = DistSqr(pt2, ptBeadOnPlane) < scThresholdSqr;
						else if (d > vecEdge2Len)
							isInsideForReal = DistSqr(pt0, ptBeadOnPlane) < scThresholdSqr;
					}
				}
					
				if (isInsideForReal && CheckConstraintPlane(iBead, plane))
				{
					m_pConstr[iBead].AddPlane(plane, hCollider);
					bAnyInContact = true;
				}
			}
		}
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#if ROPE_DEBUG_DRAW
	if (g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		Color color = bAnyInContact ? kColorRed : kColorGray;
		g_prim.Draw(DebugLine(pt0, pt1, color, 1.0f, PrimAttrib()), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(pt1, pt2, color, 1.0f, PrimAttrib()), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(pt2, pt0, color, 1.0f, PrimAttrib()), kPrimDuration1FramePauseable);
	}
#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

bool Rope2::CollidePointWithConvexInner(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const hknpConvexPolytopeShape* pConvex, F32 distTol)
{
	Locator loc = pCollider->m_loc;

	bool bAnyInContact = false;

	Point ptBeadLocal = loc.UntransformPoint(refPos);

	const hkcdVertex* pVertices = pConvex->getVertices().begin();
	hkcdVertex beadVtxLoc; beadVtxLoc.assign(hkVector4(ptBeadLocal.QuadwordValue()), 0);
	hkcdGsk::GetClosestPointInput   input; input.m_bTa.setIdentity();
	hkcdGsk::Cache                  cache; cache.init();
	hkcdGsk::GetClosestPointOutput  output;
	hkcdGsk::GetClosestPointStatus  status = hkcdGsk::getClosestPoint(pVertices, pConvex->getVertices().getSize(), &beadVtxLoc, 1, input, &cache, output);
	if ((F32)output.getDistance() <= distTol+pConvex->m_convexRadius)
	{
		// Supporting face. Basically the face that the point is closest to.
		hkVector4 planeLoc;
		hknpAngularTimType minAngle;
		I32 faceId = pConvex->getSupportingFace(output.m_normalInA, beadVtxLoc, &cache, false, -1, planeLoc, minAngle);

		// Test all edges of the face and find the one that we're close to within radius
		// because if there is one like that we will want to add the adjacent face too
		const hknpConvexPolytopeShape::Face& face = pConvex->getFaces()[faceId];
		const hkRelArray<hknpConvexShape::VertexIndex>& vtxIndices = pConvex->getFaceVertexIndices();
		F32 maxEdgeDist = -m_fRadius-distTol+pConvex->m_convexRadius;
		I32 maxEdgeDistIndex = -1;
		hkVector4 maxEdgeDir;
		hkVector4 maxEdgeBiDir = hkVector4::getZero();
		hkVector4 maxBeadEdgeVec;
		for (I32 iEdge = 0; iEdge < face.m_numIndices; iEdge++)
		{
			hkVector4 vtxA = pVertices[vtxIndices[face.m_firstIndex + iEdge]];
			hkVector4 vtxB = pVertices[vtxIndices[face.m_firstIndex + (iEdge + 1) % face.m_numIndices]];
			hkVector4 edgeDir; edgeDir.setSub(vtxB, vtxA); edgeDir.normalize3IfNotZero();
			hkVector4 edgeBiDir; edgeBiDir.setCross(edgeDir, planeLoc);
			hkVector4 beadEdgeVec; beadEdgeVec.setSub(beadVtxLoc, vtxA);
			F32 edgeDist = edgeBiDir.dot3(beadEdgeVec);
			if (edgeDist > maxEdgeDist)
			{
				maxEdgeDist = edgeDist;
				maxEdgeDistIndex = face.m_firstIndex + iEdge;
				maxEdgeDir = edgeDir;
				maxEdgeBiDir = edgeBiDir;
				maxBeadEdgeVec = beadEdgeVec;
			}
		}

		// Find plane point that is inside the face
		beadVtxLoc.setW(1.0f);
		F32 planeDist =	(F32)planeLoc.dot4(beadVtxLoc) - pConvex->m_convexRadius;
		hkVector4 planePntLoc; planePntLoc.setAddMul(beadVtxLoc, planeLoc, -planeDist);
		planePntLoc.addMul(maxEdgeBiDir, -maxEdgeDist);

		// Transform to world and add constraint
		Vector planeNorm = loc.TransformVector(Vector(planeLoc.getQuad()));
		Point planePnt = loc.TransformPoint(Point(planePntLoc.getQuad()));
		Vec4 plane = planeNorm.GetVec4();
		plane.SetW(Dot(Point(kOrigin) - planePnt, planeNorm));
		I32 iPlane0 = -1;
		if (CheckConstraintPlane(iPoint, plane))
		{
			iPlane0 = m_pConstr[iPoint].AddPlane(plane, hCollider);
			bAnyInContact = true;
		}

		if (maxEdgeDistIndex >= 0)
		{
			// If we are close to an edge, add the adjacent face too
			I32 otherFaceId = pConvex->getConnectivity()->m_faceLinks[maxEdgeDistIndex].m_faceIndex;
			hkVector4 plane2Loc = pConvex->getPlanes()[otherFaceId];
			hkVector4 edgeBiDir; edgeBiDir.setCross(plane2Loc, maxEdgeDir);
			F32 edgeDist = edgeBiDir.dot3(maxBeadEdgeVec);

			F32 plane2Dist = (F32)plane2Loc.dot4(beadVtxLoc) - pConvex->m_convexRadius;
			hkVector4 plane2PntLoc; plane2PntLoc.setAddMul(beadVtxLoc, plane2Loc, -plane2Dist);
			plane2PntLoc.addMul(edgeBiDir, -edgeDist);

			Vector plane2Norm = loc.TransformVector(Vector(plane2Loc.getQuad()));
			Point plane2Pnt = loc.TransformPoint(Point(plane2PntLoc.getQuad()));
			Vec4 plane2 = plane2Norm.GetVec4();
			plane2.SetW(Dot(Point(kOrigin) - plane2Pnt, plane2Norm));
			if (CheckConstraintPlane(iPoint, plane2))
			{
				I32 iPlane1 = m_pConstr[iPoint].AddPlane(plane2, hCollider);
				if (iPlane0 >= 0 & iPlane1 >= 0)
				{
					m_pConstr[iPoint].AddEdge(iPlane0, iPlane1);
				}
				bAnyInContact = true;
			}
		}
	}

	return bAnyInContact;
}

void Rope2::CollideWithConvex(const RopeCollider* pCollider,
							  const RopeColliderHandle& hCollider,
							  const hknpConvexPolytopeShape* pConvex,
							  const ExternalBitArray* pTestPoints)
{
	PROFILE(Havok, CollideWithConvex);

	PHYSICS_ASSERT(pConvex->getConnectivity());

	bool isChar = isCharacterBody(hCollider);

	Locator loc = pCollider->m_loc;

	F32 distTol = m_fRadius + 2e-2f;

	Locator rayLoc(pCollider->m_locPrev.GetTranslation(), loc.GetRotation()); // previous position + current rotation
	Vector shapeMove(loc.GetTranslation() - pCollider->m_locPrev.GetTranslation());

	hkAabb _aabb;
	pConvex->calcAabb(hkTransform::getIdentity(), _aabb);
	Aabb aabb;
	aabb.m_min = Point(_aabb.m_min.getQuad());
	aabb.m_max = Point(_aabb.m_max.getQuad());
	aabb.Expand(distTol+pConvex->m_convexRadius);

	bool bAnyInContact = false;

	hknpInplaceTriangleShape targetTriangle(0.0f);
	hknpCollisionQueryContext queryContext(nullptr, targetTriangle.getTriangleShape());

	hknpRayCastQuery query;

	hknpQueryFilterData filterData;
	HavokClosestHitCollector collector;

	hknpShapeQueryInfo queryInfo;
	queryInfo.m_rootShape = nullptr;
	hkTransform shapeXfm; shapeXfm.setIdentity();
	queryInfo.m_shapeToWorld = &shapeXfm;

	if (!pTestPoints)
	{
		PHYSICS_ASSERT(m_lastDynamicPoint < 1024);
		pTestPoints = &s_allSetBitArray1024;
	}

	U32F numPointsRel = m_lastDynamicPoint - m_firstDynamicPoint + 1;
	for (I32F iBeadRel = pTestPoints->FindFirstSetBit(); iBeadRel < numPointsRel; iBeadRel = pTestPoints->FindNextSetBit(iBeadRel))
	{
		I32F ii = m_firstDynamicPoint + iBeadRel;

		// Strained sections of rope do not collide with characters
		if (IsNodeStrainedInt(ii) || IsNodeKeyframedInt(ii) || (isChar && m_pRopeDist[ii] < m_minCharCollisionDist))
			continue;

		if (m_pConstr[ii].HasCollider(hCollider))
			continue;

		Point ptBead(m_pPos[ii]);
		{
			Point rayFrom = rayLoc.UntransformPoint(m_pLastPos[ii]);
			Point rayTo = rayLoc.UntransformPoint(m_pPos[ii] - shapeMove);

			// Early out AABB test
			Scalar tMin, tMax;
			if (!aabb.IntersectSegment(rayFrom, rayTo, tMin, tMax))
				continue;

			query.m_ray.setEndPoints(hkVector4(rayFrom.QuadwordValue()), hkVector4(rayTo.QuadwordValue()));

			collector.reset();
			pConvex->castRayImpl(&queryContext, query, filterData, queryInfo, &collector);
			if (collector.hasHit() && collector.getHit().m_fraction <= 1.0f)
			{
				ptBead = m_pLastPos[ii] + collector.getHit().m_fraction * (m_pPos[ii] - shapeMove - m_pLastPos[ii]) + shapeMove;
			}
		}

		if (CollidePointWithConvexInner(ii, ptBead, pCollider, hCollider, pConvex, distTol))
		{
			bAnyInContact = true;
		}
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#if ROPE_DEBUG_DRAW
	if (g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		Color color = bAnyInContact ? kColorRed : kColorGray;
		HavokDebugDrawShape(pConvex, loc, color, CollisionDebugDrawConfig::MenuOptions(), nullptr, kPrimDuration1FramePauseable);
	}
#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

void Rope2::CollidePointWithConvex(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const hknpConvexPolytopeShape* pConvex)
{
	PHYSICS_ASSERT(pConvex->getConnectivity());
	bool bAnyInContact = CollidePointWithConvexInner(iPoint, refPos, pCollider, hCollider, pConvex, 1000.0f);

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#if ROPE_DEBUG_DRAW
	if (bAnyInContact && g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		Color color = bAnyInContact ? kColorRed : kColorGray;
		HavokDebugDrawShape(pConvex, pCollider->m_loc, color, CollisionDebugDrawConfig::MenuOptions(), nullptr, kPrimDuration1FramePauseable);
	}
#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

void Rope2::CollidePointWithCollider(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider)
{
	// Strained sections of rope do not collide with characters
	if (isCharacterBody(hCollider) && (m_pNodeFlags[iPoint] & kNodeStrained))
		return;

	switch (pCollider->m_pShape->getType())
	{
	case hknpShapeType::TRIANGLE:
		// @@ ToDo
		break;

	case hknpShapeType::SPHERE:
		// @@ ToDo
		break;

	case hknpShapeType::BOX:
		CollidePointWithBox(iPoint, refPos, pCollider, hCollider);
		break;

	case hknpShapeType::CAPSULE:
		CollidePointWithCapsule(iPoint, refPos, pCollider, hCollider);
		break;

	case hknpShapeType::USER_0:
		CollidePointWithPlane(iPoint, refPos, pCollider, hCollider);
		break;

	default:
		if (const hknpConvexPolytopeShape* pConvex = pCollider->m_pShape->asConvexPolytopeShape())
		{
			CollidePointWithConvex(iPoint, refPos, pCollider, hCollider, pConvex);
		}
	}
}

bool Rope2::CheckConstraintPlane(U32F iPoint, Vec4_arg plane)
{
	// Here we check that the distance between plane and previous/next keyframed point is not bigger than the rope distance
	// If it is we consider the constraint plane invalid because it would necessary cause a stretching of the rope

	if (m_pDistConstraints && !m_bAllowDistStretchConstraints)
	{
		if (m_pDistConstraints[iPoint].W() < 1000.0f)
		{
			Scalar planeDist = -Dot4(Point(m_pDistConstraints[iPoint]), plane);

			// We add the keyframed rope move corresponding to this point
			// We arealdy added it once to the dist constraint but there are cases where the collision plane
			// rejection needs some more tolerance to avoid discarding valid collision planes
			
			F32 addDist = 0.0f;
			I32F iPrevKey = m_pPrevKeyIndex[iPoint];
			I32F iNextKey = m_pNextKeyIndex[iPoint];
			if (iPrevKey >= 0 && iNextKey >= 0)
			{
				F32 s = m_pKeyRopeDist[iNextKey] - m_pKeyRopeDist[iPrevKey];
				F32 s1 = m_pRopeDist[iPoint] - m_pKeyRopeDist[iPrevKey];
				F32 startMoveDist = m_pKeyMoveDist[iPrevKey];
				F32 endMoveDist = m_pKeyMoveDist[iNextKey];
				addDist = Lerp(startMoveDist, endMoveDist, s1/s);
			}

			if (m_pDistConstraints[iPoint].W() + addDist < planeDist)
			{
				return false;
			}

			// If dist constraint is doing something which means we have edge detection line
			// we will just ignore key stretch check bellow because it's common that the rope stretches a bit when fully pulled
			return true;
		}
	}

	if (!m_bAllowKeyStretchConstraints)
	{
		PHYSICS_ASSERT(m_pPrevKeyIndex && m_pNextKeyIndex);

		I32F iPrevKey = m_pPrevKeyIndex[iPoint];
		if (iPrevKey >= 0)
		{
			Scalar planeDist = -Dot4(m_pKeyPos[iPrevKey], plane);
			if (Scalar(m_pRopeDist[iPoint] - m_pKeyRopeDist[iPrevKey] + m_pKeyMoveDist[iPrevKey]) < planeDist)
			{
				return false;
			}
		}

		I32F iNextKey = m_pNextKeyIndex[iPoint];
		if (iNextKey >= 0)
		{
			Scalar planeDist = -Dot4(m_pKeyPos[iNextKey], plane);
			if (Scalar(m_pKeyRopeDist[iNextKey] - m_pRopeDist[iPoint] + m_pKeyMoveDist[iNextKey]) < planeDist)
			{
				return false;
			}
		}
	}

	return true;
}

struct ColCacheTreeTraverseData
{
	Rope2* m_pRope;
	const RopeColTreeNode* m_pTreeNodes;
	I32F m_lastShapeIndex;
	RopeCollider m_lastCollider;
};

bool Rope2::ColCacheTreeCollideCallback(U16 index, const ExternalBitArray* pBitArray, void* pUserData)
{
	ColCacheTreeTraverseData* pData = reinterpret_cast<ColCacheTreeTraverseData*>(pUserData);
	const RopeColTreeNode& treeNode = pData->m_pTreeNodes[index];
	const RopeColCache& colCache = pData->m_pRope->m_colCache;
	for (U32F ii = 0; ii<treeNode.m_numElems; ii++)
	{
		U32 triIndex = treeNode.m_elemIndex + ii;
		const RopeColTri& tri = colCache.m_pTris[triIndex];
		const RopeColliderHandle& shape = colCache.m_pShapes[tri.m_shapeIndex];
		if (pData->m_lastShapeIndex != tri.m_shapeIndex)
		{
			PHYSICS_ASSERT(shape.IsRigidBody());
			shape.GetCollider(&pData->m_lastCollider, pData->m_pRope);
			pData->m_lastShapeIndex = tri.m_shapeIndex;
		}
		if (tri.m_key.isValid())
		{
			pData->m_pRope->CollideWithTransformedTriangle2(treeNode.m_elemIndex + ii, shape, tri.m_pnt[0], tri.m_pnt[1], tri.m_pnt[2], pBitArray);
		}
		else
		{
			pData->m_pRope->CollideWithColCacheCollider(&pData->m_lastCollider, shape, triIndex, pBitArray);
		}
	}

	return true;
}

void Rope2::CollideWithColCache()
{
	if (m_colCache.m_numTris == 0)
		return;

	ScopedTempAllocator jjAlloc(FILE_LINE_FUNC);

	m_pPointCol = NDI_NEW Rope2PointCol[m_numPoints];

	U32F numAabbs = m_lastDynamicPoint - m_firstDynamicPoint + 1;

	HavokWorldMarkForRead();

	ColCacheTreeTraverseData data;
	data.m_pRope = this;
	data.m_pTreeNodes = m_colCache.m_pTriTreeNodes;
	data.m_lastShapeIndex = -1;
	m_colCache.m_tree.TraverseBundle(m_pNodeAabb+m_firstDynamicPoint, numAabbs, ColCacheTreeCollideCallback, &data);

	if (g_ropeMgr.m_debugDrawNodeAabb && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		for (U32 ii = m_firstDynamicPoint; ii<=m_lastDynamicPoint; ii++)
		{
			if (!g_ropeMgr.m_debugDrawSelectedIndex || ii == g_ropeMgr.m_selectedRopeIndex)
			{
				g_prim.Draw(DebugCross(m_pPos[ii], 0.03f, kColorCyan), kPrimDuration1FramePauseable);
			}
		}
	}

	for (U32 ii = m_firstDynamicPoint; ii<=m_lastDynamicPoint; ii++)
	{
		m_pPointCol[ii].GenerateConstraints(this, ii);
	}
	m_pPointCol = nullptr;

	HavokWorldUnmarkForRead();
}


/////////////////////////////////////////////////////////////////////////////////////////////
//

void Rope2::CollideWithTransformedTriangle2(I16 triIndex, const RopeColliderHandle& hCollider, Point_arg pt0, Point_arg pt1, Point_arg pt2, const ExternalBitArray* pTestPoints)
{
	Vector triEdge0 = Normalize(pt1 - pt0);
	Vector triEdge1 = Normalize(pt2 - pt1);
	Vector triEdge2 = Normalize(pt0 - pt2);
	Vector triNorm = Normalize(Cross(triEdge0, triEdge1));
	PHYSICS_ASSERT(IsFinite(triNorm) && IsFinite(triEdge0) && IsFinite(triEdge1) && IsFinite(triEdge2));

	Vector triBiEdge0 = Cross(triNorm, triEdge0);
	Vector triBiEdge1 = Cross(triNorm, triEdge1);
	Vector triBiEdge2 = Cross(triNorm, triEdge2);

	Vec4 triPlane = triNorm.GetVec4();
	triPlane.SetW(-Dot(triNorm, pt0 - kOrigin));

	Vector triEdge0Abs = Abs(triEdge0);
	Vector triEdge1Abs = Abs(triEdge1);
	Vector triEdge2Abs = Abs(triEdge2);

	Point triAabbMin = Min(Min(pt0, pt1), pt2);
	Point triAabbMax = Max(Max(pt0, pt1), pt2);
	Vector triNormAbs = Abs(triNorm);

	Vector triEdge0ZXY = Vector(Simd::ShuffleZXYW(triEdge0.QuadwordValue()));
	Vector triEdge1ZXY = Vector(Simd::ShuffleZXYW(triEdge1.QuadwordValue()));
	Vector triEdge2ZXY = Vector(Simd::ShuffleZXYW(triEdge2.QuadwordValue()));

	Vector triEdge0AbsZXY = Vector(Simd::ShuffleZXYW(triEdge0Abs.QuadwordValue()));
	Vector triEdge1AbsZXY = Vector(Simd::ShuffleZXYW(triEdge1Abs.QuadwordValue()));
	Vector triEdge2AbsZXY = Vector(Simd::ShuffleZXYW(triEdge2Abs.QuadwordValue()));

	Vector minP0, maxP0;
	Vector minP1, maxP1;
	Vector minP2, maxP2;
	{
		Vector pt0ZXY = Vector(Simd::ShuffleZXYW(pt0.QuadwordValue()));
		Vector pt1ZXY = Vector(Simd::ShuffleZXYW(pt1.QuadwordValue()));
		Vector pt2ZXY = Vector(Simd::ShuffleZXYW(pt2.QuadwordValue()));

		Vector p00 = triEdge0ZXY * (pt0-kOrigin) - triEdge0 * pt0ZXY;
		Vector p20 = triEdge0ZXY * (pt2-kOrigin) - triEdge0 * pt2ZXY;
		minP0 = Min(p00, p20);
		maxP0 = Max(p00, p20);

		Vector p01 = triEdge1ZXY * (pt0-kOrigin) - triEdge1 * pt0ZXY;
		Vector p11 = triEdge1ZXY * (pt1-kOrigin) - triEdge1 * pt1ZXY;
		minP1 = Min(p01, p11);
		maxP1 = Max(p01, p11);

		Vector p02 = triEdge2ZXY * (pt0-kOrigin) - triEdge2 * pt0ZXY;
		Vector p12 = triEdge2ZXY * (pt1-kOrigin) - triEdge2 * pt1ZXY;
		minP2 = Min(p02, p12);
		maxP2 = Max(p02, p12);
	}

	bool bAnyInContact = false;

	if (!pTestPoints)
	{
		PHYSICS_ASSERT(m_lastDynamicPoint < 1024);
		pTestPoints = &s_allSetBitArray1024;
	}

	U32F numPointsRel = m_lastDynamicPoint - m_firstDynamicPoint + 1;
	for (I32F iBeadRel = pTestPoints->FindFirstSetBit(); iBeadRel < numPointsRel; iBeadRel = pTestPoints->FindNextSetBit(iBeadRel))
	{
		I32F iBead = m_firstDynamicPoint + iBeadRel;

		// Strained sections of rope do not collide with characters
		if (IsNodeStrainedInt(iBead) || IsNodeKeyframedInt(iBead))
			continue;

		const Aabb& aabb = m_pNodeAabb[iBead];
		Point aabbCenter = aabb.GetCenter();
		Vector aabbHalfExt = aabb.GetExtent();

		// Triangle-AABB intersection test using SAT (see https://fileadmin.cs.lth.se/cs/Personal/Tomas_Akenine-Moller/pubs/tribox.pdf for example)
		// Optimized and SIMDed

		// Checking the 3 coord axis
		{
			// These both differences have to be positive
			Vector diff1 = aabb.m_max - triAabbMin;
			Vector diff2 = triAabbMax - aabb.m_min;

			// .. so this has to be positive
			Vector minDiff = Min(diff1, diff2);

			// But we also want to exclude case of triangle touching our AABB edge-on. 
			// Because in that case neighboring triangle that has edge with it might get excluded leaving a bad plane without edge
			Vector edgeOnTreshold(Simd::SetAll(0.001f));
			Vector offset(Simd::Select(Simd::GetVecAllZero(), edgeOnTreshold.QuadwordValue(), Simd::CompareGT(triNormAbs.QuadwordValue(), Simd::SetAll(0.99f)))); // will be zero except for direction where triangle is parallel to aabb face
			if (!AllComponentsGreaterThan(minDiff + offset, edgeOnTreshold))
			{
				continue;
			}
		}

		// Triangle normal
		if (Abs(Dot4(triPlane, aabbCenter.GetVec4())) > Dot(aabbHalfExt, triNormAbs))
			continue;

		Vector aabbCenterZXY = Vector(Simd::ShuffleZXYW(aabbCenter.QuadwordValue()));
		Vector aabbHalfExtZXY = Vector(Simd::ShuffleZXYW(aabbHalfExt.QuadwordValue()));

		// 3 axis coming from cross producting edge0 with 3 coord axis
		// Debugging note: axis corresponding to cross product with (x, y, z) goes to (z, x, y)
		Vector cen0 = triEdge0ZXY * (aabbCenter-kOrigin) - triEdge0 * aabbCenterZXY;
		Vector r0 = aabbHalfExt * triEdge0AbsZXY + aabbHalfExtZXY * triEdge0Abs;
		if (!AllComponentsGreaterThanOrEqual(cen0+r0, minP0) || !AllComponentsGreaterThanOrEqual(maxP0, cen0-r0))
			continue;

		// 3 axis coming from cross producting edge1 with 3 coord axis
		Vector cen1 = triEdge1ZXY * (aabbCenter-kOrigin) - triEdge1 * aabbCenterZXY;
		Vector r1 = aabbHalfExt * triEdge1AbsZXY + aabbHalfExtZXY * triEdge1Abs;
		if (!AllComponentsGreaterThanOrEqual(cen1+r1, minP1) || !AllComponentsGreaterThanOrEqual(maxP1, cen1-r1))
			continue;

		// 3 axis coming from cross producting edge2 with 3 coord axis
		Vector cen2 = triEdge2ZXY * (aabbCenter-kOrigin) - triEdge2 * aabbCenterZXY;
		Vector r2 = aabbHalfExt * triEdge2AbsZXY + aabbHalfExtZXY * triEdge2Abs;
		if (!AllComponentsGreaterThanOrEqual(cen2+r2, minP2) || !AllComponentsGreaterThanOrEqual(maxP2, cen2-r2))
			continue;

		{
			// Raycast (lastPos, pos) against the triangle
			Scalar planeDist = Dot4(triPlane, m_pPos[iBead].GetVec4());
			Scalar planeDistLast = Dot4(triPlane, m_pLastPos[iBead].GetVec4());
			Scalar d = planeDistLast - planeDist;
			Scalar f = planeDistLast / d;
			if (IsFinite(f) & (f > 0.0f) & (f < 1.0f))
			{
				Point pI = Lerp(m_pLastPos[iBead], m_pPos[iBead], f);
				Vector c(
					Dot(triBiEdge0, pI - pt0),
					Dot(triBiEdge1, pI - pt1),
					Dot(triBiEdge2, pI - pt2)
				);
				if (AllComponentsGreaterThanOrEqual(c, kZero))
				{
					// Hit! Move the current pos
					m_pPos[iBead] = pI;
				}
			}
		}
		
		m_pPointCol[iBead].AddTriangle(triIndex, triPlane, pt0, pt1, pt2, hCollider);

		bAnyInContact = bAnyInContact || !g_ropeMgr.m_debugDrawSelectedIndex || iBead == g_ropeMgr.m_selectedRopeIndex;
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#if ROPE_DEBUG_DRAW
	if (bAnyInContact && g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		g_prim.Draw(DebugLine(pt0, pt1, kColorRed, 1.0f, PrimAttrib()), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(pt1, pt2, kColorRed, 1.0f, PrimAttrib()), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(pt2, pt0, kColorRed, 1.0f, PrimAttrib()), kPrimDuration1FramePauseable);
	}
#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

void Rope2::CollideWithColCacheConvex(const RopeCollider* pCollider,
							  const RopeColliderHandle& hCollider,
							  const hknpConvexPolytopeShape* pConvex,
							  U32 triIndex,
							  const ExternalBitArray* pTestPoints)
{
	PHYSICS_ASSERT(pConvex->getConnectivity());

	bool bAnyInContact = false;

	const Locator& loc = pCollider->m_loc;

	hkAabb aabbHk;
	pConvex->calcAabb(hkTransform::getIdentity(), aabbHk);
	Obb obb(loc, Point(aabbHk.m_min.getQuad()), Point(aabbHk.m_max.getQuad()));
	obb.Expand(pConvex->m_convexRadius);

	hkcdGsk::GetClosestPointInput input; input.m_bTa.setIdentity();
	input.m_coreCollisionTolerance = 2.0f; // arbitrary

	U32F numPointsRel = m_lastDynamicPoint - m_firstDynamicPoint + 1;
	for (I32F iBeadRel = pTestPoints->FindFirstSetBit(); iBeadRel < numPointsRel; iBeadRel = pTestPoints->FindNextSetBit(iBeadRel))
	{
		I32F iBead = m_firstDynamicPoint + iBeadRel;

		// Strained sections of rope do not collide with characters
		if (IsNodeStrainedInt(iBead) || IsNodeKeyframedInt(iBead))
			continue;

		const Aabb& aabb = m_pNodeAabb[iBead];
		if (AabbObbIntersect(aabb, obb))
		{
#if defined(JSINECKY) && defined(_DEBUG)
			if (DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner) && g_ropeMgr.m_debugDrawNodes && g_ropeMgr.m_debugDrawSelectedIndex && iBead == g_ropeMgr.m_selectedRopeIndex)
			{
				printf("Break here\n");
			}
#endif

			Point ptBeadLocal = loc.UntransformPoint(m_pPos[iBead]);

			{
				// Raycast (lastPos, pos) against the triangle
				hknpInplaceTriangleShape targetTriangle( 0.0f );
				hknpCollisionQueryContext queryContext( nullptr, targetTriangle.getTriangleShape() );

				Point ptBeadLastLocal = loc.UntransformPoint(m_pLastPos[iBead]);

				hknpRayCastQuery query;
				query.m_ray.setEndPoints(hkVector4(ptBeadLastLocal.QuadwordValue()), hkVector4(ptBeadLocal.QuadwordValue()));
					
				hknpQueryFilterData filterData;
				HavokClosestHitCollector collector;

				hkTransform shapeXfm; 
				shapeXfm.setIdentity();
				hknpShapeQueryInfo queryInfo;
				queryInfo.m_rootShape = pConvex;
				queryInfo.m_shapeToWorld = &shapeXfm;

				pConvex->castRayImpl( &queryContext, query, filterData, queryInfo, &collector);
				if (collector.hasHit())
				{
					ptBeadLocal = Point(collector.getHit().m_position.getQuad());
					m_pPos[iBead] = loc.TransformPoint(ptBeadLocal);
				}
			}

			const hkcdVertex* pVertices = pConvex->getVertices().begin();
			hkcdVertex beadVtxLoc; beadVtxLoc.assign(hkVector4(ptBeadLocal.QuadwordValue()), 0);
			hkcdGsk::Cache                  cache; cache.init();
			hkcdGsk::GetClosestPointOutput  output;
			hkcdGsk::GetClosestPointStatus  status = hkcdGsk::getClosestPoint(pVertices, pConvex->getVertices().getSize(), &beadVtxLoc, 1, input, &cache, output);
			if (aabb.ContainsPoint(loc.TransformPoint(Point(output.m_pointAinA.getQuad()))))
			{
				hkVector4 planeLoc[3];
				I32 faceId[3];
				F32 distPl[3];

				// Supporting face. Basically the face that the point is closest to.
				hknpAngularTimType minAngle;
				faceId[0] = pConvex->getSupportingFace(output.m_normalInA, beadVtxLoc, &cache, false, -1, planeLoc[0], minAngle);
				const hknpConvexPolytopeShape::Face& face = pConvex->getFaces()[faceId[0]];
				const hkRelArray<hknpConvexShape::VertexIndex>& vtxIndices = pConvex->getFaceVertexIndices();
				
				distPl[0] = (F32)output.m_distance - pConvex->m_convexRadius;
				
				I32 apexId = 0;
				bool plane1IsAfterApex = false;
				hkVector4 pOnClosestEdge = hkVector4::getZero();
				if (cache.getDimA() == 1)
				{
					// Colliding with vertex
					apexId = cache.getVertexIdsA()[0];
					distPl[1] = distPl[2] = distPl[0];
				}
				else if (cache.getDimA() == 2)
				{
					// Colliding with edge
					I32 vtxId0 = cache.getVertexIdsA()[0];
					I32 vtxId1 = cache.getVertexIdsA()[1];
					bool found = false;
					for (I32 iEdge = 0; iEdge < face.m_numIndices; iEdge++)
					{
						if (vtxId0 == vtxIndices[face.m_firstIndex + iEdge])
						{
							if (vtxId1 != vtxIndices[face.m_firstIndex + (iEdge + 1) % face.m_numIndices])
							{
								if (vtxId1 != vtxIndices[iEdge > 0 ? face.m_firstIndex + (iEdge-1) : face.m_firstIndex+face.m_numIndices-1])
								{
									// Havok sometimes returns collision with a face diagonal as if it were edge collision
									// We can consider that plane collision
									cache.setDims(3, 1, 0);
									break;
								}
								I32 swapHelper = vtxId0;
								vtxId0 = vtxId1;
								vtxId1 = swapHelper;
							}
							found = true;
							break;
						}
					}
					if (cache.getDimA() == 2)
					{
						JAROS_ALWAYS_ASSERT(found);
						F32 d0 = output.m_pointAinA.distanceToSquared3(pVertices[vtxId0]);
						F32 d1 = output.m_pointAinA.distanceToSquared3(pVertices[vtxId1]);
						apexId = d0 < d1 ? vtxId0 : vtxId1;
						plane1IsAfterApex = d0 < d1;
						distPl[1] = distPl[0];
					}
				}
				if (cache.getDimA() == 3)
				{
					// Colliding with plane
					I32 iMinEdge = 0;
					F32 minDist2 = FLT_MAX;
					for (I32 iEdge = 0; iEdge < face.m_numIndices; iEdge++)
					{
						hkVector4 vtxA = pVertices[vtxIndices[face.m_firstIndex + iEdge]];
						hkVector4 vtxB = pVertices[vtxIndices[face.m_firstIndex + (iEdge + 1) % face.m_numIndices]];
						hkVector4 edgeDir; edgeDir.setSub(vtxB, vtxA); edgeDir.normalize3IfNotZero();
						hkVector4 pVec; pVec.setSub(output.m_pointAinA, vtxA); 
						hkVector4 pOnEdge; pOnEdge.setAddMul(vtxA, edgeDir, edgeDir.dot3(pVec));
						F32 dist2 = pOnEdge.distanceToSquared3(output.m_pointAinA);
						if (dist2 < minDist2)
						{
							minDist2 = dist2;
							iMinEdge = iEdge;
							pOnClosestEdge = pOnEdge;
						}
					}

					I32 vtxId0 = vtxIndices[face.m_firstIndex + iMinEdge];
					I32 vtxId1 = vtxIndices[face.m_firstIndex + (iMinEdge + 1) % face.m_numIndices];
					F32 d0 = output.m_pointAinA.distanceToSquared3(pVertices[vtxId0]);
					F32 d1 = output.m_pointAinA.distanceToSquared3(pVertices[vtxId1]);
					apexId = d0 < d1 ? vtxId0 : vtxId1;
					plane1IsAfterApex = d0 < d1;
				}

				// We have apexId of the colliding vertex
				// Find the other 2 faces
				bool linkFound = false;
				for (I32 iEdge = 0; iEdge < face.m_numIndices; iEdge++)
				{
					hknpConvexPolytopeShape::Connectivity::Edge linkEdgeBefore;
					hknpConvexPolytopeShape::Connectivity::Edge linkEdgeAfter;
					I32 iNextVtx = (iEdge + 1) % face.m_numIndices;
					if (vtxIndices[face.m_firstIndex + iNextVtx] == apexId)
					{
						hknpConvexPolytopeShape::Connectivity::Edge edge;
						edge.m_faceIndex = faceId[0];
						edge.m_edgeIndex = iEdge;
						linkEdgeBefore = pConvex->getLinkEdge(edge);
						edge.m_edgeIndex = iNextVtx;
						linkEdgeAfter = pConvex->getLinkEdge(edge);
						if (plane1IsAfterApex)
						{
							faceId[1] = linkEdgeAfter.m_faceIndex;
							faceId[2] = linkEdgeBefore.m_faceIndex;
						}
						else
						{
							faceId[2] = linkEdgeAfter.m_faceIndex;
							faceId[1] = linkEdgeBefore.m_faceIndex;
						}
						PHYSICS_ASSERT(faceId[0] != faceId[1] && faceId[0] != faceId[2] && faceId[1] != faceId[2]);
						linkFound = true;
						break;
					}
				}
				PHYSICS_ASSERT(linkFound);

				planeLoc[1] = pConvex->getPlanes()[faceId[1]];
				planeLoc[2] = pConvex->getPlanes()[faceId[2]];

				// Distance to each face
				// These distances are kind of approximate
				if (cache.getDimA() == 3)
				{
					pOnClosestEdge.addMul(planeLoc[0], pConvex->m_convexRadius);
					pOnClosestEdge.addMul(planeLoc[1], pConvex->m_convexRadius);
					if (aabb.ContainsPoint(loc.TransformPoint(Point(pOnClosestEdge.getQuad()))))
					{
						distPl[1] = pOnClosestEdge.distanceTo3(beadVtxLoc);
					}
					else
					{
						distPl[1] = FLT_MAX;
					}
				}
				if (cache.getDimA() > 1)
				{
					hkVector4 pApex = pConvex->getVertices()[apexId];
					pApex.addMul(planeLoc[0], pConvex->m_convexRadius);
					pApex.addMul(planeLoc[1], pConvex->m_convexRadius);
					pApex.addMul(planeLoc[2], pConvex->m_convexRadius);
					if (aabb.ContainsPoint(loc.TransformPoint(Point(pApex.getQuad()))))
					{
						distPl[2] = pApex.distanceTo3(beadVtxLoc);
					}
					else
					{
						distPl[2] = FLT_MAX;
					}
				}

				for (U32 iPl = 0; iPl<3; iPl++)
				{
					if (distPl[iPl] < FLT_MAX)
					{
						// Transform to world and add to point
						Vector planeNormLoc = Vector(planeLoc[iPl].getQuad());
						Vector planeNorm = loc.TransformVector(planeNormLoc);
						Point planePntLoc = Point(kOrigin) - planeNormLoc * ((F32)planeLoc[iPl].getW() - pConvex->m_convexRadius);
						Point planePnt = loc.TransformPoint(planePntLoc);
						Vec4 plane = planeNorm.GetVec4();
						plane.SetW(Dot(Point(kOrigin) - planePnt, planeNorm));
						m_pPointCol[iBead].AddConvexFace(plane, faceId[iPl], hCollider);
					}
				}

				bAnyInContact = bAnyInContact || !g_ropeMgr.m_debugDrawSelectedIndex || iBead == g_ropeMgr.m_selectedRopeIndex;
			}
		}
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#if ROPE_DEBUG_DRAW
	if (g_ropeMgr.m_debugDrawColliders && DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		Color color = bAnyInContact ? kColorRed : kColorGray;
		HavokDebugDrawShape(pConvex, loc, color, CollisionDebugDrawConfig::MenuOptions(), nullptr, kPrimDuration1FramePauseable);
	}
#endif
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}

