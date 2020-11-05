/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-stream-loader.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/io/file-system.h"
#include "ndlib/io/package-util.h"
#include "ndlib/io/pak-structs.h"

 /// --------------------------------------------------------------------------------------------------------------- ///
#define MsgAnimStream(str, ...)                                                                                        \
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_debugStreaming))                                                          \
	{                                                                                                                  \
		const float curTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime().ToSeconds();     \
		MsgAnim("[Streaming] [%0.2f] [%s] " str, curTime, streamName, __VA_ARGS__);                                    \
	}

 /// --------------------------------------------------------------------------------------------------------------- ///
#define MsgAnimStreamVerbose(str, ...)                                                                                 \
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_debugStreamingVerbose))                                                   \
	{                                                                                                                  \
		const float curTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime().ToSeconds();     \
		MsgAnim("[Streaming] [%0.2f] [%s] " str, curTime, streamName, __VA_ARGS__);                                    \
	}

 /// --------------------------------------------------------------------------------------------------------------- ///
#define ENABLE_ANIM_STREAMING_PRELOADING		1					// We now use prefetching for up to 16 MiB ahead of where we want to read right now.
static const I64 kPrefetchChunkSize = 512 * 1024;		// Make sure this size correspond to the block size of the RAM Cache
static const I64 kPrefetchingChunkLookAhead = 8;				// 4 MiB, make sure this size doesn't overwhelm the RAM Cache
static const I64 kMaxPrefetchOps = 32;
static ALIGNED(16) char g_animStreamPrefetchDest[16];				// All prefetching of anim stream data writes to this dummy location
static FileSystem::Operation g_prefetchOps[kMaxPrefetchOps];

StreamLoaderPool* s_pStreamLoaderPool = nullptr;

/// --------------------------------------------------------------------------------------------------------------- ///
void GetAnimStreamFileName(const char* streamName, char* filename)
{
	// global
	sprintf(filename,
			"%s/animstream%d/%s.stm",
			EngineComponents::GetNdGameInfo()->m_pathDetails.m_dataDir,
			g_ndConfig.m_actorPakFolder,
			streamName);
	// MsgCinematic("ANIMSTREAM: GLOBAL: %s\n", filename);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStreamLoader::AnimStreamLoader()
{
	m_fileHandle = 0;
	m_fileOpenOp = 0;
	m_fileCloseOp = 0;
	m_fileReadOp = 0;
	//m_fileStatOp = 0;
	m_used = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamLoader::Reset()
{
	m_used = false;

	m_issueReadTick = 0;
	m_lastReadSize = -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStreamLoader::IsReading() const
{
	return m_fileReadOp != 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
I64 AnimStreamLoader::GetFileSize()
{
	if (m_fileStatOp != 0)
	{
		Err err;
		if (EngineComponents::GetFileSystem()->IsDone(m_fileStatOp, &err))
		{
			EngineComponents::GetFileSystem()->ReleaseOpEx(m_fileStatOp, FILE_LINE_FUNC);
			m_fileStatOp = 0;
		}
	}

	if (m_fileHandle != 0 && m_fileOpenOp == 0 && m_fileStatOp == 0)
	{
		return (I64)m_stat.m_uSize;
	}

	return -1LL;
}
*/

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamLoader::Read(U8* pBuffer, int offset, int size, const char* streamName)
{
	ANIM_ASSERT(m_used);
	ANIM_ASSERT(!IsReading());
	ANIM_ASSERT(m_fileCloseOp == 0 && m_fileOpenOp == 0 && m_fileHandle != 0);
	ANIM_ASSERT(pBuffer);

	// request our next chunk
	const Err preadErr = EngineComponents::GetFileSystem()->PreadAsyncEx(m_fileHandle,
																		 pBuffer,
																		 offset,
																		 size,
																		 &m_fileReadOp,
																		 FILE_LINE_FUNC,
																		 false,
																		 FileSystem::kPriorityAnimstream);
	MsgAnimStreamVerbose("Issued Read : %d bytes @ offset %d (%s)\n", size, offset, preadErr.GetMessageText());
	m_issueReadTick = TimerGetRawCount();

#if ENABLE_ANIM_STREAMING_PRELOADING
	const char* filePath = nullptr;
	Err err = EngineComponents::GetFileSystem()->GetPath(m_fileHandle, &filePath);
	if (err == Err::kOK)
	{
		if (offset == 0)
		{
			for (I64 prefetchChunk = 1; prefetchChunk < kPrefetchingChunkLookAhead; ++prefetchChunk)
			{
				// Find a free op
				I64 prefetchOpIndex = 0;
				for (; prefetchOpIndex < kMaxPrefetchOps; ++prefetchOpIndex)
				{
					if (!g_prefetchOps[prefetchOpIndex])
					{
						// Mark it as taken
						g_prefetchOps[prefetchOpIndex] = 1;
						break;
					}
				}

				if (prefetchOpIndex < kMaxPrefetchOps)
				{
					EngineComponents::GetFileSystem()->PreadAsyncEx(m_fileHandle,
						g_animStreamPrefetchDest,
						AlignSize(prefetchChunk * kPrefetchChunkSize, kAlign512k),
						1,
						&g_prefetchOps[prefetchOpIndex], FILE_LINE_FUNC, false, FileSystem::kPriorityAnimstream);

					MsgAnimStreamVerbose("Preload Read : offset %d, opIndex %lld\n", AlignSize(prefetchChunk * kPrefetchChunkSize, kAlign512k), prefetchOpIndex);
				}
			}
		}
		else
		{
			// Find a free op
			I64 prefetchOpIndex = 0;
			for (; prefetchOpIndex < kMaxPrefetchOps; ++prefetchOpIndex)
			{
				if (!g_prefetchOps[prefetchOpIndex])
				{
					// Mark it as taken
					g_prefetchOps[prefetchOpIndex] = 1;
					break;
				}
			}

			if (prefetchOpIndex < kMaxPrefetchOps)
			{
				EngineComponents::GetFileSystem()->PreadAsyncEx(m_fileHandle,
					g_animStreamPrefetchDest,
					AlignSize(offset + kPrefetchingChunkLookAhead * kPrefetchChunkSize, kAlign512k),
					1,
					&g_prefetchOps[prefetchOpIndex], FILE_LINE_FUNC, false, FileSystem::kPriorityAnimstream);
				MsgAnimStreamVerbose("Preload Read : offset %d, opIndex %lld\n", AlignSize(offset + kPrefetchingChunkLookAhead * kPrefetchChunkSize, kAlign512k), prefetchOpIndex);
			}
		}
	}
#endif

	m_lastReadSize = size;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStreamLoader::WaitForRead(Err* pErr, const char* streamName)
{
	PROFILE(IO, AnimStreamLoader_WaitForRead);

	ANIM_ASSERT(m_used);
	ANIM_ASSERT(IsReading());

	*pErr = Err::kOK;

	{
		Err err;
		I64 numBytesRead;
		if (EngineComponents::GetFileSystem()->IsDone(m_fileReadOp, &err, &numBytesRead))
		{
			float elapsedMs = ConvertTicksToMilliseconds(TimerGetRawCount() - m_issueReadTick);
			MsgAnimStreamVerbose("Finished Read : asked %d bytes -> got %d bytes (%s) in %.2f ms\n", m_lastReadSize, numBytesRead, err.GetMessageText(), elapsedMs);

			// newblockIndex - 1 is the index into the block sizes list, since the block sizes list ignores the first block
			EngineComponents::GetFileSystem()->ReleaseOpEx(m_fileReadOp, FILE_LINE_FUNC);
			m_fileReadOp = 0;

			if (err.Failed())
			{
				ANIM_ASSERTF(!EngineComponents::GetNdGameInfo()->m_onDisc, ("error(%d)", err.GetCode()));
				*pErr = err;

				m_lastReadSize = -1;
				return false;
			}
			else if (m_lastReadSize != numBytesRead)
			{
				ANIM_ASSERTF(!EngineComponents::GetNdGameInfo()->m_onDisc, ("m_lastReadSize(%d) != numBytesRead(%d)", m_lastReadSize, numBytesRead));
				*pErr = Err::kErrBadData;

				m_lastReadSize = -1;
				return false;
			}
			else
			{
				m_lastReadSize = -1;
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamLoader::ReleaseCompletedOps()
{
	if (m_fileOpenOp && EngineComponents::GetFileSystem()->IsDone(m_fileOpenOp))
	{
		EngineComponents::GetFileSystem()->ReleaseOpEx(m_fileOpenOp, FILE_LINE_FUNC);
		m_fileOpenOp = 0;
	}

/*
	if (m_fileStatOp && EngineComponents::GetFileSystem()->IsDone(m_fileStatOp))
	{
		EngineComponents::GetFileSystem()->ReleaseOpEx(m_fileStatOp, FILE_LINE_FUNC);
		m_fileStatOp = 0;
	}
*/

	if (m_fileReadOp && EngineComponents::GetFileSystem()->IsDone(m_fileReadOp))
	{
		EngineComponents::GetFileSystem()->ReleaseOpEx(m_fileReadOp, FILE_LINE_FUNC);
		m_fileReadOp = 0;
	}

	if (m_fileCloseOp && EngineComponents::GetFileSystem()->IsDone(m_fileCloseOp))
	{
		EngineComponents::GetFileSystem()->ReleaseOpEx(m_fileCloseOp, FILE_LINE_FUNC);
		m_fileCloseOp = 0;
		m_fileHandle = 0;
	}

	// only close if we have no outstanding ops
	if (m_fileHandle != 0 &&
		m_fileCloseOp == 0 &&
		//m_fileStatOp == 0 &&
		m_fileOpenOp == 0 &&
		m_fileReadOp == 0)
	{
		// Close sync
		EngineComponents::GetFileSystem()->CloseSync(m_fileHandle, FileSystem::kPriorityNow);
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamLoader::GracefulShutdown()
{
	ANIM_ASSERT(!m_used);
	ANIM_ASSERT(!m_fileReadOp);

	ReleaseCompletedOps();

	// only close if we have no outstanding ops
	if (m_fileHandle != 0 && 
		m_fileCloseOp == 0 && 
		//m_fileStatOp == 0 && 
		m_fileOpenOp == 0 && 
		m_fileReadOp == 0)
	{
		// Close async
		EngineComponents::GetFileSystem()->CloseAsyncEx(m_fileHandle, &m_fileCloseOp, FILE_LINE_FUNC);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamLoader::WaitTillDoneAndRelease(FileSystem::Operation &op)
{
	//PROFILE(IO, AnimStreamLoader_WaitTillDoneAndRelease);

	if (op)
	{
		while (!EngineComponents::GetFileSystem()->IsDone(op))
		{
			ndsys::Thread::SleepMilliSeconds(1);
		}

		EngineComponents::GetFileSystem()->ReleaseOpEx(op, FILE_LINE_FUNC);
		op = 0;
		m_lastReadSize = -1;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamLoader::ForcefulShutdown()
{
	{
		PROFILE(IO, ASL_Shutdown_Open);
		WaitTillDoneAndRelease(m_fileOpenOp);
	}
/*
	{
		PROFILE(IO, ASL_Shutdown_Stat);
		WaitTillDoneAndRelease(m_fileStatOp);
	}
*/
	{
		PROFILE(IO, ASL_Shutdown_Read);
		WaitTillDoneAndRelease(m_fileReadOp);
	}
	{
		PROFILE(IO, ASL_Shutdown_Close);
		WaitTillDoneAndRelease(m_fileCloseOp);
	}

	if (m_fileHandle)
	{
		// Close sync
		EngineComponents::GetFileSystem()->CloseSync(m_fileHandle);
	}

	m_lastReadSize = -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStreamLoader::IsUsed() const
{
	return m_used;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStreamLoader::IsActive() const
{
	return (m_fileCloseOp != 0 || m_fileHandle != 0 || m_fileReadOp != 0 /*|| m_fileStatOp != 0*/ || m_fileOpenOp != 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStreamLoader::IsOpen() const
{
	return m_fileHandle != 0 && m_fileOpenOp == 0 && m_fileCloseOp == 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStreamLoader::RequestOpen(const char* streamName, Err* pErr)
{
	*pErr = Err::kOK;
	ANIM_ASSERT(m_fileCloseOp == 0);
	ANIM_ASSERT(m_fileReadOp == 0);
	//ANIM_ASSERT(m_fileStatOp == 0);

	if (m_fileOpenOp != 0)
	{
		Err err;
		if (EngineComponents::GetFileSystem()->IsDone(m_fileOpenOp, &err))
		{
			MsgAnimStreamVerbose("Open Completed (%s)\n", err.GetMessageText());

			EngineComponents::GetFileSystem()->ReleaseOpEx(m_fileOpenOp, FILE_LINE_FUNC);
			m_fileOpenOp = 0;
			if (err.Failed())
			{
				MsgConScriptError("Anim stream error: %s (%s)\n", err.GetMessageText(), streamName);
				*pErr = err;
				return false;
			}

			// kick off our check of the file size
			//EngineComponents::GetFileSystem()->FstatAsyncEx(m_fileHandle, &m_stat, &m_fileStatOp, FILE_LINE_FUNC);
		}
		else
		{
			MsgAnimStream("Waiting to open %s\n", streamName);
		}
	}

	if (m_fileOpenOp == 0 && m_fileHandle == 0)
	{
		char filename[1024];
		GetAnimStreamFileName(streamName, filename);
		Err openErr = EngineComponents::GetFileSystem()->OpenAsyncEx(filename, &m_fileHandle, &m_fileOpenOp, FILE_LINE_FUNC, 0, FileSystem::kPriorityAnimstream);

		MsgAnimStreamVerbose("Open Started : %s (%s)\n", filename, openErr.GetMessageText());
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AnimStreamLoader::GetFileName() const
{
	const char* filePath = nullptr;
	Err err = EngineComponents::GetFileSystem()->GetPath(m_fileHandle, &filePath);
	return err.Succeeded() ? filePath : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// A pool of streamloaders, we get free loaders from the pool and put back ones we
// want to close
/// --------------------------------------------------------------------------------------------------------------- ///
AnimStreamLoader* StreamLoaderPool::GetNewStreamLoader()
{
	for (int i = 0; i < kMaxAnimStreamLoaders; i++)
	{
		if (!m_animStreamLoaders[i].IsUsed() && !m_animStreamLoaders[i].IsActive())
		{
			m_animStreamLoaders[i].m_used = true;
			return &m_animStreamLoaders[i];
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StreamLoaderPool::FreeStreamLoader(AnimStreamLoader* pStreamLoader)
{
	const char streamName[] = "";
	MsgAnimStreamVerbose("FreeStreamLoader Stream Loader [%016X]\n", (uintptr_t)pStreamLoader);

	pStreamLoader->m_used = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StreamLoaderPool::ForceCloseStreamLoader(AnimStreamLoader* pStreamLoader)
{
	const char streamName[] = "";
	MsgAnimStreamVerbose("ForceCloseStreamLoader Stream Loader [%016X]\n", (uintptr_t)pStreamLoader);

	pStreamLoader->ForcefulShutdown();
	pStreamLoader->m_used = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StreamLoaderPool::Update()
{
	//PROFILE(IO, StreamLoaderPool_Update);

	for (int i = 0; i < kMaxAnimStreamLoaders; i++)
	{
		AnimStreamLoader& loader = m_animStreamLoaders[i];
		if (!loader.IsUsed() && loader.IsActive())
		{
			loader.GracefulShutdown();
		}
	}
}
