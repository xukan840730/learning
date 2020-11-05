/*
 * Copyright (c) 2019 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/io/file-system.h"

const int kMaxAnimStreamLoaders = 10;

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimStreamLoader
{
private:
	friend class StreamLoaderPool;

	FileSystem::FileHandle m_fileHandle;
	FileSystem::Operation m_fileOpenOp;
	FileSystem::Operation m_fileCloseOp;
	FileSystem::Operation m_fileReadOp;
	//FileSystem::Operation m_fileStatOp;
	U64 m_issueReadTick;
	FileSystem::Stat m_stat;
	bool m_used;
	I32 m_lastReadSize;

	void ReleaseCompletedOps();
	void WaitTillDoneAndRelease(FileSystem::Operation &op);

public:
	AnimStreamLoader();

	void Reset();
	bool IsReading() const;
	//I64 GetFileSize();
	void Read(U8* pBuffer, int offset, int size, const char* streamName);
	bool WaitForRead(Err* pErr, const char* streamName);
	void GracefulShutdown();
	void ForcefulShutdown();
	bool IsUsed() const;
	bool IsActive() const;
	bool IsOpen() const;
	bool RequestOpen(const char* streamName, Err* pErr);
	const char* GetFileName() const;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// A pool of stream loaders, we get free loaders from the pool and put back ones we
// want to close
/// --------------------------------------------------------------------------------------------------------------- ///
class StreamLoaderPool
{
public:
	AnimStreamLoader m_animStreamLoaders[kMaxAnimStreamLoaders];

	AnimStreamLoader* GetNewStreamLoader();
	void FreeStreamLoader(AnimStreamLoader* pStreamLoader);
	void ForceCloseStreamLoader(AnimStreamLoader* pStreamLoader);
	void Update();
};

extern void GetAnimStreamFileName(const char* streamName, char* filename);
