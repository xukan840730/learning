/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */


#include "gamelib/ndphys/rope/rope2-collider.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/ndphys/havok-util.h"
#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/camera/camera-manager.h"

#include <Physics/Physics/Collide/Shape/Composite/Compound/hknpCompoundShape.h>

void RopeCollider::FromRigidBody(const RigidBody* pBody, bool teleport, F32 invDt)
{
	FromRigidBodyAndShape(pBody, pBody->GetHavokShape(), teleport, invDt);
}

void RopeCollider::FromRigidBodyAndShape(const RigidBody* pBody, const hknpShape* pShape, bool teleport, F32 invDt)
{
	m_pShape = pShape;
	if (teleport)
	{
		m_loc = pBody->GetPreviousLocatorCm();
		// @@JS: We don't know what the velocity of this RB was before teleport :/
		m_linVel = kZero;
		m_angVel = kZero;	
		TeleportTo(pBody->GetLocatorCm());
		return;
	}
	m_loc = pBody->GetLocatorCm();
	m_locPrev = pBody->GetPreviousLocatorCm();

	// It would be more precise to use velocities from havok becasue they may be different for physics driven bodies
	// But to stay consistent and have rope debugger recover correct velocities from locators we will just use the locators
	// Probably no biggie
	//pBody->GetVelocity(m_linVel, m_angVel);
	m_linVel = (m_loc.GetTranslation() - m_locPrev.GetTranslation()) * invDt;
	m_angVel = GetAngVel(m_locPrev.GetRotation(), m_loc.GetRotation(), invDt);

	m_enabled = true;
	ASSERT(IsFinite(m_linVel));
	ASSERT(IsFinite(m_angVel));
}

RopeCollider::RopeCollider(const RopeCollider& parent, U32 childIndex)
{
	const hknpShape* pShape = parent.m_pShape;
	PHYSICS_ASSERT(pShape->getType() == hknpShapeType::COMPOUND);
	const hknpShapeInstance& inst = static_cast<const hknpCompoundShape*>(pShape)->getInstance(hknpShapeInstanceId(childIndex));

	const hkTransform childXfm = inst.getTransform();
	hkQuaternion childQuat(childXfm.getRotation());
	Locator childLoc(Point(childXfm.getTranslation().getQuad()), Quat(childQuat.m_vec.getQuad()));

	Vector childMoveWs = Rotate(parent.m_loc.GetRotation(), childLoc.GetTranslation() - kOrigin);
	Vector childLinVelAdd = Cross(parent.m_angVel, childMoveWs);

	m_loc = parent.m_loc.TransformLocator(childLoc);
	m_locPrev = parent.m_locPrev.TransformLocator(childLoc);

	m_linVel = childLoc.UntransformVector(parent.m_linVel) + childLinVelAdd;
	m_angVel = childLoc.UntransformVector(parent.m_angVel);

	m_pShape = inst.getShape();

	m_enabled = parent.m_enabled;
}

void RopeCollider::ResetLoc(const Locator& loc)
{
	m_locPrev = m_loc = loc;
	m_linVel = kZero;
	m_angVel = kZero;
}

void RopeCollider::UpdateLoc(const Locator& loc, bool teleport, F32 invDt)
{
	if (teleport)
	{
		TeleportTo(loc);
		return;
	}
	m_locPrev = m_loc;
	m_loc = loc;
	m_linVel = (m_loc.GetTranslation() - m_locPrev.GetTranslation()) * invDt;
	m_angVel = GetAngVel(m_locPrev.GetRotation(), m_loc.GetRotation(), invDt);
}

void RopeCollider::TeleportTo(const Locator& loc)
{
	// Teleport assuming the velocity is the same after teleport (but rotated by the teleport)
	// We need this for the train teleports. Hopefuly that should be the common case
	Locator teleLoc = loc.TransformLocator(Inverse(m_loc));
	m_linVel = teleLoc.TransformVector(m_linVel);
	m_angVel = teleLoc.TransformVector(m_angVel);
	m_loc = loc;

	// Backstep the prev loc
	m_locPrev.SetTranslation(m_loc.GetTranslation() - m_linVel * HavokGetDeltaTime());
	
	Scalar angSpeed;
	Vector axis = SafeNormalize(m_angVel, kZero, angSpeed);
	Quat drot = QuatFromAxisAngle(axis, -angSpeed * HavokGetDeltaTime());
	m_locPrev.SetRotation(drot * m_loc.GetRotation());
}

void RopeCollider::RestoreLoc(const Locator& loc, const Locator& locPrev, F32 invDt)
{
	m_locPrev = locPrev;
	m_loc = loc;
	m_linVel = (m_loc.GetTranslation() - m_locPrev.GetTranslation()) * invDt;
	m_angVel = GetAngVel(m_locPrev.GetRotation(), m_loc.GetRotation(), invDt);
}

const RopeCollider* RopeColliderHandle::GetCollider(RopeCollider* pColliderBuffer, const Rope2* pOwner) const
{
	if (!IsRigidBody())
	{
		return pOwner->GetCustomCollider(m_customIndex);
	}
	else if (const RigidBody* pBody = m_hRigidBody.ToBody())
	{
		pColliderBuffer->FromRigidBody(pBody, pOwner->GetTeleportThisFrame(), pOwner->m_scInvStepTime);
		if (m_listIndex >= 0)
		{
			*pColliderBuffer = RopeCollider(*pColliderBuffer, m_listIndex);
		}
		return pColliderBuffer;
	}
	else
	{
		return nullptr;
	}
}

Locator RopeColliderHandle::GetLocator(const Rope2* pOwner) const
{
	if (IsValid())
	{
		if (IsRigidBody())
		{
			if (const RigidBody* pBody = m_hRigidBody.ToBody())
			{
				return pBody->GetLocatorCm();
			}
		}
		else if (const RopeCollider* pCollider = pOwner->GetCustomCollider(m_customIndex))
		{
			return pCollider->m_loc;
		}
	}
	return Locator(kIdentity);
}

Locator RopeColliderHandle::GetPrevLocator(const Rope2* pOwner) const
{
	if (IsValid())
	{
		if (IsRigidBody())
		{
			if (const RigidBody* pBody = m_hRigidBody.ToBody())
			{
				return pBody->GetPreviousLocatorCm();
			}
		}
		else
		{
			return pOwner->GetCustomCollider(m_customIndex)->m_locPrev;
		}
	}
	return Locator(kIdentity);
}

const hknpShape* RopeColliderHandle::GetShape(const Rope2* pOwner) const
{
	if (IsValid())
	{
		if (IsRigidBody())
		{
			if (const RigidBody* pBody = m_hRigidBody.ToBody())
			{
				return pBody->GetHavokShape();
			}
		}
		else
		{
			return pOwner->GetCustomCollider(m_customIndex)->m_pShape;
		}
	}
	return nullptr;
}

//static 
bool RopeColliderHandle::AreCollidersAttached(const RopeColliderHandle& hCollider0, const RopeColliderHandle& hCollider1)
{
	if (hCollider0 == hCollider1)
	{
		return true;
	}

	const RigidBody* pBody0 = hCollider0.GetRigidBody();
	const RigidBody* pBody1 = hCollider1.GetRigidBody();

	if (pBody0 == pBody1)
	{
		return true;
	}

	if (pBody0 && pBody1 && pBody0->GetMotionType() == kRigidBodyMotionTypeFixed && pBody1->GetMotionType() == kRigidBodyMotionTypeFixed)
	{
		// 2 fixed bodies
		return true;
	}

	return false;
}
