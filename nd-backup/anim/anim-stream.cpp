/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-stream.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-stream.h"
#include "ndlib/anim/anim-stream-manager.h"
#include "ndlib/anim/anim-stream-loader.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/frame-params.h"
#include "ndlib/io/package-util.h"
#include "ndlib/io/pak-structs.h"
#include "ndlib/nd-frame-state.h"

#include "gamelib/level/art-item-anim.h"


/// --------------------------------------------------------------------------------------------------------------- ///
#define MsgAnimStream(str, ...)                                                                                        \
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_debugStreaming))                                                          \
	{                                                                                                                  \
		const float curTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime().ToSeconds();                \
		MsgAnim("[Streaming] [%0.2f] [%s] " str, curTime, streamName, __VA_ARGS__);                                    \
	}

/// --------------------------------------------------------------------------------------------------------------- ///
#define MsgConAnimStreamVerbose(str, ...)                                                                                     \
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_debugStreamingVerbose))                                                  \
	{                                                                                                                  \
		const float curTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime().ToSeconds();                \
		MsgCon("[Streaming] [%0.2f] " str, curTime, __VA_ARGS__);                                                      \
	}


static const U32 kStreamingBufferMaxChunks = 3;

extern StreamLoaderPool* s_pStreamLoaderPool;	// Temp hack


 /// --------------------------------------------------------------------------------------------------------------- ///
AnimStream::AnimStream()
{
	m_pHeaderOnlyAnimArray = nullptr;
	m_pAnimStreamDef = nullptr;
	m_chunks = nullptr;
	m_pStreamLoader = nullptr;
	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStream::Reset()
{
	// We are forceully resetting the stream. If everything is already waited for and closed down, then it will
	// be a quick process. If reads are outstanding, then we will wait for those before returning which could cause
	// a hitch.
	if (m_pStreamLoader)
	{
		m_pStreamLoader->ForcefulShutdown();
		ANIM_ASSERT(!m_pStreamLoader->IsReading());
		FreeStreamLoader();
	}

	if (m_pAnimStreamDef)
	{
		for (U32F iSlot = 0; iSlot < m_pAnimStreamDef->m_numAnims; ++iSlot)
		{
			m_pHeaderOnlyAnimArray[iSlot] = nullptr;
		}

		for (int chunkIndex = 0; chunkIndex < kMaxStreamingChunks; ++chunkIndex)
		{
			EngineComponents::GetAnimStreamManager()->FreeStreamingBlockBuffer(m_chunks[chunkIndex].m_pInterleavedBlockBuffer);
			m_chunks[chunkIndex].m_pInterleavedBlockBuffer = nullptr;

			for (I32F i = 0; i < m_pAnimStreamDef->m_numAnims; i++)
			{
				m_chunks[chunkIndex].m_ppArtItemAnim[i] = nullptr;
			}
			m_chunks[chunkIndex].m_chunkIndex = -1;
			m_chunks[chunkIndex].m_phaseStart = 0.0f;
			m_chunks[chunkIndex].m_phaseEnd = 0.0f;
		}
	}

	m_lock.m_atomic.Set(0);
	m_numUsedChunks = 1;
	m_requestedBlockIndex = -1;
	m_lastUsedOnFrame = m_waitRenderFrameComplete = GetCurrentFrameNumber();
	m_error = kStreamingErrorNone;
	m_streamWorkDataSize = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStream::SetAnimStreamDef(const AnimStreamDef* pAnimStreamDef)
{
	m_pAnimStreamDef = pAnimStreamDef;
	for (U32F iSlot = 0; iSlot < m_pAnimStreamDef->m_numAnims; ++iSlot)
	{
		m_pHeaderOnlyAnimArray[iSlot] = nullptr;
	}
	m_lastUsedOnFrame = 0;
	m_waitRenderFrameComplete = -1;
	m_pStreamLoader = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStream::GetStreamFileName(int index, char* filename)
{
	GetAnimStreamFileName(m_pAnimStreamDef->m_pStreamName, filename);
}

/// --------------------------------------------------------------------------------------------------------------- ///
int AnimStream::GetNumStreams()
{
	return 1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStream::Shutdown()
{
	if (m_pStreamLoader != nullptr)
	{
		s_pStreamLoaderPool->ForceCloseStreamLoader(m_pStreamLoader);
		m_pStreamLoader = nullptr;
	}

	for (U32F iSlot = 0; iSlot < m_pAnimStreamDef->m_numAnims; ++iSlot)
	{
		m_pHeaderOnlyAnimArray[iSlot] = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStream::FreeStreamLoader()
{
	const char streamName[] = "";
	MsgConAnimStreamVerbose("Freeing Stream Loader [%016X]\n", (uintptr_t)m_pStreamLoader);

	if (m_pStreamLoader != nullptr)
	{
		s_pStreamLoaderPool->FreeStreamLoader(m_pStreamLoader);
		m_pStreamLoader = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStream::IsAnimAttached(SkeletonId skelId, StringId64 animId)
{
	const I32F slotIndex = LookupSlotIndex(skelId, animId);
	if (slotIndex < 0)
		return false;

	if (m_pHeaderOnlyAnimArray[slotIndex])
	{
		const uintptr_t artItemAnimPtr = (uintptr_t)m_chunks[0].m_ppArtItemAnim[slotIndex];
		if (artItemAnimPtr
 			&& artItemAnimPtr != 0x5555555555555555ULL
 			&& artItemAnimPtr != 0x3333333333333333ULL)
			return true;
	}

	return false;
}


/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStream::AcquireStreamLoader()
{
	ANIM_ASSERT(m_pStreamLoader == nullptr);
	m_pStreamLoader = s_pStreamLoaderPool->GetNewStreamLoader();

	const char streamName[] = "";
	MsgConAnimStreamVerbose("Acquired Stream Loader [%016X]\n", (uintptr_t)m_pStreamLoader);

	return !!m_pStreamLoader;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStream::AttachAnim(SkeletonId skelId, StringId64 animNameId, const ArtItemAnim* pArtItemAnim)
{
	AtomicLockJanitor accessLock(&m_lock, FILE_LINE_FUNC);

	// iterate once to determine which stream we belong to
	const I32F slotIndex = LookupSlotIndex(skelId, animNameId);
	ANIM_ASSERT(slotIndex != -1);

	// now attach us to this stream

	// the header just needs to be a header that one of a particular group of streaming animations uses,
	// for instance, it can be any of the animations in a single igc, but not a header for a different igc
	// since what's important about the header is the frame ranges, and for any particular igcs, those
	// are *required* to match
	//
	// the only exception is LoadFirstChunk which looks up animations using the skel and hierarchyid from the header
	// so we set it right before we call
	ANIM_ASSERT(pArtItemAnim);

	m_lastUsedOnFrame = GetCurrentFrameNumber();
	m_waitRenderFrameComplete = GetCurrentFrameNumber();

	// now load the first chunk for this slot
	const ArtItemAnim* pHeader = pArtItemAnim;
	ANIM_ASSERT(pHeader);

	// Special load of first block
	const StringId64 animIdChunkName = StringId64Concat(m_pAnimStreamDef->m_pAnimNameIds[slotIndex], "-chunk-0");
	const ArtItemAnim* pFirstChunkArtItemAnim = AnimMasterTable::LookupAnim(pHeader->m_skelID, pHeader->m_pClipData->m_animHierarchyId, animIdChunkName).ToArtItem();
	ANIM_ASSERT(pFirstChunkArtItemAnim);

	m_chunks[0].m_chunkIndex = 0;
	m_chunks[0].m_phaseStart = 0.0f;
	m_chunks[0].m_phaseEnd = Min(1.0f, GetMayaFrameFromClip(pFirstChunkArtItemAnim->m_pClipData, 1.0f) / GetMayaFrameFromClip(pHeader->m_pClipData, 1.0f));
	m_chunks[0].m_pInterleavedBlockBuffer = nullptr;
	m_chunks[0].m_ppArtItemAnim[slotIndex] = pFirstChunkArtItemAnim;

	if (m_chunks[0].m_phaseEnd == 1.0f)
		m_chunks[0].m_phaseEnd = 1.0001f;

	// This registers the animation as 'attached'
	m_pHeaderOnlyAnimArray[slotIndex] = pArtItemAnim;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStream::NotifyAnimTableUpdated()
{
	AtomicLockJanitor accessLock(&m_lock, FILE_LINE_FUNC);

	for (U32F iSlot = 0; iSlot < m_pAnimStreamDef->m_numAnims; ++iSlot)
	{
		m_pHeaderOnlyAnimArray[iSlot] = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStream::Update(const float* pPhases, int numPhases)
{
	const ArtItemAnim* pHeader = AnyValidHeader();
	if (nullptr == pHeader)
	{
		// our animation just got unloaded, but the stream still exists, skip this frame, we'll get a new header next frame
		// note that this only occurs if two streams with the same name are loaded simultaneously
		return;
	}

	if (EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
	{
		m_lastUsedOnFrame = GetCurrentFrameNumber();
	}
	else if ((!m_pStreamLoader || !m_pStreamLoader->IsReading()) && 
			HasFrameRetired(m_lastUsedOnFrame + kMaxInactiveFrames) &&
			GetLastPreparedFrameNumber() > m_lastUsedOnFrame + kMaxInactiveFrames + 1)
	{
		Reset();
		return;
	}

	if (!numPhases)		// For now - FIX ME
		return;

	// request streaming for the given phase
	UnloadUnwantedChunks(pPhases, numPhases);
	RequestNextChunk(pPhases, numPhases);

	if (!ValidatePhase(pPhases, numPhases))
	{
		const char* streamName = pHeader->GetName();
		float desiredPhase = pPhases[0];
		MsgAnimStream("Anim stream fell behind! (desiredPhase = %f [chunk: %d]) [chunks: %d %d %d]\n",
					  desiredPhase,
					  GetChunkIndex(desiredPhase),
					  m_chunks[0].m_chunkIndex,
					  m_chunks[1].m_chunkIndex,
					  m_chunks[2].m_chunkIndex);
		MsgConScriptError("Anim stream %s fell behind!\n", pHeader->GetName());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemAnim* AnimStream::GetArtItemAnim(SkeletonId skelId, StringId64 animId, float phase)
{
	const I32F slotIndex = LookupSlotIndex(skelId, animId);
	if (slotIndex < 0)
		return nullptr;

	if (!m_pHeaderOnlyAnimArray[slotIndex])
		return nullptr;

	// return the correct artItem anim
	for (int i = 0; i < m_numUsedChunks; i++)
	{
		if (m_chunks[i].m_phaseStart <= phase &&
			phase < m_chunks[i].m_phaseEnd)
		{
			ANIM_ASSERT((uintptr_t)m_chunks[i].m_ppArtItemAnim[slotIndex] != 0x5555555555555555ULL
						&& (uintptr_t)m_chunks[i].m_ppArtItemAnim[slotIndex] != 0x3333333333333333ULL);
			return m_chunks[i].m_ppArtItemAnim[slotIndex];
		}
	}

	// couldn't find the right chunk, return the first chunk instead so we don't crash
	if (m_numUsedChunks > 0)
	{
		ANIM_ASSERT((uintptr_t)m_chunks[0].m_ppArtItemAnim[slotIndex] != 0x5555555555555555ULL
					&& (uintptr_t)m_chunks[0].m_ppArtItemAnim[slotIndex] != 0x3333333333333333ULL);
		return m_chunks[0].m_ppArtItemAnim[slotIndex];
	}

	ANIM_ASSERTF(false, ("For some reason we were not able to find an animation for this anim stream. That is bad."));
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AnimStream::GetStreamName()
{
	return (const char*)m_pAnimStreamDef->m_pStreamName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStream::OwnsChunkArtItem(const ArtItemAnim* pChunkArtItem)
{
	for (int i = 0; i < m_numUsedChunks; i++)
	{
		for (int j = 0; j < m_pAnimStreamDef->m_numAnims; j++)
		{
			if (m_chunks[i].m_ppArtItemAnim[j] == pChunkArtItem)
				return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemAnim* AnimStream::GetArtItemAnimForChunk(const ArtItemAnim* pChunkArtItem)
{
	for (int i = 0; i < m_numUsedChunks; i++)
	{
		for (int j = 0; j < m_pAnimStreamDef->m_numAnims; j++)
		{
			if (m_chunks[i].m_ppArtItemAnim[j] == pChunkArtItem)
			{
				const ArtItemAnim* pArtItemAnim = AnimMasterTable::LookupAnim(pChunkArtItem->m_skelID, pChunkArtItem->m_pClipData->m_animHierarchyId, m_pAnimStreamDef->m_pAnimNameIds[j]).ToArtItem();
				return pArtItemAnim;
			}
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimStream::ChunkPhase(float animPhase)
{
	float chunkPhase = 0.0f;

	for (I32F i = 0; i < m_numUsedChunks; ++i)
	{
		if (m_chunks[i].m_phaseStart <= animPhase &&
			animPhase < m_chunks[i].m_phaseEnd)
		{
			const float offset = animPhase - m_chunks[i].m_phaseStart;
			const float chunkRange = m_chunks[i].m_phaseEnd - m_chunks[i].m_phaseStart;

			chunkPhase = offset / chunkRange;

			break;
		}
	}

	return chunkPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStream::FirstChunkLoaded()
{
	return GetFirstLoadedBlockIndex() != -1;
}


I32F AnimStream::LookupSlotIndex(SkeletonId skelId, StringId64 animNameId) const
{
	ANIM_ASSERT(m_pAnimStreamDef);
	ANIM_ASSERT(m_pAnimStreamDef->m_pAnimNameIds);
	ANIM_ASSERT(m_pAnimStreamDef->m_numAnims);

	I32F foundSlot = -1;
	I32F foundStreamIndex = -1;

	const U32F numAnims = m_pAnimStreamDef->m_numAnims;

	for (U32F iAnim = 0; iAnim < numAnims; ++iAnim)
	{
		if (m_pAnimStreamDef->m_pSkelIds[iAnim] == skelId &&
			m_pAnimStreamDef->m_pAnimNameIds[iAnim] == animNameId)
		{
			foundSlot = iAnim;
			foundStreamIndex = iAnim;
			break;
		}
	}

	return foundSlot;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int AnimStream::GetLoadedChunks(int* pArray, int arraySize) const
{
	int valuesInArray = 0;
	for (int i = 0; i < m_numUsedChunks && valuesInArray < arraySize; ++i)
	{
		pArray[valuesInArray++] = m_chunks[i].m_chunkIndex;
	}
	return valuesInArray;
}


/// --------------------------------------------------------------------------------------------------------------- ///
I32 AnimStream::GetChunkIndex(float phase)
{
	// which chunk id is our phase in?
	const ArtItemAnim* pHeader = AnyValidHeader();

	// we can look at header 0 for this calculation since every anim in the stream is *required* to be the same length
	U32 frameIndex = (U32)(phase * GetMayaFrameFromClip(pHeader->m_pClipData, 1.0f));
	U32 chunkIndex = frameIndex / m_pAnimStreamDef->m_framesPerBlock;
	return chunkIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStream::UnloadUnwantedChunks(const float* pPhases, int numPhases)
{
	// Don't rearrange chunks while we are in the process of filling the buffers
	if (m_pStreamLoader && m_pStreamLoader->IsReading())
		return;

	// which chunk id is our phase in?
	I32 firstLoadedBlockIndex = GetFirstLoadedBlockIndex();
	float firstLoadedBlockStartPhase = GetFirstLoadedBlockStartPhase();

	// never free the first chunk, since it's already in memory anyway and we don't stream it
	for (I32F i = 1; i < m_numUsedChunks; ++i)
	{
		bool canUnloadChunk = true;
		for (int iPhase = 0; iPhase < numPhases; iPhase++)
		{
			I32 usedChunkIndex = GetChunkIndex(pPhases[iPhase]);
			if (m_chunks[i].m_chunkIndex == usedChunkIndex ||
				m_chunks[i].m_chunkIndex == usedChunkIndex + 1)
				canUnloadChunk = false;
		}

		if (canUnloadChunk)
		{
			const char* streamName = m_pAnimStreamDef->m_pStreamName;

			MsgConAnimStreamVerbose("Unloading chunk %d [phase: %0.3f -> %0.3f] [chunkIndex: %d] [first-loaded: %d @ %0.3f]\n",
				(int)i, m_chunks[i].m_phaseStart, m_chunks[i].m_phaseEnd, m_chunks[i].m_chunkIndex, firstLoadedBlockIndex, firstLoadedBlockStartPhase);

			// free this chunk, we're past this phase or it's too far past what we want
			const ArtItemAnim** ppSavedArtItemAnimArray = m_chunks[i].m_ppArtItemAnim;
			char* pSavedBuffer = m_chunks[i].m_pInterleavedBlockBuffer;

			m_chunks[i] = m_chunks[m_numUsedChunks - 1];

			m_chunks[m_numUsedChunks - 1].m_ppArtItemAnim = ppSavedArtItemAnimArray;
			m_chunks[m_numUsedChunks - 1].m_pInterleavedBlockBuffer = pSavedBuffer;

			// To ease debugging, clear the other fields
			m_chunks[m_numUsedChunks - 1].m_chunkIndex = -1;
			m_chunks[m_numUsedChunks - 1].m_phaseStart = 0.0f;
			m_chunks[m_numUsedChunks - 1].m_phaseEnd = 0.0f;
			for (int j = 0; j < m_pAnimStreamDef->m_numAnims; ++j)
			{
				m_chunks[m_numUsedChunks - 1].m_ppArtItemAnim[j] = nullptr;
			}
			//SS: This is still in use by the deferred anim commands, don't wipe out this memory yet!
			//memset(m_chunks[m_numUsedChunks - 1].m_pInterleavedBlockBuffer, 0x33, m_pAnimStreamDef->m_maxBlockSize);

			m_numUsedChunks--;
			i--;

			m_waitRenderFrameComplete = GetCurrentFrameNumber();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
int AnimStream::GetFirstLoadedBlockIndex()
{
	int firstLoadedChunkIndex = -1;
	for (int i = 1; i < m_numUsedChunks; i++)
	{
		if (m_chunks[i].m_chunkIndex < firstLoadedChunkIndex || firstLoadedChunkIndex == -1)
		{
			firstLoadedChunkIndex = m_chunks[i].m_chunkIndex;
		}
	}

	return firstLoadedChunkIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimStream::GetFirstLoadedBlockStartPhase()
{
	float firstLoadedBlockStartPhase = 0.0f;
	int firstLoadedChunkIndex = -1;
	for (int i = 1; i < m_numUsedChunks; i++)
	{
		if (m_chunks[i].m_chunkIndex < firstLoadedChunkIndex || firstLoadedChunkIndex == -1)
		{
			firstLoadedChunkIndex = m_chunks[i].m_chunkIndex;
			firstLoadedBlockStartPhase = m_chunks[i].m_phaseStart;
		}
	}

	return firstLoadedBlockStartPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStream::SetupChunkBufferPointers(U32 chunkIndex, U32 blockIndex)
{
	// set the chunks array of ArtItemAnim pointers to the appropriate places in the chunk

	// offset by sizeof(AnimStreamChunkHeader) to account for the chunk header
	U32 offset = sizeof(AnimStreamChunkHeader);
	for (int i = 0; i < m_pAnimStreamDef->m_numAnims; i++)
	{
		m_chunks[chunkIndex].m_ppArtItemAnim[i] = (const ArtItemAnim*)(m_chunks[chunkIndex].m_pInterleavedBlockBuffer + offset);
		offset += m_pAnimStreamDef->m_pBlockSizes[blockIndex * m_pAnimStreamDef->m_numAnims + i];
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
int AnimStream::GetExpectedStreamFileSize()
{
	int size = 0;
	for (int i = 0; i < m_pAnimStreamDef->m_numBlocks; i++)
	{
		size += GetInterleavedBlockSize(i + 1);
	}

	return size;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int AnimStream::GetInterleavedBlockSize(int animBlockIndex)
{
	int totalInterleavedBlockSize = 0;
	for (int i = 0; i < m_pAnimStreamDef->m_numAnims; i++)
	{
		totalInterleavedBlockSize += m_pAnimStreamDef->m_pBlockSizes[animBlockIndex * m_pAnimStreamDef->m_numAnims + i];
	}

	return totalInterleavedBlockSize;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int AnimStream::GetInterleavedBlockOffset(int interleavedBlockIndex)
{
	int interleavedBlockOffset = 0;
	for (int i = 0; i < interleavedBlockIndex; i++)
	{
		interleavedBlockOffset += GetInterleavedBlockSize(i);
	}

	return interleavedBlockOffset;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStream::RequestNextChunk(const float* pPhases, int numPhases)
{
	const int lastChunkIndex = (m_pAnimStreamDef->m_numBlocks / m_pAnimStreamDef->m_numAnims);

	const char* streamName = m_pAnimStreamDef->m_pStreamName;

	if (m_pStreamLoader && m_pStreamLoader->IsReading())
	{
		// Check if a previous read request has completed
		Err err = Err::kOK;
		const bool readCompleted = m_pStreamLoader->WaitForRead(&err, streamName);
		if (readCompleted)
		{
			// our chunk got loaded
			const U64 kMaxItemsPerChunk = 100;
			const ResItem* resArray[kMaxItemsPerChunk];
			U64 numEntries = 0;

			const ArtItemAnim* pArtItemAnim = nullptr;

			const int interleavedBlockSize = GetInterleavedBlockSize(m_requestedBlockIndex);
			const int interleavedBlockOffset = GetInterleavedBlockOffset(m_requestedBlockIndex);

			// we point the chunk artitemAnim pointers to the start of each pack buffer first, then we log in that pack file, then repoint the ArtItemAnim
			const int newlyLoadedChunkIndex = m_numUsedChunks;
			ANIM_ASSERT(newlyLoadedChunkIndex >= 1);

			SetupChunkBufferPointers(newlyLoadedChunkIndex, m_requestedBlockIndex);
			for (int i = 0; i < m_pAnimStreamDef->m_numAnims; i++)
			{
				const void* pPackageData = (const void*)m_chunks[newlyLoadedChunkIndex].m_ppArtItemAnim[i];

				STATIC_ASSERT(sizeof(AnimStreamChunkHeader) == 16);	// Tools assume it's 16 bytes!
				const AnimStreamChunkHeader* pAnimStreamChunkHeader = (const AnimStreamChunkHeader*)((const U8*)pPackageData - sizeof(AnimStreamChunkHeader));

				PreparePackageAndFillItemTable(const_cast<void*>(pPackageData), interleavedBlockSize, resArray, kMaxItemsPerChunk, &numEntries);

				// search our items for an ArtItemAnim
				pArtItemAnim = nullptr;
				for (int j = 0; j < numEntries; j++)
				{
					if (resArray[j]->GetTypeId() == SID("ANIM"))
					{
						pArtItemAnim = (const ArtItemAnim*)resArray[j];
					}
				}

				ANIM_ASSERTF(pArtItemAnim != nullptr, ("Anim stream: %s\n", m_pAnimStreamDef->m_pStreamName));
				m_chunks[newlyLoadedChunkIndex].m_ppArtItemAnim[i] = pArtItemAnim;
			}

			// in the code below, since we are just considering frame ranges, any ArtItemAnim will do, since streaming igcs are *required* to be the same length exactly
			const int readChunkIndex = m_requestedBlockIndex + 1;
			const ArtItemAnim* pHeader = AnyValidHeader();
			const float totalFramesInStream = GetMayaFrameFromClip(pHeader->m_pClipData, 1.0f);
			const float startFrameInStream = (float)(readChunkIndex) * (float)m_pAnimStreamDef->m_framesPerBlock;
			const float totalFramesInChunk = GetMayaFrameFromClip(pArtItemAnim->m_pClipData, 1.0f);
			m_chunks[newlyLoadedChunkIndex].m_chunkIndex = readChunkIndex;
			// IMPORTANT: We must calculate this in such a way that the next chunk's start phase matches this chunk's end phase EXACTLY.
			//            We accomplish this by doing the numerator math for start and end first (all of which is really integral), and
			//            then dividing each by the same denominator.
			m_chunks[newlyLoadedChunkIndex].m_phaseStart = startFrameInStream / totalFramesInStream;
			m_chunks[newlyLoadedChunkIndex].m_phaseEnd = Min(1.0f, (startFrameInStream + totalFramesInChunk) / totalFramesInStream);
			if (m_chunks[newlyLoadedChunkIndex].m_phaseEnd == 1.0f)
				m_chunks[newlyLoadedChunkIndex].m_phaseEnd = 1.0001f;

			m_numUsedChunks++;
			m_requestedBlockIndex = -1;

			MsgConAnimStreamVerbose("Read Completed of chunk slot %d(chunk index %d) [phase: %0.3f - %0.3f] (%d bytes, offset %d) (%s)\n",
				newlyLoadedChunkIndex,
				readChunkIndex,
				m_chunks[newlyLoadedChunkIndex].m_phaseStart,
				m_chunks[newlyLoadedChunkIndex].m_phaseEnd,
				interleavedBlockSize,
				interleavedBlockOffset,
				err.GetMessageText());
		}
	}
	else
	{
		// Can we shutdown streaming?
		for (int iPhase = 0; iPhase < numPhases; ++iPhase)
		{
			const float phase = pPhases[iPhase];
			const I32 chunkIndex = GetChunkIndex(phase);

			for (int iChunk = 1; iChunk < m_numUsedChunks; ++iChunk)
			{
				// Is the last chunk already loaded
				if (chunkIndex == lastChunkIndex &&
					m_chunks[iChunk].m_chunkIndex == lastChunkIndex)
				{
					if (m_pStreamLoader)
					{
						FreeStreamLoader();
					}
					return;
				}
			}
		}

		// try and load a chunk, unless we're waiting for a render frame to finish
		bool allowedToStream = true;
		I64 frameDiff = GetCurrentFrameNumber() - m_waitRenderFrameComplete;
		if (frameDiff < kMaxNumFramesInFlight)
		{
			RenderFrameParams* pParams = GetRenderFrameParams(m_waitRenderFrameComplete);
			if (pParams->m_renderFrameEndTick == 0)
				allowedToStream = false;
		}

		if (!m_pStreamLoader)
		{
			bool success = AcquireStreamLoader();
			if (!success)
			{
				MsgAnimStream("Could not acquire a new stream loader. Too many streams are playing at once.\n", 0);
				return;
			}
		}

		// open the file if necessary
		if (!m_pStreamLoader->IsOpen())
		{
			Err err;
			if (!m_pStreamLoader->RequestOpen(m_pAnimStreamDef->m_pStreamName, &err))
			{
				FreeStreamLoader();
			}
		}
		else
		{
			// Ensure that the streaming blocks have been allocated
			// Allocate streaming blocks but skip the first 'static' one
			for (int i = 1; i < kMaxStreamingChunks; ++i)
			{
				if (!m_chunks[i].m_pInterleavedBlockBuffer)
				{
					m_chunks[i].m_pInterleavedBlockBuffer = EngineComponents::GetAnimStreamManager()
						->AllocateStreamingBlockBuffer(m_pAnimStreamDef->m_maxBlockSize,
							m_pAnimStreamDef->m_streamNameId);
				}
			}

			// do we still have room to load chunks?
			if (allowedToStream &&
				m_numUsedChunks < kStreamingBufferMaxChunks)
			{
				// Figure out which block we want to load next
				int wantedChunkIndex = -1;
				for (int iPhase = 0; iPhase < numPhases; ++iPhase)
				{
					const float usedPhase = pPhases[iPhase];
					const I32 usedChunkIndex = GetChunkIndex(usedPhase);
					bool usedChunkIsLoaded = false;
					bool followingChunkIsLoaded = false;
					for (int iChunk = 0; iChunk < m_numUsedChunks; ++iChunk)
					{
						if (m_chunks[iChunk].m_chunkIndex == usedChunkIndex)
							usedChunkIsLoaded = true;
						if (m_chunks[iChunk].m_chunkIndex == usedChunkIndex + 1)
							followingChunkIsLoaded = true;
					}
					if (!usedChunkIsLoaded)
					{
						wantedChunkIndex = usedChunkIndex;
						break;
					}
					if (!followingChunkIsLoaded)
					{
						wantedChunkIndex = usedChunkIndex + 1;
						break;
					}
				}
				if (wantedChunkIndex == -1 || wantedChunkIndex <= 0 || wantedChunkIndex > lastChunkIndex)
					return;

				// get chunk size
				const int wantedBlockIndex = wantedChunkIndex - 1;
				const int interleavedBlockSize = GetInterleavedBlockSize(wantedBlockIndex);
				const int interleavedBlockOffset = GetInterleavedBlockOffset(wantedBlockIndex);

				m_pStreamLoader->Read((U8*)m_chunks[m_numUsedChunks].m_pInterleavedBlockBuffer,
					interleavedBlockOffset,
					interleavedBlockSize,
					m_pAnimStreamDef->m_pStreamName);

				// Remember which block we requested
				m_requestedBlockIndex = wantedBlockIndex;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemAnim* AnimStream::AnyValidHeader() const
{
	for (U32F iSlot = 0; iSlot < m_pAnimStreamDef->m_numAnims; ++iSlot)
	{
		if (m_pHeaderOnlyAnimArray[iSlot])
			return m_pHeaderOnlyAnimArray[iSlot];
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStream::ValidatePhase(const float* pPhases, int numPhases)
{
	for (int iPhase = 0; iPhase < numPhases; ++iPhase)
	{
		float phase = pPhases[iPhase];

		bool phaseIsContainedInAChunk = false;
		for (I32F i = 0; i < m_numUsedChunks; ++i)
		{
			if (m_chunks[i].m_phaseStart <= phase && phase < m_chunks[i].m_phaseEnd)
			{
				phaseIsContainedInAChunk = true;
				break;
			}
		}
		if (!phaseIsContainedInAChunk)
			return false;
	}

	return true;
}

