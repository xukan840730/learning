/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/controller/entry-controller.h"

#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/text/stringid-util.h"

#include "gamelib/gameplay/ai/agent/nav-character-adapter.inl"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/ap-entry-util.h"
#include "gamelib/gameplay/nav/entry-action-pack.h"
#include "gamelib/scriptx/h/ai-entry-ap-defines.h"
#include "gamelib/scriptx/h/anim-nav-character-info.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void AiEntryController::Enter(const ActionPackResolveInput& input,
							  ActionPack* pActionPack,
							  const ActionPackEntryDef& entryDef)
{
	ParentClass::Enter(input, pActionPack, entryDef);

	NdGameObject* pGo = GetCharacterGameObject();
	AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;

	const bool mirror = entryDef.m_pDcDef && ((entryDef.m_pDcDef->m_flags & DC::kApEntryItemFlagsMirror) != 0);

	const ArtItemAnimHandle hAnim = pAnimControl->LookupAnim(entryDef.m_entryAnimId);

	if (!hAnim.ToArtItem())
	{
		return;
	}

	AI::SetPluggableAnim(pGo, entryDef.m_entryAnimId, mirror);
	
	FadeToStateParams params;
	params.m_apRef		  = entryDef.m_apReference;
	params.m_apRefValid	  = true;
	params.m_animFadeTime = 0.4f;
	params.m_motionFadeTime	 = 0.2f;
	params.m_stateStartPhase = entryDef.m_phase;

	if (entryDef.m_pDcDef)
	{
		params.ApplyBlendParams(entryDef.m_pDcDef->m_blendIn);
	}

	m_animAction.FadeToState(pAnimControl, SID("s_entry-enter"), params);

	if (const DC::SelfBlendParams* pSelfBlend = entryDef.GetSelfBlend())
	{
		SelfBlendAction::Params sbParams;
		sbParams.m_blendParams = *pSelfBlend;
		sbParams.m_destAp	   = entryDef.m_sbApReference;
		m_animAction.ConfigureSelfBlend(pGo, sbParams);
	}

	if (DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>())
	{
		const Vector entryVelocityPs = entryDef.m_entryVelocityPs;
		const Vector charVelocityPs = pGo->GetVelocityPs();
		const float desSpeed = Length(entryVelocityPs);
		const float projSpeed = Dot(SafeNormalize(entryVelocityPs, kZero), charVelocityPs);
		const float speedRatio = (desSpeed > 0.0f) ? (projSpeed / desSpeed) : 1.0f;
		pInfo->m_speedFactor = Limit(speedRatio, 0.6f, 1.2f);
	}

	const EntryActionPack* pEntryAp = (const EntryActionPack*)pActionPack;
	const StringId64 destAnimId = pEntryAp->GetDestAnimId();

	m_finalAlignPs = pEntryAp->GetBoundFrame().GetLocatorPs();
	m_targetVelPs = kZero;

	if (const ArtItemAnim* pDestAnim = pAnimControl->LookupAnim(destAnimId).ToArtItem())
	{
		const float destAnimPhase = pEntryAp->GetDestAnimPhase();
		const Vector destAnimVelLs = GetAnimVelocityAtPhase(pDestAnim, destAnimPhase);
		m_targetVelPs = m_finalAlignPs.TransformVector(destAnimVelLs);
	}

	NavAnimHandoffDesc handoff;
	handoff.SetStateChangeRequestId(m_animAction.GetStateChangeRequestId(), SID("s_entry-enter"));
	handoff.m_motionType = kMotionTypeMax;

	NavCharacterAdapter charAdapter = GetCharacterAdapter();
	charAdapter->ConfigureNavigationHandOff(handoff, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiEntryController::Exit(const PathWaypointsEx* pExitPathPs) {}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEntryController::RequestAbortAction()
{
	if (IsBusy())
	{
		m_animAction.Reset();
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEntryController::ResolveEntry(const ActionPackResolveInput& input,
									 const ActionPack* pActionPack,
									 ActionPackEntryDef* pDefOut) const
{
	const EntryActionPack* pEntryAp = (const EntryActionPack*)pActionPack;
	const DC::EntryApTable* pDcDef = pEntryAp ? pEntryAp->GetDcDef() : nullptr;

	NdGameObject* pGo = GetCharacterGameObject();
	NavCharacterAdapter navChar = GetCharacterAdapter();
	const StringId64 charId = navChar->GetCapCharacterId();

	const I32F iAnims = EntryActionPack::SelectEntryDefAnims(pDcDef, charId);

	if (iAnims < 0)
	{
		return false;
	}

	const DC::ApEntryItemList* pEntryList = pDcDef->m_entryItems[iAnims].m_entryList;

	static CONST_EXPR U32F maxAvailableEntries = 32;
	ApEntry::AvailableEntry availableEntries[maxAvailableEntries];

	const BoundFrame defaultApRef = pEntryAp->GetBoundFrame();

	ApEntry::CharacterState cs;
	MakeEntryCharacterState(input, pEntryAp, &cs);

	const U32F numAvailableEntries = ApEntry::ResolveEntry(cs,
														   pEntryList,
														   defaultApRef,
														   availableEntries,
														   maxAvailableEntries);

	if (0 == numAvailableEntries)
	{
		return false;
	}

	const Locator& alignPs	   = pGo->GetLocatorPs();
	const Locator& parentSpace = pGo->GetParentSpace();

	const I32F iSelectedEntry = ApEntry::EntryWithLeastApproachRotation(availableEntries, numAvailableEntries, alignPs);

	if (iSelectedEntry < 0 || iSelectedEntry >= numAvailableEntries)
	{
		return false;
	}

	const ApEntry::AvailableEntry* pSelectedEntry = &availableEntries[iSelectedEntry];

	ANIM_ASSERT(pSelectedEntry);
	ANIM_ASSERT(pSelectedEntry->m_pDcDef);

	if (!ApEntry::ConstructEntryNavLocation(cs,
											pEntryAp->GetRegisteredNavLocation(),
											*pSelectedEntry,
											&pDefOut->m_entryNavLoc))
	{
		return false;
	}

	pDefOut->m_apReference = pSelectedEntry->m_rotatedApRef;
	pDefOut->m_entryAnimId = pSelectedEntry->m_resolvedAnimId;
	pDefOut->m_pDcDef	   = pSelectedEntry->m_pDcDef;
	pDefOut->m_stopBeforeEntry = pSelectedEntry->m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop;
	pDefOut->m_phase = pSelectedEntry->m_phase;
	pDefOut->m_entryVelocityPs = pSelectedEntry->m_entryVelocityPs;
	pDefOut->m_sbApReference   = pSelectedEntry->m_sbApRef;
	pDefOut->m_mirror = pSelectedEntry->m_playMirrored;
	pDefOut->m_hResolvedForAp = pActionPack;

	if (pSelectedEntry->m_pDcDef->m_selfBlend)
	{
	}
	else if (const ArtItemAnim* pAnim = pSelectedEntry->m_anim.ToArtItem())
	{
		DC::SelfBlendParams sb;
		sb.m_phase = pSelectedEntry->m_phase;
		sb.m_time	 = (1.0f - pSelectedEntry->m_phase) * GetDuration(pAnim);
		sb.m_curve = DC::kAnimCurveTypeUniformS;

		pDefOut->OverrideSelfBlend(&sb);
	}

	const Locator entryLocPs = pSelectedEntry->m_entryAlignPs;
	const Locator entryLocWs = pSelectedEntry->m_rotatedApRef.GetParentSpace().TransformLocator(entryLocPs);

	pDefOut->m_entryRotPs = parentSpace.UntransformLocator(entryLocWs).Rot();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEntryController::ResolveDefaultEntry(const ActionPackResolveInput& input,
											const ActionPack* pActionPack,
											ActionPackEntryDef* pDefOut) const
{
	const EntryActionPack* pEntryAp = (const EntryActionPack*)pActionPack;
	const DC::EntryApTable* pDcDef = pEntryAp ? pEntryAp->GetDcDef() : nullptr;

	NdGameObject* pGo = GetCharacterGameObject();
	NavCharacterAdapter navChar = GetCharacterAdapter();
	const StringId64 charId = navChar->GetCapCharacterId();
	
	const I32F iAnims = EntryActionPack::SelectEntryDefAnims(pDcDef, charId);

	if (iAnims < 0)
	{
		return false;
	}

	const DC::ApEntryItemList* pEntryList = pDcDef->m_entryItems[iAnims].m_entryList;

	static CONST_EXPR U32F maxAvailableEntries = 32;
	ApEntry::AvailableEntry availableEntries[maxAvailableEntries];

	const BoundFrame defaultApRef = pEntryAp->GetBoundFrame();

	ApEntry::CharacterState cs;
	MakeEntryCharacterState(input, pEntryAp, &cs);

	const U32F numAvailableEntries = ApEntry::ResolveDefaultEntry(cs,
																  pEntryList,
																  defaultApRef,
																  availableEntries,
																  maxAvailableEntries);

	if (0 == numAvailableEntries)
	{
		return false;
	}

	const Locator& alignPs = pGo->GetLocatorPs();
	const Locator& parentSpace = pGo->GetParentSpace();

	const I32F iSelectedEntry = ApEntry::EntryWithLeastApproachRotation(availableEntries, numAvailableEntries, alignPs);;

	if (iSelectedEntry < 0 || iSelectedEntry >= numAvailableEntries)
	{
		return false;
	}

	const ApEntry::AvailableEntry* pSelectedEntry = &availableEntries[iSelectedEntry];

	ANIM_ASSERT(pSelectedEntry);
	ANIM_ASSERT(pSelectedEntry->m_pDcDef);

	if (!ApEntry::ConstructEntryNavLocation(cs,
											pEntryAp->GetRegisteredNavLocation(),
											*pSelectedEntry,
											&pDefOut->m_entryNavLoc))
	{
		return false;
	}

	pDefOut->m_apReference = pSelectedEntry->m_rotatedApRef;
	pDefOut->m_entryAnimId = pSelectedEntry->m_resolvedAnimId;
	pDefOut->m_pDcDef	   = pSelectedEntry->m_pDcDef;
	pDefOut->m_stopBeforeEntry = pSelectedEntry->m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop;
	pDefOut->m_phase = pSelectedEntry->m_phase;
	pDefOut->m_entryVelocityPs = pSelectedEntry->m_entryVelocityPs;
	pDefOut->m_sbApReference   = pSelectedEntry->m_sbApRef;
	pDefOut->m_mirror = pSelectedEntry->m_playMirrored;
	pDefOut->m_hResolvedForAp = pActionPack;

	if (pSelectedEntry->m_pDcDef->m_selfBlend)
	{
	}
	else if (const ArtItemAnim* pAnim = pSelectedEntry->m_anim.ToArtItem())
	{
		DC::SelfBlendParams sb;
		sb.m_phase = pSelectedEntry->m_phase;
		sb.m_time = (1.0f - pSelectedEntry->m_phase) * GetDuration(pAnim);
		sb.m_curve = DC::kAnimCurveTypeUniformS;
	}

	const Locator entryLocPs = pSelectedEntry->m_entryAlignPs;
	const Locator entryLocWs = pSelectedEntry->m_rotatedApRef.GetParentSpace().TransformLocator(entryLocPs);

	pDefOut->m_entryRotPs = parentSpace.UntransformLocator(entryLocWs).Rot();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEntryController::UpdateEntry(const ActionPackResolveInput& input,
									const ActionPack* pActionPack,
									ActionPackEntryDef* pDefOut) const
{
	ANIM_ASSERT(pActionPack && (pActionPack->GetType() == ActionPack::kEntryActionPack));

	NdGameObject* pGo = GetCharacterGameObject();

	const EntryActionPack* pEntryAp = (const EntryActionPack*)pActionPack;
	const DC::ApEntryItem* pEntry = pDefOut->m_pDcDef;
	const BoundFrame defaultApRef = pEntryAp->GetBoundFrame();

	ApEntry::CharacterState cs;
	MakeEntryCharacterState(input, pEntryAp, &cs);

	pDefOut->m_hResolvedForAp = nullptr;

	ApEntry::AvailableEntry updatedEntry;
	if (!ApEntry::ConstructEntry(cs, pEntry, defaultApRef, &updatedEntry))
	{
		return false;
	}

	const bool isDefaultEntry = (pEntry->m_flags & DC::kApEntryItemFlagsDefaultEntry) != 0;
	const ApEntry::GatherMode mode = isDefaultEntry ? ApEntry::GatherMode::kDefault : ApEntry::GatherMode::kResolved;

	ApEntry::FlagInvalidEntries(cs, mode, defaultApRef, &updatedEntry, 1);

	if (!ApEntry::ConstructEntryNavLocation(cs,
											pEntryAp->GetRegisteredNavLocation(),
											updatedEntry,
											&pDefOut->m_entryNavLoc))
	{
		return false;
	}

	const bool forceStop = (updatedEntry.m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop);

	Locator entryLocPs = updatedEntry.m_entryAlignPs;
	entryLocPs.SetPos(PointFromXzAndY(entryLocPs.Pos(), pGo->GetTranslationPs()));

	pDefOut->m_entryRotPs  = entryLocPs.Rot();
	pDefOut->m_apReference = updatedEntry.m_rotatedApRef;
	pDefOut->m_phase	   = updatedEntry.m_phase;
	pDefOut->m_entryVelocityPs = updatedEntry.m_entryVelocityPs;
	pDefOut->m_refreshTime	   = pGo->GetCurTime();
	pDefOut->m_hResolvedForAp  = pActionPack;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiEntryController::RequestAnimations()
{
	if (!m_hActionPack.IsValid())
		return;

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	DC::AnimNavCharInfo* pInfo = pAnimControl ? pAnimControl->Info<DC::AnimNavCharInfo>() : nullptr;

	if (!pInfo)
		return;

	static float kEntrySpeedFilter = 0.5f;
	pInfo->m_speedFactor = AI::TrackSpeedFactor(pNavChar, pInfo->m_speedFactor, 1.0f, kEntrySpeedFilter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiEntryController::UpdateStatus()
{
	NdGameObject* pGo = GetCharacterGameObject();
	if (!pGo)
	{
		m_animAction.Reset();
		return;
	}

	AnimControl* pAnimControl = pGo->GetAnimControl();

	const Vector curVelPs = pGo->GetVelocityPs();
	const Locator& curLocPs = pGo->GetLocatorPs();

	const float speedErr = Length(curVelPs - m_targetVelPs);
	const float distErr = DistXz(curLocPs.Pos(), m_finalAlignPs.Pos());
	const float rotErr = Dot(curLocPs.Rot(), m_finalAlignPs.Rot());

	const bool speedMatch = speedErr < 1.0f;

	const bool alignPosMatch = distErr < 0.1f;
	const bool alignRotMatch = rotErr > 0.99f;

	if (false)
	{
		MsgCon("speed err: %0.3fm/s\n", speedErr);
		MsgCon("dist err: %0.3fm\n", distErr);
		MsgCon("rot err: %0.4f\n", rotErr);
	}

	if (alignPosMatch && alignRotMatch && speedMatch)
	{
		m_animAction.Reset();
	}
	else
	{
		m_animAction.Update(pAnimControl);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEntryController::IsBusy() const
{
	return m_animAction.IsValid() && !m_animAction.IsDone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiEntryController::DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const
{
	STRIP_IN_FINAL_BUILD;

	if (!pActionPack || (pActionPack->GetType() != ActionPack::kEntryActionPack))
		return;

	const EntryActionPack* pEntryAp = static_cast<const EntryActionPack*>(pActionPack);

	const NavCharacter* pNavChar = GetCharacter();
	const StringId64 charId = pNavChar->GetCapCharacterId();
	const BoundFrame apRef = pActionPack->GetBoundFrame();

	const DC::ApEntryItem* pChosenEntry = nullptr;

	const DC::EntryApTable* pEntryTable = pEntryAp->GetDcDef();

	const I32F iAnims = EntryActionPack::SelectEntryDefAnims(pEntryTable, charId);

	if (iAnims < 0)
	{
		return;
	}

	const DC::ApEntryItemList* pEntryList = (iAnims >= 0) ? pEntryTable->m_entryItems[iAnims].m_entryList : nullptr;

	DC::ApEntryItemList entryList;
	entryList.m_array = pEntryList ? pEntryList->m_array : nullptr;
	entryList.m_count = pEntryList ? pEntryList->m_count : 0;

	DebugDrawEntryAnims(input,
						pActionPack,
						apRef,
						entryList,
						pChosenEntry,
						g_ndAiOptions.m_entry);
}


/// --------------------------------------------------------------------------------------------------------------- ///
AiEntryController* CreateAiEntryController()
{
	return NDI_NEW AiEntryController;
}
