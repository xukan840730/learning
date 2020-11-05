/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef _BG_ATTACHER_H_
#define _BG_ATTACHER_H_

class NdGameObject;

const int kMaxAttachedLevels = 30;

class BgAttacher
{
public:
	void Init(StringId64 levelId, NdGameObject* pNdGameObject);
	void AddLevel(StringId64 levelId);
	void ClearLevels();
	void UpdateLevelLoc(NdGameObject* pNdGameObject);
private:
	StringId64 m_levelId[kMaxAttachedLevels];
	int m_numAttachedLevels;
};

#endif // _BG_ATTACHER_H_
