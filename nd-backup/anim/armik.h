/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/ik/joint-chain.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemSkeleton;
class Character;
class NdGameObject;
struct FgAnimData;

///////////////////////////////////////////////////////////////////////////////
// New Arm IK
///////////////////////////////////////////////////////////////////////////////

/// --------------------------------------------------------------------------------------------------------------- ///
class ArmIkChain : public JointChain
{
public:
	enum
	{
		kRoot,
		kShoulder,
		kElbow,
		kWrist,
		kPropAttach,

		kJointCount
	};

	static const StringId64 kJointIds[kArmCount][kJointCount];

	static StringId64 GetShoulderId(int armIndex) {return kJointIds[armIndex][kShoulder];}
	static StringId64 GetElbowId(int armIndex) {return kJointIds[armIndex][kElbow];}
	static StringId64 GetWristId(int armIndex) {return kJointIds[armIndex][kWrist];}

private:
	I8			m_armIndex;
	I8			m_jointOffsets[kJointCount];

	bool		m_hackOverrideArmLen;
	float		m_hackUpperArmLen;
	float		m_hackLowerArmLen;

public:
	ArmIkChain();

	virtual bool Init(NdGameObject* pGo) override
	{
		ANIM_ASSERTF(false, ("this variant of Init() it not intended to be used for ArmIkChain"));
		return JointChain::Init(pGo);
	}
	virtual bool Init(NdGameObject* pGo, StringId64 startJoint, StringId64 endJoint) override
	{
		ANIM_ASSERTF(false, ("this variant of Init() it not intended to be used for ArmIkChain"));
		return JointChain::Init(pGo, startJoint, endJoint);
	}
	virtual bool Init(const ArtItemSkeleton* pSkel, StringId64 startJoint, StringId64 endJoint) override
	{
		ANIM_ASSERTF(false, ("this variant of Init() it not intended to be used for ArmIkChain"));
		return JointChain::Init(pSkel, startJoint, endJoint);
	}

	bool Init(NdGameObject* pGo, ArmIndex armIndex, bool includeRoot=false, bool includePropAttach=false);		// Allocates memory
	bool Init(const ArtItemSkeleton* pSkel, ArmIndex armIndex, bool includeRoot = false, bool includePropAttach = false);		// Allocates memory

	int RootOffset() {return m_jointOffsets[kRoot];}
	int ShoulderOffset() {return m_jointOffsets[kShoulder];}
	int ElbowOffset() {return m_jointOffsets[kElbow];}
	int WristOffset() {return m_jointOffsets[kWrist];}
	int PropAttachOffset() {return m_jointOffsets[kPropAttach];}

	int RootJointIndex() {return GetJointIndex(m_jointOffsets[kRoot]);}
	int ShoulderJointIndex() {return GetJointIndex(m_jointOffsets[kShoulder]);}
	int ElbowJointIndex() {return GetJointIndex(m_jointOffsets[kElbow]);}
	int WristJointIndex() {return GetJointIndex(m_jointOffsets[kWrist]);}
	int PropAttachJointIndex() {return GetJointIndex(m_jointOffsets[kPropAttach]);}

	const Locator& GetRootLocWs() {return GetJointLocWs(m_jointOffsets[kRoot]);}
	const Locator& GetShoulderLocWs() {return GetJointLocWs(m_jointOffsets[kShoulder]);}
	const Locator& GetElbowLocWs() {return GetJointLocWs(m_jointOffsets[kElbow]);}
	const Locator& GetWristLocWs() {return GetJointLocWs(m_jointOffsets[kWrist]);}
	const Locator& GetPropAttachLocWs() {return GetJointLocWs(m_jointOffsets[kPropAttach]);}

	Quat RotateRootWs(Quat_arg rot) {return RotateJointWs(m_jointOffsets[kRoot], rot);}
	Quat RotateShoulderWs(Quat_arg rot) {return RotateJointWs(m_jointOffsets[kShoulder], rot);}
	Quat RotateElbowWs(Quat_arg rot) {return RotateJointWs(m_jointOffsets[kElbow], rot);}
	Quat RotateWristWs(Quat_arg rot) {return RotateJointWs(m_jointOffsets[kWrist], rot);}
	Quat RotatePropAttachWs(Quat_arg rot) {return RotateJointWs(m_jointOffsets[kPropAttach], rot);}

	float GetUpperArmLen() {return m_hackOverrideArmLen ? m_hackUpperArmLen : GetChainLength(ShoulderOffset(), ElbowOffset());}
	float GetLowerArmLen() {return m_hackOverrideArmLen ? m_hackLowerArmLen : GetChainLength(ElbowOffset(), WristOffset());}

	I8 GetArmIndex() const { return m_armIndex; }

	void SetHackOverrideArmLen(float upperArmLen, float lowerArmLen);
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ArmIkInstance
{
	JointSet*		m_ikChain = nullptr;
	int				m_armIndex = -1;		// Only needed if m_pJoints isn't type ArmIkChain
	Point			m_goalPosWs = kOrigin;
	Point			m_outputGoalPosWs = kOrigin;
	float			m_tt = 0.0f;
	bool			m_abortIfCantSolve = false;
	I32				m_jointOffsetsUsed[3] = { -1 };
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool SolveArmIk(ArmIkInstance* ik);

/// --------------------------------------------------------------------------------------------------------------- ///
class WeaponIK
{
private:
	ArmIkChain m_armIks[2];
	bool m_hasInited;

public:
	WeaponIK();
	virtual ~WeaponIK() {};
	void Init(Character* pCharacter);
	virtual void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound);
	bool Apply(float scale, FgAnimData& animData, float fade, U32 hand, bool abortIfCantSolve = false);
};

/// --------------------------------------------------------------------------------------------------------------- ///
class WeaponIKFeatherInfo
{
public:
	WeaponIKFeatherInfo(Character* pCharacter, StringId64 ikSettingsId);
	void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound);

private:
	friend class WeaponIKFeather;

	JointTree m_arms[2];
	JacobianMap m_ikJacobianMaps[2];
};

/// --------------------------------------------------------------------------------------------------------------- ///
class WeaponIKFeather : public WeaponIK
{
private:
	typedef WeaponIK ParentClass;
public:

	WeaponIKFeather(WeaponIKFeatherInfo* pIkInfo);
	bool Apply(WeaponIKFeatherInfo* pIkInfo, float scale, FgAnimData& animData, float fade, U32 hand, float handBlend, bool abortIfCantSolve);
	void ResetOffsets(WeaponIKFeatherInfo* pIkInfo);

	static void SolveWeaponFeatherIk(JointSet* (&apJoints)[2],
									 JacobianMap*(apJacobians)[2],
									 U32 hand,
									 float handBlend,
									 float ikFade,
									 int maxIter,
									 float ikRestorePct,
									 int minNumIter = -1);
};
