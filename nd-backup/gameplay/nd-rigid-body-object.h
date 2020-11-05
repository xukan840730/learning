/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ND_RIGID_BODY_OBJECT_H
#define ND_RIGID_BODY_OBJECT_H

#include "gamelib/gameplay/nd-drawable-object.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/ndphys/water-query-callback.h"

class BuoyancyAccumulator;
class ProcessSpawnInfo;


//
// NdRigidBodyObject
// A simple drawable game object whose collision/physics model is a single rigid body.
//

class NdRigidBodyObject : public NdDrawableObject
{
private:
	typedef NdDrawableObject ParentClass;

public:
	FROM_PROCESS_DECLARE(NdRigidBodyObject);
	STATE_DECLARE_OVERRIDE(Active);

	NdRigidBodyObject() 
		: m_rbInited(false)
	{}

	~NdRigidBodyObject() override;

	virtual RigidBody* GetRigidBody() const override { return const_cast<RigidBody*>(&m_rigidBody); }

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void OnKillProcess() override;
	virtual void PostAnimUpdate_Async() override;

protected:

	struct MyWaterQueryContext
	{
		MutableNdGameObjectHandle m_hGo;
	};
	static WaterQuery::GeneralWaterQuery* FindWaterQuery(const WaterQuery::GeneralWaterQuery::CallbackContext* pContext);

	TimeFrame m_lastTimeQueryWaterAllowed;

	RigidBody m_rigidBody;
	BuoyancyAccumulator* m_pBuoyancy;
	bool m_autoUpdateBuoyancy;
	bool m_rbInited;
};

class NdRigidBodyObject::Active : public NdDrawableObject::Active
{
	typedef NdDrawableObject::Active ParentClass;

public:
	BIND_TO_PROCESS(NdRigidBodyObject);

	virtual void Update() override;
};

PROCESS_DECLARE(NdRigidBodyObject);

#endif // #ifndef ND_RIGID_BODY_OBJECT_H
