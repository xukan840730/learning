/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/ndphys/rope/rope2-util.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/ndphys/collide-utils.h"
#include "ndlib/render/util/prim.h"

F32 Rope2::Raycast(Point from, Point to, F32 tol, U32& pointIndex) const
{
	CheckStepNotRunning();

	Vector rayVec = to - from;
	Scalar rayLen;
	rayVec = SafeNormalize(rayVec, kUnitXAxis, rayLen);
	F32 res = -1.0f;
	for (U32 ii = 1; ii<m_numPoints; ii++)
	{
		Vector segVec = m_pPos[ii] - m_pPos[ii-1];
		Scalar segLen;
		segVec = SafeNormalize(segVec, kUnitXAxis, segLen);
		F32 r = m_pRadius[ii] + tol * Dist(from, m_pPos[ii-1]);
		Scalar dist, s, t;
		if (SegSegDist(from, rayVec, rayLen, m_pPos[ii-1], segVec, segLen, r, dist, s, t))
		{
			pointIndex = t < 0.5f * segLen ? ii-1 : ii;
			F32 fraction = s / rayLen;
			if (res >= 0.0f)
			{
				res *= fraction;
			}
			else
			{
				res = fraction;
			}
			rayLen *= fraction;
		}
	}
	return res;
}

Point Rope2::ClosestPointOnRope(F32 minDist, const Segment& checkSeg, F32& retDist, F32* pRopeDistOut) const
{
	CheckStepNotRunning();

	F32 bestDist = kLargestFloat;
	Point bestPoint = kZero;
	F32 bestRopeDist = -1.0;

	I32 startIndex = I32(minDist  / m_fSegmentLength);

	Vector checkSegVec = checkSeg.b - checkSeg.a;
	Scalar checkSegLen;
	checkSegVec = SafeNormalize(checkSegVec, kUnitXAxis, checkSegLen);

	for (U32F idx = 0; idx < m_numPoints - 1; ++idx)
	{
		if (m_pRopeDist[idx+1] <= minDist)
			continue;
		Point segStart = m_pPos[idx];
		F32 segStartRopeDist = m_pRopeDist[idx];
		if (m_pRopeDist[idx] < minDist)
		{
			segStart = Lerp(segStart, m_pPos[idx+1], (minDist-m_pRopeDist[idx]/(m_pRopeDist[idx+1]-m_pRopeDist[idx])));
			segStartRopeDist = minDist;
		}

		Vector segVec = m_pPos[idx+1] - segStart;
		Scalar segLen;
		segVec = SafeNormalize(segVec, kUnitXAxis, segLen);
		Scalar dist, s, t;
		if (SegSegDist(checkSeg.a, checkSegVec, checkSegLen, segStart, segVec, segLen, bestDist, dist, s, t))
		{
			bestDist = dist;
			bestPoint = segStart + t * segVec;
			bestRopeDist = Lerp(segStartRopeDist, m_pRopeDist[idx+1], (F32)(t/segLen));
		}
	}

	retDist = bestDist;
	if (pRopeDistOut)
		*pRopeDistOut = bestRopeDist;
	return bestPoint;
}

