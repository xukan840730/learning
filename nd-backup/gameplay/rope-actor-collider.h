/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/process-phys-rope.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class RopeActorCollider
{
public:
	RopeActorCollider()
		: m_pColliders(nullptr)
		, m_numColliders(0)
		, m_bActive(false)
		, m_bTeleport(false)
	{
	}

	Err Init(StringId64 artGroupId);
	void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);

	void SetActor(const NdGameObject* pGo, Rope2* pRope);
	void Update(Rope2* pRope);
	void Disable(Rope2* pRope);
	void SetColliderEnabled(StringId64 jointId, bool enabled);

	void SetTeleport() { m_bTeleport = true; }

protected:
	ArtItemCollisionHandle m_hColl;
	ProcessRopeCollider* m_pColliders;
	U32 m_numColliders;
	NdGameObjectHandle m_hActor;
	bool m_bActive;
	bool m_bTeleport;
	
};
