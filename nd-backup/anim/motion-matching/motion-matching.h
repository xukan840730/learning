/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "corelib/containers/list-array.h"

#include "ndlib/util/maybe.h"

#include "gamelib/anim/anim-sample.h"
#include "gamelib/anim/motion-matching/gameplay-goal.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemAnim;
class NdGameObject;

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMLocomotionState
{
	Locator m_alignOs;
	Vector m_velocityOs;
	Vector m_strafeDirOs;
	Point m_pathPosOs;
	float m_yawSpeed;
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum class MMMirrorMode
{
	kInvalid,
	kNone,
	kAllow,
	kForced
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMSearchParams
{
	void AddActiveLayer(StringId64 layerId, float costMod)
	{
		for (U32F i = 0; i < m_numActiveLayers; ++i)
		{
			if (m_activeLayers[i] == layerId)
			{
				m_layerCostModifiers[i] = costMod;
				return;
			}
		}

		if (m_numActiveLayers < kMaxLayers)
		{
			m_activeLayers[m_numActiveLayers] = layerId;
			m_layerCostModifiers[m_numActiveLayers] = costMod;
			++m_numActiveLayers;
		}
	}

	void IncrementActiveLayer(StringId64 layerId, float costMod)
	{
		for (U32F i = 0; i < m_numActiveLayers; ++i)
		{
			if (m_activeLayers[i] == layerId)
			{
				m_layerCostModifiers[i] += costMod;
				return;
			}
		}

		if (m_numActiveLayers < kMaxLayers)
		{
			m_activeLayers[m_numActiveLayers] = layerId;
			m_layerCostModifiers[m_numActiveLayers] = costMod;
			++m_numActiveLayers;
		}
	}

	bool IsLayerActive(StringId64 layerId, float* pLayerCostOut = nullptr) const;

	void AddGoalLocator(StringId64 nameId, const Locator& loc)
	{
		for (U32F i = 0; i < m_numGoalLocators; ++i)
		{
			if (m_goalLocs[i].m_nameId == nameId)
			{
				m_goalLocs[i].m_loc = loc;
				return;
			}
		}

		if (m_numGoalLocators < kMaxGoalLocs)
		{
			m_goalLocs[m_numGoalLocators].m_nameId = nameId;
			m_goalLocs[m_numGoalLocators].m_loc	   = loc;

			++m_numGoalLocators;
		}
	}

	static CONST_EXPR size_t kMaxLayers = 12;
	static CONST_EXPR size_t kMaxGoalLocs = 3;

	AnimGoalLocator m_goalLocs[kMaxGoalLocs];
	StringId64 m_activeLayers[kMaxLayers];
	F32 m_layerCostModifiers[kMaxLayers];
	U32F m_numActiveLayers = 0;
	U32F m_numGoalLocators = 0;
	MMMirrorMode m_mirrorMode = MMMirrorMode::kInvalid;
};

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<MMLocomotionState> ComputeLocomotionStateInFuture(const AnimSample& animSample,
														float timeInFutureSec,
														float stoppingFaceDist);

/// --------------------------------------------------------------------------------------------------------------- ///
struct MotionMatchingProceduralOptions
{
	bool m_drawProceduralMotion = false;

	bool m_enableProceduralAlignRotation	= true;
	bool m_enableProceduralAlignTranslation = true;
	bool m_enableAnimSpeedScaling = true;
	bool m_enableStrafingIk		  = true;
	bool m_disableStrafeLockAngle = false;

	float m_clampDistReductionAlpha = 5.0f;
	float m_clampDistReductionScale = 1.0f;
	float m_lookAheadTimeScale		= -1.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MotionMatchingDrawOptions
{
	I64 m_debugIndex = 0;
	I64 m_numSamplesToDebug = 10;

	StringId64 m_debugExtraAnimId;
	SkeletonId m_debugExtraSkelId;
	I64 m_debugExtraAnimFrame	= -1;
	bool m_debugExtraAnimMirror = false;

	bool m_drawTrajEndPos	= false;
	bool m_drawJointNames	= false;
	bool m_drawTrajectories = true;
	bool m_drawPoses		= true;
	bool m_printPoseValues	= false;

	bool m_drawLocomotion		 = false;
	bool m_drawTrajectorySamples = false;
	bool m_drawMotionModel		 = false;
	bool m_drawMatchLocator		 = false;
	bool m_drawPathCorners		 = false;

	bool m_debugGroupSelection	 = false;
	bool m_useStickForDebugInput = false;
	bool m_printActiveLayers	 = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MotionMatchingOptions
{
	bool m_disablePrecomputedIndices = false;
	bool m_validateEffData		   = false;
	bool m_validateSampleIndices   = false;
	bool m_forceExternalPoseRating = false;
	bool m_disableMmNpcLegIk = false;
	bool m_disableMatchLocSpeedBlending = false;
	bool m_disableMmNavMeshAdjust		= false;
	bool m_disableMmGroundAdjust		= false;
	bool m_forceOffPath = false;
	bool m_disableMmTargetPoseBlending = false;
	bool m_disableHorseReverseMode = true;
	bool m_allowProceduralAlignsFromForeignSets = false;
	float m_targetPoseBlendFactor	   = 1.0f;
	I32 m_trajectorySampleResolution   = 5;
	float m_turnRateResetCosAngle = 0.9f;
	I64 m_overrideMirrorMode = I64(MMMirrorMode::kInvalid);

	bool m_disableRetargetScaling = false;
	bool m_disableTranslationSkewing = false;
	float m_retargetScaleOverride = -1.0f;

	float m_pathCornerGatherAngleDeg = 15.0f;
	bool m_pauseWhenOffPath = false;

	bool m_debugInFinalPlayerState = false;

	MotionMatchingProceduralOptions m_proceduralOptions;
	MotionMatchingDrawOptions m_drawOptions;
};

extern MotionMatchingOptions g_motionMatchingOptions;
