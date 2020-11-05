/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/nd-config.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/process-error.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-gap.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static void NavError(const char* strMsg, ...)
{
	STRIP_IN_FINAL_BUILD;

	char strBuffer[512];
	va_list args;
	va_start(args, strMsg);
	vsnprintf(strBuffer, 512, strMsg, args);
	va_end(args);
	MsgErr("Nav: ");
	MsgErr(strBuffer);
	MsgConErr(strBuffer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void NavLinkError(const NavMesh* pMesh, U32F iPoly, const char* errMsg, ...)
{
	STRIP_IN_FINAL_BUILD;

	NAV_ASSERT(pMesh);

	const U32F iPolyToUse = iPoly > pMesh->GetPolyCount() ? 0 : iPoly;
	const Point posLs = pMesh->GetPoly(iPolyToUse).GetCentroid();
	const Point posWs = pMesh->LocalToWorld(posLs) + Vector(0.0f, ((pMesh->GetNameId().GetValue() % 10) * 0.75f), 0.0f);

	char buf[1024];
	va_list args;
	va_start(args, errMsg);
	vsnprintf(buf, 1024, errMsg, args);
	va_end(args);

	// MsgConErr("  %s\n", buf);

	SpawnNavMeshErrorProcess(posWs, pMesh, iPolyToUse, buf);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static I8 FindSharedEdgeIndex(const NavPoly& curPoly, const NavPoly& nextPoly)
{
	I8 iSharedEdge = -1;
	U32F edgeCount = curPoly.GetVertexCount();

	// if polys are in the same navmesh (common case),
	if (curPoly.GetNavMesh() == nextPoly.GetNavMesh())
	{
		// look for the edge that matches poly-id
		U32F adjNodePolyId = nextPoly.GetId();
		//ASSERT(adjNodePolyId != NavPoly::kNoAdjacent);
		// for each edge,
		for (U32F iEdge = 0; iEdge < edgeCount; ++iEdge)
		{
			// match the edge based on adj node poly id
			if (curPoly.GetAdjacentId(iEdge) == adjNodePolyId)
			{
				iSharedEdge = iEdge;
				break;
			}
		}
	}
	else
	{
		// nodes are on different navmeshes
		const NavMesh* pCurMesh = curPoly.GetNavMesh();
		// for each edge,
		for (U32F iEdge = 0; iEdge < edgeCount; ++iEdge)
		{
			U32F adjPolyId = curPoly.GetAdjacentId(iEdge);
			if (adjPolyId != NavPoly::kNoAdjacent)
			{
				const NavPoly& edgePoly = pCurMesh->GetPoly(adjPolyId);
				// match the edge based on the adj poly being a link
				if (edgePoly.IsLink())
				{
					const NavMeshLink& link = pCurMesh->GetLink(edgePoly.GetLinkId());
					if (link.GetDestPreLinkPolyId() == nextPoly.GetId())
					{
						iSharedEdge = iEdge;
						break;
					}
				}
			}
		}
	}
	return iSharedEdge;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMesh::ConnectLink(U32F iLink, NavMesh* pDestMesh)
{
	NavMeshLink& link = m_linkArray[iLink];

	NAV_ASSERT(pDestMesh);
	NAV_ASSERT(link.m_hDestMesh.GetManagerId() == pDestMesh->GetManagerId());

	if (pDestMesh->GetBindSpawnerNameId() != GetBindSpawnerNameId())
	{
		NavLinkError(this,
					 link.m_srcLinkPolyId,
					 "%s and %s are in different parent spaces",
					 GetName(),
					 pDestMesh->GetName());
		return false;
	}

	if (link.m_srcLinkPolyId == NavPoly::kNoAdjacent)
	{
		NavLinkError(this,
					 link.m_srcLinkPolyId,
					 "%s has no pre-link polygon (poly that is adjacent to the link poly)",
					 GetName());
		return false;
	}

	if (link.m_destPreLinkPolyId == NavPoly::kNoAdjacent)
	{
		NavLinkError(this,
					 link.m_destPreLinkPolyId,
					 "%s has no pre-link polygon (poly that is adjacent to the link poly)",
					 pDestMesh->GetName());
		return false;
	}

	// 'linkPoly' is the link poly we are joining to the other navmesh, which means that:
	//		'linkPoly' vertexes 0 and 1 are the shared edge between the navmeshes (this is by convention, enforced in the
	//tools in NavMeshBuilder) 		'linkPoly' vertexes 2 and 3 will be copied from vertexes in the pre-link poly from the
	//other mesh 	this will ensure that the 2 navmeshes overlap perfectly
	NavPoly& linkPoly = m_pPolyArray[link.m_srcLinkPolyId];
	if (!linkPoly.IsValid())
	{
		NavLinkError(this, link.m_srcLinkPolyId, "Link poly is not valid\n");
		return false;
	}

	// 'destPreLinkPoly' is the pre-link poly from the other navmesh.  It should perfectly overlap with link poly, although
	//	the vertex ordering may differ, and the local coordinates will be different
	const NavPoly& destPreLinkPoly = pDestMesh->GetPoly(link.m_destPreLinkPolyId);

	if (!destPreLinkPoly.IsValid())
	{
		NavLinkError(pDestMesh,
					 link.m_destPreLinkPolyId,
					 "Destination pre-link poly in %s is not valid\n",
					 pDestMesh->GetName());
		return false;
	}

	if (destPreLinkPoly.GetVertexCount() != 4)
	{
		NavLinkError(pDestMesh,
					 link.m_destPreLinkPolyId,
					 "Destination polygon in %s is not a quad\n",
					 pDestMesh->GetName());
		return false;
	}

	bool valid = false;

	// transform dest pre link poly verts into this mesh's local space
	// also find the index of the vertex of the dest pre poly that is the best match for poly vert 0
	Point destVerts[4];

	U32F iDestV0 = 4;
	const float kSnapDist2 = Sqr(0.05f); // needs to be within 1cm or so
	float closestDist2	   = kLargeFloat;
	float bestDist2		   = kSnapDist2;

	// find which vertex of the dest poly is closest to vertex 0 of the source poly
	for (U32F iV = 0; iV < 4; ++iV)
	{
		destVerts[iV] = WorldToLocal(pDestMesh->LocalToWorld(destPreLinkPoly.GetVertex(iV)));
		float dist2	  = DistSqr(destVerts[iV], linkPoly.GetVertex(0));
		closestDist2  = Min(closestDist2, dist2);
		if (dist2 < bestDist2)
		{
			iDestV0 = iV;

			bestDist2 = dist2;
		}
	}

	if (iDestV0 < 4) // Found vertext 0
	{
		// if vertex1 is within the threshold,
		const Point destVert2 = destVerts[(iDestV0 + 2) & 3];
		const Point destVert3 = destVerts[(iDestV0 + 3) & 3];
		const float vert2Err2 = DistSqr(linkPoly.GetVertex(2), destVert2);
		const float vert3Err2 = DistSqr(linkPoly.GetVertex(3), destVert3);

		if ((vert2Err2 < kSnapDist2) && (vert3Err2 < kSnapDist2))
		{
			const Point destVert0 = destVerts[(iDestV0 /* + 0*/) & 3];
			const Point destVert1 = destVerts[(iDestV0 + 1) & 3];

			// stomp verts using verts from dest pre link poly
			linkPoly.SetVertexLs(0, destVert0);
			linkPoly.SetVertexLs(1, destVert1);
			linkPoly.SetVertexLs(2, destVert2);
			linkPoly.SetVertexLs(3, destVert3);

			// We need to change the shared edge verts of the pre link poly in this link too.
			NavPoly& preLinkPoly = m_pPolyArray[link.m_srcPreLinkPolyId];
			//ASSERT(preLinkPoly.IsValid());

			for (U32F iV0 = 0; iV0 < preLinkPoly.GetVertexCount(); iV0++)
			{
				const U32F adjPolyId = preLinkPoly.GetAdjacentId(iV0);
				if (adjPolyId == link.m_srcLinkPolyId)
				{
					const U32F iV1 = (iV0 + 1) & 3;

					preLinkPoly.SetVertexLs(iV0, destVert1);
					preLinkPoly.SetVertexLs(iV1, destVert0);

					break;
				}
			}

			const bool isLinkPolyValid = linkPoly.HasLegalShape();
			const bool isPreLinkPolyValid = preLinkPoly.HasLegalShape();

			if (!isLinkPolyValid)
			{
				linkPoly.m_flags.m_error = true;
			}

			if (!isPreLinkPolyValid)
			{
				preLinkPoly.m_flags.m_error = true;
			}

			valid = isLinkPolyValid && isPreLinkPolyValid;
		}
	}

	if (FALSE_IN_FINAL_BUILD(!valid))
	{
		NavLinkError(this,
					 link.m_srcLinkPolyId,
					 "Link distance between %s and %s too large (closest: %fm)",
					 GetName(),
					 pDestMesh->GetName(),
					 Sqrt(closestDist2));
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::DetachLink(U32F iLink)
{
	NavMeshLink& link	= m_linkArray[iLink];
	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMesh* pOtherMesh = const_cast<NavMesh*>(nmMgr.LookupRegisteredNavMesh(link.m_hDestMesh));
	if (!pOtherMesh)
		return;

	NAV_ASSERT(pOtherMesh != this);

	const U32F polyCount	  = GetPolyCount();
	const U32F otherPolyCount = pOtherMesh->GetPolyCount();

	for (U32F iOtherLink = 0; iOtherLink < pOtherMesh->m_linkCount; ++iOtherLink)
	{
		NavMeshLink& otherLink = pOtherMesh->m_linkArray[iOtherLink];
		if (link.m_linkId != otherLink.m_linkId)
			continue;

		if (link.m_srcPreLinkPolyId < polyCount && link.m_srcLinkPolyId < polyCount
			&& otherLink.m_srcPreLinkPolyId < otherPolyCount && otherLink.m_srcLinkPolyId < otherPolyCount)
		{
			// rewire links
			const NavPoly& srcPreLinkPoly = GetPoly(link.m_srcPreLinkPolyId);
			const NavPoly& srcLinkPoly	  = GetPoly(link.m_srcLinkPolyId);
			const NavPoly& dstPreLinkPoly = pOtherMesh->GetPoly(otherLink.m_srcPreLinkPolyId);
			const NavPoly& dstLinkPoly	  = pOtherMesh->GetPoly(otherLink.m_srcLinkPolyId);

			if (srcPreLinkPoly.IsValid() && srcLinkPoly.IsValid() && dstPreLinkPoly.IsValid() && dstLinkPoly.IsValid())
			{
				const I8 iSrcEdge = FindSharedEdgeIndex(srcPreLinkPoly, srcLinkPoly);
				const I8 iDstEdge = FindSharedEdgeIndex(dstPreLinkPoly, dstLinkPoly);

				NAV_ASSERT(iSrcEdge != -1);
				NAV_ASSERT(iDstEdge != -1);

				const Point srcEdge0Ps = LocalToParent(srcPreLinkPoly.GetVertex(iSrcEdge));
				const Point srcEdge1Ps = LocalToParent(srcPreLinkPoly.GetNextVertex(iSrcEdge));

				const Point dstEdge0Ps = pOtherMesh->LocalToParent(dstPreLinkPoly.GetVertex(iDstEdge));
				const Point dstEdge1Ps = pOtherMesh->LocalToParent(dstPreLinkPoly.GetNextVertex(iDstEdge));

				g_navPathNodeMgr.RemoveLink(srcPreLinkPoly.GetPathNodeId(), dstPreLinkPoly.GetPathNodeId());
				g_navPathNodeMgr.AddLinkSafe(srcPreLinkPoly.GetPathNodeId(),
											 srcLinkPoly.GetPathNodeId(),
											 srcEdge0Ps,
											 srcEdge1Ps);

				g_navPathNodeMgr.RemoveLink(dstPreLinkPoly.GetPathNodeId(), srcPreLinkPoly.GetPathNodeId());
				g_navPathNodeMgr.AddLinkSafe(dstPreLinkPoly.GetPathNodeId(),
											 dstLinkPoly.GetPathNodeId(),
											 dstEdge0Ps,
											 dstEdge1Ps);
			}
		}

		link.m_hDestMesh	  = nullptr;
		otherLink.m_hDestMesh = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*static*/
DC::StealthVegetationHeight NavMesh::GetStealthVegetationHeight(const EntityDB* pEntityDB)
{
	const EntityDB::Record* pRecord = pEntityDB->GetRecord(SID("stealth-vegetation-height"));
	const StringId64 heightId = pRecord ? pRecord->GetData<StringId64>(INVALID_STRING_ID_64) : INVALID_STRING_ID_64;

	switch (heightId.GetValue())
	{
	case SID_VAL("prone"):
		return DC::kStealthVegetationHeightProne;
	case SID_VAL("crouch"):
		return DC::kStealthVegetationHeightCrouch;
	case SID_VAL("stand"):
		return DC::kStealthVegetationHeightStand;
	}

	return DC::kStealthVegetationHeightInvalid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::Login(Level* pLevel)
{
	PROFILE(Frame, NavMesh_Login);

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	const StringId64 bindSpawnerNameId = GetBindSpawnerNameId();
	const EntitySpawner* pBindSpawner  = (bindSpawnerNameId != INVALID_STRING_ID_64)
											? pLevel->LookupEntitySpawnerByBareNameId(bindSpawnerNameId)
											: nullptr;

	if (kVersionId != m_versionId)
	{
		MsgErr("NavMesh in level '%s' has a version mismatch: got '%d', expected '%d'",
			   pLevel->GetName(),
			   m_versionId,
			   kVersionId);

		MsgConErr("Level '%s' has an out of date nav mesh (ver %d, need %d), please %s\n",
				  pLevel->GetName(),
				  m_versionId,
				  kVersionId,
				  kVersionId > m_versionId ? "rebuild" : "get code");

		return;
	}

	//ASSERT(m_managerId.AsU64() == NavMeshHandle::kInvalidMgrId);
	m_managerId.Invalidate();
	m_levelId = pLevel->GetNameId();

	// the memory at m_pBoundFrame's location has the tools origin on login, so we extract it
	const Point toolsOrigin = *(Point*)m_pBoundFrame;
	const Point origin		= toolsOrigin + (pLevel->GetLoc().Pos() - Point(kZero));

	// now builds a proper bound frame
	m_pBoundFrame = NDI_NEW BoundFrame(origin);

	m_gameData.m_flags.m_registered	   = false;
	m_gameData.m_flags.m_hasErrorPolys = false;
	m_gameData.m_flags.m_noPosts = false;

	m_gameData.m_npcStature = NpcStature::kStand;

	if (!nmMgr.AddNavMesh(this))
	{
		return;
	}

	//ASSERT(m_strName != nullptr);

	const int maxOccupancy		   = GetTagData(SID("max-occupancy"), -1);
	m_gameData.m_maxOccupancyCount = maxOccupancy >= 0 ? U32(maxOccupancy) : U32(~0);
	m_gameData.m_navMeshAreaXz	   = ComputeNavMeshAreaXz(*this);

	m_gameData.m_pOccupancyCount = NDI_NEW NdAtomic64(0);

	//ASSERT(m_managerId.AsU64() != NavMeshHandle::kInvalidMgrId);

	for (U32F iPoly = 0; iPoly < GetPolyCount(); ++iPoly)
	{
		// this has to happen AFTER nav mesh mgr registration but BEFORE path
		// node mgr registration for really really good reasons
		NavPoly& poly = m_pPolyArray[iPoly];
		poly.m_hNavMesh.FromU64(m_managerId.AsU64());
		poly.SetHasExGaps(false);
		poly.m_pRegistrationList = nullptr;

		// TODO: remove after ship :|
		if (poly.m_flags.m_stealth)
		{
			const EntityDB* pEntityDB = GetEntityDBByIndex(poly.m_iEntityDB);
			const DC::StealthVegetationHeight height = GetStealthVegetationHeight(pEntityDB);
			poly.m_flags.m_stealth = (height > DC::kStealthVegetationHeightProne);
		}

		if (!poly.HasLegalShape())
		{
			poly.m_flags.m_error = true;
			m_gameData.m_flags.m_hasErrorPolys = true;
			SpawnNavMeshErrorProcess(LocalToWorld(poly.GetCentroid()),
									 this,
									 iPoly,
									 StringBuilder<256>("Invalid NavPoly [%s]", pLevel->GetName()).c_str());
			continue;
		}

		for (const NavMeshGapRef* pRefToLogin = poly.m_pGapList; pRefToLogin && (pRefToLogin->m_gapIndex >= 0);
			 ++pRefToLogin)
		{
			NAV_ASSERT(pRefToLogin->m_gapIndex < m_numGaps);
			pRefToLogin->m_pGap = &m_pGapArray[pRefToLogin->m_gapIndex];

			pRefToLogin->m_pGap->m_blockageMask0 = Nav::kStaticBlockageMaskAll;
			pRefToLogin->m_pGap->m_blockageMask1 = Nav::kStaticBlockageMaskAll;
		}

		for (U32F iV = 0; iV < NavPoly::kMaxVertexCount; ++iV)
		{
			const U32F iAdj = poly.GetAdjacentId(iV);
			NAV_ASSERT((iAdj < m_polyCount) || (iAdj == NavPoly::kNoAdjacent));
		}

		for (U32F iV = 0; iV < poly.m_vertCount; ++iV)
		{
			for (U32F iVN = 0; iVN < poly.m_vertCount; ++iVN)
			{
				if (iVN == iV)
					continue;

				const Point v0 = poly.GetVertex(iV);
				const Point v1 = poly.GetVertex(iVN);

				if (DistSqr(v1, v0) < NDI_FLT_EPSILON)
				{
					MsgWarn("NavMesh poly '%d' has overlapping verts (%d and %d) Mesh: %s\n",
							(int)iPoly,
							iV,
							iVN,
							GetName());
					poly.m_flags.m_error = true;
				}
			}
		}

		if (!poly.IsValid())
		{
			m_gameData.m_flags.m_hasErrorPolys = true;
			SpawnNavMeshErrorProcess(LocalToWorld(poly.GetCentroid()),
									 this,
									 iPoly,
									 StringBuilder<256>("Invalid NavPoly [%s]", pLevel->GetName()).c_str());
		}
	}

	m_gameData.m_flags.m_pathNodesRegistered = false;

	nmMgr.OnLogin(this, pLevel);

	if (pBindSpawner)
	{
		SpawnerLoginChunkNavMesh* pChunk = NDI_NEW SpawnerLoginChunkNavMesh(this);

		pBindSpawner->AddLoginChunk(pChunk);
	}
	else
	{
		Register();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::Register()
{
	PROFILE(Frame, NavMesh_Register);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();

	// this activates handles
	nmMgr.SetRegistrationEnabled(this, true);

	m_gameData.m_flags.m_pathNodesRegistered = g_navPathNodeMgr.AddNavMesh(this);

	const I32F polyCount = GetPolyCount();
	for (I32F iPoly = 0; iPoly < polyCount; ++iPoly)
	{
		NavPoly& poly		= m_pPolyArray[iPoly];
		poly.m_blockageMask = Nav::kStaticBlockageMaskNone;
	}

	NavMesh* navMeshList[NavMeshMgr::kMaxNavMeshCount];
	const U32F iNavMeshCount = nmMgr.GetNavMeshList_IncludesUnregistered(navMeshList, NavMeshMgr::kMaxNavMeshCount);

	for (U32F iLink = 0; iLink < m_linkCount; ++iLink)
	{
		NavMeshLink& link = m_linkArray[iLink];
		//ASSERT(link.m_hDestMesh.ToNavMesh() == nullptr);
		NavMeshHandle hOtherMesh  = nullptr;
		const char* otherMeshName = "";
		NavMesh* pOtherMesh		  = nullptr;
		NavMeshLink* pOtherLink	  = nullptr;
		U32F iOtherLink = 0;
		// search for the mate to this link
		for (U32F iMesh = 0; iMesh < iNavMeshCount; ++iMesh)
		{
			NavMesh* pMesh = navMeshList[iMesh];
			if (pMesh == this)
			{
				// do not try to link back to ourself
				continue;
			}
			for (U32F ii = 0; ii < pMesh->m_linkCount; ++ii)
			{
				NavMeshLink& otherLink = pMesh->m_linkArray[ii];
				if (link.m_linkId == otherLink.m_linkId)
				{
					// if we have multiple matches,
					if (!hOtherMesh.IsNull())
					{
						NavError("NavMesh::Login: logging in %s (level %s),\n",
								 GetName(),
								 DevKitOnly_StringIdToString(m_levelId));
						NavError("  while matching link table entry-%d (link-id %s) found too many matches:\n",
								 U32(iLink),
								 DevKitOnly_StringIdToStringOrHex(link.m_linkId));
						NavError("    match1: %s link entry %d\n", otherMeshName, U32(iOtherLink));
						NavError("    match2: %s link entry %d\n", pMesh->GetName(), U32(ii));
						NavError("This indicates an error in nav mesh layout.\n");

						if (link.m_srcLinkPolyId != NavPoly::kNoAdjacent)
						{
							m_pPolyArray[link.m_srcLinkPolyId].m_flags.m_error = true;
							m_gameData.m_flags.m_hasErrorPolys = true;

							SpawnNavMeshErrorProcess(LocalToWorld(m_pPolyArray[link.m_srcLinkPolyId].GetCentroid()),
													 this,
													 link.m_srcLinkPolyId,
													 "Illegal NavPoly Link");
						}
					}

					hOtherMesh	  = otherLink.m_hDestMesh;
					otherMeshName = pMesh->GetName();

					// only consider links that are unattached, otherwise navpathnode graph will eventually become corrupt
					if (nmMgr.LookupRegisteredNavMesh(otherLink.m_hDestMesh) == nullptr)
					{
						pOtherMesh = pMesh;
						pOtherLink = &otherLink;
						iOtherLink = ii;
					}
				}
			}
		}

		// if we found the link's mate
		if (pOtherMesh && pOtherMesh->IsRegistered())
		{
			NavMeshLink& otherLink = *pOtherLink;
			// shouldn't be trying to attach a link that has already been attached!
			// navpathnode graph will get messed up if this happens
			NAV_ASSERT(link.m_hDestMesh.ToNavMesh() == nullptr); 
			NAV_ASSERT(otherLink.m_hDestMesh.ToNavMesh() == nullptr); 

			link.m_hDestMesh.FromU64(pOtherMesh->GetManagerId().AsU64());
			link.m_destLinkPolyId	 = otherLink.m_srcLinkPolyId;
			link.m_destPreLinkPolyId = otherLink.m_srcPreLinkPolyId;

			otherLink.m_hDestMesh		  = this;
			otherLink.m_destLinkPolyId	  = link.m_srcLinkPolyId;
			otherLink.m_destPreLinkPolyId = link.m_srcPreLinkPolyId;

			const bool valid1 = ConnectLink(iLink, pOtherMesh);
			const bool valid2 = pOtherMesh->ConnectLink(iOtherLink, this);

			if (valid1 && valid2)
			{
				const NavPoly& srcPreLinkPoly = GetPoly(link.m_srcPreLinkPolyId);
				const NavPoly& srcLinkPoly	  = GetPoly(link.m_srcLinkPolyId);
				const NavPoly& dstPreLinkPoly = pOtherMesh->GetPoly(otherLink.m_srcPreLinkPolyId);
				const NavPoly& dstLinkPoly	  = pOtherMesh->GetPoly(otherLink.m_srcLinkPolyId);

				const I8 iSrcEdge = FindSharedEdgeIndex(srcPreLinkPoly, dstPreLinkPoly);
				const I8 iDstEdge = FindSharedEdgeIndex(dstPreLinkPoly, srcPreLinkPoly);

				NAV_ASSERT(iSrcEdge != -1);
				NAV_ASSERT(iDstEdge != -1);

				const Point srcEdge0Ps = LocalToParent(srcPreLinkPoly.GetVertex(iSrcEdge));
				const Point srcEdge1Ps = LocalToParent(srcPreLinkPoly.GetNextVertex(iSrcEdge));

				const Point dstEdge0Ps = pOtherMesh->LocalToParent(dstPreLinkPoly.GetVertex(iDstEdge));
				const Point dstEdge1Ps = pOtherMesh->LocalToParent(dstPreLinkPoly.GetNextVertex(iDstEdge));

				g_navPathNodeMgr.RemoveLink(srcPreLinkPoly.GetPathNodeId(), srcLinkPoly.GetPathNodeId());
				g_navPathNodeMgr.AddLinkSafe(srcPreLinkPoly.GetPathNodeId(),
											 dstPreLinkPoly.GetPathNodeId(),
											 srcEdge0Ps,
											 srcEdge1Ps);

				g_navPathNodeMgr.RemoveLink(dstPreLinkPoly.GetPathNodeId(), dstLinkPoly.GetPathNodeId());
				g_navPathNodeMgr.AddLinkSafe(dstPreLinkPoly.GetPathNodeId(),
											 srcPreLinkPoly.GetPathNodeId(),
											 dstEdge0Ps,
											 dstEdge1Ps);
			}

			if (!valid1 || !valid2)
			{
				NavError("NavMesh::Login: logging in %s (level %s)\n",
						 GetName(),
						 DevKitOnly_StringIdToString(m_levelId));
				NavError("  link table entry-%d (link-id %s) is bad\n",
						 U32(iLink),
						 DevKitOnly_StringIdToStringOrHex(link.m_linkId));
				if (!valid1)
				{
					NavError("  '%s' failed to connect to '%s'\n", GetName(), pOtherMesh->GetName());
				}
				if (!valid2)
				{
					NavError("  '%s' failed to connect to '%s' (incoming)\n", pOtherMesh->GetName(), GetName());
				}

				m_gameData.m_flags.m_hasErrorPolys = true;

				if (link.m_srcLinkPolyId != NavPoly::kNoAdjacent)
				{
					m_pPolyArray[link.m_srcLinkPolyId].m_flags.m_error = true;
					SpawnNavMeshErrorProcess(LocalToWorld(m_pPolyArray[link.m_srcLinkPolyId].GetCentroid()),
											 this,
											 link.m_srcLinkPolyId,
											 "NavPoly Link Failed");
				}

				DetachLink(iLink);
			}
		}
	}

	ActionPackMgr::Get().RequestUpdateTraversalActionPackLinkages(); // TAPs need updating anytime a new navmesh comes in

	PopulateNavMeshForceFlags();

	if (NavMeshForcesProne())
	{
		m_gameData.m_npcStature = NpcStature::kProne;
	}
	else if (NavMeshForcesCrouch())
	{
		m_gameData.m_npcStature = NpcStature::kCrouch;
	}
	else
	{
		m_gameData.m_npcStature = NpcStature::kStand;
	}

	m_gameData.m_flags.m_noPosts = GetTagData<bool>(SID("no-posts"), false);

	nmMgr.OnRegister(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::PopulateNavMeshForceFlags()
{
	m_gameData.m_flags.m_navMeshForcesSwim = GetTagData(SID("force-npc-swim"), false);
	m_gameData.m_flags.m_navMeshForcesDive = GetTagData(SID("force-npc-dive"), false);
	m_gameData.m_flags.m_navMeshForcesWalk = GetTagData(SID("force-npc-walk"), false);
	m_gameData.m_flags.m_navMeshForcesCrouch = GetTagData(SID("force-npc-crouch"), false);
	m_gameData.m_flags.m_navMeshForcesProne = GetTagData(SID("force-npc-prone"), false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::Unregister()
{
	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	nmMgr.OnUnregister(this);

	g_navPathNodeMgr.Validate();

	// for each poly
	const I32F polyCount = GetPolyCount();
	for (I32F iPoly = 0; iPoly < polyCount; ++iPoly)
	{
		NavPoly& poly = m_pPolyArray[iPoly];
		for (NavPolyEx* pPolyEx = poly.m_pExPolyList; pPolyEx; pPolyEx = pPolyEx->GetNextPolyInList())
		{
			for (U32F iV = 0; iV < pPolyEx->GetVertexCount(); ++iV)
			{
				if (pPolyEx->m_adjPolys[iV].m_navMeshIndex == m_managerId.m_navMeshIndex)
					continue;

				if (NavPolyEx* pAdjPolyEx = const_cast<NavPolyEx*>(NavPolyExHandle(pPolyEx->m_adjPolys[iV])
																	   .ToNavPolyEx()))
				{
					for (U32F iVN = 0; iVN < pAdjPolyEx->GetVertexCount(); ++iVN)
					{
						if (pAdjPolyEx->m_adjPolys[iVN].m_navMeshIndex == m_managerId.m_navMeshIndex)
						{
							pAdjPolyEx->m_adjPolys[iVN].Invalidate();
						}
					}
				}
			}

			g_navPathNodeMgr.RemoveNavPolyEx(pPolyEx);
			pPolyEx->m_id = NavPolyEx::kInvalidPolyExId;
		}

		poly.m_blockageMask = Nav::kStaticBlockageMaskNone;
		poly.DetachExPolys();
	}

	g_navPathNodeMgr.Validate();

	// sever links
	for (U32F iLink = 0; iLink < m_linkCount; ++iLink)
	{
		DetachLink(iLink);
	}

	g_navPathNodeMgr.Validate();

	g_navPathNodeMgr.RemoveNavMesh(this);

	g_navPathNodeMgr.Validate();

	UnregisterAllActionPacks();

	// this invalidates any handles
	nmMgr.SetRegistrationEnabled(this, false);

	ActionPackMgr::Get()
		.RequestUpdateTraversalActionPackLinkages(); // TAPs need updating anytime a new navmesh comes in (or goes away)
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::Logout(Level* pLevel)
{
	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	if (IsRegistered())
	{
		Unregister();
	}

	for (I32F iPoly = 0; iPoly < GetPolyCount(); ++iPoly)
	{
		const NavPoly& poly = m_pPolyArray[iPoly];
		NAV_ASSERT(poly.m_pExPolyList == nullptr);
	}

	nmMgr.OnLogout(this);

	g_navPathNodeMgr.Validate();

	// must sever links before unregister or a rare problem can occur:
	//   an NPC is standing on the pre-link poly of a nav mesh that is being logged out, and when the mesh is
	// unregistered we call NavigatingCharacter::ResetNavMesh() to clear out the mesh pointer in the nav control, but then it finds he is
	// standing on the link poly of an adjacent mesh which is not yet unregistered, and then it finds the link is still valid and puts him
	// back on the now unregistered nav mesh.
	// By severing the links first, we can ensure this won't happen.
	nmMgr.RemoveNavMesh(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::UnregisterAllActionPacks()
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	for (I32F iNavPoly = 0; iNavPoly < GetPolyCount(); ++iNavPoly)
	{
		NavPoly& poly = m_pPolyArray[iNavPoly];

		while (poly.m_pRegistrationList)
		{
			NavPolyHandle hPoly = NavPolyHandle(poly.GetNavManagerId());
			ActionPack* pAp		= poly.m_pRegistrationList;

			if (pAp->IsRegistered())
			{
				NAV_ASSERT(pAp->GetRegisteredNavLocation().GetNavPolyHandle() == hPoly);
				pAp->UnregisterImmediately();
			}
			else
			{
				poly.m_pRegistrationList = nullptr;
			}
		}
	}
}
