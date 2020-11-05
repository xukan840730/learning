/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nav/nav-job-scheduler.h"

#include "corelib/util/bigsort.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/character-manager.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nav/nav-state-machine.h"
#include "ndlib/nd-frame-state.h"

NavJobScheduler NavJobScheduler::sm_manager;

/// --------------------------------------------------------------------------------------------------------------- ///
NavJobScheduler::NavJobScheduler()
	: m_accessLock()
{
	memset(m_defaultQueue, 0, sizeof(Entry)*kMaxEntries);
	memset(m_wantToMoveQueue, 0, sizeof(Entry)*kMaxEntries);
	
	m_frameIndex = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavJobScheduler::Update()
{
	if (EngineComponents::GetNdFrameState()->m_clock[kGameClock].IsPaused())
		return;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavJobScheduler::PostRenderUpdate()
{
	if (EngineComponents::GetNdFrameState()->m_clock[kGameClock].IsPaused())
		return;

	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	const I64 frameNum = m_frameIndex;
	const bool useDefault = ((frameNum & 0x3) == 0) || !m_wantToMoveQueue[0].m_hNavChar.Assigned();

	if (useDefault) // If handle goes invalid, we still need to dequeue to keep the queue moving
	{
		Dequeue(m_defaultQueue);
	}
	else
	{
		Dequeue(m_wantToMoveQueue);
	}

	if (IsQueueEmpty(m_defaultQueue))
	{
		RefillQueues();
	}

	++m_frameIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavJobScheduler::NotifyWantToMove(const NavCharacter* pNavChar)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	for (int i = 0; i < kMaxEntries; i++)
	{
		if (m_wantToMoveQueue[i].m_hNavChar.ToProcess() == pNavChar)
			return;

		if (!m_wantToMoveQueue[i].m_hNavChar.HandleValid()) 
		{
			m_wantToMoveQueue[i].m_hNavChar = pNavChar;
			return;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavJobScheduler::CanRunThisFrame(const NavCharacter* pNavChar) const
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	const I64 frameNum = m_frameIndex;
	const bool useDefault = ((frameNum & 0x3) == 0) || !m_wantToMoveQueue[0].m_hNavChar.HandleValid();

	NavCharacterHandle hNavChar = pNavChar;

	if (useDefault && m_defaultQueue[0].m_hNavChar.HandleValid())
	{
		return m_defaultQueue[0].m_hNavChar == hNavChar;
	}

	return m_wantToMoveQueue[0].m_hNavChar == hNavChar;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavJobScheduler::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (!g_navCharOptions.m_drawNavJobQueue)
		return;

	const I64 frameNum = m_frameIndex;
	U32F movingIndex = 0;
	U32F stoppedIndex = 0;

	MsgCon("-------------------------------\n");
	MsgCon(" Nav Job Queue [%d]:\n", int(frameNum));

	for (U32F i = 0; i < kMaxEntries*2; ++i)
	{
		const bool useDefault = ((frameNum + i) & 0x3) == 0;

		if (useDefault && m_defaultQueue[stoppedIndex].m_hNavChar.HandleValid())
		{
			MsgCon("  [D] %s\n", m_defaultQueue[stoppedIndex].m_hNavChar.ToProcess()->GetName());
			++stoppedIndex;
		}
		else if (m_wantToMoveQueue[movingIndex].m_hNavChar.HandleValid())
		{
			MsgCon("  [W] %s\n", m_wantToMoveQueue[movingIndex].m_hNavChar.ToProcess()->GetName());
			++movingIndex;
		}
	}

	MsgCon("-------------------------------\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavJobScheduler::Dequeue(Entry* pQueue)
{
	memmove(pQueue, &pQueue[1], sizeof(Entry)*(kMaxEntries-1));
	memset(&pQueue[kMaxEntries-1], 0, sizeof(Entry));
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavJobScheduler::IsQueueEmpty(const Entry* pQueue) const
{
	return !pQueue[0].m_hNavChar.HandleValid();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavJobScheduler::RefillQueues()
{
	CharacterManager& mgr = CharacterManager::GetManager();

	U32F defaultIndex = 0;

	const I32F numChars = mgr.GetNumCharacters();
	for (U32F iChar = 0; iChar < numChars; ++iChar)
	{
		const Character* pChar = mgr.GetCharacter(iChar);
		if (!pChar || !pChar->IsKindOf(g_type_NavCharacter))
			continue;

		const NavCharacter* pNavChar = (const NavCharacter*)pChar;
		bool skip = false;
		for (int i = 0; i < kMaxEntries; i++)
		{
			if (m_wantToMoveQueue[i].m_hNavChar.ToProcess() == pNavChar)
				skip = true;
			if (m_defaultQueue[i].m_hNavChar.ToProcess() == pNavChar)
				skip = true;
		}

		if (skip)
			continue;

		if (defaultIndex < kMaxEntries)
		{
			m_defaultQueue[defaultIndex].m_hNavChar = pNavChar;
			++defaultIndex;
		}
		else
		{
			MsgConPersistent("NavJobSchduler overflowed! Please bump kMaxEntries (%d, numChars: %d)\n", kMaxEntries, numChars);
			break;
		}
	}
}