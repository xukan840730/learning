/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/gesture-target.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-mgr.h"

#include "gamelib/gameplay/ai/agent/simple-nav-character.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const float Gesture::kDefaultGestureSpringConstant = 75.0f;
const float Gesture::kDefaultAimGestureSpringConstant = 90.0f;
const float Gesture::kDefaultDampingRatio = 1.0f;

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Point> Gesture::TargetObject::GetWs(const Locator& originWs) const
{
	Maybe<Point> posWs = MAYBE::kNothing;

	if (const NdLocatableSnapshot* pTargetLocatable = m_hTarget.ToSnapshot<NdLocatableSnapshot>())
	{
		posWs = pTargetLocatable->GetTranslation();
	}
	else if (const NdLocatableObject* pTargetLocatableObject = m_hTarget.ToProcess())
	{
		GAMEPLAY_ASSERT(!pTargetLocatableObject->IsProcessUpdating());

		posWs = pTargetLocatableObject->GetTranslation();
	}

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Point> Gesture::TargetObjectJoint::GetWs(const Locator& originWs) const
{
	Maybe<Point> posWs = MAYBE::kNothing;

	const NdGameObject* pTarget = m_hTarget.ToProcess();
	const FgAnimData* pAnimData = pTarget ? pTarget->GetAnimData() : nullptr;

	if (pAnimData)
	{
		const JointCache& jointCache = pAnimData->m_jointCache;

		const Locator& jointLocWs = jointCache.GetJointLocatorWs(m_jointIndex);

		posWs = jointLocWs.Pos();
	}

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Point> Gesture::TargetScriptLambda::GetWs(const Locator& originWs) const
{
	// Evaluate the script lambda to return a point.
	Maybe<Point> posWs = MAYBE::kNothing;

	const NdGameObject* pOwner = m_hOwner.ToProcess();
	const AnimControl* pAnimControl = pOwner ? pOwner->GetAnimControl() : nullptr;
	const DC::AnimInfoCollection* pInfoCollection = pAnimControl ? pAnimControl->GetInfoCollection() : nullptr;

	if (pInfoCollection)
	{
		ScriptValue argv[] = { ScriptValue(pInfoCollection) };

		const ScriptValue pointResult = ScriptManager::Eval(m_pTargetFn, ARRAY_COUNT(argv), argv);

		const Point* pPointResult = static_cast<const Point*>(pointResult.m_pointer);

		if (pPointResult)
		{
			posWs = *pPointResult;
		}
	}

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Point> Gesture::TargetAimAt::GetWs(const Locator& originWs) const
{
	Maybe<Point> posWs = MAYBE::kNothing;

	const Character* pChr = Character::FromProcess(m_hOwner.ToProcess());
	if (pChr && !pChr->WantNaturalAimAt())
	{
		posWs = pChr->GetAimAtPositionWs();
	}

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Point> Gesture::TargetLookAt::GetWs(const Locator& originWs) const
{
	Maybe<Point> posWs = MAYBE::kNothing;

	if (const NdGameObject* pObject = m_hOwner.ToProcess())
	{
		if (const Character* pChar = Character::FromProcess(pObject))
		{
			if (!pChar->WantNaturalLookAt())
			{
				posWs = pChar->GetLookAtPositionWs();
			}
		}
		else if (const SimpleNavCharacter* pSNpc = SimpleNavCharacter::FromProcess(pObject))
		{
			if (!pSNpc->WantNaturalLookAt())
			{
				posWs = pSNpc->GetLookAtPositionPs();
			}
		}
	}

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Point> Gesture::TargetLocator::GetWs(const Locator& originWs) const
{
	Maybe<Point> posWs = MAYBE::kNothing;

	if (const NdGameObject* pGo = m_hOwner.ToProcess())
	{
		const Locator locOs = pGo->GetChannelLocatorLs(m_locNameId);
		const Locator alignWs = pGo->GetLocator();
		const Locator locWs = alignWs.TransformLocator(locOs);

		posWs = locWs.Pos() + (10.0f * GetLocalZ(locWs));
	}

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Point> Gesture::TargetAnimEulerAngles::GetWs(const Locator& originWs) const
{
	Maybe<Point> posWs = MAYBE::kNothing;

	const NdGameObject* pGo = m_hOwner.ToProcess();
	const AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	if (pBaseLayer)
	{
		const float xValDeg = pBaseLayer->EvaluateFloat(m_xAngleId);
		const float yValDeg = pBaseLayer->EvaluateFloat(m_yAngleId);

		if (FALSE_IN_FINAL_BUILD(DebugSelection::Get().IsProcessOrNoneSelected(pGo)
								 && g_animOptions.m_gestures.m_debugDrawGestureTargets))
		{
			MsgCon("[%s] look-animated:\n", pGo->GetName());
			MsgCon("  x: %f deg\n", xValDeg);
			MsgCon("  y: %f deg\n", yValDeg);
		}

		const Locator alignWs = pGo->GetLocator();

		const Quat rotLs = Quat(DEGREES_TO_RADIANS(xValDeg),
								DEGREES_TO_RADIANS(yValDeg),
								0.0f,
								Quat::RotationOrder::kZXY);

		const Vector dirWs = Rotate(rotLs, GetLocalZ(originWs));

		posWs = originWs.Pos() + (10.0f * dirWs);
	}

	return posWs;
}
