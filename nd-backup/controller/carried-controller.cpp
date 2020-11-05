/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "game/ai/controller/carried-controller.h"

#include "gamelib/anim/motion-matching/pose-tracker.h"
#include "gamelib/gameplay/character-locomotion.h"
#include "gamelib/gameplay/flocking/flocking-agent.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"

#include "game/scriptx/h/anim-npc-info.h"
#include "game/ai/agent/npc.h"

 //--------------------------------------------------------------------------------------
 // Utilities for syncing phase with carrier
 //--------------------------------------------------------------------------------------
struct UpdateCarriedInfoParams
{
	const AnimStateLayer* m_pCarrierBaseLayer;
};


// for use with AnimStateInstance::WalkInstancesNewToOld()
static bool UpdateCarriedInstanceInfoFunc(AnimStateInstance* pInstance, AnimStateLayer* pStateLayer, uintptr_t userData)
{
	UpdateCarriedInfoParams* pParams = (UpdateCarriedInfoParams*)(userData);
	const AnimStateLayer* pCarrierBaseLayer = pParams->m_pCarrierBaseLayer;
	if (pInstance->GetStateName() == SID("npc-carried-mm"))
	{
		DC::CharacterRideHorseInstanceInfo& horseInfo = static_cast<DC::AnimCharacterInstanceInfo*>(pInstance->GetAnimInstanceInfo())->m_horse;

		AnimInstance::ID instanceId(horseInfo.m_horseAnimInstanceId);
		if (instanceId == INVALID_ANIM_INSTANCE_ID)
			return true;

		const AnimStateInstance *pCarrierInst = pCarrierBaseLayer->FindInstanceById(instanceId);
		if (pCarrierInst) //may not be valid because horse could have faded out anim already
		{
			horseInfo.m_horsePhase = pCarrierInst->GetPhase();
		}
	}
	return true; //always keep searching
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCarriedController::RequestAnimations()
{
	if (!m_hCarrier.Assigned())
		return;

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurInst = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
	const StringId64 curStateId = pCurInst ? pCurInst->GetStateName() : INVALID_STRING_ID_64;

	NavCharacter *pCarrier = m_hCarrier.ToMutableProcess();
	if (!pCarrier)
		return;

	const AnimStateLayer* pCarrierBaseLayer = pCarrier->GetAnimControl()->GetBaseStateLayer();

	AnimInstance::ID carrierAnimId = pCarrierBaseLayer->CurrentStateInstance()->GetId();

	const StringId64 carriedAnimState = SID("npc-carried-mm");

	UpdateTopInfo();

	if (carrierAnimId != m_carrierAnimIdLastFrame)
	{
		DC::AnimCharacterInstanceInfo* pInstanceInfo = static_cast<DC::AnimCharacterInstanceInfo*>(pAnimControl->GetInstanceInfo());

		float carrierPhase = 0.0f;
		const AnimStateInstance* pInst = pCarrierBaseLayer->CurrentStateInstance();
		if (pInst)
			carrierPhase = pInst->Phase();

		pInstanceInfo->m_horse.m_horsePhase = carrierPhase;
		pInstanceInfo->m_horse.m_horseAnimInstanceId = carrierAnimId.GetValue();

		FadeToStateParams params;
		params.m_animFadeTime = 0.4f;
		params.m_motionFadeTime = 0.4f;
		params.m_stateStartPhase = carrierPhase;
		params.m_apRef = pInst->GetApLocator();
		params.m_apRefValid = true;
		pBaseLayer->FadeToState(carriedAnimState, params);
	}

	m_carrierAnimIdLastFrame = carrierAnimId;

	UpdateCarriedInfoParams params;
	params.m_pCarrierBaseLayer = pCarrierBaseLayer;
	pAnimControl->GetBaseStateLayer()->WalkInstancesNewToOld(&UpdateCarriedInstanceInfoFunc, (uintptr_t)(&params));

	pNavChar->SetBoundFrame(pCarrier->GetBoundFrame());
}

void AiCarriedController::BeginCarried(NavCharacter *pCarrier)
{
	//Npc* pNpc = Npc::FromProcess(pCarrier);
	//if (pNpc)
	//	pNpc->SetShouldBeinDefaultBucket(true);

	m_hCarrier = pCarrier;
	m_carrierAnimIdLastFrame = INVALID_ANIM_INSTANCE_ID;

	NavCharacter* pNavChar = GetCharacter();
	pNavChar->EnableNavBlocker(false);

	Npc* pThisCharacter = Npc::FromProcess(pNavChar);
	if (pThisCharacter)
	{
		pThisCharacter->SetShouldBeInAttachBucket(true);
	}
}

void AiCarriedController::EndCarried()
{
	//Npc* pNpc = Npc::FromProcess(m_hCarrier.ToMutableProcess());
	//if (pNpc)
	//	pNpc->SetShouldBeinDefaultBucket(false);

	m_hCarrier = nullptr;

	NavCharacter* pNavChar = GetCharacter();
	pNavChar->EnableNavBlocker(true);

	Npc* pThisCharacter = Npc::FromProcess(pNavChar);
	if (pThisCharacter)
	{
		pThisCharacter->SetShouldBeInAttachBucket(false);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCarriedController::UpdateTopInfo()
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	if (!m_hCarrier.Assigned())
		return;

	NavCharacter *pCarrier = m_hCarrier.ToMutableProcess();
	const SsAnimateController* pCarrierAnimCtrl = pCarrier->GetPrimarySsAnimateController();
	
	if (pCarrierAnimCtrl && pCarrierAnimCtrl->IsPlayingCinematic())
	{
		return;
	}

	const StringId64 carrierAnimState = pCarrier->GetCurrentAnimState();
	const StringId64 carrierAnim = pCarrier->GetCurrentAnim();
	const StringId64 animId = StringId64Concat(carrierAnim, "-stalker");

	const AnimStateLayer* pCarrierBaseLayer = pCarrier->GetAnimControl()->GetBaseStateLayer();
	ASSERT(pCarrierBaseLayer);
	
	const bool flipped = pCarrierBaseLayer->IsFlipped();

	DC::AnimNpcTopInfo* pTopInfo = pAnimControl->TopInfo<DC::AnimNpcTopInfo>();
	pTopInfo->m_carried.m_animMm = animId;
	pTopInfo->m_carried.m_flip = flipped;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCarriedController::UpdateApRef()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCarriedController::IsBusy() const
{
	return m_hCarrier.Assigned();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCarriedController::ShouldInterruptSkills() const
{
	return IsBusy();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCarriedController::ShouldInterruptNavigation() const
{
	return IsBusy();
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiCarriedController* CreateCarriedController()
{
	return NDI_NEW AiCarriedController;
}
