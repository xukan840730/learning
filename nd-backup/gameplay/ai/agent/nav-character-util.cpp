/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/ai/agent/nav-character-util.h"

#include "ndlib/anim/anim-defines.h"

#include "gamelib/anim/motion-matching/motion-matching-manager.h"
#include "gamelib/gameplay/ai/agent/motion-config.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/nav-character-anim-defines.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nav/nav-command.h"
#include "gamelib/gameplay/nav/nav-path-find.h"
#include "gamelib/scriptx/h/nav-character-defines.h"
#include "gamelib/scriptx/h/nd-ai-defines.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "gamelib/scriptx/h/npc-demeanor-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const char* GetConfigPriorityName(const ConfigPriority pri)
{
	switch (pri)
	{
	case kConfigPriorityNormal:
		return "Normal";
	case kConfigPriorityHigh:
		return "High";
	case kConfigPriorityScript:
		return "Script";
	}
	return "<invalid>";
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionType DcMotionTypeToGame(DC::NpcMotionType dcMotionType)
{
	switch (dcMotionType)
	{
	case DC::kNpcMotionTypeWalk:	return kMotionTypeWalk;
	case DC::kNpcMotionTypeRun:		return kMotionTypeRun;
	case DC::kNpcMotionTypeSprint:	return kMotionTypeSprint;
	}
	return kMotionTypeMax;
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::NpcMotionType GameMotionTypeToDc(MotionType motionType)
{
	switch (motionType)
	{
	case kMotionTypeWalk:	return DC::kNpcMotionTypeWalk;
	case kMotionTypeRun:	return DC::kNpcMotionTypeRun;
	case kMotionTypeSprint:	return DC::kNpcMotionTypeSprint;
	}
	return DC::kNpcMotionTypeMax;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavGoalReachedType ConvertDcGoalReachedType(DC::NpcGoalReachedType dcGoalReachedType)
{
	switch (dcGoalReachedType)
	{
	case DC::kNpcGoalReachedTypeStop:				return kNavGoalReachedTypeStop;
	case DC::kNpcGoalReachedTypeStopAndFace:		return kNavGoalReachedTypeStop;
	case DC::kNpcGoalReachedTypeContinue:			return kNavGoalReachedTypeContinue;
	default:										return kNavGoalReachedTypeStop;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* MotionTypeToString(MotionType motionType)
{
	switch (motionType)
	{
	case kMotionTypeWalk:	return "walk";
	case kMotionTypeRun:	return "run";
	case kMotionTypeSprint:	return "sprint";
	}

	return "unknown";
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CanStayOnSplineSegment(const CatmullRom* const pSpline,
								   const int iSeg,
								   Point_arg pos,
								   Vector_arg dir,
								   const bool reverse,
								   const F32 onSplineRadius,
								   CatmullRom::LocalParameter& outParam)
{
	outParam = pSpline->FindLocalParamClosestToPointOnSegmentLinear(pos, iSeg, false);

	const bool isOnSeg = reverse ?
						(outParam.m_u >= 0.0f /* && outParam.m_u <= 1.0f*/) :
						(/*outParam.m_u >= 0.0f &&*/ outParam.m_u <= 1.0f);

	if (!isOnSeg)
	{
		return false;
	}

	bool isClose = false;

	const Point closestPt = pSpline->EvaluatePointLocal(outParam.m_iSegment, outParam.m_u);
	const float distSq = DistSqr(closestPt, pos);
	if (distSq < Sqr(onSplineRadius))
	{
		isClose = true;
	}

	if (!isClose)
	{
		return false;
	}

	if (Length(dir) == 0.0f)
	{
		return true;
	}

	bool isSameDir = true;

	Mat44 ctrlPts;
	pSpline->GetSegmentControlPoints(iSeg, ctrlPts);

	const Point firstPt(ctrlPts[1]);
	const Point lastPt(ctrlPts[2]);

	const Vector seg = reverse ? firstPt - lastPt : lastPt - firstPt;
	const Vector segDir = Normalize(seg);

	const float d = Dot(dir, segDir);
	if (d < 0.0f)
	{
		static const float kSameDirAngle = 100.0f;

		const float angle = RADIANS_TO_DEGREES(SafeAcos(d));
		if (angle > kSameDirAngle)
		{
			isSameDir = false;
		}
	}

	return isSameDir;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static F32 GetShortestArcDiff(F32 startArc, F32 endArc, F32 maxArc, bool looping)
{
	F32 arcDiff = endArc - startArc;

	if (looping && (Abs(arcDiff) > 0.5f * maxArc))
	{
		arcDiff = -1.0f * (maxArc - Abs(arcDiff)) * Sign(arcDiff);
	}

	return arcDiff;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Nav::UpdateSplineAdvancement(const Locator& charLocWs,
								  Vector_arg moveDirWs,
								  const CatmullRom* const pSpline,
								  const F32 splineArcGoal,
								  const F32 splineArcStep,
								  const F32 onSplineRadius,
								  const F32 splineAdvanceStartTolerance,
								  bool& outBeginSplineAdvancement,
								  F32& outCurArcLen,
								  bool debugDraw /* = false */)
{
	if (!pSpline)
	{
		return;
	}

	const Point myPosWs = charLocWs.Pos();
	const Vec3 yOffset0 = Vec3(0.0f, 0.4f, 0.0f);

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		g_prim.Draw(DebugCircle(myPosWs + yOffset0, kUnitYAxis, onSplineRadius, kColorYellowTrans),
					kPrimDuration1FramePauseable);
		g_prim.Draw(DebugArrow(myPosWs + yOffset0, myPosWs + yOffset0 + moveDirWs, kColorYellow),
					kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(myPosWs + yOffset0, myPosWs, kColorMagenta),
					kPrimDuration1FramePauseable);
	}

	const bool reverse = (splineArcStep < 0.0f);

	if (!outBeginSplineAdvancement)
	{
		if (splineAdvanceStartTolerance <= 0.0f)
		{
			const Point splineBeginPosWs = pSpline->EvaluatePointAtArcLength(outCurArcLen);
			if (DistSqr(splineBeginPosWs, myPosWs) < Sqr(onSplineRadius))
			{
				outBeginSplineAdvancement = true;
			}

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				g_prim.Draw(DebugArrow(myPosWs + yOffset0, splineBeginPosWs + yOffset0, kColorCyan),
							kPrimDuration1FramePauseable);
			}
		}
		else
		{
			Point closestPoint;
			const F32 closestArcLen = pSpline->FindArcLengthClosestToPoint(myPosWs, nullptr, false, 0.f, &closestPoint);

			if (Abs(closestArcLen - outCurArcLen) < splineAdvanceStartTolerance
				&& DistSqr(myPosWs, closestPoint) < Sqr(onSplineRadius))
			{
				outBeginSplineAdvancement = true;
			}

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				g_prim.Draw(DebugArrow(myPosWs + yOffset0, closestPoint + yOffset0, kColorCyan),
							kPrimDuration1FramePauseable);
				g_prim.Draw(DebugArrow(pSpline->EvaluatePointAtArcLength(outCurArcLen) + yOffset0,
									   closestPoint + yOffset0,
									   kColorYellow),
							kPrimDuration1FramePauseable);
			}
		}
	}

	if (outBeginSplineAdvancement)
	{
		F32 nearestArc = 0.0f;

		const CatmullRom::LocalParameter currParam = pSpline->ArcLengthToLocalParam(outCurArcLen);	// Find the current segment
		const U32 numSegs = pSpline->GetSegmentCount();
		I32 iSeg = currParam.m_iSegment;

		U32 maxSegmentCount = 4;
		while (true)
		{
			CatmullRom::LocalParameter param;
			const bool stay = CanStayOnSplineSegment(pSpline, iSeg, myPosWs, moveDirWs, reverse, onSplineRadius, param);

			if (stay)	// Stay on this segment
			{
				nearestArc = pSpline->FindArcLengthClosestToPoint(myPosWs, &iSeg, false, 1.0f);
				break;
			}


			I32 iNextSeg = reverse ? iSeg - 1 : iSeg + 1;

			if (pSpline->IsLooped())
			{
				if (iNextSeg < 0)
				{
					iNextSeg = numSegs - 1;
				}
				else if (iNextSeg == numSegs)
				{
					iNextSeg = 0;
				}
			}

			if (iNextSeg == currParam.m_iSegment ||	   // Loop back to where we start
				(iNextSeg < 0 || iNextSeg == numSegs)) // No more next
			{
				nearestArc = outCurArcLen;	// Stay
				break;
			}

			maxSegmentCount--;
			if (maxSegmentCount == 0) // We only allow checking max of 4 segments
			{
				nearestArc = outCurArcLen; // Stay
				break;
			}

			iSeg = iNextSeg;
		}

		const F32 maxArc = pSpline->GetTotalArcLength();
		F32 arcDiff	   = GetShortestArcDiff(outCurArcLen, nearestArc, maxArc, pSpline->IsLooped());

		if (Sign(arcDiff) != Sign(splineArcStep))
			arcDiff = 0.f;

		Nav::TryAdvanceSplineStep(outCurArcLen, splineArcGoal, arcDiff, maxArc, pSpline->IsLooped());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Nav::IsOnSameNavSpace(const NavLocation& locA, const NavLocation& locB)
{
	if (locA.GetType() != locB.GetType())
		return false;

	bool same = false;
	switch (locA.GetType())
	{
	case NavLocation::Type::kNavPoly:
		same = locA.GetNavMeshHandle() == locB.GetNavMeshHandle();
		break;

#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		same = locA.GetNavLedgeGraphHandle() == locB.GetNavLedgeGraphHandle();
		break;
#endif
	}

	return same;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CatmullRom* Nav::CreateApproachSpline(const Process* pOwner,
									  Point_arg startingPosWs,
									  const BoundFrame& targetLoc,
									  Vector_arg targetVelWs,
									  float cutoffRadius)
{
	SpringTracker<Vector> velTrackerWs;
	Vector sprungVelWs = targetVelWs;
	Point curPosWs = targetLoc.GetTranslationWs();
	const float startingSpeed = Length(targetVelWs);

	static CONST_EXPR size_t kMaxControlPoints = 8;
	static CONST_EXPR size_t kModelResolution = 3;

	static CONST_EXPR size_t kNumIterations = kMaxControlPoints * kModelResolution;

	static CONST_EXPR float maxTravelTime = 2.5f;
	static CONST_EXPR float dtPerStep = maxTravelTime / float(kNumIterations - 1);
	static CONST_EXPR float kModelVelSpringK = 4.5f;

	Locator controlPoints[kMaxControlPoints];
	U32F numGeneratedPoints = 0;

	Point prevControlPosWs = curPosWs;

	for (U32F i = 0; i < kNumIterations; ++i)
	{
		const bool recordPoint = (i % kModelResolution) == 0;

		if (DistXz(curPosWs, startingPosWs) < cutoffRadius)
		{
			break;
		}

		if (recordPoint)
		{
			if (DotXz(prevControlPosWs - startingPosWs, curPosWs - startingPosWs) < 0.0f)
			{
				break;
			}

			const U32F controlPointIndex = numGeneratedPoints++;
			AI_ASSERT(controlPointIndex < kMaxControlPoints);
			controlPoints[controlPointIndex] = Locator(curPosWs);

			prevControlPosWs = curPosWs;
		}

		const Vector desVelPs = SafeNormalize(curPosWs - startingPosWs, kZero) * startingSpeed;

		sprungVelWs = velTrackerWs.Track(sprungVelWs, desVelPs, dtPerStep, kModelVelSpringK);

		curPosWs += (sprungVelWs * dtPerStep * -1.0f);
	}

	// no spline needed
	if (numGeneratedPoints < 2)
		return nullptr;

	ListArray<Locator> points;
	points.Init(kMaxControlPoints, controlPoints, numGeneratedPoints);

	CatmullRom* pSpline = CatmullRomBuilder::CreateCatmullRom(points, false, INVALID_STRING_ID_64, pOwner);

	if (const RigidBody* pBindingRb = targetLoc.GetBinding().GetRigidBody())
	{
		const NdGameObject* pGo = pBindingRb->GetOwner();
		pSpline->SetParent(pGo);
	}

	return pSpline;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Nav::IsMoveSetValid(const DC::NpcMoveSetDef* pMoveSet)
{
	if (!pMoveSet)
		return false;

	if ((pMoveSet->m_motionMatchingSetId != INVALID_STRING_ID_64)
		&& !g_pMotionMatchingMgr->DoesMotionMatchingSetExist(pMoveSet->m_motionMatchingSetId))
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 Nav::GetValidMotionTypes(bool wantToStrafe, const DC::NpcMoveSetDef* const* apMoveSets)
{
	BitArray64 validMts;
	validMts.ClearAllBits();

	for (MotionType mt = kMotionTypeWalk; mt < kMotionTypeMax; mt = MotionType(mt + 1))
	{
		if (IsMoveSetValid(apMoveSets[mt]))
		{
			validMts.SetBit(mt);
		}
	}

	const U64 validBits = validMts.GetData();
	return validBits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionType Nav::GetRestrictedMotionTypeFromValid(const NavCharacter* pNavChar,
												 const MotionType desiredMotionType,
												 bool wantToStrafe,
												 const U64 validBits)
{
	AI_ASSERTF(validBits, ("GetRestrictedMotionTypeFromValid given no valid bits! '%s' 0x%.8x req: %d", pNavChar->GetName(), validBits, desiredMotionType));

	MotionType restrictedMotionType = desiredMotionType;

	const U64 mask = (1ULL << (desiredMotionType + 1)) - 1ULL;
	const U64 requestedAndLower = validBits & mask;
	const U32F index = FindLastSetBitIndex(requestedAndLower);

	if (index < kMotionTypeMax)
	{
		restrictedMotionType = MotionType(index);
	}
	else
	{
		const U32F topIndex = FindFirstSetBitIndex(validBits);
		if (topIndex < kMotionTypeMax)
		{
			restrictedMotionType = MotionType(topIndex);
		}
		else
		{
			AI_HALTF(("Should not happen '%s' 0x%.8x req: %d", pNavChar->GetName(), validBits, desiredMotionType));
			restrictedMotionType = kMotionTypeWalk;
		}
	}

	return restrictedMotionType;
}


/// --------------------------------------------------------------------------------------------------------------- ///
const DC::NpcMoveSetDef* Nav::PickMoveSet(const NavCharacter* pNavChar, const DC::NpcMoveSetContainer* pMoveSetContainer)
{
	if (!pMoveSetContainer)
		return nullptr;

	const DC::NpcMoveSetDef* pRet = nullptr;

	switch (pMoveSetContainer->m_dcType.GetValue())
	{
	case SID_VAL("npc-move-set-variations"):
		{
			const DC::NpcMoveSetVariations* pVariations = static_cast<const DC::NpcMoveSetVariations*>(pMoveSetContainer);
			if (pVariations->m_numChildren > 0)
			{
				const U32F randomIndex = ((pVariations->m_instanced != 0) ? pNavChar->GetUserId().GetValue() : urand())
					% pVariations->m_numChildren;
				const DC::NpcMoveSetContainer* pChild = pVariations->m_children->At(randomIndex);
				pRet = PickMoveSet(pNavChar, pChild);
			}
		}
		break;

	case SID_VAL("npc-move-set-def"):
		{
			pRet = static_cast<const DC::NpcMoveSetDef*>(pMoveSetContainer);

			if (nullptr == ScriptManager::Lookup<DC::AnimOverlaySet>(pRet->m_overlayName, nullptr))
			{
				AiLogAnim(pNavChar, "Move set '%s' refers to overlay '%s' that doesn't exist!\n",
						  pRet->m_name.m_string.GetString(),
						  DevKitOnly_StringIdToString(pRet->m_overlayName));
				pRet = nullptr;
			}
		}
		break;

	default:
		AI_ASSERTF(false,
				   ("Unknown npc-move-set-container type '%s'",
					DevKitOnly_StringIdToString(pMoveSetContainer->m_dcType)));
		break;
	}

	return pRet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::NpcMoveSetContainer* Nav::PickMoveSetSubcat(const DC::NpcMotionTypeMoveSets& mtMoveSets,
													  StringId64 requestedSubcat,
													  StringId64* pActualSubcatOut)
{
	const DC::NpcMoveSetContainer* pRet = nullptr;

	if (/* wantStairs */ false)
	{
		for (U32F i = 0; i < mtMoveSets.m_numSubcategories; ++i)
		{
			if (mtMoveSets.m_subcategories[i].m_name == SID("stairs"))
			{
				pRet = mtMoveSets.m_subcategories[i].m_moveSet;
				*pActualSubcatOut = requestedSubcat;
				break;
			}
		}

		if (pRet)
		{
			return pRet;
		}
	}

	for (U32F i = 0; i < mtMoveSets.m_numSubcategories; ++i)
	{
		if (mtMoveSets.m_subcategories[i].m_name == requestedSubcat)
		{
			pRet = mtMoveSets.m_subcategories[i].m_moveSet;
			*pActualSubcatOut = requestedSubcat;
			break;
		}
		else if (pRet == nullptr)
		{
			pRet = mtMoveSets.m_subcategories[i].m_moveSet;
			*pActualSubcatOut = mtMoveSets.m_subcategories[i].m_name;
		}
	}

	return pRet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Nav::SplitPathAfterTap(const PathWaypointsEx& orgPath, PathWaypointsEx* pPrePathOut, PathWaypointsEx* pPostPathOut)
{
	if (!pPrePathOut)
		return;

	pPrePathOut->Clear();

	if (pPostPathOut)
	{
		pPostPathOut->Clear();
	}

	const U32F waypointCount = orgPath.GetWaypointCount();
	bool preTap = true;

	for (U32F i = 0; i < waypointCount; ++i)
	{
		if (preTap)
		{
			pPrePathOut->AddWaypoint(orgPath, i);

			if (orgPath.GetNodeType(i) == NavPathNode::kNodeTypeActionPackEnter)
			{
				preTap = false;
			}
		}
		else if (pPostPathOut)
		{
			pPostPathOut->AddWaypoint(orgPath, i);
		}
		else
		{
			break;
		}
	}
}
