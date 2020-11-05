/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/job/job-system-defines.h"
#include "corelib/math/sphere.h"
#include "corelib/system/read-write-atomic-lock.h"
#include "corelib/util/bit-array.h"

#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/ndphys/collision-cast.h"

FWD_DECL_PROCESS_HANDLE(NdGameObject);

struct FoundInteractable
{
	MutableNdGameObjectHandle	m_hInteractable;
	float						m_dist;

	static int SortDistAscending(const FoundInteractable& a, const FoundInteractable& b)
	{
		return (a.m_dist > b.m_dist) - (a.m_dist < b.m_dist);
	}
};

class NdInteractablesManager
{
public:
	NdInteractablesManager() : m_ndInteractablesMgrLock(JlsFixedIndex::kNdInteractablesMgrLock, SID("NdInteractablesMgrLock")) {}

	void KickUpdateJob();
	ndjob::CounterHandle KickPostRenderJob();

	void Update();
	void PostRender();

	void RegisterInteractable(MutableNdGameObjectHandle hInteractable);
	void UnregisterInteractable(MutableNdGameObjectHandle hInteractable);

	int FindRegisteredInteractables(NdGameObject** pOutputList, int outputCapacity) const;
	int FindRegisteredInteractablesInSphere(MutableNdGameObjectHandle* pOutputList, int outputCapacity, Sphere searchSphere) const;
	int FindRegisteredInteractablesWithNavLocationsInSphere(MutableNdGameObjectHandle* pOutputList, int outputCapacity, Sphere searchSphere) const;
	int FindRegisteredInteractablesBetweenRadii(FoundInteractable* pOutputList, int outputCapacity, Point centerWs, float radiusInner, float radiusOuter) const;

private:
	void VerifyInternalState() const;
	void DebugDraw() const;
	void Kick(const NdGameObject* const pGo);
	void Gather();

	static CONST_EXPR int kMaxReg = 1024;
	MutableNdGameObjectHandle m_ahReg[kMaxReg];
	BitArray<kMaxReg> m_regBits;
	mutable NdRwAtomicLock64_Jls m_ndInteractablesMgrLock;
	ndjob::CounterHandle m_hJobCounter;

	// for in-flight processing
	SphereCastJob m_sphereJob;
	RayCastJob m_rayJob;
	MutableNdGameObjectHandle m_hInFlight;
	static CONST_EXPR int kMaxCandidateNavLocs = 13;
	NavLocation m_candidateNavLocs[kMaxCandidateNavLocs];
	BitArray<kMaxCandidateNavLocs> m_proneProbes;
	bool m_doVerticalProbe;
};

extern NdInteractablesManager g_ndInteractablesMgr;
