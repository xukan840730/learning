/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nav/nav-mesh-gap-ex.h"

#include "corelib/containers/hashtable.h"
#include "corelib/math/csg/poly-clipper.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/nav-blocker-mgr.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nav/nav-ex-data.h"
#include "gamelib/gameplay/nav/nav-mesh-gap.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly-edge-gatherer.h"
#include "gamelib/gameplay/nav/nav-poly.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct BlockerPairId
{
	union
	{
		struct  
		{
			U32 m_iBlocker0;
			U32 m_iBlocker1;
		};
		U64 m_u64;
	};

	void Construct(U32 iBlocker0, U32 iBlocker1)
	{
		m_iBlocker0 = iBlocker0;
		m_iBlocker1 = iBlocker1;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
template<>
struct HashT<BlockerPairId>
{
	inline uintptr_t operator () (const BlockerPairId& key) const
	{
		return static_cast<uintptr_t>(key.m_u64);
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct BlockerBlockerGapKey
{
	BlockerPairId m_comboId;

	bool operator == (const BlockerBlockerGapKey& rhs) const
	{
		return (m_comboId.m_u64 == rhs.m_comboId.m_u64);
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct BlockerGapData
{
	const NavPoly* m_pPoly0;
	const NavPoly* m_pPoly1;
	Point m_pos0;
	Point m_pos1;
	float m_gapDist;
};

/// --------------------------------------------------------------------------------------------------------------- ///
typedef HashTable<BlockerBlockerGapKey, BlockerGapData> BlockerBlockerGapTable;

/// --------------------------------------------------------------------------------------------------------------- ///
struct BlockerEdgeId
{
	union
	{
		struct
		{
			U32 m_iBlocker;
			U16 m_iPoly;
			U16 m_iPolyEdge;
		};
		U64 m_u64;
	};
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct BlockerEdgeGapKey
{
	BlockerEdgeId m_id;

	bool operator == (const BlockerEdgeGapKey& rhs) const
	{
		return (m_id.m_u64 == rhs.m_id.m_u64);
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
template<>
struct HashT<BlockerEdgeGapKey>
{
	inline uintptr_t operator () (const BlockerEdgeGapKey& key) const
	{
		return static_cast<uintptr_t>(key.m_id.m_u64);
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
typedef HashTable<BlockerEdgeGapKey, BlockerGapData> BlockerEdgeGapTable;

/// --------------------------------------------------------------------------------------------------------------- ///
struct BBGapExCallbackData
{
	BlockerBlockerGapTable* m_pGapTable;
	BlockerPairId m_comboId;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct BEGapExCallbackData
{
	BlockerEdgeGapTable* m_pGapTable;
	const NavMesh* m_pMesh;
	BlockerEdgeId m_id;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct GatherPolysData
{
	static const size_t kMaxPolys = 128;

	const NavPoly* m_pPolyList[kMaxPolys];
	size_t m_numPolys;

	const NavPolyEx* m_pPolyExList[kMaxPolys];
	size_t m_numExPolys;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static void AddBBGapExCallback(float gapDist, Point_arg p0, Point_arg p1, uintptr_t userdata)
{
	//PROFILE_ACCUM(AddGapExCallback);

	BBGapExCallbackData* pData = (BBGapExCallbackData*)userdata;

	BlockerBlockerGapKey newKey;
	newKey.m_comboId = pData->m_comboId;
	
	BlockerGapData newData;
	newData.m_pos0 = p0;
	newData.m_pos1 = p1;
	newData.m_gapDist = gapDist;

	BlockerBlockerGapTable::Iterator itr = pData->m_pGapTable->Find(newKey);

	if (itr != pData->m_pGapTable->End())
	{
		BlockerGapData& data = itr->m_data;

		if (data.m_gapDist > gapDist)
		{
			data = newData;
		}
	}
	else
	{
		pData->m_pGapTable->Set(newKey, newData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void TryGenerateGapsBlockerBlocker(BlockerBlockerGapTable* pGapTable,
										   U64 iNearBlocker,
										   U64 iFarBlocker,
										   const Clip2D::Poly* pBlockerClipPolysWs,
										   float maxGapDiameter)
{
	//PROFILE_ACCUM(TryGenerateGapsBlockerBlocker);

	const Clip2D::Poly& nearPolyWs = pBlockerClipPolysWs[iNearBlocker];
	const Clip2D::Poly& farPolyWs = pBlockerClipPolysWs[iFarBlocker];

	for (U32F iV0 = 0; iV0 < nearPolyWs.m_numVerts; ++iV0)
	{
		const Point v00Ws = nearPolyWs.GetVertex(iV0);
		const Point v01Ws = nearPolyWs.GetNextVertex(iV0);
		
		for (U32F iV1 = 0; iV1 < farPolyWs.m_numVerts; ++iV1)
		{
			const Point v10Ws = farPolyWs.GetVertex(iV1);
			const Point v11Ws = farPolyWs.GetNextVertex(iV1);

			BBGapExCallbackData cbData;
			cbData.m_pGapTable = pGapTable;
			cbData.m_comboId.Construct(iNearBlocker, iFarBlocker);
			
			// note we manually reverse the winding here since nav blockers and nav polys have opposite windings
			TryGenerateGaps(v01Ws, v00Ws, v11Ws, v10Ws, maxGapDiameter, AddBBGapExCallback, (uintptr_t)&cbData);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void GatherPolysFunc(const NavPoly* pPoly, const NavPolyEx* pPolyEx, uintptr_t userData)
{
	GatherPolysData* pData = (GatherPolysData*)userData;

	if (pPoly)
	{
		bool exists = false;
		for (U32F iPoly = 0; iPoly < pData->m_numPolys; ++iPoly)
		{
			if (pPoly == pData->m_pPolyList[iPoly])
			{
				exists = true;
				break;
			}
		}

		if (!exists && (pData->m_numPolys < GatherPolysData::kMaxPolys))
		{
			pData->m_pPolyList[pData->m_numPolys] = pPoly;
			pData->m_numPolys++;
		}
	}

	if (pPolyEx)
	{
		bool exists = false;
		for (U32F iPolyEx = 0; iPolyEx < pData->m_numExPolys; ++iPolyEx)
		{
			if (pPolyEx == pData->m_pPolyExList[iPolyEx])
			{
				exists = true;
				break;
			}
		}

		if (!exists && (pData->m_numExPolys < GatherPolysData::kMaxPolys))
		{
			pData->m_pPolyExList[pData->m_numExPolys] = pPolyEx;
			pData->m_numExPolys++;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const NavPoly* GetGapStartPoly(const NavMesh* pSourceMesh, const NavPoly* pSourcePoly, Point_arg posLs)
{
	if (!pSourcePoly)
		return nullptr;

	if (pSourcePoly->PolyContainsPointLs(posLs))
		return pSourcePoly;

	const U32F polyCount = pSourceMesh->GetPolyCount();

	if (const NavPolyDistEntry* pDistList = pSourcePoly->GetPolyDistList())
	{
		for (U32F i = 0; true; ++i)
		{
			const U32F iPoly = pDistList[i].GetPolyIndex();
			if (iPoly >= polyCount)
				break;

			const NavPoly& poly = pSourceMesh->GetPoly(iPoly);

			if (poly.PolyContainsPointLs(posLs))
			{
				return &poly;
			}
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void RegisterGapToPolys(NavMeshGapEx* pGap, const NavMesh* pMesh, const NavPoly* pSourcePoly)
{
	//PROFILE_ACCUM(RegisterGapToPolys);

	GatherPolysData gpData;
	gpData.m_numPolys = gpData.m_numExPolys = 0;

	NavMesh::ProbeParams walkParams;
	walkParams.m_start		  = pGap->m_pos0Ls;
	walkParams.m_move		  = pGap->m_pos1Ls - pGap->m_pos0Ls;
	walkParams.m_dynamicProbe = false;
	walkParams.m_pStartPoly	  = GetGapStartPoly(pMesh, pSourcePoly, pGap->m_pos0Ls);
	walkParams.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;

	if (false)
	{
		const Point p0Ws = pMesh->LocalToWorld(pGap->m_pos0Ls);
		const Point p1Ws = pMesh->LocalToWorld(pGap->m_pos1Ls);
		g_prim.Draw(DebugArrow(p0Ws, p1Ws, kColorGreen));
	}

	if (!walkParams.m_pStartPoly)
		return;

	pMesh->WalkPolysInLineLs(walkParams, GatherPolysFunc, (uintptr_t)&gpData);

	for (U32F iPoly = 0; iPoly < gpData.m_numPolys; ++iPoly)
	{
		const NavPoly* pPoly = gpData.m_pPolyList[iPoly];
		//NAV_ASSERT(pPoly);
		if (!pPoly)
			continue;

		g_navExData.RegisterExGapUnsafe(pGap, pPoly);

		// boooo
		const_cast<NavPoly*>(pPoly)->SetHasExGaps(true);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void RegisterGapsFromTable(const BlockerBlockerGapTable& table, const NavMeshPatchInput& patchInput)
{
	//PROFILE_ACCUM(RegisterGapsFromTable);

	NavBlockerMgr& nbMgr = NavBlockerMgr::Get();

	for (BlockerBlockerGapTable::ConstIterator itr = table.Begin(); itr != table.End(); itr++)
	{
		const BlockerBlockerGapKey& k = itr->m_key;
		const BlockerGapData& d = itr->m_data;

		const DynamicNavBlocker* pBlocker0 = &patchInput.m_pBlockers[k.m_comboId.m_iBlocker0];
		const DynamicNavBlocker* pBlocker1 = &patchInput.m_pBlockers[k.m_comboId.m_iBlocker1];

		const NavMesh* pMesh0 = pBlocker0->GetNavMesh();

		NavMeshGapEx* pNewGap0 = g_navExData.AllocateGapExUnsafe();

		if (!pNewGap0)
			return;

		pNewGap0->m_blockerBits.ClearAllBits();
		pNewGap0->m_blockerBits.SetBit(patchInput.m_pMgrIndices[k.m_comboId.m_iBlocker0]);
		pNewGap0->m_blockerBits.SetBit(patchInput.m_pMgrIndices[k.m_comboId.m_iBlocker1]);
		pNewGap0->m_pos0Ls = pMesh0->WorldToLocal(d.m_pos0);
		pNewGap0->m_pos1Ls = pMesh0->WorldToLocal(d.m_pos1);
		pNewGap0->m_gapDist = d.m_gapDist;

		RegisterGapToPolys(pNewGap0, pMesh0, pBlocker0->GetNavPoly());

		const NavMesh* pMesh1 = pBlocker1->GetNavMesh();

		if (pMesh0 != pMesh1)
		{
			NavMeshGapEx* pNewGap1 = g_navExData.AllocateGapExUnsafe();

			if (!pNewGap1)
				return;

			*pNewGap1 = *pNewGap0;
			pNewGap1->m_pos0Ls = pMesh1->WorldToLocal(d.m_pos0);
			pNewGap1->m_pos1Ls = pMesh1->WorldToLocal(d.m_pos1);

			RegisterGapToPolys(pNewGap1, pMesh1, pBlocker1->GetNavPoly());
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void AddBEGapExCallback(float gapDist, Point_arg p0, Point_arg p1, uintptr_t userdata)
{
	BEGapExCallbackData* pData = (BEGapExCallbackData*)userdata;

	BlockerEdgeGapKey newKey;
	newKey.m_id = pData->m_id;

	BlockerGapData newData;
	newData.m_pos0 = p0;
	newData.m_pos1 = p1;
	newData.m_gapDist = gapDist;

	BlockerEdgeGapTable::Iterator itr = pData->m_pGapTable->Find(newKey);

	if (itr != pData->m_pGapTable->End())
	{
		BlockerGapData& data = itr->m_data;

		if (data.m_gapDist > gapDist)
		{
			data = newData;
		}
	}
	else
	{
		pData->m_pGapTable->Set(newKey, newData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void TryGenerateGapsBlockerEdge(BlockerEdgeGapTable* pGapTable,
									   U64 iBlocker,
									   const NavMeshPatchInput& patchInput,
									   const Clip2D::Poly& blockerClipPolyWs,
									   const NavPolyEdge* pEdgeList,
									   const U32F numEdges,
									   float maxGapDiameter)
{
	//PROFILE_ACCUM(TryGenerateGapsBlockerEdge);

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	const DynamicNavBlocker* pNavBlocker = &patchInput.m_pBlockers[iBlocker];

	pGapTable->Clear();

	{
		//PROFILE_ACCUM(TryGenerateGapsBlockerEdge_Search);

		for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
		{
			const NavPoly* pPoly = NavPolyHandle(pEdgeList[iEdge].m_sourceId).ToNavPoly();
			const NavMesh* pMesh = pPoly->GetNavMesh();

			const Point e0Ls = pEdgeList[iEdge].m_v0;
			const Point e1Ls = pEdgeList[iEdge].m_v1;

			for (U32F iV0 = 0; iV0 < blockerClipPolyWs.m_numVerts; ++iV0)
			{
				const Point b0Ws = blockerClipPolyWs.GetVertex(iV0);
				const Point b1Ws = blockerClipPolyWs.GetNextVertex(iV0);

				const Point b0Ls = pMesh->WorldToLocal(b0Ws);
				const Point b1Ls = pMesh->WorldToLocal(b1Ws);

				BEGapExCallbackData data;
				data.m_pMesh = pMesh;
				data.m_id.m_iBlocker = iBlocker;
				data.m_id.m_iPoly = pPoly->GetId();
				data.m_id.m_iPolyEdge = pEdgeList[iEdge].m_iEdge;
				data.m_pGapTable = pGapTable;

				TryGenerateGaps(b1Ls, b0Ls, e0Ls, e1Ls, maxGapDiameter, AddBEGapExCallback, (uintptr_t)&data);
			}
		}
	}

	const I32F blockerIndex = patchInput.m_pMgrIndices[iBlocker];

	if (blockerIndex >= 0)
	{
		//PROFILE_ACCUM(TryGenerateGapsBlockerEdge_Apply);

		const NavMesh* pMesh = pNavBlocker->GetNavMesh();
		const NavPoly* pBlockerPoly = pNavBlocker->GetNavPoly();

		for (BlockerEdgeGapTable::ConstIterator itr = pGapTable->Begin(); itr != pGapTable->End(); itr++)
		{
			const BlockerEdgeGapKey& k = itr->m_key;
			const BlockerGapData& d = itr->m_data;

			bool alreadyAdded = false;
			for (BlockerEdgeGapTable::ConstIterator itr2 = pGapTable->Begin(); itr2 != itr; itr2++)
			{
				const BlockerGapData& d2 = itr2->m_data;
				const float d0 = Max(Dist(d.m_pos0, d2.m_pos0), Dist(d.m_pos1, d2.m_pos1));
				const float d1 = Max(Dist(d.m_pos0, d2.m_pos1), Dist(d.m_pos1, d2.m_pos0));
				const float compDist = Min(d0, d1);

				if (compDist < 0.01f)
				{
					alreadyAdded = true;
					break;
				}
			}

			if (alreadyAdded)
				continue;

			NavMeshGapEx* pNewGap = g_navExData.AllocateGapExUnsafe();

			if (!pNewGap)
				return;

			pNewGap->m_blockerBits.ClearAllBits();
			pNewGap->m_blockerBits.SetBit(blockerIndex);
			pNewGap->m_pos0Ls = d.m_pos0;
			pNewGap->m_pos1Ls = d.m_pos1;
			pNewGap->m_gapDist = d.m_gapDist;

			RegisterGapToPolys(pNewGap, pMesh, pBlockerPoly);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GenerateNavMeshExGaps(const NavMeshPatchInput& input,
						   const Clip2D::Poly* pBlockerClipPolysWs,
						   const NavPolyList* pBlockerPolys,
						   const NavBlockerBits& blockersToGen,
						   float maxGapDiameter)
{
	PROFILE_ACCUM(GenerateNavMeshExGaps);
	PROFILE_AUTO(Navigation);

	const float kEpsilon = 0.01f;

	if ((0 == input.m_numBlockers) || (maxGapDiameter < kSmallestFloat))
		return;

	{
		//PROFILE_ACCUM(GenerateNavMeshExGaps_BB);

		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		BlockerBlockerGapTable bbGapTable;
		bbGapTable.Init(128, FILE_LINE_FUNC);

		// blocker vs. blocker gaps
		for (U64 iBlocker = blockersToGen.FindFirstSetBit(); iBlocker < input.m_numBlockers;
			 iBlocker	  = blockersToGen.FindNextSetBit(iBlocker))
		{
			const DynamicNavBlocker* pNearBlocker = &input.m_pBlockers[iBlocker];

			for (U64 iOtherBlocker = blockersToGen.FindNextSetBit(iBlocker); iOtherBlocker < input.m_numBlockers;
				 iOtherBlocker	   = blockersToGen.FindNextSetBit(iOtherBlocker))
			{
				const DynamicNavBlocker* pFarBlocker = &input.m_pBlockers[iOtherBlocker];

				const Point nearPosPs = pNearBlocker->GetPosPs();
				const Point farPosPs = pFarBlocker->GetPosPs();

				const float nearRadius = pNearBlocker->GetBoundingRadius();
				const float farRadius = pFarBlocker->GetBoundingRadius();

				const float blockerDist = DistXz(nearPosPs, farPosPs);

				if (blockerDist > (nearRadius + farRadius + maxGapDiameter + kEpsilon))
					continue;

				const float yDelta = Abs(nearPosPs.Y() - farPosPs.Y());
				if (yDelta >= 2.0f)
					continue;

				bbGapTable.Clear();

				TryGenerateGapsBlockerBlocker(&bbGapTable, iBlocker, iOtherBlocker, pBlockerClipPolysWs, maxGapDiameter);

				RegisterGapsFromTable(bbGapTable, input);
			}
		}
	}

	{
		//PROFILE_ACCUM(GenerateNavMeshExGaps_BE);

		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		BlockerEdgeGapTable beGapTable;
		beGapTable.Init(128, FILE_LINE_FUNC);
		NavMesh::BaseProbeParams probeParams;
		probeParams.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskAll;

		for (U64 iBlocker = blockersToGen.FindFirstSetBit(); iBlocker < input.m_numBlockers;
			 iBlocker	  = blockersToGen.FindNextSetBit(iBlocker))
		{
			const DynamicNavBlocker* pNavBlocker = &input.m_pBlockers[iBlocker];

			const NavPolyList& polyList = pBlockerPolys[iBlocker];
			if (polyList.m_numPolys == 0)
				continue;

			const NavMesh* pBlockerMesh = nullptr;
			const NavPoly* pBlockerPoly = pNavBlocker->GetNavPolyHandle().ToNavPoly(&pBlockerMesh);
			if (!pBlockerPoly || !pBlockerMesh)
				continue;

			const float blockerRadius = pNavBlocker->GetBoundingRadius() * 1.1f;
			const float searchRadius = blockerRadius + maxGapDiameter;

			//const float kEpsilon = 0.001f; // unused and different from earlier definition?
			const Point centerPs = pNavBlocker->GetPosPs();

			ScopedTempAllocator scopedTempInner(FILE_LINE_FUNC);
			const Point centerLs = pBlockerMesh->ParentToLocal(centerPs);
			NavPolyEdgeGatherer* pEdgeGatherer = NDI_NEW NavPolyEdgeGatherer;

			pEdgeGatherer->Init(centerLs, searchRadius, 2.0f, probeParams, 256, searchRadius);
			pEdgeGatherer->Execute(pBlockerPoly);

			ListArray<NavPolyEdge>& edges = pEdgeGatherer->GetResults();
			NavPolyEdge* pGapEdges = edges.ArrayPointer();
			I32F numEdges = edges.Size();

			for (I32F iEdge = numEdges - 1; iEdge >= 0; --iEdge)
			{
				const Point v0Ls = pGapEdges[iEdge].m_v0;
				const Point v1Ls = pGapEdges[iEdge].m_v1;

				const Vector edgeNormalVec = -RotateY90(VectorXz(v0Ls - v1Ls));
				const Vector edgeToBlocker = centerLs - v0Ls;
				const float edgeDot = DotXz(edgeNormalVec, edgeToBlocker);

				if (edgeDot < kSmallestFloat)
				{
					// reject edge to make a gap
					pGapEdges[iEdge] = pGapEdges[numEdges - 1];
					--numEdges;
				}
			}

			TryGenerateGapsBlockerEdge(&beGapTable,
									   iBlocker,
									   input,
									   pBlockerClipPolysWs[iBlocker],
									   pGapEdges,
									   numEdges,
									   maxGapDiameter);
		}
	}
}
