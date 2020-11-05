/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/base/nd-ai-util.h"

#include "corelib/math/intersection.h"
#include "corelib/util/random.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/nd-anim-align-util.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/anim/motion-matching/motion-matching.h"
#include "gamelib/gameplay/ai/agent/nav-character-adapter.inl"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/agent/simple-nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/ai/controller/nd-animation-controllers.h"
#include "gamelib/gameplay/ai/controller/nd-cinematic-controller.h"
#include "gamelib/gameplay/ai/controller/nd-hit-controller.h"
#include "gamelib/gameplay/ai/controller/nd-traversal-controller.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-probe.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/path-waypoints.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/scriptx/h/anim-nav-character-info.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AiLibGlobalInfo* AI::GetAiLibGlobalInfo()
{
	const DC::AiLibGlobalInfo* pInfo = ScriptManager::LookupInNamespace<DC::AiLibGlobalInfo>(SID("*ai-global-info*"),
																							 SID("ai"));
	return pInfo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AI::AdjustMoveToNavMeshPs(const NavMesh* pNavMesh,
							   const NavPoly* pStartPoly,
							   Point_arg startPosPs,
							   Point_arg desiredPosPs,
							   const Segment& probePs,
							   float moveRadius,
							   float maxMoveDist,
							   const Nav::StaticBlockageMask obeyedStaticBlockerMask,
							   const NavMesh::NpcStature minNpcStature,
							   const NavBlockerBits& obeyedBlockers,
							   Point* pReachedPosPsOut,
							   bool debugDraw /* = false */)
{
	PROFILE_AUTO(AI);
	PROFILE_ACCUM(AI_AdjustMoveToNavMeshPs);

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	if (pReachedPosPsOut)
		*pReachedPosPsOut = desiredPosPs;

	if (!pNavMesh)
	{
		return false;
	}

	bool wasAdjusted = false;

	const Point startPosLs = pNavMesh->ParentToLocal(startPosPs);
	Segment capsuleSegment = probePs;

	if (!pStartPoly || !pStartPoly->PolyContainsPointLs(startPosLs))
	{
		pStartPoly = pNavMesh->FindContainingPolyPs(startPosPs);
	}

	const NavMesh* pCurMesh = pNavMesh;

	if (pStartPoly && pStartPoly->IsLink())
	{
		const NavMesh* pLinkedMesh = nullptr;
		if (const NavPoly* pLinkedPoly = pStartPoly->GetLinkedPoly(&pLinkedMesh))
		{
			pStartPoly = pLinkedPoly;
			pCurMesh = pLinkedMesh;
		}
	}

	const bool dynamicProbe = !obeyedBlockers.AreAllBitsClear();
	const NavPolyEx* pStartPolyEx = (pStartPoly && dynamicProbe) ? pStartPoly->FindContainingPolyExPs(startPosPs)
																 : nullptr;

	const float clearanceRadius = moveRadius + NavMeshClearanceProbe::kNudgeEpsilon;
	const NavPoly* pCurPoly = pStartPoly;
	const NavPolyEx* pCurPolyEx = pStartPolyEx;

	bool startIsOk = false;

	const bool startBlocked = (pStartPolyEx ? pStartPolyEx->IsBlockedBy(obeyedBlockers) : false)
							  || (pStartPoly ? pStartPoly->IsBlocked(obeyedStaticBlockerMask) : false);

	if (!startBlocked)
	{
		NavMesh::ClearanceParams cParams;
		cParams.m_point					= startPosPs;
		cParams.m_obeyedBlockers		= obeyedBlockers;
		cParams.m_obeyedStaticBlockers	= obeyedStaticBlockerMask;
		cParams.m_dynamicProbe			= dynamicProbe;
		cParams.m_radius				= clearanceRadius;
		cParams.m_pStartPoly			= pStartPoly;
		cParams.m_capsuleSegment		= capsuleSegment;
		cParams.m_crossLinks			= true;
		cParams.m_minNpcStature			= minNpcStature;

		NavMesh::ProbeResult clearanceResult = pCurMesh->CheckClearancePs(&cParams);
		startIsOk = clearanceResult == NavMesh::ProbeResult::kReachedGoal;
	}

	Point adjustStartPosPs = startPosPs;
	if (!startIsOk)
	{
		// start position is not valid
		// find nearest point in nav space that's valid and start from there
		NavMesh::FindPointParams fpParams;
		fpParams.m_point				= startPosPs;
		fpParams.m_obeyedBlockers		= obeyedBlockers;
		fpParams.m_obeyedStaticBlockers = obeyedStaticBlockerMask;
		fpParams.m_dynamicProbe			= dynamicProbe;
		fpParams.m_searchRadius			= pCurMesh->NavMeshForcesSwim() ? 8.0f : 2.0f;
		fpParams.m_depenRadius			= clearanceRadius;
		fpParams.m_crossLinks			= true;
		fpParams.m_minNpcStature		= minNpcStature;
		fpParams.m_capsuleSegment		= capsuleSegment;
		fpParams.m_pStartPoly			= pStartPoly;
		fpParams.m_pStartPolyEx			= pStartPolyEx;

		if (debugDraw)
		{
			fpParams.m_debugDraw = true; // for breakpoints
		}

		pCurMesh->FindNearestPointPs(&fpParams);

		if (!fpParams.m_pPoly)
		{
			fpParams.m_dynamicProbe = false;
			pCurMesh->FindNearestPointPs(&fpParams);
		}

		if (fpParams.m_pPoly)
		{
			pCurMesh = fpParams.m_pPoly->GetNavMesh();
			pCurPoly = fpParams.m_pPoly;
			pCurPolyEx = fpParams.m_pPolyEx;

			Vector move = VectorXz(fpParams.m_nearestPoint - adjustStartPosPs);
			capsuleSegment.a += move;
			capsuleSegment.b += move;
			adjustStartPosPs += move;

			startIsOk = true;
		}
	}

	if (FALSE_IN_FINAL_BUILD(!startIsOk && debugDraw))
	{
		const Point startPosWs = pCurMesh->ParentToWorld(startPosPs);
		g_prim.Draw(DebugCross(startPosWs, clearanceRadius, kColorRed));
		g_prim.Draw(DebugString(startPosWs, "start failed", kColorRed, 0.5f));
		return false;
	}

	Point adjustedPosPs = desiredPosPs;
	Point desAdjustedPosPs = desiredPosPs;

	if (startIsOk)
	{
		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugCross(pCurMesh->ParentToWorld(adjustStartPosPs), clearanceRadius, kColorGreenTrans));
			g_prim.Draw(DebugArrow(pCurMesh->ParentToWorld(adjustStartPosPs), AsUnitVectorXz(desiredPosPs - adjustStartPosPs, kZero), kColorCyan));
		}

		if (DistXzSqr(adjustStartPosPs, desiredPosPs) > NDI_FLT_EPSILON)
		{
			PROFILE(AI, ProbeStartOk);

			NavMesh::ProbeParams params;
			params.m_start					= adjustStartPosPs;
			params.m_move					= VectorXz(desiredPosPs - adjustStartPosPs);
			params.m_capsuleSegment			= capsuleSegment;
			params.m_obeyedBlockers			= obeyedBlockers;
			params.m_obeyedStaticBlockers	= obeyedStaticBlockerMask;
			params.m_probeRadius			= moveRadius;
			params.m_pStartPoly				= pCurPoly;
			params.m_pStartPolyEx			= pCurPolyEx;
			params.m_dynamicProbe			= dynamicProbe;
			params.m_crossLinks				= true;
			params.m_minNpcStature			= minNpcStature;

			I32F iBounceCount = 2;

			while (true)
			{
				const NavMesh::ProbeResult probeRes = pCurMesh->ProbePs(&params);

				bool stop = false;

				switch (probeRes)
				{
				case NavMesh::ProbeResult::kReachedGoal:
					desAdjustedPosPs = params.m_start + params.m_move;
					stop = true;
					break;

				case NavMesh::ProbeResult::kErrorStartedOffMesh:
				case NavMesh::ProbeResult::kErrorStartNotPassable:
					desAdjustedPosPs = params.m_start;
					stop = true;
					break;

				case NavMesh::ProbeResult::kHitEdge:
					break;
				}

				if (params.m_pReachedMesh)
				{
					pCurMesh = params.m_pReachedMesh;
				}

				if (stop)
				{
					break;
				}

				const Vector clampedMoveVec = params.m_endPoint - params.m_start;
				params.m_capsuleSegment.a += clampedMoveVec;
				params.m_capsuleSegment.b += clampedMoveVec;

				desAdjustedPosPs = params.m_endPoint;

				--iBounceCount;

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					g_prim.Draw(DebugArrow(params.m_start,
										   clampedMoveVec,
										   kColorWhite,
										   0.15f,
										   PrimAttrib(kPrimEnableHiddenLineAlpha)));
					g_prim.Draw(DebugCross(params.m_impactPoint, 0.05f, kColorRed, kPrimEnableHiddenLineAlpha));
					//MsgConPauseable("Clamped Move Vec Length: %.3f\n", (float)Length(clampedMoveVec));
				}

				if (iBounceCount == 0)
					break;

				// allow the move to slide along the boundary if possible
				// new start point at point of collision
				params.m_start = desAdjustedPosPs;

				// remaining part of move
				const Vector remMove = desiredPosPs - desAdjustedPosPs;

				// edge normal may be in wrong direction, flip it if so
				Vector edgeNormal = params.m_edgeNormal;
				if (Dot(remMove, edgeNormal) > 0.0f)
					edgeNormal = -edgeNormal;

				// remove component of move that is perpendicular to the edge that was hit
				const Vector remComp = (edgeNormal * (Dot(edgeNormal, remMove) + NavMeshClearanceProbe::kNudgeEpsilon));
				const Vector move = remMove - remComp;

				params.m_move = move;

				// redo the probe
				pCurMesh = params.m_pReachedPoly->GetNavMesh();
				pCurPoly = params.m_pReachedPoly;
				pCurPolyEx = params.m_pReachedPolyEx;

				params.m_pStartPoly = pCurPoly;
				params.m_pStartPolyEx = pCurPolyEx;
			}

			const Vector adjustDeltaPs = LimitVectorLength(desAdjustedPosPs - desiredPosPs, 0.0f, maxMoveDist);
			adjustedPosPs = PointFromXzAndY(desiredPosPs + adjustDeltaPs, desiredPosPs);

			// To avoid jittering when using a capsule ensure that we are still depenetrated from the nav mesh after making the move
			if (DistXzSqr(probePs.a, probePs.b) > NDI_FLT_EPSILON)
			{
				const Vector probeOffset = (desiredPosPs - startPosPs) + adjustDeltaPs;
				const Segment adjustedProbe(probePs.a + probeOffset, probePs.b + probeOffset);

				NavMesh::ClearanceParams cParams;
				cParams.m_point					= adjustedPosPs;
				cParams.m_obeyedBlockers		= obeyedBlockers;
				cParams.m_obeyedStaticBlockers	= obeyedStaticBlockerMask;
				cParams.m_dynamicProbe			= dynamicProbe;
				cParams.m_radius				= clearanceRadius;
				cParams.m_pStartPoly			= pCurPoly;
				cParams.m_pStartPolyEx			= pCurPolyEx;
				cParams.m_capsuleSegment		= adjustedProbe;
				cParams.m_crossLinks			= true;
				cParams.m_minNpcStature			= minNpcStature;

				NavMesh::ProbeResult clearanceResult = pCurMesh->CheckClearancePs(&cParams);
				if (clearanceResult != NavMesh::ProbeResult::kReachedGoal)
				{
					NavMesh::FindPointParams fpParams;
					fpParams.m_point				= adjustedPosPs;
					fpParams.m_obeyedBlockers		= obeyedBlockers;
					fpParams.m_obeyedStaticBlockers = obeyedStaticBlockerMask;
					fpParams.m_dynamicProbe			= dynamicProbe;
					fpParams.m_searchRadius			= pCurMesh->NavMeshForcesSwim() ? 8.0f : 2.0f;
					fpParams.m_depenRadius			= clearanceRadius;
					fpParams.m_crossLinks			= true;
					fpParams.m_minNpcStature		= minNpcStature;
					fpParams.m_capsuleSegment		= adjustedProbe;
					fpParams.m_pStartPoly			= pStartPoly;
					fpParams.m_pStartPolyEx			= pStartPolyEx;

					pCurMesh->FindNearestPointPs(&fpParams);

					if (fpParams.m_pPoly)
					{
						pCurMesh = fpParams.m_pPoly->GetNavMesh();
						pCurPoly = fpParams.m_pPoly;
						pCurPolyEx = fpParams.m_pPolyEx;

						Vector move = VectorXz(fpParams.m_nearestPoint - adjustedPosPs);
						adjustedPosPs += move;
					}
				}
			}
		}
	}

	const F32 adjustedAmount = DistXz(adjustedPosPs, desiredPosPs);
	if (adjustedAmount > 0.0001f)
	{
		wasAdjusted = true;
	}

	if (pReachedPosPsOut)
	{
		*pReachedPosPsOut = adjustedPosPs;
	}

	return wasAdjusted;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// TODO: this was originally written for NavMap (hence the 4 linear probes) and can be replaced with a single
// nav mesh nearest point search!
Point AI::AdjustPointToNavMesh(const NavCharacterAdapter& pNavChar,
							   const NavMesh* pNavMesh,
							   const NavPoly* pStartPoly,
							   Point_arg startingPosPs,
							   NavMesh::NpcStature minNpcStature,
							   float adjRadius,
							   float maxMoveDist)
{
	if (!pNavMesh || !pNavChar)
		return startingPosPs;

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	NavMesh::ProbeParams probeParams;
	probeParams.m_pStartPoly = pStartPoly;
	probeParams.m_start = startingPosPs;
	probeParams.m_minNpcStature = minNpcStature;

	if (const NavCharacter* pActualNavChar = pNavChar->ToNavCharacter())
	{
		probeParams.m_capsuleSegment = pActualNavChar->GetDepenetrationSegmentPs(startingPosPs, true);
	}

	Point probePointsPs[4];
	const Locator& alignPs = pNavChar.ToGameObject()->GetLocatorPs();
	const Vector leftPs = GetLocalX(alignPs.Rot());
	const Vector fwPs = GetLocalZ(alignPs.Rot());

	probePointsPs[0] = startingPosPs + (fwPs * adjRadius);
	probePointsPs[1] = startingPosPs - (leftPs * adjRadius);
	probePointsPs[2] = startingPosPs - (fwPs * adjRadius);
	probePointsPs[3] = startingPosPs + (leftPs * adjRadius);

	Vector adjustVecPs = kZero;

	for (U32F i = 0; i < 4; ++i)
	{
		probeParams.m_move = probePointsPs[i] - startingPosPs;

		pNavMesh->ProbePs(&probeParams);

		if (probeParams.m_hitEdge)
		{
			const float errCorrectDist = adjRadius - Dist(probeParams.m_endPoint, startingPosPs);
			const Vector localAdjustVecPs = SafeNormalize(probeParams.m_move, kZero) * -1.0f * errCorrectDist;

			if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_displayNavAdjustDist))
			{
				PrimServerWrapper ps = PrimServerWrapper(pNavChar.ToGameObject()->GetParentSpace());
				ps.SetDuration(kPrimDuration1FramePauseable);
				ps.DrawArrow(probeParams.m_endPoint, localAdjustVecPs, 0.25f, kColorMagentaTrans);
			}

			adjustVecPs += localAdjustVecPs;
		}
	}

	const float adjustLen = Min(Length(adjustVecPs), Scalar(maxMoveDist));
	const Vector adjustDirPs = SafeNormalize(adjustVecPs, kZero);

	const Vector scaledAdjustPs = adjustDirPs * adjustLen;

	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_displayNavAdjustDist))
	{
		//PrimServerWrapper ps = PrimServerWrapper(pNavChar.ToGameObject()->GetParentSpace());
		//ps.SetDuration(kPrimDuration1FramePauseable);
		//ps.DrawArrow(startingPosPs, adjustVecPs, 0.25f, kColorCyan);
		//ps.DrawCross(startingPosPs + scaledAdjustPs, 0.1f, kColorCyan);
	}

	return startingPosPs + scaledAdjustPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AI::SetPluggableAnim(const NdGameObject* pNavChar,
						  const StringId64 animId,
						  const bool mirror		/* = false */)
{
	if (!pNavChar || !pNavChar->GetAnimControl())
		return;

	DC::AnimNavCharInfo* pInfo = pNavChar->GetAnimControl()->Info<DC::AnimNavCharInfo>();
	if (!pInfo)
		return;

	pInfo->m_pluggableAnim       = animId;
	pInfo->m_pluggableAnimMirror = mirror;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AI::SetPluggableFaceAnim(const NdGameObject* pNavChar, const StringId64 animId)
{
	if (!pNavChar || !pNavChar->GetAnimControl())
		return;

	DC::AnimNavCharInfo* pInfo = pNavChar->GetAnimControl()->Info<DC::AnimNavCharInfo>();
	if (!pInfo)
		return;

	pInfo->m_pluggableFaceAnim = animId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float AdvanceTestPhase(const SkeletonId skelId,
							  const ArtItemAnim* pAnim,
							  float curPhase,
							  const Locator& currLocLs,
							  Locator* pNewLocLs,
							  bool useCurrentAlignInsteadOfApRef)
{
	const float phaseStep = pAnim->m_pClipData->m_phasePerFrame;
	Locator testLocLs;
	float newPhase = curPhase;
	do
	{
		newPhase = Limit01(newPhase + phaseStep);

		if (useCurrentAlignInsteadOfApRef)
		{
			if (!FindFutureAlignFromAlign(skelId, pAnim, newPhase, Locator(kIdentity), &testLocLs))
				return false;
		}
		else
		{
			if (!FindAlignFromApReference(skelId, pAnim, newPhase, Locator(kIdentity), SID("apReference"), &testLocLs))
				return false;
		}

		if (Dist(currLocLs.Pos(), testLocLs.Pos()) >= 0.25f)
			break;

	} while (newPhase < 1.0f);

	if (pNewLocLs)
		*pNewLocLs = testLocLs;

	return newPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AI::AnimHasClearMotion(const SkeletonId skelId,
							const ArtItemAnim* pAnim,
							const Locator& initialApRefWs,
							const Locator& rotatedApRefWs,
							const DC::SelfBlendParams* pSelfBlend,
							const NavMesh* pStartMesh,
							const NavMesh::ProbeParams& baseParams,
							bool useCurrentAlignInsteadOfApRef /* = false*/,
							const AnimClearMotionDebugOptions& debugOptions /* = AnimClearMotionDebugOptions() */)
{
	if (!pAnim || !pStartMesh)
		return false;

	const NavMesh* pCurMesh = pStartMesh;
	const float sbEndPhase = pSelfBlend ? Limit01((pSelfBlend->m_time / GetDuration(pAnim)) + pSelfBlend->m_phase) : 1.0f;

	Locator prevLocLs;
	if (useCurrentAlignInsteadOfApRef)
	{
		prevLocLs = Locator(kIdentity);

		// not needed? prevLocLs could be just set to kIdentity
		//if (!FindFutureAlignFromAlign(skelId, pAnim, 0.0f, Locator(kIdentity), &prevLocLs))
		//{
		//	return false;
		//}
	}
	else
	{
		if (!FindAlignFromApReference(skelId, pAnim, 0.0f, Locator(kIdentity), SID("apReference"), &prevLocLs))
		{
			return false;
		}
	}

	Point prevPosWs = initialApRefWs.TransformPoint(prevLocLs.Pos());

	bool result = true;
	float testPhase = 0.0f;
	do
	{
		Locator currLocLs;
		testPhase = AdvanceTestPhase(skelId, pAnim, testPhase, prevLocLs, &currLocLs, useCurrentAlignInsteadOfApRef);

		Locator currLocWs = initialApRefWs.TransformLocator(currLocLs);

		if (pSelfBlend)
		{
			const float sbValLinear = LerpScale(pSelfBlend->m_phase, sbEndPhase, 0.0f, 1.0f, testPhase);
			const float sbVal = CalculateCurveValue(sbValLinear, pSelfBlend->m_curve);

			const Locator rotLocWs = rotatedApRefWs.TransformLocator(currLocLs);

			currLocWs = Lerp(currLocWs, rotLocWs, sbVal);
		}

		const Point currPosWs = currLocWs.Pos();

		NavMesh::ProbeParams params = baseParams;
		params.m_start = pCurMesh->WorldToLocal(prevPosWs);
		params.m_move = pCurMesh->WorldToLocal(currPosWs) - params.m_start;
		params.m_pStartPoly = nullptr;
		params.m_pStartPolyEx = nullptr;

		const bool segmentClear = (pCurMesh->ProbeLs(&params) == NavMesh::ProbeResult::kReachedGoal);

		if (FALSE_IN_FINAL_BUILD(debugOptions.m_draw))
		{
			const Vector kOffsetY(0.0f, debugOptions.m_offsetY, 0.0f);
			g_prim.Draw(DebugLine(prevPosWs + kOffsetY, currPosWs + kOffsetY, segmentClear ? kColorGreen : kColorRed, 2.0f), debugOptions.m_duration);
		}

		if (!segmentClear)
			return false;

		prevLocLs = currLocLs;
		prevPosWs = currPosWs;
		pCurMesh = params.m_pReachedPoly->GetNavMesh();

	} while (testPhase < 1.0f);

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AI::NavMeshForcedDemeanor(const NavMesh* pMesh)
{
	if (!pMesh)
		return INVALID_STRING_ID_64;

	StringId64 demId = INVALID_STRING_ID_64;

	if (pMesh->NavMeshForcesCrouch())
	{
		demId = SID("crouch");
	}
	else if (pMesh->NavMeshForcesProne())
	{
		demId = SID("crawl");
	}
	else
	{
		demId = pMesh->GetTagData(SID("force-demeanor"), INVALID_STRING_ID_64);
	}

	return demId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AI::NavPolyForcesWade(const NavPoly* pPoly)
{
	return pPoly && pPoly->GetTagData(SID("force-npc-wade"), false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AI::TrackSpeedFactor(const NdGameObject* pNavChar, F32 curValue, F32 desValue, F32 filter)
{
	if (!pNavChar)
		return curValue;

	const F32 dt = pNavChar->GetClock()->GetDeltaTimeInSeconds();
	const F32 alpha = Limit01(dt * filter);
	const F32 newValue = ((desValue - curValue) * alpha) + curValue;

	return newValue;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NoAdjustToNavMeshFlagBlender::GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut)
{
	ANIM_ASSERT(pInstance);

	bool valid = false;

	float factor = 0.0f;

	const StringId64 stateId = pInstance->GetStateName();

	switch (stateId.GetValue())
	{
	case SID_VAL("s_traversal-state"):
	case SID_VAL("s_traversal-exit-state"):
	case SID_VAL("s_traversal-tilt-state"):
	case SID_VAL("s_traversal-tilt-no-ref-state"):
		{
			valid = GetTapFactor(&factor);
		}
		break;

	case SID_VAL("s_cinematic-state-enter"):
	case SID_VAL("s_cinematic-state-loop"):
	case SID_VAL("s_cinematic-state-wait"):
	case SID_VAL("s_cinematic-state-exit"):
	case SID_VAL("s_cinematic-state-interrupt"):
	case SID_VAL("s_cinematic-non-ap-state-enter"):
	case SID_VAL("s_cinematic-non-ap-state-loop"):
	case SID_VAL("s_cinematic-non-ap-state-wait"):
	case SID_VAL("s_cinematic-non-ap-state-exit"):
	case SID_VAL("s_cinematic-non-ap-state-interrupt"):
		{
			valid = GetCapFactor(&factor);
		}
		break;

	default:
		{
			const DC::AnimStateFlag stateFlags = pInstance->GetStateFlags();

			if (stateFlags & DC::kAnimStateFlagNoAdjustToNavMesh)
			{
				factor = 1.0f;
			}
			else
			{
				factor = 0.0f;

				if (IsKnownScriptedAnimationState(stateId) && (pInstance->MotionFadeTime() == 0.0f))
				{
					const TimeFrame curTime = m_charAdapter.ToGameObject()->GetCurTime();
					const TimeFrame startTime = pInstance->GetStartTimeAnimClock();

					if (curTime == startTime)
					{
						factor = 1.0f;
					}
				}
			}
			valid = true;
		}
		break;
	}

	const U32 subSystemId = pInstance->GetSubsystemControllerId();
	const NdSubsystemMgr* pSubSysMgr = m_charAdapter.GetSubsystemMgr();
	const NdSubsystem* pSubsystem = pSubSysMgr ? pSubSysMgr->FindSubsystemById(subSystemId) : nullptr;

	if (pSubsystem)
	{
		factor = pSubsystem->GetNoAdjustToNavFactor(pInstance, factor);
	}

	*pDataOut = factor;

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AI::RandomRange(const DC::AiRangeval* pRange)
{
	if (!pRange)
		return 0.0f;

	return RandomFloatRange(pRange->m_val0, pRange->m_val1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 AI::RandomRange(const DC::AiRangevalInt* pRange)
{
	if (!pRange)
		return 0;

	return RandomIntRange(pRange->m_val0, pRange->m_val1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NoAdjustToNavMeshFlagBlender::BlendData(const float& leftData,
											  const float& rightData,
											  float masterFade,
											  float animFade,
											  float motionFade)
{
	const float factor = Lerp(leftData, rightData, motionFade);
	return factor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NoAdjustToNavMeshFlagBlender::GetTapFactor(F32* pDataOut) const
{
	const NdAnimationControllers* pAnimControllers = nullptr;

	if (const NavCharacter* pNavChar = m_charAdapter.ToNavCharacter())
	{
		pAnimControllers = pNavChar->GetNdAnimationControllers();
	}
	else if (const SimpleNavCharacter* pSNpc = m_charAdapter.ToSimpleNavCharacter())
	{
		pAnimControllers = pSNpc->GetAnimationControllers();
	}

	const INdAiTraversalController* pTapController = pAnimControllers ? pAnimControllers->GetTraversalController()
																	  : nullptr;

	if (!pTapController)
		return false;

	if (!pTapController->IsBusy())
		return false;

	const F32 blendFactor = pTapController->GetNavMeshAdjustBlendFactor();
	*pDataOut = 1.0f - blendFactor;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NoAdjustToNavMeshFlagBlender::GetCapFactor(F32* pDataOut) const
{
	const NdAnimationControllers* pAnimControllers = nullptr;

	if (const NavCharacter* pNavChar = m_charAdapter.ToNavCharacter())
	{
		pAnimControllers = pNavChar->GetNdAnimationControllers();
	}
	else if (const SimpleNavCharacter* pSNpc = m_charAdapter.ToSimpleNavCharacter())
	{
		pAnimControllers = pSNpc->GetAnimationControllers();
	}

	const INdAiCinematicController* pCapController = pAnimControllers ? pAnimControllers->GetCinematicController()
																	  : nullptr;

	if (!pCapController)
		return false;

	const F32 blendFactor = pCapController->GetNavMeshAdjustBlendFactor();
	*pDataOut = 1.0f - blendFactor;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct AdjustApData
{
	const RigidBody* m_pRigidBody;
	const NdAnimAlign::InstanceAlignTable* m_pAlignTablePs;
	Point m_adjustedPosPs;
	Vector m_adjustmentVecPs;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool WalkPropagateMoveDeltaToAdjustApAnimStates(AnimStateInstance* pInstance,
													   AnimStateLayer* pStateLayer,
													   uintptr_t userData)
{
	AdjustApData& data = *(AdjustApData*)userData;

	if (!data.m_pAlignTablePs)
		return false;

	Locator instanceAlignPs;
	if (!data.m_pAlignTablePs->Get(pInstance->GetId(), &instanceAlignPs))
		return true;

	const DC::AnimStateFlag stateFlags = pInstance->GetStateFlags();
	const bool apMoveUpdate = (stateFlags & (DC::kAnimStateFlagApMoveUpdate | DC::kAnimStateFlagFirstAlignRefMoveUpdate));

	if ((apMoveUpdate && (stateFlags & DC::kAnimStateFlagAdjustApToRestrictAlign))
		|| pInstance->HasSavedAlign())
	{
		// Only adjust the ap ref if we are in the same parentspace as the animation is playing in.
		// This rarely happens but can happen during scripted animations and can cause BAD bugs.
		if (data.m_pRigidBody == pInstance->GetApLocator().GetBinding().GetRigidBody())
		{
			//const Vector instanceDeltaPs = data.m_adjustedPosPs - instanceAlignPs.Pos();
			//pInstance->ApplyApRestrictAdjustPs(instanceDeltaPs);
			pInstance->ApplyApRestrictAdjustPs(data.m_adjustmentVecPs);
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavUtil::PropagateAlignRestriction(NdGameObject* pGameObj,
										Point_arg adjustedPosPs,
										Vector_arg adjustmentVecPs,
										const NdAnimAlign::InstanceAlignTable& alignTablePs)
{
	if (!pGameObj)
		return;

	AdjustApData data;
	data.m_pRigidBody = pGameObj->GetBoundRigidBody();
	data.m_pAlignTablePs = &alignTablePs;
	data.m_adjustedPosPs = adjustedPosPs;
	data.m_adjustmentVecPs = adjustmentVecPs;

	// apply non-animation adjustments to align to AP ref
	// (this is to prevent the align from popping during certain anim states)
	AnimControl* pAnimControl = pGameObj->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	if (pBaseLayer)
	{
		pBaseLayer->WalkInstancesNewToOld(WalkPropagateMoveDeltaToAdjustApAnimStates, (uintptr_t)&data);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NoAdjustToGroundFlagBlender::GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut)
{
	const float groundAdjust = GetGroundAdjustFactorForInstance(pInstance, m_charAdapter);

	*pDataOut = Limit01(1.0f - groundAdjust);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const NdAnimationControllers* GetAnimationControllers(const NavCharacterAdapter& charAdapter)
{
	const NdAnimationControllers* pAnimControllers = nullptr;

	if (const NavCharacter* pNavChar = charAdapter.ToNavCharacter())
	{
		pAnimControllers = pNavChar->GetNdAnimationControllers();
	}
	else if (const SimpleNavCharacter* pSNpc = charAdapter.ToSimpleNavCharacter())
	{
		pAnimControllers = pSNpc->GetAnimationControllers();
	}

	return pAnimControllers;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetTapGroundAdjust(const NavCharacterAdapter& charAdapter)
{
	const NdAnimationControllers* pAnimControllers = GetAnimationControllers(charAdapter);
	const INdAiTraversalController* pTapController = pAnimControllers ? pAnimControllers->GetTraversalController() : nullptr;

	if (!pTapController)
	{
		return 1.0f;
	}

	return pTapController->GetGroundAdjustBlendFactor();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetHitReactionGroundAdjust(const NavCharacterAdapter& charAdapter)
{
	const NdAnimationControllers* pAnimControllers = GetAnimationControllers(charAdapter);
	const INdAiHitController* pHitController = pAnimControllers ? pAnimControllers->GetHitController() : nullptr;

	if (!pHitController)
	{
		return 1.0f;
	}

	float ragdollFactor = 1.0f;

	if (const NavCharacter* pNavChar = charAdapter.ToNavCharacter())
	{
		if (pNavChar->IsRagdollPhysicalized() && !pHitController->DeathAnimWhitelistedForRagdollGroundProbesFix())
		{
			ragdollFactor = pNavChar->GetRagdollCompositeBody()->GetAnimBlend(0);
		}
	}

	const float controllerFactor = pHitController->GetGroundAdjustBlendFactor();
	const float blended = Min(controllerFactor, ragdollFactor);
	return blended;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetCapGroundAdjust(const NavCharacterAdapter& charAdapter)
{
	const NdAnimationControllers* pAnimControllers = GetAnimationControllers(charAdapter);
	const INdAiCinematicController* pCapController = pAnimControllers ? pAnimControllers->GetCinematicController()
																	  : nullptr;

	if (!pCapController)
	{
		return 1.0f;
	}

	return 1.0f - pCapController->NoAdjustToGround();
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GetGroundAdjustFactorForInstance(const AnimStateInstance* pInstance, const NavCharacterAdapter& charAdapter)
{
	ANIM_ASSERT(pInstance);

	float factor = 0.0f;

	const StringId64 stateId = pInstance->GetStateName();
	switch (stateId.GetValue())
	{
	case SID_VAL("s_traversal-state"):
	case SID_VAL("s_traversal-exit-state"):
	case SID_VAL("s_traversal-tilt-state"):
	case SID_VAL("s_traversal-tilt-no-ref-state"):
		factor = GetTapGroundAdjust(charAdapter);
		break;

	case SID_VAL("s_hit-reaction-state"):
	case SID_VAL("s_hit-reaction-state-w-gesture-state"):
	case SID_VAL("s_death-state"):
		factor = GetHitReactionGroundAdjust(charAdapter);
		break;

	case SID_VAL("s_cinematic-state-enter"):
	case SID_VAL("s_cinematic-state-loop"):
	case SID_VAL("s_cinematic-state-wait"):
	case SID_VAL("s_cinematic-state-exit"):
	case SID_VAL("s_cinematic-state-interrupt"):
	case SID_VAL("s_cinematic-non-ap-state-enter"):
	case SID_VAL("s_cinematic-non-ap-state-loop"):
	case SID_VAL("s_cinematic-non-ap-state-wait"):
	case SID_VAL("s_cinematic-non-ap-state-exit"):
	case SID_VAL("s_cinematic-non-ap-state-interrupt"):
		factor = GetCapGroundAdjust(charAdapter);
		break;

	default:
		{
			const DC::AnimStateFlag stateFlag = pInstance->GetStateFlags();
			if (pInstance && (stateFlag & DC::kAnimStateFlagNoAdjustToGround))
			{
				factor = 0.0f;
			}
			else
			{
				factor = 1.0f;
			}
		}
		break;
	}

	const U32 subSystemId = pInstance->GetSubsystemControllerId();
	const NdSubsystemMgr* pSubSysMgr = charAdapter.GetSubsystemMgr();
	const NdSubsystem* pSubsystemController = pSubSysMgr ? pSubSysMgr->FindSubsystemById(subSystemId) : nullptr;

	if (pSubsystemController)
	{
		factor = pSubsystemController->GetGroundAdjustFactor(pInstance, factor);
	}
	else
	{
		// dirty hack for horse jumps -- HorseJumpController is not yet a subsystemController
		const NdSubsystem* pHorseJumpSubsystem = pSubSysMgr ? pSubSysMgr->FindBoundSubsystemAnimAction(pInstance, SID("HorseJumpAnimAction")) : nullptr;
		if (pHorseJumpSubsystem)
		{
			factor = pHorseJumpSubsystem->GetGroundAdjustFactor(pInstance, factor);
		}
	}

	return factor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NoAdjustToGroundFlagBlender::BlendData(const float& leftData,
											 const float& rightData,
											 float masterFade,
											 float animFade,
											 float motionFade)
{
	const float factor = Lerp(leftData, rightData, motionFade);
	return factor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F NavUtil::FindPathSphereIntersection(const IPathWaypoints* pPathPs,
										 Point_arg searchOriginPs,
										 F32 radius,
										 Point* pPosPsOut)
{
	if (!pPathPs || radius < kSmallestFloat)
	{
		return -1;
	}

	const U32F numWaypoints = pPathPs->GetWaypointCount();
	if (numWaypoints < 2)
	{
		return -1;
	}

	F32 bestErr   = kLargeFloat;
	I32F bestLeg  = -1;

	for (U32F ii = 0; ii < numWaypoints - 1; ++ii)
	{
		const Point leg0 = pPathPs->GetWaypoint(ii);
		const Point leg1 = pPathPs->GetWaypoint(ii + 1);

		Scalar tt0, tt1;
		if (!IntersectSphereSegment(searchOriginPs, radius, leg0, leg1, tt0, tt1))
		{
			continue;
		}

		F32 tt0Err = 0.0f;

		if (tt0 < 0.0f)
		{
			tt0Err = Abs(tt0);
		}
		else if (tt0 > 1.0f)
		{
			tt0Err = tt0 - 1.0f;
		}

		if (tt0Err < bestErr)
		{
			bestErr    = tt0Err;
			*pPosPsOut = Lerp(leg0, leg1, tt0);
			bestLeg    = ii;

			if (bestErr == 0.0f)
				break;
		}

		F32 tt1Err = 0.0f;

		if (tt1 < 0.0f)
		{
			tt1Err = Abs(tt1);
		}
		else if (tt1 > 1.0f)
		{
			tt1Err = tt1 - 1.0f;
		}

		if (tt1Err < bestErr)
		{
			bestErr    = tt1Err;
			*pPosPsOut = Lerp(leg0, leg1, tt1);
			bestLeg    = ii;

			if (bestErr == 0.0f)
				break;
		}

		//		MsgOut("%d : %f %f\n", int(ii), F32(tt0), F32(tt1));
	}

	return bestLeg;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AI::CalculateLosPositionsWs(Point_arg fromPosWs,
								 Point_arg basePosWs,
								 Point* centrePosWs,
								 Point* rightPosWs,
								 Point* leftPosWs)
{
	const Vector sideDir = SafeNormalize(basePosWs - fromPosWs, kUnitZAxis);
	const Vector right = SafeNormalize(Cross(sideDir, kUnitYAxis), kUnitZAxis);

	*centrePosWs = basePosWs - sideDir * 0.25f;
	*rightPosWs	 = basePosWs + sideDir * 0.25f + right * 1.0f;
	*leftPosWs	 = basePosWs + sideDir * 0.25f - right * 1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AI::GetExposureParamsFromMask(DC::ExposureSourceMask exposureMask,
								   DC::ExposureSource& exposureMapSource,
								   ExposureMapType& exposureMapType)
{
	if ((exposureMask & DC::kExposureSourceMaskGrassAlwaysUnexposed) != 0)
	{
		exposureMapType = ExposureMapType::kStealthGrassAlwaysUnexposed;
	}
	else
	{
		exposureMapType = ExposureMapType::kStealthGrassNormal;
	}

	exposureMapSource = DC::kExposureSourceCount;

	for (U32 srcNum = 0; srcNum < DC::kExposureSourceCount; srcNum++)
	{
		if ((exposureMask & (1U << srcNum)) != 0)
		{
			AI_ASSERT(exposureMapSource == DC::kExposureSourceCount);
			exposureMapSource = srcNum;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsKnownScriptedAnimationState(StringId64 stateId)
{
	switch (stateId.GetValue())
	{
	case SID_VAL("s_script-full-body"):
	case SID_VAL("s_script-full-body-loop"):
	case SID_VAL("s_script-full-body-ap"):
	case SID_VAL("s_script-full-body-loop-ap"):
	case SID_VAL("s_script-full-body-collide"):
	case SID_VAL("s_script-full-body-loop-collide"):
	case SID_VAL("s_script-full-body-ap-collide"):
	case SID_VAL("s_script-full-body-loop-ap-collide"):
	case SID_VAL("s_script-full-body-gesture"):
	case SID_VAL("s_script-full-body-loop-gesture"):
	case SID_VAL("s_script-full-body-ap-gesture"):
	case SID_VAL("s_script-full-body-loop-ap-gesture"):
	case SID_VAL("s_script-full-body-collide-gesture"):
	case SID_VAL("s_script-full-body-loop-collide-gesture"):
	case SID_VAL("s_script-full-body-ap-collide-gesture"):
	case SID_VAL("s_script-full-body-loop-ap-collide-gesture"):
		return true;
	}

	return false;
}
