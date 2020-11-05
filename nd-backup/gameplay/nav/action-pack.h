/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/process/bound-frame.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-handle.h"
#include "gamelib/gameplay/nav/nav-location.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class EntitySpawner;
class Level;
class NavPoly;
class ActionPack;
#if ENABLE_NAV_LEDGES
class NavLedge;
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
struct ActionPackRegistrationParams
{
public:
	ActionPackRegistrationParams();

	const Level*		m_pAllocLevel;			// That's the level owning the memory for the action pack.
	const Level*		m_pRegistrationLevel;	// That's the level owning the navmesh of the action pack.
	NdGameObjectHandle	m_hPlatformOwner;
	StringId64			m_bindId;
	float				m_probeDist;
	float				m_yThreshold;
	Point				m_regPtLs;
	bool				m_readOnly;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ActionPackFeatureSource
{
	StringId64 m_featureLevelId = INVALID_STRING_ID_64;
	I32F m_iSection = -1;
	I32F m_iCorner = -1;
	I32F m_iEdge = -1;
};

/// --------------------------------------------------------------------------------------------------------------- ///
/// Interface for an object that can block multiple Aps
class IApBlocker
{
public:
	virtual bool IsDoor() const = 0;
	virtual bool AddBlockedAp(ActionPack* pAp) = 0;
	virtual void RemoveBlockedAp(ActionPack* pAp) = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
#define DECLARE_ACTION_PACK_TYPE(cType)                                                                                \
	static CONST_EXPR Type GetStaticType() { return k##cType; }                                                        \
	static const cType* FromActionPack(const ActionPack* pAp)                                                          \
	{                                                                                                                  \
		return (pAp && (pAp->GetType() == k##cType)) ? (const cType*)pAp : nullptr;                                    \
	}                                                                                                                  \
	static cType* FromActionPack(ActionPack* pAp)                                                                      \
	{                                                                                                                  \
		return (pAp && (pAp->GetType() == k##cType)) ? (cType*)pAp : nullptr;                                          \
	}

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPack
{
public:
	static const U16 kInvalidPostId = 0xffff;

	enum UsageState
	{
		kActionPackUsageNone,
		kActionPackUsageReserved,
		kActionPackUsageEntered
	};

	enum Type
	{
		kInvalidActionPack   = -1,
		kCoverActionPack	 = 0,
		kTraversalActionPack,
		kCinematicActionPack,
		kTurretActionPack,
		kPerchActionPack,
		kVehicleActionPack,
		kLeapActionPack,
		kHorseActionPack,
		kPulloutActionPack,
		kEntryActionPack,
		kPositionalActionPack,
		kSearchActionPack,
		kActionPackCount,
	};

	static const char* GetTypeName(Type tt)
	{
		switch (tt)
		{
		case kCoverActionPack:		return "kCoverActionPack";
		case kTraversalActionPack:	return "kTraversalActionPack";
		case kCinematicActionPack:	return "kCinematicActionPack";
		case kTurretActionPack:		return "kTurretActionPack";
		case kPerchActionPack:		return "kPerchActionPack";
		case kVehicleActionPack:    return "kVehicleActionPack";
		case kLeapActionPack:		return "kLeapActionPack";
		case kHorseActionPack:      return "kHorseActionPack";
		case kPulloutActionPack:	return "kPulloutActionPack";
		case kEntryActionPack:		return "kEntryActionPack";
		case kPositionalActionPack:	return "kPositionalActionPack";
		case kSearchActionPack:		return "kSearchActionPack";
		}
		return "???";
	}

	static const char* GetShortTypeName(Type tt)
	{
		switch (tt)
		{
		case kCoverActionPack:		return "Cover";
		case kTraversalActionPack:	return "Traversal";
		case kCinematicActionPack:	return "Cinematic";
		case kTurretActionPack:		return "Turret";
		case kPerchActionPack:		return "Perch";
		case kVehicleActionPack:    return "Vehicle";
		case kLeapActionPack:		return "Leap";
		case kHorseActionPack:      return "Horse";
		case kPulloutActionPack:	return "Pullout";
		case kEntryActionPack:		return "Entry";
		case kPositionalActionPack:	return "Positional";
		case kSearchActionPack:		return "Search";
		}
		return "???";
	}

	static StringId64 GetTypeIdForScript(Type tt)
	{
		switch (tt)
		{
		case kCoverActionPack:		return SID("cover");
		case kTraversalActionPack:	return SID("tap");
		case kCinematicActionPack:	return SID("cap");
		case kTurretActionPack:		return SID("turret");
		case kPerchActionPack:		return SID("perch");
		case kVehicleActionPack:	return SID("vehicle");
		case kLeapActionPack:		return SID("leap");
		case kHorseActionPack:		return SID("horse");
		case kPulloutActionPack:	return SID("pullout");
		case kEntryActionPack:		return SID("entry");
		case kPositionalActionPack:	return SID("positional");
		case kSearchActionPack:		return SID("search");
		}
		return SID("unknown");
	}

	static StringId64 GetDcTypeNameId(Type tt)
	{
		switch (tt)
		{
		case ActionPack::kCoverActionPack:		return SID("cover-action-pack");
		case ActionPack::kTraversalActionPack:	return SID("traversal-action-pack");
		case ActionPack::kCinematicActionPack:	return SID("cinematic-action-pack");
		case ActionPack::kTurretActionPack:		return SID("turret-action-pack");
		case ActionPack::kPerchActionPack:		return SID("perch-action-pack");
		case ActionPack::kVehicleActionPack:	return SID("vehicle-action-pack");
		case ActionPack::kLeapActionPack:		return SID("leap-action-pack");
		case ActionPack::kHorseActionPack:		return SID("horse-action-pack");
		case ActionPack::kPulloutActionPack:	return SID("pullout-action-pack");
		case ActionPack::kEntryActionPack:		return SID("entry-action-pack");
		case ActionPack::kPositionalActionPack:	return SID("positional-action-pack");
		case ActionPack::kSearchActionPack:		return SID("search-action-pack");
		}

		return INVALID_STRING_ID_64;
	}

	static const float kActionPackRegistrationProbeLength;

	/// Constructors.
	ActionPack(Type tt);
	ActionPack(Type tt, Point_arg regPtLs, const BoundFrame& bfLoc, const EntitySpawner* pSpawner);

	ActionPack(Type tt, Point_arg regPtLs, const BoundFrame& bfLoc, const Level* pAllocLevel);
	ActionPack(Type tt, Point_arg regPtLs, const BoundFrame& bfLoc, const Level* pAllocLevel, const Level* pRegLevel);

	virtual ~ActionPack();

	virtual bool NeedsUpdate() const { return m_reservationExpirationTime > Seconds(0.0f); }
	virtual void Update();
	virtual void Logout();
	virtual void Reset();
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	void SetOwnerProcess(Process* pProc);
	ProcessHandle GetOwnerProcessHandle() const { return m_hOwnerProcess; }

	ActionPack::Type GetType() const { return m_type; }

	virtual void DebugDraw(DebugPrimTime tt = kPrimDuration1FrameAuto) const;
	virtual void DebugDrawRegistrationFailure() const;

	/// Set and return the name id associated with the action pack.
	const char* GetTypeName() const { return GetTypeName(m_type); }
	virtual StringId64 GetTypeIdForScript() const { return GetTypeIdForScript(m_type); }
	virtual StringId64 GetDcTypeNameId() const { return GetDcTypeNameId(m_type); }

	virtual const char* GetName() const;
	virtual void GetStatusDescription(IStringBuilder* pStrOut) const;

	StringId64 GetSpawnerId() const { return m_spawnerId; }
	StringId64 GetParentSpawnerId() const;

	virtual bool IsNpcSpecific() const { return false; }
	virtual StringId64 GetOnlyAvailableToNpcId() const { return INVALID_STRING_ID_64; }


	const EntitySpawner* GetSpawner() const;
	const EntitySpawner* GetParentSpawner() const;

	const Process* GetProcess() const;
	Process* GetProcess();

	virtual ActionPack* GetReverseActionPack() const { return nullptr; }
	virtual U32F GetReverseActionPackMgrId() const;
	virtual void SetReverseActionPack(ActionPack* pAp) {}

	// This is the level owning the memory of the action pack (ie the level that logged in the action pack).
	const Level* GetAllocLevel() const { return m_regParams.m_pAllocLevel; }

	// This is the level containing the navmesh where this action pack is.
	const Level* GetRegistrationLevel() const;

	// registering with navigation system
	void RequestRegistration(const ActionPackRegistrationParams& params);
	void RequestUnregistration();
	void RegisterImmediately(const ActionPackRegistrationParams& params);
	void UnregisterImmediately();
	bool IsRegistered() const;
	bool IsLoggedIn() const;
	bool IsAssigned() const;

	virtual bool HasNavMeshClearance(const NavLocation& navLoc,
									 bool debugDraw	  = false,
									 DebugPrimTime tt = kPrimDuration1FramePauseable) const;
	virtual void RefreshNavMeshClearance();

	virtual bool ShouldGeneratePost() const { return false; }

	ActionPack* GetRegistrationListNext() const { return m_pNavRegistrationListNext; }
	U32F GetMgrId() const { return m_mgrId; }
	void SetMgrId(U32F id) { m_mgrId = id; }

	virtual const Locator GetSpawnerSpaceLoc() const { return Locator(kIdentity); }

	const BoundFrame& GetBoundFrame() const { return m_loc; }
	void SetBoundFrame(const BoundFrame& newFrame) { m_loc = newFrame; }

	const Locator GetLocatorWs() const { return m_loc.GetLocatorWs(); }
	const Locator GetLocatorPs() const { return m_loc.GetLocatorPs(); }
	const Point GetRegistrationPointLs() const { return m_regPtLs; }
	const Point GetRegistrationPointWs() const;

	virtual float DistToPointWs(Point_arg posWs, float entryOffset) const
	{
		const Point entryPosWs = GetDefaultEntryPointWs(entryOffset);
		return Dist(posWs, entryPosWs);
	}

	virtual Point GetDefaultEntryPointWs(Scalar_arg offset) const;
	virtual Point GetDefaultEntryPointPs(Scalar_arg offset) const;
	virtual BoundFrame GetDefaultEntryBoundFrame(Scalar_arg offset) const;

	// these positions are mainly used for cover and turret action packs
	virtual const Point GetVisibilityPositionPs() const;
	virtual const Point GetFireSidePositionWs() const;
	virtual const Point GetFireOverPositionWs() const;
	virtual const Point GetFireSidePositionPs() const;
	virtual const Point GetFireOverPositionPs() const;
	virtual const Point GetCoverPositionPs() const;
	const Point GetVisibilityPositionWs() const
	{
		return m_loc.GetParentSpace().TransformPoint(GetVisibilityPositionPs());
	}

	/// Return the maximum distance away an NPC can be in order to use this action pack.
	/// If max use distance is zero, the action pack can be used from ANY distance.
	virtual float GetMaxUseDistance() const { return 0.0f; }

	virtual bool IsAvailable(bool caresAboutPlayerBlockage = true) const final;
	virtual bool IsAvailableFor(const Process* pProcess) const;
	// is this action pack already reserved?
	virtual bool IsReserved() const;
	virtual bool IsReservedBy(const Process* pProcess) const final;
	virtual bool HasUnboundedReservations() const { return false; }

	/// Reserve the action pack
	virtual bool Reserve(Process* pProcess, TimeFrame timeout = Seconds(0.0f));

	/// Release the action pack previously reserved with Reserve
	virtual void Release(const Process* pProcess);

	/// Enable or disable the action pack
	virtual void Enable(bool enable);
	bool IsEnabled() const { return !m_disabled && !m_playerDisabled; }

	void SetDynamic(bool f) { m_isDynamic = f; }
	bool IsDynamic() const { return m_isDynamic; }

	virtual void AddPlayerBlock() { m_playerBlocked = true; }
	virtual void RemovePlayerBlock() { m_playerBlocked = false; }

	virtual bool CanBeMoved() const { return true; }
	virtual bool Move(const BoundFrame& newLoc);

	bool IsPlayerBlocked() const;

	void AddPlayerDisable() { m_playerDisabled = true; }
	void RemovePlayerDisable() { m_playerDisabled = false; }
	bool IsPlayerDisabled() const { return m_playerDisabled; }

	NavLocation GetRegisteredNavLocation() const { return m_hRegisteredNavLoc; }
	Point GetRegisteredNavLocationPosPs() const { return m_hRegisteredNavLoc.GetPosPs(); }
	bool IsRegisteredNavLocationValid() const { return m_hRegisteredNavLoc.IsValid(); }
	void InvalidateRegisteredNavLocation() { m_hRegisteredNavLoc.SetWs(m_loc.GetTranslationWs()); }

	bool IsCorrupted() const;

	bool IsInSwap() const { return m_inSwap; }
	void SetInSwap(bool inSwap) { m_inSwap = inSwap; }

	bool ShouldKillUserOnExit() const { return m_killUserOnExit; }
	void SetKillUserOnExit(bool shouldKill) { m_killUserOnExit = shouldKill; }

	Process* GetReservationHolder() const;
	ProcessHandle GetReservationHolderHandle() const;

	virtual void AdjustGameTime(TimeDelta delta) {}

	virtual void LiveUpdate(EntitySpawner& spawner);

	U16 GetPostId() const { return m_postId; }
	void SetPostId(U16 id) { m_postId = id; }

	virtual bool CheckRigidBodyIsBlocking(RigidBody* pBody, uintptr_t userData) { return false; }
	virtual void RemoveBlockingRigidBody(const RigidBody* pBody) {}

	void SetSourceCorner(const Level* pLevel, I32F iSection, I32F iCorner);
	void SetSourceEdge(const Level* pLevel, I32F iSection, I32F iEdge);

	virtual Nav::StaticBlockageMask GetObeyedStaticBlockers() const { return Nav::kStaticBlockageMaskAll; }

protected:
	virtual bool CaresAboutPlayerBlockage(const Process* pProcess) const;

	const NavPoly* FindNavPolyToRegisterTo(bool debugDraw) const;

	virtual bool ReserveInternal(Process* pProcess, TimeFrame timeout = Seconds(0.0f));
	virtual bool RegisterInternal();
	virtual void UnregisterInternal();
	virtual bool RegisterSelfToNavPoly(const NavPoly* pPoly);

#if ENABLE_NAV_LEDGES
	const NavLedge* FindNavLedgeToRegisterTo(bool debugDraw) const;
	virtual bool RegisterSelfToNavLedge(const NavLedge* pNavLedge);
#endif

	virtual bool IsAvailableInternal(bool caresAboutPlayerBlockage = true) const;
	virtual bool IsAvailableForInternal(const Process* pProcess) const;
	virtual bool IsReservedByInternal(const Process* pProcess) const;

	virtual Vector GetDefaultEntryOffsetLs() const { return kZero; }

	static CONST_EXPR U32 kSentinalValue = 0xac710200;

	U32 m_sentinal0 = kSentinalValue;

	ActionPackRegistrationParams m_regParams;

	Type m_type;
	U32 m_mgrId;
	ProcessHandle m_hOwnerProcess;
	U16 m_postId;

	NavLocation::Type m_regLocType; // what kind of nav thing do we register to (if any)?
	Point m_regPtLs;				// registration point in local space
	BoundFrame m_loc;				// action pack reference world locator
	MutableProcessHandle m_hReservationHolder;
	NavLocation m_hRegisteredNavLoc;
	ActionPack* m_pNavRegistrationListNext; // pointer to next action pack in the nav registration list
	StringId64 m_spawnerId;
	TimeFrame m_reservationExpirationTime;
	ActionPackFeatureSource m_sourceFeature;

	union
	{
		U8 m_flags;
		struct
		{
			bool m_disabled : 1;		 // used by scripts to enable/disable
			bool m_navBlocked : 1;		// has no nav mesh clearance
			bool m_playerBlocked : 1;	 // used by player to block
			bool m_playerDisabled : 1; // shadowed out because of player disable radius
			bool m_isDynamic : 1;		 // dynamically allocated action packs are destructed when unregistered
			bool m_inSwap : 1;		 // being used for a cover swap
			bool m_killUserOnExit : 1; // despawn the user after he/she exits the action pack
		};
	};

	mutable NdAtomicLock m_accessLock;

	U32 m_sentinal1 = kSentinalValue;

	friend class NavPoly;		// for maintaining m_pNavPolyListNext field
	friend class ActionPackMgr; // for RegisterInternal/UnregisterInternal
#if ENABLE_NAV_LEDGES
	friend class NavLedge; // for maintaining m_pNavPolyListNext field
#endif
};

extern bool g_debugWorldsWorstDumpster;

/// --------------------------------------------------------------------------------------------------------------- ///
#if !FINAL_BUILD
#define MsgAp(...) do { if (g_ndAiOptions.m_enableMsgAp) MsgOut(__VA_ARGS__); } while (false)
#else
#define MsgAp MsgStubFunction
#endif
