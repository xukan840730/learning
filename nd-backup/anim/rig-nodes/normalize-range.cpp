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
#include "ndlib/anim/rig-nodes/utils.h"


namespace OrbisAnim
{
	namespace CommandBlock
	{

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandNormalizeRangeImpl(const HierarchyHeader* pHierarchyHeader, const NormalizeRangeParams* pParams)
		{
			RIG_NODE_TIMER_START();

			// Extract the parameters
 			float value = *HierarchyFloat(pParams->m_inputLoc, pHierarchyHeader);

			// The values in game are already are in the correct unit
			// Damn it!!! We have degrees as the unit to pass around rotations between SDK nodes. This should be changed after we ship T2!
			//if (!pParams->m_interpAsAngle)
			//	value = DegreesToRadians(value);

			float weight = 0.0;
			ANIM_ASSERT(pParams->m_inputMin != pParams->m_inputMax);
			{
				// Clamp values in input min and max range 
				if (pParams->m_inputMin <= pParams->m_inputMax)
					value = Max(pParams->m_inputMin, Min(value, pParams->m_inputMax));
				else
					value = Max(pParams->m_inputMax, Min(value, pParams->m_inputMin));
				// Now normalize the range
				weight = (value - pParams->m_inputMin) / (pParams->m_inputMax - pParams->m_inputMin);
				// Debug 
				//cout << "weight -> " << weight << endl;

				// Apply interpolation weight
				if (pParams->m_interpMode == 1)
					weight = SmoothStep(weight);
				else if (pParams->m_interpMode == 2)
					weight = SmoothGaussian(weight);

				// Now interpolate output min and max value and append into output into final array
				weight = LinearInterpolate(pParams->m_outputMin, pParams->m_outputMax, weight);
			}
				
			*HierarchyFloat(pParams->m_outputLoc, pHierarchyHeader) = weight;

			RIG_NODE_TIMER_END(RigNodeType::kNormalizeRange);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("NormalizeRange\n");
				MsgAnim("   out: %.4f \n", weight);
			}
		}


		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandNormalizeRange(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			NormalizeRangeParams const* pParams = (NormalizeRangeParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			ExecuteCommandNormalizeRangeImpl(pHierarchyHeader, pParams);
		}
	}	//namespace CommandBlock
}	//namespace OrbisAnim

