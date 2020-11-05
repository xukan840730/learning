/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-attachable-object.h"

#include "corelib/util/msg.h"
#include "gamelib/gameplay/nd-attack-handler.h"
#include "gamelib/gameplay/nd-attack-info.h"
#include "gamelib/gameplay/nd-drawable-object.h"
#include "gamelib/ndphys/collision-cast-interface.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/composite-keyframe-controller.h"
#include "gamelib/ndphys/havok-probe-shape.h"
#include "gamelib/ndphys/havok.h"
#include "gamelib/ndphys/rigid-body-user-data.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/ndphys/simple-jiggle.h"
#include "gamelib/scriptx/h/nd-prop-defines.h"
#include "gamelib/tasks/task-graph-mgr.h"
#include "gamelib/tasks/task-subnode.h"
#include "gamelib/render/gui2/menu2-root.h"
#include "gamelib/camera/camera-manager.h"

#include "ndlib/fx/fxmgr.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/ndphys/pat.h"
#include "ndlib/ndphys/rigid-body-base.h"
#include "ndlib/net/nd-net-controller.h"
#include "ndlib/net/nd-net-game-manager.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/interface/fg-instance.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include <Physics/Physics/Dynamics/Body/hknpBody.h>

class ArtItemCollision;
class HavokContactPointAddedEvent;
class hkpShape;
namespace DC {
struct Map;
struct SymbolArray;
}  // namespace DC

PROCESS_REGISTER(NdAttachableObject, NdDrawableObject);
FROM_PROCESS_DEFINE(NdAttachableObject);

static float s_attachableRollDamping = 0.1f;
static float s_attachableLinearDamping = 0.3f;
static bool g_attachableExtrapolate = false;
static bool g_attachableUpdateGoalStateDirectly = true;
static F32 g_attachableMovingVelScale = 1.0f;
static F32 g_attachableMovingVelPositionScale = 1.0f;
bool g_enableAttachableJiggle = true;
bool g_drawAttachableObjectParent = false;

/// --------------------------------------------------------------------------------------------------------------- ///
// Utility Functions
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
static void ValidateLocator(const Locator& loc, const char* strDesc)
{
	STRIP_IN_FINAL_BUILD;

	GAMEPLAY_ASSERTF(IsFinite(loc), ("%s not finite", strDesc));
	GAMEPLAY_ASSERTF(Length(loc.GetTranslation()) < 100000.0f, ("%s translation out of range: %s", strDesc, PrettyPrint(loc.GetTranslation())));
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdAttachableObject* CreateAttachable(const NdAttachableInfo& info,
									 StringId64 procId,
									 const ResolvedLookSeed* pParentLookSeed /* = nullptr*/,
									 const DC::SymbolArray* pTintArray /* = nullptr*/,
									 Process* pParent /* = nullptr*/)
{
	NdAttachableObject* proc = nullptr;
	SpawnInfo spawn(procId, pParent);
	spawn.m_pUserData = &info;
	spawn.m_pTintArray = pTintArray;
	spawn.m_pParentLookSeed = pParentLookSeed;
#ifdef JGREGORY
	{	// REMOVE THIS ELSE CASE BEFORE SUBMIT -- JUST VERIFYING WHAT I ALREADY KNOW TO BE TRUE!!!
		for (int i = 0; i < SpawnInfo::kVarArgCapacity; ++i)
			ALWAYS_ASSERT(spawn.m_apVarArgs[i] == nullptr);
	}
#endif

	// transfer the var args into SpawnInfo's array since that's where all base classes will be looking for the args
	if (info.m_apVarArgs)
	{
		for (int i = 0; i < SpawnInfo::kVarArgCapacity && info.m_apVarArgs[i]; ++i)
			spawn.m_apVarArgs[i] = info.m_apVarArgs[i];
	}

	proc = static_cast<NdAttachableObject*>(NewProcess(spawn));
	return proc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdAttachableObject* CreateAttachable(const NdAttachableInfo& info,
									 const ResolvedLookSeed* pParentLookSeed /* = nullptr*/,
									 const DC::SymbolArray* pTintArray /* = nullptr*/,
									 Process* pParent /* = nullptr*/)
{
	return CreateAttachable(info, SID("NdAttachableObject"), pParentLookSeed, pTintArray, pParent);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// NdAttachableObject
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
NdAttachableObject::NdAttachableObject()
	: ParentClass()
	, m_pDetachCallback(nullptr)
	, m_dontUseDamper(false)
	, m_hidden(false)
	, m_pJiggle(nullptr)
	, m_jiggleOn(true)
	, m_jiggleBlend(1.0f)
	, m_jiggleBlendTime(0.0f)
	, m_jiggleDisabledF(false)
	, m_jiggleDisabledPrevF(false)
	, m_jiggleBlendTimeF(0.0f)
	, m_dontInheritHcmt(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdAttachableObject::~NdAttachableObject()
{
	ParentClearAttachable();

	if (m_pJiggle)
	{
		m_pJiggle->Destroy();
		m_pJiggle = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdAttachableObject::Init(const ProcessSpawnInfo& spawn)
{
	PROFILE(Processes, NdAttachableObject_Init);

	m_hidden = false;

	m_mainBodyJoint = SID("root");

	NdAttachableInfo* pInfo = (NdAttachableInfo*) spawn.m_pUserData;
	ASSERT(pInfo != nullptr);

	if (!pInfo)
		return Err::kErrBadData;

	const SpawnInfo& spawnInfo = static_cast<const SpawnInfo&>(spawn);

	m_enableAttachLocatorUpdate = true;
	m_parentGameObjectThatDroppedMe = nullptr;

	m_lookId = (pInfo->m_artGroupId != INVALID_STRING_ID_64) ? pInfo->m_artGroupId : spawnInfo.m_artGroupId;
	m_detachTimeout = 0.0f;
	m_oldVelocity = kZero;
	m_detachVelocity = kZero;
	m_detachAngVelocity = kZero;
	m_overrideDetachVelocity = false;
	m_simpleAttachableAllowDisableAnimation = spawn.GetData<bool>(SID("allow-disable-animation"), true);

	Locator loc = pInfo->m_loc;
	ASSERT(IsFinite(loc));
	SetLocator(loc);

	// needs to be set before init so we allocate the instance parameters
	NdGameObject* pParentGo = pInfo->m_hParentProcessGo.ToMutableProcess();
	if (pParentGo)
	{
		m_goFlags.m_isVisibleInListenMode = pParentGo->IsVisibleInListenMode() && pParentGo->ShouldShowAttachablesInListenMode();
	}

	Err result = ParentClass::Init(spawn);
	if (result.Failed())
	{
		return result;
	}

	SetEnablePlayerParenting(false);

	m_suppressSsAnimatePostAnimUpdate = false;

	m_pAttachmentCtrl = NDI_NEW AttachmentCtrl(*this);

	// make sure we call ParentClass::Init to get user-id before calling SetParentAttachIndex
	if (pInfo->m_parentAttachIndex != AttachIndex::kInvalid)
	{
		I32 attachJointIndex = kInvalidJointIndex;
		if (pInfo->m_attachJoint != INVALID_STRING_ID_64)
		{
			attachJointIndex = FindJointIndex(pInfo->m_attachJoint);
			if (attachJointIndex == kInvalidJointIndex)
			{
				MsgConScriptError("Attachable %s has no joint named %s.", DevKitOnly_StringIdToString(GetLookId()), DevKitOnly_StringIdToString(pInfo->m_attachJoint));
			}
		}
		m_pAttachmentCtrl->AttachToObject(FILE_LINE_FUNC, pParentGo, pInfo->m_parentAttachIndex, pInfo->m_attachOffset, attachJointIndex, true);
	}
	else
	{
		m_pAttachmentCtrl->AttachToObject(FILE_LINE_FUNC, pParentGo, pInfo->m_parentAttach, pInfo->m_attachOffset, pInfo->m_attachJoint, true);
	}

	m_fakeAttached = pInfo->m_fakeAttached;
	m_detachRequested = false;

	m_droppedTime = GetProcessClock()->GetCurTime();
	m_physicalizeRequested = false;
	m_neverPhysicalize = false;
	m_animMap = ScriptPointer<DC::Map>(pInfo->m_animMap);
	m_disableNetSnapshots = false;

	m_smallLayerWhenDropped = spawn.GetData<bool>(SID("small-layer-when-dropped"), false);

	m_isNameValid = false;
	if (pInfo->m_userBareId != INVALID_STRING_ID_64)
	{
		if (Memory::IsDebugMemoryAvailable())
		{
			SetName(DevKitOnly_StringIdToString(pInfo->m_userBareId));
			m_isNameValid = true;
		}
		SetUserId(pInfo->m_userBareId, pInfo->m_userNamespaceId, pInfo->m_userId);
	}

	Process* pParent = nullptr;
	if (spawn.m_pParent)
	{
		const bool useParent = spawn.m_pParent->IsProcess() || spawn.m_pParent == EngineComponents::GetProcessMgr()->m_pSubAttachTree;
		const bool ignore = !useParent;
		MsgLevel("NdAttachable '%s request to parent under '%s%s\n", GetName(), spawn.m_pParent->GetName(), ignore ? " (ignored)" : "");
		if (useParent)
			pParent = spawn.m_pParent;
	}
	ChangeParentProcess(pParent ? pParent : EngineComponents::GetProcessMgr()->m_pAttachTree);

	ALWAYS_ASSERT(GetAnimControl());

	if (pInfo->m_customAttachPoints != INVALID_STRING_ID_64)
	{
		InitCustomAttachPoints(pInfo->m_customAttachPoints);
	}

	{
		Err err = InitCollision(spawn);
		if (err.Failed())
		{
			return err;
		}
	}

	UpdateRoot();

	FgAnimData* pAnimData = GetAnimData();
	pAnimData->SetXform(GetLocator().AsTransform());

	m_noIdleOnAnimEnd = false;

	// Make sure mesh ray casts are only done against my PmInstance if requested.
	if (IDrawControl* pDrawControl = GetDrawControl())
	{
		const bool allowDecals = spawn.GetData<bool>(SID("enable-decals"), false);
		if (allowDecals)
		{
			pDrawControl->ClearInstanceFlag(FgInstance::kDisableMeshRayCasts);
			pDrawControl->SetInstanceFlag(FgInstance::kNoProjection);
		}
		else
		{
			pDrawControl->SetInstanceFlag(FgInstance::kDisableMeshRayCasts | FgInstance::kNoProjection);
		}

		if (pInfo->m_netDropped)
		{
			pDrawControl->HideAllMesh(0);
			pDrawControl->HideAllMesh(1);
			m_droppedTime = GetProcessClock()->GetCurTime();
			GoNetDropped();
		}

		if (pInfo->m_skipPlayerFlashlight)
			pDrawControl->SetInstanceFlag(FgInstance::kNotShownInPlayerSpotLight);
	}

	if (UseTwoStageAnimation(spawn))
	{
		pAnimData->SetAnimSourceMode(0, FgAnimData::kAnimSourceModeClipData);
		pAnimData->SetAnimResultMode(0, FgAnimData::kAnimResultJointParamsLs);

		pAnimData->SetAnimSourceMode(1, FgAnimData::kAnimSourceModeJointParams);
		pAnimData->SetAnimResultMode(1, FgAnimData::kAnimResultJointTransformsAndSkinningMatrices);
	}
	else
	{
		pAnimData->SetAnimResultMode(0, FgAnimData::kAnimResultJointTransformsOs | FgAnimData::kAnimResultSkinningMatricesOs | FgAnimData::kAnimResultJointParamsLs);
	}

	StringId64 shimmerSettings = spawn.GetData<StringId64>(SID("shimmer-settings"), (g_ndConfig.m_pNetInfo && g_ndConfig.m_pNetInfo->IsNetActive()) ? INVALID_STRING_ID_64 : SID("*pickup-shimmer*"));
	SetShimmer(shimmerSettings, true);
	SetShimmerIntensity(0.0f);

	if (!HasShaderInstanceParams() && pInfo->m_useShaderInstanceParams)
		AllocateShaderInstanceParams();

	if (m_hidden)
	{
		if (IDrawControl* pDrawControl = GetDrawControl())
		{
			pDrawControl->HideObject(-1);
		}
	}

	if (m_pCompositeBody)
	{
		StringId64 keyFrameCtrlSetId = spawn.GetData<StringId64>(SID("composite-keyframe-controller-settings-id"), INVALID_STRING_ID_64);
		if (keyFrameCtrlSetId != INVALID_STRING_ID_64)
		{
			m_pCompositeBody->InitKeyframeController();
			CompositeKeyframeController* pKeyCtrl = m_pCompositeBody->GetKeyframeController();
			pKeyCtrl->SetDcSettings(keyFrameCtrlSetId);
		}
	}

	// Old U4 thing. Let's wait how we're going to attach flashlight in T2
	//if (pInfo->m_artGroupId == SID("flashlight") && pInfo->m_parentAttach == SID("flashlightAttach"))
	//{
	//	AttachIndex attachIndex;
	//	if (pParentGo->GetAttachSystem()->FindPointIndexById(&attachIndex, pInfo->m_parentAttach))
	//	{
	//		Locator attachOffset = pParentGo->GetAttachSystem()->GetPointSpec(attachIndex).m_jointOffset;
	//		Locator ls = Inverse(attachOffset);

	//		HingeJiggle* pJiggle = NDI_NEW HingeJiggle();
	//		pJiggle->Init(GetLocator(), ls, Inverse(ls), attachOffset.GetPosition());
	//		m_pJiggle = pJiggle;
	//	}
	//}

	ParentUpdateAttachable();

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdAttachableObject::InitCollision(const ProcessSpawnInfo& spawn)
{
	NdAttachableInfo* pInfo = (NdAttachableInfo*)spawn.m_pUserData;
	bool bInNetMenu = g_ndConfig.m_pNetInfo && g_ndConfig.m_pNetInfo->IsNetActive() && Menu2::Root::Get().IsMainMenuShown();
	if (!bInNetMenu)
	{
		const ArtItemCollision* pCollision = GetSingleCollision();
		if (pCollision)
		{
			Err err;
			CompositeBodyInitInfo cinfo;
			cinfo.m_initialMotionType = kRigidBodyMotionTypeAuto;
			err = InitCompositeBody(&cinfo);
			if (err == Err::kOK)
			{
				RigidBody* pRigidBody = GetMainRigidBody();
				if (pRigidBody)
				{
					m_pCompositeBody->SetLayer(Collide::kLayerNoCollide);
					m_pCompositeBody->SetLocatorDriver(0); // let locator be driven by root (in case the root will become physics driven)
					pRigidBody->SetMotionType(kRigidBodyMotionTypeGameDriven); // the body that we attach has to be game driven
					pRigidBody->SetDestructionEnabled(false); // not let it break in my hands
				}
			}
		}
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
RigidBody* NdAttachableObject::GetMainRigidBody() const
{
	if (!m_pCompositeBody || m_pCompositeBody->GetNumBodies() == 0)
		return nullptr;

	if (!m_mainBodyJoint)
		return nullptr;

	U32F bodyIndex = m_pCompositeBody->FindBodyIndexByJointSid(m_mainBodyJoint);
	if (bodyIndex == CompositeBody::kInvalidBodyIndex)
	{
		// ASSERT(false);
		bodyIndex = 0;
	}

	RigidBody* pRigidBody = m_pCompositeBody->GetBody(bodyIndex);
	return pRigidBody;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAttachableObject::UseTwoStageAnimation(const ProcessSpawnInfo& spawn) const
{
	const NdAttachableInfo* pInfo = (const NdAttachableInfo*) spawn.m_pUserData;
	ASSERT(pInfo != nullptr);
	return pInfo->m_needs2StageAnimation;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::InitSoundEffects(float bounceThreshold, float timeout, StringId64 hardSound, StringId64 softSound)
{
	ASSERT(hardSound != INVALID_STRING_ID_64);
	ASSERT(softSound != INVALID_STRING_ID_64);

	m_soundInfo.m_playSound = true;
	m_soundInfo.m_startedMovingTime = GetProcessClock()->GetCurTime();
	m_soundInfo.m_sfxTime = GetProcessClock()->GetCurTime();
	m_soundInfo.m_gain = 1.0f;

	m_soundInfo.m_bounceThreshold = bounceThreshold;
	m_soundInfo.m_timeout = timeout;
	m_soundInfo.m_hardSurfaceSound = hardSound;
	m_soundInfo.m_softSurfaceSound = softSound;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(offset_bytes, lowerBound, upperBound);
	if (m_pJiggle)
	{
		m_pJiggle->Relocate(offset_bytes, lowerBound, upperBound);
		RelocatePointer(m_pJiggle, offset_bytes, lowerBound, upperBound);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::OnKillProcess()
{
	if (m_pJiggle)
	{
		m_pJiggle->OnKill();
	}

	ParentClass::OnKillProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::SetParentObjectAndAttachIndex(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
													   NdGameObject* pParent,
													   AttachIndex parentAttachIndex,
													   const Locator& attachOffset)
{
	AttachIndex currParentIndex = GetParentAttachIndex();
	if (parentAttachIndex != currParentIndex || pParent != GetParentGameObject())
	{
		ParentClearAttachable();

		UnregisterFromListenModeMgr();

		const AttachSystem* pAttach = pParent ? pParent->GetAttachSystem() : nullptr;
		if (pAttach && (parentAttachIndex != AttachIndex::kInvalid))
		{
			// maintain current attach-joint index.
			I32 currAttachJointIndex = m_pAttachmentCtrl->GetAttachJointIndex();
			m_pAttachmentCtrl->AttachToObject(sourceFile, sourceLine, sourceFunc, pParent, parentAttachIndex, attachOffset, currAttachJointIndex);
		}
		else
		{
			// detach
			m_pAttachmentCtrl->Detach();
		}

		ParentUpdateAttachable();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAttachableObject::AttachTo(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
								  MutableNdGameObjectHandle hParent,
								  StringId64 attachPointName,
								  const Locator& attachOffset)
{
	NdGameObject* pParentGameObject = hParent.ToMutableProcess();
	if (pParentGameObject)
	{
		const AttachSystem* pAttach = pParentGameObject->GetAttachSystem();
		AttachIndex newIdx;
		if (pAttach->FindPointIndexById(&newIdx, attachPointName))
		{
			SetParentObjectAndAttachIndex(sourceFile, sourceLine, sourceFunc, pParentGameObject, newIdx, attachOffset);
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAttachableObject::ChangeAttachPoint(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
										   StringId64 attachPointName,
										   const Locator& attachOffset)
{
	NdGameObject* pParentGameObject = GetParentGameObjectMutable();
	if (pParentGameObject != nullptr)
	{
		return AttachTo(sourceFile, sourceLine, sourceFunc, pParentGameObject, attachPointName, attachOffset);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAttachableObject::AttachToBlend(const char* sourceFile,
									   U32F sourceLine,
									   const char* sourceFunc,
									   MutableNdGameObjectHandle hParent,
									   StringId64 attachPointName,
									   const Locator& desiredOffset,
									   float blendTime,
									   bool fadeOutVelWs)
{
	const NdGameObject* pNewParent = hParent.ToProcess();
	if (!pNewParent)
		return false;

	const AttachSystem* pNewParentAS = pNewParent->GetAttachSystem();
	if (!pNewParentAS)
		return false;

	AttachIndex dummy;
	if (!pNewParentAS->FindPointIndexById(&dummy, attachPointName))
		return false;

	Locator myLocWs = GetLocator();
	{
		I32 myJointIdx = GetAttachJointIndex();
		if (myJointIdx != -1)
		{
			if (const AnimControl* pAnimControl = GetAnimControl())
			{
				if (const JointCache* pPropJointCache = pAnimControl->GetJointCache())
				{
					myLocWs = pPropJointCache->GetJointLocatorWs(myJointIdx);
				}
			}
		}
	}

	m_pAttachmentCtrl->BlendToAttach(sourceFile,
									 sourceLine,
									 sourceFunc,
									 hParent,
									 attachPointName,
									 myLocWs,
									 desiredOffset,
									 blendTime,
									 fadeOutVelWs);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// This one blends between 2 attach spaces ignoring the fact that the gun may have a jiggle on top of our attachment space
// In theory we should always do this (since jiggle has its own blend in/out)
// but being so late in the project, I'm just spot fixing now
bool NdAttachableObject::AttachToBlendIgnoringJiggle(const char* sourceFile,
									   U32F sourceLine,
									   const char* sourceFunc,
									   MutableNdGameObjectHandle hParent,
									   StringId64 attachPointName,
									   const Locator& desiredOffset,
									   float blendTime,
									   bool fadeOutVelWs)
{
	//m_pAttachmentCtrl->

	const NdGameObject* pNewParent = hParent.ToProcess();
	if (!pNewParent)
		return false;

	const AttachSystem* pNewParentAS = pNewParent->GetAttachSystem();
	if (!pNewParentAS)
		return false;

	AttachIndex dummy;
	if (!pNewParentAS->FindPointIndexById(&dummy, attachPointName))
		return false;

	Locator myLocWs = GetLocator();
	{
		I32 myJointIdx = GetAttachJointIndex();
		if (myJointIdx != -1)
		{
			if (const AnimControl* pAnimControl = GetAnimControl())
			{
				if (const JointCache* pPropJointCache = pAnimControl->GetJointCache())
				{
					myLocWs = pPropJointCache->GetJointLocatorWs(myJointIdx);
				}
			}
		}
		else
		{
			ComputeDesiredLocatorWs(myLocWs, true);
		}
	}

	m_pAttachmentCtrl->BlendToAttach(sourceFile,
									 sourceLine,
									 sourceFunc,
									 hParent,
									 attachPointName,
									 myLocWs,
									 desiredOffset,
									 blendTime,
									 fadeOutVelWs);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAttachableObject::ChangeAttachPointBlend(const char* sourceFile,
												U32F sourceLine,
												const char* sourceFunc,
												StringId64 attachPointName,
												const Locator& desiredOffset,
												float blendTime,
												bool fadeOutVelWs)
{
	NdGameObject* pParentGameObject = GetParentGameObjectMutable();
	if (pParentGameObject != nullptr)
	{
		return AttachToBlend(sourceFile,
							 sourceLine,
							 sourceFunc,
							 pParentGameObject,
							 attachPointName,
							 desiredOffset,
							 blendTime,
							 fadeOutVelWs);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* NdAttachableObject::GetBoundPlatform() const
{
	// Usually an attachable is bound via a Parent (usually a Character)...
	const NdGameObject* pParent = GetParentGameObject();
	if (pParent)
	{
		return pParent->GetBoundPlatform();
	}
	else
	{
		// but sometimes it is bound like any other NdGameObject.
		return NdGameObject::GetBoundPlatform();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NdAttachableObject::GetVelocity() const
{
	const RigidBody* pRigidBody = GetMainRigidBody();
	return pRigidBody ? pRigidBody->GetLinearVelocity() : GetVelocityWs();
}

/// --------------------------------------------------------------------------------------------------------------- ///
AttachIndex NdAttachableObject::LookupAttachIndex(StringId64 attachId) const
{
	AttachIndex index = AttachIndex::kInvalid;

	const NdGameObject* pParentGameObject = GetParentGameObject();
	if (pParentGameObject)
	{
		const AttachSystem* pAttach = pParentGameObject->GetAttachSystem();
		if (pAttach)
		{
			pAttach->FindPointIndexById(&index, attachId);
		}
	}

	return index;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::UpdateRoot(bool postCameraAdjust)
{
	PROFILE(Processes, ProcAttach_UpdateRoot);

	Locator desiredRootLoc = GetLocator();

	if (ComputeDesiredLocatorWs(desiredRootLoc))
	{
		if (m_pJiggle && !postCameraAdjust)
		{
			if (m_jiggleDisabledF)
			{
				SetJiggleOn(false, m_jiggleBlendTimeF);
			}
			else if (m_jiggleDisabledPrevF)
			{
				SetJiggleOn(true, m_jiggleBlendTimeF);
			}
			m_jiggleDisabledPrevF = m_jiggleDisabledF;
			m_jiggleDisabledF = false; // reset each frame

			bool jiggleOn = m_jiggleOn;
			F32 blendTime = m_jiggleBlendTime;

			F32 disallowedBlendTime;
			bool jiggleAllowed = IsJiggleAllowed(disallowedBlendTime);
			if (!jiggleAllowed)
			{
				// Not allowed internally
				jiggleOn = false;
				blendTime = !m_jiggleOn ? Min(m_jiggleBlendTime, disallowedBlendTime) : disallowedBlendTime;
			}
			else if (m_jiggleOn)
			{
				// Allowed. Blend in at 0.2s or slower
				blendTime = Max(m_jiggleBlendTime, 0.2f*(1.0f-m_jiggleBlend));
			}

			if (blendTime > 0.0f)
			{
				m_jiggleBlend += (jiggleOn ? 1.0f : -1.0f) * GetProcessDeltaTime() / blendTime;
				m_jiggleBlend = MinMax01(m_jiggleBlend);
			}
			else
			{
				m_jiggleBlend = jiggleOn ? 1.0f : 0.0f;
			}

			m_jiggleBlendTime -= GetProcessDeltaTime();
			m_jiggleBlendTime = Max(0.0f, m_jiggleBlendTime);

			m_pJiggle->SetEnabled(m_jiggleBlend > 0.0f);
			m_pJiggle->SetParent(this);
			m_pJiggle->Update(desiredRootLoc);
			if (TRUE_IN_FINAL_BUILD(g_enableAttachableJiggle))
			{
				Locator jiggleLoc = m_pJiggle->GetLocatorWs();
				desiredRootLoc = Lerp(desiredRootLoc, jiggleLoc, m_jiggleBlend);
			}
		}

		if (m_enableAttachLocatorUpdate)
			SetLocator(desiredRootLoc);
	}
	else
	{
		if (m_pJiggle)
		{
			m_pJiggle->SetEnabled(false);
			m_jiggleBlend = 0.0f;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAttachableObject::ComputeDesiredLocatorWs(Locator& locWsOut, bool dontAdvance)
{
	if (m_neverPhysicalize)
		return false;

	if (m_pAttachmentCtrl->IsAttached() || m_fakeAttached)
	{
		const NdGameObject* pParentGameObject = GetParentGameObject();
		if (!pParentGameObject)
			return false;

		Locator parentAttachLoc(kIdentity);

		const AttachSystem* pAttach = pParentGameObject ? pParentGameObject->GetAttachSystem() : nullptr;

		ValidateLocator(GetParentAttachOffset(), "Parent attach offset");

		AttachIndex parentAttachIndex = GetParentAttachIndex();
		if (!pAttach || parentAttachIndex == AttachIndex::kInvalid)
		{
			parentAttachLoc = pParentGameObject->GetLocator();
		}
		else
		{
			parentAttachLoc = pAttach->GetLocator(parentAttachIndex);
		}

		ValidateLocator(parentAttachLoc, "Parent attach");

		const Locator rootLoc = m_pAttachmentCtrl->GetAttachLocation(parentAttachLoc, dontAdvance);

		ValidateLocator(rootLoc, "Attach location");

		locWsOut = rootLoc;

		return true;
	}
	else
	{
		// support detach-fade-out.
		const AttachmentCtrl::DetachFadeOut& detachFadeOutInfo = m_pAttachmentCtrl->GetDetachFadeOut();
		if (detachFadeOutInfo.m_startTime > Seconds(0) &&
			detachFadeOutInfo.m_fadeOutTime > 0.f &&
			GetCurTime() - detachFadeOutInfo.m_startTime < Seconds(detachFadeOutInfo.m_fadeOutTime))
		{
			const float elapsedTime = (GetCurTime() - detachFadeOutInfo.m_startTime).ToSeconds();
			const float t0 = LerpScaleClamp(0.f, detachFadeOutInfo.m_fadeOutTime, 0.f, 1.f, elapsedTime);
			const float t1 = CalculateCurveValue(t0, DC::kAnimCurveTypeUniformS);

			const Locator blendedRootLoc = Lerp(detachFadeOutInfo.m_startLoc, locWsOut, t1);
			locWsOut = blendedRootLoc;

			return true;
		}

		return false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdAttachableObject::GetParentAttachName() const
{
	const NdGameObject* pParentGameObject = GetParentGameObject();
	if (pParentGameObject)
	{
		const AttachSystem* pAttach = pParentGameObject->GetAttachSystem();
		AttachIndex parentAttachIndex = m_pAttachmentCtrl->GetParentAttachIndex();
		if (pAttach && !(parentAttachIndex == AttachIndex::kInvalid))
		{
			const AttachPointSpec& attachSpec = pAttach->GetPointSpec(parentAttachIndex);
			return attachSpec.m_nameId;
		}
	}
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::UpdateTimers()
{
	// Update the detach timeout if needed
	if (m_detachRequested)
	{
		m_detachTimeout -= GetProcessDeltaTime();
		if (m_detachTimeout <= 0.0f)
		{
			if (m_pDetachCallback)
			{
				m_pDetachCallback(this);
			}

			m_detachRequested = false;
			GoDropped();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::ParentUpdateAttachable()
{
	NdGameObject* pParentGameObject = GetParentGameObjectMutable();
	AttachIndex attachIndex = GetParentAttachIndex();

	if (pParentGameObject != nullptr && attachIndex != AttachIndex::kInvalid)
	{
		pParentGameObject->SetAttachable(attachIndex, GetUserId());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::ParentClearAttachable()
{
	NdGameObject* pParentGameObject = GetParentGameObjectMutable();
	AttachIndex attachIndex = GetParentAttachIndex();

	if (pParentGameObject != nullptr && attachIndex != AttachIndex::kInvalid)
	{
		pParentGameObject->SetAttachable(attachIndex, INVALID_STRING_ID_64);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::EventHandler(Event& event)
{
	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("disable-attach-locator-update"):
		m_enableAttachLocatorUpdate = false;
		break;
	case SID_VAL("enable-attach-locator-update"):
		m_enableAttachLocatorUpdate = true;
		break;
	case SID_VAL("update-visibility-for-parent"):
	{
		InheritVisibilityFromParent();
		break;
	}
	case SID_VAL("reset-attach-offset"):
		{
			SetParentAttachOffset(Locator(kIdentity));
			break;
		}
	case SID_VAL("fade-to-attach-from-ground-loc"):
		{
			const BoundFrame groundLoc = *event.Get(0).GetPtr<BoundFrame*>();
			float blendTime = event.Get(1).GetFloat();
			m_pAttachmentCtrl->SetFadeToAttach(blendTime, groundLoc);
			SetLocator(groundLoc.GetLocator());
		}
		break;
	case SID_VAL("fade-to-attach"):
		{
			const BoundFrame groundLoc = *event.Get(0).GetPtr<BoundFrame*>();
			float blendTime = event.Get(1).GetFloat();
			m_pAttachmentCtrl->SetFadeToAttach(blendTime, groundLoc);
		}
		break;
	case SID_VAL("print-offset"):
		{
			if (m_pAttachmentCtrl->IsAttached())
			{
				const NdGameObject* pParentGameObject = GetParentGameObject();
				if (pParentGameObject)
				{
					const AttachSystem* pAttach = pParentGameObject->GetAttachSystem();
					if (pAttach)
					{
						AttachIndex newAttachIndex = LookupAttachIndex(event.Get(0).GetStringId());
						const Locator loc = pAttach->GetLocator(newAttachIndex);

						Locator jointLoc = GetAnimControl()->GetJointCache()->GetJointLocatorWs(GetAttachJointIndex());

						Locator invLoc = loc.UntransformLocator(jointLoc);

						MsgOut("Locator(Vector(%f, %f, %f), Quat(%f, %f, %f, %f))\n",
							(float)invLoc.Pos().X(),
							(float)invLoc.Pos().Y(),
							(float)invLoc.Pos().Z(),
							(float)invLoc.Rot().X(),
							(float)invLoc.Rot().Y(),
							(float)invLoc.Rot().Z(),
							(float)invLoc.Rot().W());
					}
				}
			}
		}
		break;
	case SID_VAL("dropped"):
		{
			const RigidBody* pRigidBody = GetMainRigidBody();
			if (pRigidBody)
				GoDropped();
		}
		break;
	case SID_VAL("net-player-left"):
		{
			// SS: Don't need to do this anymore, the ownership should revert to the host
			/*
			if (IsState(SID("Dropped")))
			{
				I32 playerId = event.Get(0).GetI32();
				if (m_pNetController && m_pNetController->GetNetOwnerId() == playerId)
				{
					KillProcess(this);
					return;
				}
			}
			*/
		}
		break;

	case SID_VAL("collision-logout-query"):
		{
			const ArtItemCollision* pArtItem = event.Get(0).GetConstPtr<ArtItemCollision*>();
			StringId64 artName = event.Get(1).GetStringId();
			if (m_pCompositeBody && m_pCompositeBody->IsUsingCollision(pArtItem))
			{
				event.SetResponse(true);
			}
			else
			{
				ParentClass::EventHandler(event);
			}
			return;
		}

	case SID_VAL("collision-logout"):
		{
			const ArtItemCollision* pArtItem = event.Get(0).GetConstPtr<ArtItemCollision*>();
			StringId64 artName = event.Get(1).GetStringId();
			if (m_pCompositeBody && m_pCompositeBody->IsUsingCollision(pArtItem))
			{
				MsgErr("Logging out collision %s while it's still in use by %s!\n", pArtItem->GetName(), DevKitOnly_StringIdToString(GetUserId()));
				HandleDataLoggedOutError();
				KillProcess(this);
			}
			else
			{
				ParentClass::EventHandler(event);
			}
			return;
		}

	case SID_VAL("set-attached"):
		{
			m_pAttachmentCtrl->HandleAttachEvent(event);
			event.SetResponse(true);
		}
		break;

	case SID_VAL("get-parent-attach-object"):
		{
			MutableNdGameObjectHandle hParent;
			if (IsAttached())
			{
				hParent = m_pAttachmentCtrl->GetParentMutable();
			}
			event.SetResponse(hParent.ToMutableProcess());
		}
		break;

	case SID_VAL("get-parent-attach-point"):
		{
			StringId64 parentAttachPoint = INVALID_STRING_ID_64;
			if (IsAttached())
			{
				parentAttachPoint = m_pAttachmentCtrl->GetParentAttachPoint();
			}
			event.SetResponse(parentAttachPoint);
		}
		break;

	case SID_VAL("set-small-layer-when-dropped"):
		{
			SetSmallLayerWhenDropped(event.Get(0).GetAsBool(true));
		}
		break;

	default:
		NdDrawableObject::EventHandler(event);
	};
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::PostAnimUpdate_Async()
{
	NdDrawableObject::PostAnimUpdate_Async();
	if (!m_pCompositeBody)
	{
		// We skip post anim blending for optimization so we have to call this now to get our RB to sync
		NdGameObject::PostAnimBlending_Async();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::PostAnimBlending_Async()
{
	UpdateRoot();

	if (IsState(SID("Animating")))
	{
		ASSERT(m_suppressSsAnimatePostAnimUpdate);

		const NdAttachableObject::Animating* pAnimState = (NdAttachableObject::Animating*)GetState();
		AnimControl* pAnimControl = GetAnimControl();

		for (U32F iLayer = 0; iLayer < GetNumAnimateLayers(); ++iLayer)
		{
			SsAnimateController* pSsController = GetAnimateCtrl(iLayer);
			if (pSsController)
			{
				const Locator attachRootLoc = GetLocator();

				pSsController->PostAnimUpdate(); // do it here instead of in PostAnimUpdate()

				const Locator animatedRootLoc = GetLocator();

				float motionFade = 1.0f;

				const StringId64 layerId = pSsController->GetLayerId();

				const AnimSimpleLayer* pLayer = pAnimControl->GetSimpleLayerById(layerId);
				const AnimSimpleInstance* pInst = pLayer ? pLayer->GetInstanceById(pAnimState->m_instanceId) : nullptr;
				const AnimSimpleInstance* pCurInst = pLayer ? pLayer->CurrentInstance() : nullptr;

				const float fadeInTime = pSsController->GetFadeInTimeSec();
				const float fadeOutTime = pSsController->GetFadeOutTimeSec();

				if (pInst)
				{
					const float duration = pInst->GetDuration();
					const float phase = pInst->GetPhase();
					const float time = phase * duration;
					const float remainingTime = duration - time;
					if (fadeInTime > kSmallFloat && time < fadeInTime)
					{
						const float t = time / fadeInTime;
						const DC::AnimCurveType fadeInCurve = pSsController->GetFadeInCurve();
						motionFade = CalculateCurveValue(t, fadeInCurve);
					}
					else if (fadeOutTime > kSmallFloat && remainingTime < fadeOutTime)
					{
						const float t = remainingTime / fadeOutTime;
						const DC::AnimCurveType fadeOutCurve = pSsController->GetFadeOutCurve();
						motionFade = CalculateCurveValue(t, fadeOutCurve);
					}
					else
					{
						motionFade = (pInst->GetPhase() < 1.0f) ? 1.0f : 0.0f;
					}
				}

				const Locator newLocWs = Lerp(attachRootLoc, animatedRootLoc, motionFade);

				SetLocator(newLocWs);

				if (motionFade < kSmallFloat && !pSsController->IsAnimating())
				{
					GoActive();
				}
			}
		}
	}

	// Propagate new locator into joint cache
	// This has to be done before we do PostAnimBlending on composite body because there we are reading joint Ws locators from joint cache
	GetAnimData()->SetXform(GetLocator());

	if (m_pCompositeBody)
	{
		NdGameObject::PostAnimBlending_Async();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::CommonPostJointUpdate()
{
	RigidBody* pRigidBody = GetMainRigidBody();
	if (GetStateId() == SID("Dropped") && !m_neverPhysicalize)
	{
		if (m_pAttachmentCtrl->IsAttached() && pRigidBody)
		{
			Locator alignLoc = GetLocator();
			pRigidBody->SetLocator(alignLoc);
		}
		else if (m_physicalizeRequested)
		{
			// If requested we do raycast to make sure we didn't get dropped behind the collision
			bool doDepenetrate;
			Point refPos;
			GetDropDepenetrateInfo(doDepenetrate, refPos);
			if (doDepenetrate)
			{
				Locator dropLoc = pRigidBody->GetLocatorCm();

				const hknpShape* pShapeOrig = pRigidBody->GetHavokShape();
				// Let's just hope this shape is not a compound/composite?
				PHYSICS_ASSERTF(pShapeOrig->asCompositeShape() == nullptr, ("Trying to physicalize attachable that does not have simple convex collision (%s)", DevKitOnly_StringIdToString(GetLookId())));
				U8* pShapeBuffer = NDI_NEW (kAllocDoubleGameplayUpdate) U8[HKNP_SHAPE_BUFFER_SIZE];
				{
					memcpy(pShapeBuffer, (void*)pShapeOrig, HKNP_SHAPE_BUFFER_SIZE);
					U32 memSizeAndFlagsOffset = 0x10; // Hard coded since m_memSizeAndFlags is private HK_OFFSET_OF(hknpShape, m_memSizeAndFlags);
					pShapeBuffer[memSizeAndFlagsOffset] = 0; // just hack in zero in memSizeAndFlags size it's now in double buffer mem
					pShapeBuffer[memSizeAndFlagsOffset+1] = 0;
				}
				hknpShape* pShape = (hknpShape*)pShapeBuffer;

				HavokProbeShape havokProbeShape(pShape, dropLoc.GetRotation());
				if (havokProbeShape.IsValid())
				{
					m_dropDepenetrateJob.Open(1, 1, ICollCastJob::kCollCastSingleSidedCollision);
					m_dropDepenetrateJob.SetProbeShape(0, havokProbeShape);
					m_dropDepenetrateJob.SetProbeExtents(0, refPos, dropLoc.GetTranslation());
					m_dropDepenetrateJob.SetProbeFilter(0, CollideFilter(Collide::kLayerMaskBackground | Collide::kLayerMaskFgBig, Pat(Pat::kNoPhysicsMask), this));

					m_dropDepenetrateJob.Kick(FILE_LINE_FUNC);
				}
			}

			if (m_smallLayerWhenDropped)
			{
				m_pCompositeBody->SetLayer(Collide::kLayerSmall);
			}
			else
			{
				m_pCompositeBody->SetLayer(Collide::kLayerDroppedPickups);	// so player doesn't kick them around
			}
			m_pCompositeBody->SetSoftContact(false); // reset this flag in case it was set

			// Do we need any specific damping for this?
			//float effLinearDamping = (pp.m_droppedFromMovingPlatform) ? 0.0f : s_attachableLinearDamping;
			//pp.m_pRigidBody->SetDamping(effLinearDamping, s_attachableRollDamping);

			if (!m_dontUseDamper && pRigidBody)
			{
				m_damper.Start();
				pRigidBody->SetCallbackContext(&m_damper);
			}

			if (const NdGameObject* pParentGameObject = GetParentGameObject())
			{
				if (const RigidBody* pPlatformBody = pParentGameObject->GetPlatformBoundRigidBody())
				{
					pRigidBody->SetPlatformRigidBody(pPlatformBody);
					BindToRigidBody(pPlatformBody);
					m_damper.SetPlatformRigidBody(pPlatformBody);
				}
			}

			pRigidBody->Activate();
			pRigidBody->SetQualityType(hknpBodyQualityId::PRESET_CRITICAL);

			if (!pRigidBody->SetMotionType(kRigidBodyMotionTypePhysicsDriven))	// physicalize!
			{
				GoError("Failed to physicalize");
			}

			Vector clampedLinearVel, clampedAngularVel;
			if (m_overrideDetachVelocity)
			{
				clampedLinearVel = m_detachVelocity;
				clampedAngularVel = m_detachAngVelocity;
			}
			else
			{
				GetInitialPhysicalizeVelocity(clampedLinearVel, clampedAngularVel);
			}
			pRigidBody->SetVelocity(clampedLinearVel, clampedAngularVel);

#ifdef JSINECKY
			{
				Point dropPos = pRigidBody->GetLocatorCm().GetTranslation();
				DebugDrawCross(dropPos, 0.1f, kColorPink, Seconds(10.0f));
				DebugDrawLine(dropPos, dropPos + clampedLinearVel, kColorBlue, Seconds(10.0f));
			}
#endif

			// pRigidBody->SetDestructionEnabled(true);

			// @@JS hk2016
			//if (EngineComponents::GetNdGameInfo()->m_zeroGMode)
			//{
			//	// Leave some gravity so that weapons at the end go to the ground
			//	pRigidBody->GetHavokBody()->setGravityFactor(0.01f);
			//}
			//g_prim.Draw(DebugCoordAxes(pRigidBody->GetLocator(), 0.3f), Seconds(1.0f));

			m_physicalizeRequested = false;
		}
		else if (m_dropDepenetrateJob.IsValid())
		{
			m_dropDepenetrateJob.Wait();
		//	m_dropDepenetrateJob.DebugDraw(Seconds(3.0f));

			if (m_dropDepenetrateJob.NumContacts(0) > 0 && pRigidBody->GetMotionType() == kRigidBodyMotionTypePhysicsDriven)
			{
				Point correctPos = m_dropDepenetrateJob.GetContactPoint(0, 0);

				// In case we're on platform, correct the position from previous frame
				const RigidBody* pBoundBody = GetBoundRigidBody();
				if (pBoundBody)
				{
					correctPos = pBoundBody->GetLocatorCm().TransformPoint(pBoundBody->GetPreviousLocatorCm().UntransformPoint(correctPos));
				}

#ifdef JSINECKY
				MsgErr("A dropped weapon has been depenetrated\n");
				DebugDrawCross(m_droppedPos.GetTranslation(), 0.1f, kColorRed, Seconds(10.0f), &m_droppedPos);
				DebugDrawLine(m_droppedPos.GetTranslation(), correctPos, kColorRed, Seconds(10.0f), &m_droppedPos);
				DebugDrawCross(correctPos, 0.1f, kColorWhite, Seconds(10.0f), &m_droppedPos);
#endif

				m_droppedPos.SetTranslation(correctPos);
				Locator loc = pRigidBody->GetLocatorCm();
				loc.SetPosition(correctPos);
				pRigidBody->SetLocator(pRigidBody->GameFromCm(loc));
				Vector platformVelo = GetPlatformLinearVelocity();
				pRigidBody->SetVelocity(platformVelo + Vector(0.0f, 0.1f, 0.0f), Vector(kZero)); // add some little velo to prevent object from going to sleep
				m_oldVelocity = platformVelo;
			}
			m_dropDepenetrateJob.Close();
		}
	}

	if (pRigidBody && pRigidBody->GetMotionType() == kRigidBodyMotionTypePhysicsDriven && !m_dontUseDamper)
	{
		m_damper.UpdateDamper(*this);
	}

	UpdateVisualsFromParent();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::UpdateVisualsFromParent()
{
	// inherit visibility from parent
	InheritVisibilityFromParent();

	// use parent's wetmask index if we need one
	NdGameObject* pParentGameObject = GetParentGameObjectMutable();
	if (m_goFlags.m_needsWetmaskInstanceTexture && pParentGameObject)
	{
		I32 parentWetMaskIndex = pParentGameObject->GetWetMaskIndex();
		I32 currentWetMaskIndex = GetWetMaskIndex();
		if (parentWetMaskIndex != currentWetMaskIndex)
		{
			if (currentWetMaskIndex != FxMgr::kInvalid)
			{
				g_fxMgr.RemoveWetMaskDependent(currentWetMaskIndex, GetProcessId());
			}

			SetWetMaskIndex(parentWetMaskIndex);
			SetFgInstanceFlag(FgInstance::kHasWetness);

			g_fxMgr.AddWetMaskDependent(parentWetMaskIndex, GetProcessId());
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::PostJointUpdate_Async()
{
	PROFILE(Processes, ProcAttach_PJU);
	ParentClass::PostJointUpdate_Async();
	CommonPostJointUpdate();

	if (m_simpleAttachableAllowDisableAnimation
		&& GetStateId() == SID("Active")
		&& !m_pJointOverrideData
		&& !IsSimpleAnimating()
		&& (!m_pCompositeBody || (!m_pCompositeBody->GetGameDrivenBlend() && !m_pCompositeBody->GetNumFixupConstraints()))		// cannot disable when blending to game driven or when we need to fixup constraints
		)
	{
		DisableAnimation();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::FastProcessUpdate(F32 dt)
{
	CommonPostJointUpdate();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NdAttachableObject::GetMaxStateAllocSize()
{
	return sizeof(NdAttachableObject::NetDropped);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::SetSmallLayerWhenDropped(bool bSmall)
{
	m_smallLayerWhenDropped = bSmall;
	if (const RigidBody* pBody = GetMainRigidBody())
	{
		if (pBody->GetMotionType() == kRigidBodyMotionTypePhysicsDriven)
		{
			ASSERT(m_pCompositeBody);
			m_pCompositeBody->SetLayer(bSmall ? Collide::kLayerSmall : Collide::kLayerDroppedPickups);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::RequestDetach(F32 timeout, Vector_arg detachVel, Vector_arg detachAngVel, bool overrideVelo, bool immediate)
{
	if (GetStateId() == INVALID_STRING_ID_64 || IsState(SID("Active")))
	{
		m_detachRequested = true;
		m_detachTimeout = timeout;
		m_detachVelocity = detachVel;
		m_detachAngVelocity = detachAngVel;
		m_overrideDetachVelocity = overrideVelo;
		PHYSICS_ASSERT(!m_overrideDetachVelocity || (IsReasonable(m_detachVelocity, 1000000.0f) && IsReasonable(m_detachAngVelocity, 1000000.0f)));

		const RigidBody* pRigidBody = GetMainRigidBody();
		if (!pRigidBody)
		{
			StringBuilder<128> msg("Object \"%s\" can't detach because it has no collision", DevKitOnly_StringIdToString(m_lookId));
			const char* pMsg = msg.c_str();
			g_prim.Draw(DebugString(GetTranslation(), pMsg, kColorRed), Seconds(2));
			KillProcess(this);
		}
		else if (immediate)
		{
			m_detachTimeout = 0.0f;
			UpdateTimers();
			if (!m_neverPhysicalize)
			{
				m_physicalizeRequested = true;
			}
		}

		UnregisterFromListenModeMgr();
	}

	//m_pAttachmentCtrl->DetachFromCamera();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::RegisterInListenModeMgr(U32 addFlags, float fadeTime)
{
	bool playerParent = false;
	NdGameObject* parentObject = GetParentGameObjectMutable();
	while (parentObject)
	{
		if (parentObject->IsType(SID("Player")))
		{
			playerParent = true;
			break;
		}
		else if (parentObject->IsKindOf(g_type_NdAttachableObject))
		{
			NdAttachableObject* attachableObject = (NdAttachableObject*)parentObject;
			parentObject = attachableObject->GetParentGameObjectMutable();
		}
		else
		{
			parentObject = nullptr;
		}
	}

	if (parentObject && parentObject->IsType(SID("Player")))
	{
		if (parentObject->IsVisibleInListenMode())
		{
			RegisterInListenModeMgrImpl(GetParentGameObjectMutable(), addFlags);
		}
	}
	else
	{
		RegisterInListenModeMgrImpl(GetParentGameObjectMutable(), addFlags);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::ApplyOverlayInstanceTextures()
{
	if (m_pNetController)
	{
		// Removed, because this was always calling an empty stub function due to a mismatched function signature.
		//m_pNetController->HandleInstanceTextures(*this, GetParentGameObject());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::GetInitialPhysicalizeVelocity(Vector& linearVelocity, Vector& angularVelocity) const
{
	const RigidBody* pRigidBody = GetMainRigidBody();
	if (!pRigidBody)
	{
		linearVelocity = kZero;
		angularVelocity = kZero;
		return;
	}

	Vector linear = pRigidBody->GetLinearVelocity();
	Vector angular = pRigidBody->GetAngularVelocity();

	// Add detach velocity.
	linear += m_detachVelocity;
	angular += m_detachAngVelocity;

	Vector parentLin(kZero);
	Vector parentAng(kZero);
	if (const NdGameObject* pParentGameObject = GetParentGameObject())
	{
		if (const RigidBody* pPlatformBody = pParentGameObject->GetPlatformBoundRigidBody())
		{
			parentAng = pPlatformBody->GetAngularVelocity();
			parentLin = pPlatformBody->GetLinearVelocity() + Cross(parentAng, pRigidBody->GetLocatorCm().GetPosition() - pPlatformBody->GetLocatorCm().GetPosition());
		}
	}

	// Clamp to something reasonable.
	Vector localLin = linear - parentLin;
	Vector localAng = angular - parentAng;

	Vector horizLinear = localLin;
	horizLinear.SetY(Scalar(kZero));

	static F32 kMaxHorizLinear = 3.0f;
	Scalar maxHorizLinear(kMaxHorizLinear);
	if (Length(horizLinear) > maxHorizLinear)
	{
		horizLinear = maxHorizLinear * Normalize(horizLinear);
		localLin = Vector(horizLinear.X(), localLin.Y(), horizLinear.Z());
	}

	localLin.SetY(MinMax((float)localLin.Y(), -2.0f, 5.0f));

	if (Length(localAng) > 5.0f)
	{
		localAng = Normalize(localAng);
		localAng *= 5.0f;
	}

	linear = localLin + parentLin;
	angular = localAng + parentAng;

	// Return results.
	linearVelocity = linear;
	angularVelocity = angular;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::OnAttach()
{
	NdGameObject* pParent = GetParentGameObjectMutable();

	// Associate to parent's level. Weapons skip this part.
	AssociateWithLevel((pParent) ? pParent->GetAssociatedLevel() : nullptr);

	// change ambient occluders object ID to match parent
	if ((m_pDrawControl != nullptr) && (pParent != nullptr))
		m_pDrawControl->SetAmbientOccludersObjectId(pParent->GetAmbientOccludersObjectId());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::OnDetach()
{
	// Disassociate from parent's level. Weapons skip this part.
	AssociateWithLevel(nullptr);

	// set back own ambient occluders object ID
	if (m_pDrawControl != nullptr)
		m_pDrawControl->SetAmbientOccludersObjectId(GetAmbientOccludersObjectId());
}

void NdAttachableObject::PlayIdleAnim()
{
	if (AnimControl* pAnimControl = GetAnimControl())
	{
		if (pAnimControl->LookupAnim(SID("idle")).ToArtItem())
		{
			pAnimControl->GetSimpleLayerById(SID("base"))->RequestFadeToAnim(SID("idle"));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Apply damping to ensure that the weapon stays on the ground and doesn't roll away.
void NdAttachableObject::Damper::OnAddContact(RigidBody& thisBody,
	const hknpBody& otherPhysicsBody,
	const HavokContactEvent& event)
{
	// Call base class.
	MovingPlatformDamper::OnAddContact(thisBody, otherPhysicsBody, event);

	// Keep track of last hit body.
	NdAttachableObject* pSelf = PunPtr<NdAttachableObject*>(thisBody.GetOwner());
	if (pSelf)
	{
		RigidBodyUserData otherData;
		otherData.m_data = otherPhysicsBody.m_userData;
		const RigidBody* pOtherBody = RigidBody::HavokBodyToGameBody(&otherPhysicsBody);
		if (pOtherBody)
		{
			pSelf->m_hLastHitBody = pOtherBody;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Active State
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::Active::Update()
{
	PROFILE(Processes, NdAttachableObject_Active_Update);

	NdAttachableObject& pp = Self();
	AnimControl* pAnimControl = pp.GetAnimControl();

	if (pp.m_goFlags.m_needsGoremaskInstanceTexture && !pp.IsFgInstanceFlagSet(FgInstance::kHasGore))
	{
		const NdGameObject* pParentGo = pp.GetParentGameObject();
		if (pParentGo && !g_goreMgr.IsTrackingProp(pParentGo->GetProcessId(), pp.GetProcessId()))
		{
			g_goreMgr.TrackProp(pParentGo->GetProcessId(), pp.GetProcessId());
			pp.SetFgInstanceFlag(FgInstance::kHasGore);
		}
	}

	if (!pAnimControl || !pAnimControl->IsAsyncUpdateEnabled(FgAnimData::kEnableAsyncPostAnimBlending))
	{
		pp.UpdateRoot();
	}

	pp.UpdateTimers();

	if (pAnimControl)
	{
		AnimSimpleLayer* pBaseLayer = pAnimControl->GetSimpleLayerById(SID("base"));
		if (pBaseLayer)
		{
			const DC::Map* pAnimMap = pp.GetAnimMap();
			if (pAnimMap)
			{
				StringId64 animId = INVALID_STRING_ID_64;
				const NdGameObject* pParent = pp.GetParentGameObject();
				if (pParent)
				{
					StringId64 currentParentAnimId = pParent->GetCurrentAnim();
					const DC::PropAnimMapEntry* pPropAnimEntry = ScriptManager::MapLookup<DC::PropAnimMapEntry>(pAnimMap, currentParentAnimId);
					if (pPropAnimEntry)
					{
						animId = pPropAnimEntry->m_animName;
					}
					else
					{
						pPropAnimEntry = ScriptManager::MapLookup<DC::PropAnimMapEntry>(pAnimMap, SID("default"));
						if (pPropAnimEntry)
							animId = pPropAnimEntry->m_animName;
					}
				}

				if (animId != INVALID_STRING_ID_64)
				{
					pp.SetPhysMotionType(kRigidBodyMotionTypeGameDriven);

					AnimSimpleInstance* pInst = pBaseLayer->CurrentInstance();
					StringId64 currentAnimId = pInst ? pInst->GetAnimId() : INVALID_STRING_ID_64;

					if (animId != currentAnimId)
					{
						pBaseLayer->RequestFadeToAnim(animId);
					}

					pInst = pBaseLayer->CurrentInstance();
					if (pInst)
					{
						pInst->SetPlaybackRate(0.0f);
						// TODO: This won't work if the base layer is not a state layer; the GetCurrentAnim() function
						// handles this correctly, so we should add a GetCurrentAnimPhase() that also handles it correctly!
						pInst->SetPhase(pParent->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance()->Phase());
					}
				}
				else
				{
					pBaseLayer->Fade(0.0f, 0.3f);
				}
			}
			else if (!pp.m_noIdleOnAnimEnd)
			{
				AnimSimpleInstance* pInstance = pBaseLayer->CurrentInstance();
				if (pInstance)
				{
					if (pInstance->GetPhase() >= 1.0f && !pInstance->IsLooping())
					{
						pp.PlayIdleAnim();
					}
				}
			}
		}
	}

	if (FgAnimData *pAnimData = pp.GetAnimData())
	{
		pAnimData->m_allowLodCull = pp.AllowLodCull();
	}

	NdDrawableObject::Active::Update();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdAttachableObject::HandlePlayAnimEvent(Event& event)
{
	if (ParentClass::HandlePlayAnimEvent(event))
	{
		if (GetStateId() == SID("Active") || !ProcessHasState())
		{
			if (event.GetMessage() == SID("play-animation") || event.GetMessage() == SID("stop-animating"))
			{
				for (U32F iLayer = 0; iLayer < GetNumAnimateLayers(); ++iLayer)
				{
					if (SsAnimateController* pSsController = GetAnimateCtrl(iLayer))
					{
						if (pSsController->IsAnimatingAlign())
						{
							GoAnimating();
							break; // for (iLayer)
						}
					}
				}
			}
		}
		return true; // IMPORTANT: do NOT call the base class' implementation or it'll try to animate the object AGAIN
	}

	return false;
}
/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::Active::EventHandler(Event& event)
{
	NdAttachableObject& pp = Self();

	if (pp.HandlePlayAnimEvent(event))
	{
		return;
	}

	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("detach"):
		{
			// if you know it's a NdAttachableObject, just call RequestDetach directly
			const float timeout = event.Get(0).GetFloat();
			Vector detachVel(kZero);
			Vector detachAngVel(kZero);
			if (event.GetNumParams() > 1)
			{
				detachVel = *event.Get(1).GetConstPtr<const Vector*>();
				//g_prim.Draw(DebugLine(pp.GetLocator().Pos(), pp.m_detachVelocity, kColorYellow), Seconds(1.0f));
			}
			if (event.GetNumParams() > 2)
			{
				// NOTE: No longer used!
				//pp.m_detachCollideWithPlayer = event.Get(2).GetBool();
			}
			if (event.GetNumParams() > 3)
			{
				detachAngVel = *event.Get(3).GetConstPtr<const Vector*>();
			}
			pp.RequestDetach(timeout, detachVel, detachAngVel);

			event.SetResponse(true);
		}
		break;
	case SID_VAL("detach-without-physicalizing"):
		{
			const float fadeOutTime = event.GetNumParams() > 0 ? event.Get(0).GetFloat() : 0.f;
			pp.DetachButDontDrop(fadeOutTime);
			event.SetResponse(true);
		}
		break;
	case SID_VAL("is-detached"):
		event.SetResponse(pp.m_detachTimeout > 0.0f);
		break;
	case SID_VAL("dropped"):
		{
			const RigidBody* pRigidBody = pp.GetMainRigidBody();
			if (pRigidBody)
				pp.GoDropped();
		}
		break;
	default:
		NdDrawableObject::Active::EventHandler(event);
	}
}

STATE_REGISTER(NdAttachableObject, Active, kPriorityNormal);

/// --------------------------------------------------------------------------------------------------------------- ///
// Animating State
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::Animating::Enter()
{
	NdAttachableObject& pp = Self();

	pp.m_suppressSsAnimatePostAnimUpdate = true; // we call it manually in PostAnimBlending() instead
	const AnimControl* pAnimControl = pp.GetAnimControl();

	for (U32F iLayer = 0; iLayer < pp.GetNumAnimateLayers(); ++iLayer)
	{
		if (SsAnimateController* pSsController = pp.GetAnimateCtrl(iLayer))
		{
			if (pSsController->IsAnimatingAlign())
			{
				const StringId64 layerId = pSsController->GetLayerId();

				const AnimSimpleLayer* pLayer = pAnimControl->GetSimpleLayerById(layerId);
				const AnimSimpleInstance* pInst = pLayer ? pLayer->CurrentInstance() : nullptr;

				m_instanceId = pInst ? pInst->GetId() : INVALID_ANIM_INSTANCE_ID;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::Animating::Update()
{
	NdAttachableObject& pp = Self();
	const AnimControl* pAnimControl = pp.GetAnimControl();

	bool stillAnimating = false;

	for (U32F iLayer = 0; iLayer < pp.GetNumAnimateLayers(); ++iLayer)
	{
		if (SsAnimateController* pSsController = pp.GetAnimateCtrl(iLayer))
		{
			const StringId64 layerId = pSsController->GetLayerId();

			const AnimSimpleLayer* pLayer = pAnimControl->GetSimpleLayerById(layerId);

			if (!pLayer)
				continue;

			if (pSsController->IsAnimating())
			{
				const AnimSimpleInstance* pInst = pLayer->CurrentInstance();
				m_instanceId = pInst ? pInst->GetId() : m_instanceId;
				stillAnimating = true;
			}
			else
			{
				stillAnimating = pLayer->GetInstanceById(m_instanceId) != nullptr;
			}
		}
	}

	if (!stillAnimating)
	{
		pp.GoActive();
	}

	NdDrawableObject::Active::Update();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::Animating::Exit()
{
	NdAttachableObject& pp = Self();
	pp.m_suppressSsAnimatePostAnimUpdate = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::Animating::EventHandler(Event& event)
{
	NdAttachableObject& pp = Self();

	if (pp.HandlePlayAnimEvent(event))
	{
		return;
	}

	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("detach"):
		{
			// if you know it's a NdAttachableObject, just call RequestDetach directly
			const float timeout = event.Get(0).GetFloat();
			Vector detachVel(kZero);
			Vector detachAngVel(kZero);
			if (event.GetNumParams() > 1)
			{
				detachVel = *event.Get(1).GetConstPtr<const Vector*>();
				//g_prim.Draw(DebugLine(pp.GetLocator().Pos(), pp.m_detachVelocity, kColorYellow), Seconds(1.0f));
			}
			if (event.GetNumParams() > 2)
			{
				// NOTE: No longer used!
				//pp.m_detachCollideWithPlayer = event.Get(2).GetBool();
			}
			if (event.GetNumParams() > 3)
			{
				detachAngVel = *event.Get(3).GetConstPtr<const Vector*>();
			}
			pp.RequestDetach(timeout, detachVel, detachAngVel);

			event.SetResponse(true);
		}
		break;
	case SID_VAL("detach-without-physicalizing"):
		{
			const float fadeOutTime = event.GetNumParams() > 0 ? event.Get(0).GetFloat() : 0.f;
			pp.DetachButDontDrop(fadeOutTime);
			event.SetResponse(true);
		}
		break;
	case SID_VAL("is-detached"):
		event.SetResponse(pp.m_detachTimeout > 0.0f);
		break;
	case SID_VAL("dropped"):
		{
			const RigidBody* pRigidBody = pp.GetMainRigidBody();
			if (pRigidBody)
				pp.GoDropped();
		}
		break;
	default:
		NdDrawableObject::Active::EventHandler(event);
	}
}

STATE_REGISTER(NdAttachableObject, Animating, kPriorityNormalHigh);

/// --------------------------------------------------------------------------------------------------------------- ///
// Dropped State
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::Dropped::Enter()
{
	NdAttachableObject& pp = Self();

	// We need to update one last time before we detach. That way physics can get the proper initial velocity
	pp.UpdateRoot();

	TimeFrame curTime = GetProcessClock()->GetCurTime();

	pp.m_parentGameObjectThatDroppedMe = pp.m_pAttachmentCtrl->GetParent();
	pp.m_pAttachmentCtrl->Detach();

	pp.m_droppedTime = curTime;

	NdGameObject* pParentGameObject = pp.GetParentGameObjectMutable();
	const RigidBody* pPlatformBody = pParentGameObject ? pParentGameObject->GetPlatformBoundRigidBody() : nullptr;

	if (pParentGameObject)
	{
		// what??? parent game object's platform rigid body' owner is not parent game object itself??
		AttachIndex attachIndex = pp.GetParentAttachIndex();
		pp.SetParentObjectAndAttachIndex(FILE_LINE_FUNC, pPlatformBody ? pPlatformBody->GetOwner() : nullptr, attachIndex, Locator(kIdentity));
	}

	pp.m_droppedPos = BoundFrame(pp.GetLocator(), Binding(pPlatformBody));

	pp.m_soundInfo.m_startedMovingTime = curTime;
	pp.m_soundInfo.m_sfxTime = curTime;

	if (!pp.m_noIdleOnAnimEnd)
		pp.PlayIdleAnim();

	const RigidBody* pRigidBody = pp.GetMainRigidBody();

	if (pRigidBody && !pp.m_neverPhysicalize && !pRigidBody->IsBrokenBody())
	{
		pp.m_physicalizeRequested = true;
	}

	if (g_ndConfig.m_pNetInfo->IsNetActive())
	{
		pp.m_nextNetUpdate = EngineComponents::GetNdFrameState()->GetClock(kNetworkClock)->GetCurTime() + Seconds(0.1f);
	}

	if (IDrawControl* pDrawControl = pp.GetDrawControl())
	{
		// Allow mesh ray casts for foot IK, but still disallow decals.
		pDrawControl->ClearInstanceFlag(FgInstance::kDisableMeshRayCasts);
		pDrawControl->SetInstanceFlag(FgInstance::kNoDecals | FgInstance::kNoProjection);
	}

	pp.FadeShimmerIntensity(pp.ShouldShimmer() ? 1.0f : 0.0f, 1.0f);

	if (pp.m_pJiggle)
		pp.m_pJiggle->SetEnabled(false);

	NdDrawableObject::Active::Enter();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::Dropped::Update()
{
	NdAttachableObject& pp = Self();
	pp.UpdateRoot();
	pp.UpdateTimers();

	NdDrawableObject::Active::Update();

	const RigidBody* pRigidBody = pp.GetMainRigidBody();
	if (pRigidBody && pRigidBody->GetMotionType() == kRigidBodyMotionTypePhysicsDriven)
	{
		Vector oldVelocity = pp.m_oldVelocity;
		Vector newVelocity = pRigidBody->GetLinearVelocity();

		pp.m_oldVelocity = newVelocity;

		bool bAccelerating = false;
		Vector newAcceleration(kZero);

		Scalar dt(HavokGetDeltaTime());
		if (IsPositive(dt - Scalar(1.0f/300.0f)))
		{
			newAcceleration = (newVelocity - oldVelocity) * Recip(dt);
			bAccelerating = (Length(newVelocity) > Length(oldVelocity));
		}
	}

	ALWAYS_ASSERT(IsFinite(pp.GetLocator()));

	// for safety
	if (Length(pp.m_droppedPos.GetTranslation() - pp.GetLocator().Pos()) > SCALAR_LC(5000.0f))
	{
		pp.GoDie();
	}

	// Do we have an associated level?
	// if (pp.m_findCollisionLevel)
	// {
	// 	// It'd be nice to have one, so we'll get killed at a good time.
	// 	const RigidBody* pHitBodyBase = pp.m_hLastHitBody.ToBody();
	// 	const RigidBody* pHitBody = pHitBodyBase ? pHitBodyBase->GetRigidBody() : nullptr;
	// 	if (pHitBody && pHitBody->GetOwner() == nullptr && pHitBody->GetLevel() != nullptr)
	// 	{
	// 		// Hey look! We hit background geometry this frame!
	// 		// We can make an excellent guess at what level we should be associated with.
	// 		pp.AssociateWithLevel(pHitBody->GetLevel());
	// 		pp.m_findCollisionLevel = false;
	// 	}
	// }

	if (!pp.m_pAttachmentCtrl->IsAttached() && !pp.m_disableNetSnapshots && g_ndConfig.m_pNetInfo->IsNetActive() && pRigidBody && pRigidBody->IsActive())
	{
		TimeFrame netTime = EngineComponents::GetNdFrameState()->GetClock(kNetworkClock)->GetCurTime();
		if (netTime >= pp.m_nextNetUpdate)
		{
			pp.m_pNetController->SendNetEventSimpleSnapshot(pp);
			pp.m_nextNetUpdate = netTime + Seconds(0.1f);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::Dropped::Exit()
{
	NdAttachableObject& pp = Self();

	pp.m_dropDepenetrateJob.Close();

	pp.FadeShimmerIntensity(0.0f, 1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::Dropped::EventHandler(Event& event)
{
	NdAttachableObject& pp = Self();

	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("Attack"):
		{
			const NdAttackInfo* pAttackInfo = event.Get(0).GetConstPtr<NdAttackInfo*>();

			// NOTE: Removed dependency on player.h by checking this way...
			if (pAttackInfo && pAttackInfo->m_hSourceGameObj.HandleValid())
			{
				bool bAttackedByPlayer = pAttackInfo->m_hSourceGameObj.ToProcess()->IsKindOf(SID("Player"));
				if (bAttackedByPlayer)
				{
					if (NdAttackHandler* pAttackHandler = pp.GetAttackHandler())
					{
						pAttackHandler->PhysicsResponseToAttack(pp, pAttackInfo);
					}
				}
			}
		}
		break;
	case SID_VAL("PhysicalizeDropped"):
		if (!pp.m_pAttachmentCtrl->IsAttached() && !pp.m_fakeAttached)
		{
			RigidBody* pRigidBody = pp.GetMainRigidBody();
			pRigidBody->SetMotionType(kRigidBodyMotionTypePhysicsDriven);
		}
		break;
	default:
		NdDrawableObject::Active::EventHandler(event);
	}
}

STATE_REGISTER(NdAttachableObject, Dropped, kPriorityNormalHigh);

/// --------------------------------------------------------------------------------------------------------------- ///
// NetDropped State
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::NetDropped::EventHandler(Event& event)
{
	NdAttachableObject& pp = Self();

	NdDrawableObject::Active::EventHandler(event);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::NetDropped::Enter()
{
	NdDrawableObject::Active::Enter();

	NdAttachableObject& pp = Self();

	m_netForceHidden = true;

	m_enterTime = EngineComponents::GetNdFrameState()->GetClock(kNetworkClock)->GetCurTime();
	pp.m_pNetController->AddSnapshot(m_enterTime.ToSeconds(), pp.GetBoundFrame());

	pp.PlayIdleAnim();

	RigidBody* pRigidBody = pp.GetMainRigidBody();
	if (pRigidBody)
	{
		pp.m_pCompositeBody->SetLayer(Collide::kLayerNoCollide);
		pRigidBody->SetMotionType(kRigidBodyMotionTypeGameDriven);
	}

	if (IDrawControl* pDrawControl = pp.GetDrawControl())
	{
		// Allow mesh ray casts for foot IK, but still disallow decals.
		pDrawControl->ClearInstanceFlag(FgInstance::kDisableMeshRayCasts);
		pDrawControl->SetInstanceFlag(FgInstance::kNoDecals | FgInstance::kNoProjection);
	}

	pp.FadeShimmerIntensity(pp.ShouldShimmer() ? 1.0f : 0.0f, 1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::NetDropped::Update()
{
	NdDrawableObject::Active::Update();

	NdAttachableObject& pp = Self();

	BoundFrame snapshotLoc;
	if (pp.m_pNetController->GetSnapshotLocator(snapshotLoc))
	{
		m_netForceHidden = false;
		pp.SetBoundFrame(snapshotLoc);
	}

	IDrawControl* pDrawControl = pp.GetDrawControl();
	if (pDrawControl)
	{
		if (m_netForceHidden)
		{
			pDrawControl->HideAllMesh(0);
			pDrawControl->HideAllMesh(1);
		}
		else
		{
			pDrawControl->ShowAllMesh(0);
			pDrawControl->ShowAllMesh(1);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAttachableObject::NetDropped::Exit()
{
	Self().FadeShimmerIntensity(0.0f, 1.0f);

	NdDrawableObject::Active::Exit();
}

STATE_REGISTER(NdAttachableObject, NetDropped, kPriorityNormalHigh);

void NdAttachableObject::OnUserIdChange(StringId64 oldUserFullId, StringId64 newUserFullId)
{
	ParentUpdateAttachable();
}

void NdAttachableObject::InheritVisibilityFromParent()
{
	PROFILE(Processes, InheritVisibilityFromParent);

	IDrawControl* pDrawCtrl = GetDrawControl();
	const bool isCloth = IsType(SID("ProcessCharCloth"));
	const bool shouldInherit = ShouldInheritVisibilityFromParent();
	const bool inheritingAllowed = isCloth || m_pAttachmentCtrl->IsAttached() || m_fakeAttached;
	const NdGameObject* pParentGo = (shouldInherit && inheritingAllowed) ? GetParentGameObject() : nullptr;
	const IDrawControl* pParentDrawCtrl = pParentGo ? pParentGo->GetDrawControl() : nullptr;

	if (pDrawCtrl) // IMPORTANT: call this even if pParentDrawCtrl == nullptr (to properly handle detachment)
	{
		pDrawCtrl->InheritVisibilityFrom(pParentDrawCtrl);
		if (!ShouldMaintainNearCameraStatus())
			pDrawCtrl->InheritNearCameraFrom(pParentDrawCtrl);
		pDrawCtrl->InheritDisableMainViewFrom(pParentDrawCtrl);

		const bool shouldInheritHcm = inheritingAllowed && ShouldInheritHighContrastModeFromParent();
		const NdGameObject* pHcmParentGo = shouldInheritHcm ? GetParentGameObject() : nullptr;
		const IDrawControl* pHcmParentDrawControl = pHcmParentGo ? pHcmParentGo->GetDrawControl() : nullptr;
		pDrawCtrl->InheritHighContrastModeTypeFrom(pHcmParentDrawControl, GetHighContrastModeMeshExcludeFilter());
	}

	if (pParentGo)
	{
		PhotoModeSetHidden(pParentGo->GetPhotoModeHidden());

		bool isVisibleInListenMode = IsVisibleInListenMode();
		bool allowedInListenMode = pParentGo->ShouldShowAttachablesInListenMode();

		if (pDrawCtrl)
		{
			const IDrawControl* pParentDrawControl = pParentGo->GetDrawControl();
			const bool useBuddyListenMode = pParentDrawCtrl
											&& (pParentDrawCtrl->GetInstanceFlag() & FgInstance::kListenModeBuddyDraw);
			if (useBuddyListenMode)
			{
				pDrawCtrl->OrInstanceFlag(FgInstance::kListenModeBuddyDraw);
			}
			else
			{
				pDrawCtrl->ClearInstanceFlag(FgInstance::kListenModeBuddyDraw);
			}
		}

		if (isVisibleInListenMode && !allowedInListenMode)
		{
			UnregisterFromListenModeMgr();
		}
		if (allowedInListenMode)
		{
			if (pParentGo->IsVisibleInListenMode() && !isVisibleInListenMode)
			{
				// handle attachment only this way, detachment is performed in request detach
				RegisterInListenModeMgr();
				isVisibleInListenMode = true;
			}
		}
	}
}

void NdAttachableObject::SetJiggleOn(bool b, F32 blendTime)
{
	m_jiggleOn = b;
	m_jiggleBlendTime = blendTime * (b ? 1.0f-m_jiggleBlend : m_jiggleBlend);
}

void NdAttachableObject::SetJiggleDisabledF(F32 blendTime)
{
	m_jiggleDisabledF = true;
	m_jiggleBlendTimeF = blendTime;
}

bool NdAttachableObject::MeetsCriteriaToDebugRecord(bool& outRecordAsPlayer) const
{
	const NdGameObject* pParent = GetParentGameObject();

	if (pParent && (pParent->IsKindOf(SID("Player")) || pParent->IsKindOf(SID("ProcessBackpack"))))
		outRecordAsPlayer = true;

	return true;
}

F32 NdAttachableObject::GetCharacterSoftStepMultiplier(const RigidBody* pCollidingBody) const
{
	if (pCollidingBody->GetLayer() == Collide::kLayerDroppedPickups)
	{
		// Always very weak on pickups
		return 0.1f;
	}
	if (const NdGameObject* pParent = GetParentGameObject())
	{
		return pParent->GetCharacterSoftStepMultiplier(pCollidingBody);
	}
	return 1.0f;
}


/// --------------------------------------------------------------------------------------------------------------- ///
namespace
{
	struct HolsterPoint
	{
		StringId64 m_sid;
		const char* m_str;
	};
}

static const HolsterPoint kHolsters[] =
{
	{ SID("gun_holster"), "gun_holster" },
	{ SID("gun_holster_leg"), "gun_holster_leg" },
	{ SID("gun_holster_pelvis"), "gun_holster_pelvis" },
};

/// --------------------------------------------------------------------------------------------------------------- ///
const char* NdAttachableObject::HolsterPointToString(const StringId64 attachPointName)
{
	for (U32 i = 0; i < ARRAY_COUNT(kHolsters); ++i)
	{
		if (kHolsters[i].m_sid == attachPointName)
			return kHolsters[i].m_str;
	}

	return nullptr;
}

bool IsItemInHand(NdAttachableObjectHandle hItem, CharacterHandle hOwner)
{
	if (hItem.HandleValid())
	{
		const NdAttachableObject* pItem = hItem.ToProcess();
		StringId64 parentAttach = pItem->GetParentAttachName();
		NdGameObjectHandle parentHandle = pItem->GetParentObjHandle();
		if ((parentAttach == SID("rightHand") || parentAttach == SID("leftHand"))
			&& parentHandle == hOwner)
			return true;
	}
	return false;
}
