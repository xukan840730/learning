/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-stream-manager.h"

#include "corelib/memory/memory-map.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-stream-loader.h"
#include "ndlib/anim/anim-stream.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/frame-params.h"
#include "ndlib/nd-frame-state.h"

#include "gamelib/level/art-item-anim.h"

extern StreamLoaderPool* s_pStreamLoaderPool;

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
#define MsgConAnimStream(str, ...)                                                                                     \
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_debugStreaming))                                                          \
	{                                                                                                                  \
		const float curTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime().ToSeconds();     \
		MsgCon("[Streaming] [%0.2f] " str, curTime, __VA_ARGS__);                                                      \
	}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStreamManager::AnimStreamManager()
{
	m_numStreams = 0;
	m_totalAllocatedWorkDataSize = 0;

	m_lock.Set(0);
	for (int i = 0; i < AnimStream::kMaxInactiveFrames; ++i)
	{
		m_numActiveStreamingAnims[i] = 0;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamManager::NotifyAnimUsage(const ArtItemAnim* pArtItemAnim, StringId64 animNameId, float phase, I64 frameNumber)
{
	AnimStream* pStream = GetAnimStream(pArtItemAnim, animNameId);
	ANIM_ASSERT(pStream);

#ifndef FINAL_BUILD
	if (!EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
	{
		const char* streamName = pStream->GetStreamName();
		MsgAnimStreamVerbose("Notify Anim Usage: gameframe %lld, anim '%s' @ phase %.3f\n", frameNumber, pArtItemAnim->GetName(), phase);
	}
#endif

	if (!pStream->IsAnimAttached(pArtItemAnim->m_skelID, animNameId))
	{
		// We have not attached yet
		bool success = pStream->AttachAnim(pArtItemAnim->m_skelID, animNameId, pArtItemAnim);
		ANIM_ASSERT(success);
	}

	ANIM_ASSERT(pStream);

	pStream->m_lastUsedOnFrame = frameNumber;


	NdAtomic64Janitor jj(&m_lock);

	// First, let's check if we already have registered this animation this frame.
	// Animation segments will attempt to register the animation for each segment
	bool alreadyRegistered = false;
	for (int i = 0; i < m_numActiveStreamingAnims[frameNumber % AnimStream::kMaxInactiveFrames]; ++i)
	{
		const ActiveStreamingAnim& activeAnim = m_activeStreamingAnims[frameNumber % AnimStream::kMaxInactiveFrames][i];
		if (activeAnim.m_animNameId == animNameId &&
			activeAnim.m_phase == phase &&
			activeAnim.m_lastUpdatedFrame == frameNumber)
		{
			alreadyRegistered = true;
			break;
		}
	}

#ifndef FINAL_BUILD
	// Ensure that we aren't requesting different phases from the same stream
	static I64 lastFrameWePrinted = 0;
	if (lastFrameWePrinted != frameNumber)
	{
		for (int i = 0; i < m_numActiveStreamingAnims[frameNumber % AnimStream::kMaxInactiveFrames]; ++i)
		{
			const ActiveStreamingAnim& activeAnim = m_activeStreamingAnims[frameNumber % AnimStream::kMaxInactiveFrames][i];
			if (activeAnim.m_pStream == pStream &&
				activeAnim.m_lastUpdatedFrame == frameNumber &&
				!IsClose(activeAnim.m_phase, phase, 0.001f))
			{
				const char* streamName = pStream->GetStreamName();

				DoutBase* pMsgOutput = GetMsgOutput(kMsgConPauseable);
				pMsgOutput->SetForegroundColor(kTextColorRed);
				pMsgOutput->Printf("Out-of-sync phases in streaming animation: anim '%s' @ phase %.3f and '%s' @ phase %.3f\n",
					   pArtItemAnim->GetName(),
					   phase,
					   activeAnim.m_pHeaderOnlyAnim->GetName(),
					   activeAnim.m_phase);
				pMsgOutput->SetForegroundColor(kTextColorNormal);

				lastFrameWePrinted = frameNumber;
				break;
			}
		}
	}
#endif

	if (!alreadyRegistered)
	{
		ANIM_ASSERTF(m_numActiveStreamingAnims[frameNumber % AnimStream::kMaxInactiveFrames] < kMaxAnimStreams, ("Too many active streaming animations"));
		if (m_numActiveStreamingAnims[frameNumber % AnimStream::kMaxInactiveFrames] < kMaxAnimStreams)
		{
			I64 newEntry = m_numActiveStreamingAnims[frameNumber % AnimStream::kMaxInactiveFrames]++;
			ActiveStreamingAnim& activeAnim = m_activeStreamingAnims[frameNumber % AnimStream::kMaxInactiveFrames][newEntry];
			activeAnim.m_lastUpdatedFrame = frameNumber;
			activeAnim.m_pStream = pStream;
			activeAnim.m_skelId = pArtItemAnim->m_skelID;
			activeAnim.m_animNameId = animNameId;
			activeAnim.m_phase = phase;
			activeAnim.m_pHeaderOnlyAnim = pArtItemAnim;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStream* AnimStreamManager::AllocateAnimStream(const ArtItemAnim* pHeaderOnlyArtItemAnim, StringId64 animNameId)
{
	ANIM_ASSERT(pHeaderOnlyArtItemAnim);

	for (I32F iStream = 0; iStream < m_numStreams; ++iStream)
	{
		AnimStream* pCurStream = m_pStreams[iStream];
		I32F slotIndex = pCurStream->LookupSlotIndex(pHeaderOnlyArtItemAnim->m_skelID, animNameId);
		if (slotIndex >= 0)
		{
			if (pCurStream->AttachAnim(pHeaderOnlyArtItemAnim->m_skelID, animNameId, pHeaderOnlyArtItemAnim))
				return pCurStream;
			else
				return nullptr;
		}
	}

	ANIM_ASSERTF(false, ("An anim stream could not get allocated"));

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStream* AnimStreamManager::GetAnimStream(const ArtItemAnim* pArtItemAnim, StringId64 animNameId)
{
	ANIM_ASSERT(pArtItemAnim);

	for (I32F iStream = 0; iStream < m_numStreams; ++iStream)
	{
		AnimStream* pStream = m_pStreams[iStream];
		const I32F slotIndex = pStream->LookupSlotIndex(pArtItemAnim->m_skelID, pArtItemAnim->GetNameId());
		if (slotIndex >= 0)
			return pStream;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimStreamManager::GetAnimStreamPhase(const ArtItemAnim* pArtItemAnim, StringId64 animNameId)
{
	I64 currentFrameNumber = GetCurrentFrameNumber() - 1;
	if (currentFrameNumber < AnimStream::kMaxInactiveFrames)
		return 0.0f;

	for (int iFrame = 0; iFrame < AnimStream::kMaxInactiveFrames; ++iFrame, currentFrameNumber--)
	{
		for (int i = 0; i < m_numActiveStreamingAnims[currentFrameNumber % AnimStream::kMaxInactiveFrames]; ++i)
		{
			const ActiveStreamingAnim& activeAnim = m_activeStreamingAnims[currentFrameNumber
																		   % AnimStream::kMaxInactiveFrames][i];

			if (activeAnim.m_animNameId == animNameId && activeAnim.m_pHeaderOnlyAnim == pArtItemAnim)
			{
				return activeAnim.m_phase;
			}
		}
	}

	// HACK because the last remaining user of this function (as of this writing) uses it to get the start phase 
	// of a stream that might come from an existing bundle, but that specific stream has yet to be played, 
	// let's search for an existing stream pointer and use that as the start phase. The case being fixed here is
	// specifically the player facial layer using overlays to switch between streaming facial anims in the same bundle
	// and we want to use a proper start phase.
	if (pArtItemAnim)
	{
		currentFrameNumber = GetCurrentFrameNumber() - 1;
		const AnimStream* pExistingStream = GetAnimStream(pArtItemAnim, animNameId);

		for (int iFrame = 0; iFrame < AnimStream::kMaxInactiveFrames; ++iFrame, currentFrameNumber--)
		{
			for (int i = 0; i < m_numActiveStreamingAnims[currentFrameNumber % AnimStream::kMaxInactiveFrames]; ++i)
			{
				const ActiveStreamingAnim& activeAnim = m_activeStreamingAnims[currentFrameNumber
																			   % AnimStream::kMaxInactiveFrames][i];

				if (activeAnim.m_pStream == pExistingStream)
				{
					return activeAnim.m_phase;
				}
			}
		}
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStream* AnimStreamManager::GetAnimStreamFromChunk(const ArtItemAnim* pChunkArtItem)
{
	for (int i = 0; i < m_numStreams; i++)
	{
		if (m_pStreams[i]->OwnsChunkArtItem(pChunkArtItem))
			return m_pStreams[i];
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamManager::Initalize()
{
	const int kStreamBufferSize = Memory::GetSize(ALLOCATION_ANIM_STREAMING) - 512 * 1024;
	U8* pStreamBuffer = NDI_NEW (kAlign16) U8[kStreamBufferSize];
	m_streamingBufferAllocator.Init(pStreamBuffer, kStreamBufferSize);

	s_pStreamLoaderPool = NDI_NEW (kAlign16) StreamLoaderPool;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamManager::Shutdown()
{
	for (int i = 0; i < m_numStreams; i++)
	{
		m_pStreams[i]->Shutdown();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamManager::ResetAll()
{
	for (int i = 0; i < m_numStreams; i++)
	{
		m_pStreams[i]->Reset();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
int AnimStreamManager::GetUsedPhasesForStream(const AnimStream* pStream, float* pPhases, int kMaxPhases)
{
	I64 currentFrameNumber = GetCurrentFrameNumber() - 1;
	if (currentFrameNumber < AnimStream::kMaxInactiveFrames)
		return 0;

	int numStreamPhases = 0;
	for (int iFrame = 0; iFrame < AnimStream::kMaxInactiveFrames; ++iFrame, currentFrameNumber--)
	{
		for (int iAnim = 0; iAnim < m_numActiveStreamingAnims[currentFrameNumber % AnimStream::kMaxInactiveFrames]; ++iAnim)
		{
			const auto& usage = m_activeStreamingAnims[currentFrameNumber % AnimStream::kMaxInactiveFrames][iAnim];

			if (usage.m_pStream == pStream && usage.m_lastUpdatedFrame == currentFrameNumber)
			{
				// Check if it was already added
				bool alreadyAdded = false;
				for (int iPhase = 0; iPhase < numStreamPhases; ++iPhase)
				{
					if (pPhases[iPhase] == usage.m_phase)
					{
						alreadyAdded = true;
						break;
					}
				}
				if (!alreadyAdded)
				{
					ANIM_ASSERT(numStreamPhases < kMaxPhases);
					if (numStreamPhases < kMaxPhases)
						pPhases[numStreamPhases++] = usage.m_phase;
				}
			}
		}
	}

	return numStreamPhases;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamManager::UpdateAll()
{
	I64 frameNumber = GetCurrentFrameNumber() - 1;		// This update happens after we have already advanced the current frame number for RenderFrameParams. Fix post-ship. :/ - CGY

	NdAtomic64Janitor jj(&m_lock);

#if !FINAL_BUILD

	MsgConAnimStream("Num Registered Streaming Anims: %d\n", m_numStreams);
	MsgConAnimStream("Num Active Streaming Anims: %d\n", m_numActiveStreamingAnims[frameNumber % AnimStream::kMaxInactiveFrames]);
	MsgConAnimStream("Total Work Buffer Size: %d KiB\n", m_totalAllocatedWorkDataSize / 1024);

	static int maxStreamingBufferUsage = 0;
	int streamingBufferUsage = m_streamingBufferAllocator.GetAllocatedSize();
	if (maxStreamingBufferUsage < streamingBufferUsage)
		maxStreamingBufferUsage = streamingBufferUsage;
	MsgConAnimStream("Stream Buffer Mem Used: %d / %d KiB (Max: %d KiB)\n",
					 m_streamingBufferAllocator.GetAllocatedSize() / 1024,
					 m_streamingBufferAllocator.GetMemorySize() / 1024,
					 maxStreamingBufferUsage / 1024);

	for (int iStream = 0; iStream < m_numStreams; ++iStream)
	{
		const AnimStream* pStream = m_pStreams[iStream];

		const int kMaxPhasesPerStream = 15 /*kMaxSlotsPerStream*/ * AnimStream::kMaxInactiveFrames;
		float streamPhases[kMaxPhasesPerStream];
		int numPhases = GetUsedPhasesForStream(pStream, streamPhases, kMaxPhasesPerStream);
		if (!numPhases)
			continue;

		// Let's see if this stream is used this frame
		for (int slotIndex = 0; slotIndex < pStream->m_pAnimStreamDef->m_numAnims; ++slotIndex)
		{
			// Check if it was used this frame
			bool activeSlot = false;
			for (int iAnim = 0; iAnim < m_numActiveStreamingAnims[frameNumber % AnimStream::kMaxInactiveFrames]; ++iAnim)
			{
				const ActiveStreamingAnim& usage = m_activeStreamingAnims[frameNumber % AnimStream::kMaxInactiveFrames][iAnim];
				if (!usage.m_pStream || usage.m_pStream->m_pAnimStreamDef->m_pAnimNameIds[slotIndex] != usage.m_animNameId)
					continue;

				activeSlot = true;
				break;
			}

			const ArtItemAnim* pAnim = pStream->m_pHeaderOnlyAnimArray[slotIndex];
			if (pAnim)
			{
				int loadedChunks[3];
				int usedChunks = pStream->GetLoadedChunks(loadedChunks, 3);

				MsgConAnimStream("[%s] %s (slot %d), cur phase %.3f, cur chunk %d (loaded %d %2d %2d) - %s\n",
					pStream->m_pAnimStreamDef->m_pStreamName,
					DevKitOnly_StringIdToString(pAnim->GetNameId()),
					slotIndex,
					streamPhases[0],
					(U32)GetMayaFrameFromClip(pAnim->m_pClipData, streamPhases[0]) / pStream->m_pAnimStreamDef->m_framesPerBlock,
					usedChunks > 0 ? loadedChunks[0] : -1,
					usedChunks > 1 ? loadedChunks[1] : -1,
					usedChunks > 2 ? loadedChunks[2] : -1,
					activeSlot ? "active" : "inactive");
			}
		}
	}

	// Streaming stats
	for (int i = 0; i < kMaxAnimStreamLoaders; i++)
	{
		if (s_pStreamLoaderPool->m_animStreamLoaders[i].IsUsed())
		{
			const char* path = s_pStreamLoaderPool->m_animStreamLoaders[i].GetFileName();
			if (path)
			{
				MsgConAnimStream("Active Loader '%s'\n", path);
			}
		}
	}
#endif

	const int kMaxPhasesPerStream = 15 /*kMaxSlotsPerStream*/ * AnimStream::kMaxInactiveFrames;
	float streamPhases[kMaxPhasesPerStream];

	for (int i = 0; i < m_numStreams; i++)
	{
		AnimStream* pStream = m_pStreams[i];

		// Extract all phases used for this stream over the last k frames.
		// We need this to allow phases to be 'decaying' yet not unloaded.
		int numStreamPhases = GetUsedPhasesForStream(pStream, streamPhases, kMaxPhasesPerStream);

		pStream->Update(streamPhases, numStreamPhases);
	}

	m_numActiveStreamingAnims[(frameNumber + 1) % AnimStream::kMaxInactiveFrames] = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamManager::NotifyAnimTableUpdated()
{
	for (int i = 0; i < m_numStreams; i++)
	{
		m_pStreams[i]->NotifyAnimTableUpdated();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
char* AnimStreamManager::AllocateStreamingBlockBuffer(int blockSize, StringId64 animStreamId)
{
	int requiredBlockSize = blockSize + sizeof(AnimStreamChunkHeader);

	if (m_streamingBufferAllocator.CanAllocate(requiredBlockSize, kAlign16))
	{
#ifndef FINAL_BUILD
		m_streamingBufferAllocator.CheckConsistency();
#endif

		char* pMem = (char*) m_streamingBufferAllocator.Malloc(requiredBlockSize, kAlign16);

#ifndef FINAL_BUILD
		m_streamingBufferAllocator.CheckConsistency();
#endif
		return pMem;
	}

	ALWAYS_HALTF(("We ran out of stream buffer data. Too many anim streams are playing at once."));

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamManager::FreeStreamingBlockBuffer(char* pBuffer)
{
	if (!pBuffer)
		return;

	if (m_streamingBufferAllocator.IsValidPtr(pBuffer))
	{
#ifndef FINAL_BUILD
		m_streamingBufferAllocator.CheckConsistency();
#endif
		m_streamingBufferAllocator.Free((U8*)pBuffer);

#ifndef FINAL_BUILD
		m_streamingBufferAllocator.CheckConsistency();
#endif
	}
	else
	{
		ALWAYS_HALTF(("An invalid pointer was passed in to FreeStreamingBlockBuffer"));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStream* AnimStreamManager::RegisterStreamDef(const AnimStreamDef* pAnimStreamDef)
{
	I64 streamWorkDataSize = AlignSize(sizeof(AnimStream), kAlign16);
	streamWorkDataSize += AlignSize(sizeof(const ArtItemAnim*) * pAnimStreamDef->m_numAnims, kAlign16);
	streamWorkDataSize += AlignSize(sizeof(StreamingChunk) * kMaxStreamingChunks, kAlign16);
	streamWorkDataSize += AlignSize((sizeof(const ArtItemAnim*) * pAnimStreamDef->m_numAnims), kAlign16) * kMaxStreamingChunks;

	U8* pStreamWorkData = NDI_NEW(kAlign16) U8[streamWorkDataSize];		// Level heap allocation
	CustomAllocateJanitor jj(pStreamWorkData, streamWorkDataSize, FILE_LINE_FUNC);

	ANIM_ASSERT(m_numStreams < AnimStreamManager::kMaxAnimStreams);
	if (m_numStreams >= AnimStreamManager::kMaxAnimStreams)
		return nullptr;

	AnimStream* pNewAnimStream = NDI_NEW(kAlign16) AnimStream;
	pNewAnimStream->m_pHeaderOnlyAnimArray = NDI_NEW(kAlign16) const ArtItemAnim*[pAnimStreamDef->m_numAnims];
	pNewAnimStream->m_chunks = NDI_NEW(kAlign16) StreamingChunk[kMaxStreamingChunks];

	for (int i = 0; i < kMaxStreamingChunks; ++i)
	{
		pNewAnimStream->m_chunks[i].m_pInterleavedBlockBuffer = nullptr;
		pNewAnimStream->m_chunks[i].m_ppArtItemAnim = NDI_NEW(kAlign16) const ArtItemAnim*[pAnimStreamDef->m_numAnims];
		memset(pNewAnimStream->m_chunks[i].m_ppArtItemAnim, 0x55, sizeof(ArtItemAnim*) * pAnimStreamDef->m_numAnims);
	}
	pNewAnimStream->SetAnimStreamDef(pAnimStreamDef);
	pNewAnimStream->m_streamWorkDataSize = streamWorkDataSize;

	m_pStreams[m_numStreams++] = pNewAnimStream;
	m_totalAllocatedWorkDataSize += streamWorkDataSize;

	return m_pStreams[m_numStreams - 1];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStreamManager::UnregisterStreamDef(const AnimStreamDef* pStreamDef)
{
	// Find the stream
	for (int i = 0; i < m_numStreams; i++)
	{
		// Is it this stream?
		if (m_pStreams[i]->m_pAnimStreamDef == pStreamDef)
		{
			m_totalAllocatedWorkDataSize -= m_pStreams[i]->m_streamWorkDataSize;

			m_pStreams[i]->Reset();

			m_pStreams[i] = m_pStreams[m_numStreams - 1];
			m_numStreams--;
			return;
		}
	}

	ANIM_ASSERTF(false, ("Tried to free a stream that wasn't allocated.  Talk to a programmer."));
}

