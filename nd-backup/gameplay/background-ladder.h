/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/list-array.h"
#include "corelib/system/read-write-atomic-lock.h"

#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nd-locatable.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct EdgeInfo;
class TraversalActionPack;

/// --------------------------------------------------------------------------------------------------------------- ///
class BackgroundLadder : public NdLocatableObject
{
public:
	typedef NdLocatableObject ParentClass;

	FROM_PROCESS_DECLARE(BackgroundLadder);

	virtual ~BackgroundLadder() override;

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void OnKillProcess() override;

	void DebugDraw(DebugPrimTime tt = kPrimDuration1FrameAuto) const;

	static void AllocateLadderTaps(const EntitySpawner& spawner);

	const ListArray<EdgeInfo>& GetEdges() const { return m_edges; }
	const EdgeInfo& GetTapTopEdge() const;
	const EdgeInfo& GetTapBottomEdge() const;

	bool IsRailingTop() const { return m_topTypeId == SID("railing"); }
	bool IsSideTop() const { return m_topTypeId == SID("side"); }
	// maybe support side enters later?
	bool CanEnterFromTop() const { return m_topTypeId == SID("railing"); }
	bool HasEdges() const { return !m_edges.IsEmpty(); }

private:
	struct LoginTaps
	{
		TraversalActionPack* m_pUpTap = nullptr;
		TraversalActionPack* m_pDownTap = nullptr;
	};

	bool GatherEdges();
	bool DetermineNavLocs();
	void CreateLadderTaps();
	void UnregisterTaps();

	const EdgeInfo& ClosestHeightLadderEdgeWs(Point_arg posWs) const;
	void GatherRegionInfo();

	ListArray<EdgeInfo> m_edges;
	NavLocation m_topNavLoc;
	NavLocation m_bottomNavLoc;
	ActionPackHandle m_hUpTap;
	ActionPackHandle m_hDownTap;
	StringId64 m_topTypeId = INVALID_STRING_ID_64;
	U32 m_numLadderEdges = 0;
	U32 m_iTapTopEdge = 0;
	U32 m_iTapBottomEdge = 0;
};

PROCESS_DECLARE(BackgroundLadder);

FWD_DECL_PROCESS_HANDLE(BackgroundLadder);

/// --------------------------------------------------------------------------------------------------------------- ///
class BgLadderManager
{
public:
	BgLadderManager();

	void Evaluation();
	void DebugDraw() const;

	void Register(BackgroundLadder* pLadder);

private:
	mutable NdRwAtomicLock64_Jls m_accessLock;

	static CONST_EXPR size_t kMaxLadders = 256;
	BackgroundLadderHandle m_hLadders[kMaxLadders];
	size_t m_numLadders = 0;
};

extern BgLadderManager g_bgLadderManager;
