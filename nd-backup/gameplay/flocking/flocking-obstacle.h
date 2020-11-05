/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef FLOCKING_OBSTACLE_H
#define FLOCKING_OBSTACLE_H

class NavMesh;

namespace Flocking
{
	class FlockingRvoObstacle
	{
	public:
		void Init(const Point_arg p0, const Point_arg p1, const NavMesh* pNavMesh);

		void DebugDraw(int rvoObstacleIdx);

	public:
		Point m_p0;
		Point m_p1;
		Vector m_normalXz;
		const NavMesh* m_pNavMesh;
	};
}

#endif // FLOCKING_OBSTACLE_H


