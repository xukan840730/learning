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


#define ENABLE_DEBUG_DRAWING	1

#if ENABLE_DEBUG_DRAWING
#	include "ndlib/render/util/prim.h"
#endif

bool g_enablePointPoserDebugDrawing = false;
bool g_dumpInfoPointPoser = false;

namespace OrbisAnim
{
	namespace CommandBlock
	{

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandPointPoserImpl(const HierarchyHeader* pHierarchyHeader, const PointPoserParams* pParams)
		{
			RIG_NODE_TIMER_START();

			const Location* pInputWeightLocArray = (const Location*)(((U8*)pParams) + pParams->m_inputArrayLocOffset);
			const Location* pOutputLocArray = (const Location*)(((U8*)pParams) + pParams->m_outputArrayLocOffset);
			const Vector* pPointPosesArray = (const Vector*)(((U8*)pParams) + pParams->m_pointPosesLocOffset);

			// Get the input weights
			float* pWeights = STACK_ALLOC_ALIGNED(float, pParams->m_numPoses, kAlign4);
			for (int i = 0; i < pParams->m_numPoses; ++i)
			{
				const float unclampedWeight = *(const float*)OrbisAnim::HierarchyFloat(pInputWeightLocArray[i], pHierarchyHeader);
				// Clamp the value to reasonable values
				pWeights[i] = Limit(unclampedWeight, -1.0f, 1.0f);
			}

			// Initialize the points
			Point* pOutputPoints = STACK_ALLOC_ALIGNED(Point, pParams->m_numPointsPerPose, kAlign16);
			for (int pointIndex = 0; pointIndex < pParams->m_numPointsPerPose; ++pointIndex)
			{
				pOutputPoints[pointIndex] = Point(kOrigin);
			}

			// Sum the points
			const U32 numPoses = pParams->m_numPoses;
			const U32 numPointsPerPose = pParams->m_numPointsPerPose;
			unsigned currentValidBitIndex = 0;
			for (int pointIndex = 0; pointIndex < numPointsPerPose; ++pointIndex)
			{
				Point summedPoint(kOrigin);
				for (int poseIndex = 0; poseIndex < numPoses; ++poseIndex)
				{
					const float poseWeight = pWeights[poseIndex];
					const Vector delta = pPointPosesArray[poseIndex * numPointsPerPose + pointIndex];
					summedPoint += delta * poseWeight;
				}

				pOutputPoints[pointIndex] = summedPoint;
			}

			// Output the points
			for (int i = 0; i < pParams->m_numPointsPerPose; ++i)
			{
				// We can safely assume that the X/Y/Z components are right next to each other so we just do one write
				*OrbisAnim::HierarchyFloat(pOutputLocArray[i * 3 + 0], pHierarchyHeader) = pOutputPoints[i].X();
				*OrbisAnim::HierarchyFloat(pOutputLocArray[i * 3 + 1], pHierarchyHeader) = pOutputPoints[i].Y();
				*OrbisAnim::HierarchyFloat(pOutputLocArray[i * 3 + 2], pHierarchyHeader) = pOutputPoints[i].Z();
			}

			RIG_NODE_TIMER_END(RigNodeType::kPointPoser);

			// Output the points
			for (int i = 0; i < pParams->m_numPointsPerPose; ++i)
			{
#if ENABLE_DEBUG_DRAWING
				if (FALSE_IN_FINAL_BUILD(g_enablePointPoserDebugDrawing))
				{
					g_prim.Draw(DebugSphere(pOutputPoints[i], 0.005f, kColorWhite));
				}
#endif
			}

			// Dump debug info
			if (FALSE_IN_FINAL_BUILD(g_dumpInfoPointPoser))
			{
				MsgAnim("=======[ Point Poser ]==================================================================\n");

				MsgAnim("Input weights\n");
				MsgAnim("-------------\n");
				for (int i = 0; i < pParams->m_numPoses; ++i)
				{
					MsgAnim("Input weight %d: %+f\n", i, pWeights[i]);
				}

				MsgAnim("\nOutput points\n");
				MsgAnim("-------------\n");
				for (int i = 0; i < pParams->m_numPointsPerPose; ++i)
				{
					MsgAnim("Output Point %d: %+f, %+f, %+f\n", i, (float)pOutputPoints[i].X(), (float)pOutputPoints[i].Y(), (float)pOutputPoints[i].Z());
				}
			}

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("PointPoser (%s)\n", DevKitOnly_StringIdToStringOrHex(pParams->m_nodeNameId));
				for (int i = 0; i < pParams->m_numPointsPerPose; ++i)
					MsgAnim("   %.4f %.4f %.4f\n", (float)pOutputPoints[i].X(), (float)pOutputPoints[i].Y(), (float)pOutputPoints[i].Z());
			}
		}


		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandPointPoser(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			const HierarchyHeader* pHierarchyHeader = (const HierarchyHeader*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			const PointPoserParams* pParams = (const PointPoserParams*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			ExecuteCommandPointPoserImpl(pHierarchyHeader, pParams);
		}
	}	//namespace CommandBlock
}	//namespace OrbisAnim
