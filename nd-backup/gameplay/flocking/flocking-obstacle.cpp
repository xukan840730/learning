/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/flocking/flocking-obstacle.h"

#include "ndlib/render/util/prim.h"

namespace Flocking
{
	static float kLenEpsilon = 0.001f;
	static float kLenEpsilonSqr = kLenEpsilon * kLenEpsilon;

	void FlockingRvoObstacle::Init(const Point_arg p0, 
								const Point_arg p1, 
								const NavMesh *const pNavMesh)
	{
		m_p0 = p0;
		m_p1 = p1;

		const Point p0Xz = Point(p0.X(), 0.0f, p0.Z());
		const Point p1Xz = Point(p1.X(), 0.0f, p1.Z());
		const Vector p0p1Xz = p1Xz - p0Xz;

		const Vector perpXz = Vector(p0p1Xz.Z(), 0.0f, -p0p1Xz.X());
		if (LengthSqr(perpXz) < kLenEpsilon)
		{
			m_normalXz = Vector(kZero);
		}
		else
		{
			m_normalXz = Normalize(perpXz);
		}

		m_pNavMesh = pNavMesh;
	}

	void FlockingRvoObstacle::DebugDraw(int rvoObstacleIdx)
	{
		if (LengthSqr(m_normalXz) < kLenEpsilonSqr)	// Degenerated
		{
			g_prim.Draw(DebugCross(m_p0, 1.0f, kColorRedTrans));
			g_prim.Draw(DebugString(m_p0, StringBuilder<8>("%d", rvoObstacleIdx).c_str(), kColorRedTrans, 0.5f));
		}
		else
		{
			g_prim.Draw(DebugLine(m_p0, m_p1, kColorRedTrans, 3.4f));

			const Point midPos = AveragePos(m_p0, m_p1);
			g_prim.Draw(DebugArrow(midPos, midPos + m_normalXz, kColorRedTrans));
			g_prim.Draw(DebugString(midPos, StringBuilder<8>("%d", rvoObstacleIdx).c_str(), kColorRedTrans, 0.5f));

			const Point p0Xz = Point(m_p0.X(), 0.0f, m_p0.Z());
			const Point p1Xz = Point(m_p1.X(), 0.0f, m_p1.Z());
			g_prim.Draw(DebugLine(p0Xz, p1Xz, kColorRedTrans));

			g_prim.Draw(DebugLine(p0Xz, m_p0, kColorRedTrans));
			g_prim.Draw(DebugLine(p1Xz, m_p1, kColorRedTrans));
		}
	}
}