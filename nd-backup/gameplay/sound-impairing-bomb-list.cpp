/*
 * Copyright (c) 2012 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/sound-impairing-bomb-list.h"

#include "gamelib/script/nd-script-arg-iterator.h"

#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/nd-frame-state.h"

SoundImpairingBombList SoundImpairingBombList::sm_instance;

 //_____________________________________________________________________________

namespace {
	bool IsPointInsideRadius(const Point& point, const Point& bombPosition, const float fRadius)
	{
		const Vector bombToPoint = point - bombPosition;
		return LengthSqr(bombToPoint) <= Sqr(fRadius);
	}
}

 //_____________________________________________________________________________

SoundImpairingBombList::SoundImpairingBombList()
	: m_lastCleanupFrameNumber(0ll)
	, m_lock(JlsFixedIndex::kSoundImpairingBombListLock, SID("kSoundImpairingBombListLock"))
{
	// Nothing to do
}

//_____________________________________________________________________________

SoundImpairingBombList& SoundImpairingBombList::GetInstance()
{
	return sm_instance;
}

//_____________________________________________________________________________

void SoundImpairingBombList::AddBomb(const Point& position, const float innerRadius, const float outerRadius, const float ttl)
{
	AtomicLockJanitorWrite_Jls janitor(&m_lock, FILE_LINE_FUNC);

	if (m_aSoundImpairingBombs.Full())
		return;

	SoundImpairingBomb newSoundImpairingBomb;
	newSoundImpairingBomb.m_startTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();
	newSoundImpairingBomb.m_lifetime = ttl;
	newSoundImpairingBomb.m_innerRadius = innerRadius;
	newSoundImpairingBomb.m_outerRadius = outerRadius;
	newSoundImpairingBomb.m_position = position;

	m_aSoundImpairingBombs.PushBack(newSoundImpairingBomb);
}

//_____________________________________________________________________________

bool SoundImpairingBombList::IsPointWithinDeafeningRadius(const Point& point)
{
	AtomicLockJanitorRead_Jls janitor(&m_lock, FILE_LINE_FUNC);

	for (SoundImpairingBomb& bomb : m_aSoundImpairingBombs)
	{
		if (IsPointInsideRadius(point, bomb.m_position, bomb.m_innerRadius))
			return true;
	}

	return false;
}

//_____________________________________________________________________________

float SoundImpairingBombList::CalculateListeningAcuity(const Point& point)
{
	AtomicLockJanitorRead_Jls janitor(&m_lock, FILE_LINE_FUNC);

	float fCurrentAcuity = 1.0f;

	for (SoundImpairingBomb& bomb : m_aSoundImpairingBombs)
	{
		const Vector bombToPoint = point - bomb.m_position;
		const float sqrdBombToPointDist = LengthSqr(bombToPoint);
		const float sqrdBombOuterRadius = Sqr(bomb.m_outerRadius);

		const bool bIsWithinOuterRadius = (sqrdBombToPointDist <= sqrdBombOuterRadius);
		if (UNLIKELY(bIsWithinOuterRadius))
		{
			const float sqrdBombInnerRadius = Sqr(bomb.m_innerRadius);
			const bool bIsWithinInnerRadius = (sqrdBombToPointDist <= sqrdBombInnerRadius);

			if (bIsWithinInnerRadius)
			{
				return 0.0f;
			}
			else
			{
				const float outerRadiusDelta = bomb.m_outerRadius - bomb.m_innerRadius;

				if (IsNearZero(outerRadiusDelta))
				{
					return 0.0f;
				}
				else
				{
					const float distanceFromOuterRadius = bomb.m_outerRadius - sqrtf(sqrdBombToPointDist);
					const float fNewAcuity = distanceFromOuterRadius / outerRadiusDelta;

					if (fNewAcuity < fCurrentAcuity)
						fCurrentAcuity = fNewAcuity;
						
				}
			}
		}
	}

	return fCurrentAcuity;
}

//_____________________________________________________________________________

void SoundImpairingBombList::CleanUpExpiredBombs()
{
	AtomicLockJanitorWrite_Jls janitor(&m_lock, FILE_LINE_FUNC);
	NdFrameState& rFrameState = *EngineComponents::GetNdFrameState();
	
	// only cleanup once a frame
	if (m_lastCleanupFrameNumber == rFrameState.m_gameFrameNumber)
		return;

	m_lastCleanupFrameNumber = rFrameState.m_gameFrameNumber;

	U32 i = 0u;
	while (i < m_aSoundImpairingBombs.Size())
	{
		SoundImpairingBomb& soundImpairingBomb = m_aSoundImpairingBombs[i];
		const TimeFrame elapsedTime = rFrameState.GetClock(kGameClock)->GetTimePassed(m_aSoundImpairingBombs[i].m_startTime);

		if (elapsedTime > Seconds(soundImpairingBomb.m_lifetime))
		{
			const U32 lastIndex = m_aSoundImpairingBombs.Size() - 1u;

			m_aSoundImpairingBombs[i] = m_aSoundImpairingBombs[lastIndex];
			m_aSoundImpairingBombs.Resize(lastIndex);
		}
		else
		{
			++i;
		}
	}
}

//_____________________________________________________________________________

void SoundImpairingBombList::ClearList()
{
	AtomicLockJanitorWrite_Jls janitor(&m_lock, FILE_LINE_FUNC);
	m_aSoundImpairingBombs.Clear();
}

//_____________________________________________________________________________

#if !FINAL_BUILD
void SoundImpairingBombList::DebugDraw()
{
	for (SoundImpairingBomb& bomb : m_aSoundImpairingBombs)
	{
		g_prim.Draw(DebugSphere(bomb.m_position, bomb.m_innerRadius, kColorRed));
		g_prim.Draw(DebugSphere(bomb.m_position, bomb.m_outerRadius, kColorYellow));
	}
}
#endif