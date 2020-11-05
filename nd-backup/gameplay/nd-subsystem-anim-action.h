/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"

#include "gamelib/anim/subsystem-ik-node.h"
#include "gamelib/camera/camera-control.h"
#include "gamelib/gameplay/nd-attack-info.h"
#include "gamelib/gameplay/nd-subsystem.h"


/// --------------------------------------------------------------------------------------------------------------- ///
class NdGameObject;
class NdSubsystemMgr;
class NdSubsystemAnimController;
class EffectAnimInfo;

typedef TypedSubsystemHandle<NdSubsystemAnimController> NdSubsystemAnimControllerHandle;

namespace DC
{
	struct AnimState;
	struct PlayerHitReactionEntry;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class NdSubsystemAnimAction : public NdSubsystem
{
	typedef NdSubsystem ParentClass;

	friend NdSubsystemMgr;

public:
	enum class ActionState
	{
		kInvalid,
		kUnattached, // Unbound to any anim
		kAutoAttach, // Unbound, attach to next pushed anim instance
		kPending,	 // Bound to pending anim transition
		kTop,		 // Bound to current top instance
		kExiting,	 // Bound to non top instance
	};

	static const char* GetActionStateStr(NdSubsystemAnimAction::ActionState s)
	{
		switch (s)
		{
		case NdSubsystemAnimAction::ActionState::kAutoAttach:
			return "AutoAttach";
		case NdSubsystemAnimAction::ActionState::kExiting:
			return "Exiting";
		case NdSubsystemAnimAction::ActionState::kInvalid:
			return "Invalid";
		case NdSubsystemAnimAction::ActionState::kPending:
			return "Pending";
		case NdSubsystemAnimAction::ActionState::kTop:
			return "Top";
		case NdSubsystemAnimAction::ActionState::kUnattached:
			return "Unattached";
		}

		return "???";
	}

	typedef float InstanceBlendFunc(const NdSubsystemAnimAction* pController,
									const AnimStateInstance* pInst,
									void* pParam);

	class InstanceIterator
	{
		friend NdSubsystemMgr;

	private:
		NdGameObject* m_pOwner = nullptr;
		U32 m_subsystemId	   = 0;
		int m_subsystemMgrBindingIndex = -1;
		AnimStateInstance::ID m_instId = INVALID_ANIM_INSTANCE_ID;

		void Advance();

	public:
		InstanceIterator();
		InstanceIterator(NdGameObject* pOwner, U32 subsystemId, int bindingIndex, AnimStateInstance::ID instId);
		AnimStateInstance* GetInstance();

		InstanceIterator operator++();
		InstanceIterator operator++(int);

		operator AnimStateInstance*() { return GetInstance(); }
		AnimStateInstance* operator->() { return GetInstance(); }
	};

	virtual Err Init(const SubsystemSpawnInfo& info) override;

	// Instance bound to this anim action is about to be created
	virtual void InstancePrepare(const DC::AnimState* pAnimState, FadeToStateParams* pParams) {}
	// Instance not bound to this anim action is about replace this action's instance as the top instance
	virtual void InstanceReplace(const DC::AnimState* pAnimState, FadeToStateParams* pParams) {} 
	// Instance bound to this anim action has just been created
	virtual void InstanceCreate(AnimStateInstance* pInst) {} 
	// Instance bound to this anim action is about to be destroyed
	virtual void InstanceDestroy(AnimStateInstance* pInst) {} 

	virtual bool InstanceAlignFunc(const AnimStateInstance* pInst,
								   const BoundFrame& prevAlign,
								   const BoundFrame& currAlign,
								   const Locator& apAlignDelta,
								   BoundFrame* pAlignOut,
								   bool debugDraw)
	{
		return false;
	}
	virtual void InstanceIkFunc(const AnimStateInstance* pInst,
								AnimPluginContext* pPluginContext,
								const AnimSnapshotNodeSubsystemIK::SubsystemIkPluginParams* pParams)
	{
	}

	virtual void HandleTriggeredEffect(const EffectAnimInfo* pEffectAnimInfo) {}

	virtual bool IsAnimAction() override { return true; }

	virtual bool ShouldBindToNewInstance(const DC::AnimState* pAnimState, const FadeToStateParams* pParams);

	// If the new state that spawning also wants this type of action, should we automatically bind this action
	// to the new instead instead of creating a new one.
	virtual bool ShouldKeepCurrentAction()	{return false;}

	virtual NdSubsystemAnimController* GetParentSubsystemController() const;

	bool PendingRequestFailed();

	void BindToAnimRequest(StateChangeRequest::ID requestId, StringId64 layerId = INVALID_STRING_ID_64);
	void BindToInstance(AnimStateInstance* pInst);
	void AutoBind(StringId64 layerId = INVALID_STRING_ID_64);
	StateChangeRequest::ID GetRequestId() const { return m_requestId; }
	StringId64 GetLayerId() const { return m_layerId; }

	const AnimStateLayer* GetStateLayer() const;
	AnimStateLayer* GetStateLayer();

	// Get Iterator to iterate over all instances bound to this Subsystem
	InstanceIterator GetInstanceStart() const;

	// Get pointer to top instance if it is bound to this instance, otherwise nullptr
	AnimStateInstance* GetTopInstance();
	const AnimStateInstance* GetTopInstance() const;

	// Get pointer to the topmost instance bound to this Subsystem
	AnimStateInstance* GetTopBoundInstance();
	const AnimStateInstance* GetTopBoundInstance() const;

	void UpdateTopInstance(AnimStateInstance* pInst);

	void SetActionState(ActionState state) { m_actionState = state; }
	ActionState GetActionState() const { return m_actionState; }

	bool IsUnattached() const { return m_actionState == ActionState::kUnattached; }
	bool IsAutoAttach() const { return m_actionState == ActionState::kAutoAttach; }
	bool IsPending() const { return m_actionState == ActionState::kPending; }
	bool IsTop() const { return m_actionState == ActionState::kTop; }
	bool IsExiting() const { return m_actionState == ActionState::kExiting; }

	virtual Color GetQuickDebugColor() const override;
	virtual bool GetQuickDebugText(IStringBuilder* pText) const override;
	virtual bool GetAnimControlDebugText(const AnimStateInstance* pInstance, IStringBuilder* pText) const override;

	float GetBlend(InstanceBlendFunc* blendFunc = nullptr, void* pParam = nullptr) const;
	float GetAnimBlend(InstanceBlendFunc* blendFunc = nullptr, void* pParam = nullptr) const;
	float GetMotionBlend(InstanceBlendFunc* blendFunc = nullptr, void* pParam = nullptr) const;

private:
	ActionState m_actionState = ActionState::kInvalid;
	StateChangeRequest::ID m_requestId = StateChangeRequest::kInvalidId;
	StringId64 m_layerId = SID("base");
	AnimStateInstance::ID m_topInstId = INVALID_ANIM_INSTANCE_ID;

	NdSubsystemAnimControllerHandle m_hParentSubsystemController;

	int m_markedForBindingIndex = -1; // Some bookkeeping to assist NdSubsystemMgr

public:
	StringId64 m_debugExitState = INVALID_STRING_ID_64;		// Temp debug
};

TYPE_FACTORY_DECLARE(NdSubsystemAnimAction);

typedef TypedSubsystemHandle<NdSubsystemAnimAction> NdSubsystemAnimActionHandle;

/// --------------------------------------------------------------------------------------------------------------- ///
class NdSubsystemAnimController : public NdSubsystemAnimAction
{
	typedef NdSubsystemAnimAction ParentClass;

private:
	bool m_persistent = false; // Don't destroy when last instance is destroyed. Instead, set to ActionState::kUnattached.

public:
	virtual Err Init(const SubsystemSpawnInfo& info) override;
	virtual bool ShouldBindToNewInstance(const DC::AnimState* pAnimState, const FadeToStateParams* pParams) override;
	bool IsActiveController() const;

	void SetPersistent(bool enable) { m_persistent = enable; }
	bool IsPersistent() const { return m_persistent; }

	virtual bool GetPlayerMoveControl(float* pMoveCtrlPct) const { return false; }
	virtual float GetNavAdjustRadius(float radius) const { return radius; }

	virtual void RequestRefreshAnimState(const FadeToStateParams* pFadeToStateParams = nullptr,
										 bool allowStompOfInitialBlend = true)
	{
	}
	virtual void NotifyWeaponAction(AnimStateLayer* pParLayer, StringId64 state, float fadeTime) {}

	virtual float GetTargetRunSpeed() const { return 0.0f; }
	virtual bool AllowFullBodyHitReaction(const NdAttackInfo* pAttackInfo) const { return true; }
	virtual void NotifyHitReaction(const DC::PlayerHitReactionEntry* pReaction) {}

	virtual BoundFrame GetSplasherOriention() const;	// Get object orientation used by splasher system. Usually the Align.
	virtual Point GetCameraLookAtPoint(const Point& defaultPt) const { return defaultPt; }
	virtual Point GetCameraSafePoint(const Point& defaultPt) const { return defaultPt; }
	virtual CameraId GetCameraId(const CameraId& defaultCamId) const { return defaultCamId; }
	virtual StringId64 GetCameraSettingsId() const { return INVALID_STRING_ID_64; }

	virtual bool PlayerDisableExtraGroundCheck() const { return false; }
	virtual bool PlayerDisableCrouchToStand() const { return false; }
	virtual bool PlayerForceStand() const { return false; }

};

TYPE_FACTORY_DECLARE(NdSubsystemAnimController);
