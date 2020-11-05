/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/nav-assert.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class Locator;

/// --------------------------------------------------------------------------------------------------------------- ///
class IPathWaypoints
{
public:

	/// Constructors.
	IPathWaypoints() : m_count(0) {}

	void Clear() { m_count = 0; }
	I32F GetWaypointCount() const { return m_count; }

	virtual I32F GetMaxWaypointCount() const = 0;
	virtual const Point GetWaypoint(I32F i) const = 0;

	virtual void AddWaypoint(Point_arg pos) = 0;
	virtual void UpdateWaypoint(I32F i, Point_arg newPoint) = 0;

	const Point GetEndWaypoint() const
	{
		const I32F count = GetWaypointCount();
		NAV_ASSERT(count > 0);
		return GetWaypoint(count - 1);
	}

	void UpdateEndPoint(Point_arg newEndPt)
	{
		const I32F count = GetWaypointCount();

		if (count >= 2)
		{
			UpdateWaypoint(count - 1, newEndPt);
		}
	}

	void TruncatePath(I32F newPathLength)
	{
		NAV_ASSERT(newPathLength < m_count);
		m_count = newPathLength;
	}

	void RemoveEndWaypoint()
	{
		if (m_count > 0)
		{
			--m_count;
		}
	}

	void DebugDraw(const Locator& pathSpace,
				   bool drawIndices,
				   Color color0 = kColorRed,
				   Color color1 = kColorGreen,
				   float radius = 0.0f,
				   DebugPrimTime tt = kPrimDuration1FrameAuto) const;

	bool IsValid() const { return m_count > 1 && m_count <= GetMaxWaypointCount(); }
	bool IsFull() const { return GetWaypointCount() == GetMaxWaypointCount(); }

	// Path analyzers
	float ComputeCosineSharpestTurnAngle(float lookaheadDist, float skipDist = 0.0f) const;
	float ComputePathLength() const;
	float ComputePathLengthXz() const;
	I32F FindClosestIntersectingLeg(Point_arg searchOriginPs, float searchRadius, Point* pClosestPosOut) const;
	bool IntersectSegmentXz(const Segment& seg, Point* pClosestPtOut = nullptr) const;
	float ComputeRemainingLength(Point_arg startPos) const;
	Point AdvanceAlongPath(Point_arg startPos, float dist, I32F iStartingLeg = -1, I32F* pNewLegOut = nullptr) const;
	Point AdvanceAlongPathXz(Point_arg startPos, float dist, I32F iStartingLeg = -1, I32F* pNewLegOut = nullptr) const;

	void CopyFrom(const IPathWaypoints* pSourceWaypoints);

	Point GetPointAtDistance(const float dist, Vector* pLegOut = nullptr, I32F* pILegOut = nullptr) const;
	float GetDistanceAtPoint(Point_arg pos) const;

	Point ClosestPoint(Point_arg pos,
					   float* pDistanceOut	= nullptr,
					   I32F* pClosestLegOut = nullptr,
					   float* pLegTTOut		= nullptr) const;
	Point ClosestPointXz(Point_arg pos,
						 float* pDistanceOut  = nullptr,
						 I32F* pClosestLegOut = nullptr,
						 float* pLegTTOut	  = nullptr,
						 float bias			  = 0.0f) const;

	float RemainingPathDistFromPosXz(Point_arg pos) const;

	void Reverse();
	void Append(const IPathWaypoints& rhs);

	void Validate() const;

	void ChangeParentSpace(const Transform& matOldToNew);

protected:
	U32 m_count;
};

/// --------------------------------------------------------------------------------------------------------------- ///
template <size_t MAX_COUNT>
class TPathWaypoints : public IPathWaypoints
{
public:
	const static size_t kMaxCount = MAX_COUNT;

	virtual I32F GetMaxWaypointCount() const override { return kMaxCount; }

	virtual const Point GetWaypoint(I32F i) const override
	{
		NAV_ASSERT(i < GetWaypointCount());
		return m_points[i];
	}

	virtual void AddWaypoint(Point_arg pos) override
	{
		NAV_ASSERT(IsReasonable(pos));

		if (!IsFull())
		{
			m_points[m_count] = pos;
			++m_count;
		}
	}

	virtual void UpdateWaypoint(I32F i, Point_arg newPoint) override
	{
		NAV_ASSERT(IsReasonable(newPoint));

		if (i < GetWaypointCount())
		{
			m_points[i] = newPoint;
		}
	}

protected:
	Point m_points[kMaxCount];
};

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePathReachXz(const IPathWaypoints* pPath, Point_arg pos);

bool IntersectPathWaypoints(const IPathWaypoints* pSrcPath,
							const Locator& srcParentSpace,
							const IPathWaypoints* pDestPath,
							const Locator& destParentSpace,
							F32 yThreshold = 0.25f,
							F32* pDist	   = nullptr,
							Point* pSrcClosestPosWs	 = nullptr,
							Point* pDestClosestPosWs = nullptr);
