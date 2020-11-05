/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

/*! \file rope-mgr.cpp
    \author Jason Gregory [mailto:jason_gregory@naughtydog.com]
    \brief .

 */


#include "gamelib/ndphys/rope/rope-mgr.h"

#include "ndlib/memory/memory.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/process/debug-selection.h"

#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"
#include "gamelib/ndphys/rope/rope2-debug.h"
#include "gamelib/gameplay/process-phys-rope.h"

#include <Common/Base/hkBase.h>
#include <Common/Base/Types/Geometry/Aabb/hkAabb.h>

// -------------------------------------------------------------------------------------------------
// Rope2
// -------------------------------------------------------------------------------------------------

#include "gamelib/ndphys/rope/rope2.h"

// -------------------------------------------------------------------------------------------------
// Globals
// -------------------------------------------------------------------------------------------------

RopeMgr				g_ropeMgr;

// -------------------------------------------------------------------------------------------------

RopeMgr::RopeMgr() :
	m_disableStep(false),
	m_disableCollisions(false),
	m_disableStrainedCollisions(false),
	m_disableNodeAabbPostProjection(false),
	m_disablePostSlide(false),
	m_neverSleep(false),
	m_debugDrawNodes(false),
	m_debugDrawAabb(false),
	m_debugDrawNodeAabb(false),
	m_debugDrawColliders(false),
	m_debugDrawConstraints(false),
	m_debugDrawDistConstraints(false),
	m_debugDrawGrabPoints(false),
	m_debugDrawJointIndices(false),
	m_debugDrawRopeDist(false),
	m_debugDrawRopeRealDist(false),
	m_debugDrawMass(false),
	m_debugDrawTension(false),
	m_debugDrawFriction(false),
	m_debugDrawSolver(false),
	m_debugDrawSlide(false),
	m_fDebugDrawLastPos(false),
	m_fDebugDrawVeloScale(0.0f),
	m_debugDrawSelectedIndex(false),
	m_selectedRopeIndex(0),
	m_debugDrawEdges(false),
	m_debugDrawSaveEdges(false),
	m_debugEdgeLengthThreshold(10.f),
	m_debugDrawEdgePositivness(false),
	m_debugDrawInactiveEdges(false),
	m_debugDrawEdgeFlags(false),
	m_debugDrawEdgeDist(false),
	m_debugDrawEdgeRopeDist(false),
	m_debugDrawSelectedEdge(false),
	m_debugDrawTwist(false),
	m_debugDrawHitSounds(false),
	m_debugDrawSlideSounds(false),
	m_debugDrawEdgeSlideSounds(false),
	m_selectedRopeEdge(0),
	m_nWarningLevel(0),
	m_numRopes(0u),
	m_numPreProcessUpdate(0u),
	m_numPreNoRefRigidBodySync(0u),
	m_numFrameSyncEnd(0u),
	m_numDelayedDestroyCounters(0),
	m_debugDrawAnimBlend(false),
	m_debugDrawColCacheTris(false),
	m_debugDrawColCacheEdges(false),
	m_debugDrawColCacheTriIndex(false),
	m_debugDrawColCacheEdgeIndex(false),
	m_debugDrawColCacheEdgeNorm(false),
	m_debugDrawColCacheEdgeBiNorm(false),
	m_debugDrawColCacheColliders(false),
	m_debugPrintCharControls(false),
	m_pDumpViewer(nullptr),
	m_lock()
{
}

void RopeMgr::RegisterRope(Rope2* pRope)
{
	AtomicLockJanitor jjLock(&m_lock, FILE_LINE_FUNC);

	PHYSICS_ASSERT(m_numRopes < kMaxRopes);
	m_apRope[m_numRopes++] = pRope;
}

void RopeMgr::UnregisterRope(Rope2* pRope)
{
	AtomicLockJanitor jjLock(&m_lock, FILE_LINE_FUNC);

	for (U32F ii = 0; ii < m_numRopes; ++ii)
	{
		if (m_apRope[ii] == pRope)
		{
			m_numRopes--;
			// swap the last element with the one that's going away
			m_apRope[ii] = m_apRope[m_numRopes];
			m_apRope[m_numRopes] = nullptr;

			for (U32F jj = 0; jj<m_numPreProcessUpdate; jj++)
			{
				if (m_apPreProcessUpdate[jj] == pRope)
				{
					m_numPreProcessUpdate--;
					m_apPreProcessUpdate[jj] = m_apPreProcessUpdate[m_numPreProcessUpdate];
					m_apPreProcessUpdate[m_numPreProcessUpdate] = nullptr;
					break;
				}
			}

			break;
		}
	}
}

void RopeMgr::RegisterForPreProcessUpdate(Rope2* pRope)
{
	AtomicLockJanitor jjLock(&m_lock, FILE_LINE_FUNC);

	PHYSICS_ASSERT(m_numPreProcessUpdate < kMaxPreUpdateRopes);
	m_apPreProcessUpdate[m_numPreProcessUpdate++] = pRope;
}

void RopeMgr::PreProcessUpdate()
{
	for (U32F ii = 0; ii<m_numPreProcessUpdate; ii++)
	{
		m_apPreProcessUpdate[ii]->PreProcessUpdate();
		m_apPreProcessUpdate[ii] = nullptr;
	}
	m_numPreProcessUpdate = 0;

	RopeSkinning::PrepareFrame();
}

void RopeMgr::RegisterForPreNoRefRigidBodySync(ProcessPhysRope* pRope)
{
	AtomicLockJanitor jjLock(&m_lock, FILE_LINE_FUNC);

	PHYSICS_ASSERT(m_numPreNoRefRigidBodySync < kMaxPreNoRefRigidBodySyncRopes);
	m_ahPreNoRefRigidBodySync[m_numPreNoRefRigidBodySync++] = pRope;
}

void RopeMgr::PreNoRefRigidBodySync()
{
	for (U32F ii = 0; ii<m_numPreNoRefRigidBodySync; ii++)
	{
		if (ProcessPhysRope* pRope = m_ahPreNoRefRigidBodySync[ii].ToMutableProcess())
		{
			pRope->PreNoRefRigidBodySync();
		}
		m_ahPreNoRefRigidBodySync[ii] = nullptr;
	}
	m_numPreNoRefRigidBodySync = 0;

	{
		U32F ii = 0;
		while (ii<m_numDelayedDestroyCounters)
		{
			if (--m_delayedDestroyCounters[ii].m_numFrames < 0)
			{
				ndjob::FreeCounter(m_delayedDestroyCounters[ii].m_pCounter);
				m_delayedDestroyCounters[ii] = m_delayedDestroyCounters[m_numDelayedDestroyCounters-1];
				m_numDelayedDestroyCounters--;
			}
			else
			{
				ii++;
			}
		}
	}
}

void RopeMgr::RegisterForFrameSyncEnd(ProcessPhysRope* pRope)
{
	AtomicLockJanitor jjLock(&m_lock, FILE_LINE_FUNC);

	PHYSICS_ASSERT(m_numFrameSyncEnd < kMaxFrameSyncEnd);
	m_ahFrameSyncEnd[m_numFrameSyncEnd++] = pRope;
}

void RopeMgr::FrameSyncEnd()
{
	for (U32F ii = 0; ii<m_numFrameSyncEnd; ii++)
	{
		if (ProcessPhysRope* pRope = m_ahFrameSyncEnd[ii].ToMutableProcess())
		{
			pRope->FrameSyncEnd();
		}
		m_ahFrameSyncEnd[ii] = nullptr;
	}
	m_numFrameSyncEnd = 0;
}

void RopeMgr::RelocateRope(Rope2* pRope, ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	AtomicLockJanitor jjLock(&m_lock, FILE_LINE_FUNC);

	ASSERT(pRope != nullptr && "Attempt to relocate a nullptr rope");

	for (U32F ii = 0; ii < m_numRopes; ++ii)
	{
		if (m_apRope[ii] == pRope)
		{
			RelocatePointer(m_apRope[ii], delta, lowerBound, upperBound);
			break;
		}
	}

	for (U32F ii = 0; ii < m_numPreProcessUpdate; ++ii)
	{
		if (m_apPreProcessUpdate[ii] == pRope)
		{
			RelocatePointer(m_apPreProcessUpdate[ii], delta, lowerBound, upperBound);
			break;
		}
	}
}

void RopeMgr::RelocateDumpViewer(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pDumpViewer, delta, lowerBound, upperBound);
}

void RopeMgr::Step()
{
	if (!g_ndConfig.m_pDMenuMgr->IsProgPaused())
	{
		if (m_numRopes > 0)
			m_apRope[0]->InitRopeDebugger(true);
		return;
	}

	for (U32F i = 0; i < m_numRopes; ++i)
	{
		Rope2* pRope = m_apRope[i];
		pRope->UpdatePaused();
	}
}

void RopeMgr::DebugDraw()
{
#if FINAL_BUILD && !ALLOW_ROPE_DEBUGGER_IN_FINAL
	return;
#endif

	extern bool s_colCacheOverflow;
	if (s_colCacheOverflow)
	{
		SetColor(kMsgConNotRecorded, kColorRed.ToAbgr8());
		MsgConNotRecorded("!!! Rope collision cache overflow! Try reducing collision complexity around the rope !!!\n");
		SetColor(kMsgConNotRecorded, kColorWhite.ToAbgr8());
		if (!g_ndConfig.m_pDMenuMgr->IsProgPaused())
			s_colCacheOverflow = false;
	}

	if (!g_ropeMgr.m_debugDrawNodes && !g_ropeMgr.m_fDebugDrawLastPos && g_ropeMgr.m_fDebugDrawVeloScale == 0.0f && !g_ropeMgr.m_debugDrawSolver &&
		!g_ropeMgr.m_debugDrawConstraints && !g_ropeMgr.m_debugDrawDistConstraints && !g_ropeMgr.m_debugDrawEdges && !g_ropeMgr.m_debugDrawSaveEdges &&
		!g_ropeMgr.m_debugDrawTension && !g_ropeMgr.m_debugDrawFriction && !g_ropeMgr.m_debugDrawTwist)
	{
		return;
	}

	for (U32F i = 0; i < m_numRopes; ++i)
	{
		Rope2* pRope = m_apRope[i];

		if (!DebugSelection::Get().IsProcessOrNoneSelected(pRope->m_pOwner))
		{
			continue;
		}

		//ProcessPhysRope* pOwner = const_cast<ProcessPhysRope*>(ProcessPhysRope::FromProcess(pRope->m_pOwner));
		//if (pOwner && pOwner->IsStepRunning())
		//{
		//	pOwner->WaitStepRope();
		//}

		CheckRopeForNoRefRigidBodySync(pRope);

		// If running on GPU we want to wait now to debug draw the correct data
		pRope->WaitAndGatherAsyncSim();

		pRope->DebugDraw();
	}

	if (m_pDumpViewer)
		m_pDumpViewer->DebugDraw();
}

void RopeMgr::CheckRopeForNoRefRigidBodySync(Rope2* pRope)
{
	for (U32F ii = 0; ii<m_numPreNoRefRigidBodySync; ii++)
	{
		if (ProcessPhysRope* pRopeProc = m_ahPreNoRefRigidBodySync[ii].ToMutableProcess())
		{
			if (&pRopeProc->m_rope == pRope)
			{
				pRopeProc->PreNoRefRigidBodySync();
				m_ahPreNoRefRigidBodySync[ii] = m_ahPreNoRefRigidBodySync[m_numPreNoRefRigidBodySync-1];
				m_numPreNoRefRigidBodySync--;
				return;
			}
		}
	}
}

void RopeMgr::AddDelayedDestroyCounter(ndjob::CounterHandle pCounter, I32F numFrames)
{
	AtomicLockJanitor jjLock(&m_lock, FILE_LINE_FUNC);

	ALWAYS_ASSERT(m_numDelayedDestroyCounters < kMaxDelayedDestroyCounters);
	m_delayedDestroyCounters[m_numDelayedDestroyCounters].m_pCounter = pCounter;
	m_delayedDestroyCounters[m_numDelayedDestroyCounters].m_numFrames = numFrames;
	m_numDelayedDestroyCounters++;
}
