/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-subsystem-anim-action.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"

/// --------------------------------------------------------------------------------------------------------------- ///
TYPE_FACTORY_REGISTER_ABSTRACT(NdSubsystemAnimAction, NdSubsystem);

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdSubsystemAnimAction::Init(const SubsystemSpawnInfo& info)
{
	Err result = ParentClass::Init(info);
	if (result.Failed())
		return result;

	if (info.m_animActionRequestId != StateChangeRequest::kInvalidId)
		BindToAnimRequest(info.m_animActionRequestId);
	else if (info.m_spawnedFromStateInfo)
		SetActionState(ActionState::kUnattached);

	NdGameObject* pOwner = GetOwnerGameObject();
	NdSubsystemMgr* pSubSysMgr = pOwner ? pOwner->GetSubsystemMgr() : nullptr;

	if (!pSubSysMgr)
		return Err::kErrAbort;

	const U32 id = info.m_subsystemControllerId;

	if (id != FadeToStateParams::kInvalidSubsystemId)
	{
		if (NdSubsystemAnimController* pController = pSubSysMgr->FindSubsystemAnimControllerById(id))
		{
			SetParent(pController);

			m_hParentSubsystemController = pController;
		}
	}

	return Err::kOK;
}

bool NdSubsystemAnimAction::ShouldBindToNewInstance(const DC::AnimState* pAnimState, const FadeToStateParams* pParams)
{
	if (ShouldKeepCurrentAction())
	{
		const DC::SubsystemStateInfo* pSubSysStateInfo = AnimStateLookupStateInfo<DC::SubsystemStateInfo>(pAnimState, SID("subsystem"));
		if (pSubSysStateInfo && pSubSysStateInfo->m_subsystems)
		{
			for (int iSub=0; iSub<pSubSysStateInfo->m_subsystems->m_count; iSub++)
			{
				if (GetType() == pSubSysStateInfo->m_subsystems->m_array[iSub])
					return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimController* NdSubsystemAnimAction::GetParentSubsystemController() const
{
	return m_hParentSubsystemController.ToSubsystem();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSubsystemAnimAction::PendingRequestFailed()
{
	if (IsPending())
	{
		// If the request failed, kill the action
		StateChangeRequest::StatusFlag status = StateChangeRequest::kStatusFlagInvalid;

		if (AnimStateLayer* pLayer = GetStateLayer())
		{
			status = pLayer->GetTransitionStatus(GetRequestId());
		}

		if (status
			& (StateChangeRequest::kStatusFlagInvalid | StateChangeRequest::kStatusFlagQueueFull
				| StateChangeRequest::kStatusFlagFailed | StateChangeRequest::kStatusFlagIgnored))
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemAnimAction::BindToAnimRequest(StateChangeRequest::ID requestId, StringId64 layerId)
{
	GetOwnerGameObject()->GetSubsystemMgr()->BindToAnimRequest(this, requestId, layerId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemAnimAction::BindToInstance(AnimStateInstance* pInst)
{
	GetOwnerGameObject()->GetSubsystemMgr()->BindToInstance(this, pInst);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemAnimAction::AutoBind(StringId64 layerId)
{
	GetOwnerGameObject()->GetSubsystemMgr()->AutoBind(this, layerId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateLayer* NdSubsystemAnimAction::GetStateLayer() const
{
	const NdGameObject* pOwner = GetOwnerGameObject();
	const AnimControl* pAnimControl = GetOwnerGameObject()->GetAnimControl();
	const AnimLayer* pLayer = pAnimControl->GetLayerById(GetLayerId());
	GAMEPLAY_ASSERT(pLayer);

	if (pLayer->GetType() == kAnimLayerTypeState)
		return static_cast<const AnimStateLayer*>(pLayer);

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateLayer* NdSubsystemAnimAction::GetStateLayer()
{
	NdGameObject* pOwner = GetOwnerGameObject();
	AnimControl* pAnimControl = pOwner->GetAnimControl();
	AnimLayer* pLayer = pAnimControl->GetLayerById(GetLayerId());
	GAMEPLAY_ASSERT(pLayer);

	if (pLayer->GetType() == kAnimLayerTypeState)
		return static_cast<AnimStateLayer*>(pLayer);

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimAction::InstanceIterator NdSubsystemAnimAction::GetInstanceStart() const
{
	return GetOwnerGameObject()->GetSubsystemMgr()->GetInstanceIterator(GetSubsystemId(), 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* NdSubsystemAnimAction::GetTopInstance()
{
	AnimStateLayer* pLayer = GetStateLayer();
	AnimStateInstance* pTopInst = pLayer ? pLayer->CurrentStateInstance() : nullptr;

	if (pTopInst == GetTopBoundInstance())
		return pTopInst;

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* NdSubsystemAnimAction::GetTopInstance() const
{
	const AnimStateLayer* pLayer = GetStateLayer();
	const AnimStateInstance* pTopInst = pLayer ? pLayer->CurrentStateInstance() : nullptr;

	if (pTopInst == GetTopBoundInstance())
		return pTopInst;

	return nullptr;
}

AnimStateInstance* NdSubsystemAnimAction::GetTopBoundInstance()
{
	if (m_topInstId != INVALID_ANIM_INSTANCE_ID)
		return GetStateLayer()->GetInstanceById(m_topInstId);

	return nullptr;
}

const AnimStateInstance* NdSubsystemAnimAction::GetTopBoundInstance() const
{
	if (m_topInstId != INVALID_ANIM_INSTANCE_ID)
		return GetStateLayer()->GetInstanceById(m_topInstId);

	return nullptr;
}


struct CheckTopInstanceData
{
	AnimStateInstance* m_pNewInst = nullptr;
	AnimStateInstance::ID m_currTopInst = INVALID_ANIM_INSTANCE_ID;
	bool m_newInstIsTop = true;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool WalkCheckTopInstance(const AnimStateInstance* pInstance,
								 const AnimStateLayer* pStateLayer,
								 uintptr_t userData)
{
	CheckTopInstanceData* pData = reinterpret_cast<CheckTopInstanceData*>(userData);
	if (pInstance == pData->m_pNewInst)
	{
		pData->m_newInstIsTop = true;
		return false;
	}

	if (pInstance->GetId() == pData->m_currTopInst)
	{
		pData->m_newInstIsTop = false;
		return false;
	}

	return true;
}


void NdSubsystemAnimAction::UpdateTopInstance(AnimStateInstance* pInst)
{
	CheckTopInstanceData topInstanceData;
	topInstanceData.m_pNewInst = pInst;
	topInstanceData.m_currTopInst = m_topInstId;
	topInstanceData.m_newInstIsTop = true;

	pInst->GetLayer()->WalkInstancesNewToOld(WalkCheckTopInstance, reinterpret_cast<uintptr_t>(&topInstanceData));
	if (topInstanceData.m_newInstIsTop)
		m_topInstId = pInst->GetId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Color NdSubsystemAnimAction::GetQuickDebugColor() const
{
	switch (m_actionState)
	{
		default:
		case ActionState::kInvalid:		return kColorRed;
		case ActionState::kAutoAttach:	return kColorCyan;
		case ActionState::kUnattached:	return kColorWhite;
		case ActionState::kPending:		return kColorBlue;
		case ActionState::kTop:			return kColorGreen;
		case ActionState::kExiting:		return kColorYellow;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSubsystemAnimAction::GetQuickDebugText(IStringBuilder* pText) const
{
	pText->append_format("Blend %f", GetBlend());
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSubsystemAnimAction::GetAnimControlDebugText(const AnimStateInstance* pInstance, IStringBuilder* pText) const
{
	//snprintf(pBuf, len, "%s (Blend %.3f)", GetName(), GetBlend());
	//return true;
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class NdSubsystemAnimActionBlendCalc : public AnimStateLayer::InstanceBlender<float>
{
public:
	enum class FadeType
	{
		kMaster,
		kMotion,
		kAnim,
	};

	NdSubsystemAnimActionBlendCalc(const NdSubsystemAnimAction* pController,
								   NdSubsystemAnimAction::InstanceBlendFunc* blendFunc = nullptr,
								   void* pParam = nullptr)
		: m_pController(pController), m_blendFunc(blendFunc), m_pParam(pParam)
	{
		const NdGameObject* pOwner = pController->GetOwnerGameObject();
		m_pMgr = pOwner ? pOwner->GetSubsystemMgr() : nullptr;
	}

	void SetFadeType(FadeType fadeType) {m_fadeType = fadeType;}

protected:
	virtual float GetDefaultData() const override { return 0.0f; }

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut) override
	{
		ANIM_ASSERT(m_pMgr);
		if (m_pMgr->IsSubsystemBoundToAnimInstance(pInstance, m_pController))
		{
			*pDataOut = m_blendFunc ? m_blendFunc(m_pController, pInstance, m_pParam) : 1.0f;
		}
		else
		{
			*pDataOut = 0.0f;
		}
		return true;
	}

	virtual float BlendData(const float& leftData, const float& rightData, float masterFade, float animFade, float motionFade) override
	{
		float fade = masterFade;
		if (m_fadeType == FadeType::kAnim)
			fade = animFade;
		else if (m_fadeType == FadeType::kMotion)
			fade = motionFade;

		return Lerp(leftData, rightData, fade);
	}

	const NdSubsystemAnimAction* m_pController;
	const NdSubsystemMgr* m_pMgr;
	FadeType m_fadeType =FadeType::kMaster;
	NdSubsystemAnimAction::InstanceBlendFunc* m_blendFunc;
	void* m_pParam;

};

/// --------------------------------------------------------------------------------------------------------------- ///
float NdSubsystemAnimAction::GetBlend(InstanceBlendFunc* blendFunc, void* pParam) const
{
	const AnimStateLayer* pLayer = GetStateLayer();
	GAMEPLAY_ASSERT(pLayer);

	NdSubsystemAnimActionBlendCalc blend(this, blendFunc, pParam);
	return blend.BlendForward(pLayer, 0.0f);
}

float NdSubsystemAnimAction::GetAnimBlend(InstanceBlendFunc* blendFunc, void* pParam) const
{
	const AnimStateLayer* pLayer = GetStateLayer();
	GAMEPLAY_ASSERT(pLayer);

	NdSubsystemAnimActionBlendCalc blend(this, blendFunc, pParam);
	blend.SetFadeType(NdSubsystemAnimActionBlendCalc::FadeType::kAnim);
	return blend.BlendForward(pLayer, 0.0f);
}

float NdSubsystemAnimAction::GetMotionBlend(InstanceBlendFunc* blendFunc, void* pParam) const
{
	const AnimStateLayer* pLayer = GetStateLayer();
	GAMEPLAY_ASSERT(pLayer);

	NdSubsystemAnimActionBlendCalc blend(this, blendFunc, pParam);
	blend.SetFadeType(NdSubsystemAnimActionBlendCalc::FadeType::kMotion);
	return blend.BlendForward(pLayer, 0.0f);
}

// ----------------------------------------------------------------------------
// NdSubsystemAnimController
// ----------------------------------------------------------------------------
TYPE_FACTORY_REGISTER_ABSTRACT(NdSubsystemAnimController, NdSubsystemAnimAction);

Err NdSubsystemAnimController::Init(const SubsystemSpawnInfo& info)
{
	Err result = ParentClass::Init(info);
	if (result.Failed())
		return result;


	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSubsystemAnimController::ShouldBindToNewInstance(const DC::AnimState* pAnimState,
														const FadeToStateParams* pParams)
{
	const DC::SubsystemStateInfo* pSubSysStateInfo = AnimStateLookupStateInfo<DC::SubsystemStateInfo>(pAnimState,
																									  SID("subsystem"));

	bool shouldAttach = false;

	if (pSubSysStateInfo && pSubSysStateInfo->m_subsystemController != INVALID_STRING_ID_64)
	{
		if (GetSubsystemId() == pParams->m_subsystemControllerId)
		{
			shouldAttach = true;
		}
	}

	return shouldAttach;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSubsystemAnimController::IsActiveController() const
{
	const NdGameObject* pOwner = GetOwnerGameObject();
	if (!pOwner) // process killed
		return false;

	const bool isActiveController = (this == pOwner->GetActiveSubsystemController());
	GAMEPLAY_ASSERT(!isActiveController || !IsExiting());	// If we're not the top (or pending) instance, we shouldn't be the active controller

	return isActiveController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame NdSubsystemAnimController::GetSplasherOriention() const
{
	return GetOwnerGameObject()->GetBoundFrame();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimAction::InstanceIterator::InstanceIterator()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimAction::InstanceIterator::InstanceIterator(NdGameObject* pOwner,
														  U32 subsystemId,
														  int bindingIndex,
														  AnimStateInstance::ID instId)
	: m_pOwner(pOwner), m_subsystemId(subsystemId), m_subsystemMgrBindingIndex(bindingIndex), m_instId(instId)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* NdSubsystemAnimAction::InstanceIterator::GetInstance()
{
	return m_pOwner ? m_pOwner->GetSubsystemMgr()->GetInstance(*this) : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimAction::InstanceIterator NdSubsystemAnimAction::InstanceIterator::operator ++()
{
	InstanceIterator curr = *this;
	Advance();
	return curr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimAction::InstanceIterator NdSubsystemAnimAction::InstanceIterator::operator ++(int)
{
	Advance();
	return *this;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemAnimAction::InstanceIterator::Advance()
{
	if (m_pOwner)
		*this = m_pOwner->GetSubsystemMgr()->NextIterator(*this);
}
