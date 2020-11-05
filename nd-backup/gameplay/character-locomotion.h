/*
 * Copyright (c) 2016 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/ringqueue.h"

#include "ndlib/util/maybe.h"

#include "gamelib/anim/motion-matching/motion-matching.h"
#include "gamelib/gameplay/nd-subsystem-anim-action.h"

#include <functional>

/// --------------------------------------------------------------------------------------------------------------- ///
class Character;
class IMotionPose;
class MotionModel;
class MotionModelIntegrator;
class MotionMatchingSet;
struct MotionModelInput;
struct MotionModelPathInput;

namespace DC
{
	struct MotionMatchingSettings;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class ICharacterLocomotion : public NdSubsystemAnimController
{
public:
	struct InputData;

	typedef MotionModelInput (*StickFunc)(const Character* pChar,
										  const InputData* pInput,
										  const MotionMatchingSet* pArtItemSet,
										  const DC::MotionMatchingSettings* pSettings,
										  const MotionModelIntegrator& integratorPs,
										  TimeFrame delta,
										  bool finalize,
										  bool debug);

	typedef I32F (*GroupIndexFunc)(const Character* pChar,
								   const MotionMatchingSet* pArtItemSet,
								   const DC::MotionMatchingSettings* pSettings,
								   const MotionModelIntegrator& integratorPs,
								   Point_arg trajectoryEndPosOs,
								   bool debug);

	typedef void (*LayerFunc)(const Character* pChar,
							  MMSearchParams& searchParams,
							  bool debug);

	typedef MotionModelPathInput (*PathFunc)(const Character* pChar, const MotionMatchingSet* pArtItemSet, bool debug);

	/// --------------------------------------------------------------------------------------------------------------- ///
	enum class AnimChangeMode
	{
		kDefault,
		kSuppressed,
		kForced
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct InputData
	{
		void ApplyTransform(const Transform& xform)
		{
			m_desiredVelocityDirPs = m_desiredVelocityDirPs * xform;
			m_groundNormalPs = m_groundNormalPs * xform;

			if (m_desiredFacingPs.Valid())
			{
				m_desiredFacingPs = m_desiredFacingPs.Get() * xform;
			}
		}

		Vector m_desiredVelocityDirPs	= kZero;
		Vector m_groundNormalPs			= kUnitYAxis;
		Maybe<Vector> m_desiredFacingPs = MAYBE::kNothing;

		StringId64 m_setId			= INVALID_STRING_ID_64;
		StringId64 m_transitionsId	= INVALID_STRING_ID_64;
		StringId64 m_playerMoveMode	= INVALID_STRING_ID_64;

		StickFunc m_pStickFunc			= nullptr;
		GroupIndexFunc m_pGroupFunc		= nullptr;
		PathFunc m_pPathFunc			= nullptr;
		LayerFunc m_pLayerFunc	= nullptr;

		TimeFrame m_transitionInterval = Seconds(0.0f);

		MMSearchParams m_mmParams;

		float m_speedScale		   = 1.0f;
		float m_translationSkew	   = 1.0f;
		float m_baseYawSpeed	   = 0.0f;
		float m_groundAdjustFactor = 1.0f;
		float m_navMeshAdjustFactor	   = 1.0f;
		float m_strafeLockAngleDeg	   = -1.0f;
		float m_softClampAlphaOverride = -1.0f;
		float m_softClampAlphaMinFactor = 0.0f;
		float m_retargetScaleOverride = -1.0f;

		AnimChangeMode m_animChangeMode = AnimChangeMode::kDefault;

		bool m_forceExternalPose = false;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct LocomotionState
	{
		Locator m_alignPs;
		Vector m_velPs;
		TimeFrame m_time;
		float m_yawSpeed;
	};

	static LocomotionState CreateLocomotionState(const NdGameObject* pOwner);

	typedef void (*InputFunc)(Character* pChar, InputData* pData);
	typedef const IMotionPose* (*PoseFunc)(const Character* pChar, const MotionMatchingSet* pArtItemSet, bool debug);

	typedef StaticRingQueue<LocomotionState, 30> LocomotionHistory;

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct SpawnInfo : public SubsystemSpawnInfo
	{
		StringId64 m_locomotionInterfaceType = INVALID_STRING_ID_64;
		const LocomotionHistory* m_pHistorySeed = nullptr;
		
		// Special case for blending out of igcs
		float m_initialBlendTime = -1.0f; 
		DC::AnimCurveType m_initialBlendCurve = DC::kAnimCurveTypeUniformS;
		float m_initialMotionBlendTime = -1.0f;
		
		bool m_disableExternalTransitions		= false;
		bool m_disableBlendLimiter = false;

		NdSubsystemHandle m_hLocomotionInterface = nullptr;

		using SubsystemSpawnInfo::SubsystemSpawnInfo;
	};
};

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterLocomotionInterface : public NdSubsystem
{
	typedef NdSubsystem ParentClass;

public:
	virtual Err Init(const SubsystemSpawnInfo& info) override;

	virtual void GetInput(ICharacterLocomotion::InputData* pData) = 0;
	virtual const IMotionPose* GetPose(const MotionMatchingSet* pArtItemSet, bool debug) = 0;

	virtual bool HasCustomModelStep() const { return false; }
	virtual bool DoCustomModelStep(MotionModel& model,
								   const MotionModelInput& input,
								   float deltaTime,
								   Locator* pAlignOut,
								   float* pClampFactorOut,
								   bool debug) const
	{
		return false;
	}

	virtual void AdjustStickStep(Point& pos, Vector_arg vel, float deltaTime) const {}

	virtual bool GetExtraSample(AnimSampleBiased& extraSample) const { return false; }
	virtual void Update(const Character* pChar, MotionModel& modelPs) {}
	virtual void PostAnimBlending(NdGameObject* pGo, MotionModel& modelPs) {}
	virtual void InstaceCreate(AnimStateInstance* pInst) {}

	virtual void DebugDraw(const Character* pChar, const MotionModel& modelPs) const {}

	virtual Locator ApplyProceduralMotionPs(const AnimStateInstance* pInst,
											const Locator& locPs,
											const StringId64 setId,
											const MotionMatchingSet* pSet) const
	{
		return locPs;
	}

	virtual float LimitProceduralLookAheadTime(float futureTime, const AnimTrajectory& trajectoryOs) const
	{
		return futureTime;
	}

	virtual void FillFootPlantParams(const AnimStateInstance* pInstance, FootPlantParams* pParamsOut) const override {}
};
