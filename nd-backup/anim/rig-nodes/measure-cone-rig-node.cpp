/*
 * Copyright (c) 2003-2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include <orbisanim/commandblock.h>
#include <orbisanim/commanddebug.h>
#include <orbisanim/commands.h>
#include <orbisanim/animhierarchy.h>

#include "corelib/util/timer.h"
#include "ndlib/anim/rig-nodes/rig-nodes.h"


namespace OrbisAnim
{
	namespace CommandBlock
	{

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandMeasureConeImpl(const HierarchyHeader* pHierarchyHeader, const InputControlDriverParams* pParams)
		{
			RIG_NODE_TIMER_START();

			const unsigned kNdiMeasureConeAngle = 0x0010A581;		// ITSCENE::CustomPlugin::kMeasureConeAngle
			ORBISANIM_ASSERT(kNdiMeasureConeAngle == pParams->m_mayaNodeTypeId);

			const SMath::Scalar minAngleDeg(pParams->m_minAngleDeg);
			const SMath::Scalar maxAngleDeg(pParams->m_maxAngleDeg);

			// account for twist, if any
			SMath::Quat twistQuatPs(SMath::kIdentity);
			if (pParams->m_inputTwistAngleLoc != Location::kInvalid)
			{
				const SMath::Scalar twistAngleDeg(*HierarchyFloat(pParams->m_inputTwistAngleLoc, pHierarchyHeader));
				const SMath::Scalar twistAngleRad = DegreesToRadians((float)twistAngleDeg);

				const SMath::Mat44 matT2P = SMath::BuildTransform(pParams->m_twistRefPosePs, SMath::Vec4(SMath::kZero));

				SMath::Vector twistAxesPs[3];
				twistAxesPs[0] = SMath::Vector(matT2P.GetRow(0).QuadwordValue());
				twistAxesPs[1] = SMath::Vector(matT2P.GetRow(1).QuadwordValue());
				twistAxesPs[2] = SMath::Vector(matT2P.GetRow(2).QuadwordValue());
				const SMath::Vector vecTwistAxisPs = Normalize(twistAxesPs[pParams->m_twistAxis]); // not theoretically necessary to normalize, but to be sure

				twistQuatPs = SMath::Quat(SMath::Vec4(vecTwistAxisPs.QuadwordValue()), twistAngleRad);
			}

			// convert the input quats to vectors
			const SMath::Quat jointPosePsQuat = *(const SMath::Quat*) HierarchyQuadword(pParams->m_inputQuatLoc, pHierarchyHeader);
			const SMath::Quat quatJ2P = jointPosePsQuat;

			const SMath::Mat44 matJ2P = SMath::BuildTransform(quatJ2P, SMath::Vec4(SMath::kZero));
			const SMath::Mat44 matR2P = SMath::BuildTransform(pParams->m_refPosePs, SMath::Vec4(SMath::kZero));

			SMath::Vector jointAxesPs[3];
			jointAxesPs[0] = SMath::Vector(matJ2P.GetRow(0).QuadwordValue());
			jointAxesPs[1] = SMath::Vector(matJ2P.GetRow(1).QuadwordValue());
			jointAxesPs[2] = SMath::Vector(matJ2P.GetRow(2).QuadwordValue());
			const SMath::Vector inputVector = Normalize(jointAxesPs[pParams->m_primaryAxis]); // not theoretically necessary to normalize, but to be sure

			SMath::Vector refAxesPs[3];
			refAxesPs[0] = SMath::Vector(matR2P.GetRow(0).QuadwordValue());
			refAxesPs[1] = SMath::Vector(matR2P.GetRow(1).QuadwordValue());
			refAxesPs[2] = SMath::Vector(matR2P.GetRow(2).QuadwordValue());
			const SMath::Vector locatorVectorRaw = Normalize(refAxesPs[pParams->m_primaryAxis]); // not theoretically necessary to normalize, but to be sure

			// twist the locator vector as appropriate
			const SMath::Mat44 locatorRotate = SMath::BuildTransform(twistQuatPs, SMath::Vec4(SMath::kZero));
			const SMath::Vector locatorVector(MulVectorMatrix(SMath::Vec4(locatorVectorRaw.QuadwordValue()), locatorRotate).QuadwordValue());

			// calculate the output weight value

			// get the angle of the input vector vs the ndiMeasureConeAngle(locator) nodes vector
			const SMath::Scalar cosine = Dot(locatorVector, inputVector);
			SMath::Scalar vAngle = acosf((float)cosine);
			const SMath::Scalar angleRad(vAngle);
			const SMath::Scalar angleDeg = RadiansToDegrees((float)angleRad);

			// get difference between angles - assume max is never smaller than min
			SMath::Scalar weightVal(SMath::kZero);
			if (maxAngleDeg > minAngleDeg)
			{
				weightVal = SMath::Scalar(1.0f) - ( (angleDeg - minAngleDeg) / (maxAngleDeg - minAngleDeg));
				weightVal = SMath::Clamp(weightVal, SMath::Scalar(SMath::kZero), SMath::Scalar(1.0f));
			}
			// if maxAngleDeg <= minAngleDeg, then weightVal = 1.0 within min angle and 0.0 outside
			else if (angleDeg <= minAngleDeg)
			{
				weightVal = SMath::Scalar(1.0f);
			}


			*HierarchyFloat(pParams->m_outputLoc, pHierarchyHeader) = weightVal;

			RIG_NODE_TIMER_END(RigNodeType::kMeasureCone);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("MeasureCone\n");
				MsgAnim("   out: %.4f\n", (float)weightVal);
			}
		}

		// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandMeasureCone(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			const U16 numControlDrivers = param_qw0[0];
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			InputControlDriverParams const* pDriver = (InputControlDriverParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);
			ORBISANIM_ASSERT(numControlDrivers == 1);

			ExecuteCommandMeasureConeImpl(pHierarchyHeader, &pDriver[0]);
		}
	}	//namespace CommandBlock
}	//namespace OrbisAnim
