/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/anim/gesture-handle.h"
#include "gamelib/anim/nd-gesture-util.h"
#include "gamelib/facts/fact-manager.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimControl;
class AnimStateInstance;
class AnimStateLayer;
class AnimStateSnapshot;
class AnimSnapshotNode;
class NdGameObject;
class GestureTargetManager;

namespace Gesture
{
	struct PlayArgs;
	struct TargetBuffer;
	class ControllerConfig;
}

namespace DC
{
	struct GestureState;
	struct GesturePlayParams;
}

/// --------------------------------------------------------------------------------------------------------------- ///
namespace Gesture
{
	/// --------------------------------------------------------------------------------------------------------------- ///
	class LayerConfig
	{
	public:
		virtual ~LayerConfig() {}

		virtual DC::GestureState* GetGestureState() const = 0;
		virtual StringId64* GetPerformanceGestureId() const = 0;
		virtual DoutBase* GetLogger() const = 0;
		virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) = 0;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class CommonLayerConfig : public LayerConfig
	{
	public:
		CommonLayerConfig(NdGameObject* pOwner) : m_pOwner(pOwner) {}

		void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override
		{
			RelocatePointer(m_pOwner, deltaPos, lowerBound, upperBound);
		}

	protected:
		NdGameObject* m_pOwner;
	};
}

/// --------------------------------------------------------------------------------------------------------------- ///
class IGestureLayer
{
public:
	IGestureLayer(NdGameObject* pOwner, Gesture::LayerIndex index, GestureTargetManager* pTargetMgr)
		: m_hOwner(pOwner), m_index(index), m_pTargetMgr(pTargetMgr), m_pAnimStateLayer(nullptr)
	{
	}
	virtual ~IGestureLayer() {}

	virtual void Update() {}
	virtual void PostAlignUpdate(float lookEnable, float aimEnable, float feedbackEnable);
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	virtual void DebugDraw() const;

	virtual void AllocateAnimationLayers(AnimControl* pAnimControl, I32F priority) const {}
	virtual void CreateAnimationLayers(AnimControl* pAnimControl) {}

	virtual Gesture::Err PlayGesture(StringId64 gestureId, const Gesture::PlayArgs& args)
	{
		return Gesture::Err(SID("unimplemented"));
	}

	virtual Gesture::Err ClearGesture(const Gesture::PlayArgs& args)
	{
		return Gesture::Err(SID("unimplemented"));
	}

	virtual void LockAngles() {}
	virtual void HardReset() {}

	bool IsPlayingGesture() const { return GetActiveGesture() != INVALID_STRING_ID_64; }
	bool IsAiming() const;
	bool IsLooking() const;

	virtual bool IsMirrored() const { return false; }
	virtual bool CanStompAtPriority(const Gesture::PlayPriority priority) const { return false; }

	virtual bool DisablePowerRagdoll() const { return false; }
	virtual bool DisableJournal() const { return false; }
	virtual bool DisallowFacialFlinches() const { return false; }
	virtual bool SuppressBlinking() const { return false; }

	virtual StringId64 GetActiveGesture() const { return INVALID_STRING_ID_64; }
	virtual StringId64 GetSourceId() const { return INVALID_STRING_ID_64; }
	virtual StringId64 GestureInTopInstance() const { return INVALID_STRING_ID_64; }
	virtual Gesture::PlayPriority GetPlayingGesturePriority() const { return -1; }

	virtual void SetPlaybackRate(const float playbackRate) {}
	virtual float GetPhase() const { return -1.0f; }

	virtual GestureHandle GetCurrentHandle() const { return GestureHandle(); }

	virtual void SetAnimStateLayer(AnimStateLayer* pLayer) { m_pAnimStateLayer = pLayer; }
	virtual AnimStateLayer* GetAnimStateLayer() const { return m_pAnimStateLayer; }

	const NdGameObject* GetOwner() const { return m_hOwner.ToProcess(); }
	NdGameObject* GetOwner() { return m_hOwner.ToMutableProcess(); }

	virtual void SetWantLegFixIk(bool enable) {}
	virtual void SetHandFixIkMask(DC::HandIkHand mask) {}

	virtual bool WantLegFixIk() const { return false; }
	virtual DC::HandIkHand GetHandFixIkMask() const { return 0x0; }

	virtual bool CheckNodesForNewAlternates() const { return false; }
	virtual void OnAlternatesOutOfDate(const DC::BlendParams* pBlend) {}

	virtual float GetCurrentBlendOutTime() const { return -1.0f; }

	struct NodeUpdateParams
	{
		NdGameObject* m_pOwner = nullptr;
		GestureTargetManager* m_pTargetMgr = nullptr;
		AnimStateInstance* m_pInst		   = nullptr;
		const DC::BlendParams* m_pAlternateBlend = nullptr;

		// Example: If m_nodeCount equals 2, this is the 3rd gesture node within this anim state instance.
		U32 m_nodeCount		   = 0;
		float m_deltaTime	   = 0.0f;
		float m_enableLook	   = 1.0f;
		float m_enableAim	   = 1.0f;
		float m_enableFeedback = 1.0f;
		float m_orphanBlend	   = 1.0f;

		DC::HandIkHand m_handFixIkMask = 0;

		bool m_wantLegFixIk = false;
		bool m_checkForNewAlts = false;
		bool m_altOutOfDate = false;
	};

	static bool PostAlignUpdateInstanceCallback(AnimStateInstance* pInstance,
												AnimStateLayer* pStateLayer,
												uintptr_t userData);

	static bool FindPropGestureNodeInstanceCallback(const AnimStateInstance* pInstance,
													const AnimStateLayer* pStateLayer,
													uintptr_t userData);

protected:
	const IGestureNode* FindPropGestureNode(NdGameObjectHandle hProp) const;

	MutableNdGameObjectHandle m_hOwner;
	AnimStateLayer* m_pAnimStateLayer;
	GestureTargetManager* m_pTargetMgr;
	Gesture::LayerIndex m_index;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IGestureLayer* CreatePartialGestureLayer(NdGameObject* pOwner,
										 Gesture::LayerIndex index,
										 GestureTargetManager* pTargetMgr,
										 Gesture::LayerConfig* pConfig);

/// --------------------------------------------------------------------------------------------------------------- ///
IGestureLayer* CreateBaseGestureLayer(NdGameObject* pOwner,
									  Gesture::LayerIndex index,
									  GestureTargetManager* pTargetMgr,
									  Gesture::ControllerConfig* pConfig);
