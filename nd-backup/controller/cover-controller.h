/*
 * Copyright (c) 2009 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/util/tracker.h"

#include "gamelib/gameplay/ai/controller/action-pack-controller.h"
#include "gamelib/gameplay/ai/controller/nd-locomotion-controller.h"
#include "gamelib/gameplay/ap-entry-cache.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nav/action-pack-entry-def.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"

#include "game/ai/agent/npc-handle.h"
#include "game/scriptx/h/hit-reactions-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
	struct ApEntryItem;
	struct ApEntryItemList;
}

namespace ApEntry
{
	struct AvailableEntry;
	struct CharacterState;
}

namespace ApExit
{
	struct CharacterState;
}

class Character;
class JointTree;
class JacobianMap;
struct FadeToStateParams;

/// --------------------------------------------------------------------------------------------------------------- ///
class IAiCoverController : public ActionPackController
{
public:
	typedef ActionPackController ParentClass;

	enum CoverMode
	{
		kCoverModeInvalid,
		kCoverModeEntering,
		kCoverModeIdle,
		kCoverModeAim,
		kCoverModePeek,
		kCoverModeIdleFire,
		kCoverModeHunker,
		kCoverModeExiting,
		kCoverModeDraw,
		kCoverModeThrow,
		kCoverModeHitReaction,
		kCoverModePerformance,
		kCoverModeStepOut,
		kCoverModeMax
	};

	static const char* GetCoverModeStr(CoverMode mode)
	{
		switch (mode)
		{
		case kCoverModeEntering:	return "Entering";
		case kCoverModeIdle:		return "Idle";
		case kCoverModeAim:			return "Aim";
		case kCoverModePeek:		return "Peek";
		case kCoverModeIdleFire:	return "IdleFire";
		case kCoverModeHunker:		return "Hunker";
		case kCoverModeExiting:		return "Exiting";
		case kCoverModeDraw:		return "Draw";
		case kCoverModeThrow:		return "Throw";
		case kCoverModeHitReaction:	return "HitReaction";
		case kCoverModePerformance:	return "Performance";
		case kCoverModeStepOut:		return "StepOut";
		}

		return "<unknown>";
	}

	enum CoverShareDir
	{
		kCoverShareDirFr,
		kCoverShareDirLt,
		kCoverShareDirRt
	};

	IAiCoverController();
	virtual ~IAiCoverController() override {}

	// Controller function.
	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void Reset() override;
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual void UpdateProcedural() override;
	virtual bool IsBusy() const override;
	virtual bool IsEnterComplete() const override;
	virtual U64 CollectHitReactionStateFlags() const override;
	virtual bool RequestAbortAction() override;

	// action pack interface
	virtual bool ResolveEntry(const ActionPackResolveInput& input,
							  const ActionPack* pActionPack,
							  ActionPackEntryDef* pDefOut) const override;
	virtual bool ResolveDefaultEntry(const ActionPackResolveInput& input,
									 const ActionPack* pActionPack,
									 ActionPackEntryDef* pDefOut) const override;
	virtual bool UpdateEntry(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 ActionPackEntryDef* pDefOut) const override;
	virtual void Enter(const ActionPackResolveInput& input,
					   ActionPack* pActionPack,
					   const ActionPackEntryDef& entryDef) override;
	virtual void Exit(const PathWaypointsEx* pExitPathPs) override;
	virtual void OnExitComplete() override;
	virtual bool IsReadyToExit() const override;
	virtual bool ResolveForImmediateEntry(const ActionPackResolveInput& input,
										  const ActionPack* pActionPack,
										  ActionPackEntryDef* pDefOut) const override;
	virtual bool EnterImmediately(const ActionPack* pActionPack, const ActionPackEntryDef& entryDef) override;
	virtual bool TeleportInto(ActionPack* pActionPack,
							  bool playEntireEntryAnim,
							  float fadeTime,
							  BoundFrame* pNewFrameOut,
							  uintptr_t apUserData = 0) override;

	virtual bool IsAimingFirearm() const override;

	virtual bool OverrideWeaponUp() const override { return m_cachedCoverMode != kCoverModeExiting; }

	virtual StringId64 GetFlinchGestureName(bool smallFlinch) const override;

	virtual I32F GetEnterAnimNames(const ActionPack* pAp, StringId64* pNamesOut, U32F maxNamesOut) const override;
	virtual I32F GetExitAnimNames(const ActionPack* pAp, StringId64* pNamesOut, U32F maxNamesOut) const override;
	I32F GetTransferAnimNames(const ActionPack* pAp, StringId64* pNamesOut, U32F maxNamesOut) const;

	I32F GetCoverApEnterAnimCount(const CoverActionPack* pCoverAp) const;
	I32F GetCoverApExitAnimCount(const CoverActionPack* pCoverAp) const;
	I32F GetCoverApTransferAnimCount(const CoverActionPack* pCoverAp) const;

	void SetShootSide(bool shootSide);
	bool GetShootSide() const { return m_shootSide; }

	void EnableCoverShare(const Locator& coverShareLocWs);
	void DisableCoverShare();

	bool CanAim() const;

	void Idle(bool restoreAp = false);
	void Aim();
	void AimReassess();
	bool AimFire();
	void Peek();
	void IdleFire();
	bool StepOut();

	void Share(float startAnimPhase = -1.0f);
	void Unshare(float startAnimPhase = -1.0f);

	bool IsSharing() const;
	bool IsDoingPeek() const;
	bool IsBlockedByBreakable() const;
	bool IsInOpenState() const;

	virtual void ConfigureCharacter(Demeanor demeanor,
									const DC::NpcDemeanorDef* pDemeanorDef,
									const NdAiAnimationConfig* pAnimConfig) override;
	void ApplyOverlays();

	bool GrenadeToss(const bool spawnHeldGrenadeIfNotPresent = true);
	void AbortGrenadeToss();
	bool GrenadeDraw(Point targetPos, bool spawnHeldGrenadeIfNotPresent = true);

	// returns resolved anim ID
	StringId64 DoCoverPerformance(StringId64 performanceId,
								  FadeToStateParams params,
								  DC::HitReactionStateMask hitReactionStateMask = 0);

	// Debug
	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;
	virtual void DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const override;
	virtual void DebugDrawExits(const ActionPackResolveInput& input,
								const ActionPack* pActionPack,
								const IPathWaypoints* pPathPs) const override;

	StringId64 GetDestinationState(CoverDefinition::CoverType coverType) const;

	void OnBulletHitNear();

	const ActionPackEntryDef& GetEntryDef() const { return m_entryDef; }

	void OverrideNextEnterAnim(const StringId64 enterAnimId);

	CoverMode GetCurrentCoverMode() const;
	virtual bool TakeHit(const HitDescription* pHitDesc) override;

	static NdGameObject* FindThrowableObject(const BoundFrame& coverAp);

	void RefreshCoverOverlays();

	BoundFrame GetApReferenceForCover(const CoverActionPack* pCoverAp,
									  CoverActionPack::PreferredDirection dirPref) const;
	BoundFrame GetApReferenceForCurrentCover() const;
	inline F32 GetSidwaysOffset() const { return m_sidewaysOffset; }
	inline void SetSidwaysOffset(F32 sidewaysOffset) { m_sidewaysOffset = sidewaysOffset; }

	const CoverActionPack* GetActionPack() const;
	CoverDefinition::CoverType GetCoverType() const { return m_coverType; }

private:
	const DC::ApEntryItemList* GetCoverEntries(CoverDefinition::CoverType coverType,
											   StringId64 setIdOverride = INVALID_STRING_ID_64) const;
	const DC::ApExitAnimList* GetExits(CoverDefinition::CoverType coverType) const;
	const DC::ApEntryItemList* GetTransfers(CoverDefinition::CoverType coverType) const;

	bool IsPointOpenOnNavPs(Point_arg posPs) const;

	void UpdateOverlayMode(const CoverDefinition::CoverType coverType, bool canStepOut);

	virtual void MakeEntryCharacterState(const ActionPackResolveInput& input,
										 const ActionPack* pActionPack,
										 ApEntry::CharacterState* pCsOut) const override;
	void ResetCoverOverlays();

	bool PickBestCoverExit(const IPathWaypoints* pPathPs, ActionPackExitDef* pSelectedExitOut) const;

	float GetIkFade(float* pDeltaYOut, bool force) const;

	I32F GetCoverApEnterAnims(const CoverActionPack* pCoverAp,
							  DC::ApEntryItem entryItems[],
							  I32F entryItemsSize,
							  bool isTransfer,
							  CoverActionPack::PreferredDirection dirPref) const;
	I32F GetCoverApExitAnims(const CoverActionPack* pCoverAp,
							 DC::ApExitAnimDef exitItems[],
							 I32F exitItemsSize,
							 CoverActionPack::PreferredDirection dirPref) const;

	void ForceBlendToDestinationState(CoverDefinition::CoverType coverType);

	AnimActionWithSelfBlend m_animAction;
	AnimActionWithSelfBlend m_enterAction;
	AnimActionWithSelfBlend m_exitAction;
	AnimAction m_peekAction;
	AnimAction m_performanceAction;
	AnimActionWithSelfBlend m_throwGrenadeAction;
	DC::HitReactionStateMask m_performanceStateMask;
	ActionPackEntryDef m_entryDef;
	CoverDefinition::CoverType m_coverType; // remember the type of cover so that we can exit even if the cover goes away as we use it

	NavAnimHandoffDesc m_exitHandoff;

	StringId64 m_enterAnimOverride;

	float m_rangedEntryRadius; // distance from cover to be classified as a 'ranged entry'
	float m_sidewaysOffset;
	float m_exitPhase;

	bool m_canStepOut;
	bool m_shootSide;
	bool m_doingGrenadeDraw;
	bool m_doingGrenadeToss;
	bool m_doingMmEnter;
	bool m_sharingCover;
	bool m_grenadeDrawn;
	bool m_grenadeReleased;

	CoverMode m_cachedCoverMode;

	JointTree* m_pIkTree;
	JacobianMap* m_pIkJacobianMap[kLegCount];
	SpringTracker<float> m_ikHeightTracker;
	float m_ikCurDeltaY;

	Locator m_enterFinalLocPs;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiCoverController* CreateAiCoverController();

BoundFrame GetApReferenceForCover(const CoverActionPack* pCoverAp,
								  CoverDefinition::CoverType ct,
								  F32 sidewaysOffset = 0.0f);
