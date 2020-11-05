
#include "ndlib/anim/effect-anim-editor.h"

I32F EffectAnimEditor::EditEntry( EffectAnim& anim, U32F index, StringId64 name, F32 frame )
{
	if (index > anim.m_numEffects)
		return false;
	U32F dest;
	for (dest = 0; dest < anim.m_numEffects; ++dest)
	{
		if (anim.m_pEffects[dest].m_frame >= frame)
			break;
	}
	EffectAnimEntry& entry = anim.m_pEffects[index];
	entry.m_name = name;
	entry.m_frame = frame;
	if (MoveItem(anim.m_pEffects, anim.m_numEffects, index, dest))
		return (index < dest) ? dest - 1: dest;
	return -1;
}

I32F EffectAnimEditor::AddEntry( EffectAnim& anim, StringId64 name, F32 frame )
{
	EffectAnimEntry newEntry;
	newEntry.m_name = name;
	newEntry.m_frame = frame;
	newEntry.m_numTags = 0;
	newEntry.m_tags = nullptr;
	U32F dest;
	for (dest = 0; dest < anim.m_numEffects; ++dest)
	{
		if (anim.m_pEffects[dest].m_frame >= frame)
			break;
	}

	if (InsertItem(anim.m_pEffects, anim.m_numEffects, &newEntry, dest))
		return dest;
	else
		return -1;
}


I32F EffectAnimEditor::AddTag( EffectAnimEntry& entry, StringId64 type, EffectAnimEntryTag::ValueType val )
{
	EffectAnimEntryTag tag;
	tag.m_nameId = type;
	tag.m_value = val;
	if (AddItem(entry.m_tags, entry.m_numTags, &tag))
		return entry.m_numTags;
	else
		return -1;
}
