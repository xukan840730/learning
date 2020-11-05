/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/simple-physics-object.h"

#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/rigid-body.h"

class ProcessSpawnInfo;

PROCESS_REGISTER(SimplePhysicsObject, NdRigidBodyObject);

Err SimplePhysicsObject::Init(const ProcessSpawnInfo& spawn)
{
	Err result = NdRigidBodyObject::Init(spawn);
	if (result.Failed())
	{
		return result;
	}

	m_rigidBody.SetVelocity(VECTOR_LC(0.0f, -0.1f, 0.0f), VECTOR_LC(0.0f, 0.0f, 0.0f));
	m_rigidBody.SetMotionType(kRigidBodyMotionTypePhysicsDriven);
	m_rigidBody.SetLayer(Collide::kLayerSmall);

	// @ASYNC
	SetAllowThreadedUpdate(true);

	return result;
}
