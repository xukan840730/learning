/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/ndphys/pat.h"
#include "ndlib/util/maybe.h"

#include "gamelib/feature/feature-db-ref.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/faction-mgr.h"
#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-handle.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/path-waypoints.h"
#include "gamelib/scriptx/h/ai-tap-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPackMutex;
class EntitySpawner;
class Level;
class NavPoly;

#if ENABLE_NAV_LEDGES
class NavLedge;
#endif

namespace DC
{
	struct TapAnimAnchor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class TraversalActionPack : public ActionPack
{
public:
	DECLARE_ACTION_PACK_TYPE(TraversalActionPack);
	typedef ActionPack ParentClass;

	static CONST_EXPR float kPlayerBlockRadius	  = 1.1f;
	static CONST_EXPR size_t kMaxPathNodesPerSide = 16;

	enum class AnimAdjustType
	{
		kNone,
		kLinear,
		kEdge,
	};

	static const char* GetAnimAdjustTypeStr(AnimAdjustType t)
	{
		switch (t)
		{
		case AnimAdjustType::kNone:	return "none";
		case AnimAdjustType::kLinear: return "linear";
		case AnimAdjustType::kEdge: return "edge";
		}

		return "???";
	}

	struct InitParams
	{
		const Vector GetTraversalDeltaLs() const { return (m_exitPtLs - kOrigin) - m_exitOffsetLs; }

		Locator m_spawnerSpaceLoc = Locator(kOrigin);
		Point m_entryPtLs		  = kOrigin;
		Point m_exitPtLs	  = kOrigin;
		Vector m_exitOffsetLs = kZero;

		StringId64 m_infoId			= INVALID_STRING_ID_64;
		StringId64 m_mutexNameId	= INVALID_STRING_ID_64;
		StringId64 m_ropeAttachId	= INVALID_STRING_ID_64;
		StringId64 m_navMeshLevelId = INVALID_STRING_ID_64;

		U32 m_factionIdMask = 0;
		U32 m_instance		= 0;
		U32 m_skillMask		= 0;

		U8 m_tensionModeMask = 0;

		Nav::StaticBlockageMask m_obeyedStaticBlockers = Nav::kStaticBlockageMaskAll;

		float m_vaultWallHeight = 0.0f;
		float m_ladderRungSpan	= 0.0f;
		float m_extraPathCost	= 0.0f;
		float m_rbBlockedExtraPathCost = 0.0f;
		float m_usageDelay		= 0.0f;
		float m_animAdjustWidth = 0.0f;
		float m_reverseAnimAdjustWidth = -1.0f;
		float m_animRotDeg = 0.0f;
		float m_disableNearPlayerRadius = -1.0f;

		MotionType m_motionType			= kMotionTypeMax;
		AnimAdjustType m_animAdjustType = AnimAdjustType::kNone;

		DC::VarTapDirection m_directionType = DC::kVarTapDirectionUp;
		DC::VarTapAnimType m_enterAnimType	= DC::kVarTapAnimTypeGround;
		DC::VarTapAnimType m_exitAnimType	= DC::kVarTapAnimTypeGround;

		bool m_isVarTap = false;
		bool m_allowNormalDeaths = false;
		bool m_playerBoost		 = false;
		bool m_singleUse		 = false;
		bool m_reverseHalfOfTwoWayAp = false;
		bool m_noPush = false;
		bool m_enablePlayerBlocking = false;
		bool m_allowAbort = false;
		bool m_fixedAnim  = false;
		bool m_caresAboutBlockingRb	 = false;
		bool m_unboundedReservations = false;
	};

	struct AnimAdjustRange
	{
		AnimAdjustRange(float absRange) : m_min(Abs(absRange)), m_max(Abs(absRange)) {}

		AnimAdjustRange(float min, float max) : m_min(Abs(min)), m_max(Abs(max)) {}

		float m_min;
		float m_max;
	};

	// A game-specific callback to initialize InitParams for Charter live update (return value used for static init)
	typedef bool (*BuildInitParamsCb)(TraversalActionPack::InitParams* pInitParams,
									  const EntitySpawner* pSpawner,
									  const BoundFrame& baseLoc,
									  StringId64 reverseMutexNameId,
									  I32F instance);

	static void SetBuildInitParamsFunc(BuildInitParamsCb callback) { s_buildInitParamsCb = callback; }

	static bool BuildInitParams(TraversalActionPack::InitParams* pInitParams,
								const EntitySpawner* pSpawner,
								const BoundFrame& baseLoc,
								StringId64 reverseMutexNameId,
								I32F instance)
	{
		bool ret = false;
		if (s_buildInitParamsCb)
		{
			ret = s_buildInitParamsCb(pInitParams, pSpawner, baseLoc, reverseMutexNameId, instance);
		}
		return ret;
	}

	bool CanRegisterSelf(const ActionPackRegistrationParams& params,
						 const BoundFrame& tapBf,
						 NavLocation* pNavLocOut = nullptr) const;

	TraversalActionPack();
	TraversalActionPack(const BoundFrame& loc, const EntitySpawner* pSpawner, const InitParams& params);
	virtual ~TraversalActionPack() override;

	virtual void Logout() override;
	virtual void Reset() override;
	virtual bool NeedsUpdate() const override { return m_costDirty || !m_animAdjustSetup || m_navBlocked; }
	virtual void Update() override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual void DebugDraw(DebugPrimTime tt = kPrimDuration1FrameAuto) const override;
	virtual void DebugDrawRegistrationFailure() const override;

	virtual StringId64 GetTypeIdForScript() const override;
	virtual void GetStatusDescription(IStringBuilder* pStrOut) const override;
	virtual ActionPack* GetReverseActionPack() const override { return m_hReverseAp.ToActionPack(); }
	virtual U32F GetReverseActionPackMgrId() const override { return m_hReverseAp.GetMgrId(); }
	virtual void SetReverseActionPack(ActionPack* pAp) override { m_hReverseAp = pAp; }
	virtual const Locator GetSpawnerSpaceLoc() const override { return m_spawnerSpaceLoc; }
	virtual bool Reserve(Process* pProcess, TimeFrame timeout = Seconds(0.0f)) override;
	virtual void Release(const Process* pProcess) override;

	virtual void AdjustGameTime(TimeDelta delta) override;
	virtual void LiveUpdate(EntitySpawner& spawner) override;
	virtual bool CanBeMoved() const override { return false; } // don't use base class Move, we have specialty arguments required
	bool MoveTap(const BoundFrame& enterBf,
				 DC::VarTapAnimType enterTypeId,
				 const BoundFrame& exitBf,
				 DC::VarTapAnimType exitTypeId);

	virtual void Enable(bool enable) override;
	virtual void Enable(bool enable, bool propagateToMutexOwners);

	NavLocation::Type GetSourceNavType() const;
	NavLocation::Type GetDestNavType() const;
	NavLocation GetSourceNavLocation() const;
	NavLocation GetDestNavLocation() const;

	DC::VarTapDirection GetDirectionType() const { return m_directionType; }
	DC::VarTapAnimType GetEnterAnimType() const { return m_enterAnimType; }
	DC::VarTapAnimType GetExitAnimType() const { return m_exitAnimType; }
	bool IsGroupCompatible(const DC::VarTapGroup* pGroup) const;
	bool IsGroupExactMatch(const DC::VarTapGroup* pGroup) const;

	void ResetReadyToUseTimer();
	bool ShouldDoBriefReserve() const;
	bool BriefReserve(Process* pNavChar);
	bool IsBriefReserved() const;
	bool HasReachedOccupancyLimit(const Process* pProcess = nullptr) const;

	void UpdateSourceBinding();
	void UpdateNavDestUnsafe();
	void ClearNavDestUnsafe();

	// traveler beware: appears to only be valid in some cases, such as push objects.
	float GetHeightDelta() const { return m_heightDelta; }

	void AdjustHeightDeltaLs(float delta) { m_heightDelta += delta; }
	float ComputeBasePathCost() const;
	float ComputeBasePathCostForReserver() const;
	float GetExtraPathCost() const { return m_extraPathCost; }
	void SetExtraPathCost(float newCost);
	void AddFactionId(FactionId faction);
	void RemoveFactionId(FactionId faction);
	U32F GetFactionIdMask() const { return m_factionIdMask; }
	U32F GetTraversalSkillMask() const { return m_skillMask; }
	U32F GetTensionModeMask() const { return m_tensionModeMask; }
	StringId64 GetInfoId() const { return m_infoId; }
	StringId64 GetLadderAnimOverlayId() const { return m_ladderTapAnimOverlayId; }
	StringId64 GetRopeAttachId() const { return m_ropeAttachId; }
	float GetVaultWallHeight() const { return m_vaultWallHeight; }
	float GetLadderRungSpan() const { return m_ladderRungSpan; }

	void SetVaultWallHeight(float wallHeight);
	void SetVarTapOffsetLs(Vector_arg deltaLs);

	inline bool IsFactionIdMaskValid(U32F factionMask) const
	{
		return (m_factionIdMask == 0) || ((m_factionIdMask & factionMask) != 0);
	}

	inline bool IsTensionModeValid(I32F tensionMode) const
	{
		if (m_tensionModeMask == 0)
		{
			return false;
		}

		if (tensionMode < 0)
		{
			return true;
		}

		return (m_tensionModeMask & (1U << tensionMode)) != 0;
	}

	inline bool IsTraversalSkillMaskValid(U32F traversalSkillMask) const
	{
		return (m_skillMask & traversalSkillMask) == m_skillMask;
	}

	Color GetDebugDrawColor() const;
	const char* GetSkillMaskString() const;

	StringId64 GetMutexId() const { return m_mutexId; }
	ActionPackMutex* GetMutex() const { return m_pMutex; }
	I32F GetUserCount() const;
	bool AddUser(Process* pChar);
	bool TryAddUser(Process* pChar);
	void RemoveUser(Process* pChar);

	void SetEnablePlayerBlocking(bool enable) { m_enablePlayerBlocking = enable; }
	bool IsEnabledPlayerBlocking() const { return m_enablePlayerBlocking; }

	virtual float DistToPointWs(Point_arg posWs, float entryOffset) const override;

	void SetOneShotUsageDelay(F32 usageDelay);

	static bool IsRope(U32 skillMask) { return (skillMask & (1U << DC::kAiTraversalSkillRope)) != 0; }
	static bool IsRopeClimb(U32 skillMask) { return (skillMask & (1U << DC::kAiTraversalSkillRopeClimb)) != 0; }
	static bool IsSqueezeThrough(U32 skillMask)
	{
		return (skillMask & (1U << DC::kAiTraversalSkillSqueezeThrough)) != 0;
	}
	static bool IsJumpUp(U32 skillMask)
	{
		return (skillMask & ((1U << DC::kAiTraversalSkillJumpUp) | (1U << DC::kAiTraversalSkillJumpUpLow))) != 0;
	}
	static bool IsJumpDown(U32 skillMask)
	{
		return (skillMask & ((1U << DC::kAiTraversalSkillJumpDown) | (1U << DC::kAiTraversalSkillJumpDownLow))) != 0;
	}
	static bool IsJumpAcross(U32 skillMask) { return (skillMask & (1U << DC::kAiTraversalSkillJumpAcross)) != 0; }
	static bool IsJump(U32 skillMask)
	{
		return IsJumpUp(skillMask) || IsJumpDown(skillMask) || IsJumpAcross(skillMask);
	}
	static bool IsLadderUp(U32 skillMask) { return (skillMask & (1U << DC::kAiTraversalSkillLadderUp)) != 0; }
	static bool IsLadderDown(U32 skillMask) { return (skillMask & (1U << DC::kAiTraversalSkillLadderDown)) != 0; }
	static bool IsLadder(U32 skillMask) { return IsLadderUp(skillMask) || IsLadderDown(skillMask); }
	static bool IsBalanceBeam(U32 skillMask) { return (skillMask & (1U << DC::kAiTraversalSkillBalanceBeam)) != 0; }
	static bool IsDoorOpen(U32 skillMask) { return (skillMask & (1U << DC::kAiTraversalSkillDoorOpen)) != 0; }
	static bool IsVaultUp(U32 skillMask)
	{
		return (skillMask & ((1U << DC::kAiTraversalSkillVaultUp) | (1U << DC::kAiTraversalSkillVaultUpLow))) != 0;
	}
	static bool IsVaultUpLow(U32 skillMask) { return (skillMask & (1U << DC::kAiTraversalSkillVaultUpLow)) != 0; }
	static bool IsVaultDown(U32 skillMask)
	{
		return (skillMask & ((1U << DC::kAiTraversalSkillVaultDown) | (1U << DC::kAiTraversalSkillVaultDownLow))) != 0;
	}

	static bool IsVaultAcross(U32 skillMask)
	{
		return (skillMask & ((1U << DC::kAiTraversalSkillVaultAcross) | (1U << DC::kAiTraversalSkillVaultAcrossLow)))
			   != 0;
	}
	static bool IsVault(U32 skillMask)
	{
		return IsVaultUp(skillMask) || IsVaultDown(skillMask) || IsVaultAcross(skillMask);
	}
	static bool IsProne(U32 skillMask)
	{
		return (skillMask & (1U << DC::kAiTraversalSkillProne)) != 0;
	}

	bool IsRope() const { return IsRope(m_skillMask); }
	bool IsRopeClimb() const { return IsRopeClimb(m_skillMask); }
	bool IsSqueezeThrough() const { return IsSqueezeThrough(m_skillMask); }
	bool IsJumpUp() const { return IsJumpUp(m_skillMask); }
	bool IsJumpDown() const { return IsJumpDown(m_skillMask); }
	bool IsJumpAcross() const { return IsJumpAcross(m_skillMask); }
	bool IsJump() const { return IsJumpUp() || IsJumpDown() || IsJumpAcross(); }
	bool IsLadderUp() const { return IsLadderUp(m_skillMask); }
	bool IsLadderDown() const { return IsLadderDown(m_skillMask); }
	bool IsLadder() const { return IsLadderUp() || IsLadderDown(); }
	bool IsBalanceBeam() const { return IsBalanceBeam(m_skillMask); }
	bool IsDoorOpen() const { return IsDoorOpen(m_skillMask); }
	bool IsVaultUp() const { return IsVaultUp(m_skillMask); }
	bool IsVaultUpLow() const { return IsVaultUpLow(m_skillMask); }
	bool IsVaultDown() const { return IsVaultDown(m_skillMask); }
	bool IsVaultAcross() const { return IsVaultAcross(m_skillMask); }
	bool IsVault() const { return IsVaultUp() || IsVaultDown() || IsVaultAcross(); }
	bool IsProne() const { return IsProne(m_skillMask); }

	bool IsVarTap() const { return m_isVarTap; }
	bool IsReverseHalfOfTwoWayAp() const { return m_reverseHalfOfTwoWayAp; }
	bool IsPlayerBoost() const { return m_playerBoost; }
	bool IsSingleUse() const { return m_singleUse; }
	bool IsNoPush() const { return m_noPush; }
	bool IsAbortAllowed() const { return m_allowAbort; }
	bool IsToOrFromDive() const;
	bool IsFixedAnim() const { return m_fixedAnim; }
	bool IsAnimAdjustSetup() const { return m_animAdjustSetup; }

	void SetReverseHalfOfTwoWayAp(bool r) { m_reverseHalfOfTwoWayAp = r; }

	const char* GetBoostAnimOverlaySetName() const;

	bool AreNormalDeathsAllowed() const { return m_allowNormalDeaths; }
	const BoundFrame& GetOriginalBoundFrame() const { return m_origLoc; }

	bool ChangesParentSpaces() const { return m_changesParentSpaces; }
	const Binding& GetDestBinding() const { return m_destBoundFrame.GetBinding(); }

	const BoundFrame& GetBoundFrameInDestSpace() const { return m_destBoundFrame; }
	void SetBoundFrameInDestSpace(const BoundFrame& bf) { m_destBoundFrame = bf; }

	const Point GetExitPointLs() const { return m_exitPtLs; }
	const Point GetOriginalExitPointLs() const { return m_origExitPtLs; }
	void SetExitPointLs(Point exitPtLs) { m_exitPtLs = exitPtLs; }

	static const Point GetExitPointWs(const BoundFrame& destBoundFrame, Point_arg exitPtLs, float heightDelta);
	const Point GetExitPointWs() const;
	const Vector GetExitOffsetLs() const { return m_exitOffsetLs; }
	const Point GetOriginalExitPointWs() const;
	const Vector GetTraversalDeltaLs() const
	{
		return IsDoorOpen() ? kZero : ((m_exitPtLs - kOrigin) - m_exitOffsetLs);
	}
	const BoundFrame GetVarTapEndBoundFrame() const;

	// adjust the entry point up or down in order to achieve the specified height (leave exit point alone) -- for jump TAPs
	void AdjustEntryToHeight(const Locator& newTapLoc, Scalar_arg newHeight);
	bool FindEdge(bool debugDraw = false);

	// Edge Slide: allows the AP to function anywhere along a feature edge
	const FeatureEdgeReference* GetEdgeRef() const { return (m_edgeRef.IsGood()) ? &m_edgeRef : nullptr; }
	FeatureEdgeReference* GetEdgeRefForModify() { return (m_edgeRef.IsGood()) ? &m_edgeRef : nullptr; }
	void SetEdgeRef(const FeatureEdgeReference& edgeRef) { m_edgeRef = edgeRef; }
	bool GetEdgeVerts(Point& bottom0Ws, Point& bottom1Ws, Point& top0Ws, Point& top1Ws, float offset) const;

	TimeFrame UsageTimerRemaining() const;
	bool HasUsageTimerExpired() const { return UsageTimerRemaining() == Seconds(0.0f); }

	static void FixToCollision(TraversalActionPack* pTap);
	bool NeedFixedToCollision() const;
	bool IsFixedToCollision() const { return m_fixedToCollision; }
	void SetFixedToCollision(bool set = true) { m_fixedToCollision = set; }

	static CollideFilter GetCollideFilter()
	{
		return CollideFilter(Collide::kLayerMaskGeneral, Pat(1ULL << Pat::kPassThroughShift | 1ULL << Pat::kSqueezeThroughShift));
	}

	void SetLadderPat(Pat newPat)
	{
		if (IsLadder())
			m_ladderPat = newPat;
	}
	Pat GetLadderPat() const { return IsLadder() ? m_ladderPat : Pat(0); }

	virtual Point GetDefaultEntryPointWs(Scalar_arg offset) const override;
	virtual Point GetDefaultEntryPointPs(Scalar_arg offset) const override;
	virtual BoundFrame GetDefaultEntryBoundFrame(Scalar_arg offset) const override;

	AnimAdjustType GetApAnimAdjustType() const { return m_animAdjustType; }
	const AnimAdjustRange& GetApAnimAdjustRange() const { return m_animAdjustWidth; }
	const AnimAdjustRange& GetDesiredApAnimAdjustRange() const { return m_desAnimAdjustWidth; }

	Locator GetApAnimAdjustLs(Point_arg charPosWs) const;
	Locator GetApAnimAdjustLs(Point_arg pathPosAWs, Point_arg pathPosBWs) const;

	float GetAnimRotDeg() const { return m_animRotDeg; }
	float GetDisableNearPlayerRadius() const { return m_disableNearPlayerRadius; }

	I32F GetInstance() const { return m_instance; }
	void SetNextInstance(TraversalActionPack* pNextTap) { m_hNextInstance = pNextTap; }
	TraversalActionPack* GetNextInstance() { return m_hNextInstance.ToActionPack<TraversalActionPack>(); }

	virtual bool CheckRigidBodyIsBlocking(RigidBody* pBody, uintptr_t userData) override;
	virtual void RemoveBlockingRigidBody(const RigidBody* pBody) override;

	Maybe<BoundFrame> GetAnchorLocator(StringId64 apNameId, float spaceBlend) const;

	void SearchForBlockingRigidBodies();
	bool CaresAboutBlockingRigidBodies() const { return m_caresAboutBlockingRb; }
	bool IsBlockedByRigidBody() const { return m_caresAboutBlockingRb && m_hBlockingRigidBody.HandleValid(); }

	virtual void AddPlayerBlock() override;
	virtual void RemovePlayerBlock() override;

	virtual bool HasUnboundedReservations() const override
	{
		return m_unboundedReservations
			   && TRUE_IN_FINAL_BUILD(!g_navCharOptions.m_traversal.m_disableUnboundedReservations);
	}

	virtual Nav::StaticBlockageMask GetObeyedStaticBlockers() const override { return m_obeyedStaticBlockers; }

	bool IsKnownPathNodeId(U32F iNode) const;
	void DebugDrawPathNodes(DebugPrimTime tt = kPrimDuration1FrameAuto) const;

	bool GetAnimAdjustNavPortalWs(Point_arg prevPosWs,
								  Point_arg curPosWs,
								  NavPathNode::NodeType nodeType,
								  float depenRadius,
								  Point& v0WsOut,
								  Point& v1WsOut,
								  I64 instanceSeed = -1) const;

	void DebugFixToCollision();
	void DebugSetupAnimRange();

private:
	struct DebugStatus
	{
		bool m_isRegistered : 1;
		bool m_hasDestination : 1;
		bool m_isEnabled : 1;
		bool m_isSkillValid : 1;
		bool m_isTensionValid : 1;
		bool m_isFactionValid : 1;
		bool m_isPlayerBlocked : 1;
		bool m_isPlayerDisabled : 1;
		bool m_isAvailable : 1;
		bool m_isReserved : 1;
		bool m_isRbBlocked : 1;
		bool m_hasNavClearance : 1;
	};

	void GetDebugStatus(DebugStatus* pDebugStatus) const;

private:
	static BuildInitParamsCb s_buildInitParamsCb;

	virtual bool HasNavMeshClearance(const NavLocation& navLoc,
									 bool debugDraw = false,
									 DebugPrimTime tt = kPrimDuration1FramePauseable) const override;

	virtual bool IsReservedByInternal(const Process* pProcess) const override;
	virtual bool IsAvailableInternal(bool caresAboutPlayerBlockage = true) const override;
	virtual bool IsAvailableForInternal(const Process* pProcess) const override;
	bool IsAvailableCommon(const Process* pProcess, bool caresAboutPlayerBlockage) const;

	virtual bool CaresAboutPlayerBlockage(const Process* pProcess) const override;
	virtual bool ReserveInternal(Process* pProcess, TimeFrame timeout = Seconds(0.0f)) override;
	virtual void EnableInternal(bool enable);

	virtual bool RegisterInternal() override;
	virtual void UnregisterInternal() override;
	virtual bool RegisterSelfToNavPoly(const NavPoly* pPoly) override;
#if ENABLE_NAV_LEDGES
	virtual bool RegisterSelfToNavLedge(const NavLedge* pNavLedge) override;
#endif

	void AddPathNodes();
	void AddExtraPathNodes();
	Point ComputePathNodePosPs(const NavPathNode& node, bool forEnter) const;
	void RemovePathNodes();
	void BlockPathNodesInternal(bool f);
	void UpdatePathNodeCost();
	bool HavePathNodes() const;
	void SetupAnimAdjust();
	Point GetAnimAdjustTestPosWs() const;

	AnimAdjustRange DetermineAnimRangeLinear(const AnimAdjustRange& desRange,
											 bool debugDraw	  = false,
											 DebugPrimTime tt = kPrimDuration1FrameAuto) const;

	static AnimAdjustRange DetermineAnimRangeEdge(const AnimAdjustRange& desRange,
												  const FeatureEdgeReference& edge,
												  const BoundFrame& boundFrame,
												  NavLocation startNavLoc,
												  NavLocation destNavLoc,
												  Point_arg regPosWs,
												  Point_arg exitPosWs,
												  IPathWaypoints* pApPathPsOut = nullptr,
												  bool debugDraw   = false,
												  DebugPrimTime tt = kPrimDuration1FrameAuto);
	void DebugDrawAnimRange(DebugPrimTime tt = kPrimDuration1FrameAuto) const;
	void DebugDrawAnimRangeEdge(DebugPrimTime tt = kPrimDuration1FrameAuto) const;

	static AnimAdjustRange DoAnimProbes(const AnimAdjustRange& desRange,
										const Vector probeDirPs,
										const NavLocation& navLoc,
										Nav::StaticBlockageMask obeyedStaticBlockers,
										bool debugDraw = false,
										DebugPrimTime tt = kPrimDuration1FrameAuto);

	static FeatureEdgeReference FindEdge(Point_arg findEdgePosWs, Vector_arg tapFacingWs, bool debugDraw = false);

	bool GetCheckBlockingObb(Obb& obbOut) const;
	bool CheckRigidBodyIsBlockingInternal(const RigidBody* pBody);
	void SetBlockingRigidBody(const RigidBody* pBody);
	void RemoveBlockingRigidBody();

	void ResetDestBoundFrame() { m_destBoundFrame = m_origLoc; }

	void DebugDrawAnchor(StringId64 apNameId, DebugPrimTime tt = kPrimDuration1FrameAuto) const;

	NdAtomicLock	m_addUserLock;

	float m_usageDelay;			 // Delay before tap can be used again
	float m_oneShotUsageDelay;	 // Same as above but reset when tap is used
	StringId64 m_mutexId;		 //
	ActionPackMutex* m_pMutex;	 //
	BoundFrame m_destBoundFrame; // Bound frame with binding to the destination
	Locator m_spawnerSpaceLoc;	 // Needed to fix up reverse two-way TAPs on platforms
	Point m_exitPtLs;			 // Exit point of traversal in local space
	Vector m_exitOffsetLs;		 // Offset from end AP to exit point in local space

	float m_extraPathCost;				//
	float m_rbBlockedExtraPathCost;		// Extra cost added when blocked by an RB and m_caresAboutBlockingRb is false
	float m_vaultWallHeight;			// The height of the wall for a vault tap
	float m_ladderRungSpan;				// The distance between rungs of a ladder tap

	NavLocation::Type m_destNavType;	 //
	NavLocation m_hDestNavLoc;			 //
	DC::VarTapDirection m_directionType; // Used to find a compatible tap table group
	DC::VarTapAnimType m_enterAnimType;	 //
	DC::VarTapAnimType m_exitAnimType;	 //

	StringId64 m_infoId;				 //
	StringId64 m_ladderTapAnimOverlayId; //
	StringId64 m_ropeAttachId;			 // Id of the rope attach object when jumping
	StringId64 m_navMeshLevelId;		// only look for nav mesh to register to in this level

	U32 m_factionIdMask;				 //
	U32 m_instance;						 //
	U32 m_skillMask;					 // Allowed traversal skills
	U8 m_tensionModeMask;				 // Allowed tension modes
	Nav::StaticBlockageMask m_obeyedStaticBlockers;

	union // 4 bytes
	{
		struct
		{
			bool m_isVarTap : 1;
			bool m_lastFullyReserved : 1;
			bool m_lastPlayerBlocked : 1;
			bool m_costDirty : 1;
			bool m_enablePlayerBlocking : 1;
			bool m_allowNormalDeaths : 1;
			bool m_briefReserved : 1;
			bool m_playerBoost : 1;
			bool m_singleUse : 1;
			bool m_allowFixToCollision : 1;
			bool m_fixedToCollision : 1;
			bool m_noPlayerBlockForPushyNpcs : 1;
			bool m_reverseHalfOfTwoWayAp : 1;
			bool m_enableWhenExposed : 1;
			bool m_noPush : 1;
			bool m_allowAbort : 1;
			bool m_changesParentSpaces : 1;
			bool m_animAdjustSetup : 1;
			bool m_addedExtraPathNodes : 1;
			bool m_propagateToMutexOwners : 1; // Enables/Disables other owners on shared mutex on change
			bool m_fixedAnim : 1;
			bool m_wantsEnable : 1;
			bool m_caresAboutBlockingRb : 1;
			bool m_unboundedReservations : 1;
		};

		U32 m_bitPackedFields;
	};

	float m_heightDelta;
	NavPathNode::NodeId m_exitPathNodeIds[kMaxPathNodesPerSide];
	NavPathNode::NodeId m_enterPathNodeIds[kMaxPathNodesPerSide];
	TimeFrame m_usageStartTime;

	ActionPackHandle m_hReverseAp;
	ProcessHandle m_hLastReservationHolder; // Used to test for blockage
	RigidBodyHandle m_hBlockingRigidBody;

	BoundFrame m_origLoc;
	Point m_origExitPtLs;
	FeatureEdgeReference m_edgeRef;
	FeatureEdgeReference m_animEdgeRef;

	AnimAdjustType m_animAdjustType;
	AnimAdjustRange m_desAnimAdjustWidth; // How much lateral adjustment can be applied to the AP for animation
	AnimAdjustRange m_animAdjustWidth;	  // How much lateral adjustment can be applied to the AP for animation
	float m_animRotDeg;
	TPathWaypoints<16> m_edgeApPathPs;
	Quat m_edgeToApFwLs;

	Pat m_ladderPat;
	ActionPackHandle m_hNextInstance;

	NdAtomic64 m_unboundedResCount;

	float m_disableNearPlayerRadius;

	friend class NavControl;
	friend class TapBlockingBodyCollector;
	friend class PlatformControl;
};
STATIC_ASSERT(sizeof(TraversalActionPack) == 1328);

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsTraversalActionPackTypeId(const StringId64 apTypeId);
