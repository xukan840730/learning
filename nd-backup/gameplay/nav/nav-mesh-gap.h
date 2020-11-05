/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/nav/nav-blocker-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NavMesh;

const F32 kMaxNavMeshGapDiameter = 3.0f;

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavMeshGap
{
	Point m_pos0;
	Point m_pos1;

	StringId64 m_blockerId0;
	StringId64 m_blockerId1;

	float m_gapDist;

	mutable bool m_enabled0;
	mutable bool m_enabled1;

	bool m_clearance;

	mutable Nav::StaticBlockageMask m_blockageMask0 : 8; // U8. Set at login. Tools know nothing of this.
	mutable Nav::StaticBlockageMask m_blockageMask1 : 8; // U8. Set at login. Tools know nothing of this.

	U8 m_pad[7];

	void DebugDraw(const NavMesh* pMesh, const Color& col, float yOffset) const;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavMeshGapRef
{
	I64 m_gapIndex;
	mutable const NavMeshGap* m_pGap;
};

/// --------------------------------------------------------------------------------------------------------------- ///
typedef void (*FnAddGapCallback)(float gapDist, Point_arg p0, Point_arg p1, uintptr_t userdata);

/// --------------------------------------------------------------------------------------------------------------- ///
void TryGenerateGaps(Point_arg v00,
					 Point_arg v01,
					 Point_arg v10,
					 Point_arg v11,
					 float maxGapDiameter,
					 FnAddGapCallback pFnAddGapCb,
					 uintptr_t cbUserData);
