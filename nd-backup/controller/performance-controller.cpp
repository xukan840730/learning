/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/performance-controller.h"

#include "corelib/util/random.h"

#include "ndlib/anim/anim-align-cache.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/text-printer.h"

#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/ai/component/nd-ai-anim-config.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"

#include "ailib/nav/nav-ai-util.h"

#include "game/ai/agent/npc-manager.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/coordinator/encounter-coordinator.h"
#include "game/ai/knowledge/entity.h"
#include "game/ai/knowledge/knowledge.h"
#include "game/ai/skill/skill.h"
#include "game/flame-manager.h"
#include "game/scriptx/h/anim-npc-info.h"

static CONST_EXPR DC::PerformanceTypeMask kStrictTypeMatchMask = DC::kPerformanceTypeMaskDogLeash;

/// --------------------------------------------------------------------------------------------------------------- ///
static bool PerformanceHasClearMotion(const NavCharacter* pNavChar,
									  const DC::PerformanceEntry& performance,
									  const Locator& apRef,
									  bool debugDraw)
{
	PROFILE_AUTO(AI);

	//if (!pStartMesh)
	//	return false;

	//NavMesh::ProbeParams params = probeParams;
	//params.m_start = pStartMesh->WorldToLocal(startPos);
	//params.m_move = pStartMesh->WorldToLocal(endPos) - params.m_start;

	//const NavMesh::ProbeResult result = pStartMesh->ProbeLs(&params);
	//const bool clear = (result == NavMesh::ProbeResult::kReachedGoal);

	//if (debug)
	//{
	//	const DebugPrimTime kDebugDuration = Seconds(3.0f);
	//	const Vector kOffsetY = VECTOR_LC(0.0f, 0.2f, 0.0f);
	//	const Point reachedPos = pStartMesh->LocalToWorld(params.m_endPoint);
	//	const Color color = clear ? kColorGreen : kColorRed;

	//	g_prim.Draw(DebugArrow(startPos + kOffsetY, reachedPos + kOffsetY, color), kDebugDuration);
	//	if (params.m_probeRadius > 0.0f)
	//	{
	//		g_prim.Draw(DebugCircle(reachedPos + kOffsetY, kUnitYAxis, params.m_probeRadius, color), kDebugDuration);
	//		if (!clear)
	//		{
	//			g_prim.Draw(DebugArrow(reachedPos + kOffsetY, endPos + kOffsetY, kColorRed), kDebugDuration);
	//		}
	//	}
	//}

	//return clear;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const NavControl* pNavControl = pNavChar->GetNavControl();
	if (!pNavControl)
	{
		return false;
	}

	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	if (!pAnimControl)
	{
		return false;
	}

	const NavMesh* pNavMesh = pNavControl->GetNavMesh();
	if (!pNavMesh)
	{
		return false;
	}

	const AnimStateLayer* pStateLayer = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurrentInstance = pStateLayer ? pStateLayer->CurrentStateInstance() : nullptr;
	const Transform animDeltaTweak = (pCurrentInstance && pCurrentInstance->IsAnimDeltaTweakEnabled()) ? pCurrentInstance->GetAnimDeltaTweakTransform() : Transform(kIdentity);

	const Locator& parentSpace = pNavMesh->GetParentSpace();

	const SkeletonId skelId = pNavChar->GetSkeletonId();

	bool valid = true;

	const Locator identLoc = Locator(kIdentity);
	Locator prevLocPs = pNavChar->GetLocatorPs();

	for (int iPerformancePart = 0; performance.m_animIds && iPerformancePart < performance.m_animIds->m_count; iPerformancePart++)
	{
		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(performance.m_animIds->m_array[iPerformancePart]).ToArtItem();
		if (!pAnim)
		{
			continue;
		}

		float evalPhase = 0.0f;
		const float animDuration = pAnim->m_pClipData->m_fNumFrameIntervals * pAnim->m_pClipData->m_secondsPerFrame;
		const float phaseStep = pAnim->m_pClipData->m_phasePerFrame;

		const float kEndPhase = iPerformancePart == performance.m_animIds->m_count - 1 ? performance.m_exitPhase : 1.0f;

		int index = 0;
		do
		{
			Locator nextLocLs = identLoc;
			if (!FindAlignFromApReferenceCached(skelId,
				pAnim,
				Min(evalPhase, kEndPhase),
				identLoc,
				SID("apReference"),
				performance.m_mirror,
				&nextLocLs))
			{
				return false;
			}

			const Point tweakedPosLs = (nextLocLs.Pos() - kOrigin) * animDeltaTweak + Point(kOrigin);
			nextLocLs.SetPos(tweakedPosLs);

			Color lineClr = kColorGreen;

			const Locator nextLocPs = apRef.TransformLocator(nextLocLs);

			const Point prevPosPs = prevLocPs.Pos();
			const Point nextPosPs = nextLocPs.Pos();

			const Point prevPosWs = parentSpace.TransformPoint(prevPosPs);
			const Point nextPosWs = parentSpace.TransformPoint(nextPosPs);

			if (FlameManager::Get().IsRayThroughFlame(prevPosWs, nextPosWs, 0.5f))
			{
				return false;
			}

			NavMesh::ProbeParams probeParams;
			probeParams.m_start = pNavMesh->WorldToLocal(prevPosWs);
			probeParams.m_move = pNavMesh->WorldToLocal(nextPosWs - prevPosWs);
			probeParams.m_pStartPoly = nullptr;

			pNavMesh->ProbeLs(&probeParams);

			if (probeParams.m_hitEdge)
			{
				bool wasValid = valid;
				valid = false;
				lineClr = kColorRed;

				if (probeParams.m_pHitPoly && probeParams.m_pHitPoly->IsLink())
				{
					if (pNavControl->StaticClearLineOfMotionPs(prevPosPs, nextPosPs))
					{
						lineClr = kColorBlue;
						valid = true;
					}
				}
			}

			if (valid && !pNavControl->BlockersOnlyClearLineOfMotion(prevPosPs, nextPosPs))
			{
				valid = false;
				lineClr = kColorOrange;
			}

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				const Vector verticalOffsetPs = Vector(0.0f, 0.15f, 0.0f);
				PrimServerWrapper ps = PrimServerWrapper(parentSpace);
				ps.SetLineWidth(4.0f);
				ps.DrawLine(prevPosPs + verticalOffsetPs, nextPosPs + verticalOffsetPs, lineClr);

				index++;
				if (index % 3 == 0)
				{
					ps.DrawCross(prevPosPs, 0.1f, lineClr);
					char buf[16];
					sprintf(buf, "%.2f", evalPhase);
					ps.DrawString(prevPosPs, buf, lineClr);
				}
			}
			else if (!valid)
			{
				return false;
			}

			prevLocPs = nextLocPs;
			evalPhase += phaseStep;
		} while (evalPhase <= kEndPhase);
	}

	return valid;
}


/// --------------------------------------------------------------------------------------------------------------- ///
struct AiPerformanceController : public IAiPerformanceController
{
public:

	AiPerformanceController();

	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;

	virtual void Reset() override;
	virtual void ConfigureCharacter(Demeanor demeanor,
									const DC::NpcDemeanorDef* pDemeanorDef,
									const NdAiAnimationConfig* pAnimConfig) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void Interrupt() override;

	virtual void UpdateStatus() override;
	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;
	virtual bool IsBusy() const override;

	virtual bool ShouldInterruptNavigation() const override;
	virtual bool ShouldInterruptSkills() const override;

	virtual U64 CollectHitReactionStateFlags() const override;

	virtual bool IsPerformancePlaying() const override;
	virtual bool PlayPerformance(Point_arg targPosWs,
								 DC::PerformanceTypeMask performanceType,
								 const PlayPerformanceOptions& options) override;

	virtual bool LockGestures() const override
	{
		if (!IsPerformancePlaying())
			return false;

		return !m_allowGestures;
	}

	virtual const DC::PerformanceEntry* GetCurrentPerformance() const override
	{
		if (!IsPerformancePlaying())
			return nullptr;

		return &m_currentPerformance;
	}

	virtual bool PlayAnim(const PlayPerformanceAnimParams& params) override;

	virtual F32 HackGetAnimFrame(const AnimControl* pAnimControl) const override
	{
		return m_fullBodyAnimAction.GetAnimFrame(pAnimControl);
	}

protected:

	struct AvailablePerformance
	{
		const ArtItemAnim* m_pAnim = nullptr;
		Locator m_initApRefLocWs = kIdentity;
		DC::SelfBlendParams m_selfBlend;
		const DC::PerformanceEntry* m_pDcDef = nullptr;
	};

	enum class HandoffType
	{
		kNone,
		kIdle,
		kMoving
	};

	static DC::PerformanceDirectionMask GetPerformanceDirFromAngleDeg(F32 angleDeg);

	struct VisualizeLosInfo
	{
		Point m_probeStart;
		Point m_probeEnd;
		Point m_probeHit;

		union
		{
			struct
			{
				bool m_tested : 1;
				bool m_clear : 1;
				bool m_enoughClearDist : 1;
			};
			U8 m_flags;
		};

		F32 m_clearDist;

		VisualizeLosInfo()
		{
			m_flags = 0;
			m_clearDist = 0.0f;
		}
	};

	static const char* GetPerformanceDirString(DC::PerformanceDirectionMask dir);
	void DebugPerformanceFilter(Point performancePos,
								const I32F iDemeanor,
								DC::PerformanceTypeMask performanceType,
								MotionType motionType,
								bool weaponHolstered,
								bool shouldPoint,
								F32 distance,
								DC::PerformanceDirectionMask dir,
								TextPrinterParentSpace& printer,
								Color color,
								DebugPrimTime duration,
								const PlayPerformanceOptions& options) const;
	void VisualizePerformance(const DC::PerformanceEntry& performance,
							  U32F index,
							  const Locator& finalApRefLoc,
							  const NavMesh* pCharMesh,
							  const NavMesh::ProbeParams& probeParams,
							  bool demeanorValid,
							  bool typeValid,
							  bool motionValid,
							  bool weaponValid,
							  bool pointingValid,
							  bool swimmingValid,
							  bool inPlaceValid,
							  bool dirValid,
							  bool isUnique,
							  bool distanceValid, F32 distanceMin, F32 distanceMax,
							  VisualizeLosInfo* pLosInfoo,
							  TextPrinterParentSpace& printer,
							  Color color,
							  DebugPrimTime duration) const;

	virtual TimeFrame GetLastPerformanceStartedTime() const override { return m_lastPerformanceStartedTime; }
	virtual TimeFrame GetLastPerformanceActiveTime() const override { return m_lastPerformanceActiveTime; }

	bool InitializePerformance();
	bool ExecuteNextPerformanceStage();

	bool PlayAnimInternal(const PlayPerformanceAnimParams& params);

protected:
	AnimActionWithSelfBlend m_fullBodyAnimAction;
	NavAnimHandoffDesc m_navHandoff;

	Point m_targPosWs;
	StringId64 m_requestedPerformanceId;
	DC::PerformanceTypeMask m_performanceType;
	PlayPerformanceOptions m_performanceOptions;
	AvailablePerformance m_selectedPerformance;
	Locator m_finalApRefLocWs;
	I8 m_currentStage;

	F32 m_exitPhase;
	F32 m_holsterPhase;
	F32 m_unholsterPhase;

	bool m_interruptNavigation		: 1;
	bool m_interruptSkills			: 1;
	bool m_hasHosltered				: 1;
	bool m_hasUnholstered			: 1;
	bool m_holsterSlow				: 1;
	bool m_unholsterSlow			: 1;
	bool m_allowGestures			: 1;
	bool m_allowAdditiveHitReact	: 1;
	DC::HitReactionStateMask m_hitReactionState = DC::kHitReactionStateMaskIdle;

	DC::PerformanceEntry m_currentPerformance;

	TimeFrame m_lastPerformanceStartedTime;
	TimeFrame m_lastPerformanceActiveTime;
};

/// --------------------------------------------------------------------------------------------------------------- ///
AiPerformanceController::AiPerformanceController()
	: m_performanceOptions(PlayPerformanceOptions())
	, m_selectedPerformance(AvailablePerformance())
	, m_performanceType(0)
	, m_currentStage(-1)
	, m_exitPhase(-1.0f)
	, m_interruptNavigation(false)
	, m_interruptSkills(false)
	, m_allowAdditiveHitReact(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiPerformanceController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	ParentClass::Init(pNavChar, pNavControl);

	m_exitPhase = -1.0f;

	m_interruptNavigation = false;
	m_interruptSkills	  = false;
	m_hitReactionState	  = DC::kHitReactionStateMaskIdle;

	m_targPosWs = kInvalidPoint;
	m_performanceOptions = PlayPerformanceOptions();

	m_lastPerformanceStartedTime = TimeFrameNegInfinity();
	m_lastPerformanceActiveTime = TimeFrameNegInfinity();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiPerformanceController::Reset()
{
	m_fullBodyAnimAction.Reset();

	m_exitPhase = -1.0f;

	m_interruptNavigation = false;
	m_interruptSkills	  = false;
	m_hitReactionState = DC::kHitReactionStateMaskIdle;

	m_performanceType = 0;
	m_performanceOptions = PlayPerformanceOptions();
	m_selectedPerformance = AvailablePerformance();
	m_targPosWs = Point(kInvalidPoint);
	m_allowAdditiveHitReact = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiPerformanceController::ConfigureCharacter(Demeanor demeanor,
												 const DC::NpcDemeanorDef* pDemeanorDef,
												 const NdAiAnimationConfig* pAnimConfig)
{
	if (!m_fullBodyAnimAction.IsValid() || m_fullBodyAnimAction.IsDone())
		return;

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	if (m_fullBodyAnimAction.Request(pAnimControl, SID("self"), AnimAction::kFinishOnLoopingAnimEnd))
	{
		if (SelfBlendAction* pPrevSelfBlend = m_fullBodyAnimAction.GetSelfBlendAction())
		{
			const StateChangeRequest::ID changeId = m_fullBodyAnimAction.GetStateChangeRequestId();

			SelfBlendAction::Params params = pPrevSelfBlend->GetParams();

			m_fullBodyAnimAction.ClearSelfBlendParams();
			m_fullBodyAnimAction.ConfigureSelfBlend(pNavChar, params);
		}
	}

	m_navHandoff.SetStateChangeRequestId(m_fullBodyAnimAction.GetStateChangeRequestId(), INVALID_STRING_ID_64);
	pNavChar->ConfigureNavigationHandOff(m_navHandoff, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiPerformanceController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
	RelocatePointer(m_selectedPerformance.m_pDcDef, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiPerformanceController::Interrupt()
{
	if (!IsPerformancePlaying())
		return;

	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct UpdateTrackingParams
{
	const NavCharacter* m_pNavChar = nullptr;
	Locator m_initialFinalApWs = kIdentity;
	const NdLocatableObject* m_pTrackingObj = nullptr;
	float m_exitPhase = 1.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static void UpdatePerformanceTracking(AnimStateInstance* pInstance, NdSubsystemAnimAction* pAction, uintptr_t userData)
{
	if (!pAction || !pAction->IsKindOf(g_type_SelfBlendAction))
		return;

	const UpdateTrackingParams& params = *(const UpdateTrackingParams*)userData;

	SelfBlendAction* pSelfBlend = (SelfBlendAction*)pAction;

	const ArtItemAnim* pPerfAnim = pInstance ? pInstance->GetPhaseAnimArtItem().ToArtItem() : nullptr;
	if (!pPerfAnim)
		return;

	const bool mirror = pInstance->IsFlipped();
	const SkeletonId skelId = params.m_pNavChar->GetSkeletonId();

	const BoundFrame& sourceAp = pSelfBlend->GetSourceApRef();
	Locator natExitWs = kIdentity;
	if (!FindAlignFromApReference(skelId,
								  pPerfAnim,
								  params.m_exitPhase,
								  sourceAp.GetLocatorWs(),
								  pSelfBlend->GetSourceChannelId(),
								  &natExitWs,
								  mirror))
	{
		return;
	}

	const Point targPosWs = params.m_pTrackingObj->GetTranslation();
	const Vector finalDirXzWs = AsUnitVectorXz(targPosWs - natExitWs.Pos(), kUnitZAxis);
	const Quat finalRotWs = QuatFromXZDir(finalDirXzWs);

	const Locator finalAlignWs = Locator(natExitWs.Pos(), finalRotWs);

	Locator finalApRefWs;
	if (!FindApReferenceFromAlign(skelId, pPerfAnim, finalAlignWs, &finalApRefWs, params.m_exitPhase, mirror))
	{
		return;
	}

	BoundFrame destAp = pSelfBlend->GetDestApRef();
	const Locator& parentSpace = destAp.GetParentSpace();
	const Locator desFinalApRefPs = parentSpace.UntransformLocator(finalApRefWs);
	const Locator& startingFinalApRefPs = parentSpace.UntransformLocator(params.m_initialFinalApWs);

	const float phase = pInstance->Phase();
	const float startPhase = 0.2f;

	const float endPhase = Limit(params.m_exitPhase - (2.0f * pPerfAnim->m_pClipData->m_phasePerFrame),
								 startPhase + 0.01f,
								 1.0f);

	const float trackingParam = LerpScaleClamp(startPhase, endPhase, 0.0f, 1.0f, phase);
	const float trackingTT = CalculateCurveValue(trackingParam, DC::kAnimCurveTypeLongToe);

	if (false)
	{
		MsgCon("[%s] Tracking '%s' : %f (%f, %f : %f)\n",
			   params.m_pNavChar->GetName(),
			   params.m_pTrackingObj->GetName(),
			   trackingTT,
			   phase,
			   startPhase,
			   endPhase);
	}

	const Locator updatedFinalApRefPs = Lerp(startingFinalApRefPs, desFinalApRefPs, trackingTT);

	destAp.SetLocatorPs(updatedFinalApRefPs);

	pSelfBlend->UpdateDestApRef(destAp);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiPerformanceController::UpdateStatus()
{
	PROFILE_AUTO(AI);

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	m_fullBodyAnimAction.Update(pAnimControl);

	// Stop interrupting skills when performance is fading out
	if (m_interruptSkills && !m_fullBodyAnimAction.IsTopInstance(pAnimControl))
	{
		m_interruptSkills = false;
	}

	// holster / unholster
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		const F32 animPhase = m_fullBodyAnimAction.GetAnimPhase(pAnimControl);

		if (!m_hasHosltered && m_holsterPhase >= 0.0f && animPhase >= m_holsterPhase)
		{
			pNavChar->RequestGunState(kGunStateHolstered, m_holsterSlow);
			m_hasHosltered = true;
		}
		if (!m_hasUnholstered && m_unholsterPhase >= 0.0f && animPhase >= m_unholsterPhase)
		{
			if (Npc* pNpc = Npc::FromProcess(pNavChar))
			{
				const AiEntity* pTargetEntity = pNpc->GetCurrentTargetEntity();
				if (pTargetEntity && pTargetEntity->IsCertain())
				{
					pNpc->GetAnimationControllers()->GetWeaponController()->RequestWeaponUp();
				}
			}

			pNavChar->RequestGunState(kGunStateOut, m_unholsterSlow);
			m_hasUnholstered = true;
		}

		if (AnimStateInstance* pDestInstance = m_fullBodyAnimAction.GetTransitionDestInstance(pAnimControl))
		{
			DC::AnimStateFlag& stateFlags = pDestInstance->GetMutableStateFlags();
			stateFlags |= m_performanceOptions.m_additionalAnimStateFlags;
		}

		m_lastPerformanceActiveTime = pNavChar->GetCurTime();

		if (const NdLocatableObject* pTrackingObj = m_performanceOptions.m_hTrackGo.ToProcess())
		{
			NdSubsystemMgr* pSubSysMgr = pNavChar->GetSubsystemMgr();

			if (AnimStateInstance* pAnimInstance = m_fullBodyAnimAction.GetTransitionDestInstance(pAnimControl))
			{
				UpdateTrackingParams params;
				params.m_pNavChar = pNavChar;
				params.m_pTrackingObj = pTrackingObj;
				params.m_exitPhase = m_exitPhase;
				params.m_initialFinalApWs = m_selectedPerformance.m_initApRefLocWs;

				pSubSysMgr->ForEachInstanceAction(pAnimInstance, UpdatePerformanceTracking, (uintptr_t)&params);
			}
		}
	}

	if (IsBusy())
	{
		// reset due to hit reaction
		if (const IAiHitController* pHitController = pNavChar->GetAnimationControllers()->GetHitController())
		{
			const bool hitReactionPlaying = m_allowAdditiveHitReact ? pHitController->IsFullBodyReactionPlaying()
																	: pHitController->IsHitReactionPlaying();

			if (hitReactionPlaying)
			{
				Reset();
			}
		}
	}
	else if (m_selectedPerformance.m_pDcDef && m_currentStage < m_selectedPerformance.m_pDcDef->m_animIds->m_count)
	{
		ExecuteNextPerformanceStage();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiPerformanceController::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiPerformanceController::IsBusy() const
{
	bool actionBusy = m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone();
	if (actionBusy && m_exitPhase >= 0.0f)
	{
		const F32 animPhase = m_fullBodyAnimAction.GetAnimPhase(GetCharacter()->GetAnimControl());
		actionBusy = (animPhase >= 0.0f) && (animPhase <= m_exitPhase);
	}

	return actionBusy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiPerformanceController::ShouldInterruptNavigation() const
{
	if (IsBusy())
	{
		return m_interruptNavigation;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiPerformanceController::ShouldInterruptSkills() const
{
	if (IsBusy())
	{
		return m_interruptSkills;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 AiPerformanceController::CollectHitReactionStateFlags() const
{
	DC::HitReactionStateMask state = 0;

	if (IsBusy())
	{
		state = m_hitReactionState;
	}

	return state;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiPerformanceController::IsPerformancePlaying() const
{
	if (!m_fullBodyAnimAction.IsValid())
	{
		return false;
	}

	if (m_fullBodyAnimAction.IsDone())
	{
		if (!m_selectedPerformance.m_pDcDef)
			return false;

		if (m_currentStage >= m_selectedPerformance.m_pDcDef->m_animIds->m_count)
			return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// A directional performance
bool AiPerformanceController::PlayPerformance(Point_arg targPosWs,
											  DC::PerformanceTypeMask performanceType,
											  const PlayPerformanceOptions& options)
{
	m_targPosWs = targPosWs;
	m_performanceType = performanceType;
	m_performanceOptions = options;
	m_currentStage = 0;

	m_holsterPhase = -1.0f;
	m_unholsterPhase = -1.0f;
	m_hasHosltered = false;
	m_hasUnholstered = false;
	m_holsterSlow = false;
	m_unholsterSlow = false;

	if (!InitializePerformance())
		return false;

	return ExecuteNextPerformanceStage();
}

/// --------------------------------------------------------------------------------------------------------------- ///
// A directional performance
bool AiPerformanceController::InitializePerformance()
{
	static const DebugPrimTime kDebugDuration = Seconds(3.0f);
	static const F32 kDefaultBlendTime        = 0.2f;

	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
	{
		return false;
	}

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const BoundFrame charBf		 = pNavChar->GetBoundFrame();
	const NavLocation charNavLoc = pNavChar->GetNavLocation();
	const NavMesh* pCharMesh	 = charNavLoc.ToNavMesh();
	const Point charPosWs		 = charNavLoc.GetPosWs();
	const Locator charLocWs		 = pNavChar->GetLocator();
	const Vector charDirWs		 = GetLocalZ(charLocWs);
	const Vector charDirXzWs	 = SafeNormalize(VectorXz(charDirWs), kUnitZAxis);
	const Vector toTargXzDirWs	 = SafeNormalize(VectorXz(m_targPosWs - charPosWs), kUnitZAxis);
	const F32 desiredDot		 = Dot(charDirXzWs, toTargXzDirWs);
	const F32 desiredAngleRad	 = Sign(Cross(charDirXzWs, toTargXzDirWs).Y()) * SafeAcos(desiredDot);
	const F32 desiredAngleDeg	 = RadiansToDegrees(desiredAngleRad);
	const F32 targetDist		 = Dist(m_targPosWs, charPosWs);
	const DC::PerformanceDirectionMask desiredDirBit = m_performanceOptions.m_nondirectional ? DC::kPerformanceDirectionMaskNondirectional : GetPerformanceDirFromAngleDeg(desiredAngleDeg);

	if (m_performanceOptions.m_apRefValid)
	{
		m_finalApRefLocWs = m_performanceOptions.m_apRefWs;
	}
	else
	{
		m_finalApRefLocWs = Locator(charPosWs, QuatFromXZDir(toTargXzDirWs));
	}

	const Vector eyeVecLs = charLocWs.UntransformVector(pNavChar->GetEyeWs().GetPosition() - charPosWs);

	const Npc* pNpc = Npc::FromProcess(pNavChar);

	StringId64 performanceSetId = INVALID_STRING_ID_64;
	if (pNpc)
	{
		const DC::WeaponArtDef* pWeaponArtDef = ProcessWeaponBase::GetWeaponArtDef(pNpc->GetWeaponId());
		const DC::WeaponAnimType weaponAnimType = pWeaponArtDef ? pWeaponArtDef->m_animType : DC::kWeaponAnimTypeNoWeapon;

		const Archetype& archetype = pNpc->GetArchetype();

		const StringId64 performanceTableId = archetype.GetPerformanceTableId();
		const DC::AiPerformanceTable* pPerformanceTable = ScriptManager::Lookup<DC::AiPerformanceTable>(performanceTableId, nullptr);
		if (pPerformanceTable)
		{
			AI_ASSERT(weaponAnimType >= 0 && weaponAnimType < pPerformanceTable->m_count);
			performanceSetId = pPerformanceTable->m_array[weaponAnimType];
		}
	}

	const DC::PerformanceEntryList* pPerformanceList = ScriptManager::Lookup<DC::PerformanceEntryList>(performanceSetId, nullptr);
	if (!pPerformanceList)
	{
		char errBuffer[128];
		sprintf(errBuffer, "Performance set not found: \"%s\"", DevKitOnly_StringIdToString(performanceSetId));
		g_prim.Draw(DebugString(charPosWs + Vector(0.0f, 1.0f, 0.0f), errBuffer, kColorRed, 0.5f), Seconds(3.0f));
		return false;
	}

	const bool shouldPoint = m_performanceOptions.m_pointing;

	F32 maxExitPhase = 1.0f;

	if (pNpc)
	{
		if (m_performanceType == DC::kPerformanceTypeMaskSurpriseMild)
		{
			const Skill* pSkill = pNpc->GetActiveSkill();
			if (!pSkill || pSkill->GetNameId() != SID("InvestigateSkill"))
			{
				maxExitPhase = 0.5f;
			}
		}
	}

	DC::PerformanceDemeanorMask demeanorBit = DC::kPerformanceDemeanorMaskAny;
	DC::PerformanceMotionMask motionBit     = DC::kPerformanceMotionMaskAny;

	switch (pNavChar->GetCurrentDemeanor().ToI32())
	{
	case kDemeanorAmbient:		demeanorBit = DC::kPerformanceDemeanorMaskAmbient;		break;
	case kDemeanorUneasy:		demeanorBit = DC::kPerformanceDemeanorMaskUneasy;		break;
	case kDemeanorAggressive:	demeanorBit = DC::kPerformanceDemeanorMaskAggressive;	break;
	case kDemeanorCrouch:		demeanorBit = DC::kPerformanceDemeanorMaskCrouch;		break;
	case kDemeanorCrawl:		demeanorBit = DC::kPerformanceDemeanorMaskCrawl;		break;
	case kDemeanorSearch:		demeanorBit = DC::kPerformanceDemeanorMaskSearch;		break;
	default:
		AiLogAnim(pNavChar,
				  "AiPerformanceController::PlayPerformance Unexpected demeanor '%s'\n",
				  pNavChar->GetDemeanorName(pNavChar->GetCurrentDemeanor()));
	}

	switch (pNavChar->GetCurrentMotionType())
	{
	case kMotionTypeWalk:		motionBit = DC::kPerformanceMotionMaskWalk;	break;
	case kMotionTypeRun:		motionBit = DC::kPerformanceMotionMaskRun;	break;
	case kMotionTypeSprint:		motionBit = DC::kPerformanceMotionMaskRun;	break;
	case kMotionTypeMax:		motionBit = DC::kPerformanceMotionMaskIdle;	break;
	}

	const DC::PerformanceTypeMask typeBits = m_performanceType;

	const DC::PerformanceWeaponMask weaponBit = pNavChar->GetCurrentGunState() == kGunStateHolstered ?
												DC::kPerformanceWeaponMaskHolstered                  :
												DC::kPerformanceWeaponMaskUnholstered;

	const DC::PerformancePointingMask pointingBit = shouldPoint ? DC::kPerformancePointingMaskYes : DC::kPerformancePointingMaskNo;

	const bool swimming = pNavChar->IsSwimming();

	// Collect performance anims currently being played
	StringId64 aCurrentPerformanceAnimId[kMaxNpcCount];
	U32F numNpcsInPerformance = 0;
	AtomicLockJanitorRead_Jls npcGroupLock(EngineComponents::GetNpcManager()->GetNpcGroupLock(), FILE_LINE_FUNC);

	for (NpcHandle hNpc : EngineComponents::GetNpcManager()->GetNpcs())
	{
		const NavCharacter* pOtherChar = hNpc.ToProcess();
		if (!pOtherChar || pOtherChar->IsDead() || pNavChar == pOtherChar)
			continue;

		if (!pOtherChar->GetAnimationControllers()->GetPerformanceController()->IsPerformancePlaying())
			continue;

		const AnimControl* pOtherAnimControl = pOtherChar->GetAnimControl();
		if (!pOtherAnimControl)
			continue;

		const AnimStateLayer* pBaseLayer = pOtherAnimControl->GetStateLayerById(SID("base"));
		if (!pBaseLayer)
			continue;

		const AnimStateInstance* pBaseInstance = pBaseLayer->CurrentStateInstance();
		if (!pBaseInstance)
			continue;

		const StringId64 otherPerformanceAnimId = pBaseInstance->GetPhaseAnim();
		aCurrentPerformanceAnimId[numNpcsInPerformance++] = otherPerformanceAnimId;
	}

	const AnimControl* pAnimControl = pNavChar->GetAnimControl();

	// Collect valid performances
	ScopedTempAllocator alloc(FILE_LINE_FUNC);
	AvailablePerformance* aAvailablePerformance = NDI_NEW AvailablePerformance[pPerformanceList->m_count];
	AI_ASSERT(aAvailablePerformance);

	U32F numAvailablePerformances = 0;

	// for direction coverage check
	DC::PerformanceDirectionMask coveredDirMask = 0;

	for (U32F iPerformance = 0; iPerformance < pPerformanceList->m_count; ++iPerformance)
	{
		const DC::PerformanceEntry& performance = pPerformanceList->m_array[iPerformance];

		// Not too dark (readable) and not too bright (not colliding with pre-set colors)
		const Color performanceDebugColor = Color((urand() | 0xFF808080) & 0xFFCFCFCF);

		const bool visualizePerformance = FALSE_IN_FINAL_BUILD(
			g_aiGameOptions.m_performance.m_visualize																						 &&
			(g_aiGameOptions.m_performance.m_visualizeStartIndex < 0 || iPerformance >= g_aiGameOptions.m_performance.m_visualizeStartIndex) &&
			(g_aiGameOptions.m_performance.m_visualizeEndIndex < 0 || iPerformance <= g_aiGameOptions.m_performance.m_visualizeEndIndex)	 &&
			DebugSelection::Get().IsProcessOrNoneSelected(pNavChar));

		const bool demeanorValid =
			(performance.m_demeanor & demeanorBit)
			|| (performance.m_demeanor & DC::kPerformanceDemeanorMaskAny)
			|| (demeanorBit & DC::kPerformanceDemeanorMaskAny)
			|| m_performanceOptions.m_ignoreDemeanor;
		if (!demeanorValid && !visualizePerformance)
			continue;

		const bool typeValid =
			((performance.m_type & kStrictTypeMatchMask) == (typeBits & kStrictTypeMatchMask))
			&& (((performance.m_type & typeBits) == typeBits)
				|| (performance.m_type & DC::kPerformanceTypeMaskAny)
				|| (typeBits & DC::kPerformanceTypeMaskAny));
		if (!typeValid && !visualizePerformance)
			continue;

		const bool motionValid =
			(performance.m_motion & motionBit)
			|| (performance.m_motion & DC::kPerformanceMotionMaskAny)
			|| (motionBit & DC::kPerformanceMotionMaskAny)
			|| m_performanceOptions.m_ignoreMotionType;
		if (!motionValid && !visualizePerformance)
			continue;

		const bool weaponValid =
			(performance.m_weapon & weaponBit)
			|| (performance.m_weapon & DC::kPerformanceWeaponMaskAny)
			|| (weaponBit & DC::kPerformanceWeaponMaskAny);
		if (!weaponValid && !visualizePerformance)
			continue;

		const bool pointingValid = (performance.m_pointing & DC::kPerformancePointingMaskAny) || (performance.m_pointing & pointingBit);
		if (!pointingValid && !visualizePerformance)
			continue;

		const bool swimmingValid = (performance.m_swimming == swimming);
		if (!swimmingValid && !visualizePerformance)
			continue;

		const bool inPlaceValid = m_performanceOptions.m_requireInPlace ? performance.m_inPlace : true;
		if (!inPlaceValid && !visualizePerformance)
			continue;

		const bool distanceValid =
			(performance.m_distanceMin < 0.0f || targetDist >= performance.m_distanceMin)
			&& (performance.m_distanceMax < 0.0f || targetDist <= performance.m_distanceMax);
		if (!distanceValid && !visualizePerformance)
			continue;

		const StringId64 animId  = pAnimControl->LookupAnimId(performance.m_animIds->m_array[m_currentStage]);
		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();
		const SkeletonId skelId  = pNavChar->GetSkeletonId();

		const bool animValid = pAnim;
		if (!animValid && !visualizePerformance)
			continue;

		const F32 animDuration = pAnim ? GetDuration(pAnim) : -1.0f;
		const F32 exitPhaseMax = Min(maxExitPhase, animDuration > 0.0f ? Limit01((animDuration - kExitAnimTrimTime) / animDuration) : 1.0f);
		const F32 exitPhase    = performance.m_exitPhase;

		const bool nondirectional = performance.m_direction & DC::kPerformanceDirectionMaskNondirectional;
		const bool mirror = performance.m_mirror;

		// apReference is expected to point in the desired direction
		Locator initApRefLocWs = charLocWs;
		if (!nondirectional)
		{
			Locator initialAnimApWs;
			if (FindApReferenceFromAlign(skelId, pAnim, charLocWs, &initialAnimApWs, 0.0f, mirror))
			{
				//g_prim.Draw(DebugCoordAxesLabeled(charLocWs, "charLocWs", 1.0f, kPrimEnableHiddenLineAlpha, 1.0f), Seconds(3.0f));
				//g_prim.Draw(DebugCoordAxes(initialAnimApWs, 0.5f, kPrimEnableHiddenLineAlpha, 3.0f), Seconds(3.0f));
				//g_prim.Draw(DebugString(initialAnimApWs.Pos() + GetLocalZ(initialAnimApWs) * 0.5f, pAnim->GetName(), kColorWhite, 0.5f), Seconds(3.0f));

				initApRefLocWs = initialAnimApWs;
			}
			else if (m_performanceOptions.m_apRefValid)
			{
				initApRefLocWs = m_performanceOptions.m_apRefWs;
			}
		}

		// align at the start of the anim is the current direction
		Locator animInitAlignLocLs;
		if (!pAnim || nondirectional || !FindAlignFromApReference(skelId, pAnim, 0.0f, kIdentity, SID("apReference"), &animInitAlignLocLs, mirror))
		{
			animInitAlignLocLs = kIdentity;
		}

		Locator animEndAlignLocLs;
		if (!pAnim || nondirectional || !FindAlignFromApReference(skelId, pAnim, 1.0f, kIdentity, SID("apReference"), &animEndAlignLocLs, mirror))
		{
			animEndAlignLocLs = kIdentity;
		}

		Locator animEndAlignLocWs = m_finalApRefLocWs.TransformLocator(animEndAlignLocLs);
		if (nondirectional)
			FindAlignFromApReference(pAnimControl, animId, 1.0f, charLocWs, &animEndAlignLocWs, mirror);

		const Point animEndPosWs = animEndAlignLocWs.GetTranslation();

		coveredDirMask |= performance.m_direction;

		bool dirValid = (performance.m_direction & DC::kPerformanceDirectionMaskAny) || (performance.m_direction & desiredDirBit);
		if (!dirValid && m_performanceOptions.m_allowNextQuadrantDir)
		{
			DC::PerformanceDirectionMask oppositeDir = DC::kPerformanceDirectionMaskFront;
			switch (desiredDirBit)
			{
				case DC::kPerformanceDirectionMaskFront:		oppositeDir = DC::kPerformanceDirectionMaskBack;		break;
				case DC::kPerformanceDirectionMaskBack:			oppositeDir = DC::kPerformanceDirectionMaskFront;		break;
				case DC::kPerformanceDirectionMaskLeft:			oppositeDir = DC::kPerformanceDirectionMaskRight;		break;
				case DC::kPerformanceDirectionMaskRight:		oppositeDir = DC::kPerformanceDirectionMaskLeft;		break;
			}
			dirValid = (performance.m_direction != oppositeDir);
		}
		if (!dirValid && !visualizePerformance)
			continue;

		VisualizeLosInfo losInfo;
		bool losValid = true;
		if (m_performanceOptions.m_requireLos
			&& !performance.m_inPlace) // in-place performances don't need to check for LOS so we'd always have something to play (like old times!)
		{
			const Point animEndEyePosWs = animEndPosWs + animEndAlignLocWs.TransformVector(eyeVecLs);

			//g_prim.Draw(DebugCoordAxes(animEndAlignLocWs), kPrimDuration1FramePauseable);
			//g_prim.Draw(DebugCross(animEndPosWs, 0.5f, kColorOrange), kPrimDuration1FramePauseable);

			const Vector animEndEyeToTarget = m_targPosWs - animEndEyePosWs;
			const Vector animEndToTargetDirWs = SafeNormalize(animEndEyeToTarget, kUnitZAxis);
			const F32 animEndEyeToTargetDist = Length(animEndEyeToTarget);

			losInfo.m_probeStart = animEndEyePosWs;
			losInfo.m_probeEnd = animEndEyePosWs + Max(0.0f, animEndEyeToTargetDist - 0.1f) * animEndToTargetDirWs;
			RayCastContact contact;
			const Collide::LayerMask layerMask = Collide::kLayerMaskGeneral & ~(Collide::kLayerMaskNpc | Collide::kLayerMaskPlayer);
			losInfo.m_clear = !HavokRayCastJob::Probe(losInfo.m_probeStart, losInfo.m_probeEnd, 1, &contact, CollideFilter(layerMask, Pat(Pat::kSeeThroughMask | Pat::kStealthVegetationMask)));

			if (!losInfo.m_clear)
			{
				losInfo.m_probeHit = Lerp(losInfo.m_probeStart, losInfo.m_probeEnd, contact.m_t);

				// good enough with required LOS clearance distance?
				if (m_performanceOptions.m_minLosClearDist > 0.0f)
				{
					const F32 losClearDist = contact.m_t * Dist(losInfo.m_probeStart, losInfo.m_probeEnd);
					if (losClearDist >= m_performanceOptions.m_minLosClearDist)
					{
						losInfo.m_enoughClearDist = true;
						losInfo.m_clearDist = losClearDist;
					}
				}
			}

			losInfo.m_tested = true;

			losValid = losInfo.m_clear || losInfo.m_enoughClearDist;
		}
		if (!losValid && !visualizePerformance)
			continue;

		const bool allMasksValid =
			demeanorValid
			&& typeValid
			&& dirValid
			&& motionValid
			&& weaponValid
			&& pointingValid
			&& swimmingValid
			&& inPlaceValid
			&& distanceValid; // DC masks
		const bool allFiltersValid = allMasksValid && animValid && losValid; // we want to always visualize filters not part of DC masks so they are separated from allMasksValid

		NavMesh::ProbeParams probeParams;
		probeParams.m_dynamicProbe = true;
		probeParams.m_obeyedBlockers = pNavChar->GetNavControl()->BuildObeyedBlockerList(true);
		probeParams.m_probeRadius = pNavChar->GetMaximumNavAdjustRadius();

		if (visualizePerformance)
		{
			if (!g_aiGameOptions.m_performance.m_visualizeLimitToMatchingMasks || allMasksValid)
			{
				bool isUnique = true;

				for (U32F ii = 0; ii < numNpcsInPerformance; ++ii)
				{
					if (aCurrentPerformanceAnimId[ii] == animId)
					{
						isUnique = false;
						break;
					}
				}

				TextPrinterParentSpace performancePrinter = TextPrinterParentSpace(charBf, kDebugDuration);
				VisualizePerformance(performance,
									 iPerformance,
									 m_finalApRefLocWs,
									 pCharMesh,
									 probeParams,
									 demeanorValid,
									 typeValid,
									 motionValid,
									 weaponValid,
									 pointingValid,
									 swimmingValid,
									 inPlaceValid,
									 dirValid,
									 isUnique,
									 distanceValid, performance.m_distanceMin, performance.m_distanceMax,
									 m_performanceOptions.m_requireLos ? &losInfo : nullptr,
									 performancePrinter,
									 performanceDebugColor,
									 kDebugDuration);
			}
		}

		// We got here because (visualizePerformance == true) but not all criteria are valid, must continue here
		if (!allFiltersValid)
			continue;

		DC::SelfBlendParams selfBlend;
		if (const DC::SelfBlendParams* pDcSb = NavUtil::GetSelfBlendParamsForAnim(pAnim->GetNameId()))
		{
			selfBlend = *pDcSb;
		}
		else
		{
			selfBlend.m_curve = DC::kAnimCurveTypeEaseIn;
			selfBlend.m_phase = 0.0f;
			selfBlend.m_time  = GetDuration(pAnim);
		}

		/*
		if (!options.m_ignoreMotionClearance && !AI::AnimHasClearMotion(skelId, pAnim, initApRefLocWs, finalApRefLocWs, &selfBlend, pCharMesh, probeParams))
			continue;
		*/

		if (!m_performanceOptions.m_ignoreMotionClearance
			&& !performance.m_inPlace
			&& !PerformanceHasClearMotion(pNavChar, performance, initApRefLocWs, false))
			continue;

		AvailablePerformance& availablePerformance = aAvailablePerformance[numAvailablePerformances++];

		availablePerformance.m_pAnim = pAnim;
		availablePerformance.m_initApRefLocWs = initApRefLocWs;
		availablePerformance.m_selfBlend	  = selfBlend;
		availablePerformance.m_pDcDef		  = &performance;
	}

	// direction coverage check
	if (FALSE_IN_FINAL_BUILD(!(coveredDirMask & DC::kPerformanceDirectionMaskAny)))
	{
		// only case about surprises
		const bool isSurprise =
			(typeBits &
				(DC::kPerformanceTypeMaskSurpriseFast
				| DC::kPerformanceTypeMaskSurpriseMild
				| DC::kPerformanceTypeMaskSurpriseStrong));

		// don't care about crawl demeanor
		const bool ignoredDemeanor = (demeanorBit == DC::kPerformanceDemeanorMaskCrawl);

		if (isSurprise && !ignoredDemeanor)
		{
			const DC::PerformanceDirectionMask uncoveredDirMask = ~coveredDirMask & (DC::kPerformanceDirectionMaskFront | DC::kPerformanceDirectionMaskBack | DC::kPerformanceDirectionMaskLeft | DC::kPerformanceDirectionMaskRight);
			if (uncoveredDirMask)
			{
				StringBuilder<256> errSb;
				errSb.append_format("WARNING - Incomplete directional coverage\n");

				errSb.append_format("Missing: ");
				DC::GetPerformanceDirectionMaskString(uncoveredDirMask, &errSb);
				errSb.append_format("\n");

				errSb.append_format("Filters: type     - ");
				DC::GetPerformanceTypeMaskString(m_performanceType, &errSb);
				errSb.append_format("\n");

				errSb.append_format("         demeanor - ");
				DC::GetPerformanceDemeanorMaskString(demeanorBit, &errSb);
				errSb.append_format("\n");

				errSb.append_format("         motion   - ");
				DC::GetPerformanceMotionMaskString(motionBit, &errSb);
				errSb.append_format("\n");

				errSb.append_format("         pointing - ");
				DC::GetPerformancePointingMaskString(pointingBit, &errSb);
				errSb.append_format("\n");

				errSb.append_format("         weapon   - ");
				DC::GetPerformanceWeaponMaskString(weaponBit, &errSb);
				errSb.append_format("\n");

				errSb.append_format("         distance - %.2f", targetDist);

				g_prim.Draw(DebugString(pNpc->GetTranslation() + Vector(0.0f, 1.0f, 0.0f), errSb.c_str(), kColorRed, 0.5f), Seconds(3.0f));
			}
		}
	}

	// Gather performance anims not currently being played
	AvailablePerformance* aAvailableUniquePerformances = NDI_NEW AvailablePerformance[pPerformanceList->m_count];
	U32F numAvailableUniquePerformances = 0;

	for (U32F ii = 0; ii < numAvailablePerformances; ++ii)
	{
		const AvailablePerformance& availablePerformance = aAvailablePerformance[ii];
		bool isUnique = true;

		for (U32F jj = 0; jj < numNpcsInPerformance; ++jj)
		{
			if (aCurrentPerformanceAnimId[jj] == availablePerformance.m_pAnim->GetNameId())
			{
				isUnique = false;
				break;
			}
		}

		if (isUnique)
		{
			aAvailableUniquePerformances[numAvailableUniquePerformances++] = availablePerformance;
		}
	}

	const AvailablePerformance* aPerformanceSelectionPool = numAvailableUniquePerformances > 0 ? aAvailableUniquePerformances : aAvailablePerformance;
	const U32F performanceSelectionPoolSize               = numAvailableUniquePerformances > 0 ? numAvailableUniquePerformances : numAvailablePerformances;
	const U32F iSelectedPerformance                       = performanceSelectionPoolSize > 0 ? RandomIntRange(0, performanceSelectionPoolSize - 1) : 0;

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_performance.m_visualize && DebugSelection::Get().IsProcessOrNoneSelected(pNavChar)))
	{
		const Color charDebugColor = AI::IndexToColor(pNavChar->GetUserId().GetValue());

		TextPrinterParentSpace debugPrinter = TextPrinterParentSpace(charBf, kDebugDuration);
		DebugPerformanceFilter(m_targPosWs,
							   pNavChar->GetCurrentDemeanor().ToI32(),
							   m_performanceType,
							   pNavChar->GetCurrentMotionType(),
							   weaponBit == DC::kPerformanceWeaponMaskHolstered,
							   shouldPoint,
							   targetDist,
							   desiredDirBit,
							   debugPrinter,
							   charDebugColor,
							   kDebugDuration,
							   m_performanceOptions);
		if (performanceSelectionPoolSize > 0)
		{
			debugPrinter.PrintF(kColorGreen, " - Selected  %s\n", DevKitOnly_StringIdToString(aPerformanceSelectionPool[iSelectedPerformance].m_pAnim->GetNameId()));
		}
		else
		{
			debugPrinter.PrintF(kColorRed, " - Selected  None\n");
		}
	}

	if (performanceSelectionPoolSize == 0)
		return false;

	m_selectedPerformance = aPerformanceSelectionPool[iSelectedPerformance];
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiPerformanceController::ExecuteNextPerformanceStage()
{
	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
	{
		return false;
	}

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const BoundFrame charBf = pNavChar->GetBoundFrame();
	const NavLocation charNavLoc = pNavChar->GetNavLocation();
	const Point charPosWs = charNavLoc.GetPosWs();

	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const StringId64 currAnimId = pAnimControl->LookupAnimId(m_selectedPerformance.m_pDcDef->m_animIds->m_array[m_currentStage]);
	const ArtItemAnim* pCurrAnim = pAnimControl->LookupAnim(currAnimId).ToArtItem();
	if (!pCurrAnim)
		return false;

	StringId64 nextAnim = INVALID_STRING_ID_64;
	I8 nextStage = m_currentStage + 1;
	if (nextStage < m_selectedPerformance.m_pDcDef->m_animIds->m_count)
		nextAnim = m_selectedPerformance.m_pDcDef->m_animIds->m_array[nextStage];

	Locator endApRef = m_finalApRefLocWs;

	const bool nondirectional = m_selectedPerformance.m_pDcDef->m_direction & DC::kPerformanceDirectionMaskNondirectional;
	const bool noSelfBlend = m_selectedPerformance.m_pDcDef->m_noSelfBlend;

	if (nextAnim != INVALID_STRING_ID_64 && !m_performanceOptions.m_apRefValid && !noSelfBlend)
	{
		const StringId64 nextAnimId = pAnimControl->LookupAnimId(nextAnim);
		const ArtItemAnim* pNextAnim = pAnimControl->LookupAnim(nextAnimId).ToArtItem();
		const SkeletonId skelId = pNavChar->GetSkeletonId();

		const bool animValid = pNextAnim;
		if (nondirectional)
		{
			endApRef = pNavChar->GetLocator();
		}
		else if (animValid)
		{
			if (m_selectedPerformance.m_pDcDef->m_rotationOnlySelfBlend)
			{
				Locator curApReference;
				// apReference is expected to point in the desired direction
				FindApReferenceFromAlign(skelId, pNextAnim, pNavChar->GetLocator(), &curApReference, 0.0f);

				Locator endAlign;
				FindAlignFromApReference(skelId, pNextAnim, 1.0f, curApReference, SID("apReference"), &endAlign);

				// point the align toward the target
				Vector dir = VectorXz(m_targPosWs - endAlign.GetTranslation());
				dir = SafeNormalize(dir, kUnitZAxis);
				endAlign.SetRotation(QuatFromLookAt(dir, kUnitYAxis));

				// now create the final ap from the final align
				FindApReferenceFromAlign(skelId, pNextAnim, endAlign, &endApRef, 1.0f);
			}
			else
			{
				// apReference is expected to point in the desired direction
				FindApReferenceFromAlign(skelId, pNextAnim, pNavChar->GetLocator(), &endApRef, 0.0f);
			}
		}
	}

	BoundFrame initApRef = charBf;
	initApRef.SetLocatorWs(m_selectedPerformance.m_initApRefLocWs);
	BoundFrame finalApRef = charBf;
	finalApRef.SetLocatorWs(endApRef);

	PlayPerformanceAnimParams params;
	params.m_animId = currAnimId;
	params.m_frame = initApRef;
	params.m_pDcDef = m_selectedPerformance.m_pDcDef;
	params.m_gestureId = INVALID_STRING_ID_64;
	params.m_customApRefId = INVALID_STRING_ID_64;
	params.m_mirror = m_selectedPerformance.m_pDcDef->m_mirror;
	params.m_pNavHandoff = m_selectedPerformance.m_pDcDef->m_navAnimHandoff;

	if (!PlayAnimInternal(params))
	{
		char errBuffer[128];
		sprintf(errBuffer, "Failed to play performance anim: \"%s\"", DevKitOnly_StringIdToString(params.m_animId));
		g_prim.Draw(DebugString(charPosWs + Vector(0.0f, 1.0f, 0.0f), errBuffer, kColorRed, 0.5f), Seconds(3.0f));
		return false;
	}

	if (m_selectedPerformance.m_pDcDef->m_exitDemeanor < DC::kNpcDemeanorCount)
	{
		pNavChar->ForceDemeanor(DemeanorFromDcDemeanor(m_selectedPerformance.m_pDcDef->m_exitDemeanor), AI_LOG);
	}

	m_interruptNavigation = true;
	m_interruptSkills = false;
	m_hitReactionState = m_performanceOptions.m_hitReactionState;

	if (!noSelfBlend)
	{
		m_fullBodyAnimAction.SetSelfBlendParams(&m_selectedPerformance.m_selfBlend,
												finalApRef,
												pNavChar,
												m_selectedPerformance.m_pDcDef->m_exitPhase);
	}

	m_currentPerformance = *m_selectedPerformance.m_pDcDef;

	m_currentStage++;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
DC::PerformanceDirectionMask AiPerformanceController::GetPerformanceDirFromAngleDeg(F32 angleDeg)
{
	DC::PerformanceDirectionMask dir = DC::kPerformanceDirectionMaskFront;

	if (angleDeg > 45.0f && angleDeg < 135.0f)
	{
		dir = DC::kPerformanceDirectionMaskLeft;
	}
	else if (angleDeg < -45.0f && angleDeg > -135.0f)
	{
		dir = DC::kPerformanceDirectionMaskRight;
	}
	else if (angleDeg >= 135.0f || angleDeg <= -135.0f)
	{
		dir = DC::kPerformanceDirectionMaskBack;
	}

	return dir;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
const char* AiPerformanceController::GetPerformanceDirString(DC::PerformanceDirectionMask dir)
{
	STRIP_IN_FINAL_BUILD_VALUE("null");

	switch (dir)
	{
	case DC::kPerformanceDirectionMaskFront:	return "Front";
	case DC::kPerformanceDirectionMaskBack:		return "Back";
	case DC::kPerformanceDirectionMaskLeft:		return "Left";
	case DC::kPerformanceDirectionMaskRight:	return "Right";
	case DC::kPerformanceDirectionMaskAny:		return "Any";
	}

	return "<Unknown>";
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiPerformanceController::DebugPerformanceFilter(Point performancePos,
													 const I32F iDemeanor,
													 DC::PerformanceTypeMask performanceType,
													 MotionType motionType,
													 bool weaponHolstered,
													 bool shouldPoint,
													 F32 distance,
													 DC::PerformanceDirectionMask dir,
													 TextPrinterParentSpace& printer,
													 Color color,
													 DebugPrimTime duration,
													 const PlayPerformanceOptions& options) const
{
	STRIP_IN_FINAL_BUILD;

	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
	{
		return;
	}

	const BoundFrame charBf          = pNavChar->GetBoundFrame();
	const Point charPos              = pNavChar->GetTranslation();
	const F32 eyeHeight              = pNavChar->GetEyeWs().GetPosition().Y() - charPos.Y();
	const Point offsetCharPos        = charPos + eyeHeight * Vector(kUnitYAxis);
	const Point offsetPerformancePos = performancePos;
	const Point textPos              = charPos + RandomFloatRange(3.0f, 4.0f) * Vector(kUnitYAxis);

	g_prim.Draw(DebugArrow(offsetCharPos, offsetPerformancePos, color), duration);
	g_prim.Draw(DebugLine(charPos + 1.0f * Vector(kUnitYAxis), textPos, color), duration, &charBf);

	printer.Start(textPos);
	printer.SetTextScale(0.7f);

	printer.PrintF(color, "<Performance Filter>\n");

	printer.PrintF(color, " - Demeanor      ");
	switch (pNavChar->GetCurrentDemeanor().ToI32())
	{
	case kDemeanorAmbient:		printer.PrintF(color, "Ambient");		break;
	case kDemeanorUneasy:		printer.PrintF(color, "Uneasy");		break;
	case kDemeanorAggressive:	printer.PrintF(color, "Aggressive");	break;
	case kDemeanorSearch:		printer.PrintF(color, "Search");		break;
	}
	printer.PrintF(color, "\n");

	StringBuilder<256> performanceTypeSb;
	DC::GetPerformanceTypeMaskString(performanceType, &performanceTypeSb, " ");
	printer.PrintF(color, " - Type          ");
	printer.PrintF(color, performanceTypeSb.c_str());
	printer.PrintF(color, "\n");

	printer.PrintF(color, " - Motion        ");
	switch (motionType)
	{
	case kMotionTypeWalk:		printer.PrintF(color, "Walk");		break;
	case kMotionTypeRun:		printer.PrintF(color, "Run");		break;
	case kMotionTypeSprint:		printer.PrintF(color, "Sprint");	break;
	case kMotionTypeMax:		printer.PrintF(color, "Max");		break;
	default:					printer.PrintF(color, "Unknown");	break;
	}
	printer.PrintF(color, "\n");

	printer.PrintF(color, " - Weapon        %s\n", weaponHolstered ? "Holstered" : "Unholstered");
	printer.PrintF(color, " - Pointing      %s\n", shouldPoint ? "Yes" : "No");
	printer.PrintF(color, " - Swimming      %s\n", pNavChar->IsSwimming() ? "Yes" : "No");
	printer.PrintF(color, " - Direction     %s\n", GetPerformanceDirString(dir));
	printer.PrintF(color, " - Need In Place %s\n", options.m_requireInPlace ? "Yes" : "No");
	printer.PrintF(color, " - Need LOS      %s\n", options.m_requireLos ? "Yes" : "No");
	printer.PrintF(color, " - Distance      %.2f\n", distance);

	if (options.m_ignoreMotionClearance)
	{
		printer.PrintF(color, "   (Ignore Motion Clearance)\n");
	}
	if (options.m_ignoreDemeanor)
	{
		printer.PrintF(color, "   (Ignore Demeanor)\n");
	}
	if (options.m_ignoreMotionType)
	{
		printer.PrintF(color, "   (Ignore Motion Type)\n");
	}
	if (options.m_allowNextQuadrantDir)
	{
		printer.PrintF(color, "   (Allow Next Quadrant Dir)\n");
	}
	if (options.m_allowAllDirs)
	{
		printer.PrintF(color, "   (Allow All Dirs)\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiPerformanceController::VisualizePerformance(const DC::PerformanceEntry& performance,
												   U32F index,
												   const Locator& finalApRefLoc,
												   const NavMesh* pCharMesh,
												   const NavMesh::ProbeParams& probeParams,
												   bool demeanorValid,
												   bool typeValid,
												   bool motionValid,
												   bool weaponValid,
												   bool pointingValid,
												   bool swimmingValid,
												   bool inPlaceValid,
												   bool dirValid,
												   bool isUnique,
												   bool distanceValid, F32 distanceMin, F32 distanceMax,
												   VisualizeLosInfo* pLosInfo,
												   TextPrinterParentSpace& printer,
												   Color charColor,
												   DebugPrimTime duration) const
{
	STRIP_IN_FINAL_BUILD;

	const Color kFailColor = kColorRed;

	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
	{
		return;
	}

	const BoundFrame charBf = pNavChar->GetBoundFrame();
	const Locator charLoc   = pNavChar->GetLocator();
	const Point charPos     = charLoc.GetPosition();

	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const SkeletonId skelId         = pNavChar->GetSkeletonId();

	const F32 offsetY    = RandomFloatRange(0.5f, 1.0f);
	const Vector offset1 = offsetY * Vector(kUnitYAxis);
	const Vector offset2 = SafeNormalize(Vector(RandomFloatRange(-1.0, 1.0f), RandomFloatRange(0.0f, 1.0f), RandomFloatRange(-1.0f, 1.0f)), kUnitZAxis);

	// Set up for error text
	printer.Start(charPos + offset1 + offset2);
	printer.SetTextScale(0.5f);

	const StringId64 animId  = pAnimControl->LookupAnimId(performance.m_animIds->m_array[m_currentStage]);
	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();
	if (!pAnim)
	{
		g_prim.Draw(DebugLine(charPos + offset1, charPos + offset1 + offset2, kFailColor), duration);
		printer.PrintF(kFailColor, "Animation not found: %s [%s]\n", DevKitOnly_StringIdToString(animId), DevKitOnly_StringIdToString(performance.m_animIds->m_array[m_currentStage]));
		return;
	}

	const bool nondirectional = performance.m_direction & DC::kPerformanceDirectionMaskNondirectional;
	const bool mirror = performance.m_mirror;

	Locator initApRefLoc;
	if (nondirectional)
	{
		initApRefLoc = charLoc;
	}
	else if (!FindApReferenceFromAlign(skelId, pAnim, charLoc, &initApRefLoc, 0.0f, mirror))
	{
		g_prim.Draw(DebugLine(charPos + offset1, charPos + offset1 + offset2, kFailColor), duration);
		printer.PrintF(kFailColor, "'apReference' channel not found: %s [%s]\n", DevKitOnly_StringIdToString(animId), DevKitOnly_StringIdToString(performance.m_animIds->m_array[m_currentStage]));
		return;
	}

	DC::SelfBlendParams selfBlend;
	if (const DC::SelfBlendParams* pDcSb = NavUtil::GetSelfBlendParamsForAnim(pAnim->GetNameId()))
	{
		selfBlend = *pDcSb;
	}
	else
	{
		selfBlend.m_curve = DC::kAnimCurveTypeEaseIn;
		selfBlend.m_phase = 0.0f;
		selfBlend.m_time  = GetDuration(pAnim);
	}

	Locator finalLoc;
	if (nondirectional)
	{
		finalLoc = finalApRefLoc;
	}
	else if (!FindAlignFromApReference(skelId, pAnim, 1.0f, finalApRefLoc, SID("apReference"), &finalLoc, mirror))
	{
		g_prim.Draw(DebugLine(charPos + offset1, charPos + offset1 + offset2, kFailColor), duration);
		printer.PrintF(kFailColor, "'align' or 'apReference' channel not found: %s [%s]\n", DevKitOnly_StringIdToString(animId), DevKitOnly_StringIdToString(performance.m_animIds->m_array[m_currentStage]));
		return;
	}

	const Point finalPos  = finalLoc.GetPosition();

	// Set up for actual debug info
	printer.Start(finalPos + offset1 + offset2);
	printer.SetTextScale(0.5f);

	g_prim.Draw(DebugLine(finalPos + VECTOR_LC(0.0f, 0.2f, 0.0), finalPos + offset1 + offset2, charColor), duration);

	printer.PrintF(charColor, "[%d] %s [%s]\n", index, DevKitOnly_StringIdToString(animId), DevKitOnly_StringIdToString(performance.m_animIds->m_array[m_currentStage]));

	const Color demeanorColor = demeanorValid ? charColor : kFailColor;
	printer.PrintF(demeanorColor, " - Demeanor  ");
	if (performance.m_demeanor & DC::kPerformanceDemeanorMaskAny)
	{
		printer.PrintF(demeanorColor, "Any ");
	}
	else
	{
		if (performance.m_demeanor & DC::kPerformanceDemeanorMaskAmbient)
			printer.PrintF(demeanorColor, "Ambient ");
		if (performance.m_demeanor & DC::kPerformanceDemeanorMaskUneasy)
			printer.PrintF(demeanorColor, "Uneasy ");
		if (performance.m_demeanor & DC::kPerformanceDemeanorMaskAggressive)
			printer.PrintF(demeanorColor, "Aggressive ");
		if (performance.m_demeanor & DC::kPerformanceDemeanorMaskSearch)
			printer.PrintF(demeanorColor, "Search ");
	}
	printer.PrintF(demeanorColor, "\n");

	const Color typeColor = typeValid ? charColor : kFailColor;
	StringBuilder<256> performanceTypeSb;
	DC::GetPerformanceTypeMaskString(performance.m_type, &performanceTypeSb, " ");
	printer.PrintF(typeColor, " - Type      ");
	printer.PrintF(typeColor, performanceTypeSb.c_str());
	printer.PrintF(typeColor, "\n");

	const Color motionColor = motionValid ? charColor : kFailColor;
	printer.PrintF(motionColor, " - Motion    ");
	if (performance.m_motion & DC::kPerformanceMotionMaskAny)
	{
		printer.PrintF(motionColor, "Any ");
	}
	else
	{
		if (performance.m_motion & DC::kPerformanceMotionMaskIdle)
			printer.PrintF(motionColor, "Idle ");
		if (performance.m_motion & DC::kPerformanceMotionMaskWalk)
			printer.PrintF(motionColor, "Walk ");
		if (performance.m_motion & DC::kPerformanceMotionMaskRun)
			printer.PrintF(motionColor, "Run ");
	}
	printer.PrintF(motionColor, "\n");

	const Color weaponColor = weaponValid ? charColor : kFailColor;
	printer.PrintF(weaponColor, " - Weapon    ");
	if (performance.m_weapon & DC::kPerformanceWeaponMaskAny)
	{
		printer.PrintF(weaponColor, "Any ");
	}
	else
	{
		if (performance.m_weapon & DC::kPerformanceWeaponMaskHolstered)
			printer.PrintF(weaponColor, "Holstered ");
		if (performance.m_weapon & DC::kPerformanceWeaponMaskUnholstered)
			printer.PrintF(weaponColor, "Unholstered");
	}
	printer.PrintF(weaponColor, "\n");

	const Color pointingColor = pointingValid ? charColor : kFailColor;
	printer.PrintF(pointingColor, " - Pointing  %s\n", performance.m_pointing & DC::kPerformancePointingMaskAny ? "Any" : performance.m_pointing & DC::kPerformancePointingMaskYes ? "Yes" : "No");

	const Color swimmingColor = swimmingValid ? charColor : kFailColor;
	printer.PrintF(swimmingColor, " - Swimming  %s\n", performance.m_swimming ? "Yes" : "No");

	const Color inPlaceColor = inPlaceValid ? charColor : kFailColor;
	printer.PrintF(inPlaceColor, " - In Place  %s\n", performance.m_inPlace ? "Yes" : "No");

	const Color dirColor = dirValid ? charColor : kFailColor;
	printer.PrintF(dirColor, " - Direction %s\n", GetPerformanceDirString(performance.m_direction));

	/*
	const bool hasClearMotion = AI::AnimHasClearMotion(skelId,
													   pAnim,
													   initApRefLoc,
													   finalApRefLoc,
													   &selfBlend,
													   pCharMesh,
													   probeParams,
													   AI::AnimClearMotionDebugOptions(true, offsetY, duration));
	*/

	const bool hasClearMotion =
		performance.m_inPlace
		|| PerformanceHasClearMotion(pNavChar, performance, m_finalApRefLocWs, true);

	const Color clearMotionColor = hasClearMotion ? charColor : kFailColor;
	printer.PrintF(clearMotionColor," - Blocked   %s\n", hasClearMotion ? "No" : "Yes");

	printer.PrintF(charColor, " - Unique    %s\n", isUnique ? "Yes" : "No");

	if (pLosInfo)
	{
		StringBuilder<64> enoughLosClearDistStr("Enough Clearance Dist (%.2f > %.2f)", pLosInfo->m_clearDist, m_performanceOptions.m_minLosClearDist);

		const Color losColor = !pLosInfo->m_tested || pLosInfo->m_clear ? charColor : kFailColor;
		printer.PrintF
		(
			losColor, " - LOS       %s\n",
			(
				pLosInfo->m_tested
					? pLosInfo->m_clear
						? "Clear"
						: pLosInfo->m_enoughClearDist
							? enoughLosClearDistStr.c_str()
							: "Failed"
					: "Not Tested"
			)
		);

		if (pLosInfo->m_tested)
		{
			g_prim.Draw(DebugLine(pLosInfo->m_probeStart, pLosInfo->m_probeEnd, pLosInfo->m_clear ? kColorGreen : kColorRed), duration);
			if (!pLosInfo->m_clear)
				g_prim.Draw(DebugCross(pLosInfo->m_probeHit, 0.5f, kColorRed), duration);
		}
	}

	const Color distanceColor = distanceValid ? charColor : kFailColor;
	StringBuilder<64> distStr;
	distStr.append(" - Distance  [");
	if (distanceMin < 0.0f)
		distStr.append("-");
	else
		distStr.append_format("%.2f", distanceMin);
	distStr.append(", ");
	if (distanceMax < 0.0f)
		distStr.append("-");
	else
		distStr.append_format("%.2f", distanceMax);
	distStr.append("]\n");
	printer.PrintF(distanceColor, distStr.c_str());
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiPerformanceController::PlayAnim(const PlayPerformanceAnimParams& params)
{
	m_selectedPerformance = AvailablePerformance();
	return PlayAnimInternal(params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiPerformanceController::PlayAnimInternal(const PlayPerformanceAnimParams& params)
{
	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
		return false;

	DC::PerformanceEntry tempDcDef;
	if (params.m_blendTime >= 0.0f)
	{
		tempDcDef.m_animBlendTime = params.m_blendTime;
		tempDcDef.m_moveBlendTime = params.m_blendTime;
	}

	if (params.m_exitPhase >= 0.0f)
	{
		tempDcDef.m_exitPhase = params.m_exitPhase;
	}

	const DC::PerformanceEntry* pDcDef = params.m_pDcDef ? params.m_pDcDef : &tempDcDef;

	AnimControl* pAnimControl  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	StringId64 animId = params.m_animId;
	const ArtItemAnim* pArtItem = pAnimControl->LookupAnim(animId).ToArtItem();
	if (!pArtItem)
	{
		MsgAnimErr("-----------------------------------------------------------------\n");
		MsgAnimErr("Char (%s) tried to play a performance that doesn't exist!\n%s\n", pNavChar->GetName(), DevKitOnly_StringIdToString(animId));
		MsgAnimErr("-----------------------------------------------------------------\n");
		return false;
	}

	AI::SetPluggableAnim(pNavChar, params.m_animId, params.m_mirror);

	const StringId64 stateId = params.m_gestureId != INVALID_STRING_ID_64
								? SID("s_performance-w-gesture")
								: params.m_customApRefId
									? SID("s_performance-ap")
									: SID("s_performance");

	AiLogAnim(pNavChar,
			  "AiPerformanceController::PlayAnim Playing Performance Anim '%s'%s ap:%s\n",
			  DevKitOnly_StringIdToString(animId),
			  params.m_mirror ? " (Flipped)" : "",
			  PrettyPrint(params.m_frame));

	SendEvent(SID("stop-animating"), pNavChar, DC::kAnimateLayerAll, 0.0f, INVALID_STRING_ID_64);

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	// Setup the pluggable parameters

	if (DC::AnimNpcInfo* pInfo = pAnimControl->Info<DC::AnimNpcInfo>())
	{
		const StringId64 dtGroupId = SID("hit-exit^idle");
		pInfo->m_hitReactionGesture = params.m_gestureId;
		pInfo->m_hitReactionDtgroup = dtGroupId;
	}

	// Play the animation

	FadeToStateParams fadeParams;
	fadeParams.m_stateStartPhase = 0.0f;
	fadeParams.m_apRef		  = params.m_frame;
	fadeParams.m_apRefValid	  = true;
	fadeParams.m_animFadeTime = pDcDef->m_animBlendTime;
	fadeParams.m_motionFadeTime = pDcDef->m_moveBlendTime >= 0.0f ? pDcDef->m_moveBlendTime : pDcDef->m_animBlendTime;
	fadeParams.m_customApRefId	= params.m_customApRefId;

	m_fullBodyAnimAction.FadeToState(pAnimControl, stateId, fadeParams, AnimAction::kFinishOnAnimEnd);
	m_interruptNavigation = true;
	m_allowAdditiveHitReact = params.m_allowAdditiveHitReact;

	// Setup the navigation handoff
	m_navHandoff = NavAnimHandoffDesc();
	m_navHandoff.SetStateChangeRequestId(m_fullBodyAnimAction.GetStateChangeRequestId(), stateId);

	if (params.m_pNavHandoff)
	{
		m_navHandoff.ConfigureFromDc(params.m_pNavHandoff);
	}
	else
	{
		m_navHandoff.m_steeringPhase = -1.0f;
	}

	if (params.m_exitToMovingValid)
	{
		if (params.m_exitToMoving)
		{
			m_navHandoff.m_motionType = pNavChar->GetRequestedMotionType();
		}
		else
		{
			m_navHandoff.m_motionType = kMotionTypeMax;
		}
	}

	pNavChar->ConfigureNavigationHandOff(m_navHandoff, FILE_LINE_FUNC);

	m_exitPhase = pDcDef->m_exitPhase;
	m_holsterPhase = pDcDef->m_holsterPhase;
	m_unholsterPhase = pDcDef->m_unholsterPhase;
	m_holsterSlow = pDcDef->m_holsterSlow;
	m_unholsterSlow = pDcDef->m_unholsterSlow;
	m_allowGestures = params.m_allowGestures;

	m_lastPerformanceStartedTime = pNavChar->GetCurTime();
	m_lastPerformanceActiveTime = pNavChar->GetCurTime();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiPerformanceController* CreateAiPerformanceController()
{
	return NDI_NEW AiPerformanceController;
}
