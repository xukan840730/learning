/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef ND_CLIMB_CONTROLLER_H
#define ND_CLIMB_CONTROLLER_H

#if ENABLE_NAV_LEDGES

#include "gamelib/gameplay/ai/controller/nav-anim-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class INdAiClimbController : public INavAnimController
{
public:
	virtual bool IsClimbing() const { return IsMoving(); }
	virtual bool IsInWallShimmy() const { return false; }
	virtual bool IsHanging() const { return false; }
};

#endif // ENABLE_NAV_LEDGES

#endif	// ND_CLIMB_CONTROLLER_H