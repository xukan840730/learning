/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "game/ai/controller/horse-jump-controller.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/profiling/profile-cpu-categories.h"

#include "ailib/nav/nav-ai-util.h"

#include "gamelib/feature/feature-db-ref.h"
#include "gamelib/region/region-control.h"
#include "gamelib/region/region-manager.h"
#include "gamelib/gameplay/ground-hug.h"
#include "gamelib/scriptx/h/npc-demeanor-defines.h"

#include "game/ai/characters/horse.h"
#include "game/ai/controller/locomotion-controller.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/character/nearby-edge-mgr.h"
#include "game/player/can-stand.h"
#include "game/player/player.h"
#include "game/player/player-ride-horse.h"
#include "game/player/jumps/player-jump-edge-filter.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/vehicle/horse-component.h"
#include "game/vehicle/horse-jump-defines.h"
#include "game/vehicle/horse-move-controller.h"
#include "game/vehicle/horse-stick-input.h"

#if defined(FINAL_BUILD)
static const bool g_debugDrawHorseJump = false;
static const bool g_enableVerboseHorseJumpLog = false;
static const bool g_debugDrawHorseAdjustApForColl = false;
static const bool g_debugDrawHorseCliffWalls = false;
static const bool g_debugHorseJumpSpeedScale = false;
static const bool g_debugHorseStopping = false;
static const EnabledJumps g_enabledHorseJumps;
#else
extern bool g_debugDrawHorseJump;
extern bool g_enableVerboseHorseJumpLog;
extern bool g_debugDrawHorseAdjustApForColl;
extern bool g_debugDrawHorseCliffWalls;
extern bool g_debugHorseJumpSpeedScale;
extern bool g_debugHorseStopping;
extern EnabledJumps g_enabledHorseJumps;
#endif

FINAL_CONST bool g_debugDisableTooManyHorseJumpEdgeError = true;
FINAL_CONST bool g_debugHorseJumpBucketKicks = false;
FINAL_CONST bool g_disableHorseJumpProceduralAdjustments = false;
FINAL_CONST bool g_disableHorseMoveToJump = true;

/// --------------------------------------------------------------------------------------------------------------- ///
void PreparedHorseJump::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pJump, delta, lowerBound, upperBound);
	m_groundFindJob.Relocate(delta, lowerBound, upperBound);
}

void PreparedHorseJump::Reset()
{
	m_pJump = nullptr;
	m_edge = HorseJumpEdgeRating();
	m_targetPt = kZero;
	m_targetFacingDir = kZero;
	m_groundFindJob.Reset();
}

PreparedHorseJump::PreparedHorseJump()
{
	Reset();
}

bool PreparedHorseJump::IsValid() const
{
	return m_pJump;
}

void PreparedHorseJump::Prepare(IHorseJump* pHorseJump, const HorseJumpEdgeRating& edge, Point_arg moveToPt, Vector_arg desiredFacingDir)
{
	m_pJump = pHorseJump;
	m_edge = edge;
	m_targetPt = moveToPt;
	m_targetFacingDir = desiredFacingDir;

	Kick();
}

void PreparedHorseJump::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (!IsValid())
		return;

	MsgConPauseable("Moving to Jump %s\n", GetJump()->GetName());

	g_prim.Draw(DebugCross(GetTargetPt(), 1.0f, 1.0f, kColorRed, PrimAttrib(kPrimEnableHiddenLineAlpha), "PreparedJumpStart"));
	g_prim.Draw(DebugArrow(GetTargetPt(), GetTargetFacingDir(), kColorRed, 0.5f, PrimAttrib(kPrimEnableHiddenLineAlpha)));
}

void PreparedHorseJump::Kick()
{
	m_groundFindJob.Open(1, 1, ICollCastJob::kCollCastAllStartPenetrations, ICollCastJob::kClientPlayerMisc);

	const Point start = m_targetPt + kUnitYAxis;
	const Point end = start - (Vector(kUnitYAxis) * 6.0f);

	m_groundFindJob.SetProbeExtents(0, start, end);
	m_groundFindJob.SetProbeRadius(0, 0.25f);
	m_groundFindJob.SetProbeFilter(0, CollideFilter(Collide::kLayerMaskHorseAvoid, Pat(Pat::kHorseThroughMask | Pat::kStealthVegetationMask)));

	m_groundFindJob.Kick(FILE_LINE_FUNC, 1);
}

void PreparedHorseJump::Gather()
{
	if (!m_groundFindJob.IsValid())
		return;

	bool contactValid = m_groundFindJob.IsContactValid(0, 0);
	if (contactValid)
		m_targetPt = m_groundFindJob.GetShapeContactPoint(0, 0);

	m_groundFindJob.Close();
	if (!contactValid)
		Reset();
}

void PreparedHorseJump::Update()
{
	GAMEPLAY_ASSERT(IsValid());

	Gather();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void HorseJumpController::Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl)
{
	if (Horse* pHorse = Horse::FromProcess(pCharacter))
	{
		InitInternal(pHorse);
		ParentClass::Init(pCharacter, pNavControl);
	}
	else
	{
		AI_ASSERTF(false, ("Failed to Init HorseJumpController on non-horse: %s", DevKitOnly_StringIdToString(pCharacter->GetUserId())));
	}
}

void HorseJumpController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	if (Horse* pHorse = Horse::FromProcess(pNavChar))
	{
		InitInternal(pHorse);
		ParentClass::Init(pNavChar, pNavControl);
	}
	else
	{
		AI_ASSERTF(false, ("Failed to Init HorseJumpController on non-horse: %s", DevKitOnly_StringIdToString(pNavChar->GetUserId())));
	}
}

void HorseJumpController::InitInternal(Horse* pHorse)
{
	m_pHorse = pHorse;
	m_isJumping = false;
	m_lastJumpButtonFrame = 0;
	m_pCurrJump = nullptr;
	m_pBestJump = nullptr;
	m_isMidair = false;
	m_jumpUpDisabled = false;
	m_bestJumpTooFar = false;
	m_isJumpStarting = false;
	m_longDropDownEnabled = false;
	m_jumpStartedThisFrame = false;
	m_inDistanceHackRegion = false;
	m_wantToJump = false;
	m_canJump = false;
	m_jumpList.CreateJumps();
	m_curJumpId = INVALID_STRING_ID_64;
	m_jumpAction.Reset();
	m_preparedJump.Reset();
	m_distToNearestJumpEdge = NDI_FLT_MAX;
	m_edgeList = EdgeList(kMaxHorseJumpEdges);
	m_forceAllowRunningJumpFrame = -1000;
	m_scriptForceJumpSpeedFrame = -1000;
	m_forceJumpDirFrame = -1000;
	m_forceJumpDestinationFrame = -1000;

	CreateHorseJumpEdgeFilter(&m_filter);
	CreateHorseJumpDetectorEdgeFilter(&m_detectorFilter);
	CreateHorseCliffDetectorEdgeFilter(&m_cliffFilter);
	CreateHorseCliffEdgeDirectionalFilter(&m_directionalCliffFilter);

	CheckForTypos();

#ifndef FINAL_BUILD
	m_alreadyComplainedAboutBadEdges = false;
#endif
}

HorseJumpController::~HorseJumpController()
{
	m_jumpList.Clear();
}

void HorseJumpController::Reset()
{
	m_pCurrJump = nullptr;
	m_curJumpId = INVALID_STRING_ID_64;
	m_isJumping = false;
	m_jumpAction.Reset();
}

void HorseJumpController::Interrupt()
{
	Reset();
}

void HorseJumpController::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(delta, lowerBound, upperBound);
	RelocatePointer(m_pHorse, delta, lowerBound, upperBound);
	RelocatePointer(m_pCurrJump, delta, lowerBound, upperBound);
	RelocatePointer(m_pBestJump, delta, lowerBound, upperBound);
	m_jumpList.Relocate(delta, lowerBound, upperBound);
	m_filter.Relocate(delta, lowerBound, upperBound);
	m_detectorFilter.Relocate(delta, lowerBound, upperBound);
	m_cliffFilter.Relocate(delta, lowerBound, upperBound);
	m_directionalCliffFilter.Relocate(delta, lowerBound, upperBound);
	m_preparedJump.Relocate(delta, lowerBound, upperBound);
	m_edgeList.Relocate(delta, lowerBound, upperBound);
}

bool HorseJumpController::GatherJumpBucket(EdgeList& edges, Vector_arg direction, HorseJumpBucket bucket, float& bestDistSqr, bool bucketIsValid)
{
	PROFILE_AUTO(AI);

	IHorseJump* const* jumps;
	U32F jumpCount = m_jumpList.GetJumps(jumps, bucket);

	const Point horsePos = m_pHorse->GetTranslation() + (kHorseJumpsUse1FrameEstimatedDeltaPosForGather ? VectorXz(m_pHorse->GetLastFrameMoveDelta()) : kZero);
	bool result = false;

	CONST_EXPR float kBestDistEpsilon = 0.1f;

	for (U32F i = 0; i < jumpCount; ++i)
	{
		IHorseJump* pJump = jumps[i];
		if (!pJump->IsActive())
			continue;

		float distSqr;
		HorseJumpEdgeRating* pJumpEdge = nullptr;
		HorseJumpResult jumpResult = pJump->CanJump(m_pHorse, horsePos, direction, 1.0f, edges, distSqr, &pJumpEdge);
		if (jumpResult != HorseJumpResult::kFail)
		{
			if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump && bucketIsValid))
			{
				if (jumpResult == HorseJumpResult::kValidTooFar)
					MsgConPauseable("Jump %s may become valid soon, will suppress lower priority jumps (unless they are closer) [edge: %d]\n", pJump->GetName(), pJumpEdge ? (int)pJumpEdge->m_edge.m_pSrcEdge->GetId() : -1);
				else
					MsgConPauseable("Jump %s is valid [edge: %d]\n", pJump->GetName(), pJumpEdge ? (int)pJumpEdge->m_edge.m_pSrcEdge->GetId() : -1);
			}

			result = true;
			if (bucketIsValid)
			{
				if (m_pBestJump)
				{
					if (bestDistSqr < distSqr && Abs(bestDistSqr - distSqr) > kBestDistEpsilon)
					{
						if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
							MsgConPauseable("  Jump %s is farther than current jump %s (%.2f vs %.2f), skipping\n", pJump->GetName(), Sqrt(distSqr), bestDistSqr >= kLargeFloat ? 99999.0f : Sqrt(bestDistSqr), m_pBestJump->GetName());

						continue;
					}
					//lower priority is better jump
					else if (pJump->GetPriority() < m_pBestJump->GetPriority())
					{
						if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
							MsgConPauseable("  Jump %s has a better priority (%d) than best jump %s (%d). Best Jump is now %s (dist %.2f)\n", pJump->GetName(), (int)pJump->GetPriority(), m_pBestJump->GetName(), (int)m_pBestJump->GetPriority(), pJump->GetName(), Sqrt(distSqr));
						bestDistSqr = distSqr;
						m_pBestJump = pJump;
						m_bestJumpTooFar = jumpResult == HorseJumpResult::kValidTooFar;
					}
					//if we have an equal priority jump that is valid we should do that instead of waiting for another jump to complete
					else if (pJump->GetPriority() == m_pBestJump->GetPriority() && jumpResult == HorseJumpResult::kValid && m_bestJumpTooFar)
					{
						if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
							MsgConPauseable("  Jump %s has an equal priority to best jump %s, but is in range. Best Jump is now %s (dist %.2f)\n", pJump->GetName(), m_pBestJump->GetName(), pJump->GetName(), Sqrt(distSqr));
						bestDistSqr = distSqr;
						m_pBestJump = pJump;
						m_bestJumpTooFar = false;
					}
					//lower bucket numbers break ties if jumps have the same priority
					else if (pJump->GetPriority() == m_pBestJump->GetPriority() && bucket < m_pBestJump->GetBucket() && (m_bestJumpTooFar || jumpResult == HorseJumpResult::kValid))
					{
						if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
							MsgConPauseable("  Jump %s has an equal priority but better bucket (%d) than best jump %s (%d). Best Jump is now %s (dist %.2f)\n", pJump->GetName(), (int)pJump->GetBucket(), m_pBestJump->GetName(), (int)m_pBestJump->GetBucket(), pJump->GetName(), Sqrt(distSqr));
						bestDistSqr = distSqr;
						m_pBestJump = pJump;
						m_bestJumpTooFar = jumpResult == HorseJumpResult::kValidTooFar;
					}
					else if (m_bestJumpTooFar && jumpResult == HorseJumpResult::kValid && Abs(bestDistSqr - distSqr) < kBestDistEpsilon)
					{
						if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
							MsgConPauseable("  Jump %s is valid and is closer than current jump %s (%.2f vs %.2f). Best Jump is now %s (priority ignored)\n", pJump->GetName(), m_pBestJump->GetName(), Sqrt(distSqr), bestDistSqr >= kLargeFloat ? 99999.0f : Sqrt(bestDistSqr), pJump->GetName());
						bestDistSqr = distSqr;
						m_pBestJump = pJump;
						m_bestJumpTooFar = false;
					}
					else if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
					{
						MsgConPauseable("  Jump %s is rejected as it has a lower or equal to priority (P: %d B: %d) compared to best jump %s (P: %d B: %d)\n",
							pJump->GetName(), (int)pJump->GetPriority(), (int)pJump->GetBucket(), m_pBestJump->GetName(), (int)m_pBestJump->GetPriority(), (int)m_pBestJump->GetBucket());
					}
				}
				else
				{
					if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
						MsgConPauseable("  Jump %s is the only valid jump so far (dist: %.2f)\n", pJump->GetName(), Sqrt(distSqr));
					bestDistSqr = distSqr;
					m_pBestJump = pJump;
					m_bestJumpTooFar = jumpResult == HorseJumpResult::kValidTooFar;
				}
			}
		}
	}

	const Player* pPlayer = GetPlayer();
	if (pPlayer && pPlayer->IsState(SID("Investigate")))
	{
		result = false;
		m_pBestJump = nullptr;
		m_bestJumpTooFar = false;
	}

	return result;
}

bool HorseJumpController::CanTryToJump() const
{
	if (m_inNoJumpRegion)
		return false;

	const Player* pPlayer = m_pHorse->GetPlayerRider(HorseDefines::kRiderFront);
	if (!pPlayer)
		return false;

	const IPlayerRideHorseController* pRideHorse = pPlayer->m_pRideHorseController;
	if (pRideHorse->IsDismounting() || pRideHorse->IsMovingToHorse())
		return false;

	return true;
}

bool HorseJumpController::GatherJumps()
{
	PROFILE_AUTO(AI);

	m_distToNearestJumpEdge = NDI_FLT_MAX;

	Player* pPlayer = m_pHorse->GetPlayerRider(HorseDefines::kRiderFront);
	if (!pPlayer)
		return false;

	INearbyEdgeMgr* pEdgeMgr = pPlayer->GetNearbyEdgeMgr();
	if (!pEdgeMgr)
		return false;

	m_pBestJump = nullptr;
	m_bestJumpTooFar = false;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	ListArray<EdgeInfo> edges(kMaxHorseJumpEdges);
	FeatureEdge::Flags flags = FeatureEdge::kFlagHorseJump;
	bool tooManyEdges = false;
	const int edgeCount = pEdgeMgr->GatherEdges(edges, flags, kHorseJumpSearchRadius, 0.707f, &tooManyEdges);
#ifndef FINAL_BUILD
	if (tooManyEdges && !m_alreadyComplainedAboutBadEdges && !g_debugDisableTooManyHorseJumpEdgeError)
	{
		MsgConScriptError("Too many edges with Horse Jump flag at position %s (max: %d) Horse Jumps may not trigger properly\nPlease clean up collision geo or mark some surfaces with the NoHorseJump or ExcludeFromFeatureGen PATs\nTell Harold if you still see this message after removing extraneous edges\n", PrettyPrint(pPlayer->GetTranslation()), kMaxHorseJumpEdges);
		m_alreadyComplainedAboutBadEdges = true;
	}
#endif

	if (edgeCount <= 0)
		return false;

	for (int iEdge = 0; iEdge < edgeCount; ++iEdge)
	{
		const float distToEdge = Dist(m_pHorse->GetTranslation(), edges[iEdge].GetClosestEdgePt());
		m_distToNearestJumpEdge = Min(m_distToNearestJumpEdge, distToEdge);
	}

	// doing this after we search for edges to make sure m_distToNearestJumpEdge is accurate
	if (!CanTryToJump())
		return false;

	Vector direction = m_pHorse->GetEffectiveFacingDirXz();
	GAMEPLAY_ASSERT(IsNormal(direction));

	Vector stickDir = SafeNormalize(m_pHorse->GetTrueStickDir(), kZero);
	const F32 stickDotDir = Dot(direction, stickDir);

	float bestDistSqr = kLargeFloat;

	m_canJump = false;
	bool result = false;

	for (int iBucket = 0; iBucket < kHorseJumpBucketDetectors; ++iBucket)
	{
		const HorseJumpBucket bucket = (HorseJumpBucket)iBucket;
		bool bucketValid = IsBucketValid(bucket, false);
		if (FALSE_IN_FINAL_BUILD(g_debugHorseJumpBucketKicks && bucket != kHorseJumpBucketAlways && bucketValid))
		{
			MsgConPauseable("Gathering %s Jump Bucket\n", StringFromHorseJumpBucket(bucket));
		}

		if (GatherJumpBucket(edges, direction, bucket, bestDistSqr, bucketValid))
		{
			m_canJump = true;
			if (bucketValid)
				result = true;
		}
	}

	if (result && m_bestJumpTooFar)
	{
		if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
		{
			SetColor(kMsgConPauseable, kColorPink.ToAbgr8());
			MsgConPauseable("Best jump not yet valid! Jump: %s\n", m_pBestJump->GetName());
			SetColor(kMsgConPauseable, kColorWhite.ToAbgr8());
		}
		return false;
	}
	return result;
}

void HorseJumpController::PrepareBucket(EdgeList& edges, Vector_arg direction, HorseJumpBucket bucket, Player* pPlayer)
{
	IHorseJump* const* jumps;
	U32F jumpCount = m_jumpList.GetJumps(jumps, bucket);

	for (U32F i = 0; i < jumpCount; ++i)
	{
		const Point position = m_pHorse->GetTranslation() + (kHorseJumpsUse1FrameEstimatedDeltaPos ? VectorXz(m_pHorse->GetLastFrameMoveDelta()) : kZero);
		IHorseJump* pJump = jumps[i];
		bool isJumping = m_pCurrJump == pJump;
		pJump->PrepareAsyncJobs(m_pHorse, position, direction, 1.0f, edges, isJumping);

		if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump && g_debugHorseJumpBucketKicks))
		{
			g_prim.Draw(DebugArrow(m_pHorse->GetTranslation(), m_pHorse->GetLastFrameMoveDelta(), kColorBlue, 0.5f, PrimAttrib(kPrimDisableDepthTest), "lastFrameMoveDelta"), kPrimDuration1FramePauseable);
		}
	}
}

void HorseJumpController::KickBucket(EdgeList& edges, Vector_arg direction, HorseJumpBucket bucket, Player* pPlayer)
{
	IHorseJump* const* jumps;
	U32F jumpCount = m_jumpList.GetJumps(jumps, bucket);

	for (U32F i = 0; i < jumpCount; ++i)
	{
		if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump && g_debugHorseJumpBucketKicks))
			MsgCon("Kicking Jump: %s\n", jumps[i]->GetName());
		jumps[i]->KickAsyncJobs();
	}
}

void HorseJumpController::GatherValidJumpEdges()
{
	if (!IsJumpDetectorEnabled())
		return;

	PROFILE_AUTO(AI);

	ScopedTempAllocator jj(FILE_LINE_FUNC);
	IHorseJump* const* jumps;
	U32F jumpCount = m_jumpList.GetJumps(jumps, kHorseJumpBucketDetectors);
	ASSERT(jumpCount > 0);
	for (U32F iJump = 0; iJump < jumpCount; ++iJump)
	{
		IHorseJumpDetector* pJumpDetector = static_cast<IHorseJumpDetector*>(jumps[iJump]);
		if (!pJumpDetector->IsActive())
			continue;
		SetupDetectorFlags(pJumpDetector);
		pJumpDetector->GatherValidEdges();
	}
}

void HorseJumpController::GatherVirtualWalls()
{
	PROFILE_AUTO(AI);

	ScopedTempAllocator jj(FILE_LINE_FUNC);
	IHorseJump* const* jumps;
	U32F jumpCount = m_jumpList.GetJumps(jumps, kHorseJumpBucketCliffDetectors);
	ASSERT(jumpCount > 0);
	ICliffDetector* pCliffDetector = static_cast<ICliffDetector*>(jumps[0]);
	if (!pCliffDetector->IsActive())
		return;
	pCliffDetector->GatherVirtualWalls();

}

U32F HorseJumpController::GetVirtualWalls(VirtualCliffWall aOutWalls[], U32F capacity)
{
	IHorseJump* const* jumps;
	U32F jumpCount = m_jumpList.GetJumps(jumps, kHorseJumpBucketCliffDetectors);
	ASSERT(jumpCount > 0);
	VirtualCliffWall* pFirstOpenSpace = aOutWalls;
	U32F wallCount = 0;
	for (int i = 0; i < jumpCount; ++i)
	{
		ICliffDetector* pCliffDetector = static_cast<ICliffDetector*>(jumps[i]);
		U32F numWallsOutput = pCliffDetector->GetVirtualWalls(pFirstOpenSpace, capacity);
		pFirstOpenSpace = &(pFirstOpenSpace[numWallsOutput]);
		wallCount += numWallsOutput;
		capacity -= numWallsOutput;
		if (capacity <= 0)
			break;
	}
	return wallCount;
}

const EdgeInfo* HorseJumpController::GetJumpEdges(U32F* pEdgeCount) const
{
	const IHorseJump* const* jumps;
	U32F jumpCount = m_jumpList.GetJumps(jumps, kHorseJumpBucketDetectors);
	ASSERT(jumpCount > 0);
	const IHorseJumpDetector* pJumpDetector = static_cast<const IHorseJumpDetector*>(jumps[0]);
	return pJumpDetector->GetJumpEdges(pEdgeCount);
}

void HorseJumpController::KickCliffDetector()
{
	PROFILE_AUTO(AI);

	Player* pPlayer = m_pHorse->GetPlayerRider(HorseDefines::kRiderFront);
	if (!pPlayer)
		return;

	INearbyEdgeMgr* pEdgeMgr = pPlayer->GetNearbyEdgeMgr();
	if (!pEdgeMgr)
		return;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	EdgeList edges(kMaxHorseCliffEdges);
	FeatureEdge::Flags flags = FeatureEdge::kFlagHorseJump;

	const float kCliffEdgeSearchRadius = Max(kHorseCliffDirectionalSearchRadius, kHorseCliffSearchRadius);

	const int edgeCount = pEdgeMgr->GatherEdges(edges, flags, kCliffEdgeSearchRadius, 0.707f);
	if (edgeCount <= 0)
		return;

	const Vector direction = SafeNormalize(VectorXz(GetLocalZ(m_pHorse->GetRotation())), kZero);
	if (!IsNormal(direction))
		return;

	PrepareBucket(edges, direction, kHorseJumpBucketCliffDetectors, pPlayer);

	const DC::PlayerJumpSettings* pSettings = pPlayer->m_pJumpController->GetJumpSettings();
	m_cliffFilter.FilterEdges(pPlayer, m_pHorse->GetTranslation(), direction, pSettings, &edges);
	m_directionalCliffFilter.FilterEdges(pPlayer, m_pHorse->GetTranslation(), direction, pSettings, &edges);

	KickBucket(edges, direction, kHorseJumpBucketCliffDetectors, pPlayer);
}

void HorseJumpController::KickJumpDetector()
{

	if (!IsJumpDetectorEnabled())
		return;

	PROFILE_AUTO(AI);

	Player* pPlayer = m_pHorse->GetPlayerRider(HorseDefines::kRiderFront);
	if (!pPlayer)
		return;

	INearbyEdgeMgr* pEdgeMgr = pPlayer->GetNearbyEdgeMgr();
	if (!pEdgeMgr)
		return;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	EdgeList edges(kMaxHorseJumpEdges);
	FeatureEdge::Flags flags = FeatureEdge::kFlagHorseJump;
	const int edgeCount = pEdgeMgr->GatherEdges(edges, flags, kHorseJumpDetectorSearchRadius, 0.707f);
	if (edgeCount <= 0)
		return;

	const Vector direction = SafeNormalize(VectorXz(GetLocalZ(m_pHorse->GetRotation())), kZero);

	if (!IsNormal(direction))
		return;

	PrepareBucket(edges, direction, kHorseJumpBucketDetectors, pPlayer);

	const DC::PlayerJumpSettings* pSettings = pPlayer->m_pJumpController->GetJumpSettings();
	m_detectorFilter.FilterEdges(pPlayer, m_pHorse->GetTranslation(), direction, pSettings, &edges);

	KickBucket(edges, direction, kHorseJumpBucketDetectors, pPlayer);
}

void HorseJumpController::Kick()
{
	PROFILE_AUTO(AI);

	GAMEPLAY_ASSERT(!m_preparedJump.IsValid());

	Player* pPlayer = m_pHorse->GetPlayerRider(HorseDefines::kRiderFront);
	if (!pPlayer)
		return;

	INearbyEdgeMgr* pEdgeMgr = pPlayer->GetNearbyEdgeMgr();
	if (!pEdgeMgr)
		return;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	//EdgeList edges(kMaxHorseJumpEdges);
	EdgeList& edges = m_edgeList;
	FeatureEdge::Flags flags = FeatureEdge::kFlagHorseJump;
	const int edgeCount = pEdgeMgr->GatherEdges(edges, flags, kHorseJumpSearchRadius, 0.707f);
	if (edgeCount <= 0)
		return;

	Maybe<Vector> scriptedDirection = GetForceJumpDir();
	Vector direction = scriptedDirection.Otherwise(m_pHorse->GetEffectiveFacingDirXz());
	GAMEPLAY_ASSERT(IsNormal(direction));

	if (FALSE_IN_FINAL_BUILD(g_debugHorseJumpBucketKicks && g_debugDrawHorseJump))
	{
		MsgConPauseable("Length of last frame move delta: %.3f\n", (float)Length(m_pHorse->GetLastFrameMoveDelta()));
	}

	if (IsBucketValid(kHorseJumpBucketSprintPressed, true))
	{
		PrepareBucket(edges, direction, kHorseJumpBucketSprintPressed, pPlayer);
	}

	if (IsBucketValid(kHorseJumpBucketRunSprintPressed, true))
	{
		PrepareBucket(edges, direction, kHorseJumpBucketRunSprintPressed, pPlayer);
	}

	if (IsBucketValid(kHorseJumpBucketRun, true))
	{
		PrepareBucket(edges, direction, kHorseJumpBucketRun, pPlayer);
	}
	
	if (IsBucketValid(kHorseJumpBucketWalk, true))
	{
		PrepareBucket(edges, direction, kHorseJumpBucketWalk, pPlayer);
	}
	
	if (IsBucketValid(kHorseJumpBucketWalkSprintPressed, true))
	{
		PrepareBucket(edges, direction, kHorseJumpBucketWalkSprintPressed, pPlayer);
	}
	PrepareBucket(edges, direction, kHorseJumpBucketAlways, pPlayer);

	const DC::PlayerJumpSettings* pSettings = pPlayer->m_pJumpController->GetJumpSettings();
	m_filter.FilterEdges(pPlayer, m_pHorse->GetTranslation(), direction, pSettings, &edges);

	if (IsBucketValid(kHorseJumpBucketSprintPressed, true))
	{
		if (FALSE_IN_FINAL_BUILD(g_debugHorseJumpBucketKicks))
			MsgConPauseable("Kicking Sprint Pressed Jump Bucket\n");
		KickBucket(edges, direction, kHorseJumpBucketSprintPressed, pPlayer);
	}
	if (IsBucketValid(kHorseJumpBucketRunSprintPressed, true))
	{
		if (FALSE_IN_FINAL_BUILD(g_debugHorseJumpBucketKicks))
			MsgConPauseable("Kicking Run Sprint Pressed Jump Bucket\n");
		KickBucket(edges, direction, kHorseJumpBucketRunSprintPressed, pPlayer);
	}
	if (IsBucketValid(kHorseJumpBucketRun, true))
	{
		if (FALSE_IN_FINAL_BUILD(g_debugHorseJumpBucketKicks))
			MsgConPauseable("Kicking Run Jump Bucket\n");
		KickBucket(edges, direction, kHorseJumpBucketRun, pPlayer);
	}
	if (IsBucketValid(kHorseJumpBucketWalk, true))
	{
		if (FALSE_IN_FINAL_BUILD(g_debugHorseJumpBucketKicks))
			MsgConPauseable("Kicking Walk Jump Bucket\n");
		KickBucket(edges, direction, kHorseJumpBucketWalk, pPlayer);
	}
	if (IsBucketValid(kHorseJumpBucketWalkSprintPressed, true))
	{
		if (FALSE_IN_FINAL_BUILD(g_debugHorseJumpBucketKicks))
			MsgConPauseable("Kicking Walk Sprint Pressed Jump Bucket\n");
		KickBucket(edges, direction, kHorseJumpBucketWalkSprintPressed, pPlayer);
	}
	KickBucket(edges, direction, kHorseJumpBucketAlways, pPlayer);
}

void HorseJumpController::FadeToJump(StringId64 jumpId, FadeToStateParams params, float landPhase)
{
	GAMEPLAY_ASSERT(IsFinite(m_pHorse->GetBoundFrame()));
	ASSERT(m_pBestJump);

	AnimControl* pAnimControl = m_pHorse->GetAnimControl();

	m_jumpAction.FadeToState(pAnimControl, jumpId, params, AnimAction::kFinishOnNonTransitionalStateReached);
	m_curJumpId = jumpId;

	GAMEPLAY_ASSERT(IsFinite(params.m_apRef));

	NavAnimHandoffDesc handoffDesc;
	handoffDesc.SetStateChangeRequestId(m_jumpAction.GetStateChangeRequestId(), jumpId);
	handoffDesc.m_motionType = m_pHorse->GetCurrentMotionType();
	// force a move set so the handoff immediately starts blending out when jump controller relinquishes control (RIP handoff exit phase)
	handoffDesc.m_handoffMmSetId = SID("*player-horse-mm-canter*");

	m_pHorse->ConfigureNavigationHandOff(handoffDesc, FILE_LINE_FUNC);

	m_pHorse->GetNdAnimationControllers()->InterruptNavControllers();

	GAMEPLAY_ASSERT(IsFinite(m_pHorse->GetBoundFrame()));

	GroundHugController* pGroundHug = m_pHorse->GetGroundHugController();
	if (pGroundHug)
	{
		pGroundHug->Reset();
	}
}

void HorseJumpController::DoJumpInternal(IHorseJump* pJump)
{
	DC::AnimNpcTopInfo* pTopInfo = m_pHorse->GetAnimControl()->TopInfo<DC::AnimNpcTopInfo>();
	pTopInfo->m_rideHorse.m_flip = pJump->WantAnimFlip(m_pHorse);
	pTopInfo->m_rideHorse.m_jumpSpeedScale = pJump->GetDesiredSpeedScale();

	pJump->DoJump(m_pHorse);
	m_pCurrJump = pJump;
	m_jumpStartedThisFrame = true;

	Player* pPlayer = m_pHorse->GetPlayerRider();
	if (pPlayer)
	{
		pPlayer->m_pRideHorseController->Cleanup(pJump->GetAnimFadeTime(), false);
	}

	if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump && g_enableVerboseHorseJumpLog))
	{
		SetColor(kMsgCon, kColorWhite.ToAbgr8());
		MsgCon("STARTING JUMP: %s\n", m_pCurrJump->GetName());
		SetColor(kMsgCon, kColorWhite.ToAbgr8());
	}
}

void HorseJumpController::PrepareAndMoveToJump(IHorseJump* pJump)
{
	pJump->SavePreparedJump(m_preparedJump);
}

void HorseJumpController::DoBestJump()
{
	ASSERT(m_pBestJump);
	ASSERT(m_wantToJump);
	m_wantToJump = false;

	if (m_pBestJump->NeedMoveToJump()) //maybe more checks here
	{
		m_pBestJump->SavePreparedJump(m_preparedJump);
	}
	else
	{
		DoJumpInternal(m_pBestJump);
	}
}

void HorseJumpController::SetupDetectorFlags(IHorseJumpDetector* pDetector)
{
	U32 flags = HorseCanJumpProbe::CanJumpFlags::kNone;
	flags |= HorseCanJumpProbe::CanJumpFlags::kCanJumpDown;

	if (m_pHorse->IsJumpCommandActive())
	{
		flags |= HorseCanJumpProbe::CanJumpFlags::kCanJumpOver;
		flags |= HorseCanJumpProbe::CanJumpFlags::kCanJumpUp;
	}
	pDetector->SetFlags((HorseCanJumpProbe::CanJumpFlags)flags);
}

F32 HorseJumpController::GetJumpPhase(const AnimStateInstance* pStateInstance) const
{
	if (!pStateInstance)
		return 0.0f;
	return pStateInstance->Phase();
}

bool HorseJumpController::IsJumpStarting() const
{
	return m_jumpStartedThisFrame || m_isJumpStarting;
}

bool HorseJumpController::JumpInFront(Point_arg contactPos, bool asSphereLineProbe/* = false*/, float searchRadius/* = 0.75f*/, bool requireJumpCommand /*= true*/, bool includeDropDown /*= true*/, bool includeNonDropDown /*= true*/) const
{
	if (m_inNoJumpRegion)
		return false;

	if (requireJumpCommand && !IsJumpCommandActive())
		return false;

	const Horse* pHorse = m_pHorse;
	const Vector toContact = VectorXz(contactPos - pHorse->GetTranslation());
	const Point startPt = PointFromXzAndY(pHorse->GetTranslation(), contactPos.Y());
	const int edgeCount = m_edgeList.Size();
	const EdgeInfo* pBestEdge = nullptr;
	float bestEdgeDist = kLargeFloat;
	for (int iEdge = 0; iEdge < edgeCount; ++iEdge)
	{
		const EdgeInfo& edge = m_edgeList.At(iEdge);

		float tt;
		Point intersection;
		Vector normal;
		if (IntersectSpherelineSegment(edge.GetVert0(), edge.GetVert1(), startPt, toContact, searchRadius, tt, intersection, normal))
		{
			const float minY = Min(edge.GetVert0().Y(), edge.GetVert1().Y());
			if (contactPos.Y() - minY > 1.0f)
				continue;

			if (tt < 0.0f || tt > 1.0f)
				continue;

			if (asSphereLineProbe || DistSqr(contactPos, intersection) < Sqr(searchRadius))
			{
				const float distance = DistXz(intersection, startPt);
				if (distance < bestEdgeDist)
				{
					const float edgeHeight = edge.GetActualDropHeightIgnoreNpcs(1.0f);
					//MsgConPauseable("edgeHeight: %.2f\n", edgeHeight);
					// do this check last because it is the most expensive
					if (edgeHeight < 0.3f || edgeHeight > 6.0f)
						continue;

					bestEdgeDist = distance;
					pBestEdge = &edge;
				}
			}
		}
	}

	if (!pBestEdge)
		return false;

	if (FALSE_IN_FINAL_BUILD(g_debugHorseStopping))
		g_prim.Draw(DebugLine(pBestEdge->GetVert0(), pBestEdge->GetVert1(), kColorCyan, kColorCyan, 4.0f, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);

	if (!includeDropDown)
	{
		const Vector edgeNormal = pBestEdge->GetFlattenedWallNormal();
		if (DotXz(edgeNormal, toContact) > 0.0f)
			return false;
	}

	if (!includeNonDropDown)
	{
		const Vector edgeNormal = pBestEdge->GetFlattenedWallNormal();
		if (DotXz(edgeNormal, toContact) < 0.0f)
			return false;
	}

	return true;
}

const AnimStateInstance* HorseJumpController::FindJumpAnimState() const
{
	const AnimControl* pControl = m_pHorse->GetAnimControl();
	ASSERT(pControl);
	const AnimStateLayer* pBaseLayer = pControl->GetBaseStateLayer();
	ASSERT(pBaseLayer);
	const AnimStateInstance* pBestStateInstance = nullptr;
	for (U32F jumpIndex = 0; jumpIndex < kNumJumpStates; ++jumpIndex)
	{
		StringId64 jumpName = kHorseJumpAnimStates[jumpIndex];
		const AnimStateInstance* pState = pBaseLayer->FindInstanceByNameNewToOld(jumpName);
		if (!pState)
			continue;
		if (!pBestStateInstance)
			pBestStateInstance = pState;
		else if (pState->Phase() < pBestStateInstance->Phase())
		{
			pBestStateInstance = pState;
		}
	}
	return pBestStateInstance;
}

//CheckIsJumping() will set *ppJumpState to the state instance that is playing the jump
//this is because it has to trawl through all of the state instances and find this instance to do it's job,
//and we might as well skip doing that work a second time if we need to use that instance for something else right away
bool HorseJumpController::CheckIsJumping(const AnimStateInstance** ppJumpState) const
{
	const AnimControl* pControl = m_pHorse->GetAnimControl();
	ASSERT(pControl);
	const AnimStateLayer* pBaseLayer = pControl->GetBaseStateLayer();
	ASSERT(pBaseLayer);
	m_isMidair = m_isJumpStarting = false;
	if (!m_pCurrJump)
		return false;

	//find if we are playing jump animation on any state of the base layer
	const AnimStateInstance* pBestStateInstance = nullptr;
	for (U32F jumpIndex = 0; jumpIndex < kNumJumpStates; ++jumpIndex)
	{
		StringId64 jumpName = kHorseJumpAnimStates[jumpIndex];
		const AnimStateInstance* pState = pBaseLayer->FindInstanceByNameNewToOld(jumpName);
		if (!pState)
			continue;
		if (!pBestStateInstance)
			pBestStateInstance = pState;
		else if (pState->Phase() < pBestStateInstance->Phase())
		{
			pBestStateInstance = pState;
		}
	}
	if (ppJumpState != nullptr)
	{
		*ppJumpState = pBestStateInstance;
	}

	if (!pBestStateInstance)
		return false;

	float endPhase = m_pCurrJump->GetEndJumpPhase();
	float landPhase = m_pCurrJump->GetLandPhase();
	float startPhase = m_pCurrJump->GetStartJumpPhase();
	float phase = pBestStateInstance->Phase();
	m_isMidair = phase < landPhase;
	m_isJumpStarting = phase < startPhase;
	if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
		MsgConPauseable("CheckIsJumping():\n  phase: %.3f\n  endPhase: %.3f\n", phase, endPhase);
	return phase < endPhase;
}

bool HorseJumpController::IsJumping() const
{
	return m_jumpStartedThisFrame || m_isJumping;
}

//like isJumping() but returns false after jump reaches landPhase, rather than endJumpPhase
bool HorseJumpController::IsMidair() const
{
	return m_jumpStartedThisFrame || m_isMidair;
}

bool HorseJumpController::IsInJumpUp() const
{
	return IsJumping() ? m_pCurrJump->IsJumpUp() : false;
}

bool HorseJumpController::IsInRunningJump() const
{
	return IsJumping() ? m_pCurrJump->IsRunningJump() : false;
}

bool HorseJumpController::IsInJumpAnim() const
{
	return m_pHorse && IsJumpAnim(m_pHorse->GetCurrentAnimState());
}

F32 HorseJumpController::GetBaseControlBlend() const
{
	static TimeFrame lastPrintTime = TimeFrameNegInfinity();
	static StringId64 lastPrintAnimName = INVALID_STRING_ID_64;
	F32 result = 1.0f;
	const AnimStateInstance* pState = nullptr;
	if (!CheckIsJumping(&pState))
		return result;
	else if (pState)
	{
		F32 landPhase = m_pCurrJump->GetLandPhase();
		F32 startJumpPhase = m_pCurrJump->GetStartJumpPhase();
		F32 endJumpPhase = m_pCurrJump->GetEndJumpPhase();
		F32 currPhase = pState->Phase();
		if (currPhase < startJumpPhase)
		{
			F32 phaseRange = startJumpPhase;
			if (phaseRange <= 0.0f)
				return result;
			result = MinMax01((startJumpPhase - currPhase) / phaseRange);
			if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump) && (GetProcessClock()->GetCurTime() > lastPrintTime || pState->GetStateName() != lastPrintAnimName))
			{
				MsgCon("Jump Name: %s\n   Current Phase: %.3f\n   Start Jump Phase: %.3f\n   Land Phase: %.3f\n   End Phase: %.3f\n   Phase Range: %.3f\n   Control Blend: %.3f\n",
					DevKitOnly_StringIdToString(pState->GetStateName()), currPhase, startJumpPhase, landPhase, endJumpPhase, phaseRange, result);
			}
		}
		else
		{
			F32 phaseRange = (1.0f - endJumpPhase) * 0.5f; //blend in control twice as quickly
			if (phaseRange <= 0.0f)
				return result;
			result = MinMax01((currPhase - endJumpPhase) / phaseRange);
			if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump) && (GetProcessClock()->GetCurTime() > lastPrintTime || pState->GetStateName() != lastPrintAnimName))
			{
				MsgCon("Jump Name: %s\n   Current Phase: %.3f\n   Start Jump Phase: %.3f\n   Land Phase: %.3f\n   End Phase: %.3f\n   Phase Range: %.3f\n   Control Blend: %.3f\n",
					DevKitOnly_StringIdToString(pState->GetStateName()), currPhase, startJumpPhase, landPhase, endJumpPhase, phaseRange, result);
			}
		}
		lastPrintAnimName = pState->GetStateName();
	}
	lastPrintTime = GetProcessClock()->GetCurTime();
	return result;
}

F32 HorseJumpController::GetControlBlend() const
{
	//should probably be controlled by horse move settings
	const F32 extraControl = m_pHorse->GetSharedJumpSettings().m_midairJumpControl;
	F32 result = MinMax01(GetBaseControlBlend() + extraControl);
	//if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
	//	MsgCon("AdjustedControlBlend: %.3f\n", result);
	return result;
}

bool HorseJumpController::JumpHasFullControl() const
{
	const bool result = GetBaseControlBlend() <= 0.0f;
	return result;
}

void HorseJumpController::UpdateJumpAp(Vector_arg stickInput)
{
	if (!IsJumping())
		return;

	const DC::HorseJumpSharedSettings& settings = m_pHorse->GetSharedJumpSettings();
	if (!settings.m_allowControlDuringStartJump && IsJumpStarting())
		return;

	Vector forwardDir = SafeNormalize(VectorXz(GetLocalZ(m_pHorse->GetRotation())), kZero);
	//if we can't normalize for some reason (horse is facing straight up or down?) just give up
	if (!IsNormal(forwardDir))
		return;

	const float controlScale = GetForceJumpDir().Valid() ? 0.0f : settings.m_midairJumpControl;
	const Vector slerpedDir = Slerp(forwardDir, stickInput, controlScale * GetProcessDeltaTime());

	if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseAdjustApForColl))
	{
		Point startPos = m_pHorse->GetTranslation() + kUnitYAxis;
		g_prim.Draw(DebugArrow(startPos, slerpedDir, kColorGreen, 1.0f, PrimAttrib(kPrimDisableDepthTest)));
		g_prim.Draw(DebugArrow(startPos, stickInput, kColorOrange, 1.0f, PrimAttrib(kPrimDisableDepthTest)));
	}

	const Quat deltaRot = RotationBetween(forwardDir.GetVec4(), slerpedDir.GetVec4());

	ASSERT(m_pCurrJump);
	m_pCurrJump->UpdateJumpAp(deltaRot);
}

void HorseJumpController::UpdateJumpRegions()
{
	PROFILE_AUTO(Player);

	const U32F maxTags = 1;
	const EntityDB::Record* tags[maxTags];
	
	const I32F numLongJumpTags = g_regionManager.GetRecordsByPositionAndKey(tags, maxTags, m_pHorse->GetTranslation(), 1.0f, SID("player"), SID("horse-long-drop-down"));
	m_longDropDownEnabled = numLongJumpTags > 0;
	
	const I32F numNoJumpUpTags = g_regionManager.GetRecordsByPositionAndKey(tags, maxTags, m_pHorse->GetTranslation(), 1.0f, SID("player"), SID("horse-no-jump-up"));
	m_jumpUpDisabled = numNoJumpUpTags > 0;
	
	const I32F numVaultDropDownTags = g_regionManager.GetRecordsByPositionAndKey(tags, maxTags, m_pHorse->GetTranslation(), 1.0f, SID("player"), SID("horse-vault-drop-down"));
	m_vaultDropDownEnabled = numVaultDropDownTags > 0;

	const I32F numNoVaultTags = g_regionManager.GetRecordsByPositionAndKey(tags, maxTags, m_pHorse->GetTranslation(), 1.0f, SID("player"), SID("horse-no-vault"));
	m_regularVaultEnabled = numNoVaultTags == 0;

	m_regularVaultEnabled = m_regularVaultEnabled && (!m_vaultDropDownEnabled || g_regionManager.GetRecordsByPositionAndKey(tags, maxTags, m_pHorse->GetTranslation(), 1.0f, SID("player"), SID("allow-horse-vault")) > 0);
	
	const I32F numNoJumpRegionTags = g_regionManager.GetRecordsByPositionAndKey(tags, maxTags, m_pHorse->GetTranslation(), 1.0f, SID("player"), SID("horse-no-jump"));
	m_inNoJumpRegion = numNoJumpRegionTags > 0;
	
	const I32F numPermissiveVaultTags = g_regionManager.GetRecordsByPositionAndKey(tags, maxTags, m_pHorse->GetTranslation(), 1.0f, SID("player"), SID("horse-permissive-vault"));
	m_inPermissiveVaultRegion = numPermissiveVaultTags > 0;

	const I32F numDistanceHackTags = g_regionManager.GetRecordsByPositionAndKey(tags, maxTags, m_pHorse->GetTranslation(), 1.0f, SID("player"), SID("horse-jump-distance-hack"));
	m_inDistanceHackRegion = numDistanceHackTags > 0;

	if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
	{
		if (m_longDropDownEnabled)
		{
			MsgConPauseable("INSIDE LONG DROP DOWN REGION\n");
		}

		if (m_jumpUpDisabled)
		{
			MsgConPauseable("INSIDE NO JUMP UP REGION\n");
		}

		if (m_vaultDropDownEnabled)
		{
			MsgConPauseable("INSIDE VAULT DROP DOWN REGION\n");
		}

		if (m_inNoJumpRegion)
		{
			MsgConPauseable("INSIDE NO JUMP REGION\n");
		}

		if (m_inPermissiveVaultRegion)
		{
			MsgConPauseable("INSIDE PERMISSIVE VAULT REGION\n");
		}

		if (!m_regularVaultEnabled && !m_vaultDropDownEnabled)
		{
			MsgConPauseable("INSIDE NO VAULT REGION\n");
		}

		if (m_inDistanceHackRegion)
		{
			MsgConPauseable("INSIDE DISTANCE HACK REGION\n");
		}
	}
}

bool HorseJumpController::TryJump()
{
	ASSERT(m_wantToJump);

	if (!JumpHasFullControl() && !IsJumpStarting() && !m_preparedJump.IsValid())
	{
		DoBestJump();
		return true;
	}
	return false;
}

void HorseJumpController::OnHorseUpdate()
{
	PROFILE(AI, HorseJumpControllerUpdate);
	ASSERT(m_pHorse);
	m_jumpStartedThisFrame = false;

	//we don't need to check for jumps if the horse is not player controlled, otherwise our framerate will nosedive when we have multiple horses
	//NPC horse jumps should be done through TAPS
	if (m_pHorse->IsInDebugFly() || !m_pHorse->IsPlayerControlled())
		return;

	if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
	{
		const bool horseIsWalking = m_pHorse->IsTrottingOrSlower() && !m_pHorse->IsIdle();
		const bool horseIsCantering = m_pHorse->IsCantering();
		const bool horseIsIdle = m_pHorse->IsIdle();

		MsgConPauseable("Horse speed: %.2f\n", m_pHorse->GetSpeedPs());

		MsgConPauseable(PRETTY_PRINT_BOOL(horseIsWalking));
		MsgConPauseable(PRETTY_PRINT_BOOL(horseIsCantering));
		MsgConPauseable(PRETTY_PRINT_BOOL(horseIsIdle));
	}


	UpdateJumpRegions();

	UpdateJumpButton();

	const AnimStateInstance* pAnimState;
	AnimControl* pAnimControl = m_pHorse->GetAnimControl();
	ASSERT(pAnimControl);

	bool isJumping = CheckIsJumping(&pAnimState);

	if (m_jumpAction.IsValid() && m_jumpAction.WasTransitionTakenThisFrame())
	{
		GAMEPLAY_ASSERT(IsFinite(m_pHorse->GetLocator()));
		if (m_pCurrJump)
		{
			const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
			const AnimStateInstance* pInstance = pBaseLayer->FindInstanceByNameNewToOld(m_curJumpId);

			if (AnimStateInstance* pDestInstance = m_jumpAction.GetTransitionDestInstance(pAnimControl))
			{
				Locator apLoc = g_disableHorseJumpProceduralAdjustments ? m_pCurrJump->GetAnimAp(m_pHorse) : m_pCurrJump->GetUpdatedAp(m_pHorse, pInstance);

				AnimStateInstance* pMutableAnimState = const_cast<AnimStateInstance*>(pAnimState);
				pMutableAnimState->SetApLocator(apLoc);

				SelfBlendAction::Params sbParams;
				sbParams.m_blendParams = NavUtil::ConstructSelfBlendParams(pDestInstance, 0.7f);
				sbParams.m_constraintPhase = 1.0f;
				sbParams.m_destAp = apLoc;
				sbParams.m_apChannelId = m_pCurrJump->GetApName();

				m_jumpAction.ConfigureSelfBlend(m_pHorse, sbParams);
			}
		}
		else
		{
			m_jumpAction.Reset();
			EndJump();
		}
		GAMEPLAY_ASSERT(IsFinite(m_pHorse->GetLocator()));
	}

	if (isJumping)
	{
		if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
		{
			SetColor(kMsgCon, kColorGreen.ToAbgr8());
			MsgCon("Current Jump: %s\n", m_pCurrJump->GetName());
			MsgCon("Jump Control Blend: %.3f\n", GetControlBlend());
			SetColor(kMsgCon, kColorWhite.ToAbgr8());
		}

		if (FALSE_IN_FINAL_BUILD(g_debugHorseJumpSpeedScale))
		{
			const DC::AnimNpcTopInfo* pTopInfo = m_pHorse->GetAnimControl()->TopInfo<const DC::AnimNpcTopInfo>();
			MsgConPauseable("%s\n", m_pCurrJump->GetName());
			MsgConPauseable("  Speed Scale: %.2f\n", pTopInfo->m_rideHorse.m_jumpSpeedScale);
		}

		m_isJumping = true;

		if (!m_pHorse->IsInScriptState())
		{
			Locator newApRef = g_disableHorseJumpProceduralAdjustments ? m_pCurrJump->GetAnimAp(m_pHorse) : m_pCurrJump->GetUpdatedAp(m_pHorse, pAnimState);
			m_jumpAction.SetApReference(pAnimControl, newApRef);
		}
	}
	else
	{
		EndJump();
	}

	m_jumpAction.Update(pAnimControl);

	if (!JumpHasFullControl() && !IsJumpStarting() && !m_preparedJump.IsValid())
	{
		m_wantToJump = GatherJumps();
		if (m_wantToJump)
			TryJump();
	}

	if (g_enabledHorseJumps.m_jumpDetector)
		GatherValidJumpEdges();

	if (g_enabledHorseJumps.m_cliffDetector)
		GatherVirtualWalls();

	if (g_enabledHorseJumps.m_jumpDetector)
		KickJumpDetector();

	if (g_enabledHorseJumps.m_cliffDetector)
		KickCliffDetector();

	//kicking a jump that we are moving to the entry on would overwrite data we still need
	if (!m_preparedJump.IsValid())
		Kick();

	if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
		MsgConPauseable(PRETTY_PRINT_BOOL(m_canJump));

	//DebugDrawHorseHoofProbeSettings();
}

void HorseJumpController::UpdateStatus()
{
	//we don't need to check for jumps if the horse is not player controlled, otherwise our framerate will nosedive when we have multiple horses
	//NPC horse jumps should be done through TAPS
	if (m_pHorse->IsInDebugFly() || !m_pHorse->IsPlayerControlled())
		return;

	if (m_isJumping && !m_pHorse->IsInScriptState())
	{

		GroundHugController* pGroundHug = m_pHorse->GetGroundHugController();
		if (pGroundHug)
		{
			//reset ground hug upDir and spring since we are taking control away. Updir is set to apRef's upDir to match ground normal
			//BoundFrame apRef;
			//bool valid = m_jumpAction.GetApReference(m_pHorse->GetAnimControl(), &apRef);
			//GAMEPLAY_ASSERT(valid);
			//const Vector upDir = GetLocalY(apRef.GetLocator().Rot());
			const Vector upDir = GetLocalY(m_pHorse->GetRotation());
			pGroundHug->Reset(&upDir);
		}
	}
}

void HorseJumpController::EndJump()
{
	if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump && m_pCurrJump))
	{
		SetColor(kMsgCon, kColorRed.ToAbgr8());
		MsgCon("JUMP ENDED: %s\n", m_pCurrJump->GetName());
		SetColor(kMsgCon, kColorWhite.ToAbgr8());
	}

	// at low framerates jumps with only a few frames between their rumble-phase and end-phase may not yet have rumbled
	if (m_pCurrJump && !m_pCurrJump->HasRumbled())
		m_pCurrJump->DoRumble();

	m_curJumpId = INVALID_STRING_ID_64;
	m_pCurrJump = nullptr;
	m_isJumping = false;
}

bool HorseJumpController::IsJumpAnim(StringId64 animStateName) const
{
	for (U32F i = 0; i < kNumJumpStates; ++i)
	{
		StringId64 jumpAnim = kHorseJumpAnimStates[i];
		if (animStateName == jumpAnim)
			return true;
	}
	return false;
}

bool HorseJumpController::AreRuningJumpsForceAllowed() const
{
	const I64 gameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
	return gameFrame <= m_forceAllowRunningJumpFrame + 1;
}

bool HorseJumpController::AreRunningJumpsValid() const
{
	if (AreRuningJumpsForceAllowed())
		return true;

	const bool isRunning = m_pHorse->IsCantering() || IsInRunningJump();
	return isRunning;
}

void HorseJumpController::ScriptForceEnableRunningJump()
{
	m_forceAllowRunningJumpFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
}

void HorseJumpController::ScriptForceJumpSpeed(float desiredSpeed)
{
	m_scriptForceJumpSpeedFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
	m_scriptedJumpSpeed = Max(desiredSpeed, 0.1f);
}

float HorseJumpController::GetScriptedJumpSpeed() const
{
	const I64 gameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
	if (gameFrame <= m_scriptForceJumpSpeedFrame + 1)
		return m_scriptedJumpSpeed;
	else
		return -1.0f;
}

bool HorseJumpController::IsBucketInputValid(HorseJumpBucket bucket) const
{
	if (g_playerOptions.m_accessibilityAutoTraversal)
		return true;

	switch (bucket)
	{
	case kHorseJumpBucketRunSprintPressed:
	case kHorseJumpBucketSprintPressed:
	case kHorseJumpBucketWalkSprintPressed:
		return IsJumpCommandActive();
	default:
		return true;
	}
}

bool HorseJumpController::IsBucketValid(HorseJumpBucket bucket, bool kicking) const
{
	// we need to always kick buckets regardless of whether or not they are valid so accessiblity audio cues work.
	if (kicking)
		return true;

	if (!IsBucketInputValid(bucket))
		return false;

	const bool runningJumpsValid = AreRunningJumpsValid();

	switch (bucket)
	{
	case kHorseJumpBucketRunSprintPressed:
		return runningJumpsValid;
	case kHorseJumpBucketRun:
		return runningJumpsValid || kicking;
	case kHorseJumpBucketWalk:
		return !m_pHorse->IsIdle();
		//return (!m_pHorse->IsIdle() && m_pHorse->IsTrottingOrSlower());
	case kHorseJumpBucketWalkSprintPressed:
		return !m_pHorse->IsIdle();
		//return ((!m_pHorse->IsIdle() && m_pHorse->IsTrottingOrSlower()));
	case kHorseJumpBucketAlways:
	case kHorseJumpBucketDetectors:
	case kHorseJumpBucketCliffDetectors:
	case kHorseJumpBucketSprintPressed: // would have returned false in IsBucketInputValid check
		return true;
	default:
		ALWAYS_HALTF(("HorseJumpController::IsBucketValid given invalid horse jump bucket %d", bucket));
		return false;
	}
}

bool HorseJumpController::IsJumpCommandActive() const
{
	if (m_pHorse->GetMoveController()->IsSprintCommandActive())
		return true;

	Player* pPlayer = m_pHorse->GetPlayerRider(HorseDefines::kRiderFront);
	if (pPlayer && pPlayer->GetJoypad().IsCommandActive(SID("horse-jump")))
		return true;

	int jumpCommandExtraFrames = m_pHorse->GetSharedJumpSettings().m_horseJumpButtonGraceFrames;
	if (EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused - jumpCommandExtraFrames <= m_lastJumpButtonFrame)
		return true;

	return false;
}

void HorseJumpController::UpdateJumpButton()
{
	const Player* pPlayer = m_pHorse->GetPlayerRider(HorseDefines::kRiderFront);
	const bool jumpButtonActive = pPlayer && pPlayer->GetJoypad().IsCommandActive(SID("horse-jump"));
	const bool sprintCommandActive = m_pHorse->GetMoveController()->IsSprintCommandActive();
	if (sprintCommandActive || jumpButtonActive)
		m_lastJumpButtonFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
}

bool HorseJumpController::HasPreparedJump() const
{
	return m_preparedJump.IsValid();
}

void HorseJumpController::ClearPreparedJump()
{
	m_preparedJump.Reset();
}

Point HorseJumpController::GetPreparedJumpStartPos() const
{
	GAMEPLAY_ASSERT(HasPreparedJump());
	return m_preparedJump.GetTargetPt();
}

Vector HorseJumpController::GetPreparedJumpFacingDir() const
{
	GAMEPLAY_ASSERT(HasPreparedJump());
	return m_preparedJump.GetTargetFacingDir();
}

bool HorseJumpController::IsJumpDetectorEnabled() const
{
	return g_enabledHorseJumps.m_jumpDetector;
}

bool HorseJumpController::TryPreparedJump()
{
	GAMEPLAY_ASSERT(m_preparedJump.IsValid());

	m_preparedJump.GetJump()->DoPreparedJump(m_pHorse, m_preparedJump);
	m_pCurrJump = m_preparedJump.GetJump();

	m_preparedJump.Reset();
	return true;
}

float HorseJumpController::GetCameraYSpringConstant(float defaultValue)
{
	IHorseJump* pJump = GetActiveJump();
	if (!pJump)
		return defaultValue;

	const AnimStateInstance* pJumpInstance = FindJumpAnimState();
	if (!pJumpInstance)
		return defaultValue;
	//GAMEPLAY_ASSERT(pJumpInstance);
	const float phase = pJumpInstance->GetPhase();
	const float startJumpPhase = pJump->GetStartJumpPhase();
	const float landPhase = pJump->GetLandPhase();

	const float midPhase = startJumpPhase + ((landPhase - startJumpPhase) * 0.5f);
	float tt = 1.0f;
	if (phase < midPhase)
		tt = MinMax01(LerpScale(startJumpPhase, midPhase, 0.0f, 1.5f, phase));
	else if (phase > midPhase)
		tt = MinMax01(LerpScale(midPhase, landPhase, 1.5f, 0.0f, phase));

	const float jumpYSpringConstant = pJump->GetCameraYSpringConstant(defaultValue);
	const float result = Lerp(defaultValue, jumpYSpringConstant, tt);

	return result;
}

void HorseJumpController::OnKillProcess()
{
	for (U32F iBucket = 0; iBucket < kHorseJumpBucketNum; ++iBucket)
	{
		IHorseJump* const* jumps;
		const U32F jumpCount = m_jumpList.GetJumps(jumps, (HorseJumpBucket)iBucket);
		for (U32F iJump = 0; iJump < jumpCount; ++iJump)
		{
			jumps[iJump]->OnKillProcess();
		}
	}
}

void HorseJumpController::ScriptForceJumpDir(Vector_arg dir)
{
	const Vector normalDir = AsUnitVectorXz(dir, kZero);
	GAMEPLAY_ASSERT(IsNormal(normalDir));
	m_forceJumpDirFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
	m_forceJumpDir = normalDir;
}

Maybe<Vector> HorseJumpController::GetForceJumpDir() const
{
	const I64 gameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
	if (gameFrame <= m_forceJumpDirFrame + 1)
	{
		if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
		{
			g_prim.Draw(DebugArrow(m_pHorse->GetTranslation() + Vector(0.0f, 0.7f, 0.0f), m_forceJumpDir, kColorPink, 0.5f, PrimAttrib(kPrimEnableHiddenLineAlpha), "forceJumpDir", 0.5f));

		}
		return m_forceJumpDir;
	}
	return MAYBE::kNothing;
}

void HorseJumpController::ScriptForceJumpDestination(Locator forceJumpDestination)
{
	GAMEPLAY_ASSERT(IsReasonable(forceJumpDestination));
	m_forceJumpDestinationFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
	forceJumpDestination = Locator(forceJumpDestination.GetTranslation(), QuatFromXZDir(GetLocalZ(forceJumpDestination))); // ensure APRef is upright
	m_forceJumpDestination = forceJumpDestination;
}

Maybe<Locator> HorseJumpController::GetForceJumpDestination() const
{
	const I64 gameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
	if (gameFrame <= m_forceJumpDestinationFrame + 1)
	{
		if (FALSE_IN_FINAL_BUILD(g_debugDrawHorseJump))
		{
			g_prim.Draw(DebugCoordAxesLabeled(m_forceJumpDestination, "forceJumpDestination", 0.8f, PrimAttrib(kPrimEnableHiddenLineAlpha), 2.0f, kColorWhite, 0.5f));
		}
		return m_forceJumpDestination;
	}
	return MAYBE::kNothing;
}

void HorseJumpController::DebugDrawHorseHoofProbeSettings() const
{
	STRIP_IN_FINAL_BUILD;

	const DC::HorseJumpSharedSettings& jumpSettings = m_pHorse->GetSharedJumpSettings();
	float horseWidth = jumpSettings.m_minGroundWidth;
	float horseLength = jumpSettings.m_minGroundLength;

	const Locator& horseLoc = m_pHorse->GetLocator();
	const Vector leftVec = GetLocalX(horseLoc.GetRotation());
	const Vector forwardVec = GetLocalZ(horseLoc.GetRotation());
	//front left
	g_prim.Draw(DebugCross(horseLoc.GetTranslation() + leftVec * (horseWidth / 2) + forwardVec * (horseLength / 2), 1.0f, 1.0f, kColorGreen));
	//front right
	g_prim.Draw(DebugCross(horseLoc.GetTranslation() - leftVec * (horseWidth / 2) + forwardVec * (horseLength / 2), 1.0f, 1.0f, kColorGreen));
	//back left
	g_prim.Draw(DebugCross(horseLoc.GetTranslation() + leftVec * (horseWidth / 2) - forwardVec * (horseLength / 2), 1.0f, 1.0f, kColorGreen));
	//back right
	g_prim.Draw(DebugCross(horseLoc.GetTranslation() - leftVec * (horseWidth / 2) - forwardVec * (horseLength / 2), 1.0f, 1.0f, kColorGreen));
}

void HorseJumpController::CheckForTypos() const
{
	STRIP_IN_FINAL_BUILD;

	//no one but me cares
#ifndef HTOWNSEND
	return;
#else

	//check that all jump anim names are found in kHorseJumpAnimStates
	//and that all entries in kHorseJumpAnimStates correspond to at least one jump
	//I lost too much time to making typos in the anim names

	bool animUsed[kNumJumpStates];
	memset(animUsed, false, sizeof(animUsed));

	//we don't care about detectors, they don't have a jump anim
	for (int iBucket = 0; iBucket < kHorseJumpBucketDetectors; ++iBucket)
	{
		//const ptr hell
		const IHorseJump* const* jumps;
		const U32F jumpCount = m_jumpList.GetJumps(jumps, (HorseJumpBucket)iBucket);
		for (int iJump = 0; iJump < jumpCount; ++iJump)
		{
			const IHorseJump* pJump = jumps[iJump];
			const StringId64 jumpAnim = pJump->GetJumpAnimState();
			bool foundJump = false;
			for (int iAnim = 0; iAnim < kNumJumpStates; ++iAnim)
			{
				if (jumpAnim == kHorseJumpAnimStates[iAnim])
				{
					foundJump = true;
					animUsed[iAnim] = true;
					break;
				}
			}
 			ALWAYS_ASSERTF(foundJump, ("jump %s's anim %s not found in kHorseJumpAnimStates! Typo detected!", pJump->GetName(), DevKitOnly_StringIdToString(jumpAnim)));
		}
	}

	for (int iUsed = 0; iUsed < kNumJumpStates; ++iUsed)
	{
		ALWAYS_ASSERTF(animUsed[iUsed], ("anim %s is in kHorseJumpAnimStates, but no jump uses it! Typo detected!", DevKitOnly_StringIdToString(kHorseJumpAnimStates[iUsed])));
	}
#endif
}

IHorseJump* HorseJumpController::GetActiveJump() const
{
	return m_pCurrJump;
}

HorseJumpController* CreateHorseJumpController() { return NDI_NEW HorseJumpController; }

//I can't wait for C++20
CONST_EXPR StringId64  HorseJumpController::kHorseJumpAnimStates[];



class HorseJumpAnimAction : public NdSubsystemAnimAction
{
	typedef NdSubsystemAnimAction ParentClass;
	virtual float GetGroundAdjustFactor(const AnimStateInstance* pInstance, float desired) const override;
	virtual float GetLegIkEnabledFactor(const AnimStateInstance* pInstance, float desired) const override { return GetGroundAdjustFactor(pInstance, desired); }
	//virtual void InstanceDestroy(AnimStateInstance* pInst) override
	//{
	//	ParentClass::InstanceDestroy(pInst);
	//	MsgConPauseable("HorseJumpAnimAction::InstanceDestroy()\n");
	//}
};
TYPE_FACTORY_REGISTER(HorseJumpAnimAction, NdSubsystemAnimAction);

float HorseJumpAnimAction::GetGroundAdjustFactor(const AnimStateInstance* pInstance, float desired) const
{
	const Horse* pHorse = Horse::FromProcess(GetOwnerCharacter());
	if (!pHorse)
		return 1.0f;

	return pHorse->GetAnimationControllers()->GetHorseJumpController()->GetGroundAdjustFactor();
}
