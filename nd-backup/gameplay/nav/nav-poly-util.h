/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavMeshBlocker;

/// --------------------------------------------------------------------------------------------------------------- ///
namespace NavPolyUtil
{
	Scalar FindNearestPoint(Point_arg inPt, const Point* pVerts, size_t numVerts, Point* pPosOut);
	Scalar FindNearestPointXz(Point_arg inPt, const Point* pVerts, size_t numVerts, Point* pPosOut);
	void GetPointAndNormal(Point_arg inPt, const Point* pVerts, size_t numVerts, Point* pPosOut, Vector* pNormalOut);

	bool QuadContainsPointXzFast(const Point inPt,
								 const Point* const __restrict pVerts);
	bool QuadContainsPointXz(const Point inPt,
							 const Point* const __restrict pVerts,
							 float epsilon = NDI_FLT_EPSILON,
							 Vec4* const __restrict pDotsOut = nullptr);

	bool BlockerContainsPointXzPs(Point_arg posPs, const NavMeshBlocker* pBlocker, const Locator& boxLocPs);
} // namespace NavPolyUtil
