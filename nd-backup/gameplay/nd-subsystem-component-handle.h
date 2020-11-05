/*
 * Copyright (c) 2012 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

/*! \file process-component-handle.h
   \brief Handle to components of other processes

See process.cpp for more details.
*/

#ifndef GAMELIB_ND_SUBSYSTEM_COMPONENT_HANDLE_H
#define GAMELIB_ND_SUBSYSTEM_COMPONENT_HANDLE_H

#include "gamelib/gameplay/nd-subsystem.h"
#include "gamelib/gameplay/nd-subsystem-handle.h"

template < typename T >
class SubsystemComponentHandle
{
	NdSubsystemHandle m_hSubsystem;
	ptrdiff_t m_componentDiff;

	void ValidateProcessPointer(NdSubsystem* pSubsystem, T* pComponent)
	{
		if (pSubsystem)
		{
			uintptr_t lowerBound = reinterpret_cast<uintptr_t>(pSubsystem->GetHeapRecord()->m_pMem);
			uintptr_t upperBound = lowerBound + pSubsystem->GetHeapRecord()->GetBlockSize();
			uintptr_t object = reinterpret_cast<uintptr_t>(pComponent);
			ALWAYS_ASSERT(lowerBound <= object && object < upperBound);
		}
	}

public:
	SubsystemComponentHandle() {}
	SubsystemComponentHandle(NdSubsystem* pSubsystem, T* pComponent)
	{
		ASSERT(pSubsystem);
		ValidateProcessPointer(pSubsystem, pComponent);
		m_hSubsystem = pSubsystem;
		m_componentDiff = reinterpret_cast<uintptr_t>(pComponent)-reinterpret_cast<uintptr_t>(pSubsystem);
	}

	bool HandleValid() const
	{
		return m_hSubsystem.HandleValid();
	}

	T* ToPointer() const
	{
		if (NdSubsystem* pSubsystem = m_hSubsystem.ToSubsystem())
		{
			union
			{
				uintptr_t uint;
				T* ptr;
			}
			tmp;
			tmp.uint = m_componentDiff;
			tmp.uint += reinterpret_cast<uintptr_t>(pSubsystem);
			return tmp.ptr;
		}
		return nullptr;
	}
};

#endif // NDLIB_PROCESS_COMPONENT_HANDLE_H

