/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "physics/havokext/collision-filter-base.h"

class NdGameObject;
class RigidBody;
class CompositeBody;
struct CompositeBodyInitInfo;

namespace DC
{
	struct CharacterCollision;
	struct CharacterCollisionSettings;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterCollision
{
public:
	static void PostInit(NdGameObject& self, const DC::CharacterCollision* pCharColl);
	static void SetLayer(CompositeBody* pCompo, Collide::Layer layer);
	static StringId64 GetBodyAttachPoint(const RigidBody* pBody);
	static const DC::CharacterCollisionSettings* GetBodyCollisionSettings(const RigidBody* pBody);

	static bool IsClippingCameraPlane(const CompositeBody* pCompo, 
									  bool useCollCastForBackpack,
									  F32 surfaceExpand, 
									  F32 surfaceExpandBackpack,
									  Point_arg planeCenterWs, 
									  Vector_arg planeNormalWs, 
									  Quat_arg cameraRot,
									  Vec2_arg cameraWidthHeight,
									  F32 clipRadius, 
									  F32* pNearestApproach,
									  bool debugDraw = false);
};