/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

namespace Nav
{
	struct FindSinglePathParams;
	struct FindSinglePathResults;
	struct FindSplinePathParams;
	struct FindSplinePathResults;
	struct FindUndirectedPathsParams;
	struct FindUndirectedPathsResults;

	/// --------------------------------------------------------------------------------------------------------------- ///
	ndjob::CounterHandle BeginSinglePathFind(const FindSinglePathParams& params,
											 FindSinglePathResults* pResults,
											 NdGameObjectHandle hOwnerGo,
											 const char* sourceFile,
											 U32 sourceLine,
											 const char* sourceFunc,
											 ndjob::Priority priority = ndjob::Priority::kGameFrameNormal,
											 bool waitForPatchJob = false);

	/// --------------------------------------------------------------------------------------------------------------- ///
	ndjob::CounterHandle BeginSplinePathFind(const FindSplinePathParams& params,
											 FindSplinePathResults* pResults,
											 NdGameObjectHandle hOwnerGo,
											 const char* sourceFile,
											 U32 sourceLine,
											 const char* sourceFunc,
											 ndjob::Priority priority = ndjob::Priority::kGameFrameNormal,
											 bool waitForPatchJob = false);

	/// --------------------------------------------------------------------------------------------------------------- ///
	ndjob::CounterHandle BeginUndirectedPathFind(const FindUndirectedPathsParams& params,
												 FindUndirectedPathsResults* pResults,
												 NdGameObjectHandle hOwnerGo,
												 const char* sourceFile,
												 U32 sourceLine,
												 const char* sourceFunc,
												 ndjob::Priority priority = ndjob::Priority::kGameFrameNormal);

	/// --------------------------------------------------------------------------------------------------------------- ///
	ndjob::CounterHandle BeginFindDistanceGoal(const FindSinglePathParams& params,
											   FindSinglePathResults* pResults,
											   NdGameObjectHandle hOwnerGo,
											   const char* sourceFile,
											   U32 sourceLine,
											   const char* sourceFunc,
											   ndjob::Priority priority = ndjob::Priority::kGameFrameNormal);

	/// --------------------------------------------------------------------------------------------------------------- ///
	inline void CollectJobResults(ndjob::CounterHandle pCounter)
	{
		if (pCounter)
		{
			ndjob::WaitForCounterAndFree(pCounter);
		}
	}
} // namespace Nav
