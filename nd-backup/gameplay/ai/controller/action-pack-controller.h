/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-defines.h"

#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/character-motion-match-locomotion.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nav/nav-command.h"
#include "gamelib/gameplay/nd-subsystem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPack;
class ActionPackEntryDef;
class BoundFrame;
class NavCharacter;
class PathWaypointsEx;
class CharacterMotionMatchLocomotion;
class AiMmActionPackInterface;
class MotionModel;
struct ActionPackExitDef;
struct ActionPackResolveInput;

namespace DC
{
	struct ApEntryItem;
	struct ApEntryItemList;
	struct ApExitAnimDef;
	struct ApExitAnimList;
}

namespace ApEntry
{
	struct AvailableEntry;
	struct CharacterState;
}

namespace ApExit
{
	struct AvailableExit;
	struct CharacterState;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Derive from this class instead of AnimActionController if you require the action pack interfaces previously
// provided by AnimActionController.
class ActionPackController : public AnimActionController
{
public:
	~ActionPackController() override {}

	virtual void Reset() override;

	virtual bool ResolveEntry(const ActionPackResolveInput& input,
							  const ActionPack* pActionPack,
							  ActionPackEntryDef* pDefOut) const = 0;
	virtual bool ResolveDefaultEntry(const ActionPackResolveInput& input,
									 const ActionPack* pActionPack,
									 ActionPackEntryDef* pDefOut) const = 0;
	virtual bool UpdateEntry(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 ActionPackEntryDef* pDefOut) const = 0;

	virtual void Enter(const ActionPackResolveInput& input, ActionPack* pActionPack, const ActionPackEntryDef& entryDef)
	{
		m_hActionPack = pActionPack;
	}
	virtual void Exit(const PathWaypointsEx* pExitPathPs) {}
	virtual void OnExitComplete()
	{
		m_hActionPack = nullptr;
	}
	virtual bool ResolveForImmediateEntry(const ActionPackResolveInput& input,
										  const ActionPack* pActionPack,
										  ActionPackEntryDef* pDefOut) const
	{
		return false;
	}

	virtual bool EnterImmediately(const ActionPack* pActionPack, const ActionPackEntryDef& entryDef) 
	{
		m_hActionPack = pActionPack;
		return true; 
	}
	virtual bool TeleportInto(ActionPack* pActionPack,
							  bool playEntireEntryAnim,
							  float fadeTime,
							  BoundFrame* pNewFrameOut,
							  uintptr_t apUserData = 0)
	{
		m_hActionPack = pActionPack;
		return true;
	}
	virtual void DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const {}
	virtual void DebugDrawExits(const ActionPackResolveInput& input,
								const ActionPack* pActionPack,
								const IPathWaypoints* pPathPs) const
	{
	}
	virtual bool IsAimingFirearm() const { return false; }
	virtual bool IsEnterComplete() const { return !IsBusy(); }
	virtual bool IsEntryCommitted() const { return true; }
	virtual bool ShouldAutoExitAp(const ActionPack* pAp) const { return false; }

	virtual bool OverrideWeaponUp() const { return false; }

	virtual StringId64 GetFlinchGestureName(bool smallFlinch) const { return INVALID_STRING_ID_64; }

	bool ResolveExit(const ApExit::CharacterState& cs,
					 const BoundFrame& apRef,
					 const DC::ApExitAnimList* pExitList,
					 ActionPackExitDef* pExitOut,
					 StringId64 apRefId = INVALID_STRING_ID_64) const;

	virtual bool GetExitPathOrigin(NavLocation* pLocOut) const { return false; }

	virtual bool IsReadyToExit() const { return true; }

	ActionPackHandle GetActionPackHandle() const { return m_hActionPack; }

	virtual I32F GetEnterAnimNames(const ActionPack* pAp, StringId64* pNamesOut, U32F maxNamesOut) const { return 0; }
	virtual I32F GetExitAnimNames(const ActionPack* pAp, StringId64* pNamesOut, U32F maxNamesOut) const { return 0; }

	static bool TestForImmediateEntry(const NdGameObject* pGo, const ActionPackEntryDef& apEntry);

protected:
	virtual void MakeEntryCharacterState(const ActionPackResolveInput& input,
										 const ActionPack* pActionPack,
										 ApEntry::CharacterState* pCsOut) const;

	virtual void MakeExitCharacterState(const ActionPack* pActionPack,
										const IPathWaypoints* pPathPs,
										ApExit::CharacterState* pCsOut) const;

	bool EnterUsingMotionMatching(const ArtItemAnimHandle hGuideAnim,
								  const BoundFrame& startAp,
								  const BoundFrame& endAp,
								  float startPhase,
								  float endPhase,
								  const StringId64 motionMatchSetId);
	
	const AiMmActionPackInterface* GetMotionMatchInterface() const;
	AiMmActionPackInterface* GetMotionMatchInterface();
	const CharacterMotionMatchLocomotion* GetMotionMatchController() const;
	CharacterMotionMatchLocomotion* GetMotionMatchController();
	bool IsMotionMatchingEnterComplete(bool statusIfNoController = true) const;
	bool GetMmEnterStatus(StringId64& animIdOut, float& phaseOut) const;

	void DebugDrawEntryAnims(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 const BoundFrame& apRef,
							 const DC::ApEntryItemList& entryList,
							 const DC::ApEntryItem* pChosenEntry,
							 const NavCharOptions::ApControllerOptions& debugOptions) const;

	void DebugDrawExitAnims(const ActionPackResolveInput& input,
							const ActionPack* pActionPack,
							const BoundFrame& apRef,
							const IPathWaypoints* pExitPathPs,
							const DC::ApExitAnimList& exitList,
							const DC::ApExitAnimDef* pChosenExit,
							const NavCharOptions::ApControllerOptions& debugOptions) const;

	CharacterMmLocomotionHandle m_hMmController = nullptr;
	ActionPackHandle m_hActionPack = nullptr;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AiMmActionPackInterface : public CharacterLocomotionInterface
{
	typedef CharacterLocomotionInterface ParentClass;
	SUBSYSTEM_BIND(Character);

public:
	struct CustomStepData
	{
		float m_phase = -1.0f;
	};

	virtual void GetInput(ICharacterLocomotion::InputData* pData) override;
	virtual const IMotionPose* GetPose(const MotionMatchingSet* pArtItemSet, bool debug) override;

	virtual void Update(const Character* pChar, MotionModel& modelPs) override;

	virtual bool HasCustomModelStep() const override { return true; }
	virtual bool DoCustomModelStep(MotionModel& model,
								   const MotionModelInput& input,
								   float deltaTime,
								   Locator* pAlignOut,
								   float* pClampFactorOut,
								   bool debug) const override;
	virtual bool GetExtraSample(AnimSampleBiased& extraSample) const override;

	void EnableCoverShare(const Locator& shareCoverLocator) { m_bShouldUseCoverShare = true; m_coverShareLocator = shareCoverLocator; }
	void DisableCoverShare() { m_bShouldUseCoverShare = false; }

	void Configure(const StringId64 motionMatchSetId,
				   const ArtItemAnimHandle hGuideAnim,
				   float startPhase,
				   float maxEndPhase,
				   const BoundFrame& startAp,
				   const BoundFrame& endAp);

	void UpdateEndPhase(float endPhase) { m_endPhase = endPhase; }
	void UpdateStartAp(const BoundFrame& ap) { m_startAp = ap; }
	void UpdateEndAp(const BoundFrame& ap) { m_endAp = ap; }
	void SetCurPhase(float phase, MotionModel& model);

	virtual void DebugDraw(const Character* pChar,
						   const MotionModel& modelPs) const override;

	void DebugDrawPoseAtPhase(float phase, Color c) const;
	void DebugDrawGuidePath(Color c) const;

	bool IsComplete() const { return (m_curPhase >= m_endPhase) || (m_curPhase < 0.0f); }

	const ArtItemAnim* GetGuideAnim() const { return m_hGuideAnim.ToArtItem(); }
	float GetCurPhase() const { return m_curPhase; }
	float GetPrevPhase() const { return m_prevPhase; }
	StringId64 GetSetId() const { return m_motionMatchSetId; }

	static float DetermineEndPhase(const ArtItemAnim* pGuideAnim, float startPhase, float maxEndPhase, bool mirror);

private:
	bool m_valid = false;

	Locator m_coverShareLocator;

	StringId64 m_motionMatchSetId = INVALID_STRING_ID_64;
	ArtItemAnimHandle m_hGuideAnim;

	BoundFrame m_startAp;
	BoundFrame m_endAp;

	float m_startPhase = 0.0f;
	float m_endPhase   = 1.0f;
	float m_curPhase   = -1.0f;
	float m_prevPhase  = -1.0f;

	bool m_bShouldUseCoverShare = false;
};
