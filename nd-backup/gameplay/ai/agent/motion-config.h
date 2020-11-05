/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

/// --------------------------------------------------------------------------------------------------------------- ///
struct MotionConfig
{
	enum class UprightMode
	{
		kWorldSpace,
		kParentSpace,
	};

	float m_ignoreMoveDistance = 0.25f;	// If asked to move a distance less than this it will be ignored
	float m_minimumGoalRadius  = 0.25f;	// goal radius is never less than this

	// used by NavStateMachine::UpdateCurrentPath() to determine how much we need to steer to get back on the 
	// path vs. just moving in the same direction as the path
	float m_pathRejoinDist = 0.5f; 

	// turn this on to have an NPC continue their previous move command if you tell them to go somewhere but the 
	// pathfind fails
	bool m_dontStopOnFailedMoves = false;

	UprightMode m_adjustUprightMode = UprightMode::kWorldSpace;
};
