/*
 * Copyright (c) 2003-2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include <orbisanim/commandblock.h>
#include <orbisanim/commanddebug.h>
#include <orbisanim/commands.h>
#include <orbisanim/animhierarchy.h>
#include <orbisanim/joints.h>

#include "corelib/util/timer.h"
#include "ndlib/anim/rig-nodes/rig-nodes.h"


namespace OrbisAnim
{
	namespace CommandBlock
	{

		struct SetRangeParams
		{
			U32		m_mayaNodeTypeId;

			float m_rangeMin;
			float m_rangeMax;
			float m_outMin;
			float m_outMax;

			U32 m_inputLoc;				// Offset from start of this struct to where the input locations are
			U32 m_outputLoc;				// Offset from start of this struct to where the output locations are
		};

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandSetRange(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			RIG_NODE_TIMER_START();

			HierarchyHeader const* pHierarchyHeader	= (HierarchyHeader const*)	OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
																														// param_qw0[2] is padding
			SetRangeParams const* pParams	= (SetRangeParams const*) OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			// Extract the parameters
			float value = *HierarchyFloat(pParams->m_inputLoc, pHierarchyHeader);
			float scaledValue = LerpScale(pParams->m_rangeMin, pParams->m_rangeMax, pParams->m_outMin, pParams->m_outMax, value);
			*HierarchyFloat(pParams->m_outputLoc, pHierarchyHeader) = scaledValue;

			RIG_NODE_TIMER_END(RigNodeType::kSetRange);
		}

	}	//namespace CommandBlock
}	//namespace OrbisAnim

