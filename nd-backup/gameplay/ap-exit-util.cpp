/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ap-exit-util.h"

#include "corelib/util/strcmp.h"

#include "ndlib/anim/anim-align-cache.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/nd-action-pack-util.h"
#include "gamelib/gameplay/nav/path-waypoints.h"
#include "gamelib/level/artitem.h"
#include "gamelib/scriptx/h/ap-entry-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
Color ApExit::AvailableExit::GetDebugColor() const
{
	STRIP_IN_FINAL_BUILD_VALUE(kColorGreen);

	const Color color = m_errorFlags.m_wrongMotionType			? kColorPurple	:
						m_errorFlags.m_wrongMotionSubCategory	? kColorPink	:
						m_errorFlags.m_noClearLineOfMotion		? kColorRed		:
						m_errorFlags.m_wrongStrafeType			? kColorMagenta	:
						m_errorFlags.m_wrongDemeanor			? kColorMagenta	:
						m_errorFlags.m_badStopDist				? kColorBlue	:
						m_errorFlags.m_maxPathDistError			? kColorBlue	:
						m_errorFlags.m_maxAngleError			? kColorOrange	:
						m_errorFlags.m_maxRotError				? kColorOrange	:
						m_errorFlags.m_maxAlignDistError		? kColorBlue	:
						m_errorFlags.m_badExitPhaseError		? kColorRed		:
						m_errorFlags.m_badExitPhaseWarning		? kColorYellow	:
						kColorGreen;
	return color;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApExit::AvailableExit::DebugDraw(const CharacterState& cs) const
{
	STRIP_IN_FINAL_BUILD;

	static const Vector kOffsetY = Vector(kUnitYAxis) * 0.1f;

	const AnimControl* pAnimControl = cs.m_pAnimControl;

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(m_pDcDef->m_animName).ToArtItem();
	if (!pAnim)
	{
		return;
	}

	const Locator apRefWs = m_apRef.GetLocator();
	const StringId64 apRefId = m_apRefId;

	Locator endAlignWs, exitLocWs, groundLocWs, pathFitLocWs;

	if (!FindAlignFromApReference(cs.m_skelId, pAnim, 1.0f, apRefWs, apRefId, &endAlignWs))
	{
		return;
	}

	const I32 validMotions = m_pDcDef->m_validMotions;
	const StringId64 mtSubcategory = m_pDcDef->m_mtSubcategory;

	const char* csMtSubCatMsg = cs.m_mtSubcategory == INVALID_STRING_ID_64	? "<none>"	: DevKitOnly_StringIdToString(cs.m_mtSubcategory);
	const char* csMtTypeMsg = cs.m_motionType == kMotionTypeWalk	? "walk"	:
							  cs.m_motionType == kMotionTypeRun		? "run"		:
							  cs.m_motionType == kMotionTypeSprint	? "sprint"	:
							  "<idle>";
	const char* mtSubCatMsg = DevKitOnly_StringIdToString(mtSubcategory);
	StringBuilder<64> mtTypesMsg("%s%s%s%s ",
												(validMotions & (1U << kMotionTypeWalk))	? " walk"	: "",
												(validMotions & (1U << kMotionTypeRun))		? " run"	: "",
												(validMotions & (1U << kMotionTypeSprint))	? " sprint"	: "",
												validMotions == 0							? " idle"	: "");
	const char* exitPhaseMsg = m_errorFlags.m_badExitPhaseError ? "Error" : "Warning";

	const Color color = GetDebugColor();
	ScreenSpaceTextPrinter tp = ScreenSpaceTextPrinter(endAlignWs.Pos(),
													   ScreenSpaceTextPrinter::kPrintNextLineBelowPrevious,
													   kPrimDuration1FrameAuto,
													   g_msgOptions.m_conScale);

	tp.PrintText(color, "%s\n", DevKitOnly_StringIdToStringOrHex(m_pDcDef->m_animName));

	if (m_errorFlags.m_wrongMotionType)
		tp.PrintText(color, "WrongMotionType %s (%s) ", csMtTypeMsg, mtTypesMsg.c_str());
	if (m_errorFlags.m_wrongMotionSubCategory)
		tp.PrintText(color, "WrongMotionSubCategory %s (%s) ", csMtSubCatMsg, mtSubCatMsg);
	if (m_errorFlags.m_noClearLineOfMotion)
		tp.PrintText(color, "NoClearLineOfMotion ");
	if (m_errorFlags.m_wrongStrafeType)
		tp.PrintText(color, "WrongStrafeType ");
	if (m_errorFlags.m_wrongDemeanor)
		tp.PrintText(color, "WrongDemeanor ");
	if (m_errorFlags.m_badStopDist)
		tp.PrintText(color, "BadStopDist ");
	if (m_errorFlags.m_maxPathDistError)
		tp.PrintText(color, "MaxPathDistError ");
	if (m_errorFlags.m_maxAngleError)
		tp.PrintText(color, "MaxAngleError ");
	if (m_errorFlags.m_maxRotError)
		tp.PrintText(color, "MaxRotError ");
	if (m_errorFlags.m_maxAlignDistError)
		tp.PrintText(color, "MaxAlignDistError ");
	if (m_errorFlags.m_badExitPhaseWarning)
		tp.PrintText(color, "BadExitPhase%s (Can't reach path) ", exitPhaseMsg);

	if (m_pDcDef->m_selfBlend)
	{
		const F32 maxSbPhase = (m_pDcDef->m_exitPhase >= 0.0f) ? Limit01(m_pDcDef->m_exitPhase) : 1.0f;
		const F32 animDuration = GetDuration(pAnim);
		const F32 sbStartPhase = m_pDcDef->m_selfBlend->m_phase;

		const F32 maxSbDuration = (maxSbPhase - sbStartPhase) * animDuration;

		if (m_pDcDef->m_selfBlend->m_time > maxSbDuration)
		{
			const F32 overrunDuration = m_pDcDef->m_selfBlend->m_time - maxSbDuration;
			tp.PrintText(kColorYellow, "SelfBlendOverrun (%1.2fs > %1.2fs max) ", m_pDcDef->m_selfBlend->m_time, maxSbDuration);

			const F32 deltaPhase = (animDuration > 0.0f) ? (m_pDcDef->m_selfBlend->m_time / animDuration) : 0.0f;
			const F32 reqSbEndPhase = Limit01(sbStartPhase + deltaPhase);

			Locator sbStartLocWs, sbEndLocWs;
			if (FindAlignFromApReference(cs.m_skelId, pAnim, sbStartPhase, apRefWs, apRefId, &sbStartLocWs)
				&& FindAlignFromApReference(cs.m_skelId, pAnim, reqSbEndPhase, apRefWs, apRefId, &sbEndLocWs))
			{
				DrawDebugFlatArc(apRefWs, sbStartLocWs.Pos(), m_pDcDef->m_maxRotAngleDeg, kColorYellowTrans, 2.0f);
				g_prim.Draw(DebugString(sbStartLocWs.Pos(),
										StringBuilder<256>("sb start (%1.2f)", sbStartPhase).c_str(),
										kColorYellowTrans,
										g_msgOptions.m_conScale));

				DrawDebugFlatArc(apRefWs, sbEndLocWs.Pos(), m_pDcDef->m_maxRotAngleDeg, kColorYellowTrans, 2.0f);
				g_prim.Draw(DebugString(sbEndLocWs.Pos(),
										StringBuilder<256>("sb end (%1.2f)", reqSbEndPhase).c_str(),
										kColorYellowTrans,
										g_msgOptions.m_conScale));
			}
		}
	}

	// Draw exit arc
	if (m_pDcDef->m_exitPhase > 0.0f && m_pDcDef->m_exitPhase < 1.0f &&
		FindAlignFromApReference(cs.m_skelId, pAnim, m_pDcDef->m_exitPhase, apRefWs, apRefId, &exitLocWs))
	{
		DrawDebugFlatArc(apRefWs, exitLocWs.Pos(), m_pDcDef->m_maxRotAngleDeg, kColorCyan, 2.0f);
		StringBuilder<256> sb("exit (%1.2f)", m_pDcDef->m_exitPhase);
		g_prim.Draw(DebugString(exitLocWs.Pos(), sb.c_str(), kColorCyan, g_msgOptions.m_conScale));
	}

	// Draw ground arc
	if (m_pDcDef->m_navMeshPhase > 0.0f && m_pDcDef->m_navMeshPhase < 1.0f &&
		FindAlignFromApReference(cs.m_skelId, pAnim, m_pDcDef->m_navMeshPhase, apRefWs, apRefId, &groundLocWs))
	{
		DrawDebugFlatArc(apRefWs, groundLocWs.Pos(), m_pDcDef->m_maxRotAngleDeg, kColorYellow, 2.0f);
		StringBuilder<256> sb("nav (%1.2f)", m_pDcDef->m_navMeshPhase);
		g_prim.Draw(DebugString(groundLocWs.Pos(), sb.c_str(), kColorYellow, g_msgOptions.m_conScale));
	}

	// Draw path fit arc
	if (m_pDcDef->m_pathFitPhase > 0.0f && m_pDcDef->m_pathFitPhase < 1.0f &&
		FindAlignFromApReference(cs.m_skelId, pAnim, m_pDcDef->m_pathFitPhase, apRefWs, apRefId, &pathFitLocWs))
	{
		const Color pathFitColor = m_pDcDef->m_pathFitPhase >= m_pDcDef->m_exitPhase ? kColorOrange : kColorRed;
		DrawDebugFlatArc(apRefWs, pathFitLocWs.Pos(), m_pDcDef->m_maxRotAngleDeg, pathFitColor, 2.0f);
		StringBuilder<256> sb("path fit (%1.2f)", m_pDcDef->m_pathFitPhase);
		g_prim.Draw(DebugString(pathFitLocWs.Pos(), sb.c_str(), pathFitColor, g_msgOptions.m_conScale));
	}

	Locator navStartLocWs;
	if ((m_minNavStartPhase > 0.0f)
		&& FindAlignFromApReference(cs.m_skelId, pAnim, m_minNavStartPhase, apRefWs, apRefId, &navStartLocWs))
	{
		DrawDebugFlatArc(apRefWs, navStartLocWs.Pos(), m_pDcDef->m_maxRotAngleDeg, kColorYellow, 2.0f);
		StringBuilder<256> sb("nav-auto (%1.2f)", m_minNavStartPhase);
		g_prim.Draw(DebugString(navStartLocWs.Pos(), sb.c_str(), kColorYellow, g_msgOptions.m_conScale));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApExit::AvailableExit::HasError() const
{
	Flags flags = m_errorFlags;

	// Ignore warnings
	flags.m_badExitPhaseWarning = false;

	return flags.m_all;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApExit::ResolveExit(const CharacterState& cs,
						 const DC::ApExitAnimList* pSourceList,
						 const BoundFrame& apRef,
						 AvailableExit* pExitsOut,
						 const U32F maxExitsOut,
						 StringId64 apRefId)
{
	U32F numExits = BuildInitialExitList(cs,
										 pSourceList,
										 apRef,
										 pExitsOut,
										 maxExitsOut,
										 apRefId,
										 false);

	FlagInvalidExits(cs, pExitsOut, numExits, apRefId, false);

	numExits = FilterInvalidExits(pExitsOut, numExits);

	return numExits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApExit::ResolveDefaultExit(const CharacterState& cs,
								const DC::ApExitAnimList* pSourceList,
								const BoundFrame& apRef,
								AvailableExit* pExitsOut,
								U32F maxExitsOut,
								StringId64 apRefId)
{
	U32F numExits = BuildInitialExitList(cs, pSourceList, apRef, pExitsOut, maxExitsOut, apRefId, true);

	FlagInvalidExits(cs, pExitsOut, numExits, apRefId, true);

	numExits = FilterInvalidExits(pExitsOut, numExits);

	return numExits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApExit::BuildInitialExitList(const CharacterState& cs,
								  const DC::ApExitAnimList* pSourceList,
								  const BoundFrame& apRef,
								  AvailableExit* pExitsOut,
								  U32F maxExitsOut,
								  StringId64 apRefId,
								  bool wantIdleExits)
{
	PROFILE_AUTO(Animation);

	if (!pSourceList)
	{
		return 0;
	}

	const U32F gatherCount = pSourceList->m_count;

	U32F numAvailableExits = 0;
	const bool hasLegalPath = cs.m_pPathPs && cs.m_pPathPs->IsValid();

	for (U32F ii = 0; ii < gatherCount; ++ii)
	{
		if (numAvailableExits >= maxExitsOut)
		{
			break;
		}

		const DC::ApExitAnimDef* pExitDef = &pSourceList->m_array[ii];
		ASSERT(pExitDef);
		if (!pExitDef)
		{
			continue;
		}

		if (pExitDef->m_motionMatchingSetId == INVALID_STRING_ID_64)
		{
			const bool isIdleExit = pExitDef->m_validMotions == 0;
			if (wantIdleExits != isIdleExit)
			{
				continue;
			}

			if (!isIdleExit && !hasLegalPath)
			{
				continue;
			}
		}

		AvailableExit newExit;
		if (!ConstructExit(cs, pExitDef, apRef, apRefId, &newExit))
		{
			continue;
		}

		pExitsOut[numAvailableExits] = newExit;
		++numAvailableExits;
	}

	return numAvailableExits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApExit::FlagInvalidExits(const CharacterState& cs,
							  AvailableExit* pExits,
							  const U32F numExits,
							  StringId64 apRefId,
							  bool wantIdleExits /* = false */,
							  bool debugDraw /* = false */)
{
	FlagInvalidDemeanor(cs, pExits, numExits);

	FlagInvalidMotionTypeExits(cs, pExits, numExits, wantIdleExits);

	if (!wantIdleExits)
	{
		FlagInvalidPathExits(cs, pExits, numExits, apRefId, debugDraw);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApExit::FilterInvalidExits(AvailableExit* pExits, const U32F numExits)
{
	PROFILE_AUTO(Animation);

	bool hasMtSubcategory = false;

	for (U32F ii = 0; ii < numExits; ++ii)
	{
		const AvailableExit& currentExit = pExits[ii];

		if (currentExit.m_pDcDef->m_mtSubcategory != SID("normal") && !currentExit.HasError())
		{
			hasMtSubcategory = true;
			break;
		}
	}

	U32F numUsableExits = numExits;

	for (U32F ii = 0; ii < numUsableExits;)
	{
		const AvailableExit& currentExit = pExits[ii];

		if (currentExit.HasError())
		{
			pExits[ii] = pExits[numUsableExits - 1];
			--numUsableExits;
		}
		else if (hasMtSubcategory && (currentExit.m_pDcDef->m_mtSubcategory == SID("normal")))
		{
			// If we have an exit that asks for a specific motion type subcategory, then always prefer those to generics
			pExits[ii] = pExits[numUsableExits - 1];
			--numUsableExits;
		}
		else
		{
			++ii;
		}
	}

	return numUsableExits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApExit::ConstructExit(const CharacterState& cs,
						   const DC::ApExitAnimDef* pExitDef,
						   const BoundFrame& apRef,
						   StringId64 apRefId,
						   AvailableExit* pExitOut)
{
	PROFILE_AUTO(Animation);

	if (!pExitDef || !pExitOut)
	{
		return false;
	}

	const AnimControl* pAnimControl = cs.m_pAnimControl;
	if (!pAnimControl)
	{
		return false;
	}

	bool valid = false;

	if (apRefId == INVALID_STRING_ID_64)
	{
		apRefId = SID("apReference");
	}

	if (pExitDef->m_motionMatchingSetId != INVALID_STRING_ID_64)
	{
		pExitOut->m_pDcDef	= pExitDef;
		pExitOut->m_apRef	= apRef;
		pExitOut->m_apRefId = apRefId;
		pExitOut->m_motionMatchingSetId = pExitDef->m_motionMatchingSetId;

		pExitOut->m_pathFitPhase	= -1.0f;
		pExitOut->m_angleErrDeg		= -1.0f;
		pExitOut->m_animRotAngleDeg = -1.0f;
		pExitOut->m_alignDistErr	= -1.0f;

		valid = true;
	}
	else 
	{
		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(pExitDef->m_animName).ToArtItem();

		valid = ConstructRotatedLocatorsPs(cs, pAnim, pExitDef, apRef, apRefId, pExitOut);

		if (valid && (cs.m_apRegDistance >= 0.0f))
		{
			const Locator& apRefPs = apRef.GetLocatorPs();

			PhaseMatchParams params;
			params.m_apChannelId = apRefId;
			params.m_mirror = pExitOut->m_playMirrored;
			params.m_projectedBasis = GetLocalZ(apRefPs);
			params.m_distMode = cs.m_autoPhaseDistMode;

			if (pExitOut->m_pDcDef)
			{
				if (pExitOut->m_pDcDef->m_navMeshPhase >= 0.0f)
					params.m_minPhase = pExitOut->m_pDcDef->m_navMeshPhase;
				if (pExitOut->m_pDcDef->m_exitPhase >= 0.0f)
					params.m_maxPhase = pExitOut->m_pDcDef->m_exitPhase;
			}

			const float reqDist = cs.m_pNavControl->GetEffectivePathFindRadius() * 1.1f;

			const float matchPhase = ComputePhaseToMatchApAlignDistance(cs.m_skelId, pAnim, apRefPs, reqDist, params);

			pExitOut->m_minNavStartPhase = matchPhase;
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApExit::ConstructRotatedLocatorsPs(const CharacterState& cs,
										const ArtItemAnim* pAnim,
										const DC::ApExitAnimDef* pExitDef,
										const BoundFrame& animApRef,
										StringId64 animApRefId,
										AvailableExit* pExitOut)
{
	if (!pAnim || !pExitDef || !pExitOut)
	{
		return false;
	}

	const Locator kIdentLoc = Locator(kIdentity);

	StringId64 apRefId = animApRefId;
	BoundFrame apRef = animApRef;
	const bool mirrored = cs.m_mirrorBase ^ (pExitDef->m_flags & DC::kApExitItemFlagsMirror);

	if (cs.m_matchApToAlign)
	{
		Locator matchApLocPs;
		if (FindApReferenceFromAlign(cs.m_skelId, pAnim, cs.m_apMatchAlignPs, &matchApLocPs, apRefId, 0.0f, mirrored))
		{
			apRef.SetLocatorPs(matchApLocPs);
		}
	}
	
	if ((pExitDef->m_apPivotId != INVALID_STRING_ID_64)
		&& (pExitDef->m_apPivotId != apRefId)
		&& AnimHasChannel(pAnim, pExitDef->m_apPivotId))
	{
		Locator pivotLocLs, apLocLs;
		bool valid = true;
		valid = valid && EvaluateChannelInAnim(cs.m_skelId, pAnim, pExitDef->m_apPivotId, 0.0f, &pivotLocLs, mirrored);
		valid = valid && EvaluateChannelInAnim(cs.m_skelId, pAnim, animApRefId, 0.0f, &apLocLs, mirrored);

		if (valid)
		{
			apRefId = pExitDef->m_apPivotId;

			const Locator apToPivotDeltaLs = apLocLs.UntransformLocator(pivotLocLs);
			const Locator pivotToApDeltaLs = pivotLocLs.UntransformLocator(apLocLs);

			apRef.SetLocatorPs(animApRef.GetLocatorPs().TransformLocator(apToPivotDeltaLs));
		}
	}

	const F32 exitPhase = pExitDef->m_exitPhase >= 0.0f ? Limit01(pExitDef->m_exitPhase) : 1.0f;
	const F32 pathFitPhase = pExitDef->m_pathFitPhase >= 0.0f ? Limit01(pExitDef->m_pathFitPhase) : exitPhase;
	const F32 prePathFitPhase = Max(0.0f, Limit01(pathFitPhase - (3.0f * pAnim->m_pClipData->m_phasePerFrame)));

	const F32 maxCachePhase = Limit01(Max(exitPhase, pathFitPhase));

	g_animAlignCache.TryCacheAnim(pAnim, SID("align"), 0.0f, maxCachePhase);
	g_animAlignCache.TryCacheAnim(pAnim, apRefId, 0.0f, maxCachePhase);

	Locator startLocLs;
	Locator exitLocLs;
	Locator prePathFitLocLs;
	Locator pathFitLocLs;

	bool valid = FindAlignFromApReferenceCached(cs.m_skelId, pAnim, 0.0f, kIdentLoc, apRefId, mirrored, &startLocLs);
	valid = valid && FindAlignFromApReferenceCached(cs.m_skelId, pAnim, exitPhase, kIdentLoc, apRefId, mirrored, &exitLocLs);
	valid = valid && FindAlignFromApReferenceCached(cs.m_skelId, pAnim, prePathFitPhase, kIdentLoc, apRefId, mirrored, &prePathFitLocLs);
	valid = valid && FindAlignFromApReferenceCached(cs.m_skelId, pAnim, pathFitPhase, kIdentLoc, apRefId, mirrored, &pathFitLocLs);

	if (!valid)
	{
		return false;
	}

	const Locator& apRefPs		= apRef.GetLocatorPs();
	const Locator startLocPs	= apRefPs.TransformLocator(startLocLs);
	const Locator pathFitLocPs	= apRefPs.TransformLocator(pathFitLocLs);
	const F32 maxRotAngleRad	= DegreesToRadians(pExitDef->m_maxRotAngleDeg);

	*pExitOut = AvailableExit();

	pExitOut->m_pDcDef			= pExitDef;
	pExitOut->m_apRef			= apRef;
	pExitOut->m_apRefId			= apRefId;
	pExitOut->m_rotApRefPs		= apRefPs;
	pExitOut->m_rotPathFitLocPs	= pathFitLocPs;
	pExitOut->m_pathFitPhase	= pathFitPhase;
	pExitOut->m_playMirrored	= mirrored;

	const IPathWaypoints* pPathPs = cs.m_pPathPs;

	// To-stopped exit
	if ((!pPathPs || !pPathPs->IsValid()) || (pExitDef->m_validMotions == 0))
	{
		float maxStopRotAngleRad = maxRotAngleRad;

		const Point desiredOrientPosWs = cs.m_frame.GetParentSpace().TransformPoint(cs.m_facePositionPs);
		const Point desiredOrientPosPs = animApRef.GetParentSpace().UntransformPoint(desiredOrientPosWs);
		const Vector toFacePosPs = VectorXz(desiredOrientPosPs - pathFitLocPs.Pos());
		const Vector toFaceDirPs = SafeNormalize(toFacePosPs, kZero);
		const Vector animFaceDirPs = GetLocalZ(pathFitLocPs.Rot());
		const Quat faceRotAdjustPs = QuatFromVectors(animFaceDirPs, toFaceDirPs);
		const Quat limitedRotAdjustPs = LimitQuatAngle(faceRotAdjustPs, maxStopRotAngleRad);

		const Locator rotPathFitPs = Locator(pathFitLocPs.Pos(), pathFitLocPs.Rot() * limitedRotAdjustPs);
		
		Locator rotApPs;
		if (FindApReferenceFromAlign(cs.m_skelId, pAnim, rotPathFitPs, &rotApPs, apRefId, pathFitPhase, mirrored))
		{
			pExitOut->m_rotApRefPs = rotApPs;
			pExitOut->m_rotPathFitLocPs = rotPathFitPs;
		}

		const F32 angleErrDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(animFaceDirPs, toFaceDirPs)));
		pExitOut->m_angleErrDeg = angleErrDeg;

		return true;
	}

	const Point pathOriginPs = pPathPs->GetWaypoint(0);
	const Point searchOriginPs = PointFromXzAndY(apRefPs.Pos(), pathOriginPs);
	F32 searchRadius = DistXz(pathFitLocPs.Pos(), searchOriginPs);

	Point closestPosPs;
	I32F bestLeg = NavUtil::FindPathSphereIntersection(pPathPs, searchOriginPs, searchRadius, &closestPosPs);
	if (bestLeg < 0)
	{
		// The align at the exit phase doesn't reach the path, so ignore the exit phase and emit a warning
		pExitOut->m_errorFlags.m_badExitPhaseWarning = cs.m_isInAp;			// Only makes sense while in an ap

		searchRadius = DistXz(pathOriginPs, searchOriginPs);
		bestLeg = NavUtil::FindPathSphereIntersection(pPathPs, searchOriginPs, searchRadius, &closestPosPs);

		if (bestLeg < 0)
		{
			// The align at the path origin doesn't reach the path
			pExitOut->m_errorFlags.m_badExitPhaseError = cs.m_isInAp;		// Only makes sense while in an ap

			return true;
		}
	}

	const Vector toExitPosPs = SafeNormalize(VectorXz(pathFitLocPs.Pos() - apRefPs.Pos()), kZero);
	const Vector toPathPosPs = SafeNormalize(VectorXz(closestPosPs - apRefPs.Pos()), kZero);

	// Rotation to put our locator pos on the path
	const Quat rotAdjustPs = QuatFromVectors(toExitPosPs, toPathPosPs);
	const Quat apRefRotAdjustPs = LimitQuatAngle(rotAdjustPs, maxRotAngleRad);

	BoundFrame rotApRef = apRef;
	rotApRef.AdjustRotationPs(apRefRotAdjustPs);

	const Locator stage1RotApRefPs = rotApRef.GetLocatorPs();

	const Point bestLeg0				= pPathPs->GetWaypoint(bestLeg);
	const Point bestLeg1				= pPathPs->GetWaypoint(bestLeg + 1);
	const Vector closestLegPs			= VectorXz(bestLeg1 - bestLeg0);
	const Vector normClosestLegPs		= SafeNormalize(closestLegPs, kZero);
	const Vector animExitDirLs			= SafeNormalize(VectorXz(pathFitLocLs.Pos() - prePathFitLocLs.Pos()), kZero);
	const Vector animExitDirPs			= stage1RotApRefPs.TransformVector(animExitDirLs);

	// Counter rotation to point our anim exit dir in the right direction
	const Quat rawPathDirCounterRotPs	= QuatFromVectors(animExitDirPs, normClosestLegPs);
	const Quat pathDirCounterRotPs		= LimitQuatAngle(rawPathDirCounterRotPs, maxRotAngleRad);

	const Locator stage1PathFitLocPs	= stage1RotApRefPs.TransformLocator(pathFitLocLs);
	const Quat finalAlignRotPs			= stage1PathFitLocPs.GetRotation() * pathDirCounterRotPs;
	const Locator stage2PathFitLocPs	= Locator(stage1PathFitLocPs.Pos(), finalAlignRotPs);
	const Locator stage2ApRefPs			= stage2PathFitLocPs.TransformLocator(Inverse(pathFitLocLs));
	const Locator stage2StartLocPs		= stage2ApRefPs.TransformLocator(startLocLs);
	const Vector stage2AnimExitDirPs	= stage2ApRefPs.TransformVector(animExitDirLs);
	const F32 angleErrDeg				= RADIANS_TO_DEGREES(SafeAcos(Dot(stage2AnimExitDirPs, normClosestLegPs)));
	const F32 animRotAngleDeg			= RADIANS_TO_DEGREES(SafeAcos(Dot(GetLocalZ(pathFitLocPs.Rot()), GetLocalZ(stage2PathFitLocPs.Rot()))));

	pExitOut->m_rotApRefPs		= stage2ApRefPs;
	pExitOut->m_rotPathFitLocPs	= stage2PathFitLocPs;
	pExitOut->m_angleErrDeg		= angleErrDeg;
	pExitOut->m_animRotAngleDeg	= animRotAngleDeg;
	pExitOut->m_alignDistErr	= Dist(stage2StartLocPs.Pos(), startLocPs.Pos());

	if (false)
	{
		g_prim.Draw(DebugArrow(searchOriginPs, closestPosPs, kColorRed), Seconds(2.0f));

		g_prim.Draw(DebugCoordAxesLabeled(pathFitLocPs, "pathFitLocPs", 0.3f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhiteTrans, 0.4f), Seconds(2.0f));
		g_prim.Draw(DebugCoordAxesLabeled(stage1PathFitLocPs, "stage1PathFitLocPs", 0.3f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhiteTrans, 0.4f), Seconds(2.0f));
		g_prim.Draw(DebugCoordAxesLabeled(stage2ApRefPs, "stage2ApRefPs", 0.3f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhiteTrans, 0.4f), Seconds(2.0f));

		g_prim.Draw(DebugArrow(stage1PathFitLocPs.Pos(), animExitDirPs, kColorCyan), Seconds(2.0f));
		g_prim.Draw(DebugArrow(stage1PathFitLocPs.Pos(), normClosestLegPs, kColorMagenta), Seconds(2.0f));
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApExit::FlagInvalidDemeanor(const CharacterState& cs, AvailableExit* pExits, U32F numExits)
{
	PROFILE_AUTO(Animation);

	const I32F requestedDemeanor = cs.m_requestedDcDemeanor;
	const I32F requestedDemeanorMask = (1u << requestedDemeanor);

	for (U32F ii = 0; ii < numExits; ++ii)
	{
		AvailableExit& currentExit = pExits[ii];

		if (currentExit.HasError())
		{
			continue;
		}

		const I32F validDemeanors = currentExit.m_pDcDef->m_validDemeanors;

		if ((validDemeanors != 0) && (validDemeanors & requestedDemeanorMask) == 0)
		{
			currentExit.m_errorFlags.m_wrongDemeanor = true;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApExit::FlagInvalidWeaponType(const CharacterState& cs, AvailableExit* pExits, U32F numExits)
{
	if (cs.m_dcWeaponAnimTypeMask == 0)
	{
		// don't flag for invalid weapon type unless we explicitly ask for it
		return;
	}

	bool hasExplicitMatch = false;

	for (U32F i = 0; i < numExits; ++i)
	{
		const AvailableExit& currentEntry = pExits[i];

		if (!currentEntry.m_pDcDef || currentEntry.m_pDcDef->m_validWeapons == 0)
			continue;

		if ((currentEntry.m_pDcDef->m_validWeapons & cs.m_dcWeaponAnimTypeMask) != 0)
		{
			hasExplicitMatch = true;
			break;
		}
	}

	for (U32F i = 0; i < numExits; ++i)
	{
		AvailableExit& currentEntry = pExits[i];

		if (!currentEntry.m_pDcDef)
			continue;

		if (currentEntry.m_pDcDef->m_validWeapons == 0)
		{
			if (hasExplicitMatch)
			{
				currentEntry.m_errorFlags.m_wrongWeaponType = true;
			}
		}
		else if ((currentEntry.m_pDcDef->m_validWeapons & cs.m_dcWeaponAnimTypeMask) == 0)
		{
			currentEntry.m_errorFlags.m_wrongWeaponType = true;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApExit::FlagInvalidMotionTypeExits(const CharacterState& cs,
										AvailableExit* pExits,
										U32F numExits,
										bool wantIdleExits)
{
	PROFILE_AUTO(Animation);

	const I32F motionTypeMask = (1U << cs.m_motionType);

	for (U32F ii = 0; ii < numExits; ++ii)
	{
		AvailableExit& currentExit = pExits[ii];

		if (currentExit.HasError())
		{
			continue;
		}

		const I32F validMotions = currentExit.m_pDcDef->m_validMotions;
		const StringId64 mtSubcategory = currentExit.m_pDcDef->m_mtSubcategory;

		if (wantIdleExits)
		{
			if (validMotions != 0)
			{
				currentExit.m_errorFlags.m_wrongMotionType = true;
			}
		}
		else
		{
			if ((validMotions != 0) && (validMotions & motionTypeMask) == 0)
			{
				currentExit.m_errorFlags.m_wrongMotionType = true;
			}
		}

		if ((mtSubcategory != SID("normal")) && (mtSubcategory != cs.m_mtSubcategory))
		{
			currentExit.m_errorFlags.m_wrongMotionSubCategory = true;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApExit::FlagInvalidPathExits(const CharacterState& cs,
								  AvailableExit* pExits,
								  U32F numExits,
								  StringId64 apRefId,
								  bool debugDraw /* = false */)
{
	PROFILE_AUTO(Animation);

	static const F32 kMinStoppingDist = 1.0f;	//TODO(MB) Add LocomotionController::GetMinStoppingDistance() if needed

	const IPathWaypoints* pPathPs = cs.m_pPathPs;
	if (!pPathPs || !pPathPs->IsValid())
	{
		return;
	}

	const F32 pathLength = pPathPs->ComputePathLength();

	for (U32F ii = 0; ii < numExits; ++ii)
	{
		AvailableExit& currentExit = pExits[ii];

		if (currentExit.HasError())
		{
			continue;
		}

		if (currentExit.m_motionMatchingSetId != INVALID_STRING_ID_64)
		{
			continue;
		}

		const DC::ApExitAnimDef* pExit = currentExit.m_pDcDef;

		const ArtItemAnim* pAnim = cs.m_pAnimControl->LookupAnim(pExit->m_animName).ToArtItem();
		if (!pAnim)
		{
			continue;
		}

		F32 closestDistance;
		I32F closestLegIndex;

		const Locator& stage2PathFitLocPs = currentExit.m_rotPathFitLocPs;
		float legTT = 0.0f;
		const Point newClosestPosPs = pPathPs->ClosestPointXz(stage2PathFitLocPs.Pos(),
															  &closestDistance,
															  &closestLegIndex,
															  &legTT);

		F32 pathDistErr = DistXz(stage2PathFitLocPs.Pos(), newClosestPosPs);

		if ((closestLegIndex + 1) == (pPathPs->GetWaypointCount() - 1))
		{
			if (cs.m_stopAtPathEnd)
			{
				if (legTT >= 1.0f)
				{
					currentExit.m_errorFlags.m_maxPathDistError = true;
					continue;
				}
			}
			else
			{
				const Point path0Ps = pPathPs->GetWaypoint(closestLegIndex);
				const Point path1Ps = pPathPs->GetWaypoint(closestLegIndex + 1);

				pathDistErr = DistPointLineXz(stage2PathFitLocPs.Pos(), path0Ps, path1Ps);
			}
		}

		if (pathDistErr > pExit->m_maxPathDistErr)
		{
			currentExit.m_errorFlags.m_maxPathDistError = true;
			continue;
		}

		if (currentExit.m_angleErrDeg > pExit->m_maxAngleErrDeg)
		{
			currentExit.m_errorFlags.m_maxAngleError = true;
			continue;
		}

		if (currentExit.m_animRotAngleDeg > pExit->m_maxRotAngleDeg)
		{
			currentExit.m_errorFlags.m_maxRotError = true;
			continue;
		}

		if (currentExit.m_alignDistErr > pExit->m_maxAlignDistErr)
		{
			currentExit.m_errorFlags.m_maxAlignDistError = true;
			continue;
		}


		if (cs.m_stopAtPathEnd)
		{
			const Locator apRefPs = currentExit.m_apRef.GetLocatorPs();
			Locator exitLocPs;

			if (FindAlignFromApReference(cs.m_skelId,
										 pAnim,
										 currentExit.m_pDcDef->m_exitPhase,
										 apRefPs,
										 apRefId,
										 &exitLocPs))
			{
				const Point pathEndPs = pPathPs->GetEndWaypoint();
				const float distToEnd = DistXz(exitLocPs.Pos(), pathEndPs);

				if (distToEnd < kMinStoppingDist)
				{
					// Not enough distance to stop after the exit
					currentExit.m_errorFlags.m_badStopDist = true;
					continue;
				}
			}
		}

		const bool clearMotion = EntryHasClearMotionOnNavMesh(cs, currentExit, pAnim, debugDraw);

		if (!clearMotion)
		{
			currentExit.m_errorFlags.m_noClearLineOfMotion = true;
			continue;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApExit::DebugDrawExitRange(const ApExit::CharacterState& cs, const AvailableExit& entry)
{
	STRIP_IN_FINAL_BUILD;

	static const U32F kNumSteps = 15;
	static F32 kLineWidth = 3.0f;
	static const Vector kOffsetY = Vector(kUnitYAxis) * 0.1f;

	const DC::ApExitAnimDef* pExitDef = entry.m_pDcDef;

	if (!pExitDef)
	{
		return;
	}

	const Color color = entry.GetDebugColor();

	const AnimControl* pAnimControl = cs.m_pAnimControl;
	const StringId64 exitAnimId = pExitDef->m_animName;
	const SkeletonId skelId = cs.m_skelId;

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(exitAnimId).ToArtItem();
	if (!pAnim)
	{
		return;
	}

	const StringId64 apRefId = entry.m_apRefId;
	const BoundFrame& apRef = entry.m_apRef;

	const Locator apRefWs = apRef.GetLocator();
	Locator startAlignWs, endAlignWs, exitLocWs, groundLocWs;

	if (!FindAlignFromApReference(skelId, pAnim, 0.0f, apRefWs, apRefId, &startAlignWs))
	{
		return;
	}

	if (!FindAlignFromApReference(skelId, pAnim, 1.0f, apRefWs, apRefId, &endAlignWs))
	{
		return;
	}

	// Draw start and end arcs
	DrawDebugFlatArc(apRefWs, startAlignWs.Pos(), pExitDef->m_maxRotAngleDeg, color, kLineWidth);
	DrawDebugFlatArc(apRefWs, endAlignWs.Pos(), pExitDef->m_maxRotAngleDeg, color, kLineWidth);

	// Draw anim align path angle
	const F32 initialAngle			= -DegreesToRadians(pExitDef->m_maxRotAngleDeg);
	const F32 finalAngle			= DegreesToRadians(pExitDef->m_maxRotAngleDeg);
	const Transform initialXfrm		= Transform(kUnitYAxis, initialAngle);
	const Transform finalXfrm		= Transform(kUnitYAxis, finalAngle);
	const Locator initialXfrmLoc	= Locator(initialXfrm);
	const Locator finalXfrmLoc		= Locator(finalXfrm);
	const F32 phaseStep				= pAnim->m_pClipData->m_phasePerFrame;

	Locator prevLocWs = startAlignWs;
	F32 prevP = 0.0f;
	F32 p = Limit(prevP + phaseStep, 0.0f, 1.0f);

	while (prevP != p)
	{
		Locator locWs;
		FindAlignFromApReference(skelId, pAnim, p, apRefWs, apRefId, &locWs);

		const Locator prevLocLs = apRefWs.UntransformLocator(prevLocWs);
		const Locator prevInitialLs = initialXfrmLoc.TransformLocator(prevLocLs);
		const Locator prevFinalLs = finalXfrmLoc.TransformLocator(prevLocLs);

		const Locator prevInitialWs = apRefWs.TransformLocator(prevInitialLs);
		const Locator prevFinalWs = apRefWs.TransformLocator(prevFinalLs);

		const Locator locLs = apRefWs.UntransformLocator(locWs);
		const Locator initialLs = initialXfrmLoc.TransformLocator(locLs);
		const Locator finalLs = finalXfrmLoc.TransformLocator(locLs);

		const Locator initialWs = apRefWs.TransformLocator(initialLs);
		const Locator finalWs = apRefWs.TransformLocator(finalLs);

		g_prim.Draw(DebugLine(prevLocWs.Pos() + kOffsetY, locWs.Pos() + kOffsetY, color));
		g_prim.Draw(DebugLine(prevInitialWs.Pos() + kOffsetY, initialWs.Pos() + kOffsetY, color, kLineWidth));
		g_prim.Draw(DebugLine(prevFinalWs.Pos() + kOffsetY, finalWs.Pos() + kOffsetY, color, kLineWidth));

		prevLocWs = locWs;
		prevP = p;
		p = Limit(p + phaseStep, 0.0f, 1.0f);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApExit::EntryHasClearMotionOnNavMesh(const CharacterState& cs,
										  const AvailableExit& entry,
										  const ArtItemAnim* pAnim,
										  bool debugDraw /* = false */,
										  DebugPrimTime debugTT /* = kPrimDuration1FrameAuto */)
{
	PROFILE_AUTO(Animation);

	if (!entry.m_pDcDef || !pAnim)
	{
		return false;
	}

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const SkeletonId skelId = cs.m_skelId;

	const DC::ApExitAnimDef* pExit = entry.m_pDcDef;
	const F32 startPhase = Max(Max(entry.m_minNavStartPhase, pExit->m_navMeshPhase), 0.0f);
	const F32 endPhase	 = (pExit->m_pathFitPhase < 0.0f) ? 1.0f : Limit01(pExit->m_pathFitPhase);
	const DC::SelfBlendParams* pSelfBlend = pExit->m_selfBlend;

	const BoundFrame& apRef	 = entry.m_apRef;
	const StringId64 apRefId = entry.m_apRefId;
	BoundFrame rotApRef		 = apRef;
	rotApRef.SetLocatorPs(entry.m_rotApRefPs);

	const SimpleNavControl* pNavControl = cs.m_pNavControl;
	if (!pNavControl)
		return false;

	const NavMesh* pNavMesh = cs.m_apNavLocation.ToNavMesh();
	if (!pNavMesh)
		return false;

	const Locator apRefWs	 = apRef.GetLocatorWs();
	const Locator rotApRefWs = rotApRef.GetLocatorWs();

	g_animAlignCache.TryCacheAnim(pAnim, apRefId, 0.0f, 1.0f);
	g_animAlignCache.TryCacheAnim(pAnim, SID("align"), 0.0f, 1.0f);

	const Vector vo = Vector(0.0f, 0.15f, 0.0f);

	bool valid = true;

	const F32 animDuration		  = pAnim->m_pClipData->m_fNumFrameIntervals * pAnim->m_pClipData->m_secondsPerFrame;
	const bool doSelfBlend		  = pSelfBlend && (pSelfBlend->m_phase >= 0.0f);
	const F32 selfBlendStartPhase = doSelfBlend ? pSelfBlend->m_phase : -1.0f;
	const F32 selfBlendEndPhase	  = doSelfBlend ? Limit01(selfBlendStartPhase + pSelfBlend->m_time / animDuration) : -1.0f;

	const F32 phaseStep	  = pAnim->m_pClipData->m_phasePerFrame;
	const F32 probeRadius = pNavControl->GetMovingNavAdjustRadius();

	U32F index		  = 0;
	F32 evalPhase	  = startPhase;
	Locator prevLocWs = Locator(kIdentity);

	if (!FindAlignFromApReferenceCached(skelId, pAnim, evalPhase, apRefWs, apRefId, entry.m_playMirrored, &prevLocWs))
	{
		return false;
	}

	NavMesh::ProbeParams probeParams;
	probeParams.m_pStartPoly  = nullptr;
	probeParams.m_probeRadius = probeRadius;

	do
	{
		const F32 selfBlendProgressLinear = doSelfBlend ? LerpScale(selfBlendStartPhase, selfBlendEndPhase, 0.0f, 1.0f, evalPhase) : 0.0f;
		const F32 selfBlendValue = doSelfBlend ? CalculateCurveValue(selfBlendProgressLinear, pSelfBlend->m_curve) : 0.0f;
		const Locator interpApRefWs = doSelfBlend ? Lerp(apRefWs, rotApRefWs, selfBlendValue) : apRefWs;

		Locator nextLocWs = prevLocWs;
		if (!FindAlignFromApReferenceCached(skelId,
											pAnim,
											Limit(evalPhase, 0.0f, endPhase),
											interpApRefWs,
											apRefId,
											entry.m_playMirrored,
											&nextLocWs))
		{
			return false;
		}

		if (DistXz(nextLocWs.Pos(), prevLocWs.Pos()) < 0.2f)
		{
			evalPhase += phaseStep;
			continue;
		}

		probeParams.m_start = pNavMesh->WorldToLocal(prevLocWs.Pos());
		probeParams.m_move	= pNavMesh->WorldToLocal(nextLocWs.Pos() - prevLocWs.Pos());

		Color lineClr = kColorGreen;
		const char* pReasonStr = "";
		Point impactPosWs = kOrigin;

		pNavMesh->ProbeLs(&probeParams);

		if (probeParams.m_hitEdge)
		{
			bool wasValid = valid;
			valid = false;
			lineClr = kColorRed;
			pReasonStr = "hitEdge";
			impactPosWs = pNavMesh->LocalToWorld(probeParams.m_impactPoint);
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugString(prevLocWs.Pos() + vo,
									StringBuilder<128>("%d%s%s",
													   int(index),
													   (strlength(pReasonStr) > 0) ? " - " : "",
													   pReasonStr).c_str(),
									lineClr,
									0.5f),
						debugTT);

			if (probeRadius > 0.f)
			{
				DebugDrawFlatCapsule(prevLocWs.Pos() + vo,
									 nextLocWs.Pos() + vo,
									 kUnitYAxis,
									 probeRadius,
									 lineClr,
									 PrimAttrib(),
									 debugTT);
			}
			else
			{
				g_prim.Draw(DebugLine(prevLocWs.Pos() + vo, nextLocWs.Pos() + vo, lineClr, lineClr, 4.0f), debugTT);
			}
		}
		else if (!valid)
		{
			break;
		}

		if (probeParams.m_pReachedPoly)
		{
			pNavMesh = probeParams.m_pReachedPoly->GetNavMesh();

			// Grab cowboy if you hit this
			ASSERT(pNavMesh);

			if (!pNavMesh)
				return false;
		}

		prevLocWs = nextLocWs;
		evalPhase += phaseStep;
		++index;

	} while (evalPhase <= endPhase);

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawApExitAnims(const BoundFrame& apRef,
						  const ApExit::CharacterState& cs,
						  const DC::ApExitAnimList& exitList,
						  bool wantIdleExits,
						  bool isVerbose,
						  bool validOnly,
						  StringId64 apRefId)
{
	STRIP_IN_FINAL_BUILD;

	const U32F kMaxAnimExits = 32;

	if (cs.m_pPathPs && cs.m_pPathPs->IsValid())
	{
		cs.m_pPathPs->DebugDraw(cs.m_frame.GetParentSpace(), true);
	}

	ApExit::AvailableExit availableExits[kMaxAnimExits];
	const U32F numAvailableExits = ApExit::BuildInitialExitList(cs,
																&exitList,
																apRef,
																availableExits,
																kMaxAnimExits,
																apRefId,
																wantIdleExits);

	ApExit::FlagInvalidExits(cs, availableExits, numAvailableExits, apRefId, wantIdleExits, isVerbose);

	for (U32F ii = 0; ii < numAvailableExits; ++ii)
	{
		ApExit::AvailableExit& currentExit = availableExits[ii];

		if (validOnly && currentExit.m_errorFlags.m_all)
		{
			continue;
		}

		ApExit::DebugDrawExitRange(cs, currentExit);

		if (isVerbose)
		{
			currentExit.DebugDraw(cs);
		}
	}
}
