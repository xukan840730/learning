/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/nav/nav-blocker-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
namespace Clip2D
{
	struct Poly;
}

struct NavMeshPatchInput;
class NavPoly;

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavPolyList
{
	static const size_t kMaxPolys = 256;

	const NavPoly* m_pPolys[kMaxPolys];
	float m_distances[kMaxPolys];

	size_t m_numPolys;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavMeshGapEx
{
	Point m_pos0Ls;
	Point m_pos1Ls;
	float m_gapDist;
	NavBlockerBits m_blockerBits;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void GenerateNavMeshExGaps(const NavMeshPatchInput& patchInput,
						   const Clip2D::Poly* pBlockerClipPolysWs,
						   const NavPolyList* pBlockerPolys,
						   const NavBlockerBits& blockersToGen,
						   float maxGapDiameter);
