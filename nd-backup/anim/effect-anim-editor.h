#ifndef NDLIB_ANIM_EFFECT_ANIM_EDITOR_H
#define NDLIB_ANIM_EFFECT_ANIM_EDITOR_H

#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/anim/effect-group.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AllocatedArrayManager
{
public:
	AllocatedArrayManager(Memory::Allocator* pAlloc, U32F allocSize) : m_pAlloc(pAlloc), m_allocSize(allocSize) {}

	// Common API for these functions
	// pArray:	a reference to a pointer to the array you are operating on, will be changed if the array is reallocated
	// len:		a reference to the length of the array and will change if the number of elements in the array changes
	// return:	true if the operation succeeds, false otherwise (pArray and len will remain unchanged in this case)
	template<class T> bool MoveItem(T*& pArray, U64& len, U32F src, U32F dest);
	template<class T> bool AddItem(T*& pArray, U32& len, T* pItem);
	template<class T> bool AddItem(T*& pArray, U64& len, T* pItem);
	template<class T> bool RemoveItem(T*& pArray, U32& len, U32F index);
	template<class T> bool RemoveItem(T*& pArray, U64& len, U32F index);
	template<class T> bool InsertItem(T*& pArray, U64& len, T* pItem, U32F index);

private:
	template<class T> T* AddItemHelper   (T* pArray, U32F len, U32F newLen);
	template<class T> T* RemoveItemHelper(T* pArray, U32F len, U32F newLen, U32F index);
	template<class T> T* InsertItemHelper(T* pArray, U32F len, U32F newLen, T* pItem, U32F index);

	Memory::Allocator*	m_pAlloc;
	U32F				m_allocSize;
};

/// --------------------------------------------------------------------------------------------------------------- ///
template<class T>
bool AllocatedArrayManager::MoveItem(T*& pArray, U64& len, U32F src, U32F dest)
{
	if (src >= len || dest > len) return nullptr;
	if (src != dest  && src != dest - 1)
	{
		T temp;
		memcpy(&temp, pArray + src, sizeof(T));
		if (src < dest)
		{
			dest -= 1;
			memmove(pArray + src, pArray + src + 1, (dest - src) * sizeof(T));
		}
		else if (src > dest)
		{
			memmove(pArray + dest + 1, pArray + dest, (src - dest) * sizeof(T));
		}
		memcpy(pArray + dest, &temp, sizeof(T));
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<class T>
bool AllocatedArrayManager::AddItem(T*& pArray, U32& len, T* pItem)
{
	if (!m_pAlloc->IsPtrInRange(pArray))
	{
		const U32F newLen = (1 + len / m_allocSize) * m_allocSize;
		T *temp = AddItemHelper(pArray, len, newLen);
		if (!temp) return false;
		pArray = temp;
	}
	else if ((len & (m_allocSize - 1)) == 0)
	{
		const U32F newLen = len + m_allocSize;
		T* temp = AddItemHelper(pArray, len, newLen);
		if (!temp) return false;
		m_pAlloc->Free(pArray, FILE_LINE_FUNC);
		pArray = temp;
	}
	memcpy(pArray + len, pItem, sizeof(T));
	len++;
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<class T>
bool AllocatedArrayManager::AddItem(T*& pArray, U64& len, T* pItem)
{
	if (!m_pAlloc->IsPtrInRange(pArray))
	{
		const U32F newLen = (1 + len / m_allocSize) * m_allocSize;
		T *temp = AddItemHelper(pArray, len, newLen);
		if (!temp) return false;
		pArray = temp;
	}
	else if ((len & (m_allocSize - 1)) == 0)
	{
		const U32F newLen = len + m_allocSize;
		T* temp = AddItemHelper(pArray, len, newLen);
		if (!temp) return false;
		m_pAlloc->Free(pArray, FILE_LINE_FUNC);
		pArray = temp;
	}
	memcpy(pArray + len, pItem, sizeof(T));
	len++;
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<class T>
bool AllocatedArrayManager::RemoveItem(T*& pArray, U32& len, U32F index)
{
	if (!m_pAlloc->IsPtrInRange(pArray))
	{
		const U32F newLen = (1 + ((len >= 2) ? len - 2 : 0) / m_allocSize) * m_allocSize;
		T* temp = RemoveItemHelper(pArray, len, newLen, index);
		if (!temp) return false;
		pArray = temp;
	}
	else if ((len & (m_allocSize - 1)) == 1)
	{
		const U32F newLen = len - 1;
		T* temp = RemoveItemHelper(pArray, len, newLen, index);
		if (!temp) return false;
		m_pAlloc->Free(pArray, FILE_LINE_FUNC);
		pArray = temp;
	}
	else
	{
		memmove(pArray + index, pArray + index + 1, sizeof(T) * (len - (index + 1)));
	}
	len--;
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<class T>
bool AllocatedArrayManager::RemoveItem(T*& pArray, U64& len, U32F index)
{
	if (!m_pAlloc->IsPtrInRange(pArray))
	{
		const U32F newLen = (1 + ((len >= 2) ? len - 2 : 0) / m_allocSize) * m_allocSize;
		T* temp = RemoveItemHelper(pArray, len, newLen, index);
		if (!temp) return false;
		pArray = temp;
	}
	else if ((len & (m_allocSize - 1)) == 1)
	{
		const U32F newLen = len - 1;
		T* temp = RemoveItemHelper(pArray, len, newLen, index);
		if (!temp) return false;
		m_pAlloc->Free(pArray, FILE_LINE_FUNC);
		pArray = temp;
	}
	else
	{
		memmove(pArray + index, pArray + index + 1, sizeof(T) * (len - (index + 1)));
	}
	len--;
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<class T>
bool AllocatedArrayManager::InsertItem(T*& pArray, U64& len, T* pItem, U32F index)
{
	if (!m_pAlloc->IsPtrInRange(pArray))
	{
		const U32F newLen = (1 + len / m_allocSize) * m_allocSize;
		T* temp = InsertItemHelper(pArray, len, newLen, pItem, index);
		if (!temp) return false;
		pArray = temp;
	}
	else if ((len & (m_allocSize - 1)) == 0)
	{
		const U32F newLen = len + m_allocSize;
		T* temp = InsertItemHelper(pArray, len, newLen, pItem, index);
		if (!temp) return false;
		m_pAlloc->Free(pArray, FILE_LINE_FUNC);
		pArray = temp;
	}
	else
	{
		memmove(pArray + index + 1, pArray + index, sizeof(T) * (len - index));
		memcpy(pArray + index, pItem, sizeof(T));
	}
	len++;
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<class T>
T* AllocatedArrayManager::AddItemHelper(T* pArray, U32F len, U32F newLen)
{
	T* pNewArray = static_cast<T*>(m_pAlloc->Allocate(newLen * sizeof(T), kAlign16, FILE_LINE_FUNC));
	if (!pNewArray) return nullptr;
	memcpy(pNewArray, pArray, len * sizeof(T));
	return pNewArray;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<class T>
T* AllocatedArrayManager::RemoveItemHelper(T* pArray, U32F len, U32F newLen, U32F index)
{
	T* pNewArray = static_cast<T*>(m_pAlloc->Allocate(newLen * sizeof(T), kAlign16, FILE_LINE_FUNC));
	if (!pNewArray) return nullptr;
	memcpy(pNewArray, pArray, index * sizeof(T));
	memcpy(pNewArray + index, pArray + index + 1, sizeof(T) * (len - (index + 1)));
	return pNewArray;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<class T>
T* AllocatedArrayManager::InsertItemHelper(T* pArray, U32F len, U32F newLen, T* pItem, U32F index)
{
	T* pNewArray = static_cast<T*>(m_pAlloc->Allocate(newLen * sizeof(T), kAlign16, FILE_LINE_FUNC));
	if (!pNewArray) return nullptr;
	memcpy(pNewArray, pArray, index * sizeof(T));
	memcpy(pNewArray + index, pItem, sizeof(T));
	memcpy(pNewArray + index + 1, pArray + index, sizeof(T) * (len - index));
	return pNewArray;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class EffectAnimEditor : private AllocatedArrayManager
{
public:
	EffectAnimEditor() : AllocatedArrayManager(Memory::GetAllocator(kAllocDebug), 4) {}

	I32F AddEntry(EffectAnim& anim, StringId64 name, F32 frame);
	I32F EditEntry(EffectAnim& anim, U32F index, StringId64 name, F32 frame);

	bool RemoveEntry(EffectAnim& anim, U32F index)
	{
		return RemoveItem(anim.m_pEffects, anim.m_numEffects, index);
	}

	bool RemoveTag(EffectAnimEntry& entry, U32F index)
	{
		return RemoveItem(entry.m_tags, entry.m_numTags, index);
	}

	I32F AddTagU32(EffectAnimEntry& entry, StringId64 type, U32 val)
	{
		EffectAnimEntryTag::ValueType value;
		value.m_f32 = static_cast<F32>(val);
		return AddTag(entry, type, value);
	}

	I32F AddTagI32(EffectAnimEntry& entry, StringId64 type, I32 val)
	{
		EffectAnimEntryTag::ValueType value;
		value.m_f32 = static_cast<F32>(val);
		return AddTag(entry, type, value);
	}

	I32F AddTagF32(EffectAnimEntry& entry, StringId64 type, F32 val)
	{
		EffectAnimEntryTag::ValueType value;
		value.m_f32 = val;
		return AddTag(entry, type, value);
	}

	I32F AddTagSid(EffectAnimEntry& entry, StringId64 type, StringId64 val)
	{
		EffectAnimEntryTag::ValueType value;
		value.m_stringId = val;
		return AddTag(entry, type, value);
	}

	void EditTagU32(EffectAnimEntryTag& tag, U32 val)				{ tag.m_value.m_f32 = static_cast<F32>(val); }
	void EditTagI32(EffectAnimEntryTag& tag, I32 val)				{ tag.m_value.m_f32 = static_cast<F32>(val); }
	void EditTagF32(EffectAnimEntryTag& tag, F32 val)				{ tag.m_value.m_f32 = val; }
	void EditTagSid(EffectAnimEntryTag& tag, StringId64 val)		{ tag.m_value.m_stringId = val; }

private:
	I32F AddTag(EffectAnimEntry& entry, StringId64 type, EffectAnimEntryTag::ValueType val);
};

#endif
