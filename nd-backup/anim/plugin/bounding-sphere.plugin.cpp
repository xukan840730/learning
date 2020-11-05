/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "corelib/math/sphere.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/bounding-data.h"
#include "ndlib/anim/plugin/plugin-shared.h"
#include "ndlib/anim/nd-anim-plugins.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/skel-boundingbox-defines.h"

#include <orbisanim/joints.h>
#include <orbisanim/structs.h>


/// --------------------------------------------------------------------------------------------------------------- ///
void ComputeBoundsSimple(BoundingData* pOutputBoundingData,
						 const Locator& alignWs,
						 Vector_arg scale,
						 Vector_arg invScale,
						 const Locator* pJointLocsWs,
						 U32F numJoints,
						 I32F visSphereJointIndex,
						 const SMath::Vec4* pVisSphere,
						 I16 excludeJointIndex[2])
{
	if (visSphereJointIndex <= -1000)
	{
		// What the hack is this??
		ANIM_ASSERT(false);
		visSphereJointIndex += 1000;
	}

	// Calculate the bounding sphere
	if (visSphereJointIndex >= 0)
	{
		//ASSERT(false); // is this ever a thing still?

		// Only calculate the AABB on the segment containing the visSphereJoint
		if (visSphereJointIndex < numJoints)
		{
			const SMath::Scalar kOne(1.0f);

			const SMath::Vec4 cachedSphere(*pVisSphere);
			const Vector sphereOffset(cachedSphere);

			const Point jointLocOs = alignWs.UntransformPoint(pJointLocsWs[visSphereJointIndex].Pos());
			const Point sphereCenterOs = jointLocOs + sphereOffset;
			const Point sphereCenterWs = alignWs.TransformPoint(sphereCenterOs);

			Sphere sphereWs(sphereCenterWs.GetVec4());
			sphereWs.SetRadius(cachedSphere.W());

			pOutputBoundingData->m_jointBoundingSphere = sphereWs;

			// Approximation of the AABB.
			const Vector scaledDiagonal = invScale * sphereWs.GetRadius();
			pOutputBoundingData->m_aabb = Aabb(sphereCenterOs - scaledDiagonal, sphereCenterOs + scaledDiagonal);
		}
	}
	else if (visSphereJointIndex == -2)
	{
		ASSERT(false); // is this ever a thing still?

		const SMath::Scalar kOne(1.0f);

		const SMath::Vec4 cachedSphere(*pVisSphere);
		SMath::Point sphereOffset(cachedSphere);

		const SMath::Point sphereCenterOs = sphereOffset;
		const SMath::Point sphereCenterWs = sphereCenterOs;

		Sphere sphereWs(sphereCenterWs.GetVec4());
		sphereWs.SetRadius(cachedSphere.W());

		pOutputBoundingData->m_jointBoundingSphere = sphereWs;

		// Approximation of the AABB.
		const Vector scaledDiagonal = invScale * sphereWs.GetRadius();
		pOutputBoundingData->m_aabb = Aabb(sphereCenterOs - scaledDiagonal, sphereCenterOs + scaledDiagonal);
	}
	else if (visSphereJointIndex == -3)
	{
		ASSERT(false); // is this ever a thing still?

		// The VisBox has X, Y, Z and inflate. If the Z, Y or Z value is negative it means that the box extends in both directions of the joint
		// Only do this on the first segment as the dimensions are relative to the root (pJointTransforms[0])
		const SMath::Vec4 visBoxInfo(*pVisSphere);
		const Scalar paddingRadius = visBoxInfo.W();

		const Locator jointOs = alignWs.UntransformLocator(pJointLocsWs[0]);
		const SMath::Transform xformOs = jointOs.AsTransform();
		const Point jointPosOs = xformOs.GetTranslation();
		const SMath::Vector xAxis = xformOs.GetAxis(0);
		const SMath::Vector yAxis = xformOs.GetAxis(1);
		const SMath::Vector zAxis = xformOs.GetAxis(2);

		Aabb boundingBox(jointPosOs, jointPosOs);
		boundingBox.IncludePoint(jointPosOs + xAxis * visBoxInfo.X());
		boundingBox.IncludePoint(jointPosOs + yAxis * visBoxInfo.Y());
		boundingBox.IncludePoint(jointPosOs + zAxis * visBoxInfo.Z());

		if (IsNegative(visBoxInfo.X()))
		{
			boundingBox.IncludePoint(jointPosOs - xAxis * visBoxInfo.X());
		}
		if (IsNegative(visBoxInfo.Y()))
		{
			boundingBox.IncludePoint(jointPosOs - yAxis * visBoxInfo.Y());
		}
		if (IsNegative(visBoxInfo.Z()))
		{
			boundingBox.IncludePoint(jointPosOs - zAxis * visBoxInfo.Z());
		}

		boundingBox.Expand(paddingRadius);

		Sphere boundingSphere(boundingBox.ToSphere());
		boundingSphere.SetCenter(alignWs.TransformPoint(boundingSphere.GetCenter()));
		pOutputBoundingData->m_jointBoundingSphere = boundingSphere;

		// Un-scale for object space
		boundingBox.m_min = kOrigin + (boundingBox.m_min - kOrigin) * invScale;
		boundingBox.m_max = kOrigin + (boundingBox.m_max - kOrigin) * invScale;
		pOutputBoundingData->m_aabb = boundingBox;
	}
	else
	{
		// Dynamically calculate the best fitting AABB and BSphere around the joints
		float paddingRadius = 0.25f;
		if (visSphereJointIndex == -4)
			paddingRadius = pVisSphere->W();

		Vector minPos(kLargestFloat, kLargestFloat, kLargestFloat);
		Vector maxPos(-kLargestFloat, -kLargestFloat, -kLargestFloat);
		Vector minPosOs(kLargestFloat, kLargestFloat, kLargestFloat);
		Vector maxPosOs(-kLargestFloat, -kLargestFloat, -kLargestFloat);

		for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
		{
			if (iJoint == U32F(excludeJointIndex[0]) || iJoint == U32F(excludeJointIndex[1]))
				continue;

			const Vector current = pJointLocsWs[iJoint].GetTranslation() - kOrigin;

			Locator locOs = alignWs.UntransformLocator(pJointLocsWs[iJoint]);
			const Vector currentOs = locOs.GetTranslation() - kOrigin;

			// Get the minimum of all the components of the Vec4.
			minPos = Min(minPos, current);
			maxPos = Max(maxPos, current);
			minPosOs = Min(minPosOs, currentOs);
			maxPosOs = Max(maxPosOs, currentOs);
		}

		const Point centerPos = kOrigin + (minPos + maxPos) * 0.5f;
		const SMath::Vector diagonal(maxPos - minPos);

		Sphere sphereWs(centerPos, Length(diagonal) * 0.5f + paddingRadius * MaxComp(scale));
		pOutputBoundingData->m_jointBoundingSphere = sphereWs;

		const Vector pad(paddingRadius, paddingRadius, paddingRadius);

		// Un-scale for object space
		pOutputBoundingData->m_aabb = Aabb(kOrigin + minPosOs * invScale - pad, kOrigin + maxPosOs * invScale + pad);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ComputeBoundsSimpleAABB(BoundingData* pOutputBoundingData,
							 const Locator& alignWs,
							 Vector_arg scale,
							 Vector_arg invScale,
							 const Locator* pJointLocsWs,
							 U32F numJoints,
							 I32F boundingVolumeJointIndex,
							 const Aabb* pAabbOs,
							 float paddingRadius,
							 I16 excludeJointIndex[2])
{
	// Calculate the bounding sphere
	if (boundingVolumeJointIndex >= 0)
	{
		if (boundingVolumeJointIndex < numJoints)
		{
			const Vector jointVecOs = (alignWs.UntransformPoint(pJointLocsWs[boundingVolumeJointIndex].Pos()) - kOrigin) * invScale; // un-scale for object space
			pOutputBoundingData->m_aabb = *pAabbOs;
			pOutputBoundingData->m_aabb.m_min += jointVecOs; 
			pOutputBoundingData->m_aabb.m_max += jointVecOs;		

			Vec4 sphereOsScaled = pAabbOs->ToSphere() * Vec4(scale, MaxComp(scale));
			Vec4 sphereWs = pJointLocsWs[boundingVolumeJointIndex].TransformAsPoint(sphereOsScaled);
			pOutputBoundingData->m_jointBoundingSphere = Sphere(sphereWs);
		}
	}
	else
	{
		//// Dynamically calculate the best fitting AABB and BSphere around the joints
		Vector minPos(kLargestFloat, kLargestFloat, kLargestFloat);
		Vector maxPos(-kLargestFloat, -kLargestFloat, -kLargestFloat);
		Vector minPosOs(kLargestFloat, kLargestFloat, kLargestFloat);
		Vector maxPosOs(-kLargestFloat, -kLargestFloat, -kLargestFloat);

		for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
		{
			if (iJoint == U32F(excludeJointIndex[0]) || iJoint == U32F(excludeJointIndex[1]))
				continue;

			const Vector current = pJointLocsWs[iJoint].GetTranslation() - kOrigin;

			Locator locOs = alignWs.UntransformLocator(pJointLocsWs[iJoint]);
			const Vector currentOs = locOs.GetTranslation() - kOrigin;

			// Get the minimum of all the components of the Vec4.
			minPos = Min(minPos, current);
			maxPos = Max(maxPos, current);
			minPosOs = Min(minPosOs, currentOs);
			maxPosOs = Max(maxPosOs, currentOs);
		}

		const Point centerPos = kOrigin + (minPos + maxPos) * 0.5f;
		const SMath::Vector diagonal(maxPos - minPos);

		Sphere sphereWs(centerPos, Length(diagonal) * 0.5f + paddingRadius * MaxComp(scale));
		pOutputBoundingData->m_jointBoundingSphere = sphereWs;

		const Vector pad(paddingRadius, paddingRadius, paddingRadius);

		// Un-scale for object space
		pOutputBoundingData->m_aabb = Aabb(kOrigin + minPosOs * invScale - pad, kOrigin + maxPosOs * invScale + pad);
	}

}

/// --------------------------------------------------------------------------------------------------------------- ///
void ComputeBounds(BoundingData* pOutputBoundingData,
				   const OrbisAnim::SegmentContext* pSegmentContext,
				   const SMath::Transform* pObjXform,
				   U32F numJoints,
				   const OrbisAnim::JointTransform* pJointTransforms,
				   I32F visSphereJointIndex,
				   const SMath::Vec4* pVisSphere,
				   I16 excludeJointIndex[2],
				   float clothBoundingBoxMult,
				   const DC::JointsTable* pBbJoints)
{
	// Pull the info about this segment
	const U16 firstJointIndex = pSegmentContext->m_pSegment->m_firstJoint;
	numJoints = pSegmentContext->m_pSegment->m_numJoints;

	bool hackExpandAabb = false;

	if (visSphereJointIndex <= -1000)
	{
		visSphereJointIndex += 1000;
		hackExpandAabb = true;
	}

	// Calculate the bounding sphere
	if (visSphereJointIndex >= 0)
	{
		// Only calculate the AABB on the segment containing the visSphereJoint
		if (visSphereJointIndex >= firstJointIndex && 
			visSphereJointIndex < (firstJointIndex + numJoints))
		{
				const SMath::Scalar kOne(1.0f);

				const SMath::Vec4 cachedSphere(*pVisSphere);
				SMath::Point sphereOffset(cachedSphere);

				const SMath::Transform xformOs(pJointTransforms[visSphereJointIndex].GetTransform());
				const SMath::Point sphereCenterOs = sphereOffset * xformOs;
				const SMath::Point sphereCenterWs = sphereCenterOs * (*pObjXform);

				Sphere sphereWs(sphereCenterWs.GetVec4());
				sphereWs.SetRadius(cachedSphere.W());

				pOutputBoundingData->m_jointBoundingSphere = sphereWs;

				// Approximation of the AABB.
				const Vector diagonalDir = Vector(kUnitXAxis) + Vector(kUnitYAxis) + Vector(kUnitZAxis);
				const Vector scaledDiagonal = diagonalDir * sphereWs.GetRadius();
				pOutputBoundingData->m_aabb = Aabb(sphereCenterOs - scaledDiagonal,
												   sphereCenterOs + scaledDiagonal);
		}
	}
	else if (visSphereJointIndex == -2)
	{
		// Always put the bounding sphere at a fixed location. Don't use the position of the joints at all.
		// Only do this on the first segment as it will be same every time.
		if (pSegmentContext->m_iSegment == 0)
		{
			const SMath::Scalar kOne(1.0f);

			const SMath::Vec4 cachedSphere(*pVisSphere);
			SMath::Point sphereOffset(cachedSphere);

			const SMath::Point sphereCenterOs = sphereOffset;
			const SMath::Point sphereCenterWs = sphereCenterOs;

			Sphere sphereWs(sphereCenterWs.GetVec4());
			sphereWs.SetRadius(cachedSphere.W());

			pOutputBoundingData->m_jointBoundingSphere = sphereWs;

			// Approximation of the AABB.
			const Vector diagonalDir = Vector(kUnitXAxis) + Vector(kUnitYAxis) + Vector(kUnitZAxis);
			const Vector scaledDiagonal = diagonalDir * sphereWs.GetRadius();
			pOutputBoundingData->m_aabb = Aabb(sphereCenterOs - scaledDiagonal,
				sphereCenterOs + scaledDiagonal);
		}
	}
	else if (visSphereJointIndex == -3)
	{
		// The VisBox has X, Y, Z and inflate. If the Z, Y or Z value is negative it means that the box extends in both directions of the joint
		// Only do this on the first segment as the dimensions are relative to the root (pJointTransforms[0])
		if (pSegmentContext->m_iSegment == 0)
		{
			const SMath::Vec4 visBoxInfo(*pVisSphere);
			const Scalar paddingRadius = visBoxInfo.W();

			const SMath::Transform xformOs(pJointTransforms[0].GetTransform());
			const Point jointPosOs = xformOs.GetTranslation();
			const SMath::Vector xAxis = xformOs.GetAxis(0);
			const SMath::Vector yAxis = xformOs.GetAxis(1);
			const SMath::Vector zAxis = xformOs.GetAxis(2);

			Aabb boundingBox(jointPosOs, jointPosOs);
			boundingBox.IncludePoint(jointPosOs + xAxis * visBoxInfo.X());
			boundingBox.IncludePoint(jointPosOs + yAxis * visBoxInfo.Y());
			boundingBox.IncludePoint(jointPosOs + zAxis * visBoxInfo.Z());

			if (IsNegative(visBoxInfo.X()))
			{
				boundingBox.IncludePoint(jointPosOs - xAxis * visBoxInfo.X());
			}
			if (IsNegative(visBoxInfo.Y()))
			{
				boundingBox.IncludePoint(jointPosOs - yAxis * visBoxInfo.Y());
			}
			if (IsNegative(visBoxInfo.Z()))
			{
				boundingBox.IncludePoint(jointPosOs - zAxis * visBoxInfo.Z());
			}

			boundingBox.Expand(paddingRadius);

			pOutputBoundingData->m_aabb = boundingBox;

			Sphere boundingSphere(boundingBox.ToSphere());
			boundingSphere.SetCenter(boundingSphere.GetCenter() * (*pObjXform));
			pOutputBoundingData->m_jointBoundingSphere = boundingSphere;
		}
	}
	else
	{
		// Dynamically calculate the best fitting AABB and BSphere around the joints
		float paddingRadius = 0.25f;
		if (visSphereJointIndex == -4)
			paddingRadius = pVisSphere->W();

		const Vec4 pad = Vec4(paddingRadius, paddingRadius, paddingRadius, paddingRadius);

		SMath::Vec4 minPos(kLargestFloat, kLargestFloat, kLargestFloat, kLargestFloat);
		SMath::Vec4 maxPos(-kLargestFloat, -kLargestFloat, -kLargestFloat, -kLargestFloat);
		SMath::Vec4 minPosOs(kLargestFloat, kLargestFloat, kLargestFloat, kLargestFloat);
		SMath::Vec4 maxPosOs(-kLargestFloat, -kLargestFloat, -kLargestFloat, -kLargestFloat);

		//If we have a list of joints to use to create the bb then use it or else use all the joints
		if (pBbJoints)
		{
			for (int ii = 0; ii < pBbJoints->m_count; ++ii)
			{
				int jointIndex = pBbJoints->m_indices[ii];
				SMath::Transform xformOs(pJointTransforms[jointIndex].GetTransform());
				SMath::Transform xform = xformOs * (*pObjXform);

				const SMath::Vec4 current = xform.GetTranslation().GetVec4();
				const SMath::Vec4 currentOs = xformOs.GetTranslation().GetVec4();

				// Get the minimum of all the components of the Vec4.
				minPos = Min(minPos, current);
				maxPos = Max(maxPos, current);
				minPosOs = Min(minPosOs, currentOs);
				maxPosOs = Max(maxPosOs, currentOs);
			}
		}
		else
		{
			for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
			{
				if (iJoint == U32F(excludeJointIndex[0]) || iJoint == U32F(excludeJointIndex[1]))
					continue;

				SMath::Transform xformOs(pJointTransforms[iJoint].GetTransform());
				SMath::Transform xform = xformOs * (*pObjXform);

				const SMath::Vec4 current = xform.GetTranslation().GetVec4();
				const SMath::Vec4 currentOs = xformOs.GetTranslation().GetVec4();

				// Get the minimum of all the components of the Vec4.
				minPos = Min(minPos, current);
				maxPos = Max(maxPos, current);
				minPosOs = Min(minPosOs, currentOs);
				maxPosOs = Max(maxPosOs, currentOs);
			}
		}

		const SMath::Vec4 centerPos = (minPos + maxPos) * 0.5f;
		const SMath::Vector diagonal(maxPos - minPos);

		if (clothBoundingBoxMult != 1.0f)
		{
			// Giant hack for T1PS4 to increase size of bounding boxes for cloth, since the joint positions at the time of animation are incorrect
			const SMath::Vector diagonalExtended = clothBoundingBoxMult*diagonal;
			minPos = centerPos - 0.5f*diagonalExtended.GetVec4();
			maxPos = centerPos + 0.5f*diagonalExtended.GetVec4();

			const SMath::Vec4 centerPosOs = (minPosOs + maxPosOs) * 0.5f;
			const SMath::Vector diagonalOs(maxPosOs - minPosOs);

			const SMath::Vector diagonalExtendedOs = clothBoundingBoxMult*diagonalOs;
			minPosOs = centerPosOs - 0.5f*diagonalExtendedOs.GetVec4();
			maxPosOs = centerPosOs + 0.5f*diagonalExtendedOs.GetVec4();
		}

		minPosOs -= pad;
		maxPosOs += pad;

		Sphere sphereWs(centerPos);
		sphereWs.SetRadius(Length(diagonal) * 0.5f + paddingRadius);
		
		// Due to the nature of segmented skeletons we need to combine all the AABB's and bounding sphere's as we
		// go through each segment.
		if (pSegmentContext->m_iSegment == 0)
		{
			pOutputBoundingData->m_jointBoundingSphere = sphereWs;
			pOutputBoundingData->m_aabb = Aabb(Point(minPosOs), Point(maxPosOs));
		}
		else
		{
			pOutputBoundingData->m_jointBoundingSphere.CombineSpheres(sphereWs);
			pOutputBoundingData->m_aabb.Join(Aabb(Point(minPosOs), Point(maxPosOs)));
		}
	}

	if (hackExpandAabb)
	{
		const float largestVal = Dist(pOutputBoundingData->m_aabb.m_max, pOutputBoundingData->m_aabb.m_min) * 0.5f;

		const Point center = pOutputBoundingData->m_aabb.GetCenter();
		const Point posMax = Vector(largestVal, largestVal, largestVal) + center;
		const Point posMin = Vector(-largestVal, -largestVal, -largestVal) + center;

		pOutputBoundingData->m_aabb.IncludePoint(posMax);
		pOutputBoundingData->m_aabb.IncludePoint(posMin);
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void ComputeBoundsAABB(BoundingData* pOutputBoundingData,
					   const OrbisAnim::SegmentContext* pSegmentContext,
					   const SMath::Transform* pObjXform,
					   U32F numJoints,
					   const OrbisAnim::JointTransform* pJointTransforms,
					   I32F boundingVolumeJointIndex,
					   const Aabb* pAabbOs,
					   F32 dynamicPaddingRadius,
					   I16 excludeJointIndex[2],
					   float clothBoundingBoxMult,
					   const DC::JointsTable* pBbJoints)
{
	// Pull the info about this segment
	const U16 firstJointIndex	= pSegmentContext->m_pSegment->m_firstJoint;
	numJoints					= pSegmentContext->m_pSegment->m_numJoints;




	// Calculate the bounding sphere
	if (boundingVolumeJointIndex >= 0)
	{
		// Only calculate the AABB on the segment containing the visSphereJoint
		if (boundingVolumeJointIndex >= firstJointIndex && 
			boundingVolumeJointIndex < (firstJointIndex + numJoints))
		{
			const Point jointLocOs = pJointTransforms[boundingVolumeJointIndex].GetTranslation();
			pOutputBoundingData->m_aabb		= *pAabbOs;
			pOutputBoundingData->m_aabb.m_min += Vector(jointLocOs.GetVec4());		
			pOutputBoundingData->m_aabb.m_max += Vector(jointLocOs.GetVec4());		

			SMath::Vec4 sphereCenterOS = pOutputBoundingData->m_aabb.ToSphere();
			SMath::Vec4 sphereCenterWS = SMath::Vec4(sphereCenterOS.X(), sphereCenterOS.Y(), sphereCenterOS.Z(), 1.0f) * (*pObjXform);
			pOutputBoundingData->m_jointBoundingSphere = Sphere(sphereCenterWS.X(), sphereCenterWS.Y(), sphereCenterWS.Z(), sphereCenterOS.W());
		}
	}
	else
	{
		// Dynamically calculate the best fitting AABB and BSphere around the joints
		const Vec4 pad = Vec4(dynamicPaddingRadius, dynamicPaddingRadius, dynamicPaddingRadius, dynamicPaddingRadius);

		SMath::Vec4 minPos(kLargestFloat, kLargestFloat, kLargestFloat, kLargestFloat);
		SMath::Vec4 maxPos(-kLargestFloat, -kLargestFloat, -kLargestFloat, -kLargestFloat);
		SMath::Vec4 minPosOs(kLargestFloat, kLargestFloat, kLargestFloat, kLargestFloat);
		SMath::Vec4 maxPosOs(-kLargestFloat, -kLargestFloat, -kLargestFloat, -kLargestFloat);

		//If we have a list of joints to use to create the bb then use it or else use all the joint
		if (pBbJoints)
		{
			for (int ii = 0; ii < pBbJoints->m_count; ++ii)
			{
				int jointIndex = pBbJoints->m_indices[ii];
				SMath::Transform xformOs(pJointTransforms[jointIndex].GetTransform());
				SMath::Transform xform = xformOs * (*pObjXform);

				const SMath::Vec4 current = xform.GetTranslation().GetVec4();
				const SMath::Vec4 currentOs = xformOs.GetTranslation().GetVec4();

				// Get the minimum of all the components of the Vec4.
				minPos = Min(minPos, current);
				maxPos = Max(maxPos, current);
				minPosOs = Min(minPosOs, currentOs);
				maxPosOs = Max(maxPosOs, currentOs);
			}
		}
		else
		{
			for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
			{
				if (iJoint == U32F(excludeJointIndex[0]) || iJoint == U32F(excludeJointIndex[1]))
					continue;

				SMath::Transform xformOs(pJointTransforms[iJoint].GetTransform());
				SMath::Transform xform = xformOs * (*pObjXform);

				const SMath::Vec4 current = xform.GetTranslation().GetVec4();
				const SMath::Vec4 currentOs = xformOs.GetTranslation().GetVec4();

				// Get the minimum of all the components of the Vec4.
				minPos = Min(minPos, current);
				maxPos = Max(maxPos, current);
				minPosOs = Min(minPosOs, currentOs);
				maxPosOs = Max(maxPosOs, currentOs);
			}
		}

		const SMath::Vec4 centerPos = (minPos + maxPos) * 0.5f;
		const SMath::Vector diagonal(maxPos - minPos);

		if (clothBoundingBoxMult != 1.0f)
		{
			// Giant hack for T1PS4 to increase size of bounding boxes for cloth, since the joint positions at the time of animation are incorrect
			const SMath::Vector diagonalExtended = clothBoundingBoxMult*diagonal;
			minPos = centerPos - 0.5f*diagonalExtended.GetVec4();
			maxPos = centerPos + 0.5f*diagonalExtended.GetVec4();

			const SMath::Vec4	centerPosOs = (minPosOs + maxPosOs) * 0.5f;
			const SMath::Vector diagonalOs(maxPosOs - minPosOs);

			const SMath::Vector diagonalExtendedOs = clothBoundingBoxMult*diagonalOs;
			minPosOs = centerPosOs - 0.5f*diagonalExtendedOs.GetVec4();
			maxPosOs = centerPosOs + 0.5f*diagonalExtendedOs.GetVec4();
		}

		minPosOs -= pad;
		maxPosOs += pad;

		Sphere sphereWs(centerPos);
		sphereWs.SetRadius(Length(diagonal) * 0.5f + dynamicPaddingRadius);

		// Due to the nature of segmented skeletons we need to combine all the AABB's and bounding sphere's as we
		// go through each segment.
		if (pSegmentContext->m_iSegment == 0)
		{
			pOutputBoundingData->m_jointBoundingSphere = sphereWs;
			pOutputBoundingData->m_aabb = Aabb(Point(minPosOs), Point(maxPosOs));
		}
		else
		{
			pOutputBoundingData->m_jointBoundingSphere.CombineSpheres(sphereWs);
			pOutputBoundingData->m_aabb.Join(Aabb(Point(minPosOs), Point(maxPosOs)));
		}
	}

}

/// --------------------------------------------------------------------------------------------------------------- ///
OrbisAnim::Status BoundingSphereAnimPluginCallback(const OrbisAnim::SegmentContext* pSegmentContext, void* pContext)
{
	// Only update the bounding volume using segment 0
	if (pSegmentContext->m_iSegment)
		return OrbisAnim::kSuccess;

	OrbisAnim::AnimHierarchy const* pHierarchy = pSegmentContext->m_pAnimHierarchy;
	const BoundingSphereAnimPluginData* pData = (const BoundingSphereAnimPluginData*)pContext;
	const SMath::Transform* pObjXform = pData->m_pObjXform;
	U16 numJoints = pSegmentContext->m_pSegment->m_numJoints;
//	const OrbisAnim::JointTransform* pJointTransforms = (const OrbisAnim::JointTransform*)pData->m_locJointTransforms;
	const OrbisAnim::JointTransform* pJointTransforms = (const OrbisAnim::JointTransform*)((U8*)pSegmentContext->m_locJointTransforms + sizeof(OrbisAnim::JointTransform)); // Index '0' in the JointTransform array is always a 'dummy' identity matrix
	I16 visSphereJointIndex = pData->m_visSphereJointIndex;
	const SMath::Vec4* pVisSphere =	pData->m_pVisSphere;
	BoundingData* pOutputBoundingData = pData->m_pBoundingInfo;
	const Aabb* pVisAabb = pData->m_pVisAabb;
	const F32	dynamicPaddingRadius = pData->m_dynamicPaddingRadius;

	I16 excludeJointIndex[2];
	excludeJointIndex[0] = pData->m_boundingSphereExcludeJoints[0];
	excludeJointIndex[1] = pData->m_boundingSphereExcludeJoints[1];

	float clothBoundingBoxMult = pData->m_clothBoundingBoxMult;

#ifdef ANIM_DEBUG
	ValidateObjectTransform(pObjXform);
	ValidateJointTransforms((OrbisAnim::JointTransform*)pJointTransforms, numJoints);
#endif

	const DC::JointsTable* pJoints = nullptr;
	if (const DC::Map* pBbToBonesMap = EngineComponents::GetScriptManager()->LookupInModule<DC::Map>(SID("*joints-table-map*"), SID("skel-boundingbox")))
	{
		pJoints = ScriptManager::MapLookup<DC::JointsTable>(pBbToBonesMap, pData->m_skeletonNameId);
	}

	if (pData->m_useBoundingBox)
	{
		ComputeBoundsAABB(pOutputBoundingData,
						  pSegmentContext,
						  pObjXform,
						  numJoints,
						  pJointTransforms,
						  visSphereJointIndex,
						  pVisAabb,
						  dynamicPaddingRadius,
						  excludeJointIndex,
						  clothBoundingBoxMult,
						  pJoints);
	}
	else
	{
		ANIM_ASSERT(pVisSphere);
		ComputeBounds(pOutputBoundingData,
					  pSegmentContext,
					  pObjXform,
					  numJoints,
					  pJointTransforms,
					  visSphereJointIndex,
					  pVisSphere,
					  excludeJointIndex,
					  clothBoundingBoxMult,
					  pJoints);
	}

	return OrbisAnim::kSuccess;
}
