/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef AP_HANDLE_TABLE_H
#define AP_HANDLE_TABLE_H

#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "corelib/containers/hashtable.h"

/// --------------------------------------------------------------------------------------------------------------- ///
typedef HashTable<ActionPackHandle, U64> ApHandleTable;

/// --------------------------------------------------------------------------------------------------------------- ///
template<>
struct HashT<ActionPackHandle>
{
	inline uintptr_t operator () (const ActionPackHandle& key) const
	{
		const uintptr_t keyVal = key.GetMgrId();
		return keyVal;
	}
};

#endif // AP_HANDLE_TABLE_H
