/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

class IAiEvadeController : public AnimActionController
{
public:
	virtual void EnableEvade(bool enable) = 0;
	virtual bool IsEvadeEnabled() const	  = 0;
	virtual bool HasEvadeAnims() const	  = 0;
	virtual bool IsEvading() const		  = 0;

	virtual void RequestEvadeSkillOverride() = 0;
};

IAiEvadeController* CreateAiEvadeController();
