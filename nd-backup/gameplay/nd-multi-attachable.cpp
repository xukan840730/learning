/*
 * Copyright (c) 2009 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#include "gamelib/gameplay/nd-multi-attachable.h"

#include "corelib/util/msg.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/fx/fxmgr.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/interface/fg-instance.h"
#include "ndlib/script/script-manager.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/scriptx/h/character-fx-defines.h"
#include "gamelib/scriptx/h/look2-defines.h"
#include "gamelib/scriptx/h/nd-multi-attachable-defines.h"

bool g_disableSkinnedMultiAttachables = false;

PROCESS_REGISTER(NdMultiAttachable, NdAttachableObject);

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsParentedToRoot(I32F iJoint, const ArtItemSkeleton* pSkel)
{
	I32F iCur = iJoint;
	do 
	{
		if (iCur == 0)
			return true;
		else if (iCur < 0)
			return false;

		iCur = ndanim::GetParentJoint(pSkel->m_pAnimHierarchy, iCur);
	} while (true);

	ANIM_ASSERT(false);
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdMultiAttachable::NdMultiAttachable()
	: m_aSourceJointIndex(nullptr)
	, m_aSourceOutputControlIndex(nullptr)
	, m_parentJointSegmentMask(0)
	, m_pJointTree(nullptr)
	, m_forceCopyParentJoints(false)
	, m_pJointOffsets(nullptr)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdMultiAttachable::~NdMultiAttachable()
{
	NDI_DELETE m_pJointTree;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdMultiAttachable::Init(const ProcessSpawnInfo& info)
{
	m_forceCopyParentJoints = false;
	m_aSourceJointIndex = nullptr;
	m_aSourceOutputControlIndex = nullptr;
	m_parentJointSegmentMask = 0;

	Err result = ParentClass::Init(info);

	if (result.Failed())
		return result;

	const NdAttachableInfo* pAttachInfo = static_cast<const NdAttachableInfo*>(info.m_pUserData);

	if (!BuildSourceJointIndexTable(GetParentGameObject(), pAttachInfo->m_jointSuffix))
	{
		return Err::kErrAnimActorLoginFailed;
	}

	if (m_aSourceJointIndex)
	{
		ChangeAnimConfig(FgAnimData::kAnimConfigNoAnimationForCloth);

		UpdateRoot();
	}

	GAMEPLAY_ASSERTF(EngineComponents::GetProcessMgr()->GetProcessBucket(*this) > EngineComponents::GetProcessMgr()->GetProcessBucket(*GetParentGameObject()), 
		("Multiattachable %s (bucket %s) trying to parent to %s (bucket %s)\n", DevKitOnly_StringIdToString(GetUserId()), GetProcessBucketName(EngineComponents::GetProcessMgr()->GetProcessBucket(*this)),
			DevKitOnly_StringIdToString(GetParentGameObject()->GetUserId()), GetProcessBucketName(EngineComponents::GetProcessMgr()->GetProcessBucket(*GetParentGameObject()))));
			
	// Make sure mesh ray casts are not done against my PmInstance.
	if (IDrawControl* pDrawControl = GetDrawControl())
	{
		pDrawControl->SetInstanceFlag(FgInstance::kDisableMeshRayCasts | FgInstance::kNoProjection);
	}

	GetAnimControl()->EnableAsyncUpdate(FgAnimData::kEnableAsyncPostAnimBlending);

	// @ASYNC
	SetAllowThreadedUpdate(true);

	// Opt out from this late optimization
	m_simpleAttachableAllowDisableAnimation = false;

	m_isGoreHeadProp = pAttachInfo->m_isGoreHeadProp;

	if (m_isGoreHeadProp)
	{
		AnimControl* pAnimControl = GetAnimControl();
		const ArtItemSkeleton* pSkel = pAnimControl->GetArtItemSkel();
		StringId64* skelJointNames = NDI_NEW StringId64[pSkel->m_numAnimatedGameplayJoints];
	
		U32F numSkelJoints = 0;

		for (int iJoint = 0; iJoint < pSkel->m_numAnimatedGameplayJoints; iJoint++)
		{
			const StringId64 jointSid = pAnimControl->GetJointSid(iJoint);

			if (!IsParentedToRoot(iJoint, pSkel))
			{
				MsgWarn("[%s] not including joint %s (%d) in joint tree because it's not parented to the root. Eli, is this okay? (skel: %s)\n",
						GetName(),
						DevKitOnly_StringIdToString(jointSid),
						iJoint,
						ResourceTable::LookupSkelName(pSkel->m_skelId));
				continue;
			}

			skelJointNames[numSkelJoints++] = jointSid;
		}

		if (!m_pJointTree)
		{
			m_pJointTree = NDI_NEW JointTree();
		}

		m_pJointTree->Init(this, skelJointNames[0], false, numSkelJoints, skelJointNames);
		m_pJointTree->ReadJointCache();
	}
	else
	{
		m_pJointTree = nullptr;
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdMultiAttachable::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	RelocatePointer(m_aSourceJointIndex, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_aSourceOutputControlIndex, deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pJointTree, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdMultiAttachable::PostAnimBlending_Async()
{
	PROFILE(Collision, NdMultiAttachable_PAB);

	ParentClass::PostAnimBlending_Async();

	// NB: in U3 this was done in PostAnimUpdate_Async(), but that does not work for T1 -- must do it here
	if (m_pAttachmentCtrl->IsAttached() || m_fakeAttached || m_forceCopyParentJoints)
	{
		CopyJointsFromParent();
		if (m_isGoreHeadProp)
		{
			ApplyGoreHeadPropOffsets();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdMultiAttachable::EventHandler(Event& event)
{
	if (event.GetMessage() == SID("kill-attachable"))
	{
		// This event is used by task-misc.cpp to kill irrelevant processes that otherwise
		// cause problems when levels unload prior to playing movies.
		KillProcess(this);
	}
	ParentClass::EventHandler(event);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdMultiAttachable::ApplyGoreHeadPropOffsets()
{
	ScopedTempAllocator alloc{FILE_LINE_FUNC};
	ASSERT(m_pJointTree); // does this need contour matching?
	if (!m_pJointTree || !m_pJointTree->IsValid() || !m_pJointOffsets || m_pJointOffsets->m_count < 1)
	{
		return;
	}

	m_pJointTree->ReadJointCache();
	for (int i=0; i < m_pJointOffsets->m_count; i++)
	{
		// unless we make a copy of the offsets and sort it
		// either we do O(n^2) accesses on the offset data
		// or we do O(n^2) accesses on the JointTree
		// if this becomes a problem we'll want to allocate the data for the offsets
		// then have their access order match the access order of joint offsets in m_pJointTree
		// first lets make sure it works
		// If we decide to go the allocation route and trade memory for performance
		// we can set and allocate init using SpawnParams, or add the alloc/dealloc/sort logic
		// to SetJointOffsets. Note that the DcHashTable used has INVALID_STRING_ID_64
		StringId64 key = m_pJointOffsets->m_keys[i];
		if (key != INVALID_STRING_ID_64) {
			int jointTreeIndexOffset = m_pJointTree->FindJointOffset(key);

			if (jointTreeIndexOffset < JointSet::kStartJointOffset)
			{
				//MsgErr("Gore Joint Missing for Offset. actor: %s joint: %s\n", DevKitOnly_StringIdToString(GetLookId()), DevKitOnly_StringIdToString(key));
				continue;
			}

			if (const Locator* jointOffset = ScriptManager::HashLookup<Locator>(m_pJointOffsets, key))
			{
				m_pJointTree->TransformJointLs(jointTreeIndexOffset, *jointOffset, /*invalidateChildren =*/ true);
			}
		}
	}

	StringId64 rootJoint = GetAnimControl()->GetJointSid(0);
	I32F rootOffset = m_pJointTree->FindJointOffset(rootJoint);
	m_pJointTree->UpdateAllJointLocsWs(rootOffset);
	m_pJointTree->WriteJointCache();
}
/// --------------------------------------------------------------------------------------------------------------- ///
void NdMultiAttachable::CopyJointsFromParent()
{
	PROFILE(Collision, NdMultiAttachable_CopyJointsFromParent);

	const NdGameObject* pParentGo = GetParentGameObject();
	if (!pParentGo || !pParentGo->HasProcessExecuted())
		return;

	if (!m_aSourceJointIndex)
		return;
	ANIMLOG("NdMultiAttachable::CopyJointsFromParent %s mask %x", GetName(), GetParentJointSegmentMask());

	const AnimControl* pSrcAnimControl = pParentGo->GetAnimControl();
	const FgAnimData* pSrcAnimData = pParentGo->GetAnimData();
	const JointCache* pSrcJointCache = pSrcAnimControl->GetJointCache();
	AnimControl* pDstAnimControl = GetAnimControl();
	JointCache* pDstJointCache = pDstAnimControl->GetJointCache();
	const U32F numAnimatedDstJoints = pDstJointCache->GetNumAnimatedJoints();

	const I32F numAnimatedSrcJoints = pSrcJointCache->GetNumAnimatedJoints();
	const bool srcHasJointParams = (pSrcJointCache->GetJointParamsLs() != nullptr);

	const ndanim::JointParams* defaultLsJoints = pDstJointCache->GetDefaultLocalSpaceJoints();

	AnimExecutionContext* pSrcAnimCtx = GetAnimExecutionContext(pSrcAnimData);

	bool useHardcodedIdentityScale = false; 
	if ((pSrcAnimData->m_curSkelHandle.ToArtItem()->GetNameId() == SID("base-brute-female-skel") && GetAnimData()->m_curSkelHandle.ToArtItem()->GetNameId() == SID("scar-brute-female-01-jacket-cloth")) ||
		(pSrcAnimData->m_curSkelHandle.ToArtItem()->GetNameId() == SID("base-brute-male-skel") && GetAnimData()->m_curSkelHandle.ToArtItem()->GetNameId() == SID("scar-brute-male-jacket-cloth")) ||
		(pSrcAnimData->m_curSkelHandle.ToArtItem()->GetNameId() == SID("militia-whitney-skel") && GetAnimData()->m_curSkelHandle.ToArtItem()->GetNameId() == SID("militia-whitney-jacket-cloth"))
		)
	{
		useHardcodedIdentityScale = true;
	}

	U32 requiredSegmentMask = GetParentJointSegmentMask();
	if (requiredSegmentMask)
	{
		const AnimExecutionContext* pPrevAnimCtx = GetPrevFrameAnimExecutionContext(pSrcAnimData);

		// Ensure that we have local space joints for the first segment and skinning matrices for any later segments
		ProcessRequiredSegments(1, AnimExecutionContext::kOutputJointParamsLs | AnimExecutionContext::kOutputTransformsOs, pSrcAnimCtx, pPrevAnimCtx);
		if (requiredSegmentMask & ~1)
		{
			ProcessRequiredSegments(requiredSegmentMask & ~1, AnimExecutionContext::kOutputSkinningMats, pSrcAnimCtx, pPrevAnimCtx, false, true);
			ANIM_ASSERT((pSrcAnimCtx->m_processedSegmentMask_SkinningMats & requiredSegmentMask) == requiredSegmentMask);
		}
	}

	ANIMLOG("NdMultiAttachable::CopyJointsFromParent %s ProcessDeferredAnimCmd Complete", GetName());
	// Copy joints
	for (U32F dstJointIndex = 0; dstJointIndex < numAnimatedDstJoints; ++dstJointIndex)
	{
		ndanim::JointParams dstParamsLs = defaultLsJoints[dstJointIndex];
		const I32F srcJointIndex = m_aSourceJointIndex[dstJointIndex];

		if (dstJointIndex == GetAttachJointIndex())
		{
			if (const AttachSystem* pAs = pParentGo->GetAttachSystem())
			{
				const Locator rootLoc = pAs->GetLocator(GetParentAttachIndex());
				const Locator align = GetLocator();
				const Locator rootLs = align.UntransformLocator(rootLoc);
				dstParamsLs.m_trans = rootLs.Pos();
				dstParamsLs.m_quat = rootLs.Rot();

				pDstJointCache->OverwriteJointLocatorWs(0, rootLoc);
			}
		}
		else if (srcJointIndex != kInvalidJointIndex)
		{
			if (!g_disableSkinnedMultiAttachables)
			{
				int srcJointSegment = ndanim::GetSegmentForOutputJoint(pSrcAnimData->m_pSkeleton, srcJointIndex);

				const I32F dstParentIndex = pDstJointCache->GetParentJoint(dstJointIndex);
				const Locator& parentLocWs = (dstParentIndex >= 0) ? pDstJointCache->GetJointLocatorWs(dstParentIndex) : GetLocator();

				if (srcJointSegment == 0)
				{
					Locator srcJointLocatorWs = pSrcJointCache->GetJointLocatorWs(srcJointIndex);

					srcJointLocatorWs.SetRot(Normalize(srcJointLocatorWs.Rot()));

					ANIM_ASSERT(IsReasonable(srcJointLocatorWs));

					const Locator jointLocLs = parentLocWs.UntransformLocator(srcJointLocatorWs);

					dstParamsLs.m_trans = jointLocLs.Pos();
					dstParamsLs.m_quat = jointLocLs.Rot();

					if (srcJointIndex < numAnimatedSrcJoints && srcHasJointParams)
					{
						ndanim::JointParams srcParamsLs = pSrcJointCache->GetJointParamsLs(srcJointIndex);
						dstParamsLs.m_scale = srcParamsLs.m_scale;
					}

					pDstJointCache->OverwriteJointLocatorWs(dstJointIndex, srcJointLocatorWs);
				}
				else
				{
					const Mat34* pSrcAnimInverseBindPoses = ndanim::GetInverseBindPoseTable(pSrcAnimData->m_pSkeleton);
					const Mat34* pSrcSkinningBoneMats = reinterpret_cast<const Mat34*>(pSrcAnimCtx->m_pAllSkinningBoneMats);
					if (pSrcSkinningBoneMats)
					{
						// Ensure that we actually processed this segment so that the matrices aren't all garbage.
						ANIM_ASSERT(pSrcAnimCtx->m_processedSegmentMask_SkinningMats & (1 << srcJointSegment));

						const Mat44 skinningXform = pSrcSkinningBoneMats[srcJointIndex].GetMat44();

						// TODO: Cache bind pose xforms
						const Mat44 bindPoseXform = Inverse(pSrcAnimInverseBindPoses[srcJointIndex].GetMat44());
						const Mat44 objectSpaceXform = bindPoseXform * skinningXform;

						Mat44 srcJointXformWs = objectSpaceXform * pSrcAnimData->m_objXform.GetMat44();
						const Scalar xScale = Length3(srcJointXformWs.GetRow(0));
						const Scalar yScale = Length3(srcJointXformWs.GetRow(1));
						const Scalar zScale = Length3(srcJointXformWs.GetRow(2));
						Scalar zScaleFixed = zScale;

						// HACK ALERT:
						{
							// Figure out if there is a negative scale by taking the cross product of X and Y axes, and seeing if it points
							// in the same direction as the Z axis.
							const Vec4 srcXformAxisX = srcJointXformWs.GetRow(0);
							const Vec4 srcXformAxisY = srcJointXformWs.GetRow(1);
							const Vec4 srcXformAxisZ = srcJointXformWs.GetRow(2);
							const Vec4 srcXformExpectedZDir = Cross(srcXformAxisX, srcXformAxisY);

							// If Z points in the wrong direction, pick any axis and and set it's scale to negative, to create a matrix with
							// the correct handedness, so a valid sqt can be extracted.
							float dotZ = Dot3(srcXformAxisZ, srcXformExpectedZDir);
							if (dotZ < 0.0f)
							{
								srcJointXformWs.SetRow(2, -srcXformAxisZ);
								zScaleFixed = -zScaleFixed;
							}
						}

						RemoveScaleSafe(&srcJointXformWs);
						Locator srcJointLocLs = parentLocWs.UntransformLocator(Locator(srcJointXformWs));	// Joint locs in space of dstParentIndex joint

						dstParamsLs.m_trans = srcJointLocLs.Pos();
						dstParamsLs.m_quat = srcJointLocLs.Rot();
						dstParamsLs.m_scale = Vector(xScale, yScale, zScaleFixed);

						// Mega hack!! We really need to just support scale better across the board. Bleh!
						if (useHardcodedIdentityScale)
							dstParamsLs.m_scale = Vector(1, 1, 1);

						pDstJointCache->OverwriteJointLocatorWs(dstJointIndex, Locator(srcJointXformWs));
					}
				}
			}
		}

		pDstJointCache->SetJointParamsLs(dstJointIndex, dstParamsLs);
		pDstJointCache->UpdateJointLocatorWs(dstJointIndex);
	}

	// Copy output controls
	const U32 numSrcOutputControls = pSrcJointCache->GetNumOutputControls();
	const U32 numDstOutputControls = pDstJointCache->GetNumOutputControls();
	const FgAnimData& destAnimData = pDstAnimControl->GetAnimData();
	AnimExecutionContext* pDestAnimCtx = GetAnimExecutionContext(&destAnimData);
	const float * pSourceOutputControls = pSrcAnimCtx->m_pAllOutputControls ? pSrcAnimCtx->m_pAllOutputControls : pSrcJointCache->GetOutputControls(); // use the parent output controls, or the ones from jointcache it the parent has not been animated
	for (U32 outputControlIndex = 0; outputControlIndex < numDstOutputControls; ++outputControlIndex)
	{
		// Try to find the output control on the source
		const U16 srcOutputControlIndex = m_aSourceOutputControlIndex[outputControlIndex];
		if (srcOutputControlIndex < numSrcOutputControls)
		{
			const_cast<float*>(pDstJointCache->GetOutputControls())[outputControlIndex] = pSrcJointCache->GetOutputControl(srcOutputControlIndex);
			if (pDestAnimCtx)
				pDestAnimCtx->m_pAllOutputControls[outputControlIndex] = pSourceOutputControls[srcOutputControlIndex];
		}
	}

	// Not having a dest context would only happen if you spawn an object in an already updated bucket.
	// I.e. projectile spawning gore caps.
	if (pDestAnimCtx)
	{
		pDestAnimCtx->m_processedSegmentMask_JointParams |= (1 << 0);
		pDestAnimCtx->m_processedSegmentMask_Transforms |= (1 << 0);
		pDestAnimCtx->m_processedSegmentMask_FloatChannels |= (1 << 0);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdMultiAttachable::BuildSourceJointIndexTable(const NdGameObject* pSourceObject, const char* suffix)
{
	if (!pSourceObject)
	{
		MsgConErr("null Source Object passed to BuildSourceJointIndexTable()\n");
		return false;
	}

	AnimControl* pSrcAnimControl = pSourceObject->GetAnimControl();
	if (!pSrcAnimControl)
	{
		MsgConErr("Source object '%s' has no anim control!\n", pSourceObject->GetName());
		return false;
	}

	const JointCache* pSrcJointCache = pSrcAnimControl->GetJointCache();

	AnimControl* pAnimControl = GetAnimControl();
	if (!pAnimControl)
	{
		MsgConErr("Multi-attachable piece '%s' has no anim control!\n", DevKitOnly_StringIdToString(GetLookId()));
		return false;
	}

	const JointCache* pJointCache = pAnimControl->GetJointCache();
	const U32F numJoints = pJointCache->GetNumAnimatedJoints();

	m_aSourceJointIndex = NDI_NEW I16[numJoints];
	if (!m_aSourceJointIndex)
	{
		MsgConErr("Multi-attachable piece '%s' Ran out of memory to allocate multi-attachable joint translations\n", DevKitOnly_StringIdToString(GetLookId()));
		return false;
	}

	const FgAnimData* pSrcAnimData = pSourceObject->GetAnimData();
	const SkelComponentDesc* pSrcJointDescs = pSrcAnimData->m_curSkelHandle.ToArtItem()->m_pJointDescs;
	const U32F numSrcTotalJoints = pSrcAnimData->m_curSkelHandle.ToArtItem()->m_numTotalJoints;

	for (U32F i = 0; i < numJoints; ++i)
	{
		m_aSourceJointIndex[i] = kInvalidJointIndex;

		StringId64 searchJoint;
		if (suffix && strlen(suffix)>0)
		{
			const char* myJointName = pAnimControl->GetJointName(i);

			char tmpBuf[256];
			strncpy(tmpBuf, myJointName, 256);
			tmpBuf[255] = 0;
			char* suffixLoc = strstr(tmpBuf, suffix);

			if (suffixLoc)
			{
				*suffixLoc = '\0';
			}

			searchJoint = StringToStringId64(tmpBuf);
		}
		else
		{
			searchJoint = pAnimControl->GetJointSid(i);
		}

		I32F srcIndex = FindJoint(pSrcJointDescs, numSrcTotalJoints, searchJoint);
		if (srcIndex < 0)
			continue;

		const I32F srcSegment = ndanim::GetSegmentForOutputJoint(pSrcAnimData->m_pSkeleton, srcIndex);
		//ASSERT(srcSegment <= 1);

		m_parentJointSegmentMask |= (0x1<<srcSegment);

		//ASSERT(srcIndex < pSrcJointCache->GetNumTotalJoints());

		m_aSourceJointIndex[i] = srcIndex;
	}

	const U32 numDstOutputControls = pJointCache->GetNumOutputControls();
	const U32 numSrcOutputControls = pSrcAnimControl->GetAnimData().m_jointCache.GetNumOutputControls();
	if (numDstOutputControls)
	{
		const ArtItemSkeleton* pSrcSkeleton = pSrcAnimControl->GetArtItemSkel();
		const ArtItemSkeleton* pDstSkeleton = pAnimControl->GetArtItemSkel();
		m_aSourceOutputControlIndex = NDI_NEW I16[numDstOutputControls];

		for (U32F i = 0; i < numDstOutputControls; ++i)
		{
			m_aSourceOutputControlIndex[i] = -1;

			const char* dstOutputControlName = pDstSkeleton->m_pFloatDescs[i].m_pName;
			for (int x = strlen(dstOutputControlName) - 1; x > 0; --x)
			{
				if (dstOutputControlName[x] == '.')
				{
					dstOutputControlName = dstOutputControlName + x + 1;
					break;
				}
			}

			for (U32F j = 0; j < numSrcOutputControls; ++j)
			{
				const char* srcOutputControlName = pSrcSkeleton->m_pFloatDescs[j].m_pName;
				for (int x = strlen(srcOutputControlName) - 1; x > 0; --x)
				{
					if (srcOutputControlName[x] == '.')
					{
						srcOutputControlName = srcOutputControlName + x + 1;
						break;
					}
				}

				if (!strcmp(dstOutputControlName, srcOutputControlName))
				{
					m_aSourceOutputControlIndex[i] = j;
					break;
				}
			}
		}
	}

	return true;
}

void NdMultiAttachable::SetJointOffsets(const DC::HashTable* pDcOffsets, bool applyImmediately)
{
	m_pJointOffsets = pDcOffsets;

	if (applyImmediately)
	{
		CopyJointsFromParent();
		ApplyGoreHeadPropOffsets();
	}
}
