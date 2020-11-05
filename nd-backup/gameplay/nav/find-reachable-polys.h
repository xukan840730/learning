/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-path-find.h"

namespace Nav
{
	/// --------------------------------------------------------------------------------------------------------------- ///
	struct FindReachablePolysResults
	{
		const static U32F kMaxPolyOutCount = 1024;

		BitArray<kMaxPolyOutCount> m_fringeNodes;
		NavPolyHandle m_navPolys[kMaxPolyOutCount];
		Vec4 m_travelCostVecs[kMaxPolyOutCount];
		U32 m_outCount;

		F32 m_timeUs;	 // how long the job took
		bool m_finished; // job finished
		ndjob::CounterHandle
			m_pCounter; // would have liked to hide this, but this structure can be allocated outside of the search job

		FindReachablePolysResults() : m_outCount(0), m_timeUs(0.0f), m_finished(false), m_pCounter(nullptr)
		{
			m_fringeNodes.ClearAllBits();
		}
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct FindReachablePolysParams : public Nav::PathFindParams
	{
		FindReachablePolysParams() : PathFindParams(), m_radius(0.0f) {}

		F32 m_radius;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	// jobified
	FindReachablePolysResults* BeginFindReachablePolys(const FindReachablePolysParams& params,
													   const char* sourceFile,
													   U32 sourceLine,
													   const char* sourceFunc,
													   FindReachablePolysResults* pExistingFindPolysResults = nullptr);

	void CollectFindReachablePolysResults(FindReachablePolysResults* pFindPolysResults);

	/// --------------------------------------------------------------------------------------------------------------- ///
	// synchronous
	void FindReachablePolys(const FindReachablePolysParams& params, FindReachablePolysResults* pResults);
	U32F FindReachablePolys(NavPolyHandle* ppOutList,
							Vec4* pOutTravelCostVecs,
							U32F maxPolyCount,
							const FindReachablePolysParams& params);
} // namespace Nav
