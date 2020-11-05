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
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/rig-nodes/rig-nodes.h"

#define ENABLE_DEBUG_DRAWING	1

#if ENABLE_DEBUG_DRAWING
#	include "ndlib/render/util/prim.h"
#endif

bool g_enableRivetPlaneDebugDrawing = false;
bool g_dumpInfoRivetPlane = false;


namespace OrbisAnim
{
	namespace CommandBlock
	{

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandRivetPlaneImpl(const HierarchyHeader* pHierarchyHeader, const RivetPlaneParams* pParams)
		{
			RIG_NODE_TIMER_START();

			const Location* pInputLocArray = (const Location*)(((U8*)pParams) + pParams->m_inputArrayLocOffset);
			const Location* pInputMatrixLocArray = pInputLocArray;
			const Location* pInputPointLocArray = pInputLocArray + pParams->m_numSkinningJoints;
			const Location* pParentMatrixLoc = pInputPointLocArray + (pParams->m_inputPointsAreDriven ? 4 * 3 : 0);		// 12 floats if points are driven

			const RivetOutputInfo* pOutputLocArray = (const RivetOutputInfo*)(((U8*)pParams) + pParams->m_outputArrayLocOffset);

			const Point* pDefaultPlanePointArray = (const Point*)(((U8*)pParams) + pParams->m_planePointArrayLocOffset);
			const Mat44* pBindPoseMatrixArray = (const Mat44*)(((U8*)pParams) + pParams->m_bindPoseMatrixArrayLocOffset);
			const float* pSkinningWeightArray = (const float*)(((U8*)pParams) + pParams->m_skinningWeightArrayLocOffset);
			const float* pInputScaleFactorArray = (const float*)(((U8*)pParams) + pParams->m_inputScaleFactorsLocOffset);


			// Get the parent world matrix
			const OrbisAnim::JointTransform parentJointXform = *((OrbisAnim::JointTransform*)OrbisAnim::HierarchyQuadword(*pParentMatrixLoc, pHierarchyHeader));
			const Mat44 parentJointMatOs = parentJointXform.GetTransform();
#if ENABLE_DEBUG_DRAWING
			if (FALSE_IN_FINAL_BUILD(g_enableRivetPlaneDebugDrawing))
			{
				g_prim.Draw(DebugCoordAxes(parentJointMatOs, 0.05f));
			}
#endif

			// Grab the current joint matrices
			Mat44* pJointMat = STACK_ALLOC_ALIGNED(Mat44, pParams->m_numSkinningJoints, kAlign16);
			for (int i = 0; i < pParams->m_numSkinningJoints; ++i)
			{
				const float inputScaleFactor = pParams->m_inputScaleFactorsLocOffset ? pInputScaleFactorArray[i] : 1.0f;
				Mat44 scaleMat;
				scaleMat.SetScale(Vec4(inputScaleFactor, inputScaleFactor, inputScaleFactor, 1.0f));
				const Mat44 inputMat = ((OrbisAnim::JointTransform*)OrbisAnim::HierarchyQuadword(pInputMatrixLocArray[i], pHierarchyHeader))->GetMat44();
				pJointMat[i] = scaleMat * inputMat;

#if ENABLE_DEBUG_DRAWING
				if (FALSE_IN_FINAL_BUILD(g_enableRivetPlaneDebugDrawing))
				{
					g_prim.Draw(DebugCoordAxes(pJointMat[i], 0.05f));
				}
#endif
			}

			// Grab the inverse bind pose matrix
			Mat44* pInvBindPoseMat = STACK_ALLOC_ALIGNED(Mat44, pParams->m_numSkinningJoints, kAlign16);
			for (int i = 0; i < pParams->m_numSkinningJoints; ++i)
			{
				// Stored as inverse mats
				pInvBindPoseMat[i] = pBindPoseMatrixArray[i];

				// Bake scale
// 				Mat44 original = Inverse(pInvBindPoseMat[i]);
// 				original.SetRow(0, Normalize3(original.GetRow(0)));
// 				original.SetRow(1, Normalize3(original.GetRow(1)));
// 				original.SetRow(2, Normalize3(original.GetRow(2)));
// 				original.SetRow(3, original.GetRow(3) * 1.02f);
// 				pInvBindPoseMat[i] = Inverse(original);
			}


			// TESTING
// 			Mat44* pIdentity = STACK_ALLOC_ALIGNED(Mat44, pParams->m_numSkinningJoints, kAlign16);
// 			for (int i = 0; i < pParams->m_numSkinningJoints; ++i)
// 			{
// 				pIdentity[i] = pInvBindPoseMat[i] * pJointMat[i];
// 			}



			Point* pPlanePointArray = STACK_ALLOC_ALIGNED(Point, 4, kAlign16);
			if (pParams->m_inputPointsAreDriven)
			{
				for (int i = 0; i < 4; ++i)
				{
					float x = *OrbisAnim::HierarchyFloat(pInputPointLocArray[i * 3 + 0], pHierarchyHeader);
					float y = *OrbisAnim::HierarchyFloat(pInputPointLocArray[i * 3 + 1], pHierarchyHeader);
					float z = *OrbisAnim::HierarchyFloat(pInputPointLocArray[i * 3 + 2], pHierarchyHeader);
					pPlanePointArray[i] = Point(x, y, z);
				}
			}
			else
			{
				for (int i = 0; i < 4; ++i)
				{
					pPlanePointArray[i] = pDefaultPlanePointArray[i];
				}
			}

//			// Skin the plane points
//			Point* pSkinnedPlanePoints = STACK_ALLOC_ALIGNED(Point, 4, kAlign16);
//			for (int planePointIndex = 0; planePointIndex < 4; ++planePointIndex)
//			{
//				const Point& planePoint = pPlanePointArray[planePointIndex];
//
//				pSkinnedPlanePoints[planePointIndex] = Point(kOrigin);
//				for (int jointIndex = 0; jointIndex < pParams->m_numSkinningJoints; ++jointIndex)
//				{
//					Point jointSpace = planePoint * Transform(pInvBindPoseMat[jointIndex]);
//					Point newObjectSpace = jointSpace * Transform(pJointMat[jointIndex]);
//					const float jointWeight = pSkinningWeightArray[planePointIndex * pParams->m_numSkinningJoints + jointIndex];
//					pSkinnedPlanePoints[planePointIndex] += (newObjectSpace - kOrigin) * jointWeight;
//				}
//
//#if ENABLE_DEBUG_DRAWING
//				if (FALSE_IN_FINAL_BUILD(g_enableRivetPlaneDebugDrawing))
//				{
//					const U32 kBufLen = 255;
//					char buf[kBufLen + 1];
//					snprintf(buf, kBufLen, "%d", planePointIndex);
//					buf[kBufLen] = '\0';
//					g_prim.Draw(DebugString(planePoint, buf, kColorGreen, 0.5f));
//					g_prim.Draw(DebugSphere(planePoint, 0.015f, kColorGreen));
//
//					g_prim.Draw(DebugString(pSkinnedPlanePoints[planePointIndex], buf, kColorRed, 0.5f));
//					g_prim.Draw(DebugSphere(pSkinnedPlanePoints[planePointIndex], 0.015f, kColorRed));
//				}
//#endif
//			}
			//Point* pSkinnedPlanePoints = STACK_ALLOC_ALIGNED(Point, 4, kAlign16);
			//for (int planePointIndex = 0; planePointIndex < 4; ++planePointIndex)
			//{
			//	const Point& planePoint = pPlanePointArray[planePointIndex];

			//	pSkinnedPlanePoints[planePointIndex] = Point(kOrigin);
			//	for (int jointIndex = 0; jointIndex < pParams->m_numSkinningJoints; ++jointIndex)
			//	{
			//		Point jointSpace = planePoint * Transform(pInvBindPoseMat[jointIndex]);
			//		Point newObjectSpace = jointSpace * Transform(pJointMat[jointIndex]);
			//		const float jointWeight = pSkinningWeightArray[planePointIndex * pParams->m_numSkinningJoints + jointIndex];
			//		pSkinnedPlanePoints[planePointIndex] += (newObjectSpace - kOrigin) * jointWeight;
			//	}
			//}

			Point* pSkinnedPlanePoints = STACK_ALLOC_ALIGNED(Point, 4, kAlign16);
			for (int planePointIndex = 0; planePointIndex < 4; ++planePointIndex)
			{
				pSkinnedPlanePoints[planePointIndex] = Point(kOrigin);
			}
			for (int jointIndex = 0; jointIndex < pParams->m_numSkinningJoints; ++jointIndex)
			{
				Transform newObjectSpaceM = Transform(pInvBindPoseMat[jointIndex]) * Transform(pJointMat[jointIndex]);
				for (int planePointIndex = 0; planePointIndex < 4; ++planePointIndex)
				{
					const float jointWeight = pSkinningWeightArray[planePointIndex * pParams->m_numSkinningJoints + jointIndex];
					const Point& planePoint = pPlanePointArray[planePointIndex];
					Point newObjectSpace = planePoint * newObjectSpaceM;
					pSkinnedPlanePoints[planePointIndex] += (newObjectSpace - kOrigin) * jointWeight;
				}
			}

			//
			// Plane points winding order
			// Let's assume the following points and values to define the plane and tracking point
			// 
			// Points are defined in a 'Z' pattern
			// p[0] = (-0.5, 0, 0.5)
			// p[1] = ( 0.5, 0, 0.5)
			// p[2] = (-0.5, 0,-0.5)
			// p[3] = ( 0.5, 0,-0.5)
			//
			// pointU/pointV = 0.5 / 0.7
			//
			// The reference for the U/V coordinate system is point 2
			//
			//         1           mU1           0
			//                  
			//                 (0.5, 0.7)
			//         mV1          o           mV0
			//                  
			//                      c              
			//                (plane center)     V
			//                                   ^
			//                                   | 
			//                                   |
			//         3           mU0    U <----2
			//


			const float originalVecULength =
				Length(
				(pPlanePointArray[2] + (pPlanePointArray[0] - pPlanePointArray[2]) * pParams->m_pointV) -
					(pPlanePointArray[3] + (pPlanePointArray[1] - pPlanePointArray[3]) * pParams->m_pointV));
			const float originalVecVLength =
				Length(
				(pPlanePointArray[0] + (pPlanePointArray[1] - pPlanePointArray[0]) * pParams->m_pointU) -
					(pPlanePointArray[2] + (pPlanePointArray[3] - pPlanePointArray[2]) * pParams->m_pointU));

			// Calculate the mid points for the plane
			Vector deltaU[2];
			deltaU[0] = pSkinnedPlanePoints[3] - pSkinnedPlanePoints[2];
			deltaU[1] = pSkinnedPlanePoints[1] - pSkinnedPlanePoints[0];

			Vector deltaV[2];
			deltaV[0] = pSkinnedPlanePoints[0] - pSkinnedPlanePoints[2];
			deltaV[1] = pSkinnedPlanePoints[1] - pSkinnedPlanePoints[3];

			Point midPointU[2];
			midPointU[0] = pSkinnedPlanePoints[2] + deltaU[0] * pParams->m_pointU;
			midPointU[1] = pSkinnedPlanePoints[0] + deltaU[1] * pParams->m_pointU;

			Point midPointV[2];
			midPointV[0] = pSkinnedPlanePoints[2] + deltaV[0] * pParams->m_pointV;
			midPointV[1] = pSkinnedPlanePoints[3] + deltaV[1] * pParams->m_pointV;

			// These are our spanning vectors
			const Vector vecU = midPointV[1] - midPointV[0];
			const Vector vecV = midPointU[1] - midPointU[0];
			const float vecULength = Length(vecU);
			const float vecVLength = Length(vecV);

			const Point trackingPointOnPlane = midPointU[0] + vecV * pParams->m_pointV;

			// We need to average rotation in between the the desired orientation (vecU) and the 
			// orientation of 'U' if we use 'V' and the plane normal as the references
			const Vector planeNormal = Normalize(Cross(vecV, vecU));
			const Vector newU = Normalize(Cross(planeNormal, vecV));
			const Vector adjustedU = Normalize((Normalize(vecU) + newU) / 2.0f);
			const Vector adjustedV = Normalize(Cross(adjustedU, planeNormal));

			// 			g_prim.Draw(DebugLine(trackingPointOnPlane, trackingPointOnPlane + adjustedU * 0.05f, kColorRed));
			// 			g_prim.Draw(DebugLine(trackingPointOnPlane, trackingPointOnPlane + planeNormal * 0.05f, kColorGreen));
			// 			g_prim.Draw(DebugLine(trackingPointOnPlane, trackingPointOnPlane + adjustedV * 0.05f, kColorBlue));

			const float scaleMultiplierU = (pParams->m_scaleMultiplierU == 0.0f) ? 1.0f : 0.0f;
			const float scaleMultiplierV = (pParams->m_scaleMultiplierV == 0.0f) ? 1.0f : 0.0f;
			const float stretchFactorU = ((vecULength - originalVecULength) * scaleMultiplierU + originalVecULength) / originalVecULength;
			const float stretchFactorV = ((vecVLength - originalVecVLength) * scaleMultiplierV + originalVecULength) / originalVecVLength;

			const float averageLength = (vecULength + vecVLength) / 2.0f;
			const float averageOriginalLength = (originalVecULength + originalVecVLength) / 2.0f;

			// Adjust the translation based on shrink/stretch
			float bulgeFactor = 0.0;
			if (stretchFactorU < 1.0f)
			{
				const float localBulgeFactor = ((vecULength - originalVecULength) * pParams->m_bulgeShrinkU + originalVecULength) / originalVecULength;
				bulgeFactor -= localBulgeFactor - 1.0f;
			}
			else
			{
				const float localBulgeFactor = ((vecULength - originalVecULength) * pParams->m_bulgeStretchU + originalVecULength) / originalVecULength;
				bulgeFactor += localBulgeFactor - 1.0f;
			}

			if (stretchFactorV < 1.0f)
			{
				const float localBulgeFactor = ((vecVLength - originalVecVLength) * pParams->m_bulgeShrinkV + originalVecVLength) / originalVecVLength;
				bulgeFactor -= localBulgeFactor - 1.0f;
			}
			else
			{
				const float localBulgeFactor = ((vecVLength - originalVecVLength) * pParams->m_bulgeStretchV + originalVecVLength) / originalVecVLength;
				bulgeFactor += localBulgeFactor - 1.0f;
			}

			const Point bulgedTranslation = trackingPointOnPlane + planeNormal * ((originalVecULength + originalVecVLength) * 0.5f) * bulgeFactor;

			// Some dynamic math to have a base offset from the plane
			Point finalTranslationOs = bulgedTranslation + planeNormal * ((averageOriginalLength + pParams->m_normalOffset) / averageOriginalLength - 1.0f);

			// Construct the final matrix
			Mat44 trackingMatOs = Mat44(kIdentity);
			trackingMatOs.SetRow(0, adjustedU.GetVec4());
			trackingMatOs.SetRow(1, planeNormal.GetVec4());
			trackingMatOs.SetRow(2, adjustedV.GetVec4());
			trackingMatOs.SetRow(3, finalTranslationOs.GetVec4());

			// Create the joint orient matrix (Assumes RotateOrder of XYZ...) Bug?
			Mat44 jointOrientX;
			Mat44 jointOrientY;
			Mat44 jointOrientZ;
			jointOrientX.SetRotateX(pParams->m_jointOrient[0]);
			jointOrientY.SetRotateY(pParams->m_jointOrient[1]);
			jointOrientZ.SetRotateZ(pParams->m_jointOrient[2]);
			const Mat44 jointOrient = jointOrientX * jointOrientY * jointOrientZ;

			// Transform our final transform into local space of the parent joint
			const Mat44 invParentJointMatOs = Inverse(parentJointMatOs);
			const Mat44 trackingMatLs = trackingMatOs * invParentJointMatOs;

			const Vector finalTranslationLs = Vector(trackingMatLs.GetRow(3));

			// Calculate the rotation in jointOrient space
			Mat44 tempRotMatLs = trackingMatLs;
			tempRotMatLs.SetRow(3, Vec4(0.0f, 0.0f, 0.0f, 1.0f));
			const Mat44 finalRotMatLs = tempRotMatLs * Inverse(jointOrient);


			// Output the components of the matrix
			float eulerXRad, eulerYRad, eulerZRad;
			finalRotMatLs.GetEulerAngles(eulerXRad, eulerYRad, eulerZRad);
			const float eulerXDeg = RADIANS_TO_DEGREES(eulerXRad);
			const float eulerYDeg = RADIANS_TO_DEGREES(eulerYRad);
			const float eulerZDeg = RADIANS_TO_DEGREES(eulerZRad);

			for (int i = 0; i < pParams->m_numOutputs; ++i)
			{
				const U32 outputAttributeIndex = pOutputLocArray[i].m_type;
				const Location outputLoc = pOutputLocArray[i].m_loc;

				ANIM_ASSERTF(outputAttributeIndex <= 7, ("FAIL - RivetPlane output attribute index is out-of-range [%u]", outputAttributeIndex));
				switch (outputAttributeIndex)
				{
					// Translation
				case 0:		*OrbisAnim::HierarchyFloat(outputLoc, pHierarchyHeader) = finalTranslationLs.X();	break;
				case 1:		*OrbisAnim::HierarchyFloat(outputLoc, pHierarchyHeader) = finalTranslationLs.Y();	break;
				case 2:		*OrbisAnim::HierarchyFloat(outputLoc, pHierarchyHeader) = finalTranslationLs.Z();	break;

					// Rotation
				case 3:		*OrbisAnim::HierarchyFloat(outputLoc, pHierarchyHeader) = eulerXDeg;	break;
				case 4:		*OrbisAnim::HierarchyFloat(outputLoc, pHierarchyHeader) = eulerYDeg;	break;
				case 5:		*OrbisAnim::HierarchyFloat(outputLoc, pHierarchyHeader) = eulerZDeg;	break;

					// Plane scale
				case 6:		*OrbisAnim::HierarchyFloat(outputLoc, pHierarchyHeader) = stretchFactorU;	break;
				case 7:		*OrbisAnim::HierarchyFloat(outputLoc, pHierarchyHeader) = stretchFactorV;	break;
				}
			}

			RIG_NODE_TIMER_END(RigNodeType::kRivetPlane);

#if ENABLE_DEBUG_DRAWING
			if (FALSE_IN_FINAL_BUILD(g_enableRivetPlaneDebugDrawing))
			{
				g_prim.Draw(DebugLine(midPointU[0], midPointU[1], kColorRedTrans));
				g_prim.Draw(DebugLine(midPointV[0], midPointV[1], kColorBlueTrans));

				// Offset the matrix  so that I can see it
				Mat44 displMat = trackingMatOs;
				displMat.SetRow(3, displMat.GetRow(3) + Vec4(0.0f, 0.2f, 0.0f, 0.0f));
				g_prim.Draw(DebugCoordAxes(displMat, 0.2f));
			}
#endif

			// Dump debug info
			if (FALSE_IN_FINAL_BUILD(g_dumpInfoRivetPlane))
			{
				MsgAnim("=======[ Rivet Plane ]==================================================================\n");

				MsgAnim("Input points\n");
				MsgAnim("-------------\n");
				for (int i = 0; i < 4; ++i)
				{
					MsgAnim("Input Point %d: %+f, %+f, %+f\n", i, (float)pPlanePointArray[i].X(), (float)pPlanePointArray[i].Y(), (float)pPlanePointArray[i].Z());
				}

				for (int i = 0; i < 4; ++i)
				{
					MsgAnim("Post Skinning Point %d: %+f, %+f, %+f\n", i, (float)pSkinnedPlanePoints[i].X(), (float)pSkinnedPlanePoints[i].Y(), (float)pSkinnedPlanePoints[i].Z());
				}

				MsgAnim("Intersection Point: %+f, %+f, %+f\n", (float)trackingPointOnPlane.X(), (float)trackingPointOnPlane.Y(), (float)trackingPointOnPlane.Z());

				MsgAnim("\nResult joint's Joint Orient (deg):   %+f, %+f, %+f\n", RADIANS_TO_DEGREES(pParams->m_jointOrient[0]), RADIANS_TO_DEGREES(pParams->m_jointOrient[1]), RADIANS_TO_DEGREES(pParams->m_jointOrient[2]));

				MsgAnim("Input Points Are Driven: %s\n", pParams->m_inputPointsAreDriven ? "TRUE" : "FALSE");
				MsgAnim("Scale Multiplier U / V:  %+f / %+f\n", pParams->m_scaleMultiplierU, pParams->m_scaleMultiplierV);
				MsgAnim("Bulge Shrink U / V:      %+f /%+f\n", pParams->m_bulgeShrinkU, pParams->m_bulgeShrinkV);
				MsgAnim("Bulge Stretch U / V:     %+f /%+f\n", pParams->m_bulgeStretchU, pParams->m_bulgeStretchV);
				MsgAnim("Normal Offset:           %+f\n", pParams->m_normalOffset);


				MsgAnim("Output Translation: %+f, %+f, %+f\n", (float)finalTranslationLs.X(), (float)finalTranslationLs.Y(), (float)finalTranslationLs.Z());
				MsgAnim("Output Rotation (deg): %+f, %+f, %+f\n", eulerXDeg, eulerYDeg, eulerZDeg);
				MsgAnim("Output Stretch U/V: %+f / %+f\n", stretchFactorU, stretchFactorV);
			}

			if (FALSE_IN_FINAL_BUILD(g_printRigNodeOutputs))
			{
				MsgAnim("RivetPlane (%s)\n", DevKitOnly_StringIdToStringOrHex(pParams->m_nodeNameId));
				MsgAnim("   outTrans: %.4f %.4f %.4f\n", (float)finalTranslationLs.X(), (float)finalTranslationLs.Y(), (float)finalTranslationLs.Z());
				MsgAnim("   outRotate: %.4f %.4f %.4f deg\n", eulerXDeg, eulerYDeg, eulerZDeg);
				MsgAnim("   stretch: U: %.4f, V: %.4f\n", stretchFactorU, stretchFactorV);
			}
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void ExecuteCommandRivetPlane(DispatcherFunctionArgs_arg_const param_qw0, LocationMemoryMap_arg_const memoryMap, OrbisAnim::SegmentContext* /*pSegmentContext*/)
		{
			HierarchyHeader const* pHierarchyHeader = (HierarchyHeader const*)OrbisAnim::CommandBlock::LocationToPointer(param_qw0[1], memoryMap);
			// param_qw0[2] is padding
			RivetPlaneParams const* pParams = (RivetPlaneParams const*)OrbisAnim::CommandBlock::LocationToPointer(((uint32_t)param_qw0[4] << 16) | (uint32_t)param_qw0[3], memoryMap);

			ExecuteCommandRivetPlaneImpl(pHierarchyHeader, pParams);
		}
	}	//namespace CommandBlock
}	//namespace OrbisAnim
