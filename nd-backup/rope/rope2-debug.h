/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef NDPHYS_ROPE2_DEBUG_H 
#define NDPHYS_ROPE2_DEBUG_H 

#include "corelib/system/recursive-atomic-lock.h"
#include "gamelib/ndphys/rope/rope2.h"

#if !FINAL_BUILD || ALLOW_ROPE_DEBUGGER_IN_FINAL

class HavokMeshDrawData;

struct RBHandleTranslation
{
	U32F m_bodyId;
	RigidBodyHandle m_handle; 
};

class Rope2Debugger
{
public:
	enum {
		kMaxColliders = 100
	};

	Rope2Debugger() {};
	~Rope2Debugger();

	bool Init(Rope2* pRope);
	void Reset(Rope2* pRope);
	Rope2* GetRope() { return m_pRope; }
	void UpdatePaused();
	void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);
	void SetExternBuffer(U8* pBuf, U32F bufSize, U32F bufPos);
	void SetRBHandleTranslations(const RBHandleTranslation* pTrans, U32F numTrans);

	void Save();
	bool Restore(U32F numFramesBack, bool bInputOnly = false);

	void RestoreColliders();
	void OverwriteColliders();
	void DebugDrawColliders();

	bool GetBufferOverrun() const { return m_bufferOverrun; }

private:
	void CopyData(U32F& bufPos, bool bInputOnly = false, bool bOverwriteColliders = false);
	void CopyBufWithTemp(U8* pData, U8* pTemp, U32F size, U32F& bufPos, bool bInputOnly);
	void CopyBuf(U8* pData, U32F size, U32F& bufPos, bool bSkip = false);
	void SaveToBuf(const U8* pData, U32F size, U32F& bufPos);
	void RestoreFromBuf(U8* pData, U32F size, U32F& bufPos, bool bSkip = false);

	Rope2* m_pRope;

	RopeColliderHandle* m_pColliderHandles; 
	Locator* m_pColliderLoc;
	Locator* m_pColliderLocPrev;
	U32F m_numColliders;
	U32F m_numOverwrittenColliders;
	F32 m_lastInvDt;

	U8* m_pBuf;
	U32F m_bufSize;
	U32F m_bufPos;

	bool m_restoring;
	U32F m_startPosCheck;
	bool m_bufferOverrun;
	U32F m_frameBackNum;
	U32F m_numSubSteps;
	U32F m_maxNumSubSteps;

	U32F m_numRBHandleTranslations;
	const RBHandleTranslation* m_pRBHandleTranslation;

	NdRecursiveAtomicLock64 m_lock;

	friend class RopeCollidersCollector;
	friend class Rope2DumpViewer;
};

class Rope2DumpViewer : public Process
{
private:
	typedef Process ParentClass;

public:
	Rope2DumpViewer()
		: m_numBodies(0)
		, m_pBodies(nullptr)
		, m_pRBHandleTranslations(nullptr)
		, m_numColliders(0)
		, m_pColliders(nullptr)
		, m_ppDebugDrawData(nullptr)
		, m_setCameraLoc(false)
		, m_pBuf(nullptr)
		, m_bufSize(0)
		, m_bufPos(0)
	{}

	virtual Err Init(const ProcessSpawnInfo& info) override;
	void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override;
	void Dump(Rope2* pRope);
	void DebugDraw();

	void Align16();

	void Read(U32F& val);
	void Read(U64& val);
	void Read(bool& val);
	void Read(F32& val);
	hknpShape* ReadHavokShape();

	void Write(const U8* pData, U32F size);
	void Write(U32F val);
	void Write(U64 val);
	void Write(bool val);
	void Write(F32 val);
	void WriteHavokShape(const hknpShape* pShape);

public:
	Rope2 m_rope;

	U32F m_numBodies;
	RigidBody* m_pBodies;
	RBHandleTranslation* m_pRBHandleTranslations;

	U32F m_numColliders;
	RopeCollider* m_pColliders;

	HavokMeshDrawData** m_ppDebugDrawData;

	Locator m_cameraLoc;
	bool m_setCameraLoc;

private:
	U8* m_pBuf;
	I32F m_bufSize;
	I32F m_bufPos;
	FileSystem::FileHandle m_outFile;
};

PROCESS_DECLARE(Rope2DumpViewer);

#else //FINAL_BUILD

class Rope2Debugger
{
public:
	Rope2Debugger() {}
	~Rope2Debugger() {}

	void Init(Rope2* pRope) {}
	void Reset(Rope2* pRope) {}
	void UpdatePaused() {}
	void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) {}
	void SetExternBuffer(U8* pBuf, U32F bufSize, U32F bufPos) {}
	Rope2* GetRope() { return nullptr; }

	void Save() {}
	bool Restore(U32F numFramesBack) { return true; }

	void DebugDrawColliders() {}

	bool GetColliderLoc(const RopeColliderHandle& hBody, Locator& loc) { return false; }
	bool GetColliderPrevLoc(const RopeColliderHandle& hBody, Locator& loc)  { return false; }
};

class Rope2DumpViewer : public Process
{
public:
	void Dump(Rope2* pRope) {}
	void DebugDraw() {}
};

#endif //FINAL_BUILD

#endif // NDPHYS_ROPE2_DEBUG_H 

