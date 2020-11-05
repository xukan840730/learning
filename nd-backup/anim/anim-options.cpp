/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-options.h"

#include "gamelib/anim/nd-gesture-util.h"

STATIC_ASSERT((sizeof(AnimOptions) % 16) == 0);

AnimOptions g_animOptions;

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOptions::InitDefaults()
{
	memset(this, 0, sizeof(AnimOptions));

	m_disableAssertOnStateChanges = true; // not ready for prime time
	m_disableMultiActorLoginCheck = false;

	m_enableShapeStreaming = false;
	m_shapeStreamFrame = 0;

	m_debugPrint = DebugPrintOptions();

	m_masterClockScale = 1.0f;
	m_bakedScaleValue = 1.0f;
	m_verboseAnimDebug = false;
	m_showCameraAdditives = false;
	m_disableCameraAdditives = false;
	m_disableLayerIndex = -1;

	m_validateSingleInstanceAnimations = FALSE_IN_FINAL_BUILD(true);

	m_matchPose = nullptr;
	m_adjustPose = nullptr;
	m_enableAnimDataLocking = false;

	// (JDB) turn this on until we feel confident about our EFFs... (probably some months after we ship)
	// (JDB) turning this off to help profile login performance
	//m_validateEffJoints = true;
	//m_validateEffFrames = true; 

	m_logRetargetedAnimations = false;
	m_logRetargetedAnimationsOnScreen = false;

	m_generatingNetCommands = false;

	m_ankleVelMatchingSpringConst = 100.0f;

	m_disableWrinkleMapsAndBlendShapes = false;
	m_displayFloatChannels = false;
	m_renderWrinkleWeights = false;
	m_flipToZyx = false;
	m_displacementScale = 0.1f;
	m_disableWrinkleMaps = false;
	m_disableWrinkleOnly = false;
	m_disableDisplacementOnly = false;
	m_disableAoAnimationOnly = false;


	m_enableOverlayCacheLookup = true;
	m_debugVelocityTweeningK = 0.0f;

	m_enableAnimRetargeting = true;

#if defined(FINAL_BUILD)
	m_enableDoubleAnimRetargeting = false;
	m_allowOutOfDateAnimationRetargeting = false;
	m_allowOutOfDateMeshRetargeting = false;
	m_alwaysAnimateAllSegments = false;
	m_createSkinningForAllSegments = false;
#else
	m_enableDoubleAnimRetargeting = true;
	m_allowOutOfDateAnimationRetargeting = true;
	m_allowOutOfDateMeshRetargeting = false;   // hack    
	m_alwaysAnimateAllSegments = false;
	m_createSkinningForAllSegments = false;
#endif

	m_assertOnDuplicateSkel = true;

	m_enableValidation = false;
	m_captureThisFrame = false;

	m_jointSetBlend = 1.0f;

	m_gestures = Gestures();
	m_gestures.m_minErrReportSeverity = (U32)Gesture::Err::Severity::kHigh;

	m_animUpdateAsynchronously = true;

	m_postPlayStats = false; //true;
	m_disableAudioSync = false;
	m_showAudioSyncGraph = false;
	m_enableJointLimits = true;
	m_useLightObjects = true;

	m_sanitizePoses = false;
	m_cinematicCaptureMode = false;

	m_gestureCache.m_evictionTriggerMax = 0.85f;
	m_gestureCache.m_evictionTriggerMin = 0.3f;

	m_procedural.m_debugAnimNodeIk		 = false;
	m_procedural.m_debugAnimNodeIkJoints = false;
	m_procedural.m_disableAnimNodeIk	 = false;

	m_footPlant.Reset();
	m_handPlant = HandPlant();

	m_proceduralRig = ProceduralRig();

	m_enableCompiledRigExecution = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOptions::IsAudioSyncDisabled() const
{
	return m_disableAudioSync;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOptions::ActorViewerOverrides::Clear()
{
	m_mayaFrameIndex = 0.0f;
	m_playbackRate = 1.0f;
	m_mayaFrameIndexOverride = -1.0f;
	m_hasFocus = false;
	m_isPaused = false;
	m_isForceLoop = false;
	m_didFrameChange = false;
	m_steppedForward = false;
	m_wrapped = false;
	m_jumped = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimOptions::ActorViewerOverrides::ActorViewerOverrides()
{
	Clear();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOptions::LayersFilter::Add(StringId64 layerName)
{
	for (int ii = 0; ii < kMaxNumLayers; ii++)
	{
		if (m_layerNames[ii] == INVALID_STRING_ID_64)
		{
			m_layerNames[ii] = layerName;
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOptions::LayersFilter::Remove(StringId64 layerName)
{
	for (int ii = 0; ii < kMaxNumLayers; ii++)
	{
		if (m_layerNames[ii] == layerName)
			m_layerNames[ii] = INVALID_STRING_ID_64;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOptions::LayersFilter::IsSelected(StringId64 layerName) const
{
	for (int ii = 0; ii < kMaxNumLayers; ii++)
	{
		if (m_layerNames[ii] == layerName)
			return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOptions::LayersFilter::Valid() const
{
	for (int ii = 0; ii < kMaxNumLayers; ii++)
	{
		if (m_layerNames[ii] != INVALID_STRING_ID_64)
			return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOptions::DebugPrintOptions::ShouldShow(StringId64 layerName, bool isFakeBase) const
{
	if (m_showLayersFilter.Valid())
	{
		return m_showLayersFilter.IsSelected(layerName);
	}
	else if (m_hideNonBaseLayers && layerName != SID("base") && !isFakeBase)
	{
		return false;
	}

	return true;
}