/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/action-pack.h"

#include "ndlib/net/nd-net-info.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/faction-mgr.h"
#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-util.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/nd-action-pack-util.h"
#include "gamelib/gameplay/nav/perch-action-pack.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/level/level.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const float ActionPack::kActionPackRegistrationProbeLength = 0.25f;
bool g_debugWorldsWorstDumpster = false;

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackRegistrationParams::ActionPackRegistrationParams()
	: m_pAllocLevel(nullptr)
	, m_pRegistrationLevel(nullptr)
	, m_bindId(INVALID_STRING_ID_64)
	, m_probeDist(ActionPack::kActionPackRegistrationProbeLength)
	, m_yThreshold(1.5f)
	, m_regPtLs(kOrigin)
	, m_readOnly(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack::ActionPack(Type tt)
	: m_type(tt)
	, m_mgrId(ActionPackMgr::kInvalidMgrId)
	, m_regLocType(NavLocation::Type::kNavPoly)
	, m_regPtLs(kOrigin)
	, m_pNavRegistrationListNext(nullptr)
	, m_spawnerId(INVALID_STRING_ID_64)
	, m_disabled(false)
	, m_navBlocked(false)
	, m_playerBlocked(false)
	, m_playerDisabled(false)
	, m_isDynamic(false)
	, m_inSwap(false)
	, m_killUserOnExit(false)
	, m_reservationExpirationTime(Seconds(0.0f))
	, m_postId(kInvalidPostId)
{
	m_hRegisteredNavLoc.SetWs(kInvalidPoint);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack::ActionPack(Type tt, Point_arg regPtLs, const BoundFrame& bf, const Level* pAllocLevel)
	: m_type(tt)
	, m_mgrId(ActionPackMgr::kInvalidMgrId)
	, m_regLocType(NavLocation::Type::kNavPoly)
	, m_regPtLs(regPtLs)
	, m_loc(bf)
	, m_pNavRegistrationListNext(nullptr)
	, m_disabled(false)
	, m_navBlocked(false)
	, m_playerBlocked(false)
	, m_playerDisabled(false)
	, m_isDynamic(false)
	, m_inSwap(false)
	, m_killUserOnExit(false)
	, m_reservationExpirationTime(Seconds(0.0f))
	, m_postId(kInvalidPostId)
	, m_spawnerId(INVALID_STRING_ID_64)
{
	InvalidateRegisteredNavLocation();

	m_regParams.m_pAllocLevel = pAllocLevel;
	m_regParams.m_pRegistrationLevel = pAllocLevel;

	ActionPackMgr::Get().LoginActionPack(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack::ActionPack(Type tt, Point_arg regPtLs, const BoundFrame& bf, const EntitySpawner* pSpawner)
	: m_type(tt)
	, m_mgrId(ActionPackMgr::kInvalidMgrId)
	, m_regLocType(NavLocation::Type::kNavPoly)
	, m_regPtLs(regPtLs)
	, m_loc(bf)
	, m_pNavRegistrationListNext(nullptr)
	, m_disabled(false)
	, m_navBlocked(false)
	, m_playerBlocked(false)
	, m_playerDisabled(false)
	, m_isDynamic(false)
	, m_inSwap(false)
	, m_killUserOnExit(false)
	, m_reservationExpirationTime(Seconds(0.0f))
	, m_postId(kInvalidPostId)
	, m_spawnerId(pSpawner ? pSpawner->NameId() : INVALID_STRING_ID_64)
{
	InvalidateRegisteredNavLocation();

	if (pSpawner)
	{
		m_regParams.m_pAllocLevel = pSpawner->GetLevel();
		m_regParams.m_pRegistrationLevel = pSpawner->GetLevel();
	}

	ActionPackMgr::Get().LoginActionPack(this);
}

ActionPack::ActionPack(Type tt, Point_arg regPtLs, const BoundFrame& bfLoc, const Level* pAllocLevel, const Level* pRegLevel)
	: m_type(tt)
	, m_mgrId(ActionPackMgr::kInvalidMgrId)
	, m_regLocType(NavLocation::Type::kNavPoly)
	, m_regPtLs(regPtLs)
	, m_loc(bfLoc)
	, m_pNavRegistrationListNext(nullptr)
	, m_disabled(false)
	, m_navBlocked(false)
	, m_playerBlocked(false)
	, m_playerDisabled(false)
	, m_isDynamic(false)
	, m_inSwap(false)
	, m_killUserOnExit(false)
	, m_reservationExpirationTime(Seconds(0.0f))
	, m_postId(kInvalidPostId)
	, m_spawnerId(INVALID_STRING_ID_64)
{
	InvalidateRegisteredNavLocation();

	m_regParams.m_pAllocLevel = pAllocLevel;
	m_regParams.m_pRegistrationLevel = pRegLevel;

	ActionPackMgr::Get().LoginActionPack(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack::~ActionPack()
{
	Logout();

	m_hRegisteredNavLoc.SetWs(kInvalidPoint);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::Update()
{
	if (const Process* pHolder = m_hReservationHolder.ToProcess())
	{
		const Clock* pClock = pHolder->GetClock();
		if (pClock->GetCurTime() > m_reservationExpirationTime)
		{
			Release(pHolder);
			m_reservationExpirationTime = Seconds(0.0f);
		}
	}
	else
	{
		m_reservationExpirationTime = Seconds(0.0f);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::Logout()
{
	if (m_sourceFeature.m_featureLevelId != INVALID_STRING_ID_64)
	{
		// GetLevel can crash if we're in the middle of unloading. Dynamic CAPs never have featureLevelIds though.
		if (Level* pFeatureLevel = EngineComponents::GetLevelMgr()->GetLevel(m_sourceFeature.m_featureLevelId))
		{
			pFeatureLevel->RemoveFeatureGeneratedAp(m_sourceFeature.m_iSection,
				m_sourceFeature.m_iCorner,
				m_sourceFeature.m_iEdge);
		}
	}

	if (IsRegistered())
	{
		UnregisterInternal();
	}

	if (GetMgrId() != ActionPackMgr::kInvalidMgrId)
	{
		ActionPackMgr::Get().LogoutActionPack(this);
	}

	NAV_ASSERT(m_pNavRegistrationListNext == nullptr);

	const Level* pAllocLevel = GetAllocLevel();
	if (IsDynamic() && pAllocLevel)
	{
		NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());
		Level* pMutableAllocLevel = const_cast<Level*>(pAllocLevel);
		if (GetType() == kCoverActionPack)
		{
			pAllocLevel = nullptr;
			pMutableAllocLevel->FreeCoverActionPack(static_cast<CoverActionPack*>(this));
			// !!! no writing to this after memory deallocated !!!
		}
		else if (GetType() == kPerchActionPack)
		{
			pAllocLevel = nullptr;
			pMutableAllocLevel->FreePerchActionPack(static_cast<PerchActionPack*>(this));
			// !!! no writing to this after memory deallocated !!!
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* ActionPack::GetName() const
{
	static StringBuilder<256> s_desc;
	s_desc.clear();

	const StringId64 spawnerId = GetSpawnerId();
	if (spawnerId != INVALID_STRING_ID_64)
	{
		s_desc.append_format("%s <0x%x> [%s]", GetTypeName(), GetMgrId(), DevKitOnly_StringIdToString(spawnerId));
	}
	else
	{
		s_desc.append_format("%s <0x%x> @ 0x%p", GetTypeName(), GetMgrId(), this);
	}

	return s_desc.c_str();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::GetStatusDescription(IStringBuilder* pStrOut) const
{
	STRIP_IN_FINAL_BUILD;

	if (!IsEnabled())
	{
		pStrOut->append("disabled");
	}
	else if (IsPlayerBlocked())
	{
		pStrOut->append("player-blocked");
	}
	else if (IsReserved())
	{
		pStrOut->append("reserved");
	}
	else if (!IsAvailable())
	{
		pStrOut->append("used");
	}
	else
	{
		pStrOut->append("free");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::Reset()
{
	// All action packs are enabled by default
	bool enable = true;

	if (const EntitySpawner* pSpawner = GetSpawner())
	{
		enable = pSpawner->GetData<bool>(SID("enable-when-spawned"), enable);
	}

	Enable(enable);

	SetInSwap(false);

	if (IsPlayerBlocked())
	{
		RemovePlayerBlock();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ActionPackMgr::Get().RelocateActionPack(this, deltaPos, lowerBound, upperBound);

	if (!m_hRegisteredNavLoc.IsNull() && !m_regParams.m_readOnly)
	{
		NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

		if (NavPoly* pNavPoly = const_cast<NavPoly*>(m_hRegisteredNavLoc.ToNavPoly()))
		{
			pNavPoly->RelocateActionPack(this, deltaPos, lowerBound, upperBound);
		}
#if ENABLE_NAV_LEDGES
		else if (NavLedge* pNavLedge = const_cast<NavLedge*>(m_hRegisteredNavLoc.ToNavLedge()))
		{
			pNavLedge->RelocateActionPack(this, deltaPos, lowerBound, upperBound);
		}
#endif // ENABLE_NAV_LEDGES
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::SetOwnerProcess(Process* pProc)
{
	m_hOwnerProcess = pProc;

	ActionPackMgr& apMgr = ActionPackMgr::Get();

	if (pProc)
	{
		apMgr.FlagProcessOwnership(this);
	}
	else
	{
		apMgr.ClearProcessOwnership(this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Level* ActionPack::GetRegistrationLevel() const
{
	return m_regParams.m_pRegistrationLevel;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::RequestRegistration(const ActionPackRegistrationParams& params)
{
	m_regParams = params;

	ActionPackMgr::Get().RequestRegistration(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::RequestUnregistration()
{
	ActionPackMgr::Get().RequestUnregistration(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::RegisterImmediately(const ActionPackRegistrationParams& params)
{
	NAV_ASSERT(!IsRegistered());

	m_regParams = params;

	if (RegisterInternal())
	{
		ActionPackMgr& apMgr = ActionPackMgr::Get();
		AtomicLockJanitorWrite_Jls writeLock(apMgr.GetLock(), FILE_LINE_FUNC);
		apMgr.Register(this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::UnregisterImmediately()
{
	NAV_ASSERT(IsRegistered());

	UnregisterInternal();

	{
		ActionPackMgr& apMgr = ActionPackMgr::Get();
		AtomicLockJanitorWrite_Jls writeLock(apMgr.GetLock(), FILE_LINE_FUNC);
		apMgr.Unregister(this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsRegistered() const
{
	return (this == ActionPackMgr::Get().LookupRegisteredActionPack(GetMgrId()));
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsLoggedIn() const
{
	return (this == ActionPackMgr::Get().LookupLoggedInActionPack(GetMgrId()));
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsAssigned() const
{
	return GetMgrId() != ActionPackMgr::kInvalidMgrId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsPlayerBlocked() const
{
	if (IsInSwap())
	{
		return false;
	}

	return m_playerBlocked;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsReserved() const
{
	return m_hReservationHolder.HandleValid();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsReservedBy(const Process* pProcess) const
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	return IsReservedByInternal(pProcess);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsReservedByInternal(const Process* pProcess) const
{
	bool isReserved = (pProcess != nullptr) && (m_hReservationHolder.ToProcess() == pProcess);
	return isReserved;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsAvailable(bool caresAboutPlayerBlockage /* = true */) const
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	return IsAvailableInternal(caresAboutPlayerBlockage);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsAvailableFor(const Process* pProcess) const
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	return IsAvailableForInternal(pProcess);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::Reserve(Process* pProcess, TimeFrame timeout /* = Seconds(0.0f) */)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	bool success = false;
	if (IsAvailableForInternal(pProcess))
	{
		success = ReserveInternal(pProcess, timeout);
	}

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::ReserveInternal(Process* pProcess, TimeFrame timeout /* = Seconds(0.0f) */)
{
	MsgAp("ActionPack::ReserveInternal [%s] : %s\n", GetName(), pProcess ? pProcess->GetName() : "<null>");

	m_hReservationHolder = pProcess;

	if (timeout > Seconds(0.0f))
	{
		m_reservationExpirationTime = pProcess->GetClock()->GetCurTime() + timeout;
		ActionPackMgr::Get().SetUpdatesEnabled(this, true);
	}
	else
	{
		m_reservationExpirationTime = Seconds(0.0f);
		ActionPackMgr::Get().SetUpdatesEnabled(this, NeedsUpdate());
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::Release(const Process* pProcess)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	const Process* pHolder = m_hReservationHolder.ToProcess();

	NAV_ASSERT(pProcess == pHolder);

	if (pHolder == pProcess)
	{
		MsgAp("ActionPack::Release [%s] : %s\n", GetName(), pProcess ? pProcess->GetName() : "<null>");

		m_hReservationHolder = nullptr;
	}
	else
	{
		MsgAp("ActionPack::Release [%s] : %s (act: %s) -- IGNORED\n",
			  GetName(),
			  pProcess ? pProcess->GetName() : "<null>",
			  pHolder ? pHolder->GetName() : "<null>");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::Enable(bool enable)
{
	m_disabled = !enable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessHandle ActionPack::GetReservationHolderHandle() const
{
	return m_hReservationHolder;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Process* ActionPack::GetReservationHolder() const
{
	return m_hReservationHolder.ToMutableProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::LiveUpdate(EntitySpawner& spawner)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	if (IsRegistered())
	{
		NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);
		UnregisterImmediately();
	}

	SetBoundFrame(spawner.GetBoundFrame());

	RequestRegistration(m_regParams);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::SetSourceCorner(const Level* pLevel, I32F iSection, I32F iCorner)
{
	if (!pLevel)
	{
		m_sourceFeature = ActionPackFeatureSource();
		return;
	}

	m_sourceFeature.m_featureLevelId = pLevel->GetNameId();
	m_sourceFeature.m_iSection		 = iSection;
	m_sourceFeature.m_iCorner		 = iCorner;
	m_sourceFeature.m_iEdge = -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::SetSourceEdge(const Level* pLevel, I32F iSection, I32F iEdge)
{
	if (!pLevel)
	{
		m_sourceFeature = ActionPackFeatureSource();
		return;
	}

	m_sourceFeature.m_featureLevelId = pLevel->GetNameId();
	m_sourceFeature.m_iSection		 = iSection;
	m_sourceFeature.m_iEdge	  = iEdge;
	m_sourceFeature.m_iCorner = -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::DebugDraw(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;
	Locator locWs = GetBoundFrame().GetLocator();

	g_prim.Draw(DebugCoordAxes(locWs), tt);
	g_prim.Draw(DebugSphere(Sphere(GetRegistrationPointWs(), 0.03f), kColorMagentaTrans), tt);

	if (g_navMeshDrawFilter.m_drawApDetail)
	{
		g_prim.Draw(DebugString(locWs.GetTranslation(),
								StringBuilder<64>("MgrId: 0x%x", GetMgrId()).c_str(),
								kColorGreen,
								0.5f),
					tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::DebugDrawRegistrationFailure() const
{
	STRIP_IN_FINAL_BUILD;

	switch (m_regLocType)
	{
	case NavLocation::Type::kNavPoly:
		if (const NavPoly* pNavPoly = FindNavPolyToRegisterTo(true))
		{
			NavLocation navLoc;
			navLoc.SetWs(GetRegistrationPointWs(), pNavPoly);

			HasNavMeshClearance(navLoc, true);
		}
		break;

#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		FindNavLedgeToRegisterTo(true);
		break;
#endif
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point ActionPack::GetDefaultEntryPointWs(Scalar_arg offset) const
{
	const Locator locWs = m_loc.GetLocator();
	Point regPtLs = m_regPtLs;

	const Vector offsetBaseVectorLs = SafeNormalize(m_regPtLs - kOrigin, kZero);
	const Vector offsetVectorLs = offset * offsetBaseVectorLs;
	regPtLs += offsetVectorLs;

	const Vector entryOffsetLs = GetDefaultEntryOffsetLs();
	regPtLs += entryOffsetLs;

	return locWs.TransformPoint(regPtLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point ActionPack::GetDefaultEntryPointPs(Scalar_arg offset) const
{
	const Locator locPs = m_loc.GetLocatorPs();
	Point regPtLs = m_regPtLs;

	const Vector offsetBaseVectorLs = SafeNormalize(m_regPtLs - kOrigin, kZero);
	const Vector offsetVectorLs = offset * offsetBaseVectorLs;
	regPtLs += offsetVectorLs;
	regPtLs += GetDefaultEntryOffsetLs();

	return locPs.TransformPoint(regPtLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame ActionPack::GetDefaultEntryBoundFrame(Scalar_arg offset) const
{
	BoundFrame apRef = GetBoundFrame();
	Vector regVtLs = GetRegistrationPointLs() - kOrigin;

	const Vector offsetBaseVectorLs = SafeNormalize(m_regPtLs - kOrigin, kZero);
	const Vector offsetVectorLs = offset * offsetBaseVectorLs;
	regVtLs += offsetVectorLs;

	Vector entryOffsetLs = GetDefaultEntryOffsetLs();
	regVtLs += entryOffsetLs;

	Vector regVtWs = apRef.GetLocatorWs().TransformVector(regVtLs);
	apRef.AdjustTranslationWs(regVtWs);

	return apRef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point ActionPack::GetRegistrationPointWs() const
{
	const Locator locWs = m_loc.GetLocator();
	return locWs.TransformPoint(m_regPtLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point ActionPack::GetCoverPositionPs() const
{
	const Locator locPs = m_loc.GetLocatorPs();
	Point ptLs = m_regPtLs + VECTOR_LC(0.0f, 0.5f, -0.25f);
	return locPs.TransformPoint(ptLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point ActionPack::GetVisibilityPositionPs() const
{
	return m_loc.GetLocatorPs().Pos();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point ActionPack::GetFireSidePositionWs() const
{
	return m_loc.GetLocatorWs().Pos();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point ActionPack::GetFireOverPositionWs() const
{
	return m_loc.GetLocatorWs().Pos();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point ActionPack::GetFireSidePositionPs() const
{
	return m_loc.GetLocatorPs().Pos();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point ActionPack::GetFireOverPositionPs() const
{
	return m_loc.GetLocatorPs().Pos();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsCorrupted() const
{
	return (m_sentinal0 != kSentinalValue) || (m_sentinal1 != kSentinalValue);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EntitySpawner* ActionPack::GetSpawner() const
{
	if (const Level* pAllocLevel = GetAllocLevel())
	{
		return pAllocLevel->LookupEntitySpawnerByNameId(m_spawnerId);
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EntitySpawner* ActionPack::GetParentSpawner() const
{
	if (const EntitySpawner* pSpawner = GetSpawner())
	{
		return pSpawner->GetParentSpawner();
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 ActionPack::GetParentSpawnerId() const
{
	if (const EntitySpawner* pParentSpawner = GetParentSpawner())
	{
		return pParentSpawner->NameId();
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Process* ActionPack::GetProcess() const
{
	// Some action packs are associated with actual objects in the game world.
	// (e.g. interactable objects drop CAPs so NPCs can interact with them.)
	const EntitySpawner* pSpawner = GetSpawner();
	if (pSpawner)
	{
		const Process* pProc = pSpawner->GetProcess();
		if (pProc)
		{
			return pProc;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Process* ActionPack::GetProcess()
{
	// Some action packs are associated with actual objects in the game world.
	// (e.g. interactable objects drop CAPs so NPCs can interact with them.)
	const EntitySpawner* pSpawner = GetSpawner();
	if (pSpawner)
	{
		Process* pProc = pSpawner->GetProcess();
		if (pProc)
		{
			return pProc;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ActionPack::GetReverseActionPackMgrId() const
{
	return ActionPackMgr::kInvalidMgrId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsAvailableInternal(bool caresAboutPlayerBlockage /* = true */) const
{
	const bool playerBlocked = caresAboutPlayerBlockage ? IsPlayerBlocked() : false;
	bool isAvail = !m_disabled && !m_navBlocked && !playerBlocked && !IsReserved();
	return isAvail;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::IsAvailableForInternal(const Process* pProcess) const
{
	if (m_navBlocked)
	{
		return false;
	}

	const bool caresAboutPlayerBlockage = CaresAboutPlayerBlockage(pProcess);

	if (!IsEnabled())
		return false;

	bool isAvail = false;

	if (IsReservedByInternal(pProcess))
	{
		const bool playerBlocked = caresAboutPlayerBlockage ? IsPlayerBlocked() : false;
		isAvail = !m_disabled && !playerBlocked;
	}
	else
	{
		isAvail = IsAvailableInternal(caresAboutPlayerBlockage);
	}

	return isAvail;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::CaresAboutPlayerBlockage(const Process* pProcess) const
{
	if (const NavCharacter* pNavChar = NavCharacter::FromProcess(pProcess))
	{
		if (!pNavChar->CaresAboutPlayerBlockage(this))
		{
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPoly* ActionPack::FindNavPolyToRegisterTo(bool debugDraw) const
{
	const NdGameObject* pPlatformOwner = m_regParams.m_hPlatformOwner.ToProcess();
	const PlatformControl* pPlatformControl = pPlatformOwner ? pPlatformOwner->GetPlatformControl() : nullptr;
	const Level* pRegLevel = GetRegistrationLevel();

	FindBestNavMeshParams findMesh;
	findMesh.m_pointWs = GetRegistrationPointWs();
	findMesh.m_bindSpawnerNameId = m_regParams.m_bindId;
	findMesh.m_yThreshold = 1.5f;

	FindActionPackNavMesh(this, findMesh, pRegLevel, pPlatformControl, debugDraw);

	return findMesh.m_pNavPoly;
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
const NavLedge* ActionPack::FindNavLedgeToRegisterTo(bool debugDraw) const
{
	const NdGameObject* pPlatformOwner = m_regParams.m_hPlatformOwner.ToProcess();
	const PlatformControl* pPlatformControl = pPlatformOwner ? pPlatformOwner->GetPlatformControl() : nullptr;
	const Level* pAllocLevel = GetAllocLevel();

	FindNavLedgeGraphParams findGraph;
	findGraph.m_pointWs = GetRegistrationPointWs();
	findGraph.m_bindSpawnerNameId = m_regParams.m_bindId;
	findGraph.m_searchRadius = 1.5f;

	FindActionPackNavLedgeGraph(this, findGraph, pAllocLevel, pPlatformControl, debugDraw);

	return findGraph.m_pNavLedge;
}
#endif // ENABLE_NAV_LEDGES

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::RegisterInternal()
{
	PROFILE(AI, ActionPack_RegInternal);

	const bool readOnly = m_regParams.m_readOnly;

	NAV_ASSERT(readOnly ? NavMeshMgr::GetGlobalLock()->IsLocked() : NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	bool registered = false;

	const Point regPosWs = GetRegistrationPointWs();
	m_hRegisteredNavLoc.SetWs(regPosWs);
	m_pNavRegistrationListNext = nullptr;

	switch (m_regLocType)
	{
	case NavLocation::Type::kNavPoly:
		if (const NavPoly* pRegPoly = FindNavPolyToRegisterTo(false))
		{
			NavLocation navLoc;
			navLoc.SetWs(regPosWs, pRegPoly);

			if (HasNavMeshClearance(navLoc))
			{
				if (readOnly)
				{
					m_hRegisteredNavLoc.SetWs(regPosWs, pRegPoly);
				}
				else
				{
					RegisterSelfToNavPoly(pRegPoly);
				}

				registered = true;
			}
		}
		break;

#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		if (const NavLedge* pRegLedge = FindNavLedgeToRegisterTo(false))
		{
			if (readOnly)
			{
				m_hRegisteredNavLoc.SetWs(regPosWs, pRegLedge);
			}
			else
			{
				RegisterSelfToNavLedge(pRegLedge);
			}

			registered = true;
		}
		break;
#endif // ENABLE_NAV_LEDGES

	default:
		registered = true;
		break;
	}

	return registered;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::UnregisterInternal()
{
	MsgAp("ActionPack::UnregisterInternal [%s] : %s\n",
		  GetName(),
		  m_hReservationHolder.ToProcess() ? m_hReservationHolder.ToProcess()->GetName() : "<null>");

	const bool readOnly = m_regParams.m_readOnly;

	NAV_ASSERT(readOnly ? NavMeshMgr::GetGlobalLock()->IsLocked() : NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	switch (m_regLocType)
	{
	case NavLocation::Type::kNavPoly:
		if (readOnly)
		{
			m_pNavRegistrationListNext = nullptr;
		}
		else
		{
			if (NavPoly* pNavPoly = const_cast<NavPoly*>(m_hRegisteredNavLoc.ToNavPoly()))
			{
				pNavPoly->UnregisterActionPack(this);
			}
			else
			{
				m_pNavRegistrationListNext = nullptr;
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		if (readOnly)
		{
			m_pNavRegistrationListNext = nullptr;
		}
		else
		{
			if (NavLedge* pNavLedge = const_cast<NavLedge*>(m_hRegisteredNavLoc.ToNavLedge()))
			{
				pNavLedge->UnregisterActionPack(this);
			}
			else
			{
				m_pNavRegistrationListNext = nullptr;
			}
		}
		break;
#endif // ENABLE_NAV_LEDGES
	}

	m_hRegisteredNavLoc.SetWs(GetRegistrationPointWs());
	NAV_ASSERT(m_pNavRegistrationListNext == nullptr);

	m_hReservationHolder = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::RegisterSelfToNavPoly(const NavPoly* pPoly)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	NAV_ASSERT(m_pNavRegistrationListNext == nullptr);
	NAV_ASSERT(!m_hRegisteredNavLoc.IsValid());
	NAV_ASSERT(pPoly);

	pPoly->ValidateRegisteredActionPacks();

	NavPoly* pMutablePoly = const_cast<NavPoly*>(pPoly);

	pMutablePoly->RegisterActionPack(this);

	m_hRegisteredNavLoc.SetWs(GetRegistrationPointWs(), pPoly);

	pMutablePoly->ValidateRegisteredActionPacks();

	return true;
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::RegisterSelfToNavLedge(const NavLedge* pNavLedge)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	NAV_ASSERT(m_pNavRegistrationListNext == nullptr);
	NAV_ASSERT(!m_hRegisteredNavLoc.IsValid());
	NAV_ASSERT(pNavLedge);
	pNavLedge->ValidateRegisteredActionPacks();

	NavLedge* pMutableLedge = const_cast<NavLedge*>(pNavLedge);

	pMutableLedge->RegisterActionPack(this);

	m_hRegisteredNavLoc.SetWs(GetRegistrationPointWs(), pMutableLedge);

	pMutableLedge->ValidateRegisteredActionPacks();

	return true;
}
#endif // ENABLE_NAV_LEDGES

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::HasNavMeshClearance(const NavLocation& navLoc,
									 bool debugDraw /* = false */,
									 DebugPrimTime tt /* = kPrimDuration1FramePauseable */) const
{
	bool clear = true;

	const NavMesh* pMesh = navLoc.ToNavMesh();
	const NavPoly* pPoly = navLoc.ToNavPoly();

	if (pMesh && pPoly)
	{
		const Point regPosWs = GetRegistrationPointWs();

		NavMesh::ClearanceParams params;
		params.m_pStartPoly	  = pPoly;
		params.m_dynamicProbe = false;
		params.m_obeyedStaticBlockers = GetObeyedStaticBlockers();
		params.m_obeyStealthBoundary  = false;
		params.m_crossLinks = true;
		params.m_point		= pMesh->WorldToLocal(regPosWs);
		params.m_radius		= 0.15f;

		const NavMesh::ProbeResult res = pMesh->CheckClearanceLs(&params);

		if (res != NavMesh::ProbeResult::kReachedGoal)
		{
			clear = false;
		}

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			StringBuilder<256> desc;
			desc.format("%s", DevKitOnly_StringIdToString(m_spawnerId));

			g_prim.Draw(DebugCross(regPosWs, 0.05f, clear ? kColorGreenTrans : kColorRedTrans, kPrimEnableHiddenLineAlpha), tt);
			g_prim.Draw(DebugCircle(regPosWs, kUnitYAxis, params.m_radius, clear ? kColorGreen : kColorRed, kPrimEnableHiddenLineAlpha), tt);

			if (!clear)
			{
				desc.append("\n");
				switch (res)
				{
				case NavMesh::ProbeResult::kErrorStartedOffMesh:
					desc.append("Off Mesh");
					break;
				case NavMesh::ProbeResult::kErrorStartNotPassable:
					desc.append("Start Not Passable");
					break;
				case NavMesh::ProbeResult::kHitEdge:
					{
						desc.append("Hit Edge");
						const Point impactPtWs = pMesh->LocalToWorld(params.m_impactPoint);
						g_prim.Draw(DebugCross(impactPtWs, 0.025f, kColorRed, kPrimEnableHiddenLineAlpha), tt);
						g_prim.Draw(DebugArrow(regPosWs, impactPtWs, kColorRed, 0.2f, kPrimEnableHiddenLineAlpha), tt);
					}
					break;
				}
			}

			g_prim.Draw(DebugString(regPosWs, desc.c_str(), clear ? kColorGreen : kColorRed, 0.8f), tt);
		}
	}

	return clear;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPack::RefreshNavMeshClearance()
{
	const NavLocation regNavLoc = GetRegisteredNavLocation();
	const bool clear = HasNavMeshClearance(regNavLoc);
	m_navBlocked = !clear;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPack::Move(const BoundFrame& newLoc)
{
	if (!CanBeMoved())
		return false;

	NavMeshLockJanitor navLock(m_regParams.m_readOnly, FILE_LINE_FUNC);

	if (IsRegistered())
	{
		UnregisterImmediately();
	}

	m_loc = newLoc;

	RegisterImmediately(m_regParams);

	return true;
}
