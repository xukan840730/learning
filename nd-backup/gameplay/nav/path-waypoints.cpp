/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/path-waypoints.h"

#include "corelib/math/mathutils.h"
#include "corelib/math/segment-util.h"
#include "corelib/util/nd-stl.h"

#include "ndlib/math/pretty-math.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawHalfCircle(Point_arg center,
						 Vector_arg exposedVec,
						 float radius,
						 Color clr,
						 DebugPrimTime tt = kPrimDuration1FrameAuto)
{
	const Vector lat = SafeNormalize(Cross(kUnitYAxis, exposedVec), kZero);
	const Vector norm = SafeNormalize(Cross(exposedVec, lat), kUnitYAxis);

	const Point start = center + (lat * radius);

	const I32F kVertCount = 16;

	Point prev = start;

	const Vector baseVec = start - center;

	for (I32F iV = 0; iV < kVertCount; ++iV)
	{
		const float tt2 = (iV + 1) / float(kVertCount);
		const float angleRad = tt2 * PI;

		const Vector newVec = RotateVectorAbout(baseVec, norm, angleRad);

		const Point next = center + newVec;

		g_prim.Draw(DebugLine(prev, next, clr));

		prev = next;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IPathWaypoints::DebugDraw(const Locator& pathSpace,
							   bool drawIndices,
							   Color color0 /* = kColorRed */,
							   Color color1 /* = kColorGreen */,
							   float radius /* = 0.0f */,
							   DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	const I32F count = GetWaypointCount();
	const Vector offset = Vector(0, 0.2f, 0);

	if (0 == count)
		return;

	if (drawIndices)
	{
		for (I32F i = 0; i < count; ++i)
		{
			const Point pos = pathSpace.TransformPoint(GetWaypoint(i));

			g_prim.Draw(DebugCross(pos, 0.05f, kColorWhiteTrans), tt);
			g_prim.Draw(DebugLine(pos, 2.0f * offset, kColorWhiteTrans), tt);
			g_prim.Draw(DebugString(pos + (2.0f * offset), StringBuilder<64>("%d", i).c_str(), kColorWhiteTrans, 0.5f), tt);
		}
	}

	if ((count == 1) && (radius > kSmallestFloat))
	{
		const Point pos = pathSpace.TransformPoint(GetWaypoint(0));

		g_prim.Draw(DebugCircle(pos, kUnitYAxis, radius, color0));
		g_prim.Draw(DebugCross(pos, radius * 0.25f, color0));
		return;
	}

	Point prevPos = pathSpace.TransformPoint(GetWaypoint(0));
	Point prevPrevPos = prevPos;

	Point prev0 = prevPos;
	Point prev1 = prevPos;

	for (I32F i = 1; i < count; ++i)
	{
		const Point pos = pathSpace.TransformPoint(GetWaypoint(i));

		g_prim.Draw(DebugLine(prevPos + offset, pos + offset, color0, color1, 3.0f), tt);

		if (radius > kSmallestFloat)
		{
			const Vector leg = pos - prevPos;
			const Vector legDir = SafeNormalize(leg, kZero);
			const float legLen = Length(leg);

			const Vector perp = Cross(kUnitYAxis, leg);
			const Vector perpDir = SafeNormalize(perp, kZero);

			Point p00 = prevPos + (perpDir * radius);
			Point p01 = pos + (perpDir * radius);

			Point p10 = prevPos - (perpDir * radius);
			Point p11 = pos - (perpDir * radius);

			float t = -1.0f;

			if (i < (count - 1))
			{
				const Point nextPos = pathSpace.TransformPoint(GetWaypoint(i + 1));

				if (IntersectSegmentStadiumXz(Segment(p00, p01), Segment(pos, nextPos), radius, t))
				{
					p01 = Lerp(p00, p01, t);
				}

				if (IntersectSegmentStadiumXz(Segment(p10, p11), Segment(pos, nextPos), radius, t))
				{
					p11 = Lerp(p10, p11, t);
				}
			}

			if (i > 1)
			{
				const Segment probeSegRev0 = Segment(p01 + (SafeNormalize(p00 - p01, kZero) * 0.01f), p00);

				if (IntersectSegmentStadiumXz(probeSegRev0, Segment(prevPrevPos, prevPos), radius, t))
				{
					p00 = Lerp(probeSegRev0.a, probeSegRev0.b, t);
				}

				const Segment probeSegRev1 = Segment(p11 + (SafeNormalize(p10 - p11, kZero) * 0.01f), p10);

				if (IntersectSegmentStadiumXz(probeSegRev1, Segment(prevPrevPos, prevPos), radius, t))
				{
					p10 = Lerp(probeSegRev1.a, probeSegRev1.b, t);
				}
			}

			p00 = PointFromXzAndY(p00, prevPos);
			p10 = PointFromXzAndY(p10, prevPos);

			p01 = PointFromXzAndY(p01, pos);
			p11 = PointFromXzAndY(p11, pos);

			g_prim.Draw(DebugLine(p00 + offset, p01 + offset, color0, color1), tt);
			g_prim.Draw(DebugLine(p10 + offset, p11 + offset, color0, color1), tt);

			if (i == 1)
			{
				DebugDrawHalfCircle(prevPos + offset, pos - prevPos, radius, color0, tt);
			}

			if (i == count - 1)
			{
				DebugDrawHalfCircle(pos + offset, prevPos - pos, radius, color1, tt);
			}

			if (i > 1)
			{
				const Vector v0start = SafeNormalize(prev0 - prevPos, kZero);
				const Vector v0end = SafeNormalize(p00 - prevPos, kZero);
				const Vector v1start = SafeNormalize(prev1 - prevPos, kZero);
				const Vector v1end = SafeNormalize(p10 - prevPos, kZero);

				const bool inside = Dot(kUnitYAxis, Cross(prevPos - prevPrevPos, pos - prevPos)) < 0.0f;

				for (I32F j = 1; j < 11; ++j)
				{
					const float t0 = float(j - 1) * 0.1f;
					const float t1 = float(j) * 0.1f;

					const Vector v00 = Slerp(v0start, v0end, t0) * radius;
					const Vector v01 = Slerp(v0start, v0end, t1) * radius;
					const Vector v10 = Slerp(v1start, v1end, t0) * radius;
					const Vector v11 = Slerp(v1start, v1end, t1) * radius;

					if (inside)
					{
						g_prim.Draw(DebugLine(prevPos + v00 + offset, prevPos + v01 + offset, color0, color1), tt);
					}
					else
					{
						g_prim.Draw(DebugLine(prevPos + v10 + offset, prevPos + v11 + offset, color0, color1), tt);
					}
				}
			}

			prev0 = p01;
			prev1 = p11;
		}

		prevPrevPos = prevPos;
		prevPos = pos;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float IPathWaypoints::ComputeCosineSharpestTurnAngle(float lookaheadDist, float skipDist /* = 0.0f */) const
{
	float cosAngle = 1.0f;  // cos(0)
	// need at least 3 waypoints to have any turns in the path
	if (GetWaypointCount() >= 3)
	{
		float dist = 0.0f;
		Point prevPos = GetWaypoint(0);
		I32F iMaxPt = GetWaypointCount() - 1;
		for (I32F iPt = 1; iPt < iMaxPt; ++iPt)
		{
			Point curPos = GetWaypoint(iPt);
			dist += DistXz(prevPos, curPos);

			if (dist > lookaheadDist)
				break;

			if (dist > skipDist)
			{
				Point nextPos = GetWaypoint(iPt + 1);
				Vector prevToCur = VectorXz(curPos - prevPos);
				Vector curToNext = VectorXz(nextPos - curPos);
				prevToCur = SafeNormalize(prevToCur, Vector(kZero));
				curToNext = SafeNormalize(curToNext, Vector(kZero));
				float dotProd = Dot(prevToCur, curToNext);
				cosAngle = Min(cosAngle, dotProd); // sharpest turn is where the dot product is minimized (-1 being sharpest possible turn)
			}

			prevPos = curPos;
		}
	}
	return cosAngle;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float IPathWaypoints::ComputePathLength() const
{
	I32F count = GetWaypointCount();
	if (count <= 1)
		return 0.0f;
	Scalar length = 0.0f;
	for (I32F i = 0; i < count - 1; ++i)
	{
		Point start = GetWaypoint(i);
		Point end = GetWaypoint(i + 1);
		length += Dist(start, end);
	}
	return length;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float IPathWaypoints::ComputePathLengthXz() const
{
	I32F count = GetWaypointCount();
	if (count <= 1)
		return 0.0f;
	Scalar length = 0.0f;
	for (I32F i = 0; i < count - 1; ++i)
	{
		Point start = GetWaypoint(i);
		Point end = GetWaypoint(i + 1);
		length += DistXz(start, end);
	}
	return length;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F IPathWaypoints::FindClosestIntersectingLeg(Point_arg searchOrigin, float searchRadius, Point* pClosestPosOut) const
{
	I32F closestLeg0 = -1;
	float bestDist = kLargeFloat;
	Point bestPos = kOrigin;

	I32F count = GetWaypointCount();
	for (I32F i = 0; i < count - 1; ++i)
	{
		const Point start = PointFromXzAndY(GetWaypoint(i), searchOrigin);
		const Point end = PointFromXzAndY(GetWaypoint(i + 1), searchOrigin);

		const float distStart = Dist(searchOrigin, start);
		const float distEnd = Dist(searchOrigin, end);
		const float maxDist = Max(distStart, distEnd);
		const float minDist = Min(distStart, distEnd);

		const float closestLegDist = Limit(searchRadius, distStart, distEnd);

		if (closestLegDist < bestDist)
		{
			closestLeg0 = i;
			bestDist = closestLegDist;

			const Vector toStart = start - searchOrigin;
			const Vector leg = end - start;

			const float cc = Dot(toStart, toStart) - searchRadius*searchRadius;
			const float bb = 2.0f * Dot(toStart, leg);
			const float aa = Dot(leg, leg);

			const float determ = bb*bb - 4.0*aa*cc;

			float tt = 0.0f;

			if (determ < 0.0f || aa < 1.0e-16f) // aa < eps is a test for zero length segment
			{

			}
			else
			{
				const float tt0 = (-bb - Sqrt(determ)) / (SCALAR_LC(2.0f)*aa);
				const float tt1 = (-bb + Sqrt(determ)) / (SCALAR_LC(2.0f)*aa);

				if (tt0 >= 0.0f && tt1 >= 0.0f)
				{
					tt = Min(tt0, tt1);
				}
				else if (tt0 >= 0.0f)
				{
					tt = tt0;
				}
				else if (tt1 >= 0.0f)
				{
					tt = tt1;
				}
			}

			bestPos = start + (tt * leg);
			const float testDist = Dist(bestPos, searchOrigin);

			if (Abs(bestDist - searchRadius) < kSmallestFloat)
			{
				break;
			}
		}
	}

	if (pClosestPosOut)
	{
		*pClosestPosOut = bestPos;
	}

	return closestLeg0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IPathWaypoints::IntersectSegmentXz(const Segment& seg, Point* pClosestPtOut /* = nullptr */) const
{
	const I32F count = GetWaypointCount();

	if (count < 1)
		return false;

	Point closestPoint = GetWaypoint(0);
	float bestDist = kLargeFloat;
	bool intersected = false;

	for (I32F i = 0; i < count - 1; ++i)
	{
		const Point start = GetWaypoint(i);
		const Point end = GetWaypoint(i + 1);

		const Segment pathSeg = Segment(start, end);

		Scalar t0, t1;

		if (IntersectSegmentSegmentXz(seg, pathSeg, t0, t1))
		{
			closestPoint = Lerp(pathSeg, t1);
			bestDist = 0.0f;
			intersected = true;
			break;
		}
		else
		{
			const Point pathPos = Lerp(pathSeg, t1);

			const float dist = DistPointSegmentXz(pathPos, seg);

			if (dist < bestDist)
			{
				bestDist = dist;
				closestPoint = pathPos;
			}
		}
	}

	if (pClosestPtOut)
	{
		*pClosestPtOut = closestPoint;
	}

	return intersected;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IPathWaypoints::CopyFrom(const IPathWaypoints* pSourceWaypoints)
{
	Clear();

	if (!pSourceWaypoints)
		return;

	const I32F sourceCount = pSourceWaypoints->GetWaypointCount();
	const I32F maxCount = GetMaxWaypointCount();

	const I32F numToCopy = Min(sourceCount, maxCount);

	for (I32F i = 0; i < numToCopy; ++i)
	{
		const Point pos = pSourceWaypoints->GetWaypoint(i);
		AddWaypoint(pos);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point IPathWaypoints::GetPointAtDistance(const float dist, Vector* pLegOut /* = nullptr */, I32F* pILegOut /* = nullptr */) const
{
	const I32F waypointCount = GetWaypointCount();

	if (waypointCount < 2)
	{
		const Point pos = GetWaypoint(0);
		if (pLegOut)
			*pLegOut = kZero;
		if (pILegOut)
			*pILegOut = -1;
		return pos;
	}

	Point prevPos = GetWaypoint(0);
	Point returnPos = prevPos;
	Point nextPos = prevPos;
	float remDist = dist;

	I32F iLegOut = -1;
	for (I32F i = 1; i < waypointCount; ++i)
	{
		prevPos = nextPos;
		nextPos = GetWaypoint(i);

		const float legDist = Dist(nextPos, prevPos);

		if (legDist >= remDist)
		{
			const float alpha = Limit01(remDist/legDist);
			returnPos = Lerp(prevPos, nextPos, alpha);
			iLegOut = i - 1;
			break;
		}
		else
		{
			remDist -= legDist;
		}

		returnPos = nextPos;
		iLegOut = i - 1;
	}

	if (pLegOut)
	{
		*pLegOut = returnPos - prevPos;
	}

	if (pILegOut)
	{
		*pILegOut = iLegOut;
	}

	return returnPos;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// return how far along the PathWaypoints you must travel to get to the closest
// point on the PathWaypoints to the argument pos
float IPathWaypoints::GetDistanceAtPoint(Point_arg pos) const
{
	const I32F waypointCount = GetWaypointCount();

	if (waypointCount < 2)
		return 0.0f;

	F32 legDist;
	I32 legIdx;
	F32 legTt;

	ClosestPoint(pos, &legDist, &legIdx, &legTt);
	NAV_ASSERT(legIdx >= 0);

	Point prevPos = GetWaypoint(0);
	F32 totalDist = 0.f;

	for (I32 i = 1; i <= legIdx; ++i)
	{
		const Point nextPos = GetWaypoint(i);
		totalDist += Dist(nextPos, prevPos);
		prevPos = nextPos;
	}

	const Point finalPos = GetWaypoint(legIdx + 1);
	totalDist += legTt * Dist(finalPos, prevPos);

	return totalDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float IPathWaypoints::ComputeRemainingLength(Point_arg startPos) const
{
	const I32F numPts = GetWaypointCount();

	if (numPts < 2)
		return 0.0f;

	I32F i;
	Point prevPos;
	Point nextPos = ClosestPoint(startPos, nullptr, &i);

	float dist = 0.0f;
	for (++i; i < numPts; ++i)
	{
		prevPos = nextPos;
		nextPos = GetWaypoint(i);

		dist += Dist(prevPos, nextPos);
	}

	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point IPathWaypoints::AdvanceAlongPath(Point_arg startPos,
									   float dist,
									   I32F iStartingLeg /* = -1 */,
									   I32F* pNewLegOut /* = nullptr */) const
{
	const I32F numPts = GetWaypointCount();

	if (numPts <= 0)
	{
		return startPos;
	}
	else if (numPts == 1)
	{
		return GetWaypoint(0);
	}

	I32F iLeg = iStartingLeg;
	Point prevPos;
	Point nextPos = startPos;

	if (iLeg < 0)
	{
		ClosestPoint(startPos, nullptr, &iLeg);
	}

	Point advancedPos = nextPos;

	for (++iLeg; iLeg < numPts; ++iLeg)
	{
		prevPos = nextPos;
		nextPos = GetWaypoint(iLeg);

		const float legDist = Dist(prevPos, nextPos);

		if (dist <= legDist)
		{
			if (legDist < 0.001f)
			{
				advancedPos = nextPos;
			}
			else
			{
				const float tt = dist / legDist;
				advancedPos = Lerp(prevPos, nextPos, tt);
			}
			break;
		}
		else
		{
			dist -= legDist;
			advancedPos = nextPos;
		}
	}

	if (pNewLegOut)
	{
		*pNewLegOut = iLeg - 1;
	}

	NAV_ASSERT(IsReasonable(advancedPos));

	return advancedPos;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point IPathWaypoints::AdvanceAlongPathXz(Point_arg startPos,
										 float dist,
										 I32F iStartingLeg /* = -1 */,
										 I32F* pNewLegOut /* = nullptr */) const
{
	const I32F numPts = GetWaypointCount();

	if (numPts <= 0)
	{
		return startPos;
	}
	else if (numPts == 1)
	{
		return GetWaypoint(0);
	}

	I32F iLeg = iStartingLeg;
	Point prevPos;
	Point nextPos = startPos;

	if (iLeg < 0)
	{
		ClosestPointXz(startPos, nullptr, &iLeg);
	}

	Point advancedPos = nextPos;

	for (++iLeg; iLeg < numPts; ++iLeg)
	{
		prevPos = nextPos;
		nextPos = GetWaypoint(iLeg);

		const float legDist = DistXz(prevPos, nextPos);

		if (dist <= legDist)
		{
			if (legDist < 0.001f)
			{
				advancedPos = nextPos;
			}
			else
			{
				const float tt = dist / legDist;
				advancedPos = Lerp(prevPos, nextPos, tt);
			}
			break;
		}
		else
		{
			dist -= legDist;
			advancedPos = nextPos;
		}
	}

	if (pNewLegOut)
	{
		*pNewLegOut = iLeg - 1;
	}

	NAV_ASSERT(IsReasonable(advancedPos));

	return advancedPos;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point IPathWaypoints::ClosestPoint(Point_arg pos,
								   float* pDistanceOut /* = nullptr */,
								   I32F* pClosestLegOut /* = nullptr */,
								   float* pLegTTOut /* = nullptr */) const
{
	Point closestPos = kOrigin;
	float bestD = kLargeFloat;
	float bestTT = 0.0f;
	I32F closestLeg = 0;

	const I32F count = GetWaypointCount();

	if (count <= 0)
	{
		if (pDistanceOut)
			*pDistanceOut = -1.0f;
		if (pClosestLegOut)
			*pClosestLegOut = -1;

		return pos;
	}
	else if (count == 1)
	{
		if (pDistanceOut)
			*pDistanceOut = Dist(pos, GetWaypoint(0));
		if (pClosestLegOut)
			*pClosestLegOut = 0;

		return pos;
	}

	Point prevPos = GetWaypoint(0);
	for (I32F i = 1; i < count; ++i)
	{
		Point nextPos = GetWaypoint(i);

		Scalar tt;
		Point p = kOrigin;
		const float d = DistPointSegment(pos, prevPos, nextPos, &p, &tt);

		if (d < bestD)
		{
			bestD = d;
			closestPos = p;
			closestLeg = i - 1;
			bestTT = tt;
		}

		prevPos = nextPos;
	}

	if (pDistanceOut)
		*pDistanceOut = bestD;

	if (pClosestLegOut)
		*pClosestLegOut = closestLeg;

	if (pLegTTOut)
		*pLegTTOut = bestTT;

	return closestPos;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point IPathWaypoints::ClosestPointXz(Point_arg pos,
									 float* pDistanceOut /* = nullptr */,
									 I32F* pClosestLegOut /* = nullptr */,
									 float* pLegTTOut /* = nullptr */,
									 float bias /* = 0.0f */) const
{
	Point closestPos = kOrigin;
	float bestD = kLargeFloat;
	float bestTT = 0.0f;
	I32F closestLeg = 0;

	const I32F count = GetWaypointCount();

	if (count <= 0)
	{
		if (pDistanceOut)
			*pDistanceOut = -1.0f;
		if (pClosestLegOut)
			*pClosestLegOut = -1;

		return pos;
	}
	else if (count == 1)
	{
		if (pDistanceOut)
			*pDistanceOut = DistXz(pos, GetWaypoint(0));
		if (pClosestLegOut)
			*pClosestLegOut = 0;

		return pos;
	}

	Point prevPos = GetWaypoint(0);

	for (I32F i = 1; i < count; ++i)
	{
		Point nextPos = GetWaypoint(i);

		Scalar tt;
		Point p = kOrigin;
		const float d = DistPointSegmentXz(pos, prevPos, nextPos, &p, &tt);

		if (d + (bestTT == 1.0f && closestLeg == i - 2 ? 0.0f : bias) < bestD)
		{
			bestD = d;
			closestPos = p;
			closestLeg = i - 1;
			bestTT = tt;
		}

		prevPos = nextPos;
	}

	if (pDistanceOut)
		*pDistanceOut = bestD;

	if (pClosestLegOut)
		*pClosestLegOut = closestLeg;

	if (pLegTTOut)
		*pLegTTOut = bestTT;

	return closestPos;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float IPathWaypoints::RemainingPathDistFromPosXz(Point_arg pos) const
{
	const float totalDist = ComputePathLengthXz();
	I32F closestLeg = -1;
	const Point closestPos = ClosestPointXz(pos, nullptr, &closestLeg);

	if (closestLeg < 0)
	{
		return 0.0f;
	}

	float toPointTravelDist = 0.0f;
	for (I32F i = 0; i < closestLeg; ++i)
	{
		toPointTravelDist += DistXz(GetWaypoint(i), GetWaypoint(i + 1));
	}

	toPointTravelDist += DistXz(GetWaypoint(closestLeg), closestPos);

	const float remDist = Max(totalDist - toPointTravelDist, 0.0f);

	return remDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IPathWaypoints::Reverse()
{
	const I32F count = GetWaypointCount();

	for (I32F i = 0; i < count / 2; ++i)
	{
		const Point low = GetWaypoint(i);
		const Point high = GetWaypoint(count - i - 1);

		UpdateWaypoint(count - i - 1, low);
		UpdateWaypoint(i, high);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IPathWaypoints::Append(const IPathWaypoints& rhs)
{
	const I32F addCount = rhs.GetWaypointCount();
	for (I32F i = 0; i < addCount; ++i)
	{
		if (IsFull())
			break;

		AddWaypoint(rhs.GetWaypoint(i));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IPathWaypoints::Validate() const
{
	const I32F count = GetWaypointCount();
	NAV_ASSERT(count <= GetMaxWaypointCount());

	for (I32F i = 0; i < count; ++i)
	{
		const Point p = GetWaypoint(i);
		NAV_ASSERTF(IsReasonable(p), ("Unreasonable waypoint at index '%d' %s", i, PrettyPrint(p)));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IPathWaypoints::ChangeParentSpace(const Transform& matOldToNew)
{
	const I32F count = GetWaypointCount();
	for (I32F i = 0; i < count; ++i)
	{
		const Point newPos = GetWaypoint(i) * matOldToNew;
		UpdateWaypoint(i, newPos);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePathReachXz(const IPathWaypoints* pPath, Point_arg pos)
{
	NAV_ASSERT(IsReasonable(pos));

	if (!pPath || !pPath->IsValid())
		return -1.0f;

	const I32F numWaypoints = pPath->GetWaypointCount();

	float reach = 0.0f;

	for (I32F i = 0; i < numWaypoints; ++i)
	{
		const Point pathPos = pPath->GetWaypoint(i);

		const float dist = DistXz(pathPos, pos);
		reach = Max(dist, reach);
	}

	return reach;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IntersectPathWaypoints(const IPathWaypoints* pSrcPath,
							const Locator& srcParentSpace,
							const IPathWaypoints* pDestPath,
							const Locator& destParentSpace,
							F32 yThreshold,
							F32* pDist,
							Point* pSrcClosestPosWs,
							Point* pDestClosestPosWs)
{
	if (!pSrcPath || !pSrcPath->IsValid())
		return false;

	if (!pDestPath || !pDestPath->IsValid())
		return false;

	const I32F numSrcWaypoints	= pSrcPath->GetWaypointCount();
	const I32F numDestWaypoints = pDestPath->GetWaypointCount();

	const F32 kBiasDist = 0.01f;
	F32 bestDist		= NDI_FLT_MAX;

	for (U32F srcWaypointIdx = 1; srcWaypointIdx < numSrcWaypoints; srcWaypointIdx++)
	{
		const Point srcPosPs1 = pSrcPath->GetWaypoint(srcWaypointIdx - 1);
		const Point srcPosPs2 = pSrcPath->GetWaypoint(srcWaypointIdx);
		const Point srcPosWs1 = srcParentSpace.TransformPoint(srcPosPs1);
		const Point srcPosWs2 = srcParentSpace.TransformPoint(srcPosPs2);

		const Segment srcSegWs = Segment(srcPosWs1, srcPosWs2);

		for (U32F destWaypointIdx = 1; destWaypointIdx < numDestWaypoints; destWaypointIdx++)
		{
			const Point destPosPs1 = pDestPath->GetWaypoint(destWaypointIdx - 1);
			const Point destPosPs2 = pDestPath->GetWaypoint(destWaypointIdx);
			const Point destPosWs1 = destParentSpace.TransformPoint(destPosPs1);
			const Point destPosWs2 = destParentSpace.TransformPoint(destPosPs2);

			const Segment destSegWs = Segment(destPosWs1, destPosWs2);

			Point srcClosestPosWs;
			Point destClosestPosWs;
			DistSegmentSegment(srcSegWs, destSegWs, srcClosestPosWs, destClosestPosWs);

			const Vector delta = srcClosestPosWs - destClosestPosWs;
			if (Abs(delta.Y()) > yThreshold)
				continue;

			const F32 dist = LengthXz(delta);
			if (dist > (bestDist - kBiasDist))
				continue;

			bestDist = dist;

			if (pDist)
				*pDist = dist;

			if (pSrcClosestPosWs)
				*pSrcClosestPosWs = srcClosestPosWs;

			if (pDestClosestPosWs)
				*pDestClosestPosWs = destClosestPosWs;
		}
	}

	return (bestDist < kBiasDist);
}

