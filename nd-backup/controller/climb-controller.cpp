/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#if ENABLE_NAV_LEDGES

#include "game/ai/controller/climb-controller.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/agent/npc-manager.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/player/player.h"
#include "game/player/edge/player-climb2.h"
#include "game/player/edge/player-edge.h"
#include "game/scriptx/h/player-climb-defines.h"
#include "game/scriptx/h/player-settings-defines.h"
#include "game/scriptx/h/hit-reactions-defines.h"
#include "game/net/net-game-manager.h"
#include "game/gameinfo.h"

#include "gamelib/level/level-mgr.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/ai/controller/nd-weapon-controller.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-mgr.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-util.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/character-leg-raycaster.h"
#include "gamelib/scriptx/h/anim-nav-character-info.h"
#include "gamelib/anim/gesture-controller.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/anim-snapshot-node-animation.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/util/finite-state-machine.h"
#include "ndlib/nd-options.h"

#include "corelib/math/segment-util.h"
#include "corelib/memory/scoped-temp-allocator.h"

#if FINAL_BUILD
#define ClimbLogStr(str)
#define ClimbLog(str, ...)
#else
#define ClimbLogStr(str)                                                                                               \
	AiLogAnim(GetCharacter(),                                                                                          \
			  AI_LOG,                                                                                                  \
			  "[climb-%d] [%s : %s] " str,                                                                             \
			  GetCommandId(),                                                                                          \
			  GetStateName("<none>"),                                                                                  \
			  DevKitOnly_StringIdToString(GetAnimStateId()))
#define ClimbLog(str, ...)                                                                                             \
	AiLogAnim(GetCharacter(),                                                                                          \
			  AI_LOG,                                                                                                  \
			  "[climb-%d] [%s : %s] " str,                                                                             \
			  GetCommandId(),                                                                                          \
			  GetStateName("<none>"),                                                                                  \
			  DevKitOnly_StringIdToString(GetAnimStateId()),                                                           \
			  __VA_ARGS__)
#endif

const F32 kEdgeSearchRadius           = 0.5f;
const F32 kEdgeSearchCharRadius       = 1.0f;
const F32 kBlendTimeDefault           = 0.3f;
const F32 kLoopBlendOutTimeDefault    = 0.1f;
const F32 kShimmyDistMax              = 0.2f;
const F32 kReachDistMax               = 1.0f;
const U32 kInvalidEdgeIndex           = UINT_MAX;
const TimeFrame kRequestIdleDelay     = Seconds(0.1f);
const TimeFrame kPlayerClimbIdleDelay = Seconds(1.5f);

const StringId64 kShimmyAnimId            = SID("ledge-grab-strafe-r");
const StringId64 kHangShimmyAnimId        = SID("ledge-hang-strafe-r-long");
const StringId64 kWallShimmyAnimId        = SID("ledge-facing-walk-fast-r");
const StringId64 kClimbToWallShimmyAnimId = SID("ledge-grab-idle^ledge-facing-idle-full");
const StringId64 kWallShimmyToClimbAnimId = SID("ledge-facing-idle^ledge-grab-idle");

/// --------------------------------------------------------------------------------------------------------------- ///
static const DC::ClimbInfo* GetClimbInfo()
{
	const DC::ClimbInfo* pClimbInfo = ScriptPointer<DC::ClimbInfo>(SID("*player-climb-info*"));

	return pClimbInfo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const DC::PlayerEdgeHangSettings* GetEdgeHangSettings()
{
	const DC::PlayerEdgeHangSettings* pHangSettings = ScriptManager::Lookup<DC::PlayerEdgeHangSettings>(SID("default-player-edge-hang-settings"));

	return pHangSettings;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Move points toward each other by an offset - Used to adjust edge end points to make room for hands/feet. Returns
// the distance between the points - If points are closer than the offset distance it will return 0.0f, exactly.
static F32 OffsetPoints(Point* pPos0, Point* pPos1)
{
	AI_ASSERT( pPos0 );
	AI_ASSERT( pPos1 );

	const F32 kEndPointOffsetDist = 0.2f;

	const F32 edgeLength = Dist(*pPos0, *pPos1);

	if (edgeLength < kEndPointOffsetDist * 2.0f)
	{
		const Point midPos = kOrigin + ((*pPos0 - kOrigin) + (*pPos1 - kOrigin)) / 2.0f;
		*pPos0 = midPos;
		*pPos1 = midPos;
		return 0.0f;
	}

	F32 tt = kEndPointOffsetDist / edgeLength;
	const Point offsetPos0 = Lerp(*pPos0, *pPos1, tt);

	tt = (edgeLength - kEndPointOffsetDist) / edgeLength;
	const Point offsetPos1 = Lerp(*pPos0, *pPos1, tt);

	*pPos0 = offsetPos0;
	*pPos1 = offsetPos1;

	const F32 length = Dist(offsetPos0, offsetPos1);

	return length;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool SphereBlocksMovingSphere(Sphere sphere, Sphere movingSphere, Vector moveWs)
{
	const Vector toSphereWs = sphere.GetCenter() - movingSphere.GetCenter();

	// Moving away
	if (Dot(moveWs, toSphereWs) < 0.0f)
	{
		return false;
	}

	const Sphere testSphere(sphere.GetCenter(), sphere.GetRadius() + movingSphere.GetRadius());
	const bool overlaps = testSphere.TestLineOverlap(movingSphere.GetCenter(), movingSphere.GetCenter() + moveWs);

// const TimeFrame debugDelay = Seconds(1.5f);
// g_prim.Draw(DebugSphere(sphere, kColorDarkGray), debugDelay);
// const Color color = overlaps ? kColorRed : kColorGreen;
// g_prim.Draw(DebugSphere(movingSphere, color), debugDelay);
// g_prim.Draw(DebugArrow(movingSphere.GetCenter(), moveWs, color), debugDelay);

	return overlaps;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static U32F GetPlayers(Player** outPlayers)
{
	U32F numPlayers = 0;

	// -- Gather players --

	if (g_netInfo.IsNetActive())
	{
		const U32F maxPlayers = NetGameManager::kMaxNetPlayerTrackers;
		AI_ASSERT( ARRAY_ELEMENT_COUNT(outPlayers) <= maxPlayers );

		for (U32F ii = 0; ii < maxPlayers; ++ii)
		{
			if (NetPlayerTracker *pTracker = g_netGameManager.GetNetPlayerTrackerById(ii))
			{
				if (const Player* pPlayer = Player::FromProcess(pTracker->m_characterHandle.ToProcess()))
				{
					outPlayers[numPlayers++] = pPlayer;
				}
			}
		}
	}
	else
	{
		const U32F maxPlayers = EngineComponents::GetGameInfo()->GetNumPlayers();
		AI_ASSERT( ARRAY_ELEMENT_COUNT(outPlayers) <= maxPlayers );

		for (U32F ii = 0; ii < maxPlayers; ++ii)
		{
			if (Player* pPlayer = GetGlobalPlayer(ii))
			{
				outPlayers[numPlayers++] = pPlayer;
			}
		}
	}

	return numPlayers;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// NavLedgeReference
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
class NavLedgeReference
{

public:

	NavLedgeReference()														{ SetLedge(nullptr); }
	NavLedgeReference(const NavLedgeReference& rhs);
	NavLedgeReference(const NavLedgeHandle hEdge)							{ SetLedge(hEdge); }

	NavLedgeReference& SetLedge(const NavLedgeHandle hEdge);

	const U32F GetId() const;

	const Point GetVert0() const;
	const Point GetVert1() const;
	const Vector GetEdge() const;
	const Vector GetTopNormal() const;

	const Vector GetWallNormal() const;
	const Vector GetWallBinormal() const;

	FeatureEdge::Flags GetFlags() const;
	const Point GetEdgeCenter() const;

	Quat GetGrabQuaternion() const;
	Quat GetGrabQuaternionOrientToEdge() const;

	Binding GetBinding() const;

	bool IsStamina() const;
	bool IsSlippery() const;
	bool IsUnsafe() const;
	bool IsForceHang() const;
	bool IsCorner(const NavLedgeReference& edge) const;

	F32 FindGap(const NavLedgeReference& edge) const;

	operator bool () const													{ return IsEdgeValid(); }
	NavLedgeReference& operator = (const NavLedgeReference& rhs);
	bool operator != (const NavLedgeReference& rhs) const;
	bool operator == (const NavLedgeReference& rhs) const;

	bool IsEdgeValid() const;

	bool IsGood(Vector_arg up = kUnitYAxis) const;
	bool IsGood(Vector_arg up, F32 minDotSide, F32 minDotFwd, F32 minDotBack) const;

	const NavLedgeGraph* GetLedgeGraph() const;
	const NavLedge* GetSrcEdge() const;

private:

	NavLedgeReference& SetLedgeUnsafe(const NavLedgeHandle hEdge);

	const Vector GetTopNormalUnsafe() const;

	FeatureEdge::Flags GetFlagsUnsafe() const;

	bool IsEdgeValidUnsafe() const;

	bool IsOrientationGood(Vector_arg topNormal, Vector_arg sideDir, Vector up, F32 minDotSide, F32 minDotFwd, F32 minDotBack) const;

	NavLedgeHandle m_hEdge;

	mutable NdRwAtomicLock64 m_edgeLock;
};

/// --------------------------------------------------------------------------------------------------------------- ///
NavLedgeReference::NavLedgeReference(const NavLedgeReference& rhs)
{
	AtomicLockJanitorWrite edgeWriteLock(&m_edgeLock, FILE_LINE_FUNC);

	SetLedgeUnsafe(rhs.m_hEdge);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLedgeReference& NavLedgeReference::SetLedge(const NavLedgeHandle hEdge)
{
	AtomicLockJanitorWrite edgeWriteLock(&m_edgeLock, FILE_LINE_FUNC);

	SetLedgeUnsafe(hEdge);

	return *this;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const U32F NavLedgeReference::GetId() const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	const NavLedge* pLedge = GetSrcEdge();
	AI_ASSERT( pLedge );

	return pLedge->GetId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point NavLedgeReference::GetVert0() const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	const NavLedge* pLedge = GetSrcEdge();
	AI_ASSERT( pLedge );
	const NavLedgeGraph* pGraph = pLedge->GetNavLedgeGraph();
	AI_ASSERT( pGraph );

	const Point posWs = pGraph->LocalToWorld(pLedge->GetVertex0Ls());

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point NavLedgeReference::GetVert1() const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	const NavLedge* pLedge = GetSrcEdge();
	AI_ASSERT( pLedge );
	const NavLedgeGraph* pGraph = pLedge->GetNavLedgeGraph();
	AI_ASSERT( pGraph );

	const Point posWs = pGraph->LocalToWorld(pLedge->GetVertex1Ls());

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector NavLedgeReference::GetEdge() const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	const NavLedge* pLedge = GetSrcEdge();
	AI_ASSERT( pLedge );
	const NavLedgeGraph* pGraph = pLedge->GetNavLedgeGraph();
	AI_ASSERT( pGraph );

	const Vector edgeLs = pLedge->GetVertex1Ls() - pLedge->GetVertex0Ls();
	const Vector edgeWs = pGraph->LocalToWorld(edgeLs);

	return edgeWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector NavLedgeReference::GetTopNormal() const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	return GetTopNormalUnsafe();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector NavLedgeReference::GetWallNormal() const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	const NavLedge* pLedge = GetSrcEdge();
	AI_ASSERT( pLedge );
	const NavLedgeGraph* pGraph = pLedge->GetNavLedgeGraph();
	AI_ASSERT( pGraph );

	const Vector normalWs = pGraph->LocalToWorld(pLedge->GetWallNormalLs());

	return normalWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector NavLedgeReference::GetWallBinormal() const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	const NavLedge* pLedge = GetSrcEdge();
	AI_ASSERT(pLedge);
	const NavLedgeGraph* pGraph = pLedge->GetNavLedgeGraph();
	AI_ASSERT(pGraph);

	const Vector biNormalWs = pGraph->LocalToWorld(pLedge->GetWallBinormalLs());

	return biNormalWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
FeatureEdge::Flags NavLedgeReference::GetFlags() const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	return GetFlagsUnsafe();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point NavLedgeReference::GetEdgeCenter() const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	const NavLedge* pLedge = GetSrcEdge();
	AI_ASSERT( pLedge );
	const NavLedgeGraph* pGraph = pLedge->GetNavLedgeGraph();
	AI_ASSERT( pGraph );

	const Point posWs = pGraph->LocalToWorld(pLedge->GetCenterLs());

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Quat NavLedgeReference::GetGrabQuaternion() const
{
	Vector upNormalWs(kUnitYAxis);

	if (GetFlags() & FeatureEdge::kFlagLadder)
	{
		upNormalWs = GetTopNormal();
	}

	const Vector leftWs    = GetVert0() - GetVert1();
	const Vector forwardWs = Cross(upNormalWs, leftWs);

	return QuatFromLookAt(-forwardWs, upNormalWs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Quat NavLedgeReference::GetGrabQuaternionOrientToEdge() const
{
	const Vector leftWs    = -GetEdge();
	const Vector forwardWs = Cross(kUnitYAxis, leftWs);
	const Vector upWs      = Cross(leftWs, forwardWs);

	return QuatFromLookAt(-forwardWs, upWs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Binding NavLedgeReference::GetBinding() const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	const NavLedge* pLedge = GetSrcEdge();
	AI_ASSERT( pLedge );
	const NavLedgeGraph* pGraph = pLedge->GetNavLedgeGraph();
	AI_ASSERT( pGraph );

	return pGraph->GetBoundFrame().GetBinding();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::IsStamina() const
{
	// Stamina is disabled
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::IsSlippery() const
{
	return false;//(GetFlags() & FeatureEdge::kFlagUnsafe) != 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::IsUnsafe() const
{
	return IsStamina() || IsSlippery();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::IsForceHang() const
{
	return false;//(GetFlags() & FeatureEdge::kFlagForceHang) != 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::IsCorner(const NavLedgeReference& edge) const
{
	const F32 kDotCornerDeg = Cos(DegreesToRadians(60.0f));

	const Vector facing     = GetLocalZ(GetGrabQuaternionOrientToEdge());
	const Vector edgeFacing = GetLocalZ(edge.GetGrabQuaternionOrientToEdge());
	const F32 edgeDot       = Dot(facing, edgeFacing);

	const bool isCorner = edgeDot <= kDotCornerDeg && edgeDot >= -kDotCornerDeg;

	return isCorner;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 NavLedgeReference::FindGap(const NavLedgeReference& edge) const
{
	Point posWs;
	Point edgePosWs;

	Point pos0 = GetVert0();
	Point pos1 = GetVert1();

	Point edgePos0 = edge.GetVert0();
	Point edgePos1 = edge.GetVert1();

	const Segment seg(pos0, pos1);
	const Segment edgeSeg(edgePos0, edgePos1);

	const Scalar gapDist = DistSegmentSegment(seg, edgeSeg, posWs, edgePosWs);

	return gapDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLedgeReference& NavLedgeReference::operator = (const NavLedgeReference& rhs)
{
	AtomicLockJanitorWrite edgeWriteLock(&m_edgeLock, FILE_LINE_FUNC);

	return SetLedgeUnsafe(rhs.m_hEdge);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::operator != (const NavLedgeReference& rhs) const
{
	AtomicLockJanitorRead edgeReadLock(&m_edgeLock, FILE_LINE_FUNC);

	return m_hEdge != rhs.m_hEdge;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::operator == (const NavLedgeReference& rhs) const
{
	AtomicLockJanitorRead edgeReadLock(&m_edgeLock, FILE_LINE_FUNC);

	return m_hEdge == rhs.m_hEdge;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::IsEdgeValid() const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);
	AtomicLockJanitorRead edgeReadLock(&m_edgeLock, FILE_LINE_FUNC);

	return IsEdgeValidUnsafe();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::IsGood(Vector_arg up) const
{
	return IsGood(up,
				  g_ndOptions.m_climbing2_edgeFallTiltSide,
				  g_ndOptions.m_climbing2_edgeFallTiltFwd,
				  g_ndOptions.m_climbing2_edgeFallTiltBack);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::IsGood(Vector_arg up, F32 minDotSide, F32 minDotFwd, F32 minDotBack) const
{
	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);
	AtomicLockJanitorRead edgeReadLock(&m_edgeLock, FILE_LINE_FUNC);

	if (!IsEdgeValidUnsafe())
	{
		return false;
	}

	if (GetFlagsUnsafe() & FeatureEdge::kFlagDisabled)
	{
		return false;
	}

	const Vector topNormalWs = GetTopNormalUnsafe();
	const Vector sideDirWs   = SafeNormalize(GetEdge(), kZero);

	return IsOrientationGood(topNormalWs, sideDirWs, up, minDotSide, minDotFwd, minDotBack);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavLedgeGraph* NavLedgeReference::GetLedgeGraph() const
{
	AtomicLockJanitorRead edgeReadLock(&m_edgeLock, FILE_LINE_FUNC);

	return m_hEdge.ToLedgeGraph();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavLedge* NavLedgeReference::GetSrcEdge() const
{
	AtomicLockJanitorRead edgeReadLock(&m_edgeLock, FILE_LINE_FUNC);

	return m_hEdge.ToLedge();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLedgeReference& NavLedgeReference::SetLedgeUnsafe(const NavLedgeHandle hEdge)
{
	m_hEdge = hEdge;

	return *this;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector NavLedgeReference::GetTopNormalUnsafe() const
{
	const NavLedge* pLedge = GetSrcEdge();
	AI_ASSERT( pLedge );
	const NavLedgeGraph* pGraph = pLedge->GetNavLedgeGraph();
	AI_ASSERT( pGraph );

	const Vector normalWs = pGraph->LocalToWorld(pLedge->GetWallBinormalLs());

	return normalWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
FeatureEdge::Flags NavLedgeReference::GetFlagsUnsafe() const
{
	const NavLedge* pLedge = GetSrcEdge();
	AI_ASSERT( pLedge );

	const FeatureEdge::Flags flags = pLedge->GetFeatureFlags();

	return flags;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::IsEdgeValidUnsafe() const
{
	return m_hEdge.IsValid();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeReference::IsOrientationGood(Vector_arg topNormal,
										  Vector_arg sideDir,
										  Vector up,
										  F32 minDotSide,
										  F32 minDotFwd,
										  F32 minDotBack) const
{
	return true;

//TODO(MB) Disable checking edge orientation. All edges are selected by designers and we were getting false negatives.
// 	Vector fwdNormal = Cross(sideDir, topNormal);
// 
// 	Vector sideDirFlattened = sideDir - up * Dot(up, sideDir);
// 	F32 dotSide = MinMax01(Length(sideDirFlattened));
// 
// 	if (minDotSide > dotSide)
// 	{
// 		return false;
// 	}
// 
// 	Vector fwdNormalFlattened = fwdNormal - up * Dot(up, fwdNormal);
// 	F32 dotFwdBack = MinMax01(Length(fwdNormalFlattened));
// 	if (Dot(up, topNormal) < 0.0f)
// 	{
// 		dotFwdBack = -dotFwdBack;
// 	}
// 
// 	if (fwdNormal.Y() < 0.0f)
// 	{
// 		// Tilted fwd
// 		if (minDotFwd > dotFwdBack)
// 		{
// 			return false;
// 		}
// 	}
// 	else
// 	{
// 		// Tilted back
// 		if (minDotBack > dotFwdBack)
// 		{
// 			return false;
// 		}
// 	}
// 
// 	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// ClimbCommand
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
class ClimbCommand
{

public:

	enum Status
	{
		kStatusAwaitingCommands,		// Idle essentially (no goal, no path, not navigating)
		kStatusCommandPending,			// Climb command pending (received but not processed)
		kStatusCommandInProgress,		// Climb command in progress
		kStatusCommandSucceeded,		// Climb command succeeded
		kStatusCommandFailed			// Climb command failed (we cannot move)
	};

	enum Type
	{
		kTypeInvalid,
		kTypeStopClimbing,
		kTypeStartClimbing
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	static const char* GetStatusName(Status status)
	{
		switch (status)
		{
		case kStatusAwaitingCommands:	return "Awaiting Commands";
		case kStatusCommandPending:		return "Command Pending";
		case kStatusCommandInProgress:	return "Command In Progress";
		case kStatusCommandSucceeded:	return "Command Succeeded";
		case kStatusCommandFailed:		return "Command Failed";
		}

		return "<unknown status>";
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static const char* GetTypeName(Type type)
	{
		switch (type)
		{
		case kTypeInvalid:			return "Invalid";
		case kTypeStopClimbing:		return "StopClimbing";
		case kTypeStartClimbing:	return "StartClimbing";
		}

		return "<unknown type>";
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	ClimbCommand()
	{
		Reset();
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	void Reset()
	{
		m_type = kTypeInvalid;
		m_pathWaypoints.Clear();
	}

	// Accessors
	Type GetType() const								{ return m_type; }
	const char* GetTypeName() const						{ return GetTypeName(GetType()); }
	const PathWaypointsEx& GetPathWaypoints() const		{ return m_pathWaypoints; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	void AsStopClimbing(const NavCharacter* pNavChar, const NavAnimStopParams& params)
	{
		Reset();
		m_type = kTypeStopClimbing;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	void AsStartClimbing(const NavCharacter* pNavChar, const NavAnimStartParams& params)
	{
		Reset();

		m_type = kTypeStartClimbing;

		if (params.m_pPathPs)
		{
			m_pathWaypoints.CopyFrom(params.m_pPathPs);
		}
		else
		{
			m_pathWaypoints.Clear();
		}
	}

private:

	Type			m_type;
	PathWaypointsEx	m_pathWaypoints;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// AiClimbController
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
class AiClimbController : public IAiClimbController
{
	typedef IAiClimbController ParentClass;

	/// --------------------------------------------------------------------------------------------------------------- ///
	class BaseState : public Fsm::TState<AiClimbController>
	{

	public:

		virtual void OnEnter() override;
		virtual void OnUpdate()										{ }
		virtual void RequestAnimations()							{ }
		virtual bool CanProcessCommand() const						{ return true; }

		virtual bool IsBusy() const									{ return false; }

		virtual void OnStartClimbing(const ClimbCommand& cmd);
		virtual void OnStopClimbing(const ClimbCommand& cmd);
		virtual void OnInterrupted();

		// For ClimbLog
			  NavCharacter* GetCharacter()							{ return GetSelf()->GetCharacter(); }
		const NavCharacter* GetCharacter() const					{ return GetSelf()->GetCharacter(); }
		U32 GetCommandId() const									{ return GetSelf()->GetCommandId(); }
		StringId64 GetAnimStateId() const							{ return GetSelf()->GetAnimStateId(); }
		const char* GetStateName(const char* nameIfNoState) const	{ return GetSelf()->GetStateName(nameIfNoState); }
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class ClimbControllerFsm : public Fsm::TStateMachine<BaseState>
	{
		// For ClimbLog
		const NavCharacter* GetCharacter() const	{ return GetSelf<const AiClimbController>()->GetCharacter(); }
		U32 GetCommandId() const					{ return GetSelf<const AiClimbController>()->GetCommandId(); }
		StringId64 GetAnimStateId() const			{ return GetSelf<const AiClimbController>()->GetAnimStateId(); }

		virtual void OnNewStateRequested(const Fsm::StateDescriptor& desc,
										 const void* pStateArg,
										 size_t argSize,
										 const char* srcFile,
										 U32 srcLine,
										 const char* srcFunc) const
		{
			ClimbLog("New state requested: %s\n", desc.GetStateName());
		}
	};

	FSM_BASE_DECLARE(ClimbControllerFsm);

	FSM_STATE_DECLARE(Interrupted);
	FSM_STATE_DECLARE(Transition);
	FSM_STATE_DECLARE(WaitForTransition);
	FSM_STATE_DECLARE(ClimbToReach);
	FSM_STATE_DECLARE(Shimmy);
	FSM_STATE_DECLARE(ClimbToWallShimmy);
	FSM_STATE_DECLARE(WallShimmyToClimb);
	FSM_STATE_DECLARE(Idle);

public:

	AiClimbController();

	// For Fsm
	virtual U32F GetMaxStateAllocSize();
	void GoInitialState();
	const Clock* GetClock() const;

	// From AnimActionController
	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	virtual void Reset() override;
	virtual void Interrupt() override;
	virtual void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;
	virtual bool IsBusy() const override;
	virtual U64 CollectHitReactionStateFlags() const override;

	// From INavAnimController
	virtual void Activate() override;
	virtual Point GetNavigationPosPs() const override;
	virtual void StartNavigating(const NavAnimStartParams& params) override;
	virtual void StopNavigating(const NavAnimStopParams& params) override;
	virtual bool CanProcessCommand() const override;
	virtual bool IsCommandInProgress() const override { return m_commandStatus == ClimbCommand::kStatusCommandInProgress; }

	virtual bool HasArrivedAtGoal() const override;
	virtual bool HasMissedWaypoint() const override;
	virtual NavGoalReachedType GetCurrentGoalReachedType() const override { return kNavGoalReachedTypeStop; }

	virtual bool IsMoving() const override;
	virtual bool IsInterrupted() const override;

	virtual void UpdatePathWaypointsPs(const PathWaypointsEx& pathWaypointsPs, const WaypointContract& waypointContract) override;
	virtual void PatchCurrentPathEnd(const NavLocation& updatedEndLoc) override;
	virtual void InvalidateCurrentPath() override;

	virtual void SetMoveToTypeContinuePoseMatchAnim(const StringId64 animId, F32 startPhase) override { }

	virtual bool ShouldDoProceduralApRef(const AnimStateInstance* pInstance) const override { return false; }
	virtual bool CalculateProceduralAlign(const AnimStateInstance* pInstance, const Locator& baseAlignPs, Locator& alignOut) const override { return false; }
	virtual bool CalculateProceduralApRef(AnimStateInstance* pInstance) const override { return false; }

	virtual bool ShouldAdjustHeadToNav() const override { return false; }
	virtual U32 GetDesiredLegRayCastMode() const override { return IsInWallShimmy() ? LegRaycaster::kModeDefault : LegRaycaster::kModeEdge; }

	virtual const char* GetDebugStatusStr() const override { return "(Climbing) "; }

	virtual void ForceDefaultIdleState(bool playAnim = true) override;
	virtual StateChangeRequest::ID FadeToIdleState(const DC::BlendParams* pBlend = nullptr) override;

	// From IAiClimbController
	virtual void RequestIdle() override;
	virtual void BlendForceDefaultIdle(bool blend) override { m_status.m_blendForcedDefaultIdle = blend; }
	virtual bool IsBlocked(Point_arg posWs, Point_arg nextPosWs, bool isSameEdge) const override;
	virtual bool IsInWallShimmy() const override { return m_status.m_isInWallShimmy; }
	virtual bool IsHanging() const override { return m_status.m_wantHang; }

	virtual bool GetIkTargetInfo(IkTargetInfo* pTargetInfoOut) const override;

private:

	struct Status
	{
		/// --------------------------------------------------------------------------------------------------------------- ///
		Status()
		: m_animId(INVALID_STRING_ID_64)
		, m_animIndex(-1)
		, m_flags(0)
		, m_properties(0)
		, m_allowIdleExitTime(TimeFrameNegInfinity())
		, m_shimmyExitEdgeIndex(kInvalidEdgeIndex)
		, m_mirrorReach(false)
		, m_mirrorShimmy(false)
		, m_mirrored(false)
		, m_allowPlayerClimb(false)
		, m_blendForcedDefaultIdle(false)
		, m_isTeleporting(false)
		, m_isIdling(false)
		, m_isInWallShimmy(false)
		, m_isBlocked(false)
		, m_wantInsideCorner(false)
		, m_wantOutsideCorner(false)
		, m_wantWallShimmy(false)
		, m_wantHang(false)
		, m_wantReachEnd(false)
		{
			SetSource(NavLedgeReference(), BoundFrame());
			SetTarget(NavLedgeReference(), BoundFrame());
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void SetSource(const NavLedgeReference edge, const BoundFrame& bf)
		{
			m_sourceEdgeHandBf[kLeftArm]  = bf;
			m_sourceEdgeHandBf[kRightArm] = bf;
			m_sourceEdgeHand[kLeftArm]    = edge;
			m_sourceEdgeHand[kRightArm]   = edge;

			m_sourceEdgeFootBf[kLeftLeg]  = bf;
			m_sourceEdgeFootBf[kRightLeg] = bf;
			m_sourceEdgeFoot[kLeftLeg]    = edge;
			m_sourceEdgeFoot[kRightLeg]   = edge;
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void SetTarget(const NavLedgeReference edge, const BoundFrame& bf)
		{
			m_targetEdgeHandBf[kLeftArm]  = bf;
			m_targetEdgeHandBf[kRightArm] = bf;
			m_targetEdgeHand[kLeftArm]    = edge;
			m_targetEdgeHand[kRightArm]   = edge;

			m_targetEdgeFootBf[kLeftLeg]  = bf;
			m_targetEdgeFootBf[kRightLeg] = bf;
			m_targetEdgeFoot[kLeftLeg]    = edge;
			m_targetEdgeFoot[kRightLeg]   = edge;
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void CopySourceToTarget()
		{
			m_targetEdgeHandBf[kLeftArm]  = m_sourceEdgeHandBf[kLeftArm];
			m_targetEdgeHandBf[kRightArm] = m_sourceEdgeHandBf[kRightArm];
			m_targetEdgeHand[kLeftArm]    = m_sourceEdgeHand[kLeftArm];
			m_targetEdgeHand[kRightArm]   = m_sourceEdgeHand[kRightArm];

			m_targetEdgeFootBf[kLeftLeg]  = m_sourceEdgeFootBf[kLeftLeg];
			m_targetEdgeFootBf[kRightLeg] = m_sourceEdgeFootBf[kRightLeg];
			m_targetEdgeFoot[kLeftLeg]    = m_sourceEdgeFoot[kLeftLeg];
			m_targetEdgeFoot[kRightLeg]   = m_sourceEdgeFoot[kRightLeg];
		}

		/// --------------------------------------------------------------------------------------------------------------- ///
		void CopyTargetToSource()
		{
			m_sourceEdgeHandBf[kLeftArm]  = m_targetEdgeHandBf[kLeftArm];
			m_sourceEdgeHandBf[kRightArm] = m_targetEdgeHandBf[kRightArm];
			m_sourceEdgeHand[kLeftArm]    = m_targetEdgeHand[kLeftArm];
			m_sourceEdgeHand[kRightArm]   = m_targetEdgeHand[kRightArm];

			m_sourceEdgeFootBf[kLeftLeg]  = m_targetEdgeFootBf[kLeftLeg];
			m_sourceEdgeFootBf[kRightLeg] = m_targetEdgeFootBf[kRightLeg];
			m_sourceEdgeFoot[kLeftLeg]    = m_targetEdgeFoot[kLeftLeg];
			m_sourceEdgeFoot[kRightLeg]   = m_targetEdgeFoot[kRightLeg];
		}

		StringId64 m_animId;								// Anim id
		I32        m_animIndex;								// Anim index
		U32        m_flags;									// Anim flags
		U32        m_properties;							// Anim properties

		BoundFrame        m_sourceEdgeHandBf[kArmCount];	// Source edge, per hand
		NavLedgeReference m_sourceEdgeHand[kArmCount];

		BoundFrame        m_targetEdgeHandBf[kArmCount];	// Target edge, per hand
		NavLedgeReference m_targetEdgeHand[kArmCount];

		BoundFrame        m_sourceEdgeFootBf[kLegCount];	// Source edge, per foot
		NavLedgeReference m_sourceEdgeFoot[kLegCount];

		BoundFrame        m_targetEdgeFootBf[kLegCount];	// Target edge, per foot
		NavLedgeReference m_targetEdgeFoot[kLegCount];

		AiClimbController::IkTargetInfo m_ikInfo;			// Ik for hands and feet

		TimeFrame m_allowIdleExitTime;						// Time when idle exit is allowed
		U32 m_shimmyExitEdgeIndex;							// Character is at the exit of a shimmy edge with this index

		bool m_mirrorReach            : 1;					// Anim is mirrored in reach
		bool m_mirrorShimmy           : 1;					// Anim is mirrored in shimmy
		bool m_mirrored               : 1;					// Anim is mirrored
		bool m_allowPlayerClimb       : 1;					// Allow player to climb on the character like an edge
		bool m_blendForcedDefaultIdle : 1;					// Blend when forcing default idle instead of teleporting

		bool m_isTeleporting          : 1;					// Character is teleporting
		bool m_isIdling               : 1;					// Character is idling
		bool m_isInWallShimmy         : 1;					// Character is in wall shimmy
		bool m_isBlocked              : 1;					// Character is blocked (for debug)

		bool m_wantInsideCorner       : 1;					// Want an inside corner transition
		bool m_wantOutsideCorner      : 1;					// Want an outside corner transition
// 		bool m_wantForward            : 1;					// Want a forward transition
		bool m_wantWallShimmy         : 1;					// Want a wall shimmy transition
		bool m_wantHang               : 1;					// Want a hang transition
		bool m_wantReachEnd           : 1;					// Want a reach end transition
	};

	struct AnimRating
	{
		AnimRating()
		: m_animIndex(-1)
		, m_mirror(false)
		, m_rating(kLargestFloat)
		{
			for (U32F ii = 0; ii < 2; ++ii)
			{
				m_handStartRatings[ii]        = 0.0f;
				m_handEndRatings[ii]          = 0.0f;
				m_handMovementRatings[ii]     = 0.0f;
				m_handMovementDirRatings[ii]  = 0.0f;

				m_footStartRatings[ii]        = 0.0f;
				m_footEndRatings[ii]          = 0.0f;
				m_footMovementRatings[ii]     = 0.0f;
				m_footMovementDirRatings[ii]  = 0.0f;
			}
		}

		I32  m_animIndex;
		bool m_mirror;
		F32  m_rating;

		F32 m_handStartRatings[2];
		F32 m_handEndRatings[2];
		F32 m_handMovementRatings[2];
		F32 m_handMovementDirRatings[2];

		F32 m_footStartRatings[2];
		F32 m_footEndRatings[2];
		F32 m_footMovementRatings[2];
		F32 m_footMovementDirRatings[2];
	};

	struct AnimParams
	{
		AnimParams()
		: m_animationId(INVALID_STRING_ID_64)
		, m_startPhase(0.0f)
		, m_customApRefId(INVALID_STRING_ID_64)
		, m_stateId(INVALID_STRING_ID_64)
		, m_finishCondition(AnimAction::kFinishOnAnimEnd)
		, m_isMirrored(false)
		, m_isBfValid(false)
		, m_isSelfBlendBfValid(false)
		{
			m_blend.m_curve     = DC::kAnimCurveTypeInvalid;
			m_selfBlend.m_curve = DC::kAnimCurveTypeInvalid;
		}

		StringId64                  m_animationId;
		F32							m_startPhase;
		BoundFrame                  m_boundFrame;
		BoundFrame                  m_selfBlendBoundFrame;
		StringId64                  m_customApRefId;
		StringId64                  m_stateId;
		AnimAction::FinishCondition m_finishCondition;
		DC::BlendParams             m_blend;
		DC::SelfBlendParams         m_selfBlend;

		bool m_isMirrored         : 1;
		bool m_isBfValid          : 1;
		bool m_isSelfBlendBfValid : 1;
	};

	enum EdgeStatus
	{
		kEdgeStatusClimb,
		kEdgeStatusDone,
		kEdgeStatusBlocked,
		kEdgeStatusFail
	};

private:

	void ResetInternal();

	StringId64 GetAnimStateId() const;

	virtual U32F IssueCommand(const ClimbCommand& cmd);
	virtual U32F GetCommandId() const						{ return m_commandId; }

	bool ShouldKillCommand(const ClimbCommand& cmd);
	virtual void ProcessCommand();

	void ProcessCommand_StartClimbing(const ClimbCommand& cmd);
	void ProcessCommand_StopClimbing(const ClimbCommand& cmd);

	void OnCommandSucceeded();
	void OnCommandFailed(const char* msg);

	void GoSelectIdle();
	void GoSelectTransition();

	bool ShouldShimmy(const NavLedgeReference& edge, const NavLedgeReference& nextEdge) const;
	bool ShouldReach(const BoundFrame& edgeBf, const BoundFrame& nextEdgeBf) const;
	bool WantWallShimmy(NavLedgeReference edge,
						const Locator& edgeLoc,
						NavLedgeReference nextEdge,
						const Locator& nextEdgeLoc,
						bool* pIsStateValid) const;

	bool FindStartPathIndices(const PathInfo& pathInfo, U32F outWaypointIndex[2]) const;
	bool FindTransitionPoints(NavLedgeReference* pOutSrcEdge,
							  BoundFrame* pOutSrcEdgeBf,
							  NavLedgeReference* pOutDstEdge,
							  BoundFrame* pOutDstEdgeBf) const;
	EdgeStatus GetNextEdge(NavLedgeReference* pOutEdge, BoundFrame* pOutEdgeBf) const;

	bool UpdatePathWaypointsInternal(const PathInfo& pathInfo);

	bool FindClosestPointOnEdge(Point posWs, BoundFrame* pOutEdgeBf, NavLedgeReference* pOutEdge) const;

	BoundFrame FindHandAnimBf() const;
	BoundFrame FindHandSourceBf() const;
	BoundFrame FindHandTargetBf() const;

	BoundFrame FindFootAnimBf() const;
	BoundFrame FindFootSourceBf() const;
	BoundFrame FindFootTargetBf() const;

	BoundFrame FindLimbAnimBf() const;
	BoundFrame FindLimbSourceBf() const;
	BoundFrame FindLimbTargetBf() const;

	bool AttachToClosestEdge(Point posWs, StringId64 exitModeId = INVALID_STRING_ID_64);

	void PrepareToClimb();

	bool IsLedgeHang(NavLedgeReference edge, const Locator& edgeLoc) const;
// 	bool IsLedgeForward(NavLedgeReference edge, NavLedgeReference nextEdge) const;

	bool GetScaleAdjustLs(const NdGameObject* pGo, Vector* pOutScaleAdjustLs) const;
	F32 GetWallSlopeAngle(NavLedgeReference edge, const Locator& edgeLoc);

	StringId64 GetBaseAnimStateId() const;
	bool FindBestTransition(Status* pStatus) const;
	void GetBlend(const DC::BlendParams* pBlend, F32 animDuration, FadeToStateParams* pOutParams) const;
	void PlayAnimation(const AnimParams& params);
	bool HasSelfBlendStarted(const AnimActionWithSelfBlend& animAction) const;
	void SetAnimSpeedScale(F32 scale);
	F32 GetAnimSpeedScale() const;
	void AdjustWallShimmyBf(BoundFrame* pBoundFrame, F32 distFromWall) const;
	void AdjustSlopeBf(BoundFrame* pBoundFrame, NavLedgeReference edge);

	bool AllowIdleEnter() const;
	bool AllowIdleExit() const;

	// Used by Transition/WaitForTransition states
	void UpdateTransitionArmIk(ArmIndex armIndex, F32 phase, F32 startPhase, F32 endPhase);
	void UpdateTransitionIkInfo();

private:

	PathInfo m_pendingPathInfo;
	PathInfo m_activePathInfo;

	Status m_status;

	AnimActionWithSelfBlend m_animAction;

	U32                  m_commandId;
	ClimbCommand::Status m_commandStatus;
	ClimbCommand         m_pendingCommand;
	ClimbCommand         m_activeCommand;

	bool m_activePending : 1;						// Controller is about to become active (i.e. leave interrupted)
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiClimbController* CreateAiClimbController()
{
	return NDI_NEW AiClimbController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// AiClimbController::BaseState
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::BaseState::OnStartClimbing(const ClimbCommand& cmd)
{
	AiClimbController* pSelf = GetSelf();

	NavCharacter* pNavChar = GetCharacter();

	const Locator& parentSpace       = pNavChar->GetParentSpace();
	const NavAnimHandoffDesc handoff = pNavChar->PopValidHandoff();
	const StringId64 exitModeId      = handoff.IsValid(pNavChar) ? handoff.m_exitModeId : INVALID_STRING_ID_64;

	// First try nav loc
	const Point posPs = pNavChar->GetNavControl()->GetNavLocation().GetPosPs();
	Point startPosWs  = parentSpace.TransformPoint(posPs);

	if (!pSelf->AttachToClosestEdge(startPosWs, exitModeId))
	{
		// Then try limb loc
		startPosWs = pSelf->FindLimbAnimBf().GetTranslation();

		if (!pSelf->AttachToClosestEdge(startPosWs, exitModeId))
		{
			NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

			const NavLedge* pLedge = cmd.GetPathWaypoints().GetNavLedgeHandle(0).ToLedge();
			if (!pLedge)
			{
				pSelf->OnCommandFailed("No valid path ledge found");
				return;
			}

			// Then try path edge loc
			startPosWs = NavLedgeReference(pLedge).GetEdgeCenter();

			if (!pSelf->AttachToClosestEdge(startPosWs, exitModeId))
			{
				pSelf->OnCommandFailed("Unable to attach to a ledge");
				return;
			}
		}

		pSelf->m_status.m_isTeleporting = true;
	}

	pSelf->m_status.m_isIdling = false;

	pSelf->PrepareToClimb();
	pSelf->GoSelectTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::BaseState::OnStopClimbing(const ClimbCommand& cmd)
{
	AiClimbController* pSelf = GetSelf();

	// Defer idle until it is available
	pSelf->m_status.m_isIdling = true;

	if (pSelf->IsInterrupted())
	{
		const Point startPosWs = pSelf->FindLimbAnimBf().GetTranslation();

		if (!pSelf->AttachToClosestEdge(startPosWs))
		{
			pSelf->OnCommandFailed("No valid edge found");
			return;
		}
		else
		{
			pSelf->GoSelectIdle();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// AiClimbController
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::BaseState::OnEnter()
{
	AiClimbController* pSelf = GetSelf();
	const Fsm::StateDescriptor* pPrevState = pSelf->GetStateMachine().GetPrevStateDescriptor();
	ClimbLog("Changing state [%s] -> [%s]\n", pPrevState ? pPrevState->GetStateName() : "<none>", m_pDesc->GetStateName());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::BaseState::OnInterrupted()
{
	AiClimbController* pSelf = GetSelf();

	pSelf->GoInterrupted();
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiClimbController::AiClimbController()
: m_commandId(0)
, m_activePending(false)
{
	ResetInternal();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::GoInitialState()
{
	GoInterrupted();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Clock* AiClimbController::GetClock() const
{
	return GetCharacterGameObject()->GetClock();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	ParentClass::Init(pNavChar, pNavControl);

	ResetInternal();

	GetStateMachine().Init(this, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// USE WITH CAUTION!	This stops attempting to move the actor and will not request any further animations.
//						It assumes the actor is already idle.
void AiClimbController::Reset()
{
	ClimbLogStr("Reset Climb\n");

	ResetInternal();

	if (!IsInterrupted())
	{
		GoInterrupted();
		GetStateMachine().TakeStateTransition();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// USE WITH CAUTION!	This stops attempting to move the actor and will not request any further animations.
//						It assumes the actor is already idle.
void AiClimbController::Interrupt()
{
	ClimbLogStr("Interrupt Climb\n");

	ResetInternal();

	if (BaseState* pState = GetState())
	{
		pState->OnInterrupted();
	}
	else
	{
		GoInterrupted();
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(offset_bytes, lowerBound, upperBound);

	GetStateMachine().Relocate(offset_bytes, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::RequestAnimations()
{
	PROFILE_AUTO( AI );

	NavCharacter* pNavChar     = GetCharacter();
	AnimControl* pAnimControl  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	if (pNavChar->IsDead())
	{
		return;
	}

	// Is the character done blending animations?
	if (!pBaseLayer->AreTransitionsPending())
	{
		PROFILE( AI, ClimbCtrl_ProcessTransitions );

		if (m_commandStatus == ClimbCommand::kStatusCommandPending && CanProcessCommand())
		{
			ProcessCommand();
		}

		GetStateMachine().TakeStateTransition();

		if (BaseState* pState = GetState())
		{
			pState->RequestAnimations();
		}

		GetStateMachine().TakeStateTransition();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::UpdateStatus()
{
	PROFILE_AUTO( AI );

	NdGameObject* pCharacter  = GetCharacterGameObject();
	AnimControl* pAnimControl = pCharacter->GetAnimControl();

	m_animAction.Update(pAnimControl);

	GetStateMachine().TakeStateTransition();

	if (BaseState* pState = GetState())
	{
		pState->OnUpdate();
	}

	GetStateMachine().TakeStateTransition();

	const Npc* pNpc = Npc::FromProcess(pCharacter);
	if (pNpc)
	{
		const NpcSnapshot* pNpcSS = GetProcessSnapshot<NpcSnapshot>(pNpc);
		pNpc->GetFactDict()->Set(SID("is-climbing?"), pNpcSS->m_isOnLedge);
	}
	
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	if (!g_navCharOptions.m_climb.m_display)
	{
		return;
	}

	if (!pPrinter)
	{
		return;
	}

	ScreenSpaceTextPrinter& printer = *pPrinter;

	if (g_navCharOptions.m_climb.m_displayDetails && !IsInterrupted())
	{
		const NdGameObject* pCharacter   = GetCharacterGameObject();
		const bool allowIdleEnter        = AllowIdleEnter();
		const bool allowIdleExit         = AllowIdleExit();
		const NavLedgeReference edge     = m_status.m_isInWallShimmy ?
										   m_status.m_sourceEdgeFoot[kLeftLeg] :
										   m_status.m_sourceEdgeHand[kLeftArm];
		const NavLedgeReference nextEdge = m_status.m_isInWallShimmy ?
										   m_status.m_targetEdgeFoot[kLeftLeg] :
										   m_status.m_targetEdgeHand[kLeftArm];
		StringBuilder<16> shimmyExitEdgeIndex = m_status.m_shimmyExitEdgeIndex == kInvalidEdgeIndex ? StringBuilder<16>("<none>") : StringBuilder<16>("%u", m_status.m_shimmyExitEdgeIndex);

		printer.PrintText(kColorGray, "  Want Reach End:      %s", m_status.m_wantReachEnd      ? "true" : "false");
		printer.PrintText(kColorGray, "  Want Hang:           %s", m_status.m_wantHang          ? "true" : "false");
		printer.PrintText(kColorGray, "  Want Wall Shimmy:    %s", m_status.m_wantWallShimmy    ? "true" : "false");
		printer.PrintText(kColorGray, "  Want Outside Corner: %s", m_status.m_wantOutsideCorner ? "true" : "false");
		printer.PrintText(kColorGray, "  Want Inside Corner:  %s", m_status.m_wantInsideCorner  ? "true" : "false");
// 		printer.PrintText(kColorGray, "  Want Forward:        %s", m_status.m_wantForward       ? "true" : "false");
		printer.PrintText(kColorGray, "  In Wall Shimmy:      %s", m_status.m_isInWallShimmy    ? "true" : "false");
		printer.PrintText(kColorGray, "  Idling:              %s", m_status.m_isIdling          ? "true" : "false");
		printer.PrintText(kColorGray, "  Teleporting:         %s", m_status.m_isTeleporting     ? "true" : "false");
		printer.PrintText(kColorGray, "  Allow Player Climb:  %s", m_status.m_allowPlayerClimb  ? "true" : "false");
		printer.PrintText(kColorGray, "  Mirror Reach:        %s", m_status.m_mirrorReach       ? "true" : "false");
		printer.PrintText(kColorGray, "  Mirror Shimmy:       %s", m_status.m_mirrorShimmy      ? "true" : "false");
		printer.PrintText(kColorGray, "  Mirrored:            %s", m_status.m_mirrored          ? "true" : "false");
		printer.PrintText(kColorGray, "  Allow Idle Exit:     %s", allowIdleExit                ? "true" : "false");
		printer.PrintText(kColorGray, "  Allow Idle Enter:    %s", allowIdleEnter               ? "true" : "false");
		printer.PrintText(kColorGray, "  Shimmy Exit Edge:    %s", shimmyExitEdgeIndex.c_str());
		printer.PrintText(kColorGray, "  Target Edge Id:      %u", nextEdge.GetId());
		printer.PrintText(kColorGray, "  Source Edge Id:      %u", edge.GetId());
	}

	printer.PrintTextNoCr(kColorWhite, "Climb Controller (%s)", GetStateName("<none>"));

	if (m_status.m_isBlocked)
	{
		printer.PrintText(kColorRed, " <Blocked>");
	}
	else
	{
		printer.PrintText(kColorWhite, "");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::IsBusy() const
{
	bool isBusy = !m_animAction.IsDone();

	if (const BaseState* pState = GetState())
	{
		isBusy = isBusy || pState->IsBusy();
	}

	return isBusy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 AiClimbController::CollectHitReactionStateFlags() const
{
	const NavCharacter* pNavChar = GetCharacter();

	if (IsInterrupted())
	{
		return 0;
	}

	U64 state = 0;

	if (pNavChar && pNavChar->IsDead())
	{
		state |= DC::kHitReactionStateMaskClimbing;
	}
	else
	{
		state = DC::kHitReactionStateMaskAdditiveOnly;
	}

	return state;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::CanProcessCommand() const
{
	bool canProcessCommand = false;

	if (const BaseState* pState = GetState())
	{
		canProcessCommand = pState->CanProcessCommand();
	}

	return canProcessCommand;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Activate()
{
	m_activePending = IsInterrupted();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point AiClimbController::GetNavigationPosPs() const
{
	const NavCharacter* pNavChar = GetCharacter();

	Point posPs = pNavChar ? pNavChar->GetTranslationPs() : Point(kZero);

	if (!m_activePending && IsInterrupted())
	{
		return posPs;
	}

	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	const NavControl* pNavControl = pNavChar ? pNavChar->GetNavControl() : nullptr;

	const bool isTargetGood = m_status.m_isInWallShimmy ?
							  (m_status.m_targetEdgeFoot[kLeftLeg].IsGood() && m_status.m_targetEdgeFoot[kRightLeg].IsGood()) :
							  (m_status.m_targetEdgeHand[kLeftArm].IsGood() && m_status.m_targetEdgeHand[kRightArm].IsGood());
	const bool isSourceGood = m_status.m_isInWallShimmy ?
							  (m_status.m_sourceEdgeFoot[kLeftLeg].IsGood() && m_status.m_sourceEdgeFoot[kRightLeg].IsGood()) :
							  (m_status.m_sourceEdgeHand[kLeftArm].IsGood() && m_status.m_sourceEdgeHand[kRightArm].IsGood());
	const bool isGood = IsState(kStateWaitForTransition) ? isTargetGood : isSourceGood;

	// Current edge is good
	if (isGood)
	{
		posPs = IsState(kStateWaitForTransition)      ?
				FindLimbTargetBf().GetTranslationPs() :
				FindLimbSourceBf().GetTranslationPs();
	}
	// Teleport (or fall back to known nav location)
	else if (pNavControl && pNavControl->GetNavLocation().IsValid())
	{
		posPs = pNavControl->GetNavLocation().GetPosPs();
	}
	// Spawn
	else
	{
		posPs = FindLimbAnimBf().GetTranslationPs();
	}

	return posPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::ForceDefaultIdleState(bool playAnimation /*= true*/)
{
	const NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
	{
		return;
	}

	// Preserve blendForcedDefaultIdle through Reset
	const bool blendForcedDefaultIdle = m_status.m_blendForcedDefaultIdle;
	Reset();
	m_status.m_blendForcedDefaultIdle = blendForcedDefaultIdle;

	const Locator& parentSpace = pNavChar->GetParentSpace();

	// First try nav loc
	Point posPs = pNavChar->GetNavControl()->GetNavLocation().GetPosPs();
	Point posWs = parentSpace.TransformPoint(posPs);

	bool success = AttachToClosestEdge(posWs);
	if (!success)
	{
		// Then try hands
		posPs = FindHandAnimBf().GetTranslation();
		posWs = parentSpace.TransformPoint(posPs);

		success = AttachToClosestEdge(posWs);
		if (!success)
		{
			// Then try feet
			posPs = FindFootAnimBf().GetTranslation();
			posWs = parentSpace.TransformPoint(posPs);

			success = AttachToClosestEdge(posWs, SID("edge-standing-shimmy"));
		}
	}

	if (success)
	{
		m_status.m_isTeleporting = !m_status.m_blendForcedDefaultIdle;

		PrepareToClimb();
		GoSelectIdle();
	}
	else
	{
		ClimbLogStr("ForceDefaultIdleState: Unable to attach to edge - Did an IGC end too far from a ledge?\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StateChangeRequest::ID AiClimbController::FadeToIdleState(const DC::BlendParams* pBlend /* = nullptr */)
{
	AI_ASSERT(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::RequestIdle()
{
	const NavCharacter* pNavChar = GetCharacter();

	m_status.m_isIdling = true;

	if (m_status.m_allowIdleExitTime - pNavChar->GetCurTime() < kRequestIdleDelay)
	{
		m_status.m_allowIdleExitTime = pNavChar->GetCurTime() + kRequestIdleDelay;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::GetIkTargetInfo(IkTargetInfo* pTargetInfoOut) const
{
	if (!pTargetInfoOut)
	{
		return false;
	}

	//*pTargetInfoOut = m_status.m_ikInfo;

	for (U32F iArm = 0; iArm < kArmCount; ++iArm)
	{
		pTargetInfoOut->m_handEdgeVert0Ws[iArm] = m_status.m_ikInfo.m_handEdgeVert0Ws[iArm];
		pTargetInfoOut->m_handEdgeVert1Ws[iArm] = m_status.m_ikInfo.m_handEdgeVert1Ws[iArm];
		pTargetInfoOut->m_handEdgeWallNormalWs[iArm] = m_status.m_ikInfo.m_handEdgeWallNormalWs[iArm];
		pTargetInfoOut->m_handEdgeWallBinormalWs[iArm] = m_status.m_ikInfo.m_handEdgeWallBinormalWs[iArm];
		pTargetInfoOut->m_handIkStrength[iArm] = m_status.m_ikInfo.m_handIkStrength[iArm];
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::IsBlocked(Point_arg posWs, Point_arg nextPosWs, bool isSameEdge) const
{
	PROFILE_AUTO(AI);

	const F32 kLimbRadius      = 0.2f;
	const F32 kBodyRadiusScale = 0.5f;

	// -- Gather players --

	ScopedTempAllocator sta(FILE_LINE_FUNC);
	Player** players = NDI_NEW Player*[NetGameManager::kMaxNetPlayerTrackers];	

	const U32F numPlayers = GetPlayers(players);

	// -- Check if players block edge --

	Vector edgeToNextWs = nextPosWs - posWs;
	if (!IsMoving())
	{
		const F32 kIdleHighWaterMark = 0.25f;
		const Vector edgeToNextDirWs = SafeNormalize(edgeToNextWs, kZero);
		const Point adustedNextPosWs = nextPosWs + edgeToNextDirWs * kIdleHighWaterMark;

		edgeToNextWs = adustedNextPosWs - posWs;
	}

	const Vector moveWs       = isSameEdge ? SafeNormalize(edgeToNextWs, kZero) * kShimmyDistMax : edgeToNextWs;
	const F32 bodyRadiusScale = isSameEdge ? 1.0f : kBodyRadiusScale;

	const NavCharacter* pNavChar = GetCharacter();
	Sphere bodySphere(pNavChar->GetBoundingSphere().GetCenter(), pNavChar->GetBoundingSphere().GetRadius() * bodyRadiusScale);
	const Sphere limbSphere(posWs, kLimbRadius);

	Locator playerLocWs[2];


	// -- Check if npcs block edge --

	// Always use reduced body radius against npcs
	bodySphere.SetRadius(pNavChar->GetBoundingSphere().GetRadius() * kBodyRadiusScale);

	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	AtomicLockJanitorRead_Jls npcGroupLock(EngineComponents::GetNpcManager()->GetNpcGroupLock(), FILE_LINE_FUNC);

	for (NpcHandle hNpc : EngineComponents::GetNpcManager()->GetNpcs())
	{
		const Npc* pNpc	   = hNpc.ToProcess();
		if (!pNpc || pNpc->IsDead() || pNpc == pNavChar)
		{
			continue;
		}

		const NavLedgeHandle hNavLedge = pNpc->GetNavLocation().GetNavLedgeHandle();
		if (!hNavLedge.IsValid())
		{
			continue;
		}

		const Sphere npcBodySphere(pNpc->GetBoundingSphere().GetCenter(), pNpc->GetBoundingSphere().GetRadius() * kBodyRadiusScale);

		if (SphereBlocksMovingSphere(npcBodySphere, bodySphere, moveWs))
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::StartNavigating(const NavAnimStartParams& params)
{
	const NavCharacter* pNavChar = GetCharacter();

	ClimbCommand cmd;
	cmd.AsStartClimbing(pNavChar, params);

	IssueCommand(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::StopNavigating(const NavAnimStopParams& params)
{
	const NavCharacter* pNavChar = GetCharacter();

	ClimbCommand cmd;
	cmd.AsStopClimbing(pNavChar, params);

	IssueCommand(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::HasArrivedAtGoal() const
{
	return m_commandStatus == ClimbCommand::kStatusCommandSucceeded;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::HasMissedWaypoint() const
{
	return m_commandStatus == ClimbCommand::kStatusCommandFailed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::IsMoving() const
{
	return !m_status.m_isIdling;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::IsInterrupted() const
{
	return IsState(kStateInterrupted);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::InvalidateCurrentPath()
{
	m_activePathInfo.m_valid = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::UpdatePathWaypointsPs(const PathWaypointsEx& pathWaypointsPs, const WaypointContract& waypointContract)
{
	if (pathWaypointsPs.IsValid() && !IsInterrupted())
	{
		const Point endWaypointPs = pathWaypointsPs.GetEndWaypoint();
		const Point startPosWs    = FindLimbSourceBf().GetTranslationWs();
		const F32 toEndDistSqr    = DistSqr(startPosWs, endWaypointPs);
		const bool isPathToAnAp   = GetCharacter()->GetReservedActionPack();
		
		// Ignore paths below the minimum movement distance except to action packs (which may be right where we are)
		m_pendingPathInfo.m_waypointsPs = pathWaypointsPs;
		m_pendingPathInfo.m_valid       = toEndDistSqr >= Sqr(kShimmyDistMax) || isPathToAnAp;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::PatchCurrentPathEnd(const NavLocation& updatedEndLoc)
{
	// Don't listen to information unless we are actually processing a command
	if (m_commandStatus != ClimbCommand::kStatusCommandInProgress)
	{
		return;
	}

	m_pendingPathInfo.m_waypointsPs.UpdateEndPoint(updatedEndLoc.GetPosPs());
	m_activePathInfo.m_waypointsPs.UpdateEndPoint(updatedEndLoc.GetPosPs());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::ResetInternal()
{
	m_pendingPathInfo.Reset();
	m_activePathInfo.Reset();

	m_status = Status();

	m_animAction.Reset();

	m_commandStatus = ClimbCommand::kStatusAwaitingCommands;

	m_pendingCommand.Reset();
	m_activeCommand.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiClimbController::GetAnimStateId() const
{
	STRIP_IN_FINAL_BUILD_VALUE(INVALID_STRING_ID_64);

	const NavCharacter* pNavChar     = GetCharacter();
	const AnimControl* pAnimControl  = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const StringId64 stateId         = pBaseLayer ? pBaseLayer->CurrentStateId() : INVALID_STRING_ID_64;

	return stateId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AiClimbController::IssueCommand(const ClimbCommand& cmd)
{
	AI_ASSERT( cmd.GetType() != ClimbCommand::kTypeInvalid );

	++m_commandId;
	m_pendingCommand = cmd;
	m_commandStatus  = ClimbCommand::kStatusCommandPending;

	ClimbLog("Issued: %s\n", cmd.GetTypeName());

	return m_commandId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::ShouldKillCommand(const ClimbCommand& cmd)
{
	if (m_commandStatus != ClimbCommand::kStatusCommandInProgress)
	{
		return false;
	}

	if (m_activeCommand.GetType() != cmd.GetType())
	{
		return false;
	}

	if (cmd.GetPathWaypoints().GetWaypointCount() != 2)
	{
		return false;
	}

	if (m_activePathInfo.m_waypointsPs.GetWaypointCount() != 2)
	{
		return false;
	}

	const Point desGoalPs = cmd.GetPathWaypoints().GetWaypoint(1);
	const Point curGoalPs = m_activePathInfo.m_waypointsPs.GetWaypoint(1);

	if (DistSqr(desGoalPs, curGoalPs) > 0.1f)
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::ProcessCommand()
{
	switch (m_pendingCommand.GetType())
	{
	case ClimbCommand::kTypeStopClimbing:
		ProcessCommand_StopClimbing(m_pendingCommand);
		break;

	case ClimbCommand::kTypeStartClimbing:
		if (ShouldKillCommand(m_pendingCommand))
		{
			ClimbLogStr("Killing pending command");
		}
		else
		{
			ProcessCommand_StartClimbing(m_pendingCommand);
		}
		break;

	default:
		AI_ASSERT( false );
		break;
	}

	// Respect m_commandStatus if processing a command changed it
	if (m_commandStatus == ClimbCommand::kStatusCommandPending)
	{
		m_commandStatus = ClimbCommand::kStatusCommandInProgress;
		m_activeCommand = m_pendingCommand;
	}

	m_pendingCommand.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::ProcessCommand_StartClimbing(const ClimbCommand& cmd)
{
	PathInfo pathInfo;
	pathInfo.m_waypointsPs = cmd.GetPathWaypoints();
	pathInfo.m_valid       = pathInfo.m_waypointsPs.IsValid();
	AI_ASSERT( pathInfo.m_valid );

	if (UpdatePathWaypointsInternal(pathInfo))
	{
		m_pendingPathInfo.Reset();
	}

	AI_ASSERT( m_activePathInfo.m_valid );

	if (BaseState* pState = GetState())
	{
		pState->OnStartClimbing(cmd);
	}

	ClimbLogStr("Processed: StartClimbing\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::ProcessCommand_StopClimbing(const ClimbCommand& cmd)
{
	if (BaseState* pState = GetState())
	{
		pState->OnStopClimbing(cmd);
	}

	ClimbLogStr("Processed: StopClimbing\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::OnCommandSucceeded()
{
	ClimbLog("%s: %s command succeeded\n", GetCharacterGameObject()->GetName(), m_activeCommand.GetTypeName(m_activeCommand.GetType()));

	m_commandStatus = ClimbCommand::kStatusCommandSucceeded;
	m_activePathInfo.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::OnCommandFailed(const char* msg)
{
	ClimbLog("%s: %s command failed - %s\n", GetCharacterGameObject()->GetName(), m_activeCommand.GetTypeName(m_activeCommand.GetType()), msg);

	m_commandStatus = ClimbCommand::kStatusCommandFailed;
	m_activePathInfo.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::GoSelectIdle()
{
	AI_ASSERTF( AllowIdleEnter(), ("Attempt to idle when not allowed") );

	// May need to transition to the correct wall shimmy state when starting from a tap, teleport or cinematic
	if (m_status.m_wantWallShimmy != m_status.m_isInWallShimmy)
	{
		if (m_status.m_isInWallShimmy)
		{
			GoWallShimmyToClimb();
		}
		else
		{
			GoClimbToWallShimmy();
		}
	}
	else
	{
		GoIdle();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::GoSelectTransition()
{
	if (UpdatePathWaypointsInternal(m_pendingPathInfo))
	{
		m_pendingPathInfo.Reset();
	}

	if (!m_status.m_wantReachEnd)	// Set in ClimbToReach
	{
		m_status.CopyTargetToSource();
	}

	NavLedgeReference nextEdge;
	BoundFrame nextEdgeBf;

	const EdgeStatus edgeStatus = GetNextEdge(&nextEdge, &nextEdgeBf);

	if (AllowIdleEnter() && edgeStatus == kEdgeStatusDone)
	{
		OnCommandSucceeded();
		GoSelectIdle();
		return;
	}
	else if (edgeStatus == kEdgeStatusFail)
	{
		OnCommandFailed("Path waypoint has no edge");
		ForceDefaultIdleState();
		return;
	}
	else if (AllowIdleEnter() && (edgeStatus == kEdgeStatusBlocked || !IsClimbing()))
	{
		GoSelectIdle();
		return;
	}

	const NavLedgeReference edge = m_status.m_isInWallShimmy ? m_status.m_sourceEdgeFoot[kLeftLeg]   : m_status.m_sourceEdgeHand[kLeftArm];
	const BoundFrame edgeBf      = m_status.m_isInWallShimmy ? m_status.m_sourceEdgeFootBf[kLeftLeg] : m_status.m_sourceEdgeHandBf[kLeftArm];

	bool isWallShimmyStateValid = true;
	bool shouldShimmy           = false;
	bool shouldReach            = false;

	m_status.m_wantInsideCorner  = false;
	m_status.m_wantOutsideCorner = false;
// 	m_status.m_wantForward       = false;
	m_status.m_wantWallShimmy    = false;
	m_status.m_wantHang          = false;

	if (!m_status.m_wantReachEnd)	// Set in ClimbToReach
	{
// 		m_status.m_wantForward    = IsLedgeForward(edge, nextEdge);
		m_status.m_wantWallShimmy = WantWallShimmy(edge, edgeBf.GetLocator(), nextEdge, nextEdgeBf.GetLocator(), &isWallShimmyStateValid);
		m_status.m_wantHang       = IsLedgeHang(nextEdge, nextEdgeBf.GetLocator());
	}

	if (edge.IsCorner(nextEdge) && edge.FindGap(nextEdge) < kReachDistMax)
	{
		const Locator edgeLoc(edge.GetEdgeCenter(), edge.GetGrabQuaternionOrientToEdge());
		const Point nextPosLs = edgeLoc.UntransformPoint(nextEdge.GetEdgeCenter());

		m_status.m_wantOutsideCorner = nextPosLs.Z() > 0.0f;
		m_status.m_wantInsideCorner  = !m_status.m_wantOutsideCorner;
	}
	else if (!m_status.m_wantReachEnd)
	{
		shouldShimmy = ShouldShimmy(edge, nextEdge);
		shouldReach  = ShouldReach(edgeBf, nextEdgeBf);
	}

	// Update target edge
	if (!m_status.m_wantReachEnd)
	{
		m_status.SetTarget(nextEdge, nextEdgeBf);
	}

	// May need to change wall shimmy state before transitioning to the next edge
	if (isWallShimmyStateValid)
	{
		if (shouldShimmy)
		{
			GoShimmy();
		}
		else if (shouldReach)
		{
			GoClimbToReach();
		}
		else
		{
			GoTransition();
		}
	}
	else
	{
		if (m_status.m_isInWallShimmy)
		{
			GoWallShimmyToClimb();
		}
		else
		{
			GoClimbToWallShimmy();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::ShouldShimmy(const NavLedgeReference& edge, const NavLedgeReference& nextEdge) const
{
	const F32 kMaxDistBetweenShimmyEdges = 0.25f;

	if (m_status.m_sourceEdgeHand[kLeftArm] != m_status.m_sourceEdgeHand[kRightArm])
	{
		return false;
	}

	if (edge == nextEdge)
	{
		return true;
	}

	//NB: Assumes edges within shimmy distance are closest at their end points, and that the angle is sufficiently flat
	const F32 edgeGap       = edge.FindGap(nextEdge);
	const bool shouldShimmy = edgeGap < kMaxDistBetweenShimmyEdges;

	return shouldShimmy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::ShouldReach(const BoundFrame& edgeBf, const BoundFrame& nextEdgeBf) const
{
	bool shouldReach = false;

	if (!m_status.m_isInWallShimmy && m_status.m_sourceEdgeHand[kLeftArm] == m_status.m_sourceEdgeHand[kRightArm])
	{
		const F32 toNextEdgeDistSqr = DistSqr(edgeBf.GetTranslation(), nextEdgeBf.GetTranslation());
		shouldReach = toNextEdgeDistSqr < Sqr(kReachDistMax);
	}

	return shouldReach;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::WantWallShimmy(NavLedgeReference edge,
									   const Locator& edgeLoc,
									   NavLedgeReference nextEdge,
									   const Locator& nextEdgeLoc,
									   bool* pIsStateValid) const
{
	AI_ASSERT( pIsStateValid );

	const F32 kMinWallShimmyTransitionHeight   = 1.5f;
	const F32 kMinWallShimmyToWallShimmyHeight = 0.51f;

	const bool isWallShimmy     = false;//edge.GetFlags() & FeatureEdge::kFlagStandingShimmy;
	const bool isNextWallShimmy = false;//nextEdge.GetFlags() & FeatureEdge::kFlagStandingShimmy;

	const F32 deltaY = edgeLoc.Pos().Y() - nextEdgeLoc.Pos().Y();

	bool wantWallShimmy = isNextWallShimmy;

	*pIsStateValid = true;

	// When moving along a ledge, the wall shimmy state is valid if our wall shimmy state matches the next ledge
	if (edge == nextEdge)
	{
		*pIsStateValid = m_status.m_isInWallShimmy == isNextWallShimmy;
	}
	// When moving down, the wall shimmy state is valid if we aren't in wall shimmy
	else if (deltaY > kMinWallShimmyTransitionHeight)
	{
		*pIsStateValid = !m_status.m_isInWallShimmy;
	}
	// When moving up, the wall shimmy state is valid if we don't need to be in wall shimmy,
	// or if our wall shimmy state matches the next ledge
	else if (-deltaY > kMinWallShimmyTransitionHeight)
	{
		*pIsStateValid = !isWallShimmy || m_status.m_isInWallShimmy;
	}
	// When moving across, the wall shimmy state is valid if we don't need to be in wall shimmy,
	// or if our wall shimmy state matches the next ledge
	else if (Abs(deltaY) < kMinWallShimmyToWallShimmyHeight)
	{
		*pIsStateValid = !isWallShimmy || (m_status.m_isInWallShimmy == isNextWallShimmy);

		// If our wall shimmy is valid then stay in that state
		if (*pIsStateValid)
		{
			wantWallShimmy = m_status.m_isInWallShimmy;
		}
	}

	return wantWallShimmy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::FindStartPathIndices(const PathInfo& pathInfo, U32F outWaypointIndex[2]) const
{
	const NavLedgeReference* srcEdge = m_status.m_isInWallShimmy ? m_status.m_sourceEdgeFoot : m_status.m_sourceEdgeHand;

	const U32F waypointCount = pathInfo.m_waypointsPs.GetWaypointCount();

	outWaypointIndex[0] = waypointCount;
	outWaypointIndex[1] = waypointCount;

	// Find the index of the first path waypoint on edges for each limb (unique waypoints for each edge)
	for (U32F ii = 0; ii < waypointCount; ++ii)
	{
		const NavLedgeReference edge(pathInfo.m_waypointsPs.GetNavLedgeHandle(ii));

		if (edge == srcEdge[0] && outWaypointIndex[0] == waypointCount)
		{
			outWaypointIndex[0] = ii;

			if (outWaypointIndex[1] < waypointCount)
			{
				break;
			}
		}

		if (edge == srcEdge[1] && outWaypointIndex[1] == waypointCount)
		{
			outWaypointIndex[1] = ii;

			if (outWaypointIndex[0] < waypointCount)
			{
				break;
			}
		}
	}

	const bool edgeNotOnPath   = outWaypointIndex[0] == waypointCount || outWaypointIndex[1] == waypointCount;
	const bool edgesNotOnPath  = edgeNotOnPath && outWaypointIndex[0] == outWaypointIndex[1];
	const bool limbsOnSameEdge = outWaypointIndex[0] == outWaypointIndex[1];

	const bool valid = !(edgesNotOnPath || (edgeNotOnPath && limbsOnSameEdge));

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Current and next edges with exit/enter bound frames
bool AiClimbController::FindTransitionPoints(NavLedgeReference* pOutSrcEdge,
											 BoundFrame* pOutSrcEdgeBf,
											 NavLedgeReference* pOutDstEdge,
											 BoundFrame* pOutDstEdgeBf) const
{
	AI_ASSERT( pOutSrcEdgeBf );
	AI_ASSERT( pOutDstEdgeBf );
	AI_ASSERT( pOutSrcEdge );
	AI_ASSERT( pOutDstEdge );

	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	const BoundFrame* srcEdgeBf      = m_status.m_isInWallShimmy ? m_status.m_sourceEdgeFootBf : m_status.m_sourceEdgeHandBf;
	const NavLedgeReference* srcEdge = m_status.m_isInWallShimmy ? m_status.m_sourceEdgeFoot   : m_status.m_sourceEdgeHand;

	if (!m_activePathInfo.m_valid || !m_activePathInfo.m_waypointsPs.IsValid())
	{
		*pOutSrcEdgeBf = srcEdgeBf[0];
		*pOutSrcEdge   = srcEdge[0];
		*pOutDstEdgeBf = srcEdgeBf[0];
		*pOutDstEdge   = srcEdge[0];

		return true;
	}

	const U32F waypointCount = m_activePathInfo.m_waypointsPs.GetWaypointCount();
	U32F waypointIndex[2]    = { waypointCount, waypointCount };

	if (!FindStartPathIndices(m_activePathInfo, waypointIndex))
	{
		// Assumes first waypoint on the path is near the source limb position
		AI_ASSERT( Dist(srcEdgeBf[0].GetTranslation(), m_activePathInfo.m_waypointsPs.GetAsNavLocation(0).GetPosWs()) < 0.5f );

		waypointIndex[0] = 0;
		waypointIndex[1] = 0;
	}

	const bool edgeNotOnPath = waypointIndex[0] == waypointCount || waypointIndex[1] == waypointCount;

	// If limbs are on different edges return the edge furthest along the path
	if (waypointIndex[0] != waypointIndex[1])
	{
		const U32F leadIndex  = edgeNotOnPath ?
								waypointIndex[0] == waypointCount   ? 1 : 0 :
								waypointIndex[0] > waypointIndex[1] ? 0 : 1;
		const U32F trailIndex = 1 - leadIndex;

		*pOutSrcEdgeBf = srcEdgeBf[trailIndex];
		*pOutSrcEdge   = srcEdge[trailIndex];
		*pOutDstEdgeBf = srcEdgeBf[leadIndex];
		*pOutDstEdge   = srcEdge[leadIndex];

		return true;
	}

	*pOutSrcEdge = srcEdge[0];

	U32F srcWaypointIndex     = waypointIndex[0];
	U32F srcExitWaypointIndex = srcWaypointIndex;

	// Find the index of the waypoint at the exit of the current edge
	for (U32F ii = srcWaypointIndex + 1; ii < waypointCount; ++ii)
	{
		const NavLedgeReference edge(m_activePathInfo.m_waypointsPs.GetNavLedgeHandle(ii));

		if (!edge.IsGood() || edge != *pOutSrcEdge)
		{
			break;
		}

		srcExitWaypointIndex = ii;
	}

	U32F dstWaypointIndex = srcExitWaypointIndex;

	// Find the index of the first waypoint on the edge after the current edge
	for (U32F ii = srcExitWaypointIndex + 1; ii < waypointCount; ++ii)
	{
		const NavLedgeReference edge(m_activePathInfo.m_waypointsPs.GetNavLedgeHandle(ii));

		if (edge.IsGood())
		{
			dstWaypointIndex = ii;
			break;
		}
	}

//TODO(MB) HACK - How can srcWaypointIndex or dstWaypointIndex be bad here (DT#111200)?
	{
		const NavLedgeHandle hSrcLedge = m_activePathInfo.m_waypointsPs.GetNavLedgeHandle(srcWaypointIndex);
		const NavLedgeHandle hDstLedge = m_activePathInfo.m_waypointsPs.GetNavLedgeHandle(dstWaypointIndex);

		if (!hSrcLedge.IsValid() || !hDstLedge.IsValid())
		{
			AI_ASSERTF( hSrcLedge.IsValid(), ("Invalid source ledge") );
			AI_ASSERTF( hDstLedge.IsValid(), ("Invalid destination ledge") );

			*pOutSrcEdgeBf = srcEdgeBf[0];
			*pOutSrcEdge   = srcEdge[0];
			*pOutDstEdgeBf = srcEdgeBf[0];
			*pOutDstEdge   = srcEdge[0];

			return true;
		}
	}

	//-- Source edge --

	Point edgePos0 = pOutSrcEdge->GetVert0();
	Point edgePos1 = pOutSrcEdge->GetVert1();
//	OffsetPoints(&edgePos0, &edgePos1);

	Point waypointPosWs = m_activePathInfo.m_waypointsPs.GetAsNavLocation(srcExitWaypointIndex).GetPosWs();
	Point edgePosWs     = ClosestPointOnEdgeToPoint(edgePos0, edgePos1, waypointPosWs);

	*pOutSrcEdgeBf = BoundFrame(edgePosWs, pOutSrcEdge->GetGrabQuaternion(), pOutSrcEdge->GetBinding());

	//-- Dest edge --

	pOutDstEdge->SetLedge(m_activePathInfo.m_waypointsPs.GetNavLedgeHandle(dstWaypointIndex));

	edgePos0 = pOutDstEdge->GetVert0();
	edgePos1 = pOutDstEdge->GetVert1();
//	OffsetPoints(&edgePos0, &edgePos1);

	edgePosWs = ClosestPointOnEdgeToPoint(edgePos0, edgePos1, edgePosWs);

	*pOutDstEdgeBf = BoundFrame(edgePosWs, pOutDstEdge->GetGrabQuaternion(), pOutDstEdge->GetBinding());

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiClimbController::EdgeStatus AiClimbController::GetNextEdge(NavLedgeReference* pOutEdge, BoundFrame* pOutEdgeBf) const
{
	AI_ASSERT( pOutEdge );
	AI_ASSERT( pOutEdgeBf );

	const F32 kTransitionThreshold = 0.2f;

	BoundFrame edgeBf;
	BoundFrame nextEdgeBf;
	NavLedgeReference edge;
	NavLedgeReference nextEdge;

	// Current and next edges with exit/enter bound frames
	if (!FindTransitionPoints(&edge, &edgeBf, &nextEdge, &nextEdgeBf))
	{
		return kEdgeStatusFail;
	}

	const Point startPosWs = FindLimbSourceBf().GetTranslationWs();

	// If the distance between the current limb position and the next edge is close enough to the minimum distance
	// between the edges then use the current position instead to avoid what looks like an unnecessary transition.
	// When moving vertically between nearly parallel edges this encourages moving between them rather than along them.
	if (edge != nextEdge)
	{
		Point endPosWs;

		Point edgePos0 = nextEdge.GetVert0();
		Point edgePos1 = nextEdge.GetVert1();
// 		OffsetPoints(&edgePos0, &edgePos1);

		const F32 limbToNextEdgeDist = DistPointSegment(startPosWs, edgePos0, edgePos1, &endPosWs);
		const F32 edgeToNextEdgeDist = Dist(edgeBf.GetTranslation(), nextEdgeBf.GetTranslation());
		if (Abs(limbToNextEdgeDist - edgeToNextEdgeDist) < kTransitionThreshold)
		{
			edgeBf.SetTranslation(startPosWs);
			nextEdgeBf.SetTranslation(endPosWs);
		}
	}

	const F32 toEdgeDistSqr     = DistSqr(startPosWs, edgeBf.GetTranslation());
	const bool atEdgeExit       = toEdgeDistSqr < Sqr(kShimmyDistMax) || m_status.m_shimmyExitEdgeIndex == edge.GetId();
	const F32 toNextEdgeDistSqr = DistSqr(startPosWs, nextEdgeBf.GetTranslation());
	const bool handsOnSameEdge  = m_status.m_sourceEdgeHand[kLeftArm] == m_status.m_sourceEdgeHand[kRightArm];

	EdgeStatus edgeStatus = kEdgeStatusClimb;

	// If hands are on different edges return the next edge
	if (!m_status.m_isInWallShimmy && !handsOnSameEdge)
	{
		*pOutEdge   = nextEdge;
		*pOutEdgeBf = nextEdgeBf;
	}
	// On the last edge of the path
	else if (edge == nextEdge)
	{
		*pOutEdge   = nextEdge;
		*pOutEdgeBf = nextEdgeBf;

		edgeStatus = atEdgeExit ? kEdgeStatusDone : kEdgeStatusClimb;
	}
	// Return the closest point on the next edge if it is close enough to transition (Reach)
	else if (toNextEdgeDistSqr < Sqr(kReachDistMax))
	{
		*pOutEdge   = nextEdge;
		*pOutEdgeBf = nextEdgeBf;
	}
	// Return a point along the current edge if the exit point is too far to transition (Shimmy)
	else if (!atEdgeExit)
	{
		const Point edgePos0 = edge.GetVert0();
		const Point edgePos1 = edge.GetVert1();

		// Set the exit point to the end of the edge
		Point edgePosWs;
		DistPointSegment(nextEdgeBf.GetTranslation(), edgePos0, edgePos1, &edgePosWs);
		edgeBf.SetTranslation(edgePosWs);

		*pOutEdge   = edge;
		*pOutEdgeBf = edgeBf;
	}
	// Return the closest point on the next edge if it is close enough to transition (Dyno)
	else
	{
		Point edgePos0 = nextEdge.GetVert0();
		Point edgePos1 = nextEdge.GetVert1();
		OffsetPoints(&edgePos0, &edgePos1);

		// Set the exit point offset from the end of the edge
		Point edgePosWs;
		DistPointSegment(nextEdgeBf.GetTranslation(), edgePos0, edgePos1, &edgePosWs);
		nextEdgeBf.SetTranslation(edgePosWs);

		*pOutEdge   = nextEdge;
		*pOutEdgeBf = nextEdgeBf;
	}

	if (handsOnSameEdge && IsBlocked(startPosWs, pOutEdgeBf->GetTranslation(), edge == *pOutEdge))
	{
		edgeStatus = kEdgeStatusBlocked;
	}

	return edgeStatus;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::UpdatePathWaypointsInternal(const PathInfo& pathInfo)
{
	if (!pathInfo.m_valid || !pathInfo.m_waypointsPs.IsValid())
	{
		return false;
	}

	m_activePathInfo = pathInfo;
	m_activePathInfo.m_waypointsPs = pathInfo.m_waypointsPs;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::FindClosestPointOnEdge(Point posWs, BoundFrame* pOutEdgeBf, NavLedgeReference* pOutEdge) const
{
	PROFILE_AUTO( AI );

	AI_ASSERT( pOutEdgeBf );

	NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

	FindNavLedgeGraphParams findParams;
	findParams.m_pointWs           = posWs;
	findParams.m_searchRadius      = 1.0f;
	findParams.m_bindSpawnerNameId = Nav::kMatchAllBindSpawnerNameId;

	if (!NavLedgeGraphMgr::Get().FindLedgeGraph(&findParams))
	{
		return false;
	}

	NavLedgeReference closestEdge;
	closestEdge.SetLedge(findParams.m_pNavLedge);

	if (pOutEdge)
	{
		*pOutEdge = closestEdge;
	}

	pOutEdgeBf->SetTranslation(findParams.m_nearestPointWs);
	pOutEdgeBf->SetRotation(closestEdge.GetGrabQuaternion());
	pOutEdgeBf->SetBinding(closestEdge.GetBinding());

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame AiClimbController::FindHandAnimBf() const
{
	const NdGameObject* pGo = GetCharacterGameObject();
	AI_ASSERT( pGo );

	const Locator handLocWs = GetEdgeApLocator(pGo->GetAttachSystem()->GetLocatorById(SID("targLWrist")),
											   pGo->GetAttachSystem()->GetLocatorById(SID("targRWrist")));
	const BoundFrame boundFrame(handLocWs, pGo->GetBoundFrame().GetBinding());

	return boundFrame;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame AiClimbController::FindHandSourceBf() const
{
	AI_ASSERT( m_status.m_sourceEdgeHand[kLeftArm].IsGood() );
	AI_ASSERT( m_status.m_sourceEdgeHand[kRightArm].IsGood() );

	const Locator handLocWs = GetEdgeApLocator(m_status.m_sourceEdgeHandBf[kLeftArm].GetLocator(),
											   m_status.m_sourceEdgeHandBf[kRightArm].GetLocator());
	const BoundFrame boundFrame(handLocWs, m_status.m_sourceEdgeHandBf[kLeftArm].GetBinding());

	return boundFrame;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame AiClimbController::FindHandTargetBf() const
{
	AI_ASSERT( m_status.m_targetEdgeHand[kLeftArm].IsGood() );
	AI_ASSERT( m_status.m_targetEdgeHand[kRightArm].IsGood() );

	const Locator handLocWs = GetEdgeApLocator(m_status.m_targetEdgeHandBf[kLeftArm].GetLocator(),
											   m_status.m_targetEdgeHandBf[kRightArm].GetLocator());
	const BoundFrame boundFrame(handLocWs, m_status.m_targetEdgeHandBf[kLeftArm].GetBinding());

	return boundFrame;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame AiClimbController::FindFootAnimBf() const
{
	const NdGameObject* pGo = GetCharacterGameObject();
	AI_ASSERT( pGo );

	const Locator footLocWs = GetEdgeApLocator(pGo->GetAttachSystem()->GetLocatorById(SID("targLAnkle")),
											   pGo->GetAttachSystem()->GetLocatorById(SID("targRAnkle")));
	const BoundFrame boundFrame(footLocWs, pGo->GetBoundFrame().GetBinding());

	return boundFrame;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame AiClimbController::FindFootSourceBf() const
{
	AI_ASSERT( m_status.m_sourceEdgeFoot[kLeftLeg].IsGood() );
	AI_ASSERT( m_status.m_sourceEdgeFoot[kRightLeg].IsGood() );

	const Locator footLocWs = GetEdgeApLocator(m_status.m_sourceEdgeFootBf[kLeftLeg].GetLocator(),
											   m_status.m_sourceEdgeFootBf[kRightLeg].GetLocator());
	const BoundFrame boundFrame(footLocWs, m_status.m_sourceEdgeFootBf[kLeftLeg].GetBinding());

	return boundFrame;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame AiClimbController::FindFootTargetBf() const
{
	AI_ASSERT( m_status.m_targetEdgeFoot[kLeftLeg].IsGood() );
	AI_ASSERT( m_status.m_targetEdgeFoot[kRightLeg].IsGood() );

	const Locator footLocWs = GetEdgeApLocator(m_status.m_targetEdgeFootBf[kLeftLeg].GetLocator(),
											   m_status.m_targetEdgeFootBf[kRightLeg].GetLocator());
	const BoundFrame boundFrame(footLocWs, m_status.m_targetEdgeFootBf[kLeftLeg].GetBinding());

	return boundFrame;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame AiClimbController::FindLimbAnimBf() const
{
	return m_status.m_isInWallShimmy ? FindFootAnimBf() : FindHandAnimBf();
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame AiClimbController::FindLimbSourceBf() const
{
	return m_status.m_isInWallShimmy ? FindFootSourceBf() : FindHandSourceBf();
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame AiClimbController::FindLimbTargetBf() const
{
	return m_status.m_isInWallShimmy ? FindFootTargetBf() : FindHandTargetBf();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::AttachToClosestEdge(Point posWs, StringId64 exitModeId)
{
	BoundFrame edgeBf;
	NavLedgeReference edge;

	if (!FindClosestPointOnEdge(posWs, &edgeBf, &edge))
	{
		return false;
	}

	m_status.SetSource(edge, edgeBf);
	m_status.SetTarget(edge, edgeBf);

	m_status.m_shimmyExitEdgeIndex = kInvalidEdgeIndex;
	m_status.m_wantInsideCorner    = false;
	m_status.m_wantOutsideCorner   = false;
	m_status.m_wantWallShimmy      = false;//edge.GetFlags() & FeatureEdge::kFlagStandingShimmy;
	m_status.m_isInWallShimmy      = m_status.m_wantWallShimmy && (m_status.m_isInWallShimmy || exitModeId == SID("edge-standing-shimmy"));
	m_status.m_wantHang            = IsLedgeHang(edge, edgeBf.GetLocator());
	m_status.m_wantReachEnd        = false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::PrepareToClimb()
{
	NavCharacter* pNavChar = GetCharacter();
	AI_ASSERT( pNavChar );

	if (IAiWeaponController* pWeaponController = pNavChar->GetAnimationControllers()->GetWeaponController())
	{
		pWeaponController->ForceGunState(kGunStateHolstered, true, true);
		pWeaponController->AllowGunOut(false);
	}

	if (IGestureController* pGestureController = pNavChar->GetGestureController())
	{
		Gesture::PlayArgs args = Gesture::g_defaultPlayArgs;
		args.m_blendOutTime = 0.0f;

		pGestureController->Clear(args);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::IsLedgeHang(NavLedgeReference edge, const Locator& edgeLoc) const
{
	const Scalar kForwardProbeDist  = SCALAR_LC(0.6f);			// Distance to back up probe start from edge
	const Scalar kDownwardProbeDist = SCALAR_LC(1.3f);			// Approximate offset from edge to feet
	const Scalar kLeftwardProbeDist = SCALAR_LC(0.1f);			// Approximate offset from center to left foot
	const Scalar kProbeDist         = SCALAR_LC(1.0f);			// Probe distance from start
	const F32 kProbeRadius          = 0.1f;						// Make sure feet are well clear of walls

	if (m_status.m_isInWallShimmy)
	{
		return false;
	}

	if (edge.IsForceHang())
	{
		return true;
	}

	const Vector upDir      = GetLocalY(edgeLoc.GetRotation());
	const Vector forwardDir = GetLocalZ(edgeLoc.GetRotation());
	const Vector leftDir    = GetLocalX(edgeLoc.GetRotation());

	const CollideFilter collideFilter = CollideFilter(Collide::kLayerMaskGeneral, Pat((1ULL << Pat::kPassThroughShift)), GetCharacterGameObject());
	const Vector probe = kProbeDist * forwardDir;

	// Left foot probe
	Point start = edgeLoc.GetTranslation() - kDownwardProbeDist * upDir - kForwardProbeDist * forwardDir + kLeftwardProbeDist * leftDir;
	F32 tt      = DeprecatedSlowAndWeirdProbe(start, probe, kProbeRadius, nullptr, collideFilter);
	bool isHit  = tt >= 0.0f;

// #define ENABLE_HANG_DEBUG
#if defined(ENABLE_HANG_DEBUG)
g_prim.Draw( DebugArrow(start, probe, isHit ? kColorRed : kColorGreen), Seconds(1.5f) );
if (isHit) g_prim.Draw( DebugSphere(start + tt * probe, kProbeRadius, kColorRed), Seconds(1.5f) );
#endif

	if (!isHit)
	{
		return true;
	}

	// Right foot probe
	start += 2.0f * kLeftwardProbeDist * -leftDir;
	tt     = DeprecatedSlowAndWeirdProbe(start, probe, kProbeRadius, nullptr, collideFilter);
	isHit  = tt >= 0.0f;

#if defined(ENABLE_HANG_DEBUG)
g_prim.Draw( DebugArrow(start, probe, isHit ? kColorRedTrans : kColorGreenTrans), Seconds(1.5f) );
if (isHit) g_prim.Draw( DebugSphere(start + tt * probe, kProbeRadius, kColorRedTrans), Seconds(1.5f) );
#endif

	if (!isHit)
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// bool AiClimbController::IsLedgeForward(NavLedgeReference edge, NavLedgeReference nextEdge) const
// {
// 	const F32 kCosFwdDeg  = Cos(DegreesToRadians(5.0f));
// 	const F32 kMinFwdDist = 0.5f;
// 
// 	const bool isEdgeHang     = edge.GetFlags() & FeatureEdge::kFlagCanHang;
// 	const bool isNextEdgeHang = nextEdge.GetFlags() & FeatureEdge::kFlagCanHang;
// 
// 	// Edges are not hanging
// 	if (!isEdgeHang || !isNextEdgeHang)
// 	{
// 		return false;
// 	}
// 
// 	const Vector edgeNormal     = edge.GetWallNormal();
// 	const Vector nextEdgeNormal = nextEdge.GetWallNormal();
// 	const F32 parallelDot       = Dot(edgeNormal, nextEdgeNormal);
// 
// 	// Edges are opposed or are not close enough to parallel
// 	if (parallelDot < kCosFwdDeg)
// 	{
// 		return false;
// 	}
// 
// 	const Point edgePos       = edge.GetEdgeCenter();
// 	const Point nextEdgePos   = nextEdge.GetEdgeCenter();
// 	const Vector fromNextEdge = edgePos - nextEdgePos;
// 	const F32 forwardDist     = Dot(edgeNormal, fromNextEdge);
// 
// 	// Edges are too close together
// 	if (forwardDist < kMinFwdDist)
// 	{
// 		return false;
// 	}
// 
// 	return true;
// }

/// --------------------------------------------------------------------------------------------------------------- ///
//TODO(MB) HACK - Ap adjust for scaled characters
bool AiClimbController::GetScaleAdjustLs(const NdGameObject* pGo, Vector* pOutScaleAdjustLs) const
{
	AI_ASSERT( pGo );
	AI_ASSERT( pOutScaleAdjustLs );

	const Vector kFemaleAdjustLs           = Vector(0.0f, 0.06f, 0.09f);
	const Vector kFemaleHangAdjustLs       = Vector(0.0f, 0.14f, 0.05f);
	const Vector kFemaleShimmyAdjustLs     = Vector(0.0f, 0.06f, 0.05f);
	const Vector kFemaleHangShimmyAdjustLs = Vector(0.0f, 0.1f, 0.03f);
	const Vector kFemaleWallShimmyAdjustLs = Vector(0.0f, 0.0f, 0.05f);

	*pOutScaleAdjustLs = kZero;

	if (!EngineComponents::GetGameInfo()->IsFemaleSkel(pGo))
	{
		return false;
	}

	if (IsState(kStateShimmy))
	{
		*pOutScaleAdjustLs = m_status.m_isInWallShimmy ? kFemaleWallShimmyAdjustLs :
							 m_status.m_wantHang ? kFemaleHangShimmyAdjustLs :
							 kFemaleShimmyAdjustLs;
	}
	else
	{
		*pOutScaleAdjustLs = m_status.m_isInWallShimmy ? kFemaleWallShimmyAdjustLs :
							 m_status.m_wantHang ? kFemaleHangAdjustLs :
							 kFemaleAdjustLs;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
//TODO(MB) Should this be combined with IsLedgeHang to save ray casts?
F32 AiClimbController::GetWallSlopeAngle(NavLedgeReference edge, const Locator& edgeLoc)
{
	const Scalar kForwardProbeDist    = SCALAR_LC(0.8f);		// Distance to back up probe start from edge
	const Scalar kDownwardProbeDist   = SCALAR_LC(1.3f);		// Approximate offset from edge to feet
	const Scalar kLeftwardProbeDist   = SCALAR_LC(0.25f);		// Approximate offset from center to left foot
	const Scalar kProbeDist           = SCALAR_LC(1.4f);		// Probe distance from start
	const F32 kProbeRadius            = 0.06f;					// Foot clearance from the wall
	const F32 kPlayerClimbProbeRadius = 0.18f;					// Foot clearance from the wall when player is climbing

	if (m_status.m_isInWallShimmy)
	{
		return 0.0f;
	}

	if (m_status.m_wantHang)
	{
		return 0.0f;
	}

	const Vector upDir      = GetLocalY(edgeLoc.GetRotation());
	const Vector forwardDir = GetLocalZ(edgeLoc.GetRotation());
	const Vector leftDir    = GetLocalX(edgeLoc.GetRotation());

	const CollideFilter collideFilter = CollideFilter(Collide::kLayerMaskBackground, Pat((1ULL << Pat::kPassThroughShift)), GetCharacterGameObject());
	const Vector probe = kProbeDist * forwardDir;

//TODO(MB) HACK - Increase probe radius when in player-climb-over pose to push further away from the wall
	const F32 probeRadius = m_status.m_animId == SID("idle-reach0-mounted") ? kPlayerClimbProbeRadius : kProbeRadius;

	// Left foot probe
	Point footPos = edgeLoc.GetTranslation() - kDownwardProbeDist * upDir + kLeftwardProbeDist * leftDir;
	Point start   = footPos - kForwardProbeDist * forwardDir;
	F32 tt        = DeprecatedSlowAndWeirdProbe(start, probe, probeRadius, nullptr, collideFilter);
	bool isHit    = tt >= 0.0f;
	Point contact = start + tt * probe;

// #define ENABLE_SLOPE_DEBUG
#if defined(ENABLE_SLOPE_DEBUG)
g_prim.Draw( DebugArrow(start, probe, isHit ? kColorRed : kColorGreen), Seconds(1.5f) );
if (isHit) g_prim.Draw( DebugSphere(contact, probeRadius, kColorRed), Seconds(1.5f) );
#endif

	F32 leftWallSlopeAngle = 0.0f;

	if (isHit)
	{
		const Vector footUpDir = SafeNormalize(edgeLoc.GetTranslation() - footPos, kZero);
		const Vector hitDir    = SafeNormalize(contact - edgeLoc.GetTranslation(), kZero);

		const F32 dot = Limit01( Dot(-footUpDir, hitDir) );
		leftWallSlopeAngle = Acos(dot);

		if (Dot(forwardDir, hitDir) > 0.0f)
		{
			leftWallSlopeAngle = -leftWallSlopeAngle;
		}

#if defined(ENABLE_SLOPE_DEBUG)
const Point edgePosWs = edgeLoc.GetTranslation();
g_prim.Draw(DebugLine(edgePosWs, -footUpDir, kColorBlue), Seconds(1.5f));
g_prim.Draw(DebugLine(edgePosWs, hitDir, kColorCyan), Seconds(1.5f));
#endif

	}

	// Right foot probe
	footPos += 2.0f * kLeftwardProbeDist * -leftDir;
	start   += 2.0f * kLeftwardProbeDist * -leftDir;
	tt       = DeprecatedSlowAndWeirdProbe(start, probe, probeRadius, nullptr, collideFilter);
	isHit    = tt >= 0.0f;
	contact  = start + tt * probe;

#if defined(ENABLE_SLOPE_DEBUG)
g_prim.Draw( DebugArrow(start, probe, isHit ? kColorRedTrans : kColorGreenTrans), Seconds(1.5f) );
if (isHit) g_prim.Draw( DebugSphere(contact, probeRadius, kColorRedTrans), Seconds(1.5f) );
#endif

	F32 rightWallSlopeAngle = 0.0f;

	if (isHit)
	{
		const Vector footUpDir  = SafeNormalize(edgeLoc.GetTranslation() - footPos, kZero);
		const Vector hitDir     = SafeNormalize(contact - edgeLoc.GetTranslation(), kZero);

		const F32 dot = Limit01( Dot(-footUpDir, hitDir) );
		rightWallSlopeAngle = Acos(dot);

		if (Dot(forwardDir, hitDir) > 0.0f)
		{
			rightWallSlopeAngle = -rightWallSlopeAngle;
		}

#if defined(ENABLE_SLOPE_DEBUG)
const Point edgePosWs = edgeLoc.GetTranslation();
g_prim.Draw(DebugLine(edgePosWs, -footUpDir, kColorBlue), Seconds(1.5f));
g_prim.Draw(DebugLine(edgePosWs, hitDir, kColorCyan), Seconds(1.5f));
#endif

	}

// #define ENABLE_IK_DEBUG
#if defined(ENABLE_IK_DEBUG)
g_prim.Draw(DebugArrow(m_status.m_ikInfo.m_footWallPosWs[kLeftLeg], m_status.m_ikInfo.m_footWallNormalWs[kLeftLeg]), Seconds(1.5f));
g_prim.Draw(DebugArrow(m_status.m_ikInfo.m_footWallPosWs[kRightLeg], m_status.m_ikInfo.m_footWallNormalWs[kRightLeg]), Seconds(1.5f));
#endif

	const F32 wallSlopeAngle = leftWallSlopeAngle < 0.0f || rightWallSlopeAngle < 0.0f ?
							   Max(leftWallSlopeAngle, rightWallSlopeAngle) :
							   Min(leftWallSlopeAngle, rightWallSlopeAngle);

	return wallSlopeAngle;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiClimbController::GetBaseAnimStateId() const
{
	const NdGameObject* pCharacter = GetCharacterGameObject();
	if (!pCharacter)
	{
		return INVALID_STRING_ID_64;
	}

	const AnimControl* pAnimControl = pCharacter->GetAnimControl();
	if (!pAnimControl)
	{
		return INVALID_STRING_ID_64;
	}

	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	if (!pBaseLayer)
	{
		return INVALID_STRING_ID_64;
	}

	const StringId64 animStateId = pBaseLayer->CurrentStateId();

	return animStateId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::FindBestTransition(AiClimbController::Status* pStatus) const
{
	AI_ASSERT( pStatus );

	const F32 startFactor       = 2.0f;		//g_playerOptions.m_edgeOptions.m_climbing2_climbFactorStart;
	const F32 endFactor         = 1.0f;		//g_playerOptions.m_edgeOptions.m_climbing2_climbFactorEnd;
	const F32 movementFactor    = 0.25f;	//g_playerOptions.m_edgeOptions.m_climbing2_climbFactorMovement;
	const F32 movementDirFactor = 2.0f;		//g_playerOptions.m_edgeOptions.m_climbing2_climbFactorMovementDir;

	const DC::ClimbInfo* pClimbInfo = GetClimbInfo();

	Locator handEdgeLocStart[2], handEdgeLocEnd[2];
	handEdgeLocStart[kLeftArm]  = pStatus->m_sourceEdgeHandBf[kLeftArm].GetLocator();
	handEdgeLocStart[kRightArm] = pStatus->m_sourceEdgeHandBf[kRightArm].GetLocator();
	handEdgeLocEnd[kLeftArm]    = pStatus->m_targetEdgeHandBf[kLeftArm].GetLocator();
	handEdgeLocEnd[kRightArm]   = pStatus->m_targetEdgeHandBf[kRightArm].GetLocator();

	const Locator handStartAp = GetEdgeApLocator(handEdgeLocStart[kLeftArm], handEdgeLocStart[kRightArm]);
	const Locator handEndAp   = GetEdgeApLocator(handEdgeLocEnd[kLeftArm], handEdgeLocEnd[kRightArm]);

	Vector handMovement[2];
	handMovement[kLeftArm]  = handEdgeLocEnd[kLeftArm].Pos() - handEdgeLocStart[kLeftArm].Pos();
	handMovement[kRightArm] = handEdgeLocEnd[kRightArm].Pos() - handEdgeLocStart[kRightArm].Pos();

	Vector handMovementDir[2];
	handMovementDir[kLeftArm]  = SafeNormalize(handMovement[kLeftArm], kZero);
	handMovementDir[kRightArm] = SafeNormalize(handMovement[kRightArm], kZero);

// 	bool wantHandsClose = false;
// 	if (IsSameEdgeChain(GetEdge(kLeftArm), GetEdge(kRightArm)))
// 	{
// 		const Scalar handsCloseDistSqr = SCALAR_LC(Sqr(0.25f));
// 		const Scalar handDistSqr       = DistSqr(handEdgeLocStart[kLeftArm].GetTranslation(),
// 												 handEdgeLocStart[kRightArm].GetTranslation());
// 		wantHandsClose = handDistSqr < handsCloseDistSqr;
// 	}

	const bool wantOneClimbHand = !pStatus->m_isInWallShimmy && !pStatus->m_wantReachEnd &&
								  ((pStatus->m_sourceEdgeHand[kLeftArm] != pStatus->m_sourceEdgeHand[kRightArm]) ||
								   (pStatus->m_targetEdgeHand[kLeftArm] != pStatus->m_targetEdgeHand[kRightArm]));

	AnimRating currRating;
	AnimRating bestRating;

	for (I32 animIndex = 0; animIndex < pClimbInfo->m_climbTransitions->m_count; ++animIndex)
	{
		const DC::ClimbTransition* pClimb = &pClimbInfo->m_climbTransitions->m_array[animIndex];

		if (!(pClimb->m_properties & DC::kClimbPropertiesValid))
		{
			continue;
		}

		if (pClimb->m_properties & DC::kClimbPropertiesDisabled)
		{
			continue;
		}

		U32F numClimbHands = 0;
		if (pClimb->m_properties & DC::kClimbPropertiesHandL) numClimbHands++;
		if (pClimb->m_properties & DC::kClimbPropertiesHandR) numClimbHands++;

		if (wantOneClimbHand != (numClimbHands == 1))
		{
			continue;
		}

		// Filter out undesired types
		if (pClimb->m_type == DC::kClimbTypeInvalid     ||
			pClimb->m_type == DC::kClimbTypeDynoMissCw  ||
			pClimb->m_type == DC::kClimbTypeDynoMissCcw ||
			pClimb->m_type == DC::kClimbTypeFallingGrab ||
			pClimb->m_type == DC::kClimbTypePitonDyno)
		{
			continue;
		}

//TODO(MB) HACK - Ignore selection criteria and force player climb idle for testing
		if (pStatus->m_allowPlayerClimb && !AllowIdleExit() && !pStatus->m_wantWallShimmy && !pStatus->m_wantHang)
		{
			if (pClimb->m_animName == SID("idle-reach0-mounted"))
			{
				pStatus->m_animId     = pClimb->m_animName;
				pStatus->m_animIndex  = animIndex;
				pStatus->m_mirrored   = false;
				pStatus->m_flags      = pClimb->m_flags;
				pStatus->m_properties = pClimb->m_properties;
				return true;
			}
		}

		const bool isIdle = pClimb->m_type == DC::kClimbTypeIdle;

		if (isIdle != pStatus->m_isIdling)
		{
			continue;
		}

		const bool isReachEnd = pClimb->m_type == DC::kClimbTypeReach;

		if (isReachEnd != pStatus->m_wantReachEnd)
		{
			continue;
		}

		// Filter out undesired flags
		if (pClimb->m_flags & (DC::kClimbFlagsLadder         |
							   DC::kClimbFlagsOneHandedL     |
							   DC::kClimbFlagsStamina        |
							   DC::kClimbFlagsPitonCorner    |
							   DC::kClimbFlagsDynoSlip       |
							   DC::kClimbFlags180Flip        |
							   DC::kClimbFlags180Climb       |
							   DC::kClimbFlagsForwardClimb   |
							   DC::kClimbFlagsPipeClimb      |
							   DC::kClimbFlagsFatiguedOnly   |
							   DC::kClimbFlagsYoungDrakeOnly |
							   DC::kClimbFlagsPlayerOnly))
		{
			continue;
		}

		const bool isOutsideCorner   = (pClimb->m_flags & DC::kClimbFlagsCornerOutside)   != 0;
		const bool isInsideCorner    = (pClimb->m_flags & DC::kClimbFlagsCornerInside)    != 0;
		const bool isHang            = (pClimb->m_flags & DC::kClimbFlagsHang)            != 0;
		const bool hasRoomToSwing    = (pClimb->m_flags & DC::kClimbFlagsForceHangFree)   != 0;
		const bool hasNoRoomToSwing  = (pClimb->m_flags & DC::kClimbFlagsForceHangWall)   != 0;
		const bool isBack            = (pClimb->m_flags & DC::kClimbFlagsDynoBack)        != 0;
//		const bool areHandsClose     = (pClimb->m_flags & DC::kClimbFlagsHandsClose)      != 0;
		const bool isToLedgeFacing   = (pClimb->m_flags & DC::kClimbFlagsToLedgeFacing)   != 0;
		const bool isFromLedgeFacing = (pClimb->m_flags & DC::kClimbFlagsFromLedgeFacing) != 0;
// 		const bool isForward         = (pClimb->m_flags & DC::kClimbFlagsForwardClimb)    != 0;

		bool doNotMirror = (pClimb->m_flags & DC::kClimbFlagsForceNoMirror) != 0;
		bool mirrorOnly  = (pClimb->m_flags & DC::kClimbFlagsForceMirror) != 0;

		if (pClimb->m_type == DC::kClimbTypeReach)
		{
			mirrorOnly  = pStatus->m_mirrorReach;
			doNotMirror = !mirrorOnly;
		}

		if (isBack)
		{
			continue;
		}

		if (isOutsideCorner != pStatus->m_wantOutsideCorner ||
			isInsideCorner != pStatus->m_wantInsideCorner)
		{
			continue;
		}

		if (isToLedgeFacing != pStatus->m_wantWallShimmy)
		{
			continue;
		}

		if (isFromLedgeFacing != pStatus->m_isInWallShimmy)
		{
			continue;
		}

		if (!pStatus->m_wantWallShimmy && !pStatus->m_isInWallShimmy)
		{
// 			if (areHandsClose != wantHandsClose)
// 			{
// 				continue;
// 			}

			if (isHang != pStatus->m_wantHang)
			{
				continue;
			}

			if (pStatus->m_wantHang)
			{
				const bool wantRoomToSwing = false;		//TODO(MB) Always force no-swing hang to leave more wall room

				if (hasNoRoomToSwing && wantRoomToSwing)
				{
					continue;
				}

				if (hasRoomToSwing && !wantRoomToSwing)
				{
					continue;
				}
			}

// 			if (isForward != pStatus->m_wantForward)
// 			{
// 				continue;
// 			}
		}

		for (U32F mirror = 0; mirror < 2; ++mirror)
		{
			if (mirror && doNotMirror)
			{
				continue;
			}

			if (!mirror && mirrorOnly)
			{
				continue;
			}

			currRating = AnimRating();
			currRating.m_animIndex = animIndex;
			currRating.m_mirror    = mirror;
 
			const Locator startAp = ComputeClimbAnimStartApHand(pClimb,
																handStartAp,
																handEdgeLocStart[kLeftArm],
																handEdgeLocStart[kRightArm],
																mirror,
																1.0f);

			if (isIdle)
			{
				Point animHandEdgeStartLs[2];
				GetClimbAnimHandEdgeStart(pClimb, &animHandEdgeStartLs[kLeftArm], &animHandEdgeStartLs[kRightArm], mirror, 1.0f);

				Point animHandEdgeStartWs[2];
				animHandEdgeStartWs[kLeftArm]  = startAp.TransformPoint(animHandEdgeStartLs[kLeftArm]);
				animHandEdgeStartWs[kRightArm] = startAp.TransformPoint(animHandEdgeStartLs[kRightArm]);

				Vector handStartError[2];
				handStartError[kLeftArm]  = handEdgeLocStart[kLeftArm].Pos() - animHandEdgeStartWs[kLeftArm];
				handStartError[kRightArm] = handEdgeLocStart[kRightArm].Pos() - animHandEdgeStartWs[kRightArm];

				currRating.m_rating                      = LengthSqr(handStartError[kLeftArm]) + LengthSqr(handStartError[kRightArm]);;
				currRating.m_handStartRatings[kLeftArm]  = LengthSqr(handStartError[kLeftArm]);
				currRating.m_handStartRatings[kRightArm] = LengthSqr(handStartError[kRightArm]);
			}
			else if (pClimb->m_type == DC::kClimbTypeReach)
			{
				Point animHandEdgeEndLs[2];
				GetClimbAnimHandEdgeEnd(pClimb, &animHandEdgeEndLs[kLeftArm], &animHandEdgeEndLs[kRightArm], mirror, 1.0f);

				const I32F climbHand = mirror ? kLeftArm : kRightArm;

				if (pClimb->m_flags & DC::kClimbFlagsFromLedgeFacing)
				{
					Point animFootEdgeStartLs[2];
					GetClimbAnimFootEdgeStart(pClimb, &animFootEdgeStartLs[kLeftArm], &animFootEdgeStartLs[kRightArm], mirror, 1.0f);

					const I32F plantedFoot = mirror ? kLeftLeg : kRightLeg;
					AI_ASSERT( plantedFoot == climbHand );

					const Point animHandEdgeStartWs = handEndAp.TransformPoint(animFootEdgeStartLs[plantedFoot]);
					const Point animHandEdgeEndWs   = handEndAp.TransformPoint(animHandEdgeEndLs[climbHand]);
					const Vector animGrabDirWs      = animHandEdgeEndWs - animHandEdgeStartWs;

					const Vector edgeGrabDir = handEdgeLocEnd[climbHand].Pos() - pStatus->m_sourceEdgeFootBf[plantedFoot].GetTranslation();

					currRating.m_rating = Length(animGrabDirWs - edgeGrabDir);
				}
				else
				{
					Point animHandEdgeStartLs[2];
					GetClimbAnimHandEdgeStart(pClimb, &animHandEdgeStartLs[kLeftArm], &animHandEdgeStartLs[kRightArm], mirror, 1.0f);

					const I32F fixedHand = !climbHand;

					const Point animHandEdgeStartWs = handEndAp.TransformPoint(animHandEdgeStartLs[fixedHand]);
					const Point animHandEdgeEndWs   = handEndAp.TransformPoint(animHandEdgeEndLs[climbHand]);
					const Vector animGrabDirWs      = animHandEdgeEndWs - animHandEdgeStartWs;

					const F32 reachAnimBias = g_playerOptions.m_reachAnimBias;

					Vector edgeGrabDir = handEdgeLocEnd[climbHand].Pos() - handEdgeLocStart[fixedHand].Pos();
					edgeGrabDir += reachAnimBias / Length(edgeGrabDir) * edgeGrabDir;

					currRating.m_rating = Length(animGrabDirWs - edgeGrabDir);
				}
			}
			else if (pClimb->m_type == DC::kClimbTypeClimb)
			{
				BoundFrame footEdgeBfStartBf[2];
				NavLedgeReference footEdgeStart[2];

				const bool footValidL       = pStatus->m_isInWallShimmy;
				footEdgeStart[kLeftLeg]     = pStatus->m_sourceEdgeFoot[kLeftLeg];
				footEdgeBfStartBf[kLeftLeg] = pStatus->m_sourceEdgeFootBf[kLeftLeg];

				const bool footValidR        = pStatus->m_isInWallShimmy;
				footEdgeStart[kRightLeg]     = pStatus->m_sourceEdgeFoot[kRightLeg];
				footEdgeBfStartBf[kRightLeg] = pStatus->m_sourceEdgeFootBf[kRightLeg];

				Locator footEdgeLocStart[2];
				footEdgeLocStart[kLeftLeg]  = footValidL ? footEdgeBfStartBf[kLeftLeg].GetLocator()  : Locator();
				footEdgeLocStart[kRightLeg] = footValidR ? footEdgeBfStartBf[kRightLeg].GetLocator() : Locator();

				const bool wantHang            = pStatus->m_wantHang;
				const bool wantToLedgeFacing   = (pStatus->m_flags & DC::kClimbFlagsToLedgeFacing);
				const bool wantFromLedgeFacing = (pStatus->m_flags & DC::kClimbFlagsFromLedgeFacing);

				const bool useHands = !(wantToLedgeFacing && wantFromLedgeFacing);
				const bool useFeet  = !wantHang && footValidL && footValidR;

				if (useHands)
				{
					Point animHandEdgeStartLs[2], animHandEdgeEndLs[2];
					GetClimbAnimHandEdgeStart(pClimb, &animHandEdgeStartLs[kLeftArm], &animHandEdgeStartLs[kRightArm], mirror, 1.0f);
					GetClimbAnimHandEdgeEnd(pClimb, &animHandEdgeEndLs[kLeftArm], &animHandEdgeEndLs[kRightArm], mirror, 1.0f);

					Point animHandEdgeStartWs[2], animHandEdgeEndWs[2];
					animHandEdgeStartWs[kLeftArm]  = startAp.TransformPoint(animHandEdgeStartLs[kLeftArm]);
					animHandEdgeStartWs[kRightArm] = startAp.TransformPoint(animHandEdgeStartLs[kRightArm]);
					animHandEdgeEndWs[kLeftArm]    = handEndAp.TransformPoint(animHandEdgeEndLs[kLeftArm]);
					animHandEdgeEndWs[kRightArm]   = handEndAp.TransformPoint(animHandEdgeEndLs[kRightArm]);

					Vector animHandMovement[2];
					animHandMovement[kLeftArm]  = animHandEdgeEndLs[kLeftArm] - animHandEdgeStartLs[kLeftArm];
					animHandMovement[kRightArm] = animHandEdgeEndLs[kRightArm] - animHandEdgeStartLs[kRightArm];
					animHandMovement[kLeftArm]  = startAp.TransformVector(animHandMovement[kLeftArm]);
					animHandMovement[kRightArm] = startAp.TransformVector(animHandMovement[kRightArm]);

					Vector animHandMovementDir[2];
					animHandMovementDir[kLeftArm]  = SafeNormalize(animHandMovement[kLeftArm], kZero);
					animHandMovementDir[kRightArm] = SafeNormalize(animHandMovement[kRightArm], kZero);

					Vector handStartError[2], handEndError[2], handMovementError[2];
					handStartError[kLeftArm]     = handEdgeLocStart[kLeftArm].Pos() - animHandEdgeStartWs[kLeftArm];
					handStartError[kRightArm]    = handEdgeLocStart[kRightArm].Pos() - animHandEdgeStartWs[kRightArm];
					handEndError[kLeftArm]       = handEdgeLocEnd[kLeftArm].Pos() - animHandEdgeEndWs[kLeftArm];
					handEndError[kRightArm]      = handEdgeLocEnd[kRightArm].Pos() - animHandEdgeEndWs[kRightArm];
					handMovementError[kLeftArm]  = handMovement[kLeftArm] - animHandMovement[kLeftArm];
					handMovementError[kRightArm] = handMovement[kRightArm] - animHandMovement[kRightArm];

					F32 handMovementDirDotDif[2];
					handMovementDirDotDif[kLeftArm]   = 1.0f - Dot(handMovementDir[kLeftArm], animHandMovementDir[kLeftArm]);
					handMovementDirDotDif[kRightArm]  = 1.0f - Dot(handMovementDir[kRightArm], animHandMovementDir[kRightArm]);
					handMovementDirDotDif[kLeftArm]  *= Length(handMovementError[kLeftArm]);
					handMovementDirDotDif[kRightArm] *= Length(handMovementError[kRightArm]);

					currRating.m_handStartRatings[kLeftArm]        = startFactor * LengthSqr(handStartError[kLeftArm]);
					currRating.m_handStartRatings[kRightArm]       = startFactor * LengthSqr(handStartError[kRightArm]);
					currRating.m_handEndRatings[kLeftArm]          = endFactor * LengthSqr(handEndError[kLeftArm]);
					currRating.m_handEndRatings[kRightArm]         = endFactor * LengthSqr(handEndError[kRightArm]);
					currRating.m_handMovementRatings[kLeftArm]     = movementFactor * LengthSqr(handMovementError[kLeftArm]);
					currRating.m_handMovementRatings[kRightArm]    = movementFactor * LengthSqr(handMovementError[kRightArm]);
					currRating.m_handMovementDirRatings[kLeftArm]  = movementDirFactor * handMovementDirDotDif[kLeftArm] * handMovementDirDotDif[kLeftArm];
					currRating.m_handMovementDirRatings[kRightArm] = movementDirFactor * handMovementDirDotDif[kRightArm] * handMovementDirDotDif[kRightArm];			
				}

				if (useFeet)
				{
					Vector footStartError[2]    = { kZero, kZero };
					Vector footEndError[2]      = { kZero, kZero };
					Vector footMovementError[2] = { kZero, kZero };
					F32 footMovementDirError[2] = { 0.0f, 0.0f };

					Point animFootEdgeStartLs[2], animFootEdgeEndLs[2];
					GetClimbAnimFootEdgeStart(pClimb, &animFootEdgeStartLs[kLeftLeg], &animFootEdgeStartLs[kRightLeg], mirror, 1.0f);
					GetClimbAnimFootEdgeEnd(pClimb, &animFootEdgeEndLs[kLeftLeg], &animFootEdgeEndLs[kRightLeg], mirror, 1.0f);

					Vector animFootMovement[2];
					animFootMovement[kLeftLeg]  = animFootEdgeEndLs[kLeftLeg] - animFootEdgeStartLs[kLeftLeg];
					animFootMovement[kRightLeg] = animFootEdgeEndLs[kRightLeg] - animFootEdgeStartLs[kRightLeg];
					animFootMovement[kLeftLeg]  = startAp.TransformVector(animFootMovement[kLeftLeg]);
					animFootMovement[kRightLeg] = startAp.TransformVector(animFootMovement[kRightLeg]);

					Point animFootEdgeStartPosWs[2], animFootEdgeEndPosWs[2];
					animFootEdgeStartPosWs[kLeftLeg]  = startAp.TransformPoint(animFootEdgeStartLs[kLeftLeg]);
					animFootEdgeStartPosWs[kRightLeg] = startAp.TransformPoint(animFootEdgeStartLs[kRightLeg]);
					animFootEdgeEndPosWs[kLeftLeg]    = handEndAp.TransformPoint(animFootEdgeEndLs[kLeftLeg]);
					animFootEdgeEndPosWs[kRightLeg]   = handEndAp.TransformPoint(animFootEdgeEndLs[kRightLeg]);

					Locator footEdgeLocEnd[2];
					footEdgeLocEnd[kLeftLeg]  = Locator(animFootEdgeEndPosWs[kLeftLeg], handEndAp.Rot());
					footEdgeLocEnd[kRightLeg] = Locator(animFootEdgeEndPosWs[kRightLeg], handEndAp.Rot());

					Vector footMovement[2];
					footMovement[kLeftLeg]  = footEdgeLocEnd[kLeftLeg].Pos() - footEdgeLocStart[kLeftLeg].Pos();
					footMovement[kRightLeg] = footEdgeLocEnd[kRightLeg].Pos() - footEdgeLocStart[kRightLeg].Pos();

					Vector footMovementDir[2];
					footMovementDir[kLeftLeg]  = SafeNormalize(footMovement[kLeftLeg], kZero);
					footMovementDir[kRightLeg] = SafeNormalize(footMovement[kRightLeg], kZero);

					Vector animFootMovementDir[2];
					animFootMovementDir[kLeftLeg]  = SafeNormalize(animFootMovement[kLeftLeg], kZero);
					animFootMovementDir[kRightLeg] = SafeNormalize(animFootMovement[kRightLeg], kZero);

					footStartError[kLeftLeg]     = footEdgeLocStart[kLeftLeg].Pos() - animFootEdgeStartPosWs[kLeftLeg];
					footStartError[kRightLeg]    = footEdgeLocStart[kRightLeg].Pos() - animFootEdgeStartPosWs[kRightLeg];
					footEndError[kLeftLeg]       = footEdgeLocEnd[kLeftLeg].Pos() - animFootEdgeEndPosWs[kLeftLeg];
					footEndError[kRightLeg]      = footEdgeLocEnd[kRightLeg].Pos() - animFootEdgeEndPosWs[kRightLeg];
					footMovementError[kLeftLeg]  = footMovement[kLeftLeg] - animFootMovement[kLeftLeg];
					footMovementError[kRightLeg] = footMovement[kRightLeg] - animFootMovement[kRightLeg];

					F32 footMovementDirDotDif[2];
					footMovementDirDotDif[kLeftLeg]   = 1.0f - Dot(footMovementDir[kLeftLeg], footMovementDir[kLeftLeg]);
					footMovementDirDotDif[kRightLeg]  = 1.0f - Dot(footMovementDir[kRightLeg], footMovementDir[kRightLeg]);
					footMovementDirDotDif[kLeftLeg]  *= Length(footMovementError[kLeftLeg]);
					footMovementDirDotDif[kRightLeg] *= Length(footMovementError[kRightLeg]);

					footMovementDirError[kLeftLeg]  = footMovementDirDotDif[kLeftLeg];
					footMovementDirError[kRightLeg] = footMovementDirDotDif[kRightLeg];

					currRating.m_footStartRatings[kLeftLeg]        = startFactor * LengthSqr(footStartError[kLeftLeg]);
					currRating.m_footStartRatings[kRightLeg]       = startFactor * LengthSqr(footStartError[kRightLeg]);
					currRating.m_footEndRatings[kLeftLeg]          = endFactor * LengthSqr(footEndError[kLeftLeg]);
					currRating.m_footEndRatings[kRightLeg]         = endFactor * LengthSqr(footEndError[kRightLeg]);
					currRating.m_footMovementRatings[kLeftLeg]     = movementFactor * LengthSqr(footMovementError[kLeftLeg]);
					currRating.m_footMovementRatings[kRightLeg]    = movementFactor * LengthSqr(footMovementError[kRightLeg]);
					currRating.m_footMovementDirRatings[kLeftLeg]  = movementDirFactor * footMovementDirError[kLeftLeg] * footMovementDirError[kLeftLeg];
					currRating.m_footMovementDirRatings[kRightLeg] = movementDirFactor * footMovementDirError[kRightLeg] * footMovementDirError[kRightLeg];
				}

				currRating.m_rating  = 0.0f;

				currRating.m_rating += currRating.m_handStartRatings[kLeftArm] + currRating.m_handStartRatings[kRightArm];
				currRating.m_rating += currRating.m_handEndRatings[kLeftArm] + currRating.m_handEndRatings[kRightArm];
				currRating.m_rating += currRating.m_handMovementRatings[kLeftArm] + currRating.m_handMovementRatings[kRightArm];
				currRating.m_rating += currRating.m_handMovementDirRatings[kLeftArm] + currRating.m_handMovementDirRatings[kRightArm];

				currRating.m_rating += currRating.m_footStartRatings[kLeftLeg] + currRating.m_footStartRatings[kRightLeg];
				currRating.m_rating += currRating.m_footEndRatings[kLeftLeg] + currRating.m_footEndRatings[kRightLeg];
				currRating.m_rating += currRating.m_footMovementRatings[kLeftLeg] + currRating.m_footMovementRatings[kRightLeg];
				currRating.m_rating += currRating.m_footMovementDirRatings[kLeftLeg] + currRating.m_footMovementDirRatings[kRightLeg];
			}
			else if (pClimb->m_type == DC::kClimbTypeDyno)
			{
				const Vector edgeVec = handEndAp.Pos() - handStartAp.Pos();
				const Vector animVec = startAp.Pos() - handStartAp.Pos();
 
				const F32 distToApEdge = Length(edgeVec);
				const F32 distToApAnim = Length(animVec);
 
				const Vector edgeFwd          = GetLocalZ(handStartAp.Rot());
				const Vector edgeWallPlaneVec = edgeVec - Dot(edgeFwd, edgeVec) * edgeFwd;
				const Vector animWallPlaneVec = animVec - Dot(edgeFwd, animVec) * edgeFwd;
 
				const Vector edgeWallPlaneDir = SafeNormalize(edgeWallPlaneVec, kZero);
				const Vector animWallPlaneDir = SafeNormalize(animWallPlaneVec, kZero);
 
				const F32 edgeAnimDot = Dot(edgeWallPlaneDir, animWallPlaneDir);
 
				currRating.m_handMovementDirRatings[kLeftArm]  = 1.0f - edgeAnimDot;
				currRating.m_handMovementDirRatings[kRightArm] = currRating.m_handMovementDirRatings[kLeftArm];
				currRating.m_handMovementRatings[kLeftArm]     = 0.1f * Abs(distToApEdge - distToApAnim);
				currRating.m_handMovementRatings[kRightArm]    = currRating.m_handMovementRatings[kLeftArm];

				currRating.m_rating  = 0.0f;
				currRating.m_rating += 2.0f * currRating.m_handMovementDirRatings[kLeftArm];
				currRating.m_rating += 2.0f * currRating.m_handMovementRatings[kLeftArm];
			}

			if (currRating.m_rating < bestRating.m_rating)
			{
				bestRating = currRating;
			}
		}
	}

	bool foundAnim = false;

	if (bestRating.m_animIndex >= 0)
	{
		const DC::ClimbTransition* pClimb = &pClimbInfo->m_climbTransitions->m_array[bestRating.m_animIndex];

		pStatus->m_animId     = pClimb->m_animName;
		pStatus->m_animIndex  = bestRating.m_animIndex;
		pStatus->m_mirrored   = bestRating.m_mirror;
		pStatus->m_flags      = pClimb->m_flags;
		pStatus->m_properties = pClimb->m_properties;

		foundAnim = true;
	}

	return foundAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::GetBlend(const DC::BlendParams* pBlend, F32 animDuration, FadeToStateParams* pOutParams) const
{
	AI_ASSERT( pOutParams );

	pOutParams->m_blendType      = DC::kAnimCurveTypeLinear;
	pOutParams->m_animFadeTime   = 0.0f;
	pOutParams->m_motionFadeTime = 0.0f;

	if (!m_status.m_isTeleporting)
	{
		if (pBlend && pBlend->m_curve != DC::kAnimCurveTypeInvalid)
		{
			pOutParams->m_blendType      = pBlend->m_curve;
			pOutParams->m_animFadeTime   = Min(animDuration, pBlend->m_animFadeTime);
			pOutParams->m_motionFadeTime = pBlend->m_motionFadeTime >= 0.0f            ?
										   Min(animDuration, pBlend->m_motionFadeTime) :
										   pOutParams->m_animFadeTime;
		}
		else
		{
			// kBlendTimeDefault or 1/3 of the anim duration if it's longer
			pOutParams->m_animFadeTime   = Max(animDuration / 3.0f, Min(animDuration, kBlendTimeDefault));
			pOutParams->m_motionFadeTime = pOutParams->m_animFadeTime;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::PlayAnimation(const AnimParams& params)
{
	NdGameObject* pCharacter  = GetCharacterGameObject();
	AnimControl* pAnimControl = pCharacter->GetAnimControl();

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(params.m_animationId).ToArtItem();

	// Set the animation in the state
	AI::SetPluggableAnim(pCharacter, params.m_animationId, INVALID_STRING_ID_64, INVALID_STRING_ID_64, params.m_isMirrored);

	BoundFrame bf;
	if (params.m_isBfValid)
	{
		bf = params.m_boundFrame;

		Vector adjustLs;
		if (GetScaleAdjustLs(pCharacter, &adjustLs))
		{
			const Vector adjustWs = bf.GetLocator().TransformVector(adjustLs);
			bf.AdjustTranslation(adjustWs);
		}
	}

	// Set up the animation fade
	FadeToStateParams fadeParams;
	fadeParams.m_apRef           = bf;
	fadeParams.m_stateStartPhase = params.m_startPhase;
	fadeParams.m_customApRefId   = params.m_customApRefId;
	fadeParams.m_apRefValid      = params.m_isBfValid;

	const F32 animDuration = pAnim ? GetDuration(pAnim) : kBlendTimeDefault;
	GetBlend(&params.m_blend, animDuration, &fadeParams);

	// Make sure the character can be taken over without any lingering interference
	if (AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer())
	{
		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
	}

	m_animAction.ClearSelfBlendParams();
	m_animAction.FadeToState(pAnimControl, params.m_stateId, fadeParams, params.m_finishCondition);

	// Set up the self-blend
	if (params.m_isSelfBlendBfValid)
	{
		DC::SelfBlendParams selfBlendParams;

		if (params.m_selfBlend.m_curve != DC::kAnimCurveTypeInvalid)
		{
			selfBlendParams = params.m_selfBlend;
		}
		else
		{
			const F32 totalAnimTime = pAnim ?
									  pAnim->m_pClipData->m_fNumFrameIntervals * pAnim->m_pClipData->m_secondsPerFrame :
									  0.0f;
			const F32 phaseMax = 1.0f;
			const F32 animTime = Limit01(phaseMax - params.m_startPhase) * totalAnimTime;

			selfBlendParams.m_time  = animTime;
			selfBlendParams.m_phase = params.m_startPhase;
			selfBlendParams.m_curve = DC::kAnimCurveTypeUniformS;
		}

		BoundFrame sbBf = params.m_selfBlendBoundFrame;

		Vector adjustLs;
		if (GetScaleAdjustLs(pCharacter, &adjustLs))
		{
			const Vector adjustWs = sbBf.GetLocator().TransformVector(adjustLs);
			sbBf.AdjustTranslation(adjustWs);
		}

		m_animAction.SetSelfBlendParams(&selfBlendParams, sbBf, pCharacter, 1.0f);
	}

	m_status.m_isTeleporting          = false;
	m_status.m_blendForcedDefaultIdle = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Wait for a self-blend transition to be taken (just entered the first frame of the self-blend anim)
bool AiClimbController::HasSelfBlendStarted(const AnimActionWithSelfBlend& animAction) const
{
	AI_ASSERT( animAction.IsValid() );

	// Assumes kFinishOnTransitionTaken so anim has started and we just need to wait for the self-blend to start
	if (animAction.IsDone() && !animAction.IsSelfBlendPending())
	{
		NdGameObject* pCharacter   = GetCharacterGameObject();
		AnimControl* pAnimControl  = pCharacter->GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

		// The anim action is considered done when a request is sent to the underlying layer but the layer may not have
		// processed the request yet.  Make sure there are no pending transitions on the layer to handle this case.
// TODO(MB) JB notes that this is a good candidate to be handled internally in anim-action.cpp.
		if (!pBaseLayer->AreTransitionsPending())
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::SetAnimSpeedScale(F32 scale)
{
	NdGameObject* pCharacter  = GetCharacterGameObject();
	AnimControl* pAnimControl = pCharacter->GetAnimControl();

	DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>();
	AI_ASSERT( pInfo );

	pInfo->m_speedFactor = scale;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AiClimbController::GetAnimSpeedScale() const
{
	NdGameObject* pCharacter  = GetCharacterGameObject();
	AnimControl* pAnimControl = pCharacter->GetAnimControl();

	DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>();
	AI_ASSERT( pInfo );

	return pInfo->m_speedFactor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Adjust the wall shimmy bound frame a fixed distance away from the wall
void AiClimbController::AdjustWallShimmyBf(BoundFrame* pBoundFrame, F32 distFromWall) const
{
	AI_ASSERT( pBoundFrame );

	const F32 kProbeDist       = 1.2f;
	const F32 kProbeFootHeight = 0.1f;
	const F32 kProbeHeadHeight = 1.7f;

	const NavCharacter* pNavChar = GetCharacter();

	const CollideFilter collideFilter = CollideFilter(Collide::kLayerMaskGeneral, Pat((1ULL << Pat::kPassThroughShift)), GetCharacterGameObject());
	const Vector probeDirWs           = GetLocalZ(pNavChar->GetRotation());
	const Vector probeWs              = probeDirWs * kProbeDist;

	SimpleCastProbe probes[2];
	probes[0].m_pos    = pBoundFrame->GetTranslation() + Vector(0.0f, kProbeFootHeight, 0.0f);
	probes[0].m_vec    = probeWs;
	probes[0].m_radius = 0.0f;
	probes[1].m_pos    = pBoundFrame->GetTranslation() + Vector(0.0f, kProbeHeadHeight, 0.0f);
	probes[1].m_vec    = probeWs;
	probes[1].m_radius = 0.0f;

// g_prim.Draw(DebugArrow(probes[0].m_pos, probes[0].m_vec), Seconds(1.5f));
// g_prim.Draw(DebugArrow(probes[1].m_pos, probes[1].m_vec), Seconds(1.5f));

	RayCastJob job;
	SimpleCastKick(job, probes, 2, collideFilter, ICollCastJob::kCollCastSynchronous);	//TODO(MB) Synchronous!
	job.Wait();
	SimpleCastGather(job, probes, 2);

	I32F index = -1;

	if (probes[0].m_cc.m_time >= 0.0f)
	{
		if (probes[1].m_cc.m_time >= 0.0f)
		{
			index = probes[0].m_cc.m_time < probes[1].m_cc.m_time ? 0 : 1;
		}
		else
		{
			index = 0;
		}
	}
	else if (probes[1].m_cc.m_time >= 0.0f)
	{
		index = 1;
	}

	if (index >= 0)
	{
		const Vector offsetWs = probes[index].m_cc.m_contact - probes[index].m_pos - (distFromWall * probeDirWs);
		pBoundFrame->AdjustTranslation(offsetWs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::AdjustSlopeBf(BoundFrame* pBoundFrame, NavLedgeReference edge)
{
	AI_ASSERT( pBoundFrame );

	const F32 wallSlopeAngle  = GetWallSlopeAngle(edge, pBoundFrame->GetLocator());
	const Quat wallSlopeRotPs = QuatFromAxisAngle(kUnitXAxis, wallSlopeAngle);

	pBoundFrame->AdjustRotationPs(wallSlopeRotPs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::AllowIdleEnter() const
{
	const bool isReaching             = m_status.m_wantReachEnd || IsState(kStateClimbToReach);
	const bool isWallShimmyTransition = IsState(kStateWallShimmyToClimb) || IsState(kStateClimbToWallShimmy);
	const bool areLimbsOnSameEdge     = m_status.m_isInWallShimmy ?
										m_status.m_sourceEdgeFoot[kLeftLeg] == m_status.m_sourceEdgeFoot[kRightLeg] :
										m_status.m_sourceEdgeHand[kLeftArm] == m_status.m_sourceEdgeHand[kRightArm];

	const bool allowIdleEnter = !isReaching && !isWallShimmyTransition && areLimbsOnSameEdge;

	return allowIdleEnter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::AllowIdleExit() const
{
	return GetCharacter()->GetCurTime() >= m_status.m_allowIdleExitTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::UpdateTransitionArmIk(ArmIndex armIndex, F32 phase, F32 startPhase, F32 endPhase)
{
	AiClimbController::IkTargetInfo& ikInfo = m_status.m_ikInfo;

	const NavLedgeReference sourceEdge = m_status.m_sourceEdgeHand[armIndex];
	const NavLedgeReference targetEdge = m_status.m_targetEdgeHand[armIndex];

	if (m_status.m_wantReachEnd)
	{
		ikInfo.m_handEdgeVert0Ws[armIndex]        = targetEdge.GetVert0();
		ikInfo.m_handEdgeVert1Ws[armIndex]        = targetEdge.GetVert1();
		ikInfo.m_handEdgeWallNormalWs[armIndex]   = targetEdge.GetWallNormal();
		ikInfo.m_handEdgeWallBinormalWs[armIndex] = targetEdge.GetWallBinormal();
	}
	else if (phase <= startPhase)
	{
		ikInfo.m_handEdgeVert0Ws[armIndex]        = sourceEdge.GetVert0();
		ikInfo.m_handEdgeVert1Ws[armIndex]        = sourceEdge.GetVert1();
		ikInfo.m_handEdgeWallNormalWs[armIndex]   = sourceEdge.GetWallNormal();
		ikInfo.m_handEdgeWallBinormalWs[armIndex] = sourceEdge.GetWallBinormal();
	}
	else if (phase >= endPhase)
	{
		ikInfo.m_handEdgeVert0Ws[armIndex]        = targetEdge.GetVert0();
		ikInfo.m_handEdgeVert1Ws[armIndex]        = targetEdge.GetVert1();
		ikInfo.m_handEdgeWallNormalWs[armIndex]   = targetEdge.GetWallNormal();
		ikInfo.m_handEdgeWallBinormalWs[armIndex] = targetEdge.GetWallBinormal();
	}
	else
	{
		const F32 tt = LerpScale(startPhase, endPhase, 0.0f, 1.0f, phase);

		ikInfo.m_handEdgeVert0Ws[armIndex]        = Lerp(sourceEdge.GetVert0(), targetEdge.GetVert0(), tt);
		ikInfo.m_handEdgeVert1Ws[armIndex]        = Lerp(sourceEdge.GetVert1(), targetEdge.GetVert1(), tt);
		ikInfo.m_handEdgeWallNormalWs[armIndex]   = Lerp(sourceEdge.GetWallNormal(), targetEdge.GetWallNormal(), tt);
		ikInfo.m_handEdgeWallBinormalWs[armIndex] = Lerp(sourceEdge.GetWallBinormal(), targetEdge.GetWallBinormal(), tt);
	}

	ikInfo.m_handIkStrength[armIndex] = 1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::UpdateTransitionIkInfo()
{
	const NavCharacter* pNavChar = GetCharacter();
	AI_ASSERT( pNavChar );

	const AnimStateInstance* pDestInstance = m_animAction.GetTransitionDestInstance(pNavChar->GetAnimControl());
	const F32 phase                        = pDestInstance ? Limit01(pDestInstance->Phase()) : 0.0f;

	const DC::ClimbInfo* pClimbInfo   = GetClimbInfo();
	const DC::ClimbTransition* pClimb = &pClimbInfo->m_climbTransitions->m_array[m_status.m_animIndex];

	const ArmIndex leftArmIndex  = m_status.m_mirrored ? kRightArm : kLeftArm;
	const ArmIndex rightArmIndex = m_status.m_mirrored ? kLeftArm : kRightArm;

	UpdateTransitionArmIk(leftArmIndex, phase, pClimb->m_handLIkBlendStart, pClimb->m_handLIkBlendEnd);
	UpdateTransitionArmIk(rightArmIndex, phase, pClimb->m_handRIkBlendStart, pClimb->m_handRIkBlendEnd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Interrupted
/// --------------------------------------------------------------------------------------------------------------- ///

class AiClimbController::Interrupted : public AiClimbController::BaseState
{
	virtual void OnEnter() override;
	virtual void OnExit() override;
	virtual void OnInterrupted() override;
	
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Interrupted::OnEnter()
{
	AiClimbController* pSelf = GetSelf();

	NavCharacter* pNavChar = pSelf->GetCharacter();

	pSelf->m_activePending = false;

	if (IAiWeaponController* pWeaponController = pNavChar->GetAnimationControllers()->GetWeaponController())
	{
		pWeaponController->AllowGunOut(true);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Interrupted::OnExit()
{
	AiClimbController* pSelf = GetSelf();

	pSelf->m_activePending = false;

	pSelf->PrepareToClimb();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Interrupted::OnInterrupted()
{
	///Do Nothing
}

FSM_STATE_REGISTER(AiClimbController, Interrupted, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// Transition
/// --------------------------------------------------------------------------------------------------------------- ///

class AiClimbController::Transition : public AiClimbController::BaseState
{
	virtual void OnEnter() override;
	virtual void OnUpdate() override;

	F32 FindSpeedScale(const DC::ClimbTransition* pClimb, const Locator& startLocWs, const Locator& endLocWs) const;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Transition::OnEnter()
{
	const F32 kMinBlendTime = 0.3f;

	AiClimbController* pSelf = GetSelf();

	const NavCharacter* pNavChar = pSelf->GetCharacter();

//TODO(MB) Could do two passes, one for one arm on new edge, one for two arms. For now just do two arm pass.
	const bool success = pSelf->FindBestTransition(&pSelf->m_status);
	if (!success)
	{
		pSelf->OnCommandFailed("No valid transition anims - ABORTING");
		return;
	}

	const StringId64 animId       = pSelf->m_status.m_animId;
	const bool isMirrored         = pSelf->m_status.m_mirrored;
	const BoundFrame sourceEdgeBf = pSelf->FindLimbSourceBf();
	const BoundFrame targetEdgeBf = pSelf->FindLimbTargetBf();

	// Find self-blend start and end phases
	const ArtItemAnim* pAnim          = pNavChar->GetAnimControl()->LookupAnim(animId).ToArtItem();
	const DC::ClimbInfo* pClimbInfo   = GetClimbInfo();
	const DC::ClimbTransition* pClimb = &pClimbInfo->m_climbTransitions->m_array[pSelf->m_status.m_animIndex];

	const F32 minDetachPhase  = Min(pClimb->m_handLIkBlendStart, pClimb->m_handRIkBlendStart);
	const F32 maxDetachPhase  = Max(pClimb->m_handLIkBlendStart, pClimb->m_handRIkBlendStart);
	const F32 attachPhase     = Min(pClimb->m_handLIkBlendEnd, pClimb->m_handRIkBlendEnd);
	const F32 blendPhaseDelta = attachPhase > maxDetachPhase ? attachPhase - maxDetachPhase : attachPhase - minDetachPhase;
	/*const*/ F32 blendStartPhase = attachPhase > maxDetachPhase ? maxDetachPhase : minDetachPhase;

//TODO(MB) HACK - Ship it!
	if (animId == SID("hang-dyno-med-unsafe-200-1h"))
	{
		blendStartPhase = 0.055f;
	}

	DC::SelfBlendParams selfBlendParams;
	selfBlendParams.m_time  = Max(GetDuration(pAnim) * blendPhaseDelta, kMinBlendTime);
	selfBlendParams.m_phase = blendStartPhase;
	selfBlendParams.m_curve = DC::kAnimCurveTypeUniformS;

	Locator animApRef(kIdentity);
	const SkeletonId skelId = pNavChar->GetSkeletonId();
	const Locator alignLoc  = pNavChar->GetLocator();
	if (!FindApReferenceFromAlign(skelId, pAnim, alignLoc, &animApRef, 0.0f, isMirrored))
	{
		AI_ASSERT( false );
		ClimbLog("%s: %s is missing ap-ref\n", pNavChar->GetName(), DevKitOnly_StringIdToString(animId));
	}

	BoundFrame startBf(animApRef, sourceEdgeBf.GetBinding());
	BoundFrame endBf(targetEdgeBf);

	const NavLedgeReference sourceEdge = pSelf->m_status.m_isInWallShimmy           ?
										 pSelf->m_status.m_sourceEdgeFoot[kLeftLeg] :
										 pSelf->m_status.m_sourceEdgeHand[kLeftArm];
	const NavLedgeReference targetEdge = pSelf->m_status.m_isInWallShimmy           ?
										 pSelf->m_status.m_targetEdgeFoot[kLeftLeg] :
										 pSelf->m_status.m_targetEdgeHand[kLeftArm];

	// Adjust the starting ap so the limbs remain on the edge
	if (pSelf->m_status.m_wantOutsideCorner || pSelf->m_status.m_wantInsideCorner)
	{
		// NB: The ap-ref for corners is at the destination location, some distance along the target edge. We need to
		//     find that distance. As an approximation, we get the ap-ref of the anim relative to the character align,
		//     then project that onto the target edge. It'll be close if the anim and edge are close to the same angle.

		Point edgePos0 = targetEdge.GetVert0();
		Point edgePos1 = targetEdge.GetVert1();
		OffsetPoints(&edgePos0, &edgePos1);

		const Point edgePosWs = ClosestPointOnEdgeToPoint(edgePos0, edgePos1, targetEdgeBf.GetTranslation());

		endBf = targetEdgeBf;
		endBf.SetTranslation(edgePosWs);

		if (pSelf->m_status.m_wantOutsideCorner)
		{
			startBf = endBf;
			startBf.SetRotation(animApRef.GetRotation());
			startBf.SetTranslation(edgePosWs);
		}
		else if (pSelf->m_status.m_isInWallShimmy)
		{
			const Vector limbOffsetWs = animApRef.GetTranslation() - endBf.GetTranslation();
			const Vector limbOffsetLs = animApRef.UntransformVector(limbOffsetWs);
			pSelf->AdjustWallShimmyBf(&endBf, limbOffsetLs.Z());
		}
	}
	else if (pSelf->m_status.m_isInWallShimmy && pSelf->m_status.m_wantWallShimmy)
	{
		// Wall shimmy transition anims are offset from the ap-ref so no additional wall offset is needed
		pSelf->AdjustWallShimmyBf(&startBf, 0.0f);
		pSelf->AdjustWallShimmyBf(&endBf, 0.0f);
	}
	else if (pSelf->m_status.m_wantReachEnd)
	{
		// Reach ap is half-way between startBf and endBf with Y set to endBf
		Locator reachLocWs = Lerp(startBf.GetLocator(), endBf.GetLocator(), 0.5f);
		const Point posWs  = reachLocWs.GetPosition();
		reachLocWs.SetPos(Point(posWs.X(), endBf.GetTranslation().Y(), posWs.Z()));

		endBf.SetLocator(reachLocWs);
	}

	pSelf->AdjustSlopeBf(&startBf, sourceEdge);
	pSelf->AdjustSlopeBf(&endBf, targetEdge);

	AnimParams params;
	params.m_animationId         = animId;
	params.m_boundFrame          = startBf;
	params.m_stateId             = SID("s_climb-state");
	params.m_finishCondition     = AnimAction::kFinishOnTransitionTaken;
	params.m_isMirrored          = isMirrored;
	params.m_selfBlend           = selfBlendParams;
	params.m_selfBlendBoundFrame = endBf;
	params.m_isBfValid           = true;
	params.m_isSelfBlendBfValid  = true;

	pSelf->PlayAnimation(params);

	// Adjust the speed scale
	const F32 speedScale = FindSpeedScale(pClimb, startBf.GetLocator(), endBf.GetLocator());
	pSelf->SetAnimSpeedScale(speedScale);

	pSelf->UpdateTransitionIkInfo();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Transition::OnUpdate()
{
	AiClimbController* pSelf = GetSelf();

	// Wait for the enter self-blend to start
	if (pSelf->HasSelfBlendStarted(pSelf->m_animAction))
	{
		pSelf->GoWaitForTransition();
	}

	pSelf->UpdateTransitionIkInfo();
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AiClimbController::Transition::FindSpeedScale(const DC::ClimbTransition* pClimb,
												  const Locator& startLocWs,
												  const Locator& endLocWs) const
{
	// Climb at requested speed
	return pClimb->m_climbSpeedMult;

// 	AiClimbController* pSelf = GetSelf();
// 
// 	// -- Adjust speed based on anim dist vs actual dist --
// 
// 	Point animCenterOfMassStart, animCenterOfMassEnd;
// 	GetClimbCenterOfMass(pClimb, &animCenterOfMassStart, &animCenterOfMassEnd, pSelf->m_status.m_mirrored);
// 	const Vector animCenterOfMassMovement = Rotate(endLocWs.Rot(), animCenterOfMassEnd - animCenterOfMassStart);
// 
// 	const Point climbStartCenterOfMass     = startLocWs.TransformPoint(animCenterOfMassStart);
// 	const Point climbEndCenterOfMass       = endLocWs.TransformPoint(animCenterOfMassEnd);
// 	const Vector climbCenterOfMassMovement = climbEndCenterOfMass - climbStartCenterOfMass;
// 
// 	const F32 animDist  = Length(animCenterOfMassMovement);
// 	const F32 climbDist = Length(climbCenterOfMassMovement);
// 	const F32 distRatio = animDist / climbDist;
// 
// 	const F32 blendPct    = pClimb->m_centerOfMassBlendEnd - pClimb->m_centerOfMassBlendStart;
// 	F32 animDistSpeedMult = Lerp(1.0f, distRatio, blendPct);
// 	animDistSpeedMult     = MinMax(animDistSpeedMult, 0.5f, 1.1f);		// Values from the player
// 	
// 	// -- Adjust speed based on whether the edge is unsafe --
// 
// 	const bool isEdgeUnsafe = EdgeIsUnsafe(pSelf->m_status.m_sourceEdgeHand[kLeftArm]) ||
// 							  EdgeIsUnsafe(pSelf->m_status.m_sourceEdgeHand[kRightArm]);	
// 	const F32 edgeSpeedMult = isEdgeUnsafe ? pClimb->m_climbSpeedMultUnsafe : pClimb->m_climbSpeedMult;
// 
// 	// -- Compute the final speed mult --
// 
// 	const F32 speedMult = animDistSpeedMult * edgeSpeedMult;
// 
// 	return speedMult;
}

FSM_STATE_REGISTER(AiClimbController, Transition, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// WaitForTransition
/// --------------------------------------------------------------------------------------------------------------- ///

class AiClimbController::WaitForTransition : public AiClimbController::BaseState
{
	virtual void OnUpdate() override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::WaitForTransition::OnUpdate()
{
	AiClimbController* pSelf = GetSelf();

	AI_ASSERTF( pSelf->GetBaseAnimStateId() == SID("s_climb-state") ||
			    pSelf->GetBaseAnimStateId() == SID("s_death-state") ||
				pSelf->GetBaseAnimStateId() == SID("s_idle-zen"),
				("Unexpected anim state '%s'", DevKitOnly_StringIdToString(pSelf->GetBaseAnimStateId())) );

	const NavCharacter* pNavChar = pSelf->GetCharacter();
	AnimControl* pAnimControl    = pNavChar->GetAnimControl();

	if (const AnimStateInstance* pDestInstance = pSelf->m_animAction.GetTransitionDestInstance(pAnimControl))
	{
		const F32 kSettleRatio      = 0.25f;				// Extend the end frame between transitions
		const F32 kPauseSettleRatio = 0.8f;					// Extend the end frame before pausing

		const DC::ClimbInfo* pClimbInfo   = GetClimbInfo();
		const DC::ClimbTransition* pClimb = &pClimbInfo->m_climbTransitions->m_array[pSelf->m_status.m_animIndex];

		bool allowExit = true;

		if (const ArtItemAnim* pAnim = pDestInstance->GetPhaseAnimArtItem().ToArtItem())
		{
			const F32 mayaFramesPerSample = pAnim->m_pClipData->m_secondsPerFrame * 30.0f;
			const F32 mayaFrameIntervals  = pAnim->m_pClipData->m_fNumFrameIntervals * mayaFramesPerSample;
			const I32 mayaFrames          = mayaFrameIntervals + 0.5f;

			// Extend the transition end frame to let the anim settle to give it more weight
			// Extend it more when paused to show off the transition specific settle performances
			const I32 endFrame = !pSelf->IsClimbing()           ?
								 mayaFrames * kPauseSettleRatio :
								 pClimb->m_climbInterruptEndFrame >= 0                        ?
								 pClimb->m_climbInterruptEndFrame + mayaFrames * kSettleRatio :
								 mayaFrames;
			const F32 endPhase = Limit01((F32)endFrame / mayaFrames);
			const F32 curPhase = pDestInstance->Phase();

			allowExit = curPhase >= endPhase;
		}

		// Continue when the interrupt phase is reached
		if (allowExit)
		{
			const bool isToLedgeFacing = (pClimb->m_flags & DC::kClimbFlagsToLedgeFacing) != 0;
			pSelf->m_status.m_isInWallShimmy = isToLedgeFacing;
			pSelf->m_status.m_wantReachEnd   = false;
			pSelf->GoSelectTransition();
		}
	}

	pSelf->UpdateTransitionIkInfo();
}

FSM_STATE_REGISTER(AiClimbController, WaitForTransition, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// ClimbToReach
/// --------------------------------------------------------------------------------------------------------------- ///

class AiClimbController::ClimbToReach : public AiClimbController::BaseState
{
	virtual void OnEnter() override;
	virtual void OnUpdate() override;

	Vec2 FindLeanVecLs(bool* pOutIsMirrored) const;
	void UpdateAnimPhase(const Vec2& leanVec);

	void UpdateIkInfo() const;

	F32 m_elapsedTime;
	F32 m_totalTime;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::ClimbToReach::OnEnter()
{
	AiClimbController* pSelf = GetSelf();

	// The animation will start playing this frame so include the delta time of this frame in the elapsed time 
	m_elapsedTime = GetProcessDeltaTime();
	m_totalTime   = kBlendTimeDefault * pSelf->GetAnimSpeedScale();

	bool isMirrored = false;

	const StringId64 animStateId = SID("s_climb-lean-arm-state");
	const Vec2 leanVecLs         = FindLeanVecLs(&isMirrored);

	// Update the target for the fixed hand
	const I32F fixedHand = isMirrored ? kRightArm : kLeftArm;
	const I32F climbHand = !fixedHand;

	NavLedgeReference sourceEdge = pSelf->m_status.m_sourceEdgeHand[fixedHand];
	BoundFrame sourceBf          = pSelf->m_status.m_sourceEdgeHandBf[fixedHand];

	pSelf->m_status.m_targetEdgeHand[fixedHand]   = sourceEdge;
	pSelf->m_status.m_targetEdgeHandBf[fixedHand] = sourceBf;

	pSelf->m_status.m_mirrorReach = isMirrored;

	UpdateAnimPhase(leanVecLs);

	pSelf->AdjustSlopeBf(&sourceBf, sourceEdge);

	AnimParams params;
	params.m_boundFrame = sourceBf;
	params.m_stateId    = animStateId;
	params.m_isMirrored = isMirrored;
	params.m_isBfValid  = true;

	pSelf->PlayAnimation(params);

	UpdateIkInfo();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::ClimbToReach::OnUpdate()
{
	AiClimbController* pSelf = GetSelf();

	m_elapsedTime += GetProcessDeltaTime();
	if (m_elapsedTime > m_totalTime)
	{
		m_elapsedTime = m_totalTime;
	}

	if (m_elapsedTime >= m_totalTime)
	{
		pSelf->m_status.m_wantReachEnd = true;
		pSelf->GoSelectTransition();
	}

	UpdateIkInfo();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vec2 AiClimbController::ClimbToReach::FindLeanVecLs(bool* pOutIsMirrored) const
{
	AI_ASSERT( pOutIsMirrored );

	AiClimbController* pSelf = GetSelf();

	const BoundFrame sourceEdgeBf = pSelf->FindLimbSourceBf();
	const BoundFrame targetEdgeBf = pSelf->FindLimbTargetBf();

	const Point sourcePosWs = sourceEdgeBf.GetTranslation();
	const Vector toTargetWs = targetEdgeBf.GetTranslation() - sourcePosWs;
	const Vector toTargetLs = sourceEdgeBf.GetLocator().UntransformVector(toTargetWs);

	*pOutIsMirrored = toTargetLs.X() > 0.0f;
	const Vec2 leanVecLs(*pOutIsMirrored ? toTargetLs.X() : -toTargetLs.X(), toTargetLs.Y());

	return leanVecLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::ClimbToReach::UpdateAnimPhase(const Vec2& leanVec)
{
	AiClimbController* pSelf = GetSelf();

	NdGameObject* pCharacter  = pSelf->GetCharacterGameObject();
	AnimControl* pAnimControl = pCharacter->GetAnimControl();

	DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>();
	AI_ASSERT( pInfo );

	const DC::PlayerEdgeHangSettings* pHangSettings = GetEdgeHangSettings();
	AI_ASSERT( pHangSettings );

	const F32 reach2Pct = pHangSettings->m_reach2Pct;
	const F32 reach4Pct = pHangSettings->m_reach4Pct;
	const F32 reach5Pct = pHangSettings->m_reach5Pct;
	const F32 reach6Pct = pHangSettings->m_reach6Pct;

	const F32 leanBlend = leanVec.Length();

	// Set blend factors based on distance
	pInfo->m_climb.m_animBlendReach0 = LerpScaleClamp(     0.0f,     0.20f, 0.0f, 1.0f, leanBlend);
	pInfo->m_climb.m_animBlendReach2 = LerpScaleClamp(     0.0f, reach2Pct, 0.0f, 1.0f, leanBlend);
	pInfo->m_climb.m_animBlendReach4 = LerpScaleClamp(reach2Pct, reach4Pct, 0.0f, 1.0f, leanBlend);
	pInfo->m_climb.m_animBlendReach5 = LerpScaleClamp(reach4Pct, reach5Pct, 0.0f, 1.0f, leanBlend);
	pInfo->m_climb.m_animBlendReach6 = LerpScaleClamp(reach5Pct, reach6Pct, 0.0f, 1.0f, leanBlend);

	F32 angleRad = Atan2(leanVec.x, leanVec.y);
	F32 angleDeg = RadiansToDegrees(angleRad);

	if (angleDeg <= -90.0f)
	{
		angleDeg += 360.0f;
	}

	const F32 minReachAngle = pSelf->m_status.m_isInWallShimmy ? kLedgeFacingMinReachAngle : kEdgeMinReachAngle;
	const F32 maxReachAngle = pSelf->m_status.m_isInWallShimmy ? kLedgeFacingMaxReachAngle : kEdgeMaxReachAngle;

	angleDeg = MinMax(angleDeg, minReachAngle, maxReachAngle);

	// Set phase based on angle
	pInfo->m_climb.m_leanAnimPhase = LerpScaleClamp(minReachAngle, maxReachAngle, 1.0f, 0.0f, angleDeg);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::ClimbToReach::UpdateIkInfo() const
{
	AiClimbController* pSelf = GetSelf();

	const ArmIndex leftArmIndex  = pSelf->m_status.m_mirrorReach ? kRightArm : kLeftArm;
	const ArmIndex rightArmIndex = pSelf->m_status.m_mirrorReach ? kLeftArm : kRightArm;

	AiClimbController::IkTargetInfo& ikInfo = pSelf->m_status.m_ikInfo;

	// Fixed hand
	{
		const NavLedgeReference sourceEdge = pSelf->m_status.m_sourceEdgeHand[leftArmIndex];

		ikInfo.m_handEdgeVert0Ws[leftArmIndex]        = sourceEdge.GetVert0();
		ikInfo.m_handEdgeVert1Ws[leftArmIndex]        = sourceEdge.GetVert1();
		ikInfo.m_handEdgeWallNormalWs[leftArmIndex]   = sourceEdge.GetWallNormal();
		ikInfo.m_handEdgeWallBinormalWs[leftArmIndex] = sourceEdge.GetWallBinormal();
		ikInfo.m_handIkStrength[leftArmIndex]         = 1.0f;
	}

	// Climb hand
	if (false)
	{
		const NavLedgeReference sourceEdge = pSelf->m_status.m_sourceEdgeHand[rightArmIndex];
		const NavLedgeReference targetEdge = pSelf->m_status.m_targetEdgeHand[rightArmIndex];

		const F32 tt = LerpScale(0.0f, m_totalTime, 0.0f, 1.0f, m_elapsedTime);

		ikInfo.m_handEdgeVert0Ws[rightArmIndex]        = Lerp(sourceEdge.GetVert0(), targetEdge.GetVert0(), tt);
		ikInfo.m_handEdgeVert1Ws[rightArmIndex]        = Lerp(sourceEdge.GetVert1(), targetEdge.GetVert1(), tt);
		ikInfo.m_handEdgeWallNormalWs[rightArmIndex]   = Lerp(sourceEdge.GetWallNormal(), targetEdge.GetWallNormal(), tt);
		ikInfo.m_handEdgeWallBinormalWs[rightArmIndex] = Lerp(sourceEdge.GetWallBinormal(), targetEdge.GetWallBinormal(), tt);
		ikInfo.m_handIkStrength[rightArmIndex]         = 1.0f;
	}
	else
	{
		const NavLedgeReference sourceEdge = pSelf->m_status.m_sourceEdgeHand[rightArmIndex];
		const NavLedgeReference targetEdge = pSelf->m_status.m_targetEdgeHand[rightArmIndex];

		ikInfo.m_handEdgeVert0Ws[rightArmIndex]        = targetEdge.GetVert0();
		ikInfo.m_handEdgeVert1Ws[rightArmIndex]        = targetEdge.GetVert1();
		ikInfo.m_handEdgeWallNormalWs[rightArmIndex]   = targetEdge.GetWallNormal();
		ikInfo.m_handEdgeWallBinormalWs[rightArmIndex] = targetEdge.GetWallBinormal();
		ikInfo.m_handIkStrength[rightArmIndex]         = 1.0f;
	}
}

FSM_STATE_REGISTER(AiClimbController, ClimbToReach, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// Shimmy
/// --------------------------------------------------------------------------------------------------------------- ///

class AiClimbController::Shimmy : public AiClimbController::BaseState
{
	virtual void OnEnter() override;
	virtual void OnUpdate() override;

	StringId64 GetAnimId() const;
	StringId64 GetCurrentAnimId() const;

	F32 FindCurrentPhase() const;
	Vector FindApRefOffsetWs() const;
	Locator FindAlign(const NavLedgeReference& edge, const BoundFrame& edgeBf, Point* pOutEdgePosWs) const;
	Vector FindEdgeMoveWs(const NavLedgeReference& edge) const;

	bool DoesOldPathContainNew(const PathInfo& oldPathInfo, const PathInfo& newPathInfo) const;

	void UpdateArmIk(ArmIndex armIndex, StringId64 channelId);
	void UpdateIkInfo();

	AnimParams        m_animParams;
	DC::BlendParams   m_animBlendParams;
	NavLedgeReference m_edge;
	Point             m_lastHandPosWs[kArmCount];
	Point             m_edgeExitPosWs;
	Vector			  m_apRefOffsetWs;
	F32               m_elapsedTime;
	F32               m_totalTime;
	F32               m_animSpeed;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Shimmy::OnEnter()
{
	AiClimbController* pSelf = GetSelf();

	const NavCharacter* pNavChar = pSelf->GetCharacter();

	const NavLedgeReference sourceEdge = pSelf->m_status.m_isInWallShimmy           ?
										 pSelf->m_status.m_sourceEdgeFoot[kLeftLeg] :
										 pSelf->m_status.m_sourceEdgeHand[kLeftArm];
	const NavLedgeReference targetEdge = pSelf->m_status.m_isInWallShimmy ?
										 pSelf->m_status.m_targetEdgeFoot[kLeftLeg] :
										 pSelf->m_status.m_targetEdgeHand[kLeftArm];

	m_edgeExitPosWs = pSelf->FindLimbTargetBf().GetTranslation();
	m_edge          = sourceEdge;
	m_animSpeed     = 0.0f;

	Point edgePos0 = m_edge.GetVert0();
	Point edgePos1 = m_edge.GetVert1();

	// If moving to a different edge, set the exit position to the end of the source edge
	if (targetEdge != m_edge)
	{
		DistPointSegment(m_edgeExitPosWs, edgePos0, edgePos1, &m_edgeExitPosWs);
	}

	// Adjust the translation to match the closest position of the animation to the edge
	Point edgePosWs;
	BoundFrame limbAnimBf = pSelf->FindLimbAnimBf();
	DistPointSegment(limbAnimBf.GetTranslation(), edgePos0, edgePos1, &edgePosWs);

	BoundFrame edgeBf = pSelf->FindLimbSourceBf();
	edgeBf.SetTranslation(edgePosWs);

	// Must refresh m_wantHang because GoSelectTransition tests the target edge
	pSelf->m_status.m_wantHang = pSelf->IsLedgeHang(m_edge, edgeBf.GetLocator());
	pSelf->m_status.m_animId   = GetAnimId();

	const SkeletonId skelId	 = pNavChar->GetSkeletonId();
	const ArtItemAnim* pAnim = pNavChar->GetAnimControl()->LookupAnim(pSelf->m_status.m_animId).ToArtItem();

	// Find the edge span
	const Vector edgeSpanWs = m_edgeExitPosWs - edgeBf.GetTranslation();
	const F32 edgeSpanLen   = Length(edgeSpanWs);

	// Find the animation span aligned with the edge (using the ap-ref would be more accurate)
	Locator animStartLocLs(kIdentity), animEndLocLs(kIdentity);
	EvaluateChannelInAnim(skelId, pAnim, SID("align"), 0.0f, &animStartLocLs);
	EvaluateChannelInAnim(skelId, pAnim, SID("align"), 1.0f, &animEndLocLs);
	const Point animStartPosLs = animStartLocLs.Pos();
	const Point animEndPosLs   = animEndLocLs.Pos();
	const Vector animSpanLs    = animEndPosLs - animStartPosLs;
	const Vector animSpanWs    = edgeBf.GetLocator().TransformVector(animSpanLs);
	const F32 animSpanLen      = Length(animSpanLs);

	// Update mirroring
	pSelf->m_status.m_mirrorShimmy = Dot(animSpanWs, edgeSpanWs) < 0.0f;

	// Update duration and speed
	const F32 timeScale    = edgeSpanLen / animSpanLen / pSelf->GetAnimSpeedScale();
	const F32 animSpanTime = GetDuration(pAnim);

	m_elapsedTime = GetProcessDeltaTime();		// Anim starts immediately so include the delta time of this frame
	m_totalTime   = animSpanTime * timeScale;
	m_animSpeed   = animSpanLen / animSpanTime;

	// Play the shimmy anim
	m_animBlendParams.m_animFadeTime   = 0.3f;
	m_animBlendParams.m_motionFadeTime = -1.0f;
	m_animBlendParams.m_curve          = DC::kAnimCurveTypeLinear;

	m_animParams.m_animationId = pSelf->m_status.m_animId;
	m_animParams.m_startPhase  = FindCurrentPhase();
	m_animParams.m_stateId     = SID("s_climb-tilt-no-ref-state");
	m_animParams.m_isMirrored  = pSelf->m_status.m_mirrorShimmy;
	m_animParams.m_blend       = m_animBlendParams;

	pSelf->PlayAnimation(m_animParams);

	// Don't initialize the hand positions when continuing shimmy
	const StringId64 prevStateId = pSelf->GetStateMachine().GetPrevStateId();
	if (prevStateId != SID("Shimmy"))
	{
		Locator locLs[kArmCount];
		EvaluateChannelInAnim(skelId, pAnim, SID("apReference-hand-l"), m_animParams.m_startPhase, &locLs[kLeftArm]);
		EvaluateChannelInAnim(skelId, pAnim, SID("apReference-hand-r"), m_animParams.m_startPhase, &locLs[kRightArm]);
		m_lastHandPosWs[kLeftArm]  = pNavChar->GetLocator().TransformPoint(locLs[kLeftArm].GetTranslation());
		m_lastHandPosWs[kRightArm] = pNavChar->GetLocator().TransformPoint(locLs[kRightArm].GetTranslation());		
	}

	UpdateIkInfo();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Shimmy::OnUpdate()
{
	AiClimbController* pSelf = GetSelf();

	NavCharacter* pNavChar = pSelf->GetCharacter();

	AI_ASSERTF( pSelf->GetBaseAnimStateId() == SID("s_climb-tilt-no-ref-state") ||
			    pSelf->GetBaseAnimStateId() == SID("s_idle-zen"),
				("Unexpected anim state '%s'", DevKitOnly_StringIdToString(pSelf->GetBaseAnimStateId())) );

	m_elapsedTime += GetProcessDeltaTime();
	if (m_elapsedTime > m_totalTime)
	{
		m_elapsedTime = m_totalTime;
	}

	m_apRefOffsetWs = FindApRefOffsetWs();

	const Point edgePos0 = m_edge.GetVert0();
	const Point edgePos1 = m_edge.GetVert1();

	// Adjust the translation to match the edge
	Point apRefPosWs = pNavChar->GetTranslation() + m_apRefOffsetWs;
	DistPointSegment(apRefPosWs, edgePos0, edgePos1, &apRefPosWs);

	BoundFrame edgeBf = pSelf->FindLimbSourceBf();
	edgeBf.SetTranslation(apRefPosWs);

	if (!DoesOldPathContainNew(pSelf->m_pendingPathInfo, pSelf->m_activePathInfo))
	{
		if (pSelf->UpdatePathWaypointsInternal(pSelf->m_pendingPathInfo))
		{
			pSelf->m_pendingPathInfo.Reset();

			pSelf->m_status.CopySourceToTarget();
			pSelf->GoSelectTransition();
		}
	}

	if (m_elapsedTime >= m_totalTime)
	{
		pSelf->m_status.m_shimmyExitEdgeIndex = m_edge.GetId();
		pSelf->GoSelectTransition();
	}
	else if (pSelf->AllowIdleEnter() && pSelf->IsBlocked(edgeBf.GetTranslation(), m_edgeExitPosWs, true))
	{
		pSelf->GoSelectIdle();
	}
	else if (pSelf->AllowIdleEnter() && !pSelf->IsClimbing())
	{
		pSelf->GoSelectIdle();
	}
	else
	{
		const StringId64 animId = GetAnimId();

		if (pSelf->m_animAction.IsDone() || animId != pSelf->m_status.m_animId)
		{
			// Reset the phase when starting an animation
			m_animParams.m_startPhase  = 0.0f;
			m_animParams.m_animationId = animId;

			pSelf->PlayAnimation(m_animParams);
			pSelf->m_status.m_animId = animId;
		}

		pSelf->AdjustSlopeBf(&edgeBf, m_edge);

		Point edgePosWs;
		Locator locWs = FindAlign(m_edge, edgeBf, &edgePosWs);

		const Vector forwardWs     = GetLocalZ(locWs.GetRotation());
		const Vector charForwardWs = GetLocalZ(pNavChar->GetRotation());
		const F32 forwardDot       = Dot(forwardWs, charForwardWs);

		// Blend locator to smooth out transitions between adjacent shimmy ledges
		if (forwardDot < Cos(DegreesToRadians(1.0f)))
		{
			const F32 kBlendTime = 0.15f;

			const F32 t = Limit01(GetProcessDeltaTime() / kBlendTime);
			locWs = Lerp(pNavChar->GetLocator(), locWs, t);
		}

		pNavChar->SetLocator(locWs);

		// Update source
		BoundFrame bf = edgeBf;
		bf.SetTranslation(edgePosWs);
		pSelf->m_status.SetSource(m_edge, bf);

		UpdateIkInfo();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiClimbController::Shimmy::GetAnimId() const
{
	AiClimbController* pSelf = GetSelf();

	//TODO(MB) Blend with short versions of these anims to adjust shimmy speed like the player in s-climb2-shimmy?
	const StringId64 animId = pSelf->m_status.m_isInWallShimmy ?
							  kWallShimmyAnimId				 :
							  pSelf->m_status.m_wantHang ?
							  kHangShimmyAnimId          :
							  kShimmyAnimId;

	return animId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiClimbController::Shimmy::GetCurrentAnimId() const
{
	AiClimbController* pSelf = GetSelf();

	const NavCharacter* pNavChar = pSelf->GetCharacter();
	AnimControl* pAnimControl    = pNavChar->GetAnimControl();

	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	if (!pBaseLayer)
	{
		return INVALID_STRING_ID_64;
	}

	AnimStateInstance* pInstance = pBaseLayer->CurrentStateInstance();
	if (!pInstance)
	{
		return INVALID_STRING_ID_64;
	}

	const AnimStateSnapshot* pTopSnapshot = &pInstance->GetAnimStateSnapshot();
	if (!pTopSnapshot)
	{
		return INVALID_STRING_ID_64;
	}

	const AnimSnapshotNode* pRootNode = pTopSnapshot->GetSnapshotNode(pTopSnapshot->m_rootNodeIndex);
	if (!pRootNode)
	{
		return INVALID_STRING_ID_64;
	}

	const AnimSnapshotNode* pAnimNode = pRootNode->FindFirstNodeOfKind(pTopSnapshot, SID("AnimSnapshotNodeAnimation"));
	const AnimSnapshotNodeAnimation* pAnimNodeAnim = static_cast<const AnimSnapshotNodeAnimation*>(pAnimNode);
	if (!pAnimNodeAnim)
	{
		return INVALID_STRING_ID_64;
	}

	const StringId64 animId = pAnimNodeAnim->GetAnimNameId();

	return animId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 AiClimbController::Shimmy::FindCurrentPhase() const
{
	AiClimbController* pSelf = GetSelf();

	const NavCharacter* pNavChar = pSelf->GetCharacter();

	const StringId64 animId    = pSelf->m_status.m_animId;
	const StringId64 curAnimId = GetCurrentAnimId();

	// If already playing the shimmy anim return the current phase of the anim
	const AnimStateInstance* pDestInstance = pSelf->m_animAction.GetTransitionDestInstance(pNavChar->GetAnimControl());
	const F32 curPhase = curAnimId == animId && pDestInstance ? pDestInstance->Phase() : 0.0f;

	return curPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector AiClimbController::Shimmy::FindApRefOffsetWs() const
{
	AiClimbController* pSelf = GetSelf();

	const StringId64 animId       = pSelf->m_status.m_animId;
	const NavCharacter* pNavChar  = pSelf->GetCharacter();
	const SkeletonId skelId       = pNavChar->GetSkeletonId();
	const ArtItemAnim* pAnim      = pNavChar->GetAnimControl()->LookupAnim(animId).ToArtItem();
	const BoundFrame sourceEdgeBf = pSelf->FindLimbSourceBf();

	const F32 curPhase    = FindCurrentPhase();
	const bool isMirrored = pSelf->m_status.m_mirrorShimmy;

	// Find the align relative to the source edge
	Locator animCurLocLs(kIdentity);
	EvaluateChannelInAnim(skelId, pAnim, SID("align"), curPhase, &animCurLocLs, isMirrored);
	const Locator alignLocWs = sourceEdgeBf.GetLocator().TransformLocator(animCurLocLs);

	// Find the ap-ref from the align
	Locator apRef(kIdentity);
	if (!FindApReferenceFromAlign(skelId, pAnim, alignLocWs, &apRef, curPhase, isMirrored))
	{
		AI_ASSERT( false );
		ClimbLog("%s: %s is missing ap-ref\n", pNavChar->GetName(), DevKitOnly_StringIdToString(animId));
	}

	// Find the offset from the align to the ap-ref so the align can be used to find the ap-ref on the edge
	Vector apRefOffsetWs = apRef.GetTranslation() - alignLocWs.GetTranslation();

	if (pSelf->m_status.m_isInWallShimmy)
	{
		BoundFrame apRefBf = sourceEdgeBf;

		const Vector limbOffsetLs = apRefBf.GetLocator().UntransformVector(apRefOffsetWs);

		// Wall shimmy anim is not offset from the ap-ref so an additional wall offset is needed
		pSelf->AdjustWallShimmyBf(&apRefBf, limbOffsetLs.Z());

		// Based on the align we know where the ap-ref is. Based on the edge we know where the wall is. The difference
		// between the ap-ref and the wall is added to the ap-ref offset to find the correct offset from the wall.
		apRefOffsetWs = sourceEdgeBf.GetTranslation() - apRefBf.GetTranslation();
	}

	Vector adjustLs;
	if (pSelf->GetScaleAdjustLs(pNavChar, &adjustLs))
	{
		const Vector adjustWs = pNavChar->GetLocator().TransformVector(adjustLs);
		apRefOffsetWs -= adjustWs;
	}

	return apRefOffsetWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AiClimbController::Shimmy::FindAlign(const NavLedgeReference& edge,
											 const BoundFrame& edgeBf,
											 Point* pOutEdgePosWs) const
{
	AI_ASSERT( pOutEdgePosWs );

	AiClimbController* pSelf = GetSelf();

	NavCharacter* pNavChar = pSelf->GetCharacter();

	const Vector edgeMoveWs = FindEdgeMoveWs(edge);
	const Vector velocityWs = edgeMoveWs * m_animSpeed * GetProcessDeltaTime();

	Point edgePos0 = edge.GetVert0();
	Point edgePos1 = edge.GetVert1();

	// Adjust translation to match edge
	const Point apRefPosWs   = pNavChar->GetTranslation() + m_apRefOffsetWs + velocityWs;
	DistPointSegment(apRefPosWs, edgePos0, edgePos1, pOutEdgePosWs);
	const Vector posOffsetWs = *pOutEdgePosWs - apRefPosWs;
	const Point posWs        = pNavChar->GetTranslation() + posOffsetWs;

	// Adjust rotation to match edge
	const Quat rotWs = edgeBf.GetRotation();

	const Locator edgeLocWs(posWs, rotWs);

	return edgeLocWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector AiClimbController::Shimmy::FindEdgeMoveWs(const NavLedgeReference& edge) const
{
	AiClimbController* pSelf = GetSelf();

	const NavCharacter* pNavChar = pSelf->GetCharacter();

	const Vector leftDirWs   = GetLocalX(pNavChar->GetRotation());
	const Vector edgeWs      = Normalize(edge.GetEdge());
	const bool isEdgeDirLeft = Dot(leftDirWs, edgeWs) > 0.0f;
	const F32 edgeDirMult    = isEdgeDirLeft ? -1.0f : 1.0f;
	const F32 wallShimmyMult = pSelf->m_status.m_isInWallShimmy ? -1.0f : 1.0f;
	const F32 mirrorDirMult  = pSelf->m_status.m_mirrorShimmy ? -1.0f : 1.0f;
	const Vector edgeMoveWs  = edgeWs * edgeDirMult * wallShimmyMult * mirrorDirMult;

	return edgeMoveWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::Shimmy::DoesOldPathContainNew(const PathInfo& oldPathInfo, const PathInfo& newPathInfo) const
{
	if (!newPathInfo.m_valid || !newPathInfo.m_valid)
	{
		return false;
	}

	const U32F oldWaypointCount = oldPathInfo.m_waypointsPs.GetWaypointCount();
	const U32F newWaypointCount = newPathInfo.m_waypointsPs.GetWaypointCount();

	const U32F newStartLedgeId = newPathInfo.m_waypointsPs.GetNavLedgeHandle(0).GetLedgeId();

	U32F oldIndex = FLT_MAX;

	// New path is expected to be shorter, so find the index of the old path where the new path starts (if any)
	for (U32F ii = 0; ii < oldWaypointCount; ++ii)
	{
		const U32F oldLedgeId = oldPathInfo.m_waypointsPs.GetNavLedgeHandle(ii).GetLedgeId();

		if (oldLedgeId == newStartLedgeId)
		{
			oldIndex = ii;
			break;
		}
	}

	if (oldIndex == FLT_MAX)
	{
		return false;
	}
	
	// Find if the new path includes the old path
	for (U32F newIndex = 0; newIndex < newWaypointCount && oldIndex < oldWaypointCount; ++newIndex, ++oldIndex)
	{
		const U32F oldLedgeId = oldPathInfo.m_waypointsPs.GetNavLedgeHandle(oldIndex).GetLedgeId();
		const U32F newLedgeId = newPathInfo.m_waypointsPs.GetNavLedgeHandle(newIndex).GetLedgeId();

		if (oldIndex != newIndex)
		{
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Shimmy::UpdateArmIk(ArmIndex armIndex, StringId64 channelId)
{
	AiClimbController* pSelf = GetSelf();

	if (pSelf->m_status.m_isInWallShimmy)
	{
		return;
	}

// #define ENABLE_SHIMMY_DEBUG
#if defined(ENABLE_SHIMMY_DEBUG)
const Color color = armIndex == kLeftArm ? kColorCyan : kColorRed;
const Vector offset = armIndex == kLeftArm ? Vector(0.0f, 0.01f, 0.0f) : kZero;
#endif

	AiClimbController::IkTargetInfo& ikInfo = pSelf->m_status.m_ikInfo;

	bool found = false;

	if (g_navCharOptions.m_climb.m_searchForShimmyLedge)
	{
		NavMeshReadLockJanitor nmReadLock(FILE_LINE_FUNC);

		const NdGameObject* pGo = pSelf->GetCharacterGameObject();
		AI_ASSERT( pGo );

		const SkeletonId skelId  = pGo->GetSkeletonId();
		const ArtItemAnim* pAnim = pGo->GetAnimControl()->LookupAnim(pSelf->m_status.m_animId).ToArtItem();
		AI_ASSERT( pAnim );

		const F32 phase = FindCurrentPhase();

		Locator wristLocLs;
		EvaluateChannelInAnim(skelId, pAnim, channelId, phase, &wristLocLs);
		const Locator wristLocWs = pGo->GetLocator().TransformLocator(wristLocLs);

		// Search for the closest ledge to the hand
		if (const NavLedgeGraph* pGraph = m_edge.GetLedgeGraph())
		{
			FindLedgeDfsParams flParams;
			flParams.m_point        = pGraph->WorldToLocal(wristLocWs.Pos());
			flParams.m_searchRadius = 1.0f;
			flParams.m_pGraph       = pGraph;
			flParams.m_startLedgeId = m_edge.GetId();

			FindClosestLedgeDfsLs(&flParams);

			if (flParams.m_pLedge)
			{
				const Vector handMoveWs = wristLocWs.GetTranslation() - m_lastHandPosWs[armIndex];
				const Vector edgeMoveWs = FindEdgeMoveWs(m_edge);

				// Only allow forward hand movement
				if (Dot(handMoveWs, edgeMoveWs) > 0.0f)
				{
					const Vector wallNormalWs   = pGraph->LocalToWorld(flParams.m_pLedge->GetWallNormalLs());
					const Vector wallBinormalWs = pGraph->LocalToWorld(flParams.m_pLedge->GetWallBinormalLs());

					ikInfo.m_handEdgeVert0Ws[armIndex]        = pGraph->LocalToWorld(flParams.m_pLedge->GetVertex0Ls());
					ikInfo.m_handEdgeVert1Ws[armIndex]        = pGraph->LocalToWorld(flParams.m_pLedge->GetVertex1Ls());
					ikInfo.m_handEdgeWallNormalWs[armIndex]   = wallNormalWs;
					ikInfo.m_handEdgeWallBinormalWs[armIndex] = wallBinormalWs;

					ikInfo.m_handIkStrength[armIndex] = 1.0f;

					m_lastHandPosWs[armIndex] = wristLocWs.GetTranslation();
				}

				found = true;

#if defined(ENABLE_SHIMMY_DEBUG)
g_prim.Draw(DebugArrow(m_lastHandPosWs[armIndex] + offset, handMoveWs, color, 0.1f));
g_prim.Draw(DebugArrow(wristLocWs.GetTranslation() + offset, edgeMoveWs * 0.1f, kColorWhite, 0.1f));

const Point posWs = pGraph->LocalToWorld(flParams.m_nearestPoint);
g_prim.Draw(DebugLine(wristLocWs.GetTranslation(), posWs + offset, color));
#endif

			}
		}
	}

	if (!found)
	{
		ikInfo.m_handEdgeVert0Ws[armIndex]        = m_edge.GetVert0();
		ikInfo.m_handEdgeVert1Ws[armIndex]        = m_edge.GetVert1();
		ikInfo.m_handEdgeWallNormalWs[armIndex]   = m_edge.GetWallNormal();
		ikInfo.m_handEdgeWallBinormalWs[armIndex] = m_edge.GetWallBinormal();

		ikInfo.m_handIkStrength[armIndex] = 1.0f;
	}

#if defined(ENABLE_SHIMMY_DEBUG)
g_prim.Draw(DebugLine(ikInfo.m_handEdgeVert0Ws[armIndex] + offset, ikInfo.m_handEdgeVert1Ws[armIndex] + offset, color));
#endif

}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Shimmy::UpdateIkInfo()
{
	UpdateArmIk(kLeftArm, SID("apReference-hand-l"));
	UpdateArmIk(kRightArm, SID("apReference-hand-r"));
}

FSM_STATE_REGISTER(AiClimbController, Shimmy, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// ClimbToWallShimmy
/// --------------------------------------------------------------------------------------------------------------- ///

class AiClimbController::ClimbToWallShimmy : public AiClimbController::BaseState
{
	virtual void OnEnter() override;
	virtual void OnUpdate() override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::ClimbToWallShimmy::OnEnter()
{
	AiClimbController* pSelf = GetSelf();

	const BoundFrame startBf = pSelf->FindHandSourceBf();

	pSelf->m_status.CopySourceToTarget();

	AnimParams params;
	params.m_animationId     = kClimbToWallShimmyAnimId;
	params.m_boundFrame      = startBf;
	params.m_stateId         = SID("s_climb-state");
	params.m_finishCondition = AnimAction::kFinishOnAnimEndEarly;
	params.m_isMirrored      = false;
	params.m_isBfValid       = true;

	pSelf->PlayAnimation(params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::ClimbToWallShimmy::OnUpdate()
{
	AiClimbController* pSelf = GetSelf();

	if (pSelf->m_animAction.IsDone())
	{
		pSelf->m_status.m_isInWallShimmy = true;
		pSelf->GoSelectTransition();
	}
}

FSM_STATE_REGISTER(AiClimbController, ClimbToWallShimmy, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// WallShimmyToClimb
/// --------------------------------------------------------------------------------------------------------------- ///

class AiClimbController::WallShimmyToClimb : public AiClimbController::BaseState
{
	virtual void OnEnter() override;
	virtual void OnUpdate() override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::WallShimmyToClimb::OnEnter()
{
	AiClimbController* pSelf = GetSelf();

	const BoundFrame startBf = pSelf->FindFootSourceBf();

	pSelf->m_status.CopySourceToTarget();

	AnimParams params;
	params.m_animationId     = kWallShimmyToClimbAnimId;
	params.m_boundFrame      = startBf;
	params.m_stateId         = SID("s_climb-state");
	params.m_finishCondition = AnimAction::kFinishOnAnimEndEarly;
	params.m_isMirrored      = false;
	params.m_isBfValid       = true;

	pSelf->PlayAnimation(params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::WallShimmyToClimb::OnUpdate()
{
	AiClimbController* pSelf = GetSelf();

	if (pSelf->m_animAction.IsDone())
	{
		pSelf->m_status.m_isInWallShimmy = false;
		pSelf->GoSelectTransition();
	}
}

FSM_STATE_REGISTER(AiClimbController, WallShimmyToClimb, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// Idle
/// --------------------------------------------------------------------------------------------------------------- ///

class AiClimbController::Idle : public AiClimbController::BaseState
{
	enum UpdateTargetBf
	{
		kUpdateTargetBf,
		kDoNotUpdateTargetBf,
	};

	void PlayIdleAnim(UpdateTargetBf update);
	bool ShouldAllowPlayerClimb() const;

	virtual void OnEnter() override;
	virtual void OnUpdate() override;
	virtual bool CanProcessCommand() const override;
	virtual void OnExit() override;

	void UpdateArmIk(ArmIndex armIndex) const;
	void UpdateIkInfo() const;

	bool m_allowIdleExit;
	bool m_climbCollisionSpawned;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Idle::PlayIdleAnim(AiClimbController::Idle::UpdateTargetBf update)
{
	AiClimbController* pSelf = GetSelf();

	AI_ASSERTF( pSelf->AllowIdleEnter(), ("Attempt to idle when not allowed") );

	BoundFrame targetEdgeBf = pSelf->FindLimbTargetBf();
	NavLedgeReference targetEdge = pSelf->m_status.m_isInWallShimmy           ?
								   pSelf->m_status.m_sourceEdgeFoot[kLeftLeg] :
								   pSelf->m_status.m_sourceEdgeHand[kLeftArm];

	const bool success = pSelf->FindBestTransition(&pSelf->m_status);
	AI_ASSERTF( success, ("Unable to find an idle anim for the current climb pose") );

	const StringId64 animId = pSelf->m_status.m_animId;
	bool isMirrored = pSelf->m_status.m_mirrored;

	const NavCharacter* pNavChar = pSelf->GetCharacter();
	AnimControl* pAnimControl    = pNavChar->GetAnimControl();

	DC::BlendParams animBlendParams;
	animBlendParams.m_animFadeTime   = 0.5f;
	animBlendParams.m_motionFadeTime = 0.3f;
	animBlendParams.m_curve          = DC::kAnimCurveTypeLinear;

	// Idle may be requested at an arbitrary point along the edge (i.e. shimmy) so target edge bf must be updated
	if (const AnimStateInstance* pDestInstance = pSelf->m_animAction.GetTransitionDestInstance(pAnimControl))
	{
		const bool isShimmy = pDestInstance->GetStateName() == SID("s_climb-tilt-no-ref-state");

		if (update == kUpdateTargetBf && !pSelf->m_status.m_isTeleporting && isShimmy)
		{
			Point posWs = pSelf->FindLimbAnimBf().GetTranslation();

			const Vector toTargetWs = targetEdgeBf.GetTranslation() - posWs;
			const F32 toTargetDist  = Length(toTargetWs);
			const F32 navCharSpeed  = Length(pNavChar->GetVelocityWs());
			const F32 speedAdjDist  = navCharSpeed * animBlendParams.m_motionFadeTime;

			// Nudge posWs forward by the character velocity to avoid appearing to move backwards
			if (speedAdjDist < toTargetDist)
			{
				const Vector toTargetDirWs = toTargetWs / toTargetDist;
				const Point idlePosWs      = posWs + toTargetDirWs * speedAdjDist;

				posWs = idlePosWs;
			}

			// Try to snap the point the edge
			if (pSelf->AttachToClosestEdge(posWs))
			{
				targetEdgeBf = pSelf->FindLimbTargetBf();
			}

			// Blend over the remaining anim time if it is less
			const F32 phase        = pDestInstance->Phase();
			const F32 animTimeLeft = (1.0f - phase) * pDestInstance->GetDuration();

			animBlendParams.m_animFadeTime = Min(animBlendParams.m_animFadeTime, animTimeLeft);
		}
	}

	if (pSelf->m_status.m_isInWallShimmy)
	{
		// Wall shimmy idle anim is offset from the ap-ref so no additional wall offset is needed
		pSelf->AdjustWallShimmyBf(&targetEdgeBf, 0.0f);

		isMirrored = pSelf->m_status.m_mirrorShimmy;
	}
	else
	{
		pSelf->AdjustSlopeBf(&targetEdgeBf, targetEdge);
	}

	AnimParams params;
	params.m_animationId     = animId;
	params.m_boundFrame      = targetEdgeBf;
	params.m_stateId         = SID("s_climb-state");
	params.m_finishCondition = AnimAction::kFinishOnTransitionTaken;
	params.m_blend           = animBlendParams;
	params.m_isMirrored      = isMirrored;
	params.m_isBfValid       = true;

	pSelf->PlayAnimation(params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::Idle::ShouldAllowPlayerClimb() const
{
	AiClimbController* pSelf = GetSelf();

	const bool enable = g_playerOptions.m_enableBuddyClimb &&
						!g_playerOptions.m_disableBuddyClimb &&
						!pSelf->m_status.m_isInWallShimmy &&
						(pSelf->GetCharacter()->GetUserId() == SID("samuel") || pSelf->GetCharacter()->GetUserId() == SID("elena"));

	return enable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Idle::OnEnter()
{
	AiClimbController* pSelf = GetSelf();

	pSelf->m_status.m_isIdling          = true;
	pSelf->m_status.m_allowPlayerClimb  = ShouldAllowPlayerClimb();
	pSelf->m_status.m_wantInsideCorner  = false;
	pSelf->m_status.m_wantOutsideCorner = false;
// 	pSelf->m_status.m_wantForward       = false;

	m_allowIdleExit         = true;
	m_climbCollisionSpawned = false;

	// Add collision object for a climbing edge for the player
	//if (pSelf->m_status.m_allowPlayerClimb)
	//{
	//	const NavCharacter* pChar      = GetCharacter();
	//	const I32 climbBuddyIndex      = NpcClimbObject::GetClimbBuddyIndex(pChar->GetUserId());
	//	const StringId64 shoulderColId = NpcClimbObject::GetClimbBuddyShoulderCol(climbBuddyIndex);
	//
	//	if (const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByNameId(shoulderColId))
	//	{
	//		m_climbCollisionSpawned = pSpawner->Spawn(GetCharacter()) != nullptr;
	//	}
	//}

	PlayIdleAnim(kUpdateTargetBf);

	UpdateIkInfo();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Idle::OnUpdate()
{
	AiClimbController* pSelf = GetSelf();

	const NavCharacter* pNavChar = pSelf->GetCharacter();
	AnimControl* pAnimControl    = pNavChar->GetAnimControl();

	bool playIdleAnim = pSelf->AllowIdleExit() != m_allowIdleExit;

	if (playIdleAnim && m_allowIdleExit)
	{
		pSelf->m_status.m_allowIdleExitTime = pNavChar->GetCurTime() + kPlayerClimbIdleDelay;
	}

	m_allowIdleExit = pSelf->AllowIdleExit();

	if (m_allowIdleExit)
	{
		if (pSelf->UpdatePathWaypointsInternal(pSelf->m_pendingPathInfo) || pSelf->IsClimbing())
		{
			NavLedgeReference nextEdge;
			BoundFrame nextEdgeBf;

			const EdgeStatus edgeStatus = pSelf->GetNextEdge(&nextEdge, &nextEdgeBf);

			pSelf->m_pendingPathInfo.Reset();
			pSelf->m_status.m_isIdling  = edgeStatus == kEdgeStatusBlocked || edgeStatus == kEdgeStatusFail;
			pSelf->m_status.m_isBlocked = edgeStatus == kEdgeStatusBlocked;

			if (pSelf->IsClimbing() && !pSelf->m_status.m_isBlocked)
			{
				pSelf->GoSelectTransition();
				return;
			}
		}
	}

	if (const AnimStateInstance* pDestInstance = pSelf->m_animAction.GetTransitionDestInstance(pAnimControl))
	{
		if (const ArtItemAnim* pAnim = pDestInstance->GetPhaseAnimArtItem().ToArtItem())
		{
			const F32 blendTime = kLoopBlendOutTimeDefault;
			const F32 endPhase  = 1.0f - Limit01( GetPhaseFromClipTime(pAnim->m_pClipData, blendTime) );
			const F32 curPhase  = pDestInstance->Phase();

			if (curPhase >= endPhase)
			{
				playIdleAnim = true;
			}
		}
	}

	if (playIdleAnim)
	{
		PlayIdleAnim(kDoNotUpdateTargetBf);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiClimbController::Idle::CanProcessCommand() const
{
	return m_allowIdleExit;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Idle::OnExit()
{
	AiClimbController* pSelf = GetSelf();

	// Remove collision object for a climbing edge for the player
	//if (m_climbCollisionSpawned)
	//{
	//	const NavCharacter* pChar      = GetCharacter();
	//	const I32 climbBuddyIndex      = NpcClimbObject::GetClimbBuddyIndex(pChar->GetUserId());
	//	const StringId64 shoulderColId = NpcClimbObject::GetClimbBuddyShoulderCol(climbBuddyIndex);
	//
	//	if (const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByNameId(shoulderColId))
	//	{
	//		pSpawner->Destroy();
	//	}
	//
	//	m_climbCollisionSpawned = false;
	//}

	pSelf->m_status.m_allowPlayerClimb = false;
	pSelf->m_status.m_isBlocked        = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Idle::UpdateArmIk(ArmIndex armIndex) const
{
	AiClimbController* pSelf = GetSelf();

	AiClimbController::IkTargetInfo& ikInfo = pSelf->m_status.m_ikInfo;

	const NavLedgeReference sourceEdge = pSelf->m_status.m_sourceEdgeHand[armIndex];

	ikInfo.m_handEdgeVert0Ws[armIndex]        = sourceEdge.GetVert0();
	ikInfo.m_handEdgeVert1Ws[armIndex]        = sourceEdge.GetVert1();
	ikInfo.m_handEdgeWallNormalWs[armIndex]   = sourceEdge.GetWallNormal();
	ikInfo.m_handEdgeWallBinormalWs[armIndex] = sourceEdge.GetWallBinormal();
	ikInfo.m_handIkStrength[armIndex]         = 1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiClimbController::Idle::UpdateIkInfo() const
{
	UpdateArmIk(kLeftArm);
	UpdateArmIk(kRightArm);
}

FSM_STATE_REGISTER(AiClimbController, Idle, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AiClimbController::GetMaxStateAllocSize()
{
	const size_t stateSizes[] =
	{
		sizeof(Interrupted),
		sizeof(Transition),
		sizeof(WaitForTransition),
		sizeof(ClimbToReach),
		sizeof(Shimmy),
		sizeof(ClimbToWallShimmy),
		sizeof(WallShimmyToClimb),
		sizeof(Idle)
	};

	size_t maxStateSize = 0;

	for (U32F ii = 0; ii < ARRAY_COUNT(stateSizes); ++ii)
	{
		maxStateSize = Max(maxStateSize, stateSizes[ii]);
	}

	return maxStateSize;
}

#endif // ENABLE_NAV_LEDGES
