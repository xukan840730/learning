/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 *
 */

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"

#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/msg-mem.h"
#include "corelib/util/user.h"

#include "ndlib/netbridge/mail.h"
#include "ndlib/process/process-error.h"
#include "ndlib/render/display.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/agent/simple-nav-character.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/region/region.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/scriptx/h/nd-state-script-defines.h"
#include "gamelib/state-script/ss-context.h"
#include "gamelib/state-script/ss-file.h"
#include "gamelib/state-script/ss-instance.h"
#include "gamelib/state-script/ss-track.h"
#include "gamelib/state-script/state-script.h"
#include "gamelib/stats/event-log.h"
#include "gamelib/util/exception-handling.h"
#include "gamelib/debug/ai-msg-log.h"

#include <stdarg.h>

class Level;

/// --------------------------------------------------------------------------------------------------------------- ///
static inline TimeFrame Now()
{
	return GetProcessClock()->GetCurTime();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiError(const char* strMsg, ...)
{
	char strBuffer[512];
	va_list args;
	va_start(args, strMsg);
	vsnprintf(strBuffer, 512, strMsg, args);
	va_end(args);
	MsgErr("AI: ");
	MsgErr(strBuffer);
	MsgConErr(strBuffer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiError(const EntitySpawner& spawner, const char* strMsg, ...)
{
	char strBuffer[512];
	va_list args;
	va_start(args, strMsg);
	vsnprintf(strBuffer, 512, strMsg, args);
	va_end(args);
	MsgErr("AI: ");
	MsgErr(strBuffer);
	SpawnLevelErrorProcess(spawner.GetWorldSpaceLocator().Pos(), spawner.GetLevel(), strBuffer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiError(Point_arg posWs, const Level* pLevel, const char* strMsg, ...)
{
	char strBuffer[512];
	va_list args;
	va_start(args, strMsg);
	vsnprintf(strBuffer, 512, strMsg, args);
	va_end(args);
	MsgErr("AI: ");
	MsgErr(strBuffer);
	SpawnLevelErrorProcess(posWs, pLevel, strBuffer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiError(Point_arg posWs, F32 timeOut, const char* strMsg, ...)
{
	char strBuffer[512];
	va_list args;
	va_start(args, strMsg);
	vsnprintf(strBuffer, 512, strMsg, args);
	va_end(args);
	MsgErr("AI: ");
	MsgErr(strBuffer);
	g_prim.Draw(DebugString(posWs, strBuffer, kColorRed, 0.5f), Seconds(timeOut));
}

#ifndef FINAL_BUILD
/// --------------------------------------------------------------------------------------------------------------- ///
AiLogNugget::AiLogNugget() : m_bValid(false), m_szFile("<invalid>"), m_szFunc("<invalid>"), m_line(0)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiLogNugget::AiLogNugget(const char* szFile, U32F line, const char* szFunc)
	: m_bValid(true), m_szFile(szFile), m_szFunc(szFunc), m_line(line)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ShouldLog()
{
	if (Memory::IsDebugMemoryAvailable())
	{
		if (g_navCharOptions.m_logMasterEnable)
		{
			return true;
			/*U32 chanMask = g_navCharOptions.m_logChannelMask;
			if (chanMask & (1U << chan))
			{
				return true;
			}*/
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ShouldPrint(NpcLogChannel chan)
{
	if (g_navCharOptions.m_logMasterEnable)
	{

		U32 chanMask = g_navCharOptions.m_logChannelMask;
		if (chanMask & (1U << chan))
		{
			return true;
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Log(const NavCharacter* pNavChar, NpcLogChannel chan, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldLog())
		return;

	LogHdr(pNavChar, chan);

	va_list vargs;
	va_start(vargs, str);
	if (g_navCharOptions.m_npcLogToTty)
	{
		if (ShouldPrint(chan))
			GetMsgOutput(kMsgUser5)->Vprintf(str, vargs);
	}

	if (DoutMemChannels* pLog = pNavChar->GetChannelDebugLog())
	{
		pLog->Vprintf(chan, Now(), str, vargs);
	}

	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LogHdr(const NavCharacter* pNavChar, NpcLogChannel chan)
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldLog())
		return;

	TimeFrame curTime = GetContextProcessCurTime();
	if (g_navCharOptions.m_npcLogToTty && ShouldPrint(chan))
	{
		MsgAi("Npc(pid-%d) (t=%-6.2f) [%3s] %-20s ",
			  U32(pNavChar->GetProcessId()),
			  ToSeconds(curTime),
			  AI::GetNpcLogChannelName(chan),
			  pNavChar->GetName());
	}
	//replaced in favor of storing the header data in a msgNugget to save memory
	/*if (DoutMemChannels* pLog = pNavChar->GetChannelDebugLog())
	{
		// need to keep this header short because console text doesn't wrap and there isn't any horizontal scrolling
		// Making the font smaller is the only way of viewing text that runs off the right side of the screen.
		pLog->Printf(chan, curTime, "%-6.2f [%3s]", ToSeconds(curTime), GetNpcLogChannelName(chan));
	}*/
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LogMsg(const NavCharacter* pNavChar, NpcLogChannel chan, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldLog())
		return;

	va_list vargs;
	va_start(vargs, str);
	if (g_navCharOptions.m_npcLogToTty)
	{
		if (ShouldPrint(chan))
			GetMsgOutput(kMsgUser5)->Vprintf(str, vargs);
	}
	if (DoutMemChannels* pLog = pNavChar->GetChannelDebugLog())
	{
		pLog->Vprintf(chan, Now(), str, vargs);
	}

	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LogMsgAppend(const NavCharacter* pNavChar, NpcLogChannel chan, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldLog())
		return;

	va_list vargs;
	va_start(vargs, str);
	if (g_navCharOptions.m_npcLogToTty)
	{
		if (ShouldPrint(chan))
			GetMsgOutput(kMsgUser5)->Vprintf(str, vargs);
	}
	if (DoutMemChannels* pLog = pNavChar->GetChannelDebugLog())
	{
		pLog->Vappendf(str, vargs);
	}

	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LogPoint(const NavCharacter* pNavChar, NpcLogChannel chan, Point_arg pt)
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldLog())
		return;

	StringBuilder<256> desc;
	desc.append_format("(%5.3f, %5.3f, %5.3f)", float(pt.X()), float(pt.Y()), float(pt.Z()));

	if (g_navCharOptions.m_npcLogToTty)
	{
		if (ShouldPrint(chan))
			MsgAi(desc.c_str());
	}

	if (DoutMemChannels* pLog = pNavChar->GetChannelDebugLog())
	{
		if (false) // if (chan != pLog->GetLastChannel())
		{
			pLog->Printf(chan, Now(), desc.c_str());
		}
		else
		{
			pLog->Appendf(desc.c_str());
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LogVector(const NavCharacter* pNavChar, NpcLogChannel chan, Vector_arg v)
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldLog())
		return;

	StringBuilder<256> desc;
	desc.append_format("(%5.3f, %5.3f, %5.3f)", float(v.X()), float(v.Y()), float(v.Z()));

	if (g_navCharOptions.m_npcLogToTty)
	{
		if (ShouldPrint(chan))
			MsgAi(desc.c_str());
	}

	if (DoutMemChannels* pLog = pNavChar->GetChannelDebugLog())
	{
		if (false) // if (chan != pLog->GetLastChannel())
		{
			pLog->Printf(chan, Now(), desc.c_str());
		}
		else
		{
			pLog->Appendf(desc.c_str());
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LogLocator(const NavCharacter* pNavChar, NpcLogChannel chan, const Locator& loc)
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldLog())
		return;

	const Point pt = loc.Pos();
	const Quat rt  = loc.Rot();

	StringBuilder<256> desc;
	desc.append_format("pos [%5.3f, %5.3f, %5.3f] rot [%5.3f, %5.3f, %5.3f, %5.3f]",
					   float(pt.X()),
					   float(pt.Y()),
					   float(pt.Z()),
					   float(rt.X()),
					   float(rt.Y()),
					   float(rt.Z()),
					   float(rt.W()));

	if (g_navCharOptions.m_npcLogToTty)
	{
		if (ShouldPrint(chan))
			MsgAi(desc.c_str());
	}

	if (DoutMemChannels* pLog = pNavChar->GetChannelDebugLog())
	{
		if (false) // if (chan != pLog->GetLastChannel())
		{
			pLog->Printf(chan, Now(), desc.c_str());
		}
		else
		{
			pLog->Appendf(desc.c_str());
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LogNugget(const NavCharacter* pNavChar, NpcLogChannel chan, const AiLogNugget& nugget)
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldLog())
		return;

	StringBuilder<256> desc;
	desc.append_format("[%s:%d (%s)]", nugget.m_szFile, nugget.m_line, nugget.m_szFunc);

	if (g_navCharOptions.m_npcLogToTty)
	{
		if (ShouldPrint(chan))
			MsgAi(desc.c_str());
	}

	if (DoutMemChannels* pLog = pNavChar->GetChannelDebugLog())
	{
		if (false) // if (chan != pLog->GetLastChannel())
		{
			pLog->Printf(chan, Now(), desc.c_str());
		}
		else
		{
			pLog->Appendf(desc.c_str());
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LogActionPack(const NavCharacter* pNavChar, NpcLogChannel chan, const ActionPack* pAp)
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldLog())
		return;

	ASSERT(pNavChar);
	StringBuilder<256> desc;
	if (pAp)
	{
		const Point ptWs = pAp->GetRegistrationPointWs();
		const Point ptPs = pNavChar->GetParentSpace().UntransformPoint(ptWs);
		desc.append_format("%s (%5.3f, %5.3f, %5.3f)", pAp->GetName(), float(ptPs.X()), float(ptPs.Y()), float(ptPs.Z()));
	}
	else
	{
		desc.append_format("null");
	}
	if (g_navCharOptions.m_npcLogToTty)
	{
		if (ShouldPrint(chan))
			MsgAi(desc.c_str());
	}
	if (DoutMemChannels* pLog = pNavChar->GetChannelDebugLog())
	{
		if (false) // if (chan != pLog->GetLastChannel())
		{
			pLog->Printf(chan, Now(), desc.c_str());
		}
		else
		{
			pLog->Appendf(desc.c_str());
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LogNavLocation(const NavCharacter* pNavChar, NpcLogChannel chan, const NavLocation& navLoc)
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldLog())
		return;

	if (g_navCharOptions.m_npcLogToTty)
	{
		if (ShouldPrint(chan))
			navLoc.DebugPrint(GetMsgOutput(kMsgAi));
	}

	if (DoutMemChannels* pLog = pNavChar->GetChannelDebugLog())
	{
		navLoc.DebugPrint(pLog);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void InternalLog(const NavCharacter* pNavChar, NpcLogChannel chan, const char* str, va_list vargs)
{
	STRIP_IN_FINAL_BUILD;

	if (!ShouldLog())
		return;

	if (!pNavChar)
		return;

	// TODO I have no idea why we log this, or where it is debugged
	{
		char locationString[1024];
		float x, y;
		const bool bBehind = !GetRenderCameraPrev(0).WorldToScreen(x,
																   y,
																   pNavChar->GetTranslation() + Vector(0, 1.0f, 0),
																   true);

		if (x < 0 || y < 0 || x > kVirtualScreenWidth || y > kVirtualScreenHeight)
		{
			sprintf(locationString, "offscreen");
		}
		else
		{
			sprintf(locationString, "%d %d", (int)x, (int)y);
		}

		char buf[8192];
		sprintf(buf, "|%s@%s| [%3s] %s", pNavChar->GetName(), locationString, AI::GetNpcLogChannelName(chan), str);
		char newBuf[8192];
		vsprintf(newBuf, buf, vargs);
		g_eventLog.LogTTY(newBuf);
	}

	LogHdr(pNavChar, chan);

	if (g_navCharOptions.m_npcLogToTty && ShouldPrint(chan))
	{
		GetMsgOutput(kMsgAi)->Vprintf(str, vargs);
	}

	if (DoutMemChannels* pLog = pNavChar->GetChannelDebugLog())
	{
		pLog->Vprintf(chan, Now(), str, vargs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Log(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelGeneral, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLog(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelGeneral, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogNav(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelNav, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogNavDetails(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelNavDetails, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogSkill(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelSkill, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogSkillDetails(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelSkillDetails, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogBehaviorInternal(const NavCharacter* pNavChar, const char* str, va_list vargs)
{
	STRIP_IN_FINAL_BUILD;
	InternalLog(pNavChar, kNpcLogChannelBehavior, str, vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogBehaviorInternal(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	// TODO DEPRECATE THIS ONE, ONLY THE VARGS VERSION SHOULD EXIST. FUCKING BIG4X.
	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelBehavior, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogBehaviorDetails(const NavCharacter* pNavChar, const char* str, va_list vargs)
{
	STRIP_IN_FINAL_BUILD;
	InternalLog(pNavChar, kNpcLogChannelBehaviorDetails, str, vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogBehaviorDetails(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	// TODO DEPRECATE THIS ONE, ONLY THE VARGS VERSION SHOULD EXIST. FUCKING BIG4X.
	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelBehaviorDetails, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogAnim(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelAnim, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogAnim(const NavCharacter* pNavChar, const AiLogNugget& nugget, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	// LogNugget(pNavChar, kNpcLogChannelAnim, nugget);

	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelAnim, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogAnimDetails(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelAnimDetails, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogCombat(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelCombat, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogCombatDetails(const NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	va_list vargs;
	va_start(vargs, str);
	InternalLog(pNavChar, kNpcLogChannelCombatDetails, str, vargs);
	va_end(vargs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLogScriptLocation(const SsInstance* pScriptInst, const SsTrackInstance* pTrackInst, IStringBuilder* pStringOut)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (!pScriptInst || !pTrackInst)
		return false;

	const StateScript* pScript = pScriptInst->GetScript();
	if (!pScript)
		return false;

	const SsTrack* pTrack = pTrackInst->GetTrack();
	if (!pTrack)
		return false;

	I32F iLambda = pTrackInst->GetCurrentLambdaIndex(true);
	if (iLambda < 0 || iLambda >= pTrack->m_lambdaCount)
		return false;

	const DC::SsLambda* pLambda = &(pTrack->m_lambdas[iLambda]);
	if (!pLambda)
		return false;

	const Process* pProcess = pScriptInst->ToProcess();
	const EntitySpawner* pSpawner = pScriptInst->GetSpawner();

	I32F iLambdaLine = pLambda->m_line;
	const char* procName = pSpawner ? pSpawner->Name() : (pProcess ? pProcess->GetName() : nullptr);
	
	pStringOut->append_format("%s:%d", SsSourceFile::ShortenPath(pScript->m_file), iLambdaLine);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLogScriptLocation(const SsAction& ssAction, IStringBuilder* pStringOut)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	Process* pObserver = ssAction.GetObserverHandle().ToMutableProcess();
	SsTrackGroupProcess* pTgProc = SsTrackGroupProcess::FromProcess(pObserver);
	if (!pTgProc)
		return false;

	SsTrackGroupInstance& tgInst = pTgProc->GetTrackGroupInstance();
	const SsInstance* pScriptInst	   = tgInst.GetScriptInstance();
	const SsTrackInstance* pTrackInst  = tgInst.GetTrackByIndex(ssAction.GetTrackIndex());

	return AiLogScriptLocation(pScriptInst, pTrackInst, pStringOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLogScriptLocation(IStringBuilder* pStringOut)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (!HasJobSsContext())
		return false;

	const SsContext& jobContext		  = GetJobSsContext();
	const SsInstance* pScriptInst	  = jobContext.GetScriptInstance();
	const SsTrackInstance* pTrackInst = jobContext.GetTrackInstance();

	return AiLogScriptLocation(pScriptInst, pTrackInst, pStringOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiLogScriptSource(IStringBuilder* pStringOut)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (!HasJobSsContext())
		return false;

	const SsContext& jobContext = GetJobSsContext();
	const SsInstance* pScriptInst = jobContext.GetScriptInstance();
	const SsTrackInstance* pTrackInst = jobContext.GetTrackInstance();

	AiLogScriptSource(pScriptInst, pStringOut);

	return true;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogScriptSource(const SsInstance* pScriptInst, IStringBuilder* pStringOut)
{
	STRIP_IN_FINAL_BUILD;

	if (pScriptInst)
	{
		const Process* pProcess = pScriptInst->ToProcess();
		const EntitySpawner* pSpawner = pScriptInst->GetSpawner();

		const char* procName = pSpawner ? pSpawner->Name() : (pProcess ? pProcess->GetName() : nullptr);

		if (const Region* pRegion = pScriptInst->GetRegion())
		{
			pStringOut->append_format("%s: ", pRegion->m_name);
		}
		else
		{
			pStringOut->append_format("%s: ", procName);
		}
	}
	else
	{
		pStringOut->append_format("<UNKNOWN>: ");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogScriptSource(const SsAction& ssAction, IStringBuilder* pStringOut)
{
	STRIP_IN_FINAL_BUILD;
	const SsTrackGroupProcess* pTgProc = SsTrackGroupProcess::FromProcess(ssAction.GetObserverProcess());
	if (!pTgProc)
		return;

	const SsTrackGroupInstance& tgInst = pTgProc->GetTrackGroupInstance();
	const SsInstance* pScriptInst	   = tgInst.GetScriptInstance();

	AiLogScriptSource(pScriptInst, pStringOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogScriptInternal(const char* str,
						 va_list vargs,
						 DoutMemChannels* pScriptLog,
						 AiScriptLogger* pScriptLogger,
						 TimeFrame curTime)
{
	NdScriptArgIterator::SuppressNpcScriptLog();

	{
		StringBuilder<256> miniDesc;
		miniDesc.append_vargs(str, vargs);
		pScriptLogger->AddScriptLog(miniDesc.c_str(), curTime);
	}

	{
		StringBuilder<1024> desc;

		const SsInstance* pScriptInst = HasJobSsContext() ? GetJobSsContext().GetScriptInstance() : nullptr;

		AiLogScriptSource(pScriptInst, &desc);

		desc.append_vargs(str, vargs);
		desc.append_format(" - (");

		if (!AiLogScriptLocation(&desc))
		{
			desc.append_format("<SOURCE UNKNOWN>");
		}

		desc.append_format(")\n");

		pScriptLog->Printf(0, curTime, desc.c_str());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogScript(NavCharacter* pNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	if (!pNavChar)
		return;

	DoutMemChannels* pScriptLog = pNavChar->GetScriptDebugLog();
	if (!pScriptLog)
		return;

	// TODO: make these log channels instead
	const bool hasSsContext = HasJobSsContext();
	const SsInstance* pScriptInstance = hasSsContext ? GetJobSsContext().GetScriptInstance() : nullptr;
	bool scriptSuppressed = false;
	if (pScriptInstance && pScriptInstance->GetVarBoolean(SID("suppress-script-log?"), scriptSuppressed)
		&& scriptSuppressed)
	{
		return;
	}

	AiScriptLogger* pScriptLogger = pNavChar->GetScriptLogger();
	TimeFrame curTime = pNavChar->GetCurTime();

	va_list vargs;

	va_start(vargs, str);
	AiLogScriptInternal(str, vargs, pScriptLog, pScriptLogger, curTime);
	va_end(vargs);
}


/// --------------------------------------------------------------------------------------------------------------- ///
void AiLogScript(SimpleNavCharacter* pSimpleNavChar, const char* str, ...)
{
	STRIP_IN_FINAL_BUILD;

	if (!pSimpleNavChar)
		return;

	DoutMemChannels* pScriptLog = pSimpleNavChar->GetScriptDebugLog();
	if (!pScriptLog)
		return;

	// TODO: make these log channels instead
	const bool hasSsContext = HasJobSsContext();
	const SsInstance* pScriptInstance = hasSsContext ? GetJobSsContext().GetScriptInstance() : nullptr;
	bool scriptSuppressed = false;
	if (pScriptInstance && pScriptInstance->GetVarBoolean(SID("suppress-script-log?"), scriptSuppressed)
		&& scriptSuppressed)
	{
		return;
	}

	AiScriptLogger* pScriptLogger = pSimpleNavChar->GetScriptLogger();
	TimeFrame curTime = pSimpleNavChar->GetCurTime();

	va_list vargs;

	va_start(vargs, str);
	AiLogScriptInternal(str, vargs, pScriptLog, pScriptLogger, curTime);
	va_end(vargs);
}

#endif

/// --------------------------------------------------------------------------------------------------------------- ///
void MsgAiPrintPoint(Point_arg pt)
{
	MsgAi("(%5.3f, %5.3f, %5.3f)", float(pt.X()), float(pt.Y()), float(pt.Z()));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MsgAiPrintActionPack(const ActionPack* pAp)
{
	const char* str = "null";
	if (pAp)
	{
		str = pAp->GetName();
	}
	MsgAi(str);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Color AI::IndexToColor(I32F index, float alpha)
{
	// its good to have a prime number of colors in this table to reduce the
	// odds of two or more NPCs being mapped to the same color
	static Color s_colorTable[] =
	{
		Color(0.8f, 0.6f, 0.8f), // light pink
		Color(0.0f, 1.0f, 0.3f), // aqua
		Color(1.0f, 0.5f, 0.0f), // orange
		Color(1.0f, 0.3f, 0.3f), // peach
		Color(0.6f, 0.6f, 1.0f), // pastel blue
		Color(0.0f, 1.0f, 0.0f), // pure green
		Color(0.0f, 1.0f, 1.0f), // pure cyan
		Color(1.0f, 0.0f, 1.0f), // pure magenta
		Color(1.0f, 1.0f, 0.0f), // pure yellow
		Color(0.6f, 1.0f, 0.6f), // pastel green
		Color(0.6f, 1.0f, 1.0f), // pastel cyan
		Color(1.0f, 0.6f, 1.0f), // pastel magenta
		Color(1.0f, 1.0f, 0.6f), // pastel yellow
		Color(0.0f, 0.7f, 0.0f), // dark green
		Color(0.0f, 0.7f, 0.7f), // dark cyan
		Color(0.7f, 0.0f, 0.7f), // dark magenta
		Color(0.7f, 0.7f, 0.0f), // dark yellow
	};

	static const U32F kColorTableSize = ARRAY_COUNT(s_colorTable);
	Color color = s_colorTable[U32F(index) % kColorTableSize];
	color.SetA(alpha);
	return color;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* s_npcLogChannelNames[kNpcLogChannelCount] = 
{
	"General",		   // kNpcLogChannelGeneral
	"Nav",             // kNpcLogChannelNav
	"NavDetails",      // kNpcLogChannelNavDetails
	"Behavior",        // kNpcLogChannelBehavior
	"BehaviorDetails", // kNpcLogChannelBehaviorDetails
	"Skill",           // kNpcLogChannelSkill
	"SkillDetails",    // kNpcLogChannelSkillDetails
	"Anim",            // kNpcLogChannelAnim
	"AnimDetails",     // kNpcLogChannelAnimDetails
	"Perception",      // kNpcLogChannelPerception
	"Move",            // kNpcLogChannelMove
	"MoveDetails",     // kNpcLogChannelMoveDetails
	"Combat",          // kNpcLogChannelCombat
	"CombatDetails",   // kNpcLogChannelCombatDetails
};

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AI::GetNpcLogChannelName(U8 chan)
{
	GAMEPLAY_ASSERT(chan < kNpcLogChannelCount);
	return s_npcLogChannelNames[chan];
}

/// --------------------------------------------------------------------------------------------------------------- ///
#if !FINAL_BUILD

bool MailNpcLogTo(const NavCharacter* pNavChar,
				  const char* toAddr,
				  const char* subject,
				  const char* szFile,
				  U32F line,
				  const char* szFunc)
{
	if (!pNavChar || !toAddr)
		return false;

	if (strlen(g_realUserName) <= 0)
		return false;

	StringBuilder<256> fromAddr;
	fromAddr.append_format("%s-devkit@naughtydog.com", g_realUserName);

	beginMail(toAddr, StringBuilder<1024>("AI Log: %s", subject).c_str(), fromAddr.c_str());

	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
		const U32 kMailBufferSize = 16 * 1024;
		char* pMailBuffer		  = NDI_NEW char[kMailBufferSize];
		StringBuilderExternal* pSb = StringBuilderExternal::Create(pMailBuffer, kMailBufferSize);

		pSb->append_format("Something bad happened. Here's the log.\n\n");
		pSb->append_format("%s @ %s:%d\n\n", szFunc, szFile, line);

		GameDumpProgramStateInternal(*pSb, nullptr, 0, nullptr, nullptr, kEhcsSuccess, nullptr, false, false);

		addMailBody(*pSb);
	}

	if (const DoutMem* pMsgHistory = MsgGetHistoryMem())
	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
		const U32 bufferSize = tempAlloc.GetFreeSize();
		char* pMsgBuffer	 = NDI_NEW char[bufferSize];
		const U32F histSize	 = MsgGetHistory(pMsgBuffer, bufferSize);
		addMailAttachment(pMsgBuffer, histSize, "tty.txt");
	}

	if (const DoutMemChannels* pDebugLog = pNavChar->GetChannelDebugLog())
	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
		const U32 bufferSize = tempAlloc.GetFreeSize();
		char* pBuffer		 = NDI_NEW char[bufferSize];
		const U32F logSize	 = pDebugLog->DumpAllToBuffer(pBuffer, bufferSize, AI::GetNpcLogChannelName, true);
		GAMEPLAY_ASSERT(logSize < bufferSize);
		addMailAttachment(pBuffer, logSize, StringBuilder<256>("%s-log.txt", pNavChar->GetName()).c_str());
	}

	endMail();

	return true;
}
#endif // !FINAL_BUILD
