/*
 * Copyright (c) 2011 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef NDLIB_AI_CONVEX_HULL_XZ_H
#define NDLIB_AI_CONVEX_HULL_XZ_H

#include "corelib/math/basicmath.h"

#include "ndlib/render/util/prim-server-wrapper.h"

class PrimServerWrapper;
struct Color;

// a convex hull around a set of points extruded from the X-Z plane
template <U32 SIZE>
class TConvexHullXZ
{
public:
	static const U32 kMaxPlanes = SIZE;
	static const U32 kMaxVertices = SIZE;

	TConvexHullXZ();
	TConvexHullXZ(const Point pointArray[/*kMaxVertices*/], U32F pointCount, Scalar_arg yThreshold = SCALAR_LC(3.0f));

	void Build(const Point pointArray[/*kMaxVertices*/], U32F pointCount, Scalar_arg yThreshold = SCALAR_LC(3.0f));

	// move pos to closest point inside hull, or no change if already inside hull
	// does not clamp to hull floor or ceiling, only walls
	bool ClampToHull(Point& pos) const;

	// does not choose point on hull floor or ceiling, only walls
	Point GetClosestPointOnHullBoundary(Point_arg pos) const;

	bool IsInside(Point_arg pos) const;

	// get the distance from this point to the hull, or zero if this point is inside the hull
	Scalar GetOutsideDistance(Point_arg pos) const;

	// displacement from closest point on hull boundary to pos, positive if inside hull, negative if outside hull
	Scalar GetDisplacementIntoHull(Point_arg pos) const;

	void DebugDrawEdges(PrimServerWrapper& prim, const Color& displayColor) const;
	void DebugDrawFaces(PrimServerWrapper& prim, const Color& displayColor) const;

	Plane m_planes[kMaxPlanes];
	Point m_vertices[kMaxVertices];
	U32 m_planeCount;
	U32 m_vertexCount;
	F32 m_yMin;
	F32 m_yMax;

private:
	void AddEdgePlane(Point_arg a, Point_arg b);

	static F32 GetRelativeXZAngle(Vector_arg inVector, Vector_arg zeroAngleVector)
	{
		F32 zeroAngle = Atan2(zeroAngleVector.Z(), zeroAngleVector.X());
		F32 angle		= Atan2(inVector.Z(), inVector.X());
		angle -= zeroAngle;

		if (angle < -PI)
		{
			angle += 2.0f * PI;
		}
		else if (angle > PI)
		{
			angle -= 2.0f * PI;
		}
		return angle;
	}
};

typedef TConvexHullXZ<32> ConvexHullXZ;


template <U32 SIZE>
TConvexHullXZ<SIZE>::TConvexHullXZ() : m_planeCount(0), m_vertexCount(0), m_yMin(0.f), m_yMax(0.f)
{
}

template <U32 SIZE>
TConvexHullXZ<SIZE>::TConvexHullXZ(const Point pointArray[/*kMaxVertices*/],
						   U32F pointCount,
						   Scalar_arg yThreshold /*=SCALAR_LC(3.0f)*/)
	: m_planeCount(0), m_vertexCount(0), m_yMin(0.f), m_yMax(0.f)
{
	Build(pointArray, pointCount, yThreshold);
}

template <U32 SIZE>
void TConvexHullXZ<SIZE>::Build(const Point pointArray[/*kMaxVertices*/],
						 U32F pointCount,
						 Scalar_arg yThreshold /*=SCALAR_LC(3.0f)*/)
{
	m_planeCount  = 0;
	m_vertexCount = 0;
	m_yMin		  = 0.0f;
	m_yMax		  = 0.0f;

	if (pointCount == 0)
	{
		return;
	}

	if (pointCount > kMaxVertices)
	{
		ASSERT(false);
		pointCount = kMaxVertices;
	}

	// 1) collect input verts
	I32F inputVertexToPoint[kMaxVertices];
	U32F inputCount = pointCount;
	for (I32F iVert = 0; iVert < inputCount; ++iVert)
	{
		inputVertexToPoint[iVert] = iVert;
	}

	// 2) get starting output vert

	I32F outputVertexToPoint[kMaxVertices];
	U32F outputCount = 0;

	// find the vert furthest along the -X axis
	I32F iBestVert = 0;
	Point bestVert = pointArray[inputVertexToPoint[0]];

	for (I32F iVert = 1; iVert < inputCount; ++iVert)
	{
		const Point vert = pointArray[inputVertexToPoint[iVert]];
		if (vert.X() < bestVert.X())
		{
			bestVert  = vert;
			iBestVert = iVert;
		}
	}

	const I32F iFirstVert = iBestVert;
	const Point firstVert = bestVert;

	// 3) get each additional output vert

	// each vertex is found by taking the minimum angle between zero and some negative worst angle
	// where the angle is the difference from the angle of the previous edge in the range [-PI, PI]
	// (imagine a unit circle where +X is right and +Z is up)

	static const F32 kRadiansEpsilon = 0.0001f;
	F32 worstAngle = -PI; // the worst angle is the most negative that a valid angle can be

	// we measure angle relative to the previous edge.
	// For the first point consider the previous edge to be +Z
	// Since we have taken the point furthest along the -X axis, we can be sure the next point will have a negative angle relative to this edge
	Vector previousEdge(kUnitZAxis);
	bool exitEarly = false;
	while (iBestVert != -1)
	{
		// set best to current
		const I32F iCurrentVert = iBestVert;
		const Point currentVert = bestVert;
		iBestVert = -1;
		if (exitEarly)
		{
			break;
		}

		// move current vert from input buffer to output buffer
		// it will no longer be considered
		outputVertexToPoint[outputCount++] = inputVertexToPoint[iCurrentVert];
		inputVertexToPoint[iCurrentVert]   = inputVertexToPoint[--inputCount];

		// get next vert by best angle
		F32 bestAngle = worstAngle; // take the maximum angle less than zero and greater than worst angle

		iBestVert = -1;
		for (I32F iVert = 0; iVert < inputCount; ++iVert)
		{
			const Point vert = pointArray[inputVertexToPoint[iVert]];
			Vector edge		 = vert - currentVert;

			// a vertex that would create a near near-zero length edges is skipped
			if (LengthSqr(edge) > SCALAR_LC(NDI_FLT_EPSILON))
			{
				F32 angle = GetRelativeXZAngle(edge, previousEdge);
				if (angle < 0.f && angle > bestAngle)
				{
					bestAngle = angle;
					iBestVert = iVert;
					bestVert  = vert;
				}
			}
		}

		if (iBestVert != -1)
		{
			// update the previous edge
			previousEdge = bestVert - currentVert;

			// update worst angle
			// if this is the first edge found, the worst angle should remain -PI degrees
			if (iCurrentVert != iFirstVert)
			{
				Vector worstEdge = firstVert - bestVert;
				if (LengthSqr(worstEdge) > SCALAR_LC(NDI_FLT_EPSILON))
				{
					worstAngle = GetRelativeXZAngle(worstEdge, previousEdge);

					// edge cases:
					if (worstAngle >= 0.5f * PI)
					{
						// previous edge points opposite the worst edge, so it
						// wrapped around from negative to positive
						worstAngle -= 2 * PI; // take the negative equivalent
					}
					else if (worstAngle >= 0.f)
					{
						// it is also possible the worst angle is slightly
						// greater than zero because the previous edge is in
						// the same direction as the worst edge in this case it
						// is okay to reject all other vertices
						exitEarly = true;
					}
				}
				else
				{
					// hit a vertex very close to the final vertex, exit
					exitEarly = true;
				}
			}
		}
	}

	if (outputCount < 1)
	{
		ASSERT(false);
		return;
	}

	// 4) write output vertices
	m_vertexCount = outputCount;
	for (I32F iVert = 0; iVert < outputCount; ++iVert)
	{
		m_vertices[iVert] = pointArray[outputVertexToPoint[iVert]];
	}

	// vertices are now ordered counter-clockwise beginning with the left-most vertex
	// (if you imagine looking at the XZ plane so that +Z is up and +X is right)

	// 5) build the planes
	Point edgeStart = m_vertices[m_vertexCount - 1];
	for (I32F iVert = 0; iVert < outputCount; ++iVert)
	{
		const Point edgeEnd = m_vertices[iVert];
		AddEdgePlane(edgeStart, edgeEnd);
		edgeStart = edgeEnd;
	}

	// 6) get min and max height
	m_yMin = NDI_FLT_MAX;
	m_yMax = -NDI_FLT_MAX;
	for (I32F iVert = 0; iVert < outputCount; ++iVert)
	{
		const F32 y = (F32)(m_vertices[iVert].Y());
		m_yMin		= Min(m_yMin, y);
		m_yMax		= Max(m_yMax, y);
	}
	m_yMin -= yThreshold;
	m_yMax += yThreshold;
}

template <U32 SIZE>
void TConvexHullXZ<SIZE>::AddEdgePlane(Point_arg a, Point_arg b)
{
	Vector edge		   = b - a;
	Scalar crossLength = SCALAR_LC(0.f);
	Vector planeNormal = Cross(edge, Vector(kUnitYAxis));
	planeNormal		   = SafeNormalize(planeNormal, kUnitXAxis, crossLength);
	if (crossLength > SCALAR_LC(NDI_FLT_EPSILON))
	{
		m_planes[m_planeCount++] = Plane(a, planeNormal);
	}
}

template <U32 SIZE>
bool TConvexHullXZ<SIZE>::IsInside(Point_arg pos) const
{
	if (m_vertexCount >= 3)
	{
		if (pos.Y() > m_yMax || pos.Y() < m_yMin)
		{
			return false;
		}

		for (U32F iPlane = 0; iPlane < m_planeCount; ++iPlane)
		{
			const Plane& plane = m_planes[iPlane];
			const Scalar distOutsidePlane = Dot(Vector(pos - kOrigin), plane.GetNormal()) + plane.GetD();
			if (distOutsidePlane > SCALAR_LC(0.f))
			{
				return false;
			}
		}
		return true;
	}
	return false;
}

template <U32 SIZE>
Point TConvexHullXZ<SIZE>::GetClosestPointOnHullBoundary(Point_arg pos) const
{
	Point closestPoint = pos;
	if (m_vertexCount >= 3)
	{
		Point edgeStart = m_vertices[m_vertexCount - 1];
		edgeStart.SetY(0);
		Scalar closestDistSqr = SCALAR_LC(NDI_FLT_MAX);
		for (U32F iVert = 0; iVert < m_vertexCount; ++iVert)
		{
			Point edgeEnd = m_vertices[iVert];
			edgeEnd.SetY(0);

			const Point point = ClosestPointOnEdgeToPoint(edgeStart, edgeEnd, pos);
			const Scalar distSqr = DistSqr(point, pos);
			if (distSqr < closestDistSqr)
			{
				closestDistSqr = distSqr;
				closestPoint   = point;
			}

			edgeStart = edgeEnd;
		}
	}
	else if (m_vertexCount == 2)
	{
		closestPoint = ClosestPointOnEdgeToPoint(m_vertices[0], m_vertices[1], pos);
	}
	else if (m_vertexCount == 1)
	{
		closestPoint = m_vertices[0];
	}
	else
	{
		return pos;
	}

	if (closestPoint.Y() < m_yMin)
	{
		closestPoint.SetY(m_yMin);
	}
	else if (closestPoint.Y() > m_yMax)
	{
		closestPoint.SetY(m_yMax);
	}
	else
	{
		closestPoint.SetY(pos.Y());
	}

	return closestPoint;
}

template <U32 SIZE>
bool TConvexHullXZ<SIZE>::ClampToHull(Point& pos) const
{
	if (IsInside(pos))
	{
		return false;
	}
	else
	{
		pos = GetClosestPointOnHullBoundary(pos);
		return true;
	}
}

template <U32 SIZE>
Scalar TConvexHullXZ<SIZE>::GetOutsideDistance(Point_arg pos) const
{
	Point clampedPosWs = pos;
	if (ClampToHull(clampedPosWs))
	{
		const Scalar result = Dist(pos, clampedPosWs);
		return result;
	}
	return SCALAR_LC(0.f);
}

template <U32 SIZE>
Scalar TConvexHullXZ<SIZE>::GetDisplacementIntoHull(Point_arg pos) const
{
	const Point boundaryPos = GetClosestPointOnHullBoundary(pos);
	const Scalar dist		= Dist(pos, boundaryPos);
	if (IsInside(pos))
	{
		return dist;
	}
	else
	{
		return -dist;
	}
}

template <U32 SIZE>
void TConvexHullXZ<SIZE>::DebugDrawEdges(PrimServerWrapper& prim, const Color& displayColor) const
{
	STRIP_IN_FINAL_BUILD;

	if (m_vertexCount == 0)
		return;

	Point edgeStart = m_vertices[m_vertexCount - 1];
	for (U32F iVert = 0; iVert < m_vertexCount; ++iVert)
	{
		const Point edgeEnd = m_vertices[iVert];

		Point corner0 = edgeStart;
		Point corner1 = edgeStart;
		Point corner2 = edgeEnd;
		Point corner3 = edgeEnd;
		corner0.SetY(m_yMin);
		corner1.SetY(m_yMax);
		corner2.SetY(m_yMax);
		corner3.SetY(m_yMin);

		prim.DrawLine(corner0, corner1, displayColor);
		prim.DrawLine(corner1, corner2, displayColor);
		// prim.DrawLine(corner2, corner3, displayColor); // same as (corner0, corner1) for next iteration
		prim.DrawLine(corner3, corner0, displayColor);

		edgeStart = edgeEnd;
	}
}

template <U32 SIZE>
void TConvexHullXZ<SIZE>::DebugDrawFaces(PrimServerWrapper& prim, const Color& displayColor) const
{
	STRIP_IN_FINAL_BUILD;

	if (m_vertexCount == 0)
		return;

	Point edgeStart = m_vertices[m_vertexCount - 1];
	for (U32F iVert = 0; iVert < m_vertexCount; ++iVert)
	{
		const Point edgeEnd = m_vertices[iVert];

		Point corner0 = edgeStart;
		Point corner1 = edgeStart;
		Point corner2 = edgeEnd;
		Point corner3 = edgeEnd;
		corner0.SetY(m_yMin);
		corner1.SetY(m_yMax);
		corner2.SetY(m_yMax);
		corner3.SetY(m_yMin);

		prim.DrawQuad(corner3, corner2, corner1, corner0, displayColor);

		edgeStart = edgeEnd;
	}
}

#endif // NDLIB_AI_CONVEX_HULL_XZ_H
