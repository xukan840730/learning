/*
 * Copyright (c) 2008 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef AI_INVESTIGATE_CONTROLLER_H
#define AI_INVESTIGATE_CONTROLLER_H

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct IAiInvestigateController : public AnimActionController
{
	virtual bool IsPlayingInvestigateAnimation() const = 0;
	virtual bool RequestLookAround() = 0;
	virtual bool RequestLookAtDistantPoint() = 0;
	virtual bool RequestLookAroundShort() = 0;
	virtual bool RequestLookAroundDeadBody() = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiInvestigateController* CreateAiInvestigateController();

#endif

