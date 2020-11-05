/*
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/timeframe.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-snapshot-node-animation.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-streaming.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/anim/evaluate-anim-tree.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/pose-matching.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/bound-frame.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process.h"
#include "ndlib/profiling/profile-cpu-categories.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"
#include "ndlib/scriptx/h/game-types.h"
#include "ndlib/text/string-builder.h"
#include "ndlib/util/boxedvalue.h"

#include "gamelib/cinematic/cinematic-process.h"
#include "gamelib/cinematic/cinematic-table.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/artitem.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "gamelib/state-script/ss-animate.h"
#include "gamelib/state-script/ss-context.h"
#include "gamelib/state-script/ss-instance.h"
#include "gamelib/state-script/ss-manager.h"
#include "gamelib/state-script/ss-script-funcs.h"
#include "gamelib/state-script/ss-track-group.h"
#include "gamelib/state-script/ss-track.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkAnimScriptFuncs()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcPlayAnimation(int argc,
								   ScriptValue* argv, 
								   ScriptFnAddr __SCRIPT_FUNCTION__, 
								   bool allowCallbacks,
								   StringId64* pAnimId = nullptr)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGo = args.NextGameObject();
	if (!pGo)
		return ScriptValue(0);

	StringId64 animId = args.NextStringId();
	const char* goName = pGo->GetName();

	if (pAnimId)
		*pAnimId = animId;

	if (animId == INVALID_STRING_ID_64)
		return ScriptValue(0);

	AnimControl* pAnimControl = pGo->GetAnimControl();

	SsAnimateParams playParams;
	playParams.m_playedByScript = true;
	playParams.m_nameId = animId;

	const ArtItemAnim* pArtItemAnim = pAnimControl ? pAnimControl->LookupAnim(playParams.m_nameId).ToArtItem() : nullptr;

	SsTrackInstance* pTrackInst = GetJobSsContext().GetTrackInstance();
	if (pTrackInst && allowCallbacks)
	{
		// ask for the "animation-done" event when the animation's timeline overflows (phase > 1.0)
		playParams.m_sendOverflowEvent = true;	
		// ask for the "animation-done" event when the animation's timeline underflows (phase < 0.0)
		playParams.m_sendUnderflowEvent = true;

		playParams.m_callbackData = pTrackInst->GetTrackIndex();
	}

	playParams.m_exitMode = pGo->GetDefaultAnimateExitMode();

	bool explicitlyRequestedAllowFFU = false;

	SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
	if (pScriptInst)
	{
		playParams.m_cameraSource = pScriptInst->GetCameraSource();
	}


	for (;;)
	{
		// Parse the AnimateArg pairs (type, value)
		U32 argIndex = args.GetArgIndex();
		if (argIndex >= argc - 1)
			break;

		DC::AnimateArg argType = static_cast<DC::AnimateArg>(args.NextI32());

		switch (argType)
		{
		case DC::kAnimateArgAbandonNpcMove:					playParams.m_abandonNpcMove					=  args.NextBoolean(); break;
		case DC::kAnimateArgFadeInSec:						playParams.m_fadeInSec						=  args.NextFloat(); break;
		case DC::kAnimateArgAnimatedCameraId:				playParams.m_cameraSource					=  args.NextStringId(); break;
		case DC::kAnimateArgMotionFadeInSec:				playParams.m_motionFadeInSec				=  args.NextFloat(); break;
		case DC::kAnimateArgFadeInCurve:					playParams.m_fadeInCurve					=  args.NextU32(); break;
		case DC::kAnimateArgFadeOutSec:						playParams.m_fadeOutSec						=  args.NextFloat(); break;
		case DC::kAnimateArgFadeOutCurve:					playParams.m_fadeOutCurve					=  args.NextU32(); break;
		case DC::kAnimateArgMotionFadeOutSec:				playParams.m_motionFadeOutSec				=  args.NextFloat(); break;
		case DC::kAnimateArgStartPhase:						playParams.m_startPhase						=  args.NextFloat(); break;
		case DC::kAnimateArgStartPhaseMode:					playParams.m_startPhaseMode					=  args.NextI32(); break;
		case DC::kAnimateArgEndPhase:						playParams.m_endPhase						=  args.NextFloat(); break;
		case DC::kAnimateArgStickAbortFadeSec:				playParams.m_stickAbortFadeTime				=  args.NextFloat(); break;
		case DC::kAnimateArgMirror:							playParams.m_mirrored						=  args.NextBoolean(); break;
		case DC::kAnimateArgForceLoop:						playParams.m_forceLoop						=  args.NextBoolean(); break;
		case DC::kAnimateArgForceNoLoop:					playParams.m_forceNoLoop					=  args.NextBoolean(); break;
		case DC::kAnimateArgDontEndOnOverflow:				playParams.m_sendOverflowEvent				= !args.NextBoolean(); break;
		case DC::kAnimateArgDontEndOnUnderflow:				playParams.m_sendUnderflowEvent				= !args.NextBoolean(); break;
		case DC::kAnimateArgDontFadeOutPartial:				playParams.m_dontFadeOutPartial				=  args.NextBoolean(); break;
		case DC::kAnimateArgUsePrevApRef:					playParams.m_usePrevApRef					=  args.NextBoolean(); break;
		case DC::kAnimateArgNoPushPlayer:					playParams.m_noPushPlayer					=  args.NextBoolean(); break;
		case DC::kAnimateArgDisallowDeath:					playParams.m_disallowDeath					=  args.NextBoolean(); break;
		case DC::kAnimateArgNoUpdateAlign:					playParams.m_noUpdateAlign					=  args.NextBoolean(); break;
		case DC::kAnimateArgNetSync:						playParams.m_netSync						=  args.NextBoolean(); break;
		case DC::kAnimateArgFreezeSrcState:					playParams.m_freezeSrcState					=  args.NextBoolean(); break;
		case DC::kAnimateArgAllowProceduralAnimCamera:		playParams.m_allowAnimCameras				=  args.NextBoolean(); break;
		case DC::kAnimateArgCameraHandOff:					playParams.m_sortCameraAnimsByTopInstance	=  args.NextBoolean(); break;
		case DC::kAnimateArgCollision:						playParams.m_collisionMode					=  args.NextI32(); break;
		case DC::kAnimateArgWeaponMode:						playParams.m_playerWeaponMode				=  args.NextI32(); break;
		case DC::kAnimateArgCameraMode:						playParams.m_cameraMode						=  args.NextI32(); break;
		case DC::kAnimateArgLayer:							playParams.m_layer							=  args.NextI32(); break;
		case DC::kAnimateArgLayerName:						playParams.m_layerName						=  args.NextStringId(); break;
		case DC::kAnimateArgExitMirrored:					playParams.m_exitMirrored					=  args.NextBoolean(); break;
		case DC::kAnimateArgExitMode:						playParams.m_exitMode						=  args.NextI32(); break;
		case DC::kAnimateArgIkMode:							playParams.m_animIkMode						=  args.NextI32(); break;
		case DC::kAnimateArgExitMoveMode:					playParams.m_playerExitMoveMode				=  args.NextI32(); break;
		case DC::kAnimateArgArmIkLeftTarget:				playParams.m_armIkLeftTargetId				=  args.NextStringId(); break;
		case DC::kAnimateArgArmIkRightTarget:				playParams.m_armIkRightTargetId				=  args.NextStringId(); break;
		case DC::kAnimateArgWeaponPartialMode:				playParams.m_playerWeaponPartialMode		=  args.NextI32(); break;
		case DC::kAnimateArgWeaponPartialCopy:				playParams.m_playerWeaponPartialCopy		= args.NextBoolean(); break;
		case DC::kAnimateArgAllowFlashlightControl:			playParams.m_allowFlashlightControl = args.NextBoolean(); break;
		case DC::kAnimateArgWeaponHand:						playParams.m_weaponHand						=  args.NextI32(); break;
		case DC::kAnimateArgTurnSpring:						playParams.m_turnSpring						=  args.NextFloat(); break;
		case DC::kAnimateArgAnimateAlign:					playParams.m_animateAlign					=  args.NextBoolean(); break;
		case DC::kAnimateArgSearchForStartMaxPhase:			playParams.m_searchForStartMaxPhase			=  args.NextFloat(); break;
		case DC::kAnimateArgDisableDoubleAp:				playParams.m_disableDoubleAp				=  args.NextBoolean(); break;
		case DC::kAnimateArgUpdateGameCamInBlend:			playParams.m_updateGameCamInBlend			=  args.NextBoolean(); break;
		case DC::kAnimateArgAllowGetAttacked:				playParams.m_allowGetAttacked				=  args.NextBoolean(); break;
		case DC::kAnimateArgSnpcPerformanceInterrupt:		playParams.m_snpcPerformanceInterrupt		=  args.NextStringId(); break;
		case DC::kAnimateArgLayerFade:						playParams.m_layerFade						=  args.NextFloat(); break;
		case DC::kAnimateArgIsStateName:					playParams.m_isStateName					=  args.NextBoolean(); break;
		case DC::kAnimateArgResumeFlashlightPartialPhase:	playParams.m_resumeFlashlightPartialPhase	=  args.NextFloat(); break;
		case DC::kAnimateArgLookGesture:					playParams.m_lookGesture					=  args.NextStringId(); break;
		case DC::kAnimateArgCustomApRefChannel:				playParams.m_customApRefId					=  args.NextStringId(); break;
		case DC::kAnimateArgAllowNormalDeaths:				playParams.m_allowNormalDeaths				=  args.NextBoolean(); break;
		case DC::kAnimateArgAllowFlinch:					playParams.m_allowFlinch					=  args.NextBoolean(); break;
		case DC::kAnimateArgAllowPartialHits:				playParams.m_allowPartialHits			    =  args.NextBoolean(); break;
		case DC::kAnimateArgEndHiresAnimGeoSec:				playParams.m_endHiResAnimGeoSec				=  args.NextFloat(); break;
		case DC::kAnimateArgAllowWeaponActions:				playParams.m_allowWeaponActions				=  args.NextBoolean(); break;
		case DC::kAnimateArgAllowWeaponReload:				playParams.m_allowWeaponReload				= args.NextBoolean(); break;
		case DC::kAnimateArgSyncToCinematic:				playParams.m_syncToCinematicId				=  args.NextStringId(); break;
		case DC::kAnimateArgAllowShootInNpcAnimation:		playParams.m_allowShootInNpcAnimation		=  args.NextBoolean(); break;
		case DC::kAnimateArgNoEdgeIk:						playParams.m_noEdgeIk						=  args.NextBoolean(); break;
		case DC::kAnimateArgObeySkillInterrupt:				playParams.m_obeySkillInterrupt				=  args.NextBoolean(); break;
		case DC::kAnimateArgInterruptNavigation:			playParams.m_interruptNavigation			=  args.NextBoolean(); break;
		case DC::kAnimateArgExitPushObject:					playParams.m_exitPushObject					=  args.NextStringId(); break;
		case DC::kAnimateArgExitSwimOverlay:				playParams.m_exitSwimOverlay				=  args.NextStringId(); break;
		case DC::kAnimateArgDisableHiresAnimGeo:			playParams.m_disableHiresAnimGeo			=  args.NextBoolean(); break;
		case DC::kAnimateArgAllowHorseControl:				playParams.m_allowHorseControl              =  args.NextBoolean(); break;
		case DC::kAnimateArgFeatherBlendId:					playParams.m_featherBlendId					=  args.NextStringId(); break;
		case DC::kAnimateArgStickAbortFrameWithButtons:		playParams.m_stickAbortFrameWithButtons		=  args.NextBoolean(); break;
		case DC::kAnimateArgStickAbortFrameWithCamera:		playParams.m_stickAbortFrameWithCamera		=  args.NextBoolean(); break;
		case DC::kAnimateArgDodgeAbortFrame:				playParams.m_dodgeAbortFrame				= args.NextI32(); break;
		case DC::kAnimateArgUseHorseSaddleApRef:			playParams.m_useHorseSaddleApRef			=  args.NextBoolean(); break;
		case DC::kAnimateArgDontExitBeforeHorse:			playParams.m_dontExitBeforeHorse			=  args.NextBoolean(); break;
		case DC::kAnimateArgExitDemeanor:					playParams.m_exitDemeanor					=  args.NextI32(); break;
		case DC::kAnimateArgSplasherObjectState:			playParams.m_splasherObjectState			=  args.NextStringId(); break;
		case DC::kAnimateArgEnableFootPlantIk:				playParams.m_enableFootPlantIk				= args.NextBoolean(); break;
		case DC::kAnimateArgAllowFirstFrameUpdate:			playParams.m_allowFirstFrameUpdate			=  args.NextBoolean(); explicitlyRequestedAllowFFU = true; break;
		case DC::kAnimateArgAllowGestures:					playParams.m_allowGestures					=  args.NextBoolean(); break;
		case DC::kAnimateArgAllowStirrupsPartial:			playParams.m_allowStirrupsPartial			= args.NextBoolean(); break;
		case DC::kAnimateArgAllowWeaponIk:					playParams.m_allowWeaponIk					= args.NextBoolean(); break;
		case DC::kAnimateArgCinSequenceUsesCurrentAp:		playParams.m_cinSequenceUsesCurrentAp		= args.NextBoolean(); break;
		case DC::kAnimateArgNoClearAim:						playParams.m_noClearAim						= args.NextBoolean(); break;

		case DC::kAnimateArgAllowAnimateDeadIfRagdollNotActive:
			playParams.m_allowAnimateDeadIfRagdollNotActive = args.NextBoolean();
			break;

		case DC::kAnimateArgStickAbortFrame:
			{
				I32 abortFrameAsInt = args.NextI32();

				// Error if we suspect a float was passed in instead of an int
				if (IsLikelyFloat(abortFrameAsInt))
				{
					args.MsgScriptError("FAILED (%s '%s') : Invalid data for (animate-arg stick-abort-frame) parameter. Expected int32, got float.\n", goName, DevKitOnly_StringIdToString(animId));
					return ScriptValue(0);
				}

				playParams.m_stickAbortFrame = abortFrameAsInt;
			}
			break;


		case DC::kAnimateArgStartFrame:
			{
				const float startFrameAsFloat = args.NextFloat();

				// Error if we suspect an int was passed in instead of a float
				if (IsLikelyInt(startFrameAsFloat))
				{
					args.MsgScriptError("FAILED (%s '%s') : Invalid data for (animate-arg start-frame) parameter. Expected float, got int32.\n", goName, DevKitOnly_StringIdToString(animId));
					return ScriptValue(0);
				}

				const float startFrame = startFrameAsFloat;

				StringId64 cinematicId = CinematicTable::Get().LookupCinematicIdByAnimationId(playParams.m_nameId);

				// See if this animation is part of a cinematic.
				const float cinStartPhase = CinematicProcess::GetPhaseFromFrame(startFrame, pAnimControl, cinematicId);
				if (cinStartPhase != CinematicProcess::kInvalidTimeIndex)
				{
					playParams.m_startPhase = cinStartPhase;
				}
				else if (pArtItemAnim && pArtItemAnim->m_pClipData)
				{
					// Must not be a cinematic... use raw clip duration to resolve the phase.
					float duration30HzFrames = GetDuration(pArtItemAnim) * 30.0f;
					playParams.m_startPhase = (duration30HzFrames > 0.0f) ? Limit01(startFrame / duration30HzFrames) : 0.0f;
				}
				else
				{
					playParams.m_startPhase = 0.0f;
				}
			}
			break;

		case DC::kAnimateArgEndFrame:
			{
				const float endFrameAsFloat = args.NextFloat();

				// Error if we suspect an int was passed in instead of a float
				if (IsLikelyInt(endFrameAsFloat))
				{
					args.MsgScriptError("FAILED (%s '%s') : Invalid data for (animate-arg end-frame) parameter. Expected float, got int32.\n", goName, DevKitOnly_StringIdToString(animId));
					return ScriptValue(0);
				}

				const float endFrame = endFrameAsFloat;

				StringId64 cinematicId = CinematicTable::Get().LookupCinematicIdByAnimationId(playParams.m_nameId);

				// See if this animation is part of a cinematic.
				const float cinEndPhase = CinematicProcess::GetPhaseFromFrame(endFrame, pAnimControl, cinematicId);
				if (cinEndPhase != CinematicProcess::kInvalidTimeIndex)
				{
					playParams.m_endPhase = cinEndPhase;
				}
				else if (pArtItemAnim && pArtItemAnim->m_pClipData && endFrame >= 0.0f)
				{
					playParams.m_endPhase = GetClipPhaseForMayaFrame(pArtItemAnim->m_pClipData, endFrame); 
				}
			}
			break;

		case DC::kAnimateArgSpeedPct:
			{
				playParams.m_speed = args.NextFloat();
				if (!IsFinite(playParams.m_speed))
				{
					args.MsgScriptError("FAILED (%s '%s') : Invalid data for (animate-arg speed-pct) parameter\n", goName, DevKitOnly_StringIdToString(animId));
					return ScriptValue(0);
				}
			}
			break;

		case DC::kAnimateArgDesiredDurationSec:
			{
				playParams.m_desiredDurationSec = args.NextFloat();
				if (!IsFinite(playParams.m_desiredDurationSec) || playParams.m_desiredDurationSec == NDI_FLT_MAX)
				{
					args.MsgScriptError("FAILED (%s '%s') : Invalid data for (animate-arg desired-duration-sec) parameter\n", goName, DevKitOnly_StringIdToString(animId));
					return ScriptValue(0);
				}
			}
			break;

		case DC::kAnimateArgApRef:
			{
				const StringId64 isThisASid = args.GetStringId(); // don't advance yet
				const BoundFrame* pBoundFrame = args.NextBoundFrame(); // now advance

				if (pBoundFrame && pBoundFrame->GetBinding().IsSignatureValid()) // this might still crash, but we can at least try...
				{
					playParams.OverrideApOrigin(*pBoundFrame);
				}
				else
				{
					bool errorPrinted = false;
					#if !FINAL_BUILD
						if (Memory::IsDebugMemoryAvailable() && !EngineComponents::GetNdGameInfo()->m_onDisc)
						{
							const char* sidCheck = DevKitOnly_StringIdToStringOrNull(isThisASid);
							if (sidCheck)
							{
								args.MsgScriptError("arg %u: (animate-arg ap-ref) '%s -- looks like you passed a SID, not a bound-frame!!!\n", argIndex, sidCheck);
								errorPrinted = true;
							}
						}
					#endif
					if (!errorPrinted)
						args.MsgScriptError("arg %u: (animate-arg ap-ref) -- invalid bound-frame!\n", argIndex);
				}
			}
			break;

		case DC::kAnimateArgBindToHorse:
			{
				const StringId64 isThisASid = args.GetStringId();
				const DC::HorseBinding* pHorseBinding = PunPtr<const DC::HorseBinding*>(args.NextVoidPointer());

				if (pHorseBinding)
				{
					playParams.m_horseBinding = *pHorseBinding;
				}
				else
				{
					bool errorPrinted = false;
#if !FINAL_BUILD
					if (Memory::IsDebugMemoryAvailable() && !EngineComponents::GetNdGameInfo()->m_onDisc)
					{
						const char* sidCheck = DevKitOnly_StringIdToStringOrNull(isThisASid);
						if (sidCheck)
						{
							args.MsgScriptError("arg %u: (animate-arg bind-to-horse) '%s -- looks like you passed a SID, not a horse-binding!!!\n", argIndex, sidCheck);
							errorPrinted = true;
						}
					}
#endif
					if (!errorPrinted)
						args.MsgScriptError("arg %u: (animate-arg bind-to-horse) -- invalid horse-binding!\n", argIndex);
				}
			}
			break;

		default:
			args.MsgScriptError("Arg %u: invalid argument type\n", argIndex);
			break;
		}
	}

	if (args.GetArgIndex() != argc)
	{
		args.MsgScriptError("invalid anim argument pair\n");
	}

	if (!explicitlyRequestedAllowFFU
		&& ((playParams.m_startPhaseMode == DC::kAnimatePhasePrevious)
			|| playParams.m_startPhaseMode == DC::kAnimatePhaseOneMinusPrevious))
	{
		playParams.m_allowFirstFrameUpdate = true;
	}

	if ((playParams.m_stickAbortFrame != -1) && pArtItemAnim)
	{
		const float totalFrames = 30.0f * GetDuration(pArtItemAnim);
		const float fStickAbortFrame = playParams.m_stickAbortFrame;
		if (fStickAbortFrame < 0.0f || (fStickAbortFrame > totalFrames))
		{
			args.MsgScriptError("Trying to use stick abort frame '%d' out of range (%d)", playParams.m_stickAbortFrame, int(totalFrames));
			playParams.m_stickAbortFrame = -1;

			// No need to abort
			//return ScriptValue(0);
		}
	}

	if (playParams.m_searchForStartMaxPhase > -1.0f)
	{
		if (nullptr == pAnimControl)
		{
			args.MsgScriptError("'%s' is trying to play '%s' with search for start max phase, but has no AnimControl!\n", goName, DevKitOnly_StringIdToString(animId));
			return ScriptValue(0);
		}
		else if (playParams.m_apOriginValid)
		{
			const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();
			if (pAnim == nullptr)
			{
				args.MsgScriptError("Anim '%s' not found!\n", DevKitOnly_StringIdToString(animId));
			}
			else
			{
				const Locator alignWs = pGo->GetLocator();
				StringId64 apRefChannel = (playParams.m_customApRefId != INVALID_STRING_ID_64) ? playParams.m_customApRefId : SID("apReference");
				PhaseMatchParams params;
				params.m_apChannelId = apRefChannel;
				params.m_maxPhase = playParams.m_searchForStartMaxPhase;

				playParams.m_startPhase = ComputePhaseToMatchApAlign(pGo->GetSkeletonId(),
																	 pAnim,
																	 alignWs,
																	 playParams.m_apOrigin.GetLocatorWs(),
																	 params);
			}
		}
		else
		{
			args.MsgScriptError("Anim '%s' has search for start max phase, but no ApReference!\n", DevKitOnly_StringIdToString(animId));
			return ScriptValue(0);
		}
	}

	if (playParams.m_useHorseSaddleApRef)
	{
		if (playParams.m_apOriginValid)
		{
			args.MsgScriptError("'%s' is trying to play anim '%s' with both (animate-arg ap-ref) and (animate-arg use-horse-saddle-ap-ref), ignoring (animate-arg ap-ref)\n",
								goName,
								DevKitOnly_StringIdToString(animId));
		}
		playParams.m_apOriginValid = true;
	}
	
	if (playParams.m_horseBinding.Valid())
	{
		if (playParams.m_apOriginValid)
		{
			args.MsgScriptError("'%s' is trying to play anim '%s' with both (animate-arg ap-ref) and (animate-arg horse-binding), ignoring (animate-arg ap-ref)\n",
								goName,
								DevKitOnly_StringIdToString(animId));
		}
		playParams.m_apOriginValid = true;
	}

	Process* pSender = Process::GetContextProcess();
	PostEventFrom(pSender, SID("play-animation"), pGo, BoxedSsAnimateParams(&playParams));

/*	if (!response.GetBool())
	{
		args.MsgScriptError("%s FAILED (object '%s' anim '%s')\n", args.GetDcFunctionName(), goName, DevKitOnly_StringIdToString(animId));
		return ScriptValue(0);
	}*/

	return ScriptValue(1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcAnimate(int argc, ScriptValue* argv, ScriptFnAddr __SCRIPT_FUNCTION__)
{
	return DcPlayAnimation(argc, argv, __SCRIPT_FUNCTION__, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcWaitAnimate(int argc, ScriptValue* argv, ScriptFnAddr __SCRIPT_FUNCTION__)
{
	SCRIPT_ARG_ITERATOR(args, 0);

	SsInstance* pScriptInst				= args.GetContextScriptInstance();
	const SsContext& context			= GetJobSsContext();
	SsTrackGroupInstance* pGroupInst	= context.GetGroupInstance();
	SsTrackInstance* pTrackInst			= context.GetTrackInstance();

	if (pScriptInst && pGroupInst && pTrackInst)
	{
		StringId64 animId = INVALID_STRING_ID_64;
		ScriptValue result = DcPlayAnimation(argc, argv, __SCRIPT_FUNCTION__, true, &animId);

		if (result.m_int32 != 0)
		{
			// Success.
			pGroupInst->WaitForTrackDone("wait-animate");
			ScriptContinuation::Suspend(argv);
			return ScriptValue(1);
		}
		else
		{
			const NdGameObject* pGo = args.GetGameObject();
			const StringId64 userId = pGo ? pGo->GetScriptId() : INVALID_STRING_ID_64;

			// Wait one frame so that scripts which rely on there being some kind of
			// wait due to (wait-animate ...) won't get into an infinite loop.
			args.MsgScriptError("Animation '%s' did not play on %s -- waiting one frame to simulate time passing...\n",
				(argc >= 2) ? DevKitOnly_StringIdToString(animId) : "<unspecified>",
				DevKitOnly_StringIdToString(userId));

			return DcWaitInternal(1.0f/30.0f, argv);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-for-animate-done", DcWaitForAnimateDone)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pGo = args.NextGameObject();

	SsInstance* pScriptInst				= args.GetContextScriptInstance();
	const SsContext& context			= GetJobSsContext();
	SsTrackGroupInstance* pGroupInst	= context.GetGroupInstance();
	SsTrackInstance* pTrackInst			= context.GetTrackInstance();

	if (pGo && pScriptInst && pGroupInst && pTrackInst)
	{
		SsAnimateController* pSsAnimCtrl = pGo->GetPrimarySsAnimateController();
		if (pSsAnimCtrl)
		{
			// OK, it's playing some kind of IGC/cinematic... let's wait.
			Process* pSender = Process::GetContextProcess();
			if (pSsAnimCtrl->WaitForAnimationDone(pSender, pTrackInst->GetTrackIndex()))
			{
				pGroupInst->WaitForTrackDone("wait-animate");
				ScriptContinuation::Suspend(argv);
				return ScriptValue(1);
			}

			// Wait one frame so that scripts which rely on there being some kind of
			// wait due to (wait-animate ...) won't get into an infinite loop.
			args.MsgScriptError("Object '%s' is not playing an IGC/cinematic animation -- waiting one frame to simulate time passing...\n", pGo->GetName());

			return DcWaitInternal(1.0f/30.0f, argv);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue CallWithUnwrappedArgs(ScriptValue (*pFn)(int, ScriptValue*, ScriptFnAddr), 
										 int argc, 
										 ScriptValue* argv,
										 ScriptFnAddr __SCRIPT_FUNCTION__)
{
	if (argc >= 3)
	{
		const DC::BoxedArray* pArgs = static_cast<const DC::BoxedArray*>(argv[2].m_pointer);

		if (pArgs)
		{
			ScopedTempAllocator jj(FILE_LINE_FUNC);

			const U32 numArgPairs = pArgs->m_data->GetSize();

			const int newArgc = 2 + 2 * numArgPairs;
			ScriptValue* newArgv = NDI_NEW ScriptValue[newArgc+1];
			++newArgv;

			newArgv[-1] = argv[-1];
			newArgv[0] = argv[0];
			newArgv[1] = argv[1];

			for (U32 i = 0; i < numArgPairs; ++i)
			{
				const DC::AnimateArgPair* pArgPair = static_cast<const DC::AnimateArgPair*>(pArgs->m_data->At(i));

				newArgv[2 + 2 * i] = ScriptValue(pArgPair->m_argType);
				newArgv[2 + 2 * i + 1] = ScriptValue(pArgPair->m_arg);
			}

			const ScriptValue retVal = pFn(newArgc, newArgv, __SCRIPT_FUNCTION__);

			argv[-1] = newArgv[-1];

			return retVal;
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("animate_", DcAnimateInternal)
{
	return CallWithUnwrappedArgs(DcAnimate, argc, argv, __SCRIPT_FUNCTION__);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-animate_", DcWaitAnimateInternal)
{
	return CallWithUnwrappedArgs(DcWaitAnimate, argc, argv, __SCRIPT_FUNCTION__);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("allow-all-objects-to-animate-camera", DcAllowAllObjectsToAnimateCamera)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	const bool allow = args.NextBoolean();
	SsAnimateController::AllowAllObjectsToAnimateCamera(allow);
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("all-objects-allowed-to-animate-camera?", DcAllObjectsAllowedToAnimateCameraP)
{
	return ScriptValue(SsAnimateController::AreAllObjectsAllowedToAnimateCamera());
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("facial-animate", DcFacialAnimate)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* const pChar = args.NextGameObject();
	const StringId64 animName = args.NextStringId();
	const float durationSec = args.NextFloat();

	if (pChar)
	{
		SendEvent(SID("facial-animate"), pChar, animName, durationSec);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static F32 ComputePhaseFromFrame(const NdGameObject* pGo, StringId64 animId, F32 frame)
{
	const ArtItemAnim* pAnim = pGo->GetAnimControl()->LookupAnim(animId).ToArtItem();
	F32 startPhase = frame/30.0f/GetDuration(pAnim);
	return startPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-rotated-ap-ref", DcGetRotatedApRef)
{
	SCRIPT_ARG_ITERATOR(args, 4);

	NdGameObject* pGo = args.NextGameObject();
	StringId64 animName = args.NextStringId();
	const BoundFrame* pApRef = args.NextBoundFrame();
	const float phase = args.NextFloat();

	const Locator apRefLocWs   = pApRef->GetLocatorWs();
	Locator entryLocWs;
	if (FindAlignFromApReference(pGo->GetAnimControl(), animName, phase, apRefLocWs, &entryLocWs, false))
	{

		BoundFrame* pRotatedApRefOut = NDI_NEW(kAllocSingleGameFrame, kAlign16) BoundFrame(entryLocWs, pApRef->GetBinding());
	
		Locator rotatedAlignWsOut;
		GetRotatedApRefForEntry(pGo->GetSkeletonId(), pGo->GetAnimControl()->LookupAnim(animName).ToArtItem(), pGo->GetLocator(), *pApRef, entryLocWs, &rotatedAlignWsOut, pRotatedApRefOut );

		return ScriptValue(pRotatedApRefOut);
	}

	args.MsgScriptError("Unable to find rotated entry location from animation '%s' for object '%s'\n",
		DevKitOnly_StringIdToString(animName), pGo->GetName());

	return ScriptValue(pApRef);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("remove-anim-layer-ap-ref-binding", DcRemoveAnimLayerApRefBinding)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	//NdGameObject* pGo = args.NextGameObject();
	//StringId64 layerName = args.NextStringId();
	//
	//if (pGo)
	//{
	//	if (AnimControl* pAnimControl = pGo->GetAnimControl())
	//	{
	//		AnimStateLayer* pLayer = pAnimControl->GetStateLayerById(layerName);
	//		if (pLayer)
	//			pLayer->RemoveAllApReferencesBinding(); //@INTEGRATE
	//	}
	//}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-best-entry-phase", DcGetBestEntryPhase)
{
	SCRIPT_ARG_ITERATOR(args, 6);

	NdGameObject* pGo = args.NextGameObject();
	StringId64 animName = args.NextStringId();
	const BoundFrame* pApRef = args.NextBoundFrame();
	const float maxFrame = args.NextFloat();
	const StringId64 apRefChannel = args.NextStringId();
	const bool mirror = args.NextBoolean();

	AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;
	if (!pAnimControl)
	{
		return ScriptValue(0.0f);
	}

	const float maxPhase	   = Min(1.0f, ComputePhaseFromFrame(pGo, animName, maxFrame));

	const ArtItemAnim* pArtItemAnim = pAnimControl->LookupAnim(animName).ToArtItem();

	if (pArtItemAnim)
	{
		const Locator desAlignWs = pGo->GetLocator();
		const Locator apRefLocWs = pApRef->GetLocatorWs();

		PhaseMatchParams params;
		params.m_mirror		 = mirror;
		params.m_maxPhase	 = maxPhase;
		params.m_apChannelId = apRefChannel;

		const float phase = ComputePhaseToMatchApAlign(pGo->GetSkeletonId(),
													   pArtItemAnim,
													   desAlignWs,
													   apRefLocWs,
													   params);

		return ScriptValue(phase);
	}
	else
	{
		args.MsgScriptError("missing animation %s!", DevKitOnly_StringIdToString(animName));
	}

	return ScriptValue(0.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-animation-entry", DcGetAnimationEntry)
{
	SCRIPT_ARG_ITERATOR(args, 6);

	NdGameObject* pGo = args.NextGameObject();
	StringId64 animName = args.NextStringId();
	const BoundFrame* pApRef = args.NextBoundFrame();
	const bool mirror = args.NextBoolean();
	const float phase = args.NextFloat();
	const StringId64 apRefChannelArg = args.NextStringId();

	const StringId64 apRefChannel = (apRefChannelArg != INVALID_STRING_ID_64) ? apRefChannelArg : SID("apReference");

	if (pGo && animName != INVALID_STRING_ID_64 && pApRef)
	{
		const Locator apRefLocWs = pApRef->GetLocatorWs();

		Locator entryLocWs;
		if (FindAlignFromApReference(pGo->GetAnimControl(), animName, phase, apRefLocWs, apRefChannel, &entryLocWs, mirror))
		{
			BoundFrame* pResult = NDI_NEW(kAllocSingleGameFrame, kAlign16) BoundFrame(entryLocWs, pApRef->GetBinding());
			if (args.AllocSucceeded(pResult))
			{
				if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_displayScriptApEntryLoc) && DebugSelection::Get().IsProcessOrNoneSelected(pGo))
				{
					g_prim.Draw(DebugCoordAxesLabeled(apRefLocWs, "apRef", 0.5f, PrimAttrib(), 4.0f), Seconds(5.0f));
					g_prim.Draw(DebugLine(apRefLocWs.Pos(), entryLocWs.Pos()), Seconds(5.0f));
					g_prim.Draw(DebugCoordAxesLabeled(entryLocWs, StringBuilder<256>("apEntry: %s", DevKitOnly_StringIdToString(animName)).c_str()), Seconds(5.0f));
				}

				return ScriptValue(pResult);
			}
		}

		args.MsgScriptError("Unable to find entry location from animation '%s' for object '%s'\n",
							DevKitOnly_StringIdToString(animName),
							pGo->GetName());

		return ScriptValue(pApRef);
	}

	// prevent scripts from crashing!
	args.MsgScriptError("Returning (0,0,0) to avoid a crash! (animation '%s' object '%s')\n",
						DevKitOnly_StringIdToString(animName),
						pGo ? pGo->GetName() : "");
	static const BoundFrame kOriginBf(kIdentity);
	return ScriptValue(&kOriginBf);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("update-ap-ref", DcUpdateApRef)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	Process* pProcess = args.NextProcess();
	const BoundFrame* pBoundFrame = args.NextBoundFrame();
	bool currentAnimOnly = args.NextBoolean();

	if (pProcess && pBoundFrame)
	{
		SendEvent(SID("update-ap-ref"), pProcess, pBoundFrame, currentAnimOnly);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("has-anim?", DcHasAnimP)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGo = args.NextGameObject();
	StringId64 animId = args.NextStringId();

	if (pGo && pGo->GetAnimControl())
	{
		if (pGo->GetAnimControl()->LookupAnim(animId).ToArtItem())
			return ScriptValue(true);
	}		

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("stop-animating", DcStopAnimating)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	Process* pProcess = args.NextProcess(); // this works for any process that responds to the "set-animation-speed" event
	I32 layer = args.NextI32();
	float fadeTime = args.NextFloat();
	StringId64 animName = args.NextStringId();

	if (pProcess)
	{
		SsVerboseLog(1, "stop-animating %s", pProcess->GetName());

		BoxedValue response = SendEvent(SID("stop-animating"), pProcess, layer, fadeTime, animName);

		if (response.IsValid() && !response.GetBool())
		{
			args.MsgScriptError("FAILED (object: '%s' anim: '%s' layer: %d)\n", pProcess->GetName(), DevKitOnly_StringIdToString(animName), layer);
			return ScriptValue(0);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("stop-animating-by-layer-name", DcStopAnimatingByLayerName)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	Process* pProcess = args.NextProcess(); // this works for any process that responds to the "set-animation-speed" event
	StringId64 layerName = args.NextStringId();
	float fadeTime = args.NextFloat();

	if (pProcess && layerName != INVALID_STRING_ID_64)
	{
		SsVerboseLog(1, "stop-animating-by-layer-name %s", pProcess->GetName());

		BoxedValue response = SendEvent(SID("stop-animating-by-layer-name"), pProcess, layerName, fadeTime);

		if (response.IsValid() && !response.GetBool())
		{
			args.MsgScriptError("FAILED (object '%s')\n", pProcess->GetName());
			return ScriptValue(0);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("set-animate-blend", DcSetAnimateBlend)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	NdGameObject* pGo = args.NextGameObject();
	StringId64 blendParam = args.NextStringId();
	float blendVal = args.NextFloat();

	if (pGo)
	{
		BoxedValue result = SendEvent(SID("get-full-body-script-info"), pGo);
		if (!result.IsValid())
		{
			args.MsgScriptError("FAILED (object '%s' must be a Player or Npc)\n", pGo->GetName());
			return ScriptValue(0);
		}

		DC::AnimScriptInfo* pScriptInfo = result.GetPtr<DC::AnimScriptInfo*>();
		if (pScriptInfo == nullptr)
		{
			args.MsgScriptError("FAILED (object '%s' must be a Player or Npc)\n", pGo->GetName());
			return ScriptValue(0);
		}

		switch (blendParam.GetValue())
		{
			case SID_VAL("animate-blend-0"): pScriptInfo->m_animateBlend0 = blendVal; break;
			case SID_VAL("animate-blend-1"): pScriptInfo->m_animateBlend1 = blendVal; break;
			case SID_VAL("animate-blend-2"): pScriptInfo->m_animateBlend2 = blendVal; break;
			case SID_VAL("animate-blend-3"): pScriptInfo->m_animateBlend3 = blendVal; break;
			case SID_VAL("animate-blend-4"): pScriptInfo->m_animateBlend4 = blendVal; break;
			case SID_VAL("animate-blend-5"): pScriptInfo->m_animateBlend5 = blendVal; break;
			default:
				args.MsgScriptError("FAILED (blend-param must be between 'animate-blend-0 and 'animate-blend-5)\n");
				return ScriptValue(0);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-animate-blend", DcGetAnimateBlend)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pGo = args.NextGameObject();
	StringId64 blendParam = args.NextStringId();

	if (pGo)
	{
		BoxedValue result = SendEvent(SID("get-full-body-script-info"), pGo);
		if (!result.IsValid())
		{
			args.MsgScriptError("FAILED (object '%s' must be a Player or Npc)\n", pGo->GetName());
			return ScriptValue(0);
		}

		DC::AnimScriptInfo* pScriptInfo = result.GetPtr<DC::AnimScriptInfo*>();
		if (pScriptInfo == nullptr)
		{
			args.MsgScriptError("FAILED (object '%s' must be a Player or Npc)\n", pGo->GetName());
			return ScriptValue(0);
		}


		switch (blendParam.GetValue())
		{
			case SID_VAL("animate-blend-0"): return ScriptValue(pScriptInfo->m_animateBlend0);
			case SID_VAL("animate-blend-1"): return ScriptValue(pScriptInfo->m_animateBlend1);
			case SID_VAL("animate-blend-2"): return ScriptValue(pScriptInfo->m_animateBlend2);
			case SID_VAL("animate-blend-3"): return ScriptValue(pScriptInfo->m_animateBlend3);
			case SID_VAL("animate-blend-4"): return ScriptValue(pScriptInfo->m_animateBlend4);
			case SID_VAL("animate-blend-5"): return ScriptValue(pScriptInfo->m_animateBlend5);
			default:
				args.MsgScriptError("FAILED (blend-param must be between 'animate-blend-0 and 'animate-blend-5)\n");
				return ScriptValue(0);
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------------///
SCRIPT_FUNC("set-animate-anim-name", DcSetAnimateAnimName)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	NdGameObject* pGo = args.NextGameObject();
	StringId64 animIndex = args.NextStringId();
	StringId64 animName = args.NextStringId();

	if (pGo)
	{
		BoxedValue result = SendEvent(SID("get-full-body-script-info"), pGo);
		if (!result.IsValid())
		{
			args.MsgScriptError("FAILED (object '%s')\n", pGo->GetName());
			return ScriptValue(0);
		}

		DC::AnimScriptInfo* pScriptInfo = result.GetPtr<DC::AnimScriptInfo*>();
		if (pScriptInfo == nullptr)
		{
			args.MsgScriptError("FAILED (object '%s')\n", pGo->GetName());
			return ScriptValue(0);
		}

		switch (animIndex.GetValue())
		{
		case SID_VAL("anim-name-0"): pScriptInfo->m_animName0 = animName; break;
		case SID_VAL("anim-name-1"): pScriptInfo->m_animName1 = animName; break;
		case SID_VAL("anim-name-2"): pScriptInfo->m_animName2 = animName; break;
		case SID_VAL("anim-name-3"): pScriptInfo->m_animName3 = animName; break;
		case SID_VAL("anim-name-4"): pScriptInfo->m_animName4 = animName; break;
		case SID_VAL("anim-name-5"): pScriptInfo->m_animName5 = animName; break;
		default:
			args.MsgScriptError("FAILED (anim-index must be between 'anim-name-0 and 'anim-name-5)\n");
			return ScriptValue(0);
		}
	}

	return ScriptValue(0);
}

///-------------------------------------------------------------------------------------------------///
SCRIPT_FUNC("get-animate-anim-name", DcGetAnimateAnimName)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pGo = args.NextGameObject();
	StringId64 animIndex = args.NextStringId();

	if (pGo)
	{
		BoxedValue result = SendEvent(SID("get-full-body-script-info"), pGo);
		if (!result.IsValid())
		{
			args.MsgScriptError("FAILED (object '%s')\n", pGo->GetName());
			return ScriptValue(0);
		}

		DC::AnimScriptInfo* pScriptInfo = result.GetPtr<DC::AnimScriptInfo*>();
		if (pScriptInfo == nullptr)
		{
			args.MsgScriptError("FAILED (object '%s')\n", pGo->GetName());
			return ScriptValue(0);
		}

		StringId64 animName = INVALID_STRING_ID_64;
		switch (animIndex.GetValue())
		{
		case SID_VAL("anim-name-0"): animName = pScriptInfo->m_animName0; break;
		case SID_VAL("anim-name-1"): animName = pScriptInfo->m_animName1; break;
		case SID_VAL("anim-name-2"): animName = pScriptInfo->m_animName2; break;
		case SID_VAL("anim-name-3"): animName = pScriptInfo->m_animName3; break;
		case SID_VAL("anim-name-4"): animName = pScriptInfo->m_animName4; break;
		case SID_VAL("anim-name-5"): animName = pScriptInfo->m_animName5; break;
		default:
			args.MsgScriptError("FAILED (anim-index must be between 'anim-name-0 and 'anim-name-5)\n");
			return ScriptValue(INVALID_STRING_ID_64);
		}

		return ScriptValue(animName);
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-animation-playing?", DcGetAnimationPlayingP)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	Process* pProcess = args.NextProcess(); // this works for any process that responds to the "set-animation-speed" event
	StringId64 animName = args.NextStringId();
	I32 layer = args.NextI32();

	if (pProcess)
	{
		BoxedValue result = SendEvent(SID("get-animation-playing"), pProcess, animName, layer);

		SsVerboseLog(1, "get-animation-playing: '%s' playing '%s'", pProcess->GetName(), DevKitOnly_StringIdToString(animName));

		return ScriptValue(result.GetAsBool());
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-animation-fade", DcGetAnimationFadeP)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pProcess = args.NextGameObject(); // this works for any process that responds to the "set-animation-speed" event
	StringId64 animName = args.NextStringId();
	
	if (pProcess)
	{
		if (AnimSimpleLayer* pSimpLayer = pProcess->GetAnimControl()->GetSimpleLayerById(SID("base")))
		{
			for (int i = 0; i < pSimpLayer->GetNumInstances(); i++)
			{
				if (const AnimSimpleInstance* pInst = pSimpLayer->GetInstance(i))
				{
					if (pInst->GetAnimId() == animName)
					{
						return ScriptValue(pInst->GetFade());
					}
				}
			}
		}
		else if (AnimStateLayer* pStateLayer = pProcess->GetAnimControl()->GetBaseStateLayer())
		{
			for (int i = 0; i < pStateLayer->NumUsedTracks(); i++)
			{
				if (const AnimStateInstanceTrack* pTrack = pStateLayer->GetTrackByIndex(i))
				{
					for (int j = 0; j < pTrack->GetNumInstances(); j++)
					{
						const AnimStateInstance* pInst = pTrack->GetInstance(j);
						if (pInst->GetPhaseAnim() == animName)
						{
							return ScriptValue(pInst->GetEffectiveFade());
						}
					}
				}
			}
		}
	}

	return ScriptValue(0.0f);
}


/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("set-animation-speed", DcSetAnimationSpeed)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	Process* pProcess = args.NextProcess(); // this works for any process that responds to the "set-animation-speed" event
	float speed = args.NextFloat();
	I32 layer = args.NextI32();
	StringId64 animName = args.NextStringId();

	if (pProcess)
	{
		SsVerboseLog(1, "set-animation-speed: '%s' set to %g", pProcess->GetName(), speed);

		SendEvent(SID("set-animation-speed"), pProcess, speed, layer, animName);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("set-animation-phase", DcSetAnimationPhase)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	Process* pProcess = args.NextProcess(); // this works for any process that responds to the "set-animation-speed" event
	float phase = args.NextFloat();
	I32 layer = args.NextI32();
	StringId64 animName = args.NextStringId();

	if (pProcess)
	{
		SsVerboseLog(1, "set-animation-phase: '%s' set to %g", pProcess->GetName(), phase);

		if (phase < 0.0f || phase > 1.0f)
		{
			args.MsgScriptError("'%s' Tried to set invalid phase %f for anim '%s'\n",
								pProcess->GetName(),
								phase,
								DevKitOnly_StringIdToString(animName));
		}
		else
		{
			SendEvent(SID("set-animation-phase"), pProcess, phase, layer, animName);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("set-animation-layer-fade", DcSetAnimationLayerFade)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	Process* pProcess = args.NextProcess(); // this works for any process that responds to the "set-animation-speed" event
	float fade = args.NextFloat();
	float fadeTime = args.NextFloat();
	I32 layer = args.NextI32();
	StringId64 animName = args.NextStringId();

	if (pProcess)
	{
		SsVerboseLog(1, "set-animation-layer-fade: '%s' set to %g", pProcess->GetName(), fade);

		SendEvent(SID("set-animation-layer-fade"), pProcess, fade, fadeTime, layer, animName);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-animation-speed", DcGetAnimationSpeed)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	Process* pProcess = args.NextProcess(); // this works for any process that responds to the "set-animation-speed" event
	I32 layer = args.NextI32();
	StringId64 animId = args.NextStringId();

	float speed = 0.0f;

	if (pProcess)
	{
		BoxedValue result = SendEvent(SID("get-animation-speed"), pProcess, layer, animId);

		if (result.IsValid())
		{
			speed = result.GetFloat();
			SsVerboseLog(1, "get-animation-speed: '%s' = %g (%s)", pProcess->GetName(), speed, DevKitOnly_StringIdToString(animId));
		}
		else
		{
			args.MsgScriptError("'%s' (%s): Unable to determine speed of animation, assuming 0.0\n", pProcess->GetName(), DevKitOnly_StringIdToString(animId));
		}
	}

	return ScriptValue(speed);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-animation-phase", DcGetAnimationPhase)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	Process* pProcess = args.NextProcess(); // this works for any process that responds to the "set-animation-speed" event
	I32 layer = args.NextI32();
	StringId64 animId = args.NextStringId();

	float phase = 0.0f;

	if (pProcess)
	{
		BoxedValue result;
		if (!g_bSendEventToVFunc)
			result = SendEvent(SID("get-animation-phase"), pProcess, layer, animId);

		if (result.IsValid() || g_bSendEventToVFunc)
		{
			phase = g_bSendEventToVFunc ? pProcess->GetAnimationPhaseDirect(layer, animId) : result.GetFloat();
			SsVerboseLog(1, "get-animation-phase: '%s' = %g (%s)", pProcess->GetName(), phase, DevKitOnly_StringIdToString(animId));
		}
		else
		{
			args.MsgScriptError("'%s' (%s): Unable to determine phase of animation, assuming 0.0\n", pProcess->GetName(), DevKitOnly_StringIdToString(animId));
		}
	}

	return ScriptValue(phase);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-animation-frame", DcGetAnimationFrame)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	Process* pProcess = args.NextProcess(); // this works for any process that responds to the "set-animation-speed" event
	I32 layer = args.NextI32();
	StringId64 animId = args.NextStringId();

	float phase = 0.0f;

	if (pProcess)
	{
		BoxedValue result = SendEvent(SID("get-animation-frame"), pProcess, layer, animId);

		if (result.IsValid())
		{
			phase = result.GetFloat();
			SsVerboseLog(1, "get-animation-frame: '%s' = %g (%s)", pProcess->GetName(), phase, DevKitOnly_StringIdToString(animId));
		}
		else
		{
			args.MsgScriptError("'%s' (%s): Unable to determine frame of animation, assuming 0.\n", pProcess->GetName(), DevKitOnly_StringIdToString(animId));
		}
	}

	return ScriptValue(phase);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-animation-duration", DcGetAnimationDuration)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pGameObject = args.NextGameObject();
	F32 duration = -1.0f;
	if (pGameObject)
	{
		if (pGameObject->GetAnimControl())
		{
			StringId64 animName = args.NextStringId();
			if (const ArtItemAnim* pAnim = pGameObject->GetAnimControl()->LookupAnim(animName).ToArtItem())
			{
				duration = GetDuration(pAnim);
			}
		}
	}
	return ScriptValue(duration);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("player-lerp-anim-update-rate", DcPlayerLerpAnimUpdateRate)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	StringId64 animId[2];
	float updateRate[2] = {0, 0};

	const DC::AnimState* pState = args.NextPointer<DC::AnimState>();
	animId[0] = args.NextStringId();
	animId[1] = args.NextStringId();
	float lerpBlend = args.NextFloat();

	NdGameObject* pPlayerGo = EngineComponents::GetNdGameInfo()->GetPlayerGameObject();
	if (!pPlayerGo || !pPlayerGo->GetAnimControl())
		return ScriptValue(0.0f);

	const AnimControl* pAnimControl = pPlayerGo->GetAnimControl();
	const AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays();

	for (U32 i = 0; i < 2; ++i)
	{
		StringId64 translatedAnimId =	pOverlays->LookupTransformedAnimId(animId[i]);
		const ArtItemAnim* pArtItemAnim = pAnimControl->LookupAnim(translatedAnimId).ToArtItem();

		if (pArtItemAnim)
		{
			const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;

			// sample-rate / numFrames 
			if (pClipData->m_numTotalFrames > 1)
			{
				updateRate[i] = pClipData->m_framesPerSecond * pClipData->m_phasePerFrame;
			}
		}
	}

	// Hack so that a one frame animation advances phase.
	if (updateRate[0] == 0.0f) updateRate[0] = updateRate[1];
	if (updateRate[1] == 0.0f) updateRate[1] = updateRate[0];

	return ScriptValue(Lerp(updateRate[0], updateRate[1], lerpBlend));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("player-get-anim-update-rate", DcPlayerGetAnimUpdateRate)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	StringId64 animId = args.NextStringId();

	NdGameObject* pPlayerGo = EngineComponents::GetNdGameInfo()->GetPlayerGameObject();
	if (!pPlayerGo || !pPlayerGo->GetAnimControl())
		return ScriptValue(0.0f);

	const AnimControl* pAnimControl = pPlayerGo->GetAnimControl();
	const AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays();
	float updateRate = 0.f;
	
	StringId64 translatedAnimId =	pOverlays->LookupTransformedAnimId(animId);
	const ArtItemAnim* pArtItemAnim = pAnimControl->LookupAnim(translatedAnimId).ToArtItem();

	if (pArtItemAnim)
	{
		const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;

		// sample-rate / numFrames 
		if (pClipData->m_numTotalFrames > 1)
		{
			updateRate = pClipData->m_framesPerSecond * pClipData->m_phasePerFrame;
		}
	}	

	//may need a hack so that a one frame animation advances phase.
	return ScriptValue(updateRate);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("player-anim-streaming-phase", DcPlayerGetAnimStreamingPhase)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	StringId64 phaseAnimId = args.NextStringId();
	F32 oldPhase = args.NextFloat();
	F32 delta = args.NextFloat();
	
	NdGameObject* pPlayerGo = EngineComponents::GetNdGameInfo()->GetPlayerGameObject();
	if (!pPlayerGo || !pPlayerGo->GetAnimControl())
		return ScriptValue(0.0f);

	const AnimControl* pAnimControl = pPlayerGo->GetAnimControl();

	//StringId64 phaseAnim = pAnimControl->GetBaseStateLayer()->GetInstance(0)->GetPhaseAnim();
	const AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays();
	StringId64 phaseAnim = pOverlays->LookupTransformedAnimId(phaseAnimId);
	
	const ArtItemAnim* pArtItemAnim = pAnimControl->LookupAnim(phaseAnim).ToArtItem();

	if (pArtItemAnim && (pArtItemAnim->m_flags & ArtItemAnim::kStreaming))
	{
		if (!AnimStreamHasLoaded(pArtItemAnim, phaseAnim, fmod(oldPhase + delta, 1.0f)))
		{
			return ScriptValue(oldPhase);
		}
	}	

	//may need a hack so that a one frame animation advances phase.
	return ScriptValue(oldPhase + delta);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-object-anim-state", DcGetObjectAnimState)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pGo = args.NextGameObject();

	StringId64 animStateId = INVALID_STRING_ID_64;
	if (pGo)
	{
		animStateId = pGo->GetCurrentAnimState();
	}

	return ScriptValue(animStateId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-object-anim", DcGetObjectAnim)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pGo = args.NextGameObject();
	bool usePhaseAnim = args.NextBoolean();

	StringId64 animId = INVALID_STRING_ID_64;
	if (pGo)
	{
		animId = pGo->GetCurrentAnim(usePhaseAnim);
	}

	return ScriptValue(animId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("object-anim-playing?", DcObjectAnimPlayingP)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	NdGameObject* pGo = args.NextGameObject();
	StringId64 animId = args.NextStringId();
	bool anyLayer = args.NextBoolean();

	bool playing = false;
	if (pGo && animId != INVALID_STRING_ID_64)
	{
		StringId64 layer = anyLayer ? INVALID_STRING_ID_64 : SID("base");
		playing = pGo->IsAnimPlaying(animId, layer);
	}

	return ScriptValue(playing);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("simple-gesture", DcSimpleGesture)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	Process* pProcess = args.NextProcess();
	StringId64 animBase = args.NextStringId();

	Process* pSender = Process::GetContextProcess();
	SendEventFrom(pSender, SID("play-simple-gesture"), pProcess, animBase);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("set-overlay-set", DcSetOverlaySet)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGameObject = args.NextGameObject();
	StringId64 overlaySetId = args.NextStringId();

	if (!pGameObject)
		return ScriptValue(false);

	AnimControl* pAnimControl = pGameObject->GetAnimControl();
	if (!pAnimControl)
		return ScriptValue(false);

	AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays();
	if (!pOverlays)
		return ScriptValue(false);

	if (!pOverlays->SetOverlaySet(overlaySetId, true))
		return ScriptValue(false);

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("clear-overlay-layer", DcClearOverlayLayer)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGameObject = args.NextGameObject();
	StringId64 layerId = args.NextStringId();

	if (!pGameObject)
		return ScriptValue(false);

	AnimControl* pAnimControl = pGameObject->GetAnimControl();
	if (!pAnimControl)
		return ScriptValue(false);

	AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays();
	if (!pOverlays)
		return ScriptValue(false);

	pOverlays->ClearLayer(layerId);

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("calculate-pose-error", DcCalculatePoseError)
{
	SCRIPT_ARG_ITERATOR(args, 5);
	const NdGameObject* pGo = args.NextGameObject();
	const StringId64 animId = args.NextStringId();
	const float animPhase = args.NextFloat();
	const bool mirror = args.NextBoolean();
	const DC::SymbolArray* pJoints = args.NextPointer<DC::SymbolArray>();
	const DC::FloatArray* pWeights = args.NextPointer<DC::FloatArray>();
	const bool debug = args.NextBoolean();

	float error = kLargestFloat;
	if (pGo && animId != INVALID_STRING_ID_64 && pJoints && (pJoints->m_count > 0) && (!pWeights || pWeights->m_count == pJoints->m_count))
	{
		const AnimControl* pAnimControl = pGo->GetAnimControl();
		const JointCache* pJointCache = pAnimControl->GetJointCache();
		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();

		if (pAnim)
		{
			AllocateJanitor janitor(kAllocSingleGameFrame, FILE_LINE_FUNC);

			// Since we can't just get the positions of the animated joints out without
			// actually animating it, we're going to have to animate the object first. :(
			const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(pAnim->m_skelID).ToArtItem();

			// Figure out our joint indices - the one at the end of the array is the root,
			// which we use as the origin of the coordinate space.
			U32* pJointIndices = NDI_NEW U32[pJoints->m_count + 1];
			U32* pArrayIndices = NDI_NEW U32[pJoints->m_count + 1];
			
			U32F validJointCount = 0;

			for (U32F i = 0; i < pJoints->m_count; ++i)
			{
				StringId64 jointId = pJoints->m_array[i];

				const I32F jointIndex = pGo->FindJointIndex(jointId);
				if (jointIndex < 0)
				{
					args.MsgScriptError("Could not find joint \'%s\' in joint cache!\n", DevKitOnly_StringIdToString(jointId));
					continue;
				}

				pJointIndices[validJointCount] = jointIndex;
				pArrayIndices[validJointCount] = i;
				++validJointCount;
			}

			if (validJointCount == 0)
			{
				args.MsgScriptError("No valid joints!\n");
				return ScriptValue(0.0f);
			}

			// Last index is always root
			pArrayIndices[validJointCount] = 0;
			pJointIndices[validJointCount] = 0;

			// Now animate the joints to find their object-space positions at the specified phase
			Transform* pJointTransforms = NDI_NEW Transform[validJointCount + 1];

			// Since we're working in object-space, we specify the origin as the object transform
			const float sample = pAnim->m_pClipData->m_numTotalFrames * animPhase;
			AnimateJoints(Transform(kIdentity), pSkel, pAnim, sample, pJointIndices, validJointCount + 1, pJointTransforms, nullptr, nullptr, nullptr);

			error = 0.0f;

			const Locator animRootOs = Locator(pJointTransforms[validJointCount]);
			const Locator &rootWs = pJointCache->GetJointLocatorWs(0);

			for (U32F i = 0; i < validJointCount; ++i)
			{
				const float weight = pWeights ? pWeights->m_array[pArrayIndices[i]] : 1.0f;
				const U32F jointIndex = pJointIndices[i];

				ANIM_ASSERT(jointIndex < pJointCache->GetNumTotalJoints());

				// Convert both joint transforms into root-joint-space.
				const Locator &jointWs = pJointCache->GetJointLocatorWs(jointIndex);
				Locator jointRs = rootWs.UntransformLocator(jointWs);

				// Mirror if necessary!
				if (mirror)
				{
					Point trans = jointRs.GetTranslation();
					trans.SetX(-1.0f * trans.X());
					jointRs.SetTranslation(trans);

					//Quat rot = jointRs.GetRotation();
					//rot = Quat(-rot.X(), rot.Y(), rot.Z(), -rot.W());
					//jointRs.SetRotation(rot);
				}

				const Locator animJointOs = Locator(pJointTransforms[i]);
				const Locator animJointRs = animRootOs.UntransformLocator(animJointOs);

				// The error is currently just the difference between the positions. We can figure out a better
				// metric later, if necessary.
				Scalar dist = Dist(jointRs.GetPosition(), animJointRs.GetTranslation());
				Scalar jointError = dist * weight;

				// Debug drawing!
				if (debug)
				{
					// Draw in world space; bring the possibly mirrored joint back
					const Locator dbgJointWs = rootWs.TransformLocator(jointRs);
					const Locator dbgAnimJointWs = pGo->GetLocator().TransformLocator(animRootOs.TransformLocator(animJointRs));

					const Point midpoint = Lerp(dbgJointWs.GetTranslation(), dbgAnimJointWs.GetTranslation(), SCALAR_LC(0.5f));

					DebugPrimTime tt = kPrimDuration1FrameAuto;

					g_prim.Draw(DebugStringFmt(midpoint, kColorYellow, 0.5f,
						"%s %s: %f", DevKitOnly_StringIdToString(animId), DevKitOnly_StringIdToString(pJoints->m_array[pArrayIndices[i]]), (float)jointError), tt);

					g_prim.Draw(DebugCross(dbgJointWs.GetTranslation(), 0.1f, 2.0f, kColorGreen, PrimAttrib(kPrimEnableHiddenLineAlpha)), tt);
					g_prim.Draw(DebugCross(dbgAnimJointWs.GetTranslation(), 0.1f, 2.0f, kColorBlue, PrimAttrib(kPrimEnableHiddenLineAlpha)), tt);
					g_prim.Draw(DebugLine(dbgJointWs.GetTranslation(), dbgAnimJointWs.GetTranslation(), kColorGreen, kColorBlue, 2.0f, PrimAttrib(kPrimEnableHiddenLineAlpha)), tt);
				}

				error += jointError;
			}
		}
		else
		{
			args.MsgScriptError("Could not find animation \'%s\'!\n", DevKitOnly_StringIdToString(animId));
		}
	}
	else
	{
		// Errors!
		if (animId == INVALID_STRING_ID_64)
		{
			args.MsgScriptError("No animation specified!\n");
		}
		else if (!pJoints || pJoints->m_count <= 0)
		{
			args.MsgScriptError("Joints symbol-array #f or has no elements!\n");
		}
		else if (pWeights && pWeights->m_count != pJoints->m_count)
		{
			args.MsgScriptError("Weights float-array has different number of elements than joints symbol-array.\n");
		}
	}

	return ScriptValue(error);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("calculate-pose-error-simple", DcCalculatePoseErrorSimple)
{
	SCRIPT_ARG_ITERATOR(args, 5);
	const NdGameObject* pGo = args.NextGameObject();
	const StringId64 animId = args.NextStringId();
	const float animPhase = args.NextFloat();
	const bool debug = args.NextBoolean();
	const BoundFrame* pFrame = args.NextBoundFrame();

	F32 error = CalculatePoseError(pGo, animId, animPhase, debug, pFrame);
	return ScriptValue(error);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("calculate-anim-error", DcCalculateAnimError)
{
	SCRIPT_ARG_ITERATOR(args, 6);
	NdGameObject* pGo = args.NextGameObject();
	StringId64 animIdA = args.NextStringId();
	float animPhaseA = args.NextFloat();
	StringId64 animIdB = args.NextStringId();
	float animPhaseB = args.NextFloat();
	const DC::SymbolArray* pJoints = args.NextPointer<DC::SymbolArray>();
	const DC::FloatArray* pWeights = args.NextPointer<DC::FloatArray>();

	float error = kLargestFloat;
	if (pGo && (animIdA != INVALID_STRING_ID_64 && animIdB != INVALID_STRING_ID_64) && pJoints && (pJoints->m_count > 0) && (!pWeights || pWeights->m_count == pJoints->m_count))
	{
		const AnimControl* pAnimControl = pGo->GetAnimControl();
		const JointCache* pJointCache = pAnimControl->GetJointCache();
		const ArtItemAnim* pAnimA = pAnimControl->LookupAnim(animIdA).ToArtItem();
		const ArtItemAnim* pAnimB = pAnimControl->LookupAnim(animIdB).ToArtItem();

		if (pAnimA && pAnimB)
		{
			AllocateJanitor janitor(kAllocSingleGameFrame, FILE_LINE_FUNC);

			// Since we can't just get the positions of the animated joints out without
			// actually animating it, we're going to have to animate the object first. :(
			const ArtItemSkeleton* pSkelA = ResourceTable::LookupSkel(pAnimA->m_skelID).ToArtItem();
			const ArtItemSkeleton* pSkelB = ResourceTable::LookupSkel(pAnimB->m_skelID).ToArtItem();

			// Figure out our joint indices - the one at the end of the array is the root,
			// which we use as the origin of the coordinate space.
			U32* pJointIndices = NDI_NEW U32[pJoints->m_count + 1];
			bool* pValidJoints = NDI_NEW bool[pJoints->m_count];

			for (U32F i = 0; i < pJoints->m_count; ++i)
			{
				StringId64 jointId = pJoints->m_array[i];

				I32F jointIndex = pGo->FindJointIndex(jointId);
				if (jointIndex < 0)
				{
					args.MsgScriptError("Could not find joint \'%s\' in joint cache!\n", DevKitOnly_StringIdToString(jointId));
					pJointIndices[i] = 0;
					pValidJoints[i] = false;
				}

				pJointIndices[i] = jointIndex;
				pValidJoints[i] = true;
			}

			pJointIndices[pJoints->m_count] = 0;

			// Now animate the joints to find their object-space positions at the specified phase
			Transform* pJointTransformsA = NDI_NEW Transform[pJoints->m_count + 1];
			Transform* pJointTransformsB = NDI_NEW Transform[pJoints->m_count + 1];

			// Since we're working in object-space, we specify the origin as the object transform
			const float sampleA = pAnimA->m_pClipData->m_numTotalFrames * animPhaseA;
			const float sampleB = pAnimB->m_pClipData->m_numTotalFrames * animPhaseB;
			AnimateJoints(Transform(kIdentity), pSkelA, pAnimA, sampleA, pJointIndices, pJoints->m_count + 1, pJointTransformsA, nullptr, nullptr, nullptr);
			AnimateJoints(Transform(kIdentity), pSkelB, pAnimB, sampleB, pJointIndices, pJoints->m_count + 1, pJointTransformsB, nullptr, nullptr, nullptr);

			error = 0.0f;

			const Locator animRootAOs = Locator(pJointTransformsA[pJoints->m_count]);
			const Locator animRootBOs = Locator(pJointTransformsB[pJoints->m_count]);

			for (U32F i = 0; i < pJoints->m_count; ++i)
			{
				// If it's invalid, skip it.
				if (!pValidJoints[i])
					continue;

				float weight = pWeights ? pWeights->m_array[i] : 1.0f;
				U32F jointIndex = pJointIndices[i];

				// Convert both joint transforms into root-joint-space.
				const Locator animJointAOs = Locator(pJointTransformsA[i]);
				const Locator animJointBOs = Locator(pJointTransformsB[i]);
				const Locator animJointARs = animRootAOs.UntransformLocator(animJointAOs);
				const Locator animJointBRs = animRootBOs.UntransformLocator(animJointBOs);

				// The error is currently just the difference between the positions. We can figure out a better
				// metric later, if necessary.
				Scalar dist = Dist(animJointARs.GetTranslation(), animJointBRs.GetTranslation());

				error += dist * weight;
			}
		}
		else
		{
			args.MsgScriptError("Could not find animation \'%s\'!\n", DevKitOnly_StringIdToString(pAnimA ? animIdA : animIdB));
		}
	}
	else
	{
		// Errors!
		if (animIdA == INVALID_STRING_ID_64 || animIdB == INVALID_STRING_ID_64)
		{
			args.MsgScriptError("No animations specified!\n");
		}
		else if (!pJoints || pJoints->m_count <= 0)
		{
			args.MsgScriptError("Joints symbol-array #f or has no elements!\n");
		}
		else if (pWeights && pWeights->m_count != pJoints->m_count)
		{
			args.MsgScriptError("Weights float-array has different number of elements than joints symbol-array.\n");
		}
	}

	return ScriptValue(error);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("disable-anim-randomization", DcDisableAnimRandomization)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGo = args.NextGameObject();
	const bool disable = args.NextBoolean();

	AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;
	if (pAnimControl)
	{
		pAnimControl->SetRandomizationDisabled(disable);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-output-control", DcGetOutputControl)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGo = args.NextGameObject();
	const char* pControlName = args.NextString();

	FgAnimData* pAnimData = pGo ? pGo->GetAnimData() : nullptr;

	float val = 0.0f;

	if (pAnimData && pControlName)
	{
		const JointCache& jc = pAnimData->m_jointCache;
		const U32F numOutputControls = jc.GetNumOutputControls();

		I32F foundIndex = -1;

		for (U32F i = 0; i < numOutputControls; ++i)
		{
			const char* pName = pAnimData->GetOutputControlName(i);
			if (0 == strcmp(pControlName, pName))
			{
				foundIndex = i;
				break;
			}
		}

		if (foundIndex >= 0)
		{
			val = jc.GetOutputControl(foundIndex);
		}
		else
		{
			args.MsgScriptError("Float control '%s' not found on '%s'\n", pControlName, pGo->GetName());
		}
	}

	return ScriptValue(val);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("set-anim-blend-overlay", DcSetAnimBlendOverlay)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGo = args.NextGameObject();
	StringId64 blendOverlayId = args.NextStringId();

	if (pGo)
	{
		pGo->GetAnimControl()->SetCustomStateBlendOverlay(blendOverlayId);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("clear-anim-blend-overlay", DcClearAnimBlendOverlay)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	NdGameObject* pGo = args.NextGameObject();

	if (pGo)
	{
		pGo->GetAnimControl()->SetCustomStateBlendOverlay(INVALID_STRING_ID_64);
	}
	return ScriptValue(0);
}


/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-layer-animation-phase", DcGetBaseLayerAnimPhase)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	Process* pProcess = args.NextProcess(); 
	StringId64 layerId = args.NextStringId();

	float phase = 0.0f;
	bool valid = false;
	if (pProcess)
	{
		if (const NdGameObject* pGob = NdGameObject::FromProcess(pProcess))
		{
			if (AnimControl* pAnimControl = pGob->GetAnimControl())
			{
				if (AnimStateLayer* pBaseLayer = pAnimControl->GetStateLayerById(layerId))
				{
					if (AnimInstance* pInst = pBaseLayer->CurrentStateInstance())
					{
						phase = pInst->GetPhase();
						valid = true;
					}
				}
				else if (AnimSimpleLayer* pBaseSimpleLayer = pAnimControl->GetSimpleLayerById(layerId))
				{
					if (AnimInstance* pInst = pBaseSimpleLayer->CurrentInstance())
					{
						phase = pInst->GetPhase();
						valid = true;
					}
				}
			}
		}

		if (!valid)
		{
			args.MsgScriptError("'%s': Unable to determine phase of animation, assuming 0.0\n", pProcess->GetName());
		}
	}

	return ScriptValue(phase);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-layer-animation-frame", DcGetBaseLayerAnimFrame)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	Process* pProcess = args.NextProcess(); // this works for any process that responds to the "set-animation-speed" event
	StringId64 layerId = args.NextStringId();

	float frame = 0.0f;
	bool valid = false;
	if (pProcess)
	{
		if (const NdGameObject* pGob = NdGameObject::FromProcess(pProcess))
		{
			if (AnimControl* pAnimControl = pGob->GetAnimControl())
			{
				if (AnimStateLayer* pBaseLayer = pAnimControl->GetStateLayerById(layerId))
				{
					if (AnimStateInstance* pInst = pBaseLayer->CurrentStateInstance())
					{
						frame = pInst->MayaFrame();
						valid = true;
					}
				}
				else if (AnimSimpleLayer* pBaseSimpleLayer = pAnimControl->GetSimpleLayerById(layerId))
				{
					if (AnimSimpleInstance* pInst = pBaseSimpleLayer->CurrentInstance())
					{
						frame = pInst->GetMayaFrame();
						valid = true;
					}
				}
			}
		}

		if (!valid)
		{
			args.MsgScriptError("'%s': Unable to determine phase of animation, assuming 0.0\n", pProcess->GetName());
		}
	}

	return ScriptValue(frame);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("anim-exists-in-table?", DcAnimExistsInTableP)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	StringId64 animId = args.NextStringId();
	const AnimTable* pAnimTable = args.NextPointer<AnimTable>();

	bool found = false;

	if ((animId != INVALID_STRING_ID_64) && pAnimTable)
	{
		found = pAnimTable->LookupAnim(animId).ToArtItem() != nullptr;
	}

	return ScriptValue(found);
}


/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("angle-mod", DcAngleMod)
{
	return ScriptValue(AngleMod(argv[0].m_float));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("from-ups", DcFromUps)
{
	return ScriptValue(FromUPS(argv[0].m_float, argv[1].m_float));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("lerp-scale", DcLerpScale)
{
	float lower_input = argv[0].m_float;
	float upper_input = argv[1].m_float;
	float zero_val = argv[2].m_float;
	float one_val = argv[3].m_float;
	float tt = argv[4].m_float;
	return ScriptValue(::LerpScale(lower_input, upper_input, zero_val, one_val, tt));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("lerp", DcLerp)
{
	float lower = argv[0].m_float;
	float upper = argv[1].m_float;
	float tt = argv[2].m_float;
	return ScriptValue(lower + (upper-lower) * tt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("vector-lerp-scale", DcVectorLerpScale)
{
	SCRIPT_ARG_ITERATOR(args, 5);
	const Vector lower_input = args.NextVector();
	const Vector upper_input = args.NextVector();
	const Vector zero_vec = args.NextVector();
	const Vector one_vec = args.NextVector();
	const Vector tt_vec = args.NextVector();

	Vector * pVec = NDI_NEW(kAllocSingleGameFrame) Vector(LerpScale(lower_input, upper_input, zero_vec, one_vec, tt_vec));

	return ScriptValue(pVec);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function make-curve ((value float) (type anim-curve-type))
float)
*/
SCRIPT_FUNC("make-curve", DcEaseOutCurve)
{
	const float tt = argv[0].m_float;
	const DC::AnimCurveType type = argv[1].m_int32;
	return ScriptValue(CalculateCurveValue(tt, type));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("calc-ankle-match-phase", DcCalcAnkleMatchPhaseVel)
{
	float defaultPhase = 0.0f;
	float maxPhase = 1.0f;
	bool mirror = false;
	if (argc < 3)
		return ScriptValue(defaultPhase);

	if (argc >= 4)
	{
		defaultPhase = argv[3].m_float;
	}

	if (argc >= 5)
	{
		maxPhase = argv[4].m_float;
	}

	if (argc >= 6)
	{
		mirror = argv[5].m_boolean;
	}

	const DC::AnkleInfo* pAnkleInfo = (const DC::AnkleInfo*)argv[0].m_pointer;

	if (!pAnkleInfo)
		return ScriptValue(defaultPhase);

	const DC::AnimStateSnapshotInfo* pSnapshotInfo = (const DC::AnimStateSnapshotInfo*)argv[1].m_pointer;
	const bool matchVel = argv[2].m_boolean;

	if (!pSnapshotInfo)
		return ScriptValue(defaultPhase);

	const StringId64 animId = pSnapshotInfo->m_translatedPhaseAnimName;
	const SkeletonId skelId = SkeletonId(pSnapshotInfo->m_translatedPhaseAnimSkelId);
	const U32 hierarchyId = pSnapshotInfo->m_translatedPhaseAnimHierarchyId;
	const AnimStateSnapshot* pSnapshot = pSnapshotInfo->m_stateSnapshot;

	const ArtItemAnim* pAnim = AnimMasterTable::LookupAnim(skelId, hierarchyId, animId).ToArtItem();
	if (!pAnim)
		return ScriptValue(defaultPhase);

	if (FALSE_IN_FINAL_BUILD(FindChannel(pAnim, SID("rAnkle")) == nullptr || FindChannel(pAnim, SID("lAnkle")) == nullptr))
	{
		//MsgConErr("Animation %s missing left and right foot channels!  Please correct!\n", pAnim->GetName()); // unnecessary spam at this point (verified with Cowbs)
		return ScriptValue(defaultPhase);
	}

	const Point lAnkleOs = pAnkleInfo->m_leftAnklePos;
	const Point rAnkleOs = pAnkleInfo->m_rightAnklePos;
	const Vector lAnkleVel = pAnkleInfo->m_leftAnkleVel;
	const Vector rAnkleVel = pAnkleInfo->m_rightAnkleVel;

	PoseMatchInfo mi;
	mi.m_startPhase = 0.0f;
	mi.m_endPhase = maxPhase;
	mi.m_rateRelativeToEntry0 = true;
	mi.m_strideTransform = (pSnapshot && pSnapshot->IsAnimDeltaTweakEnabled()) ? pSnapshot->GetAnimDeltaTweakTransform() : kIdentity;
	mi.m_inputGroundNormalOs = pAnkleInfo->m_groundNormal;
	mi.m_mirror = pSnapshot->IsFlipped();

	mi.m_entries[0].m_channelId = pSnapshot->IsFlipped() ? SID("rAnkle") : SID("lAnkle");
	mi.m_entries[0].m_matchPosOs = lAnkleOs;
	mi.m_entries[0].m_matchVel = lAnkleVel;
	mi.m_entries[0].m_velocityValid = matchVel;
	mi.m_entries[0].m_valid = true;

	mi.m_entries[1].m_channelId =  pSnapshot->IsFlipped() ?  SID("lAnkle") : SID("rAnkle");
	mi.m_entries[1].m_matchPosOs = rAnkleOs;
	mi.m_entries[1].m_matchVel = rAnkleVel;
	mi.m_entries[1].m_velocityValid = matchVel;
	mi.m_entries[1].m_valid = true;

	// this is really for the super long infected walks
	{
		const ArtItemAnim* pAnimLs = pAnim;
		const ndanim::ClipData* pClipDataLs = pAnimLs->m_pClipData;

		if (pClipDataLs->m_fNumFrameIntervals > 60.0f)
		{
			mi.m_earlyOutThreshold = 2.0f;
		}
	}

	//mi.m_debugPrint = true;
	//mi.m_debug = true;
	//mi.m_debugDrawLoc = NdLocatableObject::FromProcess(Process::GetContextProcess())->GetLocator();
	//mi.m_debugDrawTime = Seconds(5.0f);

	PoseMatchResult result = CalculateBestPoseMatchPhase(pAnim, mi);
	float matchPhase = result.m_phase;
	if (matchPhase < 0.0f)
	{
		matchPhase = defaultPhase;
	}
	return ScriptValue(Limit01(matchPhase));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("start-for-streaming-anim", DcStartForStreamingAnim)
{
	float phase = 0.0f;
	 
	if (argc == 2)
	{
		const DC::AnimStateSnapshotInfo* pSnapshot = static_cast<const DC::AnimStateSnapshotInfo*>(argv[0].m_pointer);

		if (pSnapshot)
		{
			const ArtItemAnim* pArtItemAnim = AnimMasterTable::LookupAnim(SkeletonId(pSnapshot->m_translatedPhaseAnimSkelId), 
																		  pSnapshot->m_translatedPhaseAnimHierarchyId,
																		  pSnapshot->m_translatedPhaseAnimName).ToArtItem();

			float startPhase = AnimStreamGetStreamPhase(pArtItemAnim, pSnapshot->m_translatedPhaseAnimName); 

			phase = startPhase;
		}
	}

	return ScriptValue(phase);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("top-instance-fully-blended?", DcTopInstanceFullyBlendedP)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	NdGameObject* pGo = args.NextGameObject();
	StringId64 layerId = args.NextStringId();

	if (pGo)
	{
		if (AnimStateLayer* pLayer = pGo->GetAnimControl()->GetStateLayerById(layerId))
		{
			return ScriptValue(pLayer->CurrentStateInstance()->GetFade() == 1.0f);
		}
	}


	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
class EvaluateAnimSpeed : public EvaluateAnimTree<float>
{
public:
	EvaluateAnimSpeed(float phase, SkeletonId skelId, const DC::AnimInfoCollection* pInfoCollectionLs, AlignDistFunc pDistFunc = Dist)
		: m_phase(phase)
		, m_skelId(skelId)
		, m_pInfoCollection(pInfoCollectionLs)
		, m_pDistFunc(pDistFunc)
	{}

	virtual ~EvaluateAnimSpeed() override { }

protected:
	virtual float GetDefaultData() const override
	{
		return 0.0f;
	}

	virtual bool GetDataFromNode(const AnimSnapshotNode* pNode, float* pDataOut) override
	{
		*pDataOut = 0.0f;
		const AnimSnapshotNodeAnimation* pAnimNode = AnimSnapshotNodeAnimation::FromAnimNode(pNode);

		if (pAnimNode && pAnimNode->m_artItemAnimHandle.ToArtItem())
		{
			*pDataOut = GetAlignSpeedAtPhase(pAnimNode->m_artItemAnimHandle.ToArtItem(), m_skelId, m_phase, m_pDistFunc);
		}

		return true;
	}

	virtual float BlendData(const float& leftData, const float& rightData, const AnimSnapshotNodeBlend* pBlendNode, bool flipped) override
	{
		//See if we really want to blend
		if (pBlendNode->m_flags & DC::kAnimNodeBlendFlagEvaluateLeftNodeOnly)
		{
			return leftData;
		}

		float blendFactor = pBlendNode->m_blendFactor;

		//Evaluate the blend func because this is called before blends are set.
		if (pBlendNode->m_blendFunc)
		{
			DC::AnimStateSnapshotInfo snapshotInfo;

			ScriptValue argv[6];

			AnimSnapshotNodeBlend::GetAnimNodeBlendFuncArgs(argv,
															ARRAY_COUNT(argv),
															m_pInfoCollection,
															&snapshotInfo,
															0.0f,
															flipped,
															nullptr,
															nullptr,
															nullptr);

			const DC::ScriptLambda* pLambda = pBlendNode->m_blendFunc;
			blendFactor = ScriptManager::Eval(pLambda, SID("anim-node-blend-func"), ARRAY_COUNT(argv), argv).m_float;
		}

		return Lerp(leftData, rightData, blendFactor);
	}

private:
	float m_phase;
	SkeletonId m_skelId;
	const DC::AnimInfoCollection* m_pInfoCollection;
	AlignDistFunc* m_pDistFunc;
};

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-state-align-speed-from-tree", DCGetStateAlignSpeedFromTree)
{
	float result = 0.0f;
	if (argc >= 3)
	{
		const DC::AnimStateSnapshotInfo* pSnapShot =  static_cast<const DC::AnimStateSnapshotInfo*>(argv[0].m_pointer);
		const DC::AnimInfoCollection* pInfoCollection = static_cast<const DC::AnimInfoCollection*>(argv[1].m_pointer);
		float phase = argv[2].m_float;


		if (pSnapShot && pInfoCollection)
		{
			result = EvaluateAnimSpeed(phase, SkeletonId(pInfoCollection->m_actor->m_skelId), pInfoCollection).Evaluate(pSnapShot->m_stateSnapshot);
		}
	}
	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-state-align-speed-from-tree-xz", DCGetStateAlignSpeedFromTreeXZ)
{
	float result = 0.0f;
	if (argc >= 3)
	{
		const DC::AnimStateSnapshotInfo* pSnapShot =  static_cast<const DC::AnimStateSnapshotInfo*>(argv[0].m_pointer);
		const DC::AnimInfoCollection* pInfoCollection = static_cast<const DC::AnimInfoCollection*>(argv[1].m_pointer);
		float phase = argv[2].m_float;


		if (pSnapShot && pInfoCollection)
		{
			result = EvaluateAnimSpeed(phase, SkeletonId(pInfoCollection->m_actor->m_skelId), pInfoCollection, DistXz).Evaluate(pSnapShot->m_stateSnapshot);
		}
	}
	return ScriptValue(result);
}

/// -------------------------------------------------------------------------------------------- ///
/// Animation speed functions
/// -------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DoCalcNewPhaseFromSpeed(int argc, ScriptValue* argv, ScriptFnAddr __SCRIPT_FUNCTION__, AlignDistFunc distFunc)
{
	float result = 0.0f;
	if (argc >= 4)
	{
		const DC::AnimStateSnapshotInfo* pSnapshot = static_cast<const DC::AnimStateSnapshotInfo*>(argv[0].m_pointer);
		float oldPhase = argv[1].m_float;
		float speed = argv[2].m_float;
		float delta = argv[3].m_float;

		bool slowOnly = false;
		float minSpeedFrames = 0.0f;
		if (argc > 4)
			minSpeedFrames = argv[4].m_float;

		if (argc > 5)
			slowOnly = argv[5].m_boolean;

		if (pSnapshot)
		{
			const ArtItemAnim* pArtItemAnim = AnimMasterTable::LookupAnim(
				SkeletonId(pSnapshot->m_translatedPhaseAnimSkelId),
				pSnapshot->m_translatedPhaseAnimHierarchyId,
				pSnapshot->m_translatedPhaseAnimName).ToArtItem();

			result = ComputePhaseToTravelDistance(pArtItemAnim, oldPhase, speed * delta, FromUPS(minSpeedFrames, delta), delta, slowOnly, distFunc);
		}
	}
	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("calc-new-phase-from-speed", DcCalcNewPhaseFromSpeed)
{
	return DoCalcNewPhaseFromSpeed(argc, argv, __SCRIPT_FUNCTION__, Dist);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("calc-new-phase-from-speed-xz", DcCalcNewPhaseFromSpeedXZ)
{
	return DoCalcNewPhaseFromSpeed(argc, argv, __SCRIPT_FUNCTION__, DistXz);
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct FindRemappedAnimNodeParams
{
	StringId64 m_originalAnimId;
	const ArtItemAnim* m_pAnim;
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool ConstVisitAnimationNodeFunc(const AnimStateSnapshot* pSnapshot,
								 const AnimSnapshotNode* pNode,
								 const AnimSnapshotNodeBlend* pParentBlendNode,
								 float combinedBlend,
								 uintptr_t userData)
{
	if (const AnimSnapshotNodeAnimation* pAnimNodeAnim = AnimSnapshotNodeAnimation::FromAnimNode(pNode))
	{
		FindRemappedAnimNodeParams* pParams = (FindRemappedAnimNodeParams*)userData;
		if (pAnimNodeAnim->m_origAnimation == pParams->m_originalAnimId)
		{
			pParams->m_pAnim = pAnimNodeAnim->m_artItemAnimHandle.ToArtItem();
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("calc-new-phase-from-speed-xz-anim", DcCalcNewPhaseFromSpeedXZAnim)
{
	float result = 0.0f;
	if (argc >= 5)
	{
		const DC::AnimStateSnapshotInfo* pSnapshot = static_cast<const DC::AnimStateSnapshotInfo*>(argv[0].m_pointer);
		const StringId64 animId = argv[1].m_stringId;
		float oldPhase = argv[2].m_float;
		float speed = argv[3].m_float;
		float delta = argv[4].m_float;

		bool slowOnly = false;
		float minSpeedFrames = 0.0f;
		if (argc > 5)
			minSpeedFrames = argv[5].m_float;

		if (argc > 6)
			slowOnly = argv[6].m_boolean;

		if (pSnapshot)
		{
			FindRemappedAnimNodeParams params;
			params.m_originalAnimId = animId;
			params.m_pAnim = nullptr;
			pSnapshot->m_stateSnapshot->VisitNodesOfKind(SID("AnimSnapshotNodeAnimation"),
														 ConstVisitAnimationNodeFunc,
														 (uintptr_t)&params);

			if (const ArtItemAnim* pArtItemAnim = params.m_pAnim)
			{
				result = ComputePhaseToTravelDistance(pArtItemAnim,
													  oldPhase,
													  speed * delta,
													  FromUPS(minSpeedFrames, delta),
													  delta,
													  slowOnly,
													  DistXz);
			}
		}
	}
	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("minmax", DcMinmax)
{
	if (argc == 3)
	{
		float v0 = argv[0].m_float;
		float v1 = argv[1].m_float;
		float v2 = argv[2].m_float;

		return ScriptValue(MinMax(v0, v1, v2));
	}
	return ScriptValue(0.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("angle-diff", DcAngleDiff)
{
	float result  = 0.0f;
	if (argc == 2)
	{
		Angle a0(argv[0].m_float);
		Angle a1(argv[1].m_float);
		result = (a0-a1).ToDegrees();
	}
	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 (define-c-function read-channel-scale-z ((channel-id symbol) 
										  (snapshot anim-state-snapshot-info)
										  (info anim-info-collection) 
										  (state-phase float) 
										  (flipped boolean))
	float)
 */
SCRIPT_FUNC("read-channel-scale-z", DcReadChannelScaleZ)
{
	if (argc < 5)
		return ScriptValue(0);

	const StringId64 channelId = argv[0].m_stringId;
	const DC::AnimStateSnapshotInfo* pSnapshot = static_cast<const DC::AnimStateSnapshotInfo*>(argv[1].m_pointer);
	const DC::AnimInfoCollection* pInfoCollection = static_cast<const DC::AnimInfoCollection*>(argv[2].m_pointer);
	const float statePhase = argv[3].m_float;
	const bool flipped = argv[4].m_boolean;

	ndanim::JointParams jp;

	if (pSnapshot->m_stateSnapshot->m_rootNodeIndex == SnapshotNodeHeap::kOutOfMemoryIndex)
	{
		// we might be in the middle of Init() ...
		return ScriptValue(0.0f);
	}

	SnapshotEvaluateParams params;
	params.m_statePhase = params.m_statePhasePre = statePhase;
	params.m_flipped = flipped;
	params.m_wantRawScale = true;
	params.m_blendChannels = pInfoCollection && pInfoCollection->m_actor
							 && (pInfoCollection->m_actor->m_blendChannels != 0);

	const U32F evaluatedChannels = pSnapshot->m_stateSnapshot->Evaluate(&channelId, 1, &jp, params);

	float val = 0.0f;

	if (evaluatedChannels == 0x1)
	{
		val = jp.m_scale.Z();

		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_debugReadChannelScaleZ))
		{
			MsgCon("read-channel-scale-z: [%s @ %0.3f] %f\n", 
				   pSnapshot->m_stateSnapshot->m_animState.m_name.m_string.GetString(),
				   statePhase,
				   val);
		}
	}

	return ScriptValue(val);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 (define-c-function read-float-channel ((channel-id symbol) 
										(snapshot anim-state-snapshot-info)
										(info anim-info-collection) 
										(state-phase float) 
										(flipped boolean)
										(default float))
	float)
 */

SCRIPT_FUNC("read-float-channel", DcReadFloatChannel)
{
	if (argc < 6)
		return ScriptValue(0);

	const StringId64 channelId = argv[0].m_stringId;
	const DC::AnimStateSnapshotInfo* pSnapshot	  = static_cast<const DC::AnimStateSnapshotInfo*>(argv[1].m_pointer);
	const DC::AnimInfoCollection* pInfoCollection = static_cast<const DC::AnimInfoCollection*>(argv[2].m_pointer);
	const float statePhase = argv[3].m_float;
	const bool flipped	   = argv[4].m_boolean;
	const float defaultVal = argv[5].m_boolean;

	const AnimStateSnapshot* pStateSnapshot = pSnapshot ? pSnapshot->m_stateSnapshot : nullptr;
	if (!pStateSnapshot)
	{
		return ScriptValue(defaultVal);
	}

	if (pSnapshot->m_stateSnapshot->m_rootNodeIndex == SnapshotNodeHeap::kOutOfMemoryIndex)
	{
		// we might be in the middle of Init() ...
		return ScriptValue(defaultVal);
	}

	float floatVal = defaultVal;

	SnapshotEvaluateParams params;
	params.m_statePhase = params.m_statePhasePre = statePhase;
	params.m_flipped = flipped;
	params.m_blendChannels = pInfoCollection && pInfoCollection->m_actor
							 && (pInfoCollection->m_actor->m_blendChannels != 0);

	const U32F mask = pStateSnapshot->EvaluateFloat(&channelId, 1, &floatVal, params);

	if (mask != 0x1)
	{
		floatVal = defaultVal;
	}

	return ScriptValue(floatVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("lookup-resolved-anim-name", DcLookupResolvedAnimName)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	const NdGameObject* pGo = args.NextGameObject();
	const StringId64 inputNameId = args.NextStringId();

	const AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;

	StringId64 resolvedNameId = inputNameId;

	if (pAnimControl)
	{
		resolvedNameId = pAnimControl->LookupAnimId(inputNameId);
	}

	return ScriptValue(resolvedNameId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("read-layer-float-channel", DcReadLayerFloatChannel)
{
	SCRIPT_ARG_ITERATOR(args, 4);

	const NdGameObject* pGo = args.NextGameObject();
	const StringId64 layerId = args.NextStringId();
	const StringId64 channelId = args.NextStringId();
	const float defaultValue = args.NextFloat();

	const AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;
	
	const AnimLayer* pLayer = pAnimControl ? pAnimControl->GetLayerById(layerId) : nullptr;

	if (!pLayer)
	{
		args.MsgScriptError("Object '%s' has no anim layer named '%s'\n",
							pGo ? pGo->GetName() : nullptr,
							DevKitOnly_StringIdToString(layerId));
		return ScriptValue(defaultValue);
	}

	float fVal = defaultValue;

	switch (pLayer->GetType())
	{
	case kAnimLayerTypeState:
		{
			const AnimStateLayer* pStateLayer = (const AnimStateLayer*)pLayer;
			fVal = ComputeFloatChannelContribution(pStateLayer, channelId, defaultValue, kUseAnimFade);
		}
		break;

	case kAnimLayerTypeSimple:
		{
			const AnimSimpleLayer* pSimpleLayer = (const AnimSimpleLayer*)pLayer;

			if (const AnimSimpleInstance* pInst = pSimpleLayer->CurrentInstance())
			{
				EvaluateChannelParams params;

				if (0 == pInst->EvaluateFloatChannels(&channelId, 1, &fVal, params))
				{
					fVal = defaultValue;
				}
			}
		}
		break;
	}

	return ScriptValue(fVal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct FindAnimNodeData
{
	StringId64 m_animId = INVALID_STRING_ID_64;
	const AnimSnapshotNodeAnimation* m_pFoundNode = nullptr;
	float m_foundPhase = -1.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool VisitAnimNode(const AnimStateSnapshot* pSnapshot,
						  const AnimSnapshotNode* pNode,
						  const AnimSnapshotNodeBlend* pParentBlendNode,
						  float combinedBlend,
						  uintptr_t userData)
{
	FindAnimNodeData& data = *(FindAnimNodeData*)userData;

	const AnimSnapshotNodeAnimation* pAnimNode = (const AnimSnapshotNodeAnimation*)pNode;

	if (pAnimNode && (pAnimNode->GetAnimNameId() == data.m_animId))
	{
		data.m_pFoundNode = pAnimNode;
		data.m_foundPhase = pAnimNode->m_phase;
		return false; // all done
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool WalkFindAnimNode(const AnimStateInstance* pInstance, const AnimStateLayer* pStateLayer, uintptr_t userData)
{
	FindAnimNodeData& data = *(FindAnimNodeData*)userData;
	const AnimStateSnapshot& snapshot = pInstance->GetAnimStateSnapshot();
	snapshot.VisitNodesOfKind(SID("AnimSnapshotNodeAnimation"), VisitAnimNode, userData);

	return data.m_pFoundNode == nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function get-anim-node-phase ([entity-name symbol] [anim-name symbol] [layer-name symbol (:default 'base)])
	float)
*/
SCRIPT_FUNC("get-anim-node-phase", DcGetAnimNodePhase)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	const NdGameObject* pGo = args.NextGameObject();
	const StringId64 animId = args.NextStringId();
	const StringId64 layerId = args.NextStringId();

	if (!pGo)
	{
		return ScriptValue(-1.0f);
	}

	const AnimControl* pAnimControl = pGo->GetAnimControl();
	const AnimLayer* pLayer = pAnimControl ? pAnimControl->GetLayerById(layerId) : nullptr;

	if (!pLayer)
	{
		args.MsgScriptError("get-anim-node-phase: Layer '%s' not found!\n", DevKitOnly_StringIdToString(layerId));
		return ScriptValue(-1.0f);
	}

	float phase = -1.0f;

	const AnimLayerType layerType = pLayer->GetType();
	switch (layerType)
	{
	case kAnimLayerTypeState:
		{
			const AnimStateLayer* pStateLayer = (const AnimStateLayer*)pLayer;
			FindAnimNodeData data;
			data.m_animId = animId;
			pStateLayer->WalkInstancesNewToOld(WalkFindAnimNode, (uintptr_t)&data);

			if (data.m_pFoundNode)
			{
				phase = data.m_foundPhase;
			}
		}
		break;

	case kAnimLayerTypeSimple:
		{
			const AnimSimpleLayer* pSimpleLayer = (const AnimSimpleLayer*)pLayer;
			const I32F numInstances = pSimpleLayer->GetNumInstances();
			for (I32F iInstance = 0; iInstance < numInstances; ++iInstance)
			{
				const AnimSimpleInstance* pInstance = pSimpleLayer->GetInstance(iInstance);
				if (!pInstance)
					continue;

				if (pInstance->GetAnimId() == animId)
				{
					phase = pInstance->GetPhase();
					break;
				}
			}
		}
		break;

	default:
		args.MsgScriptError("get-anim-node-phase: Layer '%s' is unsupported type %d\n",
							DevKitOnly_StringIdToString(layerId),
							layerType);
		break;
	}

	return ScriptValue(phase);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function object-has-anim? ([entity-name symbol] [anim-name symbol])
	boolean)
*/
SCRIPT_FUNC("object-has-anim?", DcObjectHasAnimP)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	const NdGameObject* pGo = args.NextGameObject();
	const StringId64 animId = args.NextStringId();

	const AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;

	const bool hasAnim = pAnimControl ? !pAnimControl->LookupAnim(animId).IsNull() : false;

	return ScriptValue(hasAnim);
}
