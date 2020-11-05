/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 *
 */

#include "ndlib/anim/anim-mgr-jobs.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/nd-anim-plugins.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/nd-system.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process.h"
#include "ndlib/profiling/profile-cpu.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/scriptx/h/skel-retarget-defines.h"

#include "gamelib/camera/camera-manager.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"

#include <orbisanim/util.h>

/// --------------------------------------------------------------------------------------------------------------- ///
class CreateAnimCmdListData
{
public:

	CreateAnimCmdListData()
	{
		m_pAnimData = nullptr;
		m_pSkeleton = nullptr;

 		m_pInputControls = nullptr;

		m_pPluginJointSet = nullptr;
		m_pObjXform = nullptr;
	}

	FgAnimData* m_pAnimData;
	const ArtItemSkeleton* m_pSkeleton;

	JointSet* m_pPluginJointSet;
	Transform* m_pObjXform;

 	float* m_pInputControls;					// Input
};

/************************************************************************/
/* MOVE THIS ONCE FULLY ABSTRACTED - CGY                                */
/************************************************************************/
/// --------------------------------------------------------------------------------------------------------------- ///
static void PerformAnimDataCallback(U32F updateType, FgAnimData* pAnimData, bool isPaused = false)
{
	//g_processCriticalSection.EnableCriticalSections(FILE_LINE_FUNC);

	// @ASYNC-TODO: We should probably mark the process as updating here and assert
	// that it's not already updating when we call these callback functions.
	pAnimData->m_hProcess.ClearTrap();

	NdGameObject* pProcess = NdGameObject::FromProcess(pAnimData->m_hProcess.ToMutableProcess());

#ifndef FINAL_BUILD	// Debug feature to track process dereference of currently updating processes
	Process::SetUpdatingProcess(pProcess);
#endif

	if (pProcess && !pProcess->IsProcessDead())
	{
		switch (updateType)
		{
		case FgAnimData::kEnableAsyncPostAnimUpdate:
			{
				// Take requested transitions, advance frames in animations and more.
				Process::ActivateContext(pProcess, FILE_LINE_FUNC);
				pProcess->PostAnimUpdate_Async();
				Process::DeactivateContext(FILE_LINE_FUNC);
			}
			break;

		case FgAnimData::kEnableAsyncPostAnimBlending:
			{
				Process::ActivateContext(pProcess, FILE_LINE_FUNC);
				pProcess->PostAnimBlending_Async();
				Process::DeactivateContext(FILE_LINE_FUNC);
			}
			break;

		case FgAnimData::kEnableAsyncPostJointUpdate:
			{
				Process::ActivateContext(pProcess, FILE_LINE_FUNC);
				if (isPaused)
					pProcess->PostJointUpdatePaused_Async();
				else
					pProcess->PostJointUpdate_Async();
				Process::DeactivateContext(FILE_LINE_FUNC);
			}
			break;

		case FgAnimData::kEnableClothUpdate:
			{
				Process::ActivateContext(pProcess, FILE_LINE_FUNC);
				pProcess->ClothUpdate_Async();
				Process::DeactivateContext(FILE_LINE_FUNC);
			}
			break;

		case FgAnimData::kAllowAutomaticObjXformUpdate:
			{
				pAnimData->SetXform(pProcess->GetLocator());
			}
			break;
		}
	}

#ifndef FINAL_BUILD
	Process::SetUpdatingProcess(EngineComponents::GetProcessMgr()->m_pIdleProcess);
#endif

	pAnimData->m_hProcess.SetTrap();

	//g_processCriticalSection.DisableCriticalSections(FILE_LINE_FUNC);
}

		
/// --------------------------------------------------------------------------------------------------------------- ///
static void CreateAnimCmdsForObject(AnimCmdList* pAnimCmdList, const CreateAnimCmdListData* pData)
{
	PROFILE(Animation, CreateAnimCmdsForObject);

	ANIM_ASSERT(pData->m_pAnimData->m_pAnimControl);

	ANIM_ASSERT(pData->m_pSkeleton);

	const ArtItemSkeleton* pSkeleton = pData->m_pSkeleton;
	const ndanim::JointHierarchy* pJointHierarchy = pSkeleton->m_pAnimHierarchy;
	FgAnimData* pAnimData = pData->m_pAnimData;

	{
		AnimCmdGenContext segmentContext;
		segmentContext.m_pAnimData	  = pData->m_pAnimData;
		segmentContext.m_pAnimateSkel = pSkeleton;

		pAnimCmdList->AddCmd(AnimCmd::kBeginSegment);

		{
			// Process all joints in groups. The SPU's can't handle huge numbers of joints in one go.
			pAnimCmdList->AddCmd(AnimCmd::kBeginAnimationPhase);

			pAnimData->m_pAnimControl->CreateAnimCmds(segmentContext, pAnimCmdList);

			// Merge work buffer allocations for all jointParams groups, floatChannel groups
			pAnimCmdList->AddCmd(AnimCmd::kEndAnimationPhase);
		}

		{
			// batch set-up commands to prepare for primary set processing
			pAnimCmdList->AddCmd_EvaluateJointHierarchyCmds_Prepare(pData->m_pInputControls);

			{
				// We now have the option to run plugins like joint modifications and IK
				{
					if (pAnimData->m_pPluginParams)
					{
						if (pData->m_pPluginJointSet)
						{
							const float rootScale = pAnimData->m_jointCache.GetJointParamsLs()->m_scale.X();
							pAnimCmdList->AddCmd_InitializeJointSet_RigPhase(pData->m_pPluginJointSet, rootScale);
						}

						const FgAnimData::PluginParams* pPluginParams = pAnimData->m_pPluginParams;
						while (pPluginParams->m_pluginName != INVALID_STRING_ID_64)
						{
							if (pPluginParams->m_enabled)
							{
								// technically we need to run the input control drivers here too, to properly feed the
								// SDK network... however, this is ONLY necessary if our plugins READ the SQTs of the
								// HELPER joints that are driven by SDKs... which they never do

								pAnimCmdList->AddCmd_EvaluateRigPhasePlugin(pPluginParams->m_pluginName,
																				 pPluginParams->m_pPluginData,
																				 pPluginParams->m_pluginDataSize,
																				 pData->m_pPluginJointSet);
							}

							pPluginParams++;
							ANIM_ASSERT((pAnimData->m_pPluginParams - pPluginParams) < 16);
						}

						if (pData->m_pPluginJointSet)
						{
							pAnimCmdList->AddCmd_CommitJointSet_RigPhase(pData->m_pPluginJointSet, g_animOptions.m_jointSetBlend);
						}
					}
				}

				// batch our joint hierarchy SDK and parenting commands (in that order)
				pAnimCmdList->AddCmd_EvaluateJointHierarchyCmds_Evaluate();
			}
		}

		pAnimCmdList->AddCmd(AnimCmd::kEndSegment);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void CreatePostRetargetCmdsForObject(const AnimCmdList* pAnimCmdList,
	AnimCmdList* pPostRetargetAnimCmdList,
											const FgAnimData* pAnimData,
											const ndanim::AnimatedJointPose& inputPose/*,
											const int segmentIndex*/)
{
	PROFILE(Animation, CreatePostRetargetCmdsForObject);

	const ArtItemSkeleton* pSkeleton = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pImposterSkeleton = pAnimData->m_animateSkelHandle.ToArtItem();

	ANIM_ASSERT(pImposterSkeleton);
	ANIM_ASSERT(pSkeleton);

	{


		pPostRetargetAnimCmdList->AddCmd(AnimCmd::kBeginSegment);

		{
			PROFILE(Animation, AnimateObject_PRP);

			pPostRetargetAnimCmdList->AddCmd(AnimCmd::kBeginAnimationPhase);

			pPostRetargetAnimCmdList->AddCmd_BeginProcessingGroup();
			pPostRetargetAnimCmdList->AddCmd_EvaluateBindPose(0);
			pPostRetargetAnimCmdList->AddCmd(AnimCmd::kEndProcessingGroup);

			pPostRetargetAnimCmdList->AddCmd(AnimCmd::kEndAnimationPhase);

			pPostRetargetAnimCmdList->AddCmd_EvaluatePostRetarget(pImposterSkeleton, pSkeleton, inputPose/*, segmentIndex*/);
		}

		// Copy all the rig-phase commands from the original command list
		const U32* pCmdBuf = pAnimCmdList->GetBuffer();
		const U32 numWordsUsed = pAnimCmdList->GetNumWordsUsed();
		U32 curWord = 0;
		const AnimCmd* pCmd = (const AnimCmd*)(pCmdBuf);
		while (pCmd->m_type != AnimCmd::kEndAnimationPhase)
		{
			curWord += pCmd->m_numCmdWords;
			pCmd = (const AnimCmd*)(pCmdBuf + curWord);
		}

		curWord += pCmd->m_numCmdWords;
		pCmd = (const AnimCmd*)(pCmdBuf + curWord);
		while (curWord < numWordsUsed)
		{
			pPostRetargetAnimCmdList->CopyCmd(pCmd);
			curWord += pCmd->m_numCmdWords;
			pCmd = (const AnimCmd*)(pCmdBuf + curWord);
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
static void FillIdentityRigOutputs(const ArtItemSkeleton* pSkeleton, Mat34* pSkinningMats, float* pOutputControls)
{
	ANIM_ASSERT(pSkeleton->m_pAnimHierarchy);

	const ndanim::JointHierarchy* pAnimHierarchy = pSkeleton->m_pAnimHierarchy;
	const U32F numTotalJoints	 = pSkeleton->m_numTotalJoints;
	const U32F numFloatChannels	 = pAnimHierarchy->m_numFloatChannels;
	const U32F numOutputControls = pAnimHierarchy->m_numOutputControls;

	if (numTotalJoints > 0)
	{
		ANIM_ASSERT(pSkinningMats);
		
		for (int i = 0; i < numTotalJoints; ++i)
		{
			pSkinningMats[i] = Mat34(kIdentity);
		}
	}

	if (numOutputControls > 0)
	{
		ANIM_ASSERT(pOutputControls);
		const float* pDefs = (const float*)(((char*)pAnimHierarchy) + pAnimHierarchy->m_defaultFloatChannelsOffset);
		memcpy(pOutputControls, pDefs, sizeof(float) * numOutputControls);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void FillIdentityRigOutputs(FgAnimData* pAnimData, const ArtItemSkeleton* pSkeleton)
{
	ANIM_ASSERT(pAnimData);
	ANIM_ASSERT(pSkeleton);

	const AnimExecutionContext* pCtx = GetAnimExecutionContext(pAnimData);
	FillIdentityRigOutputs(pSkeleton,
						   (Mat34*)pCtx->m_pAllSkinningBoneMats,
						   pCtx->m_pAllOutputControls);
}



/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT_GLOBAL(AsyncObjectUpdate_AnimStep_Job)
{
	PROFILE_AUTO(Animation);

	const AsyncUpdateJobData* pJobData = (const AsyncUpdateJobData*)jobParam;
	if (!pJobData->m_gameplayIsPaused)
	{
		AnimControl* pAnimControl = pJobData->m_pAnimControl;
		FgAnimData* pAnimData = pJobData->m_pAnimData;
		const float deltaTime = (pAnimData->m_disabledDeltaTime + pJobData->m_animSysDeltaTime) * pAnimData->m_animClockScale;

		// Step the animation states and take transitions
		if (pAnimControl)
		{
			const Process* pProcess = pAnimData->m_hProcess.ToProcess();

			if (pProcess)
				Process::ActivateContext(pProcess, FILE_LINE_FUNC);

			pAnimControl->BeginStep(deltaTime);
			pAnimControl->FinishStep(deltaTime);

			if (pProcess)
				Process::DeactivateContext(FILE_LINE_FUNC);
		}

		if (!pJobData->m_gameplayIsPaused)
		{
			// Perform object callbacks if needed
			if (pAnimData->m_flags & FgAnimData::kEnableAsyncPostAnimUpdate)
			{
				PerformAnimDataCallback(FgAnimData::kEnableAsyncPostAnimUpdate, pAnimData);
			}

			// If the object had a post anim blending pass we want to propagate the potentially moved locator to the anim data.
			if (pAnimData->m_flags & FgAnimData::kAllowAutomaticObjXformUpdate)
			{
				PerformAnimDataCallback(FgAnimData::kAllowAutomaticObjXformUpdate, pAnimData);
			}
		}
	}

	// Decrement the object counter so that next stage job for this object is allowed to run
	if (pJobData->m_pObjectWaitCounter)
	{
		I64 oldCounterValue = pJobData->m_pObjectWaitCounter->Decrement();
		// if this counter is only waiting for AnimStep to be done, then free it here
		// otherwise allow JointUpdate to free it later
		if (oldCounterValue == 1)
			ndjob::FreeCounter(pJobData->m_pObjectWaitCounter);
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
static void GenerateAnimCommandsForObject(AnimExecutionContext* pCtx,
										  FgAnimData* pAnimData,
										  const ArtItemSkeleton* pSkelToUse,
										  float* pInputControls,
										  const AsyncUpdateJobData* pJobData)
{
	const U32 numSegments = pSkelToUse->m_numSegments;

	//////////////////////////////////////////////////////////////////////////
	// Create the command list
	//////////////////////////////////////////////////////////////////////////
	CreateAnimCmdListData data;
	data.m_pAnimData = pAnimData;
	data.m_pSkeleton = pSkelToUse;					// The skeleton we will evaluate all the anim commands on
	data.m_pPluginJointSet = pAnimData->m_pPluginJointSet;
	data.m_pObjXform = &pAnimData->m_objXform;
	data.m_pInputControls = pInputControls;			// Input to the rig evaluation



	CreateAnimCmdsForObject(&pCtx->m_animCmdList, &data);
	ANIM_ASSERT(pCtx->m_animCmdList.GetNumWordsUsed() < pCtx->m_animCmdList.GetMaxWords());


#ifndef FINAL_BUILD
		const bool debugPrint = g_animOptions.m_printCommandList && DebugSelection::Get().IsProcessOrNoneSelected(pAnimData->m_hProcess);
		if (debugPrint)
		{
			DoutBase* pOutput = nullptr;

			if (g_animOptions.m_printCommandListToMsgCon)
			{
				pOutput = GetMsgOutput(kMsgConPauseable);
			}
			else if (!pJobData->m_gameplayIsPaused)
			{
				pOutput = GetMsgOutput(kMsgAnim);
			}

			if (pOutput)
			{
				PrintAnimCmdList(&pCtx->m_animCmdList, pOutput);
			}
		}
#endif
}



/// --------------------------------------------------------------------------------------------------------------- ///
static void GetRetargetedSegments(const ArtItemSkeleton* pSrcSkel,
								  const ArtItemSkeleton* pDestSkel,
								  int destSegmentIndex,
								  SegmentMask& outRetargetMask)
{
	outRetargetMask.SetAllBits();

	const SkelTable::RetargetEntry* pRetargetEntry = SkelTable::LookupRetarget(pSrcSkel->m_skelId, pDestSkel->m_skelId);
	if (!pRetargetEntry || !pRetargetEntry->m_dstToSrcSegMapping)
		return;

	const int srcSegmentCount = pSrcSkel->m_numSegments;
	const int destSegmentCount = pDestSkel->m_numSegments;

	outRetargetMask.ClearAllBits();

	for (int iSegment = 0; iSegment < destSegmentCount; iSegment++)
	{
		if (pRetargetEntry->m_dstToSrcSegMapping[iSegment].AreAllBitsClear())
			continue;

		SegmentMask::BitwiseOr(&outRetargetMask, outRetargetMask, GetDependentSegmentMask(pDestSkel, iSegment));
	}

	if (pRetargetEntry->m_pSkelRetarget->m_noPostRetargetSegments)
	{
		SegmentMask disabledSegments;
		disabledSegments.SetData(pRetargetEntry->m_pSkelRetarget->m_noPostRetargetSegments);

		SegmentMask::BitwiseAndComp(&outRetargetMask, outRetargetMask, disabledSegments);
	}

	if (g_animOptions.m_limitPostRetargetingToFirstSegment)
	{
		outRetargetMask.ClearAllBits();
		outRetargetMask.SetBit(0);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void GetAnimMaskForPostRetarget(const ArtItemSkeleton* pSrcSkel,
									   const ArtItemSkeleton* pDestSkel,
									   const U32& retargetMask,
									   U32& outAnimMask)
{
	outAnimMask = 0;

	const SkelTable::RetargetEntry* pRetargetEntry = SkelTable::LookupRetarget(pSrcSkel->m_skelId, pDestSkel->m_skelId);

	if (pRetargetEntry && pRetargetEntry->m_jointRetarget && pRetargetEntry->m_dstToSrcSegMapping)
	{
		const int srcSegmentCount = pSrcSkel->m_numSegments;
		const int destSegmentCount = pDestSkel->m_numSegments;

		for (int iSegment = 0; iSegment < destSegmentCount; iSegment++)
		{
			if (retargetMask & (1 << iSegment))
			{
				const int numSrcSegments = pRetargetEntry->m_dstToSrcSegMapping[iSegment].GetMaxBitCount();
				for (int iSrcSegment = 0; iSrcSegment < numSrcSegments; ++iSrcSegment)
				{
					if (pRetargetEntry->m_dstToSrcSegMapping[iSegment].IsBitSet(iSrcSegment))
					{
						outAnimMask |= (1 << iSrcSegment);
					}
				}
			}
		}
	}
	else if ((pSrcSkel == pDestSkel) || (pSrcSkel->m_hierarchyId == pDestSkel->m_hierarchyId))
	{
		outAnimMask = retargetMask;
	}
	else
	{
		for (int i = 0; i < pSrcSkel->m_numSegments; ++i)
		{
			outAnimMask |= (1 << i);
		}
	}
}




/// --------------------------------------------------------------------------------------------------------------- ///
static const char* DevKitOnly_GetLookName(const Process* pProc)
{
	STRIP_IN_FINAL_BUILD_VALUE("");

	const NdGameObject* pGo = NdGameObject::FromProcess(pProc);
	if (!pGo)
		return "<null>";

	return DevKitOnly_StringIdToString(pGo->GetLookId());
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void PerformAnimBlend(const AsyncUpdateJobData* pJobData, bool shouldAnimateThisObject)
{
	AnimControl* pAnimControl = pJobData->m_pAnimControl;
	FgAnimData* pAnimData = pJobData->m_pAnimData;

	const float deltaTime = (pAnimData->m_disabledDeltaTime + pJobData->m_animSysDeltaTime) * pAnimData->m_animClockScale;

	// const bool shouldDisableAnimation = g_animOptions.m_disableAnimation || pAnimData->m_flags & FgAnimData::kDisableAnimation;

	const ArtItemSkeleton* pCurSkel	 = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem();

	ANIM_ASSERTF(pCurSkel,
				 ("Process '%s' [look: %s] has null object skeleton",
				  DevKitOnly_StringIdToString(pAnimData->m_hProcess.GetUserId()),
				  DevKitOnly_GetLookName(pAnimData->m_hProcess.ToProcess())));
	ANIM_ASSERTF(pAnimSkel,
				 ("Process '%s' [look: %s] has null anim skeleton",
				  DevKitOnly_StringIdToString(pAnimData->m_hProcess.GetUserId()),
				  DevKitOnly_GetLookName(pAnimData->m_hProcess.ToProcess())));

	const bool needsPostRetargeting = (pAnimSkel->m_hierarchyId != pCurSkel->m_hierarchyId) || g_animOptions.m_forcePostRetargeting;

	if (!pJobData->m_gameplayIsPaused)
		pAnimData->CheckPendingAnimConfigChange();
 	FgAnimData::AnimSourceMode sourceMode = pAnimData->GetAnimSourceMode(0);
 	FgAnimData::AnimResultMode resultMode = pAnimData->GetAnimResultMode(0);

	const U32 hasOutputControls = pCurSkel->m_pAnimHierarchy->m_numOutputControls > 0;
	const U32 numSegments = pCurSkel->m_numSegments;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	const int animDataIndex = EngineComponents::GetAnimMgr()->GetAnimDataIndex(pAnimData);
	const AnimExecutionContext* pPrevAnimCtx = GetPrevFrameAnimExecutionContext(pAnimData);

	AnimExecutionContext* pAnimCtx = AllocateAnimContextForThisFrame(animDataIndex);
	pAnimCtx->Init(pCurSkel,
				   &pAnimData->m_objXform,
				   const_cast<ndanim::JointParams*>(pAnimData->m_jointCache.GetJointParamsLs()),
				   pAnimData->m_jointCache.GetJointTransformsForOutput(),
				   const_cast<float*>(pAnimData->m_jointCache.GetInputControls()),
				   const_cast<float*>(pAnimData->m_jointCache.GetOutputControls()),
				   pAnimData->m_pPersistentData);

	// Hook up the plugin funcs
	pAnimCtx->m_pPluginJointSet = pAnimData->m_pPluginJointSet;
	pAnimCtx->m_pAnimPhasePluginFunc = pJobData->m_pAnimPhasePluginFunc;
	pAnimCtx->m_pRigPhasePluginFunc = pJobData->m_pRigPhasePluginFunc;

	U32 outputMask = 0;
	if (sourceMode != FgAnimData::kAnimSourceModeNone && g_animOptions.m_alwaysAnimateAllSegments)
	{
		outputMask |= AnimExecutionContext::kOutputJointParamsLs;
		outputMask |= AnimExecutionContext::kOutputFloatChannels;
		outputMask |= AnimExecutionContext::kOutputTransformsOs;
	}

	if (resultMode & FgAnimData::kAnimResultJointTransformsOs)
	{
		outputMask |= AnimExecutionContext::kOutputTransformsOs;
	}
	if (resultMode & FgAnimData::kAnimResultJointParamsLs)
	{
		outputMask |= AnimExecutionContext::kOutputJointParamsLs;
		outputMask |= AnimExecutionContext::kOutputFloatChannels;
	}
	if (resultMode & FgAnimData::kAnimResultSkinningMatricesOs)
	{
		outputMask |= AnimExecutionContext::kOutputSkinningMats;
		outputMask |= AnimExecutionContext::kOutputOutputControls;
	}

	if (shouldAnimateThisObject)
	{
		// Do the main animation pass
		if (pAnimData->m_userAnimationPassCallback[0])
		{
			ANIM_ASSERT(pCurSkel == pAnimSkel);
			pAnimData->m_userAnimationPassCallback[0](pAnimData, pJobData->m_animSysDeltaTime);

			ANIM_ASSERT(pAnimCtx->m_pAllSkinningBoneMats);

			// I don't think we ever do this
			// If we do we should clearly define what is being updated
			ANIM_ASSERT(false); 
			pAnimCtx->m_processedSegmentMask_JointParams = (1 << 0);	// Joint cache is updated

			// Propagate the skinning matrix pointers to the segment data
			for (U32 segmentIndex = 0; segmentIndex < pAnimCtx->m_pSkel->m_numSegments; ++segmentIndex)
			{
				ANIM_ASSERT(pAnimCtx->m_pAllSkinningBoneMats);
				ANIM_ASSERT(!pAnimCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats);
				const U32F firstJoint = ndanim::GetFirstJointInSegment(pAnimCtx->m_pSkel->m_pAnimHierarchy, segmentIndex);
				pAnimCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats = pAnimCtx->m_pAllSkinningBoneMats + firstJoint * 3 /*Vec4sPerMat34*/;
				pAnimCtx->m_processedSegmentMask_SkinningMats |= (1 << segmentIndex);
				ANIM_ASSERT(pAnimCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats);
			}
		}
		else if (pAnimControl)
		{
			// Only animate the 'base' segment unless we are generating skinning matrices
			U32 requiredSegmentMask = (1 << 0);
			if (((outputMask & AnimExecutionContext::kOutputSkinningMats) && ((pAnimData->m_flags & FgAnimData::kForceAnimateAllSegments) || g_animOptions.m_createSkinningForAllSegments)) || 
				g_animOptions.m_alwaysAnimateAllSegments)
			{
				requiredSegmentMask |= 0xFFFFFFFF;
			}

			if (outputMask & (AnimExecutionContext::kOutputJointParamsLs | AnimExecutionContext::kOutputTransformsOs))
			{
				// It does not seem correct to validate transforms now before they get update but this is to prevent nast race condition problem:
				// Sometimes somebody reads from joint cache while we're animating and if joint cache thinks the transforms are not up to date 
				// it will try to update it while it's being writen to from animation.
				// Whoever reads the joint cache mights still get a garbage but at least this way we will not be stuck with garbage in joint cache
				// for the rest of the frame
				pAnimData->m_jointCache.ValidateAllTransforms();
			}

			float* pInputControls = const_cast<float*>(pAnimData->m_jointCache.GetInputControls());
			GenerateAnimCommandsForObject(pAnimCtx, pAnimData, pCurSkel, pInputControls, pJobData);

			//////////////////////////////////////////////////////////////////////////
			// POST-RETARGETING SETUP: If we need post-retargeting, then swap the skeleton and override the output to generate joint params.
			//////////////////////////////////////////////////////////////////////////
			ndanim::AnimatedJointPose postRetargetInputPose;
			U32 savedRequiredSegmentMask = 0;
			if (needsPostRetargeting)
			{
				savedRequiredSegmentMask = requiredSegmentMask;

				// Only animate the segments needed for the retarget.
				U32 animSkelRequiredSegmentMask = 0;
				GetAnimMaskForPostRetarget(pAnimSkel, pCurSkel, requiredSegmentMask, animSkelRequiredSegmentMask);

				// Slight hack... as the copy constructor is private due to the NdAtomic lock
				AnimExecutionContext retargetAnimCtx;
				memcpy(&retargetAnimCtx, pAnimCtx, sizeof(AnimExecutionContext));
				retargetAnimCtx.m_lock.Set(0);

				// Reconfigure the anim exec context to use the anim skeleton
				retargetAnimCtx.m_pSkel = pAnimSkel;
				requiredSegmentMask = animSkelRequiredSegmentMask;

				// We need some temporary output for the joint params until we perform the post retarget step
				postRetargetInputPose.m_pJointParams = NDI_NEW (kAlign16) ndanim::JointParams[pAnimSkel->m_pAnimHierarchy->m_numAnimatedJoints];
				postRetargetInputPose.m_pFloatChannels = NDI_NEW (kAlign16) float[pAnimSkel->m_pAnimHierarchy->m_numOutputControls /*m_numFloatChannels*/];		// We have to output all OutputControls for now - CGY
				postRetargetInputPose.m_pValidBitsTable = nullptr; // NDI_NEW(kAlign16) ndanim::ValidBits[pSkelToUse->m_pAnimHierarchy->m_numChannelGroups];

				retargetAnimCtx.m_pSegmentData = NDI_NEW (kAlign128) AnimExecutionSegmentData[pAnimSkel->m_numSegments];
				retargetAnimCtx.m_pAllOutputControls = postRetargetInputPose.m_pFloatChannels;

				retargetAnimCtx.m_allowRigPhasePlugins = false;

				U32 outputControlIndexBase = 0;
				for (int segmentIndex = 0; segmentIndex < pAnimSkel->m_numSegments; ++segmentIndex)
				{
					const U32F firstAnimJoint = ndanim::GetFirstAnimatedJointInSegment(pAnimSkel->m_pAnimHierarchy, segmentIndex);

					retargetAnimCtx.m_pSegmentData[segmentIndex].m_pJointParams = postRetargetInputPose.m_pJointParams + firstAnimJoint;
					retargetAnimCtx.m_pSegmentData[segmentIndex].m_pOutputControls = retargetAnimCtx.m_pAllOutputControls + outputControlIndexBase;

					outputControlIndexBase += ndanim::GetNumOutputControlsInSegment(pAnimSkel->m_pAnimHierarchy, segmentIndex);
				}

				ProcessRequiredSegments(requiredSegmentMask,
										AnimExecutionContext::kOutputJointParamsLs | AnimExecutionContext::kOutputFloatChannels,
										&retargetAnimCtx,
										pPrevAnimCtx);
			}
			else
			{
				ProcessRequiredSegments(requiredSegmentMask, outputMask, pAnimCtx, pPrevAnimCtx);
			}

			// Now that we're done animating set correct valid bits in joint cache
			if (outputMask & AnimExecutionContext::kOutputJointParamsLs)
			{
				pAnimData->m_flags &= ~FgAnimData::kJointParamsLsDirty;
			}
			else
			{
				pAnimData->m_flags |= FgAnimData::kJointParamsLsDirty;
			}
			if (outputMask & AnimExecutionContext::kOutputTransformsOs)
			{
				pAnimData->m_jointCache.ValidateAllTransforms();
			}
			else
			{
				pAnimData->m_jointCache.InvalidateAllTransforms();
			}
			pAnimData->m_jointCache.InvalidateAllWsLocs();

			if (outputMask & (AnimExecutionContext::kOutputFloatChannels | AnimExecutionContext::kOutputOutputControls))
			{
				pAnimData->m_jointCache.ValidateOutputControls();
			}
			else
			{
				pAnimData->m_jointCache.InvalidateOutputControls();
			}

			pAnimCtx->m_dependencyTableValid = true;

			if (needsPostRetargeting)
			{
				AnimCmdList postRetargetAnimCmdList;
				const U64 kPostRetargetAnimCmdMemSize = 256 * sizeof(AnimCmd);
				U32* pPostRetargetAnimCmdMemory = NDI_NEW (kAlign128) U32[kPostRetargetAnimCmdMemSize / sizeof(U32)];
				postRetargetAnimCmdList.Init(pPostRetargetAnimCmdMemory, kPostRetargetAnimCmdMemSize);

				AnimExecutionContext retargetAnimCtx;
				memcpy(&retargetAnimCtx, pAnimCtx, sizeof(AnimExecutionContext));
				retargetAnimCtx.m_lock.Set(0);

				CreatePostRetargetCmdsForObject(&pAnimCtx->m_animCmdList,
												&postRetargetAnimCmdList,
												pAnimData,
												postRetargetInputPose);

				// Override the command list with our new one that skips the anim phase
				retargetAnimCtx.m_animCmdList = postRetargetAnimCmdList;
				retargetAnimCtx.m_allowAnimPhasePlugins = false;
				retargetAnimCtx.m_allowRigPhasePlugins = true;
				ProcessRequiredSegments(savedRequiredSegmentMask, outputMask, &retargetAnimCtx, pPrevAnimCtx);

				// Transfer over the processing status
				pAnimCtx->m_processedSegmentMask_JointParams = retargetAnimCtx.m_processedSegmentMask_JointParams;
				pAnimCtx->m_processedSegmentMask_FloatChannels = retargetAnimCtx.m_processedSegmentMask_FloatChannels;
				pAnimCtx->m_processedSegmentMask_Transforms = retargetAnimCtx.m_processedSegmentMask_Transforms;
				pAnimCtx->m_processedSegmentMask_SkinningMats = retargetAnimCtx.m_processedSegmentMask_SkinningMats;
				pAnimCtx->m_processedSegmentMask_OutputControls = retargetAnimCtx.m_processedSegmentMask_OutputControls;
			}

			if ((pAnimData->m_earlyDeferredSegmentMaskGame|pAnimData->m_earlyDeferredSegmentMaskRender) && (resultMode & FgAnimData::kAnimResultSkinningMatricesOs))
			{
				// If we know we will need these, kick them now so we don't have to wait for it when it's needed
				EngineComponents::GetAnimMgr()->KickEarlyDeferredAnimCmd(pAnimData, pAnimData->m_earlyDeferredSegmentMaskGame, pAnimData->m_earlyDeferredSegmentMaskRender);
				if (!pJobData->m_gameplayIsPaused)
				{
					pAnimData->m_earlyDeferredSegmentMaskGame = 0;
					pAnimData->m_earlyDeferredSegmentMaskRender = 0;
				}
			}
		}
	}
	else
	{
		outputMask &= (AnimExecutionContext::kOutputSkinningMats | AnimExecutionContext::kOutputOutputControls);
		if (outputMask)
		{
			ANIM_ASSERT(!pAnimCtx->m_animCmdList.GetNumWordsUsed());
			U32 requiredSegmentMask = 1 << 0;
			if (pPrevAnimCtx)
			{
				// While not animating we want to keep the last skinning matrices around
				// and also dependency table in case we need to fill in a missing segment
				requiredSegmentMask |= pPrevAnimCtx->m_processedSegmentMask_SkinningMats;
				if (pCurSkel->m_pAnimHierarchy->m_dependencyTableSize)
				{
					ANIM_ASSERT(pAnimCtx->m_pDependencyTable && pPrevAnimCtx->m_pDependencyTable);
					memcpy(pAnimCtx->m_pDependencyTable, pPrevAnimCtx->m_pDependencyTable, pCurSkel->m_pAnimHierarchy->m_dependencyTableSize);
				}
				pAnimCtx->m_dependencyTableValid = pPrevAnimCtx->m_dependencyTableValid;
			}
			ProcessRequiredSegments(requiredSegmentMask,
									outputMask, 
									pAnimCtx,
									pPrevAnimCtx);
		}
	}

#ifdef ANIM_STATS_ENABLED
	AnimBucketStats& animStats = g_animFrameStats.m_bucket[pData->m_bucket];
	animStats.m_pass[0].m_objects++;
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT_GLOBAL(AsyncObjectUpdate_AnimBlend_Job)
{
	const AsyncUpdateJobData* pJobData = (const AsyncUpdateJobData*)jobParam;
	AnimControl* pAnimControl = pJobData->m_pAnimControl;
	FgAnimData* pAnimData = pJobData->m_pAnimData;

	PROFILE_AUTO_STRINGID(Animation, pAnimData->m_hProcess.ToProcess()->GetUserId());

	if (CameraManager::Get().DidCameraCutThisFrame())
	{
		pAnimData->ResetPersistentRigData();
	}

	const FgAnimData::AnimSourceMode sourceMode = pAnimData->GetAnimSourceMode(0);

	const bool objectIsLive = !pJobData->m_gameplayIsPaused || (pAnimData->m_flags & FgAnimData::kForceNextPausedEvaluation);
	const bool shouldAnimateThisObject = objectIsLive && (sourceMode != FgAnimData::kAnimSourceModeNone);

	PerformAnimBlend(pJobData, shouldAnimateThisObject);

	if (objectIsLive)
	{
		pAnimData->SetRunningAnimationPass(true);
		// Perform object callbacks if needed
		if (pAnimData->m_flags & FgAnimData::kEnableAsyncPostAnimBlending)
		{

			PerformAnimDataCallback(FgAnimData::kEnableAsyncPostAnimBlending, pAnimData);
		}

		// If the object had a post anim blending pass we want to propagate the potentially moved locator to the anim data.
		if (pAnimData->m_flags & FgAnimData::kAllowAutomaticObjXformUpdate)
		{
			PerformAnimDataCallback(FgAnimData::kAllowAutomaticObjXformUpdate, pAnimData);
		}
		pAnimData->SetRunningAnimationPass(false);
	}

	// Decrement the object counter so that next stage job for this object is allowed to run
	if (pJobData->m_pObjectWaitCounter)
		pJobData->m_pObjectWaitCounter->Decrement();
}

static void PerformJointBlend(const AsyncUpdateJobData* pJobData, bool shouldAnimateThisObject)
{
	AnimControl* pAnimControl = pJobData->m_pAnimControl;
	FgAnimData* pAnimData = pJobData->m_pAnimData;

	const ArtItemSkeleton* pCurSkel = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem();
	ANIM_ASSERT(pCurSkel && pAnimSkel);

	const U32 hasOutputControls = pCurSkel->m_pAnimHierarchy->m_numOutputControls > 0;
	const U32 numSegments = pCurSkel->m_numSegments;

	// This should already be registered as this is pass 2 (unless it is cloth which has a None/JointParams setup for pass 1/2)
	AnimExecutionContext* pAnimCtx = GetAnimExecutionContext(pAnimData);
	const AnimExecutionContext* pPrevAnimCtx = GetPrevFrameAnimExecutionContext(pAnimData);
	ANIM_ASSERT(pAnimCtx);

	FgAnimData::AnimSourceMode sourceMode = pAnimData->GetAnimSourceMode(1);
	const FgAnimData::AnimResultMode resultMode = pAnimData->GetAnimResultMode(1);

	U32 outputMask = 0;
	if (g_animOptions.m_alwaysAnimateAllSegments && sourceMode != FgAnimData::kAnimSourceModeNone)
	{
		outputMask |= AnimExecutionContext::kOutputTransformsOs;
		outputMask |= AnimExecutionContext::kOutputSkinningMats;
		outputMask |= AnimExecutionContext::kOutputOutputControls;
	}

	if (resultMode & FgAnimData::kAnimResultJointTransformsOs)
	{
		outputMask |= AnimExecutionContext::kOutputTransformsOs;
	}
	if (resultMode & FgAnimData::kAnimResultJointParamsLs)
	{
		outputMask |= AnimExecutionContext::kOutputJointParamsLs;
		outputMask |= AnimExecutionContext::kOutputFloatChannels;
	}
	if (resultMode & FgAnimData::kAnimResultSkinningMatricesOs)
	{
		outputMask |= AnimExecutionContext::kOutputSkinningMats;
		outputMask |= AnimExecutionContext::kOutputOutputControls;
	}

	if (shouldAnimateThisObject)
	{
		ScopedTempAllocator jj(FILE_LINE_FUNC);

		// Only the world space positions will be out of date and the input is the joint cache...
		pAnimCtx->m_processedSegmentMask_Transforms = 0;

		if (pAnimData->m_userAnimationPassCallback[1])
		{
			ANIM_ASSERT(pCurSkel == pAnimSkel);
			// Propagate the skinning matrix pointers to the segment data
			ANIM_ASSERT(pAnimCtx->m_pAllSkinningBoneMats);
			for (U32 segmentIndex = 0; segmentIndex < pAnimCtx->m_pSkel->m_numSegments; ++segmentIndex)
			{
				const U32F firstJoint = ndanim::GetFirstJointInSegment(pAnimCtx->m_pSkel->m_pAnimHierarchy, segmentIndex);
				pAnimCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats = pAnimCtx->m_pAllSkinningBoneMats + firstJoint * 3 /*Vec4sPerMat34*/;
				ANIM_ASSERT(pAnimCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats);
				pAnimCtx->m_processedSegmentMask_SkinningMats |= (1 << segmentIndex);
			}

			pAnimData->m_userAnimationPassCallback[1](pAnimData, pJobData->m_animSysDeltaTime);

			pAnimCtx->m_processedSegmentMask_SkinningMats = (1 << pAnimCtx->m_pSkel->m_numSegments) - 1;
		}
		else if (pAnimControl)
		{
			float* pInputControls = const_cast<float*>(pAnimData->m_jointCache.GetInputControls());

			// Only animate the 'base' segment unless we are generating skinning matrices
			U32 requiredSegmentMask = (1 << 0);
			if (pAnimData->m_flags & FgAnimData::kForceAnimateAllSegments || FALSE_IN_FINAL_BUILD(g_animOptions.m_alwaysAnimateAllSegments || g_animOptions.m_createSkinningForAllSegments))
				requiredSegmentMask |= 0xFFFFFFFF;

			if (pAnimData->m_flags & FgAnimData::kDisableBSphereCompute)
				pAnimCtx->m_disableBoundingVolumeUpdate = true;

			ANIM_ASSERT(sourceMode == FgAnimData::kAnimSourceModeJointParams);

			if (requiredSegmentMask & 1)
			{
				if (outputMask & AnimExecutionContext::kOutputTransformsOs)
				{
					// It does not seem correct to validate transforms now before they get update but this is to prevent nast race condition problem:
					// Sometimes somebody reads from joint cache while we're animating and if joint cache thinks the transforms are not up to date 
					// it will try to update it while it's being writen to from animation.
					// Whoever reads the joint cache mights still get a garbage but at least this way we will not be stuck with garbage in joint cache
					// for the rest of the frame
					pAnimData->m_jointCache.ValidateAllTransforms();
				}

				ProcessRequiredSegments(1, outputMask, pAnimCtx, pPrevAnimCtx, true /*useImpliedPoseAsInput*/);

				if (outputMask & AnimExecutionContext::kOutputTransformsOs)
				{
					// Transforms just got updated
					pAnimData->m_jointCache.ValidateAllTransforms();
					pAnimData->m_jointCache.InvalidateAllWsLocs();
				}
			}
			if (requiredSegmentMask & ~1)
			{
				// Don't use an implied pose for non-base segments as the joint cache doesn't exist for them
				ProcessRequiredSegments(requiredSegmentMask & ~1, outputMask, pAnimCtx, pPrevAnimCtx);
			}

			if (outputMask & (AnimExecutionContext::kOutputFloatChannels|AnimExecutionContext::kOutputOutputControls))
			{
				pAnimData->m_jointCache.ValidateOutputControls();
			}
		}

		if ((pAnimData->m_earlyDeferredSegmentMaskGame|pAnimData->m_earlyDeferredSegmentMaskRender) && (resultMode & FgAnimData::kAnimResultSkinningMatricesOs))
		{
			// If we know we will need these, kick them now so we don't have to wait for it when it's needed
			EngineComponents::GetAnimMgr()->KickEarlyDeferredAnimCmd(pAnimData, pAnimData->m_earlyDeferredSegmentMaskGame, pAnimData->m_earlyDeferredSegmentMaskRender);
			if (!pJobData->m_gameplayIsPaused)
			{
				pAnimData->m_earlyDeferredSegmentMaskGame = 0;
				pAnimData->m_earlyDeferredSegmentMaskRender = 0;
			}
		}
	}
	else
	{
		// If outputting skinning mats we will copy them from previous frame
		outputMask &= (AnimExecutionContext::kOutputSkinningMats | AnimExecutionContext::kOutputOutputControls);
		if (outputMask)
		{
			ANIM_ASSERT(!pAnimCtx->m_animCmdList.GetNumWordsUsed());
			U32 requiredSegmentMask = 1 << 0;
			if (pPrevAnimCtx)
			{
				// While not animating we want to keep the last skinning matrices around
				// and also dependency table in case we need to fill in a missing segment
				requiredSegmentMask |= pPrevAnimCtx->m_processedSegmentMask_SkinningMats;
				if (pCurSkel->m_pAnimHierarchy->m_dependencyTableSize)
				{
					ANIM_ASSERT(pAnimCtx->m_pDependencyTable && pPrevAnimCtx->m_pDependencyTable);
					memcpy(pAnimCtx->m_pDependencyTable, pPrevAnimCtx->m_pDependencyTable, pCurSkel->m_pAnimHierarchy->m_dependencyTableSize);
				}
				pAnimCtx->m_dependencyTableValid = pPrevAnimCtx->m_dependencyTableValid;
			}
			ProcessRequiredSegments(requiredSegmentMask,
									outputMask, 
									pAnimCtx,
									pPrevAnimCtx);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT_GLOBAL(AsyncObjectUpdate_JointBlend_Job)
{
	const AsyncUpdateJobData* pJobData = (const AsyncUpdateJobData*)jobParam;
	AnimControl* pAnimControl = pJobData->m_pAnimControl;
	FgAnimData* pAnimData = pJobData->m_pAnimData;
	const float deltaTime = (pAnimData->m_disabledDeltaTime + pJobData->m_animSysDeltaTime) * pAnimData->m_animClockScale;

	PROFILE_AUTO_STRINGID(Animation, pAnimData->m_hProcess.ToProcess()->GetUserId());

	FgAnimData::AnimSourceMode sourceMode = pAnimData->GetAnimSourceMode(1);

	
	const bool objectIsLive = !pJobData->m_gameplayIsPaused || (pAnimData->m_flags & FgAnimData::kForceNextPausedEvaluation);
	const bool shouldAnimateThisObject = objectIsLive && (sourceMode != FgAnimData::kAnimSourceModeNone);

	PerformJointBlend(pJobData, shouldAnimateThisObject);

#ifdef ANIM_STATS_ENABLED
	AnimBucketStats& animStats = g_animFrameStats.m_bucket[pData->m_bucket];
	animStats.m_pass[0].m_objects++;
#endif

	// Perform object callbacks if needed
	if (pAnimData->m_flags & FgAnimData::kEnableAsyncPostJointUpdate)
	{
		PerformAnimDataCallback(FgAnimData::kEnableAsyncPostJointUpdate, pAnimData, pJobData->m_gameplayIsPaused);
	}

	if (pAnimData->m_flags & FgAnimData::kEnableClothUpdate)
	{
		PerformAnimDataCallback(FgAnimData::kEnableClothUpdate, pAnimData);
	}

	if (g_animOptions.m_forceBindPose && !(pAnimData->m_flags & FgAnimData::kIsInBindPose))
	{
		const ArtItemSkeleton* pCurSkel = pAnimData->m_curSkelHandle.ToArtItem();
		FillIdentityRigOutputs(pAnimData, pCurSkel);
	}

	if (pJobData->m_animSysDeltaTime > 0.f)
	{
		pAnimData->m_disabledDeltaTime = 0.f;
	}

	if (pAnimData->m_flags & FgAnimData::kForceNextPausedEvaluation)
	{
		pAnimData->m_flags &= ~FgAnimData::kForceNextPausedEvaluation;
	}

	// Free the counter used to chain the jobs for this object.
	ndjob::CounterHandle pObjectWaitCounter = (ndjob::CounterHandle)pJobData->m_pObjectWaitCounter;
	if (pObjectWaitCounter)
	{
		ndjob::FreeCounter(pObjectWaitCounter);
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT_GLOBAL(AnimateDisabledObjectJob)
{
	AnimDataProcessingData* pProcessingData = reinterpret_cast<AnimDataProcessingData*>(jobParam);
	U64 lastYieldTime = TimerGetRawCount();
	while (1)
	{
		I64 cursor = pProcessingData->m_cursor.Add(1);
		if (cursor >= pProcessingData->m_numAnimData)
			break;

		if (pProcessingData->m_yieldTime > 0.0f)
		{
			if (ConvertTicksToSeconds(lastYieldTime, TimerGetRawCount()) > pProcessingData->m_yieldTime)
			{
				ndjob::Yield();
				lastYieldTime = TimerGetRawCount();
			}
		}

		FgAnimData* pAnimData = pProcessingData->m_ppAnimDataArray[cursor];

		AnimateDisabledObject(pAnimData, pProcessingData->m_deltaTime);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimateDisabledObject(FgAnimData* pAnimData, F32 deltaTime)
{
	if (pAnimData->m_flags & FgAnimData::kAccumulateDisabledDeltaTime)
	{
		pAnimData->m_disabledDeltaTime += deltaTime;
	}

	ANIM_ASSERT(!GetAnimExecutionContext(pAnimData));
	AnimExecutionContext* pCtx = AllocateAnimContextForThisFrame(EngineComponents::GetAnimMgr()->GetAnimDataIndex(pAnimData));
	pCtx->Init(pAnimData->m_curSkelHandle.ToArtItem(),
			   &pAnimData->m_objXform,
			   const_cast<ndanim::JointParams*>(pAnimData->m_jointCache.GetJointParamsLs()),
			   pAnimData->m_jointCache.GetJointTransformsForOutput(),
			   const_cast<float*>(pAnimData->m_jointCache.GetInputControls()),
			   const_cast<float*>(pAnimData->m_jointCache.GetOutputControls()),
			   pAnimData->m_pPersistentData);
	pCtx->m_pPluginJointSet = pAnimData->m_pPluginJointSet;

	const AnimExecutionContext* pPrevCtx = GetPrevFrameAnimExecutionContext(pAnimData);
	ANIM_ASSERT(pPrevCtx); // We should always have previous context if disabled
	if (pAnimData->m_pSkeleton->m_dependencyTableSize)
	{
		ANIM_ASSERT(pCtx->m_pDependencyTable && pPrevCtx->m_pDependencyTable);
		memcpy(pCtx->m_pDependencyTable, pPrevCtx->m_pDependencyTable, pAnimData->m_pSkeleton->m_dependencyTableSize);
	}
	pCtx->m_dependencyTableValid = pPrevCtx->m_dependencyTableValid;

	if (pAnimData->m_userAnimationPassCallback[0])
	{
		pCtx->Validate();

		// Propagate the skinning matrix pointers to the segment data
		for (U32 segmentIndex = 0; segmentIndex < pCtx->m_pSkel->m_numSegments; ++segmentIndex)
		{
			ANIM_ASSERT(pCtx->m_pAllSkinningBoneMats);
			ANIM_ASSERT(!pCtx->m_pSegmentData[0].m_pSkinningBoneMats
						|| pCtx->m_pSegmentData[0].m_pSkinningBoneMats == pCtx->m_pAllSkinningBoneMats);

			const U32F firstJoint = ndanim::GetFirstJointInSegment(pCtx->m_pSkel->m_pAnimHierarchy, segmentIndex);
			pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats = pCtx->m_pAllSkinningBoneMats + firstJoint * 3 /*Vec4sPerMat34*/;
			ANIM_ASSERT(pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats);

			// Sum up all output controls in the previous segments
			U32 firstOutputControlInSegment = 0;
			for (int iSeg = 0; iSeg < segmentIndex; ++iSeg)
				firstOutputControlInSegment += ndanim::GetNumOutputControlsInSegment(pCtx->m_pSkel->m_pAnimHierarchy, iSeg);

			pCtx->m_pSegmentData[segmentIndex].m_pOutputControls = pCtx->m_pAllOutputControls + firstOutputControlInSegment;
		}

		const Process* pProcess = pAnimData->m_hProcess.ToProcess();
		// shouldn't need to set updating process since FastAnim callbacks never use context process
		//Process::SetUpdatingProcess(pProcess);
		pAnimData->m_userAnimationPassCallback[0](pAnimData, deltaTime);
		//Process::SetUpdatingProcess(EngineComponents::GetProcessMgr()->m_pIdleProcess);

		// This shouldn't be needed, it should be handled by the user callback but for now to ship T2 we will probably keep it here just in case
		pCtx->m_processedSegmentMask_Transforms |= pPrevCtx->m_processedSegmentMask_Transforms;
		pCtx->m_processedSegmentMask_SkinningMats |= pPrevCtx->m_processedSegmentMask_SkinningMats;
		pCtx->m_processedSegmentMask_OutputControls |= pPrevCtx->m_processedSegmentMask_OutputControls;

		pCtx->Validate();
	}
}

