/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nav/process-nav-blocker.h"

#include "gamelib/gameplay/nav/nav-blocker-mgr.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/process/spawn-info.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessNavBlocker::ProcessNavBlocker()
{
	m_pStaticNavBlocker = nullptr;
	m_pDynamicNavBlocker = nullptr;
	m_enableRequested = false;
	m_blockProcessType = INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessNavBlocker::~ProcessNavBlocker()
{
	EnableNavBlocker(false);

	NavBlockerMgr& nbMgr = NavBlockerMgr::Get();

	if (m_pStaticNavBlocker)
	{
		nbMgr.FreeStatic(m_pStaticNavBlocker);
		m_pStaticNavBlocker = nullptr;
	}

	if (m_pDynamicNavBlocker)
	{
		nbMgr.FreeDynamic(m_pDynamicNavBlocker);
		m_pDynamicNavBlocker = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err ProcessNavBlocker::Init(const ProcessSpawnInfo& spawnInfo)
{
	const SpawnInfo& spawn = static_cast<const SpawnInfo&>(spawnInfo);

	SetUserId(spawnInfo.BareNameId(INVALID_STRING_ID_64), spawnInfo.NamespaceId(INVALID_STRING_ID_64), spawnInfo.NameId(INVALID_STRING_ID_64));

	const Err result = ParentClass::Init(spawn);
	if (result.Failed())
	{
		return result;
	}

	const StringId64 bindSpawnerId = GetBindSpawnerId();

	m_type = (SpawnerNavBlockerType)spawn.GetData(SID("NavBlockerType"), (int)SpawnerNavBlockerType::None);
	m_triggerObject = spawn.GetData<StringId64>(SID("trigger-object"), INVALID_STRING_ID_64);
	m_triggerRadius = spawn.GetData<float>(SID("trigger-radius"), -1.0f);
	m_registeredPosPs = GetTranslationPs();

	m_blockProcessType = spawn.GetData<StringId64>(SID("block-process-type"), INVALID_STRING_ID_64);

	NavBlockerMgr& nbMgr = NavBlockerMgr::Get();

	switch (m_type)
	{
	case SpawnerNavBlockerType::Static:
		m_boxMinLs = spawnInfo.GetData<Vector>(SID("NavBlockerBoxMin"), kZero);
		m_boxMaxLs = spawnInfo.GetData<Vector>(SID("NavBlockerBoxMax"), kZero);

		m_pStaticNavBlocker = nbMgr.AllocateStatic(this,
												   GetBoundFrame(),
												   bindSpawnerId,
												   SID("NavMeshBlocker"),
												   spawn,
												   FILE_LINE_FUNC);
		if (!m_pStaticNavBlocker)
		{
			return Err::kErrOutOfMemory;
		}
		break;

	case SpawnerNavBlockerType::Dynamic:
		{
			const float sizeX = Abs(spawnInfo.GetData<float>(SID("size-x"), 0.0f)) * 0.5f;
			const float sizeZ = Abs(spawnInfo.GetData<float>(SID("size-z"), 0.0f)) * 0.5f;
			m_boxMinLs = Vector(-sizeX, 0.0f, -sizeZ);
			m_boxMaxLs = Vector(sizeX, 0.0f, sizeZ);

			if (m_triggerObject == INVALID_STRING_ID_64)
			{
				m_pDynamicNavBlocker = nbMgr.AllocateDynamic(this, nullptr, FILE_LINE_FUNC);
				if (!m_pDynamicNavBlocker)
					return Err::kErrOutOfMemory;

				m_pDynamicNavBlocker->SetBlockProcessType(m_blockProcessType);
			}
		}
		break;
	}

	m_enableRequested = true;

	// Disable updates completely if we aren't looking for objects or moving with objects
	if ((m_triggerObject == INVALID_STRING_ID_64) && !m_pDynamicNavBlocker)
	{
		SetUpdateEnabled(false);
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessNavBlocker::ProcessUpdate()
{
	NavBlockerMgr& nbMgr = NavBlockerMgr::Get();

	if (m_triggerObject != INVALID_STRING_ID_64)
	{
		const Point myPosWs = GetTranslation();

		bool enable = false;

		if (m_enableRequested)
		{
			const NdLocatableObject* pTriggerObj = NdLocatableObject::FromProcess(EngineComponents::GetProcessMgr()->LookupProcessByUserId(m_triggerObject));

			if (pTriggerObj)
			{
				const float dist = Dist(pTriggerObj->GetTranslation(), myPosWs);

				enable = dist < m_triggerRadius;
			}
		}

		if (enable && !m_pDynamicNavBlocker)
		{
			m_pDynamicNavBlocker = nbMgr.AllocateDynamic(this, nullptr, FILE_LINE_FUNC);

			const Quat rotPs = GetLocatorPs().Rot();

			Point quad[4];
			quad[0] = Point(kOrigin) + Rotate(rotPs, Vector(m_boxMinLs.X(), 0.0f, m_boxMinLs.Z()));
			quad[1] = Point(kOrigin) + Rotate(rotPs, Vector(m_boxMaxLs.X(), 0.0f, m_boxMinLs.Z()));
			quad[2] = Point(kOrigin) + Rotate(rotPs, Vector(m_boxMaxLs.X(), 0.0f, m_boxMaxLs.Z()));
			quad[3] = Point(kOrigin) + Rotate(rotPs, Vector(m_boxMinLs.X(), 0.0f, m_boxMaxLs.Z()));

			m_pDynamicNavBlocker->SetQuad(quad);
		}
		else if (!enable && m_pDynamicNavBlocker)
		{
			nbMgr.FreeDynamic(m_pDynamicNavBlocker);
			m_pDynamicNavBlocker = nullptr;
		}
	}

	if (m_pDynamicNavBlocker)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		
		const Point myPosWs = GetTranslation();

		const NavPoly* pNavPoly = m_hNavPoly.ToNavPoly();
		
		if (pNavPoly)
		{
			const NavMesh* pNavMesh = m_hNavPoly.ToNavMesh();

			const Point myPosLs = pNavMesh->ParentToLocal(m_registeredPosPs);
			if (!pNavPoly->PolyContainsPointLs(myPosLs))
			{
				pNavPoly = pNavMesh->FindContainingPolyLs(myPosLs);
			}
		}
		
		if (!pNavPoly)
		{
			NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
			
			FindBestNavMeshParams params;
			params.m_pointWs = myPosWs;
			params.m_bindSpawnerNameId = GetBindSpawnerId();
			params.m_cullDist = (Length(m_boxMinLs) + Length(m_boxMaxLs)) * 1.25f;

			nmMgr.FindNavMeshWs(&params);

			pNavPoly = params.m_pNavPoly;

			if (pNavPoly)
			{
				m_registeredPosPs = GetParentSpace().UntransformPoint(params.m_nearestPointWs);
			}
		}

		m_hNavPoly = pNavPoly;
		m_pDynamicNavBlocker->SetNavPoly(pNavPoly);

		const Point myPosPs = GetTranslationPs();
		m_pDynamicNavBlocker->SetPosPs(myPosPs);

		const Quat rotPs = GetLocatorPs().Rot();

		Point quad[4];
		quad[0] = Point(kOrigin) + Rotate(rotPs, Vector(m_boxMinLs.X(), 0.0f, m_boxMinLs.Z()));
		quad[1] = Point(kOrigin) + Rotate(rotPs, Vector(m_boxMaxLs.X(), 0.0f, m_boxMinLs.Z()));
		quad[2] = Point(kOrigin) + Rotate(rotPs, Vector(m_boxMaxLs.X(), 0.0f, m_boxMaxLs.Z()));
		quad[3] = Point(kOrigin) + Rotate(rotPs, Vector(m_boxMinLs.X(), 0.0f, m_boxMaxLs.Z()));

		m_pDynamicNavBlocker->SetQuad(quad);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessNavBlocker::EnableNavBlocker(bool enable)
{
	m_enableRequested = enable;

	if (m_pStaticNavBlocker)
	{
		m_pStaticNavBlocker->RequestEnabled(enable);
	}

	if (m_pDynamicNavBlocker)
	{
		if (enable)
		{
			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
			m_pDynamicNavBlocker->SetNavPoly(m_hNavPoly.ToNavPoly());
		}
		else
		{
			m_pDynamicNavBlocker->SetNavPoly(nullptr);
		}
	}
}

void ProcessNavBlocker::AllocateDynamicNavBlocker()
{
	if (m_pDynamicNavBlocker)
		return;

	m_enableRequested = true;

	if (m_triggerObject == INVALID_STRING_ID_64)
	{
		m_pDynamicNavBlocker = NavBlockerMgr::Get().AllocateDynamic(this, nullptr, FILE_LINE_FUNC);
		if (m_pDynamicNavBlocker)
			m_pDynamicNavBlocker->SetBlockProcessType(m_blockProcessType);
	}
}

void ProcessNavBlocker::DeallocateDynamicNavBlocker()
{
	if (!m_pDynamicNavBlocker)
		return;

	m_enableRequested = false;

	if (m_triggerObject != INVALID_STRING_ID_64 && m_pDynamicNavBlocker)
	{
		NavBlockerMgr::Get().FreeDynamic(m_pDynamicNavBlocker);
		m_pDynamicNavBlocker = nullptr;
	}
}

PROCESS_REGISTER_ALLOC_SIZE(ProcessNavBlocker, NdLocatableObject, 512 + sizeof(NdLocatableSnapshot));
