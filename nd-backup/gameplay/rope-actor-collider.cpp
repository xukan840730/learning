/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/rope-actor-collider.h"

#include "gamelib/level/art-item-collision.h"

#include <Physics/Physics/Dynamics/Body/hknpBodyCinfo.h>

Err RopeActorCollider::Init(StringId64 artGroupId)
{
	m_hColl = ResourceTable::LookupCollision(artGroupId);
	const ArtItemCollision* pColl = m_hColl.ToArtItem();
	if (!pColl)
	{
		MsgErr("Rope collider actor %s not loaded\n", DevKitOnly_StringIdToString(artGroupId));
		return Err::kErrGeneral;
	}

	m_numColliders = pColl->m_numBodies;
	m_pColliders = NDI_NEW ProcessRopeCollider[m_numColliders];
	for (U32 ii = 0; ii<m_numColliders; ii++)
	{
		m_pColliders[ii].Init(pColl->m_ppBodies[ii]->m_pBodyCinfo->m_shape, pColl->m_ppBodies[ii]->m_body2JointLoc);
	}

	return Err::kOK;
}

void RopeActorCollider::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pColliders, delta, lowerBound, upperBound);
}

void RopeActorCollider::Disable(Rope2* pRope)
{
	if (m_bActive)
	{
		for (U32 ii = 0; ii<m_numColliders; ii++)
		{
			pRope->RemoveCustomCollider(&m_pColliders[ii].m_collider);
		}
		m_bActive = false;
	}
	m_hActor = nullptr;
}

void RopeActorCollider::SetActor(const NdGameObject* pGo, Rope2* pRope)
{
	if (m_hActor == NdGameObjectHandle(pGo) && m_bActive)
	{
		return;
	}

	const ArtItemCollision* pColl = m_hColl.ToArtItem();
	if (!pColl)
	{
		MsgErr("Rope collider actor has been unloaded while in use\n");
		Disable(pRope);
		return;
	}

	const FgAnimData* pAnimData = pGo->GetAnimData();
	for (U32 ii = 0; ii<m_numColliders; ii++)
	{
		m_pColliders[ii].Reset(pAnimData, pColl->m_ppBodies[ii]->m_jointId);
	}
	m_hActor = pGo;
	m_bTeleport = false;

	if (!m_bActive)
	{
		for (U32 ii = 0; ii<m_numColliders; ii++)
		{
			pRope->AddCustomCollider(&m_pColliders[ii].m_collider);
		}
		m_bActive = true;
	}
}

void RopeActorCollider::SetColliderEnabled(StringId64 jointId, bool enabled)
{
	const NdGameObject* pGo = m_hActor.ToProcess();
	if (!pGo)
	{
		return;
	}
	const FgAnimData* pAnimData = pGo->GetAnimData();
	I32 joint = pAnimData->FindJoint(jointId);
	if (joint > 0)
	{
		for (U32 ii = 0; ii < m_numColliders; ii++)
		{
			if (m_pColliders[ii].m_joint == joint)
			{
				m_pColliders[ii].m_collider.m_enabled = enabled;
			}
		}
	}
}

void RopeActorCollider::Update(Rope2* pRope)
{
	if (!m_bActive)
		return;

	const NdGameObject* pGo = m_hActor.ToProcess();
	if (!pGo)
	{
		Disable(pRope);
		return;
	}

	if (!m_hColl.ToArtItem())
	{
		MsgErr("Rope collider actor has been unloaded while in use\n");
		Disable(pRope);
		return;
	}

	const FgAnimData* pAnimData = pGo->GetAnimData();
	for (U32 ii = 0; ii<m_numColliders; ii++)
	{
		m_pColliders[ii].Update(pAnimData, m_bTeleport);
	}

	m_bTeleport = false;
}
