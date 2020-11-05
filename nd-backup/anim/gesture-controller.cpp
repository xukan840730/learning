/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/gesture-controller.h"

#include "corelib/memory/relocate.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/angle.h"
#include "corelib/util/msg-mem.h"
#include "corelib/util/pair.h"

#include "ndlib/anim/anim-cmd-generator-layer.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/ik/ik-chain-setup.h"
#include "ndlib/anim/ik/joint-limits.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process.h"
#include "ndlib/render/display.h"
#include "ndlib/render/util/font.h"
#include "ndlib/render/util/text.h"
#include "ndlib/render/window-context.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/util/common.h"
#include "ndlib/util/tracker.h"

#include "gamelib/anim/blinking.h"
#include "gamelib/anim/gesture-layer.h"
#include "gamelib/anim/gesture-node.h"
#include "gamelib/anim/gesture-permit.h"
#include "gamelib/anim/gesture-target-manager.h"
#include "gamelib/anim/limb-manager.h"
#include "gamelib/anim/nd-gesture-util.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-locatable.h"
#include "gamelib/scriptx/h/nd-gesture-defines.h"
#include "gamelib/state-script/ss-manager.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static const U32 kGestureLogMem = 2048;

/// --------------------------------------------------------------------------------------------------------------- ///
class GestureController : public IGestureController
{
public:
	GestureController(NdGameObject* pOwner);
	virtual ~GestureController() override;

	virtual void Init(GesturePermit* pPermit, Gesture::ControllerConfig* pConfig) override;
	virtual void PostInit() override;

	virtual void Relocate(const ptrdiff_t deltaPos, const uintptr_t lowerBound, const uintptr_t upperBound) override;

	virtual void AllocateLayers(AnimControl* pAnimControl, I32F priority) override;
	virtual void CreateLayers(AnimControl* pAnimControl) override;

	virtual Gesture::LayerSpec GetLayerInfo(Gesture::LayerIndex n) const override
	{
		const size_t numLayers = GetNumLayers();

		if (n < numLayers)
		{
			return m_pLayerSpecs->At(n);
		}

		return Gesture::LayerSpec();
	}

	virtual HandleGestureEventResult HandleGestureEvent(const Event& evt) override;

	virtual Gesture::Err Play(const StringId64 gesture, const Gesture::PlayArgs& args) override;
	virtual Gesture::Err Clear(const Gesture::PlayArgs& args) override;

	virtual void LockGesture(const Gesture::LayerIndex n) override
	{
		m_pLayer[n]->LockAngles();
	}
	
	void SetEnabled(bool enable) override
	{
		m_enabledNext = enable;
	}

	void UpdateEnable();
	void UpdateExternalBlendFactors();
	virtual void Update() override;

	virtual void PostAlignUpdate() override;
	virtual void DebugDraw() const override;

	virtual StringId64 GetActiveGesture(const Gesture::LayerIndex n) const override
	{
		const size_t numLayers = GetNumLayers();

		if (n == Gesture::kGestureLayerInvalid)
		{
			for (Gesture::LayerIndex iLayer = GetFirstRegularGestureLayerIndex(); iLayer < numLayers; ++iLayer)
			{
				const StringId64 gestureId = m_pLayer[iLayer]->GetActiveGesture();

				if (gestureId != INVALID_STRING_ID_64)
					return gestureId;
			}

			return INVALID_STRING_ID_64;
		}

		if (n >= numLayers)
			return INVALID_STRING_ID_64;

		return m_pLayer[n]->GetActiveGesture();
	}
	
	virtual GestureHandle GetPlayingGestureHandle(const Gesture::LayerIndex n) const override
	{
		return m_pLayer[n]->GetCurrentHandle();
	}

	virtual bool IsPlayingGesture(const Gesture::LayerIndex n) const override;
	virtual Gesture::LayerIndex FindLayerPlaying(const StringId64 gesture) const override;

	virtual float GetGesturePhase(const Gesture::LayerIndex n) const override
	{
		return m_pLayer[n]->GetPhase();
	}

	virtual float GetGesturePhase(StringId64 gestureId, const Gesture::LayerIndex n) const override;

	virtual bool CanStompLayerAtPriority(const Gesture::LayerIndex n, const Gesture::PlayPriority priority) const override
	{
		return m_pLayer[n]->CanStompAtPriority(priority);
	}

	virtual bool IsLayerMirrored(const Gesture::LayerIndex n) const override
	{
		return m_pLayer[n]->IsMirrored();
	}

	virtual StringId64 GetGestureInTopInstance() const override
	{
		const Gesture::LayerIndex n = GetBaseLayerIndex();
		if (n == Gesture::kGestureLayerInvalid)
			return INVALID_STRING_ID_64;

		return m_pLayer[n]->GestureInTopInstance();
	}

	void SetGesturePlaybackRate(const Gesture::LayerIndex n, const float playbackRate) override
	{
		ANIM_ASSERT(n < GetNumLayers());

		m_pLayer[n]->SetPlaybackRate(playbackRate);
	}

	virtual bool IsAiming() const override { return m_isAiming; }
	virtual bool IsLooking() const override { return m_isLooking; }
	virtual void DisableLookAtForOneFrame(bool baseLayerOnly = false) override 
	{
		if (baseLayerOnly)
		{
			m_disableBaseLookAtThisFrame = true;
		}
		else
		{
			m_disableLookAtThisFrame = true;
		}
	}
	virtual void DisableAimForOneFrame() override { m_disableAimThisFrame = true; }

	virtual bool DisablePowerRagdoll() const override
	{
		const size_t numLayers = GetNumLayers();

		for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
		{
			if (m_pLayer[n]->DisablePowerRagdoll())
			{
				return true;
			}
		}

		return false;
	}

	virtual bool DisableJournal() const override
	{

		for (Gesture::LayerIndex n = 0; n < GetNumLayers(); n++)
		{
			if (m_pLayer[n]->DisableJournal())
				return true;
		}

		return false;
	}

	virtual bool DisallowFacialFlinches() const override
	{
		const size_t numLayers = GetNumLayers();

		for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
		{
			if (m_pLayer[n]->DisallowFacialFlinches())
			{
				return true;
			}
		}

		return false;
	}

	virtual float GetWeaponIkBlend() const override;
		
	virtual GestureTargetManager& GetTargetManager() override { return m_targetMgr; }
	virtual const GestureTargetManager& GetTargetManager() const override { return m_targetMgr; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	size_t GetNumLayers() const override
	{
		return m_pLayerSpecs ? m_pLayerSpecs->Size() : 0;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void ClearNonMeleeGestures(bool isEvade, const Gesture::PlayArgs* args) override
	{
		const size_t numLayers = GetNumLayers();
		for (U32F iLayer = 0; iLayer < numLayers; ++iLayer)
		{
			if (GetLayerInfo(iLayer).m_layerType != SID("base"))
			{
				if (StringId64 activeGesture = m_pLayer[iLayer]->GetActiveGesture())
				{
					const DC::GestureDef* pGesture = Gesture::LookupGesture(activeGesture);
					if (pGesture->m_allowDuringMelee)
						continue;

					if (isEvade && pGesture->m_allowDuringEvade)
						continue;
				}

				ClearGesture(iLayer, args);
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual Gesture::LayerIndex GetFirstEmptyLayer(StringId64 layerType) const override
	{
		const size_t numLayers = GetNumLayers();
		for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
		{
			if (layerType == INVALID_STRING_ID_64 || GetLayerInfo(n).m_layerType == layerType)
			{
				const IGestureLayer* pGestureLayer = m_pLayer[n];
				if (!pGestureLayer)
					continue;

				if (pGestureLayer->GetActiveGesture() != INVALID_STRING_ID_64)
					continue;

				const AnimStateLayer* pAnimLayer = pGestureLayer->GetAnimStateLayer();
				if (!pAnimLayer)
					continue;

				if (pAnimLayer->HasFreeInstance() && pAnimLayer->NumUsedTracks() == 0)
					return n;
			}
		}
		return Gesture::kGestureLayerInvalid;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual Gesture::LayerIndex GetFirstLayerWithSpace(StringId64 layerType) const override
	{
		const size_t numLayers = GetNumLayers();
		for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
		{
			if (layerType == INVALID_STRING_ID_64 || GetLayerInfo(n).m_layerType == layerType)
			{
				const IGestureLayer* pGestureLayer = m_pLayer[n];
				if (!pGestureLayer)
					continue;

				if (pGestureLayer->GetActiveGesture() != INVALID_STRING_ID_64)
					continue;

				const AnimStateLayer* pAnimLayer = pGestureLayer->GetAnimStateLayer();
				if (!pAnimLayer)
					continue;

				if (pAnimLayer->HasFreeInstance() && pAnimLayer->HasFreeTrack())
					return n;
			}
		}
		return Gesture::kGestureLayerInvalid;
	}

	virtual void ClearLookAtBlend() override
	{
		m_baseLookAtControlBlend = 0.0f;
	}

	virtual bool IsLegFixIkEnabledOnLayer(Gesture::LayerIndex n) const override
	{
		IGestureLayer* pLayer = m_pLayer[n];
		if (!pLayer)
			return false;

		return pLayer->WantLegFixIk();
	}

	virtual DC::HandIkHand GetHandFixIkMaskForLayer(Gesture::LayerIndex n) const override
	{
		IGestureLayer* pLayer = m_pLayer[n];
		if (!pLayer)
			return 0x0;

		return pLayer->GetHandFixIkMask();
	}

	virtual const JointLimits* GetCustomJointLimits(StringId64 jointLimitsId) const override
	{
		if (jointLimitsId == m_customJointLimitsId)
		{
			return m_pCustomJointLimits;
		}

		return nullptr;
	}

private:
	void UpdateFlagBlends();

	/// --------------------------------------------------------------------------------------------------------------- ///
	Gesture::LayerIndex ChooseLayer(const Gesture::PlayArgs* args) const
	{
		// If layer specified in the args, it takes priority.
		if (args && args->m_gestureLayer != Gesture::kGestureLayerInvalid)
		{
			return args->m_gestureLayer;
		}
		
		return GetFirstRegularGestureLayerIndex();
	}
	
	/// --------------------------------------------------------------------------------------------------------------- ///
	Gesture::Err LogResult(const Gesture::Err& err, const char* preamble, const StringId64 optionalStringId)
	{
		STRIP_IN_FINAL_BUILD_VALUE(err);

		if (err.m_refreshedExistingGesture)
		{
			// no need to log
			return err;
		}

		StringBuilder<1024> log;

		log.append_format("%s%s", preamble, (optionalStringId != INVALID_STRING_ID_64) ? DevKitOnly_StringIdToString(optionalStringId) : "");

		if (!err.Success())
		{
			log.append_format(" -> failure (%s", DevKitOnly_StringIdToString(err.m_errId));

			if (err.m_errId == SID("locked-out"))
			{
				log.append_format(", %s", err.m_pLockoutReason ? err.m_pLockoutReason : "no reason given");
			}

			log.append_format(")");
		}

		GESTURE_LOG("%s", log.c_str());

		return err;
	}

	NdGameObject* GetOwner() { return m_pOwner; }
	DoutBase* GetGestureLog() const { return m_pGestureLog; }

	bool m_enabled;
	bool m_enabledNext;

	NdGameObject* m_pOwner;
	GesturePermit* m_pPermit;
	Gesture::ControllerConfig* m_pConfig;

	IGestureLayer** m_pLayer;
	LimbLock* m_limbLock;
	
	GestureTargetManager m_targetMgr;

	// TODO: after ship, this should be a table
	// or better yet allocate these in AnimNodeJointLimits itself (which requires dynamically sized node support)
	JointLimits* m_pCustomJointLimits = nullptr;
	StringId64 m_customJointLimitsId = INVALID_STRING_ID_64;

	bool m_isAiming;
	bool m_isLooking;

	float m_lookEnabledBlend;
	float m_baseLookEnabledBlend;
	float m_aimEnabledBlend;
	float m_weaponIkBlend;
	float m_feedackDisableBlend;
	
	float m_baseLookAtControlBlend;
	float m_lookAtControlBlend;
	float m_aimControlBlend;
	bool m_disableLookAtThisFrame;
	bool m_disableBaseLookAtThisFrame;
	bool m_disableAimThisFrame;
	bool m_allowPowerRagdoll;

	DoutMem* m_pGestureLog;

	ListArray<Gesture::LayerSpec>* m_pLayerSpecs;

	DISALLOW_COPY_AND_ASSIGN(GestureController);
};

/// --------------------------------------------------------------------------------------------------------------- ///
IGestureController* CreateGestureController(NdGameObject* pOwner)
{
	return NDI_NEW GestureController(pOwner);
}

/// --------------------------------------------------------------------------------------------------------------- ///
class GenericLayerConfig : public Gesture::CommonLayerConfig
{
public:
	GenericLayerConfig(Gesture::LayerIndex index,
					   Gesture::ControllerConfig* pControllerConfig,
					   NdGameObject* pOwner,
					   DoutBase* pLogger)
		: m_index(index), m_pControllerConfig(pControllerConfig), m_pLogger(pLogger), CommonLayerConfig(pOwner)
	{
	}

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override
	{
		CommonLayerConfig::Relocate(deltaPos, lowerBound, upperBound);

		RelocatePointer(m_pControllerConfig, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_pLogger, deltaPos, lowerBound, upperBound);
	}

	DC::GestureState* GetGestureState() const override
	{
		return m_pControllerConfig->GetGestureState(m_index);
	}

	StringId64* GetPerformanceGestureId() const override
	{
		if (DC::GestureState* pGS = GetGestureState())
		{
			return &(pGS->m_performanceGestureId);
		}

		return nullptr;
	}

	DoutBase* GetLogger() const override { return m_pLogger; }

private:
	Gesture::LayerIndex m_index;
	Gesture::ControllerConfig* m_pControllerConfig;
	DoutBase* m_pLogger;
};

/// --------------------------------------------------------------------------------------------------------------- ///
GestureController::GestureController(NdGameObject* pOwner)
	: m_pOwner(pOwner)
	, m_isAiming(false)
	, m_isLooking(false)
	, m_lookAtControlBlend(1.0f)
	, m_baseLookAtControlBlend(1.0f)
	, m_aimControlBlend(1.0f)
	, m_disableLookAtThisFrame(false)
	, m_disableBaseLookAtThisFrame(false)
	, m_disableAimThisFrame(false)
	, m_pPermit(nullptr)
	, m_pConfig(nullptr)
	, m_pGestureLog(nullptr)
	, m_pLayerSpecs(nullptr)
	, m_pLayer(nullptr)
	, m_limbLock(nullptr)
	, m_enabled(true)
	, m_enabledNext(true)
	, m_allowPowerRagdoll(true)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
GestureController::~GestureController()
{
	NDI_DELETE_CONTEXT(kAllocDebug, m_pGestureLog);

	NDI_DELETE m_pPermit;
	NDI_DELETE m_pConfig;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureController::Init(GesturePermit* pPermit, Gesture::ControllerConfig* pConfig)
{
	m_pConfig = pConfig;

	ANIM_ASSERTF(pPermit, ("Tried to create GestureController without providing a GesturePermit"));
	m_pPermit = pPermit;

	if (!IsFinalBuild())
	{
		m_pGestureLog = NDI_NEW (kAllocDebug) DoutMem("Gesture Log", NDI_NEW char[kGestureLogMem], kGestureLogMem);

		GESTURE_LOG("*** Begin %s gesture log ***", m_pOwner->GetName());
	}

	m_pLayerSpecs = pConfig->GetLayerSpecs();

	const size_t numLayers = GetNumLayers();

	m_limbLock = NDI_NEW LimbLock[numLayers];

	m_pLayer = NDI_NEW IGestureLayer*[numLayers];
	memset(m_pLayer, 0, numLayers * sizeof(IGestureLayer*));

	for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
	{
		const Gesture::LayerSpec& spec = m_pLayerSpecs->At(n);

		if (n == GetBaseLayerIndex())
		{
			m_pLayer[n] = CreateBaseGestureLayer(m_pOwner, n, &m_targetMgr, m_pConfig);
		}
		else
		{
			m_pLayer[n] = CreatePartialGestureLayer(m_pOwner,
													n,
													&m_targetMgr,
													NDI_NEW GenericLayerConfig(n, pConfig, m_pOwner, m_pGestureLog));
		}
	}

	m_targetMgr.Init(m_pOwner);

	m_isAiming = false;
	m_isLooking = false;

	m_lookAtControlBlend = 1.0f;
	m_baseLookAtControlBlend = 1.0f;
	m_aimControlBlend = 1.0f;

	m_lookEnabledBlend	   = 0.0f;
	m_baseLookEnabledBlend = 0.0f;
	m_aimEnabledBlend	   = 0.0f;
	m_weaponIkBlend		   = 0.0f;
	m_feedackDisableBlend  = 0.0f;

	m_customJointLimitsId = m_pConfig->GetCustomLimitsId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureController::PostInit()
{
	AnimControl* pAnimControl = m_pOwner ? m_pOwner->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	const Gesture::LayerIndex i = GetBaseLayerIndex();
	m_pLayer[i]->SetAnimStateLayer(pBaseLayer);

	if (m_customJointLimitsId)
	{
		FgAnimData* pAnimData = m_pOwner->GetAnimData();
		const AnimTable& animTable = pAnimControl->GetAnimTable();

		m_pCustomJointLimits = NDI_NEW JointLimits;
		m_pCustomJointLimits->SetupJoints(pAnimData->m_animateSkelHandle, &animTable, m_customJointLimitsId);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureController::AllocateLayers(AnimControl* pAnimControl, I32F priority)
{
	const size_t numLayers = GetNumLayers();

	for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
	{
		if (n != GetBaseLayerIndex())
		{
			m_pLayer[n]->AllocateAnimationLayers(pAnimControl, priority + 1);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureController::CreateLayers(AnimControl* pAnimControl)
{
	const size_t numLayers = GetNumLayers();

	for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
	{
		if (n != GetBaseLayerIndex())
		{
			m_pLayer[n]->CreateAnimationLayers(pAnimControl);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureController::Relocate(const ptrdiff_t deltaPos, const uintptr_t lowerBound, const uintptr_t upperBound)
{
	const size_t numLayers = GetNumLayers();

	RelocatePointer(m_pOwner, deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pPermit, deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pConfig, deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pGestureLog, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_limbLock, deltaPos, lowerBound, upperBound);

	for (Gesture::LayerIndex i = 0; i < numLayers; ++i)
	{
		DeepRelocatePointer(m_pLayer[i], deltaPos, lowerBound, upperBound);
	}

	RelocatePointer(m_pLayer, deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pLayerSpecs, deltaPos, lowerBound, upperBound);

	DeepRelocatePointer(m_pCustomJointLimits, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
GestureController::HandleGestureEventResult GestureController::HandleGestureEvent(const Event& evt)
{
	const StringId64 msg = evt.GetMessage();
	switch (msg.GetValue())
	{
	case SID_VAL("clear-gesture-log"):
		if (m_pGestureLog)
			m_pGestureLog->Clear();
		GESTURE_LOG("*** Reset %s gesture log ***", m_pOwner->GetName());
		return HandleGestureEventResult(true);
		break;

	case SID_VAL("play-gesture"):
		{
			if (evt.GetNumParams() == 2)
			{
				const StringId64 gestureId	   = evt.Get(0).GetStringId();
				const Gesture::PlayArgs* pArgs = evt.Get(1).GetConstPtr<Gesture::PlayArgs*>();

				const Gesture::Err playResult = Play(gestureId, *pArgs);

				if (pArgs->m_pResultPlace)
				{
					*(pArgs->m_pResultPlace) = playResult;
				}

				return HandleGestureEventResult(playResult.Success());
			}
			else
			{
				return HandleGestureEventResult(false);
			}
		}
		break;

	case SID_VAL("play-gesture-towards-point"):
		{
			if (evt.GetNumParams() == 3)
			{
				const Gesture::PlayArgs* pArgs = evt.Get(2).GetConstPtr<Gesture::PlayArgs*>();

				const Gesture::Err playResult = PlayGesture(evt.Get(0).GetStringId(), evt.Get(1).GetPoint(), pArgs);

				if (pArgs && pArgs->m_pResultPlace)
				{
					*(pArgs->m_pResultPlace) = playResult;
				}

				return HandleGestureEventResult(playResult.Success());
			}
			else
			{
				return HandleGestureEventResult(false);
			}
		}
		break;

	case SID_VAL("play-gesture-debug"):
		{
			if (evt.GetNumParams() >= 1)
			{
				Gesture::PlayArgs args;
				args.SetPriority(999999);

				const Gesture::LayerIndex layerIndex = evt.Get(1).GetAsU32(Gesture::kGestureLayerInvalid);
				if (layerIndex != Gesture::kGestureLayerInvalid)
					args.m_gestureLayer = layerIndex;

				return HandleGestureEventResult(PlayGesture(evt.Get(0).GetStringId(), SMath::Point(kZero), &args).Success());
			}
			else
			{
				return HandleGestureEventResult(false);
			}
		}
		break;

	case SID_VAL("clear-gesture"):
		{
			Gesture::PlayArgs args = Gesture::g_defaultPlayArgs;
			Gesture::LayerIndex index = GetFirstRegularGestureLayerIndex();

			if ((evt.GetNumParams() > 0) && (evt.Get(0).GetType() == DC::kBoxableTypeI32))
			{
				const I32F iLayer = evt.Get(0).GetI32();
				if (iLayer >= 0)
				{
					index = iLayer;
				}
			}

			if ((evt.GetNumParams() > 1) && (evt.Get(1).GetType() == DC::kBoxableTypeF32))
			{
				args.m_blendOutTime = evt.Get(1).GetF32();
			}

			if ((evt.GetNumParams() > 2) && (evt.Get(2).GetType() == DC::kBoxableTypeU32))
			{
				args.m_blendOutCurve = evt.Get(2).GetU32();
			}

			return HandleGestureEventResult(ClearGesture(index, &args).Success());
		}
		break;

	case SID_VAL("clear-gesture-instantly"):
		{
			Gesture::PlayArgs args;
			args.m_blendOutTime = 0.0f;

			Gesture::LayerIndex index = GetFirstRegularGestureLayerIndex();

			if (evt.GetNumParams() > 0)
			{
				index = evt.Get(0).GetAsU32();
			}

			return HandleGestureEventResult(ClearGesture(index, &args).Success());
		}
		break;
	}

	return HandleGestureEventResult();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Err GestureController::Play(const StringId64 gesture, const Gesture::PlayArgs& args)
{
	if (!m_enabled || !m_enabledNext)
	{
		return Gesture::Err(SID("gesture-controller-disabled"));
	}

	AnimControl* pAnimControl = m_pOwner->GetAnimControl();

	Gesture::PlayArgs mergedArgs(args);
	mergedArgs.m_dcParams = Gesture::GetDefaultParams();
	const StringId64 remappedGesture = Gesture::RemapGestureAndIncrementVariantIndices(gesture, pAnimControl);
	const DC::GestureDef* pGesture = Gesture::LookupGesture(remappedGesture);
	if (pGesture)
	{
		Gesture::MergeParams(mergedArgs.m_dcParams, pGesture->m_defaultPlayParams);
	}
	Gesture::MergeParams(mergedArgs.m_dcParams, args.m_dcParams);

	if (!mergedArgs.m_hProp.Valid() && (mergedArgs.m_dcParams.m_propName != INVALID_STRING_ID_64))
	{
		mergedArgs.m_hProp = m_pConfig->LookupPropByName(mergedArgs.m_dcParams.m_propName);
	}

	const char* pLockoutReason = nullptr;

	const Gesture::LayerIndex kBaseLayerGestureLayer = GetBaseLayerIndex();

	GesturePermit::Context ctx;
	ctx.m_pOwner	  = m_pOwner;
	ctx.m_priority	  = mergedArgs.m_dcParams.m_priority;
	ctx.m_ppOutReason = &pLockoutReason;

	if (!m_pPermit->AllowPlay(gesture, ctx))
	{
		Gesture::Err res = Gesture::Err(SID("locked-out")).WithLockoutReason(pLockoutReason);
		res.m_severity = Gesture::Err::Severity::kLow;
		return LogResult(res, "Play '", gesture);
	}

	const LimbLockBits bitsRaw = pGesture ? Gesture::GetLimbLockBitsForGesture(pGesture, m_pOwner) : 0;
	const LimbLockBits bits = args.m_flip ? FlipLimbLockBits(bitsRaw) : bitsRaw;

	LimbLockRequest request;
	request.m_limbs = bits;
	request.m_subsystem = SID("gesture");
	request.m_subsystemsToOverrule[0] = SID("gesture");

	bool canBeLimbLocked = true;
	if (mergedArgs.m_dcParams.m_priorityValid)
	{
		if (Gesture::Config::Get()->IsGamePriority(mergedArgs.m_dcParams.m_priority))
		{
			canBeLimbLocked = false;
		}
	}

	if (canBeLimbLocked)
	{
		if (ILimbManager* pLimbManager = m_pConfig->GetLimbManager())
		{
			LimbLockDebugInfo debugInfo;
			if (!pLimbManager->WouldGetLock(request, &debugInfo))
			{
				return LogResult(Gesture::Err(SID("limb-locked-out"))
									 .WithLockoutReason(DevKitOnly_StringIdToString(debugInfo.m_overrulingSubsystem)),
								 "Play '",
								 gesture);
			}
		}
	}

	Gesture::LayerIndex const n = ChooseLayer(&mergedArgs);
	const bool layerIsValid = n < GetNumLayers();

	const StringId64 prevGestureId = layerIsValid ? m_pLayer[n]->GetActiveGesture() : INVALID_STRING_ID_64;

	if (FALSE_IN_FINAL_BUILD(prevGestureId != INVALID_STRING_ID_64))
	{
		GESTURE_LOG("Pre-empting '%s", DevKitOnly_StringIdToString(prevGestureId));
	}

	const Gesture::Err result = layerIsValid ? m_pLayer[n]->PlayGesture(gesture, mergedArgs)
											 : Gesture::Err(SID("chose-invalid-layer"));

	if (result.Success())
	{
		if (ILimbManager* pLimbManager = m_pConfig->GetLimbManager())
		{
			m_limbLock[n] = pLimbManager->GetLock(request);
		}
	}

	return LogResult(result, "Play '", gesture);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Err GestureController::Clear(const Gesture::PlayArgs& args)
{
	Gesture::PlayArgs mergedArgs(args);
	mergedArgs.m_dcParams = Gesture::GetDefaultParams();
	Gesture::MergeParams(mergedArgs.m_dcParams, args.m_dcParams);

	AnimControl* pAnimControl = m_pOwner->GetAnimControl();
	if (!pAnimControl)
	{
		return LogResult(Gesture::Err(SID("no-anim-control")), "Clear", INVALID_STRING_ID_64);
	}

	const Gesture::LayerIndex n = (mergedArgs.m_gestureLayer < GetNumLayers())
						  ? mergedArgs.m_gestureLayer
						  : GetFirstRegularGestureLayerIndex();
	
	if (n == Gesture::kGestureLayerInvalid)
	{
		return LogResult(Gesture::Err(SID("no-layers-to-clear")), "Clear", INVALID_STRING_ID_64);
	}

	const StringId64 clearingGesture = IsFinalBuild() ? SID("<unavailable>") : m_pLayer[n]->GetActiveGesture();

	const Gesture::Err result = m_pLayer[n]->ClearGesture(mergedArgs);

	if (clearingGesture != INVALID_STRING_ID_64)
	{
		LogResult(result, "Clear '", clearingGesture);
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void LockOutGesture(const StringId64 gesture, IGestureLayer* pLayer, float blendOutTime = -1.0f)
{
	const DC::GestureDef* pGesture = Gesture::LookupGesture(gesture);
	float clearGestureTime = pGesture ? pGesture->m_lockedBlendOutTime : -1.0f;

	if (clearGestureTime < 0.0f)
	{
		clearGestureTime = blendOutTime;
	}

	if (clearGestureTime < 0.0f)
	{
		clearGestureTime = 0.2f;
	}

	Gesture::PlayArgs args;
	args.m_blendOutTime = clearGestureTime;

	// Call layer directly as we don't want this logged
	pLayer->ClearGesture(args);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureController::UpdateEnable()
{
	if (!m_enabled && m_enabledNext)
	{
		m_enabled = true;

		return;
	}

	if (m_enabled && !m_enabledNext)
	{
		AnimControl* pAnimControl = m_pOwner->GetAnimControl();

		bool anyLayerPlaying = false;

		const size_t numLayers = GetNumLayers();

		for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
		{
			if (GetLayerInfo(n).m_layerType != SID("regular"))
			{
				continue;
			}

			if (!m_pLayer[n]->IsPlayingGesture())
			{
				continue;
			}

			m_pLayer[n]->ClearGesture(Gesture::PlayArgs());

			anyLayerPlaying = true;
		}

		if (!anyLayerPlaying)
		{
			m_enabled = false;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureController::UpdateExternalBlendFactors()
{
	float const dt = m_pOwner->GetClock()->GetDeltaTimeInSeconds();

	const float kExternalBlendInTime = 0.2f;
	const float kExternalBlendInSpeed = 1.0f / kExternalBlendInTime;

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableLookAts))
	{
		m_disableLookAtThisFrame = true;
	}

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableAimAts))
	{
		m_disableAimThisFrame = true;
	}

	m_baseLookAtControlBlend = m_disableBaseLookAtThisFrame
								   ? Max(0.0f, m_baseLookAtControlBlend - dt * kExternalBlendInSpeed)
								   : Min(1.0f, m_baseLookAtControlBlend + dt * kExternalBlendInSpeed);
	m_disableBaseLookAtThisFrame = false;

	m_lookAtControlBlend = m_disableLookAtThisFrame ? Max(0.0f, m_lookAtControlBlend - dt * kExternalBlendInSpeed)
													: Min(1.0f, m_lookAtControlBlend + dt * kExternalBlendInSpeed);
	m_disableLookAtThisFrame = false;

	m_aimControlBlend = m_disableAimThisFrame ? Max(0.0f, m_aimControlBlend - dt * kExternalBlendInSpeed)
											  : Min(1.0f, m_aimControlBlend + dt * kExternalBlendInSpeed);
	m_disableAimThisFrame = false;

	ANIM_ASSERT(IsReasonable(m_baseLookAtControlBlend));
	ANIM_ASSERT(IsReasonable(m_lookAtControlBlend));
	ANIM_ASSERT(IsReasonable(m_aimControlBlend));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureController::Update()
{
	UpdateEnable();

	if (!m_enabled)
	{
		return;
	}
	const size_t numLayers = GetNumLayers();

	for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
	{
		if (GetLayerInfo(n).m_layerType != SID("regular"))
			continue;

		const StringId64 playingGesture = m_pLayer[n]->GetActiveGesture();

		if (playingGesture != INVALID_STRING_ID_64)
		{
			const char* pGestureLockoutReason = nullptr;

			const Gesture::LayerIndex kBaseLayerGestureLayer = GetBaseLayerIndex();

			GesturePermit::Context ctx;
			ctx.m_pOwner	   = m_pOwner;
			ctx.m_priority	   = m_pLayer[n]->GetPlayingGesturePriority();
			ctx.m_ppOutReason  = &pGestureLockoutReason;
			ctx.m_blendOutTime = m_pLayer[n]->GetCurrentBlendOutTime();

			const bool lockedOut = !m_pPermit->AllowContinue(playingGesture, ctx);

			if (lockedOut)
			{
				LockOutGesture(playingGesture, m_pLayer[n], ctx.m_lockOutBlend);

				GESTURE_LOG("In-progress Gesture '%s locked out (%s)",
							DevKitOnly_StringIdToString(playingGesture),
							pGestureLockoutReason ? pGestureLockoutReason : "no reason given");
			}
			else
			{
				// Attempt to renew the limb lock

				LimbLock& lock = m_limbLock[n];
				if (lock.IsValid())
				{
					if (ILimbManager* pLimbManager = m_pConfig->GetLimbManager())
					{
						LimbLockDebugInfo debugInfo;

						const bool lockHolds = pLimbManager->HoldLock(lock, &debugInfo);

						if (!lockHolds)
						{
							bool canBeLimbLocked = true;
							if (Gesture::Config::Get()->IsGamePriority(m_pLayer[n]->GetPlayingGesturePriority()))
							{
								canBeLimbLocked = false;
							}

							if (canBeLimbLocked)
							{
								LockOutGesture(playingGesture, m_pLayer[n]);

								GESTURE_LOG("In-progress Gesture '%s limb-locked out (%s)",
											DevKitOnly_StringIdToString(playingGesture),
											DevKitOnly_StringIdToString(debugInfo.m_overrulingSubsystem));
							}

							lock = LimbLock();
						}
					}
				}
			}
		}
	}

	UpdateExternalBlendFactors();

	bool allowPowerRagdoll = true;
	bool suppressBlinking = false;

	for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
	{
		m_pLayer[n]->Update();

		if (m_pLayer[n]->IsPlayingGesture())
		{
			if (allowPowerRagdoll && m_pLayer[n]->DisablePowerRagdoll())
			{
				allowPowerRagdoll = false;
			}

			if (!suppressBlinking && m_pLayer[n]->SuppressBlinking())
			{
				suppressBlinking = true;
			}
		}
	}

	if (suppressBlinking)
	{
		if (BlinkController* pBlinkController = m_pOwner->GetBlinkController())
		{
			pBlinkController->SuppressBlinking(Seconds(0.5f));
		}
	}

	if (m_allowPowerRagdoll != allowPowerRagdoll)
	{
		GESTURE_LOG("Allow Power Ragdoll: %s", TrueFalse(allowPowerRagdoll));
		
		m_allowPowerRagdoll = allowPowerRagdoll;

		if (Character* pOwnerChar = Character::FromProcess(m_pOwner))
		{
			pOwnerChar->SetPowerRagdollDisabled(!allowPowerRagdoll);
		}
	}

	UpdateFlagBlends();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DebugDrawPerGestureNode(const AnimStateSnapshot* pSnapshot,
									const AnimSnapshotNode* pNode,
									const AnimSnapshotNodeBlend* pParentBlendNode,
									float combinedBlend,
									uintptr_t userData)
{
	const NdGameObject* pOwner = (const NdGameObject*)userData;
	if (!pOwner)
		return true;

	const IGestureNode* pGestureNode = IGestureNode::FromAnimNode(pNode);
	if (!pGestureNode)
		return true;

	if ((combinedBlend > NDI_FLT_EPSILON) || g_animOptions.m_gestures.m_forceDrawAllNodes)
	{
		pGestureNode->DebugDraw(pOwner);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DebugDrawInstanceCallback(const AnimStateInstance* pInstance,
									  const AnimStateLayer* pStateLayer,
									  uintptr_t userData)
{
	const AnimStateSnapshot& snapshot = pInstance->GetAnimStateSnapshot();

	snapshot.VisitNodesOfKind(SID("IGestureNode"), DebugDrawPerGestureNode, userData);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureController::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (!DebugSelection::Get().IsProcessOrNoneSelected(m_pOwner))
	{
		return;
	}

	const AnimControl* pAnimControl = m_pOwner ? m_pOwner->GetAnimControl() : nullptr;
	const U32F numAnimLayers = pAnimControl ? pAnimControl->GetNumLayers() : 0;

	for (U32F iLayer = 0; iLayer < numAnimLayers; ++iLayer)
	{
		const AnimStateLayer* pStateLayer = pAnimControl->GetStateLayerByIndex(iLayer);
		if (!pStateLayer)
			continue;

		pStateLayer->WalkInstancesOldToNew(DebugDrawInstanceCallback, (uintptr_t)m_pOwner);
	}

	const size_t numLayers = GetNumLayers();

	for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
	{
		m_pLayer[n]->DebugDraw();
	}

	MsgPriorityJanitor jj(m_pOwner->GetProcessId());

	if (g_animOptions.m_gestures.m_debugDrawGestureTargets)
	{
		m_targetMgr.DebugDraw();
	}

	if (g_animOptions.m_gestures.m_debugGesturePermissions)
	{
		for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
		{
			if (GetLayerInfo(n).m_layerType != SID("regular"))
				continue;

			const StringId64 playingGesture = m_pLayer[n]->GetActiveGesture();

			MsgCon("%s -- Gesture layer %d is ", m_pOwner->GetName(), (int)n);

			if (playingGesture != INVALID_STRING_ID_64)
			{
				MsgCon("playing '%s @ pri %s\n",
					   DevKitOnly_StringIdToString(playingGesture),
					   DC::GetGesturePriorityName(m_pLayer[n]->GetPlayingGesturePriority()));
			}
			else
			{
				MsgCon("clear\n");
			}
		}

		MsgCon("%s -- Gestures ", m_pOwner->GetName());

		const char* pGestureLockoutReason = nullptr;

		const Gesture::LayerIndex kBaseLayerGestureLayer = GetBaseLayerIndex();

		Gesture::PlayPriority lockoutPriority = -1;

		GesturePermit::Context ctx;
		ctx.m_pOwner	  = m_pOwner;
		ctx.m_ppOutReason = &pGestureLockoutReason;
		ctx.m_pLockoutPriority = &lockoutPriority;

		const bool genericPlayWouldBeAllowed = m_pPermit->AllowPlay(INVALID_STRING_ID_64, ctx);

		if (genericPlayWouldBeAllowed)
		{
			MsgCon("unlocked\n");
		}
		else
		{
			MsgCon("locked (%s) @ priority %s\n",
				   pGestureLockoutReason ? pGestureLockoutReason : "no reason given",
				   DC::GetGesturePriorityName(lockoutPriority));
		}
	}

	if (g_animOptions.m_gestures.m_showGestureLogs && m_pGestureLog)
	{
		m_pGestureLog->DumpWithOffset(GetMsgOutput(kMsgCon), m_pGestureLog->GetSkipFirstLineOffset());

		MsgCon("% 8.2f: *** End %s gesture log ***\n",
			   floorf(m_pOwner->GetClock()->GetCurTime().ToSeconds()),
			   m_pOwner->GetName());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureController::PostAlignUpdate()
{
	if (!m_enabled)
	{
		return;
	}

	ScopedTempAllocator allocJanitor(FILE_LINE_FUNC);

	const U32F numLayers = GetNumLayers();

	const float masterLookBlend = Min(m_lookEnabledBlend, m_lookAtControlBlend);
	const float masterBaseLookBlend = Min(Min(m_baseLookEnabledBlend, m_baseLookAtControlBlend), masterLookBlend);
	const float masterAimBlend	= Min(m_aimEnabledBlend, m_aimControlBlend);
	const float feedbackEnableBlend = Limit01(1.0f - m_feedackDisableBlend);

	for (Gesture::LayerIndex i = 0; i < numLayers; ++i)
	{
		float lookBlendToUse = masterLookBlend;
		if (GetLayerInfo(i).m_layerType == SID("base"))
			lookBlendToUse = masterBaseLookBlend;

		m_pLayer[i]->PostAlignUpdate(lookBlendToUse, masterAimBlend, feedbackEnableBlend);
	}

	AnimControl* pAnimControl = m_pOwner ? m_pOwner->GetAnimControl() : nullptr;
	const U32F numAnimLayers = pAnimControl ? pAnimControl->GetNumLayers() : 0;

	// update any gesture nodes that might exist outside the canonical gesture layer
	for (U32F iLayer = 0; iLayer < numAnimLayers; ++iLayer)
	{
		AnimStateLayer* pStateLayer = pAnimControl->GetStateLayerByIndex(iLayer);
		if (!pStateLayer)
			continue;

		bool skip = false;
		for (Gesture::LayerIndex i = 0; i < numLayers; ++i)
		{
			if (m_pLayer[i]->GetAnimStateLayer() == pStateLayer)
			{
				skip = true;
				break;
			}
		}

		if (skip)
		{
			continue;
		}

		IGestureLayer::NodeUpdateParams params;
		params.m_pOwner		 = m_pOwner;
		params.m_deltaTime	 = m_pOwner->GetClock()->GetDeltaTimeInSeconds();
		params.m_pTargetMgr	 = &m_targetMgr;
		params.m_orphanBlend = pStateLayer->GetDesiredFade();
		params.m_enableLook	 = masterLookBlend;
		params.m_enableAim	 = masterAimBlend;

		pStateLayer->WalkInstancesOldToNew(IGestureLayer::PostAlignUpdateInstanceCallback, (uintptr_t)&params);
	}

	// do this *after* PostAlignUpdate() because that's where we tick our used target slots
	m_targetMgr.DeleteUnusedSlots();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GestureController::IsPlayingGesture(const Gesture::LayerIndex n) const
{
	if (n >= GetNumLayers())
		return false;

	return m_pLayer[n]->IsPlayingGesture();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::LayerIndex GestureController::FindLayerPlaying(const StringId64 gesture) const
{
	const size_t numLayers = GetNumLayers();

	for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
	{
		StringId64 sourceId = m_pLayer[n]->GetSourceId();
		StringId64 activeGesture = m_pLayer[n]->GetActiveGesture();
		if (sourceId == gesture || activeGesture == gesture)
		{
			return n;
		}
	}

	return Gesture::kGestureLayerInvalid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GestureController::GetWeaponIkBlend() const
{
	return m_weaponIkBlend;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct GestureFlagBlends
{
	float m_aiming	= 0.0f;
	float m_looking = 0.0f;

	float m_lookDisabled = 0.0f;
	float m_baseLookDisabled = 0.0f;
	float m_aimDisabled		 = 0.0f;
	float m_weaponIkDisabled = 0.0f;
	float m_feedbackDisabled = 0.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class GestureFlagsBlender : public AnimStateLayer::InstanceBlender<GestureFlagBlends>
{
	virtual GestureFlagBlends GetDefaultData() const { return GestureFlagBlends(); }

	static bool GetFlagsPerGestureNode(const AnimStateSnapshot* pSnapshot,
									   const AnimSnapshotNode* pNode,
									   const AnimSnapshotNodeBlend* pParentBlendNode,
									   float combinedBlend,
									   uintptr_t userData)
	{
		const IGestureNode* pGestureNode = IGestureNode::FromAnimNode(pNode);
		const DC::GestureDef* pDcDef = pGestureNode ? pGestureNode->GetDcDef() : nullptr;
		if (!pDcDef)
			return true;

		GestureFlagBlends* pFlags = (GestureFlagBlends*)userData;
		ANIM_ASSERT(pFlags);

		if (!pDcDef)
			return true;

		switch (pGestureNode->GetGestureTypeId().GetValue())
		{
		case SID_VAL("aim"):
			pFlags->m_aiming = Max(pFlags->m_aiming, combinedBlend);
			break;
		case SID_VAL("look"):
			pFlags->m_looking = Max(pFlags->m_looking, combinedBlend);
			break;
		}

		if (pDcDef->m_disablesLook)
		{
			pFlags->m_lookDisabled = Max(pFlags->m_lookDisabled, combinedBlend);
		}
		
		if (pDcDef->m_disablesBaseLook)
		{
			pFlags->m_baseLookDisabled = Max(pFlags->m_baseLookDisabled, combinedBlend);
		}

		if (pDcDef->m_disablesAim)
		{
			pFlags->m_aimDisabled = Max(pFlags->m_aimDisabled, combinedBlend);
		}

		if (pDcDef->m_disableFeedback)
		{
			pFlags->m_feedbackDisabled = Max(pFlags->m_feedbackDisabled, combinedBlend);
		}

		const Gesture::AlternativeIndex iAlternative = pGestureNode->GetCurrentAlternative();
		const DC::GestureAnims* pGestureAnims = Gesture::GetGestureAnims(pDcDef, iAlternative);

		if (pGestureAnims && pGestureAnims->m_disableWeaponIk)
		{
			pFlags->m_weaponIkDisabled = Max(pFlags->m_weaponIkDisabled, combinedBlend);
		}
	
		return true;
	}

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, GestureFlagBlends* pDataOut)
	{
		if (!pInstance || !pDataOut)
		{
			return false;
		}

		*pDataOut = GestureFlagBlends();

		const AnimStateSnapshot& snapshot = pInstance->GetAnimStateSnapshot();

		snapshot.VisitNodesOfKind(SID("IGestureNode"), GetFlagsPerGestureNode, (uintptr_t)pDataOut);
	
		return true;
	}

	virtual GestureFlagBlends BlendData(const GestureFlagBlends& leftData,
										const GestureFlagBlends& rightData,
										float masterFade,
										float animFade,
										float motionFade)
	{
		GestureFlagBlends blendedData;

		blendedData.m_aiming	   = Lerp(leftData.m_aiming, rightData.m_aiming, animFade);
		blendedData.m_looking	   = Lerp(leftData.m_looking, rightData.m_looking, animFade);
		blendedData.m_lookDisabled = Lerp(leftData.m_lookDisabled, rightData.m_lookDisabled, animFade);
		blendedData.m_baseLookDisabled = Lerp(leftData.m_baseLookDisabled, rightData.m_baseLookDisabled, animFade);
		blendedData.m_aimDisabled  = Lerp(leftData.m_aimDisabled, rightData.m_aimDisabled, animFade);
		blendedData.m_weaponIkDisabled = Lerp(leftData.m_weaponIkDisabled, rightData.m_weaponIkDisabled, animFade);
		blendedData.m_feedbackDisabled = Lerp(leftData.m_feedbackDisabled, rightData.m_feedbackDisabled, animFade);

		return blendedData;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureController::UpdateFlagBlends()
{
	GestureFlagBlends combinedBlends;

	const size_t numLayers = GetNumLayers();
	for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
	{
		const AnimStateLayer* pAnimLayer = m_pLayer[n]->GetAnimStateLayer();

		if (!pAnimLayer)
			continue;

		GestureFlagsBlender blender;
		GestureFlagBlends layerBlends = blender.BlendForward(pAnimLayer, GestureFlagBlends());

		const float fade = pAnimLayer->GetCurrentFade();

		combinedBlends.m_aiming		  = Max(combinedBlends.m_aiming, layerBlends.m_aiming * fade);
		combinedBlends.m_looking	  = Max(combinedBlends.m_looking, layerBlends.m_looking * fade);
		combinedBlends.m_lookDisabled = Max(combinedBlends.m_lookDisabled, layerBlends.m_lookDisabled * fade);
		combinedBlends.m_baseLookDisabled = Max(combinedBlends.m_baseLookDisabled, layerBlends.m_baseLookDisabled * fade);
		combinedBlends.m_aimDisabled  = Max(combinedBlends.m_aimDisabled, layerBlends.m_aimDisabled * fade);
		combinedBlends.m_weaponIkDisabled = Max(combinedBlends.m_weaponIkDisabled, layerBlends.m_weaponIkDisabled * fade);
		combinedBlends.m_feedbackDisabled = Max(combinedBlends.m_feedbackDisabled, layerBlends.m_feedbackDisabled * fade);
	}

	m_isAiming	= combinedBlends.m_aiming > 0.0f;
	m_isLooking = combinedBlends.m_looking > 0.0f;

	m_lookEnabledBlend	   = 1.0f - combinedBlends.m_lookDisabled;
	m_baseLookEnabledBlend = 1.0f - combinedBlends.m_baseLookDisabled;
	m_aimEnabledBlend	   = 1.0f - combinedBlends.m_aimDisabled;
	m_weaponIkBlend		   = 1.0f - combinedBlends.m_weaponIkDisabled;
	m_feedackDisableBlend  = combinedBlends.m_feedbackDisabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Err IGestureController::ClearGesture()
{
	const Gesture::LayerIndex n = GetFirstRegularGestureLayerIndex();

	return ClearGesture(n, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Err IGestureController::ClearGesture(Gesture::LayerIndex n, const Gesture::PlayArgs* args)
{
	Gesture::PlayArgs newArgs(args ? *args : Gesture::g_defaultPlayArgs);

	newArgs.m_gestureLayer = n;

	return Clear(newArgs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureController::ClearNonBaseGestures(const Gesture::PlayArgs* args)
{
	const size_t numLayers = GetNumLayers();
	for (U32F iLayer = 0; iLayer < numLayers; ++iLayer)
	{
		if (GetLayerInfo(iLayer).m_layerType != SID("base"))
		{
			ClearGesture(iLayer, args);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Err IGestureController::ClearLayerPlaying(const StringId64 gesture, const Gesture::PlayArgs& args)
{
	const Gesture::LayerIndex layer = FindLayerPlaying(gesture);

	if (layer != Gesture::kGestureLayerInvalid)
	{
		Gesture::PlayArgs clearArgs(args);
		clearArgs.m_gestureLayer = layer;

		return Clear(clearArgs);
	}

	return Gesture::Err();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Err IGestureController::PlayGesture(const StringId64 gesture,
											 const NdLocatableObject& target,
											 const Gesture::PlayArgs* args)
{
	Gesture::PlayArgs overlayArgs(args ? *args : Gesture::g_defaultPlayArgs);

	NDI_NEW(&overlayArgs.m_target) Gesture::TargetObject(target);
	overlayArgs.m_targetSupplied = true;

	return Play(gesture, overlayArgs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Err IGestureController::PlayGesture(const StringId64 gesture,
											 const Point_arg target,
											 const Gesture::PlayArgs* args)
{
	Gesture::PlayArgs overlayArgs(args ? *args : Gesture::g_defaultPlayArgs);

	NDI_NEW(&overlayArgs.m_target) Gesture::TargetPoint(target);
	overlayArgs.m_targetSupplied = true;

	return Play(gesture, overlayArgs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::LayerIndex IGestureController::GetFirstRegularGestureLayerIndex() const
{
	const size_t numLayers = GetNumLayers();
	for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
	{
		if (GetLayerInfo(n).m_layerType == SID("regular"))
		{
			return n;
		}
	}

	return Gesture::kGestureLayerInvalid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::LayerIndex IGestureController::GetBaseLayerIndex() const
{
	const size_t numLayers = GetNumLayers();
	for (Gesture::LayerIndex n = 0; n < numLayers; ++n)
	{
		if (GetLayerInfo(n).m_layerType == SID("base"))
		{
			return n;
		}
	}

	return Gesture::kGestureLayerInvalid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ListArray<Gesture::LayerSpec>* Gesture::ControllerConfig::GetDefaultLayerSpecs()
{
	ListArray<LayerSpec>* pRet = NDI_NEW ListArray<LayerSpec>(2);

	{
		LayerSpec baseLayer;
		baseLayer.m_layerType = SID("base");
		pRet->PushBack(baseLayer);
	}

	{
		LayerSpec regularLayer;
		regularLayer.m_layerType = SID("regular");
		pRet->PushBack(regularLayer);
	}

	return pRet;
}


/// --------------------------------------------------------------------------------------------------------------- ///
float GestureController::GetGesturePhase(StringId64 gestureId, const Gesture::LayerIndex n) const
{
	Gesture::LayerIndex foundLayer = n;
	if (foundLayer == Gesture::kGestureLayerInvalid)
	{
		foundLayer = FindLayerPlaying(gestureId);
	}
	
	const size_t numLayers = GetNumLayers();

	if (foundLayer < 0 || foundLayer >= numLayers || !m_pLayer[foundLayer])
	{
		return -1.0f;
	}

	const AnimStateLayer* pStateLayer = m_pLayer[foundLayer]->GetAnimStateLayer();

	const float phase = Gesture::GetGesturePhase(pStateLayer, gestureId);

	return phase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Gesture::ControllerConfig::NewBaseLayerAlternativeRequested(const DC::BlendParams* pBlend)
{
	NdGameObject* pOwner	   = GetOwner();
	AnimControl* pAnimControl  = pOwner ? pOwner->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const AnimStateInstance* pCurrentInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

#if 1
	if (pCurrentInstance && !pBaseLayer->AreTransitionsPending())
	{
		FadeToStateParams params;
		params.m_stateStartPhase	  = pCurrentInstance->GetPhase();
		params.m_preserveOverlays	  = true;
		params.m_preserveInstanceInfo = true;

		if (pBlend)
		{
			params.m_animFadeTime	= pBlend->m_animFadeTime;
			params.m_motionFadeTime = pBlend->m_motionFadeTime;
			params.m_blendType		= pBlend->m_curve;
		}

		pBaseLayer->FadeToState(pCurrentInstance->GetStateName(), params);
	}
#else
	if (pBaseLayer && pBaseLayer->IsTransitionValid(SID("gesture-alt")))
	{
		FadeToStateParams params;

		if (pBlend)
		{
			params.m_animFadeTime	= pBlend->m_animFadeTime;
			params.m_motionFadeTime = pBlend->m_motionFadeTime;
			params.m_blendType		= pBlend->m_curve;
		}

		pBaseLayer->RequestTransition(SID("gesture-alt"), &params);
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* Gesture::ControllerConfig::LookupPropByName(StringId64 propId) const
{
	return NdGameObject::LookupGameObjectByUniqueId(propId);
}
