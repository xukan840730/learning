/*
 * Copyright (c) 2006 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-light-object.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/camera/camera-final.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/look.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/resource/resource-table-data.h"
#include "ndlib/settings/render-settings.h"

#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/render/light/light-update.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "gamelib/state-script/ss-animate.h"

extern LightEnvResult* GetCachedPlayerLightEnv();

/// --------------------------------------------------------------------------------------------------------------- ///
// NdLightSsAnimateEventContext - For having NdLightObject interface
//		with SSAnimate and SSAnimateSimple
/// --------------------------------------------------------------------------------------------------------------- ///
struct NdLightSsAnimateEventContext : public ISsAnimateEventContext
{
	NdLightObject* m_pObject;

	NdLightSsAnimateEventContext(NdLightObject *theObject) : m_pObject(theObject) { }

	virtual NdGameObject* GetOwner() const override
	{
		return m_pObject;
	}

	virtual void GetControllerIndexRanges(ControllerIndexRanges& ranges) const override
	{
		ranges.iScriptFullBodyController = 0;

		ranges.iBeginScriptGestureControllers = 1;
		ranges.iEndScriptGestureControllers = m_pObject->GetNumAnimateLayers();

		ranges.iBeginScriptControllers = 0;
		ranges.iEndScriptControllers = m_pObject->GetNumAnimateLayers();
	}

	virtual SsAnimateController* GetController(I32F i) const override
	{
		ALWAYS_ASSERT(m_pObject);
		ALWAYS_ASSERT(i >= 0 && i < m_pObject->GetNumAnimateLayers());
		return m_pObject->GetAnimateCtrl(i);
	}

	virtual SsAnimateController* GetControllerByLayerName(StringId64 layerName) const override
	{
		return nullptr;
	}
};


/// --------------------------------------------------------------------------------------------------------------- ///
// NdLightObject
/// --------------------------------------------------------------------------------------------------------------- ///
PROCESS_REGISTER(NdLightObject, NdGameObject);
STATE_REGISTER(NdLightObject, Active, kPriorityNormal);
FROM_PROCESS_DEFINE(NdLightObject);

/// --------------------------------------------------------------------------------------------------------------- ///
NdLightObject::NdLightObject()
: ParentClass()
, m_pSsAnimateCtrl(nullptr)
, m_numAnimateLayers(0)
, m_lookId(INVALID_STRING_ID_64)
, m_attachmentManagedManually(false)
, m_suppressSsAnimatePostAnimUpdate(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::ResolveLook(ResolvedLook& resolvedLook, const SpawnInfo& spawn, StringId64 desiredActorOrLookId)
{
	PROFILE(Processes, NdGO_ResolveLook);

	LookPointer pLook = GetLook(desiredActorOrLookId);

	// ... resolve our look into a list of actual meshes and prop sets
	if (spawn.m_pParentLookSeed)
	{
		resolvedLook.Resolve(spawn.m_pParentLookSeed->m_userId,
			spawn.m_pParentLookSeed->m_collectionRandomizer,
			spawn.m_pParentLookSeed->m_tintRandomizer,
			desiredActorOrLookId,
			pLook,
			GetLook(spawn.m_pParentLookSeed->m_lookId),
			spawn.m_pTintArray,
			false,
			nullptr);
	}
	else
	{
		resolvedLook.Resolve(GetUserIdUsedForLook(), m_lookCollectionRandomizer, m_lookTintRandomizer, desiredActorOrLookId, pLook, nullptr, spawn.m_pTintArray, false, nullptr);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U8 NdLightObject::FindLightTableIndex(const Level* pActor) const
{
	U8 lightTableIndex = 0;
	for (; lightTableIndex < pActor->GetNumLightTables(); lightTableIndex++)
	{
		if (m_lookId == StringToStringId64(pActor->GetLightTable(lightTableIndex)->m_name))
			break;
	}

	return lightTableIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U8 NdLightObject::FindAnimatedLightIndex(const Level* pActor, U8 defaultIndex) const
{
	U8 animatedLightIndex = 0;
	bool bFound = false;
	for (; animatedLightIndex < pActor->GetNumLightTables(); animatedLightIndex++)
	{
		if (m_lookId == pActor->GetAnimatedLight(0, animatedLightIndex)->m_lightTableName)
		{
			bFound = true;
			break;
		}
	}

	return bFound ? animatedLightIndex : defaultIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const StringId64 NdLightObject::GetLightTableActorNameId() const
{
	const ResourceTable::SkeletonData::Entry* pSkelEntry = ResourceTable::g_pSkelData->LookupEntry(m_skeletonId);
	return pSkelEntry->m_containingPackageNameId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline static void GetFloatAttrOffset(int& attrOffset, const SkelComponentDesc* pFloatDescs, const char* pAttrName, U16 numFloatChannels)
{
	attrOffset = -1;

	for (U32 i = 0; i < numFloatChannels; i++)
	{
		if (strstr(pFloatDescs[i].m_pName, pAttrName) && *(strstr(pFloatDescs[i].m_pName, pAttrName) + strlen(pAttrName)) == 0)	// make sure the float attribute name ends with the attribute name
		{
			attrOffset = i;
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdLightObject::Init(const ProcessSpawnInfo& spawnInfo)
{
	PROFILE(Processes, NdLightObject_Init);

	const SpawnInfo& spawn = static_cast<const SpawnInfo&>(spawnInfo);

	m_suppressSsAnimatePostAnimUpdate = false;
	m_attachmentManagedManually = false;
	m_resetTweakedLights = false;

	m_lookId = spawn.GetArtGroupId(m_lookId);

	m_numAnimateLayers = MinMax(spawn.GetData(SID("num-layers-needed"), 2), 2, 16);

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
	
	if (AnimControl* pAnimControl = GetAnimControl())
	{
		const Level* pActor = EngineComponents::GetLevelMgr()->GetActor(GetLightTableActorNameId());

		// look for the light table and animated light in the actor that corresponds to this light skel
		if (pActor && pActor->GetNumLightTables() > 0)	// pActor is null for script spawned ProcessDynLight; pActor->GetNumLightTables() is 0 for manually placed light spawner in Charter
		{
			U8 lightTableIndex = FindLightTableIndex(pActor);
			LightTable* pLightTable = pActor->GetLightTable(lightTableIndex);
			U8 animatedLightIndex = pLightTable->GetNumLights() ? FindAnimatedLightIndex(pActor, lightTableIndex) : lightTableIndex;

			// setup the link between the light table and the light skel
			SetLightTable(pLightTable);
			SetTweakedLightTable(pActor->GetTweakedLightTable(lightTableIndex));
			SetAnimatedLights(pActor->GetAnimatedLights(animatedLightIndex));

			if (pActor->Foreground())
			{
				ScopedTempAllocator jj(FILE_LINE_FUNC);

				for (int iLight = 0; iLight < pLightTable->GetNumLights(); ++iLight)
				{
					const AnimatedLight* pLight = pActor->GetAnimatedLight(iLight, animatedLightIndex);
					LightType lightType = (LightType)pLight->m_type;
					char* pLightJointName = NDI_NEW char[strlen(pLight->m_pStrName) + 1];
					PipesToUnderscores(pLightJointName, pLight->m_pStrName);

					for (U32 iJoint = 0; iJoint < pAnimControl->GetJointCache()->GetNumTotalJoints(); iJoint++)
					{
						if (strcmp(pAnimControl->GetJointName(iJoint), pLightJointName + 1) == 0)
						{
							pAnimControl->GetJointCache()->SetJointLightType(iJoint, lightType);
							pAnimControl->GetJointCache()->SetJointLightIndex(iJoint, pLight->m_index);

							break;
						}
					}
				}
			}
			else
			{
				SYSTEM_ASSERTF(animatedLightIndex < pActor->GetNumLightTables(), ("This level %s needs to be build to fix this assert\n", pActor->GetName()));

				for (int iLight = 0; iLight < pLightTable->GetNumLights(); ++iLight)
				{
					const AnimatedLight* pLight = pActor->GetAnimatedLight(iLight, animatedLightIndex);
					if (strcmp(spawnInfo.GetName(), pLight->m_pStrName) == 0)
					{
						MatchLightInTable((LightType)pLight->m_type, pLight->m_index);
						pAnimControl->GetJointCache()->SetJointLightType(0, (LightType)pLight->m_type);
						pAnimControl->GetJointCache()->SetJointLightIndex(0, pLight->m_index);
						break;
					}
				}
			}

			const SkelComponentDesc* pFloatDescs = pAnimControl->GetArtItemSkel()->m_pFloatDescs;
			U16 numFloatChannels = pAnimControl->GetJointCache()->GetNumFloatChannels() / pAnimControl->GetJointCache()->GetNumTotalJoints();
			GetFloatAttrOffset(m_colorROffset, pFloatDescs, "colorR", numFloatChannels);
			GetFloatAttrOffset(m_colorGOffset, pFloatDescs, "colorG", numFloatChannels);
			GetFloatAttrOffset(m_colorBOffset, pFloatDescs, "colorB", numFloatChannels);
			GetFloatAttrOffset(m_coneAngleOffset, pFloatDescs, "coneAngle", numFloatChannels);
			GetFloatAttrOffset(m_penumbraAngleOffset, pFloatDescs, "penumbraAngle", numFloatChannels);
			GetFloatAttrOffset(m_shadowBlurScaleOffset, pFloatDescs, "shadowBlurScale", numFloatChannels);
			GetFloatAttrOffset(m_shapeRadiusOffset, pFloatDescs, "shapeRadius", numFloatChannels);
			GetFloatAttrOffset(m_volIntensityOffset, pFloatDescs, "volumetricIntensity", numFloatChannels);
			GetFloatAttrOffset(m_shadowSlopeOffset, pFloatDescs, "shadowSlope", numFloatChannels);
			GetFloatAttrOffset(m_shadowConstOffset, pFloatDescs, "shadowConst", numFloatChannels);
			GetFloatAttrOffset(m_nearClipDistOffset, pFloatDescs, "nearClipDist", numFloatChannels);
			GetFloatAttrOffset(m_farClipDistOffset, pFloatDescs, "farClipDist", numFloatChannels);
			GetFloatAttrOffset(m_useRayTraceShadowsOffset, pFloatDescs, "useRayTraceShadows", numFloatChannels);
			GetFloatAttrOffset(m_decayRateOffset, pFloatDescs, "decayRate", numFloatChannels);
			GetFloatAttrOffset(m_startDistanceOffset, pFloatDescs, "startDistance", numFloatChannels);
			GetFloatAttrOffset(m_startRangeOffset, pFloatDescs, "startRange", numFloatChannels);
			GetFloatAttrOffset(m_radiusOffset, pFloatDescs, "radius", numFloatChannels);
			GetFloatAttrOffset(m_skipBgShadowOffset, pFloatDescs, "skipBgShadow", numFloatChannels);
			GetFloatAttrOffset(m_emitDiffuseOffset, pFloatDescs, "emitDiffuse", numFloatChannels);
			GetFloatAttrOffset(m_emitSpecularOffset, pFloatDescs, "emitSpecular", numFloatChannels);
			GetFloatAttrOffset(m_noLightBgOffset, pFloatDescs, "noLightBg", numFloatChannels);
			GetFloatAttrOffset(m_noLightFgOffset, pFloatDescs, "noLightFg", numFloatChannels);
			GetFloatAttrOffset(m_castsSssOffset, pFloatDescs, "castsSss", numFloatChannels);
			GetFloatAttrOffset(m_enableBounceOffset, pFloatDescs, "enableBounce", numFloatChannels);
			GetFloatAttrOffset(m_minRoughnessOffset, pFloatDescs, "minRoughness", numFloatChannels);
			GetFloatAttrOffset(m_charOnlyOffset, pFloatDescs, "charOnly", numFloatChannels);
			GetFloatAttrOffset(m_eyesOnlyOffset, pFloatDescs, "eyesOnly", numFloatChannels);
			GetFloatAttrOffset(m_hairOnlyOffset, pFloatDescs, "hairOnly", numFloatChannels);
			GetFloatAttrOffset(m_particleOnlyOffset, pFloatDescs, "particleOnly", numFloatChannels);
			GetFloatAttrOffset(m_particleOffset, pFloatDescs, "particle", numFloatChannels);
			GetFloatAttrOffset(m_propOnlyOffset, pFloatDescs, "propOnly", numFloatChannels);
			GetFloatAttrOffset(m_noHairOffset, pFloatDescs, "noHair", numFloatChannels);
			GetFloatAttrOffset(m_noTransmittanceOffset, pFloatDescs, "noTransmittance", numFloatChannels);
			GetFloatAttrOffset(m_specScaleOffset, pFloatDescs, "specScale", numFloatChannels);
			GetFloatAttrOffset(m_noLightCharOffset, pFloatDescs, "noLightChar", numFloatChannels);
			GetFloatAttrOffset(m_projWidthOffset, pFloatDescs, "projWidth", numFloatChannels);
			GetFloatAttrOffset(m_projHeightOffset, pFloatDescs, "projHeight", numFloatChannels);
			GetFloatAttrOffset(m_goboUScrollSpeedOffset, pFloatDescs, "goboUScrollSpeed", numFloatChannels);
			GetFloatAttrOffset(m_goboVScrollSpeedOffset, pFloatDescs, "goboVScrollSpeed", numFloatChannels);
			GetFloatAttrOffset(m_goboUTileFactorOffset, pFloatDescs, "goboUTileFactor", numFloatChannels);
			GetFloatAttrOffset(m_goboVTileFactorOffset, pFloatDescs, "goboVTileFactor", numFloatChannels);
			GetFloatAttrOffset(m_goboAnimSpeedOffset, pFloatDescs, "goboAnimSpeed", numFloatChannels);
			GetFloatAttrOffset(m_volInnerFalloffOffset, pFloatDescs, "volInnerFalloff", numFloatChannels);
			GetFloatAttrOffset(m_volInnerIntensityOffset, pFloatDescs, "volInnerIntensity", numFloatChannels);
			GetFloatAttrOffset(m_minShadowBlurScaleOffset, pFloatDescs, "minShadowBlurScale", numFloatChannels);
			GetFloatAttrOffset(m_maxShadowBlurDistOffset, pFloatDescs, "maxShadowBlurDist", numFloatChannels);
			GetFloatAttrOffset(m_variableShadowBlurOffset, pFloatDescs, "variableShadowBlur", numFloatChannels);
		}

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

	m_pAttachmentCtrl = NDI_NEW AttachmentCtrl(*this);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SsAnimateController* NdLightObject::GetPrimarySsAnimateController() const
{
	for (int i = 0; i < GetNumAnimateLayers(); i++)
	{
		SsAnimateController* pSsAnimCtrl = m_pSsAnimateCtrl[i];
		if (pSsAnimCtrl && pSsAnimCtrl->GetLayerId() == SID("base"))
		{
			return pSsAnimCtrl;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SsAnimateController* NdLightObject::CreateAnimateCtrl(StringId64 layerName, bool additive)
{
	return NDI_NEW SsAnimateController(*this, layerName, additive, SsAnimateController::kSimpleLayer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdLightObject::~NdLightObject()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::OnKillProcess()
{
	if (m_pSsAnimateCtrl)
	{
		for (int i = 0; i < GetNumAnimateLayers(); i++)
		{
			if (m_pSsAnimateCtrl[i])
				m_pSsAnimateCtrl[i]->OnKillProcess();
		}
	}
	
	RemoveLightTable();

	ParentClass::OnKillProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	if (m_pSsAnimateCtrl)
	{
		for (int i = 0; i < GetNumAnimateLayers(); i++)
			RelocatePointer(m_pSsAnimateCtrl[i], deltaPos, lowerBound, upperBound);

		RelocatePointer(m_pSsAnimateCtrl, deltaPos, lowerBound, upperBound);
	}

	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::PreUpdate()
{
	ParentClass::PreUpdate();

	if (m_pSsAnimateCtrl && GetAnimControl())
	{
		for (int i = 0; i < GetNumAnimateLayers(); i++)
			m_pSsAnimateCtrl[i]->PostScriptUpdate();
	}
}

void NdLightObject::ResetTweakedLights()
{
	if (!m_pSsAnimateCtrl[0]->IsAnimDisabled())
		m_resetTweakedLights = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::PostAnimUpdate_Async()
{
	ParentClass::PostAnimUpdate_Async();

	PROFILE_STRINGID(Processes, NdLightObject_Active_PostAnimUpdate_Async, GetTypeNameId());
	if (m_pSsAnimateCtrl && GetAnimControl() && !m_suppressSsAnimatePostAnimUpdate)
	{
		for (int i = 0; i < GetNumAnimateLayers(); i++)
			m_pSsAnimateCtrl[i]->PostAnimUpdate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::PostJointUpdate_Async()
{
	ParentClass::PostJointUpdate_Async();

	if (m_pSsAnimateCtrl[0]->IsAnimDisabled())
		return;

	if (!IsSimpleAnimating())
	{
		DisableAnimation();
		return;
	}

	if (GetStateId() == SID("Off"))
		return;

	// update the runtime lights from joint animations, replacing the functionalities in CinematicProcess::UpdateLightsNoLock
	const JointCache* pJointCache = GetAnimControl()->GetJointCache();
	ExtraLightInfoMgr extraLightInfoMgr(*m_pLightTable);
	ExtraLightInfoMgr tweakedExtraLightInfoMgr(*m_pTweakedLightTable);
	U16 numFloatChannels = pJointCache->GetNumFloatChannels() / pJointCache->GetNumTotalJoints();

	for (U32 i = 0; i < pJointCache->GetNumTotalJoints(); i++)
	{
		const Locator jointLocatorWs = GetRealJointLocatorWs(pJointCache, i);
		LightType lightType = pJointCache->GetJointLightType(i);
		U32 lightIndex = pJointCache->GetJointLightIndex(i);
		BaseLight* pBase = m_pLightTable->GetLightByType(lightType, lightIndex);
		ExtraLightInfo extraLightInfo;
		extraLightInfoMgr.GetExtraLightInfo(extraLightInfo, lightType, lightIndex);
		pBase->SetEnabled(true);
		pBase->m_pos = (Vec3)jointLocatorWs.GetTranslation();
		pBase->m_dir = (Vec3)GetLocalZ(jointLocatorWs.GetRotation());

		const float* pFloatAttrs = pJointCache->GetOutputControls() + (pJointCache->GetNumTotalJoints() - 1 - i) * numFloatChannels;

		pBase->m_radius = pFloatAttrs[m_radiusOffset];
		float colorR = pFloatAttrs[m_colorROffset];
		float colorG = pFloatAttrs[m_colorGOffset];
		float colorB = pFloatAttrs[m_colorBOffset];
		Vec3 color = Vec3(colorR, colorG, colorB);
		float intensity = Length(color);
		bool turnOffLight = intensity <= NDI_FLT_EPSILON || pBase->m_radius <= NDI_FLT_EPSILON;
		pBase->m_color = turnOffLight ? Vec3(kZero) : color / intensity;
		pBase->m_intensity = turnOffLight ? 0.0f : intensity;

		if (GetRenderSettingsBoolCurrentValue(SID("lighting:hack-dynamic-env-lights-enable")))
		{
			Vec4 envColorAndIntensityMultipliers = GetEnvironmentColorAndIntensityMultipliers();
			pBase->m_color = pBase->m_color * Vec3(envColorAndIntensityMultipliers.X(), envColorAndIntensityMultipliers.Y(), envColorAndIntensityMultipliers.Z());
			pBase->m_intensity *= envColorAndIntensityMultipliers.W();
		}

		SetIntensity(pBase->m_intensity);

		pBase->m_shapeRadius = pFloatAttrs[m_shapeRadiusOffset];
		pBase->m_startDistance = pFloatAttrs[m_startDistanceOffset];
		pBase->m_startRange = pFloatAttrs[m_startRangeOffset];
		pBase->m_minRoughness = pFloatAttrs[m_minRoughnessOffset];
		pBase->m_specScale = pFloatAttrs[m_specScaleOffset];
		extraLightInfo.m_shadowBlurScale = pFloatAttrs[m_shadowBlurScaleOffset];
		extraLightInfo.m_volIntensity = pFloatAttrs[m_volIntensityOffset];
		extraLightInfo.m_shadowSlope = pFloatAttrs[m_shadowSlopeOffset];
		extraLightInfo.m_shadowConst = pFloatAttrs[m_shadowConstOffset];
		extraLightInfo.m_nearClipDist = pFloatAttrs[m_nearClipDistOffset];
		extraLightInfo.m_farClipDist = pFloatAttrs[m_farClipDistOffset];

		if (TRUE_IN_FINAL_BUILD(m_volInnerFalloffOffset >= 0))	// newly added member, needs protection until all levels/actors are built
			extraLightInfo.m_volInnerFalloff = pFloatAttrs[m_volInnerFalloffOffset];

		if (TRUE_IN_FINAL_BUILD(m_volInnerIntensityOffset >= 0))
			extraLightInfo.m_volInnerIntensity = pFloatAttrs[m_volInnerIntensityOffset];

		if (TRUE_IN_FINAL_BUILD(m_minShadowBlurScaleOffset >= 0))
			extraLightInfo.m_minShadowBlurScale = pFloatAttrs[m_minShadowBlurScaleOffset];

		if (TRUE_IN_FINAL_BUILD(m_maxShadowBlurDistOffset >= 0))
			extraLightInfo.m_maxShadowBlurDist = pFloatAttrs[m_maxShadowBlurDistOffset];

		float useRayTranceShadow = pFloatAttrs[m_useRayTraceShadowsOffset];
		if (useRayTranceShadow <= 0.f)
			pBase->SetCastShadow(false);
		else if (useRayTranceShadow >= 1.f)
			pBase->SetCastShadow(true);

		if (TRUE_IN_FINAL_BUILD(m_variableShadowBlurOffset >= 0))	// newly added member, needs protection until all levels/actors are built
		{
			float variableShadowBlur = pFloatAttrs[m_variableShadowBlurOffset];
			if (variableShadowBlur <= 0.f)
				pBase->SetVariableShadowBlurScale(false);
			else if (variableShadowBlur >= 1.f)
				pBase->SetVariableShadowBlurScale(true);
		}

		float skipBgShadow = pFloatAttrs[m_skipBgShadowOffset];
		if (skipBgShadow <= 0.f)
			pBase->SetSkipBgShadow(false);
		else if (skipBgShadow >= 1.f)
			pBase->SetSkipBgShadow(true);

		float emitDiffuse = pFloatAttrs[m_emitDiffuseOffset];
		if (emitDiffuse <= 0.f)
			pBase->SetEmitDiffuse(false);
		else if (emitDiffuse >= 1.f)
			pBase->SetEmitDiffuse(true);

		float emitSpecular = pFloatAttrs[m_emitSpecularOffset];
		if (emitSpecular <= 0.f)
			pBase->SetEmitSpecular(false);
		else if (emitSpecular >= 1.f)
			pBase->SetEmitSpecular(true);

		float noLightBg = pFloatAttrs[m_noLightBgOffset];
		if (noLightBg <= 0.f)
			extraLightInfo.m_extraFlags &= ~ExtraLightInfo::kExtraLightFlagIgnoreBackgroundGeo;
		else if (noLightBg >= 1.f)
			extraLightInfo.m_extraFlags |= ExtraLightInfo::kExtraLightFlagIgnoreBackgroundGeo;

		float noLightFg = pFloatAttrs[m_noLightFgOffset];
		if (noLightFg <= 0.f)
			extraLightInfo.m_extraFlags &= ~ExtraLightInfo::kExtraLightFlagIgnoreForegroundGeo;
		else if (noLightFg >= 1.f)
			extraLightInfo.m_extraFlags |= ExtraLightInfo::kExtraLightFlagIgnoreForegroundGeo;

		if (TRUE_IN_FINAL_BUILD(m_noLightCharOffset >= 0))	// newly added member, needs protection until all levels/actors are built
		{
			float noLightChar = pFloatAttrs[m_noLightCharOffset];
			if (noLightChar <= 0.f)
				extraLightInfo.m_extraFlags &= ~ExtraLightInfo::kExtraLightFlagIgnoreCharacterGeo;
			else if (noLightChar >= 1.f)
				extraLightInfo.m_extraFlags |= ExtraLightInfo::kExtraLightFlagIgnoreCharacterGeo;
		}

		float charOnly = pFloatAttrs[m_charOnlyOffset];
		if (charOnly <= 0.f)
			extraLightInfo.m_extraFlags &= ~ExtraLightInfo::kExtraLightFlagOnlyCharacterGeo;
		else if (charOnly >= 1.f)
			extraLightInfo.m_extraFlags |= ExtraLightInfo::kExtraLightFlagOnlyCharacterGeo;

		float eyesOnly = pFloatAttrs[m_eyesOnlyOffset];
		if (eyesOnly <= 0.f)
			pBase->SetEyesOnly(false);
		else if (eyesOnly >= 1.f)
			pBase->SetEyesOnly(true);

		float hairOnly = pFloatAttrs[m_hairOnlyOffset];
		if (hairOnly <= 0.f)
			pBase->SetHairOnly(false);
		else if (hairOnly >= 1.f)
			pBase->SetHairOnly(true);

		float particle = pFloatAttrs[m_particleOffset];
		if (particle <= 0.f)
			extraLightInfo.m_extraFlags &= ~ExtraLightInfo::kExtraLightFlagLightParticles;
		else if (particle >= 1.f)
			extraLightInfo.m_extraFlags |= ExtraLightInfo::kExtraLightFlagLightParticles;

		float propOnly = pFloatAttrs[m_propOnlyOffset];
		if (propOnly <= 0.f)
			extraLightInfo.m_extraFlags &= ~ExtraLightInfo::kExtraLightFlagOnlyPropsGeo;
		else if (propOnly >= 1.f)
			extraLightInfo.m_extraFlags |= ExtraLightInfo::kExtraLightFlagOnlyPropsGeo;

		float noHair = pFloatAttrs[m_noHairOffset];
		if (noHair <= 0.f)
			pBase->SetSkipHair(false);
		else if (noHair >= 1.f)
			pBase->SetSkipHair(true);

		float noTransmittance = pFloatAttrs[m_noTransmittanceOffset];
		if (noTransmittance <= 0.f)
			pBase->SetDisableTransmittance(false);
		else if (noTransmittance >= 1.f)
			pBase->SetDisableTransmittance(true);

		float castsSss = pFloatAttrs[m_castsSssOffset];
		if (castsSss <= 0.f)
			pBase->SetCastsSss(false);
		else if (castsSss >= 1.f)
			pBase->SetCastsSss(true);

		float enableBounce = pFloatAttrs[m_enableBounceOffset];
		if (enableBounce <= 0.f)
			pBase->SetEnableBounce(false);
		else if (enableBounce >= 1.f)
			pBase->SetEnableBounce(true);

		pBase->SetDecay(U32(pFloatAttrs[m_decayRateOffset]));

		if (lightType == kSpotLight)
		{
			SpotLight* pSpot = PunPtr<SpotLight*>(pBase);
			pSpot->m_coneAngle = pFloatAttrs[m_coneAngleOffset];
			pSpot->m_penumbraAngle = pFloatAttrs[m_penumbraAngleOffset];
		}
		else if (lightType == kProjectorLight)
		{
			ProjLight* pProj = PunPtr<ProjLight*>(pBase);
			pProj->m_up = (Vec3)GetLocalY(jointLocatorWs.GetRotation());
			pProj->m_fadeoutRange = pFloatAttrs[m_penumbraAngleOffset];

			if (pProj->m_projType != kOrthographicProjection && pProj->m_capScale == -1.f)
			{
				float coneAngle = pFloatAttrs[m_coneAngleOffset];
				pProj->m_scaleX = 2.f * tanf(coneAngle * 0.5f);
				pProj->m_scaleY = 2.f * tanf(coneAngle * 0.5f);
			}
			else
			{
				if (TRUE_IN_FINAL_BUILD(m_projWidthOffset >= 0))	// newly added member, needs protection until all levels/actors are built
					pProj->m_scaleX = pFloatAttrs[m_projWidthOffset];

				if (TRUE_IN_FINAL_BUILD(m_projHeightOffset >= 0))
					pProj->m_scaleY = pFloatAttrs[m_projHeightOffset];
			}

			if (TRUE_IN_FINAL_BUILD(m_goboUScrollSpeedOffset >= 0))
				pProj->m_maskTextureScrollSpeed[0] = pFloatAttrs[m_goboUScrollSpeedOffset];

			if (TRUE_IN_FINAL_BUILD(m_goboVScrollSpeedOffset >= 0))
				pProj->m_maskTextureScrollSpeed[1] = pFloatAttrs[m_goboVScrollSpeedOffset];

			if (TRUE_IN_FINAL_BUILD(m_goboUTileFactorOffset >= 0))
				pProj->m_maskTextureTileFactor[0] = pFloatAttrs[m_goboUTileFactorOffset];

			if (TRUE_IN_FINAL_BUILD(m_goboVTileFactorOffset >= 0))
				pProj->m_maskTextureTileFactor[1] = pFloatAttrs[m_goboVTileFactorOffset];

			if (TRUE_IN_FINAL_BUILD(m_goboAnimSpeedOffset >= 0))
				pProj->m_maskTextureAnimSpeed = pFloatAttrs[m_goboAnimSpeedOffset];
		}

		extraLightInfoMgr.SetExtraLightInfo(extraLightInfo, lightType, lightIndex);

		// live update stuffs
		bool tweaked = pBase->LiveTweaked();
		if (tweaked && g_renderOptions.m_applyLightTweaks)
		{
			BaseLight* pTweakedBase = m_pTweakedLightTable->GetLightByType(lightType, lightIndex);

			if (m_resetTweakedLights)
			{
				pBase->SetLiveTweaked(false);
				CopyLight(pTweakedBase, pBase, lightType);
				tweakedExtraLightInfoMgr.SetExtraLightInfo(extraLightInfo, lightType, lightIndex);
			}
			else
			{
				CopyLight(pBase, pTweakedBase, lightType);

				ExtraLightInfo tweakedExtraLightInfo;
				tweakedExtraLightInfoMgr.GetExtraLightInfo(tweakedExtraLightInfo, lightType, lightIndex);

				extraLightInfoMgr.SetExtraLightInfo(tweakedExtraLightInfo, lightType, lightIndex);
			}
		}
	}

	if (m_resetTweakedLights)
		m_resetTweakedLights = false;

	// now apply the render setting overrides
	const DC::ScriptLambda* pGeneralLambda = ScriptManager::Lookup<DC::ScriptLambda>(m_pSsAnimateCtrl[0]->GetAnimId(), nullptr);
	if (pGeneralLambda != nullptr)	// CinematicLightObject should automatically skip this
	{
		const ScriptValue param[] = { ScriptValue(GetUserId()), ScriptValue(m_pSsAnimateCtrl[0]->GetAnimId()) };
		ScriptManager::Eval(pGeneralLambda, ARRAY_COUNT(param), param);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::PostAnimBlending_Async()
{
	UpdateAttached();
	ParentClass::PostAnimBlending_Async();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::DebugShowProcess(ScreenSpaceTextPrinter& printer) const
{
	STRIP_IN_FINAL_BUILD;

	ParentClass::DebugShowProcess(printer);

	const Point& pos = GetLocator().Pos();
	float distSqr = LengthSqr(pos - g_mainCameraInfo[0].GetPosition());
	if (distSqr < 25.0f * 25.0f)
	{
		printer.PrintText(kColorGreen, "%s", DevKitOnly_StringIdToString(GetLookId()));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdLightObject::GetLookId() const
{
	return m_lookId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SsAnimateController* NdLightObject::GetAnimateCtrl(int index) const
{
	if (m_pSsAnimateCtrl)
	{
		ALWAYS_ASSERT(index >= 0 && index < GetNumAnimateLayers() && "If you are trying to animate a pickup, add the tag allow-animation = 1");
		return m_pSsAnimateCtrl[index];
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int NdLightObject::GetNumAnimateLayers() const
{
	return 2*m_numAnimateLayers + 3;	// Temp: for now we have double the number of partial layers until it's possible to switch layers between slerp and additive
}

/// --------------------------------------------------------------------------------------------------------------- ///
int NdLightObject::GetNumAllocateLayers() const
{
	return GetNumAnimateLayers();
}

/// --------------------------------------------------------------------------------------------------------------- ///
FgAnimData* NdLightObject::AllocateAnimData(const ArtItemSkeletonHandle artItemSkelHandle)
{
	// NOTE: Do *not* call my parent class' implementation.

	FgAnimData* pAnimData = EngineComponents::GetAnimMgr()->AllocateAnimData(GetMaxNumDrivenInstances(), this);
	
	if (pAnimData)
		pAnimData->Init(artItemSkelHandle, GetLocator().AsTransform(), JointCache::kConfigLight);

	return pAnimData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdLightObject::ShouldDebugDrawJoint(const DebugDrawFilter& filter, int globalJointIndex) const
{
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdLightObject::IsSimpleAnimating() const
{
	SsAnimateController* pSsAnimCtrl = GetAnimateCtrl(0);
	if (pSsAnimCtrl && pSsAnimCtrl->GetAnimChain())
	{
		const bool isAnimating = pSsAnimCtrl->IsAnimating();
		const bool isAnimatingThisFrame = pSsAnimCtrl->IsAnimatingThisFrame();

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
					AnimSimpleInstance* pInstance = pSimpleLayer->GetInstance(0);
					if (pInstance && pInstance->GetFrameCount() > 1)
					{
						if (pInstance->GetAnim().ToArtItem() != nullptr || pInstance->GetAnimId() == INVALID_STRING_ID_64)
						{
							if (pInstance->GetPlaybackRate() != 0.0f)
							{
								if (pInstance->IsLooping())
									return true;

								if (pInstance->GetPhase() < 1.0f && pInstance->GetPlaybackRate() > 0.0f)
									return true;

								if (pInstance->GetPhase() > 0.0f && pInstance->GetPlaybackRate() < 0.0f)
									return true;
							}
							else if (g_animOptions.m_fakePaused)
								return pInstance->GetPhase() < 1.0f && pInstance->GetPhase() > 0.0f;
						}
					}
				}
			}
		} 
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::PostEventHandler(Event& event)
{
	switch (event.GetMessage().GetValue())
	{
		case SID_VAL("on-animation-started"):
		{
			AddLightTable();
		}
		break;

		case SID_VAL("on-animation-done"):
		{
			RemoveLightTable();
		}
		break;

		default:
			break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::EventHandler(Event& event)
{
	PreEventHandler(event);

	NdLightSsAnimateEventContext context(this);
	if (SsAnimateController::TryHandleEvent(event, context))
		return;

	PostEventHandler(event);

	ParentClass::EventHandler(event);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessSnapshot* NdLightObject::AllocateSnapshot() const
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
void NdLightObject::RefreshSnapshot(ProcessSnapshot* pSnapshot) const
{
	//PROFILE(Processes, Drawable_RefreshSnapshot);

	ALWAYS_ASSERT(pSnapshot);
	ParentClass::RefreshSnapshot(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdLightObject::GetAttachmentObjectId() const
{
	if (m_pAttachmentCtrl->IsAttached())
	{
		return m_pAttachmentCtrl->GetParent().GetUserId();
	}
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdLightObject::GetAttachmentAttachOrJointId() const
{
	if (m_pAttachmentCtrl->IsAttached())
	{
		return m_pAttachmentCtrl->GetParentAttachPoint();
	}
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdLightObject::AttachToObject(StringId64 parentObjectId, StringId64 attachOrJointId, bool manual)
{
	// once you've called this once with manual==true, you must always call it with manual==true
	ALWAYS_ASSERTF(!m_attachmentManagedManually || manual, ("Attempt to do an automatic attach when the actor %s's attachments are managed manually", GetName()));

	if (manual)
		m_attachmentManagedManually = true;

	return m_pAttachmentCtrl->AttachToObject(FILE_LINE_FUNC, parentObjectId, attachOrJointId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::Detach(bool manual)
{
	// once you've called this once with manual==true, you must always call it with manual==true
	ALWAYS_ASSERTF(!m_attachmentManagedManually || manual, ("Attempt to do an automatic detach when the actor %s's attachments are managed manually", GetName()));

	if (manual)
		m_attachmentManagedManually = true;

	m_pAttachmentCtrl->Detach();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::AddLightTable() const
{
	// cache off the original settings of the lights so we can reset them after live-tweaking
	for (int i = 0; i < m_pLightTable->GetNumLights(); ++i)
	{
		const AnimatedLight* pLight = GetAnimatedLight(i);
		LightType type = (LightType)pLight->m_type;

		BaseLight* pBase = m_pLightTable->GetLightByType(type, pLight->m_index);
		if (pBase)
		{
			pBase->SetCutSceneLight(true);
			pBase->SetLiveTweaked(false);

			// make a copy into the tweaked set
			BaseLight* pTweakedBase = m_pTweakedLightTable->GetLightByType(type, pLight->m_index);
			ASSERT(pTweakedBase);
			CopyLight(pTweakedBase, pBase, type);
		}
	}

	g_scene.AddLights(nullptr, m_pLightTable, kDynamicLight);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLightObject::RemoveLightTable() const
{
	g_scene.RemoveLights(m_pLightTable);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Active state
/// --------------------------------------------------------------------------------------------------------------- ///

void NdLightObject::Active::Update()
{
	NdLightObject &self = Self();
	ParentClass::Update();

	AnimControl* pAnimControl = self.GetAnimControl();
	if (!pAnimControl || !pAnimControl->IsAsyncUpdateEnabled(FgAnimData::kEnableAsyncPostAnimBlending))
	{
		self.UpdateAttached();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
class DetermineLightNameIterator
{
public:
	typedef bool VisitFunc(DetermineLightNameIterator& iter, Process* pProc);
	const char* m_pLightName;

	explicit DetermineLightNameIterator(const BaseLight* pLight)
		: m_pVisitFunc((VisitFunc*)Visit)
		, m_pLight(pLight)
		, m_pLightName(nullptr)
	{
		EngineComponents::GetProcessMgr()->WalkTree(EngineComponents::GetProcessMgr()->m_pDefaultTree, ProcessVisitor, this);
	}

	static bool Visit(DetermineLightNameIterator& iter, NdLightObject* pLightObj)
	{
		if (pLightObj)
		{
			const char* pLightName = pLightObj->DetermineLightNameInternal(iter.m_pLight);
			if (pLightName)
			{
				iter.m_pLightName = pLightName;
				return false;
			}
		}
		return true;
	}

private:
	VisitFunc* m_pVisitFunc;
	const BaseLight* m_pLight;

	static bool ProcessVisitor(Process* pProc, void* pUserData)
	{
		NdLightObject *pLightObj = NdLightObject::FromProcess(pProc);
		if (pLightObj)
		{
			DetermineLightNameIterator* pIter = PunPtr<DetermineLightNameIterator*>(pUserData);
			if (pIter->m_pVisitFunc)
			{
				return pIter->m_pVisitFunc(*pIter, pLightObj);
			}
		}
		return true;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
const char* NdLightObject::DetermineLightName(const BaseLight* pLight)
{
	DetermineLightNameIterator iter(pLight);
	return iter.m_pLightName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* NdLightObject::DetermineLightNameInternal(const BaseLight* pLightToFind)
{
	const char* pLightName = nullptr;

	int numLights = m_pLightTable->GetNumLights();
	for (int i = 0; i < numLights; ++i)
	{
		const AnimatedLight* pAnimatedLight = GetAnimatedLight(i);
		LightType type = (LightType)pAnimatedLight->m_type;
		BaseLight* pLight = m_pLightTable->GetLightByType(type, pAnimatedLight->m_index);

		if (pLight == pLightToFind)
		{
			pLightName = pAnimatedLight->m_pStrName;
			break;
		}
	}

	return pLightName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdLightObject::UpdateMayaLightInternal(const LightUpdateData& data)
{
	return LiveUpdateMayaLight(data);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdLightObject::UpdateMayaLightIterator::UpdateMayaLightIterator(const LightUpdateData& data) : m_data(data), m_updatedLight(false)
{
	EngineComponents::GetProcessMgr()->WalkTree(EngineComponents::GetProcessMgr()->m_pDefaultTree, ProcessVisitor, this);	// cutscene & igc lights are in default tree
	EngineComponents::GetProcessMgr()->WalkTree(EngineComponents::GetProcessMgr()->m_pEffectTree, ProcessVisitor, this);	// level lights are in effect tree
}

bool NdLightObject::LiveUpdateMayaLight(const LightUpdateData& data)
{
	bool updatedLight = false;
	BoundFrame apRefFrame;
	GetApOrigin(apRefFrame);
	Locator apRef = apRefFrame.GetLocatorWs();

	for (int i = 0; i < m_pLightTable->GetNumLights(); ++i)
	{
		const AnimatedLight* pLight = GetAnimatedLight(i);
		if (strcmp(pLight->m_pStrName, data.m_lightName) == 0)
		{
			MsgCinematic("UpdateMayaLight Light %d: %s\n", i, pLight->m_pStrName);

			LightType type = (LightType)pLight->m_type;
			BaseLight* pBase = m_pLightTable->GetLightByType(type, pLight->m_index);
			BaseLight* pTweakedBase = m_pTweakedLightTable->GetLightByType(type, pLight->m_index);

			ExtraLightInfoMgr tweakedExtraLightInfoMgr(*m_pTweakedLightTable);
			ExtraLightInfo tweakedExtraLightInfo;
			tweakedExtraLightInfoMgr.GetExtraLightInfo(tweakedExtraLightInfo, type, pLight->m_index);
			ExtraLightInfo* pTweakedExtraLightInfo = &tweakedExtraLightInfo;

			ExtraLightInfoMgr extraLightInfoMgr(*m_pLightTable);
			ExtraLightInfo extraLightInfo;
			extraLightInfoMgr.GetExtraLightInfo(extraLightInfo, type, pLight->m_index);

			ASSERT(!pBase || pBase->IsCutsceneLight());
			if (pBase && pBase->IsCutsceneLight())
			{
				updatedLight = true;

				ASSERT(pTweakedBase);
				CopyLight(pTweakedBase, pBase, type); // start with a baseline that matches the current light in-game
				memcpy(pTweakedExtraLightInfo, &extraLightInfo, sizeof(ExtraLightInfo));

				if (data.m_radius != 0.0f)
				{
					pTweakedBase->m_radius = data.m_radius;
				}
				pTweakedBase->SetLiveTweaked(true);
				pTweakedBase->m_shapeRadius = data.m_shapeRadius;
				pTweakedBase->m_shapeRadiusEyeScale = data.m_shapeRadiusEyeScale;
				pTweakedExtraLightInfo->m_shadowBlurScale = data.m_shadowBlurScale;
				pTweakedExtraLightInfo->m_shadowSlope = data.m_shadowSlope;
				pTweakedExtraLightInfo->m_shadowConst = data.m_shadowConst;
				pTweakedExtraLightInfo->m_nearClipDist = data.m_nearClipDist;
				pTweakedExtraLightInfo->m_farClipDist = data.m_farClipDist;
				pTweakedExtraLightInfo->m_volIntensity = data.m_volIntensity;
				pTweakedExtraLightInfo->m_shadowFadeDistStart = data.m_shadowFadeDistStart;
				pTweakedExtraLightInfo->m_shadowFadeDistEnd = data.m_shadowFadeDistEnd;
				pTweakedExtraLightInfo->m_lightFadeDistStart = data.m_fadeDistStart;
				pTweakedExtraLightInfo->m_lightFadeDistEnd = data.m_fadeDistEnd;
				pTweakedExtraLightInfo->m_nearLightFadeDistStart = data.m_nearFadeDistStart;
				pTweakedExtraLightInfo->m_nearLightFadeDistEnd = data.m_nearFadeDistEnd;
				pTweakedExtraLightInfo->m_minShadowBlurScale = data.m_minShadowBlurScale;
				pTweakedExtraLightInfo->m_maxShadowBlurDist = data.m_maxShadowBlurDist;
				pTweakedExtraLightInfo->m_frontIntensityMultiplier = data.m_frontIntensityMultiplier;
				pTweakedExtraLightInfo->m_backIntensityMultiplier = data.m_backIntensityMultiplier;
				pTweakedExtraLightInfo->m_sideIntensityMultiplier = data.m_sideIntensityMultiplier;
				pTweakedBase->SetCastShadow(data.m_rayTraceShadows);
				pTweakedBase->SetDecay(data.m_decayRate);
				pTweakedBase->SetEmitDiffuse(data.m_emitDiffuse);
				pTweakedBase->SetEmitSpecular(data.m_emitSpecular);
				pTweakedBase->SetSkipBgShadow(data.m_skipBgShadow);
				pTweakedBase->SetSkipPropShadow(data.m_skipPropShadow);
				pTweakedBase->SetVariableShadowBlurScale(data.m_variableShadowBlurScale);
				pTweakedBase->SetEyesOnly(data.m_eyesOnly);
				pTweakedBase->SetHairOnly(data.m_hairOnly);
				pTweakedBase->SetDisableTransmittance(data.m_disableTransmittance);
				pTweakedBase->SetEnableBounce(data.m_enableBounce);
				pTweakedBase->SetSkipHair(data.m_skipHair);
				pTweakedBase->SetCastsSss(data.m_castsSss);
				pTweakedBase->m_minRoughness = data.m_minRoughness;
				pTweakedBase->m_specScale = data.m_specScale;
				if (data.m_noLitBg)
					pTweakedExtraLightInfo->m_extraFlags |= ExtraLightInfo::kExtraLightFlagIgnoreBackgroundGeo;
				else
					pTweakedExtraLightInfo->m_extraFlags &= ~ExtraLightInfo::kExtraLightFlagIgnoreBackgroundGeo;

				if (data.m_noLitFg)
					pTweakedExtraLightInfo->m_extraFlags |= ExtraLightInfo::kExtraLightFlagIgnoreForegroundGeo;
				else
					pTweakedExtraLightInfo->m_extraFlags &= ~ExtraLightInfo::kExtraLightFlagIgnoreForegroundGeo;

				if (data.m_noLitChar)
					pTweakedExtraLightInfo->m_extraFlags |= ExtraLightInfo::kExtraLightFlagIgnoreCharacterGeo;
				else
					pTweakedExtraLightInfo->m_extraFlags &= ~ExtraLightInfo::kExtraLightFlagIgnoreCharacterGeo;

				if (data.m_onlyLitCharacter)
					pTweakedExtraLightInfo->m_extraFlags |= ExtraLightInfo::kExtraLightFlagOnlyCharacterGeo;
				else
					pTweakedExtraLightInfo->m_extraFlags &= ~ExtraLightInfo::kExtraLightFlagOnlyCharacterGeo;

				if (data.m_onlyLitProps)
					pTweakedExtraLightInfo->m_extraFlags |= ExtraLightInfo::kExtraLightFlagOnlyPropsGeo;
				else
					pTweakedExtraLightInfo->m_extraFlags &= ~ExtraLightInfo::kExtraLightFlagOnlyPropsGeo;

				pTweakedBase->m_intensity = data.m_intensity;
				pTweakedBase->m_startDistance = data.m_startDistance;
				pTweakedBase->m_startRange = data.m_startRange;

				// transform the pos/dir from apReference space into world space
				const Vec3 trans(data.m_pos);
				const Vec3 dir(data.m_dir);
				const Vector upVec = type == kProjectorLight ? Vec3(data.m_up) : (dir.Y() > 0.9f ? Vector(kUnitZAxis) : Vector(kUnitYAxis));

				const Locator lightLocCs = Locator(trans, QuatFromLookAt(dir, upVec));
				const Locator lightLocWs = apRef.TransformLocator(lightLocCs);
				pTweakedBase->m_pos = (Vec3)lightLocWs.GetTranslation();
				pTweakedBase->m_dir = (Vec3)GetLocalZ(lightLocWs.GetRotation());

				for (int j = 0; j < 3; ++j)
				{
					pTweakedBase->m_color.Set(j, data.m_color[j]);
				}

				if (GetRenderSettingsBoolCurrentValue(SID("lighting:hack-dynamic-env-lights-enable")))
				{
					Vec4 envColorAndIntensityMultipliers = GetEnvironmentColorAndIntensityMultipliers();
					pTweakedBase->m_color = pTweakedBase->m_color * Vec3(envColorAndIntensityMultipliers.X(), envColorAndIntensityMultipliers.Y(), envColorAndIntensityMultipliers.Z());
					pTweakedBase->m_intensity *= envColorAndIntensityMultipliers.W();
				}

				if (type == kSpotLight)
				{
					SpotLight* pTweakedSpot = PunPtr<SpotLight*>(pTweakedBase);
					pTweakedSpot->m_coneAngle = data.m_spotConeAngle;
					pTweakedSpot->m_penumbraAngle = data.m_spotPenumbraAngle;
					pTweakedSpot->m_dropOff = data.m_spotDropOff;
				}
				else if (type == kProjectorLight)
				{
					ProjLight* pTweakedProj = PunPtr<ProjLight*>(pTweakedBase);
					pTweakedProj->m_up = (Vec3)GetLocalY(lightLocWs.GetRotation());

					pTweakedProj->m_fadeoutRange = data.m_spotPenumbraAngle;
					if (pTweakedProj->m_projType == kOrthographicProjection)
					{
						pTweakedProj->m_scaleX = data.m_capWidth;
						pTweakedProj->m_scaleY = data.m_capHeight;
					}
					else if (pTweakedProj->m_capScale == -1.f)
					{
						pTweakedProj->m_scaleX = 2.f * tanf(data.m_spotConeAngle * 0.5f);
						pTweakedProj->m_scaleY = 2.f * tanf(data.m_spotConeAngle * 0.5f);
					}

					pTweakedProj->m_maskTextureScrollSpeed[0] = data.m_goboUScrollSpeed;
					pTweakedProj->m_maskTextureScrollSpeed[1] = data.m_goboVScrollSpeed;
					pTweakedProj->m_far = data.m_runtimeCutoff;
					pTweakedProj->m_maskTextureTileFactor[0] = data.m_goboUTileFactor;
					pTweakedProj->m_maskTextureTileFactor[1] = data.m_goboVTileFactor;
					pTweakedProj->m_maskTextureAnimSpeed = data.m_goboAnimSpeed;
				}

				CopyLight(pBase, pTweakedBase, type); // finally, copy the tweak into the live copy of the light (it'll be copied over top every frame for as long as this light is tweaked)

				tweakedExtraLightInfoMgr.SetExtraLightInfo(*pTweakedExtraLightInfo, type, pLight->m_index);
				extraLightInfoMgr.SetExtraLightInfo(*pTweakedExtraLightInfo, type, pLight->m_index);
			}

			break;
		}
	}

	return updatedLight;
}

Vec4 NdLightObject::GetEnvironmentColorAndIntensityMultipliers() const
{
	if (IsShadowCastingSpotLight())
	{
		LightEnvResult* pPlayerLightEnv = GetCachedPlayerLightEnv();
		if (!pPlayerLightEnv)
		{
			return Vec4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		// Calculate luma from extracted color
		// Use luma to calculate dynamic intensity (if needed)
		Vector lightDir = pPlayerLightEnv->m_extractedLightDir;
		Vec4 lightColor = pPlayerLightEnv->m_extractedLightColor;
		Vec4 normalizedLightColor = Normalize3(lightColor);

		/*float finalIntensity = 1.0f;
		if (GetRenderSettingsBoolCurrentValue(SID("lighting:hack-dynamic-env-lights-intensity-enable")))
		{
			const Vec4 kLumaCoeffs(0.2126f, 0.7152f, 0.0722f, 1.0f);
			F32 luma = Dot3(kLumaCoeffs, lightColor);

			F32 inAmbientMin = GetRenderSettingsFloatCurrentValue(SID("lighting:hack-dynamic-env-lights-min-ambient"));
			F32 inAmbientMax = GetRenderSettingsFloatCurrentValue(SID("lighting:hack-dynamic-env-lights-max-ambient"));
			F32 outIntensityMin = GetRenderSettingsFloatCurrentValue(SID("lighting:hack-dynamic-env-lights-min-intensity-multiplier"));
			F32 outIntensityMax = GetRenderSettingsFloatCurrentValue(SID("lighting:hack-dynamic-env-lights-max-intensity-multiplier"));

			F32 inputNormalized = Clamp((luma - inAmbientMin) / (inAmbientMax - inAmbientMin), 0.0f, 1.0f);
			finalIntensity = (inputNormalized * (outIntensityMax - outIntensityMin)) + outIntensityMin;
		}*/

		if (GetRenderSettingsBoolCurrentValue(SID("lighting:hack-dynamic-env-lights-debug-enable")))
		{
			MsgCon("Shadow casting spot light [name: %s] is being driven by player env probe\n", GetName());
		}

		F32 intensityMultiplier = GetRenderSettingsFloatCurrentValue(SID("lighting:hack-dynamic-env-lights-intensity-multiplier"));
		return Vec4(normalizedLightColor.X(), normalizedLightColor.Y(), normalizedLightColor.Z(), intensityMultiplier);
	}
	else
	{
		return Vec4(1.0f, 1.0f, 1.0f, 1.0f);
	}
}