/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/attach-system.h"

#include "corelib/containers/hashtable.h"
#include "corelib/memory/relocate.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/msg.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/mesh-table.h"
#include "ndlib/process/process.h"
#include "ndlib/render/look.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/attach-point-defines.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-geo.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/scriptx/h/look2-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const AttachIndex AttachIndex::kInvalid;

/// --------------------------------------------------------------------------------------------------------------- ///
AttachPointSpec::AttachPointSpec()
	: m_nameId(0)
	, m_jointIndex(0)
	, m_attachableId(INVALID_STRING_ID_64)
	, m_flags(0)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
AttachPointSpec::AttachPointSpec(StringId64 nameId, I16 iJoint)
	: m_jointOffset(kIdentity)
	, m_nameId(nameId)
	, m_jointIndex(iJoint)
	, m_attachableId(INVALID_STRING_ID_64)
	, m_flags(0)
{
	m_invalidJoint = iJoint < 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AttachPointSpec::AttachPointSpec(StringId64 nameId, I16 iJoint, const Locator& loc)
	: m_jointOffset(loc)
	, m_nameId(nameId)
	, m_jointIndex(iJoint)
	, m_attachableId(INVALID_STRING_ID_64)
	, m_flags(0)
{
	m_invalidJoint = iJoint < 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AttachPointSpec::AttachPointSpec(StringId64 nameId, I16 iJoint, const Locator& loc, bool hidden)
	: m_jointOffset(loc)
	, m_nameId(nameId)
	, m_jointIndex(iJoint)
	, m_attachableId(INVALID_STRING_ID_64)
	, m_flags(0)
{
	m_hidden = hidden;
	m_invalidJoint = !hidden && (iJoint < 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AttachPointSpec::AttachPointSpec(StringId64 nameId)
	: m_jointOffset(kIdentity)
	, m_nameId(nameId)
	, m_jointIndex(kInvalidJointIndex)
	, m_attachableId(INVALID_STRING_ID_64)
	, m_flags(0)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
AttachPointSpec::AttachPointSpec(StringId64 nameId, const Locator& loc)
	: m_jointOffset(loc)
	, m_nameId(nameId)
	, m_jointIndex(kInvalidJointIndex)
	, m_attachableId(INVALID_STRING_ID_64)
	, m_flags(0)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
AttachSystem::AttachSystem()
	: m_allocCount(0)
	, m_pointCount(0)
	, m_pSpecs(nullptr)
	, m_pAnimData(nullptr)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::Init(U32F count)
{
	ANIM_ASSERT(m_allocCount == 0); // we shouldn't init this twice
	ANIM_ASSERT(count > 0);

	m_allocCount = count;
	m_pSpecs = NDI_NEW (kAlign16) AttachPointSpec[count];
}

/// --------------------------------------------------------------------------------------------------------------- ///
static int AddDCAttachListToTable(HashTable<StringId64, const DC::AttachPointSpec*>& attachTable,
								  const DC::AttachPointSpecArray* pArray)
{
	int numAdded = 0;
	if (pArray)
	{
		for (int iSpec = 0; iSpec < pArray->m_count; iSpec++)
		{
			const DC::AttachPointSpec& dcSpec = pArray->m_array[iSpec];
			if (attachTable.Find(dcSpec.m_name) == attachTable.End())
			{
				attachTable.Add(dcSpec.m_name, &dcSpec);
				numAdded++;
			}
		}
	}
	return numAdded;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::InitFromDcData(StringId64 baseAttachSetId, size_t extraSpecCount)
{
	DcAttachTable attachTable;

	Memory::Allocator* pAlloc = Memory::TopAllocator();
	ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
	if (!PopulateAttachTableFromDcData(baseAttachSetId, attachTable))
		return false;

	m_allocCount = attachTable.Size() + extraSpecCount;
	if (m_allocCount == 0)
		return false;

	AllocateJanitor allocJan(pAlloc, FILE_LINE_FUNC);
	m_pSpecs = NDI_NEW (kAlign16) AttachPointSpec[m_allocCount];

	return Internal_LoadFromDcData(attachTable);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::InitFromSkeleton(size_t extraSpecCount)
{
	if (!m_pAnimData)
		return 0;

	const ArtItemSkeleton* pSkel = m_pAnimData->m_curSkelHandle.ToArtItem();
	if (!pSkel)
		return 0;

	if (pSkel->m_versionNumber < 14)
		return false;

	m_allocCount = pSkel->m_numAttachPoints + extraSpecCount;
	m_pSpecs = NDI_NEW(kAlign16) AttachPointSpec[m_allocCount];

	if (pSkel->m_numAttachPoints > 0)
		return Internal_LoadFromSkeleton(*pSkel);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::InitFromGeometry(StringId64 meshId, size_t extraSpecCount)
{
	const ArtItemGeo* pMesh = ResourceTable::LookupGeometry(meshId).ToArtItem();
	if (!pMesh)
		return 0;

	m_allocCount = pMesh->m_attachPointsCount + extraSpecCount;
	m_pSpecs = NDI_NEW(kAlign16) AttachPointSpec[m_allocCount];

	if (pMesh->m_attachPointsCount > 0)
		return Internal_LoadFromGeometry(*pMesh);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::LoadFromDcData(StringId64 baseAttachSetId)
{
	DcAttachTable attachTable;

	ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
	if (!PopulateAttachTableFromDcData(baseAttachSetId, attachTable))
		return false;

	return Internal_LoadFromDcData(attachTable);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::LoadFromSkeleton()
{
	if (!m_pAnimData)
		return false;

	const ArtItemSkeleton* pSkel = m_pAnimData->m_curSkelHandle.ToArtItem();
	if (!pSkel)
		return false;

	if (pSkel->m_versionNumber < 14)
		return false;

	return Internal_LoadFromSkeleton(*pSkel);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::LoadFromGeometry(StringId64 meshId)
{
	const ArtItemGeo* pMesh = ResourceTable::LookupGeometry(meshId).ToArtItem();
	if (!pMesh)
		return false;

	return Internal_LoadFromGeometry(*pMesh);
}

U32 AttachSystem::CountDcAttachPoints(StringId64 baseAttachSetId) const
{
	DcAttachTable attachTable;

	ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
	if (!PopulateAttachTableFromDcData(baseAttachSetId, attachTable))
		return 0;

	return attachTable.Size();
}

U32 AttachSystem::CountSkeletonAttachPoints() const
{
	if (!m_pAnimData)
		return 0;

	const ArtItemSkeleton* pSkel = m_pAnimData->m_curSkelHandle.ToArtItem();
	if (!pSkel)
		return 0;

	if (pSkel->m_versionNumber < 14)
		return 0;

	return pSkel->m_numAttachPoints;
}

U32 AttachSystem::CountGeometryAttachPoints(StringId64 meshId) const
{
	const ArtItemGeo* pMesh = ResourceTable::LookupGeometry(meshId).ToArtItem();
	if (!pMesh)
		return 0;

	return pMesh->m_attachPointsCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::CloneFrom(const AttachSystem* pSourceAs)
{
	m_allocCount = pSourceAs->m_allocCount;
	m_pSpecs = nullptr;

	if (m_allocCount > 0)
	{
		m_pSpecs = NDI_NEW (kAlign16) AttachPointSpec[m_allocCount];
		memcpy(m_pSpecs, pSourceAs->m_pSpecs, sizeof(AttachPointSpec)*m_allocCount);
	}

	m_pointCount = pSourceAs->m_pointCount;

	// We only clone the data - we do NOT want to use the same joint cache as whatever we cloned from!
	m_pAnimData = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::SetAnimData(const FgAnimData* pAnimData)
{
	m_pAnimData = pAnimData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::Clear()
{
	m_pointCount = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pSpecs, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::AddPointSpec(const AttachPointSpec& spec)
{
	if (spec.m_jointIndex == kInvalidJointIndex
		|| (m_pAnimData && (spec.m_jointIndex < m_pAnimData->m_jointCache.GetNumTotalJoints())))
	{
		if (m_pointCount < m_allocCount)
		{
			m_pSpecs[m_pointCount++] = spec;
		}
		else
		{
			MsgErr("The maximum number of attach points is reached.");
			ANIM_ASSERTF(false, ("Out of attach points"));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::SetPointOffset(AttachIndex attachPointIndex, const Locator& loc)
{
	ANIM_ASSERT(attachPointIndex.GetValue() < m_allocCount);
	m_pSpecs[attachPointIndex.GetValue()].m_jointOffset = loc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::SetJoint(AttachIndex attachPointIndex, int jointIndex, bool hidden)
{
	const U32F iSpec = attachPointIndex.GetValue();
	
	ANIM_ASSERT(iSpec < m_allocCount);
	
	m_pSpecs[iSpec].m_jointIndex   = jointIndex;
	m_pSpecs[iSpec].m_invalidJoint = !hidden && (jointIndex < 0);
	m_pSpecs[iSpec].m_hidden	   = hidden;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::CheckForInvalidAttachJoint(const AttachPointSpec& spec) const
{
	STRIP_IN_FINAL_BUILD;

	if (spec.m_invalidJoint & !spec.m_invalidReported)
	{
		const Process* pProc = m_pAnimData ? m_pAnimData->m_hProcess.ToProcess() : nullptr;
		MsgConScriptError("Couldn't find joint for attach point %s process %s\n",
						  DevKitOnly_StringIdToString(spec.m_nameId),
						  pProc ? DevKitOnly_StringIdToString(pProc->GetUserId()) : "<unknown>");
		spec.m_invalidReported = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::FindPointIndexByJointIndex(AttachIndex* pOut, int jointIndex) const
{
	if (pOut) *pOut = AttachIndex::kInvalid;
	bool found = false;
	for (U32F iPoint = 0; iPoint < m_pointCount; ++iPoint)
	{
		const AttachPointSpec& spec = m_pSpecs[iPoint];
		if (spec.m_jointIndex == jointIndex)
		{
			CheckForInvalidAttachJoint(spec);
			*pOut = (U8)iPoint;
			found = true;
			break;
		}
	}
	return found;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::FindPointIndexById(AttachIndex* pOut, StringId64 id) const
{
	if (pOut) *pOut = AttachIndex::kInvalid;
	bool found = false;
	for (U32F iPoint = 0; iPoint < m_pointCount; ++iPoint)
	{
		const AttachPointSpec& spec = m_pSpecs[iPoint];
		if (spec.m_nameId == id)
		{
			CheckForInvalidAttachJoint(spec);
			*pOut = (U8)iPoint;
			found = true;
			break;
		}
	}
	return found;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AttachIndex AttachSystem::FindPointIndexById(StringId64 id) const
{
	AttachIndex out = AttachIndex::kInvalid;
	bool success = FindPointIndexById(&out, id);
	ANIM_ASSERTF(success, ("Couldn't find attach id \"%s\"!", DevKitOnly_StringIdToString(id)));
	return out;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AttachPointSpec& AttachSystem::GetPointSpec(AttachIndex attachPointIndex) const
{
	ANIM_ASSERT(attachPointIndex.GetValue() < m_allocCount);
	CheckForInvalidAttachJoint(m_pSpecs[attachPointIndex.GetValue()]);
	return m_pSpecs[attachPointIndex.GetValue()];
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AttachSystem::GetNameId(AttachIndex attachPointIndex) const
{
	const AttachPointSpec& specs = GetPointSpec(attachPointIndex);
	return specs.m_nameId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::IsValidAttachIndex(AttachIndex index) const
{
	return index.GetValue() < m_pointCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::IsValidAttachId(StringId64 id) const
{
	AttachIndex attachIndex = AttachIndex::kInvalid;
	const bool success = FindPointIndexById(&attachIndex, id);
	return success && IsValidAttachIndex(attachIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AttachSystem::GetJointIndex(AttachIndex attachPointIndex) const
{
	if (attachPointIndex.GetValue() < m_allocCount)
	{
		CheckForInvalidAttachJoint(m_pSpecs[attachPointIndex.GetValue()]);
		return (U32)m_pSpecs[attachPointIndex.GetValue()].m_jointIndex;
	}

	return kRootJointIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AttachSystem::GetLocator(AttachIndex attachPointIndex) const
{
	Locator attachLoc(SMath::kIdentity);
	if (attachPointIndex.GetValue() < m_allocCount)
	{
		const AttachPointSpec& spec = m_pSpecs[attachPointIndex.GetValue()];
		ANIM_ASSERT(spec.m_jointIndex == kInvalidJointIndex
					|| (m_pAnimData && (spec.m_jointIndex < m_pAnimData->m_jointCache.GetNumTotalJoints())));
		CheckForInvalidAttachJoint(spec);

		if (m_pAnimData)
		{
			const Locator& jointLoc = m_pAnimData->m_jointCache.GetJointLocatorWs(spec.m_jointIndex == kInvalidJointIndex
																					  ? kRootJointIndex
																					  : (U32)spec.m_jointIndex);
			attachLoc = jointLoc.TransformLocator(spec.m_jointOffset);
		}
		else
		{
			attachLoc = spec.m_jointOffset;
		}
	}
	else
	{
		ANIM_ASSERT(m_pAnimData); // not valid without joint cache!
		// Get the root joint
		attachLoc = m_pAnimData->m_jointCache.GetJointLocatorWs(kRootJointIndex);
	}

	ASSERT(IsFinite(attachLoc)); // something wrong with JointCache? check m_pAnimData->m_hProcess
	if (IsFinite(attachLoc))
	{
		attachLoc.SetRot(Normalize(attachLoc.Rot()));
		return attachLoc;
	}
	else
	{
		return Locator(kIdentity);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AttachSystem::GetLocatorById(StringId64 id) const
{
	AttachIndex attachIndex = FindPointIndexById(id);
	return GetLocator(attachIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point AttachSystem::GetAttachPositionById(StringId64 id) const
{
	AttachIndex attachIndex = FindPointIndexById(id);
	return GetAttachPosition(attachIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// SetLocator - for overriding the locator of specific attach points (set joint index to kInvalidJointIndex)
void AttachSystem::SetLocator(AttachIndex attachPointIndex, const Locator& loc)
{
	if (attachPointIndex.GetValue() < m_pointCount)
	{
		const Locator jointLoc = m_pAnimData ? m_pAnimData->m_jointCache.GetJointLocatorWs(kRootJointIndex) : Locator(kIdentity);
		Locator attachLoc = jointLoc.UntransformLocator(loc);

		AttachPointSpec& spec = m_pSpecs[attachPointIndex.GetValue()];
		spec.m_jointOffset = attachLoc;
		spec.m_jointIndex = m_pAnimData ? (I16)kRootJointIndex : kInvalidJointIndex;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::SetLocatorById(StringId64 id, const Locator& loc)
{
	AttachIndex index = FindPointIndexById(id);
	SetLocator(index, loc);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AttachSystem::GetAttachableId(AttachIndex attachPointIndex) const
{
	if (attachPointIndex.GetValue() < m_pointCount)
	{
		const AttachPointSpec& spec = m_pSpecs[attachPointIndex.GetValue()];
		return spec.m_attachableId;
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::SetAttachableId(AttachIndex attachPointIndex, StringId64 attachableId)
{
	if (attachPointIndex.GetValue() < m_pointCount)
	{
		AttachPointSpec& spec = m_pSpecs[attachPointIndex.GetValue()];
		spec.m_attachableId = attachableId;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AttachSystem::GetDebugName(AttachIndex attachPointIndex) const
{
	ANIM_ASSERT(attachPointIndex.GetValue() < m_pointCount);
	return DevKitOnly_StringIdToString(m_pSpecs[attachPointIndex.GetValue()].m_nameId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::DebugDraw(const char* szFilter1, const char* szFilter2, const char* szFilter3) const
{
	STRIP_IN_FINAL_BUILD;

	const Color textColor(0.8f, 0.8f, 0.8f, 0.5f);

	const int BUFFER_SIZE = 64;
	char buf[BUFFER_SIZE];
	for (U32F i = 0; i < m_pointCount; ++i)
	{
		snprintf(buf, BUFFER_SIZE, "%s(%d)", GetDebugName(AttachIndex(i)), (U32)i);

		if ((szFilter1 != nullptr && szFilter1[0] != 0) || 
			(szFilter2 != nullptr && szFilter2[0] != 0) ||
			(szFilter3 != nullptr && szFilter3[0] != 0))
		{
			bool found1 = szFilter1[0] != 0 && strstr(buf, szFilter1) != nullptr;
			bool found2 = szFilter2[0] != 0 && strstr(buf, szFilter2) != nullptr;
			bool found3 = szFilter3[0] != 0 && strstr(buf, szFilter3) != nullptr;
			if (!found1 && !found2 && !found3)
				continue;
		}

		const Locator loc = GetLocator(AttachIndex(i));

		g_prim.Draw(DebugCoordAxes(loc, 0.05f, PrimAttrib(0)));
		g_prim.Draw(DebugString(loc.Pos(), buf, textColor));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachSystem::DebugDraw(I32 i) const
{
	STRIP_IN_FINAL_BUILD;

	if (i >= m_pointCount)
		return;

	const Color textColor(0.8f, 0.8f, 0.8f, 0.5f);

	const int BUFFER_SIZE = 64;
	char buf[BUFFER_SIZE];
	{
		snprintf(buf, BUFFER_SIZE, "%s(%d)", GetDebugName(AttachIndex(i)), (U32)i);
		const Locator loc = GetLocator(AttachIndex(i));

		g_prim.Draw(DebugCoordAxes(loc, 0.05f, PrimAttrib(0)));
		g_prim.Draw(DebugString(loc.Pos(), buf, textColor));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::Internal_LoadFromDcData(const DcAttachTable& attachTable)
{
	SkeletonId skelId = INVALID_SKELETON_ID;
	U32 hierId = 0;
	const SMath::Mat34* aInvBindPoses = nullptr;

	const ArtItemSkeleton* pSkel = m_pAnimData->m_curSkelHandle.ToArtItem();
	if (m_pAnimData && pSkel)
	{
		skelId = pSkel->m_skelId;
		hierId = pSkel->m_hierarchyId;
		aInvBindPoses = ndanim::GetInverseBindPoseTable(pSkel->m_pAnimHierarchy);
	}

	for (DcAttachTable::const_iterator it = attachTable.Begin(); it != attachTable.End(); ++it)
	{
		const DC::AttachPointSpec& dcSpec = *it->m_data;

		const I32F jointIndex = (dcSpec.m_joint != INVALID_STRING_ID_64) ? m_pAnimData->FindJoint(dcSpec.m_joint) : kRootJointIndex;
		if (jointIndex == kInvalidJointIndex)
		{
			MsgErr("AttachSystem, couldn't find %s, joint: %s\n",
				   DevKitOnly_StringIdToString(dcSpec.m_name),
				   DevKitOnly_StringIdToString(dcSpec.m_joint));
			continue;
		}

		Locator offset = dcSpec.m_offset;
		if (dcSpec.m_apRef != INVALID_STRING_ID_64 && dcSpec.m_apAnim != INVALID_STRING_ID_64)
		{
			if (skelId != INVALID_SKELETON_ID && aInvBindPoses)
			{
				const ArtItemAnim* pAnim = AnimMasterTable::LookupAnim(skelId, hierId, dcSpec.m_apAnim).ToArtItem();
				if (pAnim)
				{
					const CompressedChannel* pCameraChannel = FindChannel(pAnim, dcSpec.m_apRef);
					if (pCameraChannel)
					{
						ndanim::JointParams jointParams;
						ReadFromCompressedChannel(pCameraChannel, 0, &jointParams);

						Locator offsetObjectSpace = Locator(jointParams.m_trans, jointParams.m_quat); // ignore jointParams.m_scale - scaling attach points is not supported

						const SMath::Mat34& invBindPose34 = aInvBindPoses[jointIndex];
						Locator invBindPoseObjectSpace(invBindPose34.GetMat44());
						Locator bindPoseObjectSpace = Inverse(invBindPoseObjectSpace);

						offset = bindPoseObjectSpace.UntransformLocator(offsetObjectSpace); // find the delta of the apReference, relative to the joint's bind pose (both of which are in object space)
					}
				}
			}
		}

		if (m_pointCount >= m_allocCount)
		{
			MsgErr("The maximum number of attach points is reached.");
			return false;
		}

		m_pSpecs[m_pointCount++] = AttachPointSpec(dcSpec.m_name, jointIndex, offset, false);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::Internal_LoadFromSkeleton(const ArtItemSkeleton& skel)
{
	for (int ii = 0; ii < skel.m_numAttachPoints; ++ii)
	{
		if (m_pointCount >= m_allocCount)
		{
			MsgErr("The maximum number of attach points is reached.");
			return false;
		}

		const AttachPoint& point = skel.m_pAttachPoints[ii];
		AttachPointSpec newSpec(point.m_name, point.m_jointIndex, point.m_offsetLoc, false);
		newSpec.m_targPoi = point.m_isPointOfInterest;
		m_pSpecs[m_pointCount++] = newSpec;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::Internal_LoadFromGeometry(const ArtItemGeo& mesh)
{
	for (int ii = 0; ii < mesh.m_attachPointsCount; ++ii)
	{
		if (m_pointCount >= m_allocCount)
		{
			MsgErr("The maximum number of attach points is reached.");
			return false;
		}

		const AttachPoint& point = mesh.m_pAttachPoints[ii];
		AttachPointSpec newSpec(point.m_name, point.m_jointIndex, point.m_offsetLoc, false);
		newSpec.m_targPoi = point.m_isPointOfInterest;
		m_pSpecs[m_pointCount++] = newSpec;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::HasDcAttachSet(StringId64 attachSetId)
{
	// See if we can find the named attach point array in our DC data!
	const DC::Map* pMap = ScriptManager::Lookup<DC::Map>(SID("*attach-points*"), nullptr);
	if (!pMap)
		return false;

	const DC::AttachPointSpecArray* pBaseArray = ScriptManager::MapLookup<DC::AttachPointSpecArray>(pMap, attachSetId);
	return pBaseArray != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachSystem::PopulateAttachTableFromDcData(StringId64 baseAttachSetId, DcAttachTable& attachTable) const
{
	if (!m_pAnimData)
		return false;

	typedef DcArray<const StringId64*> StringIdPtrArray;
	const StringIdPtrArray* pSetArray = nullptr;

	if (const NdGameObject* pGameObj = NdGameObject::FromProcess(m_pAnimData->m_hProcess.ToProcess()))
	{
		if (const DC::Look2* pLook = GetLook(pGameObj->GetResolvedLookId()).AsLook2())
		{
			pSetArray = pLook->m_attachPointSets;
			if (pLook->m_baseAttachPointSet != INVALID_STRING_ID_64)
			{
				baseAttachSetId = pLook->m_baseAttachPointSet;
			}
		}
	}

	// See if we can find the named attach point array in our DC data!
	const DC::Map* pMap = ScriptManager::Lookup<DC::Map>(SID("*attach-points*"), nullptr);
	if (!pMap)
		return false;

	int totalCount = 0;
	if (pSetArray)
	{
		for (const StringId64* pSetId : *pSetArray)
		{
			if (const DC::AttachPointSpecArray* pArray = ScriptManager::MapLookup<DC::AttachPointSpecArray>(pMap, *pSetId))
			{
				totalCount += pArray->m_count;
			}
		}
	}
	const DC::AttachPointSpecArray* pBaseArray = ScriptManager::MapLookup<DC::AttachPointSpecArray>(pMap, baseAttachSetId);
	if (pBaseArray)
	{
		totalCount += pBaseArray->m_count;
	}

	if (totalCount == 0)
		return false;

	attachTable.Init(totalCount, FILE_LINE_FUNC);

	if (pSetArray)
	{
		for (const StringId64* pSetId : *pSetArray)
		{
			const DC::AttachPointSpecArray* pArray = ScriptManager::MapLookup<DC::AttachPointSpecArray>(pMap, *pSetId);
			AddDCAttachListToTable(attachTable, pArray);
		}
	}
	AddDCAttachListToTable(attachTable, pBaseArray);

	return true;
}
