/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/character-leg-raycaster.h"

#include "ndlib/anim/anim-options.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-options.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/dev/render-options.h"
#include "ndlib/tools-shared/patdefs.h"

#include "gamelib/gameplay/ai/agent/nav-character-adapter.inl"
#include "gamelib/gameplay/character-motion-match-locomotion.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/ndphys/collision-cast-interface.h"
#include "gamelib/ndphys/collision-cast.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/havok-collision-cast.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/ndphys/rigid-body.h"

/// --------------------------------------------------------------------------------------------------------------- ///
FINAL_CONST bool g_debugLegRaycasterMeshKicks = false;
FINAL_CONST bool g_debugLegRaycasterCollKicks = false;
FINAL_CONST bool g_debugLegRaycasterCollKicksIk = false;
FINAL_CONST bool g_debugLegRaycasterCollKicksPredictOnStairs = false;
FINAL_CONST bool g_debugLegRaycasterCollKicksAudioGround = false;

const float kLegRayCastDistance = 1.0f;
const float kSlatCastRadius		= 0.3f;
const float kPredictProbeRadius = 0.01f;
//const float kAudioProbeRadius	= 0.01f;
const float kOnStairsCastRadius = 0.02f;

const LegRaycaster::Results LegRaycaster::kInvalidResults;

FINAL_CONST bool g_requireNavMeshForPredictiveRaycasts = false;

/// --------------------------------------------------------------------------------------------------------------- ///
const Color kDebugColors[LegRaycaster::kMaxNumRays] =
{
	kColorCyan,
	kColorMagenta,
	kColorGreen,
	kColorRed,
	kColorCyanTrans,
	kColorMagentaTrans,
	kColorGreenTrans,
	kColorRedTrans,
};

/// --------------------------------------------------------------------------------------------------------------- ///
const char* kMeshDebugStrings[LegRaycaster::kMaxNumRays] =
{
	"L-m",
	"R-m",
	"FL-m",
	"FR-m",
	"L-m (pred)",
	"R-m (pred)",
	"FL-m (pred)",
	"FR-m (pred)",
};

const char* kColDebugStrings[LegRaycaster::kMaxNumRays] =
{
	"L-c",
	"R-c",
	"FL-c",
	"FR-c",
	"L-c (pred)",
	"R-c (pred)",
	"FL-c (pred)",
	"FR-c (pred)",
};

/// --------------------------------------------------------------------------------------------------------------- ///
#define CHECK_CAST_POINT_NO_SOURCE(p)                                                                                  \
	GAMEPLAY_ASSERTF(IsFinite(p) && MaxComp(Abs(p)) < 1e16f,                                                           \
					 ("Someone sends QNaN or huge numbers (%f, %f, %f) to leg raycaster.",                             \
					  (F32)p.X(),                                                                                      \
					  (F32)p.Y(),                                                                                      \
					  (F32)p.Z()))

/// --------------------------------------------------------------------------------------------------------------- ///
LegRaycaster::LegRaycaster()
	: m_pCollisionProbesJobCounter(nullptr)
{
	Init();

	m_collisionProbeKickTime = TimeFrameNegInfinity();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::SetCharacter(const Character* pChar, bool forceQuadruped)
{
	ANIM_ASSERT(pChar);
	m_hSelf = pChar;
	m_isQuadruped = forceQuadruped || (pChar && pChar->IsQuadruped());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::Init()
{
	m_usePredictiveRays = false;
	m_useMeshRaycasts = false;
	m_isQuadruped = m_hSelf.HandleValid() ? m_hSelf.ToProcess()->IsQuadruped() : false;
	m_mrcMinValidFrame = -1;
	m_meshRayIgnorePid = -1;
	m_oneRaycastDone = false;
	m_raycastMode = kModeDefault;
	m_minValidTime = GetClock()->GetCurTime() + Frames(1);

	memset(m_setup, 0, sizeof(m_setup));
	for (U32 ii = 0; ii<kMaxNumRays; ii++)
	{
		m_setup[ii].m_disabled = true;
	}
	m_predictOnStairs.Reset();
	m_audioGround.Reset();

	if (m_pCollisionProbesJobCounter)
	{
		ndjob::WaitForCounterAndFree(m_pCollisionProbesJobCounter);
		m_pCollisionProbesJobCounter = nullptr;
	}

	ClearResults();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::EnterNewParentSpace(const Transform& matOldToNew,
									   const Locator& oldParentSpace,
									   const Locator& newParentSpace)
{
	for (int i = 0; i < kMaxNumRays; ++i)
	{
		m_setup[i].m_posPs = m_setup[i].m_posPs * matOldToNew;

		CHECK_CAST_POINT_NO_SOURCE(m_setup[i].m_posPs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector LegRaycaster::GetPredictiveRayStartAdjustment() const
{
	ANIM_ASSERT(m_usePredictiveRays);

	const Character* pChar = m_hSelf.ToProcess();
	ANIM_ASSERT(pChar);
	const Vector velPs = pChar->GetVelocityPs();
	const float dt = GetProcessDeltaTime();
	CONST_EXPR int framesInFutureToCheck = 3;
	const Vector adjustmentVec = velPs * (dt * framesInFutureToCheck);
	return adjustmentVec;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::UpdateRayCastMode()
{
	Mode newMode = kModeDefault;
	const Character* pChar = m_hSelf.ToProcess();

	newMode = (Mode)pChar->GetDesiredLegRayCastMode();

	if (newMode != m_raycastMode)
	{
		Init();

		m_raycastMode = newMode;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool LegRaycaster::ConstrainPointToNavMesh(Point_arg ptWs,
										   const SimpleNavControl* pNavControl,
										   Point& outConstrainedPt) const
{
	outConstrainedPt = ptWs;

	const float searchRadius = 2.0f * pNavControl->GetMaximumNavAdjustRadius();
	FindBestNavMeshParams fbnmParams;
	fbnmParams.m_cullDist	= 1.0f;
	fbnmParams.m_yThreshold = 3.0f;
	fbnmParams.m_pointWs	= ptWs;
	fbnmParams.m_obeyedStaticBlockers = pNavControl->GetObeyedStaticBlockers();
	EngineComponents::GetNavMeshMgr()->FindNavMeshWs(&fbnmParams);

	const Point adjustedPtWs = PointFromXzAndY(ptWs, fbnmParams.m_nearestPointWs.Y());

	// give up if we don't find navmesh
	const NavMesh* pNavMesh = fbnmParams.m_pNavMesh;
	const NavPoly* pNavPoly = fbnmParams.m_pNavPoly;
	if (!pNavMesh || !pNavPoly)
		return false;

	NavMesh::FindPointParams fpParams;
	fpParams.m_pStartPoly	= pNavPoly;
	fpParams.m_searchRadius = searchRadius;
	fpParams.m_depenRadius	= 0.1f;

	const Point searchPosLs = pNavMesh->WorldToLocal(adjustedPtWs);
	fpParams.m_point		= searchPosLs;

	pNavMesh->FindNearestPointLs(&fpParams);

	if (fpParams.m_pPoly && (fpParams.m_dist < fpParams.m_searchRadius))
	{
		const Point constrainedPosWs = pNavMesh->LocalToWorld(fpParams.m_nearestPoint);
		ANIM_ASSERT(DistXz(constrainedPosWs, ptWs) < 5.0f);
		// g_prim.Draw(DebugArrow(searchPosWs, constrainedPosWs, kColorGreen, 0.5f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
		outConstrainedPt = constrainedPosWs;
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::SetProbePointsPs(Point_arg leftPosPs, Point_arg rightPosPs)
{
	ANIM_ASSERT(!m_isQuadruped);
	ANIM_ASSERT(IsReasonable(leftPosPs));
	ANIM_ASSERT(IsReasonable(rightPosPs));

	CHECK_CAST_POINT_NO_SOURCE(leftPosPs);
	CHECK_CAST_POINT_NO_SOURCE(rightPosPs);
	m_setup[kBackLeftLeg].m_posPs = leftPosPs;
	m_setup[kBackRightLeg].m_posPs = rightPosPs;
	m_setup[kBackLeftLeg].m_disabled = false;
	m_setup[kBackRightLeg].m_disabled = false;

	m_mrcResults[kFrontLeftLeg]	 = Results();
	m_mrcResults[kFrontRightLeg] = Results();
	m_colResults[kFrontLeftLeg]	 = Results();
	m_colResults[kFrontRightLeg] = Results();

	CONST_EXPR int kStartIndex = kQuadLegCount;
	if (m_usePredictiveRays)
	{
		const Character* pSelf = m_hSelf.ToProcess();
		NavCharacterAdapter navCharAdapter = NavCharacterAdapter::FromProcess(pSelf);
		const SimpleNavControl* pNavControl = navCharAdapter.GetNavControl();

		const Vector adjustmentVec = GetPredictiveRayStartAdjustment();

		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		m_setup[kStartIndex + kBackLeftLeg].m_disabled	= !ConstrainPointToNavMesh(leftPosPs + adjustmentVec, pNavControl, m_setup[kStartIndex + kBackLeftLeg].m_posPs);
		m_setup[kStartIndex + kBackRightLeg].m_disabled	= !ConstrainPointToNavMesh(rightPosPs + adjustmentVec, pNavControl, m_setup[kStartIndex + kBackRightLeg].m_posPs);
	}
	else
	{
		m_setup[kStartIndex + kBackLeftLeg].m_disabled	= true;
		m_setup[kStartIndex + kBackRightLeg].m_disabled	= true;

	}

	//Once both feet have crossed the plane, disable it
	if (m_planeFilterValid)
	{
		const Character* pSelf = m_hSelf.ToProcess();

		if (IsCollisionPointValid(pSelf->GetParentSpace().TransformPoint(leftPosPs))
			&& IsCollisionPointValid(pSelf->GetParentSpace().TransformPoint(rightPosPs)))
		{
			m_planeFilterValid = false;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::SetProbePointsPs(Point_arg backLeftPosPs,
									Point_arg backRightPosPs,
									Point_arg frontLeftPosPs,
									Point_arg frontRightPosPs,
									bool frontLeftValid /* = true */,
									bool frontRightValid /* = true */)
{
	ANIM_ASSERT(IsReasonable(backLeftPosPs));
	ANIM_ASSERT(IsReasonable(backRightPosPs));
	ANIM_ASSERT(!frontLeftValid || IsReasonable(frontLeftPosPs));
	ANIM_ASSERT(!frontLeftValid || IsReasonable(frontRightPosPs));

	m_setup[kBackLeftLeg].m_posPs	   = backLeftPosPs;
	m_setup[kBackRightLeg].m_posPs	   = backRightPosPs;
	m_setup[kBackLeftLeg].m_disabled	= false;
	m_setup[kBackRightLeg].m_disabled	= false;
	m_setup[kFrontLeftLeg].m_posPs	   = frontLeftPosPs;
	m_setup[kFrontRightLeg].m_posPs	   = frontRightPosPs;
	m_setup[kFrontLeftLeg].m_disabled  = !frontLeftValid;
	m_setup[kFrontRightLeg].m_disabled = !frontRightValid;

	CONST_EXPR int kPredictiveStartIndex = kQuadLegCount;
	if (m_usePredictiveRays)
	{
		const Character* pSelf = m_hSelf.ToProcess();
		NavCharacterAdapter navCharAdapter = NavCharacterAdapter::FromProcess(pSelf);
		const SimpleNavControl* pNavControl = navCharAdapter.GetNavControl();

		const Vector adjustmentVec = GetPredictiveRayStartAdjustment();

		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		m_setup[kPredictiveStartIndex + kBackLeftLeg].m_disabled		= !ConstrainPointToNavMesh(backLeftPosPs + adjustmentVec, pNavControl, m_setup[kPredictiveStartIndex + kBackLeftLeg].m_posPs);
		m_setup[kPredictiveStartIndex + kBackRightLeg].m_disabled		= !ConstrainPointToNavMesh(backRightPosPs + adjustmentVec, pNavControl, m_setup[kPredictiveStartIndex + kBackRightLeg].m_posPs);
		m_setup[kPredictiveStartIndex + kFrontLeftLeg].m_disabled		= !ConstrainPointToNavMesh(frontLeftPosPs + adjustmentVec, pNavControl, m_setup[kPredictiveStartIndex + kFrontLeftLeg].m_posPs);
		m_setup[kPredictiveStartIndex + kFrontRightLeg].m_disabled		= !ConstrainPointToNavMesh(frontRightPosPs + adjustmentVec, pNavControl, m_setup[kPredictiveStartIndex + kFrontRightLeg].m_posPs);

		//for (int i = kPredictiveStartIndex; i < kMaxNumRays; ++i)
		//{
		//	MsgConPauseable("Predictive %s ray disabled? %s\n", LegIndexToString(i - kPredictiveStartIndex), m_setup[i].m_disabled ? "YES" : "NO");
		//}
	}
	else
	{
		m_setup[kPredictiveStartIndex + kBackLeftLeg].m_disabled		= true;
		m_setup[kPredictiveStartIndex + kBackRightLeg].m_disabled		= true;
		m_setup[kPredictiveStartIndex + kFrontLeftLeg].m_disabled		= true;
		m_setup[kPredictiveStartIndex + kFrontRightLeg].m_disabled		= true;
	}

	//Once both feet have crossed the plane, disable it
	if (m_planeFilterValid)
	{
		const Character* pSelf = m_hSelf.ToProcess();

		const Locator& parentSpace = pSelf->GetParentSpace();

		if (IsCollisionPointValid(parentSpace.TransformPoint(backLeftPosPs))
			&& IsCollisionPointValid(parentSpace.TransformPoint(backRightPosPs))
			&& IsCollisionPointValid(parentSpace.TransformPoint(frontLeftPosPs))
			&& IsCollisionPointValid(parentSpace.TransformPoint(frontRightPosPs)))
		{
			m_planeFilterValid = false;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::GetProbePointsPs(Point& leftPosPsOut, Point& rightPosPsOut) const
{
	ANIM_ASSERT(!m_isQuadruped);

	leftPosPsOut = m_setup[kBackLeftLeg].m_posPs;
	rightPosPsOut = m_setup[kBackRightLeg].m_posPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::SetProbeUserData(const uintptr_t* pUserData, U32F userDataCount)
{
	ANIM_ASSERT(userDataCount <= kQuadLegCount);

	for (int i = 0; i < Min(userDataCount, U32F(kQuadLegCount)); ++i)
	{
		m_setup[i].m_userData = pUserData[i];
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::GetProbePointsPs(Point& backLeftPosPsOut,
									Point& backRightPosPsOut,
									Point& frontLeftPosPsOut,
									Point& frontRightPosPsOut) const
{
	ANIM_ASSERT(m_isQuadruped);

	backLeftPosPsOut   = m_setup[kBackLeftLeg].m_posPs;
	backRightPosPsOut  = m_setup[kBackRightLeg].m_posPs;
	frontLeftPosPsOut  = m_setup[kFrontLeftLeg].m_posPs;
	frontRightPosPsOut = m_setup[kFrontRightLeg].m_posPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const LegRaycaster::Results& LegRaycaster::GetProbeResults(U32 index, float meshCollisionThreshold /* = -1.0f */) const
{
	ASSERT_LEG_INDEX_VALID(index);

	if (ResultsValid(m_mrcResults[index]))
	{
		if ((meshCollisionThreshold > 0.0f) && ResultsValid(m_colResults[index]))
		{
			const float hitDelta = Dist(m_mrcResults[index].m_point.GetTranslation(),
										m_colResults[index].m_point.GetTranslation());
			
			if (hitDelta > meshCollisionThreshold)
			{
				return m_colResults[index];
			}
		}

		return m_mrcResults[index];
	}

	if (ResultsValid(m_colResults[index]))
	{
		return m_colResults[index];
	}

	return kInvalidResults;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::ClearResults()
{
	if (m_pCollisionProbesJobCounter)
	{
		ndjob::WaitForCounterAndFree(m_pCollisionProbesJobCounter);
		m_pCollisionProbesJobCounter = nullptr;
	}

	memset(m_colResults, 0, sizeof(m_colResults));
	memset(m_mrcResults, 0, sizeof(m_mrcResults));
	for (int i = 0; i < kMaxNumRays; ++i)
	{
		m_colResults[i].m_point = kIdentity;
		m_mrcResults[i].m_point = kIdentity;
	}

	m_planeFilterValid = false;

	m_collisionProbeKickTime = TimeFrameNegInfinity();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::UseMeshRayCasts(bool useMrc, U32 ignorePid)
{
	m_meshRayIgnorePid = ignorePid;

	if (useMrc == m_useMeshRaycasts)
	{
		if (!useMrc)
		{
			for (int iRay = 0; iRay < kMaxNumRays; ++iRay)
			{
				m_mrcResults[iRay].m_valid = false;
			}
		}
		return;
	}

	m_useMeshRaycasts = useMrc;

	if (useMrc)
	{
		m_mrcMinValidFrame = GetCurrentFrameNumber();
	}
	else
	{
		//for (int iRay = 0; iRay < kMaxNumRays; ++iRay)
		//{
		//	m_meshSphereJob[iRay].InvalidateResults();
		//}

		m_mrcMinValidFrame = -1;
	}

	if (!useMrc)
	{
		memset(m_mrcResults, 0, sizeof(m_mrcResults));

		for (int iRay = 0; iRay < kMaxNumRays; ++iRay)
		{
			m_mrcResults[iRay].m_point = BoundFrame();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT_CLASS_DEFINE(LegRaycaster, CollisionProbeJob)
{
	LegRayCollisionData* const pData  = reinterpret_cast<LegRayCollisionData*>(jobParam);
	LegRaycaster* const pLegRaycaster = pData->m_pLegRaycaster;

	{	// Leg probes
		for (int iRay = 0; iRay < kMaxNumRays; ++iRay)
		{
			pLegRaycaster->DoCollisionProbeInternal(pData->m_legProbes[iRay],
													iRay,
													pData->m_probeRadius,
													pData->m_debugIk);
		}
	}

	if (pLegRaycaster->m_predictOnStairs.m_probeValid)
	{	
		// Predict on stairs probe
		const TimeFrame curTime = GetClock()->GetCurTime();

		LegRaycaster::PredictOnStairs::Result result = LegRaycaster::PredictOnStairs::Result::kInvalid;
		Point hitWs = kZero;
		Vector normalWs = kUnitYAxis;

		ShapeCastContact contacts[ICollCastJob::kMaxProbeHits];
		const U32F numHits = SphereCastJob::Probe(pData->m_predictOnStairsProbe.m_start,
												  pData->m_predictOnStairsProbe.m_end,
												  kPredictProbeRadius,
												  ICollCastJob::kMaxProbeHits,
												  contacts,
												  CollideFilter(Collide::kLayerMaskGeneral,
																Pat(1 << Pat::kPassThroughShift | 1ULL << Pat::kSqueezeThroughShift)),
												  ICollCastJob::kCollCastAllowMultipleResults);

		if (numHits > 0)
		{
			I32F iStairsContact = -1;
			for (U32F i = 0; i < numHits; i++)
			{
				const ShapeCastContact& contact = contacts[i];
				if (contact.m_pat.GetStairs())
				{
					iStairsContact = (I32F)i;
					break;
				}
			}

			bool useStairsHit = false;
			const ShapeCastContact& firstHit = contacts[0];
			if (iStairsContact >= 0)
			{
				const ShapeCastContact& stairsHit = contacts[iStairsContact];

				const float yDiff = firstHit.m_contactPoint.Y() - stairsHit.m_contactPoint.Y();
				if (abs(yDiff) < 0.12f)	// If the first and the stairs hits are close enough
				{	// Care about stairs hit
					result = LegRaycaster::PredictOnStairs::Result::kHitStairs;

					hitWs = stairsHit.m_contactPoint;
					normalWs = Vector(stairsHit.m_normal);

					useStairsHit = true;
				}
			}

			if (!useStairsHit)
			{	// Care about the first (hopefully) ground hit
				result = LegRaycaster::PredictOnStairs::Result::kHitGround;
				
				hitWs = firstHit.m_contactPoint;
				normalWs = Vector(firstHit.m_normal);
			}

			if (FALSE_IN_FINAL_BUILD(pData->m_debugPredictOnStairs))
			{
				const Color drawColor = useStairsHit ? kColorBlue : kColorCyan;
				g_prim.Draw(DebugCross(hitWs, 0.2f, drawColor, kPrimEnableHiddenLineAlpha, (useStairsHit ? "c-stairs" : "c-ground"), 0.5f), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugArrow(hitWs, hitWs + normalWs * 0.5f, drawColor, 0.5f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
			}
		}

		pLegRaycaster->m_predictOnStairs.TryCommitResult(curTime, result, normalWs);
	}

	if (pLegRaycaster->m_audioGround.m_probeValid)
	{	
		// Audio ground probe
		/*ShapeCastContact contacts[ICollCastJob::kMaxProbeHits];
		const U32F numHits = SphereCastJob::Probe(pData->m_audioGroundProbe.m_start,
												pData->m_audioGroundProbe.m_end,
												kAudioProbeRadius,
												ICollCastJob::kMaxProbeHits,
												contacts,
												CollideFilter(Collide::kLayerMaskGeneral, Pat(Pat::kPassThroughMask | Pat::kPlayerNoStandMask | Pat::kNoTraversalMask | Pat::kWaterMask)),
												ICollCastJob::kCollCastAllowMultipleResults | ICollCastJob::kCollCastSingleSidedCollision);*/

		RayCastContact contacts[ICollCastJob::kMaxProbeHits];
		const U32F numHits = RayCastJob::Probe(pData->m_audioGroundProbe.m_start,
											pData->m_audioGroundProbe.m_end,
											ICollCastJob::kMaxProbeHits,
											contacts,
											CollideFilter(Collide::kLayerMaskGeneral, Pat(Pat::kPassThroughMask | Pat::kPlayerNoStandMask | Pat::kNoTraversalMask | Pat::kWaterMask)),
											ICollCastJob::kCollCastAllowMultipleResults | ICollCastJob::kCollCastSingleSidedCollision);

		const Vector diff = pData->m_audioGroundProbe.m_end - pData->m_audioGroundProbe.m_start;

		U32F numValidHits = 0;
		U32F contactIndices[ICollCastJob::kMaxProbeHits];
		for (U32F iContact = 0; iContact < numHits; iContact++)
		{
			//const ShapeCastContact& contact = contacts[iContact];
			const RayCastContact& contact = contacts[iContact];

			if (contact.m_normal.Y() >= 0.8f)	// Contact normal vertical enough
			{
				contactIndices[numValidHits++] = iContact;
			}
			else if (FALSE_IN_FINAL_BUILD(pData->m_debugAudioGround))
			{
				const Vector normalWs = Vector(contact.m_normal);
				const Point hitWs = pData->m_audioGroundProbe.m_start + diff * contact.m_t;
				g_prim.Draw(DebugCross(hitWs,
									0.2f,
									kColorGray,
									kPrimEnableHiddenLineAlpha),
									kPrimDuration1FramePauseable);
				g_prim.Draw(DebugArrow(hitWs, hitWs + normalWs * 0.5f, kColorGray, 0.5f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
			}
		}

		pLegRaycaster->m_audioGround.m_numContacts = 0;
		if (numValidHits == 1)
		{
			pLegRaycaster->m_audioGround.m_numContacts = 1;

			const U32F index = contactIndices[0];

			pLegRaycaster->m_audioGround.m_normals[0] = contacts[index].m_normal;
			const Point hitWs = pData->m_audioGroundProbe.m_start + diff * contacts[index].m_t;
			pLegRaycaster->m_audioGround.m_contacts[0] = hitWs;
		}
		else if (numValidHits >= 2)
		{
			pLegRaycaster->m_audioGround.m_numContacts = 2;

			U32F index = contactIndices[0];

			pLegRaycaster->m_audioGround.m_normals[0] = contacts[index].m_normal;
			Point hitWs = pData->m_audioGroundProbe.m_start + diff * contacts[index].m_t;
			pLegRaycaster->m_audioGround.m_contacts[0] = hitWs;

			index = contactIndices[numValidHits - 1];

			pLegRaycaster->m_audioGround.m_normals[1] = contacts[index].m_normal;
			hitWs = pData->m_audioGroundProbe.m_start + diff * contacts[index].m_t;
			pLegRaycaster->m_audioGround.m_contacts[1] = hitWs;
		}

		if (FALSE_IN_FINAL_BUILD(pData->m_debugAudioGround))
		{
			const float platformHeight = pLegRaycaster->m_audioGround.GuessPlatformHeight();
			for (int iContact = 0; iContact < pLegRaycaster->m_audioGround.m_numContacts; iContact++)
			{
				//const ShapeCastContact& contact = pLegRaycaster->m_audioGround.m_contacts[iContact];
				//const RayCastContact& contact = pLegRaycaster->m_audioGround.m_contacts[iContact];
				
				const Vector normalWs = pLegRaycaster->m_audioGround.m_normals[iContact];
				const Point hitWs = pLegRaycaster->m_audioGround.m_contacts[iContact];
				g_prim.Draw(DebugCross(hitWs,
									0.2f, 
									kColorMagenta, 
									kPrimEnableHiddenLineAlpha, 
									iContact == 0 ? StringBuilder<16>("%3.3f", platformHeight).c_str() : "",
									0.5f), 
							kPrimDuration1FramePauseable);
				g_prim.Draw(DebugArrow(hitWs, hitWs + normalWs * 0.5f, kColorMagenta, 0.5f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void MovePointIntoCapsule(Point_arg capsuleCenter, F32 radius, F32 probeRadius, Point& point)
{
	if (radius <= 0.0f)
		return;

	probeRadius += 0.01f;
	Vector toPoint = VectorXz(point - capsuleCenter);
	if ((LengthXz(toPoint) + probeRadius) > radius)
	{
		Vector toPointNew = Normalize(toPoint) * (radius - probeRadius);
		Point newPoint = capsuleCenter + toPointNew;
		point = PointFromXzAndY(newPoint, point);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::PredictOnStairs::TryCommitResult(TimeFrame curTime, Result result, Vector normalWs)
{
	if (m_startFlipTime >= TimeFrameZero())
	{	// Flipping
		GAMEPLAY_ASSERT(IsResultDifferentEnough(m_startFlipResult, m_startFlipNormalWs, m_result, m_normalWs));

		bool needToFlip = IsResultDifferentEnough(result, normalWs, m_startFlipResult, m_startFlipNormalWs);
		if (needToFlip)
		{
			needToFlip = IsResultDifferentEnough(result, normalWs, m_result, m_normalWs);
			if (needToFlip)
			{	// Restart the flip
				m_startFlipResult = result;
				m_startFlipNormalWs = normalWs;
				m_startFlipTime = curTime;
			}
			else
			{	// Cancel the flip
				m_startFlipTime = Seconds(-1.0f);
			}
		}
		else
		{
			static const float kMinFlipTime = 0.1f;
			const float dt = (curTime - m_startFlipTime).ToSeconds();
			if (dt > kMinFlipTime)
			{	// Complete the flip
				m_result = m_startFlipResult;
				m_normalWs = m_startFlipNormalWs;

				m_startFlipTime = Seconds(-1.0f);
			}
		}
	}
	else
	{	// Not flipping
		const bool needToFlip = IsResultDifferentEnough(result, normalWs, m_result, m_normalWs);

		if (needToFlip)
		{	// Start the flip
			m_startFlipResult = result;
			m_startFlipNormalWs = normalWs;
			m_startFlipTime = curTime;
		}
		else
		{	// No need to flip
			m_startFlipTime = Seconds(-1.0f);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool LegRaycaster::PredictOnStairs::IsResultDifferentEnough(Result result0,
															const Vector_arg normalWs0,
															Result result1,
															const Vector_arg normalWs1)
{
	bool isDifferent = (result0 != result1);	// Check state

	if (!isDifferent)
	{	// Check normal
		const float d = Dot(normalWs0, normalWs1);
		if (d > 0.0f)
		{	// Same direction
			const float angleRads = Acos(d);

			const float kMaxAngleDiffDegs = 5.0f;
			isDifferent = RadiansToDegrees(angleRads) > kMaxAngleDiffDegs;
		}
		else
		{	// Opposite direction
			isDifferent = true;
		}
	}

	return isDifferent;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::KickCollisionProbe()
{
	//ASSERTF(m_pCollisionProbesJobCounter == nullptr, ("%s", pSelf ? pSelf->GetName() : "<null>"));
	if (m_pCollisionProbesJobCounter)
	{
		//MsgOut("!! Duplicate Kick '%s' !!\n", pSelf ? pSelf->GetName() : "<null>");
		// Not sure how this can happen but it can
		return;
	}

	if (m_raycastMode == kModeInvalid)
	{
		return;
	}

	const Character* const pSelf = m_hSelf.ToProcess();

	const bool drawAny = FALSE_IN_FINAL_BUILD(g_debugLegRaycasterCollKicks && 
											DebugSelection::Get().IsProcessOrNoneSelected(m_hSelf));
	const bool drawIk = FALSE_IN_FINAL_BUILD(drawAny && g_debugLegRaycasterCollKicksIk);

	const TimeFrame curTime = GetClock()->GetCurTime();
	m_collisionProbeKickTime = curTime;

	LegRayCastInfo rayCastInfo;
	pSelf->GetLegRayCastInfo(m_raycastMode, rayCastInfo);

	LegRayCollisionData* const pData = NDI_NEW(kAllocSingleFrame) LegRayCollisionData;
	for (U32F ii = 0; ii < kMaxNumRays; ii++)
	{
		const Setup& setup = m_setup[ii];

		if (setup.m_disabled)
		{
			continue;
		}

		LegRayCollisionProbe& legProbe = pData->m_legProbes[ii];

		Point setupPosWs = pSelf->GetParentSpace().TransformPoint(setup.m_posPs);
		MovePointIntoCapsule(pSelf->GetTranslation(), rayCastInfo.m_capsuleRadius, rayCastInfo.m_probeRadius, setupPosWs);

		float extraDist = 0.0f;
		if (rayCastInfo.m_projectToAlign)
		{
			extraDist = Max(0.0f, (float)(setupPosWs.Y() - pSelf->GetTranslation().Y()));
		}

		legProbe.m_start = setupPosWs + (rayCastInfo.m_dir * rayCastInfo.m_probeStartAdjust);
		if (rayCastInfo.m_useLengthForCollisionCast)
		{
			legProbe.m_end = legProbe.m_start + rayCastInfo.m_dir * rayCastInfo.m_length;
		}
		else
		{
			legProbe.m_end = legProbe.m_start + rayCastInfo.m_dir * (1.0 + extraDist) * kLegRayCastDistance;
		}

		pData->m_probeRadius = rayCastInfo.m_probeRadius;

		pData->m_debugIk = drawIk;
		if (FALSE_IN_FINAL_BUILD(drawIk))
		{
			static const float kCrossRadius = 0.05f;
			g_prim.Draw(DebugCross(legProbe.m_start, kCrossRadius, 1.0f, kDebugColors[ii], kPrimEnableHiddenLineAlpha, "start"), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCross(legProbe.m_end, kCrossRadius, 1.0f, kDebugColors[ii], kPrimEnableHiddenLineAlpha, kColDebugStrings[ii]), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCross(setupPosWs, kCrossRadius, 1.0f, kDebugColors[ii], kPrimEnableHiddenLineAlpha, "setup"), kPrimDuration1FramePauseable);

			Color clrA = kDebugColors[ii];
			clrA.SetA(clrA.A() * 0.33f);
			g_prim.Draw(DebugCapsule(legProbe.m_start, legProbe.m_end, pData->m_probeRadius, clrA, 1.0f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
		}
	}

	{
		const bool drawPredictOnStairs = FALSE_IN_FINAL_BUILD(drawAny && g_debugLegRaycasterCollKicksPredictOnStairs);
		pData->m_debugPredictOnStairs = drawPredictOnStairs;

		bool predictOnStairsKicked = false;
		// Dont do this for MP.
		if (!g_ndConfig.m_pNetInfo->IsNetActive() && pSelf->HasPredictOnStairs())
		{
			// Predict on stairs state, predict into the future along the anim trajectory
			const NdSubsystemAnimController* const pAnimCtrller = pSelf->GetActiveSubsystemController(SID("CharacterMotionMatchLocomotion"));
			if (pAnimCtrller)
			{
				const CharacterMotionMatchLocomotion* const pLocomotionCtrller = static_cast<const CharacterMotionMatchLocomotion*>(pAnimCtrller);

				const DC::MotionMatchingSettings* pSettings = pLocomotionCtrller->GetSettings();
				float futureTimeSecs = pSettings ? pSettings->m_predictStairsEnterTimeSec : 0.0f;
				
				if (pSelf->OnStairs() && pSettings)
				{
					const Vector charGroundNormalPs = pSelf->GetGroundNormalPs();
					const Vector stairLeftPs		= Cross(charGroundNormalPs, kUnitYAxis);
					const Vector stairUpPs	 = Cross(stairLeftPs, charGroundNormalPs);
					const Vector stairUpXzPs = AsUnitVectorXz(stairUpPs, kUnitYAxis);
					const float stairUpDot	 = Dot(GetLocalZ(pSelf->GetRotationPs()), stairUpXzPs);

					if (stairUpDot >= 0.0f)
						futureTimeSecs = pSettings->m_predictStairsUpExitTimeSec;
					else
						futureTimeSecs = pSettings->m_predictStairsDownExitTimeSec;
				}

				Point setupPosWs;
				if (pLocomotionCtrller->GetFuturePosWs(futureTimeSecs, true, setupPosWs))
				{
					// Probe is ready, prepare and kick
					predictOnStairsKicked = true;
					m_predictOnStairs.m_probeValid = true;

					static const float kStairProbeTopOffsetY = 1.25f;
					static const float kStairProbeBottomOffsetY = 1.25f;

					LegRayCollisionProbe& stairsProbe = pData->m_predictOnStairsProbe;
					stairsProbe.m_start = setupPosWs + Vector(kUnitYAxis) * kStairProbeTopOffsetY;
					stairsProbe.m_end = setupPosWs - Vector(kUnitYAxis) * kStairProbeBottomOffsetY;

					if (FALSE_IN_FINAL_BUILD(drawPredictOnStairs))
					{
						static const float kCrossRadius = 0.05f;
						g_prim.Draw(DebugCross(stairsProbe.m_start, kCrossRadius, 1.0f, kColorPink, kPrimEnableHiddenLineAlpha, "start"), kPrimDuration1FramePauseable);
						g_prim.Draw(DebugCross(stairsProbe.m_end, kCrossRadius, 1.0f, kColorPink, kPrimEnableHiddenLineAlpha, "stairs"), kPrimDuration1FramePauseable);
						g_prim.Draw(DebugCross(setupPosWs, kCrossRadius, 1.0f, kColorPink, kPrimEnableHiddenLineAlpha, "setup"), kPrimDuration1FramePauseable);

						g_prim.Draw(DebugCapsule(stairsProbe.m_start, stairsProbe.m_end, kOnStairsCastRadius, kColorPink, 1.0f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
					}
				}
			}
		}

		if (!predictOnStairsKicked)
		{	
			// Probe didnt kick, reset
			m_predictOnStairs.Reset();
		}
	}

	{
		const bool drawAudioGround = FALSE_IN_FINAL_BUILD(drawAny && g_debugLegRaycasterCollKicksAudioGround);
		pData->m_debugAudioGround = drawAudioGround;

		// Dont do this for MP.
		if (!g_ndConfig.m_pNetInfo->IsNetActive() && pSelf->HasDetectAudioGround())
		{
			m_audioGround.m_probeValid = true;

			static const float kProbeTopOffsetY = 0.45f;
			static const float kProbeBottomOffsetY = 3.45f;
			static const float kProbeRadius = 0.01f;

			const Point setupPosWs = pSelf->GetTranslation();

			LegRayCollisionProbe& probe = pData->m_audioGroundProbe;
			probe.m_start = setupPosWs + Vector(kUnitYAxis) * kProbeTopOffsetY;
			probe.m_end = setupPosWs - Vector(kUnitYAxis) * kProbeBottomOffsetY;

			if (FALSE_IN_FINAL_BUILD(drawAudioGround))
			{
				static const float kCrossRadius = 0.05f;
				g_prim.Draw(DebugCross(probe.m_start, kCrossRadius, 1.0f, kColorBlue, kPrimEnableHiddenLineAlpha, "audio"), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugCross(probe.m_end, kCrossRadius, 1.0f, kColorBlue, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugCross(setupPosWs, kCrossRadius, 1.0f, kColorBlue, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);

				//g_prim.Draw(DebugCapsule(probe.m_start, probe.m_end, kProbeRadius, kColorBlue, 1.0f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugLine(probe.m_start, probe.m_end, kColorBlue, 1.0f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
			}
		}

		if (!m_audioGround.m_probeValid)
		{
			// Probe didnt kick, reset
			m_audioGround.Reset();
		}
	}

	pData->m_pLegRaycaster = this;

	ndjob::JobDecl jobDecl(LegRaycaster::CollisionProbeJob, (uintptr_t)pData);
	jobDecl.m_dependentCounter = g_havok.m_pProbeInfoLockCounter; // in case someone has the probeInfo write lock we will wait for the lock to go free.
	jobDecl.m_depCounterWaitCondition = ndjob::kWaitLessThanOrEqual;
	// We really don't want to run if the g_havok.m_pWorldWriteCounter is not zero at the moment -> kRecheckDependencyOnResume
	jobDecl.m_flags = ndjob::kRecheckDependencyOnResume;

	// @@JS: Trying to track down rare PKG only crash
	ndjob::RunJobs(&jobDecl,
				   1,
				   &m_pCollisionProbesJobCounter,
				   FILE_LINE_FUNC_FINAL,
				   ndjob::Priority::kGameFrameBelowNormal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::CollectCollisionProbeResults()
{
	PROFILE(AI, CollectCollisionProbeResults);

	if (!m_pCollisionProbesJobCounter)
		return;

	ndjob::WaitForCounterAndFree(m_pCollisionProbesJobCounter);
	m_pCollisionProbesJobCounter = nullptr;


	for (int iLeg = 0; iLeg < kMaxNumRays; ++iLeg)
	{
		if (m_setup[iLeg].m_disabled)
			continue;

		// Mesh raycasts don't get pat info so we rely on the pat info from the regular collision casts for that.
		m_mrcResults[iLeg].m_pat = m_colResults[iLeg].m_pat;

		m_mrcResults[iLeg].m_hasSlatPoint = m_colResults[iLeg].m_hasSlatPoint;
		m_mrcResults[iLeg].m_slatPoint = m_colResults[iLeg].m_slatPoint;

		if (const Character* pChar = m_hSelf.ToProcess())
		{
			if (m_colResults[iLeg].m_hitGround && !m_colResults[iLeg].m_hitWater)
				m_colResults[iLeg].m_point.SetBinding(Binding(pChar->GetBoundRigidBody()));

			if (pChar->GetClock()->GetDeltaTimeFrame() > Seconds(0.0f))
			{
				const Vector movement = kZero;//pChar->GetBoundFrame().GetMovementThisFrame().Pos() - kOrigin;

				if (m_mrcResults[iLeg].m_valid)
					m_mrcResults[iLeg].m_point.SetTranslation(m_mrcResults[iLeg].m_point.GetTranslation() + movement);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void OnRayCastResultsReady(const MeshProbe::CallbackObject* const pObject,
								  MeshRayCastJob::CallbackContext* const pInContext,
								  const MeshProbe::Probe& probeReq)
{
	LegRaycaster::MeshRayCastContext* const pContext = (LegRaycaster::MeshRayCastContext*)pInContext;

	Character* const pChar = pContext->m_hCharacter.ToMutableProcess();
	if (!pChar)
	{
		return;
	}

	LegRaycaster* const pRaycaster = pChar->GetLegRaycaster();
	if (pRaycaster)
	{
		pRaycaster->MeshRaycastCallback(pObject, probeReq, pContext);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::KickMeshRaycasts(Character* const pChar, bool disableMeshIk)
{
	PROFILE(AI, LegRaycaster_KickMrc);

	// Clear previous results
	if (false) // (!pChar->GetClock()->IsPaused())
	{
		memset(m_mrcResults, 0, sizeof(m_mrcResults));
		for (int i = 0; i < kMaxNumRays; ++i)
		{
			m_mrcResults[i].m_point = BoundFrame();
		}
	}

	if (!m_useMeshRaycasts)
	{
		return;
	}

	if (m_raycastMode == kModeDefault && disableMeshIk)
	{
		return;
	}

	if (m_raycastMode == kModeInvalid)
	{
		return;
	}

	LegRayCastInfo rayCastInfo;
	pChar->GetLegRayCastInfo(m_raycastMode, rayCastInfo);

	const Vector startAdjWs = rayCastInfo.m_dir * rayCastInfo.m_probeStartAdjust;
	const Vector rayVectorWs = rayCastInfo.m_dir * rayCastInfo.m_length;

	const Character* const pSelf = m_hSelf.ToProcess();

	for (int ii = 0; ii < kMaxNumRays; ii++)
	{
		if (m_setup[ii].m_disabled)
		{
			continue;
		}

		const Point setupPosWs = pSelf->GetParentSpace().TransformPoint(m_setup[ii].m_posPs);
		Point startPosWs = setupPosWs + startAdjWs;
		MovePointIntoCapsule(pSelf->GetTranslation(), rayCastInfo.m_capsuleRadius, rayCastInfo.m_probeRadius, startPosWs);

		{
			PROFILE_AUTO(AI);

			// Cast the ray. Since we are sharing this system with decals, cast at high priority
			// so we don't get starved out during heavy fire-fights.
			const MeshRayCastJob::Priority priority = pChar->GetLegIkMeshRaycastPriority();
			const U32F flags = MeshRayCastJob::kEveryFrame;

			MeshRayCastJob::CallbackContext context;
			MeshRayCastContext* const pContext = (MeshRayCastContext*)&context;
			pContext->m_hCharacter = MutableCharacterHandle(pChar);
			ASSERT_LEG_INDEX_VALID(ii);
			pContext->m_leg = ii;
			pContext->m_time = GetClock()->GetCurTime();
			pContext->m_parentSpace = pChar->GetParentSpace();
			pContext->m_binding = pChar->GetBinding();
			pContext->m_mode = m_raycastMode;
			pContext->m_userData = m_setup[ii].m_userData;

			MeshSphereCastJob meshSphereJob;
			const Point endPosWs = startPosWs + rayVectorWs;
			meshSphereJob.SetProbeExtent(startPosWs, endPosWs);
			meshSphereJob.SetProbeRadius(rayCastInfo.m_probeRadius);

			MeshRayCastJob::HitFilter filter(MeshRayCastJob::HitFilter::kHitIk,
											pChar->GetProcessId(),
											m_meshRayIgnorePid);
			filter.m_includeRagdoll = pChar->GetLegIkOnRagdolls();
			meshSphereJob.SetHitFilter(filter);

			meshSphereJob.SetBehaviorFlags(flags);
			meshSphereJob.SetPriority(priority);
			meshSphereJob.m_pCallback = OnRayCastResultsReady;
			meshSphereJob.m_pCallbackContext = (MeshRayCastJob::CallbackContext*)&context;

			meshSphereJob.Kick(FILE_LINE_FUNC);

			const bool drawMeshKicks = FALSE_IN_FINAL_BUILD(g_debugLegRaycasterMeshKicks && 
															DebugSelection::Get().IsProcessOrNoneSelected(m_hSelf));
			if (drawMeshKicks)
			{
				static const float kCrossRadius = 0.05f;
				g_prim.Draw(DebugCross(startPosWs, kCrossRadius, 1.0f, kDebugColors[ii], PrimAttrib(kPrimEnableHiddenLineAlpha), "start"));
				g_prim.Draw(DebugCross(endPosWs, kCrossRadius, 1.0f, kDebugColors[ii], PrimAttrib(kPrimEnableHiddenLineAlpha), kMeshDebugStrings[ii]));
				g_prim.Draw(DebugCross(setupPosWs, kCrossRadius, 1.0f, kDebugColors[ii], PrimAttrib(kPrimEnableHiddenLineAlpha), "setup"));

				g_prim.Draw(DebugCapsule(startPosWs, endPosWs, rayCastInfo.m_probeRadius, kDebugColors[ii], 1.0f, PrimAttrib(kPrimEnableHiddenLineAlpha)));
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool LegRaycaster::IsFootprintSurface()
{
	// intentionally leg count not raycount. we don't need to predict footprints
	const int legCount = GetLegCount();

	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		if (!m_mrcResults[iLeg].m_pat.GetFootPrintSurface())
			return false;
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::DoCollisionProbeInternal(LegRayCollisionProbe& legProbe,
											int legIndex,
											float radius,
											bool debug /* = false */)
{
	ASSERT_LEG_INDEX_VALID(legIndex);

	Results& results = m_colResults[legIndex];
	results.m_valid = false;

	if (m_setup[legIndex].m_disabled)
	{
		return;
	}

	ShapeCastContact contacts[ICollCastJob::kMaxProbeHits];
	U32F numCnt = SphereCastJob::Probe(legProbe.m_start,
									   legProbe.m_end,
									   radius,
									   ICollCastJob::kMaxProbeHits,
									   contacts,
									   CollideFilter(Collide::kLayerMaskFgBig | Collide::kLayerMaskBackground
														 | Collide::kLayerMaskWater,
													 Pat(1 << Pat::kPassThroughShift | 1ULL << Pat::kSqueezeThroughShift)),
									   ICollCastJob::kCollCastAllowMultipleResults);

	results.m_valid	   = true;
	results.m_hitWater = false;
	results.m_hasSlatPoint = false;

	m_oneRaycastDone = true;

	I32F iBgContact	   = -1;
	I32F iFgContact	   = -1;
	I32F iWaterContact = -1;

	for (U32F ii = 0; ii < numCnt; ii++)
	{
		const ShapeCastContact& cnt = contacts[ii];
		if (!IsCollisionPointValid(cnt.m_contactPoint))
		{
			continue;
		}

		if (cnt.m_pat.GetWater())
		{
			iWaterContact = Min(I32F(ii), iWaterContact);
		}
		else if (const RigidBodyBase* pBody = cnt.m_hRigidBody.ToBody())
		{
			if (pBody->GetOwnerHandle().HandleValid())
			{
				if (iFgContact < 0)
				{
					iFgContact = ii;
				}
			}
			else
			{
				// Hit Bg, we're done
				iBgContact = ii;
				break;
			}
		}
	}

	const bool hitWaterBeforeFg = (iWaterContact >= 0) && (iFgContact >= 0) && (iWaterContact < iFgContact);

	I32F iContact = -1;
	if (iFgContact >= 0 && !hitWaterBeforeFg)
	{
		// Fg contact is only valid if there was not water or no bg
		// It there was water we will use the Bg under the water
		iContact = iFgContact;
	}
	else
	{
		iContact = iBgContact;
	}

	if ((iContact >= 0) && contacts[iContact].m_pat.GetStairs()
		&& !FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableStairsDetailProbes))
	{
		CollideFilter filter;
		filter.SetPatInclude(Pat(1ull << Pat::kStairsDetailedShift));

		ShapeCastContact detailContact;
		const U32F cnt = SphereCastJob::Probe(legProbe.m_start,
											  legProbe.m_end,
											  radius,
											  1,
											  &detailContact,
											  filter);

		if (cnt == 1)
		{
			detailContact.m_pat.m_bits |= contacts[iContact].m_pat.m_bits;
			contacts[iContact] = detailContact;
		}
	}

	if (iContact >= 0)
	{
		Point pointWs;
		DistPointLine(contacts[iContact].m_contactPoint, legProbe.m_start, legProbe.m_end, &pointWs);

		const Vector normalWs	= Vector(contacts[iContact].m_normal);
		const Quat rotWs		= Normalize(QuatFromLookAt(normalWs, kUnitXAxis));

		GAMEPLAY_ASSERT(IsReasonable(pointWs));
		GAMEPLAY_ASSERT(IsReasonable(normalWs));
		GAMEPLAY_ASSERT(IsReasonable(rotWs));

		RigidBody* pBody = contacts[iContact].m_hRigidBody.ToBody();

		results.m_point		  = BoundFrame(pointWs, rotWs);
		results.m_pat		  = contacts[iContact].m_pat;
		results.m_hRayHitBody = pBody && pBody->GetOwnerHandle().HandleValid() ? pBody : nullptr;
		results.m_hitProcessId = 0;
		results.m_hitGround	   = true;

		if (FALSE_IN_FINAL_BUILD(debug))
		{
			//g_prim.Draw(DebugLine(legProbe.m_start, legProbe.m_end, kColorMagenta, kPrimEnableHiddenLineAlpha));
			//g_prim.Draw(DebugCross(probeEndWs, 0.05f, kColorCyan, kPrimEnableHiddenLineAlpha));
			//g_prim.Draw(DebugCross(contacts[iContact].m_contactPoint, 0.1f, kColorRed, kPrimEnableHiddenLineAlpha));
			g_prim.Draw(DebugCoordAxes(results.m_point.GetLocatorWs(), 0.3f, kPrimEnableHiddenLineAlpha));
		}
	}
	else
	{
		results.m_point.SetInvalid();

		results.m_pat		  = Pat(0);
		results.m_hRayHitBody = nullptr;
		results.m_hitProcessId = 0;
		results.m_hitGround	   = false;
	}

	results.m_userData = m_setup[legIndex].m_userData;
	results.m_time = m_collisionProbeKickTime;
	results.m_mode = m_raycastMode;

	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_slatLegIkTest))
	{
		CollideFilter filter = CollideFilter(Collide::kLayerMaskFgBig | Collide::kLayerMaskBackground,
											 Pat(1ULL << Pat::kPassThroughShift | 1ULL << Pat::kShootThroughShift | 1ULL << Pat::kSqueezeThroughShift));

		const I32 numSlatHits = SphereCastJob::Probe(legProbe.m_start, legProbe.m_end, kSlatCastRadius, 1, contacts, filter);
		if (numSlatHits)
		{
			results.m_hasSlatPoint = true;// contacts[0].m_pat.GetSlat();
			results.m_slatPoint = contacts[0].m_contactPoint;
			Vector xzNorm = Vector(contacts[0].m_normal);
			xzNorm.SetY(0.0f);

			Scalar normLen;
			xzNorm = Normalize(xzNorm, normLen);
			if (normLen > 0.0f)
			{
				results.m_slatPoint += -xzNorm * 0.05f;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::MeshRaycastCallback(MeshProbe::CallbackObject const* pObject,
									   const MeshProbe::Probe& probeReq,
									   const MeshRayCastContext* pContext)
{
	// this can happen if we switch the number of rays we cast between kicking and getting this callback (so far encountered when getting off the horse)
	const int legIndex = pContext->m_leg;
	GAMEPLAY_ASSERT(legIndex < kMaxNumRays);

	if (m_setup[legIndex].m_disabled)
	{
		m_mrcResults[legIndex].m_valid = false;
		return;
	}

	ASSERT_LEG_INDEX_VALID(legIndex);

	Results& res = m_mrcResults[legIndex];

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_disableLegMrcResults))
	{
		res.m_valid = false;
		return;
	}

	I64 castFrame = probeReq.m_frameNumber;

	const bool castValid = (m_mrcMinValidFrame >= 0) && (castFrame >= m_mrcMinValidFrame);

	if (pObject != nullptr && castValid)
	{
		const Character* pChar = m_hSelf.ToProcess();

		const Point startPosWs = pChar->GetParentSpace().TransformPoint(m_setup[legIndex].m_posPs);

		const MeshProbe::ProbeResult& rayResult = pObject->m_probeResults[0];

		//Act like we didnt hit anything
		if (!IsCollisionPointValid(startPosWs) || !IsCollisionPointValid(rayResult.m_contactWs))
		{
			return;
		}

		m_oneRaycastDone = true;
		res.m_valid = true;
		res.m_hRayHitBody = nullptr;

		if (rayResult.m_levelIndex == -1 && rayResult.m_fgProcessId == 0)
		{
			res.m_hitGround = false;
			res.m_hitWater = false;
		}
		else
		{
			res.m_hitGround = true;
			res.m_hitWater = false;
		}

		const Point posWs = rayResult.m_contactWs;
		const Vector normWs = rayResult.m_normalWs;

		// posWs += pChar->GetBoundFrame().GetMovementThisFrame().Pos() - kOrigin;
		const Vector up = SafeNormalize(Cross(normWs, kUnitXAxis), kUnitZAxis);
		const Locator locWs = Locator(posWs, Normalize(QuatFromLookAt(normWs, up)));

		res.m_hitProcessId = rayResult.m_fgProcessId;
		res.m_point = BoundFrame(locWs, pContext->m_binding);
		res.m_time = pContext->m_time;
		res.m_userData = pContext->m_userData;
		res.m_meshRaycastResult = true;

		res.m_mode = pContext->m_mode;
	}
	else
	{
		res.m_hitProcessId = 0;
		res.m_valid = false;
		res.m_mode = pContext->m_mode;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool LegRaycaster::IsCollisionPointValid(Point_arg p) const
{
	if (m_planeFilterValid)
	{
		if (m_planeFilterLocator.GetLocator().UntransformPoint(p).Z() < 0.0f)
		{
			return false;
		}
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::SetPlaneFilter(const BoundFrame& planeBf)
{
	m_planeFilterLocator = planeBf;
	m_planeFilterValid = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::InvalidateMeshRayResult(U32 index)
{
	ASSERT_LEG_INDEX_VALID(index);
	m_mrcResults[index] = kInvalidResults;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegRaycaster::InvalidateCollisionResult(U32 index)
{
	ASSERT_LEG_INDEX_VALID(index);
	m_colResults[index] = kInvalidResults;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Clock* LegRaycaster::GetClock()
{
	return EngineComponents::GetNdFrameState()->GetClock(kGameClock);
}

/// --------------------------------------------------------------------------------------------------------------- ///
LegRayCastInfo::LegRayCastInfo()
{
	m_dir	 = -Vector(kUnitYAxis);
	m_length = kLegRayCastDistance;
	m_projectToAlign   = false;
	m_probeStartAdjust = -0.35f;
	m_useLengthForCollisionCast = false;
	m_probeRadius = 0.075f;
}
