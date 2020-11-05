
/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nd-simple-object.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/fx/fxmgr.h"
#include "ndlib/ndphys/havok-group-filter.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/net/nd-net-controller.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/interface/bg-geometry.h"

#include "gamelib/fx/splashers.h"
#include "gamelib/gameplay/faction-mgr.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nd-attachment-ctrl.h"
#include "gamelib/gameplay/nd-interactable.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-skin-manager.h"
#include "gamelib/gameplay/spline-tracker.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/havok.h"
#include "gamelib/ndphys/rigid-body-platform-detect.h"
#include "gamelib/state-script/ss-animate.h"

#include <Eigen/Dense>

/// --------------------------------------------------------------------------------------------------------------- ///
NdSimpleObjectOptions g_ndSimpleObjectOptions;
/* static */ NdRecursiveAtomicLock64 NdSimpleObject::s_soundOpeningLock;

/// --------------------------------------------------------------------------------------------------------------- ///
NdSimpleObjectOptions::NdSimpleObjectOptions() :
	m_disableUpdateOptimization(false),
	m_disableAlignBindingFix(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObjectInfo::Validate() const
{
	GAMEPLAY_ASSERTF(m_magicCheck == 0xD00DAD00,
					 ("Somebody sent a spawner user info to NdSimpleObject that appears not to be derived from NdSimpleObjectInfo"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
PROCESS_REGISTER_ALLOC_SIZE(NdSimpleObjectLarge, NdSimpleObject, 256 * 1024);

FROM_PROCESS_DEFINE(NdSimpleObjectLarge);

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdSimpleObjectLarge::Init(const ProcessSpawnInfo& spawnInfo)
{
	const SpawnInfo& spawn = static_cast<const SpawnInfo&>(spawnInfo);
	m_maxNumDrivenInstances = (U32)Max(spawn.GetData<I32>(SID("max-num-driven-instances"), 0), 0);

	return ParentClass::Init(spawnInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NdSimpleObjectLarge::GetMaxNumDrivenInstances() const
{
	if (m_maxNumDrivenInstances > 0)
		return m_maxNumDrivenInstances;
	else
		return ParentClass::GetMaxNumDrivenInstances();
}

/// --------------------------------------------------------------------------------------------------------------- ///
PROCESS_REGISTER(NdSimpleObject, NdDrawableObject);

FROM_PROCESS_DEFINE(NdSimpleObject);

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdSimpleObject::Init(const ProcessSpawnInfo& spawnInfo)
{
	PROFILE(Processes, NdSimpleObject_Init);

	m_hNextSoundOpening = nullptr;
	m_pAttachmentCtrl = nullptr;

	const SpawnInfo& spawn = static_cast<const SpawnInfo&>(spawnInfo);

	m_numSimpleInstancesNeeded = MinMax(spawn.GetData(SID("num-blends-needed"), 0), 0, 5);
	m_outputOnlySkinningMatrices = spawn.GetData(SID("only-skinning-matrices"), 0);
	m_contactEvents = spawn.GetData<bool>(SID("collision-contact-events"), false);
	m_characterContactEvents = spawn.GetData<bool>(SID("character-collision-contact-events"), false);
	m_collisionTrigger = spawn.GetData<bool>(SID("collision-trigger"), false);
	m_allowDisableUpdates = spawn.GetData<bool>(SID("allow-disable-updates"), true);
	m_forceSpecularCubemapUpdate = !m_allowDisableUpdates;
	m_allowDisableAnimation = spawn.GetData<bool>(SID("allow-disable-animation"), false);
	m_nextSoundOpeningId = spawn.GetData(SID("next-sound-opening"), INVALID_STRING_ID_64);

	m_dynamicMaskType = kDynamicMaskTypeNone;
	m_pPlatformDetect = nullptr;

	const NdSimpleObjectInfo* pInfo = reinterpret_cast<const NdSimpleObjectInfo*>(spawn.m_pUserData);
	if (pInfo)
	{
		pInfo->Validate();
		if (pInfo->m_pDrivenBgLevelData)
		{
			m_pDrivenBgLevelData = pInfo->m_pDrivenBgLevelData;
			m_drivenBgInstanceIndex = pInfo->m_drivenBgInstanceIndex;
			m_gameObjectConfig.m_drawControlMaxMeshes = 0;
		}
	}

	Err err = ParentClass::Init(spawn);
	if (err.Failed())
	{
		return err;
	}

	if (m_pDrivenBgLevelData)
	{
		if (GetAnimData()->m_jointCache.GetNumTotalJoints() != m_pDrivenBgLevelData->GetInstanceNumBones(m_drivenBgInstanceIndex))
		{
			MsgErr("Mismatch in num of bones between bg prototype (%i) and fg actor %s (%i)\n", m_pDrivenBgLevelData->GetInstanceNumBones(m_drivenBgInstanceIndex), DevKitOnly_StringIdToString(spawn.m_artGroupId), GetAnimData()->m_jointCache.GetNumTotalJoints());
			return Err::kErrBadData;
		}
	}

	if (NeedsCollision(spawn))
	{
		PROFILE(Processes, CollisionSetup);

		CompositeBodyInitInfo info;
		info.m_initialMotionType = kRigidBodyMotionTypeAuto;
		if (spawn.GetData<bool>(SID("spawn-non-physical"), false))
			info.m_initialMotionType = kRigidBodyMotionTypeNonPhysical;
		if (spawn.GetData<bool>(SID("spawn-fixed"), false))
			info.m_initialMotionType = kRigidBodyMotionTypeFixed;
		if (spawn.GetData<bool>(SID("spawn-game-driven"), false))
			info.m_initialMotionType = kRigidBodyMotionTypeGameDriven;
		else if (spawn.GetData<bool>(SID("spawn-physics-driven"), false))
			info.m_initialMotionType = kRigidBodyMotionTypePhysicsDriven;

		if (GetSpawner() == nullptr)
			info.m_collisionArtItemId = spawn.GetArtGroupId(INVALID_STRING_ID_64);

		if (GetSpawner() && GetSpawner()->GetSpawnNonPhysical())
			info.m_initialMotionType = kRigidBodyMotionTypeNonPhysical;

		// For now this has to be opt-in
		info.m_bLookForCollisionInSkeleton = spawn.GetData<bool>(SID("allow-collision-in-skeleton"), false);

		// Allow this for simple objects now
		info.m_bAllowAutoScale = true;
		info.m_bAllowNonUniformScale = true;

		InitCompositeBody(&info);

		CompositeBody* pCompositeBody = GetCompositeBody();
		if (pCompositeBody)
		{
			if (!spawn.GetData<bool>(SID("no-ai-cover"), false))
			{
				RegenerateCoverActionPacks();
			}

			// @@JS hk2016
			// Setup any possible breakable structural groups or covers
			//SetupBreakableStructuralAndCover(pCompositeBody);

			if (spawn.GetData(SID("Shootable"), false))
			{
				SetTurnReticleRed(true);
			}

			if (spawn.GetData(SID("SmallCollision"), false))
			{
				pCompositeBody->SetLayer(Collide::kLayerSmall);
			}
			else if (!spawn.GetData(SID("driveable-collision"), true))
			{
				pCompositeBody->SetLayer(Collide::kLayerBigNotDriveable);
			}
			else
			{
				pCompositeBody->SetLayer(Collide::kLayerGameObject);
			}

			if (spawn.GetData(SID("arrows-not-thru"), false))
			{
				PHYSICS_ASSERT(false);
			}

			if (spawn.GetData(SID("NoSoftStepResponse"), false) ||
				spawn.GetData(SID("NoCharacterCollisionResponse"), false))
			{
				pCompositeBody->SetSoftStepMassFactor(0.0f);
			}
			else
			{
				F32 f = spawn.GetData(SID("SoftStepMassFactor"), -1.0f);
				if (f >= 0.0f)
				{
					if (f < 0.000001f)
						f = 1000000.0f;
					else
						f = 1.0f / f;
					pCompositeBody->SetUseSoftStepResposne(true);
					pCompositeBody->SetSoftStepMassFactor(f);
				}
			}

			bool spawnActive = false;
			if (spawn.GetData(SID("PhysicsNeverSleep"), false))
			{
				spawnActive = true;
				for (U32F iBody = 0; iBody < pCompositeBody->GetNumBodies(); ++iBody)
				{
					RigidBody* pBody = pCompositeBody->GetBody(iBody);
					pBody->SetDeactivationEnabled(false);
				}
			}

			if (spawnActive || spawn.GetData(SID("SpawnPhysAwake"), false))
			{
				for (U32F iBody = 0; iBody < pCompositeBody->GetNumBodies(); ++iBody)
				{
					RigidBody* pBody = pCompositeBody->GetBody(iBody);
					ASSERT(pBody);
					if (pBody->GetMotionType() == kRigidBodyMotionTypePhysicsDriven)
						pBody->Activate();
				}
			}

			if (spawn.GetData(SID("RunPhysWhenAwake"), false))
			{
				for (U32F i = 0; i < pCompositeBody->GetNumBodies(); i++)
				{
					RigidBody* pBody = pCompositeBody->GetBody(i);
					pBody->SetDontSimTillTouched(false);
				}
			}

			if (spawn.GetData(SID("DebrisPlatformDetect"), false))
			{
				m_pPlatformDetect = NDI_NEW RigidBodyPlatformDetect[pCompositeBody->GetNumMaxBodies()];
				for (U32F i = 0; i<pCompositeBody->GetNumBodies(); i++)
				{
					if (pCompositeBody->GetBody(i)->GetMotionType() == kRigidBodyMotionTypePhysicsDriven)
					{
						m_pPlatformDetect[i].Install(pCompositeBody->GetBody(i));
					}
				}
			}

			if (spawn.GetData(SID("AttachPhysicsWorld"), false))
			{
				ALWAYS_ASSERT(pCompositeBody->GetNumBodies() == 1);
				HavokSetWorldSpaceBody(pCompositeBody->GetBody(0));
			}

			if (spawn.GetData(SID("DisableDestruction"), false))
			{
				pCompositeBody->SetDestructionEnabled(false, -1);
			}

			// These are duplicates, should be removed after U3
			if (spawn.GetData(SID("DisableDestructionForContacts"), false))
			{
				pCompositeBody->SetDestructionEnabledForContacts(false, -1);
			}

			if (spawn.GetData(SID("reporting-while-keyframed"), false))
			{
				pCompositeBody->SetReportingWhenKeyframed(true);
			}

			if (!spawn.GetData(SID("physics-predict-parent-movement"), true))
			{
				pCompositeBody->SetPredictParentMovement(false);
			}

			if (m_collisionTrigger)
			{
				pCompositeBody->SetLayer(Collide::kLayerSmallReporting);
			}

			// Install callback context if needed
			m_callbackContext.m_requestedCallbacks = (m_pPlatformDetect ? RigidBody::CallbackContext::kChangeMotion : 0)
				| ((m_contactEvents || m_characterContactEvents)
					? RigidBody::CallbackContext::kAddContact
					: 0)
				| (m_collisionTrigger ? RigidBody::CallbackContext::kTriggerEnter
					: 0);
			if (m_callbackContext.m_requestedCallbacks)
			{
				pCompositeBody->SetCallbackContext(&m_callbackContext);
			}
		}
	}

	// Apply auto actor move mode
	m_pMoveMod = spawn.GetDataPointer<AutoActorMoveMod>(SID("MoveModData"));
	if (m_pMoveMod)
	{
		ApplyMoveModToJointCache(m_pMoveMod);
		if (CompositeBody* pCompositeBody = GetCompositeBody())
		{
			const JointCache* pJointCache = &m_pAnimData->m_jointCache;
			for (U32F ii = 0; ii < pCompositeBody->GetNumBodies(); ii++)
			{
				RigidBody* pBody = pCompositeBody->GetBody(ii);
				if (!pBody->IsBreakableNonPhysical())
				{
					I32 iJoint = pBody->GetJointIndex();
					if (iJoint >= 0)
					{
						if (AllComponentsEqual(pJointCache->GetJointParamsLs(iJoint).m_scale, Vector(kZero)))
						{
							pBody->Kill();
						}
						else if (pBody->GetMotionType() == kRigidBodyMotionTypePhysicsDriven
							|| pBody->GetMotionType() == kRigidBodyMotionTypeFixed)
						{
							pBody->SetLocator(pJointCache->GetJointLocatorWs(iJoint));
						}
					}
				}
			}
			pCompositeBody->BakeAllWorldConstraintsFromCurrentBodyPosition();
		}
	}

	const StringId64 factionSid = spawn.GetData<StringId64>(SID("faction"), INVALID_STRING_ID_64);

	if (factionSid != INVALID_STRING_ID_64)
	{
		const FactionId factionId = g_factionMgr.LookupFactionByNameId(factionSid);
		if (factionId == FactionId::kInvalid)
		{
			GoError("NdSimpleObject '%s' has invalid faction ID '%s', check case (should be CamelCase)",
					DevKitOnly_StringIdToString(GetUserId()),
					DevKitOnly_StringIdToString(factionSid));
			return Err::kErrBadData;
		}

		SetFactionId(factionId);
	}

	if (DynamicNavBlocker* pNavBlocker = GetNavBlocker())
	{
		pNavBlocker->SetFactionId(GetFactionId().GetRawFactionIndex());
	}

	// This is for platform objects that actually need to be in the default bucket because they are playing off an APRef of an actual platform bucket object
	// i.e. airstrip convoy level
	if (spawn.GetData(SID("force-default-bucket"), false))
	{
		ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pDefaultTree);
	}

	SetAllowThreadedUpdate(true);

	if (NeedsAttachmentCtrl(spawn))
	{
		m_pAttachmentCtrl = NDI_NEW AttachmentCtrl(*this);

		const StringId64 parentObjectId		   = spawn.GetData<StringId64>(SID("parent-object"), INVALID_STRING_ID_64);
		const StringId64 parentJointOrAttachId = spawn.GetData<StringId64>(SID("parent-joint-or-attach-id"),
																		   INVALID_STRING_ID_64);

		if (parentObjectId != INVALID_STRING_ID_64)
		{
			m_pAttachmentCtrl->AttachToObject(FILE_LINE_FUNC, parentObjectId, parentJointOrAttachId);
		}
	}

	m_pGroundMeshRaycastResult = nullptr;
	if (GetSplasherController() && GetSplasherController()->NeedsSurfaceProbe())
		m_pGroundMeshRaycastResult = NDI_NEW MeshRaycastResult;

	m_origAllowDisableAnimation = m_allowDisableAnimation;

	m_bDriveBgProto = true;


	if (m_pInteractCtrl && m_pInteractCtrl->HasMissingHighContrastModeTargets())
	{
		m_allowDisableUpdates = false;
	}

	return err;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::EventHandler(Event& event)
{
	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("set-attached"):		
		if ((event.GetNumParams() >= 2) && m_pAttachmentCtrl)
		{
			m_pAttachmentCtrl->HandleAttachEvent(event);

			if (event.GetNumParams() >= 3)
			{
				if (m_allowDisableAnimation)
				{
					m_allowDisableAnimation = false;
					EnableAnimation();
				}

				if (m_allowDisableUpdates)
				{
					EnableUpdates();
				}
			}

			event.SetResponse(true);
		}
		break;

	case SID_VAL("detach"):
	case SID_VAL("detach-without-physicalizing"):
		Detach();
		event.SetResponse(true);
		break;
	}

	ParentClass::EventHandler(event);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::PostAnimBlending_Async()
{
	if (m_pAttachmentCtrl && m_pAttachmentCtrl->IsAttached())
	{
		Locator alignWs = GetLocator();

		if (m_pAttachmentCtrl->DetermineAlignWs(&alignWs))
		{
			SetLocator(alignWs);
			m_pAnimData->SetXform(alignWs); // if there is any system that looks at the joint cache during post anim blending (like physics) this is needed
		}
	}

	if (m_pMoveMod)
	{
		if (IsSimpleAnimating())
		{
			// Once we animate, we forget about the movemod
			m_pMoveMod = nullptr;
		}
		else
		{
			ApplyMoveModToJointCache(m_pMoveMod);
		}
	}

	if (m_pGroundMeshRaycastResult)
		KickGroundSurfaceTypeProbe(kMeshRaycastGround);

	ParentClass::PostAnimBlending_Async();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::Detach()
{
	if (m_pAttachmentCtrl)
	{
		m_pAttachmentCtrl->Detach();
		m_allowDisableAnimation = m_origAllowDisableAnimation;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSimpleObject::NeedsCollision(const SpawnInfo& spawn) const
{
	return !spawn.GetData<bool>(SID("no-collision"), false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSimpleObject::NeedsAttachmentCtrl(const SpawnInfo& spawn) const
{
	return spawn.GetData<bool>(SID("enable-attaching"), false)
		   || spawn.GetData<StringId64>(SID("parent-object"), INVALID_STRING_ID_64) != INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdSimpleObject::ConfigureAnimData(FgAnimData* pAnimData, const ResolvedBoundingData& bounds)
{
	Err result = ParentClass::ConfigureAnimData(pAnimData, bounds);
	if (result.Failed())
		return result;

	if (m_outputOnlySkinningMatrices)
	{
		pAnimData->SetAnimSourceMode(0, FgAnimData::kAnimSourceModeClipData);
		pAnimData->SetAnimSourceMode(1, FgAnimData::kAnimSourceModeNone);
		pAnimData->SetAnimResultMode(0, FgAnimData::kAnimResultSkinningMatricesOs);
		pAnimData->SetAnimResultMode(1, FgAnimData::kAnimResultNone);
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdSimpleObject::SetupAnimControl(AnimControl* pAnimControl)
{
	AnimControl* pMasterAnimControl = GetAnimControl();
	FgAnimData& animData = pMasterAnimControl->GetAnimData();
	const I32 foundAnimations = ResourceTable::GetNumAnimationsForSkelId(animData.m_curSkelHandle.ToArtItem()->m_skelId);
	const U32F numNeededSimpleLayerInstances = Max(((foundAnimations <= 1) ? 1u : 2u), (U32)m_numSimpleInstancesNeeded);

	pAnimControl->AllocateSimpleLayer(SID("base"), ndanim::kBlendSlerp, 0, numNeededSimpleLayerInstances);

	char layerNameBuf[32];
	for (U32F i = 0; i < m_numAnimateLayers; ++i)
	{
		sprintf(layerNameBuf, "partial-slerp-%d", i);
		pAnimControl->AllocateSimpleLayer(StringToStringId64(layerNameBuf), ndanim::kBlendSlerp, 2, 1);

		sprintf(layerNameBuf, "partial-add-%d", i);
		pAnimControl->AllocateSimpleLayer(StringToStringId64(layerNameBuf), ndanim::kBlendAdditive, 2, 1);
	}

	AnimLayer* pBaseLayer = pAnimControl->CreateSimpleLayer(SID("base"));
	pBaseLayer->Fade(1.0, 0.0f);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::GetCollisionScaleInfo(F32& defaultScaleTol, bool& allowNonUniformScale)
{
	defaultScaleTol = 0.0f;
	allowNonUniformScale = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdSimpleObject::InitializeDrawControl(const ProcessSpawnInfo& spawn, const ResolvedLook& resolvedLook)
{
	const NdSimpleObjectInfo* pInfo = reinterpret_cast<const NdSimpleObjectInfo*>(spawn.m_pUserData);
	if (pInfo)
	{
		pInfo->Validate();
		if (pInfo->m_pDrivenBgLevelData)
		{
			F32 defScaleTol;
			bool allowNonUniformScale;
			GetCollisionScaleInfo(defScaleTol, allowNonUniformScale);

			// Check scale
			const SpawnInfo& gameSpawn = static_cast<const SpawnInfo&>(spawn);
			StringId64 artGroupId = gameSpawn.GetArtGroupId(INVALID_STRING_ID_64);
			if (const ArtItemCollision* pCollArtItem = ResourceTable::LookupCollision(artGroupId).ToArtItem())
			{
				Vector scaleTol = CompositeBody::GetScaleTolerance(pCollArtItem, defScaleTol);

				// In case the bg proto has a scale we will match it on this process within our collision tolerance
				Vector bucketedScale = pInfo->m_bgScale;
				if (!allowNonUniformScale)
				{
					bucketedScale = Vector(0.333f * (bucketedScale.X() + bucketedScale.Y() + bucketedScale.Z()));
				}
				CompositeBody::GetScaleBucket(scaleTol, bucketedScale);
				SetScale(bucketedScale);

				VF32 bgScaleNotAllowed = Simd::Select(pInfo->m_bgScale.QuadwordValue(), Simd::GetVecAllOne(), Simd::CompareGT(scaleTol.QuadwordValue(), Simd::GetVecAllZero()));
				if (MaxComp(Abs(Vector(bgScaleNotAllowed) - Vector(1.0f, 1.0f, 1.0f))) > 0.01f)
				{
					StringBuilder<140> scaleErrorMsg("Bg Proto/collision scale mismatch");
					SpawnCollisionScaleError(this, scaleErrorMsg.c_str());
				}
			}
			else
			{
				SetScale(pInfo->m_bgScale);
			}

			// No drawable mesh, we'll be driving bg proto
			m_pAnimData->m_flags |= FgAnimData::kDisableBSphereCompute; // also no need for BS
			return Err::kOK;
		}
	}

	return ParentClass::InitializeDrawControl(spawn, resolvedLook);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::CallbackContext::OnAddContact(RigidBody& thisBody,
												   const hknpBody& otherPhysicsBody,
												   const HavokContactEvent& event)
{
	NdSimpleObject* pSelf = PunPtr<NdSimpleObject*>(thisBody.GetOwnerUnsafe());
	U32F otherLayer = HavokGroupFilter::getLayerFromFilterInfo(otherPhysicsBody.m_collisionFilterInfo);
	if (pSelf
		&& (pSelf->m_contactEvents
			|| (pSelf->m_characterContactEvents && Collide::IsLayerInMask(otherLayer, Collide::kLayerMaskCharacter))))
	{
		NdGameObject* pGo = nullptr;
		if (const RigidBody* pBody = RigidBody::HavokBodyToGameBody(otherPhysicsBody.m_id))
			pGo = pBody->GetOwnerUnsafe();

		SendEvent(SID("collision-contact"),
				  pSelf,
				  thisBody.GetJointSid(),
				  (pGo ? pGo->GetUserId() : INVALID_STRING_ID_64),
				  (pGo ? pGo->GetTypeNameId() : INVALID_STRING_ID_64),
				  (pGo ? pGo->GetLookId() : INVALID_STRING_ID_64));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::CallbackContext::OnTriggerEnter(RigidBody& thisBody, const hknpBodyId& otherBodyId)
{
	// This is similar as collision-contact event based off contact callback above but this one is hooked up to havok's trigger material/modifier tech
	// This should be more precise, allows for enter/exit detection but can only be used on body that is not expected to collide in normal way

	if (NdSimpleObject* pSelf = PunPtr<NdSimpleObject*>(thisBody.GetOwnerUnsafe()))
	{
		NdGameObject* pGo = nullptr;
		if (const RigidBody* pBody = RigidBody::HavokBodyToGameBody(otherBodyId))
			pGo = pBody->GetOwnerUnsafe();

		if (pGo != pSelf)
		{
			SendEvent(SID("collision-trigger-enter"),
				pSelf,
				thisBody.GetJointSid(),
				(pGo ? pGo->GetUserId() : INVALID_STRING_ID_64),
				(pGo ? pGo->GetTypeNameId() : INVALID_STRING_ID_64),
				(pGo ? pGo->GetLookId() : INVALID_STRING_ID_64));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSimpleObject::CallbackContext::OnChangeMotionType(RigidBody& body, RigidBodyMotionType newMotionType)
{
	NdSimpleObject* pSelf = PunPtr<NdSimpleObject*>(body.GetOwner());
	if (pSelf)
	{
		ASSERT(body.GetMotionType() != newMotionType);
		if (newMotionType == kRigidBodyMotionTypePhysicsDriven)
		{
			if (pSelf->m_pPlatformDetect)
			{
				U32 bodyIndex = pSelf->GetCompositeBody()->FindBodyIndex(&body);
				ASSERT(bodyIndex != CompositeBody::kInvalidBodyIndex);
				if (newMotionType == kRigidBodyMotionTypePhysicsDriven)
				{
					pSelf->m_pPlatformDetect[bodyIndex].Install(&body);
				}
			}
		}
	}
	// Always permit the motion type change.
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdSimpleObject::PostInit(const ProcessSpawnInfo& spawn)
{
	PROFILE(Processes, NdSimpleObject_PostInit);

	StringId64 animId = spawn.GetData<StringId64>(SID("initial-animation"), INVALID_STRING_ID_64);
	if (animId == INVALID_STRING_ID_64)
	{
		animId = SID("idle");
	}

	if (m_pSsAnimateCtrl)
	{
		for (int i = 0; i < GetNumAnimateLayers(); ++i)
			m_pSsAnimateCtrl[i]->ResetAnimAction();
	}

	// Play initial animation.
	AnimControl* pAnimCtrl = GetAnimControl();
	if (pAnimCtrl)
	{
		//const ArtItemAnim* pAnim = pAnimCtrl->LookupAnim(animId);

		AnimSimpleLayer* pLayer = pAnimCtrl->GetSimpleLayerById(SID("base"));
		if (pLayer)
		{
			pLayer->RequestFadeToAnim(animId);
		}
	}

	Err result = ParentClass::PostInit(spawn);

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::SetupAttachSystem(const ProcessSpawnInfo& spawn)
{
	//First count the number of attach points
	U32 attachPointCount = 0;

	AttachSystem* pAttachSystem = GetAttachSystem();
	attachPointCount += pAttachSystem->CountSkeletonAttachPoints();
	attachPointCount += pAttachSystem->CountGeometryAttachPoints(m_lookId);

	U32 numJoints = 0;
	I32F iJoint = -1;
	StringId64 bindBoneId = spawn.GetData<StringId64>(SID("BindBone"), INVALID_STRING_ID_64);
	const DC::CustomAttachPointSpecArray* pCustomPointsArray = nullptr;
	StringId64 attachSpecsId = spawn.GetData<StringId64>(SID("attach-points"), INVALID_STRING_ID_64);
	if (attachSpecsId != INVALID_STRING_ID_64)
	{
		attachPointCount += pAttachSystem->CountDcAttachPoints(attachSpecsId);
	}
	else
	{
		StringId64 customAttachPointsId = spawn.GetData<StringId64>(SID("custom-attach-points"), INVALID_STRING_ID_64);
		if (customAttachPointsId != INVALID_STRING_ID_64)
		{
			pCustomPointsArray = ScriptManager::Lookup<DC::CustomAttachPointSpecArray>(customAttachPointsId);
			if (pCustomPointsArray)
				attachPointCount += CountCustomAttachPoints(pCustomPointsArray);
		}
		else
		{
			bindBoneId = spawn.GetData<StringId64>(SID("BindBone"), INVALID_STRING_ID_64);
			if (bindBoneId != INVALID_STRING_ID_64)
			{
				AnimControl* pAnimCtrl = GetAnimControl();

				if (pAnimCtrl)
				{
					numJoints = pAnimCtrl->GetJointCount();
					if (numJoints > 0)
					{
						iJoint = pAnimCtrl->GetAnimData().FindJoint(bindBoneId);
						if (0 <= iJoint && iJoint < numJoints)
							attachPointCount += 1;
					}
				}
			}
		}
	}

	//Now initialize and load
	if (attachPointCount > 0)
	{
		pAttachSystem->Init(attachPointCount);
		pAttachSystem->LoadFromSkeleton();
		pAttachSystem->LoadFromGeometry(m_lookId);

		if (attachSpecsId != INVALID_STRING_ID_64)
		{
			pAttachSystem->LoadFromDcData(attachSpecsId);
			return;
		}
		else if (pCustomPointsArray)
		{
			AddCustomAttachPoints(pCustomPointsArray);
		}
		else if (0 <= iJoint && iJoint < numJoints)
		{
			pAttachSystem->AddPointSpec(AttachPointSpec(bindBoneId, (U32)iJoint));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::OnKillProcess()
{
	if (m_pDrivenBgLevelData)
	{
		m_pDrivenBgLevelData->DeactivateInstance(m_drivenBgInstanceIndex);
	}
	ParentClass::OnKillProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSimpleObject::MayBeInBindPose() const
{
	const FgAnimData* pAnimData = GetAnimData();
	if (!pAnimData
	||  !pAnimData->m_pSkeleton
	||  pAnimData->m_pSkeleton->m_numTotalJoints > kMaxIdentityJoints)
		return false; // doesn't meet the conditions for the bind pose opt

	const AnimSimpleLayer* pBaseLayer = nullptr;

	AnimControl* pAnimCtrl = GetAnimControl();
	if (!pAnimCtrl)
		return true;

	pBaseLayer = pAnimCtrl->GetSimpleLayerById(SID("base"));

	for (I32F i = 1; i < GetNumAnimateLayers(); ++i)
	{
		AnimSimpleLayer* pLayer = pAnimCtrl->GetSimpleLayerByIndex(i);
		if (pLayer && (pLayer->GetDesiredFade() != 0.0f || pLayer->GetCurrentFade() != 0.0f))
		{
			return false; // we have more than one active layer -- probably not in bind pose
		}
	}

	if (!pBaseLayer)
		return true; // no base layer -- must be in bind pose
	if (pBaseLayer->GetNumInstances() == 0)
		return true; // nothing playing on base layer -- must be in bind pose
	if (pBaseLayer->GetCurrentFade() == 0.0f && pBaseLayer->GetDesiredFade() == 0.0f)
		return true; // base layer not contributing -- must be in bind pose

	// if none of the above conditions are true, we may still be in bind pose:
	// if we're playing a single-frame animation and not blending between anims
	const AnimSimpleInstance* pInst = pBaseLayer->GetNumInstances() == 1 ? pBaseLayer->GetInstance(0) : nullptr;
	return (pInst && pInst->GetFrameCount() <= 1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NdSimpleObject::GetTotalSoundOpeningPercentage() const
{
	// Lock to ensure multithreaded queries for opening percentage won't collide.
	RecursiveAtomicLockJanitor64 janitor(&s_soundOpeningLock, FILE_LINE_FUNC);

	float fTotalSoundOpeningPercentage = GetSoundOpeningPercentage();

	// Prevent loops by checking flag to see if we've already processed this opening.
	if (!m_totalSoundOpeningActive)
	{
		m_totalSoundOpeningActive = true;
		const NdSimpleObject* pNextOpening = GetNextSoundOpening();
		float fNextTotalSoundOpeningPercentage = (pNextOpening != nullptr) ? pNextOpening->GetTotalSoundOpeningPercentage() : 0.0f;
		fTotalSoundOpeningPercentage = Max(fTotalSoundOpeningPercentage, fNextTotalSoundOpeningPercentage);
		fTotalSoundOpeningPercentage = MinMax01(fTotalSoundOpeningPercentage);
		m_totalSoundOpeningActive = false;
	}
	return fTotalSoundOpeningPercentage;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::UpdateHorseBinding()
{
	// this approach only works if we play only one anim at a time on a simple npc, we would need some way to keep track of which anims should get their apRefs updated
	if (!m_horseBinding.Valid())
		return;

	BoundFrame horseApRef;
	bool apValid = false;
	const NdGameObject* pBoundHorse = NdGameObject::LookupGameObjectByUniqueId(m_horseBinding.Get().m_horseId);
	if (!pBoundHorse)
		return;

	apValid = pBoundHorse->HorseGetSaddleApRef(horseApRef, m_horseBinding.Get().m_riderPosition);

	// simple actors don't support binding to real horses because they live in gamelib

	if (!apValid)
		return;

	AnimControl* pAnimControl = GetAnimControl();
	AnimSimpleLayer* pBaseLayer = pAnimControl ? pAnimControl->GetSimpleLayerById(SID("base")) : nullptr;
	if (!pBaseLayer)
		return;

	if (pBaseLayer->GetNumInstances() == 0)
		return;

	AnimSimpleInstance* pAnim = pBaseLayer->GetInstance(0);
	if (!pAnim)
		return;

	pAnim->SetApOrigin(horseApRef);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::PostAnimUpdate_Async()
{
	UpdateHorseBinding();
	ParentClass::PostAnimUpdate_Async();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::PostJointUpdate_Async()
{
	ParentClass::PostJointUpdate_Async();

	const bool isAnimating = IsSimpleAnimating();

	const bool wantDisable = !g_ndSimpleObjectOptions.m_disableUpdateOptimization
							 && (m_allowDisableUpdates || m_allowDisableAnimation) && m_pBgAttacher == nullptr
							 && !isAnimating
							 && (GetSplineTracker() == nullptr || GetSplineTracker()->GetSpline() == nullptr)
							 && !m_pJointOverrideData
							 && (!m_pNetController || !m_pNetController->NeedsUpdates())
							 && !IsAttached()
							 && (!m_pCompositeBody || !m_pCompositeBody->GetGameDrivenBlend());	// we can't disable anim during game driven blend otherwise we'd loose our blend target

	// if we are finished animating, go back to sleep
	if (wantDisable)
	{
		if (m_allowDisableUpdates)
		{
			DisableUpdates();
		}
		else if (m_allowDisableAnimation)
		{
			DisableAnimation();
		}
	}

	if (m_pDrivenBgLevelData && m_bDriveBgProto)
	{
		DriveBgProto();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::PostJointUpdatePaused_Async() 
{
	ParentClass::PostJointUpdatePaused_Async();

	if (m_pDrivenBgLevelData && m_bDriveBgProto)
	{
		DriveBgProto();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdSimpleObject::GetLookId() const
{
	StringId64 baseLookId = ParentClass::GetLookId();

	if (g_pNdSkinManager)
	{
		g_pNdSkinManager->RemapLookId(baseLookId, this);
	}

	return baseLookId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pPlatformDetect, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pGroundMeshRaycastResult, deltaPos, lowerBound, upperBound);

	
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const RigidBody* NdSimpleObject::GetFrameForwardRigidBody() const
{
	return GetPlatformBoundRigidBody();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::ApplyMoveModToJointCache(const AutoActorMoveMod* pMoveMod)
{
	JointCache* pJointCache = &m_pAnimData->m_jointCache;
	const AutoActorMoveMod::Joint* pMoveModeJoints = &pMoveMod->m_joint0;
	for (U32F ii = 0; ii < pMoveMod->m_numJoints; ii++)
	{
		I32 jointIndex = m_pAnimData->FindJoint(pMoveModeJoints[ii].m_jointId);
		if (jointIndex >= 0)
		{
			ndanim::JointParams jp;
			jp.m_scale = pMoveModeJoints[ii].m_scale;
			jp.m_trans = pMoveModeJoints[ii].m_locator.GetTranslation();
			jp.m_quat = Normalize(pMoveModeJoints[ii].m_locator.GetRotation());
			PHYSICS_ASSERT(IsFinite(jp.m_scale));
			PHYSICS_ASSERT(IsFinite(jp.m_trans));
			PHYSICS_ASSERT(IsFinite(jp.m_quat));
			pJointCache->SetJointParamsLs(jointIndex, jp);
		}
	}

	// Update Os Locs. Yes *Os*!
	// Also propagating scale from parent to children
	// This will only work with uniform scale on each joint as we don't re-base scale for child joint rotations
	Locator* pWsLocs = pJointCache->GetJointLocatorsWsForOutput();
	const U32 numAnimJoints = pJointCache->GetNumAnimatedJoints();
	pWsLocs[0] = Locator(kIdentity);
	for (int ii = 1; ii < numAnimJoints; ii++)
	{
		I32F iParent = pJointCache->GetParentJoint(ii);
		ANIM_ASSERT(iParent >= 0);

		const ndanim::JointParams& parentJp = pJointCache->GetJointParamsLs(iParent);
		ndanim::JointParams jp = pJointCache->GetJointParamsLs(ii);
		const Point scaledTrans = kOrigin + (jp.m_trans - kOrigin) * parentJp.m_scale;
		const Locator childJointLocLs(scaledTrans, jp.m_quat);
		pWsLocs[ii] = pWsLocs[iParent].TransformLocator(childJointLocLs);

		jp.m_scale *= parentJp.m_scale;
	}

	// Now convert to Ws locs applying (potentially non-uniform) scale
	Locator align = GetLocator();
	Vector scale = GetScale();
	for (int ii = 0; ii < numAnimJoints; ii++)
	{
		pWsLocs[ii].SetTranslation(kOrigin + (pWsLocs[ii].GetTranslation() - kOrigin) * scale);
		pWsLocs[ii] = align.TransformLocator(pWsLocs[ii]);
		pWsLocs[ii].SetRot(Normalize(pWsLocs[ii].Rot()));
		PHYSICS_ASSERT(IsOk(pWsLocs[ii]));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSimpleObject::DisableAnimation()
{
	if (!ParentClass::DisableAnimation())
	{
		return false;
	}
	EngineComponents::GetAnimMgr()->SetAnimationDisabled(m_pAnimData, FastAnimDataUpdate);
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSimpleObject::IsAttached() const
{
	return m_pAttachmentCtrl && m_pAttachmentCtrl->IsAttached();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSimpleObject::MeetsCriteriaToDebugRecord(bool& outRecordAsPlayer) const
{
	if (const AttachmentCtrl* pAttachmentCtrl = GetAttachmentCtrl())
	{
		const NdGameObject* pParent = pAttachmentCtrl->GetParent().ToProcess();

		if (pParent && pAttachmentCtrl->IsAttached() && pParent->IsKindOf(SID("Player")))
			outRecordAsPlayer = true;
	}

	if (GetSpawner() && GetSpawner()->GetData(SID("rewind-record"), 0) == 1)
		outRecordAsPlayer = true;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSimpleObject::FastAnimDataUpdate(FgAnimData* pAnimData, F32 dt)
{
	FastAnimDataUpdateInner(pAnimData, dt);

	Process* pProcess = pAnimData->m_hProcess.ToMutableProcess();
	NdSimpleObject& self = *reinterpret_cast<NdSimpleObject*>(pProcess);

	if (self.m_pDrivenBgLevelData && self.m_bDriveBgProto)
	{
		self.DriveBgProto();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::DriveBgProto()
{
	Vec4* pSkinMtx;
	Vec4* pPrevSkinMtx;

	AnimExecutionContext* pCtx = GetAnimExecutionContext(m_pAnimData);
	pCtx->Validate();
	pSkinMtx = (pCtx->m_processedSegmentMask_SkinningMats & 1) ? pCtx->m_pAllSkinningBoneMats : g_pIdentityBoneMats;

	const AnimExecutionContext* pPrevCtx = GetPrevFrameAnimExecutionContext(m_pAnimData);
	if (pPrevCtx)
	{
		pPrevCtx->Validate();
		pPrevSkinMtx = (pPrevCtx->m_processedSegmentMask_SkinningMats & 1) ? pPrevCtx->m_pAllSkinningBoneMats : pSkinMtx;
	}
	else
	{
		pPrevSkinMtx = pSkinMtx;
	}

	// Push skinning matrices to the bg instance
	const RenderFrameParams* pParams = GetCurrentRenderFrameParams();
	m_pDrivenBgLevelData->SetInstanceBones(m_drivenBgInstanceIndex, pParams->m_frameNumber, reinterpret_cast<F32*>(pSkinMtx), reinterpret_cast<F32*>(pPrevSkinMtx));

	// If our level is logging out, suicide
	if (m_pDrivenBgLevelData->m_loggingOut)
	{
		KillProcess(this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSimpleObject::Active::ResetAnimAction()
{
	// Don't inadvertently try to notify a state script that the animation is done.
	NdSimpleObject& self = Self();
	if (self.m_pSsAnimateCtrl)
	{
		for (int i = 0; i < self.GetNumAnimateLayers(); ++i)
			self.m_pSsAnimateCtrl[i]->ResetAnimAction();
	}
}

STATE_REGISTER(NdSimpleObject, Active, kPriorityNormal);
