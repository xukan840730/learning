/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/system/recursive-atomic-lock.h"

#include "gamelib/gameplay/nd-drawable-object.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/ndphys/rigid-body.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimControl;
class AttachmentCtrl;
class HavokContactPointAddedEvent;
class ProcessSpawnInfo;
class RigidBody;
class SpawnInfo;
class hkpRigidBody;
class BgDrivenByFgLevelData;
class NdSimpleObject;
struct FgAnimData;
struct ResolvedBoundingData;
struct RigidBodyPlatformDetect;

FWD_DECL_PROCESS_HANDLE(NdSimpleObject);


/// --------------------------------------------------------------------------------------------------------------- ///
struct NdSimpleObjectOptions
{
	bool m_disableUpdateOptimization;
	bool m_disableAlignBindingFix;

	NdSimpleObjectOptions();
};

extern NdSimpleObjectOptions g_ndSimpleObjectOptions;

/// --------------------------------------------------------------------------------------------------------------- ///
struct NdSimpleObjectInfo
{
	NdSimpleObjectInfo()
		: m_magicCheck(0xD00DAD00)
		, m_pDrivenBgLevelData(nullptr)
		, m_drivenBgInstanceIndex(0)
		, m_bgScale(1.0f, 1.0f, 1.0f)
	{}

	void Validate() const;

	U32F m_magicCheck;

	// Used for fg process that is ment to drive bg mesh
	BgDrivenByFgLevelData* m_pDrivenBgLevelData;
	U32F m_drivenBgInstanceIndex;
	Vector m_bgScale;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AutoActorMoveMod
{
	struct Joint
	{
		StringId64 m_jointId;
		U64 m_pad;
		Locator m_locator;
		Vector m_scale;
	};
	U32 m_numJoints;
	U32 m_pad[3];
	Joint m_joint0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NdSimpleObject : public NdDrawableObject
{
private:
	typedef NdDrawableObject ParentClass;
public:

	FROM_PROCESS_DECLARE(NdSimpleObject);

	STATE_DECLARE_OVERRIDE(Active);

	NdSimpleObject()
		: m_pDrivenBgLevelData(nullptr)
		, m_drivenBgInstanceIndex(0)
		, m_hNextSoundOpening(nullptr)
		, m_nextSoundOpeningId(INVALID_STRING_ID_64)
		, m_flags(0)
		, m_horseBinding(MAYBE::kNothing)
	{
	}

	virtual ~NdSimpleObject() override {}

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void EventHandler(Event& event) override;
	virtual Err ConfigureAnimData(FgAnimData* pAnimData, const ResolvedBoundingData& bounds) override;
	virtual Err SetupAnimControl(AnimControl* pAnimControl) override;
	virtual Err PostInit(const ProcessSpawnInfo& spawn) override;
	virtual void SetupAttachSystem(const ProcessSpawnInfo& spawn) override;
	virtual Err InitializeDrawControl(const ProcessSpawnInfo& spawn, const ResolvedLook& resolvedLook) override;
	virtual void OnKillProcess() override;
	virtual const RigidBody* GetFrameForwardRigidBody() const;
	virtual void PostAnimUpdate_Async() override;
	virtual void PostJointUpdate_Async() override;
	virtual void PostJointUpdatePaused_Async() override;
	virtual bool MayBeInBindPose() const override;
	virtual float GetSoundOpeningPercentage() const { return 0.0f; }
	virtual float GetTotalSoundOpeningPercentage() const;
	virtual bool NeedsCollision(const SpawnInfo& spawn) const;
	virtual bool NeedsAttachmentCtrl(const SpawnInfo& spawn) const;

	virtual void PostAnimBlending_Async() override;

	virtual MeshRaycastResult* GetGroundMeshRaycastResult(MeshRaycastType type) override { return (type == kMeshRaycastGround) ? m_pGroundMeshRaycastResult : nullptr; }

	virtual I32	GetDynamicMaskType() const override { return m_dynamicMaskType; }
	virtual void SetDynamicMaskType(DynamicMaskType dynamicMaskType) { m_dynamicMaskType = dynamicMaskType; }

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual StringId64 GetLookId() const override;

	virtual bool DisableAnimation() override;
	virtual bool MeetsCriteriaToDebugRecord(bool& outRecordAsPlayer) const override;

	void SetHorseBinding(Maybe<DC::HorseBinding> newHorseBinding) { m_horseBinding = newHorseBinding; }
	void UpdateHorseBinding();

	static void FastAnimDataUpdate(FgAnimData* pAnimData, F32 dt);

	bool IsAttached() const;
	virtual bool AllowDebugRotate() const override { return true; }

	const BgDrivenByFgLevelData* GetBgDrivenByFgLevelData() const { return m_pDrivenBgLevelData; }

protected:
	void Detach();

	virtual void GetCollisionScaleInfo(F32& defaultScaleTol, bool& allowNonUniformScale);

	void ApplyMoveModToJointCache(const AutoActorMoveMod* pMoveMod);

	void DriveBgProto();

	inline const NdSimpleObject* GetNextSoundOpening() const
	{
		const NdSimpleObject* pNextOpening = nullptr;
		if (m_nextSoundOpeningId != INVALID_STRING_ID_64)
		{
			pNextOpening = m_hNextSoundOpening.ToProcess();
			if (pNextOpening == nullptr)
			{
				// Look up by spawner ID.
				pNextOpening = NdSimpleObject::FromProcess(NdGameObject::LookupGameObjectByUniqueId(m_nextSoundOpeningId));
				if (pNextOpening != nullptr)
				{
					m_hNextSoundOpening = pNextOpening;
				}
			}
		}

		return pNextOpening;
	}

	class CallbackContext : public RigidBody::CallbackContext
	{
	public:
		CallbackContext() : m_requestedCallbacks(0) {}
		virtual U8 GetRequestedCallbacks() const override { return m_requestedCallbacks; }
		virtual void OnAddContact(RigidBody& thisBody, const hknpBody& otherPhysicsBody, const HavokContactEvent& event) override;
		virtual bool OnChangeMotionType(RigidBody& body, RigidBodyMotionType newMotionType) override;
		virtual void OnTriggerEnter(RigidBody& thisBody, const hknpBodyId& otherBodyId) override;

		U8 m_requestedCallbacks;
	};
	CallbackContext m_callbackContext;

	mutable NdSimpleObjectHandle m_hNextSoundOpening;
	StringId64 m_nextSoundOpeningId;

	RigidBodyPlatformDetect* m_pPlatformDetect;

	union
	{
		U16 m_flags;
		struct {
			mutable U16 m_totalSoundOpeningActive : 1;
			U16 m_contactEvents : 1;
			U16 m_characterContactEvents : 1;
			U16 m_collisionTrigger : 1;
			U16 m_origAllowDisableAnimation : 1;
			U16 m_bDriveBgProto : 1;
			// Can process update and animation be disabled?
			U16 m_allowDisableUpdates : 1;
			// If m_allowDisableUpdates is false can at least the animation be disabled?
			U16 m_allowDisableAnimation : 1;
			U16 m_outputOnlySkinningMatrices : 1;

			// Some process types modify the m_allowDisableUpdates internally and we can inadvertently set m_allowDisableUpdates to
			// true when we really want it to be false (i.e. in the case where we want to receive correct spec cube maps).
			U16 m_forceSpecularCubemapUpdate : 1;

			U16 m_unused : 6;
		};
	};

	U8 m_numSimpleInstancesNeeded;

	DynamicMaskType m_dynamicMaskType;

	// This is used if we are a process (without geo) that is driving bg prototype
	BgDrivenByFgLevelData* m_pDrivenBgLevelData;
	U32F m_drivenBgInstanceIndex;

	const AutoActorMoveMod* m_pMoveMod;

	MeshRaycastResult*		m_pGroundMeshRaycastResult;

	static NdRecursiveAtomicLock64 s_soundOpeningLock;

	Maybe<DC::HorseBinding> m_horseBinding;

	PROCESS_IS_RAW_TYPE_DEFINE(NdSimpleObject);
};

PROCESS_DECLARE(NdSimpleObject);

/// --------------------------------------------------------------------------------------------------------------- ///
class NdSimpleObject::Active : public NdSimpleObject::ParentClass::Active
{
public:
	virtual ~Active() override {}
	typedef NdSimpleObject::ParentClass::Active ParentClass;
	BIND_TO_PROCESS(NdSimpleObject);

	void ResetAnimAction();
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NdSimpleObjectLarge : public NdSimpleObject
{
private:
	typedef NdSimpleObject ParentClass;
public:

	FROM_PROCESS_DECLARE(NdSimpleObjectLarge);

	NdSimpleObjectLarge() : m_maxNumDrivenInstances(0) { }

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual U32F GetMaxNumDrivenInstances() const override;

private:
	U32 m_maxNumDrivenInstances;
};

PROCESS_DECLARE(NdSimpleObjectLarge);
