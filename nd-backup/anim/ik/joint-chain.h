/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-debug.h"
#include "ndlib/process/process-handles.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemSkeleton;
class JacobianMap;
class NdGameObject;
struct JointConstraintData;

namespace DC
{
	struct IkConstraintInfo;
}

namespace OrbisAnim
{
	struct ValidBits;
}

namespace ndanim
{
	struct JointParams;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class JointSet
{
public:
	enum
	{
		kTypeInvalid,

		kTypeTree,

		kTypeChain,
		kTypeChainArm,
		kTypeChainLeg,
	};

	// this is duplicated in JointSetConstraint
	enum
	{
		kParentJointOffset = 0,
		kStartJointOffset = 1,
	};


	JointSet();
	virtual ~JointSet();

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	virtual void RelocateOwner(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
	{
		RelocatePointer(m_pGo, deltaPos, lowerBound, upperBound);
	}

	void InitIkData(StringId64 constraintInfoId); // Allocates memory
	void UpdateIkData(bool force = false);

	bool IsInitialized() const { return m_numJoints > 0; }
	int GetNumJoints() const { return m_numJoints; }
	bool IsTree() { return m_type == kTypeTree; }
	bool IsChain() { return m_type >= kTypeChain; }
	bool IsIkDataValid() const { return m_ikData != nullptr; }
	int GetType() { return m_type; }
	bool IsValid() { return m_jointData; }

	const DC::IkConstraintInfo* GetJointConstraints() const;

	const NdGameObject* GetNdGameObject() const { return m_pGo; }
	NdGameObject* GetNdGameObject() { return m_pGo; }

	int GetJointIndex(I32F jointOffset) const;
	I32F GetJointOffset(I32F jointIndex) const;
	StringId64 GetJointId(I32F jointOffset) const;

	virtual int GetParentOffset(I32F jointOffset) const = 0;

	int FindJointOffset(StringId64 jointId) const; // offset in joint chain
	int FindJointIndex(StringId64 jointId) const;  // index in joint cache

	bool IsAncestor(int ancestorOffset, I32F jointOffset) const;

	const Locator& GetJointLocLs(I32F jointOffset) const; // Local/joint space
	const Locator& GetJointLocWs(I32F jointOffset);		  // World space
	const Locator GetJointLocOs(I32F jointOffset);		  // Object space

	// versions that take the original joint index and convert to offset for you
	const Locator& GetJointLocLsIndex(I32F jointIndex) const; // Local/joint space
	const Locator& GetJointLocWsIndex(I32F jointIndex);		  // World space
	const Locator GetJointLocOsIndex(I32F jointIndex);		  // Object space

	// be cautious! remember to call UpdateAllJointLocsWs() before call GetRawJointLocWs(), or data is not updated!
	// in most cases, you should call ::GetJointLocWs() instead.
	const Locator GetRawJointLocWs(I32F jointOffset) const;

	const Locator& GetJointIdLocWs(StringId64 jointId);
	const Locator GetJointIdLocOs(StringId64 jointId);

	float GetChainLength(int startJointOffset, int endJointOffset) const;
	float GetRootScale() const;

	// Is every joint in this JointSet valid in the given validBits?
	bool CheckValidBits(const OrbisAnim::ValidBits& validBits, U32F indexBase) const;

	bool ReadFromJointParams(const ndanim::JointParams* pJointParamsLs,
							 U32F indexBase,
							 U32F numJoints,
							 float rootScale,
							 const OrbisAnim::ValidBits* pValidBits = nullptr,
							 const ndanim::JointParams* pDefaultJointParamsLs = nullptr); // allocates memory

	void WriteJointParamsBlend(float blendAmount,
							   ndanim::JointParams* pJointParamsLs,
							   U32F indexBase,
							   U32F numJoints,
							   bool freeMemory = true);

	void WriteJointValidBits(I32F jointOffset,
							 U32F indexBase,
							 OrbisAnim::ValidBits* pValidBits,
							 bool includingParents = true);

	bool ReadJointCache();								  // Allocate joint buffers & read in joint locators from joint cache
	void WriteJointCache(bool reparentJointCache = true); // Write data back out to joint cache (and free buffers)
	void WriteJointCacheBlend(float blendAmount,
							  bool reparentJointCache = true); // Write data back out to joint cache (and free buffers)
	virtual void DiscardJointCache();						   // Releases the joint cache without writing back to it

	void SetParentJointLocWs(const Locator& loc);

	virtual void InvalidateAllJoints() = 0;
	virtual void UpdateAllJointLocsWs(int rootOffset = kStartJointOffset) = 0;

	Quat RotateJointWs(I32F jointOffset, Quat_arg rot, bool invalidateChildren = true);
	Quat RotateJointWsIndex(I32F jointIndex, Quat_arg rot, bool invalidateChildren = true);
	Quat RotateJointOs(I32F jointOffset, Quat_arg rot, bool invalidateChildren = true);
	Quat RotateJointOsIndex(I32F jointIndex, Quat_arg rot, bool invalidateChildren = true);
	void PreRotateJointLs(I32F jointOffset, Quat_arg rot, bool invalidateChildren = true);
	void PostRotateJointLs(I32F jointOffset, Quat_arg rot, bool invalidateChildren = true);
	void PostRotateJointLsIndex(I32F jointIndex, Quat_arg rot, bool invalidateChildren = true);
	Vector TranslateJointWs(I32F jointOffset, Vector_arg trans, bool invalidateChildren = true);
	Vector TranslateJointLs(I32F jointOffset, Vector_arg trans, bool invalidateChildren = true);
	void TransformJointLs(I32F jointOffset, const Locator& loc, bool invalidateChildren = true);
	void SetJointRotationLs(I32F jointOffset, Quat_arg rot, bool invalidateChildren = true);
	void SetJointLocWs(I32F jointOffset, const Locator& loc, bool invalidateChildren = true);
	void SetJointLocOs(I32F jointOffset, const Locator& locOs, bool invalidateChildren = true);
	void SetJointLocLs(I32F jointOffset, const Locator& loc, bool invalidateChildren = true);
	void SetJointLocLsIndex(I32F jointIndex, const Locator& loc, bool invalidateChildren = true);
	void SetJointLocOsIndex(I32F jointIndex, const Locator& loc, bool invalidateChildren = true);
	void SetJointLocWsIndex(I32F jointIndex, const Locator& loc, bool invalidateChildren = true);

	void SetJointScaleIndex(I32F jointIndex, Vector_arg scale, bool invalidateChildren = true);

	// root joint was translated, update joint chain
	void TranslateRootWs(Vector_arg trans, bool invalidateChildren = true);
	void TranslateRootOs(Vector_arg trans, bool invalidateChildren = true);

	virtual void InvalidateChildren(int rootOffset = kStartJointOffset) = 0;

	Locator GetBindPoseLocLs(I32F jointOffset) const;
	Locator GetBindPoseLocWs(I32F jointOffset);
	void GetInverseBindPoseJointXform(I32F jointOffset, Transform& outXform) const;
	void GetBindPoseJointXform(I32F jointOffset, Transform& outXform) const;

	const Locator& GetParentLocWs(I32F jointOffset);
	const Locator GetParentLocOs(I32F jointOffset);

	void ApplyConstraint(JointConstraintData* ikData, I32F jointOffset, float* debugTextY = nullptr);
	void ApplyConstraints(bool debugDraw = false); // Apply constrains on all joints in the JointSet
	void ApplyConstraints(JacobianMap* pJMap,
						  bool debugDraw = false); // Apply constrains on all joints in the JacobianMap

	void RetargetJointSubsets(const I32F* aiEndJointOffsets,
							  U32F numEndJoints,
							  const ArtItemSkeleton* pSrcSkel,
							  const ArtItemSkeleton* pDstSkel);

	bool IsJointDataValid(I32F jointOffset) const
	{
		if (!m_jointData || !m_jointData->m_jointValid)
		{
			return false;
		}

		if ((jointOffset < 0) || (jointOffset >= m_numJoints))
		{
			return false;
		}

		return m_jointData->m_jointValid[jointOffset];
	}

	void Validate() const;
	void DebugDrawJoints(bool drawJoints = true, 
						bool drawJointDetails = false, 
						bool drawSkeleton = false, 
						Color drawSkeletonColor = kColorWhite, 
						DebugPrimTime tt = kPrimDuration1FramePauseable);

protected:
	typedef ScriptPointer<DC::IkConstraintInfo> IkConstraintsPtr;

	// Allocate when reading in the joint data. Used to keep JointChain size as small as possible.
	struct JointData
	{
		Locator* m_jointLocLs; // joint space locators for each joint
		Locator* m_jointLocWs; // world space locators for each joint
		Vector* m_pJointScale; // scale values for each joint
		float* m_boneLengths;  // length from the current joint to its parent
		bool* m_jointValid;	   // sometimes we don't get data for every joint (if we cross segments, for example)
		float m_rootScale;	   // root locator scale

		// For foot IK
		Vector m_hipAxis;
		Vector m_hipDown;
	};

	struct IkData
	{
		IkConstraintsPtr m_pDcIkConstraints;
		JointConstraintData* m_ikJointData;
	};

	virtual bool Init(NdGameObject* pGo);
	virtual void UpdateJointLocWs(const Locator& parentLocWs, const Locator jointLocLs, Locator* jointLocOutWs);
	virtual void UpdateJointLocWs(I32F jointOffset) = 0;
	virtual void InvalidateJoint(I32F jointOffset, bool invalidateChildren = true) = 0;
	virtual bool IsJointWorldSpaceValid(I32F jointOffset) const = 0;
	virtual void SetJointWorldSpaceValid(I32F jointOffset) = 0;
	virtual void AllocateWorldSpaceValidFlags(int numJoints) = 0;

	NdGameObject* m_pGo;

	I16 m_type;
	I16 m_numJoints;
	I16* m_joints; // array of joint indices

	JointData* m_jointData;
	IkData* m_ikData;

	bool m_useAnimatedSkel;
	SkeletonId m_skelId;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class JointChain : public JointSet
{
public:
	JointChain();

	virtual bool Init(NdGameObject* pGo) override;											 // Add all joints, allocates memory
	virtual bool Init(NdGameObject* pGo, StringId64 startJoint, StringId64 endJoint);			 // Allocates memory
	virtual bool Init(const ArtItemSkeleton* pSkeleton, StringId64 startJoint, StringId64 endJoint); // Allocates memory

	virtual int GetParentOffset(I32F jointOffset) const override;

	virtual void InvalidateChildren(int rootOffset = kStartJointOffset) override;
	virtual void InvalidateAllJoints() override;
	virtual void UpdateAllJointLocsWs(int rootOffset = kStartJointOffset) override;

	void DebugPrintJointRotation() const;

protected:
	virtual void UpdateJointLocWs(I32F jointOffset) override;
	virtual void InvalidateJoint(I32F jointOffset, bool invalidateChildren = true) override;
	virtual bool IsJointWorldSpaceValid(I32F jointOffset) const override;
	virtual void SetJointWorldSpaceValid(I32F jointOffset) override;
	virtual void AllocateWorldSpaceValidFlags(int numJoints) override;

private:
	void UpdateJointLocWsInternal(I32F jointOffset, const Locator& parentLocWs);
	int m_jointLocWsInvalidStart; // world space locators are invalid starting at this joint
};

/// --------------------------------------------------------------------------------------------------------------- ///
class JointTree : public JointSet
{
private:
	I16* m_parentOffset;   // offset of parent joint for every joint
	I16* m_childEndOffset; // offset of last child joint for every joint

public:
	JointTree();
	~JointTree() override;

	virtual bool Init(NdGameObject* pGo) override
	{
		ANIM_ASSERTF(false, ("this variant of Init() is not intended to be used for JointTree"));
		return JointSet::Init(pGo);
	}
	virtual bool Init(NdGameObject* pGo,
					  StringId64 rootJoint,
					  bool useAnimatedSkel,
					  int numEndJoints,
					  ...); // Allocates memory
	virtual bool Init(NdGameObject* pGo,
					  StringId64 rootJoint,
					  bool useAnimatedSkel,
					  int numEndJoints,
					  const StringId64* endJointNames); // Allocates memory
	virtual bool Init(const ArtItemSkeleton* pSkeleton,
					  StringId64 rootJoint,
					  int numEndJoints,
					  const StringId64* endJointNames); // Allocates memory
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual void InvalidateChildren(int rootOffset = kStartJointOffset) override;
	virtual void InvalidateAllJoints() override;
	virtual void UpdateAllJointLocsWs(int rootOffset = kStartJointOffset) override;
	virtual void DiscardJointCache() override; // Releases the joint cache without writing back to it

	virtual int GetParentOffset(I32F jointOffset) const override;

protected:
	bool DoInit(const ArtItemSkeleton* pSkeleton,
				StringId64 rootJoint,
				int numEndJoints,
				const StringId64* endJointNames);

	virtual void UpdateJointLocWs(I32F jointOffset) override;
	virtual void InvalidateJoint(I32F jointOffset, bool invalidateChildren = true) override;
	virtual bool IsJointWorldSpaceValid(I32F jointOffset) const override;
	virtual void SetJointWorldSpaceValid(I32F jointOffset) override;
	virtual void AllocateWorldSpaceValidFlags(int numJoints) override;

private:
	bool* m_jointLocWsValid; // whether or not each ws locator is valid
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct JointSetObserver : public ScriptObserver
{
	JointSetObserver();
	virtual void OnModuleImported(StringId64 moduleId, Memory::Context allocContext) override;

	int m_loadedThisFrame;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSetInit();
void JointSetUpdate();
