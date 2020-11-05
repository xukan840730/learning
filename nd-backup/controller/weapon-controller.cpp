/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/weapon-controller.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/anim/joint-modifiers/joint-modifiers.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/event.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/anim/gesture-handle.h"
#include "gamelib/audio/arm.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/ai/component/move-performance.h"
#include "gamelib/level/artitem.h"
#include "gamelib/scriptx/h/npc-demeanor-defines.h"

#include "game/ai/agent/npc.h"
#include "game/ai/characters/human.h"
#include "game/ai/component/prop-inventory.h"
#include "game/ai/component/shooting-logic.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/joint-modifiers/weapon-grip-modifier.h"
#include "game/ai/knowledge/entity.h"
#include "game/player/player-fire.h"
#include "game/scriptx/h/ai-weapon-defines.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/gesture-script-defines.h"
#include "game/scriptx/h/hit-reactions-defines.h"
#include "game/scriptx/h/npc-weapon-defines.h"
#include "game/weapon/process-weapon-base.h"
#include "game/weapon/process-weapon.h"
#include "game/weapon/weapon-toss.h"

/// --------------------------------------------------------------------------------------------------------------- ///
#if FINAL_BUILD
#define WeaponLogStr(str)
#define WeaponLog(str, ...)
#else
#define WeaponLogStr(str)                                                                                              \
	AiLogAnim(GetCharacter(),                                                                                          \
			  AI_LOG,                                                                                                  \
			  "[WeaponCtrl] [%s %d %s%s%s] " str,                                                                      \
			  GetWeaponName(),                                                                                         \
			  GetPrimaryWeaponUid(),                                                                                   \
			  GetGunStateName(m_gunState),                                                                             \
			  m_animRequestedGunState == kGunStateMax ? "" : " : ",                                                    \
			  m_animRequestedGunState == kGunStateMax ? "" : GetGunStateName(m_animRequestedGunState))

#define WeaponLog(str, ...)                                                                                            \
	AiLogAnim(GetCharacter(),                                                                                          \
			  AI_LOG,                                                                                                  \
			  "[WeaponCtrl] [%s %d %s%s%s] " str,                                                                      \
			  GetWeaponName(),                                                                                         \
			  GetPrimaryWeaponUid(),                                                                                   \
			  GetGunStateName(m_gunState),                                                                             \
			  m_animRequestedGunState == kGunStateMax ? "" : " : ",                                                    \
			  m_animRequestedGunState == kGunStateMax ? "" : GetGunStateName(m_animRequestedGunState),                 \
			  __VA_ARGS__)
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
enum class WeaponSuppressionMode
{
	kRememberRequests,
	kIgnoreRequests
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AiWeaponController : public IAiWeaponController
{
public:
	typedef IAiWeaponController ParentClass;

	AiWeaponController();

	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void Reset() override;

	void RefreshWeaponAnimSet();

	virtual void SetWeaponAnimSetList(StringId64 weaponSetListId) override;
	virtual StringId64 GetWeaponAnimSetList() const override;

	const DC::NpcWeaponAnimSet* GetWeaponAnimSet() const;

	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual bool IsBusy() const override;
	virtual bool ShouldInterruptNavigation() const override;

	virtual void Fire() override;
	virtual void ConfigureCharacter(Demeanor demeanor,
									const DC::NpcDemeanorDef* pDemeanorDef,
									const NdAiAnimationConfig* pAnimConfig) override;

	bool TryMovingReload();
	void PlayReloadAnim(AnimControl* pAnimControl, const StringId64 weaponDefId);

	virtual void Reload() override;
	virtual void Rechamber() override;
	virtual bool IsReloading() const override;
	virtual void AbortReload() override;

	virtual bool IsRecoiling() const override;
	virtual bool IsDoingWeaponSwitch() const override { return m_unholsteringForSwitch; }

	void AbortAnims(bool immediate);

	virtual void Abort(bool immediate) override;

	virtual void SetWeaponIkDesired(bool desired) override;

	virtual void RequestGunState(GunState gs, bool slow = false, const DC::BlendParams* pBlend = nullptr) override;
	bool PlayUnholsterAnimation(bool slow = false, const DC::BlendParams* pBlend = nullptr);
	bool PlayHolsterAnimation(bool slow = false, const DC::BlendParams* pBlend = nullptr);
	bool PlayWeaponSwitchAnimation(StringId64 newWeaponId);

	bool TryPlayReloadGesture();
	bool TryPlayRechamberGesture();
	bool TryPlayUnholsterGesture(bool slow = false, const DC::BlendParams* pBlend = nullptr);
	bool TryPlayHolsterGesture(bool slow = false, const DC::BlendParams* pBlend = nullptr);
	bool TryPlayWeaponDownGesture();
	bool TryPlayWeaponUpGesture();
	bool TryPlayWeaponSwitchGesture(StringId64 newWeaponId);

	virtual bool EnableGunOutSuppression() override;
	virtual bool DisableGunOutSuppression() override;
	virtual bool IsGunOutSuppressed() const override;

	virtual void SuppressWeaponChanges(float durationSec) override
	{
		if ((durationSec > 0.0f) && (m_changeSuppressionSec < 0.0f))
		{
			WeaponLog("SuppressWeaponChanges: Activating suppression for %0.4f seconds\n", durationSec);
		}
/*
		else if (durationSec > 0.0f && m_changeSuppressionSec > 0.0f && durationSec > m_changeSuppressionSec)
		{
			WeaponLog("SuppressWeaponChanges: Extending suppression to %0.4f seconds\n", durationSec);
		}
*/
		else if (durationSec <= 0.0f && m_changeSuppressionSec > 0.0f)
		{
			WeaponLog("SuppressWeaponChanges: Disabling suppression (suppressed state: %s)\n",
					  GetGunStateName(m_suppressedRequest.m_gs));
		}

		m_changeSuppressionSec = durationSec;
	}

	virtual bool TryExtendSuppression(float durationSec) override
	{
		bool updated = false;
		if (m_changeSuppressionSec > 0.0f)
		{
			m_changeSuppressionSec = Max(durationSec, m_changeSuppressionSec);
			updated = true;
		}

		return updated;
	}

	bool IsOkayToExpireSuppression(const Npc* pNpc) const;

	virtual void AllowGunOut(bool allow) override;
	virtual GunState GetRequestedGunState() const override;
	virtual GunState GetCurrentGunState() const override;
	virtual bool IsPendingGunState(GunState gs) const override;

	virtual void SetWeaponInHand(const ProcessWeaponBase* pWeapon, bool inHand) override;
	virtual void ForceGunState(GunState gs,
							   bool overrideEqualCheck = false,
							   bool abortAnims		   = false,
							   float blendTime		   = 0.0f,
							   bool immediate		   = false) override;
	virtual void EnforceGunState(ProcessWeaponBase* pWeapon, 
								 const GunState gs, 
								 bool immediate = false, 
								 float blendTime = 0.0f, 
								 bool force = false) override;
	bool IsGunStateCorrect(const ProcessWeaponBase* pWeapon, const GunState gs) const;
	bool ValidateWeaponAttachments() const;

	virtual bool GrenadeToss(const BoundFrame& bFrame, const bool spawnHeldGrenadeIfNotPresent = true) override;
	virtual void AbortGrenadeToss() override;

	virtual bool IsThrowingGrenade() const override;
	virtual void DrawGrenadeRock() override;
	virtual bool DrawGrenade(const bool spawnHeldGrenadeIfNotPresent = true) override;

	virtual ProcessWeapon* WeaponToss(Character& receiver, ForgetAboutWeaponFunc forget) override;
	virtual float GetAnimPhase() const override;

	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;

	virtual void RequestWeaponUp() override;
	virtual void RequestWeaponDown() override;
	virtual bool IsWeaponUpRequested() const override;

	virtual void UpdateWeaponUpDownPercent(float pcnt, float target) override;

	virtual U64 CollectHitReactionStateFlags() const override;

	virtual void UpdateOverlays() override;

	virtual bool RequestPrimaryWeapon(StringId64 primaryWeaponId) override;
	virtual bool ForcePrimaryWeapon(StringId64 primaryWeaponId) override;
	virtual bool IsPrimaryWeaponUpToDate() const override;
	virtual void ApplyPrimaryWeaponSwitch() override;
	virtual void ApplyAnimRequestedWeaponSwitch() override;

	virtual ProcessWeaponBase* GetPrimaryWeapon() const override;
	virtual ProcessWeaponBase* GetRequestedPrimaryWeapon() const override;
	virtual ProcessWeaponBase* GetAnimRequestedWeapon(bool allowRequestedFallback = true) const override;

	virtual void RequestWeaponIdleAnim(StringId64 animId) override;
	void UpdateWeaponIdleAnim();

	bool TryPlayGesture(StringId64 gestureId, GestureHandle* pGestureHandleOut, const DC::BlendParams* pBlend = nullptr);

	const char* GetWeaponName() const
	{
		if (const ProcessWeaponBase* pWeapon = GetPrimaryWeapon())
		{
			return DevKitOnly_StringIdToString(pWeapon->GetWeaponDefId());
		}

		return "<none>";
	}

	I32F GetPrimaryWeaponUid() const 
	{
		const Npc* pNpc = Npc::FromProcess(GetCharacter());
		const PropInventory* pPropInventory = pNpc ? pNpc->GetPropInventory() : nullptr;

		return pPropInventory ? I32F(pPropInventory->GetPrimaryWeaponUid().GetValue()) : -1;
	}

	void SetAnimRequestedGunState(GunState gs, const char* srcStr)
	{
		if (m_animRequestedGunState != gs)
		{
			WeaponLog("SetAnimRequestedGunState(%s) : %s\n", GetGunStateName(gs), srcStr ? srcStr : "<none>");
			m_animRequestedGunState = gs;
		}
	}

private:
	struct ChangeParams
	{
		GunState m_gs = kGunStateMax;
		DC::BlendParams m_blend;
		bool m_blendValid = false;
		bool m_slow = false;
	};

	bool HandleHorseAimGesture(StringId64 desiredGesture);
	HorseAimStateMachine* GetHorseAimStateMachine() const;

	AnimAction m_grenadeTossAction;
	AnimAction m_grenadeDrawAction;
	AnimAction m_weaponTossAction;
	AnimAction m_parWeaponLayerAction;
	AnimAction m_addWeaponLayerAction;
	GestureHandle m_gunStateChangeGestureHandle;
	GestureHandle m_weaponUpDownGestureHandle;
	GestureHandle m_reloadGestureHandle;
	GestureHandle m_rechamberGestureHandle;
	GestureHandle m_fireRecoilGestureHandle;

	ProcessWeaponTossAction m_processWeaponTossAction;

	GunState m_gunState;
	GunState m_animRequestedGunState;

	MovePerformance m_reloadPerformance;

	CachedAnimLookup m_weaponGripAnim;
	CachedAnimLookup m_highHandsAnim;

	CachedAnimLookup m_holsterParAnim;
	CachedAnimLookup m_unholsterParAnim;
	CachedAnimLookup m_holsterAddAnim;
	CachedAnimLookup m_unholsterAddAnim;

	CachedAnimLookup m_slowHolsterParAnim;
	CachedAnimLookup m_slowUnholsterParAnim;
	CachedAnimLookup m_slowHolsterAddAnim;
	CachedAnimLookup m_slowUnholsterAddAnim;

	StringId64 m_weaponSetListId;

	float m_changeSuppressionSec;
	WeaponSuppressionMode m_changeSuppressionMode;
	ChangeParams m_suppressedRequest;
	StringId64 m_suppressedPrimaryId;

	float m_weaponUpFactor;
	float m_weaponUpTargetFactor;
	SpringTracker<float> m_weaponUpDownBlendSpring;

	StringId64 m_requestedPrimaryWeaponId;
	PropInventoryId m_requestedPrimaryWeaponUid;

	StringId64 m_animRequestedWeaponId;
	PropInventoryId m_animRequestedWeaponUid;

	GunState m_savedGunState;

	StringId64 m_requestedWeaponIdleAnim;
	TimeFrame m_weaponIdleAnimDecay;

	bool m_reloadRequested;
	bool m_rechamberRequested;
	bool m_reloading;
	bool m_weaponIkDesired;
	bool m_effSuppressedGunOut; // an ugly approach to approximate what this should be: a priority based request system
	bool m_wepaonUpDownDirty;
	bool m_weaponUpRequested;
	bool m_allowGunOut;
	bool m_unholsteringForSwitch;
	bool m_slowWeaponSwitch;
	bool m_grenadeDrawn;
	bool m_grenadeThrown;
	bool m_mailedLog;
};

/// --------------------------------------------------------------------------------------------------------------- ///
AiWeaponController::AiWeaponController()
	: m_gunState(kGunStateMax)
	, m_animRequestedGunState(kGunStateMax)
	, m_parWeaponLayerAction(SID("weapon-partial"))
	, m_addWeaponLayerAction(SID("weapon-additive"))
	, m_reloadRequested(false)
	, m_rechamberRequested(false)
	, m_reloading(false)
	, m_wepaonUpDownDirty(false)
	, m_weaponUpRequested(false)
	, m_weaponUpFactor(0.0f)
	, m_weaponUpTargetFactor(0.0f)
	, m_weaponIkDesired(true)
	, m_effSuppressedGunOut(false)
	, m_allowGunOut(true)
	, m_weaponSetListId(SID("*default-weapon-set-list*"))
	, m_requestedPrimaryWeaponId(INVALID_STRING_ID_64)
	, m_requestedPrimaryWeaponUid(INVALID_PROPINV_ID)
	, m_savedGunState(kGunStateMax)
	, m_unholsteringForSwitch(false)
	, m_slowWeaponSwitch(false)
	, m_grenadeDrawn(false)
	, m_grenadeThrown(false)
	, m_mailedLog(false)
	, m_requestedWeaponIdleAnim(INVALID_STRING_ID_64)
	, m_weaponIdleAnimDecay(TimeFrameNegInfinity())
	, m_changeSuppressionSec(-1.0f)
	, m_changeSuppressionMode(WeaponSuppressionMode::kRememberRequests)
	, m_suppressedPrimaryId(INVALID_STRING_ID_64)
{
	m_weaponGripAnim.SetSourceId(SID("weapon-grip"));
	m_highHandsAnim.SetSourceId(SID("hands-high"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	ParentClass::Init(pNavChar, pNavControl);

	m_grenadeTossAction.Reset();
	m_grenadeDrawAction.Reset();
	m_weaponTossAction.Reset();
	m_parWeaponLayerAction.Reset();
	m_addWeaponLayerAction.Reset();

	RefreshWeaponAnimSet();

	m_reloadPerformance.AllocateCache(8);

	m_requestedPrimaryWeaponId	= INVALID_STRING_ID_64;
	m_requestedPrimaryWeaponUid = INVALID_PROPINV_ID;
	m_animRequestedWeaponId		= INVALID_STRING_ID_64;
	m_animRequestedWeaponUid	= INVALID_PROPINV_ID;

	m_savedGunState			= kGunStateMax;
	m_unholsteringForSwitch = false;
	m_slowWeaponSwitch		= false;
	m_grenadeDrawn	= false;
	m_grenadeThrown = false;
	m_mailedLog = false;

	m_changeSuppressionSec = -1.0f;
	m_suppressedRequest = ChangeParams();
	m_suppressedPrimaryId  = INVALID_STRING_ID_64;

	m_changeSuppressionMode = pNavChar->IsBuddyNpc() ? WeaponSuppressionMode::kIgnoreRequests
													 : WeaponSuppressionMode::kRememberRequests;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_reloadPerformance.Relocate(deltaPos, lowerBound, upperBound);

	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::Reset()
{
	m_grenadeTossAction.Reset();
	m_grenadeDrawAction.Reset();
	m_weaponTossAction.Reset();
	m_grenadeDrawn = false;
	m_grenadeThrown = false;
	// m_parWeaponLayerAction.Reset();		// don't reset these because it would no longer blend out
	// m_addWeaponLayerAction.Reset();		// the layer when it finishes (bugs 46064 amd 45677)
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::RefreshWeaponAnimSet()
{
	if (const DC::NpcWeaponAnimSet* pWeaponSet = GetWeaponAnimSet())
	{
		m_holsterParAnim.SetSourceId(pWeaponSet->m_holsterParAnim);
		m_holsterAddAnim.SetSourceId(pWeaponSet->m_holsterAddAnim);
		m_unholsterParAnim.SetSourceId(pWeaponSet->m_unholsterParAnim);
		m_unholsterAddAnim.SetSourceId(pWeaponSet->m_unholsterAddAnim);

		m_slowHolsterParAnim.SetSourceId(pWeaponSet->m_slowHolsterParAnim);
		m_slowHolsterAddAnim.SetSourceId(pWeaponSet->m_slowHolsterAddAnim);
		m_slowUnholsterParAnim.SetSourceId(pWeaponSet->m_slowUnholsterParAnim);
		m_slowUnholsterAddAnim.SetSourceId(pWeaponSet->m_slowUnholsterAddAnim);

	}
	else
	{
		m_holsterParAnim.SetSourceId(SID("holster-weapon-quick-par"));
		m_holsterAddAnim.SetSourceId(SID("holster-weapon-quick-add"));
		m_unholsterParAnim.SetSourceId(SID("unholster-weapon-quick-par"));
		m_unholsterAddAnim.SetSourceId(SID("unholster-weapon-quick-add"));

		m_slowHolsterParAnim.SetSourceId(SID("holster-weapon-slow-par"));
		m_slowHolsterAddAnim.SetSourceId(SID("holster-weapon-slow-add"));
		m_slowUnholsterParAnim.SetSourceId(SID("unholster-weapon-slow-par"));
		m_slowUnholsterAddAnim.SetSourceId(SID("unholster-weapon-slow-add"));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::SetWeaponAnimSetList(StringId64 weaponSetListId)
{
	m_weaponSetListId = weaponSetListId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiWeaponController::GetWeaponAnimSetList() const
{
	return m_weaponSetListId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::NpcWeaponAnimSet* AiWeaponController::GetWeaponAnimSet() const
{
	const DC::NpcWeaponAnimSetList* pList = ScriptManager::Lookup<DC::NpcWeaponAnimSetList>(m_weaponSetListId, nullptr);
	if (!pList)
		return nullptr;

	const NavCharacter* pNavChar	 = GetCharacter();
	const ProcessWeaponBase* pWeapon = pNavChar->GetWeapon();
	const DC::WeaponAnimType curWat	 = pWeapon ? pWeapon->GetWeaponAnimType() : DC::kWeaponAnimTypeNoWeapon;
	const I32 watMask				 = (1U << curWat);

	for (U32F i = 0; i < pList->m_setLength; ++i)
	{
		const DC::NpcWeaponAnimSet* pSet = &pList->m_setArray[i];

		if (0 == (watMask & pSet->m_weaponAnimTypeMask))
			continue;

		return pSet;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::UpdateOverlays()
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	Npc* pNpc = Npc::FromProcess(pNavChar);

	const ProcessWeaponBase* pWeapon = pNpc ? pNpc->GetHeldGrenade().ToProcess() : nullptr;

	if (!pWeapon)
	{
		pWeapon = pNavChar->GetWeapon();
	}

	const DC::WeaponArtDef* pWeaponArtDef = pWeapon ? pWeapon->GetWeaponArtDef() : nullptr;

	const char* overlayName = "none";

	if (pWeaponArtDef)
	{
		if (!pWeaponArtDef->m_animOverlayWeaponIdName.IsEmpty())
		{
			overlayName = pWeaponArtDef->m_animOverlayWeaponIdName.GetString();
		}
		else if (!pWeaponArtDef->m_weaponIdName.IsEmpty())
		{
			overlayName = pWeaponArtDef->m_weaponIdName.GetString();
		}
	}

	const char* pOverlayBaseName = pNavChar->GetOverlayBaseName();
	const StringId64 overlayId = AI::CreateNpcAnimOverlayName(pOverlayBaseName, "weapon-id", overlayName);

	AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays();

	if (pOverlays)
	{
		const bool changed = FALSE_IN_FINAL_BUILD(pOverlays->GetCurrentOverlay(SID("weapon-id")) != overlayId);

		if (!pOverlays->SetOverlaySet(overlayId, true))
		{
			WeaponLog("FAILED to set weapon-id overlay to '%s'!\n", overlayName);
			pOverlays->ClearLayer(SID("weapon-id"));
		}
		else if (changed)
		{
			WeaponLog("Updating weapon-id overlay to '%s' (was '%s')\n",
					  overlayName,
					  DevKitOnly_StringIdToString(pOverlays->GetCurrentOverlay(SID("weapon-id"))));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool UpdateSimpleLayerAnim(const Npc* pNpc,
								  AnimControl* pAnimControl,
								  StringId64 layerId,
								  CachedAnimLookup& lookup,
								  bool debug,
								  const char* debugStr)
{
	AnimSimpleLayer* pLayer = pAnimControl ? pAnimControl->GetSimpleLayerById(layerId) : nullptr;

	if (!pLayer)
		return false;

	CachedAnimLookup newLookup = pAnimControl->LookupAnimCached(lookup);

	const StringId64 oldId = lookup.GetFinalResolvedId();
	const StringId64 newId = newLookup.GetFinalResolvedId();

	const AnimSimpleInstance* pCurInst = pLayer->CurrentInstance();
	const ArtItemAnim* pCurAnim = pCurInst ? pCurInst->GetAnim().ToArtItem() : nullptr;
	const ArtItemAnim* pOldAnim = lookup.GetAnim().ToArtItem();
	const ArtItemAnim* pNewAnim = newLookup.GetAnim().ToArtItem();

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		MsgCon("[%s] %s:\n", pNpc->GetName(), debugStr ? debugStr : "??? Simple Layer");
		MsgCon("     Old: %s [%s]\n", pOldAnim ? pOldAnim->GetName() : "<null>", DevKitOnly_StringIdToString(oldId));
		MsgCon("     New: %s [%s]\n", pNewAnim ? pNewAnim->GetName() : "<null>", DevKitOnly_StringIdToString(newId));
		MsgCon("     Cur: %s @ %0.4f\n", pCurAnim ? pCurAnim->GetName() : "<null>", pCurInst ? pCurInst->GetPhase() : -1.0f);
		MsgCon("     Layer: %0.4f -> %0.4f\n", pLayer->GetCurrentFade(), pLayer->GetDesiredFade());
	}

	bool updated = false;

	if ((newId != oldId) || (pOldAnim != pNewAnim) || (pCurAnim != pNewAnim))
	{
		if (pNewAnim)
		{
			updated = true;

			AnimSimpleLayer::FadeRequestParams params;
			params.m_fadeTime = 0.1f;

			if (pLayer->RequestFadeToAnim(newId, params))
				pLayer->Fade(1.0f, 0.1f);
			else
				pLayer->Fade(0.0f, 0.1f);
		}
		else
		{
			// Fade out the grip anim if the new weapon mode doesn't have one.
			pLayer->Fade(0.0f, 0.1f);
		}
	}
	else if (pNewAnim && pLayer->GetDesiredFade() < 0.0f)
	{
		pLayer->Fade(1.0f, 0.1f);
	}

	lookup = newLookup;
	return updated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::IsOkayToExpireSuppression(const Npc* pNpc) const
{
	if (m_changeSuppressionMode == WeaponSuppressionMode::kIgnoreRequests)
		return true;

	if (pNpc->IsNavigationInterrupted())
		return false;

	if (pNpc->GetEnteredActionPack())
		return false;

	if (pNpc->GetCurrentMeleeAction())
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::RequestAnimations()
{
	PROFILE(AI, WeapCon_RequestAnimations);
	NavCharacter* pNavChar = GetCharacter();
	const Npc* pNpc		   = Npc::FromProcess(pNavChar);
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const float dt = pNavChar->GetClock()->GetDeltaTimeInSeconds();

	if (m_changeSuppressionSec > 0.0f)
	{
		m_changeSuppressionSec -= dt;

		if (m_changeSuppressionSec <= 0.0f)
		{
			if (IsOkayToExpireSuppression(pNpc))
			{
				WeaponLog("Suppression expired! gs: %s primary: %s\n",
						  GetGunStateName(m_suppressedRequest.m_gs),
						  DevKitOnly_StringIdToString(m_suppressedPrimaryId));

				if ((m_changeSuppressionMode == WeaponSuppressionMode::kRememberRequests) || pNpc->IsLobotomised())
				{
					if (m_suppressedPrimaryId)
					{
						if (RequestPrimaryWeapon(m_suppressedPrimaryId))
						{
							ApplyPrimaryWeaponSwitch();

							m_unholsteringForSwitch		= false;
							m_requestedPrimaryWeaponId	= INVALID_STRING_ID_64;
							m_requestedPrimaryWeaponUid = INVALID_PROPINV_ID;
						}

						m_suppressedPrimaryId = INVALID_STRING_ID_64;
					}

					if (m_suppressedRequest.m_gs != kGunStateMax)
					{
						RequestGunState(m_suppressedRequest.m_gs,
										m_suppressedRequest.m_slow,
										m_suppressedRequest.m_blendValid ? &m_suppressedRequest.m_blend : nullptr);

						m_suppressedRequest = ChangeParams();
					}
				}
			}
			else
			{
				m_changeSuppressionSec = Frames(2).ToSeconds();
			}
		}
	}

	bool partialDone = false;
	bool addDone	 = false;
	bool gestureDone = true;

	if (const IGestureController* pGestureController = pNavChar->GetGestureController())
	{
		if (m_gunStateChangeGestureHandle.Playing(pGestureController))
		{
			gestureDone = false;
		}
	}

	if (m_parWeaponLayerAction.IsValid())
	{
		if (m_parWeaponLayerAction.IsDone())
		{
			AnimLayer* pLayer = pAnimControl->GetLayerById(SID("weapon-partial"));
			if (pLayer->GetDesiredFade() > 0.0f)
			{
				pLayer->Fade(0.0f, 0.3f, DC::kAnimCurveTypeUniformS);
			}

			if (pLayer->GetCurrentFade() <= 0.0f)
			{
				partialDone = true;
				m_parWeaponLayerAction.Reset();
			}
		}
	}
	else
	{
		partialDone = true;
	}

	if (m_addWeaponLayerAction.IsValid())
	{
		if (m_addWeaponLayerAction.IsDone())
		{
			AnimLayer* pLayer = pAnimControl->GetLayerById(SID("weapon-additive"));
			if (pLayer->GetDesiredFade() > 0.0f)
			{
				pLayer->Fade(0.0f, 0.3f, DC::kAnimCurveTypeUniformS);
			}

			if (pLayer->GetCurrentFade() <= 0.0f)
			{
				addDone = true;
				m_addWeaponLayerAction.Reset();
			}
		}
	}
	else
	{
		addDone = true;
	}

	if (m_rechamberRequested)
	{
		const float rechamberPhase = 0.5f;

		const bool addReady = !m_addWeaponLayerAction.IsValid()
							  || (m_addWeaponLayerAction.GetAnimPhase(pAnimControl) >= rechamberPhase);
		const bool gestureReady = !m_fireRecoilGestureHandle.Assigned()
								  || (m_fireRecoilGestureHandle.GetPhase(pAnimControl) >= rechamberPhase);

		if (addReady && gestureReady)
		{
			WeaponLogStr("Rechamber!\n");
			Rechamber();
			m_rechamberRequested = false;
		}
	}

	if (partialDone && addDone && gestureDone)
	{
		if (m_animRequestedWeaponId)
		{
			WeaponLog("Animations are done, applying anim requested weapon (%s)\n",
					  DevKitOnly_StringIdToString(m_animRequestedWeaponId));

			ApplyAnimRequestedWeaponSwitch();

			m_animRequestedWeaponId	 = INVALID_STRING_ID_64;
			m_animRequestedWeaponUid = INVALID_PROPINV_ID;

			if (m_gunStateChangeGestureHandle.Assigned())
			{
				WeaponLog("WARNING: We finished playing gesture '%s' before applying primary weapon switch\n",
						  DevKitOnly_StringIdToString(m_gunStateChangeGestureHandle.m_originalGestureId));

/*
				if (!m_mailedLog)
				{
					MailNpcLogTo(pNavChar,
								 "john_bellomy@naughtydog.com;michal_mach@naughtydog.com;morgan_earl@naughtydog.com",
								 "Finished WeaponSwitch Anims Early",
								 FILE_LINE_FUNC);
					m_mailedLog = true;
				}
*/
			}
		}

		if (m_animRequestedGunState != kGunStateMax)
		{
			WeaponLog("Animations are done, applying anim requested gun state (%s)\n",
					  GetGunStateName(m_animRequestedGunState));

			ForceGunState(m_animRequestedGunState);
			m_animRequestedGunState = kGunStateMax;
		}

		m_reloadRequested = false;
	}

	const bool isInTap = pNavChar->IsUsingTraversalActionPack();

	if (partialDone && addDone && gestureDone && (m_requestedPrimaryWeaponUid != INVALID_PROPINV_ID) && !isInTap)
	{
		if (m_gunState == kGunStateHolstered)
		{
			// We might be coming in here a second time after aborting a previous attempt, so no need
			// to switch, just try unholstering again.
			if (!m_unholsteringForSwitch)
			{
				ApplyPrimaryWeaponSwitch();
			}

			if (m_savedGunState == kGunStateOut)
			{
				WeaponLogStr("Unholstering after primary weapon switch\n");

				if (!PlayUnholsterAnimation(m_slowWeaponSwitch))
				{
					ForceGunState(kGunStateOut);
				}

				m_unholsteringForSwitch = true;
			}
			else
			{
				m_requestedPrimaryWeaponId = INVALID_STRING_ID_64;
				m_requestedPrimaryWeaponUid = INVALID_PROPINV_ID;
				m_savedGunState = kGunStateMax;
				m_unholsteringForSwitch = false;
				m_slowWeaponSwitch = false;
			}
		}
		else if (m_unholsteringForSwitch)
		{
			WeaponLogStr("Finished switching primary weapon\n");

			const ProcessWeaponBase* pWeapon = pNavChar->GetWeapon();

			if (!IsGunStateCorrect(pWeapon, m_savedGunState))
			{
				WeaponLog("Weapon '%s' doesn't seem to be correctly attached (desired state: %s), forcing!\n",
						  DevKitOnly_StringIdToString(pWeapon->GetWeaponDefId()),
						  GetGunStateName(m_savedGunState));

				ForceGunState(m_savedGunState, true, true);
			}

			m_requestedPrimaryWeaponId = INVALID_STRING_ID_64;
			m_requestedPrimaryWeaponUid = INVALID_PROPINV_ID;
			m_savedGunState = kGunStateMax;
			m_unholsteringForSwitch = false;
			m_slowWeaponSwitch = false;
		}
		else if (PlayWeaponSwitchAnimation(m_requestedPrimaryWeaponId))
		{
			m_unholsteringForSwitch = true;
			m_animRequestedWeaponId = m_requestedPrimaryWeaponId;
			m_animRequestedWeaponUid = m_requestedPrimaryWeaponUid;
		}
		// a step above us might have aborted, set up a custom configuration (like forcing a weapon out),
		// and cleared the requested primary weapon ID
		else if (m_requestedPrimaryWeaponId != INVALID_STRING_ID_64 && !PlayHolsterAnimation(m_slowWeaponSwitch))
		{
			ForceGunState(kGunStateHolstered);
		}
	}

	// Cache the reloading flag
	if (m_gunState == kGunStateOut && m_reloadRequested)
	{
		const AnimLayer* pParLayer = pAnimControl->GetLayerById(SID("weapon-partial"));
		const bool parBusy		   = pParLayer->GetCurrentFade() > 0.0f;
		m_reloading				   = parBusy;
	}
	else
	{
		m_reloading = false;
	}

	const bool debugSimpleLayers = FALSE_IN_FINAL_BUILD(DebugSelection::Get().IsProcessOrNoneSelected(pNpc)
														&& g_navCharOptions.m_displayWeaponController);

	UpdateSimpleLayerAnim(pNpc, pAnimControl, SID("hands"), m_weaponGripAnim, debugSimpleLayers, "Weapon Grip");
	UpdateSimpleLayerAnim(pNpc, pAnimControl, SID("hands-high"), m_highHandsAnim, debugSimpleLayers, "Hands High");

	if (IWeaponGripModifier* pGripModifier = pNavChar->GetJointModifiers()->GetModifier<IWeaponGripModifier>(kWeaponGripModifier))
	{
		// This ensures that when we die we fade out the grip animation
		const bool weaponIkDesired = m_weaponIkDesired && (pNavChar->GetWeapon() != nullptr);
		const bool weaponIkActive = pGripModifier->IsEnabled() /*&& (pGripModifier->GetDesiredFade() > 0.0f)*/; // T1-TODO

		if (weaponIkDesired != weaponIkActive)
		{
			if (weaponIkDesired)
			{
				pGripModifier->Enable();
				pGripModifier->FadeIn();
			}
			else
			{
				pGripModifier->FadeOut();
			}
		}
	}

	if (DC::AnimNpcInfo* pNpcInfo = pAnimControl->Info<DC::AnimNpcInfo>())
	{
		static const float kWeaponUpBlendSpringK   = 30.0f;
		static const float kWeaponDownBlendSpringK = 10.0f;
		const float weaponUpDownBlendTarget		   = m_weaponUpRequested ? 1.0f : 0.0f;
		const float springConst		  = m_weaponUpRequested ? kWeaponUpBlendSpringK : kWeaponDownBlendSpringK;
		pNpcInfo->m_weaponUpDownBlend = m_weaponUpDownBlendSpring.Track(pNpcInfo->m_weaponUpDownBlend,
																		weaponUpDownBlendTarget,
																		dt,
																		springConst);
	}

	UpdateWeaponIdleAnim();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::UpdateStatus()
{
	PROFILE(AI, WeapCon_UpdateStatus);
	NavCharacter* pNavChar	  = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	m_grenadeTossAction.Update(pAnimControl);
	m_grenadeDrawAction.Update(pAnimControl);
	m_weaponTossAction.Update(pAnimControl);
	m_parWeaponLayerAction.Update(pAnimControl);
	m_addWeaponLayerAction.Update(pAnimControl);

	// draw
	if (m_grenadeDrawAction.IsValid() && !m_grenadeDrawAction.IsDone() && !m_grenadeDrawn)
	{
		if (const AnimStateInstance* pGrenadeDrawInstance = m_grenadeDrawAction.GetTransitionDestInstance(pAnimControl))
		{
			// if npc wants to know when the draw happens. ideally this would happen via
			// handling a triggered effect, but this hurts dev/iteration when the effs
			// are missing from some of the anims. so for now...
			const float grenadeDrawPhase = pGrenadeDrawInstance->GetPhase();
			const ArtItemAnim* pDrawAnim = pGrenadeDrawInstance->GetPhaseAnimArtItem().ToArtItem();

			bool foundEff = true;
			const float drawPhase = FindEffPhase(pDrawAnim, SID("draw-grenade"), 0.60f, &foundEff);

			if (grenadeDrawPhase >= drawPhase)
			{
				pNavChar->NotifyDrawHeldThrowable(!foundEff);
				m_grenadeDrawn = true;
			}
		}
	}

	// throw
	if (m_grenadeTossAction.IsValid() && !m_grenadeTossAction.IsDone() && !m_grenadeThrown)
	{
		if (const AnimStateInstance* pGrenadeThrowInstance = m_grenadeTossAction.GetTransitionDestInstance(pAnimControl))
		{
			// if npc wants to know when the toss happens. ideally this would happen via
			// handling a triggered effect, but this hurts dev/iteration when the effs
			// are missing from some of the anims. so for now...
			const float grenadeTossPhase = pGrenadeThrowInstance->GetPhase();
			const ArtItemAnim* pThrowAnim = pGrenadeThrowInstance->GetPhaseAnimArtItem().ToArtItem();

			bool foundEff = true;
			const float releasePhase = FindEffPhase(pThrowAnim, SID("throw-grenade"), 0.47f, &foundEff);

			if (grenadeTossPhase >= releasePhase)
			{
				pNavChar->NotifyReleaseHeldThrowable(!foundEff);
				m_grenadeThrown = true;
			}
		}
	}

	m_processWeaponTossAction.Update(m_weaponTossAction, pAnimControl, *pNavChar);

	if (m_weaponUpRequested)
	{
		if (Npc* pNpc = Npc::FromProcess(pNavChar))
		{
			if (pNpc->GetHorse())
			{
				IGestureController* pGestureController = pNpc->GetGestureController();
				const bool isAimPlaying = pGestureController->IsPlayingOnAnyLayer(SID("npc-horse-aim-gesture"));
				const F32 upDownPhase =  pGestureController->GetGesturePhase(SID("npc-horse-aim-down^up-gesture"), Gesture::kGestureLayerInvalid);

				if (!isAimPlaying
					&& (upDownPhase < 0.0f || upDownPhase > 0.7f))
				{
					HandleHorseAimGesture(SID("npc-horse-aim-gesture"));
				}
			}
		}
	}

	// MsgCon("%f (%f) %s\n", m_weaponUpFactor, m_weaponUpTargetFactor, m_wepaonUpDownDirty ? "Dirty" : "");
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::IsBusy() const
{
	const bool animActionNotDone = !m_grenadeTossAction.IsDone() || !m_weaponTossAction.IsDone()
								   || !m_parWeaponLayerAction.IsDone() || !m_grenadeDrawAction.IsDone();

	if (animActionNotDone)
		return true;

	if (IsReloading())
		return true;

	const NdGameObject* pGo = GetCharacterGameObject();
	if (!pGo)
		return false;

	const IGestureController* pGestureController = pGo->GetGestureController();
	if (m_gunStateChangeGestureHandle.Playing(pGestureController))
		return true;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::ShouldInterruptNavigation() const
{
	return !m_grenadeTossAction.IsDone() || !m_grenadeDrawAction.IsDone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::Fire()
{
	Npc* pNpc = (Npc*)GetCharacter();

	const ProcessWeapon* pWeapon = ProcessWeapon::FromProcess(pNpc->GetWeapon());
	if (!pWeapon)
		return;

	if (!TryPlayGesture(SID("fire-recoil-gesture"), &m_fireRecoilGestureHandle))
	{
		AnimControl* pAnimControl	  = pNpc->GetAnimControl();
		AnimSimpleLayer* pRecoilLayer = pAnimControl->GetSimpleLayerById(SID("weapon-additive"));

		if (!pRecoilLayer)
			return;

		const StringId64 weaponDefId = pWeapon->GetWeaponDefId();
		ArtItemAnimHandle recoilAnim = pAnimControl->LookupAnim(StringId64Concat(weaponDefId, "-fire-recoil"));

		if (nullptr == recoilAnim.ToArtItem())
		{
			recoilAnim = pAnimControl->LookupAnim(SID("fire-recoil"));
		}

		if (nullptr == recoilAnim.ToArtItem())
			return;

		const ArtItemAnim* pAnim = recoilAnim.ToArtItem();

		pRecoilLayer->Fade(1.0f, 0.0f);
		const float fadeTime = Min(0.3f, GetDuration(pAnim) * 0.25f);
#if 0
		AnimSimpleLayer::FadeRequestParams params;
		params.m_layerFadeOutParams.m_enabled  = true;
		params.m_layerFadeOutParams.m_fadeTime = fadeTime;

		pRecoilLayer->RequestFadeToAnim(recoilAnim, params);
#else
		m_addWeaponLayerAction.FadeToAnim(pAnimControl,
										  pAnim->GetNameId(),
										  0.0f,
										  fadeTime,
										  DC::kAnimCurveTypeUniformS,
										  false,
										  false,
										  AnimAction::kFinishOnAnimEndEarly);

#endif
	}

	if (pWeapon->GetFirearmArtDef()->m_rechamber)
	{
		const IAiShootingLogic* pShootingLogic = pNpc->GetShootingLogic();
		const bool haveToReload = pShootingLogic && pShootingLogic->HaveToReload();

		if (!haveToReload)
		{
			m_rechamberRequested = true;
			//Rechamber();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::ConfigureCharacter(Demeanor demeanor,
											const DC::NpcDemeanorDef* pDemeanorDef,
											const NdAiAnimationConfig* pAnimConfig)
{
	NavCharacter* pNavChar = GetCharacter();

	UpdateOverlays();

	m_reloadPerformance.RemoveTable(pNavChar, SID("reload"));

	if (pDemeanorDef && pDemeanorDef->m_reloadMovePerfs != INVALID_STRING_ID_64)
	{
		m_reloadPerformance.AddTable(pNavChar, SID("reload"), pDemeanorDef->m_reloadMovePerfs);
	}

	RefreshWeaponAnimSet();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::TryMovingReload()
{
	NavCharacter* pNavChar = GetCharacter();

	AnimationControllers* pAnimControllers		   = pNavChar ? pNavChar->GetAnimationControllers() : nullptr;
	IAiLocomotionController* pLocomotionController = pAnimControllers ? pAnimControllers->GetLocomotionController() : nullptr;

	if (!pLocomotionController)
		return false;

	MovePerformance::FindParams params;
	if (!pLocomotionController->MovePerformanceAllowed(&params))
		return false;

	MovePerformance::Performance perf;
	if (!m_reloadPerformance.FindRandomPerformance(pNavChar, params, &perf))
		return false;

	const bool success = pLocomotionController->StartMovePerformance(perf);

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::HandleHorseAimGesture(StringId64 desiredGesture)
{
	Npc* pNpc = Npc::FromProcess(GetCharacter());
	const Horse* pHorse = pNpc ? pNpc->GetHorse() : nullptr;

	if (!pHorse)
		return false;

	if (pNpc->ShouldUseHorseAimStateMachine())
		return false;

	IGestureController* pGestureController = pNpc->GetGestureController();
	AI_ASSERT(pGestureController);

	const AiEntity* pTarget = pNpc->GetCurrentTargetEntity();
	const NdGameObject* pGo = pTarget ? pTarget->ToGameObject() : nullptr;

	Gesture::PlayArgs args;
	args.m_gestureLayer = pGestureController->GetFirstRegularGestureLayerIndex() + 1;
	args.SetPriority(DC::kGesturePriorityRideHorse);
	args.m_blendInTime = 0.3f;

	Gesture::Err result = Gesture::Err();

	if (pGo)
	{
		result = pGestureController->PlayGesture(desiredGesture, *pGo, &args);
	}
	else
	{
		result = pGestureController->Play(desiredGesture, args);
	}

	return result.Success();
}

/// --------------------------------------------------------------------------------------------------------------- ///
HorseAimStateMachine* AiWeaponController::GetHorseAimStateMachine() const
{
	Npc* pNpc = Npc::FromProcess(GetCharacter());
	if (!pNpc)
		return nullptr;

	IAiRideHorseController* pRideHorse = pNpc->GetAnimationControllers()->GetRideHorseController();
	return pRideHorse ? pRideHorse->GetAimStateMachine() : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::TryPlayGesture(StringId64 gestureId,
										GestureHandle* pGestureHandleOut,
										const DC::BlendParams* pBlend /* = nullptr */)
{
	if (pGestureHandleOut)
		*pGestureHandleOut = GestureHandle();

	NdGameObject* pChar = GetCharacterGameObject();
	AnimControl* pAnimControl = pChar ? pChar->GetAnimControl() : nullptr;
	IGestureController* pGestureController = pChar ? pChar->GetGestureController() : nullptr;
	if (!pGestureController || !pAnimControl)
	{
		return false;
	}

	Gesture::CachedGestureRemap gestureRemap;
	gestureRemap.SetSourceId(gestureId);
	gestureRemap = Gesture::RemapGesture(gestureRemap, pAnimControl);

	if (!Gesture::LookupGesture(gestureRemap.m_finalGestureId))
	{
		return false;
	}

	Gesture::PlayArgs args;
	args.SetPriority(DC::kGesturePriorityNpcReload);

	if (pBlend)
	{
		args.m_blendInTime = pBlend->m_animFadeTime;
		args.m_blendInCurve = pBlend->m_curve;

		args.m_blendOutTime = pBlend->m_animFadeTime;
		args.m_blendOutCurve = pBlend->m_curve;
	}

	ANIM_ASSERT(pGestureHandleOut);

	args.m_pOutGestureHandle = pGestureHandleOut;
	pGestureController->Play(gestureId, args);

	WeaponLog("Playing weapon gesture %s (req: %s)\n",
			  DevKitOnly_StringIdToString(args.m_pOutGestureHandle->GetCurrentGestureId(pGestureController)),
			  DevKitOnly_StringIdToString(gestureId));

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::TryPlayReloadGesture()
{
	// If there is a gesture called "reload-gesture", use that instead.
	return TryPlayGesture(SID("reload-gesture"), &m_reloadGestureHandle);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::TryPlayRechamberGesture()
{
	// If there is a gesture called "reload-gesture", use that instead.
	return TryPlayGesture(SID("rechamber-gesture"), &m_rechamberGestureHandle);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::TryPlayUnholsterGesture(bool slow /* = false */, const DC::BlendParams* pBlend /* = nullptr */)
{
	HorseAimStateMachine* pHorseAim = GetHorseAimStateMachine();
	if (pHorseAim)
	{
		pHorseAim->SetEnabled(true, FILE_LINE_FUNC);
	}

	const StringId64 gestureId = slow ? SID("unholster-gesture-slow") : SID("unholster-gesture");
	return TryPlayGesture(gestureId, &m_gunStateChangeGestureHandle, pBlend);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::TryPlayHolsterGesture(bool slow /* = false */, const DC::BlendParams* pBlend /* = nullptr */)
{
	HorseAimStateMachine* pHorseAim = GetHorseAimStateMachine();
	if (pHorseAim)
	{
		pHorseAim->SetEnabled(false, FILE_LINE_FUNC);
	}

	const StringId64 gestureId = slow ? SID("holster-gesture-slow") : SID("holster-gesture");
	return TryPlayGesture(gestureId, &m_gunStateChangeGestureHandle, pBlend);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::TryPlayWeaponDownGesture()
{
	HorseAimStateMachine* pHorseAim = GetHorseAimStateMachine();
	if (pHorseAim)
	{
		pHorseAim->OnWeaponDown();
	}

	if (HandleHorseAimGesture(SID("npc-horse-aim-up^down-gesture")))
		return true;

	const StringId64 gestureId = SID("weapon-down-gesture");
	return TryPlayGesture(gestureId, &m_weaponUpDownGestureHandle);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::TryPlayWeaponUpGesture()
{
	if (HandleHorseAimGesture(SID("npc-horse-aim-down^up-gesture")))
		return true;

	const StringId64 gestureId = SID("weapon-up-gesture");
	return TryPlayGesture(gestureId, &m_weaponUpDownGestureHandle);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::TryPlayWeaponSwitchGesture(StringId64 newWeaponId)
{
	const StringId64 gestureId = StringId64Concat(newWeaponId, "-weapon-switch-gesture");
	return TryPlayGesture(gestureId, &m_gunStateChangeGestureHandle);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::PlayReloadAnim(AnimControl* pAnimControl, const StringId64 weaponDefId)
{
	if (TryPlayReloadGesture())
	{
		return;
	}

	// Reload partial
	StringId64 reloadPartialAnimId		  = StringId64Concat(weaponDefId, "-reload-partial");
	const ArtItemAnim* pReloadPartialAnim = pAnimControl->LookupAnim(reloadPartialAnimId).ToArtItem();

	if (!pReloadPartialAnim)
	{
		reloadPartialAnimId = SID("reload-partial");
		pReloadPartialAnim	= pAnimControl->LookupAnim(reloadPartialAnimId).ToArtItem();
	}

	if (pReloadPartialAnim)
	{
		AnimSimpleLayer* pPartialWeaponLayer = pAnimControl->GetSimpleLayerById(SID("weapon-partial"));
		pPartialWeaponLayer->Fade(1.0f, 0.3f);
		m_parWeaponLayerAction.FadeToAnim(pAnimControl,
										  reloadPartialAnimId,
										  0.0f,
										  0.1f,
										  DC::kAnimCurveTypeUniformS,
										  false,
										  false,
										  AnimAction::kFinishOnAnimEndEarly);
	}

	// Reload additive
	StringId64 reloadAdditiveAnimId		   = StringId64Concat(weaponDefId, "-reload-additive");
	const ArtItemAnim* pReloadAdditiveAnim = pAnimControl->LookupAnim(reloadAdditiveAnimId).ToArtItem();

	if (!pReloadAdditiveAnim)
	{
		reloadAdditiveAnimId = SID("reload-additive");
		pReloadAdditiveAnim	 = pAnimControl->LookupAnim(reloadAdditiveAnimId).ToArtItem();
	}

	if (pReloadAdditiveAnim)
	{
		AnimSimpleLayer* pAdditiveWeaponLayer = pAnimControl->GetSimpleLayerById(SID("weapon-additive"));
		pAdditiveWeaponLayer->Fade(1.0f, 0.3f);
		m_addWeaponLayerAction.FadeToAnim(pAnimControl,
										  reloadAdditiveAnimId,
										  0.0f,
										  0.1f,
										  DC::kAnimCurveTypeUniformS,
										  false,
										  false,
										  AnimAction::kFinishOnAnimEndEarly);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::Rechamber()
{
	m_rechamberRequested = false;

	if (IsBusy())
	{
		return;
	}

	if (TryPlayRechamberGesture())
	{
		Npc* pNpc = Npc::FromProcess(GetCharacter());
		AnimControl* pAnimControl = pNpc->GetAnimControl();

		ProcessWeapon* pWeapon = ProcessWeapon::FromProcess(pNpc->GetWeapon());
		if (!pWeapon)
			return;

		// Weapon anim - This is expected to be remapped by an overlay on the npc anim control (not on the weapon
		//				 anim control), so look up the anim on the npc anim control before playing it on the weapon.
		SsAnimateParams params;
		const StringId64 weaponDefId = pWeapon->GetWeaponDefId();
		const StringId64 baseWeaponRechamberAnimId = StringId64Concat(weaponDefId, "-rechamber");
		const StringId64 weaponRechamberAnimId = pAnimControl->LookupAnimId(baseWeaponRechamberAnimId);

		//pWeapon->SetNoIdleOnAnimEnd(false);

		if (!pWeapon->PlayReloadAnimation(weaponRechamberAnimId))
		{
			MsgAnim("[%s] Failed to find rechamber animation from '%s' / '%s'\n",
					pNpc->GetName(),
					DevKitOnly_StringIdToString(weaponRechamberAnimId),
					DevKitOnly_StringIdToString(baseWeaponRechamberAnimId));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::Reload()
{
	if (IsBusy())
	{
		return;
	}

	Npc* pNpc				  = Npc::FromProcess(GetCharacter());
	AnimControl* pAnimControl = pNpc->GetAnimControl();

	ProcessWeapon* pWeapon = ProcessWeapon::FromProcess(pNpc->GetWeapon());
	if (!pWeapon)
		return;

	if (TryMovingReload())
	{
		return;
	}

	const StringId64 weaponDefId = pWeapon->GetWeaponDefId();

	PlayReloadAnim(pAnimControl, weaponDefId);

	if (!m_reloadGestureHandle.Assigned())
	{
		// Weapon anim - This is expected to be remapped by an overlay on the npc anim control (not on the weapon
		//				 anim control), so look up the anim on the npc anim control before playing it on the weapon.

		SsAnimateParams params;
		const StringId64 baseWeaponReloadAnimId = StringId64Concat(weaponDefId, "-reload");
		const StringId64 weaponReloadAnimId = pAnimControl->LookupAnimId(baseWeaponReloadAnimId);

		//pWeapon->SetNoIdleOnAnimEnd(false);

		if (!pWeapon->PlayReloadAnimation(weaponReloadAnimId))
		{
			MsgAnim("[%s] Failed to find reload animation from '%s' / '%s'\n",
					pNpc->GetName(),
					DevKitOnly_StringIdToString(weaponReloadAnimId),
					DevKitOnly_StringIdToString(baseWeaponReloadAnimId));
		}
	}

	m_reloadRequested = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::IsReloading() const
{
	if (const IGestureController* pGestureController = GetCharacter()->GetGestureController())
	{
		if (m_reloadGestureHandle.Playing(pGestureController))
		{
			return true;
		}

		if (m_rechamberGestureHandle.Playing(pGestureController))
		{
			return true;
		}
	}

	// NB: m_reloading is true when weapon-partial layer is busy: i.e. When reloading or holstering/un-holstering
	return m_reloadRequested || m_reloading;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::IsRecoiling() const
{
	Npc* pNpc = Npc::FromProcess(GetCharacter());
	if (!pNpc)
		return false;

	if (m_fireRecoilGestureHandle.Playing(pNpc->GetGestureController()))
		return true;

	ProcessWeaponBase* pWeapon = pNpc->GetWeapon();
	if (!pWeapon)
		return false;

	AnimControl* pAnimControl = pNpc->GetAnimControl();
	AnimSimpleLayer* pRecoilLayer = pAnimControl->GetSimpleLayerById(SID("weapon-additive"));

	if (!pRecoilLayer)
		return false;

	const StringId64 weaponDefId = pWeapon->GetWeaponDefId();
	ArtItemAnimHandle recoilAnim = pAnimControl->LookupAnim(StringId64Concat(weaponDefId, "-fire-recoil"));

	if (nullptr == recoilAnim.ToArtItem())
	{
		recoilAnim = pAnimControl->LookupAnim(SID("fire-recoil"));
	}

	if (nullptr == recoilAnim.ToArtItem())
		return false;

	const AnimSimpleInstance* pInstance = pRecoilLayer->CurrentInstance();

	if (!pInstance)
		return false;

	return pInstance->GetAnim() == recoilAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::AbortReload()
{
	NavCharacter* pNavChar	  = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	WeaponLogStr("AbortReload\n");

	if (AnimSimpleLayer* pParLayer = pAnimControl->GetSimpleLayerById(SID("weapon-partial")))
	{
		if (AnimSimpleInstance* pCurInst = pParLayer->CurrentInstance())
		{
			pCurInst->SetFrozen(true);
		}

		pParLayer->Fade(0.0f, 0.1f);
		m_parWeaponLayerAction.Reset();
	}

	if (ProcessWeaponBase* pWeapon = pNavChar->GetWeapon())
	{
		PostEventFrom(Process::GetContextProcess(), SID("reload-aborted"), pWeapon);
	}

	if (IGestureController* pGestureController = pNavChar->GetGestureController())
	{
		m_reloadGestureHandle.Clear(pGestureController);
		m_rechamberGestureHandle.Clear(pGestureController);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::AbortAnims(bool immediate)
{
	NavCharacter* pNavChar	  = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	WeaponLog("AbortAnims%s\n", immediate ? " (immediate)" : "");

	const float fadeTime = immediate ? 0.0f : 0.1f;

	if (AnimSimpleLayer* pParLayer = pAnimControl->GetSimpleLayerById(SID("weapon-partial")))
	{
		if (AnimSimpleInstance* pCurInst = pParLayer->CurrentInstance())
		{
			pCurInst->SetFrozen(true);
		}

		pParLayer->Fade(0.0f, fadeTime);
		m_parWeaponLayerAction.Reset();
	}

	if (AnimSimpleLayer* pAddLayer = pAnimControl->GetSimpleLayerById(SID("weapon-additive")))
	{
		if (AnimSimpleInstance* pCurInst = pAddLayer->CurrentInstance())
		{
			pCurInst->SetFrozen(true);
		}

		pAddLayer->Fade(0.0f, fadeTime);
		m_addWeaponLayerAction.Reset();
	}

	if (IGestureController* pGestureController = pNavChar->GetGestureController())
	{
		Gesture::PlayArgs clearArgs = Gesture::g_defaultPlayArgs;
		clearArgs.m_freeze = true;

		if (immediate)
		{
			clearArgs.m_blendInTime	 = 0.0f;
			clearArgs.m_blendOutTime = 0.0f;
		}

		m_gunStateChangeGestureHandle.Clear(pGestureController, clearArgs);
		m_weaponUpDownGestureHandle.Clear(pGestureController, clearArgs);
		m_reloadGestureHandle.Clear(pGestureController, clearArgs);
		m_rechamberGestureHandle.Clear(pGestureController, clearArgs);
		m_fireRecoilGestureHandle.Clear(pGestureController, clearArgs);
	}

	SetAnimRequestedGunState(kGunStateMax, "AbortAnims");

	if (m_requestedPrimaryWeaponId)
	{
		WeaponLog("AbortAnims - aborting primary weapon switch to '%s'\n",
				  DevKitOnly_StringIdToString(m_requestedPrimaryWeaponId));
	}

	if (m_animRequestedWeaponId)
	{
		WeaponLog("AbortAnims - aborting anim requested weapon switch to '%s'\n",
				  DevKitOnly_StringIdToString(m_animRequestedWeaponId));
	}

	m_requestedPrimaryWeaponId	= INVALID_STRING_ID_64;
	m_requestedPrimaryWeaponUid = INVALID_PROPINV_ID;
	m_animRequestedWeaponId		= INVALID_STRING_ID_64;
	m_animRequestedWeaponUid	= INVALID_PROPINV_ID;
	m_savedGunState			= kGunStateMax;
	m_unholsteringForSwitch = false;
	m_reloading		  = false;
	m_reloadRequested = false;
	m_rechamberRequested = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::Abort(bool immediate)
{
	NavCharacter* pNavChar = GetCharacter();
	const bool wasSwitching = m_unholsteringForSwitch;

	AbortAnims(immediate);

	if (wasSwitching && pNavChar)
	{
		pNavChar->ConfigureCharacter();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::SetWeaponIkDesired(bool desired)
{
	m_weaponIkDesired = desired;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::RequestGunState(GunState gs,
										 bool slow /* = false */,
										 const DC::BlendParams* pBlend /* = nullptr */)
{
	if (m_changeSuppressionSec > 0.0f)
	{
		if (gs != m_suppressedRequest.m_gs)
		{
			WeaponLog("RequestGunState(%s) Suppressed!\n", GetGunStateName(gs));
			m_suppressedRequest.m_gs = gs;
			m_suppressedRequest.m_slow = slow;
			if (pBlend)
			{
				m_suppressedRequest.m_blend = *pBlend;
				m_suppressedRequest.m_blendValid = true;
			}
			else
			{
				m_suppressedRequest.m_blendValid = false;
			}
		}

		return;
	}

	if (m_requestedPrimaryWeaponId != INVALID_STRING_ID_64)
	{
		WeaponLog("Ignoring RequestGunState(%s) because we're switching to primary weapon '%s'\n",
				  GetGunStateName(gs),
				  DevKitOnly_StringIdToString(m_requestedPrimaryWeaponId));

		return;
	}

	if ((gs == m_animRequestedGunState) || (gs == m_gunState))
	{
		return;
	}

	ProcessWeaponBase* pWeapon = GetCharacter()->GetWeapon();

	if (nullptr == pWeapon)
	{
		WeaponLog("RequestGunState(%s) Forcing holstered because our weapon is null!\n", GetGunStateName(gs));

		m_gunState = kGunStateHolstered;
		return;
	}

	WeaponLog("RequestGunState(%s)%s\n", GetGunStateName(gs), slow ? " slow" : "");

	switch (gs)
	{
	case kGunStateOut:
		if (m_allowGunOut)
		{
			if (!PlayUnholsterAnimation(slow, pBlend))
			{
				WeaponLogStr("Failed to play unholster animation, forcing gun state out instead\n");
				ForceGunState(gs);
			}
		}
		else
		{
			WeaponLog("RequestGunState(%s) Ignoring gun out request because m_allowGunOut is false\n",
					  GetGunStateName(gs));
		}
		break;

	case kGunStateHolstered:
		{
			// source search terms:
			// weapon torch, weapon is torch, holster torch, don't holster if torch, don't holster if weapon is torch,
			// if we have a torch, if NPC has torch, if NPC has a torch, weapon is a torch
			const bool allowHolster = pWeapon && pWeapon->GetWeaponDefId() != SID("torch");

			if (allowHolster)
			{
				if (!PlayHolsterAnimation(slow, pBlend))
				{
					WeaponLogStr("Failed to play holster animation, forcing gun state holstered instead\n");
					ForceGunState(gs);
				}
			}
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::PlayUnholsterAnimation(bool slow /* = false */, const DC::BlendParams* pBlend /* = nullptr */)
{
	NavCharacter* pNavChar				  = GetCharacter();
	AnimControl* pAnimControl			  = pNavChar->GetAnimControl();
	AnimSimpleLayer* pPartialWeaponLayer  = pAnimControl->GetSimpleLayerById(SID("weapon-partial"));
	AnimSimpleLayer* pAdditiveWeaponLayer = pAnimControl->GetSimpleLayerById(SID("weapon-additive"));

	m_effSuppressedGunOut = false;

	if (TryPlayUnholsterGesture(slow, pBlend))
	{
		SetAnimRequestedGunState(kGunStateOut, "PlayUnholsterAnimation-Gesture");
		return true;
	}

	CachedAnimLookup& parAnim = slow ? m_slowUnholsterParAnim : m_unholsterParAnim;
	CachedAnimLookup& addAnim = slow ? m_slowUnholsterAddAnim : m_unholsterAddAnim;

	parAnim = pAnimControl->LookupAnimCached(parAnim);
	addAnim = pAnimControl->LookupAnimCached(addAnim);

	const ArtItemAnim* pDesAnim = parAnim.GetAnim().ToArtItem();

	if (!pDesAnim && slow)
	{
		parAnim = m_unholsterParAnim;
		addAnim = m_unholsterAddAnim;

		parAnim = pAnimControl->LookupAnimCached(parAnim);
		addAnim = pAnimControl->LookupAnimCached(addAnim);

		pDesAnim = parAnim.GetAnim().ToArtItem();
	}

	if (!pDesAnim)
	{
		return false;
	}

	const AnimSimpleInstance* pCurParInst = pPartialWeaponLayer->GetNumInstances() > 0
												? pPartialWeaponLayer->GetInstance(0)
												: nullptr;
	const ArtItemAnim* pCurAnim = pCurParInst ? pCurParInst->GetAnim().ToArtItem() : nullptr;

	if (pDesAnim && (pDesAnim != pCurAnim))
	{
		float blendTime = 0.3f;
		DC::AnimCurveType blendCurve = DC::kAnimCurveTypeUniformS;

		if (pBlend && pBlend->m_animFadeTime >= 0.0f)
		{
			blendTime = pBlend->m_animFadeTime;
		}

		if (pBlend && (pBlend->m_curve != DC::kAnimCurveTypeInvalid))
		{
			blendCurve = pBlend->m_curve;
		}

		WeaponLog("Playing unholster animation '%s' (blend time: %0.3fs, curve: %s)\n",
				  pDesAnim->GetName(),
				  blendTime,
				  DC::GetAnimCurveTypeName(blendCurve));

		m_parWeaponLayerAction.FadeToAnim(pAnimControl,
										  pDesAnim->GetNameId(),
										  0.0f,
										  blendTime,
										  blendCurve,
										  false,
										  false,
										  AnimAction::kFinishOnAnimEndEarly);
		pPartialWeaponLayer->Fade(1.0f, 0.2f);

		m_addWeaponLayerAction.FadeToAnim(pAnimControl,
										  addAnim.GetFinalResolvedId(),
										  0.0f,
										  blendTime,
										  blendCurve,
										  false,
										  false,
										  AnimAction::kFinishOnAnimEndEarly);
		pAdditiveWeaponLayer->Fade(1.0f, 0.2f);
	}

	SetAnimRequestedGunState(kGunStateOut, "PlayUnholsterAnimation");

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::PlayHolsterAnimation(bool slow /* = false */, const DC::BlendParams* pBlend /* = nullptr */)
{
	NavCharacter* pNavChar				  = GetCharacter();
	AnimControl* pAnimControl			  = pNavChar->GetAnimControl();
	AnimSimpleLayer* pPartialWeaponLayer  = pAnimControl->GetSimpleLayerById(SID("weapon-partial"));
	AnimSimpleLayer* pAdditiveWeaponLayer = pAnimControl->GetSimpleLayerById(SID("weapon-additive"));

	m_effSuppressedGunOut = false;

	if (TryPlayHolsterGesture(slow, pBlend))
	{
		SetAnimRequestedGunState(kGunStateHolstered, "PlayHolsterAnimation-Gesture");
		return true;
	}

	CachedAnimLookup& parAnim = slow ? m_slowHolsterParAnim : m_holsterParAnim;
	CachedAnimLookup& addAnim = slow ? m_slowHolsterAddAnim : m_holsterAddAnim;

	parAnim = pAnimControl->LookupAnimCached(parAnim);
	addAnim = pAnimControl->LookupAnimCached(addAnim);

	const ArtItemAnim* pDesAnim = parAnim.GetAnim().ToArtItem();

	if (!pDesAnim && slow)
	{
		parAnim = m_holsterParAnim;
		addAnim = m_holsterAddAnim;

		parAnim = pAnimControl->LookupAnimCached(parAnim);
		addAnim = pAnimControl->LookupAnimCached(addAnim);

		pDesAnim = parAnim.GetAnim().ToArtItem();
	}

	if (!pDesAnim)
	{
		return false;
	}

	const AnimSimpleInstance* pCurParInst = pPartialWeaponLayer->GetNumInstances() > 0
												? pPartialWeaponLayer->GetInstance(0)
												: nullptr;
	const ArtItemAnim* pCurAnim = pCurParInst ? pCurParInst->GetAnim().ToArtItem() : nullptr;

	if (pDesAnim && (pDesAnim != pCurAnim))
	{
		float blendTime = 0.3f;
		DC::AnimCurveType blendCurve = DC::kAnimCurveTypeUniformS;

		if (pBlend && pBlend->m_animFadeTime >= 0.0f)
		{
			blendTime = pBlend->m_animFadeTime;
		}

		if (pBlend && (pBlend->m_curve != DC::kAnimCurveTypeInvalid))
		{
			blendCurve = pBlend->m_curve;
		}

		WeaponLog("Playing holster animation '%s' (blend time: %0.3fs, curve: %s)\n",
				  pDesAnim->GetName(),
				  blendTime,
				  DC::GetAnimCurveTypeName(blendCurve));

		m_parWeaponLayerAction.FadeToAnim(pAnimControl,
										  pDesAnim->GetNameId(),
										  0.0f,
										  blendTime,
										  blendCurve,
										  false,
										  false,
										  AnimAction::kFinishOnAnimEndEarly);

		pPartialWeaponLayer->Fade(1.0f, 0.2f);

		m_addWeaponLayerAction.FadeToAnim(pAnimControl,
										  addAnim.GetFinalResolvedId(),
										  0.0f,
										  blendTime,
										  blendCurve,
										  false,
										  false,
										  AnimAction::kFinishOnAnimEndEarly);

		pAdditiveWeaponLayer->Fade(1.0f, 0.2f);
	}

	SetAnimRequestedGunState(kGunStateHolstered, "PlayHolsterAnimation");

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::PlayWeaponSwitchAnimation(StringId64 newWeaponId)
{
	m_gunStateChangeGestureHandle = GestureHandle();

	NavCharacter* pNavChar	  = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;

	if (!pAnimControl)
	{
		return false;
	}

	const DC::WeaponArtDef* pWeaponArtDef = ProcessWeaponBase::GetWeaponArtDef(newWeaponId);

	const char* artDefName = pWeaponArtDef ? pWeaponArtDef->m_animOverlayWeaponIdName.GetString() : nullptr;

	StringId64 resolvedWeaponId = newWeaponId;
	if (artDefName && artDefName[0] != 0)
	{
		resolvedWeaponId = StringToStringId64(artDefName);
	}

	if (TryPlayWeaponSwitchGesture(resolvedWeaponId))
	{
		SetAnimRequestedGunState(kGunStateOut, "PlayWeaponSwitchAnimation-Gesture");
		return true;
	}

	AnimSimpleLayer* pPartialWeaponLayer  = pAnimControl->GetSimpleLayerById(SID("weapon-partial"));
	AnimSimpleLayer* pAdditiveWeaponLayer = pAnimControl->GetSimpleLayerById(SID("weapon-additive"));

	if (!pPartialWeaponLayer && !pAdditiveWeaponLayer)
	{
		return false;
	}

	const StringId64 addAnimId = StringId64Concat(resolvedWeaponId, "-weapon-switch-add");
	const StringId64 parAnimId = StringId64Concat(resolvedWeaponId, "-weapon-switch-par");

	const ArtItemAnim* pAddAnim = pAnimControl->LookupAnim(addAnimId).ToArtItem();
	const ArtItemAnim* pParAnim = pAnimControl->LookupAnim(parAnimId).ToArtItem();

	if (!pAddAnim && !pParAnim)
	{
		WeaponLog("Failed to find weapon switch animation '%s' or '%s' for weapon '%s', falling back to regular holster/unholster\n",
				  DevKitOnly_StringIdToString(addAnimId),
				  DevKitOnly_StringIdToString(parAnimId),
				  DevKitOnly_StringIdToString(newWeaponId));
		return false;
	}

	m_effSuppressedGunOut = false;

	if (pPartialWeaponLayer)
	{
		if (pParAnim)
		{
			m_parWeaponLayerAction.FadeToAnim(pAnimControl,
											  pParAnim->GetNameId(),
											  0.0f,
											  0.3f,
											  DC::kAnimCurveTypeUniformS,
											  false,
											  false,
											  AnimAction::kFinishOnAnimEndEarly);

			pPartialWeaponLayer->Fade(1.0f, 0.2f);
		}
		else
		{
			pPartialWeaponLayer->Fade(0.0f, 0.2f);
		}
	}

	if (pAdditiveWeaponLayer)
	{
		if (pAddAnim)
		{
			m_addWeaponLayerAction.FadeToAnim(pAnimControl,
											  pAddAnim->GetNameId(),
											  0.0f,
											  0.3f,
											  DC::kAnimCurveTypeUniformS,
											  false,
											  false,
											  AnimAction::kFinishOnAnimEndEarly);

			pAdditiveWeaponLayer->Fade(1.0f, 0.2f);
		}
		else
		{
			pAdditiveWeaponLayer->Fade(0.0f, 0.2f);
		}
	}

	WeaponLog("Playing weapon switch animation (add '%s') (par '%s') for new weapon '%s'\n",
			  pAddAnim ? pAddAnim->GetName() : "<none>",
			  pParAnim ? pParAnim->GetName() : "<none>",
			  DevKitOnly_StringIdToString(newWeaponId));

	SetAnimRequestedGunState(kGunStateOut, "PlayWeaponSwitchAnimation");

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::EnableGunOutSuppression()
{
	if (m_gunState != kGunStateOut)
	{
		return false;
	}

	if (m_effSuppressedGunOut)
	{
		return true;
	}

	WeaponLogStr("EnableGunOutSuppression\n");

	if (!PlayHolsterAnimation(false))
	{
		ForceGunState(kGunStateHolstered);
	}

	m_effSuppressedGunOut = true;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::DisableGunOutSuppression()
{
	if (!m_effSuppressedGunOut)
	{
		return false;
	}

	if (m_allowGunOut && GetRequestedGunState() == kGunStateOut)
	{
		WeaponLogStr("DisableGunOutSuppression\n");

		if (!PlayUnholsterAnimation())
		{
			ForceGunState(kGunStateOut);
		}
	}

	m_effSuppressedGunOut = false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::IsGunOutSuppressed() const
{
	return m_effSuppressedGunOut || m_changeSuppressionSec > 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::AllowGunOut(bool allow)
{
	m_allowGunOut = allow;
}

/// --------------------------------------------------------------------------------------------------------------- ///
GunState AiWeaponController::GetRequestedGunState() const
{
	return (m_animRequestedGunState == kGunStateMax) ? m_gunState : m_animRequestedGunState;
}

/// --------------------------------------------------------------------------------------------------------------- ///
GunState AiWeaponController::GetCurrentGunState() const
{
	if (!IsValid())
	{
		return kGunStateHolstered;
	}

	if (m_effSuppressedGunOut)
	{
		return kGunStateHolstered;
	}

	const NavCharacter* pNavChar	 = GetCharacter();
	const ProcessWeaponBase* pWeapon = pNavChar ? pNavChar->GetWeapon() : nullptr;
	if (pWeapon)
	{
		return m_gunState;
	}

	return kGunStateHolstered;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::IsPendingGunState(GunState gs) const
{
	switch (gs)
	{
	case kGunStateHolstered:
	case kGunStateOut:
		if (GetRequestedGunState() != gs)
			return false;

		if (GetCurrentGunState() == gs)
			return false;

		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::SetWeaponInHand(const ProcessWeaponBase* pWeapon, bool inHand)
{
	WeaponLog("SetWeaponInHand(%s)\n", inHand ? "true" : "false");

	NavCharacter* pNavChar = GetCharacter();
	JointModifiers* pJointModifiers	   = pNavChar->GetJointModifiers();
	IWeaponGripModifier* pGripModifier = pJointModifiers
											 ? pJointModifiers->GetModifier<IWeaponGripModifier>(kWeaponGripModifier)
											 : nullptr;

	if (pGripModifier)
	{
		if (!inHand)
		{
			if (pGripModifier->IsEnabled())
			{
				pGripModifier->Disable();
			}
		}
		else
		{
			if (!pGripModifier->IsEnabled() && m_weaponIkDesired)
			{
				pGripModifier->FadeIn();
				pGripModifier->Enable();
			}
		}
	}

	UpdateOverlays();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::ForceGunState(GunState gs,
									   bool overrideEqualCheck /* = false */,
									   bool abortAnims /* = false */,
									   float blendTime /* = 0.0f */,
									   bool immediate /* = false */)
{
	if (m_changeSuppressionSec > 0.0f)
	{
		WeaponLogStr("ForceGunState called mid-suppression, disabling suppression\n");
		m_changeSuppressionSec = -1.0f;
		m_suppressedRequest = ChangeParams();
		m_suppressedPrimaryId = INVALID_STRING_ID_64;
	}

	if (abortAnims)
	{
		if (gs != m_animRequestedGunState || overrideEqualCheck)
		{
			AbortAnims(true);
		}
	}

	NavCharacter* pNavChar = GetCharacter();
	ProcessWeaponBase* pWeapon = pNavChar ? pNavChar->GetWeapon() : nullptr;

	if ((gs == kGunStateOut) && !pWeapon)
	{
		gs = kGunStateHolstered;
	}

	if (gs != m_gunState || overrideEqualCheck)
	{
		WeaponLog("ForceGunState(%s)%s (%0.3fs blend)\n",
				  GetGunStateName(gs),
				  overrideEqualCheck ? " overridden" : "",
				  blendTime);

		m_gunState = gs;
		SetAnimRequestedGunState(kGunStateMax, "ForceGunState");

		if (pWeapon)
		{
			SetWeaponInHand(pWeapon, gs == kGunStateOut);
			EnforceGunState(pWeapon, gs, abortAnims || immediate, blendTime);
		}

		const bool skipConfigure = m_unholsteringForSwitch && (gs == kGunStateHolstered);

		if (!skipConfigure)
		{
			pNavChar->ConfigureCharacter();
		}
	}
	else if (!IsPrimaryWeaponUpToDate())
	{
		pNavChar->ConfigureCharacter();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::EnforceGunState(ProcessWeaponBase* pWeapon,
										 const GunState gs,
										 bool immediate /* = false */,
										 float blendTime /* = 0.0f */,
										 bool force /* = false */)
{
	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
		return;

	AttachSystem* pAs = pNavChar->GetAttachSystem();
	if (!pAs)
		return;

	const AttachIndex curIdx = pWeapon->GetParentAttachIndex();

	switch (gs)
	{
	case kGunStateOut:
		{
			const StringId64 handName = pNavChar->GetWeaponAttachId(pWeapon->GetWeaponAnimType());

			if (handName != INVALID_STRING_ID_64)
			{
				AttachIndex handIdx = AttachIndex::kInvalid;
				pAs->FindPointIndexById(&handIdx, handName);

				if (FALSE_IN_FINAL_BUILD(handIdx == AttachIndex::kInvalid))
				{
					WeaponLog("EnforceGunState - Failed; invalid hand joint (%s)\n",
							  DevKitOnly_StringIdToStringOrHex(handName));
				}

				if ((curIdx != handIdx && handIdx != AttachIndex::kInvalid) || force)
				{
					WeaponLog("EnforceGunState - Changing weapon '%s' to gunout attach point '%s' (blend: %0.3fsec)\n",
							  DevKitOnly_StringIdToString(pWeapon->GetWeaponDefId()),
							  DevKitOnly_StringIdToString(handName),
							  blendTime);

					if (blendTime > NDI_FLT_EPSILON)
					{
						pWeapon->ChangeAttachPointBlend(FILE_LINE_FUNC, handName, kIdentity, blendTime);
					}
					else
					{
						pWeapon->ChangeAttachPoint(FILE_LINE_FUNC, handName);
					}

					SendEvent(SID("weapon-grabbed"), pWeapon, BoxedValue(immediate && blendTime <= 0.0f));
					if (pWeapon != nullptr && pWeapon->IsBow())
					{
						SendEvent(SID("attach-arrow"), pWeapon);
					}
				}
			}
			else
			{
				WeaponLog("EnforceGunState - Weapon '%s' has no valid gunout attach point!\n",
						  DevKitOnly_StringIdToString(pWeapon->GetWeaponDefId()));
			}
		}
		break;
	case kGunStateHolstered:
		{
			const DC::NpcWeaponHolster holster = GetWeaponHolsterAttachPoint(pWeapon->GetWeaponArtDef());

			if (holster.m_name != INVALID_STRING_ID_64)
			{
				AttachIndex holsterIdx = AttachIndex::kInvalid;
				pAs->FindPointIndexById(&holsterIdx, holster.m_name);

				if (FALSE_IN_FINAL_BUILD(holsterIdx == AttachIndex::kInvalid))
				{
					WeaponLog("EnforceGunState - Failed; invalid holster joint (%s)\n",
							  DevKitOnly_StringIdToStringOrHex(holster.m_name));
				}

				if ((curIdx != holsterIdx && holsterIdx != AttachIndex::kInvalid) || force)
				{
					WeaponLog("EnforceGunState - Changing weapon '%s' to holstered attach point '%s' (blend: %0.3fsec)\n",
							  DevKitOnly_StringIdToString(pWeapon->GetWeaponDefId()),
							  DevKitOnly_StringIdToString(holster.m_name),
							  blendTime);

					pWeapon->PutInHolster(holster, pNavChar, blendTime);
				}
			}
			else
			{
				WeaponLog("EnforceGunState - Weapon '%s' has no valid holster attach point!\n",
						  DevKitOnly_StringIdToString(pWeapon->GetWeaponDefId()));
			}
		}
		break;
	default:
		ANIM_ASSERTF(false, ("Invalid GunState passed to EnforceGunState: %d", gs));
		break;
	}

	ValidateWeaponAttachments();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsFistsWeapon(const ProcessWeaponBase* pWeapon)
{
	const DC::WeaponGameplayDef* pGameplayDef = pWeapon ? pWeapon->GetWeaponGameplayDef() : nullptr;
	const DC::MeleeWeaponGameplayDef* pMeleeGameplayDef = pGameplayDef ? pGameplayDef->m_meleeWeaponGameplayDef : nullptr;
	if (!pMeleeGameplayDef)
		return false;

	const DC::MeleeWeaponType mwt = pMeleeGameplayDef->m_weaponType;

	switch (mwt)
	{
	case DC::kMeleeWeaponTypeAbbyFists:
	case DC::kMeleeWeaponTypeAbbyFistsCarryYara:
	case DC::kMeleeWeaponTypeEllieFists:
	case DC::kMeleeWeaponTypeJoelFists:
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::ValidateWeaponAttachments() const
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	const NavCharacter* pNavChar = GetCharacter();
	const AttachSystem* pAs = pNavChar ? pNavChar->GetAttachSystem() : nullptr;
	if (!pAs || !pNavChar->IsInitialized())
		return true;

	// I think this is a scripted sequence that isn't actually broken, but constantly
	// triggers this code. Next game we will be stronger about missing holsters, maybe by hiding
	// the weapon.
	if (pNavChar->GetUserId() == SID("npc-scar-afm-burning-village-bg-melee-struggle-1"))
		return true;
	if (pNavChar->GetUserId() == SID("npc-afm-horse-chase-bg-fleeing-scars-at-fork--scar-3"))
		return true;

	const Npc* pNpc = Npc::FromProcess(GetCharacter());
	const PropInventory* pPropInventory = pNpc ? pNpc->GetPropInventory() : nullptr;

	if (!pPropInventory)
		return true;

	const I32F numProps = pPropInventory->GetNumProps();

	bool fail = false;

	for (I32F iProp = 0; iProp < numProps - 1; ++iProp)
	{
		const DC::PropType propType = pPropInventory->GetPropType(iProp);
		if (propType != DC::kPropTypeWeapon)
			continue;

		const ProcessWeaponBase* pWeapon = ProcessWeaponBase::FromProcess(pPropInventory->GetPropObject(iProp));

		if (!pWeapon || IsFistsWeapon(pWeapon))
			continue;

		const AttachIndex curIdx = pWeapon->GetParentAttachIndex();
		const StringId64 handName = pNavChar->GetWeaponAttachId(pWeapon->GetWeaponAnimType());
		AttachIndex handIdx = AttachIndex::kInvalid;
		pAs->FindPointIndexById(&handIdx, handName);

		if (curIdx != handIdx)
			continue;

		for (I32F iOtherProp = iProp + 1; iOtherProp < numProps; ++iOtherProp)
		{
			const DC::PropType otherPropType = pPropInventory->GetPropType(iOtherProp);
			if (otherPropType != DC::kPropTypeWeapon)
				continue;

			const ProcessWeaponBase* pOtherWeapon = ProcessWeaponBase::FromProcess(pPropInventory->GetPropObject(iOtherProp));
			if (!pOtherWeapon || IsFistsWeapon(pOtherWeapon))
				continue;

			const AttachIndex otherCurIdx = pOtherWeapon->GetParentAttachIndex();

			if (curIdx == otherCurIdx)
			{
				WeaponLog("Weapons '%s' and '%s' are both attached to the same hand '%s' at the same time!\n",
						  DevKitOnly_StringIdToString(pWeapon->GetWeaponDefId()),
						  DevKitOnly_StringIdToString(pOtherWeapon->GetWeaponDefId()),
						  DevKitOnly_StringIdToString(handName));
				fail = true;
			}
		}
	}

	if (fail)
	{
		MailNpcLogTo(pNavChar, "john_bellomy@naughtydog.com", "Invalid Weapon Attachments", FILE_LINE_FUNC);
	}

	return !fail;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::IsGunStateCorrect(const ProcessWeaponBase* pWeapon, const GunState gs) const
{
	const NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
		return true;

	const AttachSystem* pAs = pNavChar->GetAttachSystem();
	if (!pAs)
		return true;

	AttachIndex handIdx = AttachIndex::kInvalid;
	AttachIndex holsterIdx = AttachIndex::kInvalid;

	const StringId64 handName = pNavChar->GetWeaponAttachId(pWeapon->GetWeaponAnimType());
	pAs->FindPointIndexById(&handIdx, handName);

	const StringId64 holsterName = GetWeaponHolsterAttachPoint(pWeapon->GetWeaponArtDef()).m_name;
	if (holsterName != INVALID_STRING_ID_64)
	{
		pAs->FindPointIndexById(&holsterIdx, holsterName);
	}

	bool correct = true;

	const AttachIndex curIdx = pWeapon->GetParentAttachIndex();
	switch (gs)
	{
	case kGunStateOut:
		if (curIdx != handIdx && handIdx != AttachIndex::kInvalid)
		{
			correct = false;
		}
		break;
	case kGunStateHolstered:
		if (curIdx != holsterIdx && holsterIdx != AttachIndex::kInvalid)
		{
			correct = false;
		}
		break;
	}

	return correct;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::GrenadeToss(const BoundFrame& bFrame, const bool spawnHeldGrenadeIfNotPresent /* = true */)
{
	NavCharacter* pNavChar	   = GetCharacter();
	AnimControl* pAnimControl  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	if (spawnHeldGrenadeIfNotPresent)
	{
		if (!pNavChar->HoldingGrenade())
		{
			StringId64 grenadeId = INVALID_STRING_ID_64;
			if (const Npc* pNpc = Npc::FromProcess(pNavChar))
			{
				if (const DC::AiWeaponLoadout* pWeaponLoadout = pNpc->GetWeaponLoadout())
				{
					grenadeId = pWeaponLoadout->m_grenade;
				}
			}
			pNavChar->SpawnHeldGrenade(grenadeId);
		}
	}

	UpdateOverlays();

	ANIM_ASSERT(IsReasonable(bFrame));

	FadeToStateParams params;
	params.m_apRef = bFrame;
	params.m_apRefValid = true;
	params.m_animFadeTime = 0.4f;

	m_grenadeTossAction.FadeToState(pAnimControl,
									SID("s_standing-grenade-toss-ap"),
									params,
									AnimAction::kFinishOnNonTransitionalStateReached);
	m_grenadeThrown = false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::AbortGrenadeToss()
{
	if (m_grenadeTossAction.IsDone() && m_grenadeDrawAction.IsDone())
		return;

	NavCharacter* pNavChar	   = GetCharacter();
	AnimControl* pAnimControl  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	if (pBaseLayer->IsTransitionValid(SID("abort")))
	{
		AiLogAnim(pNavChar, "Aborting grenade toss\n");

		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

		AnimAction& animAction = m_grenadeTossAction.IsDone() ? m_grenadeTossAction : m_grenadeDrawAction;
		animAction.Request(pAnimControl, SID("abort"), AnimAction::kFinishOnNonTransitionalStateReached);

		NavAnimHandoffDesc handoff;
		handoff.SetStateChangeRequestId(animAction.GetStateChangeRequestId(), INVALID_STRING_ID_64);
	}

	UpdateOverlays();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::IsThrowingGrenade() const
{
	return !m_grenadeTossAction.IsDone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::DrawGrenadeRock()
{
	WeaponLogStr("DrawGrenadeRock\n");

	NavCharacter* pNavChar	   = GetCharacter();
	AnimControl* pAnimControl  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	FadeToStateParams params;
	params.m_animFadeTime = 0.4f;

	m_grenadeDrawAction.FadeToState(pAnimControl,
									SID("s_draw-grenade"),
									params,
									AnimAction::kFinishOnNonTransitionalStateReached);

	StringId64 grenadeId = SID("rock");

	pNavChar->SpawnHeldGrenade(grenadeId);

	m_grenadeDrawn = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::DrawGrenade(const bool spawnHeldGrenadeIfNotPresent /* = true */)
{
	WeaponLogStr("DrawGrenade\n");

	NavCharacter* pNavChar	   = GetCharacter();
	AnimControl* pAnimControl  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	UpdateOverlays();

	FadeToStateParams params;
	params.m_animFadeTime = 0.4f;

	m_grenadeDrawAction.FadeToState(pAnimControl,
									SID("s_draw-grenade"),
									params,
									AnimAction::kFinishOnNonTransitionalStateReached);

	m_grenadeDrawn = false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessWeapon* AiWeaponController::WeaponToss(Character& receiver, ForgetAboutWeaponFunc forget)
{
	NavCharacter* pNavChar			 = GetCharacter();
	AnimControl* pAnimControl		 = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	ProcessWeaponBase* pMyWeaponBase = pNavChar ? pNavChar->GetWeapon() : nullptr;
	ProcessWeapon* pMyWeapon		 = nullptr;
	AnimStateLayer* pBaseLayer		 = pAnimControl->GetBaseStateLayer();

	ProcessWeapon* pRet = nullptr;

	if (pMyWeaponBase && pMyWeaponBase->IsKindOf(SID("ProcessWeapon")))
	{
		pMyWeapon = static_cast<ProcessWeapon*>(pMyWeaponBase);
	}

	if (!pMyWeapon)
		return nullptr;

	const StringId64 stateId = SID("s_weapon-toss");
	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	FadeToStateParams params;
	params.m_animFadeTime = 0.2f;

	m_weaponTossAction.FadeToState(pAnimControl, stateId, params);

	m_processWeaponTossAction.Set(*pMyWeapon, receiver, forget);

	return pMyWeapon;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiWeaponController::GetAnimPhase() const
{
	if (const NavCharacter* pNavChar = GetCharacter())
	{
		const AnimControl* pAnimControl = pNavChar->GetAnimControl();

		const float weaponTossPhase = m_weaponTossAction.GetAnimPhase(pAnimControl);
		if (weaponTossPhase >= 0.0f)
		{
			return weaponTossPhase;
		}
		else
		{
			return m_grenadeTossAction.GetAnimPhase(pAnimControl);
		}
	}

	return -1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	if (g_navCharOptions.m_displayWeaponController)
	{
		pPrinter->PrintText(kColorWhite, "Gun State: %s\n", GetGunStateName(m_gunState));

		if (m_changeSuppressionSec > 0.0f)
		{
			if (m_suppressedRequest.m_gs != kGunStateMax)
				pPrinter->PrintText(kColorWhite, "  Suppressed GunState: %s", GetGunStateName(m_suppressedRequest.m_gs));
			if (m_suppressedPrimaryId)
				pPrinter->PrintText(kColorWhite, "  Suppressed Primary: %s", DevKitOnly_StringIdToString(m_suppressedPrimaryId));

			pPrinter->PrintText(kColorWhite, "Suppressed! %0.4f seconds", m_changeSuppressionSec);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::RequestWeaponUp()
{
	if (!m_weaponUpRequested)
	{
		WeaponLogStr("RequestWeaponUp\n");

		if (TryPlayWeaponUpGesture())
		{
			m_weaponUpRequested = true;
			m_weaponUpFactor	= 1.0f;
			m_wepaonUpDownDirty = false;
			return;
		}

		m_weaponUpRequested = true;
		m_wepaonUpDownDirty = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::RequestWeaponDown()
{
	if (m_weaponUpRequested)
	{
		WeaponLogStr("RequestWeaponDown\n");

		if (TryPlayWeaponDownGesture())
		{
			m_weaponUpRequested = false;
			m_weaponUpFactor	= 0.0f;
			m_wepaonUpDownDirty = false;
			return;
		}

		m_weaponUpRequested = false;
		m_wepaonUpDownDirty = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::IsWeaponUpRequested() const
{
	return m_weaponUpRequested;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::UpdateWeaponUpDownPercent(float pcnt, float target)
{
	m_wepaonUpDownDirty	   = false;
	m_weaponUpTargetFactor = target;

	if (target < kSmallFloat)
	{
		m_weaponUpFactor = 1.0f - pcnt;
	}
	else
	{
		m_weaponUpFactor = pcnt;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 AiWeaponController::CollectHitReactionStateFlags() const
{
	if (!m_grenadeTossAction.IsDone())
		return DC::kHitReactionStateMaskCombatThrowGrenade;

	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::RequestPrimaryWeapon(StringId64 primaryWeaponId)
{
	if (m_changeSuppressionSec > 0.0f)
	{
		if (m_suppressedPrimaryId != primaryWeaponId)
		{
			WeaponLog("RequestPrimaryWeapon(%s) Suppressed!\n", DevKitOnly_StringIdToString(primaryWeaponId));
			m_suppressedPrimaryId = primaryWeaponId;
		}

		return false;
	}

	const Npc* pNpc = Npc::FromProcess(GetCharacter());
	const PropInventory* pPropInventory = pNpc ? pNpc->GetPropInventory() : nullptr;

	if (!pPropInventory)
		return false;

	const I32F newPrimaryIndex = pPropInventory->GetWeaponIndexByWeaponId(primaryWeaponId);
	if (newPrimaryIndex < 0)
		return false;

	const PropInventoryId newPrimaryUid = pPropInventory->GetPropUid(newPrimaryIndex);
	const PropInventoryId curPrimaryUid = pPropInventory->GetPrimaryWeaponUid();

	if ((newPrimaryUid == curPrimaryUid) && IsPrimaryWeaponUpToDate())
	{
		return true;
	}

	if ((m_requestedPrimaryWeaponUid != INVALID_PROPINV_ID) && (newPrimaryUid == m_requestedPrimaryWeaponUid))
	{
		return true;
	}

	WeaponLog("RequestPrimaryWeapon(%s)\n", DevKitOnly_StringIdToString(primaryWeaponId));

	if (m_requestedPrimaryWeaponId != INVALID_STRING_ID_64)
	{
		if (m_gunState == kGunStateHolstered || m_unholsteringForSwitch)
		{
			WeaponLog("ABORTING previous RequestPrimaryWeapon(%s), m_unholsteringForSwitch = %s\n",
					  DevKitOnly_StringIdToString(m_requestedPrimaryWeaponId),
					  m_unholsteringForSwitch ? "TRUE" : "FALSE");

			AbortAnims(true);

			// AbortAnims() nukes our requested primary weapon as well as unholstering for switch,
			// so bail out early here so we don't re-request it down below
			return true;
		}
		else
		{
			WeaponLog("Previous RequestPrimaryWeapon(%s) already has our gun out, so holstering\n",
					  DevKitOnly_StringIdToString(m_requestedPrimaryWeaponId));
			RequestGunState(kGunStateHolstered, false);
		}
	}
	else
	{
		WeaponLog("Setting saved gun state = %s\n", GetGunStateName(m_gunState));

		m_savedGunState = m_gunState;
	}

	m_unholsteringForSwitch		= false;
	m_requestedPrimaryWeaponId	= primaryWeaponId;
	m_requestedPrimaryWeaponUid = newPrimaryUid;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::ForcePrimaryWeapon(StringId64 primaryWeaponId)
{
	Npc* pNpc = Npc::FromProcess(GetCharacter());
	const PropInventory* pPropInventory = pNpc ? pNpc->GetPropInventory() : nullptr;

	if (!pPropInventory)
		return false;

	const I32F newPrimaryIndex = pPropInventory->GetWeaponIndexByWeaponId(primaryWeaponId);
	if (newPrimaryIndex < 0)
		return false;

	const PropInventoryId newPrimaryUid = pPropInventory->GetPropUid(newPrimaryIndex);
	const PropInventoryId curPrimaryUid = pPropInventory->GetPrimaryWeaponUid();
	if (newPrimaryUid == curPrimaryUid)
	{
		if ((m_requestedPrimaryWeaponUid != INVALID_PROPINV_ID) && (newPrimaryUid == m_requestedPrimaryWeaponUid))
			return true;
		else if (IsPrimaryWeaponUpToDate())
			return true;
	}

	WeaponLog("ForcePrimaryWeapon(%s)\n", DevKitOnly_StringIdToString(primaryWeaponId));

	if (m_changeSuppressionSec > 0.0f)
	{
		WeaponLogStr("ForcePrimaryWeapon called mid-suppression, disabling suppression\n");
		m_changeSuppressionSec = -1.0f;
		m_suppressedRequest = ChangeParams();
		m_suppressedPrimaryId = INVALID_STRING_ID_64;
	}

	if (m_requestedPrimaryWeaponId != INVALID_STRING_ID_64)
	{
		WeaponLog("ABORTING previous requested primary weapon '%s'\n",
				  DevKitOnly_StringIdToString(m_requestedPrimaryWeaponId));

		AbortAnims(true);
	}

	const GunState finalGunState = GetRequestedGunState();

	if (m_gunState != kGunStateHolstered)
	{
		WeaponLog("ForcePrimaryWeapon(%s) : Forcing gun state to holstered\n", DevKitOnly_StringIdToString(primaryWeaponId));

		if (ProcessWeaponBase* pWeapon = pNpc->GetWeapon())
		{
			SetWeaponInHand(pWeapon, false);
			EnforceGunState(pWeapon, kGunStateHolstered);
		}

		m_gunState = kGunStateHolstered;
	}

	m_requestedPrimaryWeaponId	= primaryWeaponId;
	m_requestedPrimaryWeaponUid = newPrimaryUid;

	ApplyPrimaryWeaponSwitch();

	m_unholsteringForSwitch		= false;
	m_requestedPrimaryWeaponId	= INVALID_STRING_ID_64;
	m_requestedPrimaryWeaponUid = INVALID_PROPINV_ID;

	ForceGunState(finalGunState, false, false, 0.0f, true);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiWeaponController::IsPrimaryWeaponUpToDate() const
{
	return m_requestedPrimaryWeaponUid == INVALID_PROPINV_ID;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessWeaponBase* AiWeaponController::GetPrimaryWeapon() const
{
	const Npc* pNpc = Npc::FromProcess(GetCharacter());
	if (!pNpc)
		return nullptr;

	const PropInventory* pPropInventory = pNpc->GetPropInventory();
	if (!pPropInventory)
		return nullptr;

	return pPropInventory->GetPrimaryWeapon();
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessWeaponBase* AiWeaponController::GetRequestedPrimaryWeapon() const
{
	if (IsPrimaryWeaponUpToDate())
		return nullptr;

	const Npc* pNpc = Npc::FromProcess(GetCharacter());
	if (!pNpc)
		return nullptr;

	const PropInventory* pPropInventory = pNpc->GetPropInventory();
	if (!pPropInventory)
		return nullptr;

	const I32F weaponIndex = pPropInventory->GetPropIndexByUid(m_requestedPrimaryWeaponUid);
	return ProcessWeaponBase::FromProcess(pPropInventory->GetPropObject(weaponIndex));
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessWeaponBase* AiWeaponController::GetAnimRequestedWeapon(bool allowRequestedFallback /* = true */) const
{
	if (m_animRequestedWeaponId == INVALID_STRING_ID_64)
	{
		if (allowRequestedFallback)
			return GetRequestedPrimaryWeapon();
		return nullptr;
	}

	const Npc* pNpc = Npc::FromProcess(GetCharacter());
	if (!pNpc)
		return nullptr;

	const PropInventory* pPropInventory = pNpc->GetPropInventory();
	if (!pPropInventory)
		return nullptr;

	const I32F weaponIndex = pPropInventory->GetPropIndexByUid(m_animRequestedWeaponUid);
	return ProcessWeaponBase::FromProcess(pPropInventory->GetPropObject(weaponIndex));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::ApplyPrimaryWeaponSwitch()
{
	if (m_requestedPrimaryWeaponId == INVALID_STRING_ID_64)
		return;

	Npc* pNpc = Npc::FromProcess(GetCharacter());
	PropInventory* pPropInventory = pNpc ? pNpc->GetPropInventory() : nullptr;

	if (!pPropInventory)
	{
		return;
	}

	if (pPropInventory->GetPrimaryWeaponUid() == m_requestedPrimaryWeaponUid)
	{
		return;
	}

	if (m_gunState != kGunStateHolstered)
	{
		WeaponLogStr("Applying primary weapon switch but gun is still out, forcing holstered\n");
		ForceGunState(kGunStateHolstered);
	}

	WeaponLog("ApplyPrimaryWeaponSwitch : switching primary weapon to %s\n",
			  DevKitOnly_StringIdToString(m_requestedPrimaryWeaponId));

	const I32F newPrimaryIndex = pPropInventory->GetPropIndexByUid(m_requestedPrimaryWeaponUid);

	if (newPrimaryIndex >= 0)
	{
		pPropInventory->SetPrimaryWeapon(newPrimaryIndex);

		UpdateOverlays();
	}
	else
	{
		WeaponLog("ApplyPrimaryWeaponSwitch : FAILED to get prop inventory index for weapon %s\n",
				  DevKitOnly_StringIdToString(m_requestedPrimaryWeaponId));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::ApplyAnimRequestedWeaponSwitch()
{
	if (m_animRequestedWeaponId == INVALID_STRING_ID_64)
		return;

	Npc* pNpc = Npc::FromProcess(GetCharacter());
	PropInventory* pPropInventory = pNpc ? pNpc->GetPropInventory() : nullptr;

	if (!pPropInventory)
	{
		m_animRequestedWeaponId	 = INVALID_STRING_ID_64;
		m_animRequestedWeaponUid = INVALID_PROPINV_ID;
		return;
	}

	if (pPropInventory->GetPrimaryWeaponUid() == m_animRequestedWeaponUid)
	{
		m_animRequestedWeaponId	 = INVALID_STRING_ID_64;
		m_animRequestedWeaponUid = INVALID_PROPINV_ID;
		return;
	}

	if (m_gunState != kGunStateHolstered)
	{
		WeaponLogStr("Applying anim requested weapon switch but gun is still out, forcing holstered\n");
		ForceGunState(kGunStateHolstered);
	}

	WeaponLog("ApplyAnimRequestedWeaponSwitch : switching primary weapon to %s\n",
			  DevKitOnly_StringIdToString(m_animRequestedWeaponId));

	const I32F newPrimaryIndex = pPropInventory->GetPropIndexByUid(m_animRequestedWeaponUid);

	if (newPrimaryIndex >= 0)
	{
		pPropInventory->SetPrimaryWeapon(newPrimaryIndex);
	}
	else
	{
		WeaponLog("ApplyAnimRequestedWeaponSwitch : FAILED to get prop inventory index for weapon %s\n",
				  DevKitOnly_StringIdToString(m_animRequestedWeaponId));
	}

	m_animRequestedWeaponId	 = INVALID_STRING_ID_64;
	m_animRequestedWeaponUid = INVALID_PROPINV_ID;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::RequestWeaponIdleAnim(StringId64 animId)
{
	m_requestedWeaponIdleAnim = animId;
	m_weaponIdleAnimDecay = Frames(3.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiWeaponController::UpdateWeaponIdleAnim()
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	ProcessWeaponBase* pWeapon = pNavChar ? pNavChar->GetWeapon() : nullptr;

	if (!pWeapon)
	{
		return;
	}

	const bool holstered = GetCurrentGunState() == kGunStateHolstered;

	if (m_weaponIdleAnimDecay > Seconds(0.0f))
	{
		pWeapon->SetDesiredIdleAnim(m_requestedWeaponIdleAnim);
	}
	else if (holstered)
	{
		pWeapon->SetDesiredIdleAnim(SID("holstered-idle"));
	}
	else
	{
		pWeapon->SetDesiredIdleAnim(SID("unholstered-idle"));
	}

	pWeapon->TryPlayIdleAnim();

	m_weaponIdleAnimDecay -= pNavChar->GetClock()->GetDeltaTimeFrame();
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiWeaponController* CreateAiWeaponController()
{
	return NDI_NEW AiWeaponController();
}
