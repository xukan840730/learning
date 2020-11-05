/*
 * Copyright (c) 2012 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef SMOKE_BOMB_LIST_H
#define SMOKE_BOMB_LIST_H

#include "corelib/system/read-write-atomic-lock.h"

const int kMaxNumSmokeBombs = 20;

class SmokeBombList
{
public:
	SmokeBombList();

	static SmokeBombList& Get();

	void DebugDraw();

	int GetNumSmokeBombs();
	Point GetSmokeBombCenter(int i);
	float GetSmokeBombRadius(int i);
	void AddSmokeBomb(int ownerNetId, Point center, float startRadius, float endRadius, float radiusLerpTime, float time);

	bool IsSphereInside(Point center, float radius, int ownerNetIdFilter = 0);
	bool IsLineInSmoke(Point_arg start, Point_arg end);
	void CleanupList();
	void Reset();

private:

	struct SmokeBomb
	{
		int m_ownerNetId;
		Point m_position;
		TimeFrame m_startTime;
		float m_lifetime;
		float m_radiusLerpTime;
		float m_radiusStart;
		float m_radiusEnd;
	};

	float CalcRadius(const SmokeBomb &smokeBomb) const;

	I64			m_lastCleanupFrameNumber;
	int			m_numSmokeBombs;
	SmokeBomb	m_smokeBombs[kMaxNumSmokeBombs];

	NdRwAtomicLock64_Jls	m_lock;
};

#endif // SMOKE_BOMB_LIST_H
