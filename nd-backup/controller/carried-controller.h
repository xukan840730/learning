/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/character-motion-match-locomotion.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AiCarriedController : public AnimActionController
{
public:
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override {}

	virtual bool IsBusy() const override;
	virtual bool ShouldInterruptSkills() const override;
	virtual bool ShouldInterruptNavigation() const override;
	
	void BeginCarried(NavCharacter *pCarrier);
	void EndCarried();

private:

	void UpdateTopInfo();
	void UpdateApRef();

	AnimInstance::ID m_carrierAnimIdLastFrame;
	MutableNavCharacterHandle m_hCarrier;
};

AiCarriedController* CreateCarriedController();
