/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */


#include "gamelib/gameplay/ai/controller/nd-locomotion-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const char* GetMotionTypeName(MotionType mt, bool allowNull /* = false */)
{
	switch (mt)
	{
	case kMotionTypeWalk:		return "walk";
	case kMotionTypeRun:		return "run";
	case kMotionTypeSprint:		return "sprint";	
	}

	if (allowNull)
	{
		return nullptr;
	}
	else
	{
		return "<invalid>";
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* GetGoalReachedTypeName(NavGoalReachedType goalReachedType, bool allowNull /* = false */)
{
	switch (goalReachedType)
	{
	case kNavGoalReachedTypeStop:		return "stop";
	case kNavGoalReachedTypeContinue:	return "continue";
	}

	if (allowNull)
	{
		return nullptr;
	}
	else
	{
		return "<invalid>";
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* GetGunStateName(GunState gs, bool allowNull /* = false */)
{
	switch (gs)
	{
	case kGunStateHolstered:	return "GunHolstered";
	case kGunStateOut:			return "GunOut";
	}

	if (allowNull)
	{
		return nullptr;
	}
	else
	{
		return "<invalid>";
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
INdAiLocomotionController::INdAiLocomotionController()
{
};
