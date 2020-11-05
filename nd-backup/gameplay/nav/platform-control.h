/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/process/bound-frame.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPack;
class EntitySpawner;
#if ENABLE_NAV_LEDGES
class NavLedgeGraph;
#endif // ENABLE_NAV_LEDGES
class NavMesh;
class NdGameObject;
class RigidBody;
class TraversalActionPack;
struct FindBestNavMeshParams;
#if ENABLE_NAV_LEDGES
struct FindNavLedgeGraphParams;
#endif // ENABLE_NAV_LEDGES

/// --------------------------------------------------------------------------------------------------------------- ///
// platform _stuff
//
// platforms are moving objects with a nav mesh attached to them for NPCs to navigate on.
// In order to function properly, the platform must define a "rider space" that meets certain requirements:
//   - the rider space should be rotated the same as the platform's align (but it may be translated).  The reason for
//       this is that the nav mesh is modeled in Charter with respect to the platform's spawner.  We can't fix up the
//       nav mesh at login time either because we need the platform process to exist before any fixup can be applied.
//   - the rider space should not move with respect to the align, otherwise the nav mesh will rotate with it and no longer
//       be aligned with the object
//
class PlatformControl
{
public:
	static bool NeedsPlatformControl(const EntitySpawner* pSpawner);

	PlatformControl();
	void Init(NdGameObject* pSelf,
			  const RigidBody* pBindTarget,
			  bool basicInitOnly	 = false,
			  bool suppressTapErrors = false);
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	void Destroy(NdGameObject* pSelf);

	bool IsInitialized() const { return m_initialized; }

	void AddRider(NdGameObject* pRider);
	void RemoveRider(NdGameObject* pRider);
	U32F GetMaxRiderCount() const { return kMaxRiderCount; }
	NdGameObject* GetRider(U32F i) const
	{
		NAV_ASSERT(i < kMaxRiderCount);
		return m_riderList[i].ToMutableProcess();
	}

	void PostJointUpdate(NdGameObject& self);

	void RegisterActionPack(ActionPack* pAp, StringId64 bindId, bool immediately = false);
	void RegisterActionPackAt(ActionPack* pAp, const BoundFrame& location, StringId64 bindId, bool immediately = false);
	void UnregisterTraversalActionPacks(NdGameObject& self); // unregister all TAPs (object is being picked up or moved)
	void RegisterTraversalActionPacks(NdGameObject& self);	 // reregister all TAPs (object has been dropped or let go)

	void RegisterTraversalActionPacksImmediately(NdGameObject& self);

	void EnableTraversalActionPacks(bool enable);
	void UpdateTraversalActionPackLocations();
	I32 GatherTraversalActionPacksOnPlatform(TraversalActionPack** outTaps, I32 maxNum) const;

	RigidBody* GetBindTarget() const { return m_hBindTarget.ToBody(); }

	const NavMesh* GetNavMesh(U32F iMesh) const
	{
		NAV_ASSERT(iMesh < m_navMeshes.size());
		return m_navMeshes[iMesh];
	}
	U32F GetNavMeshCount() const { return m_navMeshes.size(); }
	bool HasRegisteredNavMesh() const;
	void FindNavMesh(FindBestNavMeshParams* pParams) const;

#if ENABLE_NAV_LEDGES
	const NavLedgeGraph* GetNavLedgeGraph(U32F iGraph) const
	{
		NAV_ASSERT(iGraph < m_ledgeGraphs.Size());
		return m_ledgeGraphs[iGraph];
	}
	U32F GetNavLedgeGraphCount() const { return m_ledgeGraphs.Size(); }
	bool HasRegisteredNavLedgeGraph() const;
	void FindNavLedgeGraph(FindNavLedgeGraphParams* pParams) const;
#endif // ENABLE_NAV_LEDGES

	void ResetOccupancyCount();

	U32F FindActionPacksByType(ActionPack** ppOutList, U32F kMaxOutCount, U32F apTypeMask);

	Locator GetActionPackRegistrationLocWs(ActionPack* pAp) const;

	const Locator GetPlatformSpace() const;

private:
	static const size_t kMaxNavMeshesPerPlatform;
#if ENABLE_NAV_LEDGES
	static const size_t kMaxNavLedgeGraphsPerPlatform;
#endif // ENABLE_NAV_LEDGES
	static const I32F kMaxRiderCount = 64;
	static const U32 kSignatureWord	 = 0x504c4154; // "PLAT"

	U32 m_sig1; // check for memory overwrite
	NdGameObjectHandle m_hSelf;
	MutableNdGameObjectHandle m_riderList[kMaxRiderCount];
	NavMeshArray m_navMeshes;
#if ENABLE_NAV_LEDGES
	NavLedgeGraphArray m_ledgeGraphs;
#endif // ENABLE_NAV_LEDGES
	RigidBodyHandle m_hBindTarget; // RigidBody to which this nav mesh is bound (parented)
	bool m_initialized;
	U32 m_sig2;					   // check for memory overwrite

	void Validate() const; // check integrity
};
