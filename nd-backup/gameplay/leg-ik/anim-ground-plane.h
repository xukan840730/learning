/*
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIM_GROUND_PLANE_H
#define ANIM_GROUND_PLANE_H

class Character;
class ArtItemAnim;
class AnimStateSnapshot;
class NdGameObject;

Plane GetAnimatedGroundPlane(const NdGameObject* pObject);
Plane GetAnimatedGroundPlane(const ArtItemAnim* pAnim, float phase, bool flipped);
Plane GetAnimatedGroundPlane(const Character* pCharacter);
Plane GetAnimatedGroundPlane(const AnimStateSnapshot &stateSnapshot, Locator alignWs);

#endif
