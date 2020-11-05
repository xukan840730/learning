/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ND_SUBSYSTEM_HANDLE_INL_H
#define ND_SUBSYSTEM_HANDLE_INL_H

#ifndef ND_SUBSYSTEM_H
#error Do not include this file directly. Instead, include "gamelib/gameplay/nd-subsystem.h"
#endif


/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
TypedSubsystemHandle<T>::TypedSubsystemHandle()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
TypedSubsystemHandle<T>::TypedSubsystemHandle(T& sys)
{
	NdSubsystem* pSys = static_cast<NdSubsystem*>(&sys);
	m_pHeapRec = pSys->GetHeapRecord();
	m_offset = reinterpret_cast<char*>(pSys) - reinterpret_cast<char*>(m_pHeapRec->m_pItem);
	m_id = pSys->GetSubsystemId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
TypedSubsystemHandle<T>::TypedSubsystemHandle(T* pTSys)
{
	if (NdSubsystem* pSys = static_cast<NdSubsystem*>(pTSys))
	{
		m_pHeapRec = pSys->GetHeapRecord();
		m_offset = reinterpret_cast<char*>(pSys) - reinterpret_cast<char*>(m_pHeapRec->m_pItem);
		m_id = pSys->GetSubsystemId();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
template <class T2> 
TypedSubsystemHandle<T>::operator TypedSubsystemHandle<T2>() const
{
	// The following line is critical to ensuring the type safety of conversions
	T2* pT = (T*)nullptr;  // static check that types are compatible
	return *(TypedSubsystemHandle<T2>*)this;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
template <class NewHandleType>
NewHandleType TypedSubsystemHandle<T>::CastHandleType() const
{
	T* pT2 = NewHandleType::NullPtr();  // static check that types are compatible
	return *(NewHandleType*)this;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
T* TypedSubsystemHandle<T>::ToSubsystem() const
{
	NdSubsystem* pSubsystem = ToSubsystemInternal();
	return (T*)pSubsystem;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
template <class T2>
T2* TypedSubsystemHandle<T>::ToSubsystem(StringId64 t2Sid) const
{
	NdSubsystem* pBasePtr = ToSubsystemInternal();
	if (pBasePtr && !pBasePtr->IsKindOf(t2Sid))
		return nullptr;
	return static_cast<T2*>(pBasePtr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
U32 TypedSubsystemHandle<T>::GetSubsystemId() const 
{ 
	// verify that this handle is valid, side stepping ToSubsystem,
	// before returning the id
	if (m_pHeapRec && m_id != 0 && m_pHeapRec->m_pItem)
	{
		NdSubsystem* pSys = (NdSubsystem*)(reinterpret_cast<char*>(m_pHeapRec->m_pItem) + m_offset);
		if (pSys->GetSubsystemId() == m_id)
		{
			return m_id;
		}
	}

	return 0; 
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
bool TypedSubsystemHandle<T>::HandleValid() const
{
	bool valid = false;
	if (m_pHeapRec && m_id != 0 && m_pHeapRec->m_pItem)
	{
		NdSubsystem* pSys = (NdSubsystem*)(reinterpret_cast<char*>(m_pHeapRec->m_pItem) + m_offset);
		if (pSys->GetSubsystemId() == m_id)
		{
			valid = true;
		}
	}
	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
bool TypedSubsystemHandle<T>::Valid() const
{
	return HandleValid();
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
bool TypedSubsystemHandle<T>::Assigned() const
{
	return m_pHeapRec != NULL;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class T>
NdSubsystem* TypedSubsystemHandle<T>::ToSubsystemInternal() const
{
	NdSubsystem* pSysRet = NULL;

	if (m_pHeapRec && m_id != 0 && m_pHeapRec->m_pItem)
	{
		NdSubsystem* pSys = (NdSubsystem*)(reinterpret_cast<char*>(m_pHeapRec->m_pItem) + m_offset);
		if (pSys->GetSubsystemId() == m_id)
		{
			pSysRet = pSys;
		}
	}

	return pSysRet;
}


typedef TypedSubsystemHandle<NdSubsystem>			NdSubsystemHandle;



#endif
