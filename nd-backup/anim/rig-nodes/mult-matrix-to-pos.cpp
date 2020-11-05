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

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandMultMatrixToPosImpl(const HierarchyHeader* pHierarchyHeader, const MultMatrixToPosParams* pParams)
		{
			RIG_NODE_TIMER_START();

			// Extract the parameters
			const OrbisAnim::JointTransform parentJointXform = *((OrbisAnim::JointTransform*)OrbisAnim::HierarchyQuadword(pParams->m_inputLocs[0], pHierarchyHeader));
			const Mat44 parentJointMatOs = parentJointXform.GetTransform();

			const OrbisAnim::JointTransform childJointXform = *((OrbisAnim::JointTransform*)OrbisAnim::HierarchyQuadword(pParams->m_inputLocs[1], pHierarchyHeader));
			const Mat44 childJointMatOs = childJointXform.GetTransform();

			const Mat44 childJointMatLs = parentJointMatOs * Inverse(childJointMatOs);
			const Vec4 childJointTransLs = childJointMatLs.GetRow(3);

			*HierarchyFloat(pParams->m_outputLocs[0], pHierarchyHeader) = childJointTransLs.X();
			*HierarchyFloat(pParams->m_outputLocs[1], pHierarchyHeader) = childJointTransLs.Y();
			*HierarchyFloat(pParams->m_outputLocs[2], pHierarchyHeader) = childJointTransLs.Z();


			RIG_NODE_TIMER_END(RigNodeType::kMultMatrixToPos);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("MultMatrixToPos\n");
				MsgAnim("   out: %.4f %.4f %.4f \n", (float)childJointTransLs.X(), (float)childJointTransLs.Y(), (float)childJointTransLs.Z());
			}
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandMultMatrixToPos(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			MultMatrixToPosParams const* pParams = (MultMatrixToPosParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			ExecuteCommandMultMatrixToPosImpl(pHierarchyHeader, pParams);
		}
	}	//namespace CommandBlock
}	//namespace OrbisAnim

