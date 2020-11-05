/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIM_NODE_LIBRARY_H
#define ANIM_NODE_LIBRARY_H

#include "ndlib/anim/snapshot-node-heap.h"

class AnimSnapshotNode;

class AnimStateSnapshot;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;

namespace DC
{
	struct AnimNode;
}

class AnimNodeLibrary
{
public:
	typedef SnapshotNodeHeap::Index SnapshotAnimNodeTreeFunc(AnimStateSnapshot* pSnapshot, const DC::AnimNode* pDcAnimNode, const SnapshotAnimNodeTreeParams& params, SnapshotAnimNodeTreeResults& results);

	struct Record
	{
		Record(StringId64 typeId, StringId64 dcTypeId, SnapshotAnimNodeTreeFunc* snapshotFunc);
	};

	bool Register(StringId64 dcTypeId, StringId64 typeFactoryId, SnapshotAnimNodeTreeFunc* snapshotFunc);

	StringId64 LookupTypeIdFromDcType(StringId64 dcTypeId) const;
	StringId64 LookupDcTypeFromTypeId(StringId64 typeFactoryId) const;
	
	AnimSnapshotNode* CreateFromTypeId(StringId64 typeFactoryId, void* pMem, SnapshotNodeHeap::Index index) const;
	AnimSnapshotNode* CreateFromDcType(StringId64 dcTypeId, void* pMem, SnapshotNodeHeap::Index index) const;

	SnapshotAnimNodeTreeFunc* GetSnapshotFuncDcType(StringId64 dcTypeId) const;

	size_t GetSizeForTypeId(StringId64 typeFactoryId) const;
	size_t GetMaxNodeSize() const;

private:
	enum { kMaxKnownNodes = 32 };

	struct RegisteredNode
	{
		StringId64 m_dcTypeId;
		StringId64 m_typeFactoryId;
		size_t m_allocSize;
		SnapshotAnimNodeTreeFunc* m_snapshotFunc;
	};

	RegisteredNode m_registrar[kMaxKnownNodes]; 
	size_t m_maxNodeSize;
	bool m_isConstructed;
};

extern AnimNodeLibrary g_animNodeLibrary;

#endif // ANIM_NODE_LIBRARY_H
