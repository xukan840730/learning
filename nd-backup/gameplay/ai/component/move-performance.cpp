/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/component/move-performance.h"

#include "corelib/math/segment-util.h"
#include "corelib/math/spherical-coords.h"
#include "corelib/util/random.h"

#include "ndlib/anim/anim-align-cache.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/anim/nd-gesture-util.h"
#include "gamelib/gameplay/ai/agent/motion-config.h"
#include "gamelib/gameplay/ai/agent/nav-character-util.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/path-waypoints.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/ndphys/collision-cast-interface.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/scriptx/h/move-performance-defines.h"
#include "gamelib/scriptx/h/nd-ai-defines.h"
#include "gamelib/scriptx/h/nd-gesture-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static void FillFromDc(MovePerformance::Performance* pPerformance, const DC::MovePerformance* pDcData)
{
	pPerformance->m_allowDialogLookGestures = pDcData->m_allowDialogLookGestures;
	pPerformance->m_dcAnimId				= pDcData->m_animId;
	pPerformance->m_flags					= pDcData->m_flags;
	pPerformance->m_maxPivotErrDeg			= pDcData->m_maxPivotErrDeg;
	pPerformance->m_maxExitErrDeg			= pDcData->m_maxExitErrDeg;
	pPerformance->m_maxPathDeviation		= pDcData->m_maxPathDeviation;
	pPerformance->m_exitPhase				= pDcData->m_exitPhase;
	pPerformance->m_wallContact				= pDcData->m_wallContact;
	pPerformance->m_focusDir				= pDcData->m_focusDir;
	pPerformance->m_focusPos				= pDcData->m_focusPos;
	pPerformance->m_categoryId				= pDcData->m_category;
	pPerformance->m_subCategoryId			= pDcData->m_subCategory;
	pPerformance->m_motionTypeMask			= pDcData->m_motionTypeMask;
	pPerformance->m_minSpeedFactor			= pDcData->m_minSpeedPct;
	pPerformance->m_fitPhaseStart			= pDcData->m_pathFitPhaseRange->m_val0;
	pPerformance->m_fitPhaseEnd				= pDcData->m_pathFitPhaseRange->m_val1;
	pPerformance->m_startPhase				= 0.0f;
	pPerformance->m_pDcData					= pDcData;

	if (pDcData->m_blendIn)
	{
		pPerformance->m_blendIn = *pDcData->m_blendIn;
	}
	else
	{
		pPerformance->m_blendIn.m_animFadeTime = 0.4f;
		pPerformance->m_blendIn.m_motionFadeTime = 0.1f;
		pPerformance->m_blendIn.m_curve = DC::kAnimCurveTypeUniformS;
	}

	if (pDcData->m_blendOut)
	{
		pPerformance->m_blendOut = *pDcData->m_blendOut;
	}
	else
	{
		pPerformance->m_blendIn.m_animFadeTime = 0.3f;
		pPerformance->m_blendIn.m_motionFadeTime = 0.3f;
		pPerformance->m_blendIn.m_curve = DC::kAnimCurveTypeUniformS;
	}

	if (pDcData->m_selfBlend)
	{
		pPerformance->m_selfBlend = *pDcData->m_selfBlend;
	}
	else
	{
		pPerformance->m_selfBlend.m_phase = -1.0f;
		pPerformance->m_selfBlend.m_time = -1.0f;
		pPerformance->m_selfBlend.m_curve = DC::kAnimCurveTypeLinear;
	}

	if (pDcData->m_wallContact && pDcData->m_wallContact->m_selfBlendIn)
	{
		pPerformance->m_wallContactSbIn = *pDcData->m_wallContact->m_selfBlendIn;
	}
	else
	{
		pPerformance->m_wallContactSbIn.m_phase = -1.0f;
		pPerformance->m_wallContactSbIn.m_time = -1.0f;
		pPerformance->m_wallContactSbIn.m_curve = DC::kAnimCurveTypeLinear;
	}

	if (pDcData->m_wallContact && pDcData->m_wallContact->m_selfBlendOut)
	{
		pPerformance->m_wallContactSbOut = *pDcData->m_wallContact->m_selfBlendOut;
	}
	else
	{
		pPerformance->m_wallContactSbOut.m_phase = -1.0f;
		pPerformance->m_wallContactSbOut.m_time = -1.0f;
		pPerformance->m_wallContactSbOut.m_curve = DC::kAnimCurveTypeLinear;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ApplyDebugEdit(MovePerformance::Performance* pPerformance,
						   ScriptPointer<const DC::MovePerformanceTable> pTable,
						   U32F tableIndex)
{
	STRIP_IN_FINAL_BUILD;

	NavCharOptions::MovePerformanceEdit& debugEdit = g_navCharOptions.m_movePerformances.m_debugEdit;

	if ((debugEdit.m_pTable == pTable) && (debugEdit.m_tableIndex == tableIndex))
	{
		pPerformance->m_motionTypeMask = debugEdit.m_motionTypeMask;
		pPerformance->m_blendIn = debugEdit.m_blendIn;
		pPerformance->m_selfBlend = debugEdit.m_selfBlend;
		pPerformance->m_maxPivotErrDeg = debugEdit.m_maxPivotErrDeg;
		pPerformance->m_maxExitErrDeg = debugEdit.m_maxExitErrDeg;
		pPerformance->m_maxPathDeviation = debugEdit.m_maxPathDeviation;
		pPerformance->m_exitPhase = debugEdit.m_exitPhase;
		pPerformance->m_minSpeedFactor = debugEdit.m_minSpeedFactor;

		if (debugEdit.m_singlePivotFitting)
		{
			pPerformance->m_flags |= DC::kMovePerformanceFlagSinglePivotFitting;
		}
		else
		{
			pPerformance->m_flags &= ~DC::kMovePerformanceFlagSinglePivotFitting;
		}
	}
	else
	{
		debugEdit.m_pTable = pTable;
		debugEdit.m_tableIndex = tableIndex;

		debugEdit.m_motionTypeMask = pPerformance->m_motionTypeMask;
		debugEdit.m_blendIn = pPerformance->m_blendIn;
		debugEdit.m_selfBlend = pPerformance->m_selfBlend;
		debugEdit.m_maxPivotErrDeg = pPerformance->m_maxPivotErrDeg;
		debugEdit.m_maxExitErrDeg = pPerformance->m_maxExitErrDeg;
		debugEdit.m_maxPathDeviation = pPerformance->m_maxPathDeviation;
		debugEdit.m_exitPhase = pPerformance->m_exitPhase;
		debugEdit.m_minSpeedFactor = pPerformance->m_minSpeedFactor;
		debugEdit.m_singlePivotFitting = (pPerformance->m_flags & DC::kMovePerformanceFlagSinglePivotFitting) != 0;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void LimitSelfBlend(DC::SelfBlendParams* pSelfBlend, float animDuration)
{
	if (!pSelfBlend)
		return;

	if (pSelfBlend->m_phase >= 0.0f)
	{
		const float remTime = (1.0f - pSelfBlend->m_phase) * animDuration;
		const float reqTime = Max(pSelfBlend->m_time, 0.0f);
		pSelfBlend->m_time = Min(remTime, reqTime);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ValidateSelfBlend(const ArtItemAnim* pAnim,
							  const DC::SelfBlendParams* pSelfBlend,
							  float startPhase,
							  float endPhase,
							  const char* description)
{
	if (!pAnim || !pAnim->m_pClipData || !pSelfBlend)
	{
		return;
	}

	if (pSelfBlend->m_phase < startPhase || pSelfBlend->m_phase > endPhase)
	{
		SetForegroundColor(kMsgOut, kTextColorYellow);
		MsgOut("        WARNING: %s ['%s' %0.2f->%0.2f] has self blend phase outside animation window! (%0.2f)\n",
			   description, pAnim->GetName(), startPhase, endPhase, pSelfBlend->m_phase);
		SetForegroundColor(kMsgOut, kTextColorNormal);
	}

	const float sbPhase		= Limit(pSelfBlend->m_phase, startPhase, endPhase);
	const float phaseDelta	= endPhase - sbPhase;
	const float timeDelta	= phaseDelta * pAnim->m_pClipData->m_secondsPerFrame * pAnim->m_pClipData->m_fNumFrameIntervals;

	if (pSelfBlend->m_time > timeDelta)
	{
		SetForegroundColor(kMsgOut, kTextColorYellow);
		MsgOut("        WARNING: %s ['%s' %0.2f->%0.2f] has self blend longer than animation window! (%0.2fsec > %0.2fsec)\n",
			   description, pAnim->GetName(), startPhase, endPhase, pSelfBlend->m_time, timeDelta);
		SetForegroundColor(kMsgOut, kTextColorNormal);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
MovePerformance::MovePerformance()
: m_numUsedTableEntries(0)
, m_pCachedPerformanceInfo(nullptr)
, m_cacheSize(0)
, m_numCachedEntries(0)
, m_cacheValid(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::AllocateCache(U32 cacheSize)
{
	AI_ASSERT(m_pCachedPerformanceInfo == nullptr);
	m_pCachedPerformanceInfo = NDI_NEW PerformanceInfo[cacheSize];
	if (!m_pCachedPerformanceInfo)
		return false;

	memset(m_pCachedPerformanceInfo, 0, sizeof(PerformanceInfo) * cacheSize);
	m_cacheSize = cacheSize;
	m_cacheValid = false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MovePerformance::RebuildCache(const NavCharacter* pNavChar)
{
	PROFILE(Animation, Mp_RebuildCache);

	if (!m_pCachedPerformanceInfo || (0 == m_cacheSize))
		return;

	memset(m_pCachedPerformanceInfo, 0, sizeof(PerformanceInfo)*m_cacheSize);

	U32 numCachedEntries = 0;

	for (U32F ii = 0; (ii < m_numUsedTableEntries) && (numCachedEntries < m_cacheSize); ++ii)
	{
		const DC::MovePerformanceTable* pTable = m_tableEntries[ii].m_pTable;
		if (!pTable)
		{
			continue;
		}

		for (U32F jj = 0; jj < (pTable->m_movePerformanceCount) && (numCachedEntries < m_cacheSize); ++jj)
		{
			const DC::MovePerformance& dcInfo = pTable->m_movePerformances[jj];
			PerformanceInfo* pPerformanceInfo = &m_pCachedPerformanceInfo[numCachedEntries];

			Performance perf;
			FillFromDc(&perf, &dcInfo);

			if (!BuildPerformanceInfo(pNavChar, &perf, 0.0f, pPerformanceInfo)) 
			{
				continue;
			}

			++numCachedEntries;
		}
	}

	m_cacheValid = true;
	m_numCachedEntries = numCachedEntries;
	m_cacheOverlayHash = pNavChar->GetAnimControl()->GetAnimOverlays()->GetHash();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MovePerformance::TryRebuildCache(const NavCharacter* pNavChar)
{
	if (!pNavChar)
		return;

	const AnimOverlaySnapshotHash curHash = pNavChar->GetAnimControl()->GetAnimOverlays()->GetHash();
	if (curHash == m_cacheOverlayHash)
		return;

	RebuildCache(pNavChar);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MovePerformance::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pCachedPerformanceInfo, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::LookupCachedPerformanceInfo(const StringId64 animId,
												  bool mirror,
												  PerformanceInfo* pInfoOut,
												  float startPhase) const
{
	if (!m_pCachedPerformanceInfo)
		return false;

	static const StringId64 sidNull = SID("null");

	for (U32F i = 0; i < m_cacheSize; ++i)
	{
		if (m_pCachedPerformanceInfo[i].m_resolvedAnimId == sidNull)
			continue;

		if (m_pCachedPerformanceInfo[i].m_mirror != mirror)
			continue;

		if ((m_pCachedPerformanceInfo[i].m_inputAnimId != animId) && (m_pCachedPerformanceInfo[i].m_resolvedAnimId != animId))
			continue;

		if (IsClose(m_pCachedPerformanceInfo[i].m_startPhase, startPhase, 0.0001f))
		{
			*pInfoOut = m_pCachedPerformanceInfo[i];
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MovePerformance::ValidateTable(const NavCharacter* pNavChar, StringId64 tableId) const
{
	STRIP_IN_FINAL_BUILD;

	const TableEntry::TablePtr pTable = TableEntry::TablePtr(tableId);
	if (!pTable.Valid())
	{
		SetForegroundColor(kMsgOut, kTextColorRed);
		MsgOut("        ERROR: Move performance table '%s' doesn't exist\n",
			   DevKitOnly_StringIdToString(tableId));
		SetForegroundColor(kMsgOut, kTextColorNormal);
		return;
	}

	for (U32F ii = 0; ii < pTable->m_movePerformanceCount; ++ii)
	{
		const DC::MovePerformance& movePerformance = pTable->m_movePerformances[ii];

		if (movePerformance.m_animId == INVALID_STRING_ID_64)
		{
			SetForegroundColor(kMsgOut, kTextColorRed);
			MsgOut("        ERROR: Invalid anim id '%s'\n",
				   DevKitOnly_StringIdToStringOrHex(pTable->m_name));
			SetForegroundColor(kMsgOut, kTextColorNormal);
		}

		if (movePerformance.m_selfBlend && movePerformance.m_wallContact)
		{
			SetForegroundColor(kMsgOut, kTextColorRed);
			MsgOut("        ERROR: Expected either self-blend or wall-contact data, not both: '%s' in '%s'\n",
				   DevKitOnly_StringIdToStringOrHex(movePerformance.m_animId),
				   DevKitOnly_StringIdToStringOrHex(pTable->m_name));
			SetForegroundColor(kMsgOut, kTextColorNormal);
		}

		const StringId64 animId           = movePerformance.m_animId;
		const AnimControl* pAnimControl = pNavChar->GetAnimControl();
		const ArtItemAnim* pAnim        = pAnimControl->LookupAnim(animId).ToArtItem();

		if (!pAnim)
		{
			SetForegroundColor(kMsgOut, kTextColorRed);
			MsgOut("        ERROR: Missing anim '%s' in '%s'\n",
				   DevKitOnly_StringIdToStringOrHex(movePerformance.m_animId),
				   DevKitOnly_StringIdToStringOrHex(pTable->m_name));
			SetForegroundColor(kMsgOut, kTextColorNormal);
		}

		if (movePerformance.m_wallContact)
		{
			const DC::WallContactParams& wallContact = *movePerformance.m_wallContact;

			if (wallContact.m_startPhase > wallContact.m_stopPhase)
			{
				SetForegroundColor(kMsgOut, kTextColorYellow);
				MsgOut("        WARNING: Contact start phase exceeds contact stop phase: '%s' in '%s'\n",
					   DevKitOnly_StringIdToStringOrHex(movePerformance.m_animId),
					   DevKitOnly_StringIdToStringOrHex(pTable->m_name));
				SetForegroundColor(kMsgOut, kTextColorNormal);
			}

			if (wallContact.m_selfBlendIn)
			{
				ValidateSelfBlend(pAnim, wallContact.m_selfBlendIn, 0.0f, wallContact.m_startPhase, "Self blend in");
			}

			if (wallContact.m_selfBlendOut)
			{
				ValidateSelfBlend(pAnim, wallContact.m_selfBlendOut, wallContact.m_stopPhase, 1.0f, "Self blend out");
			}

			// Anim id must be unique for wall contact performances so it can be used to identify ray casts
			for (U32F jj = 0; jj < m_numUsedTableEntries; ++jj)
			{
				const DC::MovePerformanceTable* pOtherTable = m_tableEntries[jj].m_pTable;
				if (!pOtherTable)
				{
					continue;
				}

				for (U32F kk = 0; kk < pOtherTable->m_movePerformanceCount; ++kk)
				{
					if (pOtherTable->m_movePerformances[kk].m_wallContact &&
						pOtherTable->m_movePerformances[kk].m_animId == movePerformance.m_animId)
					{
						if (pOtherTable != pTable || kk != ii)
						{
							SetForegroundColor(kMsgOut, kTextColorRed);
							MsgOut("        ERROR: Duplicate anim id '%s' in move performance tables '%s' and '%s'\n",
								   DevKitOnly_StringIdToStringOrHex(movePerformance.m_animId),
								   DevKitOnly_StringIdToStringOrHex(pTable->m_name),
								   DevKitOnly_StringIdToStringOrHex(pOtherTable->m_name));
							SetForegroundColor(kMsgOut, kTextColorNormal);
						}
					}
				}
			}
		}
		else if (movePerformance.m_selfBlend)
		{
			ValidateSelfBlend(pAnim, movePerformance.m_selfBlend, 0.0f, 1.0f, "Self blend");
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::AddTable(const NavCharacter* pNavChar, StringId64 entryId, StringId64 tableId)
{
	PROFILE(AI, Mp_AddTable);

	AI_ASSERT(m_numUsedTableEntries < kMaxMpTableEntries);

	if (m_numUsedTableEntries >= kMaxMpTableEntries)
	{
		ASSERT(false);
		MsgAnimWarn("AddTable: Too many move performance tables. Unable to add \"%s (%s).\"\n",
					DevKitOnly_StringIdToString(entryId),
					DevKitOnly_StringIdToString(tableId));
		return false;
	}

	const TableEntry::TablePtr pTable = TableEntry::TablePtr(tableId, SID("move-performances"));
	if (!pTable.Valid())
	{
		if (g_navCharOptions.m_movePerformances.m_debugDrawMovePerformances && tableId != INVALID_STRING_ID_64)
		{
			MsgConPersistent("AddTable: Unable to find move performance table \"%s (%s).\"\n",
				DevKitOnly_StringIdToString(entryId),
				DevKitOnly_StringIdToString(tableId));
		}

		return false;
	}

	TableEntry& entry = m_tableEntries[m_numUsedTableEntries++];

	entry.m_entryId = entryId;
	entry.m_pTable = pTable;

	RebuildCache(pNavChar);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 MovePerformance::GetTableId(StringId64 entryId) const
{
	for (U32F ii = 0; ii < m_numUsedTableEntries; ++ii)
	{
		if (m_tableEntries[ii].m_entryId == entryId)
		{
			return m_tableEntries[ii].m_pTable.GetId();
		}
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::RemoveTable(const NavCharacter* pNavChar, StringId64 entryId)
{
	PROFILE(AI, Mp_RemoveTable);

	if (m_numUsedTableEntries == 0)
	{
		return false;
	}

	bool removed = false;

	for (U32F ii = 0; ii < m_numUsedTableEntries; ++ii)
	{
		if (m_tableEntries[ii].m_entryId == entryId)
		{
			--m_numUsedTableEntries;
			if (ii < m_numUsedTableEntries)
			{
				m_tableEntries[ii] = m_tableEntries[m_numUsedTableEntries];
			}

			removed = true;
			break;
		}
	}

	if (removed)
	{
		RebuildCache(pNavChar);
	}
	else
	{
		MsgAnimWarn("RemoveTable: Unable to remove move performance table \"%s.\"\n",
					DevKitOnly_StringIdToString(entryId));
	}

	return removed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::HasTable(const NavCharacter* pNavChar, StringId64 entryId) const
{
	if (m_numUsedTableEntries == 0)
		return false;

	for (U32F ii = 0; ii < m_numUsedTableEntries; ++ii)
	{
		if (m_tableEntries[ii].m_entryId == entryId)
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Only required if tables contain entires with wall contact parameters
void MovePerformance::KickContactRayCasts(const NavCharacter* pNavChar, Vector_arg moveDirPs)
{
	PROFILE(AI, Mp_KickContactRayCasts);

	struct
	{
		Point m_startPs;
		Point m_endPs;
	} contactRayPoints[kMaxContactRays];

	U32F contactRayCount = 0;

	// ==== Collect contact ray start/end points ====

	for (U32F ii = 0; ii < m_numUsedTableEntries; ++ii)
	{
		const DC::MovePerformanceTable* pTable = m_tableEntries[ii].m_pTable;
		if (!pTable)
		{
			continue;
		}

		for (U32F jj = 0; jj < pTable->m_movePerformanceCount; ++jj)
		{
			const DC::MovePerformance& movePerformance = pTable->m_movePerformances[jj];
			const DC::WallContactParams* pWallContact = movePerformance.m_wallContact;

			if (pWallContact)
			{
				Locator startingLocPs = pNavChar->GetLocatorPs();
				startingLocPs.SetRotation(QuatFromLookAt(moveDirPs, kUnitYAxis));

				Locator contactLocPs;
				//TODO(MB) Anim contact locations could be cached when the table is added
				if (!GetAnimContactLocPs(&contactLocPs,
										 pNavChar,
										 movePerformance.m_animId,
										 *pWallContact,
										 startingLocPs,
										 movePerformance.m_mirror))
				{
					continue;
				}

				const Vector wallDirPs = -GetLocalZ(contactLocPs.GetRotation());

				contactRayPoints[contactRayCount].m_startPs = contactLocPs.Pos() - wallDirPs * pWallContact->m_range;
				contactRayPoints[contactRayCount].m_endPs = contactLocPs.Pos() + wallDirPs * pWallContact->m_range;

				m_contactRayAnimIds[contactRayCount] = movePerformance.m_animId;

				if (++contactRayCount == kMaxContactRays)
				{
					MsgAiWarn("MovePerformance::KickContactRayCasts - Exceeded available rays (%d)\n", kMaxContactRays);
					break;
				}
			}
		}

		if (contactRayCount == kMaxContactRays)
		{
			break;
		}
	}

	// ==== Kick contact rays ====

	if (contactRayCount > 0)
	{
		const U32F kMaxContactsPerRay = 1;

		m_raycastJob.Open(contactRayCount,
						  kMaxContactsPerRay,
						  ICollCastJob::kCollCastSingleSidedCollision,
						  ICollCastJob::kClientNpc);

		for (U32F ii = 0; ii < contactRayCount; ++ii)
		{
			m_raycastJob.SetProbeExtents(ii, contactRayPoints[ii].m_startPs, contactRayPoints[ii].m_endPs);
		}

		m_raycastJob.SetFilterForAllProbes(CollideFilter(Collide::kLayerMaskGeneral));
		m_raycastJob.Kick(FILE_LINE_FUNC, contactRayCount);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F MovePerformance::GetMovePerformanceCount() const
{
	U32F movePerformanceCount = 0;

	for (U32F ii = 0; ii < m_numUsedTableEntries; ++ii)
	{
		if (const DC::MovePerformanceTable* pTable = m_tableEntries[ii].m_pTable)
		{
			movePerformanceCount += pTable->m_movePerformanceCount;
		}
	}

	return movePerformanceCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::MovePerformance* MovePerformance::GetPerformanceDcData(int index) const
{
	U32F movePerformanceCount = 0;

	for (U32F ii = 0; ii < m_numUsedTableEntries; ++ii)
	{
		if (const DC::MovePerformanceTable* pTable = m_tableEntries[ii].m_pTable)
		{
			movePerformanceCount += pTable->m_movePerformanceCount;

			if (movePerformanceCount > index)
			{
				U32F movePerformanceIndex = index - (movePerformanceCount - pTable->m_movePerformanceCount);
				return &pTable->m_movePerformances[movePerformanceIndex];
			}
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MovePerformance::CalcStartPhase(const NavCharacter* pNavChar, Performance* pPerformance)
{
	const SkeletonId skelId = pNavChar->GetSkeletonId();
	const bool mirror = pPerformance->m_info.m_mirror;

	const AnkleInfo* pAnklesOs = pNavChar->GetAnkleInfo();

	PoseMatchInfo mi;
	mi.m_startPhase = 0.0f;
	mi.m_endPhase	= pPerformance->m_fitPhaseStart;
	mi.m_skelId = skelId;
	mi.m_mirror = mirror;

	if (false)
	{
		mi.m_debug = true;
		mi.m_debugDrawLoc = pNavChar->GetLocator();
		mi.m_debugDrawTime = Seconds(0.5f);
		mi.m_debugPrint = true;
	}

	mi.m_entries[0].m_channelId		= mirror ? SID("rAnkle") : SID("lAnkle");
	mi.m_entries[0].m_matchPosOs	= pAnklesOs->m_anklePos[0];
	mi.m_entries[0].m_valid			= true;
	mi.m_entries[0].m_matchVel		= pAnklesOs->m_ankleVel[0];
	mi.m_entries[0].m_velocityValid = true;

	mi.m_entries[1].m_channelId		= mirror ? SID("lAnkle") : SID("rAnkle");
	mi.m_entries[1].m_matchPosOs	= pAnklesOs->m_anklePos[1];
	mi.m_entries[1].m_valid			= true;
	mi.m_entries[1].m_matchVel		= pAnklesOs->m_ankleVel[1];
	mi.m_entries[1].m_velocityValid = true;

	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const ArtItemAnim* pAnim		= pAnimControl->LookupAnim(pPerformance->m_dcAnimId).ToArtItem();
	const PoseMatchResult result	= CalculateBestPoseMatchPhase(pAnim, mi);

	if (result.m_phase >= 0.0f)
	{
		const float startPhase	= result.m_phase;

		pPerformance->m_startPhase = startPhase;

		const Locator& curAlignPs = pNavChar->GetLocatorPs();
		Locator newStartApPs;
		Locator startAlignDeltaLs;

		if (FindApReferenceFromAlign(skelId, pAnim, curAlignPs, &newStartApPs, SID("apReference"), startPhase, mirror))
		{
			pPerformance->m_startingApRef.SetLocatorPs(newStartApPs);
		}
		else if (EvaluateChannelInAnim(skelId, pAnim, SID("align"), startPhase, &startAlignDeltaLs, mirror))
		{
			const Locator invStartDeltaLs = Inverse(startAlignDeltaLs);
			Locator desStartAlignPs = curAlignPs;

			Locator startAlignDelta2Ls;
			if (EvaluateChannelInAnim(skelId,
									  pAnim,
									  SID("align"),
									  startPhase + (pAnim->m_pClipData->m_phasePerFrame * 2.0f),
									  &startAlignDelta2Ls,
									  mirror))
			{
				const Vector desEntryVecLs = pPerformance->m_info.m_entryVecLs;
				const Vector newEntryVecLs = SafeNormalize(startAlignDelta2Ls.Pos() - startAlignDeltaLs.Pos(),
														   desEntryVecLs);

				const Vector desEntryVecPs = pPerformance->m_startingApRef.GetLocatorPs().TransformVector(desEntryVecLs);
				const Locator stage1ApPs = curAlignPs.TransformLocator(invStartDeltaLs);
				const Vector newEntryVecPs = stage1ApPs.TransformVector(newEntryVecLs);

				const Quat entryVecRotDelta = QuatFromVectors(newEntryVecPs, desEntryVecPs);

				if (false)
				{
					const Point drawPosWs = pPerformance->m_startingApRef.GetTranslation();
					g_prim.Draw(DebugArrow(drawPosWs, desEntryVecPs, kColorCyan, 0.5f, kPrimEnableHiddenLineAlpha, "desEntryVecPs"), Seconds(3.0f));
					g_prim.Draw(DebugArrow(drawPosWs, newEntryVecPs, kColorMagenta, 0.5f, kPrimEnableHiddenLineAlpha, "newEntryVecPs"), Seconds(3.0f));

					Vec4 axis;
					float angleRad;
					entryVecRotDelta.GetAxisAndAngle(axis, angleRad);

					MsgOut("%s : %0.2fdeg\n", pAnim->GetName(), RADIANS_TO_DEGREES(angleRad));
				}

				desStartAlignPs.SetRot(Normalize(entryVecRotDelta * desStartAlignPs.Rot()));
			}

			newStartApPs = desStartAlignPs.TransformLocator(invStartDeltaLs);
			pPerformance->m_startingApRef.SetLocatorPs(newStartApPs);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F MovePerformance::FindPerformances(const NavCharacter* pNavChar,
									   const FindParams& params,
									   U32F maxPerformances,
									   Performance* performancesOut) const
{
	PROFILE(AI, Mp_FindPerf);

	ASSERT(pNavChar);
	ASSERT(performancesOut);

	if (g_navCharOptions.m_movePerformances.m_disableMovePerformances)
	{
		return 0;
	}

	if (!pNavChar || !performancesOut)
	{
		return 0;
	}

	if (!IsFacingPath(pNavChar, params, params.m_debugDraw))
	{
		return 0;
	}

	U32F performancesCount = 0;

	for (U32F ii = 0; ii < m_numUsedTableEntries; ++ii)
	{
		const DC::MovePerformanceTable* pTable = m_tableEntries[ii].m_pTable;
		if (!pTable)
		{
			continue;
		}

		for (U32F jj = 0; jj < pTable->m_movePerformanceCount && performancesCount < maxPerformances; ++jj)
		{
			const DC::MovePerformance& movePerformance = pTable->m_movePerformances[jj];
			Performance* pPerformanceOut = &performancesOut[performancesCount];

			const bool debugDraw = FALSE_IN_FINAL_BUILD(params.m_debugDraw
														&& ((params.m_debugDrawIndex < 0)
															|| (params.m_debugDrawIndex == jj)));

			const bool stoppingPerformance = movePerformance.m_flags & DC::kMovePerformanceFlagStoppingPerformance;
			if (stoppingPerformance && !params.m_allowStoppingPerformance)
			{
				continue;
			}

			if (movePerformance.m_animId == params.m_excludePerformancesWithAnimId)
			{
				continue;
			}

			if (movePerformance.m_flags & DC::kMovePerformanceFlagGesture)
			{
				const DC::GestureDef* pGestureDef = Gesture::LookupGesture(movePerformance.m_animId);
				if (!pGestureDef)
					continue;
			}
			else if (params.m_gesturesOnly)
			{
				continue;
			}

			if ((movePerformance.m_flags & DC::kMovePerformanceFlagOnHorse) && !params.m_onHorse)
			{
				continue;
			}

			if (params.m_allowHoldingLeash && !movePerformance.m_allowWithLeash)
			{
				continue;
			}

			if (params.m_poiType != INVALID_STRING_ID_64)
			{
				bool includesType = false;
				for (int k = 0; k < movePerformance.m_poiTypeList->m_count; k++)
				{
					if (params.m_poiType == movePerformance.m_poiTypeList->m_array[k])
					{
						includesType = true;
						break;
					}
				}

				if (!includesType)
					continue;
			}

			FillFromDc(pPerformanceOut, &movePerformance);

			if (FALSE_IN_FINAL_BUILD(params.m_debugEditIndex == jj))
			{
				ApplyDebugEdit(pPerformanceOut, m_tableEntries[ii].m_pTable, jj);
			}

			const DC::NpcMotionType dcReqMotionType = GameMotionTypeToDc(params.m_reqMotionType);

			if ((params.m_reqMotionType < kMotionTypeMax)
				&& ((pPerformanceOut->m_motionTypeMask & (1U << dcReqMotionType)) == 0))
			{
				continue;
			}

			if ((params.m_categoryId != pPerformanceOut->m_categoryId) && (params.m_categoryId != SID("match-all"))
				&& (pPerformanceOut->m_categoryId != SID("match-all")))
			{
				continue;
			}

			if (((params.m_subCategoryId != pPerformanceOut->m_subCategoryId)
				&& (params.m_subCategoryId != SID("normal") || pPerformanceOut->m_subCategoryId != INVALID_STRING_ID_64))
				&& (params.m_subCategoryId != SID("match-all"))
				&& (pPerformanceOut->m_subCategoryId != SID("match-all")))
			{
				continue;
			}

			// find the best start phase
			pPerformanceOut->m_startPhase = 0.0f;

			if (!(movePerformance.m_flags & DC::kMovePerformanceFlagGesture) && (movePerformance.m_flags & DC::kMovePerformanceFlagNonLinear))
			{
				CalcStartPhase(pNavChar, pPerformanceOut);
			}

			PerformanceInfo performanceInfo;

			if (!BuildPerformance(pNavChar, params.m_moveDirPs, pPerformanceOut, pPerformanceOut->m_startPhase, debugDraw))
			{
				return false;
			}

			if (FALSE_IN_FINAL_BUILD(params.m_debugDrawIndex == jj))
			{
				NavCharOptions::MovePerformanceEdit& debugEdit = g_navCharOptions.m_movePerformances.m_debugEdit;
				debugEdit.m_animDuration = pPerformanceOut->m_info.m_duration;
			}

			if (!IsInFocus(pNavChar, params, pPerformanceOut, jj, debugDraw))
			{
				continue;
			}

			if (!RequiredSpeedMet(pNavChar, params, pPerformanceOut, debugDraw))
			{
				continue;
			}

			if (movePerformance.m_minPathRemaining > 0.0f
				&& ((params.m_pPathPs == nullptr)
					|| params.m_pPathPs->ComputePathLengthXz() < movePerformance.m_minPathRemaining))
			{
				continue;
			}

			if (params.m_pPathPs && !TryToFitPerformance(pNavChar, params, pPerformanceOut, debugDraw))
			{
				continue;
			}

			const DC::SelfBlendParams* pSelfBlendInParams = pPerformanceOut->m_selfBlend.m_phase >= 0.0f
																? &pPerformanceOut->m_selfBlend
																: nullptr;
			const DC::SelfBlendParams* pSelfBlendOutParams = nullptr;

			if (pPerformanceOut->m_wallContact)
			{
				if (!CheckContactPerformanceContact(pNavChar, pPerformanceOut, debugDraw))
				{
					continue;
				}

				pSelfBlendInParams = pPerformanceOut->m_wallContact->m_selfBlendIn;
				pSelfBlendOutParams = pPerformanceOut->m_wallContact->m_selfBlendOut;
			}

			if (!(movePerformance.m_flags & DC::kMovePerformanceFlagGesture) && params.m_pPathPs
				&& !IsPathDeviationWithinRange(pNavChar,
											   pPerformanceOut,
											   params,
											   pSelfBlendInParams,
											   pSelfBlendOutParams,
											   debugDraw))
			{
				continue;
			}

			if ((movePerformance.m_flags & DC::kMovePerformanceFlagGesture)
				|| PerformanceHasClearMotion(pNavChar,
											 pPerformanceOut,
											 pSelfBlendInParams,
											 pSelfBlendOutParams,
											 debugDraw))
			{
				++performancesCount;
			}
		}
	}

	return performancesCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::FindRandomPerformance(const NavCharacter* pNavChar,
											const FindParams& params,
											Performance* pPerformanceOut) const
{
	PROFILE(AI, Mp_FindRandomPerf);

	ASSERT(pNavChar);
	ASSERT(pPerformanceOut);

	if (!pNavChar || !pPerformanceOut)
	{
		return false;
	}

	const U32F movePerformanceCount = GetMovePerformanceCount();
	if (0 == movePerformanceCount)
	{
		return false;
	}

	Performance* pPerformances = NDI_NEW(kAllocSingleGameFrame) Performance[movePerformanceCount];
	if (!pPerformances)
	{
		return false;
	}

	const U32F performanceCount = FindPerformances(pNavChar,
												   params,
												   movePerformanceCount,
												   pPerformances);
	if (performanceCount == 0)
	{
		return false;
	}

	const I32F index = RandomIntRange(0, performanceCount - 1);
	*pPerformanceOut = pPerformances[index];

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MovePerformance::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (m_numUsedTableEntries == 0)
		return;

	MsgCon("Move Performances:     ");

	for (U32F ii = 0; ii < m_numUsedTableEntries; ++ii)
	{
		const TableEntry& entry = m_tableEntries[ii];
		MsgCon("%s (%s)%s",
			   DevKitOnly_StringIdToString(entry.m_entryId),
			   DevKitOnly_StringIdToString(entry.m_pTable.GetId()),
			   ii == m_numUsedTableEntries - 1 ? "" : ", ");
	}

	MsgCon("\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::BuildPerformanceInfo(const NavCharacter* pNavChar,
										   const Performance* pPerformance,
										   float startPhase,
										   PerformanceInfo* pInfoOut) const
{
	PROFILE(Animation, Mp_BuildPerfInfo);

	const SkeletonId skelId   = pNavChar->GetSkeletonId();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(pPerformance->m_dcAnimId).ToArtItem();
	if ((!pAnim || !pInfoOut) && !(pPerformance->m_flags & DC::kMovePerformanceFlagGesture))
	{
		return false;
	}

	const bool mirror = pPerformance->m_pDcData && pPerformance->m_pDcData->m_mirror;

	if (!(pPerformance->m_flags & DC::kMovePerformanceFlagGesture))
	{
		g_animAlignCache.TryCacheAnim(pAnim, SID("align"), 0.0f, 1.0f);
		g_animAlignCache.TryCacheAnim(pAnim, SID("apReference"), 0.0f, 1.0f);

		const float deltaPhi = pAnim->m_pClipData->m_phasePerFrame;
		Locator entryLoc = Locator(kIdentity);
		float entrySpeed = 0.0f;

		for (U32F i = 1; i < 6; ++i)
		{
			const float phase = startPhase + i * deltaPhi;

			if (!EvaluateChannelInAnimCached(skelId, pAnim, SID("align"), phase, mirror, &entryLoc))
			{
				return false;
			}

			const float moveDist = Dist(entryLoc.Pos(), kOrigin);

			if (moveDist > 0.00001f)
			{
				const float animDuration = GetDuration(pAnim);
				const float entryTime = animDuration * phase;
				entrySpeed = moveDist / entryTime;
				break;
			}
		}

		Locator exitLoc0Ls, exitLoc1Ls;
		if (!EvaluateChannelInAnimCached(skelId,
										 pAnim,
										 SID("align"),
										 pPerformance->m_fitPhaseEnd - (3.0f * deltaPhi),
										 mirror,
										 &exitLoc0Ls))
		{
			return false;
		}

		if (!EvaluateChannelInAnimCached(skelId,
										 pAnim,
										 SID("align"),
										 pPerformance->m_fitPhaseEnd,
										 mirror,
										 &exitLoc1Ls))
		{
			return false;
		}

		const bool stoppingPerformance = pPerformance->m_flags & DC::kMovePerformanceFlagStoppingPerformance;

		const Vector entryVecLs = SafeNormalize(entryLoc.Pos() - kOrigin, kZero);
		Vector exitDeltaLs = stoppingPerformance ? GetLocalZ(exitLoc1Ls.Rot()) : (exitLoc1Ls.Pos() - exitLoc0Ls.Pos());
		Vector exitVecLs = SafeNormalize(exitDeltaLs, kZero);

		Locator exitVectorFromAp;
		if (EvaluateChannelInAnimCached(skelId,
										pAnim,
										SID("apReference-exit-vector"),
										pPerformance->m_fitPhaseEnd,
										mirror,
										&exitVectorFromAp))
		{
			exitVecLs = GetLocalZ(exitVectorFromAp.GetRotation());
			exitVecLs = exitLoc1Ls.TransformVector(exitVecLs);
		}
		float alpha = 0.0f;

		const bool singlePivotFitting = pPerformance->m_flags & DC::kMovePerformanceFlagSinglePivotFitting;

		if (singlePivotFitting)
		{
			alpha = 0.0f;
			exitVecLs = SafeNormalize(exitLoc1Ls.Pos() - kOrigin, kZero);
		}
		else if (stoppingPerformance)
		{
			alpha = 1.0f;
		}
		else if (pPerformance->m_flags & DC::kMovePerformanceFlagPlayerAvoid)
		{
			alpha = 0.5f * Dist(exitLoc1Ls.Pos(), kOrigin);
		}
		else
		{
			const float dotP = Dot(entryVecLs, exitVecLs);
			if (dotP < 0.9f)
			{
				Scalar d = 0.0f;
				const Vector normToExitPosLs = SafeNormalize(exitLoc1Ls.Pos() - kOrigin, kZero, d);
				const float angleA = SafeAcos(Dot(normToExitPosLs, exitVecLs));
				const float angleB = SafeAcos(Dot(normToExitPosLs, entryVecLs));
				const float phi = PI - angleA - angleB;
				const float sinPhi = Sin(phi);
				const float sinRatio = (Abs(sinPhi) > 0.001f) ? (d / sinPhi) : SCALAR_LC(0.0f);
				const float sinA = Sin(angleA);

				alpha = sinRatio * sinA;
			}
		}

		const Point pivotLs = kOrigin + (entryVecLs * alpha);

		pInfoOut->m_startPhase = startPhase;
		pInfoOut->m_inputAnimId = pPerformance->m_dcAnimId;
		pInfoOut->m_resolvedAnimId = pAnim->GetNameId();
		pInfoOut->m_entryVecLs = (alpha > 0.0f) ? entryVecLs : kUnitZAxis;
		pInfoOut->m_initialSpeed = entrySpeed;
		pInfoOut->m_exitVecLs = exitVecLs;
		pInfoOut->m_exitLocLs = Locator(exitLoc1Ls.GetTranslation(), QuatFromLookAt(exitVecLs, kUnitYAxis));
		pInfoOut->m_pivotLs = pivotLs;
	}
	else
	{
		pInfoOut->m_startPhase = 0.0f;
		pInfoOut->m_inputAnimId = pPerformance->m_dcAnimId;
		pInfoOut->m_resolvedAnimId = pPerformance->m_dcAnimId;
	}

	pInfoOut->m_focusDirApId		= INVALID_STRING_ID_64;
	pInfoOut->m_focusDirLs			= kZero;
	pInfoOut->m_focusDirMaxErrDeg	= -1.0f;
	pInfoOut->m_focusDirValid		= false;
	pInfoOut->m_focusDirError		= false;

	pInfoOut->m_focusPosApId		= INVALID_STRING_ID_64;
	pInfoOut->m_focusPosLs			= kOrigin;
	pInfoOut->m_focusPosMaxDistErr	= -1.0f;
	pInfoOut->m_focusPosValid		= false;
	pInfoOut->m_focusPosError		= false;
	pInfoOut->m_mirror = mirror;

	pInfoOut->m_duration			= GetDuration(pAnim);

	if (pPerformance->m_focusDir)
	{
		const DC::FocusDirParams* pFocusDir = pPerformance->m_focusDir;
		Vector focusDirLs = SafeNormalize(pFocusDir->m_dirLs, kZero);
		Point focusOriginLs = kOrigin;

		bool success = true;

		if (pFocusDir->m_apChannelId != INVALID_STRING_ID_64)
		{
			ndanim::JointParams focusJp;
			success = EvaluateChannelInAnim(skelId,
											pAnim,
											pFocusDir->m_apChannelId,
											startPhase,
											&focusJp,
											mirror);
			if (success)
			{
				focusDirLs = GetLocalZ(focusJp.m_quat);
				focusOriginLs = focusJp.m_trans;
			}
		}

		if (success)
		{
			pInfoOut->m_focusDirLs = focusDirLs;
			pInfoOut->m_focusOriginLs = focusOriginLs;
			pInfoOut->m_focusOriginMaxPhaseLs = focusOriginLs;
			pInfoOut->m_focusDirMaxErrDeg = pFocusDir->m_maxAngleErrDeg;
			pInfoOut->m_focusDirApId = pFocusDir->m_apChannelId;
			pInfoOut->m_focusDirValid = true;

			if (pFocusDir->m_apChannelId != INVALID_STRING_ID_64)
			{
				ndanim::JointParams focusMaxJp;
				if (EvaluateChannelInAnim(skelId,
										  pAnim,
										  pFocusDir->m_apChannelId,
										  pPerformance->m_fitPhaseStart,
										  &focusMaxJp,
										  mirror))
				{
					pInfoOut->m_focusOriginMaxPhaseLs = focusMaxJp.m_trans;
				}
			}

		}
		else if (pFocusDir->m_apChannelId != INVALID_STRING_ID_64)
		{
			pInfoOut->m_focusDirError = true;
			MsgWarn("Performance %s specified apChannel of %s, but it isn't found. Performance won't play.\n",
					DevKitOnly_StringIdToString(pPerformance->m_dcAnimId),
					DevKitOnly_StringIdToString(pFocusDir->m_apChannelId));
		}
	}

	if (pPerformance->m_focusPos)
	{
		const DC::FocusPosParams* pFocusPos = pPerformance->m_focusPos;
		Point focusPosLs = Point(pFocusPos->m_posLs);
		bool success = true;

		if (pFocusPos->m_apChannelId != INVALID_STRING_ID_64)
		{
			ndanim::JointParams focusJp;
			success = EvaluateChannelInAnim(skelId,
											pAnim,
											pFocusPos->m_apChannelId,
											startPhase,
											&focusJp,
											mirror);

			if (success)
				focusPosLs = focusJp.m_trans;
		}

		if (success)
		{
			pInfoOut->m_focusPosLs = focusPosLs;
			pInfoOut->m_focusPosMaxDistErr = pFocusPos->m_maxDistErr;
			pInfoOut->m_focusPosApId = pFocusPos->m_apChannelId;
			pInfoOut->m_focusPosValid = true;
		}
		else if (pFocusPos->m_apChannelId != INVALID_STRING_ID_64)
		{
			pInfoOut->m_focusPosError = true;
			MsgWarn("Performance %s specified apChannel of %s, but it isn't found. Performance won't play.\n",
					DevKitOnly_StringIdToString(pPerformance->m_dcAnimId),
					DevKitOnly_StringIdToString(pFocusPos->m_apChannelId));
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::BuildPerformance(const NavCharacter* pNavChar,
									   Vector_arg moveDirPs,
									   Performance* pPerformanceOut,
									   float startPhase,
									   bool debugDraw) const
{
	PROFILE(AI, Mp_BuildPerf);

	if (!pNavChar || !pPerformanceOut)
	{
		return false;
	}

	if (startPhase < 0.0f)
	{
		return false;
	}

	PerformanceInfo& performanceInfo = pPerformanceOut->m_info;

	bool foundInfo = false;
	if (m_cacheValid)
	{
		const bool mirror = pPerformanceOut->m_pDcData && pPerformanceOut->m_pDcData->m_mirror;
		if (LookupCachedPerformanceInfo(pPerformanceOut->m_dcAnimId,
										mirror,
										&performanceInfo,
										startPhase))
		{
			foundInfo = true;
		}
	}

	if (!foundInfo && !BuildPerformanceInfo(pNavChar, pPerformanceOut, startPhase, &performanceInfo))
	{
		return false;
	}

	pPerformanceOut->m_startingApRef = pNavChar->GetBoundFrame();
	pPerformanceOut->m_rotatedApRef = pPerformanceOut->m_startingApRef;
	pPerformanceOut->m_animSpeed = -1.0f;

	if (pPerformanceOut->m_flags & DC::kMovePerformanceFlagGesture)
		return true;

	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const ArtItemAnim* pAnim = pAnimControl->LookupAnimNoOverlays(performanceInfo.m_resolvedAnimId).ToArtItem();

	Locator alignLocAtStartPhase;
	if (!EvaluateChannelInAnim(pNavChar->GetSkeletonId(),
							   pAnim,
							   SID("align"),
							   startPhase,
							   &alignLocAtStartPhase,
							   pPerformanceOut->m_pDcData->m_mirror))
	{
		return false;
	}

	Locator inverse = Inverse(alignLocAtStartPhase);
	Locator startLoc = pNavChar->GetLocator().TransformLocator(inverse);

	pPerformanceOut->m_startingApRef.SetLocator(startLoc);

	const Quat apRefRotPs = QuatFromLookAt(moveDirPs, kUnitYAxis);
	pPerformanceOut->m_startingApRef.SetRotationPs(apRefRotPs);

	if (pPerformanceOut->m_flags & DC::kMovePerformanceFlagPlayerAvoid)
	{
		const NdGameObject* pPlayer = EngineComponents::GetNdGameInfo()->GetPlayerGameObject();
		if (!pPlayer)
			return false;

		const Locator& alignPs = pNavChar->GetLocatorPs();
		const Point myPosPs = alignPs.Pos();

		if (!pAnim)
			return false;

		Locator playerLocLs;
		if (!EvaluateChannelInAnim(pNavChar->GetSkeletonId(),
								   pAnim,
								   SID("apReference-player"),
								   0.5f,
								   &playerLocLs,
								   pPerformanceOut->m_pDcData->m_mirror))
		{
			return false;
		}

		const Locator& parentSpace = pNavChar->GetParentSpace();
		const Point playerPosMyPs = parentSpace.UntransformPoint(pPlayer->GetTranslation());
		const Vector toPlayerPs = playerPosMyPs - myPosPs;
		const Point projectedPosPs = myPosPs + (moveDirPs * Dot(toPlayerPs, moveDirPs));
		const Point basePosPs = playerPosMyPs + LimitVectorLength(projectedPosPs - playerPosMyPs, 0.0f, 0.35f);

		if (Sign(DotXz(toPlayerPs, GetLocalX(alignPs.Rot()))) != Sign(playerLocLs.Pos().X()))
			return false;

		const Locator baseLocPs	 = Locator(basePosPs, apRefRotPs);
		const Locator entryLocLs = Locator(Point(kOrigin) - Vector(performanceInfo.m_pivotLs.GetVec4()),
										   QuatFromLookAt(performanceInfo.m_entryVecLs, kUnitYAxis));
		const Locator startLocPs = baseLocPs.TransformLocator(entryLocLs);

		const Quat alignRotPs = alignPs.Rot();
		const Quat startRotPs = startLocPs.Rot();
		const Quat rotDelta	  = alignRotPs * Conjugate(startRotPs);

		const Locator rotBaseLocPs	= Locator(baseLocPs.Pos(), baseLocPs.Rot() * rotDelta);
		const Locator rotStartLocPs = rotBaseLocPs.TransformLocator(entryLocLs);
		const Locator endLocPs		= rotStartLocPs.TransformLocator(performanceInfo.m_exitLocLs);

		const Vector playerVelWs = pPlayer->GetVelocityWs();
		const Vector playerTravelCappedWs  = LimitVectorLength(playerVelWs * performanceInfo.m_duration,
															   0.0f,
															   Dist(endLocPs.Pos(), playerPosMyPs));
		const Point predictedPlayerPosWs   = pPlayer->GetTranslation() + playerTravelCappedWs;
		const Point predictedPlayerPosMyPs = parentSpace.UntransformPoint(predictedPlayerPosWs);

		if (Dist(predictedPlayerPosMyPs, endLocPs.Pos()) < pNavChar->GetCurrentNavAdjustRadius())
		{
			return false;
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugCoordAxesLabeled(rotBaseLocPs, "rotBaseLocPs"), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugLine(rotBaseLocPs.Pos(), rotStartLocPs.Pos()), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(rotStartLocPs, DevKitOnly_StringIdToString(performanceInfo.m_resolvedAnimId)), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugLine(rotBaseLocPs.Pos(), endLocPs.Pos()), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(endLocPs, "endLocPs"), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCross(predictedPlayerPosMyPs, 0.25f, kColorMagenta), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugString(predictedPlayerPosMyPs, "predictedPlayerPosMyPs", kColorMagenta), kPrimDuration1FramePauseable);
		}

		pPerformanceOut->m_startingApRef.SetLocatorPs(rotStartLocPs);
	}

	if (performanceInfo.m_duration > kSmallestFloat)
	{
		const float animSpeed = Dist(performanceInfo.m_exitLocLs.Pos(), kOrigin) / (performanceInfo.m_duration * pPerformanceOut->m_fitPhaseEnd);
		pPerformanceOut->m_animSpeed = animSpeed;
	}

	pPerformanceOut->m_rotatedApRef = pPerformanceOut->m_startingApRef;

	LimitSelfBlend(&pPerformanceOut->m_selfBlend, performanceInfo.m_duration);
	LimitSelfBlend(&pPerformanceOut->m_wallContactSbIn, performanceInfo.m_duration);
	LimitSelfBlend(&pPerformanceOut->m_wallContactSbOut, performanceInfo.m_duration);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::IsFacingPath(const NavCharacter* pNavChar,
								   const FindParams& params,
								   bool debugDraw) const
{
	Vector facing = GetLocalZ(pNavChar->GetRotation());

	const IPathWaypoints* pPathPs = params.m_pPathPs;

	const U32F waypointCount = pPathPs ? pPathPs->GetWaypointCount() : 0;
	if (!pPathPs || (waypointCount < 1))
	{
		return false;
	}

	Vector pathDirection = SafeNormalize(pPathPs->GetWaypoint(1) - pNavChar->GetTranslation(), kUnitZAxis);

	bool facingInDirectionOfPath = Dot(pathDirection, facing) > 0.8f;

	if (debugDraw)
	{
		PrimServerWrapper ps(pNavChar->GetParentSpace());
		ps.DrawArrow(pNavChar->GetTranslationPs(), pathDirection, 0.5f, facingInDirectionOfPath ? kColorGreen : kColorRed);
		ps.DrawArrow(pNavChar->GetTranslationPs(), facing, 0.5f, facingInDirectionOfPath ? kColorBlue : kColorRed);
	}

	if (!facingInDirectionOfPath)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::IsInFocus(const NavCharacter* pNavChar,
								const FindParams& params,
								const Performance* pPerformance,
								I32F tableIndex,
								bool debugDraw) const
{
	if (UNLIKELY(!pNavChar || !pPerformance))
	{
		ASSERT(false);
		return false;
	}

	const PerformanceInfo& performanceInfo = pPerformance->m_info;

	// performances with no focus requirements at all always considered in focus
	if (!performanceInfo.m_focusDirValid && !performanceInfo.m_focusPosValid)
	{
		return true;
	}

	// if this requires a focus dir/pos and we don't have one, then automatically out of focus
	if ((performanceInfo.m_focusDirValid && !params.m_focusPosValid) || (performanceInfo.m_focusPosValid && !params.m_focusPosValid))
	{
		return false;
	}

	// if this performance contains an error, it fails
	if (performanceInfo.m_focusDirError || performanceInfo.m_focusPosError)
	{
		return false;
	}

	bool dirInFocus = true;
	bool posInFocus = true;

	if (performanceInfo.m_focusDirValid)
	{
		const float maxAngleErrRad = DEGREES_TO_RADIANS(performanceInfo.m_focusDirMaxErrDeg);

		if ((pPerformance->m_fitPhaseStart > 0.0f) && params.m_focusPosValid)
		{
			const Locator startLocPs = pPerformance->m_startingApRef.GetLocatorPs();

			const Point desiredFocusPosLs = startLocPs.UntransformPoint(params.m_focusPosPs);

			const Quat rot = QuatFromAxisAngle(kUnitYAxis, maxAngleErrRad);
			const Vector focusVec0Ls = Rotate(rot, performanceInfo.m_focusDirLs);
			const Vector focusVec1Ls = Rotate(Conjugate(rot), performanceInfo.m_focusDirLs);

			const Segment focusLine0Ls = Segment(desiredFocusPosLs, desiredFocusPosLs - focusVec0Ls);
			const Segment focusLine1Ls = Segment(desiredFocusPosLs, desiredFocusPosLs - focusVec1Ls);
			const Segment travelLineLs = Segment(performanceInfo.m_focusOriginMaxPhaseLs, performanceInfo.m_focusOriginLs);

			Scalar t00, t01;
			const bool valid0 = IntersectLinesXz(focusLine0Ls, travelLineLs, t00, t01);

			Scalar t10, t11;
			const bool valid1 = IntersectLinesXz(focusLine1Ls, travelLineLs, t10, t11);

			if (!valid0 || !valid1)
			{
				dirInFocus = false;
			}
			else if (t00 < 0.0f || t10 < 0.0f)
			{
				const Vector toDesiredLs = desiredFocusPosLs - performanceInfo.m_focusOriginLs;
				const Vector compDirLs = SafeNormalize(toDesiredLs, performanceInfo.m_focusDirLs);

				const float dotP = Dot(performanceInfo.m_focusDirLs, compDirLs);
				const float angleDiffRad = SafeAcos(dotP);
				
				dirInFocus = angleDiffRad < maxAngleErrRad;
			}
			else
			{
				const Scalar tMin = Min(t01, t11);
				const Scalar tMax = Max(t01, t11);

				if (FloatRangeOverlaps(0.0f, 1.0f, tMin, tMax))
				{
					const float coverageMin = Limit01(tMin);
					const float coverageMax = Limit01(tMax);
					const float coverageFactor = coverageMax - coverageMin;

					dirInFocus = coverageFactor >= 0.45f;
				}
				else
				{
					dirInFocus = false;
				}
			}

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				PrimServerWrapper ps(pNavChar->GetParentSpace());
				ps.SetDuration(kPrimDuration1FrameAuto);

				const Color clr = dirInFocus ? kColorGreen : kColorRed;
				const Vector focusVec0Ps = startLocPs.TransformVector(focusVec0Ls);
				const Vector focusVec1Ps = startLocPs.TransformVector(focusVec1Ls);
				ps.DrawSector(params.m_focusPosPs, focusVec0Ps, focusVec1Ps, clr);

				if (valid0 && valid1)
				{
					const Scalar tMin = Min(t01, t11);
					const Scalar tMax = Max(t01, t11);

					const Point posMinLs = Lerp(travelLineLs.a, travelLineLs.b, tMin);
					const Point posMaxLs = Lerp(travelLineLs.a, travelLineLs.b, tMax);

					const Point posMinPs = startLocPs.TransformPoint(posMinLs);
					const Point posMaxPs = startLocPs.TransformPoint(posMaxLs);

					const Point travel0Ps = startLocPs.TransformPoint(travelLineLs.a);
					const Point travel1Ps = startLocPs.TransformPoint(travelLineLs.b);
					ps.DrawLine(travel0Ps, travel1Ps, dirInFocus ? kColorGreen : kColorRed);

					if (t00 < 0.0f || t10 < 0.0f)
					{
						const Point focusOriginPs = startLocPs.TransformPoint(performanceInfo.m_focusOriginLs);

						//ps.DrawSphere(params.m_focusPosPs, 0.3f, kColorCyan);
						//ps.DrawString(params.m_focusPosPs, "desiredFocusPos");
						//ps.DrawSphere(focusOriginPs, 0.3f, kColorMagenta);
						//ps.DrawString(focusOriginPs, "focusOriginPs");

						const Vector toDesiredLs = desiredFocusPosLs - performanceInfo.m_focusOriginLs;
						const Vector compDirLs = SafeNormalize(toDesiredLs, performanceInfo.m_focusDirLs);

						const Point travelMidPs = startLocPs.TransformPoint(AveragePos(travelLineLs.a, travelLineLs.b));
						const Vector compDirPs = startLocPs.TransformVector(compDirLs);

						ps.DrawCross(travelMidPs, 0.2f, dirInFocus ? kColorGreen : kColorRed);

						ps.DrawArrow(focusOriginPs, focusVec0Ps, 0.5f, clr);
						ps.DrawArrow(focusOriginPs, focusVec1Ps, 0.5f, clr);
						ps.DrawArrow(focusOriginPs, compDirPs, 0.4f, dirInFocus ? kColorGreenTrans : kColorRedTrans);

						StringBuilder<256> desc;
						desc.format("%d: %s", tableIndex, DevKitOnly_StringIdToString(performanceInfo.m_resolvedAnimId));

						ps.DrawString(travelMidPs, desc.c_str(), kColorCyan, 0.5f);
					}
					else
					{
						ps.DrawArrow(params.m_focusPosPs, -focusVec0Ps, 0.5f, clr);
						ps.DrawArrow(params.m_focusPosPs, -focusVec1Ps, 0.5f, clr);

						ps.DrawCross(posMinPs, 0.1f, kColorCyanTrans);
						ps.DrawCross(posMaxPs, 0.1f, kColorCyanTrans);
						ps.SetLineWidth(3.0f);

						float coveragePercent = 0.0f;

						if (FloatRangeOverlaps(0.0f, 1.0f, tMin, tMax))
						{
							const float coverageMin = Limit01(tMin);
							const float coverageMax = Limit01(tMax);
							const float coverageFactor = coverageMax - coverageMin;

							coveragePercent = coverageFactor * 100.0f;

							const Point coverageMinPs = Lerp(travel0Ps, travel1Ps, coverageMin);
							const Point coverageMaxPs = Lerp(travel0Ps, travel1Ps, coverageMax);

							ps.DrawCross(coverageMinPs, 0.1f, kColorCyanTrans);
							ps.DrawCross(coverageMaxPs, 0.1f, kColorCyanTrans);

							if (coverageMin < tMin)
							{
								ps.DrawLine(posMinPs, travel0Ps, kColorCyanTrans);
							}
							
							ps.DrawLine(coverageMinPs, coverageMaxPs, kColorCyan);

							if (tMax > coverageMax)
							{
								ps.DrawLine(travel1Ps, posMaxPs, kColorCyanTrans);
							}
						}
						else
						{
							ps.DrawLine(posMinPs, posMaxPs, kColorCyanTrans);
						}

						//ps.DrawString(posMinPs, StringBuilder<256>("%0.4f", float(tMin)).c_str(), kColorCyanTrans, 0.5f);
						//ps.DrawString(posMaxPs, StringBuilder<256>("%0.4f", float(tMax)).c_str(), kColorCyanTrans, 0.5f);

						StringBuilder<256> desc;
						desc.format("%d: %s @ %0.1f%%",
									tableIndex,
									DevKitOnly_StringIdToString(performanceInfo.m_resolvedAnimId),
									coveragePercent);

						ps.DrawString(AveragePos(posMinPs, posMaxPs), desc.c_str(), kColorCyan, 0.5f);
					}
				}
			}
		}
		else
		{
			const Locator startLocPs = pPerformance->m_startingApRef.GetLocatorPs().TransformLocator(Locator(performanceInfo.m_focusOriginLs));
			const Vector animDirPs = startLocPs.TransformVector(performanceInfo.m_focusDirLs);
			const Vector myFocusDirPs = SafeNormalize(params.m_focusPosPs - startLocPs.Pos(), animDirPs);
			const float focusDot = Dot(animDirPs, myFocusDirPs);
			const float minFocusDot = Cos(maxAngleErrRad);

			// Is target in focus?
			if (focusDot < minFocusDot)
			{
				dirInFocus = false;
			}

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				const Point startPosPs = startLocPs.Pos();
				const Vector vo = Vector(0.0f, 0.01f, 0.0f);
				const DebugPrimTime tt = kPrimDuration1FrameAuto;

				PrimServerWrapper ps(pNavChar->GetParentSpace());
				ps.SetDuration(tt);
				ps.EnableWireFrame();

				const float testRadius = Sin(maxAngleErrRad);

				ps.DrawCircle(startPosPs + (animDirPs * minFocusDot), animDirPs, testRadius, dirInFocus ? kColorGreen : kColorRed);
				ps.DrawSector(startPosPs + vo, animDirPs, myFocusDirPs, dirInFocus ? kColorGreenTrans : kColorRedTrans, 1.0f);
				ps.DrawArrow(startPosPs + vo, myFocusDirPs, 0.5f, kColorCyan);
				ps.DrawArrow(startPosPs + vo, animDirPs, 0.5f, dirInFocus ? kColorGreen : kColorRed);
				ps.DrawCross(params.m_focusPosPs);

				const Vector zeroVector(kZero);
				Scalar lenLeg0, lenLeg1;
				const Vector leg0 = SafeNormalize(animDirPs, zeroVector, lenLeg0);
				const Vector leg1 = SafeNormalize(myFocusDirPs, zeroVector, lenLeg1);

				const float interpLength = Lerp((float)lenLeg0, (float)lenLeg1, 0.5f);

				const SphericalCoords s0 = SphericalCoords::FromVector(leg0);
				const SphericalCoords s1 = SphericalCoords::FromVector(leg1);

				const Angle s0Theta = Angle(s0.Theta());
				const Angle s0Phi = Angle(s0.Phi());
				const Angle s1Theta = Angle(s1.Theta());
				const Angle s1phi = Angle(s1.Phi());

				const Angle interpTheta = Lerp(s0Theta, s1Theta, 0.5f);
				const Angle interpPhi = Lerp(s0Phi, s1phi, 0.5f);
				const SphericalCoords si = SphericalCoords::FromThetaPhi(interpTheta.ToDegrees(), interpPhi.ToDegrees());

				Vector newVec = si.ToUnitVector() * interpLength;

				StringBuilder<256> desc;
				desc.format("%d: %s", tableIndex, DevKitOnly_StringIdToString(performanceInfo.m_resolvedAnimId));
				ps.DrawString(startPosPs + newVec + vo, desc.c_str(), kColorYellow, g_msgOptions.m_conScale);
			}
		}
	}

	if (performanceInfo.m_focusPosValid)
	{
		const Locator startLocPs	= pPerformance->m_startingApRef.GetLocatorPs();
		const Point animPosPs		= startLocPs.TransformPoint(performanceInfo.m_focusPosLs);
		const Point myFocusPosPs	= params.m_focusPosPs;
		const float distErr			= Dist(myFocusPosPs, animPosPs);
		const float maxDistErr		= performanceInfo.m_focusPosMaxDistErr;

		if (distErr > maxDistErr)
		{
			posInFocus = false;
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			const Point startPosPs = startLocPs.Pos();
			const Vector vo = Vector(0.0f, 0.01f, 0.0f);
			const DebugPrimTime tt = kPrimDuration1FramePauseable;

			PrimServerWrapper ps(pNavChar->GetParentSpace());
			ps.SetDuration(tt);
			ps.EnableWireFrame();

			StringBuilder<256> desc;
			desc.format("%d: %s", tableIndex, DevKitOnly_StringIdToString(performanceInfo.m_resolvedAnimId));

			ps.DrawString(animPosPs + vo, desc.c_str(), kColorCyan, g_msgOptions.m_conScale);
			ps.DrawSphere(animPosPs + vo, maxDistErr, posInFocus ? kColorGreen : kColorRed);
			ps.DrawArrow(animPosPs + vo, myFocusPosPs, 0.5f, posInFocus ? kColorGreenTrans : kColorRedTrans);
			ps.DrawCross(myFocusPosPs + vo, 0.2f, posInFocus ? kColorGreenTrans : kColorRedTrans);
		}
	}

	const bool inFocus = posInFocus && dirInFocus;

	return inFocus;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::RequiredSpeedMet(const NavCharacter* pNavChar,
									   const FindParams& params,
									   const Performance* pPerformance,
									   bool debugDraw) const
{
	if (!pNavChar || !pPerformance)
		return false;

	const float charSpeed = params.m_speed;
	const float animSpeed = pPerformance->m_info.m_initialSpeed;
	const float minSpeed = pPerformance->m_minSpeedFactor * animSpeed;

	return charSpeed >= minSpeed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::TryToFitPerformance(const NavCharacter* pNavChar,
										  const FindParams& params,
										  Performance* pPerformance,
										  bool debugDraw /* = false */) const
{
	PROFILE(AI, Mp_TryToFitPerf);

	const Locator adjParentSpace = Locator(pNavChar->GetParentSpace().Pos() + Vector(0.0f, 0.01f, 0.0f), pNavChar->GetParentSpace().Rot());
	PrimServerWrapper ps(adjParentSpace);

	const IPathWaypoints* pPathPs = params.m_pPathPs;

	const U32F waypointCount = pPathPs ? pPathPs->GetWaypointCount() : 0;
	if (!pPerformance || !pPathPs || (waypointCount < 2))
	{
		return false;
	}

	if (pPerformance->m_flags & DC::kMovePerformanceFlagGesture)
	{
		return true;
	}

	const AnimControl* pAnimControl				= pNavChar->GetAnimControl();
	const AnimStateLayer* pStateLayer			= pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurrentInstance	= pStateLayer ? pStateLayer->CurrentStateInstance() : nullptr;
	const Transform animDeltaTweak				= (pCurrentInstance && pCurrentInstance->IsAnimDeltaTweakEnabled()) ? pCurrentInstance->GetAnimDeltaTweakTransform() : Transform(kIdentity);

	const Point pivotLs		= (pPerformance->m_info.m_pivotLs - Point(kOrigin)) * animDeltaTweak + Point(kOrigin);
	const Point exitPosLs	= (pPerformance->m_info.m_exitLocLs.Pos() - Point(kOrigin)) * animDeltaTweak + Point(kOrigin);
	const Locator exitLocLs	= Locator(exitPosLs, pPerformance->m_info.m_exitLocLs.Rot());

	const Vector entryVecLs = pPerformance->m_info.m_entryVecLs;
	const Vector exitVecLs = pPerformance->m_info.m_exitVecLs;

	const BoundFrame startingApRef	= pPerformance->m_startingApRef;
	pPerformance->m_rotatedApRef	= startingApRef;

	const Locator startingApRefPs	= startingApRef.GetLocatorPs();
	const Locator endLocPs			= startingApRefPs.TransformLocator(exitLocLs);
	const Point endPosPs			= endLocPs.Pos();
	const Locator invExitLocLs		= Inverse(exitLocLs);

	Point closestPosPs;
	const float searchRadius		= Dist(exitLocLs.Pos(), kOrigin);
	const I32F closestLegIndex		= NavUtil::FindPathSphereIntersection(pPathPs, startingApRefPs.Pos(), searchRadius, &closestPosPs);

	if (closestLegIndex < 0)
	{
		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			ps.DrawString(endPosPs + Vector(0.0f, 0.2f, 0.0f), StringBuilder<256>("%s : No Path Intersection", DevKitOnly_StringIdToString(pPerformance->m_info.m_resolvedAnimId)).c_str(), kColorRedTrans, 0.5f);
		}
		return false;
	}

	const bool stoppingPerformance	= pPerformance->m_flags & DC::kMovePerformanceFlagStoppingPerformance;
	const bool singlePivotFitting	= pPerformance->m_flags & DC::kMovePerformanceFlagSinglePivotFitting;

	const Vector closestLegPs		= VectorXz(pPathPs->GetWaypoint(closestLegIndex + 1) - pPathPs->GetWaypoint(closestLegIndex));
	const Vector desiredFaceDirPs	= VectorXz(pNavChar->GetFacePositionPs() - closestPosPs);
	const Vector desiredExitDirPs	= stoppingPerformance ? SafeNormalize(desiredFaceDirPs, kZero) : SafeNormalize(closestLegPs, kZero);

	const Point pivotPs				= startingApRefPs.TransformPoint(pivotLs);
	const Vector pivotToPathPosPs	= SafeNormalize(closestPosPs - pivotPs, kUnitZAxis);
	const Vector pivotToEndPosPs	= SafeNormalize(endPosPs - pivotPs, kUnitZAxis);

	const Quat rotAdjustPs			= QuatFromVectors(pivotToEndPosPs, pivotToPathPosPs);
	const Quat pivotToEndLookAtPs	= QuatFromLookAt(endPosPs - pivotPs, kUnitYAxis);
	const Locator pivotBase			= Locator(pivotPs, pivotToEndLookAtPs);
	const Locator rotatedPivotBase	= Locator(pivotPs, Normalize(rotAdjustPs * pivotToEndLookAtPs));
	const Locator apRefInPivotSpace	= pivotBase.UntransformLocator(startingApRefPs);
	const Locator rotatedApRefPs = rotatedPivotBase.TransformLocator(apRefInPivotSpace);

	const Vector entryVecPs			= startingApRefPs.TransformVector(entryVecLs);
	const Vector exitVecPs			= rotatedApRefPs.TransformVector(exitVecLs);

	const float entryDot			= Dot(entryVecPs, params.m_moveDirPs);

	// counter rotate to try and exit moving the right direction
	const Quat counterRotPs			= Normalize(QuatFromVectors(exitVecPs, desiredExitDirPs));
	const Locator adjEndLocPs		= Locator(closestPosPs, QuatFromLookAt(exitVecPs, kUnitYAxis));
	const Locator finalEndLocPs		= Locator(closestPosPs, Normalize(adjEndLocPs.Rot() * counterRotPs));

	const Locator finalRotatedApRefPs = singlePivotFitting ? rotatedApRefPs : finalEndLocPs.TransformLocator(invExitLocLs);

	const Vector finalExitVecPs = finalRotatedApRefPs.TransformVector(exitVecLs);

	const float pivotAngleErrDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(pivotToEndPosPs, pivotToPathPosPs)));
	const float exitVecErrDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(finalExitVecPs, desiredExitDirPs)));

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		ps.EnableHiddenLineAlpha();

		ps.DrawLine(startingApRefPs.Pos(), pivotPs, kColorOrange, kColorOrange);
		ps.DrawLine(pivotPs, endPosPs, kColorOrange);

		ps.EnableWireFrame();
		ps.DrawSphere(pivotPs, 0.05f, kColorOrange);

		const float exitDot = Dot(finalExitVecPs, desiredExitDirPs);
		if (exitDot > 0.0f)
		{
			const Color pivotColor = (pivotAngleErrDeg > pPerformance->m_maxPivotErrDeg) ? kColorRed : kColorGreen;
			ps.DrawString(pivotPs + Vector(0.0f, 0.2f, 0.0f), StringBuilder<128>("%0.1fdeg", pivotAngleErrDeg).c_str(), pivotColor, 0.75f);

			ps.SetLineWidth(2.5f);
			ps.DrawLine(pivotPs, finalEndLocPs.Pos(), kColorOrange);

			if (Dist(pivotLs, kOrigin) > 0.0f)
			{
				ps.DrawArrow(pivotPs, pivotToEndPosPs, 0.5f, kColorOrange);
				ps.DrawArrow(pivotPs, pivotToPathPosPs, 0.5f, kColorBlue);
			}

			ps.SetLineWidth(1.0f);
			ps.DrawLine(endPosPs, closestPosPs + Vector(0.0f, 0.2f, 0.0f));
			ps.DrawCross(closestPosPs + Vector(0.0f, 0.2f, 0.0f), 0.1f, kColorWhite);

			const Locator startingExitLocPs = startingApRefPs.TransformLocator(exitLocLs);

			ps.DrawCoordAxes(startingExitLocPs);
			ps.DrawString(startingExitLocPs.Pos(), "S");

			ps.DrawCoordAxes(finalEndLocPs);
			ps.DrawString(finalEndLocPs.Pos(), "F");

			{
				const Vector vo2 = Vector(0.0f, 0.25f, 0.0f);
				const Color exitColor = exitVecErrDeg > pPerformance->m_maxExitErrDeg ? kColorRed : kColorGreen;
				ps.DrawString(finalEndLocPs.Pos() + vo2, StringBuilder<256>("%0.1fdeg", exitVecErrDeg).c_str(), exitColor, 0.75f);

				if (!singlePivotFitting)
				{
					ps.DrawArrow(endLocPs.Pos(), exitVecPs + vo2, 0.5f, kColorMagenta);
					ps.DrawString(endLocPs.Pos() + exitVecPs + vo2, "exitVecPs", kColorMagenta, 0.5f);
				}

				ps.DrawArrow(finalEndLocPs.Pos(), finalExitVecPs + vo2, 0.5f, kColorCyan);
				ps.DrawString(finalEndLocPs.Pos() + finalExitVecPs + vo2, "finalExitVecPs", kColorCyan, 0.5f);
			}

			ps.DrawArrow(pNavChar->GetTranslationPs(), entryVecPs, 0.5f, kColorOrange);
			ps.DrawString(pNavChar->GetTranslationPs() + entryVecPs, "entryVecPs", kColorOrange, 0.5f);

			ps.DrawCoordAxes(startingApRefPs, 0.2f);
			ps.DrawCoordAxes(finalRotatedApRefPs, 0.35f);
			ps.DrawString(startingApRefPs.Pos(), "apRef-S", kColorWhite, 0.6f);
			ps.DrawString(finalRotatedApRefPs.Pos(), "apRef-F", kColorWhite, 0.6f);
		}
		else
		{
			ps.DrawString(endPosPs, StringBuilder<256>("%s (%0.2f)", DevKitOnly_StringIdToString(pPerformance->m_info.m_resolvedAnimId), exitDot).c_str(), kColorGrayTrans, 0.5f);
		}
	}

	const float distToPathEnd	= pPathPs->RemainingPathDistFromPosXz(finalEndLocPs.Pos());
	const float goalRadius		= pNavChar->GetMotionConfig().m_minimumGoalRadius;

	if (stoppingPerformance)
	{
		// doesn't stop close enough to the goal
		if (distToPathEnd > goalRadius)
		{
			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				ps.DrawString(finalEndLocPs.Pos() + Vector(0.0f, 0.2f, 0.0f), StringBuilder<256>("%s : Stop Misses Goal", DevKitOnly_StringIdToString(pPerformance->m_info.m_resolvedAnimId)).c_str(), kColorRedTrans, 0.5f);
			}
			return false;
		}
	}
	else if (distToPathEnd < (params.m_minRemainingPathDist + goalRadius))
	{
		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			ps.DrawString(finalEndLocPs.Pos() + Vector(0.0f, 0.2f, 0.0f), StringBuilder<256>("%s : Too Close to Path End", DevKitOnly_StringIdToString(pPerformance->m_info.m_resolvedAnimId)).c_str(), kColorRedTrans, 0.5f);
		}
		// ends with too little path remaining
		return false;
	}

	if (pivotAngleErrDeg > pPerformance->m_maxPivotErrDeg)
	{
		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			ps.DrawString(finalEndLocPs.Pos() + Vector(0.0f, 0.2f, 0.0f), StringBuilder<256>("%s : Pivot Angle Err", DevKitOnly_StringIdToString(pPerformance->m_info.m_resolvedAnimId)).c_str(), kColorRedTrans, 0.5f);
		}
		return false;
	}

	if (exitVecErrDeg > pPerformance->m_maxExitErrDeg)
	{
		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			ps.DrawString(finalEndLocPs.Pos() + Vector(0.0f, 0.2f, 0.0f), StringBuilder<256>("%s : Exit Vec Angle Err", DevKitOnly_StringIdToString(pPerformance->m_info.m_resolvedAnimId)).c_str(), kColorRedTrans, 0.5f);
		}
		return false;
	}

	BoundFrame rotatedApRef = startingApRef;
	rotatedApRef.SetLocatorPs(finalRotatedApRefPs);
	pPerformance->m_rotatedApRef = rotatedApRef;

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		ps.DrawString(finalEndLocPs.Pos() + Vector(0.0f, 0.2f, 0.0f), StringBuilder<256>("%s", DevKitOnly_StringIdToString(pPerformance->m_info.m_resolvedAnimId)).c_str(), kColorGreen, 0.75f);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::GetAnimContactLocPs(Locator* pAnimContactLocPs,
										  const NavCharacter* pNavChar,
										  StringId64 animId,
										  const DC::WallContactParams& wallContact,
										  const Locator& startingLocPs,
										  bool mirror) const
{
	ASSERT(pAnimContactLocPs);
	ASSERT(pNavChar);

	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	if (!pAnimControl)
	{
		return false;
	}

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();
	if (!pAnim)
	{
		return false;
	}

	const SkeletonId skelId = pNavChar->GetSkeletonId();
	if (skelId == INVALID_SKELETON_ID)
	{
		return false;
	}

#if 0	//TODO(MB) Fake anim contact loc (until animations with apReference-wall are available)
	const Locator startingLocPs = pPerformance->m_startingApRef.GetLocatorPs();

	*pAnimContactLocPs = startingLocPs;
	pAnimContactLocPs->SetRot(QuatFromLookAt(-GetLocalX(startingLocPs.Rot()), kUnitYAxis));
	pAnimContactLocPs->SetPos(startingLocPs.Pos() + GetLocalZ(startingLocPs.Rot()) * 1.5f + GetLocalX(startingLocPs.Rot()) * 0.5f);
#else	//TODO(MB) Fake anim contact loc
	Locator animContactLocAs;
	Locator animContactAlignLocLs;
	if (!EvaluateChannelInAnim(skelId, pAnim, SID("apReference-wall"), wallContact.m_startPhase, &animContactLocAs, mirror) ||
		!EvaluateChannelInAnim(skelId, pAnim, SID("align"), wallContact.m_startPhase, &animContactAlignLocLs, mirror))
	{
		return false;
	}

	const Locator animContactLocLs = animContactAlignLocLs.TransformLocator(animContactLocAs);

	*pAnimContactLocPs = startingLocPs.TransformLocator(animContactLocLs);
#endif	//TODO(MB) Fake anim contact loc

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::CheckContactPerformanceContact(const NavCharacter* pNavChar,
													 Performance* pPerformance,
													 bool debugDraw) const
{
	PROFILE(AI, Mp_CheckContactPerf);

	AI_ASSERT(pNavChar);
	AI_ASSERT(pPerformance);
	AI_ASSERT(pPerformance->m_wallContact);

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const float kProbeCosAngleLimit = Cos(DegreesToRadians(15.0f));

	m_raycastJob.Wait();
	if (!m_raycastJob.IsValid())
	{
		return false;
	}

	const NavControl* pNavControl = pNavChar->GetNavControl();
	if (!pNavControl)
	{
		m_raycastJob.Close();
		return false;
	}

	const NavMesh* pNavMesh = pNavControl->GetNavMesh();
	if (!pNavMesh)
	{
		m_raycastJob.Close();
		return false;
	}

	// ==== Find the anim contact location ====

	Locator animContactLocPs;
	if (!GetAnimContactLocPs(&animContactLocPs,
							 pNavChar,
							 pPerformance->m_info.m_resolvedAnimId,
							 *pPerformance->m_wallContact,
							 pPerformance->m_startingApRef.GetLocatorPs(),
							 pPerformance->m_pDcData->m_mirror))
	{
		m_raycastJob.Close();
		return false;
	}

	// ==== Find the wall contact location ====

	const U32F numContactRays = m_raycastJob.NumProbes();
	U32F rayId = numContactRays + 1;

	// Find the rayId by matching the (unique) animId
	for (U32F ii = 0; ii < numContactRays; ++ii)
	{
		if (m_contactRayAnimIds[ii] == pPerformance->m_info.m_resolvedAnimId)
		{
			if (m_raycastJob.IsContactValid(ii, 0))
			{
				rayId = ii;
			}

			break;
		}
	}

	// Display wall probe
	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		const Vector verticalOffsetPs(0.0f, 0.05f, 0.0f);
		const Point startPs = m_raycastJob.GetProbeStart(rayId) + verticalOffsetPs;
		const Vector dirPs  = m_raycastJob.GetProbeDir(rayId);
		const Color color   = rayId < numContactRays ? kColorRed : kColorGreen;
		g_prim.Draw(DebugArrow(startPs, dirPs, color), Seconds(2.0f));
	}

	if (rayId >= numContactRays)
	{
		m_raycastJob.Close();
		return false;
	}

	// ==== Test the wall contact location ====

	const Vector wallDirPs       = -GetLocalZ(animContactLocPs.GetRotation());
	const Vector contactNormalPs = m_raycastJob.GetContactNormal(rayId, 0);
	const Point contactPointPs   = m_raycastJob.GetContactPoint(rayId, 0);

	const float wallAngleDot = Dot(contactNormalPs, -wallDirPs);
	const bool wallAngleOk   = wallAngleDot >= kProbeCosAngleLimit;

	// Display wall normal
	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		const Vector verticalOffsetPs(0.0f, 0.05f, 0.0f);
		const Point startPs = contactPointPs + verticalOffsetPs;
		const Vector dirPs  = contactNormalPs * 0.3f;
		const Color color   = wallAngleOk ? kColorGreenTrans : kColorRedTrans;
		g_prim.Draw(DebugArrow(startPs, dirPs, color), Seconds(2.0f));
	}

	if (!wallAngleOk)
	{
		m_raycastJob.Close();
		return false;
	}

	// ==== Build the rotated apRef ====

	const Locator wallContactLocPs(contactPointPs, QuatFromLookAt(contactNormalPs, kUnitYAxis));

	const Locator startingLocPs              = pPerformance->m_startingApRef.GetLocatorPs();
	const Locator startLocInAnimContactSpace = animContactLocPs.UntransformLocator(startingLocPs);
	const Locator adjustedStartingLocPs      = wallContactLocPs.TransformLocator(startLocInAnimContactSpace);

	pPerformance->m_rotatedApRef = adjustedStartingLocPs;

	// Display anim and wall contacts and rotated ap
	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		const Locator animContactLocLs = startingLocPs.UntransformLocator(animContactLocPs);
		const Locator adjustedAnimContactLocPs = adjustedStartingLocPs.TransformLocator(animContactLocLs);
		const Vector startToAdjustedWs = adjustedAnimContactLocPs.Pos() - animContactLocPs.Pos();
		g_prim.Draw(DebugArrow(animContactLocPs.Pos(), startToAdjustedWs, kColorOrange, 0.25f), Seconds(2.0f));
		g_prim.Draw(DebugCoordAxes(animContactLocPs), Seconds(2.0f));
		g_prim.Draw(DebugString(animContactLocPs.Pos(), "Anim", kColorWhite, g_msgOptions.m_conScale), Seconds(2.0f));
		g_prim.Draw(DebugCoordAxes(adjustedAnimContactLocPs), Seconds(2.0f));
		g_prim.Draw(DebugString(adjustedAnimContactLocPs.Pos(), "Wall", kColorWhite, g_msgOptions.m_conScale), Seconds(2.0f));

		g_prim.Draw(DebugCoordAxes(adjustedStartingLocPs), Seconds(2.0f));
		g_prim.Draw(DebugString(adjustedStartingLocPs.Pos(), "Rotated", kColorWhite, g_msgOptions.m_conScale), Seconds(2.0f));
	}

	m_raycastJob.Close();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::PerformanceHasClearMotion(const NavCharacter* pNavChar,
												const Performance* pPerformance,
												const DC::SelfBlendParams* pSelfBlendIn,
												const DC::SelfBlendParams* pSelfBlendOut,
												bool debugDraw) const
{
	PROFILE(AI, Mp_PerfHasClearMotion);

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const float kStartPhase = pPerformance->m_startPhase;
	const float kEndPhase = pPerformance->m_fitPhaseEnd;

	const NavControl* pNavControl = pNavChar->GetNavControl();
	if (!pNavControl)
	{
		return false;
	}

	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	if (!pAnimControl)
	{
		return false;
	}

	const NavMesh* pNavMesh = pNavControl->GetNavMesh();
	if (!pNavMesh)
	{
		return false;
	}

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(pPerformance->m_info.m_resolvedAnimId).ToArtItem();
	if (!pAnim)
	{
		return false;
	}

	const AnimStateLayer* pStateLayer = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurrentInstance = pStateLayer ? pStateLayer->CurrentStateInstance() : nullptr;
	const Transform animDeltaTweak = (pCurrentInstance && pCurrentInstance->IsAnimDeltaTweakEnabled()) ? pCurrentInstance->GetAnimDeltaTweakTransform() : Transform(kIdentity);

	const Locator& parentSpace = pNavMesh->GetParentSpace();

	const SkeletonId skelId = pNavChar->GetSkeletonId();

	const float animDuration = pAnim->m_pClipData->m_fNumFrameIntervals * pAnim->m_pClipData->m_secondsPerFrame;
	const float phaseStep = pAnim->m_pClipData->m_phasePerFrame;

	float evalPhase = kStartPhase;
	bool valid = true;
	bool doSelfBlend = false;

	const bool apEntryStyle = (pPerformance->m_flags & DC::kMovePerformanceFlagPlayerAvoid) != 0;

	const Locator* pStartingApRefPs	= &pPerformance->m_startingApRef.GetLocatorPs();
	const Locator* pFinalApRefPs = &pPerformance->m_rotatedApRef.GetLocatorPs();
	float selfBlendStartPhase = 1.0f;
	float selfBlendEndPhase = 1.0f;

	const DC::SelfBlendParams* pSelfBlend = nullptr;

	const Locator identLoc = Locator(kIdentity);
	Locator prevLocPs = pNavChar->GetLocatorPs();

	int index = 0;
	do
	{
		const DC::SelfBlendParams* pSelfBlendPrev = pSelfBlend;

		if (pSelfBlendOut && (evalPhase >= pSelfBlendOut->m_phase))
		{
			pSelfBlend			= pSelfBlendOut;
			pStartingApRefPs	= &pPerformance->m_rotatedApRef.GetLocatorPs();
			pFinalApRefPs		= &pPerformance->m_startingApRef.GetLocatorPs();
		}
		else
		{
			pSelfBlend			= pSelfBlendIn;
			pStartingApRefPs	= &pPerformance->m_startingApRef.GetLocatorPs();
			pFinalApRefPs		= &pPerformance->m_rotatedApRef.GetLocatorPs();
		}

		if (pSelfBlend != pSelfBlendPrev)
		{
			doSelfBlend			= pSelfBlend && (pSelfBlend->m_phase >= 0.0f);
			selfBlendStartPhase	= doSelfBlend ? Max(pSelfBlend->m_phase, kStartPhase) : -1.0f;
			selfBlendEndPhase	= doSelfBlend ? Limit01(selfBlendStartPhase + pSelfBlend->m_time / animDuration) : -1.0f;
		}

		const float selfBlendProgressLinear	= doSelfBlend ? LerpScale(selfBlendStartPhase, selfBlendEndPhase, 0.0f, 1.0f, evalPhase) : 0.0f;
		const float selfBlendValue			= doSelfBlend ? CalculateCurveValue(selfBlendProgressLinear, pSelfBlend->m_curve) : 0.0f;
		const Locator blendedApRefPs		= doSelfBlend ? Lerp(*pStartingApRefPs, *pFinalApRefPs, selfBlendValue) : *pStartingApRefPs;

		Locator nextLocLs = identLoc;
		if (!FindAlignFromApReferenceCached(skelId,
											pAnim,
											Min(evalPhase, kEndPhase),
											identLoc,
											SID("apReference"),
											false,
											&nextLocLs))
		{
			return false;
		}

		const Point tweakedPosLs = (nextLocLs.Pos() - kOrigin) * animDeltaTweak + Point(kOrigin);
		nextLocLs.SetPos(tweakedPosLs);

		Color lineClr = kColorGreen;

		const Locator nextLocStartPs = pStartingApRefPs->TransformLocator(nextLocLs);
		const Locator nextLocEndPs = pFinalApRefPs->TransformLocator(nextLocLs);
		const Locator nextLocPs = doSelfBlend ? Lerp(nextLocStartPs, nextLocEndPs, selfBlendValue) : nextLocStartPs;

		const Point prevPosPs = prevLocPs.Pos();
		const Point nextPosPs = nextLocPs.Pos();

		if (apEntryStyle) // use ap-entry style clear motion testing
		{
			const Point prevPosWs = parentSpace.TransformPoint(prevPosPs);
			const Point nextPosWs = parentSpace.TransformPoint(nextPosPs);

			NavMesh::ProbeParams probeParams;
			probeParams.m_start = pNavMesh->WorldToLocal(prevPosWs);
			probeParams.m_move = pNavMesh->WorldToLocal(nextPosWs - prevPosWs);
			probeParams.m_pStartPoly = nullptr;

			pNavMesh->ProbeLs(&probeParams);

			if (probeParams.m_hitEdge)
			{
				bool wasValid = valid;
				valid = false;
				lineClr = kColorRed;

				if (probeParams.m_pHitPoly && probeParams.m_pHitPoly->IsLink())
				{
					if (pNavControl->StaticClearLineOfMotionPs(prevPosPs, nextPosPs))
					{
						lineClr = kColorBlue;
						valid = true;
					}
				}
			}

			if (valid && !pNavControl->BlockersOnlyClearLineOfMotion(prevPosPs, nextPosPs))
			{
				valid = false;
				lineClr = kColorOrange;
			}
		}
		else
		{
			// full nav map clear motion testing
			if (!pNavControl->ClearLineOfMotionPs(prevPosPs, nextPosPs))
			{
				valid = false;
				lineClr = kColorRed;
			}
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			const Vector verticalOffsetPs = Vector(0.0f, 0.15f, 0.0f);
			PrimServerWrapper ps = PrimServerWrapper(parentSpace);
			ps.SetLineWidth(4.0f);
			ps.DrawLine(prevPosPs + verticalOffsetPs, nextPosPs + verticalOffsetPs, lineClr);

			index++;
			if (index % 3 == 0)
			{
				ps.DrawCross(prevPosPs, 0.1f, lineClr);
				char buf[16];
				sprintf(buf, "%.2f", evalPhase);
				ps.DrawString(prevPosPs, buf, lineClr);
			}
		}
		else if (!valid)
		{
			return false;
		}

		prevLocPs = nextLocPs;
		evalPhase += phaseStep;
	} while (evalPhase <= kEndPhase);

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MovePerformance::IsPathDeviationWithinRange(const NavCharacter* pNavChar,
												 const Performance* pPerformance,
												 const FindParams& params,
												 const DC::SelfBlendParams* pSelfBlendIn,
												 const DC::SelfBlendParams* pSelfBlendOut /* = nullptr */,
												 bool debugDraw /* = false */) const
{
	PROFILE(AI, Mp_IsPathDeviationWithinRange);

	const IPathWaypoints* pPathPs = params.m_pPathPs;

	if (!pNavChar || !pPerformance || !pPathPs)
	{
		return false;
	}

	const float kStartPhase = pPerformance->m_startPhase;
	const float kEndPhase = pPerformance->m_exitPhase;

	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	if (!pAnimControl)
	{
		return false;
	}

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(pPerformance->m_info.m_resolvedAnimId).ToArtItem();
	if (!pAnim)
	{
		return false;
	}

	const AnimStateLayer* pStateLayer = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurrentInstance = pStateLayer ? pStateLayer->CurrentStateInstance() : nullptr;
	const Transform animDeltaTweak = (pCurrentInstance && pCurrentInstance->IsAnimDeltaTweakEnabled()) ? pCurrentInstance->GetAnimDeltaTweakTransform() : Transform(kIdentity);

	const Locator& parentSpace = pNavChar->GetParentSpace();
	const SkeletonId skelId = pNavChar->GetSkeletonId();

	const float animDuration	= pAnim->m_pClipData->m_fNumFrameIntervals * pAnim->m_pClipData->m_secondsPerFrame;
	const float phaseStep		= pAnim->m_pClipData->m_phasePerFrame;

	float evalPhase		= kStartPhase;
	bool valid			= true;
	bool doSelfBlend	= false;

	const Locator* pStartingApRefPs = &pPerformance->m_startingApRef.GetLocatorPs();
	const Locator* pFinalApRefPs = &pPerformance->m_rotatedApRef.GetLocatorPs();
	float selfBlendStartPhase = 1.0f;
	float selfBlendEndPhase = 1.0f;

	const DC::SelfBlendParams* pSelfBlend = nullptr;

	const Locator identLoc = Locator(kIdentity);
	Locator prevLocPs = pNavChar->GetLocatorPs();

	do
	{
		const DC::SelfBlendParams* pSelfBlendPrev = pSelfBlend;

		if (pSelfBlendOut && (evalPhase >= pSelfBlendOut->m_phase))
		{
			pSelfBlend			= pSelfBlendOut;
			pStartingApRefPs	= &pPerformance->m_rotatedApRef.GetLocatorPs();
			pFinalApRefPs		= &pPerformance->m_startingApRef.GetLocatorPs();
		}
		else
		{
			pSelfBlend			= pSelfBlendIn;
			pStartingApRefPs	= &pPerformance->m_startingApRef.GetLocatorPs();
			pFinalApRefPs		= &pPerformance->m_rotatedApRef.GetLocatorPs();
		}

		if (pSelfBlend != pSelfBlendPrev)
		{
			doSelfBlend			= pSelfBlend && (pSelfBlend->m_phase >= 0.0f);
			selfBlendStartPhase	= doSelfBlend ? Max(pSelfBlend->m_phase, kStartPhase) : -1.0f;
			selfBlendEndPhase	= doSelfBlend ? Limit01(selfBlendStartPhase + pSelfBlend->m_time / animDuration) : -1.0f;
		}

		const float selfBlendProgressLinear	= doSelfBlend ? LerpScale(selfBlendStartPhase, selfBlendEndPhase, 0.0f, 1.0f, evalPhase) : 0.0f;
		const float selfBlendValue			= doSelfBlend ? CalculateCurveValue(selfBlendProgressLinear, pSelfBlend->m_curve) : 0.0f;
		const Locator blendedApRefPs		= doSelfBlend ? Lerp(*pStartingApRefPs, *pFinalApRefPs, selfBlendValue) : *pStartingApRefPs;

		Locator nextLocLs = identLoc;
		if (!FindAlignFromApReferenceCached(skelId,
											pAnim,
											Min(evalPhase, kEndPhase),
											identLoc,
											SID("apReference"),
											false,
											&nextLocLs))
		{
			break;
		}

		const Point tweakedPosLs = (nextLocLs.Pos() - kOrigin) * animDeltaTweak + Point(kOrigin);
		nextLocLs.SetPos(tweakedPosLs);

		Color lineClr = kColorGreen;

		const Locator nextLocStartPs = pStartingApRefPs->TransformLocator(nextLocLs);
		const Locator nextLocEndPs = pFinalApRefPs->TransformLocator(nextLocLs);
		const Locator nextLocPs = doSelfBlend ? Lerp(nextLocStartPs, nextLocEndPs, selfBlendValue) : nextLocStartPs;

		const Point nextPosPs = nextLocPs.Pos();

		float dist = 0.0f;
		const Point closestPosPs = pPathPs->ClosestPoint(nextPosPs, &dist);

		const bool thisValid = (dist <= pPerformance->m_maxPathDeviation);

		valid = valid && thisValid;

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			PrimServerWrapper ps(parentSpace);
			ps.SetLineWidth(2.0f);

			const Vector vo = Vector(0.0f, 0.2f, 0.0f);

			if (thisValid)
			{
				ps.DrawLine(nextPosPs + vo, closestPosPs + vo, kColorGreenTrans, kColorGreenTrans);
			}
			else
			{
				ps.DrawLine(nextPosPs + vo, closestPosPs + vo, kColorRed, kColorRed);
				ps.DrawString(AveragePos(nextPosPs, closestPosPs) + vo, StringBuilder<256>("%0.1fm", dist).c_str(), kColorRedTrans, 0.5f);
			}
		}
		else if (!valid)
		{
			break;
		}

		evalPhase += phaseStep;

	} while (evalPhase <= kEndPhase);

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		pPathPs->DebugDraw(parentSpace, false);
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool OnEditMotionTypeMask(DMENU::Item& item, DMENU::Message message)
{
	const DC::NpcMotionType mt = static_cast<DC::NpcMotionType>((uintptr_t)item.m_pBlindData);

	NavCharOptions::MovePerformanceEdit& debugEdit = g_navCharOptions.m_movePerformances.m_debugEdit;

	const U32 maskVal = (1U << mt);
	const bool currentlySet = (debugEdit.m_motionTypeMask & maskVal) != 0;
	bool nowSet = currentlySet;

	if (message == DMENU::kExecute)
	{
		nowSet = !currentlySet;

		if (currentlySet)
		{
			debugEdit.m_motionTypeMask &= ~maskVal;
		}
		else
		{
			debugEdit.m_motionTypeMask |= maskVal;
		}
	}

	return nowSet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool OnUpdateDebugEditName(DMENU::Item& item, DMENU::Message message)
{
	NavCharOptions::MovePerformanceEdit& debugEdit = g_navCharOptions.m_movePerformances.m_debugEdit;

	static StringBuilder<256> s_nameBuf;

	bool valid = false;

	s_nameBuf.clear();

	if (debugEdit.m_pTable && (debugEdit.m_tableIndex < debugEdit.m_pTable->m_movePerformanceCount))
	{
		s_nameBuf.append_format("%s", DevKitOnly_StringIdToString(debugEdit.m_pTable->m_movePerformances[debugEdit.m_tableIndex].m_animId));

		valid = true;
	}
	else
	{
		s_nameBuf.append_format("<none>");
	}

	item.m_pName = const_cast<char*>(s_nameBuf.c_str());

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool OnUpdateDebugEditTableName(DMENU::Item& item, DMENU::Message message)
{
	NavCharOptions::MovePerformanceEdit& debugEdit = g_navCharOptions.m_movePerformances.m_debugEdit;

	static StringBuilder<256> s_nameBuf;

	bool valid = false;

	s_nameBuf.clear();

	if (debugEdit.m_pTable)
	{
		s_nameBuf.append_format("%s", DevKitOnly_StringIdToString(debugEdit.m_pTable.GetId()));

		valid = true;
	}
	else
	{
		s_nameBuf.append_format("<none>");
	}

	item.m_pName = const_cast<char*>(s_nameBuf.c_str());

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float OnEditSelfBlendDuration(DMENU::Item& item, DMENU::Message message, float desiredValue, float oldValue)
{
	NavCharOptions::MovePerformanceEdit& debugEdit = g_navCharOptions.m_movePerformances.m_debugEdit;

	if ((desiredValue != oldValue) && debugEdit.m_pTable)
	{
		debugEdit.m_selfBlend.m_time = desiredValue;

		LimitSelfBlend(&debugEdit.m_selfBlend, debugEdit.m_animDuration);
	}

	return debugEdit.m_selfBlend.m_time;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static I64 OnEditSelectedMoveToMove(DMENU::Item& item, DMENU::Message message, I64 desiredValue, I64 oldValue)
{
	if (oldValue != desiredValue)
	{
		g_navCharOptions.m_movePerformances.m_debugMoveToMoveIndex = desiredValue;

		NavCharOptions::MovePerformanceEdit& debugEdit = g_navCharOptions.m_movePerformances.m_debugEdit;

		if (debugEdit.m_pTable && (desiredValue >= 0) && (debugEdit.m_tableIndex < debugEdit.m_pTable->m_movePerformanceCount))
		{

		}
		else
		{
			debugEdit.m_pTable = ScriptPointer<const DC::MovePerformanceTable>();
			debugEdit.m_tableIndex = -1;
		}

		DMENU::Menu* pMenu = (DMENU::Menu*)item.m_pBlindData;
		if (pMenu)
		{
			pMenu->ClearIsDirty();
			pMenu->MarkAsDirty();
			pMenu->MarkChildrenDirty();
			pMenu->SendMessage(DMENU::kWake);
		}
	}

	return g_navCharOptions.m_movePerformances.m_debugMoveToMoveIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void CreateEditMovePerformanceMenu(DMENU::Menu* pMenu)
{
	NavCharOptions::MovePerformanceEdit& debugEdit = g_navCharOptions.m_movePerformances.m_debugEdit;
	const DMENU::ItemEnumPair* pCurveTypes = GetAnimCurveDevMenuEnumPairs();

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Enable Debug Editing", &g_navCharOptions.m_movePerformances.m_debugEditMoveToMoves));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemInteger("Debug Move to Move Index", OnEditSelectedMoveToMove, pMenu));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("<none>", OnUpdateDebugEditName));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("<none>", OnUpdateDebugEditTableName));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("MotionType Walk", OnEditMotionTypeMask, (void*)(intptr_t)DC::kNpcMotionTypeWalk));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("MotionType Run", OnEditMotionTypeMask, (void*)(intptr_t)DC::kNpcMotionTypeRun));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("MotionType Sprint", OnEditMotionTypeMask, (void*)(intptr_t)DC::kNpcMotionTypeSprint));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("BlendIn AnimFade", &debugEdit.m_blendIn.m_animFadeTime, DMENU::FloatRange(-1.0f, 10.0f), DMENU::FloatSteps(0.1f, 1.0f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("BlendIn MotionFade", &debugEdit.m_blendIn.m_motionFadeTime, DMENU::FloatRange(-1.0f, 10.0f), DMENU::FloatSteps(0.1f, 1.0f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemEnum("BlendIn Curve", pCurveTypes, DMENU::EditInt, &debugEdit.m_blendIn.m_curve));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("SelfBlend Phase", DMENU::EditFloat, 16, "%0.2f", &debugEdit.m_selfBlend.m_phase, DMENU::FloatRange(-1.0f, 1.0f), DMENU::FloatSteps(0.1f, 0.25f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("SelfBlend Duration", OnEditSelfBlendDuration, 16, "%0.1f", &debugEdit.m_selfBlend.m_time, DMENU::FloatRange(0.0f, kLargeFloat), DMENU::FloatSteps(0.1f, 0.25f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemEnum("SelfBlend Curve", pCurveTypes, DMENU::EditInt, &debugEdit.m_selfBlend.m_curve));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Max Pivot Err (deg)", &debugEdit.m_maxPivotErrDeg, DMENU::FloatRange(0.0f, 360.0f), DMENU::FloatSteps(1.0f, 10.0f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Max Exit Err (deg)", &debugEdit.m_maxExitErrDeg, DMENU::FloatRange(0.0f, 360.0f), DMENU::FloatSteps(1.0f, 10.0f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Max Path Deviation", &debugEdit.m_maxPathDeviation, DMENU::FloatRange(0.0f, 10.0f), DMENU::FloatSteps(0.1f, 1.0f)));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Exit Phase", &debugEdit.m_exitPhase, DMENU::FloatRange(0.0f, 1.0f), DMENU::FloatSteps(0.1f, 1.0f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Min Speed Factor", &debugEdit.m_minSpeedFactor, DMENU::FloatRange(0.0f, 1.0f), DMENU::FloatSteps(0.1f, 1.0f)));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Single Pivot Fitting", &debugEdit.m_singlePivotFitting));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CreateMovePerformanceDevMenu(DMENU::Menu* pMenu)
{
	STRIP_IN_FINAL_BUILD;

	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Min Rem. Path Distance (Stopped)", &g_navCharOptions.m_movePerformances.m_minRemPathDistStopped, DMENU::FloatRange(0.0f, 5.0f), DMENU::FloatSteps(0.1f, 1.0f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Min Rem. Path Distance (Moving)", &g_navCharOptions.m_movePerformances.m_minRemPathDistMoving, DMENU::FloatRange(0.0f, 5.0f), DMENU::FloatSteps(0.1f, 1.0f)));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Move to Moves", &g_navCharOptions.m_movePerformances.m_debugMoveToMoves));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemInteger("Debug Move to Move Index", OnEditSelectedMoveToMove, pMenu));

	DMENU::Menu* pEditMenu = NDI_NEW DMENU::Menu("Debug Edit Move to Moves");
	CreateEditMovePerformanceMenu(pEditMenu);

	pMenu->PushBackItem(NDI_NEW DMENU::ItemSubmenu("Debug Edit Move to Moves ...", pEditMenu));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Draw Move Performances", &g_navCharOptions.m_movePerformances.m_debugDrawMovePerformances));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Draw Move Performance Poi", &g_navCharOptions.m_movePerformances.m_debugDrawMovePerformancePoi));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemInteger("Debug Move Performance Index", &g_navCharOptions.m_movePerformances.m_debugMovePerformanceIndex, DMENU::IntRange(-1, 1000), DMENU::IntSteps(1, 10)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Move Performances", &g_navCharOptions.m_movePerformances.m_disableMovePerformances));

}
