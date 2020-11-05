/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/resource/resource-table.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimMasterTable
{
public:
 	static const U32 kInvalidActionCounter = 0;
 	static U32 m_actionCounter;
 	static I64 m_animTableModifiedFrame;

	static ArtItemAnimHandle LookupAnim(SkeletonId skelId,
										U32 hierarchyId,
										StringId64 animNameId,
										bool useRetargets = true);

	static void SetAnimTableModified();
 	static bool WasAnimTableModifiedLastFrame();
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimTable
{
public:
	void Init(SkeletonId skelId, U32 hierarchyId);

	SkeletonId GetSkelId() const { return m_skelId; }
	U32 GetHierarchyId() const { return m_hierarchyId; }
	
	ArtItemAnimHandle LookupAnim(StringId64 animNameId) const;

	ArtItemAnimHandle DevKitOnly_LookupAnimByIndex(int index) const;

private:
	SkeletonId m_skelId = INVALID_SKELETON_ID;
	U32 m_hierarchyId = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
#if !FINAL_BUILD
typedef const ArtItemAnim* (*LiveUpdateLookupAnimCallBack)(SkeletonId skelId,
														   U32 hierarchyId,
														   StringId64 animNameId,
														   const ArtItemAnim* prevRes);
extern LiveUpdateLookupAnimCallBack g_liveUpdateLookupAnimCallBack;
#endif
