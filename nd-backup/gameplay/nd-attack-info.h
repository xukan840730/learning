/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

struct NdAttackInfo
{
	MutableNdGameObjectHandle m_hSourceGameObj;
	DC::AttackType m_type;

	NdAttackInfo(U32 type)
		: m_type(type)
	{
	}

	virtual ~NdAttackInfo() { }

	static void* Copy(const void* pOther, size_t bytes, Alignment align);
	virtual NdAttackInfo* Clone() const
	{
		return NDI_NEW NdAttackInfo(*this);
	}
};

enum { kBoxableTypeNdAttackInfo = DC::kBoxableTypeGameDefinedPointer1 };
extern BoxableType g_boxedNdAttackInfo;
