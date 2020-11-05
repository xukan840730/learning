/*
 * Copyright (c) 2009 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/effect-anim-entry-tag.h"

/// --------------------------------------------------------------------------------------------------------------- ///
EffectAnimEntryTag::EffectAnimEntryTag()
{
	m_nameId = INVALID_STRING_ID_64;
	m_value.m_stringId = INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectAnimEntryTag::Construct(StringId64 nameId, const char* valueStr)
{
	m_nameId = nameId;

	const U32 valueLen = strlen(valueStr);

	if (valueLen == 0)
	{
		m_value.m_stringId = INVALID_STRING_ID_64;
		return;
	}

	const char* valueEnd = valueStr + valueLen;
	char* last = nullptr;

	float asFloat = float(strtod(valueStr, &last));
	if (last == valueEnd)
	{
		m_value.m_f32 = asFloat;
	}
	else
	{
		m_value.m_stringId = StringToStringId64(valueStr);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectAnimEntryTag::Construct(StringId64 nameId, StringId64 valueId)
{
	m_nameId = nameId;
	m_value.m_stringId = valueId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectAnimEntryTag::Construct(StringId64 nameId, float valueFlt)
{
	m_nameId = nameId;
	m_value.m_f32 = valueFlt;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectAnimEntryTag::Construct(StringId64 nameId, int valueInt)
{
	m_nameId = nameId;
	m_value.m_f32 = static_cast<F32>(valueInt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectAnimEntryTag::SetValueAsF32(F32 newValue)
{
	m_value.m_f32 = newValue;
}
