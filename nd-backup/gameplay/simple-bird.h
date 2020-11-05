/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "ndlib/util/maybe.h"
#include "ndlib/util/tracker.h"

#include "gamelib/scriptx/h/animal-behavior-defines.h"

struct SimpleBirdDebugOptions
{
	SimpleBirdDebugOptions()
		: m_showName(false)
		, m_showStateName(false)
		, m_showAnimName(false)
		, m_showSpeed(false)
		, m_showPitchAngle(false)
		, m_showFlyPath(false)
		, m_showThreats(false)
	{}

	bool m_showName;
	bool m_showStateName;
	bool m_showAnimName;
	bool m_showSpeed;
	bool m_showPitchAngle;
	bool m_showFlyPath;
	bool m_showThreats;
	bool m_shortIdleTime = false;
};
extern SimpleBirdDebugOptions g_simpleBirdDebugOptions;

