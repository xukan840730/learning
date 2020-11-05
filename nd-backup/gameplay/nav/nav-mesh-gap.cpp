/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nav/nav-mesh-gap.h"

#include "corelib/math/segment-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "ndlib/render/util/prim.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshGap::DebugDraw(const NavMesh* pMesh, const Color& col, float yOffset) const
{
	STRIP_IN_FINAL_BUILD;

	if (!pMesh)
		return;

	const Color colorToUse = (m_enabled0 && m_enabled1) ? col : kColorGray;

	const Vector vo	  = Vector(0.0f, yOffset, 0.0f);
	const Point v0Ls  = m_pos0;
	const Point v1Ls  = m_pos1;
	const Point v0Ws  = pMesh->LocalToWorld(v0Ls) + vo;
	const Point v1Ws  = pMesh->LocalToWorld(v1Ls) + vo;
	const Point midWs = AveragePos(v0Ws, v1Ws);

	StringBuilder<256> desc;

	desc.format("%0.3f", m_gapDist);

	if (m_clearance)
	{
		desc.append_format(", clear");
	}

	if (m_blockerId0 != INVALID_STRING_ID_64 && m_blockerId1 != INVALID_STRING_ID_64)
	{
		desc.append_format(" [%s -> %s]", DevKitOnly_StringIdToString(m_blockerId0), DevKitOnly_StringIdToString(m_blockerId1));
	}
	else if (m_blockerId0 != INVALID_STRING_ID_64)
	{
		desc.append_format(" [%s -> <none>]", DevKitOnly_StringIdToString(m_blockerId0));
	}
	else if (m_blockerId1 != INVALID_STRING_ID_64)
	{
		desc.append_format(" [<none> -> %s]", DevKitOnly_StringIdToString(m_blockerId1));
	}

	g_prim.Draw(DebugLine(v0Ws, v1Ws, colorToUse, 3.0f));
	g_prim.Draw(DebugString(midWs,
							desc.c_str(),
							colorToUse,
							0.5f));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TryGenerateGaps(Point_arg v00, Point_arg v01, Point_arg v10, Point_arg v11, float maxGapDiameter, FnAddGapCallback pFnAddGapCb, uintptr_t cbUserData)
{
	//PROFILE_ACCUM(TryGenerateGaps);

	const Point v00xz = PointFromXzAndY(v00, kZero);
	const Point v01xz = PointFromXzAndY(v01, kZero);
	const Point v10xz = PointFromXzAndY(v10, kZero);
	const Point v11xz = PointFromXzAndY(v11, kZero);

	const Vector e0NormalVec = RotateYMinus90(SafeNormalize(v00 - v01, kZero));
	const Vector e1NormalVec = RotateYMinus90(SafeNormalize(v10 - v11, kZero));

	// if normals don't point towards each other, don't create a gap
	const float normDot = DotXz(e0NormalVec, e1NormalVec);
	if (normDot >= -0.3f)
		return;

	const Segment seg0 = Segment(v00xz, v01xz);
	const Segment seg1 = Segment(v10xz, v11xz);

	Scalar d = kLargeFloat;
	Scalar t0 = -1.0f;
	Scalar t1 = -1.0f;

#if 0
	CollideUtils::DistanceSegmentSegment dss;

	dss.Compute(seg0, seg1);

	d = dss.d;
	t0 = dss.t[0];
	t1 = dss.t[1];
#else
	d = DistSegmentSegmentXz(seg0, seg1, t0, t1);
#endif

	if (d > maxGapDiameter)
		return;

	const Point p0 = Lerp(v00, v01, t0);
	const Point p1 = Lerp(v10, v11, t1);

	if (DotXz(p1 - p0, e0NormalVec) <= 0.0f)
		return;

	if (DotXz(p0 - p1, e1NormalVec) <= 0.0f)
		return;

	pFnAddGapCb(d, p0, p1, cbUserData);
}
