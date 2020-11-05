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

		struct MatrixToEulersParams
		{
			U32		m_mayaNodeTypeId;
			U32		m_inputLocs[9];				// Offset from start of this struct to where the input locations are
			U32		m_outputLoc[3];				// Offset from start of this struct to where the output locations are
		};

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandMatrixToEuler(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			RIG_NODE_TIMER_START();

			HierarchyHeader const* pHierarchyHeader	= (HierarchyHeader const*)	OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
																														// param_qw0[2] is padding
			MatrixToEulersParams const* pParams	= (MatrixToEulersParams const*) OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			// Extract the parameters

			SMath::Mat44 mat;
			for (int i = 0; i < 9; i++)
			{
				mat.Set(i / 3, i % 3, *HierarchyFloat(pParams->m_inputLocs[i], pHierarchyHeader));
			}

			float eulerX, eulerY, eulerZ;
			mat.GetEulerAngles(eulerX, eulerY, eulerZ);

			*HierarchyFloat(pParams->m_outputLoc[0], pHierarchyHeader) = RadiansToDegrees(eulerX);
			*HierarchyFloat(pParams->m_outputLoc[1], pHierarchyHeader) = RadiansToDegrees(eulerY);
			*HierarchyFloat(pParams->m_outputLoc[2], pHierarchyHeader) = RadiansToDegrees(eulerZ);


			RIG_NODE_TIMER_END(RigNodeType::kMatrixToEuler);
		}

	}	//namespace CommandBlock
}	//namespace OrbisAnim

