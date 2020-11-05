/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/cover-action-pack.h"

#include "corelib/containers/list-array.h"
#include "corelib/math/intersection.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h" // for Swap()

#include "ndlib/profiling/profiling.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/util/screen-space-text-printer.h" // GraphDisplay need to be split into 'filter' and 'draw' modules.

#include "gamelib/feature/feature-db-ref.h"
#include "gamelib/feature/feature-db.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nav/cover-frustum-angles.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/level/level.h"
#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"
#include "gamelib/ndphys/moving-sphere.h"

/// --------------------------------------------------------------------------------------------------------------- ///
CONST_EXPR float kEdgeLinkStartDotPThreshold = 0.95f;
CONST_EXPR float kEdgeLinkWalkDotPThreshold = 0.85f;
CONST_EXPR float kCoverFeatureSearchRadius = 0.75f;
CONST_EXPR float kCoverSideHeight = 0.6f;
CONST_EXPR float kCoverLinkSearchRadius = 1.6f;

CONST_EXPR float kCoverSideIdleOffset = 0.1f;
CONST_EXPR float kCoverSideFireOffset = 0.3f;

/// --------------------------------------------------------------------------------------------------------------- ///
class CoverBlockingBodyCollector : public HavokAabbQueryCollector
{
public:
	ActionPackHandle m_hAp;
	CoverActionPack::BlockingDirection m_blockingDir = CoverActionPack::kBlockingCount;

	virtual bool AddBody(RigidBody* pBody)
	{
		if (!pBody->IsBreakable())
		{
			return true;
		}

		NdGameObject* pGo = pBody->GetOwner();
		if (!pGo || !pGo->GetApBlockerInterface())
		{
			return true;
		}

		if (ActionPack* pAp = m_hAp.ToActionPack())
		{
			if (pAp->CheckRigidBodyIsBlocking(pBody, m_blockingDir))
			{
				return false;
			}
		}

		return true;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPackDebugDrawFrustum(const CoverActionPack& ap,
									 const CoverFrustum& frustum,
									 Scalar_arg apRegPtDist,
									 Scalar_arg cullDist,
									 const Color colors[4],
									 DebugPrimTime tt);

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ProbeForCornerCheck(const CoverActionPack* pAp, bool debugDraw, DebugPrimTime tt = kPrimDuration1FrameAuto)
{
	const CoverDefinition& coverDef = pAp->GetDefinition();
	const NavMesh* pMesh = pAp->GetRegisteredNavLocation().ToNavMesh();

	if (!pMesh)
		return false;

	static const F32 kProbeWideDist = 1.75f;
	static const F32 kProbeWidth	= 0.25f;

	Point probeStartWs;
	Point probeEndWs;

	{
		// AP local space here
		const Point probeStartLs = Point(0.f, 0.f, -kProbeWideDist);
		Point probeEndLs;

		switch (coverDef.m_coverType)
		{
		case CoverDefinition::kCoverCrouchLeft:
		case CoverDefinition::kCoverStandLeft:
			probeEndLs = Point(kProbeWideDist, 0.f, 0.f);
			break;

		case CoverDefinition::kCoverCrouchRight:
		case CoverDefinition::kCoverStandRight:
			probeEndLs = Point(-kProbeWideDist, 0.f, 0.f);
			break;

		case CoverDefinition::kCoverCrouchOver:
			return false;

		default:
			AI_HALTF(("Invalid cover AP type %d", coverDef.m_coverType));
			return false;
		}

		const BoundFrame& bf = pAp->GetBoundFrame();
		const Locator locWs	 = bf.GetLocatorWs();

		probeStartWs = locWs.TransformPoint(probeStartLs);
		probeEndWs	 = locWs.TransformPoint(probeEndLs);
	}

	// NavMesh local space here
	const Point probeStartLs = pMesh->WorldToLocal(probeStartWs);
	const Point probeEndLs	 = pMesh->WorldToLocal(probeEndWs);

	const NavPoly* pPoly = pMesh->FindContainingPolyLs(probeStartLs);
	if (!pPoly)
	{
		const Vector vo = Vector(0.0f, 0.1f, 0.0f);
		g_prim.Draw(DebugArrow(probeStartWs, probeEndWs, kColorRed));
		return false;
	}

	NavMesh::ProbeParams probeParams;
	probeParams.m_start		  = probeStartLs;
	probeParams.m_move		  = probeEndLs - probeStartLs;
	probeParams.m_probeRadius = kProbeWidth;
	probeParams.m_pStartPoly  = pPoly;
	probeParams.m_debugDraw	  = FALSE_IN_FINAL_BUILD(debugDraw);

	const NavMesh::ProbeResult res = pMesh->ProbeLs(&probeParams);

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		const Vector vo		 = Vector(0.0f, 0.1f, 0.0f);
		PrimServerWrapper ps = PrimServerWrapper(pMesh->GetOriginWs());

		ps.SetDuration(tt);
		ps.EnableHiddenLineAlpha();

		if (res == NavMesh::ProbeResult::kReachedGoal)
		{
			ps.DrawArrow(probeStartLs + vo, probeEndLs + vo, 0.5f, kColorCyanTrans);
		}
		else
		{
			if (res == NavMesh::ProbeResult::kHitEdge)
			{
				ps.DrawLine(probeStartLs + vo, probeEndLs + vo, kColorOrangeTrans);
				ps.DrawArrow(probeStartLs + vo, probeEndLs + vo, 0.5f, kColorOrange);
				ps.DrawCross(probeParams.m_impactPoint + vo, 0.2f, kColorOrange);
			}
			else
			{
				ps.DrawCross(probeStartLs + vo, 0.1f, kColorOrange);
				ps.DrawArrow(probeStartLs + vo, probeEndLs + vo, 0.5f, kColorOrange);
			}
		}
	}

	return (res == NavMesh::ProbeResult::kReachedGoal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ProbeForCanStepOut(const CoverDefinition& coverDef,
							   const NavMesh* pMesh,
							   const NavPoly* pPoly,
							   const Locator& locWs,
							   Point_arg probeStartWs,
							   const Nav::StaticBlockageMask obeyedStaticBlockers,
							   bool debugDraw,
							   DebugPrimTime tt = kPrimDuration1FrameAuto)
{
	if (!pMesh || !pPoly)
		return false;

	static const float kProbeStepOutDist = 0.75f;

	U32F numProbeVecs = 0;
	Vector probeVecsLs[3];

	switch (coverDef.m_coverType)
	{
	case CoverDefinition::kCoverCrouchLeft:
	case CoverDefinition::kCoverStandLeft:
		probeVecsLs[numProbeVecs++] = Vector(kProbeStepOutDist, 0.0f, 0.0f);
		break;

	case CoverDefinition::kCoverCrouchRight:
	case CoverDefinition::kCoverStandRight:
		probeVecsLs[numProbeVecs++] = Vector(-kProbeStepOutDist, 0.0f, 0.0f);
		break;

	case CoverDefinition::kCoverCrouchOver:
		probeVecsLs[numProbeVecs++] = Vector(-kProbeStepOutDist, 0.0f, 0.0f);
		probeVecsLs[numProbeVecs++] = Vector(kProbeStepOutDist, 0.0f, 0.0f);
		break;

	default:
		ASSERT(false);
		return false;
	}

	bool valid = true;

	NavMesh::ProbeParams probeParams;
	probeParams.m_start = pMesh->WorldToLocal(probeStartWs);
	probeParams.m_pStartPoly = pPoly;
	probeParams.m_obeyedStaticBlockers = obeyedStaticBlockers;

	for (U32F i = 0; i < numProbeVecs; ++i)
	{
		probeParams.m_move = pMesh->WorldToLocal(locWs.TransformVector(probeVecsLs[i]));

		const NavMesh::ProbeResult res = pMesh->ProbeLs(&probeParams);

		if (res != NavMesh::ProbeResult::kReachedGoal)
		{
			valid = false;
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			const Vector vo = Vector(0.0f, 0.15f, 0.0f);
			const Point impactPointWs = pMesh->LocalToWorld(probeParams.m_impactPoint);

			if (res == NavMesh::ProbeResult::kReachedGoal)
			{
				g_prim.Draw(DebugArrow(probeStartWs + vo, probeParams.m_move, kColorCyanTrans, 0.5f, kPrimEnableHiddenLineAlpha), tt);
			}
			else
			{
				if (res == NavMesh::ProbeResult::kHitEdge)
				{
					g_prim.Draw(DebugLine(probeStartWs + vo, probeParams.m_move, kColorOrangeTrans, kPrimEnableHiddenLineAlpha), tt);
					g_prim.Draw(DebugArrow(probeStartWs + vo, impactPointWs + vo, kColorOrange, 0.5f, kPrimEnableHiddenLineAlpha), tt);
					g_prim.Draw(DebugCross(impactPointWs + vo, 0.2f, kColorOrange, kPrimEnableHiddenLineAlpha), tt);
				}
				else
				{
					g_prim.Draw(DebugCross(probeStartWs + vo, 0.1f, kColorOrange, kPrimEnableHiddenLineAlpha), tt);
					g_prim.Draw(DebugArrow(probeStartWs + vo, probeParams.m_move, kColorOrange, 0.5f, kPrimEnableHiddenLineAlpha), tt);
				}
			}
		}
		else if (res != NavMesh::ProbeResult::kReachedGoal)
		{
			break;
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ProbeForRegistration(const CoverDefinition& coverDef,
								 const NavMesh* pMesh,
								 const NavPoly* pPoly,
								 const Locator locWs,
								 Point_arg probeStartWs,
								 Nav::StaticBlockageMask obeyedStaticBlockers,
								 bool debugDraw,
								 DebugPrimTime tt = kPrimDuration1FrameAuto)
{
	if (!pMesh || !pPoly)
		return false;

	const float regProbeDist = 0.85f;
	const float cornerProbeDist = 0.5f;
	const float centerProbeDist = 0.25f;

	U32F numProbeVecs = 0;
	Vector probeVecsLs[4];
	probeVecsLs[numProbeVecs++] = Vector(0.0f, 0.0f, -regProbeDist);

	switch (coverDef.m_coverType)
	{
	case CoverDefinition::kCoverCrouchLeft:
	case CoverDefinition::kCoverStandLeft:
		probeVecsLs[numProbeVecs++] = Vector(-cornerProbeDist, 0.0f, 0.0f);
		break;

	case CoverDefinition::kCoverCrouchRight:
	case CoverDefinition::kCoverStandRight:
		probeVecsLs[numProbeVecs++] = Vector(cornerProbeDist, 0.0f, 0.0f);
		break;

	case CoverDefinition::kCoverCrouchOver:
		probeVecsLs[numProbeVecs++] = Vector(-centerProbeDist, 0.0f, 0.0f);
		probeVecsLs[numProbeVecs++] = Vector(centerProbeDist, 0.0f, 0.0f);
		break;

	default:
		ASSERT(false);
		return false;
	}

	bool valid = true;

	NavMesh::ProbeParams probeParams;
	probeParams.m_start = pMesh->WorldToLocal(probeStartWs);
	probeParams.m_pStartPoly = pPoly;
	probeParams.m_obeyedStaticBlockers = obeyedStaticBlockers;

	for (U32F i = 0; i < numProbeVecs; ++i)
	{
		probeParams.m_move = pMesh->WorldToLocal(locWs.TransformVector(probeVecsLs[i]));

		const NavMesh::ProbeResult res = pMesh->ProbeLs(&probeParams);

		if (res != NavMesh::ProbeResult::kReachedGoal)
		{
			valid = false;
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			const Vector vo = Vector(0.0f, 0.1f, 0.0f);
			const Point impactPointWs = pMesh->LocalToWorld(probeParams.m_impactPoint);
			const Vector probeMoveWs = pMesh->LocalToWorld(probeParams.m_move);

			if (res == NavMesh::ProbeResult::kReachedGoal)
			{
				g_prim.Draw(DebugArrow(probeStartWs + vo, probeParams.m_move, kColorGreenTrans, 0.5f, kPrimEnableHiddenLineAlpha), tt);
			}
			else
			{
				g_prim.Draw(DebugCoordAxes(locWs), tt);

				pPoly->DebugDrawEdges(kColorWhiteTrans, kColorRed, 0.0f, 3.0f, tt);

				if (res == NavMesh::ProbeResult::kHitEdge)
				{
					g_prim.Draw(DebugArrow(probeStartWs + vo, impactPointWs + vo, kColorRed, 0.5f, kPrimEnableHiddenLineAlpha), tt);
					g_prim.Draw(DebugCross(impactPointWs + vo, 0.2f, kColorRed, kPrimEnableHiddenLineAlpha), tt);
					g_prim.Draw(DebugLine(probeStartWs + vo, probeMoveWs, kColorRedTrans, kPrimEnableHiddenLineAlpha), tt);
				}
				else
				{
					g_prim.Draw(DebugCross(probeStartWs + vo, 0.1f, kColorRed, kPrimEnableHiddenLineAlpha), tt);
					g_prim.Draw(DebugArrow(probeStartWs + vo, probeMoveWs, kColorRed, 0.5f, kPrimEnableHiddenLineAlpha), tt);
				}
			}
		}
		else if (res != NavMesh::ProbeResult::kReachedGoal)
		{
			break;
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverFrustum::DebugDraw(Point_arg frustumPosWs, F32 cullDist, const Color colors[4], DebugPrimTime tt) const
{
	STRIP_IN_FINAL_BUILD;

	const RenderCamera& cam = GetRenderCamera(0);
	if (!cam.IsSphereInFrustum(Sphere(frustumPosWs, 2.0f)) || Dist(cam.m_position, frustumPosWs) > cullDist)
	{
		return;
	}

	const Vector leftPlaneWs  = m_planeNormLeftWs;
	const Vector rightPlaneWs = m_planeNormRightWs;
	const Vector lowerPlaneWs = m_planeNormLowerWs;
	const Vector upperPlaneWs = m_planeNormUpperWs;

	Vector vCornerDir[4];
	vCornerDir[0] = 0.7f * SafeNormalize(Cross(lowerPlaneWs, leftPlaneWs), kZero);
	vCornerDir[1] = 0.7f * SafeNormalize(Cross(rightPlaneWs, lowerPlaneWs), kZero);
	vCornerDir[2] = 0.7f * SafeNormalize(Cross(upperPlaneWs, rightPlaneWs), kZero);
	vCornerDir[3] = 0.7f * SafeNormalize(Cross(leftPlaneWs, upperPlaneWs), kZero);

	Vector vMidDir[4];
	vMidDir[0] = 0.7f * SafeNormalize(vCornerDir[3] + vCornerDir[0], kZero); // left
	vMidDir[1] = 0.7f * SafeNormalize(vCornerDir[0] + vCornerDir[1], kZero); // lower
	vMidDir[2] = 0.7f * SafeNormalize(vCornerDir[1] + vCornerDir[2], kZero); // right
	vMidDir[3] = 0.7f * SafeNormalize(vCornerDir[2] + vCornerDir[3], kZero); // upper

	PrimAttrib attrib(kPrimEnableHiddenLineAlpha, kPrimEnableWireframe);
	g_prim.Draw(DebugTriangle(frustumPosWs, frustumPosWs + vMidDir[0], frustumPosWs + vCornerDir[3], colors[0], attrib), tt);
	g_prim.Draw(DebugTriangle(frustumPosWs, frustumPosWs + vMidDir[0], frustumPosWs + vCornerDir[0], colors[0], attrib), tt);
	g_prim.Draw(DebugTriangle(frustumPosWs, frustumPosWs + vMidDir[1], frustumPosWs + vCornerDir[0], colors[1], attrib), tt);
	g_prim.Draw(DebugTriangle(frustumPosWs, frustumPosWs + vMidDir[1], frustumPosWs + vCornerDir[1], colors[1], attrib), tt);
	g_prim.Draw(DebugTriangle(frustumPosWs, frustumPosWs + vMidDir[2], frustumPosWs + vCornerDir[1], colors[2], attrib), tt);
	g_prim.Draw(DebugTriangle(frustumPosWs, frustumPosWs + vMidDir[2], frustumPosWs + vCornerDir[2], colors[2], attrib), tt);
	g_prim.Draw(DebugTriangle(frustumPosWs, frustumPosWs + vMidDir[3], frustumPosWs + vCornerDir[2], colors[3], attrib), tt);
	g_prim.Draw(DebugTriangle(frustumPosWs, frustumPosWs + vMidDir[3], frustumPosWs + vCornerDir[3], colors[3], attrib), tt);

	g_prim.Draw(DebugLine(frustumPosWs+vMidDir[0], leftPlaneWs*0.1f,   kColorBlack, colors[0], 2.0f, attrib), tt);
	g_prim.Draw(DebugLine(frustumPosWs+vMidDir[1], lowerPlaneWs*0.1f,  kColorBlack, colors[1], 2.0f, attrib), tt);
	g_prim.Draw(DebugLine(frustumPosWs+vMidDir[2], rightPlaneWs*0.1f,  kColorBlack, colors[2], 2.0f, attrib), tt);
	g_prim.Draw(DebugLine(frustumPosWs+vMidDir[3], upperPlaneWs*0.1f,  kColorBlack, colors[3], 2.0f, attrib), tt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Class CoverActionPack.
/// --------------------------------------------------------------------------------------------------------------- ///
CoverActionPack::CoverActionPack(const BoundFrame& loc,
								 const EntitySpawner* pSpawner,
								 const CoverDefinition& coverDef,
								 I32 bodyJoint)
	: ActionPack(kCoverActionPack, POINT_LC(0.0f, 0.0f, -0.3f), loc, pSpawner)
	, m_definition(coverDef)
	, m_bodyJoint(bodyJoint)
	, m_hasCoverFeatureVerts(false)
	, m_hasProtectionFrustum(false)
	, m_doorIsBlocking(false)
	, m_numNearbyCoverAps(0)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
CoverActionPack::CoverActionPack(const BoundFrame& loc,
								 F32 registrationDist,
								 const Level* pLevel,
								 const CoverDefinition& coverDef,
								 I32 bodyJoint)
	: ActionPack(kCoverActionPack, Point(0.0f, 0.0f, -registrationDist), loc, pLevel)
	, m_definition(coverDef)
	, m_bodyJoint(bodyJoint)
	, m_hasCoverFeatureVerts(false)
	, m_hasProtectionFrustum(false)
	, m_doorIsBlocking(false)
	, m_numNearbyCoverAps(0)
{
}

CoverActionPack::CoverActionPack(const BoundFrame& apLoc, F32 registrationDist, const Level* pAllocLevel, const Level* pRegLevel, const CoverDefinition& cover, I32 bodyJoint)
	: ActionPack(kCoverActionPack, Point(0.0f, 0.0f, -registrationDist), apLoc, pAllocLevel, pRegLevel)
	, m_definition(cover)
	, m_bodyJoint(bodyJoint)
	, m_hasCoverFeatureVerts(false)
	, m_hasProtectionFrustum(false)
	, m_doorIsBlocking(false)
	, m_numNearbyCoverAps(0)
{}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::Reset()
{
	m_definition.m_costUpdateTime = Seconds(0);
	ActionPack::Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
const NavPoly* CoverActionPack::FindRegisterNavPoly(const BoundFrame& apLoc,
													const CoverDefinition& coverDef,
													const ActionPackRegistrationParams& params,
													Point* pNavPolyPointWs,
													const NavMesh** ppNavMeshOut,
													bool debugDraw)
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	const Level* pAllocLevel = params.m_pAllocLevel;

	const Point regPosWs = apLoc.GetLocatorWs().TransformPoint(params.m_regPtLs);

	FindBestNavMeshParams findMesh;
	findMesh.m_pointWs = regPosWs;
	findMesh.m_bindSpawnerNameId = params.m_bindId;
	findMesh.m_yThreshold = params.m_yThreshold;
	findMesh.m_cullDist = 0.0f;
	findMesh.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;
	findMesh.m_requiredLevelId = pAllocLevel ? pAllocLevel->GetNameId() : INVALID_STRING_ID_64;

	if (pAllocLevel)
	{
		pAllocLevel->FindNavMesh(&findMesh);
	}

	if (!findMesh.m_pNavPoly)
	{
		nmMgr.FindNavMeshWs(&findMesh);
	}

	if (!findMesh.m_pNavPoly)
	{
		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugCross(findMesh.m_pointWs, 0.25f, kColorRed), Seconds(15.0f));
			g_prim.Draw(DebugSphere(findMesh.m_pointWs, 0.25f, kColorRedTrans), Seconds(15.0f));
		}

		return nullptr;
	}

	if (findMesh.m_pNavMesh->GetTagData(SID("no-cover-aps"), false))
	{
		return nullptr;
	}

	const Locator locWs = apLoc.GetLocatorWs();

	if (!ProbeForRegistration(coverDef, findMesh.m_pNavMesh, findMesh.m_pNavPoly, locWs, findMesh.m_nearestPointWs, Nav::kStaticBlockageMaskNone, debugDraw))
	{
		return nullptr;
	}

	if (pNavPolyPointWs)
		*pNavPolyPointWs = findMesh.m_nearestPointWs;

	if (ppNavMeshOut)
		*ppNavMeshOut = findMesh.m_pNavMesh;

	return findMesh.m_pNavPoly;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::RegisterInternal()
{
	const bool readOnly = m_regParams.m_readOnly;

	NAV_ASSERT(readOnly ? NavMeshMgr::GetGlobalLock()->IsLocked() : NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	const BoundFrame& bf = GetBoundFrame();

	Point navPolyPosWs;
	const NavMesh* pMesh = nullptr;
	const NavPoly* pPoly = FindRegisterNavPoly(bf, m_definition, m_regParams, &navPolyPosWs, &pMesh, false);

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

	if (!m_hasCoverFeatureVerts)
	{
		const Level* pAllocLevel = GetAllocLevel();
		TryCollectFeatureDbInfo(pAllocLevel);
	}

	RefreshProtectionFrustum();

	// testing for static blockage here, because we ignore it for purposes of registration
	// note the updating of m_canStepOut is also now handled in here
	RefreshNavMeshClearance();

	SearchForBlockingRigidBodies();

	m_doorIsBlocking = false;
	if (m_definition.m_nextToDoor)
	{
		SearchForDoor();
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::UnregisterInternal()
{
	for (U32 ii = 0; ii < 2; ii++)
	{
		RemoveBlockingRigidBody((BlockingDirection)ii);
	}

	if (NdGameObject* pDoor = m_hDoor.ToMutableProcess())
	{
		ASSERT(pDoor->GetApBlockerInterface());
		pDoor->GetApBlockerInterface()->RemoveBlockedAp(this);
	}

	ActionPack::UnregisterInternal();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool GrowLine(Point& p0, Point& p1, Point_arg p)
{
	const Vector d = p1 - p0;
	const Vector ap = p - p0;
	const Scalar lenSqr = LengthSqr(d);

	const Scalar t = AccurateDiv(Dot(d, ap), lenSqr);
	const Vector v = ap - (t * d);

	const Point closestPoint = p - v;

	if (t < 0.0f)
	{
		p0 = closestPoint;
		return true;
	}
	else if (t > 1.0f)
	{
		p1 = closestPoint;
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GetLinkedCover(const Level* pLevel, const U16 curId, Point_arg linkPos, Vector_arg coverWallNormal, CoverInfo& cOut)
{
	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	bool displayedDebug = false;

	ListArray<const FeatureCover*> featureCoverList(1024);
	ListArray<CoverInfo> coverInfoList(1024);

	featureCoverList.Clear();
	coverInfoList.Clear();

	const FeatureDbArray& featureArray = pLevel->GetFeatureDbArray();
	for (U32F iFdb = 0; iFdb < featureArray.size(); ++iFdb)
	{
		const FeatureDb* pFdb = featureArray[iFdb];
		if (!pFdb)
			continue;

		U32F numCovers = pFdb->FindCovers(&featureCoverList, MovingSphere(linkPos, 0.3f, kZero), Locator(kIdentity));
		AppendList(featureCoverList, nullptr, &coverInfoList, kOrigin, displayedDebug);
	}

	float bestRating = kEdgeLinkWalkDotPThreshold;
	bool found = false;

	for (ListArray<CoverInfo>::ConstIterator it = coverInfoList.begin(); it != coverInfoList.end(); it++)
	{
		CoverInfo info = *it;
		if (!info.m_pSrcCover || (info.m_pSrcCover->GetId() == curId))
			continue;

		const Vector wallNormal = info.GetWallNormal();
		const float rating = Dot(wallNormal, coverWallNormal);

		if (rating > bestRating)
		{
			found = true;
			cOut = info;
		}
	}

	return found;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::SetCoverFeatureVertsWs(Point_arg leftWs, Point_arg rightWs)
{
	const Locator locWs = GetLocatorWs();
	m_coverFeatureVertsLs[0] = locWs.UntransformPoint(leftWs);
	m_coverFeatureVertsLs[1] = locWs.UntransformPoint(rightWs);
	m_hasCoverFeatureVerts = true;

	RefreshProtectionFrustum();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::TryCollectFeatureDbInfo(const Level* pLevel)
{
	if (TryCollectFeatureDbInfoFromCover(pLevel))
	{
		m_hasCoverFeatureVerts = true;
	}
	else if (TryCollectFeatureDbInfoFromEdges(pLevel))
	{
		m_hasCoverFeatureVerts = true;
	}
	else
	{
		m_hasCoverFeatureVerts = false;
		return;
	}

	// try and keep 0 on the left and 1 on the right
	Point v0ls = m_coverFeatureVertsLs[0];
	Point v1ls = m_coverFeatureVertsLs[1];

	if (v1ls.X() > v0ls.X())
	{
		// swap
		Point temp = v0ls;
		v0ls = v1ls;
		v1ls = temp;
	}

	// don't allow ridiculously wide covers, clamp the distances;
	const float kMaxCoverFrustumWidth = 5.0f;
	v0ls.SetX(MinMax((float)v0ls.X(), -kMaxCoverFrustumWidth / 2.0f, kMaxCoverFrustumWidth / 2.0f));
	v1ls.SetX(MinMax((float)v1ls.X(), -kMaxCoverFrustumWidth / 2.0f, kMaxCoverFrustumWidth / 2.0f));

	m_coverFeatureVertsLs[0] = v0ls;
	m_coverFeatureVertsLs[1] = v1ls;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::RefreshProtectionFrustum()
{
	Point v0Ws, v1Ws;
/*	if (GetCoverFeatureVertsWs(v0Ws, v1Ws))
	{
		const Quat rot = GetBoundFrame().GetRotationWs();
		const Vector upWs = GetLocalY(rot);
		const Vector fwWs = GetLocalZ(rot);

		const Point srcPosWs = GetProtectionPosWs();

		const Point p0hi = v0Ws;
		const Point p1hi = v1Ws;
		const Point p0lo = v0Ws - (m_definition.m_height*upWs);
		const Point p1lo = v1Ws - (m_definition.m_height*upWs);

		const Vector v0hi = SafeNormalize(p0hi - srcPosWs, fwWs);
		const Vector v1hi = SafeNormalize(p1hi - srcPosWs, fwWs);
		const Vector v0lo = SafeNormalize(p0lo - srcPosWs, fwWs);
		const Vector v1lo = SafeNormalize(p1lo - srcPosWs, fwWs);

		// inward frustum normals
		const Vector leftWs  = SafeNormalize(Cross(v0lo, v0hi), kZero);
		const Vector rightWs = SafeNormalize(Cross(v1hi, v1lo), kZero);
		const Vector upperWs = SafeNormalize(Cross(v0hi, v1hi), kZero);
		const Vector lowerWs = SafeNormalize(Cross(v1lo, v0lo), kZero);

		// OPTIMIZATION: keep in world space to avoid transforming input vectors at runtime
		m_protectionFrustum.m_planeNormLeftWs  = leftWs;
		m_protectionFrustum.m_planeNormRightWs = rightWs;
		m_protectionFrustum.m_planeNormUpperWs = upperWs;
		m_protectionFrustum.m_planeNormLowerWs = lowerWs;
	}
	else*/
	{
		ProtectionFrustumAngles angles;
		angles.outer = 45.f;
		BuildProtectionFrustum(&m_protectionFrustum, angles);
	}

	m_hasProtectionFrustum = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::IsPointWsInProtectionFrustum(Point_arg testPointWs,
												  Scalar_arg maxSinOffAngle /* =SCALAR_LC(0.0f) */) const

{
	Scalar testPointSine(SCALAR_LC(0.0f));
	const CoverFrustum& frustum = GetProtectionFrustum();
	const Point defSrcPosWs = GetProtectionPosWs();
	const Vector toTargetWs = SafeNormalize(testPointWs - defSrcPosWs, kZero);
	testPointSine = frustum.GetVectorOutsideFrustumSine(toTargetWs); // OPTIMIZATION: this frustum is actually in WORLD SPACE
	return (testPointSine < maxSinOffAngle);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::TryCollectFeatureDbInfoFromCover(const Level* pLevel)
{
	ASSERT(pLevel);
	if (!pLevel)
		return false;

	const BoundFrame loc = GetBoundFrame();
	const Locator locWs = loc.GetLocatorWs();
	const Point posWs = loc.GetTranslationWs();
	const Vector upWs = GetLocalY(loc.GetRotationWs());
	const Vector coverWallNormalWs = -GetLocalZ(loc.GetRotationWs());
	const Sphere s = Sphere(posWs + m_definition.m_height * upWs, 0.25f);
	bool displayedDebug = false;

	m_coverFeatureVertsLs[0] = m_coverFeatureVertsLs[1] = kOrigin;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	ListArray<const FeatureCover*> featureCoverList(1024);
	ListArray<CoverInfo> coverInfoList(1024);

	coverInfoList.Clear();

	const FeatureDbArray& featureArray = pLevel->GetFeatureDbArray();
	for (U32F iFdb = 0; iFdb < featureArray.size(); ++iFdb)
	{
		const FeatureDb* pFdb = featureArray[iFdb];
		if (!pFdb)
			continue;

		featureCoverList.Clear();

		const U32F numCovers = pFdb->FindCovers(&featureCoverList, MovingSphere(posWs, kCoverFeatureSearchRadius, kZero), Locator(kIdentity));
		AppendList(featureCoverList, nullptr, &coverInfoList, kOrigin, displayedDebug);
	}

	float bestRating = kEdgeLinkStartDotPThreshold;
	U16 baseId = -1;

	Point v0Ws = posWs;
	Point v1Ws = posWs;

	for (ListArray<CoverInfo>::ConstIterator it = coverInfoList.begin(); it != coverInfoList.end(); it++)
	{
		CoverInfo info = *it;

		const Vector wallNormal = info.GetWallNormal();
		const float dotP = Dot(wallNormal, coverWallNormalWs);
		if (dotP < kEdgeLinkStartDotPThreshold)
			continue;

		const float dist = Dist(info.GetClosestPoint(posWs), posWs);
		const float rating = dotP / (dist + kSmallFloat);
		if (rating > bestRating)
		{
			info.CalculateClosestPoint(posWs);
			v0Ws = info.GetVert0();
			v1Ws = info.GetVert1();
			baseId = info.m_pSrcCover->GetId();
			bestRating = rating;
		}
	}

	if (baseId == U16(-1))
		return false;

	CoverInfo linkedCover;
	U16 curId = baseId;
	U32 count = 0; //These loops cant get stuck indefinitely
	while (GetLinkedCover(pLevel, curId, v0Ws, coverWallNormalWs, linkedCover) && count < 200)
	{
		count++;
		bool grew = false;
		grew = GrowLine(v0Ws, v1Ws, linkedCover.GetVert0()) || grew;
		grew = GrowLine(v0Ws, v1Ws, linkedCover.GetVert1()) || grew;
		if (!grew)
			break;
		curId = linkedCover.m_pSrcCover->GetId();
	}

	curId = baseId;
	count = 0;
	while (GetLinkedCover(pLevel, curId, v1Ws, coverWallNormalWs, linkedCover) && count < 200)
	{
		bool grew = false;
		grew = GrowLine(v0Ws, v1Ws, linkedCover.GetVert0()) || grew;
		grew = GrowLine(v0Ws, v1Ws, linkedCover.GetVert1()) || grew;
		if (!grew)
			break;
		curId = linkedCover.m_pSrcCover->GetId();
		count++;
	}

	GrowLine(v0Ws, v1Ws, posWs);

	Point closestPos = posWs;
	if (DistPointLine(posWs, v0Ws, v1Ws, &closestPos) > NDI_FLT_EPSILON)
	{
		const Vector adjustment = posWs - closestPos;

		v0Ws += adjustment;
		v1Ws += adjustment;
	}

	v0Ws += upWs*m_definition.m_height;
	v1Ws += upWs*m_definition.m_height;

	SetCoverFeatureVertsWs(v0Ws, v1Ws);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::TryCollectFeatureDbInfoFromEdges(const Level* pLevel)
{
	ASSERT(pLevel);
	if (!pLevel)
		return false;

	const BoundFrame loc = GetBoundFrame();
	const Point posWs = loc.GetTranslationWs();
	const Vector upWs = GetLocalY(loc.GetRotationWs());
	const Vector coverWallNormalWs = -GetLocalZ(loc.GetRotationWs());
	const Vector apDirectionWs = GetLocalZ(loc.GetRotationWs());
	const Sphere s = Sphere(posWs + m_definition.m_height * upWs, 0.25f);
	bool displayedDebug = false;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	ListArray<const FeatureEdge*> featureEdgeList(1024);
	ListArray<EdgeInfo> edgeInfoList(1024);

	edgeInfoList.Clear();

	const FeatureDbArray& featureArray = pLevel->GetFeatureDbArray();
	for (U32F iFdb = 0; iFdb < featureArray.size(); ++iFdb)
	{
		const FeatureDb* pFdb = featureArray[iFdb];
		if (!pFdb)
			continue;

		featureEdgeList.Clear();
		const U32F numEdges = pFdb->FindEdges(&featureEdgeList, s, Locator(kIdentity), 0, upWs);
		AppendList(featureEdgeList, nullptr, &edgeInfoList, s.GetCenter(), displayedDebug);
	}

	bool found = false;
	float bestRating = -kLargeFloat;
	EdgeInfo edgeInfo;
	for (ListArray<EdgeInfo>::ConstIterator it = edgeInfoList.begin(); it != edgeInfoList.end(); it++)
	{
		EdgeInfo info = *it;
		const float d = Dist(s.GetCenter(), info.GetClosestEdgePt());
		const Vector edgeNormalWs = info.GetFlattenedWallNormal(upWs);
		const float rating = Dot(edgeNormalWs, coverWallNormalWs);
		//const float c = /*d*/1.0f / (dotP + kSmallestFloat);
		if (rating > bestRating)
		{
			edgeInfo = info;
			bestRating = rating;
			found = true;
		}
	}

	if (!found)
		return false;

	m_coverFeatureVertsLs[0] = m_coverFeatureVertsLs[1] = kOrigin;

	Point v0Ws = edgeInfo.GetVert0();
	Point v1Ws = edgeInfo.GetVert1();

	// walk left
	FeatureEdgeReference link = edgeInfo.GetLink(0);
	const U16 startingLeftId = link.GetSrcEdge() ? link.GetSrcEdge()->GetId() : 0;
	U32 incrCount = 0;
	while (link)
	{
		if (Dot(link.GetFlattenedWallNormal(upWs), -apDirectionWs) < kEdgeLinkWalkDotPThreshold)
			break;

		GrowLine(v0Ws, v1Ws, link.GetVert0());
		GrowLine(v0Ws, v1Ws, link.GetVert1());

		link = link.GetLink(0);
		++incrCount;
		const FeatureEdge* pNextLinkEdge = link.GetSrcEdge();
		if (pNextLinkEdge && pNextLinkEdge->GetId() == startingLeftId)
		{
			MsgWarn("Cyclical feature edges detected at ID %d (%d steps away)\n", startingLeftId, incrCount);
			break;
		}
	}

	// walk right
	link = edgeInfo.GetLink(1);
	incrCount = 0;
	const U16 startingRightId = link.GetSrcEdge() ? link.GetSrcEdge()->GetId() : 0;
	while (link)
	{
		if (Dot(link.GetFlattenedWallNormal(upWs), -apDirectionWs) < kEdgeLinkWalkDotPThreshold)
			break;

		GrowLine(v0Ws, v1Ws, link.GetVert0());
		GrowLine(v0Ws, v1Ws, link.GetVert1());

		link = link.GetLink(1);
		++incrCount;

		const FeatureEdge* pNextLinkEdge = link.GetSrcEdge();
		if (pNextLinkEdge && pNextLinkEdge->GetId() == startingRightId)
		{
			MsgWarn("Cyclical feature edges detected at ID %d (%d steps away)\n", startingRightId, incrCount);
			break;
		}
	}

	SetCoverFeatureVertsWs(v0Ws, v1Ws);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector CoverActionPack::GetCoverDirectionPs() const
{
	return GetLocalZ(GetLocatorPs().Rot());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::SetTempCost(float cost)
{
	m_definition.m_tempCost = cost;
	m_definition.m_costUpdateTime = GetContextProcessCurTime();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void BuildProtectionFrustum(CoverFrustum* pFrustum,
										   const ProtectionFrustumAngles& angles,
										   CoverDefinition::CoverType coverType,
										   const Locator& apLocatorWs)
{
	float leftAngle = 0.0f;
	float rightAngle = 0.0f;
	float upperAngle = 0.0f;

	switch (coverType)
	{
	case CoverDefinition::kCoverStandLeft:
	case CoverDefinition::kCoverCrouchLeft:
		leftAngle  = angles.outer;
		rightAngle = angles.inner;
		upperAngle = (coverType == CoverDefinition::kCoverStandLeft) ? angles.upperStand : angles.upperCrouch;
		break;

	case CoverDefinition::kCoverStandRight:
	case CoverDefinition::kCoverCrouchRight:
		leftAngle  = -angles.inner;
		rightAngle = -angles.outer;
		upperAngle = (coverType == CoverDefinition::kCoverStandRight) ? angles.upperStand : angles.upperCrouch;
		break;

	case CoverDefinition::kCoverCrouchOver:
		leftAngle  = -angles.inner;
		rightAngle = angles.inner;
		upperAngle = angles.upperCrouch;
		break;
	}

	Quat q = kIdentity;
	// frustum plane normals
	q = QuatFromAxisAngle(Vector(kUnitYAxis), DEGREES_TO_RADIANS(leftAngle - 90.0f));
	pFrustum->m_planeNormLeftWs = apLocatorWs.TransformVector(GetLocalZ(q));

	q = QuatFromAxisAngle(Vector(kUnitYAxis), DEGREES_TO_RADIANS(rightAngle + 90.0f));
	pFrustum->m_planeNormRightWs = apLocatorWs.TransformVector(GetLocalZ(q));

	// NOTE: upper and lower angles are negated because someone got the right-hand rule confused
	// (in a RH system with Z forward, pitch is measured around X, which points to the LEFT not the right)

	q = QuatFromAxisAngle(Vector(kUnitXAxis), DEGREES_TO_RADIANS(-upperAngle + 90.0f));
	pFrustum->m_planeNormUpperWs = apLocatorWs.TransformVector(GetLocalZ(q));

	q = QuatFromAxisAngle(Vector(kUnitXAxis), DEGREES_TO_RADIANS(-angles.lower - 90.0f));
	pFrustum->m_planeNormLowerWs = apLocatorWs.TransformVector(GetLocalZ(q));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::BuildProtectionFrustum( CoverFrustum* pFrustum, const ProtectionFrustumAngles& angles ) const
{
	::BuildProtectionFrustum(pFrustum, angles, m_definition.m_coverType, GetLocatorWs());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::DebugDraw(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	const Locator locWs = GetLocatorWs();
	const Quat coverRotWs = locWs.Rot();
	const Point coverPosWs = locWs.Pos();
	const Vector upWs = GetLocalY(coverRotWs);

	// Draw the cover location.
	const Vector apDirectionWs(GetLocalZ(coverRotWs));
	//g_prim.Draw(DebugCoordAxes(Locator(coverPosWs - apDirectionWs * 0.1f , coverRotWs), 0.1f));
	//Point visPosWs = GetVisibilityPositionWs();
	//g_prim.Draw(DebugCross(visPosWs, 0.05f, kColorYellowTrans));

	Color apDebugColor = kColorWhiteTrans;
	const CoverDefinition& definition = GetDefinition();
	if (definition.CanPeekLeft())
	{
		apDebugColor = kColorOrangeTrans;
	}

	if (definition.CanPeekRight())
	{
		apDebugColor = kColorBlueTrans;
	}

	// A door is preventing a corner check from being allowed, but the cover ap is available
	if (m_doorIsBlocking)
	{
		apDebugColor = kColorCyanTrans;
	}

	if (!IsAvailable())
	{
		apDebugColor = kColorRed;
	}

	for (U32F ii = 0; ii<kBlockingCount; ii++)
	{
		if (const RigidBody* pBody = m_hBlockingRigidBody[(BlockingDirection)ii].ToBody())
		{
			HavokMarkForReadJanitor jj;
			HavokDebugDrawRigidBody(pBody->GetHavokBodyId(), pBody->GetLocatorCm(), kColorRed, CollisionDebugDrawConfig(), tt);
			apDebugColor = kColorPinkTrans;
		}
#ifdef JSINECKY
		Obb obb;
		if (GetCheckBlockingObb((BlockingDirection)ii, obb))
		{
			g_prim.Draw(DebugBox(obb.m_transform, obb.m_min, obb.m_max, m_hBlockingRigidBody[(BlockingDirection)ii].ToBody() ? kColorPinkTrans : kColorWhiteTrans, kPrimEnableWireframe), tt);
		}
#endif
	}

	if (const NavCharacter* pBlockingNavChar = GetBlockingNavChar())
	{
		const float timeSince = ToSeconds(pBlockingNavChar->GetClock()->GetTimePassed(m_navCharBlockTime));
		g_prim.Draw(DebugString(coverPosWs + Vector(0.0f, 0.5f, 0.0f),
								StringBuilder<128>("Blocked by %s %0.1fsec ago", pBlockingNavChar->GetName(), timeSince)
									.c_str(),
								kColorWhiteTrans, 0.5f),
					tt);

		apDebugColor = kColorRed;
	}

	const Vector apPlaneHeight(0.0f, definition.m_height, 0.0f);
	const Vector apRenderOffsetWs(apDirectionWs * -0.01f);
	const Vector apIntoCoverDirWs = definition.CanPeekRight() ? GetLocalX(coverRotWs) : -GetLocalX(coverRotWs);

	if (g_ndAiOptions.m_drawQuestionableCoverAPs)
	{
		const float low0 = 0.6f;
		const float low1 = 0.8f;
		const float high0 = 1.1f;
		const float high1 = 1.5f;

		bool isQuestionable = false;
		if (definition.m_height >= low0 && definition.m_height < low1)
			isQuestionable = true;
		else if (definition.m_height > high0 && definition.m_height < high1)
			isQuestionable = true;

		if (!isQuestionable)
			return;

		apDebugColor = kColorMagenta;
	}

	// Draw the wall as a quad
	Point tmpPosWs = coverPosWs + apRenderOffsetWs;
	Point verts[4] =
	{
		tmpPosWs,
		tmpPosWs + apIntoCoverDirWs * 0.1f,
		tmpPosWs + apIntoCoverDirWs * 0.1f + apPlaneHeight,
		tmpPosWs + apPlaneHeight,
	};

	if (definition.CanPeekRight())
	{
		// reorder verts, swap 0 with 3 and swap 1 with 2
		for (I32F i = 0; i < 2; ++i)
		{
			I32F i2 = 3 - i;
			Point vTmp = verts[i];
			verts[i] = verts[i2];
			verts[i2] = vTmp;
		}
	}

	g_prim.Draw(DebugQuad(verts[0], verts[1], verts[2], verts[3], apDebugColor), tt);

	g_prim.Draw(DebugSphere(GetRegistrationPointWs(), 0.03f, kColorMagentaTrans), tt);

	Color apDebugColorOpaque = apDebugColor;
	apDebugColorOpaque.a = 1.0f;

	if (g_ndAiOptions.m_drawCoverApHeight)
	{
		g_prim.Draw(DebugString(verts[3], StringBuilder<256>("%1.2fm", (F32)apPlaneHeight.Y()).c_str(), apDebugColorOpaque, 0.5f));
	}

	if (g_navMeshDrawFilter.m_drawApFeatureDetail)
	{
		Point v0Ws, v1Ws;
		if (GetCoverFeatureVertsWs(v0Ws, v1Ws))
		{
			const Point srcPosWs = GetProtectionPosWs();

			const Vector zNudge = apDirectionWs * -0.01f;

			const Point v0LowWs = v0Ws - (m_definition.m_height*upWs);
			const Point v1LowWs = v1Ws - (m_definition.m_height*upWs);

			g_prim.Draw(DebugQuad(v0Ws + zNudge, v0LowWs + zNudge, v1LowWs + zNudge, v1Ws + zNudge, kColorGreenTrans, kPrimEnableHiddenLineAlpha), tt);

			g_prim.Draw(DebugLine(v0Ws, v1Ws, kColorGreen, kColorGreen, 5.0f, kPrimEnableHiddenLineAlpha), tt);
			g_prim.Draw(DebugLine(srcPosWs, v0Ws, kColorGreenTrans, kColorGreenTrans), tt);
			g_prim.Draw(DebugLine(srcPosWs, v1Ws, kColorGreenTrans, kColorGreenTrans), tt);
			g_prim.Draw(DebugCross(srcPosWs, 0.25f, kColorGreenTrans), tt);
		}

		const CoverFrustum frustum = GetProtectionFrustum();
		const Color colors[4] ={kColorCyan, kColorOrange, kColorGreen, kColorBlue};
		frustum.DebugDraw(GetProtectionPosWs(), 20.0f, colors, tt);
	}

	if (g_navMeshDrawFilter.m_drawApDetail)
	{
		const RenderCamera& cam = GetRenderCamera(0);
		const Point renderPosWs = verts[0];
		if (cam.IsSphereInFrustum(Sphere(renderPosWs, 2.0f)) && Dist(cam.m_position, renderPosWs) < 35.0f)
		{
			ScreenSpaceTextPrinter printer(renderPosWs, ScreenSpaceTextPrinter::kPrintNextLineBelowPrevious, tt, 0.5f);

			printer.PrintText(kColorGreen, "MgrId: 0x%x", GetMgrId());

			printer.PrintTextNoCr(definition.m_canStepOut ? kColorGreen : kColorRed, "step-out ");
			printer.PrintText(definition.m_canCornerCheck ? kColorGreen : kColorRed, "corner-check-wide");
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::DebugDrawRegistrationFailure() const
{
	STRIP_IN_FINAL_BUILD;

	DebugDrawRegistrationProbes();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::DebugDrawPointOutsideFrustumSine(Point_arg testPosWs, Color color, DebugPrimTime duration) const
{
	STRIP_IN_FINAL_BUILD;

	Point v0Ws, v1Ws;
	if (GetCoverFeatureVertsWs(v0Ws, v1Ws))
	{
		const Point srcPosWs = GetProtectionPosWs();
		g_prim.Draw(DebugCross(srcPosWs, 0.1f, kColorGreen), duration);

		const Locator locWs = GetLocatorWs();
		const Vector upWs = GetLocalY(locWs.GetRotation());

		const Point p0hi = v0Ws;
		const Point p1hi = v1Ws;
		const Point p0lo = v0Ws - (m_definition.m_height*upWs);
		const Point p1lo = v1Ws - (m_definition.m_height*upWs);

		g_prim.Draw(DebugLine(p0hi, p1hi, kColorGreen, 5.0f), duration);
		g_prim.Draw(DebugLine(srcPosWs, p0hi, kColorBlueTrans), duration);
		g_prim.Draw(DebugLine(srcPosWs, p0lo, kColorBlueTrans), duration);
		g_prim.Draw(DebugLine(srcPosWs, p1hi, kColorRedTrans), duration);
		g_prim.Draw(DebugLine(srcPosWs, p1lo, kColorRedTrans), duration);

		const Vector defToTestWs = SafeNormalize(testPosWs - srcPosWs, kZero);
		g_prim.Draw(DebugLine(srcPosWs, defToTestWs, kColorBlack, color), duration);

		// IMPORTANT: the defensive rating is 1.0 within the frustum, and drops to zero the more "outside" you get
		const float sine = (float)GetPointOutsideFrustumSine(testPosWs);
		g_prim.Draw(DebugString(srcPosWs, StringBuilder<32>("%0.4f", sine).c_str(), kColorWhite, 0.5f), duration);
	}
	else
	{
		const Point srcPosWs = GetVisibilityPositionWs();
		g_prim.Draw(DebugCross(srcPosWs, 0.1f, kColorGreen), duration);

		const Vector defToTestWs = SafeNormalize(testPosWs - srcPosWs, kZero);
		g_prim.Draw(DebugLine(srcPosWs, defToTestWs, kColorBlack, color), duration);

	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::DebugDrawRegistrationProbes() const
{
	STRIP_IN_FINAL_BUILD;

	const Point regPosWs = GetRegistrationPointWs();

	const RenderCamera& cam = GetRenderCamera(0);
	if (!cam.IsSphereInFrustum(Sphere(regPosWs, 2.0f)) || Dist(cam.m_position, regPosWs) > 35.0f)
	{
		return;
	}

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const NavMesh* pMesh = GetRegisteredNavLocation().ToNavMesh();
	const NavPoly* pPoly = GetRegisteredNavLocation().ToNavPoly();

	const BoundFrame& bf = GetBoundFrame();
	const Locator locWs = bf.GetLocatorWs();
	const Point probeStartWs = regPosWs;

	// ProbeForRegistration(m_definition, pMesh, pPoly, locWs, probeStartWs, Nav::kStaticBlockageMaskAll, true);
	// ProbeForCanStepOut(m_definition, pMesh, pPoly, locWs, probeStartWs, Nav::kStaticBlockageMaskAll, true);
	ProbeForCornerCheck(this, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::AdjustGameTime(TimeDelta delta)
{
	m_navCharBlockTime += delta;
	m_definition.m_costUpdateTime += delta;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetOffsetSidePositionWs(float side, float up) const
{
	const CoverDefinition& coverDef = GetDefinition();

	const Locator& coverLocWs  = GetBoundFrame().GetLocatorWs();
	const Vector coverUpDirWs  = GetLocalY(coverLocWs.Rot());
	const Vector wallDirWs	   = GetWallDirectionWs();
	const Vector coverNormalWs = GetLocalZ(coverLocWs.Rot());
	const Point coverPosWs	   = coverLocWs.Pos();
	const F32 height = coverDef.IsCrouch() ? kCoverSideHeight : (coverDef.IsLow() ? 0.6f : 1.4f);

	const Point sideSrcPosWs = coverPosWs - (side * wallDirWs) + ((height + up) * coverUpDirWs) + (-0.25f * coverNormalWs);

	return sideSrcPosWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetIdleSidePositionWs() const
{
	return GetOffsetSidePositionWs(kCoverSideIdleOffset, 0.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetFireSidePositionWs() const
{
	return GetOffsetSidePositionWs(kCoverSideFireOffset, 0.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetOffsetOverPositionWs(float side, float up) const
{
	const CoverDefinition& coverDef = GetDefinition();

	const Locator& coverLocWs = GetBoundFrame().GetLocatorWs();
	const Vector coverUpDirWs = GetLocalY(coverLocWs.Rot());
	const Vector wallDirWs = GetWallDirectionWs();
	const Vector coverNormalWs = GetLocalZ(coverLocWs.Rot());
	const Point coverPosWs = coverLocWs.Pos();

	const Point overSrcPosWs = coverPosWs - (side * wallDirWs) + ((coverDef.m_height + up) * coverUpDirWs)
		- (0.3f * coverNormalWs);

	return overSrcPosWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetFireOverPositionWs() const
{
	const CoverDefinition& coverDef = GetDefinition();

	const Locator& coverLocWs  = GetBoundFrame().GetLocatorWs();
	const Vector coverUpDirWs  = GetLocalY(coverLocWs.Rot());
	const Vector wallDirWs	   = GetWallDirectionWs();
	const Vector coverNormalWs = GetLocalZ(coverLocWs.Rot());
	const Point coverPosWs	   = coverLocWs.Pos();

	const Point overSrcPosWs = coverPosWs - (-0.25f * wallDirWs) + ((coverDef.m_height + 0.4f) * coverUpDirWs)
							   - (0.3f * coverNormalWs);

	return overSrcPosWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetOffsetSidePositionPs(F32 offset) const
{
	const CoverDefinition& coverDef = GetDefinition();

	const Locator& coverLocPs  = GetBoundFrame().GetLocatorPs();
	const Vector coverUpDirPs  = GetLocalY(coverLocPs.Rot());
	const Vector wallDirPs	   = GetWallDirectionPs();
	const Vector coverNormalPs = GetLocalZ(coverLocPs.Rot());
	const Point coverPosPs	   = coverLocPs.Pos();
	const F32 height = coverDef.IsCrouch() ? kCoverSideHeight : (coverDef.IsLow() ? 0.6f : 1.4f);

	const Point sideSrcPosps = coverPosPs - (offset * wallDirPs) + (height * coverUpDirPs) + (-0.25f * coverNormalPs);

	return sideSrcPosps;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetIdleSidePositionPs() const
{
	return GetOffsetSidePositionPs(kCoverSideIdleOffset);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetFireSidePositionPs() const
{
	return GetOffsetSidePositionPs(kCoverSideFireOffset);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetFireOverPositionPs() const
{
	const CoverDefinition& coverDef = GetDefinition();

	const Locator& coverLocPs  = GetBoundFrame().GetLocatorPs();
	const Vector coverUpDirPs  = GetLocalY(coverLocPs.Rot());
	const Vector wallDirPs	   = GetWallDirectionPs();
	const Vector coverNormalPs = GetLocalZ(coverLocPs.Rot());
	const Point coverPosPs	   = coverLocPs.Pos();

	const Point overSrcPosPs = coverPosPs - (-0.25f * wallDirPs) + ((coverDef.m_height + 0.4f) * coverUpDirPs)
							   - (0.3f * coverNormalPs);

	return overSrcPosPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetCoverPositionPs() const
{
	const Locator locPs = m_loc.GetLocatorPs();
	Scalar yOffset = SCALAR_LC(1.3f);
	if (m_definition.IsLow())
	{
		yOffset = SCALAR_LC(0.7f);
	}

	Scalar xSign = Scalar(kZero);
	if (m_definition.CanPeekLeft())
	{
		xSign = SCALAR_LC(-0.5f);
	}
	if (m_definition.CanPeekRight())
	{
		xSign = SCALAR_LC(0.5f);
	}
	Point ptLs = m_regPtLs + Vector(xSign, yOffset, SCALAR_LC(-1.025f));
	return locPs.TransformPoint(ptLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::MakeCoverDefinition(const Locator& cornerSpace,
										  const FeatureCorner& srcCorner,
										  CoverDefinition* pCoverDef,
										  Locator* pApLoc)
{
	if (Length(VectorXz(srcCorner.GetWallNormal())) < 0.1f)
	{
		return false;
	}

	Point vert0		= cornerSpace.TransformPoint(srcCorner.GetVert0());
	Point vert1		= cornerSpace.TransformPoint(srcCorner.GetVert1());
	Vector coverDir = cornerSpace.TransformVector(-srcCorner.GetWallNormal());
	coverDir.SetY(0.0f);
	coverDir = Normalize(coverDir);
	*pApLoc	 = Locator(vert0, QuatFromXZDir(coverDir));

	const bool lowCover = (srcCorner.GetFlags() & (FeatureCorner::kFlagShortCover | FeatureCorner::kFlagLowCover));

	CoverDefinition& coverDef		= *pCoverDef;
	coverDef.m_height				= vert1.Y() - vert0.Y();
	coverDef.m_costUpdateTime		= Seconds(0.0f);
	coverDef.m_tempCost				= 0;
	coverDef.m_canCornerCheck		= false;
	coverDef.m_canStepOut			= (srcCorner.GetFlags() & FeatureCorner::kFlagCanStepOut) != 0;
	coverDef.m_srcCornerCanStepOut	= coverDef.m_canStepOut;
	coverDef.m_srcCornerCanAim		= (srcCorner.GetFlags() & FeatureCorner::kFlagNoAim) == 0;
	coverDef.m_canPeekOver			= lowCover && ((srcCorner.GetFlags() & FeatureCorner::kFlagPeekOver) != 0);
	coverDef.m_standOnly			= (srcCorner.GetFlags() & FeatureCorner::kFlagStandOnly) != 0;
	coverDef.m_nextToDoor			= (srcCorner.GetFlags() & FeatureCorner::kFlagDoorCorner) != 0;

	bool valid = false;
	if (srcCorner.GetFlags() & FeatureCorner::kFlagPeekLeft)
	{
		if (lowCover)
		{
			valid = true;
			coverDef.m_coverType = CoverDefinition::kCoverCrouchLeft;
		}
		else
		{
			valid = true;
			coverDef.m_coverType = CoverDefinition::kCoverStandLeft;
		}
	}
	else if (srcCorner.GetFlags() & FeatureCorner::kFlagPeekRight)
	{
		if (lowCover)
		{
			valid = true;
			coverDef.m_coverType = CoverDefinition::kCoverCrouchRight;
		}
		else
		{
			valid = true;
			coverDef.m_coverType = CoverDefinition::kCoverStandRight;
		}
	}
	else if (srcCorner.GetFlags() & FeatureCorner::kFlagPeekOver)
	{
		if (lowCover)
		{
			valid = true;
			coverDef.m_coverType = CoverDefinition::kCoverCrouchOver;
		}
	}
	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector CoverActionPack::GetWallDirectionPs() const
{
	const Locator& locPs = GetBoundFrame().GetLocatorPs();
	Vector wallDirPs(SMath::kZero);
	switch (m_definition.m_coverType)
	{
	case CoverDefinition::kCoverCrouchLeft:
	case CoverDefinition::kCoverStandLeft:
		wallDirPs = -GetLocalX(locPs.Rot());
		break;

	case CoverDefinition::kCoverCrouchRight:
	case CoverDefinition::kCoverStandRight:
		wallDirPs = GetLocalX(locPs.Rot());
		break;

	case CoverDefinition::kCoverCrouchOver:
		wallDirPs = GetLocalZ(locPs.Rot());
		break;
	}

	return wallDirPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Scalar CoverActionPack::GetPointOutsideFrustumSine(Point_arg testPosWs) const
{
	PROFILE(AI, CoverAP_GetPtOutFrustSine);

	Point v0Ws, v1Ws;
	if (!GetCoverFeatureVertsWs(v0Ws, v1Ws))
	{
		return -1.0f;
	}

	const Quat rot = GetBoundFrame().GetRotationWs();
	const Vector upWs = GetLocalY(rot);
	const Vector fwWs = GetLocalZ(rot);

	const Point srcPosWs = GetProtectionPosWs();
	const Vector toTargetWs = SafeNormalize(testPosWs - srcPosWs, kZero);

	const Point p0hi = v0Ws;
	const Point p1hi = v1Ws;
	const Point p0lo = v0Ws - (m_definition.m_height*upWs);
	const Point p1lo = v1Ws - (m_definition.m_height*upWs);

	const Vector v0hi = SafeNormalize(p0hi - srcPosWs, fwWs);
	const Vector v1hi = SafeNormalize(p1hi - srcPosWs, fwWs);
	const Vector v0lo = SafeNormalize(p0lo - srcPosWs, fwWs);
	const Vector v1lo = SafeNormalize(p1lo - srcPosWs, fwWs);

	// inward frustum normals
	const Vector leftWs  = SafeNormalize(Cross(v0lo, v0hi), kZero);
	const Vector rightWs = SafeNormalize(Cross(v1hi, v1lo), kZero);
	const Vector upperWs = SafeNormalize(Cross(v0hi, v1hi), kZero);
	const Vector lowerWs = SafeNormalize(Cross(v1lo, v0lo), kZero);

	// calc the sine of the largest angle made between the vector and the outside of the frustum;
	// if the vector lies INSIDE the frustum, the sine returned will be NEGATIVE

	const Scalar sine0 = SCALAR_LC(-1.0f) * Dot(toTargetWs, leftWs);
	const Scalar sine1 = SCALAR_LC(-1.0f) * Dot(toTargetWs, rightWs);
	const Scalar sine2 = SCALAR_LC(-1.0f) * Dot(toTargetWs, upperWs);
	const Scalar sine3 = SCALAR_LC(-1.0f) * Dot(toTargetWs, lowerWs);

	const Scalar worstSine = Max(Max(Max(sine0, sine1), sine2), sine3);

	return worstSine;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::GetCoverFeatureExtents(Scalar& minXOffset, Scalar& maxXOffset) const
{
	Point v0Ws, v1Ws;
	if (GetCoverFeatureVertsWs(v0Ws, v1Ws))
	{
		const Locator locWs = GetLocatorWs();
		Point v0ls = locWs.UntransformPoint(v0Ws);
		Point v1ls = locWs.UntransformPoint(v1Ws);
		minXOffset = v0ls.X();
		maxXOffset = v1ls.X();
		if (minXOffset > maxXOffset)
		{
			Swap(minXOffset, maxXOffset);
		}
		return true;
	}
	else
	{
		minXOffset = SCALAR_LC(0.0f);
		maxXOffset = SCALAR_LC(0.0f);
		return false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetDefensivePosWs() const
{
	const Locator locWs = GetLocatorWs();
	const Point offsetLs = Point(0.0f, 0.0f, -0.31f);
	return locWs.TransformPoint(offsetLs) + (0.73f * GetWallDirectionWs());
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetDefensivePosPs() const
{
	const Locator locPs = GetLocatorPs();
	const Point offsetLs = Point(0.0f, 0.0f, -0.31f);
	return locPs.TransformPoint(offsetLs) + (0.73f * GetWallDirectionPs());
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::GetCoverFeatureVertsWs(Point& v0, Point& v1) const
{
	if (m_hasCoverFeatureVerts)
	{
		v0 = GetLocatorWs().TransformPoint(m_coverFeatureVertsLs[0]);
		v1 = GetLocatorWs().TransformPoint(m_coverFeatureVertsLs[1]);
		return true;
	}
	else
	{
		v0 = v1 = GetLocatorWs().Pos();
		return false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::IsAvailableForInternal(const Process* pProcess) const
{
	const NavCharacter* pBlockingChar = GetBlockingNavChar();
	if (pBlockingChar && pProcess && (pBlockingChar != pProcess))
	{
		return false;
	}

	return ActionPack::IsAvailableForInternal(pProcess);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::TryAddBlockingNavChar(NavCharacter* pBlockingNavChar)
{
	m_hBlockingNavChar = pBlockingNavChar;
	m_navCharBlockTime = pBlockingNavChar ? pBlockingNavChar->GetCurTime() : TimeFrameNegInfinity();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavCharacter* CoverActionPack::GetBlockingNavChar() const
{
	const NavCharacter* pBlockingNavChar = m_hBlockingNavChar.ToProcess();
	if (!pBlockingNavChar)
		return nullptr;

	// if most recent test is more than 0.25s newer than most recent block time
	if (pBlockingNavChar->GetLastUpdatedBlockedApsTime() > m_navCharBlockTime + Seconds(0.25f))
		return nullptr;

	return pBlockingNavChar;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::HasNavMeshClearance(const NavLocation& navLoc,
										  bool debugDraw /* = false */,
										  DebugPrimTime tt /* = kPrimDuration1FramePauseable */) const
{
	bool clear = true;

	const NavMesh* pMesh = navLoc.ToNavMesh();
	const NavPoly* pPoly = navLoc.ToNavPoly();

	if (pMesh && pPoly)
	{
		const Point regPosWs = GetRegistrationPointWs();
		const BoundFrame& bf = GetBoundFrame();
		const Locator& locWs = bf.GetLocatorWs();

		if (ProbeForRegistration(m_definition, pMesh, pPoly, locWs, regPosWs, Nav::kStaticBlockageMaskAll, debugDraw, tt))
		{
			clear = true;
		}
		else
		{
			clear = false;
		}
	}

	return clear;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::RefreshNavMeshClearance()
{
	PROFILE_AUTO(Navigation);

	const NavLocation regNavLoc = GetRegisteredNavLocation();
	const bool clear = HasNavMeshClearance(regNavLoc);

	m_definition.m_canStepOut = false;	// If canStepOut is false determined in tool time, we dont bother with the dynamic runtime navmesh probe.

	if (clear)
	{
		// asdasd
		// Why on earth did I put that comment in there?

		if (m_definition.m_srcCornerCanStepOut)
		{
			const NavLocation registeredNavLoc = GetRegisteredNavLocation();
			const NavMesh* pMesh = registeredNavLoc.ToNavMesh();
			const NavPoly* pPoly = registeredNavLoc.ToNavPoly();

			const Point regPosWs = GetRegistrationPointWs();
			const BoundFrame& bf = GetBoundFrame();
			const Locator& locWs = bf.GetLocatorWs();

			m_definition.m_canStepOut = ProbeForCanStepOut(m_definition,
														   pMesh,
														   pPoly,
														   locWs,
														   regPosWs,
														   Nav::kStaticBlockageMaskAll,
														   false,
														   Seconds(5.0f));
		}

		{
			const bool cornerCheckClear	  = ProbeForCornerCheck(this, false);
			m_definition.m_canCornerCheck = cornerCheckClear;
		}
	}

	m_navBlocked = !clear;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
const NavPoly* CoverActionPack::CanRegisterSelf(const CoverDefinition& coverDef,
												const ActionPackRegistrationParams& params,
												const BoundFrame& apLoc,
												Point* pNavPolyPointWs,
												bool debugDraw)
{
	return FindRegisterNavPoly(apLoc, coverDef, params, pNavPolyPointWs, nullptr, debugDraw);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector CoverActionPack::GetCoverDirectionWs() const
{
	return GetLocalZ(GetLocatorWs().Rot());
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point CoverActionPack::GetProtectionPosWs() const
{
	const Locator locWs = GetLocatorWs();
	const float hh = 0.6f * m_definition.m_height;
	const Point offsetLs = Point(0.0f, hh, -0.75f);
	const Point posWs = locWs.TransformPoint(offsetLs) + (0.5f * GetWallDirectionWs());
	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector CoverActionPack::GetWallDirectionWs() const
{
	Vector wallDirWs(SMath::kZero);
	switch (m_definition.m_coverType)
	{
	case CoverDefinition::kCoverCrouchLeft:
	case CoverDefinition::kCoverStandLeft:
		wallDirWs = -GetLocalX(GetLocatorWs().Rot());
		break;

	case CoverDefinition::kCoverCrouchRight:
	case CoverDefinition::kCoverStandRight:
		wallDirWs = GetLocalX(GetLocatorWs().Rot());
		break;

	case CoverDefinition::kCoverCrouchOver:
		wallDirWs = kZero;
		break;
	}

	return wallDirWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector CoverActionPack::GetDefaultEntryOffsetLs() const
{
	switch (m_definition.m_coverType)
	{
	case CoverDefinition::kCoverCrouchLeft:
	case CoverDefinition::kCoverStandLeft:
		return -Vector(kUnitXAxis) * kCoverOffsetAmount;
		break;

	case CoverDefinition::kCoverCrouchRight:
	case CoverDefinition::kCoverStandRight:
		return Vector(kUnitXAxis) * kCoverOffsetAmount;
		break;
	}

	return kZero;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point CoverActionPack::GetVisibilityPositionPs() const
{
	float yOffset = 0.0f;
	if (m_definition.IsLow())
	{
		yOffset = 1.1f;
	}
	else
	{
		yOffset = 1.5f;
	}

	float xSign = 0.0f;
	if (m_definition.CanPeekLeft())
	{
		xSign = 1.0f;
	}
	if (m_definition.CanPeekRight())
	{
		xSign = -1.0f;
	}
	const Locator locPs = GetBoundFrame().GetLocatorPs();
	Point posPs = locPs.Pos() + GetLocalX(locPs.Rot()) * xSign *0.25f + Vector(0, yOffset, 0);
	return posPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::CheckRigidBodyIsBlocking(RigidBody* pBody, uintptr_t userData)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	BlockingDirection iDir = (BlockingDirection)userData;

	if (iDir < kBlockingCount)
	{
		if (CheckRigidBodyIsBlockingForDir(pBody, iDir))
		{
			return true;
		}
	}
	else
	{
		for (U32F ii = 0; ii < kBlockingCount; ++ii)
		{
			if (CheckRigidBodyIsBlockingForDir(pBody, BlockingDirection(ii)))
			{
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::CheckRigidBodyIsBlockingForDir(RigidBody* pBody, BlockingDirection iDir)
{
	if (!m_hBlockingRigidBody[iDir].HandleValid())
	{
		Obb checkObb;
		if (GetCheckBlockingObb(iDir, checkObb))
		{
			Obb bodyObb;
			pBody->GetObb(bodyObb);

			if (ObbObbIntersect(bodyObb, checkObb))
			{
				SetBlockingRigidBody(pBody, iDir);
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::RemoveBlockingRigidBody(const RigidBody* pBody)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	for (U32F ii = 0; ii < kBlockingCount; ii++)
	{
		if (m_hBlockingRigidBody[ii] == pBody)
		{
			RemoveBlockingRigidBody((BlockingDirection)ii);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::SearchForBlockingRigidBodies()
{
	PROFILE_AUTO(Navigation);

	for (U32F ii = 0; ii < kBlockingCount; ii++)
	{
		if (!m_hBlockingRigidBody[ii].HandleValid())
		{
			Obb obb;
			if (GetCheckBlockingObb((BlockingDirection)ii, obb))
			{
				Aabb aabb = obb.GetEnclosingAabb();
				CoverBlockingBodyCollector collector;
				collector.m_hAp = this;
				collector.m_blockingDir = (BlockingDirection)ii;

				HavokAabbQuery(aabb.m_min,
							   aabb.m_max,
							   collector,
							   CollideFilter(Collide::kLayerMaskFgBig, Pat(Pat::kPassThroughMask)));
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
RigidBodyHandle CoverActionPack::GetBlockingRigidBody(BlockingDirection dir) const
{
	return m_hBlockingRigidBody[dir];
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::GetCheckBlockingObb(BlockingDirection iDir, Obb& obbOut) const
{
	if (iDir == kBlockingOver)
	{
		if (!m_definition.CanPeekOver())
			return false;

		obbOut.m_min = Point(-0.2f, m_definition.m_height + 0.05f, -0.05f);
		obbOut.m_max = obbOut.m_min + Vector(0.4f, 0.2f, 0.3f);
	}
	else if (iDir == kBlockingSide)
	{
		if (!m_definition.CanPeekSide())
			return false;

		obbOut.m_min = Point(m_definition.CanPeekLeft() ? 0.1f : -0.3f, m_definition.IsLow() ? 0.6f : 1.2f, -0.05f);
		obbOut.m_max = obbOut.m_min + Vector(0.2f, 0.2f, 0.3f);
	}
	else
	{
		ASSERT(false);
		return false;
	}

	obbOut.m_transform = GetLocatorWs().AsTransform();
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::SetBlockingRigidBody(RigidBody* pBody, BlockingDirection iDir)
{
	m_hBlockingRigidBody[iDir] = pBody;
	NdGameObject* pGo = pBody->GetOwner();
	GAMEPLAY_ASSERT(pGo && pGo->GetApBlockerInterface());
	pGo->GetApBlockerInterface()->AddBlockedAp(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::RemoveBlockingRigidBody(BlockingDirection iDir)
{
	RigidBody* pBody = m_hBlockingRigidBody[iDir].ToBody();

	m_hBlockingRigidBody[iDir] = nullptr;

	if (pBody)
	{
		NdGameObject* pGo = pBody->GetOwner();
		bool otherBodyHasSameOwner = false;
		if (const RigidBody* pOtherBody = m_hBlockingRigidBody[(iDir + 1) & 1].ToBody())
		{
			otherBodyHasSameOwner = pOtherBody->GetOwner() == pGo;
		}

		if (!otherBodyHasSameOwner)
		{
			IApBlocker* pInterface = pGo ? pGo->GetApBlockerInterface() : nullptr;
			if (pInterface)
			{
				pInterface->RemoveBlockedAp(this);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
class BlockingDoorCollector : public HavokAabbQueryCollector
{
public:
	CoverActionPack* m_pAp;

	virtual bool AddBody(RigidBody* pBody)
	{
		NdGameObject* pGo = pBody->GetOwner();
		if (!pGo || !pGo->GetApBlockerInterface())
		{
			return true;
		}

		if (pGo->GetApBlockerInterface()->IsDoor())
		{
			pGo->GetApBlockerInterface()->AddBlockedAp(m_pAp);
			// No more search needed
			return false;
		}

		return true;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::SetDoor(NdGameObject* pDoor)
{
	ASSERT(m_definition.m_nextToDoor);
	m_hDoor = pDoor;
	if (!pDoor)
		m_doorIsBlocking = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CoverActionPack::SearchForDoor()
{
	PROFILE_AUTO(Navigation);

	Obb obb;
	if (m_definition.m_coverType == CoverDefinition::kCoverStandLeft)
	{
		obb.m_min = Point(0.0f, 0.0f, 0.0f);
		obb.m_max = Point(0.3f, 0.5f, 1.5f);
	}
	else if (m_definition.m_coverType == CoverDefinition::kCoverStandRight)
	{
		obb.m_min = Point(-1.5f, 0.0f, 0.0f);
		obb.m_max = Point(0.0f, 0.5f, 0.3f);
	}
	else
	{
/*
		NAV_ASSERTF(false,
					("Cover AP '%s' is flagged as being next to a door but it's not a standing cover (actual type: %s)",
					 GetName(),
					 CoverDefinition::GetCoverTypeName(m_definition.m_coverType)));
*/
		return;
	}

	obb.m_transform = GetLocatorWs().AsTransform();

	Aabb aabb = obb.GetEnclosingAabb();
	BlockingDoorCollector collector;
	collector.m_pAp = this;
	HavokAabbQuery(aabb.m_min, aabb.m_max, collector, CollideFilter(Collide::kLayerMaskFgBig));
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CoverActionPack::AllowCornerCheck() const
{
	const CoverDefinition& coverDef = GetDefinition();

	if (!coverDef.CanPeekLeft() && !coverDef.CanPeekRight())
		return false;

	if (!coverDef.CanStepOut())
		return false;

	if (!coverDef.CanAim())
		return false;

	if (IsDoorBlocking())
		return false;

	return true;
}

