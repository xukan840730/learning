/*
 * Copyright (c) 2012 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/smoke-bomb-list.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/nd-frame-state.h"

#include "gamelib/script/nd-script-arg-iterator.h"

// -------------------------------------------------------------------------------------------------
// SmokeBombList
// -------------------------------------------------------------------------------------------------
SmokeBombList::SmokeBombList()
	: m_lastCleanupFrameNumber(0)
	, m_numSmokeBombs(0)
	, m_lock(JlsFixedIndex::kSmokeBombListLock, SID("SmokeBombListLock"))
{}

SmokeBombList& SmokeBombList::Get()
{
	static SmokeBombList s_instance;
	return s_instance;
}

void SmokeBombList::AddSmokeBomb(int ownerNetId, Point center, float startRadius, float endRadius, float radiusLerpTime, float time)
{
	AtomicLockJanitorWrite_Jls janitor(&m_lock, FILE_LINE_FUNC);

	if (m_numSmokeBombs >= kMaxNumSmokeBombs - 1)
		return;

	SmokeBomb &smokeBomb = m_smokeBombs[m_numSmokeBombs++];

	smokeBomb.m_ownerNetId = ownerNetId;
	smokeBomb.m_startTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();
	smokeBomb.m_lifetime = time;
	smokeBomb.m_radiusStart = startRadius;
	smokeBomb.m_radiusEnd = endRadius;
	smokeBomb.m_radiusLerpTime = radiusLerpTime;
	smokeBomb.m_position = center;
}

void SmokeBombList::DebugDraw()
{
	STRIP_IN_FINAL_BUILD;

	for (int i=0; i<m_numSmokeBombs; i++)
	{
		const float radius = CalcRadius(m_smokeBombs[i]);
		g_prim.Draw(DebugSphere(m_smokeBombs[i].m_position, radius, kColorBlue));
	}
}

void SmokeBombList::CleanupList()
{
	AtomicLockJanitorWrite_Jls janitor(&m_lock, FILE_LINE_FUNC);

	// only cleanup once a frame
	if (m_lastCleanupFrameNumber == EngineComponents::GetNdFrameState()->m_gameFrameNumber)
		return;

	m_lastCleanupFrameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumber;

	for (int i=0; i<m_numSmokeBombs; i++)
	{
		SmokeBomb &smokeBomb = m_smokeBombs[i];
		if (EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetTimePassed(smokeBomb.m_startTime) > Seconds(smokeBomb.m_lifetime))
		{
			m_smokeBombs[i] = m_smokeBombs[--m_numSmokeBombs];
			i--;
		}
	}

	ALWAYS_ASSERT(m_numSmokeBombs >= 0);
}

void SmokeBombList::Reset()
{
	AtomicLockJanitorWrite_Jls janitor(&m_lock, FILE_LINE_FUNC);

	m_numSmokeBombs = 0;
}

bool SmokeBombList::IsSphereInside(Point center, float radius, int ownerNetId)
{
	AtomicLockJanitorRead_Jls janitor(&m_lock, FILE_LINE_FUNC);
	for (int i=0; i<m_numSmokeBombs; i++)
	{
		if (ownerNetId == 0 || m_smokeBombs[i].m_ownerNetId == ownerNetId)
		{
			Vector toCenter = m_smokeBombs[i].m_position - center;
			float radius2 = Sqr(CalcRadius(m_smokeBombs[i]));
			float dist2 = Dot(toCenter, toCenter);

			if (dist2 < (radius2 + radius))
			{
				return true;
			}
		}
	}

	return false;
}

bool SmokeBombList::IsLineInSmoke(Point_arg start, Point_arg end)
{
	AtomicLockJanitorRead_Jls janitor(&m_lock, FILE_LINE_FUNC);
	for (I32F i = 0; i < m_numSmokeBombs; ++i)
	{
		Sphere smokeSphere(m_smokeBombs[i].m_position, CalcRadius(m_smokeBombs[i]));
		if (smokeSphere.TestLineOverlap(start, end))
			return true;
	}

	return false;
}

int SmokeBombList::GetNumSmokeBombs()
{
	AtomicLockJanitorRead_Jls janitor(&m_lock, FILE_LINE_FUNC);
	return m_numSmokeBombs;
}

Point SmokeBombList::GetSmokeBombCenter(int i)
{
	AtomicLockJanitorRead_Jls janitor(&m_lock, FILE_LINE_FUNC);
	return m_smokeBombs[i].m_position;
}

float SmokeBombList::GetSmokeBombRadius(int i)
{
	AtomicLockJanitorRead_Jls janitor(&m_lock, FILE_LINE_FUNC);
	return CalcRadius(m_smokeBombs[i]);
}

float SmokeBombList::CalcRadius(const SmokeBomb &smokeBomb) const
{
	const float timePassed = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetTimePassed(smokeBomb.m_startTime).ToSeconds();
	return LerpScaleClamp(0.0f, smokeBomb.m_radiusLerpTime, smokeBomb.m_radiusStart, smokeBomb.m_radiusEnd, timePassed);
}

SCRIPT_FUNC("smokebomb-register", DcSmokebombRegister)
{
	SCRIPT_ARG_ITERATOR(args, 5);

	const int ownerNetId = args.NextI32();
	const BoundFrame *pBoundFrame = args.NextBoundFrame();
	const float startRadius = args.NextFloat();
	const float endRadius = args.NextFloat();
	const float radiusLerpTime = args.NextFloat();
	const float lifetime = args.NextFloat();

	SmokeBombList::Get().AddSmokeBomb(ownerNetId, pBoundFrame->GetTranslation(), startRadius, endRadius, radiusLerpTime, lifetime);

	return ScriptValue(0);
}

SCRIPT_FUNC("sphere-inside-smokebomb", DcSphereInsideSmokebomb)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	const Point pos = args.NextPoint();
	const float rad = args.NextFloat();
	const I32 ownerNetId = args.NextI32();

	const bool inside = SmokeBombList::Get().IsSphereInside(pos, rad, ownerNetId);

	return ScriptValue(inside);
}