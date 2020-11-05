/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/ai/agent/nav-character.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NavJobScheduler
{
public:
	static NavJobScheduler& Get() { return sm_manager; }
	
	NavJobScheduler();

	void Update();
	void PostRenderUpdate();
	void NotifyWantToMove(const NavCharacter* pNavChar);
	bool CanRunThisFrame(const NavCharacter* pNavChar) const;

	void DebugDraw() const;

private:
	struct Entry
	{
		NavCharacterHandle m_hNavChar;
	};

	void RefillQueues();
	bool IsQueueEmpty(const Entry* pQueue) const;
	void Dequeue(Entry* pQueue);

	static CONST_EXPR size_t kMaxEntries = 64;

	mutable NdAtomicLock m_accessLock;

	Entry m_wantToMoveQueue[kMaxEntries];
	Entry m_defaultQueue[kMaxEntries];
	I64 m_frameIndex;

	static NavJobScheduler sm_manager;
};
