/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

/*! \file rope-mgr.h
	\author Jason Gregory [mailto:jason_gregory@naughtydog.com]
	\brief .

 */

#ifndef NDLIB_ROPE_MGR_H
#define NDLIB_ROPE_MGR_H

#include "corelib/math/gamemath.h"

class Rope2;
class Rope2DumpViewer;

FWD_DECL_PROCESS_HANDLE(ProcessPhysRope);

class RopeMgr
{
public:
	bool m_disableStep;
	bool m_disableCollisions;
	bool m_disableStrainedCollisions;
	bool m_disableNodeAabbPostProjection;
	bool m_disablePostSlide;
	bool m_neverSleep;
	bool m_debugDrawNodes;
	bool m_debugDrawAabb;
	bool m_debugDrawNodeAabb;
	bool m_debugDrawColliders;
	bool m_debugDrawConstraints;
	bool m_debugDrawDistConstraints;
	bool m_debugDrawGrabPoints;
	bool m_debugDrawJointIndices;
	bool m_debugDrawRopeDist;
	bool m_debugDrawRopeRealDist;
	bool m_debugDrawMass;
	bool m_debugDrawTension;
	bool m_debugDrawFriction;
	bool m_debugDrawSolver;
	bool m_debugDrawSlide;
	bool m_fDebugDrawLastPos;
	float m_fDebugDrawVeloScale;
	bool m_debugDrawSelectedIndex;
	I64 m_selectedRopeIndex;
	bool m_debugDrawEdges;
	bool m_debugDrawSaveEdges;
	float m_debugEdgeLengthThreshold; // only display the rope if its length is longer than this threshold (stupid but works)
	bool m_debugDrawEdgePositivness;
	bool m_debugDrawInactiveEdges;
	bool m_debugDrawEdgeFlags;
	bool m_debugDrawEdgeRopeDist;
	bool m_debugDrawEdgeDist;
	bool m_debugDrawSelectedEdge;
	I64 m_selectedRopeEdge;
	I32 m_nWarningLevel;
	bool m_debugDrawAnimBlend;
	bool m_debugDrawTwist;
	bool m_debugDrawHitSounds;
	bool m_debugDrawSlideSounds;
	bool m_debugDrawEdgeSlideSounds;

	bool m_debugDrawColCacheTris;
	bool m_debugDrawColCacheEdges;
	bool m_debugDrawColCacheTriIndex;
	bool m_debugDrawColCacheEdgeIndex;
	bool m_debugDrawColCacheEdgeNorm;
	bool m_debugDrawColCacheEdgeBiNorm;
	bool m_debugDrawColCacheColliders;

	bool m_debugPrintCharControls;

public:
	static const U32F kMaxRopes = 128;
	static const U32F kMaxPreUpdateRopes = 16;
	static const U32F kMaxPreNoRefRigidBodySyncRopes = 16;
	static const U32F kMaxFrameSyncEnd = 16;
	static const U32F kMaxDelayedDestroyCounters = 16;

	Rope2DumpViewer* m_pDumpViewer;

	RopeMgr();

	void Step();

	void RegisterRope(Rope2* pRope);
	void UnregisterRope(Rope2* pRope);
	void RegisterForPreProcessUpdate(Rope2* pRope);
	void PreProcessUpdate();
	void RegisterForPreNoRefRigidBodySync(ProcessPhysRope* pRope);
	void PreNoRefRigidBodySync();
	void RegisterForFrameSyncEnd(ProcessPhysRope* pRope);
	void FrameSyncEnd();
	void RelocateDumpViewer(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);
	void RelocateRope(Rope2* pRope, ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);
	void DebugDraw();
	void CheckRopeForNoRefRigidBodySync(Rope2* pRope);
	void AddDelayedDestroyCounter(ndjob::CounterHandle pCounter, I32F numFrames);

private:
	struct DelayedDestroyCounter
	{
		I32F m_numFrames;
		ndjob::CounterHandle m_pCounter;
	};

	U32F m_numRopes;
	Rope2 *	m_apRope[kMaxRopes];
	U32F m_numPreProcessUpdate;
	Rope2*	m_apPreProcessUpdate[kMaxPreUpdateRopes];
	U32F m_numPreNoRefRigidBodySync;
	MutableProcessPhysRopeHandle m_ahPreNoRefRigidBodySync[kMaxPreNoRefRigidBodySyncRopes];
	U32F m_numFrameSyncEnd;
	MutableProcessPhysRopeHandle m_ahFrameSyncEnd[kMaxFrameSyncEnd];
	U32F m_numDelayedDestroyCounters;
	DelayedDestroyCounter m_delayedDestroyCounters[kMaxDelayedDestroyCounters];

	NdAtomicLock m_lock;
};

extern RopeMgr g_ropeMgr;

#endif // NDLIB_ROPE_MGR_H

