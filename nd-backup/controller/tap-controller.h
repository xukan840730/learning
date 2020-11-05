/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ailib/controller/ai-tap-controller.h"

#include "gamelib/gameplay/ai/agent/nav-character-adapter.h"
#include "gamelib/gameplay/ai/controller/nd-traversal-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class TapController : public AiTapController
{
	typedef AiTapController ParentClass;

	// Rope climb
	FSM_STATE_DECLARE(RopeClimb);

public:
	// For Fsm
	virtual U32F GetMaxStateAllocSize() override;

	// From AnimActionController
	virtual void Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl) override;
	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	virtual U64 CollectHitReactionStateFlags() const override;

	// From ActionPackController
	virtual bool IsAimingFirearm() const override;

	// From AiTapController
	virtual bool WasShot() const override;
	virtual void RequestDialog(StringId64 dialogId, NdGameObject* pSpeaker, const TraversalActionPack* pTap) override;

	// Rope climb
	virtual void EnterRopeClimb() override;
	void SetRopeClimbDir(float dir);
	float GetRopeClimbDir() const;

	virtual Demeanor GetFinalDemeanor() const override;

protected:
	float m_ropeClimbDir;
};

/// --------------------------------------------------------------------------------------------------------------- ///
TapController* CreateTapController();
void ValidateTraversalAnimations(const NdGameObject* pCharacter);
