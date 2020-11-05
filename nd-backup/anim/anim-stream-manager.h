/*
 * Copyright (c) 2012 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/memory/bigmalloc.h"

#include "ndlib/anim/anim-stream.h"

class ArtItemAnim;
struct AnimStreamDef;

class ALIGNED(16) AnimStreamManager
{
public:
	static const int kMaxAnimStreams = 350;

private:
	int m_numStreams;
	I64 m_totalAllocatedWorkDataSize;
	AnimStream* m_pStreams[kMaxAnimStreams];

	MemoryPoolMalloc m_streamingBufferAllocator;

	struct ActiveStreamingAnim
	{
		I64 m_lastUpdatedFrame;
		const AnimStream* m_pStream;
		SkeletonId m_skelId;
		StringId64 m_animNameId;
		float m_phase;
		const ArtItemAnim* m_pHeaderOnlyAnim;
	};

	NdAtomic64 m_lock;
	int m_numActiveStreamingAnims[AnimStream::kMaxInactiveFrames];
	ActiveStreamingAnim m_activeStreamingAnims[AnimStream::kMaxInactiveFrames][kMaxAnimStreams];

	int GetUsedPhasesForStream(const AnimStream* pStream, float* pPhases, int kMaxPhases);

public:
	AnimStreamManager();
	void Initalize();
	void Shutdown();

	void NotifyAnimUsage(const ArtItemAnim* pArtItemAnim, StringId64 animNameId, float phase, I64 frameNumber);

	AnimStream* AllocateAnimStream(const ArtItemAnim* pArtItemAnim, StringId64 animNameId);
	AnimStream* GetAnimStream(const ArtItemAnim* pArtItemAnim, StringId64 animNameId);
	AnimStream* GetAnimStreamFromChunk(const ArtItemAnim* pChunkArtItem);
	
	float GetAnimStreamPhase(const ArtItemAnim* pArtItemAnim, StringId64 animNameId);

	void ResetAll();
	void UpdateAll();
	void NotifyAnimTableUpdated();
	AnimStream* RegisterStreamDef(const AnimStreamDef* pBuffer);	
	void UnregisterStreamDef(const AnimStreamDef* pBuffer);	

	// Globally visisble for now... :/
	char* AllocateStreamingBlockBuffer(int blockSize, StringId64 animStreamId);
	void FreeStreamingBlockBuffer(char* pBuffer);
};

