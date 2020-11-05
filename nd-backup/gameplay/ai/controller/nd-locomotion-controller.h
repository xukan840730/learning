/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/component/move-performance.h"
#include "gamelib/gameplay/ai/controller/nav-anim-controller.h"
#include "gamelib/gameplay/ai/nav-character-anim-defines.h"
#include "gamelib/gameplay/nav/nav-command.h"

namespace DC
{
	struct NpcMoveSetDef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class CustomLocoStartAnim
{
public:
	CustomLocoStartAnim()
	: m_valid(false)
	, m_animId(INVALID_STRING_ID_64)
	, m_apRef(kIdentity)
	, m_sbApRef(kIdentity)
	{
		m_selfBlend.m_phase = -1.0f;
		m_selfBlend.m_time = -1.0f;
		m_selfBlend.m_curve = DC::kAnimCurveTypeLinear;
	}

	bool m_valid;
	StringId64 m_animId;
	BoundFrame m_apRef;
	BoundFrame m_sbApRef;
	DC::SelfBlendParams m_selfBlend;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class INdAiLocomotionController : public INavAnimController
{
public:
	static const float CONST_EXPR kMinTurnInPlaceAngleDeg = 45.0f;

	/// --------------------------------------------------------------------------------------------------------------- ///
	enum MoveDir
	{
		kMoveDirFw,
		kMoveDirLt,
		kMoveDirRt,
		kMoveDirBw,
		kMoveDirMax,
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	static const char* GetMoveDirName(const MoveDir md)
	{
		switch (md)
		{
		case kMoveDirFw:	return "Fw";
		case kMoveDirLt:	return "Lt";
		case kMoveDirRt:	return "Rt";
		case kMoveDirBw:	return "Bw";
		}

		return "??";
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static MoveDir AngleToMoveDir(const float angleDeg)
	{
		MoveDir d = kMoveDirFw;

		if (Abs(angleDeg) <= 45.0f)
			d = kMoveDirFw;
		else if (Abs(angleDeg) > 135.0f)
			d = kMoveDirBw;
		else if (angleDeg < -45.0f)
			d = kMoveDirRt;
		else if (angleDeg > 45.0f)
			d = kMoveDirLt;

		return d;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static MoveDir FaceDiffToStrafeDir(const float angleDeg)
	{
		return AngleToMoveDir(-1.0f * angleDeg);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	INdAiLocomotionController();

	virtual MotionType GetRequestedMotionType() const = 0;
	virtual MotionType GetCurrentMotionType() const = 0;
	virtual StringId64 GetRequestedMtSubcategory() const = 0;
	virtual StringId64 GetCurrentMtSubcategory() const = 0;
	virtual const DC::NpcMoveSetDef* GetCurrentMoveSet() const = 0;

	// Selects the animation set to use for locomotion
	virtual void RequestDemeanor(Demeanor demeanor, AI_LOG_PARAM) = 0;
	virtual Demeanor GetRequestedDemeanor() const = 0;
	virtual void BypassDemeanor(Demeanor demeanor) = 0;

	virtual bool AllowDialogLookGestures() const = 0;
	virtual bool IsDoingMovePerformance() const = 0;
	virtual StringId64 GetMovePerformanceId() const = 0;

	virtual MotionType RestrictMotionType(const MotionType desiredMotionType, bool wantToStrafe) const = 0;

	virtual bool MovePerformanceAllowed(MovePerformance::FindParams* pParamsOut) const = 0;
	virtual bool StartMovePerformance(MovePerformance::Performance& performance) = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
const char* GetMotionTypeName(MotionType mt, bool allowNull = false);
const char* GetGoalReachedTypeName(NavGoalReachedType goalReachedType, bool allowNull = false);
const char* GetGunStateName(GunState dem, bool allowNull = false);
