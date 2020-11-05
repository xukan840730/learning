/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

class ScreenSpaceTextPrinter;
class DoutMemChannels;

/// --------------------------------------------------------------------------------------------------------------- ///
class AiScriptLogger
{
public:
	static const U32 kMaxScriptLogEntries = 5;
	static const U32 kScriptLogMaxLen = 256;

	AiScriptLogger();

	void Init();
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	void DebugDraw(ScreenSpaceTextPrinter* pPrinter, Clock* pClock);
	DoutMemChannels* GetScriptDebugLog() const { return m_pScriptLog; }

	void Dump(DoutBase* pOut);
	void AddScriptLog(const char* buf, TimeFrame time);

	void FreeMemory();

private:

	struct ScriptLogEntry
	{
		char			m_buf[kScriptLogMaxLen];
		TimeFrame		m_time;
	};

	ScriptLogEntry		m_scriptLog[kMaxScriptLogEntries];
	U32					m_currentScriptLogEntry;
	U32					m_numScriptLogEntries;

	void* m_pScriptLogMem;
	mutable DoutMemChannels* m_pScriptLog;
};
