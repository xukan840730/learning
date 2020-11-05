/*
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/snapshot-node-heap.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimStateSnapshot;
class ArtItemSkeleton;
struct AnimCmdGenInfo;
struct AnimNodeDebugPrintData;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;

namespace DC
{
	struct AnimNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeEmptyPose : public AnimSnapshotNode
{
public:
	AnimSnapshotNodeEmptyPose(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex);

	static const size_t kErrorStringBufLen = 128;

	virtual void SnapshotNode(AnimStateSnapshot* pSnapshot,
							  const DC::AnimNode* pNode,
							  const SnapshotAnimNodeTreeParams& params,
							  SnapshotAnimNodeTreeResults& results) override;

	virtual void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
									  AnimCmdList* pAnimCmdList,
									  I32F outputInstance,
									  const AnimCmdGenInfo* pCmdGenInfo) const override;

	virtual ndanim::ValidBits
	GetValidBits(const ArtItemSkeleton* pSkel, const AnimStateSnapshot* pSnapshot, U32 iGroup) const override;

	virtual void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;

	void SetErrorString(const char* errorString);

	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeEmptyPose);

private:
	StringId64 m_fromGestureId;

	char m_errorString[kErrorStringBufLen];
};

ANIM_NODE_DECLARE(AnimSnapshotNodeEmptyPose);
