/*
* Copyright (c) 2007 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

#include "gamelib/gameplay/joint-wind-mgr.h"

#include "ndlib/frame-params.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/render/interface/loaded-texture.h"
#include "ndlib/render/interface/texture-mgr.h"
#include "ndlib/render/ndgi/submit-queue.h"
#include "ndlib/render/ndgi/submit-utils.h"
#include "ndlib/render/ngen/compute-queue-mgr.h"
#include "ndlib/render/post/post-shading-win.h"
#include "ndlib/render/render-globals.h"
#include "ndlib/render/ngen/bg-joint-wind.h"
#include "ndlib/scriptx/h/render-settings-defines.h"
#include "ndlib/settings/render-settings.h"
#include "gamelib/level/art-item-collision.h"
#include "gamelib/level/art-item-skeleton.h"

class GpuState;

JointWindManager g_jointWindMgr;

//static
ndgi::ComputeQueue JointWindManager::s_cmpQueue;

bool HasFgSkeletonJointWindData(StringId64 artGroup)
{
	if (const ArtItemCollision* pColl = ResourceTable::LookupCollision(artGroup).ToArtItem())
	{
		if (const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(pColl->m_skelId).ToArtItem())
		{
			if (pSkel->m_pJointWindParams)
			{
#if !FINAL_BUILD
				const Mat34* pInvBindPose = ndanim::GetInverseBindPoseTable(pSkel->m_pAnimHierarchy);
				for (U32F ii = 0; ii<pSkel->m_numTotalJoints; ii++)
				{
					if (Abs(Length3Sqr(pInvBindPose[ii].GetRow(0)) - 1.0f) > 0.001f || Abs(Length3Sqr(pInvBindPose[ii].GetRow(1)) - 1.0f) > 0.001f || Abs(Length3Sqr(pInvBindPose[ii].GetRow(2)) - 1.0f) > 0.001f)
					{
						MsgConScriptError("Skeleton %s has a scale in it and cannot be used for joint wind on bg vegetation\n", pSkel->GetName());
						return false;
					}
				}
#endif
				return true;
			}
		}
	}

	return false;
}

bool FillFgSkeletonJointWindData(StringId64 artGroup, SkeletonJointWindData* pData)
{
	const ArtItemCollision* pColl = ResourceTable::LookupCollision(artGroup).ToArtItem();
	if (!pColl)
	{
		ASSERT(false);
		return false;
	}
	const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(pColl->m_skelId).ToArtItem();
	if (!pSkel)
	{
		ASSERT(false);
		return false;
	}
	if (pSkel->m_pAnimHierarchy->m_numTotalJoints != pData->m_numJoints)
	{
		ASSERT(false);
		return false;
	}
	if (!pSkel->m_pJointWindParams)
	{
		ASSERT(false);
		return false;
	}

	const ndanim::DebugJointParentInfo* pParentInfo = ndanim::GetDebugJointParentInfoTable(pSkel->m_pAnimHierarchy);

	ExternalBitArray jointWindMask;
	jointWindMask.InitNoAssign(pData->m_numJoints, pSkel->m_pJointWindBitArray);

	U32 iWindJoint = 0;
	for (U32F ii = 0; ii < pData->m_numJoints; ii++)
	{
		ASSERT(pParentInfo[ii].m_parent < 0 || pParentInfo[ii].m_parent < pData->m_numJoints);
		pData->m_pJointData[ii].m_parentIndex = pParentInfo[ii].m_parent;
		if (jointWindMask.IsBitSet(ii))
		{
			pData->m_pJointData[ii].m_stiffness = pSkel->m_pJointWindParams[iWindJoint].m_stiffness;
			iWindJoint++;
		}
		else
		{
			pData->m_pJointData[ii].m_stiffness = 1.0f;
		}
	}

	pData->m_pInvBindPose = ndanim::GetInverseBindPoseTable(pSkel->m_pAnimHierarchy);

	return true;
}

JointWindManager::JointWindManager() : m_cmpContext("JointWindManager")
{
}

void JointWindManager::Init()
{
	s_cmpQueue.Create(1 * 1024);
	g_computeQueueMgr.MapComputeQueue("joint-wind", &s_cmpQueue, 2, 3);

	m_hOutBuffer.CreateRwStructuredBuffer(kAllocVegetation, sizeof(JointWindDataOut), kMaxWindJoints, ndgi::kUsageDefault);
	m_hOutBuffer.SetDebugName("windjoints-out-buffer");

	// Register shader validation here.
	//ndgi::ComputeShader *pShader = g_postShading.GetCsByIndex(kCsJointWind);
	//ndgi::SetInGameShaderValidator(pShader->Get()->m_cs, ValidateWetFxCs);
	
	m_inIndex = 0;
	m_pInData = nullptr;
	m_pOutData = nullptr;

	m_cmpLastKickFrame = -1;
	m_outGameFrame = -1;

	BgJointWind::s_pHasFgSkeletonJointWindDataFunc = &HasFgSkeletonJointWindData;
	BgJointWind::s_pFillFgSkeletonJointWindDataFunc = &FillFgSkeletonJointWindData;
}

void JointWindManager::Close()
{
	m_hOutBuffer.Release();
	m_cmpContext.Release();
	g_computeQueueMgr.UnmapComputeQueue(&s_cmpQueue);
	s_cmpQueue.Release();
}

JointWindDataIn* JointWindManager::GetInputBuffer(U32F numJoints, I32F& index)
{
	AtomicLockJanitor jj(&m_lock, FILE_LINE_FUNC);

	index = m_inIndex;
	m_inIndex += numJoints;

	if (!m_pInData || m_inIndex > kMaxWindJoints)
	{
		index = -1;
		return nullptr;
	}

	return &m_pInData[index];
}

JointWindDataOut* JointWindManager::GetOutputBuffer(I32F index)
{
	if (index < 0 || !m_pOutData)
		return nullptr;

	return &m_pOutData[index];
}

void JointWindManager::PreUpdate()
{
	PROFILE(Processes, JointWindMgrPreUpdate);

	I64 currentFrame = GetCurrentFrameNumber();

	GAMEPLAY_ASSERT(m_cmpLastKickFrame == -1 || currentFrame == m_cmpLastKickFrame + 1);

	if (m_cmpLastKickFrame != -1 && currentFrame == m_cmpLastKickFrame + 1)
	{
		m_cmpContext.Wait();
		m_pOutData = (JointWindDataOut*)m_hOutBuffer.GetBaseAddr();
		m_cmpLastKickFrame = -1;
	}

	if (EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
	{
		return;
	}

	if (m_outGameFrame != EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused)
	{
		m_pOutData = nullptr;
	}

	m_hInBuffer.CreateRwStructuredBuffer(kAllocGpuRing, sizeof(JointWindDataIn), kMaxWindJoints, ndgi::kUsageDefault);
	m_pInData = (JointWindDataIn*)m_hInBuffer.GetBaseAddr();
	m_inIndex = 0;
}

void JointWindManager::PostUpdate(RenderFrameParams const *pParams)
{
	//if (EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
	//{
	//	return;
	//}

	if (m_pInData && m_inIndex > 0)
	{
		const DC::RenderSettings* pRenderSettings = &g_renderSettings[0];
		const DC::RenderSettingsWind* pWindSettings = &pRenderSettings->m_wind;

		JointWindConst* pConst = NDI_NEW(kAllocGpuRing, kAlign16) JointWindConst;
		pConst->m_numJoints = m_inIndex;
		// We advance the time 1 frame forward because that's when we will use the results
		pConst->m_time = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime().ToSeconds() + EngineComponents::GetNdFrameState()->m_targetSecondsPerFrame;
		Vector windDir = SafeNormalize(Vector(pWindSettings->m_jointWindDirectionX, 0.0f, pWindSettings->m_jointWindDirectionZ), kUnitXAxis);
		pConst->m_windDirection[0] = (F32)windDir.X();
		pConst->m_windDirection[1] = (F32)windDir.Y();
		pConst->m_windDirection[2] = (F32)windDir.Z();
		pConst->m_windSpeed = pWindSettings->m_jointWindSpeed;
		pConst->m_turbulence = pWindSettings->m_jointWindTurbulence;
		pConst->m_windMultiplier = pWindSettings->m_windIntensityMultiplier;

		struct JointWindBuffers
		{
			ndgi::VSharp m_in;
			ndgi::VSharp m_out;
		};

		JointWindBuffers* pBuffers = NDI_NEW(kAllocGpuRing, kAlign16) JointWindBuffers;

		pBuffers->m_in = m_hInBuffer.GetVSharp(ndgi::kReadOnly);
		pBuffers->m_out = m_hOutBuffer.GetVSharp(ndgi::kSystemCoherent);

		struct SrtData : public ndgi::ValidSrt<SrtData>
		{
			JointWindConst *m_pConsts;
			JointWindBuffers *m_pBuffs;

			void Validate(RenderFrameParams const *params, const GpuState *pGpuState)
			{
				ValidatePtr(params, pGpuState, m_pConsts);
				ValidatePtr(params, pGpuState, m_pBuffs);
				m_pBuffs->m_in.Validate(params, pGpuState);
				m_pBuffs->m_out.Validate(params, pGpuState);
			}
		};

		SrtData srtData;
		srtData.m_pConsts = pConst;
		srtData.m_pBuffs = pBuffers;

		U32F numThreadGroups = (pConst->m_numJoints + kNumCsThreads - 1) / kNumCsThreads;

		m_cmpContext.Create(2048, kAllocGpuRing);
		m_cmpContext.Open();

		ndgi::ComputeShader *pShader = g_postShading.GetCsByIndex(kCsJointWind);
		m_cmpContext.SetCsShader(pShader);
		m_cmpContext.SetCsSrt(&srtData, sizeof(SrtData));
		m_cmpContext.SetCsSrtValidator(SrtData::ValidateSrt);
		m_cmpContext.Dispatch(numThreadGroups, 1, 1, (ndgi::Label32*)nullptr);

		m_cmpContext.Close(ndgi::kCacheActionWbInvL2Volatile, true);

		SubmitQueue<1> sq;
		sq.Add(m_cmpContext, s_cmpQueue);
		ProcessSubmitQueue(&sq);

		m_cmpLastKickFrame = GetCurrentFrameNumber();
		m_outGameFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused + 1;

		m_pOutData = nullptr;

		if (m_inIndex > kMaxWindJoints)
		{
			MsgCon("Too many joints with wind (%i)!\n", m_inIndex);
		}
	}

	m_hInBuffer.Release();

	m_pInData = nullptr;
	m_inIndex = 0;
}
