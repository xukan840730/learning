/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/positional-action-pack.h"

#include "corelib/containers/list-array.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/render/util/prim.h"

#include "gamelib/feature/feature-db-ref.h"
#include "gamelib/feature/feature-db.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/level/level.h"
#include "gamelib/ndphys/moving-sphere.h"

const U32 kRegistrationProbeVectorCount = 3;

/// --------------------------------------------------------------------------------------------------------------- ///
static U32F GetRegistrationProbeVectorsLs(Vector probeVectorsLsOut[kRegistrationProbeVectorCount])
{
	Scalar probeDist = 0.85f;
	Scalar probeLDist = 0.25f;
	Scalar probeRDist = 0.25f;

	const Scalar zero = kZero;
	probeVectorsLsOut[0] = Vector(zero, zero, -probeDist);
	probeVectorsLsOut[1] = Vector(probeLDist, zero, zero);
	probeVectorsLsOut[2] = Vector(-probeRDist, zero, zero);

	return 3;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Class PositionalActionPack.
/// --------------------------------------------------------------------------------------------------------------- ///
PositionalActionPack::PositionalActionPack(const BoundFrame& loc,
										   const EntitySpawner* pSpawner)
	: ActionPack(kPositionalActionPack, POINT_LC(0.0f, 0.0f, -0.3f), loc, pSpawner), m_numPosts(0)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
PositionalActionPack::PositionalActionPack(const BoundFrame& loc,
								 F32 registrationDist,
								 const Level* pLevel)
	: ActionPack(kPositionalActionPack, Point(0.0f, 0.0f, -registrationDist), loc, pLevel), m_numPosts(0)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PositionalActionPack::AddPostId(U16 postId)
{
	NAV_ASSERTF(m_numPosts < kMaxPostsForPositionalActionPack,
				("Tried to add more than %d posts for PAP '%s', Try reducing the PAP radius?",
				 kMaxPostsForPositionalActionPack,
				 DevKitOnly_StringIdToString(GetSpawnerId())));

	m_postIds[m_numPosts] = postId;
	m_numPosts++;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PositionalActionPack::ClearPostIds()
{
	m_numPosts = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPoly* PositionalActionPack::FindRegisterNavPoly(const BoundFrame& apLoc,
													const ActionPackRegistrationParams& params,
													Point* pNavPolyPointWs /* = nullptr */,
													const NavMesh** ppNavMeshOut /* = nullptr */,
													const NavPoly** ppNavPolyOut /* = nullptr */)
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	const Level* pAllocLevel = params.m_pAllocLevel;

	const Point regPosWs = apLoc.GetLocatorWs().TransformPoint(params.m_regPtLs);

	FindBestNavMeshParams findMesh;
	findMesh.m_pointWs = regPosWs;
	findMesh.m_bindSpawnerNameId = params.m_bindId;
	findMesh.m_yThreshold		 = params.m_yThreshold;
	findMesh.m_cullDist		   = 0.0f;
	findMesh.m_requiredLevelId = pAllocLevel ? pAllocLevel->GetNameId() : INVALID_STRING_ID_64;

	if (pAllocLevel)
	{
		pAllocLevel->FindNavMesh(&findMesh);
	}

	if (!findMesh.m_pNavPoly && !pAllocLevel)
	{
		nmMgr.FindNavMeshWs(&findMesh);
	}

	if (!findMesh.m_pNavPoly)
	{
		return nullptr;
	}

	const NavMesh* pNavMesh = findMesh.m_pNavMesh;
	const Point startPs = pNavMesh->WorldToParent(findMesh.m_nearestPointWs);

	if (pNavPolyPointWs)
		*pNavPolyPointWs = findMesh.m_nearestPointWs;

	if (ppNavMeshOut)
		*ppNavMeshOut = pNavMesh;

	if (ppNavPolyOut)
		*ppNavPolyOut = findMesh.m_pNavPoly;

	return findMesh.m_pNavPoly;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PositionalActionPack::RegisterInternal()
{
	const bool readOnly = m_regParams.m_readOnly;

	NAV_ASSERT(readOnly ? NavMeshMgr::GetGlobalLock()->IsLocked() : NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	const NavPoly* pPoly = FindRegisterNavPoly(GetBoundFrame(), m_regParams);

	if (!pPoly)
	{
		return false;
	}

	if (readOnly)
	{
		m_hRegisteredNavLoc.SetWs(GetRegistrationPointWs(), pPoly);
	}
	else
	{
		RegisterSelfToNavPoly(pPoly);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PositionalActionPack::DebugDraw(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	StringBuilder<256> desc;
	Color descColor = IsRegistered() ? kColorWhite : kColorGrayTrans;

	if (g_navMeshDrawFilter.m_drawApDetail)
	{
		desc.append_format(" MgrId: 0x%x", GetMgrId());
	}

	if (!IsEnabled())
	{
		desc.append_format(" [disabled]");
		descColor = kColorGray;
	}

	if (const NavCharacter* pBlockingNavChar = GetBlockingNavChar())
	{
		const float timeSince = ToSeconds(pBlockingNavChar->GetClock()->GetTimePassed(m_navCharBlockTime));
		desc.append_format(" Blocked by %s %0.1fsec ago", pBlockingNavChar->GetName(), timeSince);
	}

	const Point regPosWs = GetRegistrationPointWs();
	const Locator locWs = GetLocatorWs();

	g_prim.Draw(DebugSphere(regPosWs, 0.05f, kColorMagentaTrans), tt);
	g_prim.Draw(DebugString(regPosWs, desc.c_str(), descColor, 0.5f), tt);
	g_prim.Draw(DebugCoordAxes(locWs, 0.3f, kPrimEnableHiddenLineAlpha), tt);
	g_prim.Draw(DebugLine(locWs.Pos(), regPosWs, kColorWhite, kColorMagenta, 2.0f, kPrimEnableHiddenLineAlpha), tt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PositionalActionPack::DebugDrawRegistrationProbes() const
{
	STRIP_IN_FINAL_BUILD;
	const Locator locWs = GetLocatorWs();
	Vector probesLs[kRegistrationProbeVectorCount];
	U32F probeCount = GetRegistrationProbeVectorsLs(probesLs);
	for (U32F iProbe = 0; iProbe < probeCount; ++iProbe)
	{
		Vector probeVecWs = locWs.TransformVector(probesLs[iProbe]);
		Point posWs = locWs.Pos() + Vector(kUnitYAxis)*0.05f; // lift off the ground a bit to improve visibility
		g_prim.Draw(DebugLine(posWs, posWs + probeVecWs, kColorBlack, kColorWhite, 3.0f));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PositionalActionPack::IsAvailableFor(const Process* pNavChar) const
{
	const NavCharacter* pBlockingChar = GetBlockingNavChar();
	if (pBlockingChar && pNavChar && (pBlockingChar != pNavChar))
	{
		return false;
	}

	if (ActionPack::IsAvailableFor(pNavChar))
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PositionalActionPack::TryAddBlockingNavChar(NavCharacter* pBlockingNavChar)
{
	m_hBlockingNavChar = pBlockingNavChar;
	m_navCharBlockTime = pBlockingNavChar ? pBlockingNavChar->GetCurTime() : TimeFrameNegInfinity();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavCharacter* PositionalActionPack::GetBlockingNavChar() const
{
	const NavCharacter* pBlockingNavChar = m_hBlockingNavChar.ToProcess();
	if (!pBlockingNavChar)
		return nullptr;

	const Clock* pClock = pBlockingNavChar->GetClock();
	if (pClock->TimePassed(Seconds(0.25f), m_navCharBlockTime))
		return nullptr;

	return pBlockingNavChar;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PositionalActionPack::CanRegisterSelf(const ActionPackRegistrationParams& params,
										   const BoundFrame& apLoc,
										   const NavMesh** ppNavMeshOut /* = nullptr */,
									       const NavPoly** ppNavPolyOut /* = nullptr */)
{
	return FindRegisterNavPoly(apLoc, params, nullptr, ppNavMeshOut, ppNavPolyOut) != nullptr;
}
