/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/map-array.h"
#include "corelib/math/spherical-coords.h"
#include "corelib/util/pair.h"

#include "ndlib/anim/anim-defines.h"

#include "gamelib/anim/gesture-target.h"
#include "gamelib/anim/limb-manager.h"
#include "gamelib/scriptx/h/nd-gesture-script-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class SsTrackGroupInstance;
class IJointReader;
class AnimStateLayer;
class NdGameObject;
class IGestureNode;
class AnimControl;
struct GestureHandle;
struct SnapshotAnimNodeTreeParams;

namespace DC
{
	struct GestureAnims;
	struct GestureDef;
	struct GestureAlternative;
	struct GestureSpaceDef;
	struct GestureState;
	struct AnimNodeGesture;
}

/// --------------------------------------------------------------------------------------------------------------- ///
namespace Gesture
{
	typedef I32 LayerIndex;
	typedef I32 PlayPriority;

	static CONST_EXPR LayerIndex kGestureLayerInvalid = (LayerIndex)(-1);
	static CONST_EXPR size_t kMaxGestureNodeAnims = 32;

	/// --------------------------------------------------------------------------------------------------------------- ///
	enum class AnimType : U8
	{
		kSlerp,
		kAdditive,
		kCombo,
		kUnknown
	};

	typedef I8 AlternativeIndex;
	static CONST_EXPR AlternativeIndex kAlternativeIndexUnspecified = -2;
	static CONST_EXPR AlternativeIndex kAlternativeIndexNone = -1;

	/// --------------------------------------------------------------------------------------------------------------- ///
	inline const char* GetGestureAnimTypeStr(Gesture::AnimType gestureType)
	{
		switch (gestureType)
		{
		case Gesture::AnimType::kAdditive:	return "additive";
		case Gesture::AnimType::kSlerp:		return "slerp";
		case Gesture::AnimType::kCombo:		return "combo";
		default:							return "unspecified";
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	class Config
	{
	public:
		virtual PlayPriority GetDefaultGesturePriority() const = 0;
		virtual bool IsGamePriority(PlayPriority priority) const = 0;
		virtual bool IsScriptPriority(PlayPriority priority) const = 0;

		virtual bool MakeGestureTarget(const DC::GestureDef* pGesture,
									   const NdGameObject* pOwner,
									   const IGestureNode* pGestureNode,
									   const AnimStateInstance* pInst,
									   void* pTargetOut) = 0;

		static Config* Get();
		static void Set(Config* pConfig);
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct Err
	{
		enum class Severity
		{
			kLow, /* doesn't even print an error */
			kNormal,
			kHigh, /* prints red ss err message, crashing the script */
		};

		StringId64 m_errId; // 0 means no err

		Severity m_severity;

		const char* m_pLockoutReason;

		bool m_refreshedExistingGesture;

		void Reset(const StringId64 errId)
		{
			m_errId = errId;
			m_pLockoutReason = nullptr;
			m_refreshedExistingGesture = false;
			m_severity = Severity::kNormal;
		}

		explicit Err() { Reset(INVALID_STRING_ID_64); }

		explicit Err(StringId64 errId) { Reset(errId); }

		bool Success() const { return m_errId == INVALID_STRING_ID_64; }

		Err& WithLockoutReason(const char* pLockoutReason)
		{
			m_pLockoutReason = pLockoutReason;
			return *this;
		}
	};

	inline Err SeriousErr(const StringId64 errId)
	{
		Err err(errId);

		err.m_severity = Err::Severity::kHigh;

		return err;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	class CachedGestureRemap
	{
	public:
		CachedGestureRemap() { Reset(); }

		void Reset()
		{
			m_animLookup.Reset();
			m_finalGestureId = INVALID_STRING_ID_64;
		}

		void SetSourceId(StringId64 const sourceId)
		{
			if (sourceId != GetSourceId())
			{
				m_animLookup.SetSourceId(sourceId);
				m_finalGestureId = INVALID_STRING_ID_64;
			}
		}

		StringId64 const GetSourceId() const { return m_animLookup.GetSourceId(); }

		CachedAnimLookup m_animLookup;
		StringId64 m_finalGestureId;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct PlayArgs
	{
		PlayArgs()
		{
			NDI_NEW(&m_target) TargetPoint(Point(kOrigin));
		}

		void SetPriority(PlayPriority priority)
		{
			m_dcParams.m_priorityValid = true;
			m_dcParams.m_priority = priority;
		}

		void SetLegFixIk(bool legFixIk)
		{
			m_dcParams.m_legFixIkValid = true;
			m_dcParams.m_legFixIk = legFixIk;
		}

		void SetSpringConstant(const float springConstant)
		{
			m_dcParams.m_springConstantValid = true;
			m_dcParams.m_springConstant = springConstant;
		}

		// Decide upon the final gesture target based on m_target and the information supplied in the m_dcParams.
		Err EvalParamsTarget(TargetBuffer* pOutTarget) const;

		SsTrackGroupInstance* m_pGroupInst = nullptr;
		U32 m_trackIndex = -1;

		bool m_flip = false;
		bool m_looping = false;

		F32 m_timeout = -1.0f;
		bool m_tryRefreshExistingGesture = false;
		F32 m_startPhase = 0.0f;
		F32 m_playbackRate = 1.0f;

		F32 m_blendInTime = -1.0f;
		DC::AnimCurveType m_blendInCurve = DC::kAnimCurveTypeInvalid;

		F32 m_blendOutTime = -1.0f;
		DC::AnimCurveType m_blendOutCurve = DC::kAnimCurveTypeInvalid;

		bool m_freeze = false;

		F32 m_gestureBlend = 1.0f;

		/* the result of this gesture attempt will be stored here when the gesture is triggered by an event */
		Err* m_pResultPlace = nullptr;

		/* If the gesture-play is successful, a handle to it will be placed here. */
		GestureHandle* m_pOutGestureHandle = nullptr;

		// On which gesture layer are you requesting a Play or Clear?
		LayerIndex m_gestureLayer = kGestureLayerInvalid;

		TargetBuffer m_target;
		bool m_targetSupplied = false;

		DC::GesturePlayParams m_dcParams;

		bool m_clearWhenNullified = false;
		I32 m_layerPriority = -1;

		MutableNdGameObjectHandle m_hProp;
	};

	extern const PlayArgs g_defaultPlayArgs;

	/// --------------------------------------------------------------------------------------------------------------- ///
	void MergeParams(DC::GesturePlayParams& params, const DC::GesturePlayParams& overrideParams);
	const DC::GesturePlayParams& GetDefaultParams();
	Gesture::Err ValidateParams(const DC::GesturePlayParams& params);
	StringId64 GetInputGestureId(const DC::AnimNodeGesture* pDcGestureNode, const SnapshotAnimNodeTreeParams& params);
	const DC::GestureDef* LookupGesture(StringId64 const gestureId);
	StringId64 GetPhaseAnim(const DC::GestureAnims& gesture);
	CachedGestureRemap RemapGesture(const CachedGestureRemap& prevRemap, const AnimControl* pAnimControl);
	StringId64 RemapGestureAndIncrementVariantIndices(const StringId64 gestureId, AnimControl* pAnimControl);
	StringId64 RemapGesture(const StringId64 gestureId, AnimControl* pAnimControl);
	LimbLockBits GetLimbLockBitsForGesture(const DC::GestureAnims* pGesture, const NdGameObject* pObject);
	SphericalCoords ApRefToCoords(Quat_arg apRefRot, bool flipped);

	AlternativeIndex DesiredAlternative(const DC::GestureDef* pGesture,
										const FactDictionary* pFacts,
										const DC::GestureAlternative*& pOutDesiredAlternative,
										bool debug);

	AlternativeIndex DesiredNoiseAlternative(const DC::GestureDef* pGesture, const FactDictionary* pFacts, bool debug);

	const DC::GestureAnims* GetGestureAnims(const DC::GestureDef* pGesture,
											AlternativeIndex iAlt = kAlternativeIndexUnspecified);

	const DC::GestureAlternative* GetGestureAlternative(const DC::GestureDef* pGesture, AlternativeIndex iAlt);
	StringId64 GetAlternativeName(const DC::GestureDef* pGesture, AlternativeIndex iAlt);

	const IGestureNode* FindPlayingGesture(const AnimStateLayer* pStateLayer, StringId64 gestureId);
	float GetGesturePhase(const AnimStateLayer* pStateLayer, StringId64 gestureId);

} // namespace Gesture
