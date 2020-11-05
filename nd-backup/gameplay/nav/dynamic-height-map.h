/*
 * Copyright (c) 2020 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/math/basicmath.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class DynamicHeightMap
{
public:
	static CONST_EXPR int kBufSize = 144;

	// regenerated dynamically
	ndgi::Texture m_tex;

	U8 m_unused[8];

	// persistent
	Point m_bboxMinWs;
	U32 m_sizeX;
	U32 m_sizeZ;
	U32 m_sizeY;
	U32 m_expSourceMask;
	U8 m_data[kBufSize];

	Aabb GetAabbWs() const
	{
		return Aabb(m_bboxMinWs, m_bboxMinWs + Vector(m_sizeX * kHeightMapGridSpacing,
													  m_sizeY * kHeightMapHeightSpacing,
													  m_sizeZ * kHeightMapGridSpacing));
	}

	void GenerateTexture();

	template<class FunctorComputeY>
	void Populate(FunctorComputeY f, const Aabb& aabbWs, U32 expSourceMask)
	{
		NAV_ASSERT(expSourceMask < (1U << DC::kExposureSourceCount));
		m_expSourceMask = expSourceMask;

		const Vector vScale(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing);
		const Vector vInvScale(kInvHeightMapGridSpacing, kInvHeightMapHeightSpacing, kInvHeightMapGridSpacing);

		// note that the Ys of the aabb are ignored

		// pull min back to cell boundary
		const Point minWs = AsPoint(Floor(AsVector(aabbWs.m_min) * vInvScale) * vScale);
		const Point offsetMinWs = minWs + 0.5f * Vector(kHeightMapGridSpacing, 0.0f, kHeightMapGridSpacing);

		const Vector deltaHs = Ceil((aabbWs.m_max - minWs) * vInvScale);

		m_sizeX = Max((int)deltaHs.X(), 2);
		m_sizeZ = Max((int)deltaHs.Z(), 2);

		const int numCells = m_sizeX * m_sizeZ;
		if (numCells > kBufSize)
		{
			NAV_ASSERTF(false, ("Dynamic heightmap too large in XZ!\n"));
			m_sizeX = Min(m_sizeX, 8U);
			m_sizeZ = Min(m_sizeZ, 8U);
		}

		// query the client functor for y values
		float ys[kBufSize];
		float minY = NDI_FLT_MAX;
		float maxY = -NDI_FLT_MAX;
		for (int iZ = 0; iZ < m_sizeZ; ++iZ)
		{
			for (int iX = 0; iX < m_sizeX; ++iX)
			{
				const int idx = iZ * m_sizeX + iX;
				const Point ptWs = offsetMinWs + vScale * Vector(iX, 0, iZ);
				const float y = f(ptWs);

				ys[idx] = y;

				// sentinel meaning 'hole'
				if (y != -NDI_FLT_MAX)
				{
					minY = Min(minY, y);
					maxY = Max(maxY, y);
				}
			}
		}

		const int iMinY = FastCvtToIntRound(minY * kInvHeightMapHeightSpacing);
		const int iMaxY = FastCvtToIntRound(maxY * kInvHeightMapHeightSpacing);

		// reserving 0 as sentinel meaning 'hole'
		const int iBaseY = iMinY - 1;
		const int iDeltaY = iMaxY - iBaseY;

		m_sizeY = Min(iDeltaY, 256);
		NAV_ASSERTF(iDeltaY < 256, ("Dynamic heightmap too large in Y!\n"));

		for (int iZ = 0; iZ < m_sizeZ; ++iZ)
		{
			for (int iX = 0; iX < m_sizeX; ++iX)
			{
				const int idx = iZ * m_sizeX + iX;
				const float y = ys[idx];

				m_data[idx] = y == -NDI_FLT_MAX ? 0 : FastCvtToIntRound(y * kInvHeightMapHeightSpacing) - iBaseY;
			}
		}

		m_bboxMinWs = PointFromXzAndY(minWs, iBaseY * kHeightMapHeightSpacing);
	}

	void DebugDraw() const;
};

STATIC_ASSERT(sizeof(DynamicHeightMap) == DynamicHeightMap::kBufSize + 48);
