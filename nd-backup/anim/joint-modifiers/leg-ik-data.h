/*
 * Copyright (c) 2009 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/util/tracker.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class JacobianMap;
class JointLimits;

namespace DC
{
	struct LegIkConstants;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct LegIkInfoShared
{
	float m_footPos[kQuadLegCount];
	float m_probePos[kQuadLegCount];
	float m_rootPos;
	bool m_initialized;
};

CONST_EXPR StringId64 kDebugLegIkConstantsId = SID("*debug-leg-ik-constants*");

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimGroundPosOs
{
	AnimGroundPosOs() 
	{ 
		m_valid[0] = false;
		m_valid[1] = false;
	}

	bool m_valid[2];
	Point m_posOs[2];
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct LegIkData
{
	friend class AiLegModifier;
	friend class LegModifier;

public:
	typedef U32 Flags;
	enum
	{
		kFlagValid		= (1 << 0),
		kFlagHitGround	= (1 << 1),
		kFlagHitWater	= (1 << 2),
		kFlagHitStairs	= (1 << 3),
		kFlagAnimGround = (1 << 4),
	};

	struct ProbeResult
	{
		bool Valid() const { return (m_flags & kFlagValid) != 0; }
		bool HitGround() const { return (Valid() && ((m_flags & kFlagHitGround) != 0)); }
		bool HitStairs() const { return (Valid() && ((m_flags & kFlagHitStairs) != 0)); }
		bool AnimGroundValid() const { return (Valid() && ((m_flags & kFlagAnimGround) != 0)); }

		void Reset()
		{
			m_flags = 0;
			m_posOs = kOrigin;
			m_normalOs = kUnitYAxis;
			m_animGroundOs = kIdentity;
			m_animGroundLevel = 0.0f;
		}

		Point m_posOs;
		Vector m_normalOs;
		Locator m_animGroundOs;
		Flags m_flags;
		float m_animGroundLevel;
	};

	struct JointIndices
	{
		// please note that these joint indices may not all be valid for every character
		I16 m_upperThigh;
		I16 m_knee;
		I16 m_ankle;
		I16 m_heel;
		I16 m_ball;
		I16 m_spine;
		I16 m_neck;
		I16 m_head;
	};

	Vector GetHipAxis(int index) const { return m_hipAxis[index]; }
	Quat GetIkJointAxisAdjustment(int index) const { return m_ikJointAxisAdjustment[index]; }
	Vector GetNaturalAnimNormalOs() const { return m_naturalAnimNormalOs; }
	Vector GetSmoothedGroundNormalOs() const { return m_smoothedGroundNormalOs; }
	const ProbeResult& GetProbeResult(int index) const { return m_probeResults[index]; }
	const JointIndices& GetJointIndices(int index) const { return m_jointIndices[index]; }
	JacobianMap* GetLegJacobianMap(int index) const { return m_pJacobianMap[index]; }
	JacobianMap* GetSpineJacobianMap() const { return m_pSpineJacobianMap; }
	JacobianMap* GetHeadJacobianMap() const { return m_pHeadJacobianMap; }

	JointLimits* GetJointLimits() const { return m_pJointLimits; }

	float GetDefaultLegPointHeightOs(int index) const { return m_defaultLegPointHeightOs[index]; }
	float GetDeltaYFromLastFrame() const { return m_deltaYFromLastFrame; }
	void SetDeltaYFromLastFrame(float dy) { m_deltaYFromLastFrame = dy; }
	void SetNaturalAnimNormalOs(Vector_arg normalOs) { m_naturalAnimNormalOs = normalOs; }
	void SetSmoothedGroundNormalOs(Vector_arg normalOs) { m_smoothedGroundNormalOs = normalOs; }

	float GetBlend() const { return m_blend; }
	U8 GetMode() const { return m_mode; }

	Point GetFreezeLegPosWs(int index) const { return m_freezeLegPosWs[index]; }

	float GetMeleeRootShift() const { return m_meleeRootShift; }
	bool IsMeleeRootShiftSet() const { return m_meleeIkRootShiftSet; }
	bool NoFootAdjustLimit() const { return m_noFootAdjustLimit; }
	float NoRootDeltaSpring() const { return m_noRootDeltaSpring; }

	bool GetRootAdjustLimitFactors(Vec2& minMax) const
	{
		if (m_rootAdjustLimitFactorsSet)
		{
			minMax = m_rootAdjustLimitFactors;
		}

		return m_rootAdjustLimitFactorsSet;
	}

	float GetAllowLowerFeetFactor() const { return m_allowLowerFeet; }


	StringId64 GetIkConstantsId() const { return m_constantsId; }
	const DC::LegIkConstants& GetIkConstants() const
	{
		ANIM_ASSERT(m_pConstants);
		return *m_pConstants;
	}

	void ResetProbeResults()
	{
		memset(m_probeResults, 0, sizeof(ProbeResult) * kQuadLegCount); 
		for (int i = 0; i < kQuadLegCount; ++i)
		{
			m_resetGroundSpring.SetBit(i);
		}
	}

	bool IsQuadruped() const { return m_isQuadruped; }
	I16 GetIkTargetJointIndex(int legIndex) const
	{
		return ShouldUseBallAsIkTarget() ? m_jointIndices[legIndex].m_ball : m_jointIndices[legIndex].m_ankle;
	}
	bool ShouldUseBallAsIkTarget() const { return m_useBallJointAsTarget; }
	bool SpringIkCorrection(int iLeg) const { return m_springIkCorrection.IsBitSet(iLeg); }
	bool DebugDraw() const { return m_debugDraw; }
	bool DebugDrawGraphs() const { return m_debugDrawGraphs; }
	bool FirstFrame() const { return m_firstFrame; }
	void SetFirstFrame(bool ff) { m_firstFrame = ff; }

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
	{
		RelocatePointer(m_pHeadJacobianMap, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_pSpineJacobianMap, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_pJointLimits, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_pConstants, deltaPos, lowerBound, upperBound);

		for (int i = 0; i < kQuadLegCount; ++i)
		{
			RelocatePointer(m_pJacobianMap[i], deltaPos, lowerBound, upperBound);
		}
	}

	void SetPlantLevel(int iLeg, float level)
	{
		ANIM_ASSERT(iLeg < kQuadLegCount);
		ANIM_ASSERT(IsReasonable(level));
		m_plantLevel[iLeg] = level;
	}

	float GetPlantLevel(int iLeg) const { return m_plantLevel[iLeg]; }

	bool ShouldResetGroundSpring(int iLeg) const { return m_resetGroundSpring.IsBitSet(iLeg); }

private:
	U8							m_mode;
	Quat						m_ikJointAxisAdjustment[kQuadLegCount];	// Rotation adjustment to the z axis in JOINT SPACE
	float						m_blend;
	ProbeResult					m_probeResults[kQuadLegCount];
	float						m_defaultLegPointHeightOs[kQuadLegCount];
	float						m_deltaYFromLastFrame;
	Vector						m_hipAxis[kQuadLegCount];
	Vector						m_naturalAnimNormalOs;
	Vector						m_smoothedGroundNormalOs;
	Point						m_freezeLegPosWs[kQuadLegCount];
	JacobianMap*				m_pJacobianMap[kQuadLegCount];
	JacobianMap*				m_pSpineJacobianMap;
	JacobianMap*				m_pHeadJacobianMap;

	JointLimits*				m_pJointLimits;

	JointIndices				m_jointIndices[kQuadLegCount];
	float						m_meleeRootShift;
	Vec2						m_rootAdjustLimitFactors; // how much to allow the ik to procedurally move the root. Only applies if m_rootAdjustLimitFactorsSet is set
	float						m_allowLowerFeet;
	float						m_noRootDeltaSpring;
	float						m_plantLevel[kQuadLegCount];

	const DC::LegIkConstants*	m_pConstants;
	StringId64					m_constantsId;

	BitArray8					m_resetGroundSpring;
	BitArray8					m_springIkCorrection;
	bool						m_rootAdjustLimitFactorsSet : 1;
	bool						m_meleeIkRootShiftSet : 1;
	bool						m_noFootAdjustLimit : 1;
	bool						m_debugDraw : 1;
	bool						m_debugDrawGraphs : 1;
	bool						m_firstFrame : 1;
	bool						m_isQuadruped : 1;
	bool						m_useBallJointAsTarget : 1;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct LegIkPersistentData
{
	float GetDeltaTime() const { return m_deltaTime; }

	Vector m_normals[kQuadLegCount];
	SpringTracker<Vector> m_normalTrackers[kQuadLegCount];

	float m_groundHeight[kQuadLegCount];
	SpringTracker<float> m_groundHeightTrackers[kQuadLegCount];

	float m_deltaTime;

	float m_rootHeightDelta;
	float m_lastRootHeightDelta;
	float m_rootYSpeed;
	float m_rootHeight;
	float m_lastRootHeight;
	float m_lastAppliedRootDelta;
	SpringTracker<float> m_rootYSpeedSpring;
	SpringTracker<float> m_rootYSpring;

	float m_spineHeightDelta;
	float m_lastSpineHeightDelta;
	float m_spineYSpeed;
	float m_spineHeight;
	float m_lastSpineHeight;
	float m_lastAppliedSpineDelta;
	SpringTracker<float> m_spineYSpeedSpring;
	SpringTracker<float> m_spineYSpring;

	float m_sprungIkAdjustment[kQuadLegCount];
	SpringTracker<float> m_ikAdjTracker[kQuadLegCount];
	float m_ikAdjDelta[kQuadLegCount];
	SpringTracker<float> m_ikAdjDeltaTracker[kQuadLegCount];

	Quat m_ikRotDelta[kQuadLegCount];
	SpringTrackerQuat m_ikRotDeltaTracker[kQuadLegCount];

	Vector m_lastFrameLegProcAdjust[kQuadLegCount]; // How much we moved the IK target last frame to compensate for spine movement. currently only used for front legs, but it's sized for all legs to make access simpler
	SpringTracker<Vector> m_legProcAdjustSpring[kQuadLegCount]; // Currently only used for front legs, but it's sized for all legs to make access simpler

	Point m_lastGroundPosOs[kQuadLegCount]; // the ground position last frame

	Quat m_lastFrameHeadRotAdjust;
	SpringTrackerQuat m_headRotAdjustSpring;

	LegIkInfoShared m_ikInfo;	// Data that is needed to sync 2 chars when meleeing

	void Reset()
	{
		memset(this, 0, sizeof(LegIkPersistentData));
		m_lastFrameHeadRotAdjust = kIdentity;
		
		for (int i = 0; i < ARRAY_COUNT(m_ikRotDelta); ++i)
		{
			m_ikRotDelta[i] = kIdentity;
		}
	}
};
