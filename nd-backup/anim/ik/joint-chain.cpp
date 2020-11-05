/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/ik/joint-chain.h"

#include "corelib/memory/relocate.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/ik/joint-limits.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/plugin/retarget-shared.plugin.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/memory/relocatable-heap-rec.h"
#include "ndlib/process/process-heap.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/scriptx/h/ik-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static JointSetObserver s_jointSetObserver;

#if FINAL_BUILD
#define ENABLE_JOINTSET_ASSERTS false
#else
#define ENABLE_JOINTSET_ASSERTS true
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
// Global
/// --------------------------------------------------------------------------------------------------------------- ///
void JointSetInit()
{
#ifndef FINAL_BUILD
	ScriptManager::RegisterObserver(&s_jointSetObserver);
#endif
}

void JointSetUpdate()
{
#ifndef FINAL_BUILD
	if (s_jointSetObserver.m_loadedThisFrame > 0)
		s_jointSetObserver.m_loadedThisFrame--;
#endif
}

static bool JointSetModuleReloadedThisFrame()
{
	return s_jointSetObserver.m_loadedThisFrame > 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// JointSet
/// --------------------------------------------------------------------------------------------------------------- ///
JointSet::JointSet()
{
	m_type		= kTypeInvalid;
	m_numJoints = 0;
	m_joints	= nullptr;

	m_jointData = nullptr;
	m_ikData	= nullptr;

	m_skelId = INVALID_SKELETON_ID;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JointSet::~JointSet()
{
	if (m_joints)
	{
		NDI_DELETE[] m_joints;
		m_joints = nullptr;
	}

	if (m_ikData)
	{
		for (int i = 0; i < m_numJoints; i++)
		{
			if (m_ikData->m_ikJointData[i].m_coneBoundaryPointsPs)
				NDI_DELETE[] m_ikData->m_ikJointData[i].m_coneBoundaryPointsPs;
			if (m_ikData->m_ikJointData[i].m_coneSlicePlanePs)
				NDI_DELETE[] m_ikData->m_ikJointData[i].m_coneSlicePlanePs;
			if (m_ikData->m_ikJointData[i].m_coneBoundaryPlanePs)
				NDI_DELETE[] m_ikData->m_ikJointData[i].m_coneBoundaryPlanePs;
		}
		NDI_DELETE[] m_ikData->m_ikJointData;
		NDI_DELETE m_ikData;
		m_ikData = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointSet::Init(NdGameObject* pGo)
{
	if (NULL_IN_FINAL_BUILD(pGo))
	{
		const ProcessHeap* pProcessHeap = EngineComponents::GetProcessMgr()->GetHeap();

		/* If I am in the process heap, then this game object must be my process. */

		if (pProcessHeap->IsPointerInHeap(this))
		{
			const RelocatableHeapRecord* pHeapRecord = pGo->GetHeapRecord();

			ANIM_ASSERT(pHeapRecord->IsPointerInBlock(this));
		}
	}

	m_pGo = pGo;
	m_useAnimatedSkel = false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pGo, deltaPos, lowerBound, upperBound);

	RelocatePointer(m_joints, deltaPos, lowerBound, upperBound);

	if (m_ikData)
	{
		for (int i = 0; i < m_numJoints; i++)
		{
			RelocatePointer(m_ikData->m_ikJointData[i].m_coneBoundaryPointsPs, deltaPos, lowerBound, upperBound);
			RelocatePointer(m_ikData->m_ikJointData[i].m_coneSlicePlanePs, deltaPos, lowerBound, upperBound);
			RelocatePointer(m_ikData->m_ikJointData[i].m_coneBoundaryPlanePs, deltaPos, lowerBound, upperBound);
		}
		RelocatePointer(m_ikData->m_ikJointData, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_ikData, deltaPos, lowerBound, upperBound);
	}

	if (m_jointData)
	{
		RelocatePointer(m_jointData->m_jointLocLs, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_jointData->m_jointLocWs, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_jointData->m_pJointScale, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_jointData->m_boneLengths, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_jointData->m_jointValid, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_jointData, deltaPos, lowerBound, upperBound);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::InitIkData(StringId64 constraintInfoId)
{
	ANIM_ASSERT(m_ikData == nullptr);

	m_ikData					 = NDI_NEW IkData;
	m_ikData->m_ikJointData		 = NDI_NEW JointConstraintData[m_numJoints];
	m_ikData->m_pDcIkConstraints = IkConstraintsPtr(constraintInfoId, SID("ik-settings"));

	for (int j = 0; j < m_numJoints; j++)
	{
		JointConstraintData* ikData = &m_ikData->m_ikJointData[j];
		ikData->m_bindPoseLs		= (j >= kStartJointOffset) ? GetBindPoseLocLs(j) : Locator(kIdentity);
		ikData->m_coneVisPointPs	= kZero;
		ikData->m_coneRotPs			= kIdentity;
		ikData->m_coneBoundaryPointsPs	 = nullptr;
		ikData->m_coneSlicePlanePs		 = nullptr;
		ikData->m_coneBoundaryPlanePs	 = nullptr;
		ikData->m_coneBoundryTwistCenter = nullptr;
		ikData->m_coneBoundryTwistRange	 = nullptr;
		ikData->m_coneNumBoundaryPoints	 = 0;
		ikData->m_visPointTwistCenter	 = 0.0f;
		ikData->m_visPointTwistRange	 = 0.0f;
		ikData->m_debugFlags			 = 0;
	}

	if (m_ikData->m_pDcIkConstraints.Valid())
	{
		const DC::IkConstraintInfo* pConstraints = m_ikData->m_pDcIkConstraints;
		for (int j = kStartJointOffset; j < m_numJoints; j++)
		{
			const DC::IkConstraint* pConstraint = nullptr;
			for (int c = 0; c < pConstraints->m_count; c++)
			{
				if (GetJointId(j) == pConstraints->m_array[c].m_jointName)
				{
					pConstraint = &pConstraints->m_array[c];
					break;
				}
			}

			if (pConstraint == nullptr)
				continue;

			int numCorners = pConstraint->m_coneNumCorners;
			if (numCorners < 3)
				continue;

			JointConstraintData* ikData		= &m_ikData->m_ikJointData[j];
			ikData->m_coneBoundaryPointsPs  = NDI_NEW Vector[numCorners];
			ikData->m_coneSlicePlanePs		= NDI_NEW Vector[numCorners];
			ikData->m_coneBoundaryPlanePs   = NDI_NEW Vector[numCorners];
			ikData->m_coneNumBoundaryPoints = numCorners;
		}
	}

	UpdateIkData(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::UpdateIkData(bool force)
{
	if (!m_ikData || !m_ikData->m_pDcIkConstraints.Valid())
		return;

	if (!force && !JointSetModuleReloadedThisFrame())
		return;

	const DC::IkConstraintInfo* pConstraints = m_ikData->m_pDcIkConstraints;
	for (int j = kStartJointOffset; j < m_numJoints; j++)
	{
		const DC::IkConstraint* pConstraint = nullptr;
		for (int c = 0; c < pConstraints->m_count; c++)
		{
			if (GetJointId(j) == pConstraints->m_array[c].m_jointName)
			{
				pConstraint = &pConstraints->m_array[c];
				break;
			}
		}

		if (pConstraint == nullptr)
			continue;

		JointConstraintData* ikData = &m_ikData->m_ikJointData[j];
		ikData->m_debugFlags = pConstraint->m_debugFlags;

		int numCorners = pConstraint->m_coneNumCorners;
		if (numCorners < 3)
			continue;

		JointLimits::SetupJointConstraintEllipticalCone(ikData, pConstraint);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::IkConstraintInfo* JointSet::GetJointConstraints() const
{
	if (m_ikData)
		return m_ikData->m_pDcIkConstraints;

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int JointSet::GetJointIndex(I32F jointOffset) const
{
	ANIM_ASSERT(jointOffset >= kStartJointOffset && jointOffset < m_numJoints);
	return m_joints[jointOffset];
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F JointSet::GetJointOffset(I32F jointIndex) const
{
	for (U32F i = 0; i < m_numJoints; ++i)
	{
		if (m_joints[i] == jointIndex)
			return i;
	}

	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 JointSet::GetJointId(I32F jointOffset) const
{
	ANIM_ASSERT(jointOffset >= kParentJointOffset && jointOffset < m_numJoints);
	const NdGameObject* pGameObj = GetNdGameObject();
	ANIM_ASSERT(pGameObj);
	if (pGameObj != nullptr)
	{
		const FgAnimData* pAnimData = pGameObj->GetAnimData();
		const SkelComponentDesc* pUseJointDescs = pAnimData->m_pJointDescs;
		if (m_useAnimatedSkel && pAnimData->m_animateSkelHandle.ToArtItem())
		{
			pUseJointDescs = pAnimData->m_animateSkelHandle.ToArtItem()->m_pJointDescs;
		}
		return pUseJointDescs[m_joints[jointOffset]].m_nameId;
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int JointSet::FindJointOffset(StringId64 jointId) const
{
	PROFILE_AUTO(Animation);
	if (const NdGameObject* pGamObj = GetNdGameObject())
	{
		const FgAnimData* pAnimData				= pGamObj->GetAnimData();
		const SkelComponentDesc* pUseJointDescs = pAnimData->m_pJointDescs;
		if (m_useAnimatedSkel && pAnimData->m_animateSkelHandle.ToArtItem())
		{
			pUseJointDescs = pAnimData->m_animateSkelHandle.ToArtItem()->m_pJointDescs;
		}

		for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
		{
			if (jointId == pUseJointDescs[m_joints[i]].m_nameId)
				return i;
		}
	}
	else if (const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(m_skelId).ToArtItem())
	{
		const SkelComponentDesc* pUseJointDescs = pSkel->m_pJointDescs;
		for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
		{
			if (jointId == pUseJointDescs[m_joints[i]].m_nameId)
				return i;
		}
	}

	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int JointSet::FindJointIndex(StringId64 jointId) const
{
	I32F jointOffset = FindJointOffset(jointId);

	if (jointOffset >= kStartJointOffset)
		return m_joints[jointOffset];

	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointSet::IsAncestor(int ancestorOffset, I32F jointOffset) const
{
	int currOffset = jointOffset;
	while (currOffset >= 0)
	{
		if (currOffset == ancestorOffset)
			return true;

		currOffset = GetParentOffset(currOffset);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator& JointSet::GetJointLocLs(I32F jointOffset) const
{
	ANIM_ASSERT(m_jointData);
	ANIM_ASSERT(jointOffset >= 0 && jointOffset < m_numJoints);
	ANIM_ASSERT(m_jointData->m_jointValid);
	ANIM_ASSERT(m_jointData->m_jointValid[jointOffset]);

	return m_jointData->m_jointLocLs[jointOffset];
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator& JointSet::GetJointLocWs(I32F jointOffset)
{
	ANIM_ASSERT(m_jointData);
	ANIM_ASSERT(jointOffset >= 0 && jointOffset < m_numJoints);
	UpdateJointLocWs(jointOffset);
	ANIM_ASSERT(IsJointWorldSpaceValid(jointOffset));
	return m_jointData->m_jointLocWs[jointOffset];
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator JointSet::GetJointLocOs(I32F jointOffset)
{
	const NdGameObject* pGo = GetNdGameObject();

	ANIM_ASSERT(pGo);
	ANIM_ASSERT(m_jointData);
	ANIM_ASSERT(jointOffset >= 0 && jointOffset < m_numJoints);

	const Locator jointLocWs = GetJointLocWs(jointOffset);
	const Locator objectLocWs = pGo ? pGo->GetLocator() : kIdentity;

	return objectLocWs.UntransformLocator(jointLocWs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// versions that take the original joint index and convert to offset for you
const Locator& JointSet::GetJointLocLsIndex(I32F jointIndex) const
{
	const I32F offset = GetJointOffset(jointIndex);
	return GetJointLocLs(offset);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator& JointSet::GetJointLocWsIndex(I32F jointIndex)
{
	const I32F offset = GetJointOffset(jointIndex);
	return GetJointLocWs(offset);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator JointSet::GetJointLocOsIndex(I32F jointIndex)
{
	const I32F offset = GetJointOffset(jointIndex);
	return GetJointLocOs(offset);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator& JointSet::GetJointIdLocWs(StringId64 jointId)
{
	return GetJointLocWs(FindJointOffset(jointId));
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator JointSet::GetJointIdLocOs(StringId64 jointId)
{
	return GetJointLocOs(FindJointOffset(jointId));
}

///----------------------------------------------------------------------------------------///
// be cautious! remember to call UpdateAllJointLocsWs() before call GetRawJointLocWs(), or data is not updated!
// in most cases, you should call ::GetJointLocWs() instead.
const Locator JointSet::GetRawJointLocWs(I32F jointOffset) const
{
	ANIM_ASSERT(m_jointData);
	ANIM_ASSERT(jointOffset >= 0 && jointOffset < m_numJoints);
	ANIM_ASSERT(IsJointWorldSpaceValid(jointOffset));
	return m_jointData->m_jointLocWs[jointOffset];
}

/// --------------------------------------------------------------------------------------------------------------- ///
float JointSet::GetChainLength(int startJointOffset, int endJointOffset) const
{
	ANIM_ASSERT(m_jointData);
	ANIM_ASSERT(startJointOffset >= 0 && endJointOffset < m_numJoints && startJointOffset < endJointOffset);

	float len = 0.0f;

	int currOffset = endJointOffset;
	while (currOffset >= 0 && currOffset != startJointOffset)
	{
		len += m_jointData->m_boneLengths[currOffset];
		currOffset = GetParentOffset(currOffset);
	}

	ANIM_ASSERT(currOffset == startJointOffset);

	return len;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float JointSet::GetRootScale() const
{
	ANIM_ASSERT(m_jointData);
	return m_jointData->m_rootScale;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointSet::CheckValidBits(const OrbisAnim::ValidBits& validBits, U32F indexBase) const
{
	for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
	{
		const I32F bitToCheck = m_joints[i] - indexBase;

		if (bitToCheck < 0)
			return false;

		if (bitToCheck >= OrbisAnim::kJointGroupSize)
			return false;

		if (!validBits.IsBitSet(bitToCheck))
			return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointSet::ReadFromJointParams(const ndanim::JointParams* pJointParamsLs,
								   U32F indexBase,
								   U32F numJoints,
								   float rootScale,
								   const OrbisAnim::ValidBits* pValidBits /* = nullptr */,
								   const ndanim::JointParams* pDefaultJointParamsLs /* = nullptr */)
{
	PROFILE_AUTO(Animation);

	if (!pJointParamsLs || (0 == numJoints))
	{
		return false;
	}

	bool allJointsValid = true;
	ANIM_ASSERT(!m_jointData);

	m_jointData				   = NDI_NEW JointData;
	m_jointData->m_jointLocLs  = NDI_NEW Locator[m_numJoints];
	m_jointData->m_jointLocWs  = NDI_NEW Locator[m_numJoints];
	m_jointData->m_pJointScale = NDI_NEW Vector[m_numJoints];
	m_jointData->m_boneLengths = NDI_NEW float[m_numJoints];
	m_jointData->m_jointValid  = NDI_NEW bool[m_numJoints];

	AllocateWorldSpaceValidFlags(m_numJoints);

	m_jointData->m_hipAxis = kZero;
	m_jointData->m_hipDown = kZero;

	// If first joint is root, parent is Align
	if (m_joints[kParentJointOffset] < 0)
	{
		const NdGameObject* pGo = GetNdGameObject();

		m_jointData->m_jointLocLs[kParentJointOffset]  = pGo ? pGo->GetLocator() : Locator(kIdentity);

		bool parentJointValid = true;
		ndanim::JointParams localSpaceJoint = pJointParamsLs[0];
		if (pValidBits && !pValidBits->IsBitSet(0))
		{
			if (pDefaultJointParamsLs)
			{
				localSpaceJoint = pDefaultJointParamsLs[0];
			}
			else
			{
				parentJointValid = false;
				allJointsValid = false;
			}
		}

		m_jointData->m_jointValid[kParentJointOffset] = parentJointValid;
		if (parentJointValid)
		{
			m_jointData->m_pJointScale[kParentJointOffset] = localSpaceJoint.m_scale;
			ANIM_ASSERT(*((U32*)m_jointData->m_pJointScale + kParentJointOffset) != 0xffffffff);
		}
	}
	else
	{
		bool parentJointValid = false;
		const U32F jointIndex = m_joints[kParentJointOffset] - indexBase;
		if (jointIndex < numJoints)
		{
			parentJointValid = true;
			ndanim::JointParams localSpaceJoint = pJointParamsLs[jointIndex];
			if (pValidBits && !pValidBits->IsBitSet(jointIndex))
			{
				if (pDefaultJointParamsLs)
					localSpaceJoint = pDefaultJointParamsLs[jointIndex];
				else
					parentJointValid = false;
			}

			m_jointData->m_jointValid[kParentJointOffset]  = parentJointValid;
			if (parentJointValid)
			{
				m_jointData->m_jointLocLs[kParentJointOffset]  = Locator(localSpaceJoint.m_trans, localSpaceJoint.m_quat);
				m_jointData->m_pJointScale[kParentJointOffset] = localSpaceJoint.m_scale;
				ANIM_ASSERT(*((U32*)m_jointData->m_pJointScale + kParentJointOffset) != 0xffffffff);
			}
		}

		if (!parentJointValid)
		{
			allJointsValid = false;
		}
	}

	// Local space equals world space for the parent joint
	m_jointData->m_jointLocWs[kParentJointOffset] = m_jointData->m_jointLocLs[kParentJointOffset];

	m_jointData->m_rootScale = rootScale;

	for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
	{
		m_jointData->m_jointValid[i] = false;

		const U32F jointIndex = m_joints[i] - indexBase;

		if (jointIndex >= numJoints)
		{
			allJointsValid = false;
			continue;
		}

		ndanim::JointParams localSpaceJoint = pJointParamsLs[jointIndex];

		if (pValidBits && !pValidBits->IsBitSet(jointIndex))
		{
			if (pDefaultJointParamsLs)
			{
				localSpaceJoint = pDefaultJointParamsLs[jointIndex];
			}
			else
			{
				allJointsValid = false;
				continue;
			}
		}

		m_jointData->m_jointLocLs[i]  = Locator(localSpaceJoint.m_trans, Normalize(localSpaceJoint.m_quat));
		m_jointData->m_pJointScale[i] = localSpaceJoint.m_scale;
		ANIM_ASSERT(*((U32*)m_jointData->m_pJointScale + kParentJointOffset) != 0xffffffff);
		m_jointData->m_jointValid[i]  = true;
	}

	m_jointData->m_boneLengths[0] = 0.0f;

	for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
	{
		if (m_jointData->m_jointValid[i])
		{
			m_jointData->m_boneLengths[i] = m_jointData->m_rootScale * Length(m_jointData->m_jointLocLs[i].Pos() - Point(kZero));
		}
		else
		{
			m_jointData->m_boneLengths[i] = 0.0f;
		}
	}

	if (!allJointsValid)
	{
		DiscardJointCache();
	}
	
	return allJointsValid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::WriteJointParamsBlend(float blendAmount,
									 ndanim::JointParams* pJointParamsLs,
									 U32F indexBase,
									 U32F numJoints,
									 bool freeMemory /* = true */)
{
	PROFILE(Animation, JointSet_WriteJointParamsBlend);
	ANIM_ASSERT(m_jointData);

	if (blendAmount < 1.0f && blendAmount > 0.0f)
	{
		for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
		{
			const U32F outputIndex = m_joints[i] - indexBase;

			if (!m_jointData->m_jointValid[i])
			{
				continue;
			}

			ndanim::JointParams localSpaceJoint = pJointParamsLs[outputIndex];
			localSpaceJoint.m_trans = Lerp(localSpaceJoint.m_trans,
										   m_jointData->m_jointLocLs[i].GetTranslation(),
										   blendAmount);

			localSpaceJoint.m_quat = Slerp(localSpaceJoint.m_quat,
										   m_jointData->m_jointLocLs[i].GetRotation(),
										   blendAmount);

			localSpaceJoint.m_scale		= Lerp(localSpaceJoint.m_scale, m_jointData->m_pJointScale[i], blendAmount);
			pJointParamsLs[outputIndex] = localSpaceJoint;
		}
	}
	else if (blendAmount >= 1.0f)
	{
		for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
		{
			const U32F outputIndex = m_joints[i] - indexBase;

			if (!m_jointData->m_jointValid[i])
			{
				continue;
			}

			ndanim::JointParams& localSpaceJoint = pJointParamsLs[outputIndex];

			localSpaceJoint.m_trans = m_jointData->m_jointLocLs[i].GetTranslation();
			localSpaceJoint.m_quat	= m_jointData->m_jointLocLs[i].GetRotation();
			localSpaceJoint.m_scale = m_jointData->m_pJointScale[i];
		}
	}

	if (freeMemory)
	{
		DiscardJointCache();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::WriteJointValidBits(I32F jointOffset,
								   U32F indexBase,
								   OrbisAnim::ValidBits* pValidBits,
								   bool includingParents /* = true */)
{
	if (!pValidBits || !m_jointData || !m_jointData->m_jointValid)
	{
		return;
	}

	I32F iJoint = jointOffset;

	while ((iJoint >= kStartJointOffset) && (iJoint < m_numJoints))
	{
		const U32F outputIndex = m_joints[iJoint] - indexBase;

		if (m_jointData->m_jointValid[iJoint] && !pValidBits->IsBitSet(outputIndex))
		{
			pValidBits->SetBit(outputIndex);
		}

		if (includingParents)
		{
			iJoint = GetParentOffset(iJoint);
		}
		else
		{
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointSet::ReadJointCache()
{
	PROFILE(Animation, JointSet_ReadJointCache);

	const NdGameObject* pGo		= GetNdGameObject();
	const FgAnimData* pAnimData = pGo ? pGo->GetAnimData() : nullptr;

	if (!pAnimData || (m_numJoints <= 0))
	{
		return false;
	}

	m_jointData				   = NDI_NEW JointData;
	m_jointData->m_jointLocLs  = NDI_NEW Locator[m_numJoints];
	m_jointData->m_jointLocWs  = NDI_NEW Locator[m_numJoints];
	m_jointData->m_pJointScale = NDI_NEW Vector[m_numJoints];
	m_jointData->m_boneLengths = NDI_NEW float[m_numJoints];
	m_jointData->m_jointValid  = NDI_NEW bool[m_numJoints];

	AllocateWorldSpaceValidFlags(m_numJoints);

	m_jointData->m_hipAxis = kZero;
	m_jointData->m_hipDown = kZero;

	// If first joint is root, parent is Align
	if (m_joints[kParentJointOffset] < 0)
	{
		m_jointData->m_jointLocLs[kParentJointOffset]  = pGo->GetLocator();
		m_jointData->m_pJointScale[kParentJointOffset] = pAnimData->m_jointCache.GetJointParamsLs(0).m_scale;
		m_jointData->m_jointValid[kParentJointOffset]  = true;
	}
	else
	{
		m_jointData->m_jointLocLs[kParentJointOffset] = pAnimData->m_jointCache.GetJointLocatorWs(m_joints[kParentJointOffset]);
		m_jointData->m_pJointScale[kParentJointOffset] = pAnimData->m_jointCache.GetJointParamsLs(m_joints[kParentJointOffset]).m_scale;
		m_jointData->m_jointValid[kParentJointOffset] = true;
	}

	// Local space equals world space for the parent joint
	m_jointData->m_jointLocWs[kParentJointOffset] = m_jointData->m_jointLocLs[kParentJointOffset];

	m_jointData->m_rootScale = pAnimData->m_jointCache.GetJointParamsLs(0).m_scale.X();

	for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
	{
		int jointIndex = m_joints[i];

		const ndanim::JointParams& localSpaceJoint = pAnimData->m_jointCache.GetJointParamsLs(jointIndex);
		if (FALSE_IN_FINAL_BUILD(true))
		{
			ANIM_ASSERT(IsFinite(localSpaceJoint.m_trans));
			ANIM_ASSERT(IsFinite(localSpaceJoint.m_quat));
			if (ENABLE_JOINTSET_ASSERTS)
			{
				ANIM_ASSERT(IsNormal(localSpaceJoint.m_quat));
			}
			ANIM_ASSERT(IsFinite(localSpaceJoint.m_scale));
		}
		m_jointData->m_jointLocLs[i]  = Locator(localSpaceJoint.m_trans, localSpaceJoint.m_quat);
		m_jointData->m_pJointScale[i] = localSpaceJoint.m_scale;
		m_jointData->m_jointValid[i]  = true;
	}

	m_jointData->m_boneLengths[0] = 0.0f;

	for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
	{
		m_jointData->m_boneLengths[i] = m_jointData->m_rootScale*Length(m_jointData->m_jointLocLs[i].Pos() - Point(kZero));
	}

	// float goScale = pGo->GetScale();

	// Debug check bone lengths
	// float debugLen = 0.0f;
	// float err = 0.0f;
	// for (int i=0; i<m_numJoints-1; i++)
	//{
	//	Point posA = GetJointLocWs(i).GetTranslation();
	//	Point posB = GetJointLocWs(i+1).GetTranslation();
	//
	//	debugLen = Length(posA - posB);
	//	err = debugLen - m_jointData->m_boneLengths[i];
	//}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::WriteJointCache(bool reparentJointCache /* = true*/)
{
	PROFILE(Animation, JointSet_WriteJointCache);
	ANIM_ASSERT(m_jointData);

	NdGameObject* pGo	  = GetNdGameObject();
	FgAnimData* pAnimData = pGo ? pGo->GetAnimData() : nullptr;
	if (!pAnimData)
		return;

	ANIM_ASSERTF(pAnimData->GetAnimSourceMode(1) == FgAnimData::kAnimSourceModeJointParams,
				 ("'%s' Trying to write IK results when the object's animation config would ignore it", pGo->GetName()));

	for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
	{
		if (!m_jointData->m_jointValid[i])
			continue;

		const I32F jointIndex = m_joints[i];

		ndanim::JointParams localSpaceJoint = pAnimData->m_jointCache.GetJointParamsLs(jointIndex);
		localSpaceJoint.m_trans				= m_jointData->m_jointLocLs[i].GetTranslation();
		localSpaceJoint.m_quat				= m_jointData->m_jointLocLs[i].GetRotation();
		localSpaceJoint.m_scale				= m_jointData->m_pJointScale[i];
		if (FALSE_IN_FINAL_BUILD(true))
		{
			ANIM_ASSERT(IsFinite(localSpaceJoint.m_trans));
			ANIM_ASSERT(IsFinite(localSpaceJoint.m_quat));
			if (ENABLE_JOINTSET_ASSERTS)
			{
				ANIM_ASSERT(IsNormal(localSpaceJoint.m_quat));
			}
			ANIM_ASSERT(IsFinite(localSpaceJoint.m_scale));
		}
		pAnimData->m_jointCache.SetJointParamsLs(jointIndex, localSpaceJoint);
	}

	if (reparentJointCache)
		pAnimData->m_jointCache.UpdateWSSubPose(pGo->GetLocator(), m_joints[kStartJointOffset]);

	DiscardJointCache();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::WriteJointCacheBlend(float blendAmount, bool reparentJointCache /* = true*/)
{
	PROFILE(Animation, JointSet_WriteJointCache);
	ANIM_ASSERT(m_jointData);
	ANIM_ASSERT(blendAmount >= 0.0f && blendAmount <= 1.0f);

	NdGameObject* pGo	  = GetNdGameObject();
	FgAnimData* pAnimData = pGo ? pGo->GetAnimData() : nullptr;

	if (!pAnimData)
	{
		return; // process might've been killed
	}

	ANIM_ASSERTF(pAnimData->GetAnimSourceMode(1) == FgAnimData::kAnimSourceModeJointParams,
				 ("'%s' Trying to write IK results when the object's animation config would ignore it", pGo->GetName()));

	for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
	{
		if (!m_jointData->m_jointValid[i])
			continue;

		const I32F jointIndex = m_joints[i];

		ndanim::JointParams localSpaceJoint = pAnimData->m_jointCache.GetJointParamsLs(jointIndex);
		localSpaceJoint.m_trans = Lerp(localSpaceJoint.m_trans, m_jointData->m_jointLocLs[i].GetTranslation(), blendAmount);
		localSpaceJoint.m_quat = Slerp(localSpaceJoint.m_quat, m_jointData->m_jointLocLs[i].GetRotation(), blendAmount);
		localSpaceJoint.m_scale = Lerp(localSpaceJoint.m_scale, m_jointData->m_pJointScale[i], blendAmount);

		if (FALSE_IN_FINAL_BUILD(true))
		{
			ANIM_ASSERT(IsFinite(localSpaceJoint.m_trans));
			ANIM_ASSERT(IsFinite(localSpaceJoint.m_quat));
			if (ENABLE_JOINTSET_ASSERTS)
			{
				ANIM_ASSERT(IsNormal(localSpaceJoint.m_quat));
			}
			ANIM_ASSERT(IsFinite(localSpaceJoint.m_scale));
		}

		pAnimData->m_jointCache.SetJointParamsLs(jointIndex, localSpaceJoint);
	}

	//ANIM_ASSERT(Dot(pAnimData->m_jointCache.GetJointParamsLs(17).m_scale, Vector(1.0f, 1.0f, 1.0f)) > 0.9999f);

	if (reparentJointCache)
	{
		pAnimData->m_jointCache.UpdateWSSubPose(pGo->GetLocator(), m_joints[kStartJointOffset]);
	}

	DiscardJointCache();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::DiscardJointCache()
{
	PROFILE_AUTO(Animation);
	if (m_jointData != nullptr)
	{
		I64 jobId = ndjob::GetActiveJobId();

		// If we're allocating from single frame memory, DON'T DELETE THESE!
		// We're probably in a scoped allocator, so this is handled for us automatically!
		const Memory::Allocator* pSingleFrameAlloc = Memory::GetAllocator(kAllocSingleGameFrame);
		const Memory::Allocator* pScopedTempAlloc  = (jobId >= 0) ? GetScopedTempAllocatorForJob(jobId) : nullptr;
		const Memory::Allocator* pTopAlloc		   = Memory::TopAllocator();
		if (pTopAlloc != pSingleFrameAlloc && pTopAlloc != pScopedTempAlloc)
		{
			NDI_DELETE[] m_jointData->m_jointLocLs;
			NDI_DELETE[] m_jointData->m_jointLocWs;
			NDI_DELETE[] m_jointData->m_boneLengths;
			NDI_DELETE[] m_jointData->m_pJointScale;
			NDI_DELETE m_jointData;
		}

		m_jointData = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::SetParentJointLocWs(const Locator& loc)
{
	ANIM_ASSERT(m_jointData);
	m_jointData->m_jointLocLs[kParentJointOffset] = loc;
	m_jointData->m_jointLocWs[kParentJointOffset] = loc;
	InvalidateAllJoints();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Quat JointSet::RotateJointWs(I32F jointOffset, Quat_arg rot, bool invalidateChildren)
{
	ANIM_ASSERT(m_jointData && jointOffset >= kStartJointOffset);

	const Quat rotParent = GetParentLocWs(jointOffset).GetRotation();

	if (FALSE_IN_FINAL_BUILD(ENABLE_JOINTSET_ASSERTS))
	{
		ANIM_ASSERT(IsNormal(rot));
		ANIM_ASSERT(IsNormal(rotParent));
	}
	Quat premul = Normalize(Conjugate(rotParent) * rot * rotParent);
	if (FALSE_IN_FINAL_BUILD(ENABLE_JOINTSET_ASSERTS))
	{
		if (!IsNormal(premul))
		{
			MsgErr("\nQuaternion math failed!:\n");
			MsgErr("rot: %f %f %f %f\n", (float)rot.X(), (float)rot.Y(), (float)rot.Z(), (float)rot.W());
			MsgErr("IsNormal(rot): %s\n", IsNormal(rot) ? "true" : "false");
			MsgErr("Norm(rot): %f\n", (float)Norm(rot));
			MsgErr("rotParent: %f %f %f %f\n",
				   (float)rotParent.X(),
				   (float)rotParent.Y(),
				   (float)rotParent.Z(),
				   (float)rotParent.W());
			const Quat preNorm = Conjugate(rotParent) * rot * rotParent;
			MsgErr("preNorm: %f %f %f %f\n", (float)preNorm.X(), (float)preNorm.Y(), (float)preNorm.Z(), (float)preNorm.W());
		}
		ANIM_ASSERT(IsNormal(premul));
	}
	PreRotateJointLs(jointOffset, premul, invalidateChildren);
	return premul;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Quat JointSet::RotateJointWsIndex(I32F jointIndex, Quat_arg rot, bool invalidateChildren /* = true */)
{
	const I32F offset = GetJointOffset(jointIndex);
	return RotateJointWs(offset, rot, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Quat JointSet::RotateJointOs(I32F jointOffset, Quat_arg rot, bool invalidateChildren /* = true */)
{
	ANIM_ASSERT(m_jointData && jointOffset >= kStartJointOffset);

	const Quat rotParent = GetParentLocOs(jointOffset).GetRotation();

	if (FALSE_IN_FINAL_BUILD(ENABLE_JOINTSET_ASSERTS))
	{
		ANIM_ASSERT(IsNormal(rot));
		ANIM_ASSERT(IsNormal(rotParent));
	}

	Quat premul = Normalize(Conjugate(rotParent) * rot * rotParent);

	if (FALSE_IN_FINAL_BUILD(ENABLE_JOINTSET_ASSERTS))
	{
		if (!IsNormal(premul))
		{
			MsgErr("\nQuaternion math failed!:\n");
			MsgErr("rot: %f %f %f %f\n", (float)rot.X(), (float)rot.Y(), (float)rot.Z(), (float)rot.W());
			MsgErr("IsNormal(rot): %s\n", IsNormal(rot) ? "true" : "false");
			MsgErr("Norm(rot): %f\n", (float)Norm(rot));
			MsgErr("rotParent: %f %f %f %f\n",
				   (float)rotParent.X(),
				   (float)rotParent.Y(),
				   (float)rotParent.Z(),
				   (float)rotParent.W());
			const Quat preNorm = Conjugate(rotParent) * rot * rotParent;
			MsgErr("preNorm: %f %f %f %f\n", (float)preNorm.X(), (float)preNorm.Y(), (float)preNorm.Z(), (float)preNorm.W());
		}

		ANIM_ASSERT(IsNormal(premul));
	}

	PreRotateJointLs(jointOffset, premul, invalidateChildren);

	return premul;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Quat JointSet::RotateJointOsIndex(I32F jointIndex, Quat_arg rot, bool invalidateChildren /* = true */)
{
	const I32F offset = GetJointOffset(jointIndex);
	return RotateJointOs(offset, rot, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::PreRotateJointLs(I32F jointOffset, Quat_arg rot, bool invalidateChildren)
{
	ANIM_ASSERT(m_jointData && jointOffset >= kStartJointOffset);
	if (FALSE_IN_FINAL_BUILD(ENABLE_JOINTSET_ASSERTS))
	{
		ANIM_ASSERT(IsNormal(rot));
		ANIM_ASSERT(IsNormal(m_jointData->m_jointLocLs[jointOffset].Rot()));
	}
	const Quat jointRotLs = Normalize(rot * m_jointData->m_jointLocLs[jointOffset].Rot());
	if (FALSE_IN_FINAL_BUILD(ENABLE_JOINTSET_ASSERTS))
	{
		ANIM_ASSERT(IsNormal(jointRotLs));
	}
	m_jointData->m_jointLocLs[jointOffset].SetRotation(jointRotLs);

	InvalidateJoint(jointOffset, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::PostRotateJointLs(I32F jointOffset, Quat_arg rot, bool invalidateChildren)
{
	ANIM_ASSERT(m_jointData && jointOffset >= kStartJointOffset);
	if (FALSE_IN_FINAL_BUILD(ENABLE_JOINTSET_ASSERTS))
	{
		ANIM_ASSERT(IsNormal(rot));
		ANIM_ASSERT(IsNormal(m_jointData->m_jointLocLs[jointOffset].Rot()));
	}
	const Quat jointRotLs = Normalize(m_jointData->m_jointLocLs[jointOffset].Rot() * rot);
	if (FALSE_IN_FINAL_BUILD(ENABLE_JOINTSET_ASSERTS))
	{
		ANIM_ASSERT(IsNormal(jointRotLs));
	}
	m_jointData->m_jointLocLs[jointOffset].SetRotation(jointRotLs);

	InvalidateJoint(jointOffset, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::PostRotateJointLsIndex(I32F jointIndex, Quat_arg rot, bool invalidateChildren /* = true */)
{
	const I32F offset = GetJointOffset(jointIndex);
	PostRotateJointLs(jointIndex, rot, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector JointSet::TranslateJointWs(I32F jointOffset, Vector_arg trans, bool invalidateChildren)
{
	ANIM_ASSERT(m_jointData && jointOffset >= kStartJointOffset);

	const Locator& locParent = GetParentLocWs(jointOffset);
	Vector localTranslation  = Unrotate(locParent.Rot(), trans);

	m_jointData->m_jointLocLs[jointOffset].Move(localTranslation);

	InvalidateJoint(jointOffset, invalidateChildren);

	return localTranslation;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector JointSet::TranslateJointLs(I32F jointOffset, Vector_arg trans, bool invalidateChildren)
{
	ANIM_ASSERT(m_jointData && jointOffset >= kStartJointOffset);

	Quat jointRotLs = m_jointData->m_jointLocLs[jointOffset].Rot();

	Vector localTranslation = Rotate(jointRotLs, trans);
	m_jointData->m_jointLocLs[jointOffset].Move(localTranslation);

	InvalidateJoint(jointOffset, invalidateChildren);

	return localTranslation;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::TransformJointLs(I32F jointOffset, const Locator& loc, bool invalidateChildren)
{
	TranslateJointLs(jointOffset, loc.Pos() - Point(kZero), false);
	PostRotateJointLs(jointOffset, loc.Rot(), invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::SetJointRotationLs(I32F jointOffset, Quat_arg rot, bool invalidateChildren)
{
	ANIM_ASSERT(m_jointData && jointOffset >= kStartJointOffset);
	m_jointData->m_jointLocLs[jointOffset].SetRotation(rot);
	InvalidateJoint(jointOffset, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::SetJointLocWs(I32F jointOffset, const Locator& loc, bool invalidateChildren)
{
	ANIM_ASSERT(m_jointData && jointOffset >= kStartJointOffset);
	Locator parentLocWs					   = GetParentLocWs(jointOffset);
	Locator locLs						   = parentLocWs.UntransformLocator(loc);
	m_jointData->m_jointLocLs[jointOffset] = locLs;
	InvalidateJoint(jointOffset, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::SetJointLocOs(I32F jointOffset, const Locator& locOs, bool invalidateChildren /* = true */)
{
	const NdGameObject* pGo	  = GetNdGameObject();
	const Locator locWs		  = pGo->GetLocator().TransformLocator(locOs);
	const Locator parentLocWs = GetParentLocWs(jointOffset);
	const Locator locLs		  = parentLocWs.UntransformLocator(locWs);

	m_jointData->m_jointLocLs[jointOffset] = locLs;

	InvalidateJoint(jointOffset, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::SetJointLocLs(I32F jointOffset, const Locator& loc, bool invalidateChildren)
{
	ANIM_ASSERT(m_jointData && jointOffset >= kStartJointOffset);
	m_jointData->m_jointLocLs[jointOffset] = loc;
	InvalidateJoint(jointOffset, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::SetJointLocWsIndex(I32F jointIndex, const Locator& loc, bool invalidateChildren /* = true */)
{
	const I32F offset = GetJointOffset(jointIndex);
	SetJointLocWs(offset, loc, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::SetJointLocLsIndex(I32F jointIndex, const Locator& loc, bool invalidateChildren /* = true */)
{
	const I32F offset = GetJointOffset(jointIndex);
	SetJointLocLs(offset, loc, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::SetJointLocOsIndex(I32F jointIndex, const Locator& loc, bool invalidateChildren /* = true */)
{
	const I32F offset = GetJointOffset(jointIndex);
	SetJointLocOs(offset, loc, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::SetJointScaleIndex(I32F jointIndex, Vector_arg scale, bool invalidateChildren /* = true */)
{
	const I32F offset = GetJointOffset(jointIndex);
	ANIM_ASSERT(m_jointData);
	ANIM_ASSERT(offset >= 0 && offset < GetNumJoints());
	m_jointData->m_pJointScale[offset] = scale;

	InvalidateJoint(offset, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::TranslateRootWs(Vector_arg trans, bool invalidateChildren)
{
	ANIM_ASSERT(m_jointData);

	if (m_joints[kStartJointOffset] == 0)
	{
		// Chain begins with root joint, so translate it
		TranslateJointWs(kStartJointOffset, trans, invalidateChildren);
	}
	else
	{
		// Chain does not include root joint, so translate the parent world space
		m_jointData->m_jointLocLs[kParentJointOffset].Move(trans);
		m_jointData->m_jointLocWs[kParentJointOffset] = m_jointData->m_jointLocLs[kParentJointOffset];
		InvalidateJoint(kStartJointOffset, invalidateChildren);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::TranslateRootOs(Vector_arg trans, bool invalidateChildren /* = true */)
{
	const NdGameObject* pGo	  = GetNdGameObject();
	const Locator objectLocWs = pGo->GetLocator();

	const Vector transWs = objectLocWs.TransformVector(trans);

	TranslateRootWs(transWs, invalidateChildren);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator JointSet::GetBindPoseLocLs(I32F jointOffset) const
{
	ANIM_ASSERT(jointOffset >= kStartJointOffset && jointOffset < m_numJoints);

	const ndanim::JointHierarchy* pJointHier	= GetNdGameObject()->GetAnimData()->m_pSkeleton;
	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pJointHier);
	const int jointIndex						= GetJointIndex(jointOffset);
	return Locator(pDefaultParamsLs[jointIndex].m_trans, pDefaultParamsLs[jointIndex].m_quat);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator JointSet::GetBindPoseLocWs(I32F jointOffset)
{
	Locator bindPosLocWs;
	UpdateJointLocWs(GetParentLocWs(jointOffset), GetBindPoseLocLs(jointOffset), &bindPosLocWs);

	return bindPosLocWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::GetInverseBindPoseJointXform(I32F jointOffset, Transform& outXform) const
{
	ANIM_ASSERT(m_jointData);
	ANIM_ASSERT(jointOffset >= kStartJointOffset && jointOffset < m_numJoints);

	const NdGameObject* pGo = GetNdGameObject();
	pGo->GetAnimData()->m_jointCache.GetInverseBindPoseJointXform(outXform, m_joints[jointOffset]);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::GetBindPoseJointXform(I32F jointOffset, Transform& outXform) const
{
	Transform invBindPoseXform;
	GetInverseBindPoseJointXform(jointOffset, invBindPoseXform);
	outXform = Inverse(invBindPoseXform);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator& JointSet::GetParentLocWs(I32F jointOffset)
{
	ANIM_ASSERT(m_jointData);
	ANIM_ASSERT(jointOffset >= kStartJointOffset && jointOffset < m_numJoints);

	return GetJointLocWs(GetParentOffset(jointOffset));
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator JointSet::GetParentLocOs(I32F jointOffset)
{
	ANIM_ASSERT(m_jointData);
	ANIM_ASSERT(jointOffset >= kStartJointOffset && jointOffset < m_numJoints);

	return GetJointLocOs(GetParentOffset(jointOffset));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::ApplyConstraint(JointConstraintData* ikData, I32F jointOffset, float* debugTextY)
{
	ANIM_ASSERT(ikData);

	if (ikData->m_coneNumBoundaryPoints == 0)
		return;

	ConstraintFuncResult result;
	JointLimits::ApplyJointLimit(this, ikData, nullptr, jointOffset, &result);

#ifndef FINAL_BUILD
	if (debugTextY && (ikData->m_debugFlags & DC::kIkDebugFlagsDraw))
	{
		const int numPoints = ikData->m_coneNumBoundaryPoints;

		Locator jointLocPs = GetJointLocLs(jointOffset);

		float debugNewTwist = JointLimits::ComputeTwistRel(jointLocPs.Rot(), ikData->m_bindPoseLs.Rot(), ikData->m_visPointTwistCenter);
		Vector debugNewDir = GetLocalZ(jointLocPs.Rot());

		float mirrorSign = (ikData->m_debugFlags & DC::kIkDebugFlagsMirror) ? -1.0f : 1.0f;
		float coneLen	= 0.05f;

		Locator bindPoseWs;
		UpdateJointLocWs(GetParentLocWs(jointOffset), ikData->m_bindPoseLs, &bindPoseWs);

		if (ikData->m_debugFlags & DC::kIkDebugFlagsBindPoseLoc)
			g_prim.Draw(DebugCoordAxes(bindPoseWs, coneLen, PrimAttrib(kPrimWireFrame)));

		if (ikData->m_debugFlags & DC::kIkDebugFlagsConeVisPoint)
		{
			Vector coneCenterDirWs = mirrorSign * GetParentLocWs(jointOffset).TransformVector(ikData->m_coneVisPointPs);
			Point coneCenterPtWs   = bindPoseWs.Pos() + coneLen * coneCenterDirWs;
			g_prim.Draw(DebugLine(bindPoseWs.Pos(), coneCenterPtWs, kColorBlue, 1.0f, PrimAttrib(kPrimWireFrame)));
		}

		if (ikData->m_debugFlags & DC::kIkDebugFlagsJointDir)
		{
			Color jointFwdColor = result.m_inCone ? kColorWhite : kColorRed;
			Vector jointFwdWs   = mirrorSign * GetParentLocWs(jointOffset).TransformVector(result.m_jointFwdPs);
			g_prim.Draw(DebugLine(bindPoseWs.Pos(), coneLen * jointFwdWs, jointFwdColor, 2.0f, PrimAttrib(kPrimWireFrame)));

			// if (!inCone)
			//{
			//	Vector boundaryDirWs = mirrorSign*GetParentLocWs(jointOffset).TransformVector(boundaryDirPs);
			//	g_prim.Draw( DebugLine(bindPoseWs.Pos(), coneLen*boundaryDirWs, kColorWhite, 1.0f, PrimAttrib(
			//kPrimWireFrame)) );
			//}
		}

		if (ikData->m_debugFlags & DC::kIkDebugFlagsConeAxes)
		{
			Vector coneDirXWs = mirrorSign * GetParentLocWs(jointOffset).TransformVector(GetLocalX(ikData->m_coneRotPs));
			Vector coneDirYWs = mirrorSign * GetParentLocWs(jointOffset).TransformVector(GetLocalY(ikData->m_coneRotPs));
			g_prim.Draw(DebugLine(bindPoseWs.Pos() - 0.03f * coneDirXWs,
								  bindPoseWs.Pos() + 0.03f * coneDirXWs,
								  kColorRed,
								  1.0f,
								  PrimAttrib(kPrimWireFrame)));
			g_prim.Draw(DebugLine(bindPoseWs.Pos() - 0.03f * coneDirYWs,
								  bindPoseWs.Pos() + 0.03f * coneDirYWs,
								  kColorGreen,
								  1.0f,
								  PrimAttrib(kPrimWireFrame)));
		}

		if (ikData->m_debugFlags & DC::kIkDebugFlagsCone)
		{
			for (int p = 0; p < numPoints; p++)
			{
				Vector pointDirAWs = mirrorSign*GetParentLocWs(jointOffset).TransformVector(ikData->m_coneBoundaryPointsPs[p]);
				Vector pointDirBWs = mirrorSign*GetParentLocWs(jointOffset).TransformVector(ikData->m_coneBoundaryPointsPs[(p+1)%numPoints]);

				Point origin   = bindPoseWs.Pos();
				Point pointAWs = origin + coneLen * pointDirAWs;
				Point pointBWs = origin + coneLen * pointDirBWs;

				Color lineColor = (p == result.m_sliceNum) ? kColorYellow : kColorOrange;

				g_prim.Draw(DebugLine(pointAWs, pointBWs, lineColor, 1.0f, PrimAttrib(kPrimWireFrame)));
				g_prim.Draw(DebugTriangle(origin, pointAWs, pointBWs, kColorGrayTrans, PrimAttrib(kPrimWireFrame)));
			}
		}

		Vector coneFwdWs  = mirrorSign * GetParentLocWs(jointOffset).TransformVector(GetLocalZ(ikData->m_coneRotPs));
		Vector coneSideWs = mirrorSign * GetParentLocWs(jointOffset).TransformVector(GetLocalX(ikData->m_coneRotPs));

		float bindPoseTwist = JointLimits::ComputeTwistRel(ikData->m_bindPoseLs.Rot(), Quat(kIdentity), 0.0f);
		float twist			= result.m_targetTwist + ikData->m_visPointTwistCenter - bindPoseTwist;

		if (ikData->m_debugFlags & DC::kIkDebugFlagsConeTwist)
		{
			Quat rotMin = QuatFromAxisAngle(coneFwdWs, ikData->m_visPointTwistCenter - ikData->m_visPointTwistRange);
			Quat rotMax = QuatFromAxisAngle(coneFwdWs, ikData->m_visPointTwistCenter + ikData->m_visPointTwistRange);
			Vector twistMinSideDirWs = Rotate(rotMin, coneSideWs);
			Vector twistMaxSideDirWs = Rotate(rotMax, coneSideWs);
			g_prim.Draw(DebugLine(bindPoseWs.Pos(),
								  bindPoseWs.Pos() + 0.04f * twistMinSideDirWs,
								  kColorMagenta,
								  1.0f,
								  PrimAttrib(kPrimWireFrame)));
			g_prim.Draw(DebugLine(bindPoseWs.Pos(),
								  bindPoseWs.Pos() + 0.04f * twistMaxSideDirWs,
								  kColorCyan,
								  1.0f,
								  PrimAttrib(kPrimWireFrame)));

			Quat rotCurr		= QuatFromAxisAngle(coneFwdWs, twist);
			Vector currTwistDir = Rotate(rotCurr, coneSideWs);
			g_prim.Draw( DebugLine(bindPoseWs.Pos(), bindPoseWs.Pos() + 0.03f*currTwistDir, kColorYellow, 1.0f, PrimAttrib( kPrimWireFrame)) );
		}

		if (ikData->m_debugFlags & DC::kIkDebugFlagsText)
		{
			char buf[256];
			sprintf(buf,
					"%s: Twist - %.1f",
					DevKitOnly_StringIdToString(GetJointId(jointOffset)),
					RADIANS_TO_DEGREES(debugNewTwist));
			g_prim.Draw(DebugString2D(Vec2(1000.0f, *debugTextY), kDebug2DLegacyCoords, buf, kColorWhite, 0.7f));
			*debugTextY += 12.0f;
		}
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::ApplyConstraints(bool debugDraw)
{
	PROFILE(Animation, SolveIK_ApplyConstraints);

	ANIM_ASSERT(m_jointData);

	float debugTextY	 = 60.0f;
	float* debugTextYPtr = debugDraw ? &debugTextY : nullptr;

	for (I32F jointOffset = kStartJointOffset; jointOffset < m_numJoints; jointOffset++)
	{
		JointConstraintData* ikData = &m_ikData->m_ikJointData[jointOffset];
		ApplyConstraint(ikData, jointOffset, debugTextYPtr);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::ApplyConstraints(JacobianMap* pJMap, bool debugDraw)
{
	PROFILE(Animation, SolveIK_ApplyConstraints);

	ANIM_ASSERT(m_jointData);

	float debugTextY	 = 60.0f;
	float* debugTextYPtr = debugDraw ? &debugTextY : nullptr;

	for (int i = 0; i < pJMap->m_numUniqueJoints; i++)
	{
		I32F jointOffset			= pJMap->m_uniqueJoints[i].m_jointOffset;
		JointConstraintData* ikData = &m_ikData->m_ikJointData[jointOffset];
		ApplyConstraint(ikData, jointOffset, debugTextYPtr);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::RetargetJointSubsets(const I32F* aiEndJointOffsets,
									U32F numEndJoints,
									const ArtItemSkeleton* pSrcSkel,
									const ArtItemSkeleton* pDstSkel)
{
	PROFILE_AUTO(Animation);

	ANIM_ASSERT(m_jointData);

	if (!pSrcSkel || !pDstSkel)
		return;

	const SkelTable::RetargetEntry* pRetarget = SkelTable::LookupRetarget(pSrcSkel->m_skelId, pDstSkel->m_skelId);

	if (!pRetarget)
		return;

	bool* aRetargted = STACK_ALLOC(bool, m_numJoints);
	for (U32F i = 0; i < m_numJoints; ++i)
	{
		aRetargted[i] = false;
	}

	const ndanim::JointParams* pSrcDefParamsLs = ndanim::GetDefaultJointPoseTable(pSrcSkel->m_pAnimHierarchy);
	const ndanim::JointParams* pDstDefParamsLs = ndanim::GetDefaultJointPoseTable(pDstSkel->m_pAnimHierarchy);

	for (U32F iEnd = 0; iEnd < numEndJoints; ++iEnd)
	{
		I32F iRetargetOffset = aiEndJointOffsets[iEnd];

		while (iRetargetOffset >= kStartJointOffset)
		{
			if (aRetargted[iRetargetOffset])
				break;

			const I32F iSrcJointIndex = m_joints[iRetargetOffset];

			if (const SkelTable::JointRetarget* pJointRetarget = SkelTable::FindJointRetarget(pRetarget, iSrcJointIndex))
			{
				ndanim::JointParams retargetedParamsLs;

				const ndanim::JointParams srcJointLs = { m_jointData->m_pJointScale[iRetargetOffset],
														 m_jointData->m_jointLocLs[iRetargetOffset].Rot(),
														 m_jointData->m_jointLocLs[iRetargetOffset].Pos() };

				const ndanim::JointParams& srcDefJointLs = pSrcDefParamsLs[pJointRetarget->m_srcIndex];
				const ndanim::JointParams& dstDefJointLs = pDstDefParamsLs[pJointRetarget->m_destIndex];

				if (DoJointRetarget(*pJointRetarget, srcJointLs, srcDefJointLs, dstDefJointLs, false, &retargetedParamsLs))
				{
					const Locator retargetedJointLs = Locator(retargetedParamsLs.m_trans, retargetedParamsLs.m_quat);

					SetJointLocLs(iRetargetOffset, retargetedJointLs, false);

					m_jointData->m_pJointScale[iRetargetOffset] = retargetedParamsLs.m_scale;
				}
			}

			InvalidateJoint(iRetargetOffset, false);
			aRetargted[iRetargetOffset] = true;
			iRetargetOffset = GetParentOffset(iRetargetOffset);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::UpdateJointLocWs(const Locator& parentLocWs, const Locator jointLocLs, Locator* jointLocOutWs)
{
	const Point scaledTrans = Point(m_jointData->m_rootScale * jointLocLs.Pos().GetVec4());
	const Locator scaledJointLocLs(scaledTrans, jointLocLs.Rot());
	Locator locWs = parentLocWs.TransformLocator(scaledJointLocLs);

	ANIM_ASSERT(IsReasonable(scaledTrans));
	ANIM_ASSERT(IsReasonable(scaledJointLocLs));
	ANIM_ASSERT(Dist(locWs.Pos(), kOrigin) < 1e8f);

	// to fix accumulated error
	locWs.SetRotation(Normalize(locWs.GetRotation()));

	ANIM_ASSERT(IsReasonable(locWs));

	*jointLocOutWs = locWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::DebugDrawJoints(bool drawJoints/* = true*/,
							bool drawJointDetails/* = false*/,
							bool drawSkeleton/* = false*/,
							Color drawSkeletonColor/* = kColorWhite*/,
							DebugPrimTime tt/* = kPrimDuration1FramePauseable*/)
{
	STRIP_IN_FINAL_BUILD;

	for (U32F ii = kStartJointOffset; ii < m_numJoints; ++ii)
	{
		if (!m_jointData->m_jointValid[ii])
		{
			continue;
		}

		const StringId64 jointId = GetJointId(ii);
		const Locator& jointLocWs = GetJointLocWs(ii);

		if (drawJoints)
		{
			StringBuilder<256> desc("%s", DevKitOnly_StringIdToString(jointId));
			if (drawJointDetails)
			{
				desc.append_format(" [%d (offset: %d)]", m_joints[ii], ii);
			}

			g_prim.Draw(DebugCoordAxesLabeled(jointLocWs,
											desc.c_str(),
											0.1f,
											kPrimEnableHiddenLineAlpha,
											2.0f,
											kColorWhiteTrans,
											0.5f),
						tt);
		}

		if (drawSkeleton)
		{
			const int parentOffset = GetParentOffset(ii);
			if (parentOffset >= kStartJointOffset &&
				m_jointData->m_jointValid[parentOffset])
			{
				const Locator& parentLocWs = GetJointLocWs(parentOffset);
				g_prim.Draw(DebugLine(jointLocWs.GetPosition(),
									parentLocWs.GetPosition(),
									drawSkeletonColor,
									2.0f,
									kPrimEnableHiddenLineAlpha),
							tt);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSet::Validate() const
{
	if (FALSE_IN_FINAL_BUILD(true))
	{
		for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
		{
			if (!m_jointData->m_jointValid[i])
				continue;

			if (FALSE_IN_FINAL_BUILD(true))
			{
				ANIM_ASSERT(IsFinite(m_jointData->m_jointLocLs[i].GetTranslation()));
				ANIM_ASSERT(IsFinite(m_jointData->m_jointLocLs[i].GetRotation()));
				if (ENABLE_JOINTSET_ASSERTS)
				{
					ANIM_ASSERT(IsNormal(m_jointData->m_jointLocLs[i].GetRotation()));
				}
				ANIM_ASSERT(IsFinite(m_jointData->m_pJointScale[i]));
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// JointChain
/// --------------------------------------------------------------------------------------------------------------- ///
JointChain::JointChain()
{
	m_type					 = kTypeChain;
	m_jointLocWsInvalidStart = -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointChain::Init(NdGameObject* pGo)
{
	ANIM_ASSERT(m_numJoints == 0);
	ANIM_ASSERT(pGo);

	JointSet::Init(pGo);

	const FgAnimData* pAnimData = pGo->GetAnimData();

	int startJointIndex = 0;
	int endJointIndex   = pAnimData->m_jointCache.GetNumAnimatedJoints() - 1;

	StringId64 startJointId = pAnimData->m_pJointDescs[startJointIndex].m_nameId;
	StringId64 endJointId   = pAnimData->m_pJointDescs[endJointIndex].m_nameId;

	return Init(pGo, startJointId, endJointId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointChain::Init(NdGameObject* pGo, StringId64 startJointId, StringId64 endJointId)
{
	ANIM_ASSERT(m_numJoints == 0);
	ANIM_ASSERT(pGo);

	JointSet::Init(pGo);

	const FgAnimData* pAnimData = pGo->GetAnimData();

	const I32F startJointIndex = pGo->FindJointIndex(startJointId);
	const I32F endJointIndex   = pGo->FindJointIndex(endJointId);

	if ((startJointIndex < 0) || (endJointIndex < 0))
		return false;

	const I32F parentJointIndex = pAnimData->m_jointCache.GetParentJoint(startJointIndex);

	// Ensure there's a link from start to end and count the joints
	I32F numJoints = 1;
	I32F currJoint = endJointIndex;
	while (currJoint != startJointIndex)
	{
		if (currJoint < 0)
			return false;

		numJoints++;
		currJoint = pAnimData->m_jointCache.GetParentJoint(currJoint);
	}

	// We also need to store the parent of the first joint
	numJoints++;
	ANIM_ASSERT(numJoints <= 128);

	m_numJoints = (I16)numJoints;
	m_joints	= NDI_NEW I16[m_numJoints];

	int currIndex = m_numJoints - 1;
	currJoint	 = endJointIndex;
	while (currJoint != -1)
	{
		m_joints[currIndex] = (I16)currJoint;

		if (currJoint == startJointIndex)
		{
			break;
		}

		currIndex--;
		currJoint = pAnimData->m_jointCache.GetParentJoint(currJoint);
	}

	m_joints[kParentJointOffset] = (I16)parentJointIndex;

	for (int i = 0; i < m_numJoints - 1; i++)
	{
		ANIM_ASSERT(m_joints[i] < m_joints[i + 1]);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static I32F GetParentJoint(const ArtItemSkeleton* pSkeleton, I32F iJoint)
{
	if (!pSkeleton)
		return -1;

	return ndanim::GetParentJoint(pSkeleton->m_pAnimHierarchy, iJoint);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointChain::Init(const ArtItemSkeleton* pSkeleton, StringId64 startJoint, StringId64 endJoint)
{
	ANIM_ASSERT(m_numJoints == 0);

	JointSet::Init(nullptr);
	m_skelId = pSkeleton->m_skelId;

	int startJointIndex = FindJoint(pSkeleton->m_pJointDescs, pSkeleton->m_numGameplayJoints, startJoint);
	int endJointIndex   = FindJoint(pSkeleton->m_pJointDescs, pSkeleton->m_numGameplayJoints, endJoint);
	ANIM_ASSERT(startJointIndex >= 0 && endJointIndex >= 0);

	int parentJointIndex = GetParentJoint(pSkeleton, startJointIndex);

	// Ensure there's a link from start to end and count the joints
	int numJoints = 1;
	int currJoint = endJointIndex;
	while (currJoint != startJointIndex)
	{
		if (currJoint < 0)
			return false;

		numJoints++;
		currJoint = GetParentJoint(pSkeleton, currJoint);
	}

	// We also need to store the parent of the first joint
	numJoints++;
	ANIM_ASSERT(numJoints < 128);

	m_numJoints = (I16)numJoints;
	m_joints	= NDI_NEW I16[m_numJoints];

	int currIndex = m_numJoints - 1;
	currJoint	 = endJointIndex;
	while (currJoint != -1)
	{
		m_joints[currIndex] = (I16)currJoint;

		if (currJoint == startJointIndex)
		{
			break;
		}

		currIndex--;
		currJoint = GetParentJoint(pSkeleton, currJoint);
	}

	m_joints[kParentJointOffset] = (I16)parentJointIndex;

	for (int i = 0; i < m_numJoints - 1; i++)
	{
		ANIM_ASSERT(m_joints[i] < m_joints[i + 1]);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int JointChain::GetParentOffset(I32F jointOffset) const
{
	return jointOffset - 1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointChain::InvalidateChildren(int rootOffset)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointChain::InvalidateAllJoints()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointChain::UpdateAllJointLocsWs(int rootOffset)
{
	PROFILE_AUTO(Animation);
	for (U32F i = rootOffset; i < m_numJoints; ++i)
	{
		if (!m_jointData->m_jointValid[i])
			continue;

		const I32F parentOffset = i - 1;
		ANIM_ASSERT(parentOffset >= 0);
		JointSet::UpdateJointLocWs(m_jointData->m_jointLocWs[parentOffset],
								   m_jointData->m_jointLocLs[i],
								   &m_jointData->m_jointLocWs[i]);

		SetJointWorldSpaceValid(i);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointChain::UpdateJointLocWsInternal(I32F jointOffset, const Locator& parentLocWs)
{
	ANIM_ASSERT(m_jointData && jointOffset >= kStartJointOffset);
	ANIM_ASSERT(m_jointLocWsInvalidStart == jointOffset);		// Should only be called if this is the first world space locator that's invalid

	JointSet::UpdateJointLocWs(parentLocWs,
							   m_jointData->m_jointLocLs[jointOffset],
							   &m_jointData->m_jointLocWs[jointOffset]);

	SetJointWorldSpaceValid(jointOffset);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointChain::UpdateJointLocWs(I32F jointOffset)
{
	ANIM_ASSERT(m_jointData && m_jointLocWsInvalidStart > 0);

	// Update all world space locators from the first invalid one, until the one specified
	for (int i = m_jointLocWsInvalidStart; i <= jointOffset; i++)
	{
		UpdateJointLocWsInternal(i, m_jointData->m_jointLocWs[i - 1]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointChain::InvalidateJoint(I32F jointOffset, bool invalidateChildren)
{
	ANIM_ASSERT(m_jointData);
	m_jointLocWsInvalidStart = Min(jointOffset, m_jointLocWsInvalidStart);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointChain::AllocateWorldSpaceValidFlags(int numJoints)
{
	ANIM_ASSERT(numJoints > 0);
	m_jointLocWsInvalidStart = kStartJointOffset;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointChain::IsJointWorldSpaceValid(I32F jointOffset) const
{
	return jointOffset < m_jointLocWsInvalidStart;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointChain::SetJointWorldSpaceValid(I32F jointOffset)
{
	m_jointLocWsInvalidStart = jointOffset + 1;
#ifndef FINAL_BUILD
	ANIM_ASSERT(IsNormal(m_jointData->m_jointLocWs[jointOffset].GetRotation()));
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointChain::DebugPrintJointRotation() const
{
	STRIP_IN_FINAL_BUILD;

	const FgAnimData* pAnimData = GetNdGameObject()->GetAnimData();

	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pAnimData->m_pSkeleton);

	for (U32F i = kStartJointOffset; i < m_numJoints; ++i)
	{
		int jointIndex = GetJointIndex(i);

		ndanim::JointParams bindParam = pDefaultParamsLs[jointIndex];

		Locator bindLoc(bindParam.m_trans, bindParam.m_quat);
		Vector bindEuler;
		QuaternionToEulerAngles(&bindEuler, bindLoc.Rot());

		const Locator& currPose = GetJointLocLs(i);
		Vector currEuler;
		QuaternionToEulerAngles(&currEuler, currPose.Rot());

		Vector diff = currEuler - bindEuler;

		float x, y, z;
		x = currEuler.X();
		y = currEuler.Y();
		z = currEuler.Z();

		char buf[256];
		sprintf(buf, "%s: ", DevKitOnly_StringIdToString(GetJointId(i)));
		while (strlen(buf) < 14)
			strcat(buf, " ");

		char buf2[256];
		sprintf(buf2, "(%.1f, %.1f, %.1f)", RADIANS_TO_DEGREES(x), RADIANS_TO_DEGREES(y), RADIANS_TO_DEGREES(z));
		strcat(buf, buf2);

		// g_prim.Draw( DebugString2D(Vec2(900.0f, 300.0f + i*12.0f), kDebug2DLegacyCoords, buf, kColorWhite, 0.7f) );

		// m_jointData->m_jointLocLs[i] = bindLoc;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// JointTree
/// --------------------------------------------------------------------------------------------------------------- ///
JointTree::JointTree()
{
	m_type			  = kTypeTree;
	m_parentOffset	= nullptr;
	m_childEndOffset  = nullptr;
	m_jointLocWsValid = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JointTree::~JointTree()
{
	if (m_parentOffset)
		NDI_DELETE[] m_parentOffset;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AddJointAndChildren(const ArtItemSkeleton* pSkeleton,
						 int parentOffset,
						 I16* jointsOrdered,
						 I16* jointsFinal,
						 I16* jointsFinalParent,
						 I16* jointsFinalChildEnd,
						 int orderedOffset,
						 int& finalOffset,
						 int numJoints)
{
	int jointIndex  = jointsOrdered[orderedOffset];
	int jointOffset = finalOffset++;

	jointsFinal[jointOffset]	   = jointIndex;
	jointsFinalParent[jointOffset] = parentOffset;

	jointsOrdered[orderedOffset] = -1;

	for (int i = orderedOffset + 1; i < numJoints; i++)
	{
		if (jointsOrdered[i] < 0)
			continue;

		int parentIndex = GetParentJoint(pSkeleton, jointsOrdered[i]);
		if (jointIndex == parentIndex)
		{
			AddJointAndChildren(pSkeleton,
								jointOffset,
								jointsOrdered,
								jointsFinal,
								jointsFinalParent,
								jointsFinalChildEnd,
								i,
								finalOffset,
								numJoints);
		}
	}

	jointsFinalChildEnd[jointOffset] = finalOffset - 1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AddJointAndChildren(const FgAnimData* pAnimData,
						 bool useAnimatedSkel,
						 int parentOffset,
						 I16* jointsOrdered,
						 I16* jointsFinal,
						 I16* jointsFinalParent,
						 I16* jointsFinalChildEnd,
						 int orderedOffset,
						 int& finalOffset,
						 int numJoints)
{
	const ArtItemSkeleton* pSkelToUse = pAnimData->m_curSkelHandle.ToArtItem();
	if (pAnimData->m_animateSkelHandle.ToArtItem() && useAnimatedSkel)
		pSkelToUse = pAnimData->m_animateSkelHandle.ToArtItem();

	AddJointAndChildren(pSkelToUse,
						parentOffset,
						jointsOrdered,
						jointsFinal,
						jointsFinalParent,
						jointsFinalChildEnd,
						orderedOffset,
						finalOffset,
						numJoints);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointTree::Init(NdGameObject* pGo, StringId64 rootJoint, bool useAnimatedSkel, int numEndJoints, ...)
{
	StringId64* endJointNames = STACK_ALLOC(StringId64, numEndJoints);

	va_list vargs;
	va_start(vargs, numEndJoints);
	for (I32F i = 0; i < numEndJoints; ++i)
	{
		const StringId64Storage hashVal = va_arg(vargs, StringId64Storage);
		endJointNames[i]				= StringId64(hashVal);
	}
	va_end(vargs);

	return Init(pGo, rootJoint, useAnimatedSkel, numEndJoints, endJointNames);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointTree::Init(NdGameObject* pGo,
					 StringId64 rootJoint,
					 bool useAnimatedSkel,
					 int numEndJoints,
					 const StringId64* endJointNames)
{
	ANIM_ASSERT(m_numJoints == 0);
	ANIM_ASSERT(pGo);
	ANIM_ASSERT(numEndJoints > 0);

	JointSet::Init(pGo);
	const FgAnimData* pAnimData = pGo->GetAnimData();

	const ArtItemSkeleton* pSkelToUse = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem();
	if (pAnimSkel && useAnimatedSkel)
	{
		pSkelToUse = pAnimSkel;
	}

	m_useAnimatedSkel = useAnimatedSkel;

	return DoInit(pSkelToUse, rootJoint, numEndJoints, endJointNames);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointTree::Init(const ArtItemSkeleton* pSkeleton,
					 StringId64 rootJoint,
					 int numEndJoints,
					 const StringId64* endJointNames)
{
	ANIM_ASSERT(m_numJoints == 0);
	ANIM_ASSERT(numEndJoints > 0);

	JointSet::Init(nullptr);

	m_useAnimatedSkel = false;

	return DoInit(pSkeleton, rootJoint, numEndJoints, endJointNames);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointTree::DoInit(const ArtItemSkeleton* pSkeleton,
					   StringId64 rootJoint,
					   int numEndJoints,
					   const StringId64* endJointNames)
{
	ANIM_ASSERT(m_numJoints == 0);
	ANIM_ASSERT(numEndJoints > 0);

	m_skelId = pSkeleton->m_skelId;

	const int kMaxJoints = ndanim::GetNumJointsInSegment(pSkeleton->m_pAnimHierarchy, 0);

	bool* jointsFound = STACK_ALLOC(bool, kMaxJoints);

	memset(jointsFound, 0, kMaxJoints * sizeof(jointsFound[0]));

	I16 rootJointIndex = -1;
	I32 numJoints = 0;

	// If no root joint specified, it will include all joints up the tree, and can support multiple root joints,
	// for example procedural joints sometimes aren't parented to "root" and are in a sense their own root joint.
	if (rootJoint != INVALID_STRING_ID_64)
	{
		rootJointIndex = FindJoint(pSkeleton->m_pJointDescs, pSkeleton->m_numGameplayJoints, rootJoint);

		if (rootJointIndex < 0)
			return false;

		jointsFound[rootJointIndex] = true;
		numJoints = 1;
	}

	I32 parentJointIndex = GetParentJoint(pSkeleton, rootJointIndex);

	// Ensure rootJoint is the root of all end joints, and find all the joints in the tree
	for (I32 i = 0; i < numEndJoints; i++)
	{
		StringId64 endJointId = endJointNames[i];
		I32 endJointIndex	 = FindJoint(pSkeleton->m_pJointDescs, pSkeleton->m_numGameplayJoints, endJointId);

		if (rootJoint != INVALID_STRING_ID_64 && endJointIndex < rootJointIndex)
			continue;

		if (jointsFound[endJointIndex])
		{
			// duplicate in the list, already found and added
			continue;
		}

		int currIndex = endJointIndex;

		while (currIndex != rootJointIndex)
		{
			// Ensure all the root is actually the parent of all joints
			ANIM_ASSERT(currIndex >= rootJointIndex);
			if (currIndex == -1)
				break;

			if (!jointsFound[currIndex])
				numJoints++;

			jointsFound[currIndex] = true;
			currIndex = GetParentJoint(pSkeleton, currIndex);
		}
	}

	// Gather list of used joints
	I16* joints   = STACK_ALLOC(I16, numJoints);
	int currJoint = 0;
	for (int i = 0; i < ndanim::GetNumJointsInSegment(pSkeleton->m_pAnimHierarchy, 0); i++)
	{
		if (jointsFound[i])
			joints[currJoint++] = i;
	}

	ANIM_ASSERT(currJoint == numJoints);

	// Setup parent joint
	m_numJoints = (I16)numJoints + 1; // Add 1 for parent of the root joint

	m_joints					 = NDI_NEW I16[m_numJoints];
	m_joints[kParentJointOffset] = (I16)parentJointIndex;

	m_parentOffset					   = NDI_NEW I16[m_numJoints];
	m_parentOffset[kParentJointOffset] = -1;

	m_childEndOffset					 = NDI_NEW I16[m_numJoints];
	m_childEndOffset[kParentJointOffset] = m_numJoints - 1;

	// Add remaining joints in depth first order
	I32 currOffset = kStartJointOffset;

	for (int i = 0; i < numJoints; i++)
	{
		if (joints[i] >= 0)
		{
			int currentIndex = joints[i];
			int parentIndex	 = GetParentJoint(pSkeleton, currentIndex);
			if (parentIndex == parentJointIndex)
			{
				AddJointAndChildren(pSkeleton,
									0,
									joints,
									m_joints,
									m_parentOffset,
									m_childEndOffset,
									i,
									currOffset,
									m_numJoints - 1);
			}
		}
	}

	ANIM_ASSERT(currOffset == m_numJoints);
	ANIM_ASSERT(m_parentOffset[kParentJointOffset] == -1 && m_parentOffset[kStartJointOffset] == kParentJointOffset);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointTree::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	JointSet::Relocate(deltaPos, lowerBound, upperBound);
	RelocatePointer(m_parentOffset, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_childEndOffset, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
int JointTree::GetParentOffset(I32F jointOffset) const
{
	ANIM_ASSERT(IsInitialized());
	return m_parentOffset[jointOffset];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointTree::InvalidateChildren(int rootOffset)
{
	ANIM_ASSERT(m_jointData);
	for (int i = rootOffset; i <= m_childEndOffset[rootOffset]; i++)
	{
		m_jointLocWsValid[i] = m_jointLocWsValid[m_parentOffset[i]] ? m_jointLocWsValid[i] : false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointTree::InvalidateAllJoints()
{
	ANIM_ASSERT(m_jointLocWsValid && m_numJoints > 0);
	memset(m_jointLocWsValid, 0, m_numJoints * sizeof(bool));
	m_jointLocWsValid[kParentJointOffset] = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointTree::UpdateAllJointLocsWs(int rootOffset)
{
	PROFILE_AUTO(Animation);
	const int endOffset = m_childEndOffset[rootOffset];
	for (U32F i = rootOffset; i <= endOffset; ++i)
	{
		if (!m_jointData->m_jointValid[i])
			continue;

		const I32F parentOffset = GetParentOffset(i);
		ANIM_ASSERT(parentOffset >= 0);
		ANIM_ASSERT(m_jointLocWsValid[parentOffset]);

		JointSet::UpdateJointLocWs(m_jointData->m_jointLocWs[parentOffset],
								   m_jointData->m_jointLocLs[i],
								   &m_jointData->m_jointLocWs[i]);

		SetJointWorldSpaceValid(i);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointTree::AllocateWorldSpaceValidFlags(int numJoints)
{
	ANIM_ASSERT(numJoints > 0);
	m_jointLocWsValid = NDI_NEW bool[numJoints];
	memset(m_jointLocWsValid, 0, numJoints * sizeof(bool));
	m_jointLocWsValid[kParentJointOffset] = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointTree::DiscardJointCache()
{
	JointSet::DiscardJointCache();
	NDI_DELETE[] m_jointLocWsValid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointTree::UpdateJointLocWs(I32F jointOffset)
{
	ANIM_ASSERT(m_jointData);

	if (!m_jointLocWsValid[jointOffset])
	{
		JointSet::UpdateJointLocWs(GetParentLocWs(jointOffset),
								   m_jointData->m_jointLocLs[jointOffset],
								   &m_jointData->m_jointLocWs[jointOffset]);
		SetJointWorldSpaceValid(jointOffset);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointTree::InvalidateJoint(I32F jointOffset, bool invalidateChildren)
{
	ANIM_ASSERT(m_jointData);

	m_jointLocWsValid[jointOffset] = false;
	if (invalidateChildren)
		InvalidateChildren(jointOffset);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointTree::IsJointWorldSpaceValid(I32F jointOffset) const
{
	ANIM_ASSERT(m_jointLocWsValid != nullptr);
	ANIM_ASSERT(jointOffset >= kParentJointOffset && jointOffset < m_numJoints);

	return m_jointLocWsValid[jointOffset];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointTree::SetJointWorldSpaceValid(I32F jointOffset)
{
	ANIM_ASSERT(jointOffset >= kParentJointOffset && jointOffset < m_numJoints);

	m_jointLocWsValid[jointOffset] = true;
}

///////////////////////////////////////////////////////////////////////////////
// JointSetObserver - Listens for when a module is mr'ed in to know to update the joint constraint values
///////////////////////////////////////////////////////////////////////////////

/// --------------------------------------------------------------------------------------------------------------- ///
JointSetObserver::JointSetObserver() : ScriptObserver(FILE_LINE_FUNC)
{
	m_loadedThisFrame = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSetObserver::OnModuleImported(StringId64 moduleId, Memory::Context allocContext)
{
	// Reload whenever any module is loaded since we don't necessarily know the name(s) of the module(s) that contain
	// splashers
	m_loadedThisFrame = 2;

#ifdef JOINT_LIMIT_DEBUG
	if (allocContext == kAllocDevCpu)
		JointLimitReloadAll();
#endif
}
