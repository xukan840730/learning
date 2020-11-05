/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/timeframe.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class EffectAnimEntry;

/// --------------------------------------------------------------------------------------------------------------- ///
class EffectFilter
{
public:

	void Enable(float sfxLimitingTime)	{ m_sfxLimitingTime = sfxLimitingTime, m_enabled = true; }
	void Disable()						{ m_enabled = false; }
	bool IsEnabled() const				{ return m_enabled; }

	void Init();
	void DebugDraw() const;

	// call this for every effect triggered, will return true if should process
	// false if it is filtered and should be ignored
	bool HandleTriggeredEffect(const EffectAnimEntry* pTriggeredEffect,
							   uintptr_t filterGroupId,
							   bool inWaistDeepWater,
							   bool isMeleeHit,
							   bool isFlipped);

	// call this to tell the filtering system that the effect definitely played
	void MarkEffectPlayed(uintptr_t filterGroupId);

private:

	static CONST_EXPR size_t kMaxEntries = 16;
	static CONST_EXPR U32F kMinFramesToRepeatEffect = 2;

	struct Entry
	{
		Entry()
			: m_filterGroupId(0)
			, m_triggeredEffectNameSid(INVALID_STRING_ID_64)
			, m_triggeredEffectJointTagSid(INVALID_STRING_ID_64)
		{
		}

		TimeFrame	m_timeStamp;
		uintptr_t	m_filterGroupId;
		StringId64	m_triggeredEffectNameSid;
		StringId64	m_triggeredEffectJointTagSid;
	};

	Entry* GetNewEntry();
	Entry* GetAssociatedEntry(uintptr_t filterGroupId);
	bool LimitSoundEffect(const EffectAnimEntry* pTriggeredEffect, StringId64 jointId);
	bool IsExclusiveEffect(const EffectAnimEntry* pTriggeredEffect, StringId64 typeNameId) const;
	bool ShouldDisableMeleeEffect(const EffectAnimEntry* pTriggeredEffect, bool isMeleeHit) const;

	bool	m_enabled;
	Entry	m_entries[kMaxEntries];
	U8		m_nextEntrySlot;

	float	m_sfxLimitingTime;
};
