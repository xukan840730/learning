/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "game/ai/controller/flock-controller.h"

#include "gamelib/anim/motion-matching/pose-tracker.h"
#include "gamelib/gameplay/character-locomotion.h"
#include "gamelib/gameplay/flocking/flocking-agent.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AiMmFlockInterface : public CharacterLocomotionInterface
{
public:
	virtual void Update(const Character* pChar, MotionModel& modelPs) override
	{
	}

	virtual void GetInput(ICharacterLocomotion::InputData* pData) override;
	virtual const IMotionPose* GetPose(const MotionMatchingSet* pArtItemSet, bool debug) override
	{
		const Character* pChar = GetOwnerCharacter();

		const IMotionPose* pPose = nullptr;

		if (const PoseTracker* pPoseTracker = pChar->GetPoseTracker())
		{
			pPose = &pPoseTracker->GetPose();
		}

		return pPose;
	}

	void SetAgentVelDirPs(Vector_arg dirPs)
	{
		ANIM_ASSERT(IsReasonable(dirPs));
		m_agentVelDirPs = dirPs;
	}

private:
	Vector m_agentVelDirPs;
};

TYPE_FACTORY_REGISTER(AiMmFlockInterface, CharacterLocomotionInterface);

/// --------------------------------------------------------------------------------------------------------------- ///
static AiMmFlockInterface* GetFlockMmInterface(const CharacterMmLocomotionHandle& hController)
{
	AiMmFlockInterface* pInterface = nullptr;

	if (const CharacterMotionMatchLocomotion *const pController = hController.ToSubsystem())
	{
		pInterface = pController->GetInterface<AiMmFlockInterface>(SID("AiMmFlockInterface"));
	}

	return pInterface;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiFlockController::RequestAnimations()
{
	if (AiMmFlockInterface *const pInterface = GetFlockMmInterface(m_hMmController))
	{
		const Vector agentVelDirPs = GetAgentVelDirPsFromSimulation();
		pInterface->SetAgentVelDirPs(agentVelDirPs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiFlockController::IsBusy() const
{
	return m_hMmController.Assigned();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiFlockController::ShouldInterruptNavigation() const
{
	return IsBusy();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiFlockController::StartFlocking()
{
	NdGameObject *const pNavCharacter = GetCharacter();

	CharacterMotionMatchLocomotion::SpawnInfo spawnInfo(SID("CharacterMotionMatchLocomotion"), pNavCharacter);
	spawnInfo.m_locomotionInterfaceType = SID("AiMmFlockInterface");

	NdSubsystem *const pMmSubSys = NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap, spawnInfo, FILE_LINE_FUNC);
	CharacterMotionMatchLocomotion *const pController = (CharacterMotionMatchLocomotion*)pMmSubSys;

	if (pController)
	{
		m_hMmController = pController;
	}
	else
	{
		m_hMmController = CharacterMmLocomotionHandle();
	}

	if (AiMmFlockInterface *const pInterface = GetFlockMmInterface(m_hMmController))
	{
		const Vector agentVelDirPs = GetAgentVelDirPsFromSimulation();
		pInterface->SetAgentVelDirPs(agentVelDirPs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiFlockController::StopFlocking()
{
	if (CharacterMotionMatchLocomotion *const pMmController = m_hMmController.ToSubsystem())
	{
		pMmController->Kill();
	}

	m_hMmController = CharacterMmLocomotionHandle();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector AiFlockController::GetAgentVelDirPsFromSimulation() const
{
	NavCharacter *const pNavCharacter = GetCharacter();
	ANIM_ASSERT(pNavCharacter);

	const Flocking::FlockingAgent *const pFlockingAgent = pNavCharacter->GetFlockingAgent();
	ANIM_ASSERT(pFlockingAgent);

	const Vector flockingAgentVelWs = pFlockingAgent->GetVelocity();
	const Vector flockingAgentVelPs = pNavCharacter->GetParentSpace().UntransformVector(flockingAgentVelWs);
	const Vector flockingAgentVelPsXz = AsUnitVectorXz(flockingAgentVelPs, Vector(kZero));

	return flockingAgentVelPsXz;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiMmFlockInterface::GetInput(ICharacterLocomotion::InputData* pData)
{
	pData->m_setId = SID("*sheep-mm-dog-trot*");
	pData->m_desiredVelocityDirPs = m_agentVelDirPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiFlockController* CreateFlockController()
{
	return NDI_NEW AiFlockController;
}
