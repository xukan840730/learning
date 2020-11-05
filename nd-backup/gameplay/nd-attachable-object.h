/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/game-types.h"

#include "gamelib/gameplay/nd-attachment-ctrl.h"
#include "gamelib/gameplay/nd-drawable-object.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/ndphys/havok-collision-cast.h"
#include "gamelib/ndphys/moving-platform-damper.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class Armor;
class HavokContactPointAddedEvent;
class HingeJiggle;
class ProcessSpawnInfo;
class RigidBody;
class SimpleJiggle;
class SpawnInfo;
class hkpRigidBody;

FWD_DECL_PROCESS_HANDLE(Character);

namespace DC
{
	struct Map;
	struct SymbolArray;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class NdAttachableObject : public NdDrawableObject
{
	typedef NdDrawableObject ParentClass;

public:
	FROM_PROCESS_DECLARE(NdAttachableObject);

	typedef void DetachCallback(NdAttachableObject*);

	NdAttachableObject();
	~NdAttachableObject() override;

	virtual Err Init(const ProcessSpawnInfo& info) override;
	virtual Err InitCollision(const ProcessSpawnInfo& info);
	void InitSoundEffects(float bounceThreshold, float timeout, StringId64 hardSound, StringId64 softSound);
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void EventHandler(Event& event) override;
	virtual void OnKillProcess() override;

	virtual void PostAnimUpdate_Async() override;
	virtual void PostAnimBlending_Async() override;
	virtual void PostJointUpdate_Async() override;

	void InheritVisibilityFromParent();
	virtual bool ShouldInheritVisibilityFromParent() const { return true; }

	virtual bool ShouldMaintainNearCameraStatus() const { return false; }

	virtual bool ShouldInheritHighContrastModeFromParent() const { return !m_dontInheritHcmt; }
	void SetShouldInheritHighContrastMode(bool allow) { m_dontInheritHcmt = !allow; }

	virtual U32F GetMaxStateAllocSize() override;

	StringId64 GetParentAttachName() const;

	Vector GetVelocity() const;
	virtual RigidBody* GetMainRigidBody() const;
	virtual I32 GetDynamicMaskType() const override { return kDynamicMaskTypeNone; }

	virtual NdGameObject* GetBoundPlatform() const override;

	void RequestDetach(F32 timeout = 0.0f,
					   Vector_arg detachVel	   = kZero,
					   Vector_arg detachAngVel = kZero,
					   bool overrideVelo	   = false,
					   bool immediate = false);

	void DetachButDontDrop(float fadeOutTime = 0.f) { m_pAttachmentCtrl->Detach(fadeOutTime); }

	// set parent object and its attach joint should be an atomic operation
	bool AttachTo(const char* sourceFile,
				  U32F sourceLine,
				  const char* sourceFunc,
				  MutableNdGameObjectHandle hParent,
				  StringId64 attachPointName,
				  const Locator& attachOffset = Locator(kIdentity));

	// keep parent object but change attach index.
	bool ChangeAttachPoint(const char* sourceFile,
						   U32F sourceLine,
						   const char* sourceFunc,
						   StringId64 attachPointName,
						   const Locator& attachOffset = Locator(kIdentity));

	bool AttachToBlend(const char* sourceFile,
					   U32F sourceLine,
					   const char* sourceFunc,
					   MutableNdGameObjectHandle hParent,
					   StringId64 attachPointName,
					   const Locator& desiredOffset,
					   float blendTime,
					   bool fadeOutVelWs = false);

	bool AttachToBlendIgnoringJiggle(const char* sourceFile,
					   U32F sourceLine,
					   const char* sourceFunc,
					   MutableNdGameObjectHandle hParent,
					   StringId64 attachPointName,
					   const Locator& desiredOffset,
					   float blendTime,
					   bool fadeOutVelWs = false);

	bool ChangeAttachPointBlend(const char* sourceFile,
								U32F sourceLine,
								const char* sourceFunc,
								StringId64 attachPointName,
								const Locator& desiredOffset,
								float blendTime,
								bool fadeOutVelWs = false);

	//-------------------------------------------------------------------------------------------//
	// world-space blending
	//-------------------------------------------------------------------------------------------//
	void FadeToAttach(const BoundFrame& initialLocWs, float blendTime) { m_pAttachmentCtrl->SetFadeToAttach(blendTime, initialLocWs); }

	TimeFrame GetDroppedTime() const						{ return m_droppedTime; }

	virtual bool ShouldShimmer() const						{ return true; }
	const DC::Map* GetAnimMap() const						{ return m_animMap; }

	void SetDetachCallback(DetachCallback* pDetachCallback) { m_pDetachCallback = pDetachCallback; }
	void SetDisableNetSnapshot(bool disable)				{ m_disableNetSnapshots = disable; }

	virtual const Armor* GetPropArmor() const				{ return nullptr;}
	virtual Armor* GetPropArmor()							{ return nullptr;}

	void UpdateTimers();

	virtual void RegisterInListenModeMgr(U32 addFlags = 0, float fadeTime = 0.0f) override;
	virtual void ApplyOverlayInstanceTextures() override;

	AttachIndex LookupAttachIndex(StringId64 attachId) const;

	virtual void OnUserIdChange(StringId64 oldUserFullId, StringId64 newUserFullId) override;

	virtual Err	SetupPlatformControl(const SpawnInfo& spawn) override { return Err::kOK; }

	void SetSmallLayerWhenDropped(bool bSmall);
	bool GetSmallLayerWhenDropped() const { return m_smallLayerWhenDropped; }

	void NeverPhysicalize() { m_neverPhysicalize = true; }

	virtual void PlayIdleAnim();

	void SetJiggleOn(bool b, F32 blendTime = 0.0f); // for user (script etc)
	void SetJiggleDisabledF(F32 blendTime = 0.0f); // for user (script etc) to be called every frame
	virtual bool IsJiggleAllowed(F32& blendOutTime) const { return true; } // allowed by our internal logic?

	virtual bool MeetsCriteriaToDebugRecord(bool& outRecordAsPlayer) const override;

	static const char* HolsterPointToString(const StringId64 attachPointName);

	void SetNoIdleOnAnimEnd(bool f) { m_noIdleOnAnimEnd = f; }

	virtual F32 GetCharacterSoftStepMultiplier(const RigidBody* pCollidingBody) const override;


protected:
	STATE_DECLARE_OVERRIDE(Active);
	STATE_DECLARE(Animating);
	STATE_DECLARE(Dropped);
	STATE_DECLARE(NetDropped);

	// should be private, but ragdolls want to screw with us...
	virtual void UpdateRoot(bool postCameraAdjust = false);

	void SetParentObjectAndAttachIndex(const char* sourceFile,
									   U32F sourceLine,
									   const char* sourceFunc,
									   NdGameObject* pParent,
									   AttachIndex parentAttachIndex,
									   const Locator& attachOffset);

	//-------------------------------------------------------------------------------------------//
	// attach-offset
	//-------------------------------------------------------------------------------------------//
	void SetParentAttachOffset(const Locator& offset) { m_pAttachmentCtrl->SetParentAttachOffset(offset); }

	virtual bool HandlePlayAnimEvent(Event& event) override;

	void ParentClearAttachable();
	void ParentUpdateAttachable();

	virtual void GetInitialPhysicalizeVelocity(Vector& linearVel, Vector& angularVel) const;
	virtual bool UseTwoStageAnimation(const ProcessSpawnInfo& spawn) const;

	virtual bool ShouldCreateNetSnapshotController(const SpawnInfo& spawn) const override { return true; }
	virtual bool ManualLocatorUpdate() const override { return true; }

	virtual void GetDropDepenetrateInfo(bool& doDepenetrate, Point& refPos) { doDepenetrate = false; }

	virtual void OnAttach() override;
	virtual void OnDetach() override;

	bool ComputeDesiredLocatorWs(Locator& locWsOut, bool dontAdvance = false);

	virtual bool AllowLodCull() const { return true; }

	void UpdateVisualsFromParent();
	void CommonPostJointUpdate();

	virtual void FastProcessUpdate(F32 dt) override;

	struct SoundInfo
	{
		float m_bounceThreshold = 0.0f;
		float m_timeout = 0.0f;
		StringId64 m_hardSurfaceSound = INVALID_STRING_ID_64;
		StringId64 m_softSurfaceSound = INVALID_STRING_ID_64;

		TimeFrame m_startedMovingTime = TimeFrameNegInfinity();
		TimeFrame m_sfxTime = TimeFrameNegInfinity();
		float m_gain = 0.0f;
		bool m_playSound = false;
	};

protected:
	BoundFrame m_droppedPos;
	StringId64 m_droppedParent;
	Vector m_oldVelocity;
	Vector m_detachVelocity;
	Vector m_detachAngVelocity;
	bool m_overrideDetachVelocity;
	TimeFrame m_droppedTime;
	NdGameObjectHandle m_parentGameObjectThatDroppedMe;
	ScriptPointer<DC::Map> m_animMap;

	F32 m_jiggleBlend;

	// This is for user to externally disable jiggle during igc etc
	F32 m_jiggleBlendTime;
	bool m_jiggleOn : 1;

	// This for another way for user to externally disable jiggle and this has to be set every frame
	bool m_jiggleDisabledF : 1;
	bool m_jiggleDisabledPrevF : 1;
	F32 m_jiggleBlendTimeF;

	bool m_enableAttachLocatorUpdate;

	F32 m_detachTimeout;

	StringId64 m_mainBodyJoint = INVALID_STRING_ID_64;

	DetachCallback* m_pDetachCallback;

	TimeFrame m_idleEnterTime;

	SoundInfo m_soundInfo;

	bool m_fakeAttached : 1;
	bool m_findCollisionLevel : 1;
	bool m_detachRequested : 1;
	bool m_noIdleOnAnimEnd : 1;
	bool m_physicalizeRequested : 1;
	bool m_neverPhysicalize : 1; // ignore any physicalize request
	bool m_dontUseDamper : 1;
	bool m_isNameValid : 1;
	bool m_hidden : 1;
	bool m_disableNetSnapshots : 1;
	bool m_smallLayerWhenDropped : 1;
	bool m_dontInheritHcmt : 1;
	bool m_simpleAttachableAllowDisableAnimation : 1; // this is not a well integrated flag. Trying to stay safe just before shipping t2


	class Damper : public MovingPlatformDamper
	{
	public:
		virtual void OnAddContact(RigidBody& thisBody,
								  const hknpBody& otherPhysicsBody,
								  const HavokContactEvent& event) override;
	};

	Damper m_damper;
	RigidBodyHandle m_hLastHitBody;
	HavokShapeCastJob m_dropDepenetrateJob;

	TimeFrame m_nextNetUpdate;

	SimpleJiggle* m_pJiggle;

	PROCESS_IS_RAW_TYPE_DEFINE(NdAttachableObject);
};

PROCESS_DECLARE(NdAttachableObject);

/// --------------------------------------------------------------------------------------------------------------- ///
struct NdAttachableInfo
{
	Locator m_loc = kIdentity;
	Locator m_attachOffset = kIdentity;				// Offset from the attach point - adds to the offset from the parent joint in the attach point
	MutableNdGameObjectHandle m_hParentProcessGo;
	StringId64 m_artGroupId = INVALID_STRING_ID_64;
	StringId64 m_userBareId = INVALID_STRING_ID_64;				// Optional user id - Useful if manually spawning an attachable object
	StringId64 m_userNamespaceId = INVALID_STRING_ID_64;		// Optional user namespace id - Useful if manually spawning an attachable object
	StringId64 m_userId = INVALID_STRING_ID_64;			// Optional user full name id - Useful if manually spawning an attachable object
	StringId64 m_parentAttach = INVALID_STRING_ID_64;			// Attach point id - Not used if m_parentAttachIndex is valid
	StringId64 m_attachJoint = INVALID_STRING_ID_64;				// Joint on the attachable object
	StringId64 m_animMap = INVALID_STRING_ID_64;
	StringId64 m_customAttachPoints = INVALID_STRING_ID_64;

	const char* m_jointSuffix = nullptr;
	const DC::BoxedKvPair** m_apVarArgs = nullptr; // [SpawnInfo::kVarArgCapacity]

	AttachIndex m_parentAttachIndex = AttachIndex::kInvalid;	// Index of the attach point on the parent - Preferred over m_parentAttach if valid

	bool m_netDropped = false;
	bool m_fakeAttached = false;
	bool m_skipPlayerFlashlight = false;
	bool m_isGoreHeadProp = false;
	bool m_needs2StageAnimation = false;		// you'll need this for example if you have another attachable attached to this attachable :)
	bool m_useShaderInstanceParams = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NdAttachableObject::Active : public NdDrawableObject::Active
{
public:
	BIND_TO_PROCESS(NdAttachableObject);
	virtual ~Active() override {}
	virtual void Update() override;
	virtual void EventHandler(Event&) override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NdAttachableObject::Animating : public NdDrawableObject::Active
{
public:
	BIND_TO_PROCESS(NdAttachableObject);
	virtual ~Animating() override {}
	virtual void Enter() override;
	virtual void Update() override;
	virtual void Exit() override;
	virtual void EventHandler(Event&) override;

	AnimSimpleInstance::ID m_instanceId;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// Attachable has been dropped and is now a physics object.
class NdAttachableObject::Dropped : public NdDrawableObject::Active
{
	BIND_TO_PROCESS(NdAttachableObject);
public:
	virtual ~Dropped() override {}
	virtual void EventHandler(Event&) override;
	virtual void Enter() override;
	virtual void Update() override;
	virtual void Exit() override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// Attachable has been dropped by another player in multiplayer and they're updating its position
class NdAttachableObject::NetDropped : public NdDrawableObject::Active
{
	BIND_TO_PROCESS(NdAttachableObject);
public:
	virtual ~NetDropped() override {}
	virtual void EventHandler(Event& event) override;
	virtual void Enter() override;
	virtual void Update() override;
	virtual void Exit() override;
private:
	TimeFrame m_enterTime;
	bool m_netForceHidden;
};

FWD_DECL_PROCESS_HANDLE(NdAttachableObject);

struct ResolvedLookSeed;

NdAttachableObject* CreateAttachable(const NdAttachableInfo& info,
									 const ResolvedLookSeed* pParentLookSeed = nullptr,
									 const DC::SymbolArray* pTintArray		 = nullptr,
									 Process* pParent = nullptr);
NdAttachableObject* CreateAttachable(const NdAttachableInfo& info,
									 StringId64 procID,
									 const ResolvedLookSeed* pParentLookSeed = nullptr,
									 const DC::SymbolArray* pTintArray		 = nullptr,
									 Process* pParent = nullptr);

bool IsItemInHand(NdAttachableObjectHandle hItem, CharacterHandle hOwner);

extern bool g_drawAttachableObjectParent;
