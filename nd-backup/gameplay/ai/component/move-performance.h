/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/ndphys/collision-cast.h"
#include "gamelib/scriptx/h/move-performance-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NavCharacter;
class IPathWaypoints;

namespace DMENU
{
	class Menu;
}

namespace DC
{
	struct FocusDirParams;
	struct FocusPosParams;
	struct MovePerformanceTable;
	struct WallContactParams;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class MovePerformance
{
public:
	struct PerformanceInfo
	{
		Locator m_exitLocLs = Locator(kIdentity);
		Vector m_entryVecLs = kInvalidVector;
		Vector m_exitVecLs	= kInvalidVector;
		Point m_pivotLs		= kInvalidPoint;

		Point m_focusOriginLs = kInvalidPoint;
		Point m_focusOriginMaxPhaseLs = kInvalidPoint;
		Vector m_focusDirLs = kInvalidVector;
		Point m_focusPosLs	= kInvalidPoint;

		StringId64 m_inputAnimId = INVALID_STRING_ID_64;
		StringId64 m_resolvedAnimId = INVALID_STRING_ID_64;
		StringId64 m_focusDirApId	= INVALID_STRING_ID_64;
		StringId64 m_focusPosApId	= INVALID_STRING_ID_64;

		float m_focusDirMaxErrDeg  = 0.0f;
		float m_focusPosMaxDistErr = 0.0f;
		float m_duration	 = 0.0f;
		float m_initialSpeed = 0.0f;
		float m_startPhase	 = 0.0f;

		bool m_focusDirValid = false;
		bool m_focusDirError = false;
		bool m_focusPosValid = false;
		bool m_focusPosError = false;
		bool m_mirror		 = false;
	};

	struct Performance
	{
		PerformanceInfo	m_info;
		BoundFrame		m_startingApRef;
		BoundFrame		m_rotatedApRef;
		float			m_animSpeed;

		// from DC
		U32			m_motionTypeMask;
		StringId64	m_dcAnimId;
		U64			m_flags;
		float		m_maxPivotErrDeg;
		float		m_maxExitErrDeg;
		float		m_maxPathDeviation;
		float		m_exitPhase;
		float		m_minSpeedFactor;
		float		m_fitPhaseStart;
		float		m_fitPhaseEnd;
		bool		m_allowDialogLookGestures;
		StringId64	m_categoryId;
		StringId64  m_subCategoryId;
		const DC::MovePerformance* m_pDcData;

		float		m_startPhase;

		DC::SelfBlendParams	m_selfBlend;

		DC::BlendParams	m_blendIn;
		DC::BlendParams	m_blendOut;

		const DC::WallContactParams*	m_wallContact;
		DC::SelfBlendParams				m_wallContactSbIn;
		DC::SelfBlendParams				m_wallContactSbOut;
		const DC::FocusDirParams*		m_focusDir;
		const DC::FocusPosParams*		m_focusPos;
	};

	struct FindParams
	{
		const IPathWaypoints* m_pPathPs = nullptr;
		 
		StringId64	m_categoryId = SID("match-all");
		StringId64  m_subCategoryId = SID("match-all");

		StringId64  m_excludePerformancesWithAnimId = INVALID_STRING_ID_64;
		StringId64 m_poiType = INVALID_STRING_ID_64;

		Vector		m_moveDirPs = kZero;
		Point		m_focusPosPs = kOrigin;

		MotionType	m_reqMotionType = kMotionTypeMax;
		float		m_speed = 0.0f;
		float		m_minRemainingPathDist = 0.0f;

		I32F m_debugDrawIndex = -1;
		I32F m_debugEditIndex = -1;

		bool m_focusPosValid	 = false;
		bool m_allowHoldingLeash = false;
		bool m_gesturesOnly		 = false;
		bool m_onHorse = false;
		bool m_allowStoppingPerformance = false;
		bool m_debugDraw = false;
		bool m_debugEdit = false;
	};

	MovePerformance();

	bool AllocateCache(U32 cacheSize);
	void RebuildCache(const NavCharacter* pNavChar);
	void TryRebuildCache(const NavCharacter* pNavChar);
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	void ValidateTable(const NavCharacter* pNavChar, StringId64 tableId) const;
	bool AddTable(const NavCharacter* pNavChar, StringId64 entryId, StringId64 tableId);
	StringId64 GetTableId(StringId64 entryId) const;
	bool RemoveTable(const NavCharacter* pNavChar, StringId64 entryId);
	bool HasTable(const NavCharacter* pNavChar, StringId64 entryId) const;

	static void CalcStartPhase(const NavCharacter* pNavChar, Performance* pPerformance);

	void KickContactRayCasts(const NavCharacter* pNavChar, Vector_arg moveDirPs);

	U32F GetMovePerformanceCount() const;
	const DC::MovePerformance* GetPerformanceDcData(int index) const;
	U32F FindPerformances(const NavCharacter* pNavChar,
						  const FindParams& params,
						  U32F maxPerformances,
						  Performance* performancesOut) const;

	bool FindRandomPerformance(const NavCharacter* pNavChar,
							   const FindParams& params,
							   Performance* pPerformanceOut) const;

	virtual void DebugDraw() const;

private:
	static CONST_EXPR size_t kMaxMpTableEntries = 4;
	static CONST_EXPR size_t kMaxContactRays	= 4;

	struct TableEntry
	{
		typedef ScriptPointer<const DC::MovePerformanceTable> TablePtr;

		StringId64 m_entryId; // Arbitrary label used to remove entry
		TablePtr m_pTable;	  // DC move performance table
	};

	bool LookupCachedPerformanceInfo(const StringId64 animId,
									 bool mirror,
									 PerformanceInfo* pInfoOut,
									 float startPhase) const;

	bool BuildPerformanceInfo(const NavCharacter* pNavChar,
							  const Performance* pPerformance,
							  float startPhase,
							  PerformanceInfo* pInfoOut) const;

	bool BuildPerformance(const NavCharacter* pNavChar,
						  Vector_arg moveDirPs,
						  Performance* pPerformanceOut,
						  float startPhase,
						  bool debugDraw) const;

	bool IsInFocus(const NavCharacter* pNavChar,
				   const FindParams& params,
				   const Performance* pPerformance,
				   I32F tableIndex,
				   bool debugDraw) const;

	bool IsFacingPath(const NavCharacter* pNavChar, const FindParams& params, bool debugDraw) const;

	bool RequiredSpeedMet(const NavCharacter* pNavChar,
						  const FindParams& params,
						  const Performance* pPerformance,
						  bool debugDraw) const;

	bool TryToFitPerformance(const NavCharacter* pNavChar,
							 const FindParams& params,
							 Performance* pPerformance,
							 bool debugDraw = false) const;

	bool GetAnimContactLocPs(Locator* pAnimContactLocPs,
							 const NavCharacter* pNavChar,
							 StringId64 animId,
							 const DC::WallContactParams& wallContact,
							 const Locator& startingLocPs,
							 bool mirror) const;

	bool CheckContactPerformanceContact(const NavCharacter* pNavChar,
										Performance* pPerformance,
										bool debugDraw = false) const;

	bool PerformanceHasClearMotion(const NavCharacter* pNavChar,
								   const Performance* pPerformance,
								   const DC::SelfBlendParams* pSelfBlendIn,
								   const DC::SelfBlendParams* pSelfBlendOut = nullptr,
								   bool debugDraw = false) const;

	bool IsPathDeviationWithinRange(const NavCharacter* pNavChar,
									const Performance* pPerformance,
									const FindParams& params,
									const DC::SelfBlendParams* pSelfBlendIn,
									const DC::SelfBlendParams* pSelfBlendOut = nullptr,
									bool debugDraw = false) const;

	TableEntry m_tableEntries[kMaxMpTableEntries];
	U32 m_numUsedTableEntries;

	mutable RayCastJob	m_raycastJob;
	StringId64			m_contactRayAnimIds[kMaxContactRays];

	AnimOverlaySnapshotHash	m_cacheOverlayHash;
	U32						m_cacheSize;
	U32						m_numCachedEntries;
	PerformanceInfo*		m_pCachedPerformanceInfo;
	bool					m_cacheValid;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void CreateMovePerformanceDevMenu(DMENU::Menu* pMenu);

