//--------------------------------------------------------------------------------------
// File: occlusion-queries.fx
//
// Copyright (c) Naught Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "global-funcs.fxi"
#include "atomics.fxi"

struct Srt 
{
	RWStructuredBuffer<DrawIndirectArgs> m_drawIndirectArgs;
	StructuredBuffer<OcclusionQueryResults> m_occlusionQueryResults;
	uint m_numEntries;

	// Debug parameters
	uint m_gdsOffset;
	uint m_countOcclusionQueries;
	uint m_forceKillOcclusionQueries;
};

void ProcessOcclusionQueryResults(Srt srt, uint dispatchId, bool isDebug)
{
	if (dispatchId >= srt.m_numEntries)
		return;

	// Grab our occlusion results
	OcclusionQueryResults results = srt.m_occlusionQueryResults[dispatchId];

	// Compute how many pixels passed the Z test
	// TODO: Do we need to do 64 bit operations? 
	// What are the odds of rendering > 2^32 pixels?
	unsigned long totalCount = 0;

	for (int i = 0; i < g_numDBs; ++i)
	{
		totalCount += (results.m_results[i].m_zPassCountEnd - results.m_results[i].m_zPassCountBegin);
	}

	if (isDebug && srt.m_forceKillOcclusionQueries)
	{
		totalCount = 0;
	}

	if (totalCount == 0)
	{
		srt.m_drawIndirectArgs[dispatchId].m_indexCountPerInstance = 0;

		if (isDebug && srt.m_countOcclusionQueries)
		{
			NdAtomicIncrement(srt.m_gdsOffset);
		}
	}
}

[numthreads(64,1,1)]
void CS_ProcessOcclusionQueries(uint dispatchId : S_DISPATCH_THREAD_ID, Srt *pSrt : S_SRT_DATA)
{
	ProcessOcclusionQueryResults(*pSrt, dispatchId, false);
}

[numthreads(64,1,1)]
void CS_ProcessOcclusionQueriesDebug(uint dispatchId : S_DISPATCH_THREAD_ID, Srt *pSrt : S_SRT_DATA)
{
	ProcessOcclusionQueryResults(*pSrt, dispatchId, true);
}