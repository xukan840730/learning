/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/level/art-item.h"

/// --------------------------------------------------------------------------------------------------------------- ///
// Data structures written out by the tools
// Do not change these unless you also make changes to
// tools\src\tools\pipeline3\buildactor3\build-transforms\build-transform-motion-matching.cpp

#define kMotionMatchVerion 16

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMAnimVectorTableRaw
{
	F32**			m_apChunks;
	I32				m_vectorsPerChunk;
	I32				m_numVectors;
	I32				m_numDimensions;
};

/// --------------------------------------------------------------------------------------------------------------- ///
using MMAnimIndex	= U16;
using MMSampleIndex = U16;

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMCAnimSample
{
	MMAnimIndex m_animIndex;
	MMSampleIndex m_sampleIndex;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMAnimSampleRange
{
	I32 m_startIndex;
	I32 m_count;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMAnimSampleTable
{
	StringId64*			m_aAnimIds; // Sorted [m_numAnims]
	MMCAnimSample*		m_aSamples; // Sorted [m_numSamples]
	MMAnimSampleRange*	m_aAnimRanges; // [m_numAnims]
	I32					m_numAnims;
	I32					m_numSamples;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMPoseBody
{
	StringId64 m_jointId;
	bool m_isCenterOfMass;
	F32 m_positionWeight;
	F32 m_velocityWeight;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMPose
{
	StringId64	m_facingJointId;
	MMPoseBody* m_aBodies;
	Vector		m_facingAxisLs;
	F32			m_facingWeight;
	F32			m_masterWeight;
	I32			m_numBodies;
	U8			m_padding[4];
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMGoalLocator
{
	StringId64 m_locatorId;
	F32 m_goalLocWeight;
	F32 m_minGoalDist;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMGoals
{
	F32 m_maxTrajSampleTime;
	I32 m_numTrajSamples;

	F32 m_masterWeight;
	F32 m_positionWeight;

	F32 m_velocityWeight;
	F32 m_directionalWeight;
	F32 m_interimDirectionalWeight;
	F32 m_yawSpeedWeight;

	F32 m_animBiasWeight;
	F32 m_groupingWeight;

	F32 m_maxTrajSampleTimePrevTraj;
	I32 m_numTrajSamplesPrevTraj;

	F32 m_prevTrajWeight;
	F32 m_stoppingFaceDist;

	MMGoalLocator* m_aGoalLocators;

	U32 m_numGoalLocators;
	U8 m_pad[4];
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMSettings
{
	MMPose	m_pose;
	MMGoals m_goals;
	bool	m_useNormalizedScales;
	bool	m_useNormalizedData;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMClusterExtents
{
	I32 m_start;
	I32 m_end;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMDistanceIndex
{
	F32 m_dist;
	I32 m_index;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MMIndex
{
	StringId64 m_layerId;
	MMAnimVectorTableRaw* m_pMeansTable;
	MMClusterExtents* m_aExtents;
	MMDistanceIndex* m_aDistances;
	F32* m_metricScaleVector;
	F32* m_metricMinimunVector;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MotionMatchingSetDef
{
	StringId64				m_sidMotionMatch;	// Should be SID("MotionMatch")
	I32						m_version;			// Should be kMotionMatchVerion
	SkeletonId				m_skelId;
	U32						m_hierarchyId;
	I32						m_numDimensions;
	MMAnimVectorTableRaw*	m_pVectorTable;
	U8*						m_pLayerIndexTable;
	MMAnimSampleTable*		m_sampleTable;
	MMSettings*				m_pSettings;
	F32*					m_autoWeights;
	MMIndex*				m_aIndices;
	StringId64*				m_aLayerIds;
	I32						m_numIndices;
	U32						m_numLayers;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemMotionMatchingSet : public ArtItem
{
public:
	MotionMatchingSetDef m_set;
};
