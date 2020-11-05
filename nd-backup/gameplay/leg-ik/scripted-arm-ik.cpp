/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/gameplay/leg-ik/scripted-arm-ik.h"

#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/leg-ik/character-leg-ik-controller.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/ik/ik-defs.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void ScriptedArmIk::Start(Character* pCharacter)
{
	IArmIk::Start(pCharacter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ScriptedArmIk::Update(Character* pCharacter, CharacterLegIkController* pController)
{
	PROFILE(Processes, MoveLegIk_Update);

	const Locator& loc = pCharacter->GetLocator();

	const AnimStateLayer* pBaseLayer = pCharacter->GetAnimControl()->GetBaseStateLayer();

	Locator handLoc[2];
	bool handEvaluated[2];
	handLoc[kLeftArm] = loc.TransformLocator(pBaseLayer->EvaluateAP(SID("lWrist"), &handEvaluated[kLeftArm], nullptr, nullptr, kEvaluateAP_IgnoreInvalid));
	handLoc[kRightArm] = loc.TransformLocator(pBaseLayer->EvaluateAP(SID("rWrist"), &handEvaluated[kRightArm], nullptr, nullptr, kEvaluateAP_IgnoreInvalid));

	for (int i = 0; i < 2; i++)
	{
		if (handEvaluated[i])
			handLoc[i].SetTranslation(handLoc[i].GetTranslation() - Vector(0, pController->GetRootDelta(), 0));
	}
	IArmIk::DoArmIk(pCharacter, pController, handLoc, handEvaluated);
}
