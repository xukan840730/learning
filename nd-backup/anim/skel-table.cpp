/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/skel-table.h"

#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bit-array.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/armik.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/footik.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/retarget-util.h"
#include "ndlib/io/package-mgr.h"
#include "ndlib/memory/allocator-levelmem.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/skel-retarget-defines.h"

#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"

#include <orbisanim/util.h>

/// --------------------------------------------------------------------------------------------------------------- ///
ALIGNED(128) SkelTable::RetargetEntry SkelTable::m_retargetEntryTable[kMaxNumRetargetEntries];
U32 SkelTable::m_numRetargetEntries = 0;

U32 SkelTable::m_actionCounter = 0;

/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
///  retarget mechanism
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ValidateRetarget(const SkelTable::RetargetEntry* pRetargetEntry,
							 const ArtItemSkeleton* pSrcSkel,
							 const ArtItemSkeleton* pDestSkel)
{
	if (FALSE_IN_FINAL_BUILD(!g_animOptions.m_enableValidation))
	{
		return true;
	}

	ScopedTempAllocator alloc(FILE_LINE_FUNC);

	ANIM_ASSERT(pRetargetEntry);
	ANIM_ASSERT(pSrcSkel);
	ANIM_ASSERT(pDestSkel);

	bool valid = true;

	const SkelTable::JointRetarget* pRetargetTable = pRetargetEntry->m_jointRetarget;

	const ndanim::JointHierarchy* pSrcJoints = pSrcSkel->m_pAnimHierarchy;
	const ndanim::JointParams* pSrcDefJoints = ndanim::GetDefaultJointPoseTable(pSrcJoints);
	const ndanim::DebugJointParentInfo* pSrcJointParentInfo = ndanim::GetDebugJointParentInfoTable(pSrcJoints);
	const ndanim::JointHierarchy* pDestJoints = pDestSkel->m_pAnimHierarchy;
	const ndanim::JointParams* pDestDefJoints = ndanim::GetDefaultJointPoseTable(pDestJoints);
	const ndanim::DebugJointParentInfo* pDestJointParentInfo = ndanim::GetDebugJointParentInfoTable(pDestJoints);

	const float kEpsilon = DEGREES_TO_RADIANS(0.001f);

	// Create the bind pose matrices
	const Mat34* pSrcInvBindPoseXforms = ndanim::GetInverseBindPoseTable(pSrcJoints);
	Transform* pBindPoseMats0 = NDI_NEW Transform[pSrcJoints->m_numAnimatedJoints];		// No longer contiguous
	for (U32F i = 0; i < pSrcJoints->m_numAnimatedJoints; ++i)
	{
		const Transform invBindPoseMat = Inverse(Transform(pSrcInvBindPoseXforms[i].GetMat44()));
		pBindPoseMats0[i] = invBindPoseMat;
	}

	const Mat34* pDestInvBindPoseXforms = ndanim::GetInverseBindPoseTable(pDestJoints);
	Transform* pBindPoseMats1 = NDI_NEW Transform[pDestJoints->m_numAnimatedJoints];		// No longer contiguous
	for (U32F i = 0; i < pDestJoints->m_numAnimatedJoints; ++i)
	{
		const Transform invBindPoseMat = Inverse(Transform(pDestInvBindPoseXforms[i].GetMat44()));
		pBindPoseMats1[i] = invBindPoseMat;
	}

	if (pSrcSkel == pDestSkel)
	{
		MsgAnim("\t|- The skeletons are the same.\n");
		valid = false;
	}

	if (pRetargetEntry->m_count == 0)
	{
		MsgAnim("\t|- The table contains no valid retargeted joints.\n");
		valid = false;
	}

	// Check joint compensation rotations
	for (U32F ii = 0; ii < pRetargetEntry->m_count; ++ii)
	{
		U32F srcJointIdx = pRetargetTable[ii].m_srcIndex;
		U32F destJointIdx = pRetargetTable[ii].m_destIndex;

		const ndanim::JointParams& joint0 = pSrcDefJoints[srcJointIdx];
		const ndanim::JointParams& joint1 = pDestDefJoints[destJointIdx];

// 		float angles[3];
// 		const Quat adjustedQuat = joint0.m_quat * pRetargetTable[ii].m_deltaRot;
// 		const Quat diffQuat = Conjugate(adjustedQuat) * joint1.m_quat;
// 		diffQuat.GetEulerAngles(angles[0], angles[1], angles[2]);
// 
// 		for (U32F jj = 0; jj < 3; ++jj)
// 		{
// 			if (fabsf(angles[jj]) > kEpsilon )
// 			{
// 				MsgAnim("\t|- \"%s\" is oriented differently than \"%s\" after compensation.\n",
// 						pSrcSkel->m_pJointDescs[srcJointIdx].m_pName,
// 						pDestSkel->m_pJointDescs[destJointIdx].m_pName);
// 				MsgAnim("\t|\tDegrees(%3.1f, %3.1f, %3.1f)\n",
// 						RADIANS_TO_DEGREES(fabsf(angles[0])),
// 						RADIANS_TO_DEGREES(fabsf(angles[1])),
// 						RADIANS_TO_DEGREES(fabsf(angles[2])));
// 				valid = false;
// 				break;
// 			}
// 		}
// 

		// Check for parent joints that are not retargeted
		bool foundSrcParent = false;
		bool foundDestParent = false;
		I16 srcParentJointIdx = (I16)pSrcJointParentInfo[srcJointIdx].m_parent;
		I16 destParentJointIdx = (I16)pDestJointParentInfo[destJointIdx].m_parent;

		for (U32F jj = 0; jj < pRetargetEntry->m_count && (!foundSrcParent || !foundDestParent); ++jj)
		{
			foundSrcParent = foundSrcParent || srcParentJointIdx == pRetargetTable[jj].m_srcIndex;
			foundDestParent = foundDestParent || destParentJointIdx == pRetargetTable[jj].m_destIndex;
		}

		if (srcParentJointIdx != -1 && !foundSrcParent)
		{
			MsgAnim("\t|- \"%s\" joint \"%s\" is retargeted but the parent joint \"%s\" is not.\n",
					ResourceTable::LookupSkelName(pSrcSkel->m_skelId),
					pSrcSkel->m_pJointDescs[srcJointIdx].m_pName,
					pSrcSkel->m_pJointDescs[srcParentJointIdx].m_pName);
			valid = false;
		}

		if (destParentJointIdx != -1 && !foundDestParent)
		{
			MsgAnim("\t|- \"%s\" joint \"%s\" is retargeted but the parent joint \"%s\" is not.\n",
					ResourceTable::LookupSkelName(pDestSkel->m_skelId),
					pDestSkel->m_pJointDescs[destJointIdx].m_pName,
					pDestSkel->m_pJointDescs[destParentJointIdx].m_pName);
			valid = false;
		}
	}

	if (!valid)
	{
		MsgAnim("---- Validating retarget between \"%s\" and \"%s\" ----\n", 
			ResourceTable::LookupSkelName(pSrcSkel->m_skelId),
			ResourceTable::LookupSkelName(pDestSkel->m_skelId));
		MsgAnim("\tThe skeletons ARE NOT compatible for retargeting.\n");
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SkelTable::PostSyncLoadStartup()
{
	const DC::SkelRetargetTable* pRetargetTable = ScriptManager::Lookup<DC::SkelRetargetTable>(SID("*skel-retarget-tables*"));

	if (pRetargetTable)
	{
		// Skeletons may not be loaded so allocate placeholder entries and call BuildRetargetEntries() in LoginSkeleton()
		for (U32F ii = 0; ii < pRetargetTable->m_count; ++ii)
		{
			AddRetargetEntry(pRetargetTable->m_skelRetarget[ii]);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SkelTable::AddRetargetEntry(const DC::SkelRetarget& skelRetarget)
{
	ANIM_ASSERT(skelRetarget.m_srcSkelId != INVALID_STRING_ID_64);
	ANIM_ASSERT(skelRetarget.m_destSkelId != INVALID_STRING_ID_64);
	ANIM_ASSERTF(m_numRetargetEntries < kMaxNumRetargetEntries,
				 ("AddRetargetEntry: Exceeded %d retargets\n", kMaxNumRetargetEntries));

	RetargetEntry& retargetEntry		= m_retargetEntryTable[m_numRetargetEntries++];
	retargetEntry.m_pSkelRetarget		= &skelRetarget;
	retargetEntry.m_scale				= skelRetarget.m_scale;
	retargetEntry.m_srcSkelId			= INVALID_SKELETON_ID;
	retargetEntry.m_destSkelId			= INVALID_SKELETON_ID;
	retargetEntry.m_jointRetarget		= nullptr;
	retargetEntry.m_count				= (U16)skelRetarget.m_count;
	retargetEntry.m_disabled			= (U16)false;
	retargetEntry.m_dstToSrcSegMapping	= nullptr;
	retargetEntry.m_pArmData			= nullptr;
	retargetEntry.m_pLegData			= nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SkelTable::BuildRetargetEntries(SkeletonId skelId)
{
	ANIM_ASSERT(skelId != INVALID_SKELETON_ID);

	StringId64 skelPkgId = ResourceTable::LookupSkelNameId(skelId);

	if (skelPkgId != INVALID_STRING_ID_64)
	{
		for (U32F ii = 0; ii < m_numRetargetEntries; ++ii)
		{
			ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);

			RetargetEntry& retargetEntry = m_retargetEntryTable[ii];
			const DC::SkelRetarget* pSkelRetarget = retargetEntry.m_pSkelRetarget;
			ANIM_ASSERT(pSkelRetarget);

			// Try to build any entries matching the skeleton
			if (pSkelRetarget->m_srcSkelId == skelPkgId || pSkelRetarget->m_destSkelId == skelPkgId)
			{
				// Get the first skeleton
				const ArtItemSkeleton* pSrcSkel = ResourceTable::LookupSkel(pSkelRetarget->m_srcSkelId).ToArtItem();
				if (!pSrcSkel)
					continue;

				// Get the second skeleton
				const ArtItemSkeleton* pDestSkel = ResourceTable::LookupSkel(pSkelRetarget->m_destSkelId).ToArtItem();
				if (!pDestSkel)
					continue;

				// Check if the retarget pose exist if needed
				const StringId64 poseId = pSkelRetarget->m_poseId;
				const ArtItemAnim* pPoseAnim = nullptr;
				if (poseId != INVALID_STRING_ID_64)
				{
					pPoseAnim = AnimMasterTable::LookupAnim(pDestSkel->m_skelId, pDestSkel->m_hierarchyId, poseId, false).ToArtItem();
				}

				const ndanim::JointHierarchy* pSrcJoints = pSrcSkel->m_pAnimHierarchy;
				const ndanim::JointHierarchy* pDestJoints = pDestSkel->m_pAnimHierarchy;
				const ndanim::JointParams* pSrcDefJoints = reinterpret_cast<const ndanim::JointParams*>(ndanim::GetDefaultJointPoseTable(pSrcJoints));
				const ndanim::JointParams* pDestDefJoints = reinterpret_cast<const ndanim::JointParams*>(ndanim::GetDefaultJointPoseTable(pDestJoints));
				const Mat34* pSrcInvBindPoseXforms = ndanim::GetInverseBindPoseTable(pSrcJoints);
				const Mat34* pDestInvBindPoseXforms = ndanim::GetInverseBindPoseTable(pDestJoints);

				// Pose the receiving skeleton properly before calculating any delta transforms
				if (pPoseAnim)
				{
					Transform* pOutJointTransforms = NDI_NEW (kAlign16) Transform[pDestSkel->m_pAnimHierarchy->m_numTotalJoints];
					ndanim::JointParams* pOutJointParams1 = NDI_NEW(kAlign16) ndanim::JointParams[pDestSkel->m_pAnimHierarchy->m_numTotalJoints];

					if (!AnimateObject(Transform(kIdentity),
									   pDestSkel,
									   pPoseAnim,
									   0.0f,
									   pOutJointTransforms,
									   pOutJointParams1,
									   nullptr,
									   nullptr,
									   AnimateFlags::kAnimateFlag_AllSegments))
					{
						MsgErr("AnimateObject() failed, can't BuildRetargetEntry\n");
						return;
					}

					Mat34* pInvBindPoseXforms1 = NDI_NEW (kAlign16) Mat34[pDestSkel->m_pAnimHierarchy->m_numTotalJoints];
					for (U32F i = 0; i < pDestSkel->m_pAnimHierarchy->m_numTotalJoints; ++i)
					{
						const Mat44 invXform = Inverse(pOutJointTransforms[i]).GetMat44();
						pInvBindPoseXforms1[i] = Mat34(invXform);
					}

					pDestDefJoints = pOutJointParams1;
					pDestInvBindPoseXforms = pInvBindPoseXforms1;

					MsgAnim("Using animation '%s' for retarget entry\n", pPoseAnim->GetName());
				}

				BuildRetargetEntry(&m_retargetEntryTable[ii],
								   pSrcSkel,
								   pDestSkel,
								   pSrcDefJoints,
								   pDestDefJoints,
								   pSrcInvBindPoseXforms,
								   pDestInvBindPoseXforms,
								   pSkelRetarget->m_poseId,
								   pPoseAnim);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ValidateJointParams(const ArtItemSkeleton* pSkel, const ndanim::JointParams* pJoints)
{
	STRIP_IN_FINAL_BUILD;

	for (int i = 0; i < pSkel->m_pAnimHierarchy->m_numAnimatedJoints; i++)
	{ 
		ANIM_ASSERT(IsFinite(pJoints[i].m_trans));
		ANIM_ASSERT(IsFinite(pJoints[i].m_scale));
		ANIM_ASSERT(IsNormal(pJoints[i].m_quat));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SkelTable::BuildRetargetEntry(RetargetEntry* pRetargetEntry,
								   const ArtItemSkeleton* pSrcSkel,
								   const ArtItemSkeleton* pDestSkel,
								   const ndanim::JointParams* pSrcDefJoints,
								   const ndanim::JointParams* pDestDefJoints,
								   const Mat34* pSrcInvBindPoseXforms,
								   const Mat34* pDestInvBindPoseXforms,
								   StringId64 retargetPoseId,
								   const ArtItemAnim* pPoseAnim)
{
	ANIM_ASSERT(pRetargetEntry);
	ANIM_ASSERT(pRetargetEntry->m_pSkelRetarget);
	ANIM_ASSERT(pSrcSkel);
	ANIM_ASSERT(pDestSkel);
	ANIM_ASSERT((pRetargetEntry->m_pSkelRetarget->m_count > 0)
				|| (pRetargetEntry->m_pSkelRetarget->m_count == 0
					&& pSrcSkel->m_numTotalJoints <= kDefaultRetargetJointCount
					&& pDestSkel->m_numTotalJoints <= kDefaultRetargetJointCount));

	//Try to track down a crash where the pose data is garbage
	ValidateJointParams(pSrcSkel, pSrcDefJoints);
	ValidateJointParams(pDestSkel, pDestDefJoints);

	const DC::SkelRetarget* pSkelRetarget = pRetargetEntry->m_pSkelRetarget;

	// Get the first skeleton
	U32F numSrcSegments = pSrcSkel->m_pAnimHierarchy->m_numSegments;
	U32F numTotalSrcJoints = pSrcSkel->m_pAnimHierarchy->m_numTotalJoints;
	const ndanim::JointHierarchy* pSrcJoints = pSrcSkel->m_pAnimHierarchy;
	const ndanim::DebugJointParentInfo* pSrcJointParentInfo = ndanim::GetDebugJointParentInfoTable(pSrcJoints);

	// Get the second skeleton
	U32F numDestSegments = pDestSkel->m_pAnimHierarchy->m_numSegments;
	U32F numTotalDestJoints = pDestSkel->m_pAnimHierarchy->m_numTotalJoints;
	const ndanim::JointHierarchy* pDestJoints = pDestSkel->m_pAnimHierarchy;
	const ndanim::DebugJointParentInfo* pDestJointParentInfo = ndanim::GetDebugJointParentInfoTable(pDestJoints);

	// Update the skeleton ids
	pRetargetEntry->m_srcSkelId = pSrcSkel->m_skelId;
	pRetargetEntry->m_destSkelId = pDestSkel->m_skelId;
	pRetargetEntry->m_srcHierarchyId = pSrcSkel->m_pAnimHierarchy->m_animHierarchyId;
	pRetargetEntry->m_destHierarchyId = pDestSkel->m_pAnimHierarchy->m_animHierarchyId;

	const bool hierarchiesMatch = (pRetargetEntry->m_srcHierarchyId == pRetargetEntry->m_destHierarchyId);
	if (hierarchiesMatch)
	{
		// If the hierarchies match we don't need to actually retarget, we can just animate as if it was our own animation.
		return true;
	}

	if (!pRetargetEntry->m_jointRetarget)
	{
		LevelMemAllocateJanitor jj(EngineComponents::GetPackageMgr()->GetLoginPackage(), FILE_LINE_FUNC);
		const U32F count = pRetargetEntry->m_count ? pRetargetEntry->m_count : kDefaultRetargetJointCount;
		pRetargetEntry->m_jointRetarget = NDI_NEW(kAlign16) JointRetarget[count];
	}

	ANIM_ASSERTF(pRetargetEntry->m_jointRetarget,
				 ("Failed to allocate joint retarget data for retarget entry '%s' -> '%s'\n",
				  ResourceTable::LookupSkelName(pSrcSkel->m_skelId),
				  ResourceTable::LookupSkelName(pDestSkel->m_skelId)));

	if (!pRetargetEntry->m_jointRetarget)
	{
		MsgAnimErr("Failed to allocate joint retarget data for retarget entry '%s' -> '%s'\n",
				   ResourceTable::LookupSkelName(pSrcSkel->m_skelId),
				   ResourceTable::LookupSkelName(pDestSkel->m_skelId));
		return false;
	}

	if (pSkelRetarget->m_poseId != INVALID_STRING_ID_64)
	{
		MsgAnim("Adding a retarget entry for '%s' -> '%s' (retarget pose '%s%s)\n",
				ResourceTable::LookupSkelName(pSrcSkel->m_skelId),
				ResourceTable::LookupSkelName(pDestSkel->m_skelId),
				DevKitOnly_StringIdToStringOrHex(retargetPoseId),
				(pPoseAnim ? "" : " is MISSING!"));
	}
	else
	{
		MsgAnim("Adding a retarget entry for '%s' -> '%s'\n",
				ResourceTable::LookupSkelName(pSrcSkel->m_skelId),
				ResourceTable::LookupSkelName(pDestSkel->m_skelId));
	}

	// If an empty entry is found generate a default entry by matching bone names between the skeletons
	if (pSkelRetarget->m_count == 0)
	{
		U32F count = 0;

		// Output data for skel-retargets.dc
		MsgAnim("\nFound an empty retarget entry in skel-retargets.dc - generating default data:\n\n");
		MsgAnim("    (make-skel-retarget (skel-retarget-mode auto) (net-retarget-mode auto)\n");
		MsgAnim("      (%s -> %s)\n", DevKitOnly_StringIdToString(pSkelRetarget->m_srcSkelId), DevKitOnly_StringIdToString(pSkelRetarget->m_destSkelId));
		MsgAnim("      1.0\n");
		MsgAnim("      (\n");

		for (U32F ii = 0; ii < numTotalSrcJoints; ++ii)
		{
			// Skip procedural joints. We can't retarget those.
			if (!ndanim::IsJointAnimated(pSrcJoints, ii))
				continue;

			for (U32F jj = 0; jj < numTotalDestJoints; ++jj)
			{
				// Skip procedural joints. We can't retarget those.
				if (!ndanim::IsJointAnimated(pDestJoints, jj))
					continue;

				if (pSrcSkel->m_pJointDescs[ii].m_nameId == pDestSkel->m_pJointDescs[jj].m_nameId)
				{
					ANIM_ASSERTF(count < kDefaultRetargetJointCount, ("Exceeded default retarget joint count"));

					// Output data for skel-retargets.dc
					MsgAnim("        (%s", pSrcSkel->m_pJointDescs[ii].m_pName);
					U32F numTabs = 9 - (strlen(pSrcSkel->m_pJointDescs[ii].m_pName) + 9) / 4;
					for (U32F kk = 0; kk < numTabs; ++kk)
						MsgAnim("\t");
					MsgAnim("->\t\t%s\tdefault)\n", pDestSkel->m_pJointDescs[jj].m_pName);

					JointRetarget& jointRetarget = pRetargetEntry->m_jointRetarget[count];

					const I16 srcIndex = (I16)ii;
					const I16 destIndex = (I16)jj;

					jointRetarget.m_srcIndex = srcIndex;	
					jointRetarget.m_destIndex = destIndex;
					jointRetarget.m_srcAnimIndex = OrbisAnim::AnimHierarchy_OutputJointIndexToAnimatedJointIndex(pSrcJoints, jointRetarget.m_srcIndex);
					jointRetarget.m_destAnimIndex = OrbisAnim::AnimHierarchy_OutputJointIndexToAnimatedJointIndex(pDestJoints, jointRetarget.m_destIndex);
					jointRetarget.m_retargetMode = pSkelRetarget->m_overrideBoneMode;

					// Calculate bind pose compensation transforms
					{
						const I32 srcParentIndex = pSrcJointParentInfo[srcIndex].m_parent;
						const I32 destParentIndex = pDestJointParentInfo[destIndex].m_parent;

						{
							const Transform invBindPoseMat0 = Inverse(Transform(pSrcInvBindPoseXforms[srcParentIndex].GetMat44()));
							const Transform invBindPoseMat1 = Inverse(Transform(pDestInvBindPoseXforms[destParentIndex].GetMat44()));

							const Locator bindPoseParentDeltaLoc = (srcParentIndex >= 0) ? Locator(invBindPoseMat0).UntransformLocator(Locator(invBindPoseMat1)) : Locator(kIdentity);
							jointRetarget.m_parentBindPoseDelta.AsLocator(bindPoseParentDeltaLoc);
						}

						{
							const Transform invBindPoseMat0 = Inverse(Transform(pSrcInvBindPoseXforms[srcIndex].GetMat44()));
							const Transform invBindPoseMat1 = Inverse(Transform(pDestInvBindPoseXforms[destIndex].GetMat44()));

							const Locator bindPoseDeltaLoc = Locator(invBindPoseMat0).UntransformLocator(Locator(invBindPoseMat1));
							jointRetarget.m_jointBindPoseDelta.AsLocator(bindPoseDeltaLoc);
						}
					}

					ResolveValidBitMapping(jointRetarget, pSrcSkel, pDestSkel);

					// Calculate the bone length compensate
					{
						const float joint0boneLength = Length(pSrcDefJoints[jointRetarget.m_srcAnimIndex].m_trans);
						const float joint1boneLength = Length(pDestDefJoints[jointRetarget.m_destAnimIndex].m_trans);
						const float boneScale = (joint0boneLength > 0.00001f && joint1boneLength > 0.00001f) ? joint1boneLength / joint0boneLength : 1.0f;
						ANIM_ASSERT(IsFinite(boneScale));
						jointRetarget.m_boneScale = MakeF16(boneScale);
						const F32 testVal = MakeF32(jointRetarget.m_boneScale);
						ANIM_ASSERT(IsFinite(testVal));
					}

					count++;
					break;
				}
			}
		}

		// Output data for skel-retargets.dc
		MsgAnim("      )\n");
		MsgAnim("    )\n\n");

		pRetargetEntry->m_count = (U16)count;
	}
	else
	{
		// Build index map from the bone in the first skeleton to the bones in the second
		I32F iRetargetEntry = 0;
		for (U32F ii = 0; ii < pSkelRetarget->m_count; ++ii)
		{
			const DC::BoneRetarget& boneRetarget = pSkelRetarget->m_boneRetarget[ii];

			I16 srcIndex = -1;
			for (U32F jj = 0; jj < numTotalSrcJoints; ++jj)
			{
				if (pSrcSkel->m_pJointDescs[jj].m_nameId == boneRetarget.m_srcBoneId)
				{
					srcIndex = (I16)jj;
					break;
				}
			}

			if (srcIndex == -1)
			{
				if (g_animOptions.m_showRetargetingWarnings)
				{
					MsgAnimWarn("Problem retargeting skel '%s' to '%s': joint \"%s\" does not exist in source skeleton. Please fix in skel-retargets.dc.\n",
								DevKitOnly_StringIdToString(pSkelRetarget->m_srcSkelId),
								DevKitOnly_StringIdToString(pSkelRetarget->m_destSkelId),
								DevKitOnly_StringIdToString(boneRetarget.m_srcBoneId));
				}
//				pRetargetEntry->m_disabled = true;
			}

			if (!ndanim::IsJointAnimated(pSrcJoints, srcIndex))
			{
				if (g_animOptions.m_showRetargetingWarnings)
				{
					MsgAnimWarn("Problem retargeting skel '%s' to '%s': joint \"%s\" is not an animated joint. Please remove it from skel-retargets.dc.\n",
								DevKitOnly_StringIdToString(pSkelRetarget->m_srcSkelId),
								DevKitOnly_StringIdToString(pSkelRetarget->m_destSkelId),
								DevKitOnly_StringIdToString(boneRetarget.m_srcBoneId));
				}
				srcIndex = -1;
//				pRetargetEntry->m_disabled = true;
			}

			I16 destIndex = -1;
			for (U32F jj = 0; jj < numTotalDestJoints; ++jj)
			{
				if (pDestSkel->m_pJointDescs[jj].m_nameId == boneRetarget.m_destBoneId)
				{
					destIndex = (I16)jj;
					break;
				}
			}

			if (destIndex == -1)
			{
				if (g_animOptions.m_showRetargetingWarnings)
				{
					MsgAnimWarn("Problem retargeting skel '%s' to '%s': joint \"%s\" does not exist in dest skeleton. Please fix in skel-retargets.dc.\n",
								DevKitOnly_StringIdToString(pSkelRetarget->m_srcSkelId),
								DevKitOnly_StringIdToString(pSkelRetarget->m_destSkelId),
								DevKitOnly_StringIdToString(boneRetarget.m_destBoneId));
				}
//				pRetargetEntry->m_disabled = true;
			}

			if (!ndanim::IsJointAnimated(pDestJoints, destIndex))
			{
				if (g_animOptions.m_showRetargetingWarnings)
				{
					MsgAnimWarn("Problem retargeting skel '%s' to '%s': joint \"%s\" does not exist in dest skeleton. Please fix in skel-retargets.dc.\n",
								DevKitOnly_StringIdToString(pSkelRetarget->m_srcSkelId),
								DevKitOnly_StringIdToString(pSkelRetarget->m_destSkelId),
								DevKitOnly_StringIdToString(boneRetarget.m_destBoneId));
				}
				destIndex = -1;
//				pRetargetEntry->m_disabled = true;
			}

			//Ignore this entry if one of the joints is missing
			if (destIndex == -1 || srcIndex == -1)
			{				
				continue;
			}

			JointRetarget& jointRetarget = pRetargetEntry->m_jointRetarget[iRetargetEntry];
			jointRetarget.m_srcIndex = srcIndex;
			jointRetarget.m_destIndex = destIndex;
			jointRetarget.m_srcAnimIndex = OrbisAnim::AnimHierarchy_OutputJointIndexToAnimatedJointIndex(pSrcJoints, jointRetarget.m_srcIndex);
			jointRetarget.m_destAnimIndex = OrbisAnim::AnimHierarchy_OutputJointIndexToAnimatedJointIndex(pDestJoints, jointRetarget.m_destIndex);
			jointRetarget.m_retargetMode = (pSkelRetarget->m_overrideBoneMode != DC::kBoneRetargetModeDefault) ? pSkelRetarget->m_overrideBoneMode : boneRetarget.m_mode;
			jointRetarget.m_pRegression = boneRetarget.m_regression;
			jointRetarget.m_srcJointPosLs.AsPoint(pSrcDefJoints[srcIndex].m_trans);

			ResolveValidBitMapping(jointRetarget, pSrcSkel, pDestSkel);

			iRetargetEntry++;
								
			// Calculate bind pose compensation transforms
			const I32 srcParentIndex = pSrcJointParentInfo[srcIndex].m_parent;
			const I32 destParentIndex = pDestJointParentInfo[destIndex].m_parent;

// 			{
// 				const Transform srcParentBindPoseMat = Inverse(Transform(pSrcInvBindPoseXforms[srcParentIndex].GetMat44()));
// 				const Transform destParentBindPoseMat = Inverse(Transform(pDestInvBindPoseXforms[destParentIndex].GetMat44()));
// 
// 				const Locator bindPoseParentDeltaLoc = (srcParentIndex >= 0) ? Locator(srcParentBindPoseMat).UntransformLocator(Locator(destParentBindPoseMat)) : Locator(kIdentity);
// 				jointRetarget.m_parentBindPoseDelta = bindPoseParentDeltaLoc;
// 			}
// 
// 			{
// 				Transform srcBindPoseMat = Inverse(Transform(pSrcInvBindPoseXforms[srcIndex].GetMat44()));
// 				Transform destBindPoseMat = Inverse(Transform(pDestInvBindPoseXforms[destIndex].GetMat44()));
// 
// 				if (srcIndex > 0 && destIndex > 0)
// 				{
// 					const Transform srcParentBindPoseMat = Inverse(Transform(pSrcInvBindPoseXforms[srcParentIndex].GetMat44()));
// 					const Transform destParentBindPoseMat = Inverse(Transform(pDestInvBindPoseXforms[destParentIndex].GetMat44()));
// 					srcBindPoseMat.SetTranslation(kOrigin + (srcBindPoseMat.GetTranslation() - srcParentBindPoseMat.GetTranslation()));
// 					destBindPoseMat.SetTranslation(kOrigin + (destBindPoseMat.GetTranslation() - destParentBindPoseMat.GetTranslation()));
// 				}
// 
// 				const Locator bindPoseDeltaLoc = Locator(srcBindPoseMat).UntransformLocator(Locator(destBindPoseMat));
// 				jointRetarget.m_srcJointBindPose = Locator(srcBindPoseMat);
// 				jointRetarget.m_jointBindPoseDelta = bindPoseDeltaLoc;
// 			}

			{
				const Transform srcInvBindPoseMat = Transform(pSrcInvBindPoseXforms[srcParentIndex].GetMat44());
				const Transform dstInvBindPoseMat = destParentIndex >= 0 ? Transform(pDestInvBindPoseXforms[destParentIndex].GetMat44()) : Transform(kIdentity);

				const Transform srcBindPoseMat = Inverse(srcInvBindPoseMat);
				const Transform destBindPoseMat = destParentIndex >= 0 ? Inverse(dstInvBindPoseMat) : Transform(kIdentity);

				const Locator bindPoseParentDeltaLoc = (srcParentIndex >= 0)
														   ? Locator(srcBindPoseMat).UntransformLocator(Locator(destBindPoseMat))
														   : Locator(kIdentity);

				ANIM_ASSERT(IsReasonable(bindPoseParentDeltaLoc));

				jointRetarget.m_parentBindPoseDelta.AsLocator(bindPoseParentDeltaLoc);
			}

			{
				const Transform srcBindPoseMat = Inverse(Transform(pSrcInvBindPoseXforms[srcIndex].GetMat44()));
				const Transform destBindPoseMat = Inverse(Transform(pDestInvBindPoseXforms[destIndex].GetMat44()));

				const Locator bindPoseDeltaLoc = Locator(srcBindPoseMat).UntransformLocator(Locator(destBindPoseMat));

				ANIM_ASSERT(IsReasonable(bindPoseDeltaLoc));

				jointRetarget.m_jointBindPoseDelta.AsLocator(bindPoseDeltaLoc);
			}

			// Calculate the bone length compensate
			{
				const float srcJointBoneLength = Length(pSrcDefJoints[jointRetarget.m_srcAnimIndex].m_trans);
				const float destJointBoneLength = Length(pDestDefJoints[jointRetarget.m_destAnimIndex].m_trans);
				const float boneScale = (srcJointBoneLength > 0.00001f && destJointBoneLength > 0.00001f) ? destJointBoneLength / srcJointBoneLength : 1.0f;
				ANIM_ASSERT(IsReasonable(boneScale));
				jointRetarget.m_boneScale = MakeF16(boneScale);
				const F32 testVal = MakeF32(jointRetarget.m_boneScale);
				ANIM_ASSERT(IsReasonable(testVal));
			}						
		}

		pRetargetEntry->m_count = (I16)iRetargetEntry;
	}

	if (pRetargetEntry->m_pSkelRetarget->m_armIk)
	{
		LevelMemAllocateJanitor jj(EngineComponents::GetPackageMgr()->GetLoginPackage(), FILE_LINE_FUNC);

		ArmRetargetData* pArmData = NDI_NEW ArmRetargetData();
		for (int arm = 0; arm < kArmCount; arm++)
		{
			pArmData->m_apSrcIkChains[arm] = NDI_NEW ArmIkChain();
			pArmData->m_apSrcIkChains[arm]->Init(pSrcSkel, static_cast<ArmIndex>(arm), true);
			pArmData->m_apTgtIkChains[arm] = NDI_NEW ArmIkChain();
			pArmData->m_apTgtIkChains[arm]->Init(pDestSkel, static_cast<ArmIndex>(arm), true);

			OrbisAnim::ValidBits dummyBits;
			dummyBits.SetAllBits();

			pArmData->m_apSrcIkChains[arm]->ReadFromJointParams(pSrcDefJoints, 0, numTotalSrcJoints, 1.0f, &dummyBits);
			pArmData->m_apTgtIkChains[arm]->ReadFromJointParams(pDestDefJoints, 0, numTotalDestJoints, 1.0f, &dummyBits);

			Locator wristLocSrc = pArmData->m_apSrcIkChains[arm]->GetWristLocWs();
			Locator wristLocDest = pArmData->m_apTgtIkChains[arm]->GetWristLocWs();

			pArmData->m_aSrcToTargetRotationOffsets[arm] = wristLocSrc.UntransformLocator(wristLocDest).GetRotation();

			pArmData->m_apSrcIkChains[arm]->DiscardJointCache();
			pArmData->m_apTgtIkChains[arm]->DiscardJointCache();
		}
		pRetargetEntry->m_pArmData = pArmData;
	}

	if (pRetargetEntry->m_pSkelRetarget->m_legIk)
	{
		LevelMemAllocateJanitor jj(EngineComponents::GetPackageMgr()->GetLoginPackage(), FILE_LINE_FUNC);

		LegRetargetData* pLegData = NDI_NEW LegRetargetData();
		for (int leg = 0; leg < kLegCount; leg++)
		{
			pLegData->m_apSrcIkChains[leg] = NDI_NEW LegIkChain();
			pLegData->m_apSrcIkChains[leg]->Init(pSrcSkel, kFootIkCharacterTypeHuman, static_cast<LegIndex>(leg), true);
			pLegData->m_apTgtIkChains[leg] = NDI_NEW LegIkChain();
			pLegData->m_apTgtIkChains[leg]->Init(pDestSkel, kFootIkCharacterTypeHuman, static_cast<LegIndex>(leg), true);

			OrbisAnim::ValidBits dummyBits;
			dummyBits.SetAllBits();

			pLegData->m_apSrcIkChains[leg]->ReadFromJointParams(pSrcDefJoints, 0, numTotalSrcJoints, 1.0f, &dummyBits);
			pLegData->m_apTgtIkChains[leg]->ReadFromJointParams(pDestDefJoints, 0, numTotalDestJoints, 1.0f, &dummyBits);

			Locator ankleLocSrc = pLegData->m_apSrcIkChains[leg]->GetAnkleLocWs();
			Locator ankleLocDest = pLegData->m_apTgtIkChains[leg]->GetAnkleLocWs();

			Locator ankleSrcScaled(AsPoint(AsVector(ankleLocSrc.GetTranslation())*pRetargetEntry->m_pSkelRetarget->m_scale), ankleLocSrc.GetRotation());

			pLegData->m_aSrcToTargetOffsets[leg] = ankleSrcScaled.UntransformLocator(ankleLocDest);

			pLegData->m_apSrcIkChains[leg]->DiscardJointCache();
			pLegData->m_apTgtIkChains[leg]->DiscardJointCache();
		}
		pRetargetEntry->m_pLegData = pLegData;
	}
	GenerateFloatChannelMapping(pRetargetEntry, pSrcSkel, pDestSkel);

	GenerateSegmentMapping(pRetargetEntry, pSrcSkel, pDestSkel);

	ValidateRetarget(pRetargetEntry, pSrcSkel, pDestSkel);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SkelTable::GenerateSegmentMapping(RetargetEntry* pRetargetEntry,
									   const ArtItemSkeleton* pSrcSkel,
									   const ArtItemSkeleton* pDestSkel)
{
	if (!pRetargetEntry || !pSrcSkel || !pDestSkel)
		return;

	Package* pLoginPackage = EngineComponents::GetPackageMgr()->GetLoginPackage();
	if (!pLoginPackage)
		return;

	LevelMemAllocateJanitor jj(pLoginPackage, FILE_LINE_FUNC);

	const U32F numSrcSegments = pSrcSkel->m_pAnimHierarchy->m_numSegments;
	const U32F numDestSegments = pDestSkel->m_pAnimHierarchy->m_numSegments;

	if (!pRetargetEntry->m_dstToSrcSegMapping)
	{
		pRetargetEntry->m_dstToSrcSegMapping = NDI_NEW(kAlign16) ExternalBitArray[numDestSegments];
		const size_t alignedBitSize = AlignSize(ExternalBitArray::DetermineCapacity(numSrcSegments), kAlign64);

		for (U32F iDestSeg = 0; iDestSeg < numDestSegments; ++iDestSeg)
		{
			U64* pBits = NDI_NEW(kAlign64) U64[alignedBitSize / sizeof(U64)];
			pRetargetEntry->m_dstToSrcSegMapping[iDestSeg].Init(alignedBitSize, pBits);
		}
	}

	if (!pRetargetEntry->m_dstToSrcSegMapping)
		return;
	
	for (U32F iRetargetEntry = 0; iRetargetEntry < pRetargetEntry->m_count; ++iRetargetEntry)
	{
		const JointRetarget& jointRetarget = pRetargetEntry->m_jointRetarget[iRetargetEntry];
		
		const I32F iSourceSeg = ndanim::GetSegmentForOutputJoint(pSrcSkel->m_pAnimHierarchy, jointRetarget.m_srcIndex);
		const I32F iDestSeg = ndanim::GetSegmentForOutputJoint(pDestSkel->m_pAnimHierarchy, jointRetarget.m_destIndex);

		if ((iSourceSeg < 0) || (iSourceSeg >= numSrcSegments))
			continue;

		if ((iDestSeg < 0) || (iDestSeg >= numDestSegments))
			continue;

		pRetargetEntry->m_dstToSrcSegMapping[iDestSeg].SetBit(iSourceSeg);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SkelTable::InvalidateRetargetEntries(SkeletonId skelId)
{
	for (U32F ii = 0; ii < m_numRetargetEntries; ++ii)
	{
		RetargetEntry& retargetEntry = m_retargetEntryTable[ii];

		if (retargetEntry.m_srcSkelId == skelId || retargetEntry.m_destSkelId == skelId)
		{
			retargetEntry.m_srcSkelId = INVALID_SKELETON_ID;
			retargetEntry.m_destSkelId = INVALID_SKELETON_ID;
			retargetEntry.m_jointRetarget = nullptr;
			retargetEntry.m_dstToSrcSegMapping = nullptr;
			retargetEntry.m_pArmData = nullptr;
			retargetEntry.m_pLegData = nullptr;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SkelTable::DebugPrint()
{
	STRIP_IN_FINAL_BUILD;
	
	MsgCon("-------------------------------------------------------\n");
	MsgCon("Skel Table:\n");


// 	U64 bitIndex = m_usedEntries.FindFirstSetBit();
// 	while (bitIndex < kMaxNumEntries)
// 	{
// 		SkelEntry& entry = m_table[bitIndex];
// 
// 		const SkeletonId skelId = entry.m_pSkel ? entry.m_pSkel->m_skelId : INVALID_SKELETON_ID;
// 		const U32 hierId = (entry.m_pSkel && entry.m_pSkel->m_pAnimHierarchy) ? entry.m_pSkel->m_pAnimHierarchy->m_animHierarchyId : -1;
// 		const U32 numAnim = (entry.m_pSkel && entry.m_pSkel->m_pAnimHierarchy) ? entry.m_pSkel->m_pAnimHierarchy->m_numAnimatedJoints : 0;
// 		const U32 numTotal = (entry.m_pSkel && entry.m_pSkel->m_pAnimHierarchy) ? entry.m_pSkel->m_pAnimHierarchy->m_numTotalJoints : 0;
// 
// 		MsgCon(" %s - skelId: 0x%.8x heirId: 0x%.8x numAnim: %d numTotal: %d\n",
// 			entry.m_pPackageName,
// 			skelId,
// 			hierId,
// 			numAnim,
// 			numTotal);
// 
// 		bitIndex = m_usedEntries.FindNextSetBit(bitIndex);
// 	}

	MsgCon("-------------------------------------------------------\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
const SkelTable::RetargetEntry* SkelTable::LookupRetarget(SkeletonId srcSkelId, SkeletonId dstSkelId)
{
	for (U32F ii = 0; ii < m_numRetargetEntries; ++ii)
	{
		const RetargetEntry& retargetEntry = m_retargetEntryTable[ii];

		if (retargetEntry.m_srcSkelId == srcSkelId && retargetEntry.m_destSkelId == dstSkelId)
		{
			return &retargetEntry;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
const SkelTable::JointRetarget* SkelTable::FindJointRetarget(const RetargetEntry* pRetarget, I32F iSrcJoint)
{
	if (!pRetarget)
		return nullptr;

	for (U32F i = 0; i < pRetarget->m_count; ++i)
	{
		if (pRetarget->m_jointRetarget[i].m_srcIndex == iSrcJoint)
		{
			return &pRetarget->m_jointRetarget[i];
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SkelTable::EnableRetarget(StringId64 srcSkel, StringId64 dstSkel)
{
	const ArtItemSkeleton* pSrcSkel = ResourceTable::LookupSkel(srcSkel).ToArtItem();
	const ArtItemSkeleton* pDstSkel = ResourceTable::LookupSkel(dstSkel).ToArtItem();
	if (pSrcSkel && pDstSkel)
	{
		const RetargetEntry* pEntry = LookupRetarget(pSrcSkel->m_skelId, pDstSkel->m_skelId);
		if (pEntry)
		{
			pEntry->m_disabled = (U16)false;
			return true;
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SkelTable::DisableRetarget(StringId64 srcSkel, StringId64 dstSkel)
{
	const ArtItemSkeleton* pSrcSkel = ResourceTable::LookupSkel(srcSkel).ToArtItem();
	const ArtItemSkeleton* pDstSkel = ResourceTable::LookupSkel(dstSkel).ToArtItem();
	if (pSrcSkel && pDstSkel)
	{
		const RetargetEntry* pEntry = LookupRetarget(pSrcSkel->m_skelId, pDstSkel->m_skelId);
		if (pEntry)
		{
			pEntry->m_disabled = (U16)true;
			return true;
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SkelTable::ForAllMatchingRetargetEntries(SkeletonId skelId,
											  U32 hierarchyId,
											  RetargetEntryFunctor funcPtr,
											  void* data)
{
	for (U32F ii = 0; ii < m_numRetargetEntries; ++ii)
	{
		const RetargetEntry& retargetEntry = m_retargetEntryTable[ii];

		if (!retargetEntry.m_disabled && (retargetEntry.m_srcSkelId == skelId || retargetEntry.m_destSkelId == skelId))
		{
			funcPtr(skelId, hierarchyId, &retargetEntry, data);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static int FindFloatIndex(const ArtItemSkeleton* pSkel, StringId64 channelId)
{
	for (int i = 0; i < pSkel->m_pAnimHierarchy->m_numFloatChannels; i++)
	{
		if (pSkel->m_pFloatDescs[i].m_nameId == channelId)
		{
			return i;
		}
	}
	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SkelTable::GenerateFloatChannelMapping(RetargetEntry* pEntry,
											const ArtItemSkeleton* pSrcSkel,
											const ArtItemSkeleton* pDstSkel)
{
	if (pEntry->m_pSkelRetarget->m_floatChannelRetarget == nullptr
		|| pEntry->m_pSkelRetarget->m_floatChannelRetarget->GetSize() <= 0)
	{
		return;
	}

	LevelMemAllocateJanitor jj(EngineComponents::GetPackageMgr()->GetLoginPackage(), FILE_LINE_FUNC);
	pEntry->m_floatRetargetList.Init(pEntry->m_pSkelRetarget->m_floatChannelRetarget->GetSize(), FILE_LINE_FUNC);

	for (const DC::FloatChannelRetarget* pRetarget : *pEntry->m_pSkelRetarget->m_floatChannelRetarget)
	{
		int srcIndex = FindFloatIndex(pSrcSkel, pRetarget->m_srcChannelId);
		int destIndex = FindFloatIndex(pDstSkel, pRetarget->m_destChannelId);
		if (srcIndex >= 0 && destIndex >= 0)
		{
			FloatChannelRetarget retarget;
			retarget.m_srcIndex = srcIndex;
			retarget.m_destIndex = destIndex;
			pEntry->m_floatRetargetList.push_back(retarget);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SkelTable::ArmRetargetData::ArmRetargetData()
{
	m_apSrcIkChains[0] = m_apSrcIkChains[1] = nullptr;
	m_apTgtIkChains[0] = m_apTgtIkChains[1] = nullptr;
	m_aSrcToTargetRotationOffsets[0] = m_aSrcToTargetRotationOffsets[1] = Quat(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SkelTable::LegRetargetData::LegRetargetData()
{
	m_apSrcIkChains[0] = m_apSrcIkChains[1] = nullptr;
	m_apTgtIkChains[0] = m_apTgtIkChains[1] = nullptr;
	m_aSrcToTargetOffsets[0] = m_aSrcToTargetOffsets[1] = Locator(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void SkelTable::ResolveValidBitMapping(JointRetarget& jointRetarget,
									   const ArtItemSkeleton* pSrcSkel,
									   const ArtItemSkeleton* pDestSkel)
{
	const U32F srcNumProcessingGroups = OrbisAnim::AnimHierarchy_GetNumProcessingGroups(pSrcSkel->m_pAnimHierarchy);
	const ndanim::ProcessingGroup* pSrcProcGroups = ndanim::GetProcessingGroupTable(pSrcSkel->m_pAnimHierarchy);

	const U32F dstNumProcessingGroups = OrbisAnim::AnimHierarchy_GetNumProcessingGroups(pDestSkel->m_pAnimHierarchy);
	const ndanim::ProcessingGroup* pDstProcGroups = ndanim::GetProcessingGroupTable(pDestSkel->m_pAnimHierarchy);

	U32 srcChanGroup = 0;
	U32 srcBit = 0;
	const bool foundSrc = ResolveJointIndexIntoValidBit(pSrcSkel->m_pAnimHierarchy,
														srcNumProcessingGroups,
														pSrcProcGroups,
														jointRetarget.m_srcIndex,
														&srcChanGroup,
														&srcBit);
	ANIM_ASSERT(foundSrc);
	ANIM_ASSERT(srcBit < 128);

	if (foundSrc)
	{
		jointRetarget.m_srcChannelGroup = srcChanGroup;
		jointRetarget.m_srcValidBit = srcBit;
	}
	else
	{
		jointRetarget.m_srcChannelGroup = -1;
		jointRetarget.m_srcValidBit = 0;
	}

	U32 dstChanGroup = 0;
	U32 dstBit = 0;
	const bool foundDst = ResolveJointIndexIntoValidBit(pDestSkel->m_pAnimHierarchy,
														dstNumProcessingGroups,
														pDstProcGroups,
														jointRetarget.m_destIndex,
														&dstChanGroup,
														&dstBit);

	ANIM_ASSERT(foundDst);
	ANIM_ASSERT(dstBit < 128);

	if (foundDst)
	{
		jointRetarget.m_dstChannelGroup = dstChanGroup;
		jointRetarget.m_dstValidBit = dstBit;
	}
	else
	{
		jointRetarget.m_dstChannelGroup = -1;
		jointRetarget.m_dstValidBit = 0;
	}
}
