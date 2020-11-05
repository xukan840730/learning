/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-snapshot-node-blend.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/settings/priority.h"
#include "ndlib/settings/settings.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/anim/gesture-node.h"
#include "gamelib/anim/nd-gesture-util.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/scriptx/h/nd-gesture-script-defines.h"
#include "gamelib/state-script/ss-context.h"
#include "gamelib/state-script/ss-track-group.h"
#include "gamelib/state-script/ss-track.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkNdGestureScriptFuncs()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("gesture-id?", DcGestureIdP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	const StringId64 id = args.NextStringId();

	const DC::GestureDef* pGesture = Gesture::LookupGesture(id);
	bool validGesture = (pGesture && ScriptManager::TypeOf(pGesture) == SID("gesture-def"));

	return ScriptValue(validGesture);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("alloc-gesture-play-params", DcAllocGesturePlayParams)
{
	DC::GesturePlayParams* pNewParams = NDI_NEW(kAllocSingleGameFrame, kAlign16) DC::GesturePlayParams;

	memset(pNewParams, 0, sizeof(DC::GesturePlayParams));

	return ScriptValue((void*)pNewParams);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ReportGestureError(const Gesture::Err& err, const StringId64 gestureId)
{
	STRIP_IN_FINAL_BUILD;

	if (U32(err.m_severity) < g_animOptions.m_gestures.m_minErrReportSeverity)
	{
		return;
	}
	
	StringBuilder<256> errString("gesture '%s' failed (%s%s%s)\n",
								 DevKitOnly_StringIdToString(gestureId),
								 DevKitOnly_StringIdToString(err.m_errId),
								 err.m_pLockoutReason ? ", " : "",
								 err.m_pLockoutReason ? err.m_pLockoutReason : "");

	const bool dupToMsgCon = (err.m_severity == Gesture::Err::Severity::kHigh);

	MsgScript("%s", errString.c_str());

	if (dupToMsgCon)
	{
		/* this will actually crash the script */

		MsgConScriptError("%s", errString.c_str());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("gesture_", DcGestureNew)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	Process* pGesturer		   = args.NextProcess();
	const StringId64 gestureId = args.NextStringId();
	const DC::GesturePlayParams* pPlayParams = args.NextPointer<DC::GesturePlayParams>();

	if (pGesturer && (gestureId != INVALID_STRING_ID_64) && pPlayParams)
	{
		const Gesture::Err paramsValidation = Gesture::ValidateParams(*pPlayParams);
		if (!paramsValidation.Success())
		{
			MsgScript("gesture '%s' failed due to invalid parameters: %s\n",
					  DevKitOnly_StringIdToString(gestureId),
					  DevKitOnly_StringIdToString(paramsValidation.m_errId));

			return ScriptValue(paramsValidation.m_errId);
		}

		if (pPlayParams->m_priorityValid)
		{
			Gesture::Config* pConfig = Gesture::Config::Get();
			const I32 pri = pPlayParams->m_priority;

			if (!pConfig->IsScriptPriority(pri))
			{
				MsgConScriptError("Script may not request a gesture on priority %d as that level is reserved for the Game\n",
								  pri);

				return ScriptValue(SID("scripted-gesture-requested-programmer-priority"));
			}
		}

		if (pGesturer->IsKindOf(g_type_NavCharacter))
		{
			AiLogScript(static_cast<NavCharacter*>(pGesturer), "gesture %s", DevKitOnly_StringIdToString(gestureId));
		}

		const bool wait = pPlayParams->m_waitValid && pPlayParams->m_wait;

		Gesture::Err playResult(SID("err-struct-not-filled-in"));

		Gesture::PlayArgs playArgs;
		playArgs.m_dcParams = *pPlayParams;

		if (pPlayParams->m_flipValid)
		{
			playArgs.m_flip = pPlayParams->m_flip;
		}

		playArgs.m_pResultPlace = &playResult;

		if (pPlayParams->m_layerIndexValid)
		{
			playArgs.m_gestureLayer = pPlayParams->m_layerIndex;
		}

		if (pPlayParams->m_layerPriorityValid)
		{
			playArgs.m_layerPriority = pPlayParams->m_layerPriority;
		}

		// If INVALID_STRING_ID_64 is passed as the :to arg, pretend no :to arg was supplied.
		if (playArgs.m_dcParams.m_toValid && playArgs.m_dcParams.m_to == INVALID_STRING_ID_64)
		{
			playArgs.m_dcParams.m_toValid = false;
		}

		if (wait)
		{
			playArgs.m_pGroupInst = GetJobSsContext().GetGroupInstance();
			playArgs.m_trackIndex = GetJobSsContext().GetTrackInstance()->GetTrackIndex();
		}

		SendEvent(SID("play-gesture"), pGesturer, gestureId, &playArgs);

		if (!playResult.Success())
		{
			ReportGestureError(playResult, gestureId);
		}

		if (playResult.Success() && wait)
		{
			GetJobSsContext().GetGroupInstance()->WaitForTrackDone("wait-gesture");
			ScriptContinuation::Suspend(argv);
		}

		return ScriptValue(playResult.m_errId);
	}

	return ScriptValue(SID("dc-script-argument-not-specified"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("gesture-new", DcTemporaryGestureDispatch)
{
	return DcGestureNew(argc, argv, __SCRIPT_FUNCTION__);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("gesture-get-phase", DcGestureGetPhase)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	const NdGameObject* pGo = args.NextGameObject();
	const StringId64 gestureId = args.NextStringId();
	const Gesture::LayerIndex layerIndex = args.NextI32();

	const IGestureController* pGestureController = pGo ? pGo->GetGestureController() : nullptr;
	
	if (!pGestureController)
	{
		return ScriptValue(-1.0f);
	}

	const float phase = pGestureController->GetGesturePhase(gestureId, layerIndex);

	return ScriptValue(phase);
}
