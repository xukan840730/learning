/*
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

		/// --------------------------------------------------------------------------------------------------------------- ///
		void BlendQuatsAndPositions(Quat& qRot,
									Point& vPos,
									const InterpolateMatrixArrayParams::Entry::PositionsAndQuats& quatsAndPoses,
									const float* bary)
		{
			const Point* pos   = quatsAndPoses.m_pos;
			const Quat* rots   = quatsAndPoses.m_quats;
			const float w01	   = bary[1] / (bary[0] + bary[1] + 0.000000001f);
			const float w23	   = bary[3] / (bary[2] + bary[3] + 0.000000001f);
			const float w01_23 = (bary[2] + bary[3]) / (bary[0] + bary[1] + bary[2] + bary[3] + 0.00000001f);

			Quat q01 = Slerp(rots[0], rots[1], w01);
			Quat q23 = Slerp(rots[2], rots[3], w23);
			qRot	 = Slerp(q01, q23, w01_23);

			Point pos01 = Lerp(pos[0], pos[1], w01);
			Point pos23 = Lerp(pos[2], pos[3], w23);
			vPos		= Lerp(pos01, pos23, w01_23);
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void BlendMats(Quat& qRot, Point& vPos, const Transform** mInMatrix, const float* bary)
		{
			InterpolateMatrixArrayParams::Entry::PositionsAndQuats positionsAndQuats;
			for (unsigned int ii = 0; ii != 4; ++ii)
			{
				const Transform& xform = *(mInMatrix[ii]);
				positionsAndQuats.m_pos[ii] = xform.GetTranslation();
				positionsAndQuats.m_quats[ii] = Quat(xform.GetRawMat44());
			}
			BlendQuatsAndPositions(qRot, vPos, positionsAndQuats, bary);
		}


		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandInterpolateMatrixArrayImpl(const HierarchyHeader* pHierarchyHeader,
													  const InterpolateMatrixArrayParams* pParams,
													  const InterpolateMatrixArrayParams::Entry* pEntries,
													  const Transform* pDefaultMatricesTable,
													  const float* pDefaultWeightsTable,
													  const Location* pInputLocsTable)
		{
			ANIM_ASSERT(pParams->m_version == 1);
			RIG_NODE_TIMER_START();
			for (U32 iEntry = 0; iEntry != pParams->m_numEntries; ++iEntry)
			{
				const InterpolateMatrixArrayParams::Entry& entry = pEntries[iEntry];
				const Transform* pDefaultMatrices = pDefaultMatricesTable ? &pDefaultMatricesTable[entry.m_matrixArrayOffset] : (const Transform*)(((U8*)pParams) + entry.m_matrixArrayOffset);
				const float* pDefaultWeights = pDefaultWeightsTable ? &pDefaultWeightsTable[entry.m_weightArrayOffset] : (const float*)(((U8*)pParams) + entry.m_weightArrayOffset);
				const Location* pInputLocs = pInputLocsTable ? &pInputLocsTable[entry.m_inputArrayOffset] : (Location*)(((U8*)pParams) + entry.m_inputArrayOffset);

				Point vOutPos(kZero);
				Quat qOutRot(kIdentity);

				if (entry.m_flags & InterpolateMatrixArrayParams::Entry::kConstMatrices) // const Mats
				{
					const InterpolateMatrixArrayParams::Entry::PositionsAndQuats& positionsAndQuats = *(const InterpolateMatrixArrayParams::Entry::PositionsAndQuats*)pDefaultMatrices;

					float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

					ANIM_ASSERT(entry.m_numMatrices == 4);

					for (unsigned int ii = 0; ii < entry.m_numMatrices; ii++)
					{
						const Location weightInputLoc = pInputLocs[entry.m_numMatrices + ii];
						weights[ii] = (weightInputLoc == Location::kInvalid) ? pDefaultWeights[ii] : *HierarchyFloat(weightInputLoc, pHierarchyHeader);
					}
					BlendQuatsAndPositions(qOutRot, vOutPos, positionsAndQuats, weights);
				}
				else
				{
					const Transform* matrices[4] = { nullptr, nullptr, nullptr, nullptr };
					float weights[4];

					ANIM_ASSERT(entry.m_numMatrices == 4);

					for (unsigned int ii = 0; ii < entry.m_numMatrices; ii++)
					{
						const Location matrixInputLoc = pInputLocs[ii];
						const Location weightInputLoc = pInputLocs[entry.m_numMatrices + ii];

						matrices[ii] = (matrixInputLoc == Location::kInvalid) ? (pDefaultMatrices + ii) : (const Transform*)HierarchyQuadword(matrixInputLoc, pHierarchyHeader);
						weights[ii] = (weightInputLoc == Location::kInvalid) ? pDefaultWeights[ii] : *HierarchyFloat(weightInputLoc, pHierarchyHeader);
					}
					BlendMats(qOutRot, vOutPos, matrices, weights);
				}
				Transform mOutput = Transform(qOutRot, vOutPos);
				//	ANIM_ASSERT(IsFinite(mOutput));
				*(Transform*)HierarchyQuadword(entry.m_outputMatrixLoc, pHierarchyHeader) = mOutput;
			}

			RIG_NODE_TIMER_END(RigNodeType::kInterpolateMatrixArray);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				for (U32 iEntry = 0; iEntry != pParams->m_numEntries; ++iEntry)
				{
					const InterpolateMatrixArrayParams::Entry& entry = pEntries[iEntry];
					const Transform& output = *(Transform*)HierarchyQuadword(entry.m_outputMatrixLoc, pHierarchyHeader);
					MsgAnim("InterpolateMatrixArray\n");
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)output.Get(0, 0), (float)output.Get(0, 1), (float)output.Get(0, 2), (float)output.Get(0, 3));
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)output.Get(1, 0), (float)output.Get(1, 1), (float)output.Get(1, 2), (float)output.Get(1, 3));
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)output.Get(2, 0), (float)output.Get(2, 1), (float)output.Get(2, 2), (float)output.Get(2, 3));
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)output.Get(3, 0), (float)output.Get(3, 1), (float)output.Get(3, 2), (float)output.Get(3, 3));
				}
			}
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandInterpolateMatrixArray(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			InterpolateMatrixArrayParams const* pParams = (InterpolateMatrixArrayParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);
			ANIM_ASSERT(pParams->m_version);
			ExecuteCommandInterpolateMatrixArrayImpl(pHierarchyHeader,
													 pParams,
													 (const InterpolateMatrixArrayParams::Entry*)(pParams + 1),
													 nullptr,
													 nullptr,
													 nullptr);
		}

	}	//namespace CommandBlock
}	//namespace OrbisAnim

