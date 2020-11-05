/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/leg-ik/scripted-leg-ik.h"

#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/leg-ik/character-leg-ik-controller.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/footik.h"
#include "ndlib/anim/ik/ik-defs.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void ScriptedLegIk::Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision)
{
	PROFILE(Processes, MoveLegIk_Update);

	pController->Reset();

	const Locator& loc = pCharacter->GetLocator();
	
	const AnimStateLayer* pBaseLayer = pCharacter->GetAnimControl()->GetBaseStateLayer();

	// do leg ik
	Locator aLegLoc[kQuadLegCount];

	CONST_EXPR StringId64 aApNames[kQuadLegCount] =
	{
		SID("apReference-foot-l"),
		SID("apReference-foot-r"),
		SID("apReference-foot-fl"),
		SID("apReference-foot-fr"),
	};

	bool allLegsSucceeded = true;
	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		bool legSucceeded;
		aLegLoc[iLeg] = loc.TransformLocator(pBaseLayer->EvaluateAP(aApNames[iLeg], &legSucceeded));
		allLegsSucceeded = allLegsSucceeded && legSucceeded;
	}

	if (allLegsSucceeded)
	{
		ILegIk::DoLegIk(pCharacter, pController, aLegLoc, m_legCount, nullptr, Vector(kZero));
	}
}
