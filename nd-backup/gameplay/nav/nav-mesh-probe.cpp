/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-mesh-probe.h"

#include "corelib/math/intersection.h"
#include "corelib/math/segment-util.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h"
#include "corelib/util/strcmp.h"

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"

 /// --------------------------------------------------------------------------------------------------------------- ///
static FORCE_INLINE const Scalar DotXz(Vector_arg a, Point_arg b)
{
	return a.X() * b.X() + a.Z() * b.Z();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static FORCE_INLINE const Scalar DotXz(Point_arg a, Vector_arg b)
{
	return a.X() * b.X() + a.Z() * b.Z();
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavPolyOpenListEntry : public NavPolyListEntry
{
	I32F m_iEdgeIgnore;
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPolyListEntry::IsSameAs(const NavPolyListEntry& rhs) const
{
	if (m_pPoly && !rhs.m_pPoly)
		return false;

	if (m_pPolyEx && !rhs.m_pPolyEx)
		return false;

	if (m_pPolyEx)
	{
		return m_pPolyEx->GetId() == rhs.m_pPolyEx->GetId();
	}
	else if (m_pPoly)
	{
		return (m_pPoly->GetId() == rhs.m_pPoly->GetId())
			   && (m_pPoly->GetNavMeshHandle() == rhs.m_pPoly->GetNavMeshHandle());
	}

	return (rhs.m_pPoly == nullptr) && (rhs.m_pPolyEx == nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsInList(const NavPolyListEntry* pList, U32F listSize, const NavPolyListEntry& query)
{
	for (U32F i = 0; i < listSize; ++i)
	{
		if (pList[i].IsSameAs(query))
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsInList(const NavPolyOpenListEntry* pList, U32F listSize, const NavPolyListEntry& query)
{
	for (U32F i = 0; i < listSize; ++i)
	{
		if (pList[i].IsSameAs(query))
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const Segment ConstructReducedSegment(const Segment& seg, const Segment& cylSeg, Scalar cylRadius, Vec2& tRange)
{
	const Scalar segLen = Length(seg.b - seg.a);
	const Vector segDir = SafeNormalize(seg.b - seg.a, kZero);
	const Vector cylDir = SafeNormalize(cylSeg.b - cylSeg.a, kZero);

	Point cylOnSegA, cylOnSegB;
	DistPointSegmentXz(cylSeg.a, seg.a, seg.b, &cylOnSegA);
	DistPointSegmentXz(cylSeg.b, seg.a, seg.b, &cylOnSegB);

	const Scalar dotP		  = Abs(Dot(cylDir, segDir));
	const Scalar maxSegDist	  = Dist(cylOnSegA, cylOnSegB) * 0.55f;
	const Scalar paddedRadius = (2.5f * cylRadius) + (dotP * maxSegDist);

	const Scalar tPadding = (segLen > NDI_FLT_EPSILON) ? AccurateDiv(paddedRadius, segLen) : SCALAR_LC(0.0f);

	Scalar tMinPadded = 0.0f;
	Scalar tMaxPadded = 1.0f;

	Scalar tSeg, tCyl;
	if (IntersectSegmentSegmentXz(seg, cylSeg, tSeg, tCyl))
	{
		tMinPadded = Max(tSeg - tPadding, SCALAR_LC(0.0f));
		tMaxPadded = Min(tSeg + tPadding, SCALAR_LC(1.0f));
	}
	else
	{
		Scalar ta = -1.0f;
		Scalar tb = -1.0f;
		Point pa, pb;
		const Scalar da = DistPointSegmentXz(seg.a, cylSeg.a, cylSeg.b, &pa, &ta);
		const Scalar db = DistPointSegmentXz(seg.b, cylSeg.a, cylSeg.b, &pb, &tb);

		const Point posCylClosest = da <= db ? pa : pb;

		DistPointSegmentXz(posCylClosest, seg.a, seg.b, nullptr, &tSeg);

		tMinPadded = Max(tSeg - tPadding, SCALAR_LC(0.0f));
		tMaxPadded = Min(tSeg + tPadding, SCALAR_LC(1.0f));
	}

	const Scalar minDist = tMinPadded * segLen;
	const Scalar maxDist = tMaxPadded * segLen;

	Segment reduced;
	reduced.a = seg.a + (segDir * minDist);
	reduced.b = seg.a + (segDir * maxDist);

	tRange.x = tMinPadded;
	tRange.y = tMaxPadded;

	return reduced;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyExpansionSearch::Init(const NavMesh::BaseProbeParams& params, U32F maxListSize)
{
	m_baseProbeParams = params;
	m_maxListSize	  = maxListSize;
	m_closedListSize  = 0;

	if (m_maxListSize > 0)
	{
		const Alignment alignOpen  = ALIGN_OF(NavPolyOpenListEntry);
		const Alignment alignEntry = ALIGN_OF(NavPolyListEntry);
		const Alignment alignMax   = Alignment(Max(alignOpen.GetValue(), alignEntry.GetValue()));

		const size_t reqSize = AlignSize(sizeof(NavPolyOpenListEntry) * m_maxListSize, alignOpen)
							   + AlignSize(sizeof(NavPolyListEntry) * m_maxListSize, alignEntry);

		const Memory::Allocator* pTopAllocator = Memory::TopAllocator();

		if (!pTopAllocator || !pTopAllocator->CanAllocate(reqSize, alignMax))
		{
			AllocateJanitor alloc(kAllocSingleGameFrame, FILE_LINE_FUNC);

			m_pOpenList	  = NDI_NEW NavPolyOpenListEntry[m_maxListSize];
			m_pClosedList = NDI_NEW NavPolyListEntry[m_maxListSize];
		}
		else
		{
			m_pOpenList	  = NDI_NEW NavPolyOpenListEntry[m_maxListSize];
			m_pClosedList = NDI_NEW NavPolyListEntry[m_maxListSize];
		}
	}

	m_pStartMesh   = nullptr;
	m_pStartPoly   = nullptr;
	m_pStartPolyEx = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyExpansionSearch::Execute(const NavMesh* pStartMesh, const NavPoly* pStartPoly, const NavPolyEx* pStartPolyEx)
{
	NavPolyListEntry seed;
	seed.m_pMesh   = pStartMesh;
	seed.m_pPoly   = pStartPoly;
	seed.m_pPolyEx = pStartPolyEx;
	return Execute(&seed, 1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyExpansionSearch::Execute(const NavPolyListEntry* pList, U32 listSize)
{
	if (!m_pOpenList || !m_pClosedList || (0 == m_maxListSize))
		return;

	if (!pList || listSize == 0)
		return;

	m_pStartMesh   = pList[0].m_pMesh;
	m_pStartPoly   = pList[0].m_pPoly;
	m_pStartPolyEx = pList[0].m_pPolyEx;

	if (!m_pStartMesh || !m_pStartPoly)
		return;

	memset(m_pOpenList, 0, sizeof(NavPolyListEntry) * m_maxListSize);
	memset(m_pClosedList, 0, sizeof(NavPolyListEntry) * m_maxListSize);
	U32F openListSize = 0;
	m_closedListSize  = 0;

	for (U32 listNum = 0; listNum < listSize; listNum++)
	{
		if (!pList[listNum].m_pPoly)
			continue;

		if (!pList[listNum].m_pPoly->IsValid())
			continue;

		if (IsInList(m_pOpenList, openListSize, pList[listNum]))
			continue;

		m_pOpenList[openListSize].m_pMesh		= pList[listNum].m_pPoly->GetNavMesh();
		m_pOpenList[openListSize].m_pPoly		= pList[listNum].m_pPoly;
		m_pOpenList[openListSize].m_pPolyEx		= m_baseProbeParams.m_dynamicProbe ? pList[listNum].m_pPolyEx : nullptr;
		m_pOpenList[openListSize].m_iEdgeIgnore = -1;
		++openListSize;
	}

	while (openListSize > 0)
	{
		NavPolyOpenListEntry curEntry = m_pOpenList[openListSize - 1];
		memset(&m_pOpenList[openListSize - 1], 0, sizeof(NavPolyListEntry));
		openListSize--;

		const NavMesh* pMesh	 = curEntry.m_pMesh;
		const NavPoly* pPoly	 = curEntry.m_pPoly;
		const NavPolyEx* pPolyEx = curEntry.m_pPolyEx;

		const U32F vertCount = pPolyEx ? pPolyEx->GetVertexCount() : pPoly->GetVertexCount();

		for (U32F iEdge = 0; iEdge < vertCount; ++iEdge)
		{
			if (iEdge == curEntry.m_iEdgeIgnore)
				continue;

			const NavPoly* pAdjPoly		= pPoly;
			const NavPolyEx* pAdjPolyEx = pPolyEx;
			const NavMesh* pAdjMesh		= pMesh;

			const NavMesh::BoundaryFlags boundaryFlags = NavMesh::IsBlockingEdge(pMesh,
																				 pPoly,
																				 pPolyEx,
																				 iEdge,
																				 m_baseProbeParams,
																				 &pAdjMesh,
																				 &pAdjPoly,
																				 &pAdjPolyEx);

			const bool shouldExpandEdge = VisitEdge(pMesh, pPoly, pPolyEx, iEdge, boundaryFlags);

			if (shouldExpandEdge && pAdjPoly)
			{
				NavPolyOpenListEntry newEntry;
				newEntry.m_pPoly	   = pAdjPoly;
				newEntry.m_pPolyEx	   = pAdjPolyEx;
				newEntry.m_pMesh	   = pAdjMesh;
				newEntry.m_iEdgeIgnore = Nav::GetIncomingEdge(pAdjPoly, pAdjPolyEx, pPoly, pPolyEx);

				if (pMesh != pAdjMesh)
				{
					OnNewMeshEntered(pMesh, pAdjMesh);
				}

				if (!IsInList(m_pClosedList, m_closedListSize, newEntry)
					&& !IsInList(m_pOpenList, openListSize, newEntry)
					&& (openListSize < m_maxListSize))
				{
					m_pOpenList[openListSize] = newEntry;
					++openListSize;
				}
			}
		}

		m_pClosedList[m_closedListSize] = curEntry;
		++m_closedListSize;

		if (m_closedListSize >= m_maxListSize)
		{
			// list size overflow
			MsgWarn("NavPolyExpansionSearch needs a max list size bigger than %d\n", (int)m_maxListSize);
			break;
		}
	}

	Finalize();
}

/// --------------------------------------------------------------------------------------------------------------- ///
size_t NavPolyExpansionSearch::GatherClosedList(NavManagerId* pListOut, size_t maxSizeOut) const
{
	if (!pListOut || (0 == maxSizeOut))
		return 0;

	size_t count = 0;

	for (U32F i = 0; i < m_closedListSize; ++i)
	{
		NavManagerId id;
		if (m_pClosedList[i].m_pPolyEx)
			id = m_pClosedList[i].m_pPolyEx->GetNavManagerId();
		else
			id = m_pClosedList[i].m_pPoly->GetNavManagerId();

		pListOut[count] = id;
		++count;

		if (count >= maxSizeOut)
			break;
	}

	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
size_t NavPolyExpansionSearch::GatherClosedList(const NavPoly** ppPolysOut, size_t maxSizeOut) const
{
	if (!ppPolysOut || (0 == maxSizeOut))
		return 0;

	size_t count = 0;

	for (U32F i = 0; i < m_closedListSize; ++i)
	{
		NavManagerId id;
		if (m_pClosedList[i].m_pPolyEx)
			continue;

		ppPolysOut[count] = m_pClosedList[i].m_pPoly;
		++count;

		if (count >= maxSizeOut)
			break;
	}

	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshCapsuleProbe::Init(NavMesh::ProbeParams* pParams, const NavMesh* pInitialMesh)
{
	NAV_ASSERT(pParams);
	if (!pParams)
		return;

	NavPolyExpansionSearch::Init(*pParams, 384);
	m_closestProbeStopTT = kLargeFloat;
	m_pProbeParams		 = pParams;
	m_pCurMesh			 = pInitialMesh;
	m_isStadium			 = LengthSqr(pParams->m_capsuleSegment.GetVec()) > NDI_FLT_EPSILON;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool SweptCircleSegmentXz(const Point_arg c, F32 r, Vector_arg move, const Segment& seg, Scalar& t, Point& impact)
{
	F32 hit;
	Scalar t0, t1;

	Segment stadium(c, c + move);
	if (DistSegmentSegmentXz(stadium, seg, t0, t1) > r)
	{
		// We never intersect so we can early out.
		return false;
	}

	if (IntersectSegmentCircleXz(seg, c, r, hit))
	{
		// We start inside the segment so no need to find how far we can move.
		t = 0.0f;
		impact = Lerp(seg.a, seg.b, hit);
		return true;
	}

	// Check if the intersection occurs at some point on the line by treating the line as a plane and
	// finding when we hit it.
	Vector planeNormal = Normalize(RotateY90(VectorXz(seg.GetVec())));
	F32 planeDist = DotXz(planeNormal, seg.a);

	F32 da = DotXz(planeNormal, stadium.a) - planeDist;
	F32 db = DotXz(planeNormal, stadium.b) - planeDist;

	hit = (da - r) / (da - db);

	// Check if the hit point is inside our move line
	if (hit >= 0.0f && hit <= 1.0f)
	{
		t = hit;

		Point loc = Lerp(stadium.a, stadium.b, hit);
		impact = loc + r * planeNormal;

		if (Abs(DotXz(planeNormal, impact)) > NDI_FLT_EPSILON)
		{
			impact = loc - r * planeNormal;
		}
	}
	else
	{
		// If the hit point on the plane is not inside our segment then we consider the case where the
		// circle collides with the line at an endpoint. We do this by keeping the circle stationary and
		// moving the line.
		t = 1.0f;

		Segment sa(seg.a, seg.a - move);
		if (IntersectSegmentCircleXz(sa, c, r, hit))
		{
			if (hit <= t)
			{
				t = hit;
				impact = Lerp(sa.a, sa.b, hit);
			}
		}

		Segment sb(seg.b, seg.b - move);
		if (IntersectSegmentCircleXz(sb, c, r, hit))
		{
			if (hit <= t)
			{
				t = hit;
				impact = Lerp(sb.a, sb.b, hit);
			}
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool SweptStadiumSlabSegmentXz(const Segment& segStadium, F32 radius, Vector_arg move, const Segment& seg,
									  Scalar& t, Point& impact)
{
	Vector dir = Normalize(VectorXz(segStadium.GetVec()));
	Vector n = RotateY90(dir);

	Segment slab[2] =
	{
		Segment(segStadium.a + radius * n, segStadium.b + radius * n),
		Segment(segStadium.a - radius * n, segStadium.b - radius * n),
	};

	// Check to see if we start intersected with the segment (i.e. either we intersect one of the slab segments or at
	// least one of the segments endpoints is inside the slab).
	{
		Scalar it = kLargeFloat;
		Scalar t0, t1;
		if (IntersectSegmentSegmentXz(slab[0], seg, t0, t1))
		{
			it = Min(it, t1);
		}

		if (IntersectSegmentSegmentXz(slab[1], seg, t0, t1))
		{
			it = Min(it, t1);
		}

		if (it < kLargeFloat)
		{
			t = 0.0f;
			impact = Lerp(seg.a, seg.b, it);
			return true;
		}

		F32 dt = DotXz(n, slab[0].a);
		F32 db = DotXz(n, slab[1].a);

		F32 dl = DotXz(dir, segStadium.a);
		F32 dr = DotXz(dir, segStadium.b);

		F32 dna = DotXz(n, seg.a);
		F32 dda = DotXz(dir, seg.a);

		F32 dnb = DotXz(n, seg.b);
		F32 ddb = DotXz(dir, seg.b);

		if (dna >= db && dna <= dt && dda >= dl && dda <= dr)
		{
			Point closest0, closest1;
			F32 ct0 = DistPointSegmentXz(seg.a, slab[0], &closest0);
			F32 ct1 = DistPointSegmentXz(seg.a, slab[1], &closest1);

			t = 0.0f;
			impact = ct0 < ct1 ? closest0 : closest1;
			return true;
		}
		else if (dnb >= db && dnb <= dt && ddb >= dl && ddb <= dr)
		{
			Point closest0, closest1;
			F32 ct0 = DistPointSegmentXz(seg.b, slab[0], &closest0);
			F32 ct1 = DistPointSegmentXz(seg.b, slab[1], &closest1);

			t = 0.0f;
			impact = ct0 < ct1 ? closest0 : closest1;
			return true;
		}
	}

	Segment test[2] =
	{
		Segment(seg.a, seg.a - move),
		Segment(seg.b, seg.b - move),
	};

	t = kLargeFloat;

	for (I32 i = 0; i < 2; i++)
	{
		for (I32 j = 0; j < 2; j++)
		{
			Scalar t0, t1;
			if (IntersectSegmentSegmentXz(slab[i], test[j], t0, t1))
			{
				if (t1 < t)
				{
					t = Min(t, t1);
					impact = Lerp(test[j].a, test[j].b, t1);
				}
			}
		}
	}

	return t < kLargeFloat;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool SweptStadiumSegmentXz(const Segment& segStadium,
								  F32 radius,
								  Vector_arg move,
								  const Segment& seg,
								  Scalar& t,
								  Point& impact)
{
	Point hitPoint;
	Scalar hitTime;

	t = kLargeFloat;

	if (SweptCircleSegmentXz(segStadium.a, radius, move, seg, hitTime, hitPoint))
	{
		if (hitTime < t)
		{
			t	   = hitTime;
			impact = hitPoint;
		}
	}

	if (SweptCircleSegmentXz(segStadium.b, radius, move, seg, hitTime, hitPoint))
	{
		if (hitTime < t)
		{
			t	   = hitTime;
			impact = hitPoint;
		}
	}

	if (SweptStadiumSlabSegmentXz(segStadium, radius, move, seg, hitTime, hitPoint))
	{
		if (hitTime < t)
		{
			t	   = hitTime;
			impact = hitPoint;
		}
	}

	return t < kLargeFloat;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshCapsuleProbe::VisitEdge(const NavMesh* pMesh,
									const NavPoly* pPoly,
									const NavPolyEx* pPolyEx,
									I32F iEdge,
									NavMesh::BoundaryFlags boundaryFlags)
{
	NavMesh::ProbeParams* pParams = m_pProbeParams;
	if (!pParams || !pPoly || !pMesh)
		return false;

	if (m_pCurMesh != pMesh)
	{
		ChangeToNewNavMeshSpace(pMesh);
	}

	bool expand = false;

	if (m_isStadium)
	{
		const float radius = pParams->m_probeRadius;

		// We're translating the move segment to account for the capsule being off center.
		const Vector startOffset = VectorXz(pParams->m_start - Lerp(pParams->m_capsuleSegment.a, pParams->m_capsuleSegment.b, 0.5f));
		const Point start = pParams->m_start - startOffset;
		const Segment seg(start, start + pParams->m_move);
		const Vector segDir = pParams->m_move;

		const Point v0 = pPolyEx ? pPolyEx->GetVertex(iEdge) : pPoly->GetVertex(iEdge);
		const Point v1 = pPolyEx ? pPolyEx->GetNextVertex(iEdge) : pPoly->GetNextVertex(iEdge);
		const Segment edgeSeg(v0, v1);

		// Do a quick intersection test that can give false positives but will always be correct when it finds no intersection.
		{
			// Computes the radius of the bounding sphere of the capsule.
			F32 boundRadius = radius + LengthXz(pParams->m_capsuleSegment.GetVec()) / 2.0f;

			Scalar t0, t1;
			Scalar d = DistSegmentSegmentXz(seg, edgeSeg, t0, t1);
			if (d > boundRadius)
			{
				// No intersection! We're free to move the entire distance without hitting this edge.
				return false;
			}
		}

		// Ignore the edge if we are moving away from it. This avoids situations where you can get stuck on an edge in tight
		// areas that are too small for the entire capsule.
		Vector edgeNormal = -SafeNormalize(RotateY90(VectorXz(v0 - v1)), kZero);
		if (DotXz(segDir, edgeNormal) > 0.0f)
		{
			return false;
		}

		// If the quick intersection test passes we'll have to do the more expensive swept test.
		Scalar t;
		Point impact;
		if (!SweptStadiumSegmentXz(pParams->m_capsuleSegment, radius, segDir, edgeSeg, t, impact))
		{
			return false;
		}

		if (boundaryFlags != NavMesh::kBoundaryNone)
		{
			pParams->m_hitEdge = true;

			if (t < m_closestProbeStopTT)
			{
				m_closestProbeStopTT = t;

				pParams->m_hitVert[0] = v0;
				pParams->m_hitVert[1] = v1;
				pParams->m_hitEdgeIndex = iEdge;
				pParams->m_pHitPoly = pPoly;
				pParams->m_pHitPolyEx = pPolyEx;
				pParams->m_endPoint = pParams->m_start + VectorXz(t * segDir);
				pParams->m_edgeNormal = -Normalize(RotateY90(VectorXz(v0 - v1)));
				pParams->m_hitBoundaryFlags = boundaryFlags;
				pParams->m_impactPoint = impact;
			}
		}
		else if (t < m_closestProbeStopTT)
		{
			expand = true;
		}
	}
	else
	{
		const NavBlockerBits& obeyedBlockers = pParams->m_obeyedBlockers;
		const float radius = pParams->m_probeRadius;
		Segment seg;
		seg.a = pParams->m_start;
		seg.b = pParams->m_start + pParams->m_move;

		const Point v0 = pPolyEx ? pPolyEx->GetVertex(iEdge) : pPoly->GetVertex(iEdge);
		const Point v1 = pPolyEx ? pPolyEx->GetNextVertex(iEdge) : pPoly->GetNextVertex(iEdge);
		const Segment edgeSeg = Segment(v0, v1);
		const Vector edgeNormal = -Normalize(RotateY90(VectorXz(v0 - v1)));

		Vec2 tMapping = kZero;
		const Segment rseg = ConstructReducedSegment(seg, edgeSeg, radius, tMapping);

		float ttr;
		const bool rInt = IntersectSegmentStadiumXz(rseg, edgeSeg, radius, ttr);
		if (!rInt)
		{
			const float toEdgeDist = DistPointSegmentXz(rseg.a, v0, v1);
			if (toEdgeDist < (radius + 0.001f))
			{
				ttr = 0.0f;
			}
			else
			{
				return false;
			}
		}

		const float tt = LerpScale(0.0f, 1.0f, tMapping.x, tMapping.y, ttr);

		const Point probeStopPos = Lerp(seg.a, seg.b, tt);

		if (boundaryFlags != NavMesh::kBoundaryNone)
		{
			pParams->m_hitEdge = true;

			if (tt < m_closestProbeStopTT)
			{
				m_closestProbeStopTT = tt;

				DistPointSegment(probeStopPos, v0, v1, &pParams->m_impactPoint);

				pParams->m_hitVert[0] = v0;
				pParams->m_hitVert[1] = v1;
				pParams->m_hitEdgeIndex = iEdge;
				pParams->m_pHitPoly = pPoly;
				pParams->m_pHitPolyEx = pPolyEx;
				pParams->m_endPoint = probeStopPos;
				pParams->m_edgeNormal = edgeNormal;
				pParams->m_hitBoundaryFlags = boundaryFlags;
			}
		}
		else if (tt < m_closestProbeStopTT)
		{
			expand = true;
		}
	}

	return expand;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshCapsuleProbe::Finalize()
{
	if (!m_pProbeParams->m_hitEdge)
	{
		m_pProbeParams->m_endPoint	 = m_pProbeParams->m_start + m_pProbeParams->m_move;
		m_pProbeParams->m_pHitPoly	 = nullptr;
		m_pProbeParams->m_pHitPolyEx = nullptr;
	}

	if (m_pCurMesh != m_pStartMesh)
	{
		ChangeToNewNavMeshSpace(m_pStartMesh);
	}

	for (I32F iClosed = m_closedListSize - 1; iClosed >= 0; --iClosed)
	{
		const NavPolyEx* pPolyEx = m_pClosedList[iClosed].m_pPolyEx;
		const NavPoly* pPoly	 = m_pClosedList[iClosed].m_pPoly;
		const NavMesh* pMesh	 = pPoly ? pPoly->GetNavMesh() : nullptr;

		const Point posLs = (pMesh != m_pCurMesh)
								? pMesh->WorldToLocal(m_pCurMesh->LocalToWorld(m_pProbeParams->m_endPoint))
								: m_pProbeParams->m_endPoint;

		if (pPolyEx)
		{
			if (pPolyEx->PolyContainsPointLs(posLs))
			{
				m_pProbeParams->m_pReachedPoly	 = pPoly;
				m_pProbeParams->m_pReachedPolyEx = pPolyEx;
				m_pProbeParams->m_pReachedMesh	 = pMesh;
				break;
			}
		}
		else if (pPoly && pPoly->PolyContainsPointLs(posLs))
		{
			m_pProbeParams->m_pReachedPoly	 = pPoly;
			m_pProbeParams->m_pReachedPolyEx = pPolyEx;
			m_pProbeParams->m_pReachedMesh	 = pMesh;
			break;
		}
	}

	if (UNLIKELY(!m_pProbeParams->m_pReachedPoly))
	{
		for (I32F iClosed = m_closedListSize - 1; iClosed >= 0; --iClosed)
		{
			const NavPoly* pPoly = m_pClosedList[iClosed].m_pPoly;
			if (!pPoly)
				continue;

			const NavMesh* pMesh = pPoly->GetNavMesh();

			const Point posLs = (pMesh != m_pCurMesh)
									? pMesh->WorldToLocal(m_pCurMesh->LocalToWorld(m_pProbeParams->m_endPoint))
									: m_pProbeParams->m_endPoint;

			if (pPoly->SignedDistPointPolyXzSqr(posLs) < 0.01f)
			{
				m_pProbeParams->m_pReachedPoly = pPoly;
				m_pProbeParams->m_pReachedPolyEx = m_baseProbeParams.m_dynamicProbe
													   ? pPoly->FindContainingPolyExLs(posLs)
													   : nullptr;
				m_pProbeParams->m_pReachedMesh = pMesh;
				break;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshCapsuleProbe::ChangeToNewNavMeshSpace(const NavMesh* pNewMesh)
{
	if (!pNewMesh)
		return;

	if (NavMesh::ProbeParams* pParams = m_pProbeParams)
	{
		if (m_isStadium)
		{
			pParams->m_capsuleSegment.a = pNewMesh->WorldToLocal(m_pCurMesh->LocalToWorld(pParams->m_capsuleSegment.a));
			pParams->m_capsuleSegment.b = pNewMesh->WorldToLocal(m_pCurMesh->LocalToWorld(pParams->m_capsuleSegment.b));
		}

		pParams->m_start	   = pNewMesh->WorldToLocal(m_pCurMesh->LocalToWorld(pParams->m_start));
		pParams->m_hitVert[0]  = pNewMesh->WorldToLocal(m_pCurMesh->LocalToWorld(pParams->m_hitVert[0]));
		pParams->m_hitVert[1]  = pNewMesh->WorldToLocal(m_pCurMesh->LocalToWorld(pParams->m_hitVert[1]));
		pParams->m_endPoint	   = pNewMesh->WorldToLocal(m_pCurMesh->LocalToWorld(pParams->m_endPoint));
		pParams->m_impactPoint = pNewMesh->WorldToLocal(m_pCurMesh->LocalToWorld(pParams->m_impactPoint));
	}

	m_pCurMesh = pNewMesh;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshClearanceProbe::Init(Point_arg posLs, float radius, const NavMesh::BaseProbeParams& probeParams)
{
	NavPolyExpansionSearch::Init(probeParams, 384);

	m_posLs	 = posLs;
	m_radius = radius;

	float capsuleLenSqr = LengthSqr(probeParams.m_capsuleSegment.GetVec());

	m_isStadium = capsuleLenSqr > NDI_FLT_EPSILON;
	m_hitEdge	= false;
	m_bestDist	= (m_isStadium ? (Sqrt(capsuleLenSqr) / 2.0f) + radius : radius) + NDI_FLT_EPSILON;
	m_impactPt	= posLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshClearanceProbe::VisitEdge(const NavMesh* pMesh,
									  const NavPoly* pPoly,
									  const NavPolyEx* pPolyEx,
									  I32F iEdge,
									  NavMesh::BoundaryFlags boundaryFlags)
{
	Point v0 = pPolyEx ? pPolyEx->GetVertex(iEdge) : pPoly->GetVertex(iEdge);
	Point v1 = pPolyEx ? pPolyEx->GetNextVertex(iEdge) : pPoly->GetNextVertex(iEdge);

	if (pMesh != m_pStartMesh)
	{
		v0 = m_pStartMesh->WorldToLocal(pMesh->LocalToWorld(v0));
		v1 = m_pStartMesh->WorldToLocal(pMesh->LocalToWorld(v1));
	}

	Point closestPt = m_posLs;
	F32 edgeDist;

	if (m_isStadium)
	{
		Segment edgeSeg(v0, v1);

		Scalar t0, t1;
		edgeDist = DistSegmentSegmentXz(m_baseProbeParams.m_capsuleSegment, edgeSeg, t0, t1);
		closestPt = Lerp(edgeSeg.a, edgeSeg.b, t1);
	}
	else
	{
		edgeDist = DistPointSegmentXz(m_posLs, v0, v1, &closestPt);
	}

	if (edgeDist > (m_radius + kNudgeEpsilon))
		return false;

	bool expand = false;

	if (boundaryFlags != NavMesh::kBoundaryNone)
	{
		if (edgeDist < m_bestDist)
		{
			m_hitEdge  = true;
			m_bestDist = edgeDist;
			m_impactPt = closestPt;
		}
	}
	else
	{
		expand = true;
	}

	return expand;
}

/************************************************************************/
/*                                                                      */
/************************************************************************/
NavMeshStadiumDepenetrator::FeatureIndex NavMeshStadiumDepenetrator::kInvalidFeatureIndex = { -1, NavMeshStadiumDepenetrator::kEdgeFeature };

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::Init(Point_arg posLs,
									  const NavMesh* pInitialMesh,
									  const NavMesh::FindPointParams& probeParams)
{
	NAV_ASSERT(IsReasonable(posLs));
	NAV_ASSERT(IsReasonable(probeParams.m_depenRadius));

	m_depenRadius = probeParams.m_depenRadius;
	m_stadiumSeg = probeParams.m_capsuleSegment;

	// The stadium might not be centered so we need to account for that offset by translating the position to
	// the center of the stadium (we're correct this latter when we output the final position).
	m_posLsOffset = posLs - Lerp(m_stadiumSeg.a, m_stadiumSeg.b, 0.5f);
	m_maxDepenRadius = m_depenRadius + LengthXz(m_stadiumSeg.GetVec()) / 2.0f;
	m_invDepenRadius = 1.0f / m_depenRadius;
	m_isStadium = true;

	const F32 collectRadius = m_maxDepenRadius * 2.0f;

	NavMeshClearanceProbe::Init(posLs - m_posLsOffset, collectRadius, probeParams);

	m_pMesh = pInitialMesh;

	m_pEdges = NDI_NEW Edge[64];
	m_maxEdges = 64;
	m_numEdges = 0;

	m_pResolvedPoly = nullptr;
	m_pResolvedPolyEx = nullptr;
	m_closestEdgeDist = kLargeFloat;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshStadiumDepenetrator::VisitEdge(const NavMesh* pMesh,
										   const NavPoly* pPoly,
										   const NavPolyEx* pPolyEx,
										   I32F iEdge,
										   NavMesh::BoundaryFlags boundaryFlags)
{
	PROFILE(Navigation, StadiumDepen_Visit);

	if (NavMeshClearanceProbe::VisitEdge(pMesh, pPoly, pPolyEx, iEdge, boundaryFlags))
		return true;

	if (boundaryFlags == NavMesh::kBoundaryNone)
		return false;

	// blocking edge, record it
	if (m_numEdges >= m_maxEdges)
	{
		MsgWarn("NavMeshStadiumDepenetrator needs more than %d edges\n", (int)m_maxEdges);
		return false;
	}

	Point v0 = pPolyEx ? pPolyEx->GetVertex(iEdge) : pPoly->GetVertex(iEdge);
	Point v1 = pPolyEx ? pPolyEx->GetNextVertex(iEdge) : pPoly->GetNextVertex(iEdge);

	if (pMesh != m_pMesh)
	{
		v0 = m_pMesh->WorldToLocal(pMesh->LocalToWorld(v0));
		v1 = m_pMesh->WorldToLocal(pMesh->LocalToWorld(v1));
	}

	Vector edgeNormal = -AsUnitVectorXz(RotateY90(VectorXz(v0 - v1)), kZero);

	Scalar t0, t1;
	F32 distToPos = DistSegmentSegmentXz(m_stadiumSeg, Segment(v0, v1), t0, t1);

	NAV_ASSERT(IsReasonable(distToPos));

	bool expand = false;

	const U32F boundaryType = boundaryFlags & NavMesh::kBoundaryTypeAll;

	if (((boundaryFlags & NavMesh::kBoundaryInside) != 0) && ((boundaryType & NavMesh::kBoundaryTypeStealth) == 0))
	{
		edgeNormal = -edgeNormal;
		distToPos = 0.0f;
		expand = true;
	}

	F32 da = DotXz(edgeNormal, m_stadiumSeg.a);
	F32 db = DotXz(edgeNormal, m_stadiumSeg.b);
	F32 offsetDist = m_depenRadius + (Abs(db - da) / 2.0f);

	const Vector edgeDir = AsUnitVectorXz(VectorXz(v1 - v0), kZero);
	const Vector offset = edgeNormal * offsetDist;

	Edge& newEdge = m_pEdges[m_numEdges];
	++m_numEdges;

	newEdge.m_v0 = v0;
	newEdge.m_v1 = v1;
	newEdge.m_normal = edgeNormal;
	newEdge.m_pMesh = pMesh;
	newEdge.m_distToPos = distToPos;
	newEdge.m_offset0 = v0 + offset;
	newEdge.m_offset1 = v1 + offset;
	newEdge.m_neighborParallel0 = -1;
	newEdge.m_neighborParallel1 = -1;
	newEdge.m_neighborInterior0 = -1;
	newEdge.m_neighborInterior1 = -1;
	newEdge.m_iCorner0 = -1;
	newEdge.m_iCorner1 = -1;
	newEdge.m_zeroLength = false;
	newEdge.m_fromLink = pPolyEx ? pPolyEx->IsSourceLink() || pPolyEx->IsSourcePreLink()
								 : pPoly->IsLink() || pPoly->IsPreLink();

	m_closestEdgeDist = Min(m_closestEdgeDist, newEdge.m_distToPos);

	return expand;
}

/// -------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::FinalizePoint(Point_arg pos)
{
	m_resolvedPoint = pos + m_posLsOffset;

	const NavPolyListEntry containing = GetContainingPoly(m_resolvedPoint);
	m_pResolvedPoly = containing.m_pPoly;
	m_pResolvedPolyEx = containing.m_pPolyEx;
}

/// -------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::Finalize()
{
	PROFILE(Navigation, StadiumDepen_Finalize);

	const bool debugDraw = m_baseProbeParams.m_debugDraw;

	if ((m_numEdges == 0) || (m_closestEdgeDist >= m_depenRadius))
	{
		FinalizePoint(m_posLs);
		return; // nothing to do
	}

	F32 searchRadius = (m_radius + m_maxDepenRadius) * 2.15f;

	// worst case is isolated edges, each with 2 fake corners
	const U32 maxCorners = 2 * m_numEdges;
	ExtCorner* pCorners = NDI_NEW ExtCorner[maxCorners];
	U32 numCorners = GenerateCorners(pCorners, maxCorners);

	for (I32 iEdge = 0; iEdge < m_numEdges; iEdge++)
	{
		for (I32 iOtherEdge = iEdge + 1; iOtherEdge < m_numEdges; iOtherEdge++)
		{
			StretchInteriorCornerEdges(iEdge, iOtherEdge);
		}
	}

	for (I32 iEdge = 0; iEdge < m_numEdges; iEdge++)
	{
		for (I32 iOtherEdge = iEdge + 1; iOtherEdge < m_numEdges; iOtherEdge++)
		{
			StretchInteriorCornersAgainstNeighboringCorners(pCorners, iEdge, iOtherEdge);
		}
	}

	for (I32 iCorner = 0; iCorner < numCorners; iCorner++)
	{
		ExtCorner& corner = pCorners[iCorner];

		for (I32 iOtherCorner = iCorner + 1; iOtherCorner < numCorners; iOtherCorner++)
		{
			ExtCorner& other = pCorners[iOtherCorner];
			ClipCornerAgainstCorner(corner, other);
		}
	}

	for (I32 iCorner = 0; iCorner < numCorners; iCorner++)
	{
		ClipEdgesAgainstCorner(iCorner, pCorners, numCorners);
	}

	for (I32 iEdge = 0; iEdge < m_numEdges; iEdge++)
	{
		Edge& edge = m_pEdges[iEdge];
		if (LengthXz(edge.m_offset0 - edge.m_offset1) <= 0.00001f)
		{
			edge.m_zeroLength = true;
		}
	}

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		for (U32F iEdge = 0; iEdge < m_numEdges; ++iEdge)
		{
			const Edge& e = m_pEdges[iEdge];
			DebugDrawEdge(e, 0.1f, kColorPink, StringBuilder<64>("%d (%0.3fm)", (int)iEdge, e.m_distToPos).c_str());
		}
	}

	const U32 maxIntersections = Min(m_numEdges * m_numEdges * 2, 1024ULL);
	Intersection* pIntersections = NDI_NEW Intersection[maxIntersections];
	LocalIntersection* pLocalIntersections = NDI_NEW LocalIntersection[maxIntersections];
	U32 numIntersections = GenerateIntersections(pIntersections, maxIntersections, pCorners, numCorners);

	F32 bestDist = kLargeFloat;
	Point bestPos = m_posLs;

	for (U32F iEdge = 0; iEdge < m_numEdges; ++iEdge)
	{
		const FeatureIndex edgeFeatureIdx = FeatureIndex::FromEdge(iEdge);

		FindBestPositionOnEdge(debugDraw, searchRadius, pCorners, numCorners,
							   edgeFeatureIdx, pIntersections, numIntersections,
							   pLocalIntersections, bestPos, bestDist);
	}

	for (U32 iCorner = 0; iCorner < numCorners; ++iCorner)
	{
		const ExtCorner& corner = pCorners[iCorner];
		const FeatureIndex cornerFeatureIdx = FeatureIndex::FromCorner(iCorner);

		if (corner.m_hasBeginEdge)
		{
			FindBestPositionOnEdge(debugDraw, searchRadius, pCorners, numCorners,
								   cornerFeatureIdx.As(kCornerBeginEdgeFeature),
								   pIntersections, numIntersections, pLocalIntersections,
								   bestPos, bestDist);
		}

		if (corner.m_hasArc0)
		{
			FindBestPositionOnArc(debugDraw, searchRadius, pCorners, numCorners,
								  cornerFeatureIdx.As(kCornerArc0Feature),
								  pIntersections, numIntersections, pLocalIntersections,
								  bestPos, bestDist);
		}

		if (corner.m_hasEdgeBetweenArcs)
		{
			FindBestPositionOnEdge(debugDraw, searchRadius, pCorners, numCorners,
								   cornerFeatureIdx.As(kCornerBetweenEdgeFeature),
								   pIntersections, numIntersections, pLocalIntersections,
								   bestPos, bestDist);
		}

		if (corner.m_hasArc1)
		{
			FindBestPositionOnArc(debugDraw, searchRadius, pCorners, numCorners,
								  cornerFeatureIdx.As(kCornerArc1Feature),
								  pIntersections, numIntersections, pLocalIntersections,
								  bestPos, bestDist);
		}

		if (corner.m_hasEndEdge)
		{
			FindBestPositionOnEdge(debugDraw, searchRadius, pCorners, numCorners,
								   cornerFeatureIdx.As(kCornerEndEdgeFeature),
								   pIntersections, numIntersections, pLocalIntersections,
								   bestPos, bestDist);
		}
	}

	if (bestDist < kLargeFloat)
	{
		FinalizePoint(bestPos);
	}

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		g_prim.Draw(DebugCross(m_pMesh->LocalToWorld(m_resolvedPoint) - m_posLsOffset, 0.2f, kColorCyan, kPrimEnableHiddenLineAlpha));

		static U32 test = 1;
		for (U32F iCorner = 0; iCorner < numCorners; ++iCorner)
		{
			DebugDrawCorner(pCorners[iCorner], 0.1f, kColorRedTrans, StringBuilder<64>("c%d", iCorner).c_str());
		}

		for (U32F iInt = 0; iInt < numIntersections; ++iInt)
		{
			const Intersection& inter = pIntersections[iInt];

			const Point intPosWs = m_pMesh->LocalToWorld(inter.m_intPos);

			g_prim.Draw(DebugCross(intPosWs + Vector(0.0f, 0.1f, 0.0f), 0.025f, kColorOrange));
			g_prim.Draw(DebugString(intPosWs + Vector(0.0f, 0.1f, 0.0f), StringBuilder<64>("%d", iInt).c_str(), kColorOrange, 0.5f));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::FindBestPositionOnEdge(bool debugDraw,
														F32 searchRadius,
														const ExtCorner* pCorners,
														U32 numCorners,
														FeatureIndex featureIdx,
														Intersection* pIntersections,
														U32 numIntersections,
														LocalIntersection* pLocalIntersections,
														Point& bestPos,
														F32& bestDist) const
{
	const F32 kTolerance = 0.0001f;

	Segment edge = GetEdgeSegment(featureIdx, pCorners);
	Vector edgeVec = VectorXz(edge.GetVec());

	if (LengthXz(edgeVec) < kTolerance)
	{
		return;
	}

	if (featureIdx.m_type == kEdgeFeature)
	{
		edgeVec = VectorXz(m_pEdges[featureIdx.m_index].m_v1 - m_pEdges[featureIdx.m_index].m_v0);
	}

	Vector normal = GetEdgeNormal(featureIdx, pCorners);

	const U32 numLocalIntersections = GatherIntersectionsForEdge(featureIdx,
																 edgeVec,
																 pIntersections,
																 numIntersections,
																 pCorners,
																 numCorners,
																 pLocalIntersections);

	const bool startClear = IsPointClear(edge.a,
										 featureIdx,
										 kInvalidFeatureIndex,
										 searchRadius,
										 normal,
										 pCorners,
										 numCorners);

	float startTT = startClear ? 0.0f : -1.0f;

	for (U32 iInt = 0; iInt < numLocalIntersections; ++iInt)
	{
		const LocalIntersection& lint = pLocalIntersections[iInt];

		if (lint.m_forward)
		{
			startTT = lint.m_tt;
		}
		else if (startTT >= 0.0f)
		{
			const Point v0 = Lerp(edge.a, edge.b, startTT);
			const Point v1 = Lerp(edge.a, edge.b, lint.m_tt);

			Scalar closestT;
			const float d = DistPointSegmentXz(m_posLs, v0, v1, nullptr, &closestT);

			if (d < bestDist)
			{
				bestDist = d;
				bestPos = Lerp(v0, v1, closestT);
			}

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				DebugDrawEdgeRange(v0, v1);
			}

			startTT = -1.0f;
		}
	}

	if ((startTT >= 0.0f) &&
		IsPointClear(edge.b,
					 featureIdx,
					 kInvalidFeatureIndex,
					 searchRadius,
					 normal,
					 pCorners,
					 numCorners))
	{
		const Point v0 = Lerp(edge.a, edge.b, startTT);
		const Point v1 = Lerp(edge.a, edge.b, 1.0f);

		Scalar closestT;
		const float d = DistPointSegmentXz(m_posLs, v0, v1, nullptr, &closestT);

		if (d < bestDist)
		{
			bestDist = d;
			bestPos = Lerp(v0, v1, closestT);
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			DebugDrawEdgeRange(v0, v1);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::FindBestPositionOnArc(bool debugDraw,
													   F32 searchRadius,
													   const ExtCorner* pCorners,
													   U32 numCorners,
													   FeatureIndex featureIdx,
													   Intersection* pIntersections,
													   U32 numIntersections,
													   LocalIntersection* pLocalIntersections,
													   Point& bestPos,
													   F32& bestDist) const
{
	const U32F numLocalIntersections = GatherIntersectionsForArc(featureIdx,
																 pIntersections,
																 numIntersections,
																 pCorners,
																 numCorners,
																 pLocalIntersections);

	ExtCornerArc arc = GetArc(featureIdx, pCorners);

	// Ensure that the arc rotates counter clockwise.
	if (Sign(CrossY(arc.m_begin - arc.m_origin, arc.m_end - arc.m_origin)) < 0.0f)
	{
		Swap(arc.m_begin, arc.m_end);
	}

	F32 halfAngleRad = 0.0f;
	Vector normal = GetArcNormalAndHalfAngleRad(arc, halfAngleRad);

	const Point startPos = arc.m_begin;
	const bool startClear = IsPointClear(startPos,
										 featureIdx,
										 kInvalidFeatureIndex,
										 searchRadius,
										 kZero,
										 pCorners,
										 numCorners);
	float startAngle = startClear ? -halfAngleRad : -kLargeFloat;

	for (U32F iInt = 0; iInt < numLocalIntersections; ++iInt)
	{
		const LocalIntersection& lint = pLocalIntersections[iInt];

		if (lint.m_forward)
		{
			startAngle = lint.m_tt;
		}
		else if (startAngle >= -halfAngleRad)
		{
			Point closestPt;
			const float d = DistPointArcXz(m_posLs,
										   arc.m_origin,
										   normal,
										   m_depenRadius,
										   startAngle,
										   lint.m_tt,
										   &closestPt);

			if (d < bestDist)
			{
				bestDist = d;
				bestPos = closestPt;
			}

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				DebugDrawCornerRange(arc.m_origin, normal, startAngle, lint.m_tt);
			}

			startAngle = -kLargeFloat;
		}
	}

	if (startAngle >= -halfAngleRad)
	{
		if (IsPointClear(arc.m_end,
						 featureIdx,
						 kInvalidFeatureIndex,
						 searchRadius,
						 kZero,
						 pCorners,
						 numCorners))
		{
			Point closestPt;
			const float d = DistPointArcXz(m_posLs,
										   arc.m_origin,
										   normal,
										   m_depenRadius,
										   startAngle,
										   halfAngleRad,
										   &closestPt);

			if (d < bestDist)
			{
				bestDist = d;
				bestPos = closestPt;
			}

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				DebugDrawCornerRange(arc.m_origin, normal, startAngle, halfAngleRad);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NavMeshStadiumDepenetrator::GetArcNormal(const ExtCornerArc& arc) const
{
	Vector n0 = (arc.m_begin - arc.m_origin) * m_invDepenRadius;
	Vector n1 = (arc.m_end - arc.m_origin) * m_invDepenRadius;
	Vector arcNormal = AsUnitVectorXz(n0 + n1, kUnitZAxis);

	return arcNormal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NavMeshStadiumDepenetrator::GetArcNormalAndHalfAngleRad(const ExtCornerArc& arc, F32& halfAngleRad) const
{
	Vector n0 = (arc.m_begin - arc.m_origin) * m_invDepenRadius;
	Vector n1 = (arc.m_end - arc.m_origin) * m_invDepenRadius;
	Vector arcNormal = AsUnitVectorXz(n0 + n1, kUnitZAxis);

	F32 cosHalfAngle = DotXz(arcNormal, n0);
	halfAngleRad = SafeAcos(cosHalfAngle);

	return arcNormal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshStadiumDepenetrator::FindCornerBeginPlane(const ExtCorner& corner, Vector& n, F32& d) const
{
	if (corner.m_hasBeginEdge)
	{
		n = NormalizeXz(VectorXz(corner.m_arc0.m_begin - corner.m_begin));
		d = DotXz(n, corner.m_begin);
		return true;
	}
	else if (corner.m_hasArc0)
	{
		n = RotateY90(NormalizeXz(VectorXz(corner.m_arc0.m_origin - corner.m_arc0.m_begin)));
		d = DotXz(n, corner.m_arc0.m_begin);
		return true;
	}
	else if (corner.m_hasEdgeBetweenArcs)
	{
		n = NormalizeXz(VectorXz(corner.m_arc0.m_end - corner.m_arc1.m_begin));
		d = DotXz(n, corner.m_arc0.m_end);
		return true;
	}
	else if (corner.m_hasArc1)
	{
		n = RotateY90(NormalizeXz(VectorXz(corner.m_arc1.m_origin - corner.m_arc1.m_begin)));
		d = DotXz(n, corner.m_arc1.m_begin);
		return true;
	}
	else if (corner.m_hasEndEdge)
	{
		n = NormalizeXz(VectorXz(corner.m_end - corner.m_arc1.m_end));
		d = DotXz(n, corner.m_arc1.m_end);
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshStadiumDepenetrator::FindCornerEndPlane(const ExtCorner& corner, Vector& n, F32& d) const
{
	if (corner.m_hasEndEdge)
	{
		n = NormalizeXz(VectorXz(corner.m_arc1.m_end - corner.m_end));
		d = DotXz(n, corner.m_end);
		return true;
	}
	else if (corner.m_hasArc1)
	{
		n = RotateY90(NormalizeXz(VectorXz(corner.m_arc1.m_end - corner.m_arc1.m_origin)));
		d = DotXz(n, corner.m_arc1.m_end);
		return true;
	}
	else if (corner.m_hasEdgeBetweenArcs)
	{
		n = NormalizeXz(VectorXz(corner.m_arc0.m_end - corner.m_arc1.m_begin));
		d = DotXz(n, corner.m_arc1.m_begin);
		return true;
	}
	else if (corner.m_hasArc0)
	{
		n = RotateY90(NormalizeXz(VectorXz(corner.m_arc0.m_end - corner.m_arc0.m_origin)));
		d = DotXz(n, corner.m_arc0.m_end);
		return true;
	}
	else if (corner.m_hasBeginEdge)
	{
		n = NormalizeXz(VectorXz(corner.m_arc0.m_begin - corner.m_begin));
		d = DotXz(n, corner.m_arc0.m_begin);
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::ClipBeginEdge(Vector n[2], F32 d[2], Point& a, Point& b) const
{
	if (d[0] < kLargeFloat)
	{
		Vector r = VectorXz(b - a);
		F32 db = DotXz(n[0], a);
		F32 t = (d[0] - db) / DotXz(n[0], r);

		// We've intersected the plane
		if (t > 0.0f && t < 1.0f)
		{
			b = a + r * t;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::ClipEndEdge(Vector n[2], F32 d[2], Point& a, Point& b) const
{
	if (d[1] < kLargeFloat)
	{
		Vector r = VectorXz(b - a);
		F32 db = DotXz(n[1], a);

		F32 t = (d[1] - db) / DotXz(n[1], r);

		// We've intersected the plane
		if (t > 0.0f && t < 1.0f)
		{
			a = a + r * t;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::ClipMidEdge(Vector_arg mid, F32 midDist, Vector n[2], F32 d[2], Point& a, Point& b) const
{
	if (midDist < kLargeFloat && DotXz(a, mid) > midDist && DotXz(b, mid) > midDist)
	{
		a = b;
	}

	if (d[0] < kLargeFloat && d[1] < kLargeFloat &&
		DotXz(n[0], a) > d[0] && DotXz(n[0], b) > d[0] &&
		DotXz(n[1], a) > d[1] && DotXz(n[1], b) > d[1])
	{
		a = b;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::ClipEdgesAgainstCorner(U32 cornerIdx, ExtCorner* pCorners, U32 numCorners)
{
	const F32 kTolerance = 0.0001f;
	CONST_EXPR I32 kMaxIterations = 1000;

	ExtCorner& corner = pCorners[cornerIdx];

	Edge& edge0 = m_pEdges[corner.m_iEdge0];
	Edge& edge1 = m_pEdges[corner.m_iEdge1];

	Vector n[2] = { kZero, kZero };
	F32 d[2] = { kLargeFloat, kLargeFloat };

	FindCornerBeginPlane(corner, n[0], d[0]);
	FindCornerEndPlane(corner, n[1], d[1]);

	auto getNextParallelNeighbor = [](I32 start, I32 prev, const Edge& edge)
	{
		I32 next = edge.m_neighborParallel0;
		if (next == start || next == prev || next == -1)
		{
			next = edge.m_neighborParallel1;
		}

		return next == start || next == prev ? -1 : next;
	};

	// Also clip edges that are on a straight line with the corners edges.
	if (!corner.m_edge0Fake)
	{
		ClipBeginEdge(n, d, edge0.m_offset0, edge0.m_offset1);
		ClipEndEdge(n, d, edge0.m_offset0, edge0.m_offset1);

		I32 start = corner.m_iEdge0;
		I32 prev = start;
		I32 curr = getNextParallelNeighbor(start, prev, edge0);

		// sometimes we get stuck in an infinite loop here. Most likely we end up in a loop of nav polys that doesn't include the start.
		// until we have a chance to figure that out the number of iterations must be bounded
		I32 iteration = 0;
		while (curr != -1 && iteration < kMaxIterations)
		{
			Edge& other = m_pEdges[curr];
			ClipBeginEdge(n, d, other.m_offset0, other.m_offset1);
			ClipEndEdge(n, d, other.m_offset0, other.m_offset1);

			I32 next = getNextParallelNeighbor(start, prev, other);
			prev = curr;
			curr = next;
			++iteration;
		}
	}

	if (!corner.m_edge1Fake)
	{
		ClipEndEdge(n, d, edge1.m_offset0, edge1.m_offset1);
		ClipBeginEdge(n, d, edge1.m_offset0, edge1.m_offset1);

		I32 start = corner.m_iEdge1;
		I32 prev = start;
		I32 curr = getNextParallelNeighbor(start, prev, edge1);

		I32 iteration = 0;
		while (curr != -1 && iteration < kMaxIterations)
		{
			Edge& other = m_pEdges[curr];
			ClipEndEdge(n, d, other.m_offset0, other.m_offset1);
			ClipBeginEdge(n, d, other.m_offset0, other.m_offset1);

			I32 next = getNextParallelNeighbor(start, prev, other);
			prev = curr;
			curr = next;
			++iteration;
		}
	}

	// For small edges clipping might not be enough, so do one last clip.
	{
		Vector midPlane = kZero;
		F32 midPlaneDist = kLargeFloat;

		if (corner.m_hasEdgeBetweenArcs)
		{
			midPlane = Normalize(RotateY90(VectorXz(corner.m_arc1.m_begin - corner.m_arc0.m_end)));
			midPlaneDist = DotXz(midPlane, corner.m_arc0.m_end);

			if (DotXz(midPlane, corner.m_vert) > midPlaneDist)
			{
				midPlane = -midPlane;
				midPlaneDist = -midPlaneDist;
			}
		}

		if (!corner.m_edge0Fake)
		{
			// Disable clipping against the plane defined by the middle corner edge if it aligns with the edge.
			F32 midDist = midPlaneDist;
			if (Abs(DotXz(edge0.m_normal, midPlane) - 1.0f) < kTolerance)
			{
				midDist = kLargeFloat;
			}

			ClipMidEdge(midPlane, midDist, n, d, edge0.m_offset0, edge0.m_offset1);

			I32 start = corner.m_iEdge0;
			I32 prev = start;
			I32 curr = getNextParallelNeighbor(start, prev, edge0);

			I32 iteration = 0;
			while (curr != -1 && iteration < kMaxIterations)
			{
				Edge& other = m_pEdges[curr];
				ClipMidEdge(midPlane, midDist, n, d, other.m_offset0, other.m_offset1);

				I32 next = getNextParallelNeighbor(start, prev, other);
				prev = curr;
				curr = next;
				++iteration;
			}
		}

		if (!corner.m_edge1Fake)
		{
			// Disable clipping against the plane defined by the middle corner edge if it aligns with the edge.
			F32 midDist = midPlaneDist;
			if (Abs(DotXz(edge1.m_normal, midPlane) - 1.0f) < kTolerance)
			{
				midDist = kLargeFloat;
			}

			ClipMidEdge(midPlane, midDist, n, d, edge1.m_offset0, edge1.m_offset1);

			I32 start = corner.m_iEdge1;
			I32 prev = start;
			I32 curr = getNextParallelNeighbor(start, prev, edge1);

			I32 iteration = 0;
			while (curr != -1 && iteration < kMaxIterations)
			{
				Edge& other = m_pEdges[curr];
				ClipMidEdge(midPlane, midDist, n, d, other.m_offset0, other.m_offset1);

				I32 next = getNextParallelNeighbor(start, prev, other);
				prev = curr;
				curr = next;
				++iteration;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::ClipCornerEdge(Vector_arg n, F32 d, Point& begin, Point& end) const
{
	Vector r = VectorXz(end - begin);
	F32 db = DotXz(n, begin);
	F32 t = (d - db) / DotXz(n, r);

	// We've intersected the plane
	if (t > 0.0f && t < 1.0f)
	{
		end = begin + r * t;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::ClipCorner(Vector n, F32 d, ExtCorner& corner) const
{
	const F32 kTolerance = 0.00001f;

	if (corner.m_hasBeginEdge)
	{
		ClipCornerEdge(n, d, corner.m_arc0.m_begin, corner.m_begin);
		corner.m_hasBeginEdge = LengthSqr(VectorXz(corner.m_begin - corner.m_arc0.m_begin)) > kTolerance;
	}

	if (corner.m_hasEdgeBetweenArcs && !corner.m_hasArc1)
	{
		ClipCornerEdge(n, d, corner.m_arc0.m_end, corner.m_arc1.m_begin);
		corner.m_hasEdgeBetweenArcs = LengthSqr(VectorXz(corner.m_arc1.m_begin - corner.m_arc0.m_end)) > kTolerance;
	}

	if (corner.m_hasEndEdge)
	{
		ClipCornerEdge(n, d, corner.m_arc1.m_end, corner.m_end);
		corner.m_hasEndEdge = LengthSqr(VectorXz(corner.m_end - corner.m_arc1.m_end)) > kTolerance;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshStadiumDepenetrator::IsCornerDirectlyConnected(const ExtCorner& corner, const Edge& edge) const
{
	const F32 kTolerance = 0.0000001f;

	if (corner.m_hasBeginEdge)
	{
		Point begin = corner.m_begin;
		if (DistXz(begin, edge.m_offset0) < kTolerance || DistXz(begin, edge.m_offset1) < kTolerance)
		{
			return true;
		}
	}

	if (corner.m_hasEndEdge || (corner.m_hasEdgeBetweenArcs && !corner.m_hasArc1))
	{
		Point end;
		if (corner.m_hasEndEdge)
		{
			end = corner.m_end;
		}
		else // corner.m_hasEdgeBetweenArcs && !corner.m_hasArc1
		{
			end = corner.m_arc1.m_begin;
		}

		if (DistXz(end, edge.m_offset0) < kTolerance || DistXz(end, edge.m_offset1) < kTolerance)
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 NavMeshStadiumDepenetrator::FindFirstConnectedEdge(I32 start) const
{
	if (m_pEdges[start].m_neighborParallel0 == -1)
	{
		return start;
	}

	I32 iterations = 0;
	I32 prev = m_pEdges[start].m_neighborParallel0;
	while (m_pEdges[prev].m_neighborParallel0 != -1 && iterations < m_maxEdges)
	{
		prev = m_pEdges[prev].m_neighborParallel0;
		iterations++;
	}

	return prev;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 NavMeshStadiumDepenetrator::FindMatchingConnectedEdge(I32 start, I32 find0, I32 find1) const
{
	I32 iterations = 0;
	do
	{
		if (start == find0 || start == find1)
		{
			return start;
		}

		start = m_pEdges[start].m_neighborParallel1;
		iterations++;
	}
	while (start != -1 && iterations < m_maxEdges);

	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::ClipCornerAgainstCorner(ExtCorner& corner0, ExtCorner& corner1) const
{
	I32 start0 = FindFirstConnectedEdge(corner0.m_iEdge0);
	I32 end0 = FindFirstConnectedEdge(corner0.m_iEdge1);

	I32 match0 = FindMatchingConnectedEdge(start0, corner1.m_iEdge0, corner1.m_iEdge1);
	I32 match1 = FindMatchingConnectedEdge(end0, corner1.m_iEdge0, corner1.m_iEdge1);

	if (match0 == -1 && match1 == -1)
	{
		// There is no shared edge between the corners so no need to clip.
		return;
	}

	I32 sharedEdge0;
	I32 sharedEdge1;

	// Determine if only one corner is connected directly to the shared edge.
	bool connected0 = false;
	bool connected1 = false;

	if (match0 != -1)
	{
		connected0 = IsCornerDirectlyConnected(corner0, m_pEdges[corner0.m_iEdge0]);
		connected1 = IsCornerDirectlyConnected(corner1, m_pEdges[match0]);
	}
	else
	{
		connected0 = IsCornerDirectlyConnected(corner0, m_pEdges[corner0.m_iEdge1]);
		connected1 = IsCornerDirectlyConnected(corner1, m_pEdges[match1]);
	}

	ExtCorner* first = nullptr;
	ExtCorner* second = nullptr;

	if (connected0 != connected1)
	{
		first = connected0 ? &corner1 : &corner0;
		second = connected0 ? &corner0 : &corner1;
	}
	else
	{
		first = &corner0;
		second = &corner1;
	}

	Vector n;
	F32 d;

	if (FindCornerBeginPlane(*first, n, d))
	{
		ClipCorner(n, d, *second);
	}

	if (FindCornerEndPlane(*first, n, d))
	{
		ClipCorner(n, d, *second);
	}

	if (FindCornerBeginPlane(*second, n, d))
	{
		ClipCorner(n, d, *first);
	}

	if (FindCornerEndPlane(*second, n, d))
	{
		ClipCorner(n, d, *first);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsBetweenArcXz(Vector_arg arcBegin, Vector_arg arcEnd, Vector_arg test)
{
	F32 sgn = CrossY(arcBegin, arcEnd);

	F32 sb = CrossY(arcBegin, test);
	F32 se = CrossY(arcEnd, test);

	return sgn * sb > 0.0f && -sgn * se > 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IntersectCircleSegmentXz(Point_arg center, F32 r, const Segment& s, F32& t0, F32& t1)
{
	Vector segDir = VectorXz(s.GetVec());
	Vector v = s.a - center;

	F32 a = DotXz(segDir, segDir);
	F32 b = 2.0f * DotXz(segDir, v);
	F32 c = DotXz(v, v) - r * r;

	F32 discrim = b * b - 4 * a * c;
	if (discrim == 0.0f)
	{
		t0 = -b / (2.0f * a);
		t1 = t0;
	}
	else if (discrim > 0.0f)
	{
		discrim = Sqrt(discrim);

		t0 = (-b + discrim) / (2.0f * a);
		t1 = (-b - discrim) / (2.0f * a);
	}
	else
	{
		return false;
	}

	bool onEdge0 = t0 > 0.0f && t0 < 1.0f;
	bool onEdge1 = t1 > 0.0f && t1 < 1.0f;

	if (onEdge0 && !onEdge1)
	{
		t1 = t0;
	}
	else if (onEdge1 && !onEdge0)
	{
		t0 = t1;
	}

	return onEdge0 || onEdge1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IntersectCircleCircleXz(Point_arg c0, Point_arg c1, F32 r, Point& i0, Point& i1)
{
	const F32 kTolerance = 0.00001f;

	Vector d = c1 - c0;
	F32 dist = LengthXz(d);
	if (dist >= 2.0f * r || dist < kTolerance)
	{
		// The circles are either too far away (and there is no more then 1 intersection) or they are on top of each other
		// (in which case there are infinitely many intersections). In both cases we can ignore the intersections.
		return false;
	}

	F32 a = (dist * dist) / (2.0f * dist);
	F32 h = Sqrt(r * r - a * a);
	Point p = c0 + (a / dist) * d;

	i0 = Point(p.X() + (h * d.Z()) / dist, c0.Y(), p.Z() - (h * d.X()) / dist);
	i1 = Point(p.X() - (h * d.Z()) / dist, c0.Y(), p.Z() + (h * d.X()) / dist);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::CreateCorner(I32 cornerIdx,
											  ExtCorner& cornerOut,
											  Point_arg vert,
											  Vector_arg normal0,
											  Vector_arg normal1,
											  I32 iEdge0,
											  I32 iEdge1)
{
	Vector n0 = VectorXz(normal0);
	Vector n1 = VectorXz(normal1);

	cornerOut.m_vert = vert;

	cornerOut.m_edge0Fake = iEdge0 == -1;
	cornerOut.m_edge1Fake = iEdge1 == -1;

	cornerOut.m_iEdge0 = cornerOut.m_edge0Fake ? iEdge1 : iEdge0;
	cornerOut.m_iEdge1 = cornerOut.m_edge1Fake ? iEdge0 : iEdge1;

	Segment e0(m_pEdges[cornerOut.m_iEdge0].m_offset0, m_pEdges[cornerOut.m_iEdge0].m_offset1);
	Segment e1(m_pEdges[cornerOut.m_iEdge1].m_offset0, m_pEdges[cornerOut.m_iEdge1].m_offset1);

	// Create a fake second edge that is rotated 90 degrees from the other edge
	if (iEdge0 == -1 || iEdge1 == -1)
	{
		Vector fakeNormal = iEdge0 == -1 ? n0 : n1;
		Vector realNormal = iEdge0 == -1 ? n1 : n0;

		F32 da = DotXz(fakeNormal, m_stadiumSeg.a);
		F32 db = DotXz(fakeNormal, m_stadiumSeg.b);
		F32 offsetDist = m_depenRadius + (Abs(db - da) / 2.0f);

		Segment fake(vert - m_maxDepenRadius * realNormal, vert);
		fake.a += offsetDist * fakeNormal;
		fake.b += offsetDist * fakeNormal;

		if (iEdge0 == -1)
		{
			e0 = fake;
		}
		else
		{
			e1 = fake;
		}
	}

	const Vector cornerNormal = SafeNormalize(n0 + n1, kUnitZAxis);
	if (CrossY(n0, cornerNormal) > 0.0f)
	{
		Swap(cornerOut.m_iEdge0, cornerOut.m_iEdge1);
		Swap(n0, n1);
		Swap(e0, e1);

		if (cornerOut.m_edge0Fake)
		{
			Swap(e1.a, e1.b);
		}

		bool tmp = cornerOut.m_edge0Fake;
		cornerOut.m_edge0Fake = cornerOut.m_edge1Fake;
		cornerOut.m_edge1Fake = tmp;
	}

	Vector edgeNormal = n0;

	Vector edgeDir = AsUnitVectorXz(VectorXz(e0.GetVec()), kZero);
	Vector segDir = NormalizeXz(VectorXz(m_stadiumSeg.GetVec()));
	Vector segNormal = RotateY90(segDir);

	// The plane that we clip the corner against (i.e. we construct the corner shape up to the point where we cross these planes)
	Vector clipNormal = n1;
	F32 clipDist = DotXz(clipNormal, e1.a);

	F32 h = LengthXz(m_stadiumSeg.GetVec());

	// Compute the projected width and height of the capsule segment with respect to the edge
	F32 dha = DotXz(edgeNormal, m_stadiumSeg.a);
	F32 dhb = DotXz(edgeNormal, m_stadiumSeg.b);

	F32 dwa = DotXz(edgeDir, m_stadiumSeg.a);
	F32 dwb = DotXz(edgeDir, m_stadiumSeg.b);

	F32 ph = Abs(dhb - dha);
	F32 pw = Abs(dwb - dwa);

	bool hasBeginEdge		= false;
	bool hasArc0			= false;
	bool hasEdgeBetweenArcs = false;
	bool hasArc1			= false;
	bool hasEndEdge			= false;

	// The shape for an exterior corner (180 to 360 degrees) consists of at most two edges and two arcs.
	// There are two cases to consider when constructing this shape: if he center point is in front of the
	// circle touching the edge or behind.
	if ((dha <= dhb && dwa <= dwb) || (dhb <= dha && dwb <= dwa))
	{
		// In this case the center of the stadium can advance past the corner before the endpoint sphere does.

		// This detects if it's the endpoint sphere a or b that is behind the center point.
		if (dhb <= dha && dwb <= dwa)
		{
			segNormal = -segNormal;
			segDir = -segDir;
		}

		cornerOut.m_begin = e0.b;
		cornerOut.m_arc0.m_begin = cornerOut.m_begin + edgeDir * (pw / 2.0f);

		if (AddStadiumCornerEdge(clipNormal, clipDist, cornerOut.m_begin,
								 cornerOut.m_arc0.m_begin, hasBeginEdge))
		{
			cornerOut.m_arc0.m_origin = cornerOut.m_arc0.m_begin - edgeNormal * m_depenRadius;
			cornerOut.m_arc0.m_end = cornerOut.m_arc0.m_origin - segNormal * m_depenRadius;

			if (AddStadiumCornerArc(clipNormal, clipDist, m_depenRadius, cornerOut.m_arc0.m_origin,
									cornerOut.m_arc0.m_begin, cornerOut.m_arc0.m_end, hasArc0))
			{
				cornerOut.m_arc1.m_begin = cornerOut.m_arc0.m_end - segDir * h;

				if (AddStadiumCornerEdge(clipNormal, clipDist, cornerOut.m_arc0.m_end,
										 cornerOut.m_arc1.m_begin, hasEdgeBetweenArcs))
				{
					cornerOut.m_arc1.m_origin = cornerOut.m_arc1.m_begin + segNormal * m_depenRadius;
					cornerOut.m_arc1.m_end = cornerOut.m_arc1.m_origin - edgeNormal * m_depenRadius;
					cornerOut.m_end = cornerOut.m_arc1.m_end;

					if (!AddStadiumCornerArc(clipNormal, clipDist, m_depenRadius, cornerOut.m_arc1.m_origin,
											 cornerOut.m_arc1.m_begin, cornerOut.m_arc1.m_end,
											 hasArc1))
					{
						// We've clipped arc1
						cornerOut.m_hasEndEdge = AddStadiumCornerConnectionToEdge(cornerOut.m_arc1.m_end,
																				  e1, cornerOut.m_end);
					}
				}
			}
			else
			{
				// We've clipped arc0
				hasEdgeBetweenArcs = AddStadiumCornerConnectionToEdge(cornerOut.m_arc0.m_end, e1,
																	  cornerOut.m_arc1.m_begin);
			}
		}
	}
	else // (dha <= dhb && dwa >= dwb) || (dhb <= dha && dwb >= dwa)
	{
		// In this case the endpoint sphere can advance past the the corner before the center of the stadium

		// This detects if it's endpoint sphere a or b that is in front of the center point.
		if (dhb <= dha && dwb >= dwa)
		{
			segNormal = -segNormal;
			segDir = -segDir;
		}

		cornerOut.m_begin = e0.b - edgeDir * (pw / 2.0f);
		cornerOut.m_arc0.m_begin = cornerOut.m_begin;
		cornerOut.m_arc0.m_origin = cornerOut.m_arc0.m_begin - edgeNormal * m_depenRadius;
		cornerOut.m_arc0.m_end = cornerOut.m_arc0.m_origin - segNormal * m_depenRadius;

		if (AddStadiumCornerArc(clipNormal, clipDist, m_depenRadius, cornerOut.m_arc0.m_origin,
								cornerOut.m_arc0.m_begin, cornerOut.m_arc0.m_end,
								hasArc0))
		{
			cornerOut.m_arc1.m_begin = cornerOut.m_arc0.m_end - segDir * h;

			if (AddStadiumCornerEdge(clipNormal, clipDist, cornerOut.m_arc0.m_end,
									 cornerOut.m_arc1.m_begin, hasEdgeBetweenArcs))
			{
				cornerOut.m_arc1.m_origin = cornerOut.m_arc1.m_begin + segNormal * m_depenRadius;
				cornerOut.m_arc1.m_end = cornerOut.m_arc1.m_origin - edgeNormal * m_depenRadius;

				if (AddStadiumCornerArc(clipNormal, clipDist, m_depenRadius, cornerOut.m_arc1.m_origin,
										cornerOut.m_arc1.m_begin, cornerOut.m_arc1.m_end,
										hasArc1))
				{
					cornerOut.m_end = cornerOut.m_arc1.m_end - edgeDir * (pw / 2.0f);

					AddStadiumCornerEdge(clipNormal, clipDist, cornerOut.m_arc1.m_end,
										 cornerOut.m_end, hasEndEdge);
				}
				else
				{
					// We've clipped arc1
					hasEndEdge = AddStadiumCornerConnectionToEdge(cornerOut.m_arc1.m_end,
																  e1, cornerOut.m_end);
				}
			}
		}
		else
		{
			// We've clipped arc0
			hasEdgeBetweenArcs = AddStadiumCornerConnectionToEdge(cornerOut.m_arc0.m_end, e1,
																  cornerOut.m_arc1.m_begin);
		}
	}

	cornerOut.m_hasBeginEdge		= hasBeginEdge;
	cornerOut.m_hasArc0				= hasArc0;
	cornerOut.m_hasEdgeBetweenArcs	= hasEdgeBetweenArcs;
	cornerOut.m_hasArc1				= hasArc1;
	cornerOut.m_hasEndEdge			= hasEndEdge;

	if (!cornerOut.m_edge0Fake)
	{
		m_pEdges[cornerOut.m_iEdge0].m_iCorner0 = cornerIdx;
	}

	if (!cornerOut.m_edge1Fake)
	{
		m_pEdges[cornerOut.m_iEdge1].m_iCorner1 = cornerIdx;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshStadiumDepenetrator::AddStadiumCornerEdge(Vector_arg clipNormal, F32 clipDist, Point& begin, Point& end, bool& hasEdge) const
{
	hasEdge = true;

	// Clip against the plane defined by the other edge
	{
		if (DotXz(clipNormal, end) > clipDist)
		{
			// The end point is behind the plane
			hasEdge = false;
			return false;
		}

		Vector r = VectorXz(end - begin);
		F32 db = DotXz(clipNormal, begin);
		F32 t = (clipDist - db) / DotXz(clipNormal, r);

		// We've intersected the plane
		if (t > 0.0f && t < 1.0f)
		{
			end = begin + r * t;
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshStadiumDepenetrator::AddStadiumCornerArc(Vector_arg clipNormal, F32 clipDist, F32 radius, Point_arg origin,
													 Point& begin, Point& end, bool& hasArc) const
{
	hasArc = true;

	if (DotXz(clipNormal, origin) >= clipDist)
	{
		hasArc = false;
		return false;
	}

	// Compute the point on the clip plane where we know that the arc has to stop.
	Point test = origin + radius * clipNormal;

	// We assume that the arc will never be more then 180 degrees. We form the plane at the base of the sector defined
	// by the begin and end arc directions (with the normal of the plane pointing into the sector) and check if the
	// test point is in the sector (i.e. in front of or on the plane).
	Vector sectorNormal = RotateY90(NormalizeXz(VectorXz(end - begin)));
	F32 sectorDist = DotXz(sectorNormal, begin);

	if (DotXz(sectorNormal, test) >= sectorDist)
	{
		end = test;
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshStadiumDepenetrator::AddStadiumCornerConnectionToEdge(Point_arg connectFrom, const Segment& connectTo, Point& end) const
{
	Vector n = -NormalizeXz(VectorXz(connectTo.GetVec()));
	if (DotXz(n, connectFrom) > DotXz(n, connectTo.a))
	{
		end = connectTo.a;
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 NavMeshStadiumDepenetrator::DistPointStadiumCornerArcXz(Point_arg point, const ExtCornerArc& arc, Point& closestPoint) const
{
	F32 r = LengthXz(arc.m_begin - arc.m_origin);

	// Compute the planes that lie along the begin and end arc vectors (with there normals pointing into the arc).
	Vector nb = -RotateY90(VectorXz(arc.m_begin - arc.m_origin) / r);
	Vector ne = RotateY90(VectorXz(arc.m_end - arc.m_origin) / r);

	F32 db = DotXz(nb, arc.m_origin);
	F32 de = DotXz(ne, arc.m_origin);

	// If we're below or on either of the planes then one of the end points will be the closest point (or if the point
	// is the arc origin then there are infinitely many closest points so the end points are still valid).
	if (DotXz(nb, point) <= db || DotXz(ne, point) <= de)
	{
		F32 b = LengthXz(point - arc.m_begin);
		F32 e = LengthXz(point - arc.m_end);

		if (b < e)
		{
			closestPoint = arc.m_begin;
			return b;
		}
		else
		{
			closestPoint = arc.m_end;
			return e;
		}
	}

	// We're inside the planes so the closest point is the intersection between the arc and the line from the origin
	// to the point.
	closestPoint = arc.m_origin + r * NormalizeXz(VectorXz(point - arc.m_origin));
	return LengthXz(closestPoint - point);
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 NavMeshStadiumDepenetrator::DistPointStadiumCornerXz(Point_arg point, const ExtCorner& corner, Point& closestPoint) const
{
	F32 closestDist = kLargeFloat;

	if (corner.m_hasBeginEdge)
	{
		Point c;
		F32 d = DistPointSegmentXz(point, Segment(corner.m_begin, corner.m_arc0.m_begin), &c);
		if (d < closestDist)
		{
			closestPoint = c;
			closestDist = d;
		}
	}

	if (corner.m_hasEdgeBetweenArcs)
	{
		Point c;
		F32 d = DistPointSegmentXz(point, Segment(corner.m_arc0.m_end, corner.m_arc1.m_begin), &c);
		if (d < closestDist)
		{
			closestPoint = c;
			closestDist = d;
		}
	}

	if (corner.m_hasEndEdge)
	{
		Point c;
		F32 d = DistPointSegmentXz(point, Segment(corner.m_arc1.m_end, corner.m_end), &c);
		if (d < closestDist)
		{
			closestPoint = c;
			closestDist = d;
		}
	}

	if (corner.m_hasArc0)
	{
		Point c;
		F32 d = DistPointStadiumCornerArcXz(point, corner.m_arc0, c);
		if (d < closestDist)
		{
			closestPoint = c;
			closestDist = d;
		}
	}

	if (corner.m_hasArc1)
	{
		Point c;
		F32 d = DistPointStadiumCornerArcXz(point, corner.m_arc1, c);
		if (d < closestDist)
		{
			closestPoint = c;
			closestDist = d;
		}
	}

	return closestDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 NavMeshStadiumDepenetrator::GenerateCorners(ExtCorner* pCornersOut, U32 maxCornersOut)
{
	if (!pCornersOut || 0 == maxCornersOut)
	{
		return 0;
	}

	const size_t numBlocks = ExternalBitArrayStorage::DetermineNumBlocks(m_numEdges);
	U64* aBlocks0 = NDI_NEW U64[numBlocks];
	U64* aBlocks1 = NDI_NEW U64[numBlocks];

	NAV_ASSERT(aBlocks0);
	NAV_ASSERT(aBlocks1);

	ExternalBitArray attached0(m_numEdges, aBlocks0, false);
	ExternalBitArray attached1(m_numEdges, aBlocks1, false);

	U32F numCorners = 0;

	for (U32F iEdge = 0; iEdge < m_numEdges; ++iEdge)
	{
		const Edge& e0 = m_pEdges[iEdge];

		for (U32F iOtherEdge = iEdge + 1; iOtherEdge < m_numEdges; ++iOtherEdge)
		{
			const Edge& e1 = m_pEdges[iOtherEdge];

			if (e0.m_pMesh != e1.m_pMesh && !(e0.m_fromLink && e1.m_fromLink))
			{
				continue;
			}

			Point vert = kOrigin;
			Vector testEdgeVec = kZero;

			bool connected01 = Dist(e0.m_v0, e1.m_v1) < 0.001f;
			if (connected01)
			{
				vert = e0.m_v0;
				testEdgeVec = e0.m_v1 - e0.m_v0;

				attached0.SetBit(iEdge);
				attached1.SetBit(iOtherEdge);
			}
			else if (Dist(e0.m_v1, e1.m_v0) < 0.001f)
			{
				attached1.SetBit(iEdge);
				attached0.SetBit(iOtherEdge);

				vert = e0.m_v1;
				testEdgeVec = e0.m_v0 - e0.m_v1;
			}
			else
			{
				continue;
			}

			const Vector normal0 = e0.m_normal;
			const Vector normal1 = e1.m_normal;

			const float dotP = DotXz(testEdgeVec, normal1);

			if (dotP >= -0.001f)
			{
				if (dotP <= 0.001f)
				{
					if (connected01)
					{
						m_pEdges[iEdge].m_neighborParallel0 = iOtherEdge;
						m_pEdges[iOtherEdge].m_neighborParallel1 = iEdge;
					}
					else
					{
						m_pEdges[iEdge].m_neighborParallel1 = iOtherEdge;
						m_pEdges[iOtherEdge].m_neighborParallel0 = iEdge;
					}
				}
				else
				{
					if (connected01)
					{
						m_pEdges[iEdge].m_neighborInterior0 = iOtherEdge;
						m_pEdges[iOtherEdge].m_neighborInterior1 = iEdge;
					}
					else
					{
						m_pEdges[iEdge].m_neighborInterior1 = iOtherEdge;
						m_pEdges[iOtherEdge].m_neighborInterior0 = iEdge;
					}
				}

				continue;
			}

			if (numCorners < maxCornersOut)
			{
				CreateCorner(numCorners, pCornersOut[numCorners], vert, normal0, normal1, iEdge, iOtherEdge);
				++numCorners;
			}
		}
	}

	if (numCorners >= maxCornersOut)
	{
		return numCorners;
	}

	for (U32F iEdge = 0; iEdge < m_numEdges; ++iEdge)
	{
		const Edge& edge = m_pEdges[iEdge];

		if (!attached0.IsBitSet(iEdge))
		{
			const Vector normal0 = edge.m_normal;
			const Vector normal1 = RotateY90(edge.m_normal);

			CreateCorner(numCorners, pCornersOut[numCorners], edge.m_v0, normal0, normal1, iEdge, -1);
			++numCorners;

			if (numCorners >= maxCornersOut)
				break;
		}

		if (!attached1.IsBitSet(iEdge))
		{
			const Vector normal0 = RotateYMinus90(edge.m_normal);
			const Vector normal1 = edge.m_normal;

			CreateCorner(numCorners, pCornersOut[numCorners], edge.m_v1, normal0, normal1, -1, iEdge);
			++numCorners;

			if (numCorners >= maxCornersOut)
				break;
		}
	}

	return numCorners;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::AddItersection(const Intersection& i,
												Intersection* pIntersections,
												U32 maxNumIntersections,
												U32& numIntersections) const
{
	if (numIntersections < maxNumIntersections)
	{
		pIntersections[numIntersections] = i;
		numIntersections++;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 NavMeshStadiumDepenetrator::GenerateIntersections(Intersection* pIntersectionsOut,
													  U32 maxIntersections,
													  const ExtCorner* pCorners,
													  U32 numCorners) const
{
	if (m_numEdges == 0)
	{
		return 0;
	}

	U32 numIntersections = 0;
	const F32 searchRadius = (m_maxDepenRadius + m_radius) * 2.15f;

	for (I32 iEdge0 = 0; iEdge0 < (m_numEdges - 1); ++iEdge0)
	{
		if (numIntersections >= maxIntersections)
		{
			break;
		}

		const FeatureIndex idx0 = FeatureIndex::FromEdge(iEdge0);
		const Edge& edge0 = m_pEdges[iEdge0];

		if (edge0.m_zeroLength)
		{
			continue;
		}

		for (I32 iEdge1 = iEdge0 + 1; iEdge1 < m_numEdges; ++iEdge1)
		{
			if (numIntersections >= maxIntersections)
			{
				break;
			}

			const FeatureIndex idx1 = FeatureIndex::FromEdge(iEdge1);
			const Edge& edge1 = m_pEdges[iEdge1];

			if (edge1.m_zeroLength)
			{
				continue;
			}

			GenerateEdgeEdgeIntersections(searchRadius, pCorners, numCorners, pIntersectionsOut,
										  maxIntersections, numIntersections, idx0, edge0, idx1,
										  edge1);
		}
	}

	for (U32 iCorner = 0; iCorner < numCorners; ++iCorner)
	{
		if (numIntersections >= maxIntersections)
		{
			break;
		}

		const ExtCorner& corner = pCorners[iCorner];
		const FeatureIndex cornerIdx0 = FeatureIndex::FromCorner(iCorner);
		const Aabb cornerBound = GetCornerBound(corner);

		for (U32 iOtherCorner = iCorner + 1; iOtherCorner < numCorners; ++iOtherCorner)
		{
			if (numIntersections >= maxIntersections)
			{
				break;
			}

			const ExtCorner& otherCorner = pCorners[iOtherCorner];
			const FeatureIndex cornerIdx1 = FeatureIndex::FromCorner(iOtherCorner);
			const Aabb otherBound = GetCornerBound(otherCorner);

			if (cornerBound.Overlaps(otherBound))
			{
				GenerateCornerCornerIntersections(searchRadius, pCorners, numCorners, pIntersectionsOut,
												  maxIntersections, numIntersections, cornerIdx0, corner,
												  cornerIdx1, otherCorner);
			}
		}
	}

	for (U32 iCorner = 0; iCorner < numCorners; ++iCorner)
	{
		if (numIntersections >= maxIntersections)
		{
			break;
		}

		const ExtCorner& corner = pCorners[iCorner];
		const FeatureIndex cornerIdx = FeatureIndex::FromCorner(iCorner);
		const Aabb cornerBound = GetCornerBound(corner);

		for (U32 iEdge = 0; iEdge < m_numEdges; ++iEdge)
		{
			if (numIntersections >= maxIntersections)
			{
				break;
			}

			if (corner.m_iEdge0 == iEdge || corner.m_iEdge1 == iEdge)
			{
				continue;
			}

			const Edge& edge = m_pEdges[iEdge];
			const FeatureIndex edgeIdx = FeatureIndex::FromEdge(iEdge);

			if (edge.m_zeroLength)
			{
				continue;
			}

			Scalar t0, t1;
			if (cornerBound.IntersectSegment(edge.m_offset0, edge.m_offset1, t0, t1))
			{
				GenerateCornerEdgeIntersections(searchRadius, pCorners, numCorners, pIntersectionsOut,
												maxIntersections, numIntersections, cornerIdx, corner,
												edgeIdx, edge);
			}
		}
	}

	NAV_ASSERT(numIntersections <= maxIntersections);

	return numIntersections;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Aabb NavMeshStadiumDepenetrator::GetCornerBound(const ExtCorner& corner) const
{
	Aabb bound;

	if (corner.m_hasBeginEdge)
	{
		bound.IncludePoint(corner.m_begin);
		bound.IncludePoint(corner.m_arc0.m_begin);
	}
	if (corner.m_hasArc0)
	{
		bound.IncludePoint(corner.m_arc0.m_begin);
		bound.IncludePoint(corner.m_arc0.m_end);
	}
	if (corner.m_hasEdgeBetweenArcs)
	{
		bound.IncludePoint(corner.m_arc0.m_begin);
		bound.IncludePoint(corner.m_arc1.m_begin);
	}
	if (corner.m_hasArc1)
	{
		bound.IncludePoint(corner.m_arc1.m_begin);
		bound.IncludePoint(corner.m_arc1.m_end);
	}
	if (corner.m_hasEndEdge)
	{
		bound.IncludePoint(corner.m_end);
		bound.IncludePoint(corner.m_arc1.m_end);
	}

	bound.m_min.SetY(-kLargeFloat);
	bound.m_max.SetY(kLargeFloat);

	return bound;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshStadiumDepenetrator::IntersectEdgeCornerResults NavMeshStadiumDepenetrator::IntersectEdgeCorner(const FeatureIndex cornerIdx,
																									   const ExtCorner& corner,
																									   const Segment& edge) const
{
	IntersectEdgeCornerResults results;

	auto addIntersection = [&results](F32 t0, F32 t1, FeatureIndex fi)
	{
		if (t1 < results.m_t1[0])
		{
			results.m_t0[0] = t0;
			results.m_t1[0] = t1;
			results.m_fi[0] = fi;
		}

		if (t1 > results.m_t1[1])
		{
			results.m_t0[1] = t0;
			results.m_t1[1] = t1;
			results.m_fi[1] = fi;
		}
	};

	{
		Scalar t0, t1;
		if (corner.m_hasBeginEdge && IntersectSegmentSegmentXz(Segment(corner.m_begin, corner.m_arc0.m_begin), edge, t0, t1))
		{
			addIntersection(t0, t1, cornerIdx.As(kCornerBeginEdgeFeature));
		}

		if (corner.m_hasEdgeBetweenArcs && IntersectSegmentSegmentXz(Segment(corner.m_arc0.m_end, corner.m_arc1.m_begin), edge, t0, t1))
		{
			addIntersection(t0, t1, cornerIdx.As(kCornerBetweenEdgeFeature));
		}

		if (corner.m_hasEndEdge && IntersectSegmentSegmentXz(Segment(corner.m_arc1.m_end, corner.m_end), edge, t0, t1))
		{
			addIntersection(t0, t1, cornerIdx.As(kCornerEndEdgeFeature));
		}
	}

	{
		F32 t00, t01, t10, t11;
		if (corner.m_hasArc0 && IntersectArcEdge(corner.m_arc0, edge, t00, t01, t10, t11))
		{
			addIntersection(t00, t01, cornerIdx.As(kCornerArc0Feature));
			addIntersection(t10, t11, cornerIdx.As(kCornerArc0Feature));
		}

		if (corner.m_hasArc1 && IntersectArcEdge(corner.m_arc1, edge, t00, t01, t10, t11))
		{
			addIntersection(t00, t01, cornerIdx.As(kCornerArc1Feature));
			addIntersection(t10, t11, cornerIdx.As(kCornerArc1Feature));
		}
	}

	bool valid0 = results.m_t1[0] < kLargeFloat;
	bool valid1 = results.m_t1[1] > -kLargeFloat && results.m_t1[0] != results.m_t1[1];

	if (!valid0 && valid1)
	{
		Swap(results.m_fi[0], results.m_fi[1]);
		Swap(results.m_t0[0], results.m_t0[1]);
		Swap(results.m_t1[0], results.m_t1[1]);
	}

	results.m_num = valid0 + valid1;

	return results;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshStadiumDepenetrator::IntersectArcCornerResults NavMeshStadiumDepenetrator::IntersectArcCorner(const FeatureIndex cornerIdx,
																									 const ExtCorner& corner,
																									 const ExtCornerArc& arc) const
{
	IntersectArcCornerResults results;

	auto addIntersection = [&results](Point_arg intersection, F32 t0, F32 t1, FeatureIndex fi)
	{
		if (results.m_num == IntersectArcCornerResults::kMaxIntersections)
		{
			return;
		}

		U32 index = results.m_num;
		results.m_num++;

		results.m_intersection[index] = intersection;
		results.m_t0[index] = t0;
		results.m_t1[index] = t1;
		results.m_fi[index] = fi;
	};

	{
		F32 t00, t01, t10, t11;
		if (corner.m_hasBeginEdge)
		{
			Segment e(corner.m_begin, corner.m_arc0.m_begin);
			if (IntersectArcEdge(arc, e, t00, t01, t10, t11))
			{
				addIntersection(Lerp(e.a, e.b, t01), t01, t00, cornerIdx.As(kCornerBeginEdgeFeature));
				if (t01 != t11)
				{
					addIntersection(Lerp(e.a, e.b, t11), t11, t10, cornerIdx.As(kCornerBeginEdgeFeature));
				}
			}
		}

		if (corner.m_hasEdgeBetweenArcs)
		{
			Segment e(corner.m_arc0.m_end, corner.m_arc1.m_begin);
			if (IntersectArcEdge(arc, e, t00, t01, t10, t11))
			{
				addIntersection(Lerp(e.a, e.b, t01), t01, t00, cornerIdx.As(kCornerBetweenEdgeFeature));
				if (t01 != t11)
				{
					addIntersection(Lerp(e.a, e.b, t11), t11, t10, cornerIdx.As(kCornerBetweenEdgeFeature));
				}
			}
		}

		if (corner.m_hasEndEdge)
		{
			Segment e(corner.m_arc1.m_end, corner.m_end);
			if (IntersectArcEdge(arc, e, t00, t01, t10, t11))
			{
				addIntersection(Lerp(e.a, e.b, t01), t01, t00, cornerIdx.As(kCornerEndEdgeFeature));
				if (t01 != t11)
				{
					addIntersection(Lerp(e.a, e.b, t11), t11, t10, cornerIdx.As(kCornerEndEdgeFeature));
				}
			}
		}
	}

	{
		Point i0, i1;
		F32 t00, t01, t10, t11;
		if (corner.m_hasArc0 && IntersectArcArc(corner.m_arc0, arc, t00, t01, t10, t11, i0, i1))
		{
			addIntersection(i0, t00, t01, cornerIdx.As(kCornerArc0Feature));
			if (t00 != t10)
			{
				addIntersection(i1, t10, t11, cornerIdx.As(kCornerArc0Feature));
			}
		}

		if (corner.m_hasArc1 && IntersectArcArc(corner.m_arc1, arc, t00, t01, t10, t11, i0, i1))
		{
			addIntersection(i0, t00, t01, cornerIdx.As(kCornerArc1Feature));
			if (t00 != t10)
			{
				addIntersection(i1, t10, t11, cornerIdx.As(kCornerArc1Feature));
			}
		}
	}

	return results;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshStadiumDepenetrator::IntersectCornerCornerResults NavMeshStadiumDepenetrator::IntersectCornerCorner(const FeatureIndex cornerIdx0,
																										   const ExtCorner& corner0,
																										   const FeatureIndex cornerIdx1,
																										   const ExtCorner& corner1) const
{
	IntersectCornerCornerResults results;

	auto addIntersection = [&results](Point_arg intersection, F32 t0, F32 t1, FeatureIndex fi0, FeatureIndex fi1)
	{
		if (results.m_num == IntersectCornerCornerResults::kMaxIntersections)
		{
			return;
		}

		U32 index = results.m_num;
		results.m_num++;

		results.m_intersection[index] = intersection;
		results.m_t0[index] = t0;
		results.m_t1[index] = t1;
		results.m_fi0[index] = fi0;
		results.m_fi1[index] = fi1;
	};

	if (corner0.m_hasBeginEdge)
	{
		Segment e(corner0.m_begin, corner0.m_arc0.m_begin);
		IntersectEdgeCornerResults r = IntersectEdgeCorner(cornerIdx1, corner1, e);
		for (U32 i = 0; i < r.m_num; i++)
		{
			addIntersection(Lerp(e.a, e.b, r.m_t1[i]), r.m_t0[i], r.m_t1[i],
							r.m_fi[i], cornerIdx0.As(kCornerBeginEdgeFeature));
		}
	}

	if (corner0.m_hasEdgeBetweenArcs)
	{
		Segment e(corner0.m_arc0.m_end, corner0.m_arc1.m_begin);
		IntersectEdgeCornerResults r = IntersectEdgeCorner(cornerIdx1, corner1, e);
		for (U32 i = 0; i < r.m_num; i++)
		{
			addIntersection(Lerp(e.a, e.b, r.m_t1[i]), r.m_t0[i], r.m_t1[i],
							r.m_fi[i], cornerIdx0.As(kCornerBetweenEdgeFeature));
		}
	}

	if (corner0.m_hasEndEdge)
	{
		Segment e(corner0.m_arc1.m_end, corner0.m_end);
		IntersectEdgeCornerResults r = IntersectEdgeCorner(cornerIdx1, corner1, e);
		for (U32 i = 0; i < r.m_num; i++)
		{
			addIntersection(Lerp(e.a, e.b, r.m_t1[i]), r.m_t0[i], r.m_t1[i],
							r.m_fi[i], cornerIdx0.As(kCornerEndEdgeFeature));
		}
	}

	if (corner0.m_hasArc0)
	{
		IntersectArcCornerResults r = IntersectArcCorner(cornerIdx1, corner1, corner0.m_arc0);
		for (U32 i = 0; i < r.m_num; i++)
		{
			addIntersection(r.m_intersection[i], r.m_t0[i], r.m_t1[i],
							r.m_fi[i], cornerIdx0.As(kCornerArc0Feature));
		}
	}

	if (corner0.m_hasArc1)
	{
		IntersectArcCornerResults r = IntersectArcCorner(cornerIdx1, corner1, corner0.m_arc1);
		for (U32 i = 0; i < r.m_num; i++)
		{
			addIntersection(r.m_intersection[i], r.m_t0[i], r.m_t1[i],
							r.m_fi[i], cornerIdx0.As(kCornerArc1Feature));
		}
	}

	return results;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<class T>
static F32 Cross2dXz(const T& v, const T& w)
{
	return v.X() * w.Z() - v.Z() * w.X();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ItersectSegmentSegmentAsPlanesXz(const Segment& s0, const Segment& s1, F32& t0, F32& t1)
{
	Vector r = s0.GetVec();
	Vector s = s1.GetVec();

	F32 rs = Cross2dXz(r, s);
	if (Abs(rs) < 0.00001f)
	{
		return false;
	}

	Vector d = s1.a - s0.a;

	t0 = Cross2dXz(d, s) / rs;
	t1 = Cross2dXz(d, r) / rs;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshStadiumDepenetrator::IntersectEdgeAsPlaneCorner(const ExtCorner& corner, const Segment& edge,
															F32& t0, F32& t1) const
{
	bool hasIntersection = false;

	t0 = kLargeFloat;
	t1 = -kLargeFloat;

	auto addIntersection = [&t0, &t1](F32 tc, F32 te)
	{
		if (tc >= 0.0f && tc <= 1.0f)
		{
			t0 = Min(t0, te);
			t1 = Max(t1, te);
		}
	};

	auto inFrontOfCornerEdge = [&edge](Vector_arg normal, F32 d)
	{
		return DotXz(normal, edge.a) > d && DotXz(normal, edge.b) > d;
	};

	auto getEdgeNormal = [&corner](const Segment& s)
	{
		Vector normal = NormalizeXz(RotateY90(VectorXz(s.GetVec())));
		if (DotXz(normal, corner.m_vert) > DotXz(normal, s.a))
		{
			normal = -normal;
		}

		return normal;
	};

	{
		F32 tc = 0.0f;
		F32 te = 0.0f;

		if (corner.m_hasBeginEdge)
		{
			Segment seg(corner.m_begin, corner.m_arc0.m_begin);
			if (ItersectSegmentSegmentAsPlanesXz(seg, edge, tc, te))
			{
				Vector n = getEdgeNormal(seg);
				if (inFrontOfCornerEdge(n, DotXz(n, seg.a)))
				{
					addIntersection(tc, te);
				}

				hasIntersection = true;
			}
		}

		if (corner.m_hasEdgeBetweenArcs)
		{
			Segment seg(corner.m_arc0.m_end, corner.m_arc1.m_begin);
			if (ItersectSegmentSegmentAsPlanesXz(seg, edge, tc, te))
			{
				Vector n = getEdgeNormal(seg);
				if (inFrontOfCornerEdge(n, DotXz(n, seg.a)))
				{
					addIntersection(tc, te);
				}

				hasIntersection = true;
			}
		}

		if (corner.m_hasEndEdge)
		{
			Segment seg(corner.m_arc1.m_end, corner.m_end);
			if (ItersectSegmentSegmentAsPlanesXz(seg, edge, tc, te))
			{
				Vector n = getEdgeNormal(seg);
				if (inFrontOfCornerEdge(n, DotXz(n, seg.a)))
				{
					addIntersection(tc, te);
				}

				hasIntersection = true;
			}
		}
	}

	return hasIntersection;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::StretchInteriorCornerEdges(I32 iEdge0, I32 iEdge1)
{
	Edge& edge0 = m_pEdges[iEdge0];
	Edge& edge1 = m_pEdges[iEdge1];

	if (edge0.m_neighborInterior0 != iEdge1 && edge0.m_neighborInterior1 != iEdge1)
	{
		return;
	}

	const Segment seg0 = Segment(edge0.m_offset0, edge0.m_offset1);
	const Segment seg1 = Segment(edge1.m_offset0, edge1.m_offset1);

	F32 t0, t1;
	if (!ItersectSegmentSegmentAsPlanesXz(seg0, seg1, t0, t1))
	{
		return;
	}

	Vector seg0Dir = NormalizeXz(seg0.GetVec());
	Vector seg1Dir = NormalizeXz(seg1.GetVec());

	if (t0 < 0.0f)
	{
		edge0.m_offset0 = Lerp(seg0.a, seg0.b, t0) - NormalizeXz(seg0.GetVec()) * kNudgeEpsilon;
	}
	else if (t0 > 1.0f)
	{
		edge0.m_offset1 = Lerp(seg0.a, seg0.b, t0) + NormalizeXz(seg0.GetVec()) * kNudgeEpsilon;
	}

	if (t1 < 0.0f)
	{
		edge1.m_offset0 = Lerp(seg1.a, seg1.b, t1) - NormalizeXz(seg1.GetVec()) * kNudgeEpsilon;
	}
	else if (t1 > 1.0f)
	{
		edge1.m_offset1 = Lerp(seg1.a, seg1.b, t1) + NormalizeXz(seg1.GetVec()) * kNudgeEpsilon;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::StretchInteriorCornersAgainstNeighboringCorners(const ExtCorner* pCorners, I32 iEdge0, I32 iEdge1)
{
	Edge& edge0 = m_pEdges[iEdge0];
	Edge& edge1 = m_pEdges[iEdge1];

	if (edge0.m_neighborInterior0 != iEdge1 && edge0.m_neighborInterior1 != iEdge1)
	{
		return;
	}

	const Segment seg0 = Segment(edge0.m_offset0, edge0.m_offset1);
	const Segment seg1 = Segment(edge1.m_offset0, edge1.m_offset1);

	// It's possible for interior edges to be clipped against a corner, so we also want to intersect the
	// neighboring corners of each edge and extend.
	auto extendEdgeToCorner = [this, iEdge0, iEdge1](const ExtCorner& corner, I32 edgeIdx, Edge& edge)
	{
		const Segment seg = Segment(edge.m_offset0, edge.m_offset1);

		F32 t0, t1;
		bool hasIntersection = IntersectEdgeAsPlaneCorner(corner, seg, t0, t1);
		if (!hasIntersection)
		{
			if (corner.m_iEdge0 == edgeIdx ? !corner.m_edge1Fake : !corner.m_edge0Fake)
			{
				I32 otherIdx = corner.m_iEdge0 == edgeIdx ? corner.m_iEdge1 : corner.m_iEdge0;
				if (otherIdx != iEdge0 && otherIdx != iEdge1)
				{
					const Edge& other = m_pEdges[otherIdx];

					// Also try the edge connected to the other side of the corner.
					if (ItersectSegmentSegmentAsPlanesXz(Segment(other.m_offset0, other.m_offset1), seg, t0, t1))
					{
						if (t0 >= 0.0f && t0 <= 1.0f)
						{
							t0 = t1;
							hasIntersection = true;
						}
					}
				}
			}
		}

		if (hasIntersection && t0 < kLargeFloat && t1 > -kLargeFloat)
		{
			if (t0 < 0.0f)
			{
				F32 t = t1 < 0.0f ? t1 : t0;
				edge.m_offset0 = Lerp(seg.a, seg.b, t) - NormalizeXz(seg.GetVec()) * kNudgeEpsilon;
			}
			else if (t1 > 1.0f)
			{
				F32 t = t0 > 1.0f ? t0 : t1;
				edge.m_offset1 = Lerp(seg.a, seg.b, t) + NormalizeXz(seg.GetVec()) * kNudgeEpsilon;
			}
		}
	};

	I32 corner0 = edge0.m_iCorner0 == -1 ? edge0.m_iCorner1 : edge0.m_iCorner0;
	if (corner0 != -1)
	{
		extendEdgeToCorner(pCorners[corner0], iEdge1, edge1);
	}

	I32 corner1 = edge1.m_iCorner0 == -1 ? edge1.m_iCorner1 : edge1.m_iCorner0;
	if (corner1 != -1)
	{
		extendEdgeToCorner(pCorners[corner1], iEdge0, edge0);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::GenerateEdgeEdgeIntersections(F32 searchRadius,
															   const ExtCorner* pCorners,
															   U32 numCorners,
															   Intersection* pIntersectionsOut,
															   U32 maxIntersections,
															   U32 &numIntersections,
															   const FeatureIndex edgeIdx0,
															   const Edge& edge0,
															   const FeatureIndex edgeIdx1,
															   const Edge& edge1) const
{
	// Don't find intersections for edges that lie on the same line.
	if (FindMatchingConnectedEdge(FindFirstConnectedEdge(edgeIdx0.m_index),
								  edgeIdx1.m_index, edgeIdx1.m_index) != -1)
	{
		return;
	}

	const Segment seg0 = Segment(edge0.m_offset0, edge0.m_offset1);
	const Segment seg1 = Segment(edge1.m_offset0, edge1.m_offset1);

	F32 t0, t1;
	if (!ItersectSegmentSegmentAsPlanesXz(seg0, seg1, t0, t1))
	{
		return;
	}

	if (t0 < 0.0f || t0 > 1.0f || t1 < 0.0f || t1 > 1.0f)
	{
		return;
	}

	const Point intPos = Lerp(seg0.a, seg0.b, t0);

	if (IsPointClear(intPos, edgeIdx0, edgeIdx1, searchRadius, kZero, pCorners, numCorners))
	{
		Intersection newInt;
		newInt.m_feature0 = edgeIdx0;
		newInt.m_feature1 = edgeIdx1;
		newInt.m_tt0 = t0;
		newInt.m_tt1 = t1;
		newInt.m_intPos = intPos;

		AddItersection(newInt, pIntersectionsOut, maxIntersections, numIntersections);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::GenerateCornerEdgeIntersections(F32 searchRadius,
																 const ExtCorner* pCorners,
																 U32 numCorners,
																 Intersection* pIntersectionsOut,
																 U32 maxIntersections,
																 U32 &numIntersections,
																 const FeatureIndex cornerIdx,
																 const ExtCorner& corner,
																 const FeatureIndex edgeIdx,
																 const Edge& edge) const
{
	Segment e(edge.m_offset0, edge.m_offset1);
	IntersectEdgeCornerResults results = IntersectEdgeCorner(cornerIdx, corner, e);

	for (U32 i = 0; i < results.m_num; i++)
	{
		const Point intPos = Lerp(e.a, e.b, results.m_t1[i]);

		if (IsPointClear(intPos, cornerIdx, edgeIdx, searchRadius, kZero, pCorners, numCorners))
		{
			Intersection newInt;
			newInt.m_feature1 = edgeIdx;
			newInt.m_feature0 = results.m_fi[i];
			newInt.m_tt0 = results.m_t0[i];
			newInt.m_tt1 = results.m_t1[i];
			newInt.m_intPos = intPos;

			AddItersection(newInt, pIntersectionsOut, maxIntersections, numIntersections);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::GenerateCornerCornerIntersections(F32 searchRadius,
																   const ExtCorner* pCorners,
																   U32 numCorners,
																   Intersection* pIntersectionsOut,
																   U32 maxIntersections,
																   U32 &numIntersections,
																   const FeatureIndex cornerIdx0,
																   const ExtCorner& corner0,
																   const FeatureIndex cornerIdx1,
																   const ExtCorner& corner1) const
{
	IntersectCornerCornerResults results = IntersectCornerCorner(cornerIdx0, corner0, cornerIdx1, corner1);

	for (U32 i = 0; i < results.m_num; i++)
	{
		const Point intPos = results.m_intersection[i];

		if (IsPointClear(intPos, cornerIdx0, cornerIdx1, searchRadius, kZero, pCorners, numCorners))
		{
			Intersection newInt;
			newInt.m_feature0 = results.m_fi0[i];
			newInt.m_feature1 = results.m_fi1[i];
			newInt.m_tt0 = results.m_t0[i];
			newInt.m_tt1 = results.m_t1[i];
			newInt.m_intPos = intPos;

			AddItersection(newInt, pIntersectionsOut, maxIntersections, numIntersections);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshStadiumDepenetrator::IntersectArcEdge(const ExtCornerArc& cornerArc, const Segment& edge,
												  F32& t00, F32& t01, F32& t10, F32& t11) const
{
	Vector begin = VectorXz(cornerArc.m_begin - cornerArc.m_origin);
	Vector end = VectorXz(cornerArc.m_end - cornerArc.m_origin);

	F32 t0, t1;
	if (IntersectCircleSegmentXz(cornerArc.m_origin, m_depenRadius, edge, t0, t1))
	{
		Point i0 = Lerp(edge.a, edge.b, t0);
		Point i1 = Lerp(edge.a, edge.b, t1);

		bool inArc0 = IsBetweenArcXz(begin, end, i0 - cornerArc.m_origin);
		bool inArc1 = IsBetweenArcXz(begin, end, i1 - cornerArc.m_origin);

		Vector normal = GetArcNormal(cornerArc);

		if (inArc0)
		{
			Vector v0 = (i0 - cornerArc.m_origin) * m_invDepenRadius;
			t00 = GetRelativeXzAngleDiffRad(normal, v0);
			t01 = t0;
		}

		if (inArc1)
		{
			Vector v1 = (i1 - cornerArc.m_origin) * m_invDepenRadius;
			t10 = GetRelativeXzAngleDiffRad(normal, v1);
			t11 = t1;
		}

		if (inArc0 && !inArc1)
		{
			t10 = t00;
			t11 = t01;
		}
		else if (inArc1 && !inArc0)
		{
			t00 = t10;
			t01 = t11;
		}

		return inArc0 || inArc1;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshStadiumDepenetrator::IntersectArcArc(const ExtCornerArc& arc0, const ExtCornerArc& arc1,
												 F32& t00, F32& t01, F32& t10, F32& t11,
												 Point& i0, Point& i1) const
{
	if (!IntersectCircleCircleXz(arc0.m_origin, arc1.m_origin, m_depenRadius, i0, i1))
	{
		return false;
	}

	Vector begin0 = VectorXz(arc0.m_begin - arc0.m_origin);
	Vector begin1 = VectorXz(arc1.m_begin - arc1.m_origin);

	Vector end0 = VectorXz(arc0.m_end - arc0.m_origin);
	Vector end1 = VectorXz(arc1.m_end - arc1.m_origin);

	bool inArcs0 = IsBetweenArcXz(begin0, end0, VectorXz(i0 - arc0.m_origin)) &&
				   IsBetweenArcXz(begin1, end1, VectorXz(i0 - arc1.m_origin));
	bool inArcs1 = IsBetweenArcXz(begin0, end0, VectorXz(i1 - arc0.m_origin)) &&
				   IsBetweenArcXz(begin1, end1, VectorXz(i1 - arc1.m_origin));

	Vector normal0 = GetArcNormal(arc0);
	Vector normal1 = GetArcNormal(arc1);

	if (inArcs0)
	{
		Vector v0 = (i0 - arc0.m_origin) * m_invDepenRadius;
		Vector v1 = (i0 - arc1.m_origin) * m_invDepenRadius;
		t00 = GetRelativeXzAngleDiffRad(normal0, v0);
		t01 = GetRelativeXzAngleDiffRad(normal1, v1);
	}

	if (inArcs1)
	{
		Vector v0 = (i1 - arc0.m_origin) * m_invDepenRadius;
		Vector v1 = (i1 - arc1.m_origin) * m_invDepenRadius;
		t10 = GetRelativeXzAngleDiffRad(normal0, v0);
		t11 = GetRelativeXzAngleDiffRad(normal1, v1);
	}

	if (inArcs0 && !inArcs1)
	{
		i1 = i0;
		t10 = t00;
		t11 = t01;
	}

	if (inArcs1 && !inArcs0)
	{
		i0 = i1;
		t00 = t10;
		t01 = t11;
	}

	return inArcs0 || inArcs1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavMeshStadiumDepenetrator::ExtCornerArc& NavMeshStadiumDepenetrator::GetArc(FeatureIndex index, const ExtCorner* pCorners) const
{
	NAV_ASSERT(index.IsArc());

	const ExtCorner& corner = pCorners[index.m_index];
	if (index.m_type == kCornerArc0Feature)
	{
		return corner.m_arc0;
	}

	return corner.m_arc1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NavMeshStadiumDepenetrator::GetEdgeNormal(FeatureIndex index, const ExtCorner* pCorners) const
{
	if (index.IsEdge())
	{
		return m_pEdges[index.m_index].m_normal;
	}

	Segment e = GetEdgeSegment(index, pCorners);
	Vector normal = NormalizeXz(RotateY90(VectorXz(e.GetVec())));

	const ExtCorner& corner = pCorners[index.m_index];
	if (DotXz(normal, corner.m_vert) > DotXz(normal, e.a))
	{
		normal = -normal;
	}

	return normal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Segment NavMeshStadiumDepenetrator::GetEdgeSegment(FeatureIndex index, const ExtCorner* pCorners) const
{
	if (index.IsEdge())
	{
		return Segment(m_pEdges[index.m_index].m_offset0, m_pEdges[index.m_index].m_offset1);
	}

	const ExtCorner& corner = pCorners[index.m_index];
	Segment e(corner.m_begin, corner.m_arc0.m_begin);

	if (index.m_type == kCornerBetweenEdgeFeature)
	{
		e = Segment(corner.m_arc0.m_end, corner.m_arc1.m_begin);
	}
	else if (index.m_type == kCornerEndEdgeFeature)
	{
		e = Segment(corner.m_arc1.m_end, corner.m_end);
	}

	return e;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 NavMeshStadiumDepenetrator::GatherIntersectionsForEdge(FeatureIndex edgeFeature,
														   Vector_arg edgeVec,
														   const Intersection* pIntersections,
														   U32 numIntersections,
														   const ExtCorner* pCorners,
														   U32 numCorners,
														   LocalIntersection* pIntersectionsOut) const
{
	if (numIntersections == 0)
	{
		return 0;
	}

	U32F numLocal = 0;

	for (U32F iInt = 0; iInt < numIntersections; ++iInt)
	{
		const Intersection& intersection = pIntersections[iInt];
		if (intersection.m_feature0 == edgeFeature)
		{
			bool forward = false;

			if (intersection.m_feature1.IsArc())
			{
				const ExtCornerArc& arc = GetArc(intersection.m_feature1, pCorners);
				const float dotP = DotXz(edgeVec, intersection.m_intPos - arc.m_origin);
				forward = dotP > 0.0f;
			}
			else
			{
				const Vector otherEdgeNormal = GetEdgeNormal(intersection.m_feature1, pCorners);
				const float dotP = DotXz(edgeVec, otherEdgeNormal);
				forward = dotP > 0.0f;

				if (intersection.m_tt0 < 0.0f || intersection.m_tt0 > 1.0f)
				{
					continue;
				}
			}

			pIntersectionsOut[numLocal].m_tt = intersection.m_tt0;
			pIntersectionsOut[numLocal].m_forward = forward;
			++numLocal;
		}
		else if (intersection.m_feature1 == edgeFeature)
		{
			bool forward = false;

			if (intersection.m_feature0.IsArc())
			{
				const ExtCornerArc& arc = GetArc(intersection.m_feature0, pCorners);
				const float dotP = DotXz(edgeVec, intersection.m_intPos - arc.m_origin);
				forward = dotP > 0.0f;
			}
			else
			{
				const Vector otherEdgeNormal = GetEdgeNormal(intersection.m_feature0, pCorners);
				const float dotP = DotXz(edgeVec, otherEdgeNormal);
				forward = dotP > 0.0f;

				if (intersection.m_tt1 < 0.0f || intersection.m_tt1 > 1.0f)
				{
					continue;
				}
			}

			pIntersectionsOut[numLocal].m_tt = intersection.m_tt1;
			pIntersectionsOut[numLocal].m_forward = forward;
			++numLocal;
		}
	}

	if (numLocal > 1)
	{
		QuickSort(pIntersectionsOut, numLocal, LocalIntersection::Compare);
	}

	return numLocal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 NavMeshStadiumDepenetrator::GatherIntersectionsForArc(FeatureIndex arcFeature,
														  const Intersection* pIntersections,
														  U32 numIntersections,
														  const ExtCorner* pCorners,
														  U32 numCorners,
														  LocalIntersection* pIntersectionsOut) const
{
	if (numIntersections == 0)
	{
		return 0;
	}

	const ExtCornerArc& arc = GetArc(arcFeature, pCorners);
	Vector normal = GetArcNormal(arc);

	U32F numLocal = 0;

	for (U32F iInt = 0; iInt < numIntersections; ++iInt)
	{
		const Intersection& intersection = pIntersections[iInt];
		if (intersection.m_feature0 == arcFeature)
		{
			bool forward = false;

			if (intersection.m_feature1.IsArc())
			{
				const ExtCornerArc& otherArc = GetArc(intersection.m_feature1, pCorners);
				const float cy = CrossY(otherArc.m_origin - arc.m_origin, intersection.m_intPos - arc.m_origin);
				forward = cy > 0.0f;
			}
			else
			{
				const Vector otherEdgeNormal = GetEdgeNormal(intersection.m_feature1, pCorners);
				const Vector tangent = RotateY90(intersection.m_intPos - arc.m_origin);
				const float dotP = DotXz(tangent, otherEdgeNormal);
				forward = dotP > 0.0f;
			}

			pIntersectionsOut[numLocal].m_tt = intersection.m_tt0;
			pIntersectionsOut[numLocal].m_forward = forward;
			++numLocal;
		}
		else if (intersection.m_feature1 == arcFeature)
		{
			bool forward = false;

			if (intersection.m_feature0.IsArc())
			{
				const ExtCornerArc& otherArc = GetArc(intersection.m_feature0, pCorners);
				const float cy = CrossY(otherArc.m_origin - arc.m_origin, intersection.m_intPos - arc.m_origin);
				forward = cy > 0.0f;
			}
			else
			{
				const Vector otherEdgeNormal = GetEdgeNormal(intersection.m_feature0, pCorners);
				const Vector tangent = RotateY90(intersection.m_intPos - arc.m_origin);
				const float dotP = DotXz(tangent, otherEdgeNormal);
				forward = dotP > 0.0f;
			}

			pIntersectionsOut[numLocal].m_tt = intersection.m_tt1;
			pIntersectionsOut[numLocal].m_forward = forward;
			++numLocal;
		}
	}

	if (numLocal > 1)
	{
		QuickSort(pIntersectionsOut, numLocal, LocalIntersection::Compare);
	}

	return numLocal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavPolyListEntry NavMeshStadiumDepenetrator::GetContainingPoly(Point_arg pos) const
{
	for (U32F iPoly = 0; iPoly < m_closedListSize; ++iPoly)
	{
		Point testPosLs = pos;
		if (m_pClosedList[iPoly].m_pMesh != m_pMesh)
		{
			testPosLs = m_pClosedList[iPoly].m_pMesh->WorldToLocal(m_pMesh->LocalToWorld(pos));
		}

		if (m_pClosedList[iPoly].m_pPolyEx)
		{
			if (m_pClosedList[iPoly].m_pPolyEx->PolyContainsPointLs(testPosLs))
			{
				ASSERT(m_pClosedList[iPoly].m_pPoly->PolyContainsPointLs(testPosLs, 0.1f));
				return m_pClosedList[iPoly];
			}
		}
		else if (m_pClosedList[iPoly].m_pPoly)
		{
			if (m_pClosedList[iPoly].m_pPoly->PolyContainsPointLs(testPosLs))
			{
				return m_pClosedList[iPoly];
			}
		}
		else
		{
			ASSERT(false); // null entry in our closed list. how?
		}
	}

	NavPolyListEntry empty;
	empty.m_pMesh = nullptr;
	empty.m_pPoly = nullptr;
	empty.m_pPolyEx = nullptr;
	return empty;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshStadiumDepenetrator::IsPointClear(Point_arg pos,
											  FeatureIndex ignore0,
											  FeatureIndex ignore1,
											  float maxDist,
											  Vector_arg testNorm,
											  const ExtCorner* pCorners,
											  U32F numCorners) const
{
	bool clear = true;

	Vector move = pos - m_posLs;
	Segment stadium(m_stadiumSeg.a + move, m_stadiumSeg.b + move);

	for (U32 iEdge = 0; iEdge < m_numEdges; ++iEdge)
	{
		if (FeatureHasEdge(ignore0, iEdge, pCorners, numCorners) || FeatureHasEdge(ignore1, iEdge, pCorners, numCorners))
			continue;

		const Edge& edge = m_pEdges[iEdge];
		if (edge.m_distToPos > maxDist)
			continue;

		const float normDot = DotXz(edge.m_normal, testNorm);
		if (normDot > 0.999f)
			continue;

		Scalar t0, t1;
		if (DistSegmentSegmentXz(stadium, Segment(edge.m_v0, edge.m_v1), t0, t1) < (m_depenRadius - kNudgeEpsilon))
		{
			clear = false;
			break;
		}
	}

	return clear;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::DebugDrawEdge(const Edge& edge, float vo, Color c, const char* label /* = nullptr */) const
{
	STRIP_IN_FINAL_BUILD;

	const Vector evo = Vector(0.0f, vo, 0.0f);

	const Point v0Ws = m_pMesh->LocalToWorld(edge.m_v0) + evo;
	const Point v1Ws = m_pMesh->LocalToWorld(edge.m_v1) + evo;

	const Point offset0Ws = m_pMesh->LocalToWorld(edge.m_offset0) + evo;
	const Point offset1Ws = m_pMesh->LocalToWorld(edge.m_offset1) + evo;

	Color ct = c;
	ct.SetA(0.3f);

	const Point midWs = AveragePos(v0Ws, v1Ws);

	if (!edge.m_zeroLength)
	{
		g_prim.Draw(DebugArrow(offset0Ws, offset1Ws, c, 0.15f, kPrimEnableHiddenLineAlpha));
	}

	g_prim.Draw(DebugLine(v0Ws, v1Ws, ct, 4.0f));
	g_prim.Draw(DebugArrow(midWs, m_pMesh->LocalToWorld(edge.m_normal) * 0.125f, ct, 0.15f, kPrimEnableHiddenLineAlpha));
	g_prim.Draw(DebugString(Lerp(v0Ws, v1Ws, 0.1f), "v0", c, 0.65f));
	g_prim.Draw(DebugString(Lerp(v0Ws, v1Ws, 0.9f), "v1", c, 0.65f));

	if (label && (strlength(label) > 0))
	{
		g_prim.Draw(DebugString(midWs, label, c, 0.65f));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::DebugDrawCorner(const ExtCorner& corner,
												 float vo,
												 Color c,
												 const char* label /* = nullptr */) const
{
	STRIP_IN_FINAL_BUILD;

	const Vector evo = Vector(0.0f, vo, 0.0f);
	const Point cornerWs = m_pMesh->LocalToWorld(corner.m_vert) + evo;

	if (corner.m_hasArc0)
	{
		Point origin = m_pMesh->LocalToWorld(corner.m_arc0.m_origin) + evo;

		g_prim.Draw(DebugCross(origin, 0.1f, c, kPrimEnableWireframe));

		g_prim.Draw(DebugLine(origin,
							  m_pMesh->LocalToWorld(corner.m_arc0.m_begin) + evo,
							  c, kPrimEnableHiddenLineAlpha));

		g_prim.Draw(DebugLine(origin,
							  m_pMesh->LocalToWorld(corner.m_arc0.m_end) + evo,
							  c, kPrimEnableHiddenLineAlpha));

		g_prim.Draw(DebugArc(origin,
							 m_pMesh->LocalToWorld(corner.m_arc0.m_begin - corner.m_arc0.m_origin),
							 m_pMesh->LocalToWorld(corner.m_arc0.m_end - corner.m_arc0.m_origin),
							 c, 1.0f, kPrimEnableWireframe));

		g_prim.Draw(DebugString(origin, "a0", c, 0.65f));
	}

	if (corner.m_hasArc1)
	{
		Point origin = m_pMesh->LocalToWorld(corner.m_arc1.m_origin) + evo;

		g_prim.Draw(DebugCross(origin, 0.1f, c, kPrimEnableWireframe));

		g_prim.Draw(DebugLine(origin,
							  m_pMesh->LocalToWorld(corner.m_arc1.m_begin) + evo,
							  c, kPrimEnableHiddenLineAlpha));

		g_prim.Draw(DebugLine(origin,
							  m_pMesh->LocalToWorld(corner.m_arc1.m_end) + evo,
							  c, kPrimEnableHiddenLineAlpha));

		g_prim.Draw(DebugArc(origin,
							 m_pMesh->LocalToWorld(corner.m_arc1.m_begin - corner.m_arc1.m_origin),
							 m_pMesh->LocalToWorld(corner.m_arc1.m_end - corner.m_arc1.m_origin),
							 c, 1.0f, kPrimEnableWireframe));

		g_prim.Draw(DebugString(origin, "a1", c, 0.65f));
	}

	if (corner.m_hasBeginEdge)
	{
		Point begin = m_pMesh->LocalToWorld(corner.m_begin) + evo;
		Point end = m_pMesh->LocalToWorld(corner.m_arc0.m_begin) + evo;

		g_prim.Draw(DebugLine(begin, end, c, 1.0f, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugString(Lerp(begin, end, 0.5f), "b", c, 0.65f));
	}

	if (corner.m_hasEdgeBetweenArcs)
	{
		Point begin = m_pMesh->LocalToWorld(corner.m_arc0.m_end) + evo;
		Point end = m_pMesh->LocalToWorld(corner.m_arc1.m_begin) + evo;

		g_prim.Draw(DebugLine(begin, end, c, 1.0f, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugString(Lerp(begin, end, 0.5f), "m", c, 0.65f));
	}

	if (corner.m_hasEndEdge)
	{
		Point begin = m_pMesh->LocalToWorld(corner.m_arc1.m_end) + evo;
		Point end = m_pMesh->LocalToWorld(corner.m_end) + evo;

		g_prim.Draw(DebugLine(begin, end, c, 1.0f, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugString(Lerp(begin, end, 0.5f), "e", c, 0.65f));
	}

	if (label && (strlength(label) > 0))
	{
		g_prim.Draw(DebugString(cornerWs, label, c, 0.65f));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::DebugDrawEdgeRange(Point_arg v0, Point_arg v1) const
{
	STRIP_IN_FINAL_BUILD;

	const Point v0Ws = m_pMesh->LocalToWorld(v0);
	const Point v1Ws = m_pMesh->LocalToWorld(v1);
	const Vector vo = Vector(0.0f, 0.11f, 0.0f);

	g_prim.Draw(DebugLine(v0Ws + vo, v1Ws + vo, kColorGreen, 4.0f, kPrimEnableHiddenLineAlpha));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshStadiumDepenetrator::DebugDrawCornerRange(Point_arg origin, Vector_arg normal, float startAngleRad, float endAngleRad) const
{
	STRIP_IN_FINAL_BUILD;

	const Vector dir0 = Rotate(QuatFromAxisAngle(kUnitYAxis, startAngleRad), normal);
	const Vector dir1 = Rotate(QuatFromAxisAngle(kUnitYAxis, endAngleRad), normal);

	const Vector vo = Vector(0.0f, 0.11f, 0.0f);
	const Point centerWs = m_pMesh->LocalToWorld(origin);

	g_prim.Draw(DebugArc(centerWs + vo,
						 dir0 * m_depenRadius,
						 dir1 * m_depenRadius,
						 kColorGreen,
						 4.0f,
						 kPrimEnableWireframe,
						 true));
}

/************************************************************************/
/*                                                                      */
/************************************************************************/
NavMeshDepenetrator2::FeatureIndex NavMeshDepenetrator2::kInvalidFeatureIndex = { -1, false };

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshDepenetrator2::Init(Point_arg posLs,
								const NavMesh* pInitialMesh,
								const NavMesh::FindPointParams& probeParams)
{
	NAV_ASSERT(IsReasonable(posLs));
	NAV_ASSERT(IsReasonable(probeParams.m_depenRadius));

	m_depenRadius = probeParams.m_depenRadius;

	const F32 collectRadius = Max(m_depenRadius * 2.0f, probeParams.m_searchRadius);

	NavMeshClearanceProbe::Init(posLs, collectRadius, probeParams);

	m_pMesh = pInitialMesh;

	const Memory::Allocator* pTopAllocator = Memory::TopAllocator();

	const size_t reqSize = sizeof(Edge) * 64;
	if (!pTopAllocator || !pTopAllocator->CanAllocate(reqSize, ALIGN_OF(Edge)))
	{
		AllocateJanitor alloc(kAllocSingleGameFrame, FILE_LINE_FUNC);
		m_pEdges = NDI_NEW Edge[64];
	}
	else
	{
		m_pEdges = NDI_NEW Edge[64];
	}

	m_maxEdges = 64;
	m_numEdges = 0;

	m_pResolvedPoly	  = nullptr;
	m_pResolvedPolyEx = nullptr;
	m_closestEdgeDist = kLargeFloat;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshDepenetrator2::VisitEdge(const NavMesh* pMesh,
									 const NavPoly* pPoly,
									 const NavPolyEx* pPolyEx,
									 I32F iEdge,
									 NavMesh::BoundaryFlags boundaryFlags)
{
	if (NavMeshClearanceProbe::VisitEdge(pMesh, pPoly, pPolyEx, iEdge, boundaryFlags))
		return true;

	if (boundaryFlags == NavMesh::kBoundaryNone)
		return false;

	const U32F insideStaticFlags = NavMesh::kBoundaryInside | NavMesh::kBoundaryTypeStatic;
	if ((boundaryFlags & insideStaticFlags) == insideStaticFlags)
	{
		// We're both inside a poly, and this edge is a static boundary, so don't flip it and add a new edge
		// on the other side (which would be illegal space). Instead, just bail.
		return false;
	}

	// blocking edge, record it
	if (m_numEdges >= m_maxEdges)
	{
		MsgWarn("NavMeshDepenetrator2 needs more than %d edges\n", (int)m_maxEdges);
		return false;
	}

	Point v0 = pPolyEx ? pPolyEx->GetVertex(iEdge) : pPoly->GetVertex(iEdge);
	Point v1 = pPolyEx ? pPolyEx->GetNextVertex(iEdge) : pPoly->GetNextVertex(iEdge);

	if (pMesh != m_pMesh)
	{
		v0 = m_pMesh->WorldToLocal(pMesh->LocalToWorld(v0));
		v1 = m_pMesh->WorldToLocal(pMesh->LocalToWorld(v1));
	}

	Scalar t0, t1;
	const F32 distToPos = DistPointSegmentXz(m_posLs, v0, v1);

	NAV_ASSERT(IsReasonable(distToPos));

	const U32F boundaryType = boundaryFlags & NavMesh::kBoundaryTypeAll;

	const bool flipped = (((boundaryFlags & NavMesh::kBoundaryInside) != 0)
						  && ((boundaryType & NavMesh::kBoundaryTypeStealth) == 0));

	Edge& newEdge = m_pEdges[m_numEdges];
	++m_numEdges;

	newEdge.m_v0 = flipped ? v1 : v0;
	newEdge.m_v1 = flipped ? v0 : v1;

	const Vector edgeDir = AsUnitVectorXz(newEdge.m_v1 - newEdge.m_v0, kZero);

	newEdge.m_normal = RotateY90(edgeDir);

	//const Vector nudgeVec = edgeDir * kNudgeEpsilon;
	const Vector offset	  = newEdge.m_normal * m_depenRadius;

	newEdge.m_distToPos = flipped ? -distToPos : distToPos;
	newEdge.m_offset0	= newEdge.m_v0 + offset; // - nudgeVec;
	newEdge.m_offset1	= newEdge.m_v1 + offset; // + nudgeVec;
	newEdge.m_iCorner0 = newEdge.m_iCorner1 = -1;

	m_closestEdgeDist = Min(m_closestEdgeDist, newEdge.m_distToPos);

	// expand if we flipped the edge
	return flipped;
}

/// -------------------------------------------------------------------------------------------------------------- ///
void NavMeshDepenetrator2::FinalizePoint(Point_arg pos)
{
	m_resolvedPoint = pos;

	const NavPolyListEntry containing = GetContainingPoly(m_resolvedPoint);
	m_pResolvedPoly = containing.m_pPoly;
	m_pResolvedPolyEx = containing.m_pPolyEx;
}

/// -------------------------------------------------------------------------------------------------------------- ///
void NavMeshDepenetrator2::Finalize()
{
	PROFILE(Navigation, Depen2_Finalize);

	const bool debugDraw = m_baseProbeParams.m_debugDraw;

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		for (U32F iEdge = 0; iEdge < m_numEdges; ++iEdge)
		{
			const Edge& e = m_pEdges[iEdge];
			DebugDrawEdge(e, 0.1f, kColorRedTrans, StringBuilder<64>("%d (%0.3fm)", (int)iEdge, e.m_distToPos).c_str());
		}
	}

	if ((m_numEdges == 0) || (m_closestEdgeDist >= m_depenRadius))
	{
		FinalizePoint(m_posLs);
		return; // nothing to do
	}

	F32 searchRadius = (m_radius + m_depenRadius) * 2.15f;

	// worst case is isolated edges, each with 2 fake corners
	const U32F maxCorners = 2 * m_numEdges;
	const U32F maxIntersections = Min(m_numEdges * m_numEdges * 2, 1024ULL);

	ExtCorner* pCorners = nullptr;
	Intersection* pIntersections = nullptr;
	LocalIntersection* pLocalIntersections = nullptr;

	static CONST_EXPR Alignment alignCorner	  = ALIGN_OF(ExtCorner);
	static CONST_EXPR Alignment alignInt	  = ALIGN_OF(Intersection);
	static CONST_EXPR Alignment alignLocalInt = ALIGN_OF(LocalIntersection);
	static CONST_EXPR Alignment alignMax	  = Alignment(Max(Max(alignCorner.GetValue(), alignInt.GetValue()),
														  alignLocalInt.GetValue()));

	const size_t reqSize = AlignSize(sizeof(ExtCorner) * maxCorners, alignCorner)
						   + AlignSize(sizeof(Intersection) * maxIntersections, alignInt)
						   + AlignSize(sizeof(LocalIntersection) * maxIntersections, alignLocalInt);

	const Memory::Allocator* pTopAllocator = Memory::TopAllocator();

	if (!pTopAllocator || !pTopAllocator->CanAllocate(reqSize, alignMax))
	{
		AllocateJanitor alloc(kAllocSingleGameFrame, FILE_LINE_FUNC);

		pCorners	   = NDI_NEW ExtCorner[maxCorners];
		pIntersections = NDI_NEW Intersection[maxIntersections];
		pLocalIntersections = NDI_NEW LocalIntersection[maxIntersections];
	}
	else
	{
		pCorners	   = NDI_NEW ExtCorner[maxCorners];
		pIntersections = NDI_NEW Intersection[maxIntersections];
		pLocalIntersections = NDI_NEW LocalIntersection[maxIntersections];
	}

	U32 numCorners		 = GenerateCorners(pCorners, maxCorners);
	U32 numIntersections = GenerateIntersections(pIntersections, maxIntersections, pCorners, numCorners);

	F32 bestDist = kLargeFloat;
	Point bestPos = m_posLs;

	for (U32F iEdge = 0; iEdge < m_numEdges; ++iEdge)
	{
		const Edge& edge = m_pEdges[iEdge];

		const FeatureIndex edgeFeatureIdx = FeatureIndex::FromEdge(iEdge);
		const FeatureIndex corner0Idx	  = FeatureIndex::FromCorner(edge.m_iCorner0);

		const U32F numLocalIntersections = GatherIntersectionsForEdge(iEdge,
																	  pIntersections,
																	  numIntersections,
																	  pCorners,
																	  numCorners,
																	  pLocalIntersections);

		const bool startClear = IsPointClear(edge.m_offset0,
											 edgeFeatureIdx,
											 corner0Idx,
											 searchRadius,
											 edge.m_normal,
											 pCorners,
											 numCorners);

		float startTT = startClear ? 0.0f : -1.0f;

		for (U32F iInt = 0; iInt < numLocalIntersections; ++iInt)
		{
			const LocalIntersection& lint = pLocalIntersections[iInt];

			if (lint.m_forward)
			{
				startTT = lint.m_tt;
			}
			else if (startTT >= 0.0f)
			{
				const Point v0 = Lerp(edge.m_offset0, edge.m_offset1, startTT);
				const Point v1 = Lerp(edge.m_offset0, edge.m_offset1, lint.m_tt);

				Scalar closestT;
				const float d = DistPointSegmentXz(m_posLs, v0, v1, nullptr, &closestT);

				if (d < bestDist)
				{
					bestDist = d;
					bestPos = Lerp(v0, v1, closestT);
				}

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					DebugDrawEdgeRange(edge, v0, v1);
				}

				startTT = -1.0f;
			}
		}

		if (startTT >= 0.0f)
		{
			const FeatureIndex corner1Idx = FeatureIndex::FromCorner(edge.m_iCorner1);

			const bool endClear = IsPointClear(edge.m_offset1,
											   edgeFeatureIdx,
											   corner1Idx,
											   searchRadius,
											   edge.m_normal,
											   pCorners,
											   numCorners);

			if (endClear)
			{
				const Point v0 = Lerp(edge.m_offset0, edge.m_offset1, startTT);
				const Point v1 = edge.m_offset1;

				Scalar closestT;
				const float d = DistPointSegmentXz(m_posLs, v0, v1, nullptr, &closestT);

				if (d < bestDist)
				{
					bestDist = d;
					bestPos	 = Lerp(v0, v1, closestT);
				}

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					DebugDrawEdgeRange(edge, v0, v1);
				}
			}
		}
	}

	for (U32F iCorner = 0; iCorner < numCorners; ++iCorner)
	{
		const ExtCorner& corner = pCorners[iCorner];

		const FeatureIndex cornerFeatureIdx = FeatureIndex::FromCorner(iCorner);

		const U32F numLocalIntersections = GatherIntersectionsForCorner(iCorner,
																		pIntersections,
																		numIntersections,
																		pCorners,
																		numCorners,
																		pLocalIntersections);

		const Point startPos = corner.m_vert + (corner.m_dir0 * m_depenRadius);
		const bool startClear = IsPointClear(startPos,
											 cornerFeatureIdx,
											 kInvalidFeatureIndex,
											 searchRadius,
											 kZero,
											 pCorners,
											 numCorners);
		float startAngle = startClear ? -corner.m_halfAngleRad : -kLargeFloat;

		for (U32F iInt = 0; iInt < numLocalIntersections; ++iInt)
		{
			const LocalIntersection& lint = pLocalIntersections[iInt];

			if (lint.m_forward)
			{
				startAngle = lint.m_tt;
			}
			else if (startAngle >= -corner.m_halfAngleRad)
			{
				Point closestPt;
				const float d = DistPointArcXz(m_posLs,
											   corner.m_vert,
											   corner.m_normal,
											   m_depenRadius,
											   startAngle,
											   lint.m_tt,
											   &closestPt);

				if (d < bestDist)
				{
					bestDist = d;
					bestPos = closestPt;
				}

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					DebugDrawCornerRange(corner, startAngle, lint.m_tt);
				}

				startAngle = -kLargeFloat;
			}
		}

		if (startAngle >= -corner.m_halfAngleRad)
		{
			const Point endPos = corner.m_vert + (corner.m_dir1 * m_depenRadius);

			if (IsPointClear(endPos,
							 cornerFeatureIdx,
							 kInvalidFeatureIndex,
							 searchRadius,
							 kZero,
							 pCorners,
							 numCorners))
			{
				Point closestPt;
				const float d = DistPointArcXz(m_posLs,
											   corner.m_vert,
											   corner.m_normal,
											   m_depenRadius,
											   startAngle,
											   corner.m_halfAngleRad,
											   &closestPt);

				if (d < bestDist)
				{
					bestDist = d;
					bestPos = closestPt;
				}

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					DebugDrawCornerRange(corner, startAngle, corner.m_halfAngleRad);
				}
			}
		}
	}

	if (bestDist < m_radius)
	{
		FinalizePoint(bestPos);
	}

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		if (bestDist < m_radius)
		{
			g_prim.Draw(DebugCross(m_pMesh->LocalToWorld(m_resolvedPoint), 0.2f, kColorCyan, kPrimEnableHiddenLineAlpha));
		}
		else if (bestDist < kLargeFloat)
		{
			const Point bestPosWs = m_pMesh->LocalToWorld(bestPos);
			const Point startPosWs = m_pMesh->LocalToWorld(m_posLs);
			g_prim.Draw(DebugArrow(startPosWs, bestPosWs, kColorMagentaTrans, 0.5f, kPrimEnableHiddenLineAlpha));
			g_prim.Draw(DebugCross(bestPosWs, 0.2f, kColorMagenta, kPrimEnableHiddenLineAlpha, StringBuilder<256>("too far (%0.3fm > %0.3fm)", bestDist, m_radius).c_str(), 0.5f));

			g_prim.Draw(DebugCross(startPosWs, 0.1f, kColorMagenta, kPrimEnableHiddenLineAlpha));
			g_prim.Draw(DebugCircle(startPosWs, kUnitYAxis, m_radius, kColorMagenta, kPrimEnableHiddenLineAlpha));
		}

		for (U32F iCorner = 0; iCorner < numCorners; ++iCorner)
		{
			DebugDrawCorner(pCorners[iCorner], 0.1f, kColorRedTrans, StringBuilder<64>("c%d", iCorner).c_str());
		}

		for (U32F iInt = 0; iInt < numIntersections; ++iInt)
		{
			const Intersection& inter = pIntersections[iInt];

			const Point intPosWs = m_pMesh->LocalToWorld(inter.m_intPos);

			g_prim.Draw(DebugCross(intPosWs + Vector(0.0f, 0.1f, 0.0f), 0.025f, kColorOrange));
			g_prim.Draw(DebugString(intPosWs + Vector(0.0f, 0.1f, 0.0f), StringBuilder<64>("%d", iInt).c_str(), kColorOrange, 0.5f));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshDepenetrator2::CreateCorner(ExtCorner& cornerOut,
										Point_arg vert,
										Vector_arg normal0,
										Vector_arg normal1,
										I32F iEdge0,
										I32F iEdge1) const
{
	const Vector cornerNormal = SafeNormalize(normal0 + normal1, kUnitZAxis);
	cornerOut.m_vert = vert;
	cornerOut.m_normal = cornerNormal;

	if (CrossY(normal0, cornerNormal) > 0.0f)
	{
		cornerOut.m_dir0 = normal0;
		cornerOut.m_dir1 = normal1;
	}
	else
	{
		cornerOut.m_dir0 = normal1;
		cornerOut.m_dir1 = normal0;
	}

	cornerOut.m_cosHalfAngle = DotXz(cornerNormal, normal0);
	cornerOut.m_iEdge0 = iEdge0;
	cornerOut.m_iEdge1 = iEdge1;
	cornerOut.m_halfAngleRad = SafeAcos(cornerOut.m_cosHalfAngle);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMeshDepenetrator2::GenerateCorners(ExtCorner* pCornersOut, U32F maxCornersOut) const
{
	if (!pCornersOut || 0 == maxCornersOut)
	{
		return 0;
	}

	const size_t numBlocks = ExternalBitArrayStorage::DetermineNumBlocks(m_numEdges);
	U64* aBlocks0 = NDI_NEW U64[numBlocks];
	U64* aBlocks1 = NDI_NEW U64[numBlocks];

	NAV_ASSERT(aBlocks0);
	NAV_ASSERT(aBlocks1);

	ExternalBitArray attached0(m_numEdges, aBlocks0, false);
	ExternalBitArray attached1(m_numEdges, aBlocks1, false);

	U32F numCorners = 0;

	for (U32F iEdge = 0; iEdge < m_numEdges; ++iEdge)
	{
		const Edge& e0 = m_pEdges[iEdge];

		for (U32F iOtherEdge = iEdge + 1; iOtherEdge < m_numEdges; ++iOtherEdge)
		{
			const Edge& e1 = m_pEdges[iOtherEdge];

			Point vert = kOrigin;
			Vector testEdgeVec = kZero;

			I32F iCornerEdge0;
			I32F iCornerEdge1;

			if (Dist(e0.m_v0, e1.m_v1) < 0.001f)
			{
				vert = e0.m_v0;
				testEdgeVec = e0.m_v1 - e0.m_v0;

				attached0.SetBit(iEdge);
				attached1.SetBit(iOtherEdge);

				iCornerEdge0 = iEdge;
				iCornerEdge1 = iOtherEdge;
			}
			else if (Dist(e0.m_v1, e1.m_v0) < 0.001f)
			{
				attached1.SetBit(iEdge);
				attached0.SetBit(iOtherEdge);

				vert = e0.m_v1;
				testEdgeVec = e0.m_v0 - e0.m_v1;

				iCornerEdge0 = iOtherEdge;
				iCornerEdge1 = iEdge;
			}
			else
			{
				continue;
			}

			const Vector normal0 = e0.m_normal;
			const Vector normal1 = e1.m_normal;

			const float dotP = DotXz(testEdgeVec, normal1);

			if (dotP >= -0.001f)
				continue;

			m_pEdges[iCornerEdge0].m_iCorner0 = numCorners;
			m_pEdges[iCornerEdge1].m_iCorner1 = numCorners;

			CreateCorner(pCornersOut[numCorners], vert, normal0, normal1, iCornerEdge0, iCornerEdge1);
			++numCorners;

			if (numCorners >= maxCornersOut)
				break;
		}

		if (numCorners >= maxCornersOut)
			break;
	}

	if (numCorners >= maxCornersOut)
		return numCorners;

	for (U32F iEdge = 0; iEdge < m_numEdges; ++iEdge)
	{
		const Edge& edge = m_pEdges[iEdge];

		if (!attached0.IsBitSet(iEdge))
		{
			const Vector normal0 = edge.m_normal;
			const Vector normal1 = RotateY90(edge.m_normal);

			CreateCorner(pCornersOut[numCorners], edge.m_v0, normal0, normal1, iEdge, iEdge);
			++numCorners;

			if (numCorners >= maxCornersOut)
				break;
		}

		if (!attached1.IsBitSet(iEdge))
		{
			const Vector normal0 = RotateYMinus90(edge.m_normal);
			const Vector normal1 = edge.m_normal;

			CreateCorner(pCornersOut[numCorners], edge.m_v1, normal0, normal1, iEdge, iEdge);
			++numCorners;

			if (numCorners >= maxCornersOut)
				break;
		}
	}

	return numCorners;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMeshDepenetrator2::GenerateIntersections(Intersection* pIntersectionsOut,
												 U32F maxIntersections,
												 const ExtCorner* pCorners,
												 U32F numCorners) const
{
	if (m_numEdges == 0)
	{
		return 0;
	}

	U32F numIntersections = 0;
	const float searchRadius = (m_depenRadius + m_radius) * 2.15f;

	for (I32F iEdge0 = 0; iEdge0 < (m_numEdges - 1); ++iEdge0)
	{
		if (numIntersections >= maxIntersections)
			break;

		const FeatureIndex idx0 = FeatureIndex::FromEdge(iEdge0);

		const Edge& edge0 = m_pEdges[iEdge0];
		const Segment seg0 = Segment(edge0.m_offset0, edge0.m_offset1);

		for (I32F iEdge1 = iEdge0 + 1; iEdge1 < m_numEdges; ++iEdge1)
		{
			if (numIntersections >= maxIntersections)
				break;

			const FeatureIndex idx1 = FeatureIndex::FromEdge(iEdge1);

			const Edge& edge1 = m_pEdges[iEdge1];

			const bool connected = AreEdgesConnected(iEdge0, iEdge1, pCorners, numCorners);

			const float normDot = DotXz(edge0.m_normal, edge1.m_normal);
			if (normDot > 0.99f && !connected)
			{
				continue;
			}

			const Segment seg1 = Segment(edge1.m_offset0, edge1.m_offset1);

			Scalar t0, t1;
			if (IntersectSegmentSegmentXz(seg0, seg1, t0, t1))
			{
				if (connected)
				{
					if (t0 <= (NDI_FLT_EPSILON) && t1 >= (1.0f - NDI_FLT_EPSILON))
						continue;
					if (t1 <= (NDI_FLT_EPSILON) && t0 >= (1.0f - NDI_FLT_EPSILON))
						continue;
				}

				const Point intPos = Lerp(seg0.a, seg0.b, t0);

				if (IsPointClear(intPos, idx0, idx1, searchRadius, kZero, pCorners, numCorners))
				{
					Intersection& newInt = pIntersectionsOut[numIntersections];
					newInt.m_feature0.AsEdge(iEdge0);
					newInt.m_feature1.AsEdge(iEdge1);
					newInt.m_tt0 = t0;
					newInt.m_tt1 = t1;
					newInt.m_intPos = intPos;

					++numIntersections;
				}
			}
		}
	}

	if (numCorners > 0)
	{
		const float rSqr = Sqr(m_depenRadius);
		const float depenDiameter = 2.0f * m_depenRadius;
		const float depenDiameterSqr = Sqr(depenDiameter);
		const float invR = 1.0f / m_depenRadius;

		for (U32F iCorner = 0; iCorner < numCorners; ++iCorner)
		{
			const ExtCorner& corner = pCorners[iCorner];

			const FeatureIndex cornerIdx0 = FeatureIndex::FromCorner(iCorner);

			for (U32F iOtherCorner = iCorner + 1; iOtherCorner < numCorners; ++iOtherCorner)
			{
				const ExtCorner& otherCorner = pCorners[iOtherCorner];

				const FeatureIndex cornerIdx1 = FeatureIndex::FromCorner(iOtherCorner);

				const float cornerDistSqr = DistSqr(corner.m_vert, otherCorner.m_vert);

				if (cornerDistSqr > depenDiameterSqr)
					continue;

				const float cornerDist = Sqrt(cornerDistSqr);

				const float extent = Sqrt(rSqr - Sqr(cornerDist * 0.5f));
				const Point mid = Lerp(corner.m_vert, otherCorner.m_vert, 0.5f);
				const Vector perpDir = RotateY90(Normalize(VectorXz(mid - corner.m_vert)));

				const Point int0 = mid + (perpDir * extent);
				const Point int1 = mid - (perpDir * extent);
				const Vector dir00 = (int0 - corner.m_vert) * invR;
				const Vector dir01 = (int1 - corner.m_vert) * invR;
				const Vector dir10 = (int0 - otherCorner.m_vert) * invR;
				const Vector dir11 = (int1 - otherCorner.m_vert) * invR;

				const float dot00 = DotXz(corner.m_normal, dir00);
				const float dot01 = DotXz(corner.m_normal, dir01);
				const float dot10 = DotXz(otherCorner.m_normal, dir10);
				const float dot11 = DotXz(otherCorner.m_normal, dir11);

				if ((dot00 >= corner.m_cosHalfAngle) && (dot10 >= otherCorner.m_cosHalfAngle))
				{
					if (IsPointClear(int0, cornerIdx0, cornerIdx1, searchRadius, kZero, pCorners, numCorners))
					{
						const float angle00 = GetRelativeXzAngleDiffRad(corner.m_normal, dir00);
						const float angle10 = GetRelativeXzAngleDiffRad(otherCorner.m_normal, dir10);

						Intersection& newInt = pIntersectionsOut[numIntersections];
						newInt.m_intPos = int0;
						newInt.m_feature0.AsCorner(iCorner);
						newInt.m_feature1.AsCorner(iOtherCorner);
						newInt.m_tt0 = angle00;
						newInt.m_tt1 = angle10;

						++numIntersections;
					}
				}

				if ((dot01 >= corner.m_cosHalfAngle) && (dot11 >= otherCorner.m_cosHalfAngle))
				{
					if (IsPointClear(int1, cornerIdx0, cornerIdx1, searchRadius, kZero, pCorners, numCorners))
					{
						const float angle01 = GetRelativeXzAngleDiffRad(corner.m_normal, dir01);
						const float angle11 = GetRelativeXzAngleDiffRad(otherCorner.m_normal, dir11);

						Intersection& newInt = pIntersectionsOut[numIntersections];
						newInt.m_intPos = int1;
						newInt.m_feature0.AsCorner(iCorner);
						newInt.m_feature1.AsCorner(iOtherCorner);
						newInt.m_tt0 = angle01;
						newInt.m_tt1 = angle11;

						++numIntersections;
					}
				}
			}
		}

		for (U32F iCorner = 0; iCorner < numCorners; ++iCorner)
		{
			const ExtCorner& corner = pCorners[iCorner];

			const FeatureIndex cornerIdx = FeatureIndex::FromCorner(iCorner);

			for (U32F iEdge = 0; iEdge < m_numEdges; ++iEdge)
			{
				if (corner.m_iEdge0 == iEdge || corner.m_iEdge1 == iEdge)
					continue;

				const Edge& edge = m_pEdges[iEdge];

				const FeatureIndex edgeIdx = FeatureIndex::FromEdge(iEdge);

				Point closestPt;
				const float edgeDist = DistPointLineXz(corner.m_vert, edge.m_offset0, edge.m_offset1, &closestPt);
				if (edgeDist > m_depenRadius || edgeDist < NDI_FLT_EPSILON)
					continue;

				const float extent	 = Sqrt(rSqr - Sqr(edgeDist));
				const Vector perpDir = RotateY90(Normalize(VectorXz(closestPt - corner.m_vert)));

				const Point int0  = closestPt + (perpDir * extent);
				const Point int1  = closestPt - (perpDir * extent);
				const Vector dir0 = (int0 - corner.m_vert) * invR;
				const Vector dir1 = (int1 - corner.m_vert) * invR;

				const float dot0 = DotXz(corner.m_normal, dir0);
				const float dot1 = DotXz(corner.m_normal, dir1);

				if (dot0 >= corner.m_cosHalfAngle)
				{
					const float edgeTT = IndexOfPointOnEdgeXz(edge.m_offset0, edge.m_offset1, int0);

					if (edgeTT >= 0.0f && edgeTT <= 1.0f
						&& IsPointClear(int0, cornerIdx, edgeIdx, searchRadius, edge.m_normal, pCorners, numCorners))
					{
						const float angleTT = GetRelativeXzAngleDiffRad(corner.m_normal, dir0);
						Intersection& newInt = pIntersectionsOut[numIntersections];
						newInt.m_feature0.AsCorner(iCorner);
						newInt.m_feature1.AsEdge(iEdge);
						newInt.m_tt0 = angleTT;
						newInt.m_tt1 = edgeTT;
						newInt.m_intPos = int0;
						++numIntersections;
					}
				}

				if (dot1 >= corner.m_cosHalfAngle)
				{
					const float edgeTT = IndexOfPointOnEdgeXz(edge.m_offset0, edge.m_offset1, int1);

					if (edgeTT >= 0.0f && edgeTT <= 1.0f
						&& IsPointClear(int1, cornerIdx, edgeIdx, searchRadius, edge.m_normal, pCorners, numCorners))
					{
						const float angleTT = GetRelativeXzAngleDiffRad(corner.m_normal, dir1);

						Intersection& newInt = pIntersectionsOut[numIntersections];
						newInt.m_feature0.AsCorner(iCorner);
						newInt.m_feature1.AsEdge(iEdge);
						newInt.m_tt0 = angleTT;
						newInt.m_tt1 = edgeTT;
						newInt.m_intPos = int1;
						++numIntersections;
					}
				}
			}
		}
	}

	NAV_ASSERT(numIntersections <= maxIntersections);

	return numIntersections;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMeshDepenetrator2::GatherIntersectionsForEdge(I32F iEdge,
													  const Intersection* pIntersections,
													  U32F numIntersections,
													  const ExtCorner* pCorners,
													  U32F numCorners,
													  LocalIntersection* pIntersectionsOut) const
{
	if (0 == numIntersections)
	{
		return 0;
	}

	const Edge& edge = m_pEdges[iEdge];
	const Vector edgeVec = edge.m_v1 - edge.m_v0;

	U32F numLocal = 0;

	for (U32F iInt = 0; iInt < numIntersections; ++iInt)
	{
		const Intersection& intersection = pIntersections[iInt];
		if (intersection.m_feature0.IsEdge(iEdge))
		{
			bool forward = false;

			if (intersection.m_feature1.m_isCorner)
			{
				const ExtCorner& otherCorner = pCorners[intersection.m_feature1.m_index];
				const float dotP = DotXz(edgeVec, intersection.m_intPos - otherCorner.m_vert);
				forward = dotP > 0.0f;

			}
			else
			{
				const Edge& otherEdge = m_pEdges[intersection.m_feature1.m_index];
				const float dotP = DotXz(edgeVec, otherEdge.m_normal);
				forward = dotP > 0.0f;
			}

			pIntersectionsOut[numLocal].m_tt = intersection.m_tt0;
			pIntersectionsOut[numLocal].m_forward = forward;
			++numLocal;
		}
		else if (intersection.m_feature1.IsEdge(iEdge))
		{
			bool forward = false;

			if (intersection.m_feature0.m_isCorner)
			{
				const ExtCorner& otherCorner = pCorners[intersection.m_feature0.m_index];
				const float dotP = DotXz(edgeVec, intersection.m_intPos - otherCorner.m_vert);
				forward = dotP > 0.0f;
			}
			else
			{
				const Edge& otherEdge = m_pEdges[intersection.m_feature0.m_index];
				const float dotP = DotXz(edgeVec, otherEdge.m_normal);
				forward = dotP > 0.0f;
			}

			pIntersectionsOut[numLocal].m_tt = intersection.m_tt1;
			pIntersectionsOut[numLocal].m_forward = forward;
			++numLocal;
		}
	}

	if (numLocal > 1)
	{
		QuickSort(pIntersectionsOut, numLocal, LocalIntersection::Compare);
	}

	return numLocal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMeshDepenetrator2::GatherIntersectionsForCorner(U32F iCorner,
														const Intersection* pIntersections,
														U32F numIntersections,
														const ExtCorner* pCorners,
														U32F numCorners,
														LocalIntersection* pIntersectionsOut) const
{
	if (0 == numIntersections)
	{
		return 0;
	}

	const ExtCorner& corner = pCorners[iCorner];
	//const Vector cornerFw = RotateY90(corner.m_normal);

	U32F numLocal = 0;

	for (U32F iInt = 0; iInt < numIntersections; ++iInt)
	{
		const Intersection& intersection = pIntersections[iInt];
		if (intersection.m_feature0.IsCorner(iCorner))
		{
			bool forward = false;

			if (intersection.m_feature1.m_isCorner)
			{
				const ExtCorner& otherCorner = pCorners[intersection.m_feature1.m_index];
				//const float cy = CrossY(corner.m_normal, intersection.m_intPos - otherCorner.m_vert);
				const float cy = CrossY(otherCorner.m_vert - corner.m_vert, intersection.m_intPos - corner.m_vert);
				forward = cy > 0.0f;
				//const float ds = Sign(DotXz(otherCorner.m_vert - corner.m_vert, corner.m_normal));
				//forward = (cy * ds) > 0.0f;
			}
			else
			{
				const Edge& otherEdge = m_pEdges[intersection.m_feature1.m_index];
				const Vector tangent = RotateY90(intersection.m_intPos - corner.m_vert);
				const float dotP = DotXz(tangent, otherEdge.m_normal);
				forward = dotP > 0.0f;
			}

			pIntersectionsOut[numLocal].m_tt = intersection.m_tt0;
			pIntersectionsOut[numLocal].m_forward = forward;
			++numLocal;
		}
		else if (intersection.m_feature1.IsCorner(iCorner))
		{
			bool forward = false;

			if (intersection.m_feature0.m_isCorner)
			{
				const ExtCorner& otherCorner = pCorners[intersection.m_feature0.m_index];
				//const float cy = CrossY(corner.m_normal, intersection.m_intPos - otherCorner.m_vert);
				const float cy = CrossY(otherCorner.m_vert - corner.m_vert, intersection.m_intPos - corner.m_vert);
				forward = cy > 0.0f;
				//const float ds = Sign(DotXz(otherCorner.m_vert - corner.m_vert, corner.m_normal));
				//forward = (cy * ds) > 0.0f;
			}
			else
			{
				const Edge& otherEdge = m_pEdges[intersection.m_feature0.m_index];
				const float dotP = DotXz(corner.m_normal, otherEdge.m_normal);
				forward = (Sign(intersection.m_tt1) * dotP) < 0.0f;
			}

			pIntersectionsOut[numLocal].m_tt = intersection.m_tt1;
			pIntersectionsOut[numLocal].m_forward = forward;
			++numLocal;
		}
	}

	if (numLocal > 1)
	{
		QuickSort(pIntersectionsOut, numLocal, LocalIntersection::Compare);
	}

	return numLocal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavPolyListEntry NavMeshDepenetrator2::GetContainingPoly(Point_arg pos) const
{
	for (U32F iPoly = 0; iPoly < m_closedListSize; ++iPoly)
	{
		Point testPosLs = pos;
		if (m_pClosedList[iPoly].m_pMesh != m_pMesh)
		{
			testPosLs = m_pClosedList[iPoly].m_pMesh->WorldToLocal(m_pMesh->LocalToWorld(pos));
		}

		if (m_pClosedList[iPoly].m_pPolyEx)
		{
			if (m_pClosedList[iPoly].m_pPolyEx->PolyContainsPointLs(testPosLs))
			{
				ASSERT(m_pClosedList[iPoly].m_pPoly->PolyContainsPointLs(testPosLs, 0.1f));
				return m_pClosedList[iPoly];
			}
		}
		else if (m_pClosedList[iPoly].m_pPoly)
		{
			if (m_pClosedList[iPoly].m_pPoly->PolyContainsPointLs(testPosLs))
			{
				return m_pClosedList[iPoly];
			}
		}
		else
		{
			ASSERT(false); // null entry in our closed list. how?
		}
	}

	NavPolyListEntry empty;
	empty.m_pMesh	= nullptr;
	empty.m_pPoly	= nullptr;
	empty.m_pPolyEx = nullptr;
	return empty;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshDepenetrator2::IsPointClear(Point_arg pos,
										FeatureIndex ignore0,
										FeatureIndex ignore1,
										float maxDist,
										Vector_arg testNorm,
										const ExtCorner* pCorners,
										U32F numCorners) const
{
	bool clear = true;

	for (U32F iEdge = 0; iEdge < m_numEdges; ++iEdge)
	{
		if (FeatureHasEdge(ignore0, iEdge, pCorners, numCorners) || FeatureHasEdge(ignore1, iEdge, pCorners, numCorners))
			continue;

		const Edge& edge = m_pEdges[iEdge];
		if (edge.m_distToPos > maxDist)
			continue;

		const float normDot = DotXz(edge.m_normal, testNorm);
		if (normDot > 0.999f)
			continue;

		Point closestPt;
		const float distToEdge = DistPointSegmentXz(pos, edge.m_v0, edge.m_v1, &closestPt);

		if ((distToEdge + NDI_FLT_EPSILON) < m_depenRadius)
		{
#if 0
			if (ignore0.IsCorner(3) && ignore1.IsEdge(0))
			{
				const Point v0Ws = m_pMesh->LocalToWorld(edge.m_v0);
				const Point v1Ws = m_pMesh->LocalToWorld(edge.m_v1);
				const Point closestWs = m_pMesh->LocalToWorld(closestPt);
				const Point posWs = m_pMesh->LocalToWorld(pos);

				g_prim.Draw(DebugCross(posWs, 0.1f, kColorCyanTrans));
				g_prim.Draw(DebugLine(v0Ws, v1Ws, kColorMagenta, 4.0f, kPrimEnableHiddenLineAlpha));
				g_prim.Draw(DebugArrow(posWs, closestWs, kColorCyan, 0.2f));
				g_prim.Draw(DebugString(AveragePos(posWs, closestWs), StringBuilder<64>("%0.3fm", distToEdge).c_str(), kColorCyan, 0.5f));
			}
#endif

			clear = false;
			break;
		}
	}

	return clear;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshDepenetrator2::AreEdgesConnected(U32F iEdge0, U32F iEdge1, const ExtCorner* pCorners, U32F numCorners) const
{
	for (U32F iCorner = 0; iCorner < numCorners; ++iCorner)
	{
		const ExtCorner& c = pCorners[iCorner];

		if ((c.m_iEdge0 == iEdge0) && (c.m_iEdge1 == iEdge1))
		{
			return true;
		}

		if ((c.m_iEdge0 == iEdge1) && (c.m_iEdge1 == iEdge0))
		{
			return true;
		}
	}

	const Edge& e0 = m_pEdges[iEdge0];
	const Edge& e1 = m_pEdges[iEdge1];

	if (DistXz(e0.m_v0, e1.m_v1) < kNudgeEpsilon)
	{
		return true;
	}

	if (DistXz(e0.m_v1, e1.m_v0) < kNudgeEpsilon)
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshDepenetrator2::DebugDrawEdge(const Edge& edge, float vo, Color c, const char* label /* = nullptr */) const
{
	STRIP_IN_FINAL_BUILD;

	const Vector evo = Vector(0.0f, vo, 0.0f);

	const Point v0Ws = m_pMesh->LocalToWorld(edge.m_v0) + evo;
	const Point v1Ws = m_pMesh->LocalToWorld(edge.m_v1) + evo;

	const Point offset0Ws = m_pMesh->LocalToWorld(edge.m_offset0) + evo;
	const Point offset1Ws = m_pMesh->LocalToWorld(edge.m_offset1) + evo;

	Color ct = c;
	ct.SetA(0.3f);

	const Point midWs = AveragePos(v0Ws, v1Ws);

	g_prim.Draw(DebugLine(v0Ws, v1Ws, ct, 4.0f));
	g_prim.Draw(DebugArrow(offset0Ws, offset1Ws, c, 0.15f, kPrimEnableHiddenLineAlpha));
	g_prim.Draw(DebugArrow(midWs, m_pMesh->LocalToWorld(edge.m_normal) * 0.125f, ct, 0.15f, kPrimEnableHiddenLineAlpha));
	g_prim.Draw(DebugString(Lerp(v0Ws, v1Ws, 0.1f), "v0", c, 0.65f));
	g_prim.Draw(DebugString(Lerp(v0Ws, v1Ws, 0.9f), "v1", c, 0.65f));

	if (label && (strlength(label) > 0))
	{
		g_prim.Draw(DebugString(midWs, label, c, 0.65f));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshDepenetrator2::DebugDrawCorner(const ExtCorner& corner,
										   float vo,
										   Color c,
										   const char* label /* = nullptr */) const
{
	STRIP_IN_FINAL_BUILD;

	const Vector evo = Vector(0.0f, vo, 0.0f);
	const Point cornerWs = m_pMesh->LocalToWorld(corner.m_vert) + evo;

	Vector leg0Ws = m_pMesh->LocalToWorld(corner.m_dir0) * m_depenRadius;
	Vector leg1Ws = m_pMesh->LocalToWorld(corner.m_dir1) * m_depenRadius;

	g_prim.Draw(DebugArc(cornerWs, leg0Ws, leg1Ws, c, 2.0f, kPrimEnableWireframe));
	g_prim.Draw(DebugLine(cornerWs, leg0Ws, c, kPrimEnableHiddenLineAlpha));
	g_prim.Draw(DebugLine(cornerWs, leg1Ws, c, kPrimEnableHiddenLineAlpha));
	g_prim.Draw(DebugString(cornerWs + (0.5f * leg0Ws), "0", kColorRedTrans, 0.5f));
	g_prim.Draw(DebugString(cornerWs + (0.5f * leg1Ws), "1", kColorRedTrans, 0.5f));
	g_prim.Draw(DebugArrow(cornerWs, m_pMesh->LocalToWorld(corner.m_normal) * m_depenRadius, c, 0.25f, kPrimEnableHiddenLineAlpha));

	if (label && (strlength(label) > 0))
	{
		g_prim.Draw(DebugString(cornerWs, label, c, 0.65f));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshDepenetrator2::DebugDrawEdgeRange(const Edge& edge, Point_arg v0, Point_arg v1) const
{
	STRIP_IN_FINAL_BUILD;

	const ptrdiff_t iEdge = &edge - m_pEdges;

	const Vector vo = Vector(0.0f, 0.11f, 0.0f);
	const Point v0Ws = m_pMesh->LocalToWorld(v0) + vo;
	const Point v1Ws = m_pMesh->LocalToWorld(v1) + vo;

	g_prim.Draw(DebugString(AveragePos(v0Ws, v1Ws), StringBuilder<64>("%d", iEdge).c_str(), kColorGreen));
	g_prim.Draw(DebugLine(v0Ws, v1Ws, kColorGreen, 4.0f, kPrimEnableHiddenLineAlpha));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshDepenetrator2::DebugDrawCornerRange(const ExtCorner& corner, float startAngleRad, float endAngleRad) const
{
	STRIP_IN_FINAL_BUILD;

	const Vector dir0 = Rotate(QuatFromAxisAngle(kUnitYAxis, startAngleRad), corner.m_normal);
	const Vector dir1 = Rotate(QuatFromAxisAngle(kUnitYAxis, endAngleRad), corner.m_normal);

	const Vector vo = Vector(0.0f, 0.11f, 0.0f);
	const Point centerWs = m_pMesh->LocalToWorld(corner.m_vert);

	g_prim.Draw(DebugArc(centerWs + vo,
						 dir0 * m_depenRadius,
						 dir1 * m_depenRadius,
						 kColorGreen,
						 4.0f,
						 kPrimEnableWireframe,
						 true));
}
