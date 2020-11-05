/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */


#include "gamelib/gameplay/bg-attacher.h"

#include "gamelib/level/level-mgr.h"
#include "gamelib/level/level.h"

void BgAttacher::Init(StringId64 levelId, NdGameObject* pNdGameObject)
{
	m_numAttachedLevels = 0;

	if (levelId != INVALID_STRING_ID_64)
		AddLevel(levelId);
}

void BgAttacher::ClearLevels()
{
	m_numAttachedLevels = 0;
}

void BgAttacher::AddLevel(StringId64 levelId)
{
	ALWAYS_ASSERT(m_numAttachedLevels < kMaxAttachedLevels);
	m_levelId[m_numAttachedLevels] = levelId;
	m_numAttachedLevels++;
}

void BgAttacher::UpdateLevelLoc(NdGameObject* pNdGameObject)
{
	if (m_numAttachedLevels > 0)
	{
		for (int i=0; i<m_numAttachedLevels; i++)
		{
			if (Level* pLevel = EngineComponents::GetLevelMgr()->GetLevel(m_levelId[i]))
				pLevel->UpdateAttacher(pNdGameObject);
		}
	}
}
