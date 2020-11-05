/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/vehicle-controller.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/armik.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/scriptx/h/anim-overlay-defines.h"
#include "ndlib/script/script-manager.h"

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/state-script/ss-instance.h"

#include "game/ai/action-pack/vehicle-action-pack.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/agent/npc.h"
#include "game/vehicle-ctrl.h"
#include "game/script-arg-iterator.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/driveable-vehicles-defines.h"
#include "game/vehicle/driveable-vehicle.h"
#include "game/vehicle/vehicle.h"
#include "game/vehicle/rail-vehicle.h"
#include "game/player/player.h"

#define JUMP_ALIGN_DEBUG 0

StringId64 g_vehicleJumpDirectionOverride = INVALID_STRING_ID_64;

namespace { // Begin file-local defines ======================================================================================================

class VehicleActionPackController : public ActionPackController
{
	using ParentClass = ActionPackController;

public:
	bool IsBusy() const override { return false; }
	void UpdateStatus() override { }

	void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override
	{
		ParentClass::Init(pNavChar, pNavControl);

		m_apEnterTime = TimeFrameNegInfinity();
	}

	bool ResolveEntry(const ActionPackResolveInput& input,
					  const ActionPack* pActionPack,
					  ActionPackEntryDef* pDefOut) const override
	{
		GAMEPLAY_ASSERT(pDefOut);
		GAMEPLAY_ASSERT(pActionPack->GetType() == ActionPack::kVehicleActionPack);

		return ResolveDefaultEntry(input, pActionPack, pDefOut);
	}

	bool ResolveDefaultEntry(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 ActionPackEntryDef* pDefOut) const override
	{
		GAMEPLAY_ASSERT(pDefOut);
		GAMEPLAY_ASSERT(pActionPack->GetType() == ActionPack::kVehicleActionPack);

		if (pActionPack->GetType() != ActionPack::kVehicleActionPack)
		{
			return false;
		}

		const VehicleActionPack* pVehicleAP = static_cast<const VehicleActionPack*>(pActionPack);

		ActionPackEntryDef& out = *pDefOut;

		const Point posWs = pActionPack->GetDefaultEntryPointWs(0.0f);

		out.m_entryAnimId = SID("hero-passenger-a-enter-front");
		out.m_entryNavLoc = GetCharacter()->AsReachableNavLocationWs(posWs, NavLocation::Type::kNavPoly);

		out.m_preferredMotionType = kMotionTypeWalk;
		out.m_stopBeforeEntry = true;

		if (const NdGameObject* pVehicle = pVehicleAP->GetVehicle().ToProcess())
		{
			const Vector apToVehicleWs = pVehicle->GetTranslation() - posWs;

			const Vector apToVehiclePs = GetCharacter()->GetParentSpace().UntransformVector(apToVehicleWs);

			const Quat toVehiclePs = QuatFromLookAt(VectorXz(apToVehiclePs), kUnitYAxis);

			out.m_entryRotPs = toVehiclePs;
		}

		return true;
	}

	void Enter(const ActionPackResolveInput& input, ActionPack* pActionPack, const ActionPackEntryDef& entryDef) override
	{
		ParentClass::Enter(input, pActionPack, entryDef);

		const VehicleActionPack* pVehicleAp = nullptr;
		if (pActionPack->GetType() == ActionPack::kVehicleActionPack)
		{
			pVehicleAp = static_cast<const VehicleActionPack*>(pActionPack);
		}
		GAMEPLAY_ASSERT(pVehicleAp);
		GAMEPLAY_ASSERTF(pVehicleAp->GetVehicle() == m_apEnterArgs.m_vehicle, ("Vehicle being entered does not match vehicle action pack being entered"));

		EnterRightAway(false);
	}

	void EnterRightAway(const bool popIn)
	{
		AiLogAnim(GetCharacter(), "Vehicle action pack controller: EnterRightAway(%s)\n", popIn ? "true" : "false");

		IAiVehicleController* pVehicleController = GetCharacter()->GetAnimationControllers()->GetVehicleController();
		GAMEPLAY_ASSERT(pVehicleController);

		if (popIn)
		{
			m_apEnterArgs.m_overrideBlendTime = 0.0f;
		}

		pVehicleController->EnterVehicle(m_apEnterArgs);

		m_apEnterTime = GetCharacter()->GetClock()->GetCurTime();
	}

	bool UpdateEntry(const ActionPackResolveInput& input,
					 const ActionPack* pActionPack,
					 ActionPackEntryDef* pDefOut) const override
	{
		return true;
	}

	void DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const override
	{
	}

	void DebugDrawExits(const ActionPackResolveInput& input,
						const ActionPack* pActionPack,
						const IPathWaypoints* pPathPs) const override
	{
	}

	/*** PUBLIC MEMBERS ***/

	EnterVehicleArgs m_apEnterArgs;

	TimeFrame m_apEnterTime;
};

struct NpcDriveConfig : public Drive::Config
{
	DC::CharacterDriveInfo* GetDriveInfo(Character* pSelf) override
	{
		return &(pSelf->GetAnimControl()->Info<DC::AnimNpcInfo>()->m_drive);
	}

	bool EnableSteering(Character* pSelf) override
	{
		return true;
	}

	const DC::CharacterDriveStateInfo* GetDriveStateInfo(const DC::AnimState* pState) override
	{
		return AnimStateLookupStateInfo<DC::CharacterDriveStateInfo>(pState, SID("drive"));
	}

	void OnJeepTensionChange(Character* pSelf, StringId64 prevJeepTension, StringId64 jeepTension) override
	{
		Npc* pNpcSelf = Npc::FromProcess(pSelf);
		if (!pNpcSelf)
		{
			return;
		}

		switch (prevJeepTension.GetValue())
		{
		case SID_VAL("combat"):
			if (IAiWeaponController* pWeaponController = pNpcSelf->GetAnimationControllers()->GetWeaponController())
			{
				pWeaponController->HolsterWeapon();
			}
			break;
		}
	}
};

struct DeferredEntry
{
	bool m_active = false;

	BoundFrame m_target;

	bool m_cleared = false;

	const DC::VehicleEntryDef* m_pDef = nullptr;

	bool IsValid() const { return m_active; }
};

const StringId64 kLandAnim = SID("vehicle-land");
const StringId64 kHangOnAnim = SID("vehicle-hang-on");

const StringId64 kApRefTakeOffPivot = SID("apReference-pivot-takeoff");
const StringId64 kApRefLandPivot = SID("apReference-pivot-landing");

struct VehicleSpot : public DC::VehicleSpotDef
{
	I32 m_spotIndex;

	// Where is this spot?
	BoundFrame m_loc;
	bool m_locValid;

	// Initial trajectory of the land anim
	Vector m_entryDir;

	// Locator of apReference-pivot-landing in the space of the land apRef
	Locator m_landPivotLs;
	bool m_landPivotLsValid;

	VehicleSpot()
	{
		memset(static_cast<DC::VehicleSpotDef*>(this), 0, sizeof(DC::VehicleSpotDef));

		m_spotIndex = -1;

		ResetEvaluation();
	}

	VehicleSpot(const DC::VehicleSpotDef& other)
		: DC::VehicleSpotDef(other)
	{
		ResetEvaluation();

		m_spotIndex = -1;
	}

	bool Valid() const
	{
		return m_isValid;
	}

	StringId64 ApReferenceName() const
	{
		return (m_apReference != INVALID_STRING_ID_64) ? m_apReference : SID("apReference-land");
	}

	BoundFrame TerminationApRef(const NdGameObject& destVehicle) const
	{
		BoundFrame apRef;

		if (m_attachPointName != INVALID_STRING_ID_64)
		{
			const AttachSystem* pAttachSystem = destVehicle.GetAttachSystem();
			if (pAttachSystem)
			{
				const AttachIndex attachPointIndex = pAttachSystem->FindPointIndexById(m_attachPointName);

				if (attachPointIndex != AttachIndex::kInvalid)
				{
					const AttachPointSpec& attachPointSpec = pAttachSystem->GetPointSpec(attachPointIndex);

					Binding apRefBinding(destVehicle.GetPlatformBinding());

					if (const CompositeBody* pCompBody = destVehicle.GetCompositeBody())
					{
						if (const RigidBody* pBody = pCompBody->FindBodyByJointIndex(attachPointSpec.m_jointIndex))
						{
							apRefBinding = Binding(pBody);
						}
					}

					apRef = BoundFrame(
						pAttachSystem->GetLocator(attachPointIndex),
						apRefBinding);
				}
			}
		}
		else
		{
			apRef = IVehicleCtrl::GetVehicleBodyLocator(destVehicle);
		}

		ASSERTF(apRef.GetBinding().IsValid(), ("Unable to bind character to destination vehicle"));

		return apRef;
	}

	void ResetEvaluation()
	{
		m_locValid = false;
		m_entryDir = kZero;
		m_landPivotLsValid = false;
	}

	void Evaluate(VehicleHandle vehicle, const Npc* pNpc)
	{
		const NdGameObject* pVehicle = vehicle.ToProcess();
		const IAiVehicleController* pVehicleController = pNpc->GetAnimationControllers()->GetVehicleController();

		ResetEvaluation();

		if (m_attachPointName != INVALID_STRING_ID_64 && pVehicle)
		{
			const Binding platformBinding = pVehicle->GetPlatformBinding();

			if (platformBinding.IsValid())
			{
				if (const AttachSystem* pAttachSystem = pVehicle->GetAttachSystem())
				{
					AttachIndex iAttach;
					if (pAttachSystem->FindPointIndexById(&iAttach, m_attachPointName))
					{
						m_loc = BoundFrame(
							pAttachSystem->GetLocator(iAttach),
							platformBinding);
						m_locValid = true;

						if (pNpc)
						{
							m_entryDir = SafeNormalize(m_loc.GetTranslationWs() - pNpc->GetTranslation(), kZero);
						}
					}
				}
			}
		}
		else if (m_overlaySuffix && !m_overlaySuffix.IsEmpty() && m_toRole == DC::kCarRiderRoleHangingOn && pNpc && pVehicleController && pVehicle)
		{
			// Now we need to apply the overlay, see what the hang on anim is, and look at the align of the anim
			// where the apReference is the vehicle's body locator.

			const AnimOverlays* pOverlays = pNpc->GetAnimControl()->GetAnimOverlays();
			const AnimOverlaySnapshot* pOverlaySnapshot = pOverlays->GetSnapshot();

			ScopedTempAllocator jj(FILE_LINE_FUNC);

			AnimOverlaySnapshot* pOverlaysCopy = NDI_NEW AnimOverlaySnapshot;
			pOverlaysCopy->Init(pOverlaySnapshot->GetNumLayers(), pOverlaySnapshot->IsUniDirectional());
			pOverlaysCopy->CopyFrom(pOverlaySnapshot);

			// Apply the overlay

			const StringId64 overlayId = pVehicleController->GetDriveCtrl()->GetVehicleOverlayId(vehicle, m_overlaySuffix.GetString());
			if (overlayId != INVALID_STRING_ID_64)
			{
				const DC::AnimOverlaySet* pOverlaySet = ScriptManager::Lookup<DC::AnimOverlaySet>(overlayId, nullptr);

				if (pOverlaySet)
				{
					const I32F layerIndex = pOverlays->GetLayerIndex(pOverlaySet->m_layerId);

					pOverlaysCopy->SetOverlaySet(layerIndex, pOverlaySet);
				}
			}

			// See what the hang-on anim is

			const StringId64 remappedLandAnim = pOverlaysCopy->LookupTransformedAnimId(kLandAnim);
			const StringId64 remappedHangOnAnim = pOverlaysCopy->LookupTransformedAnimId(kHangOnAnim);

			const BoundFrame apReference = IVehicleCtrl::GetVehicleBodyLocator(*pVehicle);
			const StringId64 apReferenceName = ApReferenceName();

			if (remappedHangOnAnim != kHangOnAnim)
			{
				// Evaluate the align of the anim

				Locator outAlign;

				const bool didEvaluate = FindAlignFromApReference(
					pNpc->GetAnimControl(),
					remappedHangOnAnim,
					0.0f,
					apReference.GetLocatorWs(),
					apReferenceName,
					&outAlign);

				if (didEvaluate)
				{
					m_loc = BoundFrame(outAlign, apReference.GetBinding());
					m_locValid = true;
				}
			}

			if (remappedLandAnim != kLandAnim)
			{
				Locator outAlign[2];

				const bool didEvaluate1 = FindAlignFromApReference(
					pNpc->GetAnimControl(),
					remappedLandAnim,
					0.0f,
					apReference.GetLocatorWs(),
					apReferenceName,
					&(outAlign[0]));

				const bool didEvaluate2 = FindAlignFromApReference(
					pNpc->GetAnimControl(),
					remappedLandAnim,
					1.0f,
					apReference.GetLocatorWs(),
					apReferenceName,
					&(outAlign[1]));

				if (didEvaluate1 && didEvaluate2)
				{
					m_entryDir = SafeNormalize(outAlign[1].Pos() - outAlign[0].Pos(), kZero);
				}

				Locator outApRefLand;
				Locator outApRefLandPivot;

				const bool didEvaluate3 = EvaluateChannelInAnim(
					pNpc->GetAnimControl(),
					remappedLandAnim,
					ApReferenceName(),
					0.0f,
					&outApRefLand);

				const bool didEvaluate4 = didEvaluate3 && EvaluateChannelInAnim(
					pNpc->GetAnimControl(),
					remappedLandAnim,
					kApRefLandPivot,
					0.0f,
					&outApRefLandPivot);

				if (didEvaluate3 && didEvaluate4)
				{
					m_landPivotLs = outApRefLand.UntransformLocator(outApRefLandPivot);
					m_landPivotLsValid = true;
				}
			}
		}
	}
};

struct ResumeData
{
	StringId64 m_animState = INVALID_STRING_ID_64;
	DC::CharacterDriveInfo m_driveInfo = {};
};

struct AimCapableInstanceBlender : public AnimStateLayer::InstanceBlender<float>
{
	float GetDefaultData() const override
	{
		return 0.0f;
	}

	bool GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut) override
	{
		const DC::CharacterDriveStateInfo* pDriveStateInfo = LookupAnimStateInfo<DC::CharacterDriveStateInfo>(pInstance, SID("drive"));

		const bool aimCapable = pDriveStateInfo && pDriveStateInfo->m_aimCapable;

		*pDataOut = aimCapable ? 1.0f : 0.0f;

		return true;
	}

	float BlendData(const float& left, const float& right, float masterFade, float animFade, float motionFade) override
	{
		return Lerp(left, right, animFade);
	}
};

} // End file-local defines ========================================================================================================

class AiVehicleController : public IAiVehicleController
{
	typedef IAiVehicleController ParentClass;

public:
	// ------------------------------------------------------------------------------------------ //
	IVehicleCtrl* m_pVehicleCtrl;

	DeferredEntry m_deferredEntry;

	JumpDirection m_selectedJumpDirection;

	AnimAction m_readyJumpAction;
	AnimAction m_jumpAction;
	AnimActionWithSelfBlend m_landAction;
	AnimAction m_aimAction;
	VehicleHandle m_jumpTarget;
	VehicleSpot m_jumpTargetSpot;

	bool m_didSendLandEvent;

	StringId64 m_queuedAimAnim;
	StringId64 m_queuedAimToIdleAnim;
	StringId64 m_queuedIdleToAimAnim;

	VehicleSpotReservation m_spotReservation;

	ResumeData m_resumeData;

	ArmIkChain m_armIk[kArmCount];

	VehicleActionPackController* m_pVehicleActionPackController;

	// ------------------------------------------------------------------------------------------ //
	AiVehicleController()
		: m_pVehicleCtrl(nullptr)
		, m_pVehicleActionPackController(nullptr)
		, m_queueSimultaneousApEntry(false)
		, m_didSendLandEvent(false)
	{}

	// ------------------------------------------------------------------------------------------ //
	void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override
	{
		ParentClass::Init(pNavChar, pNavControl);

		m_pVehicleCtrl = CreateVehicleCtrl();
		m_pVehicleCtrl->Init(pNavChar, NDI_NEW NpcDriveConfig);

		m_queuedAimAnim = INVALID_STRING_ID_64;
		m_queuedAimToIdleAnim = INVALID_STRING_ID_64;
		m_queuedIdleToAimAnim = INVALID_STRING_ID_64;

		m_selectedJumpDirection = kJumpDirectionBack;

		m_armIk[kLeftArm].Init(pNavChar, kLeftArm);
		m_armIk[kRightArm].Init(pNavChar, kRightArm);

		m_pVehicleActionPackController = NDI_NEW VehicleActionPackController;
		m_pVehicleActionPackController->Init(pNavChar, pNavControl);
	}

	// ------------------------------------------------------------------------------------------ //
	void OnIgcBind() override
	{
	}

	// ------------------------------------------------------------------------------------------ //
	void OnIgcUnbind() override
	{
	}

	// ------------------------------------------------------------------------------------------ //
	bool IsBoundToIgc() const override
	{
		return false;
	}

	// ------------------------------------------------------------------------------------------ //
	void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound) override
	{
		ParentClass::Relocate(offset_bytes, lowerBound, upperBound);

		DeepRelocatePointer(m_pVehicleCtrl, offset_bytes, lowerBound, upperBound);

		DeepRelocatePointer(m_pVehicleActionPackController, offset_bytes, lowerBound, upperBound);

		m_armIk[kLeftArm].Relocate(offset_bytes, lowerBound, upperBound);
		m_armIk[kRightArm].Relocate(offset_bytes, lowerBound, upperBound);
	}

	// ------------------------------------------------------------------------------------------ //
	ActionPackController* GetVehicleActionPackController() override
	{
		return m_pVehicleActionPackController;
	}

	// ------------------------------------------------------------------------------------------ //
	bool IsEnteredIntoVehicle() const override
	{
		return m_pVehicleCtrl->IsEnteredIntoVehicle();
	}

	// ------------------------------------------------------------------------------------------ //
	void SetActionPackEnterArgs(const EnterVehicleArgs& args) override
	{
		m_pVehicleActionPackController->m_apEnterArgs = args;
	}

	// ------------------------------------------------------------------------------------------ //
	TimeFrame GetActionPackEnterTime() const override
	{
		return m_pVehicleActionPackController->m_apEnterTime;
	}

	// ------------------------------------------------------------------------------------------ //
	void EnterActionPackRightAway() override
	{
		m_pVehicleActionPackController->EnterRightAway(true);
	}

	static NdAtomic64 s_apSimultaneousEntryFrame;
	bool m_queueSimultaneousApEntry;

	// ------------------------------------------------------------------------------------------ //
	void EnterActionPackSimultaneous() override
	{
		const I64 frame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;

		if (s_apSimultaneousEntryFrame.Get() <= frame)
		{
			s_apSimultaneousEntryFrame.Set(frame + 6);
		}

		m_queueSimultaneousApEntry = true;

		AiLogAnim(GetCharacter(), "Vehicle controller: Queued simultaneous Ap entry for frame %lld\n", s_apSimultaneousEntryFrame.Get());
	}

	// ------------------------------------------------------------------------------------------ //
	void EnterVehicle(const EnterVehicleArgs& enterArgs) override
	{
		if (const RailVehicle* pVehicle = enterArgs.m_vehicle.ToRailVehicle())
		{
			if (pVehicle->GetVehicleClass().m_isPassThrough)
			{
				if (Npc* pSelf = Npc::FromProcess(GetCharacter()))
				{
					pSelf->DisableCharacterCollider();
				}
			}
		}

		if (enterArgs.m_spotId != INVALID_STRING_ID_64)
		{
			if (m_pVehicleCtrl->IsEnteredIntoVehicle())
			{
				ExitVehicle(DC::kCarExitTypeSkip);
			}

			m_pVehicleCtrl->Enter(enterArgs);

			m_pVehicleCtrl->BeginAnimating(true);

			return;
		}

		if (enterArgs.m_role == DC::kCarRiderRoleDriver)
		{
			const DriveableVehicle* pCar = enterArgs.m_vehicle.ToDriveableVehicle();
			if (pCar && pCar->CharacterEnteringDriverSeat())
			{
				return;
			}
		}

		const DC::CarRiderRole prevRole = m_pVehicleCtrl->Role();

		bool forceAnimate = false;

		if (m_pVehicleCtrl->IsEnteredIntoVehicle())
		{
			if (m_pVehicleCtrl->Role() == enterArgs.m_role)
			{
				return;
			}

			ExitVehicle(DC::kCarExitTypeSkip);
			forceAnimate = true;
		}

		const VehicleHandle vehicle = enterArgs.m_vehicle;
		if (!vehicle.HandleValid())
		{
			return;
		}

		m_readyJumpAction.Reset();
		m_jumpAction.Reset();
		//m_landAction.Reset();
		m_aimAction.Reset();

		m_resumeData = ResumeData();

		m_pVehicleCtrl->Enter(enterArgs);

		if (DriveableVehicle* pVehicle = vehicle.ToMutableDriveableVehicle())
		{
			if (enterArgs.m_role == DC::kCarRiderRoleDriver)
			{
				g_ssMgr.BroadcastEvent(SID("ai-driving"), pVehicle->GetScriptId());
			}

			{
				Npc* pNpc = Npc::FromProcess(GetCharacter());
				if (pNpc)
				{
					if (IAiWeaponController* pWeaponController = pNpc->GetAnimationControllers()->GetWeaponController())
					{
						pWeaponController->ForceGunState(kGunStateHolstered);
					}
				}
			}

			if (enterArgs.m_entryType != DC::kCarEntryTypeImmediate)
			{
				VehicleEntrySelector selector;
				selector.m_pVehicle = pVehicle;
				selector.m_role = enterArgs.m_role;
				selector.m_fromRole = prevRole;
				selector.m_hintBasedOnPlayerEntryDir = (enterArgs.m_overrideBlendTime == 0.0f);

				const DC::VehicleEntryDef* pBestEntry = m_pVehicleCtrl->ChooseClosestEntry(selector);
				const StringId64 anim = pBestEntry ? pBestEntry->m_anim : INVALID_STRING_ID_64;

				const BoundFrame apRef = IVehicleCtrl::GetVehicleBodyLocator(*pVehicle);

				const BoundFrame moveToTarget =
					anim != INVALID_STRING_ID_64
					? m_pVehicleCtrl->GetEntryReference(anim, apRef)
					: apRef;

				if (const Npc* pSelf = Npc::FromProcess(GetCharacter()))
				{
					m_deferredEntry = DeferredEntry();
					m_deferredEntry.m_active = true;
					m_deferredEntry.m_target = moveToTarget;
					m_deferredEntry.m_pDef = pBestEntry;
				}
			}
			else
			{
				GetCharacter()->EnableNavBlocker(false);

				m_pVehicleCtrl->BeginAnimating(forceAnimate);
			}
		}

		if (INdAiWeaponController* pWeaponController = GetCharacter()->GetAnimationControllers()->GetWeaponController())
		{
			// Refresh gun state to switch hands if necessary
			pWeaponController->ForceGunState(pWeaponController->GetCurrentGunState(), true);
		}
	}

	// ------------------------------------------------------------------------------------------ //
	static bool HasPassengers(const RailVehicle* pVehicle)
	{
		if (const RailController* pRailController = pVehicle->GetRailController())
		{
			if (pRailController->GetPassenger1().HandleValid())
				return true;

			if (pRailController->GetPassenger2().HandleValid())
				return true;
		}

		return false;
	}

	// ------------------------------------------------------------------------------------------ //
	bool CanAim() const override
	{
		if (!m_pVehicleCtrl->IsEnteredIntoVehicle())
		{
			return false;
		}

		if (!m_pVehicleCtrl->CanAim())
		{
			return false;
		}

		const NdGameObject* pVehicle = m_pVehicleCtrl->GetVehicle().ToProcess();
		if (!pVehicle)
		{
			return false;
		}

		const bool isCarDriver = m_pVehicleCtrl->Role() == DC::kCarRiderRoleDriver;

		if (pVehicle->IsKindOf(g_type_RailVehicle))
		{
			if (isCarDriver && HasPassengers(static_cast<const RailVehicle*>(pVehicle)))
			{
				return false;
			}

			return true;
		}

		return !isCarDriver;
	}

	// ------------------------------------------------------------------------------------------ //
	void GoAim() override
	{
		if (!IsAiming() && CanAim())
		{
			m_pVehicleCtrl->GetDriveInfo()->m_aimAnim = SID("sol-jeep-passenger-idle-aim");
			m_pVehicleCtrl->GetDriveInfo()->m_aimToIdleAnim = SID("sol-jeep-passenger-aim-front^idle");
			m_pVehicleCtrl->GetDriveInfo()->m_idleToAimAnim = SID("sol-jeep-passenger-idle^aim-front");

			m_aimAction.Request(GetCharacter()->GetAnimControl(), SID("aim"), AnimAction::kFinishOnTransitionTaken);
		}
	}

	// ------------------------------------------------------------------------------------------ //
	bool GoAimFailed() const override
	{
		return m_aimAction.Failed();
	}

	// ------------------------------------------------------------------------------------------ //
	bool IsAiming() const override
	{
		if (const DC::CharacterDriveStateInfo* pDriveStateInfo = m_pVehicleCtrl->GetDriveStateInfo())
		{
			return pDriveStateInfo->m_aimCapable;
		}

		return false;
	}

	// ------------------------------------------------------------------------------------------ //
	bool IsAimingBackwards() const
	{
		const StringId64 phaseAnim = GetCharacter()->GetCurrentAnim();
		const bool isBack = phaseAnim == SID("sol-jeep-passenger-idle-aim-back");
		return isBack;
	}

	// ------------------------------------------------------------------------------------------ //
	bool CanPerformAimTransition() const
	{
		const INdAiWeaponController* pWeaponController = GetCharacter()->GetAnimationControllers()->GetWeaponController();

		if (pWeaponController && pWeaponController->IsReloading())
		{
			return false;
		}

		const AnimStateLayer* pBaseLayer = GetCharacter()->GetAnimControl()->GetBaseStateLayer();

		if (!pBaseLayer->IsTransitionValid(SID("aim-transition")))
		{
			return false;
		}

		const AnimStateInstance* pCurrentStateInstance = pBaseLayer->CurrentStateInstance();
		if (!pCurrentStateInstance)
		{
			return false;
		}

		if (pCurrentStateInstance->AnimFade() < 0.9f)
		{
			return false;
		}

		return true;
	}

	// ------------------------------------------------------------------------------------------ //
	void UpdateAim()
	{
		if (IsAiming())
		{
			if (const NdLocatableObject* pVehicle = m_pVehicleCtrl->GetVehicle().ToProcess())
			{
				DC::CharacterDriveInfo* pDriveInfo = m_pVehicleCtrl->GetDriveInfo();

				float aimAngle = 0.0f;

				const Point& aimTargetWs = GetCharacter()->GetAimAtPositionWs();
				const Point& aimTargetLs = pVehicle->GetLocator().UntransformPoint(aimTargetWs);
				aimAngle = AngleFromXZVec(aimTargetLs - Point(kOrigin)).ToDegrees();

				pDriveInfo->m_aimAngle = aimAngle;

				//MsgConPauseable(GetCharacter()->GetProcessId(), "% 24s: aimAngle = % 8.2f\n", DevKitOnly_StringIdToString(GetCharacter()->GetUserId()), aimAngle);

				const bool canTransition = CanPerformAimTransition();

				if (m_queuedAimAnim != INVALID_STRING_ID_64 && m_aimAction.IsValid() && m_aimAction.IsDone())
				{
					pDriveInfo->m_aimAnim = m_queuedAimAnim;
					pDriveInfo->m_aimToIdleAnim = m_queuedAimToIdleAnim;
					pDriveInfo->m_idleToAimAnim = m_queuedIdleToAimAnim;
					m_queuedAimAnim = INVALID_STRING_ID_64;
					m_queuedAimToIdleAnim = INVALID_STRING_ID_64;
					m_queuedIdleToAimAnim = INVALID_STRING_ID_64;
				}
				else if (canTransition)
				{
					const bool isBack = IsAimingBackwards();
					const float absAimAngle = Abs(aimAngle);
					const bool wantBack = (!isBack && absAimAngle > 140.0f) || (isBack && absAimAngle > 110.0f);

					if (isBack && !wantBack)
					{
						pDriveInfo->m_aimTransitionAnim =
							aimAngle < 0.0f ?
							SID("sol-jeep-passenger-aim-r-back^r-front") : SID("sol-jeep-passenger-aim-l-back^l-front");
						m_queuedAimAnim = SID("sol-jeep-passenger-idle-aim");
						m_queuedAimToIdleAnim = SID("sol-jeep-passenger-aim-front^idle");
						m_queuedIdleToAimAnim = SID("sol-jeep-passenger-idle^aim-front");
						m_aimAction.Request(GetCharacter()->GetAnimControl(), SID("aim-transition"), AnimAction::kFinishOnTransitionTaken);
					}
					else if (!isBack && wantBack)
					{
						pDriveInfo->m_aimTransitionAnim =
							aimAngle < 0.0f ?
							SID("sol-jeep-passenger-aim-r-front^-r-back") : SID("sol-jeep-passenger-aim-l-front^-l-back");
						m_queuedAimAnim = SID("sol-jeep-passenger-idle-aim-back");
						m_queuedAimToIdleAnim = SID("sol-jeep-passenger-aim-back^idle");
						m_queuedIdleToAimAnim = SID("sol-jeep-passenger-idle^aim-back");
						m_aimAction.Request(GetCharacter()->GetAnimControl(), SID("aim-transition"), AnimAction::kFinishOnTransitionTaken);
					}
				}
			}
		}
	}

	// ------------------------------------------------------------------------------------------ //
	bool CanJumpTo(VehicleHandle targetCar) const override
	{
		if (m_pVehicleCtrl->Role() == DC::kCarRiderRoleDriver)
		{
			return false;
		}

		const IVehicle* pVehicle = targetCar.GetVehicleInterface();
		if (!pVehicle)
		{
			return false;
		}

		const DC::VehicleSetup* pSetup = pVehicle->GetVehicleSetup();
		if (!pSetup)
		{
			return false;
		}

		if (!pSetup->m_spots)
		{
			return false;
		}

		if (pSetup->m_spots->GetSize() == 0)
		{
			return false;
		}

		// If any of the spots are locked by someone else, you can't jump to this vehicle.
		VehicleSpotManager& spotMgr = pVehicle->SpotManager();

		AtomicLockJanitorRead jj(&spotMgr.m_lock, FILE_LINE_FUNC);

		NpcHandle selfHandle;
		if (GetCharacter()->IsKindOf(g_type_Npc))
		{
			selfHandle = static_cast<Npc*>(GetCharacter());
		}

		for (U32F i = 0; i < pSetup->m_spots->GetSize(); ++i)
		{
			if (i >= VehicleSpotManager::kMaxSpots)
			{
				break;
			}

			const NpcHandle& spotOccupier = spotMgr.m_spotOccupier[i];

			if (spotOccupier.HandleValid() && spotOccupier != selfHandle)
			{
				return false;
			}
		}

		return true;
	}

	// ------------------------------------------------------------------------------------------ //
	/* If pOutLock is non-null, *pOutLock will ultimately hold a lock to the returned spot        */
	VehicleSpot DetermineBestSpot(VehicleHandle targetCar, VehicleSpotReservation* pOutLock) const
	{
		VehicleSpot jumpTargetSpot;

		if (const IVehicle* pVehicle = targetCar.GetVehicleInterface())
		{
			if (const DC::VehicleSetup* pSetup = pVehicle->GetVehicleSetup())
			{
				if (pSetup->m_spots && pSetup->m_spots->GetSize() > 0)
				{
					ScopedTempAllocator jj(FILE_LINE_FUNC);

					VehicleSpot* spots = NDI_NEW VehicleSpot[pSetup->m_spots->GetSize()];

					for (U32F i = 0; i < pSetup->m_spots->GetSize(); ++i)
					{
						spots[i] = *(pSetup->m_spots->At(i));
						spots[i].Evaluate(targetCar, Npc::FromProcess(GetCharacter()));

						//if (spots[i].m_locValid)
						//{
						//	g_prim.Draw(DebugCoordAxes(spots[i].m_loc.GetLocatorWs()), Seconds(5.0f), &(spots[i].m_loc));
						//
						//	g_prim.Draw( DebugArrow( spots[i].m_loc.GetTranslationWs(), spots[i].m_entryDir, kColorRed, 0.5f ), Seconds(5.0f), &(spots[i].m_loc) );
						//}
					}

					// Choose the spot whose land trajectory best matches my offset from the vehicle.
					I32F bestSpotIndex = -1;
					float bestSpotDot = -999.0f;
					for (U32F i = 0; i < pSetup->m_spots->GetSize(); ++i)
					{
						const VehicleSpot& spot = spots[i];
						if (spot.m_locValid && !AllComponentsEqual(spot.m_entryDir, Vector(kZero)))
						{
							const Vector characterOffset = GetCharacter()->GetTranslation() - spot.m_loc.GetTranslationWs();
							const Vector desiredLandDir = SafeNormalize(-characterOffset, kZero);

							const float dot = Dot(desiredLandDir, spot.m_entryDir);
							if (dot > bestSpotDot)
							{
								VehicleSpotReservation trialLock;
								if (trialLock.Update(Self(), targetCar, i))
								{
									if (pOutLock)
									{
										pOutLock->AssignFrom(Self(), trialLock);
									}
									else
									{
										trialLock.Update(Self());
									}

									bestSpotDot = dot;
									bestSpotIndex = i;
								}
							}
						}
					}

					if (bestSpotIndex >= 0)
					{
						jumpTargetSpot = spots[bestSpotIndex];
						jumpTargetSpot.m_spotIndex = bestSpotIndex;
					}
				}
			}
		}

		return jumpTargetSpot;
	}

	// ------------------------------------------------------------------------------------------ //
	const Npc& Self() const
	{
		AI_ASSERT(GetCharacter()->IsKindOf(g_type_Npc));

		return *static_cast<Npc*>(GetCharacter());
	}

	// ------------------------------------------------------------------------------------------ //
	Npc& Self()
	{
		AI_ASSERT(GetCharacter()->IsKindOf(g_type_Npc));

		return *static_cast<Npc*>(GetCharacter());
	}

	// ------------------------------------------------------------------------------------------ //
	void GoReadyJump(VehicleHandle targetCar) override
	{
		VehicleSpotReservation acquiredSpotReservation;
		const VehicleSpot jumpTargetSpot = DetermineBestSpot(targetCar, &acquiredSpotReservation);

		if (jumpTargetSpot.Valid())
		{
			{
				AI_ASSERT(acquiredSpotReservation.Held());

				m_spotReservation.AssignFrom(Self(), acquiredSpotReservation);

				m_readyJumpAction.Reset();
				m_jumpTarget = targetCar;
				m_jumpTargetSpot = jumpTargetSpot;

				const JumpDirection readyJumpDirection = SelectJumpDirection(targetCar);
				const StringId64 readyJumpAnim = GetReadyJumpAnim(readyJumpDirection);
				m_selectedJumpDirection = readyJumpDirection;
				PlugInReadyJumpAnims(readyJumpAnim, m_pVehicleCtrl->GetDriveInfo());

				m_readyJumpAction.Request(GetCharacter()->GetAnimControl(), SID("ready-jump"), AnimAction::kFinishOnTransitionTaken);
			}
		}

		AI_ASSERT(!acquiredSpotReservation.Held());
	}

	// ------------------------------------------------------------------------------------------ //
	static void PlugInReadyJumpAnims(const StringId64 readyJumpAnim, DC::CharacterDriveInfo* pDriveInfo)
	{
		pDriveInfo->m_readyJumpAnim = readyJumpAnim;
		pDriveInfo->m_readyJumpIdleAnim = StringId64Concat(readyJumpAnim, "-idle");
		pDriveInfo->m_readyJumpToIdleAnim = StringId64Concat(readyJumpAnim, "-idle^idle");

		pDriveInfo->m_jumpAnim = StringId64Concat(readyJumpAnim, "^jump");
	}

	// ------------------------------------------------------------------------------------------ //
	JumpDirection SelectJumpDirection(VehicleHandle targetCar) const
	{
		switch (g_vehicleJumpDirectionOverride.GetValue())
		{
		case SID_VAL("back"): return kJumpDirectionBack;
		case SID_VAL("left"): return kJumpDirectionLeft;
		case SID_VAL("right"): return kJumpDirectionRight;
		}

		if (const NdLocatableObject* pCurrentVehicle = m_pVehicleCtrl->GetVehicle().ToProcess())
		{
			if (const NdLocatableObject* pTargetVehicle = targetCar.ToProcess())
			{
				const Locator targetVehicleLs = pCurrentVehicle->GetLocator().UntransformLocator(pTargetVehicle->GetLocator());
				const Vector toTargetLs = targetVehicleLs.Pos() - Point(kOrigin);
				const float readyJumpAngle = AngleFromXZVec(toTargetLs).ToDegrees();
				if (readyJumpAngle >= 140.0f || readyJumpAngle <= -140.0f)
				{
					return kJumpDirectionBack;
				}
				else if (readyJumpAngle > 0.0f)
				{
					return kJumpDirectionLeft;
				}
				else
				{
					return kJumpDirectionRight;
				}
			}
		}
		return kJumpDirectionBack;
	}

	// ------------------------------------------------------------------------------------------ //
	StringId64 GetReadyJumpAnim(JumpDirection direction) const
	{
		switch (direction)
		{
			case kJumpDirectionLeft:	return SID("vehicle-ready-jump-left");
			case kJumpDirectionRight:	return SID("vehicle-ready-jump-right");
			case kJumpDirectionBack:	return SID("vehicle-ready-jump-back");
		}
		return SID("vehicle-ready-jump-back");
	}

	// ------------------------------------------------------------------------------------------ //
	bool GoReadyJumpFailed() const override
	{
		return m_readyJumpAction.Failed() || !m_jumpTarget.HandleValid();
	}

	// ------------------------------------------------------------------------------------------ //
	bool IsReadyJump() const override
	{
		return
			m_jumpTarget.HandleValid()
			&& m_readyJumpAction.IsDone()
			&& GetCharacter()->GetAnimControl()->GetBaseStateLayer()->IsTransitionValid( SID("jump") );
	}

	// ------------------------------------------------------------------------------------------ //
	void GoIdle() override
	{
		m_spotReservation.Update(Self());

		{
			AnimStateLayer* pBaseLayer = GetCharacter()->GetAnimControl()->GetBaseStateLayer();

			if (pBaseLayer->IsTransitionValid(SID("vehicle-idle")))
			{
				pBaseLayer->RequestTransition(SID("vehicle-idle"));
			}
			else if (pBaseLayer->IsTransitionValid(SID("drive-idle")))
			{
				pBaseLayer->RequestTransition(SID("drive-idle"));
			}
			else
			{
				m_pVehicleCtrl->BeginAnimating(false);
			}
		}
	}

	// ------------------------------------------------------------------------------------------ //
	void Resume() override
	{
		const NdGameObject* pVehicle = m_pVehicleCtrl->GetVehicle().ToProcess();
		GAMEPLAY_ASSERTF(pVehicle, ("Resuming in-vehicle without a vehicle process"));

		if (pVehicle)
		{
			*(m_pVehicleCtrl->GetDriveInfo()) = m_resumeData.m_driveInfo;

			StringId64 resumeState = INVALID_STRING_ID_64;

			switch (m_resumeData.m_animState.GetValue())
			{
			case SID_VAL("s_hero-passenger-b-aim"):
			case SID_VAL("s_hero-passenger-b-idle"):
				resumeState = m_resumeData.m_animState;
				break;

			case SID_VAL("s_hero-passenger-b-aim^aim"):
				resumeState = SID("s_hero-passenger-b-aim");
				break;
			}

			if (resumeState != INVALID_STRING_ID_64)
			{
				const BoundFrame driveApRef = IVehicleCtrl::GetVehicleBodyLocator(*pVehicle);

				FadeToStateParams params;
				params.m_apRef = driveApRef;
				params.m_apRefValid = true;
				params.m_animFadeTime = 0.3f;

				GetCharacter()->GetAnimControl()->GetBaseStateLayer()->FadeToState(resumeState, params);
			}
			else
			{
				GoIdle();
			}
		}
	}

	// ------------------------------------------------------------------------------------------ //
	bool JumpSupportsTakeoffPivot(BoundFrame* pOutPivotFrame) const
	{
		const AnimControl* pAnimControl = GetCharacter()->GetAnimControl();

		const StringId64 jumpAnim = m_pVehicleCtrl->GetDriveInfo()->m_jumpAnim;

		if (const ArtItemAnim* pAnim = pAnimControl->LookupAnim(jumpAnim).ToArtItem())
		{
			if (FindChannel(pAnim, kApRefTakeOffPivot))
			{
				if (const NdGameObject* pDestVehicle = m_jumpTarget.ToProcess())
				{
					const BoundFrame termApRef = m_jumpTargetSpot.TerminationApRef(*pDestVehicle);

					*pOutPivotFrame =
						BoundFrame(
							Locator(
								GetCharacter()->GetTranslation(),
								QuatFromLookAt(VectorXz(termApRef.GetTranslationWs() - GetCharacter()->GetTranslation()), kUnitYAxis)
							),

							termApRef.GetBinding()
							//GetCharacter()->GetBinding()
						);

					//g_prim.Draw(DebugArrow( GetCharacter()->GetTranslation(), termApRef.GetTranslationWs(), kColorYellow ), Seconds(5.0f), &termApRef);

					//g_prim.Draw(DebugArrow(GetCharacter()->GetTranslation(), GetLocalX(pOutPivotFrame->GetRotationWs()), kColorYellow), Seconds(5.0f), &termApRef);

					return true;
				}
			}
		}

		return false;
	}

	// ------------------------------------------------------------------------------------------ //
	void Jump() override
	{
		if (IsReadyJump())
		{
			m_pVehicleCtrl->GetDriveInfo()->m_landAnim = m_jumpTargetSpot.m_toNavigation ? SID("sol-bike-passenger-jump-land-edge-truck") : kLandAnim;
			m_pVehicleCtrl->GetDriveInfo()->m_hangOnAnim = kHangOnAnim;
			m_pVehicleCtrl->GetDriveInfo()->m_jumpToNavigation = m_jumpTargetSpot.m_toNavigation;

			BoundFrame pivotFrame;
			if (JumpSupportsTakeoffPivot(&pivotFrame))
			{
				m_jumpAction.Request(GetCharacter()->GetAnimControl(), SID("jump"), AnimAction::kFinishOnTransitionTaken, &pivotFrame, -1.0f, kApRefTakeOffPivot);
			}
			else
			{
				m_jumpAction.Request(GetCharacter()->GetAnimControl(), SID("jump"), AnimAction::kFinishOnTransitionTaken);
			}

			IAiWeaponController* pWeaponController = GetCharacter()->GetAnimationControllers()->GetWeaponController();
			if (pWeaponController && !m_jumpTargetSpot.m_toNavigation)
			{
				pWeaponController->HolsterWeapon();
			}

			const NdGameObject* pVehicle = m_jumpTarget.ToProcess();
			const StringId64 vehicleId = pVehicle ? pVehicle->GetScriptId() : INVALID_STRING_ID_64;
			g_ssMgr.BroadcastEvent(SID("npc-started-vehicle-jump"), GetCharacter()->GetScriptId(), vehicleId);

			PostEvent(SID("npc-started-vehicle-jump"), m_pVehicleCtrl->GetVehicle(), GetCharacter()->GetScriptId(), m_selectedJumpDirection);

			g_ssMgr.BroadcastEvent(SID("npc-jumped-to-vehicle"), GetCharacter()->GetScriptId(), vehicleId, m_jumpTargetSpot.m_spotIndex);

			if (Npc* pSelf = Npc::FromProcess(GetCharacter()))
			{
				pSelf->EnableCharacterCollider();
			}
		}
	}

	// ------------------------------------------------------------------------------------------ //
	bool IsJumping() const override
	{
		const AnimStateInstance* pTopInstance = GetCharacter()->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance();
		return
			pTopInstance == m_jumpAction.GetTransitionDestInstance(GetCharacter()->GetAnimControl());
	}

	// ------------------------------------------------------------------------------------------ //
	AnimStateInstance* GetLandInstance() const
	{
		AnimStateInstance* pTopInstance = GetCharacter()->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance();

		if (pTopInstance == m_landAction.GetTransitionDestInstance(GetCharacter()->GetAnimControl()))
		{
			return pTopInstance;
		}
		else
		{
			return nullptr;
		}
	}

	// ------------------------------------------------------------------------------------------ //
	bool IsLanding() const override
	{
		return GetLandInstance() != nullptr;
	}

	// ------------------------------------------------------------------------------------------ //
	void UpdateJump()
	{
		Character* pChar = GetCharacter();
		AnimControl* pAnimControl = pChar->GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		const AnimStateInstance* pCurInstance = pBaseLayer->CurrentStateInstance();

		const NdGameObject* pDestVehicle = m_jumpTarget.ToProcess();

		#if JUMP_ALIGN_DEBUG
		if (IsJumping() || IsLanding())
		{
			const NdGameObject* pPlayerVehicle = NdGameObject::FromProcess(GetPlayer()->GetEnteredVehicle().ToProcess());
			const BoundFrame& vehicleFrame = pPlayerVehicle ? IVehicleCtrl::GetVehicleBodyLocator(*pPlayerVehicle) : BoundFrame();
			const BoundFrame* pVehicleFrame = pPlayerVehicle ? &vehicleFrame : nullptr;

			g_prim.Draw(DebugCoordAxes(pChar->GetLocator(), 0.2f), Seconds(5.0f), pVehicleFrame);

			char txt[256] = "no text";
			sprintf(txt,
					"%s %.2f fade=%.2f",
					IsJumping() ? "jump" : "land",
					pCurInstance->Phase(),
					pCurInstance->MasterFade());

			g_prim.Draw(DebugString(pChar->GetTranslation(), txt, kColorWhite, 0.5f), Seconds(5.0f), pVehicleFrame);
		}
		#endif

		if (IsReadyJump())
		{
			const AnimStateInstance* pReadyJumpInstance = pBaseLayer->CurrentStateInstance();
			const StringId64 readyJumpAnim = pReadyJumpInstance ? pReadyJumpInstance->GetPhaseAnim() : INVALID_STRING_ID_64;

			const JumpDirection selectedJumpDirection = SelectJumpDirection(m_jumpTarget);
			const StringId64 selectedReadyJumpAnim = GetReadyJumpAnim(selectedJumpDirection);

			DC::CharacterDriveInfo scratch;
			m_selectedJumpDirection = selectedJumpDirection;
			PlugInReadyJumpAnims(selectedReadyJumpAnim, &scratch);
			const StringId64 desiredReadyJumpAnim = pAnimControl->GetAnimOverlays()->LookupTransformedAnimId(scratch.m_readyJumpIdleAnim);

			if (readyJumpAnim != desiredReadyJumpAnim && pBaseLayer->IsTransitionValid(SID("ready-jump-direction-change")))
			{
				PlugInReadyJumpAnims(selectedReadyJumpAnim, m_pVehicleCtrl->GetDriveInfo());

				pBaseLayer->RequestTransition(SID("ready-jump-direction-change"));
			}

			return;
		}

		if (IsJumping())
		{
			if (pBaseLayer->IsTransitionActive(SID("land")))
			{
				if (pDestVehicle)
				{
					//const VehicleSpot possibleBetterSpot = DetermineBestSpot(m_jumpTarget);
					//if (possibleBetterSpot.Valid())
					//{
					//	VehicleSpotReservation spotReservation;
					//	if (spotReservation.Update(Self(), m_jumpTarget, possibleBetterSpot.m_spotIndex))
					//	{
					//		m_spotReservation.Update(Self());
					//		m_spotReservation = spotReservation;
					//
					//		m_jumpTargetSpot = possibleBetterSpot;
					//	}
					//}

					const DcString& overlaySuffix = m_jumpTargetSpot.m_overlaySuffix;
					if (overlaySuffix.GetString() && !overlaySuffix.IsEmpty())
					{
						m_pVehicleCtrl->SetVehicleOverlay(m_jumpTarget, overlaySuffix.GetString());
					}

					const BoundFrame termApRef = m_jumpTargetSpot.TerminationApRef(*pDestVehicle);

					BoundFrame newApRef(kIdentity);
					StringId64 apRefName = INVALID_STRING_ID_64;

					m_landAction.Reset();
					m_didSendLandEvent = false;

					if (m_jumpTargetSpot.m_landPivotLsValid)
					{
						apRefName = kApRefLandPivot;

						newApRef = BoundFrame(Locator(termApRef.GetLocatorWs()
														  .TransformPoint(m_jumpTargetSpot.m_landPivotLs.Pos()),
													  QuatFromLookAt(VectorXz(pChar->GetTranslation()
																			  - termApRef.GetTranslationWs()),
																	 kUnitYAxis)),
											  termApRef.GetBinding());

						SelfBlendAction::Params sbParams;
						sbParams.m_destAp = termApRef;
						sbParams.m_apChannelId = m_jumpTargetSpot.ApReferenceName();

						if (m_jumpTargetSpot.m_selfBlend)
						{
							sbParams.m_blendParams = *m_jumpTargetSpot.m_selfBlend;
						}
						else
						{
							sbParams.m_blendParams.m_phase = 0.5f;
							sbParams.m_blendParams.m_time = 0.4f;
							sbParams.m_blendParams.m_curve = DC::kAnimCurveTypeUniformS;
						}

						m_landAction.ConfigureSelfBlend(pChar, sbParams);
					}
					else
					{
						apRefName = m_jumpTargetSpot.ApReferenceName();

						newApRef = termApRef;
					}

					m_landAction.Request(pAnimControl,
										 SID("land"),
										 AnimAction::kFinishOnTransitionTaken,
										 &newApRef,
										 0.0f,
										 apRefName);
				}
				else
				{
					GoIdle();
				}
			}

			return;
		}

		if (IsLanding())
		{
			if (m_jumpTargetSpot.m_toNavigation)
			{
				ExitVehicle(DC::kCarExitTypeSkip);

				GetCharacter()->SetBinding(m_jumpTarget.ToProcess()->GetPlatformBinding());
			}
			else if (m_jumpTargetSpot.m_toRole != DC::kCarRiderRoleNone)
			{
				if (pDestVehicle && pBaseLayer->IsTransitionActive(SID("hang-on")))
				{
					const BoundFrame newApRef = m_jumpTargetSpot.TerminationApRef(*pDestVehicle);

					const StringId64 newApRefChannel = m_jumpTargetSpot.ApReferenceName();

					FadeToStateParams params;
					params.m_apRef = newApRef;
					params.m_apRefValid = true;
					params.m_customApRefId = newApRefChannel;

					pBaseLayer->RequestTransition(SID("hang-on"), &params);
				}

				const DriveableVehicle* pCurrentVehicle = m_pVehicleCtrl->GetVehicle().ToDriveableVehicle();

				if (pDestVehicle && pDestVehicle->IsKindOf(g_type_DriveableVehicle))
				{
					if (pDestVehicle != pCurrentVehicle)
					{
						ExitVehicle(DC::kCarExitTypeSkip);

						EnterVehicleArgs args;
						args.m_entryType = DC::kCarEntryTypeImmediate;
						args.m_role = m_jumpTargetSpot.m_toRole;
						args.m_vehicle = const_cast<NdGameObject*>(pDestVehicle);
						EnterVehicle(args);
					}
				}
				else
				{
					GoIdle();
				}
			}
			else
			{
				GAMEPLAY_ASSERTF(false, ("Vehicle %s: jump target spot does not define any way to terminate the jump", m_jumpTarget.ToProcess()->GetName()));
			}

			return;
		}
	}

	// ------------------------------------------------------------------------------------------ //
	void PostRootLocatorUpdate() override
	{
		return;
	}

	// ------------------------------------------------------------------------------------------ //
	void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override
	{
		STRIP_IN_FINAL_BUILD;

		if (!g_navCharOptions.m_vehicle.m_display)
		{
			return;
		}

		if (!pPrinter)
		{
			return;
		}

		ScreenSpaceTextPrinter& printer = *pPrinter;

		if (g_navCharOptions.m_vehicle.m_displayDetails)
		{
			printer.PrintText(kColorGray, "  Can Die from Impact: %s", CanDieFromVehicleImpact() ? "true" : "false");
			printer.PrintText(kColorGray, "  Can Melee:           %s", CanMelee()                ? "true" : "false");
			printer.PrintText(kColorGray, "  Can Aim:             %s", CanAim()                  ? "true" : "false");
			printer.PrintText(kColorGray, "  Is Aiming:           %s", IsAiming()                ? "true" : "false");
			printer.PrintText(kColorGray, "  Is Aiming Backwards: %s", IsAimingBackwards()       ? "true" : "false");
			printer.PrintText(kColorGray, "  Is Ready Jump:       %s", IsReadyJump()             ? "true" : "false");
			printer.PrintText(kColorGray, "  Is Jumping:          %s", IsJumping()               ? "true" : "false");
			printer.PrintText(kColorGray, "  Is Landing:          %s", IsLanding()               ? "true" : "false");
			printer.PrintText(kColorGray, "  Is Hanging On:       %s", IsHangingOn()             ? "true" : "false");
		}

		printer.PrintText(kColorWhite, "Vehicle Controller (Has%s Entered Vehicle)", IsEnteredIntoVehicle() ? "" : " Not");
	}

	// ------------------------------------------------------------------------------------------ //
	bool IsHangingOn() const override
	{
		if (const DC::CharacterDriveStateInfo* pDriveStateInfo = m_pVehicleCtrl->GetDriveStateInfo())
		{
			return pDriveStateInfo->m_isHangingOn;
		}

		return false;
	}

	// ------------------------------------------------------------------------------------------ //
	DC::CarMeleeHangOnLoc GetHangOnLoc() const override
	{
		if (IsHangingOn() && m_jumpTargetSpot.Valid())
		{
			return m_jumpTargetSpot.m_toHangOn;
		}

		return DC::kCarMeleeHangOnLocNone;
	}

	// ------------------------------------------------------------------------------------------ //
	bool CanMelee() const override
	{
		if (const DC::CharacterDriveStateInfo* pDriveStateInfo = m_pVehicleCtrl->GetDriveStateInfo())
		{
			return pDriveStateInfo->m_canMelee;
		}

		return false;
	}

	// ------------------------------------------------------------------------------------------ //
	bool CanDieFromVehicleImpact() const override
	{
		return !m_pVehicleCtrl->IsInDriveState();
	}

	// ------------------------------------------------------------------------------------------ //
	void DieInVehicle(Event& evt)
	{
		AiLogAnim(GetCharacter(), "Vehicle controller: DieInVehicle\n");

		IAiHitController* pHitController = GetCharacter()->GetAnimationControllers()->GetHitController();
		if (!pHitController)
		{
			DEBUG_HALTF(("Can't die in vehicle if no hit controller"));
			return;
		}

		const DriveableVehicle* pVehicle = m_pVehicleCtrl->GetVehicle().ToDriveableVehicle();
		if (!pVehicle)
		{
			DEBUG_HALTF(("Can't die in vehicle if no vehicle"));
			return;
		}

		StringId64 deathAnimId = INVALID_STRING_ID_64;
		switch (m_pVehicleCtrl->Role())
		{
		case DC::kCarRiderRolePassengerShotgun:
			deathAnimId = SID("jeep-death-cliff-front--pass-a");
			break;

		case DC::kCarRiderRolePassengerBack:
			deathAnimId = SID("jeep-death-cliff-front--pass-b");
			break;
		}

		if (deathAnimId == INVALID_STRING_ID_64)
		{
			return;
		}

		// We don't want ragdoll on these deaths
		GetCharacter()->DisallowRagdoll();

		IHealthSystem* pHealthSystem = GetCharacter()->GetHealthSystem();
		if (pHealthSystem)
		{
			pHealthSystem->Kill();
		}

		const BoundFrame deathApRef = IVehicleCtrl::GetVehicleBodyLocator(*pVehicle);

		pHitController->DieSpecial(deathAnimId, &deathApRef);
	}

	// ------------------------------------------------------------------------------------------ //
	void HandleEvent(Event & evt) override
	{
		switch (evt.GetMessage().GetValue())
		{
		case SID_VAL("drive-event"):

			switch (evt.Get(0).GetAsStringId().GetValue())
			{
			case SID_VAL("die-in-vehicle"):
				DieInVehicle(evt);
				break;

			case SID_VAL("query-vehicle"):
				m_pVehicleCtrl->HandleEvent(evt);
				break;

			default:
				m_pVehicleCtrl->HandleEvent(evt);
				break;
			}
			break;

		case SID_VAL("enter-vehicle"):
			EnterVehicle(EnterVehicleArgs(evt));
			break;

		case SID_VAL("exit-vehicle"):
			{
				I32 exitType = -1;

				if (evt.GetNumParams() >= 1)
				{
					exitType = evt.Get(0).GetI32();
				}

				if (exitType < 0)
				{
					exitType = DC::kCarExitTypeNormal;
				}

				ExitVehicle(exitType);
			}
			break;
		}
	}

	// ------------------------------------------------------------------------------------------ //
	void UpdateStatus() override
	{
		const I64 frame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
		if (m_queueSimultaneousApEntry && frame >= s_apSimultaneousEntryFrame.Get())
		{
			m_queueSimultaneousApEntry = false;

			EnterActionPackRightAway();

			PostEvent(SID("notify-buddies-popping-into-car"), GetPlayerHandle());
		}

		//if (m_spotReservation.m_spotIndex >= 0)
		//{
		//	MsgConPauseable(
		//		GetCharacter()->GetProcessId(),
		//		"%s: Spot reservation [%s, %d]\n",
		//		GetCharacter()->GetName(),
		//		m_spotReservation.m_vehicle.HandleValid() ? m_spotReservation.m_vehicle.ToProcess()->GetName() : "null",
		//		m_spotReservation.m_spotIndex);
		//}

		const bool isBusy = IsBusy();

		//MsgOut("[%lld] %s: isBusy = %s\n", EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused, GetCharacter()->GetName(), isBusy ? "true" : "false");

		// Don't clear reservations; hold them until the character dies.
		static const bool neverClearReservations = true;
		static const bool clearReservations = !neverClearReservations;

		if (clearReservations && !isBusy)
		{
			//if (m_spotReservation.m_spotIndex != -1)
			//{
			//	MsgConPersistent("Cleared reservation on %s\n", GetCharacter()->GetName());
			//}

			m_spotReservation.Update(Self());
		}

		if (isBusy)
		{
			m_resumeData.m_animState = GetCharacter()->GetCurrentAnimState();
			m_resumeData.m_driveInfo = *(m_pVehicleCtrl->GetDriveInfo());
		}

		bool igcMode = false;
		if (m_pVehicleCtrl->GetVehicle().HandleValid())
		{
			const AiScriptController* pScriptController = GetCharacter()->GetAnimationControllers()->GetScriptController(kNpcScriptFullBodyController);
			if (pScriptController && pScriptController->IsAnimating())
			{
				if (pScriptController->GetParams().m_exitMode == DC::kAnimateExitVehicle)
				{
					igcMode = true;
				}
			}
		}

		m_pVehicleCtrl->SetIgcMode(igcMode);

		if (igcMode)
		{
			return;
		}

		m_readyJumpAction.Update(GetCharacter()->GetAnimControl());
		m_jumpAction.Update(GetCharacter()->GetAnimControl());
		m_landAction.Update(GetCharacter()->GetAnimControl());
		m_aimAction.Update(GetCharacter()->GetAnimControl());

		if (!m_didSendLandEvent && m_landAction.Succeeded())
		{
			const StringId64 vehicleId = m_jumpTarget.GetScriptId();

			if (vehicleId != INVALID_STRING_ID_64)
			{
				//ALWAYS_HALTF( ("base layer state = %s", DevKitOnly_StringIdToString( GetCharacter()->GetCurrentAnimState() ) ) );

				g_ssMgr.BroadcastEvent(SID("npc-landed-on-vehicle"), GetCharacter()->GetScriptId(), vehicleId);
			}

			m_didSendLandEvent = true;
		}

		if (DC::CharacterDriveInfo* pDriveInfo = m_pVehicleCtrl->GetDriveInfo())
		{
			pDriveInfo->m_isBuddy = GetCharacter()->IsBuddyNpc();
			pDriveInfo->m_isRail = (m_pVehicleCtrl->GetVehicle().ToRailVehicle() != nullptr);
			pDriveInfo->m_handOverHand = true;
		}

		UpdateJump();
		UpdateAim();

		m_pVehicleCtrl->Update();

		const Npc* pSelf = Npc::FromProcess(GetCharacter());

		if (pSelf && m_deferredEntry.IsValid())
		{
			const AnimActionController* pLoco = pSelf->GetAnimationControllers()->GetLocomotionController();
			if (!pLoco->IsBusy())
			{
				GetCharacter()->EnableNavBlocker(false);

				if (m_deferredEntry.m_pDef)
				{
					m_pVehicleCtrl->PlayEntryAnimation(m_deferredEntry.m_pDef, nullptr);
				}
				else
				{
					m_pVehicleCtrl->BeginAnimating(false);
				}

				m_deferredEntry = DeferredEntry();
			}
		}

		if (m_pVehicleCtrl->IsEnteredIntoVehicle())
		{
			if (m_pVehicleCtrl->ExitFinished())
			{
				Exit();
			}
		}
	}

	// ------------------------------------------------------------------------------------------ //
	void UpdateProcedural() override
	{
		const NdGameObject* pVehicle = m_pVehicleCtrl->GetVehicle().ToProcess();
		const Locator charLocWs = GetCharacter()->GetLocator();

		if (pVehicle && m_pVehicleCtrl->Role() == DC::kCarRiderRoleDriver)
		{
			float bikeIkBlend = 0.0f;

			if (m_pVehicleCtrl->IsOnMotorBike())
			{
				bikeIkBlend = 1.0f;
			}

			if (bikeIkBlend > 0.0f)
			{
				const AnimStateLayer* pBaseLayer = GetCharacter()->GetAnimControl()->GetBaseStateLayer();

				float perArmBlend[kArmCount] = { 1.0f, 1.0f };

				AimCapableInstanceBlender blender;

				// What is the character's gun hand?
				const ArmIndex gunHand = Self().IsLeftHanded() ? kLeftArm : kRightArm;
				perArmBlend[gunHand] = 1.0f - blender.BlendForward(pBaseLayer, 0.0f);

				ScopedTempAllocator jj(FILE_LINE_FUNC);

				for (I32 armIndex = 0; armIndex < 2; ++armIndex)
				{
					m_armIk[armIndex].ReadJointCache();

					// It's easy to find the joints on the bike..

					// We need to find the two apReferences in the character's base layer

					// From this we can find our IK goal locator

					const StringId64 handleBarJoint = (armIndex == 0) ? SID("handle_l_attach") : SID("handle_r_attach");

					const I32 handleBarJointIndex = pVehicle->FindJointIndex(handleBarJoint);

					if (handleBarJointIndex >= 0)
					{
						const Locator handleBarWs = pVehicle->GetAnimControl()->GetJointCache()->GetJointLocatorWs(handleBarJointIndex);

						const StringId64 apRefHand = (armIndex == 0) ? SID("apReference-hand-l") : SID("apReference-hand-r");

						const StringId64 apRefHandle = (armIndex == 0) ? SID("apReference-handlebar-l") : SID("apReference-handlebar-r");

						bool didEval = false;
						const Locator apRefHandLs = pBaseLayer->EvaluateAP(apRefHand, nullptr, &didEval);

						if (didEval)
						{
							didEval = false;
							const Locator apRefHandleLs = pBaseLayer->EvaluateAP(apRefHandle, nullptr, &didEval);

							if (didEval)
							{
								//DebugDrawSphere(charLocWs.TransformPoint(apRefHandLs.Pos()), 0.2f, kColorRed, kPrimDuration1FramePauseable);
								//DebugDrawSphere(charLocWs.TransformPoint(apRefHandleLs.Pos()), 0.2f, kColorRed, kPrimDuration1FramePauseable);

								const Locator handleToHand = apRefHandleLs.UntransformLocator(apRefHandLs);

								const Locator desiredHandLocWs = handleBarWs.TransformLocator(handleToHand);

								const float blend = bikeIkBlend * perArmBlend[armIndex];

								extern bool g_railVehicleDebugDrawBikeDriverIk;
								if (!IsFinalBuild() && g_railVehicleDebugDrawBikeDriverIk)
								{
									g_prim.Draw(DebugCoordAxesLabeled(desiredHandLocWs, StringBuilder<128>("%.2f", blend).c_str(), 0.3f), kPrimDuration1FramePauseable);
								}

								ArmIkInstance armIk;

								armIk.m_ikChain = &(m_armIk[armIndex]);
								armIk.m_armIndex = armIndex;
								armIk.m_goalPosWs = desiredHandLocWs.Pos();
								armIk.m_tt = blend;
								armIk.m_abortIfCantSolve = false;

								SolveArmIk(&armIk);

								{
									const Quat desiredWristRotWs = desiredHandLocWs.Rot();
									const Quat wristPostIk = m_armIk[armIndex].GetWristLocWs().Rot();

									const Quat rotationToApply = Slerp(Quat(kIdentity), desiredWristRotWs * Conjugate(wristPostIk), blend);

									m_armIk[armIndex].RotateWristWs(rotationToApply);
								}
							}
						}
					}

					m_armIk[armIndex].WriteJointCache();
				}
			}
		}
	}

	// ------------------------------------------------------------------------------------------ //
	void OnDeath() override
	{
		VehicleHandle hVehicle = m_pVehicleCtrl->GetVehicle();

		m_spotReservation.Update(Self());

		ExitVehicle(DC::kCarExitTypeSkip);

		if (const NdGameObject* pVehicle = hVehicle.ToProcess())
		{
			Character* pChar = GetCharacter();
			CompositeBody* pRagdollCompBody = pChar ? pChar->GetRagdollCompositeBody() : nullptr;

			// disable collision between ragdoll and vehicle body
			const CompositeBody* pVehicleCompBody = pVehicle->GetCompositeBody();
			const RigidBody* pChassis = pVehicleCompBody ? pVehicleCompBody->FindBodyByJointSid(SID("body")) : nullptr;
			for (U32 i = 0 ; i < pVehicleCompBody->GetNumBodies(); ++i)
			{
				Collide::SetCollisionBetweenBodies(pRagdollCompBody, pVehicleCompBody->GetBody(i), false);
			}
			//if (pChassis)
			//	Collide::SetCollisionBetweenBodies(pRagdollCompBody, pChassis, false);
			//if (pRagdollCollision)
			//Collide::SetCollisionBetweenBodies(pRagdollCompBody, pRagdollCollision, false);

			// restore vehicle binding if the NPC is dead upon vehicle death
			if (pRagdollCompBody && pChassis)
			{
				pRagdollCompBody->SetParentBody(pChassis);
				pRagdollCompBody->SetAutoUpdateParentBody(false);
			}
		}
	}

	// ------------------------------------------------------------------------------------------ //
	bool IsBusy() const override
	{
		const bool isBusy = m_pVehicleCtrl->IsBusy() || m_deferredEntry.IsValid();

		//MsgOut("[%lld] %s: isBusy = %s\n", EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused, GetCharacter()->GetName(), isBusy ? "true" : "false");

		return isBusy;
	}

	// ------------------------------------------------------------------------------------------ //
	bool ShouldInterruptNavigation() const override
	{
		const bool isBusy = m_pVehicleCtrl->IsBusy() || m_deferredEntry.IsValid();

		if (GetCharacter()->IsBuddyNpc())
		{
			//MsgOut("[%lld] %s: IAiVehicleController::ShouldInterruptNavigation = %s; anim state = %s\n", EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused, GetCharacter()->GetName(), isBusy ? "true" : "false", DevKitOnly_StringIdToString( GetCharacter()->GetCurrentAnimState() ));
		}

		return isBusy;
	}

	// ------------------------------------------------------------------------------------------ //
	void Exit()
	{
		if (m_pVehicleCtrl->IsEnteredIntoVehicle())
		{
			const VehicleHandle hExitingVehicle = m_pVehicleCtrl->GetVehicle();
			const bool wasInJeep = hExitingVehicle.ToDriveableVehicle();

			if (m_deferredEntry.IsValid())
			{
				m_deferredEntry = DeferredEntry();
			}

			m_pVehicleCtrl->Exit();

			INdAiWeaponController* pWeaponController = GetCharacter()->GetAnimationControllers()->GetWeaponController();

			switch (GetTensionMode())
			{
			case DC::kTensionModeUnaware:
			case DC::kTensionModeCombat:
				if (!wasInJeep)
				{
					break;
				}

				if (!GetCharacter()->IsBuddyNpc())
				{
					break;
				}

				if (!pWeaponController)
				{
					break;
				}

				pWeaponController->DrawWeapon();
				break;
			}
		}
	}

	// ------------------------------------------------------------------------------------------ //
	void ExitVehicle(const DC::CarExitType exitType) override
	{
		if (!m_pVehicleCtrl->IsEnteredIntoVehicle())
		{
			return;
		}

		if (m_pVehicleCtrl->GetVehicle().ToDriveableVehicle())
		{
			GetCharacter()->EnableNavBlocker(true);

			if (exitType == DC::kCarExitTypeSkip)
			{
				Exit();
			}
			else
			{
				StringId64 preferredExit = INVALID_STRING_ID_64;

				switch (GetRole())
				{
				case DC::kCarRiderRoleDriver:
					preferredExit = SID("driver-side");
					break;
				case DC::kCarRiderRolePassengerShotgun:
					preferredExit = SID("passenger-side");
					break;
				case DC::kCarRiderRolePassengerBack:
					preferredExit = SID("back");
					break;

				}

				m_pVehicleCtrl->PlayExitAnimation(preferredExit);
			}
		}
		else
		{
			// temporary
			if (m_pVehicleCtrl->GetVehicle().ToDriveableBoat() && exitType != DC::kCarExitTypeSkip)
			{
				FadeToStateParams params;
				params.m_animFadeTime = 0.2f;
				GetCharacter()->GetAnimControl()->GetBaseStateLayer()->FadeToState(SID("s_idle"), params);
			}

			Exit();
		}

		if (INdAiWeaponController* pWeaponController = GetCharacter()->GetAnimationControllers()->GetWeaponController())
		{
			// Refresh gun state to switch hands if necessary
			pWeaponController->ForceGunState(pWeaponController->GetCurrentGunState(), true);
		}
	}

	// ------------------------------------------------------------------------------------------ //
	void QuickCompleteExit() override
	{
		if (!m_pVehicleCtrl->GetVehicle().HandleValid())
		{
			return;
		}

		const bool wasExiting = m_pVehicleCtrl->QuickCompleteExit();

		if (!wasExiting)
		{
			return;
		}

		Exit();
	}

	// ------------------------------------------------------------------------------------------ //
	bool LockGestures() const override
	{
		return m_pVehicleCtrl->LockGestures();
	}

	// ------------------------------------------------------------------------------------------ //
	bool AllowFlinches() const override
	{
		if (IsJumping() || IsLanding() || IsHangingOn())
		{
			return false;
		}

		return m_pVehicleCtrl->AllowFlinches();
	}

	virtual U64 CollectHitReactionStateFlags() const override;

	// ------------------------------------------------------------------------------------------ //
	virtual DC::CarRiderRole GetRole() const override
	{
		return m_pVehicleCtrl->Role();
	}

	// ------------------------------------------------------------------------------------------ //
	virtual VehicleHandle GetCar() const override
	{
		return m_pVehicleCtrl->GetVehicle();
	}

	virtual const IVehicleCtrl* GetDriveCtrl() const override
	{
		return m_pVehicleCtrl;
	}

	IVehicleCtrl* GetDriveCtrl() override
	{
		return m_pVehicleCtrl;
	}
};

NdAtomic64 AiVehicleController::s_apSimultaneousEntryFrame(-1);

/// --------------------------------------------------------------------------------------------------------------- ///
U64 AiVehicleController::CollectHitReactionStateFlags() const
{
	if (!m_pVehicleCtrl || !m_pVehicleCtrl->GetVehicle().HandleValid())
		return 0;

	U64 flags = 0;

	if (const IVehicle* pVehicle = GetCar().GetVehicleInterface())
	{
		const DC::Identifier vehicleType = pVehicle->GetVehicleType();

		switch (vehicleType.m_symbol.GetValue())
		{
		case SID_VAL("jeep"):			flags |= DC::kHitReactionStateMaskVehicleTypeJeep;			break;
		case SID_VAL("truck"):			flags |= DC::kHitReactionStateMaskVehicleTypeTruck;			break;
		case SID_VAL("bike"):			flags |= DC::kHitReactionStateMaskVehicleTypeBike;			break;
		case SID_VAL("turret-truck"):	flags |= DC::kHitReactionStateMaskVehicleTypeTurretTruck;	break;
		}
	}

	const DC::CarRiderRole role = GetRole();
	switch (role)
	{
	case DC::kCarRiderRoleDriver:			flags |= DC::kHitReactionStateMaskVehicleRoleDriver;			break;
	case DC::kCarRiderRolePassengerShotgun:	flags |= DC::kHitReactionStateMaskVehicleRolePassengerShotgun;	break;
	case DC::kCarRiderRolePassengerBack:	flags |= DC::kHitReactionStateMaskVehicleRolePassengerBack;		break;
	}

	if (IsReadyJump())
	{
		flags |= DC::kHitReactionStateMaskVehicleReadyJump;
	}
	else if (IsJumping())
	{
		flags |= DC::kHitReactionStateMaskVehicleJumping;
	}
	else if (IsLanding())
	{
		flags |= DC::kHitReactionStateMaskVehicleLanding;
	}
	else if (IsHangingOn())
	{
		flags |= DC::kHitReactionStateMaskVehicleHangingOn;
	}
	else
	{
		flags |= DC::kHitReactionStateMaskVehicleIdle;
	}

	if (IsAiming())
	{
		if (IsAimingBackwards())
		{
			flags |= DC::kHitReactionStateMaskVehicleAimingBack;
		}
		else
		{
			flags |= DC::kHitReactionStateMaskVehicleAimingFront;
		}
	}

	return flags;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiVehicleController* CreateAiVehicleController()
{
	return NDI_NEW AiVehicleController;
}

/// --------------------------------------------------------------------------------------------------------------- ///

SCRIPT_FUNC("npc-get-melee-hang-on-loc", DcNpcGetMeleeHangOnLoc)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (const IAiVehicleController* pVehicleController = pNpc->GetAnimationControllers()->GetVehicleController())
		{
			return ScriptValue(pVehicleController->GetHangOnLoc());
		}
	}

	return ScriptValue(DC::kCarMeleeHangOnLocNone);
}
