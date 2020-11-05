/*
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/list-array.h"

#include "ndlib/anim/anim-snapshot-node-unary.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/snapshot-node-heap.h"
#include "ndlib/scriptx/h/anim-ik-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimStateSnapshot;
class JacobianMap;
struct AnimCmdGenInfo;
struct AnimNodeDebugPrintData;
struct AnimPluginContext;
struct JacobianIkInstance;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;

namespace DC
{
	struct AnimInfoCollection;
	struct AnimNode;
	struct ScriptLambda;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(16) DCIkGoalWrapper
{
	bool m_valid;

	union
	{
		DC::IkGoalPosition	m_posGoal;
		DC::IkGoalRotation	m_rotGoal;
		DC::IkGoalLookAt	m_lookAtGoal;
		DC::IkGoalLookDir	m_lookDirGoal;
	};
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeIK : public AnimSnapshotNodeUnary
{
private:
	typedef AnimSnapshotNodeUnary ParentClass;

public:
	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeIK);

	static SnapshotNodeHeap::Index SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
													const DC::AnimNode* pNode,
													const SnapshotAnimNodeTreeParams& params,
													SnapshotAnimNodeTreeResults& results);

	static void AnimPluginCallback(AnimPluginContext* pPluginContext, const void* pData);

	explicit AnimSnapshotNodeIK(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
		: ParentClass(typeId, dcTypeId, nodeIndex)
		, m_pJocobianMap(nullptr)
		, m_pIkInstance(nullptr)
		, m_pGoalFuncs(nullptr)
		, m_pBlendFunc(nullptr)
		, m_blend(1.0f)
	{
	}

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	bool RefreshPhasesAndBlends(AnimStateSnapshot* pSnapshot,
								float statePhase,
								bool topTrackInstance,
								const DC::AnimInfoCollection* pInfoCollection) override;
	void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
							  AnimCmdList* pAnimCmdList,
							  I32F outputInstance,
							  const AnimCmdGenInfo* pCmdGenInfo) const override;
	void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;

	void RefreshGoalsAndBlend(AnimStateSnapshot* pSnapshot,
							  float statePhase,
							  const DC::AnimInfoCollection* pInfoCollection);

private:
	static CONST_EXPR size_t kBufferSize = 3 * 1024;

	void InitIk(const DC::IkNodeConfig* pConfig, DC::AnimInfoCollection& info, const SnapshotAnimNodeTreeParams& params);

	StringId64 m_configName;
	JacobianMap* m_pJocobianMap;
	JacobianIkInstance* m_pIkInstance;
	const DcArray<const DC::ScriptLambda*>* m_pGoalFuncs;
	const DC::ScriptLambda* m_pBlendFunc;
	float m_blend;
	bool m_flipped;
	mutable ListArray<DCIkGoalWrapper> m_goals;

	U8 m_memory[kBufferSize];
};

ANIM_NODE_DECLARE(AnimSnapshotNodeIK);
