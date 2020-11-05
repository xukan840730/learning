/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/hashtable.h"
#include "corelib/containers/linkednode.h"
#include "corelib/containers/list-array.h"
#include "corelib/math/locator.h"
#include "corelib/util/float16.h"

#include "ndlib/resource/resource-table.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArmIkChain;
class ArtItemAnim;
class ArtItemSkeleton;
class ExternalBitArray;
class LegIkChain;
template <U64 NUM_BITS> class BitArray;

namespace DC 
{
	struct RetargetRegression;
	struct SkelRetarget;
}

namespace ndanim
{
	struct JointParams;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class SkelTable
{
public:

	struct PointF16
	{
		void AsPoint(Point_arg p)
		{
			m_x = MakeF16(p.X());
			m_y = MakeF16(p.Y());
			m_z = MakeF16(p.Z());
		}

		Point ToPoint() const
		{
			const float x = MakeF32(m_x);
			const float y = MakeF32(m_y);
			const float z = MakeF32(m_z);

			return Point(x,y,z);
		}

		F16 m_x;
		F16 m_y;
		F16 m_z;
	};

	struct QuatF16
	{
		void AsQuat(Quat_arg q)
		{
			m_x = MakeF16(q.X());
			m_y = MakeF16(q.Y());
			m_z = MakeF16(q.Z());
			m_w = MakeF16(q.W());
		}

		Quat ToQuat() const
		{
			const float x = MakeF32(m_x);
			const float y = MakeF32(m_y);
			const float z = MakeF32(m_z);
			const float w = MakeF32(m_w);

			return Normalize(Quat(x,y,z,w));
		}

		F16 m_x;
		F16 m_y;
		F16 m_z;
		F16 m_w;
	};

	struct LocatorF16
	{
		void AsLocator(const Locator& loc) 
		{
			m_trans.AsPoint(loc.Pos());
			m_quat.AsQuat(loc.Rot());
		}

		Locator ToLocator() const
		{
			return Locator(m_trans.ToPoint(), m_quat.ToQuat());
		}

		PointF16 m_trans;
		QuatF16 m_quat;
	};

	struct JointRetarget
	{
		LocatorF16 m_jointBindPoseDelta;
		LocatorF16 m_parentBindPoseDelta;
		PointF16 m_srcJointPosLs;
		F16 m_boneScale;
		I16 m_srcIndex;
		I16 m_srcAnimIndex;
		I16 m_destIndex;
		I16 m_destAnimIndex;

		I16 m_srcChannelGroup;
		U16 m_srcValidBit;
		I16 m_dstChannelGroup;
		U16 m_dstValidBit;

		U8 m_retargetMode;
		const DC::RetargetRegression* m_pRegression;
	};

	struct FloatChannelRetarget
	{
		I8 m_srcIndex;
		I8 m_destIndex;
	};

	struct ArmRetargetData
	{
		ArmRetargetData();

		ArmIkChain* m_apSrcIkChains[2];
		ArmIkChain* m_apTgtIkChains[2];
		Quat m_aSrcToTargetRotationOffsets[2];
	};

	struct LegRetargetData
	{
		LegRetargetData();

		LegIkChain* m_apSrcIkChains[2];
		LegIkChain* m_apTgtIkChains[2];
		Locator m_aSrcToTargetOffsets[2];
	};

	struct RetargetEntry
	{
		const DC::SkelRetarget* m_pSkelRetarget;
		SkeletonId m_srcSkelId;
		SkeletonId m_destSkelId;
		U32 m_srcHierarchyId;
		U32 m_destHierarchyId;
		JointRetarget* m_jointRetarget;
		U16 m_count;
		ListArray<FloatChannelRetarget> m_floatRetargetList;
		mutable U16 m_disabled;
		F32 m_scale;

		// m_dstToSrcSegMapping[iDestSeg] defines the bitmask for the required source segments
		ExternalBitArray* m_dstToSrcSegMapping;
		ArmRetargetData* m_pArmData;
		LegRetargetData* m_pLegData;
	};

	static const U32 kDefaultRetargetJointCount = 1024;
	static const U32 kMaxNumRetargetEntries = 512;
	static RetargetEntry m_retargetEntryTable[kMaxNumRetargetEntries];
	static U32 m_numRetargetEntries;
	static const U32 kInvalidActionCounter = 0;
	static U32 m_actionCounter;

	static HeapAllocator m_skelRetargetHeap;

	static void PostSyncLoadStartup();
	static const RetargetEntry* LookupRetarget(SkeletonId srcSkelId, SkeletonId dstSkelId);
	static const JointRetarget* FindJointRetarget(const RetargetEntry* pRetarget, I32F iSrcJoint);
	static bool EnableRetarget(StringId64 srcSkel, StringId64 dstSkel);
	static bool DisableRetarget(StringId64 srcSkel, StringId64 dstSkel);
	typedef void (*RetargetEntryFunctor)(SkeletonId skelId,
										 U32 hierarchyId,
										 const RetargetEntry* pRetargetEntry,
										 void* data);
	static void ForAllMatchingRetargetEntries(SkeletonId skelId,
											  U32 hierarchyId,
											  RetargetEntryFunctor funcPtr,
											  void* data = nullptr);

	static void BuildRetargetEntries(SkeletonId skelId);
	static void InvalidateRetargetEntries(SkeletonId skelId);

	static void ResolveValidBitMapping(JointRetarget& jointRetarget,
									   const ArtItemSkeleton* pSrcSkel,
									   const ArtItemSkeleton* pDestSkel);

	static void DebugPrint();

private:
	static void AddRetargetEntry(const DC::SkelRetarget& skelRetarget);
	static bool BuildRetargetEntry(RetargetEntry* pRetargetEntry,
								   const ArtItemSkeleton* pSrcSkel,
								   const ArtItemSkeleton* pDestSkel,
								   const ndanim::JointParams* pSrcDefJoints,
								   const ndanim::JointParams* pDestDefJoints,
								   const Mat34* pSrcInvBindPoseXforms,
								   const Mat34* pDestInvBindPoseXforms,
								   StringId64 poseAnimId,
								   const ArtItemAnim* pPoseAnim);
	static void GenerateSegmentMapping(RetargetEntry* pEntry,
									   const ArtItemSkeleton* pSrcSkel,
									   const ArtItemSkeleton* pDstSkel);
	static void GenerateFloatChannelMapping(RetargetEntry* pEntry,
											const ArtItemSkeleton* pSrcSkel,
											const ArtItemSkeleton* pDstSkel);

	static NdAtomicLock m_accessLock;
};
