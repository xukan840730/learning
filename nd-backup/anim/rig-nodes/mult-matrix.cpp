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
		void ExecuteCommandMultMatrixImpl(const HierarchyHeader* pHierarchyHeader,
										  const MultMatrixParams* pParams,
										  const float* pDefaultValues,
										  const Location* pInputLocs,
										  const U8* pInvertMat,
										  const U8* pDataFormat)
		{
			RIG_NODE_TIMER_START();

			// Extract the parameters

			// ----------------------------------------------------------------
			// Input plugs
			// ----------------------------------------------------------------

			// This is more of an order matrix multiplication
			Transform mOutput(kIdentity);
			for (int i = 0; i < pParams->m_numInputs; ++i)
			{
				Transform curr;
				if (pDataFormat[i] == 0)	// Constant
				{
					const float* pMatConstData = pDefaultValues + i * sizeof(Transform) / sizeof(float);
					curr = Transform(Mat44(Vec4(pMatConstData), Vec4(pMatConstData + 4), Vec4(pMatConstData + 8), Vec4(pMatConstData + 12)));
					ANIM_ASSERT(IsFinite(curr));
				}
				else if (pDataFormat[i] == 1)	// SQT
				{
					const void* pData = HierarchyQuadword(pInputLocs[i], pHierarchyHeader);
					const ndanim::JointParams* pJp = (const ndanim::JointParams*)pData;
					Transform mat(pJp->m_quat, pJp->m_trans); 
					Transform scaleMat;
					scaleMat.SetScale(pJp->m_scale);
					curr = scaleMat * mat;
					ANIM_ASSERT(IsFinite(curr));
				}
				else if (pDataFormat[i] == 2)	// Mat34 
				{
					const void* pData = HierarchyQuadword(pInputLocs[i], pHierarchyHeader);
					const OrbisAnim::JointTransform* pJt = (const OrbisAnim::JointTransform*)pData;
					curr = Transform(pJt->GetTransform());
					ANIM_ASSERT(IsFinite(curr));
				}
				else if (pDataFormat[i] == 3)	// Input (4x4) 
				{
					const void* pData = HierarchyQuadword(pInputLocs[i], pHierarchyHeader);
					const float* pMatData = (const float*)pData;
					curr = Transform(Mat44(Vec4(pMatData), Vec4(pMatData + 4), Vec4(pMatData + 8), Vec4(pMatData + 12)));
					ANIM_ASSERT(IsFinite(curr));
				}

				if (pInvertMat[i])
					curr = Inverse(curr);

				mOutput = mOutput * curr;
			}

			//----------------------------------------------------------------
			// Set output plugs
			//----------------------------------------------------------------

			const Mat44 outMat = mOutput.GetMat44();
			ANIM_ASSERT(IsFinite(outMat));
 			*(Mat44*)HierarchyQuadword(pParams->m_outputMatrixLoc, pHierarchyHeader) = outMat;

			RIG_NODE_TIMER_END(RigNodeType::kMultMatrix);
			
			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("MultMatrix\n");
				MsgAnim("   %.4f %.4f %.4f %.4f\n", (float)outMat.GetRow(0).X(), (float)outMat.GetRow(0).Y(), (float)outMat.GetRow(0).Z(), (float)outMat.GetRow(0).W());
				MsgAnim("   %.4f %.4f %.4f %.4f\n", (float)outMat.GetRow(1).X(), (float)outMat.GetRow(1).Y(), (float)outMat.GetRow(1).Z(), (float)outMat.GetRow(1).W());
				MsgAnim("   %.4f %.4f %.4f %.4f\n", (float)outMat.GetRow(2).X(), (float)outMat.GetRow(2).Y(), (float)outMat.GetRow(2).Z(), (float)outMat.GetRow(2).W());
				MsgAnim("   %.4f %.4f %.4f %.4f\n", (float)outMat.GetRow(3).X(), (float)outMat.GetRow(3).Y(), (float)outMat.GetRow(3).Z(), (float)outMat.GetRow(3).W());
			}
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandMultMatrix(DispatcherFunctionArgs_arg_const param_qw0,
									  LocationMemoryMap_arg_const memoryMap,
									  OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)
				OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			MultMatrixParams const* pParams = (MultMatrixParams const*)
				OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3],
														   memoryMap);

			const float* pDefaultValues = (float*)(((U8*)pParams) + pParams->m_inputArrayOffset);
			const Location* pInputLocs = (Location*)(((U8*)pDefaultValues) + pParams->m_numInputs * sizeof(float) * 16);
			const U8* pInvertMat	   = (U8*)(((U8*)pInputLocs) + pParams->m_numInputs * sizeof(U32));
			const U8* pDataFormat	   = (U8*)(((U8*)pInvertMat) + pParams->m_numInputs * sizeof(U8));

			ExecuteCommandMultMatrixImpl(pHierarchyHeader, pParams, pDefaultValues, pInputLocs, pInvertMat, pDataFormat);
		}
	} // namespace CommandBlock
} // namespace OrbisAnim
