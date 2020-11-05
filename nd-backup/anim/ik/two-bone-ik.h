/*
 * Copyright (c) 2019 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

class JointSet;

/// --------------------------------------------------------------------------------------------------------------- ///
struct TwoBoneIkParams
{
	JointSet* m_pJointSet	= nullptr;
	I32 m_jointOffsets[3]	= { -1 };
	Point m_goalPos			= kInvalidPoint;
	Quat m_finalGoalRot		= kIdentity;
	float m_tt = -1.0f;
	float m_stretchLimit = -1.0f;
	bool m_objectSpace		= false;
	bool m_abortIfCantSolve = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct TwoBoneIkResults
{
	bool m_valid = false;
	Point m_outputGoalPos = kInvalidPoint;
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool SolveTwoBoneIK(const TwoBoneIkParams& params, TwoBoneIkResults& results);
