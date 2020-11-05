/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-table.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/scriptx/h/skel-retarget-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
U32 AnimMasterTable::m_actionCounter = 0;
I64 AnimMasterTable::m_animTableModifiedFrame = -1;

#if !FINAL_BUILD
LiveUpdateLookupAnimCallBack g_liveUpdateLookupAnimCallBack = nullptr;
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
struct LookupAnimSkelList
{
	static const I32 kMaxSkels = 16;
	I32 m_numSkels;
	SkeletonId m_skelIds[kMaxSkels];

	LookupAnimSkelList()
		: m_numSkels(0)
	{
		memset(m_skelIds, 0, sizeof(m_skelIds));
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
static void StaticFindRetargetSkel(SkeletonId skelId,
								   U32 hierarchyId,
								   const SkelTable::RetargetEntry* pRetargetEntry,
								   void* data)
{
	if (skelId == INVALID_SKELETON_ID || hierarchyId == 0)
	{
		return;
	}

	const DC::SkelRetarget* pSkelRetarget = pRetargetEntry->m_pSkelRetarget;
	if (pRetargetEntry->m_destSkelId == skelId && !pRetargetEntry->m_disabled && pSkelRetarget->m_mode == DC::kSkelRetargetModeAuto)
	{
		LookupAnimSkelList* pSkelList = (LookupAnimSkelList*)data;
		if (pSkelList->m_numSkels < LookupAnimSkelList::kMaxSkels)
		{
			pSkelList->m_skelIds[pSkelList->m_numSkels++] = pRetargetEntry->m_srcSkelId;
		}
		else
		{
			ANIM_ASSERTF(false, ("Too many auto retargted skeletons for LookupAnim"));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle AnimMasterTable::LookupAnim(SkeletonId skelId,
											  U32 hierarchyId,
											  StringId64 animNameId,
											  bool useRetargets)
{
	// PROFILE(Frame, AMT_LookupAnim);

	ArtItemAnimHandle anim = ResourceTable::LookupAnim(skelId, animNameId);

	if (!anim.ToArtItem() && useRetargets)
	{
		// Add secondary skeletons if any...
		LookupAnimSkelList otherSkelList;
		SkelTable::ForAllMatchingRetargetEntries(skelId, hierarchyId, StaticFindRetargetSkel, &otherSkelList);

		for (I32 iSkel = 0; iSkel < otherSkelList.m_numSkels; iSkel++)
		{
			const ArtItemSkeleton* pOtherSkel = ResourceTable::LookupSkel(otherSkelList.m_skelIds[iSkel]).ToArtItem();
			if (pOtherSkel)
			{
				anim = ResourceTable::LookupAnim(otherSkelList.m_skelIds[iSkel], animNameId);
				if (anim.ToArtItem())
				{
					break;
				}
			}
		}
	}

	// couldn't find the desired animation
	return anim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMasterTable::SetAnimTableModified()
{
	m_animTableModifiedFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimMasterTable::WasAnimTableModifiedLastFrame()
{
	return m_animTableModifiedFrame == (EngineComponents::GetNdFrameState()->m_gameFrameNumber-1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const ArtItemSkeleton* GetSkeleton(const char* pFuncName, StringId64 skeletonPackage, SkeletonId skelId)
{
	// Skeleton package id must be supplied if the animation set is being mapped on a character with a different skeleton
	const ArtItemSkeleton* pArtItemSkeleton = skeletonPackage != INVALID_STRING_ID_64 ?
		ResourceTable::LookupSkel(skeletonPackage).ToArtItem() : ResourceTable::LookupSkel(skelId).ToArtItem();

	if (!pArtItemSkeleton)
	{
		MsgAnim("%s: Unable to find skeleton 0x%.8x\n", pFuncName, skelId.GetValue());
	}

	return pArtItemSkeleton;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimTable::Init(SkeletonId skelId, U32 hierarchyId)
{
	m_skelId = skelId;
	m_hierarchyId = hierarchyId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle AnimTable::LookupAnim(StringId64 animLookupId) const
{
	return AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, animLookupId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle AnimTable::DevKitOnly_LookupAnimByIndex(int index) const
{
	return ResourceTable::DevKitOnly_GetAnimationForSkelByIndex(m_skelId, index);
}
