/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class IAiDodgeController : public AnimActionController
{
public:
	virtual bool RequestDodge(Point_arg sourcePosWs, Vector_arg aimDirWs = Vector(kZero), bool force = false) = 0;
	virtual bool RequestDive(Point_arg sourcePos, Vector_arg attackDir = Vector(kZero), bool force = false) = 0;
	virtual TimeFrame GetLastDodgedTime() const = 0;
	virtual TimeFrame GetLastDiveTime() const = 0;
	virtual void EnableDodges(bool enabled) = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiDodgeController* CreateAiDodgeController();
