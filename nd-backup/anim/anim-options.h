/*
 * Copyright (c) 2012 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

/// --------------------------------------------------------------------------------------------------------------- ///
class Clock;

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(16) AnimOptions
{
	AnimOptions() {}

	void InitDefaults();

	bool			m_disableMultiActorLoginCheck;
	bool			m_displayFloatChannels;
	bool			m_renderWrinkleWeights;
	bool			m_flipToZyx;
	float			m_displacementScale;
	bool			m_disableWrinkleMaps;
	bool			m_disableWrinkleOnly;
	bool			m_disableDisplacementOnly;
	bool			m_disableAoAnimationOnly;
	bool			m_disableWrinkleMapsAndBlendShapes;
	bool			m_enableShapeStreaming;
	I32			    m_shapeStreamFrame;
	bool			m_showCameraAdditives;
	bool			m_disableCameraAdditives;
	bool			m_displayBlendShapeAnims;
	bool			m_displayMotionBlurJoints;
	bool			m_offsetMotionBlurJoints;
	float			m_offsetMotionBlurX;
	float			m_offsetMotionBlurY;
	float			m_offsetMotionBlurZ;
	bool			m_debugReadChannelScaleZ;

	bool			m_transformJointsToLocsOnPpu;
	bool			m_disableAnimation;
	bool			m_enableFakePause;
	bool			m_forceLoopAnim;
	bool			m_fakePaused;
	bool			m_forceBindPose;
	bool			m_forcePostRetargeting;
	bool			m_limitPostRetargetingToFirstSegment;
	bool			m_disableLayerFades;
	bool			m_disableInstanceFades;
	bool			m_enableBrokenInstanceFeatherBlends;
	bool			m_disableAdditiveLayers;
	bool			m_disableAdditiveAnims;
	bool			m_disableNonBaseLayers;
	I32				m_disableLayerIndex;
	bool			m_dumpArtItemInfoOnLogin;
	U32				m_dumpArtItemInfoOnLoginSortMethod;
	bool			m_dumpKeyFramedJointsPerAnim;
	bool			m_printNonAdditive30HzAnimsOnLogin;
	bool			m_printCommandList;
	bool			m_printCommandListToMsgCon;
	bool			m_printJointParams;
	I32				m_printJointParamsSegment;
	bool			m_printSegmentProcessing;
	bool			m_printPersistentDataStats;
	float			m_bakedScaleValue;
	bool			m_displayPerformanceStats;
	float			m_masterClockScale;
	bool			m_validateJointCache;
	bool			m_checkForTransitionalLoopingAnims;
	bool			m_overrideBlendShapeValues;
	bool			m_verboseAnimDebug;
	float			m_blendShapeVal[5];
	bool			m_validateSingleInstanceAnimations;
	bool			m_validateLoopingAnims;
	bool			m_validateStateConversions;
	bool			m_validatePropAttachment;
	bool			m_validateEffJoints;
	bool			m_validateEffFrames;
	bool			m_validateEffToMsgCon;
	bool			m_validateJointOrdering;
	bool			m_validateStepAnimations;
	bool			m_warnOnMissingAnimations;
	bool			m_enableAnimDataLocking;
	bool			m_disableCameraCuts;
	bool			m_onlyCutCamera1;
	bool			m_disableFrameClampOnCameraCut;
	bool			m_postPlayStats;
	bool			m_disableAudioSync;
	bool			m_showAudioSyncGraph;
	bool			m_enableJointLimits;
	bool			m_useLightObjects;
	bool			m_enableCompiledRigExecution;

	bool			m_logRetargetedAnimations;
	bool			m_logRetargetedAnimationsOnScreen;

	bool			IsAudioSyncDisabled() const;

	bool (*m_matchPose)(const class ArtItemAnim* m_pArtItemAnim,
						int startJoint,
						int numJoints,
						float* pSQTs,
						int numFloatChannels,
						float* pFloatChannelFloats,
						float m_frame,
						float* pResFrame,
						float** ppCachedPreClipEvalData,
						float** ppCachedPreClipEvalFloatChannelData,

						float** ppClipEvalWriteBack,
						float** ppClipEvalFloatChannelWriteBack,
						bool& performNormalEvalClip);
	bool (*m_adjustPose)(const class ArtItemAnim* m_pArtItemAnim,
						 int startJoint,
						 int numJoints,
						 float* pSQTs,
						 int numFloatChannels,
						 float* pDestFloatChannelFloats,
						 float m_frame);

	bool			m_trackAnimUsage;
	bool			m_dumpPlayedClips;
	bool			m_dumpUnusedClips;

	bool			m_drawAPLocators;
	bool			m_disableVelocityTweening;
	float			m_debugVelocityTweeningK;

	Clock*			m_pAnimationClock;

	bool			m_generatingNetCommands;

	bool			m_testNpcAnklePoseMatching;
	bool			m_disableAnkleVelMatching;
	float			m_ankleVelMatchingSpringConst;

	bool			m_drawSnapshotHeaps;
	bool			m_printSnapshotHeapUsage;
	bool			m_testSnapshotRelocation;

	float			m_overlayLookupTime;
	bool			m_enableOverlayCacheLookup;
	bool			m_validateCachedAnimLookups;

	bool			m_assertOnDuplicateSkel;
	bool			m_printSkelTable;
#ifndef FINAL_BUILD
	bool			m_debugAnimOverrides;
#endif // !FINAL_BUILD

	bool			m_debugCameraCuts;
	bool			m_debugCameraCutsVerbose;
	bool			m_debugCameraCutsSuperVerbose;

	bool			m_animUpdateAsynchronously;

	bool			m_disableFeatherBlends;
	bool			m_printFeatherBlendStats;

	bool			m_disableAssertOnStateChanges;

	// re-re-re-targeting
	bool m_enableAnimRetargeting;
	bool m_enableDoubleAnimRetargeting;
	bool m_debugRetargetMemento;
	bool m_showRetargetedAnims;
	bool m_showRetargetingWarnings;
	bool m_printRetargetMementoAllocations;
	bool m_allowOutOfDateAnimationRetargeting;
	bool m_allowOutOfDateMeshRetargeting;
	bool m_alwaysAnimateAllSegments;
	bool m_createSkinningForAllSegments;

	bool m_aacEvictAll;
	bool m_aacDebugPrint;
	bool m_debugPrintApEntryCache;
	bool m_forceApEntryCacheRefresh;

	bool m_enableValidation;
	bool m_captureThisFrame;
	bool m_debugStreaming;
	bool m_debugStreamingVerbose;
	bool m_disableLegMrcResults;

	float m_jointSetBlend;

	bool m_sanitizePoses;
	bool m_cinematicCaptureMode;
	bool m_suppressScriptErrors;

	struct ProceduralOptions
	{
		bool m_disableStrideIk;
		bool m_disableLegFixIk;
		bool m_debugDrawLegFixIk;
		bool m_disableHandFixIk;
		bool m_debugDrawHandFixIk;

		bool m_debugAnimNodeIk;
		bool m_debugAnimNodeIkJoints;
		bool m_disableAnimNodeIk;
		bool m_debugWeaponIkFeather = false;
		bool m_disableWeaponIkFeather = false;
		bool m_weaponIkFeatherForce2Bone = false;
		bool m_weaponIkFeatherDisableAutoHandBlend = false;

		bool m_drawArmIkSourcePos;
		bool m_drawFootIkSourcePos;

		bool m_showBrokenArmIK = false;
		bool m_disableStairsDetailProbes = false;

		bool m_disableAllJointLimits = false;
	};

	ProceduralOptions m_procedural;

	struct FootPlant
	{
		bool m_disableFootPlantIk;
		bool m_enableFootPlantIkPrototype;
		bool m_debugFootPlantIk;
		bool m_forceUseHeelToePlantIk;
		bool m_showJacobianResult;
		bool m_showFootStateHistory;
		bool m_showFootBlendValueHistory;
		bool m_showHeelTargetHistory;
		bool m_debugPlantEvaluation;
		bool m_disableHumanFootPlant;
		bool m_disableHorseFootPlant;
		bool m_disableDogFootPlant;
		I32 m_debugHeelTargetIndex;

		void Reset()
		{
			memset(this, 0, sizeof(*this));

			m_debugHeelTargetIndex = -1;
			m_enableFootPlantIkPrototype = true;
			m_disableDogFootPlant = true;
		}
	};

	FootPlant m_footPlant;

	struct HandPlant
	{
		bool m_enable = true;
		bool m_debugPlantEvaluation = false;
		bool m_drawJacobianDeltas = false;
		bool m_drawPreIkJoints = false;
		bool m_drawPostIkJoints = false;
		float m_jointBlendOverride = -1.0f;
		bool m_disableJointLimits = false;
		bool m_debugDrawJointLimits = false;
		bool m_disableWristRot = false;
	};

	HandPlant m_handPlant;

	// gestures
	struct Gestures
	{
		bool m_drawGestureDirMesh = false;
		bool m_drawGestureDirMeshNames = true;
		bool m_debugDrawFeedback = false;

		bool m_disableGestureFeedback = false;
		bool m_disableProxyCorrection = false;
		bool m_disableLookAts = false;
		bool m_disableAimAts = false;
		bool m_disableGestureNodes = false;
		bool m_disableGestureSprings = false;

		bool m_debugGestureAlternatives = false;
		bool m_showGestureLogs = false;
		bool m_debugGesturePermissions = false;
		bool m_debugDrawGestureTargets = false;

		I32 m_debugGestureNodeAnimIndex = -1;
		bool m_overrideGestureSpring = false;
		float m_feedbackStrengthOverride = -1;
		float m_springConstantOverride = 0.0f;
		float m_springDampingRatioOverride = 0.0f;

		bool m_forceTowardsCamera = false;
		bool m_disableExternalCompensation = false;
		bool m_disableSpringTracking = false;
		bool m_forceSingleAnimMode = false;

		bool m_debugDrawFixupIk = false;
		bool m_disableFixupIk = false;
		bool m_disableBaseLayerAlternatives = true;

		bool m_disableNoiseOffsets = false;
		bool m_forceDrawAllNodes = false;

		U32 m_minErrReportSeverity = 0;
	};

	Gestures m_gestures;

	struct Blinks
	{
		bool m_disableBlinks = false;
	};

	Blinks m_blinks;

	struct GestureCacheOptions
	{
		bool m_debugPrint;
		bool m_debugPrintDetails;
		bool m_onlyPrintActiveEntries;
		bool m_dumpCachedEntries;
		bool m_alwaysTryEviction;
		float m_evictionTriggerMin;
		float m_evictionTriggerMax;
		bool m_rebuildCache;
	};

	GestureCacheOptions m_gestureCache;

	struct SpringNodeOptions
	{
		bool m_disable;
		bool m_debugDraw;
	};

	SpringNodeOptions m_jointSpringNodes;

	StringId64 m_selectedGestureId;

	bool IsGestureOrNoneSelected(StringId64 gestureId) const
	{
		return (m_selectedGestureId == gestureId) || (INVALID_STRING_ID_64 == m_selectedGestureId);
	}

	// Controls for special mode in which the instance is being inspected in the actor viewer.
	struct ActorViewerOverrides
	{
		F32			m_mayaFrameIndex;			// frame index for actor viewer playback (treated as 30 FPS sampling, even if sampled at 15 FPS internally)
		F32			m_playbackRate;				// playback rate modifier
		F32			m_mayaFrameIndexOverride;	// ability to control actor viewer frame from outside of actor viewer (for example from maya)
		bool		m_hasFocus;					// does this instance have focus in the actor viewer?
		bool		m_isPaused;					// is playback paused?
		bool		m_isForceLoop;				// is animation forced to loop?
		bool		m_didFrameChange;			// did the frame index change?
		bool		m_steppedForward;			// did the frame index step forward? (false = backward)
		bool		m_wrapped;					// did the frame index wrap?
		bool		m_jumped;					// did the frame index jump? (i.e., don't play effects)

		ActorViewerOverrides();

		void		Clear();
		bool		HasFocus() const { return m_hasFocus; }
		bool		IsForceLoop() const { return m_hasFocus && m_isForceLoop; }
	};

	// Controls for special mode in which the instance is being inspected in the actor viewer.
	ActorViewerOverrides m_actorViewerOverrides;

	bool m_debugDrawAnimGraph;

	struct LayersFilter
	{
	public:
		LayersFilter()
		{
			Clear();
		}

		void Add(StringId64 layerName);
		void Remove(StringId64 layerName);
		bool IsSelected(StringId64 layerName) const;
		bool Valid() const;
		void Clear()
		{
			memset(m_layerNames, 0, sizeof(m_layerNames));
		}

		static const int kMaxNumLayers = 64;
		StringId64 m_layerNames[kMaxNumLayers];
	};

	struct DebugPrintOptions
	{
		bool m_simplified = false;
		bool m_hideTransitions = true;
		bool m_hideNonContributingNodes = true;
		bool m_hideNonBaseLayers = false;
		bool m_hideAdditiveAnimations = false;
		LayersFilter m_showLayersFilter;
		bool m_showPakFileNames = false;
		bool m_showOnlyGestureLayers = false;
		bool m_showOnlyBaseAndGestureLayers = false;
		bool m_useEffectiveFade = false;

		bool ShouldShow(StringId64 layerName, bool isFakeBase = false) const;
	};

	DebugPrintOptions m_debugPrint;

	struct FatConstraintOverrides
	{
		F32 m_translateWeightDefault = -1.0f;
		F32 m_rotateWeightDefault = -1.0f;
		F32 m_scaleWeightDefault = -1.0f;

		F32 m_mass = -1.0f;
		F32 m_stiffness = -1.0f;
		F32 m_damping = -1.0f;

		F32 m_limitPositive[3] = { -1.0f, -1.0f, -1.0f };
		F32 m_limitNegative[3] = { -1.0f, -1.0f, -1.0f };
		F32 m_limitSmoothPercent = -1.0f;
		I32 m_flags = -1.0f;
	};

	struct ProceduralRig
	{
		bool m_debugFatConstraint = false;
		bool m_disableFatConstraint = false;
		bool m_enableFatConstraintOverrides = true;
		FatConstraintOverrides m_fatOverrides;
	};

	ProceduralRig m_proceduralRig;

	struct InverseKinematics
	{
		bool m_disableAllIk = false;
	};
	InverseKinematics m_ikOptions;
};

extern AnimOptions g_animOptions;

#define ANIM_ENABLE_ANIM_CHAIN_TRACE 0
