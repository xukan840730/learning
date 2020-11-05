/*
 * Copyright (c) 2005 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/bit-array.h"

#include "ndlib/anim/anim-defines.h"
#include "ndlib/resource/resource-table.h"
#include "ndlib/render/common/lightdef.h"

class Locator;

/// --------------------------------------------------------------------------------------------------------------- ///
/// Holds cached information about joint transformations.
///
class JointCache
{
public:

	enum ConfigType
	{
		kConfigNoCache,
		kConfigWsLocatorsOnly,
		kConfigNormal,
		kConfigLight
	};

	JointCache();

	void Init(const ArtItemSkeletonHandle skeletonHandle, const Transform& xform, ConfigType configType);
	void Duplicate(JointCache* pJointCache) const;

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	void ResetToBindpose();

	const SMath::Vec4 ComputeBoundingSphere(float paddingRadius) const;

	U16 GetNumTotalJoints() const		{ return ndanim::GetNumJointsInSegment(m_pJointHierarchy, 0); }
	U16 GetNumAnimatedJoints() const	{ return ndanim::GetNumAnimatedJointsInSegment(m_pJointHierarchy, 0); }
	U16 GetNumInputControls() const		{ return m_pJointHierarchy->m_numInputControls; }
	U16 GetNumOutputControls() const	{ return m_pJointHierarchy->m_numOutputControls; }
	U16 GetNumFloatChannels() const		{ return m_pJointHierarchy->m_numFloatChannels; }
	
	// FYI, a processing group can contain both a joint group and a float channel group
	// Also, the number of joints in a processing group is most likely less than the max of 128 joints.
	U8 GetNumProcessingGroups() const	{ return ndanim::GetNumProcessingGroupsInSegment(m_pJointHierarchy, 0); }

	// This is the number of ValidBits instances available/needed any time you evaluate a pose/clip/blend
	U8 GetNumChannelGroups() const
	{
		const U8 numProcGroups = GetNumProcessingGroups();
		U8 numChannelGroups = 0;
		for (int i = 0; i < numProcGroups; ++i)
		{
			numChannelGroups += ndanim::GetNumChannelGroupsInProcessingGroup(m_pJointHierarchy, i); 
		}
		return numChannelGroups;
	}

	float GetInputControl(U32F index) const;
	float GetInputControlById(StringId64 dagPathId) const;
	StringId64 GetInputControlId(U32F index) const;

	float GetOutputControl(U32F index) const;
	float GetOutputControlById(StringId64 dagPathId, bool* pFound = nullptr) const;
	float GetOutputControlByName(const char* nodeName, bool* pFound = nullptr) const;
	bool HasOutputControl(StringId64 dagPathId) const;

	const SMath::Mat34* GetInverseBindPoses() const;

	const Transform& GetJointTransform(U32F jointIndex) const;
	const Locator& GetJointLocatorWs(U32F jointIndex) const;
	void OverwriteJointLocatorWs(U32F iJoint, const Locator& jointLocWs);
	bool GetInverseBindPoseJointXform(Transform& outXform, U32F jointIndex) const;
	bool GetBindPoseJointXform(Transform& outXform, U32F jointIndex) const;

	void UpdateJointLocatorWs(U32F iJoint);	
	void MoveJointWS(U32F iJoint, Vector_arg moveWs);
	void RotateJointWS(U32F iJoint, Quat_arg rotor);
	void PrerotateJointLs(U32 iJoint, Quat_arg postmul );
	void PostrotateJointLs(U32 iJoint, Quat_arg postmul );
	void TranslateRootBoneWS(Vector_arg trans, const Locator& objectLoc);
	void RotateRootBoneWS(Quat_arg rotor, const Locator& objectLoc);

	I32F GetParentJoint(I32F iJoint) const;
	const ndanim::DebugJointParentInfo* GetParentInfo() const { return ndanim::GetDebugJointParentInfoTable(m_pJointHierarchy); }

	// Joint Parameter (IK-related)
	const ndanim::JointParams* GetDefaultLocalSpaceJoints() const;
	const ndanim::JointParams& GetJointParamsLs(U32F jointIndex) const;
	const ndanim::JointParams GetJointParamsSafeLs(U32F jointIndex) const;
	void SetJointParamsLs(U32F iJoint, const ndanim::JointParams& param);

	LightType GetJointLightType(U32F iJoint) const;
	U32 GetJointLightIndex(U32F iJoint) const;
	void SetJointLightType(U32F iJoint, LightType lightType);
	void SetJointLightIndex(U32F iJoint, U32 lightIndex);

	// Raw accessors. Treat data as const. If you want non-const, use the *ForOutput variants!
	const ndanim::JointParams* GetJointParamsLs() const			{ return m_pLocalSpaceJoints; }
	const Locator* GetJointLocatorsWs()	const					{ ((JointCache*)this)->ConvertAllWsLocs(); return m_pWorldSpaceLocs; }
	const float* GetInputControls() const						{ return m_pInputControls; }
	const float* GetOutputControls() const						{ return m_pOutputControls; }
	const ndanim::InputControlDriver* GetInputControlDrivers() const;
	const ndanim::JointHierarchy* GetJointHierarchy() const		{ return m_pJointHierarchy; }

	Locator* GetJointLocatorsWsForOutput() { ValidateAllWsLocs(); return m_pWorldSpaceLocs; }		// will write locators myself, so set up-to-date bits
	Transform* GetJointTransformsForOutput() { return m_pJointTransforms; }

	// Sets objXforms and invalidates WS locators
	void SetObjXForm(const Transform& objXform, bool invalidateLocWs = true);
	
	// Debug
	void DebugDrawBindPoseSkel() const;
	void DebugDrawSkel(bool includeConstrainedJoints,
					   float thicknessScale = 1.0f,
					   I32F iStart = -1,
					   I32F iEnd   = -1) const;
	void DebugDrawInputControlDrivers() const;

	void UpdateWSSubPose(const Locator& align, U32F iJoint);
	void UpdateWSRootSubPose(const Locator& align, Vector_arg uniformScale = Vector(1.0f, 1.0f, 1.0f));

	bool GetInBindPose() const { return m_bInBindPose; }

	void SetJointAabbsLs(const Aabb* pAabbs) { m_pJointAabbsLs = pAabbs; }
	const Aabb* GetJointAabbsLs() const { return m_pJointAabbsLs; }

	void InvalidateAllTransforms()	{ m_upToDateTransforms.ClearAllBits(); }
	void InvalidateAllWsLocs()	{ m_upToDateWsLocs.ClearAllBits(); }
	void ValidateAllTransforms()	{ m_upToDateTransforms.SetAllBits(); }
	void ValidateAllWsLocs()	{ m_upToDateWsLocs.SetAllBits(); }

	void InvalidateOutputControls() { m_bOutputControlsValid = false; }
	void ValidateOutputControls() { m_bOutputControlsValid = true; }

private:
	void ConvertAllTransforms();
	void ConvertTransform(unsigned int index);
	void ConvertAllWsLocs();
	void ConvertWsLoc(unsigned int index);

	Transform m_objXform;
	mutable ExternalBitArray m_upToDateTransforms;
	mutable ExternalBitArray m_upToDateWsLocs;

	ArtItemSkeletonHandle		m_skelHandle;
	const ndanim::JointHierarchy*	m_pJointHierarchy;  // relo. External pointer. No relocation needed.

	ndanim::JointParams*		m_pLocalSpaceJoints;	// (relo) Pointer to a buffer to output JointHierarchy::m_numJointsInSet[0] JointParams 
	Locator*					m_pWorldSpaceLocs;		// (relo) Pointer to a buffer to output JointHierarchy::m_numJointsInSet[0] JointParams 
	Transform*					m_pJointTransforms;		// (relo)
	LightType*					m_pJointLightTypes;		// (relo) Pointer to a buffer to the light joint's type, only allocated for light-skel
	U32*						m_pJointLightIndices;	// (relo) Pointer to a buffer to the light joint's index, only allocated for light-skel
	float*						m_pInputControls;		// (relo) Pointer to a buffer to input JointHierarchy::m_numInputControls floats
	float*						m_pOutputControls;		// (relo) Pointer to a buffer to output JointHierarchy::m_numOutputControls floats
														//			(i.e. normal maps, blendshapes, wrinkle maps)

	const Aabb*					m_pJointAabbsLs;		// External pointer. No relocation needed. This will have useful data for joints that have collision. Used for per-bone volumetric lighting.

	bool						m_bInBindPose;			// this flag is reset as soon as we start animating. It does not check to see if the anim is actually different from bind pose
	bool						m_bOutputControlsValid;
};
