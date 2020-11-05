/*
* Copyright (c) 2003-2007 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#ifndef GAMELIB_SIMPLE_PHYSICS_OBJECT_H
#define GAMELIB_SIMPLE_PHYSICS_OBJECT_H

#include "gamelib/gameplay/nd-rigid-body-object.h"

class ProcessSpawnInfo;

class SimplePhysicsObject : public NdRigidBodyObject
{
public:
	virtual Err Init(const ProcessSpawnInfo& spawn) override;
};

#endif // NDLIB_SIMPLE_PHYSICS_OBJECT_H

