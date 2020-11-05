/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/util/maybe.h"

#include "gamelib/gameplay/character-locomotion.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class Player;
struct PlayerMoveAction;

/// --------------------------------------------------------------------------------------------------------------- ///
class IPlayerMotionMatchInput
{
public:
	virtual ~IPlayerMotionMatchInput() {}

	struct Input
	{
		Vector m_velDirPs = kZero;
		Maybe<Vector> m_desiredFacingPs;
		StringId64 m_setId = INVALID_STRING_ID_64;
		F32 m_scale		   = 1.0f;
	};

	virtual Input GetInput(Player&) const = 0;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound){};
};

/// --------------------------------------------------------------------------------------------------------------- ///
PlayerMoveAction* CreateMotionMatchAction(Player& player,
										float blendInTime = -1.0f,
										float motionBlendInTime = -1.0f,
										  DC::AnimCurveType blendInCurve = DC::kAnimCurveTypeInvalid,
										  bool isBlendingOutOfIgc = false,
										bool forceBlendTime = false,
										  IPlayerMotionMatchInput* pInput = nullptr);

/// --------------------------------------------------------------------------------------------------------------- ///
class PlayerLocomotionInterface : public CharacterLocomotionInterface
{
	typedef CharacterLocomotionInterface ParentClass;
	SUBSYSTEM_BIND(Player);

private:
	StringId64	m_startingAnimId = INVALID_STRING_ID_64;
	bool		m_maintainStartingAnim = false;
	bool		m_requestedAbortMaintainStartingAnim = false;
	bool		m_forceStartingAnim = false;
	Vector		m_startingAnimDirLs = kZero;
	TimeFrame	m_startTime;

public:
	virtual Err Init(const SubsystemSpawnInfo& info) override;

	SUBSYSTEM_UPDATE(Update);

	virtual void GetInput(ICharacterLocomotion::InputData* pData) override;
	virtual const IMotionPose* GetPose(const MotionMatchingSet* pArtItemSet, bool debug) override;
	virtual void InstaceCreate(AnimStateInstance* pInst) override;

	virtual void AdjustStickStep(Point& pos, Vector_arg vel, float deltaTime) const override;

private:
	void RequestMaintainStartingAnim(AnimStateInstance* pInst);

	static float GetStoppingFaceDist(const MotionMatchingSet* pArtItemSet);

	static MotionModelInput PlayerCoverStickFunc(const Character* pChar,
												 const ICharacterLocomotion::InputData* pInput,
												 const MotionMatchingSet* pArtItemSet,
												 const DC::MotionMatchingSettings* pSettings,
												 const MotionModelIntegrator& integratorPs,
												 TimeFrame delta,
												 bool finalize,
												 bool debug);

	static MotionModelInput PlayerDoorStickFunc(const Character* pChar,
												const ICharacterLocomotion::InputData* pInput,
												const MotionMatchingSet* pArtItemSet,
												const DC::MotionMatchingSettings* pSettings,
												const MotionModelIntegrator& integratorPs,
												TimeFrame delta,
												bool finalize,
												bool debug);

	static MotionModelInput PlayerWalkBesidesStickFunc(const Character* pChar,
													   const ICharacterLocomotion::InputData* pInput,
													   const MotionMatchingSet* pArtItemSet,
													   const DC::MotionMatchingSettings* pSettings,
													   const MotionModelIntegrator& integratorPs,
													   TimeFrame delta,
													   bool finalize,
													   bool debug);

	static MotionModelInput PlayerScriptMoveStickFunc(const Character* pChar,
													  const ICharacterLocomotion::InputData* pInput,
													  const MotionMatchingSet* pArtItemSet,
													  const DC::MotionMatchingSettings* pSettings,
													  const MotionModelIntegrator& integratorPs,
													  TimeFrame delta,
													  bool finalize,
													  bool debug);

	static MotionModelInput PlayerBalanceStickFunc(const Character* pChar,
												 const ICharacterLocomotion::InputData* pInput,
												 const MotionMatchingSet* pArtItemSet,
												 const DC::MotionMatchingSettings* pSettings,
												 const MotionModelIntegrator& integratorPs,
												 TimeFrame delta,
												 bool finalize,
												 bool debug);

	static MotionModelInput PlayerWallShimmyStickFunc(const Character* pChar,
													  const ICharacterLocomotion::InputData* pInput,
													  const MotionMatchingSet* pArtItemSet,
													  const DC::MotionMatchingSettings* pSettings,
													  const MotionModelIntegrator& integratorPs,
													  TimeFrame delta,
													  bool finalize,
													  bool debug);

	static MotionModelInput PlayerEdgeSlipStickFunc(const Character* pChar,
													const ICharacterLocomotion::InputData* pInput,
													const MotionMatchingSet* pArtItemSet,
													const DC::MotionMatchingSettings* pSettings,
													const MotionModelIntegrator& integratorPs,
													TimeFrame delta,
													bool finalize,
													bool debug);

	static MotionModelInput PlayerStrafingStickFunc(const Character* pChar,
													const MotionMatchingSet* pArtItemSet,
													const DC::MotionMatchingSettings* pSettings,
													const MotionModelIntegrator& integratorPs,
													TimeFrame delta,
													bool finalize,
													bool debug);

	static MotionModelInput PlayerMoveStickFunc(const Character* pChar,
		const ICharacterLocomotion::InputData* pInput,
		const MotionMatchingSet* pArtItemSet,
		const DC::MotionMatchingSettings* pSettings,
		const MotionModelIntegrator& integratorPs,
		TimeFrame delta,
		bool finalize,
		bool debug);

	static I32F PlayerMotionMatchGroupFunc(const Character* pChar,
										   const MotionMatchingSet* pArtItemSet,
										   const DC::MotionMatchingSettings* pSettings,
										   const MotionModelIntegrator& integratorPs,
										   Point_arg trajectoryEndPosOs,
										   bool debug);

	static MotionModelPathInput PlayerMotionMatchPathFunc(const Character* pChar,
														  const MotionMatchingSet* pArtItemSet,
														  bool debug);

	static void PlayerWallShimmyLayerFunc(const Character* pChar,
										  MMSearchParams& params,
										  bool debug);

	PlayerHandle m_hPlayer;
};