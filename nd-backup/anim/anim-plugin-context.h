/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include <orbisanim/immediate.h>

struct AnimExecutionContext;

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimPluginContext
{
	OrbisAnim::WorkBuffer* m_pWorkBuffer;
	OrbisAnim::ProcessingGroupContext* m_pGroupContext;
	const AnimExecutionContext* m_pContext;

	ndanim::JointParams* GetJoints(int instanceIndex) const
	{
		const uint8_t numChannelGroups = m_pGroupContext->m_pProcessingGroup->m_numChannelGroups;
		const I32 jointBufferIndex	   = instanceIndex * numChannelGroups;
		return static_cast<ndanim::JointParams*>(m_pGroupContext->m_plocChannelGroupInstance[jointBufferIndex]);
	}

	OrbisAnim::ValidBits* GetValidBits(int instanceIndex) const
	{
		const uint8_t numChannelGroups = m_pGroupContext->m_pProcessingGroup->m_numChannelGroups;
		const I32 jointBufferIndex	   = instanceIndex * numChannelGroups;
		return (&m_pGroupContext->m_pValidOutputChannels[jointBufferIndex]);
	}

	OrbisAnim::ValidBits* GetBlendGroupJointValidBits(int instanceIndex) const
	{
		ndanim::JointParams* pJointParamsLs = GetJoints(instanceIndex);

		if (!pJointParamsLs || !m_pGroupContext || m_pGroupContext->m_pProcessingGroup)
			return nullptr;

		const uint32_t numJoints = m_pGroupContext->m_pProcessingGroup->m_numAnimatedJoints;

		return OrbisAnim::GetBlendGroupJointValidBits(pJointParamsLs, numJoints);
	}
};
