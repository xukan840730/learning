/*
* Copyright (c) 2003 Naughty Dog, Inc. 
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "ndlib/anim/anim-debug.h"

#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/user.h"

#include "ndlib/netbridge/mail.h"

#include <stdarg.h>

/// --------------------------------------------------------------------------------------------------------------- ///
#ifdef VERBOSE_ANIM_DEBUG
#include "ndlib/anim/anim-options.h"
void MsgAnimVerbose(const char* format, ...)
{
	if (g_animOptions.m_verboseAnimDebug)
	{
		DoutBase* pOutput = GetMsgOutput(kMsgAnim);
		if (pOutput)
		{
			va_list ap;
			va_start(ap, format);
			pOutput->Vprintf(format, ap);
			va_end(ap);
		}
	}
}
#endif // #ifdef VERBOSE_ANIM_DEBUG

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimError(const char* strMsg, ...)
{
	char strBuffer[512];
	va_list args;
	va_start(args, strMsg);
	vsnprintf(strBuffer, 512, strMsg, args);
	va_end(args);
	MsgErr("Anim: ");
	MsgErr(strBuffer);
	MsgConErr(strBuffer);
}

#if ANIM_TRACK_DEBUG

/// --------------------------------------------------------------------------------------------------------------- ///
class StringTable
{
public:
	static const U32 kBlockSize = 4096;

	StringTable() : m_aChar(nullptr), m_alloc(0), m_size(0) { }

	const char* Add(const char* pStr)
	{
		if (pStr)
		{
			U32 bytes = strlen(pStr) + 1;
			if (bytes > 1)
			{
				U32 newSize = m_size + bytes;

				if (newSize > m_alloc)
				{
					// allocate a brand new block

					U32 newAlloc = Max(bytes, kBlockSize);

					m_aChar = NDI_NEW(kAllocDebug) char[newAlloc];
					if (!m_aChar)
					{
						m_alloc = m_size = 0;
						return nullptr;
					}

					m_alloc = newAlloc;
					m_size = 0;
					newSize = bytes;
				}
				SYSTEM_ASSERT(newSize <= m_alloc);

				char* pTableStr = &m_aChar[m_size];

				strcpy(pTableStr, pStr);
				m_size = newSize;

				return pTableStr;
			}
		}

		return nullptr;
	}

private:
	char* m_aChar;
	U32 m_alloc;
	U32 m_size;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ClipLog
{
public:
	static const U32 kBlockSize = 4096;

	struct Clip
	{
		StringId64	m_animId;
		U32			m_hierarchyId;

		const char* m_animName;
		const char* m_pakName;
		F32			m_minFrame;
		F32			m_maxFrame;
		U32			m_totalSize;
		F32			m_durationSeconds;
		F32			m_framesPerSecond;
		bool		m_retargeted;

		static int Compare(const Clip* pA, const Clip* pB)
		{
			int diff = 0;
			if (pA && pB)
			{
				if (pA->m_pakName && pB->m_pakName)
				{
					diff = stricmp(pA->m_pakName, pB->m_pakName);
				}
				if (diff == 0 && pA->m_animName && pB->m_animName)
				{
					diff = stricmp(pA->m_animName, pB->m_animName);
				}

				int unusedA = (pA->m_minFrame < 0.0f) ? 0x40000000 : 0;
				int unusedB = (pB->m_minFrame < 0.0f) ? 0x40000000 : 0;

				diff = diff + unusedA - unusedB;
			}
			else if (pA)
			{
				diff = -1; // A before B
			}
			else if (pB)
			{
				diff = +1; // B before A
			}
			return diff;
		}
	};

	ClipLog() : m_aClip(nullptr), m_alloc(0), m_count(0) { }

	void Log(const ArtItemAnim* pAnimation, float frame, bool retargeted)
	{
		ANIM_ASSERT(m_count <= m_alloc);

		if (!pAnimation || !pAnimation->m_pClipData)
			return;

		U32 hierarchyId = pAnimation->m_pClipData->m_animHierarchyId;

		//MsgUser11("EvalClip, %6.1f, %s, %s\n", frame, pAnimation->GetName(), pAnimation->m_pPakName);

		// try to find the clip

		const StringId64 animId = pAnimation->GetNameId();

		Clip* pClip = Find(hierarchyId, animId);
		if (pClip)
		{
			// update the min and max frame indices

			if (frame >= 0.0f)
			{
				pClip->m_retargeted = pClip->m_retargeted || retargeted; // set to false when initially logged in; set to true if ever retargetted

				if (pClip->m_minFrame < 0.0f || pClip->m_minFrame > frame)
					pClip->m_minFrame = frame;
				if (pClip->m_maxFrame < frame)
					pClip->m_maxFrame = frame;
			}
		}
		else
		{
			// not found -- add it

			pClip = Add();
			if (pClip)
			{
				pClip->m_hierarchyId = hierarchyId;
				pClip->m_animId = animId;
				pClip->m_animName = m_strings.Add(pAnimation->GetName());
				pClip->m_pakName = m_strings.Add(pAnimation->m_pPakName);
				pClip->m_minFrame = pClip->m_maxFrame = frame;
				pClip->m_totalSize = pAnimation->m_pClipData->m_totalSize;
				pClip->m_durationSeconds = pAnimation->m_pClipData->m_fNumFrameIntervals * pAnimation->m_pClipData->m_secondsPerFrame;
				pClip->m_framesPerSecond = pAnimation->m_pClipData->m_framesPerSecond;
				pClip->m_retargeted = retargeted;
			}
		}
	}

	Clip* Find(U32 hierarchyId, StringId64 animId)
	{
		for (U32F i = 0; i < m_count; ++i)
		{
			Clip& clip = m_aClip[i];

			if (clip.m_hierarchyId == hierarchyId
			&&  clip.m_animId      == animId)
			{
				return &clip;
			}
		}
		return nullptr;
	}

	Clip* Add()
	{
		if (m_count == m_alloc)
		{
			// out of room -- allocate more

			U32 loggedClipAlloc = m_alloc + kBlockSize;

			Clip* aLoggedClip = NDI_NEW(kAllocDebug) Clip[loggedClipAlloc];
			if (!aLoggedClip)
				return nullptr;

			if (m_count != 0)
			{
				ANIM_ASSERT(m_aClip != nullptr);

				memcpy(aLoggedClip, m_aClip, m_count * sizeof(aLoggedClip[0]));
				NDI_DELETE[] m_aClip;
			}

			m_alloc = loggedClipAlloc;
			m_aClip = aLoggedClip;
		}

		return &m_aClip[m_count++];
	}

	void Dump()
	{
		ScopedTempAllocator alloc(FILE_LINE_FUNC);

		Clip** apClip = NDI_NEW Clip*[m_count];
		if (apClip)
		{
			for (U32F i = 0; i < m_count; ++i)
			{
				apClip[i] = &m_aClip[i];
			}

			QuickSort(apClip, m_count, Clip::Compare);

			// er, why?
			MsgUser11("%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
					  "Actor",
					  "Anim",
					  "Duration(s)",
					  "Played(s)",
					  "PlayedPct",
					  "Size",
					  "FPS",
					  "StartFr",
					  "EndFr",
					  "Retarg");

			for (U32F i = 0; i < m_count; ++i)
			{
				ANIM_ASSERT(apClip[i] != nullptr);
				Clip& clip = *apClip[i];
				if (clip.m_minFrame >= 0.0f)
				{
					float playedSeconds = (clip.m_maxFrame - clip.m_minFrame) / 30.0f;
					float playedPct = (clip.m_durationSeconds > 0.0f) ? 100.0f * playedSeconds / clip.m_durationSeconds : 0.0f;

					MsgUser11("%s,%s,%.1f,%.1f,%.1f,%u,%.0f,%.1f,%.1f,%s\n",
							  clip.m_pakName,
							  clip.m_animName,
							  clip.m_durationSeconds,
							  playedSeconds,
							  playedPct,
							  clip.m_totalSize,
							  clip.m_framesPerSecond,
							  clip.m_minFrame,
							  clip.m_maxFrame,
							  (clip.m_retargeted ? "yes" : "no"));
				}
				else if (g_animOptions.m_dumpUnusedClips)
				{
					MsgUser11("%s,%s,%s,%.1f,%.1f,%u,%.0f,%.1f,%.1f,%s\n",
							  clip.m_pakName,
							  clip.m_animName,
							  "UNUSED",
							  0.0f,
							  0.0f,
							  clip.m_totalSize,
							  clip.m_framesPerSecond,
							  -1.0f,
							  -1.0f,
							  (clip.m_retargeted ? "yes" : "no"));
				}
			}
		}
	}

private:
	Clip* m_aClip;
	U32 m_alloc;
	U32 m_count;
	StringTable m_strings;
};

ClipLog g_clipLog;

/// --------------------------------------------------------------------------------------------------------------- ///
void LogAnimation(const ArtItemAnim* pAnimation, float frame, bool retargeted)
{
	if (g_animOptions.m_dumpPlayedClips)
	{
		g_animOptions.m_dumpPlayedClips = false;
		g_clipLog.Dump();
	}

	if (g_animOptions.m_trackAnimUsage)
	{
		g_clipLog.Log(pAnimation, frame, retargeted);
	}
}

#endif // #if ANIM_TRACK_DEBUG

#ifndef FINAL_BUILD

/// --------------------------------------------------------------------------------------------------------------- ///
bool MailAnimLogTo(const char* toAddr)
{
	STRIP_IN_FINAL_BUILD;

	if (!toAddr)
		return false;

	if (strlen(g_realUserName) <= 0)
		return false;

	char fromAddr[256];
	sprintf(fromAddr, "%s-devkit@naughtydog.com", g_realUserName);

	beginMail(toAddr, "Anim Log", fromAddr);

	{
		const char* header = "Something bad happened. The log is attached.\n\n";
		addMailBody(header, strlen(header));
	}

	if (const DoutMem* pMsgHistory = MsgGetHistoryMem())
	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
		const U32 bufferSize = tempAlloc.GetFreeSize();
		char* pMsgBuffer = NDI_NEW char[bufferSize];
		const U32F histSize = MsgGetHistory(pMsgBuffer, bufferSize);
		addMailAttachment(pMsgBuffer, histSize, "tty.txt");
	}

	endMail();

	return true;
}

#endif //FINAL_BUILD
