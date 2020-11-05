#if HAVOKVER >= 0x2016

/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/ndphys/rope/rope2-debug.h"

#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/rtc.h"

#include "ndlib/nd-game-info.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/debug/nd-dmenu.h"

#include "gamelib/ndphys/havok-collision-cast.h"
#include "gamelib/ndphys/rigid-body-grid-hash.h"
#include "gamelib/ndphys/rope/rope-mgr.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/ndphys/havok-game-cast-filter.h"
#include "gamelib/ndphys/havok-data.h"
#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"
#include "gamelib/camera/camera-manager.h"
#include "gamelib/gameplay/nd-game-object.h"

#include "physics/havokext/havok-shapetag-codec.h"
#include <Common/Base/Serialize/hkSerialize.h>
#include <Common/Base/System/Io/Writer/Array/hkArrayStreamWriter.h>
#include <Physics/Physics/Dynamics/Body/hknpBody.h>
#include <Physics/Physics/Collide/Shape/Composite/Mesh/Compressed/hknpCompressedMeshShape.h>

#if !FINAL_BUILD || ALLOW_ROPE_DEBUGGER_IN_FINAL

PROCESS_REGISTER_ALLOC_SIZE(Rope2DumpViewer, Process, 1536 * 1024);

Rope2Debugger::~Rope2Debugger()
{
	AllocateJanitor jj(kAllocDebug, FILE_LINE_FUNC);

	if (m_pBuf)
		NDI_DELETE [] m_pBuf;
	if (m_pColliderHandles)
		NDI_DELETE [] m_pColliderHandles;
	if (m_pColliderLoc)
		NDI_DELETE [] m_pColliderLoc;
	if (m_pColliderLocPrev)
		NDI_DELETE [] m_pColliderLocPrev;
};

bool Rope2Debugger::Init(Rope2* pRope)
{
	m_pRope = pRope;

	AllocateNoAssertOnFailJanitor jj(kAllocDebug, FILE_LINE_FUNC);

	//m_bufSize = pRope->m_maxNumPoints * 10000;
	m_bufSize = 300*10000;
	m_pBuf = NDI_NEW U8[m_bufSize];
	m_pColliderHandles = NDI_NEW RopeColliderHandle[kMaxColliders];
	m_pColliderLoc = NDI_NEW Locator[2*kMaxColliders];
	m_pColliderLocPrev = NDI_NEW Locator[2*kMaxColliders];

	if (!m_pBuf || !m_pColliderHandles || !m_pColliderLoc | !m_pColliderLocPrev)
	{
		MsgConScriptError("Not enough debug mem for rope debugger!");
		return false;
	}
	Reset(pRope);

	return true;
}

void Rope2Debugger::Reset(Rope2* pRope)
{
	RecursiveAtomicLockJanitor64 jLock(&m_lock, FILE_LINE_FUNC);

	m_pRope = pRope;

	m_bufPos = 0;
	m_bufferOverrun = false;
	m_frameBackNum = 0;
	m_numSubSteps = 0;

	// Beginning of data signal
	U32F bufPos = 0xffffffff;
	memcpy(m_pBuf, &bufPos, sizeof(bufPos));
	m_bufPos += sizeof(bufPos);

	m_numColliders = 0;
	m_numOverwrittenColliders = 0;

	m_pRBHandleTranslation = nullptr;
	m_numRBHandleTranslations = 0;
}

void Rope2Debugger::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pRope, delta, lowerBound, upperBound);
	RelocatePointer(m_pRBHandleTranslation, delta, lowerBound, upperBound);
}

void Rope2Debugger::SetExternBuffer(U8* pBuf, U32F bufSize, U32F bufPos)
{
	if (m_pBuf)
		NDI_DELETE [] m_pBuf;

	m_pBuf = pBuf;
	m_bufSize = bufSize;
	m_bufPos = bufPos;
}

void Rope2Debugger::SetRBHandleTranslations(const RBHandleTranslation* pTrans, U32F numTrans)
{
	m_pRBHandleTranslation = pTrans;
	m_numRBHandleTranslations = numTrans;
}

void Rope2Debugger::UpdatePaused()
{
	if (g_ndConfig.m_pDMenuMgr->IsProgPaused())
	{
		RecursiveAtomicLockJanitor64 jLock(&m_lock, FILE_LINE_FUNC);

		if (m_bufferOverrun)
		{
			return;
		}
		const Joypad& joypad = *EngineComponents::GetNdFrameState()->GetJoypad(DC::kJoypadPadReal1);
		U32F numFramesBack = m_frameBackNum;
		U32F numSubSteps = m_numSubSteps;
		if (!g_ndConfig.m_pDMenuMgr->IsNextFramePaused())
		{
			numFramesBack = 0;
			numSubSteps = 0;
		}
		else
		{
			numFramesBack = g_ndConfig.m_pDMenuMgr->GetNumBackwardSteps();
			if (numFramesBack != m_frameBackNum)
			{
				numSubSteps = 0;
			}
			else if (!g_ndConfig.m_pDMenuMgr->IsMenuActive())
			{
				if (joypad.GetUpPressed())
				{
					if (numSubSteps == 0)
						numSubSteps = m_maxNumSubSteps;
					numSubSteps++;
					if (m_maxNumSubSteps > 0 && numSubSteps > m_maxNumSubSteps)
					{
						numSubSteps = m_maxNumSubSteps;
						/*if (numFramesBack > 0)
						{
							g_ndConfig.m_pDMenuMgr->DecNumBackwardSteps();
							numSubSteps = 1;
						}
						else
						{
							numSubSteps = 0;
						}*/
					}
				}
				if (joypad.GetDownPressed())
				{
					if (numSubSteps == 0)
						numSubSteps = m_maxNumSubSteps;
					if (numSubSteps > 0)
					{
						numSubSteps--;
						if (numSubSteps == 0)
						{
							//g_ndConfig.m_pDMenuMgr->IncNumBackwardSteps();
							numSubSteps = 1;
						}
					}
				}
			}
		}
		if (numFramesBack != m_frameBackNum || numSubSteps != m_numSubSteps)
		{
			bool ready = m_numSubSteps == 0 && m_frameBackNum == numFramesBack+1; // we can just go forward
			if (Restore(numFramesBack, ready))
			{
				extern U32 g_ropeDebugSubStep;
				g_ropeDebugSubStep = numSubSteps;

				// Cheat our invDt time in because it's used during calculation of collider velocities (RopeCollider::FromRigidBody)
				F32 saveInvDt = g_havok.m_invDt;
				g_havok.m_invDt = m_pRope->m_scInvStepTime;

				RopeBonesData ropeBonesData;
				if (m_pRope->m_pTwistDir)
				{
					m_pRope->FillDataForSkinningPreStep(&ropeBonesData);
				}

				m_pRope->PreStep();

				if (ready)
				{
					if (NdGameObject* pGoOwner = const_cast<NdGameObject*>(NdGameObject::FromProcess(m_pRope->m_pOwner)))
					{
						DebugPrimTime::NoRecordJanitor jj(true);
						m_pRope->UpdateSounds(pGoOwner);
						m_pRope->UpdateFx();
					}
				}

				m_pRope->StepInner();
				m_pRope->StepCleanup();

				if (m_pRope->m_pTwistDir)
				{
					m_pRope->FillDataForSkinningPostStep(&ropeBonesData, false);
					RopeSkinning::TwistPseudoSim(&ropeBonesData, m_pRope->m_scStepTime);
					m_pRope->CopyBackTwistDir();
				}

				g_havok.m_invDt = saveInvDt;

				g_ropeDebugSubStep = 0;
				m_frameBackNum = numFramesBack;
				m_numSubSteps = numSubSteps;
				m_maxNumSubSteps = m_pRope->m_lastStraightenSubsteps;
				m_pRope->ClearAllKeyframedPos();
			}
		}
	}
}

class RopeCollidersCollector : public HavokAabbQueryCollector
{
public:
	virtual bool AddBody(RigidBody* pBodyBase) override
	{
		for (U32 ii = 0; ii<m_pRopeDebugger->m_numColliders; ii++)
		{
			if (m_pRopeDebugger->m_pColliderHandles[ii] == RopeColliderHandle(pBodyBase))
			{
				return true;
			}
		}

		U32 ii = m_pRopeDebugger->m_numColliders;
		JAROS_ALWAYS_ASSERT(ii < Rope2Debugger::kMaxColliders);
		if (ii >= Rope2Debugger::kMaxColliders)
			return false;

		m_pRopeDebugger->m_pColliderHandles[ii] = RopeColliderHandle(pBodyBase);
		m_pRopeDebugger->m_pColliderLoc[ii] = pBodyBase->GetLocatorCm();
		m_pRopeDebugger->m_pColliderLocPrev[ii] = pBodyBase->GetPreviousLocatorCm();
		m_pRopeDebugger->m_numColliders++;
		return true;
	}

public:
	Rope2Debugger* m_pRopeDebugger;
};

void Rope2Debugger::Save()
{
	RecursiveAtomicLockJanitor64 jLock(&m_lock, FILE_LINE_FUNC);

	m_frameBackNum = 0;
	m_numSubSteps = 0;
	m_maxNumSubSteps = Max(1u, m_pRope->m_lastStraightenSubsteps);

	ALWAYS_ASSERT(m_numOverwrittenColliders == 0);
	m_numOverwrittenColliders = 0;
	m_numColliders = 0;
	// Save all colliders that are nearby
	{
		for (U32F ii = 0; ii<m_pRope->m_numCustomColliders; ii++)
		{
			JAROS_ALWAYS_ASSERT(m_numColliders < Rope2Debugger::kMaxColliders);
			if (m_numColliders >= Rope2Debugger::kMaxColliders)
				break;
			const RopeCollider* pCollider = m_pRope->m_ppCustomColliders[ii];
			m_pColliderHandles[m_numColliders] = RopeColliderHandle(ii);
			m_pColliderLoc[m_numColliders] = pCollider->m_loc;
			m_pColliderLocPrev[m_numColliders] = pCollider->m_locPrev;
			m_numColliders++;
		}

		for (U32F ii = 0; ii<m_pRope->m_numRigidBodyColliders; ii++)
		{
			const RigidBody* pBody = m_pRope->m_phRigidBodyColliders[ii].ToBody();
			if (!pBody)
				continue;
			JAROS_ALWAYS_ASSERT(m_numColliders < Rope2Debugger::kMaxColliders);
			if (m_numColliders >= Rope2Debugger::kMaxColliders)
				break;
			m_pColliderHandles[m_numColliders] = RopeColliderHandle(pBody);
			m_pColliderLoc[m_numColliders] = pBody->GetLocatorCm();
			m_pColliderLocPrev[m_numColliders] = pBody->GetPreviousLocatorCm();
			m_numColliders++;
		}

		Aabb aabb = m_pRope->m_aabb;
		if (aabb.IsValid())
		{
			CollideFilter filter;
			m_pRope->GetCollideFilter(false, filter);

			RopeCollidersCollector collector;
			collector.m_pRopeDebugger = this;
			HavokAabbQuery(aabb.m_min, aabb.m_max, collector, filter);
		}
	}

	U32F bufPos = m_bufPos;
	m_restoring = false;
	CopyData(bufPos);

	m_lastInvDt = m_pRope->m_scInvStepTime;

	if (!m_bufferOverrun)
	{
		if (m_bufSize - bufPos < sizeof(m_bufPos))
		{
			if (bufPos <= m_bufPos)
			{
				m_bufferOverrun = true;
			}
			bufPos = 0;
		}
		if (bufPos <= m_bufPos && m_bufPos - bufPos < sizeof(m_bufPos)) 
		{
			m_bufferOverrun = true;
		}
		if (!m_bufferOverrun)
		{
			memcpy(m_pBuf + bufPos, &m_bufPos, sizeof(m_bufPos));
			m_bufPos = bufPos + sizeof(m_bufPos);
			if (m_bufPos == m_bufSize)
			{
				m_bufPos = 0;
			}
		}
	}

	if (m_bufferOverrun)
	{
		Reset(m_pRope);
	}
}

bool Rope2Debugger::Restore(U32F numFramesBack, bool bInputOnly)
{
	if (m_bufferOverrun)
	{
		MsgConNotRecorded("Rope back-step buffer overrun\n");
		return false;
	}

	HavokMarkForWriteJanitor jj;

	// First restore all overwritten colliders
	RestoreColliders();
	m_numColliders = 0;
	m_numOverwrittenColliders = 0;

	U32F bufPos = m_bufPos;
	for (U32F ii = 0; ii<numFramesBack+1; ii++)
	{
		if (bufPos == 0)
		{
			bufPos = m_bufSize-sizeof(bufPos);
		}
		else
		{
			ALWAYS_ASSERT(bufPos >= sizeof(bufPos));
			bufPos -= sizeof(bufPos);
		}
		U32F newBufPos;
		memcpy(&newBufPos, m_pBuf + bufPos, sizeof(bufPos));
		if (newBufPos == 0xffffffff 
			|| (bufPos < m_bufPos && newBufPos > bufPos && newBufPos < m_bufPos)
			|| (bufPos >= m_bufPos && (newBufPos < m_bufPos || newBufPos > bufPos)))
		{
			MsgConNotRecorded("Rope back-step buffer overrun\n");
			return false;
		}
		bufPos = newBufPos;
	}

	m_restoring = true;
	CopyData(bufPos, bInputOnly, numFramesBack > 0);

	return true;
}

void Rope2Debugger::RestoreColliders()
{
	for (U32F ii = 0; ii<m_numOverwrittenColliders; ii++)
	{
		RopeColliderHandle hCollider = m_pColliderHandles[ii];
		if (hCollider.IsValid())
		{
			if (hCollider.IsRigidBody())
			{
				RigidBody* pBody = const_cast<RigidBody*>(hCollider.GetRigidBody());
				pBody->SetLocatorsInternalCmDebug(m_pColliderLoc[kMaxColliders+ii], m_pColliderLocPrev[kMaxColliders+ii]);
				// Update rigid body info for probes
				U32F handleIndex = pBody->GetHandleIndex();
				RigidBodyProbeInfo& rbInfo = g_pRigidBodyProbeInfo[handleIndex];
				if (rbInfo.m_gridLevel >= 0)
				{
					rbInfo.m_pos = pBody->GetBoundingSphere();
					rbInfo.m_rot = pBody->GetRot();
					g_pRigidBodyGridHash->UpdateBody(handleIndex);
				}

			}
			else
			{
				RopeCollider* pCollider = const_cast<RopeCollider*>(hCollider.GetCollider(nullptr, m_pRope));
				pCollider->RestoreLoc(m_pColliderLoc[kMaxColliders+ii], m_pColliderLocPrev[kMaxColliders+ii], m_lastInvDt);
			}
		}
	}
}

void Rope2Debugger::OverwriteColliders()
{
	for (U32F ii = 0; ii<m_numColliders; ii++)
	{
		RopeColliderHandle hCollider = m_pColliderHandles[ii];
		if (hCollider.IsValid())
		{
			if (hCollider.IsRigidBody())
			{
				RigidBody* pBody = const_cast<RigidBody*>(hCollider.GetRigidBody());
				m_pColliderLoc[kMaxColliders+ii] = pBody->GetLocatorCm();
				m_pColliderLocPrev[kMaxColliders+ii] = pBody->GetPreviousLocatorCm();
				pBody->SetLocatorsInternalCmDebug(m_pColliderLoc[ii], m_pColliderLocPrev[ii]);
				// Update rigid body info for probes
				U32F handleIndex = pBody->GetHandleIndex();
				RigidBodyProbeInfo& rbInfo = g_pRigidBodyProbeInfo[handleIndex];
				if (rbInfo.m_gridLevel >= 0)
				{
					rbInfo.m_pos = pBody->GetBoundingSphere();
					rbInfo.m_rot = pBody->GetRot();
					g_pRigidBodyGridHash->UpdateBody(handleIndex);
				}

			}
			else
			{
				RopeCollider* pCollider = const_cast<RopeCollider*>(hCollider.GetCollider(nullptr, m_pRope));
				m_pColliderLoc[kMaxColliders+ii] = pCollider->m_loc;
				m_pColliderLocPrev[kMaxColliders+ii] = pCollider->m_locPrev;
				pCollider->RestoreLoc(m_pColliderLoc[ii], m_pColliderLocPrev[ii], m_pRope->m_scInvStepTime);
			}
		}
	}
}

void Rope2Debugger::DebugDrawColliders()
{
#if FINAL_BUILD && !ALLOW_ROPE_DEBUGGER_IN_FINAL
	return;
#endif

	F32 saveInvDt = g_havok.m_invDt;
	g_havok.m_invDt = m_pRope->m_scInvStepTime;

	for (U32F ii = 0; ii<m_numColliders; ii++)
	{
		RopeColliderHandle hCollider = m_pColliderHandles[ii];
		RopeCollider collideBuffer;
		if (const RopeCollider* pCollider = hCollider.GetCollider(&collideBuffer, m_pRope))
		{
			if (pCollider->m_pShape)
			{
				HavokMeshDrawData** ppDrawData = nullptr;
				if (const RigidBody* pBody = hCollider.GetRigidBody())
				{
					if (const HavokProtoBody* pProto = pBody->GetProtoBody())
					{
						ppDrawData = &(const_cast<HavokProtoBody*>(pProto))->m_pMeshDrawData;
					}
					else if (const HavokBackgroundData* pBgData = pBody->GetBackgroundData())
					{
						ppDrawData = &(const_cast<HavokBackgroundData*>(pBgData))->m_pMeshDrawData;
					}
				}
				//HavokDebugDrawShape(pCollider->m_pShape, pCollider->m_loc, kColorBlue, CollisionDebugDrawConfig::MenuOptions(), ppDrawData);
			}
		}
	}

	g_havok.m_invDt = saveInvDt;
}

void Rope2Debugger::CopyData(U32F& bufPos, bool bInputOnly, bool bOverwriteColliders)
{
	m_startPosCheck = bufPos;

	// Input ...

	CopyBuf((U8*)&m_pRope->m_numKeyPoints, sizeof(m_pRope->m_numKeyPoints), bufPos);
	CopyBuf((U8*)m_pRope->m_pKeyPos, sizeof(m_pRope->m_pKeyPos[0])*m_pRope->m_numKeyPoints, bufPos);
	CopyBuf((U8*)m_pRope->m_pKeyVel, sizeof(m_pRope->m_pKeyVel[0])*m_pRope->m_numKeyPoints, bufPos);
	CopyBuf((U8*)m_pRope->m_pKeyRopeDist, sizeof(m_pRope->m_pKeyRopeDist[0])*m_pRope->m_numKeyPoints, bufPos);
	CopyBuf((U8*)m_pRope->m_pKeyNodeFlags, sizeof(m_pRope->m_pKeyNodeFlags[0])*m_pRope->m_numKeyPoints, bufPos);
	CopyBuf((U8*)&m_pRope->m_numKeyRadius, sizeof(m_pRope->m_numKeyRadius), bufPos);
	CopyBuf((U8*)m_pRope->m_pKeyRadius, sizeof(m_pRope->m_pKeyRadius[0])*m_pRope->m_numKeyRadius, bufPos);	
	CopyBuf((U8*)m_pRope->m_pKeyMoveDist, sizeof(m_pRope->m_pKeyMoveDist[0])*m_pRope->m_numKeyPoints, bufPos);	
	if (m_pRope->m_pKeyTwistDir)
	{
		CopyBuf((U8*)m_pRope->m_pKeyTwistDir, sizeof(m_pRope->m_pKeyTwistDir[0])*m_pRope->m_numKeyPoints, bufPos);	
	}

	CopyBuf((U8*)&m_pRope->m_fMassiveEndDist, sizeof(m_pRope->m_fMassiveEndDist), bufPos);
	CopyBuf((U8*)&m_pRope->m_fMassiveEndRatio, sizeof(m_pRope->m_fMassiveEndRatio), bufPos);

	CopyBuf((U8*)&m_pRope->m_bAllowKeyStretchConstraints, sizeof(m_pRope->m_bAllowKeyStretchConstraints), bufPos);
	CopyBuf((U8*)&m_pRope->m_bAllowDistStretchConstraints, sizeof(m_pRope->m_bAllowDistStretchConstraints), bufPos);

	CopyBuf((U8*)&m_pRope->m_userDistanceConstraint, sizeof(m_pRope->m_userDistanceConstraint), bufPos);
	CopyBuf((U8*)&m_pRope->m_userDistanceConstraintBlend, sizeof(m_pRope->m_userDistanceConstraintBlend), bufPos);

	CopyBuf((U8*)&m_pRope->m_scStepTime, sizeof(m_pRope->m_scStepTime), bufPos);
	m_pRope->m_scInvStepTime = 1.0f / m_pRope->m_scStepTime;

	// For bckward compat
	Scalar lastSTepTime(0.0f);
	CopyBuf((U8*)&lastSTepTime, sizeof(lastSTepTime), bufPos);

	CopyBuf((U8*)&m_pRope->m_numIgnoreCollisionBodies, sizeof(m_pRope->m_numIgnoreCollisionBodies), bufPos);
	CopyBuf((U8*)m_pRope->m_pIgnoreCollisionBodies, sizeof(m_pRope->m_pIgnoreCollisionBodies[0])*m_pRope->m_numIgnoreCollisionBodies, bufPos);

	// Save custom colliders enabled/disabled flag because we may change that a lot on the fly
	U32F numCustomColliders;
	if (!m_restoring)
	{
		numCustomColliders = m_pRope->m_numCustomColliders;
	}
	CopyBuf((U8*)&numCustomColliders, sizeof(numCustomColliders), bufPos);
	if (m_restoring)
	{
		ASSERT(numCustomColliders == m_pRope->m_numCustomColliders);
	}
	for (U32F ii = 0; ii<numCustomColliders; ii++)
	{
		if (ii < m_pRope->m_numCustomColliders)
		{
			CopyBuf((U8*)&m_pRope->m_ppCustomColliders[ii]->m_enabled, sizeof(m_pRope->m_ppCustomColliders[ii]->m_enabled), bufPos);
		}
		else
		{
			bool b;
			CopyBuf((U8*)&b, sizeof(b), bufPos);
		}
	}

	CopyBuf((U8*)&m_numColliders, sizeof(m_numColliders), bufPos);
	ASSERT(m_numColliders <= kMaxColliders);
	CopyBuf((U8*)m_pColliderHandles, sizeof(m_pColliderHandles[0])*m_numColliders, bufPos);
	CopyBuf((U8*)m_pColliderLoc, sizeof(m_pColliderLoc[0])*m_numColliders, bufPos);
	CopyBuf((U8*)m_pColliderLocPrev, sizeof(m_pColliderLocPrev[0])*m_numColliders, bufPos);

	if (m_restoring && m_pRBHandleTranslation)
	{
		// Translate handle id/index if we have translation table
		for (U32F ii = 0; ii<m_numColliders; ii++)
		{
			RopeColliderHandle& hCollider = m_pColliderHandles[ii];
			if (hCollider.IsRigidBody())
			{
				RigidBodyHandle hBody = hCollider.GetRigidBodyHandle();
				for (U32F iTrans = 0; iTrans<m_numRBHandleTranslations; iTrans++)
				{
					if (m_pRBHandleTranslation[iTrans].m_bodyId == hBody.GetHandleId())
					{
						hCollider = RopeColliderHandle(m_pRBHandleTranslation[iTrans].m_handle, hCollider.GetListIndex());
						break;
					}
				}
			}
		}
	}

	CopyBuf((U8*)&m_pRope->m_minCharCollisionDist, sizeof(m_pRope->m_minCharCollisionDist), bufPos);
	CopyBuf((U8*)&m_pRope->m_saveEdgePos, sizeof(m_pRope->m_saveEdgePos), bufPos);
	CopyBuf((U8*)&m_pRope->m_saveEdgePosSet, sizeof(m_pRope->m_saveEdgePosSet), bufPos);
	CopyBuf((U8*)&m_pRope->m_savePosRopeDist, sizeof(m_pRope->m_savePosRopeDist), bufPos);
	CopyBuf((U8*)&m_pRope->m_savePosRopeDistSet, sizeof(m_pRope->m_savePosRopeDistSet), bufPos);
	CopyBuf((U8*)&m_pRope->m_saveStartEdgePos, sizeof(m_pRope->m_saveStartEdgePos), bufPos);
	CopyBuf((U8*)&m_pRope->m_saveStartEdgePosSet, sizeof(m_pRope->m_saveStartEdgePosSet), bufPos);

	CopyBuf((U8*)&m_pRope->m_numExternSaveEdges, sizeof(m_pRope->m_numExternSaveEdges), bufPos);
	CopyBuf((U8*)m_pRope->m_pExternSaveEdges, sizeof(m_pRope->m_pExternSaveEdges[0])*m_pRope->m_numExternSaveEdges, bufPos);

	// Output ...

	U32F numPoints;
	CopyBufWithTemp((U8*)&m_pRope->m_numPoints, (U8*)&numPoints, sizeof(m_pRope->m_numPoints), bufPos, bInputOnly);
	CopyBuf((U8*)m_pRope->m_pPos, sizeof(m_pRope->m_pPos[0])*numPoints, bufPos, bInputOnly);
	CopyBuf((U8*)m_pRope->m_pVel, sizeof(m_pRope->m_pVel[0])*numPoints, bufPos, bInputOnly);
	//CopyBuf((U8*)m_pRope->m_pLastPos, sizeof(m_pRope->m_pLastPos[0])*numPoints, bufPos, bInputOnly);
	CopyBuf((U8*)m_pRope->m_pRopeDist, sizeof(m_pRope->m_pRopeDist[0])*numPoints, bufPos, bInputOnly);
	//CopyBuf((U8*)m_pRope->m_pRadius, sizeof(m_pRope->m_pRadius[0])*numPoints, bufPos, bInputOnly);
	CopyBuf((U8*)m_pRope->m_pNodeFlags, sizeof(m_pRope->m_pNodeFlags[0])*numPoints, bufPos, bInputOnly);
	CopyBuf((U8*)m_pRope->m_pInvRelMass, sizeof(m_pRope->m_pInvRelMass[0])*numPoints, bufPos, bInputOnly);
	if (m_pRope->m_pTensionFriction)
		CopyBuf((U8*)m_pRope->m_pTensionFriction, sizeof(m_pRope->m_pTensionFriction[0])*numPoints, bufPos, bInputOnly);
	if (m_pRope->m_pTwistDir)
		CopyBuf((U8*)m_pRope->m_pTwistDir, sizeof(m_pRope->m_pTwistDir[0])*numPoints, bufPos, bInputOnly);

	for (U32F ii = 0; ii<numPoints; ii++)
	{
		Rope2::Constraint& constr = m_pRope->m_pConstr[ii];
		U8 numPlanes;
		U8 numEdges;
		CopyBufWithTemp((U8*)&constr.m_numPlanes, (U8*)&numPlanes, sizeof(constr.m_numPlanes), bufPos, bInputOnly);
		CopyBuf((U8*)&constr.m_firstNoEdgePlane, sizeof(constr.m_firstNoEdgePlane), bufPos, bInputOnly);
		CopyBufWithTemp((U8*)&constr.m_numEdges, (U8*)&numEdges, sizeof(constr.m_numEdges), bufPos, bInputOnly);
		CopyBuf((U8*)&constr.m_flags, sizeof(constr.m_flags), bufPos, bInputOnly);
		CopyBuf((U8*)constr.m_edgePlanes, sizeof(constr.m_edgePlanes[0])*numEdges*2, bufPos, bInputOnly);
		CopyBuf((U8*)constr.m_planes, sizeof(constr.m_planes[0])*numPlanes, bufPos, bInputOnly);
		CopyBuf((U8*)constr.m_biPlanes, sizeof(constr.m_biPlanes[0])*numEdges*2, bufPos, bInputOnly);
		CopyBuf((U8*)constr.m_hCollider, sizeof(constr.m_hCollider[0])*numPlanes, bufPos, bInputOnly);
	}

	U32F numEdges;
	CopyBufWithTemp((U8*)&m_pRope->m_numEdges, (U8*)&numEdges, sizeof(m_pRope->m_numEdges), bufPos, bInputOnly);
	CopyBuf((U8*)m_pRope->m_pEdges, sizeof(m_pRope->m_pEdges[0])*numEdges, bufPos, bInputOnly);

	U32F numSaveEdges;
	CopyBufWithTemp((U8*)&m_pRope->m_numSaveEdges, (U8*)&numSaveEdges, sizeof(m_pRope->m_numSaveEdges), bufPos, bInputOnly);
	ASSERT(numSaveEdges == 0 || m_pRope->m_pSaveEdges);
	if (m_pRope->m_pSaveEdges)
		CopyBuf((U8*)m_pRope->m_pSaveEdges, sizeof(m_pRope->m_pSaveEdges[0])*numSaveEdges, bufPos, bInputOnly);

	CopyBuf((U8*)&m_pRope->m_aabbSlacky, sizeof(m_pRope->m_aabbSlacky), bufPos, bInputOnly);
	CopyBuf((U8*)&m_pRope->m_aabb, sizeof(m_pRope->m_aabb), bufPos, bInputOnly);

	CopyBuf((U8*)&m_pRope->m_vConstraintHash, sizeof(m_pRope->m_vConstraintHash), bufPos, bInputOnly);
	CopyBuf((U8*)&m_pRope->m_numFramesCreep, sizeof(m_pRope->m_numFramesCreep), bufPos, bInputOnly);

	RopeColCache& colCache = m_pRope->m_colCache;

	if (m_restoring)
	{
		if (bOverwriteColliders)
		{
			// Overwrite colliders
			OverwriteColliders();
			m_numOverwrittenColliders = m_numColliders;
		}

		ScopedTempAllocator jj(FILE_LINE_FUNC);
		U32F numEdgeIds;
		CopyBuf((U8*)&numEdgeIds, sizeof(numEdgeIds), bufPos);
		RopeColEdgeId* pEdgeIds = NDI_NEW RopeColEdgeId[numEdgeIds];
		CopyBuf((U8*)pEdgeIds, sizeof(pEdgeIds[0])*numEdgeIds, bufPos, bInputOnly);
		U32F numSaveEdgeIds;
		CopyBuf((U8*)&numSaveEdgeIds, sizeof(numSaveEdgeIds), bufPos);
		RopeColEdgeId* pSaveEdgeIds = NDI_NEW RopeColEdgeId[numSaveEdgeIds];
		CopyBuf((U8*)pSaveEdgeIds, sizeof(pSaveEdgeIds[0])*numSaveEdgeIds, bufPos, bInputOnly);
		U32F numExternSaveEdgeIds;
		CopyBuf((U8*)&numExternSaveEdgeIds, sizeof(numExternSaveEdgeIds), bufPos);
		RopeColEdgeId* pExternSaveEdgeIds = NDI_NEW RopeColEdgeId[numExternSaveEdgeIds];
		CopyBuf((U8*)pExternSaveEdgeIds, sizeof(pExternSaveEdgeIds[0])*numExternSaveEdgeIds, bufPos, bInputOnly);

		if (!bInputOnly && numEdgeIds+numSaveEdgeIds+numExternSaveEdgeIds > 0)
		{
			// Clear num edges so that CreateColCache does not mess with them
			U32F saveNumEdges = m_pRope->m_numEdges;
			m_pRope->m_numEdges = 0;
			U32F saveNumSaveEdges = m_pRope->m_numSaveEdges;
			m_pRope->m_numSaveEdges = 0;
			U32F saveExternNumSaveEdges = m_pRope->m_numExternSaveEdges;
			m_pRope->m_numExternSaveEdges = 0;
			// Cheat our invDt time in because it's used during calculation of collider velocities (RopeCollider::FromRigidBody)
			F32 saveInvDt = g_havok.m_invDt;
			g_havok.m_invDt = m_pRope->m_scInvStepTime;
			// CreateColCache to get colCache filled in
			m_pRope->CreateColCache();
			g_havok.m_invDt = saveInvDt;
			m_pRope->m_numEdges = saveNumEdges;
			m_pRope->m_numSaveEdges = saveNumSaveEdges;
			m_pRope->m_numExternSaveEdges = saveExternNumSaveEdges;

			// If we have RB handle translation use it to translate handles in edge ids
			if (m_pRBHandleTranslation)
			{
				for (U32F ii = 0; ii<numEdgeIds; ii++)
				{
					U32F bodyId = 0;
					if (pEdgeIds[ii].m_shape.IsRigidBody())
					{
						bodyId = pEdgeIds[ii].m_shape.GetRigidBodyHandle().GetHandleId();
					}
					if (bodyId)
					{
						for (U32F iTrans = 0; iTrans<m_numRBHandleTranslations; iTrans++)
						{
							if (m_pRBHandleTranslation[iTrans].m_bodyId == bodyId)
							{
								pEdgeIds[ii].m_shape = RopeColliderHandle(m_pRBHandleTranslation[iTrans].m_handle, pEdgeIds[ii].m_shape.GetListIndex());
							}
						}
					}
				}
				for (U32F ii = 0; ii<numSaveEdgeIds; ii++)
				{
					U32F bodyId = 0;
					if (pSaveEdgeIds[ii].m_shape.IsRigidBody())
					{
						bodyId = pSaveEdgeIds[ii].m_shape.GetRigidBodyHandle().GetHandleId();
					}
					if (bodyId)
					{
						for (U32F iTrans = 0; iTrans<m_numRBHandleTranslations; iTrans++)
						{
							if (m_pRBHandleTranslation[iTrans].m_bodyId == bodyId)
							{
								pSaveEdgeIds[ii].m_shape = RopeColliderHandle(m_pRBHandleTranslation[iTrans].m_handle, pSaveEdgeIds[ii].m_shape.GetListIndex());
							}
						}
					}
				}
				for (U32F ii = 0; ii<numExternSaveEdgeIds; ii++)
				{
					U32F bodyId = 0;
					if (pExternSaveEdgeIds[ii].m_shape.IsRigidBody())
					{
						bodyId = pExternSaveEdgeIds[ii].m_shape.GetRigidBodyHandle().GetHandleId();
					}
					if (bodyId)
					{
						for (U32F iTrans = 0; iTrans<m_numRBHandleTranslations; iTrans++)
						{
							if (m_pRBHandleTranslation[iTrans].m_bodyId == bodyId)
							{
								pExternSaveEdgeIds[ii].m_shape = RopeColliderHandle(m_pRBHandleTranslation[iTrans].m_handle, pExternSaveEdgeIds[ii].m_shape.GetListIndex());
							}
						}
					}
				}
			}
			// Restore edges indices from saved edge ids
			m_pRope->RestoreEdgeIndices(pEdgeIds, m_pRope->m_pEdges, m_pRope->m_numEdges);
			if (m_pRope->m_pSaveEdges)
				m_pRope->RestoreEdgeIndices(pSaveEdgeIds, m_pRope->m_pSaveEdges, m_pRope->m_numSaveEdges);
			if (m_pRope->m_pExternSaveEdges)
				m_pRope->RestoreEdgeIndices(pExternSaveEdgeIds, m_pRope->m_pExternSaveEdges, m_pRope->m_numExternSaveEdges);
		}
	}
	else
	{
		ScopedTempAllocator jj(FILE_LINE_FUNC);
		RopeColEdgeId* pEdgeIds = NDI_NEW RopeColEdgeId[m_pRope->m_numEdges*Rope2::EdgePoint::kMaxNumEdges];
		U32F numEdgeIds = m_pRope->StoreEdgeIds(pEdgeIds, m_pRope->m_pEdges, m_pRope->m_numEdges);
		CopyBuf((U8*)&numEdgeIds, sizeof(numEdgeIds), bufPos, bInputOnly);
		CopyBuf((U8*)pEdgeIds, sizeof(pEdgeIds[0])*numEdgeIds, bufPos, bInputOnly);
		RopeColEdgeId* pSaveEdgeIds = NDI_NEW RopeColEdgeId[m_pRope->m_numSaveEdges*Rope2::EdgePoint::kMaxNumEdges];
		U32F numSaveEdgeIds = m_pRope->m_pSaveEdges ? m_pRope->StoreEdgeIds(pSaveEdgeIds, m_pRope->m_pSaveEdges, m_pRope->m_numSaveEdges) : 0;
		CopyBuf((U8*)&numSaveEdgeIds, sizeof(numSaveEdgeIds), bufPos, bInputOnly);
		if (m_pRope->m_pSaveEdges)
			CopyBuf((U8*)pSaveEdgeIds, sizeof(pEdgeIds[0])*numSaveEdgeIds, bufPos, bInputOnly);
		RopeColEdgeId* pExternSaveEdgeIds = NDI_NEW RopeColEdgeId[m_pRope->m_numExternSaveEdges*Rope2::EdgePoint::kMaxNumEdges];
		U32F numExternSaveEdgeIds = m_pRope->m_pExternSaveEdges ? m_pRope->StoreEdgeIds(pExternSaveEdgeIds, m_pRope->m_pExternSaveEdges, m_pRope->m_numExternSaveEdges) : 0;
		CopyBuf((U8*)&numExternSaveEdgeIds, sizeof(numExternSaveEdgeIds), bufPos, bInputOnly);
		if (m_pRope->m_pExternSaveEdges)
			CopyBuf((U8*)pExternSaveEdgeIds, sizeof(pEdgeIds[0])*numExternSaveEdgeIds, bufPos, bInputOnly);
	}

	/*bInputOnly = false; // @@ The col cache content can diverge from the saved data so we need to keep it in sync by restoring from the buffer for now
	if (m_restoring && !bInputOnly)
		colCache.Reset();
	CopyBuf((U8*)&colCache.m_numShapes, sizeof(colCache.m_numShapes), bufPos, bInputOnly);
	CopyBuf((U8*)&colCache.m_numTris, sizeof(colCache.m_numTris), bufPos, bInputOnly);
	CopyBuf((U8*)&colCache.m_numEdges, sizeof(colCache.m_numEdges), bufPos, bInputOnly);
	CopyBuf((U8*)&colCache.m_numTriIndices, sizeof(colCache.m_numTriIndices), bufPos, bInputOnly);
	CopyBuf((U8*)&colCache.m_numEdgeIndices, sizeof(colCache.m_numEdgeIndices), bufPos, bInputOnly);
	CopyBuf((U8*)&colCache.m_numPoints, sizeof(colCache.m_numPoints), bufPos, bInputOnly);
	CopyBuf((U8*)colCache.m_pShapes, sizeof(colCache.m_pShapes[0])*colCache.m_numShapes, bufPos, bInputOnly);
	CopyBuf((U8*)colCache.m_pTris, sizeof(colCache.m_pTris[0])*colCache.m_numTris, bufPos, bInputOnly);
	CopyBuf((U8*)colCache.m_pEdges, sizeof(colCache.m_pEdges[0])*colCache.m_numEdges, bufPos, bInputOnly);
	CopyBuf((U8*)colCache.m_pPointTris, sizeof(colCache.m_pPointTris[0])*colCache.m_numPoints, bufPos, bInputOnly);
	CopyBuf((U8*)colCache.m_pPointEdges, sizeof(colCache.m_pPointEdges[0])*colCache.m_numPoints, bufPos, bInputOnly);
	CopyBuf((U8*)colCache.m_pTriIndices, sizeof(colCache.m_pTriIndices[0])*colCache.m_numTriIndices, bufPos, bInputOnly);
	CopyBuf((U8*)colCache.m_pEdgeIndices, sizeof(colCache.m_pEdgeIndices[0])*colCache.m_numEdgeIndices, bufPos, bInputOnly);*/

	ASSERT(!m_bufferOverrun);
}

void Rope2Debugger::CopyBufWithTemp(U8* pData, U8* pTemp, U32F size, U32F& bufPos, bool bInputOnly)
{
	if (!m_restoring)
	{
		memcpy(pTemp, pData, size);
	}
	CopyBuf(pTemp, size, bufPos);
	if (m_restoring && !bInputOnly)
	{
		memcpy(pData, pTemp, size);
	}
}

void Rope2Debugger::CopyBuf(U8* pData, U32F size, U32F& bufPos, bool bSkip)
{
	if (size >= m_bufSize)
	{
		m_bufferOverrun = true;
		return;
	}

	U32F bufPos0 = bufPos;
	if (m_restoring)
		RestoreFromBuf(pData, size, bufPos, bSkip);
	else
		SaveToBuf(pData, size, bufPos);

	// Check for buffer overrun
	if ((bufPos0 >= m_startPosCheck && bufPos < bufPos0 && bufPos > m_startPosCheck) 
		|| (bufPos0 < m_startPosCheck && (bufPos > m_startPosCheck || bufPos < bufPos0)))
	{
		m_bufferOverrun = true;
	}
}

void Rope2Debugger::SaveToBuf(const U8* pData, U32F size, U32F& bufPos)
{
	U32F size1 = Min(size, m_bufSize - bufPos);
	memcpy(m_pBuf + bufPos, pData, size1);
	bufPos += size1;
	if (bufPos == m_bufSize)
	{
		bufPos = 0;
	}
	if (size1 < size)
	{
		memcpy(m_pBuf, pData+size1, size-size1);
		bufPos = size-size1;
	}
}

void Rope2Debugger::RestoreFromBuf(U8* pData, U32F size, U32F& bufPos, bool bSkip)
{
	U32F size1 = Min(size, m_bufSize - bufPos);
	if (!bSkip)
		memcpy(pData, m_pBuf + bufPos, size1);
	bufPos += size1;
	if (bufPos == m_bufSize)
	{
		bufPos = 0;
	}
	if (size1 < size)
	{
		memcpy(pData+size1, m_pBuf, size-size1);
		bufPos = size-size1;
	}
}


Err Rope2DumpViewer::Init(const ProcessSpawnInfo& info)
{
	Err res = ParentClass::Init(info);

	if (info.m_pUserData)
	{
		return Err::kOK;
	}

	char filename[FileIO::MAX_PATH_SIZE];
	sprintf(filename, "%s/rope-dump.bin", EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir);

	FileSystem::FileHandle fh;
	FileSystem::Stat stats;

	EngineComponents::GetFileSystem()->OpenSync(filename, &fh, FS_O_RDONLY);
	EngineComponents::GetFileSystem()->FstatSync(fh, &stats);

	if (stats.m_uSize <= 0)
	{
		MsgErr("Failed load rope dump file '%s'\n",filename);
		EngineComponents::GetFileSystem()->CloseSync(fh);
		ALWAYS_ASSERT(false);
		return Err::kErrGeneral;
	}

	U8* pBuf = NDI_NEW(kAllocDebug, kAlign16) U8[stats.m_uSize];
	I64 numBytesRead;
	EngineComponents::GetFileSystem()->ReadSync(fh, pBuf, stats.m_uSize, &numBytesRead);
	EngineComponents::GetFileSystem()->CloseSync(fh);

	ALWAYS_ASSERT(numBytesRead == stats.m_uSize);
	m_pBuf = pBuf;
	m_bufSize = stats.m_uSize;
	m_bufPos = 0;

	U32 version;
	Read(version);
	ALWAYS_ASSERT(version == 101);

	F32 fLength;
	F32 fRadius;
	F32 fSegmentLength;
	bool bNeverStrained;
	bool bAllowStretchConstraints;
	bool bWithSavePos;

	Rope2InitData initData;
	Read(initData.m_length);
	Read(initData.m_radius);
	Read(initData.m_segmentLength);
	Read(initData.m_neverStrained);
	Read(initData.m_allowKeyStretchConstraints);
	initData.m_allowKeyStretchConstraints = !initData.m_allowKeyStretchConstraints;
	Read(initData.m_withSavePos);
	Read(initData.m_enablePostSolve);
	Read(initData.m_useTwistDir);

	m_rope.Init(initData);

	Read(m_rope.m_fDamping);
	Read(m_rope.m_fViscousDamping);
	Read(m_rope.m_fBendingStiffness);
	Read(m_rope.m_fFreeBendAngle);
	Read(m_rope.m_fProporFriction);
	Read(m_rope.m_fConstFriction);
	Read(m_rope.m_fGravityFactor);
	Read(m_rope.m_fBendingMinE);
	Read(m_rope.m_fBendingMinMR);
	Read(m_rope.m_fNumItersPerEdge);
	Read(m_rope.m_fNumMultiGridItersPerEdge);
	Read(m_rope.m_layerMask);
	Read(m_rope.m_strainedLayerMask);

	{
		HavokMarkForWriteJanitor jj;

		Read(m_numBodies);
		m_pBodies = NDI_NEW RigidBody[m_numBodies];
		m_pRBHandleTranslations = NDI_NEW(kAllocDebug) RBHandleTranslation[m_numBodies];

		for (U32F ii = 0; ii<m_numBodies; ii++)
		{
			U32F handleId;
			U32F handleIndex;
			U32F layer;
			Read(handleId);
			Read(handleIndex);
			Read(layer);

			hknpShape* pShape = ReadHavokShape();
			m_pBodies[ii].InitLinkedToNothing(*pShape);
			m_pBodies[ii].SetLayer((Collide::Layer)layer);
			m_pBodies[ii].SetMotionType(kRigidBodyMotionTypeGameDriven);
			m_pRBHandleTranslations[ii].m_bodyId = handleId;
			m_pRBHandleTranslations[ii].m_handle = RigidBodyHandle(&m_pBodies[ii]);
		}
	}

	Read(m_numColliders);
	m_pColliders = NDI_NEW RopeCollider[m_numColliders];
	for (U32F ii = 0; ii<m_numColliders; ii++)
	{
		m_pColliders[ii].m_pShape = ReadHavokShape();
		m_rope.AddCustomCollider(&m_pColliders[ii]);
	}

	{
		F32 x, y, z, w;
		Read(x); Read(y); Read(z);
		m_cameraLoc.SetTranslation(Point(x, y, z));
		Read(x); Read(y); Read(z); Read(w);
		m_cameraLoc.SetRotation(Quat(x, y, z, w));
		m_setCameraLoc = true;
	}

	m_rope.InitRopeDebugger();

	U32F debuggerBufPos;
	Read(debuggerBufPos);
	U32 debuggerBufSize;
	Read(debuggerBufSize);
	Align16();
	ALWAYS_ASSERT(debuggerBufSize <= m_bufSize-m_bufPos);
	m_rope.m_pDebugger->SetExternBuffer(m_pBuf + m_bufPos, debuggerBufSize, debuggerBufPos);

	m_rope.m_pDebugger->SetRBHandleTranslations(m_pRBHandleTranslations, m_numBodies);

	m_rope.m_pDebugger->Restore(0);
	m_rope.m_pDebugger->m_lastInvDt = m_rope.m_scInvStepTime;
	m_rope.m_pDebugger->OverwriteColliders();

	//m_rope.StepInner();

	g_ropeMgr.RegisterRope(&m_rope);

	m_ppDebugDrawData = NDI_NEW HavokMeshDrawData*[m_numBodies+m_numColliders];
	memset(m_ppDebugDrawData, 0, (m_numBodies+m_numColliders)*sizeof(m_ppDebugDrawData[0]));

	g_ropeMgr.m_pDumpViewer = this;
	g_ropeMgr.m_debugDrawNodes = true;
	g_ropeMgr.m_debugDrawEdges = true;
	g_ropeMgr.m_debugDrawEdgePositivness = true;
	g_ropeMgr.m_debugDrawInactiveEdges = true;
	g_ropeMgr.m_debugDrawColCacheEdgeNorm = true;

	return res;
}

void Rope2DumpViewer::Dump(Rope2* pRope)
{
	U32F numBodies = 0;
	RigidBodyHandle* pBodyHandles = NDI_NEW(kAllocDebug) RigidBodyHandle[1000];

	U32F numFramesBack = 0;
	while (1) 
	{
		if (!pRope->m_pDebugger->Restore(numFramesBack, true))
			break;
		if (pRope->m_pDebugger->m_bufferOverrun)
			break;

		for (U32F ii = 0; ii<pRope->m_pDebugger->m_numColliders; ii++)
		{
			RopeColliderHandle* pCollider = &pRope->m_pDebugger->m_pColliderHandles[ii];
			if (pCollider->IsRigidBody())
			{
				if (const RigidBody* pBody = pCollider->GetRigidBody())
				{
					U32F iBody;
					for (iBody = 0; iBody<numBodies; iBody++)
					{
						if (pBodyHandles[iBody].GetHandleId() == pBody->GetHandleId() && pBodyHandles[iBody].GetHandleIndex() == pBody->GetHandleIndex())
						{
							break;
						}
					}
					if (iBody == numBodies)
					{
						ALWAYS_ASSERT(numBodies < 1000);
						pBodyHandles[numBodies] = RigidBodyHandle(pBody);
						numBodies++;
					}
				}
			}
		}

		numFramesBack++;
	}

	U8* pBuf = NDI_NEW(kAllocDebug) U8[8192];
	m_pBuf = pBuf;
	m_bufSize = 8192;
	m_bufPos = 0;

	RtcDateTime time;
	rtcGetCurrentClockLocalTime(&time);

	char dirname[FileIO::MAX_PATH_SIZE];
	//sprintf(dirname, "%s/rope-dumps", EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir);
	//sprintf(dirname, "/host/X:/t2/rope-dumps");
	//EngineComponents::GetFileSystem()->CreateDirectorySync(dirname);
	sprintf(dirname, "/host/X:/t2/rope-dumps/%i-%i-%i-%i-%i-%i", (int)time.year, (int)time.month, (int)time.day, (int)time.hour, (int)time.minute, (int)time.second);
	Err res = EngineComponents::GetFileSystem()->CreateDirectorySync(dirname);
	if (!res.Succeeded())
	{
		sprintf(dirname, "/host/C:/t2/rope-dumps/%i-%i-%i-%i-%i-%i", (int)time.year, (int)time.month, (int)time.day, (int)time.hour, (int)time.minute, (int)time.second);
		res = EngineComponents::GetFileSystem()->CreateDirectorySync(dirname);
	}

	char filename[FileIO::MAX_PATH_SIZE];
	sprintf(filename, "%s/rope-dump.bin", dirname);
	EngineComponents::GetFileSystem()->OpenSync(filename, &m_outFile, FS_O_CREAT|FS_O_WRONLY);

	Write((U32)101);
	Write(pRope->m_fLength);
	Write(pRope->m_fRadius);
	Write(pRope->m_fSegmentLength);
	Write(pRope->m_bNeverStrained);
	Write(pRope->m_pPrevKeyIndex != nullptr);
	Write(pRope->m_pSaveEdges != nullptr);
	Write(pRope->m_pTensionFriction != nullptr);
	Write(pRope->m_pKeyTwistDir != nullptr);

	Write(pRope->m_fDamping);
	Write(pRope->m_fViscousDamping);
	Write(pRope->m_fBendingStiffness);
	Write(pRope->m_fFreeBendAngle);
	Write(pRope->m_fProporFriction);
	Write(pRope->m_fConstFriction);
	Write(pRope->m_fGravityFactor);
	Write(pRope->m_fBendingMinE);
	Write(pRope->m_fBendingMinMR);
	Write(pRope->m_fNumItersPerEdge);
	Write(pRope->m_fNumMultiGridItersPerEdge);
	Write(pRope->m_layerMask);
	Write(pRope->m_strainedLayerMask);
	
	{
		HavokMarkForReadJanitor jj;

		Write(numBodies);
		for (U32F ii = 0; ii<numBodies; ii++)
		{
			RigidBodyHandle& hBody = pBodyHandles[ii];
			Write(hBody.GetHandleId());
			Write(hBody.GetHandleIndex());
			Write((U32F)(hBody.ToBody()->GetLayer()));
			const hknpShape* pShape = hBody.ToBody()->GetHavokShape();
			WriteHavokShape(pShape);
		}
	}

	Write(pRope->m_numCustomColliders);
	for (U32F ii = 0; ii<pRope->m_numCustomColliders; ii++)
	{
		const RopeCollider* pCollider = pRope->m_ppCustomColliders[ii];
		WriteHavokShape(pCollider->m_pShape);
	}

	{
		const RenderCamera& cam = GetRenderCamera(0);
		const Locator& loc = cam.GetLocator();
		Write((F32)loc.GetTranslation().X()); Write((F32)loc.GetTranslation().Y()); Write((F32)loc.GetTranslation().Z());
		Write((F32)loc.GetRotation().X()); Write((F32)loc.GetRotation().Y()); Write((F32)loc.GetRotation().Z()); Write((F32)loc.GetRotation().W());
	}

	Write(pRope->m_pDebugger->m_bufPos);
	Write(pRope->m_pDebugger->m_bufSize);
	Align16();
	Write(pRope->m_pDebugger->m_pBuf, pRope->m_pDebugger->m_bufSize);

	EngineComponents::GetFileSystem()->WriteSync(m_outFile, m_pBuf, m_bufPos);
	EngineComponents::GetFileSystem()->CloseSync(m_outFile);

	MsgOut("Rope dump saved to %s\n", filename+6);

	NDI_DELETE [] pBuf;
	NDI_DELETE [] pBodyHandles;
}

void Rope2DumpViewer::DebugDraw()
{
#if FINAL_BUILD && !ALLOW_ROPE_DEBUGGER_IN_FINAL
	return;
#endif

	DebugPrimTime::NoRecordJanitor jNoRecord(true);

	if (m_setCameraLoc)
	{
		CameraManager::Get().TeleportCurrent(m_cameraLoc);
		g_ndConfig.m_pDMenuMgr->SetProgPauseDebugImmediate();
		m_setCameraLoc = false;
	}

	for (U32F ii = 0; ii<m_numBodies; ii++)
	{
		RigidBody* pBody = &m_pBodies[ii];

		HavokCastFilter filter(g_havok.m_debugFilter);
		if (!filter.isCollisionEnabled(*pBody->GetHavokBody()))
			continue;

		HavokDebugDrawShape(pBody->GetHavokShape(), pBody->GetLocatorCm(), kColorCyan, CollisionDebugDrawConfig::MenuOptions(), &m_ppDebugDrawData[ii]);
	}

	for (U32F ii = 0; ii<m_numColliders; ii++)
	{
		RopeCollider* pCollider = &m_pColliders[ii];

		HavokDebugDrawShape(pCollider->m_pShape, pCollider->m_loc, kColorCyan, CollisionDebugDrawConfig::MenuOptions(), &m_ppDebugDrawData[m_numBodies+ii]);
	}
}

void Rope2DumpViewer::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	g_ropeMgr.RelocateRope(&m_rope, delta, lowerBound, upperBound);
	g_ropeMgr.RelocateDumpViewer(delta, lowerBound, upperBound);
	m_rope.Relocate(delta, lowerBound, upperBound);
	for (U32F ii = 0; ii < m_numBodies; ++ii)
	{
		m_pBodies[ii].Relocate(delta, lowerBound, upperBound);
	}
	RelocatePointer(m_pBodies, delta, lowerBound, upperBound);
	RelocatePointer(m_pRBHandleTranslations, delta, lowerBound, upperBound);
	RelocatePointer(m_pColliders, delta, lowerBound, upperBound);
}

void Rope2DumpViewer::Align16()
{
	if (m_bufPos & 0xf)
	{
		m_bufPos = (m_bufPos & ~0xf) + 0x10;
	}
}

void Rope2DumpViewer::Read(U32F& val)
{
	val = *(U32F*)(m_pBuf+m_bufPos);
	m_bufPos += sizeof(val);
}

void Rope2DumpViewer::Read(U64& val)
{
	val = *(U64*)(m_pBuf+m_bufPos);
	m_bufPos += sizeof(val);
}

void Rope2DumpViewer::Read(bool& val)
{
	val = *(bool*)(m_pBuf+m_bufPos);
	m_bufPos += sizeof(val);
}

void Rope2DumpViewer::Read(F32& val)
{
	val = *(F32*)(m_pBuf+m_bufPos);
	m_bufPos += sizeof(val);
}

hknpShape* Rope2DumpViewer::ReadHavokShape()
{
	U32F size;
	Read(size);

	Align16();

	hknpShape* pShape = hkSerialize::InplaceLoad().toObject<hknpShape>(m_pBuf + m_bufPos, size);
	m_bufPos += size;
	ALWAYS_ASSERT(pShape);

	if (pShape->getType() == hknpShapeType::COMPRESSED_MESH)
	{
		hknpCompressedMeshShape* pMeshShape = static_cast<hknpCompressedMeshShape*>(pShape);
		if (pMeshShape->m_shapeTagCodecInfo == HAVOK_SHAPE_TAG_CODEC_PAT_IN_TABLE)
		{
			U32F patPaletteSize;
			Read(patPaletteSize);
			Align16();
			pMeshShape->m_userData = (hkUlong)(m_pBuf+m_bufPos);
			m_bufPos += patPaletteSize * sizeof(U64);
		}

		U32F numBits;
		Read(numBits);
		if (numBits > 0)
		{
			Align16();
			SetMeshOuterEdges(pMeshShape, (U64*)(m_pBuf+m_bufPos), numBits);
			m_bufPos += ExternalBitArrayStorage::DetermineNumBlocks(numBits) * sizeof(U64);
		}
	}

	return pShape;
}

void Rope2DumpViewer::Write(const U8* pData, U32F size)
{
	if (size > m_bufSize - m_bufPos)
	{
		memcpy(m_pBuf+m_bufPos, pData, m_bufSize - m_bufPos);
		EngineComponents::GetFileSystem()->WriteSync(m_outFile, m_pBuf, m_bufSize);
		pData += m_bufSize - m_bufPos;
		size -= m_bufSize - m_bufPos;
		m_bufPos = 0;
	}

	if (size > m_bufSize)
	{
		U32F writeSize = size & ~(m_bufSize-1);
		EngineComponents::GetFileSystem()->WriteSync(m_outFile, pData, writeSize);
		pData += writeSize;
		size -= writeSize;
	}

	memcpy(m_pBuf+m_bufPos, pData, size);
	m_bufPos += size;
}

void Rope2DumpViewer::Write(U32F val)
{
	Write((U8*)&val, sizeof(val));
}

void Rope2DumpViewer::Write(U64 val)
{
	Write((U8*)&val, sizeof(val));
}

void Rope2DumpViewer::Write(bool val)
{
	Write((U8*)&val, sizeof(val));
}

void Rope2DumpViewer::Write(F32 val)
{
	Write((U8*)&val, sizeof(val));
}

void Rope2DumpViewer::WriteHavokShape(const hknpShape* pShape)
{
	hkArray<char> hkBuffer;
	hkSerialize::Save().withTarget(nullptr).contentsPtr(pShape, &hkBuffer);

	Write((U32F)hkBuffer.getSize());
	Align16();
	Write((U8*)&hkBuffer[0], (U32F)hkBuffer.getSize());

	if (pShape->getType() == hknpShapeType::COMPRESSED_MESH)
	{
		hknpCompressedMeshShape* pMeshShape = (hknpCompressedMeshShape*)pShape;
		if (pMeshShape->m_shapeTagCodecInfo == HAVOK_SHAPE_TAG_CODEC_PAT_IN_TABLE)
		{
			U32F patPaletteSize = 256; // We don't know how big the palette is so we just say 256 which is the max // pMeshShape->accessCollisionFilterInfoPalette().getSize() * sizeof(U64);
			Write(patPaletteSize);
			Align16();
			Write((U8*)pMeshShape->m_userData, patPaletteSize*sizeof(U64));
		}

		if (const ExternalBitArray* pOuterEdges = GetMeshOuterEdges(pMeshShape))
		{
			U32F numBits = pOuterEdges->GetMaxBitCount();
			Write(numBits);
			Align16();
			Write((U8*)pOuterEdges->GetStorage().m_block, pOuterEdges->GetNumBlocks() * sizeof(U64));
		}
		else
		{
			U32F numBits = 0;
			Write(numBits);
		}
	}
}

#endif // FINAL_BUILD

#endif