/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/ai/controller/action-pack-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class INdAiCinematicController : public ActionPackController
{
public:
	virtual bool NoAdjustToGround() const = 0;
	virtual float GetNavMeshAdjustBlendFactor() const = 0;
};
