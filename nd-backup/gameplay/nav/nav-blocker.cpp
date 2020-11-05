/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nav/nav-blocker.h"

#include "corelib/containers/hashtable.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-error.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/text/stringid-util.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-blocker-mgr.h"
#include "gamelib/gameplay/nav/nav-defines.h"
#include "gamelib/gameplay/nav/nav-mesh-gap.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly-util.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/level/level.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void DynamicNavBlocker::Init(Process* pOwner,
							 const NavPoly* pPoly,
							 const char* sourceFile,
							 U32F sourceLine,
							 const char* sourceFunc)
{
	m_hOwner  = pOwner;
	m_hPoly	  = pPoly;
	m_faction = 0; // default faction is '0' whatever that is
	m_flags	  = 0;
	m_ignoreProcessId		 = 0;
	m_shouldAffectEnemies	 = true;
	m_shouldAffectNonEnemies = true;
	m_blockProcessType = INVALID_STRING_ID_64;

	m_sourceFile = sourceFile;
	m_sourceLine = sourceLine;
	m_sourceFunc = sourceFunc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DynamicNavBlocker::SetQuadFromRadius(float radius)
{
	m_vertsXzLs[0].x = -radius;
	m_vertsXzLs[0].y = -radius;

	m_vertsXzLs[1].x = radius;
	m_vertsXzLs[1].y = -radius;

	m_vertsXzLs[2].x = radius;
	m_vertsXzLs[2].y = radius;

	m_vertsXzLs[3].x = -radius;
	m_vertsXzLs[3].y = radius;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DynamicNavBlocker::SetQuad(const Point* vertexList)
{
	for (I32F i = 0; i < 4; ++i)
	{
		const Point pt = vertexList[i];
		m_vertsXzLs[i].x = pt.X();
		m_vertsXzLs[i].y = pt.Z();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float DynamicNavBlocker::GetBoundingRadius() const
{
	const Scalar d0 = Dist(GetQuadVertex(0), kOrigin);
	const Scalar d1 = Dist(GetQuadVertex(1), kOrigin);
	const Scalar d2 = Dist(GetQuadVertex(2), kOrigin);
	const Scalar d3 = Dist(GetQuadVertex(3), kOrigin);
	const float radius = Max(Max(d0, d1), Max(d2, d3));

	return radius;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool DynamicNavBlocker::ContainsPointPs(Point_arg posPs) const
{
	const Point selfPosPs = GetPosPs();
	const Point posLs = (posPs - selfPosPs) + Point(kOrigin);

	const Point v0 = GetQuadVertex(0);
	const Point v1 = GetQuadVertex(1);
	const Point v2 = GetQuadVertex(2);
	const Point v3 = GetQuadVertex(3);
	const Vector v0ToPt = posLs - v0;
	const Vector v1ToPt = posLs - v1;
	const Vector v2ToPt = posLs - v2;
	const Vector v3ToPt = posLs - v3;
	const Vector v0v1Perp = RotateY90(Vector(v1 - v0));
	const Vector v1v2Perp = RotateY90(Vector(v2 - v1));
	const Vector v2v3Perp = RotateY90(Vector(v3 - v2));
	const Vector v3v0Perp = RotateY90(Vector(v0 - v3));
	const Scalar dot0 = Dot(v0v1Perp, v0ToPt);
	const Scalar dot1 = Dot(v1v2Perp, v1ToPt);
	const Scalar dot2 = Dot(v2v3Perp, v2ToPt);
	const Scalar dot3 = Dot(v3v0Perp, v3ToPt);

	Vec4 vDots(dot0);
	vDots.SetY(dot1);
	vDots.SetZ(dot2);
	vDots.SetW(dot3);

	const bool inside = Simd::AllComponentsGE((VF32)vDots.QuadwordValue(), Simd::VecFromMask(Simd::GetMaskAllZero()));

	return inside;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DynamicNavBlocker::EnterNewParentSpace(const Transform& matOldToNew,
											const Locator& oldParentSpace,
											const Locator& newParentSpace)
{
	Transform mat = matOldToNew;
	mat.SetTranslation(Point(0, 0, 0));
	Point pts[4];
	for (I32F iV = 0; iV < 4; ++iV)
	{
		pts[iV] = GetQuadVertex(iV) * mat;
	}
	SetQuad(pts);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DynamicNavBlocker::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	Locator parentSpace = Locator(kIdentity);
	Color color = kColorYellow;

	if (const NavMesh* pMesh = m_hPoly.ToNavMesh())
	{
		parentSpace = pMesh->GetParentSpace();
	}
	else if (g_navMeshDrawFilter.m_onlyDrawActiveNavBlockers)
	{
		return;
	}
	else if (g_navMeshDrawFilter.m_onlyDrawBlockersForSelectedMeshes && !nmMgr.IsMeshSelected(pMesh->GetNameId()))
	{
		return;
	}
	else
	{
		color = kColorGray;
	}

	if (!DebugSelection::Get().IsProcessOrNoneSelected(m_hOwner) && g_navMeshDrawFilter.m_drawSelectedObjectBlockers)
	{
		return;
	}

	DebugDrawShape(color);

	PrimServerWrapper ps(parentSpace);
	ps.SetLineWidth(3.0f);
	ps.DisableDepthTest();

	const Point posPs = GetPosPs();

	const RenderCamera& cam = GetRenderCamera(0);
	const float distToCam = Dist(cam.m_position, parentSpace.TransformPoint(posPs));

	if (g_navMeshDrawFilter.m_drawNavBlockerNames && (distToCam < 50.0f))
	{
		NavBlockerMgr& nbMgr = NavBlockerMgr::Get();
		const I32F myIndex = nbMgr.GetNavBlockerIndex(this);

		StringBuilder<256> desc;
		const Process* pOwner = m_hOwner.ToProcess();

		if (pOwner)
		{
			desc.append_format("[%s]", pOwner->GetName());
		}
		else
		{
			desc.append_format("<null> [dynamic-%d]", (int)myIndex);
		}

		if (g_navMeshDrawFilter.m_drawNavBlockerSource)
		{
			desc.append_format("\n%s (%s:%d)", m_sourceFunc, m_sourceFile, m_sourceLine);
		}

		ps.DrawString(posPs, desc.c_str(), color, 0.5f);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DynamicNavBlocker::DebugDrawShape(Color color, DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	Locator parentSpace = Locator(kIdentity);

	if (const NavMesh* pMesh = m_hPoly.ToNavMesh())
	{
		parentSpace = pMesh->GetParentSpace();
	}

	PrimServerWrapper ps(parentSpace);
	ps.SetLineWidth(3.0f);
	ps.DisableDepthTest();
	ps.SetDuration(tt);

	const Point posPs = GetPosPs();

	for (I32F i = 0; i < 4; ++i)
	{
		Point pos0 = posPs + (GetQuadVertex(i) - kOrigin);
		Point pos1 = posPs + (GetQuadVertex((i + 1) & 3) - kOrigin);
		ps.DrawLine(pos0, pos1, color);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StaticNavBlocker::Init(Process* pOwner,
							const BoundFrame& loc,
							StringId64 bindSpawnerId,
							StringId64 blockerTagId,
							const ProcessSpawnInfo& spawnInfo,
							const char* sourceFile,
							U32F sourceLine,
							const char* sourceFunc)
{
	m_hOwner = pOwner;
	m_boundFrame = loc;
	m_bindSpawnerId = bindSpawnerId;

	m_pBlockerData = spawnInfo.GetDataPointer<NavMeshBlocker>(blockerTagId);

	BuildNavPolyList(spawnInfo);

	m_enabledRequested	 = false;
	m_enabledActive		 = false;
	m_triangulationError = false;

	m_blockageMask = Nav::kStaticBlockageMaskNone;

	if (m_pBlockerData)
	{
		// Retrieve the blockage data
		m_blockageMask = Nav::BuildStaticBlockageMask(spawnInfo.GetEntityDB());

		RequestEnabled(true);
	}

	m_sourceFile = sourceFile;
	m_sourceLine = sourceLine;
	m_sourceFunc = sourceFunc;

	DebugCheckValidity();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StaticNavBlocker::RequestEnabled(bool enabled)
{
	enabled = enabled && (m_numPolys > 0);

	if (enabled != m_enabledRequested)
	{
		m_enabledRequested = enabled;
	}

	if (m_enabledRequested != m_enabledActive)
	{
		NavBlockerMgr::Get().MarkDirty(this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StaticNavBlocker::ApplyEnabled()
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	NavBlockerMgr& nbMgr = NavBlockerMgr::Get();
	const I32F myIndex = nbMgr.GetNavBlockerIndex(this);

	ASSERT(myIndex >= 0);
	if (myIndex < 0)
		return;

	for (U32F iPoly = 0; iPoly < m_numPolys; ++iPoly)
	{
		const NavManagerId mgrId = m_hPolys[iPoly].GetManagerId();
		NavPoly* pPoly = const_cast<NavPoly*>(m_hPolys[iPoly].ToNavPoly());

		if (!pPoly)
			continue;

		StaticNavBlockerBits curBits;
		curBits.ClearAllBits();

		NavBlockerMgr::StaticBlockageTable::Iterator itr = nbMgr.m_staticBlockageTable.Find(mgrId);
		const bool existing = itr != nbMgr.m_staticBlockageTable.End();

		if (existing)
		{
			curBits = itr->m_data;
		}

		Nav::StaticBlockageMask resultingBlockageMask = Nav::kStaticBlockageMaskNone;
		if (m_enabledRequested)
		{
			curBits.SetBit(myIndex);
			resultingBlockageMask = existing ? (pPoly->GetBlockageMask() | m_blockageMask) : m_blockageMask;
		}
		else
		{
			curBits.ClearBit(myIndex);

			if (!curBits.AreAllBitsClear())
			{
				for (U32 i = 0u; i < kMaxStaticNavBlockerCount; ++i)
				{
					if (curBits.IsBitSet(i))
					{
						if (const StaticNavBlocker* pNavBlocker = nbMgr.GetStaticNavBlocker(i))
						{
							resultingBlockageMask |= pNavBlocker->m_blockageMask;
						}
					}
				}
			}
		}

		pPoly->SetBlockageMask(resultingBlockageMask);

		if (curBits.AreAllBitsClear())
		{
			if (existing)
			{
				nbMgr.m_staticBlockageTable.Erase(itr);
			}
		}
		else if (existing || !nbMgr.m_staticBlockageTable.IsFull())
		{
			nbMgr.m_staticBlockageTable.Set(mgrId, curBits);
		}
		else
		{
			MsgErr("NavBlockerMgr static blockage table overflow! Tell a programmer!\n");
		}
	}

	const Process* pOwner = m_hOwner.ToProcess();
	const StringId64 blockerId = (m_pBlockerData ? m_pBlockerData->m_blockerId : INVALID_STRING_ID_64);
	const StringId64 ownerId   = pOwner ? pOwner->GetUserId() : INVALID_STRING_ID_64;

	if (ownerId || blockerId)
	{
		for (U32F iMesh = 0; iMesh < m_numMeshes; ++iMesh)
		{
			const NavMesh* pMesh = m_hMeshes[iMesh].ToNavMesh();

			if (!pMesh)
				continue;

			const U32F numGaps = pMesh->GetNumGaps();

			for (U32F iGap = 0; iGap < numGaps; ++iGap)
			{
				const NavMeshGap& gap = pMesh->GetGap(iGap);

				if ((ownerId && gap.m_blockerId0 == ownerId) || (blockerId && gap.m_blockerId0 == blockerId))
				{
					gap.m_enabled0 = m_enabledRequested;
					gap.m_blockageMask0 = m_blockageMask;
				}

				if ((ownerId && gap.m_blockerId1 == ownerId) || (blockerId && gap.m_blockerId1 == blockerId))
				{
					gap.m_enabled1		= m_enabledRequested;
					gap.m_blockageMask1 = m_blockageMask;
				}
			}
		}
	}

	m_enabledActive = m_enabledRequested;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsPolyInList(const NavPoly* pPoly, const NavPoly** apPolyList, size_t polyListSize)
{
	PROFILE_ACCUM(IsPolyInList);

	for (U32F i = 0; i < polyListSize; ++i)
	{
		if (apPolyList[i] && apPolyList[i]->GetId() == pPoly->GetId())
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StaticNavBlocker::RefreshNearbyCovers(ApHandleTable& apNavRefreshTable) const
{
	PROFILE_AUTO(Navigation);
	PROFILE_ACCUM(RefreshStaticCoverBlockage);

	static const size_t kPolyListSize = 512;
	const NavPoly* apPolyList[kPolyListSize];
	U32F polyListSize = 0;

	static float kSearchRadius = 0.5f;

	for (U32F iPoly = 0; iPoly < m_numPolys; ++iPoly)
	{
		const NavPoly* pPoly = m_hPolys[iPoly].ToNavPoly();
		if (!pPoly)
			continue;

		if (polyListSize >= kPolyListSize)
			break;

		apPolyList[polyListSize] = pPoly;
		++polyListSize;
	}

	const size_t originalListSize = polyListSize;

	for (U32F iListPoly = 0; iListPoly < originalListSize; ++iListPoly)
	{
		const NavPoly* pPoly = apPolyList[iListPoly];
		if (!pPoly)
			continue;

		if (const NavPolyDistEntry* pDistTable = pPoly->GetPolyDistList())
		{
			const NavMesh* pMesh = pPoly->GetNavMesh();
			const size_t polyCount = pMesh->GetPolyCount();

			for (U32F i = 0; true; ++i)
			{
				if (polyListSize >= kPolyListSize)
					break;

				const float polyDist = pDistTable[i].GetDist();

				if (polyDist > kSearchRadius)
					break;

				const U32F iOtherPoly = pDistTable[i].GetPolyIndex();
				if (iOtherPoly >= polyCount)
					break;

				const NavPoly* pOtherPoly = &pMesh->GetPoly(iOtherPoly);

				if (IsPolyInList(pOtherPoly, apPolyList, polyListSize))
					continue;

				apPolyList[polyListSize] = pOtherPoly;
				++polyListSize;
			}
		}
	}

	bool debugDraw = false;
	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		for (U32F iListPoly = 0; iListPoly < polyListSize; ++iListPoly)
		{
			const NavPoly* pPoly = apPolyList[iListPoly];
			if (!pPoly)
				continue;

			pPoly->DebugDraw(kColorCyanTrans, kColorCyanTrans, 0.1f, Seconds(5.0f));
		}
	}

	const U32F kMaxActionPackSearchCount = 256;
	ActionPack* pApList[kMaxActionPackSearchCount];
	const U32F actionPackCount = GetActionPacksFromPolyList(pApList,
															kMaxActionPackSearchCount,
															apPolyList,
															polyListSize,
															-1,
															false);

	for (U32F iAp = 0; iAp < actionPackCount; ++iAp)
	{
		ActionPack* pAp = (ActionPack*)pApList[iAp];
		if (!pAp)
			continue;

		if (pAp->GetType() == ActionPack::kTraversalActionPack)
		{
			// Door taps are meant to traverse blockers
			TraversalActionPack* pTap = (TraversalActionPack*)pAp;
			if (pTap->IsDoorOpen())
			{
				continue;
			}
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugCross(pAp->GetRegistrationPointWs(), 0.15f, kColorRed, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
		}

		if (!apNavRefreshTable.IsFull())
		{
			apNavRefreshTable.Set(pAp, 1);
		}
		else
		{
			pAp->RefreshNavMeshClearance();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StaticNavBlocker::OnNavMeshRegistered(const NavMesh* pMesh)
{
	if (!m_pBlockerData)
		return;

	const StringId64 ownerId = m_pBlockerData->m_blockerId;

	if (!pMesh || !pMesh->IsKnownStaticBlocker(ownerId))
		return;

	if (pMesh->GetBindSpawnerNameId() != m_bindSpawnerId)
		return;

	const size_t oldPolyCount = m_numPolys;
	const size_t oldMeshCount = m_numMeshes;

	if (m_numMeshes < kMaxNavMeshCount)
	{
		m_hMeshes[m_numMeshes++] = pMesh;
	}

	const Locator& boxParentSpace = m_boundFrame.GetParentSpace();
	const Locator meshLocWs = pMesh->GetOriginWs();

	const Aabb meshBoxLs = pMesh->GetAabbLs();
	Aabb boxInMeshSpace;
	const Locator boxLocWs = m_boundFrame.GetLocatorWs();
	const Locator& boxLocPs = m_boundFrame.GetLocatorPs();

	for (U32F iVert = 0; iVert < m_pBlockerData->m_numVerts; ++iVert)
	{
		const Point vertWs = boxLocWs.TransformPoint(m_pBlockerData->m_aVertsLs[iVert]);
		const Point vertLs = meshLocWs.UntransformPoint(vertWs);

		boxInMeshSpace.IncludePoint(vertLs);
	}

	boxInMeshSpace.Expand(kUnitYAxis, kUnitYAxis);

	if (meshBoxLs.Overlaps(boxInMeshSpace))
	{
		const U32F numPolys = pMesh->GetPolyCount();

		for (U32F iPoly = 0; iPoly < numPolys; ++iPoly)
		{
			const NavPoly& poly = pMesh->GetPoly(iPoly);

			const Point centerLs = poly.GetCentroid();

			if (!boxInMeshSpace.ContainsPoint(centerLs))
				continue;

			const Point centerWs = pMesh->LocalToWorld(centerLs);
			const Point centerPs = boxParentSpace.UntransformPoint(centerWs);

			if (!NavPolyUtil::BlockerContainsPointXzPs(centerPs, m_pBlockerData, boxLocPs))
				continue;

			m_hPolys[m_numPolys] = &poly;

			++m_numPolys;

			if (m_numPolys >= kMaxNavPolyCount)
				break;
		}
	}

	if ((m_numMeshes != oldMeshCount) || (m_numPolys != oldPolyCount))
	{
		ApplyEnabled();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StaticNavBlocker::OnNavMeshUnregistered(const NavMesh* pMesh)
{
	const NavMeshHandle hMesh = pMesh;

	for (I32F iMesh = m_numMeshes - 1; iMesh >= 0; --iMesh)
	{
		if (m_hMeshes[iMesh] == hMesh)
		{
			m_hMeshes[iMesh] = m_hMeshes[m_numMeshes - 1];
			--m_numMeshes;
		}
	}

	NavBlockerMgr& nbMgr = NavBlockerMgr::Get();

	for (I32F iPoly = m_numPolys - 1; iPoly >= 0; --iPoly)
	{
		if (m_hPolys[iPoly].ToNavMeshHandle() == hMesh)
		{
			const NavManagerId mgrId = m_hPolys[iPoly].GetManagerId();

			NavBlockerMgr::StaticBlockageTable::Iterator itr = nbMgr.m_staticBlockageTable.Find(mgrId);

			if (itr != nbMgr.m_staticBlockageTable.End())
			{
				nbMgr.m_staticBlockageTable.Erase(itr);
			}

			m_hPolys[iPoly] = m_hPolys[m_numPolys - 1];
			--m_numPolys;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StaticNavBlocker::BuildNavPolyList(const ProcessSpawnInfo& spawnInfo)
{
	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (m_pBlockerData == nullptr)
		return;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	const U32F numIgnoredMeshes = spawnInfo.GetDataArraySize(SID("ignore-nav-mesh"));
	StringId64* aIgnoredMeshes = NDI_NEW StringId64[numIgnoredMeshes + 1];
	aIgnoredMeshes[numIgnoredMeshes] = INVALID_STRING_ID_64;

	spawnInfo.GetDataArray(SID("ignore-nav-mesh"), aIgnoredMeshes, numIgnoredMeshes, SID("invalid"));

	NavMesh** navMeshes = NDI_NEW NavMesh*[NavMeshMgr::kMaxNavMeshCount];
	const NavPoly** polys = NDI_NEW const NavPoly*[1024];

	const Locator& parentSpace = m_boundFrame.GetParentSpace();
	const Locator& locPs = m_boundFrame.GetLocatorPs();
	const Locator locWs = m_boundFrame.GetLocatorWs();

	const U32F numNavMeshes = nmMgr.GetNavMeshList_IncludesUnregistered(navMeshes, NavMeshMgr::kMaxNavMeshCount);

	const Process* pOwner = m_hOwner.ToProcess();
	const StringId64 ownerId = pOwner ? pOwner->GetUserId() : INVALID_STRING_ID_64;

	m_numPolys	= 0;
	m_numMeshes = 0;

	for (U32F iMesh = 0; iMesh < numNavMeshes; ++iMesh)
	{
		NavMesh* pMesh = navMeshes[iMesh];
		if (!pMesh)
			continue;

		if (IsStringIdInList(pMesh->GetNameId(), aIgnoredMeshes))
			continue;

		const bool match = pMesh->GetBindSpawnerNameId() == m_bindSpawnerId;

		if (!match)
			continue;

		if (!pMesh->IsKnownStaticBlocker(ownerId))
			continue;

		if (m_numMeshes >= kMaxNavMeshCount)
			continue;

		m_hMeshes[m_numMeshes++] = pMesh;

		const Aabb meshBoxLs = pMesh->GetAabbLs();

		Aabb boxInMeshSpace;

		for (U32F iVert = 0; iVert < m_pBlockerData->m_numVerts; ++iVert)
		{
			const Point vertWs = locWs.TransformPoint(m_pBlockerData->m_aVertsLs[iVert]);
			const Point vertLs = pMesh->WorldToLocal(vertWs);

			boxInMeshSpace.IncludePoint(vertLs);
		}

		boxInMeshSpace.Expand(kUnitYAxis, kUnitYAxis);

		if (!boxInMeshSpace.Overlaps(meshBoxLs))
			continue;

		const U32F numPolys = pMesh->GetPolyCount();

		for (U32F iPoly = 0; iPoly < numPolys; ++iPoly)
		{
			const NavPoly* pPoly = &pMesh->GetPoly(iPoly);
			if (!pPoly)
				continue;

			const Point polyCenterLs = pPoly->GetCentroid();

			if (!boxInMeshSpace.ContainsPoint(polyCenterLs))
				continue;

			const Point polyCenterWs = pMesh->LocalToWorld(polyCenterLs);
			const Point polyCenterPs = parentSpace.UntransformPoint(polyCenterWs);

			if (!NavPolyUtil::BlockerContainsPointXzPs(polyCenterPs, m_pBlockerData, locPs))
				continue;

			NavManagerId polyMgrId = pMesh->GetManagerId();
			polyMgrId.m_iPoly = pPoly->GetId();

			m_hPolys[m_numPolys] = NavPolyHandle(polyMgrId);
			++m_numPolys;

			if (m_numPolys >= kMaxNavPolyCount)
				break;
		}

		if (m_numPolys >= kMaxNavPolyCount)
			break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ShouldDebugDraw(StringId64 blockerId)
{
	if (!g_navMeshDrawFilter.m_onlyDrawBlockersForSelectedMeshes)
		return true;

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();

	const NavMesh* apMeshes[NavMeshMgr::kMaxNavMeshCount];
	const U32F numMeshes = nmMgr.GetSelectedNavMeshes(apMeshes, NavMeshMgr::kMaxNavMeshCount);

	if (numMeshes == 0)
	{
		return true;
	}

	bool atLeastOneKnown = false;

	for (U32F iMesh = 0; iMesh < numMeshes; ++iMesh)
	{
		const NavMesh* pMesh = apMeshes[iMesh];
		if (!pMesh)
			continue;

		if (pMesh->IsKnownStaticBlocker(blockerId))
		{
			atLeastOneKnown = true;
			break;
		}
	}

	return atLeastOneKnown;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point StaticNavBlocker::GetCenterWs() const
{
	const size_t numVerts = m_pBlockerData ? m_pBlockerData->m_numVerts : 0;

	Vector midLs = kZero;

	for (int i = 0; i < numVerts; ++i)
	{
		const Point vertLs = m_pBlockerData->m_aVertsLs[i];
		midLs += Vector(vertLs.QuadwordValue());
	}

	if (numVerts > 0)
	{
		midLs = midLs * (1.0f / float(numVerts));
	}

	const Locator locWs = m_boundFrame.GetLocatorWs();
	const Point midWs = locWs.TransformPoint(Point(midLs.QuadwordValue()));

	return midWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StaticNavBlocker::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	NavBlockerMgr& nbMgr = NavBlockerMgr::Get();
	const I32F myIndex = nbMgr.GetNavBlockerIndex(this);
	const bool enabled = IsEnabledActive();

	if (g_navMeshDrawFilter.m_onlyDrawActiveNavBlockers && (!enabled || m_numPolys == 0))
	{
		return;
	}

	if (!m_pBlockerData || !ShouldDebugDraw(m_pBlockerData->m_blockerId))
	{
		return;
	}

	if (!DebugSelection::Get().IsProcessOrNoneSelected(m_hOwner) && g_navMeshDrawFilter.m_drawSelectedObjectBlockers)
	{
		return;
	}

	const Locator locWs = m_boundFrame.GetLocatorWs();

	g_prim.Draw(DebugCoordAxes(locWs, 0.5f, kPrimEnableHiddenLineAlpha));

	float yMinWs = kLargeFloat;
	float yMaxWs = -kLargeFloat;
	for (U32F iV = 0; iV < m_pBlockerData->m_numVerts; ++iV)
	{
		const Point vertLs = m_pBlockerData->m_aVertsLs[iV];
		const Point vertWs = locWs.TransformPoint(vertLs);
		const float vertHeightWs = vertWs.Y();
		yMinWs = Min(yMinWs, vertHeightWs);
		yMaxWs = Max(yMaxWs, vertHeightWs);
	}

	const Color col =  m_triangulationError ? kColorRed : (enabled ? AI::IndexToColor(myIndex) : kColorGray);
	const Color colA = Color(col, 0.33f);
	const float yOffset = float(myIndex % 13) * 0.1f;

	const Point centerWs = GetCenterWs();

	g_prim.Draw(DebugCross(centerWs, 0.1f, col, kPrimEnableHiddenLineAlpha));

	if (true)
	{
		for (U32F iPoly = 0; iPoly < m_numPolys; ++iPoly)
		{
			const NavPoly* pPoly = m_hPolys[iPoly].ToNavPoly();
			if (!pPoly)
				continue;

			//const Point polyPosWs = pPoly->GetNavMesh()->LocalToWorld(pPoly->GetCenter());
			//g_prim.Draw(DebugLine(myPosWs, polyPosWs + Vector(0.0f, yOffset, 0.0f), col, col));

			pPoly->DebugDraw(colA, colA, yOffset);
			pPoly->DebugDrawEdges(col, colA, yOffset);
		}
	}

	StringBuilder<256> desc;
	const Process* pOwner = m_hOwner.ToProcess();

	const RenderCamera& cam = GetRenderCamera(0);
	const float distToCam = Dist(cam.m_position, centerWs);

	if (g_navMeshDrawFilter.m_drawNavBlockerNames && (distToCam < 50.0f))
	{
		if (pOwner)
		{
			desc.append_format("[%s]\n", pOwner->GetName());
		}
		else
		{
			desc.append_format("[static-%d]\n", (int)myIndex);
		}

		if (m_blockageMask != Nav::kStaticBlockageMaskAll)
		{
			desc.append("mask: ");
			Nav::GetStaticBlockageMaskStr(m_blockageMask, &desc);
			desc.append("\n");
		}

		const Level* pLevel = pOwner ? pOwner->GetAssociatedLevel() : nullptr;
		if (pLevel)
		{
			desc.append_format("Level: %s\n", pLevel->GetName());
		}

		desc.append_format("%d polys %s\n", m_numPolys, enabled ? "" : "(DISABLED)");

		if (g_navMeshDrawFilter.m_drawNavBlockerSource)
		{
			desc.append_format("%s (%s:%d)\n", m_sourceFunc, m_sourceFile, m_sourceLine);
		}

		g_prim.Draw(DebugString(centerWs, desc.c_str(), col, 0.65f));
	}

	//const Vector vHalfExtent = Vector(0.0f, (yMax - yMin) * 0.5f, 0.0f);
	const Point posYMinWs = Point(0.0f, yMinWs, 0.0f);
	const Point posYMaxWs = Point(0.0f, yMaxWs, 0.0f);
	const Point posYMidWs = Point(0.0f, (yMaxWs - yMinWs) * 0.5f + yMinWs, 0.0f);

	for (U32F iV = 0; iV < m_pBlockerData->m_numVerts; ++iV)
	{
		const U32F iVNext = (iV + 1) % m_pBlockerData->m_numVerts;

		const Point v0Ls = m_pBlockerData->m_aVertsLs[iV];
		const Point v1Ls = m_pBlockerData->m_aVertsLs[iVNext];

		const Point v0HiWs = PointFromXzAndY(locWs.TransformPoint(v0Ls), posYMaxWs);
		const Point v1HiWs = PointFromXzAndY(locWs.TransformPoint(v1Ls), posYMaxWs);
		const Point v0LowWs = PointFromXzAndY(locWs.TransformPoint(v0Ls), posYMinWs);
		const Point v1LowWs = PointFromXzAndY(locWs.TransformPoint(v1Ls), posYMinWs);

		const Point v0Ws = PointFromXzAndY(locWs.TransformPoint(v0Ls), posYMidWs);
		const Point v1Ws = PointFromXzAndY(locWs.TransformPoint(v1Ls), posYMidWs);

		g_prim.Draw(DebugLine(v0Ws, v1Ws, col, col, 4.0f, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugString(v0Ws, StringBuilder<32>("%d", iV).c_str(), col, 0.5f));

		g_prim.Draw(DebugQuad(v0HiWs,
							  v0LowWs,
							  v1LowWs,
							  v1HiWs,
							  col,
							  kPrimEnableWireframe));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StaticNavBlocker::DebugCheckValidity() const
{
	STRIP_IN_FINAL_BUILD;

	const Locator locWs = m_boundFrame.GetLocatorWs();

	if (!m_pBlockerData)
	{
		return;
	}

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const Locator& parentSpace = m_boundFrame.GetParentSpace();

	const size_t numVerts = m_pBlockerData->m_numVerts;

	Aabb boxWs;
	for (U32F iV = 0; iV < numVerts; ++iV)
	{
		const Point posWs = locWs.TransformPoint(m_pBlockerData->m_aVertsLs[iV]);
		boxWs.IncludePoint(posWs);
	}

	boxWs.Expand(kUnitYAxis, kUnitYAxis);

	FindBestNavMeshParams params;
	params.m_pointWs = boxWs.GetCenter();
	params.m_cullDist = Length(boxWs.GetExtent());
	params.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;

	EngineComponents::GetNavMeshMgr()->FindNavMeshWs(&params);

	if (!params.m_pNavMesh)
		return; // we aren't on a nav mesh

	if (!boxWs.ContainsPoint(params.m_nearestPointWs))
		return; // we aren't on a nav mesh

	if (!m_hOwner.HandleValid())
	{
		StringBuilder<256> buf;
		buf.append_format("Suspicious Static Nav Blocker for spawner %s! No Owner.\n", DevKitOnly_StringIdToString(m_bindSpawnerId));
		MsgErr(buf.c_str());
		SpawnStaticBlockerErrorProcess(this, buf.c_str());
		return;
	}

	const float blockerArea = boxWs.GetSurfaceArea();

	if (blockerArea < SCALAR_LC(0.0000001f))
	{
		StringBuilder<256> buf;
		buf.append_format("Suspicious Static Nav Blocker for spawner %s! Bad bounding box.\n", m_hOwner.ToProcess()->GetName());
		MsgErr(buf.c_str());
		SpawnStaticBlockerErrorProcess(this, buf.c_str());
		return;
	}

#if 0
	if (const NavMesh* pMesh = params.m_pNavMes)
	{
		for (U32F iVert = 0; iVert < numVerts; ++iVert)
		{
			const Point vertLs = m_pBlockerData->m_aVertsLs[iVert];
			const Point vertWs = locWs.TransformPoint(vertLs);

			const Point vertMeshLs = pMesh->WorldToLocal(vertWs);
			if (!params.m_pNavMesh->FindContainingPolyLs(vertMeshLs, 2.0f, false))
				continue;

			const Point vertNextLs = m_pBlockerData->m_aVertsLs[(iVert + 1) % numVerts];
			const Point vertNextWs = locWs.TransformPoint(vertNextLs);

			for (U32F iPoly = 0; iPoly < m_numPolys; ++iPoly)
			{
				const NavMesh* pPolyMesh = m_hPolys[iPoly].ToNavMesh();
				const NavPoly* pPoly = m_hPolys[iPoly].ToNavPoly();

				if (!pPoly || !pPolyMesh)
					continue;

				const Point vertPolyLs = pPolyMesh->WorldToLocal();

				if (pPoly->PolyContainsPointLs(vertPolyLs, 0.1f))
					continue;
			}
		}
	}
#endif

	// see if any of our known polys
	for (U32F iPoly = 0; iPoly < m_numPolys; ++iPoly)
	{
		const NavMesh* pMesh = m_hPolys[iPoly].ToNavMesh();
		const NavPoly* pPoly = m_hPolys[iPoly].ToNavPoly();

		if (!pPoly || !pMesh)
			continue;

		for (U32F iPolyVert = 0; iPolyVert < pPoly->GetVertexCount(); ++iPolyVert)
		{
			const Point polyVertLs = pPoly->GetVertex(iPolyVert);

			for (U32F iVert = 0; iVert < numVerts; ++iVert)
			{
				const U32F iVertNext = (iVert + 1) % numVerts;

				const Point vert0Ls = pMesh->WorldToLocal(locWs.TransformPoint(m_pBlockerData->m_aVertsLs[iVert]));
				const Point vert1Ls = pMesh->WorldToLocal(locWs.TransformPoint(m_pBlockerData->m_aVertsLs[iVertNext]));

				const Point midLs = AveragePos(vert0Ls, vert1Ls);
				const Vector edgeDirLs = AsUnitVectorXz(vert1Ls - vert0Ls, kZero);
				const Vector edgeNormLs = RotateYMinus90(edgeDirLs);

				const float distToEdge = DotXz(polyVertLs - midLs, edgeNormLs);

				if (distToEdge > 0.15f)
				{
					//g_prim.Draw(DebugArrow(pMesh->LocalToWorld(midLs), pMesh->LocalToWorld(edgeNormLs), kColorOrange, 0.5f, kPrimEnableHiddenLineAlpha), Seconds(10.0f));
					//g_prim.Draw(DebugArrow(pMesh->LocalToWorld(midLs), pMesh->LocalToWorld(polyVertLs), kColorPink, 0.5f, kPrimEnableHiddenLineAlpha), Seconds(10.0f));
					//g_prim.Draw(DebugArrow(pMesh->LocalToWorld(vert0Ls), pMesh->LocalToWorld(vert1Ls), kColorGreen, 0.5f, kPrimEnableHiddenLineAlpha), Seconds(10.0f));

					m_triangulationError = true;
					SpawnStaticBlockerErrorProcess(this, "Static Nav Blocker Triangulation Failed");
					return;
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Nav::StaticBlockageMask Nav::BuildStaticBlockageMask(const EntityDB* pDb)
{
	Nav::StaticBlockageMask blockageMask = 0;

	const U32F maxTypes = (U32F)Nav::StaticBlockageType::kCount;
	StringId64 aBlockageTypes[maxTypes];
	const I32F activeFlagCount = pDb ? pDb->GetDataArray(SID("StaticBlockageType"),
														 aBlockageTypes,
														 maxTypes,
														 INVALID_STRING_ID_64)
									 : 0;

	if (activeFlagCount == 0)
	{
		blockageMask = (Nav::kStaticBlockageMaskAll & ~Nav::kStaticBlockageMaskBloater);
	}
	else
	{
		for (int i = 0; i < activeFlagCount; ++i)
		{
			if (aBlockageTypes[i] == SID("None"))
				return kStaticBlockageMaskNone;

			const Nav::StaticBlockageType type = Nav::StaticBlockageTypeFromSid(aBlockageTypes[i]);
			const Nav::StaticBlockageMask mask = Nav::ToStaticBlockageMask(type);

			blockageMask |= mask;
		}
	}

	return blockageMask;
}
