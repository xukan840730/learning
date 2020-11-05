/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIMSTAT_H
#define ANIMSTAT_H

#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/resource/resource-table.h"

class ArtItemAnim;
class ArtItemSkeleton;
struct redisContext;

class AnimStat
{
public:
	AnimStat();
	
	void Init(const char* redisHost, int redisPort);
	void Shutdown();
	void SubmitPlayCount(SkeletonId skelId, StringId64 nameId, U32 count);
	void SubmitSkeleton(SkeletonId skelId);
	void SubmitToRedis();

	struct AnimObserver : public ResourceObserver
	{
		virtual void PostAnimationLogin(const ArtItemAnim* pArtItemAnim) override;
	};

	struct SkelObserver : public ResourceObserver
	{
		virtual void PostSkeletonLogin(const ArtItemSkeleton* pSkel) override;
	};

private:
	bool IsInitialized();
	void SubmitToRedisInternal();
	void SubmitSkeletonInternal(SkeletonId skelId);
	
	redisContext* m_pRedisContext;

	char m_discName[64];
	char m_incrementScriptId[128];

	struct PlayCount
	{
		StringId64 m_nameId;
		SkeletonId m_skelId;
		U32 m_count;
	};

	static const U32 kMaxStats = 256;
	PlayCount* m_pStats;
	U32 m_statCount;

	static const U32 kMaxSkels = 64;
	SkeletonId* m_pSkels;
	U32 m_skelCount;
	U32 m_lastSkelCount;

	NdAtomic64 m_lock;

	static const U32 kMaxCmdArgs = (kMaxStats + 1) * 2 + 3; // name and count for each anim plus evalsha, script id, and arg count
	static const U32 kMaxArgLength = 64;
	char** m_pCmdArgs;
};

extern AnimStat g_animStat;

#endif // ANIMSTAT_H
