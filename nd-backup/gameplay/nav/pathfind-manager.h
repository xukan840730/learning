/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#ifndef PATHFIND_MANAGER_H
#define PATHFIND_MANAGER_H

#include "corelib/containers/typed-fixed-size-heap.h"
#include "corelib/system/read-write-atomic-lock.h"

#include "gamelib/gameplay/nav/action-pack.h"

namespace Nav
{
	struct BuildPathParams;
	struct BuildPathResults;
	struct FindSinglePathParams;
	struct FindSinglePathResults;
	struct FindUndirectedPathsParams;
	struct FindUndirectedPathsResults;
}

class NavHandle;
class NavLocation;
struct NavKeyDataPair;
struct TrivialHashNavNodeData;

/// --------------------------------------------------------------------------------------------------------------- ///
struct PathfindRequestHandle
{
	PathfindRequestHandle()
	{
		Invalidate();
	}

	PathfindRequestHandle(I16 index, U16 mgrId) :
		m_index(index),
		m_mgrId(mgrId)
	{
	}

	PathfindRequestHandle(I32 rawHandle) :
		m_rawHandle(rawHandle)
	{
	}

	// the handle may still not resolve of course!
	bool IsValid() const { return m_index != -1; }
	void Invalidate() { m_index = -1; m_mgrId = -1; }

	I32 GetRawHandle() { return m_rawHandle; }

	bool operator==(const PathfindRequestHandle& other) const { return m_rawHandle == other.m_rawHandle; }
	bool operator!=(const PathfindRequestHandle& other) const { return !(*this == other); }

	union
	{
		struct
		{
			I16 m_index;
			U16 m_mgrId;
		};

		I32 m_rawHandle;
	};
};

/// --------------------------------------------------------------------------------------------------------------- ///
class PathfindManager
{
private:
	static U16 m_mgrId;

	enum PathfindType : U8
	{
		kTypeStatic,
		kTypeDistance,
		kTypeUndirected,
		kTypeCacheUndirected,
		kTypeCount
	};

	enum RequestFlags
	{
		kRequestOngoing,
		kRequestPendingDeletion,
		kRequestHighPriority,
		kRequestLowPriority,
		kRequestFlagsCount
	};

	typedef BitArray8	Flags;
	STATIC_ASSERT(kRequestFlagsCount <= Flags::GetMaxBitCount());

	class Request
	{
	public:
		Request() : m_mgrId(-1) { Init(); }
		Request(U16 mgrId) : m_mgrId(mgrId) { Init(); }

	private:
		void Init()
		{
			m_updateTime		= TimeFrameNegInfinity();
			m_paramsIdx			= -1;
			m_currentResultSlot = 0;
			m_bufferedIndices[0] = -1;
			m_bufferedIndices[1] = -1;
		}

	public:
		NdGameObjectHandle m_hGo;

		mutable NdRwAtomicLock64 m_lock;

		StringId64 m_nameId;

		TimeFrame m_updateTime;

		PathfindType m_type;
		Flags m_flags;

#ifdef HEADLESS_BUILD
		U16 m_paramsIdx;
		U16 m_currentResultSlot;
#else
		U8 m_paramsIdx;
		U8 m_currentResultSlot;
#endif

		// Double-buffered result indices
		I16 m_bufferedIndices[2];
		U16 m_mgrId;

		PathfindRequestHandle m_hRequestToCache;
	};

public:
	static PathfindManager& Get() { return s_singleton; }

	static ndjob::CounterHandle KickUpdateJob();

	void Init(U32 maxStaticPaths, U32 maxUndirectedPaths);
	void Update();

	void DebugDraw() const;
	void DebugDrawRequest(const PathfindRequestHandle& hRequest,
						  Color color0 = kColorRed,
						  Color color1 = kColorGreen) const;
	void DebugDrawSinglePathfind(const PathfindRequestHandle& hRequest,
								 Color color0 = kColorRed,
								 Color color1 = kColorGreen) const;
	void DebugDrawUndirectedPathfind(const PathfindRequestHandle& hRequest,
									 Color color0 = kColorRed,
									 Color color1 = kColorGreen) const;

	PathfindRequestHandle AddStaticRequest(StringId64 nameId,
										   NdGameObjectHandle hOwner,
										   const Nav::FindSinglePathParams& params,
										   bool ongoing = false,
										   bool highPriority = false);
	PathfindRequestHandle AddDistanceRequest(StringId64 nameId,
											 NdGameObjectHandle hOwner,
											 const Nav::FindSinglePathParams& params,
											 bool ongoing = false);
	PathfindRequestHandle AddUndirectedRequest(StringId64 nameId,
											   NdGameObjectHandle hOwner,
											   const Nav::FindUndirectedPathsParams& params,
											   bool ongoing = false,
											   bool highPriority = false,
											   bool lowPriority = false);

	PathfindRequestHandle CacheRequest(StringId64 nameId, const PathfindRequestHandle& hRequestToCache);

	void RemoveRequest(const PathfindRequestHandle& hRequest);

	void UpdateRequest(const PathfindRequestHandle& hRequest, const void* pNewParams);
	void FlipRequestDoubleBuffers();

	bool GetResults(PathfindRequestHandle hRequest, const Nav::FindSinglePathResults** pResultsOut) const;
	bool GetResults(PathfindRequestHandle hRequest, const Nav::FindUndirectedPathsResults** pResultsOut) const;

	bool GetParams(PathfindRequestHandle hRequest, const Nav::FindSinglePathParams** pParamsOut) const;
	bool GetParams(PathfindRequestHandle hRequest, const Nav::FindUndirectedPathsParams** pParamsOut) const;

	bool BuildPath(const PathfindRequestHandle& hRequest,
				   const Nav::BuildPathParams& buildParams,
				   const NavLocation& navLocation,
				   Nav::BuildPathResults* pBuildResults) const;

	bool CanPathTo(const PathfindRequestHandle& hRequest,
								const NavHandle& hNav,
#ifdef HEADLESS_BUILD
								NavKeyDataPair* pNodeDataOut = nullptr);
#else
								TrivialHashNavNodeData* pNodeDataOut = nullptr);
#endif

	bool GetApproxPathDistance(const PathfindRequestHandle& hRequest, const NavHandle& hNav, F32& pathLength);
	bool GetApproxPathDistance(const PathfindRequestHandle& hRequest, const NavLocation& navLocation, F32& pathLength);
	bool GetApproxPathDistanceSmooth(const PathfindRequestHandle& hRequest, const NavHandle& hNav, F32& pathLength);
	bool GetApproxPathDistanceSmooth(const PathfindRequestHandle& hRequest,
									 const NavLocation& navLocation,
									 F32& pathLength);

	Nav::BuildPathResults GetApproxPathSmooth(const PathfindRequestHandle& hRequest, const NavHandle& hNav);
	Nav::BuildPathResults GetApproxPathSmooth(const PathfindRequestHandle& hRequest, const NavLocation& navLocation);
	float GetFastApproxSmoothPathDistanceOnly(const PathfindRequestHandle& hRequest,
											  const NavLocation& navLocation,
											  bool* const pPathCrossesTap = nullptr);

	bool GetUndirectedPathFindOrigin(const PathfindRequestHandle& hRequest, Point& posWs) const;

	bool ApproxPathUsesTap(const PathfindRequestHandle& hRequest, const NavHandle& hNav, bool& usesTap);

	TimeFrame GetRequestUpdateTime(const PathfindRequestHandle& hRequest) const;

	static TimeFrame GetCurTime();

private:
	PathfindRequestHandle AllocRequest(PathfindType type,
									   StringId64 nameId,
									   NdGameObjectHandle hOwner,
									   const void* pParamsIn,
									   bool ongoing,
									   bool highPriority,
									   bool lowPriority);

	Request* GetRequestUnsafe(const PathfindRequestHandle& hRequest, bool allowDeleted=false);
	const Request* GetRequestUnsafe(const PathfindRequestHandle& hRequest, bool allowDeleted=false) const;

	void DeleteRequestUnsafe(Request& request);

	void ProcessRequest(Request* pRequest);
	void ProcessCacheRequest(Request* pRequest);

	FixedSizeHeap* GetParamsHeap(PathfindType type);
	const FixedSizeHeap* GetParamsHeap(PathfindType type) const;

	FixedSizeHeap* GetResultsHeap(PathfindType type);
	const FixedSizeHeap* GetResultsHeap(PathfindType type) const;

	U32 GetParamsSize(PathfindType type) const;
	U32 GetResultsSize(PathfindType type) const;

private:
	static PathfindManager	s_singleton;

	mutable NdRwAtomicLock64	m_lock;

	TypedFixedSizeHeap<Request>	m_requestsHeap;

	FixedSizeHeap	m_staticParamsHeap;
	FixedSizeHeap	m_staticResultsHeap;

	FixedSizeHeap	m_undirectedParamsHeap;
	FixedSizeHeap	m_undirectedResultsHeap;
};

#endif // PATHFIND_MANAGER_H

