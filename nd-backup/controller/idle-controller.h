/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef AI_IDLE_CONTROLLER_H
#define AI_IDLE_CONTROLLER_H

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

struct IAiIdleController : public AnimActionController
{
	virtual bool PlayPerformance(StringId64 animId) = 0;
};

IAiIdleController* CreateAiIdleController();

#endif // AI_IDLE_CONTROLLER_H
