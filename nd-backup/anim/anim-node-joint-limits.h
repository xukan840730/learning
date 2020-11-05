/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-snapshot-node-unary.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/ik/joint-limits.h"
#include "ndlib/anim/snapshot-node-heap.h"
#include "ndlib/process/process-handles.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimStateSnapshot;

struct AnimCmdGenInfo;
struct AnimNodeDebugPrintData;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;

namespace DC
{
	struct AnimNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimNodeJointLimits : public AnimSnapshotNodeUnary
{
public:
	typedef AnimSnapshotNodeUnary ParentClass;

	FROM_ANIM_NODE_DECLARE(AnimNodeJointLimits);

	explicit AnimNodeJointLimits(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
		: AnimSnapshotNodeUnary(typeId, dcTypeId, nodeIndex)
		, m_customLimitsId(INVALID_STRING_ID_64)
	{
	}

	virtual void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
									  AnimCmdList* pAnimCmdList,
									  I32F outputInstance,
									  const AnimCmdGenInfo* pCmdGenInfo) const override;

	virtual void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;

	void SetCustomLimits(NdGameObjectHandle hGo, StringId64 customLimitsId);

	static SnapshotNodeHeap::Index SnapshotNodeFunc(AnimStateSnapshot* pSnapshot,
													const DC::AnimNode* pDcAnimNode,
													const SnapshotAnimNodeTreeParams& params,
													SnapshotAnimNodeTreeResults& results);

private:
	const JointLimits* GetCustomLimits() const;

	NdGameObjectHandle m_hOwner;
	StringId64 m_customLimitsId;
};
