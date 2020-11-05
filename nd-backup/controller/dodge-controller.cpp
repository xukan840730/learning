/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "game/ai/controller/dodge-controller.h"

#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/random.h"

#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/render/util/text.h"
#include "ndlib/util/bitarray128.h"
#include "ndlib/util/finite-state-machine.h"

#include "gamelib/gameplay/ai/base//nd-ai-util.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/component/nd-ai-anim-config.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/scriptx/h/moving-dodge-defines.h"

#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/coordinator/encounter-coordinator.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AiDodgeController : public IAiDodgeController
{
public:
	struct StandingDodgeParams
	{
		StringId64 m_animId;
		BoundFrame m_rotatedApRef;
	};

	class BaseState : public Fsm::TState<AiDodgeController>
	{
	public:
		virtual void OnEnter() override = 0;
		virtual void RequestAnimations() = 0;
		virtual void UpdateStatus() = 0;
		virtual bool RequestDodge(Point_arg sourcePosWs, Vector_arg aimDirWs = Vector(kZero), bool force = false) { return false; }
		virtual bool RequestDive(Point_arg sourcePos, Vector_arg attackDir = Vector(kZero), bool force = false) { return false; }

		virtual bool IsBusy() const { return false; }
		virtual bool ShouldInterruptNavigation() const { return false; }
		virtual void DebugDraw() const {}
		virtual U64 CollectHitReactionStateFlags() const { return 0; }

		NavCharacter* GetCharacter() { return GetSelf()->GetCharacter(); }
		const NavCharacter* GetCharacter() const { return GetSelf()->GetCharacter(); }
	};

	typedef Fsm::TStateMachine<BaseState> StateMachine;
	typedef IAiDodgeController ParentClass;

	U32F GetMaxStateAllocSize() { return 512; }
	const Clock* GetClock() const { return GetCharacter()->GetClock(); }

	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	void GoInitialState() { GoInactive(); }

	virtual void Shutdown() override;
	virtual void Reset() override;			// Reset to a working default state
	virtual void Interrupt() override;		// Interrupt the controller to prevent any further changes

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual void EnableDodges(bool enabled) override;

	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;

	virtual void ConfigureCharacter(Demeanor demeanor, const DC::NpcDemeanorDef* pDemeanorDef, const NdAiAnimationConfig* pAnimConfig) override;

	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;

	virtual bool IsBusy() const override;

	virtual bool ShouldInterruptNavigation() const override;
	virtual U64 CollectHitReactionStateFlags() const override;

	virtual bool RequestDodge(Point_arg sourcePosWs, Vector_arg aimDirWs = Vector(kZero), bool force = false) override;
	virtual TimeFrame GetLastDodgedTime() const override;

	virtual bool RequestDive(Point_arg sourcePos, Vector_arg attackDir = Vector(kZero), bool force = false) override;
	virtual TimeFrame GetLastDiveTime() const override;

	FSM_BASE_DECLARE(StateMachine);

	FSM_STATE_DECLARE(Inactive);
	FSM_STATE_DECLARE_ARG(MovingDodge, DC::MovingDodge);
	FSM_STATE_DECLARE_ARG(StandingDodge, StandingDodgeParams);

	StringId64 m_movingDodgesTableId;

	TimeFrame m_lastDodgeTime;
	TimeFrame m_lastDiveTime;

	bool m_enabled;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	ParentClass::Init(pNavChar, pNavControl);

	m_lastDodgeTime = TimeFrameNegInfinity();
	m_lastDiveTime = TimeFrameNegInfinity();
	m_enabled = true;

	const size_t maxArgSize = sizeof(StandingDodgeParams);
	const bool goInitialState = true;
	GetStateMachine().Init(this, FILE_LINE_FUNC, goInitialState, maxArgSize);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::Shutdown()
{
	ParentClass::Shutdown();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::Reset()
{
	ParentClass::Reset();

	GetStateMachine().RemoveNewState();
	GoInactive();

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::Interrupt()
{
	ParentClass::Interrupt();

	GetStateMachine().RemoveNewState();

	GoInactive();

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	GetStateMachine().Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::EnableDodges(bool enabled)
{
	m_enabled = enabled;
	if (!m_enabled)
	{
		Reset();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::RequestAnimations()
{
	ParentClass::RequestAnimations();

	if (BaseState* pState = GetState())
	{
		pState->RequestAnimations();
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::UpdateStatus()
{
	if (BaseState* pState = GetState())
	{
		pState->UpdateStatus();
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::ConfigureCharacter(Demeanor demeanor, const DC::NpcDemeanorDef* pDemeanorDef, const NdAiAnimationConfig* pAnimConfig)
{
	m_movingDodgesTableId = pAnimConfig->GetMovingDodgesSetId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	ParentClass::DebugDraw(pPrinter);

	if (const BaseState* pState = GetState())
	{
		pState->DebugDraw();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiDodgeController::IsBusy() const
{
	bool busy = false;

	if (const BaseState* pState = GetState())
	{
		busy = pState->IsBusy();
	}

	return busy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiDodgeController::ShouldInterruptNavigation() const
{
	bool interrupt = false;

	if (const BaseState* pState = GetState())
	{
		interrupt = pState->ShouldInterruptNavigation();
	}

	return interrupt;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 AiDodgeController::CollectHitReactionStateFlags() const
{
	U64 flags = 0;

	if (const BaseState* pState = GetState())
	{
		flags = pState->CollectHitReactionStateFlags();
	}

	return flags;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiDodgeController::RequestDodge(Point_arg sourcePosWs, Vector_arg aimDirWs, bool force)
{
	NavCharacter* pNavChar = GetCharacter();

	bool success = false;

	if (!m_enabled)
		return success;

	if (BaseState* pState = GetState())
	{
		success = pState->RequestDodge(sourcePosWs, aimDirWs, force);
	}

	if (success)
	{
		GetStateMachine().TakeStateTransition();

		m_lastDodgeTime = pNavChar->GetCurTime();
	}

	return success;
}

bool AiDodgeController::RequestDive(Point_arg sourcePos,
									Vector_arg attackDir /* = Vector(kZero) */,
									bool force /* = false */)
{
	// stub
	NavCharacter* pNavChar = GetCharacter();

	bool success = false;
	if (BaseState* pState = GetState())
	{
		// MsgCon("NPC Would Dive! Success!\n");
		success = pState->RequestDive(sourcePos, attackDir, force);
	}
	if (success)
	{
		GetStateMachine().TakeStateTransition();
		m_lastDiveTime = pNavChar->GetCurTime();
	}
	return success;
}

TimeFrame AiDodgeController::GetLastDiveTime() const {
	return m_lastDiveTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
TimeFrame AiDodgeController::GetLastDodgedTime() const
{
	return m_lastDodgeTime;
}

/************************************************************************/
/*                                                                      */
/************************************************************************/

class AiDodgeController::Inactive : public AiDodgeController::BaseState
{
	virtual void OnEnter() override {}
	virtual void RequestAnimations() override {}
	virtual void UpdateStatus() override {}
	virtual bool RequestDodge(Point_arg sourcePosWs, Vector_arg aimDirWs = Vector(kZero), bool force = false) override;
	virtual bool RequestDive(Point_arg sourcePos, Vector_arg attackDir = Vector(kZero), bool force = false) override;
	bool RequestStandingDodge(Point_arg sourcePosWs, Vector_arg aimDirWs = Vector(kZero), bool force = false);
	bool RequestMovingDodge(Point_arg sourcePosWs, Vector_arg aimDirWs = Vector(kZero), bool force = false);
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiDodgeController::Inactive::RequestDodge(Point_arg sourcePosWs, Vector_arg aimDirWs, bool force)
{
	AiDodgeController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimationControllers* pAnimControllers = pNavChar ? pNavChar->GetAnimationControllers() : nullptr;
	IAiLocomotionController* pLocomotionController = pAnimControllers ? pAnimControllers->GetLocomotionController() : nullptr;

	bool success = false;

	if (!pNavChar->IsBusy())
	{
		const bool isRunning = (pNavChar->GetCurrentMotionType() >= kMotionTypeRun);
		if (pNavChar->IsMoving() && isRunning)
		{
			success = RequestMovingDodge(sourcePosWs, aimDirWs, force);
		}
		else
		{
			success = RequestStandingDodge(sourcePosWs, aimDirWs, force);
		}
	}

	// become busy for the rest of the frame
	if (success)
		pSelf->GetStateMachine().TakeStateTransition();

	return success;
}

bool AiDodgeController::Inactive::RequestDive(Point_arg sourcePos,
											  Vector_arg attackDir /* = Vector(kZero) */,
											  bool force /* = false */)
{
	// stub
	// todo Harold
	AiDodgeController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();

	const float kMaxAngleDot = Cos(DegreesToRadians(60.0f));

	const AnimControllerConfig* pConfig = static_cast<const AnimControllerConfig*>(pNavChar->GetAnimControllerConfig());
	const StringId64 setId = pConfig->m_gameHitReaction.m_diveReactionSetId;

	const DC::SymbolArray* pDodgeAnims = ScriptManager::Lookup<DC::SymbolArray>(setId, nullptr);
	if (!pDodgeAnims)
		return false;

	const NavControl* pNavControl = pNavChar->GetNavControl();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();

	ScopedTempAllocator sta(FILE_LINE_FUNC);

	struct DodgeEntry
	{
		StringId64 m_animId;
		BoundFrame m_apRef;
	};

	const BoundFrame& myFrame = pNavChar->GetBoundFrame();
	const Point myPosWs = myFrame.GetTranslationWs();
	const Vector toSourceDirWs = SafeNormalize(VectorXz(sourcePos - myPosWs), kZero);
	F32 dodgeSign = Cross(toSourceDirWs, -attackDir).Y();
	if (Abs(dodgeSign) < 0.1f) // too small of a margin, either direction is fine
		dodgeSign = RandomFloatRange(-1.0f, 1.0f);
	const Vector desiredDodgeDir = dodgeSign > 0.0f ? RotateY90(toSourceDirWs) : RotateYMinus90(toSourceDirWs);

	DodgeEntry* pEntries = NDI_NEW DodgeEntry[pDodgeAnims->m_count];
	U32F numEntries = 0;

	for (U32F ii = 0; ii < pDodgeAnims->m_count; ++ii)
	{
		const StringId64 animId = pDodgeAnims->m_array[ii];

		Locator animEndWs;
		if (!FindAlignFromApReference(pAnimControl, animId, 1.0f, myFrame.GetLocatorWs(), &animEndWs))
			continue;

		const Vector toAnimEndWs = SafeNormalize(VectorXz(animEndWs.Pos() - myPosWs), toSourceDirWs);
		const F32 dotP = DotXz(toAnimEndWs, desiredDodgeDir);

		//g_prim.Draw(DebugArrow(myPosWs, myPosWs + desiredDodgeDir, kColorOrange), kPrimDuration1FramePauseable);

		if (dotP > kMaxAngleDot && (pNavControl->ClearLineOfMotionPs(myPosWs, animEndWs.Pos()) || force))
		{
			//g_prim.Draw(DebugArrow(myPosWs, myPosWs + toAnimEndWs, kColorGreen), kPrimDuration1FramePauseable);

			const Quat rotAdjustWs = QuatFromVectors(toAnimEndWs, desiredDodgeDir);

			pEntries[numEntries].m_animId = animId;
			pEntries[numEntries].m_apRef = myFrame;
			//pEntries[numEntries].m_apRef.AdjustRotationWs(rotAdjustWs);
			++numEntries;
		}
		else
		{
			//g_prim.Draw(DebugArrow(myPosWs, myPosWs + toAnimEndWs, kColorRed), kPrimDuration1FramePauseable);
		}
	}

	if (numEntries == 0)
		return RequestDodge(sourcePos, attackDir); //super cheezy fallback to dodge

	const U32F selectedEntry = RandomIntRange(0, numEntries - 1);

	StandingDodgeParams params;
	params.m_animId = pEntries[selectedEntry].m_animId;
	params.m_rotatedApRef = pEntries[selectedEntry].m_apRef;

	pSelf->GoStandingDodge(params);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct MovingDodgeCandidate
{
	const DC::MovingDodge* m_pMovingDodge;
	F32 m_moveDist;
	MovingDodgeCandidate() : m_pMovingDodge(nullptr), m_moveDist(-1.0f) { }
};

static I32 CompareMovingDodgeDist(const MovingDodgeCandidate& a, const MovingDodgeCandidate& b)
{
	if (a.m_moveDist > b.m_moveDist)
		return -1;
	else if (a.m_moveDist < b.m_moveDist)
		return +1;
	return 0;
}

bool AiDodgeController::Inactive::RequestMovingDodge(Point_arg sourcePosWs, Vector_arg aimDirWs, bool force)
{
	AiDodgeController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();

	const DC::MovingDodgeTable* pMovingDodgeTable = ScriptManager::Lookup<DC::MovingDodgeTable>(pSelf->m_movingDodgesTableId, nullptr);
	if (!pMovingDodgeTable)
		return false;

	NavMeshReadLockJanitor navMeshReadLock(FILE_LINE_FUNC);
	ScopedTempAllocator alloc(FILE_LINE_FUNC);

	MovingDodgeCandidate* aMovingDodgeCandidate = NDI_NEW MovingDodgeCandidate[pMovingDodgeTable->m_movingDodgeCount];
	U32 numMovingDodgeCandidates = 0;

	const DC::MovingDodge* pFurthestMovingDodge = nullptr;
	F32 furthestMovingDodgeDistSqr = -1.0f;

	const bool debug = FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_dodge.m_displayMovingDodges);
	const SkeletonId skelId = pNavChar->GetSkeletonId();
	const Locator navCharApRefWs = pNavChar->GetLocator();
	const Point navCharPosWs = navCharApRefWs.Pos();
	const Locator& navCharParentSpace = pNavChar->GetParentSpace();
	const NavMesh* pNavMesh = pNavChar->GetNavLocation().ToNavMesh();
	NavMesh::ProbeParams probeParams; // use default for now (linear probes, not radial)

	const Vector kDebugOffsetY(0.0f, 0.1f, 0.0f);

	for (U32 i = 0; i < pMovingDodgeTable->m_movingDodgeCount; ++i)
	{
		const DC::MovingDodge& movingDodge = pMovingDodgeTable->m_movingDodges[i];

		const ArtItemAnim* pAnim = pNavChar->GetAnimControl()->LookupAnim(movingDodge.m_animId).ToArtItem();
		if (!pAnim)
			continue;

		bool movingDodgeValid = true;

		// anim has clear motion?
		if (movingDodgeValid)
		{
			movingDodgeValid = AI::AnimHasClearMotion(skelId, pAnim, navCharApRefWs, navCharApRefWs, nullptr, pNavMesh, probeParams, false, AI::AnimClearMotionDebugOptions(debug));
		}

		// enough room ahead of anim end?
		Locator preAnimEndLocWs, animEndLocWs;
		Point preAnimEndPosWs, animEndPosWs;
		Vector animEndDirWs;
		NavLocation animEndNavLocWs;
		const NavPoly* pAnimEndNavPoly = nullptr;
		const NavMesh* pAnimEndNavMesh = nullptr;
		if (movingDodgeValid)
		{
			movingDodgeValid = false;

			const float preEndPhase = 1.0f - pAnim->m_pClipData->m_phasePerFrame;
			if (FindAlignFromApReference(skelId, pAnim, preEndPhase, navCharApRefWs, SID("apReference"), &preAnimEndLocWs) && FindAlignFromApReference(skelId, pAnim, 1.0f, navCharApRefWs, SID("apReference"), &animEndLocWs))
			{
				animEndPosWs = animEndLocWs.Pos();
				animEndNavLocWs = NavUtil::ConstructNavLocation(animEndPosWs, NavLocation::Type::kNavPoly, 0.0f);
				preAnimEndPosWs = preAnimEndLocWs.Pos();
				pAnimEndNavPoly = animEndNavLocWs.ToNavPoly();
				pAnimEndNavMesh = pAnimEndNavPoly ? pAnimEndNavPoly->GetNavMesh() : nullptr;
				animEndDirWs = SafeNormalize(animEndPosWs - preAnimEndPosWs, GetLocalZ(navCharApRefWs));
				if (pAnimEndNavMesh)
				{
					const Point animEndForwardPosWs = animEndPosWs + 2.5f * animEndDirWs;

					NavMesh::ProbeParams params;
					params.m_start = pAnimEndNavMesh->WorldToLocal(animEndPosWs);
					params.m_move = pAnimEndNavMesh->WorldToLocal(animEndForwardPosWs) - params.m_start;
					params.m_pStartPoly = pAnimEndNavPoly;

					const bool clearEndMotion = (pAnimEndNavMesh->ProbeLs(&params) == NavMesh::ProbeResult::kReachedGoal);

					if (debug)
					{
						const Color color = clearEndMotion ? kColorGreen : kColorRed;

						if (!clearEndMotion)
						{
							g_prim.Draw(DebugString(Lerp(animEndPosWs, animEndForwardPosWs, 0.5f) + kDebugOffsetY, "Not enough forward room", kFontCenter, kColorRed, 0.5f), kPrimDuration1FramePauseable);
						}

						g_prim.Draw(DebugSphere(animEndPosWs + kDebugOffsetY, 0.05f, color), kPrimDuration1FramePauseable);
						g_prim.Draw(DebugLine(animEndPosWs + kDebugOffsetY, animEndForwardPosWs + kDebugOffsetY, color, 2.0f), kPrimDuration1FramePauseable);
					}

					movingDodgeValid = clearEndMotion || force;
				}
			}
		}

		// clear forward motion back to path?
		if (movingDodgeValid)
		{
			movingDodgeValid = false;

			AI_ASSERT(pAnimEndNavMesh);

			const IPathWaypoints* pPathPs = pNavChar->GetPathPs();
			if (pPathPs && pPathPs->GetWaypointCount() > 0)
			{
				const Point pathEndPosPs = pPathPs->GetEndWaypoint();
				const Point pathEndPosWs = navCharParentSpace.TransformPoint(pathEndPosPs);
				const Point spherePosPs = pNavChar->GetTranslationPs();
				const float sphereRadius = Dist(navCharPosWs, animEndPosWs) + 1.5f; // a bit more forward
				const float navCharToPathEndDist = Dist(navCharPosWs, pathEndPosWs);
				if (navCharToPathEndDist > sphereRadius)
				{
					Point posOnPathPs;
					if (NavUtil::FindPathSphereIntersection(pPathPs, spherePosPs, sphereRadius, &posOnPathPs) >= 0)
					{
						bool clearMotionBackToPath = false;

						const Point posOnPathWs = navCharParentSpace.TransformPoint(posOnPathPs);
						const Vector animEndPosToPathDirWs = SafeNormalize(posOnPathWs - animEndPosWs, animEndDirWs);
						const Vector navCharToPathDirWs = SafeNormalize(posOnPathWs - navCharPosWs, GetLocalZ(navCharApRefWs));
						const float animEndDirDot = Dot(animEndPosToPathDirWs, navCharToPathDirWs);
						if (animEndDirDot > 0.5f) // maintain forward motion?
						{
							NavMesh::ProbeParams params;
							params.m_start = pAnimEndNavMesh->WorldToLocal(animEndPosWs);
							params.m_move = pAnimEndNavMesh->WorldToLocal(posOnPathWs) - params.m_start;
							params.m_pStartPoly = pAnimEndNavPoly;

							clearMotionBackToPath = (pAnimEndNavMesh->ProbeLs(&params) == NavMesh::ProbeResult::kReachedGoal);

							if (debug)
							{
								const Color color = clearMotionBackToPath ? kColorGreen : kColorRed;
								g_prim.Draw(DebugLine(animEndPosWs + kDebugOffsetY, posOnPathWs + kDebugOffsetY, color, 2.0f), kPrimDuration1FramePauseable);
							}
						}
						else if (debug)
						{
							g_prim.Draw(DebugString(Lerp(animEndPosWs, posOnPathWs, 0.5f) + kDebugOffsetY, "Can't maintain forward motion", kFontCenter, kColorRed, 0.5f), kPrimDuration1FramePauseable);
							g_prim.Draw(DebugLine(animEndPosWs + kDebugOffsetY, posOnPathWs + kDebugOffsetY, kColorRed, 2.0f), kPrimDuration1FramePauseable);
						}

						movingDodgeValid = clearMotionBackToPath | force;
					}
					else if (debug)
					{
						g_prim.Draw(DebugString(Lerp(animEndPosWs, pathEndPosWs, 0.5f) + kDebugOffsetY, "Can't find resume pos on path", kFontCenter, kColorRed, 0.5f), kPrimDuration1FramePauseable);
						g_prim.Draw(DebugLine(animEndPosWs + kDebugOffsetY, pathEndPosWs + kDebugOffsetY, kColorRed, 2.0f), kPrimDuration1FramePauseable);
					}
				}
				else if (debug)
				{
					g_prim.Draw(DebugString(Lerp(animEndPosWs, pathEndPosWs, 0.5f) + kDebugOffsetY, "Path not long enough", kFontCenter, kColorRed, 0.5f), kPrimDuration1FramePauseable);
					g_prim.Draw(DebugLine(animEndPosWs + kDebugOffsetY, pathEndPosWs + kDebugOffsetY, kColorRed, 2.0f), kPrimDuration1FramePauseable);
				}
			}
		}

		// draw anim name
		if (debug)
		{
			Locator animNameLoc;
			if (FindAlignFromApReference(skelId, pAnim, 0.5f, navCharApRefWs, SID("apReference"), &animNameLoc))
			{
				g_prim.Draw(DebugString(animNameLoc.Pos() + kDebugOffsetY, DevKitOnly_StringIdToStringOrHex(movingDodge.m_animId), kFontCenter, movingDodgeValid ? kColorGreen : kColorRed, 0.755), kPrimDuration1FramePauseable);
			}
		}

		if (movingDodgeValid)
		{
			MovingDodgeCandidate& newCandidate = aMovingDodgeCandidate[numMovingDodgeCandidates++];
			newCandidate.m_pMovingDodge = &movingDodge;
			newCandidate.m_moveDist = Dist(navCharPosWs, animEndPosWs);
		}
	}

	if (numMovingDodgeCandidates == 0)
		return false;

	QuickSort(aMovingDodgeCandidate, numMovingDodgeCandidates, CompareMovingDodgeDist);

	I32 numMovingDodgesToConsider = numMovingDodgeCandidates;
	const F32 furthestMovingDodgeDist = aMovingDodgeCandidate[0].m_moveDist;
	for (U32 i = 1; i < numMovingDodgeCandidates; ++i)
	{
		if (aMovingDodgeCandidate[i].m_moveDist < furthestMovingDodgeDist - 1.0f) // a little leeway
		{
			numMovingDodgesToConsider = i;
			break;
		}
	}

	const U32 iSelectedMovingDodge = RandomIntRange(0, numMovingDodgesToConsider - 1);
	const DC::MovingDodge& selectedMovingDodge = *(aMovingDodgeCandidate[iSelectedMovingDodge].m_pMovingDodge);

	pSelf->GoMovingDodge(selectedMovingDodge);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiDodgeController::Inactive::RequestStandingDodge(Point_arg sourcePosWs, Vector_arg aimDirWs, bool force)
{
	AiDodgeController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();

	const float kMaxAngleDot = Cos(DegreesToRadians(60.0f));

	const AnimControllerConfig* pConfig = static_cast<const AnimControllerConfig*>(pNavChar->GetAnimControllerConfig());
	const StringId64 setId = pConfig->m_gameHitReaction.m_dodgeReactionSetId;

	const DC::SymbolArray* pDodgeAnims = ScriptManager::Lookup<DC::SymbolArray>(setId, nullptr);
	if (!pDodgeAnims)
		return false;

	const NavControl* pNavControl	= pNavChar->GetNavControl();
	const AnimControl* pAnimControl	= pNavChar->GetAnimControl();

	ScopedTempAllocator sta(FILE_LINE_FUNC);

	struct DodgeEntry
	{
		StringId64 m_animId;
		BoundFrame m_apRef;
	};

	const BoundFrame& myFrame = pNavChar->GetBoundFrame();
	const Point myPosWs = myFrame.GetTranslationWs();
	const Vector toSourceDirWs = SafeNormalize(VectorXz(sourcePosWs - myPosWs), kZero);
	F32 dodgeSign = Cross(toSourceDirWs, -aimDirWs).Y();
	if (Abs(dodgeSign) < 0.1f) // too small of a margin, either direction is fine
		dodgeSign = RandomFloatRange(-1.0f, 1.0f);
	const Vector desiredDodgeDir = dodgeSign > 0.0f ? RotateY90(toSourceDirWs) : RotateYMinus90(toSourceDirWs);

	DodgeEntry* pEntries = NDI_NEW DodgeEntry[pDodgeAnims->m_count];
	U32F numEntries = 0;

	for (U32F ii = 0; ii < pDodgeAnims->m_count; ++ii)
	{
		const StringId64 animId = pDodgeAnims->m_array[ii];

		Locator animEndWs;
		if (!FindAlignFromApReference(pAnimControl, animId, 1.0f, myFrame.GetLocatorWs(), &animEndWs))
			continue;

		const Vector toAnimEndWs = SafeNormalize(VectorXz(animEndWs.Pos() - myPosWs), toSourceDirWs);
		const F32 dotP = DotXz(toAnimEndWs, desiredDodgeDir);

		//g_prim.Draw(DebugArrow(myPosWs, myPosWs + desiredDodgeDir, kColorOrange), kPrimDuration1FramePauseable);

		if (dotP > kMaxAngleDot && (pNavControl->ClearLineOfMotionPs(myPosWs, animEndWs.Pos()) || force))
		{
			//g_prim.Draw(DebugArrow(myPosWs, myPosWs + toAnimEndWs, kColorGreen), kPrimDuration1FramePauseable);

			const Quat rotAdjustWs = QuatFromVectors(toAnimEndWs, desiredDodgeDir);

			pEntries[numEntries].m_animId = animId;
			pEntries[numEntries].m_apRef = myFrame;
			//pEntries[numEntries].m_apRef.AdjustRotationWs(rotAdjustWs);
			++numEntries;
		}
		else
		{
			//g_prim.Draw(DebugArrow(myPosWs, myPosWs + toAnimEndWs, kColorRed), kPrimDuration1FramePauseable);
		}
	}

	if (numEntries == 0)
		return false;

	const U32F selectedEntry = RandomIntRange(0, numEntries - 1);

	StandingDodgeParams params;
	params.m_animId = pEntries[selectedEntry].m_animId;
	params.m_rotatedApRef = pEntries[selectedEntry].m_apRef;

	pSelf->GoStandingDodge(params);

	return true;
}

FSM_STATE_REGISTER(AiDodgeController, Inactive, kPriorityMedium);

/************************************************************************/
/*                                                                      */
/************************************************************************/

class AiDodgeController::MovingDodge : public AiDodgeController::BaseState
{
	virtual void OnEnter() override;
	virtual bool IsBusy() const override;
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual bool ShouldInterruptNavigation() const override;
	virtual U64 CollectHitReactionStateFlags() const override;

	AnimAction m_dodgeAction;
	MotionType m_savedMotionType;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::MovingDodge::OnEnter()
{
	AiDodgeController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const DC::MovingDodge& movingDodge = GetStateArg<DC::MovingDodge>();

	AI::SetPluggableAnim(pNavChar, movingDodge.m_animId);

	const BoundFrame& apRef = pNavChar->GetBoundFrame();

	m_savedMotionType = pNavChar->GetCurrentMotionType();

	FadeToStateParams params;
	params.m_apRef = apRef;
	params.m_apRefValid = true;
	params.ApplyBlendParams(movingDodge.m_blendIn);

	m_dodgeAction.FadeToState(pAnimControl,
							  SID("s_moving-dodge"),
							  params,
							  AnimAction::kFinishOnNonTransitionalStateReached);

	NavAnimHandoffDesc handoff;
	handoff.SetStateChangeRequestId(m_dodgeAction.GetStateChangeRequestId(), SID("s_moving-dodge"));
	handoff.m_motionType = kMotionTypeMax;

	pNavChar->ConfigureNavigationHandOff(handoff, FILE_LINE_FUNC);
}

/// ------------------------------------------------------------------------------------------------------x--------- ///
bool AiDodgeController::MovingDodge::IsBusy() const
{
	const AiDodgeController* pSelf = GetSelf();
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const DC::MovingDodge& movingDodge = GetStateArg<DC::MovingDodge>();

	return !m_dodgeAction.IsDone() && m_dodgeAction.GetAnimPhase(pAnimControl) < movingDodge.m_exitPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::MovingDodge::RequestAnimations()
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	m_dodgeAction.Update(pAnimControl);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::MovingDodge::UpdateStatus()
{
	AiDodgeController* pSelf = GetSelf();
	if (!IsBusy())
		pSelf->GoInactive();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiDodgeController::MovingDodge::ShouldInterruptNavigation() const
{
	return IsBusy();
}

/// ------------------------------------------------------------------------------------------------------x--------- ///
U64 AiDodgeController::MovingDodge::CollectHitReactionStateFlags() const
{
	switch (m_savedMotionType)
	{
	case kMotionTypeWalk:	return DC::kHitReactionStateMaskWalking;
	case kMotionTypeRun:	return DC::kHitReactionStateMaskRunning;
	case kMotionTypeSprint:	return DC::kHitReactionStateMaskSprinting;
	}

	return DC::kHitReactionStateMaskIdle;
}

FSM_STATE_REGISTER_ARG(AiDodgeController, MovingDodge, kPriorityMedium, DC::MovingDodge);

/************************************************************************/
/*                                                                      */
/************************************************************************/

class AiDodgeController::StandingDodge : public AiDodgeController::BaseState
{
	virtual void OnEnter() override;
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual bool IsBusy() const override;
	virtual bool ShouldInterruptNavigation() const override { return true; }
	virtual U64 CollectHitReactionStateFlags() const override { return DC::kHitReactionStateMaskIdle; }

	AnimActionWithSelfBlend m_dodgeAction;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::StandingDodge::OnEnter()
{
	AiDodgeController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const StandingDodgeParams& params = GetStateArg<StandingDodgeParams>();

	AI::SetPluggableAnim(pNavChar, params.m_animId);

	const BoundFrame& initialApRef = pNavChar->GetBoundFrame();

	FadeToStateParams fadeParams;
	fadeParams.m_apRef = initialApRef;
	fadeParams.m_apRefValid = true;
	fadeParams.m_animFadeTime = g_aiGameOptions.m_dodge.m_standingAnimFadeTime;
	fadeParams.m_motionFadeTime = g_aiGameOptions.m_dodge.m_standingMotionFadeTime;

	m_dodgeAction.FadeToState(pAnimControl,
							  SID("s_standing-dodge"),
							  fadeParams,
							  AnimAction::kFinishOnNonTransitionalStateReached);

	NavAnimHandoffDesc handoff;
	handoff.SetStateChangeRequestId(m_dodgeAction.GetStateChangeRequestId(), SID("s_standing-dodge"));
	handoff.m_motionType = kMotionTypeMax;

	pNavChar->ConfigureNavigationHandOff(handoff, FILE_LINE_FUNC);
}

/// ------------------------------------------------------------------------------------------------------x--------- ///
bool AiDodgeController::StandingDodge::IsBusy() const
{
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::StandingDodge::RequestAnimations()
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	m_dodgeAction.Update(pAnimControl);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiDodgeController::StandingDodge::UpdateStatus()
{
	AiDodgeController* pSelf = GetSelf();
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const AnimStateInstance* pDestInst = m_dodgeAction.GetTransitionDestInstance(pAnimControl);
	if (m_dodgeAction.WasTransitionTakenThisFrame() && pDestInst)
	{
		const ArtItemAnim* pAnim = pDestInst->GetPhaseAnimArtItem().ToArtItem();
		if (pAnim)
		{
			const StandingDodgeParams& params = GetStateArg<StandingDodgeParams>();

			const float duration = GetDuration(pAnim);
			DC::SelfBlendParams sbParams;
			sbParams.m_phase = 0.0f;
			sbParams.m_time = duration;
			sbParams.m_curve = DC::kAnimCurveTypeUniformS;
			m_dodgeAction.SetSelfBlendParams(&sbParams, params.m_rotatedApRef, pNavChar);
		}
	}

	if (m_dodgeAction.IsDone())
	{
		pSelf->GoInactive();
	}
}

FSM_STATE_REGISTER_ARG(AiDodgeController, StandingDodge, kPriorityMedium, AiDodgeController::StandingDodgeParams);

/// --------------------------------------------------------------------------------------------------------------- ///
IAiDodgeController* CreateAiDodgeController()
{
	return NDI_NEW AiDodgeController;
}
