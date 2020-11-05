/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/fixedsizeheap.h"
#include "corelib/containers/hashtable.h"
#include "corelib/containers/list-array.h"
#include "corelib/math/spherical-coords.h"
#include "corelib/system/read-write-atomic-lock.h"

#include "gamelib/anim/nd-gesture-util.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemAnim;
struct TriangleIndices;

namespace DC
{
	struct GestureAnims;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class GestureCache
{
public:
	static CONST_EXPR size_t kMaxBlendTriangles = (Gesture::kMaxGestureNodeAnims * 2) - 2;

	struct TriangleIndicesU8
	{
		union
		{
			struct
			{
				U8 m_iA;
				U8 m_iB;
				U8 m_iC;
			};

			U8 m_indices[3];
		};
	};

	struct CacheKey
	{
		StringId64 m_gestureId;

		union
		{
			struct
			{
				SkeletonId m_skelId;
				U16 m_flipped;
				Gesture::AnimType m_gestureNodeType;
				Gesture::AlternativeIndex m_altIndex;
			};

			U64 m_u64 = 0ull;
		};

		bool operator == (const CacheKey& rhs) const
		{
			if (m_gestureId != rhs.m_gestureId)
				return false;
			if (m_u64 != rhs.m_u64)
				return false;
			return true;
		}
		bool operator != (const CacheKey& rhs) const
		{
			return !(*this == rhs);
		}
	};

	STATIC_ASSERT(sizeof(CacheKey) == 16);

	struct CachedAnim
	{
		StringId64 m_addBaseName;
		StringId64 m_slerpBaseName;

		ArtItemAnimHandle m_hSlerpAnim;
		ArtItemAnimHandle m_hAddAnim;

		SphericalCoords m_dir;

		float m_frame;
		float m_phase;

		bool m_extra : 1;
		bool m_hasGestureDirAp : 1;
		bool m_hasGestureDirFc : 1;
		bool m_wrapAround : 1;
	};

	struct CachedSpace
	{
		Locator m_offsetLs;

		StringId64 m_jointId; // joint id where the gesture space is attached
		StringId64 m_channelId; // proxy locator for joint inside the gesture anims themselves
	};

	struct CacheData
	{
		NdRwAtomicLock64 m_dataLock;
		mutable NdAtomic64 m_referenceCount;

		CacheKey m_key;

		CachedAnim m_cachedAnims[Gesture::kMaxGestureNodeAnims];

		StringId64 m_lowLodAnimId;
		ArtItemAnimHandle m_hLowLodAnim;

		StringId64 m_typeId;

		I32 m_numGestureAnims;
		U32 m_hierarchyId;
		Angle m_noBlendDir;

		CachedSpace m_animSpace;
		CachedSpace m_originSpace;
		CachedSpace m_feedbackSpace;

		TriangleIndicesU8 m_blendTris[kMaxBlendTriangles];
		U8 m_blendTriIslandIndex[kMaxBlendTriangles];
		U8 m_numBlendTris;
		U8 m_numIslands;

		AnimLookupSentinal m_animSentinal;

		StringId64 m_featherBlendId;
		TimeFrame m_creationTime;

		bool m_isBad : 1;
		bool m_applyJointLimits : 1;
		bool m_useDetachedPhase : 1;
		bool m_hasNoBlendDir : 1;
		bool m_hasDuplicateBlendDir : 1;
		bool m_wrap360 : 1;
		bool m_hyperRange : 1;
	};

	void Init(size_t maxEntries);
	void Update();
	void Shutdown();

	CacheData* TryCacheData(const CacheKey& key, const DC::GestureDef* pDcGesture, U32 hierarchyId);

	void ReleaseData(CacheData* pData);
	void RequestRebuild() { m_rebuildRequested = true; }
	void DebugDraw() const;
	bool DebugEraseInProgress() const { return m_debugEraseInProgress; }

private:
	void ConstructData(const CacheKey& key, const DC::GestureDef* pDcGesture, U32 hierarchyId, CacheData* pData);

	void RefreshConstruction(CacheData* pData, const DC::GestureDef* pDcGesture);

	void EraseData(CacheData* pData);

	void RefreshData(CacheData* pData);
	void RefreshAnims(CacheData* pData);
	void RefreshPhasesAndDirections(CacheData* pData);

	void ConstructBlendTriangles(CacheData* pData,
								 const DC::GestureAnims* pDcGestureAnims,
								 const DC::GestureDef* pDcGesture);
	void AppendTrianglesForGesture(CacheData* pData, const DC::GestureDef* pDcGesture);
	void AppendTrianglesForSubsetOfAnims(CacheData* pData,
										 const ListArray<int>& indices,
										 const Vec2* inputPoints,
										 int islandIndex);
	void AppendTriangles(CacheData* pData,
						 const TriangleIndices* triIndices32,
						 const U32F numGeneratedTris,
						 const Vec2* inputPoints,
						 int islandIndex);
	bool GestureAnimsAreLinear(CacheData* pData) const;
	bool RefreshNoBlendDir(CacheData* pData) const;

	static bool RefreshEntries(U8* pElement, uintptr_t userData, bool free);
	static void FixupManualTriangleWinding(TriangleIndicesU8& tri, const CacheData* pData);

	typedef HashTable<CacheKey, I32> KeyTable;

	mutable NdRwAtomicLock64 m_tableLock;

	AnimLookupSentinal m_animSentinal;

	bool m_evictionActive;
	bool m_rebuildRequested;
	bool m_debugEraseInProgress;
	KeyTable m_keyTable;
	FixedSizeHeap m_dataHeap;
};

/// --------------------------------------------------------------------------------------------------------------- ///
extern GestureCache g_gestureCache;
