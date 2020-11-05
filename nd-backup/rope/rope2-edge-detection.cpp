/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/ndphys/rope/rope2-collector.h"
#include "gamelib/ndphys/rope/rope2-util.h"
#include "gamelib/ndphys/rope/rope-mgr.h"
#include "gamelib/ndphys/rope/physvectormath.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/ndphys/havok-data.h"
#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"
#include "ndlib/render/util/prim.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h"
#include "ndlib/debug//nd-dmenu.h"

extern bool g_straightenWithEdgesShortcutsOnly;

U64 g_ropeDebugSubStep = 50;

const Scalar kTan1Deg = 0.01746f;

F32 g_ropeEdgeSlideSpeed = 20.0f;
F32 g_ropeEdgeMaxNoMoveAngle = 0.2f*PI;

static Rope2::EdgeIntersection s_switchedEdgeInteresection [] =
{
	Rope2::kIntNone,					
	Rope2::kIntInnerCorner,
	Rope2::kIntInnerCorner1,
	Rope2::kIntInnerCorner0,
	Rope2::kIntInnerCorner01,
	Rope2::kIntOuterCorner10,
	Rope2::kIntOuterCorner01,
	Rope2::kIntInnerT1,
	Rope2::kIntInnerT0,
	Rope2::kIntSame
};

void Rope2::EdgePoint::Reset()
{
	m_flags = 0;
	m_internFlags = 0;
	m_numEdges = 0;
	m_activeEdges = 0;
	m_activeEdges0 = 0;
	m_edgePositive = 0;
	m_numNewEdges = 0;
	m_slideOff = 0;
}

void Rope2::EdgePoint::RemoveEdge(U32F ii)
{
	memmove(m_edgeIndices+ii, m_edgeIndices+ii+1, (m_numEdges-ii-1)*sizeof(m_edgeIndices[0]));
	U32 maskLower = (1U << ii) - 1;
	m_activeEdges = (m_activeEdges & maskLower) + ((m_activeEdges >> 1U) & ~maskLower);
	m_edgePositive = (m_edgePositive & maskLower) + ((m_edgePositive >> 1U) & ~maskLower);
	m_slideOff = (m_slideOff & maskLower) + ((m_slideOff >> 1U) & ~maskLower);
	m_numEdges--;
}

bool Rope2::EdgePoint::AddEdge(U16 edgeIndex, bool active)
{
	I32F iExisting = FindEdge(edgeIndex);
	if (iExisting >= 0)
	{
		if (GetEdgeActive(iExisting))
		{
			ASSERT(false); // we should never add existing active edge again
			return false; // already was active, no change
		}
		SetEdgeActive(iExisting, active);
		return true;
	}

	// @@JS: replace inactive!
	if (m_numEdges == Rope2::EdgePoint::kMaxNumEdges)
	{
		JAROS_ASSERT(false);
		return false;
	}
	m_edgeIndices[m_numEdges] = edgeIndex;
	SetEdgeActive(m_numEdges, active);
	m_numEdges++;
	m_numNewEdges++;
	return true;
}

bool Rope2::EdgePoint::CopyEdge(const Rope2::EdgePoint& otherEPoint, U16 index)
{
	U32F edgeIndex = otherEPoint.m_edgeIndices[index];
	I32F iExisting = FindEdge(edgeIndex);
	if (iExisting >= 0)
	{
		JAROS_ASSERT(!GetEdgeActive(iExisting)); // we should never add existing active edge again
		return true;
	}

	// @@JS: replace inactive!
	if (m_numEdges == Rope2::EdgePoint::kMaxNumEdges)
	{
		JAROS_ASSERT(false);
		return false;
	}
	m_edgeIndices[m_numEdges] = edgeIndex;
	SetEdgeActive(m_numEdges, otherEPoint.GetEdgeActive(index));
	SetEdgePositive(m_numEdges, otherEPoint.GetEdgePositive(index));
	m_numEdges++;
	return true;
}

I32F Rope2::EdgePoint::FindEdge(U32F edgeIndex)
{
	for (U32F ii = 0; ii<m_numEdges; ii++)
	{
		if (m_edgeIndices[ii] == edgeIndex)
		{
			return ii;
		}
	}
	return -1;
}

bool CheckForEdge(const Rope2::EdgePoint& ePoint, U16 edgeIndex)
{
	U32F ii = 0;
	while (ii < ePoint.m_numEdges)
	{
		if (ePoint.m_edgeIndices[ii] == edgeIndex)
		{
			return true;
		}
		ii++;
	}
	return false;
}

bool CheckForActiveEdge(const Rope2::EdgePoint& ePoint, U16 edgeIndex)
{
	U32F ii = 0;
	while (ii < ePoint.m_numEdges)
	{
		if (ePoint.m_edgeIndices[ii] == edgeIndex)
		{
			return ePoint.GetEdgeActive(ii);
		}
		ii++;
	}
	return false;
}

bool CheckForEdgeRemoveInactive(Rope2::EdgePoint& ePoint, U16 edgeIndex)
{
	U32F ii = 0;
	while (ii < ePoint.m_numEdges)
	{
		if (ePoint.m_edgeIndices[ii] == edgeIndex)
		{
			if (ePoint.GetEdgeActive(ii))
				return true;
			ePoint.RemoveEdge(ii);
			return false;
		}
		ii++;
	}
	return false;
}

void ValidateEdgesIndices(Rope2::EdgePoint& ePoint, U32F numEdgeIndices)
{
	return;

	U32F iPointEdge = 0;
	while (iPointEdge < ePoint.m_numEdges)
	{
		if (ePoint.m_edgeIndices[iPointEdge] >= numEdgeIndices)
		{
			ASSERT(false);
			ePoint.RemoveEdge(iPointEdge);
		}
		else
			iPointEdge++;
	}
}

#if 0

struct RopeColTri
{
	Point m_a;
	Point m_b;
	Point m_c;
	Vector m_norm;
	U32F m_key;
};

struct RopeColEdge
{
	Point m_a;
	Point m_b;
	Point m_walk;
	I32F m_edgeIndex;
	Scalar m_aPlaneDist;
	Scalar m_bPlaneDist;
};

bool WalkAdjacentTriangle(const hkpShapeContainer* pShape, const HavokTriAdjacencyInfo& adjInfo, Vec4_arg walkPlane, Point_arg targetPnt, 
	RopeColTri& walkTri, RopeColEdge& walkEdge)
{
	U32F triKey;
	if (!adjInfo.GetAdjacentTriangle(walkTri.m_key, walkEdge.m_edgeIndex, triKey))
	{
		return false;
	}

	hkpShapeBuffer shapeBufferHk;
	const hkpTriangleShape* pTriShape = static_cast<const hkpTriangleShape*>(pShape->getChildShape(triKey, shapeBufferHk));
	ASSERT(pTriShape);
	ASSERT(pTriShape->getType() == HK_SHAPE_TRIANGLE);

	U32F prevTriKey = walkTri.m_key;
	walkTri.m_a = pTriShape->m_vertexA.getQuad();
	walkTri.m_b = pTriShape->m_vertexB.getQuad();
	walkTri.m_c = pTriShape->m_vertexC.getQuad();
	walkTri.m_key = triKey;

	Vector sideA = walkTri.m_b - walkTri.m_a;
	Vector sideB = walkTri.m_c - walkTri.m_b;
	walkTri.m_norm = Normalize(Cross(sideA, sideB));

	if (Dot(targetPnt - walkTri.m_a, walkTri.m_norm) >= 0.0f)
	{
		walkEdge.m_edgeIndex = -1;
		return true;
	}

	Scalar aPlaneDist = Dot4(walkTri.m_a, walkPlane);
	Scalar bPlaneDist = Dot4(walkTri.m_b, walkPlane);
	Scalar cPlaneDist = Dot4(walkTri.m_c, walkPlane);
	if (aPlaneDist * bPlaneDist <= 0.0f)
	{
		U32F adjEdgeTriKey;
		adjInfo.GetAdjacentTriangle(triKey, 0, adjEdgeTriKey);
		if (adjEdgeTriKey != prevTriKey)
		{
			// Workout the interesection of sideA with walk plane
			Vector sideADir = Normalize(sideA);
			Scalar cosAlfa = Dot(sideADir, Vector(walkPlane));
			Point sideAWalk = walkTri.m_a + (cosAlfa == 0.0f ? Vector(kZero) : (-aPlaneDist/cosAlfa * sideADir));
			//walkEdge.m_a = walkTri.m_a;
			walkEdge.m_edgeIndex = 0;
			walkEdge.m_walk = sideAWalk;
			return true;
		}
	}
	if (aPlaneDist * bPlaneDist <= 0.0f)
	{
		U32F adjEdgeTriKey;
		adjInfo.GetAdjacentTriangle(triKey, 0, adjEdgeTriKey);
		if (adjEdgeTriKey != prevTriKey)
		{
			// Workout the interesection of sideA with walk plane
			Vector sideADir = Normalize(sideA);
			Scalar cosAlfa = Dot(sideADir, Vector(walkPlane));
			Point sideAWalk = walkTri.m_a + (cosAlfa == 0.0f ? Vector(kZero) : (-aPlaneDist/cosAlfa * sideADir));
			//walkEdge.m_a = walkTri.m_a;
			walkEdge.m_edgeIndex = 0;
			walkEdge.m_walk = sideAWalk;
			return true;
		}
	}
}

#endif

#if ROPE_FREE_EDGES

bool Rope2::PointEdgeCheck(const Point& refPos0, const Point& refPos1, const Point& nextPos, const Constraint& nextConstr,  
	Point& edgePos, Constraint& edgeConstr)
{
	const Vec4 vecZero(kZero);

	Vec4 plane0 = edgeConstr.m_planes[0];
	Vector plane0Norm(plane0);

	if (nextConstr.IsOuterEdge())
	{
		// Next node is edge so use the plane out of the 2 that has normal closer
		Scalar dot0 = Dot3(nextConstr.m_planes[0], plane0);
		Scalar dot1 = Dot3(nextConstr.m_planes[1], plane0);
		edgeConstr.CopyPlanes(1, nextConstr, dot0 > dot1 ? 0 : 1);
		edgeConstr.DetectOuterEdge(refPos0, m_fRadius);
		if (!edgeConstr.IsOuterEdge() && nextConstr.IsOuterEdge2())
		{
			// Try the 2nd edge
			Scalar dot2 = Dot3(nextConstr.m_planes[2], plane0);
			Scalar dot3 = Dot3(nextConstr.m_planes[3], plane0);
			edgeConstr.CopyPlanes(1, nextConstr, dot2 > dot3 ? 2 : 3);
		}
	}
	else
	{
		// Try each plane
		for (U32F ii = 0; ii<nextConstr.m_numPlanes; ii++)
		{
			if (!AllComponentsEqual(nextConstr.m_planes[ii], vecZero))
			{
				edgeConstr.CopyPlanes(1, nextConstr, ii);
				edgeConstr.DetectOuterEdge(refPos0, m_fRadius);
				if (edgeConstr.IsOuterEdge())
					break;
			}
		}

		if (!edgeConstr.IsOuterEdge())
		{
			// We will pretend as if the the other plane of the edge is going through the next rope point 
			Vector plane1Norm = nextPos - refPos0;
			plane1Norm -= Dot(plane1Norm, plane0Norm) * plane0Norm;
			Scalar len;
			plane1Norm = SafeNormalize(plane1Norm, kZero, len);
			if (len == 0.0f)
			{
				// @@ Fixup our edgeConstr
				edgePos = refPos0;
				return true;
			}
			edgeConstr.m_planes[1] = SMath::Vec4(plane1Norm.QuadwordValue());
			edgeConstr.m_planes[1].SetW(Dot(plane1Norm, Point(kZero)-nextPos));
			edgeConstr.m_flags |= Constraint::kIsOuterEdge;
		}
	}

	if (edgeConstr.IsOuterEdge())
	{
		Point refPos1Relaxed = refPos1;
		SMath::Vec4 resid1 = edgeConstr.RelaxPos(refPos1Relaxed, m_fRadius);
		if (!AllComponentsEqual(resid1, vecZero))
		{
			// We collide with this edge, now choose new pos so that we are allowing the pos to slide towards the edge
			Point refPosTest0 = refPos0;
			Point refPosTest1 = refPos1Relaxed;
			Vec4 residTest0 = edgeConstr.RelaxPosIgnoreEdges(refPosTest0);
			Vec4 residTest1 = edgeConstr.RelaxPosIgnoreEdges(refPosTest1);
			edgePos = Length3Sqr(residTest0) < Length3Sqr(residTest1) ? refPos0 : refPos1Relaxed;
			return true;
		}
	}

	return false;
}

bool Rope2::FindClosestStrainedEdgeCollision(const Point& startPos, F32 startRopeDist, const Point& endPos, F32 endRopeDist, U32F iStart, U32F iEnd,
	Point& edgePos, F32& edgeRopeDist, Constraint& edgeConstr)
{
	if (g_ropeMgr.m_disableStrainedCollisions)
	{
		return false;
	}

	const VF32 startPosXYZ1 = MakeXYZ1(startPos.QuadwordValue());
	const Vec4 vecZero(kZero);
	Scalar scTreshold(-0.001f);

	I32F iStep = endRopeDist > startRopeDist ? 1 : -1;
	I32F iTest;
	for (iTest = iStart; iTest!=iEnd+iStep; iTest += iStep)
	{
		// Current pos
		Point refPos0 = m_pPos[iTest];
		// Pos that we would get if we allow it to go straight
		const Point nextPos = iTest == iEnd ? endPos : m_pPos[iTest+iStep];
		const F32 nextRopeDist = iTest == iEnd ? endRopeDist : m_pRopeDist[iTest+iStep];
		Point refPos1 = Lerp( startPos, nextPos, (m_pRopeDist[iTest]-startRopeDist)/(nextRopeDist-startRopeDist));

		Constraint edgeConstr2;
		memset(&edgeConstr, 0, sizeof(Constraint));
		memset(&edgeConstr2, 0, sizeof(Constraint));
		Constraint* pCurConstr = &edgeConstr;
		U32F numConstr = 0;

		U32F iPlane = 0;
		if (m_pConstr[iTest].IsOuterEdge())
		{
			// We are on the edge see if we collide with it
			// Workout the interesection of the test line with first plane of the edge and check if that point is behind the second plane
			Vector testDir = SafeNormalize(nextPos - startPos, kZero);
			Scalar cosAlfa = Dot(testDir, Vector(m_pConstr[iTest].m_planes[0]));
			Scalar d(FastDot(startPosXYZ1, m_pConstr[iTest].m_planes[0].QuadwordValue()));
			Point refPos2 = startPos + (cosAlfa == 0.0f ? Vector(kZero) : (-d/cosAlfa * testDir));
			Scalar height2 = Dot4(refPos2, m_pConstr[iTest].m_planes[1]);
			if (height2 < scTreshold)
			{
				// We collide with this edge, now choose new pos so that we are allowing the pos to slide towards the edge
				pCurConstr->CopyPlanes(0, m_pConstr[iTest], 0);
				pCurConstr->CopyPlanes(1, m_pConstr[iTest], 1);
				pCurConstr->m_flags = Constraint::kIsOuterEdge;
				pCurConstr->RelaxPos(refPos1, m_fRadius);
				Point refPosTest0 = refPos0;
				Point refPosTest1 = refPos1;
				// This should give us the residual in respect to the other plane 
				Vec4 residTest0 = pCurConstr->RelaxPosIgnoreEdges(refPosTest0);
				Vec4 residTest1 = pCurConstr->RelaxPosIgnoreEdges(refPosTest1);
				// Choose the refPos0 unless refPos1 is *ON* the edge and if it has smaller residual to the other plane than refPos1
				refPos0 = Length3Sqr(residTest1) > m_fRadius*m_fRadius && Length3Sqr(residTest0) < Length3Sqr(residTest1) ? refPos0 : refPos1;
				PHYSICS_ASSERT(IsFinite(refPos0));

				pCurConstr = &edgeConstr2;
				numConstr++;
			}

			iPlane = 2;
		}

		if (m_pConstr[iTest].IsOuterEdge2())
		{
			// We are on the edge see if we collide with it
			// Workout the interesection of the test line with first plane of the edge and check if that point is behind the second plane
			Vector testDir = SafeNormalize(nextPos - startPos, kZero);
			Scalar cosAlfa = Dot(testDir, Vector(m_pConstr[iTest].m_planes[2]));
			Scalar d(FastDot(startPosXYZ1, m_pConstr[iTest].m_planes[2].QuadwordValue()));
			Point refPos2 = startPos + (cosAlfa == 0.0f ? Vector(kZero) : (-d/cosAlfa * testDir));
			Scalar height2 = Dot4(refPos2, m_pConstr[iTest].m_planes[3]);
			if (height2 < scTreshold)
			{
				// We collide with this edge, now choose new pos so that we are allowing the pos to slide towards the edge
				pCurConstr->CopyPlanes(0, m_pConstr[iTest], 2);
				pCurConstr->CopyPlanes(1, m_pConstr[iTest], 3);
				pCurConstr->m_flags = Constraint::kIsOuterEdge;
				pCurConstr->RelaxPos(refPos1, m_fRadius);
				Point refPosTest0 = refPos0;
				Point refPosTest1 = refPos1;
				// This should give us the residual in respect to the other plane 
				Vec4 residTest0 = pCurConstr->RelaxPosIgnoreEdges(refPosTest0);
				Vec4 residTest1 = pCurConstr->RelaxPosIgnoreEdges(refPosTest1);
				// Choose the refPos0 unless refPos1 is *ON* the edge and if it has smaller residual to the other plane than refPos1
				refPos0 = Length3Sqr(residTest1) > m_fRadius*m_fRadius && Length3Sqr(residTest0) < Length3Sqr(residTest1) ? refPos0 : refPos1;
				PHYSICS_ASSERT(IsFinite(refPos0));

				pCurConstr = &edgeConstr2;
				numConstr++;
			}

			iPlane = 4;
		}

		for (; iPlane < m_pConstr[iTest].m_numPlanes; iPlane++)
		{
			memset(pCurConstr, 0, sizeof(Constraint));

			SMath::Vec4 plane = m_pConstr[iTest].m_planes[iPlane];
			if (!AllComponentsEqual(plane, vecZero))
			{
				// Colliding with a plane
				pCurConstr->CopyPlanes(0, m_pConstr[iTest], iPlane);

				// Check if rope is coming from behind the plane ...
				Scalar height(FastDot(startPosXYZ1, plane.QuadwordValue()));
				if (height < scTreshold)
				{
					// ... it does so there must be some edge
					Constraint prevConstr;
					if (iTest == iStart && startRopeDist != m_pRopeDist[iTest-iStep])
					{
						memset(&prevConstr, 0, sizeof(prevConstr));
					}
					else
					{
						prevConstr = m_pConstr[iTest-iStep];
					}

					F32 prevRopeDist = iTest == iStart ? startRopeDist : m_pRopeDist[iTest-iStep];
					Point prevPos = Lerp( startPos, refPos0, (prevRopeDist-startRopeDist)/(m_pRopeDist[iTest]-startRopeDist));
					if (PointEdgeCheck(refPos0, refPos1, prevPos, prevConstr, edgePos, *pCurConstr))
					{
						numConstr++;
						if (numConstr == 2)
							break;
						pCurConstr = &edgeConstr2;
						refPos0 = edgePos;
					}
				}

				// Check if rope is going behind the plane ...
				Scalar height2(FastDot(MakeXYZ1(nextPos.QuadwordValue()), plane.QuadwordValue()));
				if (height2 < scTreshold)
				{
					// ... it does so there must be some edge
					Constraint nextConstr;
					if (iTest == iEnd && endRopeDist != m_pRopeDist[iTest+iStep])
					{
						memset(&nextConstr, 0, sizeof(nextConstr));
					}
					else
					{
						nextConstr = m_pConstr[iTest+iStep];
					}

					if (PointEdgeCheck(refPos0, refPos1, nextPos, nextConstr, edgePos, *pCurConstr))
					{
						numConstr++;
						if (numConstr == 2)
							break;
						pCurConstr = &edgeConstr2;
						refPos0 = edgePos;
					}
				}
			}
		}

		if (numConstr)
		{
			edgePos = refPos0;
			edgeRopeDist = m_pRopeDist[iTest];
			edgeConstr.m_numPlanes = 2;
			if (numConstr == 2)
			{
				// Merge the two edges
				edgeConstr.CopyPlanes(2, edgeConstr2, 0);
				edgeConstr.CopyPlanes(3, edgeConstr2, 1);
				edgeConstr.DetectOuterEdge(refPos0, m_fRadius); // detect again in case one edge invalidates the other
			}
			return true;
		}
	}

	return false;
}

#endif

void Rope2::MergeEdgePoints(EdgePoint* pEdges, U32F& numEdges, U32F iFirst, U32F iSecond)
{
	ASSERT(iSecond > iFirst && iSecond < numEdges);

	EdgePoint& eFirst = pEdges[iFirst];
	EdgePoint& eSecond = pEdges[iSecond];

	//ASSERT(eFirst.m_flags == eSecond.m_flags);
	ALWAYS_ASSERT((eFirst.m_flags & kNodeKeyframed) == 0 && (eSecond.m_flags & kNodeKeyframed) == 0);

	for (U32F ii = 0; ii<eSecond.m_numEdges; ii++)
	{
		bool alreadyIn = false;
		for (U32F jj = 0; jj<eFirst.m_numEdges; jj++)
		{
			if (eFirst.m_edgeIndices[jj] == eSecond.m_edgeIndices[ii])
			{
				alreadyIn = true;
				break;
			}
		}
		if (alreadyIn)
			continue;

		ASSERT(eFirst.m_numEdges < EdgePoint::kMaxNumEdges);
		if (eFirst.m_numEdges == EdgePoint::kMaxNumEdges)
			break;

		U32F iEdge = eFirst.m_numEdges;
		eFirst.m_edgeIndices[iEdge] = eSecond.m_edgeIndices[ii];
		eFirst.m_numEdges++;
		eFirst.SetEdgeActive(iEdge, eSecond.GetEdgeActive(ii));
		eFirst.SetEdgePositive(iEdge, eSecond.GetEdgePositive(ii));
	}

	U32F diff = iSecond - iFirst;
	memmove(pEdges+iFirst+1, pEdges+iFirst+1+diff, (numEdges-iSecond-1)*sizeof(pEdges[0]));
	numEdges -= diff;
}

void Rope2::CheckDuplicateNeighborEdges(EdgePoint& ePoint1, EdgePoint& ePoint2)
{
	U32F i1 = 0;
	while (i1<ePoint1.m_numEdges)
	{
		U32F i2 = 0;
		while (i2<ePoint2.m_numEdges)
		{
			if (ePoint1.m_edgeIndices[i1] == ePoint2.m_edgeIndices[i2])
			{
				bool active1 = ePoint1.GetEdgeActive(i1);
				bool active2 = ePoint2.GetEdgeActive(i2);
				if (active1)
				{
					ePoint2.RemoveEdge(i2);
				}
				else if (active2)
				{
					ePoint1.RemoveEdge(i1);
				}
				else
				{
					// If they are both inactive CheckEdgeDistances should take care of the case
				}
			}
			i2++;
		}
		i1++;
	}
}

bool Rope2::CheckMergeEdgePoints(U32F& numEdges, EdgePoint* pEdges)
{
	if (numEdges < 2)
		return false;

	U32F ii = 1;
	bool bMerged = false;
	while (ii<numEdges-2)
	{
		if (DistSqr(pEdges[ii].m_pos, pEdges[ii+1].m_pos) < kScEdgeTol2)
		{
			MergeEdgePoints(pEdges, numEdges, ii, ii+1);
			if (ii > 1)
				CheckDuplicateNeighborEdges(pEdges[ii-1], pEdges[ii]);
			if (ii < numEdges-2)
				CheckDuplicateNeighborEdges(pEdges[ii], pEdges[ii+1]);
			bMerged = true;
		}
		//if (AllComponentsEqual(pEdges[ii].m_pos, pEdges[ii+1].m_pos))
		//{
		//	MergeEdgePoints(pEdges, numEdges, ii, ii+1);
		//	bMerged = true;
		//}
		//else if ((pEdges[ii].m_internFlags & kNodeSplit) && DistSqr(pEdges[ii].m_pos, pEdges[ii+1].m_pos) < kScEdgeTol2)
		//{
		//	MergeEdgePoints(pEdges, numEdges, ii, ii+1);
		//	bMerged = true;
		//}
		else
		{
			ii++;
		}
		pEdges[ii].m_internFlags &= ~kNodeSplit;
	}
	return bMerged;
}

bool Rope2::CheckEdgeDistances(U32F& numEdges, EdgePoint* pEdges)
{
	bool bChanged = false;
	for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		EdgePoint& ePoint = pEdges[ii];
		U32F iPointEdge = 0;
		U32 numPointEdges = 0;
		while (iPointEdge < ePoint.m_numEdges)
		{
			// @@JS: Don't remove edge that makes outer corner with another that we're keeping
			// @@JS: This test can be done more efficiently
			// See segment-util.cpp
			const RopeColEdge* pEdge = &m_colCache.m_pEdges[ePoint.m_edgeIndices[iPointEdge]];
			Point prevPnt = pEdges[ii-1].m_pos;
			Point nextPnt = pEdges[ii+1].m_pos;
			Scalar prevLen;
			Vector prevDir = Normalize(ePoint.m_pos - prevPnt, prevLen);
			Scalar nextLen;
			Vector nextDir = Normalize(nextPnt - ePoint.m_pos, nextLen);
			Scalar dist, sPrev, sNext, t;
			bool bRemoved = false;
			bool bPrevClose = SegSegDist(prevPnt, prevDir, prevLen, pEdge->m_pnt, Vector(pEdge->m_vec), pEdge->m_vec.W(), 2.0f*kScEdgeTol, dist, sPrev, t);
			bool bNextClose = SegSegDist(ePoint.m_pos, nextDir, nextLen, pEdge->m_pnt, Vector(pEdge->m_vec), pEdge->m_vec.W(), 2.0f*kScEdgeTol, dist, sNext, t);
			if (!bPrevClose && !bNextClose)
			{
				bChanged = bChanged || ePoint.GetEdgeActive(iPointEdge);
				bRemoved = true;
				ePoint.RemoveEdge(iPointEdge);
			}
			else
			{
				if (!ePoint.GetEdgeActive(iPointEdge))
				{
					// @@JS: if inactive edge is close to the segment but far from the edge point should we create a new edge point for it or simply remove it?
					// Migrate inactive edge to neighbor
					if (bPrevClose && ii-1 > 0 && sPrev < 0.5f * prevLen)
					{
						if (ii-1 <= 1 || pEdges[ii-2].FindEdge(ePoint.m_edgeIndices[iPointEdge]) < 0)
						{
							// Only copy edge if the prev prev ePoint does not have it already
							// If it has it active we would discard it and it has it inactive we already checked that prev ePoint is not closer so we would discard it also
							pEdges[ii-1].CopyEdge(ePoint, iPointEdge);
						}
						ePoint.RemoveEdge(iPointEdge);
						bRemoved = true;
					}
					else if (bNextClose && ii+1 < numEdges-1 && sNext > 0.5f * nextLen)
					{
						if (ii+1 >= numEdges-2 || pEdges[ii+2].FindEdge(ePoint.m_edgeIndices[iPointEdge]) < 0)
						{
							// Only copy edge if the next next ePoint does not have it already
							pEdges[ii+1].CopyEdge(ePoint, iPointEdge);
						}
						ePoint.RemoveEdge(iPointEdge);
						bRemoved = true;
					}
				}
			}
			if (!bRemoved)
				iPointEdge++;
		}
	}
	return bChanged;
}

void Rope2::InsertIntermediateEdgePoints(U32 iKey, const I32F* pOldEdgeIndex, const bool* pOldEdgeIndexExact, EdgePoint* pEdges, U32& numEdges, U32& iOldSim)
{
	{
		// Insert all intermediate edge points
		U32F iOldEdge = pOldEdgeIndex[iKey-1];
		if (pOldEdgeIndexExact[iKey-1])
			iOldEdge++;
		for (; iOldEdge<pOldEdgeIndex[iKey]; iOldEdge++)
		{
			if (numEdges == m_maxNumPoints-1)
			{
				JAROS_ALWAYS_ASSERT(false);
				break;
			}
			pEdges[numEdges] = m_pEdges[iOldEdge];
			pEdges[numEdges].m_flags = 0;
			numEdges++;
		}
	}

	// Insert slack points if we're past all old edges
	if (pOldEdgeIndex[iKey] == m_numEdges)
	{
		F32 ropeDist = pEdges[numEdges-1].m_ropeDist;
		while (iOldSim < m_numPoints && m_pRopeDist[iOldSim] <= ropeDist)
		{
			iOldSim++;
		}
		I32 iLastSlack = -1;
		while (iOldSim < m_numPoints && m_pRopeDist[iOldSim] < m_pKeyRopeDist[iKey])
		{
			Point conEdgePos;
			F32 conEdgeT;
			if (numEdges < m_maxNumPoints && m_pConstr[iOldSim-1].GetLineSegmentEdgePoint(pEdges[numEdges-1].m_pos, m_pPos[iOldSim], conEdgePos, conEdgeT))
			{
				pEdges[numEdges].Reset();
				pEdges[numEdges].m_pos = conEdgePos;
				pEdges[numEdges].m_ropeDist = Lerp(pEdges[numEdges-1].m_ropeDist, m_pRopeDist[iOldSim], conEdgeT);
				numEdges++;
			}
			if (numEdges < m_maxNumPoints && m_pConstr[iOldSim].GetLineSegmentEdgePoint(pEdges[numEdges-1].m_pos, m_pPos[iOldSim], conEdgePos, conEdgeT))
			{
				pEdges[numEdges].Reset();
				pEdges[numEdges].m_pos = conEdgePos;
				pEdges[numEdges].m_ropeDist = Lerp(pEdges[numEdges-1].m_ropeDist, m_pRopeDist[iOldSim], conEdgeT);
				numEdges++;
			}
			if (numEdges < m_maxNumPoints)
			{
				pEdges[numEdges].Reset();
				pEdges[numEdges].m_pos = m_pPos[iOldSim];
				pEdges[numEdges].m_ropeDist = m_pRopeDist[iOldSim];
				numEdges++;
				iLastSlack = iOldSim;
			}
			iOldSim++;
		}

		// @@JS: Check the last rope segment for constraint edge point?
		/*if (iLastSlack >= 0 && numEdges < m_maxNumPoints)
		{
			Point conEdgePos;
			F32 conEdgeT;
			if (m_pConstr[iLastSlack].GetLineSegmentEdgePoint(pEdges[numEdges-1].m_pos, m_pPos[iOldSim], conEdgePos, conEdgeT))
			{
				pEdges[numEdges].Reset();
				pEdges[numEdges].m_pos = conEdgePos;
				pEdges[numEdges].m_ropeDist = Lerp(pEdges[numEdges-1].m_ropeDist, m_pRopeDist[iOldSim], conEdgeT);
				pEdges[numEdges].m_flags = kNodeStrained;
				numEdges++;
			}
		}*/
	}
}

void Rope2::InsertIntermediateEdgePoints(U32 iPrevKey, F32 ropeDist, const I32F* pOldEdgeIndex, const bool* pOldEdgeIndexExact, EdgePoint* pEdges, U32& numEdges, U32& iOldSim)
{
	// Insert all intermediate edge points
	U32F iOldEdge = pOldEdgeIndex[iPrevKey];
	if (pOldEdgeIndexExact[iPrevKey])
		iOldEdge++;
	for (; iOldEdge<m_numEdges; iOldEdge++)
	{
		if (numEdges == m_maxNumPoints-1)
		{
			JAROS_ALWAYS_ASSERT(false);
			break;
		}
		if (m_pEdges[iOldEdge].m_ropeDist > ropeDist)
		{
			break;
		}
		pEdges[numEdges] = m_pEdges[iOldEdge];
		if (pEdges[numEdges].m_flags & kNodeKeyframed)
		{
			pEdges[numEdges].m_flags = 0;
		}
		numEdges++;
	}

	// Insert slack points if we're past all old edges
	if (iOldEdge == m_numEdges)
	{
		F32 lastEdgeRopeDist = pEdges[numEdges-1].m_ropeDist;
		while (iOldSim < m_numPoints && m_pRopeDist[iOldSim] <= lastEdgeRopeDist)
		{
			iOldSim++;
		}
		I32 iLastSlack = -1;
		while (iOldSim < m_numPoints && m_pRopeDist[iOldSim] < ropeDist)
		{
			Point conEdgePos;
			F32 conEdgeT;
			if (numEdges < m_maxNumPoints && m_pConstr[iOldSim-1].GetLineSegmentEdgePoint(pEdges[numEdges-1].m_pos, m_pPos[iOldSim], conEdgePos, conEdgeT))
			{
				pEdges[numEdges].Reset();
				pEdges[numEdges].m_pos = conEdgePos;
				pEdges[numEdges].m_ropeDist = Lerp(pEdges[numEdges-1].m_ropeDist, m_pRopeDist[iOldSim], conEdgeT);
				numEdges++;
			}
			if (numEdges < m_maxNumPoints && m_pConstr[iOldSim].GetLineSegmentEdgePoint(pEdges[numEdges-1].m_pos, m_pPos[iOldSim], conEdgePos, conEdgeT))
			{
				pEdges[numEdges].Reset();
				pEdges[numEdges].m_pos = conEdgePos;
				pEdges[numEdges].m_ropeDist = Lerp(pEdges[numEdges-1].m_ropeDist, m_pRopeDist[iOldSim], conEdgeT);
				numEdges++;
			}
			if (numEdges < m_maxNumPoints)
			{
				pEdges[numEdges].Reset();
				pEdges[numEdges].m_pos = m_pPos[iOldSim];
				pEdges[numEdges].m_ropeDist = m_pRopeDist[iOldSim];
				numEdges++;
				iLastSlack = iOldSim;
			}
			iOldSim++;
		}

		// @@JS: Check the last rope segment for constraint edge point?
		/*if (iLastSlack >= 0 && numEdges < m_maxNumPoints)
		{
			Point conEdgePos;
			F32 conEdgeT;
			if (m_pConstr[iLastSlack].GetLineSegmentEdgePoint(pEdges[numEdges-1].m_pos, m_pPos[iOldSim], conEdgePos, conEdgeT))
			{
				pEdges[numEdges].Reset();
				pEdges[numEdges].m_pos = conEdgePos;
				pEdges[numEdges].m_ropeDist = Lerp(pEdges[numEdges-1].m_ropeDist, m_pRopeDist[iOldSim], conEdgeT);
				pEdges[numEdges].m_flags = kNodeStrained;
				numEdges++;
			}
		}*/
	}
}

void RemoveNonKeyframedEdgePoint(Rope2::EdgePoint* pEdges, U32F& numEdges)
{
	for (I32F ii = numEdges-1; ii>0; ii--)
	{
		if ((pEdges[ii].m_flags & Rope2::kNodeKeyframed) == 0)
		{
			memcpy(pEdges+ii, pEdges+ii+1, (numEdges-ii-1)*sizeof(pEdges[0]));
			numEdges--;
			return;
		}
	}
	ALWAYS_ASSERT(false); // There are really a lot of keyframed points here :)
}

void Rope2::StrainedEdgeDetection()
{
	if (m_numKeyPoints == 0)
	{
		m_numEdges = 0;
		return;
	}

	PROFILE(Rope, StrainedEdgeDetection);

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	// Auto strained
	if (m_bAutoStrained)
	{
		for (U32F ii = 1; ii<m_numKeyPoints; ii++)
		{
			if (Dist(m_pKeyPos[ii-1], m_pKeyPos[ii]) > m_pKeyRopeDist[ii] - m_pKeyRopeDist[ii-1])
			{
				m_pKeyNodeFlags[ii] |= kNodeStrained;
			}
		}
	}

	// First for all keyframed points find corresponding edge points from previous frame
	I32F* pOldEdgeIndex = NDI_NEW I32F[m_numKeyPoints];
	bool* pOldEdgeIndexExact = NDI_NEW bool[m_numKeyPoints];
	I32F lastOldEdgeIndex = -1;
	for (U32F ii = 0; ii<m_numKeyPoints; ii++)
	{
		pOldEdgeIndex[ii] = -1;
		RopeNodeFlags userMark = (m_pKeyNodeFlags[ii] & kNodeUserMarks);
		if (userMark)
		{
			for (I32F iOldEdge = 0; iOldEdge<m_numEdges; iOldEdge++)
			{
				if ((m_pEdges[iOldEdge].m_flags & kNodeUserMarks) == userMark)
				{
					PHYSICS_ASSERT(pOldEdgeIndex[ii] == -1); // the same user mark used multiple times?
					PHYSICS_ASSERT(iOldEdge > lastOldEdgeIndex); // the user marks switched order compared to previous frame?
					pOldEdgeIndex[ii] = iOldEdge;
					pOldEdgeIndexExact[ii] = true;
					lastOldEdgeIndex = iOldEdge;
				}
			}
		}
	}

	// And corresponding 3D points from previous frame
	Point* pOldPoints = NDI_NEW Point[m_numKeyPoints];
	lastOldEdgeIndex = 0;
	for (U32F ii = 0; ii<m_numKeyPoints; ii++)
	{
		if (pOldEdgeIndex[ii] >= 0)
		{
			// Found by user mark
			pOldPoints[ii] = m_pEdges[pOldEdgeIndex[ii]].m_pos;
			lastOldEdgeIndex = pOldEdgeIndex[ii];
		}
		else
		{
			// If not found by user mark we will get edge starting position by rope dist but limited by the last and next marked edge points
			if (lastOldEdgeIndex < m_numEdges)
			{
				if (m_pKeyRopeDist[ii] <= m_pEdges[lastOldEdgeIndex].m_ropeDist)
				{
					pOldEdgeIndex[ii] = lastOldEdgeIndex;
					pOldEdgeIndexExact[ii] = true;
					pOldPoints[ii] = m_pEdges[lastOldEdgeIndex].m_pos;
				}
				else
				{
					U32F iNextMarkedKey = ii + 1;
					while (iNextMarkedKey < m_numKeyPoints && pOldEdgeIndex[iNextMarkedKey] < 0)
						iNextMarkedKey++;
					U32F nextOldEdgeIndex = iNextMarkedKey < m_numKeyPoints ? pOldEdgeIndex[iNextMarkedKey] : m_numEdges-1;
					for (U32F iOldEdge = lastOldEdgeIndex+1; iOldEdge<=nextOldEdgeIndex; iOldEdge++)
					{
						if (m_pKeyRopeDist[ii] <= m_pEdges[iOldEdge].m_ropeDist)
						{
							pOldEdgeIndex[ii] = iOldEdge;
							pOldEdgeIndexExact[ii] = m_pKeyRopeDist[ii] == m_pEdges[iOldEdge].m_ropeDist;
							lastOldEdgeIndex = pOldEdgeIndexExact[ii] ? iOldEdge : iOldEdge-1;
							pOldPoints[ii] = Lerp(m_pEdges[iOldEdge-1].m_pos, m_pEdges[iOldEdge].m_pos, (m_pKeyRopeDist[ii] - m_pEdges[iOldEdge-1].m_ropeDist) / (m_pEdges[iOldEdge].m_ropeDist - m_pEdges[iOldEdge-1].m_ropeDist));
							ASSERT(IsFinite(pOldPoints[ii]));
							break;
						}
					}
					if (pOldEdgeIndex[ii] < 0)
					{
						if (iNextMarkedKey < m_numKeyPoints)
						{
							// Hit next mark
							pOldEdgeIndex[ii] = nextOldEdgeIndex;
							lastOldEdgeIndex = nextOldEdgeIndex;
							pOldEdgeIndexExact[ii] = true;
							pOldPoints[ii] = m_pEdges[nextOldEdgeIndex].m_pos;
						}
						else
						{
							// We are past all old edges
							pOldEdgeIndex[ii] = m_numEdges;
							lastOldEdgeIndex = m_numEdges;
							pOldEdgeIndexExact[ii] = false;
							pOldPoints[ii] = GetPos(Max(m_pEdges[m_numEdges-1].m_ropeDist, m_pKeyRopeDist[ii]));
						}
					}
				}
			}
			else
			{
				// We are past all old edges
				pOldEdgeIndex[ii] = m_numEdges;
				pOldEdgeIndexExact[ii] = false;
				pOldPoints[ii] = GetPos(Max((m_numEdges > 0 ? m_pEdges[m_numEdges-1].m_ropeDist : 0.0f), m_pKeyRopeDist[ii]));
			}
		}
	}

	EdgePoint* pEdges = NDI_NEW EdgePoint[m_maxNumPoints];
	U32F numEdges = 0;

	pEdges[0].Reset();
	pEdges[0].m_pos = m_pKeyPos[0];
	pEdges[0].m_ropeDist = m_pKeyRopeDist[0];
	pEdges[0].m_flags = m_pKeyNodeFlags[0] & ~(kNodeStrained|kNodeUseSavePos);
	numEdges++;

	bool saveEdgesDone = false;
	U32 iOldSim = 0;

	for (U32F ii = 1; ii<m_numKeyPoints; ii++)
	{
		U32F iStart = numEdges-1;

		if (m_pKeyNodeFlags[ii] & kNodeUseSavePos)
		{
			PHYSICS_ASSERT(!saveEdgesDone); // kNodeUseSavePos flag used on 2 different keyframed points?
			PHYSICS_ASSERT(m_pSaveEdges); // bWithSavePos flag not used during init?
			PHYSICS_ASSERT(m_saveEdgePosSet); // saveEdgePos has not been set but kNodeUseSavePos has been used

			// If we have external save edges we use them as the starting save line
			bool useExternSaveEdges = m_saveStartEdgePosSet && m_numExternSaveEdges > 1;
			Point saveStartPos = !useExternSaveEdges && m_saveStartEdgePosSet ? m_saveStartEdgePos : m_pKeyPos[ii-1];

			U32F& useNumSaveEdges = m_numExternSaveEdges ? m_numExternSaveEdges : m_numSaveEdges;
			EdgePoint* pUseSaveEdges = m_numExternSaveEdges ? m_pExternSaveEdges : m_pSaveEdges;

			bool saveValid = useNumSaveEdges >= 2 && ((m_pKeyNodeFlags[ii] & pUseSaveEdges[useNumSaveEdges-1].m_flags) & kNodeUserMarks) != 0;
			if (!saveValid)
			{
				useNumSaveEdges = 0;
				pUseSaveEdges = m_pSaveEdges;

				pUseSaveEdges[0].Reset();
				pUseSaveEdges[0].m_pos = GetPos(m_pKeyRopeDist[ii-1]);
				pUseSaveEdges[0].m_ropeDist = m_pKeyRopeDist[ii-1];
				pUseSaveEdges[0].m_flags = m_pKeyNodeFlags[ii-1];
				useNumSaveEdges++;

				if (m_savePosRopeDistSet)
					InsertIntermediateEdgePoints(ii, m_savePosRopeDist, pOldEdgeIndex, pOldEdgeIndexExact, pUseSaveEdges, useNumSaveEdges, iOldSim);
				else
					InsertIntermediateEdgePoints(ii, pOldEdgeIndex, pOldEdgeIndexExact, pUseSaveEdges, useNumSaveEdges, iOldSim);

				pUseSaveEdges[useNumSaveEdges].Reset();
				pUseSaveEdges[useNumSaveEdges].m_pos = GetPos(m_pKeyRopeDist[ii]);
				useNumSaveEdges++;
			}

			pUseSaveEdges[0].m_ropeDist = m_pKeyRopeDist[ii-1];
			pUseSaveEdges[0].m_flags = m_pKeyNodeFlags[ii-1];
			pUseSaveEdges[useNumSaveEdges-1].m_ropeDist = m_pKeyRopeDist[ii];
			pUseSaveEdges[useNumSaveEdges-1].m_flags = m_pKeyNodeFlags[ii];

			{
				bool strained = m_pKeyNodeFlags[ii] & kNodeStrained;
				for (U32F iSaveEdge = 1; iSaveEdge<useNumSaveEdges-1; iSaveEdge++)
				{
					if (strained)
						pUseSaveEdges[iSaveEdge].m_flags |= kNodeStrained;
					else
						pUseSaveEdges[iSaveEdge].m_flags &= ~kNodeStrained;
					pUseSaveEdges[iSaveEdge].m_flags |= kNodeEdgeDetection|kNodeUseSavePos;
				}
			}

			if (!useExternSaveEdges)
			{
				StrainedEdgeDetectionFromSlacky(m_pSaveEdges, m_numSaveEdges, 0, saveStartPos, m_saveEdgePos, false);
				RedistributeStrainedEdges(m_pSaveEdges, m_numSaveEdges);
				JAROS_ALWAYS_ASSERT(m_numSaveEdges <= m_maxNumPoints);
			}
			else
			{
				RedistributeStrainedEdges(pUseSaveEdges, useNumSaveEdges);
			}

			saveEdgesDone = !useExternSaveEdges;

			if (pOldEdgeIndex[ii] == m_numEdges)
			{
				// If we don't have all the edges from previous frame, we will start with save edges
				numEdges--;
				I32F numCopyEdges = Min(useNumSaveEdges-1, m_maxNumPoints-numEdges-1);
				JAROS_ALWAYS_ASSERT(numCopyEdges >= 0);
				memcpy(pEdges+numEdges, pUseSaveEdges, numCopyEdges*sizeof(pEdges[0]));
				numEdges += numCopyEdges;
				JAROS_ALWAYS_ASSERT(numEdges < m_maxNumPoints);
				if (numEdges == m_maxNumPoints)
					RemoveNonKeyframedEdgePoint(pEdges, numEdges);
				pEdges[numEdges] = pUseSaveEdges[useNumSaveEdges-1];
				numEdges++;
				U32F iEnd = numEdges-1;
				U32F numEdgesSegment = iEnd - iStart + 1;
				StrainedEdgeDetectionFromSlacky(pEdges+iStart, numEdgesSegment, iStart, m_pKeyPos[ii-1], m_pKeyPos[ii], true);
				numEdges = iStart + numEdgesSegment;
				JAROS_ALWAYS_ASSERT(numEdges <= m_maxNumPoints);
			}
			else
			{
				// If we have all the edges from previous frame, we can just ignore save edges
				// This is necessary for the "static friction" to work consistently
				U32 iOldSimDummy = 0;
				InsertIntermediateEdgePoints(ii, pOldEdgeIndex, pOldEdgeIndexExact, pEdges, numEdges, iOldSimDummy);
				JAROS_ALWAYS_ASSERT(numEdges < m_maxNumPoints);
				if (numEdges == m_maxNumPoints)
					RemoveNonKeyframedEdgePoint(pEdges, numEdges);
				pEdges[numEdges].Reset();
				pEdges[numEdges].m_pos = pOldPoints[ii];
				pEdges[numEdges].m_ropeDist = m_pKeyRopeDist[ii];
				pEdges[numEdges].m_flags = m_pKeyNodeFlags[ii];
				numEdges++;

				bool strained = m_pKeyNodeFlags[ii] & kNodeStrained;
				for (U32F iEdge = 1; iEdge<numEdges-1; iEdge++)
				{
					if (strained)
						pEdges[iEdge].m_flags |= kNodeStrained;
					else
						pEdges[iEdge].m_flags &= ~kNodeStrained;
					pEdges[iEdge].m_flags |= kNodeEdgeDetection|kNodeUseSavePos;
				}

				U32F iEnd = numEdges-1;
				U32F numEdgesSegment = iEnd - iStart + 1;
				StrainedEdgeDetectionFromSlacky(pEdges+iStart, numEdgesSegment, iStart, m_pKeyPos[ii-1], m_pKeyPos[ii], true);
				numEdges = iStart + numEdgesSegment;
				JAROS_ALWAYS_ASSERT(numEdges <= m_maxNumPoints);
			}
		}
		else if ((m_pKeyNodeFlags[ii] & (kNodeStrained|kNodeEdgeDetection)) && (m_pKeyNodeFlags[ii] & kNodeNoEdgeDetection) == 0)
		{
			InsertIntermediateEdgePoints(ii, pOldEdgeIndex, pOldEdgeIndexExact, pEdges, numEdges, iOldSim);

			bool strained = m_pKeyNodeFlags[ii] & kNodeStrained;
			for (U32F iEdge = iStart+1; iEdge<numEdges; iEdge++)
			{
				if (strained)
					pEdges[iEdge].m_flags |= kNodeStrained;
				else
					pEdges[iEdge].m_flags &= ~kNodeStrained;
				pEdges[iEdge].m_flags |= kNodeEdgeDetection;
			}

			JAROS_ALWAYS_ASSERT(numEdges < m_maxNumPoints);
			if (numEdges == m_maxNumPoints)
				RemoveNonKeyframedEdgePoint(pEdges, numEdges);
			pEdges[numEdges].Reset();
			pEdges[numEdges].m_pos = m_pKeyPos[ii];
			pEdges[numEdges].m_ropeDist = m_pKeyRopeDist[ii];
			pEdges[numEdges].m_flags = m_pKeyNodeFlags[ii];
			numEdges++;
			ALWAYS_ASSERT(numEdges <= m_maxNumPoints);

			{
				U32F iEnd = numEdges-1;
				pEdges[iStart].m_pos = pOldPoints[ii-1];
				pEdges[iEnd].m_pos = pOldPoints[ii];
				U32F numEdgesSegment = iEnd - iStart + 1;
				StrainedEdgeDetectionFromSlacky(pEdges+iStart, numEdgesSegment, iStart, m_pKeyPos[ii-1], m_pKeyPos[ii], false);
				numEdges = iStart + numEdgesSegment;
				JAROS_ALWAYS_ASSERT(numEdges <= m_maxNumPoints);
			}
		}
		else
		{
			JAROS_ALWAYS_ASSERT(numEdges < m_maxNumPoints);
			if (numEdges == m_maxNumPoints)
				RemoveNonKeyframedEdgePoint(pEdges, numEdges);
			pEdges[numEdges].Reset();
			pEdges[numEdges].m_pos = m_pKeyPos[ii];
			pEdges[numEdges].m_ropeDist = m_pKeyRopeDist[ii];
			pEdges[numEdges].m_flags = m_pKeyNodeFlags[ii];
			numEdges++;
			ALWAYS_ASSERT(numEdges <= m_maxNumPoints);
		}
	}

	if (!saveEdgesDone)
	{
		// @@JS: m_saveStartEdgePos disconnected for now
		if (m_pSaveEdges && m_saveEdgePosSet && m_numKeyPoints > 0)
		{
			// Even though we're not using the save line we did specifically set the save position and want to keep this save line up-to-date

			bool saveValid = m_numSaveEdges >= 2;
			if (saveValid || (m_savePosRopeDistSet && m_pKeyRopeDist[0] <= m_savePosRopeDist))
			{
				I32 iPrevKey = m_numKeyPoints-1;
				if (m_savePosRopeDistSet)
				{
					PHYSICS_ASSERT(m_savePosRopeDist <= m_fLength);
					for (I32 ii = 1; ii<m_numKeyPoints; ii++)
					{
						if (m_pKeyRopeDist[ii] >= m_savePosRopeDist)
						{
							iPrevKey = ii-1;
							break;
						}
					}
				}

				if (!saveValid)
				{
					m_numSaveEdges = 0;

					m_pSaveEdges[0].Reset();
					m_pSaveEdges[0].m_pos = m_pKeyPos[iPrevKey];
					m_pSaveEdges[0].m_ropeDist = m_pKeyRopeDist[iPrevKey];
					m_pSaveEdges[0].m_flags = kNodeKeyframed;
					m_numSaveEdges++;

					iOldSim = 0;
					InsertIntermediateEdgePoints(iPrevKey, m_savePosRopeDist, pOldEdgeIndex, pOldEdgeIndexExact, m_pSaveEdges, m_numSaveEdges, iOldSim);

					for (U32F iEdge = 1; iEdge<m_numSaveEdges; iEdge++)
					{
						pEdges[iEdge].m_flags |= kNodeStrained|kNodeEdgeDetection;
					}

					m_pSaveEdges[m_numSaveEdges].Reset();
					m_pSaveEdges[m_numSaveEdges].m_pos = m_saveEdgePos;
					m_pSaveEdges[m_numSaveEdges].m_ropeDist = m_savePosRopeDist;
					m_pSaveEdges[m_numSaveEdges].m_flags = kNodeKeyframed|kNodeStrained|kNodeUserMark1; // the UserMark1 here is a total hack. That should be decided by user what mark to put on the end of the save line
					m_numSaveEdges++;
				}
				else
				{
					if (m_savePosRopeDistSet)
						m_pSaveEdges[m_numSaveEdges-1].m_ropeDist = m_savePosRopeDist;
				}

				StrainedEdgeDetectionFromSlacky(m_pSaveEdges, m_numSaveEdges, 0, m_pKeyPos[iPrevKey], m_saveEdgePos, false);
				RedistributeStrainedEdges(m_pSaveEdges, m_numSaveEdges);
				JAROS_ALWAYS_ASSERT(m_numSaveEdges <= m_maxNumPoints);

				saveEdgesDone = true;
			}
		}

		if (!saveEdgesDone)
		{
			m_numSaveEdges = 0;
		}
	}

	RedistributeStrainedEdges(pEdges, numEdges);

	memcpy(m_pEdges, pEdges, numEdges*sizeof(pEdges[0]));
	m_numEdges = numEdges;
}

void Rope2::StrainedEdgeDetectionFromSlacky(EdgePoint* pEdges, U32F& numEdges, U32F edgeBufOffset, Point_arg startTargetPos, Point_arg endTargetPos, bool skipStepMove)
{
	U32 saveLastStraightenSubsteps = m_lastStraightenSubsteps;
	m_lastStraightenSubsteps = 0;

	EdgePoint* pSecEdges = NDI_NEW EdgePoint[m_maxNumPoints];

	bool* pFixedSlackyNodes = NDI_NEW bool[m_maxNumPoints];
	U32 iSecStart = 0;
	U32 numSlackyEdges = 0;
	U32 numFixedSlackyNodes = 0;
	I32 ii = 1;
	while (ii<numEdges)
	{
		if (pEdges[ii].m_numEdges == 0)
		{
			numSlackyEdges++;
		}
		else
		{
			numSlackyEdges = 0;
		}
		if (numSlackyEdges >= 10 || ii == numEdges-1)
		{
			U32 numSecEdges = ii-iSecStart+1;
			memcpy(pSecEdges, pEdges+iSecStart, numSecEdges * sizeof(pSecEdges[0]));
			Point secStartTargetPos = iSecStart == 0 ? startTargetPos : pSecEdges[0].m_pos;
			Point secEndTargetPos = ii == numEdges-1 ? endTargetPos : pSecEdges[numSecEdges-1].m_pos;
			StrainedEdgeDetection(pSecEdges, numSecEdges, edgeBufOffset+iSecStart, secStartTargetPos, secEndTargetPos, skipStepMove);

			I32 numEdgeDiff = (I32)numSecEdges - (I32)(ii-iSecStart+1);
			memmove(pEdges+ii+1+numEdgeDiff, pEdges+ii+1, (numEdges-ii) * sizeof(pEdges[0]));
			memcpy(pEdges+iSecStart, pSecEdges, numSecEdges * sizeof(pEdges[0]));

			I32 iNew = ii + numEdgeDiff;
			for (; ii<iNew; ii++)
			{
				pFixedSlackyNodes[ii] = false;
			}
			ii = iNew;
			numEdges += numEdgeDiff;

			if (ii < numEdges-1)
			{
				pFixedSlackyNodes[ii] = true;
				numFixedSlackyNodes++;
				iSecStart = ii;
				numSlackyEdges = 0;
			}
		}
		else
		{
			pFixedSlackyNodes[ii] = false;
		}
		ii++;
	}

	if (g_ropeDebugSubStep > 0 && m_lastStraightenSubsteps >= g_ropeDebugSubStep)
	{
		if (numFixedSlackyNodes)
			m_lastStraightenSubsteps++;
		m_lastStraightenSubsteps = Max(m_lastStraightenSubsteps, saveLastStraightenSubsteps);
		return;
	}

	U32 accuSubsteps = 0;

	while (numFixedSlackyNodes)
	{
		if (g_ropeDebugSubStep > 0)
		{
			g_ropeDebugSubStep -= m_lastStraightenSubsteps;
		}
		accuSubsteps += m_lastStraightenSubsteps;
		m_lastStraightenSubsteps = 0;

		iSecStart = 0;
		numFixedSlackyNodes = 0;
		U32 numSkippedNodes = 0;
		ii = 1;
		while (ii<numEdges)
		{
			numSkippedNodes += pFixedSlackyNodes[ii] ? 1 : 0;
			if (numSkippedNodes == 2 || ii == numEdges-1)
			{
				U32 numSecEdges = ii-iSecStart+1;
				memcpy(pSecEdges, pEdges+iSecStart, numSecEdges * sizeof(pSecEdges[0]));
				Point secStartTargetPos = pSecEdges[0].m_pos;
				Point secEndTargetPos = pSecEdges[numSecEdges-1].m_pos;
				StrainedEdgeDetection(pSecEdges, numSecEdges, edgeBufOffset+iSecStart, secStartTargetPos, secEndTargetPos, true);

				I32 numEdgeDiff = (I32)numSecEdges - (I32)(ii-iSecStart+1);
				memmove(pEdges+ii+1+numEdgeDiff, pEdges+ii+1, (numEdges-ii) * sizeof(pEdges[0]));
				memcpy(pEdges+iSecStart, pSecEdges, numSecEdges * sizeof(pEdges[0]));

				I32 iNew = ii + numEdgeDiff;
				for (; ii<iNew; ii++)
				{
					pFixedSlackyNodes[ii] = false;
				}
				ii = iNew;
				numEdges += numEdgeDiff;

				if (ii < numEdges-1)
				{
					pFixedSlackyNodes[ii] = true;
					numFixedSlackyNodes++;
					iSecStart = ii;
					numSkippedNodes = 0;
				}
			}
			else
			{
				pFixedSlackyNodes[ii] = false;
			}
			ii++;
		}

		// We want to skip the first iter that really does nothing
		m_lastStraightenSubsteps--;

		if (g_ropeDebugSubStep > 0 && m_lastStraightenSubsteps >= g_ropeDebugSubStep)
		{
			if (numFixedSlackyNodes)
				m_lastStraightenSubsteps++;
			g_ropeDebugSubStep += accuSubsteps;
			m_lastStraightenSubsteps = Max(accuSubsteps+m_lastStraightenSubsteps, saveLastStraightenSubsteps);
			return;
		}
	}

	if (g_ropeDebugSubStep > 0)
	{
		g_ropeDebugSubStep += accuSubsteps;
	}
	m_lastStraightenSubsteps = Max(accuSubsteps+m_lastStraightenSubsteps, saveLastStraightenSubsteps);
}

void Rope2::StrainedEdgeDetection(EdgePoint* pEdges, U32F& numEdges, U32F edgeBufOffset, Point_arg startTargetPos, Point_arg endTargetPos, bool skipStepMove)
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);

	// If the pEdges buffer has some start offset we need to adjust the limit on max number of points so that we don't overflow
	U32F saveMaxNumPoints = m_maxNumPoints;
	m_maxNumPoints -= edgeBufOffset;

#if MOVING_EDGES2
	if (!skipStepMove)
	{
		StepMoveEdgesAndRopeEndPoints(pEdges, numEdges, startTargetPos, endTargetPos);
	}
#endif

	PROFILE(Rope, StrainedEdgeDetectionSection);

	Point* pNewPos = NDI_NEW Point[m_maxNumPoints];
	Point* pOldPos = NDI_NEW Point[m_maxNumPoints];

	bool bChanged;
	bool bStraightened;
	U32F numIter = 1;
	bool bInterrupt = false;
	do 
	{
		if (FALSE_IN_FINAL_BUILD(g_ropeDebugSubStep > 0 && numIter >= g_ropeDebugSubStep))
		{
			bInterrupt = true;
			break;
		}

#ifdef JSINECKY
		if (g_ropeDebugSubStep > 0 && numIter == g_ropeDebugSubStep-1)
		{
			printf("Break here\n");
		}
#endif

		bChanged = false;
		pNewPos[0] = startTargetPos;
		pNewPos[numEdges-1] = endTargetPos;
		bStraightened = StraightenAlongEdges(numEdges, pEdges, pNewPos);

		// Save old edge position
		for (U32F ii = 0; ii<numEdges; ii++)
		{
			pOldPos[ii] = pEdges[ii].m_pos;
		}

		bool bCollided = MoveStrainedCollideWithEdges(numEdges, pEdges, pOldPos, pNewPos);
		if (bCollided)
		{
			BackStepEdges(numEdges, pEdges, pOldPos, pNewPos);
			bChanged = true;
		}
		CheckMergeEdgePoints(numEdges, pEdges);
		if (bCollided)
		{
			// @@JS:
			//DetermineEdgePositivness(numEdges, pEdges, pOldPos);
		}
		// @@JS: When removing edges because of limits we may need to make sure to remove adjacent edges that make up an outer edge (or maybe other topological features also)
		// Not just hope for the distance tolerance in CheckInactiveEdgesDistance will do it
		//if (CheckEdgesLimits(numEdges, pEdges))
		//	bChanged = true;
		if (CheckEdgeDistances(numEdges, pEdges))
			bChanged = true;
		RemoveObsoleteEdgePoints(numEdges, pEdges);

		// Clear internal flags
		for (U32F ii = 1; ii<numEdges-1; ii++)
		{
			pEdges[ii].m_internFlags &= kNodeSlideOffPending; // clear all intern flags except kNodeSlideOffPending
		}

		numIter++;

		if (numIter > Min(numEdges * 5, 20u))
		{
			// Didn't converge, bail out
			MsgHavokErr("Rope strained edge detection didn't converge. Num iters: %i\n", numIter);
			JAROS_ASSERT(false);
			break;
		}

		if (bStraightened && !bChanged)
		{
			// We're done!
			break;
		}
	} while (true);

	m_lastStraightenSubsteps =  Max(m_lastStraightenSubsteps, bInterrupt ? numIter+1 : numIter);

	//if (CleanInactiveEdges(numEdges, pEdges))
	//	RemoveObsoleteEdgePoints(numEdges, pEdges);

	m_maxNumPoints = saveMaxNumPoints;
}

void Rope2::InitStrainedSectionEdges(U32F iKeyEnd, EdgePoint* pEdges, U32F& numEdges)
{
	ASSERT(numEdges == 1);

	F32 ropeDist = pEdges[0].m_ropeDist;
	F32 endRopeDist = m_pKeyRopeDist[iKeyEnd];

	U32F iEdge = 0;
	while (iEdge < m_numEdges && m_pEdges[iEdge].m_ropeDist <= ropeDist)
		iEdge++;

	bool bStrained = m_pKeyNodeFlags[iKeyEnd] & kNodeStrained;

	//U32F iPoint = 0;
	//while (iPoint < m_numPoints && m_pRopeDist[iPoint] <= ropeDist)
	//	iPoint++;

	while (iEdge < m_numEdges && m_pEdges[iEdge].m_ropeDist < endRopeDist)
	{
		//while (iPoint < m_numPoints && m_pRopeDist[iPoint] < m_pEdges[iEdge].m_ropeDist)
		//{
		//	if ((m_pNodeFlags[iPoint] & kNodeStrained) == 0)
		//	{
		//		// There is non-strained point here. We have to add it as initial edge point so that we check the collision as it gets straighten
		//		pEdges[numEdges].Reset();
		//		pEdges[numEdges].m_pos = m_pPos[iPoint];
		//		pEdges[numEdges].m_ropeDist = m_pRopeDist[iPoint];
		//		pEdges[numEdges].m_flags = kNodeStrained;
		//		pEdges[numEdges].m_internFlags = kNodeFreeToStrained;
		//		numEdges++;
		//		ALWAYS_ASSERT(numEdges <= m_maxNumPoints);
		//	}
		//	iPoint++;
		//}
		pEdges[numEdges] = m_pEdges[iEdge];
		if (pEdges[numEdges].m_flags & kNodeKeyframed)
		{
			pEdges[numEdges].m_flags = bStrained ? kNodeStrained : 0;
		}
		//else if ((pEdges[numEdges].m_flags & kNodeStrained) == 0)
		//{
		//	pEdges[numEdges].m_flags |= kNodeStrained;
		//	//pEdges[numEdges].m_internFlags = kNodeFreeToStrained;
		//}
		else
		{
			if (bStrained)
				pEdges[numEdges].m_flags |= kNodeStrained;
			else
				pEdges[numEdges].m_flags &= ~kNodeStrained;
		}
		numEdges++;
		ALWAYS_ASSERT(numEdges <= m_maxNumPoints);
		iEdge++;
	}

	//while (iPoint < m_numPoints && m_pRopeDist[iPoint] < endRopeDist)
	//{
	//	if ((m_pNodeFlags[iPoint] & kNodeStrained) == 0)
	//	{
	//		// There is non-strained point here. We have to add it as initial edge point so that we check the collision as it gets straighten
	//		pEdges[numEdges].Reset();
	//		pEdges[numEdges].m_pos = m_pPos[iPoint];
	//		pEdges[numEdges].m_ropeDist = m_pRopeDist[iPoint];
	//		pEdges[numEdges].m_flags = kNodeStrained;
	//		pEdges[numEdges].m_internFlags = kNodeFreeToStrained;
	//		numEdges++;
	//		ALWAYS_ASSERT(numEdges <= m_maxNumPoints);
	//	}
	//	iPoint++;
	//}

	pEdges[numEdges].Reset();
	pEdges[numEdges].m_pos = m_pKeyPos[iKeyEnd];
	pEdges[numEdges].m_ropeDist = m_pKeyRopeDist[iKeyEnd];
	pEdges[numEdges].m_flags = m_pKeyNodeFlags[iKeyEnd];
	numEdges++;
	ALWAYS_ASSERT(numEdges <= m_maxNumPoints);
}

static bool DetermineEdgePositivness(const RopeColEdge& edge, Point_arg pos0, Point_arg pos1)
{
	Vector testEdgeNormal = edge.m_normal;
	Vector edgeVec(edge.m_vec);
	Vector testEdgeBinormal = Cross(edgeVec, edge.m_normal);

	Scalar dot0 = Dot(pos0-edge.m_pnt, testEdgeNormal);
	Scalar dot1 = Dot(pos1-edge.m_pnt, testEdgeNormal);
	Scalar biDot0 = Dot(pos0-edge.m_pnt, testEdgeBinormal);
	Scalar biDot1 = Dot(pos1-edge.m_pnt, testEdgeBinormal);

	// @@
	bool positive;
	if (biDot0 > 0.01f)
	{
		if (dot0/biDot0 > kTan1Deg)
		{
			positive = true;
		}
		else if (dot0/biDot0 < -kTan1Deg)
		{
			positive = false;
		}
		else
		{
			positive = biDot0 < biDot1;
		}
	}
	else if (biDot1 > 0.01f)
	{
		if (dot1/biDot1 > kTan1Deg)
		{
			positive = false;
		}
		else if (dot1/biDot1 < -kTan1Deg)
		{
			positive = true;
		}
		else
		{
			positive = biDot0 < biDot1;
		}
	}
	else
	{
		positive = dot0 > dot1;
	}

	return positive;
}

static void TestEdgeLineCollision(const RopeColEdge& edge, bool& active, bool& positive, bool& invalid, Point_arg pos0, Point_arg pos1)
{
	// Find intersection of the line with edge normal plane
	Vector testEdgeNormal = edge.m_normal;
	Vector edgeVec(edge.m_vec);
	Vector testEdgeBinormal = Cross(edgeVec, edge.m_normal);

	Scalar dot0 = Dot(pos0-edge.m_pnt, testEdgeNormal);
	Scalar dot1 = Dot(pos1-edge.m_pnt, testEdgeNormal);
	Scalar biDot0 = Dot(pos0-edge.m_pnt, testEdgeBinormal);
	Scalar biDot1 = Dot(pos1-edge.m_pnt, testEdgeBinormal);

	Scalar posDist = Dist(pos0, pos1);
	Scalar dotMin = posDist * 0.5f * kScEdgeTol;
	bool valid0 = Abs(dot0) > dotMin || Abs(biDot0) > dotMin;
	bool valid1 = Abs(dot1) > dotMin || Abs(biDot1) > dotMin;
	if (!valid0 && !valid1)
	{
		// Rope goes pretty much parallel with the edge, ignore
		active = false;
		invalid = true;
		return;
	}

	Scalar tan0 = dot0/biDot0;
	Scalar tan1 = dot1/biDot1;

	// Update edge positive flag
	if (biDot0 > 0.01f)
	{
		if (tan0 > kTan1Deg)
		{
			// Hm ... this may actually be valid and (due to inner corner effect) active edge. We will at least try to guess the positiveness.
			// // We will mark this case invalid to prevent catching on edges "from the inside" and just rely on the pair edge
			if (biDot1 < -0.01f)
			{
				invalid = false;
				positive = tan1 > kTan1Deg;
			}
			else if (dot0 > 0.01f && dot1 < -0.01f)
			{
				invalid = false;
				positive = true;
			}
			else
			{
				invalid = true;
			}
			active = false;
			return;
		}
		else if (tan0 < -kTan1Deg)
		{
			positive = false;
		}
		else
		{
			positive = biDot0 < biDot1;
		}
	}
	else if (biDot1 > 0.01f)
	{
		if (tan1 > kTan1Deg)
		{
			// Hm ... this may actually be valid and (due to inner corner effect) active edge. We will at least try to guess the positiveness.
			// // We will mark this case invalid to prevent catching on edges "from the inside" and just rely on the pair edge
			if (biDot0 < -0.01f)
			{
				invalid = false;
				positive = tan0 < kTan1Deg;
			}
			else if (dot1 > 0.01f && dot0 < -0.01f)
			{
				invalid = false;
				positive = false;
			}
			else
			{
				invalid = true;
			}
			active = false;
			return;
		}
		else if (tan1 < -kTan1Deg)
		{
			positive = true;
		}
		else
		{
			positive = biDot0 < biDot1;
		}
	}
	else
	{
		positive = dot0 > dot1;
	}

	invalid = false;

	if (dot0 >= 0.0f && dot1 >= 0.0f)
	{
		active = false;
		return;
	}

	if (biDot0 >= 0.0f && biDot1 >= 0.0f)
	{
		active = false;
		return;
	}

	bool bInverted;
	if (positive)
	{
		if (biDot1 < 0.0f && tan1 < kTan1Deg)
		{
			// We must have caught this edge from "inside"
			invalid = true;
			active = false;
			return;
		}
		bInverted = dot1 > dot0;
	}
	else
	{
		if (biDot0 < 0.0f && tan0 < kTan1Deg)
		{
			// We must have caught this edge from "inside"
			invalid = true;
			active = false;
			return;
		}
		bInverted = dot0 > dot1;
	}

	if (0)
	{
		if (dot0 >= 0.0f && dot1 >= 0.0f)
		{
			active = false;
			return;
		}

		if (biDot0 >= 0.0f && biDot1 >= 0.0f)
		{
			active = false;
			return;
		}

		// Update edge positive flag
		bool above0 = biDot0 < 0.0f && -dot0/biDot0 > -kTan1Deg;
		bool above1 = biDot1 < 0.0f && -dot1/biDot1 > -kTan1Deg;
		if (above0)
		{
			positive = true;
		}
		else if (above1)
		{
			positive = false;
		}
		else
		{
			if (dot0 <= 0.0f && dot1 <= 0.0f)
			{
				active = false;
				return;
			}
			positive = biDot0 > biDot1;
		}

		//bool bInverted; // shadows earlier declaration
		if (positive)
		{
			bInverted = dot1 > dot0;
		}
		else
		{
			bInverted = dot0 > dot1;
		}
		//bInverted = false;
	}

	Scalar dotDiff = dot0-dot1;
	Point planeInt = Lerp(pos0, pos1, dot0/dotDiff);
	Scalar biDot = Dot(planeInt-edge.m_pnt, testEdgeBinormal);
	active = bInverted ? biDot >= 0.0f : biDot <= 0.0f;
}

static void ProjectLineEdge(const RopeColEdge& edge, Point_arg prevPnt, Point_arg nextPnt, Point& projPnt)
{
	Vector edgeVec(edge.m_vec);
	Point prevProj = edge.m_pnt + edgeVec * Dot(prevPnt-edge.m_pnt, edgeVec);
	Point nextProj = edge.m_pnt + edgeVec * Dot(nextPnt-edge.m_pnt, edgeVec);
	Scalar prevDist = Dist(prevPnt, prevProj);
	Scalar nextDist = Dist(nextPnt, nextProj);
	Scalar sumDist = prevDist+nextDist;
	if (sumDist == 0.0f)
	{
		ASSERT(false);
		projPnt = prevPnt;
		//newPoint = pEdge0->m_pnt + edgeVec * Dot(nextPnt-pEdge0->m_pnt, edgeVec);
	}
	else
	{
		projPnt = Lerp(prevProj, nextProj, prevDist/sumDist);
	}
}

static void ProjectLineEdge(Point_arg edgePnt, Vector_arg edgeVec, Point_arg prevPnt, Point_arg nextPnt, Point& projPnt)
{
	Point prevProj = edgePnt + edgeVec * Dot(prevPnt-edgePnt, edgeVec);
	Point nextProj = edgePnt + edgeVec * Dot(nextPnt-edgePnt, edgeVec);
	Scalar prevDist = Dist(prevPnt, prevProj);
	Scalar nextDist = Dist(nextPnt, nextProj);
	Scalar sumDist = prevDist+nextDist;
	if (sumDist == 0.0f)
	{
		//ASSERT(false);
		projPnt = prevPnt;
		//newPoint = pEdge0->m_pnt + edgeVec * Dot(nextPnt-pEdge0->m_pnt, edgeVec);
	}
	else
	{
		projPnt = Lerp(prevProj, nextProj, prevDist/sumDist);
	}
}

void Rope2::ClasifyEdgeIntersection(const RopeColEdge* pEdge0, bool positive0, const RopeColEdge* pEdge1, bool positive1, 
	const Point& prevRopePnt, const Point& nextRopePnt, 
	Point& cornerPt, EdgeIntersection& intType)
{
	const Vector vec0 = Vector(pEdge0->m_vec);
	const Vector vec1 = Vector(pEdge1->m_vec);
	const Scalar len0 = pEdge0->m_vec.W();
	const Scalar len1 = pEdge1->m_vec.W();
	const Point end0 = pEdge0->m_pnt + vec0 * len0;

	intType = kIntNone;

	{
		// Test for edges being pretty much the same
		// Maybe this test needs to be more robust?
		Point start0Proj1 = pEdge1->m_pnt + Dot(pEdge0->m_pnt - pEdge1->m_pnt, vec1) * vec1;
		Point end0Proj1 = pEdge1->m_pnt + Dot(end0 - pEdge1->m_pnt, vec1) * vec1;
		if (DistSqr(start0Proj1, pEdge0->m_pnt) < kScEdgeTol2 && DistSqr(end0Proj1, end0) < kScEdgeTol2)
		{
			intType = kIntSame;
			return;
		}
	}

	Scalar s, t;
	if (!LineLineIntersection(pEdge0->m_pnt, vec0, pEdge1->m_pnt, vec1, s, t))
	{
		// @@JS: ASSERT(false); // Hm??
		return;
	}

	if ((s < -0.001f && s > len0 + 0.001f) || (t < -0.001f && t > len1 + 0.001f))
	{
		return;
	}

	cornerPt = pEdge0->m_pnt + vec0 * s;

	// Normal of the plane in which both edges lie
	Vector plNorm = Cross(vec0, vec1);
	if (LengthSqr(plNorm) < 0.001f*0.001f)
	{
		return;
	}

	Scalar prevDt = Dot(prevRopePnt - cornerPt, plNorm);
	Scalar nextDt = Dot(nextRopePnt - cornerPt, plNorm);

	Point projPnt0, projPnt1;
	ProjectLineEdge(*pEdge0, prevRopePnt, nextRopePnt, projPnt0);
	ProjectLineEdge(*pEdge1, prevRopePnt, nextRopePnt, projPnt1);
	Scalar proj0s = Dot(projPnt0 - pEdge0->m_pnt, vec0);
	Scalar proj1t = Dot(projPnt1 - pEdge1->m_pnt, vec1);

	// This our plane as seen from above
	// and how we name the quadrants
	// If edge is positive rope passes on the right of the edge
	//
	//        edge1  ^
	//              /
	//     D       /      C
	//            /
	// ---------------------------->
	//          /                 edge0
	//    A    /        B 
	//        /
	//       /

	// @@JS: this test should maybe take into account dt being close to 0.0 and use the kTan1Deg test instead?
	if (prevDt*nextDt <= 0.0f)
	{
		// Rope crosses the plane of the edges once
		if (prevDt < 0.0f || (prevDt == 0.0f && nextDt > 0.0f))
		{
			// Plane has negative orientation so we have to swap the positiveness also
			positive0 = !positive0;
			positive1 = !positive1;
		}

		// See in which quadrant
		// and test if that quadrant is enclosed with edges
		if (positive0 && !positive1)
		{
			// Quadrant A
			if (s > 0.001f && t > 0.001f)
			{
				if (proj0s < s)
				{
					ASSERT_ROPE_DETAIL(proj1t >= t);
					intType = kIntInnerCorner0;
				}
				else if (proj1t < t)
				{
					intType = kIntInnerCorner1;
				}
				else
				{
					intType = kIntInnerCorner;
				}
			}
			else if (s > 0.001f)
			{
				intType = kIntInnerT1;
			}
			else if (t > 0.001f)
			{
				intType = kIntInnerT0;
			}
		}
		else if (positive0 && positive1)
		{
			// Quadrant B
			if (s < len0 - 0.001f && t > 0.001f)
			{
				if (proj0s > s)
				{
					ASSERT_ROPE_DETAIL(proj1t >= t);
					intType = kIntInnerCorner0;
				}
				else if (proj1t < t)
				{
					intType = kIntInnerCorner1;
				}
				else
				{
					intType = kIntInnerCorner;
				}
			}
			else if (s < len0 - 0.001f)
			{
				intType = kIntInnerT1;
			}
			else if (t > 0.001f)
			{
				intType = kIntInnerT0;
			}
		}
		else if (!positive0 && positive1)
		{
			// Quadrant C
			if (s < len0 - 0.001f && t < len1 - 0.001f)
			{
				if (proj0s > s)
				{
					ASSERT_ROPE_DETAIL(proj1t <= t);
					intType = kIntInnerCorner0;
				}
				else if (proj1t > t)
				{
					intType = kIntInnerCorner1;
				}
				else
				{
					intType = kIntInnerCorner;
				}
			}
			else if (s < len0 - 0.001f)
			{
				intType = kIntInnerT1;
			}
			else  if (t < len1 - 0.001f)
			{
				intType = kIntInnerT0;
			}
		}
		else
		{
			// Quadrant D
			if (s > 0.001f && t < len1 - 0.001f)
			{
				if (proj0s < s)
				{
					ASSERT_ROPE_DETAIL(proj1t <= t);
					intType = kIntInnerCorner0;
				}
				else if (proj1t > t)
				{
					intType = kIntInnerCorner1;
				}
				else
				{
					intType = kIntInnerCorner;
				}
			}
			else if (s > 0.001f)
			{
				intType = kIntInnerT1;
			}
			else if (t < len1 - 0.001f)
			{
				intType = kIntInnerT0;
			}
		}
	}
	else
	{
		// Rope crosses the plane of the edges twice
		if (prevDt > 0.0f)
		{
			// Plane has negative orientation so we have to swap the positiveness also
			positive0 = !positive0;
			positive1 = !positive1;
		}

		// Rope crosses the plane of the edges twice
		// It has to be on two opposite quadrants
		// See which quadrants those are and if there is a free path between them without crossing an edge
		if (positive0 == positive1)
		{
			// Quadrants B and D
			if (s > 0.001f && t < len1 - 0.001f)
			{
				// Quadrant D
				if (proj0s < s && t <= 0.001f)
				{
					intType = kIntInnerCorner0;
				}
				if (proj1t > t && s >= len0 - 0.001f)
				{
					intType = intType == kIntInnerCorner0 ? kIntInnerCorner01 : kIntInnerCorner1;
				}
				if (intType == kIntNone)
				{
					intType = kIntInnerCorner;
				}
			}
			else if (t > 0.001f && s < len0 - 0.001f)
			{
				// Quadrant B
				if (proj0s > s && t >= len1 - 0.001f)
				{
					intType = kIntInnerCorner0;
				}
				if (proj1t < t && s <= 0.001f)
				{
					intType = intType == kIntInnerCorner0 ? kIntInnerCorner01 : kIntInnerCorner1;
				}
				if (intType == kIntNone)
				{
					intType = kIntInnerCorner;
				}
			}
			else
			{
				// This is outer corner
				if (s > 0.001f)
				{
					ASSERT_ROPE_DETAIL(t > 0.001f);
					if (positive0)
					{
						intType = kIntOuterCorner01;
					}
					else
					{
						intType = kIntOuterCorner10;
					}
				}
				else
				{
					ASSERT_ROPE_DETAIL(s < len0 - 0.001f && t < len1 - 0.001f);
					if (positive0)
					{
						intType = kIntOuterCorner10;
					}
					else
					{
						intType = kIntOuterCorner01;
					}
				}
			}
		}
		else
		{
			// Quadrants A and C
			if (s > 0.001f && t > 0.001f)
			{
				// Quadrant A
				if (proj0s < s && t > len1 - 0.001f)
				{
					intType = kIntInnerCorner0;
				}
				if (proj1t < t && s > len0 - 0.001f)
				{
					intType = intType == kIntInnerCorner0 ? kIntInnerCorner01 : kIntInnerCorner1;
				}
				if (intType == kIntNone)
				{
					intType = kIntInnerCorner;
				}
			}
			else if (s < len0 - 0.001f && t < len1 - 0.001f)
			{
				// Quadrant C
				if (proj0s > s && t < 0.001f)
				{
					intType = kIntInnerCorner0;
				}
				if (proj1t > t && s < 0.001f)
				{
					intType = intType == kIntInnerCorner0 ? kIntInnerCorner01 : kIntInnerCorner1;
				}
				if (intType == kIntNone)
				{
					intType = kIntInnerCorner;
				}
			}
			else
			{
				// This is outer corner
				if (s > 0.001f)
				{
					ASSERT_ROPE_DETAIL(t < len1 - 0.001f);
					if (positive0)
					{
						intType = kIntOuterCorner10;
					}
					else
					{
						intType = kIntOuterCorner01;
					}
				}
				else
				{
					ASSERT_ROPE_DETAIL(s < len0 - 0.001f && t > 0.001f);
					if (positive0)
					{
						intType = kIntOuterCorner01;
					}
					else
					{
						intType = kIntOuterCorner10;
					}
				}
			}
		}
	}
}

void Rope2::StraightenUnfoldEdges(const RopeColEdge** pEdges, const U32F* pEdgePointIndices, EdgePoint* pEdgePoints, const bool* pPositive, U32F numEdges, 
	const Point& prevRopePnt, const Point& nextRopePnt, F32 moveFricMult, Point* pPoints, bool* pSlideOff, bool& bMerge, bool& bCanSlideOff)
{
	ScopedTempAllocator jj2(FILE_LINE_FUNC);

	Scalar* startT = NDI_NEW Scalar[numEdges];
	Scalar* stopTa = NDI_NEW Scalar[numEdges];
	Scalar* stopTb = NDI_NEW Scalar[numEdges];
	I8* slideOffDir = NDI_NEW I8[numEdges+1];
	I8* stopTaIsEnd = NDI_NEW I8[numEdges];
	I8* stopTbIsEnd = NDI_NEW I8[numEdges];

	Vector* vecProj = NDI_NEW Vector[numEdges];
	Point* pntProj = NDI_NEW Point[numEdges];

	vecProj[0] = (pPositive[0] ? 1.0f : -1.0f) * Vector(pEdges[0]->m_vec);
	pntProj[0] = pEdges[0]->m_pnt;
	startT[0] = Dot(pPoints[0] - pntProj[0], vecProj[0]);
	stopTa[0] = stopTb[0] = startT[0];
	vecProj[1] = (pPositive[1] ? 1.0f : -1.0f) * Vector(pEdges[1]->m_vec);
	pntProj[1] = pEdges[1]->m_pnt;
	startT[1] = Dot(pPoints[1] - pntProj[1], vecProj[1]);
	stopTa[1] = stopTb[1] = startT[1];
	
	memset(slideOffDir, 0, sizeof(slideOffDir[0])*(numEdges+1));

	Scalar t0, t1;
	bool bConverging = false;
	bool bIntersect;
	bIntersect = LineLineIntersection(pntProj[0], vecProj[0], pntProj[1], vecProj[1], t0, t1);
	if (bIntersect)
	{
		bConverging = Abs(startT[0] - t0) < 0.001f ? (pPositive[0] && t0 > 0.001f) || (!pPositive[0] && t0 > -0.001f) 
			: startT[0] < t0;
		stopTb[0] = t0;
		stopTa[1] = t1;
		if (pEdgePointIndices[0] == pEdgePointIndices[1])
		{
			// Are edges converging?
			if (bConverging)
				// Yes, if we move right we will slide off the corner
				slideOffDir[1] = +1;
			else
				// No, if we move left we will slide off the corner
				slideOffDir[1] = -1;
		}
	}

	Vector plNorm;
	plNorm = SafeNormalize(Cross(vecProj[1], vecProj[0]), kZero);
	if (AllComponentsEqual(plNorm, kZero))
	{
		Vector vec0To1Perp = pntProj[1] - pntProj[0];
		vec0To1Perp -= Dot(vec0To1Perp, vecProj[0]) * vecProj[0];
		plNorm = Normalize(Cross(vec0To1Perp, vecProj[0]));
	}
	else
	{
		// If edges are converging we need to flip the normal
		if (bConverging)
			plNorm = -plNorm;
	}

	for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		Vector vec0 = (pPositive[ii] ? 1.0f : -1.0f) * Vector(pEdges[ii]->m_vec);
		Vector vec1 = (pPositive[ii+1] ? 1.0f : -1.0f) * Vector(pEdges[ii+1]->m_vec);
		Point pnt0 = pEdges[ii]->m_pnt;
		Point pnt1 = pEdges[ii+1]->m_pnt;

		bIntersect = LineLineIntersection(pnt0, vec0, pnt1, vec1, t0, t1);
		if (bIntersect)
		{
			bConverging = (pPositive[ii] && t0 > 0.001f) || (!pPositive[ii] && t0 > -0.001f);
		}

		Vector edgeVec = vecProj[ii];
		Point edgePnt = pntProj[ii];
		Vector edgePerp = Cross(edgeVec, plNorm);

		Scalar vec1Edge = Dot(vec1, vec0);
		Scalar vec1Perp = (bConverging ? -1.0f : 1.0f) * Length(vec1 - vec1Edge * vec0);
		vecProj[ii+1] = vec1Edge * edgeVec + vec1Perp * edgePerp;

		Scalar pnt1Edge = Dot(pnt1 - pnt0, vec0);
		Scalar pnt1Perp = Length(pnt1 - pnt0 - pnt1Edge * vec0);
		pntProj[ii+1] = edgePnt + pnt1Edge * edgeVec + pnt1Perp * edgePerp;

		startT[ii+1] = Dot(pPoints[ii+1] - pnt1, vec1);
		stopTa[ii+1] = stopTb[ii+1] = startT[ii+1];

		if (bIntersect)
		{
			stopTb[ii] = t0;
			stopTa[ii+1] = t1;
			if (pEdgePointIndices[ii] == pEdgePointIndices[ii+1])
			{
				// Are edges converging?
				if (bConverging)
					// Yes, if we move right we will slide off the corner
					slideOffDir[ii+1] = +1;
				else
					// No, if we move left we will slide off the corner
					slideOffDir[ii+1] = -1;
			}
		}
	}

	Point prevPntProj;
	{
		Vector vec0 = (pPositive[0] ? 1.0f : -1.0f) * Vector(pEdges[0]->m_vec);
		Point pnt0 = pEdges[0]->m_pnt;

		Vector edgeVec = vecProj[0];
		Point edgePnt = pntProj[0];
		Vector edgePerp = -Cross(edgeVec, plNorm);

		Scalar pntEdge = Dot(prevRopePnt - pnt0, vec0);
		Scalar pntPerp = Length(prevRopePnt - pnt0 - pntEdge * vec0);
		prevPntProj = edgePnt + pntEdge * edgeVec + pntPerp * edgePerp;
	}

	Point nextPntProj;
	{
		Vector vec0 = (pPositive[numEdges-1] ? 1.0f : -1.0f) * Vector(pEdges[numEdges-1]->m_vec);
		Point pnt0 = pEdges[numEdges-1]->m_pnt;

		Vector edgeVec = vecProj[numEdges-1];
		Point edgePnt = pntProj[numEdges-1];
		Vector edgePerp = Cross(edgeVec, plNorm);

		Scalar pntEdge = Dot(nextRopePnt - pnt0, vec0);
		Scalar pntPerp = Length(nextRopePnt - pnt0 - pntEdge * vec0);
		nextPntProj = edgePnt + pntEdge * edgeVec + pntPerp * edgePerp;
	}

	// Also, we want to stop if we simply hit end of an edge before any of the intersection
	// @@JS After the demo we should clean this up. It would probably much more straightforward
	// to first do this before going through edge intersections
	memset(stopTaIsEnd, 0, sizeof(stopTaIsEnd[0])*numEdges);
	memset(stopTbIsEnd, 0, sizeof(stopTbIsEnd[0])*numEdges);
	for (U32F ii = 0; ii<numEdges; ii++)
	{
		if (pEdges[ii]->m_startTriIndex < 0 && (stopTa[ii] == startT[ii] || Abs(stopTa[ii]) > kEdgeTol) && (stopTb[ii] == startT[ii] || Abs(stopTb[ii]) > kEdgeTol))
		{
			if (slideOffDir[ii] == 0 && (startT[ii] * stopTa[ii] < 0.0f || Abs(startT[ii]) < kEdgeTol))
			{
				stopTa[ii] = pPositive[ii] ? Min(startT[ii], 0.0f) : Max(startT[ii], 0.0f);
				stopTaIsEnd[ii] = pPositive[ii] ? -1 : 1;
			}
			else if (slideOffDir[ii+1] == 0 && (startT[ii] * stopTb[ii] < 0.0f || Abs(startT[ii]) < kEdgeTol))
			{
				stopTb[ii] = pPositive[ii] ? Min(startT[ii], 0.0f) : Max(startT[ii], 0.0f);
				stopTbIsEnd[ii] = pPositive[ii] ? -1 : 1;
			}
		}
		F32 tEnd = (pPositive[ii] ? 1.0f : -1.0f) * pEdges[ii]->m_vec.W();
		if (pEdges[ii]->m_endTriIndex < 0 && (stopTa[ii] == startT[ii] || Abs(stopTa[ii]-tEnd) > kEdgeTol) && (stopTb[ii] == startT[ii] || Abs(stopTb[ii]-tEnd) > kEdgeTol))
		{
			if (slideOffDir[ii] == 0 && ((tEnd-startT[ii]) * (tEnd-stopTa[ii]) < 0.0f || Abs(tEnd-startT[ii]) < kEdgeTol))
			{
				stopTa[ii] = pPositive[ii] ? Max(startT[ii], tEnd) : Min(startT[ii], tEnd);
				stopTaIsEnd[ii] = pPositive[ii] ? 1 : -1;
			}
			else if (slideOffDir[ii+1] == 0 && ((tEnd-startT[ii]) * (tEnd-stopTb[ii]) < 0.0f || Abs(tEnd-startT[ii]) < kEdgeTol))
			{
				stopTb[ii] = pPositive[ii] ? Max(startT[ii], tEnd) : Min(startT[ii], tEnd);
				stopTbIsEnd[ii] = pPositive[ii] ? 1 : -1;
			}
		}
	}

	Point ropePnt = prevPntProj;
	Vector ropeDir = SafeNormalize(nextPntProj - ropePnt, kZero);
	ASSERT(!AllComponentsEqual(ropeDir, kZero));

	Scalar* t = NDI_NEW Scalar[numEdges];
	memcpy(t, startT, numEdges*sizeof(t[0]));

	// For edge point lerping ....
	Scalar* tLerp0 = NDI_NEW Scalar[numEdges];
	for (U32F ii = 0; ii<numEdges; ii++)
	{
		Vector edgeVec = (pPositive[ii] ? 1.0f : -1.0f) * Vector(pEdges[ii]->m_vec);
		Point edgePnt = pEdges[ii]->m_pnt;

		const Point& pos0 = pEdgePoints[pEdgePointIndices[ii]].m_pos;
		tLerp0[ii] = Dot(pos0 - edgePnt, edgeVec);
	}

	Scalar tLerp = Min(Scalar(1.0f), moveFricMult * g_ropeEdgeSlideSpeed * m_scStepTime);

	Point ropePntStopped;
	Vector ropeDirStopped;
	bool ropeStopped = false;

	for (I32F ii = 0; ii<numEdges; ii++)
	{
		if (LineLineIntersection(ropePnt, ropeDir, pntProj[ii], vecProj[ii], t0))
		{
			Scalar tLerped = Lerp(tLerp0[ii], t0, tLerp);
			if ((tLerped-startT[ii])*(t0-startT[ii]) < 0.0f)
			{
				// We need to make sure tLerped is not outside of <startT, t0> interval
				// This happens because starting point for lerping is the edge point position before we started this round straightening
				tLerped = startT[ii];
			}
			if (ropeStopped)
			{
				// We're lerping towards the target line but can't go further than the straight line from a stopped point
				if (LineLineIntersection(ropePntStopped, ropeDirStopped, pntProj[ii], vecProj[ii], t0))
				{
					if ((tLerped-t0)*(startT[ii]-t0) < 0.0f)
					{
						tLerped = t0;
					}
				}
			}
			t0 = tLerped;

			bool stopped = false;

			// Check for edge collision trims
			if (pEdges[ii]->m_startTriIndex >= 0 && (pPositive[ii] ? 1.0f : -1.0f) * t0 < 0.0f)
			{
				t0 = 0.0f;
				t[ii] = t0;
				stopped = true;
			}
			else if (pEdges[ii]->m_endTriIndex >= 0 && (pPositive[ii] ? 1.0f : -1.0f) * t0 > pEdges[ii]->m_vec.W())
			{
				t0 = (pPositive[ii] ? 1.0f : -1.0f) * pEdges[ii]->m_vec.W();
				t[ii] = t0;
				stopped = true;
			}

			// Check for hitting a stop point
			if (slideOffDir[ii] != 0)
			{
				if ((t0 - stopTa[ii]) * (F32)slideOffDir[ii] > 0.0f)
				{
					// Sliding off outer edges
					t[ii] = stopTa[ii];
					stopped = true;
					pSlideOff[ii] = true;
				}
			}
			else if (stopTaIsEnd[ii] != 0)
			{
				if ((t0 - stopTa[ii]) * (F32)stopTaIsEnd[ii] > 0.0f)
				{
					// Hitting the end of an edge
					t[ii] = stopTa[ii];
					stopped = true;
					pSlideOff[ii] = true;
					bCanSlideOff = bCanSlideOff && Abs(stopTa[ii]-startT[ii]) < kEdgeTol;
				}
			}
			else if ((t0-stopTa[ii])*(startT[ii]-stopTa[ii]) < 0.0f)
			{
				// 2 edges merging
				t[ii] = stopTa[ii];
				stopped = true;
				bMerge = true;
				pEdgePoints[pEdgePointIndices[ii]].m_internFlags |= kNodeMerge;
				ASSERT(ii>0);
				pEdgePoints[pEdgePointIndices[ii-1]].m_internFlags |= kNodeMerge;
			}
			else if (!stopTaIsEnd[ii] && Abs(startT[ii]-stopTa[ii]) >= kEdgeTol && Abs(t0-stopTa[ii]) < kEdgeTol)
			{
				// We're not crossing our merge stop point but we are within the tolerance
				bMerge = true;
				pEdgePoints[pEdgePointIndices[ii]].m_internFlags |= kNodeMerge;
				ASSERT(ii>0);
				pEdgePoints[pEdgePointIndices[ii-1]].m_internFlags |= kNodeMerge;
			}
			if (slideOffDir[ii+1] != 0)
			{
				if ((t0 - stopTb[ii]) * (F32)slideOffDir[ii+1] > 0.0f)
				{
					// Sliding off outer edges
					t[ii] = stopTb[ii];
					stopped = true;
					pSlideOff[ii] = true;
				}
			}
			else if (stopTbIsEnd[ii] != 0)
			{
				if ((t0 - stopTb[ii]) * (F32)stopTbIsEnd[ii] > 0.0f)
				{
					// Hitting the end of an edge
					t[ii] = stopTb[ii];
					stopped = true;
					pSlideOff[ii] = true;
					bCanSlideOff = bCanSlideOff && Abs(stopTb[ii]-startT[ii]) < kEdgeTol;
				}
			}
			else if ((t0-stopTb[ii])*(startT[ii]-stopTb[ii]) < 0.0f)
			{
				// 2 edges merging
				t[ii] = stopTb[ii];
				stopped = true;
				bMerge = true;
				pEdgePoints[pEdgePointIndices[ii]].m_internFlags |= kNodeMerge;
				ASSERT(ii<numEdges);
				pEdgePoints[pEdgePointIndices[ii+1]].m_internFlags |= kNodeMerge;
			}
			else if (!stopTbIsEnd[ii] && Abs(startT[ii]-stopTb[ii]) >= kEdgeTol && Abs(t0-stopTb[ii]) < kEdgeTol)
			{
				// We're not crossing our merge stop point but we are within the tolerance
				bMerge = true;
				pEdgePoints[pEdgePointIndices[ii]].m_internFlags |= kNodeMerge;
				ASSERT(ii<numEdges);
				pEdgePoints[pEdgePointIndices[ii+1]].m_internFlags |= kNodeMerge;
			}

			if (!stopped)
			{
				t[ii] = t0;
			}
			pPoints[ii] = pntProj[ii] + t[ii] * vecProj[ii];

			if (stopped)
			{
				// Rope has been stopped by the end of an edge
				// We need to go back and see if we need to retract some of the previous points
				Point ropePntBack;
				Vector ropeDirBack;
				I32F jj;
				for (jj = ii-1; jj>=0; jj--)
				{
					ropePntBack = jj == 0 ? prevPntProj : pPoints[jj-1];
					ropeDirBack = SafeNormalize(pPoints[ii] - ropePntBack, kZero);
					if (AllComponentsEqual(ropeDirBack, kZero))
					{
						// We're intersecting at the same point as the previous pair of edges, no need to retract
						break;
					}
					if (LineLineIntersection(ropePntBack, ropeDirBack, pntProj[jj], vecProj[jj], t0))
					{
						if ((t0-t[jj])*(startT[jj]-t[jj]) <= 0.0f)
						{
							// This point does not need to retract
							break;
						}
					}
					else
					{
						JAROS_ASSERT(false);
						break;
					}
				}
				if (jj+1<ii)
				{
					// We need to retract points between jj and ii onto the straight line from jj to ii
					ropePntBack = jj < 0 ? prevPntProj : pPoints[jj];
					ropeDirBack = SafeNormalize(pPoints[ii] - ropePntBack, kZero);
					ASSERT(!AllComponentsEqual(ropeDirBack, kZero));
					for (jj++; jj<ii; jj++)
					{
						if (LineLineIntersection(ropePntBack, ropeDirBack, pntProj[jj], vecProj[jj], t0))
						{
							if ((t0-startT[jj])*(t[jj]-startT[jj]) <= 0.0f)
							{
								t[jj] = startT[jj];
							}
							else
							{
								t[jj] = t0;
							}
							pSlideOff[jj] = false;
							pEdgePoints[pEdgePointIndices[jj]].m_internFlags &= ~kNodeMerge;
						}
						else
						{
							JAROS_ASSERT(false);
							break;
						}
					}
				}

				ropePntStopped = pPoints[ii];
				ropeDirStopped = SafeNormalize(nextPntProj - ropePnt, kZero);
				ASSERT(!AllComponentsEqual(ropeDirStopped, kZero));
				ropeStopped = true;
			}
		}
		else
		{
			JAROS_ASSERT(false);
			//pPoints[ii] = ii == 0 ? ropePnt : pPoints[ii-1];
		}
	}

	for (U32F ii = 0; ii<numEdges; ii++)
	{
		Vector vec = (pPositive[ii] ? 1.0f : -1.0f) * Vector(pEdges[ii]->m_vec);
		Point pnt = pEdges[ii]->m_pnt;
		pPoints[ii] = pnt + t[ii] * vec;
	}
}

void Rope2::SortOuterCornerEdges(const EdgePoint& ePoint, const EdgeIntersectionMatrix& intType, U8* pOuterCornerEdges, U32F& numOuterCornerEdges, U16& activeEdges)
{
	// @@JS: We should separate "left" and "right" outer corners
	// If we have outer corners we need to sort the edges as they go along the rope
	U8 sortedEdges[EdgePoint::kMaxNumEdges];
	U32F numSortedEdges;
	U32F iOuterCornerEdge;
	while (true)
	{
		numSortedEdges = 0;
		iOuterCornerEdge = 0;
		while (iOuterCornerEdge < numOuterCornerEdges)
		{
			U32F iPointEdge = pOuterCornerEdges[iOuterCornerEdge];
			I32F minPos = -1;
			I32F maxPos = numSortedEdges;
			I32F samePos = -1;
			for (I32F otherPos = 0; otherPos<numSortedEdges; otherPos++)
			{ 
				U32F iPointEdgeOther = sortedEdges[otherPos];
				EdgeIntersection intTypeA = intType[iPointEdge][iPointEdgeOther];
				if (intTypeA == kIntOuterCorner01)
				{
					maxPos = Min(maxPos, otherPos);
					ASSERT(minPos < maxPos);
				}
				else if (intTypeA == kIntOuterCorner10)
				{
					minPos = Max(minPos, otherPos);
					ASSERT(minPos < maxPos);
				}
				else if (intTypeA == kIntSame)
				{
					samePos = otherPos;
					ASSERT(samePos > minPos && samePos < maxPos);
				}
			}
			I32 insertPos = samePos;
			if (insertPos == -1 && maxPos-minPos == 1)
			{
				insertPos = maxPos;
			}
			if (insertPos != -1)
			{
				memmove(sortedEdges+insertPos+1, sortedEdges+insertPos, (numSortedEdges-insertPos)*sizeof(sortedEdges[0]));
				sortedEdges[insertPos] = iPointEdge;
				numSortedEdges++;
				pOuterCornerEdges[iOuterCornerEdge] = pOuterCornerEdges[numOuterCornerEdges-1];
				numOuterCornerEdges--;
				iOuterCornerEdge = 0;
			}
			else
			{
				iOuterCornerEdge++;
			}
		}

		// If any of the sorted outer edges is not active (which should be because it is part of InnerCorner0/1 edge pair, we just throw the whole group of outer edges away
		bool reset = false;
		for (U32F ii = 0; ii<numSortedEdges; ii++)
		{
			if (((1U << sortedEdges[ii]) & activeEdges) == 0)
			{
				reset = true;
				break;
			}
		}
		if (reset)
		{
			// Set this group of outer edges to inactive and reset the sort to give chance to the outer edges on other side of inner corner
			// If there are no more outer edges it should mean the other side of inner corner is a single edge and that's the one we should slide along
			for (U32F ii = 0; ii<numSortedEdges; ii++)
			{
				activeEdges &= ~(1U << sortedEdges[ii]);
			}
		}
		else
		{
			break;
		}
	}

	//See @@JS above. Disabling this assert until then.
	//JAROS_ASSERT(numOuterCornerEdges == 0);

	// Remove "same"
	U32F iEdge = 1;
	while (iEdge < numSortedEdges)
	{
		if (intType[sortedEdges[iEdge-1]][sortedEdges[iEdge]] == kIntSame)
		{
			memmove(sortedEdges+iEdge, sortedEdges+iEdge+1, (numSortedEdges-iEdge-1)*sizeof(sortedEdges[0]));
			numSortedEdges--;
		}
		else
		{
			iEdge++;
		}
	}

	// ASSERT(numSortedEdges >= 2);
	if (numSortedEdges >= 2)
	{
		memcpy(pOuterCornerEdges, sortedEdges, numSortedEdges*sizeof(pOuterCornerEdges[0]));
		numOuterCornerEdges = numSortedEdges;
	}
	else
	{
		numOuterCornerEdges = 0;
	}
}

void Rope2::SlideOffEdge(EdgePoint& ePoint, const Point& pos, U32F edgeIndex, const EdgeIntersectionMatrix& intType)
{
	U16 processedEdges = ePoint.m_slideOff;
	ePoint.SetEdgeSlideOff(edgeIndex, true);
	ePoint.SetEdgeActive(edgeIndex, false);
	while (processedEdges != ePoint.m_slideOff)
	{
		U16 toProcess = ePoint.m_slideOff & (~processedEdges);
		U32F eIndex = 0;
		while (eIndex < ePoint.m_numEdges && (toProcess & 1) == 0)
		{
			toProcess = toProcess >> 1;
			eIndex++;
		}
		ASSERT(eIndex < ePoint.m_numEdges);
		const RopeColEdge* pEdge = &m_colCache.m_pEdges[ePoint.m_edgeIndices[eIndex]];
		for (U32F eOtherIndex = 0; eOtherIndex < ePoint.m_numEdges; eOtherIndex++)
		{
			switch (intType[eIndex][eOtherIndex])
			{
			case kIntSame:
				{
					// Same means it's parallel but the extensions of this edge may be different
					Scalar t = Dot(pos - pEdge->m_pnt, Vector(pEdge->m_vec));
					// Assert bellow does not have to be true if this edge is marked as slide-off because of its InnerT1 relation to another edge
					// JAROS_ASSERT(t < kScEdgeTol || t > pEdge->m_vec.W() - kScEdgeTol);
					const RopeColEdge* pEdgeOther = &m_colCache.m_pEdges[ePoint.m_edgeIndices[eOtherIndex]];
					Scalar tOther = Dot(pos - pEdgeOther->m_pnt, Vector(pEdgeOther->m_vec));
					Scalar dir = Dot(Vector(pEdge->m_vec), Vector(pEdgeOther->m_vec));
					if ((t < kScEdgeTol && dir > 0.0f) || (t > pEdge->m_vec.W() - kScEdgeTol && dir < 0.0f))
					{
						if (tOther > kScEdgeTol)
							// Not sliding off this one
							break; 
					}
					else if ((t < kScEdgeTol && dir < 0.0f) || (t > pEdge->m_vec.W() - kScEdgeTol && dir > 0.0f))
					{
						if (tOther < pEdgeOther->m_vec.W() - kScEdgeTol)
							// Not sliding off this one
							break; 
					}
				}
				FALLTHROUGH;
			case kIntOuterCorner01:
			case kIntOuterCorner10:
			case kIntInnerT1:
				ePoint.SetEdgeSlideOff(eOtherIndex, true);
				ePoint.SetEdgeActive(eOtherIndex, false);
				break;
			}
		}
		processedEdges |= 1U << eIndex;
	}
}

void Rope2::ProcessUnfoldEdgesGroup(U32F numUnfoldEdges, const U8* pUnfoldEdgeIndices, const U32F* pUnfoldEdgePointIndices, 
	const bool* pUnfoldPositive, Point* pUnfoldPoints, U32F iPrevEdge, U32F iNextEdge,
	EdgePoint* pEdges, Point* pPosOut, Vector* pDir, EdgeIntersectionMatrix* pIntType, U32F& numEdges, 
	Scalar& maxRes2, U32F& numInserted, bool& bMerge, bool& bSlidOff, bool& bInterrupt)
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);

	const Point& prevPnt = pPosOut[iPrevEdge];
	const Point& nextPnt = pPosOut[iNextEdge];

	bool bCanSlideOff = true;
	for (U32F ii = iPrevEdge; ii<=iNextEdge; ii++)
	{
		if (!AllComponentsEqual(pPosOut[ii], pEdges[ii].m_pos))
		{
			// If any point involved in the sliding off has already moved we need to check collisions before letting these edges go
			bCanSlideOff = false;
			break;
		}
	}

	const RopeColEdge** pUnfoldEdges = NDI_NEW const RopeColEdge*[numUnfoldEdges];
	bool* pSlideOff = NDI_NEW bool[numUnfoldEdges];

	for (U32F ii = 0; ii<numUnfoldEdges; ii++)
	{
		const EdgePoint& ePoint = pEdges[pUnfoldEdgePointIndices[ii]];
		U8 eIndex = pUnfoldEdgeIndices[ii];
		pUnfoldEdges[ii] = &m_colCache.m_pEdges[ePoint.m_edgeIndices[eIndex]];
	}

	// Check the "friction" to see if we want the rope to move in this group at all
	// We're using original edge positions here (not pPosOut) so resulting moveFricMult should always be the same unless the topology
	// of the edges has changed (sliding along different edge). Not sure if that is even possible during one iteration (one call to StraightenAlongEdges)
	F32 moveFricMult = 0.0f;
	for (U32F ii = 0; ii<numUnfoldEdges; ii++)	
	{
		if (ii > 0 && pUnfoldEdgePointIndices[ii-1] == pUnfoldEdgePointIndices[ii])
		{
			// Never stop from friction if we're at outer corner
			moveFricMult = 1.0f;
			break;
		}

		Point pos = pEdges[pUnfoldEdgePointIndices[ii]].m_pos;
		Point pp = ii > 0 ? pEdges[pUnfoldEdgePointIndices[ii-1]].m_pos : pEdges[iPrevEdge].m_pos;
		Point pn = ii < numUnfoldEdges-1 ? pEdges[pUnfoldEdgePointIndices[ii+1]].m_pos : pEdges[iNextEdge].m_pos;
		Vector edgeVec(pUnfoldEdges[ii]->m_vec);

		Vector dirp = pp-pos;
		Scalar dirpEdgeProj = Dot(dirp, edgeVec);
		Vector dirpPerp = dirp - dirpEdgeProj * edgeVec;
		Scalar dirpPerpProj;
		dirpPerp = SafeNormalize(dirpPerp, kZero, dirpPerpProj);

		Vector dirn = pn-pos;
		Scalar dirnEdgeProj = Dot(dirn, edgeVec);
		Vector dirnPerp = dirn - dirnEdgeProj * edgeVec;
		Scalar dirnPerpProj;
		dirnPerp = SafeNormalize(dirnPerp, kZero, dirnPerpProj);

		if (AllComponentsEqual(dirpPerp, kZero) | AllComponentsEqual(dirnPerp, kZero))
		{
			// Rope parallel to edge, ignore
			moveFricMult = 1.0f;
			break;
		}

		F32 frictionAngle = Acos(MinMax(-(F32)Dot(dirpPerp, dirnPerp), -1.0f, 1.0f));
		PHYSICS_ASSERT(IsFinite(frictionAngle));
		F32 noMoveAngle = Lerp(0.0f, g_ropeEdgeMaxNoMoveAngle, frictionAngle/PI);

		F32 anglep = Atan2(dirpEdgeProj, dirpPerpProj);
		F32 anglen = Atan2(dirnEdgeProj, dirnPerpProj);
		F32 angle = Abs(anglep + anglen);
		PHYSICS_ASSERT(IsFinite(angle));

		F32 f = LerpScale(noMoveAngle, 2.0f*noMoveAngle, 0.0f, 1.0f, angle);
		PHYSICS_ASSERT(IsFinite(f) && f>=0.0f && f<=1.0f);
		moveFricMult = Max(moveFricMult, f);
	}

	if (moveFricMult == 0.0f)
	{
		return;
	}

	memset(pSlideOff, 0, numUnfoldEdges*sizeof(bool));

	// Straighten rope over the unfold group
	if (numUnfoldEdges == 1)
	{
		const RopeColEdge& colEdge = *pUnfoldEdges[0];
		ProjectLineEdge(colEdge, prevPnt, nextPnt, pUnfoldPoints[0]);

		// Check sliding off the end
		U32F iEdgePoint = pUnfoldEdgePointIndices[0];

		pUnfoldPoints[0] = Lerp(pEdges[iEdgePoint].m_pos, pUnfoldPoints[0], Min(Scalar(1.0f), moveFricMult * g_ropeEdgeSlideSpeed * m_scStepTime));

		Vector edgeVec = Vector(colEdge.m_vec);
		Scalar edgeLen = colEdge.m_vec.W();
		Scalar tStart = Dot(pPosOut[iEdgePoint] - colEdge.m_pnt, edgeVec);
		Scalar t = Dot(pUnfoldPoints[0] - colEdge.m_pnt, edgeVec);
		if (colEdge.m_startTriIndex >= 0 && t < 0.0f)
		{
			// Stopped by edge collision trim
			t = 0.0f;
			pUnfoldPoints[0] = pUnfoldEdges[0]->m_pnt;
		}
		else if (colEdge.m_endTriIndex >= 0 && t > edgeLen)
		{
			// Stopped by edge collision trim
			t = edgeLen;
			pUnfoldPoints[0] = pUnfoldEdges[0]->m_pnt + t * edgeVec;
		}
		else if (t > tStart && t > edgeLen)
		{
			t = Max(tStart, edgeLen);
			pUnfoldPoints[0] = pUnfoldEdges[0]->m_pnt + t * edgeVec;
			pSlideOff[0] = true;
			bCanSlideOff = bCanSlideOff && tStart > edgeLen - kScEdgeTol;
		}
		else if (t < tStart && t < 0.0f)
		{
			t = Min(tStart, Scalar(kZero));
			pUnfoldPoints[0] = pUnfoldEdges[0]->m_pnt + t * edgeVec;
			pSlideOff[0] = true;
			bCanSlideOff = bCanSlideOff && tStart < kScEdgeTol;
		}
	}
	else
	{
		StraightenUnfoldEdges(pUnfoldEdges, pUnfoldEdgePointIndices, pEdges, pUnfoldPositive, numUnfoldEdges, prevPnt, nextPnt, 
			moveFricMult, pUnfoldPoints, pSlideOff, bMerge, bCanSlideOff);
	}

	// Check can move
	bool bCanMove = true;
	for (U32F ii = 0; ii<numUnfoldEdges; ii++)
	{
		U32F iEdgePoint = pUnfoldEdgePointIndices[ii];
		if (!AllComponentsEqual(pDir[iEdgePoint], Vector(kZero)))
		{
			Vector dirMove = Dot(pDir[iEdgePoint], pUnfoldPoints[ii] - pEdges[iEdgePoint].m_pos) * pDir[iEdgePoint];
			Vector perpMove = pUnfoldPoints[ii] - pEdges[iEdgePoint].m_pos - dirMove;
			if (LengthSqr(perpMove) > kScEdgeTol2)
			{
				bCanMove = false;
			}
		}
	}
	// Check sliding off
	if (bCanMove)
	{
		for (U32F iUnfold = 0; iUnfold<numUnfoldEdges; iUnfold++)
		{
			U32F iEdgePoint = pUnfoldEdgePointIndices[iUnfold];
			EdgePoint& ePoint = pEdges[iEdgePoint];
			EdgeIntersectionMatrix& intType = pIntType[iEdgePoint]; 
			if (pSlideOff[iUnfold])
			{
				U8 iEdgeIndex = pUnfoldEdgeIndices[iUnfold];
				// This outer thinks it's sliding off.
				// But! If it has a pair edge with topological type kIntInnerCorner0 (inner corner where rope slides away along edge 0) it means
				// that this is in fact an inner edge. Our initial topological evaluation just wasn't correct and now when we took into account all the other outer edges
				// at this point it turned out that we won't slide away along edge0 and we'll be stuck in this corner instead
				for (U32F iOtherEdgeIndex = 0; iOtherEdgeIndex<ePoint.m_numEdges; iOtherEdgeIndex++)
				{
					if (intType[iEdgeIndex][iOtherEdgeIndex] == kIntInnerCorner0)
					{
						pSlideOff[iUnfold] = false;
						ePoint.m_flags |= kNodeEdgeCorner;
						ePoint.SetEdgeActive(iOtherEdgeIndex, true);
					}
				}
				if (pSlideOff[iUnfold])
				{
					if (bCanSlideOff)
					{
						SlideOffEdge(ePoint, pUnfoldPoints[iUnfold], iEdgeIndex, intType);
						bSlidOff = true;
					}
					else
					{
						ePoint.m_internFlags |= kNodeSlideOffPending;
						bInterrupt = true; // we need to interrupt and check collisions
					}
				}
			}
			else if (iUnfold<numUnfoldEdges-1 && (iUnfold == 0 || pUnfoldEdgePointIndices[iUnfold-1] != iEdgePoint))
			{
				U32F iEdgePointNext = pUnfoldEdgePointIndices[iUnfold+1];
				if (iEdgePoint == iEdgePointNext && DistSqr(pUnfoldPoints[iUnfold], pUnfoldPoints[iUnfold+1]) > kScEdgeTol2)
				{
					// This point will be splitting

					// What edges in this point are other outer edges that are splitting off
					bool bOuterEdge[EdgePoint::kMaxNumEdges];
					for (U32F iEdgeIndex = 0; iEdgeIndex<ePoint.m_numEdges; iEdgeIndex++)
					{
						bOuterEdge[iEdgeIndex] = false;
						for (U32F ii = iUnfold; ii<numUnfoldEdges; ii++)
						{
							if (pUnfoldEdgePointIndices[ii] != iEdgePoint)
								break;
							if (pUnfoldEdgeIndices[ii] == iEdgeIndex)
							{
								bOuterEdge[iEdgeIndex] = true;
								break;
							}
						}
					}

					// Find all other related outer edges on "this" side
					for (U32F iEdgeIndex = 0; iEdgeIndex<ePoint.m_numEdges; iEdgeIndex++)
					{
						if (bOuterEdge[iEdgeIndex])
							continue;
						for (U32F ii = 0; ii<ePoint.m_numEdges; ii++)
						{
							if (bOuterEdge[ii])
							{
								switch (intType[iEdgeIndex][ii])
								{
									case kIntOuterCorner01:
									case kIntOuterCorner10:
									case kIntSame:
										// If we're making outer corenr with edge on this side it should be outer edge too
										// Or if it's same of course
										bOuterEdge[iEdgeIndex] = true;
										break;
								}
							}
						}
					}

					// Some edges will need to be marked slideOff because we are splitting and moving away from them (they are on "the other" side)
					bool bSlideOffEdge[EdgePoint::kMaxNumEdges];
					for (U32F iEdgeIndex = 0; iEdgeIndex<ePoint.m_numEdges; iEdgeIndex++)
					{
						bSlideOffEdge[iEdgeIndex] = false;
						if (bOuterEdge[iEdgeIndex])
							continue;
						for (U32F ii = 0; ii<ePoint.m_numEdges; ii++)
						{
							switch (intType[iEdgeIndex][ii])
							{
								case kIntInnerCorner0:
								case kIntInnerCorner01:
									// If this edge was making an unstable inner corner with one of the outer edges we can slide off it
									if (bOuterEdge[ii])
									{
										bSlideOffEdge[iEdgeIndex] = true;
									}
									break;
								case kIntOuterCorner01:
								case kIntOuterCorner10:
									// If this edge was making an outer corner with an edge that is not participating in this outer corner group
									// that should mean it's the "other side" outer corner and we are sliding of that one
									if (!bOuterEdge[ii])
									{
										bSlideOffEdge[iEdgeIndex] = true;
									}
									break;
							}
						}
					}

					for (U32F iEdgeIndex = 0; iEdgeIndex<ePoint.m_numEdges; iEdgeIndex++)
					{
						if (bSlideOffEdge[iEdgeIndex])
						{
							if (bCanSlideOff)
							{
								SlideOffEdge(ePoint, pUnfoldPoints[iUnfold], iEdgeIndex, intType);
								bSlidOff = true;
							}
							else
							{
								ePoint.m_internFlags |= kNodeSlideOffPending;
								bInterrupt = true; // we need to interrupt and check collisions
							}
						}
					}
				}
			}
		}

		// Move/split
		numInserted = 0;
		for (U32F iUnfold = 0; iUnfold<numUnfoldEdges; iUnfold++)
		{
			U32F iEdgePoint = pUnfoldEdgePointIndices[iUnfold] + numInserted;

			if (iUnfold<numUnfoldEdges-1)
			{
				U32F iEdgePointNext = pUnfoldEdgePointIndices[iUnfold+1] + numInserted;
				if (iEdgePoint == iEdgePointNext && DistSqr(pUnfoldPoints[iUnfold], pUnfoldPoints[iUnfold+1]) > kScEdgeTol2)
				{
					// Split
					JAROS_ASSERT(!pSlideOff[iUnfold]);
					JAROS_ALWAYS_ASSERT(numEdges < m_maxNumPoints);
					bool noMoreSpace = true;
					if (numEdges < m_maxNumPoints)
					{
						noMoreSpace = false;
						memmove(pEdges+iEdgePoint+1, pEdges+iEdgePoint, (numEdges-iEdgePoint)*sizeof(pEdges[0]));
						memmove(pPosOut+iEdgePoint+1, pPosOut+iEdgePoint, (numEdges-iEdgePoint)*sizeof(pPosOut[0]));
						memmove(pDir+iEdgePoint+1, pDir+iEdgePoint, (numEdges-iEdgePoint)*sizeof(pDir[0]));
						memmove(pIntType+iEdgePoint+1, pIntType+iEdgePoint, (numEdges-iEdgePoint)*sizeof(pIntType[0]));
						numEdges++;
					}

					EdgePoint& newEPoint = pEdges[iEdgePoint];
					EdgePoint& ePoint = pEdges[iEdgePoint + (noMoreSpace ? 0 : 1)];

					U8 splitEdgeIndex; 
					{
						// We can't use pUnfoldEdgeIndices indices here because we may have already split this point and indices have shuffled
						const RopeColEdge* pEdge = pUnfoldEdges[iUnfold];
						for (splitEdgeIndex = 0; splitEdgeIndex<ePoint.m_numEdges; splitEdgeIndex++)
						{
							if (&m_colCache.m_pEdges[ePoint.m_edgeIndices[splitEdgeIndex]] == pEdge)
								break;
						}
					}
					ALWAYS_ASSERT(splitEdgeIndex < ePoint.m_numEdges);
					JAROS_ASSERT(!ePoint.GetEdgeSlideOff(splitEdgeIndex));

					if (!noMoreSpace)
					{
						newEPoint.Reset();
						newEPoint.m_flags = ePoint.m_flags;
						newEPoint.m_internFlags = ePoint.m_internFlags | kNodeSplit;
						newEPoint.m_edgeIndices[0] = ePoint.m_edgeIndices[splitEdgeIndex];
						newEPoint.m_numEdges = 1;
						newEPoint.SetEdgeActive(0, true);
						newEPoint.SetEdgePositive(0, pUnfoldPositive[iUnfold]);
						newEPoint.SetEdgeSlideOff(0, pSlideOff[iUnfold]);
						newEPoint.m_pos = ePoint.m_pos;
						newEPoint.m_ropeDist = ePoint.m_ropeDist; // does it matter at all?
					}
					ePoint.RemoveEdge(splitEdgeIndex);

					//	See what other edges should go into the new edge point
					U32F iEdgeIndex = 0;
					while (iEdgeIndex<ePoint.m_numEdges)
					{
						bool bSplitsAlso = false;
						bool bOuterEdge = false;
						if (!ePoint.GetEdgeSlideOff(iEdgeIndex))
						{
							const RopeColEdge* pEdge = &m_colCache.m_pEdges[ePoint.m_edgeIndices[iEdgeIndex]];
							for (U32F ii = iUnfold+1; ii<numUnfoldEdges; ii++)
							{
								if (pUnfoldEdgePointIndices[ii]+numInserted != iEdgePoint)
									break;
								if (pUnfoldEdges[ii] == pEdge)
								{
									bOuterEdge = true;
									break;
								}
							}
							//if (intType[splitEdgeIndex][iEdgeIndex] == kIntSame)
							//{
							//	bSplitsAlso = true;
							//}
							//else if (intType[splitEdgeIndex][iEdgeIndex] != kIntOuterCorner01 && intType[splitEdgeIndex][iEdgeIndex] != kIntOuterCorner10)
							if (!bOuterEdge)
							{
								Vector vec0 = pUnfoldPoints[iUnfold] - pEdge->m_pnt;
								Scalar dist0 = LengthSqr(vec0 - Dot(vec0, Vector(pEdge->m_vec))*Vector(pEdge->m_vec));
								Vector vec1 = pUnfoldPoints[iUnfold+1] - pEdge->m_pnt;
								Scalar dist1 = LengthSqr(vec1 - Dot(vec1, Vector(pEdge->m_vec))*Vector(pEdge->m_vec));
								bSplitsAlso = dist0 < dist1;
							}
						}
						if (bSplitsAlso)
						{
							U32F newIndex = newEPoint.m_numEdges;
							if (!noMoreSpace)
							{
								newEPoint.m_edgeIndices[newIndex] = ePoint.m_edgeIndices[iEdgeIndex];
								newEPoint.m_numEdges++;
								newEPoint.SetEdgeActive(newIndex, ePoint.GetEdgeActive(iEdgeIndex));
								newEPoint.SetEdgePositive(newIndex, ePoint.GetEdgePositive(iEdgeIndex));
								newEPoint.SetEdgeSlideOff(newIndex, ePoint.GetEdgeSlideOff(iEdgeIndex));
							}
							ePoint.RemoveEdge(iEdgeIndex);
						}
						else
						{
							iEdgeIndex++;
						}
					}
					ASSERT(ePoint.m_activeEdges);
					ePoint.m_activeEdges0 = ePoint.m_activeEdges;
					if (!noMoreSpace)
					{
						newEPoint.m_activeEdges0 = newEPoint.m_activeEdges;
						numInserted++;
					}

					// If this point is splitting and at the same time merging with previous or next we have to make sure we only leave the merge flag on the correct split-off edge point
					if (newEPoint.m_internFlags & kNodeMerge)
					{
						if (iUnfold == 0 || DistSqr(pUnfoldPoints[iUnfold-1], pUnfoldPoints[iUnfold]) > kScEdgeTol2)
						{
							newEPoint.m_internFlags &= ~kNodeMerge;
						}
					}
					if (ePoint.m_internFlags & kNodeMerge)
					{
						if (iUnfold+1 == numUnfoldEdges-1 || DistSqr(pUnfoldPoints[iUnfold+1], pUnfoldPoints[iUnfold+2]) > kScEdgeTol2)
						{
							ePoint.m_internFlags &= ~kNodeMerge;
						}
					}
				}
			}

			if (AllComponentsEqual(pDir[iEdgePoint], kZero))
			{
				Vector dir = pUnfoldPoints[iUnfold] - pEdges[iEdgePoint].m_pos;
				Scalar dirLen2 = LengthSqr(dir);
				if (dirLen2 > kScEdgeTol2)
				{
					pDir[iEdgePoint] = dir * RecipSqrt(dirLen2);
				}
			}

			F32 res2 = DistSqr(pUnfoldPoints[iUnfold], pPosOut[iEdgePoint]);
			if (res2 > maxRes2)
			{
				maxRes2 = res2;
			}
			pPosOut[iEdgePoint] = pUnfoldPoints[iUnfold];
			PHYSICS_ASSERT(IsFinite(pPosOut[iEdgePoint]));
		}					
	}
}

bool Rope2::UpdatePointActiveEdges(EdgePoint& ePoint, const Point& prevPnt, const Point& nextPnt, U16& invalidEdges)
{
	invalidEdges = 0;
	for (U32F iPointEdge = 0; iPointEdge<ePoint.m_numEdges; iPointEdge++)
	{
		const RopeColEdge* pEdge = &m_colCache.m_pEdges[ePoint.m_edgeIndices[iPointEdge]];
		bool positive = ePoint.GetEdgePositive(iPointEdge);
		bool active = ePoint.GetEdgeActive(iPointEdge);
		bool invalid;
		TestEdgeLineCollision(*pEdge, active, positive, invalid, prevPnt, nextPnt);
		ePoint.SetEdgeActive(iPointEdge, active);
		ePoint.SetEdgePositive(iPointEdge, positive);
		if (invalid)
		{
			invalidEdges |= (1U << iPointEdge);
		}
	}
	return ePoint.m_activeEdges != 0;
}

bool Rope2::UpdatePointActiveNewEdges(EdgePoint& ePoint, const Point& prevPnt, const Point& nextPnt)
{
	for (U32F iPointEdge = ePoint.m_numEdges - ePoint.m_numNewEdges; iPointEdge<ePoint.m_numEdges; iPointEdge++)
	{
		const RopeColEdge* pEdge = &m_colCache.m_pEdges[ePoint.m_edgeIndices[iPointEdge]];
		bool positive = ePoint.GetEdgePositive(iPointEdge);
		bool active = ePoint.GetEdgeActive(iPointEdge);
		bool invalid;
		TestEdgeLineCollision(*pEdge, active, positive, invalid, prevPnt, nextPnt);
		ePoint.SetEdgeActive(iPointEdge, active);
		ePoint.SetEdgePositive(iPointEdge, positive);
	}
	return ePoint.m_activeEdges != 0;
}

bool Rope2::StraightenAlongEdges(U32F& numEdges, EdgePoint* pEdges, Point* pPosOut)
{
	Scalar maxRes2;
	bool bMerge = false;
	bool bSlidOff = false;
	bool bLastIter = false;
	U32F iterCnt = 0;
	bool bInterrupt = false;

	for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		pPosOut[ii] = pEdges[ii].m_pos;
	}

	// Update active flag on all new edges 
	{
		U32F iPrev = 0;
		U32F iNext = 0;
		while (iNext < numEdges-1)
		{
			U32F iPrevPrev = iPrev;
			iPrev = iNext;
			for (iNext++; iNext<numEdges-1; iNext++)
			{
				U32 oldEdgesMask = (1U << (pEdges[iNext].m_numEdges - pEdges[iNext].m_numNewEdges)) - 1;
				if (pEdges[iNext].m_activeEdges & oldEdgesMask)
					break;
			}

			// Update new edges on a point that already was active
			if (iPrev > 0)
			{
				Point prevPnt = pPosOut[iPrevPrev];
				Point nextPnt = pPosOut[iNext];
				UpdatePointActiveNewEdges(pEdges[iPrev], prevPnt, nextPnt);
			}

			// Update all new edges on inactive points in between
			{
				Point prevPnt = pPosOut[iPrev];
				Point nextPnt = pPosOut[iNext];
				for (U32F ii = iPrev+1; ii<iNext; ii++)
				{
					UpdatePointActiveNewEdges(pEdges[ii], prevPnt, nextPnt);
				}
			}
		}
	}

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	bool bSlideOffIter = false;
	for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		pEdges[ii].m_activeEdges0 = pEdges[ii].m_activeEdges;
		if (pEdges[ii].m_internFlags & kNodeSlideOffPending)
		{
			// We will run the first iter without moving anything just to check for slide off situation
			bSlideOffIter = true;
		}
		pEdges[ii].m_slideOff = 0;
		pEdges[ii].m_numNewEdges = 0;
	}

	EdgeIntersectionMatrix* pIntType = NDI_NEW EdgeIntersectionMatrix[m_maxNumPoints];

	Vector* pDir = NDI_NEW Vector[m_maxNumPoints];
	memset(pDir, 0, numEdges * sizeof(pDir[0]));

	U32F numUnfoldEdges = 0;
	U8* pUnfoldEdgeIndices = NDI_NEW U8[m_maxNumPoints];
	U32F* pUnfoldEdgePointIndices = NDI_NEW U32F[m_maxNumPoints];
	bool* pUnfoldPositive = NDI_NEW bool[m_maxNumPoints];
	Point* pUnfoldPoints = NDI_NEW Point[m_maxNumPoints];
	U32F iUnfoldPrev = 0;
	U32F iUnfoldNext = 0;

	do 
	{
		// Check inactive points going active
		/*{
			U32F iPrev = 0;
			U32F iNext = 0;
			while (iNext < numEdges-1)
			{
				iPrev = iNext;
				for (iNext++; iNext<numEdges-1; iNext++)
				{
					if (pEdges[iNext].m_activeEdges)
						break;
				}

				{
					Point prevPnt = pPosOut[iPrev];
					Point nextPnt = pPosOut[iNext];
					for (U32F ii = iPrev+1; ii<iNext; ii++)
					{
						UpdatePointActiveEdges(pEdges[ii], prevPnt, nextPnt);
						pEdges[ii].m_activeEdges &= ~pEdges[ii].m_slideOff;
						if (pEdges[ii].m_activeEdges)
						{
							iNext = ii;
							break;
						}
					}
				}
			}
		}*/

		maxRes2 = Scalar(kZero);

		bSlidOff = false;

		bool bSkippedPoint = false;

		U32F iPrev = 0;
		U32F iNext = 1;
		for (iNext = 1; iNext<numEdges-1; iNext++)
		{
			if (pEdges[iNext].m_activeEdges|pEdges[iNext].m_activeEdges0)
				break;
		}

		// Check edges we just passed to see if any of them has just become active
		{
			Point prevPnt = pPosOut[iPrev];
			Point nextPnt = pPosOut[iNext];
			for (U32F ii = iPrev+1; ii<iNext; ii++)
			{
				U16 invalidEdges;
				UpdatePointActiveEdges(pEdges[ii], prevPnt, nextPnt, invalidEdges);
				pEdges[ii].m_activeEdges &= ~pEdges[ii].m_slideOff;
				if (pEdges[ii].m_activeEdges)
				{
					pEdges[ii].m_internFlags |= kNodeNewActivation;
					iNext = ii;
					break;
				}
			}
		}

		while (iNext<numEdges-1)
		{
			U32F iPoint = iNext;
			for (iNext++; iNext<numEdges-1; iNext++)
			{
				if (pEdges[iNext].m_activeEdges|pEdges[iNext].m_activeEdges0)
					break;
			}

			const Point prevPnt = pPosOut[iPrev];
			const Point thisPnt = pPosOut[iPoint];
			Point nextPnt = pPosOut[iNext];

			// Check edges we just passed to see if any of them has just become active
			for (U32F ii = iPoint+1; ii<iNext; ii++)
			{
				U16 invalidEdges;
				UpdatePointActiveEdges(pEdges[ii], thisPnt, nextPnt, invalidEdges);
				pEdges[ii].m_activeEdges &= ~pEdges[ii].m_slideOff;
				if (pEdges[ii].m_activeEdges)
				{
					pEdges[ii].m_internFlags |= kNodeNewActivation;
					iNext = ii;
					nextPnt = pPosOut[iNext];
					break;
				}
			}

			if (bMerge && (pEdges[iPoint].m_internFlags & kNodeMerge))
			{
				// We will skip updating active edges on edge points that are merging
				// because that would pretty much always turn all its edges into inactive state
				iPrev = iPoint;
				continue;
			}

			// Check active edges of this point
			U16 invalidEdges;
			EdgePoint& ePoint = pEdges[iPoint];
			UpdatePointActiveEdges(ePoint, prevPnt, nextPnt, invalidEdges);

			// Get the edge topology for each edge pair
			EdgeIntersectionMatrix& intType = pIntType[iPoint];
			memset(&intType[0][0], 0, sizeof(intType));
			Point intPoint[EdgePoint::kMaxNumEdges][EdgePoint::kMaxNumEdges];
			for (U32F iPointEdge = 0; iPointEdge<ePoint.m_numEdges; iPointEdge++)
			{
				bool invalid0 = (invalidEdges & (1U << iPointEdge));
				if (!invalid0)
				{
					bool active0 = (ePoint.m_activeEdges & (1U << iPointEdge));
					const RopeColEdge* pEdge0 = &m_colCache.m_pEdges[ePoint.m_edgeIndices[iPointEdge]];
					bool positive0 = ePoint.GetEdgePositive(iPointEdge);
					for (U32F iPointEdgeOther = 0; iPointEdgeOther<iPointEdge; iPointEdgeOther++)
					{
						bool invalid1 = (invalidEdges & (1U << iPointEdgeOther));
						if (!invalid1)
						{
							bool active1 = (ePoint.m_activeEdges & (1U << iPointEdgeOther));
							const RopeColEdge* pEdge1 = &m_colCache.m_pEdges[ePoint.m_edgeIndices[iPointEdgeOther]];
							bool positive1 = ePoint.GetEdgePositive(iPointEdgeOther);

							ClasifyEdgeIntersection(pEdge0, positive0, pEdge1, positive1, prevPnt, nextPnt, 
								intPoint[iPointEdge][iPointEdgeOther], intType[iPointEdge][iPointEdgeOther]);

							if (intType[iPointEdge][iPointEdgeOther] == kIntInnerCorner)
							{
								// This is inner corner. We will force both edges to be active because occasionally they may seem inactive individually but as a corner they are both well active
								ePoint.SetEdgeActive(iPointEdge, true);
								active0 = true;
								ePoint.SetEdgeActive(iPointEdgeOther, true);
								active1 = true;
							}
							else if (!active0 || !active1)
							{
								// Otherwise ignore this pair if one of the edges is inactive
								intType[iPointEdge][iPointEdgeOther] = kIntNone;
							}
							intType[iPointEdgeOther][iPointEdge] = s_switchedEdgeInteresection[intType[iPointEdge][iPointEdgeOther]];
							intPoint[iPointEdgeOther][iPointEdge] = intPoint[iPointEdge][iPointEdgeOther];
							switch (intType[iPointEdge][iPointEdgeOther])
							{
							case kIntInnerT0:
								{
									// Edge 0 is a T edge and can be deactivated
									ePoint.m_activeEdges &= ~(1U << iPointEdge);
									active0 = false;
									break;
								}
							case kIntInnerT1:
								{
									// Edge 1 is a T edge and can be deactivated
									ePoint.m_activeEdges &= ~(1U << iPointEdgeOther);
									active1 = false;
									break;
								}
							}
						}
					}
				}
			}

			// Exclude all edges we have slid off 
			ePoint.m_activeEdges &= ~ePoint.m_slideOff;

			// Process edge topology at this point for all still active edges
			U16 canSlideAlongEdge = ePoint.m_activeEdges;
			bool bInnerCorner = false;
			Point innerCornerPoint;
			U8 outerCornerEdges[EdgePoint::kMaxNumEdges];
			U32F numOuterCornerEdges = 0;
			for (U32F iPointEdge = 0; iPointEdge<ePoint.m_numEdges; iPointEdge++)
			{
				bool active0 = ePoint.GetEdgeActive(iPointEdge);
				for (U32F iPointEdgeOther = 0; iPointEdgeOther<ePoint.m_numEdges; iPointEdgeOther++)
				{
					bool active1 = ePoint.GetEdgeActive(iPointEdgeOther);
					if (active0 && active1)
					{
						switch (intType[iPointEdge][iPointEdgeOther])
						{
						case kIntInnerCorner:
							{
								innerCornerPoint = intPoint[iPointEdge][iPointEdgeOther];
								bInnerCorner = true;
							}
							break;
						case kIntInnerCorner0:
							{
								canSlideAlongEdge &= ~(1U << iPointEdgeOther);
								break;
							}
						case kIntOuterCorner01:
						case kIntOuterCorner10:
							{
								//ASSERT(ePoint.GetEdgeSlideOff(iPointEdge) == ePoint.GetEdgeSlideOff(iPointEdgeOther));
								//if (!ePoint.GetEdgeSlideOff(iPointEdge) && !ePoint.GetEdgeSlideOff(iPointEdgeOther))
								{
									U32F ii;
									for (ii = 0; ii<numOuterCornerEdges; ii++)
									{
										if (outerCornerEdges[ii] == iPointEdge)
											break;
									}
									if (ii == numOuterCornerEdges)
									{
										outerCornerEdges[ii] = iPointEdge;
										numOuterCornerEdges++;
									}
								}
							}
							break;
						}
						if (bInnerCorner)
							break;
					}
				}
				if (bInnerCorner)
					break;
			}

			// Update corner flag
			if (bInnerCorner)
				ePoint.m_flags |= kNodeEdgeCorner;
			else
				ePoint.m_flags &= ~kNodeEdgeCorner;

			if (!bInnerCorner && numOuterCornerEdges)
			{
				// If we have outer corners we need to sort the edges as they go along the rope
				SortOuterCornerEdges(ePoint, intType, outerCornerEdges, numOuterCornerEdges, canSlideAlongEdge);
			}

			// Edges we cannot slide along are now marked inactive so they can be removed if we move away from them
			ePoint.m_activeEdges = canSlideAlongEdge;
			
			if (!ePoint.m_activeEdges)
				ePoint.m_internFlags &= ~kNodeNewActivation;

			if (!AllComponentsEqual(pPosOut[iPrev], pEdges[iPrev].m_pos) || !AllComponentsEqual(pPosOut[iNext], pEdges[iNext].m_pos))
			{
				// Do we need to interrupt and check collision before continue?
				if ((ePoint.m_activeEdges0 & ePoint.m_activeEdges) != ePoint.m_activeEdges0)
				{
					// We just lost an edge
					bInterrupt = true;
				}
				else if (!ePoint.m_activeEdges)
				{
					// Hm ... we thought this point is going active but it's not. Can actually happen?
					// Yes, this can happen for example: If we're separating from an edge. But after our previous move (during which the edge was deemed inactive)
					// we collided and had to come back. The edge was marked active again at the start of this move but then we continued our separation
					// (because the collision wasn't significant) and the edge became inactive again
					//JAROS_ASSERT(false); 
					bInterrupt = true;
				}
			}

			if (!ePoint.m_activeEdges)
			{
				ePoint.m_slideOff = (1UL << ePoint.m_numEdges) - 1; // mark all as slid-off so that we don't activate them again
				iPrev = iPoint;
				//maxRes2 = 1.0f; // make sure we iterate again if we just released this edge point
				bSkippedPoint = true;
				continue;
			}

			if (bLastIter || bInterrupt)
			{
				if ((ePoint.m_internFlags & kNodeNewActivation) == 0)
				{
					// Don't skip point if this is a newly activated point
					// For newly actived points we need to get the point on the edge otherwise we may loose that edge if the point is not close to it enough
					iPrev = iPoint;
					bSkippedPoint = true;
					continue;
				}
			}

			if (bSlideOffIter)
			{
				if ((pEdges[iPoint].m_internFlags & kNodeSlideOffPending) == 0)
				{
					iPrev = iPoint;
					bSkippedPoint = true;
					continue;
				}
				else
				{
					pEdges[iPoint].m_internFlags &= ~kNodeSlideOffPending;
				}
			}

			// So are we going to slide along an edge?
			I32 firstSlidingEdge = -1;
			if (!bInnerCorner && numOuterCornerEdges)
			{
				firstSlidingEdge = outerCornerEdges[0];
			}
			else if (!bInnerCorner && canSlideAlongEdge)
			{
				// Find first edge that we can use and slide along it
				for (U32F iPointEdge = 0; iPointEdge<ePoint.m_numEdges; iPointEdge++)
				{
					if (canSlideAlongEdge & (1U << iPointEdge))
					{
						firstSlidingEdge = iPointEdge;
						break;
					}
				}
			}
			ASSERT(bInnerCorner || firstSlidingEdge >= 0);

			// Check if we can add edges from this point into the unfold group
			bool addUnfold = false;
			if (!bSkippedPoint && firstSlidingEdge >= 0 && !bSlideOffIter)
			{
				const RopeColEdge* pEdge = &m_colCache.m_pEdges[ePoint.m_edgeIndices[firstSlidingEdge]];
				if (numUnfoldEdges)
				{
					const EdgePoint& prevUnfoldEPoint = pEdges[pUnfoldEdgePointIndices[numUnfoldEdges-1]];
					U8 prevUnfoldEIndex = pUnfoldEdgeIndices[numUnfoldEdges-1];
					const RopeColEdge* pPrevEdge = &m_colCache.m_pEdges[prevUnfoldEPoint.m_edgeIndices[prevUnfoldEIndex]];
					Vector vec0 = Vector(pPrevEdge->m_vec);
					Vector vec1 = Vector(pEdge->m_vec);
					Vector plNorm = SafeNormalize(Cross(vec0, vec1), kZero);
					if (!AllComponentsEqual(plNorm, kZero))
					{
						Scalar dist = Dot(pPrevEdge->m_pnt - pEdge->m_pnt, plNorm); 
						addUnfold = Abs(dist) < kScEdgeTol;
					}
					else
					{
						// @@JS: Handle (close) parallel edges
					}
				}
			}

			if (bInnerCorner && !bSlideOffIter)
			{
				// Just move the edge point to the inner corner
				bool bCanMove = true;
				if (AllComponentsEqual(pDir[iPoint], kZero))
				{
					Vector dir = innerCornerPoint - pEdges[iPoint].m_pos;
					Scalar dirLen2 = LengthSqr(dir);
					if (dirLen2 > kScEdgeTol2)
					{
						pDir[iPoint] = dir * RecipSqrt(dirLen2);
					}
				}
				else
				{
					Vector dirMove = Dot(pDir[iPoint], innerCornerPoint - pEdges[iPoint].m_pos) * pDir[iPoint];
					Vector perpMove = innerCornerPoint - pEdges[iPoint].m_pos - dirMove;
					if (LengthSqr(perpMove) > kScEdgeTol2)
					{
						bCanMove = false;
					}
				}

				if (bCanMove)
				{
					F32 res2 = DistSqr(innerCornerPoint, pPosOut[iPoint]);
					if (res2 > maxRes2)
					{
						maxRes2 = res2;
					}
					pPosOut[iPoint] = innerCornerPoint;
					PHYSICS_ASSERT(IsFinite(pPosOut[iPoint]));
				}
			}

			if (!addUnfold && numUnfoldEdges)
			{
				// Straighten rope over the unfold group
				U32F numInserted = 0;
				ProcessUnfoldEdgesGroup(numUnfoldEdges, pUnfoldEdgeIndices, pUnfoldEdgePointIndices, pUnfoldPositive, pUnfoldPoints,
					iUnfoldPrev, iUnfoldNext, pEdges, pPosOut, pDir, pIntType, numEdges, maxRes2, numInserted, bMerge, bSlidOff, bInterrupt);
				iPrev += numInserted;
				iPoint += numInserted;
				iNext += numInserted;
				numUnfoldEdges = 0;
			}

			if (!bInnerCorner)
			{
				ASSERT(firstSlidingEdge >= 0);
				ALWAYS_ASSERT(numUnfoldEdges < m_maxNumPoints);
				if (numUnfoldEdges == 0)
					iUnfoldPrev = iPrev;
				iUnfoldNext = iNext;
				const EdgePoint& ePoint2 = pEdges[iPoint];
				pUnfoldEdgeIndices[numUnfoldEdges] = firstSlidingEdge;
				pUnfoldEdgePointIndices[numUnfoldEdges] = iPoint;
				pUnfoldPositive[numUnfoldEdges] = ePoint2.GetEdgePositive(firstSlidingEdge);
				pUnfoldPoints[numUnfoldEdges] = pPosOut[iPoint];
				numUnfoldEdges++;

				for (U32F ii = 1; ii<numOuterCornerEdges; ii++)
				{
					ALWAYS_ASSERT(numUnfoldEdges < m_maxNumPoints);
					pUnfoldEdgeIndices[numUnfoldEdges] = outerCornerEdges[ii];
					pUnfoldEdgePointIndices[numUnfoldEdges] = iPoint;
					pUnfoldPositive[numUnfoldEdges] = ePoint2.GetEdgePositive(outerCornerEdges[ii]);
					pUnfoldPoints[numUnfoldEdges] = pPosOut[iPoint];
					numUnfoldEdges++;
				}
			}

			bSkippedPoint = false;
			iPrev = iPoint;
		}

		if (numUnfoldEdges)
		{
			// Straighten rope over the unfold group
			U32F numInserted = 0;
			ProcessUnfoldEdgesGroup(numUnfoldEdges, pUnfoldEdgeIndices, pUnfoldEdgePointIndices, pUnfoldPositive, pUnfoldPoints,
				iUnfoldPrev, iUnfoldNext, pEdges, pPosOut, pDir, pIntType, numEdges, maxRes2, numInserted, bMerge, bSlidOff, bInterrupt);
			numUnfoldEdges = 0;
		}

		if (!bSlideOffIter && !bSlidOff && (bInterrupt || bMerge || maxRes2 <= 0.001f))
		{
			// If we have moved we want to do one last iter to refresh active edges but not move any point
			// so that we don't miss any edge that would just become active
			if (bLastIter || maxRes2 == 0.0f)
				break;
			else
				bLastIter = true;
		}

		if (bSlideOffIter)
		{
			for (U32F ii = 1; ii<numEdges-1; ii++)
			{
				// If we slide off during the slideOffIter we can mark those edges as inactive from start
				// That allows edge points that slide off and have no more active edges left to be omitted from further iterations
				pEdges[ii].m_activeEdges0 &= ~pEdges[ii].m_slideOff;
			}
			bSlideOffIter = false;
		}

		if (++iterCnt > numEdges*10)
		{
#ifdef JSINECKY
			ASSERT(false);
			if (iterCnt > numEdges*10+2)
				break;
#else
			break;
#endif
		}
	} while (true);

	// Project inactive edge points
	U32F iPrev = 0;
	U32F iNext = 1;
	for (iNext = 1; iNext<numEdges-1; iNext++)
	{
		if (pEdges[iNext].m_activeEdges|pEdges[iNext].m_activeEdges0)
			break;
	}
	do 
	{
		if (iNext > iPrev+1)
		{
			Vector vec = pPosOut[iNext] - pPosOut[iPrev];
			Scalar vecLen;
			vec = SafeNormalize(vec, kZero, vecLen);
			//JAROS_ASSERT(vecLen > 0.002f);
			for (U32F ii = iPrev+1; ii<iNext; ii++)
			{
				Scalar dt;
				bool closestOnEdge = false;
				if (pEdges[ii].m_numEdges)
				{
					// Get closest point on the first inactive edge
					const RopeColEdge& colEdge = m_colCache.m_pEdges[pEdges[ii].m_edgeIndices[0]];
					if (LineLineIntersection(colEdge.m_pnt, Vector(colEdge.m_vec), pPosOut[iPrev], vec, dt))
					{
						closestOnEdge = true;
					}
				}
				if (!closestOnEdge)
				{
					// If there is no edge just project
					dt = Dot(pPosOut[ii]-pPosOut[iPrev], vec);
				}
				dt = MinMax(dt, Scalar(kZero), vecLen);
				pPosOut[ii] = pPosOut[iPrev] + vec * dt;
				PHYSICS_ASSERT(IsFinite(pPosOut[ii]));
			}
#if 0
			Scalar ropeDist = pEdges[iNext].m_ropeDist - pEdges[iPrev].m_ropeDist;
			if (ropeDist*vecLen == 0.0f)
			{
				for (U32F ii = iPrev+1; ii<iNext; ii++)
				{
					pPosOut[ii] = pPosOut[iPrev];
				}
			}
			else
			{
				for (U32F ii = iPrev+1; ii<iNext; ii++)
				{
					Scalar dt = (pEdges[ii].m_ropeDist - pEdges[iPrev].m_ropeDist) / ropeDist * vecLen;
					JAROS_ASSERT(dt >= 0.0f && dt <= vecLen);
					dt = MinMax(dt, Scalar(kZero), vecLen);
					pPosOut[ii] = pPosOut[iPrev] + vec * dt;
					PHYSICS_ASSERT(IsFinite(pPosOut[ii]));
				}
			}
#endif
		}
		iPrev = iNext;
		for (iNext++; iNext<numEdges-1; iNext++)
		{
			if (pEdges[iNext].m_activeEdges|pEdges[iNext].m_activeEdges0)
				break;
		}
	} while (iNext<numEdges);

	for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		if (pEdges[ii].m_activeEdges0 && !pEdges[ii].m_activeEdges)
		{
			bInterrupt = true;
		}
	}

	return !bInterrupt && maxRes2 < 0.001f;
}

void Rope2::BackStepEdges(U32F numEdges, EdgePoint* pEdges, const Point* pPos0, const Point* pPos1)
{
	Scalar maxRes2;
	U32F iterCnt = 0;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	Point* pSavePos = NDI_NEW Point[numEdges];
	Vector* pDir = NDI_NEW Vector[numEdges];
	Scalar* pLastMove2 = NDI_NEW Scalar[numEdges];
	for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		pSavePos[ii] = pEdges[ii].m_pos;
		pDir[ii] = SafeNormalize(pPos1[ii] - pPos0[ii], kZero);
		pLastMove2[ii] = DistSqr(pEdges[ii].m_pos, pPos1[ii]);
	}
	pLastMove2[0] = Scalar(kZero);
	pLastMove2[numEdges-1] = Scalar(kZero);

	// Back step edge points that have active edges
	do 
	{
		maxRes2 = 0.0f;

		U32F iPrev = 0;
		U32F iNext = 1;
		for (iNext = 1; iNext<numEdges-1; iNext++)
		{
			if (pEdges[iNext].m_activeEdges|pEdges[iNext].m_activeEdges0)
				break;
		}

		while (iNext<numEdges-1)
		{
			U32F iPoint = iNext;
			for (iNext++; iNext<numEdges-1; iNext++)
			{
				if (pEdges[iNext].m_activeEdges|pEdges[iNext].m_activeEdges0)
					break;
			}

			// Back step if this point has any trajectory and any of its neighbors had to back step
			if (!AllComponentsEqual(pDir[iPoint], Vector(kZero)) && (pLastMove2[iPrev] > 0.0001f || pLastMove2[iNext] > 0.0001f))
			{
				Point thisPnt = pEdges[iPoint].m_pos;
				Point newPoint = thisPnt;
				/*if ((pEdges[iPoint].m_activeEdges0 & pEdges[iPoint].m_activeEdges) != pEdges[iPoint].m_activeEdges0)
				{
					// We have lost some edge and some neighbor have moved. We need to go all the way back because we may need back some of the edges
					pEdges[iPoint].m_activeEdges |= pEdges[iPoint].m_activeEdges0;
					newPoint = pPos0[iPoint];
				}
				else*/
				{
					Scalar oldT = Dot(thisPnt - pPos0[iPoint], pDir[iPoint]);
					if (oldT > 0.0f)
					{
						Point prevPnt = pEdges[iPrev].m_pos;
						Point nextPnt = pEdges[iNext].m_pos;
						ProjectLineEdge(pPos0[iPoint], pDir[iPoint], prevPnt, nextPnt, newPoint);
						Scalar newT = Dot(newPoint - pPos0[iPoint], pDir[iPoint]);
						newT = MinMax(newT, Scalar(kZero), oldT);
						newPoint = pPos0[iPoint] + newT * pDir[iPoint];
					}
				}

				PHYSICS_ASSERT(IsFinite(newPoint));
				Scalar res2 = DistSqr(newPoint, thisPnt);
				if (res2 > maxRes2)
				{
					maxRes2 = res2;
				}
				pEdges[iPoint].m_pos = newPoint;
				pLastMove2[iPoint] = res2;
			}
			iPrev = iPoint;
		}

		if (++iterCnt > numEdges*10)
		{
#ifdef JSINECKY
			ASSERT(false);
			if (iterCnt > numEdges*10+10)
				break;
#else
			break;
#endif
		}
	} while (maxRes2 > 0.001f);

	// Project other edge points
	U32F iPrev = 0;
	U32F iNext = 1;
	for (iNext = 1; iNext<numEdges-1; iNext++)
	{
		if (pEdges[iNext].m_activeEdges|pEdges[iNext].m_activeEdges0)
			break;
	}
	do 
	{
		if (iNext > iPrev+1)
		{
			for (U32F ii = iPrev+1; ii<iNext; ii++)
			{
				if (!AllComponentsEqual(pDir[ii], Vector(kZero)))
				{
					Point thisPnt = pEdges[ii].m_pos;
					Scalar oldT = Dot(thisPnt - pPos0[ii], pDir[ii]);
					if (oldT > 0.0f)
					{
						Point prevPnt = pEdges[iPrev].m_pos;
						Point nextPnt = pEdges[iNext].m_pos;
						Point newPoint;
						ProjectLineEdge(pPos0[ii], pDir[ii], prevPnt, nextPnt, newPoint);
						Scalar newT = Dot(newPoint - pPos0[ii], pDir[ii]);
						newT = MinMax(newT, Scalar(kZero), oldT);
						newPoint = pPos0[ii] + newT * pDir[ii];
						PHYSICS_ASSERT(IsFinite(newPoint));
						pEdges[ii].m_pos = newPoint;
					}
				}
			}
		}
		iPrev = iNext;
		for (iNext++; iNext<numEdges-1; iNext++)
		{
			if (pEdges[iNext].m_activeEdges|pEdges[iNext].m_activeEdges0)
				break;
		}
	} while (iNext<numEdges);

	// If we had to back step by more than 0.001 we can mark the edge as inactive
	// It will be removed later if the edge point is far away enough from the edge
	for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		EdgePoint& ePoint = pEdges[ii];
		if (ePoint.m_numNewEdges && DistSqr(ePoint.m_pos, pSavePos[ii]) > 0.000001f)
		{
			U32 oldEdgesMask = (1U << (ePoint.m_numEdges - ePoint.m_numNewEdges)) - 1;
			ePoint.m_activeEdges &= oldEdgesMask;
		}
	}
}

void Rope2::DetermineNewEdgePositivness(U32F numEdges, EdgePoint* pEdges, const Point* pPrevPos)
{
	for (U32F ii = 0; ii<numEdges-1; ii++)
	{
		EdgePoint& ePoint = pEdges[ii];
		for (U32F iPointEdge = ePoint.m_numEdges-ePoint.m_numNewEdges; iPointEdge<ePoint.m_numEdges; iPointEdge++)
		{
			RopeColEdge& edge = m_colCache.m_pEdges[ePoint.m_edgeIndices[iPointEdge]];
			DetermineEdgePositivness(edge, pEdges[ii-1].m_pos, pEdges[ii+1].m_pos); //, pPrevPos[ii-1], pPrevPos[ii+1]);
		}
	}
}

bool Rope2::MoveStrainedCollideWithEdges(U32F& numEdges, EdgePoint* pEdges, Point* pPos0, Point* pPos1)
{
	PROFILE(Rope, MoveStrainedCollideWithEdges);

	ScopedTempAllocator tempAllocJ(FILE_LINE_FUNC);

	/*F32* pRopeDist1 = NDI_NEW F32[m_maxNumPoints];

	pRopeDist1[0] = pEdges[0].m_ropeDist;
	pRopeDist1[numEdges-1] = pEdges[numEdges-1].m_ropeDist;

	F32 ropeLen = pRopeDist1[numEdges-1] - pRopeDist1[0];
	F32 dist = 0.0f;
	for (U32F ii = 1; ii<numEdges; ii++)
	{
		dist += Dist(pPos1[ii], pPos1[ii-1]);
	}

	ASSERT(dist > 0.0f);
	F32 distRatio = ropeLen / dist;
	F32 dist1 = 0.0f;
	for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		dist1 += Dist(pPos1[ii], pPos1[ii-1]);
		pRopeDist1[ii] = pRopeDist1[0] + dist1 * distRatio;
	}*/

#ifdef ROPE2_EDGES_PER_POINT
	I16* pColEdgeIndicesMin = NDI_NEW I16[m_maxNumPoints];
	I16* pColEdgeIndicesMax = NDI_NEW I16[m_maxNumPoints];

	// @@JS: Use also pEdges[ii].m_ropeDist to determine min/max edge indices?

	U32F iPoint = 0;
	for (U32F ii = 0; ii<numEdges; ii++)
	{
		while (m_pRopeDist[iPoint] < pEdges[ii].m_ropeDist && iPoint<m_numPoints-1)
			iPoint++;
		pColEdgeIndicesMax[ii] = m_colCache.GetPointLastEdgeIndex(iPoint);
		if (iPoint>0 && m_pRopeDist[iPoint] > pEdges[ii].m_ropeDist)
		{
			pColEdgeIndicesMin[ii] = m_colCache.GetPointFirstEdgeIndex(iPoint-1);
		}
		else
		{
			pColEdgeIndicesMin[ii] = m_colCache.GetPointFirstEdgeIndex(iPoint);
		}
	}
#endif

	for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		ValidateEdgesIndices(pEdges[ii], m_colCache.m_numEdges);
	}

	// Remove edges that just became active in this move
	// We prefer to re-collide them to get the correct position of where the rope hit the edge
	/*for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		if (!pEdges[ii].m_activeEdges0 && pEdges[ii].m_activeEdges)
		{
			U32F iEdge = 0;
			while (iEdge<pEdges[ii].m_numEdges)
			{
				if (pEdges[ii].GetEdgeActive(iEdge))
				{
					pEdges[ii].RemoveEdge(iEdge);
				}
				else
					iEdge++;
			}
		}
	}*/

	bool collided = false;
	{
		U32F ii = 0;
		while (ii < numEdges)
		{
			if (numEdges >= m_maxNumPoints)
				// Not good, just bail out
				break;
			U32F saveNumEdges = numEdges;
			if (MovePointCollideWithEdges(pEdges, ii, numEdges, pPos0, pPos1, nullptr))
			{
				ii += numEdges-saveNumEdges; // because we may have added a new edge
				collided = true;
			}
			ii++;
		}
	}

	return collided;
}

bool Rope2::RemoveObsoleteEdgePoints(U32F& numEdges, EdgePoint* pEdges)
{
	bool changed = false;

	U32F ii = 1;
	while (ii<numEdges-1)
	{
		EdgePoint& ePoint = pEdges[ii];

		// If edge point has no edges and is straight we can remove it
		if (!ePoint.m_numEdges)
		{
			Vector vec = pEdges[ii+1].m_pos - pEdges[ii-1].m_pos;
			vec = SafeNormalize(vec, kZero);
			if (DistSqr(pEdges[ii].m_pos, pEdges[ii-1].m_pos + vec * Dot(vec, pEdges[ii].m_pos - pEdges[ii-1].m_pos)) < 0.000001f)
			{
				memmove(pEdges+ii, pEdges+ii+1, (numEdges-ii-1)*sizeof(pEdges[0]));
				numEdges--;
			}
			else
			{
				ii++;
			}
		}
		else
		{
			ii++;
		}
	}

	return changed;

}

bool Rope2::CheckEdgesLimits(U32F numEdges, EdgePoint* pEdges)
{
	bool changed = false;

	for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		EdgePoint& ePoint = pEdges[ii];

		U32F iPointEdge = 0; 
		while (iPointEdge < ePoint.m_numEdges)
		{
			RopeColEdge& edge = m_colCache.m_pEdges[ePoint.m_edgeIndices[iPointEdge]];
			Scalar edgeT = Dot(ePoint.m_pos - edge.m_pnt, Vector(edge.m_vec));
			if (edgeT <= -0.002f || edgeT >= edge.m_vec.W() + 0.002f)
			{
				changed = changed || ePoint.GetEdgeActive(iPointEdge); // we may need to straighten this up
				ePoint.SetEdgeActive(iPointEdge, false);
			}
			iPointEdge++;
		}
	}

	return changed;
}

bool Rope2::CleanInactiveEdges(EdgePoint& ePoint)
{
	U32F iPointEdge = 0;
	U32 numPointEdges = 0;
	while (iPointEdge < ePoint.m_numEdges)
	{
		if (ePoint.m_activeEdges & (1U << iPointEdge))
		{
			ePoint.m_edgeIndices[numPointEdges] = ePoint.m_edgeIndices[iPointEdge];
			ePoint.SetEdgePositive(numPointEdges, ePoint.GetEdgePositive(iPointEdge));
			numPointEdges++;
		}
		iPointEdge++;
	}
	bool changed = ePoint.m_numEdges != numPointEdges;
	ePoint.m_numEdges = numPointEdges;
	ePoint.m_activeEdges = (1U << numPointEdges) - 1;
	ePoint.m_edgePositive &= (1U << numPointEdges) - 1;
	return changed;
}

bool Rope2::CleanInactiveEdges(U32F& numEdges, EdgePoint* pEdges)
{
	bool changed = false;
	for (U32F ii = 1; ii<numEdges-1; ii++)
	{
		if (CleanInactiveEdges(pEdges[ii]))
			changed = true;
	}

	return changed;
}

void Rope2::InsertEdgePoint(Rope2::EdgePoint* pEdges, U32F& numEdges, U32F iInsert, const Point& pos, F32 prevRopeDist, F32 nextRopeDist, Point* pPos0, Point* pPos1,
	const Scalar& t, const Scalar& u)
{
	ALWAYS_ASSERT(numEdges < m_maxNumPoints);
	memmove(pEdges+iInsert+1, pEdges+iInsert, (numEdges-iInsert)*sizeof(pEdges[0]));
	pEdges[iInsert].Reset();
	pEdges[iInsert].m_pos = pos;
	pEdges[iInsert].m_ropeDist = Lerp(prevRopeDist, nextRopeDist, (F32)u);
	JAROS_ALWAYS_ASSERT(iInsert < numEdges);
	pEdges[iInsert].m_flags = pEdges[iInsert+1].m_flags & (kNodeStrained|kNodeUseSavePos); // copy strained flag from the point just after the new one

	memmove(pPos0+iInsert+1, pPos0+iInsert, (numEdges-iInsert)*sizeof(pPos0[0]));
	pPos0[iInsert] = Lerp(pPos0[iInsert-1], pPos0[iInsert+1], u);
	memmove(pPos1+iInsert+1, pPos1+iInsert, (numEdges-iInsert)*sizeof(pPos1[0]));
	pPos1[iInsert] = t == Scalar(kZero) || AllComponentsEqual(pPos0[iInsert], pos) 
		? Lerp(pPos1[iInsert-1], pPos1[iInsert+1], u) // does not really matter as we didn't move at all
		: Lerp(pPos0[iInsert], pos, Recip(t)); // make sure pos1 is on the line from pos0 thru hitPnt

	numEdges++;
}

void Rope2::AddEdgeToEdgePoint(Rope2::EdgePoint* pEdges, U32F& numEdges, U32F iEdge, U16 edgeIndex, bool active, bool checkTrims)
{
	if (!active)
	{
		if (iEdge > 0)
		{
			if (CheckForEdge(pEdges[iEdge-1], edgeIndex))
				return;
		}
		if (iEdge < numEdges-1)
		{
			if (CheckForEdge(pEdges[iEdge+1], edgeIndex))
				return;
		}
	}
	else
	{
		if (iEdge > 0)
		{
			if (CheckForEdgeRemoveInactive(pEdges[iEdge-1], edgeIndex))
			{
				// We should prevent this during edge testing
				JAROS_ASSERT(false);
				return;
			}
		}
		if (iEdge < numEdges-1)
		{
			if (CheckForEdgeRemoveInactive(pEdges[iEdge+1], edgeIndex))
			{
				// We should prevent this during edge testing
				JAROS_ASSERT(false);
				return;
			}
		}
	}

	if (checkTrims)
	{
		if (m_colCache.CheckEdgeTrims(edgeIndex, this))
		{
			RopeColTriId triIdInvalid;
			Point adjustedPos;
			edgeIndex = m_colCache.FitPointToTrimmedEdge(edgeIndex, pEdges[iEdge].m_pos, triIdInvalid, triIdInvalid, adjustedPos);
		}
	}

	pEdges[iEdge].AddEdge(edgeIndex, active);
}

void Rope2::CheckTrimNewEdge(U16 edgeIndex, EdgePoint* pEdges, U32F numEdges, Point* pPos)
{
	if (!m_colCache.CheckEdgeTrims(edgeIndex, this))
		return;

	RopeColTriId triIdInvalid;

	for (U32F ii = 0; ii<numEdges; ii++)
	{
		EdgePoint& ePoint = pEdges[ii];
		for (U32F jj = 0; jj<ePoint.m_numEdges; jj++)
		{
			if (ePoint.m_edgeIndices[jj] == edgeIndex)
			{
				Point adjustedPos;
				ePoint.m_edgeIndices[jj] = m_colCache.FitPointToTrimmedEdge(edgeIndex, pPos ? pPos[ii] : ePoint.m_pos, triIdInvalid, triIdInvalid, adjustedPos);
				// We only adjust if the position is a separate array. We never adjust edgePoint pos itself as we should never need to do it
				// (we should never hit a new edge behind it's trimming)
				if (pPos)
					pPos[ii] = adjustedPos;
			}
		}
	}
}

const U32F kMaxEdgeHits = 256;

struct EdgeHit
{
	Scalar m_t;				// <0..1> param along the motion of the line
	Scalar m_u;				// <0..1> param along the line that was swept/rotated
	U32F m_edgeIndex;
	Vec4 m_edgeClip0;
	Vec4 m_edgeClip1;
};

I32 CompareEdgeHitsU(EdgeHit& a, EdgeHit& b)
{
	return a.m_u < b.m_u ? -1 : 1;
}

void SetEdgeIndicesBits(const Rope2::EdgePoint& ePoint, ExternalBitArray& edgeBitArray)
{
	for (U32F ii = 0; ii<ePoint.m_numEdges; ii++)
	{
		edgeBitArray.SetBit(ePoint.m_edgeIndices[ii]);
	}
}

void ClearEdgeIndicesBits(const Rope2::EdgePoint& ePoint, ExternalBitArray& edgeBitArray)
{
	for (U32F ii = 0; ii<ePoint.m_numEdges; ii++)
	{
		edgeBitArray.ClearBit(ePoint.m_edgeIndices[ii]);
	}
}

bool Rope2::MovePointCollideWithEdges(EdgePoint* pEdges, U32F iEdge, U32F& numEdges, Point* pPos0, Point* pPos1, ExternalBitArray* pEdgesPrevStep)
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);

	ASSERT(iEdge >= 0 && iEdge < numEdges); 
	EdgePoint& ePoint = pEdges[iEdge];
	Point pos1 = pPos1[iEdge];

	EdgePoint* pPrevEPoint = iEdge > 0 ? &pEdges[iEdge-1] : nullptr;
	EdgePoint* pNextEPoint = iEdge < numEdges-1 ? &pEdges[iEdge+1] : nullptr;

	if (m_colCache.m_numEdges == 0)
	{
		pEdges[iEdge].m_pos = pos1;
		return false;
	}

	Point pos0 = ePoint.m_pos;
	Vector dir = pos1-pos0;
	Scalar dist;
	dir = Normalize(dir, dist);
	if (dist == Scalar(kZero))
		return false;

	U64* pTestedEdgesData = NDI_NEW U64[ExternalBitArray::DetermineNumBlocks(m_colCache.m_numEdges)];
	ExternalBitArray edgesToTest;
	edgesToTest.Init(m_colCache.m_numEdges, pTestedEdgesData, true);

	ValidateEdgesIndices(ePoint, m_colCache.m_numEdges);

	EdgeHit* pHits0 = NDI_NEW EdgeHit[kMaxEdgeHits];
	U32F numHits0 = 0;
	I32F firstHit0 = -1;
	Scalar hitDist0 = dist;
	Scalar hitDist = dist;
	if (iEdge > 0)
	{
		// Clear bits for edges that are already on either end of this segment
		ValidateEdgesIndices(*pPrevEPoint, m_colCache.m_numEdges);
		ClearEdgeIndicesBits(*pPrevEPoint, edgesToTest);
		for (I32F ii = (I32F)iEdge-2; ii>0; ii--)
		{
			// Go back through points and disable edges if the edge points are a split off + also the one right before split
			if (DistSqr(pEdges[ii].m_pos, pPrevEPoint->m_pos) > kScEdgeTol2 && (pEdges[ii].m_internFlags & kNodeSplit) == 0 && (pEdges[ii+1].m_internFlags & kNodeSplit) == 0)
			{
				break;
			}
			ValidateEdgesIndices(pEdges[ii], m_colCache.m_numEdges);
			ClearEdgeIndicesBits(pEdges[ii], edgesToTest);
		}

		ClearEdgeIndicesBits(ePoint, edgesToTest);

		for (I32F ii = iEdge+1; ii<numEdges-1; ii++)
		{
			// Go forward through points and disable edges if the edge points are a split off + also the one right before split
			if (DistSqr(pEdges[ii].m_pos, pos0) > kScEdgeTol2 && (pEdges[ii-1].m_internFlags & kNodeSplit) == 0 && (pEdges[ii-2].m_internFlags & kNodeSplit) == 0)
			{
				break;
			}
			ValidateEdgesIndices(pEdges[ii], m_colCache.m_numEdges);
			ClearEdgeIndicesBits(pEdges[ii], edgesToTest);
		}

		// @@JS: Or moving to where the next point already is (merge)? Skip all its edges?

		const EdgePoint* pPrevPrevEPoint = (iEdge >= 2) ? &pEdges[iEdge-2] : nullptr;
		if (MoveSegmentCollideWithEdges(*pPrevEPoint, ePoint, dir, dist, edgesToTest, pEdgesPrevStep, pPrevPrevEPoint, pNextEPoint, pHits0, numHits0, firstHit0))
		{
			if (pHits0[firstHit0].m_t < 1.0f)
			{
				hitDist0 *= pHits0[firstHit0].m_t;
				hitDist = hitDist0;
			}
		}
	}

	EdgeHit* pHits1 = NDI_NEW EdgeHit[kMaxEdgeHits];
	U32F numHits1 = 0;
	I32F firstHit1 = -1;
	if (iEdge < numEdges-1 && hitDist > Scalar(kZero))
	{
		// Clear bits for edges that are already on either end of this segment
		edgesToTest.SetAllBits();

		ValidateEdgesIndices(*pNextEPoint, m_colCache.m_numEdges);
		ClearEdgeIndicesBits(*pNextEPoint, edgesToTest);
		for (I32F ii = iEdge+2; ii<numEdges-1; ii++)
		{
			// Go forward through points and disable edges if the edge points are a split off + also the one right before split
			if (DistSqr(pEdges[ii].m_pos, pNextEPoint->m_pos) > kScEdgeTol2 && (pEdges[ii-1].m_internFlags & kNodeSplit) == 0 && (pEdges[ii-2].m_internFlags & kNodeSplit) == 0)
			{
				break;
			}
			ValidateEdgesIndices(pEdges[ii], m_colCache.m_numEdges);
			ClearEdgeIndicesBits(pEdges[ii], edgesToTest);
		}

		ClearEdgeIndicesBits(ePoint, edgesToTest);

		for (I32F ii = (I32F)iEdge-1; ii>0; ii--)
		{
			// Go back through points and disable edges if the edge points are a split off + also the one right before split
			if (DistSqr(pPos0[ii], pos0) > kScEdgeTol2 && (pEdges[ii].m_internFlags & kNodeSplit) == 0 && (pEdges[ii+1].m_internFlags & kNodeSplit) == 0)
			{
				break;
			}
			ValidateEdgesIndices(pEdges[ii], m_colCache.m_numEdges);
			ClearEdgeIndicesBits(pEdges[ii], edgesToTest);
		}

		// Moving to where the previous point already is (merge)? Skip all its edges
		for (I32F ii = (I32F)iEdge-1; ii>0; ii--)
		{
			if (DistSqr(pEdges[ii].m_pos, pos1) > kScEdgeTol2)
			{
				break;
			}
			ValidateEdgesIndices(pEdges[ii], m_colCache.m_numEdges);
			ClearEdgeIndicesBits(pEdges[ii], edgesToTest);
		}

		const EdgePoint* pNextNextEPoint = (iEdge+2 <= numEdges-1) ? &pEdges[iEdge+2] : nullptr;
		if (MoveSegmentCollideWithEdges(*pNextEPoint, ePoint, dir, hitDist, edgesToTest, pEdgesPrevStep, pNextNextEPoint, pPrevEPoint, pHits1, numHits1, firstHit1))
		{
			if (pHits1[firstHit1].m_t < 1.0f)
			{
				hitDist *= pHits1[firstHit1].m_t;
				firstHit0 = -1; // we are firster
			}
		}
	}

	pos1 = pos0 + dir * hitDist;
	if (numHits0+numHits1 == 0)
	{
		pEdges[iEdge].m_pos = pos1;
		return false;
	}

	F32 ropeDist = ePoint.m_ropeDist;
	Point prevPnt = pPrevEPoint ? pPrevEPoint->m_pos : Point(kZero);
	F32 prevRopeDist = pPrevEPoint ? pPrevEPoint->m_ropeDist : 0.0f;
	Point nextPnt = pNextEPoint ? pNextEPoint->m_pos : Point(kZero);
	F32 nextRopeDist = pNextEPoint ? pNextEPoint->m_ropeDist : 0.0f;

	bool newActiveHit = false;

	if (numHits0)
	{
		// Exclude hits that are too far behind the first hit
		Vector hitVec = pos1 - prevPnt;
		Scalar hitVecLen;
		Vector hitVecDir = SafeNormalize(hitVec, kZero, hitVecLen);
		Scalar normLen;
		Vector norm = Normalize(Cross(hitVecDir, dir), normLen);
		Vector hitPlaneNorm = normLen < 1e-4f ? dir : Cross(norm, hitVecDir);
		Vec4 hitPlane = hitPlaneNorm.GetVec4();
		hitPlane.SetW(-Dot(hitPlaneNorm, pos1-Point(kZero)) - kScEdgeTol);
		U32F numFilteredHits = 0;
		for (U32F ii = 0; ii<numHits0; ii++)
		{
			Scalar dt0 = Dot4(pHits0[ii].m_edgeClip0, hitPlane);
			Scalar dt1 = Dot4(pHits0[ii].m_edgeClip1, hitPlane);
			if (dt0 < 0.0f || dt1 < 0.0f)
			{
				pHits0[numFilteredHits] = pHits0[ii];
				if (ii == firstHit0)
					firstHit0 = numFilteredHits;
				numFilteredHits++;
			}
			else
			{
				ASSERT(ii != firstHit0);
			}
		}

		// Now insert the new hits
		numHits0 = numFilteredHits;
		if (numHits0)
		{
			U32F iPrev = iEdge-1;
			U32F iInsert = iEdge;
			U32F iFirst = iInsert;
			Scalar insertU = 1.0f;
			Scalar firstU = 1.0f;
			if (firstHit0 >= 0)
			{
				// Insert the first hit
				bool firstActive = pHits0[firstHit0].m_t <= 1.0f;
				if ((insertU - pHits0[firstHit0].m_u) * hitVecLen < kScEdgeTol)
				{
					AddEdgeToEdgePoint(pEdges, numEdges, iInsert, pHits0[firstHit0].m_edgeIndex, firstActive, true);
					newActiveHit = firstActive;
				}
				else if (iPrev > 0 && pHits0[firstHit0].m_u * hitVecLen < kScEdgeTol)
				{
					AddEdgeToEdgePoint(pEdges, numEdges, iPrev, pHits0[firstHit0].m_edgeIndex, firstActive, true);
					newActiveHit = firstActive;
				}
				else
				{
					if (numEdges >= m_maxNumPoints)
					{
						JAROS_ALWAYS_ASSERT(false);
					}
					else
					{
						Point hitPnt = prevPnt + pHits0[firstHit0].m_u * hitVec;
						InsertEdgePoint(pEdges, numEdges, iInsert, hitPnt, prevRopeDist, ropeDist, pPos0, pPos1, pHits0[firstHit0].m_t, pHits0[firstHit0].m_u);
						AddEdgeToEdgePoint(pEdges, numEdges, iInsert, pHits0[firstHit0].m_edgeIndex, firstActive, true);
						firstU = pHits0[firstHit0].m_u;
						iFirst = iInsert;
						iEdge++;
						iInsert++;
						newActiveHit = firstActive;
					}
				}
				pHits0[firstHit0] = pHits0[numHits0-1];
				numHits0--;
			}

			// Sort remaining hits by u
			QuickSortStack(pHits0, numHits0, CompareEdgeHitsU);

			// Insert all other hits
			// We will add them to any of the existing EdgePoints if its within kEdgeTol
			for (I32F ii = numHits0-1; ii>=0; ii--)
			{
				if (Abs(firstU - pHits0[ii].m_u) * hitVecLen < kScEdgeTol)
				{
					AddEdgeToEdgePoint(pEdges, numEdges, iFirst, pHits0[ii].m_edgeIndex, false, true);
				}
				else if ((insertU - pHits0[ii].m_u) * hitVecLen < kScEdgeTol)
				{
					AddEdgeToEdgePoint(pEdges, numEdges, iInsert, pHits0[ii].m_edgeIndex, false, true);
				}
				else if (iPrev > 0 && pHits0[ii].m_u * hitVecLen < kScEdgeTol)
				{
					AddEdgeToEdgePoint(pEdges, numEdges, iPrev, pHits0[ii].m_edgeIndex, false, true);
				}
				else
				{
					if (numEdges >= m_maxNumPoints)
					{
						JAROS_ALWAYS_ASSERT(false);
					}
					else
					{
						if (iInsert > iFirst && pHits0[ii].m_u < firstU)
							iInsert = iFirst;
						Point hitPnt = prevPnt + pHits0[ii].m_u * hitVec;
						InsertEdgePoint(pEdges, numEdges, iInsert, hitPnt, prevRopeDist, ropeDist, pPos0, pPos1, pHits0[ii].m_t, pHits0[ii].m_u);
						AddEdgeToEdgePoint(pEdges, numEdges, iInsert, pHits0[ii].m_edgeIndex, false, true);
						insertU = pHits0[ii].m_u;
						iFirst += iFirst >= iInsert ? 1 : 0;
						iEdge++;
					}
				}
			}
		}
	}

	if (numHits1)
	{
		// Exclude hits that are too far behind the first hit
		nextPnt = pNextEPoint->m_pos;
		Vector hitVec = pos1 - nextPnt;
		Scalar hitVecLen;
		Vector hitVecDir = SafeNormalize(hitVec, kZero, hitVecLen);
		Scalar normLen;
		Vector norm = Normalize(Cross(hitVecDir, dir), normLen);
		Vector hitPlaneNorm = normLen < 1e-4f ? dir : Cross(norm, hitVecDir);
		Vec4 hitPlane = hitPlaneNorm.GetVec4();
		hitPlane.SetW(-Dot(hitPlaneNorm, pos1-Point(kZero)) - kScEdgeTol);
		U32F numFilteredHits = 0;
		for (U32F ii = 0; ii<numHits1; ii++)
		{
			Scalar dt0 = Dot4(pHits1[ii].m_edgeClip0, hitPlane);
			Scalar dt1 = Dot4(pHits1[ii].m_edgeClip1, hitPlane);
			if (dt0 < 0.0f || dt1 < 0.0f)
			{
				pHits1[numFilteredHits] = pHits1[ii];
				if (ii == firstHit1)
					firstHit1 = numFilteredHits;
				numFilteredHits++;
			}
			else
			{
				ASSERT(ii != firstHit1 || pHits1[firstHit1].m_t > 1.0f);
			}
		}

		// Now insert the new hits
		// We will not create a new edge points for inactive hits because this segment of the rope will most likely move again when we move the next point
		numHits1 = numFilteredHits;
		if (numHits1)
		{
			U32F iNext = iEdge+1;
			U32F iInsert = iEdge;
			U32F iFirst = iInsert;
			Scalar insertU = 1.0f;
			Scalar firstU = 1.0f;
			if (firstHit1 >= 0)
			{
				// Insert the first hit
				bool firstActive = pHits1[firstHit1].m_t <= 1.0f;
				if ((insertU - pHits1[firstHit1].m_u) * hitVecLen < kScEdgeTol)
				{
					if (!CheckForActiveEdge(pEdges[iInsert], pHits1[firstHit1].m_edgeIndex))
					{
						AddEdgeToEdgePoint(pEdges, numEdges, iInsert, pHits1[firstHit1].m_edgeIndex, firstActive, true);
						newActiveHit = newActiveHit || firstActive;
					}
				}
				else if (iNext < numEdges-1 && pHits1[firstHit1].m_u * hitVecLen < kScEdgeTol)
				{
					AddEdgeToEdgePoint(pEdges, numEdges, iNext, pHits1[firstHit1].m_edgeIndex, firstActive, true);
					newActiveHit = newActiveHit || firstActive;
				}
				else if (firstActive)
				{
					if (numEdges >= m_maxNumPoints)
					{
						JAROS_ALWAYS_ASSERT(false);
					}
					else
					{
						Point hitPnt = nextPnt + pHits1[firstHit1].m_u * hitVec;
						InsertEdgePoint(pEdges, numEdges, iInsert+1, hitPnt, ropeDist, nextRopeDist, pPos0, pPos1, pHits1[firstHit1].m_t, 1.0f - pHits1[firstHit1].m_u);
						AddEdgeToEdgePoint(pEdges, numEdges, iInsert+1, pHits1[firstHit1].m_edgeIndex, firstActive, true);
						firstU = pHits1[firstHit1].m_u;
						iFirst = iInsert+1;
						iNext++;
						newActiveHit = newActiveHit || firstActive;
					}
				}
				pHits1[firstHit1] = pHits1[numHits1-1];
				numHits1--;
			}

			// Sort remaining hits by u
			QuickSortStack(pHits1, numHits1, CompareEdgeHitsU);

			// Insert all other hits
			// We will add them to any of the existing EdgePoints if its within kScEdgeTol
			for (I32F ii = numHits1-1; ii>=0; ii--)
			{
				if (Abs(firstU - pHits1[ii].m_u) * hitVecLen < kScEdgeTol)
				{
					if (!CheckForEdge(pEdges[iFirst], pHits1[ii].m_edgeIndex))
						AddEdgeToEdgePoint(pEdges, numEdges, iFirst, pHits1[ii].m_edgeIndex, false, true);
				}
				else if ((insertU - pHits1[ii].m_u) * hitVecLen < kScEdgeTol)
				{
					AddEdgeToEdgePoint(pEdges, numEdges, iInsert, pHits1[ii].m_edgeIndex, false, true);
				}
				else if (iNext < numEdges-1 && pHits1[ii].m_u * hitVecLen < kScEdgeTol)
				{
					AddEdgeToEdgePoint(pEdges, numEdges, iNext, pHits1[ii].m_edgeIndex, false, true);
				}
				// We will not create a new edge points for inactive hits because this segment of the rope will most likely move again when we move the next point
				//else
				//{
				//	if (iInsert > iFirst && pHits0[ii].m_u < firstU)
				//		iInsert = iFirst;
				//	Point hitPnt = nextPnt + pHits1[ii].m_u * hitVec;
				//	InsertEdgePoint(pEdges, numEdges, iInsert+1, hitPnt, ropeDist, nextRopeDist, pPos0, pPos1, pHits1[ii].m_t, 1.0f - pHits1[ii].m_u);
				//	AddEdgeToEdgePoint(pEdges, numEdges, iInsert+1, pHits1[ii].m_edgeIndex, false);
				//	insertU = pHits1[ii].m_u;
				//	iFirst += iFirst>=iInsert+1 ? 1 : 0;
				//	iInsert++;
				//	iNext++;
				//}
			}
		}
	}

	PHYSICS_ASSERT(IsFinite(pos1));
	pEdges[iEdge].m_pos = pos1;
	return newActiveHit; // return true if we hit something (don't count edges that we just approach and have been inserted as inactive)
}

bool Rope2::MoveSegmentCollideWithEdges(const EdgePoint& fixedEPoint, const EdgePoint& ePoint, Vector_arg dir, Scalar_arg dist, 
	const ExternalBitArray& edgesToTest, ExternalBitArray* pEdgesPrevStep, const EdgePoint* pPrevEPoint, const EdgePoint* pNextEPoint, EdgeHit* pHits, U32F& numHits, I32F& firstHit)
{
	Point fixedPnt = fixedEPoint.m_pos;
	Point pnt0 = ePoint.m_pos;
	Point pnt1 = ePoint.m_pos + dir*dist;

	U64* pCandidateEdgesData = NDI_NEW U64[ExternalBitArray::DetermineNumBlocks(m_colCache.m_numEdges)];
	ExternalBitArray candidateEdges(m_colCache.m_numEdges, pCandidateEdgesData);

	{
		Aabb aabb((NoOpConstructor()));
		aabb.m_min = Min(Min(fixedPnt, pnt0), pnt1) - Vector(kScEdgeTol);
		aabb.m_max = Max(Max(fixedPnt, pnt0), pnt1) + Vector(kScEdgeTol);

		m_colCache.FindCandidateEdges(aabb, &candidateEdges);
	}

	if (candidateEdges.AreAllBitsClear())
		return false;

	const RopeColEdge* pEdges = m_colCache.m_pEdges;

	I32F lastShapeIndex = -1;
	Locator shapeLoc;
	Locator shapeLocPrev;

	// If we're going from free to strained we need much bigger backward tolerance because in free mode the rope can clip through
	// edges quite easily
	// This is not the most robust solution but oh well ...
	// @@JS: Actually this really fails. We need something better to deal with FreeToStrained transition if free nodes intersect an edge
	const Scalar g_backwardTol = (ePoint.m_internFlags & kNodeFreeToStrained) ? Scalar(0.04f) : kScEdgeTol;

	Scalar lineStartLen;
	Vector lineStart = Normalize(pnt0 - fixedPnt, lineStartLen);
	Vector norm = Cross(lineStart, dir);
	Scalar normLen;
	norm = Normalize(norm, normLen);
	if (normLen < 1e-6f)
	{
		// This is just a moving point
		Scalar firstT = FLT_MAX;
		for (U64 edgeIndex = candidateEdges.FindFirstSetBit(); edgeIndex < m_colCache.m_numEdges; edgeIndex = candidateEdges.FindNextSetBit(edgeIndex))
		{
			if (!edgesToTest.IsBitSet(edgeIndex))
				continue;

			const RopeColEdge& edge = pEdges[edgeIndex];

			// Trims are not in the tree so we just mark all trims of this edges as candidates
			if (edge.m_numExtraTrims)
			{
				candidateEdges.SetBitRange(edge.m_secondTrimIndex, edge.m_secondTrimIndex+edge.m_numExtraTrims-1);
			}

			Point edgePnt = edge.m_pnt;
			Vector edgeVec = Vector(edge.m_vec);

			if (pEdgesPrevStep && pEdgesPrevStep->IsBitSet(edgeIndex))
			{
				I32F triIndex = edge.m_triIndex;
				I32F shapeIndex = m_colCache.m_pTris[triIndex].m_shapeIndex;
				if (shapeIndex != lastShapeIndex)
				{
					const RopeColliderHandle& hCollider = m_colCache.m_pShapes[shapeIndex];
					shapeLoc = hCollider.GetLocator(this);
					shapeLocPrev = hCollider.GetPrevLocator(this);
					lastShapeIndex = shapeIndex;
				}

				edgePnt = shapeLocPrev.TransformPoint(shapeLoc.UntransformPoint(edgePnt));
				edgeVec = shapeLocPrev.TransformVector(shapeLoc.UntransformVector(edgeVec));
			}

			Scalar edgeLen = edge.m_vec.W();
			Point edgeEndPoint = edgePnt + edgeVec * edgeLen;

			Scalar segDist, s, t;
			if (SegSegDist(edgePnt, edgeVec, edgeLen, pnt0, dir, dist, kScEdgeTol, segDist, s, t))
			{
				// We need to make sure this edge actually will be successfully added, otherwise we can get into infinite loops
				// So in case this point will be added to one of the end point we need to make sure there is space for a new edge and
				// see if the same edge is not already active in the neighbor of that point
				bool bInsert = true;
				Point hitPnt = pnt0 + t*dir;
				Vector hitVec = hitPnt - fixedPnt;
				Scalar hitVecLen;
				Vector hitVecDir = SafeNormalize(hitVec, kZero, hitVecLen);
				if (ePoint.m_numEdges >= EdgePoint::kMaxNumEdges || (pNextEPoint && CheckForActiveEdge(*pNextEPoint, edgeIndex)))
					bInsert = false;

				if (bInsert)
				{
					ASSERT(numHits < kMaxEdgeHits);
					if (t < firstT && numHits == kMaxEdgeHits)
						numHits = kMaxEdgeHits-1;
					if (numHits < kMaxEdgeHits)
					{
						pHits[numHits].m_edgeIndex = edgeIndex;
						pHits[numHits].m_u = 1.0f;
						pHits[numHits].m_t = t/dist;
						pHits[numHits].m_edgeClip0 = pHits[numHits].m_edgeClip1 = (pnt0 + t*dir).GetVec4();
						if (t < firstT)
						{
							firstT = t;
							firstHit = numHits;
						}
						numHits++;
					}
				}
			}
		}
	}
	else
	{
		// Rotating line 
		Vec4 plane = norm.GetVec4();
		Vector fixedPntVec = fixedPnt-Point(kZero);
		plane.SetW(-Dot(norm, fixedPntVec));

		Vec4 planes[8];
		planes[0] = plane;
		planes[0].SetW(planes[0].W() - kScEdgeTol);
		planes[1] = -plane;
		planes[1].SetW(planes[1].W() - kScEdgeTol);
		Vector norm2 = Cross(lineStart, norm);
		planes[2] = norm2.GetVec4();
		planes[2].SetW(-Dot(norm2, fixedPntVec) - g_backwardTol);
		Vector norm3 = Cross(dir, norm);
		planes[3] = norm3.GetVec4();
		planes[3].SetW(-Dot(norm3, pnt0-Point(kZero)) - kScEdgeTol);
		Vector norm4 = Cross(Normalize(fixedPnt-pnt1), norm);
		planes[4] = norm4.GetVec4();
		planes[4].SetW(-Dot(norm4, fixedPntVec) - kScEdgeTol);
		Scalar norm5Len;
		Vector norm5 = Normalize(norm2 + norm3, norm5Len);
		if (norm5Len < 1e-3f)
			norm5 = Normalize(pnt0 - fixedPnt);
		planes[5] = norm5.GetVec4();
		planes[5].SetW(-Dot(norm5, pnt0-Point(kZero)) - kScEdgeTol);
		Scalar norm6Len;
		Vector norm6 = Normalize(norm3 + norm4, norm6Len);
		if (norm6Len < 1e-3f)
			norm6 = Normalize(pnt1 - fixedPnt);
		planes[6] = norm6.GetVec4();
		planes[6].SetW(-Dot(norm6, pnt1-Point(kZero)) - kScEdgeTol);
		Scalar norm7Len;
		Vector norm7 = Normalize(norm4 + norm2, norm7Len);
		if (norm7Len < 1e-3f)
			norm7 = Normalize(fixedPnt - pnt0);
		planes[7] = norm7.GetVec4();
		planes[7].SetW(-Dot(norm7, fixedPntVec) - kScEdgeTol);

		Scalar uDistInv = Recip(-Dot4(fixedPnt.GetVec4(), planes[3]) - kScEdgeTol);

		Scalar firstT = FLT_MAX;
		firstHit = -1;
		for (U64 edgeIndex = candidateEdges.FindFirstSetBit(); edgeIndex < m_colCache.m_numEdges; edgeIndex = candidateEdges.FindNextSetBit(edgeIndex))
		{
			if (!edgesToTest.IsBitSet(edgeIndex))
				continue;

			const RopeColEdge& edge = pEdges[edgeIndex];

			// Trims are not in the tree so we just mark all trims of this edges as candidates
			if (edge.m_numExtraTrims)
			{
				candidateEdges.SetBitRange(edge.m_secondTrimIndex, edge.m_secondTrimIndex+edge.m_numExtraTrims-1);
			}

			Point edgePnt = edge.m_pnt;
			Vector edgeVec = Vector(edge.m_vec);

			if (pEdgesPrevStep && pEdgesPrevStep->IsBitSet(edgeIndex))
			{
				I32F triIndex = edge.m_triIndex;
				I32F shapeIndex = m_colCache.m_pTris[triIndex].m_shapeIndex;
				if (shapeIndex != lastShapeIndex)
				{
					const RopeColliderHandle& hCollider = m_colCache.m_pShapes[shapeIndex];
					shapeLoc = hCollider.GetLocator(this);
					shapeLocPrev = hCollider.GetPrevLocator(this);
					lastShapeIndex = shapeIndex;
				}

				edgePnt = shapeLocPrev.TransformPoint(shapeLoc.UntransformPoint(edgePnt));
				edgeVec = shapeLocPrev.TransformVector(shapeLoc.UntransformVector(edgeVec));
			}

			Scalar edgeLen = edge.m_vec.W();
			Point edgeEndPoint = edgePnt + edgeVec * edgeLen;

			Vec4 start4 = edgePnt.GetVec4();
			Vec4 end4 = edgeEndPoint.GetVec4();

			Scalar startPenet(kScEdgeTol);
			Scalar endPenet(kScEdgeTol);

			bool hit = true;
			for (U32F iPlane = 0; iPlane < 8; iPlane++)
			{
				Scalar dtStart = Dot4(start4, planes[iPlane]);
				Scalar dtEnd = Dot4(end4, planes[iPlane]);

				if (dtStart >= 0.0f && dtEnd >= 0.0f)
				{
					// Edge is outside the plane
					hit = false;
					break;
				}

				startPenet = Min(startPenet, -dtStart);
				endPenet = Min(endPenet, -dtEnd);

				if (dtStart * dtEnd < 0.0f)
				{
					// Edge is crossing the plane, clip it
					Scalar clipT = dtStart/(dtStart-dtEnd);
					Vec4 clipPt = Lerp(start4, end4, clipT);
					if (dtStart > 0.0f)
					{
						start4 = clipPt;
						startPenet = kZero;
					}
					else
					{
						end4 = clipPt;
						endPenet = kZero;
					}
				}
			}

			if (hit)
			{
				Point start(start4);
				Point end(end4);
				Scalar startDist = Dot4(start4, plane);
				Scalar endDist = Dot4(end4, plane);
				Scalar distDiff = startDist - endDist;

				Point hitPnt;
				if (startDist*endDist < 0.0f)
				{
					// Hit point for this edge will be intersection with our plane of movement
					hitPnt = Lerp(start, end, startDist/(startDist-endDist));
				}
				else
				{
					// ... or this totally heuristic thing
					// To be correct we should calculate the point on the edge that is the closest to the triangle of movement
					Scalar startP = startPenet / kScEdgeTol;
					Scalar endP = endPenet / kScEdgeTol;
					Scalar lerpP;
					if (startP + endP == Scalar(kZero))
						lerpP = 0.5f;
					else
						lerpP = endP / (startP + endP);
					hitPnt = Lerp(start, end, lerpP);
				}

				if (DistSqr(hitPnt, fixedPnt) > kScEdgeTol2)
				{
					Scalar hitT;
					Scalar hitU = 1.0f - (-Dot4(hitPnt.GetVec4(), planes[3]) - kScEdgeTol) * uDistInv;
					if (hitU > 0.0f)
					{
						Point hitPnt0 = Lerp(fixedPnt, pnt0, hitU);
						Point hitPnt1 = Lerp(fixedPnt, pnt1, hitU);
						Scalar hitPnt01Dist = Dist(hitPnt1, hitPnt0);
						hitT = hitPnt01Dist == Scalar(kZero) ? Scalar(kZero) : Dist(hitPnt, hitPnt0) / hitPnt01Dist;
						PHYSICS_ASSERT(IsFinite(hitT));

						// We need to make sure this edge actually will be successfully added, otherwise we can get into infinite loops
						// So in case this point will be added to one of the end point we need to make sure there is space for a new edge and
						// see if the same edge is not already active in the neighbor of that point
						bool bInsert = true;
						Point hitPntFromT = pnt0 + hitT*dist*dir; // calc this from T to stay consistent to what we will do before adding the edge
						Vector hitVec = hitPntFromT - fixedPnt;
						Scalar hitVecLen;
						Vector hitVecDir = SafeNormalize(hitVec, kZero, hitVecLen);
						if ((Scalar(1.0f) - hitU) * hitVecLen < kScEdgeTol)
						{
							if (ePoint.m_numEdges >= EdgePoint::kMaxNumEdges || (pNextEPoint && CheckForActiveEdge(*pNextEPoint, edgeIndex)))
								bInsert = false;
						}
						else if (hitU * hitVecLen < kScEdgeTol)
						{
							if (fixedEPoint.m_numEdges >= EdgePoint::kMaxNumEdges || (pPrevEPoint && CheckForActiveEdge(*pPrevEPoint, edgeIndex)))
								bInsert = false;
						}

						if (bInsert)
						{
							ASSERT(numHits < kMaxEdgeHits);
							if (hitT < firstT && numHits == kMaxEdgeHits)
								numHits = kMaxEdgeHits-1;
							if (numHits < kMaxEdgeHits)
							{
								pHits[numHits].m_edgeIndex = edgeIndex;
								pHits[numHits].m_u = hitU;
								pHits[numHits].m_t = hitT;
								pHits[numHits].m_edgeClip0 = start4;
								pHits[numHits].m_edgeClip1 = end4;
								if (hitT < firstT)
								{
									firstT = hitT;
									firstHit = numHits;
								}
								numHits++;
							}
						}
					}
				}
			}
		}
	}

	return firstHit >= 0;
}

bool Rope2::MoveEdgeCollideWithSegment(const RopeColEdge& edge, const Locator& locPrev, const Locator& loc, Point_arg pnt0, Point_arg pnt1, F32& t, F32& segT, F32& edgeT)
{
	// For this test we don't consider a "thick" geometry with tolerance radius. It should be enough if we do that for all the moving rope vs. static edges tests
	// We will first move all edge points into rotate position and then move all edges with the linear movement to keep the collision sealed

	Point edgePnt1 = edge.m_pnt;
	Vector edgeVec1(edge.m_vec);
	Scalar edgeLen = edge.m_vec.W();
	Point edgeEndPnt1 = edgePnt1 + edgeVec1 * edgeLen;
	Point edgePnt0 = locPrev.TransformPoint(loc.UntransformPoint(edgePnt1));
	Point edgeEndPnt0 = locPrev.TransformPoint(loc.UntransformPoint(edgeEndPnt1));
	Vector linMove = loc.GetTranslation() - locPrev.GetTranslation();
	Point edgePntRot = edgePnt1 - linMove;
	Point edgeEndPntRot = edgeEndPnt1 - linMove;

	// Rotate edgePnt0 into edgePntRot
	bool hitRot = false;
	{
		Vector vec = edgePntRot - edgePnt0;
		Scalar dist;
		Vector dir = Normalize(vec, dist);
		if (dist > 1e-4f)
		{
			Vector norm = Cross(Normalize(edgeEndPnt0-edgePnt0), vec);
			Scalar normLen;
			norm = Normalize(norm, normLen);
			if (normLen > 1e-4f)
			{
				Scalar startDist = Dot(pnt0-edgeEndPnt0, norm);
				Scalar endDist = Dot(pnt1-edgeEndPnt0, norm);

				if (startDist * endDist <= 0.0f && Abs(startDist-endDist) > Scalar(1e-6f))
				{
					Scalar scSegT = startDist/(startDist-endDist);
					Point hitPnt = Lerp(pnt0, pnt1, scSegT);
					Vector hitDir = hitPnt - edgeEndPnt0;
					Scalar hitDist;
					hitDir = Normalize(hitDir, hitDist);
					bool hit = false;
					if (hitDist > Scalar(1e-4f))
					{
						Scalar ss, tt;
						if (LineLineIntersection(edgePnt0, dir, edgeEndPnt0, hitDir, ss, tt))
						{
							if (ss >= 0.0f && ss <= dist && tt >= hitDist)
							{
								// Collision during the rotation phase is considered t = 0.0
								t = 0.0f;
								segT = scSegT;
								edgeT = hitDist/tt;
								hitRot = true;
							}
						}
					}
				}
			}
		}
	}

	// Rotate edgeEndPnt0 into edgeEndPntRot
	{
		Vector vec = edgeEndPntRot - edgeEndPnt0;
		Scalar dist;
		Vector dir = Normalize(vec, dist);
		if (dist > 1e-4f)
		{
			Vector norm = Cross(edgeEndPnt0-edgePntRot, vec);
			Scalar normLen;
			norm = Normalize(norm, normLen);
			if (normLen > 1e-4f)
			{
				Scalar startDist = Dot(pnt0-edgePntRot, norm);
				Scalar endDist = Dot(pnt1-edgePntRot, norm);

				if (startDist * endDist <= 0.0f && Abs(startDist-endDist) > Scalar(1e-6f))
				{
					Scalar scSegT = startDist/(startDist-endDist);
					Point hitPnt = Lerp(pnt0, pnt1, scSegT);
					Vector hitDir = hitPnt - edgePntRot;
					Scalar hitDist;
					hitDir = Normalize(hitDir, hitDist);
					bool hit = false;
					if (hitDist > Scalar(1e-4f))
					{
						Scalar ss, tt;
						if (LineLineIntersection(edgeEndPnt0, dir, edgePntRot, hitDir, ss, tt))
						{
							if (ss >= 0.0f && ss <= dist && tt >= hitDist)
							{
								// Collision during the rotation phase is considered t = 0.0
								t = 0.0f;
								segT = scSegT;
								edgeT = hitDist/tt;
								hitRot = !hitRot; // if we hit again, that cancels previous hit (crossing the rope segment twice)
							}
						}
					}
				}
			}
		}
	}

	// Now linear movement of the edge from edgePntRot to edgePnt1
	{
		Scalar dist;
		Vector dir = SafeNormalize(linMove, kZero, dist);
		Vector norm = Cross(dir, edgeVec1);
		Scalar normLen;
		norm = Normalize(norm, normLen);
		if (normLen > 1e-6f)
		{
			Scalar startDist = Dot(pnt0-edgePntRot, norm);
			Scalar endDist = Dot(pnt1-edgePntRot, norm);

			if (startDist * endDist <= 0.0f && Abs(startDist-endDist) > Scalar(1e-6f))
			{
				Scalar scSegT = startDist/(startDist-endDist);
				Point hitPnt = Lerp(pnt0, pnt1, scSegT);
				Scalar ss, tt;
				if (LineLineIntersection(edgePntRot, dir, hitPnt, edgeVec1, ss, tt))
				{
					if (ss >= 0.0f && ss <= dist && tt <= 0.0f && tt >= -edgeLen)
					{
						t = ss/dist;
						ASSERT(IsFinite(t));
						segT = scSegT;
						edgeT = -tt/edgeLen;
						hitRot = !hitRot; // if we hit again, that cancels previous hit (crossing the rope segment twice)
					}
				}
			}
		}
	}

	if (hitRot)
		return true;

	// We also want to check that the final position of the edge is not within our tolerance of the rope
	// because if it is the rope would "catch" onto it as it would try to move even if it would be moving away
	{
		Scalar ropeSegLen;
		Vector ropeSegDir = SafeNormalize(pnt1 - pnt0, kUnitZAxis, ropeSegLen);
		Scalar finalDist, segSegT, segSegS;
		if (SegSegDist(edgePnt1, edgeVec1, edgeLen, pnt0, ropeSegDir, ropeSegLen, kScEdgeTol, finalDist, segSegT, segSegS))
		{
			t = 2.0f; // to indicate the edge actually didn't hit the rope
			return true;
		}
	}

	return false;
}

bool Rope2::MoveEdgeCollideWithRope(U32F edgeIndex, const Locator& locPrev, const Locator& loc, const EdgePoint* pEdges, U32F numEdges)
{
	const RopeColEdge& edge = m_colCache.m_pEdges[edgeIndex];

	for (U32F ii = 1; ii<numEdges; ii++)
	{
		const EdgePoint& ePnt0 = pEdges[ii-1];
		const EdgePoint& ePnt1 = pEdges[ii];

		// @@JS: also skip edges of previous/next edge points if they are within tolerance?

		bool skip = false;
		for (U32F iEIndex = 0; iEIndex<ePnt0.m_numEdges; iEIndex++)
		{
			if (ePnt0.m_edgeIndices[iEIndex] == edgeIndex)
			{
				skip = true;
				break;
			}
		}
		for (U32F iEIndex = 0; iEIndex<ePnt1.m_numEdges; iEIndex++)
		{
			if (ePnt1.m_edgeIndices[iEIndex] == edgeIndex)
			{
				skip = true;
				break;
			}
		}

		if (!skip)
		{
			F32 t, segT, edgeT;
			if (MoveEdgeCollideWithSegment(edge, locPrev, loc, ePnt0.m_pos, ePnt1.m_pos, t, segT, edgeT))
			{
				return true;
			}
		}
	}

	return false;
}

void Rope2::MoveEdgesCollideWithRope(ExternalBitArray& edges, const EdgePoint* pEdges, U32F numEdges)
{
	PROFILE(Rope, MoveEdgesCollideWithRope);

	I32F lastShapeIndex = -1;
	Locator shapeLoc;
	Locator shapeLocPrev;

	for (U32F edgeIndex = edges.FindFirstSetBit(); edgeIndex < m_colCache.m_numEdges; edgeIndex = edges.FindNextSetBit(edgeIndex))
	{
		const RopeColEdge& edge = m_colCache.m_pEdges[edgeIndex];
		I32F triIndex = edge.m_triIndex;
		I32F shapeIndex = m_colCache.m_pTris[triIndex].m_shapeIndex;
		if (shapeIndex != lastShapeIndex)
		{
			const RopeColliderHandle& hCollider = m_colCache.m_pShapes[shapeIndex];
			shapeLoc = hCollider.GetLocator(this);
			shapeLocPrev = hCollider.GetPrevLocator(this);
			lastShapeIndex = shapeIndex;
		}

		if (!MoveEdgeCollideWithRope(edgeIndex, shapeLocPrev, shapeLoc, pEdges, numEdges))
		{
			edges.ClearBit(edgeIndex);
		}
	}
}

bool Rope2::MoveEdgesCollideWithSegment(const ExternalBitArray& edges, const EdgePoint* pEdgePoints, U32F numEdges, U32F iEdge, I32F& earliestEdgeIndex, F32& earliestT, F32& earliestSegT, F32& earliestEdgeT)
{
	// Skip edges present in edge points of the segment

	I32F lastShapeIndex = -1;
	Locator shapeLoc;
	Locator shapeLocPrev;

	const EdgePoint& ePnt0 = pEdgePoints[iEdge-1];
	const EdgePoint& ePnt1 = pEdgePoints[iEdge];

	earliestT = 2.0f;
	earliestEdgeIndex = -1;
	for (U32F edgeIndex = edges.FindFirstSetBit(); edgeIndex < m_colCache.m_numEdges; edgeIndex = edges.FindNextSetBit(edgeIndex))
	{
		bool skip = false;
		for (U32F iEIndex = 0; iEIndex<ePnt0.m_numEdges; iEIndex++)
		{
			if (ePnt0.m_edgeIndices[iEIndex] == edgeIndex)
			{
				skip = true;
				break;
			}
		}
		for (U32F iEIndex = 0; iEIndex<ePnt1.m_numEdges; iEIndex++)
		{
			if (ePnt1.m_edgeIndices[iEIndex] == edgeIndex)
			{
				skip = true;
				break;
			}
		}

		if (!skip)
		{
			const RopeColEdge& edge = m_colCache.m_pEdges[edgeIndex];
			I32F triIndex = edge.m_triIndex;
			I32F shapeIndex = m_colCache.m_pTris[triIndex].m_shapeIndex;
			if (shapeIndex != lastShapeIndex)
			{
				const RopeColliderHandle& hCollider = m_colCache.m_pShapes[shapeIndex];
				shapeLoc = hCollider.GetLocator(this);
				shapeLocPrev = hCollider.GetPrevLocator(this);
				lastShapeIndex = shapeIndex;
			}

			F32 t, segT, edgeT;
			if (MoveEdgeCollideWithSegment(edge, shapeLocPrev, shapeLoc, ePnt0.m_pos, ePnt1.m_pos, t, segT, edgeT))
			{
				if (t < earliestT)
				{
					Point newPos = Lerp(pEdgePoints[iEdge-1].m_pos, pEdgePoints[iEdge].m_pos, segT);
					bool canBeInserted = true;
					if (Dist(newPos, pEdgePoints[iEdge-1].m_pos) < kScEdgeTol && iEdge-1 > 0)
					{
						canBeInserted = pEdgePoints[iEdge-1].m_numEdges < EdgePoint::kMaxNumEdges && !CheckForActiveEdge(pEdgePoints[iEdge-2], edgeIndex);
					}
					else if (Dist(newPos, pEdgePoints[iEdge].m_pos) < kScEdgeTol && iEdge < numEdges-1)
					{
						canBeInserted = pEdgePoints[iEdge].m_numEdges < EdgePoint::kMaxNumEdges && !CheckForActiveEdge(pEdgePoints[iEdge+1], edgeIndex);
					}
					if (canBeInserted)
					{
						earliestT = t;
						earliestSegT = segT;
						earliestEdgeIndex = edgeIndex;
						earliestEdgeT = edgeT;
					}
				}
			}
		}
	}

	return earliestEdgeIndex >= 0;
}

void Rope2::SetPos1FromEdge(EdgePoint* pEdges, U32F iEdge, Point* pPos1, ExternalBitArray* pEdgesPrevStep)
{
	EdgePoint& ePoint = pEdges[iEdge];
	ASSERT(ePoint.m_numEdges > 0);

	U32F edgeIndex0 = ePoint.m_edgeIndices[0];
	const RopeColEdge& edge0 = m_colCache.m_pEdges[edgeIndex0];
	I32F triIndex0 = edge0.m_triIndex;
	I32F shapeIndex0 = m_colCache.m_pTris[triIndex0].m_shapeIndex;
	const RopeColliderHandle& hCollider0 = m_colCache.m_pShapes[shapeIndex0];

	for (U32F iPointEdge = 1; iPointEdge<ePoint.m_numEdges; iPointEdge++)
	{
		// @@JS: Check the movement of the edge point in respect to each body and split into more edge points if needed
		const RopeColEdge& edge = m_colCache.m_pEdges[ePoint.m_edgeIndices[iPointEdge]];
		I32F triIndex = edge.m_triIndex;
		I32F shapeIndex = m_colCache.m_pTris[triIndex].m_shapeIndex;
		const RopeColliderHandle& hCollider = m_colCache.m_pShapes[shapeIndex];
		JAROS_ALWAYS_ASSERT(RopeColliderHandle::AreCollidersAttached(hCollider, hCollider0));
	}

	if (!pEdgesPrevStep->IsBitSet(edgeIndex0))
	{
		pPos1[iEdge] = ePoint.m_pos;
	}
	else
	{
		Locator shapeLoc = hCollider0.GetLocator(this);
		Locator shapeLocPrev = hCollider0.GetPrevLocator(this);
		pPos1[iEdge] = shapeLoc.TransformPoint(shapeLocPrev.UntransformPoint(ePoint.m_pos));
	}
	PHYSICS_ASSERT(IsFinite(pPos1[iEdge]));
}

bool Rope2::MovePointCollideWithEdgesSetPos1FromEdge(EdgePoint* pEdges, U32F iEdge, U32F& numEdges, Point* pPos0, Point* pPos1, ExternalBitArray* pEdgesPrevStep)
{
	PROFILE(Rope, MovePointCollideWithEdgesSetPos1FromEdge);

	EdgePoint& ePoint = pEdges[iEdge];
	I32F edgeIndex = ePoint.m_numEdges > 0 ? ePoint.m_edgeIndices[0] : -1;
	U32F numEdgesBefore = numEdges;
	if (MovePointCollideWithEdges(pEdges, iEdge, numEdges, pPos0, pPos1, pEdgesPrevStep))
	{
		// New edge points have been inserted
		// We need to calculate the target pos for them (as the calculation inside MovePointCollideWithEdges does something else)
		if (iEdge == 0)
		{
			for (U32F ii = 0; ii<numEdges-numEdgesBefore; ii++)
			{
				SetPos1FromEdge(pEdges, ii+1, pPos1, pEdgesPrevStep);
			}
		}
		else if (iEdge == numEdgesBefore-1)
		{
			for (U32F ii = 0; ii<numEdges-numEdgesBefore; ii++)
			{
				SetPos1FromEdge(pEdges, numEdges-ii-2, pPos1, pEdgesPrevStep);
			}
		}
		else
		{
			// We recognize the old point by the edge index
			JAROS_ALWAYS_ASSERT(edgeIndex >= 0);
			for (U32F ii = 0; ii<numEdges-numEdgesBefore+1; ii++)
			{
				U32F iNewEdgePoint = iEdge+ii;
				EdgePoint& newEPoint = pEdges[iNewEdgePoint];
				ASSERT(newEPoint.m_numEdges > 0);
				if (newEPoint.m_edgeIndices[0] != edgeIndex)
				{
					SetPos1FromEdge(pEdges, iNewEdgePoint, pPos1, pEdgesPrevStep);
				}
			}
		}
		return true;
	}

	return false;
}

void Rope2::StepMoveEdgesAndRopeEndPoints(EdgePoint* pEdges, U32F& numEdges, Point_arg pos0, Point_arg pos1)
{
	PROFILE(Rope, StepMoveEdgesAndRopeEndPoints);

	if (m_colCache.m_numEdges == 0)
	{
		pEdges[0].m_pos = pos0;
		pEdges[numEdges-1].m_pos = pos1;
		return;
	}

	// Initial and target position for each edge point based on the movement of the edge

	Point* pPos0 = NDI_NEW Point[m_maxNumPoints];
	Point* pPos1 = NDI_NEW Point[m_maxNumPoints];

	for (U32F ii = 0; ii<numEdges; ii++)
	{
		const EdgePoint& ePoint = pEdges[ii];

		pPos0[ii] = ePoint.m_pos;

		if (ii == 0)
		{
			pPos1[ii] = pos0;
		}
		else if (ii == numEdges-1)
		{
			pPos1[ii] = pos1;
		}
		else if (ePoint.m_numEdges)
		{
			U32F edgeIndex0 = ePoint.m_edgeIndices[0];
			const RopeColEdge& edge0 = m_colCache.m_pEdges[edgeIndex0];
			I32F triIndex0 = edge0.m_triIndex;
			I32F shapeIndex0 = m_colCache.m_pTris[triIndex0].m_shapeIndex;
			const RopeColliderHandle& hCollider0 = m_colCache.m_pShapes[shapeIndex0];

			for (U32F iPointEdge = 1; iPointEdge<ePoint.m_numEdges; iPointEdge++)
			{
				// @@JS: Check the movement of the edge point in respect to each body and split into more edge points if needed
				const RopeColEdge& edge = m_colCache.m_pEdges[ePoint.m_edgeIndices[iPointEdge]];
				I32F triIndex = edge.m_triIndex;
				I32F shapeIndex = m_colCache.m_pTris[triIndex].m_shapeIndex;
				const RopeColliderHandle& hCollider = m_colCache.m_pShapes[shapeIndex];
				JAROS_ALWAYS_ASSERT(RopeColliderHandle::AreCollidersAttached(hCollider, hCollider0));
			}

			Locator shapeLoc = hCollider0.GetLocator(this);
			Locator shapeLocPrev = hCollider0.GetPrevLocator(this);
			pPos1[ii] = shapeLoc.TransformPoint(shapeLocPrev.UntransformPoint(ePoint.m_pos));

			// Adjust target pos for edge collision trim
			for (U32F iPointEdge = 0; iPointEdge<ePoint.m_numEdges; iPointEdge++)
			{
				const RopeColEdge& edge = m_colCache.m_pEdges[ePoint.m_edgeIndices[iPointEdge]];
				if (edge.m_startTriIndex >= 0 || edge.m_endTriIndex >= 0)
				{
					Scalar t = Dot(pPos1[ii] - edge.m_pnt, Vector(edge.m_vec));
					if (edge.m_startTriIndex >= 0 && t < 0.0f)
					{
						pPos1[ii] -= t * Vector(edge.m_vec);
					}
					else if (edge.m_endTriIndex >= 0 && t > edge.m_vec.W())
					{
						pPos1[ii] -= (t - edge.m_vec.W()) * Vector(edge.m_vec);
					}
				}
			}
		}
		else
		{
			pPos1[ii] = pPos0[ii];
		}
		PHYSICS_ASSERT(IsFinite(pPos1[ii]));
	}

	U64* pCollidingEdgesData = NDI_NEW U64[ExternalBitArray::DetermineNumBlocks(m_colCache.m_maxEdges)];
	ExternalBitArray collidingEdges(m_colCache.m_maxEdges, pCollidingEdgesData);

	{
		// Which edges have moved?
		bool shapeMoved = false;
		I32F lastShapeIndex = -1;
		for (U32F edgeIndex = 0; edgeIndex < m_colCache.m_numEdges; edgeIndex++)
		{
			const RopeColEdge& edge = m_colCache.m_pEdges[edgeIndex];
			I32F triIndex = edge.m_triIndex;
			I32F shapeIndex = m_colCache.m_pTris[triIndex].m_shapeIndex;
			if (shapeIndex != lastShapeIndex)
			{
				const RopeColliderHandle& hCollider = m_colCache.m_pShapes[shapeIndex];
				Locator shapeLoc = hCollider.GetLocator(this);
				Locator shapeLocPrev = hCollider.GetPrevLocator(this);
				shapeMoved = shapeLoc != shapeLocPrev;
				lastShapeIndex = shapeIndex;
			}

			collidingEdges.AssignBit(edgeIndex, shapeMoved);
		}
	}

	MoveEdgesCollideWithRope(collidingEdges, pEdges, numEdges);

	while (MovePointCollideWithEdgesSetPos1FromEdge(pEdges, 0, numEdges, pPos0, pPos1, &collidingEdges))
	{
		MoveEdgesCollideWithRope(collidingEdges, pEdges, numEdges);
	}

	while (MovePointCollideWithEdgesSetPos1FromEdge(pEdges, numEdges-1, numEdges, pPos0, pPos1, &collidingEdges))
	{
		MoveEdgesCollideWithRope(collidingEdges, pEdges, numEdges);
	}

	U32F ii = 1;
	while (ii <= numEdges-1)
	{
		if (ii < numEdges-1 && MovePointCollideWithEdgesSetPos1FromEdge(pEdges, ii, numEdges, pPos0, pPos1, &collidingEdges))
		{
			MoveEdgesCollideWithRope(collidingEdges, pEdges, numEdges);
		}
		else
		{
			I32F earliestEdgeIndex;
			F32 earliestT;
			F32 earliestSegT;
			F32 earliestEdgeT;
			if (MoveEdgesCollideWithSegment(collidingEdges, pEdges, numEdges, ii, earliestEdgeIndex, earliestT, earliestSegT, earliestEdgeT) && earliestT <= 1.0f)
			{
				const RopeColEdge& edge = m_colCache.m_pEdges[earliestEdgeIndex];
				I32F triIndex = edge.m_triIndex;
				I32F shapeIndex = m_colCache.m_pTris[triIndex].m_shapeIndex;
				const RopeColliderHandle& hCollider = m_colCache.m_pShapes[shapeIndex];

				Point newPos = Lerp(pEdges[ii-1].m_pos, pEdges[ii].m_pos, earliestSegT);
				Point newPos1 = edge.m_pnt + earliestEdgeT * edge.m_vec.W() * Vector(edge.m_vec);

				// Check the new edge for collision trimming
				{
					if (m_colCache.CheckEdgeTrims(earliestEdgeIndex, this))
					{
						RopeColTriId triIdInvalid;
						Point adjustedPos;
						earliestEdgeIndex = m_colCache.FitPointToTrimmedEdge(earliestEdgeIndex, newPos1, triIdInvalid, triIdInvalid, adjustedPos);
						newPos1 = adjustedPos;

						// Copy colliding edge bit to all new trims
						for (U32F iNewTrim = 0; iNewTrim < edge.m_numExtraTrims; iNewTrim++)
						{
							collidingEdges.AssignBit(edge.m_secondTrimIndex+iNewTrim, collidingEdges.IsBitSet(earliestEdgeIndex));
						}
					}
				}

				if (Dist(newPos, pEdges[ii-1].m_pos) < kScEdgeTol && ii-1 > 0)
				{
					if (pEdges[ii-1].m_numEdges > 0)
					{
						const RopeColEdge& edge0 = m_colCache.m_pEdges[pEdges[ii-1].m_edgeIndices[0]];
						I32F triIndex0 = edge0.m_triIndex;
						I32F shapeIndex0 = m_colCache.m_pTris[triIndex0].m_shapeIndex;
						const RopeColliderHandle& hCollider0 = m_colCache.m_pShapes[shapeIndex0];
						JAROS_ALWAYS_ASSERT(RopeColliderHandle::AreCollidersAttached(hCollider, hCollider0));
					}
					AddEdgeToEdgePoint(pEdges, numEdges, ii-1, earliestEdgeIndex, true, false);
				}
				else if (Dist(newPos, pEdges[ii].m_pos) < kScEdgeTol && ii < numEdges-1)
				{
					if (pEdges[ii].m_numEdges > 0)
					{
						const RopeColEdge& edge0 = m_colCache.m_pEdges[pEdges[ii].m_edgeIndices[0]];
						I32F triIndex0 = edge0.m_triIndex;
						I32F shapeIndex0 = m_colCache.m_pTris[triIndex0].m_shapeIndex;
						const RopeColliderHandle& hCollider0 = m_colCache.m_pShapes[shapeIndex0];
						JAROS_ALWAYS_ASSERT(RopeColliderHandle::AreCollidersAttached(hCollider, hCollider0));
					}
					AddEdgeToEdgePoint(pEdges, numEdges, ii, earliestEdgeIndex, true, false);
				}
				else
				{
					if (numEdges >= m_maxNumPoints)
					{
						JAROS_ALWAYS_ASSERT(false);
						break;
					}
					else
					{
						InsertEdgePoint(pEdges, numEdges, ii, newPos, pEdges[ii-1].m_ropeDist, pEdges[ii].m_ropeDist, pPos0, pPos1, 0.0f, earliestSegT);
						AddEdgeToEdgePoint(pEdges, numEdges, ii, earliestEdgeIndex, true, false);
						pPos1[ii] = newPos1;
					}
				}
			}
			else
			{
				ii++;
			}
		}
	}
}
