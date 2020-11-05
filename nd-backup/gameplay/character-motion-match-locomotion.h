/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/ik/joint-chain.h"

#include "gamelib/anim/motion-matching/motion-matching.h"
#include "gamelib/anim/motion-matching/motion-model.h"
#include "gamelib/anim/motion-matching/motion-pose.h"
#include "gamelib/gameplay/character-locomotion.h"
#include "gamelib/scriptx/h/motion-matching-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionMatchIkAdjustments;
class MotionMatchingSet;

/// --------------------------------------------------------------------------------------------------------------- ///
class MMSetPtr
{
public:
	MMSetPtr() = default;
	MMSetPtr(StringId64 id) : m_ptr(id, SID("motion-matching")) {}
	MMSetPtr(const MMSetPtr& other) = default;

	StringId64 GetId() const { return m_ptr.GetId(); }
	const DC::MotionMatchingSettings* GetSettings() const;
	U32F GetNumTrajectorySamples() const;
	U32F GetNumPrevTrajectorySamples() const;
	float GetMaxTrajectoryTime() const;
	float GetMaxPrevTrajectoryTime() const;
	float GetStoppingFaceDist() const;
	StringId64 MotionMatchSetArtItemId() const;
	bool IsValid() const;

	const MotionMatchingSet* GetSetArtItem() const;

private:
	const DC::MotionMatchingSet* GetSet() const;

	ScriptPointer<void*> m_ptr;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionSettingsBlendQueue
{
public:
	bool Valid() const { return (m_queue.GetCount() > 0); }

	DC::MotionModelSettings Get(TimeFrame time) const;
	void Update(TimeFrame curTime);
	void Push(const MMSetPtr& set, TimeFrame blendTime, const Clock* pClock);
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

private:
	void CreateDirectionalSettings(DC::MotionModelSettings& settings) const;
	DC::MotionModelSettings GetFromSet(const MMSetPtr& set) const;

	struct SettingsInst
	{
		TimeFrame m_startTime;
		TimeFrame m_blendTime;
		MMSetPtr m_set;
	};
	typedef StaticRingQueue<SettingsInst, 5> SettingsQueue;
	SettingsQueue m_queue;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterMotionMatchLocomotion : public ICharacterLocomotion
{
public:
	using RecordedTraj = FixedArray<AnimTrajectorySample, 16>;

	struct RecordedState
	{
		InputData m_input;
		TimeFrame m_previousMatchTime;
		MMSetPtr m_set;
		Maybe<AnimSample> m_maybeCurSample;
		Locator m_charLocatorPs;
		Clock m_clock;
		RecordedTraj m_traj;
		RecordedMotionPose m_pose;
		I32 m_desiredGroupId;

		AnimChangeMode m_animChangeMode;

		bool m_allowExternalTransitions;
		bool m_valid = false;

		void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
		{
			m_pose.Relocate(deltaPos, lowerBound, upperBound);
		}
	};

	virtual Err Init(const SubsystemSpawnInfo& info) override;
	virtual void EnterNewParentSpace(const Transform& matOldToNew,
									 const Locator& oldParentSpace,
									 const Locator& newParentSpace) override;
	virtual void OnKilled() override;

	SUBSYSTEM_UPDATE(Update);
	SUBSYSTEM_UPDATE(PostRootLocatorUpdate);
	SUBSYSTEM_UPDATE(PostAnimBlending);

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void EventHandler(Event& event) override;

	virtual bool GetPlayerMoveControl(float* pMoveCtrlPct) const override;
	virtual float GetNavAdjustRadius(float radius) const override;

	virtual void RequestRefreshAnimState(const FadeToStateParams* pFadeToStateParams = nullptr,
										 bool allowStompOfInitialBlend = true) override;

	void ChangeInterface(StringId64 newInterfaceTypeId);
	void ChangeInterface(CharacterLocomotionInterface* pNewInterface);
	bool TryChangeSetId(StringId64 newSetId);
	void ResetCustomModelData() { m_motionModelPs.ResetCustomData(); }
	void ResetMotionModel();

	const MotionModel& GetMotionModelPs() const { return m_motionModelPs; }
	MotionModel& GetMotionModelPs() { return m_motionModelPs; }

	const DC::MotionMatchingSettings* GetSettings() const;
	StringId64 GetSetId() const;
	DC::MotionModelSettings GetMotionSettings() const;
	const Quat GetAlignProceduralRotationDeltaPs() const { return m_proceduralRotationDeltaPs; }
	const Point ApproximateStoppingPositionPs() const;

	void DebugUpdate(const RecordedState*) const;

	virtual Color GetQuickDebugColor() const override;
	virtual bool GetQuickDebugText(IStringBuilder* pText) const override;
	virtual bool GetAnimControlDebugText(const AnimStateInstance* pInstance, IStringBuilder* pText) const override;
	Quat AdjustRotationToUprightPs(Quat_arg rotPs) const;

	bool HasValidMotionSettings() const { return m_blendedSettings.Valid(); }

	Locator ApplyProceduralTranslationPs(const Locator& locPs) const;

	bool IsCharInIdle() const;

	CharacterLocomotionInterface* GetInterface() const
	{
		return m_hLocomotionInterface.ToSubsystem<CharacterLocomotionInterface>(SID("CharacterLocomotionInterface"));
	}

	template <typename T>
	T* GetInterface(StringId64 typeSid) const
	{
		return m_hLocomotionInterface.ToSubsystem<T>(typeSid);
	}

	virtual void FillFootPlantParams(const AnimStateInstance* pInstance, FootPlantParams* pParamsOut) const override;
	virtual float GetGroundAdjustFactor(const AnimStateInstance* pInstance, float desired) const override;
	virtual float GetNoAdjustToNavFactor(const AnimStateInstance* pInstance, float desired) const override;
	virtual float GetLegIkEnabledFactor(const AnimStateInstance* pInstance, float desired) const override;

	bool GetFutureFacingOs(float futureTimeSec, Vector* pFacingOsOut) const;
	bool GetFuturePosWs(float futureTimeSecs, bool clampEnd, Point& outFuturePosWs) const;

	float GetRetargetScale() const { return m_retargetScale; }

	Maybe<AnimSample> GetCurrentAnimSample() const;

private:
	using ParentClass	= ICharacterLocomotion;
	using IkHandle		= TypedSubsystemHandle<MotionMatchIkAdjustments>;
	using TransitionPtr = ScriptPointer<DC::MotionMatchTransitionTable>;

	bool IsValidToUpdate() const;
	void RequestAnims();

	struct MatchParams
	{
		MatchParams(const InputData& input)
			: m_input(input)
		{
		}

		const InputData& m_input;
		Locator m_parentSpace;

		TimeFrame m_transitionalInterval = Seconds(0.0f);
		TimeFrame m_previousMatchTime;
		MMSetPtr m_set;
		const IMotionPose* m_pCharPose = nullptr;

		Maybe<AnimSample> m_maybeCurSample;
		Locator m_charLocatorPs;
		const Clock* m_pClock;
		const Character* m_pSelf;

		const AnimTrajectory* m_pDesiredTrajectory	= nullptr;

		I32 m_desiredGroupId	  = 0;
		MMSearchParams m_searchParams;

		AnimSampleBiased m_extraBias;

		AnimChangeMode m_animChangeMode;

		bool m_allowExternalTransitionAnims = false;
		bool m_forceExternalPose = false;
		bool m_debug = false;
	};

	void CreateTrajectory(AnimTrajectory* pTrajectory,
						  const InputData& input,
						  const Locator& matchLocPs,
						  bool debug) const;

	MatchParams CreateMatchParams(const InputData& input, float dt, AnimTrajectory* pTrajectoryOut, bool debug) const;
	static Maybe<AnimSample> MatchFromInput(const MatchParams& params);

	void CalculateProceduralMotion(NdGameObject& self);
	void CalculateProceduralRotation_Moving(const NdGameObject& self,
											const Locator& animBasePs,
											const Locator& modelBasePs,
											const MMLocomotionState& futureAnimStateOs,
											const AnimTrajectorySample& futureModelStateOs);
	void CalculateProceduralRotation_Standing(const NdGameObject& self,
											  const Locator& animBasePs,
											  const Locator& modelBasePs,
											  const MMLocomotionState& futureAnimStateOs,
											  const AnimTrajectorySample& futureModelStateOs);
	void CalculateProceduralRotation_Strafing(const NdGameObject& self,
											  const Locator& animBasePs,
											  const Locator& modelBasePs,
											  const MMLocomotionState& futureAnimStateOs,
											  const AnimTrajectorySample& futureModelStateOs);

	bool ApplyBestSample(NdGameObject& self,
						 const AnimSample& bestSample,
						 const DC::MotionMatchingSettings* pSettings,
						 AnimChangeMode mode);
	void UpdateAndApplyRootRotation(NdGameObject& self);
	Locator GetMatchLocatorPs(const NdGameObject& self) const;
	bool AreExternalTransitionAnimsAllowed() const;

	float GetProceduralLookAheadTime(const Maybe<AnimSample>& sample) const;
	float GetStrafeLockAngleDeg() const;

	NdSubsystemHandle m_hLocomotionInterface;

	MotionModel m_motionModelPs;
	MMSetPtr m_set;
	TransitionPtr m_transitions;
	StringId64 m_prevSetId = INVALID_STRING_ID_64;
	StringId64 m_playerMoveMode = INVALID_STRING_ID_64;
	TimeFrame m_previousMatchTime;

	FadeToStateParams m_refreshFadeToStateParams;

	bool m_requestRefreshAnimState = false;
	bool m_hasRefreshParams		   = false;
	bool m_disableExternalTransitions = false;
	bool m_hackUpdateMotionModel	  = false;
	bool m_hasUpdated = false;

	InputData m_input;
	Locator m_lastMatchLocPs;
	Quat m_proceduralRotationDeltaPs = kIdentity;
	Quat m_strafeRotationDeltaPs	 = kIdentity;
	Vector m_proceduralTransDeltaPs	 = kZero;

	Quat m_matchRotationPs = kIdentity;
	Quat m_prevRotationPs  = kIdentity;

	float m_retargetScale	 = 1.0f;
	float m_strafeBlend		 = -1.0f;
	float m_speedScale		 = 1.0f;
	float m_timeScale		 = 1.0f;
	float m_initialBlendTime = -1.0f;
	float m_initialMotionBlendTime = -1.0f;
	bool m_disableBlendLimiter = false;

	SpringTracker<float> m_speedScaleSpring;

	IkHandle m_hIk;

	// Special case for blending out of igcs
	DC::AnimCurveType m_initialBlendCurve = DC::kAnimCurveTypeUniformS;

	MotionSettingsBlendQueue m_blendedSettings;

	LocomotionHistory m_locomotionHistory;

	Maybe<Vector> m_smoothedUserFacingPs;
	Maybe<Vector> m_facingBiasPs;

	AnimTrajectory m_animTrajectoryOs;

	friend class CharacterMotionMatchLocomotionAction;
};

typedef TypedSubsystemHandle<CharacterMotionMatchLocomotion> CharacterMmLocomotionHandle;

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterMotionMatchLocomotionAction : public NdSubsystemAnimAction
{
public:
	virtual Err Init(const SubsystemSpawnInfo& info) override;
	virtual void InstanceCreate(AnimStateInstance* pInst) override;
	virtual bool InstanceAlignFunc(const AnimStateInstance* pInst,
								   const BoundFrame& prevAlign,
								   const BoundFrame& currAlign,
								   const Locator& apAlignDelta,
								   BoundFrame* pAlignOut,
								   bool debugDraw) override;

	StringId64 GetSetId() const;
	const DC::MotionMatchingSettings* GetSettings() const;

	StringId64 GetPlayerMoveMode() { return m_playerMoveMode; }

	virtual bool GetAnimControlDebugText(const AnimStateInstance* pInstance, IStringBuilder* pText) const override;

private:
	using ParentClass = NdSubsystemAnimAction;

	Locator ApplyProceduralMotionPs(const AnimStateInstance* pInst,
									const Maybe<AnimSample>& sample,
									const BoundFrame& animDesiredLoc,
									const StringId64 setId,
									const MotionMatchingSet* pSet,
									bool debugDraw) const;

	TypedSubsystemHandle<CharacterMotionMatchLocomotion> m_hCharacterLocomotion;
	MMSetPtr m_set;
	StringId64 m_playerMoveMode = INVALID_STRING_ID_64;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionMatchIkAdjustments : public NdSubsystem
{
public:
	virtual Err Init(const SubsystemSpawnInfo& info) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void RelocateOwner(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	void DoIk(Quat_arg ikRotDesired, float blend);

private:
	using ParentClass = NdSubsystem;
	struct IkData
	{
		Quat m_ikRotation;
		SpringTrackerQuat m_ikRotationSpring;
		JointChain m_joints;
		JacobianMap m_jacobian;
		int m_chestJointOffset;
		int m_headOffset;

		IkData();

		void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	};

	IkData m_ikData;
};
