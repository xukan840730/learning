/*
 * Copyright (c)2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nd-effect-control.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/nd-config.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/surface-defines.h"
#include "ndlib/render/ngen/surfaces.h"
#include "ndlib/util/point-curve.h"

#include "gamelib/audio/arm.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/tasks/task-graph-mgr.h"
#include "gamelib/tasks/task-subnode.h"

bool g_printEffDebugging = false;					// Everything else

/// --------------------------------------------------------------------------------------------------------------- ///
struct EffectControlScriptObserver : public ScriptObserver
{
	I32 m_count;

	EffectControlScriptObserver() : ScriptObserver(FILE_LINE_FUNC, INVALID_STRING_ID_64), m_count(0)
	{
	}

	static bool WalkKillEffectControls(Process* pProc, void*)
	{
		NdGameObject* pGo = NdGameObject::FromProcess(pProc);
		if (pGo && pGo->GetEffectControl())
			KillProcess(pGo);

		return true;
	}

	virtual void OnModuleImported(StringId64 moduleId, Memory::Context allocContext) override
	{
		if (moduleId == SID("mesh-audio-config"))
		{
			if (m_count > 0)
			{
				// avoid a hard crash whenever we (mr "mesh-audio-config")
				EngineComponents::GetProcessMgr()->WalkTree(EngineComponents::GetProcessMgr()->m_pDefaultTree, WalkKillEffectControls, nullptr);

				// now re-play current task for the user's convenience
				// JQG: this may cause a deadlock because of EngineComponents::GetProcessMgr()->m_processSpawnLock... if so, we can either
				// disable this convenience feature or defer the pSubNode->Play() until after ScriptManager's UpdateJob
				if (const GameTaskSubnode * pSubNode = g_taskGraphMgr.GetActiveContinueSubnode())
				{
					g_ndConfig.m_pDMenuMgr->Exit();
					pSubNode->Play(pSubNode->GetActiveContinueName());
				}
			}
		}
	}

	virtual void OnModuleRemoved(StringId64 moduleId, Memory::Context allocContext) override
	{
		if (moduleId == SID("mesh-audio-config"))
		{
			++m_count;
		}
	}
};

EffectControlScriptObserver g_effScriptObserver;

/// --------------------------------------------------------------------------------------------------------------- ///
void IEffectControl::Init()
{
	ScriptManager::RegisterObserver(&g_effScriptObserver);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EffectControlSpawnInfo::Reset()
{
	memset(this, 0, sizeof(*this));

	m_boundFrame = BoundFrame();
	m_sfxHandle = MutableProcessHandle();
	m_hGameObject = MutableNdGameObjectHandle();
	m_endFrame = -1.0f;
	m_detachTimeout = -1.0f;

	m_fGain = DbToGain(-0.0f);
	m_fPitchMod = NDI_FLT_MAX;
	m_fGainMultiplier = 1.0f;
	m_rot = Quat(kIdentity);

	m_offsetFrame = kAlignFrame;
	m_orientFrame = kAlignFrame;
}

/// --------------------------------------------------------------------------------------------------------------- ///
EffectControlSpawnInfo::EffectControlSpawnInfo()
	: EffectSoundParams()
{
	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
EffectControlSpawnInfo::EffectControlSpawnInfo(StringId64 type,
											   StringId64 name,
											   NdGameObject* pGameObject,
											   StringId64 joint,
											   bool bTracking,
											   Process* eventProcess)
	: EffectSoundParams()
{
	Reset();

	m_type = type;
	m_name = name;
	m_hGameObject = pGameObject;
	m_bTracking = bTracking;
	m_bNeedsUpdateProcess = m_bTracking;
	m_jointId = joint;
	m_eventProcess = eventProcess;
}

/// --------------------------------------------------------------------------------------------------------------- ///
EffectControlSpawnInfo::EffectControlSpawnInfo(StringId64 type,
											   StringId64 name,
											   const BoundFrame& boundFrame,
											   bool tracking,
											   Process* eventProcess)
	: EffectSoundParams()
{
	Reset();

	m_type = type;
	m_name = name;
	m_hGameObject = nullptr;
	m_bTracking = tracking;
	m_bNeedsUpdateProcess = m_bTracking;
	m_boundFrame = boundFrame;
	m_jointId = INVALID_STRING_ID_64;
	m_eventProcess = eventProcess;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("new-eff", DcNewEff)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	const EffectAnimInfo* pEffectAnimInfo = args.NextPointer<const EffectAnimInfo>();
	SCRIPT_ASSERTF(pEffectAnimInfo, ("%s: called without passing a valid eff as the first argument", args.GetDcFunctionName()));
	const StringId64 effType = args.NextStringId();

	if (effType == INVALID_STRING_ID_64)
	{
		return ScriptValue(pEffectAnimInfo);
	}

	//int argc = args.GetArgCount(); // magically defined by SCRIPT_FUNC() macro!
	int argi = args.GetArgIndex();
	ASSERT(argc >= argi);
	const int numTags = (argc - argi) / 2;

	// clone the effect so that we can modify it
	EffectAnimInfo* pCloneInfo = NDI_NEW(kAllocSingleGameFrame, kAlign16) EffectAnimInfo;
	EffectAnimEntry* pCloneEntry = NDI_NEW(kAllocSingleGameFrame, kAlign16) EffectAnimEntry;
	if (!pCloneInfo || !pCloneEntry)
	{
		args.MsgScriptError("out of single-frame memory\n");
		return ScriptValue(pEffectAnimInfo);
	}

	*pCloneInfo = *pEffectAnimInfo;
	pCloneInfo->m_pEffect = pCloneEntry;

	StringId64* newKeys = nullptr;
	const char** newValues = nullptr;

	// now add tags as requested by the caller
	if (numTags > 0)
	{
		ScopedTempAllocator alloc(FILE_LINE_FUNC);

		newKeys = NDI_NEW StringId64[numTags];
		newValues = NDI_NEW const char*[numTags];

		if (newKeys && newValues)
		{
			StringId64* pNewKey = newKeys;
			const char** pNewVal = newValues;

			--argc; // since we're stepping by twos below
			for ( ; argi < argc; argi += 2)
			{
				const StringId64 key = args.NextStringId();
				const char* val = args.NextString();
				if (key != INVALID_STRING_ID_64)
				{
					if (key == SID("frame"))
					{
						MsgScript("%sWARNING: %s: you cannot change the frame index of an EFF (ignored)%s\n", GetTextColorString(kTextColorYellow), args.GetDcFunctionName(), GetTextColorString(kTextColorNormal));
					}
					else
					{
						*(pNewKey++) = key;
						*(pNewVal++) = val;
					}
				}
			}
		}
		else
		{
			args.MsgScriptError("out of scoped-temp memory\n");
			return ScriptValue(pEffectAnimInfo);
		}
	}

	const bool kDontCopyExistingTags = true;

	if (pCloneEntry->Clone(*pEffectAnimInfo->m_pEffect,
							effType,
							0,
							nullptr,
							nullptr,
							numTags,
							newKeys,
							newValues,
							kAllocSingleGameFrame,
							kDontCopyExistingTags))
	{
		if (const EffectAnimEntryTag* pFilterGroupTag = pCloneEntry->GetTagByName(SID("filter-group")))
		{
			pCloneInfo->m_filterGroupId = pFilterGroupTag->GetValueAsStringId().GetValue();
		}

		return ScriptValue(pCloneInfo);
	}
	else
	{
		args.MsgScriptError("out of single-frame memory\n");
	}

	return ScriptValue(pEffectAnimInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("modify-eff", DcModifyEff)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	const EffectAnimInfo* pEffectAnimInfo = args.NextPointer<const EffectAnimInfo>();
	SCRIPT_ASSERTF(pEffectAnimInfo, ("%s: called without passing a valid eff as the first argument", args.GetDcFunctionName()));

	//int argc = args.GetArgCount(); // magically defined by SCRIPT_FUNC() macro!
	int argi = args.GetArgIndex();
	ASSERT(argc >= argi);
	const int numTags = (argc - argi) / 2;
	if (numTags > 0)
	{
		// clone the effect so that we can modify it
		EffectAnimInfo* pCloneInfo = NDI_NEW(kAllocSingleGameFrame, kAlign16) EffectAnimInfo;
		EffectAnimEntry* pCloneEntry = NDI_NEW(kAllocSingleGameFrame, kAlign16) EffectAnimEntry;
		if (pCloneInfo && pCloneEntry)
		{
			*pCloneInfo = *pEffectAnimInfo;
			pCloneInfo->m_pEffect = pCloneEntry;

			// now modify/add tags as requested by the caller
			ScopedTempAllocator alloc(FILE_LINE_FUNC);

			StringId64* replaceKeys		= NDI_NEW StringId64[numTags];
			StringId64* newKeys			= NDI_NEW StringId64[numTags];
			const char** replaceValues	= NDI_NEW const char*[numTags];
			const char** newValues		= NDI_NEW const char*[numTags];

			if (replaceKeys && newKeys && replaceValues && newValues)
			{
				StringId64* pReplaceKey		= replaceKeys;
				const char** pReplaceVal	= replaceValues;
				StringId64* pNewKey			= newKeys;
				const char** pNewVal	= newValues;

				--argc; // since we're stepping by twos below
				for ( ; argi < argc; argi += 2)
				{
					const StringId64 key = args.NextStringId();
					const char* val = args.NextString();
					if (key != INVALID_STRING_ID_64)
					{
						if (key == SID("frame"))
						{
							MsgScript("%sWARNING: %s: you cannot change the frame index of an EFF (ignored)%s\n", GetTextColorString(kTextColorYellow), args.GetDcFunctionName(), GetTextColorString(kTextColorNormal));
						}
						else if (pEffectAnimInfo->m_pEffect->GetTagByName(key))
						{
							*(pReplaceKey++) = key;
							*(pReplaceVal++) = val;
						}
						else if (val != nullptr)
						{
							*(pNewKey++) = key;
							*(pNewVal++) = val;
						}
					}
				}

				ptrdiff_t numReplace = pReplaceKey - replaceKeys;
				ptrdiff_t numNew = pNewKey - newKeys;
				const bool kCopyExistingTags = false;

				if (pCloneEntry->Clone(*pEffectAnimInfo->m_pEffect,
									   INVALID_STRING_ID_64,
									   numReplace,
									   replaceKeys,
									   replaceValues,
									   numNew,
									   newKeys,
									   newValues,
									   kAllocSingleGameFrame,
									   kCopyExistingTags))
				{
					return ScriptValue(pCloneInfo);
				}
				else
				{
					args.MsgScriptError("out of single-frame memory\n");
				}
			}
			else
			{
				args.MsgScriptError("out of scoped-temp memory\n");
			}
		}
		else
		{
			args.MsgScriptError("out of single-frame memory\n");
		}
	}

	return ScriptValue(pEffectAnimInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("modify-eff-sid", DcModifyEffSid)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	const EffectAnimInfo* pEffectAnimInfo = args.NextPointer<const EffectAnimInfo>();
	SCRIPT_ASSERTF(pEffectAnimInfo, ("%s: called without passing a valid eff as the first argument", args.GetDcFunctionName()));

	//int argc = args.GetArgCount(); // magically defined by SCRIPT_FUNC() macro!
	int argi = args.GetArgIndex();
	const int numTags = (argc - argi) / 2;

	if (numTags <= 0)
	{
		return ScriptValue(pEffectAnimInfo);
	}

	// clone the effect so that we can modify it
	EffectAnimInfo* pCloneInfo = NDI_NEW(kAllocSingleGameFrame, kAlign16) EffectAnimInfo;
	EffectAnimEntry* pCloneEntry = NDI_NEW(kAllocSingleGameFrame, kAlign16) EffectAnimEntry;
	if (!pCloneInfo || !pCloneEntry)
	{
		args.MsgScriptError("out of single-frame memory\n");
		return ScriptValue(pEffectAnimInfo);
	}

	*pCloneInfo = *pEffectAnimInfo;
	pCloneInfo->m_pEffect = pCloneEntry;

	// now modify/add tags as requested by the caller
	ScopedTempAllocator alloc(FILE_LINE_FUNC);

	StringId64* replaceKeys		= NDI_NEW StringId64[numTags];
	StringId64* newKeys			= NDI_NEW StringId64[numTags];
	StringId64* replaceValues	= NDI_NEW StringId64[numTags];
	StringId64* newValues		= NDI_NEW StringId64[numTags];

	if (!replaceKeys || !newKeys || !replaceValues || !newValues)
	{
		args.MsgScriptError("out of scoped-temp memory\n");
		return ScriptValue(pEffectAnimInfo);
	}

	StringId64* pReplaceKey	= replaceKeys;
	StringId64* pReplaceVal	= replaceValues;
	StringId64* pNewKey		= newKeys;
	StringId64* pNewVal		= newValues;

	--argc; // since we're stepping by twos below
	for ( ; argi < argc; argi += 2)
	{
		const StringId64 key = args.NextStringId();
		const StringId64 val = args.NextStringId();
		if (key != INVALID_STRING_ID_64)
		{
			if (key == SID("frame"))
			{
				MsgScript("%sWARNING: %s: you cannot change the frame index of an EFF (ignored)%s\n",
							GetTextColorString(kTextColorYellow),
							args.GetDcFunctionName(),
							GetTextColorString(kTextColorNormal));
			}
			else if (pEffectAnimInfo->m_pEffect->GetTagByName(key))
			{
				*(pReplaceKey++) = key;
				*(pReplaceVal++) = val;
			}
			else if (val != INVALID_STRING_ID_64)
			{
				*(pNewKey++) = key;
				*(pNewVal++) = val;
			}
		}
	}

	ptrdiff_t numReplace = pReplaceKey - replaceKeys;
	ptrdiff_t numNew     = pNewKey - newKeys;
	const bool kCopyExistingTags = false;

	if (pCloneEntry->Clone(*pEffectAnimInfo->m_pEffect,
							INVALID_STRING_ID_64,
							numReplace,
							replaceKeys,
							replaceValues,
							numNew,
							newKeys,
							newValues,
							kAllocSingleGameFrame,
							kCopyExistingTags))
	{
		if (const EffectAnimEntryTag* pFilterGroupTag = pCloneEntry->GetTagByName(SID("filter-group")))
		{
			pCloneInfo->m_filterGroupId = pFilterGroupTag->GetValueAsStringId().GetValue();
		}

		return ScriptValue(pCloneInfo);
	}
	else
	{
		args.MsgScriptError("out of single-frame memory\n");
	}

	return ScriptValue(pEffectAnimInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("eff-tag-int32", DcEffTagInt32)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	const EffectAnimInfo* pEffectAnimInfo = args.NextPointer<const EffectAnimInfo>();
	SCRIPT_ASSERTF(pEffectAnimInfo, ("%s: called without passing a valid eff as the first argument", args.GetDcFunctionName()));
	StringId64 key = args.NextStringId();
	I32 defaultVal = args.NextI32();

	const EffectAnimEntryTag* pTag = pEffectAnimInfo->m_pEffect->GetTagByName(key);
	if (pTag)
	{
		return ScriptValue(pTag->GetValueAsI32());
	}

	return ScriptValue(defaultVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("eff-tag-float", DcEffTagFloat)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	const EffectAnimInfo* pEffectAnimInfo = args.NextPointer<const EffectAnimInfo>();
	SCRIPT_ASSERTF(pEffectAnimInfo, ("%s: called without passing a valid eff as the first argument", args.GetDcFunctionName()));
	StringId64 key = args.NextStringId();
	float defaultVal = args.NextFloat();

	const EffectAnimEntryTag* pTag = pEffectAnimInfo->m_pEffect->GetTagByName(key);
	if (pTag)
	{
		return ScriptValue(pTag->GetValueAsF32());
	}

	return ScriptValue(defaultVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("eff-tag-symbol", DcEffTagSymbol)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	const EffectAnimInfo* pEffectAnimInfo = args.NextPointer<const EffectAnimInfo>();
	SCRIPT_ASSERTF(pEffectAnimInfo, ("%s: called without passing a valid eff as the first argument", args.GetDcFunctionName()));
	StringId64 key = args.NextStringId();
	StringId64 defaultVal = args.NextStringId();

	const EffectAnimEntryTag* pTag = pEffectAnimInfo->m_pEffect->GetTagByName(key);
	if (pTag)
	{
		return ScriptValue(pTag->GetValueAsStringId());
	}

	return ScriptValue(defaultVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EffectAnimInfo* IEffectControl::HandleScriptEffect(const NdGameObject* pGo,
														 const NdGameObject* pOtherGo,
														 const EffectAnimInfo* pEffectAnimInfo,
														 StringId64 scriptId) const
{
	if (!pEffectAnimInfo || scriptId == INVALID_STRING_ID_64)
		return pEffectAnimInfo;

	const DC::ScriptLambda * pLambda = ScriptManager::Lookup<DC::ScriptLambda>(scriptId);
	if (!pLambda)
		return pEffectAnimInfo;

	const EffectAnimEntry* pEffect = pEffectAnimInfo->m_pEffect;

	//const StringId64 type = pEffect->GetNameId();
	//const StringId64 name = GetEffTagAsSID(pEffect, SID("name"), INVALID_STRING_ID_64);
	//const StringId64 animName = pEffectAnimInfo->m_anim;

	ScriptValue argv[3];
	argv[0] = ScriptValue(pEffectAnimInfo);
	argv[1] = ScriptValue(pGo      ? pGo->GetUserId()      : INVALID_STRING_ID_64);
	argv[2] = ScriptValue(pOtherGo ? pOtherGo->GetUserId() : INVALID_STRING_ID_64);

	ScriptValue result = ScriptManager::Eval(pLambda, ARRAY_ELEMENT_COUNT(argv), argv);

	const EffectAnimInfo* pFinalInfo = PunPtr<const EffectAnimInfo*>(result.m_pointer);
	if (!pFinalInfo)
	{
		pFinalInfo = pEffectAnimInfo;
	}
	else if (!Memory::GetAllocator(kAllocSingleGameFrame)->IsValidPtr(pFinalInfo))
	{
		MsgConScriptError("the defun (%s ...) did not return a valid EFF!\n", DevKitOnly_StringIdToStringOrHex(scriptId));
		pFinalInfo = pEffectAnimInfo;
	}

	return pFinalInfo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MutableProcessHandle IEffectControl::FireSurfaceSound(StringId64 soundNameId,
												      float surfaceBlend,
												      NdGameObject* pGameObject,
												      Point_arg position,
												      const EffectSoundParams& params,
												      SfxProcess::SpawnFlags spawnFlags)
{
	Process* pSfx = nullptr;

	// Don't attempt to play an effect if the name ID is INVALID_STRING_ID_64.
	if (soundNameId == INVALID_STRING_ID_64)
	{
		const ArmLogLevel eLogLevel = EngineComponents::GetAudioManager()->m_eLogLevel;
		if (eLogLevel == kArmLogErrors || eLogLevel == kArmLogAll)
		{
			MsgAudioErr("EFF: audio effect failed due to invalid name ID, this should be impossible - probably an attempt to fire EFF from an unnamed object!\n");
		}

		return nullptr;
	}

	if (pGameObject != nullptr)
	{
		SfxProcess::AttachType attachType = SfxProcess::kAttachRoot;
		StringId64 attachId = INVALID_STRING_ID_64;
		if (params.m_attachId != INVALID_STRING_ID_64)
		{
			attachId = params.m_attachId;
			attachType = SfxProcess::kAttachPoint;
		}
		if (params.m_jointId != INVALID_STRING_ID_64)
		{
			attachId = params.m_jointId;
			attachType = SfxProcess::kAttachJoint;
		}
		pSfx = NewProcess(SfxSpawnInfo(soundNameId, pGameObject, spawnFlags, attachId, attachType));
	}
	else
	{
		pSfx = NewProcess(SfxSpawnInfo(soundNameId, position, spawnFlags));
	}

	if (pSfx)
	{
		const F32 gainMultiplier = params.m_fGainMultiplier;
		const F32 fBlendGain = params.m_fBlendGain;
		F32 fPitchMod = params.m_fPitchMod;
		const F32 fGain = params.m_fGain;
		const F32 scaledGainMultiplier = gainMultiplier * fBlendGain + (1.0f - fBlendGain);

		if (fGain != NDI_FLT_MAX)
		{
			SendEvent(SID("set-gain"), pSfx, fGain * scaledGainMultiplier);
		}

		if (fPitchMod == NDI_FLT_MAX)
		{
			fPitchMod = 0.0f;
		}

		if (fPitchMod != 0.0f)
		{
			SendEvent(SID("set-pitch"), pSfx, fPitchMod);
		}

		if (surfaceBlend >= 0.0f)
		{
			SendEvent(SID("set-variable"), pSfx, CcVar("surface-blend"), surfaceBlend, U32(ARM_LOCAL_VARIABLE_FLAG_IMMEDIATE_ON_START));
		}

		if (params.m_waterDepth > 0.0f)
		{
			SendEvent(SID("set-variable"), pSfx, CcVar("water-depth"), params.m_waterDepth, U32(ARM_LOCAL_VARIABLE_FLAG_IMMEDIATE_ON_START));
		}

		if (FALSE_IN_FINAL_BUILD(g_printEffDebugging && (params.m_jointId != INVALID_STRING_ID_64) && DebugSelection::Get().IsProcessOrNoneSelected(pGameObject)))
		{
			char soundName[128];
			strncpy(soundName, DevKitOnly_StringIdToString(soundNameId), sizeof(soundName));
			soundName[sizeof(soundName) - 1] = '\0';

			char jointName[128];
			strncpy(jointName, DevKitOnly_StringIdToString(params.m_jointId), sizeof(jointName));
			jointName[sizeof(jointName) - 1] = '\0';

			const ArmLogLevel eLogLevel = EngineComponents::GetAudioManager()->m_eLogLevel;
			if (eLogLevel == kArmLogSounds || eLogLevel == kArmLogAll)
			{
				if (params.m_jointId == INVALID_STRING_ID_64)
				{
					if (fGain != NDI_FLT_MAX && fPitchMod != NDI_FLT_MAX)
						MsgAudio("EFF: firing sound '%s' (joint: none, gain: %f, pitch: %f)\n", soundName, fGain * scaledGainMultiplier, fPitchMod);
					else if (fGain != NDI_FLT_MAX)
						MsgAudio("EFF: firing sound '%s' (joint: none, gain: %f, pitch: scream)\n", soundName, fGain * scaledGainMultiplier);
					else if (fPitchMod != NDI_FLT_MAX)
						MsgAudio("EFF: firing sound '%s' (joint: none, gain: scream, pitch: %f)\n", soundName, fPitchMod);
					else
						MsgAudio("EFF: firing sound '%s' (joint: none, gain: scream, pitch: scream)\n", soundName);
				}
				else
				{
					if (fGain != NDI_FLT_MAX && fPitchMod != NDI_FLT_MAX)
						MsgAudio("EFF: firing sound '%s' (joint: '%s', gain: %f, pitch: %f)\n", soundName, jointName, fGain * scaledGainMultiplier, fPitchMod);
					else if (fGain != NDI_FLT_MAX)
						MsgAudio("EFF: firing sound '%s' (joint: '%s', gain: %f, pitch: scream)\n", soundName, jointName, fGain * scaledGainMultiplier);
					else if (fPitchMod != NDI_FLT_MAX)
						MsgAudio("EFF: firing sound '%s' (joint: '%s', gain: scream, pitch: %f)\n", soundName, jointName, fPitchMod);
					else
						MsgAudio("EFF: firing sound '%s' (joint: '%s', gain: scream, pitch: scream)\n", soundName, jointName);
				}
			}
		}
	}

	return pSfx;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MutableProcessHandle IEffectControl::FireAdditiveSound(StringId64 soundNameId,
													   float additivePercentage,
													   NdGameObject* pGameObject,
													   Point_arg position,
													   const EffectSoundParams& params,
													   SfxProcess::SpawnFlags spawnFlags,
													   bool hackUseSurfaceBlend)
{
	Process* pSfx = nullptr;

	// Don't attempt to play an effect if the name ID is INVALID_STRING_ID_64.
	if (soundNameId == INVALID_STRING_ID_64)
	{
		const ArmLogLevel eLogLevel = EngineComponents::GetAudioManager()->m_eLogLevel;
		if (eLogLevel == kArmLogErrors || eLogLevel == kArmLogAll)
		{
			MsgAudioErr("EFF: audio effect failed due to invalid name ID, this should be impossible - probably an attempt to fire EFF from an unnamed object!\n");
		}

		return nullptr;
	}

	if (pGameObject != nullptr)
	{
		SfxProcess::AttachType attachType = SfxProcess::kAttachRoot;
		StringId64 attachId = INVALID_STRING_ID_64;
		if (params.m_attachId != INVALID_STRING_ID_64)
		{
			attachId = params.m_attachId;
			attachType = SfxProcess::kAttachPoint;
		}
		if (params.m_jointId != INVALID_STRING_ID_64)
		{
			attachId = params.m_jointId;
			attachType = SfxProcess::kAttachJoint;
		}
		pSfx = NewProcess(SfxSpawnInfo(soundNameId, pGameObject, spawnFlags, attachId, attachType));
	}
	else
	{
		pSfx = NewProcess(SfxSpawnInfo(soundNameId, position, spawnFlags));
	}

	if (pSfx)
	{
		const F32 gainMultiplier = params.m_fGainMultiplier;
		const F32 fBlendGain = params.m_fBlendGain;
		F32 fPitchMod = params.m_fPitchMod;
		const F32 fGain = params.m_fGain;
		const F32 scaledGainMultiplier = gainMultiplier * fBlendGain + (1.0f - fBlendGain);

		if (fGain != NDI_FLT_MAX)
		{
			SendEvent(SID("set-gain"), pSfx, fGain * scaledGainMultiplier);
		}

		if (fPitchMod == NDI_FLT_MAX)
		{
			fPitchMod = 0.0f;
		}

		if (fPitchMod != 0.0f)
		{
			SendEvent(SID("set-pitch"), pSfx, fPitchMod);
		}

		SendEvent(SID("set-variable"), pSfx, CcVar("additive-percentage"), additivePercentage, U32(ARM_LOCAL_VARIABLE_FLAG_IMMEDIATE_ON_START));

		if (hackUseSurfaceBlend)
			SendEvent(SID("set-variable"), pSfx, CcVar("surface-blend"), additivePercentage, U32(ARM_LOCAL_VARIABLE_FLAG_IMMEDIATE_ON_START));
		else
			SendEvent(SID("set-variable"), pSfx, CcVar("surface-blend"), 1.0f, U32(ARM_LOCAL_VARIABLE_FLAG_IMMEDIATE_ON_START)); // additive sound is always 100%

		if (params.m_waterDepth > 0.0f)
		{
			SendEvent(SID("set-variable"), pSfx, CcVar("water-depth"), params.m_waterDepth, U32(ARM_LOCAL_VARIABLE_FLAG_IMMEDIATE_ON_START));
		}

		if (FALSE_IN_FINAL_BUILD(g_printEffDebugging && (params.m_jointId != INVALID_STRING_ID_64) && DebugSelection::Get().IsProcessOrNoneSelected(pGameObject)))
		{
			char soundName[128];
			strncpy(soundName, DevKitOnly_StringIdToString(soundNameId), sizeof(soundName));
			soundName[sizeof(soundName) - 1] = '\0';

			char jointName[128];
			strncpy(jointName, DevKitOnly_StringIdToString(params.m_jointId), sizeof(jointName));
			jointName[sizeof(jointName) - 1] = '\0';

			const ArmLogLevel eLogLevel = EngineComponents::GetAudioManager()->m_eLogLevel;
			if (eLogLevel == kArmLogSounds || eLogLevel == kArmLogAll)
			{
				if (params.m_jointId == INVALID_STRING_ID_64)
				{
					if (fGain != NDI_FLT_MAX && fPitchMod != NDI_FLT_MAX)
						MsgAudio("EFF: firing additive sound '%s' (joint: none, gain: %f, pitch: %f), additive-percentage: %.3f\n", soundName, fGain * scaledGainMultiplier, fPitchMod, additivePercentage);
					else if (fGain != NDI_FLT_MAX)
						MsgAudio("EFF: firing additive sound '%s' (joint: none, gain: %f, pitch: scream), additive-percentage: %.3f\n", soundName, fGain * scaledGainMultiplier, additivePercentage);
					else if (fPitchMod != NDI_FLT_MAX)
						MsgAudio("EFF: firing additive sound '%s' (joint: none, gain: scream, pitch: %f), additive-percentage: %.3f\n", soundName, fPitchMod, additivePercentage);
					else
						MsgAudio("EFF: firing additive sound '%s' (joint: none, gain: scream, pitch: scream), additive-percentage: %.3f\n", soundName, additivePercentage);
				}
				else
				{
					if (fGain != NDI_FLT_MAX && fPitchMod != NDI_FLT_MAX)
						MsgAudio("EFF: firing additive sound '%s' (joint: '%s', gain: %f, pitch: %f), additive-percentage: %.3f\n", soundName, jointName, fGain * scaledGainMultiplier, fPitchMod, additivePercentage);
					else if (fGain != NDI_FLT_MAX)
						MsgAudio("EFF: firing additive sound '%s' (joint: '%s', gain: %f, pitch: scream), additive-percentage: %.3f\n", soundName, jointName, fGain * scaledGainMultiplier, additivePercentage);
					else if (fPitchMod != NDI_FLT_MAX)
						MsgAudio("EFF: firing additive sound '%s' (joint: '%s', gain: scream, pitch: %f), additive-percentage: %.3f\n", soundName, jointName, fPitchMod, additivePercentage);
					else
						MsgAudio("EFF: firing additive sound '%s' (joint: '%s', gain: scream, pitch: scream), additive-percentage: %.3f\n", soundName, jointName, additivePercentage);
				}
			}
		}
	}

	return pSfx;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MutableProcessHandle IEffectControl::FireSurfaceSound(const SurfaceSound& surfaceSound,
												      NdGameObject* pGameObject,
												      Point_arg position,
												      const EffectSoundParams* pParams,
												      SfxProcess::SpawnFlags spawnFlags)
{
	const EffectSoundParams& params = pParams ? *pParams : EffectSoundParams();

	bool primaryIsAdditive = false;
	bool secondaryIsAdditive = false;
	float primaryBlend = 0.f;
	float secondaryBlend = 0.f;
	bool hackUseSurfaceBlendPrimary = false;
	bool hackUseSurfaceBlendSecondary = false;
	{
		const DC::SurfaceDesc* pPrimaryDesc = GetDcSurfaceDesc(surfaceSound.m_primarySurfaceId);
		if (pPrimaryDesc != nullptr)
		{
			if (pPrimaryDesc->m_soundAdditiveCurve != nullptr)
			{
				primaryIsAdditive = true;
				primaryBlend = NdUtil::EvaluatePointCurve(surfaceSound.m_blend, pPrimaryDesc->m_soundAdditiveCurve);
				hackUseSurfaceBlendPrimary = pPrimaryDesc->m_hackUseSurfaceBlend;
			}
		}

		if (surfaceSound.m_secondarySurfaceId != INVALID_STRING_ID_64)
		{
			const DC::SurfaceDesc* pSecondaryDesc = GetDcSurfaceDesc(surfaceSound.m_secondarySurfaceId);
			if (pSecondaryDesc != nullptr)
			{
				if (pSecondaryDesc->m_soundAdditiveCurve != nullptr)
				{
					secondaryIsAdditive = true;
					secondaryBlend = NdUtil::EvaluatePointCurve(1.f - surfaceSound.m_blend, pSecondaryDesc->m_soundAdditiveCurve);
					hackUseSurfaceBlendSecondary = pSecondaryDesc->m_hackUseSurfaceBlend;
				}
			}
		}
	}

	MutableProcessHandle hSfx;

	if (primaryIsAdditive && !secondaryIsAdditive && surfaceSound.m_secondarySoundId != INVALID_STRING_ID_64)
	{
		hSfx = FireSurfaceSound(surfaceSound.m_secondarySoundId, 1.f, pGameObject, position, params, spawnFlags);
	}
	else if (!primaryIsAdditive && secondaryIsAdditive)
	{
		hSfx = FireSurfaceSound(surfaceSound.m_primarySoundId, 1.f, pGameObject, position, params, spawnFlags);
	}
	else
	{
		if (surfaceSound.m_primarySoundId != INVALID_STRING_ID_64)
			hSfx = FireSurfaceSound(surfaceSound.m_primarySoundId, surfaceSound.m_blend, pGameObject, position, params, spawnFlags);

		if (surfaceSound.m_secondarySoundId != INVALID_STRING_ID_64)
		{
			MutableProcessHandle hSecondarySfx = FireSurfaceSound(surfaceSound.m_secondarySoundId, 1.f - surfaceSound.m_blend, pGameObject, position, params, spawnFlags);
			if (!hSfx.HandleValid() && hSecondarySfx.HandleValid())
				hSfx = hSecondarySfx;
		}
	}

	if (surfaceSound.m_additiveSoundId != INVALID_STRING_ID_64 && surfaceSound.m_additivePercentage > 0.f)
	{
		const DC::SurfaceDesc* pPrimaryDesc = GetDcSurfaceDesc(surfaceSound.m_primarySurfaceId);
		bool hackUseSurfaceBlend = pPrimaryDesc && pPrimaryDesc->m_hackUseSurfaceBlend;
		MutableProcessHandle hAdditive = FireAdditiveSound(surfaceSound.m_additiveSoundId, surfaceSound.m_additivePercentage, pGameObject, position, params, spawnFlags, hackUseSurfaceBlend);
		if (!hSfx.HandleValid() && hAdditive.HandleValid())
			hSfx = hAdditive;
	}

	return hSfx;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 IEffectControl::ExtractBaseLayerSurfaceType(const MeshProbe::SurfaceType& info)
{
	if (info.Valid())
	{
		{
			const DC::SurfaceDesc* pSurfaceDesc = GetDcSurfaceDesc(info.m_primary[0]);
			if (!pSurfaceDesc || !pSurfaceDesc->m_soundAdditiveCurve)
				return info.m_primary[0];
		}

		if (info.m_primary[1] != INVALID_STRING_ID_64)
		{
			const DC::SurfaceDesc* pSurfaceDesc = GetDcSurfaceDesc(info.m_primary[1]);
			if (!pSurfaceDesc || !pSurfaceDesc->m_soundAdditiveCurve)
				return info.m_primary[1];
		}
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 IEffectControl::ExtractBaseLayerSurfaceType(const MeshProbe::SurfaceTypeLayers& info)
{
	for (int kk = ARRAY_COUNT(info.m_layers) - 1; kk >= 0; kk--)
	{
		if (info.m_isWater[kk] || info.m_isDecal[kk])
			continue;

		const MeshProbe::SurfaceTypeResult& ea = info.m_layers[kk];

		for (int jj = 0; jj < ARRAY_COUNT(ea.m_surfaceType.m_primary); jj++)
		{
			const StringId64 surfaceTypeId = ea.m_surfaceType.m_primary[jj];
			if (surfaceTypeId != INVALID_STRING_ID_64)
			{
				const DC::SurfaceDesc* pSurfaceDesc = GetDcSurfaceDesc(surfaceTypeId);
				if (!pSurfaceDesc || !pSurfaceDesc->m_soundAdditiveCurve)
				{
					return surfaceTypeId;
				}
			}
		}
	}

	return INVALID_STRING_ID_64;
}
