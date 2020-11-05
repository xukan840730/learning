/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/platform-control.h"

#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-util.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level.h"
#include "ndlib/ndphys/rigid-body-base.h"

const size_t PlatformControl::kMaxNavMeshesPerPlatform = 40;

#if ENABLE_NAV_LEDGES
const size_t PlatformControl::kMaxNavLedgeGraphsPerPlatform = 8;
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::Validate() const
{
	NAV_ASSERTF(m_sig1 == kSignatureWord && m_sig2 == kSignatureWord,
				("PlatformControl: MEMORY OVERWRITE! sig1=0x%08X, sig2=0x%08X, nav mesh ptrs:\n"
				 " 0x%08X\n 0x%08X\n 0x%08X\n 0x%08X\n 0x%08X\n 0x%08X\n 0x%08X\n 0x%08X\n",
				 m_sig1,
				 m_sig2,
				 *PunPtr<const int*>(&m_navMeshes[0]),
				 *PunPtr<const int*>(&m_navMeshes[1]),
				 *PunPtr<const int*>(&m_navMeshes[2]),
				 *PunPtr<const int*>(&m_navMeshes[3]),
				 *PunPtr<const int*>(&m_navMeshes[4]),
				 *PunPtr<const int*>(&m_navMeshes[5]),
				 *PunPtr<const int*>(&m_navMeshes[6]),
				 *PunPtr<const int*>(&m_navMeshes[7])));
}

/// --------------------------------------------------------------------------------------------------------------- ///
// if a login chunk is a nav mesh, this entity needs a PlatformControl
bool PlatformControl::NeedsPlatformControl(const EntitySpawner* pSpawner)
{
	if (!pSpawner)
	{
		return false;
	}

	if (pSpawner->GetData(SID("force-disable-npc-platform"), false))
	{
		return false;
	}

	for (SpawnerLoginChunkNode* pNode = pSpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
	{
		if (pNode->GetNavMesh())
			return true;

#if ENABLE_NAV_LEDGES
		if (pNode->GetNavLedgeGraph())
			return true;
#endif
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
PlatformControl::PlatformControl()
{
	m_sig1 = kSignatureWord;
	m_sig2 = kSignatureWord;
	m_initialized = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::Init(NdGameObject* pSelf,
						   const RigidBody* pBindTarget,
						   bool basicInitOnly,
						   bool suppressTapErrors)
{
	NAV_ASSERT(pSelf);
	Validate();
	m_hSelf = pSelf;
	m_hBindTarget = pBindTarget;
	m_navMeshes.Init(kMaxNavMeshesPerPlatform, FILE_LINE_FUNC);
#if ENABLE_NAV_LEDGES
	m_ledgeGraphs.Init(kMaxNavLedgeGraphsPerPlatform, FILE_LINE_FUNC);
#endif
	m_initialized = !basicInitOnly;

	if (basicInitOnly)
	{
		if (const EntitySpawner* pMySpawner = pSelf->GetSpawner())
		{
			for (SpawnerLoginChunkNode* pNode = pMySpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
			{
				// if login chunk is an action pack,
				if (ActionPack* pAp = pNode->GetActionPack())
				{
					pAp->SetOwnerProcess(pSelf);
				}
			}
		}
	}
	else if (pBindTarget)
	{
		const Locator parentSpace = pBindTarget->GetLocatorCm();
		const Locator alignLocWs = pSelf->GetLocator();
		const Point navOffset = parentSpace.UntransformPoint(alignLocWs.Pos());  // align relative to "root" phys external

		if (const EntitySpawner* pMySpawner = pSelf->GetSpawner())
		{
			MutableProcessHandle origHandle = pMySpawner->m_handle;
			pMySpawner->m_handle = pSelf;  // hack to fix registering action packs
			const StringId64 nameId = pMySpawner->NameId();
			if (const Level* pLev = pMySpawner->GetLevel())
			{
				const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
				NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

				U32F attachedErrorCount = 0;
				//NavMesh* pNavMeshList[kMaxNavMeshesPerPlatform];
				// Find the nav meshes in the level that have us as the parent
				// walk all of my login chunks,
				for (SpawnerLoginChunkNode* pNode = pMySpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
				{
					// if login chunk is a nav mesh,
					if (NavMesh* pNavMesh = pNode->GetNavMesh())
					{
						NAV_ASSERT(!pNavMesh->IsAttached());
						NAV_ASSERT(nameId == pNavMesh->GetBindSpawnerNameId());
						if (pNavMesh->IsAttached())
						{
							++attachedErrorCount;
							continue;
						}
						// if we have room in our list of nav meshes
						if (m_navMeshes.size() < kMaxNavMeshesPerPlatform)
						{
							m_navMeshes.push_back(pNavMesh);

							pNavMesh->ConfigureParentSpace(alignLocWs, pBindTarget);
							pNavMesh->SetAttached(true);
							pNavMesh->Register();
						}
					}

#if ENABLE_NAV_LEDGES
					if (NavLedgeGraph* pGraph = pNode->GetNavLedgeGraph())
					{
						NAV_ASSERT(pGraph->IsAttached());
						NAV_ASSERT(nameId == pGraph->GetBindSpawnerNameId());

						if (pGraph->IsAttached())
						{
							++attachedErrorCount;
							continue;
						}
						// if we have room in our list of nav meshes
						if (m_ledgeGraphs.size() < kMaxNavLedgeGraphsPerPlatform)
						{
							m_ledgeGraphs.push_back(pGraph);

							pGraph->SetBinding(Binding(pBindTarget));
							pGraph->SetAttached(true);
							pGraph->Register();
						}
					}
#endif
				}

				if (attachedErrorCount > 0)
				{
					MsgConErr("PlatformControl::Init: object '%s' was respawned before the previous incarnation of it was cleaned up!\n"
							   "%d navmeshes/graphs were not attached to this object as a result.\n",
							   DevKitOnly_StringIdToString(nameId),
							   int(attachedErrorCount));
					pMySpawner->m_handle = origHandle;
					return;
				}
				// take care of action packs
				// walk all of my login chunks,

				// these locators could be simplified away
				//const Locator alignLocWs = pSelf->GetLocator();
				//const Locator mySpawnLocWs = pMySpawner->GetStaticWorldSpaceLocator();
				//const Locator mySpawnLocInvWs = Inverse(mySpawnLocWs);
				//const Locator initMoveAdjustLocWs = alignLocWs.TransformLocator(mySpawnLocInvWs);  // how much align has moved during init
				for (SpawnerLoginChunkNode* pNode = pMySpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
				{
					// if login chunk is an action pack,
					if (ActionPack* pAp = pNode->GetActionPack())
					{
						pAp->SetOwnerProcess(pSelf);

						if (!pAp->IsRegistered())
						{
							RegisterActionPack(pAp, nameId, suppressTapErrors);
						}
					}
					else if (const SpawnerCoverDef* pCoverDef = pNode->GetCoverDef())
					{
						Locator apLocPs = pCoverDef->m_locPs;
						apLocPs.Move(navOffset - kOrigin);
						Locator apLocWs = parentSpace.TransformLocator(apLocPs);
						BoundFrame bf(apLocWs, Binding(pBindTarget));

						CoverDefinition coverDef;
						coverDef.m_coverType = pCoverDef->m_coverType;
						coverDef.m_height = 1.0f;
						coverDef.m_tempCost = 0.0f;
						coverDef.m_costUpdateTime = Seconds(0);

						ActionPackRegistrationParams params;
						params.m_pAllocLevel		= pLev;
						params.m_hPlatformOwner		= pSelf;
						params.m_bindId				= pCoverDef->m_bindSpawnerNameId;
						params.m_yThreshold			= 1.0f;
						params.m_regPtLs			= POINT_LC(0.0f, 0.0f, -0.3f);

						Point nearestOnNavmeshPtWs;
						if (const NavPoly* pPoly = CoverActionPack::CanRegisterSelf(coverDef, params, bf, &nearestOnNavmeshPtWs, false))
						{
							if (CoverActionPack* pActionPack = pLev->AllocateCoverActionPack())
							{
								// placement NEW
								NDI_NEW((void*) pActionPack) CoverActionPack(bf, pMySpawner, coverDef, -1);

								pActionPack->SetDynamic(true);
								pActionPack->RequestRegistration(params);
								pActionPack->SetOwnerProcess(pSelf);

								// find occupant position and its navmesh and store as NavLocation
								{
									const Point occupantPosWs = pActionPack->GetDefensivePosWs();
									const NavMesh* pMesh = pPoly->GetNavMesh();
									const Point nearestOnNavmeshPtPs = pMesh->WorldToParent(nearestOnNavmeshPtWs);

									if (pMesh)
									{
										NavLocation occupantNavLoc;
										const Point probeStartPs = nearestOnNavmeshPtPs;
										const Vector probeVecPs = pMesh->WorldToParent(occupantPosWs) - nearestOnNavmeshPtPs;

										NavMesh::ProbeParams probeParams;
										probeParams.m_start = probeStartPs;
										probeParams.m_pStartPoly = pPoly;
										probeParams.m_move = probeVecPs;
										probeParams.m_probeRadius = 0.0f;
										NavMesh::ProbeResult result = pMesh->ProbePs(&probeParams);
										NAV_ASSERT(result == NavMesh::ProbeResult::kReachedGoal);

										occupantNavLoc.SetPs(probeParams.m_endPoint, probeParams.m_pReachedPoly);

										pActionPack->SetOccupantNavLoc(occupantNavLoc);
									}

								}
							}
						}
					}
				}
				Validate();
			}

			// register covers from our featureDb to our own nav meshes
			if ((m_navMeshes.size() > 0) && pBindTarget)
			{
				pSelf->RegisterCoverActionPacksInternal(pSelf, pBindTarget);
			}

			if ((m_navMeshes.Size() > 0)
#if ENABLE_NAV_LEDGES
				|| (m_ledgeGraphs.Size() > 0)
#endif
				)
			{
				ActionPackMgr::Get().RequestUpdateTraversalActionPackLinkages(); // TAPs need updating anytime a navmesh moves
			}

			pMySpawner->m_handle = origHandle;
		}
	}
	Validate();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_navMeshes.Relocate(deltaPos, lowerBound, upperBound);
#if ENABLE_NAV_LEDGES
	m_ledgeGraphs.Relocate(deltaPos, lowerBound, upperBound);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::Destroy(NdGameObject* pSelf)
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	Validate();

	// a spawner is required
	if (const EntitySpawner* pMySpawner = pSelf->GetSpawner())
	{
		StringId64 nameId = pMySpawner->NameId();
		if (const Level* pLev = pMySpawner->GetLevel())
		{
			// walk all of my login chunks,
			for (SpawnerLoginChunkNode* pNode = pMySpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
			{
				// if login chunk is an action pack,
				ActionPack* pAp = pNode ? pNode->GetActionPack() : nullptr;
				if (pAp)
				{
					if (pAp->IsDynamic())
					{
						// remove it completely from the AP system
						pAp->Logout();
					}
					else if (pAp->IsRegistered())
					{
						pAp->UnregisterImmediately();
					}
				}
			}
		}

		Validate();

		NAV_ASSERTF(m_navMeshes.size() <= kMaxNavMeshesPerPlatform,
					("navMeshCount 0x%08X suggests memory overwrite of PlatformControl data, nav mesh ptrs:\n"
					 " 0x%08X\n 0x%08X\n 0x%08X\n 0x%08X\n 0x%08X\n 0x%08X\n 0x%08X\n 0x%08X\n",
					 m_navMeshes.size(),
					 *PunPtr<const int*>(&m_navMeshes[0]),
					 *PunPtr<const int*>(&m_navMeshes[1]),
					 *PunPtr<const int*>(&m_navMeshes[2]),
					 *PunPtr<const int*>(&m_navMeshes[3]),
					 *PunPtr<const int*>(&m_navMeshes[4]),
					 *PunPtr<const int*>(&m_navMeshes[5]),
					 *PunPtr<const int*>(&m_navMeshes[6]),
					 *PunPtr<const int*>(&m_navMeshes[7])));
	}

	for (I32F iNavMesh = 0; iNavMesh < m_navMeshes.size(); ++iNavMesh)
	{
		if (NavMesh* pNavMesh = const_cast<NavMesh*>(m_navMeshes[iNavMesh]))
		{
			pNavMesh->UnregisterAllActionPacks();
			pNavMesh->SetAttached(false);
			pNavMesh->Unregister();
		}
	}

#if ENABLE_NAV_LEDGES
	for (U32F iGraph = 0; iGraph < m_ledgeGraphs.Size(); ++iGraph)
	{
		if (NavLedgeGraph* pGraph = const_cast<NavLedgeGraph*>(m_ledgeGraphs[iGraph]))
		{
			pGraph->UnregisterAllActionPacks();
			pGraph->SetAttached(false);
			pGraph->Unregister();
		}
	}
#endif

	Validate();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::AddRider(NdGameObject* pRider)
{
	NAV_ASSERT(pRider);
	if (pRider)
	{
		bool foundSlot = false;
		for (I32F iRider = 0; iRider < kMaxRiderCount; ++iRider)
		{
			if (const NdGameObject* pProc = m_riderList[iRider].ToProcess())
			{
				if (pRider == pProc)
				{
					foundSlot = true;
					break;
				}
			}
			else
			{
				m_riderList[iRider] = pRider;
				foundSlot = true;
				break;
			}
		}
		if (!foundSlot)
		{
			const char* strPlatformName = "";
			if (const NdGameObject* pSelf = m_hSelf.ToProcess())
			{
				strPlatformName = pSelf->GetName();
			}
			MsgErr("PlatformControl:  platform %s is full of riders!\n", strPlatformName);
			for (I32F iRider = 0; iRider < kMaxRiderCount; ++iRider)
			{
				if (const NdGameObject* pProc = m_riderList[iRider].ToProcess())
				{
					MsgErr("PlatformControl:  rider %d is %s\n", I32(iRider), pProc->GetName());
				}
			}
			NAV_ASSERT(false);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::RemoveRider(NdGameObject* pRider)
{
	NAV_ASSERT(pRider);
	if (pRider)
	{
		bool foundSlot = false;
		for (I32F iRider = 0; iRider < kMaxRiderCount; ++iRider)
		{
			if (const NdGameObject* pProc = m_riderList[iRider].ToProcess())
			{
				if (pRider == pProc)
				{
					m_riderList[iRider] = nullptr;
					foundSlot = true;
					break;
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::ResetOccupancyCount()
{
#if 0 // TODO - fix this
	U32F navMeshCount = GetNavMeshCount();
	bool enabled = false;
	for (U32F iMesh = 0; iMesh < navMeshCount; ++iMesh)
	{
		const TypedResItemHandle<NavMesh>* pHandle = m_navMeshes[iMesh];
		if (pHandle->GetItem() != nullptr)
		{
			while (EngineComponents::GetNavMeshMgr()->GetOccupancyCount(pHandle) > 0)
				EngineComponents::GetNavMeshMgr()->DecOccupancyCount(pHandle);
		}
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator PlatformControl::GetPlatformSpace() const
{
	Locator loc(kIdentity);
	if (const NdGameObject* pSelf = m_hSelf.ToProcess())
	{
		if (const RigidBody* pBindTarget = pSelf->GetDefaultBindTarget())
		{
			loc = pBindTarget->GetLocatorCm();
		}
	}
	return loc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::PostJointUpdate(NdGameObject& self)
{
	PROFILE(Processes, PlatCtrl_PostJointUpdate);
	Validate();

	if (const RigidBody* pBindTarget = GetBindTarget())
	{
		const Locator& oldLoc = pBindTarget->GetPreviousLocatorCm();
		const Locator& newLoc = pBindTarget->GetLocatorCm();
		const Locator deltaLoc = newLoc.TransformLocator(Inverse(oldLoc));

		for (I32F iRider = 0; iRider < kMaxRiderCount; ++iRider)
		{
			if (NdGameObject* pProc = m_riderList[iRider].ToMutableProcess())
			{
				Binding bind = pProc->GetBinding();
				if (bind.GetRigidBody() == pBindTarget)
				{
					pProc->OnPlatformMoved(deltaLoc);
				}
				else
				{
					m_riderList[iRider] = nullptr;
				}
			}
		}
	}
	Validate();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PlatformControl::HasRegisteredNavMesh() const
{
	const U32F navMeshCount = GetNavMeshCount();

	bool enabled = false;
	for (U32F iMesh = 0; iMesh < navMeshCount; ++iMesh)
	{
		if (m_navMeshes[iMesh] && m_navMeshes[iMesh]->IsRegistered())
		{
			enabled = true;
			break;
		}
	}

	return enabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::FindNavMesh(FindBestNavMeshParams* pParams) const
{
	FindNavMeshFromArray(m_navMeshes, pParams);
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
bool PlatformControl::HasRegisteredNavLedgeGraph() const
{
	const U32F ledgeGraphCount = GetNavLedgeGraphCount();

	bool enabled = false;
	for (U32F iGraph = 0; iGraph < ledgeGraphCount; ++iGraph)
	{
		if (m_ledgeGraphs[iGraph] && m_ledgeGraphs[iGraph]->IsRegistered())
		{
			enabled = true;
			break;
		}
	}

	return enabled;
}
#endif

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::FindNavLedgeGraph(FindNavLedgeGraphParams* pParams) const
{
	FindNavLedgeGraphFromArray(m_ledgeGraphs, pParams);
}
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
U32F PlatformControl::FindActionPacksByType(ActionPack** ppOutList, U32F kMaxOutCount, U32F apTypeMask)
{
	U32F outCount = 0;
	for (U32F iNavMesh = 0; iNavMesh < m_navMeshes.size(); ++iNavMesh)
	{
		if (const NavMesh* pNavMesh = m_navMeshes[iNavMesh])
		{
			const U32F polyCount = pNavMesh->GetPolyCount();
			for (U32F iNavPoly = 0; iNavPoly < polyCount; ++iNavPoly)
			{
				const NavPoly& poly = pNavMesh->GetPoly(iNavPoly);
				for (ActionPack* pAp = poly.GetRegisteredActionPackList(); pAp; pAp = pAp->GetRegistrationListNext())
				{
					if (((1U << pAp->GetType()) & apTypeMask) && outCount < kMaxOutCount)
					{
						ppOutList[outCount++] = pAp;
					}
				}
			}
		}
	}

#if ENABLE_NAV_LEDGES
	for (U32F iGraph = 0; iGraph < m_ledgeGraphs.Size(); ++iGraph)
	{
		if (const NavLedgeGraph* pGraph = m_ledgeGraphs[iGraph])
		{
			const U32F ledgeCount = pGraph->GetLedgeCount();

			for (U32F iLedge = 0; iLedge < ledgeCount; ++iLedge)
			{
				const NavLedge* pLedge = pGraph->GetLedge(iLedge);
				if (!pLedge)
					continue;

				for (ActionPack* pAp = pLedge->GetRegisteredActionPackList(); pAp; pAp = pAp->GetRegistrationListNext())
				{
					if (((1U << pAp->GetType()) & apTypeMask) && outCount < kMaxOutCount)
					{
						ppOutList[outCount++] = pAp;
					}
				}
			}
		}
	}
#endif

	return outCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::UnregisterTraversalActionPacks(NdGameObject& self)
{
	if (const EntitySpawner* pMySpawner = self.GetSpawner())
	{
		StringId64 nameId = pMySpawner->NameId();
		if (const Level* pLev = pMySpawner->GetLevel())
		{
			// take care of action packs
			// walk all of my login chunks,
			for (SpawnerLoginChunkNode* pNode = pMySpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
			{
				// if login chunk is an action pack,
				if (ActionPack* pAp = pNode->GetActionPack())
				{
					if (pAp->GetType() == ActionPack::kTraversalActionPack && pAp->IsRegistered())
					{
						TraversalActionPack* pTap = static_cast<TraversalActionPack*>(pAp);
						pTap->RequestUnregistration();
					}
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::RegisterTraversalActionPacks(NdGameObject& self)
{
	//Make sure we unregister first
	UnregisterTraversalActionPacks(self);

	if (const EntitySpawner* pMySpawner = self.GetSpawner())
	{
		StringId64 nameId = pMySpawner->NameId();
		if (const Level* pLev = pMySpawner->GetLevel())
		{
			// take care of action packs
			// walk all of my login chunks,
			for (SpawnerLoginChunkNode* pNode = pMySpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
			{
				// if login chunk is an action pack,
				if (ActionPack* pAp = pNode->GetActionPack())
				{
					if (pAp->GetType() == ActionPack::kTraversalActionPack)
					{
						TraversalActionPack* pTap = static_cast<TraversalActionPack*>(pAp);
						const StringId64 bindId = pMySpawner->NameId();
						RegisterActionPack(pAp, bindId, true);
					}
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::RegisterTraversalActionPacksImmediately(NdGameObject& self)
{
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	if (const EntitySpawner* pMySpawner = self.GetSpawner())
	{
		StringId64 nameId = pMySpawner->NameId();
		if (const Level* pLev = pMySpawner->GetLevel())
		{
			// take care of action packs
			// walk all of my login chunks,
			for (SpawnerLoginChunkNode* pNode = pMySpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
			{
				// if login chunk is an action pack,
				if (ActionPack* pAp = pNode->GetActionPack())
				{
					if (pAp->GetType() == ActionPack::kTraversalActionPack)
					{
						TraversalActionPack* pTap = static_cast<TraversalActionPack*>(pAp);
						if (pTap->IsRegistered())
						{
							pTap->UnregisterImmediately();
						}

						const StringId64 bindId = pMySpawner->NameId();
						RegisterActionPack(pAp, bindId, true);
					}
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 PlatformControl::GatherTraversalActionPacksOnPlatform(TraversalActionPack** outTaps, I32 maxNum) const
{
	const NdGameObject* pSelf = m_hSelf.ToProcess();
	const EntitySpawner* pMySpawner = pSelf ? pSelf->GetSpawner() : nullptr;
	if (!pMySpawner)
		return 0;

	I32 count = 0;
	StringId64 nameId = pMySpawner->NameId();

	// take care of action packs
	// walk all of my login chunks,
	for (SpawnerLoginChunkNode* pNode = pMySpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
	{
		// if login chunk is an action pack,
		if (ActionPack* pAp = pNode->GetActionPack())
		{
			if (pAp->GetType() != ActionPack::kTraversalActionPack)
				continue;

			TraversalActionPack* pTap = static_cast<TraversalActionPack*>(pAp);
			NAV_ASSERT(pTap);

			outTaps[count++] = pTap;
			if (count >= maxNum)
				break;
		}
	}
	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::EnableTraversalActionPacks(bool enable)
{
	const NdGameObject* pSelf = m_hSelf.ToProcess();
	const EntitySpawner* pMySpawner = pSelf ? pSelf->GetSpawner() : nullptr;
	if (!pMySpawner)
		return;

	ActionPackMgr& apMgr = ActionPackMgr::Get();

	// take care of action packs
	// walk all of my login chunks,
	for (SpawnerLoginChunkNode* pNode = pMySpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
	{
		ActionPack* pAp = pNode->GetActionPack();
		if (!pAp)
			continue;

		if (pAp->GetType() != ActionPack::kTraversalActionPack)
			continue;

		TraversalActionPack* pTap = (TraversalActionPack*)pAp;
		const U32F userCount = pTap->GetUserCount();

		pAp->Enable(enable);

		if (enable && (0 == userCount))
		{
			if (apMgr.IsRegisteredOrPending(pAp->GetMgrId()))
			{
				apMgr.RequestUnregistration(pAp);
			}
			apMgr.RequestRegistration(pAp);
		}

		if (const EntitySpawner* pSpawner = pTap->GetSpawner())
		{
			const Locator newLocWs = pSpawner->GetWorldSpaceLocator().TransformLocator(pTap->GetSpawnerSpaceLoc());

			BoundFrame loc = pTap->GetBoundFrame();
			pTap->m_loc.SetLocatorWs(newLocWs);
			pTap->m_origLoc.SetLocatorWs(newLocWs);

			pTap->ResetDestBoundFrame();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::UpdateTraversalActionPackLocations()
{
	const NdGameObject* pSelf = m_hSelf.ToProcess();
	const EntitySpawner* pMySpawner = pSelf ? pSelf->GetSpawner() : nullptr;
	if (!pMySpawner)
		return;

	ActionPackMgr& apMgr = ActionPackMgr::Get();

	// take care of action packs
	// walk all of my login chunks,
	for (SpawnerLoginChunkNode* pNode = pMySpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
	{
		ActionPack* pAp = pNode->GetActionPack();
		if (!pAp)
			continue;

		if (pAp->GetType() != ActionPack::kTraversalActionPack)
			continue;

		TraversalActionPack* pTap = (TraversalActionPack*)pAp;
		const EntitySpawner* pSpawner = pTap ? pTap->GetSpawner() : nullptr;

		if (!pSpawner)
			continue;

		const Locator newLocWs = pSpawner->GetWorldSpaceLocator().TransformLocator(pTap->GetSpawnerSpaceLoc());

		BoundFrame loc = pTap->GetBoundFrame();
		pTap->m_loc.SetLocatorWs(newLocWs);
		pTap->m_origLoc.SetLocatorWs(newLocWs);

		pTap->ResetDestBoundFrame();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator PlatformControl::GetActionPackRegistrationLocWs(ActionPack* pAp) const
{
	NAV_ASSERT(pAp);

	const EntitySpawner* pApSpawner = pAp->GetSpawner();
	NAV_ASSERT(pApSpawner);

	// use a loc relative to the spawner since the platform may have moved before AP is registered
	const Locator locWs = pApSpawner->GetWorldSpaceLocator().TransformLocator(pAp->GetSpawnerSpaceLoc());
	return locWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::RegisterActionPack(ActionPack* pAp,
										 StringId64 bindId,
										 bool immediately /* = false */)
{
	NAV_ASSERT(pAp);

	const EntitySpawner* pApSpawner = pAp->GetSpawner();
	NAV_ASSERT(pApSpawner);

	// use a loc relative to the spawner since the platform may have moved before AP is registered
	const Locator locWs = pApSpawner->GetWorldSpaceLocator().TransformLocator(pAp->GetSpawnerSpaceLoc());
	RegisterActionPackAt(pAp, BoundFrame(locWs, Binding()), bindId, immediately);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlatformControl::RegisterActionPackAt(ActionPack* pAp,
										   const BoundFrame& location,
										   StringId64 bindId,
										   bool immediately /* = false */)
{
	NAV_ASSERT(pAp);

	const EntitySpawner* pApSpawner = pAp->GetSpawner();
	NAV_ASSERT(pApSpawner);

	// parent the input locator to my bind target
	const RigidBody* pBindTarget = m_hBindTarget.ToBody();
	const BoundFrame bf(location.GetLocatorWs(), Binding(pBindTarget));
	pAp->SetBoundFrame(bf);

	ActionPackRegistrationParams params;
	params.m_pAllocLevel	= pApSpawner->GetLevel();
	params.m_hPlatformOwner	= m_hSelf;
	params.m_bindId			= bindId;
	params.m_regPtLs		= pAp->GetRegistrationPointLs();

	// register action pack to nav mesh
	if (immediately)
	{
		pAp->RegisterImmediately(params);
	}
	else
	{
		pAp->RequestRegistration(params);
	}
}
