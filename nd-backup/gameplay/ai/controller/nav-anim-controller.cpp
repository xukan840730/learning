/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/ai/controller/nav-anim-controller.h"

#include "ndlib/anim/anim-control.h"

#include "gamelib/gameplay/ai/agent/nav-character-util.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/scriptx/h/ap-entry-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavAnimHandoffDesc::IsValid(const NdGameObject* pChar) const
{
	if ((m_animRequestId == StateChangeRequest::kInvalidId) && (m_animStateId == INVALID_ANIM_INSTANCE_ID)
		&& (m_subsystemControllerId == 0))
	{
		return false;
	}

	const AnimControl* pAnimControl		  = pChar ? pChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer	  = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

	if (!pCurInstance)
	{
		return false;
	}

	const AnimStateInstance::ID desiredId = GetAnimStateId(pChar);

	const bool idValid	   = desiredId != INVALID_ANIM_INSTANCE_ID;
	const bool nameValid   = m_stateNameId != INVALID_STRING_ID_64;
	const bool subsysValid = m_subsystemControllerId != 0;

	const bool idMatches		= (pCurInstance->GetId() == desiredId) && idValid;
	const bool nameMatches		= (pCurInstance->GetStateName() == m_stateNameId) && nameValid;
	const bool subsystemMatches = (pCurInstance->GetSubsystemControllerId() == m_subsystemControllerId) && subsysValid;

	bool changeValid = false;
	if (m_animRequestId != StateChangeRequest::kInvalidId)
	{
		if (const AnimStateInstance* pChangeInst = pBaseLayer->GetTransitionDestInstance(m_animRequestId))
		{
			changeValid = pChangeInst == pCurInstance;
		}
		else if (pBaseLayer->GetPendingChangeRequest(m_animRequestId) != nullptr)
		{
			changeValid = true;
		}
	}

	const bool valid = idMatches || nameMatches || subsystemMatches || changeValid;

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavAnimHandoffDesc::SetStateChangeRequestId(StateChangeRequest::ID id, StringId64 stateNameId)
{
	if (m_animRequestId == id)
		return;

	Reset();

	m_animRequestId = id;
	m_stateNameId	= stateNameId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavAnimHandoffDesc::SetAnimStateInstance(const AnimStateInstance* pInst)
{
	Reset();

	if (!pInst)
		return;

	m_animStateId = pInst->GetId();
	m_stateNameId = pInst->GetStateName();
		
	m_subsystemControllerId = pInst->GetSubsystemControllerId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavAnimHandoffDesc::SetSubsystemControllerId(U32 id, StringId64 stateNameId)
{
	Reset();

	m_stateNameId = stateNameId;
	m_subsystemControllerId = id;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavAnimHandoffDesc::ConfigureFromDc(const DC::NavAnimHandoffParams* pDcParams)
{
	if (!pDcParams)
		return;

	m_steeringPhase = pDcParams->m_steeringPhase;
	m_motionType	= DcMotionTypeToGame(pDcParams->m_motionType);

	m_mmMaxDurationSec = pDcParams->m_mmMaxDurationSec;
	m_mmMinPathLength  = pDcParams->m_mmMinPathLength;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavAnimHandoffDesc::ShouldUpdateFrom(const NdGameObject* pChar, const NavAnimHandoffDesc& rhs) const
{
	if (!IsValid(pChar))
		return false;

	if ((m_animRequestId != StateChangeRequest::kInvalidId) && (m_animRequestId == rhs.m_animRequestId))
		return true;

	if ((m_stateNameId != INVALID_STRING_ID_64) && (m_stateNameId == rhs.m_stateNameId))
		return true;

	const AnimStateInstance::ID lhsAnimId = GetAnimStateId(pChar);
	const AnimStateInstance::ID rhsAnimId = rhs.GetAnimStateId(pChar);

	if (lhsAnimId != INVALID_ANIM_INSTANCE_ID && lhsAnimId == rhsAnimId)
		return true;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance::ID NavAnimHandoffDesc::GetAnimStateId(const NdGameObject* pChar) const
{
	if (m_animStateId != INVALID_ANIM_INSTANCE_ID)
		return m_animStateId;

	const AnimControl* pAnimControl = pChar ? pChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const AnimStateInstance* pDestInstance = pBaseLayer ? pBaseLayer->GetTransitionDestInstance(m_animRequestId) : nullptr;

	if (pDestInstance)
	{
		m_animStateId = pDestInstance->GetId();
	}

	return m_animStateId;
}
