/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/list-array.h"

#include "gamelib/anim/nd-gesture-util.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class Locator;
class NdGameObject;
class AnimControl;
class WindowContext;
class GesturePermit;
class Event;
class ILimbManager;
class GestureTargetManager;
class IkChainSetup;
class JointLimits;
struct GestureHandle;

namespace DC
{
	struct GestureState;
}

/// --------------------------------------------------------------------------------------------------------------- ///
namespace Gesture
{
	struct LayerSpec
	{
		StringId64 m_layerType = INVALID_STRING_ID_64; // 'base or 'regular

		bool IsValid() const
		{
			return m_layerType != INVALID_STRING_ID_64;
		}
	};

	class ControllerConfig
	{
	public:
		virtual ~ControllerConfig() {}

		virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) {}

		virtual ILimbManager* GetLimbManager() = 0;
		virtual DC::GestureState* GetGestureState(Gesture::LayerIndex layerIndex) = 0;
		virtual ListArray<LayerSpec>* GetLayerSpecs() { return GetDefaultLayerSpecs(); } /* may allocate return value from top allocator */

		// the default layer specs creates one base gesture layer and one regular gesture layer
		static ListArray<LayerSpec>* GetDefaultLayerSpecs();

		virtual void NewBaseLayerAlternativeRequested(const DC::BlendParams* pBlend);

		virtual NdGameObject* GetOwner() = 0;
		virtual const NdGameObject* GetOwner() const = 0;

		virtual NdGameObject* LookupPropByName(StringId64 propId) const;
		virtual StringId64 GetCustomLimitsId() const { return INVALID_STRING_ID_64; }
	};
}

/// --------------------------------------------------------------------------------------------------------------- ///
class IGestureController
{
public:
	virtual ~IGestureController() {}

	virtual void AllocateLayers(AnimControl* pAnimControl, I32F priority) = 0;
	virtual void CreateLayers(AnimControl* pAnimControl) = 0;

	virtual void Init(GesturePermit* pPermit, Gesture::ControllerConfig* pConfig) {}
	virtual void PostInit() {}

	virtual void Update() = 0;
	virtual void PostAlignUpdate() {}
	virtual void DebugDraw() const {}
	virtual void Relocate(const ptrdiff_t deltaPos, const uintptr_t lowerBound, const uintptr_t upperBound) = 0;

	virtual Gesture::LayerSpec GetLayerInfo(Gesture::LayerIndex n) const = 0;
	virtual size_t GetNumLayers() const = 0;

	struct HandleGestureEventResult
	{
		HandleGestureEventResult()
			: m_eventHandled(false)
			, m_playGestureSucceeded(false)
			, m_stringIdResult(INVALID_STRING_ID_64)
		{
		}

		explicit HandleGestureEventResult(bool succeeded)
			: m_eventHandled(true), m_playGestureSucceeded(succeeded), m_stringIdResult(INVALID_STRING_ID_64)
		{
		}

		bool m_eventHandled = false;
		bool m_playGestureSucceeded = false;
		StringId64 m_stringIdResult = INVALID_STRING_ID_64;
	};
	virtual HandleGestureEventResult HandleGestureEvent(const Event& evt) = 0;

	/* locks the gesture playing on the layer so that its angles become fixed and no longer track the target */
	virtual void LockGesture(const Gesture::LayerIndex n) {}
	virtual void SetEnabled(bool enable) = 0;

	virtual Gesture::Err Play(const StringId64 gesture, const Gesture::PlayArgs& args = Gesture::g_defaultPlayArgs) = 0;
	virtual Gesture::Err Clear(const Gesture::PlayArgs& args = Gesture::g_defaultPlayArgs) = 0;

	virtual GestureHandle GetPlayingGestureHandle(const Gesture::LayerIndex n) const = 0;

	virtual StringId64 GetActiveGesture(const Gesture::LayerIndex n) const = 0;
	virtual bool IsPlayingGesture(const Gesture::LayerIndex n) const		   = 0;

	// Returns Gesture::kGestureLayerInvalid if no layer is playing gesture.
	virtual Gesture::LayerIndex FindLayerPlaying(const StringId64 gesture) const
	{
		return Gesture::kGestureLayerInvalid;
	}

	bool IsPlayingOnAnyLayer(StringId64 gestureId) const
	{
		return FindLayerPlaying(gestureId) != Gesture::kGestureLayerInvalid;
	}

	virtual bool CanStompLayerAtPriority(const Gesture::LayerIndex n, const Gesture::PlayPriority priority) const
	{
		return false;
	}

	virtual bool IsLayerMirrored(const Gesture::LayerIndex n) const { return false; }

	/* returns negative number if query is meaningless */
	virtual float GetGesturePhase(const Gesture::LayerIndex n) const = 0;
	virtual float GetGesturePhase(StringId64 gestureId, const Gesture::LayerIndex n) const = 0;

	// The first base gesture that maps to the name of an actual gesture takes effect.
	static const unsigned int kMaxBaseGesturesPerLayer = 2;

	virtual StringId64 GetGestureInTopInstance() const = 0;

	virtual bool IsAiming() const = 0; // Are any of the layers playing an aim gesture?
	virtual bool IsLooking() const = 0; // Are any of the layers playing a look gesture?

	// This function will disable lookat for one frame. Call each frame if want to disable lookat for a range
	virtual void DisableLookAtForOneFrame(bool baseLayerOnly = false) = 0;
	virtual void DisableAimForOneFrame() = 0;

	// Do any of the layers request that power ragdoll be disabled?
	virtual bool DisablePowerRagdoll() const	= 0;
	virtual bool DisableJournal() const			= 0;
	virtual bool DisallowFacialFlinches() const = 0;

	virtual float GetWeaponIkBlend() const = 0;

	virtual void SetGesturePlaybackRate(const Gesture::LayerIndex n, const float playbackRate) = 0;

	virtual GestureTargetManager& GetTargetManager() = 0;
	virtual const GestureTargetManager& GetTargetManager() const = 0;

	Gesture::Err ClearGesture();
	Gesture::Err ClearGesture(Gesture::LayerIndex n, const Gesture::PlayArgs* args = nullptr);
	void ClearNonBaseGestures(const Gesture::PlayArgs* args = nullptr);
	virtual void ClearNonMeleeGestures(bool bEvade, const Gesture::PlayArgs* args = nullptr) = 0;
	Gesture::Err ClearLayerPlaying(const StringId64 gesture, const Gesture::PlayArgs& args = Gesture::g_defaultPlayArgs);
	Gesture::Err PlayGesture(const StringId64 gesture, const NdLocatableObject& target, const Gesture::PlayArgs* args = nullptr);
	Gesture::Err PlayGesture(const StringId64 gesture, const Point_arg target, const Gesture::PlayArgs* args = nullptr);
	
	Gesture::LayerIndex GetFirstRegularGestureLayerIndex() const;
	Gesture::LayerIndex GetBaseLayerIndex() const;

	virtual Gesture::LayerIndex GetFirstEmptyLayer(StringId64 layerType = SID("regular")) const = 0; //SID("regular") or SID("base")
	virtual Gesture::LayerIndex GetFirstLayerWithSpace(StringId64 layerType = SID("regular")) const = 0; //SID("regular") or SID("base")

	virtual void ClearLookAtBlend() = 0;

	virtual bool IsLegFixIkEnabledOnLayer(Gesture::LayerIndex n) const { return false; }
	virtual DC::HandIkHand GetHandFixIkMaskForLayer(Gesture::LayerIndex n) const { return 0x0; }

	virtual const JointLimits* GetCustomJointLimits(StringId64 jointLimitsId) const = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IGestureController* CreateGestureController(NdGameObject* pOwner);

/// --------------------------------------------------------------------------------------------------------------- ///
#define GESTURE_LOG(str, ...)                                                                                          \
	do                                                                                                                 \
	{                                                                                                                  \
		if (DoutBase* pGestureLog = NULL_IN_FINAL_BUILD(GetGestureLog()))                                              \
		{                                                                                                              \
			pGestureLog->Printf("% 8.2f: " str "\n", GetOwner()->GetClock()->GetCurTime().ToSeconds(), __VA_ARGS__);   \
		}                                                                                                              \
		if (const NavCharacter* pNavChar = NULL_IN_FINAL_BUILD(NavCharacter::FromProcess(GetOwner())))                 \
		{                                                                                                              \
			AiLogAnim(pNavChar, "<GestureLog> " str "\n", __VA_ARGS__);                                                \
		}                                                                                                              \
	} while (false)

/// --------------------------------------------------------------------------------------------------------------- ///
#define GESTURE_LOG_STR(str)                                                                                           \
	do                                                                                                                 \
	{                                                                                                                  \
		if (DoutBase* pGestureLog = NULL_IN_FINAL_BUILD(GetGestureLog()))                                              \
		{                                                                                                              \
			pGestureLog->Printf("% 8.2f: " str "\n", GetOwner()->GetClock()->GetCurTime().ToSeconds());                \
		}                                                                                                              \
		if (const NavCharacter* pNavChar = NULL_IN_FINAL_BUILD(NavCharacter::FromProcess(GetOwner())))                 \
		{                                                                                                              \
			AiLogAnim(pNavChar, "<GestureLog> " str "\n");                                                             \
		}                                                                                                              \
	} while (false)
