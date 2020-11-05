/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-node-library.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/util/type-factory.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AnimNodeLibrary g_animNodeLibrary;

/// --------------------------------------------------------------------------------------------------------------- ///
AnimNodeLibrary::Record::Record(StringId64 typeId, StringId64 dcTypeId, SnapshotAnimNodeTreeFunc* snapshotFunc)
{
	g_animNodeLibrary.Register(dcTypeId, typeId, snapshotFunc);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimNodeLibrary::Register(StringId64 dcTypeId, StringId64 typeFactoryId, SnapshotAnimNodeTreeFunc* snapshotFunc)
{
	if (!m_isConstructed)
	{
		memset(m_registrar, 0, sizeof(RegisteredNode)*kMaxKnownNodes);
		m_maxNodeSize = 0;
		m_isConstructed = true;
	}

	const StringId64 existingId = LookupTypeIdFromDcType(dcTypeId);
	if (existingId != INVALID_STRING_ID_64)
	{
		ANIM_ASSERT(existingId == typeFactoryId);
		return true;
	}
	
	const TypeFactory::Record* pRec = g_typeFactory.GetRecord(typeFactoryId);
	if (!pRec)
		return false;

	I32F emptySlot = -1;
	for (U32F i = 0; i < kMaxKnownNodes; ++i)
	{
		if (m_registrar[i].m_typeFactoryId == INVALID_STRING_ID_64)
		{
			emptySlot = i;
			break;
		}
	}

	ANIM_ASSERT(emptySlot >= 0);
	if (emptySlot < 0)
		return false;

	m_registrar[emptySlot].m_typeFactoryId = typeFactoryId;
	m_registrar[emptySlot].m_dcTypeId = dcTypeId;
	m_registrar[emptySlot].m_allocSize = pRec->GetAllocSize();
	m_registrar[emptySlot].m_snapshotFunc = snapshotFunc;

	m_maxNodeSize = Max(m_maxNodeSize, m_registrar[emptySlot].m_allocSize);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimNodeLibrary::LookupTypeIdFromDcType(StringId64 dcTypeId) const
{
	for (U32F i = 0; i < kMaxKnownNodes; ++i)
	{
		if (m_registrar[i].m_dcTypeId == dcTypeId)
		{
			return m_registrar[i].m_typeFactoryId;
		}
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimNodeLibrary::LookupDcTypeFromTypeId(StringId64 typeFactoryId) const
{
	for (U32F i = 0; i < kMaxKnownNodes; ++i)
	{
		if (m_registrar[i].m_typeFactoryId == typeFactoryId)
		{
			return m_registrar[i].m_dcTypeId;
		}
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNode* AnimNodeLibrary::CreateFromTypeId(StringId64 typeFactoryId, void* pMem, SnapshotNodeHeap::Index index) const
{
	ANIM_ASSERT(typeFactoryId != INVALID_STRING_ID_64);
	if (INVALID_STRING_ID_64 == typeFactoryId)
		return nullptr;

	const TypeFactory::Record* pRec = g_typeFactory.GetRecord(typeFactoryId);
	ANIM_ASSERT(pRec);
	if (!pRec)
		return nullptr;

	ANIM_ASSERT(pRec->IsKindOf(g_type_AnimSnapshotNode));

	AnimSnapshotNode* pRet = static_cast<AnimSnapshotNode*>(g_typeFactory.Create(typeFactoryId, &index, pMem));
	ANIM_ASSERTF(pRet, ("Ran out of snapshot nodes! Couldn't create '%s'", DevKitOnly_StringIdToString(typeFactoryId)));
	return pRet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNode* AnimNodeLibrary::CreateFromDcType(StringId64 dcTypeId, void* pMem, SnapshotNodeHeap::Index index) const
{
	const StringId64 typeId = LookupTypeIdFromDcType(dcTypeId);
	return CreateFromTypeId(typeId, pMem, index);
}

/// --------------------------------------------------------------------------------------------------------------- ///
size_t AnimNodeLibrary::GetSizeForTypeId(StringId64 typeFactoryId) const
{
	const TypeFactory::Record* pRec = g_typeFactory.GetRecord(typeFactoryId);

	if (pRec)
	{
		return pRec->GetAllocSize();
	}

	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
size_t AnimNodeLibrary::GetMaxNodeSize() const
{
	return m_maxNodeSize;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimNodeLibrary::SnapshotAnimNodeTreeFunc* AnimNodeLibrary::GetSnapshotFuncDcType(StringId64 dcTypeId) const
{
	for (U32F i = 0; i < kMaxKnownNodes; ++i)
	{
		if (m_registrar[i].m_dcTypeId == dcTypeId)
		{
			return m_registrar[i].m_snapshotFunc;
		}
	}

	return nullptr;
}
