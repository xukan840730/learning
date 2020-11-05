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
#include "sharedmath/ps4/include/shared/math/point.h"

namespace OrbisAnim
{
	namespace CommandBlock
	{

		void BlendQuatsAndPositions(Quat& qRot, Point& vPos, const InterpolateMatrixArray16Params::Entry::QuatAndPos* quatsAndPoses, const float* bary)
		{
			float w01 = bary[1] / (bary[0] + bary[1] + 0.000000001f);
			float w23 = bary[3] / (bary[2] + bary[3] + 0.000000001f);
			float w01_23 = (bary[2] + bary[3]) / (bary[0] + bary[1] + bary[2] + bary[3] + 0.00000001f);

			Quat q01 = Slerp(quatsAndPoses[0].m_quat, quatsAndPoses[1].m_quat, w01);
			Quat q23 = Slerp(quatsAndPoses[2].m_quat, quatsAndPoses[3].m_quat, w23);
			qRot = Slerp(q01, q23, w01_23);

			Point pos01 = Lerp(quatsAndPoses[0].m_pos, quatsAndPoses[1].m_pos, w01);
			Point pos23 = Lerp(quatsAndPoses[2].m_pos, quatsAndPoses[3].m_pos, w23);
			vPos = Lerp(pos01, pos23, w01_23);
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandInterpolateMatrixArray16Impl(
			const HierarchyHeader* pHierarchyHeader,
			const InterpolateMatrixArray16Params* pParams,
			const InterpolateMatrixArray16Params::Entry* pEntries)
		{
			RIG_NODE_TIMER_START();
			for (U32 iEntry = 0; iEntry != pParams->m_numEntries; ++iEntry)
			{
				const InterpolateMatrixArray16Params::Entry& entry = pEntries[iEntry];
				const int kNumweights = 16 + 4; // weights + normalization weights
				float weights[kNumweights];			
				for (unsigned int ii = 0; ii < kNumweights; ii++)
					weights[ii] =  *HierarchyFloat(entry.m_weightLocs[ii], pHierarchyHeader);
				const InterpolateMatrixArray16Params::Entry::QuatAndPos* quatsAndPoses = entry.m_quatsAndPositions;

				InterpolateMatrixArray16Params::Entry::QuatAndPos tileResults[4];
				BlendQuatsAndPositions(tileResults[0].m_quat, tileResults[0].m_pos, quatsAndPoses + 0, weights  + 0);
				BlendQuatsAndPositions(tileResults[1].m_quat, tileResults[1].m_pos, quatsAndPoses + 4, weights  + 4);
				BlendQuatsAndPositions(tileResults[2].m_quat, tileResults[2].m_pos, quatsAndPoses + 8, weights  + 8);
				BlendQuatsAndPositions(tileResults[3].m_quat, tileResults[3].m_pos, quatsAndPoses + 12, weights + 12);

				Point vOutPos(kZero);
				Quat qOutRot(kIdentity);
				BlendQuatsAndPositions(qOutRot, vOutPos, tileResults, weights+16);

				Transform mOutput = Transform(qOutRot, vOutPos);
				*(Transform*)HierarchyQuadword(pParams->m_outputLocs[iEntry], pHierarchyHeader) = mOutput;
			}

			RIG_NODE_TIMER_END(RigNodeType::kInterpolateMatrixArray16);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				for (U32 iEntry = 0; iEntry != pParams->m_numEntries; ++iEntry)
				{
					const Transform& output = *(Transform*)HierarchyQuadword(pParams->m_outputLocs[iEntry], pHierarchyHeader);
					MsgAnim("InterpolateMatrixArray\n");
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)output.Get(0, 0), (float)output.Get(0, 1), (float)output.Get(0, 2), (float)output.Get(0, 3));
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)output.Get(1, 0), (float)output.Get(1, 1), (float)output.Get(1, 2), (float)output.Get(1, 3));
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)output.Get(2, 0), (float)output.Get(2, 1), (float)output.Get(2, 2), (float)output.Get(2, 3));
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)output.Get(3, 0), (float)output.Get(3, 1), (float)output.Get(3, 2), (float)output.Get(3, 3));
				}
			}
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandInterpolateMatrixArray16(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			InterpolateMatrixArray16Params const*  pParams = (InterpolateMatrixArray16Params const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);
			ExecuteCommandInterpolateMatrixArray16Impl(pHierarchyHeader, pParams, (const InterpolateMatrixArray16Params::Entry*)(pParams + 1));
		}

	}	//namespace CommandBlock
}	//namespace OrbisAnim

