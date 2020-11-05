/*
* Copyright (c) 2017 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "gamelib/anim/motion-matching/motion-matching.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionMatchingDebug
{
public:
	static void DrawFuturePose(const AnimSample& curPose,
							   const Locator& refLoc,
							   Color32 c,
							   float timeInFutureSec,
							   float lineWidth = 3.0f);

	static void DebugDrawFullAnimPose(const AnimSample& animSample,
									  const Locator& alignLoc,
									  Color32 c,
									  float lineWidth = 3.0f);

	static void DrawTrajectory(const AnimSample& animSample,
							   const Locator& refLoc,
							   Color32 c,
							   int trajSamples,
							   float trajTime,
							   float stoppingFaceDist);
};
