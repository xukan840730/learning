/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/math/spherical-coords.h"

#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/math/delaunay.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/util/maybe.h"
#include "ndlib/util/tracker.h"

#include "gamelib/anim/gesture-cache.h"
#include "gamelib/anim/nd-gesture-util.h"
#include "gamelib/script/fact-map.h"
#include "gamelib/scriptx/h/nd-gesture-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class JointSet;
struct AnimPluginContext;
struct ResolvedGestureInfo;
struct SsAnimateParams;

/// --------------------------------------------------------------------------------------------------------------- ///
class IGestureNode : public AnimSnapshotNode
{
public:
	typedef AnimSnapshotNode ParentClass;
	typedef GestureCache::CachedAnim GestureAnim;

	FROM_ANIM_NODE_DECLARE(IGestureNode);

	static SnapshotNodeHeap::Index SnapshotNodeFunc(AnimStateSnapshot* pSnapshot,
													const DC::AnimNode* pDcAnimNode,
													const SnapshotAnimNodeTreeParams& params,
													SnapshotAnimNodeTreeResults& results);

	explicit IGestureNode(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex);

	bool TrySnapshotNode(AnimStateSnapshot* pSnapshot,
						 const ResolvedGestureInfo& info,
						 const SnapshotAnimNodeTreeParams& params,
						 SnapshotAnimNodeTreeResults& results,
						 IStringBuilder* pErrorStringOut = nullptr);

	void PostRootLocatorUpdate(NdGameObject* pOwner,
							   float deltaTime,
							   float statePhase,
							   float combinedBlend,
							   float feedbackBlend);
	void PostRootLocatorUpdate(NdGameObject* pOwner,
							   float deltaTime,
							   float statePhase,
							   float combinedBlend,
							   float feedbackBlend,
							   const Gesture::Target* pTarget);

	void UpdateDesiredAlternative(NdGameObject* pOwner, float statePhase);
	Gesture::AlternativeIndex GetCurrentAlternative() const { return m_cacheKey.m_altIndex; }
	Gesture::AlternativeIndex GetRequestedAlternative() const { return m_requestedAltAnims; }
	bool IsAlternateOutOfDate(const DC::GestureAlternative*& pChosenAlternative) const;

	FactDictionary* AddGestureSpecificFacts(const FactDictionary* pFacts, float statePhase, const char* sourceStr) const;

	void DebugDraw(const NdGameObject* pOwner) const;

	StringId64 GetInputGestureName() const { return m_inputGestureId; }
	StringId64 GetResolvedGestureNameId() const { return m_resolvedGestureId; }

	const DC::ScriptLambda* GetTargetFunc() const { return m_pTargetFunc; }

	bool WantJointLimits() const { return m_pCacheData ? m_pCacheData->m_applyJointLimits : false; }

	StringId64 GetGestureTypeId() const { return m_pCacheData ? m_pCacheData->m_typeId : INVALID_STRING_ID_64; }
	const DC::GestureDef* GetDcDef() const { return m_dcDef; }

	bool IsSingleAnimMode() const
	{
		const DC::GestureDef* pDcDef = GetDcDef();

		return (pDcDef && pDcDef->m_singleAnimMode)
			   || FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_forceSingleAnimMode);
	}

	ArtItemAnimHandle GetChosenSingleAnim() const;

	bool IsUsingDetachedPhase() const { return m_pCacheData->m_useDetachedPhase; }

	I32F GetTargetMgrSlot() const { return m_targetMgrSlot; }
	Point GetSprungTargetPosWs() const;
	Point GetGoalTargetPosWs() const;
	F32 GetYawDeltaForFrame() const;

	static void AnimPluginCallback(StringId64 pluginId, AnimPluginContext* pPluginContext, const void* pData);

	bool IsContiguousWith(const GestureCache::CacheKey& newCacheKey,
						  const BoundFrame& newOwnerLoc,
						  bool requireExact) const;
	
	bool WantLegFixIk() const { return m_useLegFixIk; }
	DC::HandIkHand GetHandFixIkMask() const { return m_handFixIkMask; }

	//
	// AnimSnapshotNode overrides
	//

	virtual bool IsAdditive(const AnimStateSnapshot* pSnapshot) const override { return IsAdditive(); }
	virtual bool IsFlipped() const override { return m_cacheKey.m_flipped; }
	virtual bool HasErrors(const AnimStateSnapshot* pSnapshot) const override;

	virtual void ReleaseNodeRecursive(SnapshotNodeHeap* pHeap) const override;

	virtual SnapshotNodeHeap::Index Clone(SnapshotNodeHeap* pDestHeap, const SnapshotNodeHeap* pSrcHeap) const override;

	virtual U8 RefreshBreadth(AnimStateSnapshot* pSnapshot) override;

	virtual void StepNode(AnimStateSnapshot* pSnapshot,
						  float deltaTime,
						  const DC::AnimInfoCollection* pInfoCollection) override;

	virtual ndanim::ValidBits GetValidBits(const ArtItemSkeleton* pSkel,
										   const AnimStateSnapshot* pSnapshot,
										   U32 iGroup) const override;

	virtual bool HasLoopingAnimation(const AnimStateSnapshot* pSnapshot) const override;
	virtual void GetTriggeredEffects(const AnimStateSnapshot* pSnapshot,
									 EffectUpdateStruct& effectParams,
									 float nodeBlend,
									 const AnimInstance* pInstance) const override;

	virtual bool EvaluateFloatChannel(const AnimStateSnapshot* pSnapshot,
									  float* pOutChannelFloat,
									  const SnapshotEvaluateParams& params) const override;

	virtual bool EvaluateChannel(const AnimStateSnapshot* pSnapshot,
								 ndanim::JointParams* pOutChannelJoint,
								 const SnapshotEvaluateParams& params) const override;

	virtual void CollectContributingAnims(const AnimStateSnapshot* pSnapshot,
										  float blend,
										  AnimCollection* pCollection) const override;

	virtual void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;
	virtual void DebugSubmitAnimPlayCount(const AnimStateSnapshot* pSnapshot) const override;

	virtual void ForAllAnimationsInternal(const AnimStateSnapshot* pSnapshot,
										  AnimationVisitFunc visitFunc,
										  float combinedBlend,
										  uintptr_t userData) const override;

	virtual void OnAddedToBlendNode(const SnapshotAnimNodeTreeParams& params,
									AnimStateSnapshot* pSnapshot,
									AnimSnapshotNodeBlend* pBlendNode,
									bool leftNode) override;

	const DC::GestureAnims* GetDcAnimsDef() const;
	NdGameObjectHandle GetPropHandle() const { return m_hProp; }
	bool DeterminePropAnim(SsAnimateParams* pAnimParams, float statePhase) const;
	F32 GetDetachedPhase() const { return m_detachedPhase; }

protected:
	virtual bool IsAdditive() const { return m_pCacheData->m_key.m_gestureNodeType == Gesture::AnimType::kAdditive; }

	struct SelectedAnimInfo
	{
		SelectedAnimInfo()
		{
			m_anims[0] = m_anims[1] = m_anims[2] = -1;
			m_blend		   = kZero;
			m_targetAngles = kZero;
		}

		I32 m_anims[3];
		Vec2 m_blend;
		Vec2 m_targetAngles;
	};

	void WantAddOrSlerp(bool& wantAdd, bool& wantSlerp) const
	{
		wantAdd	  = false;
		wantSlerp = false;

		switch (m_cacheKey.m_gestureNodeType)
		{
		case Gesture::AnimType::kAdditive:
			wantAdd	  = true;
			wantSlerp = false;
			break;
		case Gesture::AnimType::kSlerp:
			wantAdd	  = false;
			wantSlerp = true;
			break;
		case Gesture::AnimType::kCombo:
			wantAdd	  = true;
			wantSlerp = true;
			break;
		}
	}

	SphericalCoords GetAngles() const;
	SphericalCoords GetInputAngles(Vector_arg inputDirLs, bool* pInsideOut = nullptr) const;
	Locator GetGestureSpaceLocOs(const NdGameObject* pOwner) const;

	void GenerateAnimCommands_Internal(const AnimStateSnapshot* pSnapshot,
									   AnimCmdList* pAnimCmdList,
									   I32F outputInstance,
									   const AnimCmdGenInfo* pCmdGenInfo,
									   bool additive) const;

	void GenerateAnimCommandsForSelection(AnimCmdList* pAnimCmdList,
										  I32F outputInstance,
										  const AnimCmdGenInfo* pCmdGenInfo,
										  const SelectedAnimInfo& animInfo,
										  bool additive) const;

	bool EvaluateChannelForIsland(const AnimStateSnapshot* pSnapshot,
								  ndanim::JointParams* pOutChannelJoint,
								  const SnapshotEvaluateParams& params,
								  int islandIndex) const;

	bool EvaluateFloatChannelForIsland(float* pOutChannelFloat,
									   const SnapshotEvaluateParams& params,
									   int islandIndex,
									   bool additive) const;

	I32F FindNearestBlendTriangle(float hTargetDeg,
								  float vTargetDeg,
								  I32F islandIndex = -1,
								  SphericalCoords* pClosestAnglesOut = nullptr,
								  bool* pTriInsideOut = nullptr) const;

	Vec2 GetBlendValues(I32F iTri, float hTargetDeg, float vTargetDeg) const;
	void SelectGestureAnims(SelectedAnimInfo* pSelectedAnimInfoOut, int islandIndex) const;

	float GetSampleFrame(const GestureAnim& anim, bool additive, const float statePhase, float* pOutTotalFrames) const;
	float GetSamplePhase(const GestureAnim& anim, const float statePhase) const;

	bool AddCmdForAnimation(AnimCmdList* pAnimCmdList,
							I32F index,
							I32F outputInstance,
							const AnimCmdGenInfo* pCmdGenInfo,
							bool additive) const;

	bool AddCmdForBlend(AnimCmdList* pAnimCmdList,
						I32F indexA,
						I32F indexB,
						float blend,
						I32F outputInstance,
						const AnimCmdGenInfo* pCmdGenInfo,
						bool additive) const;

	bool ValidBitsDifferBetweenAnims() const;

	void ForAllAnimationsInternal_Selected(const SelectedAnimInfo& anims,
										   bool additive,
										   const AnimStateSnapshot* pSnapshot,
										   AnimationVisitFunc visitFunc,
										   float combinedBlend,
										   uintptr_t userData) const;

	void AnimPlugin_UpdateGestureSpace(JointSet* pJointSet);
	void AnimPlugin_GetFeedback(JointSet* pJointSet);
	bool AnimPlugin_ApplyFixupIk(const NdGameObject* pGo,
								 JointSet* pJointSet,
								 const AnimPluginContext* pPluginContext,
								 U32F instanceIndex);

	void HandleFeedback(const Locator& jointLocOs, const Locator* pOriginOs = nullptr);

	Quat ReadProxyRotLs(const DC::GestureDef* pDcDef, const AnimStateSnapshot* pSnapshot, I32F childIndex) const;

	const ArtItemAnim* GetAnimFromIndex(I32F iAnim, bool additive) const
	{
		if (iAnim < 0 || iAnim >= m_pCacheData->m_numGestureAnims)
			return nullptr;

		if (additive)
		{
			return m_pCacheData->m_cachedAnims[iAnim].m_hAddAnim.ToArtItem();
		}

		return m_pCacheData->m_cachedAnims[iAnim].m_hSlerpAnim.ToArtItem();
	}

	I32F CalculateBreadth() const;

	void DebugPrintSelectedAnims(AnimNodeDebugPrintData* pData,
								 const SelectedAnimInfo& animInfo,
								 const Color outputColor,
								 bool wantAdd) const;

	void FindFirstMissingAnims(Maybe<StringId64>& missingSlerp, Maybe<StringId64>& missingAdd) const;

	bool RefreshFixupTarget(AnimStateSnapshot* pSnapshot, const DC::AnimInfoCollection* pInfoCollection);

	bool GestureAnimsMirrored() const { return m_mirroredAnims; }

	Quat GetNoiseQuat() const;

	// We can transition from one island to another when:
	// The closest island to the target differs from the current island
	// for a certain minimum amount of time.
	struct IslandTransition
	{
		U8 m_closestIsland = 0;
		U8 m_targetIsland;

		float m_closestIslandTime = 0.0f;
		float m_transitionTime;

		bool m_transitioning = false;

		void ClosestIsland(U8 closestIsland, float dt)
		{
			if (closestIsland != m_closestIsland)
			{
				m_closestIsland		= closestIsland;
				m_closestIslandTime = 0.0f;
			}

			m_closestIslandTime += dt;
		}

		bool ShouldTransition(U8 fromIsland)
		{
			if (m_closestIsland != fromIsland && m_closestIslandTime > 0.2f)
			{
				return true;
			}

			return false;
		}

		void BeginTransition(U8 targetIsland)
		{
			m_transitioning	 = true;
			m_targetIsland	 = targetIsland;
			m_transitionTime = 0.0f;
		}

		float TransitionParameter() const { return MinMax01(m_transitionTime / 0.2f); }

		bool UpdateTransition(float dt)
		{
			m_transitionTime += dt;

			if (TransitionParameter() >= 1.0f)
			{
				m_transitioning = false;

				return true;
			}

			return false;
		}
	};

	GestureCache::CacheKey m_cacheKey;
	const GestureCache::CacheData* m_pCacheData;

	ScriptPointer<DC::GestureDef> m_dcDef;

	IslandTransition m_islandTransition;
	U8 m_currentIslandIndex;

	StringId64 m_inputGestureId;
	StringId64 m_resolvedGestureId;
	Gesture::AlternativeIndex m_requestedAltAnims;
	Gesture::AlternativeIndex m_requestedAltNoise;

	I32 m_nodeUniqueId;
	F32 m_detachedPhase;

	const DC::ScriptLambda* m_pTargetFunc;

	BoundFrame m_ownerLoc;
	Locator m_prevGestureSpaceOs;
	Locator m_gestureSpaceOs;
	Locator m_fixupTargetDeltaOs;

	Point m_targetPosPs;
	Point m_prevGestureOriginOs;
	Point m_gestureOriginOs;
	Point m_constrainedTargetPosLs;
	Quat m_feedbackError;
	Quat m_filteredFeedback;
	Quat m_proxyRotLs;
	Quat m_noiseQuat;

	NdGameObjectHandle m_hProp;

	SphericalCoords m_goalAnglesLs;
	SphericalCoords m_sprungAnglesLs;
	SphericalCoords m_prevAnglesLs;
	SphericalCoords m_prevInputAnglesLs;

	DampedSpringTracker m_hSpringLs;
	DampedSpringTracker m_vSpringLs;
	SpringTracker<float> m_feedbackEffSpring;

	float m_springK;
	float m_springKReduced;
	float m_springDampingRatio;
	float m_feedbackEffect;
	float m_sprungAngleDelay;
	I32 m_targetMgrSlot;
	SnapshotNodeHeap::Index m_baseAnimIndex;

	bool m_loddedOut;
	bool m_firstFrame;
	bool m_targetValid;
	bool m_baseLayerGesture;
	bool m_useLegFixIk;
	bool m_mirroredAnims;
	DC::HandIkHand m_handFixIkMask;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimNodeGestureLeaf : public IGestureNode
{
public:
	FROM_ANIM_NODE_DECLARE(AnimNodeGestureLeaf);

	typedef IGestureNode ParentClass;

	explicit AnimNodeGestureLeaf(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex);

	virtual void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
									  AnimCmdList* pAnimCmdList,
									  I32F outputInstance,
									  const AnimCmdGenInfo* pCmdGenInfo) const override;

	virtual void GenerateAnimCommands_PreBlend(const AnimStateSnapshot* pSnapshot,
											   AnimCmdList* pAnimCmdList,
											   const AnimCmdGenInfo* pCmdGenInfo,
											   const AnimSnapshotNodeBlend* pBlendNode,
											   bool leftNode,
											   I32F leftInstance,
											   I32F rightInstance,
											   I32F outputInstance) const override;

	virtual void GenerateAnimCommands_PostBlend(const AnimStateSnapshot* pSnapshot,
												AnimCmdList* pAnimCmdList,
												const AnimCmdGenInfo* pCmdGenInfo,
												const AnimSnapshotNodeBlend* pBlendNode,
												bool leftNode,
												I32F leftInstance,
												I32F rightInstance,
												I32F outputInstance) const override;
};

ANIM_NODE_DECLARE(AnimNodeGestureLeaf);

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimNodeGestureCombo : public IGestureNode
{
public:
	FROM_ANIM_NODE_DECLARE(AnimNodeGestureCombo);

	typedef IGestureNode ParentClass;

	explicit AnimNodeGestureCombo(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex);

	virtual void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
									  AnimCmdList* pAnimCmdList,
									  I32F outputInstance,
									  const AnimCmdGenInfo* pCmdGenInfo) const override
	{
		ANIM_ASSERT(false);
	}

	virtual void GenerateAnimCommands_CustomBlend(const AnimSnapshotNodeBlend* pParentBlendNode,
												  const AnimSnapshotNode* pChildNode,
												  float nodeBlendToUse,
												  const AnimStateSnapshot* pSnapshot,
												  AnimCmdList* pAnimCmdList,
												  I32F outputInstance,
												  const AnimCmdGenInfo* pCmdGenInfo) const override;
};

ANIM_NODE_DECLARE(AnimNodeGestureCombo);

/// --------------------------------------------------------------------------------------------------------------- ///
template <>
inline void DebugPrintToString<DC::GestureAlternative>(const DC::GestureAlternative* pValue, IStringBuilder* pStr)
{
	if (pValue->m_isKillGesture)
	{
		pStr->append_format("KILL-GESTURE");
	}
	else
	{
		pStr->append_format("(");

		if (pValue->m_altName != INVALID_STRING_ID_64)
		{
			pStr->append_format("%s ", DevKitOnly_StringIdToString(pValue->m_altName));
		}

		const DC::GestureAnims* pAnims = pValue;
		for (int i = 0; i < pAnims->m_numAnimPairs; ++i)
		{
			const DC::GestureAnimPair& animPair = pAnims->m_animPairs[i];

			pStr->append_format("(:partial-anim %s :additive-anim %s)",
								DevKitOnly_StringIdToString(animPair.m_partialAnim),
								DevKitOnly_StringIdToString(animPair.m_additiveAnim));
		}

		pStr->append_format(")");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <>
inline void DebugPrintToString<DC::GestureNoiseDef>(const DC::GestureNoiseDef* pValue, IStringBuilder* pStr)
{
	if (!pValue)
	{
		pStr->append_format("NULL");
	}
	else
	{
		const float fNumFrames = (pValue->m_dataSize > 0) ? float(pValue->m_dataSize - 1) : 0.0f;
		const float noiseDuration = fNumFrames / 30.0f; // fixed 30Hz data for now

		if (fNumFrames > 0.0f)
		{
			pStr->append_format("(%0.2fs of noise)", noiseDuration);
		}
		else
		{
			pStr->append_format("<none>");
		}
	}
}
