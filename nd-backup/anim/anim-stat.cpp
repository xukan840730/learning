/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-stat.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/netbridge/redis-conf.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"

#include "hiredis/hiredis.h"

AnimStat g_animStat;
AnimStat::AnimObserver g_animStatAnimObserver;
AnimStat::SkelObserver g_animStatSkelObserver;

AnimStat::AnimStat()
{
	m_pRedisContext = nullptr;
	m_pStats = nullptr;
	m_pSkels = nullptr;
	m_pCmdArgs = nullptr;
}

void AnimStat::Init(const char* redisHost, int redisPort)
{
	STRIP_IN_FINAL_BUILD;

	m_lock.Set(0);

	if (EngineComponents::GetNdGameInfo()->m_discId == INVALID_STRING_ID_64)
		sprintf(m_discName, "%s", EngineComponents::GetNdGameInfo()->m_gameName);
	else
		sprintf(m_discName, "%s.%s", EngineComponents::GetNdGameInfo()->m_gameName, EngineComponents::GetNdGameInfo()->m_discName);

	// One second timeout
	if (!g_ndConfig.m_pRedisConf->m_disable && !EngineComponents::GetNdGameInfo()->m_localDevkit)
	{
		m_pRedisContext = (redisContext *)redisConnectWithTimeout(redisHost, redisPort, 1000);
		if (m_pRedisContext->err != REDIS_OK)
		{
			redisFree(m_pRedisContext);
			m_pRedisContext = nullptr;
		}
		else
		{
			redisReply* pReply = nullptr;

			char redisCmd[256];
			sprintf(redisCmd, "SADD discs %s", m_discName);
			pReply = (redisReply*)redisCommand(m_pRedisContext, redisCmd);
			freeReplyObject(pReply);

			/*
				LUA script for batch processing play count stats.  The first KEYS is the set name, the first ARGV is
				the number of stats to follow.  All following KEYS are animation names and all following ARGVs are counts.

				for iStat = 2, tonumber(ARGV[1]) + 2, 1 do
					redis.call('ZINCRBY', KEYS[1], tonumber(ARGV[iStat]), KEYS[iStat])
				end
				return tonumber(ARGV[1])
			*/
			pReply = (redisReply*)redisCommand(m_pRedisContext, "script load %s", 
				"for iStat = 2, tonumber(ARGV[1]) + 2, 1 do redis.call('ZINCRBY', KEYS[1], tonumber(ARGV[iStat]), KEYS[iStat]) end return tonumber(ARGV[1])");
			
			if (pReply && pReply->type == REDIS_REPLY_STRING)
			{
				memcpy(m_incrementScriptId, pReply->str, strlen(pReply->str));
			}
			else
			{
				redisFree(m_pRedisContext);
				m_pRedisContext = nullptr;
			}

			freeReplyObject(pReply);
		}
	}
	else
	{
		m_pRedisContext = nullptr;
	}

	if (Memory::IsDebugMemoryAvailable())
	{
		AllocateJanitor alloc(kAllocDebug, FILE_LINE_FUNC);

		m_statCount = 0;
		m_pStats = NDI_NEW PlayCount[kMaxStats];

		m_skelCount = 0;
		m_lastSkelCount = 0;
		m_pSkels = NDI_NEW SkeletonId[kMaxStats];

		m_pCmdArgs = NDI_NEW char*[kMaxCmdArgs];
		char* pMem = NDI_NEW char[kMaxCmdArgs * kMaxArgLength];
		for (I32F iArg = 0; iArg < kMaxCmdArgs; ++iArg)
			m_pCmdArgs[iArg] = pMem + iArg * kMaxArgLength;

		//ResourceTable::RegisterObserver(&g_animStatAnimObserver);
		//ResourceTable::RegisterObserver(&g_animStatSkelObserver);
	}
	else
	{
		m_statCount = 0;
		m_pStats = nullptr;

		m_skelCount = 0;
		m_lastSkelCount = 0;
		m_pSkels = nullptr;

		m_pCmdArgs = nullptr;
	}
}

void AnimStat::Shutdown()
{
	STRIP_IN_FINAL_BUILD;

	if (m_pRedisContext)
	{
		redisFree(m_pRedisContext);
		m_pRedisContext = nullptr;
	}

	if (m_pCmdArgs)
	{
		for (I32F iArg = 0; iArg < kMaxCmdArgs; ++iArg)
			NDI_DELETE[] m_pCmdArgs[iArg];
		NDI_DELETE[] m_pCmdArgs;
		m_pCmdArgs = nullptr;
	}
}

bool AnimStat::IsInitialized()
{
	return m_pRedisContext != nullptr;
}

void AnimStat::SubmitPlayCount(SkeletonId skelId, StringId64 nameId, U32 count)
{
	STRIP_IN_FINAL_BUILD;
	PROFILE(Redis, AnimStat_SubmitPlayCount);

	if (!g_animOptions.m_postPlayStats || !IsInitialized())
		return;
	
	if (skelId == INVALID_SKELETON_ID || nameId == INVALID_STRING_ID_64)
		return;

	if (!m_pStats || !m_pSkels)
		return;

	// If the skeleton hasn't logged in yet, skip the animation and we'll post it when the skeleton logs in
	const char* pSkelName = ResourceTable::LookupSkelName(skelId);
	if (!pSkelName)
		return;

	NdAtomic64Janitor janitor(&m_lock);

	bool found = false;
	for (I32F iCount = 0; iCount < m_statCount; ++iCount)
	{
		if (m_pStats[iCount].m_skelId == skelId && m_pStats[iCount].m_nameId == nameId)
		{
			m_pStats[iCount].m_count += count;
			found = true;
			break;
		}
	}

	if (!found)
	{
		m_pStats[m_statCount].m_skelId = skelId;
		m_pStats[m_statCount].m_nameId = nameId;
		m_pStats[m_statCount].m_count = count;
		m_statCount++;
	}

	SubmitSkeletonInternal(skelId);

	if (m_statCount >= kMaxStats || m_skelCount >= kMaxSkels)
		SubmitToRedisInternal();

	ANIM_ASSERT(m_statCount < kMaxStats);
}

void AnimStat::SubmitSkeleton(SkeletonId skelId)
{
	STRIP_IN_FINAL_BUILD;

	if (!g_animOptions.m_postPlayStats || skelId == INVALID_SKELETON_ID || !m_pSkels || !IsInitialized())
		return;

	NdAtomic64Janitor janitor(&m_lock);

	SubmitSkeletonInternal(skelId);
}

void AnimStat::SubmitSkeletonInternal(SkeletonId skelId)
{
	STRIP_IN_FINAL_BUILD;
	PROFILE(Redis, AnimStat_SubmitSkeleton);

	bool found = false;
	for (I32F iSkel = 0; iSkel < m_skelCount; ++iSkel)
	{
		if (m_pSkels[iSkel] == skelId)
		{
			found = true;
			break;
		}
	}

	if (!found)
	{
		m_pSkels[m_skelCount] = skelId;
		++m_skelCount;
	}

	// If we fill up the skeleton table we've probably been playing through multiple levels and have skeletons
	// in the table that are no longer logged in.  Send all the stats, then clear the table.  We'll end up
	// sending extra SADD skeleton packets but it won't be frequent enough to impact performance.
	if (m_skelCount >= kMaxSkels)
	{ 
		SubmitToRedisInternal();
		m_skelCount = 0;
		m_lastSkelCount = 0;
	}
		

	ANIM_ASSERT(m_skelCount < kMaxSkels);
}

void AnimStat::SubmitToRedis()
{
	STRIP_IN_FINAL_BUILD;

	if (!g_animOptions.m_postPlayStats || !IsInitialized())
		return;

	if (!m_pStats || !m_pSkels)
		return;

	NdAtomic64Janitor janitor(&m_lock);
	SubmitToRedisInternal();
}

void AnimStat::SubmitToRedisInternal()
{
	return;
	STRIP_IN_FINAL_BUILD;
	PROFILE(Redis, AnimStat_SubmitToRedis);

	int retVal = REDIS_OK;

	int commandCount = 0;
	if (m_statCount > 0)
	{
		I32F numSkels = 0;
		SkeletonId skels[kMaxSkels];
		I32 animsPerSkel[kMaxSkels];

		// Split up animation stats by skeleton, we'll send one script packet per skeleton
		for (I32F iCount = 0; iCount < m_statCount; ++iCount)
		{
			bool foundSkel = false;
			for (I32F iSkel = 0; iSkel < numSkels; ++iSkel)
			{
				if (skels[iSkel] == m_pStats[iCount].m_skelId)
				{
					foundSkel = true;
					animsPerSkel[iSkel]++;
					break;
				}
			}

			if (!foundSkel)
			{
				skels[numSkels] = m_pStats[iCount].m_skelId;
				animsPerSkel[numSkels] = 1;
				numSkels++;
			}
		}

		AllocateJanitor alloc(kAllocDebug, FILE_LINE_FUNC);

		for (I32F iSkel = 0; iSkel < numSkels; ++iSkel)
		{
			SkeletonId skelId = skels[iSkel];

			const char* pSkelName = ResourceTable::LookupSkelName(skelId);
			if (!pSkelName)
				continue;

			I32 numAnims = animsPerSkel[iSkel];

			I32 index = 0;

			snprintf(m_pCmdArgs[index++], kMaxArgLength, "EVALSHA");
			snprintf(m_pCmdArgs[index++], kMaxArgLength, "%s", m_incrementScriptId);
			snprintf(m_pCmdArgs[index++], kMaxArgLength, "%i", numAnims + 1);

			I32 keysIndex = index;
			I32 argvIndex = index + numAnims + 1;

			snprintf(m_pCmdArgs[keysIndex++], kMaxArgLength, "%s.%s.animations", m_discName, pSkelName);
			snprintf(m_pCmdArgs[argvIndex++], kMaxArgLength, "%i", numAnims);
			index += 2;

			for (I32F iCount = 0; iCount < m_statCount; ++iCount)
			{
				if (m_pStats[iCount].m_skelId == skelId)
				{
					const char* pAnimName = DevKitOnly_StringIdToString(m_pStats[iCount].m_nameId);
					if (!pAnimName)
						continue;

					snprintf(m_pCmdArgs[keysIndex++], kMaxArgLength, "%s", pAnimName);
					snprintf(m_pCmdArgs[argvIndex++], kMaxArgLength, "%i", m_pStats[iCount].m_count);
					index += 2;
				}
			}

			retVal = redisAppendCommandArgv(m_pRedisContext, index, const_cast<const char**>(m_pCmdArgs), nullptr);
			ANIM_ASSERTF(retVal == REDIS_OK, ("Error appending command, retval = %d", retVal));
			commandCount++;
		}
	}

	// add any new skeletons
	for (I32F iSkel = m_lastSkelCount; iSkel < m_skelCount; ++iSkel)
	{
		const char* pSkelName = ResourceTable::LookupSkelName(m_pSkels[iSkel]);
		retVal = redisAppendCommand(m_pRedisContext, "SADD %s.skeletons %s", m_discName, pSkelName);
		ANIM_ASSERTF(retVal == REDIS_OK, ("Error appending command, SADD %s.skeletons %s, retval = %d", m_discName, pSkelName, retVal));
		commandCount++;
	}

	// flush the output buffer to send all the commands to the redis server
	if (m_statCount > 0 || m_lastSkelCount < m_skelCount)
	{
		while (commandCount--)
		{
			redisReply* pReply = nullptr;
			ANIM_ASSERTF(retVal == REDIS_OK, ("Error getting reply from SADD, retval = %d", retVal));
			retVal = redisGetReply(m_pRedisContext, (void**)&pReply);
			if (pReply)
			{
				freeReplyObject(pReply);
			}
		}
	}

	m_lastSkelCount = m_skelCount;
	m_statCount = 0;
}

void AnimStat::AnimObserver::PostAnimationLogin(const ArtItemAnim* pArtItemAnim)
{
	STRIP_IN_FINAL_BUILD;

	if (pArtItemAnim)
	{
		g_animStat.SubmitPlayCount(pArtItemAnim->m_skelID, pArtItemAnim->GetNameId(), 0);
	}
}

void PostSkeletonLoginAnimFucntor(const ArtItemAnim* pAnim, uintptr_t data)
{
	g_animStat.SubmitPlayCount(pAnim->m_skelID, pAnim->GetNameId(), 0);
}

void AnimStat::SkelObserver::PostSkeletonLogin(const ArtItemSkeleton* pSkel)
{
	STRIP_IN_FINAL_BUILD;

	if (pSkel && (!EngineComponents::GetNdGameInfo() || !EngineComponents::GetNdGameInfo()->m_transitionAntiStutterMode))
	{
		SkeletonId skelId = pSkel->m_skelId;
		g_animStat.SubmitSkeleton(skelId);

		// Post all the animations that logged in before the skeleton logged in
		ResourceTable::ForEachAnimation(PostSkeletonLoginAnimFucntor, skelId, 0);
	}
}
