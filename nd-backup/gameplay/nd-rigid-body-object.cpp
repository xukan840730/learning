/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-rigid-body-object.h"

#include "gamelib/ndphys/buoyancy.h"
#include "gamelib/ndphys/collision-filter.h"
#include "ndlib/process/process-spawn-info.h"

FROM_PROCESS_DEFINE(NdRigidBodyObject);
PROCESS_REGISTER(NdRigidBodyObject, NdDrawableObject);
PROCESS_REGISTER_ALIAS(ProcessRigidBody, NdRigidBodyObject); // backwards compat

STATE_REGISTER(NdRigidBodyObject, Active, kPriorityNormal);

NdRigidBodyObject::~NdRigidBodyObject()
{
	m_rigidBody.Destroy();
	if (m_rbInited)
	{
		if (const ArtItemCollision* pColl = GetSingleCollision())
			ResourceTable::DecrementRefCount(pColl);
	}

	if (m_pBuoyancy != nullptr)
	{
		NDI_DELETE m_pBuoyancy;
		m_pBuoyancy = nullptr;
	}
}

Err NdRigidBodyObject::Init(const ProcessSpawnInfo& spawn)
{
	m_pBuoyancy = nullptr;

	Err result = ParentClass::Init(spawn);
	if (result.Failed())
		return result;

	result = InitRigidBody(m_rigidBody);
	if (result.Failed())
		return result;
	m_rbInited = true;

	m_autoUpdateBuoyancy = false;
	if (spawn.GetData(SID("buoyant"), false))
	{
		m_pBuoyancy = NDI_NEW BuoyancyAccumulator;
		if (m_pBuoyancy)
		{
			m_pBuoyancy->Init(&m_rigidBody, SID("buoyancy-object-generic"), true);
			m_pBuoyancy->m_flowMul = spawn.GetData<F32>(SID("buoyancy-flow-multiplier"), 1.0f);
			m_autoUpdateBuoyancy = true;
		}
	}

	if ( spawn.GetData(SID("SmallCollision"), false) )
		m_rigidBody.SetLayer( Collide::kLayerSmall );
	else
		m_rigidBody.SetLayer( Collide::kLayerGameObject );

	bool spawnActive = false;
	if ( spawn.GetData(SID("PhysicsNeverSleep"), false) )
	{
		spawnActive = true;
		m_rigidBody.SetDeactivationEnabled(false);
	}
	if ( spawnActive || spawn.GetData(SID("SpawnPhysAwake"), false) || m_pBuoyancy != nullptr)
	{
		m_rigidBody.Activate();
		m_rigidBody.SetMotionType(kRigidBodyMotionTypePhysicsDriven);
	}

	return result;
}

void NdRigidBodyObject::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_rigidBody.Relocate(deltaPos, lowerBound, upperBound);
	RelocateObject(m_pBuoyancy, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pBuoyancy, deltaPos, lowerBound, upperBound);
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

void NdRigidBodyObject::Active::Update()
{
	ParentClass::Update();

	NdRigidBodyObject& self = Self();

	if (self.m_pBuoyancy && self.m_autoUpdateBuoyancy)
	{
		WaterQuery::GeneralWaterQuery::CallbackContext context;
		STATIC_ASSERT(sizeof(MyWaterQueryContext) <= sizeof(WaterQuery::GeneralWaterQuery::CallbackContext));
		MyWaterQueryContext* pContext = (MyWaterQueryContext*)&context;
		pContext->m_hGo = &self;

		self.m_pBuoyancy->GatherWaterDetector();
		self.m_pBuoyancy->KickWaterDetector(FILE_LINE_FUNC, &self, WaterQuery::GeneralWaterQuery::Category::kMisc, self.m_lastTimeQueryWaterAllowed, FindWaterQuery, &context);
		self.m_pBuoyancy->Update(self);

		//if (!self.m_playedSplash && self.m_pBuoyancy && self.m_pBuoyancy->m_depth >= 0.0f)
		//{
		//	self.m_playedSplash = true;
		//	Point posWs = self.GetTranslation() + Vector(0.0f, self.m_pBuoyancy->m_depth - 0.2f, 0.0f);
		//	SpawnParticles(posWs, SID("propane-water-splash"), &self);
		//}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
WaterQuery::GeneralWaterQuery* NdRigidBodyObject::FindWaterQuery(const WaterQuery::GeneralWaterQuery::CallbackContext* pContext)
{
	const MyWaterQueryContext* pInContext = (const MyWaterQueryContext*)pContext;
	NdRigidBodyObject* pGo = NdRigidBodyObject::FromProcess(pInContext->m_hGo.ToMutableProcess());
	if (!pGo)
		return nullptr;

	return pGo->m_pBuoyancy->GetWaterQuery();
}

void NdRigidBodyObject::PostAnimUpdate_Async()
{
	ParentClass::PostAnimUpdate_Async();
}

void NdRigidBodyObject::OnKillProcess()
{
	m_rigidBody.SetMotionType(kRigidBodyMotionTypeNonPhysical);
	ParentClass::OnKillProcess();
}

