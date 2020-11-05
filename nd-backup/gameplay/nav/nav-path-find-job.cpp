/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-path-find-job.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-ex-data.h"
#include "gamelib/gameplay/nav/nav-path-find.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct FindSinglePathJobInput
{
	Nav::FindSinglePathParams	m_params;
	Nav::FindSinglePathResults*	m_pResults;
	Nav::FindPathOwner			m_owner;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct FindUndirectedPathsJobInput
{
	Nav::FindUndirectedPathsParams		m_params;
	Nav::FindUndirectedPathsResults*	m_pResults;
	Nav::FindPathOwner					m_owner;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct FindSplinePathJobInput
{
	Nav::FindSplinePathParams	m_params;
	Nav::FindSplinePathResults*	m_pResults;
	Nav::FindPathOwner			m_owner;
};

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(FindSinglePathEntryPoint)
{
	FindSinglePathJobInput* pInputBuffer = (FindSinglePathJobInput*)jobParam;
	Nav::FindSinglePath(pInputBuffer->m_owner, pInputBuffer->m_params, pInputBuffer->m_pResults);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndjob::CounterHandle Nav::BeginSinglePathFind(const FindSinglePathParams& params,
											  FindSinglePathResults* pResults,
											  NdGameObjectHandle hOwnerGo,
											  const char* sourceFile,
											  U32 sourceLine,
											  const char* sourceFunc,
											  ndjob::Priority priority /* = ndjob::Priority::kGameFrameNormal */,
											  bool waitForPatchJob /* = false */)
{
	AllocateJanitor jj(kAllocDoubleGameFrame, FILE_LINE_FUNC);

	NAV_ASSERT(pResults);
	NAV_ASSERTF(IsReasonable(params.m_goal.GetPosPs()),
				("Bad path find goal from %s (%s:%d)", sourceFunc, sourceFile, sourceLine));

	FindSinglePathJobInput* pInputBuffer = NDI_NEW(kAlign128) FindSinglePathJobInput;

	if (!pInputBuffer)
	{
		NAV_ASSERTF(false, ("BeginSinglePathFind(): Out of double-frame memory"));
		return nullptr;
	}

	memset(pInputBuffer, 0, sizeof(*pInputBuffer));
	pInputBuffer->m_params = params;
	pInputBuffer->m_pResults = pResults;
	pInputBuffer->m_owner.Set(hOwnerGo, sourceFile, sourceLine, sourceFunc);

	ndjob::CounterHandle pCounter = nullptr;
	ndjob::JobDecl findSinglePathJob(FindSinglePathEntryPoint, (uintptr_t)pInputBuffer);
	findSinglePathJob.m_flags = ndjob::kRequireLargeStack;

	if (waitForPatchJob)
	{
		findSinglePathJob.m_dependentCounter = g_navExData.GetWaitForPatchCounter();
	}

	ndjob::RunJobs(&findSinglePathJob, 1, &pCounter, sourceFile, sourceLine, sourceFunc, priority);

	return pCounter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(FindUndirectedPathsEntryPoint)
{
	FindUndirectedPathsJobInput* pInputBuffer = (FindUndirectedPathsJobInput*)jobParam;
	Nav::FindUndirectedPaths(pInputBuffer->m_owner, pInputBuffer->m_params, pInputBuffer->m_pResults);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndjob::CounterHandle Nav::BeginUndirectedPathFind(const FindUndirectedPathsParams& params,
												  FindUndirectedPathsResults* pResults,
												  NdGameObjectHandle hOwnerGo,
												  const char* sourceFile,
												  U32 sourceLine,
												  const char* sourceFunc,
												  ndjob::Priority priority /* = ndjob::Priority::kGameFrameNormal */)
{
	AllocateJanitor jj(kAllocDoubleGameFrame, FILE_LINE_FUNC);

	NAV_ASSERT(pResults);

	FindUndirectedPathsJobInput* pInputBuffer = NDI_NEW(kAlign128) FindUndirectedPathsJobInput;

	if (!pInputBuffer)
	{
		NAV_ASSERTF(false, ("BeginUndirectedPathFind(): Out of double-frame memory"));
		return nullptr;
	}

	memset(pInputBuffer, 0, sizeof(*pInputBuffer));
	pInputBuffer->m_params = params;
	pInputBuffer->m_pResults = pResults;
	pInputBuffer->m_owner.Set(hOwnerGo, sourceFile, sourceLine, sourceFunc);

	ndjob::CounterHandle pCounter = nullptr;
	ndjob::JobDecl findUndirectedPathsJob(FindUndirectedPathsEntryPoint, (uintptr_t)pInputBuffer);
	findUndirectedPathsJob.m_flags = ndjob::kRequireLargeStack;
	ndjob::RunJobs(&findUndirectedPathsJob, 1, &pCounter, sourceFile, sourceLine, sourceFunc, priority);

	return pCounter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(FindDistanceGoalEntryPoint)
{
	FindSinglePathJobInput* pInputBuffer = (FindSinglePathJobInput*)jobParam;
	Nav::FindDistanceGoal(pInputBuffer->m_owner, pInputBuffer->m_params, pInputBuffer->m_pResults);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndjob::CounterHandle Nav::BeginFindDistanceGoal(const FindSinglePathParams& params,
												FindSinglePathResults* pResults,
												NdGameObjectHandle hOwnerGo,
												const char* sourceFile,
												U32 sourceLine,
												const char* sourceFunc,
												ndjob::Priority priority /* = ndjob::Priority::kGameFrameNormal */)
{
	AllocateJanitor jj(kAllocDoubleGameFrame, FILE_LINE_FUNC);

	NAV_ASSERT(pResults);

	FindSinglePathJobInput* pInputBuffer = NDI_NEW(kAlign128) FindSinglePathJobInput;

	if (!pInputBuffer)
	{
		NAV_ASSERTF(false, ("BeginFindDistanceGoal(): Out of double-frame memory"));
		return nullptr;
	}

	memset(pInputBuffer, 0, sizeof(*pInputBuffer));
	pInputBuffer->m_params = params;
	pInputBuffer->m_pResults = pResults;
	pInputBuffer->m_owner.Set(hOwnerGo, sourceFile, sourceLine, sourceFunc);

	ndjob::CounterHandle pCounter = nullptr;
	ndjob::JobDecl findDistancePathJob(FindDistanceGoalEntryPoint, (uintptr_t)pInputBuffer);
	findDistancePathJob.m_flags = ndjob::kRequireLargeStack;
	ndjob::RunJobs(&findDistancePathJob, 1, &pCounter, sourceFile, sourceLine, sourceFunc, priority);

	return pCounter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(FindSplinePathEntryPoint)
{
	FindSplinePathJobInput* pInputBuffer = (FindSplinePathJobInput*)jobParam;
	Nav::FindSplinePath(pInputBuffer->m_owner, pInputBuffer->m_params, pInputBuffer->m_pResults);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndjob::CounterHandle Nav::BeginSplinePathFind(const FindSplinePathParams& params,
											  FindSplinePathResults* pResults,
											  NdGameObjectHandle hOwnerGo,
											  const char* sourceFile,
											  U32 sourceLine,
											  const char* sourceFunc,
											  ndjob::Priority priority /* = ndjob::Priority::kGameFrameNormal */,
											  bool waitForPatchJob /* = false */)
{
	AllocateJanitor jj(kAllocDoubleGameFrame, FILE_LINE_FUNC);

	NAV_ASSERT(pResults);

	FindSplinePathJobInput* pInputBuffer = NDI_NEW(kAlign128) FindSplinePathJobInput;

	if (!pInputBuffer)
	{
		NAV_ASSERTF(false, ("BeginSplinePathFind(): Out of double-frame memory"));
		return nullptr;
	}

	memset(pInputBuffer, 0, sizeof(*pInputBuffer));
	pInputBuffer->m_params = params;
	pInputBuffer->m_pResults = pResults;
	pInputBuffer->m_owner.Set(hOwnerGo, sourceFile, sourceLine, sourceFunc);

	ndjob::CounterHandle pCounter = nullptr;
	ndjob::JobDecl findSplinePathJob(FindSplinePathEntryPoint, (uintptr_t)pInputBuffer);
	findSplinePathJob.m_flags = ndjob::kRequireLargeStack;

	if (waitForPatchJob)
	{
		findSplinePathJob.m_dependentCounter = g_navExData.GetWaitForPatchCounter();
	}

	ndjob::RunJobs(&findSplinePathJob, 1, &pCounter, sourceFile, sourceLine, sourceFunc, priority);

	return pCounter;
}
