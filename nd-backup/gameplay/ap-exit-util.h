/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/nav-location.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimControl;
class SimpleNavControl;
class ArtItemAnim;
class IPathWaypoints;

namespace DC
{
	struct ApExitAnimDef;
	struct ApExitAnimList;
	struct SelfBlendParams;
}

namespace ApExit
{
	/// --------------------------------------------------------------------------------------------------------------- ///
	struct CharacterState
	{
		BoundFrame m_frame = BoundFrame(kIdentity);

		Point m_facePositionPs = kOrigin;

		MotionType m_motionType	   = kMotionTypeMax;
		StringId64 m_mtSubcategory = INVALID_STRING_ID_64;
		I32 m_requestedDcDemeanor  = -1;
		U32 m_dcWeaponAnimTypeMask = 0;

		const AnimControl* m_pAnimControl	  = nullptr;
		const SimpleNavControl* m_pNavControl = nullptr;
		const IPathWaypoints* m_pPathPs		  = nullptr;

		SkeletonId m_skelId = INVALID_SKELETON_ID;

		float m_apRegDistance = -1.0f;

		bool m_isInAp		  = false;
		bool m_matchApToAlign = false;
		bool m_stopAtPathEnd  = false;
		bool m_mirrorBase	  = false;
		bool m_testClearMotionRadial = false;

		Locator m_apMatchAlignPs = Locator(kIdentity);

		NavLocation m_apNavLocation;
		PhaseMatchDistMode m_autoPhaseDistMode = PhaseMatchDistMode::kXz;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct AvailableExit
	{
		Color GetDebugColor() const;
		void DebugDraw(const CharacterState& cs) const;
		bool HasError() const;

		const DC::ApExitAnimDef* m_pDcDef = nullptr;

		BoundFrame m_apRef	 = BoundFrame(kIdentity);
		StringId64 m_apRefId = INVALID_STRING_ID_64;

		Locator m_rotApRefPs	  = Locator(kIdentity);
		Locator m_rotPathFitLocPs = Locator(kIdentity);

		F32 m_pathFitPhase	   = 1.0f;
		F32 m_angleErrDeg	   = 0.0f;
		F32 m_animRotAngleDeg  = 0.0f;
		F32 m_alignDistErr	   = 0.0f;
		F32 m_minNavStartPhase = 0.0f;

		StringId64 m_motionMatchingSetId = INVALID_STRING_ID_64;

		bool m_playMirrored = false;

		union Flags
		{
			struct
			{
				bool m_wrongMotionType			: 1;
				bool m_wrongMotionSubCategory	: 1;
				bool m_wrongStrafeType			: 1;
				bool m_wrongDemeanor			: 1;
				bool m_wrongWeaponType			: 1;
				bool m_badStopDist				: 1;
				bool m_maxPathDistError			: 1;
				bool m_maxAngleError			: 1;
				bool m_maxRotError				: 1;
				bool m_maxAlignDistError		: 1;
				bool m_noClearLineOfMotion		: 1;
				bool m_badExitPhaseError		: 1;
				bool m_badExitPhaseWarning		: 1;
			};
			U32 m_all = 0;
		} m_errorFlags;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Does all three steps for convenience
	U32F ResolveExit(const CharacterState& cs,
					 const DC::ApExitAnimList* pSourceList,
					 const BoundFrame& apRef,
					 AvailableExit* pExitsOut,
					 const U32F maxExitsOut,
					 StringId64 apRefId);

	U32F ResolveDefaultExit(const CharacterState& cs,
							const DC::ApExitAnimList* pSourceList,
							const BoundFrame& apRef,							
							AvailableExit* pExitsOut,
							U32F maxExitsOut,
							StringId64 apRefId);

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Step 1/3
	U32F BuildInitialExitList(const CharacterState& cs,
							  const DC::ApExitAnimList* pSourceList,
							  const BoundFrame& apRef,
							  AvailableExit* pExitsOut,
							  U32F maxExitsOut,
							  StringId64 apRefId,
							  bool wantIdleExits = false);

	// Step 2/3
	void FlagInvalidExits(const CharacterState& cs,
						  AvailableExit* pExits,
						  const U32F numExits,
						  StringId64 apRefId,
						  bool wantIdleExits = false,
						  bool debugDraw = false);

	// Step 3/3
	U32F FilterInvalidExits(AvailableExit* pExits, const U32F numExits);

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool ConstructExit(const CharacterState& cs,
					   const DC::ApExitAnimDef* pExitDef,
					   const BoundFrame& apRef,
					   StringId64 apRefId,
					   AvailableExit* pExitOut);

	bool ConstructRotatedLocatorsPs(const CharacterState& cs,
									const ArtItemAnim* pAnim,
									const DC::ApExitAnimDef* pExitDef,
									const BoundFrame& animApRef,
									StringId64 animApRefId,
									AvailableExit* pExitOut);

	/// --------------------------------------------------------------------------------------------------------------- ///
	void FlagInvalidDemeanor(const CharacterState& cs, AvailableExit* pExits, U32F numExits);
	void FlagInvalidWeaponType(const CharacterState& cs, AvailableExit* pExits, U32F numExits);
	void FlagInvalidMotionTypeExits(const CharacterState& cs, AvailableExit* pExits, U32F numExits, bool wantIdleExits);
	void FlagInvalidPathExits(const CharacterState& cs,
							  AvailableExit* pExits,
							  U32F numExits,
							  StringId64 apRefId,
							  bool debugDraw = false);

	/// --------------------------------------------------------------------------------------------------------------- ///
	void DebugDrawExitRange(const CharacterState& cs, const AvailableExit& entry);

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool EntryHasClearMotionOnNavMesh(const CharacterState& cs,
									  const AvailableExit& entry,
									  const ArtItemAnim* pAnim,
									  bool debugDraw		= false,
									  DebugPrimTime debugTT = kPrimDuration1FrameAuto);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawApExitAnims(const BoundFrame& apRef,
						  const ApExit::CharacterState& cs,
						  const DC::ApExitAnimList& exitList,
						  bool wantIdleExits,
						  bool isVerbose,
						  bool validOnly,
						  StringId64 apRefId);
