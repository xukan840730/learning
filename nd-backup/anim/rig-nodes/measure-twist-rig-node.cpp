/*
 * Copyright (c) 2003-2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include <orbisanim/commandblock.h>
#include <orbisanim/commanddebug.h>
#include <orbisanim/commands.h>
#include <orbisanim/animhierarchy.h>

#include "corelib/util/angle.h"
#include "corelib/util/timer.h"
#include "ndlib/anim/rig-nodes/rig-nodes.h"


namespace OrbisAnim
{
	namespace CommandBlock
	{

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandMeasureTwistImpl(const HierarchyHeader* pHierarchyHeader, const InputControlDriverParams* pParams)
		{
			RIG_NODE_TIMER_START();

			const unsigned kNdiMeasureTwist     = 0x0010A582;		// ITSCENE::CustomPlugin::kMeasureTwist
			ORBISANIM_ASSERT(kNdiMeasureTwist == pParams->m_mayaNodeTypeId);

			const SMath::Quat jointPosePsQuat = *(const SMath::Quat*) HierarchyQuadword(pParams->m_inputQuatLoc, pHierarchyHeader);
			//					const SMath::Quat quatJ2P = jointPosePsQuat;
			const SMath::Quat quatP2R = Conjugate(pParams->m_refPosePs);
			const SMath::Quat quatR2P = pParams->m_refPosePs; // the reference pose is always in the joint's parent-space as well
			const SMath::Quat quatJ2P = Dot(jointPosePsQuat, quatR2P) >= 0.0f ? jointPosePsQuat : -jointPosePsQuat;

			SMath::Vector refAxesPs[3];
			refAxesPs[0] = GetLocalX(quatR2P);
			refAxesPs[1] = GetLocalY(quatR2P);
			refAxesPs[2] = GetLocalZ(quatR2P);
			const SMath::Vector vecRefAxisP = SafeNormalize(refAxesPs[pParams->m_primaryAxis], kUnitZAxis); // not theoretically necessary to normalize, but to be sure

			SMath::Vector jointAxesPs[3];
			jointAxesPs[0] = GetLocalX(quatJ2P);
			jointAxesPs[1] = GetLocalY(quatJ2P);
			jointAxesPs[2] = GetLocalZ(quatJ2P);
			const SMath::Vector vecTwistAxisP = SafeNormalize(jointAxesPs[pParams->m_primaryAxis], kUnitZAxis); // not theoretically necessary to normalize, but to be sure

			//					const SMath::Quat quatP2R = Conjugate(quatR2P);

			// find the "swing" component of the rotation -- the quat that rotates the reference twist axis into
			// the current twist axis via the shortest path (about their mutually-perpendicular axis)
			const SMath::Quat quatSwing = QuatFromVectors(vecRefAxisP, vecTwistAxisP); // OPT NOTE: this function contains if() statements -- can we avoid?


			// the twist is just the "remainder" of the full rotation quat "minus" the swing quat
			//                       quatJ2P = quatSwing * quatTwist
			//        quatSwing^-1 * quatJ2P = (quatSwing^-1 * quatSwing) * quatTwist
			//     .: quatSwing^-1 * quatJ2P = quatTwist
			const SMath::Quat quatSwingInv = Conjugate(quatSwing);
			const SMath::Quat quatTwist = Normalize(quatP2R * quatSwingInv * quatJ2P);

			SMath::Vec4 vecTwistAxisI; float fTwistRadians;
			quatTwist.GetAxisAndAngle(vecTwistAxisI, fTwistRadians);
			const SMath::Scalar twistRadians(fTwistRadians);

			const SMath::Scalar directionSign = Sign(vecTwistAxisI[pParams->m_primaryAxis]);
			const SMath::Scalar measuredAngleVal = RadiansToDegrees((float)NormalizeAngle_rad(twistRadians * directionSign)); // OPT NOTE: this function contains if() statements -- can we avoid?

			// 					SMath::Scalar* pOutputDest = (SMath::Scalar*)LocToPtr(pParams->m_outputLoc);
			// 					*pOutputDest = measuredAngleVal;
			*HierarchyFloat(pParams->m_outputLoc, pHierarchyHeader) = measuredAngleVal;

			RIG_NODE_TIMER_END(RigNodeType::kMeasureTwist);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("MeasureTwist\n");
				MsgAnim("   out: %.4f\n", (float)measuredAngleVal);
			}
		}

		void ExecuteCommandMeasureTwist(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			const U16 numControlDrivers = param_qw0[0];
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			InputControlDriverParams const* pDriver = (InputControlDriverParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);
			ORBISANIM_ASSERT(numControlDrivers == 1);

			ExecuteCommandMeasureTwistImpl(pHierarchyHeader, &pDriver[0]);
		}

	}	//namespace CommandBlock
}	//namespace OrbisAnim
