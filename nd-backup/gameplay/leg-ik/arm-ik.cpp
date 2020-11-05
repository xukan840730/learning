/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/
#include "gamelib/gameplay/leg-ik/arm-ik.h"
#include "gamelib/gameplay/leg-ik/character-leg-ik-controller.h"
#include "ndlib/anim/armik.h"
#include "ndlib/anim/ik/ik-defs.h"

//---------------------------------------------------------------------------------------
// Base Leg IK Implementation
//---------------------------------------------------------------------------------------

IArmIk::IArmIk()
{
	m_blend = 0.0f;
}

void IArmIk::InitArmIk(Character* pCharacter, ArmIkChain *pArmIks)
{
	m_armIks[kLeftArm] = &pArmIks[kLeftArm];
	m_armIks[kRightArm] = &pArmIks[kRightArm];
}

void IArmIk::Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_armIks[kLeftArm], offset_bytes, lowerBound, upperBound);
	RelocatePointer(m_armIks[kRightArm], offset_bytes, lowerBound, upperBound);
}

void IArmIk::Start(Character* pCharacter)
{
	m_handBlend[kLeftArm] = 1.0f;
	m_handBlend[kRightArm] = 1.0f;
	m_blend = 0.0f;
}

void IArmIk::SetBlend(float blend)
{
	m_blend = blend;
}

void IArmIk::BlendIn(float blendSpeed)
{
	Seek(m_blend, 1.0f, FromUPS(blendSpeed));
}

void IArmIk::BlendOut(float blendSpeed)
{
	Seek(m_blend, 0.0f, FromUPS(blendSpeed));
}

float IArmIk::GetBlend(CharacterLegIkController* pController)
{
	return m_blend * pController->GetSlopeBlend();
}

void IArmIk::SingleFrameUnfreeze()
{
}

void IArmIk::DoArmIk(Character* pCharacter, CharacterLegIkController* pController, const Locator *pHandLoc, const bool *pHandEvaluated)
{
	PROFILE(Processes, DoArmIk);

	for (int i = 0; i < 2; i++)
	{
		if (pHandEvaluated[i])
		{
			const float blend = m_handBlend[i] * GetBlend(pController);

			ArmIkInstance instance;
			//			DebugDrawSphere(handLoc[i].GetTranslation(), 0.1f, kColorRed);
			instance.m_ikChain = m_armIks[i];
			instance.m_goalPosWs = pHandLoc[i].GetTranslation();
			instance.m_tt = blend;

			SolveArmIk(&instance);

			const Quat postIkHandRot = m_armIks[i]->GetWristLocWs().GetRotation();

			m_armIks[i]->RotateWristWs(Slerp(Quat(kIdentity), pHandLoc[i].GetRotation()*Conjugate(postIkHandRot), blend));
		}
	}
}
