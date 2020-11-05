/*
* Copyright (c) 2019 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "ndlib/anim/rig-nodes/utils.h"


/// --------------------------------------------------------------------------------------------------------------- ///
float SmoothStep(float value)
{
	return value * value * (3.0 - (2.0 * value));
}


/// --------------------------------------------------------------------------------------------------------------- ///
float SmoothGaussian(float value)
{
	float kSigma = 1.0;  //Must be > 0.   As drops to zero, higher value one lasts longer.
	return (1.0 - expf(-1.0 * (value * value) * 10.0 / (2.0 * kSigma * kSigma)));
}


/// --------------------------------------------------------------------------------------------------------------- ///
float LinearInterpolate(float a, float b, float weight)
{
	float x = b*weight + a*(1.0 - weight);
	return x;
}

const Vector LinearInterpolate3D(const Point& va, const Point& vb, float weight) {
	float x = LinearInterpolate(va.X(), vb.X(), weight);
	float y = LinearInterpolate(va.Y(), vb.Y(), weight);
	float z = LinearInterpolate(va.Z(), vb.Z(), weight);
	return Vector(x, y, z);
}
// 
// MEulerRotation::RotationOrder getRotationOrder(short nRotateOrder) {
// 	MEulerRotation::RotationOrder rotateOrder = MEulerRotation::kXYZ;
// 	//Get the rotation order of the current driving node
// 	if (nRotateOrder == 0)  rotateOrder = MEulerRotation::kXYZ;
// 	else if (nRotateOrder == 1)  rotateOrder = MEulerRotation::kYZX;
// 	else if (nRotateOrder == 2)  rotateOrder = MEulerRotation::kZXY;
// 	else if (nRotateOrder == 3)  rotateOrder = MEulerRotation::kXZY;
// 	else if (nRotateOrder == 4)  rotateOrder = MEulerRotation::kYXZ;
// 	else if (nRotateOrder == 5)  rotateOrder = MEulerRotation::kZYX;
// 
// 	return rotateOrder;
// }
// 
// MStatus jumpToElement(MArrayDataHandle& hArray, unsigned int nIndex)
// {
// 	MStatus status;
// 	status = hArray.jumpToElement(nIndex);
// 	if (MFAIL(status)){
// 		MArrayDataBuilder builder = hArray.builder(&status);
// 		CHECK_MSTATUS_AND_RETURN_IT(status);
// 		builder.addElement(nIndex, &status);
// 		CHECK_MSTATUS_AND_RETURN_IT(status);
// 		status = hArray.set(builder);
// 		CHECK_MSTATUS_AND_RETURN_IT(status);
// 		status = hArray.jumpToElement(nIndex);
// 		CHECK_MSTATUS_AND_RETURN_IT(status);
// 	}
// 	return status;
// }

