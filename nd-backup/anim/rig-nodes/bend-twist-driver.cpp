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
		void ExecuteCommandBendTwistDriverImpl(const HierarchyHeader* pHierarchyHeader, const BendTwistDriverParams* pParams)
		{
			RIG_NODE_TIMER_START();

			// Extract the parameters
 			const ndanim::JointParams* pInJointParams = (const ndanim::JointParams*)HierarchyQuadword(pParams->m_inputSqtLoc, pHierarchyHeader);

			// ----------------------------------------------------------------
			// Input plugs
			// ----------------------------------------------------------------
//			short nMode = dataBlock.inputValue(mode).asShort();
//			short nRotateOrder = dataBlock.inputValue(inRotateOrder).asShort();
//			double dWeight = dataBlock.inputValue(weight).asDouble();

// 			MVector		vAim = dataBlock.inputValue(aimVector).asVector();
// 			MMatrix		mLocal = dataBlock.inputValue(inMatrix).asMatrix();
			Vector vAim = Vector(pParams->m_aimVector);
			Quat qLocal = pInJointParams->m_quat;

			// Generate bind pose rotation from joint orient, later we will inverse this values to get clean rotation values
			// Joint orient is always MEulerRotation::kXYZ.
//			Vector vOrient = dataBlock.inputValue(inJointOrient).asVector();
			Quat qOrient = Quat(pParams->m_jointOrient[0], pParams->m_jointOrient[1], pParams->m_jointOrient[2], (Quat::RotationOrder)pParams->m_rotateOrder);

			// Joint matrix includes joint orient, minus joint orient will result only local rotation
			Quat qRotate = Conjugate(qOrient) * qLocal;

			// Multiply aim vector with rotation to project offset aim vector
			Vector vTarget = Rotate(qRotate, vAim);

			// Extract bend rotation of input rotation
			// Input rotation minus bend rotation, the result rotation is equivalent to twist rotation
			Quat qBend = RotationBetween(vAim.GetVec4(), vTarget.GetVec4());
			Quat qTwist = Conjugate(qBend) * qRotate;

			// Twist - bend mode
			if (pParams->m_driverMode == 1) {
				// Parent joint is now twisting instead bend, so minus twist rotation to final bend rotation.
				// This is same behavior as aimConstriant worldUpType set to "None"
				qBend = Conjugate(qTwist) * qRotate;
				// Add joint orient to twist calculation
				qTwist = (qOrient * qTwist) * Conjugate(qOrient);
			}

			// Blend weight values, may not needed for game engine?
			qBend = Slerp(qRotate, qBend, pParams->m_weight);
			qTwist = Slerp(Quat(kIdentity), qTwist, pParams->m_weight);

			float eulerBendValues[3];
			qBend.GetEulerAngles(eulerBendValues[0], eulerBendValues[1], eulerBendValues[2], (Quat::RotationOrder)pParams->m_rotateOrder);	// Same as Maya's RotationOrder enum
//			MEulerRotation eBend = .reorder(getRotationOrder(nRotateOrder));

			float eulerTwistValues[3];
			qTwist.GetEulerAngles(eulerTwistValues[0], eulerTwistValues[1], eulerTwistValues[2], (Quat::RotationOrder)pParams->m_rotateOrder);	// Same as Maya's RotationOrder enum
//			MEulerRotation eTwist = .reorder(getRotationOrder(nRotateOrder));

			//----------------------------------------------------------------
			// Set output plugs
			//----------------------------------------------------------------
			float degreesBendX = RadiansToDegrees(eulerBendValues[0]);
			float degreesBendY = RadiansToDegrees(eulerBendValues[1]);
			float degreesBendZ = RadiansToDegrees(eulerBendValues[2]);
			float degreesTwistX = RadiansToDegrees(eulerTwistValues[0]);
			float degreesTwistY = RadiansToDegrees(eulerTwistValues[1]);
			float degreesTwistZ = RadiansToDegrees(eulerTwistValues[2]);
			*HierarchyFloat(pParams->m_outputBendXLoc, pHierarchyHeader) = degreesBendX;
			*HierarchyFloat(pParams->m_outputBendYLoc, pHierarchyHeader) = degreesBendY;
			*HierarchyFloat(pParams->m_outputBendZLoc, pHierarchyHeader) = degreesBendZ;
			*HierarchyFloat(pParams->m_outputTwistXLoc, pHierarchyHeader) = degreesTwistX;
			*HierarchyFloat(pParams->m_outputTwistYLoc, pHierarchyHeader) = degreesTwistY;
			*HierarchyFloat(pParams->m_outputTwistZLoc, pHierarchyHeader) = degreesTwistZ;

			RIG_NODE_TIMER_END(RigNodeType::kBendTwistDriver);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("BendTwistDriver\n");
				MsgAnim("   BendX: %.4f deg\n", degreesBendX);
				MsgAnim("   BendY: %.4f deg\n", degreesBendY);
				MsgAnim("   BendZ: %.4f deg\n", degreesBendZ);
				MsgAnim("   TwistX: %.4f deg\n", degreesTwistX);
				MsgAnim("   TwistY: %.4f deg\n", degreesTwistY);
				MsgAnim("   TwistZ: %.4f deg\n", degreesTwistZ);
			}
		}

		void ExecuteCommandBendTwistDriver(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			BendTwistDriverParams const* pParams = (BendTwistDriverParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			ExecuteCommandBendTwistDriverImpl(pHierarchyHeader, pParams);
		}
	}	//namespace CommandBlock
}	//namespace OrbisAnim

