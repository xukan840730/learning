/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef NDPHYS_ROPE2_COLLIDER_H 
#define NDPHYS_ROPE2_COLLIDER_H

#include "ndlib/ndphys/gamephys.h"
#include "ndlib/ndphys/rigid-body-base.h"

#include <Common/Base/hkBase.h>
#include <Physics/Physics/Collide/Shape/hknpShape.h>

class Rope2;

class hkpPlaneColliderShape : public hknpShape
{
public:
	hkpPlaneColliderShape() : hknpShape(hknpShapeType::USER_0, hknpCollisionDispatchType::NONE) {};
};

class RopeCollider
{
public:
	RopeCollider() : m_pShape(nullptr), m_enabled(true) {}
	RopeCollider(const hknpShape* pShape, const Locator& loc)
		: m_pShape(pShape)
		, m_loc(loc)
		, m_locPrev(loc)
		, m_linVel(kZero)
		, m_angVel(kZero)
		, m_enabled(true)
	{
	}

	RopeCollider(const RopeCollider& parent, U32 childIndex);

	void FromRigidBody(const RigidBody* pBody, bool teleport, F32 invDt);
	void FromRigidBodyAndShape(const RigidBody* pBody, const hknpShape* pShape, bool teleport, F32 invDt);

	void SetChildShape(const hknpShape* pShape)
	{
		m_pShape = pShape;
	}

	void ResetLoc(const Locator& loc);
	void UpdateLoc(const Locator& loc, bool teleport, F32 invDt);
	void TeleportTo(const Locator& loc);
	void RestoreLoc(const Locator& loc, const Locator& locPrev, F32 invDt);

	Locator m_loc;
	Locator m_locPrev;
	Vector m_linVel;
	Vector m_angVel;
	const hknpShape* m_pShape;
	bool m_enabled;
};

class RopeColliderHandle
{
public:
	RopeColliderHandle() : m_customIndex(-1), m_listIndex(-1) {}
	RopeColliderHandle(const RigidBodyHandle& hRigidBody, I16 listIndex = -1) : m_customIndex(-1), m_listIndex(listIndex), m_hRigidBody(hRigidBody) {}
	RopeColliderHandle(U32F customIndex) : m_customIndex(customIndex), m_listIndex(-1) {}
	bool operator == (const RopeColliderHandle& hOther) const { return (m_customIndex == hOther.m_customIndex) & (m_hRigidBody == hOther.m_hRigidBody) & (m_listIndex == hOther.m_listIndex); }
	bool operator != (const RopeColliderHandle& hOther) const { return !(*this == hOther); }
	bool IsRigidBody() const { return m_customIndex == -1; }
	bool IsValid() const { return (m_customIndex >= 0) || m_hRigidBody.HandleValid(); }
	const RigidBody* GetRigidBody() const { return m_hRigidBody.HandleValid() ? m_hRigidBody.ToBody() : nullptr; }
	RigidBodyHandle GetRigidBodyHandle() const { return m_hRigidBody; }
	I16 GetListIndex() const { return m_listIndex; }
	I16 GetCustomIndex() const { return m_customIndex; }
	const RopeCollider* GetCollider(RopeCollider* pColliderBuffer, const Rope2* pOwner) const;
	void AdjustIndexOnRemoval(U32F iRemoved)
	{
		if (!IsRigidBody())
		{
			if (m_customIndex == iRemoved)
				m_customIndex = -1;
			else
				m_customIndex--;
		}
	}

	Locator GetLocator(const Rope2* pOwner) const;
	Locator GetPrevLocator(const Rope2* pOwner) const;
	const hknpShape* GetShape(const Rope2* pOwner) const;

	// Returns true if the two colliders can't move in respect to each other
	static bool AreCollidersAttached(const RopeColliderHandle& hCollider0, const RopeColliderHandle& hCollider1);

protected:
	RigidBodyHandle m_hRigidBody;
	I16 m_listIndex;
	I16 m_customIndex;
};

#endif // NDPHYS_ROPE2_COLLIDER_H 

