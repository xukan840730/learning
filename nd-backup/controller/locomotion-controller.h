/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/controller/nd-locomotion-controller.h"
#include "gamelib/gameplay/character-locomotion.h"

#include "game/ai/npc-anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NavCharacter;
namespace DC
{
	struct NpcMoveSetDef;
	struct NpcMoveSetContainer;
	struct NpcMotionTypeMoveSets;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class IAiLocomotionController : public INdAiLocomotionController
{
public:
	enum WeaponUpDownState
	{
		kNoWeapon,
		kWeaponUp,
		kWeaponDown
	};

	static const char* GetWeaponUpDownStateName(const WeaponUpDownState wuds)
	{
		switch (wuds)
		{
		case kNoWeapon:		return "Holstered";
		case kWeaponUp:		return "WeaponUp";
		case kWeaponDown:	return "WeaponDown";
		}

		return "???";
	}

	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override = 0;

	virtual void PostRootLocatorUpdate() override = 0;
	virtual float GetMovementSpeedScale() const override = 0;

	virtual float PathRemainingDist() const = 0;

	virtual bool AddMovePerformanceTable(StringId64 entryId, StringId64 tableId) = 0;
	virtual bool RemoveMovePerformanceTable(StringId64 entryId) = 0;
	virtual bool HasMovePerformanceTable(StringId64 entryId) const = 0;

	virtual Vector GetMotionDirPs() const = 0;

	virtual WeaponUpDownState GetDesiredWeaponUpDownState(bool apCheck) const = 0;
	virtual bool ConfigureWeaponUpDownOverlays(WeaponUpDownState desState) = 0;

	virtual void DebugOnly_ForceMovePerfCacheRefresh() = 0;

	virtual void PatchWaypointFacingDir(Vector_arg newFacingDir) {}
	virtual void SuppressRetargetScale() = 0;

	static bool IsKnownIdleState(StringId64 stateId);

	virtual bool IsWeaponStateUpToDate() const = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AiLocomotionInterface : public CharacterLocomotionInterface
{
	typedef CharacterLocomotionInterface ParentClass;
	SUBSYSTEM_BIND(NavCharacter);

public:
	virtual Err Init(const SubsystemSpawnInfo& info) override;

	virtual void Update(const Character* pChar, MotionModel& modelPs) override;

	virtual void GetInput(ICharacterLocomotion::InputData* pData) override;
	virtual const IMotionPose* GetPose(const MotionMatchingSet* pArtItemSet, bool debug) override;

	virtual float LimitProceduralLookAheadTime(float futureTime, const AnimTrajectory& trajectoryOs) const override;

	void ResetSpeedPercentHighWater() { m_speedPercentHighWater = 0.0f; }
	float GetSpeedPercentHighWater() const { return m_speedPercentHighWater; }

	void SetModelPosPs(Point_arg posPs) { m_modelPosPs = posPs; }
	Point GetModelPosPs() const { return m_modelPosPs; }

private:
	enum class StairsStatus
	{
		kNone,
		kEntering,
		kOnStairs,
		kExiting,
		kMax
	};

	static const char* GetStairsStatusStr(StairsStatus s)
	{
		switch (s)
		{
		case StairsStatus::kNone:		return "None";
		case StairsStatus::kEntering:	return "Entering";
		case StairsStatus::kOnStairs:	return "On Stairs";
		case StairsStatus::kExiting:	return "Exiting";
		}

		return "???";
	}

	const IAiLocomotionController* GetSelf() const;
	IAiLocomotionController* GetSelf();

	static MotionModelInput Mm_StickFuncHorse(const Character* pChar,
											  const ICharacterLocomotion::InputData* pInput,
											  const MotionMatchingSet* pArtItemSet,
											  const DC::MotionMatchingSettings* pSettings,
											  const MotionModelIntegrator& integratorPs,
											  TimeFrame delta,
											  bool finalize,
											  bool debug);

	static I32F Mm_GroupFunc(const Character* pChar,
							 const MotionMatchingSet* pArtItemSet,
							 const DC::MotionMatchingSettings* pSettings,
							 const MotionModelIntegrator& integratorPs,
							 Point_arg trajectoryEndPosOs,
							 bool debug);

	static MotionModelPathInput Mm_PathFunc(const Character* pChar, const MotionMatchingSet* pArtItemSet, bool debug);

	static Vector GetEffectiveGroundNormalPs(const NavCharacter* pNavChar,
											 bool stairsAllowed,
											 StairsStatus& stairsStatus);

	Point m_modelPosPs;
	float m_speedPercentHighWater;
	float m_stairUpDownFactor;

	StairsStatus m_stairsStatus;
	Vector m_desGroundNormalPs;
	Vector m_smoothedGroundNormalPs;

	SpringTracker<Vector> m_normalTrackerPs;
};

typedef TypedSubsystemHandle<AiLocomotionInterface> AiLocomotionIntHandle;

/// --------------------------------------------------------------------------------------------------------------- ///
IAiLocomotionController* CreateAiDefaultLocomotionController();
