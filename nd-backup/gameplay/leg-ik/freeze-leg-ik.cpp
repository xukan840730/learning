/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#include "gamelib/gameplay/leg-ik/freeze-leg-ik.h"

#include "gamelib/gameplay/character-leg-raycaster.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/leg-ik/character-leg-ik-controller.h"
#include "gamelib/gameplay/leg-ik/leg-ik.h"
#include "ndlib/anim/footik.h"
#include "ndlib/anim/ik/ik-defs.h"

//---------------------------------------------------------------------------------------
// Freeze Leg IK Implementation
//---------------------------------------------------------------------------------------

void FreezeLegIk::Start(Character* pCharacter)
{
	ILegIk::Start(pCharacter);

	m_desiredPosLocked = false;
	m_targetPosLocked = false;
}

float FreezeLegIk::GetBlend(CharacterLegIkController* pController)
{
	return m_blend;
}

void FreezeLegIk::Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision)
{
	PROFILE(Processes, FreezeLegIk_Update);

	const Locator alignWs = pCharacter->GetLocator();

	bool raycasterValid = true;

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		raycasterValid = raycasterValid && pController->m_legRaycaster.GetProbeResults(iLeg).m_valid;
	}

	const bool leftValid = pController->m_legRaycaster.GetProbeResults(kLeftLeg).m_valid;
	const bool rightValid = pController->m_legRaycaster.GetProbeResults(kRightLeg).m_valid;

	if (!m_targetPosLocked && pController->m_lastFrameResultValid)
	{
		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			m_lockedPosOs[iLeg] = pController->m_lastFrameResultAnklesOs[iLeg];
		}
		m_targetPosLocked = true;
	}

	if (m_targetPosLocked)
	{
		const float blend = GetBlend(pController);

		Locator aTargetWs[kQuadLegCount];

		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			aTargetWs[iLeg] = alignWs.TransformLocator(m_lockedPosOs[iLeg]);
			aTargetWs[iLeg] = Lerp(m_legIks[iLeg]->GetAnkleLocWs(), aTargetWs[iLeg], blend);
		}

		DoLegIk(pCharacter, pController, aTargetWs, m_legCount, nullptr, Vector(kZero));
	}	
	else if (raycasterValid)
	{
		MoveLegIk::Update(pCharacter, pController, doCollision);
		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			m_lockedPosOs[iLeg] = alignWs.UntransformLocator(m_legIks[iLeg]->GetAnkleLocWs());
		}
		m_targetPosLocked = true;
		return;
	}

	
}

void FreezeLegIk::SingleFrameUnfreeze()
{
	m_targetPosLocked = false;
}

bool FreezeLegIk::GetMeshRaycastPointsWs(Character* pCharacter, Point* pLeftLegWs, Point* pRightLegWs, Point* pFrontLeftLegWs, Point* pFrontRightLegWs)
{
	if (m_targetPosLocked)
	{
		Point* apLegWs[kQuadLegCount] = { pLeftLegWs, pRightLegWs, pFrontLeftLegWs, pFrontRightLegWs };
		const Locator alignWs = pCharacter->GetLocator();
		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			ANIM_ASSERT(apLegWs[iLeg]);
			*apLegWs[iLeg] = alignWs.TransformPoint(m_lockedPosOs[iLeg].GetTranslation());
		}
		return true;
	}
	return false;
}
