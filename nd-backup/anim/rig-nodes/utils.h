/*
* Copyright (c) 2019 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

float SmoothStep(float value);
float SmoothGaussian(float value);
float LinearInterpolate(float da, float db, float dw);

inline Vector GetVectorFromAxisIndex(int axisIndex)
{
	if (axisIndex == 0)
		return Vector(kUnitXAxis);
	else if (axisIndex == 1)
		return Vector(kUnitYAxis);
	else if (axisIndex == 2)
		return Vector(kUnitZAxis);

	return Vector(kUnitXAxis);
}


inline int GetPitchAxisIndex(int nRollAxis, int nYawAxis)
{
	short nPitchAxis = 0;
	if (nRollAxis == 0 && nYawAxis == 1) //XY
		nPitchAxis = 2; //Z
	else if (nRollAxis == 0 && nYawAxis == 2) //XZ
		nPitchAxis = 1; //Y
	else if (nRollAxis == 1 && nYawAxis == 2) //YZ
		nPitchAxis = 0; //X
	else if (nRollAxis == 1 && nYawAxis == 0) //YX
		nPitchAxis = 2; //Z
	else if (nRollAxis == 2 && nYawAxis == 1) //ZY
		nPitchAxis = 0; //X
	else if (nRollAxis == 2 && nYawAxis == 0) //ZX
		nPitchAxis = 1; //Y

	return nPitchAxis;
}

const Vector LinearInterpolate3D(const Point& va, const Point& vb, float dw);
// MEulerRotation::RotationOrder	getRotationOrder(short eRotateOrder);
// MStatus							jumpToElement(MArrayDataHandle& hArray, unsigned int eIndex);
