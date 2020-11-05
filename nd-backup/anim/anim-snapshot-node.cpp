/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-snapshot-node.h"

#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-node-library.h"
#include "ndlib/memory/relocatable-heap.h"

#include "gamelib/level/art-item-anim.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER_ABSTRACT_BASE(AnimSnapshotNode);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNode::AnimSnapshotNode(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
	: m_typeId(typeId), m_dcType(dcTypeId), m_nodeIndex(nodeIndex), m_pHeapRec(nullptr)
{
	m_pType = g_typeFactory.GetRecord(typeId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNode::ReleaseNodeRecursive(SnapshotNodeHeap* pHeap) const
{
	pHeap->ReleaseNode(m_nodeIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNode::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pHeapRec, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AnimSnapshotNode::GetName() const
{
	return DevKitOnly_StringIdToString(m_typeId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimSnapshotNode* AnimSnapshotNode::FindFirstNodeOfKind(const AnimStateSnapshot* pSnapshot,
															  StringId64 typeId) const
{
	if (m_typeId == typeId)
		return this;

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNode::VisitNodesOfKind(AnimStateSnapshot* pSnapshot,
										StringId64 typeId,
										SnapshotVisitNodeFunc visitFunc,
										uintptr_t userData)
{
	VisitNodesOfKindInternal(pSnapshot, typeId, visitFunc, nullptr, 1.0f, userData);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNode::VisitNodesOfKindInternal(AnimStateSnapshot* pSnapshot,
												StringId64 typeId,
												SnapshotVisitNodeFunc visitFunc,
												AnimSnapshotNodeBlend* pParentBlendNode,
												float combinedBlend,
												uintptr_t userData)
{
	bool result = true;
	
	const bool rightKind = (typeId == INVALID_STRING_ID_64) || IsKindOf(typeId);
	if (rightKind && visitFunc)
	{
		result = visitFunc(pSnapshot, this, pParentBlendNode, combinedBlend, userData);
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNode::VisitNodesOfKind(const AnimStateSnapshot* pSnapshot,
										StringId64 typeId,
										SnapshotConstVisitNodeFunc visitFunc,
										uintptr_t userData) const
{
	VisitNodesOfKindInternal(pSnapshot, typeId, visitFunc, nullptr, 1.0f, userData);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNode::VisitNodesOfKindInternal(const AnimStateSnapshot* pSnapshot,
												StringId64 typeId,
												SnapshotConstVisitNodeFunc visitFunc,
												const AnimSnapshotNodeBlend* pParentBlendNode,
												float combinedBlend,
												uintptr_t userData) const
{
	bool result = true;

	const bool rightKind = (typeId == INVALID_STRING_ID_64) || IsKindOf(typeId);
	if (rightKind && visitFunc)
	{
		result = visitFunc(pSnapshot, this, pParentBlendNode, combinedBlend, userData);
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNode::ForAllAnimations(const AnimStateSnapshot* pSnapshot,
										AnimationVisitFunc visitFunc,
										uintptr_t userData) const
{
	ForAllAnimationsInternal(pSnapshot, visitFunc, 1.0f, userData);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNode::ForAllAnimationsInternal(const AnimStateSnapshot* pSnapshot,
												AnimationVisitFunc visitFunc,
												float combinedBlend,
												uintptr_t userData) const
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index AnimSnapshotNode::Clone(SnapshotNodeHeap* pDestHeap, const SnapshotNodeHeap* pSrcHeap) const
{
	SnapshotNodeHeap::Index outIndex;
	AnimSnapshotNode* pNewNode = pDestHeap->AllocateNode(m_typeId, &outIndex);
	ANIM_ASSERT(pNewNode);

	const size_t nodeSide = g_animNodeLibrary.GetSizeForTypeId(m_typeId);

	// This is not the safest way to copy this
	memcpy((void*)pNewNode, (const void*)this, nodeSide);

	pNewNode->m_nodeIndex = outIndex;

	return outIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNode::GetHeapUsage(const SnapshotNodeHeap* pSrcHeap, U32& outMem, U32& outNumNodes) const
{
	outNumNodes++;
	U32 thisNodeSize = g_animNodeLibrary.GetSizeForTypeId(m_typeId);
	outMem += AlignSize(thisNodeSize, RelocatableHeap::GetAlignment());
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAnimDataEval::IAnimData* AnimSnapshotNode::EvaluateNode(IAnimDataEval* pEval, const AnimStateSnapshot* pSnapshot) const
{
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNode::CollectContributingAnimIds(const AnimStateSnapshot* pSnapshot,
												  float blend,
												  AnimIdCollection* pCollection) const
{
	AnimCollection anims;
	CollectContributingAnims(pSnapshot, blend, &anims);

	for (U32 i = 0; i < anims.m_animCount; ++i)
	{
		const StringId64 animName = anims.m_animArray[i]->GetNameId();

		if (pCollection->m_animCount < AnimIdCollection::kMaxAnimIds)
		{
			pCollection->m_animArray[pCollection->m_animCount] = animName;
			++pCollection->m_animCount;
		}
	}
}
