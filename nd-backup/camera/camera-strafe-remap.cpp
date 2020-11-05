/*
* Copyright (c) 2019 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/camera/camera-strafe-remap.h"
#include "gamelib/camera/camera-strafe-base.h"
#include "gamelib/camera/camera-animated.h"
#include "gamelib/camera/camera-manager.h"

#include "ndlib/nd-game-info.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/settings/settings.h"
#include "ndlib/settings/priority.h"
#include "ndlib/net/nd-net-info.h"

///-------------------------------------------------------------------------------------------///
/// CameraSettings Remapping.
///-------------------------------------------------------------------------------------------///
class CameraSettingsRemapping
{
public:

	static const I32 kMaxNumRemaps = 4;

	struct Entry
	{
		bool m_valid;
		StringId64 m_cameraOverlay;
		CameraRemapParams m_params;
		TimeFrame m_requestStartTime;
		char m_dbgSrc[64];

		void Reset()
		{
			m_valid = false;
			m_cameraOverlay = INVALID_STRING_ID_64;
			m_dbgSrc[0] = 0;
		}
	};
	Entry m_entries[kMaxNumRemaps];

	TimeFrame m_playerSpawnTime;

public:

	CameraSettingsRemapping()
	{
		for (U32F ii = 0; ii < kMaxNumRemaps; ii++)
		{
			m_entries[ii].Reset();
		}
	}

	U64 GetCameraRemapHash() const
	{
		U64 cameraRemapsId = 0;

		for (U32F ii = 0; ii < kMaxNumRemaps; ii++)
		{
			const Entry& entry = m_entries[ii];
			if (entry.m_cameraOverlay != INVALID_STRING_ID_64)
			{
				cameraRemapsId += entry.m_cameraOverlay.GetValue();
			}
		}

		return cameraRemapsId;
	}

	struct FindResult
	{
		I32 slotIndex = -1;
		bool alreadyExists = false;
	};

	FindResult FindFreeCameraOverlaySlot(U32 playerIndex, StringId64 sid)
	{
		for (U32F ii = 0; ii < kMaxNumRemaps; ii++)
		{
			const Entry& entry = m_entries[ii];
			if (entry.m_valid && entry.m_cameraOverlay == sid)
			{
				FindResult res;
				res.slotIndex = ii;
				res.alreadyExists = true;
				return res;
			}
		}

		for (U32F ii = 0; ii < kMaxNumRemaps; ii++)
		{
			const Entry& entry = m_entries[ii];
			if (!entry.m_valid)
			{
				FindResult res;
				res.slotIndex = ii;
				res.alreadyExists = false;
				return res;
			}
		}

		return FindResult();
	}
};

// -------------------------------------------------------------------------------------------------
// Globals
// -------------------------------------------------------------------------------------------------

static CameraSettingsRemapping s_priv[kMaxPlayers];

// -------------------------------------------------------------------------------------------------
CameraRemapSettings RemapCameraSettingsId(StringId64 baseSettingsId, StringId64 forceOverlayId)
{
	CameraRemapSettings result;
	result.m_remapSettingsId = baseSettingsId;

	if (forceOverlayId == INVALID_STRING_ID_64)
		return result;

	if (const DC::Map* pMap = ScriptManager::Lookup<DC::Map>(forceOverlayId, nullptr))
	{
		if (const StringId64* pRemappedSymbol = ScriptManager::MapLookup<StringId64>(pMap, baseSettingsId))
		{
			result.m_remapSettingsId = *pRemappedSymbol;
			result.m_valid = true;
		}
	}
	//show error on invalid remap
	else if (FALSE_IN_FINAL_BUILD(true))
	{
		MsgConScriptError("Invalid camera remap: %s\n", DevKitOnly_StringIdToString(forceOverlayId));
	}

	return result;
}

// -------------------------------------------------------------------------------------------------
CameraRemapSettings RemapCameraSettingsId(StringId64 baseSettingsId, I32 playerIndex, bool includeDistRemap)
{
	GAMEPLAY_ASSERT(playerIndex < kMaxPlayers);

	CameraRemapSettings result;
	result.m_remapSettingsId = baseSettingsId;

	// we don't want camera-remap to be overlay style. the highest priority always wins.
	I32 bestPriority = -1;
	for (U32F ii = 0; ii < CameraSettingsRemapping::kMaxNumRemaps; ii++)
	{
		const CameraSettingsRemapping::Entry& entry = s_priv[playerIndex].m_entries[ii];
		if (entry.m_cameraOverlay != INVALID_STRING_ID_64 && entry.m_params.m_priority > bestPriority)
		{
			if (!includeDistRemap && entry.m_params.m_distParams.Valid())
				continue;

			if (const DC::Map* pMap = ScriptManager::Lookup<DC::Map>(entry.m_cameraOverlay, nullptr))
			{
				if (const StringId64* pRemappedSymbol = ScriptManager::MapLookup<StringId64>(pMap, baseSettingsId))
				{
					result.m_remapSettingsId = *pRemappedSymbol;
					result.m_params = entry.m_params;
					result.m_requestStartTime = entry.m_requestStartTime;
					result.m_valid = true;
					if (!entry.m_params.m_allowStack)
						bestPriority = entry.m_params.m_priority;
				}
			}
			//show error on invalid remap
			else if (FALSE_IN_FINAL_BUILD(true))
			{
				MsgConScriptError("Invalid camera remap: %s\n", DevKitOnly_StringIdToString(entry.m_cameraOverlay));
			}
		}
	}

	if (g_ndConfig.m_pNetInfo->IsNetActive())
	{
		if (const DC::Map* pMap = ScriptManager::Lookup<DC::Map>(SID("*net-camera-remaps*"), nullptr))
		{
			const StringId64* pRemappedSymbol = ScriptManager::MapLookup<StringId64>(pMap, baseSettingsId);
			if (!pRemappedSymbol)
			{
				pRemappedSymbol = ScriptManager::MapLookup<StringId64>(pMap, result.m_remapSettingsId);
			}

			if (pRemappedSymbol)
			{
				result.m_remapSettingsId = *pRemappedSymbol;
				result.m_params = CameraRemapParams();
				result.m_valid = true;
			}
		}
	}

	return result;
}

StringId64 DebugCameraRemap(U32 playerIndex, bool debug)
{
	GAMEPLAY_ASSERT(playerIndex < kMaxPlayers);

	StringId64 cameraOverlay = INVALID_STRING_ID_64;
	I32 bestPriority = -1;

	for (U32F ii = 0; ii < CameraSettingsRemapping::kMaxNumRemaps; ii++)
	{
		const CameraSettingsRemapping::Entry& entry = s_priv[playerIndex].m_entries[ii];
		if (entry.m_valid)
		{
			if (FALSE_IN_FINAL_BUILD(debug))
			{
				MsgCon("CameraOverlay[%u]: %s Priority: %d, from: %s\n", ii, DevKitOnly_StringIdToString(entry.m_cameraOverlay), entry.m_params.m_priority, entry.m_dbgSrc);
			}

			if (entry.m_params.m_priority > bestPriority)
			{
				cameraOverlay = entry.m_cameraOverlay;
				if (!s_priv[playerIndex].m_entries[ii].m_params.m_allowStack)
					bestPriority = s_priv[playerIndex].m_entries[ii].m_params.m_priority;
			}
		}
	}

	return cameraOverlay;
}

void ValidateCameraRemap(StringId64 remapId)
{
	STRIP_IN_FINAL_BUILD;

	const DC::Map* pRemap = ScriptManager::Lookup<DC::Map>(remapId, nullptr);

	if (pRemap)
	{
		const int count = pRemap->m_count;
		for (int i = 0; i < count; i++)
		{
			const StringId64 baseCameraId = pRemap->m_keys[i];
			const StringId64* pRemappedCameraId = static_cast<const StringId64*>(pRemap->m_data[i].m_ptr);
			GAMEPLAY_ASSERT(pRemappedCameraId);
			const StringId64 remappedCameraId = *pRemappedCameraId;

			const DC::CameraBaseSettings* pBaseSettings = ScriptManager::Lookup<DC::CameraStrafeBaseSettings>(baseCameraId, nullptr);
			const DC::CameraBaseSettings* pRemappedSettings = ScriptManager::Lookup<DC::CameraStrafeBaseSettings>(remappedCameraId, nullptr);

			if (!pBaseSettings)
			{
				MsgConScriptError("Camera remap %s has invalid base (source) camera setting %s!\n", DevKitOnly_StringIdToString(remapId), DevKitOnly_StringIdToString(baseCameraId));
			}

			if (!pRemappedSettings)
			{
				MsgConScriptError("Camera remap %s has invalid remapped (target) camera setting %s!\n", DevKitOnly_StringIdToString(remapId), DevKitOnly_StringIdToString(remappedCameraId));
			}
		}
	}
	else
	{
		MsgConScriptError("No camera remap named %s found!\n", DevKitOnly_StringIdToString(remapId));
	}
}

void CameraSetRemap(U32 playerIndex, StringId64 sid, const CameraRemapParams& params, const char* szDbgSrc)
{
	GAMEPLAY_ASSERT(playerIndex < kMaxPlayers);

	const CameraSettingsRemapping::FindResult res = s_priv[playerIndex].FindFreeCameraOverlaySlot(playerIndex, sid);
	if (res.slotIndex < 0)
		return;

	CameraSettingsRemapping::Entry& entry = s_priv[playerIndex].m_entries[res.slotIndex];
	{
		StringId64 key = StringId64ConcatInteger(StringId64ConcatInteger(SID("set-camera-overlay-"), (I32)playerIndex), res.slotIndex);
		SettingSetDefault(&entry.m_cameraOverlay, INVALID_STRING_ID_64);
		SettingSetPers(key, &entry.m_cameraOverlay, sid, kPlayerModePriority, 1.0f);

		// Immediately set because otherwise there's a frame delay. And it always uses the same settings priority anyway, so last call
		// will always override the current setting. We probably shouldn't be using the Settings system at all for this - RyanB
		entry.m_cameraOverlay = sid;
	}

	{
		StringId64 key = StringId64ConcatInteger(StringId64ConcatInteger(SID("set-camera-overlay-valid-"), (I32)playerIndex), res.slotIndex);
		SettingSetDefault(&entry.m_valid, false);
		SettingSetPers(key, &entry.m_valid, true, kPlayerModePriority, 1.0f);
		entry.m_valid = true;		// immediately set valid flag so that other camera-set-overlay can find correct slot.
	}

	entry.m_params = params;

	if (FALSE_IN_FINAL_BUILD(true))
	{
		if (szDbgSrc != nullptr)
		{
			strncpy(entry.m_dbgSrc, szDbgSrc, sizeof(entry.m_dbgSrc) - 1);
		}
		else
		{
			entry.m_dbgSrc[0] = 0;
		}
	}
	else
	{
		entry.m_dbgSrc[0] = 0;
	}

	if (!res.alreadyExists)
	{
		entry.m_requestStartTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();
	}

	if (FALSE_IN_FINAL_BUILD(g_cameraOptions.m_validateCameraRemaps))
	{
		ValidateCameraRemap(sid);
	}
}

void CameraClearRemap(U32 playerIndex)
{
	GAMEPLAY_ASSERT(playerIndex < kMaxPlayers);

	for (U32F ii = 0; ii < CameraSettingsRemapping::kMaxNumRemaps; ii++)
	{
		CameraSettingsRemapping::Entry& overlay = s_priv[playerIndex].m_entries[ii];

		{
			StringId64 key = StringId64ConcatInteger(StringId64ConcatInteger(SID("set-camera-overlay-"), (I32)playerIndex), ii);
			SettingUnsetPers(key, &s_priv[playerIndex].m_entries[ii].m_cameraOverlay);
		}

		{
			StringId64 key = StringId64ConcatInteger(StringId64ConcatInteger(SID("set-camera-overlay-valid-"), (I32)playerIndex), ii);
			SettingUnsetPers(key, &s_priv[playerIndex].m_entries[ii].m_valid);
		}
	}
}

U64 GetCameraRemapHash(U32 playerIndex)
{
	GAMEPLAY_ASSERT(playerIndex < kMaxPlayers);
	return s_priv[playerIndex].GetCameraRemapHash();
}

///-------------------------------------------------------------------------------------------------------///
void SetCameraRemapSpawnTime(U32 playerIndex, const TimeFrame& timeFrame)
{
	GAMEPLAY_ASSERT(playerIndex < kMaxPlayers);
	s_priv[playerIndex].m_playerSpawnTime = timeFrame;
}

TimeFrame GetCameraRemapSpawnTime(U32 playerIndex)
{
	GAMEPLAY_ASSERT(playerIndex < kMaxPlayers);
	return s_priv[playerIndex].m_playerSpawnTime;
}

///-------------------------------------------------------------------------------------------------------///
bool TryAutoGenerateIntroCamera(I32 playerIndex,
								const CameraControl* pCurrCamera,
								DistCameraParams* pOutParams1,
								AutoGenCamParams* pOutParams2)
{
	const CameraControlStrafeBase* pCurrStrafeCam = CameraControlStrafeBase::FromProcess(pCurrCamera);
	if (!pCurrStrafeCam)
		return false;

	// there could be only 1 dist-remap camera.
	if (pCurrStrafeCam->IsFadeInByDistToObj())
		return false;

	const CameraRemapSettings remapSettings = RemapCameraSettingsId(pCurrStrafeCam->GetBaseDcSettingsId(), playerIndex, true);
	if (remapSettings.m_params.m_autoGenValid)
	{
		*pOutParams1 = remapSettings.m_params.m_distParams;
		*pOutParams2 = remapSettings.m_params.m_autoGenParams;
		return true;
	}

	return false;
}

///-------------------------------------------------------------------------------------------------------///
static bool TryAutoGenerateOutroInternal(const AnimControl* pAnimControl, 
										 const Locator& apRefLoc,
										 StringId64 phaseAnimId, 
										 float fadeOutPhase, 
										 float fadeOutDist,
										 bool mirrored,
										 AutoGenCamParams* pOutParams)
{
	// get camera locator from animation after fadeOutTime.
	//CameraAnimation finalAnimation;
	//bool valid = pCurrCameraAnimated->EvaluateCameraAnimation(fadeOutTime, finalAnimation);
	//if (valid)
	//{
	//	pOutParams->m_cameraLoc = finalAnimation.m_cameraFrameWs.GetLocator();

	//	g_prim.Draw(DebugCoordAxes(pOutParams->m_cameraLoc, 0.3f, PrimAttrib(kPrimEnableHiddenLineAlpha)), Seconds(3.f));
	//}

	Locator cameraLoc;
	StringId64 cameraChannelNameId = SID("apReference-camera1");
	U32 valid2 = FindChannelsFromApReference(pAnimControl, phaseAnimId, fadeOutPhase, apRefLoc, &cameraChannelNameId, 1, &cameraLoc, mirrored);
	if (!valid2)
		return false;

	// camera locator is flipped 180 in anim-data.
	cameraLoc.SetRotation(cameraLoc.GetRotation() * QuatFromAxisAngle(Vector(SMath::kUnitYAxis), DEGREES_TO_RADIANS(180.0f)));

	// extract fov from anim.
	Maybe<float> fov;
	{
		ndanim::JointParams cameraSqt;
		bool valid3 = EvaluateChannelInAnim(pAnimControl, phaseAnimId, cameraChannelNameId, fadeOutPhase, &cameraSqt, false, true);
		if (valid3)
		{
			fov = GetFovFromRawCameraScale(cameraSqt.m_scale);
		}
	}

	//pOutParams->m_characterLoc = characterLoc;
	pOutParams->m_cameraLoc = cameraLoc;
	pOutParams->m_fadeOutDist = fadeOutDist;
	pOutParams->m_igcName = phaseAnimId;
	pOutParams->m_igcApLoc = apRefLoc;

	if (fov.Valid())
	{
		pOutParams->m_animatedFov = fov.Get();
	}

	pOutParams->m_initDirection = GetLocalZ(cameraLoc.GetRotation());

	//g_prim.Draw(DebugCoordAxes(characterLoc, 0.3f, PrimAttrib(kPrimEnableHiddenLineAlpha)), Seconds(3.f));
	//g_prim.Draw(DebugCoordAxes(cameraLoc, 0.3f, PrimAttrib(kPrimEnableHiddenLineAlpha)), Seconds(3.f));

	return true;
}

///-------------------------------------------------------------------------------------------------------///
static bool GetCharacterLoc(const NdGameObject* pPlayerObj, const CameraControlAnimated* pCurrCameraAnimated, bool mirrored, Locator* pOutCharLoc)
{
	const AnimControl* pAnimControl = pPlayerObj->GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const AnimStateInstance* pCurrInst = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
	if (!pCurrInst || !pCurrInst->IsApLocatorActive())
		return false;

	const float currPhase = pCurrInst->Phase();

	// this only works if igc is uniformly progressing. 
	float fadeOutPhase = currPhase;
	{
		float duration = pCurrInst->GetDuration();
		if (duration != 0.f)
		{
			const float fadeOutTime = pCurrCameraAnimated->GetToAutoGenFadeOutTime();
			GAMEPLAY_ASSERT(fadeOutTime >= 0.f);
			fadeOutPhase = currPhase + fadeOutTime / duration;
			fadeOutPhase = MinMax01(fadeOutPhase);
		}
	}

	const Locator apRefLoc = pCurrInst->GetApLocator().GetLocator();
	const StringId64 phaseAnimId = pCurrInst->GetPhaseAnim();

	bool valid1 = FindAlignFromApReference(pAnimControl, phaseAnimId, fadeOutPhase, apRefLoc, SID("apReference"), pOutCharLoc, mirrored);
	if (!valid1)
		return false;

	return true;
}

///-------------------------------------------------------------------------------------------------------///
bool TryAutoGenerateOutroCamera(const CameraRequest* pBestRequest,
								const CameraControlAnimated* pCurrCameraAnimated,
								AutoGenCamParams* pOutParams)
{
	if (!pCurrCameraAnimated)
		return false;

	if (!pBestRequest || 
		pBestRequest->m_cameraId.ToConfig().GetProcessType() != SID("CameraControlStrafe") || 
		pBestRequest->m_startInfo.m_photoMode)
		return false;

	const AutoGenScriptParams& autoGenScriptParams = pCurrCameraAnimated->GetNextCameraAutoGenScriptParams();
	if (autoGenScriptParams.m_fadeOutDist <= 0.f)
		return false;

	AnimInstanceHandle hAnimInstance = pCurrCameraAnimated->GetAnimSourceInstance();
	const NdGameObject* pGameObject = hAnimInstance.GetGameObject();
	if (!pGameObject)
		return false;

	Locator apRefLoc(kIdentity);
	StringId64 phaseAnimId = INVALID_STRING_ID_64;
	float fadeOutPhase = 0.f;
	bool mirrored = false;

	// figure out other parameters
	{
		if (const AnimStateInstance* pStateInst = hAnimInstance.GetStateInstance())
		{
			if (!pStateInst->IsApLocatorActive())
				return false;

			const float currPhase = pStateInst->Phase();

			// this only works if igc is uniformly progressing. 
			fadeOutPhase = currPhase;
			{
				float duration = pStateInst->GetDuration();
				if (duration != 0.f)
				{
					const float fadeOutTime = pCurrCameraAnimated->GetToAutoGenFadeOutTime();
					GAMEPLAY_ASSERT(fadeOutTime >= 0.f);
					fadeOutPhase = currPhase + fadeOutTime / duration;
					fadeOutPhase = MinMax01(fadeOutPhase);
				}
			}

			apRefLoc = pStateInst->GetApLocator().GetLocator();
			phaseAnimId = pStateInst->GetPhaseAnim();
			mirrored = pStateInst->IsFlipped();
		}
		else if (const AnimSimpleInstance* pSimpleInst = hAnimInstance.GetSimpleInstance())
		{
			const float currPhase = pSimpleInst->GetPhase();

			// this only works if igc is uniformly progressing. 
			fadeOutPhase = currPhase;
			{
				float duration = pSimpleInst->GetDuration();
				if (duration != 0.f)
				{
					const float fadeOutTime = pCurrCameraAnimated->GetToAutoGenFadeOutTime();
					GAMEPLAY_ASSERT(fadeOutTime >= 0.f);
					fadeOutPhase = currPhase + fadeOutTime / duration;
					fadeOutPhase = MinMax01(fadeOutPhase);
				}
			}

			apRefLoc = pSimpleInst->GetApOrigin().GetLocator();
			phaseAnimId = pSimpleInst->GetAnimId();
			mirrored = pSimpleInst->IsFlipped();
		}
		else
		{
			return false;
		}
	}

	// figure out camera-loc, etc.
	const AnimControl* pAnimControl = pGameObject->GetAnimControl();
	bool valid0 = TryAutoGenerateOutroInternal(pAnimControl, apRefLoc, phaseAnimId, fadeOutPhase, autoGenScriptParams.m_fadeOutDist, mirrored, pOutParams);
	if (!valid0)
		return false;

	pOutParams->m_cameraOverlayId = autoGenScriptParams.m_cameraOverlayId;

	const NdGameObject* pFocusObj = pCurrCameraAnimated->GetFocusObject().ToProcess();
	if (!pFocusObj)
		pFocusObj = pGameObject;

	if (!pFocusObj)
		return false;

	// figure out character-loc
	if (pFocusObj == pGameObject)
	{
		Locator characterLoc;
		bool valid1 = FindAlignFromApReference(pFocusObj->GetAnimControl(), phaseAnimId, fadeOutPhase, apRefLoc, SID("apReference"), &characterLoc, mirrored);
		if (!valid1)
		{
			MsgConScriptError("auto-gen-camera failed because couldn't find apReference from %s\n", DevKitOnly_StringIdToString(phaseAnimId));
			return false;
		}

		pOutParams->m_characterLoc = characterLoc;
	}
	else
	{
		Locator characterLoc;
		bool valid1 = GetCharacterLoc(pFocusObj, pCurrCameraAnimated, mirrored, &characterLoc);
		if (!valid1)
		{
			MsgConScriptError("auto-gen-camera failed! Is focus-object playing Ap anim?\n");
			return false;
		}

		pOutParams->m_characterLoc = characterLoc;
	}

	return true;
}

///-------------------------------------------------------------------------------------------------------///
bool TryAutoGenerateDistRemapCamera(I32 playerIndex,
									const CameraRequest* pBestRequest, 
									AutoGenCamParams* pOutParams)
{
	if (!pBestRequest ||
		pBestRequest->m_cameraId.ToConfig().GetProcessType() != SID("CameraControlStrafe") ||
		pBestRequest->m_startInfo.m_photoMode)
	{
		return false;
	}

	const CameraRemapSettings remapSettings = RemapCameraSettingsId(pBestRequest->m_startInfo.m_settingsId, playerIndex, true);
	if (remapSettings.m_valid && remapSettings.m_params.m_distParams.Valid() && remapSettings.m_params.m_autoGenValid)
	{
		// check stance:
		const NdGameObject* pPlayerGo = EngineComponents::GetNdGameInfo()->GetGlobalPlayerGameObject(playerIndex);
		const Character* pPlayerChar = Character::FromProcess(pPlayerGo);
		if (!pPlayerChar)
			return false;

		if (remapSettings.m_params.m_autoGenParams.m_standDisallowed && pPlayerChar->IsStandingForCameraRemap())
			return false;

		if (!remapSettings.m_params.m_autoGenParams.m_crouchedAllowed && pPlayerChar->IsCrouchedForCameraRemap())
			return false;

		*pOutParams = remapSettings.m_params.m_autoGenParams;
		return true;
	}

	return false;
}

///-------------------------------------------------------------------------------------------------------///
bool TryTimeBasedDistRemapCamera(I32 playerIndex, 
								 const CameraRequest* pBestRequest, 
								 DistCameraParams* pOutParams)
{
	if (!pBestRequest ||
		pBestRequest->m_cameraId.ToConfig().GetProcessType() != SID("CameraControlStrafe") ||
		pBestRequest->m_startInfo.m_photoMode)
	{
		return false;
	}

	const CameraRemapSettings remapSettings = RemapCameraSettingsId(pBestRequest->m_startInfo.m_settingsId, playerIndex, true);
	if (remapSettings.m_valid && remapSettings.m_params.m_distParams.Valid())
	{
		*pOutParams = remapSettings.m_params.m_distParams;
		return true;
	}

	return false;
}