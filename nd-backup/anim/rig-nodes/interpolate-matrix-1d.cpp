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
		static Mat44 InterpolateMatrix(const Mat44& ma, const Mat44& mb, float dWeight)
		{
			float aScale[3];
			aScale[0] = Length3(ma.GetRow(0));
			aScale[1] = Length3(ma.GetRow(1));
			aScale[2] = Length3(ma.GetRow(2));

			float bScale[3];
			bScale[0] = Length3(mb.GetRow(0));
			bScale[1] = Length3(mb.GetRow(1));
			bScale[2] = Length3(mb.GetRow(2));

			Vector		vPosition = LinearInterpolate3D(Point(ma.GetRow(3)), Point(mb.GetRow(3)), dWeight);
			Quat		qRotation = Slerp(Quat(ma), Quat(mb), dWeight);
			Vector		vScale = LinearInterpolate3D(Point(aScale), Point(bScale), dWeight);

			//generate final matrix
			Transform xform;
			xform.SetScale(vScale);
			xform = xform * Transform(qRotation, Point(kZero));
			xform.SetTranslation(Point(kZero) + vPosition);

			return xform.GetMat44();
		}


		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandInterpolateMatrix1DImpl(const HierarchyHeader* pHierarchyHeader,
												   const InterpolateMatrix1DParams* pParams,
												   const InterpolateMatrix1DParams::Entry* pEntries,
												   const InterpolateMatrix1DState* pStateData)
		{
			RIG_NODE_TIMER_START();
			for (U32 iParam = 0; iParam != pParams->m_numEntries; ++iParam)
			{
				const InterpolateMatrix1DParams::Entry* pEntry = pEntries + iParam;
				const InterpolateMatrix1DState* pStates		   = pStateData
															  ? &pStateData[pEntry->m_statesOffset]
															  : (InterpolateMatrix1DState*)(((U8*)pParams)
																							+ pEntry->m_statesOffset);

				Mat44 mOutput(kIdentity);
				if (pEntry->m_numStates > 1)
				{

					// Extract the parameters
					float inValue = *HierarchyFloat(pEntry->m_inputLoc, pHierarchyHeader);

					// Damn it!!! We have degrees as the unit to pass around rotations between SDK nodes. This should be changed after we ship T2!
					//if (pParams->m_inputAsAngle)		NO NEED FOR THIS THE PROVIDED ANGLES ARE ALREADY IN DEGREES
					//	inValue *= (180.0f / 3.141592653589793238463f);   // just like the plugin

																		  // making sure input value is in between defined states
					int lastIndex = (pEntry->m_numStates - 1);
					// Pre infinity and post infinity calculation
					if (inValue < pStates[0].m_time && pEntry->m_preInfinityMode == 0)
					{
						mOutput = *(Mat44*)(pStates[0].m_values);
					}
					else if (inValue > pStates[lastIndex].m_time && pEntry->m_postInfinityMode == 0)
					{
						mOutput = *(Mat44*)(pStates[lastIndex].m_values);
					}
					else
					{
						unsigned int k = 0;
						for (k = 0; k < pEntry->m_numStates - 2; k++)
						{
							// if input value is less than next inTime value then break no need to go into next state
							if (inValue < pStates[k + 1].m_time)
								break;
						}

						float weight = (inValue - pStates[k].m_time) / (pStates[k + 1].m_time - pStates[k].m_time);
						if (pEntry->m_interpMode != 0)
						{ // Linear interpolation
							if (inValue >= pStates[k].m_time && inValue <= pStates[k + 1].m_time)
							{
								if (pEntry->m_interpMode == 1) // SmoothStep interpolation
									weight = SmoothStep(weight);
								else if (pEntry->m_interpMode == 2) // Gaussian interpolation
									weight = SmoothGaussian(weight);
							}
						}

						mOutput = InterpolateMatrix(*(Mat44*)(pStates[k].m_values),
													*(Mat44*)(pStates[k + 1].m_values),
													weight);
					}
				}

				*(Mat44*)HierarchyQuadword(pEntry->m_outputLoc, pHierarchyHeader) = mOutput;

				if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
				{
					MsgAnim("InterpolateMatrix1DMULTI\n");
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)mOutput.Get(0, 0), (float)mOutput.Get(0, 1), (float)mOutput.Get(0, 2), (float)mOutput.Get(0, 3));
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)mOutput.Get(1, 0), (float)mOutput.Get(1, 1), (float)mOutput.Get(1, 2), (float)mOutput.Get(1, 3));
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)mOutput.Get(2, 0), (float)mOutput.Get(2, 1), (float)mOutput.Get(2, 2), (float)mOutput.Get(2, 3));
					MsgAnim("   %.4f %.4f %.4f %.4f \n", (float)mOutput.Get(3, 0), (float)mOutput.Get(3, 1), (float)mOutput.Get(3, 2), (float)mOutput.Get(3, 3));
				}
			}

			RIG_NODE_TIMER_END(RigNodeType::kInterpolateMatrix1D);
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandInterpolateMatrix1D(DispatcherFunctionArgs_arg_const param_qw0,
											   LocationMemoryMap_arg_const memoryMap,
											   OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			InterpolateMatrix1DParams const* pParams = (InterpolateMatrix1DParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			ExecuteCommandInterpolateMatrix1DImpl(pHierarchyHeader,
												  pParams,
												  (const InterpolateMatrix1DParams::Entry*)(pParams + 1),
												  nullptr);
		}



	}	//namespace CommandBlock
}	//namespace OrbisAnim

