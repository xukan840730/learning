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
		struct AddFloatsParams
		{
			U32	m_mayaNodeTypeId;
			U32 m_numEntries;

			struct Entry
			{
				float m_input2;
				Location m_inputLoc[2];				// Offset from start of this struct to where the input locations are
				Location m_outputLoc;				// Offset from start of this struct to where the output locations are
			};
		};

		void ExecuteCommandAddFloat(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			RIG_NODE_TIMER_START();

			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			const AddFloatsParams* pParams = (const AddFloatsParams*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);
			const AddFloatsParams::Entry* pEntries = (const AddFloatsParams::Entry*)(pParams + 1);

			for (unsigned int iEntry = 0; iEntry != pParams->m_numEntries; ++iEntry)
			{
				// Extract the parameters
				float value1 = *HierarchyFloat(pEntries[iEntry].m_inputLoc[0], pHierarchyHeader);
				float value2 = pEntries[iEntry].m_inputLoc[1] != Location::kInvalid
								   ? *HierarchyFloat(pEntries[iEntry].m_inputLoc[1], pHierarchyHeader)
								   : pEntries[iEntry].m_input2;
				*HierarchyFloat(pEntries[iEntry].m_outputLoc, pHierarchyHeader) = value1 + value2;
			}
			RIG_NODE_TIMER_END(RigNodeType::kAddDoubleLinear);

		}


	}	//namespace CommandBlock
}	//namespace OrbisAnim

