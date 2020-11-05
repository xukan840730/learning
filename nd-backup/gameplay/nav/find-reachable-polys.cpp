/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/find-reachable-polys.h"

#include "corelib/containers/hashtable.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/render/util/prim-server-wrapper.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-node-table.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"

namespace Nav
{
	/// --------------------------------------------------------------------------------------------------------------- ///
	struct FindReachablePolysInput
	{
		FindReachablePolysParams	m_params;
		FindReachablePolysResults*	m_pResults;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	JOB_ENTRY_POINT(FindReachablePolys)
	{
		FindReachablePolysInput* pInputBuffer = (FindReachablePolysInput*)jobParam;
		FindReachablePolys(pInputBuffer->m_params, pInputBuffer->m_pResults);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	FindReachablePolysResults* BeginFindReachablePolys(const FindReachablePolysParams& params,
															 const char* sourceFile,
															 U32 sourceLine,
															 const char* sourceFunc,
															 FindReachablePolysResults* pExistingFindPolysResults /* = nullptr */)
	{
		FindReachablePolysResults* pResults = pExistingFindPolysResults;

		if (!pResults)
		{
			pResults = NDI_NEW(kAllocDoubleGameFrame, kAlign16) FindReachablePolysResults;
		}

		FindReachablePolysInput* pInputBuffer = NDI_NEW(kAllocDoubleGameFrame, kAlign16) FindReachablePolysInput;

		if (!pResults || !pInputBuffer)
		{
			NAV_ASSERTF(false, ("BeginFindReachablePolys(): Out of double-frame memory"));
			return nullptr;
		}

		pInputBuffer->m_params = params;
		pInputBuffer->m_pResults = pResults;

		ndjob::JobDecl findStaticPathJob(FindReachablePolys, (uintptr_t)pInputBuffer);
		findStaticPathJob.m_flags = ndjob::kRequireLargeStack;
		ndjob::RunJobs(&findStaticPathJob, 1, &pResults->m_pCounter, sourceFile, sourceLine, sourceFunc);

		return pResults;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	void CollectFindReachablePolysResults(FindReachablePolysResults* pFindPolysResults)
	{
		if (!pFindPolysResults)
			return;

		if (pFindPolysResults->m_pCounter)
		{
			ndjob::WaitForCounterAndFree(pFindPolysResults->m_pCounter);
			pFindPolysResults->m_pCounter = nullptr;
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static bool IsNodeInSearchRadius(const NavPathNode* pNode,
									 const NavMesh* pNavMesh,
#if ENABLE_NAV_LEDGES
									 const NavLedgeGraph* pLedgeGraph,
#endif // ENABLE_NAV_LEDGES
									 const PathFindParams* pParams)
	{
		if (!pNode || !pNavMesh)
			return false;

		NAV_ASSERT(pNode->GetNavMeshHandle().GetManagerId() == pNavMesh->GetManagerId());

		const FindReachablePolysParams& params = *(FindReachablePolysParams*)pParams;
		const Scalar radiusSqr = Sqr(params.m_radius);

		const Point nodePosPs = pNode->GetPositionPs();

		for (U32F iStart = 0; iStart < pParams->m_numStartPositions; ++iStart)
		{
			const Point startPosPs = pParams->m_startPositions[iStart].GetPosPs();

			Point testPosPs = nodePosPs;

			switch (pNode->GetNodeType())
			{
			case NavPathNode::kNodeTypePoly:
				{
					const NavPoly* pPoly = &pNavMesh->GetPoly(pNode->GetNavManagerId().m_iPoly);
					pPoly->FindNearestPointPs(&testPosPs, startPosPs);
				}
				break;

			case NavPathNode::kNodeTypePolyEx:
				{
					if (const NavPolyEx* pPolyEx = pNode->GetNavPolyExHandle().ToNavPolyEx())
					{
						pPolyEx->FindNearestPointPs(&testPosPs, startPosPs);
					}
				}
				break;
			}

			const Scalar distSqr = DistXzSqr(startPosPs, testPosPs);
			const Scalar distY = Abs(startPosPs.Y() - testPosPs.Y());

			if (distSqr < radiusSqr && distY < params.m_findPolyYThreshold)
			{
				return true;
			}
		}

		return false;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	void FindReachablePolys(const FindReachablePolysParams& params, FindReachablePolysResults* pResults)
	{
		PROFILE_ACCUM(FindReachablePolys);
		PROFILE_AUTO(AI);

		if (g_navPathNodeMgr.Failed())
		{
			pResults->m_outCount = 0;
			pResults->m_fringeNodes.ClearAllBits();
			memset(pResults->m_navPolys, 0, sizeof(NavPolyHandle) * FindReachablePolysResults::kMaxPolyOutCount);
			memset(pResults->m_travelCostVecs, 0, sizeof(Vec4) * FindReachablePolysResults::kMaxPolyOutCount);

			return;
		}

		NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		PathFindNodeData workData;

		workData.m_visitedNodes.Init(FindReachablePolysResults::kMaxPolyOutCount * 2, FILE_LINE_FUNC); // room for taps
		workData.Init(params);
		workData.m_pFnShouldExpand = IsNodeInSearchRadius;

		PathFindLocation aStartLocations[PathFindParams::kMaxStartPositions];
		U32F numStartLocations = 0;

		for (U32F iStartInput = 0; iStartInput < params.m_numStartPositions; ++iStartInput)
		{
			const NavLocation& startPos = params.m_startPositions[iStartInput];

			PathFindLocation startLocation;
			if (!startLocation.Create(params, startPos))
				continue;

			startLocation.m_baseCost = params.m_startCosts[iStartInput];
			aStartLocations[numStartLocations++] = startLocation;
		}

		NAV_ASSERT(!workData.m_trivialHashVisitedNodes);
		const PathFindResults results = DoPathFind<false>(params, workData, aStartLocations, numStartLocations, nullptr, 0);

		pResults->m_outCount = 0;
		pResults->m_fringeNodes.ClearAllBits();
		memset(pResults->m_navPolys, 0, sizeof(NavPolyHandle) * FindReachablePolysResults::kMaxPolyOutCount);
		memset(pResults->m_travelCostVecs, 0, sizeof(Vec4) * FindReachablePolysResults::kMaxPolyOutCount);

		for (NavNodeTable::Iterator itr = workData.m_visitedNodes.Begin(); itr != workData.m_visitedNodes.End(); itr++)
		{
			const NavNodeKey& k = itr->m_key;
			const NavNodeData& d = itr->m_data;

			const NavPathNode& curNode = g_navPathNodeMgr.GetNode(k.GetPathNodeId());

			if (curNode.GetNodeType() != NavPathNode::kNodeTypePoly && curNode.GetNodeType() != NavPathNode::kNodeTypePolyEx)
				continue;

			const NavPoly* pCurPoly = curNode.GetNavPolyHandle().ToNavPoly();
			if (!pCurPoly)
				continue;

			pResults->m_navPolys[pResults->m_outCount] = curNode.GetNavPolyHandle();

			Vec4 costVec = d.m_pathNodePosPs.GetVec4();
			costVec.SetW(d.m_fromCost);

			if (FALSE_IN_FINAL_BUILD(params.m_debugDrawResults))
			{
				PrimServerWrapper ps(params.m_context.m_parentSpace);

				ps.SetDuration(params.m_debugDrawTime);

				pCurPoly->DebugDraw(d.m_fringeNode ? kColorBlueTrans : kColorGreenTrans);

				NavNodeTable::ConstIterator parentItr = workData.m_visitedNodes.Find(d.m_parentNode);

				if (parentItr != workData.m_visitedNodes.End())
				{
					const NavNodeData& parentData = parentItr->m_data;
					ps.SetLineWidth(4.0f);
					ps.DrawLine(parentData.m_pathNodePosPs, d.m_pathNodePosPs, kColorYellow);
				}

				StringBuilder<128> nodeStr("%5.3f", (float)costVec.W());
				const U16 partitionId = k.GetPartitionId();
				if (partitionId)
				{
					nodeStr.append_format(" [%d]", partitionId);
				}
				ps.DrawString(d.m_pathNodePosPs, nodeStr.c_str(), kColorWhite, 0.5f);
			}

			pResults->m_travelCostVecs[pResults->m_outCount] = costVec;
			pResults->m_fringeNodes.AssignBit(pResults->m_outCount, d.m_fringeNode);
			++pResults->m_outCount;

			if (pResults->m_outCount >= FindReachablePolysResults::kMaxPolyOutCount)
			{
				break;
			}
		}

		pResults->m_finished = true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	U32F FindReachablePolys(NavPolyHandle* outPolyHandles,
							   Vec4* pOutTravelCostVecs,
							   U32F maxPolyCount,
							   const FindReachablePolysParams& params)
	{
		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
		FindReachablePolysResults* pResults = NDI_NEW FindReachablePolysResults;
		FindReachablePolys(params, pResults);

		// copy out the results
		U32F outCount = Min(maxPolyCount, U32F(pResults->m_outCount));
		for (U32F i = 0; i < outCount; ++i)
		{
			outPolyHandles[i] = pResults->m_navPolys[i];
			pOutTravelCostVecs[i] = pResults->m_travelCostVecs[i];
		}
		return outCount;
	}

} // namespace Nav
