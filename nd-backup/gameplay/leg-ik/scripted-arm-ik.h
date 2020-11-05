/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/
#ifndef SCRIPTED_ARM_IK_H
#define SCRIPTED_ARM_IK_H

#include "gamelib/gameplay/leg-ik/arm-ik.h"

class Character;
class CharacterLegIkController;

class ScriptedArmIk : public IArmIk
{
protected:

public:
	virtual void Start(Character* pCharacter) override;
	virtual void Update(Character* pCharacter, CharacterLegIkController* pController) override;

	virtual const char* GetName() const override { return "ScriptedArmIk"; };
};

#endif
