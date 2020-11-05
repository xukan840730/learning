/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/perch-combat-controller.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/scriptx/h/anim-overlay-defines.h"

#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/ai/controller/nd-locomotion-controller.h"
#include "gamelib/gameplay/ap-entry-util.h"
#include "gamelib/gameplay/ap-exit-util.h"
#include "gamelib/gameplay/nav/action-pack-entry-def.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/perch-action-pack.h"

#include "ailib/util/ai-lib-util.h"

#include "game/ai/agent/npc-action-pack-util.h"
#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/controller/weapon-controller.h"
#include "game/player/player-snapshot.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/hit-reactions-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///----
// AiPerchController
/// --------------------------------------------------------------------------------------------------------------- ///----
class AiPerchController : public IAiPerchController  
{
public:

	typedef IAiPerchController ParentClass;

	AnimActionWithSelfBlend		m_exitAction;
	bool						m_entered		: 1;
	StringId64					m_perchType;
	I32							m_debugPerchApEnterIndex;	// Index of selected enter anim
	ActionPackEntryDef			m_entryDef;
	AnimActionWithSelfBlend		m_enterAction;
	AnimAction					m_investigateAction;
	NavAnimHandoffDesc			m_exitHandoff;
	float						m_exitPhase;

	/// --------------------------------------------------------------------------------------------------------------- ///
	AiPerchController()
	: m_perchType(INVALID_STRING_ID_64)
	, m_entered(false)
	, m_debugPerchApEnterIndex(-1)
	, m_exitPhase(-1.0f)
	{
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void MakeEntryCharacterState(const ActionPackResolveInput& input,
										 const ActionPack* pActionPack,
										 ApEntry::CharacterState* pCsOut) const override
	{
		const NavCharacter* pNavChar = GetCharacter();

		if (!pNavChar || !pCsOut)
			return;

		ParentClass::MakeEntryCharacterState(input, pActionPack, pCsOut);

		ApEntry::CharacterState& cs = *pCsOut;

		const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig();

		// more potential config variables here
		cs.m_rangedEntryRadius = 3.0f;
		cs.m_maxEntryErrorAngleDeg = 40.0f;
		cs.m_maxFacingDiffAngleDeg = 40.0f;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	const DC::ApEntryItemList* GetPerchEntries(StringId64 perchTypeId) const
	{
		NavCharacter* pNavChar = GetCharacter();
		if (const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig())
		{
			const DC::Map* pEntriesMap = ScriptManager::Lookup<DC::Map>(pConfig->m_perch.m_entryDefsSetId, nullptr);
			if (!pEntriesMap)
				return nullptr;

			const DC::ApEntryItemList* pEntries = ScriptManager::MapLookup<DC::ApEntryItemList>(pEntriesMap, perchTypeId);
			return pEntries;
		}

		return nullptr;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void SetDebugPerchApEnterIndex(I32F val) override
	{
		m_debugPerchApEnterIndex = val;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	I32F GetPerchApEnterAnimCount(const PerchActionPack* pPerchAp) const override
	{
		if (!pPerchAp)
			return 0;

		if (const DC::ApEntryItemList* pDcEntryList = GetPerchEntries(pPerchAp->GetPerchType()))
		{
			return pDcEntryList->m_count;
		}
		
		return 0;
	}
	
	/// --------------------------------------------------------------------------------------------------------------- ///
	static Point GetDefaultEntryPoint(const Locator& perchApRef, const float defaultEntryDist)
	{
		const Vector entryVec = Vector(0.0f, 0.0f, -defaultEntryDist);
		const Vector offset = perchApRef.TransformVector(entryVec);
		const Point entryPos = perchApRef.Pos() + offset;
		return entryPos;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool ResolveEntry(const ActionPackResolveInput& input,
					  const ActionPack* pActionPack,
					  ActionPackEntryDef* pDefOut) const override
	{
		PROFILE(AI,PerchCon_ResolveEntry);
		bool validEntry = false;

		const Npc* pNpc = static_cast<const Npc*>(GetCharacter());
		const Locator& parentSpace = pNpc->GetParentSpace();

		AI_ASSERT(pDefOut);
		AI_ASSERT(pActionPack->GetType() == ActionPack::kPerchActionPack);

		if (!pDefOut)
			return false;

		const PerchActionPack* pPerchAp = static_cast<const PerchActionPack*>(pActionPack);
		BoundFrame perchApRef = pActionPack->GetBoundFrame();

		ApEntry::CharacterState cs;
		MakeEntryCharacterState(input, pActionPack, &cs);

		const DC::ApEntryItemList* pDcEntryList = GetPerchEntries(pPerchAp->GetPerchType());
		if (!pDcEntryList)
			return 0;

		const U32F kMaxEntries = 50;
		ApEntry::AvailableEntry availableEntries[kMaxEntries];

		const U32F numUsableEntries = ApEntry::ResolveEntry(cs, pDcEntryList, perchApRef, availableEntries, kMaxEntries);

		// this action pack may be in a different parent space than the Npc, so we first need to get the entry point in world space
		const Locator perchApRefWs = perchApRef.GetLocatorWs();

		// Typical entry
		if (numUsableEntries > 0)
		{
			const U32F selectedIndex = ApEntry::EntryWithLeastRotation(availableEntries, numUsableEntries, perchApRef);
			const ApEntry::AvailableEntry* pSelectedEntry = &availableEntries[selectedIndex];

			AI_ASSERT(pSelectedEntry);
			AI_ASSERT(pSelectedEntry->m_pDcDef);

			pDefOut->m_apReference = pSelectedEntry->m_rotatedApRef;
			pDefOut->m_entryAnimId = pSelectedEntry->m_resolvedAnimId;
			pDefOut->m_pDcDef	   = pSelectedEntry->m_pDcDef;
			pDefOut->m_stopBeforeEntry	   = pSelectedEntry->m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop;
			pDefOut->m_preferredMotionType = pNpc->GetConfiguration().m_moveToPerchMotionType;
			pDefOut->m_phase = pSelectedEntry->m_phase;
			pDefOut->m_entryVelocityPs = pSelectedEntry->m_entryVelocityPs;
			pDefOut->m_entryRotPs	   = pSelectedEntry->m_entryAlignPs.Rot();
			pDefOut->m_mirror = pSelectedEntry->m_playMirrored;

			if (!ApEntry::ConstructEntryNavLocation(cs,
													pActionPack->GetRegisteredNavLocation(),
													*pSelectedEntry,
													&pDefOut->m_entryNavLoc))
			{
				return false;
			}

			validEntry = true;
		}
		// Invalid entry
		else
		{
			pDefOut->m_preferredMotionType = pNpc->GetConfiguration().m_moveToPerchMotionType;
			pDefOut->m_apReference		   = perchApRef;
			pDefOut->m_entryAnimId		   = INVALID_STRING_ID_64;
			pDefOut->m_pDcDef = nullptr;
			pDefOut->m_stopBeforeEntry = true;
			pDefOut->m_phase = 0.0f;
			pDefOut->m_entryVelocityPs = kZero;

			Scalar apRegPtDist = 0.0f;
			if (const NavControl* pNavControl = GetNavControl())
				apRegPtDist = pNavControl->GetActionPackEntryDistance();

			Locator entryLocWs = perchApRefWs;
			const Point defEntryPosWs = GetDefaultEntryPoint(perchApRefWs, apRegPtDist);

			// adjust entry position to be on nav mesh
			FindBestNavMeshParams findNavMesh;
			findNavMesh.m_pointWs = entryLocWs.Pos();
			findNavMesh.m_cullDist = 0.5f;
			findNavMesh.m_yThreshold = 2.0f;

			NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
			nmMgr.FindNavMeshWs(&findNavMesh);

			const Point entryLocY = findNavMesh.m_pNavPoly ? findNavMesh.m_nearestPointWs : pNpc->GetTranslation(); // fall back to NPC's Y-coordinate if failed

			// save entry loc in Npcs parent space
			entryLocWs.SetPos(PointFromXzAndY(defEntryPosWs, entryLocY));

			entryLocWs.SetTranslation(GetDefaultEntryPoint(perchApRefWs, apRegPtDist));

			pDefOut->m_entryRotPs = parentSpace.UntransformLocator(entryLocWs).Rot();
			pDefOut->m_entryNavLoc = pNpc->AsReachableNavLocationWs(entryLocWs.Pos(), NavLocation::Type::kNavPoly);
		}

		if (validEntry)
		{
			pDefOut->m_hResolvedForAp = pActionPack;
			pDefOut->m_refreshTime	  = pNpc->GetCurTime();
		}

		return validEntry;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool ResolveDefaultEntry(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 ActionPackEntryDef* pDefOut) const override
	{
		PROFILE(AI, PerchCon_ResolveDefEntry);

		AI_ASSERT(pDefOut);
		AI_ASSERT(pActionPack->GetType() == ActionPack::kPerchActionPack);

		if (!pDefOut)
			return false;

		const Npc* pNpc = static_cast<const Npc*>(GetCharacter());
		AnimControl* pAnimControl = pNpc->GetAnimControl();
		const PerchActionPack* pPerchAp = static_cast<const PerchActionPack*>(pActionPack);
		BoundFrame perchApRef = pActionPack->GetBoundFrame();

		const DC::ApEntryItemList* pDcEntryList = GetPerchEntries(pPerchAp->GetPerchType());
		if (!pDcEntryList)
			return false;

		ApEntry::CharacterState cs;
		MakeEntryCharacterState(input, pActionPack, &cs);

		ApEntry::AvailableEntry defaultEntries[16];
		const U32F numDefaultEntries = ApEntry::ResolveDefaultEntry(cs, pDcEntryList, perchApRef, defaultEntries, 16);

		const Locator& alignPs = pNpc->GetLocatorPs();

		const INdAiLocomotionController* pLocomotionController = pNpc->GetAnimationControllers()->GetLocomotionController();
		const MotionType mt		= pLocomotionController->RestrictMotionType(input.m_motionType, false);
		const Vector alignFwPs	= GetLocalZ(alignPs.Rot());
		const bool strafing		= input.m_strafing && pLocomotionController->CanStrafe(mt);
		const Vector faceDirPs	= SafeNormalize(VectorXz(pNpc->GetFacePositionPs() - alignPs.Pos()), kUnitZAxis);

		I32F bestEntryIndex = -1;
		float bestRating = -1.0f;

		for (U32F i = 0; i < numDefaultEntries; ++i)
		{
			const Locator entryLocPs = defaultEntries[i].m_entryAlignPs;
			const Vector toEntryXZPs = VectorXz(entryLocPs.Pos() - alignPs.Pos());
			const float xzDist = DistXz(entryLocPs.Pos(), alignPs.Pos());
			const float minDist = 0.5f;

			const Vector entryFwPs = GetLocalZ(entryLocPs.Rot());

			float rating = -1.0f;

			if (strafing)
			{
				rating = Dot(entryFwPs, faceDirPs);
			}
			else if (xzDist >= minDist + 0.1f)
			{
				const Vector approachDirPs = SafeNormalize(toEntryXZPs, kZero);
				rating = Dot(entryFwPs, approachDirPs);
			}
			else
			{
				rating = Dot(entryFwPs, alignFwPs);
			}

			if (rating > bestRating)
			{
				bestRating = rating;
				bestEntryIndex = i;
			}
		}

		if (bestEntryIndex < 0 || bestEntryIndex >= numDefaultEntries)
			return false;

		const ApEntry::AvailableEntry& defaultEntry = defaultEntries[bestEntryIndex];

		AI_ASSERT(defaultEntry.m_pDcDef);
		if (!defaultEntry.m_pDcDef)
			return false;

		const Locator defaultEntryLocPs = defaultEntry.m_entryAlignPs;

		BoundFrame rotatedApRef = defaultEntry.m_rotatedApRef;
		Locator entryLocPs = defaultEntryLocPs;
		//entryLocPs.SetPos(PointFromXzAndY(entryLocPs.Pos(), pNpc->GetTranslationPs()));

		if (!ApEntry::ConstructEntryNavLocation(cs, pActionPack->GetRegisteredNavLocation(), defaultEntry, &pDefOut->m_entryNavLoc))
			return false;

		const bool forceStop = (defaultEntry.m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop);

		pDefOut->m_apReference = rotatedApRef;
		pDefOut->m_entryAnimId = defaultEntry.m_resolvedAnimId;
		pDefOut->m_pDcDef	   = defaultEntry.m_pDcDef;
		pDefOut->m_stopBeforeEntry	   = forceStop;
		pDefOut->m_preferredMotionType = pNpc->GetConfiguration().m_moveToPerchMotionType;
		pDefOut->m_phase	  = defaultEntry.m_phase;
		pDefOut->m_entryRotPs = entryLocPs.Rot();
		pDefOut->m_entryVelocityPs = kZero;
		pDefOut->m_mirror		  = defaultEntry.m_playMirrored;
		pDefOut->m_hResolvedForAp = pActionPack;
		pDefOut->m_refreshTime	  = pNpc->GetCurTime();

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool UpdateEntry(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 ActionPackEntryDef* pDefOut) const override
	{
		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void ApplyOverlay()
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
		AnimOverlays* pOverlays = pAnimControl ? pAnimControl->GetAnimOverlays() : nullptr;
		const NdAnimControllerConfig* pConfig = pNavChar ? pNavChar->GetAnimControllerConfig() : nullptr;
		const StringId64 mapId = StringToStringId64(StringBuilder<128>("anim-overlay-%s-action-pack", pNavChar->GetOverlayBaseName()).c_str());
		const DC::Map* pMap = pConfig ? ScriptManager::Lookup<DC::Map>(mapId, nullptr) : nullptr;
		if (!pMap || !pOverlays)
			return;

		const StringId64* pSetId = ScriptManager::MapLookup<StringId64>(pMap, m_perchType);
		if (!pSetId || (*pSetId == INVALID_STRING_ID_64))
			return;

		const DC::AnimOverlaySet* pSet = ScriptManager::Lookup<DC::AnimOverlaySet>(*pSetId);
		if (!pSet)
			return;

		pOverlays->SetOverlaySet(pSet);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void ClearOverlay()
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
		AnimOverlays* pOverlays = pAnimControl ? pAnimControl->GetAnimOverlays() : nullptr;
		const NdAnimControllerConfig* pConfig = pNavChar ? pNavChar->GetAnimControllerConfig() : nullptr;
		const StringId64 mapId = StringToStringId64(StringBuilder<128>("anim-overlay-%s-action-pack",
																	   pNavChar->GetOverlayBaseName())
														.c_str());
		const DC::Map* pMap = pConfig ? ScriptManager::Lookup<DC::Map>(mapId, nullptr) : nullptr;
		if (!pMap || !pOverlays)
			return;

		const StringId64* pSetId = ScriptManager::MapLookup<StringId64>(pMap, SID("no-change"));
		if (!pSetId || (*pSetId == INVALID_STRING_ID_64))
			return;

		const DC::AnimOverlaySet* pSet = ScriptManager::Lookup<DC::AnimOverlaySet>(*pSetId);
		if (!pSet)
			return;

		pOverlays->SetOverlaySet(pSet);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void Enter(const ActionPackResolveInput& input,
					   ActionPack* pActionPack,
					   const ActionPackEntryDef& entryDef) override
	{
		ParentClass::Enter(input, pActionPack, entryDef);

		ApplyOverlay();

		m_entryDef = entryDef;

		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		DC::AnimNpcInfo* pInfo = pAnimControl->Info<DC::AnimNpcInfo>();
		const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig();

		const TimeFrame enterTime = pNavChar->GetCurTime();

		pAnimControl->GetBaseStateLayer()->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

		AI_ASSERT(pActionPack->GetType() == ActionPack::kPerchActionPack);
		const PerchActionPack* pPerchAp = static_cast<const PerchActionPack*>(pActionPack);

		m_perchType = pPerchAp->GetPerchType();
		ApplyOverlay();

			// FadeToState parameters
		//const bool PerchShare = CanSharePerchWithPlayer(pNavChar) && pActionPack->IsPlayerBlocked();
		StringId64 animStateId = SID("s_enter-perch");
		float startPhase = 0.0f;
		BoundFrame apReference = pActionPack->GetBoundFrame();
		float animFadeTime = 0.4f;
		float motionFadeTime = 0.2f;
		
		DC::AnimCurveType curve = DC::kAnimCurveTypeUniformS;

		const DC::ApEntryItem* pDcEntryItem = entryDef.m_pDcDef;
		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(entryDef.m_entryAnimId).ToArtItem();

		if (entryDef.m_entryAnimId != INVALID_STRING_ID_64)
		{
			const float distErr = Dist(entryDef.m_entryNavLoc.GetPosPs(), pNavChar->GetTranslationPs());
			const float rotErrDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(GetLocalZ(entryDef.m_entryRotPs), GetLocalZ(pNavChar->GetRotationPs()))));
			const float rotErrFloor = LerpScale(10.0f, 25.0f, 0.05f, 0.25f, rotErrDeg);
			const float desiredMotionBlend = rotErrFloor + LerpScale(0.075f, 0.4f, 0.0f, 0.4f, distErr);

			const float maxFadeTime = LimitBlendTime(pAnim, entryDef.m_phase, animFadeTime);
			animFadeTime = Min(maxFadeTime, animFadeTime);
			motionFadeTime = Min(maxFadeTime, desiredMotionBlend);

			if (pDcEntryItem->m_blendIn)
			{
				animFadeTime = Min(pDcEntryItem->m_blendIn->m_animFadeTime, maxFadeTime);
				motionFadeTime = Min(pDcEntryItem->m_blendIn->m_motionFadeTime, maxFadeTime);
				curve = pDcEntryItem->m_blendIn->m_curve;
			}

			// Check if there is a specific transition for this enter animation
			bool mirror = false;
			if (pDcEntryItem)
			{
				mirror = pDcEntryItem->m_flags & DC::kApEntryItemFlagsMirror;
			}

			// Set up enter state data
			AI::SetPluggableAnim(pNavChar, entryDef.m_entryAnimId, mirror);

			startPhase = m_entryDef.m_phase;
			apReference = m_entryDef.m_apReference;

			if (pDcEntryItem->m_flags & DC::kApEntryItemFlagsDefaultEntry)
			{
				const SkeletonId skelId = pNavChar->GetSkeletonId();
				const Locator& alignPs = pNavChar->GetLocatorPs();
				Locator rotApRefPs;
				if (FindApReferenceFromAlign(skelId, pAnim, alignPs, &rotApRefPs, SID("apReference"), startPhase, mirror))
				{
					apReference.SetLocatorPs(rotApRefPs);
				}
			}

			AiLogAnim(pNavChar,
					  "IAiPerchController::Enter() anim: '%s' phase: %0.2f dist: %0.1fm\n",
					  DevKitOnly_StringIdToString(entryDef.m_entryAnimId),
					  entryDef.m_phase,
					  float(Dist(entryDef.m_entryNavLoc.GetPosPs(), pNavChar->GetTranslationPs())));
		}
		else
		{
			// Skip the enter state and go directly to the destination state
			animStateId = SID("s_perch-idle");
			AiLogAnim(pNavChar,
					  "IAiPerchController::Enter() Forcing blend to state '%s' Dist: %0.1fm\n",
					  DevKitOnly_StringIdToString(animStateId),
					  float(Dist(pNavChar->GetLastTranslationPs(),
								 pActionPack->GetDefaultEntryPointPs(GetNavControl()->GetActionPackEntryDistance()))));
		}

		const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		const bool hasAutoTransition = pBaseLayer->IsTransitionValid(SID("auto"));

		FadeToStateParams params;
		params.m_stateStartPhase = startPhase;
		params.m_apRef = apReference;
		params.m_apRefValid = true;
		params.m_animFadeTime = animFadeTime;
		params.m_motionFadeTime = motionFadeTime;
		params.m_freezeSrcState = false;
		params.m_freezeDestState = false;
		params.m_blendType = curve;
		params.m_newInstBehavior = FadeToStateParams::kSpawnNewTrack;
		params.m_preventBlendTimeOverrun = !hasAutoTransition;

		m_enterAction.FadeToState(pAnimControl, animStateId, params, AnimAction::kFinishOnNonTransitionalStateReached);

		m_enterAction.ClearSelfBlendParams();

		if (pDcEntryItem && pDcEntryItem->m_selfBlend)
		{
			m_enterAction.SetSelfBlendParams(pDcEntryItem->m_selfBlend, pActionPack->GetBoundFrame(), pNavChar, 1.0f);
		}
		else if (pAnim)
		{
			const float totalAnimTime = pAnim->m_pClipData->m_fNumFrameIntervals * pAnim->m_pClipData->m_secondsPerFrame;
			const float animTime = Limit01(1.0f - startPhase) * totalAnimTime;

			DC::SelfBlendParams manualParams;
			manualParams.m_time = animTime;
			manualParams.m_phase = startPhase;
			manualParams.m_curve = DC::kAnimCurveTypeUniformS;

			m_enterAction.SetSelfBlendParams(&manualParams, pActionPack->GetBoundFrame(), pNavChar, 1.0f);
		}

		const Vector entryVelocityPs = entryDef.m_entryVelocityPs;
		const Vector charVelocityPs = pNavChar->GetVelocityPs();
		const float desSpeed = Length(entryVelocityPs);
		const float projSpeed = Dot(SafeNormalize(entryVelocityPs, kZero), charVelocityPs);
		const float speedRatio = (desSpeed > 0.0f) ? (projSpeed / desSpeed) : 1.0f;
		pInfo->m_speedFactor = Limit(speedRatio, 0.8f, 1.1f);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool TeleportInto(ActionPack* pActionPack,
					  bool playEntireEntryAnim,
					  float fadeTime,
					  BoundFrame* pNewFrameOut,
					  uintptr_t apUserData = 0) override
	{
		if (pActionPack->GetType() != ActionPack::kPerchActionPack)
		{
			return false;
		}

		NavCharacterAdapter navChar = GetCharacterAdapter();

		ParentClass::TeleportInto(pActionPack, playEntireEntryAnim, fadeTime, pNewFrameOut, apUserData);

		ActionPackEntryDef entryDef;
		entryDef.m_forceBlendTime = fadeTime;
		ActionPackResolveInput input = MakeDefaultResolveInput(navChar);

		Enter(input, pActionPack, entryDef);

		if (pNewFrameOut)
		{
			AnimControl* pAnimControl = GetCharacter()->GetAnimControl();
			BoundFrame newFrame = pActionPack->GetBoundFrame();

			const StringId64 destAnimId = SID("perch-idle");

			Locator destAlignPs;
			if (FindAlignFromApReference(pAnimControl, destAnimId, newFrame.GetLocatorPs(), &destAlignPs))
			{
				newFrame.SetLocatorPs(destAlignPs);
			}

			*pNewFrameOut = newFrame;
		}

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool IsInValidPerchState() const
	{
		const NavCharacter* pNavChar = GetCharacter();
		const StringId64 stateId = pNavChar ? pNavChar->GetCurrentAnimState() : INVALID_STRING_ID_64;

		return stateId == SID("s_perch-idle");
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool RequestInvestigateFront() override
	{
		if (!m_enterAction.IsDone() || !m_exitAction.IsDone())
			return false;

		NavCharacter* pNavChar = GetCharacter();
		const ActionPack* pAp = pNavChar->GetEnteredActionPack();
		if (!pAp || pAp->GetType() != ActionPack::kPerchActionPack)
			return false;

		if (!IsInValidPerchState())
			return false;

		const PerchActionPack* pActionPack = static_cast<const PerchActionPack*>(pAp);
		AnimControl* pAnimControl = pNavChar->GetAnimControl();

		FadeToStateParams params;
		params.m_stateStartPhase = 0.0f;
		params.m_apRef = pActionPack->GetBoundFrame();
		params.m_apRefValid = true;
		params.m_animFadeTime = 0.4f;
		params.m_motionFadeTime = 0.4f;
		params.m_blendType = DC::kAnimCurveTypeEaseIn;

		m_investigateAction.FadeToState(pAnimControl, SID("s_investigate-look-around-perch-front"), params);

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool RequestInvestigateDown() override
	{
		if (!m_enterAction.IsDone() || !m_exitAction.IsDone())
			return false;

		NavCharacter* pNavChar = GetCharacter();
		const ActionPack* pAp = pNavChar->GetEnteredActionPack();
		if (!pAp || pAp->GetType() != ActionPack::kPerchActionPack)
			return false;

		if (!IsInValidPerchState())
			return false;

		const PerchActionPack* pActionPack = static_cast<const PerchActionPack*>(pAp);
		AnimControl* pAnimControl = pNavChar->GetAnimControl();

		FadeToStateParams params;
		params.m_stateStartPhase = 0.0f;
		params.m_apRef = pActionPack->GetBoundFrame();
		params.m_apRefValid = true;
		params.m_animFadeTime = 0.4f;
		params.m_motionFadeTime = 0.4f;
		params.m_blendType = DC::kAnimCurveTypeEaseIn;

		m_investigateAction.FadeToState(pAnimControl, SID("s_investigate-look-around-perch-down"), params);

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool RequestInvestigateDeadBody() override
	{
		if (!m_enterAction.IsDone() || !m_exitAction.IsDone())
			return false;

		NavCharacter* pNavChar = GetCharacter();
		const ActionPack* pAp = pNavChar->GetEnteredActionPack();
		if (!pAp || pAp->GetType() != ActionPack::kPerchActionPack)
			return false;

		if (!IsInValidPerchState())
			return false;

		const PerchActionPack* pActionPack = static_cast<const PerchActionPack*>(pAp);
		AnimControl* pAnimControl = pNavChar->GetAnimControl();

		FadeToStateParams params;
		params.m_stateStartPhase = 0.0f;
		params.m_apRef = pActionPack->GetBoundFrame();
		params.m_apRefValid = true;
		params.m_animFadeTime = 0.4f;
		params.m_motionFadeTime = 0.4f;
		params.m_blendType = DC::kAnimCurveTypeEaseIn;

		m_investigateAction.FadeToState(pAnimControl, SID("s_investigate-look-around-dead-body-perch"), params);

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool IsBusyInvestigating() const override
	{
		return !m_investigateAction.IsDone();
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void UpdateStatus() override
	{
		PROFILE(AI, PerchCon_UpdateStatus);

		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();

		m_exitAction.Update(pAnimControl);
		m_investigateAction.Update(pAnimControl);
		m_enterAction.Update(pAnimControl);

		StringId64 destStateId = INVALID_STRING_ID_64;

		if (AnimStateInstance* pDestInstance = m_exitAction.GetTransitionDestInstance(pAnimControl))
		{
			destStateId = pDestInstance->GetStateName();

			DC::AnimStateFlag& exitStateFlags = pDestInstance->GetMutableStateFlags();
			bool shouldEnableAdjustment = false;
			const bool adjustmentEnabled = (exitStateFlags & DC::kAnimStateFlagNoAdjustToNavMesh) == 0;

			if (m_exitPhase < 0.0f)
			{
				const Point myPosPs = pNavChar->GetTranslationPs();
				shouldEnableAdjustment = pNavChar->IsPointClearForMePs(myPosPs, false);
			}
			else
			{
				const float curPhase = pDestInstance->Phase();

				if (curPhase >= m_exitPhase)
				{
					shouldEnableAdjustment = true;
				}
			}

			if (shouldEnableAdjustment && !adjustmentEnabled)
			{
				AiLogAnim(pNavChar,
						  "Perch Controller enabling nav mesh adjustment for instance '%s' (%s) @ phase %0.3f\n",
						  DevKitOnly_StringIdToString(pDestInstance->GetStateName()),
						  pDestInstance->GetPhaseAnimArtItem().ToArtItem()
							  ? pDestInstance->GetPhaseAnimArtItem().ToArtItem()->GetName()
							  : "<null>",
						  pDestInstance->Phase());

				exitStateFlags &= ~DC::kAnimStateFlagNoAdjustToNavMesh;
				exitStateFlags |= DC::kAnimStateFlagAdjustApToRestrictAlign;
			}
		}

		if (m_exitAction.IsValid() && !m_exitAction.IsDone())
		{
			const float curPhase = m_exitAction.GetAnimPhase(pAnimControl);
			const float exitPhase = m_exitPhase;

			if ((exitPhase >= 0.0f) && (curPhase >= exitPhase))
			{
				m_exitHandoff.SetStateChangeRequestId(m_exitAction.GetStateChangeRequestId(), destStateId);
				pNavChar->ConfigureNavigationHandOff(m_exitHandoff, FILE_LINE_FUNC);

				m_exitAction.Reset();
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool IsBusy() const override
	{
		return !m_enterAction.IsDone() || !m_exitAction.IsDone();
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual U64 CollectHitReactionStateFlags() const override
	{
		return DC::kHitReactionStateMaskPerch;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const override
	{
		STRIP_IN_FINAL_BUILD;

		const U32 kMaxAnimEntries = 50;

		const NavCharacter* pNavChar = GetCharacter();
		const PerchActionPack* pPerchActionPack = (const PerchActionPack*)pActionPack;
		const BoundFrame apRef = pActionPack->GetBoundFrame();

		const DC::ApEntryItem* pChosenEntry = m_entryDef.m_pDcDef;

		const DC::ApEntryItemList* pDcEntryList = GetPerchEntries(pPerchActionPack->GetPerchType());
		DC::ApEntryItemList entryList;
		entryList.m_array = pDcEntryList ? pDcEntryList->m_array : nullptr;
		entryList.m_count = pDcEntryList ? pDcEntryList->m_count : 0;

		DebugDrawEntryAnims(input,
							pActionPack,
							apRef,
							entryList,
							pChosenEntry,
							g_aiGameOptions.m_perch);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void DebugDrawExits(const ActionPackResolveInput& input,
								const ActionPack* pActionPack,
								const IPathWaypoints* pPathPs) const override
	{
		STRIP_IN_FINAL_BUILD;

		const U32 kMaxAnimEntries = 50;

		const NavCharacter* pNavChar = GetCharacter();
		const PerchActionPack* pPerchActionPack = (const PerchActionPack*)pActionPack;
		const BoundFrame apRef = pActionPack->GetBoundFrame();

		const DC::ApExitAnimDef* pChosenExit = nullptr;

		DC::ApExitAnimList exitList;
		const DC::ApExitAnimList* pDcExitList = GetExits(pPerchActionPack->GetPerchType());
		exitList.m_array = pDcExitList ? pDcExitList->m_array : nullptr;
		exitList.m_count = pDcExitList ? pDcExitList->m_count : 0;

		DebugDrawExitAnims(input,
						   pActionPack,
						   apRef,
						   pPathPs,
						   exitList,
						   pChosenExit,
						   g_aiGameOptions.m_perch);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	const DC::ApExitAnimList* GetExits(StringId64 perchTypeId) const
	{
		NavCharacter* pNavChar = GetCharacter();
		if (const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig())
		{
			const DC::Map* pEntriesMap = ScriptManager::Lookup<DC::Map>(pConfig->m_perch.m_exitDefsSetId, nullptr);
			if (!pEntriesMap)
				return nullptr;

			const DC::ApExitAnimList* pEntries = ScriptManager::MapLookup<DC::ApExitAnimList>(pEntriesMap, perchTypeId);
			return pEntries;
		}

		return nullptr;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool PickBestPerchExit(const IPathWaypoints* pPathPs, ActionPackExitDef* pExitDef) const
	{
		const NavCharacter* pNavChar = GetCharacter();

		const PerchActionPack* pActionPack = m_hActionPack.ToActionPack<PerchActionPack>();
		if (!pActionPack)
			return nullptr;

		const DC::ApExitAnimList* pExitList = GetExits(m_perchType);
		const BoundFrame apRef = pActionPack->GetBoundFrame();

		ApExit::CharacterState cs;
		MakeExitCharacterState(pActionPack, pPathPs, &cs);

		return ResolveExit(cs, apRef, pExitList, pExitDef);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	void Exit(const PathWaypointsEx* pExitPathPs) override
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

		m_exitPhase = -1.0f;
		m_exitHandoff.Reset();
		m_exitAction.Reset();

		ActionPackExitDef selectedExit;

		Npc* pNpc = (Npc*)pNavChar;
		if (IAiLocomotionController* pLocomotionController = pNpc->GetAnimationControllers()->GetLocomotionController())
		{
			pLocomotionController->ConfigureWeaponUpDownOverlays(pLocomotionController->GetDesiredWeaponUpDownState(false));
		}

		const PerchActionPack* pActionPack = m_hActionPack.ToActionPack<PerchActionPack>();
		StringId64 stateNameId = SID("s_idle");

		if (!pActionPack)
		{
			AiLogAnim(pNavChar, "IAiPerchCombatController::Exit - No ActionPack! Fading to idle\n");
			FadeToStateParams params;
			params.m_animFadeTime = 0.4f;
			m_exitAction.FadeToState(pAnimControl, SID("s_idle"), params, AnimAction::kFinishOnTransitionTaken);
		}
		else
		{
			if (!m_enterAction.IsDone())
			{
				AiLogAnim(pNavChar, "IAiPerchCombatController::Exit - Aborting cover enter with 'idle' transition\n");
				m_exitAction.Request(pAnimControl, SID("idle"), AnimAction::kFinishOnNonTransitionalStateReached, &pNavChar->GetBoundFrame());
				m_enterAction.Reset();
			}
			else if (PickBestPerchExit(pExitPathPs, &selectedExit) && selectedExit.m_pDcDef)
			{
				FadeToStateParams params;
				params.m_apRefValid	   = true;
				params.m_apRef		   = selectedExit.m_apReference;
				params.m_customApRefId = selectedExit.m_apRefId;
				params.m_animFadeTime  = 0.4f;
				params.m_motionFadeTime = 0.4f;

				const DC::ApExitAnimDef* pExitPerf = selectedExit.m_pDcDef;

				AiLogAnim(pNavChar,
						  "IAiPerchCombatController::Exit - Exiting by anim '%s' to %s\n",
						  DevKitOnly_StringIdToString(pExitPerf->m_animName),
						  (pExitPerf->m_validMotions != 0) ? "MOVING" : "STOPPED");

				AI::SetPluggableAnim(pNavChar, pExitPerf->m_animName, selectedExit.m_mirror);

				DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>();
				
				if (pExitPerf->m_validMotions != 0)
				{
					m_exitAction.FadeToState(pAnimControl,
											 SID("s_cover-exit^moving"),
											 params,
											 AnimAction::kFinishOnAnimEndEarly);

					m_exitHandoff.m_motionType	= pNavChar->GetRequestedMotionType();
					stateNameId = SID("s_cover-exit^moving");
					pInfo->m_apExitTransitions = SID("ap-exit^move");
				}
				else
				{
					m_exitAction.FadeToState(pAnimControl,
											 SID("s_cover-exit^idle"),
											 params,
											 AnimAction::kFinishOnNonTransitionalStateReached);
					stateNameId = SID("s_cover-exit^idle");
					pInfo->m_apExitTransitions = SID("ap-exit^idle");
				}

				if (pExitPerf->m_navAnimHandoff)
				{
					m_exitHandoff.ConfigureFromDc(pExitPerf->m_navAnimHandoff);
				}
				else
				{
					m_exitHandoff.m_steeringPhase = pExitPerf->m_exitPhase;
				}

				m_exitPhase = pExitPerf->m_exitPhase;
				const float constraintPhase = m_exitPhase >= 0.0f ? m_exitPhase : 1.0f;

				SelfBlendAction::Params sbParams;
				sbParams.m_destAp = selectedExit.m_sbApReference;
				sbParams.m_constraintPhase = constraintPhase;
				sbParams.m_apChannelId = selectedExit.m_apRefId;

				if (pExitPerf->m_selfBlend)
				{
					sbParams.m_blendParams = *pExitPerf->m_selfBlend;
				}
				else
				{
					sbParams.m_blendParams.m_phase = 0.0f;
					sbParams.m_blendParams.m_time = GetDuration(pAnimControl->LookupAnim(pExitPerf->m_animName).ToArtItem());
					sbParams.m_blendParams.m_curve = DC::kAnimCurveTypeEaseOut;
				}

				m_exitAction.ConfigureSelfBlend(pNavChar, sbParams);
			}
			else
			{
				const BoundFrame perchApRef = pActionPack->GetBoundFrame();

				AiLogAnim(pNavChar, "IAiPerchCombatController::Exit - No exit anim, fading to idle\n");
				FadeToStateParams params;
				params.m_apRef = perchApRef;
				params.m_apRefValid = true;
				params.m_animFadeTime = 0.4f;
				m_exitAction.FadeToState(pAnimControl, SID("s_idle"), params, AnimAction::kFinishOnTransitionTaken);
			}
		}

		m_exitHandoff.SetStateChangeRequestId(m_exitAction.GetStateChangeRequestId(), stateNameId);

		pNavChar->ConfigureNavigationHandOff(m_exitHandoff, FILE_LINE_FUNC); // if we're self blending we'll need to call this again, but that should be fine
	}

	virtual void NotifyDemeanorChange() override
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
		AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

		if (!pBaseLayer || pBaseLayer->AreTransitionsPending())
			return;

		const StringId64 transitionId = SID("demeanor-change");
		if (!pBaseLayer->IsTransitionValid(transitionId))
			return;

		pBaseLayer->RequestTransition(transitionId);
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiPerchController* CreateAiPerchController()
{
	return NDI_NEW AiPerchController;
}
