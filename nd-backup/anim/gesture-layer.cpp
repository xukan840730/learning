/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/gesture-layer.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-node-hand-fix-ik.h"
#include "ndlib/anim/anim-node-leg-fix-ik.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-snapshot-node-blend.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/process/debug-selection.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/anim/gesture-node.h"
#include "gamelib/anim/gesture-target-manager.h"
#include "gamelib/anim/gesture-target.h"
#include "gamelib/anim/nd-gesture-util.h"
#include "gamelib/audio/conversation.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/script/fact-map.h"
#include "gamelib/scriptx/h/nd-gesture-defines.h"
#include "gamelib/state-script/ss-action.h"
#include "gamelib/state-script/ss-animate.h"
#include "gamelib/state-script/ss-track-group.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct BlendToAlternativeParams
{
	const DC::GestureDef* m_pGesture;
	StringId64 m_gestureId;
	StringId64 m_gestureFinalId;
	const DC::GestureAlternative* m_pAlternative;
	I32 m_iAlternative = -1;

	float m_blendInTime = -1.0f;
	DC::AnimCurveType m_blendInCurve = DC::kAnimCurveTypeInvalid;

	float m_phase;
	bool m_useLegFixIk = false;
	DC::HandIkHand m_handFixIkMask = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct GestureNodeKindFuncData
{
	StringId64 m_typeId = INVALID_STRING_ID_64;
	U32F m_count		= 0;
	float m_maxBlend	= 0.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureLayer::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pAnimStateLayer, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pTargetMgr, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool PostAlignUpdatePerGestureNode(AnimStateSnapshot* pSnapshot,
										  AnimSnapshotNode* pNode,
										  AnimSnapshotNodeBlend* pParentBlendNode,
										  float combinedBlend,
										  uintptr_t userData)
{
	IGestureNode* pGestureNode = IGestureNode::FromAnimNode(pNode);
	ANIM_ASSERT(pGestureNode);
	if (!pGestureNode)
		return true;

	IGestureLayer::NodeUpdateParams* pParams = (IGestureLayer::NodeUpdateParams*)userData;

	pParams->m_nodeCount = pParams->m_nodeCount + 1;

	const I32F iSlot = pGestureNode->GetTargetMgrSlot();
	pParams->m_pTargetMgr->TickSlot(iSlot);

	const float statePhase = pParams->m_pInst ? pParams->m_pInst->Phase() : -1.0f;
	pGestureNode->PostRootLocatorUpdate(pParams->m_pOwner,
										pParams->m_deltaTime,
										statePhase,
										combinedBlend,
										pParams->m_enableFeedback);

	if (combinedBlend > 0.0f)
	{
		pParams->m_wantLegFixIk	 = pParams->m_wantLegFixIk || pGestureNode->WantLegFixIk();
		pParams->m_handFixIkMask |= pGestureNode->GetHandFixIkMask();
	}

	if (pParams->m_checkForNewAlts && !pParams->m_altOutOfDate)
	{
		const DC::GestureAlternative* pNewAlt = nullptr;
		
		if (pGestureNode->IsAlternateOutOfDate(pNewAlt))
		{
			pParams->m_altOutOfDate	   = true;
			pParams->m_pAlternateBlend = pNewAlt ? pNewAlt->m_blendIn : nullptr;
		}
	}

	switch (pGestureNode->GetGestureTypeId().GetValue())
	{
	case SID_VAL("look"):
		if (pParentBlendNode)
		{
			pParentBlendNode->SetExternalBlendFactor(pParams->m_enableLook);
		}
		else
		{
			pParams->m_orphanBlend = Min(pParams->m_orphanBlend, pParams->m_enableLook);
		}
		break;

	case SID_VAL("aim"):
		if (pParentBlendNode)
		{
			pParentBlendNode->SetExternalBlendFactor(pParams->m_enableAim);
		}
		else
		{
			pParams->m_orphanBlend = Min(pParams->m_orphanBlend, pParams->m_enableAim);
		}
		break;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct FindPropGestureParams
{
	NdGameObjectHandle m_hProp;
	const IGestureNode* m_pFoundNode = nullptr;
	float m_statePhase = -1.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool FindPropGestureNodePerGestureNode(const AnimStateSnapshot* pSnapshot,
											  const AnimSnapshotNode* pNode,
											  const AnimSnapshotNodeBlend* pParentBlendNode,
											  float combinedBlend,
											  uintptr_t userData)
{
	const IGestureNode* pGestureNode = IGestureNode::FromAnimNode(pNode);
	ANIM_ASSERT(pGestureNode);
	if (!pGestureNode)
		return true;

	FindPropGestureParams* pParams = (FindPropGestureParams*)userData;

	if (pParams->m_pFoundNode)
		return false;

	bool keepGoing = true;

	if (pGestureNode->GetPropHandle() == pParams->m_hProp)
	{
		pParams->m_pFoundNode = pGestureNode;
		keepGoing = false;
	}

	return keepGoing;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
bool IGestureLayer::PostAlignUpdateInstanceCallback(AnimStateInstance* pInstance,
													AnimStateLayer* pStateLayer,
													uintptr_t userData)
{
	AnimStateSnapshot& snapshot = pInstance->GetAnimStateSnapshot();

	IGestureLayer::NodeUpdateParams* pParams = (IGestureLayer::NodeUpdateParams*)userData;
	pParams->m_pInst = pInstance;

	snapshot.VisitNodesOfKind(SID("IGestureNode"), PostAlignUpdatePerGestureNode, userData);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
bool IGestureLayer::FindPropGestureNodeInstanceCallback(const AnimStateInstance* pInstance,
														const AnimStateLayer* pStateLayer,
														uintptr_t userData)
{
	FindPropGestureParams* pParams = (FindPropGestureParams*)userData;

	pParams->m_statePhase = pInstance->GetPrevPhase();

	const AnimStateSnapshot& snapshot = pInstance->GetAnimStateSnapshot();

	snapshot.VisitNodesOfKind(SID("IGestureNode"), FindPropGestureNodePerGestureNode, userData);

	return pParams->m_pFoundNode == nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool GestureNodeKindFunc(const AnimStateSnapshot* pSnapshot,
								const AnimSnapshotNode* pNode,
								const AnimSnapshotNodeBlend* pParentBlendNode,
								float combinedBlend,
								uintptr_t userData)
{
	const IGestureNode* pGestureNode = IGestureNode::FromAnimNode(pNode);
	if (!pGestureNode)
		return true;

	GestureNodeKindFuncData* pData = (GestureNodeKindFuncData*)userData;

	if (pGestureNode && (pGestureNode->GetGestureTypeId() == pData->m_typeId))
	{
		++pData->m_count;
		pData->m_maxBlend = Max(pData->m_maxBlend, combinedBlend);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureLayer::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureLayer::PostAlignUpdate(float lookEnable, float aimEnable, float feedbackEnable)
{
	NdGameObject* pOwner = GetOwner();
	const Clock* pClock = pOwner ? pOwner->GetClock() : nullptr;
	float const dt = pClock ? pClock->GetDeltaTimeInSeconds() : 0.0f;

	AnimStateLayer* pLayer = GetAnimStateLayer();
	const float desiredLayerFade = pLayer ? pLayer->GetDesiredFade() : 1.0f;

	NodeUpdateParams params;
	params.m_pOwner			= pOwner;
	params.m_deltaTime		= dt;
	params.m_pTargetMgr		= m_pTargetMgr;
	params.m_orphanBlend	= desiredLayerFade;
	params.m_enableLook		= lookEnable;
	params.m_enableAim		= aimEnable;
	params.m_enableFeedback = feedbackEnable;
	params.m_checkForNewAlts = CheckNodesForNewAlternates();

	if (pLayer)
	{
		pLayer->WalkInstancesNewToOld(PostAlignUpdateInstanceCallback, (uintptr_t)&params);

		if ((pLayer->GetName() != SID("base")) && (params.m_orphanBlend < desiredLayerFade))
		{
			if (pLayer->IsFadeUpToDate())
			{
				pLayer->Fade(params.m_orphanBlend, 0.0f);
			}
			else
			{
				pLayer->UpdateDesiredFade(params.m_orphanBlend);
			}
		}
	}

	if (params.m_altOutOfDate)
	{
		OnAlternatesOutOfDate(params.m_pAlternateBlend);
	}

	SetWantLegFixIk(params.m_wantLegFixIk);
	SetHandFixIkMask(params.m_handFixIkMask);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureLayer::IsAiming() const
{
	const AnimStateLayer* pLayer = GetAnimStateLayer();
	if (!pLayer)
		return false;

	const AnimStateInstance* pCurInstance = pLayer->CurrentStateInstance();
	if (!pCurInstance)
		return false;

	GestureNodeKindFuncData data;
	data.m_typeId	= SID("aim");
	data.m_maxBlend = 0.0f;

	const AnimStateSnapshot& curSnapshot = pCurInstance->GetAnimStateSnapshot();
	if (const AnimSnapshotNode* pRootNode = pCurInstance->GetRootSnapshotNode())
	{
		pRootNode->VisitNodesOfKind(&curSnapshot, SID("IGestureNode"), GestureNodeKindFunc, (uintptr_t)&data);
	}

	return (data.m_count > 0) && (data.m_maxBlend > 0.9f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureLayer::IsLooking() const
{
	const AnimStateLayer* pLayer = GetAnimStateLayer();
	if (!pLayer)
		return false;

	const AnimStateInstance* pCurInstance = pLayer->CurrentStateInstance();
	if (!pCurInstance)
		return false;

	GestureNodeKindFuncData data;
	data.m_typeId	= SID("look");
	data.m_maxBlend = 0.0f;

	const AnimStateSnapshot& curSnapshot = pCurInstance->GetAnimStateSnapshot();
	if (const AnimSnapshotNode* pRootNode = pCurInstance->GetRootSnapshotNode())
	{
		pRootNode->VisitNodesOfKind(&curSnapshot, SID("IGestureNode"), GestureNodeKindFunc, (uintptr_t)&data);
	}

	return (data.m_count > 0) && (data.m_maxBlend > 0.9f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
class PartialGestureLayer : public IGestureLayer
{
public:
	typedef IGestureLayer ParentClass;

	virtual ~PartialGestureLayer() override { NDI_DELETE m_pConfig; }

	PartialGestureLayer(NdGameObject* pOwner,
						Gesture::LayerIndex index,
						GestureTargetManager* pTargetMgr,
						Gesture::LayerConfig* pConfig);

	virtual void AllocateAnimationLayers(AnimControl* pAnimControl, int priority) const override;
	virtual void CreateAnimationLayers(AnimControl* pAnimControl) override;

	virtual void Update() override;
	virtual void PostAlignUpdate(float lookEnable, float aimEnable, float feedbackEnable) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual Gesture::Err PlayGesture(const StringId64 gestureId, const Gesture::PlayArgs& args) override;
	virtual StringId64 GetActiveGesture() const override { return m_dcDef.GetId(); }
	virtual Gesture::Err ClearGesture(const Gesture::PlayArgs& args) override;

	void StartSsAction(const Gesture::PlayArgs* args);

	virtual void LockAngles() override;

	void ClearNullifiedRegularGestures();

	virtual bool DisablePowerRagdoll() const override;
	virtual bool DisableJournal() const override;
	virtual bool DisallowFacialFlinches() const override;
	virtual bool SuppressBlinking() const override;

	virtual StringId64 GetSourceId() const override;

	virtual Gesture::PlayPriority GetPlayingGesturePriority() const override;

	virtual bool CanStompAtPriority(const Gesture::PlayPriority priority) const override;

	virtual float GetPhase() const override;
	virtual bool IsMirrored() const override;

	virtual GestureHandle GetCurrentHandle() const override;

	void SetPhase(float const phase) const;

	bool ShouldBlendToAlternative(const DC::GestureAlternative*& pDesiredAlternative);

	virtual void SetPlaybackRate(const float playbackRate) override;

	virtual void SetAnimStateLayer(AnimStateLayer* pLayer) override { m_pAnimStateLayer = pLayer; }
	virtual AnimStateLayer* GetAnimStateLayer() const override { return m_pAnimStateLayer; }

	const IGestureNode* GetGestureNode() const;

	virtual bool WantLegFixIk() const override { return m_wantLegFixIk; }
	virtual DC::HandIkHand GetHandFixIkMask() const override { return m_handFixIkMask; }

	virtual float GetCurrentBlendOutTime() const override
	{
		float blendOut = -1.0f;
		if (GetActiveGesture())
		{
			const DC::BlendParams bp = GetEffectiveBlendOut();
			blendOut = bp.m_animFadeTime;
		}
		return blendOut;
	}

private:
	DoutBase* GetGestureLog() const { return m_pConfig->GetLogger(); }

	bool TransitionPending() const;
	void BlendToAlternative(const BlendToAlternativeParams& params);
	bool TimeToFadeOut(bool* pOutLooping, bool* pOutNeverFadeOut) const;

	virtual void SetWantLegFixIk(bool enable) override { m_wantLegFixIk = enable; }
	virtual void SetHandFixIkMask(DC::HandIkHand mask) override { m_handFixIkMask = mask; }

	DC::BlendParams GetEffectiveBlendOut(const Gesture::PlayArgs* pArgs = nullptr) const;

	static StringId64 AnimationLayerName(const Gesture::LayerIndex n)
	{
		return StringId64ConcatInteger(SID("gesture-"), n);
	}
	static StringId64 AnimationStateName(const DC::GestureDef& gesture, const Gesture::LayerIndex n)
	{
		return StringId64ConcatInteger(SID("s_gesture-"), n);
	}

	static void GestureLayer_PreBlend(const AnimStateLayer* pStateLayer,
									  const AnimCmdGenLayerContext& context,
									  AnimCmdList* pAnimCmdList,
									  SkeletonId skelId,
									  I32F leftInstance,
									  I32F rightInstance,
									  I32F outputInstance,
									  ndanim::BlendMode blendMode,
									  uintptr_t userData);

	static void GestureLayer_PostBlend(const AnimStateLayer* pStateLayer,
									   const AnimCmdGenLayerContext& context,
									   AnimCmdList* pAnimCmdList,
									   SkeletonId skelId,
									   I32F leftInstance,
									   I32F rightInstance,
									   I32F outputInstance,
									   ndanim::BlendMode blendMode,
									   uintptr_t userData);



	static CONST_EXPR float kDefaultGestureBlendTime = 0.5f;

	ScriptPointer<DC::GestureDef> m_dcDef;
	Gesture::CachedGestureRemap m_gestureRemap;

	F32 m_activeGestureTimeLeft;		   /* valid if GetActiveGesture() != INVALID_STRING_ID_64 */
	Gesture::PlayArgs m_activeGestureArgs; /* valid if GetActiveGesture() != INVALID_STRING_ID_64 */
	GestureHandle m_activeGestureHandle;

	Gesture::AlternativeIndex m_alternativeIndex;
	U32 m_uniqueGestureId;

	SsAction m_ssAction;

	Gesture::LayerConfig* m_pConfig;

	bool m_wantLegFixIk;
	DC::HandIkHand m_handFixIkMask;
};

/// --------------------------------------------------------------------------------------------------------------- ///
PartialGestureLayer::PartialGestureLayer(NdGameObject* pOwner,
										 Gesture::LayerIndex index,
										 GestureTargetManager* pTargetMgr,
										 Gesture::LayerConfig* pConfig)
	: IGestureLayer(pOwner, index, pTargetMgr)
	, m_pConfig(pConfig)
	, m_alternativeIndex(-1)
	, m_uniqueGestureId(0)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PartialGestureLayer::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	IGestureLayer::Relocate(deltaPos, lowerBound, upperBound);

	DeepRelocatePointer(m_pConfig, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PartialGestureLayer::AllocateAnimationLayers(AnimControl* pAnimControl, I32F priority) const
{
	AnimStateLayerParams params;
	params.m_blendMode		  = ndanim::kBlendSlerp;
	params.m_priority		  = priority + (2 * m_index);
	params.m_numTracksInLayer = 1;
	params.m_numInstancesInLayer   = 4;
	params.m_maxNumSnapshotNodes   = 32;
	params.m_snapshotHeapSizeBytes = 4 * 1024;
	params.m_pChannelIds   = nullptr;
	params.m_numChannelIds = 0;
	params.m_cacheTopInfo  = true;

	pAnimControl->AllocateStateLayer(AnimationLayerName(m_index), params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PartialGestureLayer::CreateAnimationLayers(AnimControl* pAnimControl)
{
	const StringId64 layerName = AnimationLayerName(m_index);
	m_pAnimStateLayer = pAnimControl->CreateStateLayer(layerName);

	ANIM_ASSERTF(m_pAnimStateLayer,
				 ("Failed to create gesture animation layer %s", DevKitOnly_StringIdToString(layerName)));

	if (m_pAnimStateLayer)
	{
		m_pAnimStateLayer->SetCurrentFade(0.0f);

		m_pAnimStateLayer->SetLayerBlendCallbacks(GestureLayer_PreBlend, GestureLayer_PostBlend);
		m_pAnimStateLayer->SetLayerBlendCallbackUserData((uintptr_t)this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PartialGestureLayer::StartSsAction(const Gesture::PlayArgs* args)
{
	if (m_ssAction.IsStarted())
	{
		m_ssAction.Stop();
	}

	if (args && args->m_pGroupInst)
	{
		m_ssAction.Start(args->m_pGroupInst->GetTrackGroupProcessHandle(), args->m_trackIndex);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Err PartialGestureLayer::PlayGesture(const StringId64 gestureId,
											  const Gesture::PlayArgs& args)
{
	if (gestureId == INVALID_STRING_ID_64)
	{
		return Gesture::Err(SID("gesture-id-is-invalid"));
	}

	const AnimStateLayer* pLayer = GetAnimStateLayer();
	if (!pLayer)
	{
		return Gesture::Err(SID("no-layer"));
	}

	NdGameObject* pOwner = GetOwner();

	const AnimControl* pAnimControl = pOwner ? pOwner->GetAnimControl() : nullptr;
	if (!pAnimControl)
	{
		return Gesture::Err(SID("no-anim-control"));
	}

	Gesture::TargetBuffer newTarget;
	const Gesture::Err targetErr = args.EvalParamsTarget(&newTarget);

	if (!targetErr.Success() && (targetErr.m_errId != SID("no-target-supplied")))
	{
		return targetErr;
	}

	Gesture::PlayPriority priority = args.m_dcParams.m_priority;

	if (!m_pConfig)
	{
		return Gesture::Err(SID("performance-gestures-not-supported"));
	}

	StringId64* pPerformanceGestureId = m_pConfig->GetPerformanceGestureId();

	if (!pPerformanceGestureId)
	{
		return Gesture::Err(SID("performance-gestures-not-supported"));
	}

	if (m_dcDef.GetId() != INVALID_STRING_ID_64 && m_activeGestureArgs.m_dcParams.m_priority > priority)
	{
		return Gesture::Err(SID("priority-too-low"));
	}

	Gesture::CachedGestureRemap possibleNewRemap = m_gestureRemap;
	possibleNewRemap.SetSourceId(gestureId);
	possibleNewRemap = Gesture::RemapGesture(possibleNewRemap, pAnimControl);

	if (possibleNewRemap.m_finalGestureId == INVALID_STRING_ID_64)
	{
		Gesture::Err err(SID("gesture-remapped-to-null"));

		err.m_severity = Gesture::Err::Severity::kLow;

		return err;
	}

	const DC::GestureDef* pGesture = Gesture::LookupGesture(possibleNewRemap.m_finalGestureId);
	if (!pGesture)
	{
		return Gesture::SeriousErr(SID("gesture-lookup-failed"));
	}

	{
		const StringId64 phaseAnim = Gesture::GetPhaseAnim(*pGesture);

		const ArtItemAnim* pPhaseAnim = pAnimControl->LookupAnim(phaseAnim).ToArtItem();

		if (!pPhaseAnim)
		{
			return Gesture::SeriousErr(SID("gesture-anims-not-found"));
		}
	}

	if (args.m_tryRefreshExistingGesture && m_dcDef.GetId() == possibleNewRemap.m_finalGestureId
		&& args.m_flip == m_activeGestureArgs.m_flip)
	{
		// We will not play a gesture. Instead, simply refresh the existing gesture and report success.

		m_activeGestureTimeLeft = args.m_timeout;

		m_activeGestureArgs.m_dcParams.m_priority = args.m_dcParams.m_priority;

		Gesture::Err ret;
		ret.m_refreshedExistingGesture = true;

		return ret;
	}

	if (!pLayer->HasFreeInstance())
	{
		StringBuilder<32> sb("numInstances: %d", pLayer->GetMaxInstances());
		return Gesture::Err(SID("no-free-instances-on-gesture-layer")).WithLockoutReason(sb.c_str());
	}

	m_dcDef = ScriptPointer<DC::GestureDef>(possibleNewRemap.m_finalGestureId, SID("gestures"));

	const bool debug = FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugGestureAlternatives
											&& g_animOptions.IsGestureOrNoneSelected(pGesture->m_name)
											&& DebugSelection::Get().IsProcessOrNoneSelected(pOwner));

	const FactDictionary* pFacts = pOwner->UpdateAndGetFactDict();

	const DC::GestureAlternative* pAlternative = nullptr;
	const Gesture::AlternativeIndex alternativeIndex = Gesture::DesiredAlternative(m_dcDef, pFacts, pAlternative, debug);

	if (pAlternative && pAlternative->m_isKillGesture)
	{
		// We immediately selected KILL-GESTURE. Report success but do nothing.

		GESTURE_LOG("Gesture %s killed immediately through alternatives",
					DevKitOnly_StringIdToString(possibleNewRemap.m_finalGestureId));

		return Gesture::Err();
	}

	m_gestureRemap		= possibleNewRemap;
	m_activeGestureArgs = args;

	m_activeGestureTimeLeft = m_activeGestureArgs.m_timeout;
	m_activeGestureHandle = GestureHandle();
	m_activeGestureHandle.m_originalGestureId = possibleNewRemap.m_finalGestureId;

	const DC::GestureAnims* pGestureAnims = pAlternative ? (const DC::GestureAnims*)pAlternative
														 : (const DC::GestureAnims*)pGesture;

	if (m_activeGestureArgs.m_dcParams.m_loop)
	{
		m_activeGestureArgs.m_looping = true;
	}
	else if (pGesture->m_looping)
	{
		m_activeGestureArgs.m_looping = true;
	}

	BlendToAlternativeParams params;
	params.m_pGesture		= pGesture;
	params.m_gestureId		= gestureId;
	params.m_gestureFinalId = possibleNewRemap.m_finalGestureId;
	params.m_pAlternative	= pAlternative;
	params.m_iAlternative	= alternativeIndex;
	params.m_blendInTime	= m_activeGestureArgs.m_blendInTime;
	params.m_blendInCurve	= m_activeGestureArgs.m_blendInCurve;
	params.m_phase = m_activeGestureArgs.m_dcParams.m_startPhaseValid ? m_activeGestureArgs.m_dcParams.m_startPhase
																	  : m_activeGestureArgs.m_startPhase;

	params.m_useLegFixIk = m_activeGestureArgs.m_dcParams.m_legFixIkValid ? m_activeGestureArgs.m_dcParams.m_legFixIk
																		  : false;

	params.m_handFixIkMask = m_activeGestureArgs.m_dcParams.m_handFixIkValid
								 ? m_activeGestureArgs.m_dcParams.m_handFixIk
								 : 0;

	m_alternativeIndex = alternativeIndex;

	if (DC::GestureState* pGestureState = m_pConfig->GetGestureState())
	{
		pGestureState->m_propHandle = args.m_hProp;
	}

	BlendToAlternative(params);

	++m_uniqueGestureId;

	if (args.m_pOutGestureHandle)
	{
		*(args.m_pOutGestureHandle) = GetCurrentHandle();
	}

	StartSsAction(&args);

	if (targetErr.m_errId != SID("no-target-supplied"))
	{
		const StringId64 slotId = m_activeGestureHandle.m_originalGestureId;

		m_pTargetMgr->CreateSlot(slotId, newTarget.AsTarget());
	}

	return Gesture::Err();
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::BlendParams PartialGestureLayer::GetEffectiveBlendOut(const Gesture::PlayArgs* pArgs /* = nullptr */) const
{
	const DC::GestureDef* pGesture = m_dcDef;
	const DC::GestureAnims* pGestureAnims = Gesture::GetGestureAnims(pGesture, m_alternativeIndex);

	float blendOutTime = kDefaultGestureBlendTime;
	DC::AnimCurveType blendOutCurve = DC::kAnimCurveTypeUniformS;

	if (pArgs && pArgs->m_blendOutTime >= 0.0f)
	{
		blendOutTime = pArgs->m_blendOutTime;
	}
	else if (m_activeGestureArgs.m_blendOutTime >= 0.0f)
	{
		blendOutTime = m_activeGestureArgs.m_blendOutTime;
	}
	else if (pGestureAnims && pGestureAnims->m_blendOut && pGestureAnims->m_blendOut->m_animFadeTime >= 0.0f)
	{
		blendOutTime = pGestureAnims->m_blendOut->m_animFadeTime;
	}
	else if (pGesture && pGesture != pGestureAnims && pGesture->m_blendOut
			 && pGesture->m_blendOut->m_animFadeTime >= 0.0f)
	{
		blendOutTime = pGesture->m_blendOut->m_animFadeTime;
	}

	if (pArgs && pArgs->m_blendOutCurve != DC::kAnimCurveTypeInvalid)
	{
		blendOutCurve = pArgs->m_blendOutCurve;
	}
	else if (m_activeGestureArgs.m_blendOutCurve != DC::kAnimCurveTypeInvalid)
	{
		blendOutCurve = m_activeGestureArgs.m_blendOutCurve;
	}
	else if (pGestureAnims && pGestureAnims->m_blendOut
			 && pGestureAnims->m_blendOut->m_curve != DC::kAnimCurveTypeInvalid)
	{
		blendOutCurve = pGestureAnims->m_blendOut->m_curve;
	}
	else if (pGesture && pGesture != pGestureAnims && pGesture->m_blendOut
			 && pGesture->m_blendOut->m_curve != DC::kAnimCurveTypeInvalid)
	{
		blendOutCurve = pGesture->m_blendOut->m_curve;
	}

	return { blendOutTime, -1.0f, blendOutCurve };
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Err PartialGestureLayer::ClearGesture(const Gesture::PlayArgs& args)
{
	const StringId64 activeGestureId = GetActiveGesture();
	if ((activeGestureId == INVALID_STRING_ID_64) || !m_pAnimStateLayer)
	{
		return Gesture::Err();
	}

	NdGameObject* pOwner = GetOwner();
	AnimControl* pAnimControl = pOwner ? pOwner->GetAnimControl() : nullptr;

	const DC::BlendParams blendOut = GetEffectiveBlendOut(&args);

	m_gestureRemap.SetSourceId(INVALID_STRING_ID_64);

	m_gestureRemap	= Gesture::RemapGesture(m_gestureRemap, pAnimControl);
	m_dcDef = ScriptPointer<DC::GestureDef>();

	if (args.m_freeze)
	{
		if (AnimStateInstance* pInstance = m_pAnimStateLayer->CurrentStateInstance())
		{
			pInstance->SetFrozen(true);
		}
	}

	m_pAnimStateLayer->Fade(0.0f, blendOut.m_animFadeTime, blendOut.m_curve);

	return Gesture::Err();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PartialGestureLayer::LockAngles()
{
	const StringId64 activeGestureId = GetActiveGesture();

	if ((activeGestureId != INVALID_STRING_ID_64) && m_pTargetMgr)
	{
		m_pTargetMgr->LockSlot(activeGestureId);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PartialGestureLayer::ClearNullifiedRegularGestures()
{
	if (!IsPlayingGesture() || !m_activeGestureArgs.m_clearWhenNullified)
	{
		return;
	}

	const NdGameObject* pOwner = GetOwner();
	const AnimControl* pAnimControl = pOwner ? pOwner->GetAnimControl() : nullptr;

	if (!pAnimControl)
	{
		return;
	}

	// If the gesture on this layer now remaps to anything other than its original remap, fade it out.
	const Gesture::CachedGestureRemap newRemap = Gesture::RemapGesture(m_gestureRemap, pAnimControl);

	if (newRemap.m_finalGestureId != m_gestureRemap.m_finalGestureId)
	{
		ClearGesture(Gesture::g_defaultPlayArgs);
	}
	else
	{
		// Though nothing has changed, save the CachedAnimLookup information.
		m_gestureRemap = newRemap;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PartialGestureLayer::DisablePowerRagdoll() const
{
	if (const DC::GestureDef* pGesture = m_dcDef)
	{
		return pGesture->m_disablePowerRagdoll;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PartialGestureLayer::SuppressBlinking() const
{
	const DC::GestureDef* pGesture = m_dcDef;
	const DC::GestureAnims* pGestureAnims = Gesture::GetGestureAnims(pGesture, m_alternativeIndex);

	if (pGestureAnims && pGestureAnims->m_suppressBlinking)
	{
		return true;
	}

	if (pGesture && pGesture != pGestureAnims && pGesture->m_suppressBlinking)
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PartialGestureLayer::DisableJournal() const
{
	if (const DC::GestureDef* pGesture = m_dcDef)
	{
		return pGesture->m_disallowJournal;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PartialGestureLayer::DisallowFacialFlinches() const
{
	if (const DC::GestureDef* pGesture = m_dcDef)
	{
		return pGesture->m_noFacialFlinches;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 PartialGestureLayer::GetSourceId() const
{
	return m_gestureRemap.GetSourceId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::PlayPriority PartialGestureLayer::GetPlayingGesturePriority() const
{
	const StringId64 activeGestureId = GetActiveGesture();

	if (activeGestureId == INVALID_STRING_ID_64)
		return -1;

	return m_activeGestureArgs.m_dcParams.m_priority;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PartialGestureLayer::CanStompAtPriority(const Gesture::PlayPriority priority) const
{
	const StringId64 activeGestureId = GetActiveGesture();
	return activeGestureId == INVALID_STRING_ID_64 || m_activeGestureArgs.m_dcParams.m_priority < priority;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float PartialGestureLayer::GetPhase() const
{
	const float failure = -1.0f;

	if (!IsPlayingGesture())
		return failure;

	const AnimStateLayer* pRelevantLayer = m_pAnimStateLayer;
	if (!pRelevantLayer)
		return failure;

	const AnimStateInstance* pCurrentStateInstance = pRelevantLayer->CurrentStateInstance();
	if (!pCurrentStateInstance)
		return failure;

	return pCurrentStateInstance->Phase();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PartialGestureLayer::IsMirrored() const
{
	const float failure = false;

	if (!IsPlayingGesture())
		return failure;

	const AnimStateLayer* pRelevantLayer = m_pAnimStateLayer;
	if (!pRelevantLayer)
		return failure;

	const AnimStateInstance* pCurrentStateInstance = pRelevantLayer->CurrentStateInstance();
	if (!pCurrentStateInstance)
		return failure;

	return pCurrentStateInstance->IsFlipped();
}

/// --------------------------------------------------------------------------------------------------------------- ///
GestureHandle PartialGestureLayer::GetCurrentHandle() const
{
	const StringId64 activeGestureId = GetActiveGesture();
	if (activeGestureId != INVALID_STRING_ID_64)
	{
		GestureHandle handle = m_activeGestureHandle;

		handle.m_gestureLayerIndex = m_index;
		handle.m_uniqueGestureId = m_uniqueGestureId;

		return handle;
	}

	return GestureHandle();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PartialGestureLayer::SetPhase(float const phase) const
{
	if (!IsPlayingGesture())
		return;

	AnimStateInstance* pInstance = m_pAnimStateLayer ? m_pAnimStateLayer->CurrentStateInstance() : nullptr;
	if (pInstance)
	{
		pInstance->SetPhase(phase);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PartialGestureLayer::Update()
{
	ParentClass::Update();

	const NdGameObject* pOwner = GetOwner();
	const AnimControl* pAnimControl = pOwner ? pOwner->GetAnimControl() : nullptr;
	const Clock* pClock = pOwner ? pOwner->GetClock() : nullptr;
	float const dt = pClock ? pClock->GetDeltaTimeInSeconds() : 0.0f;

	const StringId64 activeGestureId = GetActiveGesture();

	if (!TransitionPending())
	{
		bool looping	  = false;
		bool neverFadeOut = false;

		if (TimeToFadeOut(&looping, &neverFadeOut))
		{
			if (looping)
			{
				SetPhase(0.0f);
			}
			else 
			{
				if (m_ssAction.IsStarted())
				{
					m_ssAction.Stop();
				}

				if (!neverFadeOut)
				{
					ClearGesture(Gesture::g_defaultPlayArgs);
				}
			}
		}

		const DC::GestureAlternative* pNewAlternative = nullptr;
		if (ShouldBlendToAlternative(pNewAlternative))
		{
			if (pNewAlternative && pNewAlternative->m_isKillGesture)
			{
				GESTURE_LOG("Gesture %s killed through alternatives", DevKitOnly_StringIdToString(activeGestureId));

				if (m_ssAction.IsStarted())
				{
					m_ssAction.Stop();
				}

				ClearGesture(Gesture::g_defaultPlayArgs);
			}
			else
			{
				const DC::GestureDef* pGesture = Gesture::LookupGesture(m_gestureRemap.m_finalGestureId);

				BlendToAlternativeParams params;
				params.m_pGesture		= pGesture;
				params.m_gestureId		= m_gestureRemap.GetSourceId();
				params.m_gestureFinalId = m_gestureRemap.m_finalGestureId;
				params.m_pAlternative	= pNewAlternative;
				params.m_iAlternative	= m_alternativeIndex;
				params.m_blendInTime	= 0.3f;
				params.m_blendInCurve	= DC::kAnimCurveTypeUniformS;

				if (pNewAlternative && pNewAlternative->m_blendIn)
				{
					params.m_blendInTime  = pNewAlternative->m_blendIn->m_animFadeTime;
					params.m_blendInCurve = pNewAlternative->m_blendIn->m_curve;
				}
				else if (pGesture && (pGesture->m_alternativeBlendTime >= 0.0f))
				{
					params.m_blendInTime = pGesture->m_alternativeBlendTime;
				}

				params.m_phase = Limit01(m_activeGestureHandle.GetPhase(pAnimControl));

				BlendToAlternative(params);
			}
		}
	}

	// do this *after* we check for TimeToFadeOut() because we want to support gestures
	// that are refreshed every frame but have a really short (or even zero frame) timeout value
	if (activeGestureId != INVALID_STRING_ID_64 && m_activeGestureTimeLeft >= 0.0f)
	{
		m_activeGestureTimeLeft = Max(0.0f, m_activeGestureTimeLeft - dt);
	}

	if (activeGestureId && m_pAnimStateLayer)
	{
		const bool fadedOut = m_pAnimStateLayer->IsFadedOut();
		const bool hasInst = (m_pAnimStateLayer->CurrentStateId() != INVALID_STRING_ID_64) || m_pAnimStateLayer->AreTransitionsPending();

		if (fadedOut || !hasInst)
		{
			GESTURE_LOG("Gesture %s killed because our layer is %s",
						DevKitOnly_StringIdToString(activeGestureId),
						fadedOut ? "faded out" : "empty");

			m_gestureRemap.SetSourceId(INVALID_STRING_ID_64);
			m_gestureRemap = Gesture::RemapGesture(m_gestureRemap, pAnimControl);
			m_dcDef = ScriptPointer<DC::GestureDef>();
		}
	}

	// Additional check just to be sure.
	if (!IsPlayingGesture() && m_ssAction.IsStarted())
	{
		m_ssAction.Stop();
	}

	ClearNullifiedRegularGestures();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PartialGestureLayer::PostAlignUpdate(float lookEnable, float aimEnable, float feedbackEnable)
{
	ParentClass::PostAlignUpdate(lookEnable, aimEnable, feedbackEnable);

	if (!IsPlayingGesture())
		return;

	if (!m_pAnimStateLayer)
		return;

	if (NdGameObject* pProp = m_activeGestureArgs.m_hProp.ToMutableProcess())
	{
		FindPropGestureParams findParams;
		findParams.m_hProp = pProp;

		m_pAnimStateLayer->WalkInstancesNewToOld(FindPropGestureNodeInstanceCallback, (uintptr_t)&findParams);

		SsAnimateParams params;
		if (findParams.m_pFoundNode && findParams.m_pFoundNode->DeterminePropAnim(&params, findParams.m_statePhase))
		{
			float curPhase = -1.0f;
			bool pushAnim = false;
			if (pProp->IsAnimPlaying(params.m_nameId, INVALID_STRING_ID_64, &curPhase))
			{
				if ((params.m_speed < kSmallestFloat) && (Abs(curPhase - params.m_startPhase) > 0.01f))
				{
					pushAnim = true;
				}
			}
			else
			{
				pushAnim = true;
			}

			if (pushAnim)
			{
				const DC::BlendParams blendOut = GetEffectiveBlendOut();

				params.m_fadeInSec	  = m_activeGestureArgs.m_blendInTime;
				params.m_fadeInCurve  = m_activeGestureArgs.m_blendInCurve;
				params.m_fadeOutSec	  = blendOut.m_animFadeTime;
				params.m_fadeOutCurve = blendOut.m_curve;

				SendEventFrom(GetOwner(), SID("play-animation"), pProp, BoxedSsAnimateParams(&params));
			}
		}
	}

	const IGestureNode* pGestureNode = GetGestureNode();
	if (pGestureNode && pGestureNode->IsSingleAnimMode())
	{
		AnimStateInstance* pCurInstance = m_pAnimStateLayer->CurrentStateInstance();
		ANIM_ASSERT(pCurInstance);

		const ArtItemAnim* pSingleAnim = pGestureNode->GetChosenSingleAnim().ToArtItem();

		pCurInstance->ChangePhaseAnim(pSingleAnim);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const IGestureNode* PartialGestureLayer::GetGestureNode() const
{
	if (!IsPlayingGesture())
		return nullptr;

	return Gesture::FindPlayingGesture(m_pAnimStateLayer, m_dcDef.GetId());
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PartialGestureLayer::ShouldBlendToAlternative(const DC::GestureAlternative*& pDesiredAlternative)
{
	pDesiredAlternative = nullptr;

	const IGestureNode* pGestureNode = GetGestureNode();

	if (!pGestureNode)
	{
		return false;
	}

	if (!pGestureNode->IsAlternateOutOfDate(pDesiredAlternative))
	{
		return nullptr;
	}

	const Gesture::AlternativeIndex desiredAlternative = pGestureNode->GetRequestedAlternative();

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugGestureAlternatives
							 && g_animOptions.IsGestureOrNoneSelected(pGestureNode->GetResolvedGestureNameId())
							 && DebugSelection::Get().IsProcessOrNoneSelected(m_hOwner)))
	{
		GESTURE_LOG("Gesture %s switching to alternative %d",
					DevKitOnly_StringIdToString(m_dcDef.GetId()),
					desiredAlternative);
	}

	m_alternativeIndex = desiredAlternative;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PartialGestureLayer::SetPlaybackRate(const float playbackRate)
{
	const StringId64 activeGestureId = GetActiveGesture();

	if (activeGestureId == INVALID_STRING_ID_64)
	{
		return;
	}

	if (DC::GestureState* pGestureState = m_pConfig->GetGestureState())
	{
		pGestureState->m_playbackRate = playbackRate;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PartialGestureLayer::TransitionPending() const
{
	bool pending = false;

	const AnimStateLayer* pLayer = GetAnimStateLayer();
	if (pLayer && pLayer->GetTransitionStatus(m_activeGestureHandle.m_scid) & StateChangeRequest::kStatusFlagPending)
	{
		pending = true;
	}

	return pending;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PartialGestureLayer::BlendToAlternative(const BlendToAlternativeParams& params)
{
	NdGameObject* pOwner = GetOwner();
	AnimControl* pAnimControl = pOwner ? pOwner->GetAnimControl() : nullptr;

	ANIM_ASSERT(params.m_pGesture);

	// KILL-GESTURE must be handled earlier
	ANIM_ASSERT(!params.m_pAlternative || !params.m_pAlternative->m_isKillGesture);

	AnimStateLayer* pLayer = GetAnimStateLayer();
	ANIM_ASSERT(pLayer);

	const StringId64 layerNameId = pLayer->GetName();

	DC::GestureState* pGestureState	  = m_pConfig->GetGestureState();
	StringId64* pPerformanceGestureId = m_pConfig->GetPerformanceGestureId();

	const DC::GestureAnims* pGestureAnims = params.m_pGesture;

	if (params.m_pAlternative)
	{
		pGestureAnims = params.m_pAlternative;
	}

	pGestureState->m_alternativeIndex = params.m_iAlternative;
	pGestureState->m_phaseAnim	  = Gesture::GetPhaseAnim(*pGestureAnims);
	pGestureState->m_playbackRate = params.m_pGesture->m_playbackRate
									* (m_activeGestureArgs.m_dcParams.m_playbackRateValid
										   ? m_activeGestureArgs.m_dcParams.m_playbackRate
										   : m_activeGestureArgs.m_playbackRate);
	pGestureState->m_flip = m_activeGestureArgs.m_flip;

	pGestureState->m_springConstant		   = -1.0f;
	pGestureState->m_springConstantReduced = -1.0f;
	pGestureState->m_useLegFixIk = params.m_useLegFixIk;
	pGestureState->m_handFixIkMask = params.m_handFixIkMask;

	if (m_activeGestureArgs.m_dcParams.m_springConstantValid)
	{
		pGestureState->m_springConstant = m_activeGestureArgs.m_dcParams.m_springConstant;
	}

	if (m_activeGestureArgs.m_dcParams.m_springConstantReducedValid)
	{
		pGestureState->m_springConstantReduced = m_activeGestureArgs.m_dcParams.m_springConstantReduced;
	}

	if (params.m_pGesture->m_flipWithBaseLayer)
	{
		const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		const AnimStateInstance* pCurrentInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
		pGestureState->m_flip = pCurrentInstance && pCurrentInstance->IsFlipped();
	}

	*pPerformanceGestureId = params.m_gestureFinalId;

	m_activeGestureHandle.m_animLayerId = layerNameId;

	if (m_activeGestureArgs.m_layerPriority > -1)
	{
		pAnimControl->SetLayerPriority(layerNameId, m_activeGestureArgs.m_layerPriority);
	}
	else if (m_activeGestureArgs.m_dcParams.m_layerPriorityValid)
	{
		pAnimControl->SetLayerPriority(layerNameId, m_activeGestureArgs.m_dcParams.m_layerPriority);
	}
	else
	{
		pAnimControl->SetLayerPriority(layerNameId, 200 + (m_index * 2));
	}

	float const gestureBlend = m_activeGestureArgs.m_dcParams.m_maxBlendInValid
								   ? m_activeGestureArgs.m_dcParams.m_maxBlendIn
								   : m_activeGestureArgs.m_gestureBlend;

	FadeToStateParams fadeParams;
	fadeParams.m_animFadeTime = 0.3f;
	fadeParams.m_blendType = DC::kAnimCurveTypeUniformS;
	fadeParams.ApplyBlendParams(pGestureAnims->m_blendIn);
	fadeParams.m_stateStartPhase = params.m_phase;

	if (params.m_blendInTime >= 0.0f)
	{
		fadeParams.m_animFadeTime	= params.m_blendInTime;
		fadeParams.m_motionFadeTime = -1.0f;
	}

	if (params.m_blendInCurve != DC::kAnimCurveTypeInvalid)
	{
		fadeParams.m_blendType = params.m_blendInCurve;
	}

	pLayer->Fade(gestureBlend, fadeParams.m_animFadeTime, fadeParams.m_blendType);

	const StringId64 animStateId = AnimationStateName(*params.m_pGesture, m_index);

	m_activeGestureHandle.m_scid = pLayer->FadeToState(animStateId, fadeParams);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PartialGestureLayer::TimeToFadeOut(bool* pOutLooping, bool* pOutNeverFadeOut) const
{
	const AnimStateLayer* pLayer = m_pAnimStateLayer;
	if (!pLayer)
		return false;

	if (!IsPlayingGesture())
	{
		/* safety checks to ensure the layers go away */
		if (pLayer->GetDesiredFade() > 0.0f)
			return true;

		return false;
	}

	const DC::GestureDef* pPlayingGesture = m_dcDef;
	if (!pPlayingGesture)
		return true;

	if (m_activeGestureTimeLeft == 0.0f)
		return true;

	if (pPlayingGesture->m_neverFadeOut && pOutNeverFadeOut)
		*pOutNeverFadeOut = true;

	if (m_activeGestureArgs.m_looping && pOutLooping)
		*pOutLooping = true;

	const AnimStateLayer* pRelevantLayer = pLayer;

	/* do not allow gestures to fade out until it is at least possible to push another gesture
	right afterwards */
	if (!pLayer->HasFreeInstance())
		return false;

	const AnimStateInstance* pCurrentStateInstance = pRelevantLayer->CurrentStateInstance();
	if (!pCurrentStateInstance)
		return false;

	if (pPlayingGesture->m_blendOutOnFlipMismatch)
	{
		// pCurrentStateInstance is non-null. It should be our gesture instance, otherwise we are confused.
		const AnimStateInstance* pGestureInstance = pRelevantLayer->GetTransitionDestInstance(m_activeGestureHandle.m_scid);

		if (pGestureInstance)
		{
			const bool instanceFlip = pGestureInstance->IsFlipped();

			const NdGameObject* pOwner = GetOwner();
			const AnimControl* pAnimControl = pOwner ? pOwner->GetAnimControl() : nullptr;
			const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
			const AnimStateInstance* pBaseInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

			if (pBaseInstance)
			{
				const bool baseInstanceFlip = pBaseInstance->IsFlipped();

				// If the flips don't match, fade it out.
				if (instanceFlip != baseInstanceFlip)
				{
					GESTURE_LOG("Fading out gesture '%s due to flip mismatch\n",
								DevKitOnly_StringIdToString(GetActiveGesture()));

					return true;
				}
			}
		}
	}

	float const instPhase = pCurrentStateInstance->Phase();

	// Check the blend-out-early-tests
	const DC::FactMap* pBlendOutEarlyTests = pPlayingGesture->m_blendOutEarlyTests;
	if (pBlendOutEarlyTests)
	{
		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		const IGestureNode* pGestureNode = GetGestureNode();
		NdGameObject* pOwner = m_hOwner.ToMutableProcess();
		const FactDictionary* pOwnerFacts = pOwner ? pOwner->UpdateAndGetFactDict() : nullptr;

		FactDictionary* pGestureFacts = pGestureNode
											? pGestureNode->AddGestureSpecificFacts(pOwnerFacts,
																					pCurrentStateInstance->Phase(),
																					"Blend Out Early Tests")
											: nullptr;

		const bool debug = FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugGestureAlternatives
												&& g_animOptions.IsGestureOrNoneSelected(pPlayingGesture->m_name)
												&& DebugSelection::Get().IsProcessOrNoneSelected(pOwner));

		FactMap::IDebugPrint* pDebugger = nullptr;
		if (debug)
		{
			pDebugger = NDI_NEW(kAllocSingleFrame) FactMap::DebugPrint<float>(kMsgConPauseable);
			MsgConPauseable("Gesture Blend Out Early Tests for %s:\n", DevKitOnly_StringIdToString(pPlayingGesture->m_name));
		}

		const float* pBlendOutPhase = pGestureFacts
										  ? FactMap::Lookup<float>(pBlendOutEarlyTests, pGestureFacts, pDebugger)
										  : nullptr;

		if (pBlendOutPhase)
		{
			if (*pBlendOutPhase <= instPhase)
			{
				if (false)
				{
					MsgConPersistent("Gesture %s blended out early at phase %.2f\n",
									 DevKitOnly_StringIdToString(GetActiveGesture()),
									 *pBlendOutPhase);
				}

				return true;
			}
		}
	}

	const DC::BlendParams blendOut = GetEffectiveBlendOut();

	float const instDuration  = Max(pCurrentStateInstance->GetDuration(), 0.001f);
	float const blendOutPhase = (blendOut.m_animFadeTime > 0.0f && !m_activeGestureArgs.m_looping)
									? 1.0f - (blendOut.m_animFadeTime / instDuration)
									: 1.0f;

	return (instPhase + 0.00001f) >= blendOutPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void PartialGestureLayer::GestureLayer_PreBlend(const AnimStateLayer* pStateLayer,
												const AnimCmdGenLayerContext& context,
												AnimCmdList* pAnimCmdList,
												SkeletonId skelId,
												I32F leftInstance,
												I32F rightInstance,
												I32F outputInstance,
												ndanim::BlendMode blendMode,
												uintptr_t userData)
{
	const PartialGestureLayer* pSelf = (const PartialGestureLayer*)userData;

	if (pSelf->WantLegFixIk())
	{
		AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PreBlend(pAnimCmdList,
																	leftInstance,
																	rightInstance,
																	outputInstance);
	}

	if (pSelf->GetHandFixIkMask())
	{
		AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PreBlend(pAnimCmdList,
																	  leftInstance,
																	  rightInstance,
																	  outputInstance);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void PartialGestureLayer::GestureLayer_PostBlend(const AnimStateLayer* pStateLayer,
												 const AnimCmdGenLayerContext& context,
												 AnimCmdList* pAnimCmdList,
												 SkeletonId skelId,
												 I32F leftInstance,
												 I32F rightInstance,
												 I32F outputInstance,
												 ndanim::BlendMode blendMode,
												 uintptr_t userData)
{
	const PartialGestureLayer* pSelf = (const PartialGestureLayer*)userData;

	if (pSelf->WantLegFixIk())
	{
		AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PostBlend(pAnimCmdList,
																	 nullptr,
																	 leftInstance,
																	 rightInstance,
																	 outputInstance);
	}

	const DC::HandIkHand handFixIkMask = pSelf->GetHandFixIkMask();
	if (handFixIkMask)
	{
		HandFixIkPluginCallbackArg arg;
		arg.m_handsToIk[kLeftArm]  = handFixIkMask & DC::kHandIkHandLeft;
		arg.m_handsToIk[kRightArm] = handFixIkMask & DC::kHandIkHandRight;

		AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PostBlend(pAnimCmdList,
																	   &arg,
																	   leftInstance,
																	   rightInstance,
																	   outputInstance);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
class BaseGestureLayer : public IGestureLayer
{
public:
	BaseGestureLayer(NdGameObject* pOwner,
					 Gesture::LayerIndex index,
					 GestureTargetManager* pTargetMgr,
					 Gesture::ControllerConfig* pConfig)
		: IGestureLayer(pOwner, index, pTargetMgr), m_pConfig(pConfig)
	{
	}

	virtual bool CheckNodesForNewAlternates() const override
	{
		if (g_animOptions.m_gestures.m_disableBaseLayerAlternatives)
		{
			return false;
		}

		return true;
	}

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override
	{
		IGestureLayer::Relocate(deltaPos, lowerBound, upperBound);

		RelocatePointer(m_pConfig, deltaPos, lowerBound, upperBound);
	}

	virtual void OnAlternatesOutOfDate(const DC::BlendParams* pBlend) override
	{
		m_pConfig->NewBaseLayerAlternativeRequested(pBlend);
	}

	//DoutBase* GetGestureLog() const { return m_pConfig->GetLogger(); }

	Gesture::ControllerConfig* m_pConfig;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IGestureLayer* CreatePartialGestureLayer(NdGameObject* pOwner,
										 Gesture::LayerIndex index,
										 GestureTargetManager* pTargetMgr,
										 Gesture::LayerConfig* pConfig)
{
	return NDI_NEW PartialGestureLayer(pOwner, index, pTargetMgr, pConfig);
}

/// --------------------------------------------------------------------------------------------------------------- ///
IGestureLayer* CreateBaseGestureLayer(NdGameObject* pOwner,
									  Gesture::LayerIndex index,
									  GestureTargetManager* pTargetMgr,
									  Gesture::ControllerConfig* pConfig)
{
	return NDI_NEW BaseGestureLayer(pOwner, index, pTargetMgr, pConfig);
}
