/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/character-motion-match-locomotion.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AiFlockController : public AnimActionController
{
public:
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override {}

	virtual bool IsBusy() const override;
	virtual bool ShouldInterruptNavigation() const override;

	void StartFlocking();
	void StopFlocking();

private:
	Vector GetAgentVelDirPsFromSimulation() const;

	CharacterMmLocomotionHandle m_hMmController;
};

AiFlockController* CreateFlockController();
