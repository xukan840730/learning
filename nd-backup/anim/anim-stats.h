/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIM_STATS_H
#define ANIM_STATS_H

#ifdef ANIM_STATS_ENABLED

#include "ndlib/anim/anim-mgr.h"

struct AnimPassStats
{
	U32 m_objects;
	U32 m_memUsage;
	U32 m_maxObjects;
	U32 m_maxMemUsage;

	AnimPassStats()
	{
		memset(this, 0, sizeof(*this));
	}
};

struct AnimBucketStats
{
	U32 m_animUpdates;
	U32 m_maxAnimUpdates;

	U32 m_retargetMemUsage;
	U32 m_maxRetargetMemUsage;

	U32 m_retargetBatchMemUsage;
	U32 m_maxRetargetBatchMem;

	U32 m_numRetargetedAnims;
	U32 m_maxRetargetedAnims;

	U32 m_numMementoRetargets;
	U32 m_maxMementoRetargets;

	AnimPassStats m_pass[2];

	AnimBucketStats()
	{
		memset(this, 0, sizeof(*this));
	}
};

struct AnimFrameStats
{
	AnimBucketStats m_bucket[AnimMgr::kNumBuckets];
	U32 m_numUsedMementos;
	U32 m_numAnimatedJoints;
	U32 m_maxAnimatedJoints;
	U32 m_numBlurJoints;
	U32 m_maxBlurJoints;
	U32 m_numBlurMatrices;
	U32 m_maxBlurMatrices;
	U32 m_totalMemoryHeapSize[2];
	U32 m_curBucket;

	AnimFrameStats()
	{
		memset(this, 0, sizeof(*this));
	}

	void ResetStats()
	{
		for (U64 i = 0; i < AnimMgr::kNumBuckets; ++i)
		{
			AnimBucketStats& animBucketStats = m_bucket[i];

			// Update 'max' stats
			animBucketStats.m_maxAnimUpdates = Max(animBucketStats.m_maxAnimUpdates, animBucketStats.m_animUpdates);
			animBucketStats.m_pass[0].m_maxObjects	= Max(animBucketStats.m_pass[0].m_maxObjects, animBucketStats.m_pass[0].m_objects);
			animBucketStats.m_pass[0].m_maxMemUsage	= Max(animBucketStats.m_pass[0].m_maxMemUsage, animBucketStats.m_pass[0].m_memUsage);
			animBucketStats.m_pass[1].m_maxObjects	= Max(animBucketStats.m_pass[1].m_maxObjects, animBucketStats.m_pass[1].m_objects);
			animBucketStats.m_pass[1].m_maxMemUsage	= Max(animBucketStats.m_pass[1].m_maxMemUsage, animBucketStats.m_pass[1].m_memUsage);

			animBucketStats.m_maxRetargetMemUsage	= Max(animBucketStats.m_maxRetargetMemUsage, animBucketStats.m_retargetMemUsage);
			animBucketStats.m_maxRetargetBatchMem	= Max(animBucketStats.m_maxRetargetBatchMem, animBucketStats.m_retargetBatchMemUsage);
			animBucketStats.m_maxRetargetedAnims	= Max(animBucketStats.m_maxRetargetedAnims, animBucketStats.m_numRetargetedAnims);
			animBucketStats.m_maxMementoRetargets	= Max(animBucketStats.m_maxMementoRetargets, animBucketStats.m_numMementoRetargets);


			// Now clear 'per frame' stats
			animBucketStats.m_animUpdates = 0;
			animBucketStats.m_pass[0].m_objects = 0;
			animBucketStats.m_pass[0].m_memUsage = 0;
			animBucketStats.m_pass[1].m_objects = 0;
			animBucketStats.m_pass[1].m_memUsage = 0;

			animBucketStats.m_retargetMemUsage = 0;
			animBucketStats.m_retargetBatchMemUsage = 0;
			animBucketStats.m_numRetargetedAnims = 0;
			animBucketStats.m_numMementoRetargets = 0;
		}

		m_maxAnimatedJoints = Max(m_maxAnimatedJoints, m_numAnimatedJoints);
		m_maxBlurJoints = Max(m_maxBlurJoints, m_numBlurJoints);
		m_maxBlurMatrices = Max(m_maxBlurMatrices, m_numBlurMatrices);
		m_numAnimatedJoints = 0;
		m_numBlurJoints = 0;
		m_numBlurMatrices = 0; 
	}

	void DebugDraw() const;
};

extern AnimFrameStats g_animFrameStats;

#endif // ANIM_STATS_ENABLED

#endif // ANIM_STATS_H