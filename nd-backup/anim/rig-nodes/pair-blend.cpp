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
		void ExecuteCommandPairBlendImpl(const HierarchyHeader* pHierarchyHeader, const PairBlendParams* pParams)
		{
			RIG_NODE_TIMER_START();

			int locIndex = 0;

			// Extract the parameters
			float inTranslate1X = pParams->m_translate1[0];
			float inTranslate1Y = pParams->m_translate1[1];
			float inTranslate1Z = pParams->m_translate1[2];
			if (pParams->m_flags & PairBlendParams::kInTranslate1Connected)
			{
				inTranslate1X = *HierarchyFloat(pParams->m_locs[locIndex + 0], pHierarchyHeader);
				inTranslate1Y = *HierarchyFloat(pParams->m_locs[locIndex + 1], pHierarchyHeader);
				inTranslate1Z = *HierarchyFloat(pParams->m_locs[locIndex + 2], pHierarchyHeader);

				locIndex += 3;
			}
			float inTranslate2X = pParams->m_translate2[0];
			float inTranslate2Y = pParams->m_translate2[1];
			float inTranslate2Z = pParams->m_translate2[2];
			if (pParams->m_flags & PairBlendParams::kInTranslate2Connected)
			{
				inTranslate2X = *HierarchyFloat(pParams->m_locs[locIndex + 0], pHierarchyHeader);
				inTranslate2Y = *HierarchyFloat(pParams->m_locs[locIndex + 1], pHierarchyHeader);
				inTranslate2Z = *HierarchyFloat(pParams->m_locs[locIndex + 2], pHierarchyHeader);

				locIndex += 3;
			}

			float inRotate1X = pParams->m_rotateDeg1[0];
			float inRotate1Y = pParams->m_rotateDeg1[1];
			float inRotate1Z = pParams->m_rotateDeg1[2];
			if (pParams->m_flags & PairBlendParams::kInRotate1Connected)
			{
				inRotate1X = *HierarchyFloat(pParams->m_locs[locIndex + 0], pHierarchyHeader);
				inRotate1Y = *HierarchyFloat(pParams->m_locs[locIndex + 1], pHierarchyHeader);
				inRotate1Z = *HierarchyFloat(pParams->m_locs[locIndex + 2], pHierarchyHeader);

				locIndex += 3;
			}
			float inRotate2X = pParams->m_rotateDeg2[0];
			float inRotate2Y = pParams->m_rotateDeg2[1];
			float inRotate2Z = pParams->m_rotateDeg2[2];
			if (pParams->m_flags & PairBlendParams::kInRotate2Connected)
			{
				inRotate2X = *HierarchyFloat(pParams->m_locs[locIndex + 0], pHierarchyHeader);
				inRotate2Y = *HierarchyFloat(pParams->m_locs[locIndex + 1], pHierarchyHeader);
				inRotate2Z = *HierarchyFloat(pParams->m_locs[locIndex + 2], pHierarchyHeader);

				locIndex += 3;
			}

			float inWeight = pParams->m_weight;
			if (pParams->m_flags & PairBlendParams::kInWeightConnected)
			{
				inWeight = *HierarchyFloat(pParams->m_locs[locIndex + 0], pHierarchyHeader);

				locIndex += 1;
			}

			// ----------------------------------------------------------------
			// Input plugs
			// ----------------------------------------------------------------
// 			double dWeight = dataBlock.inputValue(weight).asDouble();
			Point vInTranslate1 = Point(inTranslate1X, inTranslate1Y, inTranslate1Z);
			Point vInTranslate2 = Point(inTranslate2X, inTranslate2Y, inTranslate2Z);

			Quat qRotate1 = Quat(DegreesToRadians(inRotate1X),
								 DegreesToRadians(inRotate1Y),
								 DegreesToRadians(inRotate1Z),
								 Quat::RotationOrder::kXYZ);
			Quat qRotate2 = Quat(DegreesToRadians(inRotate2X),
								 DegreesToRadians(inRotate2Y),
								 DegreesToRadians(inRotate2Z),
								 Quat::RotationOrder::kXYZ);

			// now interpolate values
			Vector vOutTranslate = LinearInterpolate3D(vInTranslate1, vInTranslate2, inWeight);
			Quat qOutRotate = Slerp(qRotate1, qRotate2, inWeight);

			float eulerValues[3];
			qOutRotate.GetEulerAngles(eulerValues[0], eulerValues[1], eulerValues[2], Quat::RotationOrder::kXYZ);

			//----------------------------------------------------------------
			// Set output plugs
			//----------------------------------------------------------------
			float outTranslateX = vOutTranslate.Get(0);
			float outTranslateY = vOutTranslate.Get(1);
			float outTranslateZ = vOutTranslate.Get(2);
			float outRotateX = RadiansToDegrees(eulerValues[0]);
			float outRotateY = RadiansToDegrees(eulerValues[1]);
			float outRotateZ = RadiansToDegrees(eulerValues[2]);

			if (pParams->m_flags & PairBlendParams::kOutTranslateConnected)
			{
				*HierarchyFloat(pParams->m_locs[locIndex + 0], pHierarchyHeader) = outTranslateX;
				*HierarchyFloat(pParams->m_locs[locIndex + 1], pHierarchyHeader) = outTranslateY;
				*HierarchyFloat(pParams->m_locs[locIndex + 2], pHierarchyHeader) = outTranslateZ;

				locIndex += 3;
			}
			if (pParams->m_flags & PairBlendParams::kOutRotateConnected)
			{
				*HierarchyFloat(pParams->m_locs[locIndex + 0], pHierarchyHeader) = outRotateX;
				*HierarchyFloat(pParams->m_locs[locIndex + 1], pHierarchyHeader) = outRotateY;
				*HierarchyFloat(pParams->m_locs[locIndex + 2], pHierarchyHeader) = outRotateZ;

				locIndex += 3;
			}

			RIG_NODE_TIMER_END(RigNodeType::kPairBlend);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("PairBlend\n");
				MsgAnim("   outX: %.4f deg\n", outRotateX);
				MsgAnim("   outY: %.4f deg\n", outRotateY);
				MsgAnim("   outZ: %.4f deg\n", outRotateZ);
			}
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandPairBlend(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			PairBlendParams const* pParams = (PairBlendParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			ExecuteCommandPairBlendImpl(pHierarchyHeader, pParams);
		}

	}	//namespace CommandBlock
}	//namespace OrbisAnim

