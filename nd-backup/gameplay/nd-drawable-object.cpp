/*
 * Copyright (c) 2006 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-drawable-object.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"
#include "ndlib/anim/joint-modifiers/joint-modifier.h"
#include "ndlib/anim/joint-modifiers/joint-modifiers.h"
#include "ndlib/camera/camera-final.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/look.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/util/jaf-anim-recorder.h"

#include "gamelib/gameplay/nd-attachable-object.h"
#include "gamelib/gameplay/nd-effect-control.h"
#include "gamelib/gameplay/nd-prop-control.h"
#include "gamelib/gameplay/process-char-cloth.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "gamelib/state-script/ss-animate.h"

/// --------------------------------------------------------------------------------------------------------------- ///
// NdDrawableObject
/// --------------------------------------------------------------------------------------------------------------- ///
PROCESS_REGISTER(NdDrawableObject, NdGameObject);
STATE_REGISTER(NdDrawableObject, Active, kPriorityNormal);
FROM_PROCESS_DEFINE(NdDrawableObject);

NdDrawableObject::NdDrawableObject()
: ParentClass()
, m_pSsAnimateCtrl(nullptr)
, m_numAnimateLayers(0)
, m_lookId(INVALID_STRING_ID_64)
, m_pPropControl(nullptr)
, m_suppressSsAnimatePostAnimUpdate(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdDrawableObject::Init(const ProcessSpawnInfo& spawnInfo)
{
	PROFILE(Processes, ProcessDrawable_Init);

	const SpawnInfo& spawn = static_cast<const SpawnInfo&>(spawnInfo);

	m_suppressSsAnimatePostAnimUpdate = false;
	m_extraPropCapacity = 0;
	m_pPropControl = nullptr;	

	if (m_lookId == INVALID_STRING_ID_64)
	{
		m_lookId = StringToStringId64(GetTypeName());
	}
	m_lookId = spawn.GetArtGroupId(m_lookId);
	if (EngineComponents::GetNdGameInfo()->IsJapan())
	{
		const StringId64 lookId = StringToStringId64( spawn.GetData<String>(SID("japan-art-group"), String()).GetString() );
		m_lookId = lookId != INVALID_STRING_ID_64 ? lookId : m_lookId;
	}

	m_numAnimateLayers = MinMax(spawn.GetData(SID("num-layers-needed"), 0), 0, 1024);

	if (GetNumAllocateLayers() > m_gameObjectConfig.m_animControlMaxLayers)
	{
		m_gameObjectConfig.m_animControlMaxLayers = GetNumAllocateLayers();
	}

	Err result = ParentClass::Init(spawn);
	if (result.Failed())
	{
		return result;
	}

	// this check needs to happen after parents init so that the spawner data
	// is available for overriding the skeleton id in subclasses
	if (GetSkeletonId() == INVALID_SKELETON_ID)
	{
		return Err::kErrSkeletonNotLoaded;
	}
	
	m_effSurfaceType = SID("stone-asphalt");

	if (GetAnimControl())
	{
		m_pSsAnimateCtrl = NDI_NEW SsAnimateController*[GetNumAnimateLayers()];
		for (int i = 0; i < GetNumAnimateLayers(); i++)
		{
			StringId64 layerName = SID("base");
			bool additive = false;
			if (i > 0)
			{
				StringId64 prefix;

				if (i & 1)
				{
					prefix = SID("partial-slerp-");
				}
				else
				{
					prefix = SID("partial-add-");
					additive = true;
				}

				layerName = StringId64ConcatInteger(prefix, (i-1)/2);
			}

			m_pSsAnimateCtrl[i] = CreateAnimateCtrl(layerName, additive);

			// By default, simple animated objects don't send camera updates. You need to either:
			//  (a) add the following tag in Charter to enable them, or
			//  (b) explicitly enable them in script via (enable-camera-updates).
			bool enableScriptedCamera = (spawn.GetData<I32>(SID("EnableScriptedCamera"), 0) != 0);
			m_pSsAnimateCtrl[i]->SuppressCameraUpdates(!enableScriptedCamera);
		}
	}

	if (m_pPropControl)
	{
		m_pPropControl->Init(this, spawn, m_extraPropCapacity);
		m_pPropControl->SpawnProps();
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdDrawableObject::PostInit(const ProcessSpawnInfo& spawn)
{
	Err result = ParentClass::PostInit(spawn);

	FgAnimData* pAnimData = GetAnimData();
	if (pAnimData)
	{
		SsAnimateController* pSsAnimCtrl = GetPrimarySsAnimateController();
		if (pSsAnimCtrl)
		{
			const bool isCamera = pSsAnimCtrl->IsCamera();
			if (isCamera)
				pAnimData->SetCameraCutNotificationsEnabled(true); // cameras can always trigger camera cut notifications
		}
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SsAnimateController* NdDrawableObject::GetPrimarySsAnimateController() const
{
	if (m_pSsAnimateCtrl)
	{
		for (int i = 0; i < GetNumAnimateLayers(); i++)
		{
			SsAnimateController* pSsAnimCtrl = m_pSsAnimateCtrl[i];
			if (pSsAnimCtrl && pSsAnimCtrl->GetLayerId() == SID("base"))
			{
				return pSsAnimCtrl;
			}
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdPropControl* NdDrawableObject::CreatePropControl(const SpawnInfo& spawn, const ResolvedLook& resolvedLook)
{
	if (resolvedLook.m_numPropOrSetIds > 0)
	{
		return EngineComponents::GetNdGameInfo()->CreateBasicPropControl();
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SsAnimateController* NdDrawableObject::CreateAnimateCtrl(StringId64 layerName, bool additive)
{
	return NDI_NEW SsAnimateController(*this, layerName, additive, SsAnimateController::kSimpleLayer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdDrawableObject::InitializeDrawControl(const ProcessSpawnInfo& spawn, const ResolvedLook& resolvedLook)
{
	// cache the list of resolved prop set ids
	m_pPropControl = CreatePropControl(static_cast<const SpawnInfo&>(spawn), resolvedLook);
	if (m_pPropControl)
	{
		m_pPropControl->RequestPropsFromLook(resolvedLook, this);
	}

	const Err result = ParentClass::InitializeDrawControl(spawn, resolvedLook);
	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdDrawableObject::~NdDrawableObject()
{
	if (m_pPropControl)
	{
		m_pPropControl->Destroy();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdDrawableObject::OnKillProcess()
{
	if (m_pSsAnimateCtrl)
	{
		for (int i = 0; i < GetNumAnimateLayers(); i++)
		{
			if (m_pSsAnimateCtrl[i])
			{
				m_pSsAnimateCtrl[i]->OnKillProcess();
			}
		}
	}
	
	ParentClass::OnKillProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdDrawableObject::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocateObject(m_pPropControl, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pPropControl, deltaPos, lowerBound, upperBound);

	if (m_pSsAnimateCtrl)
	{
		for (int i = 0; i < GetNumAnimateLayers(); i++)
		{
			RelocatePointer(m_pSsAnimateCtrl[i], deltaPos, lowerBound, upperBound);
		}

		RelocatePointer(m_pSsAnimateCtrl, deltaPos, lowerBound, upperBound);
	}

	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdDrawableObject::PreUpdate()
{
	ParentClass::PreUpdate();

	if (m_pSsAnimateCtrl && GetAnimControl())
	{
		for (int i = 0; i < GetNumAnimateLayers(); i++)
			m_pSsAnimateCtrl[i]->PostScriptUpdate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdDrawableObject::PostAnimUpdate_Async()
{
	ParentClass::PostAnimUpdate_Async();

	Locator replayLoc = GetLocator();

	PROFILE_STRINGID(Processes, ProcessDrawable_Active_PostAnimUpdate_Async, GetTypeNameId());
	if (m_pSsAnimateCtrl && GetAnimControl() && !m_suppressSsAnimatePostAnimUpdate)
	{
		for (int i = 0; i < GetNumAnimateLayers(); i++)
			m_pSsAnimateCtrl[i]->PostAnimUpdate();
	}

	replayLoc = GetLocator();
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NdDrawableObject::GetAnimationPhaseDirect(I32 layer/* = 0*/, StringId64 animId/* = INVALID_STRING_ID_64*/)
{
	NdDrawableSsAnimateEventContext context(this);
	return SsAnimateController::GetAnimationPhase(layer, animId, context);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdDrawableObject::HandlePlayAnimEvent(Event& event)
{
	NdDrawableSsAnimateEventContext context(this);
	if (SsAnimateController::TryHandleEvent(event, context))
		return true;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Virtual method used to draw something when the "Show Processes" menu option is enabled.
///
void NdDrawableObject::DebugShowProcess(ScreenSpaceTextPrinter& printer) const
{
	STRIP_IN_FINAL_BUILD;

	ParentClass::DebugShowProcess(printer);

	const Point& pos = GetLocator().Pos();
	float dist = LengthSqr(pos - g_mainCameraInfo[0].GetPosition());
	if (dist < 25.0f * 25.0f)
	{
		printer.PrintText(kColorGreen, "%s", DevKitOnly_StringIdToString(GetLookId()));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdDrawableObject::GetLookId() const
{
	return m_lookId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SsAnimateController* NdDrawableObject::GetAnimateCtrl(int index) const
{
	if (m_pSsAnimateCtrl)
	{
		ALWAYS_ASSERT(index >= 0 && index < GetNumAnimateLayers() && "If you are trying to animate a pickup, add the tag allow-animation = 1");
		return m_pSsAnimateCtrl[index];
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int NdDrawableObject::GetNumAnimateLayers() const
{
	// Temp: for now we have double the number of partial layers until it's possible to switch layers between slerp and additive
	return (2 * m_numAnimateLayers) + 1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int NdDrawableObject::GetNumAllocateLayers() const
{
	return GetNumAnimateLayers();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdDrawableObject::IsSimpleAnimating() const
{
	SsAnimateController* pSsAnimCtrl = GetAnimateCtrl(0);
	if (pSsAnimCtrl && pSsAnimCtrl->GetAnimChain())
	{
		const bool isAnimating = pSsAnimCtrl->IsAnimating();
		const bool isAnimatingThisFrame = pSsAnimCtrl->IsAnimatingThisFrame();

#ifdef JGREGORY
		if (isAnimating != isAnimatingThisFrame)
		{
			MsgCinematic(FRAME_NUMBER_FMT "Interesting... object '%s': (isAnimating=%d) != (isAnimatingThisFrame=%d)\n",
				FRAME_NUMBER,
				GetName(), isAnimating, isAnimatingThisFrame);
		}
#endif
		if (isAnimating || isAnimatingThisFrame)
		{
			// this is here ONLY to support the cinematic feature that allows some objects to "take a break"
			// during certain sequences of a cinematic... During these "break" sequences they need to pretend
			// to be animating even though they are not, so they won't be put into DisableUpdates() mode.
			// The test for AnimChain is here to identify cinematic objects; other objects should be able to
			// disable animation if their playbackRate is 0.0.
			return true;
		}
	}

	AnimControl* pAnimControl = GetAnimControl();
	if (pAnimControl)
	{
		AnimDummyInstance* pDummyInst = pAnimControl->GetDummyInstance();
		if (pDummyInst)
			// this is here ONLY to support the cinematic feature that allows some objects to "take a break"
			return true;

		for (int i = 0; i < pAnimControl->GetNumLayers(); ++i)
		{
			AnimSimpleLayer* pSimpleLayer = pAnimControl->GetSimpleLayerByIndex(i);
			if (pSimpleLayer)
			{
				int numInstances = pSimpleLayer->GetNumInstances();
				if (numInstances > 1)
					return true;

				if (numInstances == 1)
				{
					// we need to consider simple objects playing one frame anims relative to an apRef in case the apRef moves, even if the 

					AnimSimpleInstance* pInstance = pSimpleLayer->GetInstance(0);
					if (pInstance && (pInstance->GetFrameCount() > 1 || pInstance->IsAlignedToActionPackOrigin()))
					{
						if (pInstance->GetAnim().ToArtItem() != nullptr || pInstance->GetAnimId() == INVALID_STRING_ID_64)
						{
							if (pInstance->GetPlaybackRate() != 0.0f || pInstance->IsAlignedToActionPackOrigin())
							{
								if (pInstance->IsLooping())
									return true;

								if (pInstance->GetPhase() < 1.0f && pInstance->GetPlaybackRate() > 0.0f)
									return true;

								if (pInstance->GetPhase() > 0.0f && pInstance->GetPlaybackRate() < 0.0f)
									return true;
							}
						}
					}
				}
			}
		} 
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdDrawableObject::PopulateEffectControlSpawnInfo(const EffectAnimInfo* pEffectAnimInfo,
													  EffectControlSpawnInfo& effInfo)
{
	IEffectControl *pEffectCtrl = GetEffectControl();
	pEffectCtrl->PopulateSpawnInfo(effInfo, pEffectAnimInfo, this);
	effInfo.m_handSurfaceType[kLeftArm] = m_effSurfaceType;
	effInfo.m_handSurfaceType[kRightArm] = m_effSurfaceType;
	effInfo.m_footSurfaceType[kLeftLeg] = m_effSurfaceType;
	effInfo.m_footSurfaceType[kRightLeg] = m_effSurfaceType;

	effInfo.m_bodyImpactChestSurfaceType = m_effSurfaceType;
	effInfo.m_bodyImpactBackSurfaceType = m_effSurfaceType;

	effInfo.m_leftShoulderImpactSurfaceType = m_effSurfaceType;
	effInfo.m_rightShoulderImpactSurfaceType = m_effSurfaceType;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdDrawableObject::EventHandler(Event& event)
{
	if (HandlePlayAnimEvent(event))
		return;

	if (m_pPropControl && IsCharClothEvent(event))
	{
		const I32F numProps = m_pPropControl->GetNumProps();
		for (I32F i = 0; i < numProps; ++i)
		{
			if (NdAttachableObject *pProp = m_pPropControl->GetPropObject(i))
			{
				if (pProp->IsType(SID("ProcessCharCloth")))
				{
					pProp->DispatchEvent(event, false);
				}
			}
		}
	}

	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("play-terrain"):
		{
			StringId64 surfaceType = event.Get(0).GetStringId();
			m_effSurfaceType = surfaceType;
		}
		break;
	case SID_VAL("get-play-terrain"):
		{
			event.SetResponse(m_effSurfaceType.m_primary[0]);
		}
		break;
	case SID_VAL("play-simple-gesture"):
		{
			SsAnimateParams params;
			params.m_layer = DC::kAnimateLayerPartial;
			params.m_fadeInSec = 0.2f;
			params.m_fadeOutSec = 0.2f;

			StringId64 animBase = event.Get(0).GetStringId();
			StringId64 animPart = StringId64Concat(animBase, "--part");
			StringId64 animAdd = StringId64Concat(animBase, "--add");

			const ArtItemAnim* pAnimPart = GetAnimControl()->LookupAnim(animPart).ToArtItem();
			const ArtItemAnim* pAnimAdd = GetAnimControl()->LookupAnim(animAdd).ToArtItem();

			if (pAnimPart)
			{
				params.m_nameId = animPart;
				Event evt(SID("play-animation"), event.GetSender());
				evt.PushParam(BoxedSsAnimateParams(&params));
				NdDrawableSsAnimateEventContext context(this);
				SsAnimateController::HandleEventAnimate(evt, context);
			}

			if (pAnimAdd)
			{
				params.m_nameId = animAdd;
				Event evt(SID("play-animation"), event.GetSender());
				evt.PushParam(BoxedSsAnimateParams(&params));
				NdDrawableSsAnimateEventContext context(this);
				SsAnimateController::HandleEventAnimate(evt, context);
			}

			if (!pAnimPart && !pAnimAdd)
				MsgConScriptError("Gesture '%s' not found for '%s'\n", DevKitOnly_StringIdToString(animBase), GetName());
		}
		break;
	default:
		ParentClass::EventHandler(event);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessSnapshot* NdDrawableObject::AllocateSnapshot() const
{
#if REDUCED_SNAPSHOTS
	if (m_pTargetable == nullptr && !m_goFlags.m_isPoiLookAt)
		return nullptr;
#endif
	ProcessSnapshot* pSnapshot = ParentClass::AllocateSnapshot();
	ALWAYS_ASSERT(pSnapshot);

	return pSnapshot;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdDrawableObject::RefreshSnapshot(ProcessSnapshot* pSnapshot) const
{
	//PROFILE(Processes, Drawable_RefreshSnapshot);

	ALWAYS_ASSERT(pSnapshot);
	ParentClass::RefreshSnapshot(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdDrawableObject::Active::Update()
{
	NdDrawableObject &self = Self();
	ParentClass::Update();

	//PROFILE_STRINGID(Processes, NdDrawableObject_Active_Update, self.GetTypeNameId());
	self.UpdateShimmer();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdDrawableObject::NotifyDebugSelected()
{
	ParentClass::NotifyDebugSelected();

	if (g_animRecorderOptions.m_autoSelectAttachedObjects)
	{
		DebugSelection& selection = DebugSelection::Get();
		if (NdPropControl *pPropControl = GetNdPropControl())
		{
			U32 numProps = pPropControl->GetNumProps();
			for (U32 i = 0; i < numProps; ++i)
			{
				NdAttachableObjectHandle attachable = pPropControl->GetPropHandle(i);
				selection.SelectId(attachable.GetUserId());
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdDrawableObject::HideJoint(JointModifiers* pJointModifiers, StringId64 jointId)
{
	I32 jointIndex = FindJointIndex(jointId);
	if (jointIndex == -1)
		return;

	if (JointModifierData* pJmData = pJointModifiers->GetJointModifierData())
	{
		if (JointModifierData::HideJointData* pOutputData = pJmData->GetWeaponModData())
		{
			for (int i = 0; i < pOutputData->m_numHiddenJoints; i++)
			{
				if (pOutputData->m_hiddenJointIndices[i] == jointIndex)
					return;
			}

			ASSERT(pOutputData->m_numHiddenJoints < JointModifierData::HideJointData::kMaxHiddenJoints);
			if (pOutputData->m_numHiddenJoints < JointModifierData::HideJointData::kMaxHiddenJoints)
			{
				m_goFlags.m_hasHiddenJoints = true;
				pOutputData->m_hiddenJointIndices[pOutputData->m_numHiddenJoints++] = jointIndex;

				pJointModifiers->GetModifier(kWeaponModModifier)->Enable();
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdDrawableObject::ApplyVisualMods(JointModifiers* pJointModifiers,
									   const ListArray<WeaponUpgrades::JointMod>& jointMods,
									   const ListArray<WeaponUpgrades::ShaderParams>& shaderParams)
{
	// joints modification
	m_goFlags.m_hasHiddenJoints = false;
	if (JointModifierData* pJmData = pJointModifiers->GetJointModifierData())
	{
		if (JointModifierData::HideJointData* pOutputData = pJmData->GetWeaponModData())
		{
			pOutputData->m_numHiddenJoints = 0;
			for (I32F iJoint = 0; iJoint < jointMods.Size(); ++iJoint)
			{
				if (jointMods[iJoint].m_on)
					continue;

				I32 callJoint = FindJointIndex(jointMods[iJoint].jointId);
				if (callJoint != -1)
				{
					m_goFlags.m_hasHiddenJoints = true;
					pOutputData->m_hiddenJointIndices[pOutputData->m_numHiddenJoints++] = callJoint;
				}
				else
				{
					MsgWarn("weapon mods: (%s) couldn't find joint %s\n", DevKitOnly_StringIdToString(GetResolvedLookId()), DevKitOnly_StringIdToString(jointMods[iJoint].jointId));
				}
			}
			ALWAYS_ASSERT(pOutputData->m_numHiddenJoints <= JointModifierData::HideJointData::kMaxHiddenJoints);
		}
	}

	if (m_goFlags.m_hasHiddenJoints)
		pJointModifiers->GetModifier(kWeaponModModifier)->Enable();
	else
		pJointModifiers->GetModifier(kWeaponModModifier)->Disable();

	// shader modification
	{
		for (int ii = 0; ii < shaderParams.Size(); ii++)
		{
			StringId64 paramId = shaderParams[ii].paramId;
			float val = shaderParams[ii].value;

			InstanceParameterSpecFromId paramSpec(paramId, InstanceParameterSpecFromId::kRequireComponentSpecifier);

			bool succeeded = false;
			if (paramSpec.m_key != kInstParamCount)
			{
				if (paramSpec.m_componentIndex == InstanceParameterSpecFromId::kComponentIndexAny)
					succeeded = SetShaderInstanceParam(paramSpec.m_key, val);
				else
					succeeded = SetShaderInstanceParam(paramSpec.m_key, val, paramSpec.m_componentIndex);
			}

			if (!succeeded)
			{
				MsgWarn("weapon-mods: failed to set %s in %s\n", DevKitOnly_StringIdToString(paramId), GetName());
			}
		}
	}
}
