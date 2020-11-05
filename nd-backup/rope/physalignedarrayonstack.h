/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

/*! \file physalignedarrayonstack.h
    \author Sergiy Migdalskiy [mailto:sergiy_migdalskiy@naughtydog.com] 
    \brief .

 */

#ifndef NDPHYS_ALIGNED_ARRAY_ON_STACK_H 
#define NDPHYS_ALIGNED_ARRAY_ON_STACK_H 

#include "ndlib/memory/memory.h"
#include "corelib/memory/scoped-temp-allocator.h"

template< typename T >
class AlignedArrayOnStack
{
public:
	
	enum 
	{
		kDefaultAlignment = 16
	};

	AlignedArrayOnStack(U32 count, const char *sourceFile, U32F sourceLine, const char *sourceFunc)
		: m_scopedAlloc(sourceFile, sourceLine, sourceFile)
	{ 
		m_pData = nullptr;
		Allocate(count, kDefaultAlignment);
	}

	~AlignedArrayOnStack()
	{
		ASSERT(!m_pData);
	}

	void Allocate(U32 count, U32 alignment = kDefaultAlignment)
	{
		ASSERT(m_pData == nullptr);
		m_nSize = sizeof(T)*count;
		void* pDataRaw = NDI_NEW (Alignment(alignment)) U8[m_nSize];
		m_pData = reinterpret_cast<T*>(pDataRaw);
	}

	void Deallocate()
	{
		m_pData = nullptr;
	}
	void Construct()
	{
		m_pData = nullptr;
	}

	bool IsEmpty() const {return !m_pData;}
	U32F GetCapacity()const {return m_nSize / sizeof(T);}

#ifdef _DEBUG
	T& operator [] (U32 i)
	{
		ASSERT(i < m_nSize / sizeof(T));
		return m_pData[i];
	}
#else
	operator T* ()
	{
		return m_pData;
	}
#endif

	T* GetPtr()		{return m_pData;}

protected:
	ScopedTempAllocator m_scopedAlloc;
	T* m_pData; 
	U32 m_nSize;
};

#endif // NDPHYS_ALIGNED_ARRAY_ON_STACK_H 

