/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/ik/ik-setup-table.h"

#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/ik/ik-chain-setup.h"

#include "gamelib/level/art-item-skeleton.h"

IkSetupTable g_ikSetupTable;

/// --------------------------------------------------------------------------------------------------------------- ///
void IkSetupTable::Init()
{
	Memory::PushAllocator(kAllocIkData, FILE_LINE_FUNC);
	m_table.Init(kMaxEntries, FILE_LINE_FUNC);
	m_initialized = true;
	Memory::PopAllocator();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const LegIkChainSetups* IkSetupTable::GetLegIkSetups(SkeletonId skeletonId, LegSet legSet) const
{
	AtomicLockJanitor lock(&m_tableLock, FILE_LINE_FUNC);

	return GetLegIkSetups_NoLock(skeletonId, legSet);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const LegIkChainSetups* IkSetupTable::GetLegIkSetups_NoLock(SkeletonId skeletonId, LegSet legSet) const
{
	for (const IkTableEntry& curEntry : m_table)
	{
		if (curEntry.m_skeletonId == skeletonId)
		{
			return curEntry.m_pLegSetups[legSet];
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArmIkChainSetups* IkSetupTable::GetArmIkSetups(SkeletonId skeletonId) const
{
	AtomicLockJanitor lock(&m_tableLock, FILE_LINE_FUNC);

	return GetArmIkSetups_NoLock(skeletonId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArmIkChainSetups* IkSetupTable::GetArmIkSetups_NoLock(SkeletonId skeletonId) const
{
	for (const IkTableEntry& curEntry : m_table)
	{
		if (curEntry.m_skeletonId == skeletonId)
		{
			return curEntry.m_pArmSetups;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IkSetupTable::AddArmIkSetupsForSkeleton_NoLock(const ArtItemSkeleton* pSkel)
{
	if (!pSkel)
		return false;

	const SkeletonId skelId = pSkel->m_skelId;

	if (GetArmIkSetups_NoLock(skelId))
	{
		return true;
	}

	Memory::Allocator* pIkAllocator = Memory::GetAllocator(kAllocIkData);

	if (!pIkAllocator || !pIkAllocator->CanAllocate(sizeof(ArmIkChainSetups), ALIGN_OF(ArmIkChainSetups)))
	{
		ANIM_ASSERTF(false,
					 ("Out of memory in IK data allocator! (trying to add arm IK setups for '%s')\n",
					  ResourceTable::LookupSkelName(skelId)));
		return false;
	}

	Memory::PushAllocator(pIkAllocator, FILE_LINE_FUNC);

	ArmIkChainSetups* pArmSetups = NDI_NEW ArmIkChainSetups();
	const bool retl = pArmSetups->m_armSetups[kLeftArm].Init(SID("l_hand_prop_attachment"), pSkel);
	const bool retr = pArmSetups->m_armSetups[kRightArm].Init(SID("r_hand_prop_attachment"), pSkel);

	if (retl && retr)
	{
		SetArmIkSetups(skelId, pArmSetups);
	}
	else
	{
		MsgWarn("Failed to initialize arm IK setups for skeleton '%s' - Does it have 'l_hand_prop_attachment' and 'r_hand_prop_attachment'?\n",
				ResourceTable::LookupSkelName(skelId));
		NDI_DELETE pArmSetups;
		pArmSetups = nullptr;
	}

	Memory::PopAllocator();

	return pArmSetups != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IkSetupTable::AddArmIkSetupsForSkeleton(const FgAnimData* pAnimData)
{
	if (!pAnimData)
		return false;

	AtomicLockJanitor lock(&m_tableLock, FILE_LINE_FUNC);

	const ArtItemSkeleton* pCurSkel = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem();

	const bool curValid  = AddArmIkSetupsForSkeleton_NoLock(pCurSkel);
	const bool animValid = AddArmIkSetupsForSkeleton_NoLock(pAnimSkel);

	return curValid && animValid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IkSetupTable::AddLegIkSetupsForSkeleton_NoLock(const ArtItemSkeleton* pSkel,
													StringId64 leftEndJointId,
													StringId64 rightEndJointId,
													const StringId64* aLeftJointNames /* = nullptr */,
													const StringId64* aRightJointNames /* = nullptr */,
													LegSet legSet /* = kBackLegs */,
													bool reverseKnee /* = false */)
{
	if (!pSkel || !leftEndJointId || !rightEndJointId)
		return false;

	SkeletonId skelId = pSkel->m_skelId;
	if (GetLegIkSetups_NoLock(skelId, legSet))
	{
		return true;
	}

	Memory::Allocator* pIkAllocator = Memory::GetAllocator(kAllocIkData);

	if (!pIkAllocator || !pIkAllocator->CanAllocate(sizeof(LegIkChainSetups), ALIGN_OF(LegIkChainSetups)))
	{
		ANIM_ASSERTF(false,
					 ("Out of memory in IK data allocator! (trying to add %s leg IK setups for '%s')\n",
					  GetLegSetName(legSet),
					  ResourceTable::LookupSkelName(skelId)));

		return false;
	}

	Memory::PushAllocator(pIkAllocator, FILE_LINE_FUNC);

	LegIkChainSetups* pLegSetups = NDI_NEW LegIkChainSetups();

	LegIkChainSetup& lSetup = pLegSetups->m_legSetups[kLeftLeg];
	LegIkChainSetup& rSetup = pLegSetups->m_legSetups[kRightLeg];

	const bool retl = aLeftJointNames ? lSetup.Init(leftEndJointId, pSkel, aLeftJointNames, reverseKnee)
									  : lSetup.Init(leftEndJointId, pSkel);

	const bool retr = aRightJointNames ? rSetup.Init(rightEndJointId, pSkel, aRightJointNames, reverseKnee)
									   : rSetup.Init(rightEndJointId, pSkel);

	//ANIM_ASSERTF(retl,
	//			  ("Failed to initialize left %s leg IK chain for '%s' - does it have the '%s' joint?\n",
	//			   GetLegSetName(legSet),
	//			   ResourceTable::LookupSkelName(skelId),
	//			   DevKitOnly_StringIdToString(leftEndJointId)));
	//
	//ANIM_ASSERTF(retr,
	//			 ("Failed to initialize right %s leg IK chain for '%s' - does it have the '%s' joint?\n",
	//			  GetLegSetName(legSet),
	//			  ResourceTable::LookupSkelName(skelId),
	//			  DevKitOnly_StringIdToString(rightEndJointId)));

	if (retl && retr)
	{
		SetLegIkSetups(skelId, legSet, pLegSetups);
	}
	else
	{
		NDI_DELETE pLegSetups;
		pLegSetups = nullptr;
	}

	Memory::PopAllocator();

	return pLegSetups != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IkSetupTable::AddLegIkSetupsForSkeleton(const FgAnimData* pAnimData)
{
	if (!pAnimData)
		return false;

	AtomicLockJanitor lock(&m_tableLock, FILE_LINE_FUNC);

	const ArtItemSkeleton* pCurSkel	 = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem();

	const bool curValid  = AddLegIkSetupsForSkeleton_NoLock(pCurSkel, SID("l_ankle"), SID("r_ankle"));
	const bool animValid = AddLegIkSetupsForSkeleton_NoLock(pAnimSkel, SID("l_ankle"), SID("r_ankle"));

	return curValid && animValid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IkSetupTable::AddLegIkSetupsForSkeleton(const FgAnimData* pAnimData,
											 int endJointIndex,
											 const StringId64* aLeftJointNames,
											 const StringId64* aRightJointNames,
											 LegSet legSet,
											 bool reverseKnee)
{
	if (!pAnimData)
		return false;

	AtomicLockJanitor lock(&m_tableLock, FILE_LINE_FUNC);

	const ArtItemSkeleton* pCurSkel	 = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem();

	const bool curValid = AddLegIkSetupsForSkeleton_NoLock(pCurSkel,
														   aLeftJointNames[endJointIndex],
														   aRightJointNames[endJointIndex],
														   aLeftJointNames,
														   aRightJointNames,
														   legSet,
														   reverseKnee);

	const bool animValid = AddLegIkSetupsForSkeleton_NoLock(pAnimSkel,
															aLeftJointNames[endJointIndex],
															aRightJointNames[endJointIndex],
															aLeftJointNames,
															aRightJointNames,
															legSet,
															reverseKnee);

	return curValid && animValid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkSetupTable::SetLegIkSetups(const SkeletonId skelId, LegSet set, LegIkChainSetups* pSetups)
{
	ANIM_ASSERT(pSetups);
	ANIM_ASSERT(set < kNumLegSets);
	ANIM_ASSERT(!m_table.IsFull());

	for (IkTableEntry& curEntry : m_table)
	{
		if (curEntry.m_skeletonId != skelId)
		{
			continue;
		}

		ANIM_ASSERT(curEntry.m_pLegSetups[set] == nullptr);
		curEntry.m_pLegSetups[set] = pSetups;
		return;
	}

	IkTableEntry newEntry;
	newEntry.m_skeletonId = skelId;
	newEntry.m_pLegSetups[set] = pSetups;

	m_table.PushBack(newEntry);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IkSetupTable::SetArmIkSetups(const SkeletonId skelId, ArmIkChainSetups* pSetups)
{
	ANIM_ASSERT(pSetups);
	ANIM_ASSERT(!m_table.IsFull());

	for (IkTableEntry& curEntry : m_table)
	{
		if (curEntry.m_skeletonId != skelId)
		{
			continue;
		}

		ANIM_ASSERT(curEntry.m_pArmSetups == nullptr);
		curEntry.m_pArmSetups = pSetups;
		return;
	}

	IkTableEntry newEntry;
	newEntry.m_skeletonId = skelId;
	newEntry.m_pArmSetups = pSetups;

	m_table.PushBack(newEntry);
}
