/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/process/bound-frame.h"

#include "gamelib/gameplay/ai/agent/nav-character-adapter.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/nav/nav-handle.h"
#include "gamelib/gameplay/nav/nav-location.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemAnim;
class NavCharacter;
class SimpleNavControl;

namespace DC
{
	struct ApEntryItem;
	struct ApEntryItemList;
}

class ActionPack;
class AnimControl;

namespace ApEntry
{
	/// --------------------------------------------------------------------------------------------------------------- ///
	struct CharacterState
	{
		void InitCommon(const NavCharacter* pNavChar);
		void InitCommon(const NavCharacterAdapter pNavChar);

		BoundFrame m_frame		  = BoundFrame(kIdentity);
		float m_frameRotWindowDeg = 0.0f;
		Point m_frameRotPivotLs	  = kOrigin;

		NavLocation::Type m_entryNavLocType = NavLocation::Type::kNavPoly;

		MotionType m_motionType		  = kMotionTypeMax;
		StringId64 m_mtSubcategory	  = INVALID_STRING_ID_64;
		float m_rangedEntryRadius	  = 0.0f;
		float m_maxEntryErrorAngleDeg = 0.0f;
		float m_maxFacingDiffAngleDeg = 0.0f;
		float m_entryAngleCheckDist	  = 0.25f;

		const AnimControl* m_pAnimControl	  = nullptr;
		const SimpleNavControl* m_pNavControl = nullptr;
		SkeletonId m_skelId = INVALID_SKELETON_ID;
		Vector m_velocityPs = kZero;

		Sphere m_manualObstructionWs = Sphere(kZero);

		bool m_moving	= false;
		bool m_strafing = false;
		bool m_forceDefaultEntry		 = false;
		bool m_testClearMotionToPhaseMax = false;
		bool m_testClearMotionRadial	 = false;
		bool m_testClearMotionInitRadial = true;
		bool m_mirrorBase = false;

		bool m_matchApToAlign	 = false;
		Locator m_apMatchAlignPs = Locator(kIdentity);

		Point m_apRotOriginLs = kOrigin;

		U32 m_dcDemeanorValue = 0;
		U32 m_dcWeaponAnimTypeMask = 0;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct AvailableEntry
	{
		Color GetDebugColor() const;
		void DebugDraw(const CharacterState& cs, const ActionPack& ap, const BoundFrame& apRef) const;
		
		ArtItemAnimHandle m_anim = ArtItemAnimHandle();

		BoundFrame m_rotatedApRef	= BoundFrame(kIdentity);
		BoundFrame m_sbApRef		= BoundFrame(kIdentity);
		Locator m_charAlignUsedPs	= Locator(kIdentity);
		Locator m_entryAlignPs		= Locator(kIdentity);
		StringId64 m_resolvedAnimId = INVALID_STRING_ID_64;
		Vector m_entryVelocityPs	= kZero;

		float m_distToEntryXz = 0.0f;
		float m_phaseMin	  = 0.0f;
		float m_phaseMax	  = 0.0f;
		float m_phase		  = 0.0f;
		float m_skipPhase	  = -1.0f;

		bool m_playMirrored = false;

		const DC::ApEntryItem* m_pDcDef = nullptr;

		union
		{
			struct
			{
				bool m_wrongDemeanor			: 1;
				bool m_wrongDistanceCat			: 1;
				bool m_invalidEntryAngle		: 1;
				bool m_invalidEntryDist			: 1;
				bool m_invalidFacingDir			: 1;
				bool m_notOnNav					: 1;
				bool m_noClearLineOfMotion		: 1;
				bool m_speedMismatch			: 1;
				bool m_wrongMotionType			: 1;
				bool m_wrongMotionSubCategory	: 1;
				bool m_wrongWeaponType			: 1;
			};
			U32 m_all = 0;
		} m_errorFlags;
	};

	enum class GatherMode
	{
		kDefault,
		kResolved,
		kDebug
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	// does all three steps for you
	U32F ResolveEntry(const CharacterState& charState,
					  const DC::ApEntryItemList* pSourceList,
					  const BoundFrame& defaultApRef,
					  AvailableEntry* pEntriesOut,
					  const U32F maxEntriesOut);

	U32F ResolveDefaultEntry(const CharacterState& charState,
							 const DC::ApEntryItemList* pSourceList,
							 const BoundFrame& defaultApRef,
							 AvailableEntry* pEntriesOut,
							 const U32F maxEntriesOut);

	// step 1
	U32F BuildInitialEntryList(const CharacterState& charState,
							   const DC::ApEntryItemList* pSourceList,
							   const GatherMode gatherMode,
							   const BoundFrame& defaultApRef,
							   AvailableEntry* pEntriesOut,
							   const U32F maxEntriesOut);

	// step 2
	void FlagInvalidEntries(const CharacterState& charState,
							const GatherMode gatherMode,
							const BoundFrame& apRef,
							AvailableEntry* pEntries,
							const U32F numEntries);

	// step 3
	U32F FilterInvalidEntries(AvailableEntry* pEntries, const U32F numEntries);

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool ConstructEntry(const CharacterState& charState,
						const DC::ApEntryItem* pEntryDef,
						const BoundFrame& defaultApRef,
						AvailableEntry* pEntryOut);

	Locator ConstructedRotatedAlignForEntryPs(const ApEntry::CharacterState& charState,
											  ArtItemAnimHandle anim,
											  const DC::ApEntryItem* pEntryDef,
											  const BoundFrame& defaultApRef,
											  float phaseMin,
											  float phaseMax,
											  bool mirror);

	bool ConstructDefaultEntryAlignPs(const CharacterState& charState,
									  const Locator alignToUsePs,
									  ArtItemAnimHandle anim,
									  const DC::ApEntryItem* pEntryDef,
									  const BoundFrame& defaultCoverApRef,
									  float phaseMin,
									  float phaseMax,
									  bool mirror,
									  Locator* pDefaultAlignPsOut,
									  float* pStartPhaseOut,
									  Vector* pEntryVelocityPsOut);

	bool ConstructRotatedEntryAndApRef(const CharacterState& charState, 
									   const Locator& alignToUsePs,
									   ArtItemAnimHandle anim,
									   const DC::ApEntryItem* pEntryDef,
									   const BoundFrame& defaultCoverApRef,
									   const Locator& defaultAlignPs,
									   Vector_arg defaultEntryVelocityPs,
									   Locator* pRotatedAlignPsOut,
									   BoundFrame* pRotatedApRefOut,
									   Vector* pRotatedEntryVecPsOut);

	bool ConstructEntryNavLocation(const CharacterState& charState,
								   const NavLocation& apNavLoc,
								   const AvailableEntry& entry,
								   NavLocation* pNavLocOut);

	float GetPhaseToMatchDistance(const CharacterState& charState,
								  ArtItemAnimHandle anim,
								  float distance,
								  const DC::ApEntryItem* pEntryDef,
								  float phaseMin,
								  float phaseMax);

	/// --------------------------------------------------------------------------------------------------------------- ///
	void FlagWrongDemeanorEntries(const CharacterState& charState, AvailableEntry* pEntries, U32F numEntries);
	void FlagWrongWeaponTypeEntries(const CharacterState& charState, AvailableEntry* pEntries, U32F numEntries);
	void FlagDistanceCategoryEntries(const CharacterState& charState,
									 bool useRangedEntries,
									 const BoundFrame& apRef,
									 AvailableEntry* pEntries,
									 U32F numEntries);
	void FlagInvalidMotionTypeEntries(const CharacterState& charState, AvailableEntry* pEntries, U32F numEntries);
	void FlagInvalidDistEntries(const CharacterState& charState,
								const BoundFrame& apRef,
								AvailableEntry* pEntries,
								U32F numEntries);
	void FlagInvalidAngleEntries(const CharacterState& charState, AvailableEntry* pEntries, U32F numEntries);
	void FlagNoClearLineOfMotionEntries(const CharacterState& charState,
										const BoundFrame& apRef,
										AvailableEntry* pEntries,
										U32F numEntries);

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool IsEntryAngleValid(const CharacterState& charState, const AvailableEntry& entry);
	bool IsFacingDirectionValidPs(const CharacterState& charState, const AvailableEntry& entry);
	bool IsEntryMotionClearOnNav(const CharacterState& charState,
								 const BoundFrame& finalApRef,
								 const AvailableEntry& entry,
								 bool debugDraw = false);
	bool IsEntrySpeedValid(const CharacterState& charState, const AvailableEntry& entry);
	bool IsEntryDistValid(const CharacterState& charState, const AvailableEntry& entry);
	StringId64 GetApPivotChannelId(const ArtItemAnim* pAnim, const DC::ApEntryItem* pEntryDef);

	/// --------------------------------------------------------------------------------------------------------------- ///
	U32F CountUsableEntries(const AvailableEntry* pEntries, const U32F numEntries);

	/// --------------------------------------------------------------------------------------------------------------- ///
	U32F EntryWithLeastRotation(const AvailableEntry* pEntries, const U32F numEntries, const BoundFrame& defaultApRef);
	U32F EntryWithLeastApproachRotation(const AvailableEntry* pEntries, const U32F numEntries, const Locator& alignPs);
	U32F ClosestEntry(const AvailableEntry* pEntries, const U32F numEntries, const Locator& alignPs);

	/// --------------------------------------------------------------------------------------------------------------- ///
	void DebugDrawEntryRange(const CharacterState& charState,
							 const BoundFrame& apRef,
							 const AvailableEntry& entry,
							 const Color clr);
} // namespace ApEntry

/// --------------------------------------------------------------------------------------------------------------- ///
U32F DebugDrawApEntryAnims(const ActionPack& ap,
						   const BoundFrame& apRef,
						   const ApEntry::CharacterState& cs,
						   const DC::ApEntryItemList& entryList,
						   const NavCharOptions::ApControllerOptions& debugOptions,
						   bool validOnly = false);
