/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-plugin-context.h"
#include "ndlib/anim/anim-snapshot-node-unary.h"

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeSubsystemIK : public AnimSnapshotNodeUnary
{
private:
	typedef AnimSnapshotNodeUnary ParentClass;

	StringId64 m_ikId = INVALID_STRING_ID_64;
	StringId64 m_subsystemType	= INVALID_STRING_ID_64;
	StringId64 m_subsystemLayer = INVALID_STRING_ID_64;

public:
	struct SubsystemIkPluginParams
	{
		I32 m_instanceIndex = -1;
		const AnimStateInstance* m_pInst = nullptr;
		StringId64 m_ikId = INVALID_STRING_ID_64;
		StringId64 m_subsystemType	= INVALID_STRING_ID_64;
		StringId64 m_subsystemLayer = INVALID_STRING_ID_64;
	};

	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeSubsystemIK);

	void SnapshotNode(AnimStateSnapshot* pSnapshot,
					  const DC::AnimNode* pDcAnimNode,
					  const SnapshotAnimNodeTreeParams& params,
					  SnapshotAnimNodeTreeResults& results) override;

	explicit AnimSnapshotNodeSubsystemIK(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
		: ParentClass(typeId, dcTypeId, nodeIndex)
	{
	}

	void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
							  AnimCmdList* pAnimCmdList,
							  I32F outputInstance,
							  const AnimCmdGenInfo* pCmdGenInfo) const override;

	virtual void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;

	static void AnimPluginCallback(AnimPluginContext* pPluginContext, const void* pData);
};

ANIM_NODE_DECLARE(AnimSnapshotNodeSubsystemIK);

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeSubsystemPreIK : public AnimSnapshotNodeUnary
{
private:
	typedef AnimSnapshotNodeUnary ParentClass;

	StringId64 m_ikId = INVALID_STRING_ID_64;
	StringId64 m_subsystemType = INVALID_STRING_ID_64;
	StringId64 m_subsystemLayer = INVALID_STRING_ID_64;

public:
	struct SubsystemIkPluginParams
	{
		I32 m_instanceIndex = -1;
		const AnimStateInstance* m_pInst = nullptr;
		StringId64 m_ikId = INVALID_STRING_ID_64;
		StringId64 m_subsystemType = INVALID_STRING_ID_64;
		StringId64 m_subsystemLayer = INVALID_STRING_ID_64;
	};

	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeSubsystemPreIK);

	void SnapshotNode(AnimStateSnapshot* pSnapshot,
		const DC::AnimNode* pDcAnimNode,
		const SnapshotAnimNodeTreeParams& params,
		SnapshotAnimNodeTreeResults& results) override;

	explicit AnimSnapshotNodeSubsystemPreIK(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
		: ParentClass(typeId, dcTypeId, nodeIndex)
	{
	}

	void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
		AnimCmdList* pAnimCmdList,
		I32F outputInstance,
		const AnimCmdGenInfo* pCmdGenInfo) const override;

	virtual void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;
};

ANIM_NODE_DECLARE(AnimSnapshotNodeSubsystemPreIK);
