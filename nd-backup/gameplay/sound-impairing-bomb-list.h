/*
 * Copyright (c) 2012 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef SOUND_IMPAIRING_BOMB_LIST_H
#define SOUND_IMPAIRING_BOMB_LIST_H

#include "corelib/system/read-write-atomic-lock.h"
#include "corelib/util/fixed-array.h"
#include "sharedmath/ps4/include/shared/math/point.h"

class SoundImpairingBombList
{
public:
	static SoundImpairingBombList& GetInstance();

	void AddBomb(const Point& position, const float innerRadius, const float outerRadius, const float ttl);

	bool IsPointWithinDeafeningRadius(const Point& point);
	float CalculateListeningAcuity(const Point& point);
	void CleanUpExpiredBombs();
	void ClearList();

#if !FINAL_BUILD
	void DebugDraw();
#endif

private:

	struct SoundImpairingBomb
	{
		Point m_position;
		TimeFrame m_startTime;
		float m_lifetime;
		float m_innerRadius;
		float m_outerRadius;
	};

	SoundImpairingBombList(); // Singleton

	static CONST_EXPR U8 kMaxNumSoundImpairingBombs = 20u;

	static SoundImpairingBombList sm_instance;

	I64 m_lastCleanupFrameNumber;
	FixedArray<SoundImpairingBomb, kMaxNumSoundImpairingBombs> m_aSoundImpairingBombs;

	NdRwAtomicLock64_Jls m_lock;
};

#endif
