/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef TOOLS_PRIMITIVE_TREE_H
#define TOOLS_PRIMITIVE_TREE_H

#include "common/libmath/commonmath.h"
#include "common/libmath/aabb.h"
#include "common/libmath/obb.h"
//#include "common/libmath/sphere.h"

#define NDI_NEW new
#define NDI_DELETE delete

// Class to abstract away the real type of object being stored in the tree. An "object" is what
// is returned by ray casts and queries against the tree, while a "primitive" is the base geometric
// instance that intersection tests are performed against. An object will generally have many
// primitives, each with its own bounding geometry. For example, a tree could hold meshes (the
// object) which are made up of triangles (the primitive); as long as both the object and its
// primitives can return appropriate bounding geometry and the primitives can have rays intersected
// with them, trees can be created from them!
class PrimitiveProxy
{
public:
	virtual ~PrimitiveProxy() { }

	// Return the number of objects.
	virtual int GetNumObjects() const = 0;

	// Returns whether or not the indexed object is valid. If it is invalid,
	// none of the queries below should be executed on it!
	virtual bool ObjectValid(int objIndex) const = 0;

	// Return the number of sub-primitives for the indexed primitive.
	virtual int GetNumPrimitives(int objIndex) const = 0;

	// Get the debug names of objects and of the proxy itself.
	virtual const char* GetName() const = 0;
	virtual const char* GetPrimitiveName(int objIndex, int primIndex) const = 0;

	// Get the indexed primitive's bounding geometries
	virtual Aabb GetObjectAabb(int objIndex) const = 0;
	virtual Obb GetObjectObb(int objIndex) const = 0;

	// Get the indexed primitive's sub-primitive bounding geometries.
	virtual Aabb GetPrimitiveAabb(int objIndex, int primIndex) const = 0;
	virtual Obb GetPrimitiveObb(int objIndex, int primIndex) const = 0;
};


// The base class of all tree types that can contain "primitives," which are geometric objects
// that can be bounded by AABBs and other geometry. The PrimitiveProxy class is used as the bridge
// between our "primitives" and the actual object that is being stored in the tree (triangles,
// meshes, points... really anything that can be bounded by a specific type of geometry!); the
// trees themselves neither know nor should they actually care what the specific type of objects
// they contain really are!
class PrimitiveTree
{
public:
	PrimitiveTree(PrimitiveProxy* pProxy) : m_pProxy(pProxy) { }
	virtual ~PrimitiveTree() { }

	// Const access to the proxy object.
	const PrimitiveProxy* GetProxy() const { return m_pProxy; }

	// Return a StringId with the type of tree.
	virtual StringId64 GetTreeType() const = 0;

protected:
	PrimitiveProxy* m_pProxy;
};

// The base class of all PrimitiveTree builders.
class PrimitiveTreeBuilder
{
public:
	PrimitiveTreeBuilder(PrimitiveProxy* pProxy) : m_pProxy(pProxy) { }
	virtual ~PrimitiveTreeBuilder() { }

	// Const access to the proxy object.
	const PrimitiveProxy* GetProxy() const { return m_pProxy; }

	// Return a StringId with the type of tree.
	virtual StringId64 GetTreeType() const = 0;

	// Create the tree, using the parameters specified during tree creation.
	virtual PrimitiveTree* Build() = 0;

protected:
	PrimitiveProxy* m_pProxy;
};


#endif