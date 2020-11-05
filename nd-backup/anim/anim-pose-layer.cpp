/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-pose-layer.h"

#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/level/art-item-skeleton.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AnimPoseLayer::AnimPoseLayer(AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot)
: AnimLayer(kAnimLayerTypePose, pAnimTable, pOverlaySnapshot)
, m_pExternalPoseNode(nullptr)
, m_pExternalSnapshotNode(nullptr)
, m_deferredCapable(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimPoseLayer::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	AnimLayer::Relocate(deltaPos, lowerBound, upperBound);

	RelocatePointer(m_pExternalPoseNode, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pExternalSnapshotNode, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimPoseLayer::IsValid() const
{
	return (m_pExternalPoseNode != nullptr) || (m_pExternalSnapshotNode != nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimPoseLayer::GetNumFadesInProgress() const
{
	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimPoseLayer::Setup(StringId64 name, ndanim::BlendMode blendMode)
{
	ANIM_ASSERTF(false, ("This variant of AnimLayer::Setup() is not intended to be used on AnimPoseLayer"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimPoseLayer::Setup(StringId64 name, ndanim::BlendMode blendMode, const ndanim::PoseNode* pPoseNode)
{
	AnimLayer::Setup(name, blendMode);

	m_pExternalPoseNode = pPoseNode;
	m_pExternalSnapshotNode = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimPoseLayer::Setup(StringId64 name, ndanim::BlendMode blendMode, const DualSnapshotNode* pSnapshotNode)
{
	AnimLayer::Setup(name, blendMode);

	m_pExternalPoseNode = nullptr;
	m_pExternalSnapshotNode = pSnapshotNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimPoseLayer::DebugPrint(MsgOutput output, U32 priority) const
{
	STRIP_IN_FINAL_BUILD;

	if (!Memory::IsDebugMemoryAvailable())
		return;

	if (!g_animOptions.m_debugPrint.ShouldShow(GetName()))
		return;

	if (!IsValid())
		 return;
	 
	if (!g_animOptions.m_debugPrint.m_simplified)
	{
		SetColor(output, 0xFFFFFFFF);
		PrintTo(output, "-----------------------------------------------------------------------------------------\n");

		SetColor(output, 0xFF000000 | 0x0055FF55);
		PrintTo(output, "AnimPoseLayer \"%s\": [%s], Pri %d, Cur Fade: %1.2f, Des Fade: %1.2f\n",
			DevKitOnly_StringIdToString(m_name), m_blendMode == ndanim::kBlendSlerp ? "blend" : "additive", priority, GetCurrentFade(), GetDesiredFade());

		SetColor(output, 0xFFFFFFFF);
		PrintTo(output, "-----------------------------------------------------------------------------------------\n");
	}
} 

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimPoseLayer::CreateAnimCmds(const AnimCmdGenLayerContext& context, AnimCmdList* pAnimCmdList) const
{
	PROFILE(Animation, ToIceBatch);

	const bool destInstance = context.m_instanceZeroIsValid ? 1 : 0;

	if (m_pExternalPoseNode)
	{
		ANIM_ASSERT(context.m_pAnimateSkel->m_hierarchyId == m_pExternalPoseNode->m_hierarchyId);
		if (m_deferredCapable)
		{
			pAnimCmdList->AddCmd_EvaluatePoseDeferred(destInstance,
													  m_pExternalPoseNode->m_hierarchyId,
													  &m_pExternalPoseNode->m_jointPose);
		}
		else
		{
			pAnimCmdList->AddCmd_EvaluatePose(destInstance,
											  m_pExternalPoseNode->m_hierarchyId,
											  &m_pExternalPoseNode->m_jointPose);
		}
	}
	else if (m_pExternalSnapshotNode)
	{
		const ndanim::SnapshotNode* pSnapshotNode = m_pExternalSnapshotNode->GetSnapshoteNodeForHeirId(context.m_pAnimateSkel->m_hierarchyId);
		ANIM_ASSERT(pSnapshotNode->m_hierarchyId == context.m_pAnimateSkel->m_hierarchyId);

		if (m_deferredCapable)
		{
			pAnimCmdList->AddCmd_EvaluateSnapshotPoseDeferred(destInstance, pSnapshotNode);
		}
		else
		{
			pAnimCmdList->AddCmd_EvaluatePose(destInstance,
											  pSnapshotNode->m_hierarchyId,
											  &pSnapshotNode->m_jointPose);
		}
	}

	if (context.m_instanceZeroIsValid)
	{
		// blend all instances beyond the first down to the first
		pAnimCmdList->AddCmd_EvaluateBlend(0, 1, 0, ndanim::kBlendSlerp, GetCurrentFade());
	}
}
