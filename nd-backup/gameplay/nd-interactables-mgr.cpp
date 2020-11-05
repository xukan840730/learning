/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-interactables-mgr.h"

#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-probe.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-interactable.h"
#include "gamelib/level/level-mgr.h"

#include "ndlib/nd-frame-state.h"
#include "ndlib/process/debug-selection.h"

static CONST_EXPR float kProbe1Thetas[] = { 11.0f, 38.0f };
static CONST_EXPR float kProbe2Ys_Prone[] = { 0.1f, 0.25f };
static CONST_EXPR float kProbe2Ys_Crouch[] = { 0.3f, 0.6f };
static CONST_EXPR float kProbe2Ys_Stand[] = { 0.5f, 1.3f };
static CONST_EXPR float kProbe1Radii[] = { 0.084f, 0.095f };
static CONST_EXPR float kVerticalProbeRadius = 0.095f;

STATIC_ASSERT(sizeof(kProbe2Ys_Prone) == sizeof(kProbe2Ys_Crouch));
STATIC_ASSERT(sizeof(kProbe2Ys_Crouch) == sizeof(kProbe2Ys_Stand));

CONST_EXPR int kNumRound1ProbesPerCandidate = ARRAY_COUNT(kProbe1Thetas);
CONST_EXPR int kNumRound2ProbesPerRound1 = ARRAY_COUNT(kProbe2Ys_Stand);
CONST_EXPR int kNumRound2ProbesPerCandidate = kNumRound2ProbesPerRound1 * kNumRound1ProbesPerCandidate;

static void RejectCandidate(Point posWs, const char* s, bool fromCasts = false)
{
	if (FALSE_IN_FINAL_BUILD(fromCasts ? g_interactableOptions.m_drawInteractablesMgrCasts : g_interactableOptions.m_drawInteractablesMgrNav))
	{
		g_prim.Draw(DebugSphere(posWs, 0.1f, kColorRed));
		g_prim.Draw(DebugString(posWs, s, 1, kColorRed, 0.6f));
	}
}

enum ProbeResult
{
	kClear,
	kThroughBreakables,
	kBlocked,

	kCount
};

static Color ProbeResultToColor(const ProbeResult r)
{
	switch (r)
	{
	case kClear:
		return kColorGreen;
	case kThroughBreakables:
		return kColorPurple;
	case kBlocked:
		return kColorRed;
	default:
		ALWAYS_HALT();
		return kColorRed;
	}
}

template <class T>
static ProbeResult ComputeProbeResult(const T& job, int iProbe)
{
	ProbeResult ret = kClear;

	const int numContacts = job.NumContacts(iProbe);
	for (int iContact = 0; iContact < numContacts; ++iContact)
	{
		if (job.IsContactValid(iProbe, iContact))
		{
			if (ret != kBlocked)
			{
				ret = kBlocked;

				if (const RigidBody* const pBody = job.GetContactBody(iProbe, iContact))
				{
					if (pBody->IsBreakable())
					{
						ret = kThroughBreakables;
					}
				}
			}
		}
	}

	return ret;
}

static Point GetInteractPos(const NdInteractControl* const pCtrl, const Point userWs = kInvalidPoint)
{
	return pCtrl->GetInteractionQueryTargetWs(nullptr, userWs);
}

void NdInteractablesManager::Gather()
{
	NdGameObject* const pGo = m_hInFlight.ToMutableProcess();

	const bool hasProbeWork = m_sphereJob.IsValid();

	int numInteractNavLocations = 0;
	NavLocation interactNavLocations[NdInteractControl::kMaxInteractNavLocations];
	InteractAgilityMask interactAgilities[NdInteractControl::kMaxInteractNavLocations];

	if (hasProbeWork)
	{
		GAMEPLAY_ASSERT(m_sphereJob.IsValid());
		GAMEPLAY_ASSERT(m_rayJob.IsValid());

		m_sphereJob.Wait();
		m_rayJob.Wait();
	}
	else
	{
		GAMEPLAY_ASSERT(!m_sphereJob.IsValid());
		GAMEPLAY_ASSERT(!m_rayJob.IsValid());
	}

	if (pGo)
	{
		NdInteractControl* const pCtrl = pGo->GetInteractControl();
		GAMEPLAY_ASSERT(pCtrl);

		bool reachableWithoutProne = false;

		if (hasProbeWork)
		{
			const int numRound1Probes = m_sphereJob.NumProbes();
			const int numRound2Probes = m_rayJob.NumProbes();

			GAMEPLAY_ASSERT(numRound1Probes);
			GAMEPLAY_ASSERT(numRound2Probes);

			if (m_doVerticalProbe)
			{
				GAMEPLAY_ASSERT((numRound1Probes - 1) % kNumRound1ProbesPerCandidate == 0);
				GAMEPLAY_ASSERT(numRound2Probes % (kNumRound2ProbesPerCandidate + 1) == 0);
				GAMEPLAY_ASSERT((numRound1Probes - 1) / kNumRound1ProbesPerCandidate == numRound2Probes / (kNumRound2ProbesPerCandidate + 1));
			}
			else
			{
				GAMEPLAY_ASSERT(numRound1Probes % kNumRound1ProbesPerCandidate == 0);
				GAMEPLAY_ASSERT(numRound2Probes % kNumRound2ProbesPerCandidate == 0);
				GAMEPLAY_ASSERT(numRound1Probes / kNumRound1ProbesPerCandidate == numRound2Probes / kNumRound2ProbesPerCandidate);
			}

			const int numCandidates = (numRound1Probes - (m_doVerticalProbe ? 1 : 0)) / kNumRound1ProbesPerCandidate;
			for (int iCandidate = 0; iCandidate < numCandidates; ++iCandidate)
			{
				const int round1IdxBase = iCandidate * kNumRound1ProbesPerCandidate;

				// best == min
				// worst == max
				// each candidate's result is the best result among overall results,
				// where each overall result is the worst result between its round 1 result and (the best result among round 2 results)

				ProbeResult bestResult = kBlocked;
				for (int iRound1Probe = 0; iRound1Probe < kNumRound1ProbesPerCandidate + (m_doVerticalProbe ? 1 : 0); ++iRound1Probe)
				{
					const bool isVerticalProbe = iRound1Probe == kNumRound1ProbesPerCandidate;

					const int round1Idx = isVerticalProbe ? kNumRound1ProbesPerCandidate * numCandidates : round1IdxBase + iRound1Probe;
					const ProbeResult round1Result = ComputeProbeResult(m_sphereJob, round1Idx);

					if (isVerticalProbe && m_proneProbes.IsBitSet(iCandidate))
						continue;

					if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractablesMgrCasts))
					{
						if (!isVerticalProbe || !iCandidate)
						{
							const Point probe1StartWs = m_sphereJob.GetProbeStart(round1Idx);
							const Point probe1EndWs = m_sphereJob.GetProbeEnd(round1Idx);
							const float probeRadius = m_sphereJob.GetProbeRadius(round1Idx);
							const Color color = ProbeResultToColor(round1Result);
							//g_prim.Draw(DebugCapsule(probe1StartWs, probe1EndWs, probeRadius, color, 2.0f));
							g_prim.Draw(DebugLine(probe1StartWs, probe1EndWs, color, probeRadius * 200.0f));
							g_prim.Draw(DebugSphere(probe1StartWs, probeRadius, color));
							g_prim.Draw(DebugSphere(probe1EndWs, probeRadius, color));
						}
					}

					ProbeResult bestRound2Result = kBlocked;
					const int round2IdxBase = isVerticalProbe ? numCandidates * kNumRound2ProbesPerCandidate + iCandidate : iCandidate * kNumRound2ProbesPerCandidate + iRound1Probe * kNumRound2ProbesPerRound1;

					if (isVerticalProbe && DistXzSqr(m_rayJob.GetProbeStart(round2IdxBase), m_rayJob.GetProbeEnd(round2IdxBase)) > Sqr(0.75f))
						continue;

					for (int iRound2Probe = 0; iRound2Probe < (isVerticalProbe ? 1 : kNumRound2ProbesPerRound1); ++iRound2Probe)
					{
						const int round2Idx = round2IdxBase + iRound2Probe;

						const ProbeResult round2Result = ComputeProbeResult(m_rayJob, round2Idx);

						if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractablesMgrCasts))
						{
							const Point probe2StartWs = m_rayJob.GetProbeStart(round2Idx);
							const Point probe2EndWs = m_rayJob.GetProbeEnd(round2Idx);
							const Color color = ProbeResultToColor(round2Result);
							g_prim.Draw(DebugLine(probe2StartWs, probe2EndWs, color, 4.0f));
						}

						if (round2Result < bestRound2Result)
							bestRound2Result = round2Result;
					}

					const ProbeResult overallResult = (ProbeResult)Max(round1Result, bestRound2Result);
					if (overallResult < bestResult)
						bestResult = overallResult;
				}

				if (bestResult != kBlocked)
				{
					interactNavLocations[numInteractNavLocations] = m_candidateNavLocs[iCandidate];

					InteractAgilityMask agility = InteractAgilityMask::kNone;
					if (bestResult == ProbeResult::kThroughBreakables)
						agility = (InteractAgilityMask)((U32)agility | (U32)InteractAgilityMask::kBreakable);

					if (m_proneProbes.IsBitSet(iCandidate))
						agility = (InteractAgilityMask)((U32)agility | (U32)InteractAgilityMask::kProne);
					else
						reachableWithoutProne = true;

					interactAgilities[numInteractNavLocations] = agility;

					++numInteractNavLocations;
				}

				if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractablesMgrCasts))
				{
					if (bestResult == kBlocked)
					{
						NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
						const Point candidatePosWs = m_candidateNavLocs[iCandidate].GetPosWs();
						RejectCandidate(candidatePosWs, "kCastsBlocked", true);
					}
				}
			}
		}

		if (reachableWithoutProne)
		{
			// remove all the prone candidates

			int newNumInteractNavLocations = 0;
			NavLocation newInteractNavLocations[NdInteractControl::kMaxInteractNavLocations];
			InteractAgilityMask newInteractAgilities[NdInteractControl::kMaxInteractNavLocations];

			for (int iCandidate = 0; iCandidate < numInteractNavLocations; ++iCandidate)
			{
				if (((U32)interactAgilities[iCandidate] & (U32)InteractAgilityMask::kProne) == 0)
				{
					newInteractNavLocations[newNumInteractNavLocations] = interactNavLocations[iCandidate];
					newInteractAgilities[newNumInteractNavLocations] = interactAgilities[iCandidate];
					++newNumInteractNavLocations;
				}
			}

			for (int iCandidate = 0; iCandidate < newNumInteractNavLocations; ++iCandidate)
			{
				interactNavLocations[iCandidate] = newInteractNavLocations[iCandidate];
				interactAgilities[iCandidate] = newInteractAgilities[iCandidate];
			}

			numInteractNavLocations = newNumInteractNavLocations;
		}

		if (numInteractNavLocations == 0)
		{
			// didn't find anything systemically. check for overrides

			if (const EntitySpawner* const pGoSpawner = pGo->GetSpawner())
			{
				struct TagAgility
				{
					StringId64 tag;
					InteractAgilityMask mask;
				};

				static CONST_EXPR const TagAgility kPairs[] = {
					{ SID("override-goal")				, InteractAgilityMask::kNone },
					{ SID("override-goal-2")			, InteractAgilityMask::kNone },
					{ SID("override-goal-3")			, InteractAgilityMask::kNone },
					{ SID("override-goal-4")			, InteractAgilityMask::kNone },
					{ SID("override-goal-5")			, InteractAgilityMask::kNone },
					{ SID("override-goal-6")			, InteractAgilityMask::kNone },
					{ SID("override-goal-7")			, InteractAgilityMask::kNone },
					{ SID("override-goal-8")			, InteractAgilityMask::kNone },
					{ SID("override-goal-9")			, InteractAgilityMask::kNone },
					{ SID("override-goal-10")			, InteractAgilityMask::kNone },
					{ SID("override-goal-11")			, InteractAgilityMask::kNone },
					{ SID("override-prone-goal")		, InteractAgilityMask::kProne },
					{ SID("override-prone-goal-2")		, InteractAgilityMask::kProne },
				};
				STATIC_ASSERT(ARRAY_COUNT(kPairs) <= NdInteractControl::kMaxInteractNavLocations);

				NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

				for (const TagAgility& pair : kPairs)
				{
					if (const EntityDB::Record* const pRec = pGoSpawner->GetRecord(pair.tag))
					{
						const StringId64 posSpawnerId = pRec->GetData<StringId64>(INVALID_STRING_ID_64);
						if (posSpawnerId != INVALID_STRING_ID_64)
						{
							if (const EntitySpawner* const pPosSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByNameId(posSpawnerId))
							{
								const Point overridePosWs = pPosSpawner->GetWorldSpaceLocator().GetPosition();
								GAMEPLAY_ASSERT(IsReasonable(overridePosWs));

								if (DistSqr(overridePosWs, pGo->GetTranslation()) < Sqr(30.0f))
								{
									FindBestNavMeshParams params;
									params.m_pointWs = overridePosWs;
									params.m_cullDist = 0.40f;
									params.m_yThreshold = 0.50f;
									EngineComponents::GetNavMeshMgr()->FindNavMeshWs(&params);
									if (params.m_pNavPoly)
									{
										interactNavLocations[numInteractNavLocations].SetWs(params.m_nearestPointWs, params.m_pNavPoly);
										interactAgilities[numInteractNavLocations] = InteractAgilityMask((U32)pair.mask | (U32)InteractAgilityMask::kOverride);

										++numInteractNavLocations;
									}
								}
							}
						}
					}
				}
			}
		}

		{
			AtomicLockJanitorWrite interactNavLocationsLock(&pCtrl->m_interactNavLocationsLock, FILE_LINE_FUNC);
			pCtrl->m_interactNavLocationsLastUpdateFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
			pCtrl->m_numInteractNavLocations = numInteractNavLocations;
			for (int i = 0; i < numInteractNavLocations; ++i)
			{
				pCtrl->m_interactNavLocations[i] = interactNavLocations[i];
				pCtrl->m_interactAgilities[i] = interactAgilities[i];
			}
		}
	}

	if (hasProbeWork)
	{
		m_sphereJob.Close();
		m_rayJob.Close();
	}
}

static Color GetColor(float t)
{
	//return LumLerp(kColorBlue, kColorGreen, t);
	//return t < 0.5f ? LumLerp(kColorBlue, kColorCyan, 2.0f * t) : LumLerp(kColorCyan, kColorGreen, 2.0f * (t - 0.5f));
	return t < 0.5f ? LumLerp(kColorPurple, kColorBlue, 2.0f * t) : LumLerp(kColorBlue, kColorGreen, 2.0f * (t - 0.5f));
}

static void DrawStage(Point a, Point b, float t, float arrowWidth, const char* s)
{
	if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractablesMgrNav))
	{
		CONST_EXPR float kArrowLabelSize = 0.55f;

		g_prim.Draw(DebugArrow(a, b, GetColor(t), arrowWidth));
		g_prim.Draw(DebugString(Lerp(a, b, 0.5f), s, 1, GetColor(t), kArrowLabelSize));
	}
}

static void DrawLabel(const NdGameObject* const pGo, const Point indepPosWs)
{
	if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractablesMgrNav))
	{
		const Color color = LumLerp(kColorYellow, kColorPink, 0.35f);
		g_prim.Draw(DebugString(indepPosWs + Vector(0.0f, 0.02f, 0.0f), DevKitOnly_StringIdToString(pGo->GetUserId()), 1, color, 0.6f));
	}
}

void NdInteractablesManager::Kick(const NdGameObject* const pGo)
{
	GAMEPLAY_ASSERT(pGo);

	const NdInteractControl* const pCtrl = pGo->GetInteractControl();
	GAMEPLAY_ASSERT(pCtrl);

	if (pGo->GetUserId() == SID("item-pickup-rag-small-233"))
	{
		printf("foo\n");
	}

	// base interaction pos independent of pickup location
	const Point indepPosWs = GetInteractPos(pCtrl);
	GAMEPLAY_ASSERT(IsReasonable(indepPosWs));
	if (!IsReasonable(indepPosWs))
		return;

	DrawLabel(pGo, indepPosWs);

	const StringId64 regionNameId = pCtrl->GetInteractionRegionNameId();
	const Region* const pRegion = regionNameId != INVALID_STRING_ID_64 ? EngineComponents::GetLevelMgr()->GetRegionByName(regionNameId) : nullptr;

	STATIC_ASSERT(kMaxCandidateNavLocs <= NdInteractControl::kMaxInteractNavLocations);
	int numCandidates = 0;

	m_proneProbes.ClearAllBits();

	CONST_EXPR float kRingR = 0.85f;
	CONST_EXPR float kOptimalR = 0.60f;
	CONST_EXPR float kFindMeshR = 0.40f;
	CONST_EXPR float kDepenRadius = 0.255f;
	CONST_EXPR float kMinR = 0.10f;
	CONST_EXPR float kMaxR = 1.05f;

	// normal mesh tests
	CONST_EXPR float yStandMeshMin = -2.73f;
	CONST_EXPR float yStandMeshMax = 0.35f;
	CONST_EXPR float yStandMeshBias = 0.75f;

	// swim mesh tests
	CONST_EXPR float ySwimMeshMin = -1.00f;
	CONST_EXPR float ySwimMeshMax = 1.00f;
	CONST_EXPR float ySwimMeshBias = 0.0f;

	// crouch mesh tests
	CONST_EXPR float yCrouchMeshMin = -1.5f;
	CONST_EXPR float yCrouchMeshMax = 0.35f;
	CONST_EXPR float yCrouchMeshBias = 0.37f;

	// prone mesh tests
	CONST_EXPR float yProneMeshMin = -1.0f;
	CONST_EXPR float yProneMeshMax = 0.35f;
	CONST_EXPR float yProneMeshBias = 0.18f;

	// prone queries, not necessarily on prone mesh
	CONST_EXPR float kProneQueryMaxR = 2.0f;
	CONST_EXPR float kProneQueryMinY = -0.35f;
	CONST_EXPR float kProneQueryMaxY = 0.5f;

	CONST_EXPR float yDelta = 0.5f * (yStandMeshMax - yStandMeshMin);
	CONST_EXPR float yCtr = yStandMeshMin + yDelta;
	CONST_EXPR float yBias = yStandMeshBias;

	CONST_EXPR float kArrowWidth = 0.3f;

	CONST_EXPR Nav::StaticBlockageMask kStaticBlockageMask = Nav::kStaticBlockageMaskNone;

	bool allInteractPositionsAreSimilar = true;
	Point candidateInteractPos[kMaxCandidateNavLocs];
	Point candidateNavPos[kMaxCandidateNavLocs];

	// need to release this read lock before kicking casts as cast kick can sleep
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		ScopedTempAllocator scopedTempAllocJanitor(FILE_LINE_FUNC);
		for (int i = 0; i < kMaxCandidateNavLocs; ++i)
		{
			// step 1: create indep ring
			const float theta = i * PI_TIMES_2 / kMaxCandidateNavLocs;
			const Vector moveDir(Sin(theta), 0.0f, Cos(theta));
			const Point indepRingWs = indepPosWs + kRingR * moveDir;
			const Point depPosWs = GetInteractPos(pCtrl, indepRingWs);
			GAMEPLAY_ASSERT(IsReasonable(depPosWs));
			if (DistSqr(depPosWs, indepPosWs) > 0.02f)
				allInteractPositionsAreSimilar = false;

			DrawStage(indepPosWs, depPosWs, 0.0f, kArrowWidth, "interact pos");


			// step 2: create dep ring

			const Point ringWs = depPosWs + kRingR * moveDir;

			DrawStage(depPosWs, ringWs, 0.2f, kArrowWidth, "ring");


			// step 3: find mesh
			const NavPoly* pPoly;
			const NavMesh* pMesh;
			Point navPtWs;
			{
				FindBestNavMeshParams params;
				params.m_pointWs = ringWs + Vector(0.0f, yCtr + yBias, 0.0f);
				params.m_cullDist = kFindMeshR;
				params.m_yThreshold = yDelta + yBias;
				params.m_yCeiling = yCtr + yDelta;
				params.m_obeyedStaticBlockers = kStaticBlockageMask;
				EngineComponents::GetNavMeshMgr()->FindNavMeshWs(&params);
				pPoly = params.m_pNavPoly;

				if (!pPoly)
				{
					RejectCandidate(ringWs, "kNoMesh");
					continue;
				}
				pMesh = params.m_pNavMesh;
				GAMEPLAY_ASSERT(pMesh);
				navPtWs = params.m_nearestPointWs;
			}

			DrawStage(ringWs, navPtWs, 0.4f, kArrowWidth, "find mesh");

			// step 4: depen
			Point depenPtWs;
			{
				NavMesh::FindPointParams params;
				params.m_pStartPoly = pPoly;
				params.m_crossLinks = true;
				params.m_depenRadius = params.m_searchRadius = kDepenRadius + 0.025f;
				params.m_obeyedStaticBlockers = kStaticBlockageMask;

				NavMeshDepenetrator2 probe;
				probe.Init(pMesh->WorldToLocal(navPtWs), pMesh, params);
				probe.Execute(pMesh, pPoly, nullptr);

				pPoly = probe.GetResolvedPoly();
				if (!pPoly)
				{
					RejectCandidate(navPtWs, "kDepenFail");
					continue;
				}

				// Ls of START mesh
				depenPtWs = pMesh->LocalToWorld(probe.GetResolvedPosLs());

				pMesh = pPoly->GetNavMesh();
				GAMEPLAY_ASSERT(pMesh);
			}

			DrawStage(navPtWs, depenPtWs, 0.6f, kArrowWidth, "depen");

			// step 5: radial probe toward optimal interact distance
			Point probePtWs;
			{
				const Point goalWs = PointFromXzAndY(depPosWs + kOptimalR * moveDir, navPtWs);
				const float probeDistSqr = DistXzSqr(depenPtWs, goalWs);
				if (probeDistSqr < Sqr(0.03f))
				{
					probePtWs = depenPtWs;
				}
				else
				{
					const Point depenPtPs = pMesh->WorldToParent(depenPtWs);
					const Point goalPs = pMesh->WorldToParent(goalWs);

					NavMesh::ProbeParams params;
					params.m_start = params.m_endPoint = depenPtPs;
					params.m_pStartPoly = pPoly;
					params.m_move = goalPs - depenPtPs;
					params.m_probeRadius = kDepenRadius;
					params.m_obeyedStaticBlockers = kStaticBlockageMask;
					const NavMesh::ProbeResult res = pMesh->ProbePs(&params);

					if (res != NavMesh::ProbeResult::kHitEdge && res != NavMesh::ProbeResult::kReachedGoal)
					{
						RejectCandidate(depenPtWs, "kProbeFailed");
						continue;
					}

					pPoly = params.m_pReachedPoly;
					pMesh = pPoly->GetNavMesh();
					const Point probePtPs = params.m_endPoint;
					probePtWs = pMesh->ParentToWorld(probePtPs);
				}
			}

			DrawStage(depenPtWs, probePtWs, 0.8f, kArrowWidth, "probe");

			// step 6: heightmap-based y refinement
			const Point resWs = FindYWithFallbackWs(pMesh, pPoly, probePtWs);

			DrawStage(probePtWs, resWs, 1.0f, kArrowWidth, "refine y");

			const bool isWaterMesh = pMesh->NavMeshForcesDive() || pMesh->NavMeshForcesSwim();
			const bool isCrouchMesh = pMesh->NavMeshForcesCrouch();
			const bool isProneMesh = pMesh->NavMeshForcesProne();
			const float yDist = (resWs - depPosWs).Y();
			if (isWaterMesh)
			{
				if (yDist < ySwimMeshMin - 0.02f)
				{
					RejectCandidate(resWs, "kTooLowWater");
					continue;
				}

				if (yDist > ySwimMeshMax + 0.02f)
				{
					RejectCandidate(resWs, "kTooHighWater");
					continue;
				}
			}
			else if (isCrouchMesh)
			{
				if (yDist < yCrouchMeshMin - 0.02f)
				{
					RejectCandidate(resWs, "kTooLowCrouchMesh");
					continue;
				}

				if (yDist > yCrouchMeshMax + 0.02f)
				{
					RejectCandidate(resWs, "kTooHighCrouchMesh");
					continue;
				}
			}
			else if (isProneMesh)
			{
				if (yDist < yProneMeshMin - 0.02f)
				{
					RejectCandidate(resWs, "kTooLowProneMesh");
					continue;
				}

				if (yDist > yProneMeshMax + 0.02f)
				{
					RejectCandidate(resWs, "kTooHighProneMesh");
					continue;
				}
			}
			else
			{
				if (yDist < yStandMeshMin - 0.02f)
				{
					RejectCandidate(resWs, "kTooLow");
					continue;
				}

				if (yDist > yStandMeshMax + 0.02f)
				{
					RejectCandidate(resWs, "kTooHigh");
					continue;
				}
			}

			const float xzDist = DistXz(resWs, depPosWs);

			if (xzDist < kMinR)
			{
				RejectCandidate(resWs, "kTooCloseXz");
				continue;
			}

			if (xzDist > kMaxR)
			{
				if (xzDist < kProneQueryMaxR && yDist > kProneQueryMinY && yDist < kProneQueryMaxY)
				{
					// try for prone
					m_proneProbes.SetBit(numCandidates);

					if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractablesMgrNav))
					{
						//g_prim.Draw(DebugSphere(resWs, 0.1f, kColorYellow));
						g_prim.Draw(DebugString(resWs, "prone query", 1, kColorYellow, 0.6f));
					}
				}
				else
				{
					RejectCandidate(resWs, "kTooFarXz");
					continue;
				}
			}

			if (pRegion && !pRegion->IsInside(resWs + Vector(0.0f, 0.2f, 0.0f), 0.0f) && !pRegion->IsInside(resWs + Vector(0.0f, 0.5f, 0.0f), 0.0f))
			{
				RejectCandidate(resWs, "kOutsideInteractRegion");
				continue;
			}

			GAMEPLAY_ASSERT(numCandidates < kMaxCandidateNavLocs);
			if (numCandidates >= kMaxCandidateNavLocs)
				break;

			candidateInteractPos[numCandidates] = depPosWs;
			candidateNavPos[numCandidates] = resWs;
			NavLocation& navLoc = m_candidateNavLocs[numCandidates++];
			navLoc.SetWs(resWs, pPoly);
		}

		if (numCandidates == 0)
			return;

		// init probes
		m_doVerticalProbe = allInteractPositionsAreSimilar;

		CONST_EXPR int kMaxHitsPerProbe = 8;

		const int numExpectedSphereProbes = kNumRound1ProbesPerCandidate * numCandidates + (m_doVerticalProbe ? 1 : 0);

		GAMEPLAY_ASSERT(!m_sphereJob.IsValid());
		m_sphereJob.Open(numExpectedSphereProbes, kMaxHitsPerProbe,
			ICollCastJob::kCollCastUseGpu |
			ICollCastJob::kCollCastSingleFrame |
			ICollCastJob::kCollCastAllStartPenetrations |
			ICollCastJob::kCollCastAllowMultipleResults |
			//ICollCastJob::kCollCastAnyHits, // nope gotta catch 'em all to check for breakables
			ICollCastJob::kClientNpc // playermisc would also be acceptable
		);

		const int numExpectedRayProbes = (kNumRound2ProbesPerCandidate + (m_doVerticalProbe ? 1 : 0)) * numCandidates;

		GAMEPLAY_ASSERT(!m_rayJob.IsValid());
		m_rayJob.Open(numExpectedRayProbes, kMaxHitsPerProbe,
			ICollCastJob::kCollCastUseGpu |
			ICollCastJob::kCollCastSingleFrame |
			//ICollCastJob::kCollCastAnyHits, // nope gotta catch 'em all to check for breakables
			ICollCastJob::kClientNpc // playermisc would also be acceptable
		);

		// kick a bunch of rays to make sure we could actually reach this interact
		CONST_EXPR float kProbe1VertOffset = 0.058f;
		const Vector probe1Ofs = Vector(0.0f, kProbe1VertOffset, 0.0f);

		// standard per-candidate probes
		for (int iCandidate = 0; iCandidate < numCandidates; ++iCandidate)
		{
			const NavLocation& candidateNavLoc = m_candidateNavLocs[iCandidate];
			const NavMesh* const pMesh = candidateNavLoc.ToNavMesh();
			const float* pProbe2Ys = kProbe2Ys_Stand;
			if (pMesh && pMesh->NavMeshForcesCrouch())
				pProbe2Ys = kProbe2Ys_Crouch;
			if (pMesh && pMesh->NavMeshForcesProne())
				pProbe2Ys = kProbe2Ys_Prone;

			const int round1IdxBase = iCandidate * kNumRound1ProbesPerCandidate;

			const Point interactPosWs = candidateInteractPos[iCandidate];
			const Point candidatePosWs = candidateNavPos[iCandidate];
			const Vector toCandidateXzWs = VectorXz(candidatePosWs - interactPosWs);
			const float lenToCandidateXz = LengthXz(toCandidateXzWs);
			GAMEPLAY_ASSERT(lenToCandidateXz >= kMinR);

			const Vector dirWs = toCandidateXzWs / lenToCandidateXz;
			const Vector axisWs = Vector(-dirWs.Z(), 0.0f, dirWs.X());

			// any of n probes outward toward candidate, AND any of n probes from those endpoints to candidate crouch/stand.
			// NB: start away from the candidate a bit

			for (int iRound1Probe = 0; iRound1Probe < kNumRound1ProbesPerCandidate; ++iRound1Probe)
			{
				const int round2IdxBase = iCandidate * kNumRound2ProbesPerCandidate + iRound1Probe * kNumRound2ProbesPerRound1;

				const float theta = kProbe1Thetas[iRound1Probe];
				const float thetaRad = DEGREES_TO_RADIANS(theta);
				const float probeRadius = kProbe1Radii[iRound1Probe];
				const Vector probeDirWs = RotateVectorAbout(dirWs, axisWs, thetaRad);
				CONST_EXPR float kProbe1StartDist = 0.210f;
				CONST_EXPR float kProbe1EndDist = 0.57f;

				const Point probe1StartWs = interactPosWs + probe1Ofs + kProbe1StartDist * probeDirWs;
				const Point probe1EndWs = interactPosWs + probe1Ofs + kProbe1EndDist * probeDirWs;

				m_sphereJob.SetProbeExtents(round1IdxBase + iRound1Probe, probe1StartWs, probe1EndWs);
				m_sphereJob.SetProbeRadius(round1IdxBase + iRound1Probe, probeRadius);
				//g_prim.Draw(DebugLine(probe1StartWs, probe1EndWs, kColorWhite, kColorPink, 2.0f));

				for (int iRound2Probe = 0; iRound2Probe < kNumRound2ProbesPerRound1; ++iRound2Probe)
				{
					const float y = pProbe2Ys[iRound2Probe];
					const Point probe2StartWs = probe1EndWs;
					const Point probe2EndWs = candidatePosWs + Vector(0.0f, y, 0.0f);

					m_rayJob.SetProbeExtents(round2IdxBase + iRound2Probe, probe2StartWs, probe2EndWs);
					//g_prim.Draw(DebugLine(probe2StartWs, probe2EndWs, kColorPink, kColorRed, 2.0f));
				}
			}
		}

		// special vertical pickup probe - because it's vertical, it doesn't need per-candidate round 1
		if (m_doVerticalProbe)
		{
			const int round1IdxBase = kNumRound1ProbesPerCandidate * numCandidates;

			const Vector probeDirWs = kUnitYAxis;
			CONST_EXPR float kProbe1StartDist = 0.150f;
			CONST_EXPR float kProbe1EndDist = 0.57f;
			CONST_EXPR float kProbe2StartDist = 0.37f;

			const Point probe1StartWs = indepPosWs + probe1Ofs + kProbe1StartDist * probeDirWs;
			const Point probe1EndWs = indepPosWs + probe1Ofs + kProbe1EndDist * probeDirWs;

			m_sphereJob.SetProbeExtents(round1IdxBase, probe1StartWs, probe1EndWs);
			m_sphereJob.SetProbeRadius(round1IdxBase, kVerticalProbeRadius);
			//g_prim.Draw(DebugLine(probe1StartWs, probe1EndWs, kColorWhite, kColorPink, 2.0f));

			const int round2IdxBase = kNumRound2ProbesPerCandidate * numCandidates;

			for (int iCandidate = 0; iCandidate < numCandidates; ++iCandidate)
			{
				const Point candidatePosWs = candidateNavPos[iCandidate];

				const Point probe2StartWs = indepPosWs + probe1Ofs + kProbe2StartDist * probeDirWs;
				const Point probe2EndWs = candidatePosWs + Vector(0.0f, 0.65f, 0.0f);

				m_rayJob.SetProbeExtents(round2IdxBase + iCandidate, probe2StartWs, probe2EndWs);
				//g_prim.Draw(DebugLine(probe2StartWs, probe2EndWs, kColorPink, kColorRed, 2.0f));
			}
		}
	}

	// layer include, pat exclude, ignore object
	const CollideFilter filter(Collide::kLayerMaskGeneral, Pat(Pat::kPickUpThroughMask | Pat::kPassThroughMask), nullptr/*pGo*/);
	m_sphereJob.SetFilterForAllProbes(filter);
	m_rayJob.SetFilterForAllProbes(filter);
	m_sphereJob.Kick(FILE_LINE_FUNC);
	m_rayJob.Kick(FILE_LINE_FUNC);
}

JOB_ENTRY_POINT(UpdateFunc)
{
	g_ndInteractablesMgr.Update();
}

void NdInteractablesManager::KickUpdateJob()
{
	ALWAYS_ASSERT(!m_hJobCounter);

	ndjob::JobDecl jobDecl(UpdateFunc, 0);
	ndjob::RunJobs(&jobDecl, 1, &m_hJobCounter, FILE_LINE_FUNC, ndjob::Priority::kGameFrameBelowNormal);
}

JOB_ENTRY_POINT(PostRenderFunc)
{
	g_ndInteractablesMgr.PostRender();
}

ndjob::CounterHandle NdInteractablesManager::KickPostRenderJob()
{
	ALWAYS_ASSERT(m_hJobCounter);

	ndjob::CounterHandle pCounter = nullptr;

	ndjob::JobDecl jobDecl(PostRenderFunc, 0);
	jobDecl.m_dependentCounter = m_hJobCounter;
	ndjob::RunJobs(&jobDecl, 1, &pCounter, FILE_LINE_FUNC, ndjob::Priority::kGameFrameNormal);

	return pCounter;
}

void NdInteractablesManager::PostRender()
{
	PROFILE(Processes, NdInteractablesMgrGather);

	ALWAYS_ASSERT(m_hJobCounter);
	ALWAYS_ASSERT(m_hJobCounter->GetValue() == 0);
	FreeCounter(m_hJobCounter);
	m_hJobCounter = nullptr;

	Gather();

	DebugDraw();
}

void NdInteractablesManager::Update()
{
	PROFILE(Processes, NdInteractablesMgrUpdate);

	VerifyInternalState();

	NdGameObject* pBestGo = nullptr;

	if (EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
		pBestGo = m_hInFlight.ToMutableProcess();

	if (!pBestGo)
	{
		// take lock
		// score interactables
		// release lock
		// begin processing highest-scoring interactable

		const I64 curFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
		float bestScore = -NDI_FLT_MAX;

		const NdGameObjectHandle hPlayer = EngineComponents::GetNdGameInfo()->GetPlayerGameObjectHandle();
		const NdGameObjectSnapshot* const pPlayerSnapshot = hPlayer.ToSnapshot<NdGameObjectSnapshot>();
		const bool playerPosValid = pPlayerSnapshot;
		const Point playerPosWs = playerPosValid ? pPlayerSnapshot->GetTranslation() : kInvalidPoint;

		AtomicLockJanitorRead_Jls jj(&m_ndInteractablesMgrLock, FILE_LINE_FUNC);

		for (const U32 i : m_regBits)
		{
			const MutableNdGameObjectHandle& hGo = m_ahReg[i];
			NdGameObject* const pGo = hGo.ToMutableProcess();
			GAMEPLAY_ASSERT(pGo);

			const NdInteractControl* const pCtrl = pGo->GetInteractControl();
			GAMEPLAY_ASSERT(pCtrl);

			if (!pCtrl->IsInteractionEnabled() && !pCtrl->EvaluateForInteractablesMgrEvenWhenDisabled())
				continue;

			float score = (float)(curFrame - pCtrl->m_interactNavLocationsLastUpdateFrame);
			if (playerPosValid)
				score -= 2.8f * Dist(playerPosWs, pGo->GetTranslation());

			score += LerpScale(0.0f, 0.35f, 0.0f, 35.0f, Length(pGo->GetVelocityWs()));

			if (score > bestScore)
			{
				bestScore = score;
				pBestGo = pGo;
			}
		}
	}

#ifdef JBELLOMY
	extern bool NavTesterActive();
	if (FALSE_IN_FINAL_BUILD(NavTesterActive()))
	{
		pBestGo = nullptr;
	}
#endif

	if (pBestGo)
		Kick(pBestGo);

	m_hInFlight = pBestGo;
}

static Color AgilityToColor(const InteractAgilityMask agility)
{
	if (agility == InteractAgilityMask::kNone)
		return kColorGreen;

	if (agility == InteractAgilityMask::kBreakable)
		return kColorPurple;

	if (agility == InteractAgilityMask::kProne)
		return kColorYellow;

	if ((U32)agility == ((U32)InteractAgilityMask::kBreakable | (U32)InteractAgilityMask::kProne))
		return kColorPink;

	if (agility == InteractAgilityMask::kOverride)
		return kColorCyan;

	return kColorBlack;
}

void NdInteractablesManager::DebugDraw() const
{
	if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractablesMgr))
	{
		AtomicLockJanitorRead_Jls jj(&m_ndInteractablesMgrLock, FILE_LINE_FUNC);

		StringBuilder<kMaxReg / 2> sb("Registered Interactables: %d\n\n", (int)m_regBits.CountSetBits());

		const U64 blockCount = m_regBits.GetNumBlocks();
		for (U64 iBlock = 0; iBlock < blockCount; ++iBlock)
		{
			U64 s[3];
			sprintf((char*)s, "%016llX", m_regBits.GetBlock(iBlock));
			U64 tmp = __bswap64(s[0]);
			s[0] = __bswap64(s[1]);
			s[1] = tmp;
			sb.append_format("%s%c", (char*)s, (iBlock & 3) == 3 ? '\n' : ' ');
		}

		MsgCon(sb.c_str());

		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		for (const U32 i : m_regBits)
		{
			const NdGameObjectHandle& hGo = m_ahReg[i];
			const NdGameObject* const pGo = hGo.ToProcess();
			GAMEPLAY_ASSERT(pGo);

			const NdInteractControl* const pCtrl = pGo->GetInteractControl();
			GAMEPLAY_ASSERT(pCtrl);

			if (!pCtrl->IsInteractionEnabled() && !pCtrl->EvaluateForInteractablesMgrEvenWhenDisabled())
				continue;

			// independent pos
			const Point indepPosWs = GetInteractPos(pCtrl);
			if (!IsReasonable(indepPosWs))
				continue;

			AtomicLockJanitorRead interactNavLocationsLock(&pCtrl->m_interactNavLocationsLock, FILE_LINE_FUNC);

			const I64 curUpdatedFrame = pCtrl->m_interactNavLocationsLastUpdateFrame;

			const bool neverUpdated = curUpdatedFrame == -1;
			const bool unreachable = pCtrl->m_numInteractNavLocations == 0;
			bool unreachableWithoutOverrides = true;
			for (int iNavLoc = 0; iNavLoc < pCtrl->m_numInteractNavLocations; ++iNavLoc)
			{
				if (!((U32)pCtrl->m_interactAgilities[iNavLoc] & (U32)InteractAgilityMask::kOverride))
					unreachableWithoutOverrides = false;
			}

			const I64 framesAgo = EngineComponents::GetNdFrameState()->m_gameFrameNumber - curUpdatedFrame;

			ScreenSpaceTextPrinter printer(indepPosWs);
			printer.SetFlags(1);

			if (neverUpdated)
			{
				printer.SetScale(0.6f);
				printer.PrintText(kColorOrange, "Last updated: never");
			}
			else
			{
				if (unreachable)
				{
					printer.SetScale(0.9f);
					printer.PrintText(kColorRed, "UNREACHABLE\n");
				}
				else if (unreachableWithoutOverrides)
				{
					printer.SetScale(0.9f);
					printer.PrintText(LumLerp(kColorOrange, kColorRed, 0.5f), "ONLY REACHABLE VIA OVERRIDE\n");
				}

				if (unreachableWithoutOverrides || !g_interactableOptions.m_drawInteractablesMgrUnreachablesOnly)
				{
					if (pCtrl->IgnoresReachableProbes())
					{
						printer.SetScale(0.9f);
						printer.PrintText(LumLerp(kColorOrange, kColorRed, 0.7f), "IGNORES PROBES\n");
					}

					printer.SetScale(0.6f);
					printer.PrintText(kColorGreen, "Last updated %lld frame%c ago", framesAgo, framesAgo == 1 ? ' ' : 's');
				}
			}

			if (unreachableWithoutOverrides || !g_interactableOptions.m_drawInteractablesMgrUnreachablesOnly)
			{
				for (int iNavLoc = 0; iNavLoc < pCtrl->m_numInteractNavLocations; ++iNavLoc)
				{
					const InteractAgilityMask agility = pCtrl->m_interactAgilities[iNavLoc];
					const Color color = AgilityToColor(agility);
					const Point navLocPosWs = pCtrl->m_interactNavLocations[iNavLoc].GetPosWs();
					// dependent pos
					const Point posWs = GetInteractPos(pCtrl, navLocPosWs);
					GAMEPLAY_ASSERT(IsReasonable(posWs));

					const Vector ofs(0.0f, 0.04f, 0.0f);
					g_prim.Draw(DebugCircle(navLocPosWs + ofs, kUnitYAxis, 0.26f, color));
					g_prim.Draw(DebugLine(navLocPosWs, PointFromXzAndY(navLocPosWs, posWs + ofs), color, color, 4.0f));
					g_prim.Draw(DebugLine(posWs + ofs, PointFromXzAndY(navLocPosWs, posWs + ofs), color, color, 4.0f));

					ScreenSpaceTextPrinter printer2(PointFromXzAndY(navLocPosWs, posWs + ofs));
					printer2.SetScale(0.7f);
					printer2.SetFlags(1);
					if ((U32)agility & (U32)InteractAgilityMask::kProne)
						printer2.PrintText(AgilityToColor(InteractAgilityMask::kProne), "requires prone");
					if ((U32)agility & (U32)InteractAgilityMask::kBreakable)
						printer2.PrintText(AgilityToColor(InteractAgilityMask::kBreakable), "through breakable");
					if ((U32)agility & (U32)InteractAgilityMask::kOverride)
						printer2.PrintText(AgilityToColor(InteractAgilityMask::kOverride), "override");
				}
			}
		}
	}
}

void NdInteractablesManager::VerifyInternalState() const
{
	STRIP_IN_SUBMISSION_BUILD;

	AtomicLockJanitorRead_Jls jj(&m_ndInteractablesMgrLock, FILE_LINE_FUNC);

	for (int i = 0; i < kMaxReg; ++i)
	{
		const bool bitSet = m_regBits.IsBitSet(i);
		const bool handleValid = m_ahReg[i].HandleValid();

		if (bitSet != handleValid)
		{
			if (bitSet)
			{
				ALWAYS_HALTF(("Registered interactables slot %d is out of sync! Bit set but invalid handle.", i));
			}
			else
			{
				ALWAYS_HALTF(("Registered interactables slot %d is out of sync! No bit but valid handle pointing to %s.", i, DevKitOnly_StringIdToStringOrHex(m_ahReg[i].GetUserId())));
			}
		}
	}
}

void NdInteractablesManager::RegisterInteractable(MutableNdGameObjectHandle hInteractable)
{
	AtomicLockJanitorWrite_Jls jj(&m_ndInteractablesMgrLock, FILE_LINE_FUNC);

	// make sure it isn't already registered
#ifndef SUBMISSION_BUILD
	{
		for (const U32 i : m_regBits)
		{
			ALWAYS_ASSERTF(m_ahReg[i] != hInteractable, ("Interactable %s has registered itself more than once!", DevKitOnly_StringIdToStringOrHex(hInteractable.GetUserId())));
		}
	}
#endif

	U64 i = m_regBits.FindFirstClearBit();
	GAMEPLAY_ASSERTF(i != kBitArrayInvalidIndex, ("Ran out of registered interactables! Too many or they're not being cleaned up."));
	if (i != kBitArrayInvalidIndex)
	{
		GAMEPLAY_ASSERT(i < kMaxReg);
		m_ahReg[i] = hInteractable;
		m_regBits.SetBit(i);
	}
}

void NdInteractablesManager::UnregisterInteractable(MutableNdGameObjectHandle hInteractable)
{
	const bool handleValid = hInteractable.HandleValid();
	GAMEPLAY_ASSERTF(handleValid, ("Tried to unregister a null interactable!"));
	if (!handleValid)
		return;

	AtomicLockJanitorWrite_Jls jj(&m_ndInteractablesMgrLock, FILE_LINE_FUNC);

	for (const U32 i : m_regBits)
	{
		if (m_ahReg[i] == hInteractable)
		{
			GAMEPLAY_ASSERT(i < kMaxReg);
			m_regBits.ClearBit(i);
			m_ahReg[i] = nullptr;
			return;
		}
	}

	GAMEPLAY_ASSERTF(false, ("Tried to unregister an interactable that doesn't exist!"));
}

int NdInteractablesManager::FindRegisteredInteractables(NdGameObject** pOutputList, int outputCapacity) const
{
	AtomicLockJanitorRead_Jls jj(&m_ndInteractablesMgrLock, FILE_LINE_FUNC);

	int outputCount = 0;
	for (const U32 i : m_regBits)
	{
		NdGameObject* const pGo = m_ahReg[i].ToMutableProcess();
		GAMEPLAY_ASSERT(pGo);
		GAMEPLAY_ASSERT(pGo->GetInteractControl());

		if (outputCount < outputCapacity)
			pOutputList[outputCount++] = pGo;
	}

	return outputCount;
}

int NdInteractablesManager::FindRegisteredInteractablesInSphere(MutableNdGameObjectHandle* pOutputList, int outputCapacity, Sphere searchSphere) const
{
	if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractablesMgrQueries))
		g_prim.Draw(DebugSphere(searchSphere, kColorGreen), DebugPrimTime(Seconds(0.5f)));

	AtomicLockJanitorRead_Jls jj(&m_ndInteractablesMgrLock, FILE_LINE_FUNC);

	U32F outputCount = 0;
	for (const U32 i : m_regBits)
	{
		const MutableNdGameObjectHandle& hGo = m_ahReg[i];
		const NdGameObject* const pGo = hGo.ToProcess();
		GAMEPLAY_ASSERT(pGo);
		GAMEPLAY_ASSERT(pGo->GetInteractControl());

		const FgAnimData* pAnimData = pGo->GetAnimData();
		if (!pAnimData)
			continue;

		const bool shapesIntersect = SpheresIntersect(searchSphere, pAnimData->m_pBoundingInfo->m_jointBoundingSphere);

		if (shapesIntersect && outputCount < outputCapacity)
		{
			pOutputList[outputCount++] = hGo;
		}

		if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractablesMgrQueries && DebugSelection::Get().IsProcessOrNoneSelected(hGo)))
		{
			g_prim.Draw(DebugSphere(pAnimData->m_pBoundingInfo->m_jointBoundingSphere, (shapesIntersect ? kColorYellow : kColorGray)), DebugPrimTime(Seconds(0.5f)));
		}
	}

	return outputCount;
}

int NdInteractablesManager::FindRegisteredInteractablesWithNavLocationsInSphere(MutableNdGameObjectHandle* pOutputList, int outputCapacity, Sphere searchSphere) const
{
	AtomicLockJanitorRead_Jls jj(&m_ndInteractablesMgrLock, FILE_LINE_FUNC);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const Point centerWs = searchSphere.GetCenter();
	const float r2 = Sqr(searchSphere.GetRadius());

	U32F outputCount = 0;
	for (const U32 i : m_regBits)
	{
		const MutableNdGameObjectHandle& hGo = m_ahReg[i];
		const NdGameObject* const pGo = hGo.ToProcess();
		GAMEPLAY_ASSERT(pGo);

		const NdInteractControl* const pCtrl = pGo->GetInteractControl();
		GAMEPLAY_ASSERT(pCtrl);

		AtomicLockJanitorRead interactNavLocationsLock(&pCtrl->m_interactNavLocationsLock, FILE_LINE_FUNC);

		for (int iNavLoc = 0; iNavLoc < pCtrl->m_numInteractNavLocations; ++iNavLoc)
		{
			const NavLocation& navLoc = pCtrl->m_interactNavLocations[iNavLoc];
			const Point posWs = navLoc.GetPosWs();
			const float distSqr = DistSqr(centerWs, posWs);
			if (distSqr <= r2)
			{
				if (outputCount < outputCapacity)
				{
					pOutputList[outputCount++] = hGo;
				}
				break;
			}
		}
	}

	return outputCount;
}

int NdInteractablesManager::FindRegisteredInteractablesBetweenRadii(FoundInteractable* pOutputList, int outputCapacity, Point centerWs, float radiusInner, float radiusOuter) const
{
	const float radiusInnerSqr = Sqr(radiusInner);
	const float radiusOuterSqr = Sqr(radiusOuter);

	if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractablesMgrQueries))
	{
		const Sphere sphereInner(centerWs, radiusInner);
		const Sphere sphereOuter(centerWs, radiusOuter);
		g_prim.Draw(DebugSphere(sphereInner, kColorBlue));
		g_prim.Draw(DebugSphere(sphereOuter, kColorGreen));
	}

	AtomicLockJanitorRead_Jls jj(&m_ndInteractablesMgrLock, FILE_LINE_FUNC);

	U32F outputCount = 0;
	for (const U32 i : m_regBits)
	{
		const MutableNdGameObjectHandle& hGo = m_ahReg[i];
		const NdGameObject* const pGo = hGo.ToProcess();
		GAMEPLAY_ASSERT(pGo);
		GAMEPLAY_ASSERT(pGo->GetInteractControl());

		const FgAnimData* const pAnimData = pGo->GetAnimData();
		if (!pAnimData)
			continue;

		const Point objCenterWs = pAnimData->m_pBoundingInfo->m_jointBoundingSphere.GetCenter();
		const float radiusSqr = DistSqr(centerWs, objCenterWs);
		const bool withinOuter = (radiusSqr < radiusOuterSqr);	// open   \_ this ensures that each object is returned exactly once, when the radii butt up against
		const bool withinInner = (radiusSqr >= radiusInnerSqr);	// closed /  one another like this on each subsequent call: [0,5), [5,10), [10,15), ...

		const bool withinRadii = withinOuter && withinInner;

		if (withinRadii && outputCount < outputCapacity)
		{
			pOutputList[outputCount].m_hInteractable = hGo;
			pOutputList[outputCount].m_dist = Sqrt(radiusSqr);
			++outputCount;
		}

		if (FALSE_IN_FINAL_BUILD(g_interactableOptions.m_drawInteractablesMgrQueries && DebugSelection::Get().IsProcessOrNoneSelected(hGo)))
		{
			g_prim.Draw(DebugString(objCenterWs, DevKitOnly_StringIdToStringOrHex(pGo->GetUserId()), 0, (withinRadii ? kColorYellow : kColorGray), 0.5f), Seconds(0.5f));
			//g_prim.Draw(DebugCross(objCenterWs, 0.1f, (withinRadii ? kColorYellow : kColorGray)), DebugPrimTime(Seconds(0.5f)));
		}
	}

	return outputCount;
}

NdInteractablesManager g_ndInteractablesMgr;
