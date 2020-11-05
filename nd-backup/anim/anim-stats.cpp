/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifdef ANIM_STATS_ENABLED

#include "ndlib/anim/anim-stats.h"

void AnimFrameStats::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	MsgCon("-[ Animation Performance Stats ]----------------------------------------------------------------------------------------------------------------------\n");
	MsgCon("   Bucket   |  Animation Updates  |         Objects Animated            |                        Anim Memory Used                                     \n"); 
	MsgCon("            |                     |     -= Pass 0 =-     -= Pass 1 =-   |      -= Retarg =-        -= Pass 0 =-        -= Pass 1 =-        Total      \n"); 
	MsgCon("------------------------------------------------------------------------------------------------------------------------------------------------------\n");

	for (U32 bucket = 0; bucket < AnimMgr::kNumBuckets; ++bucket)
	{
		const AnimBucketStats& animBucketStats = m_bucket[bucket];

		MsgCon("     %d      |    %4d [%4d]        |   %4d [%4d]     %4d [%4d]           |  %4d Kb [%4d Kb]   %4d Kb [%4d Kb]   %4d Kb [%4d Kb]     %5d Kb\n",
			bucket, 
			animBucketStats.m_animUpdates,
			animBucketStats.m_maxAnimUpdates,
			animBucketStats.m_pass[0].m_objects,
			animBucketStats.m_pass[0].m_maxObjects,
			animBucketStats.m_pass[1].m_objects,
			animBucketStats.m_pass[1].m_maxObjects,
			animBucketStats.m_retargetMemUsage / 1024,
			animBucketStats.m_maxRetargetMemUsage / 1024,
			animBucketStats.m_pass[0].m_memUsage / 1024,
			animBucketStats.m_pass[0].m_maxMemUsage / 1024,
			animBucketStats.m_pass[1].m_memUsage / 1024,
			animBucketStats.m_pass[1].m_maxMemUsage / 1024,
			m_totalMemoryHeapSize[0] / 1024); 
	}
	MsgCon("-[ Retargeting Performance Stats ]----------------------------------------------------------------------------------------------------------------------\n");
	MsgCon("   Bucket   |  Num Retargeted Anims  |  Num Retarg. Mementos  |             Memory Used             \n"); 
	MsgCon("            |                        |                        |   -= Batch =-         -= Total =-    \n");
	MsgCon("------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	for (U32 bucket = 0; bucket < AnimMgr::kNumBuckets; ++bucket)
	{
		const AnimBucketStats& animBucketStats = m_bucket[bucket];

		MsgCon("     %d      |    %4d [%4d]         |    %4d [%4d]     |   %4d Kb [%4d Kb]     %4d Kb [%4d Kb]\n",
			bucket,
			animBucketStats.m_numRetargetedAnims,
			animBucketStats.m_maxRetargetedAnims,
			animBucketStats.m_numMementoRetargets,
			animBucketStats.m_maxMementoRetargets,
			animBucketStats.m_retargetBatchMemUsage / 1024,
			animBucketStats.m_maxRetargetBatchMem / 1024,
			animBucketStats.m_retargetMemUsage / 1024,
			animBucketStats.m_maxRetargetMemUsage / 1024);
	}
	MsgCon("----------------------------------------------------------------------------------------------------------------------------------\n");
	MsgCon("Total Animated Joints:    %4d (Max: %4d - DblBuf Mem Needed: %4d Kb)\n", m_numAnimatedJoints, m_maxAnimatedJoints, m_maxAnimatedJoints * sizeof(SMath::Mat34) / 1024); 
	MsgCon("Total Motion Blur Joints: %4d (Max: %4d - DblBuf Mem Needed: %4d Kb)\n", m_numBlurJoints, m_maxBlurJoints, m_maxBlurJoints * sizeof(SMath::Mat34) / 1024); 
	MsgCon("Total Motion Blur Xforms: %4d (Max: %4d - DblBuf Mem Needed: %4d Kb)\n", m_numBlurMatrices, m_maxBlurMatrices, m_maxBlurMatrices * sizeof(SMath::Mat44) / 1024); 
	MsgCon("Mementos Used:            %4d\n", m_numUsedMementos); 
}

AnimFrameStats g_animFrameStats;

#endif // ANIM_STATS_ENABLED

#ifdef _MSC_VER
void _LNK4221_avoidance_anim_stats() {}
#endif
