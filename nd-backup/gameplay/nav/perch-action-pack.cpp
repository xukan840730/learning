/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/perch-action-pack.h"

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

/// --------------------------------------------------------------------------------------------------------------- ///
static const float kPerchFeatureSearchRadius = 0.75f;
static const U32F kRegistrationProbeVectorCount = 3;
const float PerchActionPack::kPerchOffsetAmount = 0.25f;

/// --------------------------------------------------------------------------------------------------------------- ///
static U32F GetRegistrationProbeVectorsLs(const PerchDefinition& PerchDef,
										  Vector probeVectorsLsOut[kRegistrationProbeVectorCount])
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
// Class PerchActionPack.
/// --------------------------------------------------------------------------------------------------------------- ///
PerchActionPack::PerchActionPack(const BoundFrame& loc,
								 const EntitySpawner* pSpawner,
								 const PerchDefinition& perchDef,
								 I32 bodyJoint)
	: ActionPack(kPerchActionPack, POINT_LC(0.0f, 0.0f, -0.3f), loc, pSpawner)
	, m_definition(perchDef)
	, m_bodyJoint(bodyJoint)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
PerchActionPack::PerchActionPack(const BoundFrame& loc,
								 F32 registrationDist,
								 const Level* pLevel,
								 const PerchDefinition& perchDef,
								 I32 bodyJoint)
	: ActionPack(kPerchActionPack, Point(0.0f, 0.0f, -registrationDist), loc, pLevel)
	, m_definition(perchDef)
	, m_bodyJoint(bodyJoint)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
PerchActionPack::PerchActionPack(const BoundFrame& apLoc, F32 registrationDist, const Level* pAllocLevel, const Level* pRegLevel, const PerchDefinition& perchDef, I32 bodyJoint)
	: ActionPack(kPerchActionPack, Point(0.0f, 0.0f, -registrationDist), apLoc, pAllocLevel, pRegLevel)
	, m_definition(perchDef)
	, m_bodyJoint(bodyJoint)
{}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPoly* PerchActionPack::FindRegisterNavPoly(const BoundFrame& apLoc,
													const PerchDefinition& PerchDef,
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

	Vector vecsLs[kRegistrationProbeVectorCount];
	const U32F vecCount = GetRegistrationProbeVectorsLs(PerchDef, vecsLs);

	NavMesh::ProbeParams probe;
	probe.m_start = startPs;
	probe.m_pStartPoly = findMesh.m_pNavPoly;
	const Locator locPs = apLoc.GetLocatorPs();

	bool valid = true;

	for (U32F iVec = 0; iVec < vecCount; ++iVec)
	{
		probe.m_move = locPs.TransformVector(vecsLs[iVec]);

		const NavMesh::ProbeResult res = pNavMesh->ProbePs(&probe);

		if (res != NavMesh::ProbeResult::kReachedGoal)
		{
			valid = false;
			break;
		}
	}

	if (!valid)
	{
		return nullptr;
	}

	if (pNavPolyPointWs)
		*pNavPolyPointWs = findMesh.m_nearestPointWs;

	if (ppNavMeshOut)
		*ppNavMeshOut = pNavMesh;

	if (ppNavPolyOut)
		*ppNavPolyOut = findMesh.m_pNavPoly;

	return findMesh.m_pNavPoly;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PerchActionPack::RegisterInternal()
{
	const bool readOnly = m_regParams.m_readOnly;

	NAV_ASSERT(readOnly ? NavMeshMgr::GetGlobalLock()->IsLocked() : NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	const NavPoly* pPoly = FindRegisterNavPoly(GetBoundFrame(), m_definition, m_regParams);

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
const Vector PerchActionPack::GetPerchDirectionPs() const
{
	return GetLocalZ(GetLocatorPs().Rot());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PerchActionPack::DebugDraw(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	const PerchDefinition& definition = GetDefinition();
	StringBuilder<256> desc;
	Color descColor = IsRegistered() ? kColorWhite : kColorGrayTrans;

	switch (definition.m_perchType.GetValue())
	{
	case SID_VAL("perch-edge"):
		{
			desc.append_format("PE");
		}
		break;

	case SID_VAL("perch-rail"):
		{
			desc.append_format("PR");
		}
		break;
	}

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
void PerchActionPack::DebugDrawRegistrationProbes() const
{
	STRIP_IN_FINAL_BUILD;
	const Locator locWs = GetLocatorWs();
	Vector probesLs[kRegistrationProbeVectorCount];
	U32F probeCount = GetRegistrationProbeVectorsLs(m_definition, probesLs);
	for (U32F iProbe = 0; iProbe < probeCount; ++iProbe)
	{
		Vector probeVecWs = locWs.TransformVector(probesLs[iProbe]);
		Point posWs = locWs.Pos() + Vector(kUnitYAxis)*0.05f; // lift off the ground a bit to improve visibility
		g_prim.Draw(DebugLine(posWs, posWs + probeVecWs, kColorBlack, kColorWhite, 3.0f));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
size_t PerchActionPack::MakePerchDefinition(const Locator& edgeSpace,
											const FeatureEdge& edge,
											PerchDefinition* pPerchDef,
											Locator* pApLoc,
											const FeatureDb* pFeatureDb)
{
	if (!(edge.GetFlags() & FeatureEdge::kFlagCanHang))
		return 0;

	float edgeLength = Length(edge.GetVert1() - edge.GetVert0());
	if (edgeLength < 0.6)
		return 0;

	int numEdgesToCheck = (int)(edgeLength / 2.0f) + 1;

	if (numEdgesToCheck >= kMaxPerchesPerEdge)
		return 0;

	int numGoodDefinitions = 0;
	for (int i = 0; i < numEdgesToCheck; i++)
	{
		Point vert1 = edgeSpace.TransformPoint(edge.GetVert0());
		Point vert2 = edgeSpace.TransformPoint(edge.GetVert1());
		Point center = vert1 + (vert2 - vert1) * ((float)(i + 1) / (numEdgesToCheck + 1));
		Vector normal = edgeSpace.TransformVector(edge.GetWallNormal());

		StringId64 typeId = SID("perch-edge");
		{
			ScopedTempAllocator jj(FILE_LINE_FUNC);

			if (pFeatureDb)
			{
				ListArray<const FeatureCover*> covers(100);
				int numCovers = pFeatureDb->FindCovers(&covers,
													   MovingSphere(edgeSpace.UntransformPoint(center
																							   + Vector(0, -1.0f, 0)),
																	0.7f,
																	kZero),
													   Locator(kIdentity));

				for (int j = 0; j < covers.Size(); j++)
				{
					const FeatureCover* pCover = covers[j];

					Vector coverNormal = edgeSpace.TransformVector(Vector(pCover->CalculateWallNormal()));
					Point coverVert1 = edgeSpace.TransformPoint(Point(pCover->GetVert0()));
					Point coverVert2 = edgeSpace.TransformPoint(Point(pCover->GetVert1()));

					if (pCover->GetFlags() & FeatureCover::kFlagLow)
					{
						if (Dot(coverNormal, normal) < -0.8f)
						{
							// this edge is a railing, put the perch on this cover instead
							center = ClosestPointOnEdgeToPoint(coverVert1, coverVert2, center) + -0.1f * coverNormal;
							typeId = SID("perch-rail");
						}
					}
				}
			}
			else
			{
				ListArray<CoverInfo> covers(100);
				int numCovers = FindCovers(center + Vector(0, -1.0f, 0), 0.7f, &covers, nullptr);

				for (int j = 0; j < covers.Size(); j++)
				{
					CoverInfo& cover = covers[j];

					if (cover.GetFlags() & FeatureCover::kFlagLow)
					{
						if (Dot(cover.GetWallNormal(), normal) < -0.8f)
						{
							// this edge is a railing, put the perch on this cover instead
							center = ClosestPointOnEdgeToPoint(cover.GetVert0(), cover.GetVert1(), center) + -0.1f * cover.GetWallNormal();
							typeId = SID("perch-rail");
						}
					}
				}
			}
		}

		if (typeId == SID("perch-edge") && !(edge.GetFlags() & FeatureEdge::kFlagCanStand))
			continue;

		PerchDefinition& perchDef = pPerchDef[numGoodDefinitions];
		perchDef.m_perchType = typeId;

		pApLoc[numGoodDefinitions] = Locator(center, QuatFromLookAt(normal, kUnitYAxis));
		numGoodDefinitions++;
	}


	return numGoodDefinitions;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PerchActionPack::IsAvailableFor(const Process* pNavChar) const
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

	if (IsEnabled())
	{
		BoxedValue evtResult = SendEvent(SID("can-share-perch?"), const_cast<Process*>(pNavChar), BoxedValue(this));
		return evtResult.GetAsBool(false);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PerchActionPack::TryAddBlockingNavChar(NavCharacter* pBlockingNavChar)
{
	m_hBlockingNavChar = pBlockingNavChar;
	m_navCharBlockTime = pBlockingNavChar ? pBlockingNavChar->GetCurTime() : TimeFrameNegInfinity();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector PerchActionPack::GetDefaultEntryOffsetLs() const
{
	switch (m_definition.m_perchType.GetValue())
	{
	case SID_VAL("perch-edge"):
		return Vector(kUnitZAxis) * 0.4f;
		break;
	case SID_VAL("perch-rail"):
		return Vector(kUnitZAxis) * 0.4f;
		break;
	}

	return kZero;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavCharacter* PerchActionPack::GetBlockingNavChar() const
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
bool PerchActionPack::CanRegisterSelf(const PerchDefinition& PerchDef,
									  const ActionPackRegistrationParams& params,
									  const BoundFrame& apLoc,
									  const NavMesh** ppNavMeshOut /* = nullptr */,
									  const NavPoly** ppNavPolyOut /* = nullptr */)
{
	return FindRegisterNavPoly(apLoc, PerchDef, params, nullptr, ppNavMeshOut, ppNavPolyOut) != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector PerchActionPack::GetPerchDirectionWs() const
{
	return GetLocalZ(GetLocatorWs().Rot());
}
