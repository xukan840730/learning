/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

#include "game/scriptx/h/ai-defines.h"
#include "game/scriptx/h/hit-reactions-defines.h"
#include "game/scriptx/h/performance-defines.h"

static const U8 kMaxPerformanceStages = 3;
static const F32 kExitAnimTrimTime = 0.4f;

/// --------------------------------------------------------------------------------------------------------------- ///
struct PlayPerformanceOptions
{
	union
	{
		struct
		{
			bool m_ignoreMotionClearance	: 1;
			bool m_ignoreDemeanor			: 1;
			bool m_ignoreMotionType			: 1;
			bool m_allowNextQuadrantDir		: 1;
			bool m_allowAllDirs				: 1;
			bool m_requireLos				: 1;
			bool m_requireInPlace			: 1;
			bool m_pointing					: 1;
			bool m_nondirectional			: 1;
		};
		U16 m_flags = 0;
	};
	F32 m_minLosClearDist = -1.0f;

	DC::HitReactionStateMask m_hitReactionState = DC::kHitReactionStateMaskIdle;
	DC::AnimStateFlag m_additionalAnimStateFlags = 0;

	Locator m_apRefWs = Locator(kIdentity);
	bool m_apRefValid = false;

	NdGameObjectHandle m_hTrackGo;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct PlayPerformanceAnimParams
{
	StringId64 m_animId = INVALID_STRING_ID_64;
	BoundFrame m_frame = BoundFrame(kIdentity);
	const DC::PerformanceEntry* m_pDcDef = nullptr;
	StringId64 m_gestureId = INVALID_STRING_ID_64;
	StringId64 m_customApRefId = INVALID_STRING_ID_64;
	const DC::NavAnimHandoffParams* m_pNavHandoff = nullptr;
	bool m_exitToMovingValid = false;
	bool m_exitToMoving = false;
	bool m_mirror = false;
	bool m_allowGestures = true;
	bool m_allowAdditiveHitReact = false;
	float m_blendTime = -1.0f;
	float m_exitPhase = -1.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct IAiPerformanceController : public AnimActionController
{
	typedef AnimActionController ParentClass;

	virtual bool IsPerformancePlaying() const = 0;
	virtual bool PlayPerformance(Point_arg focusPosWs,
								 DC::PerformanceTypeMask performanceType,
								 const PlayPerformanceOptions& options = PlayPerformanceOptions()) = 0;

	virtual bool PlayAnim(const PlayPerformanceAnimParams& params) = 0;

	virtual const DC::PerformanceEntry* GetCurrentPerformance() const = 0;

	virtual bool LockGestures() const = 0;
	virtual TimeFrame GetLastPerformanceStartedTime() const = 0;
	virtual TimeFrame GetLastPerformanceActiveTime() const = 0;
	virtual F32 HackGetAnimFrame(const AnimControl* pAnimControl) const = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiPerformanceController* CreateAiPerformanceController();
