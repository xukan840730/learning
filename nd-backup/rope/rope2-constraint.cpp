/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "corelib/util/nd-stl.h"
#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/ndphys/rope/rope2-util.h"
#include "gamelib/ndphys/rope/rope2-point-col.h"
#include "gamelib/ndphys/rope/physvectormath.h"
#include "gamelib/ndphys/havok-internal.h"

I32 Rope2::Constraint::AddPlane(const Vec4& plane, const RopeColliderHandle& hCollider)
{
	if (m_numPlanes >= kMaxNumPlanes)
	{
		//JAROS_ASSERT(false);
		return -1; 
	}

	I32 iPlane = m_numPlanes;
	m_planes[iPlane] = plane;
	m_hCollider[iPlane] = hCollider;
	m_numPlanes++;
	return iPlane;
}

I32 Rope2::Constraint::AddEdge(I32& iPl0, I32& iPl1)
{
	if (m_numEdges >= kMaxNumEdges)
	{
		JAROS_ASSERT(false);
		return -1; 
	}

	if (iPl0 > m_firstNoEdgePlane)
	{
		Vec4 helperPlane = m_planes[iPl0];
		RopeColliderHandle hHelperCollider = m_hCollider[iPl0];
		m_planes[iPl0] = m_planes[m_firstNoEdgePlane];
		m_hCollider[iPl0] = m_hCollider[m_firstNoEdgePlane];
		m_planes[m_firstNoEdgePlane] = helperPlane;
		m_hCollider[m_firstNoEdgePlane] = hHelperCollider;
		iPl0 = m_firstNoEdgePlane;
	}
	m_firstNoEdgePlane = Max(m_firstNoEdgePlane, (U8)(iPl0+1));

	if (iPl1 > m_firstNoEdgePlane)
	{
		Vec4 helperPlane = m_planes[iPl1];
		RopeColliderHandle hHelperCollider = m_hCollider[iPl1];
		m_planes[iPl1] = m_planes[m_firstNoEdgePlane];
		m_hCollider[iPl1] = m_hCollider[m_firstNoEdgePlane];
		m_planes[m_firstNoEdgePlane] = helperPlane;
		m_hCollider[m_firstNoEdgePlane] = hHelperCollider;
		iPl1 = m_firstNoEdgePlane;
	}
	m_firstNoEdgePlane = Max(m_firstNoEdgePlane, (U8)(iPl1+1));

	I32 iEdge = m_numEdges;
	m_numEdges++;
	m_edgePlanes[iEdge*2] = iPl0;
	m_edgePlanes[iEdge*2+1] = iPl1;

	// Calc bi normals of the edge
	Vector norm0(m_planes[iPl0]);
	Vector norm1(m_planes[iPl1]);
	Vector edgeVec = Cross(norm0, norm1);
	edgeVec = Normalize(edgeVec);
	PHYSICS_ASSERT(IsFinite(edgeVec));

	Vector bi0 = Cross(edgeVec, norm0);
	Vector bi1 = Cross(norm1, edgeVec);

	Vector pnt0 = -m_planes[iPl0].W() * norm0;
	Scalar d1 = m_planes[iPl1].W();
	Scalar t = - (Dot(pnt0, norm1) + d1) / Dot(norm1, bi0);
	PHYSICS_ASSERT(IsFinite(t));

	Scalar biD0 = - Dot(bi0, pnt0) - t;

	m_biPlanes[iEdge*2] = bi0.GetVec4();
	m_biPlanes[iEdge*2].SetW(biD0);

	Vector p = pnt0 + t * bi0;
	Scalar biD1 = - Dot(bi1, p);

	m_biPlanes[iEdge*2+1] = bi1.GetVec4();
	m_biPlanes[iEdge*2+1].SetW(biD1);

	return iEdge;
}

void Rope2::Constraint::RemoveEdge(U32 iEdge, U32* pPlaneRemap, U32 numPlaneRemaps)
{
	ASSERT(iEdge < m_numEdges);

	U32 iPl0 = m_edgePlanes[iEdge*2];
	U32 iPl1 = m_edgePlanes[iEdge*2+1];

	m_edgePlanes[iEdge*2] = m_edgePlanes[(m_numEdges-1)*2];
	m_edgePlanes[iEdge*2+1] = m_edgePlanes[(m_numEdges-1)*2+1];
	m_biPlanes[iEdge*2] = m_biPlanes[(m_numEdges-1)*2];
	m_biPlanes[iEdge*2+1] = m_biPlanes[(m_numEdges-1)*2+1];
	m_numEdges--;

	U32 iMove[2];
	iMove[0] = Min(iPl0, iPl1);
	iMove[1] = Max(iPl0, iPl1);
	bool bMove[2] = { true, true };
	for (U32 ii = 0; ii<2; ii++)
	{
		for (U32 iEdgePlane = 0; iEdgePlane<m_numEdges*2; iEdgePlane++)
		{
			if (m_edgePlanes[iEdgePlane] == iMove[ii])
				bMove[ii] = false;
		}
	}

	if (bMove[0] && bMove[1] && iMove[0] < iMove[1])
	{
		Swap(iMove[0], iMove[1]);
	}

	for (U32 ii = 0; ii<2; ii++)
	{
		if (!bMove[ii])
			continue;
		if (iMove[ii] < m_firstNoEdgePlane-1)
		{
			Vec4 tmpPl = m_planes[iMove[ii]];
			m_planes[iMove[ii]] = m_planes[m_firstNoEdgePlane-1];
			m_planes[m_firstNoEdgePlane-1] = tmpPl;
			RopeColliderHandle tmpCol = m_hCollider[iMove[ii]];
			m_hCollider[iMove[ii]] = m_hCollider[m_firstNoEdgePlane-1];
			m_hCollider[m_firstNoEdgePlane-1] = tmpCol;
			if (pPlaneRemap)
			{
				for (U32 iRemap = 0; iRemap<numPlaneRemaps; iRemap++)
				{
					if (pPlaneRemap[iRemap] == iMove[ii])
					{
						pPlaneRemap[iRemap] = m_firstNoEdgePlane-1;
					}
				}
			}
			for (U32 iEdgePlane = 0; iEdgePlane<m_numEdges*2; iEdgePlane++)
			{
				if (m_edgePlanes[iEdgePlane] == m_firstNoEdgePlane-1)
					m_edgePlanes[iEdgePlane] = iMove[ii];
			}
		}
		m_firstNoEdgePlane--;
	}
}

void Rope2::Constraint::RemovePlane(U32 iPlane, U32* pRemap, U32 numRemaps)
{
	U32 numLocRemaps = numRemaps+1;
	PHYSICS_ASSERT(numLocRemaps < kMaxNumPlanes+1);
	U32 pLocRemap[kMaxNumPlanes+1];
	if (pRemap)
	{
		memcpy(pLocRemap, pRemap, numRemaps*sizeof(pRemap[0]));
	}
	pLocRemap[numLocRemaps-1] = iPlane;

	U32 iEdge = 0;
	while (iEdge<m_numEdges)
	{
		if (m_edgePlanes[iEdge*2] == iPlane || m_edgePlanes[iEdge*2+1] == iPlane)
		{
			RemoveEdge(iEdge, pLocRemap, numLocRemaps);
		}
		else
		{
			iEdge++;
		}
	}

	iPlane = pLocRemap[numLocRemaps-1];
	m_planes[iPlane] = m_planes[m_numPlanes-1];
	m_hCollider[iPlane] = m_hCollider[m_numPlanes-1];
	m_numPlanes--;

	if (pRemap)
	{
		memcpy(pRemap, pLocRemap, numRemaps*sizeof(pRemap[0]));
	}
}

void Rope2::Constraint::RemoveConstraintBreakingPlane(U32 iPlane)
{
	// Find planes we have edge with
	U32 otherPlanes[3];
	U32 numOtherPlanes = 0;
	for (U32 iEdge = 0; iEdge<m_numEdges; iEdge++)
	{
		if (m_edgePlanes[iEdge*2] == iPlane || m_edgePlanes[iEdge*2+1] == iPlane)
		{
			U32 iOtherPlane = m_edgePlanes[iEdge*2] == iPlane ? m_edgePlanes[iEdge*2+1] : m_edgePlanes[iEdge*2];
			if (iOtherPlane < m_firstNoEdgePlane)
			{
				otherPlanes[numOtherPlanes] = iOtherPlane;
				numOtherPlanes++;
			}
		}
	}

	// Now check the other planes (A)
	// If it has edge with another plane (B) that has no edge with this removed plane (C) (meaning B and C are concave), we will have to also remove B
	// Example: draping over the T junction in the fence in sea gate 
	U32 otherToRemove[3];
	U32 numOtherToRemove = 0;
	for (U32 iOther = 0; iOther<numOtherPlanes; iOther++)
	{
		U32 iOtherPlane = otherPlanes[iOther];
		for (U32 iEdge = 0; iEdge<m_numEdges; iEdge++)
		{
			if (m_edgePlanes[iEdge*2] == iOtherPlane || m_edgePlanes[iEdge*2+1] == iOtherPlane)
			{
				I32 iPlaneB = m_edgePlanes[iEdge*2] == iOtherPlane ? m_edgePlanes[iEdge*2+1] : m_edgePlanes[iEdge*2];
				if (iPlaneB != iPlane)
				{
					for (U32 jOther = 0; jOther<numOtherPlanes; jOther++)
					{
						if (otherPlanes[jOther] == iPlaneB)
						{
							iPlaneB = -1;
							break;
						}
					}
					if (iPlaneB >= 0)
					{
						otherToRemove[numOtherToRemove] = iPlaneB;
						numOtherToRemove++;
					}
				}
			}
		}
	}

	RemovePlane(iPlane, otherToRemove, numOtherToRemove);
	for (U32 ii = 0; ii<numOtherToRemove; ii++)
	{
		RemovePlane(otherToRemove[ii], otherToRemove+ii+1, numOtherToRemove-ii-1);
	}
}

Vec4 Rope2::Constraint::GetEdgePlane(U32 iEdge, const Point& refPos, U8& planeMask) const
{
	ASSERT(iEdge < m_numEdges);
	U32 iPl0 = m_edgePlanes[iEdge*2];
	U32 iPl1 = m_edgePlanes[iEdge*2+1];
	ASSERT(iPl0 < m_numPlanes && iPl1 < m_numPlanes);
	VF32 posXYZ1 = MakeXYZ1(refPos.QuadwordValue());
	Scalar biHeight0 = Scalar(FastDot(posXYZ1, m_biPlanes[iEdge*2].QuadwordValue()));
	Scalar biHeight1 = Scalar(FastDot(posXYZ1, m_biPlanes[iEdge*2+1].QuadwordValue()));
	Scalar height0 = Scalar(FastDot(posXYZ1, m_planes[iPl0].QuadwordValue()));
	Scalar height1 = Scalar(FastDot(posXYZ1, m_planes[iPl1].QuadwordValue()));
	Vec4 plane;
	if (IsPositive(biHeight0) && IsPositive(biHeight1))
	{
		Vector e(height0 * m_planes[iPl0] + biHeight0 * m_biPlanes[iEdge*2]);
		Point edgePnt = refPos - e;
		plane = SafeNormalize(e, Vector(m_planes[iPl0])).GetVec4();
		Scalar planeD = Dot3(plane, edgePnt.GetVec4());
		plane.SetW(-planeD);
		planeMask = (kIsWorking1 << iPl0) | (kIsWorking1 << iPl1);
		PHYSICS_ASSERT(IsFinite(plane));
	}
	else
	{
		plane = height0 > height1 ? m_planes[iPl0] : m_planes[iPl1];
		planeMask = height0 > height1 ? (kIsWorking1 << iPl0) : (kIsWorking1 << iPl1);
	}
	return plane;
}

Vec4 Rope2::Constraint::RelaxPos(Point &refPos, F32 radius)
{
	PHYSICS_ASSERT(IsFinite(refPos));
	Vec4 resid(kZero);
	Scalar scRadius(radius);

	for (U32 ii = 0; ii<m_numEdges; ii++)
	{
		U8 planeMask;
		Vec4 edgePlane = GetEdgePlane(ii, refPos, planeMask);
		Scalar scHeight = Dot4(refPos, edgePlane) - scRadius;
		Vec4 scClampedHeight(MakeXYZ0(ClampPos(scHeight.QuadwordValue())));
		Vec4 delta = scClampedHeight * edgePlane;
		refPos -= Vector(delta);
		resid += delta * delta;
		m_flags |= scHeight <= Scalar(kZero) ? planeMask : 0;
	}

	for (U32 ii = m_firstNoEdgePlane; ii<m_numPlanes; ii++)
	{
		Scalar scHeight = Dot4(refPos, m_planes[ii]) - scRadius;
		Vec4 scClampedHeight(MakeXYZ0(ClampPos(scHeight.QuadwordValue())));
		Vec4 delta = scClampedHeight * m_planes[ii];
		refPos -= Vector(delta);
		resid += delta * delta;
		m_flags |= scHeight <= Scalar(kZero) ? (kIsWorking1 << ii) : 0;
	}

	PHYSICS_ASSERT(IsFinite(refPos));
	return resid;
}

void Rope2::Constraint::RelaxVel(const Point& refPos, Vector& refVel, Rope2* pOwner, Scalar_arg scProporFriction, Scalar_arg scConstFriction) const
{
	PHYSICS_ASSERT(IsFinite(refPos));
	PHYSICS_ASSERT(IsFinite(refVel));

	Vector vel[kMaxNumPlanes];
	for (U32 ii = 0; ii<m_numPlanes; ii++)
	{
		RopeCollider colliderBuffer;
		if (const RopeCollider* pCollider = m_hCollider[ii].GetCollider(&colliderBuffer, pOwner))
		{
			vel[ii] = pCollider->m_linVel + Cross(pCollider->m_angVel, refPos - pCollider->m_loc.GetTranslation());
		}
		else
		{
			vel[ii] = kZero;
		}
	}
	
	U16 frictionFlags = m_flags | ~m_frictionlessFlags;

	for (U32 ii = 0; ii<m_numEdges; ii++)
	{
		U32 iPl0 = m_edgePlanes[ii*2];
		U32 iPl1 = m_edgePlanes[ii*2+1];
		U8 planeMask;
		Vec4 edgePlane = GetEdgePlane(ii, refPos, planeMask);
		Scalar dt0 = Dot3(edgePlane, m_planes[iPl0]);
		Scalar dt1 = Dot3(edgePlane, m_planes[iPl1]);
		Scalar f0 = dt0 / (dt0 + dt1);
		Vector edgeVel = f0 * vel[iPl0] + (Scalar(1.0f) - f0) * vel[iPl1];
		Vector relVel = refVel - edgeVel;
		Scalar scHeight = Dot3(relVel.GetVec4(), edgePlane);
		refVel -= Vector(Min(scHeight, Scalar(kZero)) * edgePlane);
		if (frictionFlags & planeMask)
		{
			Vector tanVel = relVel - Vector(edgePlane) * scHeight;
			Vector fricVel = scProporFriction * tanVel;
			tanVel -= fricVel;
			refVel -= fricVel;
			Scalar tanVelLen;
			Vector tanVelDir = SafeNormalize(tanVel, kZero, tanVelLen);
			refVel -= Min(scConstFriction, tanVelLen) * tanVelDir;
		}
	}

	for(U32 ii = m_firstNoEdgePlane; ii < m_numPlanes; ++ii)
	{
		Vector relVel = refVel - vel[ii];
		Scalar scHeight = Dot3(relVel.GetVec4(), m_planes[ii]);
		refVel -= Vector(Min(scHeight, Scalar(kZero)) * m_planes[ii]);

		if (frictionFlags & (kIsWorking1 << ii))
		{
			// friction
			Vector tanVel = relVel - Vector(m_planes[ii]) * scHeight;
			Vector fricVel = scProporFriction * tanVel;
			tanVel -= fricVel;
			refVel -= fricVel;
			Scalar tanVelLen;
			Vector tanVelDir = SafeNormalize(tanVel, kZero, tanVelLen);
			refVel -= Min(scConstFriction, tanVelLen) * tanVelDir;
		}
	}

	PHYSICS_ASSERT(IsFinite(refVel));
}

bool Rope2::Constraint::GetLineSegmentEdgePoint(const Point& p0, const Point& p1, Point& edgePoint, F32& t)
{
	for (U32F iEdge = 0; iEdge<m_numEdges; iEdge++)
	{
		const Vec4 pl0 = m_planes[m_edgePlanes[iEdge*2]];
		F32 d0 = Dot4(pl0, p0.GetVec4());
		F32 d1 = Dot4(pl0, p1.GetVec4());
		if (d0*d1 >= 0.0f)
		{
			continue;
		}

		F32 t0 = d0/(d0-d1);
		Point C = Lerp(p0, p1, t0);
		if (Dot4(m_biPlanes[iEdge*2], C.GetVec4()) > 0.0f)
		{
			continue;
		}

		const Vec4 bi0 = m_biPlanes[iEdge*2];

		Point p0Edge, p1Edge;
		Point p0Pl = p0 - d0*Vector(pl0);
		F32 d0Bi = Dot4(bi0, p0Pl.GetVec4());
		p0Edge = p0Pl - d0Bi*Vector(bi0);
		Point p1Pl = p1 - d1*Vector(pl0);
		F32 d1Bi = Dot4(bi0, p1Pl.GetVec4());
		p1Edge = p1Pl - d1Bi*Vector(bi0);
		Scalar p0Dist = Dist(p0, p0Edge);
		Scalar p1Dist = Dist(p1, p1Edge);
		F32 tEdge = p0Dist/(p0Dist+p1Dist);
		edgePoint = Lerp(p0Edge, p1Edge, tEdge);

		if (iEdge+1 < m_numEdges)
		{
			U32 iNextEdge = iEdge+1;
			const Vec4 nextPl0 = m_planes[m_edgePlanes[iNextEdge*2]];
			const Vec4 nextPl1 = m_planes[m_edgePlanes[iNextEdge*2+1]];
			const Vec4 nextBi0 = m_biPlanes[iNextEdge*2];
			const Vec4 nextBi1 = m_biPlanes[iNextEdge*2+1];
			F32 nextD0 = Dot4(nextPl0, edgePoint.GetVec4());
			F32 nextD1 = Dot4(nextPl1, edgePoint.GetVec4());
			if (nextD0 < 0.0f && nextD1 < 0.0f)
			{
				// We're behind the second edge
				// Just let's go to the intersection of the 2 edges
				Vector edge = Cross(Vector(pl0), Vector(bi0));
				Vector nextEdge = Cross(Vector(nextPl0), Vector(nextBi0));
				Point nextEdgePoint = edgePoint - Dot4(nextPl0, edgePoint.GetVec4())*Vector(nextPl0) - Dot4(nextBi0, edgePoint.GetVec4())*Vector(nextBi0);
				Scalar sInt, tInt;
				if (LineLineIntersection(edgePoint, edge, nextEdgePoint, nextEdge, sInt, tInt))
				{
					edgePoint = edgePoint + sInt * edge;
					nextEdgePoint = nextEdgePoint + tInt * nextEdge;
					edgePoint = AveragePos(edgePoint, nextEdgePoint);
					// ... and add a little buffer because this is all a bit whacky
					edgePoint += 0.005f * (Vector(pl0) + Vector(bi0) + Vector(nextPl0) + Vector(nextBi0));
				}
			}
		}
		return true;
	}

	return false;
}

I32 Rope2::Constraint::GetOtherEdgePlane(U32 iPlane) const
{
	for (U32 ii = 0; ii<m_numEdges; ii++)
	{
		if (m_edgePlanes[ii*2] == iPlane)
			return m_edgePlanes[ii*2+1];
		if (m_edgePlanes[ii*2+1] == iPlane)
			return m_edgePlanes[ii*2];
	}

	return -1;
}

bool Rope2::Constraint::HasCollider(const RopeColliderHandle& hCollider)
{
	for(U32F ii = 0; ii < m_numPlanes; ++ii)
	{
		if (m_hCollider[ii] == hCollider)
			return true;
	}
	return false;
}

void Rope2::Constraint::PreCollision(Rope2* pRope, U32F iPoint, const Point& pos, F32 radius, Collide::LayerMask excludeLayerMask, bool bKeepWorkingFlags)
{
	// This is to prevent/improve tunneling/jitter problems: basically we want to keep the constraints from previous frames
	// as long as they were working even if it seems like the point is not colliding anymore

	RopeCollider colliderBuffer[kMaxNumPlanes];
	const RopeCollider* pCollider[kMaxNumPlanes];
	RopeColliderHandle hCollider[kMaxNumPlanes];

	Point refPos = pos;
	U32F numPlanes = m_numPlanes;
	// Transform constraints by the movement of the rigid body
	for (U32 ii = 0; ii < numPlanes; ++ii)
	{
		pCollider[ii] = nullptr;

		const RigidBody* pBody = m_hCollider[ii].GetRigidBody();
		bool bExclude = (pBody && Collide::IsLayerInMask(pBody->GetLayer(), excludeLayerMask));
		if (!bExclude)
		{
			pCollider[ii] = m_hCollider[ii].GetCollider(&colliderBuffer[ii], pRope);
		//if (m_flags & (kIsWorking1 << ii))
		//			pCollider[ii] = m_hCollider[ii].GetCollider(&colliderBuffer[ii], pRope);
			if (pCollider[ii] && pCollider[ii]->m_enabled)
			{
				hCollider[ii] = m_hCollider[ii];
				Locator locMoved = pCollider[ii]->m_loc.TransformLocator(Inverse(pCollider[ii]->m_locPrev));
				{
					Vector norm = Vector(m_planes[ii]);
					Point p = Point(kZero) - m_planes[ii].W() * norm;
					norm = locMoved.TransformVector(norm);
					p = locMoved.TransformPoint(p);
					Scalar D = -Dot(p-Point(kZero), norm);
					m_planes[ii] = Vec4(norm.X(), norm.Y(), norm.Z(), D);
				}
				if (ii < m_firstNoEdgePlane)
				{
					U32 iBi;
					for (iBi = 0; iBi<m_numEdges*2; iBi++)
					{
						if (m_edgePlanes[iBi] == ii)
							break;
					}
					PHYSICS_ASSERT(iBi < m_numEdges*2);
					Vector norm = Vector(m_biPlanes[iBi]);
					Point p = Point(kZero) - m_biPlanes[iBi].W() * norm;
					norm = locMoved.TransformVector(norm);
					p = locMoved.TransformPoint(p);
					Scalar D = -Dot(p-Point(kZero), norm);
					m_biPlanes[iBi] = Vec4(norm.X(), norm.Y(), norm.Z(), D);
				}

				bool reCheck = m_flags & (kIsWorking1 << ii);
				if (reCheck)
				{
					for (U32 jj = 0; jj < ii; jj++)
					{
						if (hCollider[jj] == m_hCollider[ii])
						{
							reCheck = false;
							break;
						}
					}
				}
				if (!reCheck)
					pCollider[ii] = nullptr;
			}
		}
	}

	// If the point is behind the plane move it out (prevents tunneling)
	RelaxPos(refPos, radius);

	// Now we can clear the constraint and re-collide this point against bodies from previous frame 
	// ignoring distance of the refPos from the collision
	Reset(bKeepWorkingFlags);
	for (U32F ii = 0; ii<numPlanes; ii++)
	{
		if (pCollider[ii])
		{
			pRope->CollidePointWithCollider(iPoint, refPos, pCollider[ii], hCollider[ii]);
		}
	}
}

inline const Scalar	MaxComp( Vec4_arg vec )
{
	SMATH_VEC_VALIDATE_QUADWORD( vec.QuadwordValue() );
	v128 tempXY, tempZW;
	tempXY = SMATH_VEC_MAX( SMATH_VEC_REPLICATE_X( vec.QuadwordValue() ), SMATH_VEC_REPLICATE_Y( vec.QuadwordValue() ) );
	tempZW = SMATH_VEC_MAX( SMATH_VEC_REPLICATE_Z( vec.QuadwordValue() ), SMATH_VEC_REPLICATE_W( vec.QuadwordValue() ) );
	return Scalar(SMATH_VEC_MAX(tempXY, tempZW));
}


void Rope2::Constraint::Validate()
{
	STRIP_IN_FINAL_BUILD;

	for (U32F ii = 0; ii<m_numPlanes; ii++)
	{
		ALWAYS_ASSERT(IsFinite(m_planes[ii]) && MaxComp(Abs(m_planes[ii])) < 1e8f);
	}

	for (U32F ii = 0; ii<m_numEdges*2; ii++)
	{
		ALWAYS_ASSERT(IsFinite(m_biPlanes[ii]) && MaxComp(Abs(m_biPlanes[ii])) < 1e8f);
	}
}

void Rope2::Constraint::SortPlanesForPersistency(const Constraint& prev, const Point& refPos)
{
	if (m_numPlanes <= 1 || prev.m_numPlanes <= 1)
		return;

	bool newTaken[kMaxNumPlanes];
	memset(newTaken, 0, kMaxNumPlanes*sizeof(newTaken[0]));

	I32 prevToNew[kMaxNumPlanes];
	for (U32F ii = 0; ii<prev.m_numPlanes; ii++)
	{
		prevToNew[ii] = -1;
		for (U32F jj = 0; jj<m_numPlanes; jj++)
		{
			if (newTaken[jj])
				continue;
			if (Rope2PointCol::ArePlanesSimilar(prev.m_planes[ii], m_planes[jj], refPos))
			{
				prevToNew[ii] = jj;
				newTaken[jj] = true;
				break;
			}
		}
	}

	I32 oldToNew[kMaxNumPlanes];
	U32 iOld = 0;
	U32 iOldNoEdge = m_firstNoEdgePlane;
	bool change = false;
	for (U32F ii = 0; ii<prev.m_numPlanes; ii++)
	{
		if (prevToNew[ii] >= m_firstNoEdgePlane)
		{
			oldToNew[iOldNoEdge] = prevToNew[ii];
			if (oldToNew[iOldNoEdge] != iOldNoEdge)
			{
				change = true;
			}
			iOldNoEdge++;
		}
		else if (prevToNew[ii] >= 0)
		{
			oldToNew[iOld] = prevToNew[ii];
			if (oldToNew[iOld] != iOld)
			{
				change = true;
			}
			iOld++;
		}
	}

	if (!change)
		return;

	for (U32 ii = 0; ii<m_numPlanes; ii++)
	{
		if (!newTaken[ii])
		{
			if (iOld == m_firstNoEdgePlane)
			{
				iOld = iOldNoEdge;
			}
			ASSERT((ii < m_firstNoEdgePlane && iOld < m_firstNoEdgePlane) || (ii >= m_firstNoEdgePlane && iOld >= m_firstNoEdgePlane));
			oldToNew[iOld] = ii;
			iOld++;
		}
	}
	ASSERT(iOld == m_numPlanes || (iOld == m_firstNoEdgePlane && iOldNoEdge == m_numPlanes));

	Rope2::Constraint old;
	memcpy(&old, this, sizeof(old));

	m_flags = 0;
	m_collisionFlags = 0;
	for (U32 ii = 0; ii<m_numPlanes; ii++)
	{
		U32 iNew = oldToNew[ii];
		m_planes[iNew] = old.m_planes[ii];
		m_hCollider[iNew] = old.m_hCollider[ii];
		m_flags = ((old.m_flags >> ii) & 1) << iNew;
		m_canGrappleFlags = ((old.m_canGrappleFlags >> ii) & 1) << iNew;
		m_snowFlags = ((old.m_snowFlags >> ii) & 1) << iNew;
		m_frictionlessFlags = ((old.m_frictionlessFlags >> ii) & 1) << iNew;
	}

	// @@JS: we may want to reorder edges also
	for (U32 ii = 0; ii<m_numEdges*2; ii++)
	{
		U32 iNew = oldToNew[old.m_edgePlanes[ii]];
		m_edgePlanes[ii] = iNew;
	}
}

void Rope2::Constraint::FixCompetingConstraints(const Point& refPos, F32 radius)
{
	if (m_numPlanes <= 1)
		return;

	bool weak[kMaxNumPlanes];
	U32 numWeak = 0;
	for (U32F ii = 0; ii<m_numPlanes; ii++)
	{
		weak[ii] = false;
		if (m_hCollider[ii].GetCustomIndex() >= 0)
		{
			weak[ii] = true;
		}
		if (const RigidBody* pBody = m_hCollider[ii].GetRigidBody())
		{
			weak[ii] = Collide::IsLayerInMask(pBody->GetLayer(), Collide::kLayerMaskSmall | Collide::kLayerMaskCharacterAndProp);
		}
		numWeak += weak[ii] ? 1 : 0;
	}

	if (numWeak == 0 || numWeak == m_numPlanes)
		return;

	for (U32F ii = 0; ii<m_numPlanes; ii++)
	{
		if (weak[ii])
		{
			F32 moveDist = 0.0f;
			Vec4 plw = m_planes[ii];
			for (U32F jj = 0; jj<m_numPlanes; jj++)
			{
				if (!weak[jj])
				{
					Vec4 pl = m_planes[jj];
					F32 cosNorm = Dot(Vector(plw), Vector(pl));
					if (cosNorm >= 0.0f)
						continue;

					// We're considering line starting at refPos and going along plw normal
					// t refers to that line

					// current t of the weak plane
					F32 currentT = -Dot4(refPos.GetVec4(), plw);

					// intersection with the strong plane
					F32 dist = Dot4(refPos.GetVec4(), pl);
					F32 t = dist / (-cosNorm);

					// target t so that radius can fit
					F32 targetT = Max(t - radius - radius/(-cosNorm), -2.0f*radius);

					// how much we need to move the plane (only in negative direction)
					moveDist = Min(moveDist, targetT - currentT);
				}
			}

			if (moveDist < 0.0f)
			{
				// move the plane
				m_planes[ii].SetW(plw.W() - moveDist);
				if (ii < m_firstNoEdgePlane)
				{
					// move edge bi vectors
					for (U32 iEdge = 0; iEdge<m_numEdges; iEdge++)
					{
						if (m_edgePlanes[iEdge*2] == ii)
						{
							F32 cosNormBi = Dot(Vector(m_planes[ii]), Vector(m_biPlanes[iEdge*2+1]));
							m_biPlanes[iEdge*2+1].SetW(m_biPlanes[iEdge*2+1].W() - cosNormBi*moveDist);
						}
						else if (m_edgePlanes[iEdge*2+1] == ii)
						{
							F32 cosNormBi = Dot(Vector(m_planes[ii]), Vector(m_biPlanes[iEdge*2]));
							m_biPlanes[iEdge*2].SetW(m_biPlanes[iEdge*2].W() - cosNormBi*moveDist);
						}
					}
				}
			}
		}
	}
}

void Rope2::Constraint::SlowDepenetration(const Point& refPos, F32 radius, F32 dt)
{
	F32 depRate = 0.3f * dt;
	for (U32F ii = m_firstNoEdgePlane; ii<m_numPlanes; ii++)
	{
		bool soft = false;
		if (m_hCollider[ii].GetCustomIndex() >= 0)
		{
			soft = true;
		}
		if (const RigidBody* pBody = m_hCollider[ii].GetRigidBody())
		{
			soft = Collide::IsLayerInMask(pBody->GetLayer(), Collide::kLayerSmall | Collide::kLayerMaskCharacterAndProp);
		}
		if (!soft)
		{
			continue;
		}

		F32 dist = Dot4(m_planes[ii], refPos.GetVec4()) - radius;
		if (dist < -depRate)
		{
			m_planes[ii].SetW(m_planes[ii].W() - (dist + depRate));
		}
	}
}
