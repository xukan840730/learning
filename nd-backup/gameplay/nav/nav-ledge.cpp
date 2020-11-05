/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#if ENABLE_NAV_LEDGES

#include "gamelib/gameplay/nav/nav-ledge.h"

#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "ndlib/render/util/prim-server-wrapper.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedge::RegisterActionPack(ActionPack* pActionPack)
{
	NAV_ASSERT(!pActionPack->IsCorrupted());
	NAV_ASSERT(!pActionPack->GetRegisteredNavLocation().IsValid());

	ActionPack** ppAp = &m_pRegistrationList;
	for (;;)
	{
		ActionPack* pAp = *ppAp;
		// is the action pack already in the list?
		if (pAp == pActionPack)
		{
			// no need to add it again
			break;
		}
		// are we at the end of the list?
		if (pAp == nullptr)
		{
			// add it
			*ppAp = pActionPack;
			break;
		}

		NAV_ASSERT(!pAp->IsCorrupted());

		// advance to next element of the list
		ppAp = &(pAp->m_pNavRegistrationListNext);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedge::UnregisterActionPack(ActionPack* pActionPack)
{
	bool found = false;

	ActionPack** ppAp = &m_pRegistrationList;
	for (;;)
	{
		ActionPack* pAp = *ppAp;
		// have we found the action pack we are looking for?
		if (pAp == pActionPack)
		{
			// remove it
			*ppAp = pActionPack->m_pNavRegistrationListNext;
			pActionPack->m_pNavRegistrationListNext = nullptr;
			found = true;
			break;
		}
		// reached end of list?
		if (pAp == nullptr)
		{
			// not found
			break;
		}
		// advance to next element of the list
		ppAp = &(pAp->m_pNavRegistrationListNext);
	}

	return found;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedge::RelocateActionPack(ActionPack* pActionPack, ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	// trivial case, our head pointer is the one we want to relocate
	if (m_pRegistrationList == pActionPack)
	{
		RelocatePointer(m_pRegistrationList, delta, lowerBound, upperBound);
		return;
	}

	ActionPack** ppAp = &m_pRegistrationList;
	for (;;)
	{
		ActionPack* pAp = *ppAp;

		if (pAp == nullptr)
			break;

		if (pAp->m_pNavRegistrationListNext == pActionPack)
		{
			// found referencing AP, relocate the pointer it has internally
			RelocatePointer(pAp->m_pNavRegistrationListNext, delta, lowerBound, upperBound);
			break;
		}
		// advance to next element of the list
		ppAp = &(pAp->m_pNavRegistrationListNext);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedge::ValidateRegisteredActionPacks() const
{
	ActionPack* const* ppAp = &m_pRegistrationList;
	for (;;)
	{
		ActionPack* pAp = *ppAp;

		if (nullptr == pAp)
			break;

		NAV_ASSERT(!pAp->IsCorrupted());

		// advance to next element of the list
		ppAp = &(pAp->m_pNavRegistrationListNext);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedge::DebugDraw(const Color& color) const
{
	STRIP_IN_FINAL_BUILD;

	const NavLedgeGraph* pGraph = GetNavLedgeGraph();
	const Locator originWs = pGraph->GetBoundFrame().GetLocatorWs();
	PrimServerWrapper ps(originWs);

	const Point v0 = GetVertex0Ls();
	const Point v1 = GetVertex1Ls();

	ps.SetLineWidth(4.0f);
	ps.EnableHiddenLineAlpha();

	ps.DrawCross(v0, 0.05f, color);
	ps.DrawCross(v1, 0.05f, color);
	ps.DrawLine(v0, v1, color);
}

#endif // ENABLE_NAV_LEDGES
