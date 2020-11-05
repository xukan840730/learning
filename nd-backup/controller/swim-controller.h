/*
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/ai/controller/nav-anim-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class IAiSwimController : public INavAnimController
{
};

IAiSwimController* CreateAiSwimController();
