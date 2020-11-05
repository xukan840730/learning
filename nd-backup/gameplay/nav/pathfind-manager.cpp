/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/pathfind-manager.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/nd-frame-state.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/nav/nav-handle.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-node-table.h"
#include "gamelib/gameplay/nav/nav-path-build.h"
#include "gamelib/gameplay/nav/nav-path-find.h"
#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
PathfindManager PathfindManager::s_singleton;
U16 PathfindManager::m_mgrId = 0;

/// --------------------------------------------------------------------------------------------------------------- ///
void PathfindManager::Init(U32 maxStaticPaths, U32 maxUndirectedPaths)
{
	if (FALSE_IN_FINAL_BUILD(true))
	{
		// make sure indices will not overflow
		Request dummyRequest(-1);
		const U32 maxCount = Max(maxStaticPaths, maxUndirectedPaths) + 1;
		AI_ASSERT(maxCount <= (1 << (sizeof(dummyRequest.m_paramsIdx) * 8)));
		AI_ASSERT(maxCount <= (1 << ((sizeof(dummyRequest.m_bufferedIndices[0]) * 8) - 1)));
		AI_ASSERT(maxCount <= (1 << (sizeof(dummyRequest.m_currentResultSlot) * 8)));
	}

	const size_t alignedStaticParamsSize		= AlignSize(sizeof(Nav::FindSinglePathParams), kAlign16);
	const size_t alignedStaticResultsSize		= AlignSize(sizeof(Nav::FindSinglePathResults), kAlign16);
	const size_t alignedUndirectedParamsSize	= AlignSize(sizeof(Nav::FindUndirectedPathsParams), kAlign16);
	const size_t alignedUndirectedResultsSize	= AlignSize(sizeof(Nav::FindUndirectedPathsResults), kAlign16);

	// TODO find a proper home for all this memory

	// NB: We add an extra entry to each results heap, to support double buffering the results

	m_staticParamsHeap.Init(maxStaticPaths,			   alignedStaticParamsSize,			kAllocPathfindManager, kAlign16, FILE_LINE_FUNC);
	m_staticResultsHeap.Init(maxStaticPaths+1,		   alignedStaticResultsSize,		kAllocPathfindManager, kAlign16, FILE_LINE_FUNC);
	m_undirectedParamsHeap.Init(maxUndirectedPaths,	   alignedUndirectedParamsSize,		kAllocPathfindManager, kAlign16, FILE_LINE_FUNC);
	m_undirectedResultsHeap.Init(maxUndirectedPaths+1, alignedUndirectedResultsSize,	kAllocPathfindManager, kAlign16, FILE_LINE_FUNC);

	const U32 maxRequests = maxStaticPaths + maxUndirectedPaths;
	m_requestsHeap.Init(maxRequests, kAllocPathfindManager, kAlign16, FILE_LINE_FUNC);

	MsgOut("ALLOCATION_PATHFIND_MANAGER: %llu B free\n", Memory::GetAllocator(kAllocPathfindManager)->GetFreeSize());
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(PathfindManagerUpdate)
{
	PathfindManager::Get().Update();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*static*/ ndjob::CounterHandle PathfindManager::KickUpdateJob()
{
	ndjob::CounterHandle pCounter = nullptr;

	ndjob::JobDecl pathfindMgrUpdateJobDecl(PathfindManagerUpdate, 0);
	pathfindMgrUpdateJobDecl.m_flags = ndjob::kRequireLargeStack;
	ndjob::RunJobs(&pathfindMgrUpdateJobDecl, 1, &pCounter, FILE_LINE_FUNC, ndjob::Priority::kGameFrameBelowNormal);

	return pCounter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*static*/ TimeFrame PathfindManager::GetCurTime()
{
	const Clock* const pClock = EngineComponents::GetNdFrameState()->GetClock(kRealClock);
	return pClock->GetCurTime();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathfindManager::Update()
{
	PROFILE(AI, PathfindManager_Update);

#ifdef JBELLOMY
	if (g_ndConfig.m_pDMenuMgr->IsProgPaused())
		return;
#endif

	m_lock.AcquireWriteLock(FILE_LINE_FUNC);

	// process best non-cache request
	CONST_EXPR F32 kMaxUpdateRate = 0.25f;

	CONST_EXPR F32 kMaxUpdateRateHighPri = 0.125f;
	CONST_EXPR F32 kMaxUpdateRateLowPri = 0.75f;

	CONST_EXPR F32 kFrequencyBoostHighPri = 2.0f;
	CONST_EXPR F32 kFrequencyBoostLowPri = 0.30f;

	F32 bestScore = -NDI_FLT_MAX;
	Request* pBestRequest = nullptr;
	for (Request& request : m_requestsHeap)
	{
		if (request.m_type == kTypeCacheUndirected)
			continue;

		const bool ongoing = request.m_flags.IsBitSet(kRequestOngoing);
		const bool highPriority = request.m_flags.IsBitSet(kRequestHighPriority);
		const bool lowPriority = request.m_flags.IsBitSet(kRequestLowPriority);

		const float updateRate = highPriority ? kMaxUpdateRateHighPri : lowPriority ? kMaxUpdateRateLowPri : kMaxUpdateRate;

		if (ongoing)
		{
			if (request.m_updateTime != TimeFrameNegInfinity() && (GetCurTime() - request.m_updateTime) < Seconds(updateRate))
				continue;
		}
		else
		{
			if (request.m_updateTime != TimeFrameNegInfinity())
				continue;
		}

		const float frequencyBoost = highPriority ? kFrequencyBoostHighPri : lowPriority ? kFrequencyBoostLowPri : 1.0f;

		const float timeSinceService = ToSeconds(GetCurTime() - request.m_updateTime);
		const float score = frequencyBoost * timeSinceService;

		if (score <= bestScore)
			continue;

		bestScore    = score;
		pBestRequest = &request;
	}

	if (pBestRequest)
	{
		ProcessRequest(pBestRequest);
	}

	// in case ProcessRequest messed with it
	AI_ASSERT(m_lock.IsLockedForWrite());

	// process pending cache requests
	for (Request& request : m_requestsHeap)
	{
		if (request.m_type != kTypeCacheUndirected)
			continue;

		if (request.m_updateTime != TimeFrameNegInfinity())
			continue;

		if (!request.m_hGo.HandleValid())
			continue;

		ProcessCacheRequest(&request);
	}

	// free requests marked for removal
	for (Request& request : m_requestsHeap)
	{
		request.m_lock.AcquireWriteLock(FILE_LINE_FUNC);

		if (!request.m_flags.IsBitSet(kRequestPendingDeletion))
		{
			request.m_lock.ReleaseWriteLock(FILE_LINE_FUNC);
			continue;
		}

		DeleteRequestUnsafe(request);
	}

	m_lock.ReleaseWriteLock(FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathfindManager::ProcessRequest(Request* pRequest)
{
	PROFILE(AI, PathfindManager_ProcessRequest);

	AI_ASSERT(m_lock.IsLockedForWrite());
	AI_ASSERT(pRequest->m_type != kTypeCacheUndirected);

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	void* pParamsCopyMemory = nullptr;

	// duplicate the params
	{
		AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);
		const FixedSizeHeap* pParamsHeap = GetParamsHeap(pRequest->m_type);
		const U32 paramsSize	  = GetParamsSize(pRequest->m_type);
		const void* pParamsMemory = pParamsHeap->GetElement(pRequest->m_paramsIdx);

		pParamsCopyMemory = NDI_NEW U8[paramsSize];
		memcpy(pParamsCopyMemory, pParamsMemory, paramsSize);
	}

	FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequest->m_type);
	void* pResultsMemory		= pResultsHeap->Alloc();

	AI_ASSERTF(pResultsMemory, ("Run out of PathfindManager results"));

	if (!pResultsMemory)
	{
		return;
	}

	m_lock.ReleaseWriteLock(FILE_LINE_FUNC);

	Nav::FindPathOwner owner;
	owner.Set(pRequest->m_hGo, FILE_LINE_FUNC);

	switch (pRequest->m_type)
	{
	case kTypeStatic:
		{
			const Nav::FindSinglePathParams* pParams = static_cast<const Nav::FindSinglePathParams*>(pParamsCopyMemory);
			// newed so we construct, so node data is inited
			Nav::FindSinglePathResults* pResults = NDI_NEW(pResultsMemory) Nav::FindSinglePathResults;

			Nav::FindSinglePath(owner, *pParams, pResults);
		}
		break;

	case kTypeDistance:
		{
			const Nav::FindSinglePathParams* pParams = static_cast<const Nav::FindSinglePathParams*>(pParamsCopyMemory);
			// newed so we construct, so node data is inited
			Nav::FindSinglePathResults* pResults = NDI_NEW(pResultsMemory) Nav::FindSinglePathResults;
			Nav::FindDistanceGoal(owner, *pParams, pResults);
		}
		break;

	case kTypeUndirected:
		{
			const Nav::FindUndirectedPathsParams* pParams = static_cast<const Nav::FindUndirectedPathsParams*>(pParamsCopyMemory);
			// newed so we construct, so node data is inited
			Nav::FindUndirectedPathsResults* pResults = NDI_NEW(pResultsMemory) Nav::FindUndirectedPathsResults;
			Nav::FindUndirectedPaths(owner, *pParams, pResults);
		}
		break;
	}

	m_lock.AcquireWriteLock(FILE_LINE_FUNC);

	{
		AtomicLockJanitorWrite kk(&pRequest->m_lock, FILE_LINE_FUNC);
		const I16 resultsIndex = pResultsHeap->GetBlockIndex(pResultsMemory);

		// Set the result index to the unused result slot
		const U8 unusedSlot = (pRequest->m_currentResultSlot + 1) % 2;

		AI_ASSERT(pRequest->m_bufferedIndices[unusedSlot] == -1);
		pRequest->m_bufferedIndices[unusedSlot] = resultsIndex;

		pRequest->m_updateTime = GetCurTime();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathfindManager::ProcessCacheRequest(Request* pRequest)
{
	PROFILE(AI, PathfindManager_ProcessCacheRequest);

	AI_ASSERT(m_lock.IsLockedForWrite());
	AI_ASSERT(pRequest->m_type == kTypeCacheUndirected);

	const Request* pRequestToCache = GetRequestUnsafe(pRequest->m_hRequestToCache, true);

	// request to cache has been removed.
	// this cache request should be removed soon as well.
	if (!pRequestToCache)
	{
		pRequest->m_updateTime = GetCurTime(); // don't process anymore
		return;
	}

	AI_ASSERT(pRequestToCache->m_type == kTypeUndirected || pRequestToCache->m_type == kTypeCacheUndirected);

	AtomicLockJanitorRead readLock(&pRequestToCache->m_lock, FILE_LINE_FUNC);
	AtomicLockJanitorWrite writeLock(&pRequest->m_lock, FILE_LINE_FUNC);

	const I16 resultsToCacheIndex = pRequestToCache->m_bufferedIndices[pRequestToCache->m_currentResultSlot];
	if (resultsToCacheIndex == -1)
	{
		// results to cache are not ready yet, queue request
		pRequest->m_updateTime = TimeFrameNegInfinity();
		return;
	}

	FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequestToCache->m_type);
	void* pResultsMem = pResultsHeap->Alloc();

	AI_ASSERTF(pResultsMem, ("Run out of PathfindManager results"));

	if (!pResultsMem)
	{
		return;
	}

	void* pResultsToCacheMem = pResultsHeap->GetElement(resultsToCacheIndex);
	const U32 resultsSize	 = pResultsHeap->GetAlignedElementSize();

	switch (pRequestToCache->m_type)
	{
	case kTypeStatic:
	case kTypeDistance:
		AI_HALTF(("Unhandled pathfind cache type"));
		break;
	case kTypeUndirected:
	case kTypeCacheUndirected:
		{
			const ptrdiff_t deltaMem = ((uintptr_t)pResultsMem) - ((uintptr_t)pResultsToCacheMem);
			const uintptr_t lowerMem = ((uintptr_t)pResultsToCacheMem);
			const uintptr_t upperMem = lowerMem + resultsSize;

#ifdef HEADLESS_BUILD
			NDI_NEW(pResultsMem) Nav::FindUndirectedPathsResults(*(Nav::FindUndirectedPathsResults*)pResultsToCacheMem);
#else
			// We need to do it this way around, so we aren't fucking with the now
			// buffered and supposedly immutable results from the request we are caching
			memcpy(pResultsMem, pResultsToCacheMem, resultsSize);
			Nav::FindUndirectedPathsResults* pResults = static_cast<Nav::FindUndirectedPathsResults*>(pResultsMem);
			pResults->TrivialRelocate(deltaMem, lowerMem, upperMem);
#endif
		}
		break;
	}

	const I16 resultsIndex = pResultsHeap->GetBlockIndex(pResultsMem);

	AI_ASSERT(pRequest->m_bufferedIndices[0] == -1);
	pRequest->m_bufferedIndices[0] = resultsIndex;

	pRequest->m_updateTime = GetCurTime();
}

/// --------------------------------------------------------------------------------------------------------------- ///k
void PathfindManager::FlipRequestDoubleBuffers()
{
	PROFILE(AI, PathfindManager_FlipRequestDoubleBuffers);
	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	for (Request& request : m_requestsHeap)
	{
		AtomicLockJanitorWrite kk(&request.m_lock, FILE_LINE_FUNC);

		if (request.m_type == kTypeCacheUndirected)
			continue;

		if (request.m_flags.IsBitSet(kRequestPendingDeletion))
			continue;

		FixedSizeHeap* pResultsHeap = GetResultsHeap(request.m_type);

		const U8 currentSlot = request.m_currentResultSlot;
		const U8 prevSlot	 = (request.m_currentResultSlot + 1) % 2;

		// Is there is new data to be flipped.
		if (request.m_bufferedIndices[prevSlot] != -1)
		{
			// Free the old data
			if (request.m_bufferedIndices[currentSlot] != -1)
			{
				pResultsHeap->FreeIndex(request.m_bufferedIndices[currentSlot]);
			}

			request.m_bufferedIndices[currentSlot] = -1;

			// Flip
			request.m_currentResultSlot = prevSlot;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathfindManager::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (g_navCharOptions.m_displayPathfindManager)
	{
		AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

		MsgCon("Pathfind Manager:\n");

		for (const Request& request : m_requestsHeap)
		{
			AtomicLockJanitorRead kk(&request.m_lock, FILE_LINE_FUNC);

			if (!g_navCharOptions.m_displayCachePathfindRequests && request.m_type == kTypeCacheUndirected)
				continue;

			if (request.m_flags.IsBitSet(kRequestPendingDeletion))
				continue;

			const NdGameObject* pOwnerGo = request.m_hGo.ToProcess();
			StringBuilder<64> timeSince;

			if (request.m_updateTime == TimeFrameNegInfinity())
			{
				timeSince.format("never");
			}
			else
			{
				const F32 timeSinceSec = (GetCurTime() - request.m_updateTime).ToSeconds();
				if (timeSinceSec > 99.99f)
					timeSince.format("  MAX");
				else
					timeSince.format("%5.2f", timeSinceSec);
			}

			const char* pTypeStr = "";
			switch (request.m_type)
			{
			case kTypeDistance:
				pTypeStr = "distance";
				break;
			case kTypeStatic:
				pTypeStr = "static";
				break;
			case kTypeUndirected:
				pTypeStr = "undirected";
				break;
			case kTypeCacheUndirected:
				pTypeStr = "cache";
				break;
			}

			const Request* pRequestToCache = GetRequestUnsafe(request.m_hRequestToCache);

			StringBuilder<32>
				sbDetails = request.m_type == kTypeCacheUndirected
								? StringBuilder<32>("CACHED REQUEST %d",
													pRequestToCache ? m_requestsHeap.GetBlockIndex(pRequestToCache) : -1)
								: StringBuilder<32>("%s %s %s",
									request.m_flags.IsBitSet(kRequestOngoing) ? "ONGOING" : "",
									request.m_flags.IsBitSet(kRequestHighPriority) ? "HIGHPRI" : "",
									request.m_flags.IsBitSet(kRequestLowPriority) ? "LOWPRI" : "");

			const char* pDetailsStr = sbDetails.c_str();
			const U16 requestIndex	= m_requestsHeap.GetBlockIndex(&request);

			MsgCon("%2d (%10s): %10s (%24s) %s %s\n",
				   requestIndex,
				   pTypeStr,
				   DevKitOnly_StringIdToString(request.m_nameId),
				   pOwnerGo ? pOwnerGo->GetName() : "UNKNOWN",
				   timeSince.c_str(),
				   pDetailsStr);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathfindManager::DebugDrawRequest(const PathfindRequestHandle& hRequest, Color color0, Color color1) const
{
	STRIP_IN_FINAL_BUILD;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return;

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	switch (pRequest->m_type)
	{
	case kTypeStatic:
	case kTypeDistance:
		DebugDrawSinglePathfind(hRequest, color0, color1);
		break;

	case kTypeUndirected:
	case kTypeCacheUndirected:
		DebugDrawUndirectedPathfind(hRequest, color0, color1);
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathfindManager::DebugDrawSinglePathfind(const PathfindRequestHandle& hRequest, Color color0, Color color1) const
{
	const Nav::FindSinglePathParams* pParams   = nullptr;
	const Nav::FindSinglePathResults* pResults = nullptr;

	const bool paramsSuccess  = GetParams(hRequest, &pParams);
	const bool resultsSuccess = GetResults(hRequest, &pResults);

	if (paramsSuccess && resultsSuccess && pResults->m_buildResults.m_goalFound)
	{
		PathWaypointsEx::ColorScheme colors;
		colors.m_groundLeg0 = colors.m_apLeg0 = colors.m_ledgeJump0 = colors.m_ledgeShimmy0 = color0;
		colors.m_groundLeg1 = colors.m_apLeg1 = colors.m_ledgeJump1 = colors.m_ledgeShimmy1 = color1;

		const Locator& parentSpace = pParams->m_context.m_parentSpace;
		pResults->m_buildResults.m_pathWaypointsPs.DebugDraw(parentSpace, false, 0.f, colors);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathfindManager::DebugDrawUndirectedPathfind(const PathfindRequestHandle& hRequest, Color color0, Color color1) const
{
	const Nav::FindUndirectedPathsResults* pResults = nullptr;
	const bool success = GetResults(hRequest, &pResults);

	if (success)
	{
		pResults->m_searchData.DebugDraw();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const FixedSizeHeap* PathfindManager::GetParamsHeap(PathfindType type) const
{
	PathfindManager* pMutableThis = const_cast<PathfindManager*>(this);
	return pMutableThis->GetParamsHeap(type);
}

/// --------------------------------------------------------------------------------------------------------------- ///
FixedSizeHeap* PathfindManager::GetParamsHeap(PathfindType type)
{
	switch (type)
	{
	case kTypeStatic:
	case kTypeDistance:
		return &m_staticParamsHeap;
	case kTypeUndirected:
		return &m_undirectedParamsHeap;
	case kTypeCacheUndirected:
		return nullptr;
	}
	AI_HALTF(("Unhandled pathfind type"));
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const FixedSizeHeap* PathfindManager::GetResultsHeap(PathfindType type) const
{
	PathfindManager* pMutableThis = const_cast<PathfindManager*>(this);
	return pMutableThis->GetResultsHeap(type);
}

/// --------------------------------------------------------------------------------------------------------------- ///
FixedSizeHeap* PathfindManager::GetResultsHeap(PathfindType type)
{
	switch (type)
	{
	case kTypeStatic:
	case kTypeDistance:
		return &m_staticResultsHeap;
	case kTypeUndirected:
	case kTypeCacheUndirected:
		return &m_undirectedResultsHeap;
	}
	AI_HALTF(("Unhandled pathfind type"));
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 PathfindManager::GetParamsSize(PathfindType type) const
{
	switch (type)
	{
	case kTypeStatic:
	case kTypeDistance:
		return sizeof(Nav::FindSinglePathParams);
	case kTypeUndirected:
		return sizeof(Nav::FindUndirectedPathsParams);
	}
	AI_HALTF(("Unhandled pathfind type"));
	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 PathfindManager::GetResultsSize(PathfindType type) const
{
	switch (type)
	{
	case kTypeStatic:
	case kTypeDistance:
		return sizeof(Nav::FindSinglePathResults);
	case kTypeUndirected:
	case kTypeCacheUndirected:
		return sizeof(Nav::FindUndirectedPathsResults);
	}
	AI_HALTF(("Unhandled pathfind type"));
	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::GetResults(PathfindRequestHandle hRequest, const Nav::FindSinglePathResults** pResultsOut) const
{
	PROFILE(AI, PathfindManager_CopyStatic);

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	if (!hRequest.IsValid())
		return false;

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return false;

	const I16 resultsIndex = pRequest->m_bufferedIndices[pRequest->m_currentResultSlot];

	if (resultsIndex == -1)
		return false;

	switch (pRequest->m_type)
	{
	case kTypeStatic:
	case kTypeDistance:
		break;
	default:
		return false;
	}

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);
	const U32 resultsSize = GetResultsSize(pRequest->m_type);
	const FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequest->m_type);
	const Nav::FindSinglePathResults* pResults = static_cast<const Nav::FindSinglePathResults*>(pResultsHeap->GetElement(resultsIndex));

	*pResultsOut = pResults;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::GetResults(PathfindRequestHandle hRequest, const Nav::FindUndirectedPathsResults** pResultsOut) const
{
	PROFILE(AI, PathfindManager_CopyUndirected);

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	if (!hRequest.IsValid())
		return false;

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return false;

	const I8 resultsIndex = pRequest->m_bufferedIndices[pRequest->m_currentResultSlot];

	if (resultsIndex == -1)
		return false;

	switch (pRequest->m_type)
	{
	case kTypeUndirected:
	case kTypeCacheUndirected:
		break;
	default:
		return false;
	}

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);
	const U32 resultsSize = GetResultsSize(pRequest->m_type);
	const FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequest->m_type);
	const Nav::FindUndirectedPathsResults* pResults = static_cast<const Nav::FindUndirectedPathsResults*>(pResultsHeap->GetElement(resultsIndex));

	*pResultsOut = pResults;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::GetParams(PathfindRequestHandle hRequest, const Nav::FindSinglePathParams** pParamsOut) const
{
	PROFILE(AI, PathfindManager_GetParamsSingle);

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	if (!hRequest.IsValid())
		return false;

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return false;

	const I8 paramsIndex = pRequest->m_paramsIdx;

	if (paramsIndex == -1)
		return false;

	switch (pRequest->m_type)
	{
	case kTypeStatic:
	case kTypeDistance:
		break;
	default:
		return false;
	}

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	const FixedSizeHeap* pParamsHeap		 = GetParamsHeap(pRequest->m_type);
	const Nav::FindSinglePathParams* pParams = static_cast<const Nav::FindSinglePathParams*>(pParamsHeap->GetElement(paramsIndex));

	*pParamsOut = pParams;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::GetParams(PathfindRequestHandle hRequest, const Nav::FindUndirectedPathsParams** pParamsOut) const
{
	PROFILE(AI, PathfindManager_GetParamsUnd);

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	if (!hRequest.IsValid())
		return false;

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return false;

	const I8 paramsIndex = pRequest->m_paramsIdx;

	if (paramsIndex == -1)
		return false;

	switch (pRequest->m_type)
	{
	case kTypeUndirected:
	case kTypeCacheUndirected:
		break;
	default:
		return false;
	}

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	const FixedSizeHeap* pParamsHeap = GetParamsHeap(pRequest->m_type);
	const Nav::FindUndirectedPathsParams* pParams = static_cast<const Nav::FindUndirectedPathsParams*>(pParamsHeap->GetElement(paramsIndex));

	*pParamsOut = pParams;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::BuildPath(const PathfindRequestHandle& hRequest,
								const Nav::BuildPathParams& buildParams,
								const NavLocation& navLocation,
								Nav::BuildPathResults* pBuildResults) const
{
	PROFILE(AI, PathfindManager_BuildPath);

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (!navLocation.IsValid())
		return false;

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	if (!hRequest.IsValid())
		return false;

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return false;

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	switch (pRequest->m_type)
	{
	case kTypeUndirected:
	case kTypeCacheUndirected:
		break;
	default:
		return false;
	}

	const I16 resultsIndex = pRequest->m_bufferedIndices[pRequest->m_currentResultSlot];

	if (resultsIndex == -1)
		return false;

	const FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequest->m_type);
	const Nav::FindUndirectedPathsResults* pPathResults = static_cast<const Nav::FindUndirectedPathsResults*>(pResultsHeap->GetElement(resultsIndex));

#ifdef HEADLESS_BUILD

	NavKeyDataPair goalKeyData;
	const bool locVisited = pPathResults->m_searchData.IsLocationVisited(navLocation, &goalKeyData);

	if (locVisited)
		Nav::BuildPath(pPathResults->m_searchData, buildParams, goalKeyData.m_key, navLocation.GetPosWs(), pBuildResults);

#else

	const NavManagerId navManagerId = navLocation.GetNavManagerId();
	const NavPathNode::NodeId nodeId = navLocation.GetPathNodeId();
	const bool locVisited = pPathResults->m_searchData.IsNodeVisited(navManagerId, nodeId);

	if (locVisited)
	{
		Nav::BuildPath(pPathResults->m_searchData,
					   buildParams,
					   NavNodeKey(nodeId, 0),
					   navLocation.GetPosWs(),
					   pBuildResults);
	}

#endif

	const bool goalFound = pBuildResults->m_pathWaypointsPs.IsValid();

	pBuildResults->m_goalFound = goalFound;

	return goalFound;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::CanPathTo(const PathfindRequestHandle& hRequest,
								const NavHandle& hNav,
#ifdef HEADLESS_BUILD
								NavKeyDataPair* pNodeDataOut /*=nullptr*/)
#else
								TrivialHashNavNodeData* pNodeDataOut /*=nullptr*/)
#endif
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (!hNav.IsValid())
		return false;

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	if (!hRequest.IsValid())
		return false;

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return false;

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	const I16 resultsIndex = pRequest->m_bufferedIndices[pRequest->m_currentResultSlot];

	if (resultsIndex == -1)
		return false;

	AI_ASSERT(pRequest->m_type == kTypeUndirected || pRequest->m_type == kTypeCacheUndirected); // can't do this on static paths!

	const FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequest->m_type);
	const Nav::FindUndirectedPathsResults* pPathResults = static_cast<const Nav::FindUndirectedPathsResults*>(pResultsHeap->GetElement(resultsIndex));

	return pPathResults->m_searchData.IsHandleVisited(hNav, pNodeDataOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::GetApproxPathDistance(const PathfindRequestHandle& hRequest,
											const NavHandle& hNav,
											F32& pathLength)
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (!hNav.IsValid())
		return false;

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	if (!hRequest.IsValid())
		return false;

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return false;

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	AI_ASSERT(pRequest->m_type == kTypeUndirected || pRequest->m_type == kTypeCacheUndirected); // can't do this on static paths!

	const I16 resultsIndex = pRequest->m_bufferedIndices[pRequest->m_currentResultSlot];

	if (resultsIndex == -1)
		return false;

	const FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequest->m_type);
	const Nav::FindUndirectedPathsResults* pPathResults = static_cast<const Nav::FindUndirectedPathsResults*>(pResultsHeap->GetElement(resultsIndex));

#ifdef HEADLESS_BUILD
	NavKeyDataPair goalKeyData;
	if (!pPathResults->m_searchData.IsHandleVisited(hNav, &goalKeyData))
		return false;

	pathLength = goalKeyData.m_data.m_fromDist;
#else
	TrivialHashNavNodeData goalData;
	if (!pPathResults->m_searchData.IsHandleVisited(hNav, &goalData))
		return false;

	pathLength = goalData.GetFromDist();
#endif

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::GetApproxPathDistance(const PathfindRequestHandle& hRequest,
											const NavLocation& navLocation,
											F32& pathLength)
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (!navLocation.IsValid())
		return false;

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	if (!hRequest.IsValid())
		return false;

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return false;

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	AI_ASSERT(pRequest->m_type == kTypeUndirected || pRequest->m_type == kTypeCacheUndirected); // can't do this on static paths!

	const I16 resultsIndex = pRequest->m_bufferedIndices[pRequest->m_currentResultSlot];

	if (resultsIndex == -1)
		return false;

	const FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequest->m_type);
	const Nav::FindUndirectedPathsResults* pPathResults = static_cast<const Nav::FindUndirectedPathsResults*>(pResultsHeap->GetElement(resultsIndex));

#ifdef HEADLESS_BUILD
	NavKeyDataPair goalKeyData;
	if (!pPathResults->m_searchData.IsLocationVisited(navLocation, &goalKeyData))
		return false;

	const Point nodePosWs = pPathResults->m_searchData.GetParentSpace().TransformPoint(goalKeyData.m_data.m_pathNodePosPs);

	pathLength = goalKeyData.m_data.m_fromDist + Dist(navLocation.GetPosWs(), nodePosWs);
#else
	TrivialHashNavNodeData goalData;
	if (!pPathResults->m_searchData.IsHandleVisited(navLocation, &goalData))
		return false;

	const Point nodePosWs = pPathResults->m_searchData.GetParentSpace().TransformPoint(goalData.m_posPs);

	pathLength = goalData.GetFromDist() + Dist(navLocation.GetPosWs(), nodePosWs);
#endif

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::GetApproxPathDistanceSmooth(const PathfindRequestHandle& hRequest,
												  const NavHandle& hNav,
												  F32& pathLength)
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (!hNav.IsValid())
		return false;

	if (!hRequest.IsValid())
		return false;

	Nav::BuildPathResults results = GetApproxPathSmooth(hRequest, hNav);

	if (results.m_goalFound)
	{
		pathLength = results.m_length;
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::GetApproxPathDistanceSmooth(const PathfindRequestHandle& hRequest,
												  const NavLocation& navLocation,
												  F32& pathLength)
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (!navLocation.IsValid())
		return false;

	Nav::BuildPathResults results = GetApproxPathSmooth(hRequest, navLocation);

	if (results.m_goalFound)
	{
		pathLength = results.m_length;
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Nav::BuildPathResults PathfindManager::GetApproxPathSmooth(const PathfindRequestHandle& hRequest, const NavHandle& hNav)
{
	Nav::BuildPathResults results;
	results.Clear();

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return results;

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	AI_ASSERT(pRequest->m_type == kTypeUndirected || pRequest->m_type == kTypeCacheUndirected); // can't do this on static paths!

	const I16 resultsIndex = pRequest->m_bufferedIndices[pRequest->m_currentResultSlot];

	if (resultsIndex == -1)
		return results;

	const FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequest->m_type);
	const Nav::FindUndirectedPathsResults* pPathResults = static_cast<const Nav::FindUndirectedPathsResults*>(pResultsHeap->GetElement(resultsIndex));

#ifdef HEADLESS_BUILD
	NavKeyDataPair goalKeyData;
	if (!pPathResults->m_searchData.IsHandleVisited(hNav, &goalKeyData))
		return results;
#else
	const NavManagerId navManagerId = hNav.GetNavManagerId();
	const NavPathNode::NodeId nodeId = hNav.GetPathNodeId();
	if (!pPathResults->m_searchData.IsNodeVisited(navManagerId, nodeId))
		return results;
#endif

	Point navPos;

	switch (hNav.GetType())
	{
	case NavHandle::Type::kNavPoly:
	{
		if (const NavPoly* pPoly = hNav.ToNavPoly())
		{
			navPos = pPoly->GetCentroid();
		}
	}
	break;
#if ENABLE_NAV_LEDGES
	case NavHandle::Type::kNavLedge:
	{
		if (const NavLedge* pLedge = hNav.ToNavLedge())
		{
			navPos = pLedge->GetNavLedgeGraph()->GetBoundFrame().GetTranslationWs();
		}
	}
	break;
#endif // ENABLE_NAV_LEDGES

	default:
		return results;
	}

	Nav::BuildPathParams buildParams;
	buildParams.m_smoothPath = Nav::kApproxSmoothing;

#ifdef HEADLESS_BUILD
	Nav::BuildPath(pPathResults->m_searchData, buildParams, goalKeyData.m_key, navPos, &results);
#else
	Nav::BuildPath(pPathResults->m_searchData, buildParams, NavNodeKey(nodeId, 0), navPos, &results);
#endif

	results.m_goalFound = results.m_pathWaypointsPs.IsValid();

	return results;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float PathfindManager::GetFastApproxSmoothPathDistanceOnly(const PathfindRequestHandle& hRequest, const NavLocation& navLocation, bool* const pPathCrossesTap /* = nullptr */)
{
	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return NDI_FLT_MAX;

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	AI_ASSERT(pRequest->m_type == kTypeUndirected || pRequest->m_type == kTypeCacheUndirected); // can't do this on static paths!

	const I16 resultsIndex = pRequest->m_bufferedIndices[pRequest->m_currentResultSlot];

	if (resultsIndex == -1)
		return NDI_FLT_MAX;

	const FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequest->m_type);
	const Nav::FindUndirectedPathsResults* pPathResults = static_cast<const Nav::FindUndirectedPathsResults*>(pResultsHeap->GetElement(resultsIndex));

#ifdef HEADLESS_BUILD
	NavKeyDataPair goalKeyData;
	if (!pPathResults->m_searchData.IsLocationVisited(navLocation, &goalKeyData))
		return NDI_FLT_MAX;

	return Nav::GetFastApproxSmoothDistanceOnly(pPathResults->m_searchData, goalKeyData.m_key, navLocation.GetPosWs(), pPathCrossesTap);

#else
	return Nav::GetFastApproxSmoothDistanceOnly(pPathResults->m_searchData, navLocation.GetPathNodeId(), navLocation.GetPosWs(), pPathCrossesTap);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
Nav::BuildPathResults PathfindManager::GetApproxPathSmooth(const PathfindRequestHandle& hRequest, const NavLocation& navLocation)
{
	Nav::BuildPathResults results;
	results.Clear();

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return results;

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	AI_ASSERT(pRequest->m_type == kTypeUndirected || pRequest->m_type == kTypeCacheUndirected); // can't do this on static paths!

	const I16 resultsIndex = pRequest->m_bufferedIndices[pRequest->m_currentResultSlot];

	if (resultsIndex == -1)
		return results;

	const FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequest->m_type);
	const Nav::FindUndirectedPathsResults* pPathResults = static_cast<const Nav::FindUndirectedPathsResults*>(pResultsHeap->GetElement(resultsIndex));

#ifdef HEADLESS_BUILD
	NavKeyDataPair goalKeyData;
	if (!pPathResults->m_searchData.IsLocationVisited(navLocation, &goalKeyData))
		return results;
#else
	const NavManagerId navManagerId = navLocation.GetNavManagerId();
	const NavPathNode::NodeId nodeId = navLocation.GetPathNodeId();
	if (!pPathResults->m_searchData.IsNodeVisited(navManagerId, nodeId))
		return results;
#endif


	Nav::BuildPathParams buildParams;

	buildParams.m_smoothPath = Nav::kApproxSmoothing;

#ifdef HEADLESS_BUILD
	Nav::BuildPath(pPathResults->m_searchData, buildParams, goalKeyData.m_key, navLocation.GetPosWs(), &results);
#else
	Nav::BuildPath(pPathResults->m_searchData, buildParams, NavNodeKey(nodeId, 0), navLocation.GetPosWs(), &results);
#endif

	results.m_goalFound = results.m_pathWaypointsPs.IsValid();

	return results;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::GetUndirectedPathFindOrigin(const PathfindRequestHandle& hRequest, Point& posWs) const
{
	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	const Request* pRequest = GetRequestUnsafe(hRequest);
	if (!pRequest)
		return false;

	AI_ASSERT(pRequest->m_type == kTypeUndirected); // can't do this on static paths!

	const Nav::FindUndirectedPathsParams* pParams;
	if (!GetParams(hRequest, &pParams))
		return false;

	posWs = pParams->m_context.m_parentSpace.TransformLocator(pParams->m_context.m_ownerLocPs).GetPosition();
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathfindManager::ApproxPathUsesTap(const PathfindRequestHandle& hRequest,
										const NavHandle& hNav,
										bool& usesTap)
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (!hNav.IsValid())
		return false;

	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	if (!hRequest.IsValid())
		return false;

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return false;

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	AI_ASSERT(pRequest->m_type == kTypeUndirected || pRequest->m_type == kTypeCacheUndirected); // can't do this on static paths!

	const I16 resultsIndex = pRequest->m_bufferedIndices[pRequest->m_currentResultSlot];

	if (resultsIndex == -1)
		return false;

	const FixedSizeHeap* pResultsHeap = GetResultsHeap(pRequest->m_type);
	const Nav::FindUndirectedPathsResults* pPathResults
		= static_cast<const Nav::FindUndirectedPathsResults*>(pResultsHeap->GetElement(resultsIndex));

#ifdef HEADLESS_BUILD
	NavKeyDataPair keyData;
	if (!pPathResults->m_searchData.IsHandleVisited(hNav, &keyData))
		return false;

	usesTap = false;

	const NavNodeTable& nodeTable = pPathResults->m_searchData.m_visitedNodes;
	const NavPathNodeProxy* pNode = &keyData.m_data.m_pathNode;
	const NavNodeKey* pParentNode = &keyData.m_data.m_parentNode;
	while (pNode)
	{
		if (pNode->IsActionPackNode())
		{
			usesTap = true;
			break;
		}

		const NavNodeTable::ConstIterator iter = nodeTable.Find(*pParentNode);
		pNode = (iter != nodeTable.End()) ? &iter->m_data.m_pathNode : nullptr;
		pParentNode = (iter != nodeTable.End()) ? &iter->m_data.m_parentNode : nullptr;
	}

	return true;
#else
	const NavManagerId navManagerId = hNav.GetNavManagerId();
	NavPathNode::NodeId nodeId = hNav.GetPathNodeId();
	if (!pPathResults->m_searchData.IsNodeVisited(navManagerId, nodeId))
		return false;

	usesTap = false;

	const TrivialHashNavNodeData* pNodeTable = pPathResults->m_searchData.m_trivialHashVisitedNodes;
	while (nodeId < NavPathNodeMgr::kMaxNodeCount)
	{
		const TrivialHashNavNodeData& d = pNodeTable[nodeId];
		if (!d.IsValid())
			break;

		if (d.m_nodeType == NavPathNode::kNodeTypeActionPackEnter || d.m_nodeType == NavPathNode::kNodeTypeActionPackExit)
		{
			usesTap = true;
			break;
		}

		nodeId = d.m_iParent;
	}

	return true;
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
TimeFrame PathfindManager::GetRequestUpdateTime(const PathfindRequestHandle& hRequest) const
{
	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	const Request* pRequest = GetRequestUnsafe(hRequest);
	AI_ASSERT(pRequest);

	AtomicLockJanitorRead kk(&pRequest->m_lock, FILE_LINE_FUNC);

	return pRequest->m_updateTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const PathfindManager::Request* PathfindManager::GetRequestUnsafe(const PathfindRequestHandle& hRequest,
																  bool allowDeleted /*=false*/) const
{
	PathfindManager* pMutableThis = const_cast<PathfindManager*>(this);
	return pMutableThis->GetRequestUnsafe(hRequest, allowDeleted);
}

/// --------------------------------------------------------------------------------------------------------------- ///
PathfindManager::Request* PathfindManager::GetRequestUnsafe(const PathfindRequestHandle& hRequest,
															bool allowDeleted /*=false*/)
{
	AI_ASSERT(m_lock.IsLocked());

	if (!hRequest.IsValid())
		return nullptr;

	const I16 requestIndex = hRequest.m_index;

	if (!m_requestsHeap.IsElementUsed(requestIndex))
		return nullptr;

	Request* pRequest = static_cast<Request*>(m_requestsHeap.GetElement(requestIndex));
	if (pRequest->m_flags.IsBitSet(kRequestPendingDeletion) && !allowDeleted)
		return nullptr;

	if (pRequest->m_mgrId != hRequest.m_mgrId)
		return nullptr;

	return pRequest;
}

/// --------------------------------------------------------------------------------------------------------------- ///
PathfindRequestHandle PathfindManager::AllocRequest(PathfindType type,
													StringId64 nameId,
													NdGameObjectHandle hOwner,
													const void* pParamsIn,
													bool ongoing,
													bool highPriority,
													bool lowPriority)
{
	AI_ASSERT(!highPriority || !lowPriority);

	FixedSizeHeap* pParamsHeap	= GetParamsHeap(type);
	FixedSizeHeap* pResultsHeap = GetResultsHeap(type);

	Request* pRequest	= nullptr;
	void* pParamsMemory = nullptr;

	{
		AtomicLockJanitorWrite jj(&m_lock, FILE_LINE_FUNC);
		bool failed = false;

		MsgAi("PathfindManager::AllocRequest: Allocating request. Heap(%d/%d)\n",
			  m_requestsHeap.GetNumElementsUsed(),
			  m_requestsHeap.GetNumElements());

		pRequest = m_requestsHeap.Alloc();

		if (!pRequest)
		{
			failed = true;

			MsgAi("PathfindManager::AllocRequest: Run out of PathfindManager requests\n");
			AI_HALTF(("Run out of PathfindManager requests"));
		}

		MsgAi("PathfindManager::AllocRequest: Allocating params. Heap(%d/%d)\n",
			  pParamsHeap->GetNumElementsUsed(),
			  pParamsHeap->GetNumElements());

		pParamsMemory = pParamsHeap->Alloc();

		if (!pParamsMemory)
		{
			failed = true;

			MsgAi("PathfindManager::AllocRequest: Run out of PathfindManager requests\n");
			AI_HALTF(("Run out of PathfindManager params"));
		}

		if (failed)
		{
			if (pRequest)
				m_requestsHeap.Free(pRequest);

			if (pParamsMemory)
				pParamsHeap->Free(pParamsMemory);

			return PathfindRequestHandle();
		}

		const I16 requestIndex = m_requestsHeap.GetBlockIndex(pRequest);
		const I32 paramsIndex  = pParamsHeap->GetBlockIndex(pParamsMemory);
		const U16 mgrId		   = m_mgrId++;

		// placement new to ensure everything is constructed correctly
		Request* pNewRequest = NDI_NEW(pRequest) Request(mgrId);

		AtomicLockJanitorWrite kk(&pNewRequest->m_lock, FILE_LINE_FUNC);

		pNewRequest->m_paramsIdx = paramsIndex;
		pNewRequest->m_type		 = type;
		pNewRequest->m_nameId	 = nameId;
		pNewRequest->m_hGo		 = hOwner;
		pNewRequest->m_flags.AssignBit(kRequestOngoing, ongoing);
		pNewRequest->m_flags.AssignBit(kRequestHighPriority, highPriority);
		pNewRequest->m_flags.AssignBit(kRequestLowPriority, lowPriority);

		const U32 paramsSize = GetParamsSize(type);

		memcpy(pParamsMemory, pParamsIn, paramsSize);

		return PathfindRequestHandle(requestIndex, mgrId);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
PathfindRequestHandle PathfindManager::AddStaticRequest(StringId64 nameId,
														NdGameObjectHandle hOwner,
														const Nav::FindSinglePathParams& params,
														bool ongoing /*=false*/,
														bool highPriority /*=false*/)
{
	AI_ASSERTF(!ongoing || !highPriority, ("Who do you think you are? Ongoing AND High Priority?"));
	return AllocRequest(kTypeStatic, nameId, hOwner, &params, ongoing, highPriority, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
PathfindRequestHandle PathfindManager::AddDistanceRequest(StringId64 nameId,
														  NdGameObjectHandle hOwner,
														  const Nav::FindSinglePathParams& params,
														  bool ongoing /*=false*/)
{
	AI_ASSERT(params.m_distanceGoal > 0.f);
	CONST_EXPR bool kHighPriority = false;
	CONST_EXPR bool kLowPriority = false;
	return AllocRequest(kTypeDistance, nameId, hOwner, &params, ongoing, kHighPriority, kLowPriority);
}

/// --------------------------------------------------------------------------------------------------------------- ///
PathfindRequestHandle PathfindManager::AddUndirectedRequest(StringId64 nameId,
															NdGameObjectHandle hOwner,
															const Nav::FindUndirectedPathsParams& params,
															bool ongoing /*=false*/,
															bool highPriority /*=false*/,
															bool lowPriority /*=false*/)
{
	AI_ASSERT(!highPriority || !lowPriority);
	return AllocRequest(kTypeUndirected, nameId, hOwner, &params, ongoing, highPriority, lowPriority);
}

/// --------------------------------------------------------------------------------------------------------------- ///
PathfindRequestHandle PathfindManager::CacheRequest(StringId64 nameId, const PathfindRequestHandle& hRequestToCache)
{

	Request* pRequest = nullptr;
	const Request* pRequestToCache = nullptr;
	PathfindType cacheType = kTypeCount;

	{
		AtomicLockJanitorWrite jj(&m_lock, FILE_LINE_FUNC);
		bool failed = false;

		MsgAi("PathfindManager::CacheRequest: Allocating request. Heap(%d/%d)\n",
			  m_requestsHeap.GetNumElementsUsed(),
			  m_requestsHeap.GetNumElements());

		pRequest = m_requestsHeap.Alloc();

		if (!pRequest)
		{
			failed = true;

			MsgAi("PathfindManager::CacheRequest: Run out of PathfindManager requests\n");
			AI_HALTF(("Run out of PathfindManager requests"));
		}

		pRequestToCache = GetRequestUnsafe(hRequestToCache);

		if (!pRequestToCache)
		{
			failed = true;
			MsgAi("PathfindManager::CacheRequest: Request to cache is invalid\n");
		}
		else
		{
			switch (pRequestToCache->m_type)
			{
			case kTypeUndirected:
			case kTypeCacheUndirected:
				cacheType = kTypeCacheUndirected;
				break;
			default:
				failed = true;
				AI_HALTF(("PathfindManager::CacheRequest: Unsupported pathfind type for caching."));
				break;
			}
		}

		if (failed)
		{
			if (pRequest)
				m_requestsHeap.Free(pRequest);

			return PathfindRequestHandle();
		}

		const U16 requestIndex = m_requestsHeap.GetBlockIndex(pRequest);
		const U16 mgrId		   = m_mgrId++;

		// placement new to ensure everything is constructed correctly
		Request* pNewRequest = NDI_NEW(pRequest) Request(mgrId);

		AtomicLockJanitorWrite kk(&pNewRequest->m_lock, FILE_LINE_FUNC);

		pRequest->m_type   = cacheType;
		pRequest->m_nameId = nameId;
		pRequest->m_hGo	   = pRequestToCache->m_hGo;
		pRequest->m_hRequestToCache = hRequestToCache;

		return PathfindRequestHandle(requestIndex, mgrId);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathfindManager::RemoveRequest(const PathfindRequestHandle& hRequest)
{
	AtomicLockJanitorWrite jj(&m_lock, FILE_LINE_FUNC);

	Request* pRequest = GetRequestUnsafe(hRequest);
	AI_ASSERT(pRequest);

	AtomicLockJanitorWrite kk(&pRequest->m_lock, FILE_LINE_FUNC);
	pRequest->m_flags.SetBit(kRequestPendingDeletion);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathfindManager::DeleteRequestUnsafe(Request& request)
{
	AI_ASSERT(m_lock.IsLockedForWrite());
	AI_ASSERT(request.m_lock.IsLockedForWrite());

	FixedSizeHeap* pParamsHeap	= GetParamsHeap(request.m_type);
	FixedSizeHeap* pResultsHeap = GetResultsHeap(request.m_type);

	if (pParamsHeap)
		pParamsHeap->FreeIndex(request.m_paramsIdx);

	if (request.m_bufferedIndices[0] != -1)
		pResultsHeap->FreeIndex(request.m_bufferedIndices[0]);
	if (request.m_bufferedIndices[1] != -1)
		pResultsHeap->FreeIndex(request.m_bufferedIndices[1]);

	// need to release this here before freeing the memory
	request.m_lock.ReleaseWriteLock(FILE_LINE_FUNC);

	const U16 requestIndex = m_requestsHeap.GetBlockIndex(&request);
	m_requestsHeap.FreeIndex(requestIndex);

	MsgAi("PathfindManager::RemoveRequest: Freed request. Heap(%d/%d)\n",
		  m_requestsHeap.GetNumElementsUsed(),
		  m_requestsHeap.GetNumElements());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathfindManager::UpdateRequest(const PathfindRequestHandle& hRequest, const void* pNewParams)
{
	AtomicLockJanitorRead jj(&m_lock, FILE_LINE_FUNC);

	const Request* pRequest = GetRequestUnsafe(hRequest);

	if (!pRequest)
		return;

	AtomicLockJanitorWrite kk(&pRequest->m_lock, FILE_LINE_FUNC);

	const U32 paramsSize	   = GetParamsSize(pRequest->m_type);
	FixedSizeHeap* pParamsHeap = GetParamsHeap(pRequest->m_type);
	void* pParams = pParamsHeap->GetElement(pRequest->m_paramsIdx);

	memcpy(pParams, pNewParams, paramsSize);
}

