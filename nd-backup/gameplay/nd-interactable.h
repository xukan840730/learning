/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/system/read-write-atomic-lock.h"
#include "corelib/util/timeframe.h"

#include "ndlib/script/script-manager.h"
#include "ndlib/util/maybe.h"

#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/region/region.h"
#include "gamelib/scriptx/h/interactable-defines.h"

class EntityDB;
class NdGameObject;
class ProcessSpawnInfo;

struct PathfindRequestHandle;

namespace DC
{
	struct GameInteractableDef;
	struct Gui2AttachableParams;
	typedef I32 InteractableStyle;
	typedef I32 TriState;
}

namespace Hud2
{
	struct InteractableRequest;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// InteractCost: make it easy to tweak cost
/// --------------------------------------------------------------------------------------------------------------- ///
struct InteractCost
{
public:
	InteractCost()
		: m_rawCost(kLargestFloat)
		, m_weight(1.f)
	{}

	InteractCost(float _rawCost, float _weight)
		: m_rawCost(_rawCost)
		, m_weight(_weight)
	{}

	float GetCost() const { return m_rawCost * m_weight; }

	float m_rawCost;
	float m_weight;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// ActivationThresholds: define thresholds for activation of an interactable
/// --------------------------------------------------------------------------------------------------------------- ///
struct ActivationThresholds
{
public:
	ActivationThresholds()
		: m_yMin(-1.f)
		, m_yMax(-1.f)
		, m_radius(-1.f)
		, m_useRegion(false)
	{}

	F32 m_yMin;
	F32 m_yMax;
	F32 m_radius;
	RegionHandle m_hRegion;
	bool m_useRegion;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// InteractAvailability: available state of an interactable
/// --------------------------------------------------------------------------------------------------------------- ///
enum InteractAvailability
{
	kInteractableInvalid = -1,
	kInteractableUnavailable,	// object is normally interactable, but is currently unavailable
	kInteractableAvailable		// object is available to be picked up or interacted with
};

/// --------------------------------------------------------------------------------------------------------------- ///
// InteractAgilityMask: abilities required to access interactables,
// as determined by the interactables manager heuristic analysis
/// --------------------------------------------------------------------------------------------------------------- ///
enum class InteractAgilityMask : U32
{
	kNone      = 0,
	kProne     = 1,
	kBreakable = 2,
	kOverride  = 4,
};

/// --------------------------------------------------------------------------------------------------------------- ///
// Arguments to OnInteract()
/// --------------------------------------------------------------------------------------------------------------- ///
struct NdInteractArgs
{
	float m_startFrame = 0.0f;
	StringId64 m_animId = INVALID_STRING_ID_64;
	bool m_disallowEquip = false;
	bool m_isAccessibilityAutoInteract = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// NdInteractControl: General interactable controller.
/// --------------------------------------------------------------------------------------------------------------- ///

class NdInteractControl
{

/// --------------------------------------------------------------------------------------------------------------- ///
// Object Customization Interface
// Interactable game object types can provide custom implementations of these functions.

protected:
	enum AttrType
	{
		kAttrTypeFloat,
		kAttrTypeI32,
		kAttrTypeStringId,
		kAttrTypeBoolean
	};
	struct Attr
	{
		StringId64	m_key;
		AttrType	m_type;
		U32			m_offset;
	};

	DC::InteractableDef* SetCustomAttribes(const Attr* aAttr,
										   const I32 numAttr,
										   const EntityDB* pEntDb,
										   const DC::InteractableDef* pOldDef);

	virtual bool ForceAllocateCustomDef() const { return false; }

	virtual void UpdateInternal();
	virtual void OnInteract(MutableNdGameObjectHandle hUser,
							const NdInteractArgs* pOptionalArgs = nullptr,
							bool* pEquipped = nullptr);

public:
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	virtual void Update();

	virtual InteractAvailability DetermineAvailability(NdGameObjectHandle hUser) const;

	virtual void RequestInteract(MutableNdGameObjectHandle hUser,
								 bool interactButtonHeld,
								 bool interactButtonClicked,
								 bool isPickupAll = false,
								 bool isAccessibilityAutoInteract = false);
	void Interact(MutableNdGameObjectHandle hUser,
				  const NdInteractArgs* pOptionalArgs = nullptr,
				  bool* pEquipped = nullptr);

	virtual void EnableNavBlockerWhenClear(const NdGameObject* pGo) {}

	virtual int GetGrabPoints(Vector grabPointsLs[], int grabPointsCapacity, const Locator& playerLoc);

	void GetActivationThresholds(ActivationThresholds* pThresholds, NdGameObjectHandle hUser) const;
	void GetInteractionThresholds(ActivationThresholds* pThresholds, NdGameObjectHandle hUser) const;

	// if no user, please implement a sensible default using userWs instead if it's valid, used for
	// speculative queries.
	Point GetInteractionPos(NdGameObjectHandle hUser, const Point userWs = kInvalidPoint) const;

	virtual Quat GetRotationForInteract() const;
	virtual InteractCost GetBaseCost(NdGameObjectHandle hUser) const;
	virtual bool GetEntryLocation(NdGameObjectHandle hUser, BoundFrame& entry, StringId64* outAnimId = nullptr);
	virtual bool GetApReference(NdGameObjectHandle hUser, BoundFrame& apRef);
	// returns the camera look-at point for optimal selection of the interactable
	virtual Point GetSelectionTargetWs(NdGameObjectHandle hUser = NdGameObjectHandle(), const Point userWs = kInvalidPoint, bool debugDraw = false) const;

	// returns a stable point for optimal queries of good places to stand to interact with this object
	virtual Point GetInteractionQueryTargetWs(NdGameObjectHandle hUser, const Point userWs = kInvalidPoint, bool debugDraw = false) const
	{
		return GetSelectionTargetWs(hUser, userWs, debugDraw);
	};

	float GetProbeOffsetYWs() const;
	float GetProbeRadius() const;
	I32 GetNumAroundCorner() const;
	float GetAroundCornerStep() const;
	bool NoSecondaryDisplay() const;

	virtual void EndInteraction();

	virtual void PopulateHud2Request(Hud2::InteractableRequest& request, NdGameObjectHandle hUser) const;
	// virtual float GetHudIconTargetScale(NdGameObjectHandle hUser);
	virtual I32 GetHudAmmoCount();
	virtual StringId64 GetInteractCommandId() const;

	// if no user, please implement a sensible default using userWs instead if it's valid, used for
	// speculative queries.
	typedef Locator SelectionLocFunc(NdGameObjectHandle hInteractable, NdGameObjectHandle hUser, const Point userWs);

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Game Customization Interface
	// Each game must implement these appropriately.

public:

	struct InitParams
	{
		InitParams()
			: m_defaultDefId(INVALID_STRING_ID_64)
			, m_useDefaultDefId(false)
			, m_dontAllocateShaderParams(false)
			, m_disallowAlloc(false)
		{}

		InitParams(StringId64 defaultDefId)
			: m_defaultDefId(defaultDefId)
			, m_useDefaultDefId(false)
			, m_dontAllocateShaderParams(false)
			, m_disallowAlloc(false)
		{}

		StringId64 m_defaultDefId;
		bool m_useDefaultDefId;
		bool m_dontAllocateShaderParams;
		bool m_disallowAlloc;
	};

	virtual Err Init(NdGameObject* pSelf, const ProcessSpawnInfo& spawn, const InitParams& params);

	struct DrawInteractParams
	{
		DrawInteractParams()
		{
			m_isPreSelectedOnly = false;
			m_avail = kInteractableInvalid;
			m_isPlayerBlocked = false;
			m_isBestSelection = false;
			m_interactCmdId = INVALID_STRING_ID_64;
			m_disableInteractableUpdate = false;
		}

		DrawInteractParams(bool isPreSelectionOnly, InteractAvailability avail, bool isPlayerBlocked, bool isBestSelection, bool disableInteractableUpdate = false)
			: m_isPreSelectedOnly(isPreSelectionOnly)
			, m_avail(avail)
			, m_isPlayerBlocked(isPlayerBlocked)
			, m_isBestSelection(isBestSelection)
			, m_interactCmdId(INVALID_STRING_ID_64)
			, m_disableInteractableUpdate(disableInteractableUpdate)
		{}

		bool m_isPreSelectedOnly;
		InteractAvailability m_avail;
		bool m_isPlayerBlocked;
		bool m_isBestSelection;
		StringId64 m_interactCmdId;
		bool m_disableInteractableUpdate;	// Disable updates to icon linked to interaction? (eg. progress bar on hold)
	};

	// call every frame to notify the object that it is selected for player interaction
	virtual void DrawInteractHudIcon(MutableNdGameObjectHandle hUser, const DrawInteractParams& params);

	virtual BoundFrame GetIconBoundFrame(NdGameObjectHandle hUser, const Point userWs) const;

	// returns the cost rating used to determine which interactable will become selected
	virtual InteractCost GetBaseCostInternal(NdGameObjectHandle hUser) const;

	virtual DC::InteractableDef* AllocateInteractableDef() const;
	virtual void CopyInteractableDef(DC::InteractableDef* pDst, const DC::InteractableDef* pSrc) const;

	// notify that we just got interacted with
	virtual void NotifyInteracted(NdGameObjectHandle hUser);

	// enable/disable trigger-only mode
	void SetTriggerOnly(bool triggerOnly) { m_triggerOnly = triggerOnly; }
	virtual bool IsTriggerOnly() const;

	// offscreen
	virtual bool AllowOffscreen() const;

	virtual float GetDifficultyShimmerIntensity() const { return 1.f; }

/// --------------------------------------------------------------------------------------------------------------- ///
// Fixed Interface

public:
	NdInteractControl();
	virtual ~NdInteractControl();

	// System init and shutdown
	static void Init();
	static void ShutDown();

	// Management of active interactables (global list)
	static void ActivateInteractable(NdGameObject* pInteractable);
	static void ClearOldFrame();
	static I32F GetActiveInteractables(NdGameObjectHandle hUser, I32F capacity, NdGameObject* apInteractable[]);

	// Other static funcs
	static const char* GetAvailabilityAsString(InteractAvailability avail);
	static bool IsWithinLookAngle(const Locator& cameraLoc, Point_arg targetPosWs, float angleDeg);
	static bool IsWithinLookAngle(const Locator& cameraLoc, Point_arg targetPosWs, float angleHoriDeg, float angleVertDeg);
	static float GetLookAngleDiff(const Locator& cameraLoc, Point_arg targetPosWs);
	//static void AdjustApRefForTiltedFloor(BoundFrame& untiltedApRef, const BoundFrame& originalApRef, NdGameObject *pInteractable = nullptr, NdGameObjectHandle hUser = NdGameObjectHandle());
	static const DC::InteractableDef* LookupDef(StringId64 defId);
	static const DC::GameInteractableDef* LookupGameDef(StringId64 defId);

	bool IsInitialized() const { return m_initialized; }

	// DC interactable definition
	StringId64 GetDefId() const;
	const DC::InteractableDef* GetDef() const;

	virtual void OverrideInteractableDef(StringId64 interactDef);
	void UseInteractableDefFromOther(NdGameObjectHandle hSrcObj) { m_hSrcDefObj = hSrcObj; }
	NdGameObjectHandle GetSrcDefObj() const { return m_hSrcDefObj; }

	InteractAvailability GetAvailability(NdGameObjectHandle hUser, bool useInteractablesMgrHack = false) const;

	bool IsSelected() const;
	bool IsPreSelected() const;

	bool IsInActivationRange(NdGameObjectHandle hUser, Point_arg userPos, const Point* pPredictPos = nullptr) const;
	virtual bool IsInInteractionRange(NdGameObjectHandle hUser, Point_arg userPos, const Point* pPredictPos = nullptr) const;

	bool IsHighPriority() const;

	bool IgnoresReachableProbes() const { return m_ignoreProbes; }
	void SetIgnoreProbes(bool f) { m_ignoreProbes = f; }

	bool ProbesFromCameraLocation() const;
	bool IsAutoInteract() const;

	bool IsOffscreen(NdGameObjectHandle hUser) const;

	// IsInNavAssistRegion: returnt true if player is inside navigation-assist-region
	bool IsInNavAssistRegion(NdGameObjectHandle hUser) const;

	void SetIsReachable(bool isReachable) { m_isReachable = isReachable; }
	bool IsReachable() const { return m_isReachable; }
	virtual bool IsPickup() const { return false; }
	virtual bool IsSystemicPickupDisabled() const { return false; }

	// enable/disable interaction (e.g. disable interaction once a drawer has been opened or an item has been picked up)
	void SetInteractionEnabled(bool enable, bool interactablesMgrIgnoreDisableHack = false);
	bool IsInteractionEnabled() const;
	void DisableForOneFrame();
	void DisableInteractionForOneFrame();

	void SetUnavailableFromScript(bool unavailable);
	bool IsUnavailableFromScript() const { return m_unavailableScript; }

	// enable/disable single-use mode
	void SetInteractionSingleUse(bool singleUse) { m_interactionSingleUse = singleUse; }
	bool IsInteractionSingleUse() const { return m_interactionSingleUse; }

	// enable/disable partner mode
	void SetPartnerEnabled(bool partnerEnabled) { m_partnerEnabled = partnerEnabled; }
	bool IsPartnerEnabled() const { return m_partnerEnabled; }

	void SetObjectHasBeenInteracted(bool value) { m_hasBeenInteracted = value; }
	bool HasObjectBeenInteracted() const { return m_hasBeenInteracted; }

	bool UseInteractablesMgrIgnoreDisableHack() const { return m_hackInteractablesMgrIgnoreDisabledHack; }

	// PSEUDO-HACKS: not all interactables use these...

	//bool NeedProbeStartYOffsetWhenLookUp() const;
	//float GetProbeStartYOffsetWhenLookUp() const;

	bool HideUnavailable() const;

	const DC::ScriptLambda& GetInteractOverrideLambda() const { return m_interactOverrideLambda; }
	void SetInteractOverrideLambda(const DC::ScriptLambda &lambda) { m_interactOverrideLambda = lambda; }

	bool HasCustomDef() const { return m_pCustomDef != nullptr; }
	void SetCustomDefActivationRadius(float radius);
	void SetCustomDefInteractRadius(float radius);
	void SetCustomDefActivationHeight(float yMin, float yMax);
	void SetCustomDefInteractionHeight(float yMin, float yMax);

	void SetSelectJointIndex(I32 jointIndex) { m_selectUsingJointIndex = jointIndex; }

	StringId64 GetActivationRegionNameId() const;
	StringId64 GetInteractionRegionNameId() const;

	void SetActivationRegionName(StringId64 name) { m_activationRegionNameId = name; }
	void SetInteractionRegionName(StringId64 name) { m_interactionRegionNameId = name; }

	StringId64 GetShowupHintSound() const;
	bool IsHintSoundPlayed() const { return m_hintSoundPlayed; }
	void SetHintSoundPlayed(bool value) { m_hintSoundPlayed = value; }

	void SetCustomSelectionLocFunc(SelectionLocFunc* func) { m_customSelectionLocFunc = func; }

	virtual bool IsPickupController() const { return false; }

	virtual bool NoShimmer() const { return !IsInteractionEnabled(); }

	// Interactables management and analysis
	int GetInteractNavLocations(NavLocation* pOutNavLocs, int maxNavLocs) const;
	int GetInteractNavLocationAgilities(InteractAgilityMask* pOutAgilities, int maxAgilities) const;
	float GetMinPathDistanceToInteractable(const PathfindRequestHandle& hRequest) const;
	float GetMinLinearDistanceToInteractable(Point posWs, float yScale, InteractAgilityMask* const pAgilityMask = nullptr) const;
	NavLocation GetClosestNavLocationToInteractableByPathDistance(const PathfindRequestHandle& hRequest, float* const pPathDist = nullptr) const;
	NavLocation GetClosestNavLocationToInteractableByLinearDistance(Point posWs, float yScale, float* const pLinearDist = nullptr) const;

	///-------------------------------------------------------------------///
	/// Accessibility
	///-------------------------------------------------------------------///
	bool IsAutoPickupDisabled() const					{ return m_autoPickupDisabled; }
	void SetAutoPickupDisabled(bool autoPickupDisabled) { m_autoPickupDisabled = autoPickupDisabled; }

	bool IsAccessibilityAutoPickupAllowed() const;
	bool AreAccessibilitySoundCuesAllowed() const;

	StringId64 GetHCMMode() const;
	StringId64 GetHCMModeDisabled() const;

	bool EvaluateForInteractablesMgrEvenWhenDisabled() const;

	void UpdateHighContrastModeTarget();

	virtual bool ShouldPropagateHighContrastModeToOwner() const;
	DC::HCMModeType GetCurrentHighContrastModeType() const;
	Maybe<DC::HCMModeType> GetHighContrastModeTypeOverride() const { return m_hcmModeOverride; }
	U8 GetNumHighContrastModeTargets() const { return m_numHcmModeTargets; }
	MutableNdGameObjectHandle GetHighContrastModeTargetObject(U8 index) const
	{
		GAMEPLAY_ASSERT(index < m_numHcmModeTargets);
		return m_aHcmModeTargetObj[index];
	}
	StringId64 GetHighContrastModeTargetObjectId(U8 index) const
	{
		GAMEPLAY_ASSERT(index < m_numHcmModeTargets);
		return m_aHcmModeTargetObjId[index];
	}
	bool IsHighContrastModeTargetObjectValid(U8 index) const
	{
		GAMEPLAY_ASSERT(index < m_numHcmModeTargets);
		return m_aHcmModeTargetObj[index].HandleValid();
	}
	bool HasMissingHighContrastModeTargets() const;
	void UpdateHighContrastModeType();
	void UpdateHighContrastMode();

	bool AddHighContrastModeTarget(StringId64 userId, NdGameObject* pGo); // pGo may be null, in which case we will assume it has yet to spawn
	void RemoveHighContrastModeTarget(StringId64 userId, Maybe<DC::HCMModeType> newHcmMode);
	void RemoveAllHighContrastModeTargets(Maybe<DC::HCMModeType> newHCMMode);


	void SetAccessibilityCuesDisabled() { m_accessibilityDisabled = true; }
	void RestoreAccessibilityCues() { m_accessibilityDisabled = false; }

protected:

	void CacheDefPtr();
	bool AreAccessibilityCuesDisabled() const { return m_accessibilityDisabled; }

	MutableNdGameObjectHandle m_hSelf;

	StringId64 m_defId;
	DC::InteractableDef* m_pCustomDef;
	NdGameObjectHandle m_hSrcDefObj; // if valid, get InteractableDef from source-obj.

	//F32 m_highlightAlpha;
	I32 m_selectUsingJointIndex;
	StringId64 m_selectionTargetSpawner;

	I64 m_selectedFrameNumber;
	I64 m_preSelectedFrameNumber;
	TimeFrame m_unavailableSeenTime;

	bool m_initialized		: 1;
	bool m_wasSelected		: 1;
	bool m_disabled			: 1;
	bool m_unavailableScript : 1;
	bool m_triggerOnly		: 1;
	bool m_interactionSingleUse : 1;
	bool m_partnerEnabled	: 1;
	bool m_wasPreSelected	: 1;
	bool m_iconPermanent	: 1;
	bool m_hasBeenInteracted : 1;
	bool m_hintSoundPlayed	: 1;
	bool m_ignoreProbes : 1;
	bool m_isReachable : 1;
	bool m_accessibilityDisabled : 1;
	bool m_autoPickupDisabled : 1;
	bool m_hackInteractablesMgrIgnoreDisabledHack : 1;

	// PSEUDO-HACKS: not all interactables use these...

	float m_probeOffsetYWs = 0.f;

	//float GetCameraAngleCost(U32F screenIndex, Point_arg camPosWs, Vector_arg camDirWs);

	SelectionLocFunc* m_customSelectionLocFunc;

	TimeFrame m_lastInteractedTime;

	//TimeFrame m_lastHighlightUpdateTime;
	float m_interactHighlightBrightness;
	float m_selectHighlightBrightness;

	StringId64 m_activationRegionNameId = INVALID_STRING_ID_64;
	StringId64 m_interactionRegionNameId = INVALID_STRING_ID_64;
	StringId64 m_navAssistRegionNameId = INVALID_STRING_ID_64;

	DC::ScriptLambda m_interactOverrideLambda;

	I64 m_disabledFN = -1;
	I64 m_disableInteractionFN = -1;

	I64 m_lastFrameUpdated = -1;
	I32 m_interactRequestCount = 0;

	Maybe<DC::HCMModeType> m_hcmModeOverride;

	static CONST_EXPR U8 kMaxHcmTargetObjects = 4;
	int m_numHcmModeTargets;
	StringId64 m_aHcmModeTargetObjId[kMaxHcmTargetObjects];				 // userID of hcmModeTargetObj
	MutableNdGameObjectHandle m_aHcmModeTargetObj[kMaxHcmTargetObjects]; // will set high contrast mode tag of target to interactable when appropriate

	ScriptPointer<DC::InteractableDef>  m_pInteractableDef;

	// the NdInteractablesManager slowly updates interactables over time
	// with information about where one might stand, on navmesh,
	// to be able to interact with them, for use by (at least)
	// buddies and player accessibility queries/navigation
	static CONST_EXPR int kMaxInteractNavLocations = 13;
	I64 m_interactNavLocationsLastUpdateFrame;
	int m_numInteractNavLocations;
	NavLocation m_interactNavLocations[kMaxInteractNavLocations];
	InteractAgilityMask m_interactAgilities[kMaxInteractNavLocations];
	mutable NdRwAtomicLock64 m_interactNavLocationsLock;
	friend class NdInteractablesManager;
};

// ====================================================================================================================
// Information about a request to display an interactable icon in the world
// ====================================================================================================================

namespace Hud2
{

enum class InteractIconType
{
	kInvalid = -1,
	kShortGun,
	kLongGun,
	kSingleItem,
	kResource,
	kMeleeLarge,
	kMeleeSmall
};

struct InteractableRequest
{
public:

	const char*								m_widgetClass;				// Type of widget to display (rel path to .widget file)

	const char*								m_iconItem;					// Icon of the weapon or other item (for pickups)
	const char*								m_iconButton;				// Button icon to display in HUD
	const char*								m_iconInteract;				// Interact icon to display in HUD
	const char*								m_iconAlt;					// Optional secondary icon to display in HUD
	const char*								m_ammoIcon;					// Optional ammo icon for a weapon pickup.
	const char*								m_ammoWeaponIcon;			// Optional weapon icon for ammo pickup.
	InteractIconType						m_whichIconAndText;

	NdGameObjectHandle						m_hInteractable;
	StringId64								m_jointId;					// Optional joint for attaching to object

	StringId64								m_interactableDefId;		// Id of the DC def (e.g. DC::InteractableDef)
	StringId64								m_abstractInteractId;		// Id of the DC def (e.g. DC::InteractableDef)
	NdInteractControl::DrawInteractParams	m_params;

	const DC::Gui2AttachableParams*			m_pAttachParams;

	StringId64								m_itemId;
	I32										m_itemAmount;
	I32										m_inventoryItemAmount;
	bool									m_showInventoryItemAmount;

	DC::InteractableSelection				m_selectionRange;
	bool									m_showItemAmount;
	bool									m_alphaWhenOccluded;
	bool									m_alphaByDistance;
	bool									m_showWhenOffscreen;
	bool									m_countsAsPickupPrompt;
	bool									m_countsAsInteractPrompt;
	bool									m_interacted;
	Maybe<BoundFrame>						m_boundFrame;
	Vector									m_offsetWs;
	StringId64								m_tameName;					// tame-name from weapon-art-def.
	StringId64								m_descCipherId;				// description cipher-id from interactable.dcx
	StringId64								m_unavailableCipherId;		// unavailable cipher-id from interactable.dcx
	bool									m_showText3;				// optional text for certain widgets. (usually it's HOLD)

	bool									m_activationRegionUsed;			// Tracks whether an activation region has been specified
	bool									m_disableActivationCircle;		// Decides whether we should show the activation circle for this hud element
	DC::Tristate							m_activationCircleVisibilityOverride; // Override whether we show / hide the activation circle

	bool									m_activeAccessibilityCueOffscreen;	// Use active accessibility sound cues when an interactable is offscreen
	F32										m_activeAccessibilityCueYDistance;	// Vertical distance from player root to interactable for sound cue height variations

	InteractableRequest() { Clear(); }

	void Clear()
	{
		m_pAttachParams = nullptr;
		m_iconItem = nullptr;
		m_iconButton = nullptr;
		m_iconInteract = nullptr;
		m_iconAlt = nullptr;
		m_ammoIcon = nullptr;
		m_ammoWeaponIcon = nullptr;
		m_whichIconAndText = InteractIconType::kShortGun;
		m_widgetClass = nullptr;
		m_hInteractable = nullptr;
		m_itemId = INVALID_STRING_ID_64;
		m_inventoryItemAmount = 0;
		m_showInventoryItemAmount = false;
		m_interactableDefId = INVALID_STRING_ID_64;
		m_abstractInteractId = INVALID_STRING_ID_64;
		m_jointId = INVALID_STRING_ID_64;
		m_showItemAmount = false;
		m_showWhenOffscreen = false;
		m_countsAsPickupPrompt = false;
		m_countsAsInteractPrompt = false;
		m_selectionRange = DC::kInteractableSelectionOutOfRange;
		m_alphaWhenOccluded = false;
		m_alphaByDistance = false;
		m_interacted = false;
		m_offsetWs = kZero;
		m_tameName = INVALID_STRING_ID_64;
		m_descCipherId = INVALID_STRING_ID_64;
		m_unavailableCipherId = INVALID_STRING_ID_64;
		m_showText3 = false;
		m_activationRegionUsed = false;
		m_disableActivationCircle = false;
		m_activeAccessibilityCueOffscreen = false;
		m_activeAccessibilityCueYDistance = 0.f;
		m_activationCircleVisibilityOverride = 0x2; // This equates to undefined in the DC::Tristate
	}
};

} // namespace Hud2

/// --------------------------------------------------------------------------------------------------------------- ///
// InteractableOptions
/// --------------------------------------------------------------------------------------------------------------- ///

struct InteractableOptions
{
	InteractableOptions()
	{
		memset(this, 0, sizeof(*this));
		m_crouchUseStandOffset = true;
	}

	bool	m_drawInteractablesMgr;
	bool	m_drawInteractablesMgrNav;
	bool	m_drawInteractablesMgrCasts;
	bool	m_drawInteractablesMgrUnreachablesOnly;
	bool	m_drawInteractablesMgrQueries;
	bool	m_drawPickupProbes;
	bool	m_drawPlayerBlockedProbes;
	bool	m_drawNpcBlockedProbes;
	bool	m_drawActivationRegion;
	bool	m_drawInteractionRegion;
	bool	m_drawApRef;
	bool	m_drawEntry;
	bool	m_drawCost;
	bool	m_drawSelectionTarget;
	bool	m_drawDefId;
	bool	m_printStats;
	bool	m_allowInteractionWithUnavailables;
	bool	m_debugPlayerPickups;

	bool	m_debugDrawItemId;

	bool	m_showPickupRadius;
	bool	m_disableInteractableSystem;
	bool	m_disableDrawingIcon;
	bool	m_disableInteraction;
	bool	m_allInteractablesUnavailable;
	bool	m_suppressSecondarys;

	bool	m_showSearchLoc;
	bool	m_showCameraRemapRegion;

	bool	m_crouchUseStandOffset;
	bool	m_debugPickupAll;

	inline bool DebugPlayerPickups() const { return FALSE_IN_FINAL_BUILD(m_debugPlayerPickups); }
};

extern InteractableOptions g_interactableOptions;
const char* InteractableSelectionToString(DC::InteractableSelection e);
