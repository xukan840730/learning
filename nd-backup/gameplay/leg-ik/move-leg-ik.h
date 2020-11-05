/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#ifndef MOVE_LEG_IK_H
#define MOVE_LEG_IK_H

#include "gamelib/gameplay/leg-ik/leg-ik.h"
#include "ndlib/anim/ik/ik-defs.h"

class Character;
class CharacterLegIkController;
class Plane;

class MoveLegIk : public ILegIk
{
protected:
	bool m_legOnGround[kQuadLegCount];

public:
	virtual void Start(Character* pCharacter) override;

	Point ApplyLegToGround(Character* pCharacter, CharacterLegIkController* pController, const Plane& animationGround, int legIndex, Point legPoint, float baseY, bool& onGround, bool freezeIk = false);
	Vector LimitNormalAngle (Vector_arg normal, Vector_arg animationNormal);

	Vector m_footNormal[kQuadLegCount];

	void AdjustFootNormal(Character* pCharacter, CharacterLegIkController* pController, Vector normal, int legIndex, float blend, Vector_arg animationGroundNormal);
	virtual void Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision) override;

	virtual const char* GetName() const override { return "MoveLegIk"; };
};

#endif
