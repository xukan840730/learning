/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/component/ai-script-logger.h"

#include "corelib/util/msg-mem.h"

#include "ndlib/render/util/screen-space-text-printer.h"

#include "gamelib/debug/ai-msg-log.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AiScriptLogger::AiScriptLogger()
	: m_pScriptLogMem(nullptr)
	, m_pScriptLog(nullptr)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiScriptLogger::Init()
{
	m_currentScriptLogEntry = 0;
	m_numScriptLogEntries = 0;

	m_pScriptLog = nullptr;
	m_pScriptLogMem = nullptr;

	U32 debugMemSize;

	if (NavCharacter::s_pFnAllocDebugMem)
	{
		m_pScriptLogMem = NavCharacter::s_pFnAllocDebugMem(&debugMemSize, NavCharacter::kDebugMemScriptLog);

		if (m_pScriptLogMem)
		{
			const static U32 kMaxNuggets = 3072;
			const U32 nuggetMaxSize		 = (kMaxNuggets * sizeof(MsgNugget));
			const U32 nuggetBufSize		 = AlignSize(Min(nuggetMaxSize, (U32)(0.1f * debugMemSize)), kAlign8);
			const U32F doutMemSize		 = AlignSize(debugMemSize - nuggetBufSize, kAlign8);

			DoutMem* dOut = NDI_NEW(kAllocDebug) DoutMem("ScriptDebugLog", static_cast<char*>(m_pScriptLogMem), doutMemSize);
			m_pScriptLog  = NDI_NEW(kAllocDebug) DoutMemChannels(dOut, static_cast<char*>(m_pScriptLogMem) + doutMemSize, debugMemSize - doutMemSize);
			m_pScriptLog->SetRemoveDuplicates(true);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiScriptLogger::FreeMemory()
{
	if (m_pScriptLogMem)
	{
		if (NavCharacter::s_pFnFreeDebugMem)
		{
			NavCharacter::s_pFnFreeDebugMem(m_pScriptLogMem, NavCharacter::kDebugMemScriptLog);
		}

		m_pScriptLogMem = nullptr;
		m_pScriptLog = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiScriptLogger::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pScriptLog, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiScriptLogger::AddScriptLog(const char* buf, TimeFrame time)
{
	STRIP_IN_FINAL_BUILD;
#ifndef FINAL_BUILD
	strncpy(m_scriptLog[m_currentScriptLogEntry].m_buf, buf, kScriptLogMaxLen);
	m_scriptLog[m_currentScriptLogEntry].m_time = time;
	m_currentScriptLogEntry = (m_currentScriptLogEntry + 1) % kMaxScriptLogEntries;
	m_numScriptLogEntries = Min(m_numScriptLogEntries + 1, kMaxScriptLogEntries);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiScriptLogger::Dump(DoutBase* pOut)
{
	if (m_pScriptLog)
		m_pScriptLog->DumpAllToTty(pOut, nullptr, true, 200);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiScriptLogger::DebugDraw(ScreenSpaceTextPrinter* pPrinter, Clock* pClock)
{
	if (!pPrinter || !pClock)
		return;

	for (I32F iEntryNum = 0; iEntryNum < m_numScriptLogEntries; iEntryNum++)
	{
		I32F iEntry = m_currentScriptLogEntry - iEntryNum - 1;

		if (iEntry < 0)
			iEntry += kMaxScriptLogEntries;

		pPrinter->PrintText(kColorGreen, "%5.2f: %s", pClock->GetTimePassed(m_scriptLog[iEntry].m_time).ToSeconds(), m_scriptLog[iEntry].m_buf);
	}
}
