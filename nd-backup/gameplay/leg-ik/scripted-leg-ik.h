/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#ifndef SCRIPTED_LEG_IK_H
#define SCRIPTED_LEG_IK_H

#include "gamelib/gameplay/leg-ik/leg-ik.h"

class Character;
class CharacterLegIkController;

class ScriptedLegIk : public ILegIk
{
public:
	virtual void Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision) override;
	virtual const char* GetName() const override { return "ScriptedLegIk"; };
};

#endif
