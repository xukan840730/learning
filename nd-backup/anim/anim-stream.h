
/*
 * Copyright (c) 2012 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemAnim;
class AnimStreamLoader;
class AnimStream;

const int kMaxStreamingChunks = 3;

/// --------------------------------------------------------------------------------------------------------------- ///
// Read from the tools, don't change the data format unless you change to tools too!
struct StreamingChunk
{
	StreamingChunk()
	{
		m_chunkIndex = -1;
		m_phaseStart = 0.0f;
		m_phaseEnd = 0.0f;
		m_pInterleavedBlockBuffer = nullptr;
		m_ppArtItemAnim = nullptr;
	}

	I32 m_chunkIndex;
	float m_phaseStart;
	float m_phaseEnd;
	char* m_pInterleavedBlockBuffer;
	const ArtItemAnim** m_ppArtItemAnim;
};


/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimStreamChunkHeader
{
	U32 m_crc32;
	U32 m_pad[3];
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum
{
	kStreamingErrorNone = 0,
	kStreamingErrorSkip = 3,
};

/// --------------------------------------------------------------------------------------------------------------- ///
// Read from the tools, don't change the data format unless you change to tools too!
struct AnimStreamDef
{
	U32 m_numAnims;
	U32 m_framesPerBlock;
	U32 m_maxBlockSize;
	U32 m_numBlocks;

	const char* m_pStreamName;
	SkeletonId* m_pSkelIds;
	StringId64* m_pAnimNameIds;
	U32* m_pBlockSizes;
	mutable StringId64 m_streamNameId;		// Move to tools
};


/// --------------------------------------------------------------------------------------------------------------- ///
// Read from the tools, don't change the data format unless you change to tools too!
class AnimStream
{
public:
	static const int kMaxInactiveFrames = 5;

	AnimStream();

	void Reset();
	void Shutdown();

	void SetAnimStreamDef(const AnimStreamDef* pAnimStreamDef);
	const AnimStreamDef* GetAnimStreamDef() const { return m_pAnimStreamDef; }

	bool ValidatePhase(const float* pPhases, int numPhases);

	void GetStreamFileName(int index, char* filename);
	int GetNumStreams();
	void FreeStreamLoader();
	bool AcquireStreamLoader();
	void NotifyAnimTableUpdated();
	void Update(const float* pPhases, int numPhases);
	const ArtItemAnim* GetArtItemAnim(SkeletonId skelId, StringId64 animId, float phase);
	bool AttachAnim(SkeletonId skelId, StringId64 animNameId, const ArtItemAnim* pArtItemAnim);
	bool IsAnimAttached(SkeletonId skelId, StringId64 animId);
	const char* GetStreamName();
	bool OwnsChunkArtItem(const ArtItemAnim* pChunkArtItem);
	const ArtItemAnim* GetArtItemAnimForChunk(const ArtItemAnim* pChunkArtItem);
	float ChunkPhase(float animPhase);
	bool FirstChunkLoaded();

	I32F LookupSlotIndex(SkeletonId skelId, StringId64 animNameId) const;

	int GetLoadedChunks(int* pArray, int arraySize) const;

protected:
	I32 GetChunkIndex(float phase);
	void UnloadUnwantedChunks(const float* pPhases, int numPhases);
	int GetFirstLoadedBlockIndex();
	float GetFirstLoadedBlockStartPhase();
	void SetupChunkBufferPointers(U32 chunkIndex, U32 blockIndex);
	int GetExpectedStreamFileSize();
	int GetInterleavedBlockSize(int interleavedBlockIndex);
	int GetInterleavedBlockOffset(int interleavedBlockIndex);
	void RequestNextChunk(const float* pPhases, int numPhases);
	const ArtItemAnim* AnyValidHeader() const;

public:
	NdAtomicLock			m_lock;
	const ArtItemAnim**		m_pHeaderOnlyAnimArray;
	const AnimStreamDef*	m_pAnimStreamDef;
	int						m_numUsedChunks;
	StreamingChunk*			m_chunks;
	AnimStreamLoader*		m_pStreamLoader;
	U32						m_error;
	I32						m_requestedBlockIndex;
	I64						m_lastUsedOnFrame;
	I64						m_waitRenderFrameComplete;		// Don't stream new data until this render frame is complete (so we don't override anim processing deferred to render frame)
	I64						m_streamWorkDataSize;
};
