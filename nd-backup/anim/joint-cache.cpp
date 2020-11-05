/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/joint-cache.h"

#include "corelib/math/locator.h"
#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/level/art-item-skeleton.h"

#define ENSURE_JOINT_CACHE_NORMAL_QUAT 0

/// --------------------------------------------------------------------------------------------------------------- ///
JointCache::JointCache()
	: m_pJointHierarchy(nullptr)
	, m_pLocalSpaceJoints(nullptr)
	, m_pWorldSpaceLocs(nullptr)
	, m_pJointTransforms(nullptr)
	, m_pJointLightTypes(nullptr)
	, m_pJointLightIndices(nullptr)
	, m_pInputControls(nullptr)
	, m_pOutputControls(nullptr)
	, m_pJointAabbsLs(nullptr)
	, m_bInBindPose(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::Init(const ArtItemSkeletonHandle skeletonHandle, const Transform& xform, ConfigType configType)
{
	m_objXform = xform;
	m_skelHandle = skeletonHandle;

	const ArtItemSkeleton* pSkeleton = skeletonHandle.ToArtItem();
	m_pJointHierarchy = pSkeleton->m_pAnimHierarchy;

	const U16 numTotalJoints = ndanim::GetNumJointsInSegment(m_pJointHierarchy, 0);
	if ((configType == kConfigNormal || configType == kConfigWsLocatorsOnly || configType == kConfigLight) && numTotalJoints > 0)
	{
		m_pWorldSpaceLocs = NDI_NEW (kAlign16) Locator[numTotalJoints];
		m_pJointTransforms = NDI_NEW(kAlign16) Transform[numTotalJoints];

		U32 numBitArrayBlocks = ExternalBitArray::DetermineNumBlocks(numTotalJoints);
		m_upToDateTransforms.Init(numTotalJoints, NDI_NEW U64[numBitArrayBlocks]);
		m_upToDateWsLocs.Init(numTotalJoints, NDI_NEW U64[numBitArrayBlocks]);
	}

	if (configType == kConfigLight)	// this is a light-skel
	{
		m_pJointLightTypes = NDI_NEW (kAlign16) LightType[numTotalJoints];
		m_pJointLightIndices = NDI_NEW (kAlign16) U32[numTotalJoints];
	}

	const U16 numAnimatedJoints = pSkeleton->m_numAnimatedGameplayJoints;

	if ((configType == kConfigNormal || configType == kConfigLight) && numAnimatedJoints > 0)
	{
		m_pLocalSpaceJoints = NDI_NEW (kAlign16) ndanim::JointParams[numAnimatedJoints];
	}

	// used as a ice anim plugin output buffer, which needs to be 16bit aligned in size
	const U16 numInputControls = AlignSize(m_pJointHierarchy->m_numInputControls, kAlign4); 
	if (numInputControls > 0)
	{
		m_pInputControls = NDI_NEW (kAlign16) float[numInputControls];
	}

	const U16 numOutputControls = m_pJointHierarchy->m_numOutputControls;
	if (numOutputControls > 0)
	{
		m_pOutputControls = NDI_NEW (kAlign16) float[numOutputControls];
	}

	ResetToBindpose();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::Duplicate(JointCache* pJointCache) const
{
	const U16 numTotalJoints = ndanim::GetNumJointsInSegment(m_pJointHierarchy, 0);
	ANIM_ASSERT(numTotalJoints == ndanim::GetNumJointsInSegment(pJointCache->m_pJointHierarchy, 0));
	if (numTotalJoints > 0)
	{
		if (pJointCache->m_pJointTransforms)
		{
			memcpy(pJointCache->m_pWorldSpaceLocs, m_pWorldSpaceLocs, sizeof(Locator) * numTotalJoints);
			memcpy(pJointCache->m_pJointTransforms, m_pJointTransforms, sizeof(Transform) * numTotalJoints);
			pJointCache->m_upToDateTransforms.Copy(&pJointCache->m_upToDateTransforms, m_upToDateTransforms);
			pJointCache->m_upToDateWsLocs.Copy(&pJointCache->m_upToDateWsLocs, m_upToDateWsLocs);
		}

		if (pJointCache->m_pJointLightTypes)
			memcpy(pJointCache->m_pJointLightTypes, m_pJointLightTypes, sizeof(LightType) * numTotalJoints);

		if (pJointCache->m_pJointLightIndices)
			memcpy(pJointCache->m_pJointLightIndices, m_pJointLightIndices, sizeof(U32) * numTotalJoints);
	}

	pJointCache->m_objXform = m_objXform;

	const U16 numAnimatedJoints = pJointCache->GetNumAnimatedJoints();
	if (numAnimatedJoints > 0)
	{
		memcpy(pJointCache->m_pLocalSpaceJoints, m_pLocalSpaceJoints, sizeof(ndanim::JointParams) * numAnimatedJoints);
	}

	const U16 numInputControls = m_pJointHierarchy->m_numInputControls;
	ANIM_ASSERT(numInputControls == pJointCache->m_pJointHierarchy->m_numInputControls);
	if (numInputControls > 0)
	{
		memcpy(pJointCache->m_pInputControls, m_pInputControls, sizeof(float) * numInputControls);
	}

	const U16 numOutputControls = m_pJointHierarchy->m_numOutputControls;
	ANIM_ASSERT(numOutputControls == pJointCache->m_pJointHierarchy->m_numOutputControls);
	if (numOutputControls > 0)
	{
		memcpy(pJointCache->m_pOutputControls, m_pOutputControls, sizeof(float) * numOutputControls);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ndanim::JointParams* JointCache::GetDefaultLocalSpaceJoints() const
{
	return reinterpret_cast<const ndanim::JointParams*>(ndanim::GetDefaultJointPoseTable(m_pJointHierarchy));
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SMath::Mat34* JointCache::GetInverseBindPoses() const
{
	return ndanim::GetInverseBindPoseTable(m_pJointHierarchy);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::ResetToBindpose()
{
	PROFILE(Animation, ResetToBindpose);

	const U16 numAnimatedJoints = ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0);
	const U16 numTotalJoints = ndanim::GetNumJointsInSegment(m_pJointHierarchy, 0);

	const ndanim::JointParams* pDefaultLocalSpaceParams = GetDefaultLocalSpaceJoints();
	if (m_pLocalSpaceJoints)
	{
		for (U32F i = 0; i < numAnimatedJoints; ++i)
		{
			m_pLocalSpaceJoints[i] = pDefaultLocalSpaceParams[i];
			ANIM_ASSERT(IsFinite(m_pLocalSpaceJoints[i].m_trans));
			ANIM_ASSERT(IsFinite(m_pLocalSpaceJoints[i].m_quat));
		}
	}

	if (m_pJointTransforms)
	{
		for (U32 i = 0; i < numTotalJoints; ++i)
		{
			GetBindPoseJointXform(m_pJointTransforms[i], i);
			//ANIM_ASSERT(IsOrthogonal(&m_pJointTransforms[i]));
		}
		m_upToDateTransforms.SetAllBits();
		m_upToDateWsLocs.ClearAllBits();
	}

	const U16 numInputControls = m_pJointHierarchy->m_numInputControls;
	if (numInputControls > 0 && m_pInputControls)
	{
// 		float const* const pDefaultInputControls = ndanim::GetDefaultInputControlTable(m_pJointHierarchy);
// 		if (pDefaultInputControls)
// 		{
// 			memcpy(m_pInputControls, pDefaultInputControls, sizeof(float) * numInputControls);
// 		}
// 		else
		{
			memset(m_pInputControls, 0, sizeof(float) * numInputControls);
		}
	}

	const U16 numOutputControls = m_pJointHierarchy->m_numOutputControls;
	if (numOutputControls > 0 && m_pOutputControls)
	{
		const float* pDefs = (const float*)(((char*)m_pJointHierarchy) + m_pJointHierarchy->m_defaultFloatChannelsOffset);
		memcpy(m_pOutputControls, pDefs, sizeof(float) * numOutputControls);
	}

	m_bOutputControlsValid = true;
	m_bInBindPose = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	//AssertNoRelocatePointer(m_pJointHierarchy);

	RelocatePointer(m_pInputControls, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pOutputControls, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pLocalSpaceJoints, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pWorldSpaceLocs, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pJointTransforms, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pJointLightTypes, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pJointLightIndices, deltaPos, lowerBound, upperBound);

	if (m_pWorldSpaceLocs)
	{
		m_upToDateTransforms.Relocate(deltaPos, lowerBound, upperBound);
		m_upToDateWsLocs.Relocate(deltaPos, lowerBound, upperBound);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SMath::Vec4 JointCache::ComputeBoundingSphere(float paddingRadius) const
{
	SMath::Vec4 minPos(kLargeFloat, kLargeFloat, kLargeFloat, kLargeFloat);
	SMath::Vec4 maxPos(-kLargeFloat, -kLargeFloat, -kLargeFloat, -kLargeFloat);

	const U16 numJoints = ndanim::GetNumJointsInSegment(m_pJointHierarchy, 0);
	for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
	{
		const Locator& currentJointLocWs = GetJointLocatorWs(iJoint);
		const SMath::Vec4 current = currentJointLocWs.Pos().GetVec4();
		
		// Get the minimum of all the components of the Vec4.
		minPos = Min(minPos, current);
		maxPos = Max(maxPos, current);
	}

	const SMath::Vec4 centerPos = (minPos + maxPos) * 0.5f;
	const Vector diagonal(maxPos - minPos);

	SMath::Vec4 sphereWs(centerPos);
	sphereWs.SetW(Length(diagonal) * 0.5f + paddingRadius);

	return sphereWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float JointCache::GetInputControl(U32F index) const
{
	ANIM_ASSERT(index < m_pJointHierarchy->m_numInputControls);
	return m_pInputControls[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
float JointCache::GetInputControlById(StringId64 dagPathId) const
{
	for (U32F loop=0; loop < m_pJointHierarchy->m_numInputControls; loop++)
	{
		const StringId64 inputControlId = GetInputControlId(loop);
		if (dagPathId == inputControlId)
		{
			return m_pInputControls[loop];
		}
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// returns the 32-bit hash id of the given input control
StringId64 JointCache::GetInputControlId(U32F index) const
{
	const ndanim::JointHierarchyUserDataHeader* pUserDataHdr = (const ndanim::JointHierarchyUserDataHeader*)((U8 const*)m_pJointHierarchy + m_pJointHierarchy->m_userDataOffset);
	const StringId64* pInputControlIdTable = (const StringId64*)((U8 const*)pUserDataHdr + pUserDataHdr->m_inputControlIdTableOffset);

	ANIM_ASSERT(index < m_pJointHierarchy->m_numInputControls);
	return (index < m_pJointHierarchy->m_numInputControls) ? pInputControlIdTable[index] : INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float JointCache::GetOutputControl(U32F index) const
{
	ANIM_ASSERT(m_bOutputControlsValid);

	float val = 0.0f;

	if (index < m_pJointHierarchy->m_numOutputControls)
	{
		val = m_pOutputControls[index];
	}

	ANIM_ASSERT(IsFinite(val));

	return val;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float JointCache::GetOutputControlById(StringId64 dagPathId, bool* pFound /* = nullptr */) const
{
	ANIM_ASSERT(m_bOutputControlsValid);

	float val = 0.0f;
	bool found = false;
	const ArtItemSkeleton* pSkel = m_skelHandle.ToArtItem();

	for (U32F iOutput = 0; iOutput < m_pJointHierarchy->m_numOutputControls; ++iOutput)
	{
		const StringId64 outputControlId = pSkel->m_pFloatDescs[iOutput].m_nameId;
		if (dagPathId == outputControlId)
		{
			val = m_pOutputControls[iOutput];
			found = true;
			break;
		}
	}

	if (pFound)
		*pFound = found;

	ANIM_ASSERT(IsFinite(val));

	return val;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float JointCache::GetOutputControlByName(const char* nodeName, bool* pFound /* = nullptr */) const
{
	ANIM_ASSERT(m_bOutputControlsValid);

	float val = 0.0f;
	bool found = false;
	const ArtItemSkeleton* pSkel = m_skelHandle.ToArtItem();

	for (U32F iOutput = 0; iOutput < m_pJointHierarchy->m_numOutputControls; ++iOutput)
	{
		const char* outputControlStr = pSkel->m_pFloatDescs[iOutput].m_pName;

		const char* mayaNode = outputControlStr;

		for (int i = strlen(mayaNode) - 1; i > 0; --i)
		{
			if (mayaNode[i] == '.')
			{
				mayaNode = mayaNode + i + 1;
				break;
			}
		}

		if (0 == strcmp(mayaNode, nodeName))
		{
			val = m_pOutputControls[iOutput];
			found = true;
			break;
		}
	}

	if (pFound)
		*pFound = found;

	ANIM_ASSERT(IsFinite(val));

	return val;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointCache::HasOutputControl(StringId64 dagPathId) const
{
	bool found = false;
	
	const ArtItemSkeleton* pSkel = m_skelHandle.ToArtItem();

	for (U32F iOutput = 0; iOutput < m_pJointHierarchy->m_numOutputControls; ++iOutput)
	{
		const StringId64 outputControlId = pSkel->m_pFloatDescs[iOutput].m_nameId;
		if (dagPathId == outputControlId)
		{
			found = true;
			break;
		}
	}

	return found;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::SetObjXForm(const Transform& objXform, bool invalidateLocWs)
{
	ANIM_ASSERT(IsFinite(objXform));

	if (invalidateLocWs && !AllComponentsEqual(m_objXform.GetRawMat44(), objXform.GetRawMat44()))
	{
		InvalidateAllWsLocs();
	}
	m_objXform = objXform;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::ConvertAllTransforms()
{
	PROFILE(Animation, ConvertAllTransforms);

	ANIM_ASSERT(m_pJointTransforms);
	ANIM_ASSERT(m_pLocalSpaceJoints);

	const uint numAnimJoints = GetNumAnimatedJoints();
	for (I32 ii = m_upToDateTransforms.FindFirstClearBit(); ii < numAnimJoints; ii = m_upToDateTransforms.FindNextClearBit(ii))
	{
		Transform xfm(BuildTransform(m_pLocalSpaceJoints[ii].m_quat, m_pLocalSpaceJoints[ii].m_trans.GetVec4()));
		//ANIM_ASSERT(IsOrthogonal(&xfm));
		xfm.SetXAxis(xfm.GetXAxis() * m_pLocalSpaceJoints[ii].m_scale.X());
		xfm.SetYAxis(xfm.GetYAxis() * m_pLocalSpaceJoints[ii].m_scale.Y());
		xfm.SetZAxis(xfm.GetZAxis() * m_pLocalSpaceJoints[ii].m_scale.Z());
		I32 iParent = GetParentJoint(ii);
		if (iParent >= 0)
			xfm = xfm * m_pJointTransforms[iParent];
		m_pJointTransforms[ii] = xfm;
	}

	m_upToDateTransforms.SetAllBits();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::ConvertTransform(unsigned int index)
{
	PROFILE(Animation, ConvertTransform);

	ANIM_ASSERT(m_pJointTransforms);
	ANIM_ASSERT(m_pLocalSpaceJoints);
	ANIM_ASSERTF(index < GetNumAnimatedJoints(), ("Trying to query sdk joint %s after anim blend pass (skel: %s)", DevKitOnly_StringIdToString(m_skelHandle.ToArtItem()->m_pJointDescs[index].m_nameId), m_skelHandle.ToArtItem()->GetName()));

	U16* pJointStack = STACK_ALLOC(U16, 256);
	U32 iStack = 0;
	I32 iParent = index;
	do 
	{
		ANIM_ASSERT(iStack < 256);
		pJointStack[iStack] = iParent;
		iStack++;
		iParent = GetParentJoint(iParent);
	} while (iParent >= 0 && !m_upToDateTransforms.IsBitSet(iParent));

	while (iStack > 0)
	{
		iStack--;
		U32 iJoint = pJointStack[iStack];

		Transform xfm(BuildTransform(m_pLocalSpaceJoints[iJoint].m_quat, m_pLocalSpaceJoints[iJoint].m_trans.GetVec4()));
		//ANIM_ASSERT(IsOrthogonal(&xfm));
		xfm.SetXAxis(xfm.GetXAxis() * m_pLocalSpaceJoints[iJoint].m_scale.X());
		xfm.SetYAxis(xfm.GetYAxis() * m_pLocalSpaceJoints[iJoint].m_scale.Y());
		xfm.SetZAxis(xfm.GetZAxis() * m_pLocalSpaceJoints[iJoint].m_scale.Z());
		if (iParent >= 0)
			xfm = xfm * m_pJointTransforms[iParent];
		m_pJointTransforms[iJoint] = xfm;
		m_upToDateTransforms.SetBit(iJoint);
		iParent = iJoint;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Transform& JointCache::GetJointTransform(U32F jointIndex) const
{
	const U32F numSegmentJoints = ndanim::GetNumJointsInSegment(m_pJointHierarchy, 0);
	ANIM_ASSERT(jointIndex < numSegmentJoints);
	ANIM_ASSERT(m_pJointTransforms);

	if (!m_upToDateTransforms.IsBitSet(jointIndex))
		const_cast<JointCache*>(this)->ConvertTransform(jointIndex);

	return m_pJointTransforms[jointIndex];
}


/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::ConvertWsLoc(unsigned int index)
{
	Transform xform = GetJointTransform(index) * m_objXform;
	
	// Remove scale (ie : make sure every axis is of length 1) or else the conversion to quaternion inside the Locator won't work.
	RemoveScaleSafe(&xform);

	// Sadly, sometimes it's not so the resulting Quat will be whatever ...
	// But the game works and we're about to ship
	//ANIM_ASSERT(IsOrthogonal(&xform)); // also make sure it's orthogonal

	const Locator loc(xform);

	m_pWorldSpaceLocs[index] = loc;
	m_upToDateWsLocs.SetBit(index);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::ConvertAllWsLocs()
{
	ANIM_ASSERT(m_pJointTransforms);

	ConvertAllTransforms();

	const uint numTotalJoints = GetNumTotalJoints();
	for (uint ii = m_upToDateWsLocs.FindFirstClearBit(); ii < numTotalJoints; ii = m_upToDateWsLocs.FindNextClearBit(ii))
	{
		ConvertWsLoc(ii);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator& JointCache::GetJointLocatorWs(U32F jointIndex) const
{
	const U32F numSegmentJoints = ndanim::GetNumJointsInSegment(m_pJointHierarchy, 0);
	ANIM_ASSERT(jointIndex < numSegmentJoints);
	ANIM_ASSERT(m_pJointTransforms);

	if (!m_upToDateWsLocs.IsBitSet(jointIndex))
		const_cast<JointCache*>(this)->ConvertWsLoc(jointIndex);

	return m_pWorldSpaceLocs[jointIndex];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::OverwriteJointLocatorWs(U32F iJoint, const Locator& jointLocWs)
{
#if ENSURE_JOINT_CACHE_NORMAL_QUAT
	ANIM_ASSERT(IsNormal(jointLocWs.Rot()));
#endif
	ANIM_ASSERT(IsReasonable(jointLocWs));

	ANIM_ASSERT(iJoint < ndanim::GetNumJointsInSegment(m_pJointHierarchy, 0));
	m_upToDateWsLocs.SetBit(iJoint);
	m_pWorldSpaceLocs[iJoint] = jointLocWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ndanim::JointParams& JointCache::GetJointParamsLs(U32F jointIndex) const
{
	ANIM_ASSERTF(m_pLocalSpaceJoints, ("Attempted to get local space joint params from a JointCache that was not set up with them."));
	ANIM_ASSERT(jointIndex < ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0));
	return m_pLocalSpaceJoints[jointIndex];
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ndanim::JointParams JointCache::GetJointParamsSafeLs(U32F jointIndex) const
{
	if (jointIndex < ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0))
	{
		ANIM_ASSERTF(m_pLocalSpaceJoints, ("Attempted to get local space joint params from a JointCache that was not set up with them."));
		return m_pLocalSpaceJoints[jointIndex];
	}
	else if (jointIndex < ndanim::GetNumJointsInSegment(m_pJointHierarchy, 0))
	{
		const Locator locJointWs = GetJointLocatorWs(jointIndex);

		Locator locParentWs(kIdentity);
		I32F parentIndex = GetParentJoint(jointIndex);
		if (parentIndex >= 0)
		{
			locParentWs = GetJointLocatorWs(parentIndex);
		}

		const Locator locJointPs = locParentWs.UntransformLocator(locJointWs);

		ndanim::JointParams sqt;
		sqt.m_quat = locJointPs.GetRotation();
		sqt.m_trans = locJointPs.GetTranslation();
		sqt.m_scale = VECTOR_LC(1.0f, 1.0f, 1.0f); // have to assume unity scale
		return sqt;
	}
	else
	{
		ndanim::JointParams sqt;
		sqt.m_quat = Quat(kIdentity);
		sqt.m_trans = Point(kOrigin);
		sqt.m_scale = VECTOR_LC(1.0f, 1.0f, 1.0f); // have to assume unity scale
		return sqt;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointCache::GetInverseBindPoseJointXform(Transform& outXform, U32F jointIndex) const
{
	const SMath::Mat34* pInverseBindPoses = GetInverseBindPoses();

	ANIM_ASSERT(pInverseBindPoses);
	ANIM_ASSERT(jointIndex < m_pJointHierarchy->m_numTotalJoints);
	outXform = Transform(pInverseBindPoses[jointIndex].GetMat44());

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointCache::GetBindPoseJointXform(Transform& outXform, U32F jointIndex) const
{
	Transform invBindPoseXform;
	GetInverseBindPoseJointXform(invBindPoseXform, jointIndex);
	outXform = Inverse(invBindPoseXform);
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SMath::Vector GetScaleFromTransform(const SMath::Transform& xform)
{
	SMath::Vector result(Length(xform.GetXAxis()), Length(xform.GetYAxis()), Length(xform.GetZAxis()));
	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
LightType JointCache::GetJointLightType(U32F iJoint) const
{
	ANIM_ASSERT(iJoint < GetNumTotalJoints());
	return m_pJointLightTypes[iJoint];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::SetJointLightType(U32F iJoint, LightType lightType)
{
	ANIM_ASSERT(iJoint < GetNumTotalJoints());
	m_pJointLightTypes[iJoint] = lightType;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 JointCache::GetJointLightIndex(U32F iJoint) const
{
	ANIM_ASSERT(iJoint < GetNumTotalJoints());
	return m_pJointLightIndices[iJoint];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::SetJointLightIndex(U32F iJoint, U32 lightIndex)
{
	ANIM_ASSERT(iJoint < GetNumTotalJoints());
	m_pJointLightIndices[iJoint] = lightIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::SetJointParamsLs(U32F iJoint, const ndanim::JointParams& param)
{
	//ANIM_ASSERT(IsFinite(param.m_quat));
	//ANIM_ASSERT(IsFinite(param.m_trans));
	//ANIM_ASSERT(IsFinite(param.m_scale));
	ANIM_ASSERTF(m_pLocalSpaceJoints, ("Attempted to set local space joint params on a JointCache that was not set up with them."));
	ANIM_ASSERT(iJoint < ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0));

	ANIM_ASSERT(IsReasonable(param.m_trans));

#if ENSURE_JOINT_CACHE_NORMAL_QUAT
	ANIM_ASSERT(IsNormal(param.m_quat));
#endif

	m_pLocalSpaceJoints[iJoint] = param;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::DebugDrawSkel(bool includeConstrainedJoints,
							   float thicknessScale /* = 1.0f */,
							   I32F iStart /* = -1 */,
							   I32F iEnd /* = -1 */) const
{
	STRIP_IN_FINAL_BUILD;

	if (!m_pWorldSpaceLocs)
		return;

	const I32F jointCount = includeConstrainedJoints ? ndanim::GetNumJointsInSegment(m_pJointHierarchy, 0)
													 : ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0);

	if (iStart < 0)
	{
		iStart = 0;
	}

	if (iEnd < 0)
	{
		iEnd = jointCount - 1;
	}

	for (I32F iJoint = iStart; iJoint <= iEnd; ++iJoint)
	{
		const I32F parentJoint = GetParentJoint(iJoint);
		if (parentJoint == -1)
			continue;

		const Locator loc = GetJointLocatorWs(iJoint);
		const Locator parentLoc = GetJointLocatorWs(parentJoint);

		const Color color = (iJoint < ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0)) ? kColorWhite : kColorYellow;
		DebugDrawBone(parentLoc, loc, kColorWhite, thicknessScale, PrimAttrib(kPrimEnableDepthTest, kPrimEnableDepthWrite));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const char* GetInputControlName(const JointCache& jointCache, U32F iInput)
{
	const StringId64 inputControlId = jointCache.GetInputControlId(iInput);
	const char* name = DevKitOnly_StringIdToStringOrNull(inputControlId);
	return (name) ? name : "";
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDrawInputControlDriver_Twist(const JointCache& jointCache,
											  const ndanim::InputControlDriver& driver,
											  U32F iInput)
{
	STRIP_IN_FINAL_BUILD;

	float twistAngleDeg = jointCache.GetInputControl(iInput);

	const Color rgb[3] = { kColorRed, kColorGreen, kColorBlue };
	const Scalar axesScale(0.15f);

	const Locator& jointLocWs = jointCache.GetJointLocatorWs(driver.m_inputJointIndex);
	Quat quatJ2W = jointLocWs.GetRotation();
	Vector jointAxesWs[3];
	jointAxesWs[0] = Normalize(GetLocalX(quatJ2W));
	jointAxesWs[1] = Normalize(GetLocalY(quatJ2W));
	jointAxesWs[2] = Normalize(GetLocalZ(quatJ2W));
	const Vector vecTwistedPerpAxisW = jointAxesWs[(driver.m_primaryAxis + 1) % 3] * axesScale;

	I32F parentJointIndex = jointCache.GetParentJoint(driver.m_inputJointIndex);
	const Locator parentLocWs = (parentJointIndex >= 0) ? jointCache.GetJointLocatorWs(parentJointIndex) : Locator(kIdentity);
	const Quat quatR2P = driver.m_refPosePs; // the reference pose is always in the joint's parent-space as well
	const Quat quatR2W = Normalize(parentLocWs.GetRotation() * quatR2P);
	const Locator refLocWs(jointLocWs.GetTranslation(), quatR2W);
	const Point refPosWs = refLocWs.GetTranslation();
	Vector refAxesWs[3];
	refAxesWs[0] = Normalize(GetLocalX(quatR2W));
	refAxesWs[1] = Normalize(GetLocalY(quatR2W));
	refAxesWs[2] = Normalize(GetLocalZ(quatR2W));
	const Vector vecRotationAxisW = refAxesWs[driver.m_primaryAxis] * axesScale;
	const Vector vecUntwistedPerpAxisW = refAxesWs[(driver.m_primaryAxis + 1) % 3] * axesScale;

	// twist axis
	g_prim.Draw(DebugLine(refPosWs, vecRotationAxisW, rgb[driver.m_primaryAxis], 6.0f, kPrimEnableHiddenLineAlpha));
	// untwisted and twisted versions of a perpendicular axis
	g_prim.Draw(DebugLine(refPosWs, vecTwistedPerpAxisW, Lerp(rgb[(driver.m_primaryAxis + 1) % 3], kColorGray, 0.75f), 2.0f, kPrimEnableHiddenLineAlpha));
	g_prim.Draw(DebugLine(refPosWs, vecUntwistedPerpAxisW, rgb[(driver.m_primaryAxis + 1) % 3], 2.0f, kPrimEnableHiddenLineAlpha));
	// arc to emphasize the rotation
	g_prim.Draw(DebugSector(refPosWs, vecUntwistedPerpAxisW, vecTwistedPerpAxisW, kColorGray, 1.0f, PrimAttrib(kPrimEnableHiddenLineAlpha, kPrimEnableWireframe)));
	// little arrow to show rotation sense
	const Point arrowHead = refPosWs + vecTwistedPerpAxisW;
	const Vector arrowTailVec = Normalize(Lerp(vecUntwistedPerpAxisW, vecTwistedPerpAxisW, SCALAR_LC(0.9f))) * axesScale;
	const Point arrowTail = refPosWs + arrowTailVec;
	g_prim.Draw(DebugArrow(arrowTail, arrowHead - arrowTail, kColorGray, LerpScale(0.0f, 12.0f, 0.01f, 0.1f, Abs(twistAngleDeg))));

	char text[256];
	snprintf(text, sizeof(text), "%s\ntwist = %.2f deg (#%u)", GetInputControlName(jointCache, iInput), twistAngleDeg, (U32)iInput);
	g_prim.Draw(DebugString(refPosWs + vecRotationAxisW * SCALAR_LC(0.4f), text, kColorGreenTrans, g_msgOptions.m_conScale));
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Build a debug draw cone transform for a cone with the given
// diameter centered at the locator's position and aimed at the
// locator's look-at direction.
inline Transform BuildConeTransform(Scalar scale, F32 fAngleRad, const Locator& rLoc, ndanim::InputControlDriver::Axis axis)
{
	F32 fConeDepth = Cos(fAngleRad);
	F32 fConeRadius = Sin(fAngleRad);
	F32 fConeDiameter = 2.0f * fConeRadius;

	// Build a transformation matrix from cone-space into a canonical space in which the
	// cone is facing forward and scaled appropriately.
	Vector axisScale = Vector(fConeDiameter, fConeDepth, fConeDiameter) * scale;
	Transform xfmScale;
	xfmScale.SetScale(axisScale);
	const Quat rot[3] = { Quat(kUnitZAxis, (TAU) / 4.0f), Quat(kIdentity), Quat(kUnitXAxis, (-TAU) / 4.0f) };
	const Vector offsetVec[3] = { VECTOR_LC(1.0f, 0.0f, 0.0f), VECTOR_LC(0.0f, 1.0f, 0.0f), VECTOR_LC(0.0f, 0.0f, 1.0f) };
	Transform xfmOrient(rot[axis], kOrigin + offsetVec[axis] * scale * Scalar(fConeDepth));
	Transform xfmLoc(rLoc.AsTransform());
	Transform xfm = xfmScale * xfmOrient * xfmLoc;
	return(xfm);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDrawInputControlDriver_ConeAngle(const JointCache& jointCache,
												  const ndanim::InputControlDriver& driver,
												  U32F iInput)
{
	STRIP_IN_FINAL_BUILD;

	float coneAngleDeg = jointCache.GetInputControl(iInput);

	// recalc weight from cone angle, just as the plugin would have done...
	const float minAngleDeg = driver.m_minAngleDeg;
	const float maxAngleDeg = driver.m_maxAngleDeg;
	const float diff = maxAngleDeg - minAngleDeg;
	float weightVal = 0.0f;

	if (diff > 0.0f)
	{
		weightVal = 1.0f - ( (coneAngleDeg - minAngleDeg) / diff);
	}
	// this makes sure that if the angle is within the minAngle, weightVal = 1.0
	else if (coneAngleDeg <= minAngleDeg)
	{
		weightVal = 1.0f;
	}
	// clamp the weight value between 0.0 - 1.0
	weightVal = MinMax01(weightVal);


	const Color rgb[3] = { kColorRed, kColorGreen, kColorBlue };
	const Scalar axesScale(0.15f);

	const Locator& jointLocWs = jointCache.GetJointLocatorWs(driver.m_inputJointIndex);
	Quat quatJ2W = jointLocWs.GetRotation();
	Vector jointAxesWs[3];
	jointAxesWs[0] = Normalize(GetLocalX(quatJ2W));
	jointAxesWs[1] = Normalize(GetLocalY(quatJ2W));
	jointAxesWs[2] = Normalize(GetLocalZ(quatJ2W));
	const Vector vecAimedAxisW = jointAxesWs[driver.m_primaryAxis] * axesScale;

	I32F parentJointIndex = jointCache.GetParentJoint(driver.m_inputJointIndex);
	const Locator parentLocWs = (parentJointIndex >= 0) ? jointCache.GetJointLocatorWs(parentJointIndex) : Locator(kIdentity);
	Quat quatR2P = driver.m_refPosePs; // the reference pose is always in the joint's parent-space as well

	// account for twist, if any
	if (driver.m_inputTwistControl >= 0)
	{
		const Scalar twistAngleDeg(jointCache.GetInputControl(driver.m_inputTwistControl));
		const Scalar twistAngleRad = DegreesToRadians(twistAngleDeg);

		const Quat quatT2P = driver.m_twistRefPosePs;

		Vector twistAxesPs[3];
		twistAxesPs[0] = GetLocalX(quatT2P);
		twistAxesPs[1] = GetLocalY(quatT2P);
		twistAxesPs[2] = GetLocalZ(quatT2P);
		const Vector vecTwistAxisPs = Normalize(twistAxesPs[driver.m_twistAxis]); // not theoretically necessary to normalize, but to be sure

		const Quat twistQuatPs = QuatFromAxisAngle(vecTwistAxisPs, twistAngleRad);
		quatR2P = twistQuatPs * quatR2P;
	}

	const Quat quatR2W = Normalize(parentLocWs.GetRotation() * quatR2P);
	const Locator refLocWs(jointLocWs.GetTranslation(), quatR2W);
	Vector refAxesWs[3];
	refAxesWs[0] = Normalize(GetLocalX(quatR2W));
	refAxesWs[1] = Normalize(GetLocalY(quatR2W));
	refAxesWs[2] = Normalize(GetLocalZ(quatR2W));
	const Vector vecRefAxisW = refAxesWs[driver.m_primaryAxis] * axesScale;

	//g_prim.Draw(DebugCoordAxes(refLocWs, (float)axesScale, kPrimEnableHiddenLineAlpha, 2.0f)); // naw, too cluttered
	g_prim.Draw(DebugLine(refLocWs.GetTranslation(), vecRefAxisW, rgb[driver.m_primaryAxis], 6.0f, kPrimEnableHiddenLineAlpha));
	g_prim.Draw(DebugLine(refLocWs.GetTranslation(), vecAimedAxisW, Lerp(rgb[driver.m_primaryAxis], kColorGray, 0.75f), 2.0f, kPrimEnableHiddenLineAlpha));
	g_prim.Draw(DebugSector(refLocWs.GetTranslation(), vecRefAxisW, vecAimedAxisW, kColorGray, 1.0f, PrimAttrib(kPrimEnableHiddenLineAlpha, kPrimEnableWireframe)));

	char text[256];
	snprintf(text, sizeof(text), "%s\nw = %.4f | %.2f deg (#%u)", GetInputControlName(jointCache, iInput), weightVal, coneAngleDeg, (U32)iInput);
	g_prim.Draw(DebugString(refLocWs.GetTranslation() + vecRefAxisW * SCALAR_LC(0.4f), text, kColorCyanTrans, g_msgOptions.m_conScale));

	if (true) // (drawCones)
	{
		const PrimAttrib primAttribFlags = PrimAttrib(kPrimEnableHiddenLineAlpha, kPrimEnableWireframe);

		if (minAngleDeg > 0.0f)
		{
			Transform xfmMin = BuildConeTransform(axesScale * SCALAR_LC(0.5f), DEGREES_TO_RADIANS(minAngleDeg), refLocWs, driver.m_primaryAxis);
			g_prim.Draw(DebugPrimShape(xfmMin, DebugPrimShape::kConeCapped, kColorMagenta, primAttribFlags));
		}

		Transform xfmMax = BuildConeTransform(axesScale * SCALAR_LC(0.5f), DEGREES_TO_RADIANS(maxAngleDeg), refLocWs, driver.m_primaryAxis);
		g_prim.Draw(DebugPrimShape(xfmMax, DebugPrimShape::kConeCapped, kColorYellow, primAttribFlags));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Updates the joint's world space transform based on its parent's
void JointCache::UpdateJointLocatorWs(U32F iJoint)
{
	if (iJoint >= ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0))
		return;

	const float rootScale = m_pLocalSpaceJoints[0].m_scale.X();

	I32F parentJoint = GetParentJoint(iJoint);
	//ANIM_ASSERT(parentJoint != -1);		// UpdateJointLocatorWs() does not work with the root joint
	if (parentJoint != -1)
	{
		const Locator parentLocWs = GetJointLocatorWs(parentJoint);
		const Point scaledTrans = Point(m_pLocalSpaceJoints[iJoint].m_trans.GetVec4() * rootScale);
		const Locator childJointLocLs(scaledTrans, m_pLocalSpaceJoints[iJoint].m_quat);

		Locator jointLocWs = parentLocWs.TransformLocator(childJointLocLs);
		jointLocWs.SetRot(Normalize(jointLocWs.GetRotation()));
		OverwriteJointLocatorWs(iJoint, jointLocWs);
	
		ANIM_ASSERT(IsFinite(m_pWorldSpaceLocs[iJoint]));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::UpdateWSSubPose(const Locator& align, U32F iJoint)
{
	if (iJoint >= ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0))
		return;

	if (iJoint == 0)
	{
		const float rootScale = m_pLocalSpaceJoints[0].m_scale.X();
	
		const Point scaledTrans = Point(m_pLocalSpaceJoints[0].m_trans.GetVec4() * rootScale);
		const Locator scaledRootJointLocLs(scaledTrans, m_pLocalSpaceJoints[0].m_quat);

		OverwriteJointLocatorWs(0, align.TransformLocator(scaledRootJointLocLs));
	}
	else
	{
		UpdateJointLocatorWs(iJoint);
	}


	const ndanim::DebugJointParentInfo* pParentInfo = GetParentInfo();
	I32 child = pParentInfo[iJoint].m_child;
	if ((child != -1) && (child != 0) && (child != iJoint))
	{
		I32 firstchild = child;
		for (;;)
		{
			UpdateWSSubPose(align, child);
			I32 nextchild = pParentInfo[child].m_sibling;
			if ((nextchild == firstchild) || (nextchild == child) || (nextchild == -1))
				break;
			child = nextchild;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::UpdateWSRootSubPose(const Locator& align, Vector_arg scale)
{
	// Update root first
	const Vector rootScale = m_pLocalSpaceJoints[0].m_scale * scale;
	
	const Point scaledRootTrans = kOrigin + (m_pLocalSpaceJoints[0].m_trans - kOrigin) * rootScale;
	const Locator scaledRootJointLocLs(scaledRootTrans, m_pLocalSpaceJoints[0].m_quat);

	OverwriteJointLocatorWs(0, align.TransformLocator(scaledRootJointLocLs));

	// Update the rest
	const int numAnimatedJoints = GetNumAnimatedJoints();
	for (int i = 1; i<numAnimatedJoints; i++)
	{
		I32F parentJoint = GetParentJoint(i);
		ANIM_ASSERT(parentJoint >= 0);
		
		const Locator parentLocWs = GetJointLocatorWs(parentJoint);
		const Point scaledTrans = kOrigin + (m_pLocalSpaceJoints[i].m_trans - kOrigin) * rootScale;
		const Locator childJointLocLs(scaledTrans, m_pLocalSpaceJoints[i].m_quat);
		Locator jointLocWs = parentLocWs.TransformLocator(childJointLocLs);
		jointLocWs.SetRot(Normalize(jointLocWs.Rot()));
		OverwriteJointLocatorWs(i, jointLocWs);
	
		// SS: I've tmeporarily commented this out so that we could get Sully and Cutter working as player characters in game, but it seems like one single joint (597) is getting NANs.  joints above and beyond it are fine.  No idea what might be causing this
		// but we probably need to see what's going on.
		//ANIM_ASSERT(IsFinite(m_pWorldSpaceLocs[i]));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
//Helper function for moving a joint
void JointCache::MoveJointWS(U32F iJoint, Vector_arg moveWs)
{
	if (iJoint >= ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0))
		return;

	I32F parent = GetParentJoint(iJoint);
	if (parent == -1)
		return;

	const float rootScale = m_pLocalSpaceJoints[0].m_scale.X();
	if (rootScale > 0.f)
	{
		const Locator& parentLoc = GetJointLocatorWs(parent);
		ANIM_ASSERT(IsFinite(parentLoc));
		Quat parentRotation = parentLoc.Rot();

		const Vector moveLocal = moveWs / rootScale;
		ANIM_ASSERT(IsFinite(moveLocal));

		m_pLocalSpaceJoints[iJoint].m_trans += Rotate(Conjugate(parentRotation), moveLocal);
		ANIM_ASSERT(IsFinite(m_pLocalSpaceJoints[iJoint].m_trans));

		UpdateJointLocatorWs(iJoint);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::PrerotateJointLs(U32 iJoint, Quat_arg postmul )
{
	if (iJoint >= ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0))
		return;

	Quat q = postmul;
	m_pLocalSpaceJoints[iJoint].m_quat = q * m_pLocalSpaceJoints[iJoint].m_quat;
	ANIM_ASSERT(IsFinite(m_pLocalSpaceJoints[iJoint].m_quat));
	UpdateJointLocatorWs(iJoint);	
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache::PostrotateJointLs(U32 iJoint, Quat_arg postmul )
{
	if (iJoint >= ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0))
		return;

	Quat q = postmul;
	m_pLocalSpaceJoints[iJoint].m_quat = m_pLocalSpaceJoints[iJoint].m_quat * q;
	ANIM_ASSERT(IsFinite(m_pLocalSpaceJoints[iJoint].m_quat));
	UpdateJointLocatorWs(iJoint);	
}

/// --------------------------------------------------------------------------------------------------------------- ///
//Helper function for rotating a joint
void JointCache::RotateJointWS(U32F iJoint, Quat_arg rotor)
{
	if (iJoint >= ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0))
		return;

	I32F parent = GetParentJoint(iJoint);
	if (parent == -1)
		return;

	const Locator& parentLoc = GetJointLocatorWs(parent);
	ANIM_ASSERT(IsFinite(parentLoc));
	Quat parentRotation = parentLoc.Rot();
	Quat premul = Conjugate(parentRotation) * rotor * parentRotation;
	
	m_pLocalSpaceJoints[iJoint].m_quat = Normalize(premul * m_pLocalSpaceJoints[iJoint].m_quat);
	ANIM_ASSERT(IsFinite(m_pLocalSpaceJoints[iJoint].m_quat));
	UpdateJointLocatorWs(iJoint);	
}

/// --------------------------------------------------------------------------------------------------------------- ///
//Helper function for translating the root.
void JointCache::TranslateRootBoneWS(Vector_arg trans, const Locator& objectLoc)
{
	PROFILE(Animation, TranslateRootBoneWS);

	const float rootScale = m_pLocalSpaceJoints[0].m_scale.X();

	const Vector localTranslation = objectLoc.UntransformVector(trans) * rootScale;

	m_pLocalSpaceJoints[0].m_trans += localTranslation;
	for (U32F i = 0; i < GetNumTotalJoints(); ++i)
	{
		const Locator origLoc = GetJointLocatorWs(i);

		OverwriteJointLocatorWs(i, Locator(origLoc.GetPosition() + trans, origLoc.GetRotation()));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
//Helper function for translating the root.
void JointCache::RotateRootBoneWS(Quat_arg rotor, const Locator& objectLoc)
{
	Quat parentRotation = objectLoc.Rot();
	Quat premul = Conjugate(parentRotation) * rotor * parentRotation;

	m_pLocalSpaceJoints[0].m_quat = Normalize(premul * m_pLocalSpaceJoints[0].m_quat);	
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F JointCache::GetParentJoint(I32F iJoint) const
{
	return ndanim::GetParentJoint(m_pJointHierarchy, iJoint);
}
