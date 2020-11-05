/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/system/atomic.h"

#include "ndlib/anim/anim-commands.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimControl;
class ArtItemSkeleton;
struct AnimExecutionContext;
struct FgAnimData;

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimDataProcessingData
{
	FgAnimData**	m_ppAnimDataArray;
	U32F			m_numAnimData;
	NdAtomic64		m_cursor;
	F32				m_deltaTime;
	F32				m_yieldTime;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// Used by the jobs themselves
struct AsyncUpdateJobData
{
	AnimControl*	m_pAnimControl;
	FgAnimData*		m_pAnimData;
	float			m_animSysDeltaTime;
	bool			m_gameplayIsPaused;

	AnimPhasePluginCommandHandler*		m_pAnimPhasePluginFunc;
	RigPhasePluginCommandHandler*		m_pRigPhasePluginFunc;

	mutable ndjob::CounterHandle m_pObjectWaitCounter; // Free this in the last job
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimateDisabledObject(FgAnimData* pAnimData, F32 deltaTime);

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT_EXTERN(AsyncObjectUpdate_AnimStep_Job);
JOB_ENTRY_POINT_EXTERN(AsyncObjectUpdate_AnimBlend_Job);
JOB_ENTRY_POINT_EXTERN(AsyncObjectUpdate_JointBlend_Job);
JOB_ENTRY_POINT_EXTERN(AnimateDisabledObjectJob);
