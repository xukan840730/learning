/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/math/cylinder.h"
#include "corelib/math/sphere.h"

class NdGameObject;

/// --------------------------------------------------------------------------------------------------------------- ///
struct FindNdGameObjectsInShapeData
{
	FindNdGameObjectsInShapeData()
		: pOutputList(nullptr)
		, currOutputCount(0)
		, maxOutputCount(0)
		, typeIds(nullptr)
		, numTypeIds(0)
		, excludeTypeId(INVALID_STRING_ID_64)
		, useCylinder(false)
	{}

	Sphere searchSphere;
	Cylinder searchCylinder;
	MutableNdGameObjectHandle* pOutputList;
	I32F currOutputCount;
	I32F maxOutputCount;
	const StringId64* typeIds;
	U32F numTypeIds;
	StringId64 excludeTypeId;
	bool useCylinder;
};

/// --------------------------------------------------------------------------------------------------------------- ///
I32F FindNdGameObjects(NdGameObject** pOutputList, I32F maxOutputCount);
I32F FindNdGameObjectsOfType(NdGameObject** pOutputList, StringId64 typeId, StringId64 excludeTypeId, I32F maxOutputCount);
I32F FindNdGameObjectsOfTypeBucket(NdGameObject** pOutputList, StringId64 typeId, StringId64 excludeTypeId, I32F maxOutputCount, I32 bucket);

I32F FindNdGameObjectsInSphere(Sphere_arg searchSphere, MutableNdGameObjectHandle* pOutputList, I32F maxOutputCount);
I32F FindNdGameObjectsInSphereOfType(Sphere_arg searchSphere,
									 StringId64 typeId,
									 StringId64 excludeTypeId,
									 MutableNdGameObjectHandle* pOutputList,
									 I32F maxOutputCount);

I32F FindNdGameObjectsInSphereOfTypeJobfied(Sphere_arg searchSphere,
	StringId64 typeId,
	StringId64 excludeTypeId,
	ExternalBitArray& outputList,
	I32F maxOutputCount);

U32F FindNdGameObjectsInSphereOfTypesJobfied(Sphere_arg searchSphere, const StringId64 typeIds[], U32F numTypes, ExternalBitArray& outputList, U32F maxOutputCount);

I32F FindNdGameObjectsInSphereOfTypeBucket(Sphere_arg searchSphere,
										   StringId64 typeId,
										   StringId64 excludeTypeId,
										   MutableNdGameObjectHandle* pOutputList,
										   I32F maxOutputCount,
										   I32 bucket);
U32F FindNdGameObjectsInSphereOfTypes(Sphere_arg searchSphere,
									  const StringId64 typeIds[],
									  U32F numTypes,
									  MutableNdGameObjectHandle* pOutputList,
									  U32F maxOutputCount);


U32F FindNdGameObjectsInSphereOfTypesExcludeBucket(Sphere_arg searchSphere,
												   const StringId64 typeIds[],
												   U32F numTypes,
												   MutableNdGameObjectHandle* pOutputList,
												   U32F maxOutputCount,
												   I32 bucket);

U32F FindInteractablesInSphere(Sphere_arg searchSphere,
									  MutableNdGameObjectHandle* pOutputList,
									  U32F maxOutputCount);
U32F FindInteractablesInSphereExcludeBucket(Sphere_arg searchSphere,
												   MutableNdGameObjectHandle* pOutputList,
												   U32F maxOutputCount,
												   I32 bucket);
void FindInteractablesInSphereJobfied(Sphere_arg searchSphere, ExternalBitArray& outputList, U32F maxOutputCount);

I32F FindNdGameObjectsInCylinder(const Cylinder& searchCylinder, MutableNdGameObjectHandle* pOutputList, I32F maxOutputCount);
I32F FindNdGameObjectsInCylinderOfType(const Cylinder& searchCylinder,
									   StringId64 typeId,
									   StringId64 excludeTypeId,
									   MutableNdGameObjectHandle* pOutputList,
									   I32F maxOutputCount);
U32F FindNdGameObjectsInCylinderOfTypes(const Cylinder& searchCylinder,
										const StringId64 typeIds[],
										U32F numTypes,
										MutableNdGameObjectHandle* pOutputList,
										U32F maxOutputCount);
