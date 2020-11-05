/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/anim-instance-debug.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-state-layer.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/logger/logger.h"

/// --------------------------------------------------------------------------------------------------------------- ///
TYPE_FACTORY_REGISTER(NdAnimActionDebugSubsystem, NdSubsystemAnimAction);

Err NdAnimActionDebugSubsystem::Init(const SubsystemSpawnInfo& info)
{
	Err result = ParentClass::Init(info);
	if (result.Failed())
		return result;

	SetActionState(ActionState::kUnattached);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SUBSYSTEM_UPDATE_DEF(NdAnimActionDebugSubsystem, PostAnimUpdate)
{
	AnimControl* pAnimControl = GetOwnerGameObject()->GetAnimControl();
	AnimStateLayer* pLayer	  = pAnimControl ? pAnimControl->GetStateLayerById(GetLayerId()) : nullptr;
	if (pLayer)
	{
		StateChangeRequest::StatusFlag status = pLayer->GetTransitionStatus(GetRequestId());
		int i = 0;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimActionDebugSubsystem::InstancePendingChange(StringId64 layerId,
													   StateChangeRequest::ID requestId,
													   StringId64 changeId,
													   int changeType)
{
	BindToAnimRequest(requestId, layerId);

	m_changeId	 = changeId;
	m_changeType = changeType;

	LoggerScopeJanitor logJanitor;

	if (changeType == kPendingChangeFadeToState)
	{
		LoggerNewLog("[[tag anim-transition]][[yellow]]Pending change: Fade to State");
		LoggerNewLog("Layer: [[s]], Change ID: [[s]]", layerId, changeId);
	}
	else if (changeType == kPendingChangeRequestTransition)
	{
		LoggerNewLog("[[tag anim-transition]][[yellow]]Pending change: Transition");
		LoggerNewLog("Layer: [[s]], Change ID: [[s]]", layerId, changeId);
	}
	else if (changeType == kPendingChangeAutoTransition)
	{
		LoggerNewLog("[[tag anim-transition]][[yellow]]Pending change: Auto Transition");
		LoggerNewLog("Layer: [[s]], Change ID: [[s]]", layerId, changeId);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimActionDebugSubsystem::InstanceAutoTransition(StringId64 layerId)
{
	m_changeId	 = SID("auto");
	m_changeType = kPendingChangeAutoTransition;

	LoggerScopeJanitor logJanitor;
	LoggerNewLog("[[tag anim-transition]][[yellow]]Auto transition");
	LoggerNewLog("Layer: [[s]]", layerId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAnimActionDebugSubsystem::GetQuickDebugText(IStringBuilder* pText) const
{
	AnimStateInstance* pInst = GetInstanceStart();

	if (pInst)
	{
		pText->append_format("AnimState '%s': ", DevKitOnly_StringIdToString(pInst->GetStateName()));
	}

	if (m_changeType == kPendingChangeFadeToState)
	{
		pText->append_format("FadeToState: %s", DevKitOnly_StringIdToString(m_changeId));
		return true;
	}
	else if (m_changeType == kPendingChangeRequestTransition)
	{
		pText->append_format("RequestTransition: %s", DevKitOnly_StringIdToString(m_changeId));
		return true;
	}
	else if (m_changeType == kPendingChangeAutoTransition)
	{
		pText->append_format("Auto Transition");
		return true;
	}

	return false;
}
