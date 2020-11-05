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
		void ExecuteCommandDecomposeMatrixImpl(const HierarchyHeader* pHierarchyHeader, const DecomposeMatrixParams* pParams)
		{
			RIG_NODE_TIMER_START();

			// Extract the parameters
 			const Mat44* pMatrix = (const Mat44*)HierarchyQuadword(pParams->m_inputMatLoc, pHierarchyHeader);

			// ----------------------------------------------------------------
			// Input plugs
			// ----------------------------------------------------------------
// 			short nRotateOrder = dataBlock.inputValue(inRotateOrder).asShort();
// 			MVector vInJointOrient = dataBlock.inputValue(inJointOrient).asVector();
// 			MMatrix mInMatrix = dataBlock.inputValue(inMatrix).asMatrix();

			// Decompose matrix
			float dScale[3];
			const Mat44 tmOutput = *pMatrix;

			dScale[0] = Length3(tmOutput.GetRow(0));
			dScale[1] = Length3(tmOutput.GetRow(1));
			dScale[2] = Length3(tmOutput.GetRow(2));
//			tmOutput.getScale(dScale, MSpace::kTransform);

			Mat44 tmOutputNoScale = tmOutput;
			SMath::RemoveScale(&tmOutputNoScale);

//			MVector			vOutTranslate = tmOutput.translation(MSpace::kTransform);
//			MQuaternion		qOrient = MEulerRotation(vInJointOrient, getRotationOrder(0)).asQuaternion();
			const Quat qOrient = Quat(pParams->m_jointOrient[0], pParams->m_jointOrient[1], pParams->m_jointOrient[2], (Quat::RotationOrder)pParams->m_rotateOrder);
			const Quat qOutput = Quat(tmOutputNoScale);
			const Quat qLocal = Conjugate(qOrient) * qOutput;
			ANIM_ASSERT(IsNormal(qOrient));
			ANIM_ASSERT(IsNormal(qOutput));
			ANIM_ASSERT(IsNormal(qLocal));

			float eulerValues[3];
			qLocal.GetEulerAngles(eulerValues[0], eulerValues[1], eulerValues[2], (Quat::RotationOrder)pParams->m_rotateOrder);
//			MEulerRotation	eOutRotate = .reorder(getRotationOrder(nRotateOrder));

			//----------------------------------------------------------------
			// Set output plugs
			//----------------------------------------------------------------
			float outTransX = tmOutputNoScale.GetRow(3).X();
			float outTransY = tmOutputNoScale.GetRow(3).Y();
			float outTransZ = tmOutputNoScale.GetRow(3).Z();
			float outRotateX = RadiansToDegrees(eulerValues[0]);
			float outRotateY = RadiansToDegrees(eulerValues[1]);
			float outRotateZ = RadiansToDegrees(eulerValues[2]);
			float outScaleX = dScale[0];
			float outScaleY = dScale[1];
			float outScaleZ = dScale[2];
			*HierarchyFloat(pParams->m_outputTransLoc[0], pHierarchyHeader) = outTransX;
			*HierarchyFloat(pParams->m_outputTransLoc[1], pHierarchyHeader) = outTransY;
			*HierarchyFloat(pParams->m_outputTransLoc[2], pHierarchyHeader) = outTransZ;
			*HierarchyFloat(pParams->m_outputRotLoc[0], pHierarchyHeader) = outRotateX;
			*HierarchyFloat(pParams->m_outputRotLoc[1], pHierarchyHeader) = outRotateY;
			*HierarchyFloat(pParams->m_outputRotLoc[2], pHierarchyHeader) = outRotateZ;
			*HierarchyFloat(pParams->m_outputScaleLoc[0], pHierarchyHeader) = outScaleX;
			*HierarchyFloat(pParams->m_outputScaleLoc[1], pHierarchyHeader) = outScaleY;
			*HierarchyFloat(pParams->m_outputScaleLoc[2], pHierarchyHeader) = outScaleZ;

			RIG_NODE_TIMER_END(RigNodeType::kDecomposeMatrix);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("DecomposeMatrix\n");
				MsgAnim("   outTrans: %.4f %.4f %.4f\n", outTransX, outTransY, outTransZ);
				MsgAnim("   outRotate: %.4f %.4f %.4f deg\n", outRotateX, outRotateY, outRotateZ);
				MsgAnim("   outScale: %.4f %.4f %.4f\n", outScaleX, outScaleY, outScaleZ);
			}
		}

		void ExecuteCommandDecomposeMatrix(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			DecomposeMatrixParams const* pParams = (DecomposeMatrixParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			ExecuteCommandDecomposeMatrixImpl(pHierarchyHeader, pParams);
		}

	}	//namespace CommandBlock
}	//namespace OrbisAnim

