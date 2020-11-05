/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/gameplay/simple-bird.h"
#include "gamelib/gameplay/animal-behavior/animal-base.h"
#include "gamelib/gameplay/flocking/flocking-object.h"
#include "gamelib/ndphys/havok-collision-cast.h"

#include "corelib/util/random.h"
#include "corelib/util/bigsort.h"

#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/nd-anim-align-util.h"
#include "ndlib/util/finite-state-machine.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/script/script-callback.h"

#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/state-script/ss-animate.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/spline/catmull-rom.h"

/// --------------------------------------------------------------------------------------------------------------- ///
SimpleBirdDebugOptions g_simpleBirdDebugOptions;

//--------------------------------------------------------------------//
class SimpleBird : public NdAnimalBase
{
	typedef NdAnimalBase ParentClass;

public:

	FROM_PROCESS_DECLARE(SimpleBird);

	virtual void GoInitialState() override { GoActive(); }
	virtual U32F GetMaxStateAllocSize() override;

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual void EventHandler(Event& event) override;
	virtual void PostAnimUpdate_Async() override;

	virtual void DebugDrawAnimal(ScreenSpaceTextPrinter* pPrinter) const override;
	const DC::AnimalBehaviorSettings* GetAnimalBehaviorSettings() const;

	virtual Err SetupAnimControl(AnimControl* pAnimControl) override
	{
		m_numAnimateLayers = 1;
		AnimControl* pMasterAnimControl = GetAnimControl();
		FgAnimData& animData = pMasterAnimControl->GetAnimData();
		const I32 foundAnimations = ResourceTable::GetNumAnimationsForSkelId(animData.m_curSkelHandle.ToArtItem()->m_skelId);
		const U32F numNeededSimpleLayerInstances = Max(((foundAnimations <= 1) ? 1u : 2u), (U32)m_numSimpleInstancesNeeded);

		pAnimControl->AllocateSimpleLayer(SID("base"), ndanim::kBlendSlerp, 0, numNeededSimpleLayerInstances);

		char layerNameBuf[32];
		for (U32F i = 0; i < m_numAnimateLayers; ++i)
		{
			sprintf(layerNameBuf, "partial-slerp-%d", i);
			pAnimControl->AllocateSimpleLayer(StringToStringId64(layerNameBuf), ndanim::kBlendSlerp, 2, 1);
		}

		AnimLayer* pBaseLayer = pAnimControl->CreateSimpleLayer(SID("base"));
		pBaseLayer->Fade(1.0, 0.0f);

		return Err::kOK;
	}

	static constexpr float kMaxLandingDist = 3.f;

private:

	virtual void PostUpdate() override
	{
		UpdateEnableAnimation();

		// update velocity
		Point currPosWs = GetTranslation();
		float dt = GetClock()->GetDeltaTimeInSeconds();
		if (dt > SCALAR_LC(0.0f))
		{
			const Vector deltaPosWs = currPosWs - m_myPrevPosWs;
			m_currVel = deltaPosWs / dt;
		}

		m_myPrevPosWs = currPosWs;
	}

	static StringId64 GetTakeOffAnimId(Vector_arg currFacing, Vector_arg flyDirection, bool isPanic);
	static DC::BirdFlyAnim GetFlyAnimId(StringId64 skelSizeId, Point_arg currPos, Point_arg endPos, StringId64 prevAnim, bool isPanic);

	struct FlyPath
	{
	public:
		FlyPath()
		{
			Clear();
		}

		void Init()
		{
			Clear();

			m_allocator.InitAsHeap(NDI_NEW U8[kPathMemorySize], kPathMemorySize);
		}

		void Clear()
		{
			m_pPath = nullptr;
			m_valid = false;
		}

		void CreatePath(const ListArray<Locator>& waypoints)
		{
			AllocateJanitor jj(&m_allocator, FILE_LINE_FUNC);

			ASSERTF(!m_valid, ("make sure free path before create a new path!"));
			m_pPath = CatmullRomBuilder::CreateCatmullRom(waypoints);
			m_valid = true;
		}

		void ClearPath()
		{
			// reset path allocator so the memory can be reused.
			m_allocator.Clear();
			Clear();
		}

		bool IsValid() const { return m_valid; }
		const CatmullRom& GetPath() const { GAMEPLAY_ASSERT(m_valid); return *m_pPath; }

		void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
		{
			m_allocator.Relocate(deltaPos, lowerBound, upperBound);
			DeepRelocatePointer(m_pPath, deltaPos, lowerBound, upperBound);
		}

		void DebugDraw() const
		{
			if (IsValid())
			{
				CatmullRom::DrawOptions options;
				options.m_drawName = false;
				options.m_drawPointId = true;

				m_pPath->Draw(&options);
			}
		}

	private:
		static const U32 kPathMemorySize = 4 * 1024;
		HeapAllocator m_allocator;

		CatmullRom* m_pPath;
		bool m_valid;
	};
	FlyPath m_flyPath;

	static float GetDesiredPitchAngleRad(float totalFlyDist, float currFlyDist, float currSpeed, Point_arg currPos, Point_arg startPos, Point_arg endPos);

	void PlayAnim(StringId64 animId, StringId64 upAnimId, bool forceLoop, float fadeInTime = 0.2f, float fadeOutTime = 0.2f);

	class BaseState : public NdDrawableObject::Active
	{
		typedef NdDrawableObject::Active ParentClass;
	protected:
		BoundFrame m_initLocation;
		TimeFrame m_startTime;

	public:
		BIND_TO_PROCESS(SimpleBird);
		virtual void Enter() override;
		virtual void Update() override
		{
			SimpleBird& self = Self();
			self.UpdateThreats();
		}
		virtual void PostAnimUpdate() {}

		virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const {}
	};

	class MoveState : public BaseState
	{
		typedef BaseState ParentClass;
	protected:
		bool m_initialized = false;
		bool m_isDone = false; // indicate flight is done.

		bool m_isAvoidThreat = false;

		float m_currSpeed = 0.f;
		SpringTracker<float> m_speedTracker;

		float m_currT; // current t on spline path.

		bool m_targetLocValid = false;
		Locator m_targetLoc;

		float m_currPitchAngleRad = 0.f;
		SpringTracker<float> m_pitchTracker;

		bool m_useBigThreatRange = false;
		float m_extraHeight = 0.f; // to avoid human-threat/collision, fly extra meter above path.
		SpringTracker<float> m_extraHeightTracker;

		NdLocatableObjectHandle m_hInitThreat;

		bool m_useRaycast = false;
		bool m_raycastDone = false;

		// collision phase1:
		I64 m_raycastKickedFN1 = -1;
		bool m_raycastGathered1 = false;
		HavokRayCastJob m_phase1Job;

		// collision phase2:
		I64 m_raycastKickedFN2 = -1;
		bool m_raycastGathered2 = false;
		HavokSphereCastJob m_phase2Job;

		bool m_avoidCollPosValid = false;
		Point m_avoidCollPos = kOrigin;

		struct FlyAnim
		{
			FlyAnim() { memset(this, 0, sizeof(FlyAnim)); }

			StringId64 m_animId;
			StringId64 m_upAnimId;
			float m_duration;
			float m_elapsedTime;
			float m_desiredSpeed;
			float m_speedSpring;
			Vector m_velocity = kZero;
			//bool m_startSlowdown = false;
		} m_flyAnim;

		void StartFlyAnim(SimpleBird& self, const DC::BirdFlyAnim& anim);
		void UpdateFlyAnim(SimpleBird& self, float dt, const Locator& targetLoc, float currFlyDist, float totalFlyDist);

		struct TakeOffAnim
		{
			TakeOffAnim() {}

			bool m_valid = false;
			bool m_isPlaying = false;
			bool m_isDone = false;
			StringId64 m_animId = INVALID_STRING_ID_64;
			float m_endPhase = -1.f;
			Point m_endPos = kOrigin;
			TimeFrame m_endTime;
			float m_procTurningTime = 0.f; // procedural turning time
			float m_procTurningMaxTime = 0.5f;
			Vector m_velocity = kZero;
			Locator m_idealStartLoc = kIdentity;
			Locator m_actualStartLoc = kIdentity;
		} m_takeoffAnim;

		bool NeedTakeOffAnim(const SimpleBird& self) const;
		Locator PlayTakeOffAnim(SimpleBird& self, Point_arg targetPos, bool isPanic);

		struct LandingAnim
		{
			LandingAnim() {}

			bool m_valid = false; // does bird need play landing anim?
			bool m_isPlaying = false; // is playing landing anim?
			bool m_shouldPlay = false;
			StringId64 m_animId = INVALID_STRING_ID_64;
			float m_distToDest = 0.f;
			Locator m_idealStartLoc = kIdentity;
			Locator m_actualStartLoc = kIdentity;
			//float m_animStartSpeed = 0.f;
			TimeFrame m_startTime;
		};
		bool m_needLandingAnim = false;
		bool m_landingAnimPrepared = false;
		LandingAnim m_landingAnim;

		void PrepareLandingAnim(const SimpleBird& self, StringId64 skelSizeId, float currSpeed, const Locator& targetLoc);
		void PlayLandingAnim(SimpleBird& self);

		struct SimulateFlyRes
		{
			void Clear() { m_valid = false; }

			bool m_valid = false;
			Point m_rawPos = kOrigin;
			Point m_adjustedPos = kOrigin;
			Vector m_facingXz = kUnitZAxis;
			float m_desiredPitchRad = 0.0f;
			float m_nextT = 0.0f;
		};
		SimulateFlyRes m_flyRes;

		static void CalcNextFrame(const FlyPath& flyPath,
			Point_arg currPos, const BoundFrame& startBf, const BoundFrame& targetBf,
			const float currFlyDist, const float totalFlyDist,
			float currT, float currSpeed, float dt, SimulateFlyRes* outRes);

		void UpdateExtraHeight(SimpleBird& self, Point_arg newPos, float distToDest, float totalFlyDist, float dt);

		float m_upLayerBlend = 0.f;
		SpringTracker<float> m_upLayerBlendTracker;

		void PostAnimUpdateTakeoff(SimpleBird& self, float dt);
		void PostAnimUpdateLanding(SimpleBird& self, float dt);
		void PostAnimUpdateFlying(SimpleBird& self, float dt);

	public:
		BIND_TO_PROCESS(SimpleBird);

		virtual void Enter() override
		{
			ParentClass::Enter();
			m_initialized = false;
		}
		Maybe<Locator> GetTargetLoc() const 
		{ 
			if (m_targetLocValid)
				return m_targetLoc;
			else
				return MAYBE::kNothing; 
		}
		virtual bool AllowChangePerchOnThreat() const 
		{ 
			// it looks bad when taking off is interrupted.
			return !m_takeoffAnim.m_isPlaying;
		}

		void PrepareFly(const Locator& targetLoc, bool isPanic, bool needLandingAnim, NdLocatableObjectHandle hInitThreat = NdLocatableObjectHandle());
		void BeginFlyTo();

		void UpdateFlyTo();
		bool IsFlyToDone() const { return m_isDone; }
		virtual void PostAnimUpdate() override;
		Locator GetRootAnimDelta(SimpleBird& self, const float phase0, const float phase1) const;

		virtual bool IsAvoidThreat() const { return false; }

		void KickRaycastPhase1(Point_arg startPos, Point_arg targetPos);
		void GatherRaycastPhase1();

		void KickRaycastPhase2(Point_arg testPos);
		void GatherRaycastPhase2();

		virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;
	};

	STATE_DECLARE_OVERRIDE(Active);
	STATE_DECLARE(FlyToPerch);
	STATE_DECLARE(ChillAndWait);
	STATE_DECLARE(WaitTimer);
	//STATE_DECLARE(AvoidThreat);
	STATE_DECLARE(AvoidDanger);
	//STATE_DECLARE(FlyAround);  // for t2: we simply kill birds after gunshot.

	bool IsInAvoidThreat() const;

private:
	MutableProcessHandle m_hPerchMgr;

	struct WaitTimerParams
	{
		WaitTimerParams() { Clear(); }
		void Clear()
		{
			m_nextStateId = INVALID_STRING_ID_64;
			m_timer = 0.f;
		}

		StringId64 m_nextStateId;
		float m_timer;
	} m_waitTimerParams;

	void GoWaitTimerState(StringId64 nextStateId, float timer)
	{
		ASSERT(nextStateId != INVALID_STRING_ID_64);
		m_waitTimerParams.m_nextStateId = nextStateId;
		m_waitTimerParams.m_timer = timer;
		GoWaitTimer();
		//TakeStateTransition();
	}

	struct FlyToPerchParams
	{
		bool m_preferPerchLoc = false;
		bool m_avoidThreat = false;
		bool m_useRaycast = false;
		NdLocatableObjectHandle m_hInitThreat;
	} m_flyToPerchParams;

	void GoFlyToPerchState(bool preferPerchLoc, bool avoidThreat, bool useRaycast = false, NdLocatableObjectHandle hInitThreat = NdLocatableObjectHandle())
	{
		m_flyToPerchParams.m_preferPerchLoc = preferPerchLoc;
		m_flyToPerchParams.m_avoidThreat = avoidThreat;
		m_flyToPerchParams.m_useRaycast = useRaycast;
		m_flyToPerchParams.m_hInitThreat = hInitThreat;
		GoFlyToPerch();
	}

	//-----------------------------------------------------------------------------------------//
	// looks like animating is most expensive part of birds,
	// so i need enable/disable animation
	//-----------------------------------------------------------------------------------------//
	void UpdateEnableAnimation();

	//-----------------------------------------------------------------------------------------//
	// human threats
	//-----------------------------------------------------------------------------------------//
	struct Threat
	{
		Threat() {}
		Threat(NdLocatableObjectHandle handle, TimeFrame activeTime)
			: m_handle(handle)
			, m_activeTime(activeTime)
		{}

		NdLocatableObjectHandle m_handle;
		TimeFrame m_activeTime;
	};
	static const U32F kMaxNumThreats = 16;
	ListArray<Threat> m_threats;

	void UpdateThreats();
	void OnThreat(NdLocatableObjectHandle hThreatObj, const DC::EllipseRange* pThreatRange);
	Threat* FindThreat(NdLocatableObjectHandle hThreat);
	const Threat* FindDangerousThreat(Point_arg selfPos) const;

	//-----------------------------------------------------------------------------------------//
	// danger: gun-fire, etc.
	//-----------------------------------------------------------------------------------------//
	struct Danger
	{
		Danger()
			: m_active(false)
		{}

		bool IsActive() const { return m_active; }
		void ClearDanger() { m_active = false; }
		void AddDanger(Point_arg escapePos) { m_escapePosWs = escapePos; m_active = true; }

		bool m_active;
		Point m_escapePosWs;
	} m_danger;

	// data.
	float m_idleTimeMin;
	float m_idleTimeMax;
	StringId64 m_behaviorSettingsId;
	ScriptPointer<DC::AnimalBehaviorSettings> m_pBehaviorSettings;

	StringId64 m_skelSizeId;

	// idle anims:
	StringId64 m_idleAnimListShared;
	StringId64 m_idleAnimListPerch;
	StringId64 m_idleAnimListGround;
	ScriptPointer<DC::AnimalIdleAnimList> m_pIdleAnimListShared;
	ScriptPointer<DC::AnimalIdleAnimList> m_pIdleAnimListPerch;
	ScriptPointer<DC::AnimalIdleAnimList> m_pIdleAnimListGround;

	void GetIdleAnimList(ListArray<StringId64>& outIdleAnims, bool includePerch, bool includeGround) const;

	FlockingCtrl m_flockingCtrl;

	Maybe<Point> m_lastPerchDest;
	PerchLocationResult::Type m_lastPerchLocType;

	Point m_myPrevPosWs;
	Vector m_currVel; // instead of using GetVelocityWs(), that funcs requires birds keed animating, but we are enable/disable animating.

	RingQueue<Locator> m_flyToHistory;
};

PROCESS_DECLARE(SimpleBird);


/// --------------------------------------------------------------------------------------------------------------- ///
Err SimpleBird::Init(const ProcessSpawnInfo& spawn)
{
	Err res = ParentClass::Init(spawn);
	if (res != Err::kOK)
		return res;

	m_hPerchMgr = MutableProcessHandle();

	m_idleTimeMin = 0.0f;
	m_idleTimeMax = 22.0f;

	m_lastPerchLocType = PerchLocationResult::Type::kNone;

	m_behaviorSettingsId = INVALID_STRING_ID_64;

	m_waitTimerParams.Clear();

	m_allowDisableUpdates = false;
	SetUpdateEnabled(true); // is it necessary?
	SetAllowThreadedUpdate(true);

	StringId64 flockingSettingsId = spawn.GetData<StringId64>(SID("flocking-settings"), SID("*flocking-default*"));
	m_flockingCtrl.Init(flockingSettingsId);

	m_threats.Init(kMaxNumThreats, FILE_LINE_FUNC);

	bool found = false;
	m_skelSizeId = SID("medium");

	m_idleAnimListShared = INVALID_STRING_ID_64;
	m_idleAnimListPerch = INVALID_STRING_ID_64;
	m_idleAnimListGround = INVALID_STRING_ID_64;
	{
		const FgAnimData& animData = GetAnimControl()->GetAnimData();
		const DC::AnimalSizeAndAnimMapping* pMapping = ScriptManager::LookupInModule<DC::AnimalSizeAndAnimMapping>(SID("*bird-size-anim-mapping*"), SID("animal-behavior"));
		const ArtItemSkeleton* pSkel = animData.m_curSkelHandle.ToArtItem();
		for (I32 ii = 0; ii < pMapping->m_count; ii++)
		{
			if (StringToStringId64(pSkel->GetName()) == pMapping->m_array[ii].m_size)
			{
				m_skelSizeId = pMapping->m_array[ii].m_skel;
				m_idleAnimListShared = pMapping->m_array[ii].m_animListShared;
				m_idleAnimListPerch = pMapping->m_array[ii].m_animListPerch;
				m_idleAnimListGround = pMapping->m_array[ii].m_animListGround;
				found = true;
				break;
			}
		}
	}
	ASSERT(found);
	m_pIdleAnimListShared = ScriptPointer<DC::AnimalIdleAnimList>(m_idleAnimListShared, SID("animal-behavior"));
	m_pIdleAnimListPerch = ScriptPointer<DC::AnimalIdleAnimList>(m_idleAnimListPerch, SID("animal-behavior"));
	m_pIdleAnimListGround = ScriptPointer<DC::AnimalIdleAnimList>(m_idleAnimListGround, SID("animal-behavior"));

	// initialize path allocator.
	m_flyPath.Init();

	m_currVel = kZero;
	m_myPrevPosWs = GetTranslation();

	// initialize fly-to history
	{
		Locator* pLocBuffer = NDI_NEW Locator[4];
		m_flyToHistory.Init(pLocBuffer, 4);
	}

	// move birds to character bucket for performance.
	ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pCharacterTree);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	m_threats.Relocate(deltaPos, lowerBound, upperBound);
	m_flyPath.Relocate(deltaPos, lowerBound, upperBound);
	m_flyToHistory.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool InThreatRange(Point_arg selfPos, Point_arg threatPos, const DC::EllipseRange& range)
{
	float distXzSqr = DistXzSqr(selfPos, threatPos);
	float distY = (float)(selfPos.Y() - threatPos.Y());
	return distXzSqr / Sqr(range.m_radiusXz) + Sqr(distY / range.m_halfHeight) <= 1.f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Maybe<float> CalcThreatHeight(Point_arg selfPos, Point_arg threatPos, const DC::EllipseRange& range)
{
	float distXzSqr = DistXzSqr(selfPos, threatPos);
	const float t0 = 1.f - (distXzSqr / Sqr(range.m_radiusXz));
	if (t0 > 0.f)
	{
		float desiredHeight = Sqrt(t0) * range.m_halfHeight;
		return desiredHeight;
	}

	return MAYBE::kNothing;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::EventHandler(Event& event)
{
	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("set-behavior-settings-id"):
		{
			m_behaviorSettingsId = event.Get(0).GetStringId();
			m_pBehaviorSettings = ScriptPointer<DC::AnimalBehaviorSettings>(m_behaviorSettingsId, SID("animal-behavior"));
		}
		break;

	case SID_VAL("set-perch-manager"):
		{
			m_hPerchMgr = EngineComponents::GetProcessMgr()->LookupProcessByUserId(event.Get(0).GetStringId());
			m_idleTimeMin = event.Get(1).GetFloat();
			m_idleTimeMax = event.Get(2).GetFloat();
			m_lastPerchLocType = (PerchLocationResult::Type)event.Get(3).GetI32();
		}
		break;

	case SID_VAL("avoid-threat"):
		{
			// check threat is in range.
			NdLocatableObjectHandle hThreatObj = NdLocatableObject::FromProcess(event.Get(0).GetProcess());
			if (hThreatObj.HandleValid())
			{
				const NdLocatableSnapshot* pThreatSnapshot = hThreatObj.ToSnapshot<NdLocatableSnapshot>();

				const float radiusArg = event.Get(1).GetAsF32();
				if (radiusArg >= 0.0f)
				{
					// if radius is specified then treat as generic sphere blocker
					const float dist = Length(GetTranslation() - pThreatSnapshot->GetTranslation());
					if (dist < radiusArg)
						OnThreat(hThreatObj, nullptr);
				}
				else
				{
					const DC::AnimalBehaviorSettings* pSettings = GetAnimalBehaviorSettings();

					const bool isThreatPlayer = pThreatSnapshot->IsProcessKindOf(SID("Player"));
					const DC::EllipseRange threatRange = isThreatPlayer ? pSettings->m_playerThreatEllipse : pSettings->m_npcThreatEllipse;
					if (InThreatRange(GetTranslation(), pThreatSnapshot->GetTranslation(), threatRange))
						OnThreat(hThreatObj, &threatRange);
				}				
			}
		}
		break;

	case SID_VAL("avoid-danger"):
		{
			m_danger.AddDanger(event.Get(0).GetPoint());
			float waitTime = RandomFloatRange(0.0f, 1.0f);
			GoWaitTimerState(SID("AvoidDanger"), waitTime);
			event.SetResponse(true);
		}
		break;

	case SID_VAL("danger-cleared"):
		m_danger.ClearDanger();
		event.SetResponse(true);
		break;
	}

	ParentClass::EventHandler(event);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::PostAnimUpdate_Async()
{
	ParentClass::PostAnimUpdate_Async();

	BaseState* pState = static_cast<BaseState*>(GetState());
	if (pState != nullptr)
	{
		pState->PostAnimUpdate();
	}

	if (m_flockingCtrl.IsEnabled())
	{
		const DC::AnimalBehaviorSettings* pSettings = GetAnimalBehaviorSettings();
		m_flockingCtrl.CalculateAccelAndVelocity(this, pSettings);
		AdjustTranslation(m_flockingCtrl.GetVelocity() * GetProcessDeltaTime());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::DebugDrawAnimal(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	if (g_simpleBirdDebugOptions.m_showName)
	{
		pPrinter->PrintText(kColorYellow, "%s, pid:%d", GetName(), GetProcessId());
	}

	if (g_simpleBirdDebugOptions.m_showStateName)
	{
		pPrinter->PrintText(kColorCyan, "State: %s", GetStateName());

		if (IsInAvoidThreat())
		{
			pPrinter->PrintText(kColorRed, "Avoid Threat");
		}
	}

	if (g_simpleBirdDebugOptions.m_showAnimName)
	{
		const AnimSimpleLayer* pBaseLayer = GetAnimControl()->GetSimpleLayerById(SID("base"));
		const AnimSimpleInstance* pCurrInst = pBaseLayer ? pBaseLayer->CurrentInstance() : nullptr;
		const StringId64 currAnimId = pCurrInst ? pCurrInst->GetAnimId() : INVALID_STRING_ID_64;
		const float currPhase = pCurrInst ? pCurrInst->GetPhase() : -1.f;
		pPrinter->PrintText(kColorCyan, "Anim: %s, %.3f", DevKitOnly_StringIdToString(currAnimId), currPhase);
	}

	if (g_simpleBirdDebugOptions.m_showPitchAngle)
	{
		const Vector myFwd = GetLocalZ(GetRotation());
		const float pitchDeg = RADIANS_TO_DEGREES(SafeAsin(myFwd.Y()));
		pPrinter->PrintText(kColorCyan, "pitch: %.3f", pitchDeg);
	}

	if (g_simpleBirdDebugOptions.m_showThreats)
	{
		pPrinter->PrintText(kColorRed, "Number of Threats: %d", m_threats.Size());

		const Point selfPos = GetTranslation();

		for (U32 ii = 0; ii < m_threats.Size(); ii++)
		{
			if (m_threats[ii].m_handle.HandleValid())
			{
				const NdLocatableSnapshot* pThreatSnapshot = m_threats[ii].m_handle.ToSnapshot<NdLocatableSnapshot>();
				g_prim.Draw(DebugLine(selfPos, pThreatSnapshot->GetTranslation(), kColorRed));
			}
		}
	}

	// each state debug draw.
	const BaseState* pState = static_cast<const BaseState*>(GetState());
	pState->DebugDraw(pPrinter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimalBehaviorSettings* SimpleBird::GetAnimalBehaviorSettings() const
{
	ASSERT(m_behaviorSettingsId != INVALID_STRING_ID_64);
	const DC::AnimalBehaviorSettings* pDef = m_pBehaviorSettings.GetTyped();
	GAMEPLAY_ASSERT(ScriptManager::TypeOf(pDef) == SID("animal-behavior-settings"));
	return pDef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::UpdateEnableAnimation()
{
	// use game camera if possible? probably not, because camera update is very late in a frame.
	const RenderCamera& cam = GetRenderCamera(0);
	if (cam.IsSphereInFrustum(Sphere(GetTranslation(), 3.f)))
	{
		// it doesn't work well right now.
		//EnableAnimation();
	}
	else
	{
		//DisableAnimation();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Threats
/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::UpdateThreats()
{
	// clean up old threats
	for (ListArray<Threat>::Iterator ii = m_threats.Begin(); ii != m_threats.End(); ++ii)
	{
		bool cleanup = false;

		Threat& threat = *ii;
		if (!threat.m_handle.HandleValid())
		{
			cleanup = true;
		}
		else
		{
			if (GetCurTime() - threat.m_activeTime > Seconds(10.f))
				cleanup = true;
		}

		if (cleanup)
		{
			m_threats.Erase(ii);
			--ii;
		}
	}

}

/// --------------------------------------------------------------------------------------------------------------- ///
SimpleBird::Threat* SimpleBird::FindThreat(NdLocatableObjectHandle hThreat)
{
	for (U32 ii = 0; ii < m_threats.Size(); ii++)
	{
		if (m_threats[ii].m_handle == hThreat)
			return &m_threats[ii];
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SimpleBird::Threat* SimpleBird::FindDangerousThreat(Point_arg selfPos) const
{
	struct Cost
	{
		TimeFrame m_activeTime;
		float m_distSqr;
		I32 m_index;

		static I32 Compare(const Cost& a, const Cost& b)
		{
			if (a.m_activeTime > b.m_activeTime)
				return -1;
			if (a.m_activeTime < b.m_activeTime)
				return +1;
			if (a.m_distSqr < b.m_distSqr)
				return -1;
			if (a.m_distSqr > b.m_distSqr)
				return +1;
			return 0;
		}
	};

	Cost threatCosts[kMaxNumThreats];
	memset(threatCosts, 0, sizeof(threatCosts));

	I32 numCosts = 0;

	for (U32 ii = 0; ii < m_threats.Size(); ii++)
	{
		if (m_threats[ii].m_handle.HandleValid())
		{
			float distSqr = DistSqr(m_threats[ii].m_handle.ToProcess()->GetTranslation(), selfPos);

			Cost newCost;
			newCost.m_distSqr = distSqr;
			newCost.m_activeTime = m_threats[ii].m_activeTime;
			newCost.m_index = ii;
			threatCosts[numCosts++] = newCost;
		}
	}

	if (numCosts == 0)
		return nullptr;

	QuickSort(threatCosts, numCosts, Cost::Compare);
	return &m_threats[threatCosts[0].m_index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::OnThreat(NdLocatableObjectHandle hThreatObj, const DC::EllipseRange* pThreatRange)
{
	if (!hThreatObj.HandleValid())
		return;

	if (m_threats.IsFull())
		return;

	Threat* pExistThreat = FindThreat(hThreatObj);
	if (pExistThreat)
	{
		pExistThreat->m_activeTime = GetCurTime();
	}
	else
	{
		Threat newThreat(hThreatObj, GetCurTime());
		m_threats.PushBack(newThreat);
	}

	const NdLocatableSnapshot* pThreatSnapshot = hThreatObj.ToSnapshot<NdLocatableSnapshot>();
	const Point threatPos = pThreatSnapshot->GetTranslation();

	if (IsState(SID("WaitTimer")))
	{
		// TODO: we may want to change direction.
		if (m_waitTimerParams.m_nextStateId == SID("AvoidThreat"))
			return;
	}
	else if (IsInAvoidThreat())
	{
		// TODO: we may want to change direction.
		return;
	}
	else if (IsState(SID("AvoidDanger")))
	{
		// AvoidDanger is higher priority than AvoidThreat.
		return;
	}
	else if (IsState(SID("FlyToPerch")))
	{
		if (pThreatRange != nullptr)
		{
			// check if destination is safe. If not, fly away.
			const MoveState* pMoveState = static_cast<const MoveState*>(GetState());
			if (pMoveState->AllowChangePerchOnThreat())
			{
				Maybe<Locator> targetLoc = pMoveState->GetTargetLoc();
				if (targetLoc.Valid())
				{
					const Point targetPos = targetLoc.Get().GetTranslation();
					if (InThreatRange(targetPos, threatPos, *pThreatRange))
					{
						// go avoid threat immediately
						GoFlyToPerchState(true, true, false, hThreatObj);
					}
				}
			}
		}
		return;
	}

	float waitTime = RandomFloatRange(0.0f, 1.0f);
	GoWaitTimerState(SID("AvoidThreat"), waitTime);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::GetIdleAnimList(ListArray<StringId64>& outIdleAnims, bool includePerch, bool includeGround) const
{
	outIdleAnims.Clear();

	{
		const DC::AnimalIdleAnimList* pList = m_pIdleAnimListShared.GetTyped();
		if (pList != nullptr)
		{
			for (int kk = 0; kk < pList->m_count; kk++)
			{
				outIdleAnims.PushBack(pList->m_array[kk]);
			}
		}
	}

	if (includePerch)
	{
		const DC::AnimalIdleAnimList* pList = m_pIdleAnimListPerch.GetTyped();
		if (pList != nullptr)
		{
			for (int kk = 0; kk < pList->m_count; kk++)
			{
				outIdleAnims.PushBack(pList->m_array[kk]);
			}
		}
	}

	if (includeGround)
	{
		const DC::AnimalIdleAnimList* pList = m_pIdleAnimListGround.GetTyped();
		if (pList != nullptr)
		{
			for (int kk = 0; kk < pList->m_count; kk++)
			{
				outIdleAnims.PushBack(pList->m_array[kk]);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::PlayAnim(StringId64 animId, StringId64 upAnimId, bool forceLoop, float fadeInTime, float fadeOutTime)
{
	SsAnimateParams params;
	params.m_nameId = animId;
	params.m_forceLoop = forceLoop;
	params.m_fadeInSec = fadeInTime;
	params.m_fadeOutSec = fadeOutTime;
	SendEvent(SID("play-animation"), this, BoxedSsAnimateParams(&params));

	AnimSimpleLayer* pUpAnimLayer = GetAnimControl()->GetSimpleLayerByIndex(1);

	if (upAnimId)
	{
		if (!pUpAnimLayer)
		{
			pUpAnimLayer = GetAnimControl()->CreateSimpleLayer(SID("partial-slerp-0"), ndanim::kBlendSlerp, 1);
			GAMEPLAY_ASSERT(pUpAnimLayer);
		}

		if (pUpAnimLayer)
		{
			AnimSimpleLayer::FadeRequestParams fparams;
			fparams.m_skipFirstFrameUpdate = true;
			fparams.m_forceLoop = forceLoop;
			pUpAnimLayer->RequestFadeToAnim(upAnimId, fparams);
		}
	}
	else if (pUpAnimLayer && !pUpAnimLayer->BeingDestroyed())
	{
		pUpAnimLayer->FadeOutAndDestroy(fadeInTime);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 SimpleBird::GetTakeOffAnimId(Vector_arg currFacing, Vector_arg flyDir, bool isPanic)
{
	StringId64 takeOffAnimId = INVALID_STRING_ID_64;

	static ScriptPointer<DC::ScriptLambda> s_lambda(SID("get-take-off-anim-id"), SID("animal-behavior"));
	const DC::ScriptLambda* pScriptLambda = s_lambda.GetTyped();
	if (pScriptLambda)
	{
		const ScriptValue args[] = { ScriptValue(&currFacing), ScriptValue(&flyDir), ScriptValue(isPanic) };
		ScriptValue retVal = ScriptManager::Eval(pScriptLambda, ARRAY_COUNT(args), args);
		takeOffAnimId = retVal.m_stringId;
	}

	return takeOffAnimId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::BirdFlyAnim SimpleBird::GetFlyAnimId(StringId64 skelSizeId, Point_arg currPos, Point_arg endPos, StringId64 prevAnim, bool isPanic)
{
	DC::BirdFlyAnim result;
	memset(&result, 0, sizeof(DC::BirdFlyAnim));

	static ScriptPointer<DC::ScriptLambda> s_lambda(SID("get-fly-anim-id"), SID("animal-behavior"));
	const DC::ScriptLambda* pScriptLambda = s_lambda.GetTyped();
	if (pScriptLambda)
	{
		const ScriptValue args[] = { ScriptValue(skelSizeId), ScriptValue(&currPos), ScriptValue(&endPos), ScriptValue(prevAnim), ScriptValue(isPanic) };
		ScriptValue retVal = ScriptManager::Eval(pScriptLambda, ARRAY_COUNT(args), args);
		const DC::BirdFlyAnim* pResult = static_cast<const DC::BirdFlyAnim*>(retVal.m_pointer);
		GAMEPLAY_ASSERTF(pResult && pResult->m_signature == SID("bird-fly-anim"),
			("get-fly-anim-id supposed to get struct bird-fly-anim as result but not. params:(%s, curr-pos:(%s), end-pos:(%s), prev-anim:%s)\n",
				DevKitOnly_StringIdToString(skelSizeId),
				PrettyPrint(currPos), PrettyPrint(endPos), DevKitOnly_StringIdToString(prevAnim)));
		result = *pResult;
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float SimpleBird::GetDesiredPitchAngleRad(float totalFlyDist, float currFlyDist, float currSpeed, Point_arg currPos, Point_arg startPos, Point_arg endPos)
{
	static ScriptPointer<DC::ScriptLambda> s_lambda(SID("get-simple-bird-desired-pitch-angle"), SID("animal-behavior"));
	const DC::ScriptLambda* pScriptLambda = s_lambda.GetTyped();
	float val = 0.0f;
	if (pScriptLambda)
	{
		const ScriptValue args[] = { ScriptValue(totalFlyDist), ScriptValue(currFlyDist), ScriptValue(currSpeed), ScriptValue(&currPos), ScriptValue(&startPos), ScriptValue(&endPos) };
		ScriptValue retVal = ScriptManager::Eval(pScriptLambda, ARRAY_COUNT(args), args);

		val = DEGREES_TO_RADIANS(retVal.m_float);
	}

	ANIM_ASSERT(IsFinite(val));
	return val;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleBird::IsInAvoidThreat() const
{
	if (IsState(SID("FlyToPerch")))
	{
		const MoveState* pState = static_cast<const MoveState*>(GetState());
		if (pState->IsAvoidThreat())
		{
			return true;
		}
	}

	return false;
}

FROM_PROCESS_DEFINE(SimpleBird);
PROCESS_REGISTER(SimpleBird, NdAnimalBase);

/// --------------------------------------------------------------------------------------------------------------- ///
// bird state-machine
/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::BaseState::Enter()
{
	ParentClass::Enter();

	SimpleBird& self = Self();
	m_initLocation = self.GetBoundFrame();
	m_startTime = self.GetCurTime();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::StartFlyAnim(SimpleBird& self, const DC::BirdFlyAnim& anim)
{
	m_flyAnim.m_animId = anim.m_name;
	m_flyAnim.m_upAnimId = anim.m_upName;
	if (anim.m_loopTime > 0)
	{
		const ArtItemAnim* pAnim = self.GetAnimControl()->LookupAnim(anim.m_name).ToArtItem();
		m_flyAnim.m_duration = pAnim ? (pAnim->m_pClipData->m_fNumFrameIntervals * pAnim->m_pClipData->m_secondsPerFrame) * anim.m_loopTime : 0.f;
	}
	else
	{
		m_flyAnim.m_duration = anim.m_duration;
	}
	m_flyAnim.m_elapsedTime = 0.f;

	self.PlayAnim(m_flyAnim.m_animId, m_flyAnim.m_upAnimId, true, anim.m_blendTime, anim.m_blendTime);
	m_flyAnim.m_desiredSpeed = anim.m_desiredSpeed;
	m_flyAnim.m_speedSpring = anim.m_speedSpring;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::UpdateFlyAnim(SimpleBird& self, float dt, const Locator& targetLoc, float currFlyDist, float totalFlyDist)
{
	m_flyAnim.m_elapsedTime += dt;

	if (m_needLandingAnim && !m_landingAnimPrepared)
	{
		if (currFlyDist + SimpleBird::kMaxLandingDist >= totalFlyDist)
		{
			PrepareLandingAnim(self, self.m_skelSizeId, m_currSpeed, targetLoc);
			m_landingAnimPrepared = true;
		}
	}

	if (m_landingAnim.m_valid)
	{
		if (m_landingAnim.m_isPlaying)
		{
			return;
		}
		else if (m_landingAnim.m_shouldPlay)
		{
			PlayLandingAnim(self);
			return;
		}
	}

	if (m_flyAnim.m_elapsedTime >= m_flyAnim.m_duration)
	{
		const Point currPos = self.GetTranslation();
		// start a new fly animation.
		const bool isPanic = self.IsInAvoidThreat();
		const DC::BirdFlyAnim newFlyAnim = GetFlyAnimId(self.m_skelSizeId, currPos, m_targetLoc.GetTranslation(), m_flyAnim.m_animId, isPanic);
		StartFlyAnim(self, newFlyAnim);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::PrepareLandingAnim(const SimpleBird& self, StringId64 skelSizeId, float currSpeed, const Locator& targetLoc)
{
	m_landingAnim.m_valid = false;

	static ScriptPointer<DC::ScriptLambda> s_lambda(SID("get-landing-anim-id"), SID("animal-behavior"));
	const DC::ScriptLambda* pScriptLambda = s_lambda.GetTyped();
	if (pScriptLambda)
	{
		const ScriptValue args[] = { ScriptValue(skelSizeId), ScriptValue(currSpeed) };
		ScriptValue retVal = ScriptManager::Eval(pScriptLambda, ARRAY_COUNT(args), args);
		const DC::BirdLandingAnim* pResult = static_cast<const DC::BirdLandingAnim*>(retVal.m_pointer);

		if (pResult)
		{
			ALWAYS_ASSERTF(pResult->m_signature == SID("bird-landing-anim"),
				("get-landing-anim-id supposed to get struct bird-landing-anim as result but not. params:(%s, curr-speed:%f)\n",
				DevKitOnly_StringIdToString(skelSizeId), currSpeed));

			const ArtItemAnim* pAnim = self.GetAnimControl()->LookupAnim(pResult->m_name).ToArtItem();
			if (pAnim != nullptr)
			{
				m_landingAnim.m_animId = pResult->m_name;
				m_landingAnim.m_distToDest = Min(pResult->m_distToDest, SimpleBird::kMaxLandingDist);
				m_landingAnim.m_isPlaying = false;

				const float animDuration = GetDuration(pAnim);
				Locator animAlignDelta(kIdentity);
				{
					Locator alignLoc0, alignLoc1;
					bool valid0 = EvaluateChannelInAnim(self.GetAnimControl(), m_landingAnim.m_animId, SID("align"), 0.f, &alignLoc0);
					bool valid1 = EvaluateChannelInAnim(self.GetAnimControl(), m_landingAnim.m_animId, SID("align"), 1.f, &alignLoc1);
					if (valid0 && valid1)
					{
						animAlignDelta = alignLoc0.UntransformLocator(alignLoc1);
					}

					//Locator alignLoc2;
					//bool valid2 = EvaluateChannelInAnim(self.GetAnimControl(), m_landingAnim.m_animId, SID("align"), (0.033f / animDuration), &alignLoc2);
					//if (valid2)
					//{
					//	const Locator animDelta = alignLoc0.UntransformLocator(alignLoc2);
					//	m_landingAnim.m_animStartSpeed = Length((animDelta.GetTranslation() - Point(kOrigin)) / GetProcessDeltaTime());
					//}
				}

				m_landingAnim.m_idealStartLoc = targetLoc.TransformLocator(Inverse(animAlignDelta));

				const Point idealStartPos = m_landingAnim.m_idealStartLoc.GetTranslation();
				//const Locator testLoc = m_landingAnim.m_startLoc.TransformLocator(animAlignDelta);
				//GAMEPLAY_ASSERT(IsClose(testLoc, targetLoc, 0.0001f, 0.9999f));
				m_landingAnim.m_valid = true;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::PlayLandingAnim(SimpleBird& self)
{
	GAMEPLAY_ASSERT(m_landingAnim.m_valid);
	//self.SetLocator(m_landingAnim.m_startLoc);
	self.PlayAnim(m_landingAnim.m_animId, INVALID_STRING_ID_64, false);

	m_landingAnim.m_isPlaying = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::CalcNextFrame(const FlyPath& flyPath,
	Point_arg currPos, const BoundFrame& startBf, const BoundFrame& targetBf,
	const float currFlyDist, const float totalFlyDist,
	float currT, float currSpeed, float dt, SimulateFlyRes* outRes)
{
	const CatmullRom& splinePath = flyPath.GetPath();

	const float distTravelThisFrame = currSpeed * dt;

	if (currFlyDist + distTravelThisFrame < totalFlyDist)
	{
		const float nextTT = splinePath.MoveDistanceAlong(currT, distTravelThisFrame);

		Point desiredPos;
		Vector desiredFacing, dummy;
		splinePath.EvaluateGlobal(nextTT, desiredPos, dummy, desiredFacing);

		// still flying.
		const Vector toNextPos = desiredPos - currPos;
		if (Length(toNextPos) > 0.001f)
		{
			Angle angleXz = AngleFromXZVec(desiredFacing);
			Vector facingXz = VectorFromAngleXZ(angleXz);

			const F32 desiredPitchAngleRad = GetDesiredPitchAngleRad(totalFlyDist, currFlyDist, currSpeed, currPos, startBf.GetTranslation(), targetBf.GetTranslation());

			outRes->m_rawPos = desiredPos;
			outRes->m_facingXz = facingXz;
			outRes->m_desiredPitchRad = desiredPitchAngleRad;
		}
		else
		{
			Angle angleXz = AngleFromXZVec(desiredFacing);
			Vector facingXz = VectorFromAngleXZ(angleXz);

			outRes->m_rawPos = desiredPos;
			outRes->m_facingXz = desiredFacing;
			outRes->m_desiredPitchRad = 0.0f;
		}

		outRes->m_nextT = nextTT;
		outRes->m_valid = true;
	}
	else
	{
		const float nextTT = 1.f;
		Point desiredPos;
		Vector desiredFacing, dummy;
		splinePath.EvaluateGlobal(nextTT, desiredPos, dummy, desiredFacing);

		// reach the end of spline.
		outRes->m_rawPos = desiredPos;
		outRes->m_facingXz = desiredFacing;
		outRes->m_desiredPitchRad = 0.0f;
		outRes->m_nextT = 1.f;
		outRes->m_valid = true;
	}

	ANIM_ASSERT(IsFinite(outRes->m_desiredPitchRad));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::UpdateExtraHeight(SimpleBird& self, Point_arg newPos, float distToDest, float totalFlyDist, float dt)
{
	bool handled = false;
	Maybe<Point> threatPos;

	if (totalFlyDist > 10.f) // if total-fly-dist is small, it doesn't look good.
	{
		for (int kk = 0; kk < self.m_threats.Size(); kk++)
		{
			if (self.m_threats[kk].m_handle.IsKindOf(SID("Player")))
			{
				const NdLocatableSnapshot* pThreatSnapshot = self.m_threats[kk].m_handle.ToSnapshot<NdLocatableSnapshot>();
				if (!pThreatSnapshot)
					continue;

				threatPos = pThreatSnapshot->GetTranslation();
			}
		}
	}

	if (threatPos.Valid())
	{
		const float scale = MinMax01(distToDest / 10.f);
		const DC::AnimalBehaviorSettings* pSettings = self.GetAnimalBehaviorSettings();
		const DC::EllipseRange threatRange = pSettings->m_playerThreatEllipse;
		const float springConst = LerpScaleClamp(0.f, 1.f, 5.f, 2.f, scale);

		if (m_useBigThreatRange)
		{
			DC::EllipseRange bigThreatRange = threatRange;
			bigThreatRange.m_halfHeight += 0.5f;

			if (InThreatRange(newPos, threatPos.Get(), bigThreatRange))
			{
				Maybe<float> threatHeight = CalcThreatHeight(newPos, threatPos.Get(), threatRange);
				if (threatHeight.Valid())
				{
					const float desiredHeightDiff = threatHeight.Get() - (newPos.Y() - threatPos.Get().Y()) + 0.2f; // 0.2m threshold.
					m_extraHeight = m_extraHeightTracker.Track(m_extraHeight, desiredHeightDiff * scale, dt, springConst);
					handled = true;
				}
			}
		}
		else
		{
			if (InThreatRange(newPos, threatPos.Get(), threatRange))
			{
				Maybe<float> threatHeight = CalcThreatHeight(newPos, threatPos.Get(), threatRange);
				if (threatHeight.Valid())
				{
					const float desiredHeightDiff = threatHeight.Get() - (newPos.Y() - threatPos.Get().Y()) + 0.2f; // 0.2m threshold.
					m_extraHeight = m_extraHeightTracker.Track(m_extraHeight, desiredHeightDiff * scale, dt, springConst);
					m_useBigThreatRange = true;
					handled = true;
				}
			}
		}
	}

	if (!handled)
	{
		m_extraHeight = m_extraHeightTracker.Track(m_extraHeight, 0.f, dt, 1.f);
		m_useBigThreatRange = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleBird::MoveState::NeedTakeOffAnim(const SimpleBird& self) const
{
	bool needTakeOffAnim = false;
	{
		StringId64 currAnimId = self.GetCurrentAnim();

		StringId64 blocks[128];
		ListArray<StringId64> idleAnims(128, blocks);

		self.GetIdleAnimList(idleAnims, true, true);
		for (int kk = 0; kk < idleAnims.Size(); kk++)
		{
			if (currAnimId == idleAnims[kk])
			{
				needTakeOffAnim = true;
				break;
			}
		}
	}
	return needTakeOffAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator SimpleBird::MoveState::PlayTakeOffAnim(SimpleBird& self, Point_arg targetPos, bool isPanic)
{
	m_takeoffAnim.m_valid = true;
	m_takeoffAnim.m_isPlaying = true;

	const Point currPos = self.GetTranslation();
	const Quat currRot = self.GetRotation();
	const Vector currFacing = GetLocalZ(currRot);
	const Vector currFacingXz = SafeNormalize(VectorXz(GetLocalZ(currRot)), currFacing);
	const Vector flyDirNorm = SafeNormalize(targetPos - currPos, kUnitZAxis);
	m_takeoffAnim.m_animId = GetTakeOffAnimId(currFacingXz, flyDirNorm, isPanic);

	const ArtItemAnim* pAnim = self.GetAnimControl()->LookupAnim(m_takeoffAnim.m_animId).ToArtItem();
	const SkeletonId skelId = self.GetSkeletonId();
	const float duration = GetDuration(pAnim);
	m_takeoffAnim.m_endPhase = MinMax01((duration - 0.1f) / duration);

	Locator takeoffAlignDelta(kIdentity);
	{
		Locator alignLoc0, alignLoc1;
		bool valid0 = EvaluateChannelInAnim(skelId, pAnim, SID("align"), 0.f, &alignLoc0);
		bool valid1 = EvaluateChannelInAnim(skelId, pAnim, SID("align"), m_takeoffAnim.m_endPhase, &alignLoc1);
		if (valid0 && valid1)
		{
			takeoffAlignDelta = alignLoc0.UntransformLocator(alignLoc1);
		}
	}

	const Quat flyDirRot = QuatFromLookAt(flyDirNorm, kUnitYAxis);

	const Quat takeoffRotDelta = takeoffAlignDelta.GetRotation();
	const Quat adjustRot = Conjugate(currRot) * flyDirRot * Conjugate(takeoffRotDelta);
	m_takeoffAnim.m_procTurningTime = 0.f;
	m_takeoffAnim.m_procTurningMaxTime = Max(duration - 0.2f, 0.f); // smaller than end-phase

	self.PlayAnim(m_takeoffAnim.m_animId, INVALID_STRING_ID_64, false);

	const Quat newRot = currRot * adjustRot;
	const Locator newLoc(currPos, newRot);
	const Locator afterTakeOffLoc = newLoc.TransformLocator(takeoffAlignDelta);

	m_takeoffAnim.m_actualStartLoc = Locator(currPos, currRot);
	m_takeoffAnim.m_idealStartLoc = newLoc;

	if (FALSE_IN_FINAL_BUILD(g_simpleBirdDebugOptions.m_showAnimName))
	{
		const Locator currLoc(currPos, currRot);

		g_prim.Draw(DebugCoordAxes(currLoc, 0.3f, PrimAttrib(kPrimEnableHiddenLineAlpha)), Seconds(1.f));
		g_prim.Draw(DebugCoordAxes(newLoc, 0.35f, PrimAttrib(kPrimEnableHiddenLineAlpha)), Seconds(1.f));
		g_prim.Draw(DebugCoordAxes(afterTakeOffLoc, 0.3f, PrimAttrib(kPrimEnableHiddenLineAlpha)), Seconds(1.f));
		g_prim.Draw(DebugString(afterTakeOffLoc.GetTranslation(), "after-take-off", kColorWhite, 0.6f), Seconds(1.f));
	}

	return afterTakeOffLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::PrepareFly(const Locator& targetLoc, bool isPanic, bool needLandingAnim, NdLocatableObjectHandle hInitThreat)
{
	SimpleBird& self = Self();

	const Point initPos = self.GetTranslation();

	m_targetLoc = targetLoc;
	m_targetLocValid = true;

	m_isAvoidThreat = isPanic;
	m_needLandingAnim = needLandingAnim;

	m_hInitThreat = hInitThreat;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::BeginFlyTo()
{
	SimpleBird& self = Self();

	const Point initPos = self.GetTranslation();
	const Quat currRot = self.GetRotation();

	m_pitchTracker.Reset();
	m_currPitchAngleRad = 0.0f;

	GAMEPLAY_ASSERT(m_targetLocValid);
	const Locator& targetLoc = m_targetLoc;
	const Point targetPos = targetLoc.GetTranslation();

	m_isDone = false;

	const bool needTakeOffAnim = NeedTakeOffAnim(self);
	Maybe<Locator> afterTakeoffLoc;
	if (needTakeOffAnim)
	{
		afterTakeoffLoc = PlayTakeOffAnim(self, targetPos, m_isAvoidThreat);
	}
	else
	{
		const DC::BirdFlyAnim flyAnim = GetFlyAnimId(self.m_skelSizeId, initPos, targetPos, INVALID_STRING_ID_64, m_isAvoidThreat);
		StartFlyAnim(self, flyAnim);
	}

	// set initial facing.
	const Vector currFacing = GetLocalZ(self.GetRotation());
	const Vector desiredFacingXZ = SafeNormalize(targetPos - initPos, currFacing);
	const Quat desiredRot = QuatFromXZDir(desiredFacingXZ);

	m_initLocation = self.GetBoundFrame(); // update self rotation.

	const Vector currVel = self.m_currVel;
	m_currSpeed = Length(currVel);
	m_speedTracker.Reset();

	m_currT = 0.f;

	// create spline fly path.
	Locator blocks[128];
	ListArray<Locator> pathWaypoints(128, blocks);

	pathWaypoints.PushBack(Locator(initPos)); // start pos

	bool addMiddlePoint = true;

	if (afterTakeoffLoc.Valid())
	{
		pathWaypoints.PushBack(afterTakeoffLoc.Get());
	}

	//if (pMiddlePoints)
	//{
	//	const ListArray<Point>& middlePoints = *pMiddlePoints;
	//	for (U32 ii = 0; ii < middlePoints.Size(); ii++)
	//		pathWaypoints.PushBack(Locator(middlePoints[ii]));

	//	addMiddlePoint = false;
	//}
	//else 
	if (m_currSpeed > 0.1f && !afterTakeoffLoc.Valid())
	{
		if (Dot(desiredFacingXZ, currVel) < 0.f)
		{
			const Vector currVelNorm = SafeNormalize(currVel, kZero);
			const Vector vTest0 = Cross(currVelNorm, kUnitYAxis);

			const NdLocatableSnapshot* pInitThreat = m_hInitThreat.ToSnapshot<NdLocatableSnapshot>();

			Vector avoidDir;
			if (pInitThreat != nullptr)
			{
				avoidDir = Dot(pInitThreat->GetTranslation() - initPos, vTest0) < 0.f ? vTest0 : -vTest0;
			}
			else
			{
				avoidDir = frand() < 0.5f ? vTest0 : -vTest0;
			}			 

			const Point momentumPos1 = initPos + currVel * 1.f;
			const Point momentumPos2 = momentumPos1 + avoidDir * 1.f;
			pathWaypoints.PushBack(Locator(momentumPos2));
		}
		else
		{
			const Point momentumPos = initPos + currVel * 1.f + Vector(0.f, 1.f, 0.f);
			pathWaypoints.PushBack(Locator(momentumPos));
		}

		addMiddlePoint = false;
	}

	if (m_avoidCollPosValid)
	{
		pathWaypoints.PushBack(Locator(m_avoidCollPos));

		// add some momentum to avoid clipping geo.
		Maybe<Point> momentumPos;
		for (int kk = 0; kk < 4; kk++)
		{
			const Point testPos = m_avoidCollPos + SafeNormalize(m_avoidCollPos - initPos, kZero) * (5.f - kk * 1.f);
			if (Dot(testPos - m_avoidCollPos, targetPos - testPos) > 0.f) // to avoid sharp turn, ideally we should do more vertical raycasts.
			{
				momentumPos = testPos;
				break;
			}
		}
		if (momentumPos.Valid())
		{
			pathWaypoints.PushBack(Locator(momentumPos.Get()));
		}

		addMiddlePoint = false;
	}

	if (addMiddlePoint)
	{
		Point middlePos = Lerp(initPos, targetPos, 0.5f);
		middlePos += Vector(0.f, 1.f, 0.f);
		pathWaypoints.PushBack(Locator(middlePos));
	}

	pathWaypoints.PushBack(m_targetLoc); // end pos
	self.m_flyPath.CreatePath(pathWaypoints);

	m_initialized = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::UpdateFlyTo()
{
	SimpleBird& self = Self();

	m_flyRes.Clear();

	if (m_useRaycast && !m_raycastDone)
	{
		const I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;

		if (m_raycastKickedFN2 != -1 && !m_raycastGathered2)
		{
			GAMEPLAY_ASSERT(currFN > m_raycastKickedFN2);
			GatherRaycastPhase2();
			BeginFlyTo();
		}
		else if (m_raycastKickedFN1 != -1 && !m_raycastGathered1)
		{
			if (currFN > m_raycastKickedFN1)
			{
				GatherRaycastPhase1();

				if (m_raycastDone)
					BeginFlyTo();
				else
					return;
			}
			else
			{
				// wait for raycast result.
				return;
			}
		}
	}

	GAMEPLAY_ASSERT(m_initialized);

	const float elapsedTime = (self.GetCurTime() - m_startTime).ToSeconds();
	const float dt = self.GetClock()->GetDeltaTimeInSeconds();
	const Point currPos = self.GetTranslation();

	const CatmullRom& splinePath = self.m_flyPath.GetPath();

	if (m_landingAnim.m_valid && m_landingAnim.m_isPlaying)
	{
		const AnimSimpleLayer* pBaseLayer = self.GetAnimControl()->GetSimpleLayerById(SID("base"));
		const AnimSimpleInstance* pCurrInst = pBaseLayer ? pBaseLayer->CurrentInstance() : nullptr;
		if (pCurrInst != nullptr && pCurrInst->GetPhase() >= 1.0f)
		{
			m_isDone = true;
		}

		return;
	}

	if (m_takeoffAnim.m_valid && m_takeoffAnim.m_isPlaying)
	{
		if (m_takeoffAnim.m_isDone)
		{
			// start a new fly animation.
			const bool isPanic = self.IsInAvoidThreat();
			const DC::BirdFlyAnim newFlyAnim = GetFlyAnimId(self.m_skelSizeId, currPos, m_targetLoc.GetTranslation(), INVALID_STRING_ID_64, isPanic);
			StartFlyAnim(self, newFlyAnim);

			m_takeoffAnim.m_isPlaying = false;
		}
	}
	
	if (!m_takeoffAnim.m_isPlaying)
	{
		const float currFlyDist = splinePath.GlobalParamToArcLength(m_currT);
		const float totalFlyDist = splinePath.GetTotalArcLength();

		UpdateFlyAnim(self, dt, m_targetLoc, currFlyDist, totalFlyDist);

		if (!m_landingAnim.m_isPlaying)
		{
			// check slowdown to landing speed.
			//if (m_landingAnim.m_valid && m_landingAnim.m_startT >= 0.f && !m_flyAnim.m_startSlowdown)
			//{
			//	const float futureTravelDist = m_currSpeed * 0.3f; // TODO: specify landing start dist.
			//	const float nextTT = splinePath.MoveDistanceAlong(m_currT, futureTravelDist);
			//	if (nextTT >= m_landingAnim.m_startT)
			//		m_flyAnim.m_startSlowdown = true;
			//}

			//if (m_flyAnim.m_startSlowdown)
			//	m_currSpeed = m_speedTracker.Track(m_currSpeed, m_landingAnim.m_animStartSpeed, dt, 20.f);
			//else
			m_currSpeed = m_speedTracker.Track(m_currSpeed, m_flyAnim.m_desiredSpeed, dt, m_flyAnim.m_speedSpring);

			CalcNextFrame(self.m_flyPath, currPos, m_initLocation, m_targetLoc, currFlyDist, totalFlyDist, m_currT, m_currSpeed, dt, &m_flyRes);

			UpdateExtraHeight(self, m_flyRes.m_rawPos, totalFlyDist - currFlyDist, totalFlyDist, dt);
			m_flyRes.m_adjustedPos = m_flyRes.m_rawPos + Vector(kUnitYAxis) * m_extraHeight;
			GAMEPLAY_ASSERT(IsFinite(m_flyRes.m_adjustedPos));

			m_currPitchAngleRad = m_pitchTracker.Track(m_currPitchAngleRad, m_flyRes.m_desiredPitchRad, dt, 4.0f);

			m_currT = m_flyRes.m_nextT;
			if (m_currT >= 1.f && !m_landingAnim.m_valid)
			{
				m_isDone = true;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator SimpleBird::MoveState::GetRootAnimDelta(SimpleBird& self, const float phase0, const float phase1) const
{
	Locator alignDelta(kIdentity);

	const AnimSimpleLayer* pBaseLayer = self.GetAnimControl()->GetSimpleLayerById(SID("base"));
	const AnimSimpleInstance* pInst = pBaseLayer ? pBaseLayer->CurrentInstance() : nullptr;

	const SkeletonId skelId = self.GetSkeletonId();
	const ArtItemAnim* pAnim = pInst->GetAnim().ToArtItem();

	Locator alignLoc0, alignLoc1;
	bool valid0 = EvaluateChannelInAnim(skelId, pAnim, SID("align"), phase0, &alignLoc0);
	bool valid1 = EvaluateChannelInAnim(skelId, pAnim, SID("align"), phase1, &alignLoc1);
	if (valid0 && valid1)
	{
		alignDelta = alignLoc0.UntransformLocator(alignLoc1);
	}
	return alignDelta;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::PostAnimUpdateTakeoff(SimpleBird& self, float dt)
{
	const Locator currLoc = self.GetLocator();

	const AnimSimpleLayer* pBaseLayer = self.GetAnimControl()->GetSimpleLayerById(SID("base"));
	const AnimSimpleInstance* pInst = pBaseLayer ? pBaseLayer->CurrentInstance() : nullptr;

	if (m_takeoffAnim.m_procTurningMaxTime > 0.f && m_takeoffAnim.m_procTurningTime < m_takeoffAnim.m_procTurningMaxTime)
	{
		m_takeoffAnim.m_procTurningTime = Min(m_takeoffAnim.m_procTurningTime + dt, m_takeoffAnim.m_procTurningMaxTime);

		const Locator alignDelta = GetRootAnimDelta(self, 0.f, pInst->GetPhase());
		const Locator actualLoc = m_takeoffAnim.m_actualStartLoc.TransformLocator(alignDelta);
		const Locator idealLoc = m_takeoffAnim.m_idealStartLoc.TransformLocator(alignDelta);

		//g_prim.Draw(DebugCoordAxes(actualLoc, 0.3f, PrimAttrib(kPrimDisableHiddenLineAlpha)));
		//g_prim.Draw(DebugCoordAxes(idealLoc, 0.4f, PrimAttrib(kPrimDisableHiddenLineAlpha)));

		const float t0 = MinMax01(m_takeoffAnim.m_procTurningTime / m_takeoffAnim.m_procTurningMaxTime);
		const float t1 = CalculateCurveValue(t0, DC::kAnimCurveTypeUniformS);
		Locator blendedLoc = Lerp(actualLoc, idealLoc, t1);
		blendedLoc.SetRot(Normalize(blendedLoc.Rot()));
		self.SetLocator(blendedLoc);
	}
	else
	{
		const Locator alignDelta = GetRootAnimDelta(self, pInst->GetPrevPhase(), pInst->GetPhase());
		Locator newLoc = currLoc.TransformLocator(alignDelta);
		newLoc.SetRot(Normalize(newLoc.Rot()));
		self.SetLocator(newLoc);
	}

	const Locator newLoc = self.GetLocator();
	m_currPitchAngleRad = SafeAsin(GetLocalZ(newLoc.GetRotation()).Y());

	if (pInst->GetPhase() < 1.f)
	{
		m_takeoffAnim.m_velocity = (newLoc.GetTranslation() - currLoc.GetTranslation()) / dt;
	}

	GAMEPLAY_ASSERT(m_takeoffAnim.m_endPhase >= 0.f && m_takeoffAnim.m_endPhase <= 1.f);
	if (pInst->GetPhase() >= m_takeoffAnim.m_endPhase)
	{
		m_takeoffAnim.m_isDone = true;
		m_takeoffAnim.m_endPos = newLoc.GetTranslation();
		m_takeoffAnim.m_endTime = self.GetCurTime();

		const CatmullRom& splinePath = self.m_flyPath.GetPath();
		m_currT = MinMax01(splinePath.FindGlobalParamClosestToPoint(m_takeoffAnim.m_endPos));
		m_currSpeed = Length(m_takeoffAnim.m_velocity);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::PostAnimUpdateLanding(SimpleBird& self, float dt)
{
	const AnimSimpleLayer* pBaseLayer = self.GetAnimControl()->GetSimpleLayerById(SID("base"));
	const AnimSimpleInstance* pInst = pBaseLayer ? pBaseLayer->CurrentInstance() : nullptr;

	const SkeletonId skelId = self.GetSkeletonId();
	const ArtItemAnim* pAnim = pInst->GetAnim().ToArtItem();
	if (pAnim != nullptr)
	{
		const float animDuration = GetDuration(pAnim);
		const float timeElapsed = (self.GetCurTime() - m_landingAnim.m_startTime).ToSeconds();
		const float blendTime = animDuration * 0.7f;

		const Locator targetLoc = m_targetLoc;
		Locator actualLoc = m_landingAnim.m_actualStartLoc;
		{
			const Vector startToDest = targetLoc.GetTranslation() - actualLoc.GetTranslation();
			const Vector initVelH = VectorXz(m_flyAnim.m_velocity);
			const Vector initVelV = Vector(0, m_flyAnim.m_velocity.Y(), 0);

			Vector currVelH;
			float tH;
			{
				const float distStartToDestH = LengthXz(startToDest);
				const float speedH = Length(initVelH);

				float decelTime;
				if (0.5f * speedH * blendTime > distStartToDestH)
					decelTime = 2.f * distStartToDestH / speedH;
				else
					decelTime = blendTime;

				const Vector decelH = -initVelH / decelTime;
				tH = Min(timeElapsed, decelTime);
				currVelH = initVelH + decelH * tH;
			}

			Vector currVelV;
			float tV;
			{
				const Vector startToDestV = Vector(0, startToDest.Y(), 0);
				const float distStartToDestV = Abs(startToDest.Y());
				const float speedV = Abs(m_flyAnim.m_velocity.Y());

				float decelTime;
				if (Dot(startToDestV, initVelV) < 0.f)
					decelTime = blendTime;
				else if (0.5f * speedV * blendTime > distStartToDestV)
					decelTime = 2.f * distStartToDestV / speedV;
				else
					decelTime = blendTime;

				const Vector decelV = -initVelV / decelTime;
				tV = Min(timeElapsed, decelTime);
				currVelV = initVelV + decelV * tV;
			}

			const Vector translationH = (initVelH + currVelH) / 2.f * tH;
			const Vector translationV = (initVelV + currVelV) / 2.f * tV;
			const Point actualPos = m_landingAnim.m_actualStartLoc.GetTranslation() + translationH + translationV;
			actualLoc.SetTranslation(actualPos);
		}

		Locator idealLoc;
		{
			Locator alignLoc0, alignLoc1;
			bool valid0 = EvaluateChannelInAnim(skelId, pAnim, SID("align"), 0.f, &alignLoc0);
			bool valid1 = EvaluateChannelInAnim(skelId, pAnim, SID("align"), pInst->GetPhase(), &alignLoc1);
			if (valid0 && valid1)
			{
				const Locator alignDelta = alignLoc0.UntransformLocator(alignLoc1);
				idealLoc = m_landingAnim.m_idealStartLoc.TransformLocator(alignDelta);
			}
		}

		if (FALSE_IN_FINAL_BUILD(g_simpleBirdDebugOptions.m_showAnimName))
		{
			g_prim.Draw(DebugCoordAxes(actualLoc, 0.3f, PrimAttrib(kPrimEnableHiddenLineAlpha)));
			g_prim.Draw(DebugCoordAxes(idealLoc, 0.35f, PrimAttrib(kPrimEnableHiddenLineAlpha)));
		}

		const float t0 = MinMax01(timeElapsed / blendTime);
		const float t1 = CalculateCurveValue(t0, DC::kAnimCurveTypeUniformS);
		const Locator finalLoc = Lerp(actualLoc, idealLoc, t1);
		self.SetLocator(finalLoc);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::PostAnimUpdateFlying(SimpleBird& self, float dt)
{
	if (!m_flyRes.m_valid)
		return;

	const SimulateFlyRes& res = m_flyRes;

	const Point currPos = self.GetTranslation();

	const Quat velRot = QuatFromXZDir(res.m_facingXz);
	const Quat pitchRot = QuatFromAxisAngle(-Vector(kUnitXAxis), m_currPitchAngleRad);
	const Quat finalRot = Normalize(velRot * pitchRot);

	Point newPos = res.m_adjustedPos;
	if (m_takeoffAnim.m_endTime > Seconds(0))
	{
		const float elapsedTime = (self.GetCurTime() - m_takeoffAnim.m_endTime).ToSeconds();
		if (elapsedTime <= 1.f)
		{
			const float tt = CalculateCurveValue(MinMax01(elapsedTime), DC::kAnimCurveTypeUniformS);
			newPos = Lerp(m_takeoffAnim.m_endPos + m_takeoffAnim.m_velocity * elapsedTime, newPos, tt);
		}
	}

	m_flyAnim.m_velocity = (newPos - currPos) / dt;

	const Locator finalLoc = Locator(newPos, finalRot);
	self.SetLocator(finalLoc);

	if (m_landingAnim.m_valid && m_landingAnim.m_distToDest > 0.f)
	{
		const CatmullRom& splinePath = self.m_flyPath.GetPath();
		const float currFlyDist = splinePath.GlobalParamToArcLength(m_currT);
		const float totalFlyDist = splinePath.GetTotalArcLength();
		if (currFlyDist + m_landingAnim.m_distToDest >= totalFlyDist)
		{
			m_landingAnim.m_shouldPlay = true;
			m_landingAnim.m_actualStartLoc = self.GetLocator();
			m_landingAnim.m_startTime = self.GetCurTime();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::PostAnimUpdate()
{
	SimpleBird& self = Self();

	const float dt = GetProcessDeltaTime();

	if (m_takeoffAnim.m_isPlaying)
	{
		PostAnimUpdateTakeoff(self, dt);
	}
	else if (m_landingAnim.m_isPlaying)
	{
		PostAnimUpdateLanding(self, dt);
	}
	else
	{
		PostAnimUpdateFlying(self, dt);
	}

	AnimControl* pAnimControl = self.GetAnimControl();
	AnimSimpleLayer* pUpAnimLayer = pAnimControl->GetSimpleLayerByIndex(1);
	if (pUpAnimLayer && pUpAnimLayer->GetDesiredFade() > 0.f && !pUpAnimLayer->BeingDestroyed())
	{
		const Vector vel = self.GetVelocityWs();
		F32 upAngle = RADIANS_TO_DEGREES(Atan2(vel.Y(), LengthXz(vel)));
		F32 upBlend = LerpScale(0.0f, 75.0f, 0.01f, 1.0f, upAngle);
		m_upLayerBlend = m_upLayerBlendTracker.Track(m_upLayerBlend, upBlend, dt, 4.f);

		F32 finalFade = pAnimControl->GetSimpleLayerByIndex(0)->GetCurrentFade() * m_upLayerBlend;
		pUpAnimLayer->SetCurrentFade(finalFade);
	}
	else
	{
		m_upLayerBlend = 0.f;
		m_upLayerBlendTracker.Reset();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::KickRaycastPhase1(Point_arg startPos, Point_arg targetPos)
{
	HavokRayCastJob& job = m_phase1Job;

	const Point middlePos = Lerp(startPos, targetPos, 0.5f);
	const Vector toTargetDir = SafeNormalize(targetPos - startPos, kZero);
	const Vector vLeft = Cross(kUnitYAxis, toTargetDir);

	const Point middlePos1 = middlePos + vLeft * 1.f;
	const Point middlePos2 = middlePos - vLeft * 1.f;

	job.Open(6, 1, 0);

	{
		const float kPadding = 2.f;
		Point startPos0 = startPos;
		Point startPos1 = startPos;
		Point startPos2 = startPos;

		if (DistSqr(startPos0, middlePos) > Sqr(kPadding))
		{
			startPos0 = startPos0 + SafeNormalize(middlePos - startPos, kZero) * kPadding;
		}

		if (DistSqr(startPos1, middlePos1) > Sqr(kPadding))
		{
			startPos1 = startPos1 + SafeNormalize(middlePos1 - startPos1, kZero) * kPadding;
		}

		if (DistSqr(startPos2, middlePos2) > Sqr(kPadding))
		{
			startPos2 = startPos2 + SafeNormalize(middlePos2 - startPos2, kZero) * kPadding;
		}

		job.SetProbeExtents(0, startPos0, middlePos);
		job.SetProbeExtents(1, startPos1, middlePos1);
		job.SetProbeExtents(2, startPos2, middlePos2);
	}

	{
		const float kPadding = 3.f;
		Point targetPos0 = targetPos;
		Point targetPos1 = targetPos;
		Point targetPos2 = targetPos;

		if (DistSqr(middlePos, targetPos0) > Sqr(kPadding))
		{
			targetPos0 = targetPos0 + SafeNormalize(middlePos - targetPos0, kZero) * kPadding;
		}

		if (DistSqr(middlePos1, targetPos1) > Sqr(kPadding))
		{
			targetPos1 = targetPos1 + SafeNormalize(middlePos1 - targetPos1, kZero) * kPadding;
		}

		if (DistSqr(middlePos2, targetPos2) > Sqr(kPadding))
		{
			targetPos2 = targetPos2 + SafeNormalize(middlePos2 - targetPos2, kZero) * kPadding;
		}

		job.SetProbeExtents(3, middlePos, targetPos0);
		job.SetProbeExtents(4, middlePos1, targetPos1);
		job.SetProbeExtents(5, middlePos2, targetPos2);
	}

	job.SetFilterForAllProbes(CollideFilter(Collide::kLayerMaskGeneral | Collide::kLayerMaskWater,
		Pat(1ULL << Pat::kPassThroughShift | 1ULL << Pat::kShootThroughShift | 1ULL << Pat::kCameraPassThroughShift | 1ULL << Pat::kStealthVegetationShift))
	);

	job.Kick(FILE_LINE_FUNC);

	m_raycastKickedFN1 = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::GatherRaycastPhase1()
{
	GAMEPLAY_ASSERT(m_raycastKickedFN1 != -1);

	HavokRayCastJob& job = m_phase1Job;
	job.Wait();

	//job.DebugDraw();

	bool centralPathClear = false;
	if (!job.IsContactValid(0, 0) && !job.IsContactValid(3, 0))
	{
		centralPathClear = true;
	}

	bool leftPathClear = false;
	if (!job.IsContactValid(1, 0) && !job.IsContactValid(4, 0))
	{
		leftPathClear = true;
	}
	
	bool rightPathClear = false;
	if (!job.IsContactValid(2, 0) && !job.IsContactValid(5, 0))
	{
		rightPathClear = true;
	}

	if (centralPathClear)
	{
		// we don't need any avoid-collision path.
		m_raycastDone = true;
	}
	else
	{
		bool takeLeftPath = false;
		bool takeRightPath = false;
		if (leftPathClear && rightPathClear)
		{
			if (frand() < 0.5f)
				takeLeftPath = true;
			else
				takeRightPath = true;
		}
		else if (leftPathClear)
		{
			takeLeftPath = true;
		}
		else if (rightPathClear)
		{
			takeRightPath = true;
		}

		if (takeLeftPath)
		{
			if (job.IsContactValid(1, 0))
			{
				const Point contactPos = job.GetContactPoint(1, 0);
				KickRaycastPhase2(contactPos);
			}
			else if (job.IsContactValid(4, 0))
			{
				const Point contactPos = job.GetContactPoint(4, 0);
				KickRaycastPhase2(contactPos);
			}
			else
			{
				m_raycastDone = true;
			}
		}
		else if (takeRightPath)
		{
			if (job.IsContactValid(2, 0))
			{
				const Point contactPos = job.GetContactPoint(2, 0);
				KickRaycastPhase2(contactPos);
			}
			else if (job.IsContactValid(5, 0))
			{
				const Point contactPos = job.GetContactPoint(5, 0);
				KickRaycastPhase2(contactPos);
			}
			else
			{
				m_raycastDone = true;
			}
		}
		else
		{
			if (job.IsContactValid(0, 0))
			{
				const Point contactPos = job.GetContactPoint(0, 0);
				KickRaycastPhase2(contactPos);
			}
			else if (job.IsContactValid(3, 0))
			{
				const Point contactPos = job.GetContactPoint(3, 0);
				KickRaycastPhase2(contactPos);
			}
			else
			{
				m_raycastDone = true;
			}
		}
	}
	m_raycastGathered1 = true;

	job.Close();

	GAMEPLAY_ASSERT(m_raycastDone || m_raycastKickedFN2 != -1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::KickRaycastPhase2(Point_arg testPos)
{
	HavokSphereCastJob& job = m_phase2Job;

	job.Open(1, 1, 0);

	const Point startPos = testPos + Vector(0.f, 10.f, 0.f);
	const Point endPos = testPos - Vector(0.f, 1.f, 0.f);
	job.SetProbeExtents(0, startPos, testPos);
	job.SetProbeRadius(0, 0.05f);
	job.SetFilterForAllProbes(CollideFilter(Collide::kLayerMaskGeneral | Collide::kLayerMaskWater, 
		Pat(1ULL << Pat::kPassThroughShift | 1ULL << Pat::kShootThroughShift | 1ULL << Pat::kCameraPassThroughShift | 1ULL << Pat::kStealthVegetationShift))
	);

	job.Kick(FILE_LINE_FUNC);

	m_raycastKickedFN2 = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::GatherRaycastPhase2()
{
	GAMEPLAY_ASSERT(m_raycastKickedFN2 != -1);

	HavokSphereCastJob& job = m_phase2Job;
	job.Wait();

	const Point startPos = job.GetProbeStart(0);
	const Point endPos = job.GetProbeEnd(0);

	if (job.IsContactValid(0, 0))
	{
		const Point contactPos = job.GetContactPoint(0, 0) + job.GetContactLever(0, 0);

		if (FALSE_IN_FINAL_BUILD(g_simpleBirdDebugOptions.m_showFlyPath))
			g_prim.Draw(DebugCross(contactPos, 0.1f, kColorRed), Seconds(1.f));

		m_avoidCollPos = contactPos + Vector(kUnitYAxis) * 0.5f;
		m_avoidCollPosValid = true;
	}
	else
	{
		m_avoidCollPos = endPos + Vector(kUnitYAxis) * 0.5f;
		m_avoidCollPosValid = true;
	}

	job.Close();

	m_raycastGathered2 = true;
	m_raycastDone = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleBird::MoveState::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	if (m_initialized)
	{
		const SimpleBird& self = Self();

		if (g_simpleBirdDebugOptions.m_showFlyPath)
		{
			if (self.m_flyPath.IsValid())
				self.m_flyPath.DebugDraw();

			const Locator targetLoc = m_targetLoc;
			g_prim.Draw(DebugCoordAxes(targetLoc, 0.3f, PrimAttrib(kPrimDisableHiddenLineAlpha)));

			if (g_simpleBirdDebugOptions.m_showSpeed)
				pPrinter->PrintText(kColorCyan, "curr-t:%0.3f, speed:%0.3f, desired:%0.3f", m_currT, m_currSpeed, m_flyAnim.m_desiredSpeed);
		}
		else if (g_simpleBirdDebugOptions.m_showSpeed)
		{
			pPrinter->PrintText(kColorCyan, "curr-t:%0.3f, speed:%0.3f, desired:%0.3f", m_currT, m_currSpeed, m_flyAnim.m_desiredSpeed);
		}

		if (m_extraHeight != 0.f)
		{
			pPrinter->PrintText(kColorCyan, "extra-height:%0.3f, %d", m_extraHeight, m_useBigThreatRange);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
class SimpleBird::Active : public SimpleBird::BaseState
{
	typedef SimpleBird::BaseState ParentClass;

public:
	BIND_TO_PROCESS(SimpleBird);

	virtual void Update() override
	{
		ParentClass::Update();

		SimpleBird& self = Self();
		if (self.m_hPerchMgr.HandleValid())
			self.GoChillAndWait();
	}
};

STATE_REGISTER(SimpleBird, Active, kPriorityNormal);

/// --------------------------------------------------------------------------------------------------------------- ///
class SimpleBird::WaitTimer : public SimpleBird::BaseState
{
	typedef SimpleBird::BaseState ParentClass;

public:
	BIND_TO_PROCESS(SimpleBird);

	virtual void Update() override
	{
		ParentClass::Update();

		SimpleBird& self = Self();
		if ((self.GetCurTime() - m_startTime).ToSeconds() > self.m_waitTimerParams.m_timer)
		{
			switch (self.m_waitTimerParams.m_nextStateId.GetValue())
			{
			case SID_VAL("ChillAndWait"):
				self.GoChillAndWait();
				break;
			case SID_VAL("FlyToPerch"):
				self.GoFlyToPerchState(false, false, true);
				break;
			case SID_VAL("AvoidThreat"):
				self.GoFlyToPerchState(true, true, true);
				break;
			case SID_VAL("AvoidDanger"):
				self.GoAvoidDanger();
				break;
			//case SID_VAL("FlyAround"):
			//	self.GoFlyAround();
			//	break;

			default:
				ASSERT(false);
				break;
			}
		}
	}

	virtual void Exit() override
	{
		SimpleBird& self = Self();
		//self.m_waitTimerParams.Clear();
	}
};

STATE_REGISTER(SimpleBird, WaitTimer, kPriorityNormal);

/// --------------------------------------------------------------------------------------------------------------- ///
class SimpleBird::FlyToPerch : public SimpleBird::MoveState
{
	typedef SimpleBird::MoveState ParentClass;

	bool m_flyToLocValid = false;
	Locator m_flyToLoc;

	bool m_failed = false;

public:
	BIND_TO_PROCESS(SimpleBird);

	virtual bool IsAvoidThreat() const override { return m_isAvoidThreat; }

	virtual void Enter() override
	{
		ParentClass::Enter();

		SimpleBird& self = Self();
		StringId64 selfId = self.GetUserId();

		m_useRaycast = self.m_flyToPerchParams.m_useRaycast;

		bool handled = false;

		// request a new perch location.
		SendEvent(SID("request-location"), self.m_hPerchMgr, selfId, self.m_flyToPerchParams.m_preferPerchLoc, &self.m_flyToHistory);

		BoxedValue bValue = SendEvent(SID("perch-locator-valid?"), self.m_hPerchMgr, selfId);
		if (bValue.IsValid() && bValue.GetBool())
		{
			BoxedValue bfValue = SendEvent(SID("get-perch-locator"), self.m_hPerchMgr, selfId);
			const PerchLocationResult* pResult = bfValue.GetConstPtr<PerchLocationResult*>();
			if (!IsClose(pResult->m_locator.GetTranslation(), self.GetTranslation(), 2.f)) // less than 2m, take off anim looks bad.
			{
				const Point initPos = self.GetTranslation();
				const Point targetPos = pResult->m_locator.GetTranslation();
				// set initial facing.
				const Vector currFacing = GetLocalZ(self.GetRotation());
				const Vector desiredFacingXZ = SafeNormalize(targetPos - initPos, currFacing);
				const Quat desiredRot = QuatFromXZDir(desiredFacingXZ);
				const Locator targetLoc = Locator(targetPos, desiredRot);

				PrepareFly(targetLoc, self.m_flyToPerchParams.m_avoidThreat, true, self.m_flyToPerchParams.m_hInitThreat);

				if (m_useRaycast)
				{
					KickRaycastPhase1(self.GetTranslation(), targetPos);
				}
				else
				{
					BeginFlyTo();
				}

				m_flyToLoc = pResult->m_locator.GetLocator();
				m_flyToLocValid = true;

				self.m_lastPerchLocType = pResult->m_type;
				self.m_lastPerchDest = pResult->m_locator.GetTranslation();

				handled = true;
			}
		}

		if (!handled)
		{
			m_failed = true;
			MsgOut("bird %s couldn't find perch-location\n", DevKitOnly_StringIdToString(self.GetUserId()));
			self.GoChillAndWait();
		}
	}

	virtual void Exit() override
	{
		ParentClass::Exit();

		SimpleBird& self = Self();
		StringId64 selfId = self.GetUserId();

		self.m_flyPath.ClearPath();

		if (m_flyToLocValid)
		{
			if (self.m_flyToHistory.IsFull())
				self.m_flyToHistory.Dequeue();
			self.m_flyToHistory.Enqueue(m_flyToLoc);
		}
	}

	virtual void Update() override
	{
		ParentClass::Update();

		SimpleBird& self = Self();
		if (m_failed)
		{
			self.GoChillAndWait();
		}
		else
		{
			UpdateFlyTo();
			if (IsFlyToDone())
				self.GoChillAndWait();
		}
	}
};

STATE_REGISTER(SimpleBird, FlyToPerch, kPriorityNormal);

/// --------------------------------------------------------------------------------------------------------------- ///
class SimpleBird::ChillAndWait : public SimpleBird::BaseState
{
	typedef SimpleBird::BaseState ParentClass;

	float m_duration;

public:
	BIND_TO_PROCESS(SimpleBird);

	virtual void Enter() override
	{
		ParentClass::Enter();

		SimpleBird& self = Self();
		m_duration = RandomFloatRange(self.m_idleTimeMin, self.m_idleTimeMax);

		StringId64 idleAnim = FindIdleAnim();
		self.PlayAnim(idleAnim, INVALID_STRING_ID_64, true);

		self.m_lastPerchDest = MAYBE::kNothing;
	}

	virtual void Update() override
	{
		ParentClass::Update();

		SimpleBird& self = Self();
		const float duration = g_simpleBirdDebugOptions.m_shortIdleTime ? 1.f : m_duration;
		if ((self.GetCurTime() - m_startTime).ToSeconds() > duration)
			self.GoFlyToPerchState(false, false, true);
	}

	StringId64 FindIdleAnim() const
	{
		const SimpleBird& self = Self();

		StringId64 blocks[128];
		ListArray<StringId64> idleAnims(128, blocks);

		const bool includePerch = self.m_lastPerchLocType == PerchLocationResult::kPerch;
		const bool includeGround = self.m_lastPerchLocType == PerchLocationResult::kGround;

		self.GetIdleAnimList(idleAnims, includePerch, includeGround);
		if (idleAnims.Size() > 0)
		{
			I32 numIdleAnims = idleAnims.Size();
			I32 randIdx = RandomIntRange(0, numIdleAnims - 1);
			return idleAnims[randIdx];
		}

		return SID("idle");
	}
};

STATE_REGISTER(SimpleBird, ChillAndWait, kPriorityNormal);

/// --------------------------------------------------------------------------------------------------------------- ///
//class SimpleBird::AvoidThreat : public SimpleBird::MoveState
//{
//	typedef SimpleBird::MoveState ParentClass;
//
//public:
//	BIND_TO_PROCESS(SimpleBird);
//
//	virtual void Enter() override
//	{
//		ParentClass::Enter();
//
//		SimpleBird& self = Self();
//		const Point selfPos = self.GetTranslation();
//		StringId64 selfId = self.GetUserId();
//
//		SendEvent(SID("release-location"), self.m_hPerchMgr, selfId);
//
//		// find the latest and closest threats.
//		const Threat* pThreat = self.FindDangerousThreat(selfPos);
//		if (pThreat)
//		{
//			const Point threatCenter = pThreat->m_handle.ToProcess()->GetTranslation();
//
//			const Vector currVel = self.m_currVel;
//			const float currSpeed = Length(currVel);
//
//			Point blocks[8];
//			ListArray<Point> middlePoints(8, blocks);
//
//			Maybe<Point> flyToPos;
//
//			if (self.m_lastPerchDest.Valid() && currSpeed > 0.1f)
//			{
//				const Sphere threatSphere(threatCenter, 8.f);
//				const Point origFlyDest = self.m_lastPerchDest.Get();
//
//				const Vector currVelXz = VectorFromXzAndY(currVel, Vector(kZero));
//				const Vector currVelNormXz = SafeNormalize(currVelXz, kZero);
//				const Vector selfToThreatCenter = threatCenter - selfPos;
//
//				Point selfPosCopy = selfPos;
//				Point origFlyDestCopy = origFlyDest;
//
//				if (threatSphere.IsPointInside(origFlyDest))
//				{
//					// fly dest is inside the threat sphere, need update dest.
//					const Point closestPos = selfPos + currVelNormXz * Dot(selfToThreatCenter, currVelNormXz);
//					const Vector avoidDir = closestPos - threatCenter;
//					const Vector avoidDirXz = VectorFromXzAndY(avoidDir, Vector(kZero));
//					const Vector avoidDirNormXz = SafeNormalize(avoidDirXz, kZero);
//
//					flyToPos = threatCenter + avoidDirNormXz * 20.f + Vector(0.f, 10.f, 0.f);
//
//					Point momentumPos = selfPos + currVelXz * 0.3f;
//					middlePoints.PushBack(momentumPos);
//				}
//				else if (threatSphere.ClipLineSegment(selfPosCopy, origFlyDestCopy))
//				{
//					// fly path clips threat sphere, so fly higher.
//					flyToPos = origFlyDest;
//
//					const Vector selfToDest = origFlyDest - selfPos;
//					const Vector selfToDestNorm = SafeNormalize(selfToDest, kZero);
//					const Point closestPos = selfPos + selfToDestNorm * Dot(selfToThreatCenter, selfToDestNorm);
//					const Vector avoidDir = closestPos - threatCenter;
//					const Vector avoidDirXz = VectorFromXzAndY(avoidDir, Vector(kZero));
//					const Vector avoidDirNormXz = SafeNormalize(avoidDirXz, kZero);
//
//					Point middlePoint = Lerp(selfPos, flyToPos.Get(), 0.2f);
//					middlePoint += avoidDirNormXz * 2.f + Vector(0.f, 2.f, 0.f);
//					middlePoints.PushBack(middlePoint);
//				}
//				else
//				{
//					flyToPos = origFlyDest;
//
//					Point middlePoint = Lerp(selfPos, flyToPos.Get(), 0.3f);
//					middlePoint += Vector(0.f, 2.f, 0.f);
//					middlePoints.PushBack(middlePoint);
//				}
//			}
//			else
//			{
//				const Vector flyToVecXz = VectorFromXzAndY(selfPos - threatCenter, kZero);
//				const Vector flyToVecNormXz = SafeNormalize(flyToVecXz, kZero);
//				flyToPos = threatCenter + flyToVecNormXz * 20.f + Vector(0.f, 10.f, 0.f);
//
//				Point middlePoint = Lerp(selfPos, flyToPos.Get(), 0.3f);
//				middlePoint += Vector(0.f, 2.f, 0.f);
//				middlePoints.PushBack(middlePoint);
//			}
//
//			ListArray<Point>* pMiddlePoints = nullptr;
//			if (middlePoints.Size() > 0)
//				pMiddlePoints = &middlePoints;
//
//			g_prim.Draw(DebugCross(flyToPos.Get(), 0.3f, kColorCyan, PrimAttrib(kPrimEnableHiddenLineAlpha)), Seconds(3.f));
//			BeginFlyTo(flyToPos.Get(), pMiddlePoints);
//
//			self.m_lastPerchLocType = PerchLocationResult::Type::kNone;
//		}
//		else
//		{
//			self.GoFlyToPerchState(true);
//		}
//
//		self.m_lastPerchDest = MAYBE::kNothing;
//	}
//
//	virtual void Exit() override
//	{
//		ParentClass::Exit();
//
//		SimpleBird& self = Self();
//		self.m_flyPath.ClearPath();
//	}
//
//	virtual void Update() override
//	{
//		ParentClass::Update();
//
//		SimpleBird& self = Self();
//		if (UpdateFlyTo() || IsSafeFromThreats())
//		{
//			self.GoFlyToPerchState(true);
//		}
//	}
//
//	bool IsSafeFromThreats() const
//	{
//		const SimpleBird& self = Self();
//		const Point selfPos = self.GetTranslation();
//
//		const DC::AnimalBehaviorSettings* pSettings = self.GetAnimalBehaviorSettings();
//
//		for (U32 ii = 0; ii < self.m_threats.Size(); ii++)
//		{
//			const SimpleBird::Threat& threat = self.m_threats[ii];
//			if (threat.m_handle.HandleValid())
//			{
//				const Point threatPos = threat.m_handle.ToProcess()->GetTranslation();
//				const bool isThreatPlayer = threat.m_handle.IsKindOf(SID("Player"));
//
//				DC::CylinderRange range = isThreatPlayer ? pSettings->m_playerThreatCylinder : pSettings->m_npcThreatCylinder;
//				range.m_radius += pSettings->m_extraSafeFromThreatCylinder.m_radius;
//				range.m_halfHeight += pSettings->m_extraSafeFromThreatCylinder.m_halfHeight;
//
//				if (InThreatRange(selfPos, threatPos, range))
//					return false;
//			}
//		}
//
//		return true;
//	}
//};
//STATE_REGISTER(SimpleBird, AvoidThreat, kPriorityNormal);

/// --------------------------------------------------------------------------------------------------------------- ///
class SimpleBird::AvoidDanger : public SimpleBird::MoveState
{
	typedef SimpleBird::MoveState ParentClass;

public:
	BIND_TO_PROCESS(SimpleBird);

	virtual void Enter() override
	{
		ParentClass::Enter();

		SimpleBird& self = Self();
		StringId64 selfId = self.GetUserId();
		SendEvent(SID("release-location"), self.m_hPerchMgr, selfId);

		// set initial facing.
		const Point initPos = self.GetTranslation();
		const Point targetPos = self.m_danger.m_escapePosWs;
		const Vector currFacing = GetLocalZ(self.GetRotation());
		const Vector desiredFacingXZ = SafeNormalize(targetPos - initPos, currFacing);
		const Quat desiredRot = QuatFromXZDir(desiredFacingXZ);
		const Locator targetLoc = Locator(targetPos, desiredRot);

		PrepareFly(targetLoc, true, false);
		BeginFlyTo();
	}

	virtual void Exit() override
	{
		ParentClass::Exit();

		SimpleBird& self = Self();
		self.m_flyPath.ClearPath();
	}

	virtual void Update() override
	{
		ParentClass::Update();

		SimpleBird& self = Self();
		UpdateFlyTo();
		if (IsFlyToDone())
		{
			//float waitTime = RandomFloatRange(0.0f, 1.0f);
			//self.GoWaitTimerState(SID("FlyAround"), waitTime);

			KillProcess(&self);
		}
	}
};
STATE_REGISTER(SimpleBird, AvoidDanger, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
//class SimpleBird::FlyAround : public SimpleBird::MoveState
//{
//	typedef SimpleBird::MoveState ParentClass;
//
//	float m_duration;
//
//public:
//	BIND_TO_PROCESS(SimpleBird);
//
//	virtual void Enter() override
//	{
//		ParentClass::Enter();
//
//		SimpleBird& self = Self();
//		self.PlayAnim(SID("flap"), INVALID_STRING_ID_64, true);
//
//		m_duration = RandomFloatRange(15.0f, 20.0f);
//
//		StringId64 settingsId = SID("*flocking-bird-perch*");
//		self.m_flockingCtrl.Init(settingsId);
//		self.m_flockingCtrl.SetVelocity(self.m_currVel);
//		self.m_flockingCtrl.Enable();
//
//		AddFlockingBoid(&self, self.m_hPerchMgr.ToProcess()->GetSpawner()->NameId());
//
//		m_currPitchAngleRad = 0.0f;
//		m_pitchTracker.Reset();
//	}
//
//	virtual void Exit() override
//	{
//		SimpleBird& self = Self();
//		self.m_flockingCtrl.Disable();
//		RemoveFlockingBoid(&self);
//	}
//
//	virtual void Update() override
//	{
//		ParentClass::Update();
//
//		SimpleBird& self = Self();
//
//		const float elapsedTime = (self.GetCurTime() - m_startTime).ToSeconds();
//		const float dt = self.GetClock()->GetDeltaTimeInSeconds();
//
//		const Vector currFacing = GetLocalZ(self.GetRotation());
//		const Vector desiredFacingXZ = SafeNormalize(self.m_flockingCtrl.GetVelocity(), currFacing);
//
//		m_currPitchAngleRad = 0.f;
//
//		const Quat velRot = QuatFromXZDir(desiredFacingXZ);
//		const Quat pitchRot = QuatFromAxisAngle(-Vector(kUnitXAxis), m_currPitchAngleRad);
//		const Quat finalRot = velRot * pitchRot;
//		self.SetRotation(finalRot);
//
//		if (!self.m_danger.m_active)
//			self.GoFlyToPerchState(true, false);
//	}
//};
//
//STATE_REGISTER(SimpleBird, FlyAround, kPriorityNormal);

///------------------------------------------------------------------------------------------------------------------///
U32F SimpleBird::GetMaxStateAllocSize()
{
	//return Max(Max(sizeof(SimpleBird::Active), sizeof(SimpleBird::FlyToPerch)), sizeof(SimpleBird::AvoidThreat));
	return Max(sizeof(SimpleBird::Active), sizeof(SimpleBird::FlyToPerch));
}

///------------------------------------------------------------------------------------------------------------------///
/// Script funcs
///------------------------------------------------------------------------------------------------------------------///
SCRIPT_FUNC("alloc-bird-fly-anim-by-duration", DcAllocBirdFlyAnimByDuration)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	StringId64 animName = args.NextStringId();
	float duration = args.NextFloat();
	float desiredSpeed = args.NextFloat();
	float speedFactor = args.NextFloat();
	StringId64 upAnimName = args.NextStringId();

	DC::BirdFlyAnim* pAnim = NDI_NEW(kAllocSingleGameFrame, kAlign16) DC::BirdFlyAnim;
	memset(pAnim, 0, sizeof(DC::BirdFlyAnim));
	pAnim->m_signature = SID("bird-fly-anim");
	pAnim->m_name = animName;
	pAnim->m_duration = duration;
	pAnim->m_desiredSpeed = desiredSpeed;
	pAnim->m_speedSpring = speedFactor;
	pAnim->m_blendTime = 0.5f;
	pAnim->m_upName = upAnimName;

	(void)args.AllocSucceeded(pAnim);
	return ScriptValue(pAnim);
}

///------------------------------------------------------------------------------------------------------------------///
SCRIPT_FUNC("alloc-bird-fly-anim-by-loop-time", DcAllocBirdFlyAnimByLoopTime)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	StringId64 animName = args.NextStringId();
	float loopTime = args.NextI32();
	float desiredSpeed = args.NextFloat();
	float speedFactor = args.NextFloat();
	StringId64 upAnimName = args.NextStringId();

	DC::BirdFlyAnim* pAnim = NDI_NEW(kAllocSingleGameFrame, kAlign16) DC::BirdFlyAnim;
	memset(pAnim, 0, sizeof(DC::BirdFlyAnim));
	pAnim->m_signature = SID("bird-fly-anim");
	pAnim->m_name = animName;
	pAnim->m_loopTime = loopTime;
	pAnim->m_desiredSpeed = desiredSpeed;
	pAnim->m_speedSpring = speedFactor;
	pAnim->m_blendTime = 0.5f;
	pAnim->m_upName = upAnimName;

	(void)args.AllocSucceeded(pAnim);
	return ScriptValue(pAnim);
}

///------------------------------------------------------------------------------------------------------------------///
SCRIPT_FUNC("alloc-bird-landing-anim", DcAllocBirdLandingAnim)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	StringId64 animId = args.NextStringId();
	float distToDest = args.NextFloat();

	DC::BirdLandingAnim* pAnim = NDI_NEW(kAllocSingleGameFrame, kAlign16) DC::BirdLandingAnim;
	memset(pAnim, 0, sizeof(DC::BirdLandingAnim));
	pAnim->m_signature = SID("bird-landing-anim");
	pAnim->m_name = animId;
	pAnim->m_distToDest = distToDest;

	return ScriptValue(pAnim);
}
