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
		void ExecuteCommandAngleDriverImpl(const HierarchyHeader* pHierarchyHeader, const AngleDriverParams* pParams)
		{
			RIG_NODE_TIMER_START();

			// ----------------------------------------------------------------
			// Input plugs
			// ----------------------------------------------------------------
			const ndanim::JointParams* pJointParams = (const ndanim::JointParams*)HierarchyQuadword(pParams->m_inputSqtLoc, pHierarchyHeader);

			SMath::Quat qLocal = pJointParams->m_quat;

			// Generate bind pose rotation from joint orient, later we will inverse this values to get clean rotation values.
			// Joint orient is always MEulerRotation::kXYZ.
//			MVector		vOrient = dataBlock.inputValue(inJointOrient).asVector();
			Quat qOrient = Quat(pParams->m_jointOrient[0], pParams->m_jointOrient[1], pParams->m_jointOrient[2], (Quat::RotationOrder)pParams->m_rotateOrder);

			// Setup yaw, pitch and roll axis
			Vector vAim = GetVectorFromAxisIndex(pParams->m_rollAxis);
			int nPitchAxis = GetPitchAxisIndex(pParams->m_rollAxis, pParams->m_yawAxis);

			// Joint matrix includes joint orient, minus joint orient will result only local rotation
			Quat qRotate = Conjugate(qOrient) * qLocal;

			// Multiply aim vector with rotation to project offset aim vector
			Mat33 qRotateMat;
			BuildMat33(qRotate, qRotateMat);
			Vector vTarget = (vAim * qRotateMat);

			// Extract bend rotation of input rotation
			// Input rotation minus bend rotation, the result rotation is equivalent to twist rotation
			Quat qBend = RotationBetween(vAim.GetVec4(), vTarget.GetVec4());
			Quat qFinal = Conjugate(qBend) * qRotate;

			float eulerTwistValues[3];
			qFinal.GetEulerAngles(eulerTwistValues[0], eulerTwistValues[1], eulerTwistValues[2], (Quat::RotationOrder)pParams->m_rotateOrder);	// Same as Maya's RotationOrder enum

			// Exponential Map calculation
			if (qBend.W() < 0.0f) {
				qBend = -qBend; // Equivalent to qBend = -qBend
			}
			float isina;
			if (Abs(qBend.W()) > 1.0f - 1e-6f) {
				isina = 0.0f;
			}
			else {
				float a = Acos(qBend.W());
				isina = a / Sin(a);
			}
			// generate exp map vector
			Vector vExpMap = Vector(qBend.X() * isina, qBend.Y() * isina, qBend.Z() * isina);
			// Now generate out angles
			float outYaw = vExpMap[pParams->m_yawAxis] * 2.0f;
			float outPitch = vExpMap[nPitchAxis] * 2.0f;
			float outRoll = eulerTwistValues[pParams->m_rollAxis];

			//----------------------------------------------------------------
			// Set output plugs
			//----------------------------------------------------------------
			float outYawDegrees = RadiansToDegrees(outYaw);
			float outPitchDegrees = RadiansToDegrees(outPitch);
			float outRollDegrees = RadiansToDegrees(outRoll);
			*HierarchyFloat(pParams->m_outputYawLoc, pHierarchyHeader) = outYawDegrees;
			*HierarchyFloat(pParams->m_outputPitchLoc, pHierarchyHeader) = outPitchDegrees;
			*HierarchyFloat(pParams->m_outputRollLoc, pHierarchyHeader) = outRollDegrees;

			RIG_NODE_TIMER_END(RigNodeType::kAngleDriver);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("AngleDriver\n");
				MsgAnim("   outYaw: %.4f deg\n", outYawDegrees);
				MsgAnim("   outPitch: %.4f deg\n", outPitchDegrees);
				MsgAnim("   outRoll: %.4f deg\n", outRollDegrees);
			}
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandAngleDriver(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			AngleDriverParams const* pParams = (AngleDriverParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			ExecuteCommandAngleDriverImpl(pHierarchyHeader, pParams);
		}

		}	//namespace CommandBlock
}	//namespace OrbisAnim

