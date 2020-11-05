/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef BOUNDING_DATA_H
#define BOUNDING_DATA_H

#include "corelib/math/aabb.h"
#include "corelib/math/sphere.h"

struct BoundingData
{
	Aabb	m_aabb;
	Sphere	m_jointBoundingSphere;
};

#endif // #ifndef BOUNDING_DATA_H
