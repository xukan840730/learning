/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#if ENABLE_NAV_LEDGES

#include "gamelib/gameplay/nav/nav-ledge-graph-util.h"

#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/feature/feature-db.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"

/// --------------------------------------------------------------------------------------------------------------- ///
NavLedgeGraphDrawFilter::NavLedgeGraphDrawFilter()
{
	m_drawNames = false;
	m_drawLedges = false;
	m_drawLedgeNormals = false;
	m_drawFeatureFlags = false;
	m_drawLedgeIds = false;
	m_drawLinkIds = false;
	m_drawLinkDetails = false;
}

NavLedgeGraphDrawFilter g_navLedgeGraphDrawFilter;

/// --------------------------------------------------------------------------------------------------------------- ///
static void BuildFeatureEdgeFlagString(StringBuilder<256>& desc, U32 edgeFlags)
{
	struct FlagDesc
	{
		U32 m_flag;
		const char* m_desc;
	};

	static const FlagDesc s_descArray[] =
	{
		{FeatureEdge::kFlagNoSwing, "NoSwing"},
		{FeatureEdge::kFlagCanStand, "CanStand"},
		{FeatureEdge::kFlagCanHang, "CanHang"},
		{FeatureEdge::kFlagResistWalk, "ResistWalk"},
		{FeatureEdge::kFlagCanUseLowCover, "CanUseLowCover"},
		{FeatureEdge::kFlagLadder, "Ladder"},
		{FeatureEdge::kFlagNoJumpAim, "NoJumpAim"},
		{FeatureEdge::kFlagVerticalCover, "VerticalCover"},
		{FeatureEdge::kFlagVault, "Vault"},
		{FeatureEdge::kFlagDisabled, "Disabled"},
		{FeatureEdge::kFlagCanGrapple, "CanGrapple"},
		//{FeatureEdge::kFlagStandingShimmy, "StandingShimmy"},
		//{FeatureEdge::kFlagUnsafe, "Unsafe"},
		{FeatureEdge::kFlagForeground, "Foreground"},
		{FeatureEdge::kFlagLoop, "Loop"},
		//{FeatureEdge::kFlagDummy, "Dummy"},
		{FeatureEdge::kFlagCanCrouch, "CanCrouch"},
		//{FeatureEdge::kFlagForceHang, "ForceHang"},
		//{FeatureEdge::kFlagVerticalPipe, "VerticalPipe"},
		{FeatureEdge::kFlagCanPlank, "CanPlank"},
		//{FeatureEdge::kFlagEdgeBar, "EdgeBar"},
		{FeatureEdge::kFlagForceGrab, "ForceGrab"},
		{FeatureEdge::kFlagEdgeDynamic, "EdgeDynamic"}
	};
	static const size_t descArraySize = ARRAY_COUNT(s_descArray);

	for (U32F i = 0; i < descArraySize; ++i)
	{
		if (edgeFlags & s_descArray[i].m_flag)
		{
			desc.append_format(" %s", s_descArray[i].m_desc);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphDebugDraw(const NavLedgeGraph* pLedgeGraph, const NavLedgeGraphDrawFilter& filter)
{
	STRIP_IN_FINAL_BUILD;

	if (!pLedgeGraph)
		return;

	const Locator originWs = pLedgeGraph->GetBoundFrame().GetLocatorWs();

	StringBuilder<256> desc;

	PrimServerWrapper ps(originWs);

	ps.EnableHiddenLineAlpha();
	ps.SetLineWidth(4.0f);

	const Color clrMain = pLedgeGraph->IsRegistered() ? kColorGreen : kColorGray;
	const Color clrMainTrans = pLedgeGraph->IsRegistered() ? kColorGreenTrans : kColorGrayTrans;

	const Color clrLink = pLedgeGraph->IsRegistered() ? kColorOrange : kColorGray;
	const Color clrLinkDetails = pLedgeGraph->IsRegistered() ? kColorOrangeTrans : kColorGrayTrans;

	if (filter.m_drawLedges || filter.m_drawLedgeIds || filter.m_drawLedgeNormals || filter.m_drawLinkIds || filter.m_drawFeatureFlags || filter.m_drawLinkDetails)
	{
		const U32F numLedges = pLedgeGraph->GetLedgeCount();
		for (U32F iLedge = 0; iLedge < numLedges; ++iLedge)
		{
			const NavLedge* pLedge = pLedgeGraph->GetLedge(iLedge);
			if (!pLedge)
				continue;

			const Point v0 = pLedge->GetVertex0Ls();
			const Point v1 = pLedge->GetVertex1Ls();

			const Point center = AveragePos(v0, v1);

			desc.clear();
			desc.append_format("id: %d", (int)iLedge);

			if (filter.m_drawFeatureFlags)
			{
				BuildFeatureEdgeFlagString(desc, pLedge->GetFeatureFlags());
			}

			if (filter.m_drawLedgeIds || filter.m_drawFeatureFlags)
			{
				ps.DrawString(center, desc.c_str(), clrMainTrans, 0.6f);
			}

			pLedge->DebugDraw(clrMain);

			if (filter.m_drawLedgeNormals)
			{
				ps.DrawArrow(center, pLedge->GetWallNormalLs(), 0.3f, pLedgeGraph->IsRegistered() ? kColorCyanTrans : kColorGrayTrans);
				ps.DrawArrow(center, pLedge->GetWallBinormalLs(), 0.3f, pLedgeGraph->IsRegistered() ? kColorMagentaTrans : kColorGrayTrans);
			}

			const size_t numNeighbors = pLedge->GetNumNeighbors();
			for (U32F iNeighbor = 0; iNeighbor < numNeighbors; ++iNeighbor)
			{
				const NavLedge::Link& link = pLedge->GetLink(iNeighbor);
				const NavLedge* pNeighbor = pLedgeGraph->GetLedge(link.m_destLedgeId);
				if (!pLedgeGraph)
					continue;

				const Segment mySeg = Segment(v0, v1);
				const Segment neighborSeg = Segment(pNeighbor->GetVertex0Ls(), pNeighbor->GetVertex1Ls());

				const Point nv0 = pNeighbor->GetVertex0Ls();
				const Point nv1 = pNeighbor->GetVertex1Ls();

				const Point myPos = Lerp(v0, v1, link.m_closest.m_srcTT);
				const Point neighborPos = Lerp(nv0, nv1, link.m_closest.m_destTT);
				const float dist = link.m_closest.m_dist;

				if (filter.m_drawLinkIds || filter.m_drawLinkDetails)
				{
					const Point idPos = Lerp(myPos, neighborPos, 0.333f);
					desc.clear();
					desc.append_format("link: %d", (int)iNeighbor);
					ps.DrawString(idPos, desc.c_str(), clrLinkDetails, 0.5f);
				}

				if (dist > 0.01f)
				{
					ps.DrawArrow(myPos, neighborPos, 0.4f, clrLink);

					if (filter.m_drawLinkDetails)
					{
						if (iLedge < link.m_destLedgeId)
						{
							desc.clear();
							desc.append_format("%0.1fm", dist);
							ps.DrawString(AveragePos(myPos, neighborPos), desc.c_str(), clrLinkDetails, 0.5f);
						}

						if ((link.m_v0.m_dist > 0.01f) && (link.m_v1.m_dist > 0.01f))
						{
							const Point v0Dest = Lerp(nv0, nv1, link.m_v0.m_destTT);
							ps.DrawArrow(v0, v0Dest, 0.3f, clrLinkDetails);
							desc.clear();
							desc.append_format("%0.1fm", link.m_v0.m_dist);
							ps.DrawString(AveragePos(v0, v0Dest), desc.c_str(), clrLinkDetails, 0.5f);

							const Point v1Dest = Lerp(nv0, nv1, link.m_v1.m_destTT);
							ps.DrawArrow(v1, v1Dest, 0.3f, clrLinkDetails);
							desc.clear();
							desc.append_format("%0.1fm", link.m_v1.m_dist);
							ps.DrawString(AveragePos(v1, v1Dest), desc.c_str(), clrLinkDetails, 0.5f);
						}
					}
				}
				else
				{
					ps.DrawSphere(myPos, 0.05f, clrMainTrans);
				}
			}
		}
	}

	if (filter.m_drawNames)
	{
		desc.clear();
		desc.append_format("%s [%s]", pLedgeGraph->GetName(), DevKitOnly_StringIdToString(pLedgeGraph->GetLevelId()));
		g_prim.Draw(DebugCoordAxesLabeled(originWs, desc.c_str(), 0.5f));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FindNavLedgeGraphFromArray(const NavLedgeGraphArray& ledgeGraphArray, FindNavLedgeGraphParams* pParams)
{
	if (!pParams)
		return;

	pParams->m_pLedgeGraph = nullptr;
	pParams->m_pNavLedge = nullptr;
	pParams->m_nearestPointWs = pParams->m_pointWs;

	float bestDist = pParams->m_searchRadius;

	const size_t numLedgeGraphs = ledgeGraphArray.Size();
	for (U32F iGraph = 0; iGraph < numLedgeGraphs; ++iGraph)
	{
		const NavLedgeGraph* pLedgeGraph = ledgeGraphArray.At(iGraph);
		if (!pLedgeGraph || !pLedgeGraph->IsRegistered())
			continue;

		const bool bindSpawnerMatches = (pParams->m_bindSpawnerNameId == Nav::kMatchAllBindSpawnerNameId)
			|| (pLedgeGraph->GetBindSpawnerNameId() == pParams->m_bindSpawnerNameId);

		if (!bindSpawnerMatches)
			continue;

		const Point posLs = pLedgeGraph->WorldToLocal(pParams->m_pointWs);
		const Aabb bboxLs = pLedgeGraph->GetBoundingBoxLs();

		if (!bboxLs.ContainsPoint(posLs))
			continue;

		const U32F numLedges = pLedgeGraph->GetLedgeCount();
		for (U32F iLedge = 0; iLedge < numLedges; ++iLedge)
		{
			const NavLedge* pLedge = pLedgeGraph->GetLedge(iLedge);
			if (!pLedge)
				continue;

			const Point p0 = pLedge->GetVertex0Ls();
			const Point p1 = pLedge->GetVertex1Ls();
			Point nearestPtLs;

			const float d = DistPointSegment(posLs, p0, p1, &nearestPtLs);

			if (d < bestDist)
			{
				pParams->m_pNavLedge = pLedge;
				pParams->m_pLedgeGraph = pLedgeGraph;
				pParams->m_nearestPointWs = pLedgeGraph->LocalToWorld(nearestPtLs);
				bestDist = d;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindClosestLedgeDfsLs(FindLedgeDfsParams* pParams)
{
	if (!pParams)
	{
		return false;
	}

	if (!pParams->m_pGraph)
	{
		return false;
	}

	if (pParams->m_startLedgeId >= pParams->m_pGraph->GetLedgeCount())
	{
		return false;
	}

	const NavLedge* pBestLedge = nullptr;
	float bestDist             = pParams->m_searchRadius;
	Point closestPt            = pParams->m_point;

	const U32 kMaxQueueSize      = 32;
	const U32 kMaxLedgeGraphSize = 256;

	typedef BitArray<kMaxLedgeGraphSize> LedgeBits;
	NAV_ASSERTF(pParams->m_pGraph->GetLedgeCount() <= kMaxLedgeGraphSize,
				("Bit array too small - Make kMaxLedgeGraphSize > %d", pParams->m_pGraph->GetLedgeCount()));

	LedgeBits visited;
	visited.ClearAllBits();
	U32F ledgeIds[kMaxQueueSize];
	U32F headIndex = 0;
	U32F tailIndex = 0;

	ledgeIds[headIndex++] = pParams->m_startLedgeId;		// Push

	while (headIndex != tailIndex)
	{
		const U32F ledgeId = ledgeIds[tailIndex];			// Pop
		NAV_ASSERTF(ledgeId < pParams->m_pGraph->GetLedgeCount(), ("Invalid ledge id"));

		if (++tailIndex == kMaxQueueSize)
		{
			tailIndex = 0;
		}

		visited.SetBit(ledgeId);							// Visit

		const NavLedge* pLedge = pParams->m_pGraph->GetLedge(ledgeId);
		NAV_ASSERTF(pLedge, ("Invalid ledge"));

		const Point v0 = pLedge->GetVertex0Ls();
		const Point v1 = pLedge->GetVertex1Ls();

		Point edgePt;
		const float d = DistPointSegment(pParams->m_point, v0, v1, &edgePt);

		if (d <= bestDist)
		{
			bestDist   = d;
			closestPt  = edgePt;
			pBestLedge = pLedge;

			for (U32F iNeighbor = 0; iNeighbor < pLedge->GetNumNeighbors(); ++iNeighbor)
			{
				const U32F neighborId = pLedge->GetLink(iNeighbor).m_destLedgeId;

				if (!visited.IsBitSet(neighborId))
				{
					ledgeIds[headIndex] = neighborId;		// Push

					if (++headIndex == kMaxQueueSize)
					{
						headIndex = 0;
					}

					NAV_ASSERTF(headIndex != tailIndex, ("Queue overflow - Make kMaxQueueSize > %d", kMaxQueueSize));
				}
			}
		}
	}

	pParams->m_pLedge       = pBestLedge;
	pParams->m_nearestPoint = closestPt;
	pParams->m_dist         = bestDist;

	return pParams->m_pLedge != nullptr;
}

#endif // ENABLE_NAV_LEDGES
