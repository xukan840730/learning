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

		struct MatrixToFloatsParams
		{
			U32		m_mayaNodeTypeId;
			U32		m_numOutputs;

			U16		m_inputLocs;				// Offset from start of this struct to where the input locations are
			U16		m_outputInfos;				// Offset from start of this struct to where the output locations are
			U16		m_padding[2];

			StringId64		m_nodeNameId;
		};

		struct MatrixToFloatsOutputInfo
		{
			U32 m_type;
			U32 m_loc;
		};

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandMatrixToFloatsImpl(const HierarchyHeader* pHierarchyHeader, const MatrixToFloatsParams* pParams)
		{
			RIG_NODE_TIMER_START();

			// Extract the parameters
			const I64 numOutputs = pParams->m_numOutputs;
			const U32* pInputLocs = (U32*)(((U8*)pParams) + pParams->m_inputLocs);
			const MatrixToFloatsOutputInfo* pOutputInfos = (MatrixToFloatsOutputInfo*)(((U8*)pParams) + pParams->m_outputInfos);

			// Construct the local transformation matrix
			OrbisAnim::JointParams* pJointParam = nullptr;

			const float* pQuatPtr = (float*)HierarchyQuadword(pInputLocs[3], pHierarchyHeader);
			pJointParam = (OrbisAnim::JointParams*)(pQuatPtr - 4);	// The SQT starts 16 bytes (4 floats) in front of the Quat.

			const Mat44 rotTransXform = BuildTransform(pJointParam->m_quat, pJointParam->m_trans.GetVec4());
			Mat44 scaleXform(kIdentity);
			scaleXform.SetScale(pJointParam->m_scale.GetVec4());
			const Mat44 xform = scaleXform * rotTransXform;

			// Write out the output values
			for (I64 i = 0; i < numOutputs; ++i)
			{
				float* pOutputPtr = HierarchyFloat(pOutputInfos[i].m_loc, pHierarchyHeader);

				switch(pOutputInfos[i].m_type)
				{
				case 0:		// Translation - X
					*pOutputPtr = xform.Get(3, 0);
					break;

				case 1:		// Translation - Y
					*pOutputPtr = xform.Get(3, 1);
					break;

				case 2:		// Translation - Z
					*pOutputPtr = xform.Get(3, 2);
					break;

				case 3:		// Quat - X
					*pOutputPtr = pJointParam->m_quat.X();
					break;

				case 4:		// Quat - Y
					*pOutputPtr = pJointParam->m_quat.Y();
					break;

				case 5:		// Quat - Z
					*pOutputPtr = pJointParam->m_quat.Z();
					break;

				case 6:		// Quat - W
					*pOutputPtr = pJointParam->m_quat.W();
					break;

				case 7:			// LocalX - X
					*pOutputPtr = xform.Get(0, 0);
					break;

				case 8:			// LocalX - Y
					*pOutputPtr = xform.Get(0, 1);
					break;

				case 9:			// LocalX - Z
					*pOutputPtr = xform.Get(0, 2);
					break;

				case 10:		// LocalY - X
					*pOutputPtr = xform.Get(1, 0);
					break;

				case 11:		// LocalY - Y
					*pOutputPtr = xform.Get(1, 1);
					break;

				case 12:		// LocalY - Z
					*pOutputPtr = xform.Get(1, 2);
					break;

				case 13:		// LocalZ - X
					*pOutputPtr = xform.Get(2, 0);
					break;

				case 14:		// LocalZ - Y
					*pOutputPtr = xform.Get(2, 1);
					break;

				case 15:		// LocalZ - Z
					*pOutputPtr = xform.Get(2, 2);
					break;

				default:
					*pOutputPtr = 1.0f;
					break;
				}
			}

			RIG_NODE_TIMER_END(RigNodeType::kMatrixToFloats);

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("MatrixToFloats (%s)\n", DevKitOnly_StringIdToStringOrHex(pParams->m_nodeNameId));
				MsgAnim("   Trans: %.4f %.4f %.4f\n", (float)xform.Get(3, 0), (float)xform.Get(3, 1), (float)xform.Get(3, 2));
				MsgAnim("   Quat: %.4f %.4f %.4f %.4f\n", (float)pJointParam->m_quat.X(), (float)pJointParam->m_quat.Y(), (float)pJointParam->m_quat.Z(), (float)pJointParam->m_quat.W());
				MsgAnim("   Local X-Axis: %.4f %.4f %.4f\n", (float)xform.Get(0, 0), (float)xform.Get(0, 1), (float)xform.Get(0, 2));
				MsgAnim("   Local Y-Axis: %.4f %.4f %.4f\n", (float)xform.Get(1, 0), (float)xform.Get(1, 1), (float)xform.Get(1, 2));
				MsgAnim("   Local Z-Axis: %.4f %.4f %.4f\n", (float)xform.Get(2, 0), (float)xform.Get(2, 1), (float)xform.Get(2, 2));
			}
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandMatrixToFloats(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			//			const U16 numControlDrivers						= param_qw0[0];
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			MatrixToFloatsParams const* pParams = (MatrixToFloatsParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);
			//			ORBISANIM_ASSERT(numControlDrivers == 1);

			ExecuteCommandMatrixToFloatsImpl(pHierarchyHeader, pParams);
		}
	}	//namespace CommandBlock
}	//namespace OrbisAnim

