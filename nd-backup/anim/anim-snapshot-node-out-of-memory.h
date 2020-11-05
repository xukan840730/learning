/*
 * Copyright (c) 2013 Naughty Dog, Inc.
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
struct AnimCameraCutInfo;
struct AnimCmdGenInfo;
struct AnimNodeDebugPrintData;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;
struct SnapshotEvaluateParams;

namespace DC
{
	struct AnimInfoCollection;
	struct AnimNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeOutOfMemory : public AnimSnapshotNode
{
public:
	static AnimSnapshotNodeOutOfMemory& GetSingleton() { return sm_singleton; }

	explicit AnimSnapshotNodeOutOfMemory(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
		: AnimSnapshotNode(typeId, dcTypeId, nodeIndex)
	{
	}

	//
	// Construction, update, etc
	//
	virtual void SnapshotNode(AnimStateSnapshot* pSnapshot,
							  const DC::AnimNode* pNode,
							  const SnapshotAnimNodeTreeParams& params,
							  SnapshotAnimNodeTreeResults& results) override;
	virtual bool RefreshAnims(AnimStateSnapshot* pSnapshot) override;
	virtual void StepNode(AnimStateSnapshot* pSnapshot,
						  float deltaTime,
						  const DC::AnimInfoCollection* pInfoCollection) override;
	virtual bool RefreshPhasesAndBlends(AnimStateSnapshot* pSnapshot,
										float statePhase,
										bool topTrackInstance,
										const DC::AnimInfoCollection* pInfoCollection) override;
	virtual U8 RefreshBreadth(AnimStateSnapshot* pSnapshot) override { return 1; }
	virtual void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
									  AnimCmdList* pAnimCmdList,
									  I32F outputInstance,
									  const AnimCmdGenInfo* pCmdGenInfo) const override;
	virtual void ReleaseNodeRecursive(SnapshotNodeHeap* pHeap) const override;

	//
	// Various queries
	//
	virtual ndanim::ValidBits GetValidBits(const ArtItemSkeleton* pSkel,
										   const AnimStateSnapshot* pSnapshot,
										   U32 iGroup) const override;
	virtual bool IsAdditive(const AnimStateSnapshot* pSnapshot) const override;

	virtual bool HasLoopingAnimation(const AnimStateSnapshot* pSnapshot) const override;

	virtual void GetTriggeredEffects(const AnimStateSnapshot* pSnapshot,
									 EffectUpdateStruct& effectParams,
									 float nodeBlend,
									 const AnimInstance* pInstance) const override;

	virtual void CollectContributingAnims(const AnimStateSnapshot* pSnapshot,
										  float blend,
										  AnimCollection* pCollection) const override
	{
	}

	//
	// Debug Nonsense
	//
	virtual void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;
	virtual void DebugSubmitAnimPlayCount(const AnimStateSnapshot* pSnapshot) const override;

	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeOutOfMemory);

private:
	static AnimSnapshotNodeOutOfMemory sm_singleton;
};

ANIM_NODE_DECLARE(AnimSnapshotNodeOutOfMemory);
