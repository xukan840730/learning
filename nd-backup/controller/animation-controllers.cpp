/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/animation-controllers.h"

#include "ndlib/profiling/profiling.h"
#include "ndlib/util/bitarray128.h"

#include "gamelib/gameplay/ai/controller/entry-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AiScriptController* AnimationControllers::GetScriptController(U32F typeIndex)
{
	AI_ASSERT(typeIndex >= kBeginNpcScriptControllers && typeIndex < kEndNpcScriptControllers);

	return (AiScriptController*) m_controllerList[typeIndex];

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiScriptController* AnimationControllers::GetScriptControllerByLayerName(StringId64 layerName)
{
	for (U32F ii = kBeginNpcScriptControllers; ii < kEndNpcScriptControllers; ii++)
	{
		if (m_controllerList[ii] != nullptr)
		{
			AiScriptController* pScriptController = (AiScriptController*)m_controllerList[ii];
			if (pScriptController->GetLayerId() == layerName)
				return pScriptController;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AiScriptController* AnimationControllers::GetScriptController(U32F typeIndex) const
{
	AI_ASSERT(typeIndex >= kBeginNpcScriptControllers && typeIndex < kEndNpcScriptControllers);

	return (AiScriptController*)m_controllerList[typeIndex];
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackController* AnimationControllers::GetControllerForActionPackType(ActionPack::Type apType)
{
	ActionPackController* pApCtrl = nullptr;
	switch (apType)
	{
	case ActionPack::kCoverActionPack:
		pApCtrl = GetCoverController();
		break;
	case ActionPack::kCinematicActionPack:
		pApCtrl = GetCinematicController();
		break;
	case ActionPack::kTraversalActionPack:
		pApCtrl = GetTraversalController();
		break;
	case ActionPack::kTurretActionPack:
		pApCtrl = GetTurretController();
		break;
	case ActionPack::kPerchActionPack:
		pApCtrl = GetPerchController();
		break;
	case ActionPack::kVehicleActionPack:
		if (IAiVehicleController* pVehicleController = GetVehicleController())
		{
			pApCtrl = pVehicleController->GetVehicleActionPackController();
		}
		break;
	case ActionPack::kLeapActionPack:
		pApCtrl = GetLeapController();
		break;
	case ActionPack::kHorseActionPack:
		if (IAiRideHorseController* pRideHorseController = GetRideHorseController())
		{
			pApCtrl = pRideHorseController->GetHorseActionPackController();
		}
		break;
	case ActionPack::kEntryActionPack:
		pApCtrl = GetEntryController();
		break;
	case ActionPack::kSearchActionPack:
		pApCtrl = GetSearchController();
		break;
	default:
		break;
	}
	return pApCtrl;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ActionPackController* AnimationControllers::GetControllerForActionPackType(ActionPack::Type apType) const
{
	return const_cast<AnimationControllers*>(this)->GetControllerForActionPackType(apType);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimationControllers::ShouldInterruptNavigation() const
{
	bool isAnyBusy = false;

	for (U32F i = 0; i < m_numControllers; ++i)
	{
		bool isBusy = false;
		if (const AnimActionController* pControl = m_controllerList[i])
		{
			isBusy = pControl->ShouldInterruptNavigation();
			isAnyBusy |= isBusy;
		}
	}

	return isAnyBusy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimationControllers::GetShouldInterruptNavigationForEach(BitArray128& bitArrayOut) const
{
	ASSERT(m_numControllers <= 128);

	bitArrayOut.Clear();
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		bool isBusy = false;
		if (const AnimActionController* pControl = m_controllerList[i])
		{
			if (pControl->ShouldInterruptNavigation())
			{
				bitArrayOut.SetBit(i);
			}
		}
	}

	return m_numControllers;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimationControllers::ShouldInterruptSkills() const
{
	bool isAnyBusy = false;

	for (U32F i = 0; i < m_numControllers; ++i)
	{
		bool isBusy = false;
		if (const AnimActionController* pControl = m_controllerList[i])
		{
			isBusy = pControl->ShouldInterruptSkills();
			isAnyBusy |= isBusy;
		}
	}

	return isAnyBusy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimationControllers::GetShouldInterruptSkillsForEach(BitArray128& bitArrayOut) const
{
	ASSERT(m_numControllers <= 128);

	bitArrayOut.Clear();
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		bool isBusy = false;
		if (const AnimActionController* pControl = m_controllerList[i])
		{
			if(pControl->ShouldInterruptSkills())
			{
				bitArrayOut.SetBit(i);
			}
		}
	}

	return m_numControllers;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AnimationControllers::GetControllerName(U32F typeIndex) const
{
	const char* name = "<unknown controller>";

	switch (typeIndex)
	{
	case kLocomotionController:			name = "LocomotionController";		break;
	case kClimbController:				name = "ClimbController";			break;
	case kCoverController:				name = "CoverController";			break;
	case kHitController:				name = "HitController";				break;
	case kPerformanceController:		name = "PerformanceController";		break;
	case kMeleeActionController:		name = "MeleeActionController";		break;
	case kIdleController:				name = "IdleController";			break;
	case kWeaponController:				name = "WeaponController";			break;
	case kCinematicController:			name = "CinematicController";		break;
	case kTurretController:				name = "TurretController";			break;
	case kPerchController:				name = "PerchController";			break;
	case kTraversalController:			name = "TraversalController";		break;
	case kInvestigateController:		name = "InvestigateController";		break;
	case kSearchController:				name = "SearchController";			break;
	case kFaceController:				name = "FaceController";			break;
	case kRideHorseController:			name = "RideHorseController";		break;
	case kHorseJumpController:			name = "HorseJumpController";		break;
	case kEvadeController:				name = "EvadeController";			break;
	case kSwimController:				name = "SwimController";			break;
	case kVehicleController:			name = "VehicleController";			break;
	case kDodgeController:				name = "DodgeController";			break;
	case kPushController:				name = "PushController";			break;
	case kInfectedController:			name = "InfectedController";		break;
	case kLeapController:				name = "LeapController";			break;
	case kEntryController:				name = "EntryController";			break;
	case kFlockController:				name = "FlockController";			break;
	case kCarriedController:			name = "CarriedController";			break;
	case kNpcScriptFullBodyController:	name = "ScriptFullBodyController";	break;
	case kNpcScriptGestureController0:	name = "ScriptGestureController0";	break;
	case kNpcScriptGestureController1:	name = "ScriptGestureController1";	break;
	case kNpcScriptGestureController2:	name = "ScriptGestureController2";	break;
	case kNpcScriptGestureController3:	name = "ScriptGestureController3";	break;
	}

	return name;
}
