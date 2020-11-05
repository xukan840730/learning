/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/effect-filter.h"

#include "corelib/util/color.h"
#include "corelib/util/msg.h"

#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/process/clock.h"
#include "ndlib/process/process.h"

/// --------------------------------------------------------------------------------------------------------------- ///
extern bool IsSoundEffect(const StringId64 effectType); // from effect-control.cpp

bool g_enableSfxLimiting		= true;
float g_sfxLimitingMinTime		= 8.0f / 30.0f;
float g_sfxLimitingHorseMinTime = 8.0f / 30.0f;

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectFilter::Init()
{
	for (U32F i = 0; i < kMaxEntries; ++i)
	{
		m_entries[i] = Entry();
	}
	m_nextEntrySlot = 0;
	m_enabled = false;
	m_sfxLimitingTime = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectFilter::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (!IsEnabled())
		return;

	const Process* pProc = Process::GetContextProcess();
	const Clock* pProcessClock = GetProcessClock();

	const float maxTime = m_sfxLimitingTime;

	SetColor(kMsgCon, kColorWhite.ToAbgr8());

	MsgCon("----------------------------------------------------------------\n");

	for (U32F i = 0; i < kMaxEntries; ++i)
	{
		const Entry& ent = m_entries[i];
		if (!ent.m_filterGroupId)
			continue;

		// const EffectAnimEntryTag* pCmpJointTag = ent.m_pTriggeredEffect->GetTagByName(SID("joint"));

		const float deltaTime = ToSeconds(pProcessClock->GetTimePassed(ent.m_timeStamp));

		if (deltaTime > maxTime)
			continue;

		const Color clr = Lerp(kColorRed, kColorWhite, MinMax(deltaTime/maxTime, 0.0f, 1.0f));

		SetColor(kMsgCon, clr.ToAbgr8());

		MsgCon("%s %s %0.2f [%s]\n",
			   DevKitOnly_StringIdToString(ent.m_triggeredEffectNameSid),
			   (ent.m_triggeredEffectJointTagSid != INVALID_STRING_ID_64)
				   ? DevKitOnly_StringIdToString(ent.m_triggeredEffectJointTagSid)
				   : "",
			   deltaTime,
			   DevKitOnly_StringIdToString(StringId64(ent.m_filterGroupId)));
	}

	SetColor(kMsgCon, kColorWhite.ToAbgr8());
	MsgCon("----------------------------------------------------------------\n\n\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EffectFilter::IsExclusiveEffect(const EffectAnimEntry* pTriggeredEffect, StringId64 typeNameId) const
{
	for (int i = 0; i < pTriggeredEffect->GetNumTags(); i++)
	{
		const EffectAnimEntryTag* pTag = pTriggeredEffect->GetTagByIndex(i);
		if (pTag->GetNameId() == SID("type"))
		{
			if (pTag->GetValueAsStringId() == typeNameId)
				return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EffectFilter::HandleTriggeredEffect(const EffectAnimEntry* pTriggeredEffect,
										 uintptr_t filterGroupId,
										 bool inWaistDeepWater,
										 bool isMeleeHit,
										 bool isFlipped)
{
	if (!IsEnabled())
		return true;

	if (!pTriggeredEffect)
		return false;

	//if (inWaistDeepWater && IsExclusiveEffect(pTriggeredEffect, SID("dry")))
	//	return false;

	if (inWaistDeepWater != IsExclusiveEffect(pTriggeredEffect, SID("water")))
		return false;

	if (ShouldDisableMeleeEffect(pTriggeredEffect, isMeleeHit))
		return false;

	const Clock* pProcessClock = GetProcessClock();

	StringId64 jointId = INVALID_STRING_ID_64;
	const EffectAnimEntryTag* pSrcJointTag = pTriggeredEffect->GetTagByName(SID("joint"));
	if (pSrcJointTag)
	{
		jointId = pSrcJointTag->GetValueAsStringId();
	}


	if (isFlipped)
	{
		// Replace the 'trigger location'
		switch (jointId.GetValue())
		{
			// Left -> Right
		case SID_VAL("l_knee"):				jointId = SID("r_knee");			break;
		case SID_VAL("l_ankle"):			jointId = SID("r_ankle");			break;
		case SID_VAL("l_heel"):				jointId = SID("r_heel");			break;
		case SID_VAL("l_ball"):				jointId = SID("r_ball");			break;
		case SID_VAL("l_toe"):				jointId = SID("r_toe");			break;
		case SID_VAL("l_palm"):				jointId = SID("r_palm");			break;
		case SID_VAL("l_wrist"):			jointId = SID("r_wrist");			break;
		case SID_VAL("l_elbow"):			jointId = SID("r_elbow");			break;
		case SID_VAL("l_shoulder"):			jointId = SID("r_shoulder");		break;

			// Right -> Left
		case SID_VAL("r_knee"):				jointId = SID("l_knee");			break;
		case SID_VAL("r_ankle"):			jointId = SID("l_ankle");			break;
		case SID_VAL("r_heel"):				jointId = SID("l_heel");			break;
		case SID_VAL("r_ball"):				jointId = SID("l_ball");			break;
		case SID_VAL("r_toe"):				jointId = SID("l_toe");			break;
		case SID_VAL("r_palm"):				jointId = SID("l_palm");			break;
		case SID_VAL("r_wrist"):			jointId = SID("l_wrist");			break;
		case SID_VAL("r_elbow"):			jointId = SID("l_elbow");			break;
		case SID_VAL("r_shoulder"):			jointId = SID("l_shoulder");		break;

		default:
			break;
		}
	}

	if (LimitSoundEffect(pTriggeredEffect, jointId))
		return false;

	if (Entry* pNewEntry = GetNewEntry())
	{
		// we should not dereference this pointer, as it may be unloaded later on
		// we hold on to it only as a unique tag for each effect
		pNewEntry->m_filterGroupId = filterGroupId;
		pNewEntry->m_timeStamp = pProcessClock->GetCurTime(); // don't mark it as "played" just yet... see MarkEffectPlayed()
		
		pNewEntry->m_triggeredEffectNameSid = pTriggeredEffect ? pTriggeredEffect->GetNameId() : INVALID_STRING_ID_64;
		pNewEntry->m_triggeredEffectJointTagSid = jointId;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectFilter::MarkEffectPlayed(uintptr_t filterGroupId)
{
	if (IsEnabled())
	{
		if (Entry* pExistingEntry = GetAssociatedEntry(filterGroupId))
		{
			const Clock* pProcessClock = GetProcessClock();
			ASSERT(pProcessClock);
			pExistingEntry->m_timeStamp = pProcessClock->GetCurTime();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
EffectFilter::Entry* EffectFilter::GetAssociatedEntry(uintptr_t filterGroupId)
{
	for (U32F i = 0; i < kMaxEntries; ++i)
	{
		if (m_entries[i].m_filterGroupId == filterGroupId)
			return &m_entries[i];
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
EffectFilter::Entry* EffectFilter::GetNewEntry()
{
	Entry* pNewEntry = &m_entries[m_nextEntrySlot];

	m_nextEntrySlot++;
	m_nextEntrySlot %= kMaxEntries;

	return pNewEntry;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EffectFilter::LimitSoundEffect(const EffectAnimEntry* pTriggeredEffect, StringId64 jointId)
{
	if (!g_enableSfxLimiting)
		return false;

	if (!pTriggeredEffect)
		return false;

	if (!IsSoundEffect(pTriggeredEffect->GetNameId()))
		return false;

	const Clock* pProcessClock = GetProcessClock();

	const TimeFrame maxTime = Seconds(m_sfxLimitingTime);

	for (U32F i = 0; i < kMaxEntries; ++i)
	{
		const Entry& ent = m_entries[i];
		if (!ent.m_filterGroupId)
			continue;

		if (!IsSoundEffect(ent.m_triggeredEffectNameSid))
			continue;

		// const EffectAnimEntryTag* pCmpJointTag = ent.m_pTriggeredEffect->GetTagByName(SID("joint"));
		if (ent.m_triggeredEffectJointTagSid == INVALID_STRING_ID_64)
			continue;

		if (jointId != ent.m_triggeredEffectJointTagSid)
			continue;

		TimeFrame deltaTime = pProcessClock->GetTimePassed(ent.m_timeStamp);

		if (deltaTime > maxTime)
			continue;
		
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EffectFilter::ShouldDisableMeleeEffect(const EffectAnimEntry* pTriggeredEffect, bool isMeleeHit) const
{
	bool isMelee = false;
	bool effectIsHit = false;
	for (int i = 0; i < pTriggeredEffect->GetNumTags(); i++)
	{
		const EffectAnimEntryTag* pTag = pTriggeredEffect->GetTagByIndex(i);
		if (pTag->GetNameId() == SID("type"))
		{
			if (pTag->GetValueAsStringId() == SID("melee-hit"))
			{
				isMelee = true;
				effectIsHit = true;
				break;
			}
			else if (pTag->GetValueAsStringId() == SID("melee-miss"))
			{
				isMelee = true;
				effectIsHit = false;
				break;
			}
		}
	}

	if (isMelee && effectIsHit != isMeleeHit)
		return true;

	return false;
}
