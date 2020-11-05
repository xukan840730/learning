/*
 * Copyright (c) 2009 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/math/locator.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/joint-modifiers/leg-ik-data.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class JacobianMap;
class JointLimits;

/// --------------------------------------------------------------------------------------------------------------- ///
class JointModifierData
{
public:
	struct HideJointData 
	{
		static const I32 kMaxHiddenJoints = 128;

		U8 m_hiddenJointIndices[kMaxHiddenJoints];
		U8 m_numHiddenJoints;

		U8 m_magazineIndex;
		Locator m_wsMagazineLocator;
		bool m_hideMagazine;
		bool m_attachMagToParent;

		U8 m_barrelIndex;
		F32 m_barrelRot;

		U8 m_burstSelectorIndex;
		F32 m_burstSelectorAngle;
	};

	struct JointLimitData 
	{
		static const I32 kMaxLimitJoints = 5;
		Quat m_bindPoseRot[kMaxLimitJoints];
		float m_limitValues[kMaxLimitJoints];		// degrees
		U8 m_jointChainIndices[kMaxLimitJoints];	// spined, neck, heada, headb
		U32 m_numLimits;
	};

	struct EyeIkData 
	{
		U32 m_numAnimatedJoints;

		Point m_lookAtPointWs;
		Point m_lookAtPointOs;
		U16 m_eyeJointId[2];				// Left(0), Right(1)
		U16 m_eyeParentJointId[2];			// Left(0), Right(1)
		Locator m_defaultEyeJointLocLs[2];	// Left(0), Right(1)
		F32 m_xAngleMin;
		F32 m_xAngleMax;
		F32 m_yAngleMin;
		F32 m_yAngleMax;
	};

	struct ArmJointIndices
	{
		I16 m_shoulder;
		I16 m_elbow;
		I16 m_wrist;
		I16 m_prop;
	};

	struct WeaponGripIkData 
	{
		float m_fade;
		float m_handBlend;
		U8 m_hand; // which hand to make dominant (default = 1, right hand)

		ArmJointIndices m_jointIndices[2];
		JacobianMap* m_pJacobianMap[2];
	};

	struct ArmIkData 
	{
		float m_fade;

		Locator m_targetLocOs[2];

		ArmJointIndices m_jointIndices[2];
	};

	struct NetStrafeIkData
	{
		float m_chestRotBlend;
		Quat m_chestRot;
		JacobianMap* m_pJacobianMap;
		JointLimits* m_pJointLimits;
	};

	struct StrideScaleIkData
	{
		Transform m_strideTransform;
	};

	struct OutputData
	{
		LegIkPersistentData m_legIkPersistentData;
		Quat m_averageEyeRotPreIkWs;
		float m_weaponGripPropErr;
	};

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
	{
		m_legIkData.Relocate(deltaPos, lowerBound, upperBound);

		RelocatePointer(m_weaponGripIkData.m_pJacobianMap[0], deltaPos, lowerBound, upperBound);
		RelocatePointer(m_weaponGripIkData.m_pJacobianMap[1], deltaPos, lowerBound, upperBound);
	}

	void EnableEyeIk()			{ m_flags |= kEyeIkEnabled; }
	void DisableEyeIk()			{ m_flags &= ~kEyeIkEnabled; }
	void EnableWeaponIk()		{ m_flags |= kWeaponIkEnabled; }
	void DisableWeaponIk()		{ m_flags &= ~kWeaponIkEnabled; }
	void EnableArmIk()			{ m_flags |= kArmIkEnabled; }
	void DisableArmIk()			{ m_flags &= ~kArmIkEnabled; }
	void EnableLegIk()			{ m_flags |= kLegIkEnabled; }
	void DisableLegIk()			{ m_flags &= ~kLegIkEnabled; }
	void EnableEyeOutput()		{ m_flags |= kEyeIkOutputEnabled; }
	void DisableEyeOutput()		{ m_flags &= ~kEyeIkOutputEnabled; }
	void EnableStrideIk()		{ m_flags |= kStrideIkEnabled; }
	void DisableStrideIk()		{ m_flags &= ~kStrideIkEnabled; }
	void EnableWeaponMod()		{ m_flags |= kWeaponModEnabled; }
	void DisableWeaponMod()		{ m_flags &= ~kWeaponModEnabled; }
	void EnableNetStrafeIk()	{ m_flags |= kNetStrafeIkEnabled; }
	void DisableNetStrafeIk()	{ m_flags &= ~kNetStrafeIkEnabled; }

	bool IsEyeIkEnabled() const			{ return (m_flags & kEyeIkEnabled) != 0; }
	bool IsWeaponIkEnabled() const		{ return (m_flags & kWeaponIkEnabled) != 0; }
	bool IsArmIkEnabled() const			{ return (m_flags & kArmIkEnabled) != 0; }
	bool IsLegIkEnabled() const			{ return (m_flags & kLegIkEnabled) != 0; }
	bool IsEyeOutputEnabled() const		{ return (m_flags & kEyeIkOutputEnabled) != 0; }
	bool IsStrideIkEnabled() const		{ return (m_flags & kStrideIkEnabled) != 0; }
	bool IsHideJointEnabled() const		{ return (m_flags & kWeaponModEnabled) != 0; }
	bool IsNetStrafeIkEnabled() const	{ return (m_flags & kNetStrafeIkEnabled) != 0; }

	EyeIkData*			GetEyeIkData()			{ return &m_eyeIkData; }
	WeaponGripIkData*	GetWeaponGripIkData()	{ return &m_weaponGripIkData; }
	ArmIkData*			GetArmIkData()			{ return &m_armIkData; }
	LegIkData*			GetLegIkData()			{ return &m_legIkData; }
	OutputData*			GetOutputData()			{ return &m_outputData; }
	StrideScaleIkData*	GetStrideIkData()		{ return &m_strideIkData; }
	HideJointData*		GetWeaponModData()		{ return &m_hideJointData; }
	JointLimitData*		GetJointLimitData()		{ return &m_jointLimitData; }
	NetStrafeIkData*	GetNetStrafeIkData()	{ return &m_netStrafeIkData; }

	const EyeIkData*			GetEyeIkData() const		{ return &m_eyeIkData; }
	const WeaponGripIkData*		GetWeaponGripIkData() const	{ return &m_weaponGripIkData; }
	const ArmIkData*			GetArmIkData() const		{ return &m_armIkData; }
	const LegIkData*			GetLegIkData() const		{ return &m_legIkData; }
	const OutputData*			GetOutputData() const		{ return &m_outputData; }
	const StrideScaleIkData*	GetStrideIkData() const		{ return &m_strideIkData; }
	const HideJointData*		GetWeaponModData() const	{ return &m_hideJointData; }
	const JointLimitData*		GetJointLimitData() const	{ return &m_jointLimitData; }
	const NetStrafeIkData*		GetNetStrafeIkData() const	{ return &m_netStrafeIkData; }
// 	const JointOverrideData*	GetJointOverrideData() const	{ return &m_jointOverrideData; }

private:

	typedef U32 Flags;
	enum
	{
		kEyeIkEnabled			= (1 << 0),
		kWeaponIkEnabled		= (1 << 1),
		kArmIkEnabled			= (1 << 2),
		kLegIkEnabled			= (1 << 3),
		kEyeIkOutputEnabled		= (1 << 4),
		kStrideIkEnabled		= (1 << 5),
		kWeaponModEnabled		= (1 << 6),
		kNetStrafeIkEnabled		= (1 << 7),
	};

	Flags				m_flags;

	EyeIkData			m_eyeIkData;
	WeaponGripIkData	m_weaponGripIkData;
	ArmIkData			m_armIkData;
	LegIkData			m_legIkData;
	StrideScaleIkData	m_strideIkData;
	HideJointData		m_hideJointData;
	JointLimitData		m_jointLimitData;
	NetStrafeIkData		m_netStrafeIkData;

	OutputData			m_outputData;
};
