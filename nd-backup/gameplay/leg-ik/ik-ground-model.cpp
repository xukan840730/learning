/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/leg-ik/ik-ground-model.h"

#include "corelib/containers/list-array.h"
#include "corelib/math/intersection.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/frame-params.h"
#include "ndlib/nd-options.h"
#include "ndlib/render/util/prim-server-wrapper.h"

#include "gamelib/gameplay/leg-ik/character-leg-ik-controller.h"
#include "gamelib/ndphys/collision-cast-interface.h"
#include "gamelib/ndphys/collision-cast.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/havok.h"
#include "gamelib/ndphys/simple-collision-cast.h"

#include <Eigen/Dense>
#include <Geometry/Collide/Algorithms/Triangle/hkcdTriangleUtil.h>

/// --------------------------------------------------------------------------------------------------------------- ///
static bool SegmentIntersectHalfSpace(Point_arg p,
									  Vector_arg n,
									  const Point p0,
									  const Point p1,
									  Point& outP0,
									  Point& outP1)
{
	Scalar d0 = Dot(p0 - p, n);
	Scalar d1 = Dot(p1 - p, n);
	// Intersection
	if (Sign(d0) != Sign(d1))
	{
		Point segPoint, planePt;
		Scalar tt = LinePlaneIntersect(p, n, p0, p1, &segPoint, &planePt);
		ASSERT(tt >= 0.0f && tt <= 1.0f);
		if (Sign(d0) > Scalar(kZero))
		{
			outP0 = p0;
			outP1 = segPoint;
		}
		else
		{
			outP1 = p1;
			outP0 = segPoint;
		}
		return true;
	}
	else if (d0 >= Scalar(kZero) && d1 >= Scalar(kZero))
	{
		outP0 = p0;
		outP1 = p1;
		return true;
	}
	outP0 = p0;
	outP1 = p1;
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vector GroundPlaneNormFromPoints(Point_arg p0, Point_arg p1)
{
	Vector v(p1 - p0);
	return SafeNormalize(Cross(Cross(v, kUnitYAxis), v), kUnitYAxis);
}

/// --------------------------------------------------------------------------------------------------------------- ///
GroundModel::HullCastResult GroundModel::CastRayToHull(Point_arg pos, Vector_arg dir) const
{
	ANIM_ASSERT(IsReasonable(pos));
	ANIM_ASSERT(IsReasonable(dir));
	Vector dirFlat	 = VectorXz(dir);
	Vector planeNorm = Cross(kUnitYAxis, dirFlat);

	bool hit = false;
	Point contact(pos);
	Vector normal  = kZero;
	Scalar minDist = kLargestFloat;

	for (I32F i = 0; i < m_hull.m_vertexCount; i++)
	{
		const I32F iNext = (i + 1) % m_hull.m_vertexCount;

		Point planeContact;
		Scalar edgeTT = LinePlaneIntersect(pos,
										   planeNorm,
										   m_hull.m_vertices[i],
										   m_hull.m_vertices[iNext],
										   nullptr,
										   &planeContact);

		if (edgeTT >= 0.0f && edgeTT <= 1.0f)
		{
			Scalar dist = Dist(pos, planeContact);
			if (dist < minDist)
			{
				hit		= true;
				contact = planeContact;
				normal	= kUnitYAxis;
				minDist = dist;
			}
		}
	}

	if (!hit)
	{
		if (pos.X() < (m_minX - 0.2f) || pos.X() > (m_maxX + 0.2f))
		{
			return HullCastResult();
		}
		// ASSERT(pos.X() < m_minX || pos.X() > m_maxX);
		const Plane* pPlane = &m_minPlane;

		if (pos.X() > m_maxX)
		{
			pPlane = &m_maxPlane;
		}

		const Vector locPlaneNorm = pPlane->GetNormal();
		Scalar t = (-pPlane->GetD() - Dot(locPlaneNorm, AsVector(pos))) / Dot(locPlaneNorm, dirFlat);
		if (t > 0.0f)
		{
			contact = pos + dirFlat * t;
			normal	= locPlaneNorm;
			ALWAYS_ASSERT(IsReasonable(contact));
		}
		else
		{
			return HullCastResult();
		}
		// MsgCon("Cast ray to hull failed");
	}

	ANIM_ASSERT(IsReasonable(contact));
	ANIM_ASSERT(IsReasonable(normal));

	HullCastResult res;
	res.m_valid	 = true;
	res.m_pos	 = contact;
	res.m_normal = normal;

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GroundModel::FindGround(Point_arg startPos, Point_arg endPos, Point_arg alignPos)
{
	DoFindGround(startPos, endPos, alignPos, kNumRays, g_enableLegRaycasts);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GroundModel::DoFindGround(Point_arg startPos,
							   Point_arg endPos,
							   Point_arg alignPos,
							   const int numRays,
							   const bool useMeshRays)
{
	// MsgCon("Num raycasts sets complete: %d\n", m_numMeshRaySetsDone);
	ASSERT(Dist(startPos, kOrigin) > 0.01f);
	ASSERT(Dist(endPos, kOrigin) > 0.01f);

	const Vector rayDir		= VECTOR_LC(0.0f, -5.0f, 0.0f);
	const Scalar probeY		= alignPos.Y() + SCALAR_LC(2.5f);
	SimpleCastProbe* probes = STACK_ALLOC(SimpleCastProbe, numRays);
	for (int i = 0; i < numRays; i++)
	{
		float tt		= (float)i / (numRays - 1);
		probes[i].m_pos = Lerp(startPos, endPos, tt);
		probes[i].m_pos.SetY(probeY);
		probes[i].m_vec = rayDir;
		probes[i].m_radius = 0.05f;
	}

	int pendingArrayIndex = GetFreePendingRayProbes();
	m_numMeshRaySetsDone  = 0;
	if (pendingArrayIndex >= 0 && useMeshRays)
	{
		ALWAYS_ASSERT(pendingArrayIndex < 6);

		PendingRayProbes& pendingProbes = m_aPendingProbes[pendingArrayIndex];
		pendingProbes.m_resultsValid.ClearAllBits();
		pendingProbes.m_frameNumber = GetCurrentFrameNumber();
		pendingProbes.m_startPos	= startPos;
		pendingProbes.m_endPos		= endPos;
		pendingProbes.m_alignPos	= alignPos;

		const Point adjStart(startPos.X(), probeY, startPos.Z());
		const Point adjEnd(endPos.X(), probeY, endPos.Z());
		pendingProbes.m_segmentToWorld = Transform(kIdentity);
		pendingProbes.m_segmentToWorld.SetTranslation(adjStart);
		pendingProbes.m_segmentToWorld.SetXAxis(adjEnd - adjStart);
		pendingProbes.m_segmentToWorld.SetYAxis(rayDir);
		pendingProbes.m_segmentToWorld.SetZAxis(SafeNormalize(Cross(pendingProbes.m_segmentToWorld.GetXAxis(),
																	pendingProbes.m_segmentToWorld.GetYAxis()),
															  kUnitZAxis));

		pendingProbes.m_worldToSegment = Inverse(pendingProbes.m_segmentToWorld);

		for (int i = 0; i < ARRAY_COUNT(pendingProbes.m_aResults); i++)
			pendingProbes.m_aResults[i].Clear();

		ASSERT(m_hOwner.HandleValid());
		for (int i = 0; i < numRays; i++)
		{
			CallbackContext context;
			context.m_hGroundModel = ProcessComponentHandle<GroundModel>(m_hOwner.ToMutableProcess(), this);
			context.m_probeIndex   = i;
			context.m_frameNumber  = GetCurrentFrameNumber();

			MeshRayCastJob rayJob;
			rayJob.SetProbeExtent(probes[i].m_pos, probes[i].m_pos + probes[i].m_vec);
			rayJob.SetHitFilter(MeshRayCastJob::HitFilter(MeshRayCastJob::HitFilter::kHitIk));
			rayJob.SetBehaviorFlags(MeshRayCastJob::kEveryFrame);
			rayJob.m_pCallback		  = MeshrayCallbackFunc;
			rayJob.m_pCallbackContext = reinterpret_cast<MeshRayCastJob::CallbackContext*>(&context);

			rayJob.Kick(FILE_LINE_FUNC);
		}
	}

	const int completedMeshJob = GetNextCompleteRayProbes();
	if (completedMeshJob >= 0 && useMeshRays)
	{
		const PendingRayProbes& pendingProbes = m_aPendingProbes[completedMeshJob];

		Vector localZ = kUnitYAxis;
		Vector localX = SafeNormalize(VectorXz(pendingProbes.m_endPos - pendingProbes.m_startPos), kUnitZAxis);
		Vector localY = Cross(localZ, localX);

		Transform hullToWorld(localX, localY, localZ, startPos);
		m_worldToHull = Inverse(hullToWorld);

		ConstructGroundFromContacts(pendingProbes.m_startPos,
									pendingProbes.m_endPos,
									numRays,
									pendingProbes.m_aResults,
									pendingProbes.m_alignPos);
		m_hullFromRenderable = true;
	}
	else if (!useMeshRays)
	{
		SphereCastJob job;
		SimpleCastKick(job,
					   probes,
					   numRays,
					   CollideFilter(Collide::kLayerMaskGeneral),
					   ICollCastJob::kCollCastSynchronous,
					   ICollCastJob::kClientPlayerMisc);
		// 	 	job.Wait();
		// 	 	job.DebugDraw(kPrimDuration1FramePauseable);
		SimpleCastGather(job, probes, numRays);

		Vector localZ = kUnitYAxis;
		Vector localX = SafeNormalize(VectorXz(endPos - startPos), kUnitZAxis);
		Vector localY = Cross(localZ, localX);

		Transform hullToWorld(localX, localY, localZ, startPos);
		m_worldToHull = Inverse(hullToWorld);

		ProbeResult* aResults = STACK_ALLOC(ProbeResult, numRays);
		for (int i = 0; i < numRays; i++)
		{
			ALWAYS_ASSERT(IsReasonable(probes[i].m_cc.m_contact));
			ALWAYS_ASSERT(IsReasonable(probes[i].m_cc.m_normal));
			aResults[i].m_contact	   = probes[i].m_cc.m_contact;
			aResults[i].m_normal	   = probes[i].m_cc.m_normal;
			aResults[i].m_valid		   = probes[i].m_cc.m_time >= 0.0f;
			aResults[i].m_segmentValid = false;
		}

		ConstructGroundFromContacts(startPos, endPos, numRays, aResults, alignPos);
		m_hullFromRenderable = false;
	}
	return;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Point> GroundModel::ProjectPointToGround(Point_arg posWs) const
{
	ANIM_ASSERT(IsReasonable(posWs));
	if (!m_valid)
	{
		return MAYBE::kNothing;
	}
	Point posHullSpace = posWs * m_worldToHull;
	posHullSpace.SetY(0.0f);
	posHullSpace.SetZ(6.0f);

	Transform hullToWorld = Inverse(m_worldToHull);
	PrimServerWrapper wrapper(hullToWorld);

	Point startPos(posHullSpace);
	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
	{
		wrapper.DrawCross(startPos, 0.1f, kColorYellow);
		g_prim.Draw(DebugSphere(posWs, 0.1f), kPrimDuration1FramePauseable);
	}

	const HullCastResult castResult = CastRayToHull(startPos, -Vector(kUnitZAxis));
	if (castResult.m_valid)
	{
		posHullSpace = castResult.m_pos;
	}
	else
	{
		return MAYBE::kNothing;
	}

	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
	{
		wrapper.DrawCross(posHullSpace, 0.1f, kColorRed);
		wrapper.DrawLine(startPos, posHullSpace, kColorGreen, kColorRed);
	}

	ANIM_ASSERT(IsReasonable(posHullSpace));
	return posHullSpace * Inverse(m_worldToHull);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Vector> GroundModel::GetGroundNormalAtPos(Point_arg posWs) const
{
	ANIM_ASSERT(IsReasonable(posWs));
	if (!m_valid)
	{
		return MAYBE::kNothing;
	}
	Point posHullSpace = posWs * m_worldToHull;
	posHullSpace.SetY(0.0f);
	posHullSpace.SetZ(6.0f);

	Transform hullToWorld = Inverse(m_worldToHull);
	PrimServerWrapper wrapper(hullToWorld);

	const Point startPos(posHullSpace);

	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
	{
		wrapper.DrawCross(startPos, 0.1f, kColorYellow);
		g_prim.Draw(DebugSphere(posWs, 0.1f), kPrimDuration1FramePauseable);
	}

	Vector normalHullSpace = kZero;
	const HullCastResult castResult = CastRayToHull(startPos, -Vector(kUnitZAxis));
	if (castResult.m_valid)
	{
		normalHullSpace = castResult.m_normal;
	}
	else
	{
		return MAYBE::kNothing;
	}

	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
	{
		wrapper.DrawCross(posHullSpace, 0.1f, kColorRed);
		wrapper.DrawLine(startPos, posHullSpace, kColorGreen, kColorRed);
	}

	ANIM_ASSERT(IsReasonable(normalHullSpace));
	return normalHullSpace * Inverse(m_worldToHull);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GroundModel::IsValid() const
{
	return m_valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
GroundModel::GroundModel()
{
	m_valid = false;
	m_numMeshRaySetsDone = 0;
	m_hullFromRenderable = false;

	for (int i = 0; i < ARRAY_COUNT(m_aPendingProbes); i++)
	{
		m_aPendingProbes[i].m_frameNumber = -1;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DrawHull(PrimServerWrapper& prim,
					 const ConvexHullXZ& hull,
					 const Color& edgeDisplayColor,
					 const Color& quadColor,
					 Vector_arg upVec)
{
	if (hull.m_vertexCount == 0)
	{
		return;
	}

	// prim.DisableDepthTest();
	Point edgeStart = hull.m_vertices[hull.m_vertexCount - 1];
	for (U32F iVert = 0; iVert < hull.m_vertexCount; ++iVert)
	{
		const Point edgeEnd = hull.m_vertices[iVert];

		Point corner0 = edgeStart;
		Point corner1 = edgeStart;
		Point corner2 = edgeEnd;
		Point corner3 = edgeEnd;
		corner0.SetY(hull.m_yMin);
		corner1.SetY(hull.m_yMax);
		corner2.SetY(hull.m_yMax);
		corner3.SetY(hull.m_yMin);

		Vector norm = Normalize(Cross(corner1 - corner0, corner2 - corner0));

		if (Dot(norm, upVec) < 0.0f)
		{
			// prim.DrawLine(corner0, corner0 + norm, displayColor);

			prim.DrawLine(corner0, corner1, edgeDisplayColor);
			prim.DrawLine(corner1, corner2, edgeDisplayColor);
			prim.DrawLine(corner2, corner3, edgeDisplayColor); // same as (corner0, corner1) for next iteration
			prim.DrawLine(corner3, corner0, edgeDisplayColor);
			prim.DrawQuad(corner3, corner2, corner1, corner0, quadColor);
		}

		edgeStart = edgeEnd;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GroundModel::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	DebugDraw(kColorWhite, kPrimDuration1FramePauseable);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GroundModel::DebugDraw(Color c, DebugPrimTime time) const
{
	STRIP_IN_FINAL_BUILD;
	if (!IsValid())
		return;

	Transform hullToWorld(Inverse(m_worldToHull));

	c = m_hullFromRenderable ? kColorGreen : c;

	PrimServerWrapper wrapper(hullToWorld);
	wrapper.SetDuration(time);
	const Vector upHullSpace = Vector(kUnitYAxis) * m_worldToHull;
	DrawHull(wrapper, m_hull, kColorMagenta, c, upHullSpace);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GroundModel::EnforcePointOnGround(Point_arg pos)
{
	ANIM_ASSERT(IsReasonable(pos));
	Maybe<Point> projected = ProjectPointToGround(pos);

	float epsilon = 0.005f;
	if (!projected.Valid() || projected.Get().Y() < pos.Y())
	{
		Point oldVertices[ConvexHullXZ::kMaxVertices];
		U32 vertexCount = m_hull.m_vertexCount;
		memcpy(oldVertices, m_hull.m_vertices, sizeof(Point) * vertexCount);
		oldVertices[vertexCount] = pos * m_worldToHull;
		oldVertices[vertexCount].SetY(kZero);
		vertexCount++;
		m_hull.Build(oldVertices, vertexCount, 0.25f);
	}
	else if (Abs(projected.Get().Y() - pos.Y()) > epsilon && false)
	{
		Point posHullSpace = pos * m_worldToHull;
		int nextPointIndex = -1;
		for (int i = 0; i < m_hull.m_vertexCount; i++)
		{
			if (m_hull.m_vertices[i].X() > posHullSpace.X() && m_hull.m_vertices[i].X() > 0.0f)
			{
				nextPointIndex = i;
				break;
			}
		}
		if (nextPointIndex >= 0)
		{
			Point pointOnHull = m_hull.m_vertices[nextPointIndex];
			Vector planeNorm  = Normalize(Cross(pointOnHull - posHullSpace, kUnitYAxis));
			ASSERT(IsReasonable(planeNorm));
			Vector planeNormWS = planeNorm * Inverse(m_worldToHull);
			ASSERT(Dot(planeNormWS, kUnitYAxis) > 0.0f);

			Plane newPlane(pointOnHull, planeNorm);

			Point oldVertices[ConvexHullXZ::kMaxVertices];
			U32 vertexCount = m_hull.m_vertexCount;
			memcpy(oldVertices, m_hull.m_vertices, sizeof(Point) * vertexCount);

			ListArray<Point> verList(ARRAY_COUNT(oldVertices), oldVertices, vertexCount);
			int numRemoved = 0;
			for (ListArray<Point>::Iterator it = verList.begin(); it != verList.end(); it++)
			{
				if (it->X() < pointOnHull.X() && newPlane.Dist(*it) > 0.0f)
				{
					*it = newPlane.ProjectPoint(*it);
					numRemoved++;
				}
			}
			// ASSERT(numRemoved > 0);
			verList.push_back(posHullSpace);

			m_hull.Build(oldVertices, verList.Size(), 0.25f);
			Maybe<Point> projectedTest = ProjectPointToGround(pos);
			ASSERT(projectedTest.Valid() && Abs(projectedTest.Get().Y() - pos.Y()) < epsilon);
		}
	}

	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
	{
		DebugDraw(kColorOrange, kPrimDuration1FramePauseable);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point GroundModel::GetClosestPointOnGround(Point_arg pos)
{
	ANIM_ASSERT(IsReasonable(pos));
	if (!IsValid())
		return pos;

	Point posHullSpace = pos * m_worldToHull;
	Point* pClosestPt  = nullptr;
	Scalar closestDist(kLargestFloat);
	for (int i = 0; i < m_hull.m_vertexCount; i++)
	{
		Scalar dist = Dist(posHullSpace, m_hull.m_vertices[i]);
		if (dist < closestDist)
		{
			pClosestPt	= &m_hull.m_vertices[i];
			closestDist = dist;
		}
	}

	ASSERT(pClosestPt);

	return *pClosestPt * Inverse(m_worldToHull);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vector SanitizeGroundNormal(Vector_arg groundNormal)
{
	if (groundNormal.Y() < 0.707f)
	{
		// ASSERT(false);
		return kUnitYAxis;
	}
	return groundNormal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GroundModel::ConstructGroundFromContacts(Point_arg startPos,
											  Point_arg endPos,
											  const int numRays,
											  const ProbeResult* probes,
											  Point_arg& alignPos)
{
	Point startGround(startPos);
	Point endGround(endPos);

	Vector startGroundNorm(kUnitYAxis);
	Vector endGroundNorm(kUnitYAxis);

	int maxValidIndex = numRays - 1;

	if (probes[0].m_valid)
	{
		float delta		 = probes[0].m_contact.Y() - alignPos.Y();
		float deltaXZ	 = DistXz(probes[0].m_contact, alignPos);
		float slopeAngle = RADIANS_TO_DEGREES(Atan2(delta, deltaXZ));

		if (deltaXZ > 0.0f)
		{
			if (slopeAngle > 45.0f || slopeAngle < -45.0f)
			{
				m_valid = false;
				return;
			}
		}
	}
	else
	{
		m_valid = false;
		return;
	}

	for (int i = 1; i < numRays; i++)
	{
		const ProbeResult& curProbe	 = probes[i];
		const ProbeResult& prevProbe = probes[i - 1];

		if (curProbe.m_valid && prevProbe.m_valid)
		{
			float delta = curProbe.m_contact.Y() - prevProbe.m_contact.Y();
			if (delta > 0.3f || delta < -0.4f)
			{
				maxValidIndex = i - 1;
				endGround	  = prevProbe.m_contact;
				break;
			}
		}
	}

	if (probes[maxValidIndex].m_valid)
	{
		endGround.SetY(probes[maxValidIndex].m_contact.Y());
		endGroundNorm = probes[maxValidIndex].m_normal;
		endGroundNorm = SanitizeGroundNormal(endGroundNorm);
	}
	else
	{
		endGround.SetY(alignPos.Y());
	}

	if (probes[0].m_valid)
	{
		startGround.SetY(probes[0].m_contact.Y());
		startGroundNorm = probes[0].m_normal;
		startGroundNorm = SanitizeGroundNormal(startGroundNorm);
	}
	else
	{
		startGround.SetY(alignPos.Y());
	}

	Vector planeNorm = GroundPlaneNormFromPoints(startGround, endGround);

	const int maxNumPoints = numRays * 2 + 2;
	Point* aHullPts		   = STACK_ALLOC(Point, maxNumPoints);
	ListArray<Point> pointList(maxNumPoints, aHullPts);

	Point startGroundHullSpace = startGround * m_worldToHull;
	ANIM_ASSERT(IsReasonable(startGroundHullSpace));
	pointList.push_back(startGroundHullSpace);

	for (int i = maxValidIndex - 1; i >= 1; i--)
	{
		if (!probes[i].m_valid)
		{
			continue;
		}
		const Point& contact = probes[i].m_contact;
		ANIM_ASSERT(probes[i].m_valid);
		ANIM_ASSERT(IsReasonable(contact));
		float dist = Dot(planeNorm, contact - startGround);
		if (dist > 0.005f)
		{
			if (probes[i].m_segmentValid)
			{
				if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
				{
					DebugDrawLine(probes[i].m_segmentPos[0], probes[i].m_segmentPos[1], kColorYellow);
				}
				pointList.push_back(probes[i].m_segmentPos[0] * m_worldToHull);
				pointList.push_back(probes[i].m_segmentPos[1] * m_worldToHull);
			}
			else
			{
				if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
				{
					DebugDrawCross(contact, 0.05f, kColorYellow);
				}
				pointList.push_back(contact * m_worldToHull);
			}
		}
	}
	Point endGroundHullSpace = endGround * m_worldToHull;
	ANIM_ASSERT(IsReasonable(endGroundHullSpace));
	pointList.push_back(endGroundHullSpace);

	m_hull.Build(aHullPts, pointList.Size(), 0.25f);

	ALWAYS_ASSERT(IsReasonable(startGroundNorm));
	ALWAYS_ASSERT(IsReasonable(endGroundNorm));
	ASSERT(startGroundNorm.Y() >= 0.707f);
	ASSERT(endGroundNorm.Y() >= 0.707f);

	m_minX	   = startGroundHullSpace.X();
	m_minPlane = Plane(startGroundHullSpace, startGroundNorm * m_worldToHull);
	m_maxPlane = Plane(endGroundHullSpace, endGroundNorm * m_worldToHull);
	m_maxX	   = endGroundHullSpace.X();

	m_valid = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GroundModel::MeshrayCallbackFunc(MeshProbe::CallbackObject const* pObject,
									  MeshRayCastJob::CallbackContext* pContext,
									  const MeshProbe::Probe& probeReq)
{
	PROFILE(Player, GroundModel_MeshrayCallback);
	CallbackContext* pCallbackContext = reinterpret_cast<CallbackContext*>(pContext);

	GroundModel* pGroundModel = pCallbackContext->m_hGroundModel.ToMutablePointer();
	// using havok for plane intersection, if we reimplement plane intersection ourselves, we can get rid of this check
	if (!pGroundModel || !HavokIsEnabled())
	{
		return;
	}
	// 	if (pGroundModel->m_pTriangleCache)
	// 	{
	// 		pGroundModel->m_pTriangleCache->AddTriangle(pObject);
	// 	}
	bool found = false;
	for (int i = 0; i < ARRAY_COUNT(m_aPendingProbes); i++)
	{
		PendingRayProbes& pendingProbe = pGroundModel->m_aPendingProbes[i];
		if (pendingProbe.m_frameNumber == pCallbackContext->m_frameNumber)
		{
			found = true;
			if (!pObject)
			{
				pendingProbe.m_aResults[pCallbackContext->m_probeIndex].m_valid = false;
				// MsgCon("Miss\n");
			}
			else
			{
				ANIM_ASSERT(IsReasonable(pObject->m_probeResults[0].m_contactWs));
				ANIM_ASSERT(IsReasonable(pObject->m_probeResults[0].m_normalWs));

				ProbeResult& result = pendingProbe.m_aResults[pCallbackContext->m_probeIndex];

				result.m_valid		  = true;
				result.m_segmentValid = false;
				result.m_contact	  = pObject->m_probeResults[0].m_contactWs;
				result.m_normal		  = pObject->m_probeResults[0].m_normalWs;

				// Convert the verts to segment space
				Transform& worldToSegment = pendingProbe.m_worldToSegment;

				Point p0 = Point(pObject->m_probeResults[0].m_vertexWs0) * worldToSegment;
				Point p1 = Point(pObject->m_probeResults[0].m_vertexWs1) * worldToSegment;
				Point p2 = Point(pObject->m_probeResults[0].m_vertexWs2) * worldToSegment;

				hkVector4 segmetPlane(0.0f, 0.0f, 1.0f, 0.0f);
				hkVector4 edgeIntersections[6];
				// if we replace this check with our code, we can remove the above HavokIsEnabled check
				const int numEdges = hkcdTriangleUtil::clipWithPlane(hkVector4(p0.QuadwordValue()),
																	 hkVector4(p1.QuadwordValue()),
																	 hkVector4(p2.QuadwordValue()),
																	 segmetPlane,
																	 0.001f,
																	 edgeIntersections);
				if (numEdges > 0)
				{
					Point edgeASS = Point(edgeIntersections[0].getQuad());
					Point edgeBSS = Point(edgeIntersections[1].getQuad());

					// g_prim.Draw(DebugLine(edgeASS * pendingProbe.m_segmentToWorld, edgeBSS * pendingProbe.m_segmentToWorld), kPrimDuration1FramePauseable);

					// Clip based on the extents of the segment ie the unit square in XY plane
					Point planePts[] = {
						Point(kOrigin), Point(kOrigin), Point(1.0f, 1.0f, 0.0), Point(1.0f, 1.0f, 0.0)
					};
					Vector planeNorms[] = {
						Vector(kUnitXAxis), Vector(kUnitYAxis), -Vector(kUnitXAxis), -Vector(kUnitYAxis)
					};

					if (SegmentIntersectHalfSpace(planePts[0], planeNorms[0], edgeASS, edgeBSS, edgeASS, edgeBSS)
						&& SegmentIntersectHalfSpace(planePts[1], planeNorms[1], edgeASS, edgeBSS, edgeASS, edgeBSS)
						&& SegmentIntersectHalfSpace(planePts[2], planeNorms[2], edgeASS, edgeBSS, edgeASS, edgeBSS)
						&& SegmentIntersectHalfSpace(planePts[3], planeNorms[3], edgeASS, edgeBSS, edgeASS, edgeBSS))
					{
						result.m_segmentValid	  = true;
						Transform& segmentToWorld = pendingProbe.m_segmentToWorld;

						result.m_segmentPos[0] = edgeASS * segmentToWorld;
						result.m_segmentPos[1] = edgeBSS * segmentToWorld;

						// DebugDrawLine(result.m_segmentPos[0], result.m_segmentPos[1], kColorMagenta, kPrimDuration1FramePauseable);
					}
				}
				// MsgCon("hit\n");
			}

			pendingProbe.m_resultsValid.SetBit(pCallbackContext->m_probeIndex);
			pGroundModel->m_numMeshRaySetsDone++;

			break;
		}
	}

	if (!found)
	{
		// MsgCon("Pending probes not found!\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GroundModel::SetOwner(MutableProcessHandle hOwner)
{
	m_hOwner = hOwner;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int GroundModel::GetFreePendingRayProbes() const
{
	// const I64 currentFrameNumber = GetCurrentFrameNumber();
	// const I64 dist = (currentFrameNumber - ARRAY_COUNT(m_aPendingProbes));
	I64 minFrameNumber = GetCurrentFrameNumber();
	int bestIndex	   = -1;
	for (int i = 0; i < ARRAY_COUNT(m_aPendingProbes); i++)
	{
		if (m_aPendingProbes[i].m_frameNumber <= -1)
		{
			return i;
		}
		else if (m_aPendingProbes[i].m_frameNumber < minFrameNumber)
		{
			bestIndex	   = i;
			minFrameNumber = m_aPendingProbes[i].m_frameNumber;
		}
	}
	return bestIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int GroundModel::GetNextCompleteRayProbes() const
{
	I64 maxCompletedFrame = -1;
	int probesIndex		  = -1;
	for (int i = 0; i < ARRAY_COUNT(m_aPendingProbes); i++)
	{
		if (maxCompletedFrame < m_aPendingProbes[i].m_frameNumber
			&& m_aPendingProbes[i].m_resultsValid.CountSetBits() == kNumRays)
		{
			maxCompletedFrame = m_aPendingProbes[i].m_frameNumber;
			probesIndex		  = i;
		}
	}
	return probesIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GroundModel::FindGroundForceCol(Point_arg startPos, Point_arg endPos, Point_arg alignPos)
{
	DoFindGround(startPos, endPos, alignPos, 20, false);
}
