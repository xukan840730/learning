/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/system/atomic-assert.h"

/// --------------------------------------------------------------------------------------------------------------- ///
//
// Enable this to enable lots of extra animation debug features.
//
#ifndef FINAL_BUILD
// 	#define ANIM_DEBUG
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
//
// Enable this to make ANIM_ASSERTS into ALWAYS_ASSERTS
//
#ifndef ENABLE_ANIM_ASSERTS
#ifdef FINAL_BUILD
#define ENABLE_ANIM_ASSERTS 0
#else
#define ENABLE_ANIM_ASSERTS 1
#endif
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
//
// Enable this to get statistics on how many objects are animating in which bucket and such
//
#ifndef FINAL_BUILD
//	#define ANIM_STATS_ENABLED
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
//
// Enable this to validate that each command push the correct amount of data into the command stream.
//
#ifndef FINAL_BUILD
#define ENABLE_ANIM_CMD_GEN_VALIDATION 0
#else
#define ENABLE_ANIM_CMD_GEN_VALIDATION 0
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
#if ENABLE_ANIM_ASSERTS
#define ANIM_ASSERT(x) ALWAYS_ASSERT(x)
#define ANIM_ASSERTF(x, y) ALWAYS_ASSERTF(x, y)
#else
#define ANIM_ASSERT(x) ASSERT(x)
#define ANIM_ASSERTF(x, y) ASSERTF(x, y)
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
//
// Enable this to get detailed animation system output
//
//#define VERBOSE_ANIM_DEBUG

#ifdef VERBOSE_ANIM_DEBUG
void MsgAnimVerbose(const char* format, ...);
#else
#define MsgAnimVerbose
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimError(const char* strMsg, ...);

/// --------------------------------------------------------------------------------------------------------------- ///
#define ANIM_TRACK_DEBUG 0

#if ANIM_TRACK_DEBUG
extern void LogAnimation(const class ArtItemAnim* pAnimation, float frame, bool retargeted);
#else
#define LogAnimation(a, f, r)
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
#ifdef FINAL_BUILD
inline bool MailAnimLogTo(const char* toAddr)
{
	return false;
}
#else
bool MailAnimLogTo(const char* toAddr);
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
#ifndef ENABLE_ANIM_ASSERTION_ATOMICS
#define ENABLE_ANIM_ASSERTION_ATOMICS 1
#endif

#if ENABLE_ANIM_ASSERTION_ATOMICS
DECLARE_ASSERT_NOT_IN_USE_JANITOR(AnimDcAssertNotRelocatingJanitor, g_dcRelocationCheckAtomic);
#else
DECLARE_ASSERT_NOT_IN_USE_JANITOR_DISABLED(AnimDcAssertNotRelocatingJanitor);
#endif
