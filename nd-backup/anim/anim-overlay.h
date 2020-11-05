/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
	struct AnimOverlayLayerPriorities; 
	struct AnimOverlaySet;
}

struct VariantIndex
{
	StringId64 m_animNameId;
	U8 m_index;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ALIGNED(16) AnimOverlaySnapshot
{
public:
	struct DbgTransformHistory
	{
		void Allocate(Memory::Context context)
		{
			m_pEntries = NDI_NEW(context) AnimOverlayIterator[kMaxNumEntries];
			m_numEntries = 0;
		}

		void Append(const AnimOverlayIterator& itr)
		{
			if (m_numEntries < kMaxNumEntries)
			{
				m_pEntries[m_numEntries++] = itr;
			}
		}

		static const int kMaxNumEntries = 100;
		AnimOverlayIterator* m_pEntries;
		int m_numEntries;
	};

	struct Result
	{
		Result()
			: m_animId(INVALID_STRING_ID_64)
			, m_pHistory(nullptr)
		{}

		Result(StringId64 animId, DbgTransformHistory* pHistory)
			: m_animId(animId)
			, m_pHistory(pHistory)
		{}

		StringId64 m_animId;
		DbgTransformHistory* m_pHistory; // m_history is only valid for one frame, make a copy if you need.
	};

	AnimOverlaySnapshot();

	bool Init(const U32F numLayers, bool uniDirectional);
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	U32F GetNumLayers() const { return m_numOverlayLayers; }
	void CopyFrom(const AnimOverlaySnapshot* pSourceSnapshot);

	// CopyInclude: only copy those layers from source-snapshot
	void CopyInclude(const AnimOverlaySnapshot* pSourceSnapshot, const I32* layerIndices, const I32 numLayers);

	// CopyExclude: copy from source-snapshot but excludes from those layers.
	void CopyExclude(const AnimOverlaySnapshot* pSourceSnapshot, const I32* layerIndices, const I32 numLayers);

	StringId64 LookupTransformedAnimId(StringId64 animNameId) const;
	Result DevKitOnly_LookupTransformedAnimId(StringId64 animNameId) const;
	CachedAnimOverlayLookup LookupTransformedAnimId(const CachedAnimOverlayLookup& lookup) const;

	StringId64 LookupTransformedAnimIdAndIncrementVariantIndices(StringId64 animNameId);

	U32F GetVariantIndex(StringId64 animNameId) const;
	U32F GetVariantIndexAndIncrement(StringId64 animNameId, U32F numVariants, bool randomize);
	void UpdateVariantsFrom(const AnimOverlaySnapshot* pSourceSnapshot);
	void ResetVariantIndexes();

	void SetOverlaySet(U32F index, const DC::AnimOverlaySet* pOverlaySet);
	const DC::AnimOverlaySet* GetOverlaySet(U32F index) const { return m_ppOverlaySets[index]; }
	void ClearLayer(I32F layerIndex);

	AnimOverlayIterator GetNextEntry(const AnimOverlayIterator& startingIterator) const;

	bool IsUniDirectional() const { return m_uniDirectional; }

	U32 GetInstanceSeed() const { return m_instanceSeed; }
	void SetInstanceSeed(U32 seed) { m_instanceSeed = seed; }

	const AnimOverlaySnapshotHash& GetHash() const { return m_currentHash; }

	bool DebugPrint(const AnimOverlayIterator& iter, char* pOutBuf, int numBytes) const;

	void SetDebugName(const char* szDbgName) { m_szDbgName = szDbgName; }
	const char* GetDebugName() const { return m_szDbgName; }

private:
	void RefreshHash();

	void InternalUpdateVariantsFrom(const AnimOverlaySnapshot* pSourceSnapshot);

	Result InternalLookupTransformedAnimId(StringId64 animNameId,
										   bool incrementVariantIndices,
										   bool debugHistory = false);

	AnimOverlaySnapshotHash m_currentHash;
	const DC::AnimOverlaySet** m_ppOverlaySets;
	StringId64* m_overlaySetIds;
	VariantIndex* m_pVariantIndexArray;

	U32 m_instanceSeed;
	U8 m_numOverlayLayers;
	bool m_uniDirectional;

	///-------------------------------------------------------------------------------///
	/// DEBUG ONLY!
	///-------------------------------------------------------------------------------///
	const char* m_szDbgName;
	
	friend class AnimOverlays;
	friend class AnimStateSnapshot;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ALIGNED(16) AnimOverlays
{
public:
	AnimOverlays();
	void Init(U32 instanceSeed, StringId64 priorityNameId, bool uniDirectional = false);

	bool SetOverlaySet(const DC::AnimOverlaySet* pOverlaySet, bool allowMissing = false);
	bool SetOverlaySet(StringId64 overlaySetName, bool allowMissing = false);
	bool SetOverlaySet(StringId64 overlaySetName, StringId64 nameSpace, bool allowMissing = false);

	// SetOverlaySetByLayer is for DC::AnimOverlaySet's layer-id is different from player-overlay's layer-id. 
	// like weapon-upgrades, it has 3 layers in player-overlays.
	bool SetOverlaySetByLayer(const DC::AnimOverlaySet* pOverlaySet, StringId64 layerId);

	bool IsOverlaySet(StringId64 overlaySetName) const;
	StringId64 GetCurrentOverlay(StringId64 layerId) const;
	const DC::AnimOverlaySet* GetCurrentOverlaySet(StringId64 layerId) const;

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	U32F GetNumLayers() const;

	StringId64 LookupTransformedAnimId(StringId64 animNameId) const;
	CachedAnimOverlayLookup LookupTransformedAnimId(const CachedAnimOverlayLookup& lookup) const;
	AnimOverlaySnapshot::Result DevKitOnly_LookupTransformedAnimId(StringId64 animNameId) const;

	U32F GetVariantIndex(StringId64 animNameId) const;
	U32F GetVariantIndexAndIncrement(StringId64 animNameId, U32F numVariants, bool randomize);
	void NotifyDcUpdated();
	void DebugPrint() const;

	AnimOverlaySnapshot* GetSnapshot() { return m_pOverlaySnapshot; }
	const AnimOverlaySnapshot* GetSnapshot() const { return m_pOverlaySnapshot; }

	static void RegisterScriptObserver();

	I32F GetLayerIndex(StringId64 layerId) const;
	void ClearLayer(StringId64 layerId);
	StringId64 GetLayerName(I32F index) const;

	AnimOverlaySnapshotHash GetHash() const
	{
		return GetSnapshot() ? GetSnapshot()->m_currentHash : AnimOverlaySnapshotHash();
	}

	const DC::AnimOverlayLayerPriorities* GetPriorities() const { return m_pPriorities; }

	void SetDebugName(const char* pDbgName)
	{
		if (m_pOverlaySnapshot != nullptr)
			m_pOverlaySnapshot->SetDebugName(pDbgName);
	}
	const char* GetDebugName() const { return m_pOverlaySnapshot ? m_pOverlaySnapshot->m_szDbgName : nullptr; }

private:
	AnimOverlaySnapshot* m_pOverlaySnapshot;
	StringId64 m_priorityNameId;
	const DC::AnimOverlayLayerPriorities* m_pPriorities;

	friend class AnimStateSnapshot;
	friend class AnimOverlaySnapshot;
};
