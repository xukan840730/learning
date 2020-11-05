/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "corelib/containers/map-array.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/timer.h"

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/pose-matching.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/script/script-manager.h"

#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"
#include "gamelib/scriptx/h/pose-matching-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct PoseMatchingSingleTable
{
public:
	PoseMatchingSingleTable()
	{
		memset(this, 0, sizeof(*this));
	}

	~PoseMatchingSingleTable()
	{
		Destroy();
	}

	bool CreateTable(const ArtItemSkeleton* pSkel,
					 StringId64 weightName,
					 StringId64 srcAnimId,
					 StringId64 dstAnimId,
					 float minPhase,
					 float maxPhase)
	{
		// anim hasn't been loaded into memory yet.
		if (pSkel)
		{
			ANIM_ASSERT(weightName != INVALID_STRING_ID_64);
			m_skelId = pSkel->m_skelId;
			m_hierarchyId = pSkel->m_hierarchyId;
			m_weightName = weightName;

			m_srcAnimId = srcAnimId;
			m_dstAnimId = dstAnimId;

			m_tableNameId = CreateTableNameId(srcAnimId, dstAnimId);

			m_tableCreated = true;
			m_tableFilled = false;

			m_minPhase = minPhase;
			m_maxPhase = maxPhase;

			return true;
		}

		return false;
	}

	StringId64 GetTableNameId() const
	{
		return m_tableNameId;
	}

	static StringId64 CreateTableNameId(StringId64 srcAnimId, StringId64 dstAnimId)
	{
		const U64 dstAnimNumber = dstAnimId.GetValue();
		StringId64 tableNameId = StringId64Concat(srcAnimId, "->");
		tableNameId = StringId64ConcatInteger(tableNameId, dstAnimNumber);
		return tableNameId;
	}

	void RegisterPoseMatchingAnim(const ArtItemAnim* pAnim)
	{
		if (!IsTableCreated())
			return;

		if (IsTableFilled())
			return;

		const ArtItemAnim* pSrcAnim = nullptr;
		const ArtItemAnim* pDstAnim = nullptr;

		if (pAnim->GetNameId() == m_srcAnimId)
		{
			pSrcAnim = pAnim;
			pDstAnim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, m_dstAnimId).ToArtItem();
		}
		else if (pAnim->GetNameId() == m_dstAnimId)
		{
			pSrcAnim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, m_srcAnimId).ToArtItem();
			pDstAnim = pAnim;
		}
		else
		{
			ANIM_ASSERT(false);
		}

		if (pSrcAnim != nullptr && pDstAnim != nullptr)
		{
			static const I32 sc_maxNumJointDiffWeights = 64;
			JointDiffWeight arrJointWeights[sc_maxNumJointDiffWeights];
			I32 numJointDiffWeights = 0;
			float rootDeltaDiffWeight = 0.f;
			ConvertJointWeights(m_weightName, m_skelId, &rootDeltaDiffWeight, arrJointWeights, &numJointDiffWeights, sc_maxNumJointDiffWeights);

			U32 aJointIndices[sc_maxNumJointDiffWeights];
			ANIM_ASSERT(numJointDiffWeights <= sc_maxNumJointDiffWeights);
			// fill the indices.
			for (U32F iJoint = 0; iJoint < numJointDiffWeights; iJoint++)
				aJointIndices[iJoint] = arrJointWeights[iJoint].m_jointIndex;			

			U64 startTime = TimerGetRawCount();
			FillTable(m_skelId, aJointIndices, numJointDiffWeights, pSrcAnim, pDstAnim, m_minPhase, m_maxPhase);
			U64 stopTime = TimerGetRawCount();
			const F32 calculationTime = ConvertTicksToSeconds(startTime, stopTime);
			MsgOut("--------- finish calculating pose-match anim, %s -> %s takes %0.3fs-----------\n", DevKitOnly_StringIdToString(pSrcAnim->GetNameId()), DevKitOnly_StringIdToString(pDstAnim->GetNameId()), calculationTime);

			m_tableFilled = true;
		}
	}

	void Destroy()
	{
		NDI_DELETE[] m_table;
		m_table = nullptr;
	}

	I16 LookupTargetFrame(I16 inputFrame) const
	{
		ANIM_ASSERT(inputFrame >= 0 && inputFrame < m_numSrcTotalFrames);
		I16 targetFrame = m_table[inputFrame];
		ANIM_ASSERT(targetFrame >= 0 && targetFrame < m_numDstTotalFrames);
		return targetFrame;
	}

	bool IsTableCreated() const { return m_tableCreated; }
	bool IsTableFilled() const { return m_tableFilled; }

	StringId64 GetSrcAnimId() const { return m_srcAnimId; }
	StringId64 GetDstAnimId() const { return m_dstAnimId; }

	bool LookupTransitionFrame(const StringId64 srcAnimId, const StringId64 dstAnimId, const I32 srcFrame, I32* outFrame) const
	{
		if (m_srcAnimId == srcAnimId && m_dstAnimId == dstAnimId)
		{
			if (m_numSrcTotalFrames == 0)
				return false;

			I32 inputFrame = Clamp(srcFrame, 0, m_numSrcTotalFrames - 1);
			*outFrame = GetTargetFrame(inputFrame);
			ANIM_ASSERT(*outFrame >= 0 && *outFrame < m_numDstTotalFrames);
			return true;
		}

		return false;
	}

private:
	void Allocate(I32 numFrames)
	{
		ANIM_ASSERT(numFrames > 0 && numFrames < 16000);
		m_table = NDI_NEW I16[numFrames];
	}

	void SetTargetFrame(I16 inputFrame, I16 targetFrame)
	{
		ANIM_ASSERT(inputFrame >= 0 && inputFrame < m_numSrcTotalFrames);
		ANIM_ASSERT(targetFrame >= 0 && targetFrame < m_numDstTotalFrames);
		m_table[inputFrame] = targetFrame;
	}

	I32 GetTargetFrame(I16 inputFrame) const
	{
		ANIM_ASSERTF(inputFrame >= 0 && inputFrame < m_numSrcTotalFrames, ("PoseMatching, Lookup frame out of bound! inputFrame:%d, m_numSrcTotalFrames:%d\n", inputFrame, m_numSrcTotalFrames));
		return m_table[inputFrame];
	}

	bool FillTable(const SkeletonId skelId,
				   const U32* aJointIndices,
				   const U32 numJoints,
				   const ArtItemAnim* pSrcAnim,
				   const ArtItemAnim* pDstAnim,
				   float minPhase,
				   float maxPhase)
	{
		ANIM_ASSERT(pSrcAnim && pDstAnim);

		m_numSrcTotalFrames = pSrcAnim->m_pClipData->m_numTotalFrames;
		m_numDstTotalFrames = pDstAnim->m_pClipData->m_numTotalFrames;

		Allocate(m_numSrcTotalFrames);

		// precache joint local transforms.
		const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(skelId).ToArtItem();

		ScopedTempAllocator jj(FILE_LINE_FUNC);

		AnimJointBuffer srcJointBuffer;
		AnimJointBuffer dstJointBuffer;

		// fill anim basic info.
		srcJointBuffer.Allocate(m_numSrcTotalFrames, numJoints);
		dstJointBuffer.Allocate(m_numDstTotalFrames, numJoints);

		// precache src anim joints transform.
		for (U32F iFrame = 0; iFrame < m_numSrcTotalFrames; iFrame++)
		{
			Locator oAlign;
			float phase = (float)iFrame / m_numSrcTotalFrames;
			bool valid = FindAlignFromApReference(skelId, pSrcAnim, phase, Locator(kOrigin), SID("apReference"), &oAlign);

			const Transform& oAlignTrans = Transform(oAlign.GetRotation(), oAlign.GetTranslation());

			AnimateJoints(oAlignTrans,
						  pArtSkeleton,
						  pSrcAnim,
						  iFrame,
						  aJointIndices,
						  numJoints,
						  nullptr,
						  srcJointBuffer.GetFrameBuffer(iFrame).m_joints,
						  nullptr,
						  nullptr);
		}

		// precache dst anim joints transform.
		for (U32F iFrame = 0; iFrame < m_numDstTotalFrames; iFrame++)
		{
			Locator oAlign;
			float phase = (float)iFrame/ m_numDstTotalFrames;
			bool valid = FindAlignFromApReference(skelId, pDstAnim, phase, Locator(kOrigin), SID("apReference"), &oAlign);

			const Transform& oAlignTrans = Transform(oAlign.GetRotation(), oAlign.GetTranslation());

			AnimateJoints(oAlignTrans,
						  pArtSkeleton,
						  pDstAnim,
						  iFrame,
						  aJointIndices,
						  numJoints,
						  nullptr,
						  dstJointBuffer.GetFrameBuffer(iFrame).m_joints,
						  nullptr,
						  nullptr);
		}

		for (U32F iFrame = 0; iFrame < m_numSrcTotalFrames; iFrame++)
		{
			JointDiffFuncParams params;
			params.m_jointDiffDefId = m_weightName;
			params.m_skelId = skelId;
			params.m_pAnimI = pSrcAnim;
			params.m_pAnimJ = pDstAnim;
			params.m_frameI = iFrame;
			params.m_precacheAnimBufferI = &srcJointBuffer;
			params.m_precacheAnimBufferJ = &dstJointBuffer;
			params.m_minPhase = minPhase;
			params.m_maxPhase = maxPhase;
			params.m_useLookupTable = false;	// we're generating lookup table, don't use it!			

			I32 outFrame = -1;
			const bool success = ComputeJointDiffTransitionFrame(params, &outFrame);

			if (success)
			{
				SetTargetFrame(iFrame, outFrame);
			}
			else
			{
				SetTargetFrame(iFrame, 0);
			}
		}

		m_tableFilled = true;
		return true;
	}

	SkeletonId m_skelId;
	I32 m_hierarchyId;
	StringId64 m_weightName;
	StringId64 m_srcAnimId;
	StringId64 m_dstAnimId;
	StringId64 m_tableNameId;
	I16 m_numSrcTotalFrames;
	I16 m_numDstTotalFrames;

	I16* m_table;
	bool m_tableCreated;
	bool m_tableFilled;

	float m_minPhase;
	float m_maxPhase;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct PoseMatchingTableSet
{
public:
	typedef MapArray<StringId64, PoseMatchingSingleTable> InternalPoseMatchingTableSet;

	PoseMatchingTableSet()
		: m_setData(nullptr)
		, m_skelId(0)
		, m_weightName(INVALID_STRING_ID_64)
		, m_initialized(false)
	{}

	~PoseMatchingTableSet()
	{
		if (m_setData != nullptr)
		{
			NDI_DELETE[] m_setData;
			m_setData = nullptr;
		}		
	}

	bool CreateTable(const DC::PoseMatchingPrecacheAnimSet& dcSet)
	{
		const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(dcSet.m_packageName).ToArtItem();
		if (!pSkel)
			return false;

		const SkeletonId skelId = pSkel->m_skelId;
		const I32F numEntries = dcSet.m_numEntries;

		I32 totalCount = 0;

		// get total count so that we can allocate the memory.
		for (U32F iEntry = 0; iEntry < dcSet.m_numEntries; iEntry++)
		{
			const DC::PoseMatchingPrecacheAnimEntry& dcEntry = dcSet.m_entries[iEntry];
			totalCount += dcEntry.m_bidirectional ? 2 : 1;
		}
		ANIM_ASSERT(totalCount > 0 && totalCount < 1024);

		m_setData = NDI_NEW InternalPoseMatchingTableSet::NodeType[totalCount];
		m_internalMap.Init(totalCount, m_setData);

		I32 count = 0;
		for (U32F iEntry = 0; iEntry < dcSet.m_numEntries; iEntry++)
		{
			const DC::PoseMatchingPrecacheAnimEntry& dcEntry = dcSet.m_entries[iEntry];

			{
				PoseMatchingSingleTable newTable;
				bool success = newTable.CreateTable(pSkel,
													dcSet.m_weightName,
													dcEntry.m_srcAnimName,
													dcEntry.m_dstAnimName,
													dcEntry.m_minPhase,
													dcEntry.m_maxPhase);

				if (!success)
					return false;

				StringId64 newTableNameId = newTable.GetTableNameId();
				m_internalMap.Set(newTableNameId, newTable);
				count++;
			}

			if (dcEntry.m_bidirectional)
			{
				PoseMatchingSingleTable newTable;
				bool success = newTable.CreateTable(pSkel,
													dcSet.m_weightName,
													dcEntry.m_dstAnimName,
													dcEntry.m_srcAnimName,
													dcEntry.m_minPhase,
													dcEntry.m_maxPhase);

				if (!success)
					return false;

				StringId64 newTableNameId = newTable.GetTableNameId();
				m_internalMap.Set(newTableNameId, newTable);
				count++;
			}
		}
		ANIM_ASSERT(count == totalCount);

		m_skelId = skelId;
		m_weightName = dcSet.m_weightName;

		m_initialized = true;
		return true;
	}

	void RegisterPoseMatchingAnim(const ArtItemAnim* pAnim)
	{
		if (!IsInitialized())
			return;

		StringId64 animId = pAnim->GetNameId();

		for (InternalPoseMatchingTableSet::Iterator it = m_internalMap.Begin(); it != m_internalMap.End(); ++it)
		{
			InternalPoseMatchingTableSet::NodeType* node = *it;
			ANIM_ASSERT(node != nullptr && node->first != INVALID_STRING_ID_64);
			PoseMatchingSingleTable& eachTable = node->second;
			if (eachTable.GetSrcAnimId() == animId || eachTable.GetDstAnimId() == animId)
			{
				node->second.RegisterPoseMatchingAnim(pAnim);
			}
		}
	}

	bool IsInitialized() const { return m_initialized; }

	bool LookupTransitionFrame(const SkeletonId skelId,
							   StringId64 weightNameId,
							   const StringId64 srcAnimId,
							   const StringId64 dstAnimId,
							   const I32 srcFrame,
							   I32* outFrame) const
	{
		if (!IsInitialized())
			return false;

		// if it doesn't match.
		if (skelId != m_skelId || weightNameId != m_weightName)
			return false;

		StringId64 tableNameId = PoseMatchingSingleTable::CreateTableNameId(srcAnimId, dstAnimId);
		InternalPoseMatchingTableSet::ConstIterator it = m_internalMap.Find(tableNameId);
		if (it != m_internalMap.End())
		{
			const InternalPoseMatchingTableSet::NodeType* node = *it;
			ANIM_ASSERT(node != nullptr && node->first == tableNameId);

			return node->second.LookupTransitionFrame(srcAnimId, dstAnimId, srcFrame, outFrame);
		}

		return false;
	}

private:
	InternalPoseMatchingTableSet m_internalMap;
	InternalPoseMatchingTableSet::NodeType* m_setData;
	SkeletonId m_skelId;
	StringId64 m_weightName;
	bool m_initialized;
};

/// --------------------------------------------------------------------------------------------------------------- ///
template <>
struct MapArrayFailT<PoseMatchingTableSet::InternalPoseMatchingTableSet>
{
	void operator()(const PoseMatchingTableSet::InternalPoseMatchingTableSet* pMap,
					size_t capacity,
					const void* pNewNode) const
	{
		SYSTEM_ASSERTF(false, ("Ran out of space in InternalPoseMatchingTableSet (capacity: %d)", pMap->GetCapacity()));
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct PoseMatchingTableManager
{
public:
	static const I32 kMaxNumSets = 16;

	PoseMatchingTableManager()
		: m_numSets(0)
	{}

	void CreateTable()
	{
		const DC::PoseMatchingPrecacheAnimsList* dcSets = static_cast<const DC::PoseMatchingPrecacheAnimsList*>(ScriptManager::LookupPointerInModule(SID("*pose-matching-precache-anim-small-list*"), SID("pose-matching"), nullptr));
		if (dcSets != nullptr)
		{
			if (m_numSets == 0)
				m_numSets = dcSets->m_count;

			ANIM_ASSERT(dcSets->m_count <= kMaxNumSets);

			for (U32F iSet = 0; iSet < dcSets->m_count; iSet++)
			{
				const DC::PoseMatchingPrecacheAnimSet& dcSet = dcSets->m_entrys[iSet];

				PoseMatchingTableSet& eachSet = m_sets[iSet];
				if (!eachSet.IsInitialized())
				{
					eachSet.CreateTable(dcSet);
				}
			}
		}
	}

	void RegisterPoseMatchingAnim(const ArtItemAnim* pAnim)
	{
		for (U32F iSet = 0; iSet < m_numSets; iSet++)
		{
			PoseMatchingTableSet& eachSet = m_sets[iSet];
			eachSet.RegisterPoseMatchingAnim(pAnim);
		}
	}

	I32 GetNumSets() const { return m_numSets; }

	bool LookupTransitionFrame(const SkeletonId skelId,
							   StringId64 weightNameId,
							   const StringId64 srcAnimId,
							   const StringId64 dstAnimId,
							   const I32 srcFrame,
							   I32* outFrame) const
	{
		for (U32F iSet = 0; iSet < m_numSets; iSet++)
		{
			const PoseMatchingTableSet& eachSet = m_sets[iSet];
			if (eachSet.LookupTransitionFrame(skelId, weightNameId, srcAnimId, dstAnimId, srcFrame, outFrame))
				return true;
		}

		return false;
	}

public:
	static bool s_preprocessTable;

private:
	PoseMatchingTableSet m_sets[kMaxNumSets];
	I32 m_numSets;	
} g_poseMatchingTableManager;

bool PoseMatchingTableManager::s_preprocessTable = false;

/// --------------------------------------------------------------------------------------------------------------- ///
void RegisterPoseMatchingAnim(const ArtItemAnim* pAnim)
{
	// use debug memory right now.
	if (!Memory::IsDebugMemoryAvailable())
		return;

	if (g_poseMatchingTableManager.s_preprocessTable)
	{
		AllocateJanitor jj(kAllocDebug, FILE_LINE_FUNC);

		// package might not be loaded when this func is called.
		g_poseMatchingTableManager.CreateTable();

		if (g_poseMatchingTableManager.GetNumSets() > 0)
			g_poseMatchingTableManager.RegisterPoseMatchingAnim(pAnim);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ToggleNeedPreProcessPoseMatchingAnims()
{
	PoseMatchingTableManager::s_preprocessTable = !PoseMatchingTableManager::s_preprocessTable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsPreProcessingPoseMatchingAnims()
{
	return PoseMatchingTableManager::s_preprocessTable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
PostMatchLookupResult LookupPreProcessTransitionFrame(const SkeletonId skelId,
													  StringId64 weightNameId,
													  const StringId64 srcAnimId,
													  const StringId64 dstAnimId,
													  const I32 srcFrame,
													  I32* outFrame)
{
	if (g_poseMatchingTableManager.LookupTransitionFrame(skelId, weightNameId, srcAnimId, dstAnimId, srcFrame, outFrame))
	{
		return kPoseMatchLookupSuccess;
	}

	return g_poseMatchingTableManager.s_preprocessTable ? kPoseMatchLookupFailed : kPoseMatchLookupTableNotBuilt;
}
