/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/ap-entry-util.h"

#include "ndlib/anim/anim-align-cache.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/util/point-curve.h"

#include "gamelib/anim/motion-matching/motion-matching-manager.h"
#include "gamelib/gameplay/ai/agent/nav-character-adapter.inl"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/ai/nav-character-anim-defines.h"
#include "gamelib/gameplay/ap-entry-cache.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nd-action-pack-util.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/scriptx/h/ap-entry-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApEntry::ResolveEntry(const CharacterState& charState,
						   const DC::ApEntryItemList* pSourceList,
						   const BoundFrame& defaultApRef,
						   AvailableEntry* pEntriesOut,
						   const U32F maxEntriesOut)
{
	PROFILE(Animation, ApEntry_ResolveEntry);

	const U32F numInitialEntries = BuildInitialEntryList(charState,
														 pSourceList,
														 GatherMode::kResolved,
														 defaultApRef,
														 pEntriesOut,
														 maxEntriesOut);

	FlagInvalidEntries(charState, GatherMode::kResolved, defaultApRef, pEntriesOut, numInitialEntries);

	const U32F numActualEntries = FilterInvalidEntries(pEntriesOut, numInitialEntries);

	return numActualEntries;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApEntry::ResolveDefaultEntry(const CharacterState& charState,
								  const DC::ApEntryItemList* pSourceList,
								  const BoundFrame& defaultApRef,
								  AvailableEntry* pEntriesOut,
								  U32F maxEntriesOut)
{
	PROFILE(Animation, ApEntry_ResolveDefEntry);

	const U32F numInitialEntries = BuildInitialEntryList(charState,
														 pSourceList,
														 GatherMode::kDefault,
														 defaultApRef,
														 pEntriesOut,
														 maxEntriesOut);

	FlagInvalidEntries(charState, GatherMode::kDefault, defaultApRef, pEntriesOut, numInitialEntries);

	const U32F numActualEntries = FilterInvalidEntries(pEntriesOut, numInitialEntries);

	return numActualEntries;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntry::ConstructDefaultEntryAlignPs(const CharacterState& charState,
										   const Locator alignToUsePs,
										   ArtItemAnimHandle anim,
										   const DC::ApEntryItem* pEntryDef,
										   const BoundFrame& defaultApRef,
										   float phaseMin,
										   float phaseMax,
										   bool mirror,
										   Locator* pDefaultAlignPsOut,
										   float* pStartPhaseOut,
										   Vector* pEntryVelocityPsOut)
{
	PROFILE_AUTO(Animation);

	if (!anim.ToArtItem())
		return false;

	const Locator& parentSpace = charState.m_frame.GetParentSpace();
	const Locator curLocWs = parentSpace.TransformLocator(alignToUsePs);

	const float distToAp = DistXz(curLocWs.Pos(), defaultApRef.GetTranslationWs());
	const float desiredPhase = (charState.m_forceDefaultEntry)
								   ? 0.0f
								   : GetPhaseToMatchDistance(charState, anim, distToAp, pEntryDef, phaseMin, phaseMax);

	Locator alignPs = Locator(kIdentity);
	const Locator defaultApRefWs = defaultApRef.GetLocatorWs();
	const Locator defaultApRefPs = parentSpace.UntransformLocator(defaultApRefWs);

	const ArtItemAnim* pAnim = anim.ToArtItem();
	const StringId64 apPivotChannelId = GetApPivotChannelId(pAnim, pEntryDef);

	if (!FindAlignFromApReferenceCached(charState.m_skelId,
										pAnim,
										desiredPhase,
										defaultApRefPs,
										apPivotChannelId,
										mirror,
										&alignPs))
	{
		return false;
	}

	const float nudgePhaseAmount = pAnim->m_pClipData->m_phasePerFrame;
	Locator nudgedAlignPs;
	const float nudgePhase = (desiredPhase > nudgePhaseAmount) ? Limit01(desiredPhase - nudgePhaseAmount)
															   : Limit01(desiredPhase + nudgePhaseAmount);

	if (!FindAlignFromApReferenceCached(charState.m_skelId,
										pAnim,
										nudgePhase,
										defaultApRefPs,
										apPivotChannelId,
										mirror,
										&nudgedAlignPs))
	{
		return false;
	}

	const Vector entryVecPs = (desiredPhase > nudgePhase) ? (alignPs.Pos() - nudgedAlignPs.Pos())
														  : (nudgedAlignPs.Pos() - alignPs.Pos());

	if (pDefaultAlignPsOut)
		*pDefaultAlignPsOut = alignPs;

	if (pStartPhaseOut)
		*pStartPhaseOut = desiredPhase;

	if (pEntryVelocityPsOut)
	{
		const float dt = Abs(nudgePhase - desiredPhase) * pAnim->m_pClipData->m_secondsPerFrame
						 * pAnim->m_pClipData->m_fNumFrameIntervals;
		const Vector velocityPs = (dt > 0.0f) ? (entryVecPs / dt) : kZero;
// 		const float debugSpeed = (dt > 0.0f) ? float(Length(entryVecPs) / dt) : 0.0f;
		*pEntryVelocityPsOut = velocityPs;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator ApEntry::ConstructedRotatedAlignForEntryPs(const ApEntry::CharacterState& charState,
												   ArtItemAnimHandle anim,
												   const DC::ApEntryItem* pEntryDef,
												   const BoundFrame& defaultApRef,
												   float phaseMin,
												   float phaseMax,
												   bool mirror)
{
	PROFILE_AUTO(Animation);

	const Locator& curLocPs = charState.m_frame.GetLocatorPs();

	if ((charState.m_frameRotWindowDeg <= 0.0f) || (charState.m_forceDefaultEntry))
		return curLocPs;

	const Quat pivotOrientLs = QuatFromLookAt(Point(kOrigin) - charState.m_frameRotPivotLs, kUnitYAxis);
	const Locator pivotLocLs = Locator(charState.m_frameRotPivotLs, pivotOrientLs);
	const Locator pivotPs = curLocPs.TransformLocator(pivotLocLs);

	const Locator& parentSpace = charState.m_frame.GetParentSpace();
	const Locator pivotWs = parentSpace.TransformLocator(pivotPs);

	const ArtItemAnim* pAnim = anim.ToArtItem();
	const float distToAp = DistXz(pivotWs.Pos(), defaultApRef.GetTranslationWs());
	const float phaseToPivot = GetPhaseToMatchDistance(charState, anim, distToAp, pEntryDef, phaseMin, phaseMax);
	const float ppf = pAnim->m_pClipData->m_phasePerFrame;
	const float nudgedPhaseToPivot = Limit01((phaseToPivot > ppf) ? (phaseToPivot - ppf) : (phaseToPivot + ppf));

	const StringId64 apPivotChannelId = GetApPivotChannelId(pAnim, pEntryDef);
	Locator phasedEntryAlignPs, nudgedEntryAlignPs;
	const bool valid0 = FindAlignFromApReferenceCached(charState.m_skelId,
													   pAnim,
													   phaseToPivot,
													   defaultApRef.GetLocatorPs(),
													   apPivotChannelId,
													   mirror,
													   &phasedEntryAlignPs);
	const bool valid1 = FindAlignFromApReferenceCached(charState.m_skelId,
											  pAnim,
											  nudgedPhaseToPivot,
											  defaultApRef.GetLocatorPs(),
											  apPivotChannelId,
											  mirror,
											  &nudgedEntryAlignPs);

	if (!valid0 || !valid1)
	{
		return curLocPs;
	}

	const Vector phasedEntryVecPs = (phaseToPivot > ppf) ? (phasedEntryAlignPs.Pos() - nudgedEntryAlignPs.Pos())
														 : (nudgedEntryAlignPs.Pos() - phasedEntryAlignPs.Pos());

	const Vector toEntryAlignPs = phasedEntryAlignPs.Pos() - defaultApRef.GetTranslationPs();
	const Vector toPivotAlignPs = pivotPs.Pos() - defaultApRef.GetTranslationPs();

	const Vector normToEntryAlignPs = SafeNormalize(toEntryAlignPs, kZero);
	const Vector normToPivotAlignPs = SafeNormalize(toPivotAlignPs, kZero);

	const Quat rotToPivotPs = QuatFromVectors(normToEntryAlignPs, normToPivotAlignPs);
	Vec4 axis;
	float angle;
	rotToPivotPs.GetAxisAndAngle(axis, angle);

	float constrainedAngle = angle;
	if (axis.Y() > 0.0f)
	{
		constrainedAngle = Min(angle, DEGREES_TO_RADIANS(pEntryDef->m_rangeAngleCcw));
	}
	else
	{
		constrainedAngle = Min(angle, DEGREES_TO_RADIANS(pEntryDef->m_rangeAngleCw));
	}

	const Quat constrainedRotToPivotPs = Quat(axis, constrainedAngle);

	BoundFrame rotatedApRef = defaultApRef;
	rotatedApRef.AdjustRotationPs(constrainedRotToPivotPs);

	const Vector phasedEntryVecLs = defaultApRef.GetLocatorPs().UntransformVector(phasedEntryVecPs);
	const Vector rotatedPhasedEntryVecPs = rotatedApRef.GetLocatorPs().TransformVector(phasedEntryVecLs);

	const Vector normCurPs = SafeNormalize(charState.m_velocityPs, GetLocalZ(curLocPs.Rot()));
	const Vector normDesiredPs = SafeNormalize(rotatedPhasedEntryVecPs, normCurPs);

	const Quat desiredDeltaQuat = QuatFromVectors(normCurPs, normDesiredPs);
	const Quat constrainedQuat = QuatLimitAngleRad(desiredDeltaQuat, DEGREES_TO_RADIANS(charState.m_frameRotWindowDeg));

	const Locator alignInPivotSpace = pivotPs.UntransformLocator(curLocPs);
	const Locator rotatedPivotPs = Locator(pivotPs.Pos(), pivotPs.Rot() * constrainedQuat);
	const Locator newAlignPs = rotatedPivotPs.TransformLocator(alignInPivotSpace);

/*
	const Point apPosPs = defaultApRef.GetTranslationPs();
	g_prim.Draw(DebugArrow(apPosPs, normToEntryAlignPs, kColorCyan), Seconds(5.0f));
	g_prim.Draw(DebugArrow(apPosPs, normToPivotAlignPs, kColorMagenta), Seconds(5.0f));

	g_prim.Draw(DebugArrow(pivotPs.Pos(), normCurPs, kColorCyan), Seconds(5.0f));
	g_prim.Draw(DebugArrow(pivotPs.Pos(), normDesiredPs, kColorMagenta), Seconds(5.0f));
	g_prim.Draw(DebugCoordAxesLabeled(pivotPs, "pivotPs"), Seconds(5.0f));
	g_prim.Draw(DebugCoordAxesLabeled(newAlignPs, "alignToUsePs"), Seconds(5.0f));
*/

	return newAlignPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntry::ConstructRotatedEntryAndApRef(const CharacterState& charState,
											const Locator& alignToUsePs,
											ArtItemAnimHandle anim,
											const DC::ApEntryItem* pEntryDef,
											const BoundFrame& defaultApRef,
											const Locator& defaultAlignPs,
											Vector_arg defaultEntryVelocityPs,
											Locator* pRotatedAlignPsOut,
											BoundFrame* pRotatedApRefOut,
											Vector* pRotatedEntryVecPsOut)
{
	PROFILE_AUTO(Animation);

	// NB: This should probably be automatically inferred if we have an pivot AP
	const Locator& parentSpace = charState.m_frame.GetParentSpace();
	const Point apRefPosWs = defaultApRef.GetLocatorWs().TransformPoint(charState.m_apRotOriginLs);
	const Point apRefPosPs = parentSpace.UntransformPoint(apRefPosWs);

	Vector toDefaultAlignPs = defaultAlignPs.Pos() - apRefPosPs;
	Vector toCurrentAlignPs = alignToUsePs.Pos() - apRefPosPs;

	toDefaultAlignPs.SetY(0);
	toCurrentAlignPs.SetY(0);

	const Vector normToDefaultPs = SafeNormalize(toDefaultAlignPs, kUnitZAxis);
	const Vector normToCurrentPs = SafeNormalize(toCurrentAlignPs, kUnitZAxis);
	const Scalar dotP = Dot(normToDefaultPs, normToCurrentPs);

	float angleDiffRad = SafeAcos(dotP);
	if (IsNegative(Dot(Cross(kUnitYAxis, normToDefaultPs), normToCurrentPs)))
		angleDiffRad = -angleDiffRad;

	const float angleMinRad = -DEGREES_TO_RADIANS(pEntryDef->m_rangeAngleCw);
	const float angleMaxRad = DEGREES_TO_RADIANS(pEntryDef->m_rangeAngleCcw);
	const float constrainedAngleDiffRad = Limit(angleDiffRad, angleMinRad, angleMaxRad);

	BoundFrame rotatedApRef = defaultApRef;
	if (charState.m_forceDefaultEntry)
	{
		// leave unrotated
	}
	else
	{
		const Quat rotAdjustmentWs = Quat(kUnitYAxis, constrainedAngleDiffRad);
		rotatedApRef.AdjustRotationWs(rotAdjustmentWs);
	}

	if (pRotatedApRefOut)
		*pRotatedApRefOut = rotatedApRef;

	const Locator defaultApRefPs = parentSpace.UntransformLocator(defaultApRef.GetLocator());
	const Locator rotatedApRefPs = parentSpace.UntransformLocator(rotatedApRef.GetLocator());

	const Locator defaultAlignLs = defaultApRefPs.UntransformLocator(defaultAlignPs);
	const Locator rotatedAlignPs = rotatedApRefPs.TransformLocator(defaultAlignLs);

	const Vector defaultEntryVelocityLs = defaultApRefPs.UntransformVector(defaultEntryVelocityPs);
	const Vector rotatedEntryVelocityPs = rotatedApRefPs.TransformVector(defaultEntryVelocityLs);

	if (pRotatedAlignPsOut)
		*pRotatedAlignPsOut = rotatedAlignPs;

	if (pRotatedEntryVecPsOut)
		*pRotatedEntryVecPsOut = rotatedEntryVelocityPs;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntry::ConstructEntryNavLocation(const CharacterState& charState,
										const NavLocation& apNavLoc,
										const AvailableEntry& entry,
										NavLocation* pNavLocOut)
{
	if (!pNavLocOut)
	{
		return false;
	}

	NavLocation ret;

	const Point entryPosPs = entry.m_entryAlignPs.Pos();

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	bool valid = false;
	const SimpleNavControl* pNavControl = charState.m_pNavControl;

	if ((charState.m_entryNavLocType == NavLocation::Type::kNavPoly) && pNavControl)
	{
		const Nav::StaticBlockageMask obeyedStaticBlockage = pNavControl->GetObeyedStaticBlockers();

		const NavMesh* pApNavMesh = nullptr;
		const NavPoly* pApNavPoly = apNavLoc.ToNavPoly(&pApNavMesh);
		const NavMesh* pCharNavMesh = pNavControl->GetNavMesh();

		Point searchPosPs = entryPosPs;

		if (pApNavMesh)
		{
			const NavPoly* pInitialPoly = pApNavMesh->FindContainingPolyPs(entryPosPs, 2.0f, obeyedStaticBlockage);
			if (!pInitialPoly)
			{
				pApNavPoly->FindNearestPointPs(&searchPosPs, entryPosPs);
			}
		}

		const Point searchPosWs = entry.m_rotatedApRef.GetParentSpace().TransformPoint(searchPosPs);

		NavMesh::FindPointParams fpParams;
		fpParams.m_point		= searchPosPs;
		fpParams.m_depenRadius	= pNavControl->GetActionPackEntryDistance();
		fpParams.m_searchRadius = fpParams.m_depenRadius * 1.5f;
		fpParams.m_obeyedStaticBlockers = obeyedStaticBlockage;
		fpParams.m_crossLinks = true;

		NavMesh::ProbeParams probeParams;
		probeParams.m_obeyStealthBoundary = fpParams.m_obeyStealthBoundary;
		probeParams.m_crossLinks = true;
		probeParams.m_obeyedStaticBlockers = obeyedStaticBlockage;

		const NavPoly* pPoly = nullptr;

		if (pApNavMesh)
		{
			pApNavMesh->FindNearestPointPs(&fpParams);
			pPoly = fpParams.m_pPoly;

			probeParams.m_start = fpParams.m_nearestPoint;
			probeParams.m_move = (fpParams.m_point - probeParams.m_start) * 0.99f;

			const NavMesh::ProbeResult res = pApNavMesh->ProbePs(&probeParams);

			if (res != NavMesh::ProbeResult::kReachedGoal)
			{
				pPoly = nullptr;
			}
		}

		if (!pPoly && pCharNavMesh && pApNavMesh && (pCharNavMesh != pApNavMesh))
		{
			const Point entryPosMyPs = charState.m_frame.GetParentSpace().TransformPoint(searchPosWs);
			fpParams.m_point = entryPosMyPs;
			pCharNavMesh->FindNearestPointPs(&fpParams);
			pPoly = fpParams.m_pPoly;

			probeParams.m_start = fpParams.m_nearestPoint;
			probeParams.m_move = (fpParams.m_point - probeParams.m_start) * 0.99f;

			const NavMesh::ProbeResult res = pCharNavMesh->ProbePs(&probeParams);

			if (res != NavMesh::ProbeResult::kReachedGoal)
			{
				pPoly = nullptr;
			}
		}

		if (pPoly)
		{
			const NavMesh* pMesh = pPoly->GetNavMesh();
			NAV_ASSERT(pMesh);

			const Point nearestWs = pMesh->ParentToWorld(fpParams.m_nearestPoint);
			ret.SetWs(nearestWs, pPoly);

			valid = true;
		}
		else
		{
			ret.SetWs(searchPosWs);
		}
	}
#if ENABLE_NAV_LEDGES
	else if (const NavLedge* pApNavLedge = apNavLoc.ToNavLedge())
	{
		ret.SetPs(entryPosPs, pApNavLedge);

		valid = true;
	}
#endif

	*pNavLocOut = ret;

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ApEntry::GetPhaseToMatchDistance(const CharacterState& charState,
									   ArtItemAnimHandle anim,
									   float distance,
									   const DC::ApEntryItem* pEntryDef,
									   float phaseMin,
									   float phaseMax)
{
	if (!pEntryDef)
		return -1.0f;

	const float cachedValue = g_apEntryCache.TryGetPhaseForDistance(charState.m_skelId, pEntryDef, anim, distance);
	if (cachedValue >= 0.0f)
	{
		return cachedValue;
	}

	const StringId64 apPivotChannelId = GetApPivotChannelId(anim.ToArtItem(), pEntryDef);

	PhaseMatchParams params;
	params.m_apChannelId = apPivotChannelId;
	params.m_minPhase = Limit01(phaseMin);
	params.m_maxPhase = Limit01(phaseMax);

	const float bestPhase = ComputePhaseToMatchDistanceCached(charState.m_skelId, anim.ToArtItem(), distance, params);

	return bestPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ValidateEntryEffs(const ApEntry::CharacterState& charState, ApEntry::AvailableEntry& entry)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	const DC::ApEntryItem* pEntryDef = entry.m_pDcDef;

	if (!pEntryDef)
		return false;

	const ArtItemAnim* pArtItemAnim = charState.m_pAnimControl->LookupAnim(pEntryDef->m_animName).ToArtItem();
	if (!pArtItemAnim || !pArtItemAnim->m_pClipData)
		return false;

	bool valid = true;

	if (const EffectAnim* pEffectAnim = pArtItemAnim->m_pEffectAnim)
	{
		for (U32F ii = 0; ii < pEffectAnim->m_numEffects; ++ii)
		{
			const EffectAnimEntry& effectEntry = pEffectAnim->m_pEffects[ii];

			const StringId64 effectNameId = effectEntry.GetNameId();

			switch (effectNameId.GetValue())
			{
			case SID_VAL("gear-effect"):
			case SID_VAL("foot-effect"):
			case SID_VAL("voice-effect"):
			case SID_VAL("sound-effect"):
			case SID_VAL("hand-effect-left"):
			case SID_VAL("hand-effect-right"):
				if (!g_navCharOptions.m_actionPack.m_validateApSoundEffs)
					continue;
			}

			const float effectPhase = GetPhaseFromClipFrame(pArtItemAnim->m_pClipData, effectEntry.GetFrame());

			if (effectPhase < entry.m_phaseMax)
			{
				AnimError("EFF '%s' on '%s' may not trigger - phase %0.1f (frame %0.1f) < phase-range max %0.1f\n",
						  DevKitOnly_StringIdToString(effectNameId),
						  DevKitOnly_StringIdToString(pEntryDef->m_animName),
						  effectPhase,
						  effectEntry.GetFrame(),
						  entry.m_phaseMax);
				valid = false;
			}
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ShouldPreFilterEntry(const ApEntry::CharacterState& charState,
								 const DC::ApEntryItem* pEntryDef,
								 const ApEntry::GatherMode gatherMode,
								 const bool hasExplicitMtMatch,
								 const bool hasExplicitWeaponMatch)
{
	ANIM_ASSERT(pEntryDef);

	if (FALSE_IN_FINAL_BUILD(gatherMode == ApEntry::GatherMode::kDebug))
	{
		return false;
	}

	const bool isDefaultEntry = (0 != (pEntryDef->m_flags & DC::kApEntryItemFlagsDefaultEntry));
	const bool wantDefaultEntries = gatherMode != ApEntry::GatherMode::kResolved;

	if (wantDefaultEntries != isDefaultEntry)
	{
		return true;
	}

	if (hasExplicitMtMatch)
	{
		if (pEntryDef->m_mtSubcategory != charState.m_mtSubcategory)
		{
			return true;
		}
	}
	else if (pEntryDef->m_mtSubcategory != SID("normal"))
	{
		return true;
	}

	const bool requireMotion = pEntryDef->m_flags & DC::kApEntryItemFlagsRequireMotion;
	const I32 reqMtMask = charState.m_strafing ? pEntryDef->m_validStrafeMotions : pEntryDef->m_validMotions;

	if (requireMotion)
	{
		if (!charState.m_moving)
		{
			return true;
		}

		const I32 curMtMask = (1U << charState.m_motionType);

		if (0 == (reqMtMask & curMtMask))
		{
			return true;
		}
	}

	if (pEntryDef->m_validWeapons == 0)
	{
		if (hasExplicitWeaponMatch)
		{
			return true;
		}
	}
	else if ((pEntryDef->m_validWeapons & charState.m_dcWeaponAnimTypeMask) == 0)
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApEntry::BuildInitialEntryList(const CharacterState& charState,
									const DC::ApEntryItemList* pSourceList,
									const GatherMode gatherMode,
									const BoundFrame& defaultApRef,
									AvailableEntry* pEntriesOut,
									const U32F maxEntriesOut)
{
	const U32F currentDemeanor = charState.m_dcDemeanorValue;
	const U32F currentDemeanorMask = (1ULL << currentDemeanor);

	PROFILE(Animation, BuildInitialEntryList);

	if (!pSourceList)
		return 0;

	U32F numAvailableEntries = 0;

	bool hasExplicitMtMatch = false;
	bool hasExplicitWeaponMatch = false;

	for (U32F i = 0; i < pSourceList->m_count; ++i)
	{
		const DC::ApEntryItem* pEntryDef = &pSourceList->m_array[i];

		const bool isDefaultEntry = (0 != (pEntryDef->m_flags & DC::kApEntryItemFlagsDefaultEntry));
		const bool wantDefaultEntries = gatherMode != ApEntry::GatherMode::kResolved;

		// at least for now, only do this for hunker: REQUIRE
		// a matching subtype, even to the extent of preferring default entries over resolved entries.
		//
		// possibly a good thing to do in all cases.
		if (charState.m_mtSubcategory == SID("hunker"))
		{
			if (!hasExplicitMtMatch && charState.m_mtSubcategory == pEntryDef->m_mtSubcategory)
			{
				hasExplicitMtMatch = true;
			}
		}

		if (wantDefaultEntries != isDefaultEntry)
		{
			continue;
		}

		if (!hasExplicitMtMatch && (charState.m_mtSubcategory != INVALID_STRING_ID_64)
			&& (charState.m_mtSubcategory == pEntryDef->m_mtSubcategory))
		{
			hasExplicitMtMatch = true;
		}

		if (!hasExplicitWeaponMatch && (pEntryDef->m_validWeapons != 0)
			&& (pEntryDef->m_validWeapons & charState.m_dcWeaponAnimTypeMask))
		{
			hasExplicitWeaponMatch = true;
		}

		if (hasExplicitMtMatch && hasExplicitWeaponMatch)
		{
			break;
		}
	}

	for (U32F i = 0; i < pSourceList->m_count; ++i)
	{
		if (numAvailableEntries >= maxEntriesOut)
		{
			MsgWarn("Npc maxed out potential AP entry list size %d with possible %d entries\n",
					maxEntriesOut,
					pSourceList->m_count);
			break;
		}

		PROFILE(Animation, ApEntry_ConstructEntry);

		const DC::ApEntryItem* pEntryDef = &pSourceList->m_array[i];
		if (!pEntryDef)
			continue;

		if (ShouldPreFilterEntry(charState, pEntryDef, gatherMode, hasExplicitMtMatch, hasExplicitWeaponMatch))
		{
			continue;
		}

		if ((pEntryDef->m_motionMatchingSetId != INVALID_STRING_ID_64)
			&& !g_pMotionMatchingMgr->DoesMotionMatchingSetExist(pEntryDef->m_motionMatchingSetId))
		{
			continue;
		}

		AvailableEntry newEntry;
		if (!ConstructEntry(charState, pEntryDef, defaultApRef, &newEntry))
		{
			continue;
		}

		if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_actionPack.m_validateApEntryEffs))
		{
			ValidateEntryEffs(charState, newEntry);
		}

		pEntriesOut[numAvailableEntries] = newEntry;
		++numAvailableEntries;
	}

	return numAvailableEntries;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntry::ConstructEntry(const CharacterState& charState,
							 const DC::ApEntryItem* pEntryDef,
							 const BoundFrame& defaultApRef,
							 AvailableEntry* pEntryOut)
{
	PROFILE_AUTO(Animation);

	if (!pEntryDef || !pEntryOut)
		return false;

	const AnimControl* pAnimControl = charState.m_pAnimControl;
	ANIM_ASSERT(pAnimControl);

	ArtItemAnimHandle hAnim = g_apEntryCache.LookupEntryAnim(charState.m_skelId, pEntryDef);

	const ArtItemAnim* pAnim = hAnim.ToArtItem();

	if (!pAnim)
	{
		hAnim = pAnimControl->LookupAnim(pEntryDef->m_animName);
		pAnim = hAnim.ToArtItem();
	}

	if (!pAnim)
	{
		return false;
	}

	g_apEntryCache.TryAddEntry(charState.m_skelId, pEntryDef);

	float phaseMin = -1.0f;
	float phaseMax = -1.0f;

	if (!g_apEntryCache.GetPhaseRangeForEntry(charState.m_skelId, hAnim, pEntryDef, phaseMin, phaseMax))
	{
		phaseMin = pEntryDef->m_phaseMin;
		phaseMax = pEntryDef->m_phaseMax;
	}

	float startPhase = phaseMin;

	BoundFrame apRef = defaultApRef;
	const bool mirrored = charState.m_mirrorBase ^ ((pEntryDef->m_flags & DC::kApEntryItemFlagsMirror) != 0);

	if (charState.m_matchApToAlign)
	{
		Locator matchApLocPs;
		if (FindApReferenceFromAlign(charState.m_skelId,
									 pAnim,
									 charState.m_apMatchAlignPs,
									 &matchApLocPs,
									 SID("apReference"),
									 1.0f,
									 mirrored))
		{
			apRef.SetLocatorPs(matchApLocPs);
		}
	}

	Locator defaultAlignPs	= apRef.GetLocatorPs();
	Locator rotatedAlignPs	= defaultAlignPs;
	Locator alignToUsePs	= defaultAlignPs;
	BoundFrame rotatedApRef	= apRef;

	Vector rotatedEntryVelocityPs = kZero;

	if (charState.m_entryNavLocType == NavLocation::Type::kNavPoly)
	{
		BoundFrame defaultFittingApRef = apRef;
		Locator apToPivotDeltaLs = Locator(kIdentity);
		Locator pivotToApDeltaLs = Locator(kIdentity);

		g_animAlignCache.TryCacheAnim(pAnim, SID("align"), phaseMin, Max(phaseMax, pEntryDef->m_clearMotionEndPhase));
		g_animAlignCache.TryCacheAnim(pAnim, SID("apReference"), phaseMin, Max(phaseMax, pEntryDef->m_clearMotionEndPhase));

		const StringId64 apPivotChannelId = GetApPivotChannelId(pAnim, pEntryDef);
		if (apPivotChannelId != SID("apReference"))
		{
			g_animAlignCache.TryCacheAnim(pAnim, apPivotChannelId, phaseMin, Max(phaseMax, pEntryDef->m_clearMotionEndPhase));

			Locator pivotLocLs, apLocLs;
			bool valid = true;
			valid = valid && EvaluateChannelInAnim(charState.m_skelId, pAnim, apPivotChannelId, 0.0f, &pivotLocLs);
			valid = valid && EvaluateChannelInAnim(charState.m_skelId, pAnim, SID("apReference"), 0.0f, &apLocLs);

			if (valid)
			{
				apToPivotDeltaLs = apLocLs.UntransformLocator(pivotLocLs);
				pivotToApDeltaLs = pivotLocLs.UntransformLocator(apLocLs);

				defaultFittingApRef.SetLocatorPs(apRef.GetLocatorPs().TransformLocator(apToPivotDeltaLs));
			}
		}

		alignToUsePs = ConstructedRotatedAlignForEntryPs(charState,
														 hAnim,
														 pEntryDef,
														 defaultFittingApRef,
														 phaseMin,
														 phaseMax,
														 mirrored);
		Vector defaultEntryVelocityPs = kZero;

		const bool defEntryAlignValid = ConstructDefaultEntryAlignPs(charState,
																	 alignToUsePs,
																	 hAnim,
																	 pEntryDef,
																	 defaultFittingApRef,
																	 phaseMin,
																	 phaseMax,
																	 mirrored,
																	 &defaultAlignPs,
																	 &startPhase,
																	 &defaultEntryVelocityPs);

		if (!defEntryAlignValid)
		{
			return false;
		}

		const bool rotEntryValid = ConstructRotatedEntryAndApRef(charState,
																 alignToUsePs,
																 hAnim,
																 pEntryDef,
																 defaultFittingApRef,
																 defaultAlignPs,
																 defaultEntryVelocityPs,
																 &rotatedAlignPs,
																 &rotatedApRef,
																 &rotatedEntryVelocityPs);
		if (!rotEntryValid)
		{
			return false;
		}

		if (apPivotChannelId != SID("apReference"))
		{
			rotatedApRef.SetLocatorPs(rotatedApRef.GetLocatorPs().TransformLocator(pivotToApDeltaLs));
		}
	}

	const Point rotatedAlignPosWs = rotatedApRef.GetParentSpace().TransformPoint(rotatedAlignPs.Pos());

	AvailableEntry newEntry;

	newEntry.m_anim				= hAnim;
	newEntry.m_resolvedAnimId	= pAnim->GetNameId();
	newEntry.m_charAlignUsedPs	= alignToUsePs;
	newEntry.m_rotatedApRef		= rotatedApRef;
	newEntry.m_sbApRef			= apRef;
	newEntry.m_entryAlignPs		= rotatedAlignPs;
	newEntry.m_entryVelocityPs	= VectorXz(rotatedEntryVelocityPs);
	newEntry.m_pDcDef			= pEntryDef;
	newEntry.m_distToEntryXz	= DistXz(charState.m_frame.GetTranslation(), rotatedAlignPosWs);
	newEntry.m_errorFlags.m_all	= 0;
	newEntry.m_phase			= charState.m_forceDefaultEntry ? phaseMin : startPhase;
	newEntry.m_phaseMin			= phaseMin;
	newEntry.m_phaseMax			= phaseMax;
	newEntry.m_playMirrored		= mirrored;

	*pEntryOut = newEntry;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::FlagInvalidEntries(const CharacterState& charState,
								 const GatherMode gatherMode,
								 const BoundFrame& apRef,
								 AvailableEntry* pEntries,
								 const U32F numEntries)
{
	FlagInvalidMotionTypeEntries(charState, pEntries, numEntries);

	FlagWrongDemeanorEntries(charState, pEntries, numEntries);

	FlagWrongWeaponTypeEntries(charState, pEntries, numEntries);

	if (gatherMode != GatherMode::kDefault)
	{
		FlagInvalidDistEntries(charState, apRef, pEntries, numEntries);

		// Remove entries that would result in a too acute movement angle change.
		FlagInvalidAngleEntries(charState, pEntries, numEntries);

		// Remove entries that have obstructions between them and the AP.
		// DO THIS LAST as it is the most expensive
		FlagNoClearLineOfMotionEntries(charState, apRef, pEntries, numEntries);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApEntry::FilterInvalidEntries(AvailableEntry* pEntries, const U32F numEntries)
{
	PROFILE(Animation, FilterInvalidEntries);

	U32F numUsableEntries = numEntries;

	for (U32F i = 0; i < numUsableEntries;)
	{
		const AvailableEntry& currentEntry = pEntries[i];

		// Discard entry if any error flag is set
		if (currentEntry.m_errorFlags.m_all)
		{
			pEntries[i] = pEntries[numUsableEntries - 1];
			--numUsableEntries;
		}
		else
		{
			++i;
		}
	}

	return numUsableEntries;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApEntry::CountUsableEntries(const AvailableEntry* pEntries, const U32F numEntries)
{
	U32F numUsableEntries = numEntries;

	for (U32F i = 0; i < numEntries; ++i)
	{
		const AvailableEntry& currentEntry = pEntries[i];
		if (currentEntry.m_errorFlags.m_all)
		{
			--numUsableEntries;
		}
	}

	return numUsableEntries;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApEntry::EntryWithLeastRotation(const AvailableEntry* pEntries,
									 const U32F numEntries,
									 const BoundFrame& defaultApRef)
{
	if (!pEntries || 0 == numEntries)
		return 0;

	float largestDot = -1.0f;
	I32F bestIndex = -1;

	const Quat defaultRotWs = defaultApRef.GetRotationWs();

	for (U32F i = 0; i < numEntries; ++i)
	{
		const Quat rotatedRotWs = pEntries[i].m_rotatedApRef.GetRotationWs();

		const float dotP = Dot(defaultRotWs, rotatedRotWs);

		if (dotP > largestDot)
		{
			bestIndex = i;
			largestDot = dotP;
		}
	}

	if (bestIndex < 0)
		return 0;

	return bestIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApEntry::EntryWithLeastApproachRotation(const AvailableEntry* pEntries,
											 const U32F numEntries,
											 const Locator& alignPs)
{
	if (!pEntries || 0 == numEntries)
		return 0;

	float largestDot = -1.0f;
	I32F bestIndex = -1;

	const Quat alignRotPs = alignPs.Rot();

	for (U32F i = 0; i < numEntries; ++i)
	{
		const Locator entryLocPs = pEntries[i].m_entryAlignPs;
		const Vector approachDirPs = AsUnitVectorXz(entryLocPs.Pos() - alignPs.Pos(), kZero);
		const Vector entryDirPs = AsUnitVectorXz(pEntries[i].m_entryVelocityPs, GetLocalZ(entryLocPs.Rot()));
		const float dotP = Dot(entryDirPs, approachDirPs);

		if (dotP > largestDot)
		{
			bestIndex = i;
			largestDot = dotP;
		}
	}

	if (bestIndex < 0)
		return 0;

	return bestIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ApEntry::ClosestEntry(const AvailableEntry* pEntries, const U32F numEntries, const Locator& alignPs)
{
	if (!pEntries || 0 == numEntries)
		return 0;

	float closestDist = kLargeFloat;
	I32F bestIndex = -1;

	const Point alignPosPs = alignPs.Pos();

	for (U32F i = 0; i < numEntries; ++i)
	{
		const Locator entryLocPs = pEntries[i].m_entryAlignPs;
		const float dist = DistXz(entryLocPs.Pos(), alignPosPs);

		if (dist < closestDist)
		{
			bestIndex = i;
			closestDist = dist;
		}
	}

	if (bestIndex < 0)
		return 0;

	return bestIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::FlagWrongDemeanorEntries(const CharacterState& charState, AvailableEntry* pEntries, U32F numEntries)
{
	const U32F currentDemeanor = charState.m_dcDemeanorValue;
	const U32F currentDemeanorMask = (1ULL << currentDemeanor);

	for (U32F i = 0; i < numEntries; ++i)
	{
		AvailableEntry& currentEntry = pEntries[i];

		if (currentEntry.m_errorFlags.m_all)
			continue;

		if (currentEntry.m_pDcDef->m_validDemeanors == 0)
			continue;

		if ((currentEntry.m_pDcDef->m_validDemeanors & currentDemeanorMask) == 0)
		{
			currentEntry.m_errorFlags.m_wrongDemeanor = true;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::FlagWrongWeaponTypeEntries(const CharacterState& charState, AvailableEntry* pEntries, U32F numEntries)
{
	if (charState.m_dcWeaponAnimTypeMask == 0)
	{
		// don't flag for invalid weapon type unless we explicitly ask for it
		return;
	}

	bool hasExplicitMatch = false;

	for (U32F i = 0; i < numEntries; ++i)
	{
		const AvailableEntry& currentEntry = pEntries[i];

		if (!currentEntry.m_pDcDef || currentEntry.m_pDcDef->m_validWeapons == 0)
			continue;

		if ((currentEntry.m_pDcDef->m_validWeapons & charState.m_dcWeaponAnimTypeMask) != 0)
		{
			hasExplicitMatch = true;
			break;
		}
	}

	for (U32F i = 0; i < numEntries; ++i)
	{
		AvailableEntry& currentEntry = pEntries[i];

		if (!currentEntry.m_pDcDef)
			continue;

		if (currentEntry.m_pDcDef->m_validWeapons == 0)
		{
			if (hasExplicitMatch)
			{
				currentEntry.m_errorFlags.m_wrongWeaponType = true;
			}
		}
		else if ((currentEntry.m_pDcDef->m_validWeapons & charState.m_dcWeaponAnimTypeMask) == 0)
		{
			currentEntry.m_errorFlags.m_wrongWeaponType = true;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::FlagDistanceCategoryEntries(const CharacterState& charState,
										  bool useRangedEntries,
										  const BoundFrame& apRef,
										  AvailableEntry* pEntries,
										  U32F numEntries)
{
	PROFILE(Animation, FlagDistanceCategoryEntries);

	const Scalar kMaxCloseEntryDistSq = charState.m_rangedEntryRadius * charState.m_rangedEntryRadius;
	const Point apPosPs = apRef.GetTranslationPs();

	for (U32F i = 0; i < numEntries; ++i)
	{
		AvailableEntry& currentEntry = pEntries[i];

		if (currentEntry.m_errorFlags.m_all)
			continue;

		const bool closeEntry = LengthSqr(currentEntry.m_entryAlignPs.Pos() - apPosPs) < kMaxCloseEntryDistSq;
		if ((closeEntry && useRangedEntries) || (!closeEntry && !useRangedEntries))
		{
			currentEntry.m_errorFlags.m_wrongDistanceCat = true;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::FlagInvalidMotionTypeEntries(const CharacterState& charState, AvailableEntry* pEntries, U32F numEntries)
{
	PROFILE(Animation, FlagInvalidMotionTypeEntries);

	const I32F motionTypeMask = (1U << charState.m_motionType);

	bool hasExplicitMtSubcatMatch = false;

	if (charState.m_mtSubcategory != INVALID_STRING_ID_64 && charState.m_mtSubcategory != SID("normal"))
	{
		for (U32F i = 0; i < numEntries; ++i)
		{
			AvailableEntry& currentEntry = pEntries[i];

			if (currentEntry.m_errorFlags.m_all)
				continue;

			const StringId64 mtSubcategory = currentEntry.m_pDcDef->m_mtSubcategory;
			if (mtSubcategory == charState.m_mtSubcategory)
			{
				hasExplicitMtSubcatMatch = true;
				break;
			}
		}
	}

	for (U32F i = 0; i < numEntries; ++i)
	{
		AvailableEntry& currentEntry = pEntries[i];

		if (currentEntry.m_errorFlags.m_all)
			continue;

		const I32 validMotions		   = currentEntry.m_pDcDef->m_validMotions;
		const StringId64 mtSubcategory = currentEntry.m_pDcDef->m_mtSubcategory;

		if ((validMotions != 0) && (validMotions & motionTypeMask) == 0)
		{
			currentEntry.m_errorFlags.m_wrongMotionType = true;
		}

		if (hasExplicitMtSubcatMatch)
		{
			if (charState.m_mtSubcategory != mtSubcategory)
			{
				currentEntry.m_errorFlags.m_wrongMotionSubCategory = true;
			}
		}
		else if (mtSubcategory != SID("normal"))
		{
			currentEntry.m_errorFlags.m_wrongMotionSubCategory = true;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::FlagInvalidDistEntries(const CharacterState& charState,
									 const BoundFrame& apRef,
									 AvailableEntry* pEntries,
									 U32F numEntries)
{
	const Locator& parentSpace = charState.m_frame.GetParentSpace();
	const Point charPosPs = charState.m_frame.GetTranslationPs();
	const Point apRefPosPs = parentSpace.UntransformPoint(apRef.GetTranslationWs());

	const float distToAp = DistXz(charPosPs, apRefPosPs);

	for (U32F i = 0; i < numEntries; ++i)
	{
		AvailableEntry& currentEntry = pEntries[i];

		if (currentEntry.m_errorFlags.m_all)
			continue;

		const float minDist = currentEntry.m_pDcDef->m_distMin;
		const float maxDist = currentEntry.m_pDcDef->m_distMax;

		if ((minDist >= 0.0f) && (distToAp < minDist))
		{
			currentEntry.m_errorFlags.m_invalidEntryDist = true;
			continue;
		}

		if ((maxDist >= 0.0f) && (distToAp > maxDist))
		{
			currentEntry.m_errorFlags.m_invalidEntryDist = true;
			continue;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::FlagInvalidAngleEntries(const CharacterState& charState, AvailableEntry* pEntries, U32F numEntries)
{
	PROFILE(Animation, FlagInvalidAngleEntries);

	for (U32F i = 0; i < numEntries; ++i)
	{
		AvailableEntry& currentEntry = pEntries[i];

		if (currentEntry.m_errorFlags.m_all)
			continue;

		const U32 entryFlags = currentEntry.m_pDcDef->m_flags;
		const bool forcesStop = 0 != (entryFlags & DC::kApEntryItemFlagsForceStop);
		const bool checkFacingDir = 0 == (entryFlags & DC::kApEntryItemFlagsIgnoreFacingDirection);
		const bool checkEntryAngle = 0 == (entryFlags & DC::kApEntryItemFlagsIgnoreEntryAngle);

		if (forcesStop)
			continue;

		if (checkEntryAngle)
		{
			if (!IsEntryAngleValid(charState, currentEntry))
			{
				currentEntry.m_errorFlags.m_invalidEntryAngle = true;
			}
		}

		if (checkFacingDir)
		{
			if (!IsFacingDirectionValidPs(charState, currentEntry))
			{
				currentEntry.m_errorFlags.m_invalidFacingDir = true;
			}
		}

		if (!IsEntrySpeedValid(charState, currentEntry))
		{
			currentEntry.m_errorFlags.m_speedMismatch = true;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::FlagNoClearLineOfMotionEntries(const CharacterState& charState,
											 const BoundFrame& finalApRef,
											 AvailableEntry* pEntries,
											 U32F numEntries)
{
	PROFILE(Animation, FlagNoClearLineOfMotionEntries);

	for (U32F i = 0; i < numEntries; ++i)
	{
		AvailableEntry& currentEntry = pEntries[i];

		if (currentEntry.m_errorFlags.m_all)
			continue;

		if (currentEntry.m_pDcDef->m_flags & DC::kApEntryItemFlagsNoClearMotionTest)
			continue;

		// Look for clear line of motion from the entry to a custom test point to check for objects blocking the entry
		if (!IsEntryMotionClearOnNav(charState, finalApRef, currentEntry))
		{
			currentEntry.m_errorFlags.m_noClearLineOfMotion = true;
			continue;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntry::IsEntryAngleValid(const CharacterState& charState, const AvailableEntry& entry)
{
	if (entry.m_distToEntryXz <= charState.m_entryAngleCheckDist)
	{
		return true;
	}

	const Locator& alignToUsePs = entry.m_charAlignUsedPs;
	const Locator& entryLocPs = entry.m_entryAlignPs;
	const Vector entryVecPs = SafeNormalize(entry.m_entryVelocityPs, GetLocalZ(entryLocPs.Rot()));

	const float kMaxEntryToApCosAngle = Cos(DEGREES_TO_RADIANS(charState.m_maxEntryErrorAngleDeg));

	const Point curPosPs = alignToUsePs.Pos();
	const Vector curFacingPs = GetLocalZ(alignToUsePs.Rot());

	// if we are right on top of the starting align of the enter animation (which can happen if we're in the middle of the phase range)
	// then just use our current align z as our "approach" vector to compare entry angles instead of trying to normalize a near-zero vector
	const Vector curApproachPs = AsUnitVectorXz(entryLocPs.Pos() - curPosPs, curFacingPs);

	const float entryCosAngle = Dot(curApproachPs, entryVecPs);

	// If the direction to the entry and the direction from the entry to AP are
	// somewhat aligned we consider it a valid entry.
	if (entryCosAngle > kMaxEntryToApCosAngle)
	{
		return true;
	}
	else
	{
		return false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntry::IsFacingDirectionValidPs(const CharacterState& charState, const AvailableEntry& entry)
{
	const Locator& alignToUsePs = entry.m_charAlignUsedPs;
	const Locator& entryAlignPs = entry.m_entryAlignPs;

	const float kMaxFacingDiffCosAngle = Cos(DEGREES_TO_RADIANS(charState.m_maxFacingDiffAngleDeg));

	// Find the angle between the current npc facing direction and the facing direction
	// on the first frame in the entry animation.
	const Vector npcFwdPs = GetLocalZ(alignToUsePs.Rot());
	const Vector entryFwdPs = GetLocalZ(entryAlignPs.Rot());
	const Scalar fwdCosAngle = DotXz(npcFwdPs, entryFwdPs);

	// If the direction to the entry and the direction from the entry to AP are
	// somewhat aligned we consider it a valid entry.
	if (fwdCosAngle > kMaxFacingDiffCosAngle)
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntry::IsEntryMotionClearOnNav(const CharacterState& charState,
									  const BoundFrame& finalApRef,
									  const AvailableEntry& entry,
									  bool debugDraw /* = false */)
{
	PROFILE_AUTO(Animation);

	if (!entry.m_pDcDef || (INVALID_STRING_ID_64 == entry.m_pDcDef->m_animName))
		return false;

	const AnimControl* pAnimControl		= charState.m_pAnimControl;
	const SimpleNavControl* pNavControl = charState.m_pNavControl;

	if (!pNavControl || !pAnimControl)
		return false;

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const NavMesh* pNavMesh = charState.m_pNavControl->GetNavMesh();
	if (!pNavMesh)
		return false;

	const Locator& parentSpace	 = pNavMesh->GetParentSpace();
	const Locator rotatedApRefWs = entry.m_rotatedApRef.GetLocatorWs();
	const Locator finalApRefWs	 = finalApRef.GetLocatorWs();
	const ArtItemAnim* pAnim	 = pAnimControl->LookupAnim(entry.m_pDcDef->m_animName).ToArtItem();

	if (!pAnim)
		return false;

	const bool mirror	 = entry.m_playMirrored;
	const F32 startPhase = entry.m_phase;
	const F32 endPhase	 = charState.m_testClearMotionToPhaseMax ? entry.m_phaseMax
															     : entry.m_pDcDef->m_clearMotionEndPhase;
	const F32 phaseStep  = pAnim->m_pClipData->m_phasePerFrame;
	F32 evalPhase		 = startPhase;

	Locator finalLocWs;
	if (!FindAlignFromApReferenceCached(charState.m_skelId,
										pAnim,
										endPhase,
										finalApRefWs,
										SID("apReference"),
										mirror,
										&finalLocWs))
	{
		return false;
	}

	Locator prevLocWs = charState.m_frame.GetParentSpace().TransformLocator(entry.m_charAlignUsedPs);
	const Vector vo	  = Vector(0.0f, 0.15f, 0.0f);

	bool valid = true;

	const DC::SelfBlendParams* pSelfBlend = entry.m_pDcDef->m_selfBlend;
	const F32 animDuration		  = pAnim->m_pClipData->m_fNumFrameIntervals * pAnim->m_pClipData->m_secondsPerFrame;
	const bool doSelfBlend		  = pSelfBlend && (pSelfBlend->m_phase >= 0.0f);
	const F32 selfBlendStartPhase = doSelfBlend ? Max(pSelfBlend->m_phase, entry.m_phase) : -1.0f;
	const F32 selfBlendEndPhase	  = doSelfBlend ? Limit01(selfBlendStartPhase + pSelfBlend->m_time / animDuration)
											    : -1.0f;
	U32F index = 0;

	const bool closeButNotTooClose = entry.m_distToEntryXz < 7.5f && entry.m_distToEntryXz > 0.5f;

	const F32 probeRadius		 = charState.m_testClearMotionRadial ? pNavControl->GetMaximumNavAdjustRadius() : 0.0f;
	const F32 initialProbeRadius = (closeButNotTooClose && charState.m_testClearMotionInitRadial)
									   ? pNavControl->GetEffectivePathFindRadius()
									   : 0.0f;

	NavMesh::ProbeParams probeParams;

	probeParams.m_pStartPoly	 = pNavMesh->FindContainingPolyPs(pNavMesh->WorldToParent(prevLocWs.Pos()));
	probeParams.m_obeyedBlockers = pNavControl->BuildObeyedBlockerList(true);
	probeParams.m_dynamicProbe	 = true;
	probeParams.m_probeRadius	 = initialProbeRadius;

	do
	{
		Locator nextLocWs = prevLocWs;

		const F32 selfBlendProgressLinear = doSelfBlend
												? LerpScale(selfBlendStartPhase, selfBlendEndPhase, 0.0f, 1.0f, evalPhase)
												: 0.0f;
		const F32 selfBlendValue = doSelfBlend ? CalculateCurveValue(selfBlendProgressLinear, pSelfBlend->m_curve)
											   : 0.0f;
		const Locator apRefWs = doSelfBlend ? Lerp(rotatedApRefWs, finalApRefWs, selfBlendValue) : rotatedApRefWs;

		if (!FindAlignFromApReferenceCached(charState.m_skelId,
											pAnim,
											evalPhase,
											apRefWs,
											SID("apReference"),
											mirror,
											&nextLocWs))
		{
			return false;
		}

		evalPhase += phaseStep;

		if (DistXz(nextLocWs.Pos(), prevLocWs.Pos()) < probeRadius * 2.0f)
		{
			continue;
		}

		Color lineClr = kColorGreen;

		if (charState.m_manualObstructionWs.GetRadius() > 0.0f)
		{
			Point closestPosWs = kOrigin;
			const F32 obsDist  = DistPointSegment(charState.m_manualObstructionWs.GetCenter(),
												  prevLocWs.Pos(),
												  nextLocWs.Pos(),
												  &closestPosWs);
			if (obsDist <= charState.m_manualObstructionWs.GetRadius())
			{
				valid	= false;
				lineClr = kColorOrange;

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					g_prim.Draw(DebugCross(closestPosWs, 0.1f, kColorOrange), kPrimDuration1FramePauseable);
					g_prim.Draw(DebugLine(closestPosWs, charState.m_manualObstructionWs.GetCenter(), kColorOrange),
								kPrimDuration1FramePauseable);
					g_prim.Draw(DebugSphere(charState.m_manualObstructionWs, kColorOrange),
								kPrimDuration1FramePauseable);
				}
				else
				{
					return false;
				}
			}
		}

		probeParams.m_start = pNavMesh->WorldToLocal(prevLocWs.Pos());
		probeParams.m_move	= pNavMesh->WorldToLocal(nextLocWs.Pos() - prevLocWs.Pos());

		const NavMesh::ProbeResult res = pNavMesh->ProbeLs(&probeParams);

		const Point endPointWs	 = pNavMesh->LocalToWorld(probeParams.m_endPoint);
		const Point nextYPointWs = doSelfBlend ? Lerp(endPointWs, nextLocWs.Pos(), selfBlendValue) : endPointWs;
		nextLocWs.SetPos(PointFromXzAndY(nextLocWs.Pos(), nextYPointWs));

		probeParams.m_pStartPoly = probeParams.m_pHitPoly;

		if (probeParams.m_pStartPoly && probeParams.m_pStartPoly->GetNavMeshHandle().GetManagerId() != pNavMesh->GetManagerId())
		{
			pNavMesh = probeParams.m_pStartPoly->GetNavMesh();
		}

		if (res != NavMesh::ProbeResult::kReachedGoal)
		{
			probeParams.m_pStartPoly = nullptr;

			valid	= false;
			lineClr = kColorRed;
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugString(prevLocWs.Pos() + vo, StringBuilder<128>("%d", int(index)).c_str(), lineClr, 0.5f),
						kPrimDuration1FramePauseable);

			if (probeParams.m_probeRadius > 0.0f)
			{
				DebugDrawFlatCapsule(prevLocWs.Pos() + vo,
									 nextLocWs.Pos() + vo,
									 kUnitYAxis,
									 probeParams.m_probeRadius,
									 lineClr,
									 PrimAttrib(),
									 kPrimDuration1FramePauseable);
			}
			else
			{
				g_prim.Draw(DebugLine(prevLocWs.Pos() + vo, nextLocWs.Pos() + vo, lineClr, lineClr, 4.0f),
							kPrimDuration1FramePauseable);
			}
		}
		else if (!valid)
		{
			return false;
		}

		prevLocWs = nextLocWs;
		probeParams.m_probeRadius = probeRadius;

		++index;

	} while (evalPhase < endPhase);

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		g_prim.Draw(DebugString(finalLocWs.Pos() + vo, "F", kColorWhite, 0.75f), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(prevLocWs.Pos() + vo, finalLocWs.Pos() + vo, kColorGreen, kColorWhite, 4.0f),
					kPrimDuration1FramePauseable);
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntry::IsEntrySpeedValid(const CharacterState& charState, const AvailableEntry& entry)
{
	float minSpeed = entry.m_pDcDef->m_minSpeed;

	if (entry.m_pDcDef->m_minSpeedCurve)
	{
		minSpeed = NdUtil::EvaluatePointCurve(entry.m_phase, entry.m_pDcDef->m_minSpeedCurve);
	}

	if (minSpeed < 0.0f)
		return true;

	const Vector normEntryVec = SafeNormalize(entry.m_entryVelocityPs, kUnitZAxis);
	const float projSpeed = DotXz(charState.m_velocityPs, normEntryVec);
	if (projSpeed >= minSpeed)
		return true;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ApEntry::IsEntryDistValid(const CharacterState& charState, const AvailableEntry& entry)
{
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 ApEntry::GetApPivotChannelId(const ArtItemAnim* pAnim, const DC::ApEntryItem* pEntryDef)
{
	if (!pAnim || !pEntryDef || (pEntryDef->m_apPivotId == SID("apReference")))
	{
		return SID("apReference");
	}

	if (AnimHasChannel(pAnim, pEntryDef->m_apPivotId))
	{
		return pEntryDef->m_apPivotId;
	}

	return SID("apReference");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::DebugDrawEntryRange(const CharacterState& charState,
								  const BoundFrame& apRef,
								  const AvailableEntry& entry,
								  const Color clr)
{
	STRIP_IN_FINAL_BUILD;

	const DC::ApEntryItem* pEntryDef = entry.m_pDcDef;

	static const U32F kNumSteps = 15;

	const Vector vAdjust		 = Vector(kUnitYAxis) * 0.1f;
	const float initialAngle	 = -DEGREES_TO_RADIANS(pEntryDef->m_rangeAngleCw);
	const float finalAngle		 = DEGREES_TO_RADIANS(pEntryDef->m_rangeAngleCcw);
	const Transform initialXfrm	 = Transform(kUnitYAxis, initialAngle);
	const Transform finalXfrm	 = Transform(kUnitYAxis, finalAngle);
	const Locator initialXfrmLoc = Locator(initialXfrm);
	const Locator finalXfrmLoc	 = Locator(finalXfrm);
	const Locator apRefWs		 = apRef.GetLocatorWs();
	const bool mirror			 = entry.m_playMirrored;

	if (!charState.m_pAnimControl)
		return;

	const ArtItemAnim* pAnim = charState.m_pAnimControl->LookupAnim(pEntryDef->m_animName).ToArtItem();
	if (!pAnim)
		return;

	Locator startAlignWs, endAlignWs;

	if (!FindAlignFromApReferenceCached(charState.m_skelId,
										pAnim,
										entry.m_phaseMin,
										apRefWs,
										SID("apReference"),
										mirror,
										&startAlignWs))
	{
		return;
	}

	if (!FindAlignFromApReferenceCached(charState.m_skelId,
										pAnim,
										entry.m_phaseMax,
										apRefWs,
										SID("apReference"),
										mirror,
										&endAlignWs))
	{
		return;
	}

	//const Locator startAlignLs = apRefWs.UntransformLocator(startAlignWs);
	//const Locator endAlignLs = apRefWs.UntransformLocator(endAlignWs);

	DrawDebugFlatArc(apRefWs, startAlignWs.Pos(), pEntryDef->m_rangeAngleCw, clr, 3.0f);
	DrawDebugFlatArc(apRefWs, endAlignWs.Pos(), pEntryDef->m_rangeAngleCcw, clr, 3.0f);

	const float phaseStep = /*(1.0f / 30.0f) * pAnim->m_pClipData->m_framesPerSecond * */pAnim->m_pClipData->m_phasePerFrame;

	Locator prevLocWs = startAlignWs;

	float prevP = entry.m_phaseMin;
	float p = Limit(prevP + phaseStep, entry.m_phaseMin, entry.m_phaseMax);

	while (prevP != p)
	{
		Locator locWs;
		FindAlignFromApReferenceCached(charState.m_skelId, pAnim, p, apRefWs, SID("apReference"), mirror, &locWs);

		const Locator prevLocLs		= apRefWs.UntransformLocator(prevLocWs);
		const Locator prevInitialLs	= initialXfrmLoc.TransformLocator(prevLocLs);
		const Locator prevFinalLs	= finalXfrmLoc.TransformLocator(prevLocLs);

		const Locator prevInitialWs	= apRefWs.TransformLocator(prevInitialLs);
		const Locator prevFinalWs	= apRefWs.TransformLocator(prevFinalLs);

		const Locator locLs		= apRefWs.UntransformLocator(locWs);
		const Locator initialLs	= initialXfrmLoc.TransformLocator(locLs);
		const Locator finalLs	= finalXfrmLoc.TransformLocator(locLs);

		const Locator initialWs = apRefWs.TransformLocator(initialLs);
		const Locator finalWs = apRefWs.TransformLocator(finalLs);

		g_prim.Draw(DebugLine(prevLocWs.Pos() + vAdjust, locWs.Pos() + vAdjust, clr));
		g_prim.Draw(DebugLine(prevInitialWs.Pos() + vAdjust, initialWs.Pos() + vAdjust, clr, 3.0f));
		g_prim.Draw(DebugLine(prevFinalWs.Pos() + vAdjust, finalWs.Pos() + vAdjust, clr, 3.0f));

		prevLocWs = locWs;

		prevP = p;
		p = Limit(p + phaseStep, entry.m_phaseMin, entry.m_phaseMax);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Color ApEntry::AvailableEntry::GetDebugColor() const
{
	STRIP_IN_FINAL_BUILD_VALUE(kColorGreen);

	const Color color = m_errorFlags.m_wrongDistanceCat			? kColorMagenta		:
						m_errorFlags.m_invalidFacingDir			? kColorYellow		:
						m_errorFlags.m_invalidEntryAngle		? kColorOrange		:
						m_errorFlags.m_notOnNav					? kColorGray		:
						m_errorFlags.m_noClearLineOfMotion		? kColorRed			:
						m_errorFlags.m_invalidEntryDist			? kColorBlue		:
						m_errorFlags.m_speedMismatch			? kColorCyan		:
						m_errorFlags.m_wrongMotionType			? kColorPurple		:
						m_errorFlags.m_wrongMotionSubCategory	? kColorPink		:
						m_errorFlags.m_wrongDemeanor			? kColorMagenta		:
						m_errorFlags.m_wrongWeaponType			? kColorMagenta		:
						kColorGreen;
	return color;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::AvailableEntry::DebugDraw(const CharacterState& cs, const ActionPack& ap, const BoundFrame& apRef) const
{
	STRIP_IN_FINAL_BUILD;

	const Vector yOffset(0.0f, 0.05f, 0.0f);

	const Point npcPosPs		= cs.m_frame.GetTranslationPs();
	const Quat npcRotPs			= cs.m_frame.GetRotationPs();
	const Locator entryLocPs	= m_entryAlignPs;
	const Point entryPosPs		= entryLocPs.Pos();
	const Vector toEntryXz		= VectorXz(entryPosPs - npcPosPs);
	const Vector normEntryVecPs	= SafeNormalize(m_entryVelocityPs, kZero);
	const float entrySpeed		= Length(m_entryVelocityPs);
	const float charSpeed		= Length(cs.m_velocityPs);
	const float projSpeed		= Dot(cs.m_velocityPs, normEntryVecPs);
	const Vector entryFwPs		= GetLocalZ(m_entryAlignPs.Rot());
	const Vector currentFwPs	= GetLocalZ(npcRotPs);
	const float cosAngle		= Dot(entryFwPs, currentFwPs);
	const float angleDeg		= RADIANS_TO_DEGREES(SafeAcos(cosAngle));
	const Vector entryVecPs		= SafeNormalize(m_entryVelocityPs, kZero);
	const Vector curApproachPs	= m_distToEntryXz > cs.m_entryAngleCheckDist ? SafeNormalize(toEntryXz, currentFwPs)
																			: entryVecPs;
	const float entryAngleDeg	= RADIANS_TO_DEGREES(SafeAcos(Dot(curApproachPs, entryVecPs)));

	const I32 validMotions			= m_pDcDef->m_validMotions;
	const StringId64 mtSubcategory	= m_pDcDef->m_mtSubcategory;

	const char* csMtSubcatMsg = cs.m_mtSubcategory == INVALID_STRING_ID_64
									? "<none>"
									: DevKitOnly_StringIdToString(cs.m_mtSubcategory);
	const char* csMtTypeMsg = cs.m_motionType == kMotionTypeWalk   ? "walk"   :
							  cs.m_motionType == kMotionTypeRun    ? "run"    :
							  cs.m_motionType == kMotionTypeSprint ? "sprint" :
							  "<idle>";
	const char* mtSubCatMsg = DevKitOnly_StringIdToString(mtSubcategory);
	StringBuilder<64> mtTypesMsg("%s%s%s%s ",
								 (validMotions & (1U << kMotionTypeWalk)) ? " walk" : "",
								 (validMotions & (1U << kMotionTypeRun)) ? " run" : "",
								 (validMotions & (1U << kMotionTypeSprint)) ? " sprint" : "",
								 validMotions == 0 ? " idle" : "");

	StringBuilder<256> desc;
	desc.append_format("%s @ %0.2f\n", DevKitOnly_StringIdToStringOrHex(m_pDcDef->m_animName), m_phase);

	float minSpeed = m_pDcDef->m_minSpeed;

	if (m_pDcDef->m_minSpeedCurve)
	{
		minSpeed = NdUtil::EvaluatePointCurve(m_phase, m_pDcDef->m_minSpeedCurve);
	}

	const Point apRefPosPs = cs.m_frame.GetParentSpace().UntransformPoint(apRef.GetTranslationWs());
	const float distToAp = DistXz(npcPosPs, apRefPosPs);

	if (m_errorFlags.m_noClearLineOfMotion)
		desc.append_format("NoClearLineOfMotion ");
	if (m_errorFlags.m_notOnNav)
		desc.append_format("NotOnNav ");
	if (m_errorFlags.m_invalidEntryAngle)
		desc.append_format("InvalidEntryAngle %.1fdeg ", entryAngleDeg);
	if (m_errorFlags.m_invalidEntryDist)
		desc.append_format("InvalidEntryDist %0.3fm", distToAp);
	if (m_errorFlags.m_invalidFacingDir)
		desc.append_format("InvalidFacingDir %.1fdeg ", angleDeg);
	if (m_errorFlags.m_wrongDistanceCat)
		desc.append_format("WrongDistanceCat ");
	if (m_errorFlags.m_speedMismatch)
		desc.append_format("SpeedMismatch [proj: %0.2f m/s < min: %0.2f m/s, char: %0.2f m/s] ", projSpeed, minSpeed, charSpeed);
	if (m_errorFlags.m_wrongMotionType)
		desc.append_format("WrongMotionType %s (%s) ", csMtTypeMsg, mtTypesMsg.c_str());
	if (m_errorFlags.m_wrongMotionSubCategory)
		desc.append_format("WrongMotionSubCategory %s (%s) ", csMtSubcatMsg, mtSubCatMsg);
	if (m_errorFlags.m_wrongDemeanor)
		desc.append_format("WrongDemeanor ");
	if (m_errorFlags.m_wrongWeaponType)
		desc.append_format("WrongWeaponType ");

	Locator yOffsetEntryLocPs = entryLocPs;
	yOffsetEntryLocPs.SetPos(entryPosPs + yOffset);

	const Color color = GetDebugColor();

	PrimServerWrapper ps = PrimServerWrapper(cs.m_frame.GetParentSpace());
	ps.DrawString(m_entryAlignPs.Pos() + yOffset, desc.c_str(), color, g_msgOptions.m_conScale);
	ps.DrawCoordAxes(yOffsetEntryLocPs, 0.2f);
	ps.DrawArrow(entryPosPs + yOffset, entryVecPs, 0.5f, color);

	if (m_errorFlags.m_invalidEntryAngle)
	{
		ps.DrawArrow(entryPosPs + yOffset, curApproachPs, 0.5f, kColorWhiteTrans);
	}

	if (m_errorFlags.m_noClearLineOfMotion)
	{
		ApEntry::IsEntryMotionClearOnNav(cs, apRef, *this, true);
	}

	if (m_errorFlags.m_speedMismatch)
	{
		g_prim.Draw(DebugArrow(entryPosPs + yOffset, normEntryVecPs, color));
	}

	if (m_pDcDef->m_flags & DC::kApEntryItemFlagsDefaultEntry)
	{
		const float apEntryDist			= cs.m_pNavControl->GetMaximumNavAdjustRadius();
		const Point defaultEntryPosWs	= ap.GetDefaultEntryPointWs(apEntryDist);

		g_prim.Draw(DebugSphere(Sphere(entryPosPs, 0.05f), kColorOrange));
		g_prim.Draw(DebugString(entryPosPs, "default-entry", kColorOrange, g_msgOptions.m_conScale));
		g_prim.Draw(DebugArrow(entryPosPs, defaultEntryPosWs, kColorOrange, 0.25f));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::CharacterState::InitCommon(const NavCharacter* pNavChar)
{
	if (!pNavChar)
		return;

	m_frame		= pNavChar->GetBoundFrame();
	m_moving	= pNavChar->IsMoving();
	m_skelId	= pNavChar->GetSkeletonId();

	m_pAnimControl	= pNavChar->GetAnimControl();
	m_pNavControl	= pNavChar->GetNavControl();
	m_motionType	= pNavChar->GetCurrentMotionType();
	m_mtSubcategory = pNavChar->GetRequestedMtSubcategory();
	m_velocityPs	= pNavChar->GetVelocityPs();

	m_dcDemeanorValue = pNavChar->GetCurrentDemeanor().ToI32();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ApEntry::CharacterState::InitCommon(const NavCharacterAdapter pNavChar)
{
	const NdGameObject* pCharacter = pNavChar->ToGameObject();

	if (!pCharacter)
		return;

	m_frame		= pCharacter->GetBoundFrame();
	m_moving	= pNavChar->IsMoving();
	m_skelId	= pCharacter->GetSkeletonId();

	m_pAnimControl	= pCharacter->GetAnimControl();
	m_pNavControl	= pNavChar->GetNavControl();
	m_motionType	= pNavChar->GetCurrentMotionType();
	m_mtSubcategory = pNavChar->GetRequestedMtSubcategory();
	m_velocityPs	= pNavChar->GetVelocityPs();

	if (const NavCharacter* pRealNavChar = pNavChar->ToNavCharacter())
	{
		m_dcDemeanorValue = pRealNavChar->GetCurrentDcDemeanor();
	}
	else
	{
		m_dcDemeanorValue = 0;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F DebugDrawApEntryAnims(const ActionPack& ap,
						   const BoundFrame& apRef,
						   const ApEntry::CharacterState& cs,
						   const DC::ApEntryItemList& entryList,
						   const NavCharOptions::ApControllerOptions& debugOptions,
						   bool validOnly /* = false */)
{
	STRIP_IN_FINAL_BUILD_VALUE(0);

	const bool isVerbose = debugOptions.m_verboseAnimDebug;

	const U32F kMaxAnimEntries = 64;

	const AnimControl* pAnimControl = cs.m_pAnimControl;
	const Locator apRefLoc = apRef.GetLocator();

	ApEntry::AvailableEntry availableEntries[kMaxAnimEntries];
	const U32F numAvailableEntries = ApEntry::BuildInitialEntryList(cs,
																	&entryList,
																	ApEntry::GatherMode::kDebug,
																	apRef,
																	availableEntries,
																	kMaxAnimEntries);

	ApEntry::FlagInvalidEntries(cs, ApEntry::GatherMode::kDebug, apRef, availableEntries, numAvailableEntries);

	U32F numDrawn = 0;

	for (U32F ii = 0; ii < numAvailableEntries; ++ii)
	{
		const ApEntry::AvailableEntry& currentEntry = availableEntries[ii];

		if (validOnly && currentEntry.m_errorFlags.m_all)
			continue;

		if (debugOptions.m_motionTypeFilter >= 0 && debugOptions.m_motionTypeFilter < kMotionTypeMax)
		{
			const I32F motionTypeMask = (1U << debugOptions.m_motionTypeFilter);
			const I32 validMotions = currentEntry.m_pDcDef->m_validMotions;

			if ((validMotions != 0) && (validMotions & motionTypeMask) == 0)
				continue;
		}
		else if (debugOptions.m_motionTypeFilter < 0) // use current
		{
			const I32F motionTypeMask = (1U << cs.m_motionType);
			const I32 validMotions = currentEntry.m_pDcDef->m_validMotions;

			if ((validMotions != 0) && (validMotions & motionTypeMask) == 0)
				continue;
		}

		if (debugOptions.m_mtSubcategoryFilter != INVALID_STRING_ID_64)
		{
			const StringId64 mtSubcategory = currentEntry.m_pDcDef->m_mtSubcategory;
			if (debugOptions.m_mtSubcategoryFilter == SID("default"))
			{
				if ((mtSubcategory != INVALID_STRING_ID_64) && (mtSubcategory != SID("normal")))
					continue;
			}
			else
			{
				if (mtSubcategory != debugOptions.m_mtSubcategoryFilter)
					continue;
			}
		}

		const StringId64 animId	= currentEntry.m_pDcDef->m_animName;
		const float startPhase	= currentEntry.m_phaseMin;
		const bool mirror		= currentEntry.m_playMirrored;
		const Color color		= currentEntry.GetDebugColor();

		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();

		if (!pAnim)
			continue;

		Locator startAlignWs(kIdentity);
		if (!FindAlignFromApReferenceCached(cs.m_skelId,
											pAnim,
											startPhase,
											apRefLoc,
											SID("apReference"),
											mirror,
											&startAlignWs))
		{
			continue;
		}

		ApEntry::DebugDrawEntryRange(cs, apRef, availableEntries[ii], color);

		if (isVerbose)
		{
			currentEntry.DebugDraw(cs, ap, apRef);

			if (currentEntry.m_errorFlags.m_noClearLineOfMotion)
			{
				ApEntry::IsEntryMotionClearOnNav(cs, apRef, currentEntry, true);
			}
		}

		++numDrawn;
	}

	return numDrawn;
}
