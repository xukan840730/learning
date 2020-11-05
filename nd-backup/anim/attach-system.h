/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/hashtable.h"
#include "corelib/math/locator.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemGeo;
class ArtItemSkeleton;
struct FgAnimData;

namespace DC
{
	struct AttachPointSpec;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const I16 kInvalidJointIndex = -1;
const U32 kRootJointIndex	 = 0;

/// --------------------------------------------------------------------------------------------------------------- ///
struct AttachPointSpec
{
	Locator m_jointOffset;
	StringId64 m_nameId;
	StringId64 m_attachableId; // support only 1 attachable now, but potentially we need more.
	I16 m_jointIndex;
	union
	{
		U8 m_flags;
		struct
		{
			bool m_targPoi : 1;
			bool m_invalidJoint : 1;
			bool m_hidden : 1;
			mutable bool m_invalidReported : 1;
		};
	};

	AttachPointSpec();
	AttachPointSpec(StringId64 nameId, I16 joint);
	AttachPointSpec(StringId64 nameId, I16 joint, const Locator& loc);
	AttachPointSpec(StringId64 nameId, I16 joint, const Locator& loc, bool hidden);
	AttachPointSpec(StringId64 nameId);
	AttachPointSpec(StringId64 nameId, const Locator& loc);
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AttachIndex
{
public:
	static const AttachIndex kInvalid;

	AttachIndex() : m_value(0xff) {}
	AttachIndex(U8 value) : m_value(value) {}

	bool operator==(const AttachIndex& rhs) const { return m_value == rhs.m_value; }
	bool operator!=(const AttachIndex& rhs) const { return m_value != rhs.m_value; }

	bool operator==(U8 rhs) const { return m_value == rhs; }
	bool operator!=(U8 rhs) const { return m_value != rhs; }

	U32F GetValue() const { return m_value; }

private:
	U8 m_value;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AttachSystem
{
public:
	AttachSystem();

	void Init(U32F count);
	bool InitFromDcData(StringId64 baseAttachSetId, size_t extraSpecCount = 0);
	bool InitFromSkeleton(size_t extraSpecCount = 0);
	bool InitFromGeometry(StringId64 meshId, size_t extraSpecCount = 0);
	static bool HasDcAttachSet(StringId64 attachSetId);

	bool LoadFromDcData(StringId64 baseAttachSetId);
	bool LoadFromSkeleton();
	bool LoadFromGeometry(StringId64 meshId);

	U32 CountDcAttachPoints(StringId64 baseAttachSetId) const;
	U32 CountSkeletonAttachPoints() const;
	U32 CountGeometryAttachPoints(StringId64 meshId) const;

	void CloneFrom(const AttachSystem* pSourceAs);

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	void SetAnimData(const FgAnimData* pAnimData);
	void Clear();

	void AddPointSpec(const AttachPointSpec& spec);
	void SetPointOffset(AttachIndex attachPointIndex, const Locator& loc);
	void SetJoint(AttachIndex attachPointIndex, int jointIndex, bool hidden);
	const AttachPointSpec& GetPointSpec(AttachIndex attachPointIndex) const;
	StringId64 GetNameId(AttachIndex attachPointIndex) const;

	bool IsValidAttachIndex(AttachIndex index) const;
	bool IsValidAttachId(StringId64 id) const;

	U32F GetPointCount() const { return m_pointCount; }
	U32F GetAllocCount() const { return m_allocCount; }

	U32F GetJointIndex(AttachIndex attachPointIndex) const;

	Locator GetLocator(AttachIndex attachPointIndex) const;
	Point GetAttachPosition(AttachIndex attachPointIndex) const
	{
		return GetLocator(attachPointIndex).GetTranslation();
	}

	/// SetLocator - for overriding the locator of specific attach points
	// joint index will be set to kInvalidJointIndex
	void SetLocator(AttachIndex attachPointIndex, const Locator& loc);

	// TODO: DEPRECATE these 2 functions. Only attachable item is saved here.
	StringId64 GetAttachableId(AttachIndex attachPointIndex) const;
	void SetAttachableId(AttachIndex attachPointIndex, StringId64 attachableId);

	// Convenience functions - prefer these if you aren't going to check for errors anyway!
	Locator GetLocatorById(StringId64 id) const;
	Point GetAttachPositionById(StringId64 id) const;

	void SetLocatorById(StringId64 id, const Locator& loc);

	bool FindPointIndexById(AttachIndex* pOut, StringId64 id) const;
	AttachIndex FindPointIndexById(StringId64 id) const;
	bool FindPointIndexByJointIndex(AttachIndex* pOut, int jointIndex) const;
	const char* GetDebugName(AttachIndex attachPointIndex) const;
	void DebugDraw(const char* szFilter1, const char* szFilter2, const char* szFilter3) const;
	void DebugDraw(I32 index) const;

private:
	typedef HashTable<StringId64, const DC::AttachPointSpec*> DcAttachTable;

	U32 m_allocCount;
	U32 m_pointCount;
	AttachPointSpec* m_pSpecs;	   // relo:
	const FgAnimData* m_pAnimData; // no relo: exist in the draw data

	bool Internal_LoadFromDcData(const DcAttachTable& attachTable);
	bool Internal_LoadFromSkeleton(const ArtItemSkeleton& skel);
	bool Internal_LoadFromGeometry(const ArtItemGeo& mesh);

	bool PopulateAttachTableFromDcData(StringId64 baseAttachSetId, DcAttachTable& attachTable) const;

	void CheckForInvalidAttachJoint(const AttachPointSpec& spec) const;
};
