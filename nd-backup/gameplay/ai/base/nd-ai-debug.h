/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/color.h"

#include "gamelib/gameplay/ai/base/nd-ai-options.h"

/// --------------------------------------------------------------------------------------------------------------- ///
/// Debugging functions: Function dedicated to the debugging of the AI. This layer lives above
/// the standard debugging system. It is critical to keep this layer as AI debugging is VERY specific.
/// --------------------------------------------------------------------------------------------------------------- ///

class ActionPack;
class EntitySpawner;
class Locator;
class NavCharacter;
class NavLocation;
class Level;
class SimpleNavCharacter;
class SsAction;
class SsTrackInstance;

enum NpcLogChannel
{
	kNpcLogChannelGeneral,
	kNpcLogChannelNav,
	kNpcLogChannelNavDetails,
	kNpcLogChannelBehavior,
	kNpcLogChannelBehaviorDetails,
	kNpcLogChannelSkill,
	kNpcLogChannelSkillDetails,
	kNpcLogChannelAnim,
	kNpcLogChannelAnimDetails,
	kNpcLogChannelPerception,
	kNpcLogChannelMove,
	kNpcLogChannelMoveDetails,
	kNpcLogChannelCombat,
	kNpcLogChannelCombatDetails,
	kNpcLogChannelCount
};

namespace AI
{
	Color IndexToColor(I32F index, float alpha = 1.0f);

	const char* GetNpcLogChannelName(U8 chan);
} // namespace AI

/// --------------------------------------------------------------------------------------------------------------- ///
void AiError(const char* strMsg, ...);
void AiError(const EntitySpawner& spawner, const char* strMsg, ...);
void AiError(Point_arg posWs, const Level* pLevel, const char* strMsg, ...);
void AiError(Point_arg posWs, F32 timeOut, const char* strMsg, ...);

#ifndef FINAL_BUILD

/// --------------------------------------------------------------------------------------------------------------- ///
struct AiLogNugget
{
	AiLogNugget();
	AiLogNugget(const char* szFile, U32F line, const char* szFunc);

	bool m_bValid;

	const char* m_szFile;
	const char* m_szFunc;
	U32F m_line;
};

#define AI_LOG AiLogNugget(FILE_LINE_FUNC)
#define AI_LOG_PARAM const AiLogNugget& logNugget

#else

#define AI_LOG -1
#define AI_LOG_PARAM U32 logNugget
typedef U32 AiLogNugget;

#endif

/// --------------------------------------------------------------------------------------------------------------- ///
#ifdef FINAL_BUILD
	inline bool ShouldLog() { return false; }
	inline bool ShouldPrint(NpcLogChannel chan) { return false; }

	inline void Log(const NavCharacter* pNavChar, NpcLogChannel chan, const char* str, ...) {}
	inline void LogHdr(const NavCharacter* pNavChar, NpcLogChannel chan) {}
	inline void LogMsg(const NavCharacter* pNavChar, NpcLogChannel chan, const char* str, ...) {}
	inline void LogMsgAppend(const NavCharacter* pNavChar, NpcLogChannel chan, const char* str, ...) {}
	inline void LogPoint(const NavCharacter* pNavChar, NpcLogChannel chan, Point_arg pt) {}
	inline void LogVector(const NavCharacter* pNavChar, NpcLogChannel chan, Vector_arg v) {}
	inline void LogLocator(const NavCharacter* pNavChar, NpcLogChannel chan, const Locator& loc) {}
	inline void LogActionPack(const NavCharacter* pNavChar, NpcLogChannel chan, const ActionPack* pAp) {}
	inline void LogNavLocation(const NavCharacter* pNavChar, NpcLogChannel chan, const NavLocation& navLoc) {}
	inline void LogNugget(const NavCharacter* pNavChar, NpcLogChannel chan, const AiLogNugget& nugget) {}

	// Channel specific convenience functions
	inline void Log(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLog(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogNav(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogNavDetails(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogBehaviorInternal(const NavCharacter* pNavChar, const char* str, va_list vargs) {}
	inline void AiLogBehaviorInternal(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogBehaviorDetails(const NavCharacter* pNavChar, const char* str, va_list vargs) {}
	inline void AiLogBehaviorDetails(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogSkill(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogSkillDetails(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogAnim(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogAnim(const NavCharacter* pNavChar, const AiLogNugget& nugget, const char* str, ...) {}
	inline void AiLogAnimDetails(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogCombat(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogCombatDetails(const NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogScript(NavCharacter* pNavChar, const char* str, ...) {}
	inline void AiLogScript(SimpleNavCharacter* pNavChar, const char* str, ...) {}
	inline bool AiLogScriptLocation(IStringBuilder* pString) { return false; }
	inline bool AiLogScriptLocation(const SsAction& ssAction, IStringBuilder* pStringOut) { return false; }
	inline bool AiLogScriptLocation(const SsInstance* pScriptInst, const SsTrackInstance* pTrackInst, IStringBuilder* pStringOut) { return false; }
	inline bool AiLogScriptSource(IStringBuilder* pStringOut) { return false; }
	inline void AiLogScriptSource(const SsInstance* pScriptInst, IStringBuilder* pStringOut) {}
	inline void AiLogScriptSource(const SsAction& ssAction, IStringBuilder* pStringOut) {}

	inline bool MailNpcLogTo(const NavCharacter* pNavChar,
							 const char* toAddr,
							 const char* subject,
							 const char* szFile,
							 U32F line,
							 const char* szFunc)
	{
		return false;
	}

#else
	bool ShouldPrint(NpcLogChannel chan);
	bool ShouldLog();

	void Log(const NavCharacter* pNavChar, NpcLogChannel chan, const char* str, ...);
	void LogHdr(const NavCharacter* pNavChar, NpcLogChannel chan);
	void LogMsg(const NavCharacter* pNavChar, NpcLogChannel chan, const char* str, ...);
	void LogMsgAppend(const NavCharacter* pNavChar, NpcLogChannel chan, const char* str, ...);
	void LogPoint(const NavCharacter* pNavChar, NpcLogChannel chan, Point_arg pt);
	void LogVector(const NavCharacter* pNavChar, NpcLogChannel chan, Vector_arg v);
	void LogLocator(const NavCharacter* pNavChar, NpcLogChannel chan, const Locator& loc);
	void LogActionPack(const NavCharacter* pNavChar, NpcLogChannel chan, const ActionPack* pAp);
	void LogNavLocation(const NavCharacter* pNavChar, NpcLogChannel chan, const NavLocation& navLoc);
	void LogNugget(const NavCharacter* pNavChar, NpcLogChannel chan, const AiLogNugget& nugget);

	// Channel specific convenience functions
	void Log(const NavCharacter* pNavChar, const char* str, ...);
	void AiLog(const NavCharacter* pNavChar, const char* str, ...);
	void AiLogNav(const NavCharacter* pNavChar, const char* str, ...);
	void AiLogNavDetails(const NavCharacter* pNavChar, const char* str, ...);
	void AiLogBehaviorInternal(const NavCharacter* pNavChar, const char* str, va_list vargs);
	void AiLogBehaviorInternal(const NavCharacter* pNavChar, const char* str, ...);
	void AiLogBehaviorDetails(const NavCharacter* pNavChar, const char* str, va_list vargs);
	void AiLogBehaviorDetails(const NavCharacter* pNavChar, const char* str, ...);
	void AiLogSkill(const NavCharacter* pNavChar, const char* str, ...);
	void AiLogSkillDetails(const NavCharacter* pNavChar, const char* str, ...);
	void AiLogAnim(const NavCharacter* pNavChar, const char* str, ...);
	void AiLogAnim(const NavCharacter* pNavChar, const AiLogNugget& nugget, const char* str, ...);
	void AiLogAnimDetails(const NavCharacter* pNavChar, const char* str, ...);
	void AiLogCombat(const NavCharacter* pNavChar, const char* str, ...);
	void AiLogCombatDetails(const NavCharacter* pNavChar, const char* str, ...);
	void AiLogScript(NavCharacter* pNavChar, const char* str, ...);
	void AiLogScript(SimpleNavCharacter* pNavChar, const char* str, ...);
	bool AiLogScriptLocation(IStringBuilder* pString);
	bool AiLogScriptLocation(const SsAction& ssAction, IStringBuilder* pStringOut);
	bool AiLogScriptLocation(const SsInstance* pScriptInst, const SsTrackInstance* pTrackInst, IStringBuilder* pStringOut);
	bool AiLogScriptSource(IStringBuilder* pStringOut);
	void AiLogScriptSource(const SsInstance* pScriptInst, IStringBuilder* pStringOut);
	void AiLogScriptSource(const SsAction& ssAction, IStringBuilder* pStringOut);

	bool MailNpcLogTo(const NavCharacter* pNavChar,
					  const char* toAddr,
					  const char* subject,
					  const char* szFile,
					  U32F line,
					  const char* szFunc);
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
// MsgCon helper functions
void MsgAiPrintPoint(const NavCharacter* pNavChar, Point_arg pt);
void MsgAiPrintActionPack(const NavCharacter* pNavChar, const ActionPack* pAp);

/// --------------------------------------------------------------------------------------------------------------- ///
//
// Enable this to enable lots of extra AI debug features.
//
#ifndef FINAL_BUILD
	#define AI_DEBUG
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
// Enable AI asserts if you are running with AI_DEBUG enabled
#ifdef AI_DEBUG
	#ifndef ENABLE_AI_ASSERTS
		#define ENABLE_AI_ASSERTS  1
	#endif
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
#if ENABLE_AI_ASSERTS
	#define AI_ASSERT(x)		ALWAYS_ASSERT(x)
	#define AI_ASSERTF(x, y)	ALWAYS_ASSERTF(x, y)
	#define AI_HALTF(x)			ALWAYS_HALTF(x)
#else
	#define AI_ASSERT(x)		ASSERT(x)
	#define AI_ASSERTF(x, y)	ASSERTF(x, y)
	#define AI_HALTF(x)			DEBUG_HALTF(x)
#endif
