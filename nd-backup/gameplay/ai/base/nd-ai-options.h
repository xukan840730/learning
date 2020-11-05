/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class GraphDisplay;
template<class T> class RingQueue;

namespace DMENU
{
	class Menu;
}

namespace DC
{
	struct MovePerformanceTable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// navigation (global)
struct NavOptions
{
	struct NavMeshPatchOptions
	{
		bool m_freezeNavMeshPatchProcessing = false;
		bool m_freezeNavMeshPatchInput		= false;
		bool m_serialNavMeshPatching		= false;
		bool m_debugPrintNavMeshPatchBlockers = false;

		float m_navMeshPatchSnapDist = 0.02f;
		bool m_navMeshPatchIslands	 = true;

		bool m_navMeshPatchIslandsDrawIslands = false;

		bool m_navMeshPatchIslandsDrawMerged = false;
		bool m_navMeshPatchIslandsDrawVerts	 = false;
		bool m_navMeshPatchIslandsDrawSegs	 = false;

		I32F m_navMeshPatchIslandIdx = -1;
		I32F m_navMeshPatchIslandsDrawIntersections = -1;
		I32F m_navMeshPatchIslandsDrawVertOwners	= -1;
		I32F m_navMeshPatchIslandsDrawSegOwners		= -1;
		I32F m_navMeshPatchIslandsNavPolyIdx		= -1;
		I32F m_navMeshPatchIslandsDrawTriangulation = -1;
		I32F m_navMeshPatchIslandsDrawPoly = -1;
	};

	NavMeshPatchOptions m_navMeshPatch;

	bool m_reportAnyNavMeshErrors	= false;
	bool m_printPathNodeStats		= false;
	bool m_drawNavPathNodes			= false;
	bool m_drawNavPathNodesStaticOnly	  = false;
	bool m_enableNavPathGraphValidation	  = false;
	bool m_validateNavPathGraphEveryFrame = false;
	bool m_dumpNavPathNodeMgrLogToTTY	  = false;
	bool m_printNavPathNodeMgrLogToMsgCon = false;
	bool m_disablePathNodeMgrErrors		  = false;
	bool m_neverHidePathNodeMgrErrors	  = false;
	bool m_useNewRadialPathBuild = true;
	bool m_debugFindClosestPointScript = false;

	bool m_disableNewHeuristicHack = true;
};
extern NavOptions g_navOptions;

extern U32 g_defaultLogChannels;

/// --------------------------------------------------------------------------------------------------------------- ///
// navigation (npc)
struct ALIGNED(16) NavCharOptions
{
	enum ApAnimDebugMode
	{
		kApAnimDebugModeNone,
		kApAnimDebugModeEnterAnimsChosen,
		kApAnimDebugModeEnterAnimsValid,
		kApAnimDebugModeEnterAnimsAll,
		kApAnimDebugModeExitAnimsValid,
		kApAnimDebugModeExitAnimsAll,
		kApAnimDebugModeExitAnimsChosen,
	};

	struct ApControllerOptions
	{
		ApAnimDebugMode m_debugMode = kApAnimDebugModeNone;
		I64 m_debugEnterIndex		= -1;
		I64 m_debugExitIndex		= -1;
		I32 m_motionTypeFilter		= kMotionTypeMax;
		StringId64 m_mtSubcategoryFilter = INVALID_STRING_ID_64;
		bool m_verboseAnimDebug = false;
		bool m_forceDefault = false;
	};

	bool m_forceInjured = false;
	float m_injuredHealthPercentage	  = 0.5f; // can be overridden by archetype
	bool m_showScriptControlIndicator = false;
	bool m_alwaysAllowHeal = false;
	float m_npcScale	   = 1.0f;
	bool m_debugNpcFalling = false;
	bool m_disableMovement = false;
	bool m_ragdollOnDeath  = true;
	bool m_invulnerableNpc = false;
	bool m_oneHitDeaths	   = false;
	float m_steerFailAngleDeg = 45.0f;

	// exposure map
	I32 m_displayExposureMap = -1;
	bool m_bExposureMapUseGPU  = true;
	bool m_bExposureMapEnabled = true;
	bool m_bExposureMapStealthOccluded = false;
	bool m_bExposureMapDrawDepthTest   = true;
	bool m_bExposureMapSelectedNpcOnly = false;

	// logging
	bool m_logMasterEnable = true;
	U32 m_logChannelMask = g_defaultLogChannels;

	I32 m_npcLogMsgConLines = 300;
	bool m_npcLogToMsgCon	= false;
	bool m_npcLogToTty		= false;
	bool m_npcScriptLogToMsgCon = false;
	bool m_npcScriptLogOverhead = false;

	bool m_displayLookAtPosition = false;
	bool m_displayFacePosition	 = false;
	bool m_displayDetailedLookAtPosition = false;
	bool m_displayAimAtPosition = false;
	bool m_displayPathDirection = false;
	bool m_displayAlignBlending = false;

	bool m_displayNavMesh = false;

	bool m_displayNavMeshAdjust	  = false;
	bool m_displayNavAdjustDist	  = false;
	bool m_displayNavDepenCapsule = false;
	bool m_displayAdjustHeadToNav = false;
	bool m_disableAdjustHeadToNav = false;

	bool m_debugObeyedPathBlockers = false;
	bool m_debugObeyedDepenBlockers = false;
	float m_obeyedNpcPathBlockerMaxDist = 15.0f;
	float m_obeyedNpcPathBlockerStandingDist = 3.0f;
	float m_obeyedNpcPathBlockerSameDirDeg = 25.0f;

	bool m_displayPath = false;
	bool m_displaySinglePathfindJobs   = false;
	bool m_displaySinglePathfindText   = false;
	float m_drawSinglePathfindTextSize = 0.5f;
	bool m_displayAvoidSpheres		   = false;
	bool m_debugPrintFindPathParams	   = false;
	bool m_enableChasePathFinds		   = true;
	bool m_disableSplineTailGoal	   = false;

	bool m_enablePathFailTesting = false;
	bool m_forceNoNavTaps		 = false;
	bool m_debugGroundAdjustment = false;
	bool m_displayGround		 = false;
	bool m_forceSwimMesh		 = false;

	float m_groundSmoothingStrength = 12.0f;
	bool m_disableSmoothGround = false;

	bool m_disableBackgroundNavJobs = false;

	bool m_displayCoverStatus = false;

	bool m_displayGoreDamageState = false;

	struct MovePerformanceEdit
	{
		ScriptPointer<const DC::MovePerformanceTable> m_pTable;
		I32 m_tableIndex	 = 0;
		float m_animDuration = 0.0f;

		U32 m_motionTypeMask = 0;

		DC::BlendParams m_blendIn;
		DC::SelfBlendParams m_selfBlend;

		F32 m_maxPivotErrDeg   = 0.0f;
		F32 m_maxExitErrDeg	   = 0.0f;
		F32 m_maxPathDeviation = 0.0f;
		F32 m_exitPhase		   = 0.0f;
		F32 m_minSpeedFactor   = 0.0f;

		bool m_singlePivotFitting = false;
		bool m_avoidRepetition	  = false;
	};

	struct MovePerformanceOptions
	{
		float m_minRemPathDistStopped = 1.5f;
		float m_minRemPathDistMoving = 0.5f;

		bool	m_disableMovePerformances = false;
		bool	m_debugDrawMovePerformances = false;
		bool    m_debugDrawMovePerformancePoi = false;
		I32     m_debugMovePerformanceIndex = -1;

		bool	m_debugMoveToMoves = false;
		I32		m_debugMoveToMoveIndex = -1;

		bool	m_debugEditMoveToMoves = false;
		MovePerformanceEdit m_debugEdit;
	};

	MovePerformanceOptions m_movePerformances;

	bool	m_displayScriptApEntryLoc = false;
	bool	m_displayPathfindManager = false;
	bool	m_displayCachePathfindRequests = false;

	struct LocomotionOptions
	{
		bool m_display		  = false;
		bool m_displayDetails = false;

		bool m_forceSuspendCommands = false;

		bool m_disableStairBlend	 = false;
		bool m_displayMmSamples		 = false;
		bool m_disableMmNavAvoidance = false;
		bool m_disableStairsMmLayer	 = false;

		float m_mmStoppedSpeed = 0.7f;
		float m_mmStoppedAngleSpeedDps = 90.0f;
		float m_stairsLayerCostBias	   = 1.1f;
		float m_groundNormalSpringK	   = 10.0f;
		float m_recklessStopKMult	   = 1.5f;

		bool m_debugDrawTargetPoseSkewing = false;
		bool m_disablePoseMatchSkewing	  = false;
		bool m_forceDesiredLoopPhase	  = false;
		bool m_disableSkewLowPassFilter	  = false;
		bool m_forceNonStrafeMovement	  = false;
	};
	LocomotionOptions	m_locomotionController;

	struct SwimOptions
	{
		bool m_display = false;
		bool m_displayDetails = false;
		bool m_displayStopDetails = false;
		bool m_useMmSwimming = true;
		float m_mmStoppedSpeed = 0.7f;
	};
	SwimOptions m_swimController;

	struct ClimbOptions
	{
		bool m_display = false;
		bool m_displayDetails = false;
		bool m_searchForShimmyLedge = true;
	};
	ClimbOptions	m_climb;

	struct VehicleOptions
	{
		bool m_display = false;
		bool m_displayDetails = false;
	};
	VehicleOptions	m_vehicle;

	bool m_displayWeaponController		 = false;
	bool m_displayPowerRagdollController = false;
	bool m_displayNavState = false;
	bool m_displayNavStateSourceInfo = false;

	bool m_forceOnStairs = false;
	bool m_displayNavigationHandoffs = false;
	bool m_displayInterCharDepen	 = false;
	bool m_displayWetness = false;

	bool m_forcePathSuspension = false;

	bool m_disableNavJobScheduling = false;
	bool m_drawNavJobQueue		   = false;

	bool m_dropMoveToBreadCrumbs = false;

	enum TapDebugMode
	{
		kTapDebugModeNone,
		kTapDebugModeEnterAnims,
		kTapDebugModeEnterAnimsVerbose,
		kTapDebugModeEnterAnimsAll,
		kTapDebugModeEnterAnimsAllVerbose,
		kTapDebugModeLoopAnims,
		kTapDebugModeExitAnims,
		kTapDebugModeExitAnimsVerbose,
		kTapDebugModeExitAnimsAll,
		kTapDebugModeExitAnimsAllVerbose,
		kTapDebugModeAnimPoints,
		kTapDebugModeAbort
	};

	struct TraversalOptions : public ApControllerOptions
	{
		TapDebugMode m_tapDebugMode = kTapDebugModeNone;

		bool m_display = false;
		bool m_debugLandApAdjustement = false;
		bool m_displaySkills	  = false;
		bool m_checkBlendOverruns = false;
		bool m_debugVarTapRating  = false;
		bool m_disableCachedUsageInfo = false;
		bool m_debugTapDistortions	  = false;
		bool m_disableTapConstraints  = false;
		bool m_disableAbortingTaps	  = false;
		bool m_allowBadGeometryTaps	  = false;
		bool m_debugOnGround		  = false;

		float m_reservedExtraCost	  = 3.0f;
		float m_tapDistScaleEffectStr = 0.5f;
		float m_vaultCostScale		  = 3.0f;
		float m_alignIntersectSpeedModPcnt = 1.0f;

		bool m_disableEnterSelfBlend = false;
		bool m_disableExitSelfBlend	 = false;
		bool m_disableUnboundedReservations = false;

		bool m_disableFixTapsToCollision = false;
		bool m_disableApAnimAdjust = false;
		bool m_useOldConstraintGroundAdj = false;

		bool m_disableLadderIk = false;
		bool m_debugLadderIk = false;

		bool m_overrideEnterSelfBlend = false;
		DC::SelfBlendParams m_enterSbOverride;
	};
	TraversalOptions m_traversal;

	struct ActionPackOptions
	{
		bool m_validateApEntryEffs = false;
		bool m_validateApExitEffs  = false;
		bool m_validateApSoundEffs = false;
	};
	ActionPackOptions m_actionPack;

	StringId64 m_velocityProfileOwner = INVALID_STRING_ID_64;
	GraphDisplay* m_pVelocityProfile  = nullptr;
	RingQueue<float>* m_pVelocityProfileSamples = nullptr;

	StringId64 m_debugMtSubcategory = INVALID_STRING_ID_64;

	U32 m_overlayInstanceSeed = 0;
};
extern NavCharOptions g_navCharOptions;

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(16) NdAiOptions
{
	I32		m_numActionPacksLoggedInPerFrame = 10;
	bool	m_aggressiveApMutexValidation = false;
	bool	m_enableMsgAp = false;

	bool	m_drawCoverApHeight = false;
	bool	m_drawQuestionableCoverAPs = false;
	bool	m_disableProps = false;
	bool	m_enableMouseSelect = false;

	bool	m_drawNpcVoxels = false;
	bool	m_drawNpcVoxelRaycasts = false;
	bool	m_displayDialogLookDebug = false;

	bool	m_drawLogicControl = false;
	bool	m_drawLogicControlLog = false;

	bool	m_debugHeartRateList = false;
	bool	m_debugHeartRateGraph = false;
	bool	m_debugHeartRateWs = false;
	bool	m_debugBreathAnims = false;

	bool	m_enableBreathStartAnimOverrides = true;

	struct Ladders
	{
		bool m_debugDraw = false;
	};
	Ladders m_ladders;

	struct EntryApOptions : public NavCharOptions::ApControllerOptions
	{
	};
	EntryApOptions m_entry;
};
extern NdAiOptions g_ndAiOptions;

/// --------------------------------------------------------------------------------------------------------------- ///
void CreateApAnimDebugMenu(DMENU::Menu* pMenu, const char* apTypeStr, NavCharOptions::ApControllerOptions& options);
