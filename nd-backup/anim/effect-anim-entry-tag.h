/*
 * Copyright (c) 2009 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

/// --------------------------------------------------------------------------------------------------------------- ///
class EffectAnimEntryTag
{
public:
	EffectAnimEntryTag();

	StringId64 GetNameId() const	{ return m_nameId; }

	U32 GetValueAsU32() const				{ return static_cast<I32>(m_value.m_f32); }
	I32 GetValueAsI32() const				{ return static_cast<I32>(m_value.m_f32); }
	F32 GetValueAsF32() const				{ return m_value.m_f32; }
	StringId64 GetValueAsStringId() const	{ return m_value.m_stringId; }

	void Construct(StringId64 nameId, const char* valueStr);
	void Construct(StringId64 nameId, StringId64 valueId);
	void Construct(StringId64 nameId, float valueFlt);
	void Construct(StringId64 nameId, int valueInt);
	void SetValueAsF32(F32 newValue);

	typedef union
	{
		F32 m_f32;
		StringId64 m_stringId;
	} ValueType;

private:
	StringId64 m_nameId;
	ValueType m_value;
	friend class EffectAnimEditor;
};
