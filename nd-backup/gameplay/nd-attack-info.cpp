/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-attack-info.h"


// Define a custom Boxable<T> copy function that actually uses a virtual Clone() function.
// This ONLY WORKS because we always construct our BoxedValue using an AttackInfo*, never
// a pointer to a derived type.  So the BoxedValue ctor always picks up the correct type
// (Boxable<AttackInfo>) and calls AttackInfo::Copy(), which then calls the virtual.

//static
void* NdAttackInfo::Copy(const void* pOther, size_t, Alignment)
{
	if (pOther)
	{
		const NdAttackInfo* pOtherInfo = reinterpret_cast<const NdAttackInfo*>(pOther);
		return pOtherInfo->Clone();
	}
	return nullptr;
}

DEFINE_BOXABLE_CUSTOM(NdAttackInfo, kBoxableTypeNdAttackInfo, NdAttackInfo::Copy, kAlign4);