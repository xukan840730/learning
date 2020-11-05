/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-commands.h"

#include "corelib/containers/hashtable.h"
#include "corelib/hashes/crc32.h"
#include "corelib/math/locator.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bit-array.h"
#include "corelib/util/hashable-pair.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/anim/anim-stream-manager.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/ik/joint-limits.h"
#include "ndlib/anim/nd-anim-plugins.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/retarget-util.h"
#include "ndlib/anim/rig-nodes/rig-nodes.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/engine-components.h"
#include "ndlib/io/pak-structs.h"
#include "ndlib/io/package-mgr.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/scriptx/h/skel-retarget-defines.h"

#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"

#include <orbisanim/animclip.h>
#include <orbisanim/animhierarchy.h>
#include <orbisanim/commandblock.h>
#include <orbisanim/immediate.h>
#include <orbisanim/structs.h>
#include <orbisanim/util.h>
#include <orbisanim/workbuffer.h>


static const size_t kNumVec4PerMat34 = sizeof(Mat34) / sizeof(Vec4);

extern OrbisAnim::Status BoundingSphereAnimPluginCallback(const OrbisAnim::SegmentContext* pSegmentContext, void* pContext);


/// --------------------------------------------------------------------------------------------------------------- ///
namespace OrbisAnim
{
	struct JointParams;

	namespace CommandBlock
	{
		extern DispatcherFunction g_pfnCustomCommand[];
		extern size_t g_numCustomCommands;
	}
}

extern bool g_allowLoadingOfDebugPages;


/// --------------------------------------------------------------------------------------------------------------- ///
static void UpdateBoundingInfo(const OrbisAnim::SegmentContext* pSegmentContext, const AnimExecutionContext* pCtx)
{
	// Update the bounding info
	// We only use the gameplay joints to figure out the bounding sphere
	if (pSegmentContext->m_iSegment == 0)
	{
		if (!pCtx->m_disableBoundingVolumeUpdate && pCtx->m_pAnimDataHack)
		{
			const OrbisAnim::AnimHierarchy* pSkeleton = pSegmentContext->m_pAnimHierarchy;
			BoundingSphereAnimPluginData data;
			data.m_pObjXform = pCtx->m_pObjXform;										// objTransform
			data.m_skeletonNameId = pCtx->m_pSkel->GetNameId();
			data.m_numTotalJoints = pSkeleton->m_numTotalJoints;				// num joints
			data.m_locJointTransforms = (void*)((uintptr_t)pSegmentContext->m_locJointTransforms + 0x40/*sizeof(OrbisAnim::JointTransform)*/);	// object space joint transforms
			data.m_visSphereJointIndex = (I16)pCtx->m_pAnimDataHack->m_visSphereJointIndex;	// bounding sphere info
			data.m_pVisSphere = &pCtx->m_pAnimDataHack->m_visSphere;						// visSphere
			data.m_pBoundingInfo = pCtx->m_pAnimDataHack->m_pBoundingInfo;								// output bounding info
			data.m_boundingSphereExcludeJoints[0] = pCtx->m_pAnimDataHack->m_boundingSphereExcludeJoints[0];		// Exclude Joint Index 0
			data.m_boundingSphereExcludeJoints[1] = pCtx->m_pAnimDataHack->m_boundingSphereExcludeJoints[1];		// Exclude Joint Index 1
			data.m_clothBoundingBoxMult = pCtx->m_pAnimDataHack->m_clothBoundingBoxMult;
			data.m_dynamicPaddingRadius = pCtx->m_pAnimDataHack->m_dynamicBoundingBoxPad;
			data.m_pVisAabb = &pCtx->m_pAnimDataHack->m_visAabb;
			data.m_useBoundingBox = pCtx->m_pAnimDataHack->m_useBoundingBox;

			BoundingSphereAnimPluginCallback(pSegmentContext, &data);
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
static void EmitRequestedFloatChannelOutputs(const AnimExecutionContext* pCtx,
	const AnimExecutionContext* pPrevCtx,
	const OrbisAnim::SegmentContext* pSegmentContext,
	U32 segmentIndex,
	U32 outputMask)
{
	const OrbisAnim::AnimHierarchy* pHierarchy = pCtx->m_pSkel->m_pAnimHierarchy;

	// Can we pull information from the animation system or do we need to output things from the previous frame or bindpose?
	if (pSegmentContext)
	{
		ANIM_ASSERT(pSegmentContext->m_iSegment == segmentIndex);
		const OrbisAnim::SegmentContext& segmentContext = *pSegmentContext;

		if ((outputMask & AnimExecutionContext::kOutputFloatChannels) &&
			!(pCtx->m_processedSegmentMask_FloatChannels & (1 << segmentIndex)))
		{
			const int numToCopy = pSegmentContext->m_pSegment->m_numFloatChannels;
			if (numToCopy)
			{
				memcpy(pCtx->m_pAllOutputControls, segmentContext.m_locFloatChannels, sizeof(float) * numToCopy);

				// Hack for now as we need to copy the output controls to both the joint cache as well as the single
				// large arry of output controls in the AnimExecutionContext.
				if (segmentIndex == 0 &&
					pCtx->m_pSegmentData[0].m_pOutputControls != pCtx->m_pAllOutputControls)
				{
					memcpy(pCtx->m_pSegmentData[0].m_pOutputControls, segmentContext.m_locFloatChannels, sizeof(float) * numToCopy);
				}
			}

			const U32 segmentMask = (1 << segmentIndex);
			pCtx->m_processedSegmentMask_FloatChannels |= segmentMask;
		}
	}
	else
	{
		if ((outputMask & AnimExecutionContext::kOutputFloatChannels) &&
			!(pCtx->m_processedSegmentMask_FloatChannels & (1 << segmentIndex)))
		{
			const int numFloatChannels = pCtx->m_pSkel->m_pAnimHierarchy->m_numFloatChannels;
			const int numOutputControls = pCtx->m_pSkel->m_pAnimHierarchy->m_numOutputControls;
			int numToCopy = numFloatChannels;
			if (numToCopy)
			{
				if (pPrevCtx && pPrevCtx->m_processedSegmentMask_FloatChannels & (1 << segmentIndex))
				{
					ANIM_ASSERT(pCtx->m_pAllOutputControls);
					ANIM_ASSERT(pPrevCtx->m_pAllOutputControls);
					memcpy(pCtx->m_pAllOutputControls,
						pPrevCtx->m_pAllOutputControls,
						numToCopy * sizeof(float));
				}
				else
				{
					ANIM_ASSERT(pCtx->m_pAllOutputControls);
					const float* pDefaultFloatChannels = OrbisAnim::AnimHierarchy_GetDefaultFloatChannelPoseTable(pHierarchy);
					memcpy(pCtx->m_pAllOutputControls, pDefaultFloatChannels, numToCopy * sizeof(float));

					// We don't have the default values for the output controls so set them to zero for now.
					int numOutputControlsToReset = numOutputControls - numFloatChannels;
					if (numOutputControlsToReset)
					{
						memset(pCtx->m_pAllOutputControls + numFloatChannels, 0, numOutputControlsToReset * sizeof(float));
					}
				}
			}

			const U32 segmentMask = (1 << segmentIndex);
			pCtx->m_processedSegmentMask_FloatChannels |= segmentMask;
		}
	}

	pCtx->Validate();
}


/// --------------------------------------------------------------------------------------------------------------- ///
static void EmitRequestedOutputs(const AnimExecutionContext* pCtx,
	const AnimExecutionContext* pPrevCtx,
	const OrbisAnim::SegmentContext* pSegmentContext,
	U32 segmentIndex,
	U32 outputMask)
{
	const OrbisAnim::AnimHierarchy* pHierarchy = pCtx->m_pSkel->m_pAnimHierarchy;

	// Can we pull information from the animation system or do we need to output things from the previous frame or bindpose?
	if (pSegmentContext)
	{
		ANIM_ASSERT(pSegmentContext->m_iSegment == segmentIndex);
		const OrbisAnim::SegmentContext& segmentContext = *pSegmentContext;

		if ((outputMask & AnimExecutionContext::kOutputJointParamsLs) &&
			!(pCtx->m_processedSegmentMask_JointParams & (1 << segmentIndex)))
		{
			const U32F numAnimatedJointsInSegment = OrbisAnim::AnimHierarchy_GetNumAnimatedJointsInSegment(pHierarchy, segmentIndex);
			const U32F numTotalJointsInSegment = OrbisAnim::AnimHierarchy_GetNumJointsInSegment(pHierarchy, segmentIndex);
			const U32F numJointsToOutput = pCtx->m_includeProceduralJointParamsInOutput ? numTotalJointsInSegment : numAnimatedJointsInSegment;

			if (numJointsToOutput)
			{
				ANIM_ASSERT(pCtx->m_pSegmentData[segmentIndex].m_pJointParams);

				// Pack the animated joint params by extracting parts of the output joint params array.
				OrbisAnim::Status eStatus = OrbisAnim::EvaluateOutputJointParams(&segmentContext,
					(OrbisAnim::JointParams*)pCtx->m_pSegmentData[segmentIndex].m_pJointParams,		// [numAnimatedJointsInSegment or numJointsInSegment]
					0,
					numJointsToOutput);
				ANIM_ASSERT(eStatus == OrbisAnim::kSuccess);
			}

			const U32 segmentMask = (1 << segmentIndex);
			pCtx->m_processedSegmentMask_JointParams |= segmentMask;
		}

		if ((outputMask & AnimExecutionContext::kOutputTransformsOs) &&
			!(pCtx->m_processedSegmentMask_Transforms & (1 << segmentIndex)))
		{
			ANIM_ASSERT(pCtx->m_pSegmentData[segmentIndex].m_pJointTransforms);

			const U16 numJoints = segmentContext.m_pSegment->m_numJoints;

			const OrbisAnim::JointTransform* pJointTransforms = (const OrbisAnim::JointTransform*)((uintptr_t)segmentContext.m_locJointTransforms + 0x40 /* sizeof(OrbisAnim::JointTransform) */);	// object space joint transforms;
			Transform* pOutputTransforms = pCtx->m_pSegmentData[segmentIndex].m_pJointTransforms;
			ANIM_ASSERT(pOutputTransforms);
			for (U32F jointIndex = 0; jointIndex < numJoints; ++jointIndex)
			{
				pOutputTransforms[jointIndex] = Transform(pJointTransforms[jointIndex].GetTransformNoScale());
			}

			const U32 segmentMask = (1 << segmentIndex);
			pCtx->m_processedSegmentMask_Transforms |= segmentMask;
		}

		if ((outputMask & AnimExecutionContext::kOutputSkinningMats) &&
			!(pCtx->m_processedSegmentMask_SkinningMats & (1 << segmentIndex)))
		{
			ANIM_ASSERT(pCtx->m_pObjXform);
			ANIM_ASSERT(pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats);

			const U32F firstJoint = OrbisAnim::AnimHierarchy_GetFirstJointInSegment(pHierarchy, segmentIndex);
			const U32F numJoints = OrbisAnim::AnimHierarchy_GetNumJointsInSegment(pHierarchy, segmentIndex);

			Transform ident(kIdentity);
			OrbisAnim::Status eStatus = OrbisAnim::EvaluateOutputSkinningMatrices34(&segmentContext,
				&ident,
				(SMath::Mat34*)pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats,		// [numJointsInSegment]
				firstJoint - segmentContext.m_pSegment->m_firstJoint,
				numJoints);
			ANIM_ASSERT(eStatus == OrbisAnim::kSuccess);

			UpdateBoundingInfo(&segmentContext, pCtx);

			const U32 segmentMask = (1 << segmentIndex);

			// We need to ensure that the motion blur matrices are not using garbage during LOD transitions
			if (pPrevCtx && !(pPrevCtx->m_processedSegmentMask_SkinningMats & segmentMask) && pPrevCtx->m_pAllSkinningBoneMats)
			{
				const size_t offset = firstJoint * 3;		// 3 x Vec4 in a Mat34

				// We need to pretend like the previous frame's skinning matrices for this newly evaluated segment were the same
				memcpy(pPrevCtx->m_pAllSkinningBoneMats + offset, pCtx->m_pAllSkinningBoneMats + offset, numJoints * sizeof(SMath::Mat34));
			}

			pCtx->m_processedSegmentMask_SkinningMats |= segmentMask;
			ANIM_ASSERT(pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats);
		}

		if ((outputMask & AnimExecutionContext::kOutputOutputControls) &&
			!(pCtx->m_processedSegmentMask_OutputControls & (1 << segmentIndex)))
		{
			int numToCopy = pCtx->m_pSkel->m_pAnimHierarchy->m_numOutputControls;

			OrbisAnim::Status eStatus = OrbisAnim::EvaluateSdkFloatOutputCommands(&segmentContext, pCtx->m_pAllOutputControls);
			ANIM_ASSERT(eStatus == OrbisAnim::kSuccess);

			// Hack for now as we need to copy the output controls to both the joint cache as well as the single
			// large arry of output controls in the AnimExecutionContext.
			if (segmentIndex == 0 &&
				pCtx->m_pSegmentData[0].m_pOutputControls != pCtx->m_pAllOutputControls)
			{
				eStatus = OrbisAnim::EvaluateSdkFloatOutputCommands(&segmentContext, pCtx->m_pSegmentData[0].m_pOutputControls);
				ANIM_ASSERT(eStatus == OrbisAnim::kSuccess);
			}

			const U32 segmentMask = (1 << segmentIndex);
			pCtx->m_processedSegmentMask_OutputControls |= segmentMask;
		}
	}
	else
	{
		if (outputMask & AnimExecutionContext::kOutputJointParamsLs &&
			!(pCtx->m_processedSegmentMask_JointParams & (1 << segmentIndex)))
		{
			// The local space joint params from last frame are now stale. Output bindpose
			const OrbisAnim::JointParams* pDefaultPoseTable = (const OrbisAnim::JointParams*)OrbisAnim::AnimHierarchy_GetDefaultJointPoseTable(pHierarchy);
			const int firstAnimatedJoint = OrbisAnim::AnimHierarchy_GetFirstAnimatedJointInSegment(pHierarchy, segmentIndex);
			const int numAnimatedJoints = OrbisAnim::AnimHierarchy_GetNumAnimatedJointsInSegment(pHierarchy, segmentIndex);
			memcpy(pCtx->m_pSegmentData[segmentIndex].m_pJointParams,
				pDefaultPoseTable + firstAnimatedJoint,
				numAnimatedJoints * sizeof(OrbisAnim::JointParams));

			const U32 segmentMask = (1 << segmentIndex);
			pCtx->m_processedSegmentMask_JointParams |= segmentMask;
		}

		if (outputMask & AnimExecutionContext::kOutputTransformsOs &&
			!(pCtx->m_processedSegmentMask_Transforms & (1 << segmentIndex)))
		{
			// The transforms from last frame are now stale. Output bindpose
			const SMath::Mat34* pInvBindPoseTable = (const SMath::Mat34*)OrbisAnim::AnimHierarchy_GetInverseBindPoseTable(pHierarchy);
			const int firstJoint = OrbisAnim::AnimHierarchy_GetFirstJointInSegment(pHierarchy, segmentIndex);
			const int numJoints = OrbisAnim::AnimHierarchy_GetNumJointsInSegment(pHierarchy, segmentIndex);
			for (int jointIndex = 0; jointIndex < numJoints; ++jointIndex)
			{
				pCtx->m_pSegmentData[segmentIndex].m_pJointTransforms[jointIndex] = Transform(Inverse(pInvBindPoseTable[firstJoint].GetMat44()));
			}

			const U32 segmentMask = (1 << segmentIndex);
			pCtx->m_processedSegmentMask_Transforms |= segmentMask;
		}

		if (outputMask & AnimExecutionContext::kOutputSkinningMats &&
			!(pCtx->m_processedSegmentMask_SkinningMats & (1 << segmentIndex)))
		{
			const int numJoints = OrbisAnim::AnimHierarchy_GetNumJointsInSegment(pHierarchy, segmentIndex);

			if (pPrevCtx && (pPrevCtx->m_processedSegmentMask_SkinningMats & (1 << segmentIndex)))
			{
				const int firstJoint = OrbisAnim::AnimHierarchy_GetFirstJointInSegment(pHierarchy, segmentIndex);
				const size_t offset = firstJoint * 3;		// 3 x Vec4 in a Mat34

				// The m_pSegmentData in PrevCtx is not alive anymore at this point so we need to use the 'All' pointer
				memcpy(pCtx->m_pAllSkinningBoneMats + offset, pPrevCtx->m_pAllSkinningBoneMats + offset, numJoints * sizeof(SMath::Mat34));
			}
			else
			{
				// Bind pose
				// Use parent's skinning matrix to keep joint's relative position in repsect to parent the same as in bind pose
				const int firstJoint = OrbisAnim::AnimHierarchy_GetFirstJointInSegment(pHierarchy, segmentIndex);
				for (int jointIndex = 0; jointIndex < numJoints; ++jointIndex)
				{
					I32 iParent = ndanim::GetParentJoint(pCtx->m_pSkel->m_pAnimHierarchy, firstJoint+jointIndex);
					if (iParent >= 0)
					{
						reinterpret_cast<SMath::Mat34*>(pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats)[jointIndex] = reinterpret_cast<SMath::Mat34*>(pCtx->m_pAllSkinningBoneMats)[iParent];
					}
					else
					{
						reinterpret_cast<SMath::Mat34*>(pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats)[jointIndex] = SMath::Mat34(kIdentity);
					}
				}
			}

//			UpdateBoundingInfo(segmentContext, pCtx);

			const U32 segmentMask = (1 << segmentIndex);
			pCtx->m_processedSegmentMask_SkinningMats |= segmentMask;
			ANIM_ASSERT(pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats);
		}

		if ((outputMask & AnimExecutionContext::kOutputOutputControls) &&
			!(pCtx->m_processedSegmentMask_OutputControls & (1 << segmentIndex)))
		{
			const int numFloatChannels = pCtx->m_pSkel->m_pAnimHierarchy->m_numFloatChannels;
			const int numOutputControls = pCtx->m_pSkel->m_pAnimHierarchy->m_numOutputControls;
			int numToCopy = numOutputControls;
			if (numToCopy)
			{
				if (pPrevCtx && pPrevCtx->m_processedSegmentMask_OutputControls & (1 << segmentIndex))
				{
					ANIM_ASSERT(pCtx->m_pAllOutputControls);
					ANIM_ASSERT(pPrevCtx->m_pAllOutputControls);
					memcpy(pCtx->m_pAllOutputControls, pPrevCtx->m_pAllOutputControls, numToCopy * sizeof(float));
				}
				else
				{
					ANIM_ASSERT(pCtx->m_pAllOutputControls);
					const float* pDefaultOutputControls = OrbisAnim::AnimHierarchy_GetDefaultFloatChannelPoseTable(pHierarchy);
					memcpy(pCtx->m_pAllOutputControls, pDefaultOutputControls, numFloatChannels * sizeof(float));

					// We don't have the default values for the output controls so set them to zero for now.
					int numOutputControlsToReset = numOutputControls - numFloatChannels;
					if (numOutputControlsToReset)
					{
						memset(pCtx->m_pAllOutputControls + numFloatChannels, 0, numOutputControlsToReset * sizeof(float));
					}
				}
			}

			const U32 segmentMask = (1 << segmentIndex);
			pCtx->m_processedSegmentMask_OutputControls |= segmentMask;
		}
	}

	pCtx->Validate();
}


/// --------------------------------------------------------------------------------------------------------------- ///
static OrbisAnim::Status EvaluateJointHierarchyCmds_Prepare(OrbisAnim::SegmentContext* pSegmentContext,
															F32 const* pInputControls)
{
	OrbisAnim::Status eStatus = OrbisAnim::kSuccess;
	OrbisAnim::Status eStatus2;

	OrbisAnim::AnimHierarchy const* pHierarchy = pSegmentContext->m_pAnimHierarchy;
	OrbisAnim::AnimHierarchySegment const* pSegment = pSegmentContext->m_pSegment;

	// Run our primary sdk input commands
	//  Allocate the full scalar table if needed
	if (pSegment->m_numScalars > 0)
	{
		eStatus2 = OrbisAnim::AllocateSdkScalarTable(pSegmentContext);
		ANIM_ASSERT(eStatus2 == OrbisAnim::kSuccess);
	}

	eStatus2 = OrbisAnim::EvaluateSdkFloatInputCommands(pSegmentContext, pInputControls);
	ANIM_ASSERT(eStatus2 == OrbisAnim::kSuccess);

	// We are done with our float channel table, and must deallocate it before
	// our procedural joint params are allocated, or we'll end up with a space
	// in the middle of our joint params table.
	//NOTE: If we wanted the float channel table to persist, we'd have to make
	// a copy of it elsewhere in the work buffer here.
	OrbisAnim::FreeFloatChannelTable(pSegmentContext);

	//  Rearrange the work buffer to match the output of EvaluateCommandBlocks
	eStatus2 = OrbisAnim::PrepareWorkBufferForEvaluateCommandBlocks(pSegmentContext);
	ANIM_ASSERT(eStatus2 == OrbisAnim::kSuccess);

	return eStatus;
}


/// --------------------------------------------------------------------------------------------------------------- ///
struct EvaluateCompiledRigParams
{
	OrbisAnim::AnimHierarchy const* m_pHierarchy;
	OrbisAnim::SegmentContext * m_pSegmentContext;
};

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(EvaluateCompiledRig)
{
	const U64 startTick = TimerGetRawCount();

	PROFILE_AUTO(Animation);

	const EvaluateCompiledRigParams* pParams = (const EvaluateCompiledRigParams*)jobParam;

	extern OrbisAnim::Status EvaluateFlipTestRig(U32 hierarchyId, OrbisAnim::SegmentContext* context);
	extern OrbisAnim::Status EvaluateDogSkelRig(U32 hierarchyId, OrbisAnim::SegmentContext* context);
	extern OrbisAnim::Status EvaluateInterpolateMatrixArray1dMultMatrixTestRig(U32 hierarchyId, OrbisAnim::SegmentContext* context);
	extern OrbisAnim::Status EvaluateWachspressInterpolateMatrixArrayNetworkFatconstraintTestRig(U32 hierarchyId, OrbisAnim::SegmentContext* context);

	if (EvaluateFlipTestRig(pParams->m_pHierarchy->m_animHierarchyId, pParams->m_pSegmentContext) == OrbisAnim::kSuccess)
	{
		RegisterTotalRigNodeStats(startTick, TimerGetRawCount());
		return;
	}

	if (EvaluateDogSkelRig(pParams->m_pHierarchy->m_animHierarchyId, pParams->m_pSegmentContext) == OrbisAnim::kSuccess)
	{
		RegisterTotalRigNodeStats(startTick, TimerGetRawCount());
		return;
	}
	
	if (EvaluateInterpolateMatrixArray1dMultMatrixTestRig(pParams->m_pHierarchy->m_animHierarchyId, pParams->m_pSegmentContext) == OrbisAnim::kSuccess)
	{
		RegisterTotalRigNodeStats(startTick, TimerGetRawCount());
		return;
	}

	if (EvaluateWachspressInterpolateMatrixArrayNetworkFatconstraintTestRig(pParams->m_pHierarchy->m_animHierarchyId, pParams->m_pSegmentContext) == OrbisAnim::kSuccess)
	{
		RegisterTotalRigNodeStats(startTick, TimerGetRawCount());
		return;
	}
 	
	OrbisAnim::Status eStatus2 = OrbisAnim::EvaluateCommandBlocks(pParams->m_pSegmentContext);
 	ANIM_ASSERT(eStatus2 == OrbisAnim::kSuccess);

	RegisterTotalRigNodeStats(startTick, TimerGetRawCount());
}

/// --------------------------------------------------------------------------------------------------------------- ///
static OrbisAnim::Status EvaluateJointHierarchyCmds_Evaluate(OrbisAnim::SegmentContext* pSegmentContext)
{
	OrbisAnim::Status eStatus = OrbisAnim::kSuccess;

	OrbisAnim::AnimHierarchy const* pHierarchy = pSegmentContext->m_pAnimHierarchy;
	OrbisAnim::AnimHierarchySegment const* pSegment = pSegmentContext->m_pSegment;

#if !FINAL_BUILD
	static bool printingAllowed = false;
	static I64 printedFrameNumber = 0;
	if (g_printRigNodeOutputs)
	{
		if (pSegmentContext->m_iSegment == 0)
		{
			if (printedFrameNumber == 0)
			{
				printedFrameNumber = GetCurrentFrameNumber();
			}
			else
			{
				printedFrameNumber = 0;
				g_printRigNodeOutputs = false;
			}
		}
	}
#endif


	// Run sdk and parenting commands for primary set
	if (g_animOptions.m_enableCompiledRigExecution)
	{
		EvaluateCompiledRigParams params;
		params.m_pHierarchy = pHierarchy;
		params.m_pSegmentContext = pSegmentContext;

		if (ndjob::IsAllowedToSleep())
		{
			ndjob::RunJobAndWait(EvaluateCompiledRig, (uintptr_t)&params, ndjob::kRequireLargeStack, FILE_LINE_FUNC);
		}
		else
		{
			EvaluateCompiledRig((uintptr_t)&params);
		}
	}
	else
	{
		const U64 startTick = TimerGetRawCount();
		OrbisAnim::Status eStatus2 = OrbisAnim::EvaluateCommandBlocks(pSegmentContext);
		ANIM_ASSERT(eStatus2 == OrbisAnim::kSuccess);

		RegisterTotalRigNodeStats(startTick, TimerGetRawCount());
	}

#if !FINAL_BUILD
	if (g_animOptions.m_printJointParams &&
		pSegmentContext->m_iSegment == g_animOptions.m_printJointParamsSegment)
	{
		g_animOptions.m_printJointParams = false;

		DoutBase* pOutput = GetMsgOutput(kMsgAnim);

		const AnimExecutionContext* pContext = (const AnimExecutionContext*)pSegmentContext->m_pCustomContext;
		const ArtItemSkeleton* pSkel = pContext->m_pSkel;
		const ndanim::JointParams* pDefaultPoseTable = ndanim::GetDefaultJointPoseTable(pSkel->m_pAnimHierarchy);
		const ndanim::JointParams* pPoseTable = (const ndanim::JointParams*)pSegmentContext->m_locJointParams;
		const OrbisAnim::JointTransform* pTransformTable = (const OrbisAnim::JointTransform*)pSegmentContext->m_locJointTransforms;

		char buf[1024];
		for (int jointIndex = 0; jointIndex < pSegmentContext->m_pSegment->m_numJoints; ++jointIndex)
		{
			const char* jointName = pSkel->m_pJointDescs[pSegmentContext->m_pSegment->m_firstJoint + jointIndex].m_pName;

			int numCharsWritten = snprintf(buf, sizeof(buf), "%s(%d)", jointName, jointIndex);
			if (numCharsWritten < sizeof(buf))
			{
				const OrbisAnim::JointTransform jointTransform = pTransformTable[jointIndex + 1];	// First transform is identity

				// Maya's "joint orient", which is really just the parent-relative (local space) bind pose of the joint
				const Quat jointOrientQuat = pDefaultPoseTable[pSegmentContext->m_pSegment->m_firstJoint + jointIndex].m_quat;
				float jointOrientX, jointOrientY, jointOrientZ;
				jointOrientQuat.GetEulerAngles(jointOrientX, jointOrientY, jointOrientZ, SMath::Quat::RotationOrder::kYZX);

				// Extract the rotation in the space of the joint orient
				ndanim::JointParams jointPose = pPoseTable[jointIndex];
				jointPose.m_trans = Point(kOrigin) + (jointPose.m_trans - Point(kOrigin)) * (1.0f / g_animOptions.m_bakedScaleValue);
				const Quat mayaQuat = Conjugate(jointOrientQuat) * jointPose.m_quat;

				float rotX, rotY, rotZ;
				mayaQuat.GetEulerAngles(rotX, rotY, rotZ, (Quat::RotationOrder)pSkel->m_pJointRotateOrder[pSegmentContext->m_pSegment->m_firstJoint + jointIndex]); // joints in Maya may have various rotation orders -- set this to match the joint you're interested in!

// 				const char* rotOrderStr;
// 				switch (filter.m_drawJointParamsRotOrder)
// 				{
// 				case SMath::Quat::kXYZ: rotOrderStr = "XYZ"; break;
// 				case SMath::Quat::kYZX: rotOrderStr = "YZX"; break;
// 				case SMath::Quat::kZXY: rotOrderStr = "ZXY"; break;
// 				case SMath::Quat::kXZY: rotOrderStr = "XZY"; break;
// 				case SMath::Quat::kYXZ: rotOrderStr = "YXZ"; break;
// 				case SMath::Quat::kZYX:	rotOrderStr = "ZYX"; break;
// 				default:				rotOrderStr = "???"; break;
// 				}

				snprintf(buf + numCharsWritten, sizeof(buf) - numCharsWritten,
					"\nLocal Space\n"
					"Trans:       (%7.3f, %7.3f, %7.3f)\n"
// 					"Rot:         (%7.4f, %7.4f, %7.4f; %7.4f)\n"
					"Scale:       (%7.3f, %7.3f, %7.3f)\n"
					"Maya Rot:    [%7.3f, %7.3f, %7.3f] (%s)\n"
// 					"     Quat    (%7.4f, %7.4f, %7.4f; %7.4f)\n"
					"Maya Orient: [%7.3f, %7.3f, %7.3f] (%s)\n" // Maya joint orient
// 					"     Quat    (%7.4f, %7.4f, %7.4f; %7.4f)\n"
					"Object Space\n"
					"Trans:       (%7.3f, %7.3f, %7.3f)\n"
// 					"VecX:        (%7.3f, %7.3f, %7.3f)\n"
// 					"VecY:        (%7.3f, %7.3f, %7.3f)\n"
// 					"VecZ:        (%7.3f, %7.3f, %7.3f)\n"
					"Local Scale: (%7.3f, %7.3f, %7.3f)\n",
					(float)jointPose.m_trans.X(), (float)jointPose.m_trans.Y(), (float)jointPose.m_trans.Z(),
// 					(float)jointPose.m_quat.X(), (float)jointPose.m_quat.Y(), (float)jointPose.m_quat.Z(), (float)jointPose.m_quat.W(),
					(float)jointPose.m_scale.X(), (float)jointPose.m_scale.Y(), (float)jointPose.m_scale.Z(),
					RADIANS_TO_DEGREES(rotX), RADIANS_TO_DEGREES(rotY), RADIANS_TO_DEGREES(rotZ), "XYZ",
// 					(float)mayaQuat.X(), (float)mayaQuat.Y(), (float)mayaQuat.Z(), (float)mayaQuat.W(),
					RADIANS_TO_DEGREES(jointOrientX), RADIANS_TO_DEGREES(jointOrientY), RADIANS_TO_DEGREES(jointOrientZ), "XYZ",
// 					(float)jointOrientQuat.X(), (float)jointOrientQuat.Y(), (float)jointOrientQuat.Z(), (float)jointOrientQuat.W(),
					(float)jointTransform.GetTranslation().X(), (float)jointTransform.GetTranslation().Y(), (float)jointTransform.GetTranslation().Z(),
// 					(float)jointTransform.GetCol(0).X(), (float)jointTransform.GetCol(0).Y(), (float)jointTransform.GetCol(0).Z(),
// 					(float)jointTransform.GetCol(1).X(), (float)jointTransform.GetCol(1).Y(), (float)jointTransform.GetCol(1).Z(),
// 					(float)jointTransform.GetCol(2).X(), (float)jointTransform.GetCol(2).Y(), (float)jointTransform.GetCol(2).Z(),
					(float)jointTransform.GetScale().X(), (float)jointTransform.GetScale().Y(), (float)jointTransform.GetScale().Z());
			}
			buf[sizeof(buf) - 1] = '\0';

			pOutput->Printf(buf);
		}
	}
#endif

	return eStatus;
}


/// --------------------------------------------------------------------------------------------------------------- ///
static bool AssertPrintAnimCmdList(const Process* pOwner, const AnimCmdList* pAnimCmdList, const AnimCmd* pCurCmd)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (pAnimCmdList)
	{
		MsgOut("============================ Anim Commands for '%s' ============================\n", pOwner ? pOwner->GetName() : "<null>");

		PrintAnimCmdList(pAnimCmdList, GetMsgOutput(kMsgOut), pCurCmd);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ValidatePose(const Process* pOwner,
						 const AnimCmdList* pAnimCmdList,
						 const AnimCmd* pCurCmd,
						 const OrbisAnim::AnimHierarchySegment* pSegment,
						 const OrbisAnim::ProcessingGroup* pProcessingGroup,
						 const ArtItemSkeleton* pSkel,
						 const OrbisAnim::AnimHierarchy* pAnimHierarchy,
						 const ndanim::AnimatedJointPose* pJointPose)
{
	if (!FALSE_IN_FINAL_BUILD(g_animOptions.m_enableValidation))
	{
		return;
	}

	ANIM_ASSERT(pAnimHierarchy);
	ANIM_ASSERT(pSegment);
	ANIM_ASSERT(pProcessingGroup);
	ANIM_ASSERT(pJointPose);
//	ANIM_ASSERT(pJointPose->m_pValidBitsTable);


	// NB: this assumes that animated joints will always and only ever be inside the first channel group
	// and if we ever have more than one channel group its because the second one is all float channels
	U32F channelGroupIndex = pSegment->m_firstChannelGroup + pProcessingGroup->m_firstChannelGroup;

	{
		const U32F numJointsInGroup = pProcessingGroup->m_numAnimatedJoints;
		const U32F firstJointIndexInGroup = pProcessingGroup->m_firstAnimatedJoint;

		for (U32F jointIndex = 0; jointIndex < numJointsInGroup; ++jointIndex)
		{
			if (pJointPose->m_pValidBitsTable && !pJointPose->m_pValidBitsTable[channelGroupIndex].IsBitSet(jointIndex))
				continue;

			// NB: Can't assert on this earlier because its possible we didn't end up animating any joints
			// and thus never allocated a buffer for them
			ANIM_ASSERT(pJointPose->m_pJointParams);

			const U32F iJoint = firstJointIndexInGroup + jointIndex;
			const ndanim::JointParams& params = pJointPose->m_pJointParams[iJoint];

			ANIM_ASSERTF(IsReasonable(params.m_trans) || AssertPrintAnimCmdList(pOwner, pAnimCmdList, pCurCmd),
						 ("Invalid translation for joint '%s'", GetJointName(pSkel, iJoint, "<null>")));
			ANIM_ASSERTF(IsFinite(params.m_quat) || AssertPrintAnimCmdList(pOwner, pAnimCmdList, pCurCmd),
						 ("Invalid rotation for joint '%s'", GetJointName(pSkel, iJoint, "<null>")));
			ANIM_ASSERTF(IsNormal(params.m_quat) || AssertPrintAnimCmdList(pOwner, pAnimCmdList, pCurCmd),
						 ("De-normal rotation for joint '%s'", GetJointName(pSkel, iJoint, "<null>")));
			ANIM_ASSERTF(IsReasonable(params.m_scale) || AssertPrintAnimCmdList(pOwner, pAnimCmdList, pCurCmd),
						 ("Invalid scale for joint '%s'", GetJointName(pSkel, iJoint, "<null>")));
		}
	}

	if (pJointPose->m_pFloatChannels)
	{
		++channelGroupIndex;

		const U32F numFloatChannelsInGroup = pProcessingGroup->m_numFloatChannels;
		const U32F firstFloatChannelInGroup = pProcessingGroup->m_firstFloatChannel;

		const F32* pFloatChannels = (const F32*)pJointPose->m_pFloatChannels;

		for (U32F iFloat = 0; iFloat < numFloatChannelsInGroup; ++iFloat)
		{
			if (pJointPose->m_pValidBitsTable && !pJointPose->m_pValidBitsTable[channelGroupIndex].IsBitSet(iFloat))
				continue;

			ANIM_ASSERT(IsReasonable(pFloatChannels[iFloat]) || AssertPrintAnimCmdList(pOwner, pAnimCmdList, pCurCmd));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ValidatePose(const Process* pOwner,
						 const AnimCmdList* pAnimCmdList,
						 const AnimCmd* pCurCmd,
						 const OrbisAnim::ProcessingGroupContext* pGroupContext,
						 const ArtItemSkeleton* pSkel,
						 const OrbisAnim::AnimHierarchy* pAnimHierarchy,
						 const ndanim::AnimatedJointPose* pJointPose)
{
	if (!FALSE_IN_FINAL_BUILD(g_animOptions.m_enableValidation))
	{
		return;
	}

	ANIM_ASSERT(pAnimHierarchy);
	ANIM_ASSERT(pGroupContext);

	const OrbisAnim::ProcessingGroup* pProcessingGroup = pGroupContext->m_pProcessingGroup;
	const I32F iProcessingGroup = pGroupContext->m_iProcessingGroup;
	const OrbisAnim::AnimHierarchySegment* pSegment = pGroupContext->m_pSegmentContext->m_pSegment;

	ValidatePose(pOwner,
				 pAnimCmdList,
				 pCurCmd,
				 pSegment,
				 pProcessingGroup,
				 pSkel,
				 pAnimHierarchy,
				 pJointPose);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ValidateSegmentInPose(const Process* pOwner,
						   const AnimCmdList* pAnimCmdList,
						   const AnimCmd* pCurCmd,
						   const ArtItemSkeleton* pSkel,
						   const OrbisAnim::AnimHierarchy* pAnimHierarchy,
						   const OrbisAnim::SegmentContext* pSegmentContext,
						   const ndanim::AnimatedJointPose* pJointPose)
{
	if (FALSE_IN_FINAL_BUILD(!g_animOptions.m_enableValidation))
	{
		return;
	}

	ANIM_ASSERT(pAnimHierarchy);
	ANIM_ASSERT(pSegmentContext);

	const I32F iSegment = pSegmentContext->m_iSegment;
	const U32F numProcessingGroups = OrbisAnim::AnimHierarchy_GetNumProcessingGroupsInSegment(pAnimHierarchy, iSegment);
	const U32F firstProcGroupInSegment = OrbisAnim::AnimHierarchy_GetFirstProcessingGroupInSegment(pAnimHierarchy, iSegment);

	const OrbisAnim::ProcessingGroup* pProcessingGroups = OrbisAnim::AnimHierarchy_GetProcessingGroup(pAnimHierarchy,
																									  firstProcGroupInSegment);

	for (U32F groupIndex = 0; groupIndex < numProcessingGroups; ++groupIndex)
	{
		ValidatePose(pOwner,
					 pAnimCmdList,
					 pCurCmd,
					 pSegmentContext->m_pSegment,
					 &pProcessingGroups[groupIndex],
					 pSkel,
					 pAnimHierarchy,
					 pJointPose);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsInstanceEmpty(const OrbisAnim::ProcessingGroupContext* pGroupContext, I64 instance)
{
	const U32F numJointsInGroup = pGroupContext->m_pProcessingGroup->m_numAnimatedJoints;
	const U32F firstJointIndexInGroup = pGroupContext->m_pProcessingGroup->m_firstAnimatedJoint;

	U32F instanceIndex = instance * pGroupContext->m_pProcessingGroup->m_numChannelGroups;
	const OrbisAnim::ValidBits* pResultBits = pGroupContext->m_pValidOutputChannels + instanceIndex;
	bool empty = true;
	for (int channelGroup = 0; channelGroup < pGroupContext->m_pProcessingGroup->m_numChannelGroups; channelGroup++)
	{
		empty &= pResultBits[channelGroup].IsEmpty();
	}
	return empty;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ValidateInstance(const Process* pOwner,
							 const AnimCmdList* pAnimCmdList,
							 const AnimCmd* pCurCmd,
							 const OrbisAnim::ProcessingGroupContext* pGroupContext,
							 I64 instance,
							 bool ignoreValidBits = false)
{
	if (FALSE_IN_FINAL_BUILD(!g_animOptions.m_enableValidation))
	{
		return;
	}

	const U32F channelGroupInstanceIndex = instance * pGroupContext->m_pProcessingGroup->m_numChannelGroups;
	ANIM_ASSERT(channelGroupInstanceIndex < pGroupContext->m_numInstances * 2);
	void** plocChannelGroup = pGroupContext->m_plocChannelGroupInstance + channelGroupInstanceIndex;

	U32F iChannelGroup = 0;

	if (pGroupContext->m_pProcessingGroup->m_numAnimatedJoints > 0)
	{
		const U32F numJointsInGroup = pGroupContext->m_pProcessingGroup->m_numAnimatedJoints;
		const U32F firstJointIndexInGroup = pGroupContext->m_pProcessingGroup->m_firstAnimatedJoint;

		const ndanim::JointParams* pJointParams = (const ndanim::JointParams*)plocChannelGroup[iChannelGroup];
		const OrbisAnim::ValidBits* pResultBits = pGroupContext->m_pValidOutputChannels + channelGroupInstanceIndex + iChannelGroup;

		if (!ignoreValidBits && pResultBits)
		{
			for (U32F jointIndex = 0; jointIndex < numJointsInGroup; ++jointIndex)
			{
				if (pResultBits->IsBitSet(jointIndex))
				{
					ANIM_ASSERT((Dist(pJointParams[jointIndex].m_trans, kOrigin) < 1e8f) || AssertPrintAnimCmdList(pOwner, pAnimCmdList, pCurCmd));
					ANIM_ASSERT(IsReasonable(pJointParams[jointIndex].m_trans) || AssertPrintAnimCmdList(pOwner, pAnimCmdList, pCurCmd));
					ANIM_ASSERT(IsFinite(pJointParams[jointIndex].m_quat) || AssertPrintAnimCmdList(pOwner, pAnimCmdList, pCurCmd));
					ANIM_ASSERT(IsReasonable(pJointParams[jointIndex].m_scale) || AssertPrintAnimCmdList(pOwner, pAnimCmdList, pCurCmd));
				}
			}
		}

		++iChannelGroup;
	}

	if (pGroupContext->m_pProcessingGroup->m_numFloatChannels > 0)
	{
		const U32F numFloatChannelsInGroup = pGroupContext->m_pProcessingGroup->m_numFloatChannels;
		const U32F firstFloatChannelInGroup = pGroupContext->m_pProcessingGroup->m_firstFloatChannel;

		const F32* pFloatChannels = (const F32*)plocChannelGroup[iChannelGroup];
		const OrbisAnim::ValidBits* pResultBits = pGroupContext->m_pValidOutputChannels + channelGroupInstanceIndex + iChannelGroup;

		for (U32F iFloat = 0; iFloat < numFloatChannelsInGroup; ++iFloat)
		{
			if (!ignoreValidBits && pResultBits && !pResultBits->IsBitSet(iFloat))
			{
				continue;
			}

			ANIM_ASSERT(IsReasonable(pFloatChannels[iFloat]) || AssertPrintAnimCmdList(pOwner, pAnimCmdList, pCurCmd));
		}
	
		++iChannelGroup;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ConstructAnimHierarchyForAnimation(const ArtItemAnim* pArtItemAnim, 
											   const ArtItemSkeleton* pOriginalSkel, 
											   ArtItemSkeleton** ppOutSkel)
{
	typedef HashTable<U32, ArtItemSkeleton*> HierarchyIdToSkeletonTable;
	static HierarchyIdToSkeletonTable s_hierToSkeletonTable;
	static NdAtomicLock s_skelTableLock;

	{
		AtomicLockJanitor jan(&s_skelTableLock, FILE_LINE_FUNC);
		if (!s_hierToSkeletonTable.IsInitialized())
		{
			s_hierToSkeletonTable.Init(64, FILE_LINE_FUNC);
		}
		{
			HierarchyIdToSkeletonTable::iterator it = s_hierToSkeletonTable.Find(pArtItemAnim->m_pClipData->m_animHierarchyId);
			if (it != s_hierToSkeletonTable.End())
			{
				*ppOutSkel = it->m_data;

				ArtItemSkeleton* pResultSkel = *ppOutSkel;

				// Make sure channels match
				if (const DebugSkelInfo* pInfo = pArtItemAnim->m_pDebugSkelInfo)
				{
					const U32 numSegments = pInfo->m_numSegments;

					for (U32F iSeg = 0; iSeg < numSegments; ++iSeg)
					{
						for (U32F iProc = 0; iProc < pInfo->m_pSkelSegmentDetails[iSeg].m_numProcessingGroups; ++iProc)
						{
							int groupIndex = pInfo->m_pSkelSegmentDetails[iSeg].m_firstProcessingGroup + iProc;			

							const ndanim::ClipData*	 pAnimClip = pArtItemAnim->m_pClipData;
							OrbisAnim::AnimClipGroupHeader const* pClipGroup = (OrbisAnim::AnimClipGroupHeader const*)((uintptr_t)pAnimClip + pAnimClip->m_groupHeadersOffset + (groupIndex)*sizeof(OrbisAnim::AnimClipGroupHeader));

							const ndanim::JointHierarchy* pResHierarchy = pResultSkel->m_pAnimHierarchy;
							ANIM_ASSERT(groupIndex < GetNumProcessingGroups(pResHierarchy));

							const bool numCgMismatch = GetNumChannelGroupsInProcessingGroup(pResHierarchy, groupIndex)
													   != pInfo->m_pProcessingGroupDetails[groupIndex].m_numChannelGroups;

							const bool numAjMismatch = OrbisAnim::AnimHierarchy_GetNumAnimatedJointsInGroup(pResHierarchy,
																											groupIndex)
													   != pClipGroup->m_numAnimatedJoints;

							const bool numFcMismatch = OrbisAnim::AnimHierarchy_GetNumFloatChannelsInGroup(pResHierarchy,
																										   groupIndex)
													   != pClipGroup->m_numFloatChannels;

							if (numCgMismatch || numAjMismatch || numFcMismatch)
							{
								*ppOutSkel = nullptr;
								return;
							}
						}
					}
				}

				return;
			}
		}
	}

	const DebugSkelInfo* pInfo = pArtItemAnim->m_pDebugSkelInfo;
	if (pArtItemAnim->m_flags & ArtItemAnim::kStreamingChunk)
	{
		AnimStream* pAnimStream =	EngineComponents::GetAnimStreamManager()->GetAnimStreamFromChunk(pArtItemAnim);
		const ArtItemAnim* pBaseAnim = pAnimStream->GetArtItemAnimForChunk(pArtItemAnim);
		pInfo = pBaseAnim->m_pDebugSkelInfo;
	}

	if (!pInfo)
	{
		*ppOutSkel = nullptr;
		return;
	}

	const U32 numTotalJoints = pInfo->m_numTotalJoints;
	const U32 numSegments = pInfo->m_numSegments;

	U32F numAnimatedJointRanges = 0;
	U32F numProcessingGroups = 0;
	U32F numChannelGroups = 0;
	U32F numFloatChannels = 0;
	for (U32F iSeg = 0; iSeg < numSegments; ++iSeg)
	{
		if (pInfo->m_pSkelSegmentDetails[iSeg].m_numAnimatedJoints > 0)
			++numAnimatedJointRanges;

		numProcessingGroups += pInfo->m_pSkelSegmentDetails[iSeg].m_numProcessingGroups;

		for (U32F iProc = 0; iProc < pInfo->m_pSkelSegmentDetails[iSeg].m_numProcessingGroups; ++iProc)
		{
			const int procGroupIndex = pInfo->m_pSkelSegmentDetails[iSeg].m_firstProcessingGroup + iProc;
			numChannelGroups += pInfo->m_pProcessingGroupDetails[procGroupIndex].m_numChannelGroups;
			numFloatChannels += pInfo->m_pProcessingGroupDetails[procGroupIndex].m_numFloatChannels;

			const ndanim::ClipData*	 pAnimClip = pArtItemAnim->m_pClipData;
			OrbisAnim::AnimClipGroupHeader const* pClipGroup = (OrbisAnim::AnimClipGroupHeader const*)((uintptr_t)pAnimClip + pAnimClip->m_groupHeadersOffset + (procGroupIndex)*sizeof(OrbisAnim::AnimClipGroupHeader));
			
			if (pClipGroup->m_numFloatChannels != pInfo->m_pProcessingGroupDetails[procGroupIndex].m_numFloatChannels||
				pClipGroup->m_numAnimatedJoints != pInfo->m_pProcessingGroupDetails[procGroupIndex].m_numAnimatedJoints)
			{
				*ppOutSkel = nullptr;
				return;
			}
		}
	}

	int totalSize = AlignSize(sizeof(ArtItemSkeleton), kAlign16)
					+ AlignSize(sizeof(ndanim::JointHierarchy), kAlign16)
					+ AlignSize(sizeof(OrbisAnim::ProcessingGroup) * numProcessingGroups, kAlign16)
					+ AlignSize(sizeof(OrbisAnim::AnimHierarchySegment) * numSegments, kAlign16)
					+ AlignSize(sizeof(OrbisAnim::JointParams) * numTotalJoints, kAlign16);

	HeapAllocator allocator("Skeleton alloc");
	void* pMem =  NDI_NEW (kAlign16) U8[totalSize];
	allocator.InitAsHeap(pMem, totalSize);

	Memory::PushAllocator(&allocator, FILE_LINE_FUNC);

	// Allocate the skeleton. Hack hack hack
	void* pSkelData = NDI_NEW (kAlign16) U8[sizeof(ArtItemSkeleton)];
	ArtItemSkeleton* pSkel = (ArtItemSkeleton*)pSkelData;
	memset(pSkel, 0, sizeof(ArtItemSkeleton));

	// Allocate the hierarchy itself
	ndanim::JointHierarchy* pJointHierarchy = NDI_NEW (kAlign16) ndanim::JointHierarchy;
	memset(pJointHierarchy, 0, sizeof(ndanim::JointHierarchy));

	// Allocate the ProcessingGroups
	OrbisAnim::ProcessingGroup* pProcessingGroups = NDI_NEW (kAlign16) OrbisAnim::ProcessingGroup[numProcessingGroups];
	memset(pProcessingGroups, 0, sizeof(OrbisAnim::ProcessingGroup) * numProcessingGroups);

	for (U32 iSeg = 0; iSeg < numSegments; ++iSeg)
	{
		U32F firstAnimatedJoint = 0;
		U32F firstChannelGroup = 0;
		U32F firstFloatChannel = 0;

		const SkelSegmentDetails* pSegDetails = &pInfo->m_pSkelSegmentDetails[iSeg];
		if (pSegDetails->m_numAnimatedJoints == 0)
			continue;

		for (U32 iProc = 0; iProc < pSegDetails->m_numProcessingGroups; ++iProc)
		{
			const SkelProcGroupDetails* pProcGroupDetails = &pInfo->m_pProcessingGroupDetails[pSegDetails->m_firstProcessingGroup + iProc];

			pProcessingGroups[pSegDetails->m_firstProcessingGroup + iProc].m_firstAnimatedJoint = firstAnimatedJoint;
			pProcessingGroups[pSegDetails->m_firstProcessingGroup + iProc].m_firstChannelGroup = firstChannelGroup;
			pProcessingGroups[pSegDetails->m_firstProcessingGroup + iProc].m_firstFloatChannel = firstFloatChannel;
			pProcessingGroups[pSegDetails->m_firstProcessingGroup + iProc].m_numAnimatedJoints = pProcGroupDetails->m_numAnimatedJoints;
			pProcessingGroups[pSegDetails->m_firstProcessingGroup + iProc].m_numChannelGroups = pProcGroupDetails->m_numChannelGroups;
			pProcessingGroups[pSegDetails->m_firstProcessingGroup + iProc].m_numFloatChannels = pProcGroupDetails->m_numFloatChannels;

			firstAnimatedJoint += pProcGroupDetails->m_numAnimatedJoints;
			firstChannelGroup += pProcGroupDetails->m_numChannelGroups;
			firstFloatChannel += pProcGroupDetails->m_numFloatChannels;
		}
	}

	// Allocate the Segments
	OrbisAnim::AnimHierarchySegment* pSegments = NDI_NEW (kAlign16) OrbisAnim::AnimHierarchySegment[numSegments];
	memset(pSegments, 0, sizeof(OrbisAnim::AnimHierarchySegment) * numSegments);
	U32F numChannelGroupsInPreviousSegments = 0;
	U32F numAnimatedJoints = 0;
	for (U32 iSeg = 0; iSeg < numSegments; ++iSeg)
	{
		const SkelSegmentDetails* pSegDetails = &pInfo->m_pSkelSegmentDetails[iSeg];

		U32F numChannelGroupsInSegment = 0;
		U32F numFloatChannelsInSegment = 0;
		for (U32F iProc = 0; iProc < pInfo->m_pSkelSegmentDetails[iSeg].m_numProcessingGroups; ++iProc)
		{
			numChannelGroupsInSegment += pInfo->m_pProcessingGroupDetails[pInfo->m_pSkelSegmentDetails[iSeg].m_firstProcessingGroup + iProc].m_numChannelGroups;
			numFloatChannelsInSegment += pInfo->m_pProcessingGroupDetails[pInfo->m_pSkelSegmentDetails[iSeg].m_firstProcessingGroup + iProc].m_numFloatChannels;
		}

		pSegments[iSeg].m_firstJoint = pSegDetails->m_firstJoint;
		pSegments[iSeg].m_firstProcessingGroup = pSegDetails->m_firstProcessingGroup;
		pSegments[iSeg].m_firstChannelGroup = numChannelGroupsInPreviousSegments;
// 		pSegments[iSeg].m_firstAnimatedJointRange = iSeg;
		pSegments[iSeg].m_firstCommandBlock = 0;
		pSegments[iSeg].m_numJoints = pSegDetails->m_numJoints;
		pSegments[iSeg].m_numProcessingGroups = pSegDetails->m_numProcessingGroups;
//		pSegments[iSeg].m_numChannelGroups = numChannelGroupsInSegment;
// 		pSegments[iSeg].m_numAnimatedJointRanges = (pSegDetails->m_numAnimatedJoints > 0) ? 1 : 0;
		pSegments[iSeg].m_numCommandBlocks = 0;
		pSegments[iSeg].m_numAnimatedJoints = pSegDetails->m_numAnimatedJoints;
		pSegments[iSeg].m_numScalars = numFloatChannelsInSegment;

		numAnimatedJoints += pSegDetails->m_numAnimatedJoints;
		numChannelGroupsInPreviousSegments += numChannelGroupsInSegment;
	}


	// Now, fill in all the fields in the skeleton
	pJointHierarchy->m_magic = OrbisAnim::kAnimHierarchyMagicVersion_Current;
	pJointHierarchy->m_animHierarchyId = pArtItemAnim->m_pClipData->m_animHierarchyId;
	pJointHierarchy->m_gameHierarchyId = pArtItemAnim->m_pClipData->m_animHierarchyId;
	pJointHierarchy->m_maxInstances = 0xFF;

	pJointHierarchy->m_numAnimatedJoints = numAnimatedJoints;
	pJointHierarchy->m_numTotalJoints = numTotalJoints;
	pJointHierarchy->m_numFloatChannels = numFloatChannels;
	pJointHierarchy->m_dependencyTableSize = pOriginalSkel->m_pAnimHierarchy->m_dependencyTableSize;

// 	pJointHierarchy->m_numAnimatedJointRanges = numAnimatedJointRanges;
// 	ANIM_ASSERT(((U8*)pAnimatedJointRanges - (U8*)pJointHierarchy) > 0);
// 	pJointHierarchy->m_animatedJointRangeOffset = (U8*)pAnimatedJointRanges - (U8*)pJointHierarchy;

//	pJointHierarchy->m_numProcessingGroups = numProcessingGroups;
	ANIM_ASSERT(((U8*)pProcessingGroups - (U8*)pJointHierarchy) > 0);
	pJointHierarchy->m_processingGroupOffset = (U8*)pProcessingGroups - (U8*)pJointHierarchy;
	pJointHierarchy->m_numChannelGroups = numChannelGroups;

	pJointHierarchy->m_numSegments = numSegments;
	ANIM_ASSERT(((U8*)pSegments - (U8*)pJointHierarchy) > 0);
	pJointHierarchy->m_segmentOffset = (U8*)pSegments - (U8*)pJointHierarchy;

	// Hack for now until the debug skel info has the default joint data
	OrbisAnim::JointParams* pDefaultJointParams = NDI_NEW(kAlign16) OrbisAnim::JointParams[pJointHierarchy->m_numTotalJoints];
	for (int i = 0; i < pJointHierarchy->m_numAnimatedJoints; ++i)
	{
		pDefaultJointParams[i].m_scale = pInfo->m_pDefaultPoses[i].m_scale;
		pDefaultJointParams[i].m_quat = pInfo->m_pDefaultPoses[i].m_quat;
		pDefaultJointParams[i].m_trans = pInfo->m_pDefaultPoses[i].m_trans;
	}
	pJointHierarchy->m_defaultPoseOffset = (U8*)pDefaultJointParams - (U8*)pJointHierarchy;


	// Fill in the skeleton
	pSkel->m_pAnimHierarchy = pJointHierarchy;
	pSkel->m_numSegments = numSegments;
	pSkel->m_numTotalJoints = numTotalJoints;
	pSkel->m_numGameplayJoints = pInfo->m_pSkelSegmentDetails[0].m_numJoints;
	pSkel->m_numAnimatedGameplayJoints = pInfo->m_pSkelSegmentDetails[0].m_numAnimatedJoints;
	pSkel->m_skelId = pArtItemAnim->m_skelID;
	pSkel->m_hierarchyId = pJointHierarchy->m_animHierarchyId;

	(*ppOutSkel) = pSkel;

	Memory::PopAllocator();

	{
		AtomicLockJanitor jan(&s_skelTableLock, FILE_LINE_FUNC);
		s_hierToSkeletonTable.Add(pArtItemAnim->m_pClipData->m_animHierarchyId, pSkel);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ConstructRetargetEntry(const ArtItemSkeleton* pSrcSkel,
								   const StringId64* pSrcJointNames,
								   const ArtItemSkeleton* pDestSkel,
								   SkelTable::RetargetEntry** pOutRetargetEntry)
{
	typedef HashTable<hashablePair<U32, U32>, SkelTable::RetargetEntry*> RetargetEntryTable;
	static RetargetEntryTable s_constructedRetargetEntries;
	static NdAtomicLock s_constructedRetargetEntriesLock;

	{
		AtomicLockJanitor jan(&s_constructedRetargetEntriesLock, FILE_LINE_FUNC);
		if (!s_constructedRetargetEntries.IsInitialized())
		{
			s_constructedRetargetEntries.Init(128, FILE_LINE_FUNC);
		}

		RetargetEntryTable::Key k = RetargetEntryTable::Key(pSrcSkel->m_pAnimHierarchy->m_animHierarchyId,
															pDestSkel->m_pAnimHierarchy->m_animHierarchyId);

		RetargetEntryTable::Iterator it = s_constructedRetargetEntries.Find(k);
		if (it != s_constructedRetargetEntries.End())
		{
			*pOutRetargetEntry = it->m_data;
			return;
		}
	}

	SkelTable::RetargetEntry* pRetargetEntry = NDI_NEW (kAlign16) SkelTable::RetargetEntry;
	memset(pRetargetEntry, 0, sizeof(SkelTable::RetargetEntry));

	// Get the first skeleton
	U32F numTotalSrcJoints = pSrcSkel->m_pAnimHierarchy->m_numTotalJoints;
	const ndanim::JointHierarchy* pSrcJoints = pSrcSkel->m_pAnimHierarchy;

	// Get the second skeleton
	U32F numTotalDestJoints = pDestSkel->m_pAnimHierarchy->m_numTotalJoints;
	const ndanim::JointHierarchy* pDestJoints = pDestSkel->m_pAnimHierarchy;

	// Update the skeleton ids
	pRetargetEntry->m_srcSkelId = pSrcSkel->m_skelId;
	pRetargetEntry->m_destSkelId = pDestSkel->m_skelId;
	pRetargetEntry->m_srcHierarchyId = pSrcSkel->m_pAnimHierarchy->m_animHierarchyId;
	pRetargetEntry->m_destHierarchyId = pDestSkel->m_pAnimHierarchy->m_animHierarchyId;
	pRetargetEntry->m_jointRetarget = NDI_NEW (kAlign16) SkelTable::JointRetarget[2048];		// Calculate this 

	ANIM_ASSERT(pRetargetEntry->m_jointRetarget);

	U32F count = 0;
	for (U32F ii = 0; ii < numTotalSrcJoints; ++ii)
	{
		if (!ndanim::IsJointAnimated(pSrcSkel->m_pAnimHierarchy, ii))
			continue;

		for (U32F jj = 0; jj < numTotalDestJoints; ++jj)
		{
			if (!ndanim::IsJointAnimated(pDestSkel->m_pAnimHierarchy, jj))
				continue;

			if (pSrcJointNames[ii] == pDestSkel->m_pJointDescs[jj].m_nameId)
			{
				ANIM_ASSERTF(count < 2048, ("Exceeded default retarget joint count"));

				SkelTable::JointRetarget& jointRetarget = pRetargetEntry->m_jointRetarget[count];

				const I16 srcIndex = (I16)ii;
				const I16 destIndex = (I16)jj;

				jointRetarget.m_srcIndex = srcIndex;
				jointRetarget.m_destIndex = destIndex;
				ANIM_ASSERT(srcIndex < numTotalSrcJoints);
				ANIM_ASSERT(destIndex < numTotalDestJoints);
				jointRetarget.m_srcAnimIndex = OrbisAnim::AnimHierarchy_OutputJointIndexToAnimatedJointIndex(pSrcJoints, jointRetarget.m_srcIndex);
				jointRetarget.m_destAnimIndex = OrbisAnim::AnimHierarchy_OutputJointIndexToAnimatedJointIndex(pDestJoints, jointRetarget.m_destIndex);
				jointRetarget.m_retargetMode = DC::kBoneRetargetModeDefault;

				jointRetarget.m_parentBindPoseDelta.AsLocator(Locator(kIdentity));
				jointRetarget.m_jointBindPoseDelta.AsLocator(Locator(kIdentity));

				jointRetarget.m_boneScale = MakeF16(1.0f);

				SkelTable::ResolveValidBitMapping(jointRetarget, pSrcSkel, pDestSkel);

				count++;
				break;
			}
		}
	}

	pRetargetEntry->m_count = count;
	pRetargetEntry->m_scale = 1.0f;

	(*pOutRetargetEntry) = pRetargetEntry;

	{
		AtomicLockJanitor jan(&s_constructedRetargetEntriesLock, FILE_LINE_FUNC);

		if (!s_constructedRetargetEntries.IsFull())
		{
			s_constructedRetargetEntries.Add(RetargetEntryTable::Key(pSrcSkel->m_pAnimHierarchy->m_animHierarchyId,
																	 pDestSkel->m_pAnimHierarchy->m_animHierarchyId),
											 pRetargetEntry);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool EvaluateClip_DebugPreEvalCallback(OrbisAnim::ProcessingGroupContext* pGroupContext,
											  const AnimCmd_EvaluateClip* pCmdEvalClip)
{
#if FINAL_BUILD
	return true;
#else
	if (LIKELY(!g_animOptions.m_matchPose))
		return true;

	bool performNormalEvalClip = true;

	OrbisAnim::ProcessingGroup const& groupDesc = *pGroupContext->m_pProcessingGroup;
	if (groupDesc.m_numAnimatedJoints != 0)
	{
		// evaluating an instance of a group
		// each instance can have one (joints) or two (joints + float channels) channel groups
		// there is one or two outputs per instance
		
		U32F iOutputIndex = pCmdEvalClip->m_outputInstance * groupDesc.m_numChannelGroups;


		void** plocChannelGroup = pGroupContext->m_plocChannelGroupInstance + iOutputIndex; // ptrs to output streams

		F32* pJointChannels = (F32*)(uintptr_t)((uintptr_t)(plocChannelGroup[0]) &~1);

		F32* pFloatChannelsOutputs = nullptr;

		if (groupDesc.m_numChannelGroups > 1)
			pFloatChannelsOutputs = (F32*)(uintptr_t)((uintptr_t)(plocChannelGroup[1]) &~1);

		if (pJointChannels)
		{
			float frameOverride = NDI_FLT_MAX;
			float* pCachedData = nullptr;
			float* pCachedFloatChannelData = nullptr;
			float* pRequestWriteBackData = nullptr;
			float* pRequestFloatChannelWriteBackData = nullptr;
			bool performEvaluateClip = (g_animOptions.m_matchPose)(pCmdEvalClip->m_pArtItemAnim,
																   groupDesc.m_firstAnimatedJoint,
																   groupDesc.m_numAnimatedJoints,
																   pJointChannels,
																   groupDesc.m_numFloatChannels,
																   pFloatChannelsOutputs,
																   pCmdEvalClip->m_frame,
																   &frameOverride,
																   &pCachedData,
																   &pCachedFloatChannelData,
																   &pRequestWriteBackData,
																   &pRequestFloatChannelWriteBackData,
																   performNormalEvalClip);

			if (performEvaluateClip && pCachedData && pRequestWriteBackData)
			{
				OrbisAnim::EvaluateClip(pGroupContext, 
										pCmdEvalClip->m_outputInstance,
										pCmdEvalClip->m_pArtItemAnim->m_pClipData, // check clip data for additive flag
										frameOverride != NDI_FLT_MAX ? frameOverride : pCmdEvalClip->m_frame,
										nullptr);

				memcpy(pRequestWriteBackData, pJointChannels, groupDesc.m_numAnimatedJoints * 12 * sizeof(float));
				memcpy(pJointChannels, pCachedData, groupDesc.m_numAnimatedJoints * 12 * sizeof(float));

				if (pFloatChannelsOutputs)
				{
					memcpy(pRequestFloatChannelWriteBackData, pFloatChannelsOutputs, groupDesc.m_numFloatChannels * sizeof(float));
					memcpy(pFloatChannelsOutputs, pCachedFloatChannelData, groupDesc.m_numFloatChannels * sizeof(float));
				}
			}

			if (!performNormalEvalClip)
			{
				OrbisAnim::EvaluateClip(pGroupContext,
										pCmdEvalClip->m_outputInstance,
										pCmdEvalClip->m_pArtItemAnim->m_pClipData,
										0.0f, // call evaluate clip to do setup of valid bits, etc. evaluate 0 phase. phase doesnt matter since we will override this completely
										nullptr);
			}
		}

// 		for (int iJoint = 0; iJoint < groupDesc.m_numAnimatedJoints; ++iJoint)
// 		{
// 			float *pJoint = &pJointChannels[12 * iJoint];
// 			memset(pJoint, 0xec, sizeof(float) * 12);
// 
// 			//memset(pJoint, 0, sizeof(float) * 12);
// 		}
	}

	return performNormalEvalClip;

#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void SanitateInstance(const OrbisAnim::ProcessingGroupContext* pGroupContext, I64 instance)
{
	STRIP_IN_FINAL_BUILD;
	if (g_animOptions.m_sanitizePoses)
	{
		if (pGroupContext->m_pProcessingGroup->m_numAnimatedJoints == 0)
			return;

		const U32F numJointsInGroup = pGroupContext->m_pProcessingGroup->m_numAnimatedJoints;
		const U32F firstJointIndexInGroup = pGroupContext->m_pProcessingGroup->m_firstAnimatedJoint;

		U32F instanceIndex = instance * pGroupContext->m_pProcessingGroup->m_numChannelGroups;
		const OrbisAnim::ValidBits* pResultBits = pGroupContext->m_pValidOutputChannels + instanceIndex;
		void** plocChannelGroup = pGroupContext->m_plocChannelGroupInstance + instanceIndex;
		ndanim::JointParams* pJointParams = (ndanim::JointParams*)plocChannelGroup[0];

		for (U32F jointIndex = 0; jointIndex < numJointsInGroup; ++jointIndex)
		{
			if (pResultBits->IsBitSet(jointIndex))
			{
				if (!IsFinite(pJointParams[jointIndex].m_trans)
					|| !IsNormal(pJointParams[jointIndex].m_quat)
					|| !IsFinite(pJointParams[jointIndex].m_scale))
				{
					pJointParams[jointIndex].m_trans = kZero;
					pJointParams[jointIndex].m_quat = kIdentity;
					pJointParams[jointIndex].m_scale = Vector(1.0f, 1.0f, 1.0f);
					const_cast<OrbisAnim::ValidBits*>(pResultBits)->ClearBit(jointIndex);
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool EvaluateClip_DebugPostEvalCallback(OrbisAnim::ProcessingGroupContext* pGroupContext,
											   const AnimCmd_EvaluateClip* pCmdEvalClip)
{
#if FINAL_BUILD
	return false;
#else
	
	if (LIKELY(!g_animOptions.m_adjustPose))
			return false;

	OrbisAnim::ProcessingGroup const& groupDesc = *pGroupContext->m_pProcessingGroup;
	if (groupDesc.m_numAnimatedJoints != 0)
	{
		U32F iOutputIndex = pCmdEvalClip->m_outputInstance * groupDesc.m_numChannelGroups;
		void** plocChannelGroup = pGroupContext->m_plocChannelGroupInstance + iOutputIndex;
		F32* pJointChannels = (F32*)(uintptr_t)((uintptr_t)(plocChannelGroup[0]) &~1);

		F32* pFloatChannelsOutputs = nullptr;

		if (groupDesc.m_numChannelGroups > 1)
			pFloatChannelsOutputs = (F32*)(uintptr_t)((uintptr_t)(plocChannelGroup[1]) &~1);

		if (pJointChannels)
		{
			return (g_animOptions.m_adjustPose)(pCmdEvalClip->m_pArtItemAnim,
												groupDesc.m_firstAnimatedJoint,
												groupDesc.m_numAnimatedJoints,
												pJointChannels,
												groupDesc.m_numFloatChannels,
												pFloatChannelsOutputs,
												pCmdEvalClip->m_frame);
		}
	}

	return false;
#endif
}


/// --------------------------------------------------------------------------------------------------------------- ///
static bool DoOutOfDateAnimRetargeting(const AnimCmd_EvaluateClip* pCmdEvalClip,
									   const AnimExecutionContext* pContext,
									   const OrbisAnim::SegmentContext& segmentContext,
									   OrbisAnim::ProcessingGroupContext* pGroupContext,
									   const AnimCmdList* pAnimCmdList)
{
	PROFILE(Animation, EvalClip_Retarget_SLOW);

	ScopedTempAllocator jj2(FILE_LINE_FUNC);

	Memory::PushAllocator(kAllocDebug, FILE_LINE_FUNC);

	// Create a custom skeleton
	ArtItemSkeleton* pSrcAnimSkel = nullptr;
	ConstructAnimHierarchyForAnimation(pCmdEvalClip->m_pArtItemAnim, pContext->m_pSkel, &pSrcAnimSkel);
	Memory::PopAllocator();

	//ANIM_ASSERT(pSrcAnimSkel);
	if (!pSrcAnimSkel)
		return false;		// We couldn't construct the anim hierarchy for some reason. Abort.

	bool instanceEvaluated = false;
	if (pSrcAnimSkel)
	{
		Memory::PushAllocator(kAllocDebug, FILE_LINE_FUNC);
		// Create a custom retarget entry
		SkelTable::RetargetEntry* pRetargetEntry = nullptr;

		ConstructRetargetEntry(pSrcAnimSkel,
							   pCmdEvalClip->m_pArtItemAnim->m_pDebugSkelInfo->m_pJointNameIds,
							   pContext->m_pSkel,
							   &pRetargetEntry);

		Memory::PopAllocator();

		if (pRetargetEntry && !pRetargetEntry->m_disabled)
		{
			ANIM_ASSERT(pRetargetEntry->m_jointRetarget);

			const ndanim::JointHierarchy* pTgtHier = pContext->m_pSkel->m_pAnimHierarchy;
			const U32F tgtNumChannelGroups = ndanim::GetNumChannelGroups(pTgtHier);
			const U32F tgtValidBitsSize = tgtNumChannelGroups * sizeof(ndanim::ValidBits);
			const U32F tgtFloatChannelSize = pTgtHier->m_numFloatChannels * sizeof(float);
			const U32F tgtJointCount = pTgtHier->m_numTotalJoints;
			const U32F tgtJointParamsSize = tgtJointCount * sizeof(ndanim::JointParams);

			// Retarget the out-of-date animation onto the current skeleton
			char* pOutputBuffer = NDI_NEW (kAlign16) char[tgtValidBitsSize + tgtJointParamsSize + tgtFloatChannelSize];

			ndanim::AnimatedJointPose outJointPose;
			outJointPose.m_pValidBitsTable = reinterpret_cast<ndanim::ValidBits*>(pOutputBuffer);
			outJointPose.m_pJointParams = reinterpret_cast<ndanim::JointParams*>(pOutputBuffer + tgtValidBitsSize);
			ANIM_ASSERT(outJointPose.m_pJointParams);
			outJointPose.m_pFloatChannels = reinterpret_cast<float*>(pOutputBuffer + tgtValidBitsSize + tgtJointParamsSize);

			RetargetInput ri;
			ri.m_pArtItemAnim = pCmdEvalClip->m_pArtItemAnim;
			ri.m_sample = pCmdEvalClip->m_frame;
			RetargetPoseForSegment(ri,
								   pSrcAnimSkel,
								   pContext->m_pSkel,
								   pRetargetEntry,
								   segmentContext.m_iSegment,
								   &outJointPose);

			OrbisAnim::EvaluatePose(pGroupContext,
									pCmdEvalClip->m_outputInstance,
									(OrbisAnim::JointParams*)outJointPose.m_pJointParams,
									outJointPose.m_pFloatChannels,
									(OrbisAnim::ValidBits*)outJointPose.m_pValidBitsTable);

			ValidateInstance(pContext->m_pAnimDataHack ? pContext->m_pAnimDataHack->m_hProcess.ToProcess() : nullptr,
							 pAnimCmdList,
							 pCmdEvalClip,
							 pGroupContext,
							 pCmdEvalClip->m_outputInstance);

			instanceEvaluated = true;
		}
	}

	return instanceEvaluated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DoSkelToSkelRetargeting(const AnimExecutionContext* pContext,
									const OrbisAnim::SegmentContext& segmentContext,
									OrbisAnim::ProcessingGroupContext* pGroupContext,
									const ArtItemSkeleton* pSrcSkel,
									const AnimCmd_EvaluateClip* pCmdEvalClip,
									const SkelTable::RetargetEntry* pRetargetEntry,
									const ArtItemSkeleton* pDstSkel,
									const OrbisAnim::AnimHierarchy* pHierarchy,
									const AnimCmdList* pAnimCmdList)
{
	ANIM_ASSERT(pRetargetEntry->m_jointRetarget);

	if (nullptr == pRetargetEntry->m_jointRetarget)
	{
		OrbisAnim::EvaluateEmptyPose(pGroupContext, pCmdEvalClip->m_outputInstance);
	}
	else
	{
		// Retarget the out-of-date animation onto the current skeleton
		ndanim::AnimatedJointPose* pOutJointPose = nullptr;

		// Push a scoped temp allocator for the output buffers since we don't need them to persist outside of this function
		ScopedTempAllocator jj2(FILE_LINE_FUNC);

		const ndanim::JointHierarchy* pTgtHier = pContext->m_pSkel->m_pAnimHierarchy;
		const U32F tgtNumChannelGroups = ndanim::GetNumChannelGroups(pTgtHier);
		const U32F tgtValidBitsSize = tgtNumChannelGroups * sizeof(ndanim::ValidBits);
		const U32F tgtFloatChannelSize = pTgtHier->m_numFloatChannels * sizeof(float);
		const U32F tgtJointCount = pTgtHier->m_numTotalJoints;
		const U32F tgtJointParamsSize = tgtJointCount * sizeof(ndanim::JointParams);

		// Retarget the out-of-date animation onto the current skeleton
		char* pOutputBuffer = NDI_NEW(kAlign16) char[tgtValidBitsSize + tgtJointParamsSize + tgtFloatChannelSize];
		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_enableValidation))
		{
			memset(pOutputBuffer, 0x11, tgtValidBitsSize + tgtJointParamsSize + tgtFloatChannelSize);
		}

		ndanim::AnimatedJointPose outJointPose;
		outJointPose.m_pValidBitsTable = reinterpret_cast<ndanim::ValidBits*>(pOutputBuffer);
		outJointPose.m_pJointParams = reinterpret_cast<ndanim::JointParams*>(pOutputBuffer + tgtValidBitsSize);
		ANIM_ASSERT(outJointPose.m_pJointParams);
		outJointPose.m_pFloatChannels = reinterpret_cast<float*>(pOutputBuffer + tgtValidBitsSize + tgtJointParamsSize);

		RetargetInput ri;
		ri.m_pArtItemAnim = pCmdEvalClip->m_pArtItemAnim;
		ri.m_sample = pCmdEvalClip->m_frame;
		RetargetPoseForSegment(ri,
							   pSrcSkel,
							   pContext->m_pSkel,
							   pRetargetEntry,
							   segmentContext.m_iSegment,
							   &outJointPose);

		ValidatePose(pContext->m_pAnimDataHack ? pContext->m_pAnimDataHack->m_hProcess.ToProcess() : nullptr,
					 pAnimCmdList,
					 pCmdEvalClip,
					 pGroupContext,
					 pDstSkel,
					 pHierarchy,
					 &outJointPose);

		// Now evaluate the newly created pose
		OrbisAnim::EvaluatePose(pGroupContext,
								pCmdEvalClip->m_outputInstance,
								(OrbisAnim::JointParams*)outJointPose.m_pJointParams,
								outJointPose.m_pFloatChannels,
								(OrbisAnim::ValidBits*)outJointPose.m_pValidBitsTable);

		ValidateInstance(pContext->m_pAnimDataHack ? pContext->m_pAnimDataHack->m_hProcess.ToProcess() : nullptr,
						 pAnimCmdList,
						 pCmdEvalClip,
						 pGroupContext,
						 pCmdEvalClip->m_outputInstance);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DoDoubleRetargeting(const AnimExecutionContext* pContext,
								const OrbisAnim::SegmentContext* pSegmentContext,
								OrbisAnim::ProcessingGroupContext* pGroupContext,
								const ArtItemSkeleton* pSrcSkel,
								const AnimCmd_EvaluateClip* pCmdEvalClip,
								const SkelTable::RetargetEntry* pRetargetEntry,
								const ArtItemSkeleton* pDstSkel,
								const OrbisAnim::AnimHierarchy* pHierarchy,
								const AnimCmdList* pAnimCmdList)
{
	ANIM_ASSERT(pCmdEvalClip);
	
	const ArtItemAnim* pSourceAnim = pCmdEvalClip->m_pArtItemAnim;
	
	// Double retargeting...
	ANIM_ASSERT(pSourceAnim);

	// If we didn't load debug pages
	if ((uintptr_t)pSourceAnim->m_pDebugSkelInfo == PackageMgr::kDebugPageMagic)
		return false;

	// Create a custom skeleton
	Memory::PushAllocator(kAllocDebug, FILE_LINE_FUNC);
	ArtItemSkeleton* pSrcAnimSkel = nullptr;
	ConstructAnimHierarchyForAnimation(pSourceAnim, pContext->m_pSkel, &pSrcAnimSkel);
	Memory::PopAllocator();

	ANIM_ASSERT(pSrcAnimSkel);
	if (!pSrcAnimSkel)
		return false;		// We couldn't construct the anim hierarchy for some reason. Abort.

	// Retarget the out-of-date animation onto the current skeleton
	ndanim::AnimatedJointPose upToDatePose;
	if (pSrcAnimSkel)
	{
		// Create a custom retarget entry
		Memory::PushAllocator(kAllocDebug, FILE_LINE_FUNC);
		SkelTable::RetargetEntry* pAnimRetargetEntry = nullptr;

		const DebugSkelInfo* pInfo = pSourceAnim->m_pDebugSkelInfo;
		if (pSourceAnim->m_flags & ArtItemAnim::kStreamingChunk)
		{
			AnimStream* pAnimStream = EngineComponents::GetAnimStreamManager()->GetAnimStreamFromChunk(pSourceAnim);
			if (!pAnimStream)
				return false;
			const ArtItemAnim* pBaseAnim = pAnimStream->GetArtItemAnimForChunk(pSourceAnim);
			pInfo = pBaseAnim->m_pDebugSkelInfo;
		}

		ConstructRetargetEntry(pSrcAnimSkel, pInfo->m_pJointNameIds, pSrcSkel, &pAnimRetargetEntry);
		Memory::PopAllocator();

		if (pAnimRetargetEntry && !pAnimRetargetEntry->m_disabled)
		{
			ANIM_ASSERT(pAnimRetargetEntry->m_jointRetarget);

			// We are retargeting from the anim skel to the 'src' skel as this is the out-of-date portion
			const ndanim::JointHierarchy* pTgtHier = pSrcSkel->m_pAnimHierarchy;
			const U32F tgtNumChannelGroups = ndanim::GetNumChannelGroups(pTgtHier);
			const U32F tgtValidBitsSize = tgtNumChannelGroups * sizeof(ndanim::ValidBits);
			const U32F tgtFloatChannelSize = pTgtHier->m_numFloatChannels * sizeof(float);
			const U32F tgtJointCount = pTgtHier->m_numTotalJoints;
			const U32F tgtJointParamsSize = tgtJointCount * sizeof(ndanim::JointParams);

			// Retarget the out-of-date animation onto the current skeleton
			char* pOutputBuffer = NDI_NEW(kAlign16) char[tgtValidBitsSize + tgtJointParamsSize + tgtFloatChannelSize];
			if (FALSE_IN_FINAL_BUILD(g_animOptions.m_enableValidation))
			{
				memset(pOutputBuffer, 0x33, tgtValidBitsSize + tgtJointParamsSize + tgtFloatChannelSize);
			}

			upToDatePose.m_pValidBitsTable = reinterpret_cast<ndanim::ValidBits*>(pOutputBuffer);
			upToDatePose.m_pJointParams = reinterpret_cast<ndanim::JointParams*>(pOutputBuffer + tgtValidBitsSize);
			ANIM_ASSERT(upToDatePose.m_pJointParams);
			upToDatePose.m_pFloatChannels = reinterpret_cast<float*>(pOutputBuffer + tgtValidBitsSize + tgtJointParamsSize);

			if (pRetargetEntry->m_dstToSrcSegMapping)
			{
				// We need to figure out which anim-skel source segments to animate to get the up-to-date source segments
				// that are needed to provide the single target skel output segment joints.
				const U32F numSrcSkelSegments = pSrcSkel->m_numSegments;
				for (I32F iSrcSkelSeg = 0; iSrcSkelSeg < numSrcSkelSegments; ++iSrcSkelSeg)
				{
					if (pRetargetEntry->m_dstToSrcSegMapping[pSegmentContext->m_iSegment].IsBitSet(iSrcSkelSeg))
					{
						RetargetInput ri;
						ri.m_pArtItemAnim = pSourceAnim;
						ri.m_sample = pCmdEvalClip->m_frame;
						RetargetPoseForSegment(ri,
							pSrcAnimSkel,
							pSrcSkel,
							pAnimRetargetEntry,
							iSrcSkelSeg,
							&upToDatePose);
					}
				}
			}

			ValidatePose(pContext->m_pAnimDataHack ? pContext->m_pAnimDataHack->m_hProcess.ToProcess() : nullptr,
						 pAnimCmdList,
						 pCmdEvalClip,
						 pGroupContext,
						 pDstSkel,
						 pHierarchy,
						 &upToDatePose);
		}
	}
	ANIM_ASSERT(upToDatePose.m_pJointParams);
	ANIM_ASSERT(pRetargetEntry->m_jointRetarget);

	// Retarget the out-of-date animation onto the current skeleton
	ndanim::AnimatedJointPose outJointPose;

	const ndanim::JointHierarchy* pTgtHier = pContext->m_pSkel->m_pAnimHierarchy;
	const U32F tgtNumChannelGroups = ndanim::GetNumChannelGroups(pTgtHier);
	const U32F tgtValidBitsSize = tgtNumChannelGroups * sizeof(ndanim::ValidBits);
	const U32F tgtFloatChannelSize = pTgtHier->m_numFloatChannels * sizeof(float);
	const U32F tgtJointCount = pTgtHier->m_numTotalJoints;
	const U32F tgtJointParamsSize = tgtJointCount * sizeof(ndanim::JointParams);

	// Retarget the out-of-date animation onto the current skeleton
	char* pOutputBuffer = NDI_NEW(kAlign16) char[tgtValidBitsSize + tgtJointParamsSize + tgtFloatChannelSize];
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_enableValidation))
	{
		memset(pOutputBuffer, 0x44, tgtValidBitsSize + tgtJointParamsSize + tgtFloatChannelSize);
	}

	outJointPose.m_pValidBitsTable = reinterpret_cast<ndanim::ValidBits*>(pOutputBuffer);
	outJointPose.m_pJointParams = reinterpret_cast<ndanim::JointParams*>(pOutputBuffer + tgtValidBitsSize);
	outJointPose.m_pFloatChannels = reinterpret_cast<float*>(pOutputBuffer + tgtValidBitsSize + tgtJointParamsSize);

	// Push a scoped temp allocator for the output buffers since we don't need them to persist outside of this function
	ScopedTempAllocator jj2(FILE_LINE_FUNC);
	RetargetInput ri;
	ri.m_pInPose = &upToDatePose;
	ri.m_inPoseIsAdditive = pSourceAnim->IsAdditive();
	RetargetPoseForSegment(ri,
							pSrcSkel,
							pContext->m_pSkel,
							pRetargetEntry,
							pSegmentContext->m_iSegment,
							&outJointPose);

	ValidatePose(pContext->m_pAnimDataHack ? pContext->m_pAnimDataHack->m_hProcess.ToProcess() : nullptr,
				 pAnimCmdList,
				 pCmdEvalClip,
				 pGroupContext,
				 pDstSkel,
				 pHierarchy,
				 &outJointPose);

	// Now evaluate the newly created pose into the desired instance
	OrbisAnim::EvaluatePose(pGroupContext,
							pCmdEvalClip->m_outputInstance,
							(OrbisAnim::JointParams*)outJointPose.m_pJointParams,
							outJointPose.m_pFloatChannels,
							(OrbisAnim::ValidBits*)outJointPose.m_pValidBitsTable);

	ValidateInstance(pContext->m_pAnimDataHack ? pContext->m_pAnimDataHack->m_hProcess.ToProcess() : nullptr,
					 pAnimCmdList,
					 pCmdEvalClip,
					 pGroupContext,
					 pCmdEvalClip->m_outputInstance);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct DoDoubleRetargetingData
{
	const AnimExecutionContext* m_pContext;
	const OrbisAnim::SegmentContext* m_pSegmentContext;
	OrbisAnim::ProcessingGroupContext* m_pGroupContext;
	const ArtItemSkeleton* m_pSrcSkel;
	const ArtItemSkeleton* m_pDstSkel;
	const AnimCmd_EvaluateClip* m_pCmdEvalClip;
	const SkelTable::RetargetEntry* m_pRetargetEntry;
	const OrbisAnim::AnimHierarchy* m_pHierarchy;
	const AnimCmdList* m_pAnimCmdList;
	bool m_instanceEvaluated;
};

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(DoDoubleRetargetingEntry)
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);

	DoDoubleRetargetingData* pData = (DoDoubleRetargetingData*)jobParam;

	pData->m_instanceEvaluated = DoDoubleRetargeting(pData->m_pContext,
													 pData->m_pSegmentContext,
													 pData->m_pGroupContext,
													 pData->m_pSrcSkel,
													 pData->m_pCmdEvalClip,
													 pData->m_pRetargetEntry,
													 pData->m_pDstSkel,
													 pData->m_pHierarchy,
													 pData->m_pAnimCmdList);
}


/// --------------------------------------------------------------------------------------------------------------- ///
static void ProcessAnimSegment(U32 segmentIndex,
							   U32 outputMask,
							   const AnimExecutionContext* pCtx,
							   const AnimExecutionContext* pPrevCtx,
							   AnimPhasePluginCommandHandler* pAnimPhasePluginCommandFunc,
							   RigPhasePluginCommandHandler* pRigPhasePluginCommandFunc,
							   bool useImpliedPoseAsInput)
{
#ifndef FINAL_BUILD
	const U64 startTick =  TimerGetRawCount();
	const bool isRetargeting =
		!(outputMask & AnimExecutionContext::kOutputJointParamsLs) &&
		!(outputMask & AnimExecutionContext::kOutputTransformsOs) &&
		!(outputMask & AnimExecutionContext::kOutputSkinningMats);
#endif

	pCtx->Validate();

	ANIM_ASSERT(pCtx != pPrevCtx);
	ANIM_ASSERT(!pPrevCtx || pCtx->m_pAnimDataHack);

	// Try to catch cases where the anim data was freed and then allocated by another object
	ANIM_ASSERT(!pPrevCtx || pCtx->m_pAnimDataHack == pPrevCtx->m_pAnimDataHack);
	ANIM_ASSERT(!pPrevCtx || pCtx->m_pSkel == pPrevCtx->m_pSkel);

	const U32 kMaxChannelGroups = 300;
	ALIGNED(16) OrbisAnim::ValidBits validBits[kMaxChannelGroups];

	PROFILE(Animation, Process_Anim_Cmd_List_Segment);

	const AnimCmdList* pAnimCmdList = &pCtx->m_animCmdList;

	// If we need to animate non-zero segment and we have no skinning mtx available and no command list (we're in pause or anim is disabled),
	// we just fill in bind pose
	U32 bindPoseCmdMemory[64];
	AnimCmdList bindPoseCmdList;
	if (segmentIndex > 0 && pAnimCmdList->GetNumWordsUsed() == 0 && pPrevCtx && pCtx->m_dependencyTableValid)
	{
		if (((outputMask & AnimExecutionContext::kOutputSkinningMats) && !(pCtx->m_processedSegmentMask_SkinningMats & (1 << segmentIndex)) && (!pPrevCtx || !(pPrevCtx->m_processedSegmentMask_SkinningMats & (1 << segmentIndex)))) ||
			((outputMask & AnimExecutionContext::kOutputOutputControls) && !(pCtx->m_processedSegmentMask_OutputControls & (1 << segmentIndex)) && (!pPrevCtx || !(pPrevCtx->m_processedSegmentMask_OutputControls & (1 << segmentIndex)))))
		{
			bindPoseCmdList.Init(&bindPoseCmdMemory, sizeof(bindPoseCmdMemory));

			bindPoseCmdList.AddCmd(AnimCmd::kBeginSegment);
			bindPoseCmdList.AddCmd(AnimCmd::kBeginAnimationPhase);
			bindPoseCmdList.AddCmd_BeginProcessingGroup();
			bindPoseCmdList.AddCmd_EvaluateBindPose(0);
			bindPoseCmdList.AddCmd(AnimCmd::kEndProcessingGroup);
			bindPoseCmdList.AddCmd(AnimCmd::kEndAnimationPhase);

			bindPoseCmdList.AddCmd_EvaluateJointHierarchyCmds_Prepare(pCtx->m_pAllInputControls);
			bindPoseCmdList.AddCmd_EvaluateJointHierarchyCmds_Evaluate();

			bindPoseCmdList.AddCmd(AnimCmd::kEndSegment);

			pAnimCmdList = &bindPoseCmdList;
		}
	}

	U32 impliedPoseCmdListBuffer[64];
	AnimCmdList impliedPoseCmdList;
	impliedPoseCmdList.Init(impliedPoseCmdListBuffer, sizeof(impliedPoseCmdListBuffer));
	if (useImpliedPoseAsInput)
	{
		// Should really only run on Segment 0 - Propagate any gameplay joint updates
		if (segmentIndex == 0 && pCtx->m_pSegmentData[segmentIndex].m_pJointParams)
		{
			impliedPoseCmdList.AddCmd(AnimCmd::kBeginSegment);

			// Anim phase
			impliedPoseCmdList.AddCmd(AnimCmd::kBeginAnimationPhase);
			impliedPoseCmdList.AddCmd_BeginProcessingGroup();
			impliedPoseCmdList.AddCmd_EvaluateEmptyPose(0);
			impliedPoseCmdList.AddCmd(AnimCmd::kEndProcessingGroup);
			impliedPoseCmdList.AddCmd(AnimCmd::kEndAnimationPhase);
			impliedPoseCmdList.AddCmd_EvaluateImpliedPose();

			// Rig phase
			impliedPoseCmdList.AddCmd_EvaluateJointHierarchyCmds_Prepare(pCtx->m_pAllInputControls);
			impliedPoseCmdList.AddCmd_EvaluateJointHierarchyCmds_Evaluate();

			impliedPoseCmdList.AddCmd(AnimCmd::kEndSegment);

			// Replace the command list with this explicit one
			pAnimCmdList = &impliedPoseCmdList;
		}
		else
		{
			// Fall-through and re-evaluate the command list as we don't have any joint params from earlier to use
		}
	}

	if (!pAnimCmdList->GetNumWordsUsed())
	{
		EmitRequestedFloatChannelOutputs(pCtx, pPrevCtx, nullptr, segmentIndex, outputMask);
		EmitRequestedOutputs(pCtx, pPrevCtx, nullptr, segmentIndex, outputMask);

#ifndef FINAL_BUILD
		if (g_animOptions.m_printSegmentProcessing)
		{
			const U64 endTick = TimerGetRawCount();

			MsgAnim("Frame %ull: Processing segment %d for '%s' [%s] in %.3f ms - %s%s%s%s in the %s phase\n",
				GetCurrentFrameNumber(),
				segmentIndex,
				pCtx->m_pAnimDataHack ? pCtx->m_pAnimDataHack->m_hProcess.ToProcess()->GetName() : "unknown",
				pCtx->m_pSkel->GetName(),
				ConvertTicksToMilliseconds(endTick - startTick),
				(outputMask & AnimExecutionContext::kOutputJointParamsLs) ? "JointParams " : "",
				(outputMask & AnimExecutionContext::kOutputTransformsOs) ? "Transforms " : "",
				(outputMask & AnimExecutionContext::kOutputSkinningMats) ? "SkinMats " : "",
				isRetargeting ? "Retarget" : "",
				ndjob::IsRenderFrameJob() ? "Render" : "Game");
		}
#endif

		return;
	}

//	ANIM_ASSERT(pAnimCmdList->GetNumWordsUsed());

	const Process* pOwner = FALSE_IN_FINAL_BUILD(pCtx->m_pAnimDataHack) ? (pCtx->m_pAnimDataHack->m_hProcess.ToProcess()) : nullptr;

	// A few helper variables used during the animation blending.
	ArtItemSkeleton const* pSkel = pCtx->m_pSkel;
	OrbisAnim::AnimHierarchy const* pHierarchy = pSkel->m_pAnimHierarchy;

	const U32 numProcessingGroupsInSegment = OrbisAnim::AnimHierarchy_GetNumProcessingGroupsInSegment(pHierarchy, segmentIndex);
	U32 currentProcessingGroupIndex = 0;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	// Setup the work buffer
	const int kWorkBufferSize = 256 * 1024;
	void* pWorkBufferData = NDI_NEW (kAlign128) char[kWorkBufferSize];
//	memset(pWorkBufferData, 0xCC, kWorkBufferSize);

	const int kWorkBufferTrackerSize = 8 * 1024;
	void* pWorkBufferTrackerAlloc = NDI_NEW (kAlign128) char[kWorkBufferTrackerSize];
	OrbisAnim::WorkBuffer* pWorkBuffer = NDI_NEW (kAlign128) OrbisAnim::WorkBuffer(pWorkBufferData, kWorkBufferSize, pWorkBufferTrackerAlloc, kWorkBufferTrackerSize);

	const int kProcessingGroupContextSize = 4 * 1024;
	void* pProcessingGroupContextAlloc = NDI_NEW (kAlign128) char[kProcessingGroupContextSize];

	OrbisAnim::SegmentContext segmentContext = {0};
	OrbisAnim::ProcessingGroupContext* pGroupContext = nullptr;

	U32F maxProcessingGroupInstances = 0;
	U32F maxChannelGroupInstances = 0;

	const bool isDeferred = ndjob::IsRenderFrameJob();

	const U32* pAnimCmdStream = pAnimCmdList->GetBuffer();

	// Now parse all commands
	U32F currentWordInStream = 0;
	U32 beginProcGroupCmdIndex = 0;

	while (currentWordInStream < pAnimCmdList->GetNumWordsUsed())
	{
		const U32* currentCmdAddr = pAnimCmdStream + currentWordInStream;
		Memory::PrefetchForLoad(currentCmdAddr, 0x80);

		const AnimCmd* pCmd = reinterpret_cast<const AnimCmd*>(currentCmdAddr);

		const U32 cmdType = pCmd->m_type;
		const U32 numCmdWords = pCmd->m_numCmdWords;
		ANIM_ASSERT(numCmdWords < 50);			// Ensure we have a reasonable value here.
		currentWordInStream += numCmdWords;

		switch (cmdType) 
		{
		case AnimCmd::kBeginSegment:
			{
//				PROFILE(Animation, Process_Anim_Cmd_List_BeginObject);

				OrbisAnim::Status eStatus2;
				eStatus2 = OrbisAnim::BeginSegment(&segmentContext, pWorkBuffer, pHierarchy, pCtx->m_pDependencyTable, segmentIndex);
				ANIM_ASSERT(eStatus2 == OrbisAnim::kSuccess);

				// Setup custom stuff in the segment context
				segmentContext.m_pCustomContext = (uintptr_t)pCtx;
				segmentContext.m_pCustomCommands = OrbisAnim::CommandBlock::g_pfnCustomCommand;
				segmentContext.m_numCustomCommands = OrbisAnim::CommandBlock::g_numCustomCommands;

				segmentContext.m_pPersistentData = pCtx->m_pPersistentData;

				currentProcessingGroupIndex = 0;
			}
			break;

		case AnimCmd::kEndSegment:
			{
				EmitRequestedOutputs(pCtx, pPrevCtx, &segmentContext, segmentIndex, outputMask);

				segmentContext.m_pCustomContext = 0;

				OrbisAnim::Status eStatus2;
				eStatus2 = OrbisAnim::EndSegment(&segmentContext);
				ANIM_ASSERT(eStatus2 == OrbisAnim::kSuccess);

				// Ensure that we processed the entire command list
				ANIM_ASSERT(pAnimCmdList->GetNumWordsUsed() == currentWordInStream);
			}
			break;

		case AnimCmd::kBeginAnimationPhase:
			{
			}
			break;

		case AnimCmd::kEndAnimationPhase:
			{
				OrbisAnim::Status eStatus2;
				// Merge work buffer allocations for all jointParams groups, floatChannel groups
				eStatus2 = OrbisAnim::MergeProcessingGroupData(&segmentContext);
				ANIM_ASSERT(eStatus2 == OrbisAnim::kSuccess);
			}
			break;

		case AnimCmd::kBeginProcessingGroup:
			{
//				PROFILE(Animation, Process_Anim_Cmd_List_BeginProcGroup);

				beginProcGroupCmdIndex = currentWordInStream - numCmdWords;

				const AnimCmd_BeginProcessingGroup* pCmdBeginProcGroup = static_cast<const AnimCmd_BeginProcessingGroup*>(pCmd);
				maxProcessingGroupInstances = pCmdBeginProcGroup->m_neededProcGroupInstances;
				maxChannelGroupInstances = (maxProcessingGroupInstances) * 2;	// channel group instances (joints + floats)
				ANIM_ASSERTF(currentProcessingGroupIndex < OrbisAnim::AnimHierarchy_GetNumProcessingGroups(pHierarchy), ("Processing group index is out of range"));

				U32 sizeofProcessingGroupContext = OrbisAnim::GetSizeofProcessingGroupContext(maxChannelGroupInstances);
				ANIM_ASSERT(sizeofProcessingGroupContext < kProcessingGroupContextSize);
				pGroupContext = OrbisAnim::UseProcessingGroupContextAlloc(&segmentContext, maxChannelGroupInstances, pProcessingGroupContextAlloc);

				OrbisAnim::Status eStatus2;
				eStatus2 = OrbisAnim::BeginProcessingGroup(pGroupContext, &segmentContext, currentProcessingGroupIndex);
				ANIM_ASSERT(eStatus2 == OrbisAnim::kSuccess);

				// Allocate work buffer space for as many instances of the current channel groups as we need:
				eStatus2 = OrbisAnim::AllocateProcessingGroupInstances(pGroupContext, maxProcessingGroupInstances);
				ANIM_ASSERTF(eStatus2 == OrbisAnim::kSuccess, ("Processing group instances could not be allocated"));

				{
					U32F iLocalChannelGroup = 0;
					if (pGroupContext->m_pProcessingGroup->m_numAnimatedJoints > 0)
					{
						++iLocalChannelGroup;
					}

					for (U32F instance = 0; instance < pGroupContext->m_numInstances; ++instance)
					{
						const U32F instanceIndex = instance * pGroupContext->m_pProcessingGroup->m_numChannelGroups;
						void** plocChannelGroup = pGroupContext->m_plocChannelGroupInstance + instanceIndex;

						if (pGroupContext->m_pProcessingGroup->m_numFloatChannels > 0)
						{
							const U32F alignedNumFloatChannels = AlignSize(pGroupContext->m_pProcessingGroup->m_numFloatChannels, kAlign4);

							F32* pFloatChannels = (F32*)plocChannelGroup[iLocalChannelGroup];

							memset(pFloatChannels, 0, sizeof(F32) * alignedNumFloatChannels);
						}
					}
				}

				// Special case: If we have no processing groups... skip all commands
				if (!segmentContext.m_pSegment->m_numAnimatedJoints && !segmentContext.m_pSegment->m_numFloatChannels)
				{
					const AnimCmd* pNextCmd = reinterpret_cast<const AnimCmd*>(pAnimCmdStream + currentWordInStream);
					while (pNextCmd->m_type != AnimCmd::kEndProcessingGroup)
					{
						currentWordInStream += pNextCmd->m_numCmdWords;
						pNextCmd = reinterpret_cast<const AnimCmd*>(pAnimCmdStream + currentWordInStream);
					}
				}
			}
			break;

		case AnimCmd::kEndProcessingGroup:
			{
//				PROFILE(Animation, Process_Anim_Cmd_List_EndProcGroup);

				OrbisAnim::Status eStatus2;

				// Special case: If we have no processing groups... skip all commands
				if (segmentContext.m_pSegment->m_numAnimatedJoints || segmentContext.m_pSegment->m_numFloatChannels)
				{
					// Fill in undefined channels from default pose
					eStatus2 = OrbisAnim::EvaluateFillInUndefinedFromDefaultPose(pGroupContext);
					ANIM_ASSERT(eStatus2 == OrbisAnim::kSuccess);
				}

				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, 0, true);

				// Free all state and all allocations for instances other than the first, which contains our output data:
				eStatus2 = OrbisAnim::FreeProcessingGroupInstancesExceptOutput(pGroupContext);
				ANIM_ASSERT(eStatus2 == OrbisAnim::kSuccess);
				pGroupContext = nullptr;

				currentProcessingGroupIndex++;
				if (currentProcessingGroupIndex < numProcessingGroupsInSegment)
				{
					// Reset the command stream to the 'Begin Processing Group' command
					currentWordInStream = beginProcGroupCmdIndex;
				}
			}
			break;

		case AnimCmd::kEvaluateClip:
			{
				PROFILE(Animation, Process_Anim_Cmd_List_EvalClip);

				const AnimCmd_EvaluateClip* pCmdEvalClip = static_cast<const AnimCmd_EvaluateClip*>(pCmd);

				ANIM_ASSERT(pCmdEvalClip->m_outputInstance < maxProcessingGroupInstances);
				ANIM_ASSERTF(pCmdEvalClip->m_pArtItemAnim, ("Evaluate Clip Has No ArtItemAnim"));

				OrbisAnim::AnimClip const* pAnimClip = (OrbisAnim::AnimClip const*)pCmdEvalClip->m_pArtItemAnim->m_pClipData;
				OrbisAnim::Status eStatus2;

				if (pAnimClip->m_animHierarchyId == pHierarchy->m_animHierarchyId)
				{
					bool perfromNormalEvaluateClip = EvaluateClip_DebugPreEvalCallback(pGroupContext, pCmdEvalClip);

					if (LIKELY(perfromNormalEvaluateClip))
					{
						OrbisAnim::EvaluateClip(pGroupContext,
												pCmdEvalClip->m_outputInstance,
												pAnimClip,
												pCmdEvalClip->m_frame,
												nullptr);
					}
//					LogAnimation(pCmdEvalClip->m_pArtItemAnim, pCmdEvalClip->m_frame, false);

					SanitateInstance(pGroupContext, pCmdEvalClip->m_outputInstance);
					EvaluateClip_DebugPostEvalCallback(pGroupContext, pCmdEvalClip);

					ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalClip->m_outputInstance);
				}
				else
				{
					// Ok, the animation isn't compatible. Was it meant for this skeleton?
					const SkeletonId animSkelID = pCmdEvalClip->m_pArtItemAnim->m_skelID;
					const SkeletonId skelSkelID = pCtx->m_pSkel->m_skelId;

					bool instanceEvaluated = false;

					// Attempt a 'formal' retargeting of the animation using skel-retargets.dc
					if (skelSkelID != animSkelID)
					{
						// Nope, it was destined for another skeleton. Retarget it.
						const SkelTable::RetargetEntry* pRetargetEntry = SkelTable::LookupRetarget(animSkelID, skelSkelID);
						if (g_animOptions.m_enableAnimRetargeting && pRetargetEntry && !pRetargetEntry->m_disabled)
						{
							PROFILE(Animation, EvalClip_Retarget);

							const ArtItemSkeleton* pSrcSkel = ResourceTable::LookupSkel(pCmdEvalClip->m_pArtItemAnim->m_skelID).ToArtItem();
							ANIM_ASSERT(pSrcSkel);

							// Don't allow the source animation to be out-of-date compared to the source skeleton if we are retargeting to another skeleton.
							const bool srcAnimIsOutOfDate = pSrcSkel->m_hierarchyId != pCmdEvalClip->m_pArtItemAnim->m_pClipData->m_animHierarchyId;

							if (srcAnimIsOutOfDate)
							{
								if (g_animOptions.m_enableDoubleAnimRetargeting)
								{
									DoDoubleRetargetingData data;
									data.m_pContext = pCtx;
									data.m_pSegmentContext = &segmentContext;
									data.m_pGroupContext = pGroupContext;
									data.m_pSrcSkel = pSrcSkel;
									data.m_pDstSkel = pSkel;
									data.m_pCmdEvalClip = pCmdEvalClip;
									data.m_pRetargetEntry = pRetargetEntry;
									data.m_pHierarchy = pHierarchy;
									data.m_pAnimCmdList = pAnimCmdList;
									data.m_instanceEvaluated = false;

									ndjob::JobDecl activeJobDecl;
									ndjob::GetActiveJobDecl(&activeJobDecl);
									ndjob::SetActiveJobFlags(activeJobDecl.m_flags & ~ndjob::kDisallowSleep);	// Allow sleeping in this special occasion

									ndjob::CounterHandle hCounter = ndjob::AllocateCounter(FILE_LINE_FUNC, 1);
									ndjob::JobDecl jobDecl(DoDoubleRetargetingEntry, (uintptr_t)&data);
									jobDecl.m_associatedCounter = hCounter;
									ndjob::JobArrayHandle hJobArray = ndjob::BeginJobArray(1, ndjob::Priority::kAboveNormal);
									ndjob::AddJobs(hJobArray, &jobDecl, 1);
									ndjob::CommitJobArray(hJobArray);
									ndjob::WaitForCounterAndFree(hCounter);

									ndjob::SetActiveJobFlags(activeJobDecl.m_flags);		// Restore the flags

									instanceEvaluated = data.m_instanceEvaluated;
								}
								else
								{
									OrbisAnim::EvaluateEmptyPose(pGroupContext, pCmdEvalClip->m_outputInstance);
									instanceEvaluated = true;
								}
							}
							else if (pRetargetEntry->m_dstToSrcSegMapping && !pRetargetEntry->m_dstToSrcSegMapping[segmentContext.m_iSegment].AreAllBitsClear())
							{
								instanceEvaluated = DoSkelToSkelRetargeting(pCtx,
																			segmentContext,
																			pGroupContext,
																			pSrcSkel,
																			pCmdEvalClip,
																			pRetargetEntry,
																			pSkel,
																			pHierarchy,
																			pAnimCmdList);
							}
							else
							{
								OrbisAnim::EvaluateEmptyPose(pGroupContext, pCmdEvalClip->m_outputInstance);
								instanceEvaluated = true;
							}

							ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalClip->m_outputInstance);
						}
					}

					// Ok, 'proper' retargeting didn't work. Let's try the slower name-matching retargeting
					if (g_animOptions.m_allowOutOfDateAnimationRetargeting &&
						!EngineComponents::GetNdGameInfo()->m_onDisc &&					// On disc we don't have debug pages and can therefore not use out-of-date stuff
						Memory::IsDebugMemoryAvailable() &&
						!instanceEvaluated && 
						pCmdEvalClip->m_pArtItemAnim->m_versionNumber >= 7 &&
						skelSkelID == animSkelID &&
						pCmdEvalClip->m_pArtItemAnim->m_pDebugSkelInfo &&
						g_allowLoadingOfDebugPages)
					{
						instanceEvaluated = DoOutOfDateAnimRetargeting(pCmdEvalClip,
																	   pCtx,
																	   segmentContext,
																	   pGroupContext,
																	   pAnimCmdList);
					}

					// Ok, nothing we tried worked. Let's just evaluate a bind pose and be done with it.
					if (!instanceEvaluated)
					{
						if (!(pCmdEvalClip->m_pArtItemAnim->m_flags & ArtItemAnim::kAdditive))
						{
							OrbisAnim::JointParams const* pJoints = (OrbisAnim::JointParams const*)OrbisAnim::AnimHierarchy_GetDefaultJointPoseTable(pHierarchy);
							F32 const* pFloatChannels = OrbisAnim::AnimHierarchy_GetDefaultFloatChannelPoseTable(pHierarchy);
							const U32F numChannelGroups = OrbisAnim::AnimHierarchy_GetNumChannelGroups(pHierarchy);                
							ANIM_ASSERT(kMaxChannelGroups >= numChannelGroups);                             

							for (U32F iChannelGroup = 0; iChannelGroup < numChannelGroups; ++iChannelGroup)
								validBits[iChannelGroup].SetAllBits();                                                                             

							ANIM_ASSERT(pCmdEvalClip->m_outputInstance < maxProcessingGroupInstances);

							OrbisAnim::EvaluatePose(pGroupContext,
													pCmdEvalClip->m_outputInstance,
													pJoints,
													pFloatChannels,
													validBits);
						}
						else
						{
							OrbisAnim::EvaluateEmptyPose(pGroupContext, pCmdEvalClip->m_outputInstance);
							
						}
						ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalClip->m_outputInstance);
					}
				}
			}
			break;

		case AnimCmd::kEvaluateBlend:
		case AnimCmd::kEvaluateFeatherBlend:
			{
				PROFILE(Animation, Process_Anim_Cmd_List_EvalBlend);

				const AnimCmd_EvaluateBlend* pCmdEvalBlend = static_cast<const AnimCmd_EvaluateBlend*>(pCmd);

				const U32F leftInstance = pCmdEvalBlend->m_leftInstance;
				const U32F rightInstance = pCmdEvalBlend->m_rightInstance;
				const U32F outputInstance = pCmdEvalBlend->m_outputInstance;

				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, leftInstance);
				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, rightInstance);

				ANIM_ASSERT(leftInstance < maxProcessingGroupInstances);
				ANIM_ASSERT(rightInstance < maxProcessingGroupInstances);
				ANIM_ASSERTF(outputInstance == leftInstance || outputInstance == rightInstance,
							 ("OrbisAnim::EvaluateBlend only supports outputting to left or right instance"));

				const bool leftEmpty = IsInstanceEmpty(pGroupContext, leftInstance);
				const bool rightEmpty = IsInstanceEmpty(pGroupContext, rightInstance);
				if (leftEmpty && rightEmpty)
				{
					OrbisAnim::EvaluateEmptyPose(pGroupContext, outputInstance);
				}				
				else
				{
					const bool outputToRight = (outputInstance == rightInstance);

					if (cmdType == AnimCmd::kEvaluateFeatherBlend)
					{
						const AnimCmd_EvaluateFeatherBlend* pCmdEvalFb = static_cast<const AnimCmd_EvaluateFeatherBlend*>(pCmd);

						const uint32_t* pNumChannelFactors = pCmdEvalFb->m_pNumChannelFactors;
						const OrbisAnim::ChannelFactor* const* ppChannelFactors = pCmdEvalFb->m_ppChannelFactors;

						ANIM_ASSERT(pNumChannelFactors);
						ANIM_ASSERT(ppChannelFactors);

						//Memory::Allocator* pGame2GpuAlloc = Memory::GetAllocator(kAllocGameToGpuRing);
						//ANIM_ASSERT(pGame2GpuAlloc->IsValidPtr(pNumChannelFactors));
						//ANIM_ASSERT(pGame2GpuAlloc->IsValidPtr(ppChannelFactors));
						//ANIM_ASSERT(pGame2GpuAlloc->IsValidPtr(*ppChannelFactors));

						OrbisAnim::EvaluateBlend(pGroupContext,
												 leftInstance,
												 rightInstance,
												 outputToRight,
												 (OrbisAnim::BlendMode)pCmdEvalBlend->m_blendMode,
												 pCmdEvalBlend->m_blendFactor,
												 pNumChannelFactors,
												 ppChannelFactors);
					}
					else
					{
						OrbisAnim::EvaluateBlend(pGroupContext,
												 leftInstance,
												 rightInstance,
												 outputToRight,
												 (OrbisAnim::BlendMode)pCmdEvalBlend->m_blendMode,
												 pCmdEvalBlend->m_blendFactor);
					}

					SanitateInstance(pGroupContext, outputInstance);
				}
				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, outputInstance);
			}
			break;

		case AnimCmd::kEvaluateFlip:
			{
				PROFILE(Animation, Process_Anim_Cmd_List_EvalFlip);

				const AnimCmd_EvaluateFlip* pCmdEvalFlip = static_cast<const AnimCmd_EvaluateFlip*>(pCmd);

				U16* pNumFlips = nullptr;
				U8** ppJointOffsets = nullptr;
				U32** ppFlipOps = nullptr;
				if (pCtx->m_pSkel->m_pJointFlipData->m_pV4)
				{
					pNumFlips = pCtx->m_pSkel->m_pJointFlipData->m_pV4->m_pNumFlips;
					ppJointOffsets = pCtx->m_pSkel->m_pJointFlipData->m_pV4->m_ppJointOffsets;
					ppFlipOps = pCtx->m_pSkel->m_pJointFlipData->m_pV4->m_ppFlipOps;
				}

				ANIM_ASSERT(pCmdEvalFlip->m_outputInstance < maxProcessingGroupInstances);
				OrbisAnim::EvaluateFlip(pGroupContext,
										pCmdEvalFlip->m_outputInstance,
										pNumFlips,
										ppJointOffsets,
										ppFlipOps);

				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalFlip->m_outputInstance);
			}
			break;

		case AnimCmd::kEvaluateEmptyPose:
			{
				PROFILE(Animation, Process_Anim_Cmd_List_EvalEmptyPose);
				const AnimCmd_EvaluateEmptyPose* pCmdEvalEmptyPose = static_cast<const AnimCmd_EvaluateEmptyPose*>(pCmd);
				OrbisAnim::EvaluateEmptyPose(pGroupContext, pCmdEvalEmptyPose->m_outputInstance);

				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalEmptyPose->m_outputInstance);
			}
			break;

		case AnimCmd::kEvaluatePose:
			{
				PROFILE(Animation, Process_Anim_Cmd_List_EvalPose);
				const AnimCmd_EvaluatePose* pCmdEvalPose = static_cast<const AnimCmd_EvaluatePose*>(pCmd);

				if (pCmdEvalPose->m_hierarchyId != pHierarchy->m_animHierarchyId)
				{
					ANIM_ASSERT(pCmdEvalPose->m_outputInstance < maxProcessingGroupInstances);
					OrbisAnim::EvaluateEmptyPose(pGroupContext, pCmdEvalPose->m_outputInstance);
					break;
				}
				ANIM_ASSERT(pCmdEvalPose->m_hierarchyId == pHierarchy->m_animHierarchyId);

				// We are not allowed to evaluate poses in a deferred manner. It could read data that has relocated since we created the command list
 				if (isDeferred)
 				{
 					ANIM_ASSERT(pCmdEvalPose->m_outputInstance < maxProcessingGroupInstances);
 					OrbisAnim::EvaluateEmptyPose(pGroupContext, pCmdEvalPose->m_outputInstance);
 				}
 				else
				{
					ValidatePose(pOwner,
								 pAnimCmdList,
								 pCmd,
								 pGroupContext,
								 pSkel,
								 pHierarchy,
								 &pCmdEvalPose->m_jointPose);

					ANIM_ASSERT(pCmdEvalPose->m_outputInstance < maxProcessingGroupInstances);

					const ndanim::ValidBits* pValidBits = pCmdEvalPose->m_jointPose.m_pValidBitsTable;
					ndanim::ValidBits* pNonConstValidBits = STACK_ALLOC_ALIGNED(ndanim::ValidBits, pHierarchy->m_numChannelGroups, kAlign16);
					if (!pCmdEvalPose->m_jointPose.m_pValidBitsTable)
					{
						pValidBits = pNonConstValidBits;

						// Construct a fully defined pose for this segment (move this to the SkeletonArtItem)
						const ndanim::ProcessingGroup* pProcGroupTable = ndanim::GetProcessingGroupTable((const ndanim::JointHierarchy*)pHierarchy);
						const U16 firstProcGroupInSeg = segmentContext.m_pSegment->m_firstProcessingGroup;
						const U16 numProcGroupsInSeg = segmentContext.m_pSegment->m_numProcessingGroups;
						U16 channelGroupOffset = 0;
						for (U16 iProcGrp = 0; iProcGrp < numProcGroupsInSeg; ++iProcGrp)
						{
							const ndanim::ProcessingGroup* pProcGroup = &pProcGroupTable[firstProcGroupInSeg + iProcGrp];
							const U16 firstChannelGroup = segmentContext.m_pSegment->m_firstChannelGroup + pProcGroup->m_firstChannelGroup;
							for (U16 iChannelGroup = 0; iChannelGroup < pProcGroup->m_numChannelGroups; ++iChannelGroup)
							{
								ndanim::ValidBits* pLocalValidBits = &pNonConstValidBits[firstChannelGroup + iChannelGroup];
								if (iChannelGroup == 0)	// Joints
								{
									const U16 firstAnimatedJoint = pProcGroup->m_firstAnimatedJoint;
									const U16 numAnimatedJoints = pProcGroup->m_numAnimatedJoints;
									for (U16 iJoint = 0; iJoint < numAnimatedJoints; iJoint++)
										pLocalValidBits->SetBit(iJoint);
								}
								else // Float channels
								{
									const U16 firstFloatChannel = pProcGroup->m_firstFloatChannel;
									const U16 numFloatChannels = pProcGroup->m_numFloatChannels;
									for (U16 iFloatChan = 0; iFloatChan < numFloatChannels; iFloatChan++)
									{
										pLocalValidBits->IsBitSet(iFloatChan);
									}
								}
								channelGroupOffset++;
							}
						}
					}
					OrbisAnim::EvaluatePose(pGroupContext,
											pCmdEvalPose->m_outputInstance,
											(void*)pCmdEvalPose->m_jointPose.m_pJointParams,
											pCmdEvalPose->m_jointPose.m_pFloatChannels,
											(OrbisAnim::ValidBits const*)pValidBits);
				}

				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalPose->m_outputInstance);
			}
			break;

		case AnimCmd::kEvaluatePoseDeferred:
			{
				PROFILE(Animation, Process_Anim_Cmd_List_EvalPoseDef);
				const AnimCmd_EvaluatePoseDeferred* pCmdEvalPoseDeferred = static_cast<const AnimCmd_EvaluatePoseDeferred*>(pCmd);

				if (pCmdEvalPoseDeferred->m_hierarchyId != pHierarchy->m_animHierarchyId)
				{
					ANIM_ASSERT(pCmdEvalPoseDeferred->m_outputInstance < maxProcessingGroupInstances);
					OrbisAnim::EvaluateEmptyPose(pGroupContext, pCmdEvalPoseDeferred->m_outputInstance);
					break;
				}
				ANIM_ASSERT(pCmdEvalPoseDeferred->m_hierarchyId == pHierarchy->m_animHierarchyId);
				ValidatePose(pOwner,
							 pAnimCmdList,
							 pCmd,
							 pGroupContext,
							 pSkel,
							 pHierarchy,
							 &pCmdEvalPoseDeferred->m_jointPose);

				ANIM_ASSERT(pCmdEvalPoseDeferred->m_outputInstance < maxProcessingGroupInstances);
				OrbisAnim::EvaluatePose(pGroupContext,
										pCmdEvalPoseDeferred->m_outputInstance,
										pCmdEvalPoseDeferred->m_jointPose.m_pJointParams,
										pCmdEvalPoseDeferred->m_jointPose.m_pFloatChannels,
										(OrbisAnim::ValidBits*)pCmdEvalPoseDeferred->m_jointPose.m_pValidBitsTable);

				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalPoseDeferred->m_outputInstance);
			}
			break;

		case AnimCmd::kEvaluateSnapshot:
			{
				PROFILE(Animation, Process_Anim_Cmd_List_EvalSnapshot);
				const AnimCmd_EvaluateSnapshot* pCmdEvalSnapshot = static_cast<const AnimCmd_EvaluateSnapshot*>(pCmd);
				if (pCmdEvalSnapshot->m_hierarchyId != pHierarchy->m_animHierarchyId)
				{
					break;
				}
				ANIM_ASSERT(pCmdEvalSnapshot->m_hierarchyId == pHierarchy->m_animHierarchyId);
				// We are not allowed to evaluate poses in a deferred manner. It could read data that has relocated since we created the command list
				
				if (!isDeferred)
				{
					ANIM_ASSERT(pCmdEvalSnapshot->m_inputInstance < maxProcessingGroupInstances);
					ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalSnapshot->m_inputInstance);
					ValidateSegmentInPose(pOwner,
										  pAnimCmdList,
										  pCmd,
										  pSkel,
										  pHierarchy,
										  &segmentContext,
										  &pCmdEvalSnapshot->m_jointPose);

					OrbisAnim::EvaluateSnapshot(pGroupContext,
												pCmdEvalSnapshot->m_inputInstance,
												pCmdEvalSnapshot->m_jointPose.m_pJointParams,
												pCmdEvalSnapshot->m_jointPose.m_pFloatChannels,
												(OrbisAnim::ValidBits*)pCmdEvalSnapshot->m_jointPose.m_pValidBitsTable);

					ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalSnapshot->m_inputInstance);
					ValidatePose(pOwner,
								 pAnimCmdList,
								 pCmd,
								 pGroupContext,
								 pSkel,
								 pHierarchy,
								 &pCmdEvalSnapshot->m_jointPose);
				}
			}
			break;

		case AnimCmd::kEvaluateBindPose:
			{
				PROFILE(Animation, Process_Anim_Cmd_List_EvalBindPose);
				const AnimCmd_EvaluateBindPose* pCmdEvalBindPose = static_cast<const AnimCmd_EvaluateBindPose*>(pCmd);

				//pGroupContext->m_pJointHierarchy
				OrbisAnim::JointParams const* pJoints = (OrbisAnim::JointParams const*)OrbisAnim::AnimHierarchy_GetDefaultJointPoseTable(pHierarchy);
				F32 const* pFloatChannels = OrbisAnim::AnimHierarchy_GetDefaultFloatChannelPoseTable(pHierarchy);
				const U32F numChannelGroups = OrbisAnim::AnimHierarchy_GetNumChannelGroups(pHierarchy);			
				ANIM_ASSERT(kMaxChannelGroups >= numChannelGroups);					

				for (U32F iChannelGroup = 0; iChannelGroup < numChannelGroups; ++iChannelGroup)
					validBits[iChannelGroup].SetAllBits();										

				ANIM_ASSERT(pCmdEvalBindPose->m_outputInstance < maxProcessingGroupInstances);

				OrbisAnim::EvaluatePose(pGroupContext,
										pCmdEvalBindPose->m_outputInstance,
										pJoints,
										pFloatChannels,
										validBits);

				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalBindPose->m_outputInstance);

				//OrbisAnim::EvaluateEmptyPose(pGroupContext, pCmdEvalBindPose->m_outputInstance);
			}
			break;

		case AnimCmd::kEvaluateCopy:
			{
				PROFILE(Animation, Process_Anim_Cmd_List_EvalCopy);
				const AnimCmd_EvaluateCopy* pCmdEvalCopy = static_cast<const AnimCmd_EvaluateCopy*>(pCmd);

				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalCopy->m_srcInstance);

				ANIM_ASSERT(pCmdEvalCopy->m_srcInstance < maxProcessingGroupInstances);
				ANIM_ASSERT(pCmdEvalCopy->m_destInstance < maxProcessingGroupInstances);
				OrbisAnim::EvaluateCopy(pGroupContext, pCmdEvalCopy->m_srcInstance, pCmdEvalCopy->m_destInstance);

				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalCopy->m_destInstance);
			}
			break;

		case AnimCmd::kEvaluateImpliedPose:
			{
				const AnimCmd_EvaluateImpliedPose* pCmdEvalImpliedPose = static_cast<const AnimCmd_EvaluateImpliedPose*>(pCmd);

				ndanim::AnimatedJointPose jointPose;
				jointPose.m_pJointParams = pCtx->m_pSegmentData[segmentIndex].m_pJointParams;
				jointPose.m_pFloatChannels = pCtx->m_pSegmentData[segmentIndex].m_pOutputControls;
				jointPose.m_pValidBitsTable = nullptr; // Assume all joints being valid

				ValidateSegmentInPose(pOwner, pAnimCmdList, pCmd, pSkel, pHierarchy, &segmentContext, &jointPose);

				if (segmentContext.m_pSegment->m_numAnimatedJoints)
				{
					U32 firstAnimatedJointIndexInSeg = OrbisAnim::AnimHierarchy_GetFirstAnimatedJointInSegment(pHierarchy, segmentContext.m_iSegment);

					ANIM_ASSERT(jointPose.m_pJointParams);
					OrbisAnim::EvaluateFullPose(&segmentContext,
						(const OrbisAnim::JointParams*)jointPose.m_pJointParams - firstAnimatedJointIndexInSeg,		// WARNING!!!! Fake offset to align just this segment
						jointPose.m_pFloatChannels);

					ValidateSegmentInPose(pOwner, pAnimCmdList, pCmd, pSkel, pHierarchy, &segmentContext, &jointPose);
				}
			}
			break;

		case AnimCmd::kEvaluateFullPose:
			{
				const AnimCmd_EvaluateFullPose* pCmdEvalFullPose = static_cast<const AnimCmd_EvaluateFullPose*>(pCmd);

				ndanim::AnimatedJointPose jointPose;
				jointPose.m_pJointParams = (ndanim::JointParams*)pCmdEvalFullPose->m_pJointParams;
 				ANIM_ASSERT(jointPose.m_pJointParams);
				jointPose.m_pFloatChannels = (float*)pCmdEvalFullPose->m_pFloatChannels;
				jointPose.m_pValidBitsTable = nullptr; // Assume all joints being valid

				ValidateSegmentInPose(pOwner, pAnimCmdList, pCmd, pSkel, pHierarchy, &segmentContext, &jointPose);

				// #ifdef ANIM_DEBUG
				// 				OrbisAnim::ProcessingGroup const* pProcessingGroups = OrbisAnim::AnimHierarchy_GetProcessingGroupTable(pHierarchy);
				// 				for (U32F groupIndex = 0; groupIndex < pHierarchy->m_numProcessingGroups; ++groupIndex)
				// 				{
				// 					const U32F numJointsInGroup = pProcessingGroups[groupIndex].m_numAnimatedJoints;
				// 					const U32F firstJointIndexInGroup = pProcessingGroups[groupIndex].m_firstAnimatedJoint;
				// 					for (U32F jointIndex = 0; jointIndex < numJointsInGroup; ++jointIndex)
				// 					{
				// 						const ndanim::JointParams& params = pCmdEvalFullPose->m_pJointParams[firstJointIndexInGroup + jointIndex];
				// 						ANIM_ASSERT(IsFinite(params.m_trans));
				// 						ANIM_ASSERT(IsFinite(params.m_quat));
				// 						ANIM_ASSERT(IsFinite(params.m_scale));
				// 					}
				// 				}
				// #endif

				// We are not allowed to evaluate poses in a deferred manner. It could read data that has relocated since we created the command list
				if (isDeferred)
				{
					OrbisAnim::EvaluateDefaultPose(&segmentContext);
				}
				else
				{
 					// Due to 'EvaluateFullPose' requiring the input as an array of 'packed' animated joints, then we assert here to allow it to be used
 					// with 'unpacked' (i.e. all joints) but the two arrays are only similar for the first animated joints in segment 0.
 					ANIM_ASSERT(segmentContext.m_iSegment == 0);
					if (segmentContext.m_pSegment->m_numAnimatedJoints)
					{
						U32 firstAnimatedJointIndexInSeg = OrbisAnim::AnimHierarchy_GetFirstAnimatedJointInSegment(pHierarchy, segmentContext.m_iSegment);

						ANIM_ASSERT(jointPose.m_pJointParams);
						OrbisAnim::EvaluateFullPose(&segmentContext,
							(const OrbisAnim::JointParams*)pCmdEvalFullPose->m_pJointParams,
							pCmdEvalFullPose->m_pFloatChannels);

						ValidateSegmentInPose(pOwner, pAnimCmdList, pCmd, pSkel, pHierarchy, &segmentContext, &jointPose);
					}
				}
			}
			break;

		case AnimCmd::kEvaluateJointHierarchyCmds_Prepare:
			{
				PROFILE(Animation, JointHierarchyCmd_Prepare);
	
				// Apply joint limits right before running the hierarchy commands
				if (g_animOptions.m_enableJointLimits)
				{
					PROFILE(Animation, ApplyJointLimit);

					// Apply joint limits defined by the skeleton
					const U32 numJointLimits = pCtx->m_pSkel->m_numJointLimitDefs;
					if (segmentContext.m_iSegment == 0 && numJointLimits)
					{
						const JointLimitDef* pJointLimitDefs = pCtx->m_pSkel->m_pJointLimitDefs;
						const U8* pJointRotateOrder = pCtx->m_pSkel->m_pJointRotateOrder;
						for (U32 i = 0; i < numJointLimits; ++i)
						{
							const JointLimitDef& jl = pJointLimitDefs[i];
							ndanim::JointParams& jp = ((ndanim::JointParams*)segmentContext.m_locJointParams)[jl.m_jointIndex];

							// Apply the limits
							if (jl.m_type < 3)			// Translation
							{
								U32 componentIndex = jl.m_type;
								F32 value = jp.m_trans.Get(componentIndex);
								F32 clampedValue = Limit(value, jl.m_min, jl.m_max);
								jp.m_trans.Set(componentIndex, clampedValue);
							}
							else if (jl.m_type < 6)		// Rotation
							{
								U32 componentIndex = jl.m_type - 3;

								const ndanim::JointParams* pDefaultLocalSpaceParams = reinterpret_cast<const ndanim::JointParams*>(ndanim::GetDefaultJointPoseTable(pCtx->m_pSkel->m_pAnimHierarchy));
								const Quat bindposeQuat = pDefaultLocalSpaceParams[jl.m_jointIndex].m_quat;
								const Quat quatInJointOrientSpace = Conjugate(bindposeQuat) * jp.m_quat;

								Vec4 quatAxis;
								float quatAngleRadians;
								quatInJointOrientSpace.GetAxisAndAngle(quatAxis, quatAngleRadians);

								if (quatAngleRadians > PI)
									quatAngleRadians = quatAngleRadians - 2.0f * PI;
								if (quatAngleRadians < -PI)
									quatAngleRadians = 2.0f * PI + quatAngleRadians;

								Quat limitedRot = jp.m_quat;
								if (componentIndex == 0)	// Limit rotation to only be around the local X-axis
								{
									// Check if we need to flip the axis/angle
									if (Dot(Vector(quatAxis), Vector(kUnitXAxis)) < 0.0f)
									{
										quatAngleRadians = -quatAngleRadians;
									}
									float limitedRotationAngleRadians = Limit(quatAngleRadians, DEGREES_TO_RADIANS(jl.m_min), DEGREES_TO_RADIANS(jl.m_max));
									limitedRot = QuatFromAxisAngle(Vector(kUnitXAxis), limitedRotationAngleRadians);
								}
								else if (componentIndex == 1)	// Limit rotation to only be around the local Y-axis
								{
									if (Dot(Vector(quatAxis), Vector(kUnitYAxis)) < 0.0f)
									{
										quatAngleRadians = -quatAngleRadians;
									}
									float limitedRotationAngleRadians = Limit(quatAngleRadians, DEGREES_TO_RADIANS(jl.m_min), DEGREES_TO_RADIANS(jl.m_max));
									limitedRot = QuatFromAxisAngle(Vector(kUnitYAxis), limitedRotationAngleRadians);
								}
								else if (componentIndex == 2)	// Limit rotation to only be around the local Z-axis
								{
									if (Dot(Vector(quatAxis), Vector(kUnitZAxis)) < 0.0f)
									{
										quatAngleRadians = -quatAngleRadians;
									}
									float limitedRotationAngleRadians = Limit(quatAngleRadians, DEGREES_TO_RADIANS(jl.m_min), DEGREES_TO_RADIANS(jl.m_max));
									limitedRot = QuatFromAxisAngle(Vector(kUnitZAxis), limitedRotationAngleRadians);
								}

								// Apply the limited quat
								jp.m_quat = bindposeQuat * limitedRot;
							}
							else if (jl.m_type < 9)		// Scale
							{
								U32 componentIndex = jl.m_type - 6;
								F32 value = jp.m_scale.Get(componentIndex);
								F32 clampedValue = Limit(value, jl.m_min, jl.m_max);
								jp.m_scale.Set(componentIndex, clampedValue);
							}
						}
					}
				}

				EmitRequestedFloatChannelOutputs(pCtx, pPrevCtx, &segmentContext, segmentIndex, outputMask);

				// Only do this if we need output requiring parenting
 				if ((outputMask & (AnimExecutionContext::kOutputTransformsOs | AnimExecutionContext::kOutputSkinningMats | AnimExecutionContext::kOutputOutputControls)) ||
 					((outputMask & AnimExecutionContext::kOutputJointParamsLs) && pCtx->m_includeProceduralJointParamsInOutput))
				{
					const AnimCmd_EvaluateJointHierarchy_Prepare* pCmdEvalJointHier = static_cast<const AnimCmd_EvaluateJointHierarchy_Prepare*>(pCmd);
					OrbisAnim::Status status = EvaluateJointHierarchyCmds_Prepare(&segmentContext, pCmdEvalJointHier->m_pInputControls); 
					ANIM_ASSERT(status == OrbisAnim::kSuccess);
				}
			}
			break;

		case AnimCmd::kEvaluateJointHierarchyCmds_Evaluate:
			{
				// Only do this costly step if we need output requiring parenting
				if ((outputMask & (AnimExecutionContext::kOutputTransformsOs | AnimExecutionContext::kOutputSkinningMats | AnimExecutionContext::kOutputOutputControls)) ||
					((outputMask & AnimExecutionContext::kOutputJointParamsLs) && pCtx->m_includeProceduralJointParamsInOutput))
				{
					PROFILE(Animation, JointHierarchyCmd_Evaluate);
					const AnimCmd_EvaluateJointHierarchy_Evaluate* pCmdEvalJointHier = static_cast<const AnimCmd_EvaluateJointHierarchy_Evaluate*>(pCmd);
					OrbisAnim::Status status = EvaluateJointHierarchyCmds_Evaluate(&segmentContext);
					ANIM_ASSERT(status == OrbisAnim::kSuccess);
				}
			}
			break;

		case AnimCmd::kInitializeJointSet_AnimPhase:
			{
				if (!pCtx->m_allowAnimPhasePlugins)
					continue;

				const AnimCmd_InitializeJointSet_AnimPhase* pCmdInitJointSet = static_cast<const AnimCmd_InitializeJointSet_AnimPhase*>(pCmd);

				// We are not allowed to evaluate poses in a deferred manner. It could read data that has relocated since we created the command list
				if (!isDeferred && segmentContext.m_iSegment == 0)
				{
					ANIM_ASSERT(pCmdInitJointSet->m_pJointSet);
					ANIM_ASSERT(pGroupContext);
					ANIM_ASSERT(pGroupContext->m_pProcessingGroup);
				
					const U32F numJointsInGroup = pGroupContext->m_pProcessingGroup->m_numAnimatedJoints;
					const U32F firstJointIndexInGroup = pGroupContext->m_pProcessingGroup->m_firstAnimatedJoint;

					U32F instanceIndex = pCmdInitJointSet->m_instance * pGroupContext->m_pProcessingGroup->m_numChannelGroups;
					const OrbisAnim::ValidBits* pValidBits = pGroupContext->m_pValidOutputChannels + instanceIndex;
					void** plocChannelGroup = pGroupContext->m_plocChannelGroupInstance + instanceIndex;
					const ndanim::JointParams* pJointParams = (const ndanim::JointParams*)plocChannelGroup[0];

					const ndanim::JointParams* pDefaultJoints = ndanim::GetDefaultJointPoseTable((const ndanim::JointHierarchy*)pHierarchy);
					const ndanim::JointParams* pFirstDefaultJoint = pDefaultJoints + firstJointIndexInGroup;

					pCmdInitJointSet->m_pJointSet->ReadFromJointParams(pJointParams,
																	   firstJointIndexInGroup,
																	   numJointsInGroup,
																	   pCmdInitJointSet->m_rootScale,
																	   pValidBits,
																	   pFirstDefaultJoint);
					//pCmdInitJointSet->m_pJointSet->DebugDrawJoints();
				}
			}
			break;

		case AnimCmd::kInitializeJointSet_RigPhase:
			{
				if (!pCtx->m_allowRigPhasePlugins)
					continue;

				const AnimCmd_InitializeJointSet_RigPhase* pCmdInitJointSet = static_cast<const AnimCmd_InitializeJointSet_RigPhase*>(pCmd);

				// We are not allowed to evaluate poses in a deferred manner. It could read data that has relocated since we created the command list
				if (!isDeferred && segmentContext.m_iSegment == 0)
				{
					ANIM_ASSERT(pCmdInitJointSet->m_pJointSet);

					const ndanim::JointParams* pJointParamsLs = static_cast<const ndanim::JointParams*>(segmentContext.m_locJointParams);
					ANIM_ASSERT(pJointParamsLs);

					const U32F indexBase = segmentContext.m_pSegment->m_firstJoint;
					const U32F numJoints = segmentContext.m_pSegment->m_numAnimatedJoints;

					if (!pCmdInitJointSet->m_pJointSet->ReadFromJointParams(pJointParamsLs,
																			indexBase,
																			numJoints,
																			pCmdInitJointSet->m_rootScale))
					{
						ANIM_ASSERTF(AssertPrintAnimCmdList(pOwner, pAnimCmdList, pCmd),
									 ("'%s' Failed to initialize joint set for segment %d (joints %d -> %d)",
									  pOwner ? pOwner->GetName() : "<null>",
									  segmentContext.m_iSegment,
									  indexBase,
									  indexBase + numJoints));
					}
				}
			}
			break;

		case AnimCmd::kCommitJointSet_AnimPhase:
			{
				if (!pCtx->m_allowAnimPhasePlugins)
					continue;

				const AnimCmd_CommitJointSet_AnimPhase* pCmdCommitJointSet = static_cast<const AnimCmd_CommitJointSet_AnimPhase*>(pCmd);

				// We are not allowed to evaluate poses in a deferred manner. It could read data that has relocated since we created the command list
				if (!isDeferred && segmentContext.m_iSegment == 0)
				{
					ANIM_ASSERT(pCmdCommitJointSet->m_pJointSet);
					ANIM_ASSERT(pGroupContext);
					ANIM_ASSERT(pGroupContext->m_pProcessingGroup);

					const U32F numJointsInGroup = pGroupContext->m_pProcessingGroup->m_numAnimatedJoints;
					const U32F firstJointIndexInGroup = pGroupContext->m_pProcessingGroup->m_firstAnimatedJoint;

					U32F instanceIndex = pCmdCommitJointSet->m_instance * pGroupContext->m_pProcessingGroup->m_numChannelGroups;
					void** plocChannelGroup = pGroupContext->m_plocChannelGroupInstance + instanceIndex;
					ndanim::JointParams* pJointParams = (ndanim::JointParams*)plocChannelGroup[0];

					pCmdCommitJointSet->m_pJointSet->WriteJointParamsBlend(pCmdCommitJointSet->m_blend,
																		   pJointParams,
																		   firstJointIndexInGroup,
																		   numJointsInGroup);
					//pCmdCommitJointSet->m_pJointSet->WriteJointParamsBlend(0.0f, pJointParams, firstJointIndexInGroup, numJointsInGroup);
				}
			}
			break;

		case AnimCmd::kCommitJointSet_RigPhase:
			{
				if (!pCtx->m_allowRigPhasePlugins)
					continue;

				const AnimCmd_CommitJointSet_RigPhase* pCmdCommitJointSet = static_cast<const AnimCmd_CommitJointSet_RigPhase*>(pCmd);

				// We are not allowed to evaluate poses in a deferred manner. It could read data that has relocated since we created the command list
				if (!isDeferred && segmentContext.m_iSegment == 0)
				{
					ndanim::JointParams* pJointParamsLs = static_cast<ndanim::JointParams*>(segmentContext.m_locJointParams);
					const U32F indexBase = segmentContext.m_pSegment->m_firstJoint;
					const U32F numJoints = segmentContext.m_pSegment->m_numJoints;

					JointSet* pJointSet = pCmdCommitJointSet->m_pJointSet;
					if (pJointSet && pJointSet->IsJointDataValid(0))
					{
						pJointSet->WriteJointParamsBlend(pCmdCommitJointSet->m_blend,
														 pJointParamsLs,
														 indexBase,
														 numJoints);
					}
				}
			}
			break;

		case AnimCmd::kApplyJointLimits:
			{
				const AnimCmd_ApplyJointLimits* pCmdApplyJointLimits = static_cast<const AnimCmd_ApplyJointLimits*>(pCmd);

				// We are not allowed to evaluate poses in a deferred manner. It could read data that has relocated since we created the command list
				if (!isDeferred && segmentContext.m_iSegment == 0)
				{
					ANIM_ASSERT(pCmdApplyJointLimits->m_pJointLimits);
					ANIM_ASSERT(pCmdApplyJointLimits->m_pJointSet);

					pCmdApplyJointLimits->m_pJointLimits->ApplyJointLimits(pCmdApplyJointLimits->m_pJointSet);
				}
			}
			break;

		case AnimCmd::kEvaluateAnimPhasePlugin:
			{
				if (!pCtx->m_allowAnimPhasePlugins)
					continue;

				const AnimCmd_EvaluateAnimPhasePlugin* pCmdEvalPlugin = static_cast<const AnimCmd_EvaluateAnimPhasePlugin*>(pCmd);

				// We are not allowed to evaluate poses in a deferred manner. It could read data that has relocated since we created the command list
				if (!isDeferred && segmentContext.m_iSegment == 0)
				{
					PROFILE_STRINGID(Animation, Process_EvaluateAnimPhasePlugin, pCmdEvalPlugin->m_pluginId);

					if (FALSE_IN_FINAL_BUILD(pGroupContext && g_animOptions.m_enableValidation))
					{
						for (int i = 0; i < pGroupContext->m_numInstances; ++i)
						{
							ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, i);
						}
					}

					ANIM_ASSERT(pGroupContext);
					ANIM_ASSERTF(pAnimPhasePluginCommandFunc, ("A AnimPhase plugin was found in the command stream but no handler had been registered!"));
					if (pAnimPhasePluginCommandFunc)
					{
						pAnimPhasePluginCommandFunc(pWorkBuffer,
													&segmentContext,
													pGroupContext,
													pCtx,
													pCmdEvalPlugin->m_pluginId,
													&pCmdEvalPlugin->m_blindData[0],
													pCmdEvalPlugin->m_pPluginJointSet);
					}

					if (FALSE_IN_FINAL_BUILD(pGroupContext && g_animOptions.m_enableValidation))
					{
						for (int i = 0; i < pGroupContext->m_numInstances; ++i)
						{
							ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, i);
						}
					}
				}
			}
			break;

		case AnimCmd::kEvaluateRigPhasePlugin:
			{
				if (!pCtx->m_allowRigPhasePlugins)
					continue;

				const AnimCmd_EvaluateRigPhasePlugin* pCmdEvalPlugin = static_cast<const AnimCmd_EvaluateRigPhasePlugin*>(pCmd);

				// We are not allowed to evaluate poses in a deferred manner. It could read data that has relocated since we created the command list
				if ((!isDeferred && segmentContext.m_iSegment == 0) || pCmdEvalPlugin->m_pluginId == SID("retarget-anim"))
				{
					PROFILE_STRINGID(Animation, Process_EvaluateRigPhasePlugin, pCmdEvalPlugin->m_pluginId);

					if (FALSE_IN_FINAL_BUILD(pGroupContext && g_animOptions.m_enableValidation))
					{
						for (int i = 0; i < pGroupContext->m_numInstances; ++i)
						{
							ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, i);
						}
					}

					ANIM_ASSERTF(pRigPhasePluginCommandFunc, ("A RigPhase plugin was found in the command stream but no handler had been registered!"));
					if (pRigPhasePluginCommandFunc)
					{
						pRigPhasePluginCommandFunc(pWorkBuffer,
														&segmentContext,
														pCtx,
														pCmdEvalPlugin->m_pluginId,
														&pCmdEvalPlugin->m_blindData[0],
														pCmdEvalPlugin->m_pPluginJointSet);
					}
				}
			}
			break;

		case AnimCmd::kEvaluatePostRetarget:
			{
				// We are not allowed to evaluate poses in a deferred manner. It could read data that has relocated since we created the command list
				if (segmentContext.m_locJointParams)
				{
					const AnimCmd_EvaluatePostRetarget* pCmdEvalPostRetarget = static_cast<const AnimCmd_EvaluatePostRetarget*>(pCmd);
					
					const SkeletonId srcSkelId = pCmdEvalPostRetarget->m_pSrcSkel->m_skelId;
					const SkeletonId tgtSkelId = pCmdEvalPostRetarget->m_pTgtSkel->m_skelId;

					const SkelTable::RetargetEntry* pRetargetEntry = SkelTable::LookupRetarget(srcSkelId, tgtSkelId);

					if (FALSE_IN_FINAL_BUILD(!pRetargetEntry))
					{
						Memory::PushAllocator(kAllocDebug, FILE_LINE_FUNC);

						StringId64* pJointIds = STACK_ALLOC(StringId64, pCmdEvalPostRetarget->m_pTgtSkel->m_numTotalJoints);
						for (U32 i = 0; i < pCmdEvalPostRetarget->m_pTgtSkel->m_numTotalJoints; ++i)
						{
							pJointIds[i] = pCmdEvalPostRetarget->m_pTgtSkel->m_pJointDescs[i].m_nameId;
						}

						// Create a custom retarget entry
						SkelTable::RetargetEntry* pNewRetargetEntry = nullptr;
						ConstructRetargetEntry(pCmdEvalPostRetarget->m_pSrcSkel,
											   pJointIds,
											   pCmdEvalPostRetarget->m_pTgtSkel,
											   &pNewRetargetEntry);
						pRetargetEntry = pNewRetargetEntry;

						Memory::PopAllocator();
					}

					ANIM_ASSERT(pRetargetEntry);

					// We need the valid bits so that we can figure out if we need to fill in some undefined joints after the retargeting is done
					ndanim::ValidBits* pValidBitsTable = STACK_ALLOC(ndanim::ValidBits, pCmdEvalPostRetarget->m_pTgtSkel->m_pAnimHierarchy->m_numChannelGroups);
					memset(pValidBitsTable, 0, sizeof(ndanim::ValidBits) * pCmdEvalPostRetarget->m_pTgtSkel->m_pAnimHierarchy->m_numChannelGroups);

					const U32 firstAnimatedJointInSeg = ndanim::GetFirstAnimatedJointInSegment(pCmdEvalPostRetarget->m_pTgtSkel->m_pAnimHierarchy, segmentContext.m_iSegment);

					// ****** IMPORTANT NOTE ****** We will now fake a complete joint pose but we are actually only having a valid data section where this segment have data.
					// ****** IMPORTANT NOTE ****** We will now fake a complete joint pose but we are actually only having a valid data section where this segment have data.
					ndanim::AnimatedJointPose outJointPose;
					outJointPose.m_pJointParams = (ndanim::JointParams*)segmentContext.m_locJointParams - firstAnimatedJointInSeg;
					ANIM_ASSERT(outJointPose.m_pJointParams);
					// ****** IMPORTANT NOTE ****** We will now fake a complete joint pose but we are actually only having a valid data section where this segment have data.
					// ****** IMPORTANT NOTE ****** We will now fake a complete joint pose but we are actually only having a valid data section where this segment have data.
					outJointPose.m_pFloatChannels = (float*)segmentContext.m_locFloatChannels;
					outJointPose.m_pValidBitsTable = pValidBitsTable;

					RetargetInput ri;
					ri.m_pInPose = &pCmdEvalPostRetarget->m_inputPose;
					RetargetPoseForSegment(ri,
										   pCmdEvalPostRetarget->m_pSrcSkel,
										   pCmdEvalPostRetarget->m_pTgtSkel,
										   pRetargetEntry,
										   segmentContext.m_iSegment,
										   &outJointPose);

					// Fill in undefined joints using the valid bits as guidance
					const ndanim::JointParams* pDefaultJointParams = ndanim::GetDefaultJointPoseTable(pCmdEvalPostRetarget->m_pTgtSkel->m_pAnimHierarchy);
					const F32* pDefaultFloatChannels = ndanim::GetDefaultFloatChannelPoseTable(pCmdEvalPostRetarget->m_pTgtSkel->m_pAnimHierarchy);
					const ndanim::ProcessingGroup* pProcGroupTable = ndanim::GetProcessingGroupTable(pCmdEvalPostRetarget->m_pTgtSkel->m_pAnimHierarchy);
					const U16 firstProcGroupInSeg = segmentContext.m_pSegment->m_firstProcessingGroup;
					const U16 numProcGroupsInSeg = segmentContext.m_pSegment->m_numProcessingGroups;
					U16 channelGroupOffset = 0;
					for (U16 iProcGrp = 0; iProcGrp < numProcGroupsInSeg; ++iProcGrp)
					{
						const ndanim::ProcessingGroup* pProcGroup = &pProcGroupTable[firstProcGroupInSeg + iProcGrp];
						const U16 firstChannelGroup = segmentContext.m_pSegment->m_firstChannelGroup + pProcGroup->m_firstChannelGroup;
						for (U16 iChannelGroup = 0; iChannelGroup < pProcGroup->m_numChannelGroups; ++iChannelGroup)
						{
							const ndanim::ValidBits* pValidBits = &pValidBitsTable[firstChannelGroup + iChannelGroup];
							if (iChannelGroup == 0)	// Joints
							{
								const U16 firstAnimatedJoint = pProcGroup->m_firstAnimatedJoint;
								const U16 numAnimatedJoints = pProcGroup->m_numAnimatedJoints;
								for (U16 iJoint = 0; iJoint < numAnimatedJoints; iJoint++)
								{
									// Fill in default pose values for all invalid joints
									if (!pValidBits->IsBitSet(iJoint))
									{
										outJointPose.m_pJointParams[firstAnimatedJoint + iJoint] = pDefaultJointParams[firstAnimatedJoint + iJoint];
									}
								}
							}
							else					// Float channels
							{
								const U16 firstFloatChannel = pProcGroup->m_firstFloatChannel;
								const U16 numFloatChannels = pProcGroup->m_numFloatChannels;
								for (U16 iFloatChan = 0; iFloatChan < numFloatChannels; iFloatChan++)
								{
									// Fill in default pose values for all invalid joints
									if (!pValidBits->IsBitSet(iFloatChan))
									{
										outJointPose.m_pFloatChannels[firstFloatChannel + iFloatChan] = pDefaultFloatChannels[firstFloatChannel + iFloatChan];
									}
								}
							}
							channelGroupOffset++;
						}
					}
				}
			}
			break;

		case AnimCmd::kLayer:
		case AnimCmd::kState:
			break;

		case AnimCmd::kEvaluateSnapshotPoseDeferred:
			{
				PROFILE(Animation, Process_Anim_Cmd_List_EvalPoseDeferred);
				const AnimCmd_EvaluateSnapshotPoseDeferred* pCmdEvalPose = static_cast<const AnimCmd_EvaluateSnapshotPoseDeferred*>(pCmd);
				ANIM_ASSERT(pCmdEvalPose->m_pNode);
				if (pCmdEvalPose->m_pNode->m_hierarchyId != pHierarchy->m_animHierarchyId)
				{
					ANIM_ASSERT(pCmdEvalPose->m_outputInstance < maxProcessingGroupInstances);
					OrbisAnim::EvaluateEmptyPose(pGroupContext, pCmdEvalPose->m_outputInstance);
					break;
				}

				ANIM_ASSERT(pCmdEvalPose->m_pNode->m_hierarchyId == pHierarchy->m_animHierarchyId);
				ValidateSegmentInPose(pOwner,
									  pAnimCmdList,
									  pCmd,
									  pSkel,
									  pHierarchy,
									  &segmentContext,
									  &pCmdEvalPose->m_pNode->m_jointPose);

				ANIM_ASSERT(pCmdEvalPose->m_outputInstance < maxProcessingGroupInstances);
				OrbisAnim::EvaluatePose(pGroupContext,
										pCmdEvalPose->m_outputInstance,
										(OrbisAnim::JointParams const*)pCmdEvalPose->m_pNode->m_jointPose.m_pJointParams,
										pCmdEvalPose->m_pNode->m_jointPose.m_pFloatChannels,
										(OrbisAnim::ValidBits const*)pCmdEvalPose->m_pNode->m_jointPose.m_pValidBitsTable);

				ValidatePose(pOwner,
							 pAnimCmdList,
							 pCmd,
							 pGroupContext,
							 pSkel,
							 pHierarchy,
							 &pCmdEvalPose->m_pNode->m_jointPose);

				ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalPose->m_outputInstance);
			}
			break;

		case AnimCmd::kEvaluateSnapshotDeferred:
			{
				PROFILE(Animation, Process_Anim_Cmd_List_EvalSnapshotDeferred);
				const AnimCmd_EvaluateSnapshotDeferred* pCmdEvalSnapshot = static_cast<const AnimCmd_EvaluateSnapshotDeferred*>(pCmd);
				ANIM_ASSERT(pCmdEvalSnapshot->m_pNode);
				if (pCmdEvalSnapshot->m_pNode->m_hierarchyId != pHierarchy->m_animHierarchyId)
				{
					break;
				}
				ANIM_ASSERT(pCmdEvalSnapshot->m_pNode->m_hierarchyId == pHierarchy->m_animHierarchyId);
				{
					ANIM_ASSERT(pCmdEvalSnapshot->m_inputInstance < maxProcessingGroupInstances);
					OrbisAnim::EvaluateSnapshot(pGroupContext,
												pCmdEvalSnapshot->m_inputInstance,
												(OrbisAnim::JointParams*)pCmdEvalSnapshot->m_pNode->m_jointPose.m_pJointParams,
												pCmdEvalSnapshot->m_pNode->m_jointPose.m_pFloatChannels,
												(OrbisAnim::ValidBits*)pCmdEvalSnapshot->m_pNode->m_jointPose.m_pValidBitsTable);

					ValidateInstance(pOwner, pAnimCmdList, pCmd, pGroupContext, pCmdEvalSnapshot->m_inputInstance);
					ValidatePose(pOwner,
								 pAnimCmdList,
								 pCmd,
								 pGroupContext,
								 pSkel,
								 pHierarchy,
								 &pCmdEvalSnapshot->m_pNode->m_jointPose);
				}
			}
			break;

		default:
			ALWAYS_ASSERT(false);
			break;
		}
	}


#ifndef FINAL_BUILD
	if (g_animOptions.m_printSegmentProcessing)
	{
		const U64 endTick = TimerGetRawCount();
		MsgAnim("Frame %ull: Processing segment %d for '%s' [%s] in %.3f ms - %s%s%s%s in the %s phase\n",
			GetCurrentFrameNumber(),
			segmentIndex,
			pCtx->m_pAnimDataHack ? pCtx->m_pAnimDataHack->m_hProcess.ToProcess()->GetName() : "unknown",
			pCtx->m_pSkel->GetName(),
			ConvertTicksToMilliseconds(endTick - startTick),
			(outputMask & AnimExecutionContext::kOutputJointParamsLs) ? "JointParams " : "",
			(outputMask & AnimExecutionContext::kOutputTransformsOs) ? "Transforms " : "",
			(outputMask & AnimExecutionContext::kOutputSkinningMats) ? "SkinMats " : "",
			isRetargeting ? "Retarget" : "",
			ndjob::IsRenderFrameJob() ? "Render" : "Game");
	}
#endif
}


/// --------------------------------------------------------------------------------------------------------------- ///
struct ProcessAnimCmdListData
{
	static const int kMaxBatchSize = 16;
	AnimExecutionContext*		m_pCtx[kMaxBatchSize];
	const AnimExecutionContext*	m_pPrevCtx[kMaxBatchSize];
	U32							m_requiredSegmentMask[kMaxBatchSize];
	U32							m_outputMask[kMaxBatchSize];
	int							m_ctxCount = 0;
	bool						m_allowYielding = false;
};


/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(ProcessAnimCmdListJobBatched)
{
	const ProcessAnimCmdListData* pJobData = (const ProcessAnimCmdListData*)jobParam;

	for (int ctxNum = 0; ctxNum < pJobData->m_ctxCount; ctxNum++)
	{
		AnimExecutionContext* pCtx = pJobData->m_pCtx[ctxNum];
		const AnimExecutionContext* pPrevCtx = pJobData->m_pPrevCtx[ctxNum];
		const U32 requiredSegmentMask = pJobData->m_requiredSegmentMask[ctxNum];
		const U32 outputMask = pJobData->m_outputMask[ctxNum];

		if (requiredSegmentMask)
		{
			ProcessRequiredSegments(requiredSegmentMask, outputMask, pCtx, pPrevCtx, false, true);

			ANIM_ASSERT(pJobData->m_requiredSegmentMask[ctxNum] == 0xFFFFFFFF ||
				(pCtx->GetCombinedSegmentMask() & pJobData->m_requiredSegmentMask[ctxNum]) == pJobData->m_requiredSegmentMask[ctxNum]);
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessRequiredSegments(U32 requiredSegmentMask,
							 U32 outputMask,
							 AnimExecutionContext* pCtx,
							 const AnimExecutionContext* pPrevCtx,
							 bool useImpliedPoseAsInput,
							 bool kickEarlyNextFrame)
{
	PROFILE(Animation, ProcessRequiredSegments);

	NdAtomic64Janitor jj(&pCtx->m_lock);

	ANIM_ASSERT(pCtx->m_pSkel);

	if (outputMask & AnimExecutionContext::kOutputJointParamsLs)
	{
		// We can't output to the joint params if we have also specified that the joint params should be an output
		ANIM_ASSERT(!useImpliedPoseAsInput);
	}

	ANIMLOG("ProcessRequiredSegments %s req seg mask %x",
			pCtx->m_pSkel ? pCtx->m_pSkel->GetName() : "<null>",
			requiredSegmentMask);

	// If we know we will need these again we can kick them early next frame so that we don't have to wait
	if (kickEarlyNextFrame)
	{
		ANIM_ASSERT(outputMask & AnimExecutionContext::kOutputSkinningMats);
		if (ndjob::IsRenderFrameJob())
		{
			const_cast<FgAnimData*>(pCtx->m_pAnimDataHack)->m_earlyDeferredSegmentMaskRender |= (requiredSegmentMask & ~1);
		}
		else
		{
			const_cast<FgAnimData*>(pCtx->m_pAnimDataHack)->m_earlyDeferredSegmentMaskGame |= (requiredSegmentMask & ~1);
		}
	}

	// Expand the required segments by also including any dependent segments
	SegmentMask dependentSegments(false);
	const int numSegments = pCtx->m_pSkel ? pCtx->m_pSkel->m_numSegments : 0;
	for (int iSegment = 0; iSegment < numSegments; iSegment++)
	{
		const U32 segmentIndexBit = (0x1 << iSegment);
		if (requiredSegmentMask & segmentIndexBit)
		{
			dependentSegments.SetBit(iSegment);

			// HACK - We only need to animate the previous segments if we are outputting parented data
			// Pure animation data does not need the previous segments to animate
			if ((outputMask & (AnimExecutionContext::kOutputTransformsOs | AnimExecutionContext::kOutputSkinningMats)) ||
				((outputMask & AnimExecutionContext::kOutputJointParamsLs) && pCtx->m_includeProceduralJointParamsInOutput))
			{
				SegmentMask::BitwiseOr(&dependentSegments, dependentSegments, GetDependentSegmentMask(pCtx->m_pSkel, iSegment));
			}
		}
	}

	const U64 requestedSegments = dependentSegments.GetData();
	ANIMLOG("ProcessRequiredSegments %s requested 0x%x processed mask 0x%x",
			pCtx->m_pSkel ? pCtx->m_pSkel->GetName() : "<null>",
			requestedSegments,
			pCtx->GetCombinedSegmentMask());

	// Figure out which segments have already been processed
	U64 segmentsToAnimate = 0;
	for (int iSegment = 0; iSegment < numSegments; iSegment++)
	{
		const U32 segmentIndexBit = (0x1 << iSegment);
		if (requestedSegments & segmentIndexBit)
		{
			bool needToProcessSegment = false;
			if (outputMask & AnimExecutionContext::kOutputJointParamsLs)
			{
				if (!(pCtx->m_processedSegmentMask_JointParams & segmentIndexBit))
					needToProcessSegment = true;
			}
			if (outputMask & AnimExecutionContext::kOutputFloatChannels)
			{
				if (!(pCtx->m_processedSegmentMask_FloatChannels & segmentIndexBit))
					needToProcessSegment = true;
			}
			if (outputMask & AnimExecutionContext::kOutputTransformsOs)
			{
				if (!(pCtx->m_processedSegmentMask_Transforms & segmentIndexBit))
					needToProcessSegment = true;
			}
			if (outputMask & AnimExecutionContext::kOutputSkinningMats)
			{
				if (!(pCtx->m_processedSegmentMask_SkinningMats & segmentIndexBit))
					needToProcessSegment = true;
			}
			if (outputMask & AnimExecutionContext::kOutputOutputControls)
			{
				if (!(pCtx->m_processedSegmentMask_OutputControls & segmentIndexBit))
					needToProcessSegment = true;
			}

			// Retargeting is not having an output mask, so this is a special case for now
			if (needToProcessSegment || !outputMask)
				segmentsToAnimate |= segmentIndexBit;
		}
	}

	ANIMLOG("ProcessRequiredSegments %s to animate 0x%x",
			pCtx->m_pSkel ? pCtx->m_pSkel->GetName() : "<null>",
			segmentsToAnimate);

	if (!segmentsToAnimate)
		return;

	// Now, process each segment in order
	for (U32 segmentIndex = 0; segmentIndex < numSegments; ++segmentIndex)
	{
		const U32 segmentIndexMask = (0x1 << segmentIndex);
		if (segmentsToAnimate & segmentIndexMask)
		{
			// Allocate output buffers if needed
			if ((outputMask & AnimExecutionContext::kOutputJointParamsLs) &&
				!pCtx->m_pSegmentData[segmentIndex].m_pJointParams)
			{
				// We need to manually provide the JointParam array if we want to include the procedural joints
				ANIM_ASSERT(!pCtx->m_includeProceduralJointParamsInOutput);

				const U32F numAnimatedJointsInSegment = ndanim::GetNumAnimatedJointsInSegment(pCtx->m_pSkel->m_pAnimHierarchy, segmentIndex);
				if (numAnimatedJointsInSegment)
				{
					pCtx->m_pSegmentData[segmentIndex].m_pJointParams = NDI_NEW(kAllocGpuRing, kAlign128) ndanim::JointParams[numAnimatedJointsInSegment];
				}
			}

			// Allocate output buffers if needed
			if ((outputMask & AnimExecutionContext::kOutputTransformsOs) &&
				!pCtx->m_pSegmentData[segmentIndex].m_pJointTransforms)
			{
				const U32F numJointsInSegment = ndanim::GetNumJointsInSegment(pCtx->m_pSkel->m_pAnimHierarchy, segmentIndex);
				pCtx->m_pSegmentData[segmentIndex].m_pJointTransforms = NDI_NEW(kAllocGpuRing, kAlign128) Transform[numJointsInSegment];
			}

			if ((outputMask & AnimExecutionContext::kOutputSkinningMats) &&
				!pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats)
			{
				ANIM_ASSERT(pCtx->m_pAllSkinningBoneMats);
				ANIM_ASSERT(!pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats);
				const U32F firstJoint = ndanim::GetFirstJointInSegment(pCtx->m_pSkel->m_pAnimHierarchy, segmentIndex);
				pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats = pCtx->m_pAllSkinningBoneMats + firstJoint * 3 /*Vec4sPerMat34*/;
				ANIM_ASSERT(pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats);
			}

			if ((outputMask & (AnimExecutionContext::kOutputFloatChannels | AnimExecutionContext::kOutputOutputControls)) &&
				!pCtx->m_pSegmentData[segmentIndex].m_pOutputControls)
			{
				// Sum up all output controls in the previous segments
				U32 firstOutputControlInSegment = 0;
				for (int iSeg = 0; iSeg < segmentIndex; ++iSeg)
					firstOutputControlInSegment += ndanim::GetNumOutputControlsInSegment(pCtx->m_pSkel->m_pAnimHierarchy, iSeg);

				pCtx->m_pSegmentData[segmentIndex].m_pOutputControls = pCtx->m_pAllOutputControls + firstOutputControlInSegment;
			}

			ProcessAnimSegment(segmentIndex,
							   outputMask,
							   pCtx,
							   pPrevCtx,
							   pCtx->m_pAnimPhasePluginFunc,
							   pCtx->m_pRigPhasePluginFunc,
							   useImpliedPoseAsInput);

			if (outputMask & AnimExecutionContext::kOutputJointParamsLs)
			{
				ANIM_ASSERT(pCtx->m_processedSegmentMask_JointParams & segmentIndexMask);
			}
			if (outputMask & AnimExecutionContext::kOutputFloatChannels)
			{
				ANIM_ASSERT(pCtx->m_processedSegmentMask_FloatChannels & segmentIndexMask);
			}
			if (outputMask & AnimExecutionContext::kOutputTransformsOs)
			{
				ANIM_ASSERT(pCtx->m_processedSegmentMask_Transforms & segmentIndexMask);
			}
			if (outputMask & AnimExecutionContext::kOutputSkinningMats)
			{
				ANIM_ASSERT(pCtx->m_processedSegmentMask_SkinningMats & segmentIndexMask);
			}
			if (outputMask & AnimExecutionContext::kOutputOutputControls)
			{
				ANIM_ASSERT(pCtx->m_processedSegmentMask_OutputControls & segmentIndexMask);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BatchProcessRequiredSegmentsForAllObjects(const U32* pRequiredSegmentMasks, const U32* pOutputMasks, I64 frameIndex)
{
	PROFILE(Animation, BatchProcessRequiredSegmentsForAllObjects);

	RenderFrameParams* pParams = GetRenderFrameParams(frameIndex);
	RenderFrameParams* pPrevParams = (frameIndex > 0) ? GetRenderFrameParams(frameIndex - 1) : nullptr;

	AnimFrameExecutionData* pFrameData = pParams->m_pAnimFrameExecutionData;
	AnimFrameExecutionData* pPrevFrameData = pPrevParams ? pPrevParams->m_pAnimFrameExecutionData : nullptr;

	AnimMgr::AnimDataBits* pUsedAnimBits = pFrameData->m_pUsedAnimExecutionContexts;

	const int numCtxs = pUsedAnimBits->CountSetBits();
	if (numCtxs > 0)
	{
		const int maxBatchSize = ProcessAnimCmdListData::kMaxBatchSize;
		int batchSize = MinMax(numCtxs / 12, 1, maxBatchSize);
		int numBatches = (numCtxs + batchSize - 1) / batchSize;

		ProcessAnimCmdListData* pBatchData = NDI_NEW(kAllocSingleFrame) ProcessAnimCmdListData[numBatches];

		// ProcessAnimCmdListJobBatched is causing stalls when run on core 6, let's not use it
		ndjob::JobArrayHandle jobArray = ndjob::BeginJobArray(numCtxs, ndjob::Priority::kGameFrameLowest, ndjob::Affinity::kMask_AllReal);
		ndjob::CounterHandle pJobCounter = ndjob::AllocateCounter(FILE_LINE_FUNC);
		ndjob::JobDecl* jobDecls = NDI_NEW(kAllocSingleFrame, kAlign64) ndjob::JobDecl[numCtxs];

		int currBatch = 0;
		for (U64 animDataIndex : *pUsedAnimBits)
		{
			ProcessAnimCmdListData* pCurrBatch = &pBatchData[currBatch];
			ANIM_ASSERT(pCurrBatch->m_ctxCount < ProcessAnimCmdListData::kMaxBatchSize);
			ANIM_ASSERT(pFrameData->m_pAnimExecutionContexts[animDataIndex].m_pSkel);

			pCurrBatch->m_requiredSegmentMask[pCurrBatch->m_ctxCount] = pRequiredSegmentMasks[animDataIndex];
			pCurrBatch->m_outputMask[pCurrBatch->m_ctxCount] = pOutputMasks[animDataIndex];

			pCurrBatch->m_pCtx[pCurrBatch->m_ctxCount] = &pFrameData->m_pAnimExecutionContexts[animDataIndex];
			pCurrBatch->m_pPrevCtx[pCurrBatch->m_ctxCount] = (pPrevFrameData && pPrevFrameData->m_pUsedAnimExecutionContexts->IsBitSet(animDataIndex)) ? &pPrevFrameData->m_pAnimExecutionContexts[animDataIndex] : nullptr;
			ANIM_ASSERT(pCurrBatch->m_pCtx[pCurrBatch->m_ctxCount]->m_pAnimDataHack);

			pCurrBatch->m_ctxCount++;
			pCurrBatch->m_allowYielding = ndjob::IsRenderFrameJob();

			currBatch = (currBatch + 1) % numBatches;

		}

		for (int i = 0; i < numBatches; i++)
		{
			jobDecls[i] = ndjob::JobDecl(ProcessAnimCmdListJobBatched, (uintptr_t)&pBatchData[i]);
			jobDecls[i].m_flags = ndjob::kDisallowSleep;		// Needed for submits of 32 jobs or more
			jobDecls[i].m_associatedCounter = pJobCounter;
		}

		pJobCounter->SetValue(numBatches);
		ndjob::AddJobs(jobArray, jobDecls, numBatches);
		ndjob::CommitJobArray(jobArray);
		ndjob::WaitForCounterAndFree(pJobCounter);
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void AnimExecutionContext::Init(const ArtItemSkeleton* pArtItemSkel,
								const Transform* pObjXform,
								ndanim::JointParams* pJointCacheJointParams,
								Transform* pJointCacheTransforms,
								const float* pJointCacheInputControls,
								float* pJointCacheOutputControls,
								void* pPersistentData,
								Memory::Allocator* const pAlloc)
{
	ANIM_ASSERT(pPersistentData || !pArtItemSkel->m_persistentDataSize);	// Make sure we have a persistent data buffer if required by the skeleton

	m_lock.Set(0);

	AnimExecutionContext* pCtx = this;
	pCtx->m_pObjXform = pObjXform;
	pCtx->m_pPersistentData = pPersistentData;

	const ArtItemSkeleton* pSkel = pArtItemSkel;

	Memory::Allocator* pLocalAlloc = pAlloc;
	Memory::Allocator* pDoubleAlloc = pAlloc;
	if (!pLocalAlloc)
	{
		pLocalAlloc = Memory::GetAllocator(kAllocGpuRing);
		pDoubleAlloc = Memory::GetAllocator(kAllocDoubleGpuRing);
	}

	const U32 depTableSize = pSkel->m_pAnimHierarchy->m_dependencyTableSize;
	pCtx->m_pDependencyTable = depTableSize ? pDoubleAlloc->Allocate(depTableSize, kAlign128, FILE_LINE_FUNC) : nullptr;
	pCtx->m_dependencyTableValid = false;

	const U32 kAnimCmdMemSize = 1500 * sizeof(AnimCmd);
	U8* pAnimCmdMemory = (U8*)pLocalAlloc->Allocate(kAnimCmdMemSize, kAlign128, FILE_LINE_FUNC);
	pCtx->m_animCmdList.Init(pAnimCmdMemory, kAnimCmdMemSize);

	ANIM_ASSERT(pSkel);
	const U32 numTotalJoints = pSkel->m_numTotalJoints;
	const U32 numInputControls = pSkel->m_pAnimHierarchy->m_numInputControls;
	const U32 numOutputControls = pSkel->m_pAnimHierarchy->m_numOutputControls;

	pCtx->m_pAllInputControls = nullptr;
	if (numInputControls)
	{
		float* pInputControls = (float*)pLocalAlloc->Allocate(numInputControls * sizeof(float), kAlign16, FILE_LINE_FUNC);

		if (pJointCacheInputControls)
			memcpy(pInputControls, pJointCacheInputControls, numInputControls * sizeof(float));

		pCtx->m_pAllInputControls = pInputControls;
	}

	pCtx->m_pAllOutputControls = numOutputControls ? (float*)pDoubleAlloc->Allocate (numOutputControls * sizeof(float), kAlign16, FILE_LINE_FUNC) : nullptr;

	pCtx->m_pSkel = pSkel;

	char* pAnimExecutionSegmentDataRaw = (char*)pDoubleAlloc->Allocate(pSkel->m_numSegments * sizeof(AnimExecutionSegmentData), kAlign128, FILE_LINE_FUNC);
	pCtx->m_pSegmentData = new (pAnimExecutionSegmentDataRaw) AnimExecutionSegmentData[pSkel->m_numSegments];

	// Let's use the joint cache as the storage location for the first segment
	pCtx->m_pSegmentData[0].m_pJointParams = pJointCacheJointParams;
	pCtx->m_pSegmentData[0].m_pJointTransforms = pJointCacheTransforms;
	pCtx->m_pSegmentData[0].m_pOutputControls = pJointCacheOutputControls;

	m_allowAnimPhasePlugins = true;
	m_allowRigPhasePlugins = true;

	m_disableBoundingVolumeUpdate = false;
	m_includeProceduralJointParamsInOutput = false;

	// HACK - We need this here for now because we are updating instance data in parallel with execution of the anim commands (which allocates the skinning mats memory)
	AllocateSkinningMats(pAlloc);
}


/// --------------------------------------------------------------------------------------------------------------- ///
void AnimExecutionContext::AllocateSkinningMats(Memory::Allocator* const pAlloc)
{
	if (m_pAllSkinningBoneMats)
		return;

	const int numSegments = m_pSkel->m_numSegments;
	const U32 numTotalJoints = m_pSkel->m_numTotalJoints;

	Memory::Allocator* pLocalAlloc = pAlloc;
	if (!pLocalAlloc)
		pLocalAlloc = Memory::GetAllocator(kAllocDoubleGpuRing);

	U8* pRawSkinningMatMem = (U8*)pLocalAlloc->Allocate(numTotalJoints * kNumVec4PerMat34 * sizeof(Vec4), kAlign16, FILE_LINE_FUNC);
	m_pAllSkinningBoneMats = (Vec4*)pRawSkinningMatMem;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void AnimExecutionContext::Validate() const
{
	STRIP_IN_FINAL_BUILD;
	if (!g_animOptions.m_enableValidation)
		return;

	const int numSegments = m_pSkel->m_numSegments;
	for (int iSeg = 0; iSeg < numSegments; ++iSeg)
	{
		const int numJointsInSeg = ndanim::GetNumJointsInSegment(m_pSkel->m_pAnimHierarchy, iSeg);
		const int numAnimatedJointsInSeg = ndanim::GetNumAnimatedJointsInSegment(m_pSkel->m_pAnimHierarchy, iSeg);
		const int numOutputControlsInSeg = ndanim::GetNumOutputControlsInSegment(m_pSkel->m_pAnimHierarchy, iSeg);

		if (m_processedSegmentMask_JointParams & (1 << iSeg))
		{
			ANIM_ASSERT(numAnimatedJointsInSeg == 0 || m_pSegmentData[iSeg].m_pJointParams);
			for (int iJoint = 0; iJoint < numAnimatedJointsInSeg; ++iJoint)
			{
				ANIM_ASSERT(IsReasonable(m_pSegmentData[iSeg].m_pJointParams[iJoint].m_scale));
				ANIM_ASSERT(IsReasonable(m_pSegmentData[iSeg].m_pJointParams[iJoint].m_quat));
				ANIM_ASSERT(IsReasonable(m_pSegmentData[iSeg].m_pJointParams[iJoint].m_trans));
			}
		}

		// Due to JointCache::OverwriteJointLocatorWs we can't guarantee that the transforms are valid.
		// However, we still need to set the 'Transform' processed flag to prevent the data written by
		// 'OverwriteJointLocatorWs' from being invalidated and tomped.
// 		if (m_processedSegmentMask_Transforms & (1 << iSeg))
// 		{
// 			ANIM_ASSERT(m_pSegmentData[iSeg].m_pJointTransforms);
// 			for (int iJoint = 0; iJoint < numAnimatedJointsInSeg; ++iJoint)
// 			{
// 				ANIM_ASSERT(IsReasonable(m_pSegmentData[iSeg].m_pJointTransforms[iJoint]));
// 			}
// 		}

		if (m_processedSegmentMask_SkinningMats & (1 << iSeg))
		{
			if (iSeg == 0)
			{
				ANIM_ASSERT(m_pSegmentData[iSeg].m_pSkinningBoneMats == m_pAllSkinningBoneMats);
			}

			ANIM_ASSERT(m_pSegmentData[iSeg].m_pSkinningBoneMats);
			for (int iRow = 0; iRow < numJointsInSeg*3; ++iRow)
			{
 				ANIM_ASSERT(IsReasonable(m_pSegmentData[iSeg].m_pSkinningBoneMats[iRow]));
			}
		}

		if (m_processedSegmentMask_OutputControls & (1 << iSeg))
		{
			if (numOutputControlsInSeg)
			{
				ANIM_ASSERT(m_pSegmentData[iSeg].m_pOutputControls);
				for (int iVal = 0; iVal < numOutputControlsInSeg; ++iVal)
				{
					ANIM_ASSERT(IsReasonable(m_pSegmentData[iSeg].m_pOutputControls[iVal]));
				}
			}
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
AnimExecutionContext* AllocateAnimContextForThisFrame(int animDataIndex)
{
	ANIM_ASSERT(animDataIndex >= 0 && animDataIndex < AnimMgr::kMaxAnimData);

	RenderFrameParams* pParams = GetCurrentRenderFrameParams();
	AnimFrameExecutionData* pExecutionData = pParams->m_pAnimFrameExecutionData;

	SpinLockAtomic32(&pExecutionData->m_lock);
	ANIM_ASSERT(!pExecutionData->m_pUsedAnimExecutionContexts->IsBitSet(animDataIndex));
	pExecutionData->m_pUsedAnimExecutionContexts->SetBit(animDataIndex);
	ReleaseLockAtomic32(&pExecutionData->m_lock);

	AnimExecutionContext* pCtx = NDI_NEW(pExecutionData->m_pAnimExecutionContexts + animDataIndex) AnimExecutionContext;
	pCtx->m_pAnimDataHack = EngineComponents::GetAnimMgr()->GetAnimData(animDataIndex);
	ANIM_ASSERT(pCtx->m_pAnimDataHack);

	return pCtx;
}


/// --------------------------------------------------------------------------------------------------------------- ///
AnimExecutionContext* GetAnimExecutionContext(const FgAnimData* pAnimData)
{
	I64 animIndex = EngineComponents::GetAnimMgr()->GetAnimDataIndex(pAnimData);
	if (animIndex < 0 || animIndex >= AnimMgr::kMaxAnimData)
		return nullptr;


	I64 frameIndex = GetCurrentFrameNumber();
	RenderFrameParams* pParams = GetRenderFrameParams(frameIndex);

	AnimFrameExecutionData* pFrameData = pParams->m_pAnimFrameExecutionData;
	if (!pFrameData)
		return nullptr;

	ANIM_ASSERT(pFrameData->m_pAnimExecutionContexts);

	bool isAllocated = pFrameData->m_pUsedAnimExecutionContexts->IsBitSet(animIndex);
	return isAllocated ? pFrameData->m_pAnimExecutionContexts + animIndex : nullptr;
}


/// --------------------------------------------------------------------------------------------------------------- ///
const AnimExecutionContext* GetPrevFrameAnimExecutionContext(const FgAnimData* pAnimData)
{
	I64 animIndex = EngineComponents::GetAnimMgr()->GetAnimDataIndex(pAnimData);
	if (animIndex < 0 || animIndex >= AnimMgr::kMaxAnimData)
		return nullptr;

	I64 frameIndex = GetCurrentFrameNumber();
	if (frameIndex == 0)
		return nullptr;

	RenderFrameParams* pPrevParams = GetRenderFrameParams(frameIndex - 1);
	ANIM_ASSERT(pPrevParams);

	AnimFrameExecutionData* pPrevFrameData = pPrevParams->m_pAnimFrameExecutionData;
	ANIM_ASSERT(pPrevFrameData);

	bool isAllocated = pPrevFrameData->m_pUsedAnimExecutionContexts->IsBitSet(animIndex);
	return isAllocated ? pPrevFrameData->m_pAnimExecutionContexts + animIndex : nullptr;
}


