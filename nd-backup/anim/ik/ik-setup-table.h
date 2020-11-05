/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/list-array.h"

#include "ndlib/anim/ik/ik-chain-setup.h"

 /// --------------------------------------------------------------------------------------------------------------- ///
struct FgAnimData;
struct LegIkChainSetups;

/// --------------------------------------------------------------------------------------------------------------- ///
class IkSetupTable
{
public:
	void Init();

	const LegIkChainSetups* GetLegIkSetups(SkeletonId skeletonId, LegSet legSet) const;
	const ArmIkChainSetups* GetArmIkSetups(SkeletonId skeletonId) const;

	bool AddArmIkSetupsForSkeleton(const FgAnimData* pAnimData);

	bool AddLegIkSetupsForSkeleton(const FgAnimData* pAnimData);
	bool AddLegIkSetupsForSkeleton(const FgAnimData* pAnimData,
								   int endJointIndex,
								   const StringId64* aLeftJointNames,
								   const StringId64* aRightJointNames,
								   LegSet legSet,
								   bool reverseKnee = false);

private:
	// Max NPCs * Max LegSets = 32 * 2 (Quadropeds)
	static const size_t kMaxEntries = 64;

	struct IkTableEntry
	{
		SkeletonId m_skeletonId = INVALID_SKELETON_ID;
		LegIkChainSetups* m_pLegSetups[kNumLegSets] = { nullptr };
		ArmIkChainSetups* m_pArmSetups = nullptr;
	};

	bool AddLegIkSetupsForSkeleton_NoLock(const ArtItemSkeleton* pSkel,
										  StringId64 leftEndJointId,
										  StringId64 rightEndJointId,
										  const StringId64* aLeftJointNames = nullptr,
										  const StringId64* aRightJointNames = nullptr,
										  LegSet legSet = kBackLegs,
										  bool reverseKnee = false);

	bool AddArmIkSetupsForSkeleton_NoLock(const ArtItemSkeleton* pSkel);

	void SetLegIkSetups(const SkeletonId skelId, LegSet set, LegIkChainSetups* pSetups);
	void SetArmIkSetups(const SkeletonId skelId, ArmIkChainSetups* pSetups);

	const LegIkChainSetups* GetLegIkSetups_NoLock(SkeletonId skeletonId, LegSet legSet) const;
	const ArmIkChainSetups* GetArmIkSetups_NoLock(SkeletonId skeletonId) const;

	mutable NdAtomicLock m_tableLock;

	ListArray<IkTableEntry> m_table;

	bool m_initialized = false;
};

extern IkSetupTable g_ikSetupTable;