/*
 * Copyright (c) 2003-2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "corelib/util/angle.h"
#include "corelib/util/timer.h"

#include <orbisanim/commandblock.h>
#include <orbisanim/commanddebug.h>
#include <orbisanim/commands.h>
#include <orbisanim/animhierarchy.h>
#include <orbisanim/joints.h>

#include "ndlib/anim/rig-nodes/rig-nodes.h"


namespace OrbisAnim
{
	namespace CommandBlock
	{

	    enum EulerOrder { kEOXyz, kEOYzx, kEOZxy, kEOXzy, kEOYxz, kEOZyx };

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandMatrixToExpMapImpl(const HierarchyHeader* pHierarchyHeader, const MatrixToExpMatParams* pParams)
		{
			RIG_NODE_TIMER_START();

			if (pParams->m_useMatrix)
			{
				const float* pSQTPtr = (float*)HierarchyQuadword(pParams->m_inputQuatLoc, pHierarchyHeader);
				OrbisAnim::JointParams* pJointParam = (OrbisAnim::JointParams*)(pSQTPtr - 4);	// The SQT starts 16 bytes (4 floats) in front of the Quat.

				// Do the work
				const Quat q = pJointParam->m_quat.W() < 0.0f ? Conjugate(pJointParam->m_quat) : pJointParam->m_quat;
				const float absW = Abs(q.W());
				float isina = 0.0f;
				if (absW < 1.0f - 1e-6f)
				{
					float a = Acos(absW);
					isina = a / Sin(a);
				}

				// Write out the output values
				*HierarchyFloat(pParams->m_outputLocs[0], pHierarchyHeader) = q.X() * isina;
				*HierarchyFloat(pParams->m_outputLocs[1], pHierarchyHeader) = q.Y() * isina;
				*HierarchyFloat(pParams->m_outputLocs[2], pHierarchyHeader) = q.Z() * isina;
			}
			else
			{
				const float rotateX = DEGREES_TO_RADIANS(*(float*)HierarchyQuadword(pParams->m_inputLocs[0], pHierarchyHeader));
				const float rotateY = DEGREES_TO_RADIANS(*(float*)HierarchyQuadword(pParams->m_inputLocs[1], pHierarchyHeader));
				const float rotateZ = DEGREES_TO_RADIANS(*(float*)HierarchyQuadword(pParams->m_inputLocs[2], pHierarchyHeader));

				const SMath::Vec4 xAxis(1, 0, 0, 0);
				const SMath::Vec4 yAxis(0, 1, 0, 0);
				const SMath::Vec4 zAxis(0, 0, 1, 0);

				Quat combinedQuat;
				switch (pParams->m_rotateOrder)
				{
				case kEOXyz:
					combinedQuat = Quat(zAxis, rotateZ) * Quat(yAxis, rotateY) * Quat(xAxis, rotateX);
					break;
				case kEOYzx:
					combinedQuat = Quat(xAxis, rotateX) * Quat(zAxis, rotateZ) * Quat(yAxis, rotateY);
					break;
				case kEOZxy:
					combinedQuat = Quat(yAxis, rotateY) * Quat(xAxis, rotateX) * Quat(zAxis, rotateZ);
					break;
				case kEOXzy:
					combinedQuat = Quat(yAxis, rotateY) * Quat(zAxis, rotateZ) * Quat(xAxis, rotateX);
					break;
				case kEOYxz:
					combinedQuat = Quat(zAxis, rotateZ) * Quat(xAxis, rotateX) * Quat(yAxis, rotateY);
					break;
				case kEOZyx:
					combinedQuat = Quat(xAxis, rotateX) * Quat(yAxis, rotateY) * Quat(zAxis, rotateZ);
					break;
				default:
					break;
				}

				// Do the work
				const Quat q = combinedQuat.W() < 0.0f ? Conjugate(combinedQuat) : combinedQuat;
				const float absW = Abs(q.W());
				float isina = 0.0f;
				if (absW < 1.0f - 1e-6f)
				{
					float a = Acos(absW);
					isina = a / Sin(a);
				}

				const float outExpMapX = q.X() * isina;
				const float outExpMapY = q.Y() * isina;
				const float outExpMapZ = q.Z() * isina;

				// Write out the output values
				*HierarchyFloat(pParams->m_outputLocs[0], pHierarchyHeader) = outExpMapX;
				*HierarchyFloat(pParams->m_outputLocs[1], pHierarchyHeader) = outExpMapY;
				*HierarchyFloat(pParams->m_outputLocs[2], pHierarchyHeader) = outExpMapZ;
			}

			RIG_NODE_TIMER_END(RigNodeType::kMatrixToExpMap);
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandMatrixToExpMap(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			RIG_NODE_TIMER_START();

			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			MatrixToExpMatParams const* pParams = (MatrixToExpMatParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			ExecuteCommandMatrixToExpMapImpl(pHierarchyHeader, pParams);
		}

	}	//namespace CommandBlock
}	//namespace OrbisAnim

