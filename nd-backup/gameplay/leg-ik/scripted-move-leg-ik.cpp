/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#include "gamelib/gameplay/leg-ik/scripted-move-leg-ik.h"

class Character;
class CharacterLegIkController;

//---------------------------------------------------------------------------------------
// Scripted Move Leg IK Implementation
//---------------------------------------------------------------------------------------

void ScriptedMoveLegIk::Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision)
{
	PROFILE(Processes, MoveLegIk_Update);

	MoveLegIk::Update(pCharacter, pController, doCollision);	
}
