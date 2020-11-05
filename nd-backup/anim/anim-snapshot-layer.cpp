/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-snapshot-layer.h"

#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/nd-anim-util.h"

#include "gamelib/level/art-item-skeleton.h"

#include <orbisanim/util.h>

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotLayer::AnimSnapshotLayer(AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot)
: AnimLayer(kAnimLayerTypeSnapshot, pAnimTable, pOverlaySnapshot)
, m_pExternalSnapshotNode(nullptr)
{
}

	/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotLayer::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	AnimLayer::Relocate(deltaPos, lowerBound, upperBound);

	RelocatePointer(m_pExternalSnapshotNode, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotLayer::IsValid() const
{
	return m_pExternalSnapshotNode != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimSnapshotLayer::GetNumFadesInProgress() const
{
	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotLayer::Setup(StringId64 name, ndanim::BlendMode blendMode)
{
	ANIM_ASSERTF(false, ("This variant of AnimLayer::Setup() is not intended to be used on AnimSnapshotLayer"));
	Setup(name, blendMode, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotLayer::Setup(StringId64 name, ndanim::BlendMode blendMode, DualSnapshotNode* snapshotNode)
{
	AnimLayer::Setup(name, blendMode);

	m_pExternalSnapshotNode = snapshotNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotLayer::DebugPrint(MsgOutput output, U32 priority) const
{
	if (!Memory::IsDebugMemoryAvailable())
		return;

	if (!g_animOptions.m_debugPrint.ShouldShow(GetName()))
		return;

	if (!IsValid())
		 return;

	if (!g_animOptions.m_debugPrint.m_simplified)
	{
		SetColor(output, 0xFF000000 | 0x00FFFFFF);
		PrintTo(output, "-----------------------------------------------------------------------------------------\n");

		SetColor(output, 0xFF000000 | 0x0055FF55);
		PrintTo(output, "AnimSnapshotLayer \"%s\": Pri %d, Cur Fade: %1.2f, Des Fade: %1.2f\n",
			DevKitOnly_StringIdToString(m_name), priority, GetCurrentFade(), GetDesiredFade());

		SetColor(output, 0xFF000000 | 0x00FFFFFF);
		PrintTo(output, "-----------------------------------------------------------------------------------------\n");
	}
} 

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotLayer::CreateAnimCmds(const AnimCmdGenLayerContext& context, AnimCmdList* pAnimCmdList) const
{
	ANIM_ASSERT(m_pExternalSnapshotNode);

	const U32 hierarchyId = context.m_pAnimateSkel->m_hierarchyId;
	const ndanim::SnapshotNode* pSnapshotNode = m_pExternalSnapshotNode->GetSnapshoteNodeForHeirId(hierarchyId);

	ANIM_ASSERTF(pSnapshotNode, ("Snapshot Retargeting not implemented"));

	if (!pSnapshotNode)
	{
		return;
	}

	// We need a base layer to take a snapshot.
	if (context.m_instanceZeroIsValid)
	{
		pAnimCmdList->AddCmd_EvaluateSnapshot(0,
											  pSnapshotNode->m_hierarchyId,
											  &pSnapshotNode->m_jointPose);
	}
	else
	{
		const U32F numGroups = OrbisAnim::AnimHierarchy_GetNumChannelGroups(context.m_pAnimateSkel->m_pAnimHierarchy);
		for (U32F groupIndex = 0; groupIndex < numGroups; ++groupIndex)
		{
			pSnapshotNode->m_jointPose.m_pValidBitsTable[groupIndex].Clear();
		}
	}
}
