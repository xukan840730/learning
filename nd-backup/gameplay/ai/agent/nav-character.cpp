/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/agent/nav-character.h"

#include "corelib/math/basicmath.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/msg-mem.h"

#include "ndlib/anim/anim-cmd-generator-layer.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/character-speech-anim.h"
#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"
#include "ndlib/anim/joint-modifiers/joint-modifiers.h"
#include "ndlib/anim/nd-anim-align-util.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/camera/camera-final.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/fx/fxmgr.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/ndphys/rigid-body-base.h"
#include "ndlib/net/nd-net-anim-command-generator-base.h"
#include "ndlib/net/nd-net-game-manager.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/net/nd-net-player-tracker.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/fg-draw-mgr.h"
#include "ndlib/render/look.h"
#include "ndlib/render/ngen/meshraycaster.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"
#include "ndlib/util/finite-state-machine.h"
#include "ndlib/util/graph-display.h"
#include "ndlib/water/waterflow.h"

#include "gamelib/anim/anim-layer-ik.h"
#include "gamelib/anim/blinking.h"
#include "gamelib/anim/gesture-controller.h"
#include "gamelib/anim/motion-matching/pose-tracker.h"
#include "gamelib/audio/nd-vox-controller-base.h"
#include "gamelib/debug/ai-msg-log.h"
#include "gamelib/fx/splashers.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/ai/component/nd-ai-anim-config.h"
#include "gamelib/gameplay/ai/component/nd-scripted-animation.h"
#include "gamelib/gameplay/ai/controller/action-pack-controller.h"
#include "gamelib/gameplay/ai/controller/nd-animation-controllers.h"
#include "gamelib/gameplay/ai/controller/nd-climb-controller.h"
#include "gamelib/gameplay/ai/controller/nd-hit-controller.h"
#include "gamelib/gameplay/ai/controller/nd-traversal-controller.h"
#include "gamelib/gameplay/ai/controller/nd-weapon-controller.h"
#include "gamelib/gameplay/character-leg-raycaster.h"
#include "gamelib/gameplay/faction-mgr.h"
#include "gamelib/gameplay/ground-hug.h"
#include "gamelib/gameplay/health-system.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-defines.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/nav-state-machine.h"
#include "gamelib/gameplay/nav/path-waypoints-ex.h"
#include "gamelib/gameplay/nav/platform-control.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/gameplay/nd-attachable-object.h"
#include "gamelib/gameplay/nd-effect-control.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-locatable.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/level/level.h"
#include "gamelib/ndphys/collision-cast-interface.h"
#include "gamelib/ndphys/collision-cast.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/composite-ragdoll-controller.h"
#include "gamelib/ndphys/simple-collision-cast.h"
#include "gamelib/ndphys/water-detector.h"
#include "gamelib/region/region-control.h"
#include "gamelib/scriptx/h/anim-nav-character-info.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "gamelib/scriptx/h/npc-demeanor-defines.h"
#include "gamelib/scriptx/h/vox-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
#define NAVCHAR_ASSERT(x) ANIM_ASSERTF((x), ("NavChar %s", GetName()))
#define NAVCHAR_ASSERTF ANIM_ASSERTF

PROCESS_REGISTER_ABSTRACT(NavCharacter, Character);
FROM_PROCESS_DEFINE(NavCharacter);

PROCESS_SNAPSHOT_DEFINE(NavCharacterSnapshot, CharacterSnapshot);

NavCharacter::PFnAllocateDebugMem	NavCharacter::s_pFnAllocDebugMem = nullptr;
NavCharacter::PFnFreeDebugMem		NavCharacter::s_pFnFreeDebugMem = nullptr;

bool g_cheapUseZeroRadii = true;

/// --------------------------------------------------------------------------------------------------------------- ///
const ActionPackResolveInput MakeDefaultResolveInput(const NavCharacterAdapter& adapter) // @SNPCTAP rewrite for SNPC
{
	const NavCharacter* pNavChar = adapter.ToNavCharacter();
	if (pNavChar)
	{
		ActionPackResolveInput input;
		input.m_frame = pNavChar->GetBoundFrame();

		input.m_moving		  = pNavChar->IsMoving();
		input.m_motionType	  = pNavChar->GetCurrentMotionType();
		input.m_mtSubcategory = pNavChar->GetRequestedMtSubcategory();
		input.m_velocityPs	  = pNavChar->GetVelocityPs();
		input.m_strafing	  = pNavChar->IsStrafing();

		if (input.m_motionType == kMotionTypeMax)
		{
			input.m_motionType = pNavChar->GetRequestedMotionType();
		}

		if (!input.m_moving)
		{
			const StringId64 nsmStateId = pNavChar->GetNavStateMachine()->GetStateId();
			switch (nsmStateId.GetValue())
			{
			case SID_VAL("MovingAlongPath"):
			case SID_VAL("SteeringAlongPath"):
				input.m_moving = true;
				input.m_motionType = pNavChar->GetRequestedMotionType();
				input.m_strafing = pNavChar->IsStrafing();
				break;
			}
		}

		return input;
	}
	else
	{
		const SimpleNavCharacter* pSNavChar = adapter.ToSimpleNavCharacter();
		ANIM_ASSERT(pSNavChar);
		extern const ActionPackResolveInput MakeDefaultResolveInput(const SimpleNavCharacter* pNavChar);
		return MakeDefaultResolveInput(pSNavChar);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavCharacter::NavCharacter()
	: m_pNetAnimCmdGenerator(nullptr)
	, m_wasMovementConstrained(false)
	, m_allowAdjustHeadToNav(!g_navCharOptions.m_disableAdjustHeadToNav)
	, m_disableAdjustToCharBlockers(false)
	, m_keepCollisionCapsulesIfNotRagdolling(true)
	, m_killIfCantRagdoll(false)
	, m_forceRagdollOnExplosionDeath(false)
	, m_ragdollStumble(false)
	, m_diedInVehicle(false)
	, m_vehicleDeathRagdollEff(false)
	, m_requestSnapToGround(false)
	, m_requestPushPlayer(false)
	, m_pathThroughPlayer(false)
	, m_disablePlayerNpcCollision(false)
	, m_useNaturalFacePosition(true)
	, m_isWading(false)
	, m_suppressGroundProbeFilter(false)
	, m_disableNavigationRefCount(0)
	, m_pNavControl(nullptr)
	, m_pNavStateMachine(nullptr)
	, m_pAnimationControllers(nullptr)
	, m_pJointModifiers(nullptr)
	, m_pWetPoints(nullptr)
	, m_pWaterDetectors(nullptr)
	, m_pWaterDetectorAlign(nullptr)
	, m_pLogMem(nullptr)
	, m_pScriptLogger(nullptr)
	, m_pChannelLog(nullptr)
	, m_requestedMotionType(kMotionTypeWalk)
	, m_adjustToOtherNpcsTime(0.0f)
	, m_adjustToOtherNpcsDelayTime(0.0f)
	, m_adjustJointsToNavSmoothing(1.0f)
	, m_fallSpeed(0.0f)
	, m_interCharDepenRadius(-1.0f)
	, m_delayRagdollTime(-1.0f)
	, m_currentPat(0)
	, m_ladderPat(0)
	, m_groundNormalPs(kUnitYAxis)
	, m_minGroundAdjustSpeed(0.0f)
	, m_pScriptedAnimation(nullptr)
	, m_aimOriginAttachIndex(0)
	, m_lookAttachIndex(0)
	, m_desiredFacePositionPs(POINT_LC(0.0f, 0.0f, 10000.0f))
	, m_handsProbesShot(false)
	, m_pCharacterSpeechAnim(nullptr)
	, m_numDemeanors(0)
	, m_demeanorMatrixId(INVALID_STRING_ID_64)
	, m_animConfigMatrixId(INVALID_STRING_ID_64)
{
	m_adjustToNavIndex[0] = AttachIndex::kInvalid;
	m_adjustToNavIndex[1] = AttachIndex::kInvalid;

	m_pusherPlanePs = Plane(Vector(kZero), kZero);
	// Setup the speed buffer
	Vector* pVelocityBuffer = NDI_NEW Vector[4];
	m_velocityPsHistory.Init(pVelocityBuffer, 4);
	m_smoothedVelocityPs = kZero;

	for (U32F i = 0; i < kMaxDemeanorSlots; ++i)
	{
		m_demeanorDefinitions[i] = nullptr;
	}

	m_ankleInfoLs.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavCharacter::~NavCharacter()
{
	PROFILE(Processes, NavCharacterDestructor);

	if (m_pAnimationControllers)
	{
		m_pAnimationControllers->Destroy();
		m_pAnimationControllers = nullptr;
	}

	if (m_pJointModifiers)
	{
		m_pJointModifiers->Destroy();
		delete m_pJointModifiers;
		m_pJointModifiers = nullptr;
	}

	if (m_pWetPoints && m_pWaterDetectors)
	{
		I32F numWetPoints = 0;
		for (I32F i = 0; m_pWetPoints[i].m_jointA != INVALID_STRING_ID_64; i++)
		{
			++numWetPoints;
		}

		for (I32F i = 0; i < (numWetPoints * 2); ++i)
		{
			m_pWaterDetectors[i].Destroy();
		}

		delete[] m_pWaterDetectors;
		m_pWaterDetectors = nullptr;
	}

	if (m_pNavControl)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		m_pNavControl->ResetNavMesh(nullptr);
	}

	if (m_pNavStateMachine)
	{
		m_pNavStateMachine->Shutdown();
		m_pNavStateMachine = nullptr;
	}

	if (m_pNetAnimCmdGenerator)
	{
		NDI_DELETE m_pNetAnimCmdGenerator;
		m_pNetAnimCmdGenerator = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NavCharacter::Init(const ProcessSpawnInfo& info)
{
	PROFILE(AI, NavChar_Init);

	MEMTRACK_BEGIN(20);

	m_pSubsystemMgr = NDI_NEW NdSubsystemMgr;
	m_pSubsystemMgr->Init(this, 24, 24);
	MEMTRACK_SAMPLE("SubsystemMgr->Init");

	m_inflateRadius = 0.0f;

	m_shouldUpdateRegionControl = true;

	m_pLogMem = nullptr;

	if (s_pFnAllocDebugMem)
	{
		PROFILE(AI, NavChar_GetDebugMem);
		U32 debugMemSize;

		m_pLogMem = s_pFnAllocDebugMem(&debugMemSize, NavCharacter::kDebugMemCharLog);

		if (m_pLogMem)
		{
			const static U32 kMaxNuggets = 3072;
			const U32 nuggetMaxSize		 = (kMaxNuggets * sizeof(MsgNugget));
			const U32 nuggetBufSize		 = AlignSize(Min(nuggetMaxSize, (U32)(0.1f * debugMemSize)), kAlign8);
			const U32F doutMemSize		 = AlignSize(debugMemSize - nuggetBufSize, kAlign8);

			DoutMem* dOut = NDI_NEW DoutMem("NavCharDebug", static_cast<char*>(m_pLogMem), doutMemSize);
			m_pChannelLog = NDI_NEW DoutMemChannels(dOut, static_cast<char*>(m_pLogMem) + doutMemSize, debugMemSize - doutMemSize);
		}

		m_pScriptLogger = NDI_NEW AiScriptLogger();
		m_pScriptLogger->Init();

		Log(this, kNpcLogChannelGeneral, "NavChar::Init()\n");
	}
	MEMTRACK_SAMPLE("FnAllocDebugMem");

	m_motionConfig = GetDefaultMotionConfig();

	m_pAnimConfig = NDI_NEW NdAiAnimationConfig;
	MEMTRACK_SAMPLE("NdAiAnimationConfig");

	m_groundAdjustment = kZero;
	m_waterAdjustmentWs = kZero;

	m_lastUpdatedBlockedApsPos = GetTranslation() + Vector(1.0f, 1.0f, 1.0f);
	m_lastUpdatedBlockedApsTime = TimeFrameNegInfinity();

	const Err animRes = ConfigureAnimParams(true);
	if (animRes.Failed())
		return animRes;
	MEMTRACK_SAMPLE("AnimParams");

	Err result = ParentClass::Init(info);
	if (result.Failed())
		return result;
	MEMTRACK_SAMPLE("ParentClass");

	if (GetSkeletonId() == INVALID_SKELETON_ID)
		return Err::kErrSkeletonNotLoaded;

	AnimControl* pAnimControl = GetAnimControl();
	if (!pAnimControl)
		return Err::kErrAnimActorLoginFailed;

	Log(this, kNpcLogChannelGeneral, "... name: %s (user id: %s)\n", GetName(), DevKitOnly_StringIdToString(GetUserId()));
	if (const EntitySpawner* pSpawner = GetSpawner())
	{
		Log(this, kNpcLogChannelGeneral, "... spawner name: %s\n", pSpawner->Name());
	}
	Log(this, kNpcLogChannelGeneral, "... look name: %s\n", DevKitOnly_StringIdToString(GetLookId()));
	Log(this, kNpcLogChannelGeneral, "... skel name: %s\n", ResourceTable::LookupSkelName(GetSkeletonId()));

	//
	// during NdLocatableObject::Init, we may become bound to an incorrect rigid body because
	// NdLocatableObject::Init cannot call the overriden BindToRigidBody or it would
	// crash in Npc::EnterNewParentSpace
	//
	if (const RigidBody* pBindBody = GetBoundRigidBody())
	{
		if (const NdGameObject* pBindGo = pBindBody->GetOwner())
		{
			if (const PlatformControl* pPlatCon = pBindGo->GetPlatformControl())
			{
				const RigidBody* pDesiredBindBody = pPlatCon->GetBindTarget();
				if (pDesiredBindBody != pBindBody)
				{
					Log(this, kNpcLogChannelGeneral, "NavChar::Init - binding to platform %s\n", DevKitOnly_StringIdToString(GetBindSpawnerId()));
					// avoid crashing in Npc::EnterNewParentSpace due to being partially initialized
					NdLocatableObject::BindToRigidBody(pDesiredBindBody);
				}
			}
		}
	}

	MEMTRACK_SAMPLE("Misc");

	m_pScriptedAnimation = CreateScriptedAnimation();
	MEMTRACK_SAMPLE("ScriptedAnimation");

	const Point myPosPs = GetTranslationPs();
	const Vector forwardPs = GetLocalZ(GetRotationPs());
	m_desiredFacePositionPs = myPosPs + forwardPs * SCALAR_LC(100.0f);
	NAVCHAR_ASSERT(IsReasonable(m_desiredFacePositionPs));

	m_groundPositionPs = myPosPs;
	m_groundNormalPs   = kUnitYAxis;

	ResetGroundFilter();

	m_currentPat.m_bits = 1; // assume we are on ground until we get raycast results

	m_allowFall = true;
	m_requestSnapToGround = false;
	m_secondsFalling = 0.0f;
	m_secondsNoNavMesh = 0.0f;

	if (ConfiguredForNavigation())
	{
		m_pNavControl = NDI_NEW NavControl;

		if (m_pNavControl)
		{
			m_pNavControl->SetIgnoreNavBlocker(0, this); // by default ignore own nav blockers
		}

		SetupNavControl(m_pNavControl);
	}
	MEMTRACK_SAMPLE("SetupNavControl et al");

	m_pNavStateMachine = NDI_NEW NavStateMachine;
	MEMTRACK_SAMPLE("NavStateMachine");

	if (NavControl* pNavCon = GetNavControl())
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		pNavCon->ResetNavMesh(nullptr, this);

		const Point navPosPs = GetTranslationPs();

		pNavCon->UpdatePs(navPosPs, this);

		// add all traversal skills by default
		pNavCon->SetTraversalSkillMask(GetDefaultTraversalSkillMask());
	}

	m_depenetrationOffsetFw = 0.0f;
	m_depenetrationOffsetBw = 0.0f;
	m_depenetrationSegmentScale = 1.0f;
	m_depenetrationSegmentScaleScriptTarget = -1.0f;
	m_depenSegmentInScriptedOverride = false;

	m_lookAttachIndex	   = AttachIndex::kInvalid;
	m_aimOriginAttachIndex = AttachIndex::kInvalid;

	if (AttachSystem* pAs = GetAttachSystem())
	{
		pAs->FindPointIndexById(&m_lookAttachIndex, SID("targEye"));
		pAs->FindPointIndexById(&m_aimOriginAttachIndex, SID("targRShoulder"));
	}

	m_aimOriginOs = kOrigin;
	m_lookOriginOs = kIdentity;
	m_animDesiredAlignPs = kIdentity;
	m_desiredLocPs = kIdentity;

	m_curInjuryLevel = DC::kAiInjuryLevelNone;

	const Err res = InitJointLimits();
	if (res.Failed())
		return res;

	MEMTRACK_SAMPLE("JointLimits");

	{
		PROFILE(AI, CreateAnimControllers);
		m_pAnimationControllers = CreateAnimationControllersCollection();
		MEMTRACK_SAMPLE("AnimControllersColl");

		// Allocate and initialize all anim action controllers
		CreateAnimationControllers();
		MEMTRACK_SAMPLE("AnimControllers");

		result = ConfigureDemeanor(true);

		if (result.Failed())
			return result;

		m_pAnimationControllers->Init(this, GetNavControl());
		MEMTRACK_SAMPLE("AnimControllersInit");

		result = ConfigureCharacter();
		if (result.Failed())
			return result;
		MEMTRACK_SAMPLE("ConfigureCharacter");

		// The character is configured properly. Now we can setup the anim actor and select the initial start state and animations
		result = pAnimControl->ConfigureStartState(GetAnimControlStartState(), UseRandomIdlePhaseOnSpawn());
		if (result.Failed())
			return result;

		if (AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer())
		{
			SetDefaultInstanceCallbacks(pBaseLayer);

			pBaseLayer->SetLayerBlendCallbacks(AnimStateLayer_BlendedIkCb_PreBlend,
											   AnimStateLayer_BlendedIkCb_PostBlend);
		}
	}

	NAV_ASSERT(m_numDemeanors != 0); // a derived class must have installed a demeanor definitions array by this point

	MEMTRACK_SAMPLE("Misc2");

	{
		PROFILE(AI, CreateJointModifiers);
		m_pJointModifiers = NDI_NEW JointModifiers();
		AI_ASSERT(m_pJointModifiers);
		if (m_pJointModifiers)
		{
			CreateJointModifiers(info);
			m_pJointModifiers->Init(this);
		}
	}
	MEMTRACK_SAMPLE("JointModifiers");

	g_fxMgr.SetWetMaskValue(GetWetMaskIndex(), 0.0f);

	{
		PROFILE(AI, NavSMInit);
		m_pNavStateMachine->Init(GetNdAnimationControllers(), GetNavControl(), this);
	}
	MEMTRACK_SAMPLE("NavStateMachine->Init");

	m_lastEyeAnimRotWs = GetRotation();

	m_ragdollStumble = false;

	m_prevLocatorPs = GetLocatorPs();

	if (DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>())
	{
		const Locator loc = GetLocator();
		const AttachSystem* pAs = GetAttachSystem();

		AttachIndex ind;
		pAs->FindPointIndexById(&ind, SID("targRAnkle"));
		Locator ankleR = pAs->GetLocator(ind);
		pAs->FindPointIndexById(&ind, SID("targLAnkle"));
		Locator ankleL = pAs->GetLocator(ind);

		const Point newLAnklePosLs = loc.UntransformPoint(ankleL.Pos());
		const Point newRAnklePosLs = loc.UntransformPoint(ankleR.Pos());

		const Vector groundNormalOs = GetLocatorPs().UntransformVector(GetGroundNormalPs());
		m_ankleInfoLs.Update(newLAnklePosLs, newRAnklePosLs, 0.0f, groundNormalOs, &pInfo->m_anklesLs);
	}

	if (CanSwim())
	{
		m_pWaterDetectorAlign = NDI_NEW WaterDetector;
		m_pWaterDetectorAlign->SetDisplacementType(WaterDetectorDisplacement::Type::kXzAndHeight);
	}

	m_pPoseTracker = (PoseTracker*)NdSubsystem::Create(NdSubsystem::Alloc::kParentInit, SubsystemSpawnInfo(SID("PoseTracker"), this), FILE_LINE_FUNC);
	MEMTRACK_SAMPLE("PostTrackerSubsys");

	// @todo: fix push player away for net, currently references player which is incorrect in MP.
	if (g_ndConfig.m_pNetInfo->IsNetActive())
	{
		DisablePushPlayerAway();
	}

	const INavAnimController* pNavAnimController = GetActiveNavAnimController();

	if (NdAnimationControllers* pAnimControllers = GetNdAnimationControllers())
	{
		pAnimControllers->InterruptNavControllers(pNavAnimController);
	}

	ResetInterCharacterDepenRadius();

	m_disableAdjustToCharBlockers = info.GetData<bool>(SID("disable-adjust-to-char-blockers"), m_disableAdjustToCharBlockers);

	MEMTRACK_END("NavChar");

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NavCharacter::ConfigureDemeanor(bool initialConfigure)
{
	AnimControl* pAnimControl = GetAnimControl();
	AnimTable& animTable = pAnimControl->GetAnimTable();
	const I32F demeanorIndex = GetCurrentDemeanor().ToI32();
	const I32F numDemeanors = GetNumDemeanors();

	if (demeanorIndex >= numDemeanors || demeanorIndex < 0)
		return Err::kErrAbort;

	const DC::NpcDemeanorDef** ppDemeanorDefs = GetDemeanorDefinitions();
	if (!ppDemeanorDefs)
		return Err::kErrAbort;

	const DC::NpcDemeanorDef* pDemeanorDef = ppDemeanorDefs[demeanorIndex];

	if (!pDemeanorDef)
		return Err::kErrAbort;

	// Overlays
	if (AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays())
	{
		const DC::NpcDemeanorDef* pPrevDef = GetPreviousDemeanorDef();

		if (pPrevDef && (pPrevDef != pDemeanorDef))
		{
			for (I32F iOverlay = 0; iOverlay < pPrevDef->m_overlayListLength; ++iOverlay)
			{
				const StringId64 overlayId = pPrevDef->m_overlayList[iOverlay];
				const DC::AnimOverlaySet* pSet = ScriptManager::Lookup<DC::AnimOverlaySet>(overlayId, nullptr);

				if (!pSet)
					continue;

				pOverlays->ClearLayer(pSet->m_layerId);
			}
		}

		for (U32F i = 0; i < pDemeanorDef->m_overlayListLength; ++i)
		{
			const StringId64 overlaySetId = pDemeanorDef->m_overlayList[i];
			if (!pOverlays->SetOverlaySet(overlaySetId, true))
			{
				MsgWarn("Npc '%s' is missing overlay set '%s' for demeanor '%s'\n",
						GetName(),
						pDemeanorDef->m_name.m_string.GetString(),
						DevKitOnly_StringIdToString(overlaySetId));
			}
		}
	}

	// Set the blend overlay
	const StringId64 animBlendOverlay = GetCurrentDemeanorDef()->m_animBlendOverlay;
	if (animBlendOverlay != INVALID_STRING_ID_64)
	{
		pAnimControl->SetCustomStateBlendOverlay(animBlendOverlay);
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NavCharacter::ConfigureCharacter()
{
	const Err animRes = ConfigureAnimParams(false);
	if (animRes.Failed())
		return animRes;

	const Err demRes = ConfigureDemeanor(false);
	if (demRes.Failed())
		return demRes;

	const Demeanor curDemeanor = GetCurrentDemeanor();
	const DC::NpcDemeanorDef* pDemeanorDef = GetCurrentDemeanorDef();
	NdAnimationControllers* pAnimControllers = GetNdAnimationControllers();
	NdAiAnimationConfig* pAnimConfig = GetAnimConfig();

	float movingNavAdjustRadius = 0.35f;
	float idleNavAdjustRadius	= -1.0f;
	float pathFindRadius		= -1.0f;

	if (pDemeanorDef)
	{
		movingNavAdjustRadius = pDemeanorDef->m_navAdjRadius;
		idleNavAdjustRadius	  = pDemeanorDef->m_idleNavAdjRadius;
		pathFindRadius		  = pDemeanorDef->m_pathFindRadius;
	}

	if (IsCheap() && g_cheapUseZeroRadii)
	{
		movingNavAdjustRadius = idleNavAdjustRadius = 0.0f;
		pathFindRadius = 0.5f;
	}

	if (NavControl* pNavControl = GetNavControl())
	{
		pNavControl->ConfigureNavRadii(movingNavAdjustRadius, idleNavAdjustRadius, pathFindRadius);
	}

	pAnimControllers->ConfigureCharacter(curDemeanor, pDemeanorDef, pAnimConfig);

	if (BlinkController* pBlinkController = GetBlinkController())
	{
		pBlinkController->ChangeSettings(pDemeanorDef->m_blinkSettings);
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ConvertToFromCheap()
{
	ParentClass::ConvertToFromCheap();

	if (INavAnimController* pNavController = GetActiveNavAnimController())
	{
		pNavController->ConvertToFromCheap();
	}

	ConfigureCharacter();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::DefineDemeanors(const DC::NpcDemeanorDef* const* ppDemeanorDefinitions, U32F numDemeanors)
{
	AI_ASSERT(ppDemeanorDefinitions);
	AI_ASSERT(numDemeanors <= kMaxDemeanorSlots);

	m_numDemeanors = numDemeanors;

	for (U32F i = 0; i < numDemeanors; ++i)
	{
		m_demeanorDefinitions[i] = ppDemeanorDefinitions[i];
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::NpcDemeanorDef* NavCharacter::GetCurrentDemeanorDef() const
{
	const DC::NpcDemeanorDef* pDef = nullptr;

	const I32F demeanorIndex = GetCurrentDemeanor().ToI32();

	if (demeanorIndex < m_numDemeanors && demeanorIndex >= 0)
	{
		pDef = m_demeanorDefinitions[demeanorIndex];
	}

	return pDef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::NpcDemeanorDef* NavCharacter::GetPreviousDemeanorDef() const
{
	const DC::NpcDemeanorDef* pDef = nullptr;

	const I32F demeanorIndex = GetPreviousDemeanor().ToI32();

	if (demeanorIndex < m_numDemeanors && demeanorIndex >= 0)
	{
		pDef = m_demeanorDefinitions[demeanorIndex];
	}

	return pDef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavCharacter::GetNumDemeanors() const
{
	return m_numDemeanors;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* NavCharacter::GetDemeanorName(Demeanor dem) const
{
	const I32F demeanorIndex = dem.ToI32();
	NAV_ASSERT(demeanorIndex < m_numDemeanors && demeanorIndex >= 0);

	if (demeanorIndex < m_numDemeanors && demeanorIndex >= 0)
	{
		if (m_demeanorDefinitions[demeanorIndex])
			return m_demeanorDefinitions[demeanorIndex]->m_name.m_string;
	}

	return "<EMPTY>";
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NavCharacter::GetDemeanorId(Demeanor dem) const
{
	const I32F demeanorIndex = dem.ToI32();
	NAV_ASSERT(demeanorIndex < m_numDemeanors && demeanorIndex >= 0);

	if (demeanorIndex < m_numDemeanors && demeanorIndex >= 0)
	{
		if (m_demeanorDefinitions[demeanorIndex])
			return m_demeanorDefinitions[demeanorIndex]->m_name.m_symbol;
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NavCharacter::GetDemeanorCategory(Demeanor dem) const
{
	const I32F demeanorIndex = dem.ToI32();
	NAV_ASSERT(demeanorIndex < m_numDemeanors && demeanorIndex >= 0);

	if (demeanorIndex < m_numDemeanors && demeanorIndex >= 0)
	{
		if (m_demeanorDefinitions[demeanorIndex])
			return m_demeanorDefinitions[demeanorIndex]->m_category;
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NavCharacter::SetupAnimControl(AnimControl* pAnimControl)
{
	const Err overlayResult = SetupAnimOverlays(pAnimControl);
	if (overlayResult != Err::kOK)
	{
		return overlayResult;
	}

	if (AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays())
	{
		const SkeletonId skelId = GetSkeletonId();
		const char* skelName = ResourceTable::LookupSkelName(skelId);

		if (skelName)
		{
			StringBuilder<1024> overlaySetStr("anim-overlay-%s-skel-%s", GetOverlayBaseName(), skelName);

			const StringId64 overlaySetId = StringToStringId64(overlaySetStr.c_str());
			pOverlays->SetOverlaySet(overlaySetId, true);
		}

		ApplyCharacterOverlays(pOverlays);
	}

	Err result = pAnimControl->SetupAnimActor(SID("anim-npc"));
	if (result.Failed())
		return result;

	// Allocate the layers
	AllocateAnimationLayers(pAnimControl);

	CreateAnimationLayers(pAnimControl);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::OnKillProcess()
{
	if (m_pLogMem)
	{
		if (s_pFnFreeDebugMem)
		{
			s_pFnFreeDebugMem(m_pLogMem, kDebugMemCharLog);
		}

		m_pLogMem = nullptr;
		m_pChannelLog = nullptr;

	}

	if (m_pScriptLogger)
	{
		m_pScriptLogger->FreeMemory();
	}

	ParentClass::OnKillProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::CaresAboutPlayerBlockage(const ActionPack* pActionPack) const
{
	const FactionId playerFactionId = EngineComponents::GetNdGameInfo()->GetPlayerFaction();
	const bool enemyOfThePlayer = g_factionMgr.IsEnemy(GetFactionId(), playerFactionId);

	if (enemyOfThePlayer)
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pAnimConfig, deltaPos, lowerBound, upperBound);

	DeepRelocatePointer(m_pNavControl, deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pNavStateMachine, deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pAnimationControllers, deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pJointModifiers, deltaPos, lowerBound, upperBound);

	RelocatePointer(m_pWaterDetectors, deltaPos, lowerBound, upperBound);

	RelocatePointer(m_pWaterDetectorAlign, deltaPos, lowerBound, upperBound);

	DeepRelocatePointer(m_pNetAnimCmdGenerator, deltaPos, lowerBound, upperBound);

	DeepRelocatePointer(m_pScriptLogger, deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pChannelLog, deltaPos, lowerBound, upperBound);

	RelocatePointer(m_pScriptedAnimation, deltaPos, lowerBound, upperBound);

	m_velocityPsHistory.Relocate(deltaPos, lowerBound, upperBound);

	DeepRelocatePointer(m_pCharacterSpeechAnim, deltaPos, lowerBound, upperBound);

	DeepRelocatePointer(m_pPoseTracker, deltaPos, lowerBound, upperBound);

	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* NavCharacter::GetOverlayBaseName() const
{
	if (const NdAiAnimationConfig* pAnimConfig = GetAnimConfig())
	{
		return pAnimConfig->GetOverlayBaseName();
	}

	return "";
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack* NavCharacter::GetCurrentActionPack() const
{
	return m_pNavStateMachine->GetActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack* NavCharacter::GetReservedActionPack() const
{
	if (m_pNavStateMachine->GetActionPackUsageState() != NavStateMachine::kActionPackUsageNone)
		return m_pNavStateMachine->GetActionPack();

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack* NavCharacter::GetEnteredActionPack() const
{
	if (m_pNavStateMachine->GetActionPackUsageState() == NavStateMachine::kActionPackUsageEntered)
		return m_pNavStateMachine->GetActionPack();

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
TraversalActionPack* NavCharacter::GetTraversalActionPack()
{
	return m_pNavStateMachine->GetTraversalActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const TraversalActionPack* NavCharacter::GetTraversalActionPack() const
{
	return m_pNavStateMachine->GetTraversalActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsNextWaypointActionPackEntry() const
{
	return m_pNavStateMachine->IsNextWaypointActionPackEntry();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ActionPack* NavCharacter::GetGoalActionPack() const
{
	return m_pNavStateMachine->GetGoalOrPendingActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
uintptr_t NavCharacter::GetGoalOrPendingApUserData() const
{
	return m_pNavStateMachine->GetGoalOrPendingApUserData();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsActionPackReserved() const
{
	return IsActionPackReserved(m_pNavStateMachine->GetActionPack());
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsActionPackEntered() const
{
	return m_pNavStateMachine && m_pNavStateMachine->IsActionPackEntryComplete();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsEnteringActionPack() const
{
	return m_pNavStateMachine && m_pNavStateMachine->IsEnteringActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsNormalMovementSuppressed() const
{
	return m_pNavStateMachine && m_pNavStateMachine->IsNormalMovementSuppressed();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsActionPackReserved(const ActionPack* pActionPack) const
{
	bool isReserved = false;
	if (pActionPack == m_pNavStateMachine->GetActionPack())
	{
		isReserved = m_pNavStateMachine->GetActionPackUsageState() != NavStateMachine::kActionPackUsageNone;
	}
	return isReserved;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::BindToRigidBody(const RigidBody* pBindTarget)
{
	if (pBindTarget)
	{
		// if we are asked to bind to a game object,
		if (const NdGameObject* pGo = pBindTarget->GetOwner())
		{
			if (!pGo->ForceAllowNpcBindings())
			{
				// if it is a platform, and this bind target has a nav mesh, then allow the binding
				if (const PlatformControl* pPlatCon = pGo->GetPlatformControl())
				{
					const RigidBody* pPlatformBindTarget = pPlatCon->GetBindTarget();
					if (pBindTarget != pPlatformBindTarget)
					{
						// not the right RigidBody for this platform control - don't bind to it!
						pBindTarget = GetBoundRigidBody();
					}
					else if (!pPlatCon->HasRegisteredNavMesh()
#if ENABLE_NAV_LEDGES
						&& !pPlatCon->HasRegisteredNavLedgeGraph()
#endif // ENABLE_NAV_LEDGES
						)
					{
						// platform control has no navmeshes that are enabled - don't bind
						pBindTarget = GetBoundRigidBody();
					}
				}
				else
				{
					// not a platform - don't bind to it!
					pBindTarget = GetBoundRigidBody();
				}
			}
		}
	}

	NdAnimationControllers* pAnimControllers = GetNdAnimationControllers();
	INdAiHitController* pHitController = pAnimControllers ? pAnimControllers->GetHitController() : nullptr;
	const bool hitReactionBusy = pHitController ? pHitController->IsBusy() : false;
	if (!IsDead() && hitReactionBusy)
	{
		// prevent rebinding while hit reactions are playing as a safety precaution
		pBindTarget = GetBoundRigidBody();
	}

	if (!IsSameBindSpace(pBindTarget))
	{
		Locator oldParentSpace = GetParentSpace();
		Locator newParentSpace(kIdentity);
		if (pBindTarget)
		{
			newParentSpace = pBindTarget->GetLocatorCm();
		}
		//Locator invNewParentSpace = Inverse(newParentSpace);
		Locator oldToNewLoc = newParentSpace.UntransformLocator(oldParentSpace);
		Transform mat(oldToNewLoc.Rot(), oldToNewLoc.Pos());
		NdGameObject::BindToRigidBody(pBindTarget);
		EnterNewParentSpace(mat, oldParentSpace, newParentSpace);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::EnterNewParentSpace(const Transform& matOldToNew,
									   const Locator& oldParentSpace,
									   const Locator& newParentSpace)
{
	m_pNavStateMachine->EnterNewParentSpace(matOldToNew, oldParentSpace, newParentSpace);

#if 0
	m_groundPositionPs		 = m_groundPositionPs * matOldToNew;
	m_groundNormalPs		 = m_groundNormalPs * matOldToNew;
	m_filteredGroundPosPs	 = m_filteredGroundPosPs * matOldToNew;
	m_filteredGroundNormalPs = m_filteredGroundNormalPs * matOldToNew;
#else
	m_groundPositionPs = GetTranslationPs();
	m_groundNormalPs = kUnitYAxis;
	ResetGroundFilter();
#endif

	m_desiredFacePositionPs = m_desiredFacePositionPs * matOldToNew;
	NAVCHAR_ASSERT(IsReasonable(m_desiredFacePositionPs));

	LegRaycaster* pLegRaycaster = GetLegRaycaster();
	if (pLegRaycaster)
	{
		pLegRaycaster->EnterNewParentSpace(matOldToNew, oldParentSpace, newParentSpace);
	}

	if (JointModifiers* pJointModifiers = GetJointModifiers())
	{
		pJointModifiers->EnterNewParentSpace(matOldToNew, oldParentSpace, newParentSpace);
	}

	if (NdAnimationControllers* pAnimControllers = GetNdAnimationControllers())
	{
		pAnimControllers->EnterNewParentSpace(matOldToNew, oldParentSpace, newParentSpace);
	}

	if (NdSubsystemMgr* pSubSysMgr = GetSubsystemMgr())
	{
		pSubSysMgr->EnterNewParentSpace(matOldToNew, oldParentSpace, newParentSpace);
	}

	if (ShouldLog())
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		LogHdr(this, kNpcLogChannelNav);
		LogMsg(this,
			   kNpcLogChannelNav,
			   "change of parent space: bound to platform '%s' transformed goalPs",
			   DevKitOnly_StringIdToString(GetBindSpawnerId()));
		LogPoint(this, kNpcLogChannelNav, m_pNavStateMachine->GetFinalDestPointPs());
		LogMsg(this, kNpcLogChannelNav, "\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ResetNavMesh()
{
	if (NavControl* pNavCon = GetNavControl())
	{
		pNavCon->ResetNavMesh(nullptr);

		const Point navPosPs = GetNavigationPosPs();

		pNavCon->UpdatePs(navPosPs, this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ResetNavigation(bool playIdle/* = true*/)
{
	m_pNavStateMachine->Reset(playIdle);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::DisableNavigation(const char* sourceFile, U32F sourceLine, const char* sourceFunc)
{
	AiLogNav(this,
			 "DisableNavigation (ref count=%d) %s:%d:%s\n",
			 (int)m_disableNavigationRefCount + 1,
			 sourceFile,
			 sourceLine,
			 sourceFunc);

	if (m_disableNavigationRefCount == 0)
	{
		Interrupt(sourceFile, sourceLine, sourceFunc);
	}

	++m_disableNavigationRefCount;
	// I don't expect to ever have more than 3 systems disabling navigation simultaneously
	NAV_ASSERT(m_disableNavigationRefCount < 3);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::EnableNavigation(const char* sourceFile, U32F sourceLine, const char* sourceFunc)
{
	AiLogNav(this,
			 "EnableNavigation (ref count=%d) %s:%d:%s\n",
			 (int)m_disableNavigationRefCount,
			 sourceFile,
			 sourceLine,
			 sourceFunc);

	NAV_ASSERT(m_disableNavigationRefCount > 0);
	if (m_disableNavigationRefCount > 0)
	{
		--m_disableNavigationRefCount;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::AbandonInterruptedNavCommand()
{
	if (m_pNavStateMachine)
	{
		m_pNavStateMachine->AbandonInterruptedCommand();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ResetLegIkForTeleport()
{
	JointModifiers* pJointModifiers = GetJointModifiers();

	pJointModifiers->OnTeleport();

	if (JointModifierData* pData = pJointModifiers->GetJointModifierData())
	{
		if (LegIkData* pLegIkData = pData->GetLegIkData())
		{
			pLegIkData->SetDeltaYFromLastFrame(0.0f);
			pLegIkData->SetNaturalAnimNormalOs(kUnitYAxis);
			pLegIkData->ResetProbeResults();
			pLegIkData->SetFirstFrame(true);
		}

		if (JointModifierData::OutputData* pOutputData = pData->GetOutputData())
		{
			pOutputData->m_legIkPersistentData.Reset();
		}
	}

	if (LegRaycaster* pLegRaycaster = GetLegRaycaster())
	{
		pLegRaycaster->ClearResults();
		const TimeFrame minTime = pLegRaycaster->GetClock()->GetCurTime() + Frames(3);
		pLegRaycaster->SetMinValidTime(minTime);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::ShouldConstrainLegs() const
{
	return !IsClimbing() && !IsInScriptedAnimationState();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsNavigationInterrupted() const
{
	return m_pNavStateMachine->IsInterrupted();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsUsingTraversalActionPack() const
{
	if (m_pNavStateMachine->GetActionPackUsageState() == NavStateMachine::kActionPackUsageEntered)
		return m_pNavStateMachine->GetActionPackUsageType() == ActionPack::kTraversalActionPack;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsUsingMultiCinematicActionPack() const
{
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsPlayingWorldRelativeAnim() const
{
	return m_pNavStateMachine->IsPlayingWorldRelativeAnim();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::CanProcessNavCommands() const
{
	return m_pNavStateMachine->CanProcessCommands();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::AddTraversalSkill(U32 skill)
{
	if (NavControl* pNavCon = GetNavControl())
	{
		U32F skillMask = pNavCon->GetTraversalSkillMask();
		skillMask |= (1U << skill);
		pNavCon->SetTraversalSkillMask(skillMask);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::RemoveTraversalSkill(U32 skill)
{
	if (NavControl* pNavCon = GetNavControl())
	{
		U32F skillMask = pNavCon->GetTraversalSkillMask();
		skillMask &= ~(1U << skill);
		pNavCon->SetTraversalSkillMask(skillMask);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::HasTraversalSkill(U32 skill) const
{
	if (const NavControl* pNavCon = GetNavControl())
	{
		U32F skillMask = pNavCon->GetTraversalSkillMask();
		return (skillMask & (1U << skill)) != 0;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::PatchPathStartPs(Point_arg newStartPs)
{
	if (NavStateMachine* pNsm = GetNavStateMachine())
	{
		pNsm->PatchPathStartPs(newStartPs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Nav::StaticBlockageMask NavCharacter::GetObeyedStaticBlockers() const
{
	Nav::StaticBlockageMask obeyed = Nav::kStaticBlockageMaskNone;

	if (const NavControl* pNavControl = GetNavControl())
	{
		obeyed = pNavControl->GetObeyedStaticBlockers();
	}

	return obeyed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::IssueNavCommand(const NavCommand& cmd)
{
	m_pNavStateMachine->AddCommand(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::MoveTo(NavLocation dest,
						  const NavMoveArgs& args,
						  const char* sourceFile,
						  U32F sourceLine,
						  const char* sourceFunc)
{
	if (ShouldLog() && !ValidateMoveToLocation(dest))
	{
		const Point destPs = dest.GetPosPs();
		AiLogNav(this,
				 "NavChar::MoveTo with unreachable position at (%.3f, %.3f, %.3f)\n",
				 (float)destPs.X(),
				 (float)destPs.Y(),
				 (float)destPs.Z());
	}

	if (args.m_motionType < kMotionTypeMax)
	{
		m_requestedMotionType = args.m_motionType;
	}

	NAV_ASSERTF(IsReasonable(dest.GetPosPs()),
				("Npc '%s' told to move to invalid location from %s (%s : %d)",
				 GetName(),
				 sourceFunc,
				 sourceFile,
				 sourceLine));

	NavCommand cmd;
	cmd.AsMoveToLocation(dest, args, sourceFile, sourceLine, sourceFunc);
	IssueNavCommand(cmd);

	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_dropMoveToBreadCrumbs && DebugSelection::Get().IsProcessSelected(this)))
	{
		const Point destPs = dest.GetPosPs();
		const Point destWs = GetParentSpace().TransformPoint(destPs);
		Color clr = AI::IndexToColor(GetUserId().GetValue());
		g_prim.Draw(DebugCross(destWs, 0.2f, 3.0f, clr, PrimAttrib(0)), Seconds(4.0f));

		char indexStr[16];
		snprintf(indexStr, sizeof(indexStr), "%u", (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused);
		indexStr[sizeof(indexStr)-1] = '\0';
		g_prim.Draw(DebugString(destWs, indexStr, clr, 0.7f), Seconds(4.0f));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::MoveToActionPack(ActionPack* pAp,
									const NavMoveArgs& args,
									const char* sourceFile,
									U32F sourceLine,
									const char* sourceFunc)
{
	if (args.m_motionType < kMotionTypeMax)
	{
		m_requestedMotionType = args.m_motionType;
	}

	NavCommand cmd;
	cmd.AsMoveToActionPack(pAp, args, sourceFile, sourceLine, sourceFunc);
	IssueNavCommand(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SwitchActionPack(ActionPack* pAp)
{
	if (pAp)
	{
		m_pNavStateMachine->SwitchActionPack(pAp);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::TeleportIntoActionPack(ActionPack* pAp,
										  bool playEntireEnterAnim,
										  bool force,
										  float fadeTime,
										  uintptr_t apUserData /* = 0 */)
{
	if (!pAp)
	{
		return;
	}

	AiLogNav(this,
			 "TeleportIntoActionPack: (dest: %s %s)\n",
			 pAp->GetName(),
			 PrettyPrint(pAp->GetDefaultEntryBoundFrame(m_pNavControl->GetActionPackEntryDistance())));

	if (ActionPack* pCurrentAp = m_pNavStateMachine->GetActionPack())
	{
		if (pCurrentAp == pAp && !force)
			return;

		const ActionPack::Type curApType = pCurrentAp->GetType();

		if (ActionPackController* pController = m_pAnimationControllers->GetControllerForActionPackType(curApType))
		{
			pController->Exit(nullptr);
		}
	}

	if (ActionPackController* pController = m_pAnimationControllers->GetControllerForActionPackType(pAp->GetType()))
	{
		BoundFrame newFrame = GetBoundFrame();

		m_pAnimationControllers->Reset();

		if (pController->TeleportInto(pAp, playEntireEnterAnim, fadeTime, &newFrame, apUserData))
		{
			const NavLocation navLoc = pAp->GetRegisteredNavLocation();

			if (fadeTime < NDI_FLT_EPSILON)
			{
				TeleportToBoundFrame(newFrame, navLoc.GetType(), false);
			}

			SwitchActionPack(pAp);

			if (NavControl* pNavControl = GetNavControl())
			{
				pNavControl->SetNavLocation(this, navLoc);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::MoveAlongSpline(const CatmullRom* pSpline,
								   float arcStart,
								   float arcGoal,
								   float arcStep,
								   float advanceStartTolerance,
								   const NavMoveArgs& args,
								   const char* sourceFile,
								   U32F sourceLine,
								   const char* sourceFunc)
{
	AI_ASSERT(pSpline);

	if (args.m_motionType < kMotionTypeMax)
	{
		m_requestedMotionType = args.m_motionType;
	}

	NavCommand cmd;
	cmd.AsMoveAlongSpline(pSpline, arcStart, arcGoal, arcStep, advanceStartTolerance, args, sourceFile, sourceLine, sourceFunc);

	IssueNavCommand(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::MoveInDirectionPs(Vector_arg dirPs,
									 const NavMoveArgs& args,
									 const char* sourceFile,
									 U32F sourceLine,
									 const char* sourceFunc)
{
	if (args.m_motionType < kMotionTypeMax)
	{
		m_requestedMotionType = args.m_motionType;
	}

	NavCommand cmd;
	cmd.AsMoveInDirectionPs(dirPs, args, sourceFile, sourceLine, sourceFunc);

	IssueNavCommand(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::MoveInDirectionWs(Vector_arg dirWs,
									 const NavMoveArgs& args,
									 const char* sourceFile,
									 U32F sourceLine,
									 const char* sourceFunc)
{
	const Vector dirPs = GetParentSpace().UntransformVector(dirWs);
	MoveInDirectionPs(dirPs, args, sourceFile, sourceLine, sourceFunc);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SteerTo(NavLocation dest,
						   float steerRateDps,
						   const NavMoveArgs& args,
						   const char* sourceFile,
						   U32F sourceLine,
						   const char* sourceFunc)
{
	NavCommand cmd;
	cmd.AsSteerToLocation(dest, steerRateDps, args, sourceFile, sourceLine, sourceFunc);

	IssueNavCommand(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::GetMoveAlongSplineCurrentArcLen(const StringId64 splineNameId, F32& outArcLen) const
{
	return GetNavStateMachine()->GetMoveAlongSplineCurrentArcLen(splineNameId, outArcLen);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::StopAndStand(float goalRadius, const char* sourceFile, U32F sourceLine, const char* sourceFunc)
{
	NAV_ASSERT(IsReasonable(goalRadius));

	NavCommand cmd;
	const float actualGoalRadius = goalRadius >= 0.0f ? goalRadius : GetMotionConfig().m_minimumGoalRadius;
	cmd.AsStopAndStand(actualGoalRadius, sourceFile, sourceLine, sourceFunc);

	IssueNavCommand(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::StopAndFace(Vector_arg faceDirPs,
							   float goalRadius,
							   const char* sourceFile,
							   U32F sourceLine,
							   const char* sourceFunc)
{
	NAV_ASSERT(IsReasonable(faceDirPs));
	NAV_ASSERT(IsReasonable(goalRadius));

	NavCommand cmd;
	const float actualGoalRadius = goalRadius >= 0.0f ? goalRadius : GetMotionConfig().m_minimumGoalRadius;
	cmd.AsStopAndFace(faceDirPs, actualGoalRadius, sourceFile, sourceLine, sourceFunc);

	IssueNavCommand(cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ActionPackEntryDef* NavCharacter::GetResolvedActionPackEntryDef() const
{
	const ActionPackEntryDef* pDef = nullptr;

	// We only want the resolved entry if we are going to the goal action pack (traversal action packs can be taken along the way)
	if (m_pNavStateMachine->GetActionPack() != nullptr &&
		 m_pNavStateMachine->GetActionPack() == m_pNavStateMachine->GetGoalActionPack())
	{
		pDef = m_pNavStateMachine->GetActionPackEntryDef();
	}
	return pDef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::HasResolvedEntryForGoalCoverAp() const
{
	const ActionPack* const pAp = m_pNavStateMachine->GetActionPack();
	const ActionPack* const pGoalAp = m_pNavStateMachine->GetGoalActionPack();
	return pAp && pAp == pGoalAp && pAp->GetType() == ActionPack::kCoverActionPack && m_pNavStateMachine->IsNextWaypointResolvedEntry();
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Selects the animation set to use for locomotion
bool NavCharacter::RequestDemeanor(Demeanor demeanor, AI_LOG_PARAM)
{
	if (demeanor.ToI32() >= m_numDemeanors || demeanor.ToI32() < 0 || !m_demeanorDefinitions[demeanor.ToI32()])
		return false;

	INdAiLocomotionController* pLocomotionController = GetNdAnimationControllers()->GetLocomotionController();

	Demeanor requestedDem = pLocomotionController->GetRequestedDemeanor();
	if (requestedDem != demeanor)
	{
		RequestCustomIdle(INVALID_STRING_ID_64, logNugget);

		pLocomotionController->RequestDemeanor(demeanor, logNugget);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NavCharacter::GetCustomIdle() const
{
	AnimControl* pAnimControl = GetAnimControl();
	DC::AnimNavCharInfo* pNavCharInfo = pAnimControl ? pAnimControl->Info<DC::AnimNavCharInfo>() : nullptr;

	return pNavCharInfo ? pNavCharInfo->m_customIdleAnim : INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::RequestCustomIdle(StringId64 idleAnimId, AI_LOG_PARAM)
{
	AnimControl* pAnimControl = GetAnimControl();
	DC::AnimNavCharInfo* pNavCharInfo = pAnimControl ? pAnimControl->Info<DC::AnimNavCharInfo>() : nullptr;

	if (pNavCharInfo && (pNavCharInfo->m_customIdleAnim != idleAnimId))
	{
		AiLogAnim(this, "Requesting new custom idle '%s' (was '%s')\n",
				  DevKitOnly_StringIdToString(idleAnimId),
				  DevKitOnly_StringIdToString(pNavCharInfo->m_customIdleAnim));

		pNavCharInfo->m_customIdleAnim = idleAnimId;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::CheckInjuredByAttack()
{
	const float healthPcnt = GetHealthSystem()->GetHealthPercentage();
	if (healthPcnt <= g_navCharOptions.m_injuredHealthPercentage)
	{
		m_curInjuryLevel = DC::kAiInjuryLevelMild;
	}
	else
	{
		m_curInjuryLevel = DC::kAiInjuryLevelNone;
	}
}

bool NavCharacter::ShouldResetInjured() const
{
	// Should use heal skill for now.
	//// Make sure we've finished our entry
	//if (IsInCover() && m_pNavStateMachine->GetStateId() == SID("UsingActionPack"))
	//{
	//	return true;
	//}

	if (GetHealthSystem()->GetTimeSinceLastAttacked() > Seconds(20.0f))
		return true;

	return !IsInjured();
}

DC::AiInjuryLevel NavCharacter::GetInjuryLevel() const
{
	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_forceInjured))
	{
		// TODO (eomernick): Need more extensive debug for this.
		return DC::kAiInjuryLevelMild;
	}

	AI_ASSERT(m_curInjuryLevel < DC::kAiInjuryLevelCount);

	return m_curInjuryLevel;
}

bool NavCharacter::IsInjured(DC::AiInjuryLevel level) const
{
	AI_ASSERT(level < DC::kAiInjuryLevelCount);

	return GetInjuryLevel() >= level;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ForceDemeanor(Demeanor demeanor, AI_LOG_PARAM)
{
	if (demeanor.ToI32() >= m_numDemeanors || demeanor.ToI32() < 0 || !m_demeanorDefinitions[demeanor.ToI32()])
		return;

	if (demeanor == m_currentDemeanor)
		return;

	RequestDemeanor(demeanor, logNugget);
	m_previousDemeanor = m_currentDemeanor;
	m_currentDemeanor = demeanor;
	ConfigureCharacter();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Demeanor NavCharacter::GetRequestedDemeanor() const
{
	if (const NdAnimationControllers* const pAnimCons = GetNdAnimationControllers())
	{
		if (const INdAiLocomotionController* const pLocoCon = pAnimCons->GetLocomotionController())
		{
			return pLocoCon->GetRequestedDemeanor();
		}
	}

	return m_currentDemeanor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavGoalReachedType NavCharacter::GetGoalReachedType() const
{
	return m_pNavStateMachine->GetGoalReachedType();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector NavCharacter::GetPathDirPs() const
{
	return m_pNavStateMachine->GetPathDirPs();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsWaitingForPath() const
{
	return m_pNavStateMachine->IsWaitingForPath();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::RequestGunState(GunState gunState,
								   bool slow /* = false */,
								   const DC::BlendParams* pBlend /* = nullptr */)
{
	if (!IsNetOwnedByMe())
		return;

	INdAiWeaponController* pWeaponController = GetNdAnimationControllers()->GetWeaponController();
	pWeaponController->RequestGunState(gunState, slow, pBlend);
}

/// --------------------------------------------------------------------------------------------------------------- ///
GunState NavCharacter::GetRequestedGunState() const
{
	const INdAiWeaponController* pWeaponController = GetNdAnimationControllers()->GetWeaponController();
	return pWeaponController->GetRequestedGunState();
}

/// --------------------------------------------------------------------------------------------------------------- ///
GunState NavCharacter::GetCurrentGunState() const
{
	const INdAiWeaponController* pWeaponController = GetNdAnimationControllers()->GetWeaponController();
	return pWeaponController ? pWeaponController->GetCurrentGunState() : kGunStateHolstered;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetMotionConfig(const MotionConfig& motionConfig)
{
	m_motionConfig = motionConfig;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const MotionConfig& NavCharacter::GetMotionConfig() const
{
	return m_motionConfig;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionConfig& NavCharacter::GetMotionConfig()
{
	return m_motionConfig;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionType NavCharacter::GetCurrentMotionType() const
{
	const INdAiLocomotionController* pLocomotionController = GetNdAnimationControllers()->GetLocomotionController();
	AI_ASSERT(pLocomotionController);
	return pLocomotionController->GetCurrentMotionType();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NavCharacter::GetCurrentMtSubcategory() const
{
	if (IsNavigationInterrupted())
	{
		return GetRequestedMtSubcategory();
	}
	else
	{
		const INdAiLocomotionController* pLocomotionController = GetNdAnimationControllers()->GetLocomotionController();
		AI_ASSERT(pLocomotionController);
		return pLocomotionController->GetCurrentMtSubcategory();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NavCharacter::GetRequestedMtSubcategory() const
{
	const NavStateMachine* pNavStateMachine = GetNavStateMachine();
	AI_ASSERT(pNavStateMachine);
	return pNavStateMachine->GetMtSubcategory();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NavCharacter::GetPendingMtSubcategory() const
{
	const NavStateMachine* pNavStateMachine = GetNavStateMachine();
	AI_ASSERT(pNavStateMachine);
	return pNavStateMachine->GetPendingMtSubcategory();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NavCharacter::GetDefaultMotionTypeSubcategory() const
{
	StringId64 mtSubCat = SID("normal");

	if (IsCheap())
	{
		mtSubCat = SID("horde");
	}
	else if (IsWading())
	{
		mtSubCat = SID("wade");
	}
	else if (IsInjured(DC::kAiInjuryLevelCritical))
	{
		mtSubCat = SID("injured");
	}

	return mtSubCat;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionType NavCharacter::GetRequestedMotionType() const
{
	return m_requestedMotionType;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetRequestedMotionType(MotionType type)
{
	if (type < kMotionTypeMax)
	{
		m_requestedMotionType = type;

		if (INavAnimController* pNavAnimController = GetActiveNavAnimController())
		{
			pNavAnimController->RequestMotionType(type);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::HasMoveSetFor(MotionType mt) const
{
	const INavAnimController* pNavAnimController = GetActiveNavAnimController();
	return pNavAnimController && pNavAnimController->HasMoveSetFor(mt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::CanStrafe(MotionType mt) const
{
	const INavAnimController* pNavAnimController = GetActiveNavAnimController();
	return pNavAnimController && pNavAnimController->CanStrafe(mt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsMoving() const
{
	const INavAnimController* pNavAnimController = GetActiveNavAnimController();
	return pNavAnimController && pNavAnimController->IsMoving();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsStrafing() const
{
	const INavAnimController* pNavAnimController = GetActiveNavAnimController();
	return pNavAnimController && pNavAnimController->IsStrafing();
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavCharacter::WaterDepth() const
{
	const SplasherSkeletonInfo* pSplasherSkel = GetSplasherController() ? GetSplasherController()->GetSkel() : nullptr;
	if (pSplasherSkel)
	{
		float waterDepth[2] = {0.0f, 0.0f};

		Point chrPos = GetTranslation();

		for (int iLeg=0; iLeg<2; iLeg++)
		{
			int footJoint = pSplasherSkel->GetFootInWaterJoint(iLeg);
			if (footJoint >= 0)
			{
				Point waterSurfacePos;
				bool bIsUnderwater = pSplasherSkel->GetWaterSurfacePos(footJoint, &waterSurfacePos, nullptr);
				if (bIsUnderwater)
					waterDepth[iLeg] = waterSurfacePos.Y() - chrPos.Y();
			}
		}

		return Max(0.0f, Lerp(waterDepth[0], waterDepth[1], 0.5f));
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavCharacter::WaterSurfaceY() const
{
	return GetTranslation().Y() + WaterDepth();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsSwimming() const
{
	bool isSwimming = false;

	if (m_pNavControl->GetActiveNavType() == NavLocation::Type::kNavPoly)
	{
		isSwimming = m_pNavControl->NavMeshForcesSwim() || m_pNavControl->NavMeshForcesDive();
	}

	return isSwimming;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetFacePositionPs(Point_arg facePosPs)
{
	const Point myPosPs = GetTranslationPs();

	NAVCHAR_ASSERTF(IsReasonable(facePosPs),
					("Npc '%s' given unreasonable face position '%s'", GetName(), PrettyPrint(facePosPs)));
	NAVCHAR_ASSERT(Dist(myPosPs, facePosPs) < 100000.0f);

	// project facePosPs onto the character's walking plane
	Point facePosOnWalkPlanePs(Simd::Select(facePosPs.QuadwordValue(), myPosPs.QuadwordValue(), Simd::GetMaskY()));

	m_useNaturalFacePosition = false;
	m_desiredFacePositionPs = facePosOnWalkPlanePs;

	NAVCHAR_ASSERT(IsReasonable(m_desiredFacePositionPs));
	NAVCHAR_ASSERT(Dist(myPosPs, m_desiredFacePositionPs) < 100000.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetFacePositionWs(Point_arg facingPositionWs)
{
	SetFacePositionPs(GetParentSpace().UntransformPoint(facingPositionWs));
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point NavCharacter::GetFacePositionPs() const
{
	if (m_useNaturalFacePosition)
		return GetLocatorPs().GetPosition() + GetLocalZ(GetRotationPs()) * 100.0f;

	return m_desiredFacePositionPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point NavCharacter::GetFacePositionWs() const
{
	return GetParentSpace().TransformPoint(GetFacePositionPs());
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavCommand::Status NavCharacter::GetNavStatus() const
{
	return m_pNavStateMachine->GetStatus();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsNavigationInProgress() const
{
	return m_pNavStateMachine->GetStatus() == NavCommand::kStatusCommandPending ||
		   m_pNavStateMachine->GetStatus() == NavCommand::kStatusCommandInProgress;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsCommandStopAndStand() const
{
	return m_pNavStateMachine->IsCommandStopAndStand();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsStopped() const
{
	return m_pNavStateMachine->IsStopped();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point NavCharacter::GetDestinationPs() const
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	return m_pNavStateMachine->GetFinalDestPointPs();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point NavCharacter::GetDestinationWs() const
{
	const Point posPs = m_pNavStateMachine->GetFinalDestPointPs();
	return GetParentSpace().TransformPoint(posPs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavLocation NavCharacter::GetFinalNavGoal() const
{
	return m_pNavStateMachine->GetFinalNavGoal();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsPathValid() const
{
	return m_pNavStateMachine->IsPathValid();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const PathWaypointsEx* NavCharacter::GetPathPs() const
{
	return m_pNavStateMachine->GetCurrentPathPs();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const PathWaypointsEx* NavCharacter::GetPostTapPathPs() const
{
	return m_pNavStateMachine->GetPostTapPathPs();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const PathWaypointsEx* NavCharacter::GetLastFoundPathPs() const
{
	return m_pNavStateMachine->GetLastFoundPathPs();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator NavCharacter::GetPathTapAnimAdjustLs() const
{
	return m_pNavStateMachine->GetPathTapAnimAdjustLs();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const TraversalActionPack* NavCharacter::GetPathActionPack() const
{
	return (const TraversalActionPack*)m_pNavStateMachine->GetPathActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavCharacter::GetPathLength() const
{
	return m_pNavStateMachine->GetCurrentPathLength();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsWaitingForActionPack() const
{
	return m_pNavStateMachine->IsWaitingForActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsMovingStalled() const
{
	return m_pNavStateMachine->IsMovementStalled();
}

/// --------------------------------------------------------------------------------------------------------------- ///
TimeFrame NavCharacter::GetMoveStallTimeElapsed() const
{
	return m_pNavStateMachine->GetStallTimeElapsed();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vector ClampVectorToXzAngle(const Vector& baseDir, const Vector& dir, float maxAngle)
{
	const Scalar cosClampMaxAngle = Cos(maxAngle);

	Vector unitFlatBaseDir = baseDir;
	unitFlatBaseDir.SetY(0.0f);
	unitFlatBaseDir = SafeNormalize(unitFlatBaseDir, kUnitYAxis);

	Vector unitFlatDir = dir;
	unitFlatDir.SetY(0.0f);
	unitFlatDir = SafeNormalize(unitFlatDir, kUnitYAxis);

	// Dir is within maxAngle of baseDir
	Scalar dot = Dot(unitFlatBaseDir, unitFlatDir);
	if (dot >= cosClampMaxAngle)
	{
		return dir;
	}

	const float dirAngle = Acos(dot);

	// Find rotation direction
	const Vector leftDir = Cross(kUnitYAxis, unitFlatBaseDir);
	dot = Dot(leftDir, dir);
	const float clampAngle = dot > 0.0f ? maxAngle - dirAngle : dirAngle - maxAngle;

	const Vector clampedDir = RotateVectorAbout(dir, kUnitYAxis, clampAngle);

	return clampedDir;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator NavCharacter::GetEyeWs() const
{
	if (!GetAttachSystem())
		return GetLocator();

	const Locator eyeRawLocWs = GetEyeRawWs();

	if (!AdjustEyeToLookDirection())
		return eyeRawLocWs;

	Locator eyeLocWs = eyeRawLocWs;

	// Respect the look at position
	const Point lookAtPosWs = GetParentSpace().TransformPoint(GetLookAtPositionPs());
	const Vector unitLookAtVectorWs = SafeNormalize(lookAtPosWs - eyeRawLocWs.Pos(), GetLocalZ(eyeRawLocWs.Rot()));
	eyeLocWs.SetRot(QuatFromLookAt(unitLookAtVectorWs, kUnitYAxis));


	Vector rawDir = GetLocalZ(eyeRawLocWs.Rot());

	if (const ActionPack* pAp = GetEnteredActionPack())
	{
		if (pAp->GetType() == ActionPack::kCoverActionPack)
		{
			Vector coverDir = GetLocalZ(pAp->GetBoundFrame().GetRotation());
			rawDir = coverDir;
		}
	}

	// Constrain the eye direction to an angle around the raw eye direction
	const Vector clampedDirWs = ClampVectorToXzAngle(rawDir,
													 GetLocalZ(eyeLocWs.Rot()),
													 DegreesToRadians(45.0f));
	eyeLocWs.SetRotation(QuatFromLookAt(clampedDirWs, kUnitYAxis));

	return eyeLocWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator NavCharacter::GetEyeRawWs() const
{
	return GetLocator().TransformLocator(m_lookOriginOs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::ShouldAllowTeleport() const
{
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::HandleTriggeredEffect(const EffectAnimInfo* pEffectAnimInfo)
{
	EffectControlSpawnInfo effInfo;
	PopulateEffectControlSpawnInfo(pEffectAnimInfo, effInfo);

	if (m_pEffectControl->IsEffectFilteredByCriterion(&effInfo))
	{
		return;
	}

	NdAttachableObject* pWeapon = PunPtr<NdAttachableObject*>(GetWeapon());

	switch (pEffectAnimInfo->m_pEffect->GetNameId().GetValue())
	{
	case SID_VAL("voice-effect"):
		if (!EngineComponents::GetNdGameInfo()->m_disableVoiceEffects)
		{
			if (const EffectAnimEntryTag* pNameTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("name")))
			{
				RequestVoxArgs args;
				args.m_voxId = pNameTag->GetValueAsStringId();
				args.m_eBroadcastMode = DC::kVoxBroadcastModeLocal;

// (jdl) Adjusting volume and pitch for voice-effect is bad for mixing!  This feature is now deprecated!
//				const EffectAnimEntryTag* pVolumeTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("vol"));
//				args.m_fGain = pVolumeTag ? DbToGain(pVolumeTag->GetValueAsF32()) : NDI_FLT_MAX;

//				const EffectAnimEntryTag* pPitchTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("pitch"));
//				args.m_fPitchMod = pPitchTag ? pPitchTag->GetValueAsF32() : NDI_FLT_MAX;

				GetVoxController().RequestVox(args);
			}
		}
		break;

	case SID_VAL("abort-voice-effect"):
		GetVoxController().StopVox();
		break;

	case SID_VAL("holster-weapon-quick"):
		RequestGunState(kGunStateHolstered);
		break;

	case SID_VAL("unholster-weapon-quick"):
		RequestGunState(kGunStateOut);
		break;

	case SID_VAL("hide-weapon-mesh"):
		if (pWeapon)
		{
			if (IDrawControl* pDrawControl = pWeapon->GetDrawControl())
			{
				pDrawControl->HideAllMesh();
			}
		}
		break;

	case SID_VAL("show-weapon-mesh"):
		if (pWeapon)
		{
			if (IDrawControl* pDrawControl = pWeapon->GetDrawControl())
			{
				pDrawControl->ShowAllMesh();
			}
		}
		break;

	case SID_VAL("eject-magazine"):
	case SID_VAL("show-magazine"):
	case SID_VAL("hide-magazine"):
		if (pWeapon)
		{
			SendEvent(pEffectAnimInfo->m_pEffect->GetNameId(), MutableProcessHandle(pWeapon));
		}
		break;

	case SID_VAL("start-ragdoll-stumble"):
		if (!g_ndConfig.m_pNetInfo->IsNetActive() || g_ndConfig.m_pNetInfo->IsTo1())
			m_ragdollStumble = true;
		break;

	case SID_VAL("stop-ragdoll-stumble"):
		m_ragdollStumble = false;
		break;

	case SID_VAL("head-effect"):
		{
			// Need to do a probe down from the character's head to see what kind of pat we hit
			// Fuck it, let's just make it synchronous. It's only used during melee anyway.
			RayCastJob rayJob;

			Locator eyeLoc = GetAttachSystem()->GetLocator(FindAttachPoint(SID("targEye")));
			Point startPos = eyeLoc.GetTranslation();
			Vector forward = GetLocalZ(eyeLoc.GetRotation());
			CollideFilter filter(Collide::kLayerMaskBackground | Collide::kLayerMaskForeground, Pat(0), this);

			//g_prim.Draw(DebugLine(startPos, forward, kColorRed), Seconds(1.0f));

			SimpleCastKick(rayJob, startPos, forward, filter, ICollCastJob::kCollCastSynchronous, ICollCastJob::kClientNpc);

			SimpleCollision cc;
			float t = SimpleCastGather(rayJob, &cc);

			if (t >= 0.0f)
			{
				//effInfo.m_pPat = &cc.m_pat;
				effInfo.m_handSurfaceType[kLeftArm].Clear();
				effInfo.m_handSurfaceType[kRightArm].Clear();

				// We abuse the bound frame to be the point at which to play the particle effect.
				Vector up = Dot(cc.m_normal, kUnitYAxis) >= 0.9f ? Vector(kUnitZAxis) : Vector(kUnitYAxis);
				Locator particleLoc(cc.m_contact, QuatFromLookAt(cc.m_normal, up));
				effInfo.m_boundFrame = BoundFrame(particleLoc, GetBoundFrame().GetBinding());

				GetEffectControl()->FireEffect(&effInfo);
			}

			break;
		}
	case SID_VAL("vehicle-death-go-ragdoll"):
		TriggerVehicleDeathRagdollEffect();
		break;

	default:
		ParentClass::HandleTriggeredEffect(pEffectAnimInfo);
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::PopulateEffectControlSpawnInfo(const EffectAnimInfo* pEffectAnimInfo, EffectControlSpawnInfo& effInfo)
{
	MeshProbe::SimpleSurfaceTypeLayers groundSurfaceId = MeshProbe::SimpleSurfaceTypeLayers::From(m_groundMeshRaycastResult[kMeshRaycastGround].m_surfaceTypeInfo);

	MeshProbe::SimpleSurfaceTypeLayers leftHandSurfaceType = groundSurfaceId;
	MeshProbe::SimpleSurfaceTypeLayers rightHandSurfaceType = groundSurfaceId;
	if (IsBuddyNpc())
	{
		leftHandSurfaceType = MeshProbe::SimpleSurfaceTypeLayers::From(m_handRaycastResult[kLeftArm].m_surfaceTypeInfo);
		rightHandSurfaceType = MeshProbe::SimpleSurfaceTypeLayers::From(m_handRaycastResult[kRightArm].m_surfaceTypeInfo);
	}

	if (m_overrideSurfaceType.m_hand.Valid())
	{
		leftHandSurfaceType = m_overrideSurfaceType.m_hand;
		rightHandSurfaceType = m_overrideSurfaceType.m_hand;
	}

	bool inVehicle = false;
	{
		RigidBody* pBindBody = GetBinding().GetRigidBody();
		if (pBindBody)
		{
			NdGameObject* pBindOwner = pBindBody->GetOwner();
			if (pBindOwner && (pBindOwner->IsKindOf(SID("DriveableVehicle")) || pBindOwner->IsKindOf(SID("DriveableBoat"))))
				inVehicle = true;
		}
	}
	const bool feetInWater = FeetSplashersInWater() && !inVehicle;

	GetEffectControl()->PopulateSpawnInfo(effInfo,
										  pEffectAnimInfo,
										  this,
										  this,
										  feetInWater,
										  IsInWaistDeepWater());

	effInfo.m_isMeleeHit = IsMeleeActionSuccessful();

	effInfo.m_handSurfaceType[kLeftArm] = leftHandSurfaceType;
	effInfo.m_handSurfaceType[kRightArm] = rightHandSurfaceType;

	effInfo.m_hOtherGameObject = GetOtherMeleeCharacter();
	effInfo.m_footSurfaceType[kLeftLeg] = groundSurfaceId;
	effInfo.m_footSurfaceType[kRightLeg] = groundSurfaceId;

	const StringId64 backupBaseSt = m_groundMeshRaycastResult[kMeshRaycastGround].m_backupBaseLayerSt;
	effInfo.m_backupFootSurfaceType[kLeftLeg] = backupBaseSt;
	effInfo.m_backupFootSurfaceType[kRightLeg] = backupBaseSt;

	MeshProbe::SurfaceType surfaceType = m_bodyMeshRaycastResult[kBodyImpactChestSound].m_surfaceTypeInfo.m_layers[0].m_surfaceType;
	effInfo.m_bodyImpactChestSurfaceType = surfaceType;

	effInfo.m_bodyImpactBackSurfaceType = GetBodyImpactSurfaceType();

	if (m_overrideSurfaceType.m_body.Valid())
	{
		effInfo.m_bodyImpactChestSurfaceType = m_overrideSurfaceType.m_body;
		effInfo.m_bodyImpactBackSurfaceType = m_overrideSurfaceType.m_body;
	}

	effInfo.m_leftShoulderImpactSurfaceType = m_shoulderRaycastResult[kLeftShoulderImpactSound].m_surfaceTypeInfo.m_layers[0].m_surfaceType;
	effInfo.m_rightShoulderImpactSurfaceType = m_shoulderRaycastResult[kRightShoulderImpactSound].m_surfaceTypeInfo.m_layers[0].m_surfaceType;

	if (feetInWater)
	{
		effInfo.m_footSurfaceType[kLeftLeg] = MeshProbe::SurfaceType(SID("water"));
		effInfo.m_footSurfaceType[kRightLeg] = MeshProbe::SurfaceType(SID("water"));
	}

	if (m_overrideSurfaceType.m_feet.Valid())
	{
		effInfo.m_footSurfaceType[kLeftLeg] = m_overrideSurfaceType.m_feet;
		effInfo.m_footSurfaceType[kRightLeg] = m_overrideSurfaceType.m_feet;
	}

	/////////////////////////////////////////////////
	////////////////// MP EFF GAIN HACK
	////////////////// MP EFF GAIN HACK
	////////////////// MP EFF GAIN HACK
	/////////////////////////////////////////////////
	if (g_ndConfig.m_pNetInfo->IsNetActive()
		&& g_ndConfig.m_pNetInfo->IsTo1()
		&& (effInfo.m_type == SID("foot-effect")
			|| effInfo.m_type == SID("gear-effect")
			|| effInfo.m_type == SID("hand-effect-left")
			|| effInfo.m_type == SID("hand-effect-right")
			|| effInfo.m_type == SID("bp-effect")))
	{
		static ScriptPointer<F32> s_pGainMulti = ScriptPointer<F32>(SID("*npc-eff-audio-gain-multiplier*"));
		if (s_pGainMulti)
		{
			effInfo.m_fGain *= *s_pGainMulti;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Update the potential move and update the requested movement deltas in the movement root.
void NavCharacter::Update()
{
	PROFILE(AI, NavChar_Update);

	NavControl* pNavControl = GetNavControl();
	AnimControl* pAnimControl = GetAnimControl();

	// if player friend AND not dead AND allowed to teleport
	if (g_factionMgr.IsFriend(GetFactionId(), EngineComponents::GetNdGameInfo()->GetPlayerFaction()) && !IsDead() && ShouldAllowTeleport() && pNavControl && !IsInScriptedAnimationState())
	{
		// handled in EmergencyTeleportFallRecovery

		//const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
		//NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		//// if nav mesh unloaded out from under us,
		//if (pNavControl && pNavControl->HasNavLocationTurnedInvalid())
		//{
		//	AiError("An npc's (%s) Nav location got unloaded out from under them; teleporting.\n", GetName());
		//	if (!TeleportToPlayersNavMesh())
		//	{
		//		AiError("An npc's (%s) player-teleportation failed. Killing.\n", GetName());
		//		GetHealthSystem()->Kill();
		//	}
		//}
	}

	UpdateDepenSegmentScale();
	UpdateRagdoll();

	NdVoxControllerBase* pVoxControllerBase = &GetVoxController();
	//VoxController* pVoxController = static_cast<VoxController*>(pVoxControllerBase);
	pVoxControllerBase->ActiveUpdate();

	if (AnimOverlays* pOverlays = pAnimControl->GetAnimOverlays())
	{
		const bool wantWade = pNavControl->NavMeshForcesWade();

		if (m_isWading != wantWade)
		{
			AiLogAnim(this,
					  "Wading changing from %s to %s\n",
					  m_isWading ? "ENABLED" : "DISABLED",
					  wantWade ? "ENABLED" : "DISABLED");

			m_isWading = wantWade;

			StringBuilder<128> wadeOverlay;
			wadeOverlay.format("anim-overlay-%s-wading-%s", GetOverlayBaseName(), wantWade ? "enabled" : "disabled");

			const StringId64 wadeOverlayId = StringToStringId64(wadeOverlay.c_str());
			pOverlays->SetOverlaySet(wadeOverlayId, true);

			ConfigureCharacter();
		}
	}

	if (ConfiguredForExtraAnimLayers())
	{
		const F32 voiceEnergyLevel = 100.0f * pVoxControllerBase->GetRmsLevel();
		m_lipSync.Update(pAnimControl, voiceEnergyLevel);
	}

	m_pScriptedAnimation->Update(this);

	DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>();
	pInfo->m_healthPercentage = Limit01(GetHealthSystem()->GetHealthPercentage());

	const bool isAnimationControlled = IsAnimationControlled() || IsSpawnAnimPlaying() || m_disableNavigationRefCount != 0;

#if !FINAL_BUILD
	if (IsInScriptedAnimationState() && g_navCharOptions.m_showScriptControlIndicator)
	{
		// Show when an Npc is being script controlled.
		g_prim.Draw(DebugCircle(GetTranslation() + Vector(0.0f, 0.05f, 0.0f), Vector(SMath::kUnitYAxis), 0.5f, kColorRed));
	}
#endif

	if (isAnimationControlled)
	{
		if (!IsNavigationInterrupted())
		{
			Interrupt(FILE_LINE_FUNC);
		}
	}
	else if (!IsDead())
	{
		if (IsNavigationInterrupted())
		{
			Log(this, kNpcLogChannelGeneral, "Resuming Navigation\n");
			m_pNavStateMachine->Resume();
		}
	}
	else if (CompositeRagdollController* pRagdollController = GetRagdollController())
	{
		if (!IsNavigationInterrupted())
		{
			Interrupt(FILE_LINE_FUNC);
		}
	}

	// Update the look, face and movement values for the animation controllers.
	// process queued navigation commands
	if (!IsDead())
	{
		if (GetNavControl())
		{
			// update nav control stuff if any, prior to nav state machine update
			UpdateNavControlLogic();

			m_pNavStateMachine->Update();
		}

		// Allow animation controllers to request further transitions to be taken this frame.
		m_pAnimationControllers->RequestAnimations();

		// update vulnerability
		UpdateVulnerability(isAnimationControlled);
	}
	else if (INdAiHitController* pHitController = m_pAnimationControllers->GetHitController())
	{
		// always update the hit controller because it is responsible for death animations
		pHitController->RequestAnimations();
	}

	if (!IsNavigationInterrupted())
	{
		const bool isAnimationControlled2 = IsAnimationControlled() || m_disableNavigationRefCount != 0;

		if (isAnimationControlled2)
		{
			Interrupt(FILE_LINE_FUNC);
		}
	}

	if (!GetAttachSystem())
	{
		AI_ASSERTF(false, ("Hey, you! Better check out this invalid character here.\n"));
	}

	if (m_curInjuryLevel > DC::kAiInjuryLevelNone && ShouldResetInjured())
	{
		m_curInjuryLevel = DC::kAiInjuryLevelNone;
	}

	if (m_suppressGroundProbeFilter)
	{
		ResetGroundFilter();

		m_suppressGroundProbeFilter = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::PostRenderUpdateAfterHavokStep(bool allowPoiCollect)
{
	const bool performRootUpdate = GetProcessDeltaTime() > 0.0f;
	if (performRootUpdate)
	{
		NavControl* pNavControl = GetNavControl();
		if (!IsDead() && pNavControl)
		{
			m_pNavStateMachine->GatherPathFindResults();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::PostAnimUpdate_Async()
{
	PROFILE(AI, NavChar_PostAnimUpdateAsync);

	// do this here because for instances that
	// have 'saved align' we want them to get data
	// before we do UpdateRootLocator()
	ParentClass::PostAnimUpdate_Async();

	if (!IsNetOwnedByMe())
	{
		// apply the net layer?
		{
			PROFILE(Net, NavChar_NetAnimLayer);
			BoundFrame netLoc;
			AnimControl* pAnimControl = GetAnimControl();
			AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
			AnimCmdGeneratorLayer* pNetLayer = pAnimControl->GetCmdGeneratorLayerById(SID("net-anim-layer"));

			bool rootLocatorUpdated = false;

			F32 offset;

			if (pAnimControl && pBaseLayer && pNetLayer && pNetLayer->GetCurrentFade() > 0.0f && m_pNetAnimCmdGenerator->GetAlign(netLoc, false, offset) && !IsBusyInMelee())
			{
				Locator newAlign;
				if (pNetLayer->GetCurrentFade() < 1.0f)
				{
					const bool performRootUpdate = GetProcessDeltaTime() > 0.0f;
					if (performRootUpdate)
					{
						NavControl* pNavControl = GetNavControl();
						if (!IsRagdollPowerFadedOut() && pNavControl)
						{
							//MsgConPauseable("NavCharacter Setting Align From UpdateRootLocator To Blend With net layer %.2f\n", pNetLayer->GetCurrentFade());
							UpdateRootLocator();
							rootLocatorUpdated = true;
						}
					}
					Locator baseLoc = GetLocator();

					newAlign = Lerp(baseLoc, netLoc.GetLocator(), pNetLayer->GetCurrentFade());
					newAlign.SetRot(Normalize(newAlign.Rot()));
				}
				else
				{
					newAlign = netLoc.GetLocator();
					newAlign.SetRot(Normalize(newAlign.Rot()));
				}

				// 			DebugDrawSphere(baseLoc.GetTranslation(), 0.15f, kColorRed);
				// 			DebugDrawSphere(netLoc.GetTranslation(), 0.15f, kColorBlue);
				// 			DebugDrawSphere(newAlign.GetTranslation(), 0.1f, kColorYellow);

				//MsgConPauseable("NavCharacter Setting Align From Net Layer Blend %.2f\n", pNetLayer->GetCurrentFade());

				SetLocator(newAlign);
				BindToRigidBody(netLoc.GetBinding().GetRigidBody());
			}
			else if (pAnimControl && pBaseLayer)
			{
				const Locator baseLoc = NdAnimAlign::WalkComputeAlign(this,
																	  pBaseLayer,
																	  GetLocator(),
																	  kIdentity,
																	  GetUniformScale());
				Locator newAlign = baseLoc;
				newAlign.SetRot(Normalize(newAlign.Rot()));

				const bool performRootUpdate = GetProcessDeltaTime() > 0.0f;

				if (performRootUpdate)
				{
					NavControl* pNavControl = GetNavControl();
					if (!IsRagdollPowerFadedOut() && pNavControl)
					{
						UpdateRootLocator();
						rootLocatorUpdated = true;
					}
				}
			}

			if (m_pNetAnimCmdGenerator)
			{
				m_pNetAnimCmdGenerator->UpdateFlagsAndWeapons(this);
			}

			if (!rootLocatorUpdated)
			{
				m_groundPositionPs = GetLocatorPs().GetPosition();
				m_groundNormalPs   = kUnitYAxis;
				ResetGroundFilter();
			}
		}

		return;
	}

	// Animation transitions have been taken and we can now update all controllers
	// so that we know if they are 'busy' or not
	{
		PROFILE(AI, NavChar_AnimCtrlsUpdateStatus);
		m_pAnimationControllers->UpdateStatus();
	}

	// It need to do so using the correct world transform.
	const float dt = GetProcessDeltaTime();
	const bool performRootUpdate = dt > 0.0f;
	if (performRootUpdate)
	{
		NavControl* pNavControl = GetNavControl();
		if (!IsRagdollPowerFadedOut() && !m_pNavStateMachine->IsNormalMovementSuppressed() && pNavControl)
		{
			UpdateRootLocator();
		}
		else
		{
			m_prevLocatorPs = GetLocatorPs();
		}

		if (IsProcessDead())
		{
			return;
		}

		PostRootLocatorUpdate();

		// do this after nav controllers have potentially completed their commands
		m_pNavStateMachine->PostRootLocatorUpdate();
	}

	// Modify the scale here...
	if (g_navCharOptions.m_npcScale != 1.0f)
	{
		SetUniformScale(g_navCharOptions.m_npcScale);
		if (NdAttachableObject* pWeaponBase = PunPtr<NdAttachableObject*>(GetWeapon()))
		{
			pWeaponBase->SetUniformScale(g_navCharOptions.m_npcScale);
		}
	}

	// if for whatever reason we don't get data back from the plugin, default to the raw eye
	m_lastEyeAnimRotWs = GetEyeRawWs().Rot();

	if (JointModifierData* pJmData = GetJointModifiers()->GetJointModifierData())
	{
		if (JointModifierData::OutputData* pOutputData = pJmData->GetOutputData())
		{
			pOutputData->m_averageEyeRotPreIkWs = m_lastEyeAnimRotWs;
		}
	}

	// IK could potentially modify the root locator
	m_pJointModifiers->PreAnimBlending();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::PostAnimBlending_Async()
{
	PROFILE(AI, NavChar_PostAnimBlendingAsync);

	// pull the pre-ik eye average out from the plugin data, if it's there
	JointModifierData* pJmData = GetJointModifiers()->GetJointModifierData();
	if (pJmData && pJmData->IsEyeIkEnabled())
	{
		if (JointModifierData::OutputData* pOutputData = pJmData->GetOutputData())
		{
			m_lastEyeAnimRotWs = pOutputData->m_averageEyeRotPreIkWs;
		}
	}

	// work IK controllers now that animation has been processed
	m_pJointModifiers->PostAnimBlending();

	if (NdAnimationControllers* pAnimControllers = GetNdAnimationControllers())
	{
		pAnimControllers->UpdateProcedural();
	}

	ParentClass::PostAnimBlending_Async();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ProcessRegionControlUpdate()
{
	{
		// clear override surface type.
		m_overrideSurfaceType.Reset();
	}

	if (IsBuddyNpc())
	{
		// For buddy NPCs, we would like a more accurate collision detection model against the regions, for instance region planes or
		// thin region boxes. This is required for buddy NPCs as we usually trigger state-script changes through regions, and shapes
		// like region planes are very useful for design. The parent class (Character->NdGameObject) implements a "continuous model" that takes care of
		// previous and current character positions.
		// For non-buddy NPCs we are fine with trading accuracy for performance, at least for now.
		ParentClass::ProcessRegionControlUpdate();
	}
	else
	{
		GetRegionControl()->Update(GetLocator().Pos(), 0.0f, this); // we pass game object pointer here because RegionControl has no knowledge of it
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::PostJointUpdate_Async()
{
	PROFILE(AI, NavChar_PostJointUpdateAsync);

	ParentClass::PostJointUpdate_Async();

	if (true)
	{
		PROFILE(AI, NavChar_UpdatePushers);
		UpdatePushers();
	}

	m_pJointModifiers->PostJointUpdate();
	//	MsgOut("REPLAY: (NavChar) IsBusy?: %s\n", IsBusy() ? "TRUE" : "FALSE");

	if (AttachSystem* pAs = GetAttachSystem())
	{
		const Locator locWs = GetLocator();
		Point aimOriginWs = pAs->GetAttachPosition(m_aimOriginAttachIndex);
		m_aimOriginOs = locWs.UntransformPoint(aimOriginWs);

		Locator eyeRawWs = pAs->GetLocator(m_lookAttachIndex);
		m_lookOriginOs = locWs.UntransformLocator(eyeRawWs);
	}
	else
	{
		m_aimOriginOs = kOrigin;
		m_lookOriginOs = kIdentity;
	}

	// this region update needs to occur late in this callback, as it may kill us which will delete and nullptr several of our pointers
	if (!IsDead() && ShouldUpdateRegionControl())
	{
		PROFILE(AI, NavChar_UpdateRegionCtrl);
		SetRegionControlUpdateEnabled(true);

		if (FALSE_IN_FINAL_BUILD(g_gameObjectDrawFilter.m_drawRegions && DebugSelection::Get().IsProcessOrNoneSelected(this)))
		{
			GetRegionControl()->DebugDraw(g_ndConfig.m_pFnFilterDrawnRegionFunc);
			//g_prim.Draw(DebugCross(newRegionPos, outerRadius, g_colorRed));
			//g_prim.Draw(DebugSphere(Sphere(newRegionPos, outerRadius), g_colorBlue));
		}
	}
	else
	{
		SetRegionControlUpdateEnabled(false);
	}

	AnimControl* pAnimControl = GetAnimControl();
	if (DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>())
	{
		const Locator loc = GetLocator();
		const AttachSystem* pAs = GetAttachSystem();

		AttachIndex ind;
		pAs->FindPointIndexById(&ind, SID("targRAnkle"));
		Locator ankleR = pAs->GetLocator(ind);
		pAs->FindPointIndexById(&ind, SID("targLAnkle"));
		Locator ankleL = pAs->GetLocator(ind);

		const Point newLAnklePosLs = loc.UntransformPoint(ankleL.Pos());
		const Point newRAnklePosLs = loc.UntransformPoint(ankleR.Pos());
		const float deltaTime = GetProcessDeltaTime();

		const Vector groundNormalOs = GetLocatorPs().UntransformVector(GetGroundNormalPs());
		m_ankleInfoLs.Update(newLAnklePosLs, newRAnklePosLs, deltaTime, groundNormalOs, &pInfo->m_anklesLs);

		if (false)
		{
			PrimServerWrapper ps(loc);
			ps.EnableHiddenLineAlpha();
			ps.DrawCross(m_ankleInfoLs.m_anklePos[0], 0.1f, kColorCyan);
			ps.DrawArrow(m_ankleInfoLs.m_anklePos[0], m_ankleInfoLs.m_ankleVel[0], 0.2f, kColorCyan);
			ps.DrawCross(m_ankleInfoLs.m_anklePos[1], 0.1f, kColorMagenta);
			ps.DrawArrow(m_ankleInfoLs.m_anklePos[1], m_ankleInfoLs.m_ankleVel[1], 0.2f, kColorMagenta);
		}
	}

/*
	if (const PoseTracker* pPoseTracker = GetPoseTracker())
	{
		IMotionPose::BodyData ld = pPoseTracker->GetPose().GetJointDataByIdOs(SID("l_ankle"));
		IMotionPose::BodyData rd = pPoseTracker->GetPose().GetJointDataByIdOs(SID("r_ankle"));

		const Locator loc = GetLocator();
		PrimServerWrapper ps(loc);
		ps.EnableHiddenLineAlpha();
		ps.DrawCross(ld.m_pos, 0.1f, kColorCyan);
		ps.DrawArrow(ld.m_pos, ld.m_vel, 0.2f, kColorCyan);
		ps.DrawCross(rd.m_pos, 0.1f, kColorMagenta);
		ps.DrawArrow(rd.m_pos, rd.m_vel, 0.2f, kColorMagenta);
	}*/
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool WalkResetSavedAlign(AnimStateInstance* pInstance, AnimStateLayer* pStateLayer, uintptr_t userData)
{
	const BoundFrame& bf = *(const BoundFrame*)userData;

	if (pInstance->GetStateFlags() & DC::kAnimStateFlagSaveTopAlign)
	{
		// if we have an apRef don't trash it (set saved align overwrites the apRef)
		if (!pInstance->IsApLocatorActive())
		{
			pInstance->SetSavedAlign(bf);
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::TeleportToBoundFrame(const BoundFrame& bf,
										const NavLocation::Type navType /* = NavLocation::Type::kNavPoly */,
										bool resetNavigation /* = true */,
										bool allowIdleAnim /*= true*/)
{
	AiLogNav(this, "TeleportToBoundFrame: (dest: %s)\n", PrettyPrint(bf));

	BindToRigidBody(bf.GetBinding().GetRigidBody());
	SetLocatorPs(bf.GetLocatorPs());

	AnimControl* pAnimControl = GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	if (pBaseLayer)
	{
		pBaseLayer->WalkInstancesNewToOld(WalkResetSavedAlign, (uintptr_t)&bf);
	}

	NavControl* pNavControl = GetNavControl();

	pNavControl->SetActiveNavType(this, navType);
	NavLocation navLoc;
	navLoc.SetWs(bf.GetTranslationWs());
	pNavControl->SetNavLocation(this, navLoc);
	pNavControl->ResetEverHadAValidNavMesh();

	m_prevLocatorPs		 = GetLocatorPs();
	m_desiredLocPs		 = GetLocatorPs();
	m_animDesiredAlignPs = GetLocatorPs();

	m_groundPositionPs = GetTranslationPs();
	m_groundNormalPs   = kUnitYAxis;

	m_groundAdjustment = kZero;
	m_waterAdjustmentWs = kZero;

	m_fallSpeed = 0.0f;
	m_secondsFalling = 0.0f;
	m_secondsNoNavMesh = 0.0f;

	SendEvent(SID("stop-animating"), this, DC::kAnimateLayerAll, 0.0f, INVALID_STRING_ID_64);

	if (resetNavigation)
	{
		ResetNavigation(allowIdleAnim);
	}

	OnTeleport();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::OnTeleport()
{
	// a little messy but the idea is this function should be responsible for any side effects of teleportation,
	// things that don't directly represent the npcs state, so if we have some 0 second blend igc blends we can
	// cut to the character and have them looking visually correct

	if (FgAnimData* pAnimData = GetAnimData())
	{
		pAnimData->OnTeleport();
	}

	if (PoseTracker* pPoseTracker = GetPoseTracker())
	{
		pPoseTracker->OnTeleport();
	}

	SendEvent(SID("teleport-cloth"), this);

	ResetGroundFilter();

	ResetVelocity();

	ResetLegIkForTeleport();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::TeleportToPlayersNavMesh()
{
	Point playerPos(kZero);
	if (EngineComponents::GetNdGameInfo()->GetPlayerGameObject() == nullptr)
	{
		bool foundPlayer = false;
		if (g_ndConfig.m_pNetInfo->IsNetActive())
		{
			for (U32 i = 0; i < g_ndConfig.m_pNetInfo->m_pNetGameManager->GetNumNetPlayerTrackers(); ++i)
			{
				if (NdNetPlayerTracker *pTracker = g_ndConfig.m_pNetInfo->m_pNetGameManager->GetNdNetPlayerTrackerByIndex(i))
				{
					playerPos = pTracker->GetBoundFramePos().GetTranslation();
					foundPlayer = true;
					break;
				}
			}
		}

		if (!foundPlayer)
			return false;
	}

	NavMeshMgr& mgr = *EngineComponents::GetNavMeshMgr();
	FindBestNavMeshParams findNavMeshParams;
	findNavMeshParams.m_pointWs = playerPos;
	findNavMeshParams.m_yThreshold = 1e10f;
	findNavMeshParams.m_cullDist = 1e10f;

	mgr.FindNavMeshWs(&findNavMeshParams);

	if (!findNavMeshParams.m_pNavMesh || !findNavMeshParams.m_pNavPoly)
		return false;

	Level* pDestLevel = EngineComponents::GetLevelMgr()->GetLevel(findNavMeshParams.m_pNavMesh->GetLevelId());
	if (!pDestLevel || !pDestLevel->IsLoaded())
		return false;

	Point destPoint = findNavMeshParams.m_pNavPoly->GetCentroid();
	destPoint = findNavMeshParams.m_pNavMesh->LocalToWorld(destPoint);

	const Locator locWs(destPoint, GetLocator().Rot());
	const BoundFrame bf(locWs, Binding());
	TeleportToBoundFrame(bf);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavCharacter::FilterGroundPosition(Point_arg targetGroundPosPs, Vector_arg targetGroundNormalPs)
{
	NAV_ASSERT(IsReasonable(targetGroundPosPs));
	NAV_ASSERT(IsReasonable(targetGroundNormalPs));

	NAV_ASSERT(IsReasonable(m_filteredGroundNormalPs));

#ifndef FINAL_BUILD
	const bool kDebug = false;

	PrimServerWrapper ps(GetParentSpace());
	ps.EnableHiddenLineAlpha();
	Point arrowEndPs;
#endif

	const Point groundPosPs = GetGroundPosPs();
	const Vector groundNormalPs = GetGroundNormalPs();
#ifndef FINAL_BUILD
	if (kDebug)
	{
		ps.DrawPlaneCross(groundPosPs, groundNormalPs, kColorWhiteTrans);
		arrowEndPs = groundPosPs + groundNormalPs * 1.75f;
		ps.DrawString(arrowEndPs, "actual", kColorWhiteTrans, 0.5f);
		ps.DrawArrow(groundPosPs, arrowEndPs, 0.5f, kColorWhiteTrans);

		ps.DrawPlaneCross(targetGroundPosPs, targetGroundNormalPs, kColorRedTrans);
		arrowEndPs = targetGroundPosPs + targetGroundNormalPs * 1.5f;
		ps.DrawString(arrowEndPs, "target", kColorRedTrans, 0.5f);
		ps.DrawArrow(targetGroundPosPs, arrowEndPs, 0.5f, kColorRedTrans);
	}
#endif

	const Point filteredGroundPosPs = GetFilteredGroundPosPs();
	const Vector filteredGroundNormalPs = GetFilteredGroundNormalPs();
#ifndef FINAL_BUILD
	if (kDebug)
	{
		ps.DrawPlaneCross(filteredGroundPosPs, filteredGroundNormalPs, kColorCyanTrans);
		arrowEndPs = filteredGroundPosPs + filteredGroundNormalPs * 1.25f;
		ps.DrawString(arrowEndPs, "start", kColorCyanTrans, 0.5f);
		ps.DrawArrow(filteredGroundPosPs, arrowEndPs, 0.5f, kColorCyanTrans);
	}
#endif

	const float dt = GetProcessDeltaTime();

	{	// Smooth normal
		m_filteredGroundNormalPs = m_groundNormalSpring.Track(m_filteredGroundNormalPs,
															targetGroundNormalPs,
															dt,
															g_navCharOptions.m_groundSmoothingStrength);
		m_filteredGroundNormalPs = SafeNormalize(m_filteredGroundNormalPs, kUnitYAxis);
	}

	{	// Smooth the ground position so small bumps will be ignored (like shock absorbers)
		// The smoothing is relative to the surface normal to be more accurate when traveling on an incline
		const Plane groundPlanePs = Plane(groundPosPs, groundNormalPs);

		const float curGroundHeightDelta = groundPlanePs.Dist(filteredGroundPosPs);
		const int trackDir = (curGroundHeightDelta >= 0.0f ? 1 : -1);

		const float smoothedHeightDelta = m_groundHeightDeltaSpring.Track(curGroundHeightDelta,
																		0.0f,
																		dt,
																		g_navCharOptions.m_groundSmoothingStrength,
																		trackDir);

		const Point midGroundPosPs = groundPlanePs.ProjectPoint(filteredGroundPosPs) + groundNormalPs * smoothedHeightDelta;
		const Plane midGroundPlane = Plane(midGroundPosPs, m_filteredGroundNormalPs);
#ifndef FINAL_BUILD
		if (kDebug)
		{
			ps.DrawPlaneCross(midGroundPosPs, m_filteredGroundNormalPs, kColorYellowTrans);
			arrowEndPs = midGroundPosPs + m_filteredGroundNormalPs * 1.0f;
			ps.DrawString(arrowEndPs, "mid", kColorYellowTrans, 0.5f);
			ps.DrawArrow(midGroundPosPs, arrowEndPs, 0.5f, kColorYellowTrans);
		}
#endif

		const Point candidatePosPs = midGroundPlane.ProjectPoint(targetGroundPosPs);
		const float candidateGroundHeightDelta = groundPlanePs.Dist(candidatePosPs);

		if ((trackDir == 1 && candidateGroundHeightDelta > smoothedHeightDelta) ||	// Candidate pos is higher
			(trackDir == -1 && candidateGroundHeightDelta < smoothedHeightDelta))	// Candidate pos is lower
		{
			// Adjust candidatePosPs so that it is vertically closer(than filteredGroundPosPs, which is the whole point of calculating smoothedHeightDelta),
			// to the ground plane, otherwise this will create an unwanted overshoot when transitioning in/out of slopes.
			m_filteredGroundPosPs = candidatePosPs - (candidateGroundHeightDelta - smoothedHeightDelta) * groundNormalPs;
		}
		else
		{
			m_filteredGroundPosPs = candidatePosPs;
		}

#ifndef FINAL_BUILD
		if (kDebug)
		{
			ps.DrawPlaneCross(m_filteredGroundPosPs, m_filteredGroundNormalPs, kColorGreenTrans);
			arrowEndPs = m_filteredGroundPosPs + m_filteredGroundNormalPs * 0.75f;
			ps.DrawString(arrowEndPs, "end", kColorGreenTrans, 0.5f);
			ps.DrawArrow(m_filteredGroundPosPs, arrowEndPs, 0.5f, kColorGreenTrans);
		}
#endif

		NAV_ASSERT(IsReasonable(m_filteredGroundPosPs));
	}

	return m_filteredGroundPosPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ResetGroundFilter()
{
	m_filteredGroundPosPs	 = m_groundPositionPs;
	m_filteredGroundNormalPs = m_groundNormalPs;

	m_groundNormalSpring.Reset();
	m_groundHeightDeltaSpring.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ResetGroundPositionAndFilter()
{
	const Point myPosPs = GetTranslationPs();
	const Vector upPs = GetLocalY(GetRotationPs());

	SetGroundPositionPs(myPosPs);
	SetGroundNormalPs(upPs);
	ResetGroundFilter();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point NavCharacter::AdjustToGround(Point_arg oldPosPs, Point_arg desiredPosPs)
{
	PROFILE(AI, NavChar_AdjustToGround);

	NAV_ASSERT(IsReasonable(oldPosPs));
	NAV_ASSERT(IsReasonable(desiredPosPs));

	Point adjustedPosPs = desiredPosPs;

	// get grounded position and apply filter
	Point clampedToGroundPosPs = Plane(m_groundPositionPs, m_groundNormalPs).ProjectPoint(adjustedPosPs);
	Point filteredGroundedPosPs = clampedToGroundPosPs;
	if (m_requestSnapToGround || !m_currentPat.m_bits)
	{
		ResetGroundFilter();
	}
	else
	{
		filteredGroundedPosPs = FilterGroundPosition(clampedToGroundPosPs, m_groundNormalPs);
		filteredGroundedPosPs = PointFromXzAndY(adjustedPosPs, filteredGroundedPosPs);
	}

	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_disableSmoothGround))
	{
		filteredGroundedPosPs = clampedToGroundPosPs;
	}

	NAV_ASSERT(IsReasonable(filteredGroundedPosPs));

	if (IsPlayingWorldRelativeAnim())
	{
		// we are in an action pack
		// detected ground?
		if (m_currentPat.m_bits)
		{
			Scalar deltaGroundY = filteredGroundedPosPs.Y() - oldPosPs.Y();
			//
			// y offset check added to fix case in village-2 where villagers in a house taking cover by a window were animating
			//  such that their align goes through a wall and caused them to pop down to a lower level of ground.  Now we only snap
			//  to the ground if the y distance is somewhat small.  This "fix" may^H^H^H WILL almost certainly cause other issues elsewhere.
			//
			// if y offset is small enough,
			if (Abs(deltaGroundY) < SCALAR_LC(0.5f))
			{
				const Vector originalDelta = desiredPosPs - oldPosPs;
				if (!m_requestSnapToGround && LengthSqr(originalDelta) > 0.0f)
				{
					// on ground
					adjustedPosPs.SetY(filteredGroundedPosPs.Y());

					const Vector delta = adjustedPosPs - oldPosPs;
					const Scalar deltaLen = Length(delta);
					const Scalar originalDeltaLen = Length(originalDelta);
					const Vector limitedDelta = Min(deltaLen, originalDeltaLen) * SafeNormalize(delta, kZero);

					adjustedPosPs = oldPosPs + limitedDelta;
				}
			}
		}
		m_secondsFalling = 0.0f;
	}
	else
	{
		const float dt = GetProcessDeltaTime();

		// not in an action pack
		bool allowFall = m_allowFall;
		// detected ground?
		if (m_currentPat.m_bits)
		{
			// on ground
			if (m_requestSnapToGround)
			{
				adjustedPosPs.SetY(filteredGroundedPosPs.Y());
			}
			else
			{
				// Prevent the NPC from moving faster on slopes than on flat ground.  Also,
				// prevent the NPC from popping up or down steps when moving slowly.
				//
				// originalDelta is the movement from animation, we shouldn't move any faster than that
				//   when going up or down slopes
				const Vector originalDelta = desiredPosPs - oldPosPs;
				const Scalar originalDeltaLen = Max(Length(originalDelta),
													Scalar(dt * m_minGroundAdjustSpeed));
				// delta is the direction to move in
				const Vector delta = filteredGroundedPosPs - oldPosPs;
				const Scalar deltaLen = Length(delta);
				// clamp the magnitude of movement to originalDeltaLen
				const Vector limitedDelta = Min(deltaLen, originalDeltaLen) * SafeNormalize(delta, kZero);

				adjustedPosPs = oldPosPs + limitedDelta;
			}
			m_secondsFalling = 0.0f;
		}
		else if (allowFall)
		{
			// "fall" ?
			adjustedPosPs = oldPosPs + Vector(0.0f, -m_fallSpeed * dt, 0.0f);
			m_secondsFalling += dt;
		}
	}

	NAV_ASSERT(IsReasonable(adjustedPosPs));

	return adjustedPosPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool WalkCheckUprightFlag(const AnimStateInstance* pInstance,
								 const AnimStateLayer* pStateLayer,
								 uintptr_t userData)
{
	bool& allStatesWantUpright = *(bool*)userData;
	if (pInstance->GetStateFlags() & DC::kAnimStateFlagNoAdjustToUpright)
	{
		allStatesWantUpright = false;
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame NavCharacter::AdjustBoundFrameToUpright(const BoundFrame& bf) const
{
	const BoundFrame& myBf = GetBoundFrame();

	BoundFrame ret = bf;

	switch (m_motionConfig.m_adjustUprightMode)
	{
	case MotionConfig::UprightMode::kWorldSpace:
		{
			// Normalize the and force the rotation to be upright.
			Quat newRotationWs = bf.GetRotationWs();
			newRotationWs.SetX(0.0f);
			newRotationWs.SetZ(0.0f);
			newRotationWs = Normalize(newRotationWs);
			ret.SetRotationWs(newRotationWs);
		}
		break;

	case MotionConfig::UprightMode::kParentSpace:
		if (bf.IsSameBinding(myBf.GetBinding()))
		{
			// Normalize the and force the rotation to be upright.
			Quat newRotationPs = bf.GetRotationPs();
			newRotationPs.SetX(0.0f);
			newRotationPs.SetZ(0.0f);
			newRotationPs = Normalize(newRotationPs);
			ret.SetRotationPs(newRotationPs);
		}
		else
		{
			Quat newRotationInMyPs = myBf.GetParentSpace().UntransformLocator(bf.GetLocatorWs()).GetRotation();
			newRotationInMyPs.SetX(0.0f);
			newRotationInMyPs.SetZ(0.0f);
			newRotationInMyPs = Normalize(newRotationInMyPs);
			ret.SetRotationPs(newRotationInMyPs);
		}
		break;

	default:
		break;
	};

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NavCharacter::AdjustLocatorToUprightPs(const Locator& locPs) const
{
	Locator retPs = locPs;

	switch (m_motionConfig.m_adjustUprightMode)
	{
	case MotionConfig::UprightMode::kWorldSpace:
		{
			const Locator& parentSpace = GetParentSpace();

			// Normalize the and force the rotation to be upright.
			Locator locWs = parentSpace.TransformLocator(locPs);

			Quat newRotationWs = locWs.GetRotation();
			newRotationWs.SetX(0.0f);
			newRotationWs.SetZ(0.0f);
			newRotationWs = Normalize(newRotationWs);
			locWs.SetRot(newRotationWs);

			retPs = parentSpace.UntransformLocator(locWs);
		}
		break;

	case MotionConfig::UprightMode::kParentSpace:
		{
			// Normalize the and force the rotation to be upright.
			Quat newRotationPs = locPs.Rot();
			newRotationPs.SetX(0.0f);
			newRotationPs.SetZ(0.0f);
			newRotationPs = Normalize(newRotationPs);

			retPs.SetRot(newRotationPs);
		}
		break;

	default:
		break;
	};

	return retPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NavCharacter::AdjustLocatorToUprightWs(const Locator& locWs) const
{
	Locator retWs = locWs;

	switch (m_motionConfig.m_adjustUprightMode)
	{
	case MotionConfig::UprightMode::kWorldSpace:
		{
			// Normalize the and force the rotation to be upright.
			Quat newRotationWs = locWs.GetRotation();
			newRotationWs.SetX(0.0f);
			newRotationWs.SetZ(0.0f);
			newRotationWs = Normalize(newRotationWs);

			retWs.SetRot(newRotationWs);
		}
		break;

	case MotionConfig::UprightMode::kParentSpace:
		{
			const Locator& parentSpace = GetParentSpace();

			Locator locPs = parentSpace.UntransformLocator(locWs);

			// Normalize the and force the rotation to be upright.
			Quat newRotationPs = locPs.Rot();
			newRotationPs.SetX(0.0f);
			newRotationPs.SetZ(0.0f);
			newRotationPs = Normalize(newRotationPs);
			locPs.SetRot(newRotationPs);

			retWs = parentSpace.TransformLocator(locPs);
		}
		break;

	default:
		break;
	};

	return retWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::AdjustToUpright(const AnimControl* pAnimControl)
{
	PROFILE(AI, NavChar_AdjustToUpright);

	bool allStatesWantUpright = true;

	const AnimStateLayer* pBaseLayer = pAnimControl->GetStateLayerById(SID("base"));
	pBaseLayer->WalkInstancesNewToOld(WalkCheckUprightFlag, (uintptr_t)&allStatesWantUpright);

	if (allStatesWantUpright)
	{
		const BoundFrame& curBf = GetBoundFrame();
		const BoundFrame newBf = AdjustBoundFrameToUpright(curBf);
		SetBoundFrame(newBf);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
class NavCharAnimAlignBlender : public NdAnimAlign::AnimAlignBlender
{
public:
	typedef NdAnimAlign::AnimAlignBlender ParentClass;

	NavCharAnimAlignBlender(const BoundFrame& currentAlign,
							const NavCharacter* pNavChar,
							NdAnimAlign::InstanceAlignTable* pInstanceTablePs,
							bool debugDraw)
		: AnimAlignBlender(pNavChar,
						   currentAlign,
						   pNavChar->GetParentSpace(),
						   pNavChar->GetUniformScale(),
						   pInstanceTablePs,
						   debugDraw)
		, m_instanceCount(0)
	{
		m_noAdjustToGround = pNavChar->PreventAdjustToGround();
	}

	virtual ~NavCharAnimAlignBlender() override {}

	virtual Locator AdjustLocatorToUpright(const Locator& loc) const override
	{
		Locator ret = loc;

		if (const NavCharacter* pNavChar = GetCharacter())
		{
			ret = pNavChar->AdjustLocatorToUprightPs(loc);
		}

		return ret;
	}

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, NdAnimAlign::LocatorData* pDataOut) override
	{
		NdAnimAlign::LocatorData parentData(Locator(kIdentity), NdAnimAlign::kInvalidInterp);

		if (!ParentClass::GetDataForInstance(pInstance, &parentData))
		{
			return false;
		}

		const NavCharacter* pNavChar = GetCharacter();
		const Locator& parentSpace = pNavChar->GetParentSpace();
		const DC::AnimStateFlag stateFlags = pInstance->GetStateFlags();
		const INavAnimController* pNavAnimController = pNavChar->GetActiveNavAnimController();

		const Locator rawAnimAlignPs = parentData.m_locator;

		Locator alignLocPs = rawAnimAlignPs;

		Locator navProcAlign = alignLocPs;

		if (pNavAnimController
			&& pNavAnimController->CalculateProceduralAlign(pInstance, m_baseAlign.GetLocatorPs(), navProcAlign))
		{
			alignLocPs = navProcAlign;
		}

		if (((stateFlags & DC::kAnimStateFlagNoAdjustToGround) == 0) && !pNavChar->IsSwimming() && !m_noAdjustToGround)
		{
			//const Point groundPosPs = pNavChar->GetGroundPosPs();
			const Point groundPosPs = pNavChar->GetFilteredGroundPosPs();
			const Point alignOnGroundPs = PointFromXzAndY(alignLocPs.Pos(), groundPosPs);

			const float tt = GetGroundAdjustFactorForInstance(pInstance, pNavChar);
			if (tt > 0.0f)
			{
				const Point adjustedPosPs = Lerp(alignLocPs.Pos(), alignOnGroundPs, tt);
				alignLocPs.SetTranslation(adjustedPosPs);
			}
		}

		if (FALSE_IN_FINAL_BUILD(m_debugDraw))
		{
			Locator labelLocPs = alignLocPs;
			labelLocPs.SetPos(alignLocPs.Pos() + Vector(0.0f, 0.05f, 0.0f) * float(m_instanceCount + 1));

			PrimServerWrapper ps = PrimServerWrapper(parentSpace);
			if (stateFlags & (DC::kAnimStateFlagApMoveUpdate | DC::kAnimStateFlagFirstAlignRefMoveUpdate))
			{
				const Locator apRefWs = pInstance->GetApLocator().GetLocatorWs();
				const Vector offsetWs = Vector(0.0f, 0.05f, 0.0f) * float(m_instanceCount + 1);
				const Locator apDrawLocWs = Locator(apRefWs.Pos(), apRefWs.Rot());

				g_prim.Draw(DebugCoordAxes(apRefWs, 0.05f));

				StringBuilder<256> desc;

				if (pNavAnimController && pNavAnimController->ShouldDoProceduralApRef(pInstance))
				{
					desc.append_format("proc-");
				}

				desc.append_format("ap-%u [%s]", m_instanceCount, DevKitOnly_StringIdToString(pInstance->GetStateName()));

				if (const RigidBody* pBoundRb = pInstance->GetApLocator().GetBinding().GetRigidBody())
				{
					const NdGameObject* pOwner = pBoundRb->GetOwner();
					const StringId64 ownerId = pOwner ? pOwner->GetUserId() : INVALID_STRING_ID_64;
					char text[256];
					GetBindingString(pBoundRb, ownerId, text, sizeof(text));

					desc.append_format(" (%s)", text);
				}

				g_prim.Draw(DebugString(apDrawLocWs.Pos() + offsetWs, desc.c_str(), kColorWhite, 0.5f));

				g_prim.Draw(DebugLine(apDrawLocWs.Pos(),
									  alignLocPs.Pos(),
									  kColorCyan,
									  kColorMagenta,
									  2.0f,
									  kPrimEnableHiddenLineAlpha));

				const Vector adjustmentPs = pInstance->GetApRestrictAdjustmentPs();
				if (Length(adjustmentPs) > 0.1f)
				{
					const Vector adjustmentWs = pInstance->GetApLocator().GetParentSpace().TransformVector(adjustmentPs);
					Locator orgLocatorWs = apDrawLocWs;
					orgLocatorWs.SetPos(orgLocatorWs.Pos() - adjustmentWs);
					g_prim.Draw(DebugCoordAxes(orgLocatorWs, 0.05f));
					g_prim.Draw(DebugString(orgLocatorWs.Pos(),
											StringBuilder<128>("org-ap-%u [%s]",
															   m_instanceCount,
															   DevKitOnly_StringIdToString(pInstance->GetStateName()))
												.c_str(),
											kColorWhite,
											0.5f));
					g_prim.Draw(DebugLine(apDrawLocWs.Pos(),
										  orgLocatorWs.Pos(),
										  kColorRed,
										  kColorGreen,
										  2.0f,
										  kPrimEnableHiddenLineAlpha));
				}
			}

			if (DebugSelection::Get().IsProcessSelected(pNavChar))
			{
				MsgCon("[%06.3f] [%d : %s @ %0.3f]: %f (raw %f) (phase-delta %f)\n",
					   pNavChar->GetClock()->GetCurTime().ToPrintableSeconds(),
					   m_instanceCount,
					   DevKitOnly_StringIdToString(pInstance->GetStateName()),
					   pInstance->MotionFade(),
					   float(Dist(m_baseAlign.GetTranslationPs(), alignLocPs.Pos())),
					   float(Dist(m_baseAlign.GetTranslationPs(), rawAnimAlignPs.Pos())),
					   pInstance->Phase() - pInstance->PrevPhase());
			}

			StringBuilder<256> desc;
			desc.append_format("%d-%s", m_instanceCount, DevKitOnly_StringIdToString(pInstance->GetStateName()));

			if ((stateFlags & DC::kAnimStateFlagSaveTopAlign) && pInstance->HasSavedAlign())
			{
				desc.append_format(" [saved]");
			}
			else if ((stateFlags & DC::kAnimStateFlagNoAdjustToGround) == 0 && !m_noAdjustToGround)
			{
				// g_prim.Draw(DebugCross(pNavChar->GetFilteredGroundPosPs(), 0.1f, kColorRed, kPrimEnableHiddenLineAlpha));

				const float tt = GetGroundAdjustFactorForInstance(pInstance, pNavChar);
				desc.append_format(" [ground @ %0.3f]", tt);
			}

			ps.DrawString(labelLocPs.Pos(), desc.c_str(), kColorWhite, 0.5f);
			ps.DrawCoordAxes(alignLocPs, 0.1f);
		}

		if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_disableMovement))
		{
			alignLocPs = m_baseAlign.GetLocatorPs();
		}

		++m_instanceCount;

		*pDataOut = NdAnimAlign::LocatorData(alignLocPs, NdAnimAlign::kLinearInterp);
		return true;
	}

	virtual NdAnimAlign::LocatorData BlendData(const NdAnimAlign::LocatorData& leftData,
											   const NdAnimAlign::LocatorData& rightData,
											   float masterFade,
											   float animFade,
											   float motionFade) override
	{
		Locator alignLocPs = ParentClass::BlendData(leftData, rightData, masterFade, animFade, motionFade).m_locator;

		if (FALSE_IN_FINAL_BUILD(m_debugDraw))
		{
			const NavCharacter* pNavChar = GetCharacter();
			const Locator& parentSpace = pNavChar->GetParentSpace();
			PrimServerWrapper ps = PrimServerWrapper(parentSpace);

			const Color interpClr = Slerp(kColorRed, kColorGreen, motionFade);
			ps.DrawLine(leftData.m_locator.Pos(), alignLocPs.Pos(), kColorRed, interpClr);
			ps.DrawLine(rightData.m_locator.Pos(), alignLocPs.Pos(), kColorGreen, interpClr);
			ps.DrawCoordAxes(alignLocPs, 0.2f);
		}

		return NdAnimAlign::LocatorData(alignLocPs, leftData.m_flags | rightData.m_flags);
	}

	const NavCharacter* GetCharacter() const { return (const NavCharacter*)m_pObject; }

	U32 m_instanceCount;
	bool m_noAdjustToGround;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool WalkApplyProcApRot(AnimStateInstance* pInstance, AnimStateLayer* pStateLayer, uintptr_t userData)
{
	const INavAnimController* pNavAnimController = (const INavAnimController*)userData;
	if (!pNavAnimController)
		return false;

	pNavAnimController->CalculateProceduralApRef(pInstance);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class StairsCapableBlender : public AnimStateLayer::InstanceBlender<float>
{
	virtual float GetDefaultData() const override { return 0.0f; }
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut) override
	{
		if (!pInstance)
			return false;

		const StringId64 stateId = pInstance->GetStateName();

		float val = 0.0f;

		switch (stateId.GetValue())
		{
		case SID_VAL("s_move-fw"):
		case SID_VAL("s_idle^move^fw-loco"):
		case SID_VAL("s_move-fw^idle"):
			val = 1.0f;
			break;
		}

		*pDataOut = val;
		return true;
	}

	virtual float BlendData(const float& leftData,
							const float& rightData,
							float masterFade,
							float animFade,
							float motionFade) override
	{
		return Lerp(leftData, rightData, motionFade);
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
static const Locator CalculateDesiredNewPositionPs(const NavCharacter* pNavChar,
												   const BoundFrame& currentAlign,
												   const NavStateMachine* pNavStateMachine,
												   NdAnimAlign::InstanceAlignTable* pInstanceTablePsOut = nullptr,
												   Locator* pAnimDesiredAlignPs = nullptr,
												   bool debugDraw = false)
{
	PROFILE(AI, CalculateDesiredNewPositionPs);

	const Locator& parentSpace = pNavChar->GetParentSpace();
	const Locator& currentAlignPs = currentAlign.GetLocatorPs();

	AnimControl* pAnimControl  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const AnimStateLayer* pConstBaseLayer = pAnimControl->GetBaseStateLayer();
	const INavAnimController* pNavAnimController = pNavChar->GetActiveNavAnimController();

	pBaseLayer->WalkInstancesOldToNew(WalkApplyProcApRot, (uintptr_t)pNavAnimController);

	NdAnimAlign::LocatorData initialData = NdAnimAlign::LocatorData(currentAlignPs, NdAnimAlign::kInvalidInterp);
	NavCharAnimAlignBlender b = NavCharAnimAlignBlender(currentAlign, pNavChar, pInstanceTablePsOut, debugDraw);

	const Locator animAlignPs = b.BlendForward(pConstBaseLayer, initialData).m_locator;

	Locator finalAlignPs = animAlignPs;
	if (pAnimDesiredAlignPs)
	{
		*pAnimDesiredAlignPs = animAlignPs;
	}

	PrimServerWrapper ps = PrimServerWrapper(parentSpace);
/*
	ps.DrawString(finalAlignPs.Pos() + (0.25f * GetLocalY(finalAlignPs.Rot())), "F*");
	ps.DrawCoordAxes(finalAlignPs, 0.25f);
*/

	if (AnimSimpleLayer* pStairsLayer = pAnimControl->GetSimpleLayerById(SID("stairs")))
	{
		StairsCapableBlender stairsEnabledBlender;
		const float stairsCapacityVal = stairsEnabledBlender.BlendForward(pBaseLayer, 0.0f);
		const float stairsLayerVal = pStairsLayer->GetCurrentFade();
		const float stairsBlend = stairsLayerVal * stairsCapacityVal;

		if (stairsBlend > 0.0f)
		{
			Locator stairAlignDeltaPs(kOrigin);
			if (pStairsLayer->GetAnimAlignDelta(currentAlignPs, stairAlignDeltaPs))
			{
				Locator stairAlignPs = currentAlignPs.TransformLocator(stairAlignDeltaPs);

				// use only the travel distance and vertical displacement of stair align
				// the base layer should maintain control of align rotation and the xz-direction of movement
				const Vector flatBaseMovementDirPs = SafeNormalize(finalAlignPs.Pos() - currentAlignPs.Pos(), kZero);
				const Vector stairMovementPs = stairAlignDeltaPs.Pos() - Point(kOrigin);
				const Scalar stairFlatMovementDist = LengthXz(stairMovementPs);
				const Vector directedStairMovementPs = stairFlatMovementDist*flatBaseMovementDirPs + Vector(kUnitYAxis)*stairMovementPs.Y();
				const Point directedStairAlignPosPs = currentAlignPs.Pos() + directedStairMovementPs;

				const Point stairsPosPs = Lerp(finalAlignPs.Pos(), directedStairAlignPosPs, stairsBlend);
				const Locator stairsLocPs = Locator(stairsPosPs, finalAlignPs.Rot());

				finalAlignPs = stairsLocPs;

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					g_prim.Draw(DebugCoordAxesLabeled(parentSpace.TransformLocator(stairsLocPs),
													  StringBuilder<256>("stairs @ %0.2f * %0.2f = %0.2f",
																		 stairsCapacityVal,
																		 stairsLayerVal,
																		 stairsBlend).c_str(),
																		 0.3f,
																		 PrimAttrib(),
																		 2.0f,
																		 kColorWhite,
																		 0.5f));
				}
			}
		}
	}

	/*if (AnimStateLayer* pHrLayer = pAnimControl->GetStateLayerById(SID("hit-reaction-partial-override")))
	{
		//bool hackE3Scripted = pNavChar->GetCurrentMeleeAction() && pNavChar->GetCurrentMeleeAction()->IsScripted();
		if (!pHrLayer->IsFadedOut() && pHrLayer->CurrentStateInstance())
		{
			const float layerVal = pHrLayer->GetCurrentFade();

			if (layerVal > 0.0f)
			{
				NdAnimAlign::LocatorData initialData2 = NdAnimAlign::LocatorData(currentAlignPs, NdAnimAlign::kInvalidInterp);

				finalAlignPs = b.BlendForward(pHrLayer, initialData2).m_locator;

				/ *if (pHrLayer->GetAnimAlignDelta(currentAlignPs, hrAlignDeltaPs))
				{
					finalAlignPs = currentAlignPs.TransformLocator(hrAlignDeltaPs);

					if (FALSE_IN_FINAL_BUILD(debugDraw))
					{
						g_prim.Draw(DebugCoordAxesLabeled(finalAlignPs.GetTranslation(),
							StringBuilder<256>("hr @ %0.2f",
							layerVal
							).c_str(),
							0.3f,
							PrimAttrib(),
							2.0f,
							kColorWhite,
							0.5f));
					}
				}* /
			}
		}
	}*/

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		ps.DrawArrow(currentAlignPs.Pos(), GetLocalZ(currentAlignPs.Rot()), 0.2f, kColorGreenTrans);
		ps.DrawString(currentAlignPs.Pos() + GetLocalZ(currentAlignPs.Rot()), "S", kColorWhite, 0.75f);
		ps.DrawCoordAxes(currentAlignPs, 0.125f);

		ps.DrawArrow(finalAlignPs.Pos(), GetLocalZ(finalAlignPs.Rot()), 0.2f, kColorRedTrans);
		ps.DrawString(finalAlignPs.Pos() + GetLocalZ(finalAlignPs.Rot()), "F", kColorWhite, 0.75f);
		ps.DrawCoordAxes(finalAlignPs, 0.15f);
	}

	return finalAlignPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
enum AdjustToBlockersFlags
{
	kNoAdjustToCharacters = 1u << 0,
	kNoAdjustToPlayer	  = 1u << 1
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool AdjustToNavObeyedBlockers(const SimpleNavControl* pNavControl,
									  const DynamicNavBlocker* pBlocker,
									  BoxedValue userData,
									  bool forPathing,
									  bool debug)
{
	const AdjustToBlockersFlags flags = (AdjustToBlockersFlags)userData.GetU64();

	if (((flags & kNoAdjustToCharacters) != 0) && (pBlocker->GetOwner().IsKindOf(g_type_Character)))
	{
		return false;
	}

	const bool adjustToPlayer = (flags & kNoAdjustToPlayer) == 0;

	if (!adjustToPlayer && pBlocker && pBlocker->GetOwner().Valid()
		&& EngineComponents::GetNdGameInfo()->IsPlayerHandle(pBlocker->GetOwner()))
	{
		return false;
	}

	if (pBlocker)
	{
		if (const Character* pChar = Character::FromProcess(pBlocker->GetOwner().ToProcess()))
		{
			if (pChar->IsBuddyNpc())
			{
				if (!pChar->IsNavBlockerEnabled())
					return false;
			}
		}
	}

	return SimpleNavControl::DefaultNavBlockerFilter(pNavControl, pBlocker, userData, forPathing, debug);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::UpdateDepenSegmentScale()
{
	const F32 kMaxScaleAdjustRate = 2.0f;

	F32 desiredScale = 1.0f;
	const NdSubsystem* pSubsystem = GetSubsystemMgr()->FindSubsystem(SID("CharacterMotionMatchLocomotion"));
	if (pSubsystem && pSubsystem->IsKindOf(SID("CharacterMotionMatchLocomotion")))
	{
		const CharacterMotionMatchLocomotion* pController = static_cast<const CharacterMotionMatchLocomotion*>(pSubsystem);
		const MotionModel& motionModel = pController->GetMotionModelPs();
		const F32 radPerSec = Abs(motionModel.GetYawSpeed());
		desiredScale		= LerpScale(PI * 0.5f, PI * 1.5f, 1.0f, 0.0f, radPerSec);
	}

	if (m_depenetrationSegmentScaleScriptTarget < 0.0f)
	{
		if (m_depenSegmentInScriptedOverride) // Return to normalcy
		{
			const DC::NpcDemeanorDef* pDemeanorDef = GetCurrentDemeanorDef();
			if (pDemeanorDef)
			{
				desiredScale = Limit01(Max(desiredScale, pDemeanorDef->m_minDepenCapsuleScale));
			}

			Seek(m_depenetrationSegmentScale, desiredScale, kMaxScaleAdjustRate * GetProcessDeltaTime());

			if (Abs(desiredScale - m_depenetrationSegmentScale) < NDI_FLT_EPSILON)
				m_depenSegmentInScriptedOverride = false;

		}
		else // Regular systemic
		{
			Seek(m_depenetrationSegmentScale, desiredScale, kMaxScaleAdjustRate * GetProcessDeltaTime());

			const DC::NpcDemeanorDef* pDemeanorDef = GetCurrentDemeanorDef();
			if (pDemeanorDef)
			{
				m_depenetrationSegmentScale = Limit01(Max(m_depenetrationSegmentScale, pDemeanorDef->m_minDepenCapsuleScale));
			}
		}
	}
	else // Seek to scripted target
	{
		Seek(m_depenetrationSegmentScale, Limit01(m_depenetrationSegmentScaleScriptTarget), kMaxScaleAdjustRate * GetProcessDeltaTime());
	}

	m_depenetrationSegmentScaleScriptTarget = -1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Segment NavCharacter::GetDepenetrationSegmentWs(Point basePoint, bool allowCapsuleShape) const
{
	if (!allowCapsuleShape)
		return Segment(basePoint, basePoint);

	Vector localZWs = GetLocalZ(GetLocator());
	return Segment(basePoint - (localZWs * m_depenetrationOffsetBw * m_depenetrationSegmentScale), basePoint + (localZWs * m_depenetrationOffsetFw * m_depenetrationSegmentScale));
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavCharacter::GetMaxDepenSegmentComponent() const
{
	return Max(m_depenetrationOffsetBw, m_depenetrationOffsetFw);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Segment NavCharacter::GetDepenetrationSegmentPs(Point basePoint, bool allowCapsuleShape) const
{
	if (!allowCapsuleShape)
		return Segment(basePoint, basePoint);

	Vector localZPs = GetLocalZ(GetLocatorPs());
	return Segment(basePoint - (localZPs * m_depenetrationOffsetBw * m_depenetrationSegmentScale), basePoint + (localZPs * m_depenetrationOffsetFw * m_depenetrationSegmentScale));
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavCharacter::AdjustRootToNavSpace(Point_arg currentPosPs,
										 Point_arg adjustedPosPs,
										 float navMeshAdjustFactor,
										 float deltaTime,
										 bool shouldAdjustToPlayer)
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	const Locator& parentSpace = GetParentSpace();

	const AnimControl* pAnimControl = GetAnimControl();
	const AnimStateLayer* pConstBaseLayer = pAnimControl->GetBaseStateLayer();
	const DC::AnimState* pCurrentState = pConstBaseLayer->CurrentState();
	const DC::AnimStateFlag currentAnimStateFlags = pCurrentState ? pCurrentState->m_flags : 0;

	const NavControl* pNavControl = GetNavControl();
	const INavAnimController* pNavAnimController = GetActiveNavAnimController();

	float tt = navMeshAdjustFactor;
	if (m_ragdollStumble)
	{
		tt = 1.0f;
	}

	Point navAdjustedPosPs;
	const NavMesh* pNavMesh = pNavControl->GetNavLocation().ToNavMesh();
	const NavPoly* pNavPoly = pNavControl->GetNavLocation().ToNavPoly();
	const bool crouched = IsCrouched();
	const bool onSmallNavMesh = pNavMesh ? (pNavMesh->GetMaxOccupancyCount() <= 1) : false;
	bool shouldAdjustHeadToNav = crouched && !onSmallNavMesh;

	// if we are playing a hit reaction
	if (IsInScriptedAnimationState())
	{
		shouldAdjustHeadToNav = false;
	}
	else if (m_pNavStateMachine->IsInterrupted())
	{
		shouldAdjustHeadToNav = true;
	}
	else if (m_pNavStateMachine->GetActionPackUsageState() != NavStateMachine::kActionPackUsageNone)
	{
		shouldAdjustHeadToNav = false;
	}
	else if (AlwaysWantAdjustHeadToNav())
	{
		shouldAdjustHeadToNav = true;
	}
	else if (pNavAnimController)
	{
		// if we are stopped or coming to a stop and not approaching an action pack,
		shouldAdjustHeadToNav = pNavAnimController->ShouldAdjustHeadToNav() && !m_pNavStateMachine->IsNextWaypointActionPackEntry() && !onSmallNavMesh;
	}

	shouldAdjustHeadToNav = shouldAdjustHeadToNav && m_allowAdjustHeadToNav && !IsCheap();

	const bool staticOnly = currentAnimStateFlags & DC::kAnimStateFlagNoAdjustToDynamicNavBlockers;
	NavBlockerBits obeyedBlockers;
	if (staticOnly || (IsBuddyNpc() && !IsNavBlockerEnabled()))
	{
		obeyedBlockers.ClearAllBits();
	}
	else
	{
		const bool shouldAdjustToPlayerNavBlocker = IsBuddyNpc() ? false : shouldAdjustToPlayer;

		U64 flags = 0;

		if (!shouldAdjustToPlayerNavBlocker)
			flags |= kNoAdjustToPlayer;

		if (m_disableAdjustToCharBlockers)
			flags |= kNoAdjustToCharacters;

		obeyedBlockers = Nav::BuildObeyedBlockerList(pNavControl,
													 AdjustToNavObeyedBlockers,
													 false,
													 flags);

		if (pNavControl->HasCustomNavBlockerFilterFunc())
		{
			const NavBlockerBits& customObeyedBlockers = pNavControl->GetCachedObeyedNavBlockers();
			NavBlockerBits::BitwiseAnd(&obeyedBlockers, obeyedBlockers, customObeyedBlockers);
		}
	}

	const float navAdjustRadius = pNavControl->GetCurrentNavAdjustRadius();
	const NavMesh::NpcStature minNpcStature = pNavControl->GetMinNpcStature();

	if (shouldAdjustHeadToNav)
	{
		const AttachSystem* pAs = GetAttachSystem();

		// if we are playing a hit reaction or stopped/stopping we try to keep NPC from putting head through walls and such.
		// We can't do this while NPC is navigating however, it will interfere with the NPCs ability to navigate
		m_adjustJointsToNavSmoothing = Min(1.0f, m_adjustJointsToNavSmoothing + deltaTime * 2.0f);
		const U32F kMaxAdjustVectorCount = kMaxAdjustToNavIndexCount + 1;
		Sphere adjustSpheresPs[kMaxAdjustVectorCount];
		const Vector toHeadOffsetPs = GetLocatorPs().TransformPoint(m_lookOriginOs.Pos()) - currentPosPs;
		adjustSpheresPs[0] = Sphere(Point(kOrigin) + toHeadOffsetPs * m_adjustJointsToNavSmoothing, 0.15f);

		U32F adjustSphereCount = 1;
		for (U32F i = 0; i < kMaxAdjustToNavIndexCount; ++i)
		{
			AttachIndex attachIndex = m_adjustToNavIndex[i];
			if (attachIndex != AttachIndex::kInvalid)
			{
				const Point attachPosWs = pAs->GetAttachPosition(attachIndex);
				const Point attachPosPs = parentSpace.UntransformPoint(attachPosWs);
				const Vector adjustVecPs = attachPosPs - currentPosPs;
				NAV_ASSERT(adjustSphereCount < kMaxAdjustVectorCount);
				adjustSpheresPs[adjustSphereCount] = Sphere(Point(kOrigin) + adjustVecPs * m_adjustJointsToNavSmoothing,
															0.15f);
				adjustSphereCount++;
			}
		}

		const bool wasAdjusted = AdjustMoveToNavSpacePs(currentPosPs,
														adjustedPosPs,
														navAdjustRadius,
														obeyedBlockers,
														adjustSpheresPs,
														adjustSphereCount,
														&navAdjustedPosPs);

		if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_displayAdjustHeadToNav))
		{
			g_prim.Draw(DebugLine(GetTranslation(),
								  VectorXz(parentSpace.TransformVector(adjustSpheresPs[0].GetCenter() - Point(kOrigin))),
								  kColorBlack,
								  kColorYellow,
								  3.0f,
								  kPrimEnableHiddenLineAlpha));
		}
	}
	else
	{
		// we may be navigating, just worry about keeping the align on the navmap
		const bool wasAdjusted = AdjustMoveToNavSpacePs(currentPosPs,
														adjustedPosPs,
														navAdjustRadius,
														obeyedBlockers,
														nullptr,
														0,
														&navAdjustedPosPs);

		m_adjustJointsToNavSmoothing = 0.0f;
	}

	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_displayNavAdjustDist))
	{
		const float navAdjustDist = DistXz(adjustedPosPs, navAdjustedPosPs);
		g_prim.Draw(DebugArrow(adjustedPosPs, navAdjustedPosPs, kColorBlack, 0.3f), kPrimDuration1FramePauseable);
		MsgConPauseable("navAdjustDist: %f\n", navAdjustDist);
	}

	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_displayNavDepenCapsule))
	{
		Segment depenSegment = GetDepenetrationSegmentWs(GetTranslation());
		g_prim.Draw(DebugCapsule(depenSegment.a, depenSegment.b, GetMaximumNavAdjustRadius(), kColorRedTrans),
					kPrimDuration1FramePauseable);
	}

	{
		bool stillOffNav = false;

		if (const NavMesh* pMesh = pNavControl->GetNavMesh())
		{
			NavMesh::ClearanceParams params;
			params.m_point = pMesh->ParentToLocal(navAdjustedPosPs);
			params.m_radius = navAdjustRadius * 0.85f;
			params.m_dynamicProbe = true;
			params.m_obeyedBlockers = obeyedBlockers;
			params.m_obeyedStaticBlockers = pNavControl->GetObeyedStaticBlockers();
			params.m_minNpcStature = minNpcStature;

			Segment capsulePs = GetDepenetrationSegmentPs(navAdjustedPosPs, true);
			params.m_capsuleSegment = capsulePs;

			const NavMesh::ProbeResult res = pMesh->CheckClearanceLs(&params);

			stillOffNav = res != NavMesh::ProbeResult::kReachedGoal;
		}

		if (stillOffNav)
		{
			const Point navMeshAdjustedPosPs = AI::AdjustPointToNavMesh(this,
																		pNavMesh,
																		pNavPoly,
																		navAdjustedPosPs,
																		minNpcStature,
																		navAdjustRadius,
																		2.0f * GetProcessDeltaTime());
			const Vector navAdjustDirPs = AsUnitVectorXz(navAdjustedPosPs - adjustedPosPs, kZero);
			const Vector navMeshAdjustDirPs = AsUnitVectorXz(navMeshAdjustedPosPs - adjustedPosPs, kZero);
			const float dotP = Dot(navAdjustDirPs, navMeshAdjustDirPs);

			if (dotP < 0.75f)
			{
				navAdjustedPosPs = navMeshAdjustedPosPs;
			}
		}
	}

	const Point finalAdjustedPosPs = Lerp(adjustedPosPs, navAdjustedPosPs, tt);
	NAVCHAR_ASSERT(IsReasonable(adjustedPosPs));
	NAVCHAR_ASSERT(Length(adjustedPosPs) < 100000.0f);

	return finalAdjustedPosPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static NavMesh::NpcStature CalculateMinStatureFromDemeanor(const DC::NpcDemeanor dem)
{
	if (dem == DC::kNpcDemeanorCrawl)
	{
		return NavMesh::NpcStature::kProne;
	}
	else if (dem == DC::kNpcDemeanorCrouch)
	{
		return NavMesh::NpcStature::kCrouch;
	}
	else
	{
		return NavMesh::NpcStature::kStand;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMesh::NpcStature NavCharacter::GetMinNavMeshStature() const
{
	const DC::NpcDemeanor curDem = GetCurrentDcDemeanor();

	return CalculateMinStatureFromDemeanor(curDem);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetupPathContext(Nav::PathFindContext& context) const
{
	context.m_ownerLocPs = GetLocatorPs();
	if (const ActionPack* const pReservedAp = GetReservedActionPack())
		context.m_ownerReservedApMgrId = pReservedAp->GetMgrId();

	if (IsKindOf(SID("Horse")))
		context.m_isHorse = true;

	Nav::CombatVectorInfo combatVectorInfo;

	if (GetCombatVectorInfo(combatVectorInfo))
	{
		context.m_hasCombatVector  = true;
		context.m_combatVectorInfo = combatVectorInfo;
	}

	context.m_numMeleePenaltySegments = GetMeleePathPenaltySegments(context.m_aMeleePenaltySegment, ARRAY_COUNT(context.m_aMeleePenaltySegment));
	context.m_hasMeleePenaltySegments = context.m_numMeleePenaltySegments > 0;

	context.m_threatPositionCount = GetThreatsPs(context.m_threatPositionsPs, context.m_threatFuturePosPs);
	context.m_friendPositionCount = GetFriendsPs(context.m_friendPositionsPs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::UpdateRootLocator()
{
	PROFILE(AI, NavChar_UpdateRootLocator);

	const BoundFrame& currentAlign = GetBoundFrame();
	const Locator& parentSpace = GetParentSpace();

	INavAnimController* pNavAnimController = GetActiveNavAnimController();
	const NavControl* pNavControl = GetNavControl();

	m_prevLocatorPs = GetLocatorPs();
	const float deltaTime = GetProcessDeltaTime();

	m_groundAdjustment = kZero;	// recompute every frame

	AnimControl* pAnimControl = GetAnimControl();
	const AnimStateLayer* pConstBaseLayer = pAnimControl->GetBaseStateLayer();
	const DC::AnimState* pCurrentState = pConstBaseLayer->CurrentState();
	const DC::AnimStateFlag currentAnimStateFlags = pCurrentState ? pCurrentState->m_flags : 0;
	const Locator currentAlignPs = GetLocatorPs();
	const Point currentPosPs = currentAlignPs.Pos();
	const Quat currentRotPs = currentAlignPs.Rot();

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
	NdAnimAlign::InstanceAlignTable alignTablePs;
	alignTablePs.Init(pConstBaseLayer->GetNumTotalInstances());

	// Calculate the desired position using the animation deltas and desired procedural movement
	const Locator computedDesiredLocPs = CalculateDesiredNewPositionPs(this,
																	   currentAlign,
																	   m_pNavStateMachine,
																	   &alignTablePs,
																	   &m_animDesiredAlignPs);

	NAVCHAR_ASSERT(IsReasonable(computedDesiredLocPs));
	NAVCHAR_ASSERT(Length(computedDesiredLocPs.GetTranslation()) < 100000.0f);

	m_desiredLocPs = computedDesiredLocPs;

	// Now we are ready to start adjusting and restricting the desired position
	const bool apMoveUpdate = (currentAnimStateFlags & (DC::kAnimStateFlagApMoveUpdate | DC::kAnimStateFlagAdjustApToRestrictAlign));
	const bool usingTap = m_pNavStateMachine->GetCurrentActionPackType() == ActionPack::kTraversalActionPack;
	const bool navigationInterrupted = m_pNavStateMachine->IsInterrupted();
	const bool shouldAdjustToPlayer = !usingTap
									  && !apMoveUpdate
									  && !WantsToPushPlayerAway()
									  && AllowAdjustToPlayer();

	//MsgCon("[%s] %d\n", GetName(), shouldAdjustToPlayer);

	bool moveClamped = false;
	Point adjustedPosPs(m_desiredLocPs.GetTranslation());
	const float navMeshAdjustFactor = 1.0f - NoAdjustToNavMeshFlagBlender(this).BlendForward(pConstBaseLayer, 0.0f);
	const bool allowNavSpaceAdjust = ((navMeshAdjustFactor > 0.0f) || m_ragdollStumble) && !MeleePreventsAdjustToNav();
	//MsgCon("[%s] %f\n", GetName(), navMeshAdjustFactor);

	/***********************************************************
		ADJUST TO PUSHER PLANE
	***********************************************************/
	if (allowNavSpaceAdjust)
	{
		const Scalar planeDist = m_pusherPlanePs.Dist(adjustedPosPs);

		// if we are behind the pusher plane,
		if (planeDist < Scalar(kZero))
		{
			// move adjusted point in XZ only (parent space) to be on the plane
			const Scalar distToPlane = -planeDist;
			const Vector normal = m_pusherPlanePs.GetNormal();
			Vector flattenedNorm = VectorXz(normal);
			const Scalar flattenedNormLen = Length(flattenedNorm);
			Vector adjustVec = kZero;

			if (flattenedNormLen > kSmallFloat)
			{
				adjustVec = navMeshAdjustFactor * flattenedNorm * distToPlane / flattenedNormLen;
				adjustedPosPs += adjustVec;
				moveClamped = true;
			}
		}
	}

	/***********************************************************
		ADJUST TO OTHER characters
	***********************************************************/
	if (allowNavSpaceAdjust && !apMoveUpdate && !usingTap && AllowAdjustToOtherCharacters())
	{
		Scalar npcRadius = GetCurrentNavAdjustRadius();
		const float kShrinkDelay = 0.25f;
		const float kShrinkRate = 0.5f;
		if (m_adjustToOtherNpcsTime > kShrinkDelay)
		{
			float shrinkAmount = (m_adjustToOtherNpcsTime - kShrinkDelay) * kShrinkRate;
			npcRadius = Max(0.0f, float(npcRadius) - shrinkAmount) * navMeshAdjustFactor;
		}
		bool headOnCollision;
		const Point rawNpcAdjustedPosPs = AdjustToOtherCharacters(currentPosPs, adjustedPosPs, npcRadius, &headOnCollision, false, shouldAdjustToPlayer);

		const F32 kDefaultPushSpeed = 3.0f;
		const float maxMoveDist = Max(kDefaultPushSpeed * GetProcessDeltaTime(), (float)DistXz(currentPosPs, adjustedPosPs));
		const Vector constrainedAdjustPs = LimitVectorLength(rawNpcAdjustedPosPs - adjustedPosPs, 0.0f, maxMoveDist);
		const Point npcAdjustedPosPs = adjustedPosPs + constrainedAdjustPs;

		if (!AllComponentsEqual(npcAdjustedPosPs, adjustedPosPs))
		{
			LogHdr(this, kNpcLogChannelMove);
			LogMsg(this, kNpcLogChannelMove, " - AdjustToOtherCharacters moved to ");
			LogPoint(this, kNpcLogChannelMove, npcAdjustedPosPs);
			LogMsg(this, kNpcLogChannelMove, "\n");
			moveClamped = true;
		}

		if (headOnCollision)
		{
			m_adjustToOtherNpcsDelayTime = 0.0f;
			m_adjustToOtherNpcsTime += deltaTime;
/*
			g_prim.Draw(DebugCircle(GetTranslation(),
									Vector(kUnitYAxis),
									npcRadius,
									kColorWhite,
									kPrimEnableHiddenLineAlpha));
*/
		}
		else
		{
			const F32 kShrinkDecayDelay = 0.5f;
			m_adjustToOtherNpcsDelayTime += deltaTime;
			if (m_adjustToOtherNpcsDelayTime > kShrinkDecayDelay)
			{
				const F32 kShrinkDecay = 0.5f;
				m_adjustToOtherNpcsTime *= Pow(kShrinkDecay, deltaTime);
			}
		}

		adjustedPosPs = npcAdjustedPosPs;
	}
	else
	{
		m_adjustToOtherNpcsTime = 0.0f;
		m_adjustToOtherNpcsDelayTime = 0.0f;
	}

	/***********************************************************
		ADJUST TO PLAYER
	***********************************************************/

	if (shouldAdjustToPlayer)
	{
		adjustedPosPs = AdjustToPlayer(adjustedPosPs);
	}

	/***********************************************************
	ADJUST TO WATER SURFACE (maybe)
	***********************************************************/
	if (IsSwimming() && m_pWaterDetectorAlign && !pNavControl->NavMeshForcesDive() && !IsPlayingWorldRelativeAnim() && !IsInScriptedAnimationState())
	{
		const Point adjustedPosWs = parentSpace.TransformPoint(adjustedPosPs);
		const Point waterQueryPosWs = adjustedPosWs - m_waterAdjustmentWs;

		if (m_pWaterDetectorAlign->Update(FILE_LINE_FUNC, waterQueryPosWs, 5.0f, kWaterQueryAll) && m_pWaterDetectorAlign->WaterFound())
		{
			const Point waterSurfaceWs = m_pWaterDetectorAlign->WaterSurface();
			const Point waterSurfacePs = parentSpace.UntransformPoint(waterSurfaceWs);

			//g_prim.Draw(DebugCross(waterSurfaceWs, 0.25f, kColorRed, kPrimEnableHiddenLineAlpha, "water surface", 0.5f));

			NAVCHAR_ASSERT(IsReasonable(waterSurfacePs));

			const Vector rawAdjustmentWs = waterSurfaceWs - waterQueryPosWs;

			m_waterAdjustmentWs.SetX(m_waterAdjustmentWs.X() + ((rawAdjustmentWs.X() - m_waterAdjustmentWs.X()) * Limit01(0.75f * deltaTime)));
			m_waterAdjustmentWs.SetY(m_waterAdjustmentWs.Y() + ((rawAdjustmentWs.Y() - m_waterAdjustmentWs.Y()) * Limit01(3.5f * deltaTime)));
			m_waterAdjustmentWs.SetZ(m_waterAdjustmentWs.Z() + ((rawAdjustmentWs.Z() - m_waterAdjustmentWs.Z()) * Limit01(0.75f * deltaTime)));

			//m_waterAdjustmentWs.SetY(Min(m_waterAdjustmentWs.Y(), rawAdjustmentWs.Y()));

			adjustedPosPs = parentSpace.UntransformPoint(waterQueryPosWs + m_waterAdjustmentWs);

			m_waterAdjustmentWs = Lerp(m_waterAdjustmentWs, kZero, Limit01(0.5f * deltaTime));

			//g_prim.Draw(DebugArrow(adjustedPosPs, m_waterAdjustmentWs, kColorCyan, 0.5f, kPrimEnableHiddenLineAlpha));
		}
		else
		{
			//g_prim.Draw(DebugCross(adjustedPosWs, 0.35f, kColorRed, kPrimEnableHiddenLineAlpha, "WaterDetector Failed"));

			m_waterAdjustmentWs = kZero;
		}
	}
	else if (m_pWaterDetectorAlign && pNavControl->NavMeshForcesWade() && IsBuddyNpc())
	{
		const Point adjustedPosWs = parentSpace.TransformPoint(adjustedPosPs);

		m_pWaterDetectorAlign->Update(FILE_LINE_FUNC, adjustedPosWs, 5.0f, kWaterQueryAll);
	}
	else
	{
		if (m_pWaterDetectorAlign)
		{
			m_pWaterDetectorAlign->Reset();
		}

		m_waterAdjustmentWs = kZero;
	}

	/***********************************************************
		ADJUST TO NAV SPACE
	***********************************************************/
	if (allowNavSpaceAdjust && AllowNavMeshDepenetration())
	{
		adjustedPosPs = AdjustRootToNavSpace(currentPosPs,
											 adjustedPosPs,
											 navMeshAdjustFactor,
											 deltaTime,
											 shouldAdjustToPlayer);

		if (DistXz(adjustedPosPs, m_desiredLocPs.Pos()) > 0.0001f)
		{
			OnConstrainedByNavmesh(currentPosPs, m_desiredLocPs.Pos(), adjustedPosPs);
			moveClamped = true;
		}
	}
	else
	{
		m_adjustJointsToNavSmoothing = 0.0f;
	}

	// Remember whether or not the movement was constrained to navmap
	m_wasMovementConstrained = moveClamped;

	/***********************************************************
		ADJUST TO GROUND
	***********************************************************/

	if (PreventAdjustToGround())
	{
		ResetGroundFilter();
	}
	else
	{
		GroundHugController* pGroundHug = GetGroundHugController();

		if (pGroundHug && !g_groundHugOptions.m_disable)
		{
			pGroundHug->UpdateWheels();
			Locator preLoc = m_desiredLocPs;
			preLoc.SetPosition(adjustedPosPs);
			preLoc = GetParentSpace().TransformLocator(preLoc);
			Locator postLoc = Locator(preLoc);
			pGroundHug->GroundHug(preLoc, postLoc);
			Locator postLocPs = GetParentSpace().UntransformLocator(postLoc);
			adjustedPosPs = PointFromXzAndY(postLocPs.GetTranslation(), adjustedPosPs.Y());
			m_groundPositionPs = m_filteredGroundPosPs = postLocPs.Pos();

			if (ShouldUseGroundHugRotation())
			{
				m_desiredLocPs.SetRotation(postLocPs.GetRotation());
			}
		}
		else if (ShouldUseCrawlGroundProbes())
		{
			const Vector filteredCrawlGroundNormal = GetParentSpace().TransformVector(GetFilteredGroundNormalPs());
			const Quat crawlRotAdj = QuatFromVectors(kUnitYAxis, filteredCrawlGroundNormal);
			const Vector currFw = GetLocalZ(m_desiredLocPs);
			const Vector crawlRotFw = Rotate(crawlRotAdj, currFw);
			const Quat crawlRot = QuatFromLookAt(crawlRotFw, filteredCrawlGroundNormal);
			m_desiredLocPs.SetRotation(crawlRot);
		}

		{
			const bool topStateAdjustsToGround = (currentAnimStateFlags & DC::kAnimStateFlagNoAdjustToGround) == 0;
			if (topStateAdjustsToGround || m_ragdollStumble)
			{
				if (m_currentPat.m_bits || IsUsingTraversalActionPack())
				{
					m_fallSpeed = 0.0f;
				}
				else
				{
					// falling
					m_fallSpeed += 10.0f * GetProcessDeltaTime();
				}
			}

			// scale the adjustment by the contribution of animations that want to be adjusted the ground
			const float noAdjToGroundFactor = NoAdjustToGroundFlagBlender(this).BlendForward(pConstBaseLayer, 0.0f);
			const float adjustmentBlend = 1.0f - noAdjToGroundFactor;

			if (adjustmentBlend <= 0.0f)
			{
				ResetGroundFilter();
				m_filteredGroundPosPs = currentPosPs;
			}

			const Point groundedPos = (adjustmentBlend > 0.0f) ? AdjustToGround(currentPosPs, adjustedPosPs) : adjustedPosPs;
			NAVCHAR_ASSERT(IsReasonable(groundedPos));

			const Vector groundAdjustVec = adjustmentBlend * (groundedPos - adjustedPosPs);
			NAVCHAR_ASSERT(IsReasonable(groundAdjustVec));
			m_groundAdjustment = groundAdjustVec;

			if (false) // if (Length(m_groundAdjustment) > 0.005f)
			{
				if (m_requestSnapToGround)
				{
					LogHdr(this, kNpcLogChannelMove);
					LogMsg(this, kNpcLogChannelMove, "AdjustToGround SNAPPED to ");
					LogPoint(this, kNpcLogChannelMove, groundedPos);
					LogMsg(this, kNpcLogChannelMove, "\n");
				}
				else
				{
					LogHdr(this, kNpcLogChannelMoveDetails);
					LogMsg(this,
						   kNpcLogChannelMoveDetails,
						   "AdjustToGround (%0.2f) moved %5.3fm to ",
						   adjustmentBlend,
						   (float)Length(m_groundAdjustment));
					LogPoint(this, kNpcLogChannelMoveDetails, groundedPos);
					LogMsg(this, kNpcLogChannelMoveDetails, "\n");
				}
			}

			m_requestSnapToGround = false;

			NAVCHAR_ASSERT(IsReasonable(m_groundAdjustment));

			adjustedPosPs += m_groundAdjustment;
			NAVCHAR_ASSERT(IsReasonable(adjustedPosPs));
			NAVCHAR_ASSERT(Length(adjustedPosPs) < 100000.0f);
		}
	}

	/************************************************************************/
	/* Handle NPCs Falling out of the world                                 */
	/************************************************************************/
	const bool failTaskOnDeath = ShouldFailTaskOnDeath();
	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_debugNpcFalling))
	{
		adjustedPosPs = m_prevLocatorPs.Pos();
		g_prim.Draw(DebugString(GetTranslation(), "FALLING", kColorRed));
		if (DebugSelection::Get().IsProcessSelected(this) && ! g_ndConfig.m_pDMenuMgr->IsProgPaused())
		{
			g_ndConfig.m_pDMenuMgr->SetProgPause(true);
		}
	}
	else if (m_secondsFalling > 1.5f)
	{
		if (failTaskOnDeath)
		{
			const bool handled = EmergencyTeleportFallRecovery(m_secondsFalling, m_desiredLocPs);
			if (handled)
				adjustedPosPs = m_desiredLocPs.GetPosition();
		}
		else if (m_secondsFalling > 3.0f && !IsDead())
		{
			DisallowRagdoll();
			Die();
		}
		else if (m_secondsFalling > 30.0f)
		{
			// this way scripting gets the relevant events etc
			OnDeath();
			KillProcess(this);
			return;
		}
	}

	const NavMeshHandle hMesh = pNavControl->GetNavMeshHandle();

	if (!IsNavigationInterrupted()
		&& !IsClimbing()
		&& !IsBusy()
		&& m_currentPat.m_bits
		&& hMesh.IsNull()
		&& pNavControl->EverHadAValidNavMesh()
		&& !IsInScriptedAnimationState()
		&& CaresAboutHavingNoMesh()
		&& !IsDead())
	{
		m_secondsNoNavMesh += GetProcessDeltaTime();

		if (m_secondsNoNavMesh >= 15.0f)
		{
			if (failTaskOnDeath)
			{
				const bool handled = EmergencyTeleportFallRecovery(m_secondsFalling, m_desiredLocPs);
				if (handled)
					adjustedPosPs = m_desiredLocPs.GetPosition();
			}
			else
			{
				// this way scripting gets the relevant events etc
				OnDeath();
				KillProcess(this);
				return;
			}
		}
	}
	else
	{
		m_secondsNoNavMesh = 0.0f;
	}

	/***********************************************************
		ADJUSTMENT IS COMPLETE - NOW WE ASSIGN
	***********************************************************/
	NAVCHAR_ASSERT(IsReasonable(adjustedPosPs));
	NAVCHAR_ASSERT(Length(adjustedPosPs) < 100000.0f);

	MsgAnimVerbose("NavChar(%08X): Initial  Pos [%5.3f ; %5.3f ; %5.3f] \n", (uintptr_t)this, (float)currentPosPs.X(), (float)currentPosPs.Y(), (float)currentPosPs.Z());
	MsgAnimVerbose("NavChar(%08X): Adjusted Pos [%5.3f ; %5.3f ; %5.3f] \n", (uintptr_t)this, (float)adjustedPosPs.X(), (float)adjustedPosPs.Y(), (float)adjustedPosPs.Z());
	MsgAnimVerbose("NavChar(%08X): Delta    Pos [%5.3f ; %5.3f ; %5.3f] \n", (uintptr_t)this, (float)(adjustedPosPs.X() - currentPosPs.X()), (float)(adjustedPosPs.Y() - currentPosPs.Y()), (float)(adjustedPosPs.Z() - currentPosPs.Z()));

	const Point adjustmentLs = currentAlignPs.UntransformPoint(adjustedPosPs);
	MsgAnimVerbose("NavChar(%08X): Delta(2) Pos [%5.3f ; %5.3f ; %5.3f] \n", (uintptr_t)this, (float)adjustmentLs.X(), (float)adjustmentLs.Y(), (float)adjustmentLs.Z());

	const Vector adjustmentVectorPs = adjustedPosPs - m_desiredLocPs.GetTranslation();
	NavUtil::PropagateAlignRestriction(this, adjustedPosPs, adjustmentVectorPs, alignTablePs);

	// Update the align
	Locator nextLocPs = Locator(adjustedPosPs, m_desiredLocPs.Rot());
	SetLocatorPs(nextLocPs);

	if (!ShouldUseGroundHugRotation() && !ShouldUseCrawlGroundProbes())
		AdjustToUpright(pAnimControl);

	SetRotationPs(Normalize(GetRotationPs()));

	AI_ASSERT(IsReasonable(GetLocatorPs()));
}

bool NavCharacter::EmergencyTeleportFallRecovery(float timeFalling, Locator& outDesiredLocPs)
{
	if (timeFalling < 3.0f)
		return false;

	BoundFrame destBf = kIdentity;
	if (const NdGameObject* pPlayer = EngineComponents::GetNdGameInfo()->GetPlayerGameObject())
	{
		destBf = pPlayer->GetBoundFrame();
	}
	else
	{
		const RenderCamera& cam = GetRenderCamera(0);
		const Point spawnPosWs	= cam.GetPosition() + (cam.GetDir() * 2.0f);
		destBf.SetTranslationWs(spawnPosWs);
	}

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	FindBestNavMeshParams findParams;
	findParams.m_pointWs	= destBf.GetTranslation();
	findParams.m_cullDist	= 10.0f;
	findParams.m_yThreshold = 10.0f;
	findParams.m_bindSpawnerNameId	  = Nav::kMatchAllBindSpawnerNameId;
	findParams.m_obeyedStaticBlockers = GetObeyedStaticBlockers();

	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		nmMgr.FindNavMeshWs(&findParams);
	}

	if (findParams.m_pNavPoly)
	{
		destBf.SetTranslationWs(findParams.m_nearestPointWs);
	}

	destBf.AdjustTranslationWs(Vector(0.0f, 0.25f, 0.0f));

	if (FALSE_IN_FINAL_BUILD(true))
	{
		g_prim.Draw(DebugSphere(destBf.GetTranslationWs(), 1.0f), Seconds(10.0f));

		const Point myPosWs = GetTranslation();
		const float x		= myPosWs.X();
		const float z		= myPosWs.Z();

		g_prim.Draw(DebugString2D(Vec2(0.1f, 0.2f),
								  kDebug2DNormalizedCoords,
								  StringBuilder<256>("'%s' Fell to their death at [x: %0.1f z: %0.1f]! Fix that collision!",
													 GetName(),
													 x,
													 z)
									  .c_str(),
								  kColorRed,
								  1.0f),
					Seconds(10.0f));
		g_prim.Draw(DebugLine(myPosWs,
							  myPosWs + Vector(0.0f, 10000.0f, 0.0f),
							  kColorRed,
							  10.0f,
							  kPrimEnableHiddenLineAlpha),
					Seconds(10.0f));
	}

	TeleportToBoundFrame(destBf);
	outDesiredLocPs = destBf.GetLocatorPs();

	return true;
}

bool NavCharacter::IsCommandPending() const
{
	return GetNavStateMachine()->IsCommandPending();
}

bool NavCharacter::IsCommandInProgress() const
{
	return GetNavStateMachine()->IsCommandInProgress();
}

bool NavCharacter::IsExitingActionPack() const
{
	return GetNavStateMachine()->IsExitingActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::PostRootLocatorUpdate()
{
	/***********************************************************
	PERFORM POST ALIGN UPDATE OPERATIONS
	***********************************************************/
	// Update the animation states that allow the ap to be adjusted based on a restricted align
	Point finalPosPs = GetLocatorPs().GetPosition();
	const Vector adjustmentVectorPs = finalPosPs - m_desiredLocPs.GetPosition();

	const Locator finalAlignWs = GetLocator();
	const Locator animDesiredLocWs = GetParentSpace().TransformLocator(m_animDesiredAlignPs);

	if (NdSubsystemMgr* pSubsysMgr = GetSubsystemMgr())
	{
		const bool forceUpdate = FALSE_IN_FINAL_BUILD(g_navCharOptions.m_disableMovement);
		pSubsysMgr->SendEvent(SID("PostAlignMovement"), &finalAlignWs, &animDesiredLocWs, forceUpdate);
	}

	Locator alignDelta = animDesiredLocWs.UntransformLocator(finalAlignWs);
	PostAlignMovement(alignDelta);

	/*
	MsgOut("[%d] total move dist: %f\n", GetClock()->GetCurTime().GetRaw(), float(Dist(GetLastTranslationPs(), GetTranslationPs())));
	*/

	// Update speed and velocity.
	float deltaTime = GetProcessDeltaTime();
	if (deltaTime > 0.0f)
	{
		const Point p1 = GetLocatorPs().Pos();
		const Point p0 = GetPrevLocatorPs().Pos();
		const Vector instantVelocityPs = (p1 - p0) / deltaTime;
		if (m_velocityPsHistory.IsFull())
		{
			m_velocityPsHistory.Dequeue();
		}

#if 0
		const float instSpeed = Length(instantVelocityPs);

		if (instSpeed > 10.0f && DebugSelection::Get().IsProcessSelected(this))
		{
			g_prim.Draw(DebugCoordAxesLabeled(GetLocatorPs(), "new", 0.3f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhite, 0.5f));
			g_prim.Draw(DebugCoordAxesLabeled(GetPrevLocatorPs(), "prev", 0.3f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhite, 0.5f));

			MsgOut("%s -> %s = %0.4fm / %0.4fs = %0.4fm/s\n",
				   PrettyPrint(p0),
				   PrettyPrint(p1),
				   float(Dist(p0, p1)),
				   deltaTime,
				   instSpeed);
		}
#endif

		m_velocityPsHistory.Enqueue(instantVelocityPs);

		Vector smoothedVelocityPs = kZero;

		if (m_velocityPsHistory.GetCount() > 0)
		{
			for (I32F i = 0; i < m_velocityPsHistory.GetCount(); ++i)
			{
				smoothedVelocityPs += m_velocityPsHistory.GetAtRawIndex(m_velocityPsHistory.GetOldestRawIndex() + i);
			}
			smoothedVelocityPs = smoothedVelocityPs / ((float)m_velocityPsHistory.GetCount());
		}

		m_smoothedVelocityPs = smoothedVelocityPs;
	}

#if !FINAL_BUILD
	if (g_navCharOptions.m_pVelocityProfile && g_navCharOptions.m_pVelocityProfileSamples
		&& DebugSelection::Get().IsProcessSelected(this) && g_navCharOptions.m_pVelocityProfile->IsEnabled())
	{
		const bool isOwner = g_navCharOptions.m_velocityProfileOwner == GetUserId();

		if (!isOwner)
		{
			g_navCharOptions.m_pVelocityProfileSamples->Reset();
			g_navCharOptions.m_velocityProfileOwner = GetUserId();
		}

		if (g_navCharOptions.m_pVelocityProfileSamples->IsFull())
			g_navCharOptions.m_pVelocityProfileSamples->Dequeue();

		g_navCharOptions.m_pVelocityProfileSamples->Enqueue(Length(GetVelocityPs()));
	}
#endif

	ParentClass::PostRootLocatorUpdate();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsPointClearForMePs(Point_arg posPs, bool staticCheckOnly) const
{
	const NavControl* pNavCon = GetNavControl();
	if (!pNavCon)
		return false;

	if (staticCheckOnly)
	{
		return !pNavCon->IsPointStaticallyBlockedPs(posPs);
	}
	else
	{
		return !pNavCon->IsPointDynamicallyBlockedPs(posPs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::OnSuccessfulEvade()
{
	NdAnimationControllers* pNdAnimControllers = GetNdAnimationControllers();

	INdAiTraversalController* pTapController = pNdAnimControllers ? pNdAnimControllers->GetTraversalController()
																  : nullptr;

	if (pTapController && pTapController->IsBusy())
	{
		pTapController->Reset();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::AdjustMoveToNavSpacePs(Point_arg basePosPs,
										  Point_arg desiredPosPs,
										  float adjustRadius,
										  const NavBlockerBits& obeyedBlockers,
										  const Sphere* offsetsPs,
										  U32F offsetCount,
										  Point* pResultPosPsOut) const
{
	const bool debugDraw = FALSE_IN_FINAL_BUILD(DebugSelection::Get().IsProcessOrNoneSelected(this)
												&& g_navCharOptions.m_displayNavMeshAdjust);

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const NavControl* pNavControl = GetNavControl();
	if (!pNavControl)
	{
		*pResultPosPsOut = desiredPosPs;
		return false;
	}

	// NB: this code used to allow us to slowly depenetrate over time, but it broke
	// and what was moveDistCap was really a *moveDistFloor*. Now as are trying to ship T2 we
	// have way too many systems that have been built relying on that instantaneous depenetration
	// so.... c'est la vie.
	const float moveDistFloor = IsSwimming() ? (6.5f * GetProcessDeltaTime()) : GetProcessDeltaTime();
	const float desMoveDist = DistXz(basePosPs, desiredPosPs);
	const float maxMoveDist = Max(moveDistFloor, 2.0f * desMoveDist);

	Point desiredBasePosPs = desiredPosPs;
	bool wasAdjusted = false;

	for (U32F i = 0; i < offsetCount; ++i)
	{
		const Vector offsetPs = Vector(offsetsPs[i].GetCenter() - kOrigin);
		const float offsetRadius = offsetsPs[i].GetRadius();
		const Point offsetPosPs = desiredBasePosPs + offsetPs;

		Point desiredOffsetPos;

		//only the align should use the capsule shape. Others should be spherical
		Segment probeSegment = GetDepenetrationSegmentPs(offsetPosPs, false);

		if (pNavControl->AdjustMoveVectorPs(offsetPosPs,
											offsetPosPs,
											probeSegment,
											offsetRadius,
											maxMoveDist,
											obeyedBlockers,
											&desiredOffsetPos,
											debugDraw))
		{
			wasAdjusted = true;
		}

		desiredBasePosPs = desiredOffsetPos - offsetPs;
	}

	Segment probeSegment = GetDepenetrationSegmentPs(basePosPs, true);

	if (pNavControl->AdjustMoveVectorPs(basePosPs,
										desiredBasePosPs,
										probeSegment,
										adjustRadius,
										maxMoveDist,
										obeyedBlockers,
										pResultPosPsOut,
										debugDraw))
	{
		wasAdjusted = true;
	}

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		const Point prevPosWs = GetTranslation();
		const Color clr =  wasAdjusted ? kColorRed : AI::IndexToColor(GetUserId().GetValue());
		g_prim.Draw(DebugArrow(basePosPs, desiredPosPs, clr, 0.1f, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugLine(prevPosWs, basePosPs, clr, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugCross(prevPosWs, 0.1f, clr, kPrimEnableHiddenLineAlpha, GetName(), 0.5f));
		g_prim.Draw(DebugSphere(*pResultPosPsOut, 0.1f, clr, kPrimEnableWireframe));
	}

	return wasAdjusted;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavCharacter::GetCurrentNavAdjustRadius() const
{
	return m_pNavControl ? m_pNavControl->GetCurrentNavAdjustRadius() : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavCharacter::GetDesiredNavAdjustRadius() const
{
	return m_pNavControl ? m_pNavControl->GetDesiredNavAdjustRadius() : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavCharacter::GetMaximumNavAdjustRadius() const
{
	return m_pNavControl ? m_pNavControl->GetMaximumNavAdjustRadius() : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavCharacter::GetMovingNavAdjustRadius() const
{
	return m_pNavControl ? m_pNavControl->GetMovingNavAdjustRadius() : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavCharacter::GetIdleNavAdjustRadius() const
{
	return m_pNavControl ? m_pNavControl->GetIdleNavAdjustRadius() : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 NavCharacter::GetInterCharacterDepenRadius() const
{
	const float curRadius = m_pNavControl ? m_pNavControl->GetEffectivePathFindRadius() : 0.0f;
	float radius = curRadius;

	if (m_interCharDepenRadius >= 0.0f)
	{
		radius = Min(m_interCharDepenRadius, curRadius);
	}

	return radius;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::CanRagdoll() const
{
	return g_navCharOptions.m_ragdollOnDeath && ParentClass::CanRagdoll();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::KillIfCantRagdoll() const
{
	return m_killIfCantRagdoll;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetKillIfCantRagdoll(bool okToKill)
{
	m_killIfCantRagdoll = okToKill;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::ForceRagdollOnExplosionDeath() const
{
	return m_forceRagdollOnExplosionDeath;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetForceRagdollOnExplosionDeath(bool force)
{
	m_forceRagdollOnExplosionDeath = force;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavCharacter::GetRagdollDelay() const
{
	return m_delayRagdollTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetRagdollDelay(F32 time)
{
	m_delayRagdollTime = time;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetWetPoints(WetPoint* pWetPoints)
{
	m_pWetPoints = pWetPoints;
	I32F numWetPoints = 0;
	for (U32F i = 0; pWetPoints[i].m_jointA != INVALID_STRING_ID_64; ++i)
	{
		++numWetPoints;
	}

	m_pWaterDetectors = NDI_NEW WaterDetector[numWetPoints*2];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::DealDamage(F32 damage, bool isExplosion, bool ignoreInvincibility, bool isBullet)
{
	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_invulnerableNpc && !g_ndConfig.m_pNetInfo->CheatingDisabled())) // this is a debug dev menu thing only
		return;

	GetHealthSystem()->DealDamage(damage, false, isExplosion, ignoreInvincibility, isBullet);

	Log(this, kNpcLogChannelGeneral, "Dealt %d damage, new health: %d\n", (int)damage, (int)GetHealthSystem()->GetHealth());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::OnDeath()
{
	m_pNavStateMachine->Shutdown();

	// allow death animations to take us over edges and into walls (rely on ragdoll collision to not have us interpenetrate)
	if (GetAllowAdjustHeadToNav())
	{
		SetAllowAdjustHeadToNav(false);
	}

	AnimControl* pAnimControl = GetAnimControl();
	AnimSimpleLayer* pHandsLayer = pAnimControl ? pAnimControl->GetSimpleLayerById(SID("hands")) : nullptr;
	AnimSimpleInstance* pHandsInst = pHandsLayer ? pHandsLayer->CurrentInstance() : nullptr;

	if (pHandsInst)
	{
		pHandsInst->SetFrozen(true);
	}

	const DC::Look2* pLook = GetLook(GetResolvedLookId()).AsLook2();
	for (U32F iSwap = 0; iSwap < pLook->m_onDeathMaterialSwaps.GetSize(); iSwap++)
	{
		const DC::Look2MaterialSwap* const pSwap = pLook->m_onDeathMaterialSwaps.At(iSwap);
		ChangeMaterial(pSwap->m_from, pSwap->m_to, INVALID_STRING_ID_64, true);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::DebugDraw(WindowContext* pDebugWindowContext, ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	PROFILE(AI, NavChar_DebugDraw);

	ParentClass::DebugDraw(pDebugWindowContext, pPrinter);

	ScreenSpaceTextPrinter& printer = *pPrinter;
	const Locator& parentSpace = GetParentSpace();
	const Point curPosPs = GetTranslationPs();
	const Point curPosWs = GetTranslation();
	const NavControl* pNavControl = GetNavControl();
	const AnimControl* pAnimControl = GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	if (!pNavControl)
		return;

	if (g_animOptions.m_testNpcAnklePoseMatching)
	{
		if (const AnimStateInstance* pInst = pBaseLayer->CurrentStateInstance())
		{
			//const ArtItemAnim* pAnim = pAnimControl->LookupAnim(SID("ellie-fist-ambi-walk"));
			const ArtItemAnim* pAnim = pInst->GetPhaseAnimArtItem().ToArtItem();
			if (pAnim)
			{
				PoseMatchInfo mi;
				mi.m_startPhase = 0.0f; // 0.8f; //0.5f + 1e-5;
				mi.m_endPhase = 1.0f; // 0.3f; //0.5f - 1e-5;
				mi.m_rateRelativeToEntry0 = true;
				mi.m_strideTransform = kIdentity;
				mi.m_inputGroundNormalOs = GetLocatorPs().UntransformVector(GetGroundNormalPs());

				mi.m_entries[0].m_channelId = SID("lAnkle");
				mi.m_entries[0].m_matchPosOs = m_ankleInfoLs.m_anklePos[0];
				mi.m_entries[0].m_matchVel = m_ankleInfoLs.m_ankleVel[0];
				mi.m_entries[0].m_velocityValid = true;
				mi.m_entries[0].m_valid = true;

				mi.m_entries[1].m_channelId = SID("rAnkle");
				mi.m_entries[1].m_matchPosOs = m_ankleInfoLs.m_anklePos[1];
				mi.m_entries[1].m_matchVel = m_ankleInfoLs.m_ankleVel[1];
				mi.m_entries[1].m_velocityValid = true;
				mi.m_entries[1].m_valid = true;

				mi.m_debug = true;
				mi.m_debugDrawLoc = GetLocator();
				mi.m_debugDrawTime = kPrimDuration1FrameAuto;

				const float matchPhase = CalculateBestPoseMatchPhase(pAnim, mi).m_phase;
				const float actualPhase = pInst->Phase();
				const float phaseErr = Min(Abs(matchPhase - actualPhase), Abs((1.0f - actualPhase) - matchPhase));
				g_prim.Draw(DebugString(GetTranslation() + kUnitYAxis, StringBuilder<128>("%0.3f (%0.3f)", matchPhase, 100.0f*phaseErr).c_str()));
			}

			if (pInst->GetStateFlags() & DC::kAnimStateFlagFirstAlignRefMoveUpdate)
			{
				const StringId64 channels[] ={pInst->GetApRefChannelId()};

				ndanim::JointParams outChannelJoints[1];
				const U32F evaluatedChannelsMask = pInst->EvaluateChannels(channels, 1, outChannelJoints);
				if (evaluatedChannelsMask & (1 << 0))
				{
					DebugDrawPrintf(curPosWs, kColorRed, "Anim '%s' has an unexpected apReference\n", DevKitOnly_StringIdToStringOrHex(pInst->GetPhaseAnim()));
				}
			}
		}
	}

	bool isSelected = DebugSelection::Get().IsProcessSelected(this);
	if (g_navCharOptions.m_dropMoveToBreadCrumbs && isSelected)
	{
		Color clr = AI::IndexToColor(GetUserId().GetValue());
		g_prim.Draw(DebugCross(curPosWs + VECTOR_LC(0.0f, 2.0f, 0.0f), 0.5f, 3.0f, clr, PrimAttrib(0))); // so you can see which guys' bread crumbs are which
	}

#ifndef FINAL_BUILD
	if (m_pScriptLogger && (isSelected || DebugSelection::Get().GetCount() == 0))
	{
		if (g_navCharOptions.m_npcScriptLogOverhead)
		{
			m_pScriptLogger->DebugDraw(pPrinter, GetClock());
		}
	}
#endif

	if (!isSelected && DebugSelection::Get().GetCount() != 0)
	{
		return;
	}

	if (isSelected && m_pChannelLog && g_navCharOptions.m_npcLogToMsgCon)
	{
		DoutBase* pMsgCon = GetMsgOutput(kMsgConNonpauseable);

		U32 channelMask = 0;
		for (U32F chan = 0; chan < kNpcLogChannelCount; ++chan)
		{
			if (ShouldPrint(static_cast<NpcLogChannel>(chan)))
				channelMask |= (1U << chan);
		}

		pMsgCon->Printf("NAV CHAR LOG ---- %s ------------ pid-%d\n", GetName(), U32(GetProcessId()));
		m_pChannelLog->DumpChannelsToTty(pMsgCon,
										 channelMask,
										 AI::GetNpcLogChannelName,
										 true,
										 g_navCharOptions.m_npcLogMsgConLines);
	}
	else if (isSelected && m_pScriptLogger && g_navCharOptions.m_npcScriptLogToMsgCon)
	{
		DoutBase* pMsgCon = GetMsgOutput(kMsgConNonpauseable);
		pMsgCon->Printf("SCRIPT LOG ------ %s ------------ pid-%d\n", GetName(), U32(GetProcessId()));
		m_pScriptLogger->Dump(pMsgCon);
	}

	if (g_navCharOptions.m_displayAlignBlending)
	{
		// Draw 'align' and 'apReference' locators.
		BoundFrame baseAlign = GetBoundFrame();
		baseAlign.SetLocatorPs(GetPrevLocatorPs());

		const Locator blendedAlignPs = CalculateDesiredNewPositionPs(this,
																	 baseAlign,
																	 m_pNavStateMachine,
																	 nullptr,
																	 nullptr,
																	 true);

		if (pAnimControl && isSelected)
		{
			const DC::AnimTopInfo* pTopInfo = pAnimControl->TopInfo<DC::AnimTopInfo>();
			const DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>();

			MsgCon("Align Delta(cm):   %6.2f\n", (float)(Dist(blendedAlignPs.Pos(), curPosPs) * 100.0f));
			MsgCon("Face Diff(deg):    %6.2f\n", pTopInfo->m_facingDiff);
			MsgCon("Move Angle(deg):   %6.2f\n", pTopInfo->m_moveAngle);
			MsgCon("Move Angle Change: %6.2f\n", pTopInfo->m_moveAngleChange);
			MsgCon("Lean Factor:       %6.2f\n", pInfo->m_leanFactor);
		}
	}

	if (IsDead())
	{
		return;
	}

	PrimServerWrapper psPrim(parentSpace);
	psPrim.EnableWireFrame();

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();

	if (false)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		const NavMesh* pNavMesh = pNavControl->GetNavLocation().ToNavMesh();

		if (pNavMesh && !IsAnimationControlled() && !GetEnteredActionPack())
		{
			const NavPoly* pCurPoly = pNavMesh->FindContainingPolyPs(curPosPs);
			const NavPolyEx* pCurPolyEx = pCurPoly ? pCurPoly->FindContainingPolyExPs(curPosPs) : nullptr;

			const NavBlockerBits obeyedBlockers = pNavControl->BuildObeyedBlockerList(true);
			const Nav::StaticBlockageMask obeyedStaticBlockers = pNavControl->GetObeyedStaticBlockers();

			const bool isBlocked = (!pCurPoly) || pCurPoly->IsBlocked(obeyedStaticBlockers)
								   || (pCurPolyEx && pCurPolyEx->IsBlockedBy(obeyedBlockers));
			if (isBlocked)
			{
				psPrim.DrawCross(curPosPs, 0.4f, kColorRed);
				psPrim.DrawString(curPosPs, "nav blocked", kColorRed);
			}
		}
	}

	if (g_navCharOptions.m_displayNavMesh)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		if (const NavMesh* pNavMesh = pNavControl->GetNavLocation().ToNavMesh())
		{
			NavMeshDrawFilter filter;
			memset(&filter, 0, sizeof(filter));
			filter.m_drawPolys = true;

			NavMeshDebugDraw(pNavMesh, filter);

			Point navMeshOrigin = pNavMesh->LocalToWorld(Point(0, 0, 0));
			g_prim.Draw(DebugLine(GetTranslation(), navMeshOrigin, kColorBlack, kColorGreen));

			if (const NavPoly* pPoly = pNavControl->GetNavPoly())
			{
				pPoly->DebugDrawEdges(kColorWhite, kColorYellow, 0.15f);

				const NavBlockerBits obeyedBlockers = pNavControl->BuildObeyedBlockerList(true);
				pPoly->DebugDrawExEdges(kColorWhiteTrans, kColorWhiteTrans, kColorRedTrans, 0.2f, 3.0f, kPrimDuration1FrameAuto, &obeyedBlockers);
			}
		}
	}

	if (g_navCharOptions.m_displayWetness)
	{
		DebugDrawWetness();
	}

	const PathWaypointsEx* pCurrentPath = m_pNavStateMachine->GetCurrentPathPs();

	if (g_navCharOptions.m_displayPath && pCurrentPath)
	{
		PathWaypointsEx::ColorScheme colors;

		// Display the path
		if (m_pNavStateMachine->PathStatusSuspended())
		{
			colors.m_groundLeg0 = colors.m_apLeg0 = colors.m_ledgeJump0 = colors.m_ledgeShimmy0 = kColorGray;
			colors.m_groundLeg1 = colors.m_apLeg1 = colors.m_ledgeJump1 = colors.m_ledgeShimmy1 = kColorWhite;
		}

		pCurrentPath->DebugDraw(parentSpace, true, 0.0f, colors, kPrimDuration1FramePauseable);
	}

	if (g_navCharOptions.m_debugObeyedPathBlockers)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		BoxedValue ugh = const_cast<NavCharacter*>(this);
		Nav::BuildObeyedBlockerList(pNavControl, NavStateMachine::NsmPathFindBlockerFilter, true, ugh, true);
	}

	if (g_navCharOptions.m_debugObeyedDepenBlockers)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		BoxedValue ugh = const_cast<NavCharacter*>(this);
		Nav::BuildObeyedBlockerList(pNavControl, NavStateMachine::NsmPathFindBlockerFilter, false, ugh, true);
	}

	if (g_navCharOptions.m_displayFacePosition)
	{
		const Point facePosWs = parentSpace.TransformPoint(GetFacePositionPs());
		const Vector faceDirWs = SafeNormalize(facePosWs - curPosWs, kUnitZAxis);

		g_prim.Draw(DebugString(facePosWs, "face-pos", kColorMagenta));
		g_prim.Draw(DebugCross(facePosWs, 0.5f, kColorMagenta));
		g_prim.Draw(DebugArrow(curPosWs + Vector(0.0f, 0.1f, 0.0f), faceDirWs, kColorMagenta));
		g_prim.Draw(DebugStringFmt(curPosWs + Vector(0.0f, 0.1f, 0.0f) + faceDirWs, "face-dir %s", PrettyPrint(faceDirWs)));
	}

	if (g_navCharOptions.m_displayLookAtPosition || g_navCharOptions.m_displayAimAtPosition)
	{
		const Locator eyeLocWs = GetEyeWs();
		const Point aimFromWs = GetAimOriginWs();
		const Point eyePosWs = eyeLocWs.GetTranslation();

		const Point desiredAimPosPs = GetAimAtPositionPs();
		const Point smoothedLookAtPosPs = GetLookAtPositionPs();

		// Draw the LookAt, Aim and Face positions.
		const Point desiredAimPositionWs = parentSpace.TransformPoint(desiredAimPosPs);
		const Point smoothedLookAtPosWs = parentSpace.TransformPoint(smoothedLookAtPosPs);
		const Vector desiredAimDirWs = Normalize(desiredAimPositionWs - aimFromWs);
		const Vector smoothedLookAtDirWs = Normalize(smoothedLookAtPosWs - eyePosWs);

		if (g_navCharOptions.m_displayAimAtPosition)
		{
			const char* aimStr = GetGestureController() && GetGestureController()->IsAiming() ? "aim-at (aiming)"
																							  : "aim-at (not aiming)";
			g_prim.Draw(DebugLine(aimFromWs, aimFromWs + desiredAimDirWs * 2.0f, kColorCyan));
			g_prim.Draw(DebugCross(desiredAimPositionWs, 0.6f, kColorCyan));
			g_prim.Draw(DebugString(aimFromWs + desiredAimDirWs * 2.0f, aimStr, kColorCyan));
		}

		if (g_navCharOptions.m_displayLookAtPosition)
		{
			const char* lookStr = GetGestureController() && GetGestureController()->IsLooking()
									  ? "look-at (looking)"
									  : "look-at (not looking)";
			g_prim.Draw(DebugLine(eyePosWs, eyePosWs + smoothedLookAtDirWs * 2.2f, kColorRed));
			g_prim.Draw(DebugString(eyePosWs + smoothedLookAtDirWs * 2.2f, lookStr, kColorRed));
			g_prim.Draw(DebugCross(parentSpace.TransformPoint(smoothedLookAtPosPs), 0.5f, kColorBlue));
		}

		if (g_navCharOptions.m_displayDetailedLookAtPosition)
		{
			const Point defPosWs = GetDefaultLookAtPosWs();

			g_prim.Draw(DebugString(defPosWs, "look-at (default)", kColorGray));
			g_prim.Draw(DebugCross(defPosWs, 0.5f, kColorGray));

			DebugDrawVector(eyePosWs, GetLocalZ(eyeLocWs.Rot()) * 2.1f, kColorBlue);
			g_prim.Draw(DebugString(eyePosWs + GetLocalZ(eyeLocWs.Rot()) * 2.1f, "eye", kColorBlue));

			DebugDrawVector(eyePosWs, GetLocalZ(m_lastEyeAnimRotWs) * 2.5f, kColorWhiteTrans);
			g_prim.Draw(DebugString(eyePosWs + GetLocalZ(m_lastEyeAnimRotWs) * 2.5f, "last eye", kColorWhiteTrans));
		}

		DebugDrawVector(eyePosWs, GetLocalZ(GetEyeRawWs().Rot()) * 1.0f, kColorGreen);
		g_prim.Draw(DebugString(eyePosWs + GetLocalZ(GetEyeRawWs().Rot()) * 1.0f, "raweye", kColorGreen));

#if SHOW_AIM_ERROR
		if (const ProcessWeaponBase* pWeapon = GetWeapon())
		{
			if (GetNdAnimationControllers()->GetWeaponController()->IsAiming())
			{
				const Locator muzzleLoc = pWeapon->GetMuzzleLocator();
				const Vector muzzleZ = GetLocalZ(muzzleLoc.Rot());

				g_prim.Draw(DebugLine(aimFromWs, muzzleZ, kColorOrange));

				float currAngleY = Asin(muzzleZ.Y());
				float desAngleY = Asin(desiredAimVec.Y());

				float angleErrY = RADIANS_TO_DEGREES(Abs(currAngleY - desAngleY));
				Vector muzzleZflat = muzzleZ;
				muzzleZflat.SetY(0.0f);
				Vector desiredAimFlat = desiredAimVec;
				desiredAimFlat.SetY(0.0f);
				muzzleZflat = SafeNormalize(muzzleZflat, kZero);
				desiredAimFlat = SafeNormalize(desiredAimFlat, kZero);
				float dotXZ = DotXz(muzzleZflat, desiredAimFlat);
				float angleErrXZ = RADIANS_TO_DEGREES(SafeAcos(dotXZ));
				char buf[128];
				sprintf(buf, "errY: %0.1f errXZ: %0.1f", angleErrY, angleErrXZ);
				g_prim.Draw(DebugString(aimFromWs, buf));
			}
		}
#endif
	}

	if (g_navCharOptions.m_displayPathDirection)
	{
		// Move direction
		const Vector velocityWs = GetParentSpace().TransformVector(GetVelocityPs());
		const Vector moveDirWs = LengthSqr(velocityWs) > 0.001f ? Normalize(velocityWs) : GetLocalZ(GetRotation());

		psPrim.DrawArrow(curPosPs + Vector(0,0.3f,0), m_pNavStateMachine->GetPathDirPs(), 0.5f, kColorRed);
		psPrim.DrawString(curPosPs + Vector(0,0.3f,0) + m_pNavStateMachine->GetPathDirPs(), "Path", kColorRed);
	}

	if (g_navCharOptions.m_displayCoverStatus)
	{
		if (GetTakingCover())
		{
			psPrim.DrawString(curPosPs, "COVER", kColorGreen);
		}
	}

	m_pAnimationControllers->DebugDraw(pPrinter);

	if (g_navCharOptions.m_displayNavState)
	{
		m_pNavStateMachine->DebugDraw();
		if (isSelected)
		{
			m_pNavStateMachine->DebugDumpToStream(GetMsgOutput(kMsgCon));
		}
	}

	if (g_navCharOptions.m_displayNavigationHandoffs)
	{
		MsgCon("Navigation Handoffs [%s]:\n", GetName());
		for (U32 i = 0; i < kMaxKnownHandoffs; ++i)
		{
			const NavAnimHandoffDesc& desc = m_knownHandoffs[i];
			if (!desc.IsValid(this))
				continue;

			MsgCon("  [%d] State:           %s\n"
				   "      Motion type:     %s\n",
				   i,
				   DevKitOnly_StringIdToString(desc.GetStateNameId()),
				   GetMotionTypeName(desc.m_motionType));
		}
	}

	if (g_navCharOptions.m_displayInterCharDepen)
	{
		const Color color = AI::IndexToColor(GetUserId().GetValue());
		const Vector kOffsetY(0.0f, 0.1f, 0.0f);
		const Point charPos = GetTranslation();
		const F32 radius = GetInterCharacterDepenRadius();
		const Segment segment = GetDepenetrationSegmentWs(charPos);
		if (DistSqr(segment.a, segment.b) > 0.001f)
		{
			g_prim.Draw(DebugCapsule(segment.a, segment.b, radius, color));
		}
		else
		{
			g_prim.Draw(DebugCircle(charPos + kOffsetY, kUnitYAxis, radius, color));
		}
	}

	if (g_navCharOptions.m_displayGround)
	{
		const DC::AnimState* pCurrentState = pBaseLayer->CurrentState();
		const DC::AnimStateFlag currentAnimStateFlags = pCurrentState ? pCurrentState->m_flags : 0;
		const float adjustmentBlend = 1.0f - NoAdjustToGroundFlagBlender(this).BlendForward(pBaseLayer, 0.0f);

		const Point groundPosPs		= GetGroundPosPs();
		const Vector groundNormalPs = GetGroundNormalPs();

		const Point filteredGroundPosPs		= GetFilteredGroundPosPs();
		const Vector filteredGroundNormalPs = GetFilteredGroundNormalPs();

		if (m_requestSnapToGround)
		{
			g_prim.Draw(DebugString(filteredGroundPosPs, "SNAP!", kColorRed), Seconds(3.0f));
		}

		StringBuilder<256> smoothStr;
		smoothStr.format("smoothed @ %0.1f", adjustmentBlend * 100.0f);

		PrimServerWrapper ps(parentSpace);
		ps.EnableHiddenLineAlpha();
		ps.DrawPlaneCross(filteredGroundPosPs, filteredGroundNormalPs, kColorCyan);
		Point arrowEndPs = filteredGroundPosPs + filteredGroundNormalPs;
		ps.DrawString(arrowEndPs, smoothStr.c_str(), kColorCyanTrans, 0.5f);
		ps.DrawArrow(filteredGroundPosPs, arrowEndPs, 0.5f, kColorCyanTrans);

		ps.DrawPlaneCross(groundPosPs, groundNormalPs, kColorRedTrans);
		arrowEndPs = groundPosPs + (groundNormalPs * 0.85f);
		ps.DrawString(arrowEndPs, "actual", kColorRedTrans, 0.5f);
		ps.DrawArrow(groundPosPs, arrowEndPs, 0.5f, kColorRedTrans);
	}

	//g_prim.Draw(DebugCross(GetParentSpace().TransformPoint(m_groundPositionPs)));

	//GetJointModifiers()->DebugDraw();

/*
	const Locator alignWs = GetLocator();
	g_prim.Draw(DebugCross(alignWs.TransformPoint(pInfo->m_leftAnklePosLs)));
	g_prim.Draw(DebugCross(alignWs.TransformPoint(pInfo->m_rightAnklePosLs)));
*/
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NavCharacter::GetInstantaneousVelocityPs() const
{
	if (m_velocityPsHistory.IsEmpty())
		return kZero;

	return m_velocityPsHistory.GetAtRawIndex(m_velocityPsHistory.GetNewestRawIndex());
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavCharacter::GetSpeedPs(bool xzOnly /*= false*/) const
{
	float result = 0.0f;
	const int sampleCount = m_velocityPsHistory.GetCount();
	for (int i = 0; i < sampleCount; ++i)
	{
		result += xzOnly ? LengthXz(m_velocityPsHistory.GetAt(i)) : Length(m_velocityPsHistory.GetAt(i));
	}

	if (sampleCount > 0)
		result /= sampleCount;

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::OnStairs() const
{
	const LegRaycaster* pRayCaster = GetLegRaycaster();
	if (!pRayCaster)
		return false;

	const bool feetOnStairs = pRayCaster->GetProbeResults(0).m_pat.GetStairs()
							  || pRayCaster->GetProbeResults(1).m_pat.GetStairs();
	const bool alignOnStairs = m_currentPat.GetStairs();
	return alignOnStairs || feetOnStairs || g_navCharOptions.m_forceOnStairs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsInWallShimmy() const
{
	bool isInWallShimmy = false;

#if ENABLE_NAV_LEDGES
	if (IsClimbing())
	{
		const NdAnimationControllers* pAnimControllers = GetNdAnimationControllers();
		const INdAiClimbController* pClimbController = pAnimControllers->GetClimbController();

		isInWallShimmy = pClimbController->IsInWallShimmy();
	}
#endif // ENABLE_NAV_LEDGES

	return isInWallShimmy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsHanging() const
{
	bool isHanging = false;

#if ENABLE_NAV_LEDGES
	if (IsClimbing())
	{
		const NdAnimationControllers* pAnimControllers = GetNdAnimationControllers();
		const INdAiClimbController* pClimbController = pAnimControllers->GetClimbController();

		isHanging = pClimbController->IsHanging();
	}
#endif // ENABLE_NAV_LEDGES

	return isHanging;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsClimbing() const
{
	bool isClimbing = false;

#if ENABLE_NAV_LEDGES
	if (GetNavLocation().GetType() == NavLocation::Type::kNavLedge)
	{
		isClimbing = true;
	}
	else if (const NavControl* pNavControl = GetNavControl())
	{
		if (pNavControl->GetActiveNavType() == NavLocation::Type::kNavLedge)
		{
			isClimbing = true;
		}
	}
#endif // ENABLE_NAV_LEDGES

	return isClimbing;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsFreeRoping() const
{
	// TODO: check if current TAP is using rope?
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsInCover() const
{
	const ActionPack* pAp = GetEnteredActionPack();
	return pAp && pAp->GetType() == ActionPack::kCoverActionPack;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsInHighCover() const
{
	const ActionPack* pAp = GetEnteredActionPack();
	if (pAp && pAp->GetType() == ActionPack::kCoverActionPack)
	{
		CoverActionPack* pCoverAp = (CoverActionPack*)pAp;
		if (pCoverAp->GetDefinition().IsLow())
			return false;
		else
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsSharingCover() const
{
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsInStealthVegetation() const
{
	const NavControl* pNavControl = GetNavControl();
	return pNavControl ? pNavControl->IsInStealthVegetation() : false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 NavCharacter::GetDesiredLegRayCastMode() const
{
	U32 mode = LegRaycaster::kModeDefault;

	if (const INavAnimController* pNavAnimController = GetActiveNavAnimController())
	{
		mode = pNavAnimController->GetDesiredLegRayCastMode();
	}

	return mode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::UpdateWetness()
{
	PROFILE(AI, NavChar_UpdateWetness);

	ParentClass::UpdateWetness();

	const I32 fxId = GetWetMaskIndex();
	if (fxId == FxMgr::kInvalid)
		return;

	if (!m_pWetPoints)
		return;

	U32F index = 0;

	AttachSystem* pAttachSystem = GetAttachSystem();

	while ((m_pWetPoints[index].m_jointA != INVALID_STRING_ID_64) && (m_pWetPoints[index].m_jointB != INVALID_STRING_ID_64))
	{
		AttachIndex indA, indB;
		bool res = pAttachSystem->FindPointIndexById(&indA, m_pWetPoints[index].m_jointA);
		res = res && pAttachSystem->FindPointIndexById(&indB, m_pWetPoints[index].m_jointB);

		if (res)
		{
			const Point posA = pAttachSystem->GetAttachPosition(indA);
			const Point posB = pAttachSystem->GetAttachPosition(indB);

			WaterDetector& detA = m_pWaterDetectors[index*2];
			WaterDetector& detB = m_pWaterDetectors[index*2+1];

			detA.Update(FILE_LINE_FUNC, posA);
			detB.Update(FILE_LINE_FUNC, posB);

			const bool posAunderWater = detA.IsUnderwater();
			Point posAatWaterLevel = detA.WaterSurface();

			const bool posBunderWater = detB.IsUnderwater();
			Point posBatWaterLevel = detB.WaterSurface();

			// both points under water
			if (posAunderWater && posBunderWater)
			{
				g_fxMgr.GetWet(this, 0, m_pWetPoints[index].m_uStart, m_pWetPoints[index].m_vStart, m_pWetPoints[index].m_uEnd, m_pWetPoints[index].m_vEnd);
			}
			// no points under water
			else if (!posAunderWater && !posBunderWater)
			{
				// do nothing
			}
			// only one point under water
			else
			{
				static float s_debugInterpV = -1.0f;
				const float maxLen = Dist(posB, posA);

				// assume posB should be higher than posA
				const float intputLen = posAunderWater ? (maxLen - Dist(posB, posAatWaterLevel)) : Dist(posBatWaterLevel, posA);

				// only lerp the v
				float interpolatedV = LerpScale(0.0f,
												maxLen,
												m_pWetPoints[index].m_vStart,
												m_pWetPoints[index].m_vEnd,
												intputLen);

				if (s_debugInterpV >= 0.0f)
				{
					interpolatedV = s_debugInterpV;
				}

				g_fxMgr.GetWet(this, 0,
							   m_pWetPoints[index].m_uStart,
							   m_pWetPoints[index].m_vStart,
							   m_pWetPoints[index].m_uEnd,
							   interpolatedV);
			}
		}

		++index;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::UpdateVulnerability(bool isAnimationControlled)
{
	DC::Vulnerability vul = DC::kVulnerabilityNone;

	// hit reaction, sync action etc
	if (isAnimationControlled)
	{
		vul = DC::kVulnerabilityNone;
	}
	else if (m_pNavStateMachine->IsStopped())
	{
		vul = DC::kVulnerabilityStanding;
	}
	else
	{
		vul = DC::kVulnerabilityRun;
	}

	SetVulnerability(vul);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetUndirectedPlayerBlockageCost(Nav::PlayerBlockageCost undirectedPlayerBlockageCost)
{
	if (m_pNavControl)
		m_pNavControl->SetUndirectedPlayerBlockageCost(undirectedPlayerBlockageCost);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ClearUndirectedPlayerBlockageCost()
{
	if (m_pNavControl)
		m_pNavControl->ClearUndirectedPlayerBlockageCost();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Nav::PlayerBlockageCost NavCharacter::GetUndirectedPlayerBlockageCost() const
{
	return m_pNavControl ? m_pNavControl->GetUndirectedPlayerBlockageCost() : Nav::PlayerBlockageCost::kExpensive;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetDirectedPlayerBlockageCost(Nav::PlayerBlockageCost directedPlayerBlockageCost)
{
	if (m_pNavControl)
		m_pNavControl->SetDirectedPlayerBlockageCost(directedPlayerBlockageCost);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ClearDirectedPlayerBlockageCost()
{
	if (m_pNavControl)
		m_pNavControl->ClearDirectedPlayerBlockageCost();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Nav::PlayerBlockageCost NavCharacter::GetDirectedPlayerBlockageCost() const
{
	return m_pNavControl ? m_pNavControl->GetDirectedPlayerBlockageCost() : Nav::PlayerBlockageCost::kExpensive;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::SetCareAboutPlayerBlockageDespitePushingPlayer()
{
	if (m_pNavControl)
		m_pNavControl->SetCareAboutPlayerBlockageDespitePushingPlayer();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ClearCareAboutPlayerBlockageDespitePushingPlayer()
{
	if (m_pNavControl)
		m_pNavControl->ClearCareAboutPlayerBlockageDespitePushingPlayer();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::CareAboutPlayerBlockageDespitePushingPlayer() const
{
	return m_pNavControl ? m_pNavControl->CareAboutPlayerBlockageDespitePushingPlayer() : false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::DebugDrawWetness() const
{
	STRIP_IN_FINAL_BUILD;
	PROFILE(AI, NavChar_DebugDrawWetness);

	const I32 fxId = GetWetMaskIndex();
	if (fxId == FxMgr::kInvalid)
		return;

	if (!m_pWetPoints)
		return;

	U32F index = 0;

	const AttachSystem* pAttachSystem = GetAttachSystem();

	while ((m_pWetPoints[index].m_jointA != INVALID_STRING_ID_64) && (m_pWetPoints[index].m_jointB != INVALID_STRING_ID_64))
	{
		AttachIndex indA, indB;
		bool res = pAttachSystem->FindPointIndexById(&indA, m_pWetPoints[index].m_jointA);
		res = res && pAttachSystem->FindPointIndexById(&indB, m_pWetPoints[index].m_jointB);

		if (res)
		{
			const Point posA = pAttachSystem->GetAttachPosition(indA);
			const Point posB = pAttachSystem->GetAttachPosition(indB);

			WaterDetector& detA = m_pWaterDetectors[index*2];
			WaterDetector& detB = m_pWaterDetectors[index*2+1];

			bool posAunderWater = detA.IsUnderwater();
			bool posBunderWater = detB.IsUnderwater();

			g_prim.Draw(DebugCross(posA, 0.3f, posAunderWater ? kColorGreen : kColorWhite, PrimAttrib(0)));
			g_prim.Draw(DebugCross(posB, 0.3f, posBunderWater ? kColorGreen : kColorWhite, PrimAttrib(0)));
			g_prim.Draw(DebugLine(posA, posB, posAunderWater ? kColorGreen : kColorWhite, posBunderWater ? kColorGreen : kColorWhite, 2.0f, PrimAttrib(0)));

		}

		++index;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point NavCharacter::GetDefaultLookAtPosWs() const
{
	const Locator eyeRawLocWs = GetEyeRawWs();
	const Point lookAtPosWs = eyeRawLocWs.Pos() + GetLocalZ(eyeRawLocWs.Rot());

	return lookAtPosWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NavCharacter::GetJointLocBindPoseLs(I32F jointIdx) const
{
	if (m_pAnimData)
	{
		JointCache& jointCache = m_pAnimData->m_jointCache;
		ANIM_ASSERT(jointIdx >= 0 && jointIdx < jointCache.GetNumTotalJoints());

		Transform jointT;
		if (jointCache.GetBindPoseJointXform(jointT, (U32F)jointIdx))
		{
			Locator jointLoc(jointT);
			ANIM_ASSERT(IsReasonable(jointLoc));
			return jointLoc;
		}
	}
	return Locator(kOrigin);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NavCharacter::GetEyeLocBindPoseLs() const
{
	AI_ASSERT(m_pAnimData);
	if (m_lookAttachIndex != AttachIndex::kInvalid)
	{
		U32F jointIdx = GetAttachSystem()->GetJointIndex(m_lookAttachIndex);
		return GetJointLocBindPoseLs(jointIdx);
	}
	return Locator(kOrigin);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Scalar NavCharacter::GetEyeHeightBindPose() const
{
	return GetEyeLocBindPoseLs().Pos().Y();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::RequestEnterScriptedAnimationState()
{
	if (!m_pScriptedAnimation->IsActiveRequested())
	{
		Log(this, kNpcLogChannelGeneral, "NavChar::RequestEnterScriptedAnimationState\n");
		m_pScriptedAnimation->Enter(this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::RequestStopScriptedAnimationState()
{
	if (m_pScriptedAnimation->IsActiveRequested())
	{
		Log(this, kNpcLogChannelGeneral, "NavChar::RequestStopScriptedAnimationState\n");
		m_pScriptedAnimation->RequestStop(this, -1.0f);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::RequestExitScriptedAnimationState()
{
	if (m_pScriptedAnimation->IsActiveRequested())
	{
		Log(this, kNpcLogChannelGeneral, "NavChar::RequestExitScriptedAnimationState\n");
		m_pScriptedAnimation->Exit(this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsInScriptedAnimationState() const
{
	return m_pScriptedAnimation->IsActive(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 NavCharacter::GetNetSnapshotTime() const
{
	if (m_pNetAnimCmdGenerator)
		return m_pNetAnimCmdGenerator->GetTime();
	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::ValidateMoveToLocation(const NavLocation& dest, float yThreshold /* = -1.0f */) const
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const bool valid = dest.IsValid();

	if (!valid)
	{
		LogHdr(this, kNpcLogChannelGeneral);
		LogMsg(this, kNpcLogChannelGeneral, "ValidateMoveToPos failed to find nav mesh at destWs");
		LogPoint(this, kNpcLogChannelGeneral, dest.GetPosWs());
		LogMsg(this, kNpcLogChannelGeneral, "\n");
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsUsingBadTraversalActionPack() const
{
	return m_pNavStateMachine && m_pNavStateMachine->IsUsingBadTraversalActionPack();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsInWaistDeepWater() const
{
	return GetSplasherSet() && GetSplasherSet()->TorsoInWater();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::Interrupt(const char* sourceFile, U32F sourceLine, const char* sourceFunc)
{
	LogNavInterruption(sourceFile, sourceLine, sourceFunc);

	m_requestedMotionType = kMotionTypeMax;

	m_pNavStateMachine->Interrupt(sourceFile, sourceLine, sourceFunc);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::LogNavInterruption(const char* sourceFile, U32F sourceLine, const char* sourceFunc) const
{
	Log(this, kNpcLogChannelGeneral, "Interrupting:%s\n", IsInScriptedAnimationState() ? " (scripted anim playing)" : "");
	Log(this, kNpcLogChannelGeneral, "  Source: %s %s:%d\n", sourceFunc, sourceFile, sourceLine);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ForceDefaultIdleState()
{
	if (INavAnimController* pNavAnimController = GetActiveNavAnimController())
	{
		pNavAnimController->ForceDefaultIdleState();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsAnimationWalkToIdle() const
{
	return GetNavStateMachine()->IsState(NavStateMachine::kStateStopping);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ConfigureNavigationHandOff(const NavAnimHandoffDesc& desc,
											  const char* sourceFile,
											  U32F sourceLine,
											  const char* sourceFunc)
{
	I32F updateIndex = -1;

	for (U32F i = 0; i < kMaxKnownHandoffs; ++i)
	{
		if (m_knownHandoffs[i].ShouldUpdateFrom(this, desc))
		{
			m_knownHandoffs[i] = desc;
			updateIndex = i;
			break;
		}
	}

	if (updateIndex < 0)
	{
		AiLogAnim(this,
				  "Registering new handoff ['%s' -> '%s'] [state-change : %d | state-id : %08x | subsys: %d] from %s:%d:%s\n",
				  DevKitOnly_StringIdToString(desc.GetStateNameId()),
				  desc.m_motionType < kMotionTypeMax ? GetMotionTypeName(desc.m_motionType) : "stopped",
				  desc.GetChangeRequestId().GetVal(),
				  desc.GetAnimStateId(this).GetValue(),
				  desc.GetSubsystemControllerId(),
				  sourceFile,
				  sourceLine,
				  sourceFunc);

		for (I32F i = kMaxKnownHandoffs - 1; i > 0; --i)
		{
			m_knownHandoffs[i] = m_knownHandoffs[i-1];
		}

		m_knownHandoffs[0] = desc;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavCharacter::GetRegisteredNavAnimHandoffs(NavAnimHandoffDesc* pHandoffsOut, U32F maxHandoffsOut) const
{
	const U32F numOut = Min(U32F(kMaxKnownHandoffs), maxHandoffsOut);
	for (U32F i = 0; i < numOut; ++i)
	{
		pHandoffsOut[i] = m_knownHandoffs[i];
	}
	return numOut;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::PeekValidHandoff(NavAnimHandoffDesc* pHandoffOut) const
{
	const INavAnimController* pNavAnimController = GetActiveNavAnimController();

	if (!pNavAnimController)
	{
		return false;
	}

	bool valid = false;

	for (U32F i = 0; i < kMaxKnownHandoffs; ++i)
	{
		if (m_knownHandoffs[i].IsValid(this))
		{
			if (pHandoffOut)
				*pHandoffOut = m_knownHandoffs[i];
			valid = true;
			break;
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavAnimHandoffDesc NavCharacter::PopValidHandoff()
{
	I32F returnIndex = -1;

	for (U32F i = 0; i < kMaxKnownHandoffs; ++i)
	{
		if (m_knownHandoffs[i].IsValid(this))
		{
			returnIndex = i;
			break;
		}
	}

	NavAnimHandoffDesc ret;
	ret.Reset();

	if (returnIndex < 0)
	{
		// nothing valid found
		return ret;
	}

	ret = m_knownHandoffs[returnIndex];

	// shift everything up
	for (U32F i = returnIndex; i < kMaxKnownHandoffs - 1; ++i)
	{
		m_knownHandoffs[i] = m_knownHandoffs[i + 1];
	}

	m_knownHandoffs[kMaxKnownHandoffs - 1].Reset();

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::NotifyApReservationLost(const ActionPack* pAp)
{
	GetNavStateMachine()->NotifyApReservationLost(pAp);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::ConfigRagdollGroundProbes(RagdollGroundProbesConfig& config) const
{
	if (m_pAnimationControllers)
	{
		if (INdAiHitController* pHitController = m_pAnimationControllers->GetHitController())
		{
			config.m_blendOutHoleDepth = pHitController->GetRagdollBlendOutHoleDepth();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const TimeFrame NavCharacter::GetPathFoundTime() const
{
	if (m_pNavStateMachine)
	{
		return m_pNavStateMachine->GetLastPathFoundTime();
	}

	return TimeFrameNegInfinity();
}

/// --------------------------------------------------------------------------------------------------------------- ///
MeshProbe::SurfaceType NavCharacter::GetCachedFootSurfaceType() const
{
	return m_groundMeshRaycastResult[kMeshRaycastGround].m_surfaceTypeInfo.m_layers[0].m_surfaceType;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::CheckForApReservationSwap()
{
	if (NavStateMachine* pNsm = GetNavStateMachine())
	{
		pNsm->CheckForApReservationSwap();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLocation NavCharacter::AsReachableNavLocationWs(Point_arg posWs, NavLocation::Type navType) const
{
	const NavControl* pNavControl = GetNavControl();

	const float maxRadius  = pNavControl ? pNavControl->GetMaximumNavAdjustRadius() : 0.0f;
	const float pathRadius = pNavControl ? pNavControl->GetPathFindRadius() : 0.0f;
	const float convRadius = Max(maxRadius, pathRadius);

	Segment probeSegment = GetDepenetrationSegmentWs(posWs);

	const NavLocation ret = NavUtil::ConstructNavLocation(posWs, navType, convRadius, probeSegment, 1.0f, 2.0f, GetObeyedStaticBlockers());
	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLocation NavCharacter::GetNavLocation() const
{
	NavLocation ret;

	if (m_pNavControl)
	{
		ret = m_pNavControl->GetNavLocation();
	}
	else
	{
		const Point posPs = GetNavigationPosPs();
		const Point posWs = GetParentSpace().TransformPoint(posPs);

		ret.SetWs(posWs);
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
INavAnimController* NavCharacter::GetActiveNavAnimController()
{
	NdAnimationControllers* pAnimControllers = GetNdAnimationControllers();
	INavAnimController* pController = nullptr;

	switch (m_pNavControl->GetActiveNavType())
	{
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		pController = pAnimControllers->GetClimbController();
		break;
#endif // ENABLE_NAV_LEDGES

	case NavLocation::Type::kNavPoly:
	default:
		if (m_pNavControl->NavMeshForcesSwim() || m_pNavControl->NavMeshForcesDive())
		{
			pController = pAnimControllers->GetSwimController();
		}
		else
		{
			pController = pAnimControllers->GetLocomotionController();
		}
		break;
	}

	return pController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const INavAnimController* NavCharacter::GetActiveNavAnimController() const
{
	return ((NavCharacter*)this)->GetActiveNavAnimController();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavCharacter::GetNavigationPosPs() const
{
	Point posPs = GetTranslationPs();

	if (const INavAnimController* pNavAnimController = GetActiveNavAnimController())
	{
		posPs = pNavAnimController->GetNavigationPosPs();
	}

	return posPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavCharacter::IsNextWaypointTypeStop() const
{
	return GetNavStateMachine()->IsNextWaypointTypeStop();
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessSnapshot* NavCharacter::AllocateSnapshot() const
{
	return NavCharacterSnapshot::Create(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavCharacter::RefreshSnapshot(ProcessSnapshot* pSnapshot) const
{
	PROFILE(Processes, NavCharacter_RefreshSnapshot);

	ParentClass::RefreshSnapshot(pSnapshot);

	if (NavCharacterSnapshot* pNavCharSnapshot = NavCharacterSnapshot::FromSnapshot(pSnapshot))
	{
		pNavCharSnapshot->m_curNavAdjustRadius = GetCurrentNavAdjustRadius();
		pNavCharSnapshot->m_maxNavAdjustRadius = GetMaximumNavAdjustRadius();
		pNavCharSnapshot->m_interCharDepenRadius = GetInterCharacterDepenRadius();
		pNavCharSnapshot->m_inflateRadius = m_inflateRadius;
		pNavCharSnapshot->m_depenSegmentPs = GetDepenetrationSegmentPs(GetTranslationPs(), true);

		if (const NavStateMachine* pNsm = GetNavStateMachine())
		{
			const PathWaypointsEx* pCurrentPathPs = pNsm->GetCurrentPathPs();
			pNavCharSnapshot->m_currentPathPs.CopyFrom(pCurrentPathPs);
			pNavCharSnapshot->m_followingPath = pNsm->IsFollowingPath();
			pNavCharSnapshot->m_interruptedOrMovingBlind = pNsm->IsInterrupted()
														   || pNsm->IsState(NavStateMachine::kStateMovingBlind);
		}
	}
}
